/*
 * Copyright 2006-2007 Advanced Micro Devices, Inc.  
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/****************************************************************************/	
/*Portion I: Definitions  shared between VBIOS and Driver                   */
/****************************************************************************/


#ifndef _ATOMBIOS_H
#define _ATOMBIOS_H

#define ATOM_VERSION_MAJOR                   0x00020000
#define ATOM_VERSION_MINOR                   0x00000002

#define ATOM_HEADER_VERSION (ATOM_VERSION_MAJOR | ATOM_VERSION_MINOR)

/* Endianness should be specified before inclusion,
 * default to little endian
 */
#ifndef ATOM_BIG_ENDIAN
#error Endian not specified
#endif

#ifdef _H2INC
  #ifndef ULONG 
    typedef unsigned long ULONG;
  #endif

  #ifndef UCHAR
    typedef unsigned char UCHAR;
  #endif

  #ifndef USHORT 
    typedef unsigned short USHORT;
  #endif
#endif
      
#define ATOM_DAC_A            0 
#define ATOM_DAC_B            1
#define ATOM_EXT_DAC          2

#define ATOM_CRTC1            0
#define ATOM_CRTC2            1
#define ATOM_CRTC3            2
#define ATOM_CRTC4            3
#define ATOM_CRTC5            4
#define ATOM_CRTC6            5
#define ATOM_CRTC_INVALID     0xFF

#define ATOM_DIGA             0
#define ATOM_DIGB             1

#define ATOM_PPLL1            0
#define ATOM_PPLL2            1
#define ATOM_DCPLL            2
#define ATOM_PPLL0            2
#define ATOM_PPLL3            3

#define ATOM_EXT_PLL1         8
#define ATOM_EXT_PLL2         9
#define ATOM_EXT_CLOCK        10
#define ATOM_PPLL_INVALID     0xFF

#define ENCODER_REFCLK_SRC_P1PLL       0       
#define ENCODER_REFCLK_SRC_P2PLL       1
#define ENCODER_REFCLK_SRC_DCPLL       2
#define ENCODER_REFCLK_SRC_EXTCLK      3
#define ENCODER_REFCLK_SRC_INVALID     0xFF

#define ATOM_SCALER1          0
#define ATOM_SCALER2          1

#define ATOM_SCALER_DISABLE   0   
#define ATOM_SCALER_CENTER    1   
#define ATOM_SCALER_EXPANSION 2   
#define ATOM_SCALER_MULTI_EX  3   

#define ATOM_DISABLE          0
#define ATOM_ENABLE           1
#define ATOM_LCD_BLOFF                          (ATOM_DISABLE+2)
#define ATOM_LCD_BLON                           (ATOM_ENABLE+2)
#define ATOM_LCD_BL_BRIGHTNESS_CONTROL          (ATOM_ENABLE+3)
#define ATOM_LCD_SELFTEST_START									(ATOM_DISABLE+5)
#define ATOM_LCD_SELFTEST_STOP									(ATOM_ENABLE+5)
#define ATOM_ENCODER_INIT			                  (ATOM_DISABLE+7)
#define ATOM_INIT			                          (ATOM_DISABLE+7)
#define ATOM_GET_STATUS                         (ATOM_DISABLE+8)

#define ATOM_BLANKING         1
#define ATOM_BLANKING_OFF     0

#define ATOM_CURSOR1          0
#define ATOM_CURSOR2          1

#define ATOM_ICON1            0
#define ATOM_ICON2            1

#define ATOM_CRT1             0
#define ATOM_CRT2             1

#define ATOM_TV_NTSC          1
#define ATOM_TV_NTSCJ         2
#define ATOM_TV_PAL           3
#define ATOM_TV_PALM          4
#define ATOM_TV_PALCN         5
#define ATOM_TV_PALN          6
#define ATOM_TV_PAL60         7
#define ATOM_TV_SECAM         8
#define ATOM_TV_CV            16

#define ATOM_DAC1_PS2         1
#define ATOM_DAC1_CV          2
#define ATOM_DAC1_NTSC        3
#define ATOM_DAC1_PAL         4

#define ATOM_DAC2_PS2         ATOM_DAC1_PS2
#define ATOM_DAC2_CV          ATOM_DAC1_CV
#define ATOM_DAC2_NTSC        ATOM_DAC1_NTSC
#define ATOM_DAC2_PAL         ATOM_DAC1_PAL
 
#define ATOM_PM_ON            0
#define ATOM_PM_STANDBY       1
#define ATOM_PM_SUSPEND       2
#define ATOM_PM_OFF           3

/* Bit0:{=0:single, =1:dual},
   Bit1 {=0:666RGB, =1:888RGB},
   Bit2:3:{Grey level}
   Bit4:{=0:LDI format for RGB888, =1 FPDI format for RGB888}*/

#define ATOM_PANEL_MISC_DUAL               0x00000001
#define ATOM_PANEL_MISC_888RGB             0x00000002
#define ATOM_PANEL_MISC_GREY_LEVEL         0x0000000C
#define ATOM_PANEL_MISC_FPDI               0x00000010
#define ATOM_PANEL_MISC_GREY_LEVEL_SHIFT   2
#define ATOM_PANEL_MISC_SPATIAL            0x00000020
#define ATOM_PANEL_MISC_TEMPORAL           0x00000040
#define ATOM_PANEL_MISC_API_ENABLED        0x00000080


#define MEMTYPE_DDR1              "DDR1"
#define MEMTYPE_DDR2              "DDR2"
#define MEMTYPE_DDR3              "DDR3"
#define MEMTYPE_DDR4              "DDR4"

#define ASIC_BUS_TYPE_PCI         "PCI"
#define ASIC_BUS_TYPE_AGP         "AGP"
#define ASIC_BUS_TYPE_PCIE        "PCI_EXPRESS"

/* Maximum size of that FireGL flag string */

#define ATOM_FIREGL_FLAG_STRING     "FGL"             //Flag used to enable FireGL Support
#define ATOM_MAX_SIZE_OF_FIREGL_FLAG_STRING  3        //sizeof( ATOM_FIREGL_FLAG_STRING )

#define ATOM_FAKE_DESKTOP_STRING    "DSK"             //Flag used to enable mobile ASIC on Desktop
#define ATOM_MAX_SIZE_OF_FAKE_DESKTOP_STRING  ATOM_MAX_SIZE_OF_FIREGL_FLAG_STRING 

#define ATOM_M54T_FLAG_STRING       "M54T"            //Flag used to enable M54T Support
#define ATOM_MAX_SIZE_OF_M54T_FLAG_STRING    4        //sizeof( ATOM_M54T_FLAG_STRING )

#define HW_ASSISTED_I2C_STATUS_FAILURE          2
#define HW_ASSISTED_I2C_STATUS_SUCCESS          1

#pragma pack(1)                                       /* BIOS data must use byte alignment */

/*  Define offset to location of ROM header. */

#define OFFSET_TO_POINTER_TO_ATOM_ROM_HEADER		0x00000048L
#define OFFSET_TO_ATOM_ROM_IMAGE_SIZE				    0x00000002L

#define OFFSET_TO_ATOMBIOS_ASIC_BUS_MEM_TYPE    0x94
#define MAXSIZE_OF_ATOMBIOS_ASIC_BUS_MEM_TYPE   20    /* including the terminator 0x0! */
#define	OFFSET_TO_GET_ATOMBIOS_STRINGS_NUMBER		0x002f
#define	OFFSET_TO_GET_ATOMBIOS_STRINGS_START		0x006e

/* Common header for all ROM Data tables.
  Every table pointed  _ATOM_MASTER_DATA_TABLE has this common header. 
  And the pointer actually points to this header. */

typedef struct _ATOM_COMMON_TABLE_HEADER
{
  USHORT usStructureSize;
  UCHAR  ucTableFormatRevision;   /*Change it when the Parser is not backward compatible */
  UCHAR  ucTableContentRevision;  /*Change it only when the table needs to change but the firmware */
                                  /*Image can't be updated, while Driver needs to carry the new table! */
}ATOM_COMMON_TABLE_HEADER;

/****************************************************************************/	
// Structure stores the ROM header.
/****************************************************************************/	
typedef struct _ATOM_ROM_HEADER
{
  ATOM_COMMON_TABLE_HEADER		sHeader;
  UCHAR	 uaFirmWareSignature[4];    /*Signature to distinguish between Atombios and non-atombios, 
                                      atombios should init it as "ATOM", don't change the position */
  USHORT usBiosRuntimeSegmentAddress;
  USHORT usProtectedModeInfoOffset;
  USHORT usConfigFilenameOffset;
  USHORT usCRC_BlockOffset;
  USHORT usBIOS_BootupMessageOffset;
  USHORT usInt10Offset;
  USHORT usPciBusDevInitCode;
  USHORT usIoBaseAddress;
  USHORT usSubsystemVendorID;
  USHORT usSubsystemID;
  USHORT usPCI_InfoOffset; 
  USHORT usMasterCommandTableOffset; /*Offset for SW to get all command table offsets, Don't change the position */
  USHORT usMasterDataTableOffset;   /*Offset for SW to get all data table offsets, Don't change the position */
  UCHAR  ucExtendedFunctionCode;
  UCHAR  ucReserved;
}ATOM_ROM_HEADER;

/*==============================Command Table Portion==================================== */

#ifdef	UEFI_BUILD
	#define	UTEMP	USHORT
	#define	USHORT	void*
#endif

/****************************************************************************/	
// Structures used in Command.mtb 
/****************************************************************************/	
typedef struct _ATOM_MASTER_LIST_OF_COMMAND_TABLES{
  USHORT ASIC_Init;                              //Function Table, used by various SW components,latest version 1.1
  USHORT GetDisplaySurfaceSize;                  //Atomic Table,  Used by Bios when enabling HW ICON
  USHORT ASIC_RegistersInit;                     //Atomic Table,  indirectly used by various SW components,called from ASIC_Init
  USHORT VRAM_BlockVenderDetection;              //Atomic Table,  used only by Bios
  USHORT DIGxEncoderControl;										 //Only used by Bios
  USHORT MemoryControllerInit;                   //Atomic Table,  indirectly used by various SW components,called from ASIC_Init
  USHORT EnableCRTCMemReq;                       //Function Table,directly used by various SW components,latest version 2.1
  USHORT MemoryParamAdjust; 										 //Atomic Table,  indirectly used by various SW components,called from SetMemoryClock if needed
  USHORT DVOEncoderControl;                      //Function Table,directly used by various SW components,latest version 1.2
  USHORT GPIOPinControl;												 //Atomic Table,  only used by Bios
  USHORT SetEngineClock;                         //Function Table,directly used by various SW components,latest version 1.1
  USHORT SetMemoryClock;                         //Function Table,directly used by various SW components,latest version 1.1
  USHORT SetPixelClock;                          //Function Table,directly used by various SW components,latest version 1.2  
  USHORT EnableDispPowerGating;                  //Atomic Table,  indirectly used by various SW components,called from ASIC_Init
  USHORT ResetMemoryDLL;                         //Atomic Table,  indirectly used by various SW components,called from SetMemoryClock
  USHORT ResetMemoryDevice;                      //Atomic Table,  indirectly used by various SW components,called from SetMemoryClock
  USHORT MemoryPLLInit;                          //Atomic Table,  used only by Bios
  USHORT AdjustDisplayPll;											 //Atomic Table,  used by various SW componentes. 
  USHORT AdjustMemoryController;                 //Atomic Table,  indirectly used by various SW components,called from SetMemoryClock                
  USHORT EnableASIC_StaticPwrMgt;                //Atomic Table,  only used by Bios
  USHORT SetUniphyInstance;                      //Atomic Table,  only used by Bios   
  USHORT DAC_LoadDetection;                      //Atomic Table,  directly used by various SW components,latest version 1.2  
  USHORT LVTMAEncoderControl;                    //Atomic Table,directly used by various SW components,latest version 1.3
  USHORT HW_Misc_Operation;                      //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT DAC1EncoderControl;                     //Atomic Table,  directly used by various SW components,latest version 1.1  
  USHORT DAC2EncoderControl;                     //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT DVOOutputControl;                       //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT CV1OutputControl;                       //Atomic Table,  Atomic Table,  Obsolete from Ry6xx, use DAC2 Output instead 
  USHORT GetConditionalGoldenSetting;            //Only used by Bios
  USHORT TVEncoderControl;                       //Function Table,directly used by various SW components,latest version 1.1
  USHORT PatchMCSetting;                         //only used by BIOS
  USHORT MC_SEQ_Control;                         //only used by BIOS
  USHORT Gfx_Harvesting;                         //Atomic Table,  Obsolete from Ry6xx, Now only used by BIOS for GFX harvesting
  USHORT EnableScaler;                           //Atomic Table,  used only by Bios
  USHORT BlankCRTC;                              //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT EnableCRTC;                             //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT GetPixelClock;                          //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT EnableVGA_Render;                       //Function Table,directly used by various SW components,latest version 1.1
  USHORT GetSCLKOverMCLKRatio;                   //Atomic Table,  only used by Bios
  USHORT SetCRTC_Timing;                         //Atomic Table,  directly used by various SW components,latest version 1.1
  USHORT SetCRTC_OverScan;                       //Atomic Table,  used by various SW components,latest version 1.1 
  USHORT SetCRTC_Replication;                    //Atomic Table,  used only by Bios
  USHORT SelectCRTC_Source;                      //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT EnableGraphSurfaces;                    //Atomic Table,  used only by Bios
  USHORT UpdateCRTC_DoubleBufferRegisters;			 //Atomic Table,  used only by Bios
  USHORT LUT_AutoFill;                           //Atomic Table,  only used by Bios
  USHORT EnableHW_IconCursor;                    //Atomic Table,  only used by Bios
  USHORT GetMemoryClock;                         //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT GetEngineClock;                         //Atomic Table,  directly used by various SW components,latest version 1.1 
  USHORT SetCRTC_UsingDTDTiming;                 //Atomic Table,  directly used by various SW components,latest version 1.1
  USHORT ExternalEncoderControl;                 //Atomic Table,  directly used by various SW components,latest version 2.1
  USHORT LVTMAOutputControl;                     //Atomic Table,  directly used by various SW components,latest version 1.1
  USHORT VRAM_BlockDetectionByStrap;             //Atomic Table,  used only by Bios
  USHORT MemoryCleanUp;                          //Atomic Table,  only used by Bios    
  USHORT ProcessI2cChannelTransaction;           //Function Table,only used by Bios
  USHORT WriteOneByteToHWAssistedI2C;            //Function Table,indirectly used by various SW components 
  USHORT ReadHWAssistedI2CStatus;                //Atomic Table,  indirectly used by various SW components
  USHORT SpeedFanControl;                        //Function Table,indirectly used by various SW components,called from ASIC_Init
  USHORT PowerConnectorDetection;                //Atomic Table,  directly used by various SW components,latest version 1.1
  USHORT MC_Synchronization;                     //Atomic Table,  indirectly used by various SW components,called from SetMemoryClock
  USHORT ComputeMemoryEnginePLL;                 //Atomic Table,  indirectly used by various SW components,called from SetMemory/EngineClock
  USHORT MemoryRefreshConversion;                //Atomic Table,  indirectly used by various SW components,called from SetMemory or SetEngineClock
  USHORT VRAM_GetCurrentInfoBlock;               //Atomic Table,  used only by Bios
  USHORT DynamicMemorySettings;                  //Atomic Table,  indirectly used by various SW components,called from SetMemoryClock
  USHORT MemoryTraining;                         //Atomic Table,  used only by Bios
  USHORT EnableSpreadSpectrumOnPPLL;             //Atomic Table,  directly used by various SW components,latest version 1.2
  USHORT TMDSAOutputControl;                     //Atomic Table,  directly used by various SW components,latest version 1.1
  USHORT SetVoltage;                             //Function Table,directly and/or indirectly used by various SW components,latest version 1.1
  USHORT DAC1OutputControl;                      //Atomic Table,  directly used by various SW components,latest version 1.1
  USHORT DAC2OutputControl;                      //Atomic Table,  directly used by various SW components,latest version 1.1
  USHORT ComputeMemoryClockParam;                //Function Table,only used by Bios, obsolete soon.Switch to use "ReadEDIDFromHWAssistedI2C"
  USHORT ClockSource;                            //Atomic Table,  indirectly used by various SW components,called from ASIC_Init
  USHORT MemoryDeviceInit;                       //Atomic Table,  indirectly used by various SW components,called from SetMemoryClock
  USHORT GetDispObjectInfo;                      //Atomic Table,  indirectly used by various SW components,called from EnableVGARender
  USHORT DIG1EncoderControl;                     //Atomic Table,directly used by various SW components,latest version 1.1
  USHORT DIG2EncoderControl;                     //Atomic Table,directly used by various SW components,latest version 1.1
  USHORT DIG1TransmitterControl;                 //Atomic Table,directly used by various SW components,latest version 1.1
  USHORT DIG2TransmitterControl;	               //Atomic Table,directly used by various SW components,latest version 1.1 
  USHORT ProcessAuxChannelTransaction;					 //Function Table,only used by Bios
  USHORT DPEncoderService;											 //Function Table,only used by Bios
  USHORT GetVoltageInfo;                         //Function Table,only used by Bios since SI
}ATOM_MASTER_LIST_OF_COMMAND_TABLES;   

// For backward compatible 
#define ReadEDIDFromHWAssistedI2C                ProcessI2cChannelTransaction
#define DPTranslatorControl                      DIG2EncoderControl
#define UNIPHYTransmitterControl			     DIG1TransmitterControl
#define LVTMATransmitterControl				     DIG2TransmitterControl
#define SetCRTC_DPM_State                        GetConditionalGoldenSetting
#define ASIC_StaticPwrMgtStatusChange            SetUniphyInstance 
#define HPDInterruptService                      ReadHWAssistedI2CStatus
#define EnableVGA_Access                         GetSCLKOverMCLKRatio
#define EnableYUV                                GetDispObjectInfo                         
#define DynamicClockGating                       EnableDispPowerGating
#define SetupHWAssistedI2CStatus                 ComputeMemoryClockParam

#define TMDSAEncoderControl                      PatchMCSetting
#define LVDSEncoderControl                       MC_SEQ_Control
#define LCD1OutputControl                        HW_Misc_Operation
#define TV1OutputControl                         Gfx_Harvesting

typedef struct _ATOM_MASTER_COMMAND_TABLE
{
  ATOM_COMMON_TABLE_HEADER           sHeader;
  ATOM_MASTER_LIST_OF_COMMAND_TABLES ListOfCommandTables;
}ATOM_MASTER_COMMAND_TABLE;

/****************************************************************************/	
// Structures used in every command table
/****************************************************************************/	
typedef struct _ATOM_TABLE_ATTRIBUTE
{
#if ATOM_BIG_ENDIAN
  USHORT  UpdatedByUtility:1;         //[15]=Table updated by utility flag
  USHORT  PS_SizeInBytes:7;           //[14:8]=Size of parameter space in Bytes (multiple of a dword), 
  USHORT  WS_SizeInBytes:8;           //[7:0]=Size of workspace in Bytes (in multiple of a dword), 
#else
  USHORT  WS_SizeInBytes:8;           //[7:0]=Size of workspace in Bytes (in multiple of a dword), 
  USHORT  PS_SizeInBytes:7;           //[14:8]=Size of parameter space in Bytes (multiple of a dword), 
  USHORT  UpdatedByUtility:1;         //[15]=Table updated by utility flag
#endif
}ATOM_TABLE_ATTRIBUTE;

typedef union _ATOM_TABLE_ATTRIBUTE_ACCESS
{
  ATOM_TABLE_ATTRIBUTE sbfAccess;
  USHORT               susAccess;
}ATOM_TABLE_ATTRIBUTE_ACCESS;

/****************************************************************************/	
// Common header for all command tables.
// Every table pointed by _ATOM_MASTER_COMMAND_TABLE has this common header. 
// And the pointer actually points to this header.
/****************************************************************************/	
typedef struct _ATOM_COMMON_ROM_COMMAND_TABLE_HEADER
{
  ATOM_COMMON_TABLE_HEADER CommonHeader;
  ATOM_TABLE_ATTRIBUTE     TableAttribute;	
}ATOM_COMMON_ROM_COMMAND_TABLE_HEADER;

/****************************************************************************/	
// Structures used by ComputeMemoryEnginePLLTable
/****************************************************************************/	
#define COMPUTE_MEMORY_PLL_PARAM        1
#define COMPUTE_ENGINE_PLL_PARAM        2
#define ADJUST_MC_SETTING_PARAM         3

/****************************************************************************/	
// Structures used by AdjustMemoryControllerTable
/****************************************************************************/	
typedef struct _ATOM_ADJUST_MEMORY_CLOCK_FREQ
{
#if ATOM_BIG_ENDIAN
  ULONG ulPointerReturnFlag:1;      // BYTE_3[7]=1 - Return the pointer to the right Data Block; BYTE_3[7]=0 - Program the right Data Block 
  ULONG ulMemoryModuleNumber:7;     // BYTE_3[6:0]
  ULONG ulClockFreq:24;
#else
  ULONG ulClockFreq:24;
  ULONG ulMemoryModuleNumber:7;     // BYTE_3[6:0]
  ULONG ulPointerReturnFlag:1;      // BYTE_3[7]=1 - Return the pointer to the right Data Block; BYTE_3[7]=0 - Program the right Data Block 
#endif
}ATOM_ADJUST_MEMORY_CLOCK_FREQ;
#define POINTER_RETURN_FLAG             0x80

typedef struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS
{
  ULONG   ulClock;        //When returen, it's the re-calculated clock based on given Fb_div Post_Div and ref_div
  UCHAR   ucAction;       //0:reserved //1:Memory //2:Engine  
  UCHAR   ucReserved;     //may expand to return larger Fbdiv later
  UCHAR   ucFbDiv;        //return value
  UCHAR   ucPostDiv;      //return value
}COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS;

typedef struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V2
{
  ULONG   ulClock;        //When return, [23:0] return real clock 
  UCHAR   ucAction;       //0:reserved;COMPUTE_MEMORY_PLL_PARAM:Memory;COMPUTE_ENGINE_PLL_PARAM:Engine. it return ref_div to be written to register
  USHORT  usFbDiv;		    //return Feedback value to be written to register
  UCHAR   ucPostDiv;      //return post div to be written to register
}COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V2;
#define COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_PS_ALLOCATION   COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS


#define SET_CLOCK_FREQ_MASK                     0x00FFFFFF  //Clock change tables only take bit [23:0] as the requested clock value
#define USE_NON_BUS_CLOCK_MASK                  0x01000000  //Applicable to both memory and engine clock change, when set, it uses another clock as the temporary clock (engine uses memory and vice versa)
#define USE_MEMORY_SELF_REFRESH_MASK            0x02000000	//Only applicable to memory clock change, when set, using memory self refresh during clock transition
#define SKIP_INTERNAL_MEMORY_PARAMETER_CHANGE   0x04000000  //Only applicable to memory clock change, when set, the table will skip predefined internal memory parameter change
#define FIRST_TIME_CHANGE_CLOCK									0x08000000	//Applicable to both memory and engine clock change,when set, it means this is 1st time to change clock after ASIC bootup
#define SKIP_SW_PROGRAM_PLL											0x10000000	//Applicable to both memory and engine clock change, when set, it means the table will not program SPLL/MPLL
#define USE_SS_ENABLED_PIXEL_CLOCK  USE_NON_BUS_CLOCK_MASK

#define b3USE_NON_BUS_CLOCK_MASK                  0x01       //Applicable to both memory and engine clock change, when set, it uses another clock as the temporary clock (engine uses memory and vice versa)
#define b3USE_MEMORY_SELF_REFRESH                 0x02	     //Only applicable to memory clock change, when set, using memory self refresh during clock transition
#define b3SKIP_INTERNAL_MEMORY_PARAMETER_CHANGE   0x04       //Only applicable to memory clock change, when set, the table will skip predefined internal memory parameter change
#define b3FIRST_TIME_CHANGE_CLOCK									0x08       //Applicable to both memory and engine clock change,when set, it means this is 1st time to change clock after ASIC bootup
#define b3SKIP_SW_PROGRAM_PLL											0x10			 //Applicable to both memory and engine clock change, when set, it means the table will not program SPLL/MPLL

typedef struct _ATOM_COMPUTE_CLOCK_FREQ
{
#if ATOM_BIG_ENDIAN
  ULONG ulComputeClockFlag:8;                 // =1: COMPUTE_MEMORY_PLL_PARAM, =2: COMPUTE_ENGINE_PLL_PARAM
  ULONG ulClockFreq:24;                       // in unit of 10kHz
#else
  ULONG ulClockFreq:24;                       // in unit of 10kHz
  ULONG ulComputeClockFlag:8;                 // =1: COMPUTE_MEMORY_PLL_PARAM, =2: COMPUTE_ENGINE_PLL_PARAM
#endif
}ATOM_COMPUTE_CLOCK_FREQ;

typedef struct _ATOM_S_MPLL_FB_DIVIDER
{
  USHORT usFbDivFrac;  
  USHORT usFbDiv;  
}ATOM_S_MPLL_FB_DIVIDER;

typedef struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V3
{
  union
  {
    ATOM_COMPUTE_CLOCK_FREQ  ulClock;         //Input Parameter
    ULONG ulClockParams;                      //ULONG access for BE
    ATOM_S_MPLL_FB_DIVIDER   ulFbDiv;         //Output Parameter
  };
  UCHAR   ucRefDiv;                           //Output Parameter      
  UCHAR   ucPostDiv;                          //Output Parameter      
  UCHAR   ucCntlFlag;                         //Output Parameter      
  UCHAR   ucReserved;
}COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V3;

// ucCntlFlag
#define ATOM_PLL_CNTL_FLAG_PLL_POST_DIV_EN          1
#define ATOM_PLL_CNTL_FLAG_MPLL_VCO_MODE            2
#define ATOM_PLL_CNTL_FLAG_FRACTION_DISABLE         4
#define ATOM_PLL_CNTL_FLAG_SPLL_ISPARE_9						8


// V4 are only used for APU which PLL outside GPU
typedef struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V4
{
#if ATOM_BIG_ENDIAN
  ULONG  ucPostDiv:8;        //return parameter: post divider which is used to program to register directly
  ULONG  ulClock:24;         //Input= target clock, output = actual clock 
#else
  ULONG  ulClock:24;         //Input= target clock, output = actual clock 
  ULONG  ucPostDiv:8;        //return parameter: post divider which is used to program to register directly
#endif
}COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V4;

typedef struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V5
{
  union
  {
    ATOM_COMPUTE_CLOCK_FREQ  ulClock;         //Input Parameter
    ULONG ulClockParams;                      //ULONG access for BE
    ATOM_S_MPLL_FB_DIVIDER   ulFbDiv;         //Output Parameter
  };
  UCHAR   ucRefDiv;                           //Output Parameter      
  UCHAR   ucPostDiv;                          //Output Parameter      
  union
  {
    UCHAR   ucCntlFlag;                       //Output Flags
    UCHAR   ucInputFlag;                      //Input Flags. ucInputFlag[0] - Strobe(1)/Performance(0) mode
  };
  UCHAR   ucReserved;                       
}COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V5;


typedef struct _COMPUTE_GPU_CLOCK_INPUT_PARAMETERS_V1_6
{
  ATOM_COMPUTE_CLOCK_FREQ  ulClock;         //Input Parameter
  ULONG   ulReserved[2];
}COMPUTE_GPU_CLOCK_INPUT_PARAMETERS_V1_6;

//ATOM_COMPUTE_CLOCK_FREQ.ulComputeClockFlag
#define COMPUTE_GPUCLK_INPUT_FLAG_CLK_TYPE_MASK            0x0f
#define COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK           0x00
#define COMPUTE_GPUCLK_INPUT_FLAG_SCLK                     0x01

typedef struct _COMPUTE_GPU_CLOCK_OUTPUT_PARAMETERS_V1_6
{
  COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V4  ulClock;         //Output Parameter: ucPostDiv=DFS divider
  ATOM_S_MPLL_FB_DIVIDER   ulFbDiv;         //Output Parameter: PLL FB divider
  UCHAR   ucPllRefDiv;                      //Output Parameter: PLL ref divider      
  UCHAR   ucPllPostDiv;                     //Output Parameter: PLL post divider      
  UCHAR   ucPllCntlFlag;                    //Output Flags: control flag
  UCHAR   ucReserved;                       
}COMPUTE_GPU_CLOCK_OUTPUT_PARAMETERS_V1_6;

//ucPllCntlFlag
#define SPLL_CNTL_FLAG_VCO_MODE_MASK            0x03 


// ucInputFlag
#define ATOM_PLL_INPUT_FLAG_PLL_STROBE_MODE_EN  1   // 1-StrobeMode, 0-PerformanceMode

// use for ComputeMemoryClockParamTable
typedef struct _COMPUTE_MEMORY_CLOCK_PARAM_PARAMETERS_V2_1
{
  union
  {
    ULONG  ulClock;         
    ATOM_S_MPLL_FB_DIVIDER   ulFbDiv;         //Output:UPPER_WORD=FB_DIV_INTEGER,  LOWER_WORD=FB_DIV_FRAC shl (16-FB_FRACTION_BITS)
  };
  UCHAR   ucDllSpeed;                         //Output 
  UCHAR   ucPostDiv;                          //Output
  union{
    UCHAR   ucInputFlag;                      //Input : ATOM_PLL_INPUT_FLAG_PLL_STROBE_MODE_EN: 1-StrobeMode, 0-PerformanceMode
    UCHAR   ucPllCntlFlag;                    //Output: 
  };
  UCHAR   ucBWCntl;                       
}COMPUTE_MEMORY_CLOCK_PARAM_PARAMETERS_V2_1;

// definition of ucInputFlag
#define MPLL_INPUT_FLAG_STROBE_MODE_EN          0x01
// definition of ucPllCntlFlag
#define MPLL_CNTL_FLAG_VCO_MODE_MASK            0x03 
#define MPLL_CNTL_FLAG_BYPASS_DQ_PLL            0x04
#define MPLL_CNTL_FLAG_QDR_ENABLE               0x08
#define MPLL_CNTL_FLAG_AD_HALF_RATE             0x10

//MPLL_CNTL_FLAG_BYPASS_AD_PLL has a wrong name, should be BYPASS_DQ_PLL
#define MPLL_CNTL_FLAG_BYPASS_AD_PLL            0x04

typedef struct _DYNAMICE_MEMORY_SETTINGS_PARAMETER
{
  ATOM_COMPUTE_CLOCK_FREQ ulClock;
  ULONG ulReserved[2];
}DYNAMICE_MEMORY_SETTINGS_PARAMETER;

typedef struct _DYNAMICE_ENGINE_SETTINGS_PARAMETER
{
  ATOM_COMPUTE_CLOCK_FREQ ulClock;
  ULONG ulMemoryClock;
  ULONG ulReserved;
}DYNAMICE_ENGINE_SETTINGS_PARAMETER;

/****************************************************************************/	
// Structures used by SetEngineClockTable
/****************************************************************************/	
typedef struct _SET_ENGINE_CLOCK_PARAMETERS
{
  ULONG ulTargetEngineClock;          //In 10Khz unit
}SET_ENGINE_CLOCK_PARAMETERS;

typedef struct _SET_ENGINE_CLOCK_PS_ALLOCATION
{
  ULONG ulTargetEngineClock;          //In 10Khz unit
  COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_PS_ALLOCATION sReserved;
}SET_ENGINE_CLOCK_PS_ALLOCATION;

/****************************************************************************/	
// Structures used by SetMemoryClockTable
/****************************************************************************/	
typedef struct _SET_MEMORY_CLOCK_PARAMETERS
{
  ULONG ulTargetMemoryClock;          //In 10Khz unit
}SET_MEMORY_CLOCK_PARAMETERS;

typedef struct _SET_MEMORY_CLOCK_PS_ALLOCATION
{
  ULONG ulTargetMemoryClock;          //In 10Khz unit
  COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_PS_ALLOCATION sReserved;
}SET_MEMORY_CLOCK_PS_ALLOCATION;

/****************************************************************************/	
// Structures used by ASIC_Init.ctb
/****************************************************************************/	
typedef struct _ASIC_INIT_PARAMETERS
{
  ULONG ulDefaultEngineClock;         //In 10Khz unit
  ULONG ulDefaultMemoryClock;         //In 10Khz unit
}ASIC_INIT_PARAMETERS;

typedef struct _ASIC_INIT_PS_ALLOCATION
{
  ASIC_INIT_PARAMETERS sASICInitClocks;
  SET_ENGINE_CLOCK_PS_ALLOCATION sReserved; //Caller doesn't need to init this structure
}ASIC_INIT_PS_ALLOCATION;

/****************************************************************************/	
// Structure used by DynamicClockGatingTable.ctb
/****************************************************************************/	
typedef struct _DYNAMIC_CLOCK_GATING_PARAMETERS 
{
  UCHAR ucEnable;                     // ATOM_ENABLE or ATOM_DISABLE
  UCHAR ucPadding[3];
}DYNAMIC_CLOCK_GATING_PARAMETERS;
#define  DYNAMIC_CLOCK_GATING_PS_ALLOCATION  DYNAMIC_CLOCK_GATING_PARAMETERS

/****************************************************************************/	
// Structure used by EnableDispPowerGatingTable.ctb
/****************************************************************************/	
typedef struct _ENABLE_DISP_POWER_GATING_PARAMETERS_V2_1 
{
  UCHAR ucDispPipeId;                 // ATOM_CRTC1, ATOM_CRTC2, ...
  UCHAR ucEnable;                     // ATOM_ENABLE or ATOM_DISABLE
  UCHAR ucPadding[2];
}ENABLE_DISP_POWER_GATING_PARAMETERS_V2_1;

/****************************************************************************/	
// Structure used by EnableASIC_StaticPwrMgtTable.ctb
/****************************************************************************/	
typedef struct _ENABLE_ASIC_STATIC_PWR_MGT_PARAMETERS
{
  UCHAR ucEnable;                     // ATOM_ENABLE or ATOM_DISABLE
  UCHAR ucPadding[3];
}ENABLE_ASIC_STATIC_PWR_MGT_PARAMETERS;
#define ENABLE_ASIC_STATIC_PWR_MGT_PS_ALLOCATION  ENABLE_ASIC_STATIC_PWR_MGT_PARAMETERS

/****************************************************************************/	
// Structures used by DAC_LoadDetectionTable.ctb
/****************************************************************************/	
typedef struct _DAC_LOAD_DETECTION_PARAMETERS
{
  USHORT usDeviceID;                  //{ATOM_DEVICE_CRTx_SUPPORT,ATOM_DEVICE_TVx_SUPPORT,ATOM_DEVICE_CVx_SUPPORT}
  UCHAR  ucDacType;                   //{ATOM_DAC_A,ATOM_DAC_B, ATOM_EXT_DAC}
  UCHAR  ucMisc;											//Valid only when table revision =1.3 and above
}DAC_LOAD_DETECTION_PARAMETERS;

// DAC_LOAD_DETECTION_PARAMETERS.ucMisc
#define DAC_LOAD_MISC_YPrPb						0x01

typedef struct _DAC_LOAD_DETECTION_PS_ALLOCATION
{
  DAC_LOAD_DETECTION_PARAMETERS            sDacload;
  ULONG                                    Reserved[2];// Don't set this one, allocation for EXT DAC
}DAC_LOAD_DETECTION_PS_ALLOCATION;

/****************************************************************************/	
// Structures used by DAC1EncoderControlTable.ctb and DAC2EncoderControlTable.ctb
/****************************************************************************/	
typedef struct _DAC_ENCODER_CONTROL_PARAMETERS 
{
  USHORT usPixelClock;                // in 10KHz; for bios convenient
  UCHAR  ucDacStandard;               // See definition of ATOM_DACx_xxx, For DEC3.0, bit 7 used as internal flag to indicate DAC2 (==1) or DAC1 (==0)
  UCHAR  ucAction;                    // 0: turn off encoder
                                      // 1: setup and turn on encoder
                                      // 7: ATOM_ENCODER_INIT Initialize DAC
}DAC_ENCODER_CONTROL_PARAMETERS;

#define DAC_ENCODER_CONTROL_PS_ALLOCATION  DAC_ENCODER_CONTROL_PARAMETERS

/****************************************************************************/	
// Structures used by DIG1EncoderControlTable
//                    DIG2EncoderControlTable
//                    ExternalEncoderControlTable
/****************************************************************************/	
typedef struct _DIG_ENCODER_CONTROL_PARAMETERS
{
  USHORT usPixelClock;		// in 10KHz; for bios convenient
  UCHAR  ucConfig;		  
                            // [2] Link Select:
                            // =0: PHY linkA if bfLane<3
                            // =1: PHY linkB if bfLanes<3
                            // =0: PHY linkA+B if bfLanes=3
                            // [3] Transmitter Sel
                            // =0: UNIPHY or PCIEPHY
                            // =1: LVTMA 					
  UCHAR ucAction;           // =0: turn off encoder					
                            // =1: turn on encoder			
  UCHAR ucEncoderMode;
                            // =0: DP   encoder      
                            // =1: LVDS encoder          
                            // =2: DVI  encoder  
                            // =3: HDMI encoder
                            // =4: SDVO encoder
  UCHAR ucLaneNum;          // how many lanes to enable
  UCHAR ucReserved[2];
}DIG_ENCODER_CONTROL_PARAMETERS;
#define DIG_ENCODER_CONTROL_PS_ALLOCATION			  DIG_ENCODER_CONTROL_PARAMETERS
#define EXTERNAL_ENCODER_CONTROL_PARAMETER			DIG_ENCODER_CONTROL_PARAMETERS

//ucConfig
#define ATOM_ENCODER_CONFIG_DPLINKRATE_MASK				0x01
#define ATOM_ENCODER_CONFIG_DPLINKRATE_1_62GHZ		0x00
#define ATOM_ENCODER_CONFIG_DPLINKRATE_2_70GHZ		0x01
#define ATOM_ENCODER_CONFIG_DPLINKRATE_5_40GHZ		0x02
#define ATOM_ENCODER_CONFIG_LINK_SEL_MASK				  0x04
#define ATOM_ENCODER_CONFIG_LINKA								  0x00
#define ATOM_ENCODER_CONFIG_LINKB								  0x04
#define ATOM_ENCODER_CONFIG_LINKA_B							  ATOM_TRANSMITTER_CONFIG_LINKA
#define ATOM_ENCODER_CONFIG_LINKB_A							  ATOM_ENCODER_CONFIG_LINKB
#define ATOM_ENCODER_CONFIG_TRANSMITTER_SEL_MASK	0x08
#define ATOM_ENCODER_CONFIG_UNIPHY							  0x00
#define ATOM_ENCODER_CONFIG_LVTMA								  0x08
#define ATOM_ENCODER_CONFIG_TRANSMITTER1				  0x00
#define ATOM_ENCODER_CONFIG_TRANSMITTER2				  0x08
#define ATOM_ENCODER_CONFIG_DIGB								  0x80			// VBIOS Internal use, outside SW should set this bit=0
// ucAction
// ATOM_ENABLE:  Enable Encoder
// ATOM_DISABLE: Disable Encoder

//ucEncoderMode
#define ATOM_ENCODER_MODE_DP											0
#define ATOM_ENCODER_MODE_LVDS										1
#define ATOM_ENCODER_MODE_DVI											2
#define ATOM_ENCODER_MODE_HDMI										3
#define ATOM_ENCODER_MODE_SDVO										4
#define ATOM_ENCODER_MODE_DP_AUDIO                5
#define ATOM_ENCODER_MODE_TV											13
#define ATOM_ENCODER_MODE_CV											14
#define ATOM_ENCODER_MODE_CRT											15
#define ATOM_ENCODER_MODE_DVO											16
#define ATOM_ENCODER_MODE_DP_SST                  ATOM_ENCODER_MODE_DP    // For DP1.2
#define ATOM_ENCODER_MODE_DP_MST                  5                       // For DP1.2

typedef struct _ATOM_DIG_ENCODER_CONFIG_V2
{
#if ATOM_BIG_ENDIAN
    UCHAR ucReserved1:2;
    UCHAR ucTransmitterSel:2;     // =0: UniphyAB, =1: UniphyCD  =2: UniphyEF
    UCHAR ucLinkSel:1;            // =0: linkA/C/E =1: linkB/D/F
    UCHAR ucReserved:1;
    UCHAR ucDPLinkRate:1;         // =0: 1.62Ghz, =1: 2.7Ghz
#else
    UCHAR ucDPLinkRate:1;         // =0: 1.62Ghz, =1: 2.7Ghz
    UCHAR ucReserved:1;
    UCHAR ucLinkSel:1;            // =0: linkA/C/E =1: linkB/D/F
    UCHAR ucTransmitterSel:2;     // =0: UniphyAB, =1: UniphyCD  =2: UniphyEF
    UCHAR ucReserved1:2;
#endif
}ATOM_DIG_ENCODER_CONFIG_V2;


typedef struct _DIG_ENCODER_CONTROL_PARAMETERS_V2
{
  USHORT usPixelClock;      // in 10KHz; for bios convenient
  ATOM_DIG_ENCODER_CONFIG_V2 acConfig;
  UCHAR ucAction;                                       
  UCHAR ucEncoderMode;
                            // =0: DP   encoder      
                            // =1: LVDS encoder          
                            // =2: DVI  encoder  
                            // =3: HDMI encoder
                            // =4: SDVO encoder
  UCHAR ucLaneNum;          // how many lanes to enable
  UCHAR ucStatus;           // = DP_LINK_TRAINING_COMPLETE or DP_LINK_TRAINING_INCOMPLETE, only used by VBIOS with command ATOM_ENCODER_CMD_QUERY_DP_LINK_TRAINING_STATUS
  UCHAR ucReserved;
}DIG_ENCODER_CONTROL_PARAMETERS_V2;

//ucConfig
#define ATOM_ENCODER_CONFIG_V2_DPLINKRATE_MASK				0x01
#define ATOM_ENCODER_CONFIG_V2_DPLINKRATE_1_62GHZ		  0x00
#define ATOM_ENCODER_CONFIG_V2_DPLINKRATE_2_70GHZ		  0x01
#define ATOM_ENCODER_CONFIG_V2_LINK_SEL_MASK				  0x04
#define ATOM_ENCODER_CONFIG_V2_LINKA								  0x00
#define ATOM_ENCODER_CONFIG_V2_LINKB								  0x04
#define ATOM_ENCODER_CONFIG_V2_TRANSMITTER_SEL_MASK	  0x18
#define ATOM_ENCODER_CONFIG_V2_TRANSMITTER1				    0x00
#define ATOM_ENCODER_CONFIG_V2_TRANSMITTER2				    0x08
#define ATOM_ENCODER_CONFIG_V2_TRANSMITTER3				    0x10

// ucAction:
// ATOM_DISABLE
// ATOM_ENABLE
#define ATOM_ENCODER_CMD_DP_LINK_TRAINING_START       0x08
#define ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN1    0x09
#define ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN2    0x0a
#define ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN3    0x13
#define ATOM_ENCODER_CMD_DP_LINK_TRAINING_COMPLETE    0x0b
#define ATOM_ENCODER_CMD_DP_VIDEO_OFF                 0x0c
#define ATOM_ENCODER_CMD_DP_VIDEO_ON                  0x0d
#define ATOM_ENCODER_CMD_QUERY_DP_LINK_TRAINING_STATUS    0x0e
#define ATOM_ENCODER_CMD_SETUP                        0x0f
#define ATOM_ENCODER_CMD_SETUP_PANEL_MODE             0x10

// ucStatus
#define ATOM_ENCODER_STATUS_LINK_TRAINING_COMPLETE    0x10
#define ATOM_ENCODER_STATUS_LINK_TRAINING_INCOMPLETE  0x00

//ucTableFormatRevision=1
//ucTableContentRevision=3
// Following function ENABLE sub-function will be used by driver when TMDS/HDMI/LVDS is used, disable function will be used by driver
typedef struct _ATOM_DIG_ENCODER_CONFIG_V3
{
#if ATOM_BIG_ENDIAN
    UCHAR ucReserved1:1;
    UCHAR ucDigSel:3;             // =0/1/2/3/4/5: DIG0/1/2/3/4/5 (In register spec also referred as DIGA/B/C/D/E/F)
    UCHAR ucReserved:3;
    UCHAR ucDPLinkRate:1;         // =0: 1.62Ghz, =1: 2.7Ghz
#else
    UCHAR ucDPLinkRate:1;         // =0: 1.62Ghz, =1: 2.7Ghz
    UCHAR ucReserved:3;
    UCHAR ucDigSel:3;             // =0/1/2/3/4/5: DIG0/1/2/3/4/5 (In register spec also referred as DIGA/B/C/D/E/F)
    UCHAR ucReserved1:1;
#endif
}ATOM_DIG_ENCODER_CONFIG_V3;

#define ATOM_ENCODER_CONFIG_V3_DPLINKRATE_MASK				0x03
#define ATOM_ENCODER_CONFIG_V3_DPLINKRATE_1_62GHZ		  0x00
#define ATOM_ENCODER_CONFIG_V3_DPLINKRATE_2_70GHZ		  0x01
#define ATOM_ENCODER_CONFIG_V3_ENCODER_SEL					  0x70
#define ATOM_ENCODER_CONFIG_V3_DIG0_ENCODER					  0x00
#define ATOM_ENCODER_CONFIG_V3_DIG1_ENCODER					  0x10
#define ATOM_ENCODER_CONFIG_V3_DIG2_ENCODER					  0x20
#define ATOM_ENCODER_CONFIG_V3_DIG3_ENCODER					  0x30
#define ATOM_ENCODER_CONFIG_V3_DIG4_ENCODER					  0x40
#define ATOM_ENCODER_CONFIG_V3_DIG5_ENCODER					  0x50

typedef struct _DIG_ENCODER_CONTROL_PARAMETERS_V3
{
  USHORT usPixelClock;      // in 10KHz; for bios convenient
  ATOM_DIG_ENCODER_CONFIG_V3 acConfig;
  UCHAR ucAction;                              
  union {
    UCHAR ucEncoderMode;
                            // =0: DP   encoder      
                            // =1: LVDS encoder          
                            // =2: DVI  encoder  
                            // =3: HDMI encoder
                            // =4: SDVO encoder
                            // =5: DP audio
    UCHAR ucPanelMode;      // only valid when ucAction == ATOM_ENCODER_CMD_SETUP_PANEL_MODE
	                    // =0:     external DP
	                    // =1:     internal DP2
	                    // =0x11:  internal DP1 for NutMeg/Travis DP translator
  };
  UCHAR ucLaneNum;          // how many lanes to enable
  UCHAR ucBitPerColor;      // only valid for DP mode when ucAction = ATOM_ENCODER_CMD_SETUP
  UCHAR ucReserved;
}DIG_ENCODER_CONTROL_PARAMETERS_V3;

//ucTableFormatRevision=1
//ucTableContentRevision=4
// start from NI           
// Following function ENABLE sub-function will be used by driver when TMDS/HDMI/LVDS is used, disable function will be used by driver
typedef struct _ATOM_DIG_ENCODER_CONFIG_V4
{
#if ATOM_BIG_ENDIAN
    UCHAR ucReserved1:1;
    UCHAR ucDigSel:3;             // =0/1/2/3/4/5: DIG0/1/2/3/4/5 (In register spec also referred as DIGA/B/C/D/E/F)
    UCHAR ucReserved:2;
    UCHAR ucDPLinkRate:2;         // =0: 1.62Ghz, =1: 2.7Ghz, 2=5.4Ghz    <= Changed comparing to previous version
#else
    UCHAR ucDPLinkRate:2;         // =0: 1.62Ghz, =1: 2.7Ghz, 2=5.4Ghz    <= Changed comparing to previous version
    UCHAR ucReserved:2;
    UCHAR ucDigSel:3;             // =0/1/2/3/4/5: DIG0/1/2/3/4/5 (In register spec also referred as DIGA/B/C/D/E/F)
    UCHAR ucReserved1:1;
#endif
}ATOM_DIG_ENCODER_CONFIG_V4;

#define ATOM_ENCODER_CONFIG_V4_DPLINKRATE_MASK				0x03
#define ATOM_ENCODER_CONFIG_V4_DPLINKRATE_1_62GHZ		  0x00
#define ATOM_ENCODER_CONFIG_V4_DPLINKRATE_2_70GHZ		  0x01
#define ATOM_ENCODER_CONFIG_V4_DPLINKRATE_5_40GHZ		  0x02
#define ATOM_ENCODER_CONFIG_V4_DPLINKRATE_3_24GHZ		  0x03
#define ATOM_ENCODER_CONFIG_V4_ENCODER_SEL					  0x70
#define ATOM_ENCODER_CONFIG_V4_DIG0_ENCODER					  0x00
#define ATOM_ENCODER_CONFIG_V4_DIG1_ENCODER					  0x10
#define ATOM_ENCODER_CONFIG_V4_DIG2_ENCODER					  0x20
#define ATOM_ENCODER_CONFIG_V4_DIG3_ENCODER					  0x30
#define ATOM_ENCODER_CONFIG_V4_DIG4_ENCODER					  0x40
#define ATOM_ENCODER_CONFIG_V4_DIG5_ENCODER					  0x50
#define ATOM_ENCODER_CONFIG_V4_DIG6_ENCODER					  0x60

typedef struct _DIG_ENCODER_CONTROL_PARAMETERS_V4
{
  USHORT usPixelClock;      // in 10KHz; for bios convenient
  union{
  ATOM_DIG_ENCODER_CONFIG_V4 acConfig;
  UCHAR ucConfig;
  };
  UCHAR ucAction;                              
  union {
    UCHAR ucEncoderMode;
                            // =0: DP   encoder      
                            // =1: LVDS encoder          
                            // =2: DVI  encoder  
                            // =3: HDMI encoder
                            // =4: SDVO encoder
                            // =5: DP audio
    UCHAR ucPanelMode;      // only valid when ucAction == ATOM_ENCODER_CMD_SETUP_PANEL_MODE
	                    // =0:     external DP
	                    // =1:     internal DP2
	                    // =0x11:  internal DP1 for NutMeg/Travis DP translator
  };
  UCHAR ucLaneNum;          // how many lanes to enable
  UCHAR ucBitPerColor;      // only valid for DP mode when ucAction = ATOM_ENCODER_CMD_SETUP
  UCHAR ucHPD_ID;           // HPD ID (1-6). =0 means to skip HDP programming. New comparing to previous version
}DIG_ENCODER_CONTROL_PARAMETERS_V4;

// define ucBitPerColor: 
#define PANEL_BPC_UNDEFINE                               0x00
#define PANEL_6BIT_PER_COLOR                             0x01 
#define PANEL_8BIT_PER_COLOR                             0x02
#define PANEL_10BIT_PER_COLOR                            0x03
#define PANEL_12BIT_PER_COLOR                            0x04
#define PANEL_16BIT_PER_COLOR                            0x05

//define ucPanelMode
#define DP_PANEL_MODE_EXTERNAL_DP_MODE                   0x00
#define DP_PANEL_MODE_INTERNAL_DP2_MODE                  0x01
#define DP_PANEL_MODE_INTERNAL_DP1_MODE                  0x11

/****************************************************************************/	
// Structures used by UNIPHYTransmitterControlTable
//                    LVTMATransmitterControlTable
//                    DVOOutputControlTable
/****************************************************************************/	
typedef struct _ATOM_DP_VS_MODE
{
  UCHAR ucLaneSel;
  UCHAR ucLaneSet;
}ATOM_DP_VS_MODE;

typedef struct _DIG_TRANSMITTER_CONTROL_PARAMETERS
{
	union
	{
  USHORT usPixelClock;		// in 10KHz; for bios convenient
	USHORT usInitInfo;			// when init uniphy,lower 8bit is used for connector type defined in objectid.h
  ATOM_DP_VS_MODE asMode; // DP Voltage swing mode
	};
  UCHAR ucConfig;
													// [0]=0: 4 lane Link,      
													//    =1: 8 lane Link ( Dual Links TMDS ) 
                          // [1]=0: InCoherent mode   
													//    =1: Coherent Mode										
													// [2] Link Select:
  												// =0: PHY linkA   if bfLane<3
													// =1: PHY linkB   if bfLanes<3
		  										// =0: PHY linkA+B if bfLanes=3		
                          // [5:4]PCIE lane Sel
                          // =0: lane 0~3 or 0~7
                          // =1: lane 4~7
                          // =2: lane 8~11 or 8~15
                          // =3: lane 12~15 
	UCHAR ucAction;				  // =0: turn off encoder					
	                        // =1: turn on encoder			
  UCHAR ucReserved[4];
}DIG_TRANSMITTER_CONTROL_PARAMETERS;

#define DIG_TRANSMITTER_CONTROL_PS_ALLOCATION		DIG_TRANSMITTER_CONTROL_PARAMETERS					

//ucInitInfo
#define ATOM_TRAMITTER_INITINFO_CONNECTOR_MASK	0x00ff			

//ucConfig 
#define ATOM_TRANSMITTER_CONFIG_8LANE_LINK			0x01
#define ATOM_TRANSMITTER_CONFIG_COHERENT				0x02
#define ATOM_TRANSMITTER_CONFIG_LINK_SEL_MASK		0x04
#define ATOM_TRANSMITTER_CONFIG_LINKA						0x00
#define ATOM_TRANSMITTER_CONFIG_LINKB						0x04
#define ATOM_TRANSMITTER_CONFIG_LINKA_B					0x00			
#define ATOM_TRANSMITTER_CONFIG_LINKB_A					0x04

#define ATOM_TRANSMITTER_CONFIG_ENCODER_SEL_MASK	0x08			// only used when ATOM_TRANSMITTER_ACTION_ENABLE
#define ATOM_TRANSMITTER_CONFIG_DIG1_ENCODER		0x00				// only used when ATOM_TRANSMITTER_ACTION_ENABLE
#define ATOM_TRANSMITTER_CONFIG_DIG2_ENCODER		0x08				// only used when ATOM_TRANSMITTER_ACTION_ENABLE

#define ATOM_TRANSMITTER_CONFIG_CLKSRC_MASK			0x30
#define ATOM_TRANSMITTER_CONFIG_CLKSRC_PPLL			0x00
#define ATOM_TRANSMITTER_CONFIG_CLKSRC_PCIE			0x20
#define ATOM_TRANSMITTER_CONFIG_CLKSRC_XTALIN		0x30
#define ATOM_TRANSMITTER_CONFIG_LANE_SEL_MASK		0xc0
#define ATOM_TRANSMITTER_CONFIG_LANE_0_3				0x00
#define ATOM_TRANSMITTER_CONFIG_LANE_0_7				0x00
#define ATOM_TRANSMITTER_CONFIG_LANE_4_7				0x40
#define ATOM_TRANSMITTER_CONFIG_LANE_8_11				0x80
#define ATOM_TRANSMITTER_CONFIG_LANE_8_15				0x80
#define ATOM_TRANSMITTER_CONFIG_LANE_12_15			0xc0

//ucAction
#define ATOM_TRANSMITTER_ACTION_DISABLE					       0
#define ATOM_TRANSMITTER_ACTION_ENABLE					       1
#define ATOM_TRANSMITTER_ACTION_LCD_BLOFF				       2
#define ATOM_TRANSMITTER_ACTION_LCD_BLON				       3
#define ATOM_TRANSMITTER_ACTION_BL_BRIGHTNESS_CONTROL  4
#define ATOM_TRANSMITTER_ACTION_LCD_SELFTEST_START		 5
#define ATOM_TRANSMITTER_ACTION_LCD_SELFTEST_STOP			 6
#define ATOM_TRANSMITTER_ACTION_INIT						       7
#define ATOM_TRANSMITTER_ACTION_DISABLE_OUTPUT	       8
#define ATOM_TRANSMITTER_ACTION_ENABLE_OUTPUT		       9
#define ATOM_TRANSMITTER_ACTION_SETUP						       10
#define ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH           11
#define ATOM_TRANSMITTER_ACTION_POWER_ON               12
#define ATOM_TRANSMITTER_ACTION_POWER_OFF              13

// Following are used for DigTransmitterControlTable ver1.2
typedef struct _ATOM_DIG_TRANSMITTER_CONFIG_V2
{
#if ATOM_BIG_ENDIAN
  UCHAR ucTransmitterSel:2;         //bit7:6: =0 Dig Transmitter 1 ( Uniphy AB )
                                    //        =1 Dig Transmitter 2 ( Uniphy CD )
                                    //        =2 Dig Transmitter 3 ( Uniphy EF )
  UCHAR ucReserved:1;               
  UCHAR fDPConnector:1;             //bit4=0: DP connector  =1: None DP connector
  UCHAR ucEncoderSel:1;             //bit3=0: Data/Clk path source from DIGA( DIG inst0 ). =1: Data/clk path source from DIGB ( DIG inst1 )
  UCHAR ucLinkSel:1;                //bit2=0: Uniphy LINKA or C or E when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is A or C or E
                                    //    =1: Uniphy LINKB or D or F when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is B or D or F

  UCHAR fCoherentMode:1;            //bit1=1: Coherent Mode ( for DVI/HDMI mode )
  UCHAR fDualLinkConnector:1;       //bit0=1: Dual Link DVI connector
#else
  UCHAR fDualLinkConnector:1;       //bit0=1: Dual Link DVI connector
  UCHAR fCoherentMode:1;            //bit1=1: Coherent Mode ( for DVI/HDMI mode )
  UCHAR ucLinkSel:1;                //bit2=0: Uniphy LINKA or C or E when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is A or C or E
                                    //    =1: Uniphy LINKB or D or F when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is B or D or F
  UCHAR ucEncoderSel:1;             //bit3=0: Data/Clk path source from DIGA( DIG inst0 ). =1: Data/clk path source from DIGB ( DIG inst1 )
  UCHAR fDPConnector:1;             //bit4=0: DP connector  =1: None DP connector
  UCHAR ucReserved:1;               
  UCHAR ucTransmitterSel:2;         //bit7:6: =0 Dig Transmitter 1 ( Uniphy AB )
                                    //        =1 Dig Transmitter 2 ( Uniphy CD )
                                    //        =2 Dig Transmitter 3 ( Uniphy EF )
#endif
}ATOM_DIG_TRANSMITTER_CONFIG_V2;

//ucConfig 
//Bit0
#define ATOM_TRANSMITTER_CONFIG_V2_DUAL_LINK_CONNECTOR			0x01

//Bit1
#define ATOM_TRANSMITTER_CONFIG_V2_COHERENT				          0x02

//Bit2
#define ATOM_TRANSMITTER_CONFIG_V2_LINK_SEL_MASK		        0x04
#define ATOM_TRANSMITTER_CONFIG_V2_LINKA  			            0x00
#define ATOM_TRANSMITTER_CONFIG_V2_LINKB				            0x04

// Bit3
#define ATOM_TRANSMITTER_CONFIG_V2_ENCODER_SEL_MASK	        0x08
#define ATOM_TRANSMITTER_CONFIG_V2_DIG1_ENCODER		          0x00				// only used when ucAction == ATOM_TRANSMITTER_ACTION_ENABLE or ATOM_TRANSMITTER_ACTION_SETUP
#define ATOM_TRANSMITTER_CONFIG_V2_DIG2_ENCODER		          0x08				// only used when ucAction == ATOM_TRANSMITTER_ACTION_ENABLE or ATOM_TRANSMITTER_ACTION_SETUP

// Bit4
#define ATOM_TRASMITTER_CONFIG_V2_DP_CONNECTOR			        0x10

// Bit7:6
#define ATOM_TRANSMITTER_CONFIG_V2_TRANSMITTER_SEL_MASK     0xC0
#define ATOM_TRANSMITTER_CONFIG_V2_TRANSMITTER1           	0x00	//AB
#define ATOM_TRANSMITTER_CONFIG_V2_TRANSMITTER2           	0x40	//CD
#define ATOM_TRANSMITTER_CONFIG_V2_TRANSMITTER3           	0x80	//EF

typedef struct _DIG_TRANSMITTER_CONTROL_PARAMETERS_V2
{
	union
	{
  USHORT usPixelClock;		// in 10KHz; for bios convenient
	USHORT usInitInfo;			// when init uniphy,lower 8bit is used for connector type defined in objectid.h
  ATOM_DP_VS_MODE asMode; // DP Voltage swing mode
	};
  ATOM_DIG_TRANSMITTER_CONFIG_V2 acConfig;
	UCHAR ucAction;				  // define as ATOM_TRANSMITER_ACTION_XXX
  UCHAR ucReserved[4];
}DIG_TRANSMITTER_CONTROL_PARAMETERS_V2;

typedef struct _ATOM_DIG_TRANSMITTER_CONFIG_V3
{
#if ATOM_BIG_ENDIAN
  UCHAR ucTransmitterSel:2;         //bit7:6: =0 Dig Transmitter 1 ( Uniphy AB )
                                    //        =1 Dig Transmitter 2 ( Uniphy CD )
                                    //        =2 Dig Transmitter 3 ( Uniphy EF )
  UCHAR ucRefClkSource:2;           //bit5:4: PPLL1 =0, PPLL2=1, EXT_CLK=2
  UCHAR ucEncoderSel:1;             //bit3=0: Data/Clk path source from DIGA/C/E. =1: Data/clk path source from DIGB/D/F
  UCHAR ucLinkSel:1;                //bit2=0: Uniphy LINKA or C or E when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is A or C or E
                                    //    =1: Uniphy LINKB or D or F when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is B or D or F
  UCHAR fCoherentMode:1;            //bit1=1: Coherent Mode ( for DVI/HDMI mode )
  UCHAR fDualLinkConnector:1;       //bit0=1: Dual Link DVI connector
#else
  UCHAR fDualLinkConnector:1;       //bit0=1: Dual Link DVI connector
  UCHAR fCoherentMode:1;            //bit1=1: Coherent Mode ( for DVI/HDMI mode )
  UCHAR ucLinkSel:1;                //bit2=0: Uniphy LINKA or C or E when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is A or C or E
                                    //    =1: Uniphy LINKB or D or F when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is B or D or F
  UCHAR ucEncoderSel:1;             //bit3=0: Data/Clk path source from DIGA/C/E. =1: Data/clk path source from DIGB/D/F
  UCHAR ucRefClkSource:2;           //bit5:4: PPLL1 =0, PPLL2=1, EXT_CLK=2
  UCHAR ucTransmitterSel:2;         //bit7:6: =0 Dig Transmitter 1 ( Uniphy AB )
                                    //        =1 Dig Transmitter 2 ( Uniphy CD )
                                    //        =2 Dig Transmitter 3 ( Uniphy EF )
#endif
}ATOM_DIG_TRANSMITTER_CONFIG_V3;


typedef struct _DIG_TRANSMITTER_CONTROL_PARAMETERS_V3
{
	union
	{
    USHORT usPixelClock;		// in 10KHz; for bios convenient
	  USHORT usInitInfo;			// when init uniphy,lower 8bit is used for connector type defined in objectid.h
    ATOM_DP_VS_MODE asMode; // DP Voltage swing mode
	};
  ATOM_DIG_TRANSMITTER_CONFIG_V3 acConfig;
	UCHAR ucAction;				    // define as ATOM_TRANSMITER_ACTION_XXX
  UCHAR ucLaneNum;
  UCHAR ucReserved[3];
}DIG_TRANSMITTER_CONTROL_PARAMETERS_V3;

//ucConfig 
//Bit0
#define ATOM_TRANSMITTER_CONFIG_V3_DUAL_LINK_CONNECTOR			0x01

//Bit1
#define ATOM_TRANSMITTER_CONFIG_V3_COHERENT				          0x02

//Bit2
#define ATOM_TRANSMITTER_CONFIG_V3_LINK_SEL_MASK		        0x04
#define ATOM_TRANSMITTER_CONFIG_V3_LINKA  			            0x00
#define ATOM_TRANSMITTER_CONFIG_V3_LINKB				            0x04

// Bit3
#define ATOM_TRANSMITTER_CONFIG_V3_ENCODER_SEL_MASK	        0x08
#define ATOM_TRANSMITTER_CONFIG_V3_DIG1_ENCODER		          0x00
#define ATOM_TRANSMITTER_CONFIG_V3_DIG2_ENCODER		          0x08

// Bit5:4
#define ATOM_TRASMITTER_CONFIG_V3_REFCLK_SEL_MASK 	        0x30
#define ATOM_TRASMITTER_CONFIG_V3_P1PLL          		        0x00
#define ATOM_TRASMITTER_CONFIG_V3_P2PLL		                  0x10
#define ATOM_TRASMITTER_CONFIG_V3_REFCLK_SRC_EXT            0x20

// Bit7:6
#define ATOM_TRANSMITTER_CONFIG_V3_TRANSMITTER_SEL_MASK     0xC0
#define ATOM_TRANSMITTER_CONFIG_V3_TRANSMITTER1           	0x00	//AB
#define ATOM_TRANSMITTER_CONFIG_V3_TRANSMITTER2           	0x40	//CD
#define ATOM_TRANSMITTER_CONFIG_V3_TRANSMITTER3           	0x80	//EF


/****************************************************************************/	
// Structures used by UNIPHYTransmitterControlTable V1.4
// ASIC Families: NI
// ucTableFormatRevision=1
// ucTableContentRevision=4
/****************************************************************************/	
typedef struct _ATOM_DP_VS_MODE_V4
{
  UCHAR ucLaneSel;
 	union
 	{  
 	  UCHAR ucLaneSet;
 	  struct {
#if ATOM_BIG_ENDIAN
 		  UCHAR ucPOST_CURSOR2:2;         //Bit[7:6] Post Cursor2 Level      <= New in V4
 		  UCHAR ucPRE_EMPHASIS:3;         //Bit[5:3] Pre-emphasis Level
 		  UCHAR ucVOLTAGE_SWING:3;        //Bit[2:0] Voltage Swing Level
#else
 		  UCHAR ucVOLTAGE_SWING:3;        //Bit[2:0] Voltage Swing Level
 		  UCHAR ucPRE_EMPHASIS:3;         //Bit[5:3] Pre-emphasis Level
 		  UCHAR ucPOST_CURSOR2:2;         //Bit[7:6] Post Cursor2 Level      <= New in V4
#endif
 		};
 	}; 
}ATOM_DP_VS_MODE_V4;
 
typedef struct _ATOM_DIG_TRANSMITTER_CONFIG_V4
{
#if ATOM_BIG_ENDIAN
  UCHAR ucTransmitterSel:2;         //bit7:6: =0 Dig Transmitter 1 ( Uniphy AB )
                                    //        =1 Dig Transmitter 2 ( Uniphy CD )
                                    //        =2 Dig Transmitter 3 ( Uniphy EF )
  UCHAR ucRefClkSource:2;           //bit5:4: PPLL1 =0, PPLL2=1, DCPLL=2, EXT_CLK=3   <= New
  UCHAR ucEncoderSel:1;             //bit3=0: Data/Clk path source from DIGA/C/E. =1: Data/clk path source from DIGB/D/F
  UCHAR ucLinkSel:1;                //bit2=0: Uniphy LINKA or C or E when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is A or C or E
                                    //    =1: Uniphy LINKB or D or F when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is B or D or F
  UCHAR fCoherentMode:1;            //bit1=1: Coherent Mode ( for DVI/HDMI mode )
  UCHAR fDualLinkConnector:1;       //bit0=1: Dual Link DVI connector
#else
  UCHAR fDualLinkConnector:1;       //bit0=1: Dual Link DVI connector
  UCHAR fCoherentMode:1;            //bit1=1: Coherent Mode ( for DVI/HDMI mode )
  UCHAR ucLinkSel:1;                //bit2=0: Uniphy LINKA or C or E when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is A or C or E
                                    //    =1: Uniphy LINKB or D or F when fDualLinkConnector=0. when fDualLinkConnector=1, it means master link of dual link is B or D or F
  UCHAR ucEncoderSel:1;             //bit3=0: Data/Clk path source from DIGA/C/E. =1: Data/clk path source from DIGB/D/F
  UCHAR ucRefClkSource:2;           //bit5:4: PPLL1 =0, PPLL2=1, DCPLL=2, EXT_CLK=3   <= New
  UCHAR ucTransmitterSel:2;         //bit7:6: =0 Dig Transmitter 1 ( Uniphy AB )
                                    //        =1 Dig Transmitter 2 ( Uniphy CD )
                                    //        =2 Dig Transmitter 3 ( Uniphy EF )
#endif
}ATOM_DIG_TRANSMITTER_CONFIG_V4;

typedef struct _DIG_TRANSMITTER_CONTROL_PARAMETERS_V4
{
  union
  {
    USHORT usPixelClock;		// in 10KHz; for bios convenient
    USHORT usInitInfo;			// when init uniphy,lower 8bit is used for connector type defined in objectid.h
    ATOM_DP_VS_MODE_V4 asMode; // DP Voltage swing mode     Redefined comparing to previous version
  };
  union
  {
  ATOM_DIG_TRANSMITTER_CONFIG_V4 acConfig;
  UCHAR ucConfig;
  };
  UCHAR ucAction;				    // define as ATOM_TRANSMITER_ACTION_XXX	                        
  UCHAR ucLaneNum;
  UCHAR ucReserved[3];
}DIG_TRANSMITTER_CONTROL_PARAMETERS_V4;

//ucConfig 
//Bit0
#define ATOM_TRANSMITTER_CONFIG_V4_DUAL_LINK_CONNECTOR			0x01
//Bit1
#define ATOM_TRANSMITTER_CONFIG_V4_COHERENT				          0x02
//Bit2
#define ATOM_TRANSMITTER_CONFIG_V4_LINK_SEL_MASK		        0x04
#define ATOM_TRANSMITTER_CONFIG_V4_LINKA  			            0x00			
#define ATOM_TRANSMITTER_CONFIG_V4_LINKB				            0x04
// Bit3
#define ATOM_TRANSMITTER_CONFIG_V4_ENCODER_SEL_MASK	        0x08
#define ATOM_TRANSMITTER_CONFIG_V4_DIG1_ENCODER		          0x00				 
#define ATOM_TRANSMITTER_CONFIG_V4_DIG2_ENCODER		          0x08				
// Bit5:4
#define ATOM_TRANSMITTER_CONFIG_V4_REFCLK_SEL_MASK 	        0x30
#define ATOM_TRANSMITTER_CONFIG_V4_P1PLL         		        0x00
#define ATOM_TRANSMITTER_CONFIG_V4_P2PLL		                0x10
#define ATOM_TRANSMITTER_CONFIG_V4_DCPLL		                0x20   // New in _V4
#define ATOM_TRANSMITTER_CONFIG_V4_REFCLK_SRC_EXT           0x30   // Changed comparing to V3
// Bit7:6
#define ATOM_TRANSMITTER_CONFIG_V4_TRANSMITTER_SEL_MASK     0xC0
#define ATOM_TRANSMITTER_CONFIG_V4_TRANSMITTER1           	0x00	//AB
#define ATOM_TRANSMITTER_CONFIG_V4_TRANSMITTER2           	0x40	//CD
#define ATOM_TRANSMITTER_CONFIG_V4_TRANSMITTER3           	0x80	//EF


typedef struct _ATOM_DIG_TRANSMITTER_CONFIG_V5
{
#if ATOM_BIG_ENDIAN
  UCHAR ucReservd1:1;
  UCHAR ucHPDSel:3;
  UCHAR ucPhyClkSrcId:2;            
  UCHAR ucCoherentMode:1;            
  UCHAR ucReserved:1;
#else
  UCHAR ucReserved:1;
  UCHAR ucCoherentMode:1;            
  UCHAR ucPhyClkSrcId:2;            
  UCHAR ucHPDSel:3;
  UCHAR ucReservd1:1;
#endif
}ATOM_DIG_TRANSMITTER_CONFIG_V5;

typedef struct _DIG_TRANSMITTER_CONTROL_PARAMETERS_V1_5
{
  USHORT usSymClock;		        // Encoder Clock in 10kHz,(DP mode)= linkclock/10, (TMDS/LVDS/HDMI)= pixel clock,  (HDMI deep color), =pixel clock * deep_color_ratio
  UCHAR  ucPhyId;                   // 0=UNIPHYA, 1=UNIPHYB, 2=UNIPHYC, 3=UNIPHYD, 4= UNIPHYE 5=UNIPHYF
  UCHAR  ucAction;				    // define as ATOM_TRANSMITER_ACTION_xxx
  UCHAR  ucLaneNum;                 // indicate lane number 1-8
  UCHAR  ucConnObjId;               // Connector Object Id defined in ObjectId.h
  UCHAR  ucDigMode;                 // indicate DIG mode
  union{
  ATOM_DIG_TRANSMITTER_CONFIG_V5 asConfig;
  UCHAR ucConfig;
  };
  UCHAR  ucDigEncoderSel;           // indicate DIG front end encoder 
  UCHAR  ucDPLaneSet;
  UCHAR  ucReserved;
  UCHAR  ucReserved1;
}DIG_TRANSMITTER_CONTROL_PARAMETERS_V1_5;

//ucPhyId
#define ATOM_PHY_ID_UNIPHYA                                 0  
#define ATOM_PHY_ID_UNIPHYB                                 1
#define ATOM_PHY_ID_UNIPHYC                                 2
#define ATOM_PHY_ID_UNIPHYD                                 3
#define ATOM_PHY_ID_UNIPHYE                                 4
#define ATOM_PHY_ID_UNIPHYF                                 5
#define ATOM_PHY_ID_UNIPHYG                                 6

// ucDigEncoderSel
#define ATOM_TRANMSITTER_V5__DIGA_SEL                       0x01
#define ATOM_TRANMSITTER_V5__DIGB_SEL                       0x02
#define ATOM_TRANMSITTER_V5__DIGC_SEL                       0x04
#define ATOM_TRANMSITTER_V5__DIGD_SEL                       0x08
#define ATOM_TRANMSITTER_V5__DIGE_SEL                       0x10
#define ATOM_TRANMSITTER_V5__DIGF_SEL                       0x20
#define ATOM_TRANMSITTER_V5__DIGG_SEL                       0x40

// ucDigMode
#define ATOM_TRANSMITTER_DIGMODE_V5_DP                      0
#define ATOM_TRANSMITTER_DIGMODE_V5_LVDS                    1
#define ATOM_TRANSMITTER_DIGMODE_V5_DVI                     2
#define ATOM_TRANSMITTER_DIGMODE_V5_HDMI                    3
#define ATOM_TRANSMITTER_DIGMODE_V5_SDVO                    4
#define ATOM_TRANSMITTER_DIGMODE_V5_DP_MST                  5

// ucDPLaneSet
#define DP_LANE_SET__0DB_0_4V                               0x00
#define DP_LANE_SET__0DB_0_6V                               0x01
#define DP_LANE_SET__0DB_0_8V                               0x02
#define DP_LANE_SET__0DB_1_2V                               0x03
#define DP_LANE_SET__3_5DB_0_4V                             0x08  
#define DP_LANE_SET__3_5DB_0_6V                             0x09
#define DP_LANE_SET__3_5DB_0_8V                             0x0a
#define DP_LANE_SET__6DB_0_4V                               0x10
#define DP_LANE_SET__6DB_0_6V                               0x11
#define DP_LANE_SET__9_5DB_0_4V                             0x18  

// ATOM_DIG_TRANSMITTER_CONFIG_V5 asConfig;
// Bit1
#define ATOM_TRANSMITTER_CONFIG_V5_COHERENT				          0x02

// Bit3:2
#define ATOM_TRANSMITTER_CONFIG_V5_REFCLK_SEL_MASK 	        0x0c
#define ATOM_TRANSMITTER_CONFIG_V5_REFCLK_SEL_SHIFT		    0x02

#define ATOM_TRANSMITTER_CONFIG_V5_P1PLL         		        0x00
#define ATOM_TRANSMITTER_CONFIG_V5_P2PLL		                0x04
#define ATOM_TRANSMITTER_CONFIG_V5_P0PLL		                0x08   
#define ATOM_TRANSMITTER_CONFIG_V5_REFCLK_SRC_EXT           0x0c
// Bit6:4
#define ATOM_TRANSMITTER_CONFIG_V5_HPD_SEL_MASK		          0x70
#define ATOM_TRANSMITTER_CONFIG_V5_HPD_SEL_SHIFT		      0x04

#define ATOM_TRANSMITTER_CONFIG_V5_NO_HPD_SEL				        0x00
#define ATOM_TRANSMITTER_CONFIG_V5_HPD1_SEL				          0x10
#define ATOM_TRANSMITTER_CONFIG_V5_HPD2_SEL				          0x20
#define ATOM_TRANSMITTER_CONFIG_V5_HPD3_SEL				          0x30
#define ATOM_TRANSMITTER_CONFIG_V5_HPD4_SEL				          0x40
#define ATOM_TRANSMITTER_CONFIG_V5_HPD5_SEL				          0x50
#define ATOM_TRANSMITTER_CONFIG_V5_HPD6_SEL				          0x60

#define DIG_TRANSMITTER_CONTROL_PS_ALLOCATION_V1_5            DIG_TRANSMITTER_CONTROL_PARAMETERS_V1_5


/****************************************************************************/	
// Structures used by ExternalEncoderControlTable V1.3
// ASIC Families: Evergreen, Llano, NI
// ucTableFormatRevision=1
// ucTableContentRevision=3
/****************************************************************************/	

typedef struct _EXTERNAL_ENCODER_CONTROL_PARAMETERS_V3
{
  union{
  USHORT usPixelClock;      // pixel clock in 10Khz, valid when ucAction=SETUP/ENABLE_OUTPUT 
  USHORT usConnectorId;     // connector id, valid when ucAction = INIT
  };
  UCHAR  ucConfig;          // indicate which encoder, and DP link rate when ucAction = SETUP/ENABLE_OUTPUT  
  UCHAR  ucAction;          // 
  UCHAR  ucEncoderMode;     // encoder mode, only used when ucAction = SETUP/ENABLE_OUTPUT
  UCHAR  ucLaneNum;         // lane number, only used when ucAction = SETUP/ENABLE_OUTPUT  
  UCHAR  ucBitPerColor;     // output bit per color, only valid when ucAction = SETUP/ENABLE_OUTPUT and ucEncodeMode= DP
  UCHAR  ucReserved;        
}EXTERNAL_ENCODER_CONTROL_PARAMETERS_V3;

// ucAction
#define EXTERNAL_ENCODER_ACTION_V3_DISABLE_OUTPUT         0x00
#define EXTERNAL_ENCODER_ACTION_V3_ENABLE_OUTPUT          0x01
#define EXTERNAL_ENCODER_ACTION_V3_ENCODER_INIT           0x07
#define EXTERNAL_ENCODER_ACTION_V3_ENCODER_SETUP          0x0f
#define EXTERNAL_ENCODER_ACTION_V3_ENCODER_BLANKING_OFF   0x10
#define EXTERNAL_ENCODER_ACTION_V3_ENCODER_BLANKING       0x11
#define EXTERNAL_ENCODER_ACTION_V3_DACLOAD_DETECTION      0x12
#define EXTERNAL_ENCODER_ACTION_V3_DDC_SETUP              0x14

// ucConfig
#define EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_MASK				0x03
#define EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_1_62GHZ		  0x00
#define EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_2_70GHZ		  0x01
#define EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_5_40GHZ		  0x02
#define EXTERNAL_ENCODER_CONFIG_V3_ENCODER_SEL_MASK		    0x70
#define EXTERNAL_ENCODER_CONFIG_V3_ENCODER1		            0x00
#define EXTERNAL_ENCODER_CONFIG_V3_ENCODER2		            0x10
#define EXTERNAL_ENCODER_CONFIG_V3_ENCODER3		            0x20

typedef struct _EXTERNAL_ENCODER_CONTROL_PS_ALLOCATION_V3
{
  EXTERNAL_ENCODER_CONTROL_PARAMETERS_V3 sExtEncoder;
  ULONG ulReserved[2];
}EXTERNAL_ENCODER_CONTROL_PS_ALLOCATION_V3;


/****************************************************************************/	
// Structures used by DAC1OuputControlTable
//                    DAC2OuputControlTable
//                    LVTMAOutputControlTable  (Before DEC30)
//                    TMDSAOutputControlTable  (Before DEC30)
/****************************************************************************/	
typedef struct _DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS
{
  UCHAR  ucAction;                    // Possible input:ATOM_ENABLE||ATOMDISABLE
                                      // When the display is LCD, in addition to above:
                                      // ATOM_LCD_BLOFF|| ATOM_LCD_BLON ||ATOM_LCD_BL_BRIGHTNESS_CONTROL||ATOM_LCD_SELFTEST_START||
                                      // ATOM_LCD_SELFTEST_STOP
                                      
  UCHAR  aucPadding[3];               // padding to DWORD aligned
}DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS;

#define DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS


#define CRT1_OUTPUT_CONTROL_PARAMETERS     DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS 
#define CRT1_OUTPUT_CONTROL_PS_ALLOCATION  DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION

#define CRT2_OUTPUT_CONTROL_PARAMETERS     DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS 
#define CRT2_OUTPUT_CONTROL_PS_ALLOCATION  DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION

#define CV1_OUTPUT_CONTROL_PARAMETERS      DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS
#define CV1_OUTPUT_CONTROL_PS_ALLOCATION   DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION

#define TV1_OUTPUT_CONTROL_PARAMETERS      DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS
#define TV1_OUTPUT_CONTROL_PS_ALLOCATION   DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION

#define DFP1_OUTPUT_CONTROL_PARAMETERS     DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS
#define DFP1_OUTPUT_CONTROL_PS_ALLOCATION  DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION

#define DFP2_OUTPUT_CONTROL_PARAMETERS     DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS
#define DFP2_OUTPUT_CONTROL_PS_ALLOCATION  DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION

#define LCD1_OUTPUT_CONTROL_PARAMETERS     DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS
#define LCD1_OUTPUT_CONTROL_PS_ALLOCATION  DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION

#define DVO_OUTPUT_CONTROL_PARAMETERS      DISPLAY_DEVICE_OUTPUT_CONTROL_PARAMETERS
#define DVO_OUTPUT_CONTROL_PS_ALLOCATION   DIG_TRANSMITTER_CONTROL_PS_ALLOCATION
#define DVO_OUTPUT_CONTROL_PARAMETERS_V3	 DIG_TRANSMITTER_CONTROL_PARAMETERS

/****************************************************************************/	
// Structures used by BlankCRTCTable
/****************************************************************************/	
typedef struct _BLANK_CRTC_PARAMETERS
{
  UCHAR  ucCRTC;                    	// ATOM_CRTC1 or ATOM_CRTC2
  UCHAR  ucBlanking;                  // ATOM_BLANKING or ATOM_BLANKINGOFF
  USHORT usBlackColorRCr;
  USHORT usBlackColorGY;
  USHORT usBlackColorBCb;
}BLANK_CRTC_PARAMETERS;
#define BLANK_CRTC_PS_ALLOCATION    BLANK_CRTC_PARAMETERS

/****************************************************************************/	
// Structures used by EnableCRTCTable
//                    EnableCRTCMemReqTable
//                    UpdateCRTC_DoubleBufferRegistersTable
/****************************************************************************/	
typedef struct _ENABLE_CRTC_PARAMETERS
{
  UCHAR ucCRTC;                    	  // ATOM_CRTC1 or ATOM_CRTC2
  UCHAR ucEnable;                     // ATOM_ENABLE or ATOM_DISABLE 
  UCHAR ucPadding[2];
}ENABLE_CRTC_PARAMETERS;
#define ENABLE_CRTC_PS_ALLOCATION   ENABLE_CRTC_PARAMETERS

/****************************************************************************/	
// Structures used by SetCRTC_OverScanTable
/****************************************************************************/	
typedef struct _SET_CRTC_OVERSCAN_PARAMETERS
{
  USHORT usOverscanRight;             // right
  USHORT usOverscanLeft;              // left
  USHORT usOverscanBottom;            // bottom
  USHORT usOverscanTop;               // top
  UCHAR  ucCRTC;                      // ATOM_CRTC1 or ATOM_CRTC2
  UCHAR  ucPadding[3];
}SET_CRTC_OVERSCAN_PARAMETERS;
#define SET_CRTC_OVERSCAN_PS_ALLOCATION  SET_CRTC_OVERSCAN_PARAMETERS

/****************************************************************************/	
// Structures used by SetCRTC_ReplicationTable
/****************************************************************************/	
typedef struct _SET_CRTC_REPLICATION_PARAMETERS
{
  UCHAR ucH_Replication;              // horizontal replication
  UCHAR ucV_Replication;              // vertical replication
  UCHAR usCRTC;                       // ATOM_CRTC1 or ATOM_CRTC2
  UCHAR ucPadding;
}SET_CRTC_REPLICATION_PARAMETERS;
#define SET_CRTC_REPLICATION_PS_ALLOCATION  SET_CRTC_REPLICATION_PARAMETERS

/****************************************************************************/	
// Structures used by SelectCRTC_SourceTable
/****************************************************************************/	
typedef struct _SELECT_CRTC_SOURCE_PARAMETERS
{
  UCHAR ucCRTC;                    	  // ATOM_CRTC1 or ATOM_CRTC2
  UCHAR ucDevice;                     // ATOM_DEVICE_CRT1|ATOM_DEVICE_CRT2|....
  UCHAR ucPadding[2];
}SELECT_CRTC_SOURCE_PARAMETERS;
#define SELECT_CRTC_SOURCE_PS_ALLOCATION  SELECT_CRTC_SOURCE_PARAMETERS

typedef struct _SELECT_CRTC_SOURCE_PARAMETERS_V2
{
  UCHAR ucCRTC;                    	  // ATOM_CRTC1 or ATOM_CRTC2
  UCHAR ucEncoderID;                  // DAC1/DAC2/TVOUT/DIG1/DIG2/DVO
  UCHAR ucEncodeMode;									// Encoding mode, only valid when using DIG1/DIG2/DVO
  UCHAR ucPadding;
}SELECT_CRTC_SOURCE_PARAMETERS_V2;

//ucEncoderID
//#define ASIC_INT_DAC1_ENCODER_ID    						0x00 
//#define ASIC_INT_TV_ENCODER_ID									0x02
//#define ASIC_INT_DIG1_ENCODER_ID								0x03
//#define ASIC_INT_DAC2_ENCODER_ID								0x04
//#define ASIC_EXT_TV_ENCODER_ID									0x06
//#define ASIC_INT_DVO_ENCODER_ID									0x07
//#define ASIC_INT_DIG2_ENCODER_ID								0x09
//#define ASIC_EXT_DIG_ENCODER_ID									0x05

//ucEncodeMode
//#define ATOM_ENCODER_MODE_DP										0
//#define ATOM_ENCODER_MODE_LVDS									1
//#define ATOM_ENCODER_MODE_DVI										2
//#define ATOM_ENCODER_MODE_HDMI									3
//#define ATOM_ENCODER_MODE_SDVO									4
//#define ATOM_ENCODER_MODE_TV										13
//#define ATOM_ENCODER_MODE_CV										14
//#define ATOM_ENCODER_MODE_CRT										15

/****************************************************************************/	
// Structures used by SetPixelClockTable
//                    GetPixelClockTable 
/****************************************************************************/	
//Major revision=1., Minor revision=1
typedef struct _PIXEL_CLOCK_PARAMETERS
{
  USHORT usPixelClock;                // in 10kHz unit; for bios convenient = (RefClk*FB_Div)/(Ref_Div*Post_Div)
                                      // 0 means disable PPLL
  USHORT usRefDiv;                    // Reference divider
  USHORT usFbDiv;                     // feedback divider
  UCHAR  ucPostDiv;                   // post divider	
  UCHAR  ucFracFbDiv;                 // fractional feedback divider
  UCHAR  ucPpll;                      // ATOM_PPLL1 or ATOM_PPL2
  UCHAR  ucRefDivSrc;                 // ATOM_PJITTER or ATO_NONPJITTER
  UCHAR  ucCRTC;                      // Which CRTC uses this Ppll
  UCHAR  ucPadding;
}PIXEL_CLOCK_PARAMETERS;

//Major revision=1., Minor revision=2, add ucMiscIfno
//ucMiscInfo:
#define MISC_FORCE_REPROG_PIXEL_CLOCK 0x1
#define MISC_DEVICE_INDEX_MASK        0xF0
#define MISC_DEVICE_INDEX_SHIFT       4

typedef struct _PIXEL_CLOCK_PARAMETERS_V2
{
  USHORT usPixelClock;                // in 10kHz unit; for bios convenient = (RefClk*FB_Div)/(Ref_Div*Post_Div)
                                      // 0 means disable PPLL
  USHORT usRefDiv;                    // Reference divider
  USHORT usFbDiv;                     // feedback divider
  UCHAR  ucPostDiv;                   // post divider	
  UCHAR  ucFracFbDiv;                 // fractional feedback divider
  UCHAR  ucPpll;                      // ATOM_PPLL1 or ATOM_PPL2
  UCHAR  ucRefDivSrc;                 // ATOM_PJITTER or ATO_NONPJITTER
  UCHAR  ucCRTC;                      // Which CRTC uses this Ppll
  UCHAR  ucMiscInfo;                  // Different bits for different purpose, bit [7:4] as device index, bit[0]=Force prog
}PIXEL_CLOCK_PARAMETERS_V2;

//Major revision=1., Minor revision=3, structure/definition change
//ucEncoderMode:
//ATOM_ENCODER_MODE_DP
//ATOM_ENOCDER_MODE_LVDS
//ATOM_ENOCDER_MODE_DVI
//ATOM_ENOCDER_MODE_HDMI
//ATOM_ENOCDER_MODE_SDVO
//ATOM_ENCODER_MODE_TV										13
//ATOM_ENCODER_MODE_CV										14
//ATOM_ENCODER_MODE_CRT										15

//ucDVOConfig
//#define DVO_ENCODER_CONFIG_RATE_SEL							0x01
//#define DVO_ENCODER_CONFIG_DDR_SPEED						0x00
//#define DVO_ENCODER_CONFIG_SDR_SPEED						0x01
//#define DVO_ENCODER_CONFIG_OUTPUT_SEL						0x0c
//#define DVO_ENCODER_CONFIG_LOW12BIT							0x00
//#define DVO_ENCODER_CONFIG_UPPER12BIT						0x04
//#define DVO_ENCODER_CONFIG_24BIT								0x08

//ucMiscInfo: also changed, see below
#define PIXEL_CLOCK_MISC_FORCE_PROG_PPLL						0x01
#define PIXEL_CLOCK_MISC_VGA_MODE										0x02
#define PIXEL_CLOCK_MISC_CRTC_SEL_MASK							0x04
#define PIXEL_CLOCK_MISC_CRTC_SEL_CRTC1							0x00
#define PIXEL_CLOCK_MISC_CRTC_SEL_CRTC2							0x04
#define PIXEL_CLOCK_MISC_USE_ENGINE_FOR_DISPCLK			0x08
#define PIXEL_CLOCK_MISC_REF_DIV_SRC                    0x10
// V1.4 for RoadRunner
#define PIXEL_CLOCK_V4_MISC_SS_ENABLE               0x10
#define PIXEL_CLOCK_V4_MISC_COHERENT_MODE           0x20


typedef struct _PIXEL_CLOCK_PARAMETERS_V3
{
  USHORT usPixelClock;                // in 10kHz unit; for bios convenient = (RefClk*FB_Div)/(Ref_Div*Post_Div)
                                      // 0 means disable PPLL. For VGA PPLL,make sure this value is not 0.
  USHORT usRefDiv;                    // Reference divider
  USHORT usFbDiv;                     // feedback divider
  UCHAR  ucPostDiv;                   // post divider	
  UCHAR  ucFracFbDiv;                 // fractional feedback divider
  UCHAR  ucPpll;                      // ATOM_PPLL1 or ATOM_PPL2
  UCHAR  ucTransmitterId;             // graphic encoder id defined in objectId.h
	union
	{
  UCHAR  ucEncoderMode;               // encoder type defined as ATOM_ENCODER_MODE_DP/DVI/HDMI/
	UCHAR  ucDVOConfig;									// when use DVO, need to know SDR/DDR, 12bit or 24bit
	};
  UCHAR  ucMiscInfo;                  // bit[0]=Force program, bit[1]= set pclk for VGA, b[2]= CRTC sel
                                      // bit[3]=0:use PPLL for dispclk source, =1: use engine clock for dispclock source
                                      // bit[4]=0:use XTALIN as the source of reference divider,=1 use the pre-defined clock as the source of reference divider
}PIXEL_CLOCK_PARAMETERS_V3;

#define PIXEL_CLOCK_PARAMETERS_LAST			PIXEL_CLOCK_PARAMETERS_V2
#define GET_PIXEL_CLOCK_PS_ALLOCATION		PIXEL_CLOCK_PARAMETERS_LAST

typedef struct _PIXEL_CLOCK_PARAMETERS_V5
{
  UCHAR  ucCRTC;             // ATOM_CRTC1~6, indicate the CRTC controller to 
                             // drive the pixel clock. not used for DCPLL case.
  union{
  UCHAR  ucReserved;
  UCHAR  ucFracFbDiv;        // [gphan] temporary to prevent build problem.  remove it after driver code is changed.
  };
  USHORT usPixelClock;       // target the pixel clock to drive the CRTC timing
                             // 0 means disable PPLL/DCPLL. 
  USHORT usFbDiv;            // feedback divider integer part. 
  UCHAR  ucPostDiv;          // post divider. 
  UCHAR  ucRefDiv;           // Reference divider
  UCHAR  ucPpll;             // ATOM_PPLL1/ATOM_PPLL2/ATOM_DCPLL
  UCHAR  ucTransmitterID;    // ASIC encoder id defined in objectId.h, 
                             // indicate which graphic encoder will be used. 
  UCHAR  ucEncoderMode;      // Encoder mode: 
  UCHAR  ucMiscInfo;         // bit[0]= Force program PPLL 
                             // bit[1]= when VGA timing is used. 
                             // bit[3:2]= HDMI panel bit depth: =0: 24bpp =1:30bpp, =2:32bpp
                             // bit[4]= RefClock source for PPLL. 
                             // =0: XTLAIN( default mode )
	                           // =1: other external clock source, which is pre-defined
                             //     by VBIOS depend on the feature required.
                             // bit[7:5]: reserved.
  ULONG  ulFbDivDecFrac;     // 20 bit feedback divider decimal fraction part, range from 1~999999 ( 0.000001 to 0.999999 )

}PIXEL_CLOCK_PARAMETERS_V5;

#define PIXEL_CLOCK_V5_MISC_FORCE_PROG_PPLL					0x01
#define PIXEL_CLOCK_V5_MISC_VGA_MODE								0x02
#define PIXEL_CLOCK_V5_MISC_HDMI_BPP_MASK           0x0c
#define PIXEL_CLOCK_V5_MISC_HDMI_24BPP              0x00
#define PIXEL_CLOCK_V5_MISC_HDMI_30BPP              0x04
#define PIXEL_CLOCK_V5_MISC_HDMI_32BPP              0x08
#define PIXEL_CLOCK_V5_MISC_REF_DIV_SRC             0x10

typedef struct _CRTC_PIXEL_CLOCK_FREQ
{
#if ATOM_BIG_ENDIAN
  ULONG  ucCRTC:8;            // ATOM_CRTC1~6, indicate the CRTC controller to 
                              // drive the pixel clock. not used for DCPLL case.
  ULONG  ulPixelClock:24;     // target the pixel clock to drive the CRTC timing. 
                              // 0 means disable PPLL/DCPLL. Expanded to 24 bits comparing to previous version.
#else
  ULONG  ulPixelClock:24;     // target the pixel clock to drive the CRTC timing. 
                              // 0 means disable PPLL/DCPLL. Expanded to 24 bits comparing to previous version.
  ULONG  ucCRTC:8;            // ATOM_CRTC1~6, indicate the CRTC controller to 
                              // drive the pixel clock. not used for DCPLL case.
#endif
}CRTC_PIXEL_CLOCK_FREQ;

typedef struct _PIXEL_CLOCK_PARAMETERS_V6
{
  union{
    CRTC_PIXEL_CLOCK_FREQ ulCrtcPclkFreq;    // pixel clock and CRTC id frequency 
    ULONG ulDispEngClkFreq;                  // dispclk frequency
  };
  USHORT usFbDiv;            // feedback divider integer part. 
  UCHAR  ucPostDiv;          // post divider. 
  UCHAR  ucRefDiv;           // Reference divider
  UCHAR  ucPpll;             // ATOM_PPLL1/ATOM_PPLL2/ATOM_DCPLL
  UCHAR  ucTransmitterID;    // ASIC encoder id defined in objectId.h, 
                             // indicate which graphic encoder will be used. 
  UCHAR  ucEncoderMode;      // Encoder mode: 
  UCHAR  ucMiscInfo;         // bit[0]= Force program PPLL 
                             // bit[1]= when VGA timing is used. 
                             // bit[3:2]= HDMI panel bit depth: =0: 24bpp =1:30bpp, =2:32bpp
                             // bit[4]= RefClock source for PPLL. 
                             // =0: XTLAIN( default mode )
	                           // =1: other external clock source, which is pre-defined                                            
                             //     by VBIOS depend on the feature required.
                             // bit[7:5]: reserved.
  ULONG  ulFbDivDecFrac;     // 20 bit feedback divider decimal fraction part, range from 1~999999 ( 0.000001 to 0.999999 )

}PIXEL_CLOCK_PARAMETERS_V6;

#define PIXEL_CLOCK_V6_MISC_FORCE_PROG_PPLL					0x01
#define PIXEL_CLOCK_V6_MISC_VGA_MODE								0x02
#define PIXEL_CLOCK_V6_MISC_HDMI_BPP_MASK           0x0c
#define PIXEL_CLOCK_V6_MISC_HDMI_24BPP              0x00
#define PIXEL_CLOCK_V6_MISC_HDMI_36BPP              0x04
#define PIXEL_CLOCK_V6_MISC_HDMI_36BPP_V6           0x08    //for V6, the correct defintion for 36bpp should be 2 for 36bpp(2:1)
#define PIXEL_CLOCK_V6_MISC_HDMI_30BPP              0x08
#define PIXEL_CLOCK_V6_MISC_HDMI_30BPP_V6           0x04    //for V6, the correct defintion for 30bpp should be 1 for 36bpp(5:4)
#define PIXEL_CLOCK_V6_MISC_HDMI_48BPP              0x0c
#define PIXEL_CLOCK_V6_MISC_REF_DIV_SRC             0x10
#define PIXEL_CLOCK_V6_MISC_GEN_DPREFCLK            0x40

typedef struct _GET_DISP_PLL_STATUS_INPUT_PARAMETERS_V2
{
  PIXEL_CLOCK_PARAMETERS_V3 sDispClkInput;
}GET_DISP_PLL_STATUS_INPUT_PARAMETERS_V2;

typedef struct _GET_DISP_PLL_STATUS_OUTPUT_PARAMETERS_V2
{
  UCHAR  ucStatus;
  UCHAR  ucRefDivSrc;                 // =1: reference clock source from XTALIN, =0: source from PCIE ref clock
  UCHAR  ucReserved[2];
}GET_DISP_PLL_STATUS_OUTPUT_PARAMETERS_V2;

typedef struct _GET_DISP_PLL_STATUS_INPUT_PARAMETERS_V3
{
  PIXEL_CLOCK_PARAMETERS_V5 sDispClkInput;
}GET_DISP_PLL_STATUS_INPUT_PARAMETERS_V3;

/****************************************************************************/	
// Structures used by AdjustDisplayPllTable
/****************************************************************************/	
typedef struct _ADJUST_DISPLAY_PLL_PARAMETERS
{
	USHORT usPixelClock;
	UCHAR ucTransmitterID;
	UCHAR ucEncodeMode;
	union
	{
		UCHAR ucDVOConfig;									//if DVO, need passing link rate and output 12bitlow or 24bit
		UCHAR ucConfig;											//if none DVO, not defined yet
	};
	UCHAR ucReserved[3];
}ADJUST_DISPLAY_PLL_PARAMETERS;

#define ADJUST_DISPLAY_CONFIG_SS_ENABLE       0x10
#define ADJUST_DISPLAY_PLL_PS_ALLOCATION			ADJUST_DISPLAY_PLL_PARAMETERS

typedef struct _ADJUST_DISPLAY_PLL_INPUT_PARAMETERS_V3
{
	USHORT usPixelClock;                    // target pixel clock
	UCHAR ucTransmitterID;                  // GPU transmitter id defined in objectid.h
	UCHAR ucEncodeMode;                     // encoder mode: CRT, LVDS, DP, TMDS or HDMI
  UCHAR ucDispPllConfig;                 // display pll configure parameter defined as following DISPPLL_CONFIG_XXXX
  UCHAR ucExtTransmitterID;               // external encoder id.
	UCHAR ucReserved[2];
}ADJUST_DISPLAY_PLL_INPUT_PARAMETERS_V3;

// usDispPllConfig v1.2 for RoadRunner
#define DISPPLL_CONFIG_DVO_RATE_SEL                0x0001     // need only when ucTransmitterID = DVO
#define DISPPLL_CONFIG_DVO_DDR_SPEED               0x0000     // need only when ucTransmitterID = DVO
#define DISPPLL_CONFIG_DVO_SDR_SPEED               0x0001     // need only when ucTransmitterID = DVO
#define DISPPLL_CONFIG_DVO_OUTPUT_SEL              0x000c     // need only when ucTransmitterID = DVO
#define DISPPLL_CONFIG_DVO_LOW12BIT                0x0000     // need only when ucTransmitterID = DVO
#define DISPPLL_CONFIG_DVO_UPPER12BIT              0x0004     // need only when ucTransmitterID = DVO
#define DISPPLL_CONFIG_DVO_24BIT                   0x0008     // need only when ucTransmitterID = DVO
#define DISPPLL_CONFIG_SS_ENABLE                   0x0010     // Only used when ucEncoderMode = DP or LVDS
#define DISPPLL_CONFIG_COHERENT_MODE               0x0020     // Only used when ucEncoderMode = TMDS or HDMI
#define DISPPLL_CONFIG_DUAL_LINK                   0x0040     // Only used when ucEncoderMode = TMDS or LVDS


typedef struct _ADJUST_DISPLAY_PLL_OUTPUT_PARAMETERS_V3
{
  ULONG ulDispPllFreq;                 // return display PPLL freq which is used to generate the pixclock, and related idclk, symclk etc
  UCHAR ucRefDiv;                      // if it is none-zero, it is used to be calculated the other ppll parameter fb_divider and post_div ( if it is not given )
  UCHAR ucPostDiv;                     // if it is none-zero, it is used to be calculated the other ppll parameter fb_divider
  UCHAR ucReserved[2];  
}ADJUST_DISPLAY_PLL_OUTPUT_PARAMETERS_V3;

typedef struct _ADJUST_DISPLAY_PLL_PS_ALLOCATION_V3
{
  union 
  {
    ADJUST_DISPLAY_PLL_INPUT_PARAMETERS_V3  sInput;
    ADJUST_DISPLAY_PLL_OUTPUT_PARAMETERS_V3 sOutput;
  };
} ADJUST_DISPLAY_PLL_PS_ALLOCATION_V3;

/****************************************************************************/	
// Structures used by EnableYUVTable
/****************************************************************************/	
typedef struct _ENABLE_YUV_PARAMETERS
{
  UCHAR ucEnable;                     // ATOM_ENABLE:Enable YUV or ATOM_DISABLE:Disable YUV (RGB)
  UCHAR ucCRTC;                       // Which CRTC needs this YUV or RGB format
  UCHAR ucPadding[2];
}ENABLE_YUV_PARAMETERS;
#define ENABLE_YUV_PS_ALLOCATION ENABLE_YUV_PARAMETERS

/****************************************************************************/	
// Structures used by GetMemoryClockTable
/****************************************************************************/	
typedef struct _GET_MEMORY_CLOCK_PARAMETERS
{
  ULONG ulReturnMemoryClock;          // current memory speed in 10KHz unit
} GET_MEMORY_CLOCK_PARAMETERS;
#define GET_MEMORY_CLOCK_PS_ALLOCATION  GET_MEMORY_CLOCK_PARAMETERS

/****************************************************************************/	
// Structures used by GetEngineClockTable
/****************************************************************************/	
typedef struct _GET_ENGINE_CLOCK_PARAMETERS
{
  ULONG ulReturnEngineClock;          // current engine speed in 10KHz unit
} GET_ENGINE_CLOCK_PARAMETERS;
#define GET_ENGINE_CLOCK_PS_ALLOCATION  GET_ENGINE_CLOCK_PARAMETERS

/****************************************************************************/	
// Following Structures and constant may be obsolete
/****************************************************************************/	
//Maxium 8 bytes,the data read in will be placed in the parameter space.
//Read operaion successeful when the paramter space is non-zero, otherwise read operation failed
typedef struct _READ_EDID_FROM_HW_I2C_DATA_PARAMETERS
{
  USHORT    usPrescale;         //Ratio between Engine clock and I2C clock
  USHORT    usVRAMAddress;      //Address in Frame Buffer where to pace raw EDID
  USHORT    usStatus;           //When use output: lower byte EDID checksum, high byte hardware status
                                //WHen use input:  lower byte as 'byte to read':currently limited to 128byte or 1byte
  UCHAR     ucSlaveAddr;        //Read from which slave
  UCHAR     ucLineNumber;       //Read from which HW assisted line
}READ_EDID_FROM_HW_I2C_DATA_PARAMETERS;
#define READ_EDID_FROM_HW_I2C_DATA_PS_ALLOCATION  READ_EDID_FROM_HW_I2C_DATA_PARAMETERS


#define  ATOM_WRITE_I2C_FORMAT_PSOFFSET_PSDATABYTE                  0
#define  ATOM_WRITE_I2C_FORMAT_PSOFFSET_PSTWODATABYTES              1
#define  ATOM_WRITE_I2C_FORMAT_PSCOUNTER_PSOFFSET_IDDATABLOCK       2
#define  ATOM_WRITE_I2C_FORMAT_PSCOUNTER_IDOFFSET_PLUS_IDDATABLOCK  3
#define  ATOM_WRITE_I2C_FORMAT_IDCOUNTER_IDOFFSET_IDDATABLOCK       4

typedef struct _WRITE_ONE_BYTE_HW_I2C_DATA_PARAMETERS
{
  USHORT    usPrescale;         //Ratio between Engine clock and I2C clock
  USHORT    usByteOffset;       //Write to which byte
                                //Upper portion of usByteOffset is Format of data 
                                //1bytePS+offsetPS
                                //2bytesPS+offsetPS
                                //blockID+offsetPS
                                //blockID+offsetID
                                //blockID+counterID+offsetID
  UCHAR     ucData;             //PS data1
  UCHAR     ucStatus;           //Status byte 1=success, 2=failure, Also is used as PS data2
  UCHAR     ucSlaveAddr;        //Write to which slave
  UCHAR     ucLineNumber;       //Write from which HW assisted line
}WRITE_ONE_BYTE_HW_I2C_DATA_PARAMETERS;

#define WRITE_ONE_BYTE_HW_I2C_DATA_PS_ALLOCATION  WRITE_ONE_BYTE_HW_I2C_DATA_PARAMETERS

typedef struct _SET_UP_HW_I2C_DATA_PARAMETERS
{
  USHORT    usPrescale;         //Ratio between Engine clock and I2C clock
  UCHAR     ucSlaveAddr;        //Write to which slave
  UCHAR     ucLineNumber;       //Write from which HW assisted line
}SET_UP_HW_I2C_DATA_PARAMETERS;


/**************************************************************************/
#define SPEED_FAN_CONTROL_PS_ALLOCATION   WRITE_ONE_BYTE_HW_I2C_DATA_PARAMETERS


/****************************************************************************/	
// Structures used by PowerConnectorDetectionTable
/****************************************************************************/	
typedef struct	_POWER_CONNECTOR_DETECTION_PARAMETERS
{
  UCHAR   ucPowerConnectorStatus;      //Used for return value 0: detected, 1:not detected
	UCHAR   ucPwrBehaviorId;							
	USHORT	usPwrBudget;								 //how much power currently boot to in unit of watt
}POWER_CONNECTOR_DETECTION_PARAMETERS;

typedef struct POWER_CONNECTOR_DETECTION_PS_ALLOCATION
{                               
  UCHAR   ucPowerConnectorStatus;      //Used for return value 0: detected, 1:not detected
	UCHAR   ucReserved;
	USHORT	usPwrBudget;								 //how much power currently boot to in unit of watt
  WRITE_ONE_BYTE_HW_I2C_DATA_PS_ALLOCATION    sReserved;
}POWER_CONNECTOR_DETECTION_PS_ALLOCATION;

/****************************LVDS SS Command Table Definitions**********************/

/****************************************************************************/	
// Structures used by EnableSpreadSpectrumOnPPLLTable
/****************************************************************************/	
typedef struct	_ENABLE_LVDS_SS_PARAMETERS
{
  USHORT  usSpreadSpectrumPercentage;       
  UCHAR   ucSpreadSpectrumType;           //Bit1=0 Down Spread,=1 Center Spread. Bit1=1 Ext. =0 Int. Others:TBD
  UCHAR   ucSpreadSpectrumStepSize_Delay; //bits3:2 SS_STEP_SIZE; bit 6:4 SS_DELAY
  UCHAR   ucEnable;                       //ATOM_ENABLE or ATOM_DISABLE
  UCHAR   ucPadding[3];
}ENABLE_LVDS_SS_PARAMETERS;

//ucTableFormatRevision=1,ucTableContentRevision=2
typedef struct	_ENABLE_LVDS_SS_PARAMETERS_V2
{
  USHORT  usSpreadSpectrumPercentage;       
  UCHAR   ucSpreadSpectrumType;           //Bit1=0 Down Spread,=1 Center Spread. Bit1=1 Ext. =0 Int. Others:TBD
  UCHAR   ucSpreadSpectrumStep;           //
  UCHAR   ucEnable;                       //ATOM_ENABLE or ATOM_DISABLE
  UCHAR   ucSpreadSpectrumDelay;
  UCHAR   ucSpreadSpectrumRange;
  UCHAR   ucPadding;
}ENABLE_LVDS_SS_PARAMETERS_V2;

//This new structure is based on ENABLE_LVDS_SS_PARAMETERS but expands to SS on PPLL, so other devices can use SS.
typedef struct	_ENABLE_SPREAD_SPECTRUM_ON_PPLL
{
  USHORT  usSpreadSpectrumPercentage;
  UCHAR   ucSpreadSpectrumType;           // Bit1=0 Down Spread,=1 Center Spread. Bit1=1 Ext. =0 Int. Others:TBD
  UCHAR   ucSpreadSpectrumStep;           //
  UCHAR   ucEnable;                       // ATOM_ENABLE or ATOM_DISABLE
  UCHAR   ucSpreadSpectrumDelay;
  UCHAR   ucSpreadSpectrumRange;
  UCHAR   ucPpll;												  // ATOM_PPLL1/ATOM_PPLL2
}ENABLE_SPREAD_SPECTRUM_ON_PPLL;

typedef struct _ENABLE_SPREAD_SPECTRUM_ON_PPLL_V2
{
  USHORT  usSpreadSpectrumPercentage;
  UCHAR   ucSpreadSpectrumType;	        // Bit[0]: 0-Down Spread,1-Center Spread. 
                                        // Bit[1]: 1-Ext. 0-Int. 
                                        // Bit[3:2]: =0 P1PLL =1 P2PLL =2 DCPLL
                                        // Bits[7:4] reserved
  UCHAR   ucEnable;	                    // ATOM_ENABLE or ATOM_DISABLE
  USHORT  usSpreadSpectrumAmount;      	// Includes SS_AMOUNT_FBDIV[7:0] and SS_AMOUNT_NFRAC_SLIP[11:8]    
  USHORT  usSpreadSpectrumStep;	        // SS_STEP_SIZE_DSFRAC
}ENABLE_SPREAD_SPECTRUM_ON_PPLL_V2;

#define ATOM_PPLL_SS_TYPE_V2_DOWN_SPREAD      0x00
#define ATOM_PPLL_SS_TYPE_V2_CENTRE_SPREAD    0x01
#define ATOM_PPLL_SS_TYPE_V2_EXT_SPREAD       0x02
#define ATOM_PPLL_SS_TYPE_V2_PPLL_SEL_MASK    0x0c
#define ATOM_PPLL_SS_TYPE_V2_P1PLL            0x00
#define ATOM_PPLL_SS_TYPE_V2_P2PLL            0x04
#define ATOM_PPLL_SS_TYPE_V2_DCPLL            0x08
#define ATOM_PPLL_SS_AMOUNT_V2_FBDIV_MASK     0x00FF
#define ATOM_PPLL_SS_AMOUNT_V2_FBDIV_SHIFT    0
#define ATOM_PPLL_SS_AMOUNT_V2_NFRAC_MASK     0x0F00
#define ATOM_PPLL_SS_AMOUNT_V2_NFRAC_SHIFT    8

// Used by DCE5.0
 typedef struct _ENABLE_SPREAD_SPECTRUM_ON_PPLL_V3
{
  USHORT  usSpreadSpectrumAmountFrac;   // SS_AMOUNT_DSFRAC New in DCE5.0
  UCHAR   ucSpreadSpectrumType;	        // Bit[0]: 0-Down Spread,1-Center Spread. 
                                        // Bit[1]: 1-Ext. 0-Int. 
                                        // Bit[3:2]: =0 P1PLL =1 P2PLL =2 DCPLL
                                        // Bits[7:4] reserved
  UCHAR   ucEnable;	                    // ATOM_ENABLE or ATOM_DISABLE
  USHORT  usSpreadSpectrumAmount;      	// Includes SS_AMOUNT_FBDIV[7:0] and SS_AMOUNT_NFRAC_SLIP[11:8]    
  USHORT  usSpreadSpectrumStep;	        // SS_STEP_SIZE_DSFRAC
}ENABLE_SPREAD_SPECTRUM_ON_PPLL_V3;
    
#define ATOM_PPLL_SS_TYPE_V3_DOWN_SPREAD      0x00
#define ATOM_PPLL_SS_TYPE_V3_CENTRE_SPREAD    0x01
#define ATOM_PPLL_SS_TYPE_V3_EXT_SPREAD       0x02
#define ATOM_PPLL_SS_TYPE_V3_PPLL_SEL_MASK    0x0c
#define ATOM_PPLL_SS_TYPE_V3_P1PLL            0x00
#define ATOM_PPLL_SS_TYPE_V3_P2PLL            0x04
#define ATOM_PPLL_SS_TYPE_V3_DCPLL            0x08
#define ATOM_PPLL_SS_TYPE_V3_P0PLL            ATOM_PPLL_SS_TYPE_V3_DCPLL
#define ATOM_PPLL_SS_AMOUNT_V3_FBDIV_MASK     0x00FF
#define ATOM_PPLL_SS_AMOUNT_V3_FBDIV_SHIFT    0
#define ATOM_PPLL_SS_AMOUNT_V3_NFRAC_MASK     0x0F00
#define ATOM_PPLL_SS_AMOUNT_V3_NFRAC_SHIFT    8

#define ENABLE_SPREAD_SPECTRUM_ON_PPLL_PS_ALLOCATION  ENABLE_SPREAD_SPECTRUM_ON_PPLL

/**************************************************************************/

typedef struct _SET_PIXEL_CLOCK_PS_ALLOCATION
{
  PIXEL_CLOCK_PARAMETERS sPCLKInput;
  ENABLE_SPREAD_SPECTRUM_ON_PPLL sReserved;//Caller doesn't need to init this portion 
}SET_PIXEL_CLOCK_PS_ALLOCATION;

#define ENABLE_VGA_RENDER_PS_ALLOCATION   SET_PIXEL_CLOCK_PS_ALLOCATION

/****************************************************************************/	
// Structures used by ###
/****************************************************************************/	
typedef struct	_MEMORY_TRAINING_PARAMETERS
{
  ULONG ulTargetMemoryClock;          //In 10Khz unit
}MEMORY_TRAINING_PARAMETERS;
#define MEMORY_TRAINING_PS_ALLOCATION MEMORY_TRAINING_PARAMETERS


/****************************LVDS and other encoder command table definitions **********************/


/****************************************************************************/	
// Structures used by LVDSEncoderControlTable   (Before DCE30)
//                    LVTMAEncoderControlTable  (Before DCE30)
//                    TMDSAEncoderControlTable  (Before DCE30)
/****************************************************************************/	
typedef struct _LVDS_ENCODER_CONTROL_PARAMETERS
{
  USHORT usPixelClock;  // in 10KHz; for bios convenient
  UCHAR  ucMisc;        // bit0=0: Enable single link
                        //     =1: Enable dual link
                        // Bit1=0: 666RGB
                        //     =1: 888RGB
  UCHAR  ucAction;      // 0: turn off encoder
                        // 1: setup and turn on encoder
}LVDS_ENCODER_CONTROL_PARAMETERS;

#define LVDS_ENCODER_CONTROL_PS_ALLOCATION  LVDS_ENCODER_CONTROL_PARAMETERS
   
#define TMDS1_ENCODER_CONTROL_PARAMETERS    LVDS_ENCODER_CONTROL_PARAMETERS
#define TMDS1_ENCODER_CONTROL_PS_ALLOCATION TMDS1_ENCODER_CONTROL_PARAMETERS

#define TMDS2_ENCODER_CONTROL_PARAMETERS    TMDS1_ENCODER_CONTROL_PARAMETERS
#define TMDS2_ENCODER_CONTROL_PS_ALLOCATION TMDS2_ENCODER_CONTROL_PARAMETERS


//ucTableFormatRevision=1,ucTableContentRevision=2
typedef struct _LVDS_ENCODER_CONTROL_PARAMETERS_V2
{
  USHORT usPixelClock;  // in 10KHz; for bios convenient
  UCHAR  ucMisc;        // see PANEL_ENCODER_MISC_xx defintions below
  UCHAR  ucAction;      // 0: turn off encoder
                        // 1: setup and turn on encoder
  UCHAR  ucTruncate;    // bit0=0: Disable truncate
                        //     =1: Enable truncate
                        // bit4=0: 666RGB
                        //     =1: 888RGB
  UCHAR  ucSpatial;     // bit0=0: Disable spatial dithering
                        //     =1: Enable spatial dithering
                        // bit4=0: 666RGB
                        //     =1: 888RGB
  UCHAR  ucTemporal;    // bit0=0: Disable temporal dithering
                        //     =1: Enable temporal dithering
                        // bit4=0: 666RGB
                        //     =1: 888RGB
                        // bit5=0: Gray level 2
                        //     =1: Gray level 4
  UCHAR  ucFRC;         // bit4=0: 25FRC_SEL pattern E
                        //     =1: 25FRC_SEL pattern F
                        // bit6:5=0: 50FRC_SEL pattern A
                        //       =1: 50FRC_SEL pattern B
                        //       =2: 50FRC_SEL pattern C
                        //       =3: 50FRC_SEL pattern D
                        // bit7=0: 75FRC_SEL pattern E
                        //     =1: 75FRC_SEL pattern F
}LVDS_ENCODER_CONTROL_PARAMETERS_V2;

#define LVDS_ENCODER_CONTROL_PS_ALLOCATION_V2  LVDS_ENCODER_CONTROL_PARAMETERS_V2
   
#define TMDS1_ENCODER_CONTROL_PARAMETERS_V2    LVDS_ENCODER_CONTROL_PARAMETERS_V2
#define TMDS1_ENCODER_CONTROL_PS_ALLOCATION_V2 TMDS1_ENCODER_CONTROL_PARAMETERS_V2
  
#define TMDS2_ENCODER_CONTROL_PARAMETERS_V2    TMDS1_ENCODER_CONTROL_PARAMETERS_V2
#define TMDS2_ENCODER_CONTROL_PS_ALLOCATION_V2 TMDS2_ENCODER_CONTROL_PARAMETERS_V2

#define LVDS_ENCODER_CONTROL_PARAMETERS_V3     LVDS_ENCODER_CONTROL_PARAMETERS_V2
#define LVDS_ENCODER_CONTROL_PS_ALLOCATION_V3  LVDS_ENCODER_CONTROL_PARAMETERS_V3

#define TMDS1_ENCODER_CONTROL_PARAMETERS_V3    LVDS_ENCODER_CONTROL_PARAMETERS_V3
#define TMDS1_ENCODER_CONTROL_PS_ALLOCATION_V3 TMDS1_ENCODER_CONTROL_PARAMETERS_V3

#define TMDS2_ENCODER_CONTROL_PARAMETERS_V3    LVDS_ENCODER_CONTROL_PARAMETERS_V3
#define TMDS2_ENCODER_CONTROL_PS_ALLOCATION_V3 TMDS2_ENCODER_CONTROL_PARAMETERS_V3

/****************************************************************************/	
// Structures used by ###
/****************************************************************************/	
typedef struct _ENABLE_EXTERNAL_TMDS_ENCODER_PARAMETERS
{                               
  UCHAR    ucEnable;            // Enable or Disable External TMDS encoder
  UCHAR    ucMisc;              // Bit0=0:Enable Single link;=1:Enable Dual link;Bit1 {=0:666RGB, =1:888RGB}
  UCHAR    ucPadding[2];
}ENABLE_EXTERNAL_TMDS_ENCODER_PARAMETERS;

typedef struct _ENABLE_EXTERNAL_TMDS_ENCODER_PS_ALLOCATION
{                               
  ENABLE_EXTERNAL_TMDS_ENCODER_PARAMETERS    sXTmdsEncoder;
  WRITE_ONE_BYTE_HW_I2C_DATA_PS_ALLOCATION   sReserved;     //Caller doesn't need to init this portion
}ENABLE_EXTERNAL_TMDS_ENCODER_PS_ALLOCATION;

#define ENABLE_EXTERNAL_TMDS_ENCODER_PARAMETERS_V2  LVDS_ENCODER_CONTROL_PARAMETERS_V2

typedef struct _ENABLE_EXTERNAL_TMDS_ENCODER_PS_ALLOCATION_V2
{                               
  ENABLE_EXTERNAL_TMDS_ENCODER_PARAMETERS_V2    sXTmdsEncoder;
  WRITE_ONE_BYTE_HW_I2C_DATA_PS_ALLOCATION      sReserved;     //Caller doesn't need to init this portion
}ENABLE_EXTERNAL_TMDS_ENCODER_PS_ALLOCATION_V2;

typedef struct _EXTERNAL_ENCODER_CONTROL_PS_ALLOCATION
{
  DIG_ENCODER_CONTROL_PARAMETERS            sDigEncoder;
  WRITE_ONE_BYTE_HW_I2C_DATA_PS_ALLOCATION sReserved;
}EXTERNAL_ENCODER_CONTROL_PS_ALLOCATION;

/****************************************************************************/	
// Structures used by DVOEncoderControlTable
/****************************************************************************/	
//ucTableFormatRevision=1,ucTableContentRevision=3

//ucDVOConfig:
#define DVO_ENCODER_CONFIG_RATE_SEL							0x01
#define DVO_ENCODER_CONFIG_DDR_SPEED						0x00
#define DVO_ENCODER_CONFIG_SDR_SPEED						0x01
#define DVO_ENCODER_CONFIG_OUTPUT_SEL						0x0c
#define DVO_ENCODER_CONFIG_LOW12BIT							0x00
#define DVO_ENCODER_CONFIG_UPPER12BIT						0x04
#define DVO_ENCODER_CONFIG_24BIT								0x08

typedef struct _DVO_ENCODER_CONTROL_PARAMETERS_V3
{
  USHORT usPixelClock; 
  UCHAR  ucDVOConfig;
  UCHAR  ucAction;														//ATOM_ENABLE/ATOM_DISABLE/ATOM_HPD_INIT
  UCHAR  ucReseved[4];
}DVO_ENCODER_CONTROL_PARAMETERS_V3;
#define DVO_ENCODER_CONTROL_PS_ALLOCATION_V3	DVO_ENCODER_CONTROL_PARAMETERS_V3

typedef struct _DVO_ENCODER_CONTROL_PARAMETERS_V1_4
{
  USHORT usPixelClock; 
  UCHAR  ucDVOConfig;
  UCHAR  ucAction;														//ATOM_ENABLE/ATOM_DISABLE/ATOM_HPD_INIT
  UCHAR  ucBitPerColor;                       //please refer to definition of PANEL_xBIT_PER_COLOR
  UCHAR  ucReseved[3];
}DVO_ENCODER_CONTROL_PARAMETERS_V1_4;
#define DVO_ENCODER_CONTROL_PS_ALLOCATION_V1_4	DVO_ENCODER_CONTROL_PARAMETERS_V1_4


//ucTableFormatRevision=1
//ucTableContentRevision=3 structure is not changed but usMisc add bit 1 as another input for 
// bit1=0: non-coherent mode
//     =1: coherent mode

//==========================================================================================
//Only change is here next time when changing encoder parameter definitions again!
#define LVDS_ENCODER_CONTROL_PARAMETERS_LAST     LVDS_ENCODER_CONTROL_PARAMETERS_V3
#define LVDS_ENCODER_CONTROL_PS_ALLOCATION_LAST  LVDS_ENCODER_CONTROL_PARAMETERS_LAST

#define TMDS1_ENCODER_CONTROL_PARAMETERS_LAST    LVDS_ENCODER_CONTROL_PARAMETERS_V3
#define TMDS1_ENCODER_CONTROL_PS_ALLOCATION_LAST TMDS1_ENCODER_CONTROL_PARAMETERS_LAST

#define TMDS2_ENCODER_CONTROL_PARAMETERS_LAST    LVDS_ENCODER_CONTROL_PARAMETERS_V3
#define TMDS2_ENCODER_CONTROL_PS_ALLOCATION_LAST TMDS2_ENCODER_CONTROL_PARAMETERS_LAST

#define DVO_ENCODER_CONTROL_PARAMETERS_LAST      DVO_ENCODER_CONTROL_PARAMETERS
#define DVO_ENCODER_CONTROL_PS_ALLOCATION_LAST   DVO_ENCODER_CONTROL_PS_ALLOCATION

//==========================================================================================
#define PANEL_ENCODER_MISC_DUAL                0x01
#define PANEL_ENCODER_MISC_COHERENT            0x02
#define	PANEL_ENCODER_MISC_TMDS_LINKB					 0x04
#define	PANEL_ENCODER_MISC_HDMI_TYPE					 0x08

#define PANEL_ENCODER_ACTION_DISABLE           ATOM_DISABLE
#define PANEL_ENCODER_ACTION_ENABLE            ATOM_ENABLE
#define PANEL_ENCODER_ACTION_COHERENTSEQ       (ATOM_ENABLE+1)

#define PANEL_ENCODER_TRUNCATE_EN              0x01
#define PANEL_ENCODER_TRUNCATE_DEPTH           0x10
#define PANEL_ENCODER_SPATIAL_DITHER_EN        0x01
#define PANEL_ENCODER_SPATIAL_DITHER_DEPTH     0x10
#define PANEL_ENCODER_TEMPORAL_DITHER_EN       0x01
#define PANEL_ENCODER_TEMPORAL_DITHER_DEPTH    0x10
#define PANEL_ENCODER_TEMPORAL_LEVEL_4         0x20
#define PANEL_ENCODER_25FRC_MASK               0x10
#define PANEL_ENCODER_25FRC_E                  0x00
#define PANEL_ENCODER_25FRC_F                  0x10
#define PANEL_ENCODER_50FRC_MASK               0x60
#define PANEL_ENCODER_50FRC_A                  0x00
#define PANEL_ENCODER_50FRC_B                  0x20
#define PANEL_ENCODER_50FRC_C                  0x40
#define PANEL_ENCODER_50FRC_D                  0x60
#define PANEL_ENCODER_75FRC_MASK               0x80
#define PANEL_ENCODER_75FRC_E                  0x00
#define PANEL_ENCODER_75FRC_F                  0x80

/****************************************************************************/	
// Structures used by SetVoltageTable
/****************************************************************************/	
#define SET_VOLTAGE_TYPE_ASIC_VDDC             1
#define SET_VOLTAGE_TYPE_ASIC_MVDDC            2
#define SET_VOLTAGE_TYPE_ASIC_MVDDQ            3
#define SET_VOLTAGE_TYPE_ASIC_VDDCI            4
#define SET_VOLTAGE_INIT_MODE                  5
#define SET_VOLTAGE_GET_MAX_VOLTAGE            6					//Gets the Max. voltage for the soldered Asic

#define SET_ASIC_VOLTAGE_MODE_ALL_SOURCE       0x1
#define SET_ASIC_VOLTAGE_MODE_SOURCE_A         0x2
#define SET_ASIC_VOLTAGE_MODE_SOURCE_B         0x4

#define	SET_ASIC_VOLTAGE_MODE_SET_VOLTAGE      0x0
#define	SET_ASIC_VOLTAGE_MODE_GET_GPIOVAL      0x1
#define	SET_ASIC_VOLTAGE_MODE_GET_GPIOMASK     0x2

typedef struct	_SET_VOLTAGE_PARAMETERS
{
  UCHAR    ucVoltageType;               // To tell which voltage to set up, VDDC/MVDDC/MVDDQ
  UCHAR    ucVoltageMode;               // To set all, to set source A or source B or ...
  UCHAR    ucVoltageIndex;              // An index to tell which voltage level
  UCHAR    ucReserved;          
}SET_VOLTAGE_PARAMETERS;

typedef struct	_SET_VOLTAGE_PARAMETERS_V2
{
  UCHAR    ucVoltageType;               // To tell which voltage to set up, VDDC/MVDDC/MVDDQ
  UCHAR    ucVoltageMode;               // Not used, maybe use for state machine for differen power mode
  USHORT   usVoltageLevel;              // real voltage level
}SET_VOLTAGE_PARAMETERS_V2;

// used by both SetVoltageTable v1.3 and v1.4
typedef struct	_SET_VOLTAGE_PARAMETERS_V1_3
{
  UCHAR    ucVoltageType;               // To tell which voltage to set up, VDDC/MVDDC/MVDDQ/VDDCI
  UCHAR    ucVoltageMode;               // Indicate action: Set voltage level
  USHORT   usVoltageLevel;              // real voltage level in unit of mv or Voltage Phase (0, 1, 2, .. )
}SET_VOLTAGE_PARAMETERS_V1_3;

//ucVoltageType
#define VOLTAGE_TYPE_VDDC                    1
#define VOLTAGE_TYPE_MVDDC                   2
#define VOLTAGE_TYPE_MVDDQ                   3
#define VOLTAGE_TYPE_VDDCI                   4

//SET_VOLTAGE_PARAMETERS_V3.ucVoltageMode
#define ATOM_SET_VOLTAGE                     0        //Set voltage Level
#define ATOM_INIT_VOLTAGE_REGULATOR          3        //Init Regulator
#define ATOM_SET_VOLTAGE_PHASE               4        //Set Vregulator Phase, only for SVID/PVID regulator
#define ATOM_GET_MAX_VOLTAGE                 6        //Get Max Voltage, not used from SetVoltageTable v1.3
#define ATOM_GET_VOLTAGE_LEVEL               6        //Get Voltage level from vitual voltage ID, not used for SetVoltage v1.4
#define ATOM_GET_LEAKAGE_ID                  8        //Get Leakage Voltage Id ( starting from SMU7x IP ), SetVoltage v1.4 

// define vitual voltage id in usVoltageLevel
#define ATOM_VIRTUAL_VOLTAGE_ID0             0xff01
#define ATOM_VIRTUAL_VOLTAGE_ID1             0xff02
#define ATOM_VIRTUAL_VOLTAGE_ID2             0xff03
#define ATOM_VIRTUAL_VOLTAGE_ID3             0xff04
#define ATOM_VIRTUAL_VOLTAGE_ID4             0xff05
#define ATOM_VIRTUAL_VOLTAGE_ID5             0xff06
#define ATOM_VIRTUAL_VOLTAGE_ID6             0xff07
#define ATOM_VIRTUAL_VOLTAGE_ID7             0xff08

typedef struct _SET_VOLTAGE_PS_ALLOCATION
{
  SET_VOLTAGE_PARAMETERS sASICSetVoltage;
  WRITE_ONE_BYTE_HW_I2C_DATA_PS_ALLOCATION sReserved;
}SET_VOLTAGE_PS_ALLOCATION;

// New Added from SI for GetVoltageInfoTable, input parameter structure
typedef struct  _GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_1
{
  UCHAR    ucVoltageType;               // Input: To tell which voltage to set up, VDDC/MVDDC/MVDDQ/VDDCI
  UCHAR    ucVoltageMode;               // Input: Indicate action: Get voltage info
  USHORT   usVoltageLevel;              // Input: real voltage level in unit of mv or Voltage Phase (0, 1, 2, .. ) or Leakage Id 
  ULONG    ulReserved;
}GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_1;

// New Added from SI for GetVoltageInfoTable, output parameter structure when ucVotlageMode == ATOM_GET_VOLTAGE_VID
typedef struct  _GET_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_1
{
  ULONG    ulVotlageGpioState;
  ULONG    ulVoltageGPioMask;
}GET_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_1;

// New Added from SI for GetVoltageInfoTable, output parameter structure when ucVotlageMode == ATOM_GET_VOLTAGE_STATEx_LEAKAGE_VID
typedef struct  _GET_LEAKAGE_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_1
{
  USHORT   usVoltageLevel;
  USHORT   usVoltageId;                                  // Voltage Id programmed in Voltage Regulator
  ULONG    ulReseved;
}GET_LEAKAGE_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_1;


// GetVoltageInfo v1.1 ucVoltageMode
#define	ATOM_GET_VOLTAGE_VID                0x00
#define ATOM_GET_VOTLAGE_INIT_SEQ           0x03
#define ATOM_GET_VOLTTAGE_PHASE_PHASE_VID   0x04
#define ATOM_GET_VOLTAGE_SVID2              0x07        //Get SVI2 Regulator Info

// for SI, this state map to 0xff02 voltage state in Power Play table, which is power boost state
#define ATOM_GET_VOLTAGE_STATE0_LEAKAGE_VID 0x10
// for SI, this state map to 0xff01 voltage state in Power Play table, which is performance state
#define	ATOM_GET_VOLTAGE_STATE1_LEAKAGE_VID 0x11

#define	ATOM_GET_VOLTAGE_STATE2_LEAKAGE_VID 0x12
#define	ATOM_GET_VOLTAGE_STATE3_LEAKAGE_VID 0x13

// New Added from CI Hawaii for GetVoltageInfoTable, input parameter structure
typedef struct  _GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_2
{
  UCHAR    ucVoltageType;               // Input: To tell which voltage to set up, VDDC/MVDDC/MVDDQ/VDDCI
  UCHAR    ucVoltageMode;               // Input: Indicate action: Get voltage info
  USHORT   usVoltageLevel;              // Input: real voltage level in unit of mv or Voltage Phase (0, 1, 2, .. ) or Leakage Id 
  ULONG    ulSCLKFreq;                  // Input: when ucVoltageMode= ATOM_GET_VOLTAGE_EVV_VOLTAGE, DPM state SCLK frequency, Define in PPTable SCLK/Voltage dependence table
}GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_2;

// New in GetVoltageInfo v1.2 ucVoltageMode
#define ATOM_GET_VOLTAGE_EVV_VOLTAGE        0x09        

// New Added from CI Hawaii for EVV feature 
typedef struct  _GET_EVV_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_2
{
  USHORT   usVoltageLevel;                               // real voltage level in unit of mv
  USHORT   usVoltageId;                                  // Voltage Id programmed in Voltage Regulator
  ULONG    ulReseved;
}GET_EVV_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_2;

/****************************************************************************/	
// Structures used by TVEncoderControlTable
/****************************************************************************/	
typedef struct _TV_ENCODER_CONTROL_PARAMETERS
{
  USHORT usPixelClock;                // in 10KHz; for bios convenient
  UCHAR  ucTvStandard;                // See definition "ATOM_TV_NTSC ..."
  UCHAR  ucAction;                    // 0: turn off encoder
                                      // 1: setup and turn on encoder
}TV_ENCODER_CONTROL_PARAMETERS;

typedef struct _TV_ENCODER_CONTROL_PS_ALLOCATION
{
  TV_ENCODER_CONTROL_PARAMETERS sTVEncoder;          
  WRITE_ONE_BYTE_HW_I2C_DATA_PS_ALLOCATION    sReserved; // Don't set this one
}TV_ENCODER_CONTROL_PS_ALLOCATION;

//==============================Data Table Portion====================================

/****************************************************************************/	
// Structure used in Data.mtb
/****************************************************************************/	
typedef struct _ATOM_MASTER_LIST_OF_DATA_TABLES
{
  USHORT        UtilityPipeLine;	        // Offest for the utility to get parser info,Don't change this position!
  USHORT        MultimediaCapabilityInfo; // Only used by MM Lib,latest version 1.1, not configuable from Bios, need to include the table to build Bios 
  USHORT        MultimediaConfigInfo;     // Only used by MM Lib,latest version 2.1, not configuable from Bios, need to include the table to build Bios
  USHORT        StandardVESA_Timing;      // Only used by Bios
  USHORT        FirmwareInfo;             // Shared by various SW components,latest version 1.4
  USHORT        PaletteData;              // Only used by BIOS
  USHORT        LCD_Info;                 // Shared by various SW components,latest version 1.3, was called LVDS_Info 
  USHORT        DIGTransmitterInfo;       // Internal used by VBIOS only version 3.1
  USHORT        AnalogTV_Info;            // Shared by various SW components,latest version 1.1 
  USHORT        SupportedDevicesInfo;     // Will be obsolete from R600
  USHORT        GPIO_I2C_Info;            // Shared by various SW components,latest version 1.2 will be used from R600           
  USHORT        VRAM_UsageByFirmware;     // Shared by various SW components,latest version 1.3 will be used from R600
  USHORT        GPIO_Pin_LUT;             // Shared by various SW components,latest version 1.1
  USHORT        VESA_ToInternalModeLUT;   // Only used by Bios
  USHORT        ComponentVideoInfo;       // Shared by various SW components,latest version 2.1 will be used from R600
  USHORT        PowerPlayInfo;            // Shared by various SW components,latest version 2.1,new design from R600
  USHORT        CompassionateData;        // Will be obsolete from R600
  USHORT        SaveRestoreInfo;          // Only used by Bios
  USHORT        PPLL_SS_Info;             // Shared by various SW components,latest version 1.2, used to call SS_Info, change to new name because of int ASIC SS info
  USHORT        OemInfo;                  // Defined and used by external SW, should be obsolete soon
  USHORT        XTMDS_Info;               // Will be obsolete from R600
  USHORT        MclkSS_Info;              // Shared by various SW components,latest version 1.1, only enabled when ext SS chip is used
  USHORT        Object_Header;            // Shared by various SW components,latest version 1.1
  USHORT        IndirectIOAccess;         // Only used by Bios,this table position can't change at all!!
  USHORT        MC_InitParameter;         // Only used by command table
  USHORT        ASIC_VDDC_Info;						// Will be obsolete from R600
  USHORT        ASIC_InternalSS_Info;			// New tabel name from R600, used to be called "ASIC_MVDDC_Info"
  USHORT        TV_VideoMode;							// Only used by command table
  USHORT        VRAM_Info;								// Only used by command table, latest version 1.3
  USHORT        MemoryTrainingInfo;				// Used for VBIOS and Diag utility for memory training purpose since R600. the new table rev start from 2.1
  USHORT        IntegratedSystemInfo;			// Shared by various SW components
  USHORT        ASIC_ProfilingInfo;				// New table name from R600, used to be called "ASIC_VDDCI_Info" for pre-R600
  USHORT        VoltageObjectInfo;				// Shared by various SW components, latest version 1.1
	USHORT				PowerSourceInfo;					// Shared by various SW components, latest versoin 1.1
}ATOM_MASTER_LIST_OF_DATA_TABLES;

typedef struct _ATOM_MASTER_DATA_TABLE
{ 
  ATOM_COMMON_TABLE_HEADER sHeader;  
  ATOM_MASTER_LIST_OF_DATA_TABLES   ListOfDataTables;
}ATOM_MASTER_DATA_TABLE;

// For backward compatible 
#define LVDS_Info                LCD_Info
#define DAC_Info                 PaletteData
#define TMDS_Info                DIGTransmitterInfo

/****************************************************************************/	
// Structure used in MultimediaCapabilityInfoTable
/****************************************************************************/	
typedef struct _ATOM_MULTIMEDIA_CAPABILITY_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  ULONG                    ulSignature;      // HW info table signature string "$ATI"
  UCHAR                    ucI2C_Type;       // I2C type (normal GP_IO, ImpactTV GP_IO, Dedicated I2C pin, etc)
  UCHAR                    ucTV_OutInfo;     // Type of TV out supported (3:0) and video out crystal frequency (6:4) and TV data port (7)
  UCHAR                    ucVideoPortInfo;  // Provides the video port capabilities
  UCHAR                    ucHostPortInfo;   // Provides host port configuration information
}ATOM_MULTIMEDIA_CAPABILITY_INFO;

/****************************************************************************/	
// Structure used in MultimediaConfigInfoTable
/****************************************************************************/	
typedef struct _ATOM_MULTIMEDIA_CONFIG_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;
  ULONG                    ulSignature;      // MM info table signature sting "$MMT"
  UCHAR                    ucTunerInfo;      // Type of tuner installed on the adapter (4:0) and video input for tuner (7:5)
  UCHAR                    ucAudioChipInfo;  // List the audio chip type (3:0) product type (4) and OEM revision (7:5)
  UCHAR                    ucProductID;      // Defines as OEM ID or ATI board ID dependent on product type setting
  UCHAR                    ucMiscInfo1;      // Tuner voltage (1:0) HW teletext support (3:2) FM audio decoder (5:4) reserved (6) audio scrambling (7)
  UCHAR                    ucMiscInfo2;      // I2S input config (0) I2S output config (1) I2S Audio Chip (4:2) SPDIF Output Config (5) reserved (7:6)
  UCHAR                    ucMiscInfo3;      // Video Decoder Type (3:0) Video In Standard/Crystal (7:4)
  UCHAR                    ucMiscInfo4;      // Video Decoder Host Config (2:0) reserved (7:3)
  UCHAR                    ucVideoInput0Info;// Video Input 0 Type (1:0) F/B setting (2) physical connector ID (5:3) reserved (7:6)
  UCHAR                    ucVideoInput1Info;// Video Input 1 Type (1:0) F/B setting (2) physical connector ID (5:3) reserved (7:6)
  UCHAR                    ucVideoInput2Info;// Video Input 2 Type (1:0) F/B setting (2) physical connector ID (5:3) reserved (7:6)
  UCHAR                    ucVideoInput3Info;// Video Input 3 Type (1:0) F/B setting (2) physical connector ID (5:3) reserved (7:6)
  UCHAR                    ucVideoInput4Info;// Video Input 4 Type (1:0) F/B setting (2) physical connector ID (5:3) reserved (7:6)
}ATOM_MULTIMEDIA_CONFIG_INFO;


/****************************************************************************/	
// Structures used in FirmwareInfoTable
/****************************************************************************/	

// usBIOSCapability Definition:
// Bit 0 = 0: Bios image is not Posted, =1:Bios image is Posted; 
// Bit 1 = 0: Dual CRTC is not supported, =1: Dual CRTC is supported; 
// Bit 2 = 0: Extended Desktop is not supported, =1: Extended Desktop is supported; 
// Others: Reserved
#define ATOM_BIOS_INFO_ATOM_FIRMWARE_POSTED         0x0001
#define ATOM_BIOS_INFO_DUAL_CRTC_SUPPORT            0x0002
#define ATOM_BIOS_INFO_EXTENDED_DESKTOP_SUPPORT     0x0004
#define ATOM_BIOS_INFO_MEMORY_CLOCK_SS_SUPPORT      0x0008		// (valid from v1.1 ~v1.4):=1: memclk SS enable, =0 memclk SS disable. 
#define ATOM_BIOS_INFO_ENGINE_CLOCK_SS_SUPPORT      0x0010		// (valid from v1.1 ~v1.4):=1: engclk SS enable, =0 engclk SS disable. 
#define ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU         0x0020
#define ATOM_BIOS_INFO_WMI_SUPPORT                  0x0040
#define ATOM_BIOS_INFO_PPMODE_ASSIGNGED_BY_SYSTEM   0x0080
#define ATOM_BIOS_INFO_HYPERMEMORY_SUPPORT          0x0100
#define ATOM_BIOS_INFO_HYPERMEMORY_SIZE_MASK        0x1E00
#define ATOM_BIOS_INFO_VPOST_WITHOUT_FIRST_MODE_SET 0x2000
#define ATOM_BIOS_INFO_BIOS_SCRATCH6_SCL2_REDEFINE  0x4000
#define ATOM_BIOS_INFO_MEMORY_CLOCK_EXT_SS_SUPPORT  0x0008		// (valid from v2.1 ): =1: memclk ss enable with external ss chip
#define ATOM_BIOS_INFO_ENGINE_CLOCK_EXT_SS_SUPPORT  0x0010		// (valid from v2.1 ): =1: engclk ss enable with external ss chip

#ifndef _H2INC

//Please don't add or expand this bitfield structure below, this one will retire soon.!
typedef struct _ATOM_FIRMWARE_CAPABILITY
{
#if ATOM_BIG_ENDIAN
  USHORT Reserved:1;
  USHORT SCL2Redefined:1;
  USHORT PostWithoutModeSet:1;
  USHORT HyperMemory_Size:4;
  USHORT HyperMemory_Support:1;
  USHORT PPMode_Assigned:1;
  USHORT WMI_SUPPORT:1;
  USHORT GPUControlsBL:1;
  USHORT EngineClockSS_Support:1;
  USHORT MemoryClockSS_Support:1;
  USHORT ExtendedDesktopSupport:1;
  USHORT DualCRTC_Support:1;
  USHORT FirmwarePosted:1;
#else
  USHORT FirmwarePosted:1;
  USHORT DualCRTC_Support:1;
  USHORT ExtendedDesktopSupport:1;
  USHORT MemoryClockSS_Support:1;
  USHORT EngineClockSS_Support:1;
  USHORT GPUControlsBL:1;
  USHORT WMI_SUPPORT:1;
  USHORT PPMode_Assigned:1;
  USHORT HyperMemory_Support:1;
  USHORT HyperMemory_Size:4;
  USHORT PostWithoutModeSet:1;
  USHORT SCL2Redefined:1;
  USHORT Reserved:1;
#endif
}ATOM_FIRMWARE_CAPABILITY;

typedef union _ATOM_FIRMWARE_CAPABILITY_ACCESS
{
  ATOM_FIRMWARE_CAPABILITY sbfAccess;
  USHORT                   susAccess;
}ATOM_FIRMWARE_CAPABILITY_ACCESS;

#else

typedef union _ATOM_FIRMWARE_CAPABILITY_ACCESS
{
  USHORT                   susAccess;
}ATOM_FIRMWARE_CAPABILITY_ACCESS;

#endif

typedef struct _ATOM_FIRMWARE_INFO
{
  ATOM_COMMON_TABLE_HEADER        sHeader; 
  ULONG                           ulFirmwareRevision;
  ULONG                           ulDefaultEngineClock;       //In 10Khz unit
  ULONG                           ulDefaultMemoryClock;       //In 10Khz unit
  ULONG                           ulDriverTargetEngineClock;  //In 10Khz unit
  ULONG                           ulDriverTargetMemoryClock;  //In 10Khz unit
  ULONG                           ulMaxEngineClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxMemoryClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxPixelClockPLL_Output;  //In 10Khz unit
  ULONG                           ulASICMaxEngineClock;       //In 10Khz unit
  ULONG                           ulASICMaxMemoryClock;       //In 10Khz unit
  UCHAR                           ucASICMaxTemperature;
  UCHAR                           ucPadding[3];               //Don't use them
  ULONG                           aulReservedForBIOS[3];      //Don't use them
  USHORT                          usMinEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Output; //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Output; //In 10Khz unit
  USHORT                          usMaxPixelClock;            //In 10Khz unit, Max.  Pclk
  USHORT                          usMinPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMaxPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMinPixelClockPLL_Output;  //In 10Khz unit, the definitions above can't change!!!
  ATOM_FIRMWARE_CAPABILITY_ACCESS usFirmwareCapability;
  USHORT                          usReferenceClock;           //In 10Khz unit	
  USHORT                          usPM_RTS_Location;          //RTS PM4 starting location in ROM in 1Kb unit 
  UCHAR                           ucPM_RTS_StreamSize;        //RTS PM4 packets in Kb unit
  UCHAR                           ucDesign_ID;                //Indicate what is the board design
  UCHAR                           ucMemoryModule_ID;          //Indicate what is the board design
}ATOM_FIRMWARE_INFO;

typedef struct _ATOM_FIRMWARE_INFO_V1_2
{
  ATOM_COMMON_TABLE_HEADER        sHeader; 
  ULONG                           ulFirmwareRevision;
  ULONG                           ulDefaultEngineClock;       //In 10Khz unit
  ULONG                           ulDefaultMemoryClock;       //In 10Khz unit
  ULONG                           ulDriverTargetEngineClock;  //In 10Khz unit
  ULONG                           ulDriverTargetMemoryClock;  //In 10Khz unit
  ULONG                           ulMaxEngineClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxMemoryClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxPixelClockPLL_Output;  //In 10Khz unit
  ULONG                           ulASICMaxEngineClock;       //In 10Khz unit
  ULONG                           ulASICMaxMemoryClock;       //In 10Khz unit
  UCHAR                           ucASICMaxTemperature;
  UCHAR                           ucMinAllowedBL_Level;
  UCHAR                           ucPadding[2];               //Don't use them
  ULONG                           aulReservedForBIOS[2];      //Don't use them
  ULONG                           ulMinPixelClockPLL_Output;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Output; //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Output; //In 10Khz unit
  USHORT                          usMaxPixelClock;            //In 10Khz unit, Max.  Pclk
  USHORT                          usMinPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMaxPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMinPixelClockPLL_Output;  //In 10Khz unit - lower 16bit of ulMinPixelClockPLL_Output
  ATOM_FIRMWARE_CAPABILITY_ACCESS usFirmwareCapability;
  USHORT                          usReferenceClock;           //In 10Khz unit	
  USHORT                          usPM_RTS_Location;          //RTS PM4 starting location in ROM in 1Kb unit 
  UCHAR                           ucPM_RTS_StreamSize;        //RTS PM4 packets in Kb unit
  UCHAR                           ucDesign_ID;                //Indicate what is the board design
  UCHAR                           ucMemoryModule_ID;          //Indicate what is the board design
}ATOM_FIRMWARE_INFO_V1_2;

typedef struct _ATOM_FIRMWARE_INFO_V1_3
{
  ATOM_COMMON_TABLE_HEADER        sHeader; 
  ULONG                           ulFirmwareRevision;
  ULONG                           ulDefaultEngineClock;       //In 10Khz unit
  ULONG                           ulDefaultMemoryClock;       //In 10Khz unit
  ULONG                           ulDriverTargetEngineClock;  //In 10Khz unit
  ULONG                           ulDriverTargetMemoryClock;  //In 10Khz unit
  ULONG                           ulMaxEngineClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxMemoryClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxPixelClockPLL_Output;  //In 10Khz unit
  ULONG                           ulASICMaxEngineClock;       //In 10Khz unit
  ULONG                           ulASICMaxMemoryClock;       //In 10Khz unit
  UCHAR                           ucASICMaxTemperature;
  UCHAR                           ucMinAllowedBL_Level;
  UCHAR                           ucPadding[2];               //Don't use them
  ULONG                           aulReservedForBIOS;         //Don't use them
  ULONG                           ul3DAccelerationEngineClock;//In 10Khz unit
  ULONG                           ulMinPixelClockPLL_Output;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Output; //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Output; //In 10Khz unit
  USHORT                          usMaxPixelClock;            //In 10Khz unit, Max.  Pclk
  USHORT                          usMinPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMaxPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMinPixelClockPLL_Output;  //In 10Khz unit - lower 16bit of ulMinPixelClockPLL_Output
  ATOM_FIRMWARE_CAPABILITY_ACCESS usFirmwareCapability;
  USHORT                          usReferenceClock;           //In 10Khz unit	
  USHORT                          usPM_RTS_Location;          //RTS PM4 starting location in ROM in 1Kb unit 
  UCHAR                           ucPM_RTS_StreamSize;        //RTS PM4 packets in Kb unit
  UCHAR                           ucDesign_ID;                //Indicate what is the board design
  UCHAR                           ucMemoryModule_ID;          //Indicate what is the board design
}ATOM_FIRMWARE_INFO_V1_3;

typedef struct _ATOM_FIRMWARE_INFO_V1_4
{
  ATOM_COMMON_TABLE_HEADER        sHeader; 
  ULONG                           ulFirmwareRevision;
  ULONG                           ulDefaultEngineClock;       //In 10Khz unit
  ULONG                           ulDefaultMemoryClock;       //In 10Khz unit
  ULONG                           ulDriverTargetEngineClock;  //In 10Khz unit
  ULONG                           ulDriverTargetMemoryClock;  //In 10Khz unit
  ULONG                           ulMaxEngineClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxMemoryClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxPixelClockPLL_Output;  //In 10Khz unit
  ULONG                           ulASICMaxEngineClock;       //In 10Khz unit
  ULONG                           ulASICMaxMemoryClock;       //In 10Khz unit
  UCHAR                           ucASICMaxTemperature;
  UCHAR                           ucMinAllowedBL_Level;
  USHORT                          usBootUpVDDCVoltage;        //In MV unit
  USHORT                          usLcdMinPixelClockPLL_Output; // In MHz unit
  USHORT                          usLcdMaxPixelClockPLL_Output; // In MHz unit
  ULONG                           ul3DAccelerationEngineClock;//In 10Khz unit
  ULONG                           ulMinPixelClockPLL_Output;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Output; //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Output; //In 10Khz unit
  USHORT                          usMaxPixelClock;            //In 10Khz unit, Max.  Pclk
  USHORT                          usMinPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMaxPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMinPixelClockPLL_Output;  //In 10Khz unit - lower 16bit of ulMinPixelClockPLL_Output
  ATOM_FIRMWARE_CAPABILITY_ACCESS usFirmwareCapability;
  USHORT                          usReferenceClock;           //In 10Khz unit	
  USHORT                          usPM_RTS_Location;          //RTS PM4 starting location in ROM in 1Kb unit 
  UCHAR                           ucPM_RTS_StreamSize;        //RTS PM4 packets in Kb unit
  UCHAR                           ucDesign_ID;                //Indicate what is the board design
  UCHAR                           ucMemoryModule_ID;          //Indicate what is the board design
}ATOM_FIRMWARE_INFO_V1_4;

//the structure below to be used from Cypress
typedef struct _ATOM_FIRMWARE_INFO_V2_1
{
  ATOM_COMMON_TABLE_HEADER        sHeader; 
  ULONG                           ulFirmwareRevision;
  ULONG                           ulDefaultEngineClock;       //In 10Khz unit
  ULONG                           ulDefaultMemoryClock;       //In 10Khz unit
  ULONG                           ulReserved1;
  ULONG                           ulReserved2;
  ULONG                           ulMaxEngineClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxMemoryClockPLL_Output; //In 10Khz unit
  ULONG                           ulMaxPixelClockPLL_Output;  //In 10Khz unit
  ULONG                           ulBinaryAlteredInfo;        //Was ulASICMaxEngineClock
  ULONG                           ulDefaultDispEngineClkFreq; //In 10Khz unit
  UCHAR                           ucReserved1;                //Was ucASICMaxTemperature;
  UCHAR                           ucMinAllowedBL_Level;
  USHORT                          usBootUpVDDCVoltage;        //In MV unit
  USHORT                          usLcdMinPixelClockPLL_Output; // In MHz unit
  USHORT                          usLcdMaxPixelClockPLL_Output; // In MHz unit
  ULONG                           ulReserved4;                //Was ulAsicMaximumVoltage
  ULONG                           ulMinPixelClockPLL_Output;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxEngineClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinEngineClockPLL_Output; //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMaxMemoryClockPLL_Input;  //In 10Khz unit
  USHORT                          usMinMemoryClockPLL_Output; //In 10Khz unit
  USHORT                          usMaxPixelClock;            //In 10Khz unit, Max.  Pclk
  USHORT                          usMinPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMaxPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMinPixelClockPLL_Output;  //In 10Khz unit - lower 16bit of ulMinPixelClockPLL_Output
  ATOM_FIRMWARE_CAPABILITY_ACCESS usFirmwareCapability;
  USHORT                          usCoreReferenceClock;       //In 10Khz unit	
  USHORT                          usMemoryReferenceClock;     //In 10Khz unit	
  USHORT                          usUniphyDPModeExtClkFreq;   //In 10Khz unit, if it is 0, In DP Mode Uniphy Input clock from internal PPLL, otherwise Input clock from external Spread clock
  UCHAR                           ucMemoryModule_ID;          //Indicate what is the board design
  UCHAR                           ucReserved4[3];
}ATOM_FIRMWARE_INFO_V2_1;

//the structure below to be used from NI
//ucTableFormatRevision=2
//ucTableContentRevision=2
typedef struct _ATOM_FIRMWARE_INFO_V2_2
{
  ATOM_COMMON_TABLE_HEADER        sHeader; 
  ULONG                           ulFirmwareRevision;
  ULONG                           ulDefaultEngineClock;       //In 10Khz unit
  ULONG                           ulDefaultMemoryClock;       //In 10Khz unit
  ULONG                           ulSPLL_OutputFreq;          //In 10Khz unit  
  ULONG                           ulGPUPLL_OutputFreq;        //In 10Khz unit
  ULONG                           ulReserved1;                //Was ulMaxEngineClockPLL_Output; //In 10Khz unit*
  ULONG                           ulReserved2;                //Was ulMaxMemoryClockPLL_Output; //In 10Khz unit*
  ULONG                           ulMaxPixelClockPLL_Output;  //In 10Khz unit
  ULONG                           ulBinaryAlteredInfo;        //Was ulASICMaxEngineClock  ?
  ULONG                           ulDefaultDispEngineClkFreq; //In 10Khz unit. This is the frequency before DCDTO, corresponding to usBootUpVDDCVoltage.          
  UCHAR                           ucReserved3;                //Was ucASICMaxTemperature;
  UCHAR                           ucMinAllowedBL_Level;
  USHORT                          usBootUpVDDCVoltage;        //In MV unit
  USHORT                          usLcdMinPixelClockPLL_Output; // In MHz unit
  USHORT                          usLcdMaxPixelClockPLL_Output; // In MHz unit
  ULONG                           ulReserved4;                //Was ulAsicMaximumVoltage
  ULONG                           ulMinPixelClockPLL_Output;  //In 10Khz unit
  UCHAR                           ucRemoteDisplayConfig;
  UCHAR                           ucReserved5[3];             //Was usMinEngineClockPLL_Input and usMaxEngineClockPLL_Input
  ULONG                           ulReserved6;                //Was usMinEngineClockPLL_Output and usMinMemoryClockPLL_Input
  ULONG                           ulReserved7;                //Was usMaxMemoryClockPLL_Input and usMinMemoryClockPLL_Output
  USHORT                          usReserved11;               //Was usMaxPixelClock;  //In 10Khz unit, Max.  Pclk used only for DAC
  USHORT                          usMinPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usMaxPixelClockPLL_Input;   //In 10Khz unit
  USHORT                          usBootUpVDDCIVoltage;       //In unit of mv; Was usMinPixelClockPLL_Output;
  ATOM_FIRMWARE_CAPABILITY_ACCESS usFirmwareCapability;
  USHORT                          usCoreReferenceClock;       //In 10Khz unit	
  USHORT                          usMemoryReferenceClock;     //In 10Khz unit	
  USHORT                          usUniphyDPModeExtClkFreq;   //In 10Khz unit, if it is 0, In DP Mode Uniphy Input clock from internal PPLL, otherwise Input clock from external Spread clock
  UCHAR                           ucMemoryModule_ID;          //Indicate what is the board design
  UCHAR                           ucReserved9[3];
  USHORT                          usBootUpMVDDCVoltage;       //In unit of mv; Was usMinPixelClockPLL_Output;
  USHORT                          usReserved12;
  ULONG                           ulReserved10[3];            // New added comparing to previous version
}ATOM_FIRMWARE_INFO_V2_2;

#define ATOM_FIRMWARE_INFO_LAST  ATOM_FIRMWARE_INFO_V2_2


// definition of ucRemoteDisplayConfig
#define REMOTE_DISPLAY_DISABLE                   0x00
#define REMOTE_DISPLAY_ENABLE                    0x01

/****************************************************************************/	
// Structures used in IntegratedSystemInfoTable
/****************************************************************************/	
#define IGP_CAP_FLAG_DYNAMIC_CLOCK_EN      0x2
#define IGP_CAP_FLAG_AC_CARD               0x4
#define IGP_CAP_FLAG_SDVO_CARD             0x8
#define IGP_CAP_FLAG_POSTDIV_BY_2_MODE     0x10

typedef struct _ATOM_INTEGRATED_SYSTEM_INFO
{
  ATOM_COMMON_TABLE_HEADER        sHeader; 
  ULONG	                          ulBootUpEngineClock;		    //in 10kHz unit
  ULONG	                          ulBootUpMemoryClock;		    //in 10kHz unit
  ULONG	                          ulMaxSystemMemoryClock;	    //in 10kHz unit
  ULONG	                          ulMinSystemMemoryClock;	    //in 10kHz unit
  UCHAR                           ucNumberOfCyclesInPeriodHi;
  UCHAR                           ucLCDTimingSel;             //=0:not valid.!=0 sel this timing descriptor from LCD EDID.
  USHORT                          usReserved1;
  USHORT                          usInterNBVoltageLow;        //An intermidiate PMW value to set the voltage 
  USHORT                          usInterNBVoltageHigh;       //Another intermidiate PMW value to set the voltage 
  ULONG	                          ulReserved[2];

  USHORT	                        usFSBClock;			            //In MHz unit
  USHORT                          usCapabilityFlag;		        //Bit0=1 indicates the fake HDMI support,Bit1=0/1 for Dynamic clocking dis/enable
																                              //Bit[3:2]== 0:No PCIE card, 1:AC card, 2:SDVO card
                                                              //Bit[4]==1: P/2 mode, ==0: P/1 mode
  USHORT	                        usPCIENBCfgReg7;				    //bit[7:0]=MUX_Sel, bit[9:8]=MUX_SEL_LEVEL2, bit[10]=Lane_Reversal
  USHORT	                        usK8MemoryClock;            //in MHz unit
  USHORT	                        usK8SyncStartDelay;         //in 0.01 us unit
  USHORT	                        usK8DataReturnTime;         //in 0.01 us unit
  UCHAR                           ucMaxNBVoltage;
  UCHAR                           ucMinNBVoltage;
  UCHAR                           ucMemoryType;					      //[7:4]=1:DDR1;=2:DDR2;=3:DDR3.[3:0] is reserved
  UCHAR                           ucNumberOfCyclesInPeriod;		//CG.FVTHROT_PWM_CTRL_REG0.NumberOfCyclesInPeriod 
  UCHAR                           ucStartingPWM_HighTime;     //CG.FVTHROT_PWM_CTRL_REG0.StartingPWM_HighTime
  UCHAR                           ucHTLinkWidth;              //16 bit vs. 8 bit
  UCHAR                           ucMaxNBVoltageHigh;    
  UCHAR                           ucMinNBVoltageHigh;
}ATOM_INTEGRATED_SYSTEM_INFO;

/* Explanation on entries in ATOM_INTEGRATED_SYSTEM_INFO
ulBootUpMemoryClock:    For Intel IGP,it's the UMA system memory clock 
                        For AMD IGP,it's 0 if no SidePort memory installed or it's the boot-up SidePort memory clock
ulMaxSystemMemoryClock: For Intel IGP,it's the Max freq from memory SPD if memory runs in ASYNC mode or otherwise (SYNC mode) it's 0
                        For AMD IGP,for now this can be 0
ulMinSystemMemoryClock: For Intel IGP,it's 133MHz if memory runs in ASYNC mode or otherwise (SYNC mode) it's 0 
                        For AMD IGP,for now this can be 0

usFSBClock:             For Intel IGP,it's FSB Freq 
                        For AMD IGP,it's HT Link Speed

usK8MemoryClock:        For AMD IGP only. For RevF CPU, set it to 200
usK8SyncStartDelay:     For AMD IGP only. Memory access latency in K8, required for watermark calculation
usK8DataReturnTime:     For AMD IGP only. Memory access latency in K8, required for watermark calculation

VC:Voltage Control
ucMaxNBVoltage:         Voltage regulator dependent PWM value. Low 8 bits of the value for the max voltage.Set this one to 0xFF if VC without PWM. Set this to 0x0 if no VC at all.
ucMinNBVoltage:         Voltage regulator dependent PWM value. Low 8 bits of the value for the min voltage.Set this one to 0x00 if VC without PWM or no VC at all.

ucNumberOfCyclesInPeriod:   Indicate how many cycles when PWM duty is 100%. low 8 bits of the value. 
ucNumberOfCyclesInPeriodHi: Indicate how many cycles when PWM duty is 100%. high 8 bits of the value.If the PWM has an inverter,set bit [7]==1,otherwise set it 0 

ucMaxNBVoltageHigh:     Voltage regulator dependent PWM value. High 8 bits of  the value for the max voltage.Set this one to 0xFF if VC without PWM. Set this to 0x0 if no VC at all.
ucMinNBVoltageHigh:     Voltage regulator dependent PWM value. High 8 bits of the value for the min voltage.Set this one to 0x00 if VC without PWM or no VC at all.


usInterNBVoltageLow:    Voltage regulator dependent PWM value. The value makes the voltage >=Min NB voltage but <=InterNBVoltageHigh. Set this to 0x0000 if VC without PWM or no VC at all.
usInterNBVoltageHigh:   Voltage regulator dependent PWM value. The value makes the voltage >=InterNBVoltageLow but <=Max NB voltage.Set this to 0x0000 if VC without PWM or no VC at all.
*/


/*
The following IGP table is introduced from RS780, which is supposed to be put by SBIOS in FB before IGP VBIOS starts VPOST;
Then VBIOS will copy the whole structure to its image so all GPU SW components can access this data structure to get whatever they need. 
The enough reservation should allow us to never change table revisions. Whenever needed, a GPU SW component can use reserved portion for new data entries.

SW components can access the IGP system infor structure in the same way as before
*/


typedef struct _ATOM_INTEGRATED_SYSTEM_INFO_V2
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  ULONG	                     ulBootUpEngineClock;       //in 10kHz unit
  ULONG			     ulReserved1[2];            //must be 0x0 for the reserved
  ULONG	                     ulBootUpUMAClock;          //in 10kHz unit
  ULONG	                     ulBootUpSidePortClock;     //in 10kHz unit
  ULONG	                     ulMinSidePortClock;        //in 10kHz unit
  ULONG			     ulReserved2[6];            //must be 0x0 for the reserved
  ULONG                      ulSystemConfig;            //see explanation below
  ULONG                      ulBootUpReqDisplayVector;
  ULONG                      ulOtherDisplayMisc;
  ULONG                      ulDDISlot1Config;
  ULONG                      ulDDISlot2Config;
  UCHAR                      ucMemoryType;              //[3:0]=1:DDR1;=2:DDR2;=3:DDR3.[7:4] is reserved
  UCHAR                      ucUMAChannelNumber;
  UCHAR                      ucDockingPinBit;
  UCHAR                      ucDockingPinPolarity;
  ULONG                      ulDockingPinCFGInfo;
  ULONG                      ulCPUCapInfo;
  USHORT                     usNumberOfCyclesInPeriod;
  USHORT                     usMaxNBVoltage;
  USHORT                     usMinNBVoltage;
  USHORT                     usBootUpNBVoltage;
  ULONG                      ulHTLinkFreq;              //in 10Khz
  USHORT                     usMinHTLinkWidth;
  USHORT                     usMaxHTLinkWidth;
  USHORT                     usUMASyncStartDelay;
  USHORT                     usUMADataReturnTime;
  USHORT                     usLinkStatusZeroTime;
  USHORT                     usDACEfuse;				//for storing badgap value (for RS880 only)
  ULONG                      ulHighVoltageHTLinkFreq;     // in 10Khz
  ULONG                      ulLowVoltageHTLinkFreq;      // in 10Khz
  USHORT                     usMaxUpStreamHTLinkWidth;
  USHORT                     usMaxDownStreamHTLinkWidth;
  USHORT                     usMinUpStreamHTLinkWidth;
  USHORT                     usMinDownStreamHTLinkWidth;
  USHORT                     usFirmwareVersion;         //0 means FW is not supported. Otherwise it's the FW version loaded by SBIOS and driver should enable FW.
  USHORT                     usFullT0Time;             // Input to calculate minimum HT link change time required by NB P-State. Unit is 0.01us.
  ULONG                      ulReserved3[96];          //must be 0x0
}ATOM_INTEGRATED_SYSTEM_INFO_V2;   

/*
ulBootUpEngineClock:   Boot-up Engine Clock in 10Khz;
ulBootUpUMAClock:      Boot-up UMA Clock in 10Khz; it must be 0x0 when UMA is not present
ulBootUpSidePortClock: Boot-up SidePort Clock in 10Khz; it must be 0x0 when SidePort Memory is not present,this could be equal to or less than maximum supported Sideport memory clock

ulSystemConfig:  
Bit[0]=1: PowerExpress mode =0 Non-PowerExpress mode; 
Bit[1]=1: system boots up at AMD overdrived state or user customized  mode. In this case, driver will just stick to this boot-up mode. No other PowerPlay state
      =0: system boots up at driver control state. Power state depends on PowerPlay table.
Bit[2]=1: PWM method is used on NB voltage control. =0: GPIO method is used.
Bit[3]=1: Only one power state(Performance) will be supported.
      =0: Multiple power states supported from PowerPlay table.
Bit[4]=1: CLMC is supported and enabled on current system. 
      =0: CLMC is not supported or enabled on current system. SBIOS need to support HT link/freq change through ATIF interface.  
Bit[5]=1: Enable CDLW for all driver control power states. Max HT width is from SBIOS, while Min HT width is determined by display requirement.  
      =0: CDLW is disabled. If CLMC is enabled case, Min HT width will be set equal to Max HT width. If CLMC disabled case, Max HT width will be applied.
Bit[6]=1: High Voltage requested for all power states. In this case, voltage will be forced at 1.1v and powerplay table voltage drop/throttling request will be ignored.
      =0: Voltage settings is determined by powerplay table.
Bit[7]=1: Enable CLMC as hybrid Mode. CDLD and CILR will be disabled in this case and we're using legacy C1E. This is workaround for CPU(Griffin) performance issue.
      =0: Enable CLMC as regular mode, CDLD and CILR will be enabled.
Bit[8]=1: CDLF is supported and enabled on current system.
      =0: CDLF is not supported or enabled on current system.
Bit[9]=1: DLL Shut Down feature is enabled on current system.
      =0: DLL Shut Down feature is not enabled or supported on current system.

ulBootUpReqDisplayVector: This dword is a bit vector indicates what display devices are requested during boot-up. Refer to ATOM_DEVICE_xxx_SUPPORT for the bit vector definitions.

ulOtherDisplayMisc: [15:8]- Bootup LCD Expansion selection; 0-center, 1-full panel size expansion;
			              [7:0] - BootupTV standard selection; This is a bit vector to indicate what TV standards are supported by the system. Refer to ucTVSupportedStd definition;

ulDDISlot1Config: Describes the PCIE lane configuration on this DDI PCIE slot (ADD2 card) or connector (Mobile design).
      [3:0]  - Bit vector to indicate PCIE lane config of the DDI slot/connector on chassis (bit 0=1 lane 3:0; bit 1=1 lane 7:4; bit 2=1 lane 11:8; bit 3=1 lane 15:12)
			[7:4]  - Bit vector to indicate PCIE lane config of the same DDI slot/connector on docking station (bit 4=1 lane 3:0; bit 5=1 lane 7:4; bit 6=1 lane 11:8; bit 7=1 lane 15:12)
      When a DDI connector is not "paired" (meaming two connections mutualexclusive on chassis or docking, only one of them can be connected at one time.
      in both chassis and docking, SBIOS has to duplicate the same PCIE lane info from chassis to docking or vice versa. For example:
      one DDI connector is only populated in docking with PCIE lane 8-11, but there is no paired connection on chassis, SBIOS has to copy bit 6 to bit 2.

			[15:8] - Lane configuration attribute; 
      [23:16]- Connector type, possible value:
               CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D
               CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D
               CONNECTOR_OBJECT_ID_HDMI_TYPE_A
               CONNECTOR_OBJECT_ID_DISPLAYPORT
               CONNECTOR_OBJECT_ID_eDP
			[31:24]- Reserved

ulDDISlot2Config: Same as Slot1.
ucMemoryType: SidePort memory type, set it to 0x0 when Sideport memory is not installed. Driver needs this info to change sideport memory clock. Not for display in CCC.
For IGP, Hypermemory is the only memory type showed in CCC.

ucUMAChannelNumber:  how many channels for the UMA;

ulDockingPinCFGInfo: [15:0]-Bus/Device/Function # to CFG to read this Docking Pin; [31:16]-reg offset in CFG to read this pin 
ucDockingPinBit:     which bit in this register to read the pin status;
ucDockingPinPolarity:Polarity of the pin when docked;

ulCPUCapInfo:        [7:0]=1:Griffin;[7:0]=2:Greyhound;[7:0]=3:K8, [7:0]=4:Pharaoh, other bits reserved for now and must be 0x0

usNumberOfCyclesInPeriod:Indicate how many cycles when PWM duty is 100%.

usMaxNBVoltage:Max. voltage control value in either PWM or GPIO mode. 
usMinNBVoltage:Min. voltage control value in either PWM or GPIO mode.
                    GPIO mode: both usMaxNBVoltage & usMinNBVoltage have a valid value ulSystemConfig.SYSTEM_CONFIG_USE_PWM_ON_VOLTAGE=0
                    PWM mode: both usMaxNBVoltage & usMinNBVoltage have a valid value ulSystemConfig.SYSTEM_CONFIG_USE_PWM_ON_VOLTAGE=1
                    GPU SW don't control mode: usMaxNBVoltage & usMinNBVoltage=0 and no care about ulSystemConfig.SYSTEM_CONFIG_USE_PWM_ON_VOLTAGE

usBootUpNBVoltage:Boot-up voltage regulator dependent PWM value.

ulHTLinkFreq:       Bootup HT link Frequency in 10Khz.
usMinHTLinkWidth:   Bootup minimum HT link width. If CDLW disabled, this is equal to usMaxHTLinkWidth. 
                    If CDLW enabled, both upstream and downstream width should be the same during bootup.
usMaxHTLinkWidth:   Bootup maximum HT link width. If CDLW disabled, this is equal to usMinHTLinkWidth. 
                    If CDLW enabled, both upstream and downstream width should be the same during bootup.  

usUMASyncStartDelay: Memory access latency, required for watermark calculation 
usUMADataReturnTime: Memory access latency, required for watermark calculation
usLinkStatusZeroTime:Memory access latency required for watermark calculation, set this to 0x0 for K8 CPU, set a proper value in 0.01 the unit of us 
for Griffin or Greyhound. SBIOS needs to convert to actual time by:
                     if T0Ttime [5:4]=00b, then usLinkStatusZeroTime=T0Ttime [3:0]*0.1us (0.0 to 1.5us)
                     if T0Ttime [5:4]=01b, then usLinkStatusZeroTime=T0Ttime [3:0]*0.5us (0.0 to 7.5us)
                     if T0Ttime [5:4]=10b, then usLinkStatusZeroTime=T0Ttime [3:0]*2.0us (0.0 to 30us)
                     if T0Ttime [5:4]=11b, and T0Ttime [3:0]=0x0 to 0xa, then usLinkStatusZeroTime=T0Ttime [3:0]*20us (0.0 to 200us)

ulHighVoltageHTLinkFreq:     HT link frequency for power state with low voltage. If boot up runs in HT1, this must be 0.
                             This must be less than or equal to ulHTLinkFreq(bootup frequency). 
ulLowVoltageHTLinkFreq:      HT link frequency for power state with low voltage or voltage scaling 1.0v~1.1v. If boot up runs in HT1, this must be 0.
                             This must be less than or equal to ulHighVoltageHTLinkFreq.

usMaxUpStreamHTLinkWidth:    Asymmetric link width support in the future, to replace usMaxHTLinkWidth. Not used for now.
usMaxDownStreamHTLinkWidth:  same as above.
usMinUpStreamHTLinkWidth:    Asymmetric link width support in the future, to replace usMinHTLinkWidth. Not used for now.
usMinDownStreamHTLinkWidth:  same as above.
*/

// ATOM_INTEGRATED_SYSTEM_INFO::ulCPUCapInfo  - CPU type definition 
#define    INTEGRATED_SYSTEM_INFO__UNKNOWN_CPU             0
#define    INTEGRATED_SYSTEM_INFO__AMD_CPU__GRIFFIN        1
#define    INTEGRATED_SYSTEM_INFO__AMD_CPU__GREYHOUND      2
#define    INTEGRATED_SYSTEM_INFO__AMD_CPU__K8             3
#define    INTEGRATED_SYSTEM_INFO__AMD_CPU__PHARAOH        4
#define    INTEGRATED_SYSTEM_INFO__AMD_CPU__OROCHI         5

#define    INTEGRATED_SYSTEM_INFO__AMD_CPU__MAX_CODE       INTEGRATED_SYSTEM_INFO__AMD_CPU__OROCHI    // this deff reflects max defined CPU code

#define SYSTEM_CONFIG_POWEREXPRESS_ENABLE                 0x00000001
#define SYSTEM_CONFIG_RUN_AT_OVERDRIVE_ENGINE             0x00000002
#define SYSTEM_CONFIG_USE_PWM_ON_VOLTAGE                  0x00000004 
#define SYSTEM_CONFIG_PERFORMANCE_POWERSTATE_ONLY         0x00000008
#define SYSTEM_CONFIG_CLMC_ENABLED                        0x00000010
#define SYSTEM_CONFIG_CDLW_ENABLED                        0x00000020
#define SYSTEM_CONFIG_HIGH_VOLTAGE_REQUESTED              0x00000040
#define SYSTEM_CONFIG_CLMC_HYBRID_MODE_ENABLED            0x00000080
#define SYSTEM_CONFIG_CDLF_ENABLED                        0x00000100
#define SYSTEM_CONFIG_DLL_SHUTDOWN_ENABLED                0x00000200

#define IGP_DDI_SLOT_LANE_CONFIG_MASK                     0x000000FF

#define b0IGP_DDI_SLOT_LANE_MAP_MASK                      0x0F
#define b0IGP_DDI_SLOT_DOCKING_LANE_MAP_MASK              0xF0
#define b0IGP_DDI_SLOT_CONFIG_LANE_0_3                    0x01
#define b0IGP_DDI_SLOT_CONFIG_LANE_4_7                    0x02
#define b0IGP_DDI_SLOT_CONFIG_LANE_8_11                   0x04
#define b0IGP_DDI_SLOT_CONFIG_LANE_12_15                  0x08

#define IGP_DDI_SLOT_ATTRIBUTE_MASK                       0x0000FF00
#define IGP_DDI_SLOT_CONFIG_REVERSED                      0x00000100
#define b1IGP_DDI_SLOT_CONFIG_REVERSED                    0x01

#define IGP_DDI_SLOT_CONNECTOR_TYPE_MASK                  0x00FF0000

// IntegratedSystemInfoTable new Rev is V5 after V2, because of the real rev of V2 is v1.4. This rev is used for RR
typedef struct _ATOM_INTEGRATED_SYSTEM_INFO_V5
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  ULONG	                     ulBootUpEngineClock;       //in 10kHz unit
  ULONG                      ulDentistVCOFreq;          //Dentist VCO clock in 10kHz unit, the source of GPU SCLK, LCLK, UCLK and VCLK. 
  ULONG                      ulLClockFreq;              //GPU Lclk freq in 10kHz unit, have relationship with NCLK in NorthBridge
  ULONG	                     ulBootUpUMAClock;          //in 10kHz unit
  ULONG                      ulReserved1[8];            //must be 0x0 for the reserved
  ULONG                      ulBootUpReqDisplayVector;
  ULONG                      ulOtherDisplayMisc;
  ULONG                      ulReserved2[4];            //must be 0x0 for the reserved
  ULONG                      ulSystemConfig;            //TBD
  ULONG                      ulCPUCapInfo;              //TBD
  USHORT                     usMaxNBVoltage;            //high NB voltage, calculated using current VDDNB (D24F2xDC) and VDDNB offset fuse;
  USHORT                     usMinNBVoltage;            //low NB voltage, calculated using current VDDNB (D24F2xDC) and VDDNB offset fuse;
  USHORT                     usBootUpNBVoltage;         //boot up NB voltage
  UCHAR                      ucHtcTmpLmt;               //bit [22:16] of D24F3x64 Hardware Thermal Control (HTC) Register, may not be needed, TBD
  UCHAR                      ucTjOffset;                //bit [28:22] of D24F3xE4 Thermtrip Status Register,may not be needed, TBD
  ULONG                      ulReserved3[4];            //must be 0x0 for the reserved
  ULONG                      ulDDISlot1Config;          //see above ulDDISlot1Config definition
  ULONG                      ulDDISlot2Config;
  ULONG                      ulDDISlot3Config;
  ULONG                      ulDDISlot4Config;
  ULONG                      ulReserved4[4];            //must be 0x0 for the reserved
  UCHAR                      ucMemoryType;              //[3:0]=1:DDR1;=2:DDR2;=3:DDR3.[7:4] is reserved
  UCHAR                      ucUMAChannelNumber;
  USHORT                     usReserved;
  ULONG                      ulReserved5[4];            //must be 0x0 for the reserved
  ULONG                      ulCSR_M3_ARB_CNTL_DEFAULT[10];//arrays with values for CSR M3 arbiter for default
  ULONG                      ulCSR_M3_ARB_CNTL_UVD[10]; //arrays with values for CSR M3 arbiter for UVD playback
  ULONG                      ulCSR_M3_ARB_CNTL_FS3D[10];//arrays with values for CSR M3 arbiter for Full Screen 3D applications
  ULONG                      ulReserved6[61];           //must be 0x0
}ATOM_INTEGRATED_SYSTEM_INFO_V5;   

#define ATOM_CRT_INT_ENCODER1_INDEX                       0x00000000
#define ATOM_LCD_INT_ENCODER1_INDEX                       0x00000001
#define ATOM_TV_INT_ENCODER1_INDEX                        0x00000002
#define ATOM_DFP_INT_ENCODER1_INDEX                       0x00000003
#define ATOM_CRT_INT_ENCODER2_INDEX                       0x00000004
#define ATOM_LCD_EXT_ENCODER1_INDEX                       0x00000005
#define ATOM_TV_EXT_ENCODER1_INDEX                        0x00000006
#define ATOM_DFP_EXT_ENCODER1_INDEX                       0x00000007
#define ATOM_CV_INT_ENCODER1_INDEX                        0x00000008
#define ATOM_DFP_INT_ENCODER2_INDEX                       0x00000009
#define ATOM_CRT_EXT_ENCODER1_INDEX                       0x0000000A
#define ATOM_CV_EXT_ENCODER1_INDEX                        0x0000000B
#define ATOM_DFP_INT_ENCODER3_INDEX                       0x0000000C
#define ATOM_DFP_INT_ENCODER4_INDEX                       0x0000000D

// define ASIC internal encoder id ( bit vector ), used for CRTC_SourceSelTable
#define ASIC_INT_DAC1_ENCODER_ID    											0x00 
#define ASIC_INT_TV_ENCODER_ID														0x02
#define ASIC_INT_DIG1_ENCODER_ID													0x03
#define ASIC_INT_DAC2_ENCODER_ID													0x04
#define ASIC_EXT_TV_ENCODER_ID														0x06
#define ASIC_INT_DVO_ENCODER_ID														0x07
#define ASIC_INT_DIG2_ENCODER_ID													0x09
#define ASIC_EXT_DIG_ENCODER_ID														0x05
#define ASIC_EXT_DIG2_ENCODER_ID													0x08
#define ASIC_INT_DIG3_ENCODER_ID													0x0a
#define ASIC_INT_DIG4_ENCODER_ID													0x0b
#define ASIC_INT_DIG5_ENCODER_ID													0x0c
#define ASIC_INT_DIG6_ENCODER_ID													0x0d
#define ASIC_INT_DIG7_ENCODER_ID													0x0e

//define Encoder attribute
#define ATOM_ANALOG_ENCODER																0
#define ATOM_DIGITAL_ENCODER															1		
#define ATOM_DP_ENCODER															      2		

#define ATOM_ENCODER_ENUM_MASK                            0x70
#define ATOM_ENCODER_ENUM_ID1                             0x00
#define ATOM_ENCODER_ENUM_ID2                             0x10
#define ATOM_ENCODER_ENUM_ID3                             0x20
#define ATOM_ENCODER_ENUM_ID4                             0x30
#define ATOM_ENCODER_ENUM_ID5                             0x40 
#define ATOM_ENCODER_ENUM_ID6                             0x50

#define ATOM_DEVICE_CRT1_INDEX                            0x00000000
#define ATOM_DEVICE_LCD1_INDEX                            0x00000001
#define ATOM_DEVICE_TV1_INDEX                             0x00000002
#define ATOM_DEVICE_DFP1_INDEX                            0x00000003
#define ATOM_DEVICE_CRT2_INDEX                            0x00000004
#define ATOM_DEVICE_LCD2_INDEX                            0x00000005
#define ATOM_DEVICE_DFP6_INDEX                            0x00000006
#define ATOM_DEVICE_DFP2_INDEX                            0x00000007
#define ATOM_DEVICE_CV_INDEX                              0x00000008
#define ATOM_DEVICE_DFP3_INDEX                            0x00000009
#define ATOM_DEVICE_DFP4_INDEX                            0x0000000A
#define ATOM_DEVICE_DFP5_INDEX                            0x0000000B

#define ATOM_DEVICE_RESERVEDC_INDEX                       0x0000000C
#define ATOM_DEVICE_RESERVEDD_INDEX                       0x0000000D
#define ATOM_DEVICE_RESERVEDE_INDEX                       0x0000000E
#define ATOM_DEVICE_RESERVEDF_INDEX                       0x0000000F
#define ATOM_MAX_SUPPORTED_DEVICE_INFO                    (ATOM_DEVICE_DFP3_INDEX+1)
#define ATOM_MAX_SUPPORTED_DEVICE_INFO_2                  ATOM_MAX_SUPPORTED_DEVICE_INFO
#define ATOM_MAX_SUPPORTED_DEVICE_INFO_3                  (ATOM_DEVICE_DFP5_INDEX + 1 )

#define ATOM_MAX_SUPPORTED_DEVICE                         (ATOM_DEVICE_RESERVEDF_INDEX+1)

#define ATOM_DEVICE_CRT1_SUPPORT                          (0x1L << ATOM_DEVICE_CRT1_INDEX )
#define ATOM_DEVICE_LCD1_SUPPORT                          (0x1L << ATOM_DEVICE_LCD1_INDEX )
#define ATOM_DEVICE_TV1_SUPPORT                           (0x1L << ATOM_DEVICE_TV1_INDEX  )
#define ATOM_DEVICE_DFP1_SUPPORT                          (0x1L << ATOM_DEVICE_DFP1_INDEX )
#define ATOM_DEVICE_CRT2_SUPPORT                          (0x1L << ATOM_DEVICE_CRT2_INDEX )
#define ATOM_DEVICE_LCD2_SUPPORT                          (0x1L << ATOM_DEVICE_LCD2_INDEX )
#define ATOM_DEVICE_DFP6_SUPPORT                          (0x1L << ATOM_DEVICE_DFP6_INDEX )
#define ATOM_DEVICE_DFP2_SUPPORT                          (0x1L << ATOM_DEVICE_DFP2_INDEX )
#define ATOM_DEVICE_CV_SUPPORT                            (0x1L << ATOM_DEVICE_CV_INDEX   )
#define ATOM_DEVICE_DFP3_SUPPORT                          (0x1L << ATOM_DEVICE_DFP3_INDEX )
#define ATOM_DEVICE_DFP4_SUPPORT                          (0x1L << ATOM_DEVICE_DFP4_INDEX )
#define ATOM_DEVICE_DFP5_SUPPORT                          (0x1L << ATOM_DEVICE_DFP5_INDEX )

#define ATOM_DEVICE_CRT_SUPPORT                           (ATOM_DEVICE_CRT1_SUPPORT | ATOM_DEVICE_CRT2_SUPPORT)
#define ATOM_DEVICE_DFP_SUPPORT                           (ATOM_DEVICE_DFP1_SUPPORT | ATOM_DEVICE_DFP2_SUPPORT |  ATOM_DEVICE_DFP3_SUPPORT | ATOM_DEVICE_DFP4_SUPPORT | ATOM_DEVICE_DFP5_SUPPORT | ATOM_DEVICE_DFP6_SUPPORT)
#define ATOM_DEVICE_TV_SUPPORT                            (ATOM_DEVICE_TV1_SUPPORT)
#define ATOM_DEVICE_LCD_SUPPORT                           (ATOM_DEVICE_LCD1_SUPPORT | ATOM_DEVICE_LCD2_SUPPORT)

#define ATOM_DEVICE_CONNECTOR_TYPE_MASK                   0x000000F0
#define ATOM_DEVICE_CONNECTOR_TYPE_SHIFT                  0x00000004
#define ATOM_DEVICE_CONNECTOR_VGA                         0x00000001
#define ATOM_DEVICE_CONNECTOR_DVI_I                       0x00000002
#define ATOM_DEVICE_CONNECTOR_DVI_D                       0x00000003
#define ATOM_DEVICE_CONNECTOR_DVI_A                       0x00000004
#define ATOM_DEVICE_CONNECTOR_SVIDEO                      0x00000005
#define ATOM_DEVICE_CONNECTOR_COMPOSITE                   0x00000006
#define ATOM_DEVICE_CONNECTOR_LVDS                        0x00000007
#define ATOM_DEVICE_CONNECTOR_DIGI_LINK                   0x00000008
#define ATOM_DEVICE_CONNECTOR_SCART                       0x00000009
#define ATOM_DEVICE_CONNECTOR_HDMI_TYPE_A                 0x0000000A
#define ATOM_DEVICE_CONNECTOR_HDMI_TYPE_B                 0x0000000B
#define ATOM_DEVICE_CONNECTOR_CASE_1                      0x0000000E
#define ATOM_DEVICE_CONNECTOR_DISPLAYPORT                 0x0000000F


#define ATOM_DEVICE_DAC_INFO_MASK                         0x0000000F
#define ATOM_DEVICE_DAC_INFO_SHIFT                        0x00000000
#define ATOM_DEVICE_DAC_INFO_NODAC                        0x00000000
#define ATOM_DEVICE_DAC_INFO_DACA                         0x00000001
#define ATOM_DEVICE_DAC_INFO_DACB                         0x00000002
#define ATOM_DEVICE_DAC_INFO_EXDAC                        0x00000003

#define ATOM_DEVICE_I2C_ID_NOI2C                          0x00000000

#define ATOM_DEVICE_I2C_LINEMUX_MASK                      0x0000000F
#define ATOM_DEVICE_I2C_LINEMUX_SHIFT                     0x00000000

#define ATOM_DEVICE_I2C_ID_MASK                           0x00000070
#define ATOM_DEVICE_I2C_ID_SHIFT                          0x00000004
#define ATOM_DEVICE_I2C_ID_IS_FOR_NON_MM_USE              0x00000001
#define ATOM_DEVICE_I2C_ID_IS_FOR_MM_USE                  0x00000002
#define ATOM_DEVICE_I2C_ID_IS_FOR_SDVO_USE                0x00000003    //For IGP RS600
#define ATOM_DEVICE_I2C_ID_IS_FOR_DAC_SCL                 0x00000004    //For IGP RS690

#define ATOM_DEVICE_I2C_HARDWARE_CAP_MASK                 0x00000080
#define ATOM_DEVICE_I2C_HARDWARE_CAP_SHIFT                0x00000007
#define	ATOM_DEVICE_USES_SOFTWARE_ASSISTED_I2C            0x00000000
#define	ATOM_DEVICE_USES_HARDWARE_ASSISTED_I2C            0x00000001

//  usDeviceSupport:
//  Bits0	= 0 - no CRT1 support= 1- CRT1 is supported
//  Bit 1	= 0 - no LCD1 support= 1- LCD1 is supported
//  Bit 2	= 0 - no TV1  support= 1- TV1  is supported
//  Bit 3	= 0 - no DFP1 support= 1- DFP1 is supported
//  Bit 4	= 0 - no CRT2 support= 1- CRT2 is supported
//  Bit 5	= 0 - no LCD2 support= 1- LCD2 is supported
//  Bit 6	= 0 - no DFP6 support= 1- DFP6 is supported
//  Bit 7	= 0 - no DFP2 support= 1- DFP2 is supported
//  Bit 8	= 0 - no CV   support= 1- CV   is supported
//  Bit 9	= 0 - no DFP3 support= 1- DFP3 is supported
//  Bit 10      = 0 - no DFP4 support= 1- DFP4 is supported
//  Bit 11      = 0 - no DFP5 support= 1- DFP5 is supported
//   
//  

/****************************************************************************/
/* Structure used in MclkSS_InfoTable                                       */
/****************************************************************************/
//		ucI2C_ConfigID
//    [7:0] - I2C LINE Associate ID
//          = 0   - no I2C
//    [7]		-	HW_Cap        =	1,  [6:0]=HW assisted I2C ID(HW line selection)
//                          =	0,  [6:0]=SW assisted I2C ID
//    [6-4]	- HW_ENGINE_ID  =	1,  HW engine for NON multimedia use
//                          =	2,	HW engine for Multimedia use
//                          =	3-7	Reserved for future I2C engines
//		[3-0] - I2C_LINE_MUX  = A Mux number when it's HW assisted I2C or GPIO ID when it's SW I2C

typedef struct _ATOM_I2C_ID_CONFIG
{
#if ATOM_BIG_ENDIAN
  UCHAR   bfHW_Capable:1;
  UCHAR   bfHW_EngineID:3;
  UCHAR   bfI2C_LineMux:4;
#else
  UCHAR   bfI2C_LineMux:4;
  UCHAR   bfHW_EngineID:3;
  UCHAR   bfHW_Capable:1;
#endif
}ATOM_I2C_ID_CONFIG;

typedef union _ATOM_I2C_ID_CONFIG_ACCESS
{
  ATOM_I2C_ID_CONFIG sbfAccess;
  UCHAR              ucAccess;
}ATOM_I2C_ID_CONFIG_ACCESS;
   

/****************************************************************************/	
// Structure used in GPIO_I2C_InfoTable
/****************************************************************************/	
typedef struct _ATOM_GPIO_I2C_ASSIGMENT
{
  USHORT                    usClkMaskRegisterIndex;
  USHORT                    usClkEnRegisterIndex;
  USHORT                    usClkY_RegisterIndex;
  USHORT                    usClkA_RegisterIndex;
  USHORT                    usDataMaskRegisterIndex;
  USHORT                    usDataEnRegisterIndex;
  USHORT                    usDataY_RegisterIndex;
  USHORT                    usDataA_RegisterIndex;
  ATOM_I2C_ID_CONFIG_ACCESS sucI2cId;
  UCHAR                     ucClkMaskShift;
  UCHAR                     ucClkEnShift;
  UCHAR                     ucClkY_Shift;
  UCHAR                     ucClkA_Shift;
  UCHAR                     ucDataMaskShift;
  UCHAR                     ucDataEnShift;
  UCHAR                     ucDataY_Shift;
  UCHAR                     ucDataA_Shift;
  UCHAR                     ucReserved1;
  UCHAR                     ucReserved2;
}ATOM_GPIO_I2C_ASSIGMENT;

typedef struct _ATOM_GPIO_I2C_INFO
{ 
  ATOM_COMMON_TABLE_HEADER	sHeader;
  ATOM_GPIO_I2C_ASSIGMENT   asGPIO_Info[ATOM_MAX_SUPPORTED_DEVICE];
}ATOM_GPIO_I2C_INFO;

/****************************************************************************/	
// Common Structure used in other structures
/****************************************************************************/	

#ifndef _H2INC
  
//Please don't add or expand this bitfield structure below, this one will retire soon.!
typedef struct _ATOM_MODE_MISC_INFO
{ 
#if ATOM_BIG_ENDIAN
  USHORT Reserved:6;
  USHORT RGB888:1;
  USHORT DoubleClock:1;
  USHORT Interlace:1;
  USHORT CompositeSync:1;
  USHORT V_ReplicationBy2:1;
  USHORT H_ReplicationBy2:1;
  USHORT VerticalCutOff:1;
  USHORT VSyncPolarity:1;      //0=Active High, 1=Active Low
  USHORT HSyncPolarity:1;      //0=Active High, 1=Active Low
  USHORT HorizontalCutOff:1;
#else
  USHORT HorizontalCutOff:1;
  USHORT HSyncPolarity:1;      //0=Active High, 1=Active Low
  USHORT VSyncPolarity:1;      //0=Active High, 1=Active Low
  USHORT VerticalCutOff:1;
  USHORT H_ReplicationBy2:1;
  USHORT V_ReplicationBy2:1;
  USHORT CompositeSync:1;
  USHORT Interlace:1;
  USHORT DoubleClock:1;
  USHORT RGB888:1;
  USHORT Reserved:6;           
#endif
}ATOM_MODE_MISC_INFO;
  
typedef union _ATOM_MODE_MISC_INFO_ACCESS
{ 
  ATOM_MODE_MISC_INFO sbfAccess;
  USHORT              usAccess;
}ATOM_MODE_MISC_INFO_ACCESS;
  
#else
  
typedef union _ATOM_MODE_MISC_INFO_ACCESS
{ 
  USHORT              usAccess;
}ATOM_MODE_MISC_INFO_ACCESS;
   
#endif

// usModeMiscInfo-
#define ATOM_H_CUTOFF           0x01
#define ATOM_HSYNC_POLARITY     0x02             //0=Active High, 1=Active Low
#define ATOM_VSYNC_POLARITY     0x04             //0=Active High, 1=Active Low
#define ATOM_V_CUTOFF           0x08
#define ATOM_H_REPLICATIONBY2   0x10
#define ATOM_V_REPLICATIONBY2   0x20
#define ATOM_COMPOSITESYNC      0x40
#define ATOM_INTERLACE          0x80
#define ATOM_DOUBLE_CLOCK_MODE  0x100
#define ATOM_RGB888_MODE        0x200

//usRefreshRate-
#define ATOM_REFRESH_43         43
#define ATOM_REFRESH_47         47
#define ATOM_REFRESH_56         56	
#define ATOM_REFRESH_60         60
#define ATOM_REFRESH_65         65
#define ATOM_REFRESH_70         70
#define ATOM_REFRESH_72         72
#define ATOM_REFRESH_75         75
#define ATOM_REFRESH_85         85

// ATOM_MODE_TIMING data are exactly the same as VESA timing data.
// Translation from EDID to ATOM_MODE_TIMING, use the following formula.
//
//	VESA_HTOTAL			=	VESA_ACTIVE + 2* VESA_BORDER + VESA_BLANK
//						=	EDID_HA + EDID_HBL
//	VESA_HDISP			=	VESA_ACTIVE	=	EDID_HA
//	VESA_HSYNC_START	=	VESA_ACTIVE + VESA_BORDER + VESA_FRONT_PORCH
//						=	EDID_HA + EDID_HSO
//	VESA_HSYNC_WIDTH	=	VESA_HSYNC_TIME	=	EDID_HSPW
//	VESA_BORDER			=	EDID_BORDER

/****************************************************************************/	
// Structure used in SetCRTC_UsingDTDTimingTable
/****************************************************************************/	
typedef struct _SET_CRTC_USING_DTD_TIMING_PARAMETERS
{
  USHORT  usH_Size;
  USHORT  usH_Blanking_Time;
  USHORT  usV_Size;
  USHORT  usV_Blanking_Time;			
  USHORT  usH_SyncOffset;
  USHORT  usH_SyncWidth;
  USHORT  usV_SyncOffset;
  USHORT  usV_SyncWidth;
  ATOM_MODE_MISC_INFO_ACCESS  susModeMiscInfo;  
  UCHAR   ucH_Border;         // From DFP EDID
  UCHAR   ucV_Border;
  UCHAR   ucCRTC;             // ATOM_CRTC1 or ATOM_CRTC2  
  UCHAR   ucPadding[3];
}SET_CRTC_USING_DTD_TIMING_PARAMETERS;

/****************************************************************************/	
// Structure used in SetCRTC_TimingTable
/****************************************************************************/	
typedef struct _SET_CRTC_TIMING_PARAMETERS
{
  USHORT                      usH_Total;        // horizontal total
  USHORT                      usH_Disp;         // horizontal display
  USHORT                      usH_SyncStart;    // horozontal Sync start
  USHORT                      usH_SyncWidth;    // horizontal Sync width
  USHORT                      usV_Total;        // vertical total
  USHORT                      usV_Disp;         // vertical display
  USHORT                      usV_SyncStart;    // vertical Sync start
  USHORT                      usV_SyncWidth;    // vertical Sync width
  ATOM_MODE_MISC_INFO_ACCESS  susModeMiscInfo;
  UCHAR                       ucCRTC;           // ATOM_CRTC1 or ATOM_CRTC2
  UCHAR                       ucOverscanRight;  // right
  UCHAR                       ucOverscanLeft;   // left
  UCHAR                       ucOverscanBottom; // bottom
  UCHAR                       ucOverscanTop;    // top
  UCHAR                       ucReserved;
}SET_CRTC_TIMING_PARAMETERS;
#define SET_CRTC_TIMING_PARAMETERS_PS_ALLOCATION SET_CRTC_TIMING_PARAMETERS

/****************************************************************************/	
// Structure used in StandardVESA_TimingTable
//                   AnalogTV_InfoTable 
//                   ComponentVideoInfoTable
/****************************************************************************/	
typedef struct _ATOM_MODE_TIMING
{
  USHORT  usCRTC_H_Total;
  USHORT  usCRTC_H_Disp;
  USHORT  usCRTC_H_SyncStart;
  USHORT  usCRTC_H_SyncWidth;
  USHORT  usCRTC_V_Total;
  USHORT  usCRTC_V_Disp;
  USHORT  usCRTC_V_SyncStart;
  USHORT  usCRTC_V_SyncWidth;
  USHORT  usPixelClock;					                 //in 10Khz unit
  ATOM_MODE_MISC_INFO_ACCESS  susModeMiscInfo;
  USHORT  usCRTC_OverscanRight;
  USHORT  usCRTC_OverscanLeft;
  USHORT  usCRTC_OverscanBottom;
  USHORT  usCRTC_OverscanTop;
  USHORT  usReserve;
  UCHAR   ucInternalModeNumber;
  UCHAR   ucRefreshRate;
}ATOM_MODE_TIMING;

typedef struct _ATOM_DTD_FORMAT
{
  USHORT  usPixClk;
  USHORT  usHActive;
  USHORT  usHBlanking_Time;
  USHORT  usVActive;
  USHORT  usVBlanking_Time;			
  USHORT  usHSyncOffset;
  USHORT  usHSyncWidth;
  USHORT  usVSyncOffset;
  USHORT  usVSyncWidth;
  USHORT  usImageHSize;
  USHORT  usImageVSize;
  UCHAR   ucHBorder;
  UCHAR   ucVBorder;
  ATOM_MODE_MISC_INFO_ACCESS susModeMiscInfo;
  UCHAR   ucInternalModeNumber;
  UCHAR   ucRefreshRate;
}ATOM_DTD_FORMAT;

/****************************************************************************/	
// Structure used in LVDS_InfoTable 
//  * Need a document to describe this table
/****************************************************************************/	
#define SUPPORTED_LCD_REFRESHRATE_30Hz          0x0004
#define SUPPORTED_LCD_REFRESHRATE_40Hz          0x0008
#define SUPPORTED_LCD_REFRESHRATE_50Hz          0x0010
#define SUPPORTED_LCD_REFRESHRATE_60Hz          0x0020

//ucTableFormatRevision=1
//ucTableContentRevision=1
typedef struct _ATOM_LVDS_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  ATOM_DTD_FORMAT     sLCDTiming;
  USHORT              usModePatchTableOffset;
  USHORT              usSupportedRefreshRate;     //Refer to panel info table in ATOMBIOS extension Spec.
  USHORT              usOffDelayInMs;
  UCHAR               ucPowerSequenceDigOntoDEin10Ms;
  UCHAR               ucPowerSequenceDEtoBLOnin10Ms;
  UCHAR               ucLVDS_Misc;               // Bit0:{=0:single, =1:dual},Bit1 {=0:666RGB, =1:888RGB},Bit2:3:{Grey level}
                                                 // Bit4:{=0:LDI format for RGB888, =1 FPDI format for RGB888}
                                                 // Bit5:{=0:Spatial Dithering disabled;1 Spatial Dithering enabled}
                                                 // Bit6:{=0:Temporal Dithering disabled;1 Temporal Dithering enabled}
  UCHAR               ucPanelDefaultRefreshRate;
  UCHAR               ucPanelIdentification;
  UCHAR               ucSS_Id;
}ATOM_LVDS_INFO;

//ucTableFormatRevision=1
//ucTableContentRevision=2
typedef struct _ATOM_LVDS_INFO_V12
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  ATOM_DTD_FORMAT     sLCDTiming;
  USHORT              usExtInfoTableOffset;
  USHORT              usSupportedRefreshRate;     //Refer to panel info table in ATOMBIOS extension Spec.
  USHORT              usOffDelayInMs;
  UCHAR               ucPowerSequenceDigOntoDEin10Ms;
  UCHAR               ucPowerSequenceDEtoBLOnin10Ms;
  UCHAR               ucLVDS_Misc;               // Bit0:{=0:single, =1:dual},Bit1 {=0:666RGB, =1:888RGB},Bit2:3:{Grey level}
                                                 // Bit4:{=0:LDI format for RGB888, =1 FPDI format for RGB888}
                                                 // Bit5:{=0:Spatial Dithering disabled;1 Spatial Dithering enabled}
                                                 // Bit6:{=0:Temporal Dithering disabled;1 Temporal Dithering enabled}
  UCHAR               ucPanelDefaultRefreshRate;
  UCHAR               ucPanelIdentification;
  UCHAR               ucSS_Id;
  USHORT              usLCDVenderID;
  USHORT              usLCDProductID;
  UCHAR               ucLCDPanel_SpecialHandlingCap; 
	UCHAR								ucPanelInfoSize;					//  start from ATOM_DTD_FORMAT to end of panel info, include ExtInfoTable
  UCHAR               ucReserved[2];
}ATOM_LVDS_INFO_V12;

//Definitions for ucLCDPanel_SpecialHandlingCap:

//Once DAL sees this CAP is set, it will read EDID from LCD on its own instead of using sLCDTiming in ATOM_LVDS_INFO_V12. 
//Other entries in ATOM_LVDS_INFO_V12 are still valid/useful to DAL 
#define	LCDPANEL_CAP_READ_EDID                  0x1

//If a design supports DRR (dynamic refresh rate) on internal panels (LVDS or EDP), this cap is set in ucLCDPanel_SpecialHandlingCap together
//with multiple supported refresh rates@usSupportedRefreshRate. This cap should not be set when only slow refresh rate is supported (static
//refresh rate switch by SW. This is only valid from ATOM_LVDS_INFO_V12
#define	LCDPANEL_CAP_DRR_SUPPORTED              0x2

//Use this cap bit for a quick reference whether an embadded panel (LCD1 ) is LVDS or eDP.
#define	LCDPANEL_CAP_eDP                        0x4


//Color Bit Depth definition in EDID V1.4 @BYTE 14h
//Bit 6  5  4
                              //      0  0  0  -  Color bit depth is undefined
                              //      0  0  1  -  6 Bits per Primary Color
                              //      0  1  0  -  8 Bits per Primary Color
                              //      0  1  1  - 10 Bits per Primary Color
                              //      1  0  0  - 12 Bits per Primary Color
                              //      1  0  1  - 14 Bits per Primary Color
                              //      1  1  0  - 16 Bits per Primary Color
                              //      1  1  1  - Reserved

#define PANEL_COLOR_BIT_DEPTH_MASK    0x70

// Bit7:{=0:Random Dithering disabled;1 Random Dithering enabled}   
#define PANEL_RANDOM_DITHER   0x80
#define PANEL_RANDOM_DITHER_MASK   0x80

#define ATOM_LVDS_INFO_LAST  ATOM_LVDS_INFO_V12   // no need to change this 

/****************************************************************************/	
// Structures used by LCD_InfoTable V1.3    Note: previous version was called ATOM_LVDS_INFO_V12
// ASIC Families:  NI
// ucTableFormatRevision=1
// ucTableContentRevision=3
/****************************************************************************/	
typedef struct _ATOM_LCD_INFO_V13
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  ATOM_DTD_FORMAT     sLCDTiming;
  USHORT              usExtInfoTableOffset;
  USHORT              usSupportedRefreshRate;     //Refer to panel info table in ATOMBIOS extension Spec.
  ULONG               ulReserved0;
  UCHAR               ucLCD_Misc;                // Reorganized in V13
                                                 // Bit0: {=0:single, =1:dual},
                                                 // Bit1: {=0:LDI format for RGB888, =1 FPDI format for RGB888}  // was {=0:666RGB, =1:888RGB},
                                                 // Bit3:2: {Grey level}
                                                 // Bit6:4 Color Bit Depth definition (see below definition in EDID V1.4 @BYTE 14h) 
                                                 // Bit7   Reserved.  was for ATOM_PANEL_MISC_API_ENABLED, still need it?  
  UCHAR               ucPanelDefaultRefreshRate;
  UCHAR               ucPanelIdentification;
  UCHAR               ucSS_Id;
  USHORT              usLCDVenderID;
  USHORT              usLCDProductID;
  UCHAR               ucLCDPanel_SpecialHandlingCap;  // Reorganized in V13 
                                                 // Bit0: Once DAL sees this CAP is set, it will read EDID from LCD on its own
                                                 // Bit1: See LCDPANEL_CAP_DRR_SUPPORTED
                                                 // Bit2: a quick reference whether an embadded panel (LCD1 ) is LVDS (0) or eDP (1)
                                                 // Bit7-3: Reserved 
  UCHAR               ucPanelInfoSize;					 //  start from ATOM_DTD_FORMAT to end of panel info, include ExtInfoTable
  USHORT              usBacklightPWM;            //  Backlight PWM in Hz. New in _V13

  UCHAR               ucPowerSequenceDIGONtoDE_in4Ms;
  UCHAR               ucPowerSequenceDEtoVARY_BL_in4Ms;
  UCHAR               ucPowerSequenceVARY_BLtoDE_in4Ms;
  UCHAR               ucPowerSequenceDEtoDIGON_in4Ms;

  UCHAR               ucOffDelay_in4Ms;
  UCHAR               ucPowerSequenceVARY_BLtoBLON_in4Ms;
  UCHAR               ucPowerSequenceBLONtoVARY_BL_in4Ms;
  UCHAR               ucReserved1;

  UCHAR               ucDPCD_eDP_CONFIGURATION_CAP;     // dpcd 0dh
  UCHAR               ucDPCD_MAX_LINK_RATE;             // dpcd 01h
  UCHAR               ucDPCD_MAX_LANE_COUNT;            // dpcd 02h
  UCHAR               ucDPCD_MAX_DOWNSPREAD;            // dpcd 03h

  USHORT              usMaxPclkFreqInSingleLink;        // Max PixelClock frequency in single link mode. 
  UCHAR               uceDPToLVDSRxId;
  UCHAR               ucLcdReservd;
  ULONG               ulReserved[2];
}ATOM_LCD_INFO_V13;  

#define ATOM_LCD_INFO_LAST  ATOM_LCD_INFO_V13    

//Definitions for ucLCD_Misc
#define ATOM_PANEL_MISC_V13_DUAL                   0x00000001
#define ATOM_PANEL_MISC_V13_FPDI                   0x00000002
#define ATOM_PANEL_MISC_V13_GREY_LEVEL             0x0000000C
#define ATOM_PANEL_MISC_V13_GREY_LEVEL_SHIFT       2
#define ATOM_PANEL_MISC_V13_COLOR_BIT_DEPTH_MASK   0x70
#define ATOM_PANEL_MISC_V13_6BIT_PER_COLOR         0x10
#define ATOM_PANEL_MISC_V13_8BIT_PER_COLOR         0x20

//Color Bit Depth definition in EDID V1.4 @BYTE 14h
//Bit 6  5  4
                              //      0  0  0  -  Color bit depth is undefined
                              //      0  0  1  -  6 Bits per Primary Color
                              //      0  1  0  -  8 Bits per Primary Color
                              //      0  1  1  - 10 Bits per Primary Color
                              //      1  0  0  - 12 Bits per Primary Color
                              //      1  0  1  - 14 Bits per Primary Color
                              //      1  1  0  - 16 Bits per Primary Color
                              //      1  1  1  - Reserved
 
//Definitions for ucLCDPanel_SpecialHandlingCap:

//Once DAL sees this CAP is set, it will read EDID from LCD on its own instead of using sLCDTiming in ATOM_LVDS_INFO_V12. 
//Other entries in ATOM_LVDS_INFO_V12 are still valid/useful to DAL 
#define	LCDPANEL_CAP_V13_READ_EDID              0x1        // = LCDPANEL_CAP_READ_EDID no change comparing to previous version

//If a design supports DRR (dynamic refresh rate) on internal panels (LVDS or EDP), this cap is set in ucLCDPanel_SpecialHandlingCap together
//with multiple supported refresh rates@usSupportedRefreshRate. This cap should not be set when only slow refresh rate is supported (static
//refresh rate switch by SW. This is only valid from ATOM_LVDS_INFO_V12
#define	LCDPANEL_CAP_V13_DRR_SUPPORTED          0x2        // = LCDPANEL_CAP_DRR_SUPPORTED no change comparing to previous version

//Use this cap bit for a quick reference whether an embadded panel (LCD1 ) is LVDS or eDP.
#define	LCDPANEL_CAP_V13_eDP                    0x4        // = LCDPANEL_CAP_eDP no change comparing to previous version

//uceDPToLVDSRxId
#define eDP_TO_LVDS_RX_DISABLE                  0x00       // no eDP->LVDS translator chip 
#define eDP_TO_LVDS_COMMON_ID                   0x01       // common eDP->LVDS translator chip without AMD SW init
#define eDP_TO_LVDS_RT_ID                       0x02       // RT tanslator which require AMD SW init

typedef struct  _ATOM_PATCH_RECORD_MODE
{
  UCHAR     ucRecordType;
  USHORT    usHDisp;
  USHORT    usVDisp;
}ATOM_PATCH_RECORD_MODE;

typedef struct  _ATOM_LCD_RTS_RECORD
{
  UCHAR     ucRecordType;
  UCHAR     ucRTSValue;
}ATOM_LCD_RTS_RECORD;

//!! If the record below exists, it should always be the first record for easy use in command table!!!
// The record below is only used when LVDS_Info is present. From ATOM_LVDS_INFO_V12, use ucLCDPanel_SpecialHandlingCap instead.
typedef struct  _ATOM_LCD_MODE_CONTROL_CAP
{
  UCHAR     ucRecordType;
  USHORT    usLCDCap;
}ATOM_LCD_MODE_CONTROL_CAP;

#define LCD_MODE_CAP_BL_OFF                   1
#define LCD_MODE_CAP_CRTC_OFF                 2
#define LCD_MODE_CAP_PANEL_OFF                4

typedef struct _ATOM_FAKE_EDID_PATCH_RECORD
{
  UCHAR ucRecordType;
  UCHAR ucFakeEDIDLength;
  UCHAR ucFakeEDIDString[];    // This actually has ucFakeEdidLength elements.
} ATOM_FAKE_EDID_PATCH_RECORD;

typedef struct  _ATOM_PANEL_RESOLUTION_PATCH_RECORD
{
   UCHAR    ucRecordType;
   USHORT		usHSize;
   USHORT		usVSize;
}ATOM_PANEL_RESOLUTION_PATCH_RECORD;

#define LCD_MODE_PATCH_RECORD_MODE_TYPE       1
#define LCD_RTS_RECORD_TYPE                   2
#define LCD_CAP_RECORD_TYPE                   3
#define LCD_FAKE_EDID_PATCH_RECORD_TYPE       4
#define LCD_PANEL_RESOLUTION_RECORD_TYPE      5
#define LCD_EDID_OFFSET_PATCH_RECORD_TYPE     6
#define ATOM_RECORD_END_TYPE                  0xFF

/****************************Spread Spectrum Info Table Definitions **********************/

//ucTableFormatRevision=1
//ucTableContentRevision=2
typedef struct _ATOM_SPREAD_SPECTRUM_ASSIGNMENT
{
  USHORT              usSpreadSpectrumPercentage; 
  UCHAR               ucSpreadSpectrumType;	    //Bit1=0 Down Spread,=1 Center Spread. Bit1=1 Ext. =0 Int. Bit2=1: PCIE REFCLK SS =0 iternal PPLL SS  Others:TBD
  UCHAR               ucSS_Step;
  UCHAR               ucSS_Delay;
  UCHAR               ucSS_Id;
  UCHAR               ucRecommendedRef_Div;
  UCHAR               ucSS_Range;               //it was reserved for V11
}ATOM_SPREAD_SPECTRUM_ASSIGNMENT;

#define ATOM_MAX_SS_ENTRY                      16
#define ATOM_DP_SS_ID1												 0x0f1			// SS ID for internal DP stream at 2.7Ghz. if ATOM_DP_SS_ID2 does not exist in SS_InfoTable, it is used for internal DP stream at 1.62Ghz as well. 
#define ATOM_DP_SS_ID2												 0x0f2			// SS ID for internal DP stream at 1.62Ghz, if it exists in SS_InfoTable. 
#define ATOM_LVLINK_2700MHz_SS_ID              0x0f3      // SS ID for LV link translator chip at 2.7Ghz
#define ATOM_LVLINK_1620MHz_SS_ID              0x0f4      // SS ID for LV link translator chip at 1.62Ghz


#define ATOM_SS_DOWN_SPREAD_MODE_MASK          0x00000000
#define ATOM_SS_DOWN_SPREAD_MODE               0x00000000
#define ATOM_SS_CENTRE_SPREAD_MODE_MASK        0x00000001
#define ATOM_SS_CENTRE_SPREAD_MODE             0x00000001
#define ATOM_INTERNAL_SS_MASK                  0x00000000
#define ATOM_EXTERNAL_SS_MASK                  0x00000002
#define EXEC_SS_STEP_SIZE_SHIFT                2
#define EXEC_SS_DELAY_SHIFT                    4    
#define ACTIVEDATA_TO_BLON_DELAY_SHIFT         4

typedef struct _ATOM_SPREAD_SPECTRUM_INFO
{ 
  ATOM_COMMON_TABLE_HEADER	sHeader;
  ATOM_SPREAD_SPECTRUM_ASSIGNMENT   asSS_Info[ATOM_MAX_SS_ENTRY];
}ATOM_SPREAD_SPECTRUM_INFO;

/****************************************************************************/	
// Structure used in AnalogTV_InfoTable (Top level)
/****************************************************************************/	
//ucTVBootUpDefaultStd definition:

//ATOM_TV_NTSC                1
//ATOM_TV_NTSCJ               2
//ATOM_TV_PAL                 3
//ATOM_TV_PALM                4
//ATOM_TV_PALCN               5
//ATOM_TV_PALN                6
//ATOM_TV_PAL60               7
//ATOM_TV_SECAM               8

//ucTVSupportedStd definition:
#define NTSC_SUPPORT          0x1
#define NTSCJ_SUPPORT         0x2

#define PAL_SUPPORT           0x4
#define PALM_SUPPORT          0x8
#define PALCN_SUPPORT         0x10
#define PALN_SUPPORT          0x20
#define PAL60_SUPPORT         0x40
#define SECAM_SUPPORT         0x80

#define MAX_SUPPORTED_TV_TIMING    2

typedef struct _ATOM_ANALOG_TV_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  UCHAR                    ucTV_SupportedStandard;
  UCHAR                    ucTV_BootUpDefaultStandard; 
  UCHAR                    ucExt_TV_ASIC_ID;
  UCHAR                    ucExt_TV_ASIC_SlaveAddr;
  /*ATOM_DTD_FORMAT          aModeTimings[MAX_SUPPORTED_TV_TIMING];*/
  ATOM_MODE_TIMING         aModeTimings[MAX_SUPPORTED_TV_TIMING];
}ATOM_ANALOG_TV_INFO;

#define MAX_SUPPORTED_TV_TIMING_V1_2    3

typedef struct _ATOM_ANALOG_TV_INFO_V1_2
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  UCHAR                    ucTV_SupportedStandard;
  UCHAR                    ucTV_BootUpDefaultStandard; 
  UCHAR                    ucExt_TV_ASIC_ID;
  UCHAR                    ucExt_TV_ASIC_SlaveAddr;
  ATOM_DTD_FORMAT          aModeTimings[MAX_SUPPORTED_TV_TIMING_V1_2];
}ATOM_ANALOG_TV_INFO_V1_2;

typedef struct _ATOM_DPCD_INFO
{
  UCHAR   ucRevisionNumber;        //10h : Revision 1.0; 11h : Revision 1.1   
  UCHAR   ucMaxLinkRate;           //06h : 1.62Gbps per lane; 0Ah = 2.7Gbps per lane
  UCHAR   ucMaxLane;               //Bits 4:0 = MAX_LANE_COUNT (1/2/4). Bit 7 = ENHANCED_FRAME_CAP 
  UCHAR   ucMaxDownSpread;         //Bit0 = 0: No Down spread; Bit0 = 1: 0.5% (Subject to change according to DP spec)
}ATOM_DPCD_INFO;

#define ATOM_DPCD_MAX_LANE_MASK    0x1F

/**************************************************************************/
// VRAM usage and their defintions

// One chunk of VRAM used by Bios are for HWICON surfaces,EDID data.
// Current Mode timing and Dail Timing and/or STD timing data EACH device. They can be broken down as below.
// All the addresses below are the offsets from the frame buffer start.They all MUST be Dword aligned!
// To driver: The physical address of this memory portion=mmFB_START(4K aligned)+ATOMBIOS_VRAM_USAGE_START_ADDR+ATOM_x_ADDR
// To Bios:  ATOMBIOS_VRAM_USAGE_START_ADDR+ATOM_x_ADDR->MM_INDEX 

#ifndef VESA_MEMORY_IN_64K_BLOCK
#define VESA_MEMORY_IN_64K_BLOCK        0x100       //256*64K=16Mb (Max. VESA memory is 16Mb!)
#endif

#define ATOM_EDID_RAW_DATASIZE          256         //In Bytes
#define ATOM_HWICON_SURFACE_SIZE        4096        //In Bytes
#define ATOM_HWICON_INFOTABLE_SIZE      32
#define MAX_DTD_MODE_IN_VRAM            6
#define ATOM_DTD_MODE_SUPPORT_TBL_SIZE  (MAX_DTD_MODE_IN_VRAM*28)    //28= (SIZEOF ATOM_DTD_FORMAT) 
#define ATOM_STD_MODE_SUPPORT_TBL_SIZE  32*8                         //32 is a predefined number,8= (SIZEOF ATOM_STD_FORMAT)
//20 bytes for Encoder Type and DPCD in STD EDID area
#define DFP_ENCODER_TYPE_OFFSET         (ATOM_EDID_RAW_DATASIZE + ATOM_DTD_MODE_SUPPORT_TBL_SIZE + ATOM_STD_MODE_SUPPORT_TBL_SIZE - 20)    
#define ATOM_DP_DPCD_OFFSET             (DFP_ENCODER_TYPE_OFFSET + 4 )        

#define ATOM_HWICON1_SURFACE_ADDR       0
#define ATOM_HWICON2_SURFACE_ADDR       (ATOM_HWICON1_SURFACE_ADDR + ATOM_HWICON_SURFACE_SIZE)
#define ATOM_HWICON_INFOTABLE_ADDR      (ATOM_HWICON2_SURFACE_ADDR + ATOM_HWICON_SURFACE_SIZE)
#define ATOM_CRT1_EDID_ADDR             (ATOM_HWICON_INFOTABLE_ADDR + ATOM_HWICON_INFOTABLE_SIZE)
#define ATOM_CRT1_DTD_MODE_TBL_ADDR     (ATOM_CRT1_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_CRT1_STD_MODE_TBL_ADDR	    (ATOM_CRT1_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_LCD1_EDID_ADDR             (ATOM_CRT1_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_LCD1_DTD_MODE_TBL_ADDR     (ATOM_LCD1_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_LCD1_STD_MODE_TBL_ADDR   	(ATOM_LCD1_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_TV1_DTD_MODE_TBL_ADDR      (ATOM_LCD1_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_DFP1_EDID_ADDR             (ATOM_TV1_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_DFP1_DTD_MODE_TBL_ADDR     (ATOM_DFP1_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_DFP1_STD_MODE_TBL_ADDR	    (ATOM_DFP1_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_CRT2_EDID_ADDR             (ATOM_DFP1_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_CRT2_DTD_MODE_TBL_ADDR     (ATOM_CRT2_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_CRT2_STD_MODE_TBL_ADDR	    (ATOM_CRT2_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_LCD2_EDID_ADDR             (ATOM_CRT2_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_LCD2_DTD_MODE_TBL_ADDR     (ATOM_LCD2_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_LCD2_STD_MODE_TBL_ADDR   	(ATOM_LCD2_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_DFP6_EDID_ADDR             (ATOM_LCD2_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_DFP6_DTD_MODE_TBL_ADDR     (ATOM_DFP6_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_DFP6_STD_MODE_TBL_ADDR     (ATOM_DFP6_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_DFP2_EDID_ADDR             (ATOM_DFP6_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_DFP2_DTD_MODE_TBL_ADDR     (ATOM_DFP2_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_DFP2_STD_MODE_TBL_ADDR     (ATOM_DFP2_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_CV_EDID_ADDR               (ATOM_DFP2_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_CV_DTD_MODE_TBL_ADDR       (ATOM_CV_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_CV_STD_MODE_TBL_ADDR       (ATOM_CV_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_DFP3_EDID_ADDR             (ATOM_CV_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_DFP3_DTD_MODE_TBL_ADDR     (ATOM_DFP3_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_DFP3_STD_MODE_TBL_ADDR     (ATOM_DFP3_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_DFP4_EDID_ADDR             (ATOM_DFP3_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_DFP4_DTD_MODE_TBL_ADDR     (ATOM_DFP4_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_DFP4_STD_MODE_TBL_ADDR     (ATOM_DFP4_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_DFP5_EDID_ADDR             (ATOM_DFP4_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)
#define ATOM_DFP5_DTD_MODE_TBL_ADDR     (ATOM_DFP5_EDID_ADDR + ATOM_EDID_RAW_DATASIZE)
#define ATOM_DFP5_STD_MODE_TBL_ADDR     (ATOM_DFP5_DTD_MODE_TBL_ADDR + ATOM_DTD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_DP_TRAINING_TBL_ADDR       (ATOM_DFP5_STD_MODE_TBL_ADDR + ATOM_STD_MODE_SUPPORT_TBL_SIZE)

#define ATOM_STACK_STORAGE_START        (ATOM_DP_TRAINING_TBL_ADDR + 1024)       
#define ATOM_STACK_STORAGE_END          ATOM_STACK_STORAGE_START + 512        

//The size below is in Kb!
#define ATOM_VRAM_RESERVE_SIZE         ((((ATOM_STACK_STORAGE_END - ATOM_HWICON1_SURFACE_ADDR)>>10)+4)&0xFFFC)
   
#define ATOM_VRAM_RESERVE_V2_SIZE      32

#define	ATOM_VRAM_OPERATION_FLAGS_MASK         0xC0000000L
#define ATOM_VRAM_OPERATION_FLAGS_SHIFT        30
#define	ATOM_VRAM_BLOCK_NEEDS_NO_RESERVATION   0x1
#define	ATOM_VRAM_BLOCK_NEEDS_RESERVATION      0x0

/***********************************************************************************/	
// Structure used in VRAM_UsageByFirmwareTable
// Note1: This table is filled by SetBiosReservationStartInFB in CoreCommSubs.asm
//        at running time.   
// note2: From RV770, the memory is more than 32bit addressable, so we will change 
//        ucTableFormatRevision=1,ucTableContentRevision=4, the structure remains
//        exactly same as 1.1 and 1.2 (1.3 is never in use), but ulStartAddrUsedByFirmware 
//        (in offset to start of memory address) is KB aligned instead of byte aligend.
/***********************************************************************************/	
// Note3:
/* If we change usReserved to "usFBUsedbyDrvInKB", then to VBIOS this usFBUsedbyDrvInKB is a predefined, unchanged constant across VGA or non VGA adapter,
for CAIL, The size of FB access area is known, only thing missing is the Offset of FB Access area, so we can  have:

If (ulStartAddrUsedByFirmware!=0)
FBAccessAreaOffset= ulStartAddrUsedByFirmware - usFBUsedbyDrvInKB;
Reserved area has been claimed by VBIOS including this FB access area; CAIL doesn't need to reserve any extra area for this purpose
else	//Non VGA case
 if (FB_Size<=2Gb)
    FBAccessAreaOffset= FB_Size - usFBUsedbyDrvInKB;
 else
	  FBAccessAreaOffset= Aper_Size - usFBUsedbyDrvInKB

CAIL needs to claim an reserved area defined by FBAccessAreaOffset and usFBUsedbyDrvInKB in non VGA case.*/

/***********************************************************************************/	
#define ATOM_MAX_FIRMWARE_VRAM_USAGE_INFO			1

typedef struct _ATOM_FIRMWARE_VRAM_RESERVE_INFO
{
  ULONG   ulStartAddrUsedByFirmware;
  USHORT  usFirmwareUseInKb;
  USHORT  usReserved;
}ATOM_FIRMWARE_VRAM_RESERVE_INFO;

typedef struct _ATOM_VRAM_USAGE_BY_FIRMWARE
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  ATOM_FIRMWARE_VRAM_RESERVE_INFO	asFirmwareVramReserveInfo[ATOM_MAX_FIRMWARE_VRAM_USAGE_INFO];
}ATOM_VRAM_USAGE_BY_FIRMWARE;

// change version to 1.5, when allow driver to allocate the vram area for command table access.
typedef struct _ATOM_FIRMWARE_VRAM_RESERVE_INFO_V1_5
{
  ULONG   ulStartAddrUsedByFirmware;
  USHORT  usFirmwareUseInKb;
  USHORT  usFBUsedByDrvInKb;
}ATOM_FIRMWARE_VRAM_RESERVE_INFO_V1_5;

typedef struct _ATOM_VRAM_USAGE_BY_FIRMWARE_V1_5
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  ATOM_FIRMWARE_VRAM_RESERVE_INFO_V1_5	asFirmwareVramReserveInfo[ATOM_MAX_FIRMWARE_VRAM_USAGE_INFO];
}ATOM_VRAM_USAGE_BY_FIRMWARE_V1_5;

/****************************************************************************/	
// Structure used in GPIO_Pin_LUTTable
/****************************************************************************/	
typedef struct _ATOM_GPIO_PIN_ASSIGNMENT
{
  USHORT                   usGpioPin_AIndex;
  UCHAR                    ucGpioPinBitShift;
  UCHAR                    ucGPIO_ID;
}ATOM_GPIO_PIN_ASSIGNMENT;

//ucGPIO_ID pre-define id for multiple usage
//from SMU7.x, if ucGPIO_ID=PP_AC_DC_SWITCH_GPIO_PINID in GPIO_LUTTable, AC/DC switching feature is enable
#define PP_AC_DC_SWITCH_GPIO_PINID          60
//from SMU7.x, if ucGPIO_ID=VDDC_REGULATOR_VRHOT_GPIO_PINID in GPIO_LUTable, VRHot feature is enable
#define VDDC_VRHOT_GPIO_PINID               61
//if ucGPIO_ID=VDDC_PCC_GPIO_PINID in GPIO_LUTable, Peak Current Control feature is enabled
#define VDDC_PCC_GPIO_PINID                 62

typedef struct _ATOM_GPIO_PIN_LUT
{
  ATOM_COMMON_TABLE_HEADER  sHeader;
  ATOM_GPIO_PIN_ASSIGNMENT	asGPIO_Pin[1];
}ATOM_GPIO_PIN_LUT;

/****************************************************************************/	
// Structure used in ComponentVideoInfoTable	
/****************************************************************************/	
#define GPIO_PIN_ACTIVE_HIGH          0x1

#define MAX_SUPPORTED_CV_STANDARDS    5

// definitions for ATOM_D_INFO.ucSettings
#define ATOM_GPIO_SETTINGS_BITSHIFT_MASK  0x1F    // [4:0]
#define ATOM_GPIO_SETTINGS_RESERVED_MASK  0x60    // [6:5] = must be zeroed out
#define ATOM_GPIO_SETTINGS_ACTIVE_MASK    0x80    // [7]

typedef struct _ATOM_GPIO_INFO
{
  USHORT  usAOffset;
  UCHAR   ucSettings;
  UCHAR   ucReserved;
}ATOM_GPIO_INFO;

// definitions for ATOM_COMPONENT_VIDEO_INFO.ucMiscInfo (bit vector)
#define ATOM_CV_RESTRICT_FORMAT_SELECTION           0x2

// definitions for ATOM_COMPONENT_VIDEO_INFO.uc480i/uc480p/uc720p/uc1080i
#define ATOM_GPIO_DEFAULT_MODE_EN                   0x80 //[7];
#define ATOM_GPIO_SETTING_PERMODE_MASK              0x7F //[6:0]

// definitions for ATOM_COMPONENT_VIDEO_INFO.ucLetterBoxMode
//Line 3 out put 5V.
#define ATOM_CV_LINE3_ASPECTRATIO_16_9_GPIO_A       0x01     //represent gpio 3 state for 16:9
#define ATOM_CV_LINE3_ASPECTRATIO_16_9_GPIO_B       0x02     //represent gpio 4 state for 16:9
#define ATOM_CV_LINE3_ASPECTRATIO_16_9_GPIO_SHIFT   0x0   

//Line 3 out put 2.2V              
#define ATOM_CV_LINE3_ASPECTRATIO_4_3_LETBOX_GPIO_A 0x04     //represent gpio 3 state for 4:3 Letter box
#define ATOM_CV_LINE3_ASPECTRATIO_4_3_LETBOX_GPIO_B 0x08     //represent gpio 4 state for 4:3 Letter box
#define ATOM_CV_LINE3_ASPECTRATIO_4_3_LETBOX_GPIO_SHIFT 0x2     

//Line 3 out put 0V
#define ATOM_CV_LINE3_ASPECTRATIO_4_3_GPIO_A        0x10     //represent gpio 3 state for 4:3
#define ATOM_CV_LINE3_ASPECTRATIO_4_3_GPIO_B        0x20     //represent gpio 4 state for 4:3
#define ATOM_CV_LINE3_ASPECTRATIO_4_3_GPIO_SHIFT    0x4 

#define ATOM_CV_LINE3_ASPECTRATIO_MASK              0x3F     // bit [5:0]

#define ATOM_CV_LINE3_ASPECTRATIO_EXIST             0x80     //bit 7

//GPIO bit index in gpio setting per mode value, also represend the block no. in gpio blocks.
#define ATOM_GPIO_INDEX_LINE3_ASPECRATIO_GPIO_A   3   //bit 3 in uc480i/uc480p/uc720p/uc1080i, which represend the default gpio bit setting for the mode.
#define ATOM_GPIO_INDEX_LINE3_ASPECRATIO_GPIO_B   4   //bit 4 in uc480i/uc480p/uc720p/uc1080i, which represend the default gpio bit setting for the mode.


typedef struct _ATOM_COMPONENT_VIDEO_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;
  USHORT             usMask_PinRegisterIndex;
  USHORT             usEN_PinRegisterIndex;
  USHORT             usY_PinRegisterIndex;
  USHORT             usA_PinRegisterIndex;
  UCHAR              ucBitShift;
  UCHAR              ucPinActiveState;  //ucPinActiveState: Bit0=1 active high, =0 active low
  ATOM_DTD_FORMAT    sReserved;         // must be zeroed out
  UCHAR              ucMiscInfo;
  UCHAR              uc480i;
  UCHAR              uc480p;
  UCHAR              uc720p;
  UCHAR              uc1080i;
  UCHAR              ucLetterBoxMode;
  UCHAR              ucReserved[3];
  UCHAR              ucNumOfWbGpioBlocks; //For Component video D-Connector support. If zere, NTSC type connector
  ATOM_GPIO_INFO     aWbGpioStateBlock[MAX_SUPPORTED_CV_STANDARDS];
  ATOM_DTD_FORMAT    aModeTimings[MAX_SUPPORTED_CV_STANDARDS];
}ATOM_COMPONENT_VIDEO_INFO;

//ucTableFormatRevision=2
//ucTableContentRevision=1
typedef struct _ATOM_COMPONENT_VIDEO_INFO_V21
{
  ATOM_COMMON_TABLE_HEADER sHeader;
  UCHAR              ucMiscInfo;
  UCHAR              uc480i;
  UCHAR              uc480p;
  UCHAR              uc720p;
  UCHAR              uc1080i;
  UCHAR              ucReserved;
  UCHAR              ucLetterBoxMode;
  UCHAR              ucNumOfWbGpioBlocks; //For Component video D-Connector support. If zere, NTSC type connector
  ATOM_GPIO_INFO     aWbGpioStateBlock[MAX_SUPPORTED_CV_STANDARDS];
  ATOM_DTD_FORMAT    aModeTimings[MAX_SUPPORTED_CV_STANDARDS];
}ATOM_COMPONENT_VIDEO_INFO_V21;

#define ATOM_COMPONENT_VIDEO_INFO_LAST  ATOM_COMPONENT_VIDEO_INFO_V21

/****************************************************************************/	
// Structure used in object_InfoTable
/****************************************************************************/	
typedef struct _ATOM_OBJECT_HEADER
{ 
  ATOM_COMMON_TABLE_HEADER	sHeader;
  USHORT                    usDeviceSupport;
  USHORT                    usConnectorObjectTableOffset;
  USHORT                    usRouterObjectTableOffset;
  USHORT                    usEncoderObjectTableOffset;
  USHORT                    usProtectionObjectTableOffset; //only available when Protection block is independent.
  USHORT                    usDisplayPathTableOffset;
}ATOM_OBJECT_HEADER;

typedef struct _ATOM_OBJECT_HEADER_V3
{ 
  ATOM_COMMON_TABLE_HEADER	sHeader;
  USHORT                    usDeviceSupport;
  USHORT                    usConnectorObjectTableOffset;
  USHORT                    usRouterObjectTableOffset;
  USHORT                    usEncoderObjectTableOffset;
  USHORT                    usProtectionObjectTableOffset; //only available when Protection block is independent.
  USHORT                    usDisplayPathTableOffset;
  USHORT                    usMiscObjectTableOffset;
}ATOM_OBJECT_HEADER_V3;

typedef struct  _ATOM_DISPLAY_OBJECT_PATH
{
  USHORT    usDeviceTag;                                   //supported device 
  USHORT    usSize;                                        //the size of ATOM_DISPLAY_OBJECT_PATH
  USHORT    usConnObjectId;                                //Connector Object ID 
  USHORT    usGPUObjectId;                                 //GPU ID 
  USHORT    usGraphicObjIds[1];                             //1st Encoder Obj source from GPU to last Graphic Obj destinate to connector.
}ATOM_DISPLAY_OBJECT_PATH;

typedef struct  _ATOM_DISPLAY_EXTERNAL_OBJECT_PATH
{
  USHORT    usDeviceTag;                                   //supported device 
  USHORT    usSize;                                        //the size of ATOM_DISPLAY_OBJECT_PATH
  USHORT    usConnObjectId;                                //Connector Object ID 
  USHORT    usGPUObjectId;                                 //GPU ID 
  USHORT    usGraphicObjIds[2];                            //usGraphicObjIds[0]= GPU internal encoder, usGraphicObjIds[1]= external encoder 
}ATOM_DISPLAY_EXTERNAL_OBJECT_PATH;

typedef struct _ATOM_DISPLAY_OBJECT_PATH_TABLE
{
  UCHAR                           ucNumOfDispPath;
  UCHAR                           ucVersion;
  UCHAR                           ucPadding[2];
  ATOM_DISPLAY_OBJECT_PATH        asDispPath[1];
}ATOM_DISPLAY_OBJECT_PATH_TABLE;


typedef struct _ATOM_OBJECT                                //each object has this structure    
{
  USHORT              usObjectID;
  USHORT              usSrcDstTableOffset;
  USHORT              usRecordOffset;                     //this pointing to a bunch of records defined below
  USHORT              usReserved;
}ATOM_OBJECT;

typedef struct _ATOM_OBJECT_TABLE                         //Above 4 object table offset pointing to a bunch of objects all have this structure     
{
  UCHAR               ucNumberOfObjects;
  UCHAR               ucPadding[3];
  ATOM_OBJECT         asObjects[1];
}ATOM_OBJECT_TABLE;

typedef struct _ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT         //usSrcDstTableOffset pointing to this structure
{
  UCHAR               ucNumberOfSrc;
  USHORT              usSrcObjectID[1];
  UCHAR               ucNumberOfDst;
  USHORT              usDstObjectID[1];
}ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT;


//Two definitions below are for OPM on MXM module designs

#define EXT_HPDPIN_LUTINDEX_0                   0
#define EXT_HPDPIN_LUTINDEX_1                   1
#define EXT_HPDPIN_LUTINDEX_2                   2
#define EXT_HPDPIN_LUTINDEX_3                   3
#define EXT_HPDPIN_LUTINDEX_4                   4
#define EXT_HPDPIN_LUTINDEX_5                   5
#define EXT_HPDPIN_LUTINDEX_6                   6
#define EXT_HPDPIN_LUTINDEX_7                   7
#define MAX_NUMBER_OF_EXT_HPDPIN_LUT_ENTRIES   (EXT_HPDPIN_LUTINDEX_7+1)

#define EXT_AUXDDC_LUTINDEX_0                   0
#define EXT_AUXDDC_LUTINDEX_1                   1
#define EXT_AUXDDC_LUTINDEX_2                   2
#define EXT_AUXDDC_LUTINDEX_3                   3
#define EXT_AUXDDC_LUTINDEX_4                   4
#define EXT_AUXDDC_LUTINDEX_5                   5
#define EXT_AUXDDC_LUTINDEX_6                   6
#define EXT_AUXDDC_LUTINDEX_7                   7
#define MAX_NUMBER_OF_EXT_AUXDDC_LUT_ENTRIES   (EXT_AUXDDC_LUTINDEX_7+1)

//ucChannelMapping are defined as following
//for DP connector, eDP, DP to VGA/LVDS 
//Bit[1:0]: Define which pin connect to DP connector DP_Lane0, =0: source from GPU pin TX0, =1: from GPU pin TX1, =2: from GPU pin TX2, =3 from GPU pin TX3
//Bit[3:2]: Define which pin connect to DP connector DP_Lane1, =0: source from GPU pin TX0, =1: from GPU pin TX1, =2: from GPU pin TX2, =3 from GPU pin TX3
//Bit[5:4]: Define which pin connect to DP connector DP_Lane2, =0: source from GPU pin TX0, =1: from GPU pin TX1, =2: from GPU pin TX2, =3 from GPU pin TX3
//Bit[7:6]: Define which pin connect to DP connector DP_Lane3, =0: source from GPU pin TX0, =1: from GPU pin TX1, =2: from GPU pin TX2, =3 from GPU pin TX3
typedef struct _ATOM_DP_CONN_CHANNEL_MAPPING
{
#if ATOM_BIG_ENDIAN
  UCHAR ucDP_Lane3_Source:2;
  UCHAR ucDP_Lane2_Source:2;
  UCHAR ucDP_Lane1_Source:2;
  UCHAR ucDP_Lane0_Source:2;
#else
  UCHAR ucDP_Lane0_Source:2;
  UCHAR ucDP_Lane1_Source:2;
  UCHAR ucDP_Lane2_Source:2;
  UCHAR ucDP_Lane3_Source:2;
#endif
}ATOM_DP_CONN_CHANNEL_MAPPING;

//for DVI/HDMI, in dual link case, both links have to have same mapping. 
//Bit[1:0]: Define which pin connect to DVI connector data Lane2, =0: source from GPU pin TX0, =1: from GPU pin TX1, =2: from GPU pin TX2, =3 from GPU pin TX3
//Bit[3:2]: Define which pin connect to DVI connector data Lane1, =0: source from GPU pin TX0, =1: from GPU pin TX1, =2: from GPU pin TX2, =3 from GPU pin TX3
//Bit[5:4]: Define which pin connect to DVI connector data Lane0, =0: source from GPU pin TX0, =1: from GPU pin TX1, =2: from GPU pin TX2, =3 from GPU pin TX3
//Bit[7:6]: Define which pin connect to DVI connector clock lane, =0: source from GPU pin TX0, =1: from GPU pin TX1, =2: from GPU pin TX2, =3 from GPU pin TX3
typedef struct _ATOM_DVI_CONN_CHANNEL_MAPPING
{
#if ATOM_BIG_ENDIAN
  UCHAR ucDVI_CLK_Source:2;
  UCHAR ucDVI_DATA0_Source:2;
  UCHAR ucDVI_DATA1_Source:2;
  UCHAR ucDVI_DATA2_Source:2;
#else
  UCHAR ucDVI_DATA2_Source:2;
  UCHAR ucDVI_DATA1_Source:2;
  UCHAR ucDVI_DATA0_Source:2;
  UCHAR ucDVI_CLK_Source:2;
#endif
}ATOM_DVI_CONN_CHANNEL_MAPPING;

typedef struct _EXT_DISPLAY_PATH
{
  USHORT  usDeviceTag;                    //A bit vector to show what devices are supported 
  USHORT  usDeviceACPIEnum;               //16bit device ACPI id. 
  USHORT  usDeviceConnector;              //A physical connector for displays to plug in, using object connector definitions
  UCHAR   ucExtAUXDDCLutIndex;            //An index into external AUX/DDC channel LUT
  UCHAR   ucExtHPDPINLutIndex;            //An index into external HPD pin LUT
  USHORT  usExtEncoderObjId;              //external encoder object id
  union{
    UCHAR   ucChannelMapping;                  // if ucChannelMapping=0, using default one to one mapping
    ATOM_DP_CONN_CHANNEL_MAPPING asDPMapping;
    ATOM_DVI_CONN_CHANNEL_MAPPING asDVIMapping;
  };
  UCHAR   ucChPNInvert;                   // bit vector for up to 8 lanes, =0: P and N is not invert, =1 P and N is inverted
  USHORT  usCaps;
  USHORT  usReserved; 
}EXT_DISPLAY_PATH;
   
#define NUMBER_OF_UCHAR_FOR_GUID          16
#define MAX_NUMBER_OF_EXT_DISPLAY_PATH    7

//usCaps
#define  EXT_DISPLAY_PATH_CAPS__HBR2_DISABLE          0x01
#define  EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN        0x02

typedef  struct _ATOM_EXTERNAL_DISPLAY_CONNECTION_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;
  UCHAR                    ucGuid [NUMBER_OF_UCHAR_FOR_GUID];     // a GUID is a 16 byte long string
  EXT_DISPLAY_PATH         sPath[MAX_NUMBER_OF_EXT_DISPLAY_PATH]; // total of fixed 7 entries.
  UCHAR                    ucChecksum;                            // a simple Checksum of the sum of whole structure equal to 0x0. 
  UCHAR                    uc3DStereoPinId;                       // use for eDP panel
  UCHAR                    ucRemoteDisplayConfig;
  UCHAR                    uceDPToLVDSRxId;
  UCHAR                    ucFixDPVoltageSwing;                   // usCaps[1]=1, this indicate DP_LANE_SET value
  UCHAR                    Reserved[3];                           // for potential expansion
}ATOM_EXTERNAL_DISPLAY_CONNECTION_INFO;

//Related definitions, all records are different but they have a commond header
typedef struct _ATOM_COMMON_RECORD_HEADER
{
  UCHAR               ucRecordType;                      //An emun to indicate the record type
  UCHAR               ucRecordSize;                      //The size of the whole record in byte
}ATOM_COMMON_RECORD_HEADER;


#define ATOM_I2C_RECORD_TYPE                           1         
#define ATOM_HPD_INT_RECORD_TYPE                       2
#define ATOM_OUTPUT_PROTECTION_RECORD_TYPE             3
#define ATOM_CONNECTOR_DEVICE_TAG_RECORD_TYPE          4
#define	ATOM_CONNECTOR_DVI_EXT_INPUT_RECORD_TYPE	     5 //Obsolete, switch to use GPIO_CNTL_RECORD_TYPE
#define ATOM_ENCODER_FPGA_CONTROL_RECORD_TYPE          6 //Obsolete, switch to use GPIO_CNTL_RECORD_TYPE
#define ATOM_CONNECTOR_CVTV_SHARE_DIN_RECORD_TYPE      7
#define ATOM_JTAG_RECORD_TYPE                          8 //Obsolete, switch to use GPIO_CNTL_RECORD_TYPE
#define ATOM_OBJECT_GPIO_CNTL_RECORD_TYPE              9
#define ATOM_ENCODER_DVO_CF_RECORD_TYPE               10
#define ATOM_CONNECTOR_CF_RECORD_TYPE                 11
#define	ATOM_CONNECTOR_HARDCODE_DTD_RECORD_TYPE	      12
#define ATOM_CONNECTOR_PCIE_SUBCONNECTOR_RECORD_TYPE  13
#define ATOM_ROUTER_DDC_PATH_SELECT_RECORD_TYPE	      14
#define ATOM_ROUTER_DATA_CLOCK_PATH_SELECT_RECORD_TYPE	15
#define ATOM_CONNECTOR_HPDPIN_LUT_RECORD_TYPE          16 //This is for the case when connectors are not known to object table
#define ATOM_CONNECTOR_AUXDDC_LUT_RECORD_TYPE          17 //This is for the case when connectors are not known to object table
#define ATOM_OBJECT_LINK_RECORD_TYPE                   18 //Once this record is present under one object, it indicats the oobject is linked to another obj described by the record
#define ATOM_CONNECTOR_REMOTE_CAP_RECORD_TYPE          19
#define ATOM_ENCODER_CAP_RECORD_TYPE                   20
#define ATOM_BRACKET_LAYOUT_RECORD_TYPE                21

//Must be updated when new record type is added,equal to that record definition!
#define ATOM_MAX_OBJECT_RECORD_NUMBER             ATOM_BRACKET_LAYOUT_RECORD_TYPE

typedef struct  _ATOM_I2C_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  ATOM_I2C_ID_CONFIG          sucI2cId; 
  UCHAR                       ucI2CAddr;              //The slave address, it's 0 when the record is attached to connector for DDC
}ATOM_I2C_RECORD;

typedef struct  _ATOM_HPD_INT_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucHPDIntGPIOID;         //Corresponding block in GPIO_PIN_INFO table gives the pin info           
  UCHAR                       ucPlugged_PinState;
}ATOM_HPD_INT_RECORD;


typedef struct  _ATOM_OUTPUT_PROTECTION_RECORD 
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucProtectionFlag;
  UCHAR                       ucReserved;
}ATOM_OUTPUT_PROTECTION_RECORD;

typedef struct  _ATOM_CONNECTOR_DEVICE_TAG
{
  ULONG                       ulACPIDeviceEnum;       //Reserved for now
  USHORT                      usDeviceID;             //This Id is same as "ATOM_DEVICE_XXX_SUPPORT"
  USHORT                      usPadding;
}ATOM_CONNECTOR_DEVICE_TAG;

typedef struct  _ATOM_CONNECTOR_DEVICE_TAG_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucNumberOfDevice;
  UCHAR                       ucReserved;
  ATOM_CONNECTOR_DEVICE_TAG   asDeviceTag[1];         //This Id is same as "ATOM_DEVICE_XXX_SUPPORT", 1 is only for allocation
}ATOM_CONNECTOR_DEVICE_TAG_RECORD;


typedef struct  _ATOM_CONNECTOR_DVI_EXT_INPUT_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR						            ucConfigGPIOID;
  UCHAR						            ucConfigGPIOState;	    //Set to 1 when it's active high to enable external flow in
  UCHAR                       ucFlowinGPIPID;
  UCHAR                       ucExtInGPIPID;
}ATOM_CONNECTOR_DVI_EXT_INPUT_RECORD;

typedef struct  _ATOM_ENCODER_FPGA_CONTROL_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucCTL1GPIO_ID;
  UCHAR                       ucCTL1GPIOState;        //Set to 1 when it's active high
  UCHAR                       ucCTL2GPIO_ID;
  UCHAR                       ucCTL2GPIOState;        //Set to 1 when it's active high
  UCHAR                       ucCTL3GPIO_ID;
  UCHAR                       ucCTL3GPIOState;        //Set to 1 when it's active high
  UCHAR                       ucCTLFPGA_IN_ID;
  UCHAR                       ucPadding[3];
}ATOM_ENCODER_FPGA_CONTROL_RECORD;

typedef struct  _ATOM_CONNECTOR_CVTV_SHARE_DIN_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucGPIOID;               //Corresponding block in GPIO_PIN_INFO table gives the pin info 
  UCHAR                       ucTVActiveState;        //Indicating when the pin==0 or 1 when TV is connected
}ATOM_CONNECTOR_CVTV_SHARE_DIN_RECORD;

typedef struct  _ATOM_JTAG_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucTMSGPIO_ID;
  UCHAR                       ucTMSGPIOState;         //Set to 1 when it's active high
  UCHAR                       ucTCKGPIO_ID;
  UCHAR                       ucTCKGPIOState;         //Set to 1 when it's active high
  UCHAR                       ucTDOGPIO_ID;
  UCHAR                       ucTDOGPIOState;         //Set to 1 when it's active high
  UCHAR                       ucTDIGPIO_ID;
  UCHAR                       ucTDIGPIOState;         //Set to 1 when it's active high
  UCHAR                       ucPadding[2];
}ATOM_JTAG_RECORD;


//The following generic object gpio pin control record type will replace JTAG_RECORD/FPGA_CONTROL_RECORD/DVI_EXT_INPUT_RECORD above gradually
typedef struct _ATOM_GPIO_PIN_CONTROL_PAIR
{
  UCHAR                       ucGPIOID;               // GPIO_ID, find the corresponding ID in GPIO_LUT table
  UCHAR                       ucGPIO_PinState;        // Pin state showing how to set-up the pin
}ATOM_GPIO_PIN_CONTROL_PAIR;

typedef struct  _ATOM_OBJECT_GPIO_CNTL_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucFlags;                // Future expnadibility
  UCHAR                       ucNumberOfPins;         // Number of GPIO pins used to control the object
  ATOM_GPIO_PIN_CONTROL_PAIR  asGpio[1];              // the real gpio pin pair determined by number of pins ucNumberOfPins
}ATOM_OBJECT_GPIO_CNTL_RECORD;

//Definitions for GPIO pin state 
#define GPIO_PIN_TYPE_INPUT             0x00
#define GPIO_PIN_TYPE_OUTPUT            0x10
#define GPIO_PIN_TYPE_HW_CONTROL        0x20

//For GPIO_PIN_TYPE_OUTPUT the following is defined 
#define GPIO_PIN_OUTPUT_STATE_MASK      0x01
#define GPIO_PIN_OUTPUT_STATE_SHIFT     0
#define GPIO_PIN_STATE_ACTIVE_LOW       0x0
#define GPIO_PIN_STATE_ACTIVE_HIGH      0x1

// Indexes to GPIO array in GLSync record 
// GLSync record is for Frame Lock/Gen Lock feature.
#define ATOM_GPIO_INDEX_GLSYNC_REFCLK    0
#define ATOM_GPIO_INDEX_GLSYNC_HSYNC     1
#define ATOM_GPIO_INDEX_GLSYNC_VSYNC     2
#define ATOM_GPIO_INDEX_GLSYNC_SWAP_REQ  3
#define ATOM_GPIO_INDEX_GLSYNC_SWAP_GNT  4
#define ATOM_GPIO_INDEX_GLSYNC_INTERRUPT 5
#define ATOM_GPIO_INDEX_GLSYNC_V_RESET   6
#define ATOM_GPIO_INDEX_GLSYNC_SWAP_CNTL 7
#define ATOM_GPIO_INDEX_GLSYNC_SWAP_SEL  8
#define ATOM_GPIO_INDEX_GLSYNC_MAX       9

typedef struct  _ATOM_ENCODER_DVO_CF_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  ULONG                       ulStrengthControl;      // DVOA strength control for CF
  UCHAR                       ucPadding[2];
}ATOM_ENCODER_DVO_CF_RECORD;

// Bit maps for ATOM_ENCODER_CAP_RECORD.ucEncoderCap
#define ATOM_ENCODER_CAP_RECORD_HBR2                  0x01         // DP1.2 HBR2 is supported by HW encoder
#define ATOM_ENCODER_CAP_RECORD_HBR2_EN               0x02         // DP1.2 HBR2 setting is qualified and HBR2 can be enabled 

typedef struct  _ATOM_ENCODER_CAP_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  union {
    USHORT                    usEncoderCap;         
    struct {
#if ATOM_BIG_ENDIAN
      USHORT                  usReserved:14;        // Bit1-15 may be defined for other capability in future
      USHORT                  usHBR2En:1;           // Bit1 is for DP1.2 HBR2 enable
      USHORT                  usHBR2Cap:1;          // Bit0 is for DP1.2 HBR2 capability. 
#else
      USHORT                  usHBR2Cap:1;          // Bit0 is for DP1.2 HBR2 capability. 
      USHORT                  usHBR2En:1;           // Bit1 is for DP1.2 HBR2 enable
      USHORT                  usReserved:14;        // Bit1-15 may be defined for other capability in future
#endif
    };
  }; 
}ATOM_ENCODER_CAP_RECORD;                             

// value for ATOM_CONNECTOR_CF_RECORD.ucConnectedDvoBundle
#define ATOM_CONNECTOR_CF_RECORD_CONNECTED_UPPER12BITBUNDLEA   1
#define ATOM_CONNECTOR_CF_RECORD_CONNECTED_LOWER12BITBUNDLEB   2

typedef struct  _ATOM_CONNECTOR_CF_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  USHORT                      usMaxPixClk;
  UCHAR                       ucFlowCntlGpioId;
  UCHAR                       ucSwapCntlGpioId;
  UCHAR                       ucConnectedDvoBundle;
  UCHAR                       ucPadding;
}ATOM_CONNECTOR_CF_RECORD;

typedef struct  _ATOM_CONNECTOR_HARDCODE_DTD_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
	ATOM_DTD_FORMAT							asTiming;
}ATOM_CONNECTOR_HARDCODE_DTD_RECORD;

typedef struct _ATOM_CONNECTOR_PCIE_SUBCONNECTOR_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;                //ATOM_CONNECTOR_PCIE_SUBCONNECTOR_RECORD_TYPE
  UCHAR                       ucSubConnectorType;     //CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D|X_ID_DUAL_LINK_DVI_D|HDMI_TYPE_A
  UCHAR                       ucReserved;
}ATOM_CONNECTOR_PCIE_SUBCONNECTOR_RECORD;


typedef struct _ATOM_ROUTER_DDC_PATH_SELECT_RECORD
{
	ATOM_COMMON_RECORD_HEADER   sheader;                
	UCHAR												ucMuxType;							//decide the number of ucMuxState, =0, no pin state, =1: single state with complement, >1: multiple state
	UCHAR												ucMuxControlPin;
	UCHAR												ucMuxState[2];					//for alligment purpose
}ATOM_ROUTER_DDC_PATH_SELECT_RECORD;

typedef struct _ATOM_ROUTER_DATA_CLOCK_PATH_SELECT_RECORD
{
	ATOM_COMMON_RECORD_HEADER   sheader;                
	UCHAR												ucMuxType;
	UCHAR												ucMuxControlPin;
	UCHAR												ucMuxState[2];					//for alligment purpose
}ATOM_ROUTER_DATA_CLOCK_PATH_SELECT_RECORD;

// define ucMuxType
#define ATOM_ROUTER_MUX_PIN_STATE_MASK								0x0f
#define ATOM_ROUTER_MUX_PIN_SINGLE_STATE_COMPLEMENT		0x01

typedef struct _ATOM_CONNECTOR_HPDPIN_LUT_RECORD     //record for ATOM_CONNECTOR_HPDPIN_LUT_RECORD_TYPE
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucHPDPINMap[MAX_NUMBER_OF_EXT_HPDPIN_LUT_ENTRIES];  //An fixed size array which maps external pins to internal GPIO_PIN_INFO table 
}ATOM_CONNECTOR_HPDPIN_LUT_RECORD;

typedef struct _ATOM_CONNECTOR_AUXDDC_LUT_RECORD  //record for ATOM_CONNECTOR_AUXDDC_LUT_RECORD_TYPE
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  ATOM_I2C_ID_CONFIG          ucAUXDDCMap[MAX_NUMBER_OF_EXT_AUXDDC_LUT_ENTRIES];  //An fixed size array which maps external pins to internal DDC ID
}ATOM_CONNECTOR_AUXDDC_LUT_RECORD;

typedef struct _ATOM_OBJECT_LINK_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  USHORT                      usObjectID;         //could be connector, encorder or other object in object.h
}ATOM_OBJECT_LINK_RECORD;

typedef struct _ATOM_CONNECTOR_REMOTE_CAP_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  USHORT                      usReserved;
}ATOM_CONNECTOR_REMOTE_CAP_RECORD;

typedef struct  _ATOM_CONNECTOR_LAYOUT_INFO
{
   USHORT usConnectorObjectId;
   UCHAR  ucConnectorType;
   UCHAR  ucPosition;
}ATOM_CONNECTOR_LAYOUT_INFO;

// define ATOM_CONNECTOR_LAYOUT_INFO.ucConnectorType to describe the display connector size
#define CONNECTOR_TYPE_DVI_D                 1
#define CONNECTOR_TYPE_DVI_I                 2
#define CONNECTOR_TYPE_VGA                   3
#define CONNECTOR_TYPE_HDMI                  4
#define CONNECTOR_TYPE_DISPLAY_PORT          5
#define CONNECTOR_TYPE_MINI_DISPLAY_PORT     6

typedef struct  _ATOM_BRACKET_LAYOUT_RECORD
{
  ATOM_COMMON_RECORD_HEADER   sheader;
  UCHAR                       ucLength;
  UCHAR                       ucWidth;
  UCHAR                       ucConnNum;
  UCHAR                       ucReserved;
  ATOM_CONNECTOR_LAYOUT_INFO  asConnInfo[1];
}ATOM_BRACKET_LAYOUT_RECORD;

/****************************************************************************/	
// ASIC voltage data table
/****************************************************************************/	
typedef struct  _ATOM_VOLTAGE_INFO_HEADER
{
   USHORT   usVDDCBaseLevel;                //In number of 50mv unit
   USHORT   usReserved;                     //For possible extension table offset
   UCHAR    ucNumOfVoltageEntries;
   UCHAR    ucBytesPerVoltageEntry;
   UCHAR    ucVoltageStep;                  //Indicating in how many mv increament is one step, 0.5mv unit
   UCHAR    ucDefaultVoltageEntry;
   UCHAR    ucVoltageControlI2cLine;
   UCHAR    ucVoltageControlAddress;
   UCHAR    ucVoltageControlOffset;
}ATOM_VOLTAGE_INFO_HEADER;

typedef struct  _ATOM_VOLTAGE_INFO
{
   ATOM_COMMON_TABLE_HEADER	sHeader; 
   ATOM_VOLTAGE_INFO_HEADER viHeader;
   UCHAR    ucVoltageEntries[64];            //64 is for allocation, the actual number of entry is present at ucNumOfVoltageEntries*ucBytesPerVoltageEntry
}ATOM_VOLTAGE_INFO;


typedef struct  _ATOM_VOLTAGE_FORMULA
{
   USHORT   usVoltageBaseLevel;             // In number of 1mv unit
   USHORT   usVoltageStep;                  // Indicating in how many mv increament is one step, 1mv unit
	 UCHAR		ucNumOfVoltageEntries;					// Number of Voltage Entry, which indicate max Voltage
	 UCHAR		ucFlag;													// bit0=0 :step is 1mv =1 0.5mv
	 UCHAR		ucBaseVID;											// if there is no lookup table, VID= BaseVID + ( Vol - BaseLevle ) /VoltageStep
	 UCHAR		ucReserved;
	 UCHAR		ucVIDAdjustEntries[32];					// 32 is for allocation, the actual number of entry is present at ucNumOfVoltageEntries
}ATOM_VOLTAGE_FORMULA;

typedef struct  _VOLTAGE_LUT_ENTRY
{
	 USHORT		usVoltageCode;									// The Voltage ID, either GPIO or I2C code
	 USHORT		usVoltageValue;									// The corresponding Voltage Value, in mV
}VOLTAGE_LUT_ENTRY;

typedef struct  _ATOM_VOLTAGE_FORMULA_V2
{
	 UCHAR		ucNumOfVoltageEntries;					// Number of Voltage Entry, which indicate max Voltage
	 UCHAR		ucReserved[3];
	 VOLTAGE_LUT_ENTRY asVIDAdjustEntries[32];// 32 is for allocation, the actual number of entries is in ucNumOfVoltageEntries
}ATOM_VOLTAGE_FORMULA_V2;

typedef struct _ATOM_VOLTAGE_CONTROL
{
	UCHAR		 ucVoltageControlId;							//Indicate it is controlled by I2C or GPIO or HW state machine		
  UCHAR    ucVoltageControlI2cLine;
  UCHAR    ucVoltageControlAddress;
  UCHAR    ucVoltageControlOffset;	 	
  USHORT   usGpioPin_AIndex;								//GPIO_PAD register index
  UCHAR    ucGpioPinBitShift[9];						//at most 8 pin support 255 VIDs, termintate with 0xff
	UCHAR		 ucReserved;
}ATOM_VOLTAGE_CONTROL;

// Define ucVoltageControlId
#define	VOLTAGE_CONTROLLED_BY_HW							0x00
#define	VOLTAGE_CONTROLLED_BY_I2C_MASK				0x7F
#define	VOLTAGE_CONTROLLED_BY_GPIO						0x80
#define	VOLTAGE_CONTROL_ID_LM64								0x01									//I2C control, used for R5xx Core Voltage
#define	VOLTAGE_CONTROL_ID_DAC								0x02									//I2C control, used for R5xx/R6xx MVDDC,MVDDQ or VDDCI
#define	VOLTAGE_CONTROL_ID_VT116xM						0x03									//I2C control, used for R6xx Core Voltage
#define VOLTAGE_CONTROL_ID_DS4402							0x04									
#define VOLTAGE_CONTROL_ID_UP6266 						0x05									
#define VOLTAGE_CONTROL_ID_SCORPIO						0x06
#define	VOLTAGE_CONTROL_ID_VT1556M						0x07									
#define	VOLTAGE_CONTROL_ID_CHL822x						0x08									
#define	VOLTAGE_CONTROL_ID_VT1586M						0x09
#define VOLTAGE_CONTROL_ID_UP1637 						0x0A
#define	VOLTAGE_CONTROL_ID_CHL8214            0x0B
#define	VOLTAGE_CONTROL_ID_UP1801             0x0C
#define	VOLTAGE_CONTROL_ID_ST6788A            0x0D
#define VOLTAGE_CONTROL_ID_CHLIR3564SVI2      0x0E
#define VOLTAGE_CONTROL_ID_AD527x      	      0x0F
#define VOLTAGE_CONTROL_ID_NCP81022    	      0x10
#define VOLTAGE_CONTROL_ID_LTC2635			  0x11

typedef struct  _ATOM_VOLTAGE_OBJECT
{
 	 UCHAR		ucVoltageType;									//Indicate Voltage Source: VDDC, MVDDC, MVDDQ or MVDDCI	 
	 UCHAR		ucSize;													//Size of Object	
	 ATOM_VOLTAGE_CONTROL			asControl;			//describ how to control 	 
 	 ATOM_VOLTAGE_FORMULA			asFormula;			//Indicate How to convert real Voltage to VID 
}ATOM_VOLTAGE_OBJECT;

typedef struct  _ATOM_VOLTAGE_OBJECT_V2
{
 	 UCHAR		ucVoltageType;									//Indicate Voltage Source: VDDC, MVDDC, MVDDQ or MVDDCI	 
	 UCHAR		ucSize;													//Size of Object	
	 ATOM_VOLTAGE_CONTROL			asControl;			//describ how to control 	 
 	 ATOM_VOLTAGE_FORMULA_V2	asFormula;			//Indicate How to convert real Voltage to VID 
}ATOM_VOLTAGE_OBJECT_V2;

typedef struct  _ATOM_VOLTAGE_OBJECT_INFO
{
   ATOM_COMMON_TABLE_HEADER	sHeader; 
	 ATOM_VOLTAGE_OBJECT			asVoltageObj[3];	//Info for Voltage control	  	 
}ATOM_VOLTAGE_OBJECT_INFO;

typedef struct  _ATOM_VOLTAGE_OBJECT_INFO_V2
{
   ATOM_COMMON_TABLE_HEADER	sHeader; 
	 ATOM_VOLTAGE_OBJECT_V2			asVoltageObj[3];	//Info for Voltage control	  	 
}ATOM_VOLTAGE_OBJECT_INFO_V2;

typedef struct  _ATOM_LEAKID_VOLTAGE
{
	UCHAR		ucLeakageId;
	UCHAR		ucReserved;
	USHORT	usVoltage;
}ATOM_LEAKID_VOLTAGE;

typedef struct _ATOM_VOLTAGE_OBJECT_HEADER_V3{
 	 UCHAR		ucVoltageType;									//Indicate Voltage Source: VDDC, MVDDC, MVDDQ or MVDDCI	 
   UCHAR		ucVoltageMode;							    //Indicate voltage control mode: Init/Set/Leakage/Set phase 
	 USHORT		usSize;													//Size of Object	
}ATOM_VOLTAGE_OBJECT_HEADER_V3;

// ATOM_VOLTAGE_OBJECT_HEADER_V3.ucVoltageMode
#define VOLTAGE_OBJ_GPIO_LUT                 0        //VOLTAGE and GPIO Lookup table ->ATOM_GPIO_VOLTAGE_OBJECT_V3
#define VOLTAGE_OBJ_VR_I2C_INIT_SEQ          3        //VOLTAGE REGULATOR INIT sequece through I2C -> ATOM_I2C_VOLTAGE_OBJECT_V3
#define VOLTAGE_OBJ_PHASE_LUT                4        //Set Vregulator Phase lookup table ->ATOM_GPIO_VOLTAGE_OBJECT_V3
#define VOLTAGE_OBJ_SVID2                    7        //Indicate voltage control by SVID2 ->ATOM_SVID2_VOLTAGE_OBJECT_V3
#define VOLTAGE_OBJ_EVV                      8 
#define VOLTAGE_OBJ_PWRBOOST_LEAKAGE_LUT     0x10     //Powerboost Voltage and LeakageId lookup table->ATOM_LEAKAGE_VOLTAGE_OBJECT_V3
#define VOLTAGE_OBJ_HIGH_STATE_LEAKAGE_LUT   0x11     //High voltage state Voltage and LeakageId lookup table->ATOM_LEAKAGE_VOLTAGE_OBJECT_V3
#define VOLTAGE_OBJ_HIGH1_STATE_LEAKAGE_LUT  0x12     //High1 voltage state Voltage and LeakageId lookup table->ATOM_LEAKAGE_VOLTAGE_OBJECT_V3

typedef struct  _VOLTAGE_LUT_ENTRY_V2
{
	 ULONG		ulVoltageId;									  // The Voltage ID which is used to program GPIO register
	 USHORT		usVoltageValue;									// The corresponding Voltage Value, in mV
}VOLTAGE_LUT_ENTRY_V2;

typedef struct  _LEAKAGE_VOLTAGE_LUT_ENTRY_V2
{
  USHORT	usVoltageLevel; 							  // The Voltage ID which is used to program GPIO register
  USHORT  usVoltageId;                    
	USHORT	usLeakageId;									  // The corresponding Voltage Value, in mV
}LEAKAGE_VOLTAGE_LUT_ENTRY_V2;

typedef struct  _ATOM_I2C_VOLTAGE_OBJECT_V3
{
   ATOM_VOLTAGE_OBJECT_HEADER_V3 sHeader;    // voltage mode = VOLTAGE_OBJ_VR_I2C_INIT_SEQ
   UCHAR	ucVoltageRegulatorId;					  //Indicate Voltage Regulator Id
   UCHAR    ucVoltageControlI2cLine;
   UCHAR    ucVoltageControlAddress;
   UCHAR    ucVoltageControlOffset;	 	
   ULONG    ulReserved;
   VOLTAGE_LUT_ENTRY asVolI2cLut[1];        // end with 0xff
}ATOM_I2C_VOLTAGE_OBJECT_V3;

// ATOM_I2C_VOLTAGE_OBJECT_V3.ucVoltageControlFlag
#define VOLTAGE_DATA_ONE_BYTE                0
#define VOLTAGE_DATA_TWO_BYTE                1

typedef struct  _ATOM_GPIO_VOLTAGE_OBJECT_V3
{
   ATOM_VOLTAGE_OBJECT_HEADER_V3 sHeader;   // voltage mode = VOLTAGE_OBJ_GPIO_LUT or VOLTAGE_OBJ_PHASE_LUT
   UCHAR    ucVoltageGpioCntlId;         // default is 0 which indicate control through CG VID mode 
   UCHAR    ucGpioEntryNum;              // indiate the entry numbers of Votlage/Gpio value Look up table
   UCHAR    ucPhaseDelay;                // phase delay in unit of micro second
   UCHAR    ucReserved;   
   ULONG    ulGpioMaskVal;               // GPIO Mask value
   VOLTAGE_LUT_ENTRY_V2 asVolGpioLut[1];   
}ATOM_GPIO_VOLTAGE_OBJECT_V3;

typedef struct  _ATOM_LEAKAGE_VOLTAGE_OBJECT_V3
{
   ATOM_VOLTAGE_OBJECT_HEADER_V3 sHeader;    // voltage mode = 0x10/0x11/0x12
   UCHAR    ucLeakageCntlId;             // default is 0
   UCHAR    ucLeakageEntryNum;           // indicate the entry number of LeakageId/Voltage Lut table
   UCHAR    ucReserved[2];               
   ULONG    ulMaxVoltageLevel;
   LEAKAGE_VOLTAGE_LUT_ENTRY_V2 asLeakageIdLut[1];   
}ATOM_LEAKAGE_VOLTAGE_OBJECT_V3;


typedef struct  _ATOM_SVID2_VOLTAGE_OBJECT_V3
{
   ATOM_VOLTAGE_OBJECT_HEADER_V3 sHeader;    // voltage mode = VOLTAGE_OBJ_SVID2
// 14:7  PSI0_VID
// 6  PSI0_EN
// 5  PSI1
// 4:2  load line slope trim. 
// 1:0  offset trim, 
   USHORT   usLoadLine_PSI;    
// GPU GPIO pin Id to SVID2 regulator VRHot pin. possible value 0~31. 0 means GPIO0, 31 means GPIO31
   UCHAR    ucSVDGpioId;     //0~31 indicate GPIO0~31
   UCHAR    ucSVCGpioId;     //0~31 indicate GPIO0~31
   ULONG    ulReserved;
}ATOM_SVID2_VOLTAGE_OBJECT_V3;

typedef union _ATOM_VOLTAGE_OBJECT_V3{
  ATOM_GPIO_VOLTAGE_OBJECT_V3 asGpioVoltageObj;
  ATOM_I2C_VOLTAGE_OBJECT_V3 asI2cVoltageObj;
  ATOM_LEAKAGE_VOLTAGE_OBJECT_V3 asLeakageObj;
  ATOM_SVID2_VOLTAGE_OBJECT_V3 asSVID2Obj;
}ATOM_VOLTAGE_OBJECT_V3;

typedef struct  _ATOM_VOLTAGE_OBJECT_INFO_V3_1
{
   ATOM_COMMON_TABLE_HEADER	sHeader; 
	 ATOM_VOLTAGE_OBJECT_V3			asVoltageObj[3];	//Info for Voltage control	  	 
}ATOM_VOLTAGE_OBJECT_INFO_V3_1;

typedef struct  _ATOM_ASIC_PROFILE_VOLTAGE
{
	UCHAR		ucProfileId;
	UCHAR		ucReserved;
	USHORT	usSize;
	USHORT	usEfuseSpareStartAddr;
	USHORT	usFuseIndex[8];												//from LSB to MSB, Max 8bit,end of 0xffff if less than 8 efuse id, 
	ATOM_LEAKID_VOLTAGE					asLeakVol[2];			//Leakid and relatd voltage
}ATOM_ASIC_PROFILE_VOLTAGE;

//ucProfileId
#define	ATOM_ASIC_PROFILE_ID_EFUSE_VOLTAGE			1		
#define	ATOM_ASIC_PROFILE_ID_EFUSE_PERFORMANCE_VOLTAGE			1
#define	ATOM_ASIC_PROFILE_ID_EFUSE_THERMAL_VOLTAGE					2

typedef struct  _ATOM_ASIC_PROFILING_INFO
{
  ATOM_COMMON_TABLE_HEADER			asHeader; 
	ATOM_ASIC_PROFILE_VOLTAGE			asVoltage;
}ATOM_ASIC_PROFILING_INFO;

typedef struct  _ATOM_ASIC_PROFILING_INFO_V2_1
{
  ATOM_COMMON_TABLE_HEADER			asHeader; 
  UCHAR  ucLeakageBinNum;                // indicate the entry number of LeakageId/Voltage Lut table
  USHORT usLeakageBinArrayOffset;        // offset of USHORT Leakage Bin list array ( from lower LeakageId to higher) 

  UCHAR  ucElbVDDC_Num;               
  USHORT usElbVDDC_IdArrayOffset;        // offset of USHORT virtual VDDC voltage id ( 0xff01~0xff08 )
  USHORT usElbVDDC_LevelArrayOffset;     // offset of 2 dimension voltage level USHORT array

  UCHAR  ucElbVDDCI_Num;
  USHORT usElbVDDCI_IdArrayOffset;       // offset of USHORT virtual VDDCI voltage id ( 0xff01~0xff08 )
  USHORT usElbVDDCI_LevelArrayOffset;    // offset of 2 dimension voltage level USHORT array
}ATOM_ASIC_PROFILING_INFO_V2_1;

typedef struct  _ATOM_ASIC_PROFILING_INFO_V3_1
{
  ATOM_COMMON_TABLE_HEADER         asHeader; 
  ULONG  ulEvvDerateTdp;
  ULONG  ulEvvDerateTdc;
  ULONG  ulBoardCoreTemp;
  ULONG  ulMaxVddc;
  ULONG  ulMinVddc;
  ULONG  ulLoadLineSlop;
  ULONG  ulLeakageTemp;
  ULONG  ulLeakageVoltage;
  ULONG  ulCACmEncodeRange;
  ULONG  ulCACmEncodeAverage;
  ULONG  ulCACbEncodeRange;
  ULONG  ulCACbEncodeAverage;
  ULONG  ulKt_bEncodeRange;
  ULONG  ulKt_bEncodeAverage;
  ULONG  ulKv_mEncodeRange;
  ULONG  ulKv_mEncodeAverage;
  ULONG  ulKv_bEncodeRange;
  ULONG  ulKv_bEncodeAverage;
  ULONG  ulLkgEncodeLn_MaxDivMin;
  ULONG  ulLkgEncodeMin;
  ULONG  ulEfuseLogisticAlpha;
  USHORT usPowerDpm0;
  USHORT usCurrentDpm0;
  USHORT usPowerDpm1;
  USHORT usCurrentDpm1;
  USHORT usPowerDpm2;
  USHORT usCurrentDpm2;
  USHORT usPowerDpm3;
  USHORT usCurrentDpm3;
  USHORT usPowerDpm4;
  USHORT usCurrentDpm4;
  USHORT usPowerDpm5;
  USHORT usCurrentDpm5;
  USHORT usPowerDpm6;
  USHORT usCurrentDpm6;
  USHORT usPowerDpm7;
  USHORT usCurrentDpm7;
}ATOM_ASIC_PROFILING_INFO_V3_1;


typedef struct _ATOM_POWER_SOURCE_OBJECT
{
	UCHAR	ucPwrSrcId;													// Power source
	UCHAR	ucPwrSensorType;										// GPIO, I2C or none
	UCHAR	ucPwrSensId;											  // if GPIO detect, it is GPIO id,  if I2C detect, it is I2C id
	UCHAR	ucPwrSensSlaveAddr;									// Slave address if I2C detect
	UCHAR ucPwrSensRegIndex;									// I2C register Index if I2C detect
	UCHAR ucPwrSensRegBitMask;								// detect which bit is used if I2C detect
	UCHAR	ucPwrSensActiveState;								// high active or low active
	UCHAR	ucReserve[3];												// reserve		
	USHORT usSensPwr;													// in unit of watt
}ATOM_POWER_SOURCE_OBJECT;

typedef struct _ATOM_POWER_SOURCE_INFO
{
		ATOM_COMMON_TABLE_HEADER		asHeader;
		UCHAR												asPwrbehave[16];
		ATOM_POWER_SOURCE_OBJECT		asPwrObj[1];
}ATOM_POWER_SOURCE_INFO;


//Define ucPwrSrcId
#define POWERSOURCE_PCIE_ID1						0x00
#define POWERSOURCE_6PIN_CONNECTOR_ID1	0x01
#define POWERSOURCE_8PIN_CONNECTOR_ID1	0x02
#define POWERSOURCE_6PIN_CONNECTOR_ID2	0x04
#define POWERSOURCE_8PIN_CONNECTOR_ID2	0x08

//define ucPwrSensorId
#define POWER_SENSOR_ALWAYS							0x00
#define POWER_SENSOR_GPIO								0x01
#define POWER_SENSOR_I2C								0x02

typedef struct _ATOM_CLK_VOLT_CAPABILITY
{
  ULONG      ulVoltageIndex;                      // The Voltage Index indicated by FUSE, same voltage index shared with SCLK DPM fuse table        
  ULONG      ulMaximumSupportedCLK;               // Maximum clock supported with specified voltage index, unit in 10kHz
}ATOM_CLK_VOLT_CAPABILITY;

typedef struct _ATOM_AVAILABLE_SCLK_LIST
{
  ULONG      ulSupportedSCLK;               // Maximum clock supported with specified voltage index,  unit in 10kHz
  USHORT     usVoltageIndex;                // The Voltage Index indicated by FUSE for specified SCLK  
  USHORT     usVoltageID;                   // The Voltage ID indicated by FUSE for specified SCLK 
}ATOM_AVAILABLE_SCLK_LIST;

// ATOM_INTEGRATED_SYSTEM_INFO_V6 ulSystemConfig cap definition
#define ATOM_IGP_INFO_V6_SYSTEM_CONFIG__PCIE_POWER_GATING_ENABLE             1       // refer to ulSystemConfig bit[0]

// this IntegrateSystemInfoTable is used for Liano/Ontario APU
typedef struct _ATOM_INTEGRATED_SYSTEM_INFO_V6
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  ULONG  ulBootUpEngineClock;
  ULONG  ulDentistVCOFreq;          
  ULONG  ulBootUpUMAClock;          
  ATOM_CLK_VOLT_CAPABILITY   sDISPCLK_Voltage[4];            
  ULONG  ulBootUpReqDisplayVector;
  ULONG  ulOtherDisplayMisc;
  ULONG  ulGPUCapInfo;
  ULONG  ulSB_MMIO_Base_Addr;
  USHORT usRequestedPWMFreqInHz;
  UCHAR  ucHtcTmpLmt;   
  UCHAR  ucHtcHystLmt;
  ULONG  ulMinEngineClock;           
  ULONG  ulSystemConfig;            
  ULONG  ulCPUCapInfo;              
  USHORT usNBP0Voltage;               
  USHORT usNBP1Voltage;
  USHORT usBootUpNBVoltage;                       
  USHORT usExtDispConnInfoOffset;
  USHORT usPanelRefreshRateRange;     
  UCHAR  ucMemoryType;  
  UCHAR  ucUMAChannelNumber;
  ULONG  ulCSR_M3_ARB_CNTL_DEFAULT[10];  
  ULONG  ulCSR_M3_ARB_CNTL_UVD[10]; 
  ULONG  ulCSR_M3_ARB_CNTL_FS3D[10];
  ATOM_AVAILABLE_SCLK_LIST   sAvail_SCLK[5];
  ULONG  ulGMCRestoreResetTime;
  ULONG  ulMinimumNClk;
  ULONG  ulIdleNClk;
  ULONG  ulDDR_DLL_PowerUpTime;
  ULONG  ulDDR_PLL_PowerUpTime;
  USHORT usPCIEClkSSPercentage;
  USHORT usPCIEClkSSType;
  USHORT usLvdsSSPercentage;
  USHORT usLvdsSSpreadRateIn10Hz;
  USHORT usHDMISSPercentage;
  USHORT usHDMISSpreadRateIn10Hz;
  USHORT usDVISSPercentage;
  USHORT usDVISSpreadRateIn10Hz;
  ULONG  SclkDpmBoostMargin;
  ULONG  SclkDpmThrottleMargin;
  USHORT SclkDpmTdpLimitPG; 
  USHORT SclkDpmTdpLimitBoost;
  ULONG  ulBoostEngineCLock;
  UCHAR  ulBoostVid_2bit;  
  UCHAR  EnableBoost;
  USHORT GnbTdpLimit;
  USHORT usMaxLVDSPclkFreqInSingleLink;
  UCHAR  ucLvdsMisc;
  UCHAR  ucLVDSReserved;
  ULONG  ulReserved3[15]; 
  ATOM_EXTERNAL_DISPLAY_CONNECTION_INFO sExtDispConnInfo;   
}ATOM_INTEGRATED_SYSTEM_INFO_V6;   

// ulGPUCapInfo
#define INTEGRATED_SYSTEM_INFO_V6_GPUCAPINFO__TMDSHDMI_COHERENT_SINGLEPLL_MODE       0x01
#define INTEGRATED_SYSTEM_INFO_V6_GPUCAPINFO__DISABLE_AUX_HW_MODE_DETECTION          0x08

//ucLVDSMisc:                   
#define SYS_INFO_LVDSMISC__888_FPDI_MODE                                             0x01
#define SYS_INFO_LVDSMISC__DL_CH_SWAP                                                0x02
#define SYS_INFO_LVDSMISC__888_BPC                                                   0x04
#define SYS_INFO_LVDSMISC__OVERRIDE_EN                                               0x08
#define SYS_INFO_LVDSMISC__BLON_ACTIVE_LOW                                           0x10
// new since Trinity
#define SYS_INFO_LVDSMISC__TRAVIS_LVDS_VOL_OVERRIDE_EN                               0x20

// not used any more
#define SYS_INFO_LVDSMISC__VSYNC_ACTIVE_LOW                                          0x04
#define SYS_INFO_LVDSMISC__HSYNC_ACTIVE_LOW                                          0x08

/**********************************************************************************************************************
  ATOM_INTEGRATED_SYSTEM_INFO_V6 Description
ulBootUpEngineClock:              VBIOS bootup Engine clock frequency, in 10kHz unit. if it is equal 0, then VBIOS use pre-defined bootup engine clock
ulDentistVCOFreq:                 Dentist VCO clock in 10kHz unit. 
ulBootUpUMAClock:                 System memory boot up clock frequency in 10Khz unit. 
sDISPCLK_Voltage:                 Report Display clock voltage requirement.
 
ulBootUpReqDisplayVector:         VBIOS boot up display IDs, following are supported devices in Liano/Ontaio projects:
                                  ATOM_DEVICE_CRT1_SUPPORT                  0x0001
                                  ATOM_DEVICE_CRT2_SUPPORT                  0x0010
                                  ATOM_DEVICE_DFP1_SUPPORT                  0x0008 
                                  ATOM_DEVICE_DFP6_SUPPORT                  0x0040 
                                  ATOM_DEVICE_DFP2_SUPPORT                  0x0080       
                                  ATOM_DEVICE_DFP3_SUPPORT                  0x0200       
                                  ATOM_DEVICE_DFP4_SUPPORT                  0x0400        
                                  ATOM_DEVICE_DFP5_SUPPORT                  0x0800
                                  ATOM_DEVICE_LCD1_SUPPORT                  0x0002
ulOtherDisplayMisc:      	        Other display related flags, not defined yet. 
ulGPUCapInfo:                     bit[0]=0: TMDS/HDMI Coherent Mode use cascade PLL mode.
                                        =1: TMDS/HDMI Coherent Mode use signel PLL mode.
                                  bit[3]=0: Enable HW AUX mode detection logic
                                        =1: Disable HW AUX mode dettion logic
ulSB_MMIO_Base_Addr:              Physical Base address to SB MMIO space. Driver needs to initialize it for SMU usage.

usRequestedPWMFreqInHz:           When it's set to 0x0 by SBIOS: the LCD BackLight is not controlled by GPU(SW). 
                                  Any attempt to change BL using VBIOS function or enable VariBri from PP table is not effective since ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==0;
                                  
                                  When it's set to a non-zero frequency, the BackLight is controlled by GPU (SW) in one of two ways below:
                                  1. SW uses the GPU BL PWM output to control the BL, in chis case, this non-zero frequency determines what freq GPU should use;
                                  VBIOS will set up proper PWM frequency and ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==1,as the result,
                                  Changing BL using VBIOS function is functional in both driver and non-driver present environment; 
                                  and enabling VariBri under the driver environment from PP table is optional.

                                  2. SW uses other means to control BL (like DPCD),this non-zero frequency serves as a flag only indicating
                                  that BL control from GPU is expected.
                                  VBIOS will NOT set up PWM frequency but make ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==1
                                  Changing BL using VBIOS function could be functional in both driver and non-driver present environment,but
                                  it's per platform 
                                  and enabling VariBri under the driver environment from PP table is optional.

ucHtcTmpLmt:                      Refer to D18F3x64 bit[22:16], HtcTmpLmt. 
                                  Threshold on value to enter HTC_active state.
ucHtcHystLmt:                     Refer to D18F3x64 bit[27:24], HtcHystLmt. 
                                  To calculate threshold off value to exit HTC_active state, which is Threshold on vlaue minus ucHtcHystLmt.
ulMinEngineClock:                 Minimum SCLK allowed in 10kHz unit. This is calculated based on WRCK Fuse settings.
ulSystemConfig:                   Bit[0]=0: PCIE Power Gating Disabled 
                                        =1: PCIE Power Gating Enabled
                                  Bit[1]=0: DDR-DLL shut-down feature disabled.
                                         1: DDR-DLL shut-down feature enabled.
                                  Bit[2]=0: DDR-PLL Power down feature disabled.
                                         1: DDR-PLL Power down feature enabled.                                 
ulCPUCapInfo:                     TBD
usNBP0Voltage:                    VID for voltage on NB P0 State
usNBP1Voltage:                    VID for voltage on NB P1 State  
usBootUpNBVoltage:                Voltage Index of GNB voltage configured by SBIOS, which is suffcient to support VBIOS DISPCLK requirement.
usExtDispConnInfoOffset:          Offset to sExtDispConnInfo inside the structure
usPanelRefreshRateRange:          Bit vector for LCD supported refresh rate range. If DRR is requestd by the platform, at least two bits need to be set
                                  to indicate a range.
                                  SUPPORTED_LCD_REFRESHRATE_30Hz          0x0004
                                  SUPPORTED_LCD_REFRESHRATE_40Hz          0x0008
                                  SUPPORTED_LCD_REFRESHRATE_50Hz          0x0010
                                  SUPPORTED_LCD_REFRESHRATE_60Hz          0x0020
ucMemoryType:                     [3:0]=1:DDR1;=2:DDR2;=3:DDR3.[7:4] is reserved.
ucUMAChannelNumber:      	        System memory channel numbers. 
ulCSR_M3_ARB_CNTL_DEFAULT[10]:    Arrays with values for CSR M3 arbiter for default
ulCSR_M3_ARB_CNTL_UVD[10]:        Arrays with values for CSR M3 arbiter for UVD playback.
ulCSR_M3_ARB_CNTL_FS3D[10]:       Arrays with values for CSR M3 arbiter for Full Screen 3D applications.
sAvail_SCLK[5]:                   Arrays to provide availabe list of SLCK and corresponding voltage, order from low to high  
ulGMCRestoreResetTime:            GMC power restore and GMC reset time to calculate data reconnection latency. Unit in ns. 
ulMinimumNClk:                    Minimum NCLK speed among all NB-Pstates to calcualte data reconnection latency. Unit in 10kHz. 
ulIdleNClk:                       NCLK speed while memory runs in self-refresh state. Unit in 10kHz.
ulDDR_DLL_PowerUpTime:            DDR PHY DLL power up time. Unit in ns.
ulDDR_PLL_PowerUpTime:            DDR PHY PLL power up time. Unit in ns.
usPCIEClkSSPercentage:            PCIE Clock Spred Spectrum Percentage in unit 0.01%; 100 mean 1%.
usPCIEClkSSType:                  PCIE Clock Spred Spectrum Type. 0 for Down spread(default); 1 for Center spread.
usLvdsSSPercentage:               LVDS panel ( not include eDP ) Spread Spectrum Percentage in unit of 0.01%, =0, use VBIOS default setting. 
usLvdsSSpreadRateIn10Hz:          LVDS panel ( not include eDP ) Spread Spectrum frequency in unit of 10Hz, =0, use VBIOS default setting. 
usHDMISSPercentage:               HDMI Spread Spectrum Percentage in unit 0.01%; 100 mean 1%,  =0, use VBIOS default setting. 
usHDMISSpreadRateIn10Hz:          HDMI Spread Spectrum frequency in unit of 10Hz,  =0, use VBIOS default setting. 
usDVISSPercentage:                DVI Spread Spectrum Percentage in unit 0.01%; 100 mean 1%,  =0, use VBIOS default setting. 
usDVISSpreadRateIn10Hz:           DVI Spread Spectrum frequency in unit of 10Hz,  =0, use VBIOS default setting. 
usMaxLVDSPclkFreqInSingleLink:    Max pixel clock LVDS panel single link, if=0 means VBIOS use default threhold, right now it is 85Mhz
ucLVDSMisc:                       [bit0] LVDS 888bit panel mode =0: LVDS 888 panel in LDI mode, =1: LVDS 888 panel in FPDI mode
                                  [bit1] LVDS panel lower and upper link mapping =0: lower link and upper link not swap, =1: lower link and upper link are swapped
                                  [bit2] LVDS 888bit per color mode  =0: 666 bit per color =1:888 bit per color
                                  [bit3] LVDS parameter override enable  =0: ucLvdsMisc parameter are not used =1: ucLvdsMisc parameter should be used
                                  [bit4] Polarity of signal sent to digital BLON output pin. =0: not inverted(active high) =1: inverted ( active low )
**********************************************************************************************************************/

// this Table is used for Liano/Ontario APU
typedef struct _ATOM_FUSION_SYSTEM_INFO_V1
{
  ATOM_INTEGRATED_SYSTEM_INFO_V6    sIntegratedSysInfo;   
  ULONG  ulPowerplayTable[128];  
}ATOM_FUSION_SYSTEM_INFO_V1; 


typedef struct _ATOM_TDP_CONFIG_BITS
{
#if ATOM_BIG_ENDIAN
  ULONG   uReserved:2;
  ULONG   uTDP_Value:14;  // Original TDP value in tens of milli watts
  ULONG   uCTDP_Value:14; // Override value in tens of milli watts
  ULONG   uCTDP_Enable:2; // = (uCTDP_Value > uTDP_Value? 2: (uCTDP_Value < uTDP_Value))
#else
  ULONG   uCTDP_Enable:2; // = (uCTDP_Value > uTDP_Value? 2: (uCTDP_Value < uTDP_Value))
  ULONG   uCTDP_Value:14; // Override value in tens of milli watts
  ULONG   uTDP_Value:14;  // Original TDP value in tens of milli watts
  ULONG   uReserved:2;
#endif
}ATOM_TDP_CONFIG_BITS;

typedef union _ATOM_TDP_CONFIG
{
  ATOM_TDP_CONFIG_BITS TDP_config;
  ULONG            TDP_config_all;
}ATOM_TDP_CONFIG;

/**********************************************************************************************************************
  ATOM_FUSION_SYSTEM_INFO_V1 Description
sIntegratedSysInfo:               refer to ATOM_INTEGRATED_SYSTEM_INFO_V6 definition.
ulPowerplayTable[128]:            This 512 bytes memory is used to save ATOM_PPLIB_POWERPLAYTABLE3, starting form ulPowerplayTable[0]    
**********************************************************************************************************************/ 

// this IntegrateSystemInfoTable is used for Trinity APU
typedef struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_7
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  ULONG  ulBootUpEngineClock;
  ULONG  ulDentistVCOFreq;
  ULONG  ulBootUpUMAClock;
  ATOM_CLK_VOLT_CAPABILITY   sDISPCLK_Voltage[4];
  ULONG  ulBootUpReqDisplayVector;
  ULONG  ulOtherDisplayMisc;
  ULONG  ulGPUCapInfo;
  ULONG  ulSB_MMIO_Base_Addr;
  USHORT usRequestedPWMFreqInHz;
  UCHAR  ucHtcTmpLmt;
  UCHAR  ucHtcHystLmt;
  ULONG  ulMinEngineClock;
  ULONG  ulSystemConfig;            
  ULONG  ulCPUCapInfo;
  USHORT usNBP0Voltage;               
  USHORT usNBP1Voltage;
  USHORT usBootUpNBVoltage;                       
  USHORT usExtDispConnInfoOffset;
  USHORT usPanelRefreshRateRange;     
  UCHAR  ucMemoryType;  
  UCHAR  ucUMAChannelNumber;
  UCHAR  strVBIOSMsg[40];
  ATOM_TDP_CONFIG  asTdpConfig;
  ULONG  ulReserved[19];
  ATOM_AVAILABLE_SCLK_LIST   sAvail_SCLK[5];
  ULONG  ulGMCRestoreResetTime;
  ULONG  ulMinimumNClk;
  ULONG  ulIdleNClk;
  ULONG  ulDDR_DLL_PowerUpTime;
  ULONG  ulDDR_PLL_PowerUpTime;
  USHORT usPCIEClkSSPercentage;
  USHORT usPCIEClkSSType;
  USHORT usLvdsSSPercentage;
  USHORT usLvdsSSpreadRateIn10Hz;
  USHORT usHDMISSPercentage;
  USHORT usHDMISSpreadRateIn10Hz;
  USHORT usDVISSPercentage;
  USHORT usDVISSpreadRateIn10Hz;
  ULONG  SclkDpmBoostMargin;
  ULONG  SclkDpmThrottleMargin;
  USHORT SclkDpmTdpLimitPG; 
  USHORT SclkDpmTdpLimitBoost;
  ULONG  ulBoostEngineCLock;
  UCHAR  ulBoostVid_2bit;  
  UCHAR  EnableBoost;
  USHORT GnbTdpLimit;
  USHORT usMaxLVDSPclkFreqInSingleLink;
  UCHAR  ucLvdsMisc;
  UCHAR  ucTravisLVDSVolAdjust;
  UCHAR  ucLVDSPwrOnSeqDIGONtoDE_in4Ms;
  UCHAR  ucLVDSPwrOnSeqDEtoVARY_BL_in4Ms;
  UCHAR  ucLVDSPwrOffSeqVARY_BLtoDE_in4Ms;
  UCHAR  ucLVDSPwrOffSeqDEtoDIGON_in4Ms;
  UCHAR  ucLVDSOffToOnDelay_in4Ms;
  UCHAR  ucLVDSPwrOnSeqVARY_BLtoBLON_in4Ms;
  UCHAR  ucLVDSPwrOffSeqBLONtoVARY_BL_in4Ms;
  UCHAR  ucMinAllowedBL_Level;
  ULONG  ulLCDBitDepthControlVal;
  ULONG  ulNbpStateMemclkFreq[4];
  USHORT usNBP2Voltage;               
  USHORT usNBP3Voltage;
  ULONG  ulNbpStateNClkFreq[4];
  UCHAR  ucNBDPMEnable;
  UCHAR  ucReserved[3];
  UCHAR  ucDPMState0VclkFid;
  UCHAR  ucDPMState0DclkFid;
  UCHAR  ucDPMState1VclkFid;
  UCHAR  ucDPMState1DclkFid;
  UCHAR  ucDPMState2VclkFid;
  UCHAR  ucDPMState2DclkFid;
  UCHAR  ucDPMState3VclkFid;
  UCHAR  ucDPMState3DclkFid;
  ATOM_EXTERNAL_DISPLAY_CONNECTION_INFO sExtDispConnInfo;
}ATOM_INTEGRATED_SYSTEM_INFO_V1_7;

// ulOtherDisplayMisc
#define INTEGRATED_SYSTEM_INFO__GET_EDID_CALLBACK_FUNC_SUPPORT            0x01
#define INTEGRATED_SYSTEM_INFO__GET_BOOTUP_DISPLAY_CALLBACK_FUNC_SUPPORT  0x02
#define INTEGRATED_SYSTEM_INFO__GET_EXPANSION_CALLBACK_FUNC_SUPPORT       0x04
#define INTEGRATED_SYSTEM_INFO__FAST_BOOT_SUPPORT                         0x08

// ulGPUCapInfo
#define SYS_INFO_GPUCAPS__TMDSHDMI_COHERENT_SINGLEPLL_MODE                0x01
#define SYS_INFO_GPUCAPS__DP_SINGLEPLL_MODE                               0x02
#define SYS_INFO_GPUCAPS__DISABLE_AUX_MODE_DETECT                         0x08
#define SYS_INFO_GPUCAPS__ENABEL_DFS_BYPASS                               0x10

/**********************************************************************************************************************
  ATOM_INTEGRATED_SYSTEM_INFO_V1_7 Description
ulBootUpEngineClock:              VBIOS bootup Engine clock frequency, in 10kHz unit. if it is equal 0, then VBIOS use pre-defined bootup engine clock
ulDentistVCOFreq:                 Dentist VCO clock in 10kHz unit. 
ulBootUpUMAClock:                 System memory boot up clock frequency in 10Khz unit. 
sDISPCLK_Voltage:                 Report Display clock voltage requirement.
 
ulBootUpReqDisplayVector:         VBIOS boot up display IDs, following are supported devices in Trinity projects:
                                  ATOM_DEVICE_CRT1_SUPPORT                  0x0001
                                  ATOM_DEVICE_DFP1_SUPPORT                  0x0008 
                                  ATOM_DEVICE_DFP6_SUPPORT                  0x0040 
                                  ATOM_DEVICE_DFP2_SUPPORT                  0x0080       
                                  ATOM_DEVICE_DFP3_SUPPORT                  0x0200       
                                  ATOM_DEVICE_DFP4_SUPPORT                  0x0400        
                                  ATOM_DEVICE_DFP5_SUPPORT                  0x0800
                                  ATOM_DEVICE_LCD1_SUPPORT                  0x0002
ulOtherDisplayMisc:      	        bit[0]=0: INT15 callback function Get LCD EDID ( ax=4e08, bl=1b ) is not supported by SBIOS. 
                                        =1: INT15 callback function Get LCD EDID ( ax=4e08, bl=1b ) is supported by SBIOS. 
                                  bit[1]=0: INT15 callback function Get boot display( ax=4e08, bl=01h) is not supported by SBIOS
                                        =1: INT15 callback function Get boot display( ax=4e08, bl=01h) is supported by SBIOS
                                  bit[2]=0: INT15 callback function Get panel Expansion ( ax=4e08, bl=02h) is not supported by SBIOS
                                        =1: INT15 callback function Get panel Expansion ( ax=4e08, bl=02h) is supported by SBIOS
                                  bit[3]=0: VBIOS fast boot is disable
                                        =1: VBIOS fast boot is enable. ( VBIOS skip display device detection in every set mode if LCD panel is connect and LID is open)
ulGPUCapInfo:                     bit[0]=0: TMDS/HDMI Coherent Mode use cascade PLL mode.
                                        =1: TMDS/HDMI Coherent Mode use signel PLL mode.
                                  bit[1]=0: DP mode use cascade PLL mode ( New for Trinity )
                                        =1: DP mode use single PLL mode
                                  bit[3]=0: Enable AUX HW mode detection logic
                                        =1: Disable AUX HW mode detection logic
                                      
ulSB_MMIO_Base_Addr:              Physical Base address to SB MMIO space. Driver needs to initialize it for SMU usage.

usRequestedPWMFreqInHz:           When it's set to 0x0 by SBIOS: the LCD BackLight is not controlled by GPU(SW). 
                                  Any attempt to change BL using VBIOS function or enable VariBri from PP table is not effective since ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==0;
                                  
                                  When it's set to a non-zero frequency, the BackLight is controlled by GPU (SW) in one of two ways below:
                                  1. SW uses the GPU BL PWM output to control the BL, in chis case, this non-zero frequency determines what freq GPU should use;
                                  VBIOS will set up proper PWM frequency and ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==1,as the result,
                                  Changing BL using VBIOS function is functional in both driver and non-driver present environment; 
                                  and enabling VariBri under the driver environment from PP table is optional.

                                  2. SW uses other means to control BL (like DPCD),this non-zero frequency serves as a flag only indicating
                                  that BL control from GPU is expected.
                                  VBIOS will NOT set up PWM frequency but make ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==1
                                  Changing BL using VBIOS function could be functional in both driver and non-driver present environment,but
                                  it's per platform 
                                  and enabling VariBri under the driver environment from PP table is optional.

ucHtcTmpLmt:                      Refer to D18F3x64 bit[22:16], HtcTmpLmt. 
                                  Threshold on value to enter HTC_active state.
ucHtcHystLmt:                     Refer to D18F3x64 bit[27:24], HtcHystLmt. 
                                  To calculate threshold off value to exit HTC_active state, which is Threshold on vlaue minus ucHtcHystLmt.
ulMinEngineClock:                 Minimum SCLK allowed in 10kHz unit. This is calculated based on WRCK Fuse settings.
ulSystemConfig:                   Bit[0]=0: PCIE Power Gating Disabled 
                                        =1: PCIE Power Gating Enabled
                                  Bit[1]=0: DDR-DLL shut-down feature disabled.
                                         1: DDR-DLL shut-down feature enabled.
                                  Bit[2]=0: DDR-PLL Power down feature disabled.
                                         1: DDR-PLL Power down feature enabled.                                 
ulCPUCapInfo:                     TBD
usNBP0Voltage:                    VID for voltage on NB P0 State
usNBP1Voltage:                    VID for voltage on NB P1 State  
usNBP2Voltage:                    VID for voltage on NB P2 State
usNBP3Voltage:                    VID for voltage on NB P3 State  
usBootUpNBVoltage:                Voltage Index of GNB voltage configured by SBIOS, which is suffcient to support VBIOS DISPCLK requirement.
usExtDispConnInfoOffset:          Offset to sExtDispConnInfo inside the structure
usPanelRefreshRateRange:          Bit vector for LCD supported refresh rate range. If DRR is requestd by the platform, at least two bits need to be set
                                  to indicate a range.
                                  SUPPORTED_LCD_REFRESHRATE_30Hz          0x0004
                                  SUPPORTED_LCD_REFRESHRATE_40Hz          0x0008
                                  SUPPORTED_LCD_REFRESHRATE_50Hz          0x0010
                                  SUPPORTED_LCD_REFRESHRATE_60Hz          0x0020
ucMemoryType:                     [3:0]=1:DDR1;=2:DDR2;=3:DDR3.[7:4] is reserved.
ucUMAChannelNumber:      	        System memory channel numbers. 
ulCSR_M3_ARB_CNTL_DEFAULT[10]:    Arrays with values for CSR M3 arbiter for default
ulCSR_M3_ARB_CNTL_UVD[10]:        Arrays with values for CSR M3 arbiter for UVD playback.
ulCSR_M3_ARB_CNTL_FS3D[10]:       Arrays with values for CSR M3 arbiter for Full Screen 3D applications.
sAvail_SCLK[5]:                   Arrays to provide availabe list of SLCK and corresponding voltage, order from low to high  
ulGMCRestoreResetTime:            GMC power restore and GMC reset time to calculate data reconnection latency. Unit in ns. 
ulMinimumNClk:                    Minimum NCLK speed among all NB-Pstates to calcualte data reconnection latency. Unit in 10kHz. 
ulIdleNClk:                       NCLK speed while memory runs in self-refresh state. Unit in 10kHz.
ulDDR_DLL_PowerUpTime:            DDR PHY DLL power up time. Unit in ns.
ulDDR_PLL_PowerUpTime:            DDR PHY PLL power up time. Unit in ns.
usPCIEClkSSPercentage:            PCIE Clock Spread Spectrum Percentage in unit 0.01%; 100 mean 1%.
usPCIEClkSSType:                  PCIE Clock Spread Spectrum Type. 0 for Down spread(default); 1 for Center spread.
usLvdsSSPercentage:               LVDS panel ( not include eDP ) Spread Spectrum Percentage in unit of 0.01%, =0, use VBIOS default setting. 
usLvdsSSpreadRateIn10Hz:          LVDS panel ( not include eDP ) Spread Spectrum frequency in unit of 10Hz, =0, use VBIOS default setting. 
usHDMISSPercentage:               HDMI Spread Spectrum Percentage in unit 0.01%; 100 mean 1%,  =0, use VBIOS default setting. 
usHDMISSpreadRateIn10Hz:          HDMI Spread Spectrum frequency in unit of 10Hz,  =0, use VBIOS default setting. 
usDVISSPercentage:                DVI Spread Spectrum Percentage in unit 0.01%; 100 mean 1%,  =0, use VBIOS default setting. 
usDVISSpreadRateIn10Hz:           DVI Spread Spectrum frequency in unit of 10Hz,  =0, use VBIOS default setting. 
usMaxLVDSPclkFreqInSingleLink:    Max pixel clock LVDS panel single link, if=0 means VBIOS use default threhold, right now it is 85Mhz
ucLVDSMisc:                       [bit0] LVDS 888bit panel mode =0: LVDS 888 panel in LDI mode, =1: LVDS 888 panel in FPDI mode
                                  [bit1] LVDS panel lower and upper link mapping =0: lower link and upper link not swap, =1: lower link and upper link are swapped
                                  [bit2] LVDS 888bit per color mode  =0: 666 bit per color =1:888 bit per color
                                  [bit3] LVDS parameter override enable  =0: ucLvdsMisc parameter are not used =1: ucLvdsMisc parameter should be used
                                  [bit4] Polarity of signal sent to digital BLON output pin. =0: not inverted(active high) =1: inverted ( active low )
                                  [bit5] Travid LVDS output voltage override enable, when =1, use ucTravisLVDSVolAdjust value to overwrite Traivs register LVDS_CTRL_4
ucTravisLVDSVolAdjust             When ucLVDSMisc[5]=1,it means platform SBIOS want to overwrite TravisLVDSVoltage. Then VBIOS will use ucTravisLVDSVolAdjust 
                                  value to program Travis register LVDS_CTRL_4
ucLVDSPwrOnSeqDIGONtoDE_in4Ms:    LVDS power up sequence time in unit of 4ms, time delay from DIGON signal active to data enable signal active( DE ).
                                  =0 mean use VBIOS default which is 8 ( 32ms ). The LVDS power up sequence is as following: DIGON->DE->VARY_BL->BLON. 
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.
ucLVDSPwrOnDEtoVARY_BL_in4Ms:     LVDS power up sequence time in unit of 4ms., time delay from DE( data enable ) active to Vary Brightness enable signal active( VARY_BL ).  
                                  =0 mean use VBIOS default which is 90 ( 360ms ). The LVDS power up sequence is as following: DIGON->DE->VARY_BL->BLON. 
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.

ucLVDSPwrOffVARY_BLtoDE_in4Ms:    LVDS power down sequence time in unit of 4ms, time delay from data enable ( DE ) signal off to LCDVCC (DIGON) off. 
                                  =0 mean use VBIOS default delay which is 8 ( 32ms ). The LVDS power down sequence is as following: BLON->VARY_BL->DE->DIGON
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.

ucLVDSPwrOffDEtoDIGON_in4Ms:      LVDS power down sequence time in unit of 4ms, time delay from vary brightness enable signal( VARY_BL) off to data enable ( DE ) signal off. 
                                  =0 mean use VBIOS default which is 90 ( 360ms ). The LVDS power down sequence is as following: BLON->VARY_BL->DE->DIGON
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.

ucLVDSOffToOnDelay_in4Ms:         LVDS power down sequence time in unit of 4ms. Time delay from DIGON signal off to DIGON signal active. 
                                  =0 means to use VBIOS default delay which is 125 ( 500ms ).
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.

ucLVDSPwrOnSeqVARY_BLtoBLON_in4Ms:
                                  LVDS power up sequence time in unit of 4ms. Time delay from VARY_BL signal on to DLON signal active. 
                                  =0 means to use VBIOS default delay which is 0 ( 0ms ).
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.

ucLVDSPwrOffSeqBLONtoVARY_BL_in4Ms:  
                                  LVDS power down sequence time in unit of 4ms. Time delay from BLON signal off to VARY_BL signal off. 
                                  =0 means to use VBIOS default delay which is 0 ( 0ms ).
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.

ucMinAllowedBL_Level:             Lowest LCD backlight PWM level. This is customer platform specific parameters. By default it is 0. 

ulNbpStateMemclkFreq[4]:          system memory clock frequncey in unit of 10Khz in different NB pstate. 

**********************************************************************************************************************/

// this IntegrateSystemInfoTable is used for Kaveri & Kabini APU
typedef struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_8
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  ULONG  ulBootUpEngineClock;
  ULONG  ulDentistVCOFreq;
  ULONG  ulBootUpUMAClock;
  ATOM_CLK_VOLT_CAPABILITY   sDISPCLK_Voltage[4];
  ULONG  ulBootUpReqDisplayVector;
  ULONG  ulVBIOSMisc;
  ULONG  ulGPUCapInfo;
  ULONG  ulDISP_CLK2Freq;
  USHORT usRequestedPWMFreqInHz;
  UCHAR  ucHtcTmpLmt;
  UCHAR  ucHtcHystLmt;
  ULONG  ulReserved2;
  ULONG  ulSystemConfig;            
  ULONG  ulCPUCapInfo;
  ULONG  ulReserved3;
  USHORT usGPUReservedSysMemSize;
  USHORT usExtDispConnInfoOffset;
  USHORT usPanelRefreshRateRange;     
  UCHAR  ucMemoryType;  
  UCHAR  ucUMAChannelNumber;
  UCHAR  strVBIOSMsg[40];
  ATOM_TDP_CONFIG  asTdpConfig;
  ULONG  ulReserved[19];
  ATOM_AVAILABLE_SCLK_LIST   sAvail_SCLK[5];
  ULONG  ulGMCRestoreResetTime;
  ULONG  ulReserved4;
  ULONG  ulIdleNClk;
  ULONG  ulDDR_DLL_PowerUpTime;
  ULONG  ulDDR_PLL_PowerUpTime;
  USHORT usPCIEClkSSPercentage;
  USHORT usPCIEClkSSType;
  USHORT usLvdsSSPercentage;
  USHORT usLvdsSSpreadRateIn10Hz;
  USHORT usHDMISSPercentage;
  USHORT usHDMISSpreadRateIn10Hz;
  USHORT usDVISSPercentage;
  USHORT usDVISSpreadRateIn10Hz;
  ULONG  ulGPUReservedSysMemBaseAddrLo;
  ULONG  ulGPUReservedSysMemBaseAddrHi;
  ULONG  ulReserved5[3];
  USHORT usMaxLVDSPclkFreqInSingleLink;
  UCHAR  ucLvdsMisc;
  UCHAR  ucTravisLVDSVolAdjust;
  UCHAR  ucLVDSPwrOnSeqDIGONtoDE_in4Ms;
  UCHAR  ucLVDSPwrOnSeqDEtoVARY_BL_in4Ms;
  UCHAR  ucLVDSPwrOffSeqVARY_BLtoDE_in4Ms;
  UCHAR  ucLVDSPwrOffSeqDEtoDIGON_in4Ms;
  UCHAR  ucLVDSOffToOnDelay_in4Ms;
  UCHAR  ucLVDSPwrOnSeqVARY_BLtoBLON_in4Ms;
  UCHAR  ucLVDSPwrOffSeqBLONtoVARY_BL_in4Ms;
  UCHAR  ucMinAllowedBL_Level;
  ULONG  ulLCDBitDepthControlVal;
  ULONG  ulNbpStateMemclkFreq[4];
  ULONG  ulReserved6;               
  ULONG  ulNbpStateNClkFreq[4];
  USHORT usNBPStateVoltage[4];            
  USHORT usBootUpNBVoltage;   
  USHORT usReserved2; 
  ATOM_EXTERNAL_DISPLAY_CONNECTION_INFO sExtDispConnInfo;
}ATOM_INTEGRATED_SYSTEM_INFO_V1_8;

/**********************************************************************************************************************
  ATOM_INTEGRATED_SYSTEM_INFO_V1_8 Description
ulBootUpEngineClock:              VBIOS bootup Engine clock frequency, in 10kHz unit. if it is equal 0, then VBIOS use pre-defined bootup engine clock
ulDentistVCOFreq:                 Dentist VCO clock in 10kHz unit. 
ulBootUpUMAClock:                 System memory boot up clock frequency in 10Khz unit. 
sDISPCLK_Voltage:                 Report Display clock frequency requirement on GNB voltage(up to 4 voltage levels).
 
ulBootUpReqDisplayVector:         VBIOS boot up display IDs, following are supported devices in Trinity projects:
                                  ATOM_DEVICE_CRT1_SUPPORT                  0x0001
                                  ATOM_DEVICE_DFP1_SUPPORT                  0x0008
                                  ATOM_DEVICE_DFP6_SUPPORT                  0x0040
                                  ATOM_DEVICE_DFP2_SUPPORT                  0x0080
                                  ATOM_DEVICE_DFP3_SUPPORT                  0x0200
                                  ATOM_DEVICE_DFP4_SUPPORT                  0x0400
                                  ATOM_DEVICE_DFP5_SUPPORT                  0x0800
                                  ATOM_DEVICE_LCD1_SUPPORT                  0x0002

ulVBIOSMisc:      	              Miscellenous flags for VBIOS requirement and interface 
                                  bit[0]=0: INT15 callback function Get LCD EDID ( ax=4e08, bl=1b ) is not supported by SBIOS. 
                                        =1: INT15 callback function Get LCD EDID ( ax=4e08, bl=1b ) is supported by SBIOS.
                                  bit[1]=0: INT15 callback function Get boot display( ax=4e08, bl=01h) is not supported by SBIOS
                                        =1: INT15 callback function Get boot display( ax=4e08, bl=01h) is supported by SBIOS
                                  bit[2]=0: INT15 callback function Get panel Expansion ( ax=4e08, bl=02h) is not supported by SBIOS
                                        =1: INT15 callback function Get panel Expansion ( ax=4e08, bl=02h) is supported by SBIOS
                                  bit[3]=0: VBIOS fast boot is disable
                                        =1: VBIOS fast boot is enable. ( VBIOS skip display device detection in every set mode if LCD panel is connect and LID is open)

ulGPUCapInfo:                     bit[0~2]= Reserved
                                  bit[3]=0: Enable AUX HW mode detection logic
                                        =1: Disable AUX HW mode detection logic
                                  bit[4]=0: Disable DFS bypass feature
                                        =1: Enable DFS bypass feature

usRequestedPWMFreqInHz:           When it's set to 0x0 by SBIOS: the LCD BackLight is not controlled by GPU(SW). 
                                  Any attempt to change BL using VBIOS function or enable VariBri from PP table is not effective since ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==0;
                                  
                                  When it's set to a non-zero frequency, the BackLight is controlled by GPU (SW) in one of two ways below:
                                  1. SW uses the GPU BL PWM output to control the BL, in chis case, this non-zero frequency determines what freq GPU should use;
                                  VBIOS will set up proper PWM frequency and ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==1,as the result,
                                  Changing BL using VBIOS function is functional in both driver and non-driver present environment; 
                                  and enabling VariBri under the driver environment from PP table is optional.

                                  2. SW uses other means to control BL (like DPCD),this non-zero frequency serves as a flag only indicating
                                  that BL control from GPU is expected.
                                  VBIOS will NOT set up PWM frequency but make ATOM_BIOS_INFO_BL_CONTROLLED_BY_GPU==1
                                  Changing BL using VBIOS function could be functional in both driver and non-driver present environment,but
                                  it's per platform 
                                  and enabling VariBri under the driver environment from PP table is optional.

ucHtcTmpLmt:                      Refer to D18F3x64 bit[22:16], HtcTmpLmt. Threshold on value to enter HTC_active state.
ucHtcHystLmt:                     Refer to D18F3x64 bit[27:24], HtcHystLmt. 
                                  To calculate threshold off value to exit HTC_active state, which is Threshold on vlaue minus ucHtcHystLmt.

ulSystemConfig:                   Bit[0]=0: PCIE Power Gating Disabled 
                                        =1: PCIE Power Gating Enabled
                                  Bit[1]=0: DDR-DLL shut-down feature disabled.
                                         1: DDR-DLL shut-down feature enabled.
                                  Bit[2]=0: DDR-PLL Power down feature disabled.
                                         1: DDR-PLL Power down feature enabled. 
                                  Bit[3]=0: GNB DPM is disabled
                                        =1: GNB DPM is enabled                                
ulCPUCapInfo:                     TBD

usExtDispConnInfoOffset:          Offset to sExtDispConnInfo inside the structure
usPanelRefreshRateRange:          Bit vector for LCD supported refresh rate range. If DRR is requestd by the platform, at least two bits need to be set
                                  to indicate a range.
                                  SUPPORTED_LCD_REFRESHRATE_30Hz          0x0004
                                  SUPPORTED_LCD_REFRESHRATE_40Hz          0x0008
                                  SUPPORTED_LCD_REFRESHRATE_50Hz          0x0010
                                  SUPPORTED_LCD_REFRESHRATE_60Hz          0x0020

ucMemoryType:                     [3:0]=1:DDR1;=2:DDR2;=3:DDR3;=5:GDDR5; [7:4] is reserved.
ucUMAChannelNumber:      	        System memory channel numbers. 

strVBIOSMsg[40]:                  VBIOS boot up customized message string 

sAvail_SCLK[5]:                   Arrays to provide availabe list of SLCK and corresponding voltage, order from low to high  

ulGMCRestoreResetTime:            GMC power restore and GMC reset time to calculate data reconnection latency. Unit in ns. 
ulIdleNClk:                       NCLK speed while memory runs in self-refresh state, used to calculate self-refresh latency. Unit in 10kHz.
ulDDR_DLL_PowerUpTime:            DDR PHY DLL power up time. Unit in ns.
ulDDR_PLL_PowerUpTime:            DDR PHY PLL power up time. Unit in ns.

usPCIEClkSSPercentage:            PCIE Clock Spread Spectrum Percentage in unit 0.01%; 100 mean 1%.
usPCIEClkSSType:                  PCIE Clock Spread Spectrum Type. 0 for Down spread(default); 1 for Center spread.
usLvdsSSPercentage:               LVDS panel ( not include eDP ) Spread Spectrum Percentage in unit of 0.01%, =0, use VBIOS default setting. 
usLvdsSSpreadRateIn10Hz:          LVDS panel ( not include eDP ) Spread Spectrum frequency in unit of 10Hz, =0, use VBIOS default setting. 
usHDMISSPercentage:               HDMI Spread Spectrum Percentage in unit 0.01%; 100 mean 1%,  =0, use VBIOS default setting. 
usHDMISSpreadRateIn10Hz:          HDMI Spread Spectrum frequency in unit of 10Hz,  =0, use VBIOS default setting. 
usDVISSPercentage:                DVI Spread Spectrum Percentage in unit 0.01%; 100 mean 1%,  =0, use VBIOS default setting. 
usDVISSpreadRateIn10Hz:           DVI Spread Spectrum frequency in unit of 10Hz,  =0, use VBIOS default setting. 

usGPUReservedSysMemSize:          Reserved system memory size for ACP engine in APU GNB, units in MB. 0/2/4MB based on CMOS options, current default could be 0MB. KV only, not on KB.
ulGPUReservedSysMemBaseAddrLo:    Low 32 bits base address to the reserved system memory. 
ulGPUReservedSysMemBaseAddrHi:    High 32 bits base address to the reserved system memory. 

usMaxLVDSPclkFreqInSingleLink:    Max pixel clock LVDS panel single link, if=0 means VBIOS use default threhold, right now it is 85Mhz
ucLVDSMisc:                       [bit0] LVDS 888bit panel mode =0: LVDS 888 panel in LDI mode, =1: LVDS 888 panel in FPDI mode
                                  [bit1] LVDS panel lower and upper link mapping =0: lower link and upper link not swap, =1: lower link and upper link are swapped
                                  [bit2] LVDS 888bit per color mode  =0: 666 bit per color =1:888 bit per color
                                  [bit3] LVDS parameter override enable  =0: ucLvdsMisc parameter are not used =1: ucLvdsMisc parameter should be used
                                  [bit4] Polarity of signal sent to digital BLON output pin. =0: not inverted(active high) =1: inverted ( active low )
                                  [bit5] Travid LVDS output voltage override enable, when =1, use ucTravisLVDSVolAdjust value to overwrite Traivs register LVDS_CTRL_4
ucTravisLVDSVolAdjust             When ucLVDSMisc[5]=1,it means platform SBIOS want to overwrite TravisLVDSVoltage. Then VBIOS will use ucTravisLVDSVolAdjust 
                                  value to program Travis register LVDS_CTRL_4
ucLVDSPwrOnSeqDIGONtoDE_in4Ms:    
                                  LVDS power up sequence time in unit of 4ms, time delay from DIGON signal active to data enable signal active( DE ).
                                  =0 mean use VBIOS default which is 8 ( 32ms ). The LVDS power up sequence is as following: DIGON->DE->VARY_BL->BLON. 
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.
ucLVDSPwrOnDEtoVARY_BL_in4Ms:     
                                  LVDS power up sequence time in unit of 4ms., time delay from DE( data enable ) active to Vary Brightness enable signal active( VARY_BL ).  
                                  =0 mean use VBIOS default which is 90 ( 360ms ). The LVDS power up sequence is as following: DIGON->DE->VARY_BL->BLON. 
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.
ucLVDSPwrOffVARY_BLtoDE_in4Ms:    
                                  LVDS power down sequence time in unit of 4ms, time delay from data enable ( DE ) signal off to LCDVCC (DIGON) off. 
                                  =0 mean use VBIOS default delay which is 8 ( 32ms ). The LVDS power down sequence is as following: BLON->VARY_BL->DE->DIGON
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.
ucLVDSPwrOffDEtoDIGON_in4Ms:      
                                   LVDS power down sequence time in unit of 4ms, time delay from vary brightness enable signal( VARY_BL) off to data enable ( DE ) signal off. 
                                  =0 mean use VBIOS default which is 90 ( 360ms ). The LVDS power down sequence is as following: BLON->VARY_BL->DE->DIGON
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.
ucLVDSOffToOnDelay_in4Ms:         
                                  LVDS power down sequence time in unit of 4ms. Time delay from DIGON signal off to DIGON signal active. 
                                  =0 means to use VBIOS default delay which is 125 ( 500ms ).
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.
ucLVDSPwrOnSeqVARY_BLtoBLON_in4Ms:
                                  LVDS power up sequence time in unit of 4ms. Time delay from VARY_BL signal on to DLON signal active. 
                                  =0 means to use VBIOS default delay which is 0 ( 0ms ).
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.

ucLVDSPwrOffSeqBLONtoVARY_BL_in4Ms:  
                                  LVDS power down sequence time in unit of 4ms. Time delay from BLON signal off to VARY_BL signal off. 
                                  =0 means to use VBIOS default delay which is 0 ( 0ms ).
                                  This parameter is used by VBIOS only. VBIOS will patch LVDS_InfoTable.
ucMinAllowedBL_Level:             Lowest LCD backlight PWM level. This is customer platform specific parameters. By default it is 0. 

ulLCDBitDepthControlVal:          GPU display control encoder bit dither control setting, used to program register mmFMT_BIT_DEPTH_CONTROL

ulNbpStateMemclkFreq[4]:          system memory clock frequncey in unit of 10Khz in different NB P-State(P0, P1, P2 & P3).
ulNbpStateNClkFreq[4]:            NB P-State NClk frequency in different NB P-State
usNBPStateVoltage[4]:             NB P-State (P0/P1 & P2/P3) voltage; NBP3 refers to lowes voltage
usBootUpNBVoltage:                NB P-State voltage during boot up before driver loaded 
sExtDispConnInfo:                 Display connector information table provided to VBIOS

**********************************************************************************************************************/

// this Table is used for Kaveri/Kabini APU
typedef struct _ATOM_FUSION_SYSTEM_INFO_V2
{
  ATOM_INTEGRATED_SYSTEM_INFO_V1_8    sIntegratedSysInfo;       // refer to ATOM_INTEGRATED_SYSTEM_INFO_V1_8 definition
  ULONG                               ulPowerplayTable[128];    // Update comments here to link new powerplay table definition structure
}ATOM_FUSION_SYSTEM_INFO_V2; 


/**************************************************************************/
// This portion is only used when ext thermal chip or engine/memory clock SS chip is populated on a design
//Memory SS Info Table
//Define Memory Clock SS chip ID
#define ICS91719  1
#define ICS91720  2

//Define one structure to inform SW a "block of data" writing to external SS chip via I2C protocol
typedef struct _ATOM_I2C_DATA_RECORD
{
  UCHAR         ucNunberOfBytes;                                              //Indicates how many bytes SW needs to write to the external ASIC for one block, besides to "Start" and "Stop"
  UCHAR         ucI2CData[1];                                                 //I2C data in bytes, should be less than 16 bytes usually
}ATOM_I2C_DATA_RECORD;


//Define one structure to inform SW how many blocks of data writing to external SS chip via I2C protocol, in addition to other information
typedef struct _ATOM_I2C_DEVICE_SETUP_INFO
{
  ATOM_I2C_ID_CONFIG_ACCESS       sucI2cId;               //I2C line and HW/SW assisted cap.
  UCHAR		                        ucSSChipID;             //SS chip being used
  UCHAR		                        ucSSChipSlaveAddr;      //Slave Address to set up this SS chip
  UCHAR                           ucNumOfI2CDataRecords;  //number of data block
  ATOM_I2C_DATA_RECORD            asI2CData[1];  
}ATOM_I2C_DEVICE_SETUP_INFO;

//==========================================================================================
typedef struct  _ATOM_ASIC_MVDD_INFO
{
  ATOM_COMMON_TABLE_HEADER	      sHeader; 
  ATOM_I2C_DEVICE_SETUP_INFO      asI2CSetup[1];
}ATOM_ASIC_MVDD_INFO;

//==========================================================================================
#define ATOM_MCLK_SS_INFO         ATOM_ASIC_MVDD_INFO

//==========================================================================================
/**************************************************************************/

typedef struct _ATOM_ASIC_SS_ASSIGNMENT
{
	ULONG								ulTargetClockRange;						//Clock Out frequence (VCO ), in unit of 10Khz
  USHORT              usSpreadSpectrumPercentage;		//in unit of 0.01%
	USHORT							usSpreadRateInKhz;						//in unit of kHz, modulation freq
  UCHAR               ucClockIndication;					  //Indicate which clock source needs SS
	UCHAR								ucSpreadSpectrumMode;					//Bit1=0 Down Spread,=1 Center Spread.
	UCHAR								ucReserved[2];
}ATOM_ASIC_SS_ASSIGNMENT;

//Define ucClockIndication, SW uses the IDs below to search if the SS is required/enabled on a clock branch/signal type.
//SS is not required or enabled if a match is not found.
#define ASIC_INTERNAL_MEMORY_SS	         1
#define ASIC_INTERNAL_ENGINE_SS	         2
#define ASIC_INTERNAL_UVD_SS             3
#define ASIC_INTERNAL_SS_ON_TMDS         4
#define ASIC_INTERNAL_SS_ON_HDMI         5
#define ASIC_INTERNAL_SS_ON_LVDS         6
#define ASIC_INTERNAL_SS_ON_DP           7
#define ASIC_INTERNAL_SS_ON_DCPLL        8
#define ASIC_EXTERNAL_SS_ON_DP_CLOCK     9
#define ASIC_INTERNAL_VCE_SS             10
#define ASIC_INTERNAL_GPUPLL_SS          11


typedef struct _ATOM_ASIC_SS_ASSIGNMENT_V2
{
	ULONG								ulTargetClockRange;						//For mem/engine/uvd, Clock Out frequence (VCO ), in unit of 10Khz
                                                    //For TMDS/HDMI/LVDS, it is pixel clock , for DP, it is link clock ( 27000 or 16200 )
  USHORT              usSpreadSpectrumPercentage;		//in unit of 0.01% or 0.001%, decided by ucSpreadSpectrumMode bit4
	USHORT							usSpreadRateIn10Hz;						//in unit of 10Hz, modulation freq
  UCHAR               ucClockIndication;					  //Indicate which clock source needs SS
	UCHAR								ucSpreadSpectrumMode;					//Bit0=0 Down Spread,=1 Center Spread, bit1=0: internal SS bit1=1: external SS
	UCHAR								ucReserved[2];
}ATOM_ASIC_SS_ASSIGNMENT_V2;

//ucSpreadSpectrumMode
//#define ATOM_SS_DOWN_SPREAD_MODE_MASK          0x00000000
//#define ATOM_SS_DOWN_SPREAD_MODE               0x00000000
//#define ATOM_SS_CENTRE_SPREAD_MODE_MASK        0x00000001
//#define ATOM_SS_CENTRE_SPREAD_MODE             0x00000001
//#define ATOM_INTERNAL_SS_MASK                  0x00000000
//#define ATOM_EXTERNAL_SS_MASK                  0x00000002

typedef struct _ATOM_ASIC_INTERNAL_SS_INFO
{
  ATOM_COMMON_TABLE_HEADER	      sHeader; 
  ATOM_ASIC_SS_ASSIGNMENT		      asSpreadSpectrum[4];
}ATOM_ASIC_INTERNAL_SS_INFO;

typedef struct _ATOM_ASIC_INTERNAL_SS_INFO_V2
{
  ATOM_COMMON_TABLE_HEADER	      sHeader; 
  ATOM_ASIC_SS_ASSIGNMENT_V2		  asSpreadSpectrum[1];      //this is point only. 
}ATOM_ASIC_INTERNAL_SS_INFO_V2;

typedef struct _ATOM_ASIC_SS_ASSIGNMENT_V3
{
	ULONG								ulTargetClockRange;						//For mem/engine/uvd, Clock Out frequence (VCO ), in unit of 10Khz
                                                    //For TMDS/HDMI/LVDS, it is pixel clock , for DP, it is link clock ( 27000 or 16200 )
  USHORT              usSpreadSpectrumPercentage;		//in unit of 0.01%
	USHORT							usSpreadRateIn10Hz;						//in unit of 10Hz, modulation freq
  UCHAR               ucClockIndication;					  //Indicate which clock source needs SS
	UCHAR								ucSpreadSpectrumMode;					//Bit0=0 Down Spread,=1 Center Spread, bit1=0: internal SS bit1=1: external SS
	UCHAR								ucReserved[2];
}ATOM_ASIC_SS_ASSIGNMENT_V3;

//ATOM_ASIC_SS_ASSIGNMENT_V3.ucSpreadSpectrumMode
#define SS_MODE_V3_CENTRE_SPREAD_MASK             0x01
#define SS_MODE_V3_EXTERNAL_SS_MASK               0x02
#define SS_MODE_V3_PERCENTAGE_DIV_BY_1000_MASK    0x10

typedef struct _ATOM_ASIC_INTERNAL_SS_INFO_V3
{
  ATOM_COMMON_TABLE_HEADER	      sHeader; 
  ATOM_ASIC_SS_ASSIGNMENT_V3		  asSpreadSpectrum[1];      //this is pointer only. 
}ATOM_ASIC_INTERNAL_SS_INFO_V3;


//==============================Scratch Pad Definition Portion===============================
#define ATOM_DEVICE_CONNECT_INFO_DEF  0
#define ATOM_ROM_LOCATION_DEF         1
#define ATOM_TV_STANDARD_DEF          2
#define ATOM_ACTIVE_INFO_DEF          3
#define ATOM_LCD_INFO_DEF             4
#define ATOM_DOS_REQ_INFO_DEF         5
#define ATOM_ACC_CHANGE_INFO_DEF      6
#define ATOM_DOS_MODE_INFO_DEF        7
#define ATOM_I2C_CHANNEL_STATUS_DEF   8
#define ATOM_I2C_CHANNEL_STATUS1_DEF  9
#define ATOM_INTERNAL_TIMER_DEF       10

// BIOS_0_SCRATCH Definition 
#define ATOM_S0_CRT1_MONO               0x00000001L
#define ATOM_S0_CRT1_COLOR              0x00000002L
#define ATOM_S0_CRT1_MASK               (ATOM_S0_CRT1_MONO+ATOM_S0_CRT1_COLOR)

#define ATOM_S0_TV1_COMPOSITE_A         0x00000004L
#define ATOM_S0_TV1_SVIDEO_A            0x00000008L
#define ATOM_S0_TV1_MASK_A              (ATOM_S0_TV1_COMPOSITE_A+ATOM_S0_TV1_SVIDEO_A)

#define ATOM_S0_CV_A                    0x00000010L
#define ATOM_S0_CV_DIN_A                0x00000020L
#define ATOM_S0_CV_MASK_A               (ATOM_S0_CV_A+ATOM_S0_CV_DIN_A)


#define ATOM_S0_CRT2_MONO               0x00000100L
#define ATOM_S0_CRT2_COLOR              0x00000200L
#define ATOM_S0_CRT2_MASK               (ATOM_S0_CRT2_MONO+ATOM_S0_CRT2_COLOR)

#define ATOM_S0_TV1_COMPOSITE           0x00000400L
#define ATOM_S0_TV1_SVIDEO              0x00000800L
#define ATOM_S0_TV1_SCART               0x00004000L
#define ATOM_S0_TV1_MASK                (ATOM_S0_TV1_COMPOSITE+ATOM_S0_TV1_SVIDEO+ATOM_S0_TV1_SCART)

#define ATOM_S0_CV                      0x00001000L
#define ATOM_S0_CV_DIN                  0x00002000L
#define ATOM_S0_CV_MASK                 (ATOM_S0_CV+ATOM_S0_CV_DIN)

#define ATOM_S0_DFP1                    0x00010000L
#define ATOM_S0_DFP2                    0x00020000L
#define ATOM_S0_LCD1                    0x00040000L
#define ATOM_S0_LCD2                    0x00080000L
#define ATOM_S0_DFP6                    0x00100000L
#define ATOM_S0_DFP3                    0x00200000L
#define ATOM_S0_DFP4                    0x00400000L
#define ATOM_S0_DFP5                    0x00800000L

#define ATOM_S0_DFP_MASK                ATOM_S0_DFP1 | ATOM_S0_DFP2 | ATOM_S0_DFP3 | ATOM_S0_DFP4 | ATOM_S0_DFP5 | ATOM_S0_DFP6

#define ATOM_S0_FAD_REGISTER_BUG        0x02000000L // If set, indicates we are running a PCIE asic with 
                                                    // the FAD/HDP reg access bug.  Bit is read by DAL, this is obsolete from RV5xx

#define ATOM_S0_THERMAL_STATE_MASK      0x1C000000L
#define ATOM_S0_THERMAL_STATE_SHIFT     26

#define ATOM_S0_SYSTEM_POWER_STATE_MASK 0xE0000000L
#define ATOM_S0_SYSTEM_POWER_STATE_SHIFT 29 

#define ATOM_S0_SYSTEM_POWER_STATE_VALUE_AC     1
#define ATOM_S0_SYSTEM_POWER_STATE_VALUE_DC     2
#define ATOM_S0_SYSTEM_POWER_STATE_VALUE_LITEAC 3
#define ATOM_S0_SYSTEM_POWER_STATE_VALUE_LIT2AC 4

//Byte aligned definition for BIOS usage
#define ATOM_S0_CRT1_MONOb0             0x01
#define ATOM_S0_CRT1_COLORb0            0x02
#define ATOM_S0_CRT1_MASKb0             (ATOM_S0_CRT1_MONOb0+ATOM_S0_CRT1_COLORb0)

#define ATOM_S0_TV1_COMPOSITEb0         0x04
#define ATOM_S0_TV1_SVIDEOb0            0x08
#define ATOM_S0_TV1_MASKb0              (ATOM_S0_TV1_COMPOSITEb0+ATOM_S0_TV1_SVIDEOb0)

#define ATOM_S0_CVb0                    0x10
#define ATOM_S0_CV_DINb0                0x20
#define ATOM_S0_CV_MASKb0               (ATOM_S0_CVb0+ATOM_S0_CV_DINb0)

#define ATOM_S0_CRT2_MONOb1             0x01
#define ATOM_S0_CRT2_COLORb1            0x02
#define ATOM_S0_CRT2_MASKb1             (ATOM_S0_CRT2_MONOb1+ATOM_S0_CRT2_COLORb1)

#define ATOM_S0_TV1_COMPOSITEb1         0x04
#define ATOM_S0_TV1_SVIDEOb1            0x08
#define ATOM_S0_TV1_SCARTb1             0x40
#define ATOM_S0_TV1_MASKb1              (ATOM_S0_TV1_COMPOSITEb1+ATOM_S0_TV1_SVIDEOb1+ATOM_S0_TV1_SCARTb1)

#define ATOM_S0_CVb1                    0x10
#define ATOM_S0_CV_DINb1                0x20
#define ATOM_S0_CV_MASKb1               (ATOM_S0_CVb1+ATOM_S0_CV_DINb1)

#define ATOM_S0_DFP1b2                  0x01
#define ATOM_S0_DFP2b2                  0x02
#define ATOM_S0_LCD1b2                  0x04
#define ATOM_S0_LCD2b2                  0x08
#define ATOM_S0_DFP6b2                  0x10
#define ATOM_S0_DFP3b2                  0x20
#define ATOM_S0_DFP4b2                  0x40
#define ATOM_S0_DFP5b2                  0x80


#define ATOM_S0_THERMAL_STATE_MASKb3    0x1C
#define ATOM_S0_THERMAL_STATE_SHIFTb3   2

#define ATOM_S0_SYSTEM_POWER_STATE_MASKb3 0xE0
#define ATOM_S0_LCD1_SHIFT              18

// BIOS_1_SCRATCH Definition
#define ATOM_S1_ROM_LOCATION_MASK       0x0000FFFFL
#define ATOM_S1_PCI_BUS_DEV_MASK        0xFFFF0000L

//	BIOS_2_SCRATCH Definition
#define ATOM_S2_TV1_STANDARD_MASK       0x0000000FL
#define ATOM_S2_CURRENT_BL_LEVEL_MASK   0x0000FF00L
#define ATOM_S2_CURRENT_BL_LEVEL_SHIFT  8

#define ATOM_S2_FORCEDLOWPWRMODE_STATE_MASK       0x0C000000L
#define ATOM_S2_FORCEDLOWPWRMODE_STATE_MASK_SHIFT 26
#define ATOM_S2_FORCEDLOWPWRMODE_STATE_CHANGE     0x10000000L

#define ATOM_S2_DEVICE_DPMS_STATE       0x00010000L
#define ATOM_S2_VRI_BRIGHT_ENABLE       0x20000000L

#define ATOM_S2_DISPLAY_ROTATION_0_DEGREE     0x0
#define ATOM_S2_DISPLAY_ROTATION_90_DEGREE    0x1
#define ATOM_S2_DISPLAY_ROTATION_180_DEGREE   0x2
#define ATOM_S2_DISPLAY_ROTATION_270_DEGREE   0x3
#define ATOM_S2_DISPLAY_ROTATION_DEGREE_SHIFT 30
#define ATOM_S2_DISPLAY_ROTATION_ANGLE_MASK   0xC0000000L


//Byte aligned definition for BIOS usage
#define ATOM_S2_TV1_STANDARD_MASKb0     0x0F
#define ATOM_S2_CURRENT_BL_LEVEL_MASKb1 0xFF
#define ATOM_S2_DEVICE_DPMS_STATEb2     0x01

#define ATOM_S2_DEVICE_DPMS_MASKw1      0x3FF
#define ATOM_S2_FORCEDLOWPWRMODE_STATE_MASKb3     0x0C
#define ATOM_S2_FORCEDLOWPWRMODE_STATE_CHANGEb3   0x10
#define ATOM_S2_TMDS_COHERENT_MODEb3    0x10          // used by VBIOS code only, use coherent mode for TMDS/HDMI mode
#define ATOM_S2_VRI_BRIGHT_ENABLEb3     0x20
#define ATOM_S2_ROTATION_STATE_MASKb3   0xC0


// BIOS_3_SCRATCH Definition
#define ATOM_S3_CRT1_ACTIVE             0x00000001L
#define ATOM_S3_LCD1_ACTIVE             0x00000002L
#define ATOM_S3_TV1_ACTIVE              0x00000004L
#define ATOM_S3_DFP1_ACTIVE             0x00000008L
#define ATOM_S3_CRT2_ACTIVE             0x00000010L
#define ATOM_S3_LCD2_ACTIVE             0x00000020L
#define ATOM_S3_DFP6_ACTIVE             0x00000040L
#define ATOM_S3_DFP2_ACTIVE             0x00000080L
#define ATOM_S3_CV_ACTIVE               0x00000100L
#define ATOM_S3_DFP3_ACTIVE							0x00000200L
#define ATOM_S3_DFP4_ACTIVE							0x00000400L
#define ATOM_S3_DFP5_ACTIVE							0x00000800L

#define ATOM_S3_DEVICE_ACTIVE_MASK      0x00000FFFL

#define ATOM_S3_LCD_FULLEXPANSION_ACTIVE         0x00001000L
#define ATOM_S3_LCD_EXPANSION_ASPEC_RATIO_ACTIVE 0x00002000L

#define ATOM_S3_CRT1_CRTC_ACTIVE        0x00010000L
#define ATOM_S3_LCD1_CRTC_ACTIVE        0x00020000L
#define ATOM_S3_TV1_CRTC_ACTIVE         0x00040000L
#define ATOM_S3_DFP1_CRTC_ACTIVE        0x00080000L
#define ATOM_S3_CRT2_CRTC_ACTIVE        0x00100000L
#define ATOM_S3_LCD2_CRTC_ACTIVE        0x00200000L
#define ATOM_S3_DFP6_CRTC_ACTIVE        0x00400000L
#define ATOM_S3_DFP2_CRTC_ACTIVE        0x00800000L
#define ATOM_S3_CV_CRTC_ACTIVE          0x01000000L
#define ATOM_S3_DFP3_CRTC_ACTIVE				0x02000000L
#define ATOM_S3_DFP4_CRTC_ACTIVE				0x04000000L
#define ATOM_S3_DFP5_CRTC_ACTIVE				0x08000000L

#define ATOM_S3_DEVICE_CRTC_ACTIVE_MASK 0x0FFF0000L
#define ATOM_S3_ASIC_GUI_ENGINE_HUNG    0x20000000L
//Below two definitions are not supported in pplib, but in the old powerplay in DAL
#define ATOM_S3_ALLOW_FAST_PWR_SWITCH   0x40000000L
#define ATOM_S3_RQST_GPU_USE_MIN_PWR    0x80000000L

//Byte aligned definition for BIOS usage
#define ATOM_S3_CRT1_ACTIVEb0           0x01
#define ATOM_S3_LCD1_ACTIVEb0           0x02
#define ATOM_S3_TV1_ACTIVEb0            0x04
#define ATOM_S3_DFP1_ACTIVEb0           0x08
#define ATOM_S3_CRT2_ACTIVEb0           0x10
#define ATOM_S3_LCD2_ACTIVEb0           0x20
#define ATOM_S3_DFP6_ACTIVEb0           0x40
#define ATOM_S3_DFP2_ACTIVEb0           0x80
#define ATOM_S3_CV_ACTIVEb1             0x01
#define ATOM_S3_DFP3_ACTIVEb1						0x02
#define ATOM_S3_DFP4_ACTIVEb1						0x04
#define ATOM_S3_DFP5_ACTIVEb1						0x08

#define ATOM_S3_ACTIVE_CRTC1w0          0xFFF

#define ATOM_S3_CRT1_CRTC_ACTIVEb2      0x01
#define ATOM_S3_LCD1_CRTC_ACTIVEb2      0x02
#define ATOM_S3_TV1_CRTC_ACTIVEb2       0x04
#define ATOM_S3_DFP1_CRTC_ACTIVEb2      0x08
#define ATOM_S3_CRT2_CRTC_ACTIVEb2      0x10
#define ATOM_S3_LCD2_CRTC_ACTIVEb2      0x20
#define ATOM_S3_DFP6_CRTC_ACTIVEb2      0x40
#define ATOM_S3_DFP2_CRTC_ACTIVEb2      0x80
#define ATOM_S3_CV_CRTC_ACTIVEb3        0x01
#define ATOM_S3_DFP3_CRTC_ACTIVEb3			0x02
#define ATOM_S3_DFP4_CRTC_ACTIVEb3			0x04
#define ATOM_S3_DFP5_CRTC_ACTIVEb3			0x08

#define ATOM_S3_ACTIVE_CRTC2w1          0xFFF

// BIOS_4_SCRATCH Definition
#define ATOM_S4_LCD1_PANEL_ID_MASK      0x000000FFL
#define ATOM_S4_LCD1_REFRESH_MASK       0x0000FF00L
#define ATOM_S4_LCD1_REFRESH_SHIFT      8

//Byte aligned definition for BIOS usage
#define ATOM_S4_LCD1_PANEL_ID_MASKb0	  0x0FF
#define ATOM_S4_LCD1_REFRESH_MASKb1		  ATOM_S4_LCD1_PANEL_ID_MASKb0
#define ATOM_S4_VRAM_INFO_MASKb2        ATOM_S4_LCD1_PANEL_ID_MASKb0

// BIOS_5_SCRATCH Definition, BIOS_5_SCRATCH is used by Firmware only !!!!
#define ATOM_S5_DOS_REQ_CRT1b0          0x01
#define ATOM_S5_DOS_REQ_LCD1b0          0x02
#define ATOM_S5_DOS_REQ_TV1b0           0x04
#define ATOM_S5_DOS_REQ_DFP1b0          0x08
#define ATOM_S5_DOS_REQ_CRT2b0          0x10
#define ATOM_S5_DOS_REQ_LCD2b0          0x20
#define ATOM_S5_DOS_REQ_DFP6b0          0x40
#define ATOM_S5_DOS_REQ_DFP2b0          0x80
#define ATOM_S5_DOS_REQ_CVb1            0x01
#define ATOM_S5_DOS_REQ_DFP3b1					0x02
#define ATOM_S5_DOS_REQ_DFP4b1					0x04
#define ATOM_S5_DOS_REQ_DFP5b1					0x08

#define ATOM_S5_DOS_REQ_DEVICEw0        0x0FFF

#define ATOM_S5_DOS_REQ_CRT1            0x0001
#define ATOM_S5_DOS_REQ_LCD1            0x0002
#define ATOM_S5_DOS_REQ_TV1             0x0004
#define ATOM_S5_DOS_REQ_DFP1            0x0008
#define ATOM_S5_DOS_REQ_CRT2            0x0010
#define ATOM_S5_DOS_REQ_LCD2            0x0020
#define ATOM_S5_DOS_REQ_DFP6            0x0040
#define ATOM_S5_DOS_REQ_DFP2            0x0080
#define ATOM_S5_DOS_REQ_CV              0x0100
#define ATOM_S5_DOS_REQ_DFP3            0x0200
#define ATOM_S5_DOS_REQ_DFP4            0x0400
#define ATOM_S5_DOS_REQ_DFP5            0x0800

#define ATOM_S5_DOS_FORCE_CRT1b2        ATOM_S5_DOS_REQ_CRT1b0
#define ATOM_S5_DOS_FORCE_TV1b2         ATOM_S5_DOS_REQ_TV1b0
#define ATOM_S5_DOS_FORCE_CRT2b2        ATOM_S5_DOS_REQ_CRT2b0
#define ATOM_S5_DOS_FORCE_CVb3          ATOM_S5_DOS_REQ_CVb1
#define ATOM_S5_DOS_FORCE_DEVICEw1      (ATOM_S5_DOS_FORCE_CRT1b2+ATOM_S5_DOS_FORCE_TV1b2+ATOM_S5_DOS_FORCE_CRT2b2+\
                                        (ATOM_S5_DOS_FORCE_CVb3<<8))

// BIOS_6_SCRATCH Definition
#define ATOM_S6_DEVICE_CHANGE           0x00000001L
#define ATOM_S6_SCALER_CHANGE           0x00000002L
#define ATOM_S6_LID_CHANGE              0x00000004L
#define ATOM_S6_DOCKING_CHANGE          0x00000008L
#define ATOM_S6_ACC_MODE                0x00000010L
#define ATOM_S6_EXT_DESKTOP_MODE        0x00000020L
#define ATOM_S6_LID_STATE               0x00000040L
#define ATOM_S6_DOCK_STATE              0x00000080L
#define ATOM_S6_CRITICAL_STATE          0x00000100L
#define ATOM_S6_HW_I2C_BUSY_STATE       0x00000200L
#define ATOM_S6_THERMAL_STATE_CHANGE    0x00000400L
#define ATOM_S6_INTERRUPT_SET_BY_BIOS   0x00000800L
#define ATOM_S6_REQ_LCD_EXPANSION_FULL         0x00001000L //Normal expansion Request bit for LCD
#define ATOM_S6_REQ_LCD_EXPANSION_ASPEC_RATIO  0x00002000L //Aspect ratio expansion Request bit for LCD

#define ATOM_S6_DISPLAY_STATE_CHANGE    0x00004000L        //This bit is recycled when ATOM_BIOS_INFO_BIOS_SCRATCH6_SCL2_REDEFINE is set,previously it's SCL2_H_expansion
#define ATOM_S6_I2C_STATE_CHANGE        0x00008000L        //This bit is recycled,when ATOM_BIOS_INFO_BIOS_SCRATCH6_SCL2_REDEFINE is set,previously it's SCL2_V_expansion

#define ATOM_S6_ACC_REQ_CRT1            0x00010000L
#define ATOM_S6_ACC_REQ_LCD1            0x00020000L
#define ATOM_S6_ACC_REQ_TV1             0x00040000L
#define ATOM_S6_ACC_REQ_DFP1            0x00080000L
#define ATOM_S6_ACC_REQ_CRT2            0x00100000L
#define ATOM_S6_ACC_REQ_LCD2            0x00200000L
#define ATOM_S6_ACC_REQ_DFP6            0x00400000L
#define ATOM_S6_ACC_REQ_DFP2            0x00800000L
#define ATOM_S6_ACC_REQ_CV              0x01000000L
#define ATOM_S6_ACC_REQ_DFP3						0x02000000L
#define ATOM_S6_ACC_REQ_DFP4						0x04000000L
#define ATOM_S6_ACC_REQ_DFP5						0x08000000L

#define ATOM_S6_ACC_REQ_MASK                0x0FFF0000L
#define ATOM_S6_SYSTEM_POWER_MODE_CHANGE    0x10000000L
#define ATOM_S6_ACC_BLOCK_DISPLAY_SWITCH    0x20000000L
#define ATOM_S6_VRI_BRIGHTNESS_CHANGE       0x40000000L
#define ATOM_S6_CONFIG_DISPLAY_CHANGE_MASK  0x80000000L

//Byte aligned definition for BIOS usage
#define ATOM_S6_DEVICE_CHANGEb0         0x01
#define ATOM_S6_SCALER_CHANGEb0         0x02
#define ATOM_S6_LID_CHANGEb0            0x04
#define ATOM_S6_DOCKING_CHANGEb0        0x08
#define ATOM_S6_ACC_MODEb0              0x10
#define ATOM_S6_EXT_DESKTOP_MODEb0      0x20
#define ATOM_S6_LID_STATEb0             0x40
#define ATOM_S6_DOCK_STATEb0            0x80
#define ATOM_S6_CRITICAL_STATEb1        0x01
#define ATOM_S6_HW_I2C_BUSY_STATEb1     0x02  
#define ATOM_S6_THERMAL_STATE_CHANGEb1  0x04
#define ATOM_S6_INTERRUPT_SET_BY_BIOSb1 0x08
#define ATOM_S6_REQ_LCD_EXPANSION_FULLb1        0x10    
#define ATOM_S6_REQ_LCD_EXPANSION_ASPEC_RATIOb1 0x20 

#define ATOM_S6_ACC_REQ_CRT1b2          0x01
#define ATOM_S6_ACC_REQ_LCD1b2          0x02
#define ATOM_S6_ACC_REQ_TV1b2           0x04
#define ATOM_S6_ACC_REQ_DFP1b2          0x08
#define ATOM_S6_ACC_REQ_CRT2b2          0x10
#define ATOM_S6_ACC_REQ_LCD2b2          0x20
#define ATOM_S6_ACC_REQ_DFP6b2          0x40
#define ATOM_S6_ACC_REQ_DFP2b2          0x80
#define ATOM_S6_ACC_REQ_CVb3            0x01
#define ATOM_S6_ACC_REQ_DFP3b3          0x02
#define ATOM_S6_ACC_REQ_DFP4b3          0x04
#define ATOM_S6_ACC_REQ_DFP5b3          0x08

#define ATOM_S6_ACC_REQ_DEVICEw1        ATOM_S5_DOS_REQ_DEVICEw0
#define ATOM_S6_SYSTEM_POWER_MODE_CHANGEb3 0x10
#define ATOM_S6_ACC_BLOCK_DISPLAY_SWITCHb3 0x20
#define ATOM_S6_VRI_BRIGHTNESS_CHANGEb3    0x40
#define ATOM_S6_CONFIG_DISPLAY_CHANGEb3    0x80

#define ATOM_S6_DEVICE_CHANGE_SHIFT             0
#define ATOM_S6_SCALER_CHANGE_SHIFT             1
#define ATOM_S6_LID_CHANGE_SHIFT                2
#define ATOM_S6_DOCKING_CHANGE_SHIFT            3
#define ATOM_S6_ACC_MODE_SHIFT                  4
#define ATOM_S6_EXT_DESKTOP_MODE_SHIFT          5
#define ATOM_S6_LID_STATE_SHIFT                 6
#define ATOM_S6_DOCK_STATE_SHIFT                7
#define ATOM_S6_CRITICAL_STATE_SHIFT            8
#define ATOM_S6_HW_I2C_BUSY_STATE_SHIFT         9
#define ATOM_S6_THERMAL_STATE_CHANGE_SHIFT      10
#define ATOM_S6_INTERRUPT_SET_BY_BIOS_SHIFT     11
#define ATOM_S6_REQ_SCALER_SHIFT                12
#define ATOM_S6_REQ_SCALER_ARATIO_SHIFT         13
#define ATOM_S6_DISPLAY_STATE_CHANGE_SHIFT      14
#define ATOM_S6_I2C_STATE_CHANGE_SHIFT          15
#define ATOM_S6_SYSTEM_POWER_MODE_CHANGE_SHIFT  28
#define ATOM_S6_ACC_BLOCK_DISPLAY_SWITCH_SHIFT  29
#define ATOM_S6_VRI_BRIGHTNESS_CHANGE_SHIFT     30
#define ATOM_S6_CONFIG_DISPLAY_CHANGE_SHIFT     31

// BIOS_7_SCRATCH Definition, BIOS_7_SCRATCH is used by Firmware only !!!!
#define ATOM_S7_DOS_MODE_TYPEb0             0x03
#define ATOM_S7_DOS_MODE_VGAb0              0x00
#define ATOM_S7_DOS_MODE_VESAb0             0x01
#define ATOM_S7_DOS_MODE_EXTb0              0x02
#define ATOM_S7_DOS_MODE_PIXEL_DEPTHb0      0x0C
#define ATOM_S7_DOS_MODE_PIXEL_FORMATb0     0xF0
#define ATOM_S7_DOS_8BIT_DAC_ENb1           0x01
#define ATOM_S7_ASIC_INIT_COMPLETEb1        0x02
#define ATOM_S7_ASIC_INIT_COMPLETE_MASK     0x00000200
#define ATOM_S7_DOS_MODE_NUMBERw1           0x0FFFF

#define ATOM_S7_DOS_8BIT_DAC_EN_SHIFT       8

// BIOS_8_SCRATCH Definition
#define ATOM_S8_I2C_CHANNEL_BUSY_MASK       0x00000FFFF
#define ATOM_S8_I2C_HW_ENGINE_BUSY_MASK     0x0FFFF0000   

#define ATOM_S8_I2C_CHANNEL_BUSY_SHIFT      0
#define ATOM_S8_I2C_ENGINE_BUSY_SHIFT       16

// BIOS_9_SCRATCH Definition
#ifndef ATOM_S9_I2C_CHANNEL_COMPLETED_MASK 
#define ATOM_S9_I2C_CHANNEL_COMPLETED_MASK  0x0000FFFF
#endif
#ifndef ATOM_S9_I2C_CHANNEL_ABORTED_MASK  
#define ATOM_S9_I2C_CHANNEL_ABORTED_MASK    0xFFFF0000
#endif
#ifndef ATOM_S9_I2C_CHANNEL_COMPLETED_SHIFT 
#define ATOM_S9_I2C_CHANNEL_COMPLETED_SHIFT 0
#endif
#ifndef ATOM_S9_I2C_CHANNEL_ABORTED_SHIFT   
#define ATOM_S9_I2C_CHANNEL_ABORTED_SHIFT   16
#endif

 
#define ATOM_FLAG_SET                         0x20
#define ATOM_FLAG_CLEAR                       0
#define CLEAR_ATOM_S6_ACC_MODE                ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_ACC_MODE_SHIFT | ATOM_FLAG_CLEAR)
#define SET_ATOM_S6_DEVICE_CHANGE             ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_DEVICE_CHANGE_SHIFT | ATOM_FLAG_SET)
#define SET_ATOM_S6_VRI_BRIGHTNESS_CHANGE     ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_VRI_BRIGHTNESS_CHANGE_SHIFT | ATOM_FLAG_SET)
#define SET_ATOM_S6_SCALER_CHANGE             ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_SCALER_CHANGE_SHIFT | ATOM_FLAG_SET)
#define SET_ATOM_S6_LID_CHANGE                ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_LID_CHANGE_SHIFT | ATOM_FLAG_SET)

#define SET_ATOM_S6_LID_STATE                 ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_LID_STATE_SHIFT | ATOM_FLAG_SET)
#define CLEAR_ATOM_S6_LID_STATE               ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_LID_STATE_SHIFT | ATOM_FLAG_CLEAR)

#define SET_ATOM_S6_DOCK_CHANGE			          ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_DOCKING_CHANGE_SHIFT | ATOM_FLAG_SET)
#define SET_ATOM_S6_DOCK_STATE                ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_DOCK_STATE_SHIFT | ATOM_FLAG_SET)
#define CLEAR_ATOM_S6_DOCK_STATE              ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_DOCK_STATE_SHIFT | ATOM_FLAG_CLEAR)

#define SET_ATOM_S6_THERMAL_STATE_CHANGE      ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_THERMAL_STATE_CHANGE_SHIFT | ATOM_FLAG_SET)
#define SET_ATOM_S6_SYSTEM_POWER_MODE_CHANGE  ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_SYSTEM_POWER_MODE_CHANGE_SHIFT | ATOM_FLAG_SET)
#define SET_ATOM_S6_INTERRUPT_SET_BY_BIOS     ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_INTERRUPT_SET_BY_BIOS_SHIFT | ATOM_FLAG_SET)

#define SET_ATOM_S6_CRITICAL_STATE            ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_CRITICAL_STATE_SHIFT | ATOM_FLAG_SET)
#define CLEAR_ATOM_S6_CRITICAL_STATE          ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_CRITICAL_STATE_SHIFT | ATOM_FLAG_CLEAR)

#define SET_ATOM_S6_REQ_SCALER                ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_REQ_SCALER_SHIFT | ATOM_FLAG_SET)  
#define CLEAR_ATOM_S6_REQ_SCALER              ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_REQ_SCALER_SHIFT | ATOM_FLAG_CLEAR )

#define SET_ATOM_S6_REQ_SCALER_ARATIO         ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_REQ_SCALER_ARATIO_SHIFT | ATOM_FLAG_SET )
#define CLEAR_ATOM_S6_REQ_SCALER_ARATIO       ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_REQ_SCALER_ARATIO_SHIFT | ATOM_FLAG_CLEAR )

#define SET_ATOM_S6_I2C_STATE_CHANGE          ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_I2C_STATE_CHANGE_SHIFT | ATOM_FLAG_SET )

#define SET_ATOM_S6_DISPLAY_STATE_CHANGE      ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_DISPLAY_STATE_CHANGE_SHIFT | ATOM_FLAG_SET )

#define SET_ATOM_S6_DEVICE_RECONFIG           ((ATOM_ACC_CHANGE_INFO_DEF << 8 )|ATOM_S6_CONFIG_DISPLAY_CHANGE_SHIFT | ATOM_FLAG_SET)
#define CLEAR_ATOM_S0_LCD1                    ((ATOM_DEVICE_CONNECT_INFO_DEF << 8 )|  ATOM_S0_LCD1_SHIFT | ATOM_FLAG_CLEAR )
#define SET_ATOM_S7_DOS_8BIT_DAC_EN           ((ATOM_DOS_MODE_INFO_DEF << 8 )|ATOM_S7_DOS_8BIT_DAC_EN_SHIFT | ATOM_FLAG_SET )
#define CLEAR_ATOM_S7_DOS_8BIT_DAC_EN         ((ATOM_DOS_MODE_INFO_DEF << 8 )|ATOM_S7_DOS_8BIT_DAC_EN_SHIFT | ATOM_FLAG_CLEAR )

/****************************************************************************/	
//Portion II: Definitions only used in Driver
/****************************************************************************/

// Macros used by driver
#ifdef __cplusplus
#define GetIndexIntoMasterTable(MasterOrData, FieldName) ((reinterpret_cast<char*>(&(static_cast<ATOM_MASTER_LIST_OF_##MasterOrData##_TABLES*>(0))->FieldName)-static_cast<char*>(0))/sizeof(USHORT))

#define GET_COMMAND_TABLE_COMMANDSET_REVISION(TABLE_HEADER_OFFSET) (((static_cast<ATOM_COMMON_TABLE_HEADER*>(TABLE_HEADER_OFFSET))->ucTableFormatRevision )&0x3F)
#define GET_COMMAND_TABLE_PARAMETER_REVISION(TABLE_HEADER_OFFSET)  (((static_cast<ATOM_COMMON_TABLE_HEADER*>(TABLE_HEADER_OFFSET))->ucTableContentRevision)&0x3F)
#else // not __cplusplus
#define	GetIndexIntoMasterTable(MasterOrData, FieldName) (offsetof(ATOM_MASTER_LIST_OF_##MasterOrData##_TABLES, FieldName)/sizeof(USHORT))

#define GET_COMMAND_TABLE_COMMANDSET_REVISION(TABLE_HEADER_OFFSET) ((((ATOM_COMMON_TABLE_HEADER*)TABLE_HEADER_OFFSET)->ucTableFormatRevision)&0x3F)
#define GET_COMMAND_TABLE_PARAMETER_REVISION(TABLE_HEADER_OFFSET)  ((((ATOM_COMMON_TABLE_HEADER*)TABLE_HEADER_OFFSET)->ucTableContentRevision)&0x3F)
#endif // __cplusplus

#define GET_DATA_TABLE_MAJOR_REVISION GET_COMMAND_TABLE_COMMANDSET_REVISION
#define GET_DATA_TABLE_MINOR_REVISION GET_COMMAND_TABLE_PARAMETER_REVISION

/****************************************************************************/	
//Portion III: Definitinos only used in VBIOS
/****************************************************************************/
#define ATOM_DAC_SRC					0x80
#define ATOM_SRC_DAC1					0
#define ATOM_SRC_DAC2					0x80

typedef struct _MEMORY_PLLINIT_PARAMETERS
{
  ULONG ulTargetMemoryClock; //In 10Khz unit
  UCHAR   ucAction;					 //not define yet
  UCHAR   ucFbDiv_Hi;				 //Fbdiv Hi byte
  UCHAR   ucFbDiv;					 //FB value
  UCHAR   ucPostDiv;				 //Post div
}MEMORY_PLLINIT_PARAMETERS;

#define MEMORY_PLLINIT_PS_ALLOCATION  MEMORY_PLLINIT_PARAMETERS


#define	GPIO_PIN_WRITE													0x01			
#define	GPIO_PIN_READ														0x00

typedef struct  _GPIO_PIN_CONTROL_PARAMETERS
{
  UCHAR ucGPIO_ID;           //return value, read from GPIO pins
  UCHAR ucGPIOBitShift;	     //define which bit in uGPIOBitVal need to be update 
	UCHAR ucGPIOBitVal;		     //Set/Reset corresponding bit defined in ucGPIOBitMask
  UCHAR ucAction;				     //=GPIO_PIN_WRITE: Read; =GPIO_PIN_READ: Write
}GPIO_PIN_CONTROL_PARAMETERS;

typedef struct _ENABLE_SCALER_PARAMETERS
{
  UCHAR ucScaler;            // ATOM_SCALER1, ATOM_SCALER2
  UCHAR ucEnable;            // ATOM_SCALER_DISABLE or ATOM_SCALER_CENTER or ATOM_SCALER_EXPANSION
  UCHAR ucTVStandard;        // 
  UCHAR ucPadding[1];
}ENABLE_SCALER_PARAMETERS; 
#define ENABLE_SCALER_PS_ALLOCATION ENABLE_SCALER_PARAMETERS 

//ucEnable:
#define SCALER_BYPASS_AUTO_CENTER_NO_REPLICATION    0
#define SCALER_BYPASS_AUTO_CENTER_AUTO_REPLICATION  1
#define SCALER_ENABLE_2TAP_ALPHA_MODE               2
#define SCALER_ENABLE_MULTITAP_MODE                 3

typedef struct _ENABLE_HARDWARE_ICON_CURSOR_PARAMETERS
{
  ULONG  usHWIconHorzVertPosn;        // Hardware Icon Vertical position
  UCHAR  ucHWIconVertOffset;          // Hardware Icon Vertical offset
  UCHAR  ucHWIconHorzOffset;          // Hardware Icon Horizontal offset
  UCHAR  ucSelection;                 // ATOM_CURSOR1 or ATOM_ICON1 or ATOM_CURSOR2 or ATOM_ICON2
  UCHAR  ucEnable;                    // ATOM_ENABLE or ATOM_DISABLE
}ENABLE_HARDWARE_ICON_CURSOR_PARAMETERS;

typedef struct _ENABLE_HARDWARE_ICON_CURSOR_PS_ALLOCATION
{
  ENABLE_HARDWARE_ICON_CURSOR_PARAMETERS  sEnableIcon;
  ENABLE_CRTC_PARAMETERS                  sReserved;  
}ENABLE_HARDWARE_ICON_CURSOR_PS_ALLOCATION;

typedef struct _ENABLE_GRAPH_SURFACE_PARAMETERS
{
  USHORT usHight;                     // Image Hight
  USHORT usWidth;                     // Image Width
  UCHAR  ucSurface;                   // Surface 1 or 2	
  UCHAR  ucPadding[3];
}ENABLE_GRAPH_SURFACE_PARAMETERS;

typedef struct _ENABLE_GRAPH_SURFACE_PARAMETERS_V1_2
{
  USHORT usHight;                     // Image Hight
  USHORT usWidth;                     // Image Width
  UCHAR  ucSurface;                   // Surface 1 or 2
  UCHAR  ucEnable;                    // ATOM_ENABLE or ATOM_DISABLE
  UCHAR  ucPadding[2];
}ENABLE_GRAPH_SURFACE_PARAMETERS_V1_2;

typedef struct _ENABLE_GRAPH_SURFACE_PARAMETERS_V1_3
{
  USHORT usHight;                     // Image Hight
  USHORT usWidth;                     // Image Width
  UCHAR  ucSurface;                   // Surface 1 or 2
  UCHAR  ucEnable;                    // ATOM_ENABLE or ATOM_DISABLE
  USHORT usDeviceId;                  // Active Device Id for this surface. If no device, set to 0. 
}ENABLE_GRAPH_SURFACE_PARAMETERS_V1_3;

typedef struct _ENABLE_GRAPH_SURFACE_PARAMETERS_V1_4
{
  USHORT usHight;                     // Image Hight
  USHORT usWidth;                     // Image Width
  USHORT usGraphPitch;
  UCHAR  ucColorDepth;
  UCHAR  ucPixelFormat;
  UCHAR  ucSurface;                   // Surface 1 or 2
  UCHAR  ucEnable;                    // ATOM_ENABLE or ATOM_DISABLE
  UCHAR  ucModeType;
  UCHAR  ucReserved;
}ENABLE_GRAPH_SURFACE_PARAMETERS_V1_4;

// ucEnable
#define ATOM_GRAPH_CONTROL_SET_PITCH             0x0f
#define ATOM_GRAPH_CONTROL_SET_DISP_START        0x10

typedef struct _ENABLE_GRAPH_SURFACE_PS_ALLOCATION
{
  ENABLE_GRAPH_SURFACE_PARAMETERS sSetSurface;          
  ENABLE_YUV_PS_ALLOCATION        sReserved; // Don't set this one
}ENABLE_GRAPH_SURFACE_PS_ALLOCATION;

typedef struct _MEMORY_CLEAN_UP_PARAMETERS
{
  USHORT  usMemoryStart;                //in 8Kb boundary, offset from memory base address
  USHORT  usMemorySize;                 //8Kb blocks aligned
}MEMORY_CLEAN_UP_PARAMETERS;
#define MEMORY_CLEAN_UP_PS_ALLOCATION MEMORY_CLEAN_UP_PARAMETERS

typedef struct  _GET_DISPLAY_SURFACE_SIZE_PARAMETERS
{
  USHORT  usX_Size;                     //When use as input parameter, usX_Size indicates which CRTC                 
  USHORT  usY_Size;
}GET_DISPLAY_SURFACE_SIZE_PARAMETERS; 

typedef struct  _GET_DISPLAY_SURFACE_SIZE_PARAMETERS_V2
{
  union{
    USHORT  usX_Size;                     //When use as input parameter, usX_Size indicates which CRTC                 
    USHORT  usSurface; 
  };
  USHORT usY_Size;
  USHORT usDispXStart;               
  USHORT usDispYStart;
}GET_DISPLAY_SURFACE_SIZE_PARAMETERS_V2; 


typedef struct _PALETTE_DATA_CONTROL_PARAMETERS_V3 
{
  UCHAR  ucLutId;
  UCHAR  ucAction;
  USHORT usLutStartIndex;
  USHORT usLutLength;
  USHORT usLutOffsetInVram;
}PALETTE_DATA_CONTROL_PARAMETERS_V3;

// ucAction:
#define PALETTE_DATA_AUTO_FILL            1
#define PALETTE_DATA_READ                 2
#define PALETTE_DATA_WRITE                3


typedef struct _INTERRUPT_SERVICE_PARAMETERS_V2
{
  UCHAR  ucInterruptId;
  UCHAR  ucServiceId;
  UCHAR  ucStatus;
  UCHAR  ucReserved;
}INTERRUPT_SERVICE_PARAMETER_V2;

// ucInterruptId
#define HDP1_INTERRUPT_ID                 1
#define HDP2_INTERRUPT_ID                 2
#define HDP3_INTERRUPT_ID                 3
#define HDP4_INTERRUPT_ID                 4
#define HDP5_INTERRUPT_ID                 5
#define HDP6_INTERRUPT_ID                 6
#define SW_INTERRUPT_ID                   11   

// ucAction
#define INTERRUPT_SERVICE_GEN_SW_INT      1
#define INTERRUPT_SERVICE_GET_STATUS      2

 // ucStatus
#define INTERRUPT_STATUS__INT_TRIGGER     1
#define INTERRUPT_STATUS__HPD_HIGH        2

typedef struct _INDIRECT_IO_ACCESS
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  UCHAR                    IOAccessSequence[256];
} INDIRECT_IO_ACCESS;

#define INDIRECT_READ              0x00
#define INDIRECT_WRITE             0x80

#define INDIRECT_IO_MM             0
#define INDIRECT_IO_PLL            1
#define INDIRECT_IO_MC             2
#define INDIRECT_IO_PCIE           3
#define INDIRECT_IO_PCIEP          4
#define INDIRECT_IO_NBMISC         5
#define INDIRECT_IO_SMU            5

#define INDIRECT_IO_PLL_READ       INDIRECT_IO_PLL   | INDIRECT_READ
#define INDIRECT_IO_PLL_WRITE      INDIRECT_IO_PLL   | INDIRECT_WRITE
#define INDIRECT_IO_MC_READ        INDIRECT_IO_MC    | INDIRECT_READ
#define INDIRECT_IO_MC_WRITE       INDIRECT_IO_MC    | INDIRECT_WRITE
#define INDIRECT_IO_PCIE_READ      INDIRECT_IO_PCIE  | INDIRECT_READ
#define INDIRECT_IO_PCIE_WRITE     INDIRECT_IO_PCIE  | INDIRECT_WRITE
#define INDIRECT_IO_PCIEP_READ     INDIRECT_IO_PCIEP | INDIRECT_READ
#define INDIRECT_IO_PCIEP_WRITE    INDIRECT_IO_PCIEP | INDIRECT_WRITE
#define INDIRECT_IO_NBMISC_READ    INDIRECT_IO_NBMISC | INDIRECT_READ
#define INDIRECT_IO_NBMISC_WRITE   INDIRECT_IO_NBMISC | INDIRECT_WRITE
#define INDIRECT_IO_SMU_READ       INDIRECT_IO_SMU | INDIRECT_READ
#define INDIRECT_IO_SMU_WRITE      INDIRECT_IO_SMU | INDIRECT_WRITE

typedef struct _ATOM_OEM_INFO
{ 
  ATOM_COMMON_TABLE_HEADER	sHeader;
  ATOM_I2C_ID_CONFIG_ACCESS sucI2cId;
}ATOM_OEM_INFO;

typedef struct _ATOM_TV_MODE
{
   UCHAR	ucVMode_Num;			  //Video mode number
   UCHAR	ucTV_Mode_Num;			//Internal TV mode number
}ATOM_TV_MODE;

typedef struct _ATOM_BIOS_INT_TVSTD_MODE
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
   USHORT	usTV_Mode_LUT_Offset;	// Pointer to standard to internal number conversion table
   USHORT	usTV_FIFO_Offset;		  // Pointer to FIFO entry table
   USHORT	usNTSC_Tbl_Offset;		// Pointer to SDTV_Mode_NTSC table
   USHORT	usPAL_Tbl_Offset;		  // Pointer to SDTV_Mode_PAL table 
   USHORT	usCV_Tbl_Offset;		  // Pointer to SDTV_Mode_PAL table 
}ATOM_BIOS_INT_TVSTD_MODE;


typedef struct _ATOM_TV_MODE_SCALER_PTR
{
   USHORT	ucFilter0_Offset;		//Pointer to filter format 0 coefficients
   USHORT	usFilter1_Offset;		//Pointer to filter format 0 coefficients
   UCHAR	ucTV_Mode_Num;
}ATOM_TV_MODE_SCALER_PTR;

typedef struct _ATOM_STANDARD_VESA_TIMING
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  ATOM_DTD_FORMAT 				 aModeTimings[16];      // 16 is not the real array number, just for initial allocation
}ATOM_STANDARD_VESA_TIMING;


typedef struct _ATOM_STD_FORMAT
{ 
  USHORT    usSTD_HDisp;
  USHORT    usSTD_VDisp;
  USHORT    usSTD_RefreshRate;
  USHORT    usReserved;
}ATOM_STD_FORMAT;

typedef struct _ATOM_VESA_TO_EXTENDED_MODE
{
  USHORT  usVESA_ModeNumber;
  USHORT  usExtendedModeNumber;
}ATOM_VESA_TO_EXTENDED_MODE;

typedef struct _ATOM_VESA_TO_INTENAL_MODE_LUT
{ 
  ATOM_COMMON_TABLE_HEADER   sHeader;  
  ATOM_VESA_TO_EXTENDED_MODE asVESA_ToExtendedModeInfo[76];
}ATOM_VESA_TO_INTENAL_MODE_LUT;

/*************** ATOM Memory Related Data Structure ***********************/
typedef struct _ATOM_MEMORY_VENDOR_BLOCK{
	UCHAR												ucMemoryType;
	UCHAR												ucMemoryVendor;
	UCHAR												ucAdjMCId;
	UCHAR												ucDynClkId;
	ULONG												ulDllResetClkRange;
}ATOM_MEMORY_VENDOR_BLOCK;


typedef struct _ATOM_MEMORY_SETTING_ID_CONFIG{
#if ATOM_BIG_ENDIAN
	ULONG												ucMemBlkId:8;
	ULONG												ulMemClockRange:24;
#else
	ULONG												ulMemClockRange:24;
	ULONG												ucMemBlkId:8;
#endif
}ATOM_MEMORY_SETTING_ID_CONFIG;

typedef union _ATOM_MEMORY_SETTING_ID_CONFIG_ACCESS
{
  ATOM_MEMORY_SETTING_ID_CONFIG slAccess;
  ULONG                         ulAccess;
}ATOM_MEMORY_SETTING_ID_CONFIG_ACCESS;


typedef struct _ATOM_MEMORY_SETTING_DATA_BLOCK{
	ATOM_MEMORY_SETTING_ID_CONFIG_ACCESS			ulMemoryID;
	ULONG															        aulMemData[1];
}ATOM_MEMORY_SETTING_DATA_BLOCK;


typedef struct _ATOM_INIT_REG_INDEX_FORMAT{
	 USHORT											usRegIndex;                                     // MC register index
	 UCHAR											ucPreRegDataLength;                             // offset in ATOM_INIT_REG_DATA_BLOCK.saRegDataBuf
}ATOM_INIT_REG_INDEX_FORMAT;


typedef struct _ATOM_INIT_REG_BLOCK{
	USHORT													usRegIndexTblSize;													//size of asRegIndexBuf
	USHORT													usRegDataBlkSize;														//size of ATOM_MEMORY_SETTING_DATA_BLOCK
	ATOM_INIT_REG_INDEX_FORMAT			asRegIndexBuf[1];
	ATOM_MEMORY_SETTING_DATA_BLOCK	asRegDataBuf[1];
}ATOM_INIT_REG_BLOCK;

#define END_OF_REG_INDEX_BLOCK  0x0ffff
#define END_OF_REG_DATA_BLOCK   0x00000000
#define ATOM_INIT_REG_MASK_FLAG 0x80               //Not used in BIOS
#define	CLOCK_RANGE_HIGHEST			0x00ffffff

#define VALUE_DWORD             SIZEOF ULONG
#define VALUE_SAME_AS_ABOVE     0
#define VALUE_MASK_DWORD        0x84

#define INDEX_ACCESS_RANGE_BEGIN	    (VALUE_DWORD + 1)
#define INDEX_ACCESS_RANGE_END		    (INDEX_ACCESS_RANGE_BEGIN + 1)
#define VALUE_INDEX_ACCESS_SINGLE	    (INDEX_ACCESS_RANGE_END + 1)
//#define ACCESS_MCIODEBUGIND            0x40       //defined in BIOS code
#define ACCESS_PLACEHOLDER             0x80

typedef struct _ATOM_MC_INIT_PARAM_TABLE
{ 
  ATOM_COMMON_TABLE_HEADER		sHeader;
  USHORT											usAdjustARB_SEQDataOffset;
  USHORT											usMCInitMemTypeTblOffset;
  USHORT											usMCInitCommonTblOffset;
  USHORT											usMCInitPowerDownTblOffset;
	ULONG												ulARB_SEQDataBuf[32];
	ATOM_INIT_REG_BLOCK					asMCInitMemType;
	ATOM_INIT_REG_BLOCK					asMCInitCommon;
}ATOM_MC_INIT_PARAM_TABLE;


#define _4Mx16              0x2
#define _4Mx32              0x3
#define _8Mx16              0x12
#define _8Mx32              0x13
#define _16Mx16             0x22
#define _16Mx32             0x23
#define _32Mx16             0x32
#define _32Mx32             0x33
#define _64Mx8              0x41
#define _64Mx16             0x42
#define _64Mx32             0x43
#define _128Mx8             0x51
#define _128Mx16            0x52
#define _128Mx32            0x53
#define _256Mx8             0x61
#define _256Mx16            0x62
#define _512Mx8             0x71

#define SAMSUNG             0x1
#define INFINEON            0x2
#define ELPIDA              0x3
#define ETRON               0x4
#define NANYA               0x5
#define HYNIX               0x6
#define MOSEL               0x7
#define WINBOND             0x8
#define ESMT                0x9
#define MICRON              0xF

#define QIMONDA             INFINEON
#define PROMOS              MOSEL
#define KRETON              INFINEON
#define ELIXIR              NANYA
#define MEZZA               ELPIDA


/////////////Support for GDDR5 MC uCode to reside in upper 64K of ROM/////////////

#define UCODE_ROM_START_ADDRESS		0x1b800
#define	UCODE_SIGNATURE			0x4375434d // 'MCuC' - MC uCode

//uCode block header for reference

typedef struct _MCuCodeHeader
{
  ULONG  ulSignature;
  UCHAR  ucRevision;
  UCHAR  ucChecksum;
  UCHAR  ucReserved1;
  UCHAR  ucReserved2;
  USHORT usParametersLength;
  USHORT usUCodeLength;
  USHORT usReserved1;
  USHORT usReserved2;
} MCuCodeHeader;

//////////////////////////////////////////////////////////////////////////////////

#define ATOM_MAX_NUMBER_OF_VRAM_MODULE	16

#define ATOM_VRAM_MODULE_MEMORY_VENDOR_ID_MASK	0xF
typedef struct _ATOM_VRAM_MODULE_V1
{
  ULONG                      ulReserved;
  USHORT                     usEMRSValue;  
  USHORT                     usMRSValue;
  USHORT                     usReserved;
  UCHAR                      ucExtMemoryID;     // An external indicator (by hardcode, callback or pin) to tell what is the current memory module
  UCHAR                      ucMemoryType;      // [7:4]=0x1:DDR1;=0x2:DDR2;=0x3:DDR3;=0x4:DDR4;[3:0] reserved;
  UCHAR                      ucMemoryVenderID;  // Predefined,never change across designs or memory type/vender 
  UCHAR                      ucMemoryDeviceCfg; // [7:4]=0x0:4M;=0x1:8M;=0x2:16M;0x3:32M....[3:0]=0x0:x4;=0x1:x8;=0x2:x16;=0x3:x32...
  UCHAR                      ucRow;             // Number of Row,in power of 2;
  UCHAR                      ucColumn;          // Number of Column,in power of 2;
  UCHAR                      ucBank;            // Nunber of Bank;
  UCHAR                      ucRank;            // Number of Rank, in power of 2
  UCHAR                      ucChannelNum;      // Number of channel;
  UCHAR                      ucChannelConfig;   // [3:0]=Indication of what channel combination;[4:7]=Channel bit width, in number of 2
  UCHAR                      ucDefaultMVDDQ_ID; // Default MVDDQ setting for this memory block, ID linking to MVDDQ info table to find real set-up data;
  UCHAR                      ucDefaultMVDDC_ID; // Default MVDDC setting for this memory block, ID linking to MVDDC info table to find real set-up data;
  UCHAR                      ucReserved[2];
}ATOM_VRAM_MODULE_V1;


typedef struct _ATOM_VRAM_MODULE_V2
{
  ULONG                      ulReserved;
  ULONG                      ulFlags;     			// To enable/disable functionalities based on memory type
  ULONG                      ulEngineClock;     // Override of default engine clock for particular memory type
  ULONG                      ulMemoryClock;     // Override of default memory clock for particular memory type
  USHORT                     usEMRS2Value;      // EMRS2 Value is used for GDDR2 and GDDR4 memory type
  USHORT                     usEMRS3Value;      // EMRS3 Value is used for GDDR2 and GDDR4 memory type
  USHORT                     usEMRSValue;  
  USHORT                     usMRSValue;
  USHORT                     usReserved;
  UCHAR                      ucExtMemoryID;     // An external indicator (by hardcode, callback or pin) to tell what is the current memory module
  UCHAR                      ucMemoryType;      // [7:4]=0x1:DDR1;=0x2:DDR2;=0x3:DDR3;=0x4:DDR4;[3:0] - must not be used for now;
  UCHAR                      ucMemoryVenderID;  // Predefined,never change across designs or memory type/vender. If not predefined, vendor detection table gets executed
  UCHAR                      ucMemoryDeviceCfg; // [7:4]=0x0:4M;=0x1:8M;=0x2:16M;0x3:32M....[3:0]=0x0:x4;=0x1:x8;=0x2:x16;=0x3:x32...
  UCHAR                      ucRow;             // Number of Row,in power of 2;
  UCHAR                      ucColumn;          // Number of Column,in power of 2;
  UCHAR                      ucBank;            // Nunber of Bank;
  UCHAR                      ucRank;            // Number of Rank, in power of 2
  UCHAR                      ucChannelNum;      // Number of channel;
  UCHAR                      ucChannelConfig;   // [3:0]=Indication of what channel combination;[4:7]=Channel bit width, in number of 2
  UCHAR                      ucDefaultMVDDQ_ID; // Default MVDDQ setting for this memory block, ID linking to MVDDQ info table to find real set-up data;
  UCHAR                      ucDefaultMVDDC_ID; // Default MVDDC setting for this memory block, ID linking to MVDDC info table to find real set-up data;
  UCHAR                      ucRefreshRateFactor;
  UCHAR                      ucReserved[3];
}ATOM_VRAM_MODULE_V2;


typedef	struct _ATOM_MEMORY_TIMING_FORMAT
{
	ULONG											 ulClkRange;				// memory clock in 10kHz unit, when target memory clock is below this clock, use this memory timing 	
  union{
	  USHORT										 usMRS;							// mode register						
    USHORT                     usDDR3_MR0;
  };
  union{
	  USHORT										 usEMRS;						// extended mode register
    USHORT                     usDDR3_MR1;
  };
	UCHAR											 ucCL;							// CAS latency
	UCHAR											 ucWL;							// WRITE Latency				
	UCHAR											 uctRAS;						// tRAS
	UCHAR											 uctRC;							// tRC	
	UCHAR											 uctRFC;						// tRFC
	UCHAR											 uctRCDR;						// tRCDR	
	UCHAR											 uctRCDW;						// tRCDW
	UCHAR											 uctRP;							// tRP
	UCHAR											 uctRRD;						// tRRD	
	UCHAR											 uctWR;							// tWR
	UCHAR											 uctWTR;						// tWTR
	UCHAR											 uctPDIX;						// tPDIX
	UCHAR											 uctFAW;						// tFAW
	UCHAR											 uctAOND;						// tAOND
  union 
  {
    struct {
	    UCHAR											 ucflag;						// flag to control memory timing calculation. bit0= control EMRS2 Infineon 
	    UCHAR											 ucReserved;						
    };
    USHORT                   usDDR3_MR2;
  };
}ATOM_MEMORY_TIMING_FORMAT;


typedef	struct _ATOM_MEMORY_TIMING_FORMAT_V1
{
	ULONG											 ulClkRange;				// memory clock in 10kHz unit, when target memory clock is below this clock, use this memory timing 	
	USHORT										 usMRS;							// mode register						
	USHORT										 usEMRS;						// extended mode register
	UCHAR											 ucCL;							// CAS latency
	UCHAR											 ucWL;							// WRITE Latency				
	UCHAR											 uctRAS;						// tRAS
	UCHAR											 uctRC;							// tRC	
	UCHAR											 uctRFC;						// tRFC
	UCHAR											 uctRCDR;						// tRCDR	
	UCHAR											 uctRCDW;						// tRCDW
	UCHAR											 uctRP;							// tRP
	UCHAR											 uctRRD;						// tRRD	
	UCHAR											 uctWR;							// tWR
	UCHAR											 uctWTR;						// tWTR
	UCHAR											 uctPDIX;						// tPDIX
	UCHAR											 uctFAW;						// tFAW
	UCHAR											 uctAOND;						// tAOND
	UCHAR											 ucflag;						// flag to control memory timing calculation. bit0= control EMRS2 Infineon 
////////////////////////////////////GDDR parameters///////////////////////////////////
	UCHAR											 uctCCDL;						// 
	UCHAR											 uctCRCRL;						// 
	UCHAR											 uctCRCWL;						// 
	UCHAR											 uctCKE;						// 
	UCHAR											 uctCKRSE;						// 
	UCHAR											 uctCKRSX;						// 
	UCHAR											 uctFAW32;						// 
	UCHAR											 ucMR5lo;					// 
	UCHAR											 ucMR5hi;					// 
	UCHAR											 ucTerminator;
}ATOM_MEMORY_TIMING_FORMAT_V1;

typedef	struct _ATOM_MEMORY_TIMING_FORMAT_V2
{
	ULONG											 ulClkRange;				// memory clock in 10kHz unit, when target memory clock is below this clock, use this memory timing 	
	USHORT										 usMRS;							// mode register						
	USHORT										 usEMRS;						// extended mode register
	UCHAR											 ucCL;							// CAS latency
	UCHAR											 ucWL;							// WRITE Latency				
	UCHAR											 uctRAS;						// tRAS
	UCHAR											 uctRC;							// tRC	
	UCHAR											 uctRFC;						// tRFC
	UCHAR											 uctRCDR;						// tRCDR	
	UCHAR											 uctRCDW;						// tRCDW
	UCHAR											 uctRP;							// tRP
	UCHAR											 uctRRD;						// tRRD	
	UCHAR											 uctWR;							// tWR
	UCHAR											 uctWTR;						// tWTR
	UCHAR											 uctPDIX;						// tPDIX
	UCHAR											 uctFAW;						// tFAW
	UCHAR											 uctAOND;						// tAOND
	UCHAR											 ucflag;						// flag to control memory timing calculation. bit0= control EMRS2 Infineon 
////////////////////////////////////GDDR parameters///////////////////////////////////
	UCHAR											 uctCCDL;						// 
	UCHAR											 uctCRCRL;						// 
	UCHAR											 uctCRCWL;						// 
	UCHAR											 uctCKE;						// 
	UCHAR											 uctCKRSE;						// 
	UCHAR											 uctCKRSX;						// 
	UCHAR											 uctFAW32;						// 
	UCHAR											 ucMR4lo;					// 
	UCHAR											 ucMR4hi;					// 
	UCHAR											 ucMR5lo;					// 
	UCHAR											 ucMR5hi;					// 
	UCHAR											 ucTerminator;
	UCHAR											 ucReserved;	
}ATOM_MEMORY_TIMING_FORMAT_V2;

typedef	struct _ATOM_MEMORY_FORMAT
{
	ULONG											 ulDllDisClock;			// memory DLL will be disable when target memory clock is below this clock
  union{
    USHORT                     usEMRS2Value;      // EMRS2 Value is used for GDDR2 and GDDR4 memory type
    USHORT                     usDDR3_Reserved;   // Not used for DDR3 memory
  };
  union{
    USHORT                     usEMRS3Value;      // EMRS3 Value is used for GDDR2 and GDDR4 memory type
    USHORT                     usDDR3_MR3;        // Used for DDR3 memory
  };
  UCHAR                      ucMemoryType;      // [7:4]=0x1:DDR1;=0x2:DDR2;=0x3:DDR3;=0x4:DDR4;[3:0] - must not be used for now;
  UCHAR                      ucMemoryVenderID;  // Predefined,never change across designs or memory type/vender. If not predefined, vendor detection table gets executed
  UCHAR                      ucRow;             // Number of Row,in power of 2;
  UCHAR                      ucColumn;          // Number of Column,in power of 2;
  UCHAR                      ucBank;            // Nunber of Bank;
  UCHAR                      ucRank;            // Number of Rank, in power of 2
	UCHAR											 ucBurstSize;				// burst size, 0= burst size=4  1= burst size=8
  UCHAR                      ucDllDisBit;				// position of DLL Enable/Disable bit in EMRS ( Extended Mode Register )
  UCHAR                      ucRefreshRateFactor;	// memory refresh rate in unit of ms	
	UCHAR											 ucDensity;					// _8Mx32, _16Mx32, _16Mx16, _32Mx16
	UCHAR											 ucPreamble;				//[7:4] Write Preamble, [3:0] Read Preamble
  UCHAR											 ucMemAttrib;				// Memory Device Addribute, like RDBI/WDBI etc
	ATOM_MEMORY_TIMING_FORMAT	 asMemTiming[5];		//Memory Timing block sort from lower clock to higher clock
}ATOM_MEMORY_FORMAT;


typedef struct _ATOM_VRAM_MODULE_V3
{
	ULONG											 ulChannelMapCfg;		// board dependent paramenter:Channel combination
	USHORT										 usSize;						// size of ATOM_VRAM_MODULE_V3
  USHORT                     usDefaultMVDDQ;		// board dependent parameter:Default Memory Core Voltage
  USHORT                     usDefaultMVDDC;		// board dependent parameter:Default Memory IO Voltage
	UCHAR                      ucExtMemoryID;     // An external indicator (by hardcode, callback or pin) to tell what is the current memory module
  UCHAR                      ucChannelNum;      // board dependent parameter:Number of channel;
	UCHAR											 ucChannelSize;			// board dependent parameter:32bit or 64bit	
	UCHAR											 ucVREFI;						// board dependnt parameter: EXT or INT +160mv to -140mv
	UCHAR											 ucNPL_RT;					// board dependent parameter:NPL round trip delay, used for calculate memory timing parameters
	UCHAR											 ucFlag;						// To enable/disable functionalities based on memory type
	ATOM_MEMORY_FORMAT				 asMemory;					// describ all of video memory parameters from memory spec
}ATOM_VRAM_MODULE_V3;


//ATOM_VRAM_MODULE_V3.ucNPL_RT
#define NPL_RT_MASK															0x0f
#define BATTERY_ODT_MASK												0xc0

#define ATOM_VRAM_MODULE		 ATOM_VRAM_MODULE_V3

typedef struct _ATOM_VRAM_MODULE_V4
{
  ULONG	  ulChannelMapCfg;	                // board dependent parameter: Channel combination
  USHORT  usModuleSize;                     // size of ATOM_VRAM_MODULE_V4, make it easy for VBIOS to look for next entry of VRAM_MODULE
  USHORT  usPrivateReserved;                // BIOS internal reserved space to optimize code size, updated by the compiler, shouldn't be modified manually!!
                                            // MC_ARB_RAMCFG (includes NOOFBANK,NOOFRANKS,NOOFROWS,NOOFCOLS)
  USHORT  usReserved;
  UCHAR   ucExtMemoryID;    		            // An external indicator (by hardcode, callback or pin) to tell what is the current memory module
  UCHAR   ucMemoryType;                     // [7:4]=0x1:DDR1;=0x2:DDR2;=0x3:DDR3;=0x4:DDR4; 0x5:DDR5 [3:0] - Must be 0x0 for now;
  UCHAR   ucChannelNum;                     // Number of channels present in this module config
  UCHAR   ucChannelWidth;                   // 0 - 32 bits; 1 - 64 bits
	UCHAR   ucDensity;                        // _8Mx32, _16Mx32, _16Mx16, _32Mx16
	UCHAR	  ucFlag;						                // To enable/disable functionalities based on memory type
	UCHAR	  ucMisc;						                // bit0: 0 - single rank; 1 - dual rank;   bit2: 0 - burstlength 4, 1 - burstlength 8
  UCHAR		ucVREFI;                          // board dependent parameter
  UCHAR   ucNPL_RT;                         // board dependent parameter:NPL round trip delay, used for calculate memory timing parameters
  UCHAR		ucPreamble;                       // [7:4] Write Preamble, [3:0] Read Preamble
  UCHAR   ucMemorySize;                     // BIOS internal reserved space to optimize code size, updated by the compiler, shouldn't be modified manually!!
                                            // Total memory size in unit of 16MB for CONFIG_MEMSIZE - bit[23:0] zeros
  UCHAR   ucReserved[3];

//compare with V3, we flat the struct by merging ATOM_MEMORY_FORMAT (as is) into V4 as the same level
  union{
    USHORT	usEMRS2Value;                   // EMRS2 Value is used for GDDR2 and GDDR4 memory type
    USHORT  usDDR3_Reserved;
  };
  union{
    USHORT	usEMRS3Value;                   // EMRS3 Value is used for GDDR2 and GDDR4 memory type
    USHORT  usDDR3_MR3;                     // Used for DDR3 memory
  };  
  UCHAR   ucMemoryVenderID;  		            // Predefined, If not predefined, vendor detection table gets executed
  UCHAR	  ucRefreshRateFactor;              // [1:0]=RefreshFactor (00=8ms, 01=16ms, 10=32ms,11=64ms)
  UCHAR   ucReserved2[2];
  ATOM_MEMORY_TIMING_FORMAT  asMemTiming[5];//Memory Timing block sort from lower clock to higher clock
}ATOM_VRAM_MODULE_V4;

#define VRAM_MODULE_V4_MISC_RANK_MASK       0x3
#define VRAM_MODULE_V4_MISC_DUAL_RANK       0x1
#define VRAM_MODULE_V4_MISC_BL_MASK         0x4
#define VRAM_MODULE_V4_MISC_BL8             0x4
#define VRAM_MODULE_V4_MISC_DUAL_CS         0x10

typedef struct _ATOM_VRAM_MODULE_V5
{
  ULONG	  ulChannelMapCfg;	                // board dependent parameter: Channel combination
  USHORT  usModuleSize;                     // size of ATOM_VRAM_MODULE_V4, make it easy for VBIOS to look for next entry of VRAM_MODULE
  USHORT  usPrivateReserved;                // BIOS internal reserved space to optimize code size, updated by the compiler, shouldn't be modified manually!!
                                            // MC_ARB_RAMCFG (includes NOOFBANK,NOOFRANKS,NOOFROWS,NOOFCOLS)
  USHORT  usReserved;
  UCHAR   ucExtMemoryID;    		            // An external indicator (by hardcode, callback or pin) to tell what is the current memory module
  UCHAR   ucMemoryType;                     // [7:4]=0x1:DDR1;=0x2:DDR2;=0x3:DDR3;=0x4:DDR4; 0x5:DDR5 [3:0] - Must be 0x0 for now;
  UCHAR   ucChannelNum;                     // Number of channels present in this module config
  UCHAR   ucChannelWidth;                   // 0 - 32 bits; 1 - 64 bits
	UCHAR   ucDensity;                        // _8Mx32, _16Mx32, _16Mx16, _32Mx16
	UCHAR	  ucFlag;						                // To enable/disable functionalities based on memory type
	UCHAR	  ucMisc;						                // bit0: 0 - single rank; 1 - dual rank;   bit2: 0 - burstlength 4, 1 - burstlength 8
  UCHAR		ucVREFI;                          // board dependent parameter
  UCHAR   ucNPL_RT;                         // board dependent parameter:NPL round trip delay, used for calculate memory timing parameters
  UCHAR		ucPreamble;                       // [7:4] Write Preamble, [3:0] Read Preamble
  UCHAR   ucMemorySize;                     // BIOS internal reserved space to optimize code size, updated by the compiler, shouldn't be modified manually!!
                                            // Total memory size in unit of 16MB for CONFIG_MEMSIZE - bit[23:0] zeros
  UCHAR   ucReserved[3];

//compare with V3, we flat the struct by merging ATOM_MEMORY_FORMAT (as is) into V4 as the same level
  USHORT	usEMRS2Value;      		            // EMRS2 Value is used for GDDR2 and GDDR4 memory type
  USHORT	usEMRS3Value;      		            // EMRS3 Value is used for GDDR2 and GDDR4 memory type
  UCHAR   ucMemoryVenderID;  		            // Predefined, If not predefined, vendor detection table gets executed
  UCHAR	  ucRefreshRateFactor;              // [1:0]=RefreshFactor (00=8ms, 01=16ms, 10=32ms,11=64ms)
  UCHAR	  ucFIFODepth;			                // FIFO depth supposes to be detected during vendor detection, but if we dont do vendor detection we have to hardcode FIFO Depth
  UCHAR   ucCDR_Bandwidth;		   // [0:3]=Read CDR bandwidth, [4:7] - Write CDR Bandwidth
  ATOM_MEMORY_TIMING_FORMAT_V1  asMemTiming[5];//Memory Timing block sort from lower clock to higher clock
}ATOM_VRAM_MODULE_V5;

typedef struct _ATOM_VRAM_MODULE_V6
{
  ULONG	  ulChannelMapCfg;	                // board dependent parameter: Channel combination
  USHORT  usModuleSize;                     // size of ATOM_VRAM_MODULE_V4, make it easy for VBIOS to look for next entry of VRAM_MODULE
  USHORT  usPrivateReserved;                // BIOS internal reserved space to optimize code size, updated by the compiler, shouldn't be modified manually!!
                                            // MC_ARB_RAMCFG (includes NOOFBANK,NOOFRANKS,NOOFROWS,NOOFCOLS)
  USHORT  usReserved;
  UCHAR   ucExtMemoryID;    		            // An external indicator (by hardcode, callback or pin) to tell what is the current memory module
  UCHAR   ucMemoryType;                     // [7:4]=0x1:DDR1;=0x2:DDR2;=0x3:DDR3;=0x4:DDR4; 0x5:DDR5 [3:0] - Must be 0x0 for now;
  UCHAR   ucChannelNum;                     // Number of channels present in this module config
  UCHAR   ucChannelWidth;                   // 0 - 32 bits; 1 - 64 bits
	UCHAR   ucDensity;                        // _8Mx32, _16Mx32, _16Mx16, _32Mx16
	UCHAR	  ucFlag;						                // To enable/disable functionalities based on memory type
	UCHAR	  ucMisc;						                // bit0: 0 - single rank; 1 - dual rank;   bit2: 0 - burstlength 4, 1 - burstlength 8
  UCHAR		ucVREFI;                          // board dependent parameter
  UCHAR   ucNPL_RT;                         // board dependent parameter:NPL round trip delay, used for calculate memory timing parameters
  UCHAR		ucPreamble;                       // [7:4] Write Preamble, [3:0] Read Preamble
  UCHAR   ucMemorySize;                     // BIOS internal reserved space to optimize code size, updated by the compiler, shouldn't be modified manually!!
                                            // Total memory size in unit of 16MB for CONFIG_MEMSIZE - bit[23:0] zeros
  UCHAR   ucReserved[3];

//compare with V3, we flat the struct by merging ATOM_MEMORY_FORMAT (as is) into V4 as the same level
  USHORT	usEMRS2Value;      		            // EMRS2 Value is used for GDDR2 and GDDR4 memory type
  USHORT	usEMRS3Value;      		            // EMRS3 Value is used for GDDR2 and GDDR4 memory type
  UCHAR   ucMemoryVenderID;  		            // Predefined, If not predefined, vendor detection table gets executed
  UCHAR	  ucRefreshRateFactor;              // [1:0]=RefreshFactor (00=8ms, 01=16ms, 10=32ms,11=64ms)
  UCHAR	  ucFIFODepth;			                // FIFO depth supposes to be detected during vendor detection, but if we dont do vendor detection we have to hardcode FIFO Depth
  UCHAR   ucCDR_Bandwidth;		   // [0:3]=Read CDR bandwidth, [4:7] - Write CDR Bandwidth
  ATOM_MEMORY_TIMING_FORMAT_V2  asMemTiming[5];//Memory Timing block sort from lower clock to higher clock
}ATOM_VRAM_MODULE_V6;

typedef struct _ATOM_VRAM_MODULE_V7
{
// Design Specific Values
  ULONG	  ulChannelMapCfg;	                // mmMC_SHARED_CHREMAP
  USHORT  usModuleSize;                     // Size of ATOM_VRAM_MODULE_V7
  USHORT  usPrivateReserved;                // MC_ARB_RAMCFG (includes NOOFBANK,NOOFRANKS,NOOFROWS,NOOFCOLS)
  USHORT  usEnableChannels;                 // bit vector which indicate which channels are enabled
  UCHAR   ucExtMemoryID;                    // Current memory module ID
  UCHAR   ucMemoryType;                     // MEM_TYPE_DDR2/DDR3/GDDR3/GDDR5
  UCHAR   ucChannelNum;                     // Number of mem. channels supported in this module
  UCHAR   ucChannelWidth;                   // CHANNEL_16BIT/CHANNEL_32BIT/CHANNEL_64BIT
  UCHAR   ucDensity;                        // _8Mx32, _16Mx32, _16Mx16, _32Mx16
  UCHAR	  ucReserve;                        // Former container for Mx_FLAGS like DBI_AC_MODE_ENABLE_ASIC for GDDR4. Not used now.
  UCHAR	  ucMisc;                           // RANK_OF_THISMEMORY etc.
  UCHAR	  ucVREFI;                          // Not used.
  UCHAR   ucNPL_RT;                         // Round trip delay (MC_SEQ_CAS_TIMING [28:24]:TCL=CL+NPL_RT-2). Always 2.
  UCHAR	  ucPreamble;                       // [7:4] Write Preamble, [3:0] Read Preamble
  UCHAR   ucMemorySize;                     // Total memory size in unit of 16MB for CONFIG_MEMSIZE - bit[23:0] zeros
  USHORT  usSEQSettingOffset;
  UCHAR   ucReserved;
// Memory Module specific values
  USHORT  usEMRS2Value;                     // EMRS2/MR2 Value. 
  USHORT  usEMRS3Value;                     // EMRS3/MR3 Value.
  UCHAR   ucMemoryVenderID;                 // [7:4] Revision, [3:0] Vendor code
  UCHAR	  ucRefreshRateFactor;              // [1:0]=RefreshFactor (00=8ms, 01=16ms, 10=32ms,11=64ms)
  UCHAR	  ucFIFODepth;                      // FIFO depth can be detected during vendor detection, here is hardcoded per memory
  UCHAR   ucCDR_Bandwidth;                  // [0:3]=Read CDR bandwidth, [4:7] - Write CDR Bandwidth
  char    strMemPNString[20];               // part number end with '0'. 
}ATOM_VRAM_MODULE_V7;

typedef struct _ATOM_VRAM_INFO_V2
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  UCHAR                      ucNumOfVRAMModule;
  ATOM_VRAM_MODULE           aVramInfo[ATOM_MAX_NUMBER_OF_VRAM_MODULE];      // just for allocation, real number of blocks is in ucNumOfVRAMModule;
}ATOM_VRAM_INFO_V2;

typedef struct _ATOM_VRAM_INFO_V3
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
	USHORT										 usMemAdjustTblOffset;													 // offset of ATOM_INIT_REG_BLOCK structure for memory vendor specific MC adjust setting
	USHORT										 usMemClkPatchTblOffset;												 //	offset of ATOM_INIT_REG_BLOCK structure for memory clock specific MC setting
	USHORT										 usRerseved;
	UCHAR           	         aVID_PinsShift[9];															 // 8 bit strap maximum+terminator
  UCHAR                      ucNumOfVRAMModule;
  ATOM_VRAM_MODULE		       aVramInfo[ATOM_MAX_NUMBER_OF_VRAM_MODULE];      // just for allocation, real number of blocks is in ucNumOfVRAMModule;
	ATOM_INIT_REG_BLOCK				 asMemPatch;																		 // for allocation
																																						 //	ATOM_INIT_REG_BLOCK				 aMemAdjust;
}ATOM_VRAM_INFO_V3;

#define	ATOM_VRAM_INFO_LAST	     ATOM_VRAM_INFO_V3

typedef struct _ATOM_VRAM_INFO_V4
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  USHORT                     usMemAdjustTblOffset;													 // offset of ATOM_INIT_REG_BLOCK structure for memory vendor specific MC adjust setting
  USHORT                     usMemClkPatchTblOffset;												 //	offset of ATOM_INIT_REG_BLOCK structure for memory clock specific MC setting
  USHORT										 usRerseved;
  UCHAR           	         ucMemDQ7_0ByteRemap;													   // DQ line byte remap, =0: Memory Data line BYTE0, =1: BYTE1, =2: BYTE2, =3: BYTE3
  ULONG                      ulMemDQ7_0BitRemap;                             // each DQ line ( 7~0) use 3bits, like: DQ0=Bit[2:0], DQ1:[5:3], ... DQ7:[23:21]
  UCHAR                      ucReservde[4]; 
  UCHAR                      ucNumOfVRAMModule;
  ATOM_VRAM_MODULE_V4		     aVramInfo[ATOM_MAX_NUMBER_OF_VRAM_MODULE];      // just for allocation, real number of blocks is in ucNumOfVRAMModule;
	ATOM_INIT_REG_BLOCK				 asMemPatch;																		 // for allocation
																																						 //	ATOM_INIT_REG_BLOCK				 aMemAdjust;
}ATOM_VRAM_INFO_V4;

typedef struct _ATOM_VRAM_INFO_HEADER_V2_1
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  USHORT                     usMemAdjustTblOffset;													 // offset of ATOM_INIT_REG_BLOCK structure for memory vendor specific MC adjust setting
  USHORT                     usMemClkPatchTblOffset;												 //	offset of ATOM_INIT_REG_BLOCK structure for memory clock specific MC setting
  USHORT                     usPerBytePresetOffset;                          // offset of ATOM_INIT_REG_BLOCK structure for Per Byte Offset Preset Settings
  USHORT                     usReserved[3];
  UCHAR                      ucNumOfVRAMModule;                              // indicate number of VRAM module
  UCHAR                      ucMemoryClkPatchTblVer;                         // version of memory AC timing register list
  UCHAR                      ucVramModuleVer;                                // indicate ATOM_VRAM_MODUE version
  UCHAR                      ucReserved; 
  ATOM_VRAM_MODULE_V7		     aVramInfo[ATOM_MAX_NUMBER_OF_VRAM_MODULE];      // just for allocation, real number of blocks is in ucNumOfVRAMModule;
}ATOM_VRAM_INFO_HEADER_V2_1;


typedef struct _ATOM_VRAM_GPIO_DETECTION_INFO
{
  ATOM_COMMON_TABLE_HEADER   sHeader;
  UCHAR           	         aVID_PinsShift[9];   //8 bit strap maximum+terminator
}ATOM_VRAM_GPIO_DETECTION_INFO;


typedef struct _ATOM_MEMORY_TRAINING_INFO
{
	ATOM_COMMON_TABLE_HEADER   sHeader;
	UCHAR											 ucTrainingLoop;
	UCHAR											 ucReserved[3];
	ATOM_INIT_REG_BLOCK				 asMemTrainingSetting;
}ATOM_MEMORY_TRAINING_INFO;


typedef struct SW_I2C_CNTL_DATA_PARAMETERS
{
  UCHAR    ucControl;
  UCHAR    ucData; 
  UCHAR    ucSatus; 
  UCHAR    ucTemp; 
} SW_I2C_CNTL_DATA_PARAMETERS;

#define SW_I2C_CNTL_DATA_PS_ALLOCATION  SW_I2C_CNTL_DATA_PARAMETERS

typedef struct _SW_I2C_IO_DATA_PARAMETERS
{                               
  USHORT   GPIO_Info;
  UCHAR    ucAct; 
  UCHAR    ucData; 
 } SW_I2C_IO_DATA_PARAMETERS;

#define SW_I2C_IO_DATA_PS_ALLOCATION  SW_I2C_IO_DATA_PARAMETERS

/****************************SW I2C CNTL DEFINITIONS**********************/
#define SW_I2C_IO_RESET       0
#define SW_I2C_IO_GET         1
#define SW_I2C_IO_DRIVE       2
#define SW_I2C_IO_SET         3
#define SW_I2C_IO_START       4

#define SW_I2C_IO_CLOCK       0
#define SW_I2C_IO_DATA        0x80

#define SW_I2C_IO_ZERO        0
#define SW_I2C_IO_ONE         0x100

#define SW_I2C_CNTL_READ      0
#define SW_I2C_CNTL_WRITE     1
#define SW_I2C_CNTL_START     2
#define SW_I2C_CNTL_STOP      3
#define SW_I2C_CNTL_OPEN      4
#define SW_I2C_CNTL_CLOSE     5
#define SW_I2C_CNTL_WRITE1BIT 6

//==============================VESA definition Portion===============================
#define VESA_OEM_PRODUCT_REV			            "01.00"
#define VESA_MODE_ATTRIBUTE_MODE_SUPPORT	     0xBB	//refer to VBE spec p.32, no TTY support
#define VESA_MODE_WIN_ATTRIBUTE						     7
#define VESA_WIN_SIZE											     64

typedef struct _PTR_32_BIT_STRUCTURE
{
	USHORT	Offset16;			
	USHORT	Segment16;				
} PTR_32_BIT_STRUCTURE;

typedef union _PTR_32_BIT_UNION
{
	PTR_32_BIT_STRUCTURE	SegmentOffset;
	ULONG					        Ptr32_Bit;
} PTR_32_BIT_UNION;

typedef struct _VBE_1_2_INFO_BLOCK_UPDATABLE
{
	UCHAR				      VbeSignature[4];
	USHORT				    VbeVersion;
	PTR_32_BIT_UNION	OemStringPtr;
	UCHAR				      Capabilities[4];
	PTR_32_BIT_UNION	VideoModePtr;
	USHORT				    TotalMemory;
} VBE_1_2_INFO_BLOCK_UPDATABLE;


typedef struct _VBE_2_0_INFO_BLOCK_UPDATABLE
{
	VBE_1_2_INFO_BLOCK_UPDATABLE	CommonBlock;
	USHORT							    OemSoftRev;
	PTR_32_BIT_UNION				OemVendorNamePtr;
	PTR_32_BIT_UNION				OemProductNamePtr;
	PTR_32_BIT_UNION				OemProductRevPtr;
} VBE_2_0_INFO_BLOCK_UPDATABLE;

typedef union _VBE_VERSION_UNION
{
	VBE_2_0_INFO_BLOCK_UPDATABLE	VBE_2_0_InfoBlock;
	VBE_1_2_INFO_BLOCK_UPDATABLE	VBE_1_2_InfoBlock;
} VBE_VERSION_UNION;

typedef struct _VBE_INFO_BLOCK
{
	VBE_VERSION_UNION			UpdatableVBE_Info;
	UCHAR						      Reserved[222];
	UCHAR						      OemData[256];
} VBE_INFO_BLOCK;

typedef struct _VBE_FP_INFO
{
  USHORT	HSize;
	USHORT	VSize;
	USHORT	FPType;
	UCHAR		RedBPP;
	UCHAR		GreenBPP;
	UCHAR		BlueBPP;
	UCHAR		ReservedBPP;
	ULONG		RsvdOffScrnMemSize;
	ULONG		RsvdOffScrnMEmPtr;
	UCHAR		Reserved[14];
} VBE_FP_INFO;

typedef struct _VESA_MODE_INFO_BLOCK
{
// Mandatory information for all VBE revisions
  USHORT    ModeAttributes;  //			dw	?	; mode attributes
	UCHAR     WinAAttributes;  //			db	?	; window A attributes
	UCHAR     WinBAttributes;  //			db	?	; window B attributes
	USHORT    WinGranularity;  //			dw	?	; window granularity
	USHORT    WinSize;         //			dw	?	; window size
	USHORT    WinASegment;     //			dw	?	; window A start segment
	USHORT    WinBSegment;     //			dw	?	; window B start segment
	ULONG     WinFuncPtr;      //			dd	?	; real mode pointer to window function
	USHORT    BytesPerScanLine;//			dw	?	; bytes per scan line

//; Mandatory information for VBE 1.2 and above
  USHORT    XResolution;      //			dw	?	; horizontal resolution in pixels or characters
	USHORT    YResolution;      //			dw	?	; vertical resolution in pixels or characters
	UCHAR     XCharSize;        //			db	?	; character cell width in pixels
	UCHAR     YCharSize;        //			db	?	; character cell height in pixels
	UCHAR     NumberOfPlanes;   //			db	?	; number of memory planes
	UCHAR     BitsPerPixel;     //			db	?	; bits per pixel
	UCHAR     NumberOfBanks;    //			db	?	; number of banks
	UCHAR     MemoryModel;      //			db	?	; memory model type
	UCHAR     BankSize;         //			db	?	; bank size in KB
	UCHAR     NumberOfImagePages;//		  db	?	; number of images
	UCHAR     ReservedForPageFunction;//db	1	; reserved for page function

//; Direct Color fields(required for direct/6 and YUV/7 memory models)
	UCHAR			RedMaskSize;        //		db	?	; size of direct color red mask in bits
	UCHAR			RedFieldPosition;   //		db	?	; bit position of lsb of red mask
	UCHAR			GreenMaskSize;      //		db	?	; size of direct color green mask in bits
	UCHAR			GreenFieldPosition; //		db	?	; bit position of lsb of green mask
	UCHAR			BlueMaskSize;       //		db	?	; size of direct color blue mask in bits
	UCHAR			BlueFieldPosition;  //		db	?	; bit position of lsb of blue mask
	UCHAR			RsvdMaskSize;       //		db	?	; size of direct color reserved mask in bits
	UCHAR			RsvdFieldPosition;  //		db	?	; bit position of lsb of reserved mask
	UCHAR			DirectColorModeInfo;//		db	?	; direct color mode attributes

//; Mandatory information for VBE 2.0 and above
	ULONG			PhysBasePtr;        //		dd	?	; physical address for flat memory frame buffer
	ULONG			Reserved_1;         //		dd	0	; reserved - always set to 0
	USHORT		Reserved_2;         //	  dw	0	; reserved - always set to 0

//; Mandatory information for VBE 3.0 and above
	USHORT		LinBytesPerScanLine;  //	dw	?	; bytes per scan line for linear modes
	UCHAR			BnkNumberOfImagePages;//	db	?	; number of images for banked modes
	UCHAR			LinNumberOfImagPages; //	db	?	; number of images for linear modes
	UCHAR			LinRedMaskSize;       //	db	?	; size of direct color red mask(linear modes)
	UCHAR			LinRedFieldPosition;  //	db	?	; bit position of lsb of red mask(linear modes)
	UCHAR			LinGreenMaskSize;     //	db	?	; size of direct color green mask(linear modes)
	UCHAR			LinGreenFieldPosition;//	db	?	; bit position of lsb of green mask(linear modes)
	UCHAR			LinBlueMaskSize;      //	db	?	; size of direct color blue mask(linear modes)
	UCHAR			LinBlueFieldPosition; //	db	?	; bit position of lsb of blue mask(linear modes)
	UCHAR			LinRsvdMaskSize;      //	db	?	; size of direct color reserved mask(linear modes)
	UCHAR			LinRsvdFieldPosition; //	db	?	; bit position of lsb of reserved mask(linear modes)
	ULONG			MaxPixelClock;        //	dd	?	; maximum pixel clock(in Hz) for graphics mode
	UCHAR			Reserved;             //	db	190 dup (0)
} VESA_MODE_INFO_BLOCK;

// BIOS function CALLS
#define ATOM_BIOS_EXTENDED_FUNCTION_CODE        0xA0	        // ATI Extended Function code
#define ATOM_BIOS_FUNCTION_COP_MODE             0x00
#define ATOM_BIOS_FUNCTION_SHORT_QUERY1         0x04
#define ATOM_BIOS_FUNCTION_SHORT_QUERY2         0x05
#define ATOM_BIOS_FUNCTION_SHORT_QUERY3         0x06
#define ATOM_BIOS_FUNCTION_GET_DDC              0x0B   
#define ATOM_BIOS_FUNCTION_ASIC_DSTATE          0x0E
#define ATOM_BIOS_FUNCTION_DEBUG_PLAY           0x0F
#define ATOM_BIOS_FUNCTION_STV_STD              0x16
#define ATOM_BIOS_FUNCTION_DEVICE_DET           0x17
#define ATOM_BIOS_FUNCTION_DEVICE_SWITCH        0x18

#define ATOM_BIOS_FUNCTION_PANEL_CONTROL        0x82
#define ATOM_BIOS_FUNCTION_OLD_DEVICE_DET       0x83
#define ATOM_BIOS_FUNCTION_OLD_DEVICE_SWITCH    0x84
#define ATOM_BIOS_FUNCTION_HW_ICON              0x8A 
#define ATOM_BIOS_FUNCTION_SET_CMOS             0x8B
#define SUB_FUNCTION_UPDATE_DISPLAY_INFO        0x8000          // Sub function 80
#define SUB_FUNCTION_UPDATE_EXPANSION_INFO      0x8100          // Sub function 80

#define ATOM_BIOS_FUNCTION_DISPLAY_INFO         0x8D
#define ATOM_BIOS_FUNCTION_DEVICE_ON_OFF        0x8E
#define ATOM_BIOS_FUNCTION_VIDEO_STATE          0x8F 
#define ATOM_SUB_FUNCTION_GET_CRITICAL_STATE    0x0300          // Sub function 03  
#define ATOM_SUB_FUNCTION_GET_LIDSTATE          0x0700          // Sub function 7
#define ATOM_SUB_FUNCTION_THERMAL_STATE_NOTICE  0x1400          // Notify caller the current thermal state
#define ATOM_SUB_FUNCTION_CRITICAL_STATE_NOTICE 0x8300          // Notify caller the current critical state
#define ATOM_SUB_FUNCTION_SET_LIDSTATE          0x8500          // Sub function 85
#define ATOM_SUB_FUNCTION_GET_REQ_DISPLAY_FROM_SBIOS_MODE 0x8900// Sub function 89
#define ATOM_SUB_FUNCTION_INFORM_ADC_SUPPORT    0x9400          // Notify caller that ADC is supported
     

#define ATOM_BIOS_FUNCTION_VESA_DPMS            0x4F10          // Set DPMS 
#define ATOM_SUB_FUNCTION_SET_DPMS              0x0001          // BL: Sub function 01 
#define ATOM_SUB_FUNCTION_GET_DPMS              0x0002          // BL: Sub function 02 
#define ATOM_PARAMETER_VESA_DPMS_ON             0x0000          // BH Parameter for DPMS ON.  
#define ATOM_PARAMETER_VESA_DPMS_STANDBY        0x0100          // BH Parameter for DPMS STANDBY  
#define ATOM_PARAMETER_VESA_DPMS_SUSPEND        0x0200          // BH Parameter for DPMS SUSPEND
#define ATOM_PARAMETER_VESA_DPMS_OFF            0x0400          // BH Parameter for DPMS OFF
#define ATOM_PARAMETER_VESA_DPMS_REDUCE_ON      0x0800          // BH Parameter for DPMS REDUCE ON (NOT SUPPORTED)

#define ATOM_BIOS_RETURN_CODE_MASK              0x0000FF00L
#define ATOM_BIOS_REG_HIGH_MASK                 0x0000FF00L
#define ATOM_BIOS_REG_LOW_MASK                  0x000000FFL

// structure used for VBIOS only

//DispOutInfoTable
typedef struct _ASIC_TRANSMITTER_INFO
{
	USHORT usTransmitterObjId;
	USHORT usSupportDevice;
  UCHAR  ucTransmitterCmdTblId;
	UCHAR  ucConfig;
	UCHAR  ucEncoderID;					 //available 1st encoder ( default )
	UCHAR  ucOptionEncoderID;    //available 2nd encoder ( optional )
	UCHAR  uc2ndEncoderID;
	UCHAR  ucReserved;
}ASIC_TRANSMITTER_INFO;

#define ASIC_TRANSMITTER_INFO_CONFIG__DVO_SDR_MODE          0x01
#define ASIC_TRANSMITTER_INFO_CONFIG__COHERENT_MODE         0x02
#define ASIC_TRANSMITTER_INFO_CONFIG__ENCODEROBJ_ID_MASK    0xc4
#define ASIC_TRANSMITTER_INFO_CONFIG__ENCODER_A             0x00
#define ASIC_TRANSMITTER_INFO_CONFIG__ENCODER_B             0x04
#define ASIC_TRANSMITTER_INFO_CONFIG__ENCODER_C             0x40
#define ASIC_TRANSMITTER_INFO_CONFIG__ENCODER_D             0x44
#define ASIC_TRANSMITTER_INFO_CONFIG__ENCODER_E             0x80
#define ASIC_TRANSMITTER_INFO_CONFIG__ENCODER_F             0x84

typedef struct _ASIC_ENCODER_INFO
{
	UCHAR ucEncoderID;
	UCHAR ucEncoderConfig;
  USHORT usEncoderCmdTblId;
}ASIC_ENCODER_INFO;

typedef struct _ATOM_DISP_OUT_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
	USHORT ptrTransmitterInfo;
	USHORT ptrEncoderInfo;
	ASIC_TRANSMITTER_INFO  asTransmitterInfo[1];
	ASIC_ENCODER_INFO      asEncoderInfo[1];
}ATOM_DISP_OUT_INFO;

typedef struct _ATOM_DISP_OUT_INFO_V2
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
	USHORT ptrTransmitterInfo;
	USHORT ptrEncoderInfo;
  USHORT ptrMainCallParserFar;                  // direct address of main parser call in VBIOS binary. 
	ASIC_TRANSMITTER_INFO  asTransmitterInfo[1];
	ASIC_ENCODER_INFO      asEncoderInfo[1];
}ATOM_DISP_OUT_INFO_V2;


typedef struct _ATOM_DISP_CLOCK_ID {
  UCHAR ucPpllId; 
  UCHAR ucPpllAttribute;
}ATOM_DISP_CLOCK_ID;

// ucPpllAttribute
#define CLOCK_SOURCE_SHAREABLE            0x01
#define CLOCK_SOURCE_DP_MODE              0x02
#define CLOCK_SOURCE_NONE_DP_MODE         0x04

//DispOutInfoTable
typedef struct _ASIC_TRANSMITTER_INFO_V2
{
	USHORT usTransmitterObjId;
	USHORT usDispClkIdOffset;    // point to clock source id list supported by Encoder Object
  UCHAR  ucTransmitterCmdTblId;
	UCHAR  ucConfig;
	UCHAR  ucEncoderID;					 // available 1st encoder ( default )
	UCHAR  ucOptionEncoderID;    // available 2nd encoder ( optional )
	UCHAR  uc2ndEncoderID;
	UCHAR  ucReserved;
}ASIC_TRANSMITTER_INFO_V2;

typedef struct _ATOM_DISP_OUT_INFO_V3
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
	USHORT ptrTransmitterInfo;
	USHORT ptrEncoderInfo;
  USHORT ptrMainCallParserFar;                  // direct address of main parser call in VBIOS binary. 
  USHORT usReserved;
  UCHAR  ucDCERevision;   
  UCHAR  ucMaxDispEngineNum;
  UCHAR  ucMaxActiveDispEngineNum;
  UCHAR  ucMaxPPLLNum;
  UCHAR  ucCoreRefClkSource;                    // value of CORE_REF_CLK_SOURCE
  UCHAR  ucDispCaps;
  UCHAR  ucReserved[2];
  ASIC_TRANSMITTER_INFO_V2  asTransmitterInfo[1];     // for alligment only
}ATOM_DISP_OUT_INFO_V3;

//ucDispCaps
#define DISPLAY_CAPS__DP_PCLK_FROM_PPLL        0x01
#define DISPLAY_CAPS__FORCE_DISPDEV_CONNECTED  0x02

typedef enum CORE_REF_CLK_SOURCE{
  CLOCK_SRC_XTALIN=0,
  CLOCK_SRC_XO_IN=1,
  CLOCK_SRC_XO_IN2=2,
}CORE_REF_CLK_SOURCE;

// DispDevicePriorityInfo
typedef struct _ATOM_DISPLAY_DEVICE_PRIORITY_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
	USHORT asDevicePriority[16];
}ATOM_DISPLAY_DEVICE_PRIORITY_INFO;

//ProcessAuxChannelTransactionTable
typedef struct _PROCESS_AUX_CHANNEL_TRANSACTION_PARAMETERS
{
	USHORT	lpAuxRequest;
	USHORT  lpDataOut;
	UCHAR		ucChannelID;
	union
	{
  UCHAR   ucReplyStatus;
	UCHAR   ucDelay;
	};
  UCHAR   ucDataOutLen;
	UCHAR   ucReserved;
}PROCESS_AUX_CHANNEL_TRANSACTION_PARAMETERS;

//ProcessAuxChannelTransactionTable
typedef struct _PROCESS_AUX_CHANNEL_TRANSACTION_PARAMETERS_V2
{
	USHORT	lpAuxRequest;
	USHORT  lpDataOut;
	UCHAR		ucChannelID;
	union
	{
  UCHAR   ucReplyStatus;
	UCHAR   ucDelay;
	};
  UCHAR   ucDataOutLen;
	UCHAR   ucHPD_ID;                                       //=0: HPD1, =1: HPD2, =2: HPD3, =3: HPD4, =4: HPD5, =5: HPD6
}PROCESS_AUX_CHANNEL_TRANSACTION_PARAMETERS_V2;

#define PROCESS_AUX_CHANNEL_TRANSACTION_PS_ALLOCATION			PROCESS_AUX_CHANNEL_TRANSACTION_PARAMETERS

//GetSinkType

typedef struct _DP_ENCODER_SERVICE_PARAMETERS
{
	USHORT ucLinkClock;
	union 
	{
	UCHAR ucConfig;				// for DP training command
	UCHAR ucI2cId;				// use for GET_SINK_TYPE command
	};
	UCHAR ucAction;
	UCHAR ucStatus;
	UCHAR ucLaneNum;
	UCHAR ucReserved[2];
}DP_ENCODER_SERVICE_PARAMETERS;

// ucAction
#define ATOM_DP_ACTION_GET_SINK_TYPE							0x01
/* obsolete */
#define ATOM_DP_ACTION_TRAINING_START							0x02
#define ATOM_DP_ACTION_TRAINING_COMPLETE					0x03
#define ATOM_DP_ACTION_TRAINING_PATTERN_SEL				0x04
#define ATOM_DP_ACTION_SET_VSWING_PREEMP					0x05
#define ATOM_DP_ACTION_GET_VSWING_PREEMP					0x06
#define ATOM_DP_ACTION_BLANKING                   0x07

// ucConfig
#define ATOM_DP_CONFIG_ENCODER_SEL_MASK						0x03
#define ATOM_DP_CONFIG_DIG1_ENCODER								0x00
#define ATOM_DP_CONFIG_DIG2_ENCODER								0x01
#define ATOM_DP_CONFIG_EXTERNAL_ENCODER						0x02
#define ATOM_DP_CONFIG_LINK_SEL_MASK							0x04
#define ATOM_DP_CONFIG_LINK_A											0x00
#define ATOM_DP_CONFIG_LINK_B											0x04
/* /obselete */
#define DP_ENCODER_SERVICE_PS_ALLOCATION				WRITE_ONE_BYTE_HW_I2C_DATA_PARAMETERS


typedef struct _DP_ENCODER_SERVICE_PARAMETERS_V2
{
	USHORT usExtEncoderObjId;   // External Encoder Object Id, output parameter only, use when ucAction = DP_SERVICE_V2_ACTION_DET_EXT_CONNECTION
  UCHAR  ucAuxId;
  UCHAR  ucAction;
  UCHAR  ucSinkType;          // Iput and Output parameters. 
  UCHAR  ucHPDId;             // Input parameter, used when ucAction = DP_SERVICE_V2_ACTION_DET_EXT_CONNECTION
	UCHAR  ucReserved[2];
}DP_ENCODER_SERVICE_PARAMETERS_V2;

typedef struct _DP_ENCODER_SERVICE_PS_ALLOCATION_V2
{
  DP_ENCODER_SERVICE_PARAMETERS_V2 asDPServiceParam;
  PROCESS_AUX_CHANNEL_TRANSACTION_PARAMETERS_V2 asAuxParam;
}DP_ENCODER_SERVICE_PS_ALLOCATION_V2;

// ucAction
#define DP_SERVICE_V2_ACTION_GET_SINK_TYPE							0x01
#define DP_SERVICE_V2_ACTION_DET_LCD_CONNECTION			    0x02


// DP_TRAINING_TABLE
#define DPCD_SET_LINKRATE_LANENUM_PATTERN1_TBL_ADDR				ATOM_DP_TRAINING_TBL_ADDR		
#define DPCD_SET_SS_CNTL_TBL_ADDR													(ATOM_DP_TRAINING_TBL_ADDR + 8 )
#define DPCD_SET_LANE_VSWING_PREEMP_TBL_ADDR							(ATOM_DP_TRAINING_TBL_ADDR + 16 )
#define DPCD_SET_TRAINING_PATTERN0_TBL_ADDR								(ATOM_DP_TRAINING_TBL_ADDR + 24 )
#define DPCD_SET_TRAINING_PATTERN2_TBL_ADDR								(ATOM_DP_TRAINING_TBL_ADDR + 32)
#define DPCD_GET_LINKRATE_LANENUM_SS_TBL_ADDR							(ATOM_DP_TRAINING_TBL_ADDR + 40)
#define	DPCD_GET_LANE_STATUS_ADJUST_TBL_ADDR							(ATOM_DP_TRAINING_TBL_ADDR + 48)
#define DP_I2C_AUX_DDC_WRITE_START_TBL_ADDR								(ATOM_DP_TRAINING_TBL_ADDR + 60)
#define DP_I2C_AUX_DDC_WRITE_TBL_ADDR											(ATOM_DP_TRAINING_TBL_ADDR + 64)
#define DP_I2C_AUX_DDC_READ_START_TBL_ADDR								(ATOM_DP_TRAINING_TBL_ADDR + 72)
#define DP_I2C_AUX_DDC_READ_TBL_ADDR											(ATOM_DP_TRAINING_TBL_ADDR + 76)
#define DP_I2C_AUX_DDC_WRITE_END_TBL_ADDR                 (ATOM_DP_TRAINING_TBL_ADDR + 80) 
#define DP_I2C_AUX_DDC_READ_END_TBL_ADDR									(ATOM_DP_TRAINING_TBL_ADDR + 84)

typedef struct _PROCESS_I2C_CHANNEL_TRANSACTION_PARAMETERS
{
	UCHAR   ucI2CSpeed;
 	union
	{
   UCHAR ucRegIndex;
   UCHAR ucStatus;
	};
	USHORT  lpI2CDataOut;
  UCHAR   ucFlag;               
  UCHAR   ucTransBytes;
  UCHAR   ucSlaveAddr;
  UCHAR   ucLineNumber;
}PROCESS_I2C_CHANNEL_TRANSACTION_PARAMETERS;

#define PROCESS_I2C_CHANNEL_TRANSACTION_PS_ALLOCATION       PROCESS_I2C_CHANNEL_TRANSACTION_PARAMETERS

//ucFlag
#define HW_I2C_WRITE        1
#define HW_I2C_READ         0
#define I2C_2BYTE_ADDR      0x02

/****************************************************************************/	
// Structures used by HW_Misc_OperationTable
/****************************************************************************/	
typedef struct  _ATOM_HW_MISC_OPERATION_INPUT_PARAMETER_V1_1 
{
  UCHAR  ucCmd;                //  Input: To tell which action to take
  UCHAR  ucReserved[3];
  ULONG  ulReserved;
}ATOM_HW_MISC_OPERATION_INPUT_PARAMETER_V1_1; 

typedef struct  _ATOM_HW_MISC_OPERATION_OUTPUT_PARAMETER_V1_1 
{
  UCHAR  ucReturnCode;        // Output: Return value base on action was taken
  UCHAR  ucReserved[3];
  ULONG  ulReserved;
}ATOM_HW_MISC_OPERATION_OUTPUT_PARAMETER_V1_1;

// Actions code
#define  ATOM_GET_SDI_SUPPORT              0xF0

// Return code 
#define  ATOM_UNKNOWN_CMD                   0
#define  ATOM_FEATURE_NOT_SUPPORTED         1
#define  ATOM_FEATURE_SUPPORTED             2

typedef struct _ATOM_HW_MISC_OPERATION_PS_ALLOCATION
{
	ATOM_HW_MISC_OPERATION_INPUT_PARAMETER_V1_1        sInput_Output;
	PROCESS_I2C_CHANNEL_TRANSACTION_PARAMETERS         sReserved; 
}ATOM_HW_MISC_OPERATION_PS_ALLOCATION;

/****************************************************************************/	

typedef struct _SET_HWBLOCK_INSTANCE_PARAMETER_V2
{
   UCHAR ucHWBlkInst;                // HW block instance, 0, 1, 2, ...
   UCHAR ucReserved[3]; 
}SET_HWBLOCK_INSTANCE_PARAMETER_V2;

#define HWBLKINST_INSTANCE_MASK       0x07
#define HWBLKINST_HWBLK_MASK          0xF0
#define HWBLKINST_HWBLK_SHIFT         0x04

//ucHWBlock
#define SELECT_DISP_ENGINE            0
#define SELECT_DISP_PLL               1
#define SELECT_DCIO_UNIPHY_LINK0      2
#define SELECT_DCIO_UNIPHY_LINK1      3
#define SELECT_DCIO_IMPCAL            4
#define SELECT_DCIO_DIG               6
#define SELECT_CRTC_PIXEL_RATE        7
#define SELECT_VGA_BLK                8

// DIGTransmitterInfoTable structure used to program UNIPHY settings 
typedef struct _DIG_TRANSMITTER_INFO_HEADER_V3_1{  
  ATOM_COMMON_TABLE_HEADER sHeader;  
  USHORT usDPVsPreEmphSettingOffset;     // offset of PHY_ANALOG_SETTING_INFO * with DP Voltage Swing and Pre-Emphasis for each Link clock 
  USHORT usPhyAnalogRegListOffset;       // offset of CLOCK_CONDITION_REGESTER_INFO* with None-DP mode Analog Setting's register Info 
  USHORT usPhyAnalogSettingOffset;       // offset of CLOCK_CONDITION_SETTING_ENTRY* with None-DP mode Analog Setting for each link clock range
  USHORT usPhyPllRegListOffset;          // offset of CLOCK_CONDITION_REGESTER_INFO* with Phy Pll register Info 
  USHORT usPhyPllSettingOffset;          // offset of CLOCK_CONDITION_SETTING_ENTRY* with Phy Pll Settings
}DIG_TRANSMITTER_INFO_HEADER_V3_1;

typedef struct _DIG_TRANSMITTER_INFO_HEADER_V3_2{  
  ATOM_COMMON_TABLE_HEADER sHeader;  
  USHORT usDPVsPreEmphSettingOffset;     // offset of PHY_ANALOG_SETTING_INFO * with DP Voltage Swing and Pre-Emphasis for each Link clock 
  USHORT usPhyAnalogRegListOffset;       // offset of CLOCK_CONDITION_REGESTER_INFO* with None-DP mode Analog Setting's register Info 
  USHORT usPhyAnalogSettingOffset;       // offset of CLOCK_CONDITION_SETTING_ENTRY* with None-DP mode Analog Setting for each link clock range
  USHORT usPhyPllRegListOffset;          // offset of CLOCK_CONDITION_REGESTER_INFO* with Phy Pll register Info 
  USHORT usPhyPllSettingOffset;          // offset of CLOCK_CONDITION_SETTING_ENTRY* with Phy Pll Settings
  USHORT usDPSSRegListOffset;            // offset of CLOCK_CONDITION_REGESTER_INFO* with Phy SS Pll register Info 
  USHORT usDPSSSettingOffset;            // offset of CLOCK_CONDITION_SETTING_ENTRY* with Phy SS Pll Settings
}DIG_TRANSMITTER_INFO_HEADER_V3_2;

typedef struct _CLOCK_CONDITION_REGESTER_INFO{
  USHORT usRegisterIndex;
  UCHAR  ucStartBit;
  UCHAR  ucEndBit;
}CLOCK_CONDITION_REGESTER_INFO;

typedef struct _CLOCK_CONDITION_SETTING_ENTRY{
  USHORT usMaxClockFreq;
  UCHAR  ucEncodeMode;
  UCHAR  ucPhySel;
  ULONG  ulAnalogSetting[1];
}CLOCK_CONDITION_SETTING_ENTRY;

typedef struct _CLOCK_CONDITION_SETTING_INFO{
  USHORT usEntrySize;
  CLOCK_CONDITION_SETTING_ENTRY asClkCondSettingEntry[1];
}CLOCK_CONDITION_SETTING_INFO;

typedef struct _PHY_CONDITION_REG_VAL{
  ULONG  ulCondition;
  ULONG  ulRegVal;
}PHY_CONDITION_REG_VAL;

typedef struct _PHY_CONDITION_REG_VAL_V2{
  ULONG  ulCondition;
  UCHAR  ucCondition2;
  ULONG  ulRegVal;
}PHY_CONDITION_REG_VAL_V2;

typedef struct _PHY_CONDITION_REG_INFO{
  USHORT usRegIndex;
  USHORT usSize;
  PHY_CONDITION_REG_VAL asRegVal[1];
}PHY_CONDITION_REG_INFO;

typedef struct _PHY_CONDITION_REG_INFO_V2{
  USHORT usRegIndex;
  USHORT usSize;
  PHY_CONDITION_REG_VAL_V2 asRegVal[1];
}PHY_CONDITION_REG_INFO_V2;

typedef struct _PHY_ANALOG_SETTING_INFO{
  UCHAR  ucEncodeMode;
  UCHAR  ucPhySel;
  USHORT usSize;
  PHY_CONDITION_REG_INFO  asAnalogSetting[1];
}PHY_ANALOG_SETTING_INFO;

typedef struct _PHY_ANALOG_SETTING_INFO_V2{
  UCHAR  ucEncodeMode;
  UCHAR  ucPhySel;
  USHORT usSize;
  PHY_CONDITION_REG_INFO_V2  asAnalogSetting[1];
}PHY_ANALOG_SETTING_INFO_V2;

typedef struct _GFX_HAVESTING_PARAMETERS {
  UCHAR ucGfxBlkId;                        //GFX blk id to be harvested, like CU, RB or PRIM
  UCHAR ucReserved;                        //reserved 
  UCHAR ucActiveUnitNumPerSH;              //requested active CU/RB/PRIM number per shader array
  UCHAR ucMaxUnitNumPerSH;                 //max CU/RB/PRIM number per shader array   
} GFX_HAVESTING_PARAMETERS;

//ucGfxBlkId
#define GFX_HARVESTING_CU_ID               0
#define GFX_HARVESTING_RB_ID               1
#define GFX_HARVESTING_PRIM_ID             2

/****************************************************************************/	
//Portion VI: Definitinos for vbios MC scratch registers that driver used
/****************************************************************************/

#define MC_MISC0__MEMORY_TYPE_MASK    0xF0000000
#define MC_MISC0__MEMORY_TYPE__GDDR1  0x10000000
#define MC_MISC0__MEMORY_TYPE__DDR2   0x20000000
#define MC_MISC0__MEMORY_TYPE__GDDR3  0x30000000
#define MC_MISC0__MEMORY_TYPE__GDDR4  0x40000000
#define MC_MISC0__MEMORY_TYPE__GDDR5  0x50000000
#define MC_MISC0__MEMORY_TYPE__HBM    0x60000000
#define MC_MISC0__MEMORY_TYPE__DDR3   0xB0000000

#define ATOM_MEM_TYPE_DDR_STRING      "DDR"
#define ATOM_MEM_TYPE_DDR2_STRING     "DDR2"
#define ATOM_MEM_TYPE_GDDR3_STRING    "GDDR3"
#define ATOM_MEM_TYPE_GDDR4_STRING    "GDDR4"
#define ATOM_MEM_TYPE_GDDR5_STRING    "GDDR5"
#define ATOM_MEM_TYPE_HBM_STRING      "HBM"
#define ATOM_MEM_TYPE_DDR3_STRING     "DDR3"

/****************************************************************************/	
//Portion VI: Definitinos being oboselete
/****************************************************************************/

//==========================================================================================
//Remove the definitions below when driver is ready!
typedef struct _ATOM_DAC_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  USHORT                   usMaxFrequency;      // in 10kHz unit
  USHORT                   usReserved;
}ATOM_DAC_INFO;


typedef struct  _COMPASSIONATE_DATA           
{
  ATOM_COMMON_TABLE_HEADER sHeader; 

  //==============================  DAC1 portion
  UCHAR   ucDAC1_BG_Adjustment;
  UCHAR   ucDAC1_DAC_Adjustment;
  USHORT  usDAC1_FORCE_Data;
  //==============================  DAC2 portion
  UCHAR   ucDAC2_CRT2_BG_Adjustment;
  UCHAR   ucDAC2_CRT2_DAC_Adjustment;
  USHORT  usDAC2_CRT2_FORCE_Data;
  USHORT  usDAC2_CRT2_MUX_RegisterIndex;
  UCHAR   ucDAC2_CRT2_MUX_RegisterInfo;     //Bit[4:0]=Bit position,Bit[7]=1:Active High;=0 Active Low
  UCHAR   ucDAC2_NTSC_BG_Adjustment;
  UCHAR   ucDAC2_NTSC_DAC_Adjustment;
  USHORT  usDAC2_TV1_FORCE_Data;
  USHORT  usDAC2_TV1_MUX_RegisterIndex;
  UCHAR   ucDAC2_TV1_MUX_RegisterInfo;      //Bit[4:0]=Bit position,Bit[7]=1:Active High;=0 Active Low
  UCHAR   ucDAC2_CV_BG_Adjustment;
  UCHAR   ucDAC2_CV_DAC_Adjustment;
  USHORT  usDAC2_CV_FORCE_Data;
  USHORT  usDAC2_CV_MUX_RegisterIndex;
  UCHAR   ucDAC2_CV_MUX_RegisterInfo;       //Bit[4:0]=Bit position,Bit[7]=1:Active High;=0 Active Low
  UCHAR   ucDAC2_PAL_BG_Adjustment;
  UCHAR   ucDAC2_PAL_DAC_Adjustment;
  USHORT  usDAC2_TV2_FORCE_Data;
}COMPASSIONATE_DATA;

/****************************Supported Device Info Table Definitions**********************/
//  ucConnectInfo:
//    [7:4] - connector type
//      = 1   - VGA connector   
//      = 2   - DVI-I
//      = 3   - DVI-D
//      = 4   - DVI-A
//      = 5   - SVIDEO
//      = 6   - COMPOSITE
//      = 7   - LVDS
//      = 8   - DIGITAL LINK
//      = 9   - SCART
//      = 0xA - HDMI_type A
//      = 0xB - HDMI_type B
//      = 0xE - Special case1 (DVI+DIN)
//      Others=TBD
//    [3:0] - DAC Associated
//      = 0   - no DAC
//      = 1   - DACA
//      = 2   - DACB
//      = 3   - External DAC
//      Others=TBD
//    

typedef struct _ATOM_CONNECTOR_INFO
{
#if ATOM_BIG_ENDIAN
  UCHAR   bfConnectorType:4;
  UCHAR   bfAssociatedDAC:4;
#else
  UCHAR   bfAssociatedDAC:4;
  UCHAR   bfConnectorType:4;
#endif
}ATOM_CONNECTOR_INFO;

typedef union _ATOM_CONNECTOR_INFO_ACCESS
{
  ATOM_CONNECTOR_INFO sbfAccess;
  UCHAR               ucAccess;
}ATOM_CONNECTOR_INFO_ACCESS;

typedef struct _ATOM_CONNECTOR_INFO_I2C
{
  ATOM_CONNECTOR_INFO_ACCESS sucConnectorInfo;
  ATOM_I2C_ID_CONFIG_ACCESS  sucI2cId;
}ATOM_CONNECTOR_INFO_I2C;


typedef struct _ATOM_SUPPORTED_DEVICES_INFO
{ 
  ATOM_COMMON_TABLE_HEADER	sHeader;
  USHORT                    usDeviceSupport;
  ATOM_CONNECTOR_INFO_I2C   asConnInfo[ATOM_MAX_SUPPORTED_DEVICE_INFO];
}ATOM_SUPPORTED_DEVICES_INFO;

#define NO_INT_SRC_MAPPED       0xFF

typedef struct _ATOM_CONNECTOR_INC_SRC_BITMAP
{
  UCHAR   ucIntSrcBitmap;
}ATOM_CONNECTOR_INC_SRC_BITMAP;

typedef struct _ATOM_SUPPORTED_DEVICES_INFO_2
{ 
  ATOM_COMMON_TABLE_HEADER      sHeader;
  USHORT                        usDeviceSupport;
  ATOM_CONNECTOR_INFO_I2C       asConnInfo[ATOM_MAX_SUPPORTED_DEVICE_INFO_2];
  ATOM_CONNECTOR_INC_SRC_BITMAP asIntSrcInfo[ATOM_MAX_SUPPORTED_DEVICE_INFO_2];
}ATOM_SUPPORTED_DEVICES_INFO_2;

typedef struct _ATOM_SUPPORTED_DEVICES_INFO_2d1
{ 
  ATOM_COMMON_TABLE_HEADER      sHeader;
  USHORT                        usDeviceSupport;
  ATOM_CONNECTOR_INFO_I2C       asConnInfo[ATOM_MAX_SUPPORTED_DEVICE];
  ATOM_CONNECTOR_INC_SRC_BITMAP asIntSrcInfo[ATOM_MAX_SUPPORTED_DEVICE];
}ATOM_SUPPORTED_DEVICES_INFO_2d1;

#define ATOM_SUPPORTED_DEVICES_INFO_LAST ATOM_SUPPORTED_DEVICES_INFO_2d1



typedef struct _ATOM_MISC_CONTROL_INFO
{
   USHORT usFrequency;
   UCHAR  ucPLL_ChargePump;				                // PLL charge-pump gain control
   UCHAR  ucPLL_DutyCycle;				                // PLL duty cycle control
   UCHAR  ucPLL_VCO_Gain;				                  // PLL VCO gain control
   UCHAR  ucPLL_VoltageSwing;			                // PLL driver voltage swing control
}ATOM_MISC_CONTROL_INFO;  


#define ATOM_MAX_MISC_INFO       4

typedef struct _ATOM_TMDS_INFO
{
  ATOM_COMMON_TABLE_HEADER sHeader;  
  USHORT							usMaxFrequency;             // in 10Khz
  ATOM_MISC_CONTROL_INFO				asMiscInfo[ATOM_MAX_MISC_INFO];
}ATOM_TMDS_INFO;


typedef struct _ATOM_ENCODER_ANALOG_ATTRIBUTE
{
  UCHAR ucTVStandard;     //Same as TV standards defined above, 
  UCHAR ucPadding[1];
}ATOM_ENCODER_ANALOG_ATTRIBUTE;

typedef struct _ATOM_ENCODER_DIGITAL_ATTRIBUTE
{
  UCHAR ucAttribute;      //Same as other digital encoder attributes defined above
  UCHAR ucPadding[1];		
}ATOM_ENCODER_DIGITAL_ATTRIBUTE;

typedef union _ATOM_ENCODER_ATTRIBUTE
{
  ATOM_ENCODER_ANALOG_ATTRIBUTE sAlgAttrib;
  ATOM_ENCODER_DIGITAL_ATTRIBUTE sDigAttrib;
}ATOM_ENCODER_ATTRIBUTE;


typedef struct _DVO_ENCODER_CONTROL_PARAMETERS
{
  USHORT usPixelClock; 
  USHORT usEncoderID; 
  UCHAR  ucDeviceType;												//Use ATOM_DEVICE_xxx1_Index to indicate device type only.	
  UCHAR  ucAction;														//ATOM_ENABLE/ATOM_DISABLE/ATOM_HPD_INIT
  ATOM_ENCODER_ATTRIBUTE usDevAttr;     		
}DVO_ENCODER_CONTROL_PARAMETERS;

typedef struct _DVO_ENCODER_CONTROL_PS_ALLOCATION
{                               
  DVO_ENCODER_CONTROL_PARAMETERS    sDVOEncoder;
  WRITE_ONE_BYTE_HW_I2C_DATA_PS_ALLOCATION      sReserved;     //Caller doesn't need to init this portion
}DVO_ENCODER_CONTROL_PS_ALLOCATION;


#define ATOM_XTMDS_ASIC_SI164_ID        1
#define ATOM_XTMDS_ASIC_SI178_ID        2
#define ATOM_XTMDS_ASIC_TFP513_ID       3
#define ATOM_XTMDS_SUPPORTED_SINGLELINK 0x00000001
#define ATOM_XTMDS_SUPPORTED_DUALLINK   0x00000002
#define ATOM_XTMDS_MVPU_FPGA            0x00000004

                           
typedef struct _ATOM_XTMDS_INFO
{
  ATOM_COMMON_TABLE_HEADER   sHeader;  
  USHORT                     usSingleLinkMaxFrequency; 
  ATOM_I2C_ID_CONFIG_ACCESS  sucI2cId;           //Point the ID on which I2C is used to control external chip
  UCHAR                      ucXtransimitterID;          
  UCHAR                      ucSupportedLink;    // Bit field, bit0=1, single link supported;bit1=1,dual link supported
  UCHAR                      ucSequnceAlterID;   // Even with the same external TMDS asic, it's possible that the program seqence alters 
                                                 // due to design. This ID is used to alert driver that the sequence is not "standard"!              
  UCHAR                      ucMasterAddress;    // Address to control Master xTMDS Chip
  UCHAR                      ucSlaveAddress;     // Address to control Slave xTMDS Chip
}ATOM_XTMDS_INFO;

typedef struct _DFP_DPMS_STATUS_CHANGE_PARAMETERS
{  
  UCHAR ucEnable;                     // ATOM_ENABLE=On or ATOM_DISABLE=Off
  UCHAR ucDevice;                     // ATOM_DEVICE_DFP1_INDEX....
  UCHAR ucPadding[2];             
}DFP_DPMS_STATUS_CHANGE_PARAMETERS;

/****************************Legacy Power Play Table Definitions **********************/

//Definitions for ulPowerPlayMiscInfo
#define ATOM_PM_MISCINFO_SPLIT_CLOCK                     0x00000000L
#define ATOM_PM_MISCINFO_USING_MCLK_SRC                  0x00000001L
#define ATOM_PM_MISCINFO_USING_SCLK_SRC                  0x00000002L

#define ATOM_PM_MISCINFO_VOLTAGE_DROP_SUPPORT            0x00000004L
#define ATOM_PM_MISCINFO_VOLTAGE_DROP_ACTIVE_HIGH        0x00000008L

#define ATOM_PM_MISCINFO_LOAD_PERFORMANCE_EN             0x00000010L

#define ATOM_PM_MISCINFO_ENGINE_CLOCK_CONTRL_EN          0x00000020L
#define ATOM_PM_MISCINFO_MEMORY_CLOCK_CONTRL_EN          0x00000040L
#define ATOM_PM_MISCINFO_PROGRAM_VOLTAGE                 0x00000080L  //When this bit set, ucVoltageDropIndex is not an index for GPIO pin, but a voltage ID that SW needs program  
 
#define ATOM_PM_MISCINFO_ASIC_REDUCED_SPEED_SCLK_EN      0x00000100L
#define ATOM_PM_MISCINFO_ASIC_DYNAMIC_VOLTAGE_EN         0x00000200L
#define ATOM_PM_MISCINFO_ASIC_SLEEP_MODE_EN              0x00000400L
#define ATOM_PM_MISCINFO_LOAD_BALANCE_EN                 0x00000800L
#define ATOM_PM_MISCINFO_DEFAULT_DC_STATE_ENTRY_TRUE     0x00001000L
#define ATOM_PM_MISCINFO_DEFAULT_LOW_DC_STATE_ENTRY_TRUE 0x00002000L
#define ATOM_PM_MISCINFO_LOW_LCD_REFRESH_RATE            0x00004000L

#define ATOM_PM_MISCINFO_DRIVER_DEFAULT_MODE             0x00008000L
#define ATOM_PM_MISCINFO_OVER_CLOCK_MODE                 0x00010000L 
#define ATOM_PM_MISCINFO_OVER_DRIVE_MODE                 0x00020000L
#define ATOM_PM_MISCINFO_POWER_SAVING_MODE               0x00040000L
#define ATOM_PM_MISCINFO_THERMAL_DIODE_MODE              0x00080000L

#define ATOM_PM_MISCINFO_FRAME_MODULATION_MASK           0x00300000L  //0-FM Disable, 1-2 level FM, 2-4 level FM, 3-Reserved
#define ATOM_PM_MISCINFO_FRAME_MODULATION_SHIFT          20 

#define ATOM_PM_MISCINFO_DYN_CLK_3D_IDLE                 0x00400000L
#define ATOM_PM_MISCINFO_DYNAMIC_CLOCK_DIVIDER_BY_2      0x00800000L
#define ATOM_PM_MISCINFO_DYNAMIC_CLOCK_DIVIDER_BY_4      0x01000000L
#define ATOM_PM_MISCINFO_DYNAMIC_HDP_BLOCK_EN            0x02000000L  //When set, Dynamic 
#define ATOM_PM_MISCINFO_DYNAMIC_MC_HOST_BLOCK_EN        0x04000000L  //When set, Dynamic
#define ATOM_PM_MISCINFO_3D_ACCELERATION_EN              0x08000000L  //When set, This mode is for acceleated 3D mode

#define ATOM_PM_MISCINFO_POWERPLAY_SETTINGS_GROUP_MASK   0x70000000L  //1-Optimal Battery Life Group, 2-High Battery, 3-Balanced, 4-High Performance, 5- Optimal Performance (Default state with Default clocks) 
#define ATOM_PM_MISCINFO_POWERPLAY_SETTINGS_GROUP_SHIFT  28
#define ATOM_PM_MISCINFO_ENABLE_BACK_BIAS                0x80000000L

#define ATOM_PM_MISCINFO2_SYSTEM_AC_LITE_MODE            0x00000001L
#define ATOM_PM_MISCINFO2_MULTI_DISPLAY_SUPPORT          0x00000002L
#define ATOM_PM_MISCINFO2_DYNAMIC_BACK_BIAS_EN           0x00000004L
#define ATOM_PM_MISCINFO2_FS3D_OVERDRIVE_INFO            0x00000008L
#define ATOM_PM_MISCINFO2_FORCEDLOWPWR_MODE              0x00000010L
#define ATOM_PM_MISCINFO2_VDDCI_DYNAMIC_VOLTAGE_EN       0x00000020L
#define ATOM_PM_MISCINFO2_VIDEO_PLAYBACK_CAPABLE         0x00000040L  //If this bit is set in multi-pp mode, then driver will pack up one with the minior power consumption. 
                                                                      //If it's not set in any pp mode, driver will use its default logic to pick a pp mode in video playback
#define ATOM_PM_MISCINFO2_NOT_VALID_ON_DC                0x00000080L
#define ATOM_PM_MISCINFO2_STUTTER_MODE_EN                0x00000100L
#define ATOM_PM_MISCINFO2_UVD_SUPPORT_MODE               0x00000200L 

//ucTableFormatRevision=1
//ucTableContentRevision=1
typedef struct  _ATOM_POWERMODE_INFO
{
  ULONG     ulMiscInfo;                 //The power level should be arranged in ascending order
  ULONG     ulReserved1;                // must set to 0
  ULONG     ulReserved2;                // must set to 0
  USHORT    usEngineClock;
  USHORT    usMemoryClock;
  UCHAR     ucVoltageDropIndex;         // index to GPIO table
  UCHAR     ucSelectedPanel_RefreshRate;// panel refresh rate
  UCHAR     ucMinTemperature;
  UCHAR     ucMaxTemperature;
  UCHAR     ucNumPciELanes;             // number of PCIE lanes
}ATOM_POWERMODE_INFO;

//ucTableFormatRevision=2
//ucTableContentRevision=1
typedef struct  _ATOM_POWERMODE_INFO_V2
{
  ULONG     ulMiscInfo;                 //The power level should be arranged in ascending order
  ULONG     ulMiscInfo2;                
  ULONG     ulEngineClock;                
  ULONG     ulMemoryClock;
  UCHAR     ucVoltageDropIndex;         // index to GPIO table
  UCHAR     ucSelectedPanel_RefreshRate;// panel refresh rate
  UCHAR     ucMinTemperature;
  UCHAR     ucMaxTemperature;
  UCHAR     ucNumPciELanes;             // number of PCIE lanes
}ATOM_POWERMODE_INFO_V2;

//ucTableFormatRevision=2
//ucTableContentRevision=2
typedef struct  _ATOM_POWERMODE_INFO_V3
{
  ULONG     ulMiscInfo;                 //The power level should be arranged in ascending order
  ULONG     ulMiscInfo2;                
  ULONG     ulEngineClock;                
  ULONG     ulMemoryClock;
  UCHAR     ucVoltageDropIndex;         // index to Core (VDDC) votage table
  UCHAR     ucSelectedPanel_RefreshRate;// panel refresh rate
  UCHAR     ucMinTemperature;
  UCHAR     ucMaxTemperature;
  UCHAR     ucNumPciELanes;             // number of PCIE lanes
  UCHAR     ucVDDCI_VoltageDropIndex;   // index to VDDCI votage table
}ATOM_POWERMODE_INFO_V3;


#define ATOM_MAX_NUMBEROF_POWER_BLOCK  8

#define ATOM_PP_OVERDRIVE_INTBITMAP_AUXWIN            0x01
#define ATOM_PP_OVERDRIVE_INTBITMAP_OVERDRIVE         0x02

#define ATOM_PP_OVERDRIVE_THERMALCONTROLLER_LM63      0x01
#define ATOM_PP_OVERDRIVE_THERMALCONTROLLER_ADM1032   0x02
#define ATOM_PP_OVERDRIVE_THERMALCONTROLLER_ADM1030   0x03
#define ATOM_PP_OVERDRIVE_THERMALCONTROLLER_MUA6649   0x04
#define ATOM_PP_OVERDRIVE_THERMALCONTROLLER_LM64      0x05
#define ATOM_PP_OVERDRIVE_THERMALCONTROLLER_F75375    0x06
#define ATOM_PP_OVERDRIVE_THERMALCONTROLLER_ASC7512   0x07	// Andigilog


typedef struct  _ATOM_POWERPLAY_INFO
{
  ATOM_COMMON_TABLE_HEADER	sHeader; 
  UCHAR    ucOverdriveThermalController;
  UCHAR    ucOverdriveI2cLine;
  UCHAR    ucOverdriveIntBitmap;
  UCHAR    ucOverdriveControllerAddress;
  UCHAR    ucSizeOfPowerModeEntry;
  UCHAR    ucNumOfPowerModeEntries;
  ATOM_POWERMODE_INFO asPowerPlayInfo[ATOM_MAX_NUMBEROF_POWER_BLOCK];
}ATOM_POWERPLAY_INFO;

typedef struct  _ATOM_POWERPLAY_INFO_V2
{
  ATOM_COMMON_TABLE_HEADER	sHeader; 
  UCHAR    ucOverdriveThermalController;
  UCHAR    ucOverdriveI2cLine;
  UCHAR    ucOverdriveIntBitmap;
  UCHAR    ucOverdriveControllerAddress;
  UCHAR    ucSizeOfPowerModeEntry;
  UCHAR    ucNumOfPowerModeEntries;
  ATOM_POWERMODE_INFO_V2 asPowerPlayInfo[ATOM_MAX_NUMBEROF_POWER_BLOCK];
}ATOM_POWERPLAY_INFO_V2;
  
typedef struct  _ATOM_POWERPLAY_INFO_V3
{
  ATOM_COMMON_TABLE_HEADER	sHeader; 
  UCHAR    ucOverdriveThermalController;
  UCHAR    ucOverdriveI2cLine;
  UCHAR    ucOverdriveIntBitmap;
  UCHAR    ucOverdriveControllerAddress;
  UCHAR    ucSizeOfPowerModeEntry;
  UCHAR    ucNumOfPowerModeEntries;
  ATOM_POWERMODE_INFO_V3 asPowerPlayInfo[ATOM_MAX_NUMBEROF_POWER_BLOCK];
}ATOM_POWERPLAY_INFO_V3;


// Following definitions are for compatibility issue in different SW components. 
#define ATOM_MASTER_DATA_TABLE_REVISION   0x01
#define Object_Info												Object_Header			
#define	AdjustARB_SEQ											MC_InitParameter
#define	VRAM_GPIO_DetectionInfo						VoltageObjectInfo
#define	ASIC_VDDCI_Info                   ASIC_ProfilingInfo														
#define ASIC_MVDDQ_Info										MemoryTrainingInfo
#define SS_Info                           PPLL_SS_Info                      
#define ASIC_MVDDC_Info                   ASIC_InternalSS_Info
#define DispDevicePriorityInfo						SaveRestoreInfo
#define DispOutInfo												TV_VideoMode


#define ATOM_ENCODER_OBJECT_TABLE         ATOM_OBJECT_TABLE
#define ATOM_CONNECTOR_OBJECT_TABLE       ATOM_OBJECT_TABLE

//New device naming, remove them when both DAL/VBIOS is ready
#define DFP2I_OUTPUT_CONTROL_PARAMETERS    CRT1_OUTPUT_CONTROL_PARAMETERS
#define DFP2I_OUTPUT_CONTROL_PS_ALLOCATION DFP2I_OUTPUT_CONTROL_PARAMETERS

#define DFP1X_OUTPUT_CONTROL_PARAMETERS    CRT1_OUTPUT_CONTROL_PARAMETERS
#define DFP1X_OUTPUT_CONTROL_PS_ALLOCATION DFP1X_OUTPUT_CONTROL_PARAMETERS

#define DFP1I_OUTPUT_CONTROL_PARAMETERS    DFP1_OUTPUT_CONTROL_PARAMETERS
#define DFP1I_OUTPUT_CONTROL_PS_ALLOCATION DFP1_OUTPUT_CONTROL_PS_ALLOCATION

#define ATOM_DEVICE_DFP1I_SUPPORT          ATOM_DEVICE_DFP1_SUPPORT
#define ATOM_DEVICE_DFP1X_SUPPORT          ATOM_DEVICE_DFP2_SUPPORT

#define ATOM_DEVICE_DFP1I_INDEX            ATOM_DEVICE_DFP1_INDEX
#define ATOM_DEVICE_DFP1X_INDEX            ATOM_DEVICE_DFP2_INDEX
 
#define ATOM_DEVICE_DFP2I_INDEX            0x00000009
#define ATOM_DEVICE_DFP2I_SUPPORT          (0x1L << ATOM_DEVICE_DFP2I_INDEX)

#define ATOM_S0_DFP1I                      ATOM_S0_DFP1
#define ATOM_S0_DFP1X                      ATOM_S0_DFP2

#define ATOM_S0_DFP2I                      0x00200000L
#define ATOM_S0_DFP2Ib2                    0x20

#define ATOM_S2_DFP1I_DPMS_STATE           ATOM_S2_DFP1_DPMS_STATE
#define ATOM_S2_DFP1X_DPMS_STATE           ATOM_S2_DFP2_DPMS_STATE

#define ATOM_S2_DFP2I_DPMS_STATE           0x02000000L
#define ATOM_S2_DFP2I_DPMS_STATEb3         0x02

#define ATOM_S3_DFP2I_ACTIVEb1             0x02

#define ATOM_S3_DFP1I_ACTIVE               ATOM_S3_DFP1_ACTIVE 
#define ATOM_S3_DFP1X_ACTIVE               ATOM_S3_DFP2_ACTIVE

#define ATOM_S3_DFP2I_ACTIVE               0x00000200L

#define ATOM_S3_DFP1I_CRTC_ACTIVE          ATOM_S3_DFP1_CRTC_ACTIVE
#define ATOM_S3_DFP1X_CRTC_ACTIVE          ATOM_S3_DFP2_CRTC_ACTIVE
#define ATOM_S3_DFP2I_CRTC_ACTIVE          0x02000000L

#define ATOM_S3_DFP2I_CRTC_ACTIVEb3        0x02
#define ATOM_S5_DOS_REQ_DFP2Ib1            0x02

#define ATOM_S5_DOS_REQ_DFP2I              0x0200
#define ATOM_S6_ACC_REQ_DFP1I              ATOM_S6_ACC_REQ_DFP1
#define ATOM_S6_ACC_REQ_DFP1X              ATOM_S6_ACC_REQ_DFP2

#define ATOM_S6_ACC_REQ_DFP2Ib3            0x02
#define ATOM_S6_ACC_REQ_DFP2I              0x02000000L

#define TMDS1XEncoderControl               DVOEncoderControl           
#define DFP1XOutputControl                 DVOOutputControl

#define ExternalDFPOutputControl           DFP1XOutputControl
#define EnableExternalTMDS_Encoder         TMDS1XEncoderControl

#define DFP1IOutputControl                 TMDSAOutputControl
#define DFP2IOutputControl                 LVTMAOutputControl      

#define DAC1_ENCODER_CONTROL_PARAMETERS    DAC_ENCODER_CONTROL_PARAMETERS
#define DAC1_ENCODER_CONTROL_PS_ALLOCATION DAC_ENCODER_CONTROL_PS_ALLOCATION

#define DAC2_ENCODER_CONTROL_PARAMETERS    DAC_ENCODER_CONTROL_PARAMETERS
#define DAC2_ENCODER_CONTROL_PS_ALLOCATION DAC_ENCODER_CONTROL_PS_ALLOCATION

#define ucDac1Standard  ucDacStandard
#define ucDac2Standard  ucDacStandard  

#define TMDS1EncoderControl TMDSAEncoderControl
#define TMDS2EncoderControl LVTMAEncoderControl

#define DFP1OutputControl   TMDSAOutputControl
#define DFP2OutputControl   LVTMAOutputControl
#define CRT1OutputControl   DAC1OutputControl
#define CRT2OutputControl   DAC2OutputControl

//These two lines will be removed for sure in a few days, will follow up with Michael V.
#define EnableLVDS_SS   EnableSpreadSpectrumOnPPLL
#define ENABLE_LVDS_SS_PARAMETERS_V3  ENABLE_SPREAD_SPECTRUM_ON_PPLL  

//#define ATOM_S2_CRT1_DPMS_STATE         0x00010000L
//#define ATOM_S2_LCD1_DPMS_STATE	        ATOM_S2_CRT1_DPMS_STATE
//#define ATOM_S2_TV1_DPMS_STATE          ATOM_S2_CRT1_DPMS_STATE
//#define ATOM_S2_DFP1_DPMS_STATE         ATOM_S2_CRT1_DPMS_STATE
//#define ATOM_S2_CRT2_DPMS_STATE         ATOM_S2_CRT1_DPMS_STATE

#define ATOM_S6_ACC_REQ_TV2             0x00400000L
#define ATOM_DEVICE_TV2_INDEX           0x00000006
#define ATOM_DEVICE_TV2_SUPPORT         (0x1L << ATOM_DEVICE_TV2_INDEX)
#define ATOM_S0_TV2                     0x00100000L
#define ATOM_S3_TV2_ACTIVE              ATOM_S3_DFP6_ACTIVE
#define ATOM_S3_TV2_CRTC_ACTIVE         ATOM_S3_DFP6_CRTC_ACTIVE

//
#define ATOM_S2_CRT1_DPMS_STATE         0x00010000L
#define ATOM_S2_LCD1_DPMS_STATE	        0x00020000L
#define ATOM_S2_TV1_DPMS_STATE          0x00040000L
#define ATOM_S2_DFP1_DPMS_STATE         0x00080000L
#define ATOM_S2_CRT2_DPMS_STATE         0x00100000L
#define ATOM_S2_LCD2_DPMS_STATE         0x00200000L
#define ATOM_S2_TV2_DPMS_STATE          0x00400000L
#define ATOM_S2_DFP2_DPMS_STATE         0x00800000L
#define ATOM_S2_CV_DPMS_STATE           0x01000000L
#define ATOM_S2_DFP3_DPMS_STATE					0x02000000L
#define ATOM_S2_DFP4_DPMS_STATE					0x04000000L
#define ATOM_S2_DFP5_DPMS_STATE					0x08000000L

#define ATOM_S2_CRT1_DPMS_STATEb2       0x01
#define ATOM_S2_LCD1_DPMS_STATEb2       0x02
#define ATOM_S2_TV1_DPMS_STATEb2        0x04
#define ATOM_S2_DFP1_DPMS_STATEb2       0x08
#define ATOM_S2_CRT2_DPMS_STATEb2       0x10
#define ATOM_S2_LCD2_DPMS_STATEb2       0x20
#define ATOM_S2_TV2_DPMS_STATEb2        0x40
#define ATOM_S2_DFP2_DPMS_STATEb2       0x80
#define ATOM_S2_CV_DPMS_STATEb3         0x01
#define ATOM_S2_DFP3_DPMS_STATEb3				0x02
#define ATOM_S2_DFP4_DPMS_STATEb3				0x04
#define ATOM_S2_DFP5_DPMS_STATEb3				0x08

#define ATOM_S3_ASIC_GUI_ENGINE_HUNGb3	0x20
#define ATOM_S3_ALLOW_FAST_PWR_SWITCHb3 0x40
#define ATOM_S3_RQST_GPU_USE_MIN_PWRb3  0x80

/*********************************************************************************/

#pragma pack() // BIOS data must use byte alignment

//
// AMD ACPI Table
//
#pragma pack(1)

typedef struct {
  ULONG Signature;
  ULONG TableLength;      //Length
  UCHAR Revision;
  UCHAR Checksum;
  UCHAR OemId[6];
  UCHAR OemTableId[8];    //UINT64  OemTableId;
  ULONG OemRevision;
  ULONG CreatorId;
  ULONG CreatorRevision;
} AMD_ACPI_DESCRIPTION_HEADER;
/*
//EFI_ACPI_DESCRIPTION_HEADER from AcpiCommon.h
typedef struct {
  UINT32  Signature;       //0x0
  UINT32  Length;          //0x4
  UINT8   Revision;        //0x8
  UINT8   Checksum;        //0x9
  UINT8   OemId[6];        //0xA
  UINT64  OemTableId;      //0x10
  UINT32  OemRevision;     //0x18
  UINT32  CreatorId;       //0x1C
  UINT32  CreatorRevision; //0x20
}EFI_ACPI_DESCRIPTION_HEADER;
*/
typedef struct {
  AMD_ACPI_DESCRIPTION_HEADER SHeader;
  UCHAR TableUUID[16];    //0x24
  ULONG VBIOSImageOffset; //0x34. Offset to the first GOP_VBIOS_CONTENT block from the beginning of the structure.
  ULONG Lib1ImageOffset;  //0x38. Offset to the first GOP_LIB1_CONTENT block from the beginning of the structure.
  ULONG Reserved[4];      //0x3C
}UEFI_ACPI_VFCT;

typedef struct {
  ULONG  PCIBus;          //0x4C
  ULONG  PCIDevice;       //0x50
  ULONG  PCIFunction;     //0x54
  USHORT VendorID;        //0x58
  USHORT DeviceID;        //0x5A
  USHORT SSVID;           //0x5C
  USHORT SSID;            //0x5E
  ULONG  Revision;        //0x60
  ULONG  ImageLength;     //0x64
}VFCT_IMAGE_HEADER;


typedef struct {
  VFCT_IMAGE_HEADER	VbiosHeader;
  UCHAR	VbiosContent[1];
}GOP_VBIOS_CONTENT;

typedef struct {
  VFCT_IMAGE_HEADER	Lib1Header;
  UCHAR	Lib1Content[1];
}GOP_LIB1_CONTENT;

#pragma pack()


#endif /* _ATOMBIOS_H */

#include "pptable.h"

