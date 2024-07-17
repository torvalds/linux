/****************************************************************************\
* 
*  File Name      atomfirmware.h
*  Project        This is an interface header file between atombios and OS GPU drivers for SoC15 products
*
*  Description    header file of general definitions for OS and pre-OS video drivers
*
*  Copyright 2014 Advanced Micro Devices, Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
* and associated documentation files (the "Software"), to deal in the Software without restriction,
* including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
* subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all copies or substantial
* portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
\****************************************************************************/

/*IMPORTANT NOTES
* If a change in VBIOS/Driver/Tool's interface is only needed for SoC15 and forward products, then the change is only needed in this atomfirmware.h header file.
* If a change in VBIOS/Driver/Tool's interface is only needed for pre-SoC15 products, then the change is only needed in atombios.h header file.
* If a change is needed for both pre and post SoC15 products, then the change has to be made separately and might be differently in both atomfirmware.h and atombios.h.
*/

#ifndef _ATOMFIRMWARE_H_
#define _ATOMFIRMWARE_H_

enum  atom_bios_header_version_def{
  ATOM_MAJOR_VERSION        =0x0003,
  ATOM_MINOR_VERSION        =0x0003,
};

#ifdef _H2INC
  #ifndef uint32_t
    typedef unsigned long uint32_t;
  #endif

  #ifndef uint16_t
    typedef unsigned short uint16_t;
  #endif

  #ifndef uint8_t 
    typedef unsigned char uint8_t;
  #endif
#endif

enum atom_crtc_def{
  ATOM_CRTC1      =0,
  ATOM_CRTC2      =1,
  ATOM_CRTC3      =2,
  ATOM_CRTC4      =3,
  ATOM_CRTC5      =4,
  ATOM_CRTC6      =5,
  ATOM_CRTC_INVALID  =0xff,
};

enum atom_ppll_def{
  ATOM_PPLL0          =2,
  ATOM_GCK_DFS        =8,
  ATOM_FCH_CLK        =9,
  ATOM_DP_DTO         =11,
  ATOM_COMBOPHY_PLL0  =20,
  ATOM_COMBOPHY_PLL1  =21,
  ATOM_COMBOPHY_PLL2  =22,
  ATOM_COMBOPHY_PLL3  =23,
  ATOM_COMBOPHY_PLL4  =24,
  ATOM_COMBOPHY_PLL5  =25,
  ATOM_PPLL_INVALID   =0xff,
};

// define ASIC internal encoder id ( bit vector ), used for CRTC_SourceSel
enum atom_dig_def{
  ASIC_INT_DIG1_ENCODER_ID  =0x03,
  ASIC_INT_DIG2_ENCODER_ID  =0x09,
  ASIC_INT_DIG3_ENCODER_ID  =0x0a,
  ASIC_INT_DIG4_ENCODER_ID  =0x0b,
  ASIC_INT_DIG5_ENCODER_ID  =0x0c,
  ASIC_INT_DIG6_ENCODER_ID  =0x0d,
  ASIC_INT_DIG7_ENCODER_ID  =0x0e,
};

//ucEncoderMode
enum atom_encode_mode_def
{
  ATOM_ENCODER_MODE_DP          =0,
  ATOM_ENCODER_MODE_DP_SST      =0,
  ATOM_ENCODER_MODE_LVDS        =1,
  ATOM_ENCODER_MODE_DVI         =2,
  ATOM_ENCODER_MODE_HDMI        =3,
  ATOM_ENCODER_MODE_DP_AUDIO    =5,
  ATOM_ENCODER_MODE_DP_MST      =5,
  ATOM_ENCODER_MODE_CRT         =15,
  ATOM_ENCODER_MODE_DVO         =16,
};

enum atom_encoder_refclk_src_def{
  ENCODER_REFCLK_SRC_P1PLL      =0,
  ENCODER_REFCLK_SRC_P2PLL      =1,
  ENCODER_REFCLK_SRC_P3PLL      =2,
  ENCODER_REFCLK_SRC_EXTCLK     =3,
  ENCODER_REFCLK_SRC_INVALID    =0xff,
};

enum atom_scaler_def{
  ATOM_SCALER_DISABLE          =0,  /*scaler bypass mode, auto-center & no replication*/
  ATOM_SCALER_CENTER           =1,  //For Fudo, it's bypass and auto-center & auto replication
  ATOM_SCALER_EXPANSION        =2,  /*scaler expansion by 2 tap alpha blending mode*/
};

enum atom_operation_def{
  ATOM_DISABLE             = 0,
  ATOM_ENABLE              = 1,
  ATOM_INIT                = 7,
  ATOM_GET_STATUS          = 8,
};

enum atom_embedded_display_op_def{
  ATOM_LCD_BL_OFF                = 2,
  ATOM_LCD_BL_OM                 = 3,
  ATOM_LCD_BL_BRIGHTNESS_CONTROL = 4,
  ATOM_LCD_SELFTEST_START        = 5,
  ATOM_LCD_SELFTEST_STOP         = 6,
};

enum atom_spread_spectrum_mode{
  ATOM_SS_CENTER_OR_DOWN_MODE_MASK  = 0x01,
  ATOM_SS_DOWN_SPREAD_MODE          = 0x00,
  ATOM_SS_CENTRE_SPREAD_MODE        = 0x01,
  ATOM_INT_OR_EXT_SS_MASK           = 0x02,
  ATOM_INTERNAL_SS_MASK             = 0x00,
  ATOM_EXTERNAL_SS_MASK             = 0x02,
};

/* define panel bit per color  */
enum atom_panel_bit_per_color{
  PANEL_BPC_UNDEFINE     =0x00,
  PANEL_6BIT_PER_COLOR   =0x01,
  PANEL_8BIT_PER_COLOR   =0x02,
  PANEL_10BIT_PER_COLOR  =0x03,
  PANEL_12BIT_PER_COLOR  =0x04,
  PANEL_16BIT_PER_COLOR  =0x05,
};

//ucVoltageType
enum atom_voltage_type
{
  VOLTAGE_TYPE_VDDC = 1,
  VOLTAGE_TYPE_MVDDC = 2,
  VOLTAGE_TYPE_MVDDQ = 3,
  VOLTAGE_TYPE_VDDCI = 4,
  VOLTAGE_TYPE_VDDGFX = 5,
  VOLTAGE_TYPE_PCC = 6,
  VOLTAGE_TYPE_MVPP = 7,
  VOLTAGE_TYPE_LEDDPM = 8,
  VOLTAGE_TYPE_PCC_MVDD = 9,
  VOLTAGE_TYPE_PCIE_VDDC = 10,
  VOLTAGE_TYPE_PCIE_VDDR = 11,
  VOLTAGE_TYPE_GENERIC_I2C_1 = 0x11,
  VOLTAGE_TYPE_GENERIC_I2C_2 = 0x12,
  VOLTAGE_TYPE_GENERIC_I2C_3 = 0x13,
  VOLTAGE_TYPE_GENERIC_I2C_4 = 0x14,
  VOLTAGE_TYPE_GENERIC_I2C_5 = 0x15,
  VOLTAGE_TYPE_GENERIC_I2C_6 = 0x16,
  VOLTAGE_TYPE_GENERIC_I2C_7 = 0x17,
  VOLTAGE_TYPE_GENERIC_I2C_8 = 0x18,
  VOLTAGE_TYPE_GENERIC_I2C_9 = 0x19,
  VOLTAGE_TYPE_GENERIC_I2C_10 = 0x1A,
};

enum atom_dgpu_vram_type {
  ATOM_DGPU_VRAM_TYPE_GDDR5 = 0x50,
  ATOM_DGPU_VRAM_TYPE_HBM2  = 0x60,
  ATOM_DGPU_VRAM_TYPE_HBM2E = 0x61,
  ATOM_DGPU_VRAM_TYPE_GDDR6 = 0x70,
  ATOM_DGPU_VRAM_TYPE_HBM3 = 0x80,
};

enum atom_dp_vs_preemph_def{
  DP_VS_LEVEL0_PREEMPH_LEVEL0 = 0x00,
  DP_VS_LEVEL1_PREEMPH_LEVEL0 = 0x01,
  DP_VS_LEVEL2_PREEMPH_LEVEL0 = 0x02,
  DP_VS_LEVEL3_PREEMPH_LEVEL0 = 0x03,
  DP_VS_LEVEL0_PREEMPH_LEVEL1 = 0x08,
  DP_VS_LEVEL1_PREEMPH_LEVEL1 = 0x09,
  DP_VS_LEVEL2_PREEMPH_LEVEL1 = 0x0a,
  DP_VS_LEVEL0_PREEMPH_LEVEL2 = 0x10,
  DP_VS_LEVEL1_PREEMPH_LEVEL2 = 0x11,
  DP_VS_LEVEL0_PREEMPH_LEVEL3 = 0x18,
};

#define BIOS_ATOM_PREFIX   "ATOMBIOS"
#define BIOS_VERSION_PREFIX  "ATOMBIOSBK-AMD"
#define BIOS_STRING_LENGTH 43

/*
enum atom_string_def{
asic_bus_type_pcie_string = "PCI_EXPRESS", 
atom_fire_gl_string       = "FGL",
atom_bios_string          = "ATOM"
};
*/

#pragma pack(1)                          /* BIOS data must use byte aligment*/

enum atombios_image_offset{
  OFFSET_TO_ATOM_ROM_HEADER_POINTER          = 0x00000048,
  OFFSET_TO_ATOM_ROM_IMAGE_SIZE              = 0x00000002,
  OFFSET_TO_ATOMBIOS_ASIC_BUS_MEM_TYPE       = 0x94,
  MAXSIZE_OF_ATOMBIOS_ASIC_BUS_MEM_TYPE      = 20,  /*including the terminator 0x0!*/
  OFFSET_TO_GET_ATOMBIOS_NUMBER_OF_STRINGS   = 0x2f,
  OFFSET_TO_GET_ATOMBIOS_STRING_START        = 0x6e,
  OFFSET_TO_VBIOS_PART_NUMBER                = 0x80,
  OFFSET_TO_VBIOS_DATE                       = 0x50,
};

/****************************************************************************   
* Common header for all tables (Data table, Command function).
* Every table pointed in _ATOM_MASTER_DATA_TABLE has this common header. 
* And the pointer actually points to this header.
****************************************************************************/   

struct atom_common_table_header
{
  uint16_t structuresize;
  uint8_t  format_revision;   //mainly used for a hw function, when the parser is not backward compatible 
  uint8_t  content_revision;  //change it when a data table has a structure change, or a hw function has a input/output parameter change                                
};

/****************************************************************************  
* Structure stores the ROM header.
****************************************************************************/   
struct atom_rom_header_v2_2
{
  struct atom_common_table_header table_header;
  uint8_t  atom_bios_string[4];        //enum atom_string_def atom_bios_string;     //Signature to distinguish between Atombios and non-atombios, 
  uint16_t bios_segment_address;
  uint16_t protectedmodeoffset;
  uint16_t configfilenameoffset;
  uint16_t crc_block_offset;
  uint16_t vbios_bootupmessageoffset;
  uint16_t int10_offset;
  uint16_t pcibusdevinitcode;
  uint16_t iobaseaddress;
  uint16_t subsystem_vendor_id;
  uint16_t subsystem_id;
  uint16_t pci_info_offset;
  uint16_t masterhwfunction_offset;      //Offest for SW to get all command function offsets, Don't change the position
  uint16_t masterdatatable_offset;       //Offest for SW to get all data table offsets, Don't change the position
  uint16_t reserved;
  uint32_t pspdirtableoffset;
};

/*==============================hw function portion======================================================================*/


/****************************************************************************   
* Structures used in Command.mtb, each function name is not given here since those function could change from time to time
* The real functionality of each function is associated with the parameter structure version when defined
* For all internal cmd function definitions, please reference to atomstruct.h
****************************************************************************/   
struct atom_master_list_of_command_functions_v2_1{
  uint16_t asic_init;                   //Function
  uint16_t cmd_function1;               //used as an internal one
  uint16_t cmd_function2;               //used as an internal one
  uint16_t cmd_function3;               //used as an internal one
  uint16_t digxencodercontrol;          //Function   
  uint16_t cmd_function5;               //used as an internal one
  uint16_t cmd_function6;               //used as an internal one 
  uint16_t cmd_function7;               //used as an internal one
  uint16_t cmd_function8;               //used as an internal one
  uint16_t cmd_function9;               //used as an internal one
  uint16_t setengineclock;              //Function
  uint16_t setmemoryclock;              //Function
  uint16_t setpixelclock;               //Function
  uint16_t enabledisppowergating;       //Function            
  uint16_t cmd_function14;              //used as an internal one             
  uint16_t cmd_function15;              //used as an internal one
  uint16_t cmd_function16;              //used as an internal one
  uint16_t cmd_function17;              //used as an internal one
  uint16_t cmd_function18;              //used as an internal one
  uint16_t cmd_function19;              //used as an internal one 
  uint16_t cmd_function20;              //used as an internal one               
  uint16_t cmd_function21;              //used as an internal one
  uint16_t cmd_function22;              //used as an internal one
  uint16_t cmd_function23;              //used as an internal one
  uint16_t cmd_function24;              //used as an internal one
  uint16_t cmd_function25;              //used as an internal one
  uint16_t cmd_function26;              //used as an internal one
  uint16_t cmd_function27;              //used as an internal one
  uint16_t cmd_function28;              //used as an internal one
  uint16_t cmd_function29;              //used as an internal one
  uint16_t cmd_function30;              //used as an internal one
  uint16_t cmd_function31;              //used as an internal one
  uint16_t cmd_function32;              //used as an internal one
  uint16_t cmd_function33;              //used as an internal one
  uint16_t blankcrtc;                   //Function
  uint16_t enablecrtc;                  //Function
  uint16_t cmd_function36;              //used as an internal one
  uint16_t cmd_function37;              //used as an internal one
  uint16_t cmd_function38;              //used as an internal one
  uint16_t cmd_function39;              //used as an internal one
  uint16_t cmd_function40;              //used as an internal one
  uint16_t getsmuclockinfo;             //Function
  uint16_t selectcrtc_source;           //Function
  uint16_t cmd_function43;              //used as an internal one
  uint16_t cmd_function44;              //used as an internal one
  uint16_t cmd_function45;              //used as an internal one
  uint16_t setdceclock;                 //Function
  uint16_t getmemoryclock;              //Function           
  uint16_t getengineclock;              //Function           
  uint16_t setcrtc_usingdtdtiming;      //Function
  uint16_t externalencodercontrol;      //Function 
  uint16_t cmd_function51;              //used as an internal one
  uint16_t cmd_function52;              //used as an internal one
  uint16_t cmd_function53;              //used as an internal one
  uint16_t processi2cchanneltransaction;//Function           
  uint16_t cmd_function55;              //used as an internal one
  uint16_t cmd_function56;              //used as an internal one
  uint16_t cmd_function57;              //used as an internal one
  uint16_t cmd_function58;              //used as an internal one
  uint16_t cmd_function59;              //used as an internal one
  uint16_t computegpuclockparam;        //Function         
  uint16_t cmd_function61;              //used as an internal one
  uint16_t cmd_function62;              //used as an internal one
  uint16_t dynamicmemorysettings;       //Function function
  uint16_t memorytraining;              //Function function
  uint16_t cmd_function65;              //used as an internal one
  uint16_t cmd_function66;              //used as an internal one
  uint16_t setvoltage;                  //Function
  uint16_t cmd_function68;              //used as an internal one
  uint16_t readefusevalue;              //Function
  uint16_t cmd_function70;              //used as an internal one 
  uint16_t cmd_function71;              //used as an internal one
  uint16_t cmd_function72;              //used as an internal one
  uint16_t cmd_function73;              //used as an internal one
  uint16_t cmd_function74;              //used as an internal one
  uint16_t cmd_function75;              //used as an internal one
  uint16_t dig1transmittercontrol;      //Function
  uint16_t cmd_function77;              //used as an internal one
  uint16_t processauxchanneltransaction;//Function
  uint16_t cmd_function79;              //used as an internal one
  uint16_t getvoltageinfo;              //Function
};

struct atom_master_command_function_v2_1
{
  struct atom_common_table_header  table_header;
  struct atom_master_list_of_command_functions_v2_1 listofcmdfunctions;
};

/**************************************************************************** 
* Structures used in every command function
****************************************************************************/   
struct atom_function_attribute
{
  uint16_t  ws_in_bytes:8;            //[7:0]=Size of workspace in Bytes (in multiple of a dword), 
  uint16_t  ps_in_bytes:7;            //[14:8]=Size of parameter space in Bytes (multiple of a dword), 
  uint16_t  updated_by_util:1;        //[15]=flag to indicate the function is updated by util
};


/**************************************************************************** 
* Common header for all hw functions.
* Every function pointed by _master_list_of_hw_function has this common header. 
* And the pointer actually points to this header.
****************************************************************************/   
struct atom_rom_hw_function_header
{
  struct atom_common_table_header func_header;
  struct atom_function_attribute func_attrib;  
};


/*==============================sw data table portion======================================================================*/
/****************************************************************************
* Structures used in data.mtb, each data table name is not given here since those data table could change from time to time
* The real name of each table is given when its data structure version is defined
****************************************************************************/
struct atom_master_list_of_data_tables_v2_1{
  uint16_t utilitypipeline;               /* Offest for the utility to get parser info,Don't change this position!*/
  uint16_t multimedia_info;               
  uint16_t smc_dpm_info;
  uint16_t sw_datatable3;                 
  uint16_t firmwareinfo;                  /* Shared by various SW components */
  uint16_t sw_datatable5;
  uint16_t lcd_info;                      /* Shared by various SW components */
  uint16_t sw_datatable7;
  uint16_t smu_info;                 
  uint16_t sw_datatable9;
  uint16_t sw_datatable10; 
  uint16_t vram_usagebyfirmware;          /* Shared by various SW components */
  uint16_t gpio_pin_lut;                  /* Shared by various SW components */
  uint16_t sw_datatable13; 
  uint16_t gfx_info;
  uint16_t powerplayinfo;                 /* Shared by various SW components */
  uint16_t sw_datatable16;                
  uint16_t sw_datatable17;
  uint16_t sw_datatable18;
  uint16_t sw_datatable19;                
  uint16_t sw_datatable20;
  uint16_t sw_datatable21;
  uint16_t displayobjectinfo;             /* Shared by various SW components */
  uint16_t indirectioaccess;			  /* used as an internal one */
  uint16_t umc_info;                      /* Shared by various SW components */
  uint16_t sw_datatable25;
  uint16_t sw_datatable26;
  uint16_t dce_info;                      /* Shared by various SW components */
  uint16_t vram_info;                     /* Shared by various SW components */
  uint16_t sw_datatable29;
  uint16_t integratedsysteminfo;          /* Shared by various SW components */
  uint16_t asic_profiling_info;           /* Shared by various SW components */
  uint16_t voltageobject_info;            /* shared by various SW components */
  uint16_t sw_datatable33;
  uint16_t sw_datatable34;
};


struct atom_master_data_table_v2_1
{ 
  struct atom_common_table_header table_header;
  struct atom_master_list_of_data_tables_v2_1 listOfdatatables;
};


struct atom_dtd_format
{
  uint16_t  pixclk;
  uint16_t  h_active;
  uint16_t  h_blanking_time;
  uint16_t  v_active;
  uint16_t  v_blanking_time;
  uint16_t  h_sync_offset;
  uint16_t  h_sync_width;
  uint16_t  v_sync_offset;
  uint16_t  v_syncwidth;
  uint16_t  reserved;
  uint16_t  reserved0;
  uint8_t   h_border;
  uint8_t   v_border;
  uint16_t  miscinfo;
  uint8_t   atom_mode_id;
  uint8_t   refreshrate;
};

/* atom_dtd_format.modemiscinfo defintion */
enum atom_dtd_format_modemiscinfo{
  ATOM_HSYNC_POLARITY    = 0x0002,
  ATOM_VSYNC_POLARITY    = 0x0004,
  ATOM_H_REPLICATIONBY2  = 0x0010,
  ATOM_V_REPLICATIONBY2  = 0x0020,
  ATOM_INTERLACE         = 0x0080,
  ATOM_COMPOSITESYNC     = 0x0040,
};


/* utilitypipeline
 * when format_revision==1 && content_revision==1, then this an info table for atomworks to use during debug session, no structure is associated with it.
 * the location of it can't change
*/


/* 
  ***************************************************************************
    Data Table firmwareinfo  structure
  ***************************************************************************
*/

struct atom_firmware_info_v3_1
{
  struct atom_common_table_header table_header;
  uint32_t firmware_revision;
  uint32_t bootup_sclk_in10khz;
  uint32_t bootup_mclk_in10khz;
  uint32_t firmware_capability;             // enum atombios_firmware_capability
  uint32_t main_call_parser_entry;          /* direct address of main parser call in VBIOS binary. */
  uint32_t bios_scratch_reg_startaddr;      // 1st bios scratch register dword address 
  uint16_t bootup_vddc_mv;
  uint16_t bootup_vddci_mv; 
  uint16_t bootup_mvddc_mv;
  uint16_t bootup_vddgfx_mv;
  uint8_t  mem_module_id;       
  uint8_t  coolingsolution_id;              /*0: Air cooling; 1: Liquid cooling ... */
  uint8_t  reserved1[2];
  uint32_t mc_baseaddr_high;
  uint32_t mc_baseaddr_low;
  uint32_t reserved2[6];
};

/* Total 32bit cap indication */
enum atombios_firmware_capability
{
	ATOM_FIRMWARE_CAP_FIRMWARE_POSTED = 0x00000001,
	ATOM_FIRMWARE_CAP_GPU_VIRTUALIZATION  = 0x00000002,
	ATOM_FIRMWARE_CAP_WMI_SUPPORT  = 0x00000040,
	ATOM_FIRMWARE_CAP_HWEMU_ENABLE  = 0x00000080,
	ATOM_FIRMWARE_CAP_HWEMU_UMC_CFG = 0x00000100,
	ATOM_FIRMWARE_CAP_SRAM_ECC      = 0x00000200,
	ATOM_FIRMWARE_CAP_ENABLE_2STAGE_BIST_TRAINING  = 0x00000400,
	ATOM_FIRMWARE_CAP_ENABLE_2ND_USB20PORT = 0x0008000,
	ATOM_FIRMWARE_CAP_DYNAMIC_BOOT_CFG_ENABLE = 0x0020000,
};

enum atom_cooling_solution_id{
  AIR_COOLING    = 0x00,
  LIQUID_COOLING = 0x01
};

struct atom_firmware_info_v3_2 {
  struct atom_common_table_header table_header;
  uint32_t firmware_revision;
  uint32_t bootup_sclk_in10khz;
  uint32_t bootup_mclk_in10khz;
  uint32_t firmware_capability;             // enum atombios_firmware_capability
  uint32_t main_call_parser_entry;          /* direct address of main parser call in VBIOS binary. */
  uint32_t bios_scratch_reg_startaddr;      // 1st bios scratch register dword address
  uint16_t bootup_vddc_mv;
  uint16_t bootup_vddci_mv;
  uint16_t bootup_mvddc_mv;
  uint16_t bootup_vddgfx_mv;
  uint8_t  mem_module_id;
  uint8_t  coolingsolution_id;              /*0: Air cooling; 1: Liquid cooling ... */
  uint8_t  reserved1[2];
  uint32_t mc_baseaddr_high;
  uint32_t mc_baseaddr_low;
  uint8_t  board_i2c_feature_id;            // enum of atom_board_i2c_feature_id_def
  uint8_t  board_i2c_feature_gpio_id;       // i2c id find in gpio_lut data table gpio_id
  uint8_t  board_i2c_feature_slave_addr;
  uint8_t  reserved3;
  uint16_t bootup_mvddq_mv;
  uint16_t bootup_mvpp_mv;
  uint32_t zfbstartaddrin16mb;
  uint32_t reserved2[3];
};

struct atom_firmware_info_v3_3
{
  struct atom_common_table_header table_header;
  uint32_t firmware_revision;
  uint32_t bootup_sclk_in10khz;
  uint32_t bootup_mclk_in10khz;
  uint32_t firmware_capability;             // enum atombios_firmware_capability
  uint32_t main_call_parser_entry;          /* direct address of main parser call in VBIOS binary. */
  uint32_t bios_scratch_reg_startaddr;      // 1st bios scratch register dword address
  uint16_t bootup_vddc_mv;
  uint16_t bootup_vddci_mv;
  uint16_t bootup_mvddc_mv;
  uint16_t bootup_vddgfx_mv;
  uint8_t  mem_module_id;
  uint8_t  coolingsolution_id;              /*0: Air cooling; 1: Liquid cooling ... */
  uint8_t  reserved1[2];
  uint32_t mc_baseaddr_high;
  uint32_t mc_baseaddr_low;
  uint8_t  board_i2c_feature_id;            // enum of atom_board_i2c_feature_id_def
  uint8_t  board_i2c_feature_gpio_id;       // i2c id find in gpio_lut data table gpio_id
  uint8_t  board_i2c_feature_slave_addr;
  uint8_t  reserved3;
  uint16_t bootup_mvddq_mv;
  uint16_t bootup_mvpp_mv;
  uint32_t zfbstartaddrin16mb;
  uint32_t pplib_pptable_id;                // if pplib_pptable_id!=0, pplib get powerplay table inside driver instead of from VBIOS
  uint32_t reserved2[2];
};

struct atom_firmware_info_v3_4 {
	struct atom_common_table_header table_header;
	uint32_t firmware_revision;
	uint32_t bootup_sclk_in10khz;
	uint32_t bootup_mclk_in10khz;
	uint32_t firmware_capability;             // enum atombios_firmware_capability
	uint32_t main_call_parser_entry;          /* direct address of main parser call in VBIOS binary. */
	uint32_t bios_scratch_reg_startaddr;      // 1st bios scratch register dword address
	uint16_t bootup_vddc_mv;
	uint16_t bootup_vddci_mv;
	uint16_t bootup_mvddc_mv;
	uint16_t bootup_vddgfx_mv;
	uint8_t  mem_module_id;
	uint8_t  coolingsolution_id;              /*0: Air cooling; 1: Liquid cooling ... */
	uint8_t  reserved1[2];
	uint32_t mc_baseaddr_high;
	uint32_t mc_baseaddr_low;
	uint8_t  board_i2c_feature_id;            // enum of atom_board_i2c_feature_id_def
	uint8_t  board_i2c_feature_gpio_id;       // i2c id find in gpio_lut data table gpio_id
	uint8_t  board_i2c_feature_slave_addr;
	uint8_t  ras_rom_i2c_slave_addr;
	uint16_t bootup_mvddq_mv;
	uint16_t bootup_mvpp_mv;
	uint32_t zfbstartaddrin16mb;
	uint32_t pplib_pptable_id;                // if pplib_pptable_id!=0, pplib get powerplay table inside driver instead of from VBIOS
	uint32_t mvdd_ratio;                      // mvdd_raio = (real mvdd in power rail)*1000/(mvdd_output_from_svi2)
	uint16_t hw_bootup_vddgfx_mv;             // hw default vddgfx voltage level decide by board strap
	uint16_t hw_bootup_vddc_mv;               // hw default vddc voltage level decide by board strap
	uint16_t hw_bootup_mvddc_mv;              // hw default mvddc voltage level decide by board strap
	uint16_t hw_bootup_vddci_mv;              // hw default vddci voltage level decide by board strap
	uint32_t maco_pwrlimit_mw;                // bomaco mode power limit in unit of m-watt
	uint32_t usb_pwrlimit_mw;                 // power limit when USB is enable in unit of m-watt
	uint32_t fw_reserved_size_in_kb;          // VBIOS reserved extra fw size in unit of kb.
        uint32_t pspbl_init_done_reg_addr;
        uint32_t pspbl_init_done_value;
        uint32_t pspbl_init_done_check_timeout;   // time out in unit of us when polling pspbl init done
        uint32_t reserved[2];
};

