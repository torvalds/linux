#ifndef _H_XD_SM
#define _H_XD_SM

#include "xd_protocol.h"
//#include "sm_protocol.h"
    
//Never change any sequence of following data variables
#pragma pack(1)
    
//XD status bit definitions
#define XD_SM_STATUS_PASS						0
#define XD_SM_STATUS_FAIL						1
#define XD_SM_STATUS_BUSY						0
#define XD_SM_STATUS_READY						1
#define XD_SM_STATUS_PROTECTED					0
#define XD_SM_STATUS_NOT_PROTECTED				1
typedef struct XD_SM_Status1  {
	unsigned char Pass_Fail:1;
	unsigned char Reserved1:1;
	unsigned char Reserved2:1;
	unsigned char Reserved3:1;
	unsigned char Reserved4:1;
	unsigned char Reserved5:1;
	unsigned char Ready_Busy:1;
	unsigned char Write_Protect:1;
} XD_SM_Status1_t;
typedef struct XD_SM_ID_90  {
	unsigned char Maker_Code;
	unsigned char Device_Code;
	unsigned char Option_Code1;
	unsigned char Option_Code2;
} XD_SM_ID_90_t;

#define CIS_FIELD1_OFFSET						0
#define IDI_FIELD1_OFFSET						128
#define CIS_FIELD2_OFFSET						256
#define IDI_FIELD2_OFFSET						384
#define STORING_AREA1_OFFSET					0
#define STORING_AREA2_OFFSET					256
    
#define DATA_STATUS_VALID						0xFF
#define DATA_STATUS_INVALID						0x00
//if four or more bits are "0", should be assumed to be synonymous with "00h"
    
#define BLOCK_STATUS_NORMAL						0xFF
#define BLOCK_STATUS_INITIALLY_DEFECTIVE		0x00
#define BLOCK_STATUS_MARKED_DEFECTIVE			0xF0
//if two or more bits are "0", should be judged to be a defective block
    
//D7  D6  D5  D4  D3  D2  D1  D0        512+16 Bytes/page
//0   0   0   1   0   BA9 BA8 BA7       518,523 Bytes
//BA6 BA5 BA4 BA3 BA2 BA1 BA0 P         519,524 Bytes
//P: Even parity bit
typedef struct XD_SM_Redundant_Area  {
	unsigned long Reserved;
	unsigned char Data_Status_Flag;
	unsigned char Block_Status_Flag;
	unsigned char Block_Address1[2];
	unsigned char ECC2[3];
	unsigned char Block_Address2[2];
	unsigned char ECC1[3];
} XD_SM_Redundant_Area_t;
extern unsigned char CIS_DATA_0_9[];

#define CIS_MANUFACTURE_NAME_OFFSET				0x59
#define CIS_PRODUCT_NAME_OFFSET					0x61
#define CIS_PRODUCT_VERSION_OFFSET				0x66
    
#pragma pack()
    
#define ZONE_NUMS_16MB							1
#define ZONE_NUMS_32MB							2
#define ZONE_NUMS_64MB							4
#define ZONE_NUMS_128MB							8
#define ZONE_NUMS_256MB							16
#define ZONE_NUMS_512MB							32
#define ZONE_NUMS_1GB							64
#define ZONE_NUMS_2GB							128
#define ZONE_NUMS_4GB							256
#define ZONE_NUMS_8GB							512
    
#define XD_SM_SECTOR_SIZE						512
#define MAX_REDUNDANT_SIZE						16
#define MAX_PAGES_PER_BLOCK						32
#define MAX_PHYSICAL_BLKS_PER_ZONE				1024
#define MAX_LOGICAL_BLKS_PER_ZONE				1000
#define MAX_SUPPORTED_ZONES						ZONE_NUMS_1GB
    
#define INVALID_BLOCK_ADDRESS					0xFFFF
    
#define XD_SM_WRITE_DISABLED					0
#define XD_SM_WRITE_ENABLED						1
    
