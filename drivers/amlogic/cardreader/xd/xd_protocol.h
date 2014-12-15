#ifndef _H_XD_PROTOCOL
#define _H_XD_PROTOCOL

//Never change any sequence of following data variables
#pragma pack(1)
typedef struct XD_Status2  {
	unsigned char Pass_Fail:1;
	unsigned char Dist0_P_F:1;
	unsigned char Dist1_P_F:1;
	unsigned char Dist2_P_F:1;
	unsigned char Dist3_P_F:1;
	unsigned char NotUsed:1;
	unsigned char Ready_Busy:1;
	unsigned char Write_Protect:1;
} XD_Status2_t;
typedef struct XD_ID_91  {
	unsigned char Characteristics_Code;
	unsigned char Vendor_Specific1;
	unsigned char Size_Code;
	unsigned char Vendor_Specific2;
	unsigned char Vendor_Specific3;
} XD_ID_91_t;

#define CELL_TYPE_SINGLE_LC						0
#define CELL_TYPE_2xMULTI_LC					1
#define CELL_TYPE_4xMULTI_LC					2
#define CELL_TYPE_8xMULTI_LC					4
    
#define x4_MULTI_BLOCK_NOT_SUPPORTED			0
#define x4_MULTI_BLOCK_SUPPORTED				2
typedef struct _Characteristics_Code  {
	unsigned char Vendor_Specific1:2;
	unsigned char Cell_Type:2;
	unsigned char x4_Multi_Block_Flag:2;
	unsigned char Vendor_Specific2:2;
} Characteristics_Code_t;
typedef struct XD_ID_9A  {
	unsigned char Vendor_Specific1;
	unsigned char Vendor_Specific2;
	unsigned char XD_CARD_ID;
	unsigned char Vendor_Specific3;
} XD_ID_9A_t;

#pragma pack()
    
//Busy Intervals of 16MB to 2 GB XD card
#define XD_BUSY_TIME_PROG						(1*TIMER_1MS)	//1000us, True Program Busy Time
#define XD_BUSY_TIME_BERASE						(10*TIMER_1MS)	//10ms, Block Erase Busy Time
#define XD_BUSY_TIME_DBSY						(1*TIMER_10US)	//10us, Dummy Program Busy Time during Multi Block Programming
#define XD_BUSY_TIME_MBPBSY						(1*TIMER_1MS)	//1000us, Multi Block Program Busy Time
#define XD_BUSY_TIME_R							(3*TIMER_10US)	//25us, Data Transfer Time(Cell to Register), 25us
#define XD_BUSY_TIME_RST_PROGRAM				(2*TIMER_10US)	//20us, Device Resetting Time: Program
#define XD_BUSY_TIME_RST_ERASE					(50*TIMER_10US)	//0.5ms, Device Resetting Time: Erase
#define XD_BUSY_TIME_RST_READ					(1*TIMER_10US)	//6us, Device Resetting Time: Read, 6us
#define XD_BUSY_TIME_RST						XD_BUSY_TIME_RST_ERASE
#define XD_BUSY_TIME_TCRY						(2*TIMER_10US)	//6+Tr(RY/-BY)us, -CE High to Ready
    
#define xd_card_init							xd_sm_init
#define xd_read_data							xd_sm_read_data
#define xd_write_data							xd_sm_write_data
int xd_check_insert(void);
void xd_power_on(void);
void xd_power_off(void);
void xd_prepare_init(void);
void xd_io_config(void);
void enc_trans_end_ctl(void);
void xd_cmd_input_cycle(unsigned char cmd, int enable_write);
void xd_addr_input_cycle(unsigned long addr, int cycles);
void xd_data_input_cycle(unsigned char *data_buf, unsigned long data_cnt,
			  unsigned char *redundant_buf,
			  unsigned long redundant_cnt);
void xd_serial_read_cycle(unsigned char *data_buf, unsigned long data_cnt,
			   unsigned char *redundant_buf,
			   unsigned long redundant_cnt);
int xd_test_ready(void);
int xd_card_capacity_determin(unsigned char device_code);

//Read data from XD/SM card
int xd_sm_read_data(unsigned long lba, unsigned long byte_cnt,
		    unsigned char *data_buf);

//Write data to XD/SM card
int xd_sm_write_data(unsigned long lba, unsigned long byte_cnt,
		     unsigned char *data_buf);

#endif				//_H_XD_PROTOCOL