struct atom_firmware_info_v3_5 {
  struct atom_common_table_header table_header;
  uint32_t firmware_revision;
  uint32_t bootup_clk_reserved[2];
  uint32_t firmware_capability;             // enum atombios_firmware_capability
  uint32_t fw_protect_region_size_in_kb;    /* FW allocate a write protect region at top of FB. */
  uint32_t bios_scratch_reg_startaddr;      // 1st bios scratch register dword address
  uint32_t bootup_voltage_reserved[2];
  uint8_t  mem_module_id;
  uint8_t  coolingsolution_id;              /*0: Air cooling; 1: Liquid cooling ... */
  uint8_t  hw_blt_mode;                     //0:HW_BLT_DMA_PIO_MODE; 1:HW_BLT_LITE_SDMA_MODE; 2:HW_BLT_PCI_IO_MODE
  uint8_t  reserved1;
  uint32_t mc_baseaddr_high;
  uint32_t mc_baseaddr_low;
  uint8_t  board_i2c_feature_id;            // enum of atom_board_i2c_feature_id_def
  uint8_t  board_i2c_feature_gpio_id;       // i2c id find in gpio_lut data table gpio_id
  uint8_t  board_i2c_feature_slave_addr;
  uint8_t  ras_rom_i2c_slave_addr;
  uint32_t bootup_voltage_reserved1;
  uint32_t zfb_reserved;
  // if pplib_pptable_id!=0, pplib get powerplay table inside driver instead of from VBIOS
  uint32_t pplib_pptable_id;
  uint32_t hw_voltage_reserved[3];
  uint32_t maco_pwrlimit_mw;                // bomaco mode power limit in unit of m-watt
  uint32_t usb_pwrlimit_mw;                 // power limit when USB is enable in unit of m-watt
  uint32_t fw_reserved_size_in_kb;          // VBIOS reserved extra fw size in unit of kb.
  uint32_t pspbl_init_reserved[3];
  uint32_t spi_rom_size;                    // GPU spi rom size
  uint16_t support_dev_in_objinfo;
  uint16_t disp_phy_tunning_size;
  uint32_t reserved[16];
};
/* 
  ***************************************************************************
    Data Table lcd_info  structure
  ***************************************************************************
*/

struct lcd_info_v2_1
{
  struct  atom_common_table_header table_header;
  struct  atom_dtd_format  lcd_timing;
  uint16_t backlight_pwm;
  uint16_t special_handle_cap;
  uint16_t panel_misc;
  uint16_t lvds_max_slink_pclk;
  uint16_t lvds_ss_percentage;
  uint16_t lvds_ss_rate_10hz;
  uint8_t  pwr_on_digon_to_de;          /*all pwr sequence numbers below are in uint of 4ms*/
  uint8_t  pwr_on_de_to_vary_bl;
  uint8_t  pwr_down_vary_bloff_to_de;
  uint8_t  pwr_down_de_to_digoff;
  uint8_t  pwr_off_delay;
  uint8_t  pwr_on_vary_bl_to_blon;
  uint8_t  pwr_down_bloff_to_vary_bloff;
  uint8_t  panel_bpc;
  uint8_t  dpcd_edp_config_cap;
  uint8_t  dpcd_max_link_rate;
  uint8_t  dpcd_max_lane_count;
  uint8_t  dpcd_max_downspread;
  uint8_t  min_allowed_bl_level;
  uint8_t  max_allowed_bl_level;
  uint8_t  bootup_bl_level;
  uint8_t  dplvdsrxid;
  uint32_t reserved1[8];
};

/* lcd_info_v2_1.panel_misc defintion */
enum atom_lcd_info_panel_misc{
  ATOM_PANEL_MISC_FPDI            =0x0002,
};

//uceDPToLVDSRxId
enum atom_lcd_info_dptolvds_rx_id
{
  eDP_TO_LVDS_RX_DISABLE                 = 0x00,       // no eDP->LVDS translator chip 
  eDP_TO_LVDS_COMMON_ID                  = 0x01,       // common eDP->LVDS translator chip without AMD SW init
  eDP_TO_LVDS_REALTEK_ID                 = 0x02,       // Realtek tansaltor which require AMD SW init
};

    
/* 
  ***************************************************************************
    Data Table gpio_pin_lut  structure
  ***************************************************************************
*/

struct atom_gpio_pin_assignment
{
  uint32_t data_a_reg_index;
  uint8_t  gpio_bitshift;
  uint8_t  gpio_mask_bitshift;
  uint8_t  gpio_id;
  uint8_t  reserved;
};

/* atom_gpio_pin_assignment.gpio_id definition */
enum atom_gpio_pin_assignment_gpio_id {
  I2C_HW_LANE_MUX        =0x0f, /* only valid when bit7=1 */
  I2C_HW_ENGINE_ID_MASK  =0x70, /* only valid when bit7=1 */ 
  I2C_HW_CAP             =0x80, /*only when the I2C_HW_CAP is set, the pin ID is assigned to an I2C pin pair, otherwise, it's an generic GPIO pin */

  /* gpio_id pre-define id for multiple usage */
  /* GPIO use to control PCIE_VDDC in certain SLT board */
  PCIE_VDDC_CONTROL_GPIO_PINID = 56,
  /* if PP_AC_DC_SWITCH_GPIO_PINID in Gpio_Pin_LutTable, AC/DC swithing feature is enable */
  PP_AC_DC_SWITCH_GPIO_PINID = 60,
  /* VDDC_REGULATOR_VRHOT_GPIO_PINID in Gpio_Pin_LutTable, VRHot feature is enable */
  VDDC_VRHOT_GPIO_PINID = 61,
  /*if VDDC_PCC_GPIO_PINID in GPIO_LUTable, Peak Current Control feature is enabled */
  VDDC_PCC_GPIO_PINID = 62,
  /* Only used on certain SLT/PA board to allow utility to cut Efuse. */
  EFUSE_CUT_ENABLE_GPIO_PINID = 63,
  /* ucGPIO=DRAM_SELF_REFRESH_GPIO_PIND uses  for memory self refresh (ucGPIO=0, DRAM self-refresh; ucGPIO= */
  DRAM_SELF_REFRESH_GPIO_PINID = 64,
  /* Thermal interrupt output->system thermal chip GPIO pin */
  THERMAL_INT_OUTPUT_GPIO_PINID =65,
};


struct atom_gpio_pin_lut_v2_1
{
  struct  atom_common_table_header  table_header;
  /*the real number of this included in the structure is calcualted by using the (whole structure size - the header size)/size of atom_gpio_pin_lut  */
  struct  atom_gpio_pin_assignment  gpio_pin[];
};


/*
 * VBIOS/PRE-OS always reserve a FB region at the top of frame buffer. driver should not write
 * access that region. driver can allocate their own reservation region as long as it does not
 * overlap firwmare's reservation region.
 * if (pre-NV1X) atom data table firmwareInfoTable version < 3.3:
 * in this case, atom data table vram_usagebyfirmwareTable version always <= 2.1
 *   if VBIOS/UEFI GOP is posted:
 *     VBIOS/UEFIGOP update used_by_firmware_in_kb = total reserved size by VBIOS
 *     update start_address_in_kb = total_mem_size_in_kb - used_by_firmware_in_kb;
 *     ( total_mem_size_in_kb = reg(CONFIG_MEMSIZE)<<10)
 *     driver can allocate driver reservation region under firmware reservation,
 *     used_by_driver_in_kb = driver reservation size
 *     driver reservation start address =  (start_address_in_kb - used_by_driver_in_kb)
 *     Comment1[hchan]: There is only one reservation at the beginning of the FB reserved by
 *     host driver. Host driver would overwrite the table with the following
 *     used_by_firmware_in_kb = total reserved size for pf-vf info exchange and
 *     set SRIOV_MSG_SHARE_RESERVATION mask start_address_in_kb = 0
 *   else there is no VBIOS reservation region:
 *     driver must allocate driver reservation region at top of FB.
 *     driver set used_by_driver_in_kb = driver reservation size
 *     driver reservation start address =  (total_mem_size_in_kb - used_by_driver_in_kb)
 *     same as Comment1
 * else (NV1X and after):
 *   if VBIOS/UEFI GOP is posted:
 *     VBIOS/UEFIGOP update:
 *       used_by_firmware_in_kb = atom_firmware_Info_v3_3.fw_reserved_size_in_kb;
 *       start_address_in_kb = total_mem_size_in_kb - used_by_firmware_in_kb;
 *       (total_mem_size_in_kb = reg(CONFIG_MEMSIZE)<<10)
 *   if vram_usagebyfirmwareTable version <= 2.1:
 *     driver can allocate driver reservation region under firmware reservation,
 *     driver set used_by_driver_in_kb = driver reservation size
 *     driver reservation start address = start_address_in_kb - used_by_driver_in_kb
 *     same as Comment1
 *   else driver can:
 *     allocate it reservation any place as long as it does overlap pre-OS FW reservation area
 *     set used_by_driver_region0_in_kb = driver reservation size
 *     set driver_region0_start_address_in_kb =  driver reservation region start address
 *     Comment2[hchan]: Host driver can set used_by_firmware_in_kb and start_address_in_kb to
 *     zero as the reservation for VF as it doesn’t exist.  And Host driver should also
 *     update atom_firmware_Info table to remove the same VBIOS reservation as well.
 */

struct vram_usagebyfirmware_v2_1
{
	struct  atom_common_table_header  table_header;
	uint32_t  start_address_in_kb;
	uint16_t  used_by_firmware_in_kb;
	uint16_t  used_by_driver_in_kb;
};

struct vram_usagebyfirmware_v2_2 {
	struct  atom_common_table_header  table_header;
	uint32_t  fw_region_start_address_in_kb;
	uint16_t  used_by_firmware_in_kb;
	uint16_t  reserved;
	uint32_t  driver_region0_start_address_in_kb;
	uint32_t  used_by_driver_region0_in_kb;
	uint32_t  reserved32[7];
};

/* 
  ***************************************************************************
    Data Table displayobjectinfo  structure
  ***************************************************************************
*/

enum atom_object_record_type_id {
	ATOM_I2C_RECORD_TYPE = 1,
	ATOM_HPD_INT_RECORD_TYPE = 2,
	ATOM_CONNECTOR_CAP_RECORD_TYPE = 3,
	ATOM_CONNECTOR_SPEED_UPTO = 4,
	ATOM_OBJECT_GPIO_CNTL_RECORD_TYPE = 9,
	ATOM_CONNECTOR_HPDPIN_LUT_RECORD_TYPE = 16,
	ATOM_CONNECTOR_AUXDDC_LUT_RECORD_TYPE = 17,
	ATOM_ENCODER_CAP_RECORD_TYPE = 20,
	ATOM_BRACKET_LAYOUT_RECORD_TYPE = 21,
	ATOM_CONNECTOR_FORCED_TMDS_CAP_RECORD_TYPE = 22,
	ATOM_DISP_CONNECTOR_CAPS_RECORD_TYPE = 23,
	ATOM_BRACKET_LAYOUT_V2_RECORD_TYPE = 25,
	ATOM_RECORD_END_TYPE = 0xFF,
};

struct atom_common_record_header
{
  uint8_t record_type;                      //An emun to indicate the record type
  uint8_t record_size;                      //The size of the whole record in byte
};

struct atom_i2c_record
{
  struct atom_common_record_header record_header;   //record_type = ATOM_I2C_RECORD_TYPE
  uint8_t i2c_id; 
  uint8_t i2c_slave_addr;                   //The slave address, it's 0 when the record is attached to connector for DDC
};

struct atom_hpd_int_record
{
  struct atom_common_record_header record_header;  //record_type = ATOM_HPD_INT_RECORD_TYPE
  uint8_t  pin_id;              //Corresponding block in GPIO_PIN_INFO table gives the pin info           
  uint8_t  plugin_pin_state;
};

struct atom_connector_caps_record {
	struct atom_common_record_header
		record_header; //record_type = ATOM_CONN_CAP_RECORD_TYPE
	uint16_t connector_caps; //01b if internal display is checked; 10b if internal BL is checked; 0 of Not
};

struct atom_connector_speed_record {
	struct atom_common_record_header
		record_header; //record_type = ATOM_CONN_SPEED_UPTO
	uint32_t connector_max_speed; // connector Max speed attribute, it sets 8100 in Mhz when DP connector @8.1Ghz.
	uint16_t reserved;
};

// Bit maps for ATOM_ENCODER_CAP_RECORD.usEncoderCap
enum atom_encoder_caps_def
{
  ATOM_ENCODER_CAP_RECORD_HBR2                  =0x01,         // DP1.2 HBR2 is supported by HW encoder, it is retired in NI. the real meaning from SI is MST_EN
  ATOM_ENCODER_CAP_RECORD_MST_EN                =0x01,         // from SI, this bit means DP MST is enable or not. 
  ATOM_ENCODER_CAP_RECORD_HBR2_EN               =0x02,         // DP1.2 HBR2 setting is qualified and HBR2 can be enabled 
  ATOM_ENCODER_CAP_RECORD_HDMI6Gbps_EN          =0x04,         // HDMI2.0 6Gbps enable or not. 
  ATOM_ENCODER_CAP_RECORD_HBR3_EN               =0x08,         // DP1.3 HBR3 is supported by board. 
  ATOM_ENCODER_CAP_RECORD_DP2                   =0x10,         // DP2 is supported by ASIC/board.
  ATOM_ENCODER_CAP_RECORD_UHBR10_EN             =0x20,         // DP2.0 UHBR10 settings is supported by board
  ATOM_ENCODER_CAP_RECORD_UHBR13_5_EN           =0x40,         // DP2.0 UHBR13.5 settings is supported by board
  ATOM_ENCODER_CAP_RECORD_UHBR20_EN             =0x80,         // DP2.0 UHBR20 settings is supported by board
  ATOM_ENCODER_CAP_RECORD_USB_C_TYPE            =0x100,        // the DP connector is a USB-C type.
};

struct  atom_encoder_caps_record
{
  struct atom_common_record_header record_header;  //record_type = ATOM_ENCODER_CAP_RECORD_TYPE
  uint32_t  encodercaps;
};

enum atom_connector_caps_def
{
  ATOM_CONNECTOR_CAP_INTERNAL_DISPLAY         = 0x01,        //a cap bit to indicate that this non-embedded display connector is an internal display
  ATOM_CONNECTOR_CAP_INTERNAL_DISPLAY_BL      = 0x02,        //a cap bit to indicate that this internal display requires BL control from GPU, refers to lcd_info for BL PWM freq 
};

struct atom_disp_connector_caps_record
{
  struct atom_common_record_header record_header;
  uint32_t connectcaps;                          
};

//The following generic object gpio pin control record type will replace JTAG_RECORD/FPGA_CONTROL_RECORD/DVI_EXT_INPUT_RECORD above gradually
struct atom_gpio_pin_control_pair
{
  uint8_t gpio_id;               // GPIO_ID, find the corresponding ID in GPIO_LUT table
  uint8_t gpio_pinstate;         // Pin state showing how to set-up the pin
};

struct atom_object_gpio_cntl_record
{
  struct atom_common_record_header record_header;
  uint8_t flag;                   // Future expnadibility
  uint8_t number_of_pins;         // Number of GPIO pins used to control the object
  struct atom_gpio_pin_control_pair gpio[1];              // the real gpio pin pair determined by number of pins ucNumberOfPins
};

//Definitions for GPIO pin state 
enum atom_gpio_pin_control_pinstate_def
{
  GPIO_PIN_TYPE_INPUT             = 0x00,
  GPIO_PIN_TYPE_OUTPUT            = 0x10,
  GPIO_PIN_TYPE_HW_CONTROL        = 0x20,

//For GPIO_PIN_TYPE_OUTPUT the following is defined 
  GPIO_PIN_OUTPUT_STATE_MASK      = 0x01,
  GPIO_PIN_OUTPUT_STATE_SHIFT     = 0,
  GPIO_PIN_STATE_ACTIVE_LOW       = 0x0,
  GPIO_PIN_STATE_ACTIVE_HIGH      = 0x1,
};

// Indexes to GPIO array in GLSync record 
// GLSync record is for Frame Lock/Gen Lock feature.
enum atom_glsync_record_gpio_index_def
{
  ATOM_GPIO_INDEX_GLSYNC_REFCLK    = 0,
  ATOM_GPIO_INDEX_GLSYNC_HSYNC     = 1,
  ATOM_GPIO_INDEX_GLSYNC_VSYNC     = 2,
  ATOM_GPIO_INDEX_GLSYNC_SWAP_REQ  = 3,
  ATOM_GPIO_INDEX_GLSYNC_SWAP_GNT  = 4,
  ATOM_GPIO_INDEX_GLSYNC_INTERRUPT = 5,
  ATOM_GPIO_INDEX_GLSYNC_V_RESET   = 6,
  ATOM_GPIO_INDEX_GLSYNC_SWAP_CNTL = 7,
  ATOM_GPIO_INDEX_GLSYNC_SWAP_SEL  = 8,
  ATOM_GPIO_INDEX_GLSYNC_MAX       = 9,
};


struct atom_connector_hpdpin_lut_record     //record for ATOM_CONNECTOR_HPDPIN_LUT_RECORD_TYPE
{
  struct atom_common_record_header record_header;
  uint8_t hpd_pin_map[8];             
};

struct atom_connector_auxddc_lut_record     //record for ATOM_CONNECTOR_AUXDDC_LUT_RECORD_TYPE
{
  struct atom_common_record_header record_header;
  uint8_t aux_ddc_map[8];
};

struct atom_connector_forced_tmds_cap_record
{
  struct atom_common_record_header record_header;
  // override TMDS capability on this connector when it operate in TMDS mode.  usMaxTmdsClkRate = max TMDS Clock in Mhz/2.5
  uint8_t  maxtmdsclkrate_in2_5mhz;
  uint8_t  reserved;
};    

struct atom_connector_layout_info
{
  uint16_t connectorobjid;
  uint8_t  connector_type;
  uint8_t  position;
};

// define ATOM_CONNECTOR_LAYOUT_INFO.ucConnectorType to describe the display connector size
enum atom_connector_layout_info_connector_type_def
{
  CONNECTOR_TYPE_DVI_D                 = 1,
 
  CONNECTOR_TYPE_HDMI                  = 4,
  CONNECTOR_TYPE_DISPLAY_PORT          = 5,
  CONNECTOR_TYPE_MINI_DISPLAY_PORT     = 6,
};

struct  atom_bracket_layout_record
{
  struct atom_common_record_header record_header;
  uint8_t bracketlen;
  uint8_t bracketwidth;
  uint8_t conn_num;
  uint8_t reserved;
  struct atom_connector_layout_info  conn_info[1];
};
struct atom_bracket_layout_record_v2 {
	struct atom_common_record_header
		record_header; //record_type =  ATOM_BRACKET_LAYOUT_RECORD_TYPE
	uint8_t bracketlen; //Bracket Length in mm
	uint8_t bracketwidth; //Bracket Width in mm
	uint8_t conn_num; //Connector numbering
	uint8_t mini_type; //Mini Type (0 = Normal; 1 = Mini)
	uint8_t reserved1;
	uint8_t reserved2;
};

enum atom_connector_layout_info_mini_type_def {
	MINI_TYPE_NORMAL = 0,
	MINI_TYPE_MINI = 1,
};

enum atom_display_device_tag_def{
  ATOM_DISPLAY_LCD1_SUPPORT            = 0x0002, //an embedded display is either an LVDS or eDP signal type of display
  ATOM_DISPLAY_LCD2_SUPPORT			       = 0x0020, //second edp device tag 0x0020 for backward compability
  ATOM_DISPLAY_DFP1_SUPPORT            = 0x0008,
  ATOM_DISPLAY_DFP2_SUPPORT            = 0x0080,
  ATOM_DISPLAY_DFP3_SUPPORT            = 0x0200,
  ATOM_DISPLAY_DFP4_SUPPORT            = 0x0400,
  ATOM_DISPLAY_DFP5_SUPPORT            = 0x0800,
  ATOM_DISPLAY_DFP6_SUPPORT            = 0x0040,
  ATOM_DISPLAY_DFPx_SUPPORT            = 0x0ec8,
};

struct atom_display_object_path_v2
{
  uint16_t display_objid;                  //Connector Object ID or Misc Object ID
  uint16_t disp_recordoffset;
  uint16_t encoderobjid;                   //first encoder closer to the connector, could be either an external or intenal encoder
  uint16_t extencoderobjid;                //2nd encoder after the first encoder, from the connector point of view;
  uint16_t encoder_recordoffset;
  uint16_t extencoder_recordoffset;
  uint16_t device_tag;                     //a supported device vector, each display path starts with this.the paths are enumerated in the way of priority, a path appears first 
  uint8_t  priority_id;
  uint8_t  reserved;
};

struct atom_display_object_path_v3 {
	uint16_t display_objid; //Connector Object ID or Misc Object ID
	uint16_t disp_recordoffset;
	uint16_t encoderobjid; //first encoder closer to the connector, could be either an external or intenal encoder
	uint16_t reserved1; //only on USBC case, otherwise always = 0
	uint16_t reserved2; //reserved and always = 0
	uint16_t reserved3; //reserved and always = 0
	//a supported device vector, each display path starts with this.the paths are enumerated in the way of priority,
	//a path appears first
	uint16_t device_tag;
	uint16_t reserved4; //reserved and always = 0
};

struct display_object_info_table_v1_4
{
  struct    atom_common_table_header  table_header;
  uint16_t  supporteddevices;
  uint8_t   number_of_path;
  uint8_t   reserved;
  struct    atom_display_object_path_v2 display_path[8];   //the real number of this included in the structure is calculated by using the (whole structure size - the header size- number_of_path)/size of atom_display_object_path
};

struct display_object_info_table_v1_5 {
	struct atom_common_table_header table_header;
	uint16_t supporteddevices;
	uint8_t number_of_path;
	uint8_t reserved;
	// the real number of this included in the structure is calculated by using the
	// (whole structure size - the header size- number_of_path)/size of atom_display_object_path
	struct atom_display_object_path_v3 display_path[8];
};

/* 
  ***************************************************************************
    Data Table dce_info  structure
  ***************************************************************************
*/
struct atom_display_controller_info_v4_1
{
  struct  atom_common_table_header  table_header;
  uint32_t display_caps;
  uint32_t bootup_dispclk_10khz;
  uint16_t dce_refclk_10khz;
  uint16_t i2c_engine_refclk_10khz;
  uint16_t dvi_ss_percentage;       // in unit of 0.001%
  uint16_t dvi_ss_rate_10hz;        
  uint16_t hdmi_ss_percentage;      // in unit of 0.001%
  uint16_t hdmi_ss_rate_10hz;
  uint16_t dp_ss_percentage;        // in unit of 0.001%
  uint16_t dp_ss_rate_10hz;
  uint8_t  dvi_ss_mode;             // enum of atom_spread_spectrum_mode
  uint8_t  hdmi_ss_mode;            // enum of atom_spread_spectrum_mode
  uint8_t  dp_ss_mode;              // enum of atom_spread_spectrum_mode 
  uint8_t  ss_reserved;
  uint8_t  hardcode_mode_num;       // a hardcode mode number defined in StandardVESA_TimingTable when a CRT or DFP EDID is not available
  uint8_t  reserved1[3];
  uint16_t dpphy_refclk_10khz;  
  uint16_t reserved2;
  uint8_t  dceip_min_ver;
  uint8_t  dceip_max_ver;
  uint8_t  max_disp_pipe_num;
  uint8_t  max_vbios_active_disp_pipe_num;
  uint8_t  max_ppll_num;
  uint8_t  max_disp_phy_num;
  uint8_t  max_aux_pairs;
  uint8_t  remotedisplayconfig;
  uint8_t  reserved3[8];
};

struct atom_display_controller_info_v4_2
{
  struct  atom_common_table_header  table_header;
  uint32_t display_caps;            
  uint32_t bootup_dispclk_10khz;
  uint16_t dce_refclk_10khz;
  uint16_t i2c_engine_refclk_10khz;
  uint16_t dvi_ss_percentage;       // in unit of 0.001%   
  uint16_t dvi_ss_rate_10hz;
  uint16_t hdmi_ss_percentage;      // in unit of 0.001%
  uint16_t hdmi_ss_rate_10hz;
  uint16_t dp_ss_percentage;        // in unit of 0.001%
  uint16_t dp_ss_rate_10hz;
  uint8_t  dvi_ss_mode;             // enum of atom_spread_spectrum_mode
  uint8_t  hdmi_ss_mode;            // enum of atom_spread_spectrum_mode
  uint8_t  dp_ss_mode;              // enum of atom_spread_spectrum_mode 
  uint8_t  ss_reserved;
  uint8_t  dfp_hardcode_mode_num;   // DFP hardcode mode number defined in StandardVESA_TimingTable when EDID is not available
  uint8_t  dfp_hardcode_refreshrate;// DFP hardcode mode refreshrate defined in StandardVESA_TimingTable when EDID is not available
  uint8_t  vga_hardcode_mode_num;   // VGA hardcode mode number defined in StandardVESA_TimingTable when EDID is not avablable
  uint8_t  vga_hardcode_refreshrate;// VGA hardcode mode number defined in StandardVESA_TimingTable when EDID is not avablable
  uint16_t dpphy_refclk_10khz;  
  uint16_t reserved2;
  uint8_t  dcnip_min_ver;
  uint8_t  dcnip_max_ver;
  uint8_t  max_disp_pipe_num;
  uint8_t  max_vbios_active_disp_pipe_num;
  uint8_t  max_ppll_num;
  uint8_t  max_disp_phy_num;
  uint8_t  max_aux_pairs;
  uint8_t  remotedisplayconfig;
  uint8_t  reserved3[8];
};