#define ECC_NO_ERROR							0
#define ECC_ERROR_CORRECTED						1
#define ECC_ERROR_ECC							2
#define ECC_ERROR_UNCORRECTABLE					3
#define ECC_ERROR_UNKOWN	//
    
//Busy Intervals of 1MB to 256MB SM card
#define BUSY_TIME_PROG							(100*TIMER_1MS)	// xd: 1ms,   sm: 20ms
#define BUSY_TIME_BERASE						(400*TIMER_1MS)	// xd: 10ms,  sm: 400ms
#define BUSY_TIME_R								(10*TIMER_1MS)	// xd: 25us,  sm: 25us
#define BUSY_TIME_RST							(6*TIMER_1MS)	// xd: 0.5ms, sm: 6ms
#define BUSY_TIME_TCRY							(1*TIMER_1MS)
    
/* Error codes */ 
typedef enum XD_SM_Error_Status_t 
    { XD_SM_NO_ERROR =
0, XD_SM_ERROR_DRIVER_FAILURE, XD_SM_ERROR_BUSY, XD_SM_ERROR_TIMEOUT, XD_SM_ERROR_PARAMETER,
	XD_SM_ERROR_ECC, XD_SM_ERROR_BLOCK_ADDRESS,
	XD_SM_ERROR_PHYSICAL_FORMAT,
	XD_SM_ERROR_CONVERTSION_TABLE, XD_SM_ERROR_UNSUPPORTED_CAPACITY,
	XD_SM_ERROR_DEVICE_ID,
	XD_SM_ERROR_CARD_ID, XD_SM_ERROR_LOGICAL_FORMAT,
	XD_SM_ERROR_NO_FREE_BLOCK,
	XD_SM_ERROR_BLOCK_ERASE, XD_SM_ERROR_COPY_PAGE,
	XD_SM_ERROR_PAGE_PROGRAM, XD_SM_ERROR_DATA,
	XD_SM_ERROR_CARD_TYPE, XD_SM_ERROR_NO_MEMORY 
} XD_SM_Error_Status_t;
typedef enum XD_SM_Card_Type 
    { CARD_TYPE_NONE = 0, CARD_TYPE_XD, CARD_TYPE_SM 
} XD_SM_Card_Type_t;
typedef struct XD_SM_Card_Buffer  {
	
	    //unsigned long total_zones;
	    //unsigned long totoal_physical_blks;
	    //unsigned long totoal_logical_blks;
	    //unsigned short physical_blks_perzone;
	    //unsigned short logical_blks_perzone;
	    //unsigned short pages_per_blk;
	    //unsigned short page_size;
	    //unsigned short redundant_size;
	int mask_rom_flag;
	int addr_cycles;
	unsigned short cis_block_no;
	unsigned short cis_search_max;
	char *capacity_str;
	unsigned char page_buf[XD_SM_SECTOR_SIZE];
	unsigned char redundant_buf[MAX_PAGES_PER_BLOCK * MAX_REDUNDANT_SIZE];
	
#ifdef XD_SM_NUM_POINTER	
	unsigned short (*logical_physical_table)[MAX_LOGICAL_BLKS_PER_ZONE];
	unsigned char (*free_block_table)[MAX_PHYSICAL_BLKS_PER_ZONE / 8];
	
#else				/*  */
	    unsigned short
	 logical_physical_table[MAX_SUPPORTED_ZONES]
	    [MAX_LOGICAL_BLKS_PER_ZONE];
	unsigned char
	 free_block_table[MAX_SUPPORTED_ZONES][MAX_PHYSICAL_BLKS_PER_ZONE / 8];
	
#endif				/*  */
} XD_SM_Card_Buffer_t;
typedef struct XD_SM_Card_Info  {
	XD_SM_Card_Type_t card_type;
	unsigned long blk_len;
	unsigned long blk_nums;
	int read_only_flag;
	int xd_inited_flag;
	int xd_removed_flag;
	int xd_init_retry;
	int sm_inited_flag;
	int sm_removed_flag;
	int sm_init_retry;
	XD_ID_9A_t raw_cid;
	void (*xd_power) (int power_on);
	int (*xd_get_ins) (void);
	void (*xd_io_release) (void);
	void (*sm_power) (int power_on);
	int (*sm_get_ins) (void);
	int (*sm_get_wp) (void);
	void (*sm_io_release) (void);
} XD_SM_Card_Info_t;

