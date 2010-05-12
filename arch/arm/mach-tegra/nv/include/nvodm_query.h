/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * @file
 * <b>NVIDIA Tegra ODM Kit:
 *         ODM Query API</b>
 *
 * @b Description: Defines a set of query functions for ODMs that may be
 *                 accessed at boot-time, runtime, or anywhere in between.
 */

#ifndef INCLUDED_NVODM_QUERY_H
#define INCLUDED_NVODM_QUERY_H

/**
 * @defgroup groupODMQueryAPI ODM Query API
 * @ingroup nvodm_query
 * @{
 */

#include "nvcommon.h"
#include "nvodm_modules.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * Defines the memory types for which configuration data may be retrieved.
 */
typedef enum
{
    /// Specifies SDRAM memory; target memory for runtime image and heap.
    NvOdmMemoryType_Sdram,

    /// Specifies NAND ROM; storage (may include the bootloader).
    NvOdmMemoryType_Nand,

    /// Specifies NOR ROM; storage (may include the bootloader).
    NvOdmMemoryType_Nor,

    /// Specifies EEPROM; storage (may include the bootloader).
    NvOdmMemoryType_I2CEeprom,

    /// Specifies HSMMC NAND; storage (may include the bootloader).
    NvOdmMemoryType_Hsmmc,

    /// Memory mapped I/O device.
    NvOdmMemoryType_Mio,

    /// Specifies DPRAM memory
    NvOdmMemoryType_Dpram,

    NvOdmMemoryType_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmMemoryType_Force32 = 0x7FFFFFFF
} NvOdmMemoryType;

/**
 * Defines the devices that can serve as the debug console.
 */
typedef enum
{
    /// Specifies that the debug console is undefined.
    NvOdmDebugConsole_Undefined,

    /// Specifies that no debug console is to be used.
    NvOdmDebugConsole_None,

    /// Specifies that the ARM Debug Communication Channel
    /// (Dcc) port is the debug console
    NvOdmDebugConsole_Dcc,

    /// Specifies that UART-A is the debug console.
    NvOdmDebugConsole_UartA,

    /// Specifies that UART-B is the debug console.
    NvOdmDebugConsole_UartB,

    /// Specifies that UART-C is the debug console.
    NvOdmDebugConsole_UartC,

    /// Specifies that UART-D is the debug console (not available on AP15/AP16).
    NvOdmDebugConsole_UartD,

    /// Specifies that UART-E is the debug console (not available on AP15/AP16).
    NvOdmDebugConsole_UartE,

    NvOdmDebugConsole_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmDebugConsole_Force32 = 0x7FFFFFFF
} NvOdmDebugConsole;


/**
 * Defines the devices that can serve as the download transport.
 */
typedef enum
{
    /// Specifies that the download transport is undefined.
    NvOdmDownloadTransport_Undefined = 0,

    /// Specifies that no download transport device is to be used.
    NvOdmDownloadTransport_None,

    /// Specifies that an ODM-specific external Ethernet adapter
    /// is the download transport device.
    NvOdmDownloadTransport_MioEthernet,

    /// Deprecated name -- retained for backward compatibility.
    NvOdmDownloadTransport_Ethernet = NvOdmDownloadTransport_MioEthernet,

    /// Specifies that USB is the download transport device.
    NvOdmDownloadTransport_Usb,

    /// Specifies that SPI (Ethernet) is the download transport device.
    NvOdmDownloadTransport_SpiEthernet,

    /// Deprecated name -- retained for backward compatibility.
    NvOdmDownloadTransport_Spi = NvOdmDownloadTransport_SpiEthernet,

    /// Specifies that UART-A is the download transport device.
    NvOdmDownloadTransport_UartA,

    /// Specifies that UART-B is the download transport device.
    NvOdmDownloadTransport_UartB,

    /// Specifies that UART-C is the download transport device.
    NvOdmDownloadTransport_UartC,

    /// Specifies that UART-D is the download transport device (not available on AP15/AP16).
    NvOdmDownloadTransport_UartD,

    /// Specifies that UART-E is the download transport device (not available on AP15/AP16).
    NvOdmDownloadTransport_UartE,

    NvOdmDownloadTransport_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmDownloadTransport_Force32 = 0x7FFFFFFF
} NvOdmDownloadTransport;

/** 
 * Contains information and settings for the display (such as the default 
 * backlight intensity level). 
 */ 
typedef struct 
{ 
    /// Default backlight intensity (scaled from 0 to 255). 
    NvU8 BacklightIntensity; 
} NvOdmQueryDisplayInfo; 

/**
 * Defines the SPI signal mode for SPI communications to the device.
 */
typedef enum
{
    /// Specifies the invalid signal mode.
    NvOdmQuerySpiSignalMode_Invalid = 0x0,

    /// Specifies mode 0 (CPOL=0, CPHA=0) of SPI controller.
    NvOdmQuerySpiSignalMode_0,

    /// Specifies mode 1 (CPOL=0, CPHA=1) of SPI controller.
    NvOdmQuerySpiSignalMode_1,

    /// Specifies mode 2 (CPOL=1, CPHA=0) of SPI controller.
    NvOdmQuerySpiSignalMode_2,

    /// Specifies mode 3 (CPOL=1, CPHA=1) of SPI controller.
    NvOdmQuerySpiSignalMode_3,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmQuerySpiSignalMode_Force32 = 0x7FFFFFFF
} NvOdmQuerySpiSignalMode;

/**
 * Holds the SPI device information.
 */
typedef struct
{
    /// Holds the signal mode for the SPI interfacing.
    NvOdmQuerySpiSignalMode SignalMode;

    /// If this is NV_TRUE, then this device's chip select is an active-low signal
    /// (the device is selected by driving its chip select line low). If this
    /// is NV_FALSE, then this device's chip select is an active-high signal.
    NvBool ChipSelectActiveLow;

    /// If this is NV_TRUE, then this device is an SPI slave.
    NvBool IsSlave;
} NvOdmQuerySpiDeviceInfo;