struct atom_display_controller_info_v4_3
{
  struct  atom_common_table_header  table_header;
  uint32_t display_caps;
  uint32_t bootup_dispclk_10khz;
  uint16_t dce_refclk_10khz;
  uint16_t i2c_engine_refclk_10khz;
  uint16_t dvi_ss_percentage;       // in unit of 0.001%
  uint16_t dvi_ss_rate_10hz;
  uint16_t hdmi_ss_percentage;      // in unit of 0.001%
  uint16_t hdmi_ss_rate_10hz;
  uint16_t dp_ss_percentage;        // in unit of 0.001%
  uint16_t dp_ss_rate_10hz;
  uint8_t  dvi_ss_mode;             // enum of atom_spread_spectrum_mode
  uint8_t  hdmi_ss_mode;            // enum of atom_spread_spectrum_mode
  uint8_t  dp_ss_mode;              // enum of atom_spread_spectrum_mode
  uint8_t  ss_reserved;
  uint8_t  dfp_hardcode_mode_num;   // DFP hardcode mode number defined in StandardVESA_TimingTable when EDID is not available
  uint8_t  dfp_hardcode_refreshrate;// DFP hardcode mode refreshrate defined in StandardVESA_TimingTable when EDID is not available
  uint8_t  vga_hardcode_mode_num;   // VGA hardcode mode number defined in StandardVESA_TimingTable when EDID is not avablable
  uint8_t  vga_hardcode_refreshrate;// VGA hardcode mode number defined in StandardVESA_TimingTable when EDID is not avablable
  uint16_t dpphy_refclk_10khz;
  uint16_t reserved2;
  uint8_t  dcnip_min_ver;
  uint8_t  dcnip_max_ver;
  uint8_t  max_disp_pipe_num;
  uint8_t  max_vbios_active_disp_pipe_num;
  uint8_t  max_ppll_num;
  uint8_t  max_disp_phy_num;
  uint8_t  max_aux_pairs;
  uint8_t  remotedisplayconfig;
  uint8_t  reserved3[8];
};

struct atom_display_controller_info_v4_4 {
	struct atom_common_table_header table_header;
	uint32_t display_caps;
	uint32_t bootup_dispclk_10khz;
	uint16_t dce_refclk_10khz;
	uint16_t i2c_engine_refclk_10khz;
	uint16_t dvi_ss_percentage;	 // in unit of 0.001%
	uint16_t dvi_ss_rate_10hz;
	uint16_t hdmi_ss_percentage;	 // in unit of 0.001%
	uint16_t hdmi_ss_rate_10hz;
	uint16_t dp_ss_percentage;	 // in unit of 0.001%
	uint16_t dp_ss_rate_10hz;
	uint8_t dvi_ss_mode;		 // enum of atom_spread_spectrum_mode
	uint8_t hdmi_ss_mode;		 // enum of atom_spread_spectrum_mode
	uint8_t dp_ss_mode;		 // enum of atom_spread_spectrum_mode
	uint8_t ss_reserved;
	uint8_t dfp_hardcode_mode_num;	 // DFP hardcode mode number defined in StandardVESA_TimingTable when EDID is not available
	uint8_t dfp_hardcode_refreshrate;// DFP hardcode mode refreshrate defined in StandardVESA_TimingTable when EDID is not available
	uint8_t vga_hardcode_mode_num;	 // VGA hardcode mode number defined in StandardVESA_TimingTable when EDID is not avablable
	uint8_t vga_hardcode_refreshrate;// VGA hardcode mode number defined in StandardVESA_TimingTable when EDID is not avablable
	uint16_t dpphy_refclk_10khz;
	uint16_t hw_chip_id;
	uint8_t dcnip_min_ver;
	uint8_t dcnip_max_ver;
	uint8_t max_disp_pipe_num;
	uint8_t max_vbios_active_disp_pipum;
	uint8_t max_ppll_num;
	uint8_t max_disp_phy_num;
	uint8_t max_aux_pairs;
	uint8_t remotedisplayconfig;
	uint32_t dispclk_pll_vco_freq;
	uint32_t dp_ref_clk_freq;
	uint32_t max_mclk_chg_lat;	 // Worst case blackout duration for a memory clock frequency (p-state) change, units of 100s of ns (0.1 us)
	uint32_t max_sr_exit_lat;	 // Worst case memory self refresh exit time, units of 100ns of ns (0.1us)
	uint32_t max_sr_enter_exit_lat;	 // Worst case memory self refresh entry followed by immediate exit time, units of 100ns of ns (0.1us)
	uint16_t dc_golden_table_offset; // point of struct of atom_dc_golden_table_vxx
	uint16_t dc_golden_table_ver;
	uint32_t reserved3[3];
};

struct atom_dc_golden_table_v1
{
	uint32_t aux_dphy_rx_control0_val;
	uint32_t aux_dphy_tx_control_val;
	uint32_t aux_dphy_rx_control1_val;
	uint32_t dc_gpio_aux_ctrl_0_val;
	uint32_t dc_gpio_aux_ctrl_1_val;
	uint32_t dc_gpio_aux_ctrl_2_val;
	uint32_t dc_gpio_aux_ctrl_3_val;
	uint32_t dc_gpio_aux_ctrl_4_val;
	uint32_t dc_gpio_aux_ctrl_5_val;
	uint32_t reserved[23];
};

enum dce_info_caps_def {
	// only for VBIOS
	DCE_INFO_CAPS_FORCE_DISPDEV_CONNECTED = 0x02,
	// only for VBIOS
	DCE_INFO_CAPS_DISABLE_DFP_DP_HBR2 = 0x04,
	// only for VBIOS
	DCE_INFO_CAPS_ENABLE_INTERLAC_TIMING = 0x08,
	// only for VBIOS
	DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE = 0x20,
	DCE_INFO_CAPS_VBIOS_LTTPR_TRANSPARENT_ENABLE = 0x40,
};

struct atom_display_controller_info_v4_5
{
  struct  atom_common_table_header  table_header;
  uint32_t display_caps;
  uint32_t bootup_dispclk_10khz;
  uint16_t dce_refclk_10khz;
  uint16_t i2c_engine_refclk_10khz;
  uint16_t dvi_ss_percentage;       // in unit of 0.001%
  uint16_t dvi_ss_rate_10hz;
  uint16_t hdmi_ss_percentage;      // in unit of 0.001%
  uint16_t hdmi_ss_rate_10hz;
  uint16_t dp_ss_percentage;        // in unit of 0.001%
  uint16_t dp_ss_rate_10hz;
  uint8_t  dvi_ss_mode;             // enum of atom_spread_spectrum_mode
  uint8_t  hdmi_ss_mode;            // enum of atom_spread_spectrum_mode
  uint8_t  dp_ss_mode;              // enum of atom_spread_spectrum_mode
  uint8_t  ss_reserved;
  // DFP hardcode mode number defined in StandardVESA_TimingTable when EDID is not available
  uint8_t  dfp_hardcode_mode_num;
  // DFP hardcode mode refreshrate defined in StandardVESA_TimingTable when EDID is not available
  uint8_t  dfp_hardcode_refreshrate;
  // VGA hardcode mode number defined in StandardVESA_TimingTable when EDID is not avablable
  uint8_t  vga_hardcode_mode_num;
  // VGA hardcode mode number defined in StandardVESA_TimingTable when EDID is not avablable
  uint8_t  vga_hardcode_refreshrate;
  uint16_t dpphy_refclk_10khz;
  uint16_t hw_chip_id;
  uint8_t  dcnip_min_ver;
  uint8_t  dcnip_max_ver;
  uint8_t  max_disp_pipe_num;
  uint8_t  max_vbios_active_disp_pipe_num;
  uint8_t  max_ppll_num;
  uint8_t  max_disp_phy_num;
  uint8_t  max_aux_pairs;
  uint8_t  remotedisplayconfig;
  uint32_t dispclk_pll_vco_freq;
  uint32_t dp_ref_clk_freq;
  // Worst case blackout duration for a memory clock frequency (p-state) change, units of 100s of ns (0.1 us)
  uint32_t max_mclk_chg_lat;
  // Worst case memory self refresh exit time, units of 100ns of ns (0.1us)
  uint32_t max_sr_exit_lat;
  // Worst case memory self refresh entry followed by immediate exit time, units of 100ns of ns (0.1us)
  uint32_t max_sr_enter_exit_lat;
  uint16_t dc_golden_table_offset;  // point of struct of atom_dc_golden_table_vxx
  uint16_t dc_golden_table_ver;
  uint32_t aux_dphy_rx_control0_val;
  uint32_t aux_dphy_tx_control_val;
  uint32_t aux_dphy_rx_control1_val;
  uint32_t dc_gpio_aux_ctrl_0_val;
  uint32_t dc_gpio_aux_ctrl_1_val;
  uint32_t dc_gpio_aux_ctrl_2_val;
  uint32_t dc_gpio_aux_ctrl_3_val;
  uint32_t dc_gpio_aux_ctrl_4_val;
  uint32_t dc_gpio_aux_ctrl_5_val;
  uint32_t reserved[26];
};

/* 
  ***************************************************************************
    Data Table ATOM_EXTERNAL_DISPLAY_CONNECTION_INFO  structure
  ***************************************************************************
*/
struct atom_ext_display_path
{
  uint16_t  device_tag;                      //A bit vector to show what devices are supported 
  uint16_t  device_acpi_enum;                //16bit device ACPI id. 
  uint16_t  connectorobjid;                  //A physical connector for displays to plug in, using object connector definitions
  uint8_t   auxddclut_index;                 //An index into external AUX/DDC channel LUT
  uint8_t   hpdlut_index;                    //An index into external HPD pin LUT
  uint16_t  ext_encoder_objid;               //external encoder object id
  uint8_t   channelmapping;                  // if ucChannelMapping=0, using default one to one mapping
  uint8_t   chpninvert;                      // bit vector for up to 8 lanes, =0: P and N is not invert, =1 P and N is inverted
  uint16_t  caps;
  uint16_t  reserved; 
};

//usCaps
enum ext_display_path_cap_def {
	EXT_DISPLAY_PATH_CAPS__HBR2_DISABLE =           0x0001,
	EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN =         0x0002,
	EXT_DISPLAY_PATH_CAPS__EXT_CHIP_MASK =          0x007C,
	EXT_DISPLAY_PATH_CAPS__HDMI20_PI3EQX1204 =      (0x01 << 2), //PI redriver chip
	EXT_DISPLAY_PATH_CAPS__HDMI20_TISN65DP159RSBT = (0x02 << 2), //TI retimer chip
	EXT_DISPLAY_PATH_CAPS__HDMI20_PARADE_PS175 =    (0x03 << 2)  //Parade DP->HDMI recoverter chip
};

struct atom_external_display_connection_info
{
  struct  atom_common_table_header  table_header;
  uint8_t                  guid[16];                                  // a GUID is a 16 byte long string
  struct atom_ext_display_path path[7];                               // total of fixed 7 entries.
  uint8_t                  checksum;                                  // a simple Checksum of the sum of whole structure equal to 0x0. 
  uint8_t                  stereopinid;                               // use for eDP panel
  uint8_t                  remotedisplayconfig;
  uint8_t                  edptolvdsrxid;
  uint8_t                  fixdpvoltageswing;                         // usCaps[1]=1, this indicate DP_LANE_SET value
  uint8_t                  reserved[3];                               // for potential expansion
};

/* 
  ***************************************************************************
    Data Table integratedsysteminfo  structure
  ***************************************************************************
*/

struct atom_camera_dphy_timing_param
{
  uint8_t  profile_id;       // SENSOR_PROFILES
  uint32_t param;
};

struct atom_camera_dphy_elec_param
{
  uint16_t param[3];
};

struct atom_camera_module_info
{
  uint8_t module_id;                    // 0: Rear, 1: Front right of user, 2: Front left of user
  uint8_t module_name[8];
  struct atom_camera_dphy_timing_param timingparam[6]; // Exact number is under estimation and confirmation from sensor vendor
};

struct atom_camera_flashlight_info
{
  uint8_t flashlight_id;                // 0: Rear, 1: Front
  uint8_t name[8];
};

struct atom_camera_data
{
  uint32_t versionCode;
  struct atom_camera_module_info cameraInfo[3];      // Assuming 3 camera sensors max
  struct atom_camera_flashlight_info flashInfo;      // Assuming 1 flashlight max
  struct atom_camera_dphy_elec_param dphy_param;
  uint32_t crc_val;         // CRC
};


struct atom_14nm_dpphy_dvihdmi_tuningset
{
  uint32_t max_symclk_in10khz;
  uint8_t encoder_mode;            //atom_encode_mode_def, =2: DVI, =3: HDMI mode
  uint8_t phy_sel;                 //bit vector of phy, bit0= phya, bit1=phyb, ....bit5 = phyf 
  uint16_t margindeemph;           //COMMON_MAR_DEEMPH_NOM[7:0]tx_margin_nom [15:8]deemph_gen1_nom
  uint8_t deemph_6db_4;            //COMMON_SELDEEMPH60[31:24]deemph_6db_4
  uint8_t boostadj;                //CMD_BUS_GLOBAL_FOR_TX_LANE0 [19:16]tx_boost_adj  [20]tx_boost_en  [23:22]tx_binary_ron_code_offset
  uint8_t tx_driver_fifty_ohms;    //COMMON_ZCALCODE_CTRL[21].tx_driver_fifty_ohms
  uint8_t deemph_sel;              //MARGIN_DEEMPH_LANE0.DEEMPH_SEL
};

struct atom_14nm_dpphy_dp_setting{
  uint8_t dp_vs_pemph_level;       //enum of atom_dp_vs_preemph_def
  uint16_t margindeemph;           //COMMON_MAR_DEEMPH_NOM[7:0]tx_margin_nom [15:8]deemph_gen1_nom
  uint8_t deemph_6db_4;            //COMMON_SELDEEMPH60[31:24]deemph_6db_4
  uint8_t boostadj;                //CMD_BUS_GLOBAL_FOR_TX_LANE0 [19:16]tx_boost_adj  [20]tx_boost_en  [23:22]tx_binary_ron_code_offset
};

struct atom_14nm_dpphy_dp_tuningset{
  uint8_t phy_sel;                 // bit vector of phy, bit0= phya, bit1=phyb, ....bit5 = phyf 
  uint8_t version;
  uint16_t table_size;             // size of atom_14nm_dpphy_dp_tuningset
  uint16_t reserved;
  struct atom_14nm_dpphy_dp_setting dptuning[10];
};

struct atom_14nm_dig_transmitter_info_header_v4_0{  
  struct  atom_common_table_header  table_header;  
  uint16_t pcie_phy_tmds_hdmi_macro_settings_offset;     // offset of PCIEPhyTMDSHDMIMacroSettingsTbl 
  uint16_t uniphy_vs_emph_lookup_table_offset;           // offset of UniphyVSEmphLookUpTbl
  uint16_t uniphy_xbar_settings_table_offset;            // offset of UniphyXbarSettingsTbl
};

struct atom_14nm_combphy_tmds_vs_set
{
  uint8_t sym_clk;
  uint8_t dig_mode;
  uint8_t phy_sel;
  uint16_t common_mar_deemph_nom__margin_deemph_val;
  uint8_t common_seldeemph60__deemph_6db_4_val;
  uint8_t cmd_bus_global_for_tx_lane0__boostadj_val ;
  uint8_t common_zcalcode_ctrl__tx_driver_fifty_ohms_val;
  uint8_t margin_deemph_lane0__deemph_sel_val;         
};

struct atom_DCN_dpphy_dvihdmi_tuningset
{
  uint32_t max_symclk_in10khz;
  uint8_t  encoder_mode;           //atom_encode_mode_def, =2: DVI, =3: HDMI mode
  uint8_t  phy_sel;                //bit vector of phy, bit0= phya, bit1=phyb, ....bit5 = phyf 
  uint8_t  tx_eq_main;             // map to RDPCSTX_PHY_FUSE0/1/2/3[5:0](EQ_MAIN)
  uint8_t  tx_eq_pre;              // map to RDPCSTX_PHY_FUSE0/1/2/3[11:6](EQ_PRE)
  uint8_t  tx_eq_post;             // map to RDPCSTX_PHY_FUSE0/1/2/3[17:12](EQ_POST)
  uint8_t  reserved1;
  uint8_t  tx_vboost_lvl;          // tx_vboost_lvl, map to RDPCSTX_PHY_CNTL0.RDPCS_PHY_TX_VBOOST_LVL
  uint8_t  reserved2;
};

struct atom_DCN_dpphy_dp_setting{
  uint8_t dp_vs_pemph_level;       //enum of atom_dp_vs_preemph_def
  uint8_t tx_eq_main;             // map to RDPCSTX_PHY_FUSE0/1/2/3[5:0](EQ_MAIN)
  uint8_t tx_eq_pre;              // map to RDPCSTX_PHY_FUSE0/1/2/3[11:6](EQ_PRE)
  uint8_t tx_eq_post;             // map to RDPCSTX_PHY_FUSE0/1/2/3[17:12](EQ_POST)
  uint8_t tx_vboost_lvl;          // tx_vboost_lvl, map to RDPCSTX_PHY_CNTL0.RDPCS_PHY_TX_VBOOST_LVL
};

struct atom_DCN_dpphy_dp_tuningset{
  uint8_t phy_sel;                 // bit vector of phy, bit0= phya, bit1=phyb, ....bit5 = phyf 
  uint8_t version;
  uint16_t table_size;             // size of atom_14nm_dpphy_dp_setting
  uint16_t reserved;
  struct atom_DCN_dpphy_dp_setting dptunings[10];
};

struct atom_i2c_reg_info {
  uint8_t ucI2cRegIndex;
  uint8_t ucI2cRegVal;
};

struct atom_hdmi_retimer_redriver_set {
  uint8_t HdmiSlvAddr;
  uint8_t HdmiRegNum;
  uint8_t Hdmi6GRegNum;
  struct atom_i2c_reg_info HdmiRegSetting[9];        //For non 6G Hz use
  struct atom_i2c_reg_info Hdmi6GhzRegSetting[3];    //For 6G Hz use.
};

struct atom_integrated_system_info_v1_11
{
  struct  atom_common_table_header  table_header;
  uint32_t  vbios_misc;                       //enum of atom_system_vbiosmisc_def
  uint32_t  gpucapinfo;                       //enum of atom_system_gpucapinf_def   
  uint32_t  system_config;                    
  uint32_t  cpucapinfo;
  uint16_t  gpuclk_ss_percentage;             //unit of 0.001%,   1000 mean 1% 
  uint16_t  gpuclk_ss_type;
  uint16_t  lvds_ss_percentage;               //unit of 0.001%,   1000 mean 1%
  uint16_t  lvds_ss_rate_10hz;
  uint16_t  hdmi_ss_percentage;               //unit of 0.001%,   1000 mean 1%
  uint16_t  hdmi_ss_rate_10hz;
  uint16_t  dvi_ss_percentage;                //unit of 0.001%,   1000 mean 1%
  uint16_t  dvi_ss_rate_10hz;
  uint16_t  dpphy_override;                   // bit vector, enum of atom_sysinfo_dpphy_override_def
  uint16_t  lvds_misc;                        // enum of atom_sys_info_lvds_misc_def
  uint16_t  backlight_pwm_hz;                 // pwm frequency in hz
  uint8_t   memorytype;                       // enum of atom_dmi_t17_mem_type_def, APU memory type indication.
  uint8_t   umachannelnumber;                 // number of memory channels
  uint8_t   pwr_on_digon_to_de;               /* all pwr sequence numbers below are in uint of 4ms */
  uint8_t   pwr_on_de_to_vary_bl;
  uint8_t   pwr_down_vary_bloff_to_de;
  uint8_t   pwr_down_de_to_digoff;
  uint8_t   pwr_off_delay;
  uint8_t   pwr_on_vary_bl_to_blon;
  uint8_t   pwr_down_bloff_to_vary_bloff;
  uint8_t   min_allowed_bl_level;
  uint8_t   htc_hyst_limit;
  uint8_t   htc_tmp_limit;
  uint8_t   reserved1;
  uint8_t   reserved2;
  struct atom_external_display_connection_info extdispconninfo;
  struct atom_14nm_dpphy_dvihdmi_tuningset dvi_tuningset;
  struct atom_14nm_dpphy_dvihdmi_tuningset hdmi_tuningset;
  struct atom_14nm_dpphy_dvihdmi_tuningset hdmi6g_tuningset;
  struct atom_14nm_dpphy_dp_tuningset dp_tuningset;        // rbr 1.62G dp tuning set
  struct atom_14nm_dpphy_dp_tuningset dp_hbr3_tuningset;   // HBR3 dp tuning set
  struct atom_camera_data  camera_info;
  struct atom_hdmi_retimer_redriver_set dp0_retimer_set;   //for DP0
  struct atom_hdmi_retimer_redriver_set dp1_retimer_set;   //for DP1
  struct atom_hdmi_retimer_redriver_set dp2_retimer_set;   //for DP2
  struct atom_hdmi_retimer_redriver_set dp3_retimer_set;   //for DP3
  struct atom_14nm_dpphy_dp_tuningset dp_hbr_tuningset;    //hbr 2.7G dp tuning set
  struct atom_14nm_dpphy_dp_tuningset dp_hbr2_tuningset;   //hbr2 5.4G dp turnig set
  struct atom_14nm_dpphy_dp_tuningset edp_tuningset;       //edp tuning set
  uint32_t  reserved[66];
};

struct atom_integrated_system_info_v1_12
{
  struct  atom_common_table_header  table_header;
  uint32_t  vbios_misc;                       //enum of atom_system_vbiosmisc_def
  uint32_t  gpucapinfo;                       //enum of atom_system_gpucapinf_def   
  uint32_t  system_config;                    
  uint32_t  cpucapinfo;
  uint16_t  gpuclk_ss_percentage;             //unit of 0.001%,   1000 mean 1% 
  uint16_t  gpuclk_ss_type;
  uint16_t  lvds_ss_percentage;               //unit of 0.001%,   1000 mean 1%
  uint16_t  lvds_ss_rate_10hz;
  uint16_t  hdmi_ss_percentage;               //unit of 0.001%,   1000 mean 1%
  uint16_t  hdmi_ss_rate_10hz;
  uint16_t  dvi_ss_percentage;                //unit of 0.001%,   1000 mean 1%
  uint16_t  dvi_ss_rate_10hz;
  uint16_t  dpphy_override;                   // bit vector, enum of atom_sysinfo_dpphy_override_def
  uint16_t  lvds_misc;                        // enum of atom_sys_info_lvds_misc_def
  uint16_t  backlight_pwm_hz;                 // pwm frequency in hz
  uint8_t   memorytype;                       // enum of atom_dmi_t17_mem_type_def, APU memory type indication.
  uint8_t   umachannelnumber;                 // number of memory channels
  uint8_t   pwr_on_digon_to_de;               // all pwr sequence numbers below are in uint of 4ms //
  uint8_t   pwr_on_de_to_vary_bl;
  uint8_t   pwr_down_vary_bloff_to_de;
  uint8_t   pwr_down_de_to_digoff;
  uint8_t   pwr_off_delay;
  uint8_t   pwr_on_vary_bl_to_blon;
  uint8_t   pwr_down_bloff_to_vary_bloff;
  uint8_t   min_allowed_bl_level;
  uint8_t   htc_hyst_limit;
  uint8_t   htc_tmp_limit;
  uint8_t   reserved1;
  uint8_t   reserved2;
  struct atom_external_display_connection_info extdispconninfo;
  struct atom_DCN_dpphy_dvihdmi_tuningset  TMDS_tuningset;
  struct atom_DCN_dpphy_dvihdmi_tuningset  hdmiCLK5_tuningset;
  struct atom_DCN_dpphy_dvihdmi_tuningset  hdmiCLK8_tuningset;
  struct atom_DCN_dpphy_dp_tuningset rbr_tuningset;        // rbr 1.62G dp tuning set
  struct atom_DCN_dpphy_dp_tuningset hbr3_tuningset;   // HBR3 dp tuning set  
  struct atom_camera_data  camera_info;
  struct atom_hdmi_retimer_redriver_set dp0_retimer_set;   //for DP0
  struct atom_hdmi_retimer_redriver_set dp1_retimer_set;   //for DP1
  struct atom_hdmi_retimer_redriver_set dp2_retimer_set;   //for DP2
  struct atom_hdmi_retimer_redriver_set dp3_retimer_set;   //for DP3
  struct atom_DCN_dpphy_dp_tuningset hbr_tuningset;    //hbr 2.7G dp tuning set
  struct atom_DCN_dpphy_dp_tuningset hbr2_tuningset;   //hbr2 5.4G dp turnig set
  struct atom_DCN_dpphy_dp_tuningset edp_tunings;       //edp tuning set
  struct atom_DCN_dpphy_dvihdmi_tuningset  hdmiCLK6_tuningset;
  uint32_t  reserved[63];
};

struct edp_info_table
{
        uint16_t edp_backlight_pwm_hz;
        uint16_t edp_ss_percentage;
        uint16_t edp_ss_rate_10hz;
        uint16_t reserved1;
        uint32_t reserved2;
        uint8_t  edp_pwr_on_off_delay;
        uint8_t  edp_pwr_on_vary_bl_to_blon;
        uint8_t  edp_pwr_down_bloff_to_vary_bloff;
        uint8_t  edp_panel_bpc;
        uint8_t  edp_bootup_bl_level;
        uint8_t  reserved3[3];
        uint32_t reserved4[3];
};

struct atom_integrated_system_info_v2_1
{
        struct  atom_common_table_header  table_header;
        uint32_t  vbios_misc;                       //enum of atom_system_vbiosmisc_def
        uint32_t  gpucapinfo;                       //enum of atom_system_gpucapinf_def
        uint32_t  system_config;
        uint32_t  cpucapinfo;
        uint16_t  gpuclk_ss_percentage;             //unit of 0.001%,   1000 mean 1%
        uint16_t  gpuclk_ss_type;
        uint16_t  dpphy_override;                   // bit vector, enum of atom_sysinfo_dpphy_override_def
        uint8_t   memorytype;                       // enum of atom_dmi_t17_mem_type_def, APU memory type indication.
        uint8_t   umachannelnumber;                 // number of memory channels
        uint8_t   htc_hyst_limit;
        uint8_t   htc_tmp_limit;
        uint8_t   reserved1;
        uint8_t   reserved2;
        struct edp_info_table edp1_info;
        struct edp_info_table edp2_info;
        uint32_t  reserved3[8];
        struct atom_external_display_connection_info extdispconninfo;
        struct atom_DCN_dpphy_dvihdmi_tuningset  TMDS_tuningset;
        struct atom_DCN_dpphy_dvihdmi_tuningset  hdmiCLK5_tuningset; //add clk6
        struct atom_DCN_dpphy_dvihdmi_tuningset  hdmiCLK6_tuningset;
        struct atom_DCN_dpphy_dvihdmi_tuningset  hdmiCLK8_tuningset;
        uint32_t reserved4[6];//reserve 2*sizeof(atom_DCN_dpphy_dvihdmi_tuningset)
        struct atom_DCN_dpphy_dp_tuningset rbr_tuningset;        // rbr 1.62G dp tuning set
        struct atom_DCN_dpphy_dp_tuningset hbr_tuningset;    //hbr 2.7G dp tuning set
        struct atom_DCN_dpphy_dp_tuningset hbr2_tuningset;   //hbr2 5.4G dp turnig set
        struct atom_DCN_dpphy_dp_tuningset hbr3_tuningset;   // HBR3 dp tuning set
        struct atom_DCN_dpphy_dp_tuningset edp_tunings;       //edp tuning set
        uint32_t reserved5[28];//reserve 2*sizeof(atom_DCN_dpphy_dp_tuningset)
        struct atom_hdmi_retimer_redriver_set dp0_retimer_set;   //for DP0
        struct atom_hdmi_retimer_redriver_set dp1_retimer_set;   //for DP1
        struct atom_hdmi_retimer_redriver_set dp2_retimer_set;   //for DP2
        struct atom_hdmi_retimer_redriver_set dp3_retimer_set;   //for DP3
        uint32_t reserved6[30];// reserve size of(atom_camera_data) for camera_info
        uint32_t reserved7[32];

};

