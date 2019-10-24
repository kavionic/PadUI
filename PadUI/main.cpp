/*
 * PadUI.cpp
 *
 * Created: 19/10/04 23:37:37
 * Author : kurts
 */ 


#include "sam.h"
#include "Kernel/HAL/SAME70System.h"
#include "SystemSetup.h"
#include "component/tc.h"


#include "Kernel/VFS/KBlockCache.h"
#include "Kernel/Drivers/HSMCIDriver.h"
#include "Kernel/Drivers/UARTDriver.h"
#include "Kernel/Drivers/I2CDriver.h"
#include "Kernel/Drivers/FT5x0xDriver.h"
#include "Kernel/Drivers/INA3221Driver.h"
#include "Kernel/Drivers/BME280Driver.h"
#include "DeviceControl/INA3221.h"
#include "DeviceControl/BME280.h"
#include "DeviceControl/I2C.h"


#include "Kernel/FSDrivers/FAT/FATFilesystem.h"
#include "Kernel/VFS/KFilesystem.h"
#include "Kernel/KPowerManager.h"
#include "ApplicationServer/ApplicationServer.h"
#include "Applications/TestApp/TestApp.h"
#include "Applications/WindowManager/WindowManager.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

ApplicationServer* g_ApplicationServer;

int main(void)
{
    __disable_irq();

    SAME70System::EnablePeripheralClock(ID_PIOA);
    SAME70System::EnablePeripheralClock(ID_PIOB);
    SAME70System::EnablePeripheralClock(ID_PIOC);
    SAME70System::EnablePeripheralClock(ID_PIOD);
    SAME70System::EnablePeripheralClock(ID_PIOE);

    SAME70System::EnablePeripheralClock(SYSTEM_TIMER_PERIPHERALID1);
    SAME70System::EnablePeripheralClock(SYSTEM_TIMER_PERIPHERALID2);
    
    kernel::Kernel::Initialize(CLOCK_CPU_FREQUENCY, &SYSTEM_TIMER->TcChannel[SYSTEM_TIMER_SPIN_TIMER_L], &SYSTEM_TIMER->TcChannel[SYSTEM_TIMER_POWER_SW_TIMER], POWER_SWITCH_Pin);
    return 0;
}




///////////////////////////////////////////////////////////////////////////////
/// \author Kurt Skauen
///////////////////////////////////////////////////////////////////////////////

static Ptr<kernel::UARTDriver> g_UARTDriver = ptr_new<kernel::UARTDriver>();
static void SetupDevices()
{
    g_UARTDriver->Setup("uart/0", kernel::UART::Channels::Channel4_D18D19);
//    kernel::Kernel::RegisterDevice("uart/0", ptr_new<kernel::UARTDriver>(kernel::UART::Channels::Channel4_D18D19));

    int file = open("/dev/uart/0", O_WRONLY);
    
    if (file >= 0)
    {
        if (file != 0) {
            FileIO::Dupe(file, 0);
        }
        if (file != 1) {
            FileIO::Dupe(file, 1);
        }
        if (file != 2) {
            FileIO::Dupe(file, 2);
        }
        if (file != 0 && file != 1 && file != 2) {
            close(file);
        }
        write(0, "Testing testing\n", strlen("Testing testing\n"));
    }
}

///////////////////////////////////////////////////////////////////////////////
/// \author Kurt Skauen
///////////////////////////////////////////////////////////////////////////////

/*void RunTests(void* args)
{
    Tests tests;
    exit_thread(0);
}*/

///////////////////////////////////////////////////////////////////////////////
/// \author Kurt Skauen
///////////////////////////////////////////////////////////////////////////////