/**
 * Defines the SPI signal state in idle state, i.e., when no transaction is going on.
 */
typedef struct
{
    /// Specifies the signal idle state, whether it is normal or tristate.
    NvBool IsTristate;

    /// Specifies the signal mode for idle state.
    NvOdmQuerySpiSignalMode SignalMode;

    /// Specifies the idle state data out level.
    NvBool IsIdleDataOutHigh;

} NvOdmQuerySpiIdleSignalState;

/**
 * Defines the SDIO slot usage.
 */
typedef enum
{
    /** Unused interface. */
    NvOdmQuerySdioSlotUsage_unused = 0x0,

    /** Specifies a Wireless LAN device. */
    NvOdmQuerySdioSlotUsage_wlan = 0x1,

    /** Specifies the boot slot (contains the operating system code,
     *  typically populated by an eMMC). */
    NvOdmQuerySdioSlotUsage_Boot = 0x2,
    
    /** Specifies the media slot, used for user data like audio/video/images. */
    NvOdmQuerySdioSlotUsage_Media = 0x4,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmQuerySdioSlotUsage_Force32 = 0x7FFFFFFF,

} NvOdmQuerySdioSlotUsage;

/**
 * Holds the SDIO interface properties.
 */
typedef struct
{
    /// Holds a flag indicating whether or not the eMMC card connected to the
    /// SDIO interface is pluggable on the board.
    ///
    /// @note If a GPIO is already assigned by NvOdmGpioPinGroup::NvOdmGpioPinGroup_Sdio,
    /// then this value is ignored. 
    ///
    /// If this is NV_TRUE, the eMMC card is pluggable on the board.
    /// If this is NV_FALSE, the eMMC card is fixed permanently (or soldered) on the board.
    /// For more information, see NvDdkSdioIsCardInserted().
    NvBool IsCardRemovable;

    /// Holds SDIO card HW settling time after reset, i.e., before reading the OCR.
    NvU32 SDIOCardSettlingDelayMSec;

    /// Indicates to the driver whether the card must be re-enumerated after returning
    /// from suspend or deep sleep modes, because of power loss to the card during those
    /// modes. NV_TRUE means that the card is powered even though the device enters
    /// suspend or deep sleep mode, and there is no need to re-enumerate the card after
    /// returning from suspend/deep sleep.
    NvBool AlwaysON;

    /// Indicates the tap delay to adjust the track delay on the PCB/Boards from SOC to connector.
    NvU32 TapDelay;

    /// Defines what the slot is used for.
    NvOdmQuerySdioSlotUsage usage;

} NvOdmQuerySdioInterfaceProperty;

/**
 * Defines the bus width used by the HSMMC controller on the platform.
 */
typedef enum
{
    /// Specifies the invalid bus width.
    NvOdmQueryHsmmcBusWidth_Invalid = 0x0,

    /// Specifies 4-bit wide bus.
    NvOdmQueryHsmmcBusWidth_FourBitWide,

    /// Specifies 8-bit wide bus.
    NvOdmQueryHsmmcBusWidth_EightBitWide,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmQueryHsmmcBusWidth_Force32 = 0x7FFFFFFF
} NvOdmQueryHsmmcBusWidth;

/**
 * Holds the HSMMC interface properties.
 */
typedef struct
{
    /// Holds a flag to indicate whether or not the eMMC card connected to
    /// the HSMMC interface is pluggable on the board. Set this to NV_TRUE
    /// if the eMMC card is pluggable on the board. If the eMMC card is fixed
    /// permanently (or soldered) on the board, then set this variable to NV_FALSE.
    ///
    /// @note If a GPIO is already assigned by NvOdmGpioPinGroup::NvOdmGpioPinGroup_Hsmmc,
    /// then this value is ignored. 
    /// For more information, see NvDdkHsmmcIsCardInserted().
    NvBool IsCardRemovable;

    /// Holds the bus width supported by the platform for the HSMMC controller.
    NvOdmQueryHsmmcBusWidth Buswidth;
} NvOdmQueryHsmmcInterfaceProperty;