struct atom_n6_display_phy_tuning_set {
	uint8_t display_signal_type;
	uint8_t phy_sel;
	uint8_t preset_level;
	uint8_t reserved1;
	uint32_t reserved2;
	uint32_t speed_upto;
	uint8_t tx_vboost_level;
	uint8_t tx_vreg_v2i;
	uint8_t tx_vregdrv_byp;
	uint8_t tx_term_cntl;
	uint8_t tx_peak_level;
	uint8_t tx_slew_en;
	uint8_t tx_eq_pre;
	uint8_t tx_eq_main;
	uint8_t tx_eq_post;
	uint8_t tx_en_inv_pre;
	uint8_t tx_en_inv_post;
	uint8_t reserved3;
	uint32_t reserved4;
	uint32_t reserved5;
	uint32_t reserved6;
};

struct atom_display_phy_tuning_info {
	struct atom_common_table_header table_header;
	struct atom_n6_display_phy_tuning_set disp_phy_tuning[1];
};

struct atom_integrated_system_info_v2_2
{
	struct  atom_common_table_header  table_header;
	uint32_t  vbios_misc;                       //enum of atom_system_vbiosmisc_def
	uint32_t  gpucapinfo;                       //enum of atom_system_gpucapinf_def
	uint32_t  system_config;
	uint32_t  cpucapinfo;
	uint16_t  gpuclk_ss_percentage;             //unit of 0.001%,   1000 mean 1%
	uint16_t  gpuclk_ss_type;
	uint16_t  dpphy_override;                   // bit vector, enum of atom_sysinfo_dpphy_override_def
	uint8_t   memorytype;                       // enum of atom_dmi_t17_mem_type_def, APU memory type indication.
	uint8_t   umachannelnumber;                 // number of memory channels
	uint8_t   htc_hyst_limit;
	uint8_t   htc_tmp_limit;
	uint8_t   reserved1;
	uint8_t   reserved2;
	struct edp_info_table edp1_info;
	struct edp_info_table edp2_info;
	uint32_t  reserved3[8];
	struct atom_external_display_connection_info extdispconninfo;

	uint32_t  reserved4[189];
};

struct uma_carveout_option {
  char       optionName[29];        //max length of string is 28chars + '\0'. Current design is for "minimum", "Medium", "High". This makes entire struct size 64bits
  uint8_t    memoryCarvedGb;        //memory carved out with setting
  uint8_t    memoryRemainingGb;     //memory remaining on system
  union {
    struct _flags {
      uint8_t Auto     : 1;
      uint8_t Custom   : 1;
      uint8_t Reserved : 6;
    } flags;
    uint8_t all8;
  } uma_carveout_option_flags;
};

struct atom_integrated_system_info_v2_3 {
  struct  atom_common_table_header table_header;
  uint32_t  vbios_misc; // enum of atom_system_vbiosmisc_def
  uint32_t  gpucapinfo; // enum of atom_system_gpucapinf_def
  uint32_t  system_config;
  uint32_t  cpucapinfo;
  uint16_t  gpuclk_ss_percentage; // unit of 0.001%,   1000 mean 1%
  uint16_t  gpuclk_ss_type;
  uint16_t  dpphy_override;  // bit vector, enum of atom_sysinfo_dpphy_override_def
  uint8_t memorytype;       // enum of atom_dmi_t17_mem_type_def, APU memory type indication.
  uint8_t umachannelnumber; // number of memory channels
  uint8_t htc_hyst_limit;
  uint8_t htc_tmp_limit;
  uint8_t reserved1; // dp_ss_control
  uint8_t gpu_package_id;
  struct  edp_info_table  edp1_info;
  struct  edp_info_table  edp2_info;
  uint32_t  reserved2[8];
  struct  atom_external_display_connection_info extdispconninfo;
  uint8_t UMACarveoutVersion;
  uint8_t UMACarveoutIndexMax;
  uint8_t UMACarveoutTypeDefault;
  uint8_t UMACarveoutIndexDefault;
  uint8_t UMACarveoutType;           //Auto or Custom
  uint8_t UMACarveoutIndex;
  struct  uma_carveout_option UMASizeControlOption[20];
  uint8_t reserved3[110];
};

// system_config
enum atom_system_vbiosmisc_def{
  INTEGRATED_SYSTEM_INFO__GET_EDID_CALLBACK_FUNC_SUPPORT = 0x01,
};


// gpucapinfo
enum atom_system_gpucapinf_def{
  SYS_INFO_GPUCAPS__ENABEL_DFS_BYPASS  = 0x10,
};

//dpphy_override
enum atom_sysinfo_dpphy_override_def{
  ATOM_ENABLE_DVI_TUNINGSET   = 0x01,
  ATOM_ENABLE_HDMI_TUNINGSET  = 0x02,
  ATOM_ENABLE_HDMI6G_TUNINGSET  = 0x04,
  ATOM_ENABLE_DP_TUNINGSET  = 0x08,
  ATOM_ENABLE_DP_HBR3_TUNINGSET  = 0x10,  
};

//lvds_misc
enum atom_sys_info_lvds_misc_def
{
  SYS_INFO_LVDS_MISC_888_FPDI_MODE                 =0x01,
  SYS_INFO_LVDS_MISC_888_BPC_MODE                  =0x04,
  SYS_INFO_LVDS_MISC_OVERRIDE_EN                   =0x08,
};


//memorytype  DMI Type 17 offset 12h - Memory Type
enum atom_dmi_t17_mem_type_def{
  OtherMemType = 0x01,                                  ///< Assign 01 to Other
  UnknownMemType,                                       ///< Assign 02 to Unknown
  DramMemType,                                          ///< Assign 03 to DRAM
  EdramMemType,                                         ///< Assign 04 to EDRAM
  VramMemType,                                          ///< Assign 05 to VRAM
  SramMemType,                                          ///< Assign 06 to SRAM
  RamMemType,                                           ///< Assign 07 to RAM
  RomMemType,                                           ///< Assign 08 to ROM
  FlashMemType,                                         ///< Assign 09 to Flash
  EepromMemType,                                        ///< Assign 10 to EEPROM
  FepromMemType,                                        ///< Assign 11 to FEPROM
  EpromMemType,                                         ///< Assign 12 to EPROM
  CdramMemType,                                         ///< Assign 13 to CDRAM
  ThreeDramMemType,                                     ///< Assign 14 to 3DRAM
  SdramMemType,                                         ///< Assign 15 to SDRAM
  SgramMemType,                                         ///< Assign 16 to SGRAM
  RdramMemType,                                         ///< Assign 17 to RDRAM
  DdrMemType,                                           ///< Assign 18 to DDR
  Ddr2MemType,                                          ///< Assign 19 to DDR2
  Ddr2FbdimmMemType,                                    ///< Assign 20 to DDR2 FB-DIMM
  Ddr3MemType = 0x18,                                   ///< Assign 24 to DDR3
  Fbd2MemType,                                          ///< Assign 25 to FBD2
  Ddr4MemType,                                          ///< Assign 26 to DDR4
  LpDdrMemType,                                         ///< Assign 27 to LPDDR
  LpDdr2MemType,                                        ///< Assign 28 to LPDDR2
  LpDdr3MemType,                                        ///< Assign 29 to LPDDR3
  LpDdr4MemType,                                        ///< Assign 30 to LPDDR4
  GDdr6MemType,                                         ///< Assign 31 to GDDR6
  HbmMemType,                                           ///< Assign 32 to HBM
  Hbm2MemType,                                          ///< Assign 33 to HBM2
  Ddr5MemType,                                          ///< Assign 34 to DDR5
  LpDdr5MemType,                                        ///< Assign 35 to LPDDR5
};


// this Table is used starting from NL/AM, used by SBIOS and pass the IntegratedSystemInfoTable/PowerPlayInfoTable/SystemCameraInfoTable 
struct atom_fusion_system_info_v4
{
  struct atom_integrated_system_info_v1_11   sysinfo;           // refer to ATOM_INTEGRATED_SYSTEM_INFO_V1_8 definition
  uint32_t   powerplayinfo[256];                                // Reserve 1024 bytes space for PowerPlayInfoTable
}; 


/* 
  ***************************************************************************
    Data Table gfx_info  structure
  ***************************************************************************
*/

struct  atom_gfx_info_v2_2
{
  struct  atom_common_table_header  table_header;
  uint8_t gfxip_min_ver;
  uint8_t gfxip_max_ver;
  uint8_t max_shader_engines;
  uint8_t max_tile_pipes;
  uint8_t max_cu_per_sh;
  uint8_t max_sh_per_se;
  uint8_t max_backends_per_se;
  uint8_t max_texture_channel_caches;
  uint32_t regaddr_cp_dma_src_addr;
  uint32_t regaddr_cp_dma_src_addr_hi;
  uint32_t regaddr_cp_dma_dst_addr;
  uint32_t regaddr_cp_dma_dst_addr_hi;
  uint32_t regaddr_cp_dma_command; 
  uint32_t regaddr_cp_status;
  uint32_t regaddr_rlc_gpu_clock_32;
  uint32_t rlc_gpu_timer_refclk; 
};

struct  atom_gfx_info_v2_3 {
  struct  atom_common_table_header  table_header;
  uint8_t gfxip_min_ver;
  uint8_t gfxip_max_ver;
  uint8_t max_shader_engines;
  uint8_t max_tile_pipes;
  uint8_t max_cu_per_sh;
  uint8_t max_sh_per_se;
  uint8_t max_backends_per_se;
  uint8_t max_texture_channel_caches;
  uint32_t regaddr_cp_dma_src_addr;
  uint32_t regaddr_cp_dma_src_addr_hi;
  uint32_t regaddr_cp_dma_dst_addr;
  uint32_t regaddr_cp_dma_dst_addr_hi;
  uint32_t regaddr_cp_dma_command;
  uint32_t regaddr_cp_status;
  uint32_t regaddr_rlc_gpu_clock_32;
  uint32_t rlc_gpu_timer_refclk;
  uint8_t active_cu_per_sh;
  uint8_t active_rb_per_se;
  uint16_t gcgoldenoffset;
  uint32_t rm21_sram_vmin_value;
};

struct  atom_gfx_info_v2_4
{
  struct  atom_common_table_header  table_header;
  uint8_t gfxip_min_ver;
  uint8_t gfxip_max_ver;
  uint8_t max_shader_engines;
  uint8_t reserved;
  uint8_t max_cu_per_sh;
  uint8_t max_sh_per_se;
  uint8_t max_backends_per_se;
  uint8_t max_texture_channel_caches;
  uint32_t regaddr_cp_dma_src_addr;
  uint32_t regaddr_cp_dma_src_addr_hi;
  uint32_t regaddr_cp_dma_dst_addr;
  uint32_t regaddr_cp_dma_dst_addr_hi;
  uint32_t regaddr_cp_dma_command;
  uint32_t regaddr_cp_status;
  uint32_t regaddr_rlc_gpu_clock_32;
  uint32_t rlc_gpu_timer_refclk;
  uint8_t active_cu_per_sh;
  uint8_t active_rb_per_se;
  uint16_t gcgoldenoffset;
  uint16_t gc_num_gprs;
  uint16_t gc_gsprim_buff_depth;
  uint16_t gc_parameter_cache_depth;
  uint16_t gc_wave_size;
  uint16_t gc_max_waves_per_simd;
  uint16_t gc_lds_size;
  uint8_t gc_num_max_gs_thds;
  uint8_t gc_gs_table_depth;
  uint8_t gc_double_offchip_lds_buffer;
  uint8_t gc_max_scratch_slots_per_cu;
  uint32_t sram_rm_fuses_val;
  uint32_t sram_custom_rm_fuses_val;
};

struct atom_gfx_info_v2_7 {
	struct atom_common_table_header table_header;
	uint8_t gfxip_min_ver;
	uint8_t gfxip_max_ver;
	uint8_t max_shader_engines;
	uint8_t reserved;
	uint8_t max_cu_per_sh;
	uint8_t max_sh_per_se;
	uint8_t max_backends_per_se;
	uint8_t max_texture_channel_caches;
	uint32_t regaddr_cp_dma_src_addr;
	uint32_t regaddr_cp_dma_src_addr_hi;
	uint32_t regaddr_cp_dma_dst_addr;
	uint32_t regaddr_cp_dma_dst_addr_hi;
	uint32_t regaddr_cp_dma_command;
	uint32_t regaddr_cp_status;
	uint32_t regaddr_rlc_gpu_clock_32;
	uint32_t rlc_gpu_timer_refclk;
	uint8_t active_cu_per_sh;
	uint8_t active_rb_per_se;
	uint16_t gcgoldenoffset;
	uint16_t gc_num_gprs;
	uint16_t gc_gsprim_buff_depth;
	uint16_t gc_parameter_cache_depth;
	uint16_t gc_wave_size;
	uint16_t gc_max_waves_per_simd;
	uint16_t gc_lds_size;
	uint8_t gc_num_max_gs_thds;
	uint8_t gc_gs_table_depth;
	uint8_t gc_double_offchip_lds_buffer;
	uint8_t gc_max_scratch_slots_per_cu;
	uint32_t sram_rm_fuses_val;
	uint32_t sram_custom_rm_fuses_val;
	uint8_t cut_cu;
	uint8_t active_cu_total;
	uint8_t cu_reserved[2];
	uint32_t gc_config;
	uint8_t inactive_cu_per_se[8];
	uint32_t reserved2[6];
};

struct atom_gfx_info_v3_0 {
	struct atom_common_table_header table_header;
	uint8_t gfxip_min_ver;
	uint8_t gfxip_max_ver;
	uint8_t max_shader_engines;
	uint8_t max_tile_pipes;
	uint8_t max_cu_per_sh;
	uint8_t max_sh_per_se;
	uint8_t max_backends_per_se;
	uint8_t max_texture_channel_caches;
	uint32_t regaddr_lsdma_queue0_rb_rptr;
	uint32_t regaddr_lsdma_queue0_rb_rptr_hi;
	uint32_t regaddr_lsdma_queue0_rb_wptr;
	uint32_t regaddr_lsdma_queue0_rb_wptr_hi;
	uint32_t regaddr_lsdma_command;
	uint32_t regaddr_lsdma_status;
	uint32_t regaddr_golden_tsc_count_lower;
	uint32_t golden_tsc_count_lower_refclk;
	uint8_t active_wgp_per_se;
	uint8_t active_rb_per_se;
	uint8_t active_se;
	uint8_t reserved1;
	uint32_t sram_rm_fuses_val;
	uint32_t sram_custom_rm_fuses_val;
	uint32_t inactive_sa_mask;
	uint32_t gc_config;
	uint8_t inactive_wgp[16];
	uint8_t inactive_rb[16];
	uint32_t gdfll_as_wait_ctrl_val;
	uint32_t gdfll_as_step_ctrl_val;
	uint32_t reserved[8];
};

/* 
  ***************************************************************************
    Data Table smu_info  structure
  ***************************************************************************
*/
struct atom_smu_info_v3_1
{
  struct  atom_common_table_header  table_header;
  uint8_t smuip_min_ver;
  uint8_t smuip_max_ver;
  uint8_t smu_rsd1;
  uint8_t gpuclk_ss_mode;           // enum of atom_spread_spectrum_mode
  uint16_t sclk_ss_percentage;
  uint16_t sclk_ss_rate_10hz;
  uint16_t gpuclk_ss_percentage;    // in unit of 0.001%
  uint16_t gpuclk_ss_rate_10hz;
  uint32_t core_refclk_10khz;
  uint8_t  ac_dc_gpio_bit;          // GPIO bit shift in SMU_GPIOPAD_A  configured for AC/DC switching, =0xff means invalid
  uint8_t  ac_dc_polarity;          // GPIO polarity for AC/DC switching
  uint8_t  vr0hot_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A  configured for VR0 HOT event, =0xff means invalid
  uint8_t  vr0hot_polarity;         // GPIO polarity for VR0 HOT event
  uint8_t  vr1hot_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A configured for VR1 HOT event , =0xff means invalid
  uint8_t  vr1hot_polarity;         // GPIO polarity for VR1 HOT event 
  uint8_t  fw_ctf_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A configured for CTF, =0xff means invalid
  uint8_t  fw_ctf_polarity;         // GPIO polarity for CTF
};

struct atom_smu_info_v3_2 {
  struct   atom_common_table_header  table_header;
  uint8_t  smuip_min_ver;
  uint8_t  smuip_max_ver;
  uint8_t  smu_rsd1;
  uint8_t  gpuclk_ss_mode;
  uint16_t sclk_ss_percentage;
  uint16_t sclk_ss_rate_10hz;
  uint16_t gpuclk_ss_percentage;    // in unit of 0.001%
  uint16_t gpuclk_ss_rate_10hz;
  uint32_t core_refclk_10khz;
  uint8_t  ac_dc_gpio_bit;          // GPIO bit shift in SMU_GPIOPAD_A  configured for AC/DC switching, =0xff means invalid
  uint8_t  ac_dc_polarity;          // GPIO polarity for AC/DC switching
  uint8_t  vr0hot_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A  configured for VR0 HOT event, =0xff means invalid
  uint8_t  vr0hot_polarity;         // GPIO polarity for VR0 HOT event
  uint8_t  vr1hot_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A configured for VR1 HOT event , =0xff means invalid
  uint8_t  vr1hot_polarity;         // GPIO polarity for VR1 HOT event
  uint8_t  fw_ctf_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A configured for CTF, =0xff means invalid
  uint8_t  fw_ctf_polarity;         // GPIO polarity for CTF
  uint8_t  pcc_gpio_bit;            // GPIO bit shift in SMU_GPIOPAD_A configured for PCC, =0xff means invalid
  uint8_t  pcc_gpio_polarity;       // GPIO polarity for CTF
  uint16_t smugoldenoffset;
  uint32_t gpupll_vco_freq_10khz;
  uint32_t bootup_smnclk_10khz;
  uint32_t bootup_socclk_10khz;
  uint32_t bootup_mp0clk_10khz;
  uint32_t bootup_mp1clk_10khz;
  uint32_t bootup_lclk_10khz;
  uint32_t bootup_dcefclk_10khz;
  uint32_t ctf_threshold_override_value;
  uint32_t reserved[5];
};

struct atom_smu_info_v3_3 {
  struct   atom_common_table_header  table_header;
  uint8_t  smuip_min_ver;
  uint8_t  smuip_max_ver;
  uint8_t  waflclk_ss_mode;
  uint8_t  gpuclk_ss_mode;
  uint16_t sclk_ss_percentage;
  uint16_t sclk_ss_rate_10hz;
  uint16_t gpuclk_ss_percentage;    // in unit of 0.001%
  uint16_t gpuclk_ss_rate_10hz;
  uint32_t core_refclk_10khz;
  uint8_t  ac_dc_gpio_bit;          // GPIO bit shift in SMU_GPIOPAD_A  configured for AC/DC switching, =0xff means invalid
  uint8_t  ac_dc_polarity;          // GPIO polarity for AC/DC switching
  uint8_t  vr0hot_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A  configured for VR0 HOT event, =0xff means invalid
  uint8_t  vr0hot_polarity;         // GPIO polarity for VR0 HOT event
  uint8_t  vr1hot_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A configured for VR1 HOT event , =0xff means invalid
  uint8_t  vr1hot_polarity;         // GPIO polarity for VR1 HOT event
  uint8_t  fw_ctf_gpio_bit;         // GPIO bit shift in SMU_GPIOPAD_A configured for CTF, =0xff means invalid
  uint8_t  fw_ctf_polarity;         // GPIO polarity for CTF
  uint8_t  pcc_gpio_bit;            // GPIO bit shift in SMU_GPIOPAD_A configured for PCC, =0xff means invalid
  uint8_t  pcc_gpio_polarity;       // GPIO polarity for CTF
  uint16_t smugoldenoffset;
  uint32_t gpupll_vco_freq_10khz;
  uint32_t bootup_smnclk_10khz;
  uint32_t bootup_socclk_10khz;
  uint32_t bootup_mp0clk_10khz;
  uint32_t bootup_mp1clk_10khz;
  uint32_t bootup_lclk_10khz;
  uint32_t bootup_dcefclk_10khz;
  uint32_t ctf_threshold_override_value;
  uint32_t syspll3_0_vco_freq_10khz;
  uint32_t syspll3_1_vco_freq_10khz;
  uint32_t bootup_fclk_10khz;
  uint32_t bootup_waflclk_10khz;
  uint32_t smu_info_caps;
  uint16_t waflclk_ss_percentage;    // in unit of 0.001%
  uint16_t smuinitoffset;
  uint32_t reserved;
};

struct atom_smu_info_v3_5
{
  struct   atom_common_table_header  table_header;
  uint8_t  smuip_min_ver;
  uint8_t  smuip_max_ver;
  uint8_t  waflclk_ss_mode;
  uint8_t  gpuclk_ss_mode;
  uint16_t sclk_ss_percentage;
  uint16_t sclk_ss_rate_10hz;
  uint16_t gpuclk_ss_percentage;    // in unit of 0.001%
  uint16_t gpuclk_ss_rate_10hz;
  uint32_t core_refclk_10khz;
  uint32_t syspll0_1_vco_freq_10khz;
  uint32_t syspll0_2_vco_freq_10khz;
  uint8_t  pcc_gpio_bit;            // GPIO bit shift in SMU_GPIOPAD_A configured for PCC, =0xff means invalid
  uint8_t  pcc_gpio_polarity;       // GPIO polarity for CTF
  uint16_t smugoldenoffset;
  uint32_t syspll0_0_vco_freq_10khz;
  uint32_t bootup_smnclk_10khz;
  uint32_t bootup_socclk_10khz;
  uint32_t bootup_mp0clk_10khz;
  uint32_t bootup_mp1clk_10khz;
  uint32_t bootup_lclk_10khz;
  uint32_t bootup_dcefclk_10khz;
  uint32_t ctf_threshold_override_value;
  uint32_t syspll3_0_vco_freq_10khz;
  uint32_t syspll3_1_vco_freq_10khz;
  uint32_t bootup_fclk_10khz;
  uint32_t bootup_waflclk_10khz;
  uint32_t smu_info_caps;
  uint16_t waflclk_ss_percentage;    // in unit of 0.001%
  uint16_t smuinitoffset;
  uint32_t bootup_dprefclk_10khz;
  uint32_t bootup_usbclk_10khz;
  uint32_t smb_slave_address;
  uint32_t cg_fdo_ctrl0_val;
  uint32_t cg_fdo_ctrl1_val;
  uint32_t cg_fdo_ctrl2_val;
  uint32_t gdfll_as_wait_ctrl_val;
  uint32_t gdfll_as_step_ctrl_val;
  uint32_t bootup_dtbclk_10khz;
  uint32_t fclk_syspll_refclk_10khz;
  uint32_t smusvi_svc0_val;
  uint32_t smusvi_svc1_val;
  uint32_t smusvi_svd0_val;
  uint32_t smusvi_svd1_val;
  uint32_t smusvi_svt0_val;
  uint32_t smusvi_svt1_val;
  uint32_t cg_tach_ctrl_val;
  uint32_t cg_pump_ctrl1_val;
  uint32_t cg_pump_tach_ctrl_val;
  uint32_t thm_ctf_delay_val;
  uint32_t thm_thermal_int_ctrl_val;
  uint32_t thm_tmon_config_val;
  uint32_t reserved[16];
};

struct atom_smu_info_v3_6
{
	struct   atom_common_table_header  table_header;
	uint8_t  smuip_min_ver;
	uint8_t  smuip_max_ver;
	uint8_t  waflclk_ss_mode;
	uint8_t  gpuclk_ss_mode;
	uint16_t sclk_ss_percentage;
	uint16_t sclk_ss_rate_10hz;
	uint16_t gpuclk_ss_percentage;
	uint16_t gpuclk_ss_rate_10hz;
	uint32_t core_refclk_10khz;
	uint32_t syspll0_1_vco_freq_10khz;
	uint32_t syspll0_2_vco_freq_10khz;
	uint8_t  pcc_gpio_bit;
	uint8_t  pcc_gpio_polarity;
	uint16_t smugoldenoffset;
	uint32_t syspll0_0_vco_freq_10khz;
	uint32_t bootup_smnclk_10khz;
	uint32_t bootup_socclk_10khz;
	uint32_t bootup_mp0clk_10khz;
	uint32_t bootup_mp1clk_10khz;
	uint32_t bootup_lclk_10khz;
	uint32_t bootup_dxioclk_10khz;
	uint32_t ctf_threshold_override_value;
	uint32_t syspll3_0_vco_freq_10khz;
	uint32_t syspll3_1_vco_freq_10khz;
	uint32_t bootup_fclk_10khz;
	uint32_t bootup_waflclk_10khz;
	uint32_t smu_info_caps;
	uint16_t waflclk_ss_percentage;
	uint16_t smuinitoffset;
	uint32_t bootup_gfxavsclk_10khz;
	uint32_t bootup_mpioclk_10khz;
	uint32_t smb_slave_address;
	uint32_t cg_fdo_ctrl0_val;
	uint32_t cg_fdo_ctrl1_val;
	uint32_t cg_fdo_ctrl2_val;
	uint32_t gdfll_as_wait_ctrl_val;
	uint32_t gdfll_as_step_ctrl_val;
	uint32_t reserved_clk;
	uint32_t fclk_syspll_refclk_10khz;
	uint32_t smusvi_svc0_val;
	uint32_t smusvi_svc1_val;
	uint32_t smusvi_svd0_val;
	uint32_t smusvi_svd1_val;
	uint32_t smusvi_svt0_val;
	uint32_t smusvi_svt1_val;
	uint32_t cg_tach_ctrl_val;
	uint32_t cg_pump_ctrl1_val;
	uint32_t cg_pump_tach_ctrl_val;
	uint32_t thm_ctf_delay_val;
	uint32_t thm_thermal_int_ctrl_val;
	uint32_t thm_tmon_config_val;
	uint32_t bootup_vclk_10khz;
	uint32_t bootup_dclk_10khz;
	uint32_t smu_gpiopad_pu_en_val;
	uint32_t smu_gpiopad_pd_en_val;
	uint32_t reserved[12];
};

