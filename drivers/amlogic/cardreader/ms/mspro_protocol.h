#ifndef _H_MSPRO_PROTOCOL
#define _H_MSPRO_PROTOCOL

#pragma pack(1)
typedef struct _MSPRO_Registers  {	//Register Name                         //Addr  R/W
	unsigned char Reserved00;	//0x00  ---
	unsigned char INT_Reg;	//0x01  R
	unsigned char Status_Reg;	//0x02  R
	unsigned char Reserved03;	//0x03  ---
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
	unsigned char Data_Count_Reg1;	//0x11  W
	unsigned char Data_Count_Reg0;	//0x12  W
	union  {
		struct		//for data access
		{
			unsigned char Data_Address_Reg3;	//0x13  W
			unsigned char Data_Address_Reg2;	//0x14  W
			unsigned char Data_Address_Reg1;	//0x15  W
			unsigned char Data_Address_Reg0;	//0x16  W
			unsigned char TPC_Pamameter_Reg;	//0x17  W
			unsigned char CMD_Parameter_Reg;	//0x18  W
		} data;
		struct		//for security access
		{
			unsigned char Security_Parameter_Reg5;	//0x13  W
			unsigned char Security_Parameter_Reg4;	//0x14  W
			unsigned char Security_Parameter_Reg3;	//0x15  W
			unsigned char Security_Parameter_Reg2;	//0x16  W
			unsigned char Security_Parameter_Reg1;	//0x17  W
			unsigned char Security_Parameter_Reg0;	//0x18  W
		} security;
	} parameters;
	unsigned char Reserved19;	//0x19  ---
	unsigned char Reserved1A;	//0x1A  ---
	unsigned char Reserved1B;	//0x1B  ---
	unsigned char Reserved1C;	//0x1C  ---
	unsigned char Reserved1D;	//0x1D  ---
	unsigned char Reserved1E;	//0x1E  ---
	//unsigned char Reserved1F~ReservedFF               //0x1F~0xFF
} MSPRO_Registers_t;
typedef struct _MSPRO_Status_Register  {
	unsigned WP:1;		//D0: Write Protect
	unsigned SL:1;		//D1: Sleep
	unsigned Reserved1:1;	//D2
	unsigned Reserved2:1;	//D3
	unsigned Reserved3:1;	//D4
	unsigned Reserved4:1;	//D5
	unsigned Reserved5:1;	//D6
	unsigned ES:1;		//D7: Erase Status
} MSPRO_Status_Register_t;
typedef struct _MSPRO_System_Parameter_Register  {
	unsigned Reserved1:1;	//D0
	unsigned Reserved2:1;	//D1
	unsigned Reserved3:1;	//D2
	unsigned Reserved4:1;	//D3
	unsigned Reserved5:1;	//D4
	unsigned Reserved6:1;	//D5
	unsigned Reserved7:1;	//D6
	unsigned SRAC:1;	//D7: Serial Access
} MSPRO_System_Parameter_Register_t;
typedef struct _MSPRO_Attribute_Information_Area  {
	unsigned short Signature_Code;	//Setting: A5C3h
	unsigned short Version_Information;	//Setting: 01xxh
	unsigned char Device_Information_Entry_Count;	//1~12
	unsigned char Reserved[11];
} MSPRO_Attribute_Information_Area_t;
typedef struct _MSPRO_Device_Information_Entry  {
	unsigned long Address;	//Starting Location
	unsigned long Size;	//Size(by bytes)
	unsigned char Device_Information_ID;	//
	unsigned char Reserved[3];
} MSPRO_Device_Information_Entry_t;
typedef struct _MSPRO_System_Information	//big_endian
{
	unsigned char Memory_Stick_Class;	//2: Memory Stick Pro, other: reserved
	unsigned char Reserved0;	//Undefined value
	unsigned short Block_Size;	//Erase size(number of units)
	unsigned short Total_Blocks;	//Number of the whole block
	unsigned short User_Area_Blocks;	//Number of User Area blocks
	unsigned short Page_Size;	//Writing and reading size(number of units)
	unsigned short Reserved1;	//Undefined value
	struct  {
		unsigned char GMT_Difference;	//Time difference from GMT              
		unsigned short Year;	//Binary description  0xffff: No setting
		unsigned char Month;	//Binary description  0xff: No setting  
		unsigned char Day;	//Binary description  0xff: No setting  
		unsigned char Hour;	//Binary description  0xff: No setting  
		unsigned char Minute;	//Binary description  0xff: No setting  
		unsigned char Second;	//Binary description  0xff: No setting  
	} Assembly_Date;
	unsigned long Serial_Number;	//Product serial number                            
	unsigned char Assembly_Maker_Code;	//Assembly maker code                              
	unsigned char Assembly_Model_Code2;	//Assembly model code                              
	unsigned char Assembly_Model_Code1;	//Assembly model code                              
	unsigned char Assembly_Model_Code0;	//Assembly model code                              
	unsigned short Memory_Maker_Code;	//Memory maker code                                
	unsigned short Memory_Model_Code;	//Memory model code                                
	unsigned long Reserved2;	//Undefined value                                  
	unsigned char VCC;	//Expressed in 0.1V(VCC unit)ex3.3V 0x21           
	unsigned char VPP;	//Expressed in 0.1V(VPP unit)ex3.3V 0x21           
	unsigned short Controller_Number;	//Internal controller number                       
	unsigned short Controller_Function;	//Internal controller function                     
	unsigned short Start_Sector;	//Starting sector number of the block boundary     
	unsigned short Unit_Size;	//unit size(Number of bytes)                       
	unsigned char Memory_Stick_Sub_Class;	//Memory Stick sub class                           
	unsigned long Reserved3;	//Undefined value                                  
	unsigned char Interface_Type;	//0: serial, 1: Serial&Parallel, ohter: reserved   
	unsigned short Controller_Code;	//Internal controller code                         
	unsigned char Format_Type;	//1: FAT, ohter: reserved                          
	unsigned char Reserved4;	//Undefined value                                  
	unsigned char Device_Type;	//0: R/W, 1: ROM, 2: ROM2, 3: ROM3, other: reserved
	unsigned char Reserved5[7];	//Undefined value                                  
	unsigned char Memory_Stick_Pro_ID[16];	//Different ID is set on every Media               
	unsigned char Reserved6[16];	//Undefined value                                  
} MSPRO_System_Information_t;
typedef struct _MSPRO_Modal_Name	//big_endian
{
	char CardName[0x10];	//0x00~0x0F, "Memory Stick PRO"
	char MediaType[0x09];	//0x10~0x18, "-ROM Duo "
	char Capacity[0x08];	//0x18~0x20, "( 512MB)"
	char VendorStr[0x0F];	//0x21~0x2F
} MSPRO_Modal_Name_t;
typedef struct _MSPRO_MBR_Values	//little_endian
{
	unsigned char Default_boot_partition;
	unsigned char Start_head_No;
	unsigned char Start_sector_No;
	unsigned char Start_cylinder_No;
	unsigned char Partition_type;
	unsigned char End_head_No;
	unsigned char End_sector_No;
	unsigned char End_cylinder_No;
	unsigned long PBR_start_sectors;
	unsigned long Sectors_per_partition;
} MSPRO_MBR_Values_t;
typedef struct _MSPRO_PBR_FAT16_Values	//little_endian
{
	unsigned char Jmp_Code[3];
	char OEM_Name_Version[8];
	unsigned short Bytes_PerSector;
	unsigned char Sectors_PerCluster;
	unsigned short Reserved_Sectors;
	unsigned char FAT_Numbers;
	unsigned short Root_Directory_Entrys;
	unsigned short Total_Sectors;
	unsigned char Media_ID_Bytes;
	unsigned short FAT_Sectors;
	unsigned short Sectors_PerTrack;
	unsigned short Head_Numbers;
	unsigned long Hidden_Sectors;
	unsigned char Drive_Number;
	unsigned char PBR_Reserved;
	unsigned char EXT_Boot_Signature;
	unsigned long Volume_ID;
	char Volume_Label[11];
	char File_System_Type[8];
	unsigned char Reserved[2];
} MSPRO_PBR_FAT16_Values_t;
typedef struct _MSPRO_PBR_FAT32_Values	//little_endian
{
	unsigned char Jmp_Code[3];
	char OEM_Name_Version[8];
	unsigned short Bytes_PerSector;
	unsigned char Sectors_PerCluster;
	unsigned short Reserved_Sectors;
	unsigned char FAT_Numbers;
	unsigned short Root_Directory_Entrys;
	unsigned short Total_Sectors;
	unsigned char Media_ID_Bytes;
	unsigned short FAT_Sectors;
	unsigned short Sectors_PerTrack;
	unsigned short Head_Numbers;
	unsigned long Hidden_Sectors;
	unsigned long Huge_Sectors;
	unsigned long FAT_Numbers_Big;
	unsigned short Extended_Flag;
	unsigned short File_System_Version;
	unsigned long Root_Start_Cluster;
	unsigned short FSINFO_Start_Sector;
	unsigned short Boot_Copy_Start_Sector;
	unsigned char PBR_Reserved1[12];
	unsigned char Drive_Number;
	unsigned char PBR_Reserved2;
	unsigned char EXT_Boot_Signature;
	unsigned long Volume_ID;
	char Volume_Label[11];
	char File_System_Type[8];
	unsigned char Reserved[6];
} MSPRO_PBR_FAT32_Values_t;
typedef struct _MSPRO_Special_File_Values	//little_endian
{
	char File_Name[8];
	char Extention[3];
	unsigned char Attribute;
	unsigned char Reserved[10];
	unsigned short Modified_Time;
	unsigned short Modified_Date;
	unsigned short Starting_Cluster_Number;
	unsigned long File_Size;
} MSPRO_Special_File_Values_t;
typedef struct _MSPRO_Identify_Device_Information	//big_endian
{
	unsigned short Cylinder_Numbers;
	unsigned short Head_Numbers;
	unsigned short Unformatted_Bytes_PerTrack;
	unsigned short Unformatted_Bytes_PerSector;
	unsigned short Sectors_PerTrack;
	unsigned char Reserved[6];
} MSPRO_Identify_Device_Information_t;

