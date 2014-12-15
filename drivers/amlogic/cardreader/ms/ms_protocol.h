#ifndef _H_MS_PROTOCOL
#define _H_MS_PROTOCOL

#pragma pack(1)
typedef struct _MS_Registers  {	//Register Name                         //Addr  R/W
	unsigned char Reserved00;	//0x00  ---
	unsigned char INT_Reg;	//0x01  R
	unsigned char Status_Reg0;	//0x02  R
	unsigned char Status_Reg1;	//0x03  R
	unsigned char Type_Reg;	//0x04  R
	unsigned char Reserved05;	//0x05  ---
	unsigned char Category_Reg;	//0x06  R
	unsigned char Class_Reg;	//0x07  R
	unsigned char Reserved08;	//0x08  ---
	unsigned char Reserved09;	//0x09  ---
	unsigned char Reserved0A;	//0x0A  ---
	unsigned char Reserved0B;	//0x0B  ---
	unsigned char Reserved0C;	//0x0C  ---
	unsigned char Reserved0D;	//0x0D  ---
	unsigned char Reserved0E;	//0x0E  ---
	unsigned char Reserved0F;	//0x0F  ---
	unsigned char System_Parameter_Reg;	//0x10  W
	unsigned char Block_Address_Reg2;	//0x11  W
	unsigned char Block_Address_Reg1;	//0x12  W
	unsigned char Block_Address_Reg0;	//0x13  W
	unsigned char CMD_Parameter_Reg;	//0x14  W
	unsigned char Page_Address_Reg;	//0x15  R/W
	unsigned char Overwrite_Flag_Reg;	//0x16  R/W
	unsigned char Management_Flag_Reg;	//0x17  R/W
	unsigned char Logical_Address_Reg1;	//0x18  R/W
	unsigned char Logical_Address_Reg0;	//0x19  R/W
	unsigned char Reserved1A;	//0x1A  R/W
	unsigned char Reserved1B;	//0x1B  R/W
	unsigned char Reserved1C;	//0x1C  R/W
	unsigned char Reserved1D;	//0x1D  R/W
	unsigned char Reserved1E;	//0x1E  R/W
	//unsigned char Reserved1F~ReservedFF               //0x1F~0xFF
} MS_Registers_t;
typedef struct _MS_Status_Register0  {
	unsigned WP:1;		//D0: Write Protect
	unsigned SL:1;		//D1: Sleep
	unsigned Reserved1:1;	//D2
	unsigned Reserved2:1;	//D3
	unsigned BF:1;		//D4: Buffer Full
	unsigned BE:1;		//D5: Buffer Empty
	unsigned FB0:1;		//D6: Flash Busy 0
	unsigned MB:1;		//D7: Media Busy
} MS_Status_Register0_t;
typedef struct _MS_Status_Register1  {
	unsigned UCFG:1;	//D0: Unable to Correct Flag
	unsigned FGER:1;	//D1: Flag Error
	unsigned UCEX:1;	//D2: Unable to Correct Extra Data
	unsigned EXER:1;	//D3: Extra Data Error
	unsigned UCDT:1;	//D4: Unable to Correct Data
	unsigned DTER:1;	//D5: Data Error
	unsigned FB1:1;		//D6: Flash Busy 1
	unsigned MB:1;		//D7: Media Busy
} MS_Status_Register1_t;
typedef struct _MS_System_Parameter_Register  {
	unsigned Reserved1:1;	//D0
	unsigned Reserved2:1;	//D1
	unsigned Reserved3:1;	//D2
	unsigned PAM:1;		//D3: Parallel Access Mode
	unsigned Reserved4:1;	//D4
	unsigned Reserved5:1;	//D5
	unsigned Reserved6:1;	//D6
	unsigned Reserved7:1;	//D7: 1
} MS_System_Parameter_Register_t;
typedef struct _MS_Overwrite_Flag_Register  {
	unsigned Reserved1:1;	//D0
	unsigned Reserved2:1;	//D1
	unsigned Reserved3:1;	//D2
	unsigned Reserved4:1;	//D3
	unsigned UDST:1;	//D4: Update Status
	unsigned PGST1:1;	//D5: Page Status
	unsigned PGST0:1;	//D6: Page Status
	unsigned BKST:1;	//D7: Block Status
} MS_Overwrite_Flag_Register_t;
typedef struct _MS_Management_Flag_Register  {
	unsigned Reserved1:1;	//D0
	unsigned Reserved2:1;	//D1
	unsigned SYSFLG:1;	//D2: System bit
	unsigned ATFLG:1;	//D3: Conversion table bit
	unsigned SCMS1:1;	//D4: Digital Read Protectd bit
	unsigned SCMS0:1;	//D5: Digital Read Protectd bit
	unsigned Reserved3:1;	//D6
	unsigned Reserved4:1;	//D7
} MS_Management_Flag_Register_t;
typedef struct MS_Boot_Header	//big_endian
{
	unsigned short Block_ID;	//0x0001: Boot Block ID
	unsigned char Data_Entry;	//2: Number of effective blocks in a Boot Block (Disabled Block Data and CIS/IDI)
} MS_Boot_Header_t;
typedef struct MS_Boot_System_Entry	//big_endian
{
	unsigned long Disabled_Block_Start_Address;	//0x00000000: Disabled Block Data start location
	unsigned long Disabled_Block_Data_Size;	//(Disabled number of blocks * 2)Bytes
	unsigned char Disabled_Block_Data_Type_ID;	//0x01: Disabled Block Data
	unsigned char Reserved0[3];	//All 0x00
	unsigned long CIS_IDI_Start_Address;	//0x00000200: CIS/IDI data start location
	unsigned long CIS_IDI_Data_Size;	//0x00000200: Data Size (512[Bytes], 1page)
	unsigned char CIS_IDI_Data_Type_ID;	//0x0a
	unsigned char Reserved1[27];	//All 0x00
} MS_Boot_System_Entry_t;
typedef struct MS_Boot_Attribute_Information	//big_endian
{
	unsigned char Memory_Stick_Class;	//1: ver1.xx, other: reserved
	unsigned char Format_Unique_Value1;	//Set to 2
	unsigned short Block_Size;	//16[kb]:0x10, 8[kb]:0x08, other: reserved
	unsigned short Block_Numbers;	//4MB:0x0200, 8MB:0x0400, 16MB:0x0400, 32MB:0x0800, 64MB:0x1000, 128MB:0x2000
	unsigned short Effective_Block_Numbers;	//4MB:0x01f0, 8MB:0x03e0, 16MB:0x03e0, 32MB:0x07c0, 64MB:0x0f80, 128MB:0x1f00
	unsigned short Page_Size;	//0x0200(512) fixed
	unsigned char Extra_Data_Area_Size;	//0x10(16) fixed
	unsigned char Format_Unique_Value2;	//Set to 1
	struct  {
		unsigned char TD;	//Can be set in unite of 15mins from 12 to -12 hours, negative numbers are expressed in complement. 0x80: No setting
		unsigned short AD;	//Binary description  0xffff: No setting
		unsigned char Month;	//Binary description  0xff: No setting
		unsigned char Day;	//Binary description  0xff: No setting
		unsigned char Hour;	//Binary description  0xff: No setting
		unsigned char Minute;	//Binary description  0xff: No setting
		unsigned char Second;	//Binary description  0xff: No setting
	} Assembly_Date_Time;
	unsigned char Format_Unique_Value3;	//Set to 0
	unsigned char Serial_Number2;	//The number shall not be
	unsigned char Serial_Number1;	//set duplicated for Memory Sticks
	unsigned char Serial_Number0;	//manufactured on the same day
	unsigned char Assembly_Manufacturer_Code;	//Setting values shall be obtained from the licensor
	unsigned char Assembly_Model_Code2;	//4MB: 0x00, 8MB: 0x00, 16MB: 0x00, 32MB: 0x00, 64MB: 0x00, 128MB: 0x01
	unsigned char Assembly_Model_Code1;	//4MB: 0x04, 8MB: 0x08, 16MB: 0x16, 32MB: 0x32, 64MB: 0x64, 128MB: 0x28
	unsigned char Assembly_Model_Code0;	//4MB: 0x00, 8MB: 0x00, 16MB: 0x02, 32MB: 0x00, 64MB: 0x00, 128MB: 0x00
	unsigned short Memory_Manufacturer_Code;	//0: Unknown, other: reserved
	unsigned short Memory_Device_Code;	//0: Unknown, other: reserved
	unsigned short Implemented_Capacity;	//describe total MB
	unsigned char Format_Unique_Value4;	//Set to 1
	unsigned char Format_Unique_Value5;	//Set to 1
	unsigned char VCC;	//Expressed in 0.1V(VCC unit)ex3.3V 0x21
	unsigned char VPP;	//Expressed in 0.1V(VPP unit)ex3.3V 0x21
	unsigned short Controller_Number;	//Controller chip number
	unsigned short Controller_Function;	//0x1001: MG, other: reserved
	unsigned char Reserved0[9];	//
	unsigned char Parallel_Transfer_Supporting;	//0: serial, 1: Serial&Parallel, ohter: reserved
	unsigned short Format_Unique_Value6;	//Set to 0
	unsigned char Format_Type;	//1: FAT, ohter: reserved
	unsigned char Memory_Stick_Application;	//0: General purpose, ohter: reserved
	unsigned char Device_Type;	//0: Memory Stick, 1: ROM, 2: ROM2, 3: ROM3, other: reserved
	unsigned char Reserved1[22];	//
	unsigned char Format_Unique_Value7;	//Set to 0x0a(10)
	unsigned char Format_Unique_Value8;	//Set to 1
	unsigned char Reserved2[15];	//
} MS_Boot_Attribute_Information_t;
typedef struct MS_Boot_CIS	//big_endian
{
	unsigned char CISTPL_DEVICE[6];
	unsigned char CISTPL_DEVICEOC[6];
	unsigned char CISTPL_JEDEC_C[6];
	unsigned char CISTPL_MANFID[6];
	unsigned char CISTPL_VERS_1[32];
	unsigned char CISTPL_FUNID[4];
	unsigned char CISTPL_FUNCE1[4];
	unsigned char CISTPL_FUNCE2[5];
	unsigned char CISTPL_CONFIG[7];
	unsigned char CISTPL_CFTABL_ENTRY1[10];
	unsigned char CISTPL_CFTABL_ENTRY2[8];
	unsigned char CISTPL_CFTABL_ENTRY3[12];
	unsigned char CISTPL_CFTABL_ENTRY4[8];
	unsigned char CISTPL_CFTABL_ENTRY5[17];
	unsigned char CISTPL_CFTABL_ENTRY6[8];
	unsigned char CISTPL_CFTABL_ENTRY7[17];
	unsigned char CISTPL_CFTABL_ENTRY8[8];
	unsigned char CISTPL_NO_LINK[2];
	unsigned char CISTPL_END[1];
	unsigned char Reserved[90];
} MS_Boot_CIS_t;
typedef struct MS_Boot_IDI	//little_endian
{
	unsigned short General_Configuration;
	unsigned short Logical_Cylinder_Numbers;
	unsigned short Reserved0;
	unsigned short Logical_Head_Numbers;
	unsigned short Logical_Bytes_PerTrack;
	unsigned short Logical_Bytes_PerSector;
	unsigned short Logical_Sectors_PerTrack;
	unsigned short Logical_Sector_Numbers_MSW;
	unsigned short Logical_Sector_Numbers_LSW;
	unsigned short Reserved1;;
	unsigned char Serial_Number[20];
	unsigned short Buffer_Type;
	unsigned short Buffer_Size_By512Bytes;
	unsigned short Long_Command_ECC;
	unsigned char Firmware_Version[8];
	unsigned char Model_Name[40];
	unsigned short Reserved2;
	unsigned short Dual_Word_Supported;
	unsigned short DMA_Transfer_Support;
	unsigned short Reserved3;
	unsigned short PIO_Mode_Number;
	unsigned short DMA_Mode_Number;
	unsigned short Field_Validity;
	unsigned short Current_Logical_Cylinders;
	unsigned short Current_Logical_Heads;
	unsigned short Current_Logical_Sectors_PerTrack;
	unsigned long Current_Sectors_Capacity;
	unsigned short Multi_Sector_Setting;
	unsigned long User_Addressable_Sectors;
	unsigned short Single_Word_DMA_Transfer;
	unsigned short Multi_Word_DMA_Transfer;
	unsigned char Reserved4[128];
} MS_Boot_IDI_t;