struct atom_smu_info_v4_0 {
	struct atom_common_table_header table_header;
	uint32_t bootup_gfxclk_bypass_10khz;
	uint32_t bootup_usrclk_10khz;
	uint32_t bootup_csrclk_10khz;
	uint32_t core_refclk_10khz;
	uint32_t syspll1_vco_freq_10khz;
	uint32_t syspll2_vco_freq_10khz;
	uint8_t pcc_gpio_bit;
	uint8_t pcc_gpio_polarity;
	uint16_t bootup_vddusr_mv;
	uint32_t syspll0_vco_freq_10khz;
	uint32_t bootup_smnclk_10khz;
	uint32_t bootup_socclk_10khz;
	uint32_t bootup_mp0clk_10khz;
	uint32_t bootup_mp1clk_10khz;
	uint32_t bootup_lclk_10khz;
	uint32_t bootup_dcefclk_10khz;
	uint32_t ctf_threshold_override_value;
	uint32_t syspll3_vco_freq_10khz;
	uint32_t mm_syspll_vco_freq_10khz;
	uint32_t bootup_fclk_10khz;
	uint32_t bootup_waflclk_10khz;
	uint32_t smu_info_caps;
	uint16_t waflclk_ss_percentage;
	uint16_t smuinitoffset;
	uint32_t bootup_dprefclk_10khz;
	uint32_t bootup_usbclk_10khz;
	uint32_t smb_slave_address;
	uint32_t cg_fdo_ctrl0_val;
	uint32_t cg_fdo_ctrl1_val;
	uint32_t cg_fdo_ctrl2_val;
	uint32_t gdfll_as_wait_ctrl_val;
	uint32_t gdfll_as_step_ctrl_val;
	uint32_t bootup_dtbclk_10khz;
	uint32_t fclk_syspll_refclk_10khz;
	uint32_t smusvi_svc0_val;
	uint32_t smusvi_svc1_val;
	uint32_t smusvi_svd0_val;
	uint32_t smusvi_svd1_val;
	uint32_t smusvi_svt0_val;
	uint32_t smusvi_svt1_val;
	uint32_t cg_tach_ctrl_val;
	uint32_t cg_pump_ctrl1_val;
	uint32_t cg_pump_tach_ctrl_val;
	uint32_t thm_ctf_delay_val;
	uint32_t thm_thermal_int_ctrl_val;
	uint32_t thm_tmon_config_val;
	uint32_t smbus_timing_cntrl0_val;
	uint32_t smbus_timing_cntrl1_val;
	uint32_t smbus_timing_cntrl2_val;
	uint32_t pwr_disp_timer_global_control_val;
	uint32_t bootup_mpioclk_10khz;
	uint32_t bootup_dclk0_10khz;
	uint32_t bootup_vclk0_10khz;
	uint32_t bootup_dclk1_10khz;
	uint32_t bootup_vclk1_10khz;
	uint32_t bootup_baco400clk_10khz;
	uint32_t bootup_baco1200clk_bypass_10khz;
	uint32_t bootup_baco700clk_bypass_10khz;
	uint32_t reserved[16];
};

/*
 ***************************************************************************
   Data Table smc_dpm_info  structure
 ***************************************************************************
 */
struct atom_smc_dpm_info_v4_1
{
  struct   atom_common_table_header  table_header;
  uint8_t  liquid1_i2c_address;
  uint8_t  liquid2_i2c_address;
  uint8_t  vr_i2c_address;
  uint8_t  plx_i2c_address;

  uint8_t  liquid_i2c_linescl;
  uint8_t  liquid_i2c_linesda;
  uint8_t  vr_i2c_linescl;
  uint8_t  vr_i2c_linesda;

  uint8_t  plx_i2c_linescl;
  uint8_t  plx_i2c_linesda;
  uint8_t  vrsensorpresent;
  uint8_t  liquidsensorpresent;

  uint16_t maxvoltagestepgfx;
  uint16_t maxvoltagestepsoc;

  uint8_t  vddgfxvrmapping;
  uint8_t  vddsocvrmapping;
  uint8_t  vddmem0vrmapping;
  uint8_t  vddmem1vrmapping;

  uint8_t  gfxulvphasesheddingmask;
  uint8_t  soculvphasesheddingmask;
  uint8_t  padding8_v[2];

  uint16_t gfxmaxcurrent;
  uint8_t  gfxoffset;
  uint8_t  padding_telemetrygfx;

  uint16_t socmaxcurrent;
  uint8_t  socoffset;
  uint8_t  padding_telemetrysoc;

  uint16_t mem0maxcurrent;
  uint8_t  mem0offset;
  uint8_t  padding_telemetrymem0;

  uint16_t mem1maxcurrent;
  uint8_t  mem1offset;
  uint8_t  padding_telemetrymem1;

  uint8_t  acdcgpio;
  uint8_t  acdcpolarity;
  uint8_t  vr0hotgpio;
  uint8_t  vr0hotpolarity;

  uint8_t  vr1hotgpio;
  uint8_t  vr1hotpolarity;
  uint8_t  padding1;
  uint8_t  padding2;

  uint8_t  ledpin0;
  uint8_t  ledpin1;
  uint8_t  ledpin2;
  uint8_t  padding8_4;

	uint8_t  pllgfxclkspreadenabled;
	uint8_t  pllgfxclkspreadpercent;
	uint16_t pllgfxclkspreadfreq;

  uint8_t uclkspreadenabled;
  uint8_t uclkspreadpercent;
  uint16_t uclkspreadfreq;

  uint8_t socclkspreadenabled;
  uint8_t socclkspreadpercent;
  uint16_t socclkspreadfreq;

	uint8_t  acggfxclkspreadenabled;
	uint8_t  acggfxclkspreadpercent;
	uint16_t acggfxclkspreadfreq;

	uint8_t Vr2_I2C_address;
	uint8_t padding_vr2[3];

	uint32_t boardreserved[9];
};

/*
 ***************************************************************************
   Data Table smc_dpm_info  structure
 ***************************************************************************
 */
struct atom_smc_dpm_info_v4_3
{
  struct   atom_common_table_header  table_header;
  uint8_t  liquid1_i2c_address;
  uint8_t  liquid2_i2c_address;
  uint8_t  vr_i2c_address;
  uint8_t  plx_i2c_address;

  uint8_t  liquid_i2c_linescl;
  uint8_t  liquid_i2c_linesda;
  uint8_t  vr_i2c_linescl;
  uint8_t  vr_i2c_linesda;

  uint8_t  plx_i2c_linescl;
  uint8_t  plx_i2c_linesda;
  uint8_t  vrsensorpresent;
  uint8_t  liquidsensorpresent;

  uint16_t maxvoltagestepgfx;
  uint16_t maxvoltagestepsoc;

  uint8_t  vddgfxvrmapping;
  uint8_t  vddsocvrmapping;
  uint8_t  vddmem0vrmapping;
  uint8_t  vddmem1vrmapping;

  uint8_t  gfxulvphasesheddingmask;
  uint8_t  soculvphasesheddingmask;
  uint8_t  externalsensorpresent;
  uint8_t  padding8_v;

  uint16_t gfxmaxcurrent;
  uint8_t  gfxoffset;
  uint8_t  padding_telemetrygfx;

  uint16_t socmaxcurrent;
  uint8_t  socoffset;
  uint8_t  padding_telemetrysoc;

  uint16_t mem0maxcurrent;
  uint8_t  mem0offset;
  uint8_t  padding_telemetrymem0;

  uint16_t mem1maxcurrent;
  uint8_t  mem1offset;
  uint8_t  padding_telemetrymem1;

  uint8_t  acdcgpio;
  uint8_t  acdcpolarity;
  uint8_t  vr0hotgpio;
  uint8_t  vr0hotpolarity;

  uint8_t  vr1hotgpio;
  uint8_t  vr1hotpolarity;
  uint8_t  padding1;
  uint8_t  padding2;

  uint8_t  ledpin0;
  uint8_t  ledpin1;
  uint8_t  ledpin2;
  uint8_t  padding8_4;

  uint8_t  pllgfxclkspreadenabled;
  uint8_t  pllgfxclkspreadpercent;
  uint16_t pllgfxclkspreadfreq;

  uint8_t uclkspreadenabled;
  uint8_t uclkspreadpercent;
  uint16_t uclkspreadfreq;

  uint8_t fclkspreadenabled;
  uint8_t fclkspreadpercent;
  uint16_t fclkspreadfreq;

  uint8_t fllgfxclkspreadenabled;
  uint8_t fllgfxclkspreadpercent;
  uint16_t fllgfxclkspreadfreq;

  uint32_t boardreserved[10];
};

struct smudpm_i2ccontrollerconfig_t {
  uint32_t  enabled;
  uint32_t  slaveaddress;
  uint32_t  controllerport;
  uint32_t  controllername;
  uint32_t  thermalthrottler;
  uint32_t  i2cprotocol;
  uint32_t  i2cspeed;
};

struct atom_smc_dpm_info_v4_4
{
  struct   atom_common_table_header  table_header;
  uint32_t  i2c_padding[3];

  uint16_t maxvoltagestepgfx;
  uint16_t maxvoltagestepsoc;

  uint8_t  vddgfxvrmapping;
  uint8_t  vddsocvrmapping;
  uint8_t  vddmem0vrmapping;
  uint8_t  vddmem1vrmapping;

  uint8_t  gfxulvphasesheddingmask;
  uint8_t  soculvphasesheddingmask;
  uint8_t  externalsensorpresent;
  uint8_t  padding8_v;

  uint16_t gfxmaxcurrent;
  uint8_t  gfxoffset;
  uint8_t  padding_telemetrygfx;

  uint16_t socmaxcurrent;
  uint8_t  socoffset;
  uint8_t  padding_telemetrysoc;

  uint16_t mem0maxcurrent;
  uint8_t  mem0offset;
  uint8_t  padding_telemetrymem0;

  uint16_t mem1maxcurrent;
  uint8_t  mem1offset;
  uint8_t  padding_telemetrymem1;


  uint8_t  acdcgpio;
  uint8_t  acdcpolarity;
  uint8_t  vr0hotgpio;
  uint8_t  vr0hotpolarity;

  uint8_t  vr1hotgpio;
  uint8_t  vr1hotpolarity;
  uint8_t  padding1;
  uint8_t  padding2;


  uint8_t  ledpin0;
  uint8_t  ledpin1;
  uint8_t  ledpin2;
  uint8_t  padding8_4;


  uint8_t  pllgfxclkspreadenabled;
  uint8_t  pllgfxclkspreadpercent;
  uint16_t pllgfxclkspreadfreq;


  uint8_t  uclkspreadenabled;
  uint8_t  uclkspreadpercent;
  uint16_t uclkspreadfreq;


  uint8_t  fclkspreadenabled;
  uint8_t  fclkspreadpercent;
  uint16_t fclkspreadfreq;


  uint8_t  fllgfxclkspreadenabled;
  uint8_t  fllgfxclkspreadpercent;
  uint16_t fllgfxclkspreadfreq;


  struct smudpm_i2ccontrollerconfig_t  i2ccontrollers[7];


  uint32_t boardreserved[10];
};

enum smudpm_v4_5_i2ccontrollername_e{
    SMC_V4_5_I2C_CONTROLLER_NAME_VR_GFX = 0,
    SMC_V4_5_I2C_CONTROLLER_NAME_VR_SOC,
    SMC_V4_5_I2C_CONTROLLER_NAME_VR_VDDCI,
    SMC_V4_5_I2C_CONTROLLER_NAME_VR_MVDD,
    SMC_V4_5_I2C_CONTROLLER_NAME_LIQUID0,
    SMC_V4_5_I2C_CONTROLLER_NAME_LIQUID1,
    SMC_V4_5_I2C_CONTROLLER_NAME_PLX,
    SMC_V4_5_I2C_CONTROLLER_NAME_SPARE,
    SMC_V4_5_I2C_CONTROLLER_NAME_COUNT,
};

enum smudpm_v4_5_i2ccontrollerthrottler_e{
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_TYPE_NONE = 0,
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_VR_GFX,
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_VR_SOC,
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_VR_VDDCI,
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_VR_MVDD,
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_LIQUID0,
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_LIQUID1,
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_PLX,
    SMC_V4_5_I2C_CONTROLLER_THROTTLER_COUNT,
};

enum smudpm_v4_5_i2ccontrollerprotocol_e{
    SMC_V4_5_I2C_CONTROLLER_PROTOCOL_VR_0,
    SMC_V4_5_I2C_CONTROLLER_PROTOCOL_VR_1,
    SMC_V4_5_I2C_CONTROLLER_PROTOCOL_TMP_0,
    SMC_V4_5_I2C_CONTROLLER_PROTOCOL_TMP_1,
    SMC_V4_5_I2C_CONTROLLER_PROTOCOL_SPARE_0,
    SMC_V4_5_I2C_CONTROLLER_PROTOCOL_SPARE_1,
    SMC_V4_5_I2C_CONTROLLER_PROTOCOL_COUNT,
};

struct smudpm_i2c_controller_config_v2
{
    uint8_t   Enabled;
    uint8_t   Speed;
    uint8_t   Padding[2];
    uint32_t  SlaveAddress;
    uint8_t   ControllerPort;
    uint8_t   ControllerName;
    uint8_t   ThermalThrotter;
    uint8_t   I2cProtocol;
};

struct atom_smc_dpm_info_v4_5
{
  struct   atom_common_table_header  table_header;
    // SECTION: BOARD PARAMETERS
    // I2C Control
  struct smudpm_i2c_controller_config_v2  I2cControllers[8];

  // SVI2 Board Parameters
  uint16_t     MaxVoltageStepGfx; // In mV(Q2) Max voltage step that SMU will request. Multiple steps are taken if voltage change exceeds this value.
  uint16_t     MaxVoltageStepSoc; // In mV(Q2) Max voltage step that SMU will request. Multiple steps are taken if voltage change exceeds this value.

  uint8_t      VddGfxVrMapping;   // Use VR_MAPPING* bitfields
  uint8_t      VddSocVrMapping;   // Use VR_MAPPING* bitfields
  uint8_t      VddMem0VrMapping;  // Use VR_MAPPING* bitfields
  uint8_t      VddMem1VrMapping;  // Use VR_MAPPING* bitfields

  uint8_t      GfxUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      SocUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      ExternalSensorPresent; // External RDI connected to TMON (aka TEMP IN)
  uint8_t      Padding8_V;

  // Telemetry Settings
  uint16_t     GfxMaxCurrent;   // in Amps
  uint8_t      GfxOffset;       // in Amps
  uint8_t      Padding_TelemetryGfx;
  uint16_t     SocMaxCurrent;   // in Amps
  uint8_t      SocOffset;       // in Amps
  uint8_t      Padding_TelemetrySoc;

  uint16_t     Mem0MaxCurrent;   // in Amps
  uint8_t      Mem0Offset;       // in Amps
  uint8_t      Padding_TelemetryMem0;

  uint16_t     Mem1MaxCurrent;   // in Amps
  uint8_t      Mem1Offset;       // in Amps
  uint8_t      Padding_TelemetryMem1;

  // GPIO Settings
  uint8_t      AcDcGpio;        // GPIO pin configured for AC/DC switching
  uint8_t      AcDcPolarity;    // GPIO polarity for AC/DC switching
  uint8_t      VR0HotGpio;      // GPIO pin configured for VR0 HOT event
  uint8_t      VR0HotPolarity;  // GPIO polarity for VR0 HOT event

  uint8_t      VR1HotGpio;      // GPIO pin configured for VR1 HOT event 
  uint8_t      VR1HotPolarity;  // GPIO polarity for VR1 HOT event 
  uint8_t      GthrGpio;        // GPIO pin configured for GTHR Event
  uint8_t      GthrPolarity;    // replace GPIO polarity for GTHR

  // LED Display Settings
  uint8_t      LedPin0;         // GPIO number for LedPin[0]
  uint8_t      LedPin1;         // GPIO number for LedPin[1]
  uint8_t      LedPin2;         // GPIO number for LedPin[2]
  uint8_t      padding8_4;

  // GFXCLK PLL Spread Spectrum
  uint8_t      PllGfxclkSpreadEnabled;   // on or off
  uint8_t      PllGfxclkSpreadPercent;   // Q4.4
  uint16_t     PllGfxclkSpreadFreq;      // kHz

  // GFXCLK DFLL Spread Spectrum
  uint8_t      DfllGfxclkSpreadEnabled;   // on or off
  uint8_t      DfllGfxclkSpreadPercent;   // Q4.4
  uint16_t     DfllGfxclkSpreadFreq;      // kHz

  // UCLK Spread Spectrum
  uint8_t      UclkSpreadEnabled;   // on or off
  uint8_t      UclkSpreadPercent;   // Q4.4
  uint16_t     UclkSpreadFreq;      // kHz

  // SOCCLK Spread Spectrum
  uint8_t      SoclkSpreadEnabled;   // on or off
  uint8_t      SocclkSpreadPercent;   // Q4.4
  uint16_t     SocclkSpreadFreq;      // kHz

  // Total board power
  uint16_t     TotalBoardPower;     //Only needed for TCP Estimated case, where TCP = TGP+Total Board Power
  uint16_t     BoardPadding; 

  // Mvdd Svi2 Div Ratio Setting
  uint32_t MvddRatio; // This is used for MVDD Vid workaround. It has 16 fractional bits (Q16.16)
  
  uint32_t     BoardReserved[9];

};

struct atom_smc_dpm_info_v4_6
{
  struct   atom_common_table_header  table_header;
  // section: board parameters
  uint32_t     i2c_padding[3];   // old i2c control are moved to new area

  uint16_t     maxvoltagestepgfx; // in mv(q2) max voltage step that smu will request. multiple steps are taken if voltage change exceeds this value.
  uint16_t     maxvoltagestepsoc; // in mv(q2) max voltage step that smu will request. multiple steps are taken if voltage change exceeds this value.

  uint8_t      vddgfxvrmapping;     // use vr_mapping* bitfields
  uint8_t      vddsocvrmapping;     // use vr_mapping* bitfields
  uint8_t      vddmemvrmapping;     // use vr_mapping* bitfields
  uint8_t      boardvrmapping;      // use vr_mapping* bitfields

  uint8_t      gfxulvphasesheddingmask; // set this to 1 to set psi0/1 to 1 in ulv mode
  uint8_t      externalsensorpresent; // external rdi connected to tmon (aka temp in)
  uint8_t      padding8_v[2];

  // telemetry settings
  uint16_t     gfxmaxcurrent;   // in amps
  uint8_t      gfxoffset;       // in amps
  uint8_t      padding_telemetrygfx;

  uint16_t     socmaxcurrent;   // in amps
  uint8_t      socoffset;       // in amps
  uint8_t      padding_telemetrysoc;

  uint16_t     memmaxcurrent;   // in amps
  uint8_t      memoffset;       // in amps
  uint8_t      padding_telemetrymem;

  uint16_t     boardmaxcurrent;   // in amps
  uint8_t      boardoffset;       // in amps
  uint8_t      padding_telemetryboardinput;

  // gpio settings
  uint8_t      vr0hotgpio;      // gpio pin configured for vr0 hot event
  uint8_t      vr0hotpolarity;  // gpio polarity for vr0 hot event
  uint8_t      vr1hotgpio;      // gpio pin configured for vr1 hot event
  uint8_t      vr1hotpolarity;  // gpio polarity for vr1 hot event

 // gfxclk pll spread spectrum
  uint8_t	   pllgfxclkspreadenabled;	// on or off
  uint8_t	   pllgfxclkspreadpercent;	// q4.4
  uint16_t	   pllgfxclkspreadfreq;		// khz

 // uclk spread spectrum
  uint8_t	   uclkspreadenabled;   // on or off
  uint8_t	   uclkspreadpercent;   // q4.4
  uint16_t	   uclkspreadfreq;	   // khz

 // fclk spread spectrum
  uint8_t	   fclkspreadenabled;   // on or off
  uint8_t	   fclkspreadpercent;   // q4.4
  uint16_t	   fclkspreadfreq;	   // khz


  // gfxclk fll spread spectrum
  uint8_t      fllgfxclkspreadenabled;   // on or off
  uint8_t      fllgfxclkspreadpercent;   // q4.4
  uint16_t     fllgfxclkspreadfreq;      // khz

  // i2c controller structure
  struct smudpm_i2c_controller_config_v2 i2ccontrollers[8];

  // memory section
  uint32_t	 memorychannelenabled; // for dram use only, max 32 channels enabled bit mask.

  uint8_t 	 drambitwidth; // for dram use only.  see dram bit width type defines
  uint8_t 	 paddingmem[3];

	// total board power
  uint16_t	 totalboardpower;	  //only needed for tcp estimated case, where tcp = tgp+total board power
  uint16_t	 boardpadding;

	// section: xgmi training
  uint8_t 	 xgmilinkspeed[4];
  uint8_t 	 xgmilinkwidth[4];

  uint16_t	 xgmifclkfreq[4];
  uint16_t	 xgmisocvoltage[4];

  // reserved
  uint32_t   boardreserved[10];
};

struct atom_smc_dpm_info_v4_7
{
  struct   atom_common_table_header  table_header;
    // SECTION: BOARD PARAMETERS
    // I2C Control
  struct smudpm_i2c_controller_config_v2  I2cControllers[8];

  // SVI2 Board Parameters
  uint16_t     MaxVoltageStepGfx; // In mV(Q2) Max voltage step that SMU will request. Multiple steps are taken if voltage change exceeds this value.
  uint16_t     MaxVoltageStepSoc; // In mV(Q2) Max voltage step that SMU will request. Multiple steps are taken if voltage change exceeds this value.

  uint8_t      VddGfxVrMapping;   // Use VR_MAPPING* bitfields
  uint8_t      VddSocVrMapping;   // Use VR_MAPPING* bitfields
  uint8_t      VddMem0VrMapping;  // Use VR_MAPPING* bitfields
  uint8_t      VddMem1VrMapping;  // Use VR_MAPPING* bitfields

  uint8_t      GfxUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      SocUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      ExternalSensorPresent; // External RDI connected to TMON (aka TEMP IN)
  uint8_t      Padding8_V;

  // Telemetry Settings
  uint16_t     GfxMaxCurrent;   // in Amps
  uint8_t      GfxOffset;       // in Amps
  uint8_t      Padding_TelemetryGfx;
  uint16_t     SocMaxCurrent;   // in Amps
  uint8_t      SocOffset;       // in Amps
  uint8_t      Padding_TelemetrySoc;

  uint16_t     Mem0MaxCurrent;   // in Amps
  uint8_t      Mem0Offset;       // in Amps
  uint8_t      Padding_TelemetryMem0;

  uint16_t     Mem1MaxCurrent;   // in Amps
  uint8_t      Mem1Offset;       // in Amps
  uint8_t      Padding_TelemetryMem1;

  // GPIO Settings
  uint8_t      AcDcGpio;        // GPIO pin configured for AC/DC switching
  uint8_t      AcDcPolarity;    // GPIO polarity for AC/DC switching
  uint8_t      VR0HotGpio;      // GPIO pin configured for VR0 HOT event
  uint8_t      VR0HotPolarity;  // GPIO polarity for VR0 HOT event

  uint8_t      VR1HotGpio;      // GPIO pin configured for VR1 HOT event
  uint8_t      VR1HotPolarity;  // GPIO polarity for VR1 HOT event
  uint8_t      GthrGpio;        // GPIO pin configured for GTHR Event
  uint8_t      GthrPolarity;    // replace GPIO polarity for GTHR

  // LED Display Settings
  uint8_t      LedPin0;         // GPIO number for LedPin[0]
  uint8_t      LedPin1;         // GPIO number for LedPin[1]
  uint8_t      LedPin2;         // GPIO number for LedPin[2]
  uint8_t      padding8_4;

  // GFXCLK PLL Spread Spectrum
  uint8_t      PllGfxclkSpreadEnabled;   // on or off
  uint8_t      PllGfxclkSpreadPercent;   // Q4.4
  uint16_t     PllGfxclkSpreadFreq;      // kHz

  // GFXCLK DFLL Spread Spectrum
  uint8_t      DfllGfxclkSpreadEnabled;   // on or off
  uint8_t      DfllGfxclkSpreadPercent;   // Q4.4
  uint16_t     DfllGfxclkSpreadFreq;      // kHz

  // UCLK Spread Spectrum
  uint8_t      UclkSpreadEnabled;   // on or off
  uint8_t      UclkSpreadPercent;   // Q4.4
  uint16_t     UclkSpreadFreq;      // kHz

  // SOCCLK Spread Spectrum
  uint8_t      SoclkSpreadEnabled;   // on or off
  uint8_t      SocclkSpreadPercent;   // Q4.4
  uint16_t     SocclkSpreadFreq;      // kHz

  // Total board power
  uint16_t     TotalBoardPower;     //Only needed for TCP Estimated case, where TCP = TGP+Total Board Power
  uint16_t     BoardPadding;

  // Mvdd Svi2 Div Ratio Setting
  uint32_t     MvddRatio; // This is used for MVDD Vid workaround. It has 16 fractional bits (Q16.16)

  // GPIO pins for I2C communications with 2nd controller for Input Telemetry Sequence
  uint8_t      GpioI2cScl;          // Serial Clock
  uint8_t      GpioI2cSda;          // Serial Data
  uint16_t     GpioPadding;

  // Additional LED Display Settings
  uint8_t      LedPin3;         // GPIO number for LedPin[3] - PCIE GEN Speed
  uint8_t      LedPin4;         // GPIO number for LedPin[4] - PMFW Error Status
  uint16_t     LedEnableMask;

  // Power Limit Scalars
  uint8_t      PowerLimitScalar[4];    //[PPT_THROTTLER_COUNT]

  uint8_t      MvddUlvPhaseSheddingMask;
  uint8_t      VddciUlvPhaseSheddingMask;
  uint8_t      Padding8_Psi1;
  uint8_t      Padding8_Psi2;

  uint32_t     BoardReserved[5];
};

struct smudpm_i2c_controller_config_v3
{
  uint8_t   Enabled;
  uint8_t   Speed;
  uint8_t   SlaveAddress;
  uint8_t   ControllerPort;
  uint8_t   ControllerName;
  uint8_t   ThermalThrotter;
  uint8_t   I2cProtocol;
  uint8_t   PaddingConfig;
};

struct atom_smc_dpm_info_v4_9
{
  struct   atom_common_table_header  table_header;

  //SECTION: Gaming Clocks
  //uint32_t     GamingClk[6];

  // SECTION: I2C Control
  struct smudpm_i2c_controller_config_v3  I2cControllers[16];     

  uint8_t      GpioScl;  // GPIO Number for SCL Line, used only for CKSVII2C1
  uint8_t      GpioSda;  // GPIO Number for SDA Line, used only for CKSVII2C1
  uint8_t      FchUsbPdSlaveAddr; //For requesting USB PD controller S-states via FCH I2C when entering PME turn off
  uint8_t      I2cSpare;

  // SECTION: SVI2 Board Parameters
  uint8_t      VddGfxVrMapping;   // Use VR_MAPPING* bitfields
  uint8_t      VddSocVrMapping;   // Use VR_MAPPING* bitfields
  uint8_t      VddMem0VrMapping;  // Use VR_MAPPING* bitfields
  uint8_t      VddMem1VrMapping;  // Use VR_MAPPING* bitfields