#pragma pack()
    
#define MSPRO_SECTOR_SIZE                               512
    
#define MSPRO_DEVID_SYSTEM_INFORMATION                  0x10
#define MSPRO_DEVID_MODEL_NAME                          0x15
#define MSPRO_DEVID_MBR_VALUES                          0x20
#define MSPRO_DEVID_PBR_FAT16_VALUES                    0x21
#define MSPRO_DEVID_PBR_FAT32_VALUES                    0x22
#define MSPRO_DEVID_SPECIAL_VALUES1                     0x25
#define MSPRO_DEVID_SPECIAL_VALUES2                     0x26
#define MSPRO_DEVID_IDENTIFY_DEVICE_INFORMATION         0x30
#define MSPRO_DEVID_VENDOR_SPECIFICATION1               0x11
#define MSPRO_DEVID_VENDOR_SPECIFICATION2               0x12
    
#define MSRPO_ATTRIBUTE_INFOMATION_SIZE                 16
#define MSRPO_DEVICE_INFORMATION_ENTRY_SIZE             12
#define MSRPO_MAX_DEVICE_INFORMATION_ENTRY              12
#define MSPRO_SYSTEM_INFORMATION_SIZE                   96
#define MSPRO_MODAL_NAME_SIZE                           48
#define MSPRO_MBR_VALUES_SIZE                           16
#define MSPRO_PBR_FAT16_VALUES_SIZE                     64
#define MSPRO_PBR_FAT32_VALUES_SIZE                     96
#define MSPRO_SPECIAL_FILE_VALUES_SIZE                  32
#define MSPRO_IDENTIFY_DEVICE_INFORMATION_SIZE          16
    
