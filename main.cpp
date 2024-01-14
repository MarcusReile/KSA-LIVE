
    #include <csignal>
    #include <fstream>
    #include <iostream>
    #include "cxxopts.hpp"
    #include "api/BRVBIControl.h"
    #include <iomanip>
    #include <unistd.h>



    static bool cancelFlag;                 //for programm abortion

    void CancelHandler(int)
    {
        std::cout << std::endl << "Abort..." << std::endl;
        cancelFlag = true;
    }



    int main(int argc, char **argv) {

        BRVBIControl 	control;
        int				nRet;               //Return Value for BRVBI functions
        uint32_t		dwMessageIndex;
        uint32_t 		dwMessageCount;
        short*			iqData;
        float*          iqDataFloat;

        std::string 	ipAddress = "192.168.1.111";
        std::string		iqFilename = "KSA_LIVE_1_CB.iq";
        std::string     multicastAddress = "232.1.1.111:40001";
        double 			dCenterFrequencyMHz = 649.0;
        uint32_t 		dwSamplingClock = 7.68e6;
        uint32_t 		dwAcquisitionSize = 2048000;  //in Samples
        uint32_t		dwIQDataSize =204800;        //size Blockbuffer
        uint32_t		dwTimeout = 1000;
        const float     scale = 32768.0f;


        std::cout << std::endl << "### KSA LIVE! ###" << std::endl << std::endl;

        //1.Step: Read the Config File and establish the BR-VBI Connection


        std::ifstream configFile("config.txt");

        if (!configFile) {
            std::cerr << "Could not open Configuration File" << std::endl;
            return 1;
        }

        std::string line;

        // Search the file each line at a time for key words
        while(std::getline(configFile, line)) {
            // Config. File Format is key=value; search for "=" and put delimiter there
            size_t delimiterPos = line.find('=');

            if (delimiterPos != std::string::npos) { //if delimiter is in the line, take what you found before and after the delimiter
                std::string key = line.substr(0, delimiterPos);
                std::string value = line.substr(delimiterPos + 1);

                // Error routine: Die the user (accidently) add a space in front or at the end? -> exclude the space
                value = value.substr(value.find_first_not_of(" \t"));
                value = value.substr(0, value.find_last_not_of(" \t") + 1);
                // combine the Key with the corresponding Value
                if (key == "IPAdresse") {
                    ipAddress = value;
                } else if (key == "FrequenzMHz") {
                    dCenterFrequencyMHz = std::stod(value);
                } else if (key == "Multicast") {
                    multicastAddress = value;
                }
                //new config keys here
            }
        }


        configFile.close();

        // Check the values
        if (!ipAddress.empty()) {
            std::cout << "IP-Address: " << ipAddress << std::endl;
        } else {
            std::cerr << "IP-Address not found!" << std::endl;
            return 1;
        }

        if (dCenterFrequencyMHz > 0.0) {
            std::cout << "Frequenz (MHz): " << int(dCenterFrequencyMHz) << std::endl;
        } else {
            std::cerr << "Frequency not found or unvalid" << std::endl;
            return 1;
        }

        if (!multicastAddress.empty()) {
            std::cout << "Multicast-Address: " << ipAddress << std::endl;
        } else {
            std::cerr << "Multicast-Address not found!" << std::endl;
            return 1;
        }



        // connect to receiver
        nRet = control.init(ipAddress, dCenterFrequencyMHz, dwSamplingClock, dwAcquisitionSize);
        if(nRet) {
            std::cout << "Initialization failed!" << std::endl;
            exit(EXIT_FAILURE);
        }
        std::cout << "Init complete" << std::endl;


        //2. Step: Start VLC and Modem

        // VLC startup command including M'Cast Address (" &" for programm continuation)
        std::string vlcCommand = "vlc udp://" + multicastAddress +" &";

        // basically like a Terminal input
        int result = std::system(vlcCommand.c_str());

        if (result != 0) {
            std::cerr << "Error starting VLC." << std::endl;
        }



        // start the Modem
        const char* modemCommand = "modem -f /home/marcus/BR-VBI_API/BR-VBI_API/KSA_LIVE_1_CB.iq &";
        int resultModem = std::system(modemCommand);

        if (result == 0) {
            std::cout << "Modem wurde erfolgreich gestartet." << std::endl;
        } else {
            std::cerr << "Error starting Modem" << std::endl;
        }


        // 3. Step: Start stream
        nRet = control.startStream();
        if(nRet) {
            std::cout << "Start stream failed!" << std::endl;
            exit(EXIT_FAILURE);
        }
        std::cout << "Streaming started" << std::endl;

        // open file
        std::ofstream outputFile(iqFilename, std::ios::binary | std::ios::trunc);

        // set up cancellation option
        cancelFlag = false;
        signal(SIGINT, CancelHandler);


        // prepare IQ data buffer
        iqData = (short*)calloc(dwIQDataSize*2, sizeof(short));
        iqDataFloat = (float*)calloc(dwIQDataSize*2, sizeof(float));



        // start RX
        dwMessageCount = 0;
        dwTimeout = dwIQDataSize < dwSamplingClock ? 2000 : (dwIQDataSize / dwSamplingClock + 2) * 1000;
        std::cout << "Data reception started, press 'Ctrl+C' to terminate..." << std::endl;


        // Start RX-Loop

        while (!cancelFlag)
        {
            // Request data
            nRet = control.getStreamData(dwTimeout, dwIQDataSize, iqData);
            if (nRet == dwIQDataSize)
            {
                // Convert, scale
                for(int i=0; i < dwIQDataSize*2; i++)
                {
                    iqDataFloat[i]=static_cast<float>(iqData[i]);
                    iqDataFloat[i]= iqDataFloat[i]/scale;
                }
                //...and copy to file
                outputFile.write((const char*)&iqDataFloat[0], dwIQDataSize*2*sizeof(float));

                dwMessageCount++;

                if (!cancelFlag)
                {
                    std::cout << "Data blocks written: " << dwMessageCount << '\r' << std::flush;
                }
            }

        }

        std::cout << std::endl;

        // stop stream
        nRet = control.stopStream();
            if(nRet) {
                std::cout << "Stop stream failed!" << std::endl;
                exit(EXIT_FAILURE);
            }

        // close iq file
        outputFile.flush();
        outputFile.close();
        free(iqData);

        // reset cancellation procedure to default
        signal(SIGINT, SIG_DFL);

        std::cout << "Terminated successfully" << std::endl;
        return EXIT_SUCCESS;
    }