  uint8_t      GfxUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      SocUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      VddciUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode
  uint8_t      MvddUlvPhaseSheddingMask; // set this to 1 to set PSI0/1 to 1 in ULV mode

  // SECTION: Telemetry Settings
  uint16_t     GfxMaxCurrent;   // in Amps
  uint8_t      GfxOffset;       // in Amps
  uint8_t      Padding_TelemetryGfx;

  uint16_t     SocMaxCurrent;   // in Amps
  uint8_t      SocOffset;       // in Amps
  uint8_t      Padding_TelemetrySoc;

  uint16_t     Mem0MaxCurrent;   // in Amps
  uint8_t      Mem0Offset;       // in Amps
  uint8_t      Padding_TelemetryMem0;
  
  uint16_t     Mem1MaxCurrent;   // in Amps
  uint8_t      Mem1Offset;       // in Amps
  uint8_t      Padding_TelemetryMem1;

  uint32_t     MvddRatio; // This is used for MVDD  Svi2 Div Ratio workaround. It has 16 fractional bits (Q16.16)
  
  // SECTION: GPIO Settings
  uint8_t      AcDcGpio;        // GPIO pin configured for AC/DC switching
  uint8_t      AcDcPolarity;    // GPIO polarity for AC/DC switching
  uint8_t      VR0HotGpio;      // GPIO pin configured for VR0 HOT event
  uint8_t      VR0HotPolarity;  // GPIO polarity for VR0 HOT event

  uint8_t      VR1HotGpio;      // GPIO pin configured for VR1 HOT event 
  uint8_t      VR1HotPolarity;  // GPIO polarity for VR1 HOT event 
  uint8_t      GthrGpio;        // GPIO pin configured for GTHR Event
  uint8_t      GthrPolarity;    // replace GPIO polarity for GTHR

  // LED Display Settings
  uint8_t      LedPin0;         // GPIO number for LedPin[0]
  uint8_t      LedPin1;         // GPIO number for LedPin[1]
  uint8_t      LedPin2;         // GPIO number for LedPin[2]
  uint8_t      LedEnableMask;

  uint8_t      LedPcie;        // GPIO number for PCIE results
  uint8_t      LedError;       // GPIO number for Error Cases
  uint8_t      LedSpare1[2];

  // SECTION: Clock Spread Spectrum
  
  // GFXCLK PLL Spread Spectrum
  uint8_t      PllGfxclkSpreadEnabled;   // on or off
  uint8_t      PllGfxclkSpreadPercent;   // Q4.4
  uint16_t     PllGfxclkSpreadFreq;      // kHz

  // GFXCLK DFLL Spread Spectrum
  uint8_t      DfllGfxclkSpreadEnabled;   // on or off
  uint8_t      DfllGfxclkSpreadPercent;   // Q4.4
  uint16_t     DfllGfxclkSpreadFreq;      // kHz
  
  // UCLK Spread Spectrum
  uint8_t      UclkSpreadEnabled;   // on or off
  uint8_t      UclkSpreadPercent;   // Q4.4
  uint16_t     UclkSpreadFreq;      // kHz

  // FCLK Spread Spectrum
  uint8_t      FclkSpreadEnabled;   // on or off
  uint8_t      FclkSpreadPercent;   // Q4.4
  uint16_t     FclkSpreadFreq;      // kHz
  
  // Section: Memory Config
  uint32_t     MemoryChannelEnabled; // For DRAM use only, Max 32 channels enabled bit mask. 
  
  uint8_t      DramBitWidth; // For DRAM use only.  See Dram Bit width type defines
  uint8_t      PaddingMem1[3];

  // Section: Total Board Power
  uint16_t     TotalBoardPower;     //Only needed for TCP Estimated case, where TCP = TGP+Total Board Power
  uint16_t     BoardPowerPadding; 
  
  // SECTION: XGMI Training
  uint8_t      XgmiLinkSpeed   [4];
  uint8_t      XgmiLinkWidth   [4];

  uint16_t     XgmiFclkFreq    [4];
  uint16_t     XgmiSocVoltage  [4];

  // SECTION: Board Reserved

  uint32_t     BoardReserved[16];

};

struct atom_smc_dpm_info_v4_10
{
  struct   atom_common_table_header  table_header;

  // SECTION: BOARD PARAMETERS
  // Telemetry Settings
  uint16_t GfxMaxCurrent; // in Amps
  uint8_t   GfxOffset;     // in Amps
  uint8_t  Padding_TelemetryGfx;

  uint16_t SocMaxCurrent; // in Amps
  uint8_t   SocOffset;     // in Amps
  uint8_t  Padding_TelemetrySoc;

  uint16_t MemMaxCurrent; // in Amps
  uint8_t   MemOffset;     // in Amps
  uint8_t  Padding_TelemetryMem;

  uint16_t BoardMaxCurrent; // in Amps
  uint8_t   BoardOffset;     // in Amps
  uint8_t  Padding_TelemetryBoardInput;

  // Platform input telemetry voltage coefficient
  uint32_t BoardVoltageCoeffA; // decode by /1000
  uint32_t BoardVoltageCoeffB; // decode by /1000

  // GPIO Settings
  uint8_t  VR0HotGpio;     // GPIO pin configured for VR0 HOT event
  uint8_t  VR0HotPolarity; // GPIO polarity for VR0 HOT event
  uint8_t  VR1HotGpio;     // GPIO pin configured for VR1 HOT event
  uint8_t  VR1HotPolarity; // GPIO polarity for VR1 HOT event

  // UCLK Spread Spectrum
  uint8_t  UclkSpreadEnabled; // on or off
  uint8_t  UclkSpreadPercent; // Q4.4
  uint16_t UclkSpreadFreq;    // kHz

  // FCLK Spread Spectrum
  uint8_t  FclkSpreadEnabled; // on or off
  uint8_t  FclkSpreadPercent; // Q4.4
  uint16_t FclkSpreadFreq;    // kHz

  // I2C Controller Structure
  struct smudpm_i2c_controller_config_v3  I2cControllers[8];

  // GPIO pins for I2C communications with 2nd controller for Input Telemetry Sequence
  uint8_t  GpioI2cScl; // Serial Clock
  uint8_t  GpioI2cSda; // Serial Data
  uint16_t spare5;

  uint32_t reserved[16];
};

/* 
  ***************************************************************************
    Data Table asic_profiling_info  structure
  ***************************************************************************
*/
struct  atom_asic_profiling_info_v4_1
{
  struct  atom_common_table_header  table_header;
  uint32_t  maxvddc;                 
  uint32_t  minvddc;               
  uint32_t  avfs_meannsigma_acontant0;
  uint32_t  avfs_meannsigma_acontant1;
  uint32_t  avfs_meannsigma_acontant2;
  uint16_t  avfs_meannsigma_dc_tol_sigma;
  uint16_t  avfs_meannsigma_platform_mean;
  uint16_t  avfs_meannsigma_platform_sigma;
  uint32_t  gb_vdroop_table_cksoff_a0;
  uint32_t  gb_vdroop_table_cksoff_a1;
  uint32_t  gb_vdroop_table_cksoff_a2;
  uint32_t  gb_vdroop_table_ckson_a0;
  uint32_t  gb_vdroop_table_ckson_a1;
  uint32_t  gb_vdroop_table_ckson_a2;
  uint32_t  avfsgb_fuse_table_cksoff_m1;
  uint32_t  avfsgb_fuse_table_cksoff_m2;
  uint32_t  avfsgb_fuse_table_cksoff_b;
  uint32_t  avfsgb_fuse_table_ckson_m1;	
  uint32_t  avfsgb_fuse_table_ckson_m2;
  uint32_t  avfsgb_fuse_table_ckson_b;
  uint16_t  max_voltage_0_25mv;
  uint8_t   enable_gb_vdroop_table_cksoff;
  uint8_t   enable_gb_vdroop_table_ckson;
  uint8_t   enable_gb_fuse_table_cksoff;
  uint8_t   enable_gb_fuse_table_ckson;
  uint16_t  psm_age_comfactor;
  uint8_t   enable_apply_avfs_cksoff_voltage;
  uint8_t   reserved;
  uint32_t  dispclk2gfxclk_a;
  uint32_t  dispclk2gfxclk_b;
  uint32_t  dispclk2gfxclk_c;
  uint32_t  pixclk2gfxclk_a;
  uint32_t  pixclk2gfxclk_b;
  uint32_t  pixclk2gfxclk_c;
  uint32_t  dcefclk2gfxclk_a;
  uint32_t  dcefclk2gfxclk_b;
  uint32_t  dcefclk2gfxclk_c;
  uint32_t  phyclk2gfxclk_a;
  uint32_t  phyclk2gfxclk_b;
  uint32_t  phyclk2gfxclk_c;
};

struct  atom_asic_profiling_info_v4_2 {
	struct  atom_common_table_header  table_header;
	uint32_t  maxvddc;
	uint32_t  minvddc;
	uint32_t  avfs_meannsigma_acontant0;
	uint32_t  avfs_meannsigma_acontant1;
	uint32_t  avfs_meannsigma_acontant2;
	uint16_t  avfs_meannsigma_dc_tol_sigma;
	uint16_t  avfs_meannsigma_platform_mean;
	uint16_t  avfs_meannsigma_platform_sigma;
	uint32_t  gb_vdroop_table_cksoff_a0;
	uint32_t  gb_vdroop_table_cksoff_a1;
	uint32_t  gb_vdroop_table_cksoff_a2;
	uint32_t  gb_vdroop_table_ckson_a0;
	uint32_t  gb_vdroop_table_ckson_a1;
	uint32_t  gb_vdroop_table_ckson_a2;
	uint32_t  avfsgb_fuse_table_cksoff_m1;
	uint32_t  avfsgb_fuse_table_cksoff_m2;
	uint32_t  avfsgb_fuse_table_cksoff_b;
	uint32_t  avfsgb_fuse_table_ckson_m1;
	uint32_t  avfsgb_fuse_table_ckson_m2;
	uint32_t  avfsgb_fuse_table_ckson_b;
	uint16_t  max_voltage_0_25mv;
	uint8_t   enable_gb_vdroop_table_cksoff;
	uint8_t   enable_gb_vdroop_table_ckson;
	uint8_t   enable_gb_fuse_table_cksoff;
	uint8_t   enable_gb_fuse_table_ckson;
	uint16_t  psm_age_comfactor;
	uint8_t   enable_apply_avfs_cksoff_voltage;
	uint8_t   reserved;
	uint32_t  dispclk2gfxclk_a;
	uint32_t  dispclk2gfxclk_b;
	uint32_t  dispclk2gfxclk_c;
	uint32_t  pixclk2gfxclk_a;
	uint32_t  pixclk2gfxclk_b;
	uint32_t  pixclk2gfxclk_c;
	uint32_t  dcefclk2gfxclk_a;
	uint32_t  dcefclk2gfxclk_b;
	uint32_t  dcefclk2gfxclk_c;
	uint32_t  phyclk2gfxclk_a;
	uint32_t  phyclk2gfxclk_b;
	uint32_t  phyclk2gfxclk_c;
	uint32_t  acg_gb_vdroop_table_a0;
	uint32_t  acg_gb_vdroop_table_a1;
	uint32_t  acg_gb_vdroop_table_a2;
	uint32_t  acg_avfsgb_fuse_table_m1;
	uint32_t  acg_avfsgb_fuse_table_m2;
	uint32_t  acg_avfsgb_fuse_table_b;
	uint8_t   enable_acg_gb_vdroop_table;
	uint8_t   enable_acg_gb_fuse_table;
	uint32_t  acg_dispclk2gfxclk_a;
	uint32_t  acg_dispclk2gfxclk_b;
	uint32_t  acg_dispclk2gfxclk_c;
	uint32_t  acg_pixclk2gfxclk_a;
	uint32_t  acg_pixclk2gfxclk_b;
	uint32_t  acg_pixclk2gfxclk_c;
	uint32_t  acg_dcefclk2gfxclk_a;
	uint32_t  acg_dcefclk2gfxclk_b;
	uint32_t  acg_dcefclk2gfxclk_c;
	uint32_t  acg_phyclk2gfxclk_a;
	uint32_t  acg_phyclk2gfxclk_b;
	uint32_t  acg_phyclk2gfxclk_c;
};

/* 
  ***************************************************************************
    Data Table multimedia_info  structure
  ***************************************************************************
*/
struct atom_multimedia_info_v2_1
{
  struct  atom_common_table_header  table_header;
  uint8_t uvdip_min_ver;
  uint8_t uvdip_max_ver;
  uint8_t vceip_min_ver;
  uint8_t vceip_max_ver;
  uint16_t uvd_enc_max_input_width_pixels;
  uint16_t uvd_enc_max_input_height_pixels;
  uint16_t vce_enc_max_input_width_pixels;
  uint16_t vce_enc_max_input_height_pixels; 
  uint32_t uvd_enc_max_bandwidth;           // 16x16 pixels/sec, codec independent
  uint32_t vce_enc_max_bandwidth;           // 16x16 pixels/sec, codec independent
};


/* 
  ***************************************************************************
    Data Table umc_info  structure
  ***************************************************************************
*/
struct atom_umc_info_v3_1
{
  struct  atom_common_table_header  table_header;
  uint32_t ucode_version;
  uint32_t ucode_rom_startaddr;
  uint32_t ucode_length;
  uint16_t umc_reg_init_offset;
  uint16_t customer_ucode_name_offset;
  uint16_t mclk_ss_percentage;
  uint16_t mclk_ss_rate_10hz;
  uint8_t umcip_min_ver;
  uint8_t umcip_max_ver;
  uint8_t vram_type;              //enum of atom_dgpu_vram_type
  uint8_t umc_config;
  uint32_t mem_refclk_10khz;
};

// umc_info.umc_config
enum atom_umc_config_def {
  UMC_CONFIG__ENABLE_1KB_INTERLEAVE_MODE  =   0x00000001,
  UMC_CONFIG__DEFAULT_MEM_ECC_ENABLE      =   0x00000002,
  UMC_CONFIG__ENABLE_HBM_LANE_REPAIR      =   0x00000004,
  UMC_CONFIG__ENABLE_BANK_HARVESTING      =   0x00000008,
  UMC_CONFIG__ENABLE_PHY_REINIT           =   0x00000010,
  UMC_CONFIG__DISABLE_UCODE_CHKSTATUS     =   0x00000020,
};

struct atom_umc_info_v3_2
{
  struct  atom_common_table_header  table_header;
  uint32_t ucode_version;
  uint32_t ucode_rom_startaddr;
  uint32_t ucode_length;
  uint16_t umc_reg_init_offset;
  uint16_t customer_ucode_name_offset;
  uint16_t mclk_ss_percentage;
  uint16_t mclk_ss_rate_10hz;
  uint8_t umcip_min_ver;
  uint8_t umcip_max_ver;
  uint8_t vram_type;              //enum of atom_dgpu_vram_type
  uint8_t umc_config;
  uint32_t mem_refclk_10khz;
  uint32_t pstate_uclk_10khz[4];
  uint16_t umcgoldenoffset;
  uint16_t densitygoldenoffset;
};

struct atom_umc_info_v3_3
{
  struct  atom_common_table_header  table_header;
  uint32_t ucode_reserved;
  uint32_t ucode_rom_startaddr;
  uint32_t ucode_length;
  uint16_t umc_reg_init_offset;
  uint16_t customer_ucode_name_offset;
  uint16_t mclk_ss_percentage;
  uint16_t mclk_ss_rate_10hz;
  uint8_t umcip_min_ver;
  uint8_t umcip_max_ver;
  uint8_t vram_type;              //enum of atom_dgpu_vram_type
  uint8_t umc_config;
  uint32_t mem_refclk_10khz;
  uint32_t pstate_uclk_10khz[4];
  uint16_t umcgoldenoffset;
  uint16_t densitygoldenoffset;
  uint32_t umc_config1;
  uint32_t bist_data_startaddr;
  uint32_t reserved[2];
};

enum atom_umc_config1_def {
	UMC_CONFIG1__ENABLE_PSTATE_PHASE_STORE_TRAIN = 0x00000001,
	UMC_CONFIG1__ENABLE_AUTO_FRAMING = 0x00000002,
	UMC_CONFIG1__ENABLE_RESTORE_BIST_DATA = 0x00000004,
	UMC_CONFIG1__DISABLE_STROBE_MODE = 0x00000008,
	UMC_CONFIG1__DEBUG_DATA_PARITY_EN = 0x00000010,
	UMC_CONFIG1__ENABLE_ECC_CAPABLE = 0x00010000,
};

struct atom_umc_info_v4_0 {
	struct atom_common_table_header table_header;
	uint32_t ucode_reserved[5];
	uint8_t umcip_min_ver;
	uint8_t umcip_max_ver;
	uint8_t vram_type;
	uint8_t umc_config;
	uint32_t mem_refclk_10khz;
	uint32_t clk_reserved[4];
	uint32_t golden_reserved;
	uint32_t umc_config1;
	uint32_t reserved[2];
	uint8_t channel_num;
	uint8_t channel_width;
	uint8_t channel_reserve[2];
	uint8_t umc_info_reserved[16];
};

/* 
  ***************************************************************************
    Data Table vram_info  structure
  ***************************************************************************
*/
struct atom_vram_module_v9 {
  // Design Specific Values
  uint32_t  memory_size;                   // Total memory size in unit of MB for CONFIG_MEMSIZE zeros
  uint32_t  channel_enable;                // bit vector, each bit indicate specific channel enable or not
  uint32_t  max_mem_clk;                   // max memory clock of this memory in unit of 10kHz, =0 means it is not defined
  uint16_t  reserved[3];
  uint16_t  mem_voltage;                   // mem_voltage
  uint16_t  vram_module_size;              // Size of atom_vram_module_v9
  uint8_t   ext_memory_id;                 // Current memory module ID
  uint8_t   memory_type;                   // enum of atom_dgpu_vram_type
  uint8_t   channel_num;                   // Number of mem. channels supported in this module
  uint8_t   channel_width;                 // CHANNEL_16BIT/CHANNEL_32BIT/CHANNEL_64BIT
  uint8_t   density;                       // _8Mx32, _16Mx32, _16Mx16, _32Mx16
  uint8_t   tunningset_id;                 // MC phy registers set per. 
  uint8_t   vender_rev_id;                 // [7:4] Revision, [3:0] Vendor code
  uint8_t   refreshrate;                   // [1:0]=RefreshFactor (00=8ms, 01=16ms, 10=32ms,11=64ms)
  uint8_t   hbm_ven_rev_id;		   // hbm_ven_rev_id
  uint8_t   vram_rsd2;			   // reserved
  char    dram_pnstring[20];               // part number end with '0'. 
};

struct atom_vram_info_header_v2_3 {
  struct   atom_common_table_header table_header;
  uint16_t mem_adjust_tbloffset;                         // offset of atom_umc_init_reg_block structure for memory vendor specific UMC adjust setting
  uint16_t mem_clk_patch_tbloffset;                      // offset of atom_umc_init_reg_block structure for memory clock specific UMC setting
  uint16_t mc_adjust_pertile_tbloffset;                  // offset of atom_umc_init_reg_block structure for Per Byte Offset Preset Settings
  uint16_t mc_phyinit_tbloffset;                         // offset of atom_umc_init_reg_block structure for MC phy init set
  uint16_t dram_data_remap_tbloffset;                    // reserved for now
  uint16_t tmrs_seq_offset;                              // offset of HBM tmrs
  uint16_t post_ucode_init_offset;                       // offset of atom_umc_init_reg_block structure for MC phy init after MC uCode complete umc init
  uint16_t vram_rsd2;
  uint8_t  vram_module_num;                              // indicate number of VRAM module
  uint8_t  umcip_min_ver;
  uint8_t  umcip_max_ver;
  uint8_t  mc_phy_tile_num;                              // indicate the MCD tile number which use in DramDataRemapTbl and usMcAdjustPerTileTblOffset
  struct   atom_vram_module_v9  vram_module[16];         // just for allocation, real number of blocks is in ucNumOfVRAMModule;
};

/*
  ***************************************************************************
    Data Table vram_info v3.0  structure
  ***************************************************************************
*/
struct atom_vram_module_v3_0 {
	uint8_t density;
	uint8_t tunningset_id;
	uint8_t ext_memory_id;
	uint8_t dram_vendor_id;
	uint16_t dram_info_offset;
	uint16_t mem_tuning_offset;
	uint16_t tmrs_seq_offset;
	uint16_t reserved1;
	uint32_t dram_size_per_ch;
	uint32_t reserved[3];
	char dram_pnstring[40];
};

struct atom_vram_info_header_v3_0 {
	struct atom_common_table_header table_header;
	uint16_t mem_tuning_table_offset;
	uint16_t dram_info_table_offset;
	uint16_t tmrs_table_offset;
	uint16_t mc_init_table_offset;
	uint16_t dram_data_remap_table_offset;
	uint16_t umc_emuinittable_offset;
	uint16_t reserved_sub_table_offset[2];
	uint8_t vram_module_num;
	uint8_t umcip_min_ver;
	uint8_t umcip_max_ver;
	uint8_t mc_phy_tile_num;
	uint8_t memory_type;
	uint8_t channel_num;
	uint8_t channel_width;
	uint8_t reserved1;
	uint32_t channel_enable;
	uint32_t channel1_enable;
	uint32_t feature_enable;
	uint32_t feature1_enable;
	uint32_t hardcode_mem_size;
	uint32_t reserved4[4];
	struct atom_vram_module_v3_0 vram_module[8];
};

struct atom_umc_register_addr_info{
  uint32_t  umc_register_addr:24;
  uint32_t  umc_reg_type_ind:1;
  uint32_t  umc_reg_rsvd:7;
};

//atom_umc_register_addr_info.
enum atom_umc_register_addr_info_flag{
  b3ATOM_UMC_REG_ADD_INFO_INDIRECT_ACCESS  =0x01,
};

union atom_umc_register_addr_info_access
{
  struct atom_umc_register_addr_info umc_reg_addr;
  uint32_t u32umc_reg_addr;
};

struct atom_umc_reg_setting_id_config{
  uint32_t memclockrange:24;
  uint32_t mem_blk_id:8;
};

union atom_umc_reg_setting_id_config_access
{
  struct atom_umc_reg_setting_id_config umc_id_access;
  uint32_t  u32umc_id_access;
};

struct atom_umc_reg_setting_data_block{
  union atom_umc_reg_setting_id_config_access  block_id;
  uint32_t u32umc_reg_data[1];                       
};

struct atom_umc_init_reg_block{
  uint16_t umc_reg_num;
  uint16_t reserved;    
  union atom_umc_register_addr_info_access umc_reg_list[1];     //for allocation purpose, the real number come from umc_reg_num;
  struct atom_umc_reg_setting_data_block umc_reg_setting_list[1];
};

struct atom_vram_module_v10 {
  // Design Specific Values
  uint32_t  memory_size;                   // Total memory size in unit of MB for CONFIG_MEMSIZE zeros
  uint32_t  channel_enable;                // bit vector, each bit indicate specific channel enable or not
  uint32_t  max_mem_clk;                   // max memory clock of this memory in unit of 10kHz, =0 means it is not defined
  uint16_t  reserved[3];
  uint16_t  mem_voltage;                   // mem_voltage
  uint16_t  vram_module_size;              // Size of atom_vram_module_v9
  uint8_t   ext_memory_id;                 // Current memory module ID
  uint8_t   memory_type;                   // enum of atom_dgpu_vram_type
  uint8_t   channel_num;                   // Number of mem. channels supported in this module
  uint8_t   channel_width;                 // CHANNEL_16BIT/CHANNEL_32BIT/CHANNEL_64BIT
  uint8_t   density;                       // _8Mx32, _16Mx32, _16Mx16, _32Mx16
  uint8_t   tunningset_id;                 // MC phy registers set per
  uint8_t   vender_rev_id;                 // [7:4] Revision, [3:0] Vendor code
  uint8_t   refreshrate;                   // [1:0]=RefreshFactor (00=8ms, 01=16ms, 10=32ms,11=64ms)
  uint8_t   vram_flags;			   // bit0= bankgroup enable
  uint8_t   vram_rsd2;			   // reserved
  uint16_t  gddr6_mr10;                    // gddr6 mode register10 value
  uint16_t  gddr6_mr1;                     // gddr6 mode register1 value
  uint16_t  gddr6_mr2;                     // gddr6 mode register2 value
  uint16_t  gddr6_mr7;                     // gddr6 mode register7 value
  char    dram_pnstring[20];               // part number end with '0'
};

struct atom_vram_info_header_v2_4 {
  struct   atom_common_table_header table_header;
  uint16_t mem_adjust_tbloffset;                         // offset of atom_umc_init_reg_block structure for memory vendor specific UMC adjust setting
  uint16_t mem_clk_patch_tbloffset;                      // offset of atom_umc_init_reg_block structure for memory clock specific UMC setting
  uint16_t mc_adjust_pertile_tbloffset;                  // offset of atom_umc_init_reg_block structure for Per Byte Offset Preset Settings
  uint16_t mc_phyinit_tbloffset;                         // offset of atom_umc_init_reg_block structure for MC phy init set
  uint16_t dram_data_remap_tbloffset;                    // reserved for now
  uint16_t reserved;                                     // offset of reserved
  uint16_t post_ucode_init_offset;                       // offset of atom_umc_init_reg_block structure for MC phy init after MC uCode complete umc init
  uint16_t vram_rsd2;
  uint8_t  vram_module_num;                              // indicate number of VRAM module
  uint8_t  umcip_min_ver;
  uint8_t  umcip_max_ver;
  uint8_t  mc_phy_tile_num;                              // indicate the MCD tile number which use in DramDataRemapTbl and usMcAdjustPerTileTblOffset
  struct   atom_vram_module_v10  vram_module[16];        // just for allocation, real number of blocks is in ucNumOfVRAMModule;
};

struct atom_vram_module_v11 {
	// Design Specific Values
	uint32_t  memory_size;                   // Total memory size in unit of MB for CONFIG_MEMSIZE zeros
	uint32_t  channel_enable;                // bit vector, each bit indicate specific channel enable or not
	uint16_t  mem_voltage;                   // mem_voltage
	uint16_t  vram_module_size;              // Size of atom_vram_module_v9
	uint8_t   ext_memory_id;                 // Current memory module ID
	uint8_t   memory_type;                   // enum of atom_dgpu_vram_type
	uint8_t   channel_num;                   // Number of mem. channels supported in this module
	uint8_t   channel_width;                 // CHANNEL_16BIT/CHANNEL_32BIT/CHANNEL_64BIT
	uint8_t   density;                       // _8Mx32, _16Mx32, _16Mx16, _32Mx16
	uint8_t   tunningset_id;                 // MC phy registers set per.
	uint16_t  reserved[4];                   // reserved
	uint8_t   vender_rev_id;                 // [7:4] Revision, [3:0] Vendor code
	uint8_t   refreshrate;			 // [1:0]=RefreshFactor (00=8ms, 01=16ms, 10=32ms,11=64ms)
	uint8_t   vram_flags;			 // bit0= bankgroup enable
	uint8_t   vram_rsd2;			 // reserved
	uint16_t  gddr6_mr10;                    // gddr6 mode register10 value
	uint16_t  gddr6_mr0;                     // gddr6 mode register0 value
	uint16_t  gddr6_mr1;                     // gddr6 mode register1 value
	uint16_t  gddr6_mr2;                     // gddr6 mode register2 value
	uint16_t  gddr6_mr4;                     // gddr6 mode register4 value
	uint16_t  gddr6_mr7;                     // gddr6 mode register7 value
	uint16_t  gddr6_mr8;                     // gddr6 mode register8 value
	char    dram_pnstring[40];               // part number end with '0'.
};