//Acceptable Command List for Memory Stick Pro
#define CMD_MSPRO_READ_DATA                             0x20
#define CMD_MSPRO_WRITE_DATA                            0x21
#define CMD_MSPRO_READ_ATRB                             0x24
#define CMD_MSPRO_STOP                                  0x25
#define CMD_MSPRO_ERASE                                 0x26
#define CMD_MSPRO_SET_IBD                               0x46
#define CMD_MSPRO_GET_IBD                               0x47
#define CMD_MSPRO_FORMAT                                0x10
#define CMD_MSPRO_SLEEP                                 0x11
    
//definitions for progress report
#define MSPRO_REPORT_32BYTES                            0x00
#define MSPRO_REPORT_64BYTES                            0x01
#define MSPRO_REPORT_128BYTES                           0x02
#define MSPRO_REPORT_256BYTES                           0x03
    
#define MSPRO_REPORT_TYPE                               MSPRO_REPORT_32BYTES
    
#if (MSPRO_REPORT_TYPE==MSPRO_REPORT_32BYTES)
#define MSPRO_REPORT_SIZE                           32
#elif (MSPRO_REPORT_TYPE==MSPRO_REPORT_64BYTES)
#define MSPRO_REPORT_SIZE                           64
#elif (MSPRO_REPORT_TYPE==MSPRO_REPORT_128BYTES)
#define MSPRO_REPORT_SIZE                           128
#else				/*  */
#define MSPRO_REPORT_SIZE                           256
#endif				/*  */
    
//definitions for INT timeout
#define MSPRO_INT_TIMEOUT_READ_DATA                     (1000*TIMER_1MS)
#define MSPRO_INT_TIMEOUT_WRITE_DATA                    (1000*TIMER_1MS)
#define MSPRO_INT_TIMEOUT_READ_ATRB                     (1000*TIMER_1MS)
#define MSPRO_INT_TIMEOUT_STOP                          (1000*TIMER_1MS)
#define MSPRO_INT_TIMEOUT_ERASE                         (1000*TIMER_1MS)
#define MSPRO_INT_TIMEOUT_FORMAT                        (3000*TIMER_1MS)
#define MSPRO_INT_TIMEOUT_SLEEP                         (1000*TIMER_1MS)
#define MSPRO_INT_TIMEOUT_SET_IBD                       (1000*TIMER_1MS)
#define MSPRO_INT_TIMEOUT_GET_IBD                       (1000*TIMER_1MS)
    
#define MSPRO_SLEEP_TIMEOUT_PLUS                        (500*TIMER_1MS)
    
//#define MSPRO_STARTUP_TIMEOUT                         (1000*TIMER_1MS)
#define MSPRO_STARTUP_TIMEOUT                           300

#endif				//_H_MSPRO_PROTOCOL