/**
* Defines the OWR device details.
*/ 
typedef struct
{
    /** Flag to indicate if the "Byte transfer mode" is supported for the given
     *  OWR device. If not supported for a given device, the driver uses
     *  "Bit transfer mode" for reading/writing data to the device.
     */
    NvBool IsByteModeSupported;

    /** Read data setup, Tsu = N owr clks, Range = tsu < 1. */
    NvU32 Tsu;
    /** Release 1-wire time, Trelease = N owr clks, Range = 0 <= trelease < 45. */
    NvU32 TRelease;
    /**  Read data valid time, Trdv = N+1 owr clks, Range = Exactly 15. */
    NvU32 TRdv;
    /** Write zero time low, Tlow0 = N+1 owr clks, Range = 60 <= tlow0 < tslot < 120. */
    NvU32 TLow0;
    /** Write one time low, or TLOWR both are same Tlow1 = N+1 owr clks, 
     *  Range = 1 <= tlow1 < 15 TlowR = N+1 owr clks, Range = 1 <= tlowR < 15.
     */
    NvU32 TLow1;        
    /** Active time slot for write or read data, Tslot = N+1 owr clks, 
     *  Range = 60 <= tslot < 120.
     */
    NvU32 TSlot;


    /** ::PRESENCE_DETECT_LOW Tpdl = N owr clks, Range = 60 <= tpdl < 240.  */
    NvU32 Tpdl;
    /** ::PRESENCE_DETECT_HIGH Tpdh = N+1 owr clks, Range = 15 <= tpdh < 60.  */
    NvU32 Tpdh;
    /** ::RESET_TIME_LOW Trstl = N+1 owr clks, Range = 480 <= trstl < infinity.  */
    NvU32 TRstl;        
    /** ::RESET_TIME_HIGH, Trsth = N+1 owr clks, Range = 480 <= trsth < infinity.  */
    NvU32 TRsth;            

    /** Program pulse width, Tpp = N owr clks Range = 480 to 5000.  */
    NvU32 Tpp;
    /** Program voltage fall time, Tfp = N owr clks Range = 0.5 to 5. */
    NvU32 Tfp;
    /** Program voltage rise time, Trp = N owr clks Range = 0.5 to 5. */
    NvU32 Trp;
    /** Delay to verify, Tdv = N owr clks, Range = > 5.  */
    NvU32 Tdv;    
    /** Delay to program, Tpd = N+1 owr clks, Range = > 5. */
    NvU32 Tpd;

    /** Should be less than or equal to (tlow1 - 6) clks, 6 clks are used for Deglitch,
     *  if Deglitch bypassed it is 3 clks.
     */
    NvU32 ReadDataSampleClk;
    /** Should be less than or equal to (tpdl - 6) clks, 6 clks are used for dglitch,
     *  if Deglitch bypassed it is 3 clks.
     */
    NvU32 PresenceSampleClk;

    /** OWR device memory address size. */
    NvU32 AddressSize;
    /** OWR device Memory size. */
    NvU32 MemorySize;
} NvOdmQueryOwrDeviceInfo;

/**
 * Defines the functional mode for the I2S channel.
 */
typedef enum
{
    /// Specifies the I2S controller will generate the clock.
    NvOdmQueryI2sMode_Master = 1,

    /// Specifies the I2S controller will not generate the clock;
    /// the audio codec will generate the clock.
    NvOdmQueryI2sMode_Slave,

    /// Specifies the I2S communication is internal to audio codec.
    NvOdmQueryI2sMode_Internal,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmQueryI2sMode_Force32 = 0x7FFFFFFF
} NvOdmQueryI2sMode;

/**
 * Defines the left and right channel data selection control for the audio.
 */
typedef enum
{
    /// Specifies the left channel when left/right line control signal is low.
    NvOdmQueryI2sLRLineControl_LeftOnLow = 1,

    /// Specifies the right channel when left/right line control signal is low.
    NvOdmQueryI2sLRLineControl_RightOnLow,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmQueryI2sLRLineControl_Force32 = 0x7FFFFFFF
} NvOdmQueryI2sLRLineControl;

/**
 * Defines the possible I2S data communication formats with the audio codec.
 */
typedef enum
{
    /// Specifies the I2S format for data communication.
    NvOdmQueryI2sDataCommFormat_I2S = 0x1,

    /// Specifies right-justified format for data communication.
    NvOdmQueryI2sDataCommFormat_RightJustified,

    /// Specifies left-justified format for data communication.
    NvOdmQueryI2sDataCommFormat_LeftJustified,

    /// Specifies DSP format for data communication.
    NvOdmQueryI2sDataCommFormat_Dsp,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmQueryI2sDataCommFormat_Force32 = 0x7FFFFFFF
} NvOdmQueryI2sDataCommFormat;


/**
 * Combines the one-time configuration property for the I2S interface with
 * audio codec.
 */
typedef struct
{
    /// Holds the I2S controller functional mode.
    NvOdmQueryI2sMode Mode;

    /// Holds the left and right channel control.
    NvOdmQueryI2sLRLineControl I2sLRLineControl;

    /// Holds the information about the I2S data communication format.
    NvOdmQueryI2sDataCommFormat  I2sDataCommunicationFormat;

    /// Specifies the codec needs a fixed MCLK when I2s acts as Master
    NvBool IsFixedMCLK;

    /// Specifies the Fixed MCLK Frequency in Khz.
    /// Supports only three fixed frequencies: 11289, 12288, and 12000.
    NvU32  FixedMCLKFrequency;
    
} NvOdmQueryI2sInterfaceProperty;

/**
 * Defines the left and right channel data selection control.
 */
typedef enum
{
    /// Specifies the left channel when left/right line control signal is low.
    NvOdmQuerySpdifDataCaptureControl_FromLeft = 1,

    /// Specifies the right channel when left/right line control signal is low.
    NvOdmQuerySpdifDataCaptureControl_FromRight,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmQuerySpdifDataCaptureControl_Force32 = 0x7FFFFFFF
} NvOdmQuerySpdifDataCaptureControl;

/**
 * Combines the one time configuration property for the SPDIF interface.
 */
typedef struct NvOdmQuerySpdifInterfacePropertyRec
{
    /// Holds the left and right channel control.
    NvOdmQuerySpdifDataCaptureControl SpdifDataCaptureControl;
} NvOdmQuerySpdifInterfaceProperty;

/**
 * Combines the one-time configuration property for the AC97 interface.
 */
typedef struct
{
    /// Identifies whether secondary codec is available.
    NvBool IsSecondoaryCodecAvailable;

    /// Identifies whether left/right surround sound is enabled.
    NvBool IsLRSurroundSoundEnable;

    /// Identifies whether LFE is enabled.
    NvBool IsLFEEnable;

    /// Identifies whether center speaker is enabled.
    NvBool IsCenterSpeakerEnable;

    /// Identifies whether left right PCM is enabled.
    NvBool IsLRPcmEnable;
} NvOdmQueryAc97InterfaceProperty;

/**
 * Combines the one-time configuration property for the audio codec interfaced
 * by I2S.
 */