struct atom_gddr6_ac_timing_v2_5 {
	uint32_t  u32umc_id_access;
	uint8_t  RL;
	uint8_t  WL;
	uint8_t  tRAS;
	uint8_t  tRC;

	uint16_t  tREFI;
	uint8_t  tRFC;
	uint8_t  tRFCpb;

	uint8_t  tRREFD;
	uint8_t  tRCDRD;
	uint8_t  tRCDWR;
	uint8_t  tRP;

	uint8_t  tRRDS;
	uint8_t  tRRDL;
	uint8_t  tWR;
	uint8_t  tWTRS;

	uint8_t  tWTRL;
	uint8_t  tFAW;
	uint8_t  tCCDS;
	uint8_t  tCCDL;

	uint8_t  tCRCRL;
	uint8_t  tCRCWL;
	uint8_t  tCKE;
	uint8_t  tCKSRE;

	uint8_t  tCKSRX;
	uint8_t  tRTPS;
	uint8_t  tRTPL;
	uint8_t  tMRD;

	uint8_t  tMOD;
	uint8_t  tXS;
	uint8_t  tXHP;
	uint8_t  tXSMRS;

	uint32_t  tXSH;

	uint8_t  tPD;
	uint8_t  tXP;
	uint8_t  tCPDED;
	uint8_t  tACTPDE;

	uint8_t  tPREPDE;
	uint8_t  tREFPDE;
	uint8_t  tMRSPDEN;
	uint8_t  tRDSRE;

	uint8_t  tWRSRE;
	uint8_t  tPPD;
	uint8_t  tCCDMW;
	uint8_t  tWTRTR;

	uint8_t  tLTLTR;
	uint8_t  tREFTR;
	uint8_t  VNDR;
	uint8_t  reserved[9];
};

struct atom_gddr6_bit_byte_remap {
	uint32_t dphy_byteremap;    //mmUMC_DPHY_ByteRemap
	uint32_t dphy_bitremap0;    //mmUMC_DPHY_BitRemap0
	uint32_t dphy_bitremap1;    //mmUMC_DPHY_BitRemap1
	uint32_t dphy_bitremap2;    //mmUMC_DPHY_BitRemap2
	uint32_t aphy_bitremap0;    //mmUMC_APHY_BitRemap0
	uint32_t aphy_bitremap1;    //mmUMC_APHY_BitRemap1
	uint32_t phy_dram;          //mmUMC_PHY_DRAM
};

struct atom_gddr6_dram_data_remap {
	uint32_t table_size;
	uint8_t phyintf_ck_inverted[8];     //UMC_PHY_PHYINTF_CNTL.INV_CK
	struct atom_gddr6_bit_byte_remap bit_byte_remap[16];
};

struct atom_vram_info_header_v2_5 {
	struct   atom_common_table_header table_header;
	uint16_t mem_adjust_tbloffset;                         // offset of atom_umc_init_reg_block structure for memory vendor specific UMC adjust settings
	uint16_t gddr6_ac_timing_offset;                     // offset of atom_gddr6_ac_timing_v2_5 structure for memory clock specific UMC settings
	uint16_t mc_adjust_pertile_tbloffset;                  // offset of atom_umc_init_reg_block structure for Per Byte Offset Preset Settings
	uint16_t mc_phyinit_tbloffset;                         // offset of atom_umc_init_reg_block structure for MC phy init set
	uint16_t dram_data_remap_tbloffset;                    // offset of atom_gddr6_dram_data_remap array to indicate DRAM data lane to GPU mapping
	uint16_t reserved;                                     // offset of reserved
	uint16_t post_ucode_init_offset;                       // offset of atom_umc_init_reg_block structure for MC phy init after MC uCode complete umc init
	uint16_t strobe_mode_patch_tbloffset;                  // offset of atom_umc_init_reg_block structure for Strobe Mode memory clock specific UMC settings
	uint8_t  vram_module_num;                              // indicate number of VRAM module
	uint8_t  umcip_min_ver;
	uint8_t  umcip_max_ver;
	uint8_t  mc_phy_tile_num;                              // indicate the MCD tile number which use in DramDataRemapTbl and usMcAdjustPerTileTblOffset
	struct   atom_vram_module_v11  vram_module[16];        // just for allocation, real number of blocks is in ucNumOfVRAMModule;
};

struct atom_vram_info_header_v2_6 {
	struct atom_common_table_header table_header;
	uint16_t mem_adjust_tbloffset;
	uint16_t mem_clk_patch_tbloffset;
	uint16_t mc_adjust_pertile_tbloffset;
	uint16_t mc_phyinit_tbloffset;
	uint16_t dram_data_remap_tbloffset;
	uint16_t tmrs_seq_offset;
	uint16_t post_ucode_init_offset;
	uint16_t vram_rsd2;
	uint8_t  vram_module_num;
	uint8_t  umcip_min_ver;
	uint8_t  umcip_max_ver;
	uint8_t  mc_phy_tile_num;
	struct atom_vram_module_v9 vram_module[16];
};
/* 
  ***************************************************************************
    Data Table voltageobject_info  structure
  ***************************************************************************
*/
struct  atom_i2c_data_entry
{
  uint16_t  i2c_reg_index;               // i2c register address, can be up to 16bit
  uint16_t  i2c_reg_data;                // i2c register data, can be up to 16bit
};

struct atom_voltage_object_header_v4{
  uint8_t    voltage_type;                           //enum atom_voltage_type
  uint8_t    voltage_mode;                           //enum atom_voltage_object_mode 
  uint16_t   object_size;                            //Size of Object
};

// atom_voltage_object_header_v4.voltage_mode
enum atom_voltage_object_mode 
{
   VOLTAGE_OBJ_GPIO_LUT              =  0,        //VOLTAGE and GPIO Lookup table ->atom_gpio_voltage_object_v4
   VOLTAGE_OBJ_VR_I2C_INIT_SEQ       =  3,        //VOLTAGE REGULATOR INIT sequece through I2C -> atom_i2c_voltage_object_v4
   VOLTAGE_OBJ_PHASE_LUT             =  4,        //Set Vregulator Phase lookup table ->atom_gpio_voltage_object_v4
   VOLTAGE_OBJ_SVID2                 =  7,        //Indicate voltage control by SVID2 ->atom_svid2_voltage_object_v4
   VOLTAGE_OBJ_EVV                   =  8, 
   VOLTAGE_OBJ_MERGED_POWER          =  9,
};

struct  atom_i2c_voltage_object_v4
{
   struct atom_voltage_object_header_v4 header;  // voltage mode = VOLTAGE_OBJ_VR_I2C_INIT_SEQ
   uint8_t  regulator_id;                        //Indicate Voltage Regulator Id
   uint8_t  i2c_id;
   uint8_t  i2c_slave_addr;
   uint8_t  i2c_control_offset;       
   uint8_t  i2c_flag;                            // Bit0: 0 - One byte data; 1 - Two byte data
   uint8_t  i2c_speed;                           // =0, use default i2c speed, otherwise use it in unit of kHz. 
   uint8_t  reserved[2];
   struct atom_i2c_data_entry i2cdatalut[1];     // end with 0xff
};

// ATOM_I2C_VOLTAGE_OBJECT_V3.ucVoltageControlFlag
enum atom_i2c_voltage_control_flag
{
   VOLTAGE_DATA_ONE_BYTE = 0,
   VOLTAGE_DATA_TWO_BYTE = 1,
};


struct atom_voltage_gpio_map_lut
{
  uint32_t  voltage_gpio_reg_val;              // The Voltage ID which is used to program GPIO register
  uint16_t  voltage_level_mv;                  // The corresponding Voltage Value, in mV
};

struct atom_gpio_voltage_object_v4
{
   struct atom_voltage_object_header_v4 header;  // voltage mode = VOLTAGE_OBJ_GPIO_LUT or VOLTAGE_OBJ_PHASE_LUT
   uint8_t  gpio_control_id;                     // default is 0 which indicate control through CG VID mode 
   uint8_t  gpio_entry_num;                      // indiate the entry numbers of Votlage/Gpio value Look up table
   uint8_t  phase_delay_us;                      // phase delay in unit of micro second
   uint8_t  reserved;   
   uint32_t gpio_mask_val;                         // GPIO Mask value
   struct atom_voltage_gpio_map_lut voltage_gpio_lut[] __counted_by(gpio_entry_num);
};

struct  atom_svid2_voltage_object_v4
{
   struct atom_voltage_object_header_v4 header;  // voltage mode = VOLTAGE_OBJ_SVID2
   uint8_t loadline_psi1;                        // bit4:0= loadline setting ( Core Loadline trim and offset trim ), bit5=0:PSI1_L disable =1: PSI1_L enable
   uint8_t psi0_l_vid_thresd;                    // VR PSI0_L VID threshold
   uint8_t psi0_enable;                          // 
   uint8_t maxvstep;
   uint8_t telemetry_offset;
   uint8_t telemetry_gain; 
   uint16_t reserved1;
};

struct atom_merged_voltage_object_v4
{
  struct atom_voltage_object_header_v4 header;  // voltage mode = VOLTAGE_OBJ_MERGED_POWER
  uint8_t  merged_powerrail_type;               //enum atom_voltage_type
  uint8_t  reserved[3];
};

union atom_voltage_object_v4{
  struct atom_gpio_voltage_object_v4 gpio_voltage_obj;
  struct atom_i2c_voltage_object_v4 i2c_voltage_obj;
  struct atom_svid2_voltage_object_v4 svid2_voltage_obj;
  struct atom_merged_voltage_object_v4 merged_voltage_obj;
};

struct  atom_voltage_objects_info_v4_1
{
  struct atom_common_table_header table_header; 
  union atom_voltage_object_v4 voltage_object[1];   //Info for Voltage control
};


/* 
  ***************************************************************************
              All Command Function structure definition 
  *************************************************************************** 
*/   

/* 
  ***************************************************************************
              Structures used by asic_init
  *************************************************************************** 
*/   

struct asic_init_engine_parameters
{
  uint32_t sclkfreqin10khz:24;
  uint32_t engineflag:8;              /* enum atom_asic_init_engine_flag  */
};

struct asic_init_mem_parameters
{
  uint32_t mclkfreqin10khz:24;
  uint32_t memflag:8;                 /* enum atom_asic_init_mem_flag  */
};

struct asic_init_parameters_v2_1
{
  struct asic_init_engine_parameters engineparam;
  struct asic_init_mem_parameters memparam;
};

struct asic_init_ps_allocation_v2_1
{
  struct asic_init_parameters_v2_1 param;
  uint32_t reserved[16];
};


enum atom_asic_init_engine_flag
{
  b3NORMAL_ENGINE_INIT = 0,
  b3SRIOV_SKIP_ASIC_INIT = 0x02,
  b3SRIOV_LOAD_UCODE = 0x40,
};

enum atom_asic_init_mem_flag
{
  b3NORMAL_MEM_INIT = 0,
  b3DRAM_SELF_REFRESH_EXIT =0x20,
};

/* 
  ***************************************************************************
              Structures used by setengineclock
  *************************************************************************** 
*/   

struct set_engine_clock_parameters_v2_1
{
  uint32_t sclkfreqin10khz:24;
  uint32_t sclkflag:8;              /* enum atom_set_engine_mem_clock_flag,  */
  uint32_t reserved[10];
};

struct set_engine_clock_ps_allocation_v2_1
{
  struct set_engine_clock_parameters_v2_1 clockinfo;
  uint32_t reserved[10];
};


enum atom_set_engine_mem_clock_flag
{
  b3NORMAL_CHANGE_CLOCK = 0,
  b3FIRST_TIME_CHANGE_CLOCK = 0x08,
  b3STORE_DPM_TRAINGING = 0x40,         //Applicable to memory clock change,when set, it store specific DPM mode training result
};

/* 
  ***************************************************************************
              Structures used by getengineclock
  *************************************************************************** 
*/   
struct get_engine_clock_parameter
{
  uint32_t sclk_10khz;          // current engine speed in 10KHz unit
  uint32_t reserved;
};

/* 
  ***************************************************************************
              Structures used by setmemoryclock
  *************************************************************************** 
*/   
struct set_memory_clock_parameters_v2_1
{
  uint32_t mclkfreqin10khz:24;
  uint32_t mclkflag:8;              /* enum atom_set_engine_mem_clock_flag,  */
  uint32_t reserved[10];
};

struct set_memory_clock_ps_allocation_v2_1
{
  struct set_memory_clock_parameters_v2_1 clockinfo;
  uint32_t reserved[10];
};


/* 
  ***************************************************************************
              Structures used by getmemoryclock
  *************************************************************************** 
*/   
struct get_memory_clock_parameter
{
  uint32_t mclk_10khz;          // current engine speed in 10KHz unit
  uint32_t reserved;
};



/* 
  ***************************************************************************
              Structures used by setvoltage
  *************************************************************************** 
*/   

struct set_voltage_parameters_v1_4
{
  uint8_t  voltagetype;                /* enum atom_voltage_type */
  uint8_t  command;                    /* Indicate action: Set voltage level, enum atom_set_voltage_command */
  uint16_t vlevel_mv;                  /* real voltage level in unit of mv or Voltage Phase (0, 1, 2, .. ) */
};

//set_voltage_parameters_v2_1.voltagemode
enum atom_set_voltage_command{
  ATOM_SET_VOLTAGE  = 0,
  ATOM_INIT_VOLTAGE_REGULATOR = 3,
  ATOM_SET_VOLTAGE_PHASE = 4,
  ATOM_GET_LEAKAGE_ID    = 8,
};

struct set_voltage_ps_allocation_v1_4
{
  struct set_voltage_parameters_v1_4 setvoltageparam;
  uint32_t reserved[10];
};


/* 
  ***************************************************************************
              Structures used by computegpuclockparam
  *************************************************************************** 
*/   

//ATOM_COMPUTE_CLOCK_FREQ.ulComputeClockFlag
enum atom_gpu_clock_type 
{
  COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK =0x00,
  COMPUTE_GPUCLK_INPUT_FLAG_GFXCLK =0x01,
  COMPUTE_GPUCLK_INPUT_FLAG_UCLK =0x02,
};

struct compute_gpu_clock_input_parameter_v1_8
{
  uint32_t  gpuclock_10khz:24;         //Input= target clock, output = actual clock 
  uint32_t  gpu_clock_type:8;          //Input indicate clock type: enum atom_gpu_clock_type
  uint32_t  reserved[5];
};


struct compute_gpu_clock_output_parameter_v1_8
{
  uint32_t  gpuclock_10khz:24;              //Input= target clock, output = actual clock 
  uint32_t  dfs_did:8;                      //return parameter: DFS divider which is used to program to register directly
  uint32_t  pll_fb_mult;                    //Feedback Multiplier, bit 8:0 int, bit 15:12 post_div, bit 31:16 frac
  uint32_t  pll_ss_fbsmult;                 // Spread FB Mult: bit 8:0 int, bit 31:16 frac
  uint16_t  pll_ss_slew_frac;
  uint8_t   pll_ss_enable;
  uint8_t   reserved;
  uint32_t  reserved1[2];
};



/* 
  ***************************************************************************
              Structures used by ReadEfuseValue
  *************************************************************************** 
*/   

struct read_efuse_input_parameters_v3_1
{
  uint16_t efuse_start_index;
  uint8_t  reserved;
  uint8_t  bitslen;
};

// ReadEfuseValue input/output parameter
union read_efuse_value_parameters_v3_1
{
  struct read_efuse_input_parameters_v3_1 efuse_info;
  uint32_t efusevalue;
};


/* 
  ***************************************************************************
              Structures used by getsmuclockinfo
  *************************************************************************** 
*/   
struct atom_get_smu_clock_info_parameters_v3_1
{
  uint8_t syspll_id;          // 0= syspll0, 1=syspll1, 2=syspll2                
  uint8_t clk_id;             // atom_smu9_syspll0_clock_id  (only valid when command == GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ )
  uint8_t command;            // enum of atom_get_smu_clock_info_command
  uint8_t dfsdid;             // =0: get DFS DID from register, >0, give DFS divider, (only valid when command == GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ )
};

enum atom_get_smu_clock_info_command 
{
  GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ       = 0,
  GET_SMU_CLOCK_INFO_V3_1_GET_PLLVCO_FREQ      = 1,
  GET_SMU_CLOCK_INFO_V3_1_GET_PLLREFCLK_FREQ   = 2,
};

enum atom_smu9_syspll0_clock_id
{
  SMU9_SYSPLL0_SMNCLK_ID   = 0,       //  SMNCLK
  SMU9_SYSPLL0_SOCCLK_ID   = 1,       //	SOCCLK (FCLK)
  SMU9_SYSPLL0_MP0CLK_ID   = 2,       //	MP0CLK
  SMU9_SYSPLL0_MP1CLK_ID   = 3,       //	MP1CLK
  SMU9_SYSPLL0_LCLK_ID     = 4,       //	LCLK
  SMU9_SYSPLL0_DCLK_ID     = 5,       //	DCLK
  SMU9_SYSPLL0_VCLK_ID     = 6,       //	VCLK
  SMU9_SYSPLL0_ECLK_ID     = 7,       //	ECLK
  SMU9_SYSPLL0_DCEFCLK_ID  = 8,       //	DCEFCLK
  SMU9_SYSPLL0_DPREFCLK_ID = 10,      //	DPREFCLK
  SMU9_SYSPLL0_DISPCLK_ID  = 11,      //	DISPCLK
};

enum atom_smu11_syspll_id {
  SMU11_SYSPLL0_ID            = 0,
  SMU11_SYSPLL1_0_ID          = 1,
  SMU11_SYSPLL1_1_ID          = 2,
  SMU11_SYSPLL1_2_ID          = 3,
  SMU11_SYSPLL2_ID            = 4,
  SMU11_SYSPLL3_0_ID          = 5,
  SMU11_SYSPLL3_1_ID          = 6,
};

enum atom_smu11_syspll0_clock_id {
  SMU11_SYSPLL0_ECLK_ID     = 0,       //	ECLK
  SMU11_SYSPLL0_SOCCLK_ID   = 1,       //	SOCCLK
  SMU11_SYSPLL0_MP0CLK_ID   = 2,       //	MP0CLK
  SMU11_SYSPLL0_DCLK_ID     = 3,       //	DCLK
  SMU11_SYSPLL0_VCLK_ID     = 4,       //	VCLK
  SMU11_SYSPLL0_DCEFCLK_ID  = 5,       //	DCEFCLK
};

enum atom_smu11_syspll1_0_clock_id {
  SMU11_SYSPLL1_0_UCLKA_ID   = 0,       // UCLK_a
};

enum atom_smu11_syspll1_1_clock_id {
  SMU11_SYSPLL1_0_UCLKB_ID   = 0,       // UCLK_b
};

enum atom_smu11_syspll1_2_clock_id {
  SMU11_SYSPLL1_0_FCLK_ID   = 0,        // FCLK
};

enum atom_smu11_syspll2_clock_id {
  SMU11_SYSPLL2_GFXCLK_ID   = 0,        // GFXCLK
};

enum atom_smu11_syspll3_0_clock_id {
  SMU11_SYSPLL3_0_WAFCLK_ID = 0,       //	WAFCLK
  SMU11_SYSPLL3_0_DISPCLK_ID = 1,      //	DISPCLK
  SMU11_SYSPLL3_0_DPREFCLK_ID = 2,     //	DPREFCLK
};

enum atom_smu11_syspll3_1_clock_id {
  SMU11_SYSPLL3_1_MP1CLK_ID = 0,       //	MP1CLK
  SMU11_SYSPLL3_1_SMNCLK_ID = 1,       //	SMNCLK
  SMU11_SYSPLL3_1_LCLK_ID = 2,         //	LCLK
};

enum atom_smu12_syspll_id {
  SMU12_SYSPLL0_ID          = 0,
  SMU12_SYSPLL1_ID          = 1,
  SMU12_SYSPLL2_ID          = 2,
  SMU12_SYSPLL3_0_ID        = 3,
  SMU12_SYSPLL3_1_ID        = 4,
};

enum atom_smu12_syspll0_clock_id {
  SMU12_SYSPLL0_SMNCLK_ID   = 0,			//	SOCCLK
  SMU12_SYSPLL0_SOCCLK_ID   = 1,			//	SOCCLK
  SMU12_SYSPLL0_MP0CLK_ID   = 2,			//	MP0CLK
  SMU12_SYSPLL0_MP1CLK_ID   = 3,			//	MP1CLK
  SMU12_SYSPLL0_MP2CLK_ID   = 4,			//	MP2CLK
  SMU12_SYSPLL0_VCLK_ID     = 5,			//	VCLK
  SMU12_SYSPLL0_LCLK_ID     = 6,			//	LCLK
  SMU12_SYSPLL0_DCLK_ID     = 7,			//	DCLK
  SMU12_SYSPLL0_ACLK_ID     = 8,			//	ACLK
  SMU12_SYSPLL0_ISPCLK_ID   = 9,			//	ISPCLK
  SMU12_SYSPLL0_SHUBCLK_ID  = 10,			//	SHUBCLK
};

enum atom_smu12_syspll1_clock_id {
  SMU12_SYSPLL1_DISPCLK_ID  = 0,      //	DISPCLK
  SMU12_SYSPLL1_DPPCLK_ID   = 1,      //	DPPCLK
  SMU12_SYSPLL1_DPREFCLK_ID = 2,      //	DPREFCLK
  SMU12_SYSPLL1_DCFCLK_ID   = 3,      //	DCFCLK
};

enum atom_smu12_syspll2_clock_id {
  SMU12_SYSPLL2_Pre_GFXCLK_ID = 0,   // Pre_GFXCLK
};

enum atom_smu12_syspll3_0_clock_id {
  SMU12_SYSPLL3_0_FCLK_ID = 0,      //	FCLK
};

enum atom_smu12_syspll3_1_clock_id {
  SMU12_SYSPLL3_1_UMCCLK_ID = 0,    //	UMCCLK
};

struct  atom_get_smu_clock_info_output_parameters_v3_1
{
  union {
    uint32_t smu_clock_freq_hz;
    uint32_t syspllvcofreq_10khz;
    uint32_t sysspllrefclk_10khz;
  }atom_smu_outputclkfreq;
};



/* 
  ***************************************************************************
              Structures used by dynamicmemorysettings
  *************************************************************************** 
*/   

enum atom_dynamic_memory_setting_command 
{
  COMPUTE_MEMORY_PLL_PARAM = 1,
  COMPUTE_ENGINE_PLL_PARAM = 2,
  ADJUST_MC_SETTING_PARAM = 3,
};

/* when command = COMPUTE_MEMORY_PLL_PARAM or ADJUST_MC_SETTING_PARAM */
struct dynamic_mclk_settings_parameters_v2_1
{
  uint32_t  mclk_10khz:24;         //Input= target mclk
  uint32_t  command:8;             //command enum of atom_dynamic_memory_setting_command
  uint32_t  reserved;
};

/* when command = COMPUTE_ENGINE_PLL_PARAM */
struct dynamic_sclk_settings_parameters_v2_1
{
  uint32_t  sclk_10khz:24;         //Input= target mclk
  uint32_t  command:8;             //command enum of atom_dynamic_memory_setting_command
  uint32_t  mclk_10khz;
  uint32_t  reserved;
};

union dynamic_memory_settings_parameters_v2_1
{
  struct dynamic_mclk_settings_parameters_v2_1 mclk_setting;
  struct dynamic_sclk_settings_parameters_v2_1 sclk_setting;
};



/* 
  ***************************************************************************
              Structures used by memorytraining
  *************************************************************************** 
*/   

enum atom_umc6_0_ucode_function_call_enum_id
{
  UMC60_UCODE_FUNC_ID_REINIT                 = 0,
  UMC60_UCODE_FUNC_ID_ENTER_SELFREFRESH      = 1,
  UMC60_UCODE_FUNC_ID_EXIT_SELFREFRESH       = 2,
};


struct memory_training_parameters_v2_1
{
  uint8_t ucode_func_id;
  uint8_t ucode_reserved[3];
  uint32_t reserved[5];
};


/* 
  ***************************************************************************
              Structures used by setpixelclock
  *************************************************************************** 
*/   

struct set_pixel_clock_parameter_v1_7
{
    uint32_t pixclk_100hz;               // target the pixel clock to drive the CRTC timing in unit of 100Hz. 

    uint8_t  pll_id;                     // ATOM_PHY_PLL0/ATOM_PHY_PLL1/ATOM_PPLL0
    uint8_t  encoderobjid;               // ASIC encoder id defined in objectId.h, 
                                         // indicate which graphic encoder will be used. 
    uint8_t  encoder_mode;               // Encoder mode: 
    uint8_t  miscinfo;                   // enum atom_set_pixel_clock_v1_7_misc_info
    uint8_t  crtc_id;                    // enum of atom_crtc_def
    uint8_t  deep_color_ratio;           // HDMI panel bit depth: enum atom_set_pixel_clock_v1_7_deepcolor_ratio
    uint8_t  reserved1[2];    
    uint32_t reserved2;
};

//ucMiscInfo
enum atom_set_pixel_clock_v1_7_misc_info
{
  PIXEL_CLOCK_V7_MISC_FORCE_PROG_PPLL         = 0x01,
  PIXEL_CLOCK_V7_MISC_PROG_PHYPLL             = 0x02,
  PIXEL_CLOCK_V7_MISC_YUV420_MODE             = 0x04,
  PIXEL_CLOCK_V7_MISC_DVI_DUALLINK_EN         = 0x08,
  PIXEL_CLOCK_V7_MISC_REF_DIV_SRC             = 0x30,
  PIXEL_CLOCK_V7_MISC_REF_DIV_SRC_XTALIN      = 0x00,
  PIXEL_CLOCK_V7_MISC_REF_DIV_SRC_PCIE        = 0x10,
  PIXEL_CLOCK_V7_MISC_REF_DIV_SRC_GENLK       = 0x20,
  PIXEL_CLOCK_V7_MISC_REF_DIV_SRC_REFPAD      = 0x30, 
  PIXEL_CLOCK_V7_MISC_ATOMIC_UPDATE           = 0x40,
  PIXEL_CLOCK_V7_MISC_FORCE_SS_DIS            = 0x80,
};