bool PrintFileContent(const char* format, const char* path)
{
    int testFile = FileIO::Open(path, O_RDONLY);
    if (testFile != -1)
    {
        std::vector<char> buffer;
        buffer.resize(1024);
        
        int length = FileIO::Read(testFile, 0, buffer.data(), buffer.size() - 1);
        if (length >= 0) {
            buffer[length] = 0;
            printf(format, buffer.data());
        } else {
            printf(format, "*ERROR READING*");            
        }
        FileIO::Close(testFile);
        return true;
    }
    else
    {
        printf(format, "*ERROR OPENING*");
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////
/// \author Kurt Skauen
///////////////////////////////////////////////////////////////////////////////

static void TestFilesystem()
{
    int dir = FileIO::Open("/sdcard", O_RDONLY);
    if (dir >= 0)
    {
        printf("Listing SD-card\n");
        kernel::dir_entry entry;
        for (int i = 0; FileIO::ReadDirectory(dir, &entry, sizeof(entry)) == 1; ++i) {
            printf("%02d: '%s'\n", i, entry.d_name);            
        }
    }
    int testFile = FileIO::Open("/sdcard/TestTextFile.txt", O_RDONLY);
    if (testFile != -1)
    {
        printf("Succeeded opening file\n");
        char buffer[512];
        int length = FileIO::Read(testFile, 0, buffer, sizeof(buffer) - 1);
        if (length >= 0) {
            buffer[length] = 0;
            printf("Content: '%s'\n", buffer);
        }
        FileIO::Close(testFile);
    }
    
    if (PrintFileContent("Existing write test file: '%s'\n", "/sdcard/WriteTestFile.txt"))
    {
        if (FileIO::Unlink("/sdcard/WriteTestFile.txt") >= 0) {
            printf("Successfully removed the previous write test file.\n");
        } else {            
            printf("ERROR: Failed to remove old write test file.\n");
        }        
    }
    
    testFile = FileIO::Open("/sdcard/WriteTestFile.txt", O_RDWR | O_CREAT);
    if (testFile != -1)
    {
        String buffer("Test write string");
        if (FileIO::Write(testFile, buffer.c_str(), buffer.size()) != buffer.size()) {
            printf("ERROR: Failed to write test file: %s\n", strerror(get_last_error()));
        }
        FileIO::Close(testFile);
    }
    PrintFileContent("Write test content: '%s'\n", "/sdcard/WriteTestFile.txt");
}
///////////////////////////////////////////////////////////////////////////////
/// \author Kurt Skauen
///////////////////////////////////////////////////////////////////////////////

Ptr<kernel::I2CDriver>     g_I2CDriver;
Ptr<kernel::FT5x0xDriver>  g_FT5x0xDriver;
Ptr<kernel::INA3221Driver> g_INA3221Driver;
Ptr<kernel::BME280Driver>  g_BME280Driver;
Ptr<kernel::HSMCIDriver>   g_HSMCIDriver;


void ApplicationMain()
{
    SetupDevices();

    printf("Initialize display.\n");

    kernel::GfxDriver::Instance.InitDisplay(LCD_REGISTERS, LCD_RESET_Pin, LCD_TP_RESET_Pin, LCD_BL_CTRL_Pin);

    uint32_t resetStatus = RSTC->RSTC_SR;
    if ((resetStatus & RSTC_SR_RSTTYP_Msk) == RSTC_SR_RSTTYP_GENERAL_RST_Val)
    {
        KPowerManager::Shutdown();
    }

    g_I2CDriver = ptr_new<kernel::I2CDriver>();
    g_I2CDriver->Setup("i2c/0", kernel::I2CDriverINode::Channels::Channel0);
    g_I2CDriver->Setup("i2c/2", kernel::I2CDriverINode::Channels::Channel2);
    printf("Setup I2C drivers\n");

    g_FT5x0xDriver = ptr_new<kernel::FT5x0xDriver>();
    g_FT5x0xDriver->Setup("ft5x0x/0", LCD_TP_WAKE_Pin, LCD_TP_RESET_Pin, LCD_TP_INT_Pin, "/dev/i2c/0");
    
    g_INA3221Driver = ptr_new<kernel::INA3221Driver>();
    g_INA3221Driver->Setup("ina3221/0", "/dev/i2c/2");
    
    g_BME280Driver = ptr_new<kernel::BME280Driver>();
    g_BME280Driver->Setup("bme280/0", "/dev/i2c/2");
    
    g_HSMCIDriver = ptr_new<kernel::HSMCIDriver>(DigitalPin(e_DigitalPortID_A, 24));
    g_HSMCIDriver->Setup("sdcard/");
    
    INA3221ShuntConfig shuntConfig;
    shuntConfig.ShuntValues[INA3221_SENSOR_IDX_1] = 47.0e-3;
    shuntConfig.ShuntValues[INA3221_SENSOR_IDX_2] = 47.0e-3;
    shuntConfig.ShuntValues[INA3221_SENSOR_IDX_3] = 47.0e-3 * 0.5;

    int currentSensor = open("/dev/ina3221/0", O_RDWR);
    if (currentSensor >= 0) {
        INA3221_SetShuntConfig(currentSensor, shuntConfig);
        close(currentSensor);
    } else {
        printf("ERROR: Failed to open current sensor for configuration.\n");
    }

//    static int32_t args[] = {1, 2, 3};
//    spawn_thread("Tester", RunTests, 0, args);

//    printf("Start I2S clock output\n");
    SAME70System::EnablePeripheralClock(ID_PMC);
/*    PMC->PMC_WPMR = PMC_WPMR_WPKEY_PASSWD; // | PMC_WPMR_WPEN_Msk; // Remove register write protection.
    PMC->PMC_PCK[1] = PMC_PCK_CSS_MAIN_CLK | PMC_PCK_PRES(0);
    PMC->PMC_SCER = PMC_SCER_PCK1_Msk;
    PMC->PMC_WPMR = PMC_WPMR_WPKEY_PASSWD | PMC_WPMR_WPEN_Msk; // Write protect registers.
    DigitalPort::SetPeripheralMux(e_DigitalPortID_A, PIN21_bm, DigitalPort::PeripheralID::B); // PCK1
    */

    
    CHARGER_OTG_Pin.Write(true);
    CHARGER_OTG_Pin.SetDirection(DigitalPinDirection_e::Out);
    
    CHARGER_CHG_ENABLE_Pin.SetDirection(DigitalPinDirection_e::Out);
    CHARGER_CHG_ENABLE_Pin = false;

/*    int charger = open("dev/i2c/2", O_RDWR);
    
    if (charger >= 0)
    {
        I2CIOCTL_SetSlaveAddress(charger, 0x6b);
        I2CIOCTL_SetInternalAddrLen(charger, 1);
        
        FileIO::Write(charger, 0, 0x33);
    }*/



    snooze(bigtime_from_ms(1000));
#if 0
    std::vector<uint8_t> buffer1;
    std::vector<uint8_t> buffer2;
    
    for (int i = 0; i < 512 / 2; ++i) {
        buffer1.push_back((i+42) << 8);
        buffer1.push_back((i+42));
    }
    buffer2.resize(buffer1.size());
    
    int dev = FileIO::Open("/dev/sdcard/0", O_RDWR);
    if (dev >= 0)
    {
        off64_t pos = 1024LL*1024LL*1024LL*16LL;
        if (FileIO::Write(dev, pos, buffer1.data(), buffer1.size()) == buffer1.size())
        {
            if (FileIO::Read(dev, pos, buffer2.data(), buffer2.size()) == buffer2.size())
            {
                for (size_t i = 0; i < buffer1.size(); ++i)
                {
                    if (buffer1[i] != buffer2[i]) {
                        kprintf("ERROR: Failed to write block %" PRIu64 ". Mismatch at %d\n", pos, i);
                        break;
                    }
                }
            }            
        }
    }
#else
    printf("Initializeing the FAT filesystem driver\n");
    FileIO::RegisterFilesystem("fat", ptr_new<kernel::FATFilesystem>());

    printf("Mounting SD-card\n");
    
    FileIO::CreateDirectory("/sdcard", 0777);
    FileIO::Mount("/dev/sdcard/0", "/sdcard", "fat", 0, nullptr, 0);

    TestFilesystem();    
#endif


    printf("Start Application Server.\n");

    g_ApplicationServer = new ApplicationServer();
    g_ApplicationServer->Start("appserver");
    
    WindowManager* windowManager = new WindowManager();
    windowManager->Start("window_manager");

    Application* testApp = new TestApp();
    
    testApp->Start("test_app");

    exit_thread(0);    
}