typedef struct
{
    /// Holds whether the audio codec is in master mode or in slave mode.
    NvBool IsCodecMasterMode;

    /// Holds the dap port index used to connect to the codec.
    NvU32  DapPortIndex;

    /// Holds the device address if it is an I2C interface, else the chip
    /// select ID if it is an SPI interface.
    NvU32 DeviceAddress;

    /// Tells whether it is the USB mode or normal mode of interfacing for the
    /// audio codec.
    NvU32 IsUsbMode;

    /// Holds the left and right channel control.
    NvOdmQueryI2sLRLineControl I2sCodecLRLineControl;

    /// Holds the information about the I2S data communication format.
    NvOdmQueryI2sDataCommFormat  I2sCodecDataCommFormat;
} NvOdmQueryI2sACodecInterfaceProp;

/**
 * Defines the oscillator source.
 */
typedef enum
{
    /// Specifies the cyrstal oscillator as the clock source.
    NvOdmQueryOscillator_Xtal = 1,

    /// Specifies an external clock source (bypass mode).
    NvOdmQueryOscillator_External,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmQueryOscillator_Force32 = 0x7FFFFFFF
} NvOdmQueryOscillator;

/**
 *  Defines the wakeup polarity.
 */
typedef enum
{
    NvOdmWakeupPadPolarity_Low = 0,
    NvOdmWakeupPadPolarity_High,
    NvOdmWakeupPadPolarity_AnyEdge,
    NvOdmWakeupPadPolarity_Force32 = 0x7FFFFFFF
} NvOdmWakeupPadPolarity;

/** Defines the wakeup pad attributes. */
typedef struct
{
    /// Specifies to enable this pad as wakeup or not.
    NvBool  enable;

    /// Specifies the wake up pad number. Valid values for AP15 are 0 to 15.
    NvU32   WakeupPadNumber;

    /// Specifies wake up polarity.
    NvOdmWakeupPadPolarity Polarity;

} NvOdmWakeupPadInfo;

/**
 * Defines the index for possible connection based on the use case.
 */
typedef enum
{
    /// Specifies the default music path.
    NvOdmDapConnectionIndex_Music_Path = 0,

    /// Specifies the voice call without Bluetooth.
    NvOdmDapConnectionIndex_VoiceCall_NoBlueTooth = 1,
    
    /// Specifies the HD radio.
    NvOdmDapConnectionIndex_HD_Radio,

    /// Specifies the voice call with Bluetooth.
    NvOdmDapConnectionIndex_VoiceCall_WithBlueTooth,

    /// Specifies the Bluetooth to codec.
    NvOdmDapConnectionIndex_BlueTooth_Codec,

    /// Specifies DAC1-to-DAP2 bypass, used for h/w verification.
    NvOdmDapConnectionIndex_DAC1_DAP2,

    /// Specifies DAC1-to-DAP3 bypass, used for h/w verification.
    NvOdmDapConnectionIndex_DAC1_DAP3,

    /// Specifies DAC1-to-DAP4 bypass, used for h/w verification.
    NvOdmDapConnectionIndex_DAC1_DAP4,

    /// Specifies DAC2-to-DAP2 bypass, used for hardware verification.
    NvOdmDapConnectionIndex_DAC2_DAP2,

    /// Specifies DAC2-to-DAP3 bypass, used for hardware verification.
    NvOdmDapConnectionIndex_DAC2_DAP3,

    /// Specifies DAC2-to-DAP4 bypass, used for hardware verification.
    NvOdmDapConnectionIndex_DAC2_DAP4,

    /// Specifies a custom type connection.
    NvOdmDapConnectionIndex_Custom,

    /// Specifies unknown.
    NvOdmDapConnectionIndex_Unknown,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmDapConnectionIndex_Force32 = 0x7FFFFFFF

}NvOdmDapConnectionIndex;

/**
 * Defines the DAP port source and destination enumerations.
 */
typedef enum
{
    /// NONE DAP port - no connection.
    NvOdmDapPort_None = 0,

    /// Specifies DAP port 1.
    NvOdmDapPort_Dap1,

    /// Specifies DAP port 2.
    NvOdmDapPort_Dap2,

    /// Specifies DAP port 3.
    NvOdmDapPort_Dap3,

    /// Specifies DAP port 4.
    NvOdmDapPort_Dap4,

    /// Specifies DAP port 5.
    NvOdmDapPort_Dap5,

    /// Specifies I2S DAP port 1.
    NvOdmDapPort_I2s1,

    /// Specifies I2S DAP port 2.
    NvOdmDapPort_I2s2,

    /// Specifies AC97 DAP port.
    NvOdmDapPort_Ac97,

    /// Specifies baseband DAP port.
    NvOdmDapPort_BaseBand,

    /// Specifies Bluetooth DAP port.
    NvOdmDapPort_BlueTooth,

    /// Specifies media type DAP port.
    NvOdmDapPort_MediaType,

    /// Specifies voice type DAP port.
    NvOdmDapPort_VoiceType,

    /// Specifies high fidelity codec DAP port.
    NvOdmDapPort_HifiCodecType,

    /// Specifies voice codec DAP port.
    NvOdmDapPort_VoiceCodecType,
    
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmDapPort_Force32 = 0x7FFFFFFF
} NvOdmDapPort;

#define NvOdmDapPort_Max NvOdmDapPort_Dap5+1
/**
 * Combines the one-time configuration property for DAP device wired to the DAP port.
 * Currently define only for best suited values. If the device can support more ranges,
 * you have to consider it accordingly.
 */
typedef struct NvOdmDapDevicePropertyRec
{
    /// Specifies the number of channels, such as 2 for stereo.
    NvU32 NumberOfChannels;

    /// Specifies the number of bits per sample, such as 8 or 16 bits.
    NvU32 NumberOfBitsPerSample;

    /// Specifies the sampling rate in Hz, such as 8000 for 8 kHz, 44100
    /// for 44.1 kHz.
    NvU32 SamplingRateInHz;

    /// Holds the information DAP port data communication format.
    NvOdmQueryI2sDataCommFormat  DapPortCommunicationFormat;

}NvOdmDapDeviceProperty;