#pragma pack()
    
#define MS_PAGE_SIZE                                    512
#define MS_MAX_DISABLED_BLOCKS                          MS_PAGE_SIZE/2
typedef struct _MS_Disabled_Block_Data  {
	unsigned short disabled_block_nums;
	unsigned short disabled_block_table[MS_MAX_DISABLED_BLOCKS];
} MS_Disabled_Block_Data_t;

#define MS_BOOT_HEADER_SIZE                             368
#define MS_BOOT_SYSTEM_ENTRY_SIZE                       48
#define MS_BOOT_ATTRIBUTE_INFORMATION_SIZE              96
#define MS_BOOT_CIS_SIZE                                256
#define MS_BOOT_IDI_SIZE                                256
    
#define MS_BLOCKS_PER_SEGMENT                           512
#define MS_LOGICAL_SIZE_PER_SEGMENT                     496
#define MS_MAX_FREE_BLOCKS_PER_SEGMENT                  16
#define MS_MAX_SEGMENT_NUMBERS                          16
    
//Acceptable Command List for Memory Stick
#define CMD_MS_BLOCK_READ                               0xAA
#define CMD_MS_BLOCK_WRITE                              0x55
#define CMD_MS_BLOCK_END                                0x33
#define CMD_MS_BLOCK_ERASE                              0x99
#define CMD_MS_FLASH_STOP                               0xCC
#define CMD_MS_SLEEP                                    0x5A
#define CMD_MS_CLEAR_BUFFER                             0xC3
#define CMD_MS_RESET                                    0x3C
    
//definitions for INT timeout
#define MS_INT_TIMEOUT_BLOCK_READ                       (5*TIMER_1MS)
#define MS_INT_TIMEOUT_BLOCK_WRITE                      (10*TIMER_1MS)
#define MS_INT_TIMEOUT_BLOCK_END                        (5*TIMER_1MS)
#define MS_INT_TIMEOUT_ERASE                            (100*TIMER_1MS)
#define MS_INT_TIMEOUT_FLASH_STOP                       (5*TIMER_1MS)
#define MS_INT_TIMEOUT_SLEEP                            (1*TIMER_1MS)
#define MS_INT_TIMEOUT_CLEAR_BUF                        (1*TIMER_1MS)
    
//#define MS_WRITE_PATTERN_1
#define MS_WRITE_PATTERN_2


#endif				//_H_MS_PROTOCOL