/* deep_color_ratio */
enum atom_set_pixel_clock_v1_7_deepcolor_ratio
{
  PIXEL_CLOCK_V7_DEEPCOLOR_RATIO_DIS          = 0x00,      //00 - DCCG_DEEP_COLOR_DTO_DISABLE: Disable Deep Color DTO 
  PIXEL_CLOCK_V7_DEEPCOLOR_RATIO_5_4          = 0x01,      //01 - DCCG_DEEP_COLOR_DTO_5_4_RATIO: Set Deep Color DTO to 5:4 
  PIXEL_CLOCK_V7_DEEPCOLOR_RATIO_3_2          = 0x02,      //02 - DCCG_DEEP_COLOR_DTO_3_2_RATIO: Set Deep Color DTO to 3:2 
  PIXEL_CLOCK_V7_DEEPCOLOR_RATIO_2_1          = 0x03,      //03 - DCCG_DEEP_COLOR_DTO_2_1_RATIO: Set Deep Color DTO to 2:1 
};

/* 
  ***************************************************************************
              Structures used by setdceclock
  *************************************************************************** 
*/   

// SetDCEClock input parameter for DCE11.2( ELM and BF ) and above 
struct set_dce_clock_parameters_v2_1
{
  uint32_t dceclk_10khz;                               // target DCE frequency in unit of 10KHZ, return real DISPCLK/DPREFCLK frequency. 
  uint8_t  dceclktype;                                 // =0: DISPCLK  =1: DPREFCLK  =2: PIXCLK
  uint8_t  dceclksrc;                                  // ATOM_PLL0 or ATOM_GCK_DFS or ATOM_FCH_CLK or ATOM_COMBOPHY_PLLx
  uint8_t  dceclkflag;                                 // Bit [1:0] = PPLL ref clock source ( when ucDCEClkSrc= ATOM_PPLL0 )
  uint8_t  crtc_id;                                    // ucDisp Pipe Id, ATOM_CRTC0/1/2/..., use only when ucDCEClkType = PIXCLK
};

//ucDCEClkType
enum atom_set_dce_clock_clock_type
{
  DCE_CLOCK_TYPE_DISPCLK                      = 0,
  DCE_CLOCK_TYPE_DPREFCLK                     = 1,
  DCE_CLOCK_TYPE_PIXELCLK                     = 2,        // used by VBIOS internally, called by SetPixelClock 
};

//ucDCEClkFlag when ucDCEClkType == DPREFCLK 
enum atom_set_dce_clock_dprefclk_flag
{
  DCE_CLOCK_FLAG_PLL_REFCLK_SRC_MASK          = 0x03,
  DCE_CLOCK_FLAG_PLL_REFCLK_SRC_GENERICA      = 0x00,
  DCE_CLOCK_FLAG_PLL_REFCLK_SRC_GENLK         = 0x01,
  DCE_CLOCK_FLAG_PLL_REFCLK_SRC_PCIE          = 0x02,
  DCE_CLOCK_FLAG_PLL_REFCLK_SRC_XTALIN        = 0x03,
};

//ucDCEClkFlag when ucDCEClkType == PIXCLK 
enum atom_set_dce_clock_pixclk_flag
{
  DCE_CLOCK_FLAG_PCLK_DEEPCOLOR_RATIO_MASK    = 0x03,
  DCE_CLOCK_FLAG_PCLK_DEEPCOLOR_RATIO_DIS     = 0x00,      //00 - DCCG_DEEP_COLOR_DTO_DISABLE: Disable Deep Color DTO 
  DCE_CLOCK_FLAG_PCLK_DEEPCOLOR_RATIO_5_4     = 0x01,      //01 - DCCG_DEEP_COLOR_DTO_5_4_RATIO: Set Deep Color DTO to 5:4 
  DCE_CLOCK_FLAG_PCLK_DEEPCOLOR_RATIO_3_2     = 0x02,      //02 - DCCG_DEEP_COLOR_DTO_3_2_RATIO: Set Deep Color DTO to 3:2 
  DCE_CLOCK_FLAG_PCLK_DEEPCOLOR_RATIO_2_1     = 0x03,      //03 - DCCG_DEEP_COLOR_DTO_2_1_RATIO: Set Deep Color DTO to 2:1 
  DCE_CLOCK_FLAG_PIXCLK_YUV420_MODE           = 0x04,
};

struct set_dce_clock_ps_allocation_v2_1
{
  struct set_dce_clock_parameters_v2_1 param;
  uint32_t ulReserved[2];
};


/****************************************************************************/   
// Structures used by BlankCRTC
/****************************************************************************/   
struct blank_crtc_parameters
{
  uint8_t  crtc_id;                   // enum atom_crtc_def
  uint8_t  blanking;                  // enum atom_blank_crtc_command
  uint16_t reserved;
  uint32_t reserved1;
};

enum atom_blank_crtc_command
{
  ATOM_BLANKING         = 1,
  ATOM_BLANKING_OFF     = 0,
};

/****************************************************************************/   
// Structures used by enablecrtc
/****************************************************************************/   
struct enable_crtc_parameters
{
  uint8_t crtc_id;                    // enum atom_crtc_def
  uint8_t enable;                     // ATOM_ENABLE or ATOM_DISABLE 
  uint8_t padding[2];
};


/****************************************************************************/   
// Structure used by EnableDispPowerGating
/****************************************************************************/   
struct enable_disp_power_gating_parameters_v2_1
{
  uint8_t disp_pipe_id;                // ATOM_CRTC1, ATOM_CRTC2, ...
  uint8_t enable;                     // ATOM_ENABLE or ATOM_DISABLE
  uint8_t padding[2];
};

struct enable_disp_power_gating_ps_allocation 
{
  struct enable_disp_power_gating_parameters_v2_1 param;
  uint32_t ulReserved[4];
};

/****************************************************************************/   
// Structure used in setcrtc_usingdtdtiming
/****************************************************************************/   
struct set_crtc_using_dtd_timing_parameters
{
  uint16_t  h_size;
  uint16_t  h_blanking_time;
  uint16_t  v_size;
  uint16_t  v_blanking_time;
  uint16_t  h_syncoffset;
  uint16_t  h_syncwidth;
  uint16_t  v_syncoffset;
  uint16_t  v_syncwidth;
  uint16_t  modemiscinfo;  
  uint8_t   h_border;
  uint8_t   v_border;
  uint8_t   crtc_id;                   // enum atom_crtc_def
  uint8_t   encoder_mode;			   // atom_encode_mode_def
  uint8_t   padding[2];
};


/****************************************************************************/   
// Structures used by processi2cchanneltransaction
/****************************************************************************/   
struct process_i2c_channel_transaction_parameters
{
  uint8_t i2cspeed_khz;
  union {
    uint8_t regindex;
    uint8_t status;                  /* enum atom_process_i2c_flag */
  } regind_status;
  uint16_t  i2c_data_out;
  uint8_t   flag;                    /* enum atom_process_i2c_status */
  uint8_t   trans_bytes;
  uint8_t   slave_addr;
  uint8_t   i2c_id;
};

//ucFlag
enum atom_process_i2c_flag
{
  HW_I2C_WRITE          = 1,
  HW_I2C_READ           = 0,
  I2C_2BYTE_ADDR        = 0x02,
  HW_I2C_SMBUS_BYTE_WR  = 0x04,
};

//status
enum atom_process_i2c_status
{
  HW_ASSISTED_I2C_STATUS_FAILURE     =2,
  HW_ASSISTED_I2C_STATUS_SUCCESS     =1,
};


/****************************************************************************/   
// Structures used by processauxchanneltransaction
/****************************************************************************/   

struct process_aux_channel_transaction_parameters_v1_2
{
  uint16_t aux_request;
  uint16_t dataout;
  uint8_t  channelid;
  union {
    uint8_t   reply_status;
    uint8_t   aux_delay;
  } aux_status_delay;
  uint8_t   dataout_len;
  uint8_t   hpd_id;                                       //=0: HPD1, =1: HPD2, =2: HPD3, =3: HPD4, =4: HPD5, =5: HPD6
};


/****************************************************************************/   
// Structures used by selectcrtc_source
/****************************************************************************/   

struct select_crtc_source_parameters_v2_3
{
  uint8_t crtc_id;                        // enum atom_crtc_def
  uint8_t encoder_id;                     // enum atom_dig_def
  uint8_t encode_mode;                    // enum atom_encode_mode_def
  uint8_t dst_bpc;                        // enum atom_panel_bit_per_color
};


/****************************************************************************/   
// Structures used by digxencodercontrol
/****************************************************************************/   

// ucAction:
enum atom_dig_encoder_control_action
{
  ATOM_ENCODER_CMD_DISABLE_DIG                  = 0,
  ATOM_ENCODER_CMD_ENABLE_DIG                   = 1,
  ATOM_ENCODER_CMD_DP_LINK_TRAINING_START       = 0x08,
  ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN1    = 0x09,
  ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN2    = 0x0a,
  ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN3    = 0x13,
  ATOM_ENCODER_CMD_DP_LINK_TRAINING_COMPLETE    = 0x0b,
  ATOM_ENCODER_CMD_DP_VIDEO_OFF                 = 0x0c,
  ATOM_ENCODER_CMD_DP_VIDEO_ON                  = 0x0d,
  ATOM_ENCODER_CMD_SETUP_PANEL_MODE             = 0x10,
  ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN4    = 0x14,
  ATOM_ENCODER_CMD_STREAM_SETUP                 = 0x0F, 
  ATOM_ENCODER_CMD_LINK_SETUP                   = 0x11, 
  ATOM_ENCODER_CMD_ENCODER_BLANK                = 0x12,
};

//define ucPanelMode
enum atom_dig_encoder_control_panelmode
{
  DP_PANEL_MODE_DISABLE                        = 0x00,
  DP_PANEL_MODE_ENABLE_eDP_MODE                = 0x01,
  DP_PANEL_MODE_ENABLE_LVLINK_MODE             = 0x11,
};

//ucDigId
enum atom_dig_encoder_control_v5_digid
{
  ATOM_ENCODER_CONFIG_V5_DIG0_ENCODER           = 0x00,
  ATOM_ENCODER_CONFIG_V5_DIG1_ENCODER           = 0x01,
  ATOM_ENCODER_CONFIG_V5_DIG2_ENCODER           = 0x02,
  ATOM_ENCODER_CONFIG_V5_DIG3_ENCODER           = 0x03,
  ATOM_ENCODER_CONFIG_V5_DIG4_ENCODER           = 0x04,
  ATOM_ENCODER_CONFIG_V5_DIG5_ENCODER           = 0x05,
  ATOM_ENCODER_CONFIG_V5_DIG6_ENCODER           = 0x06,
  ATOM_ENCODER_CONFIG_V5_DIG7_ENCODER           = 0x07,
};

struct dig_encoder_stream_setup_parameters_v1_5
{
  uint8_t digid;            // 0~6 map to DIG0~DIG6 enum atom_dig_encoder_control_v5_digid
  uint8_t action;           // =  ATOM_ENOCODER_CMD_STREAM_SETUP
  uint8_t digmode;          // ATOM_ENCODER_MODE_DP/ATOM_ENCODER_MODE_DVI/ATOM_ENCODER_MODE_HDMI
  uint8_t lanenum;          // Lane number     
  uint32_t pclk_10khz;      // Pixel Clock in 10Khz
  uint8_t bitpercolor;
  uint8_t dplinkrate_270mhz;//= DP link rate/270Mhz, =6: 1.62G  = 10: 2.7G, =20: 5.4Ghz, =30: 8.1Ghz etc
  uint8_t reserved[2];
};

struct dig_encoder_link_setup_parameters_v1_5
{
  uint8_t digid;           // 0~6 map to DIG0~DIG6 enum atom_dig_encoder_control_v5_digid
  uint8_t action;          // =  ATOM_ENOCODER_CMD_LINK_SETUP              
  uint8_t digmode;         // ATOM_ENCODER_MODE_DP/ATOM_ENCODER_MODE_DVI/ATOM_ENCODER_MODE_HDMI
  uint8_t lanenum;         // Lane number     
  uint8_t symclk_10khz;    // Symbol Clock in 10Khz
  uint8_t hpd_sel;
  uint8_t digfe_sel;       // DIG stream( front-end ) selection, bit0 means DIG0 FE is enable, 
  uint8_t reserved[2];
};

struct dp_panel_mode_set_parameters_v1_5
{
  uint8_t digid;              // 0~6 map to DIG0~DIG6 enum atom_dig_encoder_control_v5_digid
  uint8_t action;             // = ATOM_ENCODER_CMD_DPLINK_SETUP
  uint8_t panelmode;      // enum atom_dig_encoder_control_panelmode
  uint8_t reserved1;    
  uint32_t reserved2[2];
};

struct dig_encoder_generic_cmd_parameters_v1_5 
{
  uint8_t digid;           // 0~6 map to DIG0~DIG6 enum atom_dig_encoder_control_v5_digid
  uint8_t action;          // = rest of generic encoder command which does not carry any parameters
  uint8_t reserved1[2];    
  uint32_t reserved2[2];
};

union dig_encoder_control_parameters_v1_5
{
  struct dig_encoder_generic_cmd_parameters_v1_5  cmd_param;
  struct dig_encoder_stream_setup_parameters_v1_5 stream_param;
  struct dig_encoder_link_setup_parameters_v1_5   link_param;
  struct dp_panel_mode_set_parameters_v1_5 dppanel_param;
};

/* 
  ***************************************************************************
              Structures used by dig1transmittercontrol
  *************************************************************************** 
*/   
struct dig_transmitter_control_parameters_v1_6
{
  uint8_t phyid;           // 0=UNIPHYA, 1=UNIPHYB, 2=UNIPHYC, 3=UNIPHYD, 4= UNIPHYE 5=UNIPHYF
  uint8_t action;          // define as ATOM_TRANSMITER_ACTION_xxx
  union {
    uint8_t digmode;        // enum atom_encode_mode_def
    uint8_t dplaneset;      // DP voltage swing and pre-emphasis value defined in DPCD DP_LANE_SET, "DP_LANE_SET__xDB_y_zV"
  } mode_laneset;
  uint8_t  lanenum;        // Lane number 1, 2, 4, 8    
  uint32_t symclk_10khz;   // Symbol Clock in 10Khz
  uint8_t  hpdsel;         // =1: HPD1, =2: HPD2, .... =6: HPD6, =0: HPD is not assigned
  uint8_t  digfe_sel;      // DIG stream( front-end ) selection, bit0 means DIG0 FE is enable, 
  uint8_t  connobj_id;     // Connector Object Id defined in ObjectId.h
  uint8_t  reserved;
  uint32_t reserved1;
};

struct dig_transmitter_control_ps_allocation_v1_6
{
  struct dig_transmitter_control_parameters_v1_6 param;
  uint32_t reserved[4];
};

//ucAction
enum atom_dig_transmitter_control_action
{
  ATOM_TRANSMITTER_ACTION_DISABLE                 = 0,
  ATOM_TRANSMITTER_ACTION_ENABLE                  = 1,
  ATOM_TRANSMITTER_ACTION_LCD_BLOFF               = 2,
  ATOM_TRANSMITTER_ACTION_LCD_BLON                = 3,
  ATOM_TRANSMITTER_ACTION_BL_BRIGHTNESS_CONTROL   = 4,
  ATOM_TRANSMITTER_ACTION_LCD_SELFTEST_START      = 5,
  ATOM_TRANSMITTER_ACTION_LCD_SELFTEST_STOP       = 6,
  ATOM_TRANSMITTER_ACTION_INIT                    = 7,
  ATOM_TRANSMITTER_ACTION_DISABLE_OUTPUT          = 8,
  ATOM_TRANSMITTER_ACTION_ENABLE_OUTPUT           = 9,
  ATOM_TRANSMITTER_ACTION_SETUP                   = 10,
  ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH            = 11,
  ATOM_TRANSMITTER_ACTION_POWER_ON                = 12,
  ATOM_TRANSMITTER_ACTION_POWER_OFF               = 13,
};

// digfe_sel
enum atom_dig_transmitter_control_digfe_sel
{
  ATOM_TRANMSITTER_V6__DIGA_SEL                   = 0x01,
  ATOM_TRANMSITTER_V6__DIGB_SEL                   = 0x02,
  ATOM_TRANMSITTER_V6__DIGC_SEL                   = 0x04,
  ATOM_TRANMSITTER_V6__DIGD_SEL                   = 0x08,
  ATOM_TRANMSITTER_V6__DIGE_SEL                   = 0x10,
  ATOM_TRANMSITTER_V6__DIGF_SEL                   = 0x20,
  ATOM_TRANMSITTER_V6__DIGG_SEL                   = 0x40,
};


//ucHPDSel
enum atom_dig_transmitter_control_hpd_sel
{
  ATOM_TRANSMITTER_V6_NO_HPD_SEL                  = 0x00,
  ATOM_TRANSMITTER_V6_HPD1_SEL                    = 0x01,
  ATOM_TRANSMITTER_V6_HPD2_SEL                    = 0x02,
  ATOM_TRANSMITTER_V6_HPD3_SEL                    = 0x03,
  ATOM_TRANSMITTER_V6_HPD4_SEL                    = 0x04,
  ATOM_TRANSMITTER_V6_HPD5_SEL                    = 0x05,
  ATOM_TRANSMITTER_V6_HPD6_SEL                    = 0x06,
};

// ucDPLaneSet
enum atom_dig_transmitter_control_dplaneset
{
  DP_LANE_SET__0DB_0_4V                           = 0x00,
  DP_LANE_SET__0DB_0_6V                           = 0x01,
  DP_LANE_SET__0DB_0_8V                           = 0x02,
  DP_LANE_SET__0DB_1_2V                           = 0x03,
  DP_LANE_SET__3_5DB_0_4V                         = 0x08, 
  DP_LANE_SET__3_5DB_0_6V                         = 0x09,
  DP_LANE_SET__3_5DB_0_8V                         = 0x0a,
  DP_LANE_SET__6DB_0_4V                           = 0x10,
  DP_LANE_SET__6DB_0_6V                           = 0x11,
  DP_LANE_SET__9_5DB_0_4V                         = 0x18, 
};



/****************************************************************************/ 
// Structures used by ExternalEncoderControl V2.4
/****************************************************************************/   

struct external_encoder_control_parameters_v2_4
{
  uint16_t pixelclock_10khz;  // pixel clock in 10Khz, valid when ucAction=SETUP/ENABLE_OUTPUT 
  uint8_t  config;            // indicate which encoder, and DP link rate when ucAction = SETUP/ENABLE_OUTPUT  
  uint8_t  action;            // 
  uint8_t  encodermode;       // encoder mode, only used when ucAction = SETUP/ENABLE_OUTPUT
  uint8_t  lanenum;           // lane number, only used when ucAction = SETUP/ENABLE_OUTPUT  
  uint8_t  bitpercolor;       // output bit per color, only valid when ucAction = SETUP/ENABLE_OUTPUT and ucEncodeMode= DP
  uint8_t  hpd_id;        
};


// ucAction
enum external_encoder_control_action_def
{
  EXTERNAL_ENCODER_ACTION_V3_DISABLE_OUTPUT           = 0x00,
  EXTERNAL_ENCODER_ACTION_V3_ENABLE_OUTPUT            = 0x01,
  EXTERNAL_ENCODER_ACTION_V3_ENCODER_INIT             = 0x07,
  EXTERNAL_ENCODER_ACTION_V3_ENCODER_SETUP            = 0x0f,
  EXTERNAL_ENCODER_ACTION_V3_ENCODER_BLANKING_OFF     = 0x10,
  EXTERNAL_ENCODER_ACTION_V3_ENCODER_BLANKING         = 0x11,
  EXTERNAL_ENCODER_ACTION_V3_DACLOAD_DETECTION        = 0x12,
  EXTERNAL_ENCODER_ACTION_V3_DDC_SETUP                = 0x14,
};

// ucConfig
enum external_encoder_control_v2_4_config_def
{
  EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_MASK          = 0x03,
  EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_1_62GHZ       = 0x00,
  EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_2_70GHZ       = 0x01,
  EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_5_40GHZ       = 0x02,
  EXTERNAL_ENCODER_CONFIG_V3_DPLINKRATE_3_24GHZ       = 0x03,  
  EXTERNAL_ENCODER_CONFIG_V3_ENCODER_SEL_MAKS         = 0x70,
  EXTERNAL_ENCODER_CONFIG_V3_ENCODER1                 = 0x00,
  EXTERNAL_ENCODER_CONFIG_V3_ENCODER2                 = 0x10,
  EXTERNAL_ENCODER_CONFIG_V3_ENCODER3                 = 0x20,
};

struct external_encoder_control_ps_allocation_v2_4
{
  struct external_encoder_control_parameters_v2_4 sExtEncoder;
  uint32_t reserved[2];
};


/* 
  ***************************************************************************
                           AMD ACPI Table
  
  *************************************************************************** 
*/   

struct amd_acpi_description_header{
  uint32_t signature;
  uint32_t tableLength;      //Length
  uint8_t  revision;
  uint8_t  checksum;
  uint8_t  oemId[6];
  uint8_t  oemTableId[8];    //UINT64  OemTableId;
  uint32_t oemRevision;
  uint32_t creatorId;
  uint32_t creatorRevision;
};

struct uefi_acpi_vfct{
  struct   amd_acpi_description_header sheader;
  uint8_t  tableUUID[16];    //0x24
  uint32_t vbiosimageoffset; //0x34. Offset to the first GOP_VBIOS_CONTENT block from the beginning of the stucture.
  uint32_t lib1Imageoffset;  //0x38. Offset to the first GOP_LIB1_CONTENT block from the beginning of the stucture.
  uint32_t reserved[4];      //0x3C
};

struct vfct_image_header{
  uint32_t  pcibus;          //0x4C
  uint32_t  pcidevice;       //0x50
  uint32_t  pcifunction;     //0x54
  uint16_t  vendorid;        //0x58
  uint16_t  deviceid;        //0x5A
  uint16_t  ssvid;           //0x5C
  uint16_t  ssid;            //0x5E
  uint32_t  revision;        //0x60
  uint32_t  imagelength;     //0x64
};


struct gop_vbios_content {
  struct vfct_image_header vbiosheader;
  uint8_t                  vbioscontent[1];
};

struct gop_lib1_content {
  struct vfct_image_header lib1header;
  uint8_t                  lib1content[1];
};



/* 
  ***************************************************************************
                   Scratch Register definitions
  Each number below indicates which scratch regiser request, Active and 
  Connect all share the same definitions as display_device_tag defines
  *************************************************************************** 
*/   

enum scratch_register_def{
  ATOM_DEVICE_CONNECT_INFO_DEF      = 0,
  ATOM_BL_BRI_LEVEL_INFO_DEF        = 2,
  ATOM_ACTIVE_INFO_DEF              = 3,
  ATOM_LCD_INFO_DEF                 = 4,
  ATOM_DEVICE_REQ_INFO_DEF          = 5,
  ATOM_ACC_CHANGE_INFO_DEF          = 6,
  ATOM_PRE_OS_MODE_INFO_DEF         = 7,
  ATOM_PRE_OS_ASSERTION_DEF      = 8,    //For GOP to record a 32bit assertion code, this is enabled by default in prodution GOP drivers.
  ATOM_INTERNAL_TIMER_INFO_DEF      = 10,
};

enum scratch_device_connect_info_bit_def{
  ATOM_DISPLAY_LCD1_CONNECT           =0x0002,
  ATOM_DISPLAY_DFP1_CONNECT           =0x0008,
  ATOM_DISPLAY_DFP2_CONNECT           =0x0080,
  ATOM_DISPLAY_DFP3_CONNECT           =0x0200,
  ATOM_DISPLAY_DFP4_CONNECT           =0x0400,
  ATOM_DISPLAY_DFP5_CONNECT           =0x0800,
  ATOM_DISPLAY_DFP6_CONNECT           =0x0040,
  ATOM_DISPLAY_DFPx_CONNECT           =0x0ec8,
  ATOM_CONNECT_INFO_DEVICE_MASK       =0x0fff,
};

enum scratch_bl_bri_level_info_bit_def{
  ATOM_CURRENT_BL_LEVEL_SHIFT         =0x8,
#ifndef _H2INC
  ATOM_CURRENT_BL_LEVEL_MASK          =0x0000ff00,
  ATOM_DEVICE_DPMS_STATE              =0x00010000,
#endif
};

enum scratch_active_info_bits_def{
  ATOM_DISPLAY_LCD1_ACTIVE            =0x0002,
  ATOM_DISPLAY_DFP1_ACTIVE            =0x0008,
  ATOM_DISPLAY_DFP2_ACTIVE            =0x0080,
  ATOM_DISPLAY_DFP3_ACTIVE            =0x0200,
  ATOM_DISPLAY_DFP4_ACTIVE            =0x0400,
  ATOM_DISPLAY_DFP5_ACTIVE            =0x0800,
  ATOM_DISPLAY_DFP6_ACTIVE            =0x0040,
  ATOM_ACTIVE_INFO_DEVICE_MASK        =0x0fff,
};

enum scratch_device_req_info_bits_def{
  ATOM_DISPLAY_LCD1_REQ               =0x0002,
  ATOM_DISPLAY_DFP1_REQ               =0x0008,
  ATOM_DISPLAY_DFP2_REQ               =0x0080,
  ATOM_DISPLAY_DFP3_REQ               =0x0200,
  ATOM_DISPLAY_DFP4_REQ               =0x0400,
  ATOM_DISPLAY_DFP5_REQ               =0x0800,
  ATOM_DISPLAY_DFP6_REQ               =0x0040,
  ATOM_REQ_INFO_DEVICE_MASK           =0x0fff,
};

enum scratch_acc_change_info_bitshift_def{
  ATOM_ACC_CHANGE_ACC_MODE_SHIFT    =4,
  ATOM_ACC_CHANGE_LID_STATUS_SHIFT  =6,
};

enum scratch_acc_change_info_bits_def{
  ATOM_ACC_CHANGE_ACC_MODE          =0x00000010,
  ATOM_ACC_CHANGE_LID_STATUS        =0x00000040,
};

enum scratch_pre_os_mode_info_bits_def{
  ATOM_PRE_OS_MODE_MASK             =0x00000003,
  ATOM_PRE_OS_MODE_VGA              =0x00000000,
  ATOM_PRE_OS_MODE_VESA             =0x00000001,
  ATOM_PRE_OS_MODE_GOP              =0x00000002,
  ATOM_PRE_OS_MODE_PIXEL_DEPTH      =0x0000000C,
  ATOM_PRE_OS_MODE_PIXEL_FORMAT_MASK=0x000000F0,
  ATOM_PRE_OS_MODE_8BIT_PAL_EN      =0x00000100,
  ATOM_ASIC_INIT_COMPLETE           =0x00000200,
#ifndef _H2INC
  ATOM_PRE_OS_MODE_NUMBER_MASK      =0xFFFF0000,
#endif
};



/* 
  ***************************************************************************
                       ATOM firmware ID header file
              !! Please keep it at end of the atomfirmware.h !!
  *************************************************************************** 
*/   
#include "atomfirmwareid.h"
#pragma pack()

#endif