/**
*  Defines the connection line and connection table.
*/
typedef struct NvOdmQueryDapPortConnectionLinesRec
{
    /// Specifies the source for the connection line.
    NvOdmDapPort Source;

    /// Specifies the destination for the connection line.
    NvOdmDapPort Destination;

    /// Specifies the source to act as master or slave.
    NvBool       IsSourceMaster;

}NvOdmQueryDapPortConnectionLines;

/**
*  Increases the maximum connection line based on use case connection needed.
*/
#define NVODM_MAX_CONNECTIONLINES 8

/**
*    Defines the DAP port connection.
*/
typedef struct NvOdmQueryDapPortConnectionRec
{
    /// Specifie the connection use case from the enum provided.
    NvU32 UseIndex;

    /// Specifies the number of connection line for the table.
    NvU32 NumofEntires;

    /// Specifies the connection lines for the table.
    NvOdmQueryDapPortConnectionLines DapPortConnectionLines[NVODM_MAX_CONNECTIONLINES];

}NvOdmQueryDapPortConnection;
/**
 * Combines the one-time configuration property for DAP port setting.
 */
typedef struct NvOdmQueryDapPortPropertyRec
{
    /// Specifies the source for the DAP port.
    NvOdmDapPort DapSource;

    /// Specifies the destination for the DAP port.
    NvOdmDapPort DapDestination;

    /// Specified the property of device wired to DAP port.
    NvOdmDapDeviceProperty DapDeviceProperty;

} NvOdmQueryDapPortProperty;

/**
 *  Defines ODM interrupt polarity.
 */
typedef enum
{
    NvOdmInterruptPolarity_Low = 1,
    NvOdmInterruptPolarity_High,
    NvOdmInterruptPolarity_Force32 = 0x7FFFFFFF
} NvOdmInterruptPolarity;

/**
 *  Defines core power request polarity, as required by a PMU.
 */
typedef enum
{
  NvOdmCorePowerReqPolarity_Low,
  NvOdmCorePowerReqPolarity_High,
  NvOdmCorePowerReqPolarity_Force32 = 0x7FFFFFFF
}NvOdmCorePowerReqPolarity;

/**
 *  Defines system clock request polarity, as required by the clock source.
 */
typedef enum
{
  NvOdmSysClockReqPolarity_Low,
  NvOdmSysClockReqPolarity_High,
  NvOdmSysClockReqPolarity_Force32 = 0x7FFFFFFF
}NvOdmSysClockReqPolarity;


/**
 * Combines PMU configuration properties.
 */
typedef struct NvOdmPmuPropertyRec
{
    /// Specifies if PMU interrupt is connected to SoC.
    NvBool IrqConnected;

    /// Specifies the time required for power to be stable (in 32 kHz counts).
    NvU32   PowerGoodCount;

    /// Specifies the PMU interrupt polarity.
    NvOdmInterruptPolarity IrqPolarity;

    /// Specifies the core power request signal polarity.
    NvOdmCorePowerReqPolarity CorePowerReqPolarity;

    /// Specifies the system clock request signal polarity.
    NvOdmSysClockReqPolarity SysClockReqPolarity;

    /// Specifies whether or not only one power request input on PMU is available. 
    /// Relevant for SoCs with separate CPU and core power request outputs:
    /// - NV_TRUE specifies PMU has single power request input, in this case SoC 
    /// CPU and core power requests must be combined by external logic with
    /// proper pull-up/pull-down.
    /// - NV_FALSE specifies PMU has at least two power request inputs, in this
    /// case SoC CPU and core power requests are connected separately to
    /// the respective PMU inputs.
    NvBool CombinedPowerReq;

    /// Specifies the time required for CPU power to be stable (in US).
    /// Relevant for SoC with separate CPU and core power request outputs.
    NvU32 CpuPowerGoodUs;

    /// Specifies whether or not CPU voltage will switch back to OTP (default)
    /// value after CPU request on-off-on transition (typically this transition
    /// happens on entry/exit to/from low power states). Relevant for SoCs with
    /// separate CPU and core power request outputs:
    /// - NV_TRUE specifies PMU will switch CPU voltage to default level after
    /// CPU request  on-off-on transition. This PMU mode is not compatible with
    /// DVFS core voltage scaling, which will be disabled in this case.
    /// - NV_FALSE specifies PMU will restore CPU voltage after CPU request
    ///  on-off-on transition to the level it has just before the transition
    /// happens. In this case DVFS core voltage scaling can be enabled.
    NvBool  VCpuOTPOnWakeup;

    /// Specifies PMU Core and CPU voltage regulation accuracy in percent
    NvU32 AccuracyPercent;

} NvOdmPmuProperty;

/**
 *  Defines SOC power states.
 */
typedef enum
{
  /// State where power to non-always-on (non-AO) partitions are
  /// removed, and double-data rate (DDR) SDRAM is in self-refresh
  /// mode. Wake up by any enabled \a external event/interrupt.
  NvOdmSocPowerState_DeepSleep,

  /// State where the CPU is halted by the flow controller and power
  /// is gated, plus DDR is in self-refresh. Wake up by any enabled interrupt.
  NvOdmSocPowerState_Suspend,

  /// Specifies to disable the SOC power state.
  NvOdmSocPowerState_Active,

  /// Ignore -- Forces compilers to make 32-bit enums.
  NvOdmSocPowerState_Force32 = 0x7FFFFFFFUL

} NvOdmSocPowerState;

/**
 *  SOC power state information.
 */