#define XD_Card_Info_t							XD_SM_Card_Info_t
#define SM_Card_Info_t							XD_SM_Card_Info_t
extern unsigned long xd_sm_total_zones;
extern unsigned long xd_sm_totoal_physical_blks;
extern unsigned long xd_sm_totoal_logical_blks;
extern unsigned short xd_sm_physical_blks_perzone;
extern unsigned short xd_sm_logical_blks_perzone;
extern unsigned short xd_sm_pages_per_blk;
extern unsigned short xd_sm_page_size;
extern unsigned short xd_sm_redundant_size;
extern unsigned short xd_sm_actually_zones;
extern XD_Card_Info_t *xd_sm_info;
extern XD_SM_Card_Buffer_t *xd_sm_buf;
extern char xd_sm_capacity_1MB[];
extern char xd_sm_capacity_2MB[];
extern char xd_sm_capacity_4MB[];
extern char xd_sm_capacity_8MB[];
extern char xd_sm_capacity_16MB[];
extern char xd_sm_capacity_32MB[];
extern char xd_sm_capacity_64MB[];
extern char xd_sm_capacity_128MB[];
extern char xd_sm_capacity_256MB[];
extern char xd_sm_capacity_512MB[];
extern char xd_sm_capacity_1GB[];
extern char xd_sm_capacity_2GB[];
 /**/ typedef void (*xd_sm_io_config_t) (void);
 /**/ typedef void (*xd_sm_cmd_input_cycle_t) (unsigned char cmd,
						int enable_write);
 /**/ typedef void (*xd_sm_addr_input_cycle_t) (unsigned long addr,
						 int cycles);
 /**/ typedef void (*xd_sm_data_input_cycle_t) (unsigned char *data_buf,
						 unsigned long data_cnt,
						 unsigned char *redundant_buf,
						 unsigned long redundant_cnt);
 /**/ typedef void (*xd_sm_serial_read_cycle_t) (unsigned char *data_buf,
						  unsigned long data_cnt,
						  unsigned char *redundant_buf,
						  unsigned long redundant_cnt);
 /**/ typedef int (*xd_sm_test_ready_t) (void);

//XD Operation Command Table                                    1st                       2nd                   Busy
#define XD_SM_SERIAL_DATA_INPUT					0x80
#define XD_SM_READ1								0x00
#define XD_SM_READ2								0x01
#define XD_SM_READ3								0x50
#define XD_SM_RESET								0xFF	//Yes
#define XD_SM_TRUE_PAGE_PROGRAM					0x10
#define XD_TRUE_DUMMY_PROGRAM					0x11
#define XD_MULTI_BLOCK_PROGRAM					0x15
#define XD_SM_BLOCK_ERASE_SETUP					0x60	//0xD0
#define XD_SM_BLOCK_ERASE_EXECUTE				0xD0
#define XD_SM_STATUS_READ1						0x70	//Yes
#define XD_STATUS_READ2							0x71	//Yes
#define XD_SM_ID_READ90							0x90
#define XD_SM_ID_READ91							0x91
#define XD_ID_READ9A							0x9A
    
#define XD_SM_INIT_RETRY						3
    
//Following functions are the API used for outside routine
    
//XD/SM Initialization...
int xd_sm_init(XD_Card_Info_t * card_info);
void xd_exit(void);
void sm_exit(void);

//Check if any card is inserted
//int xd_sm_check_insert();
    
//XD/SM Power on/off
void xd_sm_power_on(void);
void xd_sm_power_off(void);

#endif				//_H_XD_SM