typedef struct NvOdmSocPowerStateInfoRec
{
    // Specifies the lowest supported power state.
    NvOdmSocPowerState LowestPowerState;

    // Specifies the idle time (in Msecs) threshold to enter the power state.
    NvU32 IdleThreshold;

} NvOdmSocPowerStateInfo;

/**  External interface type for USB controllers */
typedef enum
{
    /// Specifies the USB controller is connected to a standard UTMI interface
    /// (only valid for ::NvOdmIoModule_Usb).
    NvOdmUsbInterfaceType_Utmi = 1,

    /// Specifies the USB controller is connected to a phy-less ULPI interface
    /// (only valid for ::NvOdmIoModule_Ulpi).
    NvOdmUsbInterfaceType_UlpiNullPhy,

    /// Specifies the USB controller is connected to a ULPI interface that has an
    /// external phy (only valid for \c NvOdmIoModule_Ulpi).
    NvOdmUsbInterfaceType_UlpiExternalPhy,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmUsbInterfaceType_Force32 = 0x7FFFFFFFUL

} NvOdmUsbInterfaceType;


/** Defines the USB line states. */ 
typedef enum 
{
    /// Specifies USB host based charging type.
    NvOdmUsbChargerType_UsbHost = 0,

    /// Specifies charger type 0, USB compliant charger, when D+ and D- are at low voltage.
    NvOdmUsbChargerType_SE0 = 1,

    /// Specifies charger type 1, when D+ is high and D- is low.
    NvOdmUsbChargerType_SJ = 2,

    /// Specifies charger type 2, when D+ is low and D- is high.
    NvOdmUsbChargerType_SK = 4,

    /// Specifies charger type 3, when D+ and D- are at high voltage.
    NvOdmUsbChargerType_SE1 = 8,

    /// Specifies charger type 4, D+ and D- are undefined.
    /// @note If dummy charger is selected, then charger type will be always 
    /// dummy and other type chargers are detected but treated as dummy.
    NvOdmUsbChargerType_Dummy = 0x10,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmUsbChargerType_Force32 = 0x7FFFFFFF,
} NvOdmUsbChargerType;

/** Defines the USB mode for the instance. */ 
typedef enum 
{
    /// Specifies the instance is not present or cannot be used for USB.
    NvOdmUsbModeType_None = 0,

    /// Specifies the instance as USB host.
    NvOdmUsbModeType_Host = 1,

    /// Specifies the instance as USB Device.
    NvOdmUsbModeType_Device = 2,

    /// Specifies the instance as USB OTG.
    NvOdmUsbModeType_OTG= 4,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmUsbModeType_Force32 = 0x7FFFFFFF,
} NvOdmUsbModeType;

/** Defines the USB ID pin detection type. */ 
typedef enum 
{
    /// Specifies there is no ID pin detection mechanism.
    NvOdmUsbIdPinType_None = 0,

    /// Specifies ID pin detection is done with GPIO.
    NvOdmUsbIdPinType_Gpio= 1,

    /// Specifies ID pin detection is done with cable ID.
    NvOdmUsbIdPinType_CableId= 2,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmUsbIdPinType_Force32 = 0x7FFFFFFF,
} NvOdmUsbIdPinType;

/** Defines the USB connectors multiplex type. */ 
typedef enum 
{
    /// Specifies there is no connectors mux  mechanism
   NvOdmUsbConnectorsMuxType_None = 0,

    /// Specifies microAB/TypeA mux is available. 
    NvOdmUsbConnectorsMuxType_MicroAB_TypeA= 1,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmUsbConnectorsMuxType_Force32 = 0x7FFFFFFF,
} NvOdmUsbConnectorsMuxType;

/**
 *  Defines the USB trimmer control values. Keep all values zero unless the
 *  default trimmer values programmed in DDK do not work on the customer board.
 */
typedef struct NvOdmUsbTrimmerCtrlRec
{
    /// Programmable delay on the Shadow ULPI Clock (0 ~ 31)
    NvU8 UlpiShadowClkDelay;

    /// Programmable delay on the ULPI Clock out (0 ~ 31)
    NvU8 UlpiClockOutDelay;

    /// ULPI Data Trimmer Value (0 ~ 7)
    NvU8 UlpiDataTrimmerSel;

    /// ULPI STP/DIR/NXT Trimmer Value (0 ~ 7)
    NvU8 UlpiStpDirNxtTrimmerSel;
} NvOdmUsbTrimmerCtrl;

/**  Defines USB interface properties. */
typedef struct NvOdmUsbPropertyRec
{
    ///  Specifies the USB controller's external interface type.
    ///  @see NvOdmUsbInterfaceType
    NvOdmUsbInterfaceType UsbInterfaceType;

    /// Specifies the charger types supported on this interface.
    /// If dummy charger is selected then other type chargers are detected as dummy.
    /// @see NvOdmUsbChargerType
    NvU32 SupportedChargers;

    /// Specifies the time required to wait before checking for the line status.
    /// @see NvOdmUsbChargerType
    NvU32 ChargerDetectTimeMs;

    /// Specifies internal PHY to use as source for VBUS detection in the low power mode.
    /// Set to NV_TRUE to use internal PHY for VBUS detection.
    /// Set to NV_FALSE to use PMU interrupt for VBUS detection.
    NvBool UseInternalPhyWakeup;

    /// Specifies the USB mode for the instance.
    /// @see NvOdmUsbModeType
    NvU32 UsbMode;

    /// Specifies the USB ID pin detection type.
    /// @see NvOdmUsbIdPinType
    NvU32 IdPinDetectionType;

    /// Specifies the USB connectors multiplex type.
    /// @see NvOdmUsbConnectorsMuxType
    NvOdmUsbConnectorsMuxType   ConMuxType;

    /// Specifies Usb rail to  power off or not  in the deep sleep mode.
    /// Set to NV_TRUE to specify usb rail power off in the deep sleep
    /// Set to NV_FALSE to specify usb rail can not be power off in the deep sleep
    NvBool UsbRailPoweOffInDeepSleep;

    /// Specifies the USB trimmer values. The default value will be used if all values are zeros.
    /// @see NvOdmUsbTrimmerCtrl
    NvOdmUsbTrimmerCtrl TrimmerCtrl;
} NvOdmUsbProperty;

/** Defines wakeup sources. */
typedef enum
{
    NvOdmGpioWakeupSource_Invalid = 0,
    NvOdmGpioWakeupSource_RIL,
    NvOdmGpioWakeupSource_UART,
    NvOdmGpioWakeupSource_BluetoothIrq,
    NvOdmGpioWakeupSource_HDMIDetection,
    NvOdmGpioWakeupSource_USB,
    NvOdmGpioWakeupSource_Lid,
    NvOdmGpioWakeupSource_AudioIrq,
    NvOdmGpioWakeupSource_ACCIrq,
    NvOdmGpioWakeupSource_HSMMCCardDetect,
    NvOdmGpioWakeupSource_SdioDat1,
    NvOdmGpioWakeupSource_SdioCardDetect,
    NvOdmGpioWakeupSource_KBC,
    NvOdmGpioWakeupSource_PWR,
    NvOdmGpioWakeupSource_BasebandModem,
    NvOdmGpioWakeupSource_DVI,
    NvOdmGpioWakeupSource_GpsOnOff,
    NvOdmGpioWakeupSource_GpsInterrupt,
    NvOdmGpioWakeupSource_Accelerometer,
    NvOdmGpioWakeupSource_HeadsetDetect,
    NvOdmGpioWakeupSource_PenInterrupt,
    NvOdmGpioWakeupSource_WlanInterrupt,
    NvOdmGpioWakeupSource_UsbVbus,
    NvOdmGpioWakeupSource_Force32 = 0x7FFFFFFF,
} NvOdmGpioWakeupSource;

/**
 * Gets the total memory size for the specified memory type.
 *
 * @note The implementation of this function must not make reference to
 * any global or static variables of any kind whatsoever.
 *
 * @param MemType Specifies the memory type.
 *
 * @return The memory size (in bytes), or 0 if no memory of that type exists.
 */
NvU32 NvOdmQueryMemSize(NvOdmMemoryType MemType);

/**
 * Gets the memory occupied by the secure region. Must be 1 MB aligned.
 *
 * @returns The memory occupied (in bytes).
 */
NvU32 NvOdmQuerySecureRegionSize(void);

/**
 * Gets the size of the carveout region.
 *
 * The carveout memory region is contiguous physical memory used by some
 * software modules instead of allocating memory from the OS heap. This memory
 * is separate from the operating system's heap.
 *
 * The carveout memory region is useful because the OS heap often becomes
 * fragmented after boot time, making it difficult to obtain physically
 * contiguous memory.
 */
NvU32 NvOdmQueryCarveoutSize(void);

/**
 * Gets the port to use as the debug console.
 *
 * @return The debug console ID.
 */
NvOdmDebugConsole NvOdmQueryDebugConsole(void);

/**
 * Gets the device to use as the download transport.
 *
 * @return The download transport device ID.
 */
NvOdmDownloadTransport NvOdmQueryDownloadTransport(void);

/**
 * Gets the null-terminated device name prefix string (i.e., that
 * part of a device name that is common to all devices of this type).
 *
 * @return The device name prefix string.
 */
const NvU8* NvOdmQueryDeviceNamePrefix(void);

/** 
 * Gets the configuration info for the display. 
 * 
 * @param Instance The instance number of the display controller. 
 * @return A pointer to the structure containing the display information. 
 */
const NvOdmQueryDisplayInfo * 
NvOdmQueryGetDisplayInfo(
    NvU32 Instance); 

/**
 * Gets the interfacing properties of the device connected to a given chip
 * select on a given SPI controller.
 *
 * @param OdmIoModule The ODM I/O module name, such as SPI, S-LINK, or S-Flash.
 * @param ControllerId The SPI instance ID.
 * @param ChipSelect The chip select ID from the connected device.
 *
 * @return A pointer to a structure describing the device's properties.
 */
const NvOdmQuerySpiDeviceInfo *
NvOdmQuerySpiGetDeviceInfo(
    NvOdmIoModule OdmIoModule,
    NvU32 ControllerId,
    NvU32 ChipSelect);


/**
 * Gets the default signal level of the SPI interfacing lines.
 * This indicates whether signal lines are in the tristate or not, and if not,
 * then indicates what is the normal state of the SCLK and data out line.
 * This state is set once the transaction is completed.
 * During the transaction, the chip-specific setting is done.
 *
 * @param OdmIoModule The ODM I/O module name, such as SPI, S-LINK, or S-Flash.
 * @param ControllerId The SPI instance ID.
 *
 * @return A pointer to a structure describing the idle signal state.
 */
const NvOdmQuerySpiIdleSignalState *
NvOdmQuerySpiGetIdleSignalState(
    NvOdmIoModule OdmIoModule,
    NvU32 ControllerId);

/**
 * Gets the S/PDIF interfacing property parameters with the audio codec that
 * are set for the data transfer.
 *
 * @param SpdifInstanceId The S/PDIF controller instance ID.
 *
 * @return A pointer to a structure describing the I2S interface properties.
 */
const NvOdmQuerySpdifInterfaceProperty *
NvOdmQuerySpdifGetInterfaceProperty(
    NvU32 SpdifInstanceId);

 /**
 * Gets the I2S interfacing property parameter, which is set for
 * the data transfer.
 *
 * @param I2sInstanceId The I2S controller instance ID.
 *
 * @return A pointer to a structure describing the I2S interface properties.
 *
 */
const NvOdmQueryI2sInterfaceProperty *
NvOdmQueryI2sGetInterfaceProperty(
    NvU32 I2sInstanceId);

/**
 * Gets the AC97 interfacing property with AC97 codec parameters that are set
 * for the data transfer.
 *
 * @param Ac97InstanceId The instance ID for the AC97 cotroller.
 *
 * @return A pointer to a structure describing the AC97 interface properties.
 *
 */
const NvOdmQueryAc97InterfaceProperty *
NvOdmQueryAc97GetInterfaceProperty(
    NvU32 Ac97InstanceId);

/**
 * Gets the DAP port property.
 *
 * This shows how the DAP connection is made along with
 * the format and mode it supports.
 *
 * @param DapPortId The DAP port.
 *
 * @return A pointer to a structure holding the DAP port connection properties.
 */
const NvOdmQueryDapPortProperty *
NvOdmQueryDapPortGetProperty(
    NvU32 DapPortId);

/**
 * Gets the DAP port connection table.
 *
 * This shows how the connections are made along with
 * the use case.
 *
 * @param ConnectionIndex The index to ConnectionTable based on the use case.
 *
 * @return A pointer to a structure holding the connection lines.
 */
const NvOdmQueryDapPortConnection*
NvOdmQueryDapPortGetConnectionTable(
    NvU32 ConnectionIndex);

/**
 * Gets the I2S audio codec interfacing property.
 *
 * @param AudioCodecId The instance ID or the audio codec cotroller.
 *
 * @return A pointer to a structure describing the audio codec interface
 *           properties.
 */
const NvOdmQueryI2sACodecInterfaceProp *
NvOdmQueryGetI2sACodecInterfaceProperty(
    NvU32 AudioCodecId);

/**
 * Gets the oscillator source.
 *
 * @see NvOdmQueryOscillator
 *
 * @return The oscillator source.
 */
NvOdmQueryOscillator NvOdmQueryGetOscillatorSource(void);

/**
 * Gets the oscillator drive strength setting.
 *
 * @return The oscillator drive strength setting.
 */
NvU32 NvOdmQueryGetOscillatorDriveStrength(void);

/**
 * Gets the null-terminated device manufacturer string.
 *
 * @return A pointer to the device manufacturer string.
 */
const NvU8* NvOdmQueryManufacturer(void);

/**
 * Gets the null-terminated device model string.
 *
 * @return A pointer to the device model string.
 */
const NvU8* NvOdmQueryModel(void);

/**
 * Gets the null-terminated device platform string.
 *
 * @return A pointer to the device platform string.
 */
const NvU8* NvOdmQueryPlatform(void);

/**
 * Gets the null-terminated device project name string.
 *
 * @return A pointer to the device project name string.
 */
const NvU8* NvOdmQueryProjectName(void);

/**
 * Gets the wake pads configuration table.
 *
 * @param entries A pointer to a variable that this function sets to the
 * number of entries in the configuration table.
 *
 * @return A pointer to the configuration table.
 */
const NvOdmWakeupPadInfo *NvOdmQueryGetWakeupPadTable(NvU32 *entries);

/**
 * Gets the external PMU property.
 *
 * @param pPmuProperty A pointer to the returned PMU property structure.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmQueryGetPmuProperty(NvOdmPmuProperty* pPmuProperty);

/**
 * Gets the lowest SOC power state info supported by the ODM.
 *
 * @return A pointer to the NvOdmSocPowerStateInfo structure
 */
const NvOdmSocPowerStateInfo* NvOdmQueryLowestSocPowerState(void);

/**
 * Returns the type of the USB interface based on module ID and
 * instance. The \a Module and \a Instance parameter are identical to the
 * \a IoModule parameter and array index, respectively, used in the 
 * NvOdmQueryPinMux() and NvOdmQueryClockLimits() APIs.
 *
 * @return The properties structure for the USB interface.
 */
const NvOdmUsbProperty*
NvOdmQueryGetUsbProperty(NvOdmIoModule Module, NvU32 Instance);

/**
 * Gets the interface properties of the SDIO controller.
 *
 * @param Instance The instance number of the SDIO controller.
 * @return A pointer to the structure containing the SDIO interface property.
 */
 
const NvOdmQuerySdioInterfaceProperty*
NvOdmQueryGetSdioInterfaceProperty(
        NvU32 Instance);

/**
 * Gets the interface properties of the HSMMC controller.
 *
 * @param Instance The instance number of the HSMMC controller.
 * @return A pointer to the structure containing the HSMMC interface property.
 */
 
const NvOdmQueryHsmmcInterfaceProperty*
NvOdmQueryGetHsmmcInterfaceProperty(
        NvU32 Instance);

/**
 * Gets the ODM-specific sector size for block devices.
 *
 * @param OdmIoModule The ODM I/O module type.
 * @return An integer indicating the sector size if non-zero, or 
 *      zero if the sector size equals the actual device-reported sector size.
 */
NvU32
NvOdmQueryGetBlockDeviceSectorSize(NvOdmIoModule OdmIoModule);

/**
 * Gets the OWR device information.
 *
 * @param Instance The instance number of the OWR controller.
 * @return A pointer to the structure containing the OWR device info.
 */
const NvOdmQueryOwrDeviceInfo* NvOdmQueryGetOwrDeviceInfo(NvU32 Instance);

/**
 * Gets the list of supported wakeup sources.
 *
 * @param pCount The number of wakeup sources.
 * @return A pointer to the array containing the wakeup sources.
 */
const NvOdmGpioWakeupSource *NvOdmQueryGetWakeupSources(NvU32 *pCount);

#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_QUERY_H
