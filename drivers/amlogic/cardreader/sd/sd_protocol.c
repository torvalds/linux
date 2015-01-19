#include <linux/clk.h>
#include "sd_port.h"
#include "sd_misc.h"
#include "sd_protocol.h"

//Global struct variable, to hold all card information need to operate card
/*static SD_MMC_Card_Info_t _sd_mmc_info = {CARD_TYPE_NONE,           //card_type
										  CARD_TYPE_NONE_SDIO,		//sdio_card_type
										 CARD_INDENTIFICATION_MODE, //operation_mode
										 SD_BUS_SINGLE,             //bus_width
										 SPEC_VERSION_10_101,       //spec_v1.0
										 SPEC_VERSION_10_12,        //spec_v1.0-v1.2
										 NORMAL_SPEED,              //normal_speed
										 {0},						//raw cid
										 0,                         //card_rca
										 0,                         //blk_len
										 0,                         //blk_nums
										 SD_MMC_TIME_NAC_DEFAULT,   //clks_nac
										 0,							//function_no
										 3000,						//clk_unit(default 3000ns)
										 0,                         //write_protected_flag
										 0,                         //inited_flag
										 0,                         //removed_flag
										 0,							//init_retry
										 0,							//single_blk_failed
										 0,                         //sdio_init_flag
										 NULL,						//sd_mmc_power
										 NULL,						//sd_mmc_get_ins
										 NULL,						//sd_get_wp
										 NULL						//sd_mmc_io_release
										 };*/
//SD_MMC_Card_Info_t *sd_mmc_info = &_sd_mmc_info;

extern unsigned sdio_timeout_int_times;
extern unsigned sdio_command_err;
extern unsigned sdio_command_int_num;

static char * sd_error_string[]={
	"SD_MMC_NO_ERROR",
	"SD_MMC_ERROR_OUT_OF_RANGE",        //Bit 31
	"SD_MMC_ERROR_ADDRESS",             //Bit 30 
	"SD_MMC_ERROR_BLOCK_LEN",           //Bit 29
	"SD_MMC_ERROR_ERASE_SEQ",           //Bit 28
	"SD_MMC_ERROR_ERASE_PARAM",         //Bit 27
	"SD_MMC_ERROR_WP_VIOLATION",        //Bit 26
	"SD_ERROR_CARD_IS_LOCKED",          //Bit 25
	"SD_ERROR_LOCK_UNLOCK_FAILED",      //Bit 24
	"SD_MMC_ERROR_COM_CRC",             //Bit 23
	"SD_MMC_ERROR_ILLEGAL_COMMAND",     //Bit 22
	"SD_ERROR_CARD_ECC_FAILED",         //Bit 21
	"SD_ERROR_CC",                      //Bit 20
	"SD_MMC_ERROR_GENERAL",             //Bit 19
	"SD_ERROR_Reserved1",               //Bit 18
	"SD_ERROR_Reserved2",               //Bit 17
	"SD_MMC_ERROR_CID_CSD_OVERWRITE",   //Bit 16
	"SD_ERROR_AKE_SEQ",                 //Bit 03
	"SD_MMC_ERROR_STATE_MISMATCH",
	"SD_MMC_ERROR_HEADER_MISMATCH",
	"SD_MMC_ERROR_DATA_CRC",
	"SD_MMC_ERROR_TIMEOUT", 
	"SD_MMC_ERROR_DRIVER_FAILURE",
	"SD_MMC_ERROR_WRITE_PROTECTED",
	"SD_MMC_ERROR_NO_MEMORY",
	"SD_ERROR_SWITCH_FUNCTION_COMUNICATION",
	"SD_ERROR_NO_FUNCTION_SWITCH",
	"SD_MMC_ERROR_NO_CARD_INS",
	"SD_MMC_ERROR_READ_DATA_FAILED",
	"SD_SDIO_ERROR_NO_FUNCTION"
};

static unsigned char char_mode[4][3] = {{0x10, 0x01, 0},
										{0x20, 0x02, 0},
										{0x40, 0x04, 0},
										{0x80, 0x08, 0}};
						
//All local function definitions, only used in this .C file
char * sd_error_to_string(int errcode);

void sd_delay_clocks_z(SD_MMC_Card_Info_t *sd_mmc_info, int num_clk);
void sd_delay_clocks_h(SD_MMC_Card_Info_t *sd_mmc_info, int num_clk);

void sd_clear_response(unsigned char * res_buf);
int sd_write_cmd_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long byte_cnt, unsigned char * data_buf);
int sd_get_dat0_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long byte_cnt, unsigned char * data_buf, unsigned short * crc16);
int sd_get_response_length(SD_Response_Type_t res_type);
int sd_read_response_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long byte_cnt, unsigned char * res_buf);
#ifdef SD_MMC_SW_CONTROL
int sd_send_cmd_sw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned char cmd, unsigned long arg, SD_Response_Type_t res_type, unsigned char * res_buf);
#endif
#ifdef SD_MMC_HW_CONTROL
int sd_send_cmd_hw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned char cmd, unsigned long arg, SD_Response_Type_t res_type, unsigned char * res_buf, unsigned char *data_buf, unsigned long data_cnt, int retry_flag);
#endif

int sd_check_response_r1(unsigned char cmd, SD_Response_R1_t * r1);
int sd_check_response_r3(unsigned char cmd, SD_Response_R3_t * r3);
int sd_check_response_r4(unsigned char cmd, SDIO_Response_R4_t * r4);
int sd_check_response_r5(unsigned char cmd, SDIO_RW_CMD_Response_R5_t * r5);
int sd_check_response_r6(unsigned char cmd, SD_Response_R6_t * r6);
int sd_check_response_r7(unsigned char cmd, SD_Response_R7_t * r7);
int sd_check_response_r2_cid(unsigned char cmd, SD_Response_R2_CID_t * r2_cid);
int sd_check_response_r2_csd(unsigned char cmd, SD_Response_R2_CSD_t * r2_csd);
int sd_check_response(unsigned char cmd, SD_Response_Type_t res_type, unsigned char * res_buf);

int sd_hw_reset(SD_MMC_Card_Info_t *sd_mmc_info);
int sd_sw_reset(SD_MMC_Card_Info_t *sd_mmc_info);
int sd_voltage_validation(SD_MMC_Card_Info_t *sd_mmc_info);
int sd_identify_process(SD_MMC_Card_Info_t *sd_mmc_info);
int sd_mmc_switch_function(SD_MMC_Card_Info_t *sd_mmc_info);
int sd_check_sdio_card_type(SD_MMC_Card_Info_t *sd_mmc_info);
int sdio_data_transfer_abort(SD_MMC_Card_Info_t *sd_mmc_info, int function_no);
int sdio_card_reset(SD_MMC_Card_Info_t *sd_mmc_info);
void sd_mmc_set_input(SD_MMC_Card_Info_t *sd_mmc_info);

//Read single block data from SD card
int sd_read_single_block(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned char * data_buf);
//Read multi block data from SD card
int sd_read_multi_block(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long lba_cnt, unsigned char * data_buf);
//Write single block data to SD card
int sd_write_single_block(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned char * data_buf);
//Write multi block data to SD card
int sd_write_multi_block(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long lba_cnt, unsigned char * data_buf);

//Read Operation Conditions Register
int sd_read_reg_ocr(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_OCR_t * ocr);
//Read Card_Identification Register
int sd_read_reg_cid(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_CID_t * cid);
//Read Card-Specific Data Register
int sd_read_reg_csd(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_CSD_t * csd);
//Read Relative Card Address Register
int sd_read_reg_rca(SD_MMC_Card_Info_t *sd_mmc_info, unsigned short * rca);
//Read Driver Stage Register
int sd_read_reg_dsr(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_DSR_t * dsr);
//Read SD CARD Configuration Register
int sd_read_reg_scr(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_SCR_t * scr);

int sd_check_data_consistency(SD_MMC_Card_Info_t *sd_mmc_info);

void sd_mmc_prepare_power(SD_MMC_Card_Info_t *sd_mmc_info);
void sd_mmc_io_config(SD_MMC_Card_Info_t *sd_mmc_info);
int sd_mmc_staff_init(SD_MMC_Card_Info_t *sd_mmc_info);
int sd_mmc_cmd_test(SD_MMC_Card_Info_t *sd_mmc_info);

int sd_mmc_check_wp(SD_MMC_Card_Info_t *sd_mmc_info);


#ifdef CONFIG_CARD_MUTE_INIT_INFO
static int card_dbg = 0;
#else
static int card_dbg = 1;
#endif
static int __init card_dbg_func(char *str)
{
	card_dbg = 1;
	printk("card_dbg\n");
	return 1;
}

__setup("carddbg", card_dbg_func);

#define sdpro_dbg(fmt, args...) do{ \
    if(card_dbg) \
        printk(fmt, ##args); \
}while(0)


//Return the string buf address of specific errcode
char * sd_error_to_string(int errcode)
{
	return sd_error_string[errcode];
}

//Set clock delay, Z-bit is driven to (respectively kept) HIGH by the pull-up resistors RCMD respectively RDAT.
void sd_delay_clocks_z(SD_MMC_Card_Info_t *sd_mmc_info, int num_clk)
{
	int i;
	
	sd_set_cmd_input();
	
	if(sd_mmc_info->operation_mode == CARD_INDENTIFICATION_MODE)
	{
		for(i = 0; i < num_clk; i++)
		{
			sd_clk_identify_low();
			sd_clk_identify_high();
		}
	}
	else    // Tranfer mode
	{
		for(i = 0; i < num_clk; i++)
		{
			sd_clk_transfer_low();
			sd_clk_transfer_high();
		}
	}
}

//Set clock delay, P-bit is actively driven to HIGH by the card respectively host output driver.
void sd_delay_clocks_h(SD_MMC_Card_Info_t *sd_mmc_info, int num_clk)
{
	int i;
	
	sd_set_cmd_output();
	sd_set_cmd_value(1);
	
	if(sd_mmc_info->operation_mode == CARD_INDENTIFICATION_MODE)
	{
		for(i = 0; i < num_clk; i++)
		{
			sd_clk_identify_low();
			sd_clk_identify_high();
		}
	}
	else    // Tranfer mode
	{
		for(i = 0; i < num_clk; i++)
		{
			sd_clk_transfer_low();
			sd_clk_transfer_high();
		}
	}
}

//Clear response data buffer
void sd_clear_response(unsigned char * res_buf)
{
	int i;

	if(res_buf == NULL)
		return;

	for(i = 0; i < MAX_RESPONSE_BYTES; i++)
		res_buf[i]=0;
}

//Put data bytes to cmd line
int sd_write_cmd_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long byte_cnt, unsigned char * data_buf)
{
	unsigned long data_cnt,data;
	int i;

	sd_set_cmd_output();

	if(sd_mmc_info->operation_mode == CARD_INDENTIFICATION_MODE)
	{
		for(data_cnt = 0; data_cnt < byte_cnt; data_cnt++)
		{
			for(i=7; i>=0; i--)
			{
				sd_clk_identify_low();
				
				data = (*data_buf >> i) & 0x01;
				sd_set_cmd_value(data);
				
				sd_clk_identify_high();
			}
			
			data_buf++;
		}
	}
	else    // Tranfer mode
	{
		for(data_cnt = 0; data_cnt < byte_cnt; data_cnt++)
		{
			for(i=7; i>=0; i--)
			{
				sd_clk_transfer_low();
				
				data = (*data_buf >> i) & 0x01;
				sd_set_cmd_value(data);
				
				sd_clk_transfer_high();
			}
			
			data_buf++;
		}
	}
	
	return SD_MMC_NO_ERROR; 
}

//Get data bytes from data0 line
int sd_get_dat0_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long byte_cnt, unsigned char * data_buf, unsigned short * crc16)
{
	unsigned long data_cnt,data,temp, num_nac=0;
	int busy = 1, i;
	
	if(!byte_cnt)
		return SD_MMC_NO_ERROR;
		
	memset(data_buf, 0, byte_cnt);
	*crc16 = 0;
	
	//wait until data is valid
	sd_set_dat0_input();
	
	if(sd_mmc_info->operation_mode == CARD_INDENTIFICATION_MODE)
	{
		do
		{
			sd_clk_identify_low();
		
			data = sd_get_dat0_value();
			if(!data)
			{
				busy = 0;
			}
			
			sd_clk_identify_high();
			
			num_nac++;
		
		}while(busy && (num_nac < sd_mmc_info->clks_nac));

		if(num_nac >= sd_mmc_info->clks_nac)
			return SD_MMC_ERROR_TIMEOUT;

		//read data
		for(data_cnt = 0; data_cnt < byte_cnt; data_cnt++)
		{
			temp = 0;
			
			for(i=0; i<8; i++)
			{
				sd_clk_identify_low();
				
				data = sd_get_dat0_value();
				temp <<= 1;
				temp |= data;
				
				sd_clk_identify_high();
			}
			
			*data_buf = temp;
			data_buf++;
		}
	
		//Read CRC16 data
		for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
		{
			sd_clk_identify_low();
		
			data = sd_get_dat0_value();
			*crc16 <<= 1;
			*crc16 |= data;
					
			sd_clk_identify_high();
		}
		
		//for end bit
		sd_clk_identify_low();
		sd_clk_identify_high();
	}
	else    // Tranfer mode
	{
		do
		{
			sd_clk_transfer_low();
		
			data = sd_get_dat0_value();
			if(!data)
			{
				busy = 0;
			}
			
			sd_clk_transfer_high();
			
			num_nac++;
		
		}while(busy && (num_nac < sd_mmc_info->clks_nac));

		if(num_nac >= sd_mmc_info->clks_nac)
			return SD_MMC_ERROR_TIMEOUT;

		//read data
		for(data_cnt = 0; data_cnt < byte_cnt; data_cnt++)
		{
			temp = 0;
			
			for(i=0; i<8; i++)
			{
				sd_clk_transfer_low();
				
				data = sd_get_dat0_value();
				temp <<= 1;
				temp |= data;
				
				sd_clk_transfer_high();
			}
			
			*data_buf = temp;
			data_buf++;
		}
	
		//Read CRC16 data
		for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
		{
			sd_clk_transfer_low();
		
			data = sd_get_dat0_value();
			*crc16 <<= 1;
			*crc16 |= data;
					
			sd_clk_transfer_high();
		}
		
		//for end bit
		sd_clk_transfer_low();
		sd_clk_transfer_high();
	}

	return SD_MMC_NO_ERROR;
}

//Get response length according to response type
int sd_get_response_length(SD_Response_Type_t res_type)
{
	int num_res;
	
	switch(res_type)
	{
		case RESPONSE_R1:
		case RESPONSE_R1B:
		case RESPONSE_R3:
		case RESPONSE_R4:
		case RESPONSE_R5:
		case RESPONSE_R6:
		case RESPONSE_R7:
			num_res = RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH;
			break;
		case RESPONSE_R2_CID:
		case RESPONSE_R2_CSD:
			num_res = RESPONSE_R2_CID_CSD_LENGTH;
			break;
		case RESPONSE_NONE:
			num_res = RESPONSE_NONE_LENGTH;
			break;
		default:
			num_res = RESPONSE_NONE_LENGTH;
			break;
	 }
	 
	 return num_res;
}

//Send command with response
#ifdef SD_MMC_SW_CONTROL
int sd_send_cmd_sw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned char cmd, unsigned long arg, SD_Response_Type_t res_type, unsigned char * res_buf)
{
	int ret = SD_MMC_NO_ERROR, num_res;
	unsigned char cmd_buf[6];

	cmd_buf[0] = 0x40 | cmd;                //0x40: host command, command: 6 bits
	cmd_buf[1] = arg >> 24;                 //Command argument: 32 bits
	cmd_buf[2] = arg >> 16;
	cmd_buf[3] = arg >> 8;
	cmd_buf[4] = (unsigned char)arg;
	cmd_buf[5] = sd_cal_crc7(cmd_buf, 5) | 0x01;            //Calculate CRC checksum, 7 bits
	
	ret = sd_verify_crc7(cmd_buf, 6);
	if(ret)
		return SD_MMC_ERROR_COM_CRC;

	sd_write_cmd_data(sd_mmc_info, 6, cmd_buf);
	
	if(res_type == RESPONSE_NONE)
	{
		sd_delay_clocks_z(sd_mmc_info, SD_MMC_TIME_NRC_NCC);
		return SD_MMC_NO_ERROR;
	}

	//A delay before dealing with response
	sd_delay_clocks_z(sd_mmc_info,SD_MMC_Z_CMD_TO_RES);
	
	num_res = sd_get_response_length(res_type);
	
	sd_clear_response(res_buf);
	ret = sd_read_response_data(sd_mmc_info, num_res, res_buf); 
	if(ret)
		return ret;

	ret = sd_check_response(cmd, res_type, res_buf);

	sd_delay_clocks_z(sd_mmc_info, SD_MMC_TIME_NRC_NCC);
	
	return ret;
}
#endif

static const char* cmd_strings[] = {
//0
	"SD_MMC_GO_IDLE_STATE",
	"MMC_SEND_OP_COND",
	"SD_MMC_ALL_SEND_CID",
	"SD_MMC_SEND_RELATIVE_ADDR",
	"Reserved",
//5
	"IO_SEND_OP_COND",
	"SD_SET_BUS_WIDTHS",	//MMC_SWITCH_FUNTION(6), SD_SWITCH_FUNCTION(46)
	"SD_MMC_SELECT_DESELECT_CARD",
	"SD_SEND_IF_COND",		//MMC_SEND_EXT_CSD(8)
	"SD_MMC_SEND_CSD",
//10	
	"SD_MMC_SEND_CID",
//	"SD_READ_DAT_UNTIL_STOP",	//replaced by "VOLTAGE_SWITCH"
	"VOLTAGE_SWITCH",
	"SD_MMC_STOP_TRANSMISSION",
	"SD_MMC_SEND_STATUS",
	"Reserved",
//15
	"SD_MMC_GO_INACTIVE_STATE",
	"SD_MMC_SET_BLOCKLEN",
	"SD_MMC_READ_SINGLE_BLOCK",
	"SD_MMC_READ_MULTIPLE_BLOCK",
	"Reserved",
//20
	"Reserved",
	"Reserved",
	"SD_SEND_NUM_WR_BLOCKS",
	"SD_SET_WR_BLK_ERASE_COUNT",
	"SD_MMC_WRITE_BLOCK",
//25
	"SD_MMC_WRITE_MULTIPLE_BLOCK",
	"Reserved",
	"SD_MMC_PROGRAM_CSD",
	"SD_MMC_SET_WRITE_PROT",
	"SD_MMC_CLR_WRITE_PROT",
//30
	"SD_MMC_SEND_WRITE_PROT",
	"Reserved",
	"SD_ERASE_WR_BLK_START",	//MMC_TAG_SECTOR_START(32)
	"SD_ERASE_WR_BLK_END",		//MMC_TAG_SECTOR_END(33)
	"MMC_UNTAG_SECTOR",
//35
	"MMC_TAG_ERASE_GROUP_START",
	"MMC_TAG_ERASE_GROUP_END",
	"MMC_UNTAG_ERASE_GROUP",
	"SD_MMC_ERASE",
	"Reserved",
//40
	"Reserved",
	"SD_APP_OP_COND",
	"SD_SET_CLR_CARD_DETECT",	//MMC_LOCK_UNLOCK(42)
	"SDA reserved",
	"SDA reserved",
//45
	"SDA reserved",
	"SD_SWITCH_FUNCTION",
	"SDA reserved",
	"SDA reserved",
	"SDA reserved",
//50
	"SDA reserved",
	"SD_SEND_SCR",
	"IO_RW_DIRECT",
	"IO_RW_EXTENDED",
	"SDA reserved",
//55	
	"SD_APP_CMD",
	"SD_GEN_CMD",
	"Reserved",
	"Reserved",
	"Reserved",
//60	
	"Manufact reserved",
	"Manufact reserved",
	"Manufact reserved",
	"Manufact reserved",
};
#if 0
static void my_print_cmd(unsigned char cmd)
{
	if (cmd < 64)
	{
		printk("CMD%d : %s\n", cmd, cmd_strings[cmd]);
	}
	else
	{
		printk("CMD%d : no such cmd\n", cmd);
	}
}
#endif
int using_sdxc_controller = 0;	//set in sd_io_init()

int sdxc_int_param = 0;
unsigned int sdxc_delay = 20;	//5*32=160us

static void my_swap_64(unsigned char *pbuf)
{
	unsigned int tmp;
	unsigned char *pt;
	tmp = *((unsigned int *)pbuf);
	pt = (unsigned char *)&tmp;

	pbuf[0] = pbuf[7];
	pbuf[1] = pbuf[6];
	pbuf[2] = pbuf[5];
	pbuf[3] = pbuf[4];
	
	pbuf[4] = pt[3];
	pbuf[5] = pt[2];
	pbuf[6] = pt[1];
	pbuf[7] = pt[0];
}

static int my_check = 0;
#define MY_CHECK() { if (my_check) printk("check %d\n", check++); }

unsigned char mycmd = 0;
static int myt1 = 0;

extern struct completion dat0_int_complete;

int sdxc_check_dat0_ready(void)
{
	unsigned int sdxc_status;
	SDXC_Status_Reg_t *sdxc_status_reg;
	sdxc_status = READ_CBUS_REG(SD_REG3_STAT);
	sdxc_status_reg = (void *)&sdxc_status;
	return (sdxc_status_reg->dat_3_0 & 0x01) == 1;
}

///my_write()
int sdxc_send_cmd_hw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned char cmd, unsigned long arg, SD_Response_Type_t res_type, unsigned char * res_buf, unsigned char *data_buf, unsigned long data_cnt, int retry_flag)
{
	int ret = SD_MMC_NO_ERROR, num_res;
	unsigned char *buffer = NULL;
	unsigned int value;
	int check = 0;

	SDXC_Send_Reg_t *sdxc_send_reg;
	SDXC_Ctrl_Reg_t *sdxc_ctrl_reg;
	SDXC_Status_Reg_t *sdxc_status_reg;
	//SDXC_Clk_Reg_t *sdxc_clk_reg;
	SDXC_PDMA_Reg_t *sdxc_pdma_reg;
	//SDXC_Misc_Reg_t *sdxc_misc_reg;
	SDXC_Ictl_Reg_t *sdxc_ictl_reg;
	SDXC_Ista_Reg_t *sdxc_ista_reg;
	//SDXC_Srst_Reg_t *sdxc_srst_reg;

	unsigned int sdxc_send, sdxc_ctrl, sdxc_status, sdxc_pdma;// sdxc_clk, sdxc_misc;
	unsigned int sdxc_ictl, sdxc_ista;
	
	unsigned int timeout;
	dma_addr_t data_dma_to_device_addr=0;
	dma_addr_t data_dma_from_device_addr=0;
	int i;
	int check_dat0_busy = 0;
	//unsigned long tick1;

	mycmd = cmd;

/*
	my_print_cmd(cmd);	//lin

	if (cmd == SD_MMC_READ_SINGLE_BLOCK || cmd == SD_MMC_READ_MULTIPLE_BLOCK) {
		printk("sd_read : lba=%d, data_buf=%p/%p, data_cnt=%p\n", arg, data_buf, sd_mmc_info->sd_mmc_buf, data_cnt);
	}
*/
	MY_CHECK();

	sdxc_send = 0;
	sdxc_send_reg = (void *)&sdxc_send;

	sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
	sdxc_ctrl_reg = (void *)&sdxc_ctrl;

	sdxc_status = READ_CBUS_REG(SD_REG3_STAT);
	sdxc_status &= 0xFFF000FF;	//clear rx_count & tx_count
	sdxc_status_reg = (void *)&sdxc_status;

	sdxc_pdma = READ_CBUS_REG(SD_REG6_PDMA);
	sdxc_pdma_reg = (void *) &sdxc_pdma;

	sdxc_ictl = READ_CBUS_REG(SD_REG9_ICTL);
	sdxc_ictl_reg = (void *)&sdxc_ictl;

	sdxc_ista = READ_CBUS_REG(SD_REGA_ISTA);
	sdxc_ista_reg = (void *)&sdxc_ista;

	/*
	sdxc_srst = READ_CBUS_REG(SD_REGB_SRST);
	sdxc_srst_reg = (void *)&sdxc_srst;

	sdxc_srst_reg->main_ctrl_srst = 1;
	sdxc_srst_reg->rx_fifo_srst = 1;
	sdxc_srst_reg->tx_fifo_srst = 1;
	sdxc_srst_reg->dma_srst = 1;
	WRITE_CBUS_REG(SD_REGB_SRST, sdxc_srst);
	
	sd_delay_ms(10);

	//may remove
	sdxc_srst_reg->main_ctrl_srst = 0;
	WRITE_CBUS_REG(SD_REGB_SRST, sdxc_srst);
	*/

		/*
		sdxc_srst = 0x27;	//0x3F -- 0x27 : exclude rx_dphy_srst & tx_dphy_srst
		WRITE_CBUS_REG(SD_REGB_SRST, sdxc_srst);
		
		sd_delay_ms(1);
		*/

		if (myt1)
			sd_delay_ms(1);
		/*
		sdxc_srst = 0x27;			
		WRITE_CBUS_REG(SD_REGB_SRST, sdxc_srst);
		*/
		
	if ((cmd == SD_SWITCH_FUNCTION) || (cmd == MMC_SEND_EXT_CSD))
		sdxc_send_reg->command_index = cmd-40;  //for distinguish ACMD6 and CMD6,Maybe more good way but now I cant find
	else	
	    sdxc_send_reg->command_index = cmd;

	sd_clear_response(res_buf);

	MY_CHECK();
	switch(res_type)
	{
		case RESPONSE_R1:
		case RESPONSE_R1B:
		case RESPONSE_R3:
		case RESPONSE_R4:
		case RESPONSE_R6:
		case RESPONSE_R5:
		case RESPONSE_R7:
			sdxc_send_reg->command_has_resp = 1;
			sdxc_send_reg->response_length = 0;		//48 bits
			break;
		case RESPONSE_R2_CID:
		case RESPONSE_R2_CSD:
			sdxc_send_reg->command_has_resp = 1;
			sdxc_send_reg->response_length = 1;		//136 bits
			//sdxx : cmd_send_reg->res_crc7_from_8 = 1;		//new not necessary ?
			sdxc_send_reg->response_no_crc = 1;	//sdxx
			break;
		case RESPONSE_NONE:
			sdxc_send_reg->command_has_resp = 0;
			sdxc_send_reg->response_length = 0;
			break;
		default:
			sdxc_send_reg->command_has_resp = 0;
			sdxc_send_reg->response_length = 0;
			break;
	 }

	//cmd with adtc
	switch(cmd)
	{
		case SD_MMC_SEND_STATUS : 
		case SD_SEND_TUNNING_PATTERN :
			sdxc_send_reg->command_has_data = 1;
			sdxc_send_reg->data_direction = 0;	//rx
			sdxc_send_reg->total_pack = 0;
			sdxc_ctrl_reg->pack_len = data_cnt;
			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;
			
		case SD_MMC_READ_SINGLE_BLOCK:
		case SD_MMC_READ_MULTIPLE_BLOCK:
			sdxc_send_reg->command_has_data = 1;
			sdxc_send_reg->data_direction = 0;	//rx
			sdxc_send_reg->total_pack = data_cnt/sd_mmc_info->blk_len - 1;
			sdxc_ctrl_reg->pack_len = 0;
			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;

        case SD_SWITCH_FUNCTION:
		case MMC_SEND_EXT_CSD:
			sdxc_send_reg->command_has_data = 1;
			sdxc_send_reg->data_direction = 0;	//rx
			sdxc_send_reg->total_pack = 0;
			sdxc_ctrl_reg->pack_len = data_cnt;
			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;

		case SD_MMC_WRITE_BLOCK:
		case SD_MMC_WRITE_MULTIPLE_BLOCK:
			sdxc_send_reg->command_has_data = 1;
			sdxc_send_reg->data_direction = 1;	//tx
			sdxc_send_reg->total_pack = data_cnt/sd_mmc_info->blk_len - 1;
			sdxc_ctrl_reg->pack_len = 0;
			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;

		case IO_RW_EXTENDED:
			if(arg & (1<<27))
			{
				sdxc_send_reg->total_pack = data_cnt/sd_mmc_info->blk_len - 1;
				sdxc_ctrl_reg->pack_len = 0;
			}
			else
			{
				sdxc_send_reg->total_pack = 0;
				sdxc_ctrl_reg->pack_len = data_cnt;
			}

			if(arg & (1<<31))
			{
				sdxc_send_reg->command_has_data = 1;
				sdxc_send_reg->data_direction = 1;	//tx
				//memcpy(sd_mmc_info->sd_mmc_buf, data_buf, data_cnt);
				//buffer = sd_mmc_info->sd_mmc_phy_buf;
				data_dma_to_device_addr=dma_map_single(NULL, (void *)data_buf, data_cnt, DMA_TO_DEVICE);	
				buffer = (unsigned char*)data_dma_to_device_addr;
			}
			else
			{
				sdxc_send_reg->command_has_data = 1;
				sdxc_send_reg->data_direction = 0;	//rx
				//buffer = sd_mmc_info->sd_mmc_phy_buf;
				data_dma_from_device_addr = dma_map_single(NULL, (void *)data_buf, data_cnt, DMA_FROM_DEVICE );
				buffer = (unsigned char*)data_dma_from_device_addr;
			}
			break;

		////case SD_READ_DAT_UNTIL_STOP:
		case SD_SEND_NUM_WR_BLOCKS:
		
		case SD_MMC_PROGRAM_CSD:
		case SD_MMC_SEND_WRITE_PROT:
		case MMC_LOCK_UNLOCK:
		case SD_SEND_SCR:
		case SD_GEN_CMD:
			sdxc_send_reg->command_has_data = 1;
			sdxc_send_reg->data_direction = 0;	//rx
			sdxc_send_reg->total_pack = 0;
			sdxc_ctrl_reg->pack_len = data_cnt;
			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;
			
		default:
			break;
			
	}

	//cmd with R1b
	switch(cmd)
	{
		case SD_MMC_STOP_TRANSMISSION:
			sdxc_send_reg->data_stop = 1;
		case SD_MMC_SET_WRITE_PROT:
		case SD_MMC_CLR_WRITE_PROT:
		case SD_MMC_ERASE:
		case MMC_LOCK_UNLOCK:
			//sdxx : cmd_send_reg->check_dat0_busy = 1;
			check_dat0_busy = 1;
			break;
		default:
			break;
			
	}

	//cmd with R3
	switch(cmd)
	{
		case MMC_SEND_OP_COND:
		case SD_APP_OP_COND:
		case IO_SEND_OP_COND:
			//cmd_send_reg->res_without_crc7 = 1;
			sdxc_send_reg->response_no_crc = 1;	//sdxx
			break;
		default:
			break;
			
	}

	#define SD_MMC_CMD_COUNT			20000//20
	#define SD_MMC_READ_BUSY_COUNT		2000000//20
	#define SD_MMC_WRITE_BUSY_COUNT		50000000//500000
	#define SD_MMC_WAIT_STOP_COUNT		100000000
	#define SD_MMC_RETRY_COUNT			2
    
    if((sdxc_send_reg->command_has_data == 1) && (sdxc_send_reg->data_direction == 1))
    {    
        if(cmd == SD_MMC_WRITE_MULTIPLE_BLOCK)
    	    timeout = SD_MMC_WRITE_BUSY_COUNT * (data_cnt/512);
    	else if(cmd == IO_RW_EXTENDED)
    		timeout = SD_MMC_WRITE_BUSY_COUNT * (sdxc_send_reg->total_pack + 1);
    	else
    	    timeout = SD_MMC_WRITE_BUSY_COUNT;
    }
    else
    {    
        if(cmd == SD_MMC_READ_MULTIPLE_BLOCK)
    	    timeout = SD_MMC_READ_BUSY_COUNT * (data_cnt/512);
    	else if(cmd == IO_RW_EXTENDED)
    		timeout = SD_MMC_READ_BUSY_COUNT * (sdxc_send_reg->total_pack + 1);
        else
    	    timeout = SD_MMC_CMD_COUNT;
    }
    
	MY_CHECK();
    if(cmd == SD_MMC_STOP_TRANSMISSION)
        timeout = SD_MMC_WAIT_STOP_COUNT;

	sdxc_ista |= 0x00007FFF;	//sdxx : clear the interrupt status bits
	
	if(sdxc_send_reg->command_has_data)	{
		WRITE_CBUS_REG(SD_REG5_ADDR, (unsigned long)buffer);
	}

	if ((sd_mmc_info->card_type == CARD_TYPE_EMMC || sd_mmc_info->card_type == CARD_TYPE_MMC) &&
		(cmd == SD_MMC_READ_SINGLE_BLOCK || cmd == SD_MMC_READ_MULTIPLE_BLOCK 
		 || cmd == SD_MMC_WRITE_BLOCK || cmd == SD_MMC_WRITE_MULTIPLE_BLOCK)) {
		sdxc_ctrl_reg->endian = 0x00;
	}
	else {
		sdxc_ctrl_reg->endian = 0x03;
		if (sdxc_send_reg->command_has_data && (sdxc_send_reg->data_direction == 1)) {	//@@ wr
			unsigned int packs, count;
			unsigned char* pbuf; 
			
			packs = sdxc_send_reg->total_pack;
			
			if (sdxc_ctrl_reg->pack_len != 0)
				count = sdxc_ctrl_reg->pack_len / 8;	// convert byte to int64
			else
				count = 512 / 8;	// convert byte to int64
			
			pbuf = (unsigned char*)data_buf;
			
			do {
				for (i = 0; i < count; i++)
				{
					my_swap_64(pbuf);
					pbuf += 8;
				}
			} while (packs--);
		}
	}

	WRITE_CBUS_REG(SD_REG0_ARGU, arg);
	WRITE_CBUS_REG(SD_REGA_ISTA, sdxc_ista);
	WRITE_CBUS_REG(SD_REG3_STAT, sdxc_status);
	WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
	

	MY_CHECK();
	sdio_command_err = SDIO_NONE_ERR;
	sdxc_int_param = 0;

	sdio_command_int_num = 0;
	if (sdxc_send_reg->command_has_resp)
		sdio_command_int_num++;

	if (sdxc_send_reg->command_has_data)
		sdio_command_int_num++;
	
	init_completion(&dat0_int_complete);

	init_completion(&sdio_int_complete);
	sdio_open_host_interrupt(SDIO_CMD_INT);
	sdio_open_host_interrupt(SDIO_TIMEOUT_INT);

	/*
	if (check_dat0_busy) {
		sdxc_ictl = READ_CBUS_REG(SD_REG9_ICTL);
		sdxc_ictl_reg->dat0_ready_int_en = 1;
		WRITE_CBUS_REG(SD_REG9_ICTL, sdxc_ictl);
	}
	*/
	WRITE_CBUS_REG(SD_REG1_SEND, sdxc_send);	//launching sdxc
	
	timeout =50;//0.5s
	if (cmd == SD_MMC_READ_SINGLE_BLOCK || cmd == SD_MMC_READ_MULTIPLE_BLOCK 
		 || cmd == SD_MMC_WRITE_BLOCK || cmd == SD_MMC_WRITE_MULTIPLE_BLOCK)
		 timeout = 500; // 5s
	timeout = wait_for_completion_timeout(&sdio_int_complete,timeout);

/*
	if (timeout != 0 && check_dat0_busy && !sdxc_check_dat0_ready()) {
		//unsigned int timeout2 = 50; // 0.5s
		unsigned int timeout2 = 300; // 0.5s
		timeout2 = wait_for_completion_timeout(&dat0_int_complete,timeout2);
		if(timeout2 == 0) {
			printk("[sdxc] dat0 timeout2\n");
			if (!sdxc_check_dat0_ready()) {
				printk("[sdxc] dat0 not 1\n");
				timeout = 0;	//set up traps
			}
		}
	}
*/

	if (timeout != 0 && check_dat0_busy && !sdxc_check_dat0_ready()) {
		unsigned int retry_limit;
		unsigned int timeout3;

		retry_limit = 200;
		while (--retry_limit) {
			timeout3 = wait_for_completion_timeout(&dat0_int_complete, 1);
			if(timeout3 != 0) {
				if (!sdxc_check_dat0_ready())
					printk("bad dat0 int, sdxc_int_param=%03d\n", sdxc_int_param);
				//printk("[sdxc] dat0 int %d\n", 100 - retry_limit);
				break;
			}
			else if (sdxc_check_dat0_ready()) {
				//printk("[sdxc] dat0 ready %d\n", 100 - retry_limit);
				break;
			}
		}
		if (retry_limit == 0) {
			printk("[sdxc] dat0 error\n");
			timeout = 0;	//set up traps			
		}
	}

	
	sdio_close_host_interrupt(SDIO_CMD_INT);
	sdio_close_host_interrupt(SDIO_TIMEOUT_INT);

	/*
	if (check_dat0_busy) {
		sdxc_ictl = READ_CBUS_REG(SD_REG9_ICTL);
		sdxc_ictl_reg->dat0_ready_int_en = 0;
		WRITE_CBUS_REG(SD_REG9_ICTL, sdxc_ictl);
	}
	*/
	if(timeout == 0) {
		ret = SD_MMC_ERROR_TIMEOUT;
		printk("[sdxc] wait_for_completion_timeout for cmd(%d)=%s\n", cmd, cmd_strings[cmd]);
		goto error;
	}
	
	MY_CHECK();		// 4
	if (sdio_command_err == SDIO_RES_TIMEOUT_ERR || sdio_command_err == SDIO_PACK_TIMEOUT_ERR)
	{
		printk("sdio_command_err = %s for cmd(%d)=%s\n", sdio_command_err == SDIO_RES_TIMEOUT_ERR ? "SDIO_RES_TIMEOUT_ERR" : "SDIO_PACK_TIMEOUT_ERR", cmd, cmd_strings[cmd]);
		ret = SD_MMC_ERROR_TIMEOUT;
		goto error;
	}
	
	if (sdio_command_err == SDIO_RES_CRC_ERR || sdio_command_err == SDIO_PACK_CRC_ERR)
	{
		printk("sdio_command_err2 = %s for cmd(%d)=%s\n", sdio_command_err == SDIO_RES_CRC_ERR ? "SDIO_RES_CRC_ERR" : "SDIO_PACK_CRC_ERR", cmd, cmd_strings[cmd]);
		ret = SD_MMC_ERROR_COM_CRC;
		goto error;
	}

	/*
	if (check_dat0_busy && !sdxc_check_dat0_ready()) {
		sdxc_ictl = READ_CBUS_REG(SD_REG9_ICTL);
		sdxc_ictl_reg->dat0_ready_int_en = 1;
		WRITE_CBUS_REG(SD_REG9_ICTL, sdxc_ictl);

		timeout = 50; // 0.5s
		timeout = wait_for_completion_timeout(&dat0_int_complete,timeout);
		
		sdxc_ictl_reg->dat0_ready_int_en = 0;
		WRITE_CBUS_REG(SD_REG9_ICTL, sdxc_ictl);

		if(timeout == 0) {
			printk("[sdxc] dat0 timeout\n");
			if (!sdxc_check_dat0_ready()) {
				ret = SD_MMC_ERROR_TIMEOUT;
				printk("[sdxc] dat0 not 1\n");
				goto error;
			}
		}
	}
	*/
	
	//printk("sdxc_int_param=%03d for cmd=%d\n", sdxc_int_param, cmd);

/*	
	tick1 = jiffies + 200;	// 2s
	if (check_dat0_busy) {
		do {
			sdxc_ista = READ_CBUS_REG(SD_REGA_ISTA);
			sdxc_ista_reg = (void *)&sdxc_ista;
			sdxc_status = READ_CBUS_REG(SD_REG3_STAT);
			sdxc_status_reg = (void *)&sdxc_status;
			if (time_after(jiffies, tick1)) {
				printk("wait for dat0 timeout for cmd(%d)=%s\n", cmd, cmd_strings[cmd]);
				break;
			}
		} while ((sdxc_ista_reg->dat0_ready_int == 0) && ((sdxc_status_reg->dat_3_0 & 0x01) == 0));
		//printk("A dat0_ready_int=%d, dat_3_0=%d for cmd(%d)=%s\n", sdxc_ista_reg->dat0_ready_int, sdxc_status_reg->dat_3_0, cmd, cmd_strings[cmd]);
	}
	printk("sdxc_int_param = %d for cmd=%d\n", sdxc_int_param, cmd);
*/
	MY_CHECK();		// 5
	if (sdxc_send_reg->command_has_resp)
	{
		/*
		{	//lin : test resp[39:08]
			sdxc_pdma_reg->pio_rd_resp = 0;
			WRITE_CBUS_REG(SD_REG6_PDMA, sdxc_pdma);
			value = READ_CBUS_REG(SD_REG0_ARGU);
			printk("response[39:08]=%08x\n    ", value);

			for (i = 7; i>= 0; i--)
			{
				sdxc_pdma_reg->pio_rd_resp = i;	
				WRITE_CBUS_REG(SD_REG6_PDMA, sdxc_pdma);
				value = READ_CBUS_REG(SD_REG0_ARGU);
				printk("%08x ", value);
			}
			printk("\n");
		}
		*/

		if (sdxc_send_reg->response_length == 0)	//48 bits
		{
			num_res = 6;			
		}
		else	//136 bits
		{
			num_res = 17;
		}

		sdxc_pdma_reg->pio_rd_resp = 0;
		while (1)
		{
			sdxc_pdma_reg->pio_rd_resp++;
			WRITE_CBUS_REG(SD_REG6_PDMA, sdxc_pdma);
			value = READ_CBUS_REG(SD_REG0_ARGU);
			
			res_buf[--num_res] = value & 0xFF;
			if (num_res == 0)
				break;
			res_buf[--num_res] = (value >> 8) & 0xFF;
			if (num_res == 0)
				break;
			res_buf[--num_res] = (value >> 16) & 0xFF;
			if (num_res == 0)
				break;
			res_buf[--num_res] = (value >> 24) & 0xFF;
			if (num_res == 0)
				break;
		}
	}
	
	MY_CHECK();	// 6
	ret = sd_check_response(cmd, res_type, res_buf);
	if(ret)
		goto error;

	/*error need dma_unmap_single also*/
error:
	//printk("sdxc cmd(%d) : sdxc_int_param=%d, sdio_command_err=%d\n", cmd, sdxc_int_param, sdio_command_err);
	if(data_dma_from_device_addr)
	{
		dma_unmap_single(NULL, data_dma_from_device_addr, data_cnt, DMA_FROM_DEVICE);
	}
	if(data_dma_to_device_addr)
	{
		dma_unmap_single(NULL, data_dma_to_device_addr, data_cnt, DMA_TO_DEVICE);
	}

/*	//lin : I fix the sd_read_multi_block_hw() and sd_write_multi_block_hw() already
	if((sdxc_send_reg->command_has_data == 1) && (sdxc_send_reg->data_direction == 0) && buffer && (data_buf != sd_mmc_info->sd_mmc_buf)
		&& (!data_dma_from_device_addr) && (!data_dma_to_device_addr))
	{
		printk("Not sd_mmc_buf, copying\n");
		////memcpy(data_buf, sd_mmc_info->sd_mmc_buf, data_cnt);
	}
*/

	MY_CHECK();	// 7
	return ret;
}

int wait_flag;
//Send command with response
#ifdef SD_MMC_HW_CONTROL
int sd_send_cmd_hw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned char cmd, unsigned long arg, SD_Response_Type_t res_type, unsigned char * res_buf, unsigned char *data_buf, unsigned long data_cnt, int retry_flag)
{
	int ret = SD_MMC_NO_ERROR, num_res;
	unsigned char *buffer = NULL;
	unsigned int cmd_ext, cmd_send;

	MSHW_IRQ_Config_Reg_t *irq_config_reg;
	SDIO_Status_IRQ_Reg_t *status_irq_reg;
	SDHW_CMD_Send_Reg_t *cmd_send_reg;
	SDHW_Extension_Reg_t *cmd_ext_reg;
	unsigned int irq_config, status_irq, timeout;
	dma_addr_t data_dma_to_device_addr=0;
	dma_addr_t data_dma_from_device_addr=0;

	if ((sd_mmc_info->io_pad_type == SDXC_CARD_0_5) || 
		(sd_mmc_info->io_pad_type == SDXC_BOOT_0_11) ||
		(sd_mmc_info->io_pad_type == SDXC_GPIOX_0_9))
		using_sdxc_controller = 1;
	else
		using_sdxc_controller = 0;
	
	if (using_sdxc_controller)
		return sdxc_send_cmd_hw(sd_mmc_info, cmd, arg, res_type, res_buf, data_buf, data_cnt, retry_flag);

	//my_print_cmd(cmd);	//lin
	
	cmd_send = 0;
	cmd_send_reg = (void *)&cmd_send;
	if ((cmd == SD_SWITCH_FUNCTION) || (cmd == MMC_SEND_EXT_CSD))
		cmd_send_reg->cmd_data  = 0x40 | (cmd-40);          //for distinguish ACMD6 and CMD6,Maybe more good way but now I cant find
	else	
	    cmd_send_reg->cmd_data = 0x40 | cmd;
	cmd_send_reg->use_int_window = 1;

	cmd_ext = 0;
	cmd_ext_reg = (void *)&cmd_ext;
	//if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	//	cmd_ext_reg->crc_status_4line = 1;

	sd_clear_response(res_buf);

	switch(res_type)
	{
		case RESPONSE_R1:
		case RESPONSE_R1B:
		case RESPONSE_R3:
		case RESPONSE_R4:
		case RESPONSE_R6:
		case RESPONSE_R5:
		case RESPONSE_R7:
			cmd_send_reg->cmd_res_bits = 45;		// RESPONSE have 7(cmd)+32(respnse)+7(crc)-1 data
			break;
		case RESPONSE_R2_CID:
		case RESPONSE_R2_CSD:
			cmd_send_reg->cmd_res_bits = 133;		// RESPONSE have 7(cmd)+120(respnse)+7(crc)-1 data
			cmd_send_reg->res_crc7_from_8 = 1;
			break;
		case RESPONSE_NONE:
			cmd_send_reg->cmd_res_bits = 0;			// NO_RESPONSE
			break;
		default:
			cmd_send_reg->cmd_res_bits = 0;			// NO_RESPONSE
			break;
	 }

	//cmd with adtc
	switch(cmd)
	{
		case SD_MMC_SEND_STATUS : 
		case SD_SEND_TUNNING_PATTERN :
			cmd_send_reg->res_with_data = 1;
			cmd_send_reg->repeat_package_times = 0;
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)
				cmd_ext_reg->data_rw_number = data_cnt * 8 + (16 - 1) * 4;
			else
				cmd_ext_reg->data_rw_number = data_cnt * 8 + 16 - 1;
			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;
			
		case SD_MMC_READ_SINGLE_BLOCK:
		case SD_MMC_READ_MULTIPLE_BLOCK:
			cmd_send_reg->res_with_data = 1;
			cmd_send_reg->repeat_package_times = data_cnt/sd_mmc_info->blk_len - 1;
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)
				cmd_ext_reg->data_rw_number = sd_mmc_info->blk_len * 8 + (16 - 1) * 4;
			else
				cmd_ext_reg->data_rw_number = sd_mmc_info->blk_len * 8 + 16 - 1;

			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;

        case SD_SWITCH_FUNCTION:
        case MMC_SEND_EXT_CSD:
			cmd_send_reg->res_with_data = 1;
			cmd_send_reg->repeat_package_times = 0;
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)
				cmd_ext_reg->data_rw_number = data_cnt * 8 + (16 - 1) * 4;
			else
				cmd_ext_reg->data_rw_number = data_cnt * 8 + 16 - 1;
			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;

		case SD_MMC_WRITE_BLOCK:
		case SD_MMC_WRITE_MULTIPLE_BLOCK:
			cmd_send_reg->cmd_send_data = 1;
			cmd_send_reg->repeat_package_times = data_cnt/sd_mmc_info->blk_len - 1;
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)
				cmd_ext_reg->data_rw_number = sd_mmc_info->blk_len * 8 + (16 - 1) * 4;
			else
				cmd_ext_reg->data_rw_number = sd_mmc_info->blk_len * 8 + 16 - 1;
			
			buffer = sd_mmc_info->sd_mmc_phy_buf;
                        wmb();
			break;

		case IO_RW_EXTENDED:
			if(arg & (1<<27))
			{
				cmd_send_reg->repeat_package_times = data_cnt/sd_mmc_info->blk_len - 1;
				if(sd_mmc_info->bus_width == SD_BUS_WIDE)
					cmd_ext_reg->data_rw_number = sd_mmc_info->blk_len * 8 + (16 - 1) * 4;
				else
					cmd_ext_reg->data_rw_number = sd_mmc_info->blk_len * 8 + (16 - 1);
			}
			else
			{
				cmd_send_reg->repeat_package_times = 0;
				if(sd_mmc_info->bus_width == SD_BUS_WIDE)
					cmd_ext_reg->data_rw_number = data_cnt * 8 + (16 - 1) * 4;
				else
					cmd_ext_reg->data_rw_number = data_cnt * 8 + (16 - 1);
			}

			if(arg & (1<<31))
			{
				cmd_send_reg->cmd_send_data = 1;
				//memcpy(sd_mmc_info->sd_mmc_buf, data_buf, data_cnt);
				//buffer = sd_mmc_info->sd_mmc_phy_buf;
				data_dma_to_device_addr=dma_map_single(NULL, (void *)data_buf, data_cnt, DMA_TO_DEVICE);	
				buffer = (unsigned char*)data_dma_to_device_addr;
			}
			else
			{
				cmd_send_reg->res_with_data = 1;
				//buffer = sd_mmc_info->sd_mmc_phy_buf;
				data_dma_from_device_addr = dma_map_single(NULL, (void *)data_buf, data_cnt, DMA_FROM_DEVICE );
				buffer = (unsigned char*)data_dma_from_device_addr;
			}
			break;

		////case SD_READ_DAT_UNTIL_STOP:
		case SD_SEND_NUM_WR_BLOCKS:
		
		case SD_MMC_PROGRAM_CSD:
		case SD_MMC_SEND_WRITE_PROT:
		case MMC_LOCK_UNLOCK:
		case SD_SEND_SCR:
		case SD_GEN_CMD:
			cmd_send_reg->res_with_data = 1;
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)
				cmd_ext_reg->data_rw_number = data_cnt * 8 + (16 - 1) * 4;
			else
				cmd_ext_reg->data_rw_number = data_cnt * 8 + 16 - 1;
			buffer = sd_mmc_info->sd_mmc_phy_buf;
			break;
			
		default:
			break;
			
	}

	//cmd with R1b
	switch(cmd)
	{
		case SD_MMC_STOP_TRANSMISSION:
		case SD_MMC_SET_WRITE_PROT:
		case SD_MMC_CLR_WRITE_PROT:
		case SD_MMC_ERASE:
		case MMC_LOCK_UNLOCK:
			cmd_send_reg->check_dat0_busy = 1;
			break;
		default:
			break;
			
	}

	//cmd with R3
	switch(cmd)
	{
		case MMC_SEND_OP_COND:
		case SD_APP_OP_COND:
		case IO_SEND_OP_COND:
			cmd_send_reg->res_without_crc7 = 1;
			break;
		default:
			break;
			
	}

	#define SD_MMC_CMD_COUNT				20000//20
	#define SD_MMC_READ_BUSY_COUNT		2000000//20
	#define SD_MMC_WRITE_BUSY_COUNT		50000000//500000
	//#define SD_MMC_WAIT_STOP_COUNT		100000000
	#define SD_MMC_RETRY_COUNT			2
    
    if(cmd_send_reg->cmd_send_data)
    {    
        if(cmd == SD_MMC_WRITE_MULTIPLE_BLOCK)
    	    timeout = SD_MMC_WRITE_BUSY_COUNT * (data_cnt/512);
    	else if(cmd == IO_RW_EXTENDED)
    		timeout = SD_MMC_WRITE_BUSY_COUNT * (cmd_send_reg->repeat_package_times + 1);
    	else
    	    timeout = SD_MMC_WRITE_BUSY_COUNT;
    }
    else
    {    
        if(cmd == SD_MMC_READ_MULTIPLE_BLOCK)
    	    timeout = SD_MMC_READ_BUSY_COUNT * (data_cnt/512);
    	else if(cmd == IO_RW_EXTENDED)
    		timeout = SD_MMC_READ_BUSY_COUNT * (cmd_send_reg->repeat_package_times + 1);
        else
    	    timeout = SD_MMC_CMD_COUNT;
    }
    if(cmd == SD_SWITCH_FUNCTION)
        timeout = SD_MMC_WRITE_BUSY_COUNT;
    if(cmd == SD_MMC_STOP_TRANSMISSION)
        timeout = SD_MMC_WAIT_STOP_COUNT;

	irq_config = READ_CBUS_REG(SDIO_IRQ_CONFIG);
	irq_config_reg = (void *)&irq_config;

	irq_config_reg->soft_reset = 1;
	WRITE_CBUS_REG(SDIO_IRQ_CONFIG, irq_config);

	status_irq = 0;
	status_irq_reg = (void *)&status_irq;
	status_irq_reg->if_int = 1;
	status_irq_reg->cmd_int = 1;
	status_irq_reg->timing_out_int = 1;
	if(timeout > (sd_mmc_info->sdio_clk_unit*0x1FFF)/1000)
	{
		status_irq_reg->timing_out_count = 0x1FFF;
		//sdio_timeout_int_times = (timeout*1000)/(sd_mmc_info->sdio_clk_unit*0x1FFF);
		sdio_timeout_int_times = timeout/(sd_mmc_info->sdio_clk_unit*0x1FFF/1000);
	}
	else
	{
		status_irq_reg->timing_out_count = (timeout/sd_mmc_info->sdio_clk_unit)*1000;
		sdio_timeout_int_times = 1;
	}
	WRITE_CBUS_REG(SDIO_STATUS_IRQ, status_irq);

	WRITE_CBUS_REG(CMD_ARGUMENT, arg);
	WRITE_CBUS_REG(SDIO_EXTENSION, cmd_ext);
	if(buffer != NULL)
	{
		WRITE_CBUS_REG(SDIO_M_ADDR, (unsigned long)buffer);
	}

	init_completion(&sdio_int_complete);
	sdio_open_host_interrupt(SDIO_CMD_INT);
	sdio_open_host_interrupt(SDIO_TIMEOUT_INT);

	WRITE_CBUS_REG(CMD_SEND, cmd_send);	//launching sdhc

	timeout =500;/*5s*/
	timeout = wait_for_completion_timeout(&sdio_int_complete,timeout);
	
	sdio_close_host_interrupt(SDIO_CMD_INT);
	sdio_close_host_interrupt(SDIO_TIMEOUT_INT);
	if(sdio_timeout_int_times == 0 || timeout == 0){
		ret = SD_MMC_ERROR_TIMEOUT;
		if(timeout == 0) {
			printk("[sd_send_cmd_hw] wait_for_completion_timeout for cmd(%d)=%s\n", cmd, cmd_strings[cmd]);
			ret = SD_WAIT_FOR_COMPLETION_TIMEOUT;
		}
		printk("sdio_timeout_int_times = %d; timeout = %d\n",sdio_timeout_int_times,timeout);
		goto error;
	}
	status_irq = READ_CBUS_REG(SDIO_STATUS_IRQ);
	if(cmd_send_reg->cmd_res_bits && !cmd_send_reg->res_without_crc7 && !status_irq_reg->res_crc7_ok && !sd_mmc_info->sdio_read_crc_close){
		ret = SD_MMC_ERROR_COM_CRC;
		goto error;
	}

	num_res = sd_get_response_length(res_type);
	
	if(num_res > 0)
	{
		unsigned long multi_config = 0;
		SDIO_Multi_Config_Reg_t *multi_config_reg = (void *)&multi_config;
		multi_config_reg->write_read_out_index = 1;
		WRITE_CBUS_REG(SDIO_MULT_CONFIG, multi_config);

		num_res--;		// Minus CRC byte
	}
	while(num_res)
	{
		unsigned long data_temp = READ_CBUS_REG(CMD_ARGUMENT);
		
		res_buf[--num_res] = data_temp & 0xFF;
		if(num_res <= 0)
			break;
		res_buf[--num_res] = (data_temp >> 8) & 0xFF;
		if(num_res <= 0)
			break;
		res_buf[--num_res] = (data_temp >> 16) & 0xFF;
		if(num_res <= 0)
			break;
		res_buf[--num_res] = (data_temp >> 24) & 0xFF;
	}
	
	ret = sd_check_response(cmd, res_type, res_buf);
	if(ret)
		goto error;

	//cmd with adtc
	switch(cmd)
	{
		////case SD_READ_DAT_UNTIL_STOP:
		case SD_MMC_READ_SINGLE_BLOCK:
		case SD_MMC_READ_MULTIPLE_BLOCK:
		case SD_SWITCH_FUNCTION:
                case MMC_SEND_EXT_CSD:
			if(!status_irq_reg->data_read_crc16_ok){
				ret = SD_MMC_ERROR_DATA_CRC;
				goto error;
			}
			break;
		case SD_MMC_WRITE_BLOCK:
		case SD_MMC_WRITE_MULTIPLE_BLOCK:
		case SD_MMC_PROGRAM_CSD:
			if(!status_irq_reg->data_write_crc16_ok){
				ret =  SD_MMC_ERROR_DATA_CRC;
				goto error;
			}
			break;
		case SD_SEND_NUM_WR_BLOCKS:
		case SD_MMC_SEND_WRITE_PROT:
		case MMC_LOCK_UNLOCK:
		case SD_SEND_SCR:
		case SD_GEN_CMD:
			if(!status_irq_reg->data_read_crc16_ok){
				ret = SD_MMC_ERROR_DATA_CRC;
				goto error;
			}
			break;

		case IO_RW_EXTENDED:
			if(arg & (1<<31))
			{
				if(!status_irq_reg->data_write_crc16_ok){
					ret =  SD_MMC_ERROR_DATA_CRC;
					goto error;
				}
			}
			else
			{
				if(!sd_mmc_info->sdio_read_crc_close)
				{
					if(!status_irq_reg->data_read_crc16_ok){
						ret = SD_MMC_ERROR_DATA_CRC;
						goto error;
					}
				}
			}
			break;
		default:
			break;
			
	}
	/*error need dma_unmap_single also*/
error:
	if(data_dma_from_device_addr)
	{
		dma_unmap_single(NULL, data_dma_from_device_addr, data_cnt, DMA_FROM_DEVICE);
	}
	if(data_dma_to_device_addr)
	{
		dma_unmap_single(NULL, data_dma_to_device_addr, data_cnt, DMA_TO_DEVICE);
	}
	if(cmd_send_reg->res_with_data && buffer && (data_buf != sd_mmc_info->sd_mmc_buf)
		&& (!data_dma_from_device_addr) && (!data_dma_to_device_addr))
	{
		printk("Not sd_mmc_buf2, copying\n");
		memcpy(data_buf, sd_mmc_info->sd_mmc_buf, data_cnt);
	}

	return ret;
}
#endif

//Read SD Response Data
int sd_read_response_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long byte_cnt, unsigned char * res_buf)
{
	unsigned long data_cnt, num_ncr = 0;
	unsigned char data, temp;
	int busy = 1, i;
	
	if(!byte_cnt)
		return SD_MMC_NO_ERROR;

	memset(res_buf, 0, byte_cnt);
	
	sd_set_cmd_input();
	
	if(sd_mmc_info->operation_mode == CARD_INDENTIFICATION_MODE)
	{
		//wait until cmd line is valid
		do
		{
			sd_clk_identify_low();
		
			data = sd_get_cmd_value();
			if(!data)
			{
				busy = 0;
				break;
			}
		
			sd_clk_identify_high();
			
			num_ncr++;
		
		}while(busy && (num_ncr < SD_MMC_TIME_NCR_MAX));

		if(num_ncr >= SD_MMC_TIME_NCR_MAX)
			return SD_MMC_ERROR_TIMEOUT;

		for(data_cnt = 0; data_cnt < byte_cnt; data_cnt++)
		{
			temp = 0;
			
			for(i=0; i<8; i++)
			{
				sd_clk_identify_low();
				
				data = sd_get_cmd_value();
				temp <<= 1;
				temp |= data;
							
				sd_clk_identify_high();
			}
			
			*res_buf = temp;
			res_buf++;
		}
	}
	else    // Tranfer mode
	{
		//wait until cmd line is valid
		do
		{
			sd_clk_transfer_low();
		
			data = sd_get_cmd_value();
			if(!data)
			{
				busy = 0;
				break;
			}
		
			sd_clk_transfer_high();
			
			num_ncr++;
		
		}while(busy && (num_ncr < SD_MMC_TIME_NCR_MAX));

		if(num_ncr >= SD_MMC_TIME_NCR_MAX)
			return SD_MMC_ERROR_TIMEOUT;

		for(data_cnt = 0; data_cnt < byte_cnt; data_cnt++)
		{
			temp = 0;
			
			for(i=0; i<8; i++)
			{
				sd_clk_transfer_low();
				
				data = sd_get_cmd_value();
				temp <<= 1;
				temp |= data;
							
				sd_clk_transfer_high();
			}
			
			*res_buf = temp;
			res_buf++;
		}
	}

	return SD_MMC_NO_ERROR;
}

//Check R1 response and return the result
int sd_check_response_r1(unsigned char cmd, SD_Response_R1_t * r1)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		return SD_MMC_NO_ERROR;
		//if(0)//if ((r1->command & 0x3F) != cmd)
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{	
		if (r1->command != cmd)
			return SD_MMC_ERROR_HEADER_MISMATCH;		
		if (r1->card_status.OUT_OF_RANGE)
			return SD_MMC_ERROR_OUT_OF_RANGE;
		else if (r1->card_status.ADDRESS_ERROR)
			return SD_MMC_ERROR_ADDRESS;
		else if (r1->card_status.BLOCK_LEN_ERROR)
			return SD_MMC_ERROR_BLOCK_LEN;
		else if (r1->card_status.ERASE_SEQ_ERROR)
			return SD_MMC_ERROR_ERASE_SEQ;
		else if (r1->card_status.ERASE_PARAM)
			return SD_MMC_ERROR_ERASE_PARAM;
		else if (r1->card_status.WP_VIOLATION)
			return SD_MMC_ERROR_WP_VIOLATION;
		else if (r1->card_status.CARD_IS_LOCKED)
			return SD_ERROR_CARD_IS_LOCKED;
		else if (r1->card_status.LOCK_UNLOCK_FAILED)
			return SD_ERROR_LOCK_UNLOCK_FAILED;
		else if (r1->card_status.COM_CRC_ERROR)
			return SD_MMC_ERROR_COM_CRC;
		else if (r1->card_status.ILLEGAL_COMMAND)
			return SD_MMC_ERROR_ILLEGAL_COMMAND;
		else if (r1->card_status.CARD_ECC_FAILED)
			return SD_ERROR_CARD_ECC_FAILED;
		else if (r1->card_status.CC_ERROR)
			return SD_ERROR_CC;
		else if (r1->card_status.ERROR)
			return SD_MMC_ERROR_GENERAL;
		else if (r1->card_status.CID_CSD_OVERWRITE)
			return SD_MMC_ERROR_CID_CSD_OVERWRITE;
		else if (r1->card_status.AKE_SEQ_ERROR)
			return SD_ERROR_AKE_SEQ;	
	}
#endif

	return SD_MMC_NO_ERROR;	
}

//Check R3 response and return the result
int sd_check_response_r3(unsigned char cmd, SD_Response_R3_t * r3)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if(0)//if ((r3->reserved1 & 0x3F) != 0x3F)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{	
		if ((r3->reserved1 != 0x3F))// || (r3->reserved2 != 0x7F))
		return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif	

	return SD_MMC_NO_ERROR;
}

int sd_check_response_r4(unsigned char cmd, SDIO_Response_R4_t * r4)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if(0)//if ((r3->reserved1 & 0x3F) != 0x3F)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{	
		if ((r4->reserved1 != 0x3F))// || (r3->reserved2 != 0x7F))
		return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif	

	return SD_MMC_NO_ERROR;
}

int sd_check_response_r5(unsigned char cmd, SDIO_RW_CMD_Response_R5_t * r5)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{	
		if(0)//if ((r6->command & 0x3F) != SD_MMC_SEND_RELATIVE_ADDR)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{
		if ((r5->command == IO_RW_DIRECT) || (r5->command == IO_RW_EXTENDED))
			return SD_MMC_NO_ERROR;
		
		return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif

	return SD_MMC_NO_ERROR;
}

//Check R6 response and return the result
int sd_check_response_r6(unsigned char cmd, SD_Response_R6_t * r6)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{	
		if(0)//if ((r6->command & 0x3F) != SD_MMC_SEND_RELATIVE_ADDR)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{
		if (r6->command != SD_MMC_SEND_RELATIVE_ADDR)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif

	return SD_MMC_NO_ERROR;
}

int sd_check_response_r7(unsigned char cmd, SD_Response_R7_t * r7)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{	
		if(0)//if ((r6->command & 0x3F) != SD_MMC_SEND_RELATIVE_ADDR)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{
		if (r7->command != SD_SEND_IF_COND)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif

	return SD_MMC_NO_ERROR;	
}

//Check R2_CID response and return the result
int sd_check_response_r2_cid(unsigned char cmd, SD_Response_R2_CID_t * r2_cid)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{	
		if(0)//if ((r2_cid->reserved & 0x3F) != 0x3F)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{
		if (r2_cid->reserved != 0x3F)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif	

	return SD_MMC_NO_ERROR;
}

//Check R2_CSD response and return the result
int sd_check_response_r2_csd(unsigned char cmd, SD_Response_R2_CSD_t * r2_csd)
{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if(0)//if ((r2_csd->reserved & 0x3F)!= 0x3F)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{
		if(r2_csd->reserved != 0x3F)
			return SD_MMC_ERROR_HEADER_MISMATCH;
	}
#endif	

	return SD_MMC_NO_ERROR;
}

//Check response and return the result
int sd_check_response(unsigned char cmd, SD_Response_Type_t res_type, unsigned char * res_buf)
{
	int ret = SD_MMC_NO_ERROR;
	
	switch(res_type)
	{
		case RESPONSE_R1:
		case RESPONSE_R1B:
			ret = sd_check_response_r1(cmd, (SD_Response_R1_t *)res_buf);
			break;
		
		case RESPONSE_R3:
			ret = sd_check_response_r3(cmd, (SD_Response_R3_t *)res_buf);
			break;
	   
		case RESPONSE_R4:
			ret = sd_check_response_r4(cmd, (SDIO_Response_R4_t *)res_buf);
			break;
		case RESPONSE_R5:
			ret = sd_check_response_r5(cmd, (SDIO_RW_CMD_Response_R5_t *)res_buf);
			break;
		case RESPONSE_R6:
			ret = sd_check_response_r6(cmd, (SD_Response_R6_t *)res_buf);
			break;
		
		case RESPONSE_R2_CID:
			ret = sd_check_response_r2_cid(cmd, (SD_Response_R2_CID_t *)res_buf);
			break;
		 
		case RESPONSE_R2_CSD:
			ret = sd_check_response_r2_csd(cmd, (SD_Response_R2_CSD_t *)res_buf);
			break;
		
		case RESPONSE_NONE:
			break;
			
		default:
			break;
	}
	
	return ret;
}

//static void my_disp_sdhc_regs(void);

void try_to_fix_error(SD_MMC_Card_Info_t *sd_mmc_info)
{
	unsigned int sdxc_srst;
	SDXC_PDMA_Reg_t *sdxc_pdma_reg;
	unsigned int sdxc_pdma;

	if ((sd_mmc_info->io_pad_type != SDXC_CARD_0_5) && 
		(sd_mmc_info->io_pad_type != SDXC_BOOT_0_11) &&
		(sd_mmc_info->io_pad_type != SDXC_GPIOX_0_9))
		return;

	printk("try to fix error\n");
	//my_disp_sdhc_regs();
	{
		unsigned int value4, value6, value2, value3;
		
		value4 = READ_CBUS_REG(PERIPHS_PIN_MUX_4);
		value6 = READ_CBUS_REG(PERIPHS_PIN_MUX_6);
		value2 = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
		value3 = READ_CBUS_REG(PERIPHS_PIN_MUX_3);
		printk("MUX4:[0x%08x]/[0x%02x], MUX6:[0x%08x]/[0x%02x], MUX2:[0x%08x]/[0x%03x], MUX3:[0x%08x]/[0x%02x]\n", 
			value4, (value4 >> 26) & 0x1F, value6, (value6 >> 24) & 0x3F, value2, (value2 >> 16) & 0xFFF, value3, (value3 >> 31) & 0x01);
	}
	{
		unsigned int value, value2, value3;
		
		value = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
		value2 = READ_CBUS_REG(PERIPHS_PIN_MUX_4);
		value3 = READ_CBUS_REG(PERIPHS_PIN_MUX_5);
		printk("MUX2-sdxc:[0x%08x]/[0x%02x], MUX4:[0x%08x]/[0x%02x], MUX5:[0x%08x]/[0x%02x]\n", 
			value, (value >> 4) & 0x1F, value2, (value2 >> 26) & 0x1F, value3, (value3 >> 10) & 0x1F);
	
		value = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
		value2 = READ_CBUS_REG(PERIPHS_PIN_MUX_6);
		value3 = READ_CBUS_REG(PERIPHS_PIN_MUX_8);
		printk("MUX2-sdhc:[0x%08x]/[0x%02x], MUX6:[0x%08x]/[0x%02x], MUX8:[0x%08x]/[0x%02x]\n", 
			value, (value >> 10) & 0x3F, value2, (value2 >> 24) & 0x3F, value3, (value3 >> 0) & 0x3F);
	}

	sdxc_pdma = READ_CBUS_REG(SD_REG6_PDMA);
	sdxc_pdma_reg = (void *) &sdxc_pdma;
	sdxc_pdma_reg->rx_manual_flush = 1;
	WRITE_CBUS_REG(SD_REG6_PDMA, sdxc_pdma);	
	
	sdxc_srst = 0x3F;	//0x3F -- 0x27 : exclude rx_dphy_srst & tx_dphy_srst
	WRITE_CBUS_REG(SD_REGB_SRST, sdxc_srst);	
	sd_delay_ms(1);

	sdxc_srst = 0x00;
	WRITE_CBUS_REG(SD_REGB_SRST, sdxc_srst);	
	sd_delay_ms(1);
}

//Read single block data from SD card
#ifdef SD_MMC_HW_CONTROL
int sd_read_single_block_hw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned char * data_buf)
{
	int ret, read_retry_count, read_single_block_hw_failed = 0;
	unsigned long data_addr;
	unsigned char response[MAX_RESPONSE_BYTES];

	if ((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	{
		data_addr = lba;
	}
	else
	{
	    data_addr = sd_mmc_info->blk_len;
	    data_addr *= lba;
	}

    for(read_retry_count=0; read_retry_count<3; read_retry_count++)
    {
	    ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_READ_SINGLE_BLOCK, data_addr, RESPONSE_R1, response, data_buf, sd_mmc_info->blk_len, 1);
	    if(ret)
	    {
	    	printk(" sd_read_single_block_hw ret %d \n", ret);
	        read_single_block_hw_failed++;
	        continue;
	    }
	    else
		    break;
    }

    if(read_single_block_hw_failed >= 3)
        return SD_MMC_ERROR_READ_DATA_FAILED;

    if(ret)
        return ret;

	return SD_MMC_NO_ERROR;
}
#endif

//Read single block data from SD card
#ifdef SD_MMC_SW_CONTROL
int sd_read_single_block_sw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned char * data_buf)
{
	unsigned long data = 0, res = 0, temp = 0;
	int ret, data_busy = 1, res_busy = 1;
	
	unsigned long res_cnt = 0, data_cnt = 0, num_nac = 0, num_ncr = 0;
	
	unsigned short crc16_array[4] = {0, 0, 0, 0};
	unsigned short crc16 = 0;
	
	unsigned char response[MAX_RESPONSE_BYTES];
	
	unsigned long data_addr, loop_num;
	
	int i,j;

#ifdef SD_MMC_CRC_CHECK
	unsigned short crc_check = 0, crc_check_array[4]={0,0,0,0};
	int error=0;
	//unsigned char *org_buf=data_buf;
#endif
	
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	{
		sd_set_dat0_3_input();
	}
	else
	{
		sd_set_dat0_input();
	}

	if ((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	{
		data_addr = lba;
	}
	else
	{
	data_addr = sd_mmc_info->blk_len;
	data_addr *= lba;
	}
	
	ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_READ_SINGLE_BLOCK, data_addr, RESPONSE_NONE, 0);
	if(ret)
		return ret;
	
	sd_clear_response(response);
	sd_delay_clocks_z(sd_mmc_info, SD_MMC_Z_CMD_TO_RES);
	
	sd_set_cmd_input();
	//wait until both response and data is valid    
	do
	{
		sd_clk_transfer_low();
		
		res = sd_get_cmd_value();
		data = sd_get_dat0_value();
		
		if (res_busy)
		{
			if (res)
				num_ncr++;
			else
				res_busy = 0;
		}
		else
		{
			if (res_cnt < (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8))
			{
				response[res_cnt>>3] <<= 1;
				response[res_cnt>>3] |= res;
				
				res_cnt++;
			}
		}
		
		if (data_busy)
		{
			if (data)
				num_nac++;
			else
				data_busy = 0;
		}
		else
		{
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
			{
				data = sd_get_dat0_3_value();
				temp <<= 4;
				temp |= data;
#ifdef SD_MMC_CRC_CHECK
				SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
				SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
				SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
				SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
				if((data_cnt & 0x01) == 1)
				{
#ifdef AMLOGIC_CHIP_SUPPORT
					if((unsigned long)data_buf == 0x3400000)
					{
						WRITE_BYTE_TO_FIFO(temp);
					}
					else
#endif
					{
						*data_buf = temp;
						data_buf++;
					}

					temp = 0;   //one byte received, clear temp varialbe
				}                   
			}
			else                //only data0 lines
			{
				data = sd_get_dat0_value();
				temp <<= 1;
				temp |= data;
				if((data_cnt & 0x07) == 7)
				{
#ifdef AMLOGIC_CHIP_SUPPORT
					if((unsigned)data_buf == 0x3400000)
					{
						WRITE_BYTE_TO_FIFO(temp);
					}
					else
#endif
					{
						*data_buf = temp;
						data_buf++;
					}

#ifdef SD_MMC_CRC_CHECK
					crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
					temp = 0;   //one byte received, clear temp varialbe
				}
			}
			data_cnt++;
		}
		
		sd_clk_transfer_high();
		
		if(!res_busy && !data_busy)
		{
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
			{
				if((res_cnt >= (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8)) && ((data_cnt&0x01) == 0))
				{
					data_cnt >>= 1;
					break;
				}
			}
			else
			{
				if((res_cnt >= (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8)) && ((data_cnt&0x07) == 0))
				{
					data_cnt >>= 3;
					break;
				}
			}
		}

	}while((num_ncr < SD_MMC_TIME_NCR_MAX) && (num_nac < sd_mmc_info->clks_nac));
	
	if((num_ncr >= SD_MMC_TIME_NCR_MAX) || (num_nac >= sd_mmc_info->clks_nac))
		return SD_MMC_ERROR_TIMEOUT;

	//Read data and response
	loop_num = sd_mmc_info->blk_len;
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
	{
#ifdef AMLOGIC_CHIP_SUPPORT
		if((unsigned long)data_buf == 0x3400000)
		{
			for(; data_cnt < loop_num; data_cnt++)
			{
				temp = 0;   //clear temp varialbe
			
				for(i = 0; i < 2; i++)
				{
					sd_clk_transfer_low();
		
					data = sd_get_dat0_3_value();
					temp <<= 4;
					temp |= data;
				
					sd_clk_transfer_high();
					
#ifdef SD_MMC_CRC_CHECK
					SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
					SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
					SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
					SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
				}
				
				WRITE_BYTE_TO_FIFO(temp);
			}
		}
		else
#endif
		{
			for(; data_cnt < loop_num; data_cnt++)
			{
				temp = 0;   //clear temp varialbe
			
				for(i = 0; i < 2; i++)
				{
					sd_clk_transfer_low();
		
					data = sd_get_dat0_3_value();
					temp <<= 4;
					temp |= data;
				
					sd_clk_transfer_high();
					
#ifdef SD_MMC_CRC_CHECK
					SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
					SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
					SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
					SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
				}
				
				*data_buf = temp;
				data_buf++;
			}
		}
		
		//Read CRC16 data
		for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
		{
			sd_clk_transfer_low();
		
			crc16_array[0] <<= 1;
			crc16_array[1] <<= 1;
			crc16_array[2] <<= 1;
			crc16_array[3] <<= 1;
			data = sd_get_dat0_3_value();
			crc16_array[0] |= (data & 0x01);
			crc16_array[1] |= ((data >> 1) & 0x01);
			crc16_array[2] |= ((data >> 2) & 0x01);
			crc16_array[3] |= ((data >> 3) & 0x01);

			sd_clk_transfer_high();
		}
		
#ifdef SD_MMC_CRC_CHECK
		for(i=0; i<4; i++)
		{
			//crc_check_array[i] = sd_cal_crc_mode(org_buf, sd_mmc_info->blk_len, char_mode[i]);
			if(crc16_array[i] != crc_check_array[i])
			{
				error = SD_MMC_ERROR_DATA_CRC;
				break;
			}
		}
#endif
	}
	else    //only data0 lines
	{
#ifdef AMLOGIC_CHIP_SUPPORT
		if((unsigned)data_buf == 0x3400000)
		{
			for(; data_cnt < loop_num; data_cnt++)
			{
				temp = 0;   //clear temp varialbe
			
				for(j = 0; j < 8; j++)
				{
					sd_clk_transfer_low();
				
					data = sd_get_dat0_value();
					temp <<= 1;
					temp |= data;
				
					sd_clk_transfer_high();
				}
				
				WRITE_BYTE_TO_FIFO(temp);
				
#ifdef SD_MMC_CRC_CHECK
				crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
			}
		}
		else
#endif
		{
			for(; data_cnt < loop_num; data_cnt++)
			{
				temp = 0;   //clear temp varialbe
			
				for(j = 0; j < 8; j++)
				{
					sd_clk_transfer_low();
				
					data = sd_get_dat0_value();
					temp <<= 1;
					temp |= data;
				
					sd_clk_transfer_high();
				}
				
				*data_buf = temp;
				data_buf++;
				
#ifdef SD_MMC_CRC_CHECK
				crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
			}
		}

		//Read CRC16 data
		for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
		{
			sd_clk_transfer_low();
		
			data = sd_get_dat0_value();
			crc16 <<= 1;
			crc16 |= data;

			sd_clk_transfer_high();
		}
		
#ifdef SD_MMC_CRC_CHECK
		if(crc16 != crc_check)
			error = SD_MMC_ERROR_DATA_CRC;
#endif
	}

	sd_clk_transfer_low();      //for end bit
	sd_clk_transfer_high();
	
	sd_delay_clocks_z(sd_mmc_info, SD_MMC_TIME_NRC_NCC);     //Clock delay, Z type

#ifdef SD_MMC_CRC_CHECK
	if(error == SD_MMC_ERROR_DATA_CRC)
	{
		//#ifdef  SD_MMC_DEBUG
		//Debug_Printf("#%s error occured in sd_read_single_block()!\n", sd_error_to_string(error));
		//#endif
		return error;
	}
#endif

	return SD_MMC_NO_ERROR;
}
#endif

#ifdef SD_MMC_HW_CONTROL
int sd_read_multi_block_hw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long lba_cnt, unsigned char * data_buf)
{
	int ret, read_retry_count, read_multi_block_hw_failed = 0;
	unsigned long data_addr, lba_num, data_offset = 0;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned char * orig_phy_buf = sd_mmc_info->sd_mmc_phy_buf;
	unsigned char * orig_virt_buf = sd_mmc_info->sd_mmc_buf;
	
	if(lba_cnt == 0)
		return SD_MMC_ERROR_BLOCK_LEN;

	if ((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	{
		data_addr = lba;
	}
	else
	{
		data_addr = sd_mmc_info->blk_len;
		data_addr *= lba;
	}
	
        while(lba_cnt)
        {
            if(lba_cnt > sd_mmc_info->max_blk_count)
                lba_num = sd_mmc_info->max_blk_count;
            else
                lba_num = lba_cnt;

            if((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
                data_addr += data_offset/512;
            else
                data_addr += data_offset;

            data_buf += data_offset;
			sd_mmc_info->sd_mmc_phy_buf += data_offset;
            for(read_retry_count=0; read_retry_count<3; read_retry_count++)
            {
	            ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_READ_MULTIPLE_BLOCK, data_addr, RESPONSE_R1, response, data_buf, sd_mmc_info->blk_len*lba_num, 1);
	            if(ret)
	            {
	                read_multi_block_hw_failed++;
	                ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
	                if(ret)
	                    goto out;
					else {
						try_to_fix_error(sd_mmc_info);
						continue;
					}
                }
                else
                    break;
            }

            if(read_multi_block_hw_failed >= 3)
            {
				ret = SD_MMC_ERROR_READ_DATA_FAILED;
                goto out;
            }	

	        if(ret)
	        { 
                goto out;
            }
            else	//fix stop
            {        
	            ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
	            if(ret)
	                goto out;
	        }

            lba_cnt -= lba_num;
            data_offset = lba_num*512;
        }

	ret = SD_MMC_NO_ERROR;
	
out : 
	sd_mmc_info->sd_mmc_phy_buf = orig_phy_buf;
	sd_mmc_info->sd_mmc_buf = orig_virt_buf;
	return ret;
}
#endif

#ifdef SD_MMC_SW_CONTROL
int sd_read_multi_block_sw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long lba_cnt, unsigned char * data_buf)
{
	unsigned long data = 0, res = 0, temp = 0;
	int ret, data_busy = 1, res_busy = 1;
	
	unsigned long res_cnt = 0, data_cnt = 0, num_nac = 0, num_ncr = 0;
	
	unsigned short crc16_array[4] = {0, 0, 0, 0};
	unsigned short crc16 = 0;
	
	unsigned char response[MAX_RESPONSE_BYTES];
	
	unsigned long data_addr,loop_num,blk_cnt;
	
	int i,j;

#ifdef SD_MMC_CRC_CHECK
	unsigned short crc_check = 0, crc_check_array[4]={0,0,0,0};
	int error=0;
	unsigned char *org_buf=data_buf;
#endif
	
	if(lba_cnt == 0)
		return SD_MMC_ERROR_BLOCK_LEN;
		
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	{
		sd_set_dat0_3_input();
	}
	else
	{
		sd_set_dat0_input();
	}

	if ((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	{
		data_addr = lba;
	}
	else
	{
        data_addr = sd_mmc_info->blk_len;
        data_addr *= lba;
	}
	ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_READ_MULTIPLE_BLOCK, data_addr, RESPONSE_NONE, 0);
	if(ret)
		return ret;
	
	sd_clear_response(response);
	sd_delay_clocks_z(sd_mmc_info, SD_MMC_Z_CMD_TO_RES);
	
	sd_set_cmd_input();
	//wait until both response and data is valid    
	do
	{
		sd_clk_transfer_low();
		
		res = sd_get_cmd_value();
		data = sd_get_dat0_value();
		
		if (res_busy)
		{
			if (res)
				num_ncr++;
			else
				res_busy = 0;
		}
		else
		{
			if (res_cnt < (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8))
			{
				response[res_cnt>>3] <<= 1;
				response[res_cnt>>3] |= res;
				
				res_cnt++;
			}
		}
		
		if (data_busy)
		{
			if (data)
				num_nac++;
			else
				data_busy = 0;
		}
		else
		{
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
			{
				data = sd_get_dat0_3_value();
				temp <<= 4;
				temp |= data;
#ifdef SD_MMC_CRC_CHECK
				SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
				SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
				SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
				SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
				if((data_cnt & 0x01) == 1)
				{
#ifdef AMLOGIC_CHIP_SUPPORT
					if((unsigned long)data_buf == 0x3400000)
					{
						WRITE_BYTE_TO_FIFO(temp);
					}
					else
#endif
					{
						*data_buf = temp;
						data_buf++;
					}

					temp = 0;   //one byte received, clear temp varialbe
				}                   
			}
			else                //only data0 lines
			{
				data = sd_get_dat0_value();
				temp <<= 1;
				temp |= data;
				if((data_cnt & 0x07) == 7)
				{
#ifdef AMLOGIC_CHIP_SUPPORT
					if((unsigned)data_buf == 0x3400000)
					{
						WRITE_BYTE_TO_FIFO(temp);
					}
					else
#endif
					{
						*data_buf = temp;
						data_buf++;
					}

#ifdef SD_MMC_CRC_CHECK
					crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
					temp = 0;   //one byte received, clear temp varialbe
				}
			}
			data_cnt++;
		}
			
		sd_clk_transfer_high();
		
		if(!res_busy && !data_busy)
		{
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
			{
				if((res_cnt >= (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8)) && ((data_cnt&0x01) == 0))
				{
					data_cnt >>= 1;
					break;
				}
			}
			else
			{
				if((res_cnt >= (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8)) && ((data_cnt&0x07) == 0))
				{
					data_cnt >>= 3;
					break;
				}
			}
		}

	}while((num_ncr < SD_MMC_TIME_NCR_MAX) && (num_nac < sd_mmc_info->clks_nac));
	
	if((num_ncr >= SD_MMC_TIME_NCR_MAX) || (num_nac >= sd_mmc_info->clks_nac))
		return SD_MMC_ERROR_TIMEOUT;

	//Read all data blocks
	loop_num = sd_mmc_info->blk_len;
	for (blk_cnt = 0; blk_cnt < lba_cnt; blk_cnt++)
	{
		//wait until data is valid
		num_nac = 0;    
		do
		{   
			if(!data_busy)
				break;
				
			sd_clk_transfer_low();
		
			data = sd_get_dat0_value();
		
			if(data)
			{
				num_nac++;
			}
			else
			{
				data_busy = 0;
			}
		
			sd_clk_transfer_high();

		}while(data_busy && (num_nac < sd_mmc_info->clks_nac));
		
		if(num_nac >= sd_mmc_info->clks_nac)
			return SD_MMC_ERROR_TIMEOUT;
		
		//Read data
		if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
		{
#ifdef AMLOGIC_CHIP_SUPPORT
			if((unsigned long)data_buf == 0x3400000)
			{
				for(; data_cnt < loop_num; data_cnt++)
				{
					temp = 0;   //clear temp varialbe
				
					for(i = 0; i < 2; i++)
					{
						sd_clk_transfer_low();
		
						data = sd_get_dat0_3_value();
						temp <<= 4;
						temp |= data;
					
						sd_clk_transfer_high();
						
#ifdef SD_MMC_CRC_CHECK
						SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
						SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
						SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
						SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
					}
					
					WRITE_BYTE_TO_FIFO(temp);
				}
			}
			else
#endif
			{
				for(; data_cnt < loop_num; data_cnt++)
				{
					temp = 0;   //clear temp varialbe
				
					for(i = 0; i < 2; i++)
					{
						sd_clk_transfer_low();
						
						data = sd_get_dat0_3_value();
						temp <<= 4;
						temp |= data;
					
						sd_clk_transfer_high();
						
#ifdef SD_MMC_CRC_CHECK
						SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
						SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
						SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
						SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
					}
					
					*data_buf = temp;
					data_buf++;
				}
			}
			
			//Read CRC16 data
			for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
			{
				sd_clk_transfer_low();
		
				crc16_array[0] <<= 1;
				crc16_array[1] <<= 1;
				crc16_array[2] <<= 1;
				crc16_array[3] <<= 1;
				data = sd_get_dat0_3_value();
				crc16_array[0] |= (data & 0x01);
				crc16_array[1] |= ((data >> 1) & 0x01);
				crc16_array[2] |= ((data >> 2) & 0x01);
				crc16_array[3] |= ((data >> 3) & 0x01);
			
				sd_clk_transfer_high();
			}
			
#ifdef SD_MMC_CRC_CHECK
			for(i=0; i<4; i++)
			{
				//crc_check_array[i] = sd_cal_crc_mode(org_buf, sd_mmc_info->blk_len, char_mode[i]);
				if(crc16_array[i] != crc_check_array[i])
				{
					error = SD_MMC_ERROR_DATA_CRC;
					break;
				}
			}
#endif
		}
		else    //only data0 lines
		{
#ifdef AMLOGIC_CHIP_SUPPORT
			if((unsigned long)data_buf == 0x3400000)
			{
				for(; data_cnt < loop_num; data_cnt++)
				{
					temp = 0;   //clear temp varialbe
				
					for(j = 0; j < 8; j++)
					{
						sd_clk_transfer_low();
					
						data = sd_get_dat0_value();
						temp <<= 1;
						temp |= data;
					
						sd_clk_transfer_high();
					}
					
					WRITE_BYTE_TO_FIFO(temp);
					
#ifdef SD_MMC_CRC_CHECK
					crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
				}
			}
			else
#endif
			{
				for(; data_cnt < loop_num; data_cnt++)
				{
					temp = 0;   //clear temp varialbe
				
					for(j = 0; j < 8; j++)
					{
						sd_clk_transfer_low();
					
						data = sd_get_dat0_value();
						temp <<= 1;
						temp |= data;
					
						sd_clk_transfer_high();
					}
					
					*data_buf = temp;
					data_buf++;
					
#ifdef SD_MMC_CRC_CHECK
					crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
				}
			}

			//Read CRC16 data
			for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
			{
				sd_clk_transfer_low();
		
				data = sd_get_dat0_value();
				crc16 <<= 1;
				crc16 |= data;      
			
				sd_clk_transfer_high();
			}
			
#ifdef SD_MMC_CRC_CHECK
			if(crc16 != crc_check)
				error = SD_MMC_ERROR_DATA_CRC;
#endif
		}
		
		sd_clk_transfer_low();      //for end bit
		sd_clk_transfer_high();
		
		data_busy = 1;
		data_cnt = 0;
		
#ifdef SD_MMC_CRC_CHECK
		org_buf = data_buf;
		crc_check = 0;
		crc_check_array[0] = crc_check_array[1] = crc_check_array[2] = crc_check_array[3] = 0;
#endif
	}

	sd_delay_clocks_z(sd_mmc_info, SD_MMC_TIME_NRC_NCC);     //Clock delay, Z type
	
	ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response);

#ifdef SD_MMC_CRC_CHECK
	if(error == SD_MMC_ERROR_DATA_CRC)
	{
		//#ifdef  SD_MMC_DEBUG
		//Debug_Printf("#%s error occured in sd_read_multi_block()!\n", sd_error_to_string(error));
		//#endif
		return error;
	}
#endif
	
	return ret;
}
#endif 

#ifdef SD_MMC_HW_CONTROL
int sd_write_single_block_hw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned char * data_buf)
{
	int ret, write_retry_count;
	unsigned long data_addr;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned char *status_data_buf = sd_mmc_info->sd_mmc_buf;

	if ((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	{
		data_addr = lba;
	}
	else
	{
        data_addr = sd_mmc_info->blk_len;
        data_addr *= lba;
	}
	
	for(write_retry_count=0; write_retry_count<4; write_retry_count++)
	{
	    ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_WRITE_BLOCK, data_addr, RESPONSE_R1, response, data_buf, sd_mmc_info->blk_len, 1);
		if(ret == SD_MMC_ERROR_DATA_CRC || ret == SD_MMC_ERROR_COM_CRC)
		{
		    if(sd_mmc_info->spec_version || sd_mmc_info->card_type == CARD_TYPE_SDHC)
		    {  
                memset(status_data_buf, 0, 64);
#ifdef SD_MMC_HW_CONTROL
	            if(SD_WORK_MODE == CARD_HW_MODE)
		            ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x00FFFF01, RESPONSE_R1, response, status_data_buf, 64, 1);
#endif
            }
            //for some sdhc card write problem on 7216 picframe
			continue;
		}
		else
		{    
			break;
		}
    }

    if(write_retry_count >= 4)
        return SD_MMC_ERROR_DATA_CRC;

    if(ret)
        return ret;

	return SD_MMC_NO_ERROR;
}
#endif

#ifdef SD_MMC_SW_CONTROL
int sd_write_single_block_sw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned char * data_buf)
{
	int ret, i, j;
	unsigned long crc_status, data;
	
	unsigned long data_cnt = 0;
	
	unsigned char * org_buf = data_buf;
	unsigned short crc16_array[4] = {0, 0, 0, 0};
	unsigned short crc16;
		
	unsigned char response[MAX_RESPONSE_BYTES];
	
	unsigned long data_addr,loop_num;
	
	//Set data lines busy
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	{
		sd_set_dat0_3_output();
		
		sd_clk_transfer_low();

		sd_set_dat0_3_value(0x0F);
		
		sd_clk_transfer_high();
	}
	else
	{
		sd_set_dat0_output();
		
		sd_clk_transfer_low();
		
		sd_set_dat0_value(0x01);
		
		sd_clk_transfer_high();
	}

	if ((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	{
		data_addr = lba;
	}
	else
	{
	data_addr = sd_mmc_info->blk_len;
	data_addr *= lba;
	}
	
	ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_WRITE_BLOCK, data_addr, RESPONSE_R1, response);
	if(ret)
		return ret;
		
	//Nwr cycles delay
	sd_delay_clocks_h(sd_mmc_info, SD_MMC_TIME_NWR);
	
	//Start bit
	sd_clk_transfer_low();
	
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	{
		sd_set_dat0_3_value(0x00);
	}
	else
	{
		sd_set_dat0_value(0x00);
	}
	
	sd_clk_transfer_high();
	
	//Write data
	loop_num = sd_mmc_info->blk_len;
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	{
		for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
		{
			for(i=1; i>=0; i--)
			{
				sd_clk_transfer_low();
				
				data = (*data_buf >> (i<<2)) & 0x0F;
				sd_set_dat0_3_value(data);
				
				sd_clk_transfer_high();
			}
			
			data_buf++;
		}
		
		//Caculate CRC16 value and write to line
		for(i=0; i<4; i++)
		{
			crc16_array[i] = sd_cal_crc_mode(org_buf, sd_mmc_info->blk_len, char_mode[i]);
		}
	
		//Write CRC16
		for(i=15; i>=0; i--)
		{
			sd_clk_transfer_low();
		
			data = 0;
			for(j=3; j>=0; j--)
			{
				data <<= 1; 
				data |= (crc16_array[j] >> i) & 0x0001;
			}
			sd_set_dat0_3_value(data);
			
			sd_clk_transfer_high();
		}
	}
	else    //only dat0 line
	{
		for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
		{
			for(i=7; i>=0; i--)
			{
				sd_clk_transfer_low();
				
				data = (*data_buf >> i) & 0x01;
				sd_set_dat0_value(data);
				
				sd_clk_transfer_high();
			}
			
			data_buf++;
		}
		
		//Caculate CRC16 value and write to line
		crc16 = sd_cal_crc16(org_buf, sd_mmc_info->blk_len);

		//Write CRC16
		for(i=15; i>=0; i--)
		{
			sd_clk_transfer_low();
		
			data = (crc16 >> i) & 0x0001;
			sd_set_dat0_value(data);
			
			sd_clk_transfer_high();
		}
	}

	//End bit
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	{
		sd_clk_transfer_low();
	
		sd_set_dat0_3_value(0x0F);
		
		sd_clk_transfer_high();
		
		sd_set_dat0_3_input();
	}
	else
	{
		sd_clk_transfer_low();
	
		sd_set_dat0_value(0x01);
		
		sd_clk_transfer_high();
		
		sd_set_dat0_input();
	}

	sd_delay_clocks_h(sd_mmc_info, SD_MMC_Z_CMD_TO_RES);
	
	crc_status = 0;
	//Check CRC status
	sd_set_dat0_input();
	for(i = 0; i < 5; i++)
	{
		sd_clk_transfer_low();
		
		data = sd_get_dat0_value();
		crc_status <<= 1;
		crc_status |= data; 
		
		sd_clk_transfer_high();
	}
	if (crc_status == 0x0A)         //1011, CRC error
		return SD_MMC_ERROR_DATA_CRC;
	else if (crc_status == 0x0F)        //1111, Programming error
		return SD_MMC_ERROR_DRIVER_FAILURE;
						//0101, CRC ok
		
	//Check busy
	sd_start_timer(SD_PROGRAMMING_TIMEOUT);
	do
	{
		sd_clk_transfer_low();
		
		data = sd_get_dat0_value();
		
		sd_clk_transfer_high();
		
		if(data)
			break;

	}while(!sd_check_timer());
	
	if(sd_check_timeout())
	{
		return SD_MMC_ERROR_TIMEOUT;
	}

	return SD_MMC_NO_ERROR;
}
#endif

#ifdef SD_MMC_HW_CONTROL
int sd_write_multi_block_hw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long lba_cnt, unsigned char * data_buf)
{
	int ret, write_retry_count;
	unsigned long lba_num, data_addr, data_offset = 0;
	unsigned char *status_data_buf = sd_mmc_info->sd_mmc_buf;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned char * orig_phy_buf = sd_mmc_info->sd_mmc_phy_buf;
	unsigned char * orig_virt_buf = sd_mmc_info->sd_mmc_buf;
	
	if(lba_cnt == 0)
		return SD_MMC_ERROR_BLOCK_LEN;

	if ((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	{
		data_addr = lba;
	}
	else
	{
		data_addr = sd_mmc_info->blk_len;
		data_addr *= lba;
	}

        while(lba_cnt)
        {
            if(lba_cnt > sd_mmc_info->max_blk_count)
                lba_num = sd_mmc_info->max_blk_count;
            else
                lba_num = lba_cnt;

			/*
	        if(sd_mmc_info->card_type == CARD_TYPE_SD || sd_mmc_info->card_type == CARD_TYPE_SDHC)
	        {
		        ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, NULL, 0, 0);
		        if (ret) 
		            goto out;
		
		        ret = sd_send_cmd_hw(sd_mmc_info, SD_SET_WR_BLK_ERASE_COUNT, lba_num, RESPONSE_R1, response, NULL, 0, 0);
		        if (ret) 
		            goto out;
	        }
			*/

            if((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	            data_addr += data_offset/512;
	        else
	            data_addr += data_offset;

            data_buf += data_offset;
			sd_mmc_info->sd_mmc_phy_buf += data_offset;
	        for(write_retry_count=0; write_retry_count<4; write_retry_count++)
	        {
	            ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_WRITE_MULTIPLE_BLOCK, data_addr, RESPONSE_R1, response, data_buf, lba_num*512, 1);
	            if(ret == SD_MMC_ERROR_DATA_CRC || ret == SD_MMC_ERROR_COM_CRC)
	            {
					printk("sd write fail case 1\n");
	                ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
	                if(ret)
	                {
						printk("sd write fail case 2\n");
	                    goto out;
	                }
	                else
	                {
						printk("sd write fail case 3\n");
	                    if(sd_mmc_info->spec_version || sd_mmc_info->card_type == CARD_TYPE_SDHC)
		                {  
		                /*status should not cover data, get status at data_offset & read data cover it*/
		                status_data_buf = sd_mmc_info->sd_mmc_buf+data_offset;
                            memset(status_data_buf, 0, 64);
#ifdef SD_MMC_HW_CONTROL
	                        if(SD_WORK_MODE == CARD_HW_MODE)
		                        ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x00FFFF01, RESPONSE_R1, response, status_data_buf, 64, 1);
#endif
                        //for some sdhc card write problem on 7216 picframe
                        }
	                    continue;
	                }
	            }
				/*
	            else if(ret == SD_MMC_ERROR_TIMEOUT)
	            {
					printk("sd write fail case 11\n");
	                ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
	                if(ret)
	                {
						printk("sd write fail case 12\n");
	                    goto out;
	                }
	                else
	                {
						printk("sd write fail case 13\n");
	                    continue;
	                }
	            }
				*/
	            else if(ret == SD_MMC_ERROR_TIMEOUT)
	            {
					printk("sd write fail case 31\n");
	                ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
					{
						int i = 10;
						while (ret != SD_MMC_NO_ERROR && i--) {
							msleep(5);
							ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
						}
						if (ret != SD_MMC_NO_ERROR)
							printk("sorryxxx, send STOP cmd failed\n");
						else if (i != 10)
							printk("sendingxxx stop retry %d times\n", 10 - i);
					}
	                if(ret)
	                {
						printk("sd write fail case 32\n");
	                    goto out;
	                }
	                else
	                {
						printk("sd write fail case 33\n");
	                    continue;
	                }
	            }
				else 
				{
					if (ret != SD_MMC_NO_ERROR)
						printk("failed here\n");
	                break;
	            }
	        }

            if(write_retry_count >= 4)
            {
				ret = SD_MMC_ERROR_DATA_CRC;
                goto out;
            }

            if(ret)
            { 
				printk("sd write fail case 5, data_addr=%d, lba_num=%d\n", (unsigned int)data_addr, (unsigned int)lba_num);
                goto out;
            }
            else	//fix stop
            {        
	            ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
				{
					int i = 10;
					while (ret != SD_MMC_NO_ERROR && i--) {
						msleep(5);
						ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
					}
					if (ret != SD_MMC_NO_ERROR)
						printk("sorry, send STOP cmd failed\n");
					else if (i != 10)
						printk("sending stop retry %d times\n", 10 - i);
				}
	            if(ret)
	                goto out;
	        }

            lba_cnt -= lba_num;
            data_offset = lba_num*512;
	    }

	ret = SD_MMC_NO_ERROR;
	
out :
	sd_mmc_info->sd_mmc_phy_buf = orig_phy_buf;
	sd_mmc_info->sd_mmc_buf = orig_virt_buf;
	return ret;
}
#endif

#ifdef SD_MMC_SW_CONTROL
int sd_write_multi_block_sw(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long lba_cnt, unsigned char * data_buf)
{
	int ret,i,j;
	unsigned long crc_status, data;
	
	unsigned long data_cnt = 0;
	
	unsigned char * org_buf = data_buf;
	unsigned short crc16_array[4] = {0, 0, 0, 0};
	unsigned short crc16;
	unsigned char char_mode[4][3] = {{0x10, 0x01, 0},
					{0x20, 0x02, 0},
					{0x40, 0x04, 0},
					{0x80, 0x08, 0}};
	
	unsigned char response[MAX_RESPONSE_BYTES];
	
	unsigned long data_addr,loop_num,blk_cnt;
	
	if(lba_cnt == 0)
		return SD_MMC_ERROR_BLOCK_LEN;
		
	if (sd_mmc_info->card_type == CARD_TYPE_SD || sd_mmc_info->card_type == CARD_TYPE_SDHC)
	{
		ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response);
		if (ret) return ret;
		
		ret = sd_send_cmd_sw(sd_mmc_info, SD_SET_WR_BLK_ERASE_COUNT, lba_cnt, RESPONSE_R1, response);
		if (ret) return ret;
	}

	//Set data lines busy
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	{
		sd_set_dat0_3_output();
		
		sd_clk_transfer_low();
		
		sd_set_dat0_3_value(0x0F);
		
		sd_clk_transfer_high();
	}
	else
	{
		sd_set_dat0_output();
		
		sd_clk_transfer_low();
		
		sd_set_dat0_value(0x01);
		
		sd_clk_transfer_high();
	}

	if ((sd_mmc_info->card_type == CARD_TYPE_SDHC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
	{
		data_addr = lba;
	}
	else
	{
	    data_addr = sd_mmc_info->blk_len;
	    data_addr *= lba;
	}
	
	ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_WRITE_MULTIPLE_BLOCK, data_addr, RESPONSE_R1, response);
	if(ret)
		return ret;
	
	loop_num = sd_mmc_info->blk_len;
	for(blk_cnt = 0; blk_cnt < lba_cnt; blk_cnt++)
	{
		org_buf = data_buf;
		
		//Nwr cycles delay
		sd_delay_clocks_h(sd_mmc_info, SD_MMC_TIME_NWR);
		
		//Start bit
		if(sd_mmc_info->bus_width == SD_BUS_WIDE)
		{
			sd_set_dat0_3_output();
			
			sd_clk_transfer_low();
			
			sd_set_dat0_3_value(0x00);
			
			sd_clk_transfer_high();
		}
		else
		{
			sd_set_dat0_output();
			
			sd_clk_transfer_low();
			
			sd_set_dat0_value(0x00);
			
			sd_clk_transfer_high();
		}

		//Write data
		if(sd_mmc_info->bus_width == SD_BUS_WIDE)
		{
			for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
			{
				for(i=1; i>=0; i--)
				{
					sd_clk_transfer_low();
				
					data = (*data_buf >> (i<<2)) & 0x0F;
					sd_set_dat0_3_value(data);
					
					sd_clk_transfer_high();
				}
				
				data_buf++;
			}
			
			//Caculate CRC16 value and write to line
			for(i=0; i<4; i++)
			{
				crc16_array[i] = sd_cal_crc_mode(org_buf, sd_mmc_info->blk_len, char_mode[i]);
			}
			
			//Write CRC16
			for(i=15; i>=0; i--)
			{
				sd_clk_transfer_low();
		
				data = 0;
				for(j=3; j>=0; j--)
				{
					data <<= 1; 
					data |= (crc16_array[j] >> i) & 0x0001;
				}
				sd_set_dat0_3_value(data);
			
				sd_clk_transfer_high();
			}
		}
		else    // only dat0 line
		{
			for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
			{
				for(i=7; i>=0; i--)
				{
					sd_clk_transfer_low();
					
					data = (*data_buf >> i) & 0x01;
					sd_set_dat0_value(data);
					
					sd_clk_transfer_high();
				}
				
				data_buf++;
			}
			
			//Caculate CRC16 value and write to line
			crc16 = sd_cal_crc16(org_buf, sd_mmc_info->blk_len);

			//Write CRC16
			for(i=15; i>=0; i--)
			{
				sd_clk_transfer_low();
		
				data = (crc16 >> i) & 0x0001;
				sd_set_dat0_value(data);
			
				sd_clk_transfer_high();
			}
		}

		//End bit
		if(sd_mmc_info->bus_width == SD_BUS_WIDE)
		{
			sd_clk_transfer_low();
			
			sd_set_dat0_3_value(0x0F);
			
			sd_clk_transfer_high();
			
			sd_set_dat0_3_input();
		}
		else
		{
			sd_clk_transfer_low();
			
			sd_set_dat0_value(0x01);
			
			sd_clk_transfer_high();
			
			sd_set_dat0_input();
		}

		sd_delay_clocks_h(sd_mmc_info, SD_MMC_Z_CMD_TO_RES);
	
		crc_status = 0;
	
		//Check CRC status
		sd_set_dat0_input();
		for(i = 0; i < 5; i++)
		{
			sd_clk_transfer_low();
		
			data = sd_get_dat0_value();
			crc_status <<= 1;
			crc_status |= data; 
		
			sd_clk_transfer_high();
		}
		if (crc_status == 0x0A)         //1010, CRC error
			return SD_MMC_ERROR_DATA_CRC;
		else if (crc_status == 0x0F)        //1111, Programming error
			return SD_MMC_ERROR_DRIVER_FAILURE;
							//0101, CRC ok
							
		//Check busy
		sd_start_timer(SD_PROGRAMMING_TIMEOUT);
		do
		{
			sd_clk_transfer_low();
			
			data = sd_get_dat0_value();
			
			sd_clk_transfer_high();
		
			if(data)
				break;

		}while(!sd_check_timer());
	
		if(sd_check_timeout())
		{
			return SD_MMC_ERROR_TIMEOUT;
		}
	}
	
	ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0 , RESPONSE_R1B, response);
	if(ret)
		return ret;
	
	//Check busy
	sd_start_timer(SD_PROGRAMMING_TIMEOUT);
	do
	{
		sd_clk_transfer_low();
		
		data = sd_get_dat0_value();
		
		sd_clk_transfer_high();
		
		if(data)
			break;

	}while(!sd_check_timer());
	
	if(sd_check_timeout())
	{
		return SD_MMC_ERROR_TIMEOUT;
	}

	return SD_MMC_NO_ERROR;
}
#endif

//Functions for SD INIT
int sd_hw_reset(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int ret=SD_MMC_NO_ERROR;
	sd_mmc_info->operation_mode = CARD_INDENTIFICATION_MODE;

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_GO_IDLE_STATE, 0, RESPONSE_NONE, 0, 0, 0, 1);
#endif	
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{
		sd_delay_clocks_h(sd_mmc_info, 74);  //74 is enough according to spec
	
		sd_delay_ms(1);
		
		ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_GO_IDLE_STATE, 0, RESPONSE_NONE, 0);
	}
#endif
	
	return ret;
}

int sd_sw_reset(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int ret=SD_MMC_NO_ERROR;
	
	sd_mmc_info->operation_mode = CARD_INDENTIFICATION_MODE;

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_GO_IDLE_STATE, 0, RESPONSE_NONE, 0, 0, 0, 1);
#endif	
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)	
		ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_GO_IDLE_STATE, 0, RESPONSE_NONE, 0);
#endif
	
	return ret;
}
//lin
/*
static int sd_change_to_low_voltage(SD_MMC_Card_Info_t *sd_mmc_info)
{
	unsigned char response[MAX_RESPONSE_BYTES];
	//SD_Response_R1_t * r1;
	int ret = 0;

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)	//CMD11
		ret = sd_send_cmd_hw(sd_mmc_info, VOLTAGE_SWITCH, 0, RESPONSE_R1, response, 0, 0, 0);
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		ret = sd_send_cmd_sw(sd_mmc_info, VOLTAGE_SWITCH, 0, RESPONSE_R1, response);	
#endif

	if (ret)
	{
		sd_mmc_info->support_uhs_mode = 0;	//fail to switch to 1.8v, so pretend to not support UHS
		return ret;
	}
	else
	{
		printk("Start switch to 1.8V\n");

		return SD_MMC_NO_ERROR;
	}	
}
*/
int sd_voltage_validation(SD_MMC_Card_Info_t *sd_mmc_info)
{
	unsigned char response[MAX_RESPONSE_BYTES];
	SD_Response_R3_t * r3;
	SD_Response_R7_t * r7;
	SDIO_Response_R4_t *r4;
	int ret = 0,error = 0,delay_time,delay_cnt;

	//sd_delay_ms(10);
	
	delay_time = 10;
	delay_cnt = 1;
	//Detect if SD card is inserted first

	printk("sd_mmc_info->card_type=%d\n",sd_mmc_info->card_type);
	printk("begin SDIO check ......\n");
	do
	{
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
			ret = sd_send_cmd_hw(sd_mmc_info, IO_SEND_OP_COND, 0x00000000, RESPONSE_R4, response, 0, 0, 0);   // 0x00000000: 0v
#endif
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
			ret = sd_send_cmd_sw(sd_mmc_info, IO_SEND_OP_COND, 0x00200000, RESPONSE_R4, response);   // 0x00200000: 3.3v~3.4v
#endif
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
			ret = sd_send_cmd_hw(sd_mmc_info, IO_SEND_OP_COND, 0x00200000, RESPONSE_R4, response, 0, 0, 0);   // 0x00200000: 3.3v~3.4v
#endif
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
			ret = sd_send_cmd_sw(sd_mmc_info, IO_SEND_OP_COND, 0x00200000, RESPONSE_R4, response);   // 0x00200000: 3.3v~3.4v
#endif
	    if(ret == SD_MMC_ERROR_TIMEOUT)
	    {	
			printk("SEND OP timeout @1\n");
		    error = sd_hw_reset(sd_mmc_info);
		    if(error)
		    {
#ifdef  SD_MMC_DEBUG
			    Debug_Printf("#%s error occured in sd_hw_reset()\n", sd_error_to_string(error));
#endif
			    return error;
		    }

		    break;
	    }
	    else
	    {
            r4 = (SDIO_Response_R4_t *)response;
            if(r4->Card_Ready)
            {
                sd_mmc_info->card_type = CARD_TYPE_SDIO;
                sd_mmc_info->sdio_function_nums = r4->IO_Function_No;
                if(r4->Memory_Present)
                   break;
                else {
#ifdef SD_MMC_DEBUG
					Debug_Printf("Actual delay time in sdio_voltage_validation() = %d ms\n", delay_time*delay_cnt);
#endif
                    return SD_MMC_NO_ERROR;
            	}
            }

            sd_delay_ms(delay_time);
		    delay_cnt++;
        }
    } while(delay_cnt < (SD_MMC_IDENTIFY_TIMEOUT/delay_time));	//lin


	printk("begin SD&SDHC check ......\n");
	sd_delay_ms(10);

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		ret = sd_send_cmd_hw(sd_mmc_info, SD_SEND_IF_COND, 0x000001aa, RESPONSE_R7, response, 0, 0, 0);
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		ret = sd_send_cmd_sw(sd_mmc_info, SD_SEND_IF_COND, 0x000001aa, RESPONSE_R7, response);
#endif


	if(ret)
	{
		if(ret == SD_MMC_ERROR_TIMEOUT) {
			printk("SEND IF timeout @2\n");
			error = sd_hw_reset(sd_mmc_info);
			if(error)
			{
#ifdef  SD_MMC_DEBUG
				Debug_Printf("#%s error occured in sd_hw_reset()\n", sd_error_to_string(error));
#endif
				return error;
			}
		}
		else
			return ret;
	}
	else
	{
		r7 = (SD_Response_R7_t *)response;
		if(r7->cmd_version == 0 && r7->voltage_accept == 1 && r7->check_pattern == 0xAA)
		{
			printk("set type to SDHC\n");
			sd_mmc_info->card_type = CARD_TYPE_SDHC;
		}
		else
		{
			printk("set type to SD\n");
			sd_mmc_info->card_type = CARD_TYPE_SD;
		}
	}

	delay_cnt = 2;
    do
    {
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
			ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, 0, RESPONSE_R1, response, 0, 0, 0);
#endif		
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
			ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_CMD, 0, RESPONSE_R1, response);
#endif
					
		if(ret)
		{
			if(ret == SD_MMC_ERROR_TIMEOUT)
				break;
				
			sd_delay_ms(delay_time);
			delay_cnt++;
				
			continue;
		}

		//lin : merge
		////if(sd_mmc_info->card_type == CARD_TYPE_SDHC)
		////{	
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)	//0x40200000 : add S18R
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_OP_COND, 0x41200000, RESPONSE_R3, response, 0, 0, 0);   // 0x00200000: 3.3v~3.4v
#endif
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
				ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_OP_COND, 0x41200000, RESPONSE_R3, response);   // 0x00200000: 3.3v~3.4v
#endif
		/*
			printk("APP OP SDHC\n");
		}
		else
		{	
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)	//0x00200000 : add HCS
			ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_OP_COND, 0x40200000, RESPONSE_R3, response, 0, 0, 0);   // 0x00200000: 3.3v~3.4v
#endif
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
			ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_OP_COND, 0x40200000, RESPONSE_R3, response);   // 0x00200000: 3.3v~3.4v
#endif
			printk("APP OP SD\n");
		}	
		*/

		r3 = (SD_Response_R3_t *)response;
		if(ret == SD_MMC_NO_ERROR && r3->ocr.Card_Busy)
		{
#ifdef SD_MMC_DEBUG
			Debug_Printf("Actual delay time in sd_voltage_validation() = %d ms\n", delay_time*delay_cnt);
#endif
			if(!r3->ocr.Card_Capacity_Status) 
			{
				printk("Change from %d to SD\n", sd_mmc_info->card_type);
				sd_mmc_info->card_type = CARD_TYPE_SD;
			}

			printk("S18A : %d\n", r3->ocr.S18A);
			sd_mmc_info->support_uhs_mode = r3->ocr.S18A;
			if (sd_mmc_info->support_uhs_mode)
			{
				////return sd_change_to_low_voltage(sd_mmc_info);
				return SD_MMC_NO_ERROR;
			}
			else
			{			
				return SD_MMC_NO_ERROR;
			}
		}

		sd_delay_ms(delay_time);
		delay_cnt++;
    } while(delay_cnt < (SD_MMC_IDENTIFY_TIMEOUT/delay_time));	//lin

	printk("begin MMC check ......\n");
	sd_sw_reset(sd_mmc_info);
	sd_delay_ms(10);

	delay_cnt = 2;
	//No SD card, detect if MMC card is inserted then
	do
	{
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SEND_OP_COND, 0x40FF8000, RESPONSE_R3, response, 0, 0, 0); // 0x00200000: 3.3v~3.4v
#endif 
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
			ret = sd_send_cmd_sw(sd_mmc_info, MMC_SEND_OP_COND, 0x40FF8000, RESPONSE_R3, response); // 0x00200000: 3.3v~3.4v
#endif
			r3 = (SD_Response_R3_t *)response;
		if(ret == SD_MMC_ERROR_TIMEOUT)
		{
			break;
		}
		else if((ret == SD_MMC_NO_ERROR) && r3->ocr.Card_Busy)
		{
#ifdef SD_MMC_DEBUG
			Debug_Printf("Actual delay time in sd_voltage_validation() = %d ms\n", delay_time*delay_cnt);
#endif
			if(!r3->ocr.Card_Capacity_Status)
				sd_mmc_info->card_type = CARD_TYPE_MMC;
			else
				sd_mmc_info->card_type = CARD_TYPE_EMMC;

			printk("set type to %s\n", sd_mmc_info->card_type == CARD_TYPE_MMC ? "mmc" : "emmc");
			return SD_MMC_NO_ERROR;
		}
		
		sd_delay_ms(delay_time);
		delay_cnt++;
    } while(delay_cnt < (SD_MMC_IDENTIFY_TIMEOUT/delay_time));	//lin

#ifdef SD_MMC_DEBUG
	Debug_Printf("No any SD/MMC card detected!\n");
#endif
	return SD_MMC_ERROR_DRIVER_FAILURE;
}

static void my_disp_csdv2(SDHC_REG_CSD_t *csd)
{
	unsigned int i;
	unsigned char *pcsd;

	sdpro_dbg("\n********** CSD v2 **************\n");

	pcsd = (unsigned char *)csd;
	for (i = 0; i < sizeof(SDHC_REG_CSD_t); i++)
	{
		sdpro_dbg("%02x:", pcsd[i]);
	}
	sdpro_dbg("\nCSD_STRUCTURE[2]=%d\n", csd->CSD_STRUCTURE);
	sdpro_dbg("Reserved1[6]=%d\n", csd->Reserved1);
	
	sdpro_dbg("TAAC[8]=%d\n", csd->TAAC);
	sdpro_dbg("NSAC[8]=%d\n", csd->NSAC);
	sdpro_dbg("TRAN_SPEED[8]=%d\n", csd->TRAN_SPEED);
	
	sdpro_dbg("CCC[8+4]=0x%x\n", (csd->CCC_high <<4) | csd->CCC_low);	//
	sdpro_dbg("READ_BL_LEN[4]=%d\n", csd->READ_BL_LEN);
	
	sdpro_dbg("READ_BL_PARTIAL[1]=%d\n", csd->READ_BL_PARTIAL);
	sdpro_dbg("WRITE_BLK_MISALIGN[1]=%d\n", csd->WRITE_BLK_MISALIGN);
	sdpro_dbg("READ_BLK_MISALIGN[1]=%d\n", csd->READ_BLK_MISALIGN);	
	sdpro_dbg("DSR_IMP[1]=%d\n", csd->DSR_IMP);
	sdpro_dbg("Reserved2[4]=%d\n", csd->Reserved2);
	
	sdpro_dbg("Reserved3[2]=%d\n", csd->Reserved3);	
	sdpro_dbg("C_SIZE[6+8+8]=%d\n", (csd->C_SIZE_high << 16) | (csd->C_SIZE_mid << 8) | csd->C_SIZE_low);	//

	sdpro_dbg("Reserved4[1]=%d\n", csd->Reserved4);
	sdpro_dbg("ERASE_BLK_EN[1]=%d\n", csd->ERASE_BLK_EN);	
	sdpro_dbg("SECTOR_SIZE[6+1]=%d\n", (csd->SECTOR_SIZE_high << 1) | csd->SECTOR_SIZE_low);	//
	sdpro_dbg("WP_GRP_SIZE[7]=%d\n", csd->WP_GRP_SIZE);
	
	sdpro_dbg("WP_GRP_ENABLE[1]=%d\n", csd->WP_GRP_ENABLE);
	sdpro_dbg("Reserved5[2]=%d\n", csd->Reserved5);
	sdpro_dbg("R2W_FACTOR[3]=%d\n", csd->R2W_FACTOR);
	sdpro_dbg("WRITE_BL_LEN[2+2]=%d\n", (csd->WRITE_BL_LEN_high << 2) | csd->WRITE_BL_LEN_low);	//
	
	sdpro_dbg("WRITE_BL_PARTIAL[1]=%d\n", csd->WRITE_BL_PARTIAL);
	sdpro_dbg("Reserved6[5]=%d\n", csd->Reserved6);
	
	sdpro_dbg("FILE_FORMAT_GRP[1]=%d\n", csd->FILE_FORMAT_GRP);
	sdpro_dbg("COPY[1]=%d\n", csd->COPY);
	sdpro_dbg("PERM_WRITE_PROTECT[1]=%d\n", csd->PERM_WRITE_PROTECT);
	sdpro_dbg("TMP_WRITE_PROTECT[1]=%d\n", csd->TMP_WRITE_PROTECT);
	sdpro_dbg("FILE_FORMAT[2]=%d\n", csd->FILE_FORMAT);
	sdpro_dbg("Reserved7[2]=%d\n", csd->Reserved7);

	sdpro_dbg("CRC[7]=%d\n", csd->CRC);
	sdpro_dbg("NotUsed[1]=%d\n\n", csd->NotUsed);	
}

static void my_disp_csdv1(SD_REG_CSD_t *csd)
{
	unsigned int i;
	unsigned char *pcsd;

	sdpro_dbg("\n********** CSD v1 **************\n");

	pcsd = (unsigned char *)csd;
	for (i = 0; i < sizeof(SD_REG_CSD_t); i++)
	{
		sdpro_dbg("%02x:", pcsd[i]);
	}

	sdpro_dbg("\nCSD_STRUCTURE[2]=%d\n", csd->CSD_STRUCTURE);
	sdpro_dbg("MMC_SPEC_VERS[4]=%d\n", csd->MMC_SPEC_VERS);
	sdpro_dbg("Reserved1[2]=%d\n", csd->Reserved1);
	
	sdpro_dbg("TAAC[8]=%d\n", csd->TAAC);
	sdpro_dbg("NSAC[8]=%d\n", csd->NSAC);
	sdpro_dbg("TRAN_SPEED[8]=%d\n", csd->TRAN_SPEED);

	sdpro_dbg("CCC[8+4]=%d\n", (csd->CCC_high << 4) | csd->CCC_low);	//
	sdpro_dbg("READ_BL_LEN[4]=%d\n", csd->READ_BL_LEN);

	sdpro_dbg("READ_BL_PARTIAL[1]=%d\n", csd->READ_BL_PARTIAL);
	sdpro_dbg("WRITE_BLK_MISALIGN[1]=%d\n", csd->WRITE_BLK_MISALIGN);
	sdpro_dbg("READ_BLK_MISALIGN[1]=%d\n", csd->READ_BLK_MISALIGN);
	sdpro_dbg("DSR_IMP[1]=%d\n", csd->DSR_IMP);
	sdpro_dbg("Reserved2[2]=%d\n", csd->Reserved2);
	sdpro_dbg("C_SIZE[2+8+2]=%d\n", (csd->C_SIZE_high << 10) | (csd->C_SIZE_mid << 2) | csd->C_SIZE_low);	//
	
	sdpro_dbg("VDD_R_CURR_MIN[3]=%d\n", csd->VDD_R_CURR_MIN);
	sdpro_dbg("VDD_R_CURR_MAX[3]=%d\n", csd->VDD_R_CURR_MAX);
	
	sdpro_dbg("VDD_W_CURR_MIN[3]=%d\n", csd->VDD_W_CURR_MIN);
	sdpro_dbg("VDD_W_CURR_MAX[3]=%d\n", csd->VDD_W_CURR_MAX);
	sdpro_dbg("C_SIZE_MULT[2+1]=%d\n", (csd->C_SIZE_MULT_high << 1) | csd->C_SIZE_MULT_low);	//
	sdpro_dbg("ERASE_BLK_EN[1]=%d\n", csd->ERASE_BLK_EN);
	sdpro_dbg("SECTOR_SIZE[6+1]=%d\n", (csd->SECTOR_SIZE_high <<1) | csd->SECTOR_SIZE_low);	//
	
	sdpro_dbg("WP_GRP_SIZE[7]=%d\n", csd->WP_GRP_SIZE);
	sdpro_dbg("Reserved3[2]=%d\n", csd->Reserved3);
	sdpro_dbg("R2W_FACTOR[3]=%d\n", csd->R2W_FACTOR);
	sdpro_dbg("WRITE_BL_LEN[2+2]=%d\n", (csd->WRITE_BL_LEN_high << 2) | csd->WRITE_BL_LEN_low);	//
	sdpro_dbg("WRITE_BL_PARTIAL[1]=%d\n", csd->WRITE_BL_PARTIAL);
	sdpro_dbg("Reserved4[5]=%d\n", csd->Reserved4);
	sdpro_dbg("WP_GRP_ENABLE[1]=%d\n", csd->WP_GRP_ENABLE);
	
	sdpro_dbg("FILE_FORMAT_GRP[1]=%d\n", csd->FILE_FORMAT_GRP);
	sdpro_dbg("COPY[1]=%d\n", csd->COPY);
	sdpro_dbg("PERM_WRITE_PROTECT[1]=%d\n", csd->PERM_WRITE_PROTECT);
	sdpro_dbg("TMP_WRITE_PROTECT[1]=%d\n", csd->TMP_WRITE_PROTECT);
	sdpro_dbg("FILE_FORMAT[2]=%d\n", csd->FILE_FORMAT);
	sdpro_dbg("Reserved5[2]=%d\n", csd->Reserved5);
	
	sdpro_dbg("CRC[7]=%d\n", csd->CRC);
	sdpro_dbg("NotUsed[1]=%d\n\n", csd->NotUsed);
}

#if 0
static void my_disp_csd_ext(MMC_REG_EXT_CSD_t *csd)
{
	unsigned int i;
	unsigned char *pcsd;

	printk("\n********** CSD ext **************\n");

	pcsd = (unsigned char *)csd;
	for (i = 0; i < sizeof(MMC_REG_EXT_CSD_t); i++)
	{
		printk("%02x:", pcsd[i]);
		if ((i % 32) == 31)
			printk("\n");
	}

	printk("\nSEC_BAD_BLK_MGMNT=%d\n", csd->SEC_BAD_BLK_MGMNT);
	printk("ENH_START_ADDR[4B]=%d\n", *((unsigned *)&csd->ENH_START_ADDR));
	printk("ENH_SIZE_MULT[3B]=%d\n", (csd->ENH_SIZE_MULT[0]<<16) + (csd->ENH_SIZE_MULT[1]<<8) + (csd->ENH_SIZE_MULT[2]));
	//printk("GP_SIZE_MULT=%d\n", csd->GP_SIZE_MULT);
	printk("PARTITION_SETTING_COMPLETED=%d\n", csd->PARTITION_SETTING_COMPLETED);
	printk("PARTITIONS_ATTRIBUTE=%d\n", csd->PARTITIONS_ATTRIBUTE);
	printk("MAX_ENH_SIZE_MULT[3B]=%d\n", (csd->MAX_ENH_SIZE_MULT[0]<<16) + (csd->MAX_ENH_SIZE_MULT[1]<<8) + (csd->MAX_ENH_SIZE_MULT[2]));
	printk("PARTITIONING_SUPPORT=%d\n", csd->PARTITIONING_SUPPORT);
	printk("HPI_MGMT=%d\n", csd->HPI_MGMT);
	//printk("=%d\n", csd->);
	printk("SEC_COUNT[4B]=%d\n", *((unsigned *)&csd->SEC_COUNT));
	printk("S_CMD_SET=%d\n\n", csd->S_CMD_SET);
}
#endif

//lin
/*
static void my_disp_ssr(SD_REG_SSR_t *ssr)
{
	unsigned int i;
	unsigned char *pssr;

	printk("\n********** SSR **************\n");

	pssr = (unsigned char *)ssr;
	for (i = 0; i < sizeof(SD_REG_SSR_t); i++)
	{
		printk("%02x:", pssr[i]);
	}

	printk("\nDAT_BUS_WIDTH[2]=%d\n", ssr->DAT_BUS_WIDTH);
	printk("SECURE_MODE[1]=%d\n", ssr->SECURE_MODE);
	printk("Reserved1[5+2]=%d\n", (ssr->Reserved1_1 << 2) | ssr->Reserved1_2);	//
	printk("Reserved2[6]=%d\n", ssr->Reserved2);
	
	printk("SD_CARD_TYPE[16]=%d\n", ssr->SD_CARD_TYPE);
	printk("SIZE_OF_PROTECTED_AREA[32]=%d\n", ssr->SIZE_OF_PROTECTED_AREA);
	
	printk("SPEED_CLASS[8]=%d\n", ssr->SPEED_CLASS);
	printk("PERPORMANCE_MOVE[8]=%d\n", ssr->PERPORMANCE_MOVE);
	
	printk("AU_SIZE[4]=%d\n", ssr->AU_SIZE);
	printk("Reserved3[4]=%d\n", ssr->Reserved3);
	
	printk("ERASE_SIZE[16]=%d\n", ssr->ERASE_SIZE);
	
	printk("ERASE_TIMEOUT[6]=%d\n", ssr->ERASE_TIMEOUT);
	printk("ERASE_OFFSET[2]=%d\n", ssr->ERASE_OFFSET);
}

//SSR(512) is translated in DAT
static int sd_read_reg_ssr(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_SSR_t *ssr)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];

	sd_clear_response(response);

	if (SD_WORK_MODE == CARD_HW_MODE)
	{
		ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, NULL, 0, 1);
		if (ret)
			return ret;

		ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_STATUS, 0, RESPONSE_R1, response, sd_mmc_info->sd_mmc_buf, sizeof(SD_REG_SSR_t), 1);
		if (ret)
			return ret;

		memcpy(ssr, sd_mmc_info->sd_mmc_buf, sizeof(SD_REG_SSR_t));	
	}

	return SD_MMC_NO_ERROR;
}
*/

//sys pll = 800M
////#define CLK_DIV_IDENTIFY	1999	//400K
#define CLK_DIV_IDENTIFY	3999	//IDENTIFY			(200KHz)
#define CLK_DIV_25M			31		//DS, SDR12 			(25MHz)
#define CLK_DIV_50M			15		//HS, SDR25, DDR50 	(50MHz)
#define CLK_DIV_100M		7		//SDR50   			(100MHz)
#define CLK_DIV_208M		3		//SDR104 			(208MHz)
	
#define CLK_DIV_HS_MMC		7
#define CLK_DIV_HS_SDIO		7
	
int sd_identify_process(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int ret = 0, slot_id, times;
	unsigned temp;  ///< for compiler bug
	unsigned delay_time, delay_cnt = 0;
	
	unsigned char response[MAX_RESPONSE_BYTES];
	SD_Response_R2_CSD_t * r2_csd = NULL;
	SDHC_Response_R2_CSD_t * sdhc_r2_csd = NULL;
	SD_Response_R6_t * sd_response_r6;
	
	unsigned c_size;
	unsigned char c_size_multi;
	unsigned char read_reg_data, write_reg_data;
	unsigned sdio_config;
	SDIO_Config_Reg_t *config_reg = NULL;

	unsigned char *mmc_ext_csd_buf = sd_mmc_info->sd_mmc_buf;
	MMC_REG_EXT_CSD_t *mmc_ext_csd_reg;
	//Request all devices to send their CIDs
	if(sd_mmc_info->card_type != CARD_TYPE_SDIO)
	{
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_ALL_SEND_CID, 0, RESPONSE_R2_CID, response, 0, 0, 1);
#endif	
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_ALL_SEND_CID, 0, RESPONSE_R2_CID, response);
#endif
	}

	sd_delay_ms(50);  //for MUSE 64MB CARD sd_identify_process timeout
	//sd_delay_ms(10);  //for samsung card
	/* Assign IDs to all devices found */
	slot_id = 1;
	delay_time = 10;
	while(delay_cnt < SD_IDENTIFICATION_TIMEOUT/TIMER_1MS)
	{
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
		{
			if(sd_mmc_info->card_type == CARD_TYPE_SD || sd_mmc_info->card_type == CARD_TYPE_SDHC || sd_mmc_info->card_type == CARD_TYPE_SDIO)
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_RELATIVE_ADDR, slot_id<<16, RESPONSE_R6, response, 0, 0, 0);   ///* Send out a byte to read RCA*/
			else if((sd_mmc_info->card_type == CARD_TYPE_MMC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC)) 
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_RELATIVE_ADDR, slot_id<<16, RESPONSE_R1, response, 0, 0, 0);   ///* Send out a byte to read RCA*/
		}
#endif			
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
		{
			if(sd_mmc_info->card_type == CARD_TYPE_SD || sd_mmc_info->card_type == CARD_TYPE_SDHC || sd_mmc_info->card_type == CARD_TYPE_SDIO)
				ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_SEND_RELATIVE_ADDR, slot_id<<16, RESPONSE_R6, response);   ///* Send out a byte to read RCA*/
			else if((sd_mmc_info->card_type == CARD_TYPE_MMC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
				ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_SEND_RELATIVE_ADDR, slot_id<<16, RESPONSE_R1, response);   ///* Send out a byte to read RCA*/
		}
#endif

		sd_response_r6 = (SD_Response_R6_t *)response;
		/* Check for SD card */
		if((sd_mmc_info->card_type == CARD_TYPE_SD || sd_mmc_info->card_type == CARD_TYPE_SDHC || sd_mmc_info->card_type == CARD_TYPE_SDIO) && (ret == SD_MMC_NO_ERROR))
			break;

				/* Get device information and assign an RCA to it. */
		if (((sd_mmc_info->card_type == CARD_TYPE_MMC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC)) && ( ret == SD_MMC_NO_ERROR))
		{
			/* There isn't any more device found */
			break;        
		}
		else if((sd_mmc_info->card_type == CARD_TYPE_MMC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
		{
			/* The RCA is returned in pc->LastResponse[4] */
			slot_id += 1;
		}
	
		sd_delay_ms(delay_time);
		delay_cnt += delay_time;
	}
	
	if(delay_cnt >= SD_IDENTIFICATION_TIMEOUT/TIMER_1MS)
	{
		return SD_MMC_ERROR_DRIVER_FAILURE;
	}
	
	if(sd_mmc_info->card_type == CARD_TYPE_SD || sd_mmc_info->card_type == CARD_TYPE_SDHC || sd_mmc_info->card_type == CARD_TYPE_SDIO)
		sd_mmc_info->card_rca = ((SD_Response_R6_t *)response)->rca_high << 8 | ((SD_Response_R6_t *)response)->rca_low;
	else if((sd_mmc_info->card_type == CARD_TYPE_MMC)  || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
		sd_mmc_info->card_rca = slot_id;

	printk("Got RCA = %d\n", sd_mmc_info->card_rca);
	
    if(sd_mmc_info->card_type == CARD_TYPE_SDIO)
    {
#ifdef SD_MMC_HW_CONTROL
	    if(SD_WORK_MODE == CARD_HW_MODE)
		    ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SELECT_DESELECT_CARD, sd_mmc_info->card_rca<<16, RESPONSE_R1B, response, 0, 0, 1);
#endif	
#ifdef SD_MMC_SW_CONTROL
	    if(SD_WORK_MODE == CARD_SW_MODE)
		    ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_SELECT_DESELECT_CARD, sd_mmc_info->card_rca<<16, RESPONSE_R1B, response);
#endif

		ret = sdio_read_reg(sd_mmc_info, 0, BUS_Interface_Control_REG, &read_reg_data);
		if(ret)
			return ret;

		sd_mmc_info->operation_mode = DATA_TRANSFER_MODE;
		write_reg_data = ((read_reg_data & 0xfc) | SDIO_Wide_bus_Bit);
		ret = sdio_write_reg(sd_mmc_info, 0, BUS_Interface_Control_REG, &write_reg_data, SDIO_Read_After_Write);
		if(ret)
		{
			write_reg_data = SDIO_Single_bus_Bit;
    		ret = sdio_write_reg(sd_mmc_info, 0, BUS_Interface_Control_REG, &write_reg_data, SDIO_Read_After_Write);
    		if(ret)
				return ret;

			sd_mmc_info->bus_width = SD_BUS_SINGLE;
	        return SD_MMC_NO_ERROR;
	    }  
		else
		{
			sd_mmc_info->bus_width = SD_BUS_WIDE;
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
			{
				sdio_config = 0;
				config_reg = (void *)&sdio_config;
				sdio_config = READ_CBUS_REG(SDIO_CONFIG);
				config_reg->bus_width = 1;
				WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
			}
#endif
			return SD_MMC_NO_ERROR;
		}    
    }

	ret = sd_read_reg_cid(sd_mmc_info, &sd_mmc_info->raw_cid);
	if(ret)
		return ret;

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_CSD, sd_mmc_info->card_rca<<16, RESPONSE_R2_CSD, response, 0, 0, 1);
#endif	
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_SEND_CSD, sd_mmc_info->card_rca<<16, RESPONSE_R2_CSD, response);
#endif
	if(ret)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured at line: %d in file %s\n", sd_error_to_string(ret),__LINE__,__FILE__);
#endif
		return ret;
	}
	
	if(sd_mmc_info->card_type == CARD_TYPE_SDHC)
	{
		sdhc_r2_csd = (SDHC_Response_R2_CSD_t *)response;
		my_disp_csdv2((SDHC_REG_CSD_t *)&sdhc_r2_csd->csd);
	}
	else
	{
	    r2_csd = (SD_Response_R2_CSD_t *)response;
	    my_disp_csdv1((SD_REG_CSD_t *)&r2_csd->csd);
	}

	if((sd_mmc_info->card_type == CARD_TYPE_MMC) || (sd_mmc_info->card_type == CARD_TYPE_EMMC))
    	sd_mmc_info->mmc_spec_version = r2_csd->csd.MMC_SPEC_VERS;

	if(sd_mmc_info->card_type == CARD_TYPE_SDHC)
	{
		sd_mmc_info->clks_nac = 50000;
		c_size = (sdhc_r2_csd->csd.C_SIZE_high << 16) | (sdhc_r2_csd->csd.C_SIZE_mid << 8) | (sdhc_r2_csd->csd.C_SIZE_low);
		sd_mmc_info->blk_nums = (c_size + 1) << 10;
		sd_mmc_info->blk_len = 512;
	}
	else 
	{
	    sd_mmc_info->clks_nac = sd_cal_clks_nac(r2_csd->csd.TAAC, r2_csd->csd.NSAC);

	    c_size = (r2_csd->csd.C_SIZE_high << 10) | (r2_csd->csd.C_SIZE_mid << 2) | r2_csd->csd.C_SIZE_low;
	    c_size_multi = (r2_csd->csd.C_SIZE_MULT_high << 1) | r2_csd->csd.C_SIZE_MULT_low;
	    temp = (c_size+1) * (1 << (c_size_multi+2));
	    sd_mmc_info->blk_nums = temp;
	
	    sd_mmc_info->blk_len = 1 << r2_csd->csd.READ_BL_LEN;
	    if(sd_mmc_info->blk_len != 512)
	    {
			printk("!!! convert blk_len !!!\n");
		    temp = sd_mmc_info->blk_len;
		    if((temp % 512) != 0)
			    return SD_MMC_ERROR_BLOCK_LEN;
		
		    times = temp / 512;
		    temp = sd_mmc_info->blk_nums;
		    temp *= times;
		    sd_mmc_info->blk_nums = temp;
		    sd_mmc_info->blk_len = 512;
	    }
	}
	
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SELECT_DESELECT_CARD, sd_mmc_info->card_rca<<16, RESPONSE_R1B, response, 0, 0, 1);
#endif	
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_SELECT_DESELECT_CARD, sd_mmc_info->card_rca<<16, RESPONSE_R1B, response);
#endif
	if (ret)
		return ret;

	if(sd_mmc_info->card_type == CARD_TYPE_EMMC)
	{
		ret = sd_send_cmd_hw(sd_mmc_info,MMC_SEND_EXT_CSD, 0, RESPONSE_R1, response, mmc_ext_csd_buf, sizeof(MMC_REG_EXT_CSD_t), 1);		
        if(ret)
            return ret;
            
        mmc_ext_csd_reg = (MMC_REG_EXT_CSD_t *)mmc_ext_csd_buf;
		//my_disp_csd_ext(mmc_ext_csd_reg);
        sd_mmc_info->blk_nums = *((unsigned *)&mmc_ext_csd_reg->SEC_COUNT);	
        sd_mmc_info->blk_len = 512;
	}
	printk("total block nums : 0x%x(%d)\n",sd_mmc_info->blk_nums, sd_mmc_info->blk_nums);

	if(sd_mmc_info->card_type == CARD_TYPE_SD || sd_mmc_info->card_type == CARD_TYPE_SDHC)
	{
		SD_REG_SCR_t scr;
		ret = sd_read_reg_scr(sd_mmc_info, &scr);
		if(ret)
		{
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured at line: %d in file %s\n", sd_error_to_string(ret),__LINE__,__FILE__);
#endif			
			return ret;
		}

		sd_mmc_info->spec_version = scr.SD_SPEC;
		if (scr.SD_SPEC3)
			sd_mmc_info->spec_version += 1;
		
		if(sd_mmc_info->disable_wide_bus)
			scr.SD_BUS_WIDTHS = SD_BUS_SINGLE;
			
		if(!scr.SD_BUS_WIDTHS)
			scr.SD_BUS_WIDTHS = SD_BUS_WIDE | SD_BUS_SINGLE;
			
		if(scr.SD_BUS_WIDTHS & SD_BUS_WIDE)
		{//then set to 4bits width
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
			{
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, 0, 0, 1);
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x02, RESPONSE_R1, response, 0, 0, 1); //0 1bit, 10=4bits
			}
#endif			
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
			{
				ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response);
				ret = sd_send_cmd_sw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x02, RESPONSE_R1, response); //0 1bit, 10=4bits
			}
#endif
			if(ret)
			{
				sd_mmc_info->bus_width = SD_BUS_SINGLE;
			}
			else
			{
				sd_mmc_info->bus_width = SD_BUS_WIDE;
#ifdef SD_MMC_HW_CONTROL
				if(SD_WORK_MODE == CARD_HW_MODE)
				{
					if (using_sdxc_controller)
					{
						unsigned long sdxc_ctrl = 0;
						SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
						sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
						sdxc_ctrl_reg->dat_width = 1;
						WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
					}
					else
					{
						unsigned long sdio_config = 0;
						SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
						sdio_config = READ_CBUS_REG(SDIO_CONFIG);
						config_reg->bus_width = 1;
						WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
					}
				}
#endif
			}
		}
		else
		{//then set to 1bits width
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
			{
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, 0, 0, 1);
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x00, RESPONSE_R1, response, 0, 0, 1); //0 1bit, 10=4bits
			}
#endif
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
			{
				ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response);
				ret = sd_send_cmd_sw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x00, RESPONSE_R1, response); //0 1bit, 10=4bits
			}
#endif
			
			sd_mmc_info->bus_width = SD_BUS_SINGLE;
		}
	}
	else    //MMC card
	{
	    if(sd_mmc_info->mmc_spec_version == SPEC_VERSION_40_41)
	    {
#ifdef SD_MMC_HW_CONTROL
            if(SD_WORK_MODE == CARD_HW_MODE)
            {
                ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b70100, RESPONSE_R1, response, 0, 0, 1);
            }
#endif			
#ifdef SD_MMC_SW_CONTROL
            if(SD_WORK_MODE == CARD_SW_MODE)
            {
                ret = sd_send_cmd_sw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b70100, RESPONSE_R1, response);
            }
#endif
            if(ret)
            {
                sd_mmc_info->bus_width = SD_BUS_SINGLE;
            }
            else
            {
                sd_mmc_info->bus_width = SD_BUS_WIDE;
#ifdef SD_MMC_HW_CONTROL
                if(SD_WORK_MODE == CARD_HW_MODE)
                {
					if (using_sdxc_controller)
					{
						unsigned long sdxc_ctrl = 0;
						SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
						sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
						sdxc_ctrl_reg->dat_width = 1;
						WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
					}
					else
					{
	                    unsigned long sdio_config = 0;
	                    SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
	                    sdio_config = READ_CBUS_REG(SDIO_CONFIG);
	                    config_reg->bus_width = 1;
	                    WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
					}
                }
#endif
            }
        }
	    else
        {
            sd_mmc_info->bus_width = SD_BUS_SINGLE;
        }
	}

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if (using_sdxc_controller)
		{
			unsigned long sdxc_clk = 0;
			SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
			sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
			sdxc_clk_reg->clk_ctl_en = 0;
			WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			sdxc_clk_reg->clk_div = CLK_DIV_25M;
			sdxc_clk_reg->clk_en = 1;
			sdxc_clk_reg->clk_ctl_en = 1;
			WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
		}
		else
		{
			unsigned long sdio_config = 0;
			SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
			sdio_config = READ_CBUS_REG(SDIO_CONFIG);
			if(sd_mmc_info->disable_high_speed == 1)
			{
				config_reg->cmd_clk_divide = 4;
			}
			else
			{
				config_reg->cmd_clk_divide = 3;
			}

			WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
			sd_mmc_info->sdio_clk = clk_get_rate(clk_get_sys("clk81", NULL))/2000000/(config_reg->cmd_clk_divide + 1);
			sd_mmc_info->sdio_clk_unit = 1000/sd_mmc_info->sdio_clk;	
		}
	}
#endif

	sd_mmc_info->operation_mode = DATA_TRANSFER_MODE;
	
	sd_mmc_check_wp(sd_mmc_info);

	return SD_MMC_NO_ERROR;
}

int sdxc_mmc_staff_init(SD_MMC_Card_Info_t *sd_mmc_info)
{
	//SDXC_Send_Reg_t *sdxc_send_reg;
	SDXC_Ctrl_Reg_t *sdxc_ctrl_reg;
	//SDXC_Status_Reg_t *sdxc_status_reg;
	SDXC_Clk_Reg_t *sdxc_clk_reg;
	SDXC_PDMA_Reg_t *sdxc_pdma_reg;
	SDXC_Misc_Reg_t *sdxc_misc_reg;

	unsigned int sdxc_ctrl, sdxc_clk, sdxc_misc, sdxc_pdma;
	//unsigned int sdxc_send, sdxc_status;
	unsigned int sdxc_srst;

    sd_mmc_prepare_power(sd_mmc_info);   
	sd_mmc_power_on(sd_mmc_info);
	
	sdxc_clk = 0;
	sdxc_clk_reg = (void *)&sdxc_clk;
	sdxc_clk_reg->clk_ctl_en = 0;
	WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
	sdxc_clk_reg->clk_in_sel = 1;
	sdxc_clk_reg->clk_div = CLK_DIV_IDENTIFY; //sdxx
	sdxc_clk_reg->clk_en = 1;
	sdxc_clk_reg->clk_jic_control = 0;
	sdxc_clk_reg->clk_ctl_en = 1;
	WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);

	sdxc_misc = READ_CBUS_REG(SD_REG7_MISC);
	sdxc_misc_reg = (void *)&sdxc_misc;
	sdxc_misc_reg->manual_stop = 1;
	WRITE_CBUS_REG(SD_REG7_MISC, sdxc_misc);

	sdxc_ctrl = 0;
	sdxc_ctrl_reg = (void *)&sdxc_ctrl;
	sdxc_ctrl_reg->dat_width = 0;
	sdxc_ctrl_reg->rx_timeout = 0x40;
	sdxc_ctrl_reg->rc_period = 0x08;
	sdxc_ctrl_reg->endian = 0x03;
	WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);

	sdxc_pdma = READ_CBUS_REG(SD_REG6_PDMA);
	sdxc_pdma_reg = (void *)&sdxc_pdma;
	sdxc_pdma_reg->dma_mode = 1;
	WRITE_CBUS_REG(SD_REG6_PDMA, sdxc_pdma);

	sdxc_srst = 0x27;	//0x3F -- 0x27 : exclude rx_dphy_srst & tx_dphy_srst
	WRITE_CBUS_REG(SD_REGB_SRST, sdxc_srst);	
	sd_delay_ms(1);

	sd_sdio_enable(sd_mmc_info->io_pad_type);

/*
	{
		unsigned int pullup;
		pullup = READ_CBUS_REG(PAD_PULL_UP_REG3);
		pullup |= 0xFF;
		WRITE_CBUS_REG(PAD_PULL_UP_REG3, pullup);		
	}
*/
	sd_mmc_info->card_type = CARD_TYPE_NONE;
	sd_mmc_info->operation_mode = CARD_INDENTIFICATION_MODE;
	sd_mmc_info->bus_width = SD_BUS_SINGLE;
    sd_mmc_info->spec_version = SPEC_VERSION_10_101;
    sd_mmc_info->speed_class = NORMAL_SPEED;
	
	sd_mmc_info->card_rca = 0;
	
	sd_mmc_info->blk_len = 0;
	sd_mmc_info->blk_nums = 0;
	
	sd_mmc_info->clks_nac = SD_MMC_TIME_NAC_DEFAULT;
	
	sd_mmc_info->inited_flag = 0;
	sd_mmc_info->removed_flag = 0;
	
	sd_mmc_info->write_protected_flag = 0;
	sd_mmc_info->single_blk_failed = 1;

	sd_mmc_info->support_uhs_mode = 0;

	//sd_mmc_info->disable_wide_bus = 1;	//sdxx
	sd_mmc_info->read_multi_block_failed = 0;
	sd_mmc_info->write_multi_block_failed = 0;	//gg

	return SD_MMC_NO_ERROR;
}

int sdxc_mmc_init(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int error;
	
	sdio_close_host_interrupt(SDIO_IF_INT);

	if(++sd_mmc_info->init_retry > SD_MMC_INIT_RETRY)
		return SD_MMC_ERROR_DRIVER_FAILURE;

#ifdef  SD_MMC_DEBUG
	Debug_Printf("\nSDXC initialization started......\n");
#endif

	error = sdxc_mmc_staff_init(sd_mmc_info);
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sdxc_mmc_staff_init()()\n", sd_error_to_string(error));
#endif
		goto error;
	}

	sd_mmc_set_input(sd_mmc_info);
	error = sd_hw_reset(sd_mmc_info);
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_hw_reset()\n", sd_error_to_string(error));
#endif
		goto error;
	}

	sd_delay_ms(200);
	error = sd_voltage_validation(sd_mmc_info);
	
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_voltage_validation()\n", sd_error_to_string(error));
#endif
		goto error;
	}

	error = sd_identify_process(sd_mmc_info);
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_identify_process()\n", sd_error_to_string(error));
#endif
		goto error;
	}

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		{
		if(!sd_mmc_info->disable_high_speed)	
		{
	    	error = sd_mmc_switch_function(sd_mmc_info);
	    	if(error)
	    	{
#ifdef  SD_MMC_DEBUG
		    	Debug_Printf("#%s error occured in sd_switch_funtion()\n", sd_error_to_string(error));
#endif
			//return error;
	    	}
		}
	}
#endif

	if(sd_mmc_info->card_type == CARD_TYPE_SDIO)
	{
		error = sd_check_sdio_card_type(sd_mmc_info);
		if(error)
	    {
#ifdef  SD_MMC_DEBUG
		    Debug_Printf("#%s error occured in sd_check_sdio_card_type()\n", sd_error_to_string(error));
#endif
			goto error;
	    }
	}

#ifdef SD_MMC_DEBUG
	{
		char *card_types[] = {
			"Unknown",
			"SD",
			"SDHC",
			"MMC",
			"EMMC",
			"SDIO"
		};
		char *card_str = card_types[sd_mmc_info->card_type];
		char *bus_str = sd_mmc_info->bus_width == SD_BUS_WIDE ? "Wide Bus" : "Single Bus";
		char *speed_str = sd_mmc_info->speed_class == HIGH_SPEED ? "High Speed" : "Normal Speed";
		Debug_Printf("sdxc_mmc_init() is completed successfully!\n");
		Debug_Printf("This %s card is working in %s and %s mode!\n\n", card_str, bus_str, speed_str);
	}
#endif

	sd_mmc_info->inited_flag = 1;
	sd_mmc_info->init_retry = 0;

	sd_mmc_info->sdxc_save_hw_io_flag = 1;
	sd_mmc_info->sdxc_save_hw_io_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
	sd_mmc_info->sdxc_save_hw_io_clk = READ_CBUS_REG(SD_REG4_CLKC);
	sd_gpio_enable(sd_mmc_info->io_pad_type);

	return SD_MMC_NO_ERROR;

error:
	sd_gpio_enable(sd_mmc_info->io_pad_type);
	return error;
}

int sd_mmc_init(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int error;
	/*
	{	//lin : clear pin mux
		WRITE_CBUS_REG(PERIPHS_PIN_MUX_2, 0);
		WRITE_CBUS_REG(PERIPHS_PIN_MUX_3, 0);
	}
	*/
	if ((sd_mmc_info->io_pad_type == SDXC_CARD_0_5) || 
		(sd_mmc_info->io_pad_type == SDXC_BOOT_0_11) ||
		(sd_mmc_info->io_pad_type == SDXC_GPIOX_0_9))
		return sdxc_mmc_init(sd_mmc_info);

	/*close IF INT before change to sd to avoid error IF INT*/
	sdio_close_host_interrupt(SDIO_IF_INT);
	WRITE_CBUS_REG(SDIO_CONFIG, 0);
	WRITE_CBUS_REG(SDIO_MULT_CONFIG, 0);

	if(sd_mmc_info->inited_flag && !sd_mmc_info->removed_flag)
	{
#ifdef SD_MMC_HW_CONTROL
	    if(SD_WORK_MODE == CARD_HW_MODE)
		    sd_sdio_enable(sd_mmc_info->io_pad_type);
#endif
#ifdef SD_MMC_SW_CONTROL
	    if(SD_WORK_MODE == CARD_SW_MODE)
	    {	
		    sd_gpio_enable(sd_mmc_info->io_pad_type);
	    }
#endif		
			error = sd_mmc_cmd_test(sd_mmc_info);
			if(!error)
				goto error;
	}
	if(++sd_mmc_info->init_retry > SD_MMC_INIT_RETRY)
		return SD_MMC_ERROR_DRIVER_FAILURE;

#ifdef  SD_MMC_DEBUG
	Debug_Printf("\nSD/MMC initialization started......\n");
#endif
	
	error = sd_mmc_staff_init(sd_mmc_info);
	
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_mmc_staff_init()()\n", sd_error_to_string(error));
#endif
		goto error;
	}
	sd_mmc_set_input(sd_mmc_info);
	error = sd_hw_reset(sd_mmc_info);
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_hw_reset()\n", sd_error_to_string(error));
#endif
		goto error;
	}

	error = sd_voltage_validation(sd_mmc_info);
	
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_voltage_validation()\n", sd_error_to_string(error));
#endif
		goto error;
	}

	error = sd_identify_process(sd_mmc_info);
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_identify_process()\n", sd_error_to_string(error));
#endif
		goto error;
	}


#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		{
		if(!sd_mmc_info->disable_high_speed)	
		{
	    	error = sd_mmc_switch_function(sd_mmc_info);
	    	if(error)
	    	{
#ifdef  SD_MMC_DEBUG
		    	Debug_Printf("#%s error occured in sd_switch_funtion()\n", sd_error_to_string(error));
#endif
			//return error;
	    	}
		}
	}
#endif

	if(sd_mmc_info->card_type == CARD_TYPE_SDIO)
	{
		error = sd_check_sdio_card_type(sd_mmc_info);
		if(error)
	    {
#ifdef  SD_MMC_DEBUG
		    Debug_Printf("#%s error occured in sd_check_sdio_card_type()\n", sd_error_to_string(error));
#endif
			goto error;
	    }
	}

#ifdef SD_MMC_DEBUG
	{
		char *card_types[] = {
			"Unknown",
			"SD",
			"SDHC",
			"MMC",
			"EMMC",
			"SDIO"
		};
		char *card_str = card_types[sd_mmc_info->card_type];
		char *bus_str = sd_mmc_info->bus_width == SD_BUS_WIDE ? "Wide Bus" : "Single Bus";
		char *speed_str = sd_mmc_info->speed_class == HIGH_SPEED ? "High Speed" : "Normal Speed";
                   if(!sd_mmc_info->sdio_clk && sd_mmc_info->sdio_clk_unit)
                        sd_mmc_info->sdio_clk = 1000/sd_mmc_info->sdio_clk_unit;
		Debug_Printf("sd_mmc_init() is completed successfully!\n");
		Debug_Printf("This %s card is working in %s and at %dMHz %s mode!\n\n", card_str, bus_str, sd_mmc_info->sdio_clk,speed_str);
	}

#endif

	sd_mmc_info->inited_flag = 1;
	sd_mmc_info->init_retry = 0;

	sd_mmc_info->sd_save_hw_io_flag = 1;
	sd_mmc_info->sd_save_hw_io_config = READ_CBUS_REG(SDIO_CONFIG);
	sd_mmc_info->sd_save_hw_io_mult_config = READ_CBUS_REG(SDIO_MULT_CONFIG);
	sd_gpio_enable(sd_mmc_info->io_pad_type);

	return SD_MMC_NO_ERROR;

error:
	sd_gpio_enable(sd_mmc_info->io_pad_type);
	return error;
}

void sd_mmc_io_config(SD_MMC_Card_Info_t *sd_mmc_info)
{
	sd_gpio_enable(sd_mmc_info->io_pad_type);
	
	sd_set_cmd_output();
	sd_set_cmd_value(1);
	sd_set_clk_output();
	sd_set_clk_high();
	sd_set_dat0_3_input();
}

int sd_mmc_staff_init(SD_MMC_Card_Info_t *sd_mmc_info)
{
	unsigned int sdio_config, sdio_multi_config;
	SDIO_Config_Reg_t *config_reg;

    sd_mmc_prepare_power(sd_mmc_info);
   
	sd_mmc_power_on(sd_mmc_info);
#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		sdio_multi_config = (READ_CBUS_REG(SDIO_MULT_CONFIG) & 0x00000003);
		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sdio_multi_config);

		sdio_config = 0;
		config_reg = (void *)&sdio_config;
		config_reg->cmd_clk_divide = 375;
		config_reg->cmd_argument_bits = 39;
		config_reg->m_endian = 3;
		config_reg->write_Nwr = 2;
		config_reg->write_crc_ok_status = 2;
		WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
		sd_mmc_info->sdio_clk_unit = (1000/SD_MMC_IDENTIFY_CLK)*1000;

		sd_sdio_enable(sd_mmc_info->io_pad_type);
	}
#endif
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		sd_mmc_io_config(sd_mmc_info);
#endif

	sd_mmc_info->card_type = CARD_TYPE_NONE;
	sd_mmc_info->operation_mode = CARD_INDENTIFICATION_MODE;
	sd_mmc_info->bus_width = SD_BUS_SINGLE;
    sd_mmc_info->spec_version = SPEC_VERSION_10_101;
    sd_mmc_info->speed_class = NORMAL_SPEED;
	
	sd_mmc_info->card_rca = 0;
	
	sd_mmc_info->blk_len = 0;
	sd_mmc_info->blk_nums = 0;
	
	sd_mmc_info->clks_nac = SD_MMC_TIME_NAC_DEFAULT;
	
	sd_mmc_info->inited_flag = 0;
	sd_mmc_info->removed_flag = 0;
	
	sd_mmc_info->write_protected_flag = 0;
	sd_mmc_info->single_blk_failed = 1;

	sd_mmc_info->support_uhs_mode = 0;
	
	return SD_MMC_NO_ERROR;
}
#pragma pack(1)
typedef struct _MySCR {
	unsigned SD_SPEC:4;	//SD Card spec. Version
	unsigned SCR_STRUCTURE:4;	//SCR Structure
	
	unsigned SD_BUS_WIDTHS:4;	//DAT Bus widths supported
	unsigned SD_SECURITY:3;	//SD Security Support
	unsigned DATA_STAT_AFTER_ERASE:1;	//data_status_after erases

	unsigned Rsv1 : 7;
	unsigned SD_SPEC3 : 1;
	unsigned CMD_SUPPORT : 2;
	unsigned Rsv2 : 6;
	
	////unsigned Reserved1:16;	//for alignment
	unsigned long Reserved2;	
} MySCR;

typedef struct _MyCID {
	unsigned char MID;	//Manufacturer ID
	char OID[2];		//OEM/Application ID	
	char PNM[5];		//Product Name
	unsigned char PRV;	//Product Revision
	unsigned long PSN;	//Serial Number
	unsigned MDT_high:4;	//Manufacture Date Code
	unsigned Reserved:4;	
	unsigned MDT_low:8;	// MDT = (MDT_high << 8) | MDT_low
	unsigned NotUsed:1;
	unsigned CRC:7;	//CRC7 checksum
} MyCID;

#pragma pack()

static void my_disp_cid(MyCID *cid)
{
	int size;
	int i;
	unsigned char *pcid;
	unsigned long mdt;

	sdpro_dbg("\n***********CID*************\n");
	pcid = (unsigned char*)cid;

	size = sizeof (MyCID);
	for (i = 0 ; i < size; i++)
	{
		sdpro_dbg("%02x:", pcid[i]);
	}

	sdpro_dbg("\nMID[8]=%d\n", cid->MID);
	sdpro_dbg("OID[16]=%c%c\n", cid->OID[0], cid->OID[1]);
	sdpro_dbg("PNM[40]=%c%c%c%c%c\n", cid->PNM[0], cid->PNM[1], cid->PNM[2], cid->PNM[3], cid->PNM[4]);
	sdpro_dbg("PRV[8]=%d\n", cid->PRV);
	sdpro_dbg("PSN[32]=0x%x\n", (unsigned int)cid->PSN);

	sdpro_dbg("Reserved[4]=%d\n", cid->Reserved);
	mdt = (cid->MDT_high << 8) | cid->MDT_low;
	sdpro_dbg("year[8].month[4]=%ld.%ld\n", (mdt >> 4) + 2000, mdt & 0xF);

	sdpro_dbg("NotUsed[1]=%d\n", cid->NotUsed);
	sdpro_dbg("CRC[7]=%d\n\n", cid->CRC);

	//printk("*************** from mmc *************\n");
	//mmc_decode_cid(cid);
}

static void my_disp_scr(MySCR *scr)
{
	int size;
	int i;
	unsigned char *pscr;

	sdpro_dbg("\n***********SCR*************\n");
	pscr = (unsigned char*)scr;

	size = sizeof (MySCR);
	for (i = 0 ; i < size; i++)
	{
		sdpro_dbg("%02x:", pscr[i]);
	}

	sdpro_dbg("\nSD_SPEC[4]=%d\n", scr->SD_SPEC);
	sdpro_dbg("SCR_STRUCTURE[4]=%d\n", scr->SCR_STRUCTURE);

	sdpro_dbg("SD_BUS_WIDTHS[4]=%d\n", scr->SD_BUS_WIDTHS);
	sdpro_dbg("SD_SECURITY[3]=%d\n", scr->SD_SECURITY);
	sdpro_dbg("DATA_STAT_AFTER_ERASE[1]=%d\n", scr->DATA_STAT_AFTER_ERASE);

	sdpro_dbg("Rsv1[7]=%d\n", scr->Rsv1);
	sdpro_dbg("SD_SPEC3[1]=%d\n", scr->SD_SPEC3);

	sdpro_dbg("CMD_SUPPORT[2]=%d\n", scr->CMD_SUPPORT);
	sdpro_dbg("Rsv2[6]=%d\n", scr->Rsv2);

	sdpro_dbg("Reserved2[32]=%ld\n\n", scr->Reserved2);	
}

//Read Operation Conditions Register
int sd_read_reg_ocr(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_OCR_t * ocr);
//Read Card_Identification Register
int sd_read_reg_cid(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_CID_t * cid)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	SD_Response_R2_CID_t *res_cid;

	res_cid = (SD_Response_R2_CID_t *)response;
	sd_clear_response(response);

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_CID, sd_mmc_info->card_rca<<16, RESPONSE_R2_CID, response, NULL, 0, 1);
		if(ret)
			return ret;		
		memcpy(cid, (unsigned char*)&res_cid->cid, sizeof(SD_REG_CID_t));
		my_disp_cid((MyCID *)cid);
	}
#endif

#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{
		ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_SEND_CID,sd_mmc_info->card_rca<<16, RESPONSE_R2_CID, response);
		if(ret)
			return ret;
		memcpy(cid, (unsigned char*)&res_cid->cid, sizeof(SD_REG_CID_t));
	}
#endif

	return SD_MMC_NO_ERROR;
}

//Read Card-Specific Data Register
int sd_read_reg_csd(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_CSD_t * csd);
//Read Relative Card Address Register
int sd_read_reg_rca(SD_MMC_Card_Info_t *sd_mmc_info, unsigned short * rca);
//Read Driver Stage Register
int sd_read_reg_dsr(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_DSR_t * dsr);
//Read SD CARD Configuration Register

int sd_read_reg_scr(SD_MMC_Card_Info_t *sd_mmc_info, SD_REG_SCR_t * scr)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
#ifdef SD_MMC_SW_CONTROL
	unsigned short crc16;
#endif
	
	sd_clear_response(response);

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, 0, 0, 1);   
		 if(ret)
			return ret;
		
		ret = sd_send_cmd_hw(sd_mmc_info, SD_SEND_SCR, 0, RESPONSE_R1, response, sd_mmc_info->sd_mmc_buf, sizeof(SD_REG_SCR_t), 1);
		if(ret)
			return ret;
		memcpy(scr, sd_mmc_info->sd_mmc_buf, sizeof(SD_REG_SCR_t));
	}
#endif		
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	{
		ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response);   
		if(ret)
			return ret;
	
		ret = sd_send_cmd_sw(sd_mmc_info, SD_SEND_SCR, 0, RESPONSE_NONE, response);
		if(ret)
			return ret;

		ret = sd_get_dat0_data(sd_mmc_info, sizeof(SD_REG_SCR_t), (unsigned char *)scr, &crc16);
		   if(ret)
			return ret;
	}
#endif

	my_disp_scr((MySCR*)scr);
	return SD_MMC_NO_ERROR;
}

//Check if any card is connected to adapter
#ifdef SD_MMC_SW_CONTROL
SD_Card_Type_t sd_mmc_check_present(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int retry, ret;
	
	unsigned char response[MAX_RESPONSE_BYTES];
	SD_Response_R3_t * r3;
	
	//Detect if SD card is inserted first
	for(retry = 0; retry < MAX_CHECK_INSERT_RETRY; retry++)
	{
		ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_CMD, 0, RESPONSE_R1, response);
		if(ret)
			continue;

		ret = sd_send_cmd_sw(sd_mmc_info, SD_APP_OP_COND, 0x00200000, RESPONSE_R3, response);   // 0x00200000: 3.3v~3.4v
		r3 = (SD_Response_R3_t *)response;
		
		if((ret == SD_MMC_NO_ERROR) && r3->ocr.Card_Busy)
			return CARD_TYPE_SD;
	} 
	
	sd_sw_reset(sd_mmc_info);
	
	//No SD card, detect if MMC card is inserted then
	for(retry = 0; retry < MAX_CHECK_INSERT_RETRY; retry++)
	{
		ret = sd_send_cmd_sw(sd_mmc_info, MMC_SEND_OP_COND, 0x00200000, RESPONSE_R3, response); // 0x00200000: 3.3v~3.4v
		r3 = (SD_Response_R3_t *)response;
		
		if((ret == SD_MMC_NO_ERROR) && r3->ocr.Card_Busy)
			return CARD_TYPE_MMC;
	}
	
	return CARD_TYPE_NONE;
}
#endif
//Check if any card is inserted according to pull up resistor
int sd_mmc_check_insert(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int level;
	
	if(sd_mmc_info->sd_mmc_get_ins)
	{
		level = sd_mmc_info->sd_mmc_get_ins();
	}
	else
	{	
		sd_set_ins_input();
		level = sd_get_ins_value();
	}

	if(sd_mmc_info->init_retry | sd_mmc_info->inited_flag){
		if(level){
			udelay(20);
			level = sd_get_ins_value();
			if(level){
				udelay(20);
				level = sd_get_ins_value();
			}
		}
	}
	
	if (level)
	{
		if(sd_mmc_info->init_retry)
		{
			sd_mmc_power_off(sd_mmc_info);
			sd_mmc_info->init_retry = 0;
		}
		if(sd_mmc_info->inited_flag)
		{
			sd_mmc_power_off(sd_mmc_info);
			sd_mmc_info->removed_flag = 1;
			sd_mmc_info->inited_flag = 0;
		}

		return 0;       //No card is inserted
	}
	else
	{
		return 1;       //A card is inserted
	}
}

//Read data from SD/MMC card
int sd_mmc_read_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error = 0;
	unsigned long lba_nums;

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if (sd_mmc_info->sd_save_hw_io_flag) {
	    		WRITE_CBUS_REG(SDIO_CONFIG, sd_mmc_info->sd_save_hw_io_config);
	      		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sd_mmc_info->sd_save_hw_io_mult_config);
	    	}	
		if (sd_mmc_info->sdxc_save_hw_io_flag) {
			WRITE_CBUS_REG(SD_REG2_CNTL, sd_mmc_info->sdxc_save_hw_io_ctrl);
			WRITE_CBUS_REG(SD_REG4_CLKC, 0);
			WRITE_CBUS_REG(SD_REG4_CLKC, sd_mmc_info->sdxc_save_hw_io_clk);
		}
	}
#endif       
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
	   	sd_mmc_io_config(sd_mmc_info);
#endif

	lba_nums = sd_mmc_info->blk_len;
	lba_nums = (byte_cnt + sd_mmc_info->blk_len - 1) / lba_nums;
	if(lba_nums == 0)
	{
		error = SD_MMC_ERROR_BLOCK_LEN;
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_mmc_read_data()\n", sd_error_to_string(error));
#endif
		return error;
	}
	else if ((lba_nums == 1) && !sd_mmc_info->single_blk_failed)
	{
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
			error = sd_read_single_block_hw(sd_mmc_info, lba, data_buf);
#endif 
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
					error = sd_read_single_block_sw(sd_mmc_info, lba, data_buf);
#endif 		
		if(error)
		{			
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sd_read_single_block()\n", sd_error_to_string(error));
#endif
			return error;
		}
	}
	else
	{		
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
		{
			error = sd_read_multi_block_hw(sd_mmc_info, lba, lba_nums, data_buf);
			if(error)
				error = sd_read_multi_block_hw(sd_mmc_info, lba, lba_nums, data_buf);
		}
#endif 
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
			error = sd_read_multi_block_sw(sd_mmc_info, lba, lba_nums, data_buf);
#endif		

		if(error)
		{			
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sd_read_multi_block()\n", sd_error_to_string(error));
#endif
			return error;
		}
	}	
	
	return SD_MMC_NO_ERROR;
}

//Write data to SD/MMC card
int sd_mmc_write_data(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error = 0;
	unsigned long lba_nums;

	if(sd_mmc_info->write_protected_flag)
	{
		error = SD_MMC_ERROR_WRITE_PROTECTED;
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_mmc_write_data()\n", sd_error_to_string(error));
#endif
		return error;
	}

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if (sd_mmc_info->sd_save_hw_io_flag) {
    		WRITE_CBUS_REG(SDIO_CONFIG, sd_mmc_info->sd_save_hw_io_config);
      		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sd_mmc_info->sd_save_hw_io_mult_config);
    	}		
		if (sd_mmc_info->sdxc_save_hw_io_flag) {
			WRITE_CBUS_REG(SD_REG2_CNTL, sd_mmc_info->sdxc_save_hw_io_ctrl);
			WRITE_CBUS_REG(SD_REG4_CLKC, 0);
			WRITE_CBUS_REG(SD_REG4_CLKC, sd_mmc_info->sdxc_save_hw_io_clk);
		}
	}
#endif        	
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)    	    
       	sd_mmc_io_config(sd_mmc_info);	  	
#endif

	//memcpy(sd_write_buf, data_buf, byte_cnt);
	lba_nums = sd_mmc_info->blk_len;
	lba_nums = (byte_cnt + sd_mmc_info->blk_len - 1) / lba_nums;
	if(lba_nums == 0)
	{
		error = SD_MMC_ERROR_BLOCK_LEN;
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_mmc_write_data()\n", sd_error_to_string(error));
#endif
		return error;
	}
	else if ((lba_nums == 1) && !sd_mmc_info->single_blk_failed)
	{
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
			error = sd_write_single_block_hw(sd_mmc_info, lba, data_buf);
#endif 
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
			error = sd_write_single_block_sw(sd_mmc_info, lba, data_buf);
#endif		
		if(error)
		{
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sd_write_single_block()\n", sd_error_to_string(error));
#endif
			return error;
		}
	}
	else
	{
#ifdef SD_MMC_HW_CONTROL
		if(SD_WORK_MODE == CARD_HW_MODE)
			error = sd_write_multi_block_hw(sd_mmc_info, lba, lba_nums, data_buf);
#endif 
#ifdef SD_MMC_SW_CONTROL
		if(SD_WORK_MODE == CARD_SW_MODE)
			error = sd_write_multi_block_sw(sd_mmc_info, lba, lba_nums, data_buf);
#endif		
		if(error)
		{
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sd_write_multi_block()\n", sd_error_to_string(error));
#endif
			return error;
		}
	}
	
	return SD_MMC_NO_ERROR;
}

#ifdef SD_MMC_SW_CONTROL
int sd_mmc_cmd_test(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];

	sd_clear_response(response);
	
	sd_mmc_io_config(sd_mmc_info);
	
	ret = sd_send_cmd_sw(sd_mmc_info, SD_MMC_SEND_STATUS, sd_mmc_info->card_rca<<16, RESPONSE_R1, response);   
	
	return ret;
}
#endif 

void sd_mmc_power_on(SD_MMC_Card_Info_t *sd_mmc_info)
{
    if(!sd_mmc_info->sd_mmc_power_delay){
        printk("need not do external power on\n");
        return;
    }
	sd_delay_ms(sd_mmc_info->sd_mmc_power_delay+1);
	
#ifdef SD_MMC_POWER_CONTROL

	if(sd_mmc_info->sd_mmc_power)
	{
		sd_mmc_info->sd_mmc_power(0);
	}
	else
	{
		sd_set_disable();
	}
	sd_delay_ms(sd_mmc_info->sd_mmc_power_delay);

	if(sd_mmc_info->sd_mmc_power)
	{
		if(sd_mmc_check_insert(sd_mmc_info)) //ensure card wasn't removed at this time 
		{
			sd_mmc_info->sd_mmc_power(1);
		}
	}
	else
	{
		if(sd_mmc_check_insert(sd_mmc_info)) //ensure card wasn't removed at this time 
		{
			sd_set_enable();
		}
	}
	sd_delay_ms(sd_mmc_info->sd_mmc_power_delay);
#else
	sd_delay_ms(10);
#endif
}

void sd_mmc_power_off(SD_MMC_Card_Info_t *sd_mmc_info)
{
#ifdef SD_MMC_POWER_CONTROL
	if(sd_mmc_info->sd_mmc_power)
	{
		sd_mmc_info->sd_mmc_power(0);
	}
	else
	{
		sd_set_disable();
		sd_set_pwr_input();
	}
#endif
}

//Check if Write-Protected switch is on
int sd_mmc_check_wp(SD_MMC_Card_Info_t *sd_mmc_info)
{
#ifdef SD_MMC_WP_CHECK
	int ret = 0;

	if(sd_mmc_info->sd_get_wp)
	{
		ret = sd_mmc_info->sd_get_wp();
	}
	else
	{	
		sd_set_wp_input();
		ret = sd_get_wp_value();
	}
		
	if (ret)
	{
		sd_mmc_info->write_protected_flag = 1;
		
		return 1;       //switch is on
	}
	else
	{
		return 0;       //switch is off
	}
#else
	return 0;
#endif
}

//check data lines consistency
int sd_check_data_consistency(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int error, func_num=0;
	unsigned char read_reg_data;
	unsigned read_addr, sdio_cis_addr;
#ifdef SD_MMC_SW_CONTROL
	unsigned char response[MAX_RESPONSE_BYTES];
#endif
	
	unsigned char *mbr_buf = sd_mmc_info->sd_mmc_buf;

	//This card is working in wide bus mode!
	memset(mbr_buf, 0, sd_mmc_info->blk_len);
	if(sd_mmc_info->bus_width == SD_BUS_WIDE)
	{
		if(sd_mmc_info->card_type == CARD_TYPE_SDIO)
		{
			for (func_num=0; func_num<sd_mmc_info->sdio_function_nums; func_num++) {

				read_addr = ((func_num << 8) | Common_CIS_Pointer3_REG);
				error = sdio_read_reg(sd_mmc_info, 0, read_addr, &read_reg_data);
			if(error)
				return error;
				sdio_cis_addr = read_reg_data;

				read_addr = ((func_num << 8) | Common_CIS_Pointer2_REG);
				error = sdio_read_reg(sd_mmc_info, 0, read_addr, &read_reg_data);
			if(error)
				return error;
				sdio_cis_addr = ((sdio_cis_addr << 8) | read_reg_data);

				read_addr = ((func_num << 8) | Common_CIS_Pointer1_REG);
				error = sdio_read_reg(sd_mmc_info, 0, read_addr, &read_reg_data);
				if (error)
					return error;
				sdio_cis_addr = ((sdio_cis_addr << 8) | read_reg_data);
				sd_mmc_info->sdio_cis_addr[func_num] = sdio_cis_addr;
				//printk("sdio cis addr is %x \n", sdio_cis_addr);

			}

			return SD_MMC_NO_ERROR;
		}
		//read MBR information
		error = sd_mmc_read_data(sd_mmc_info, 0, sd_mmc_info->blk_len, mbr_buf);
		if(error)
		{
			//error! retry again!
			error = sd_mmc_read_data(sd_mmc_info, 0, sd_mmc_info->blk_len, mbr_buf);
			if(error)
				return error;
		}
		
		//check MBR data consistency
		if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
		{
			//data consistency error! retry again!
			error = sd_mmc_read_data(sd_mmc_info, 0, sd_mmc_info->blk_len*2, mbr_buf);
			if(error)
				return error;

			//check MBR data consistency
			if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
			{
#ifdef  SD_MMC_DEBUG
				Debug_Printf("SD/MMC data consistency error in Wide Bus mode! Try Single Bus mode...\n");
#endif

				//error again! retry single bus mode!
#ifdef SD_MMC_SW_CONTROL
				if(SD_WORK_MODE == CARD_SW_MODE)	
				{				
					error = sd_send_cmd_sw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response);
					if(error)
						return error;
					error = sd_send_cmd_sw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x00, RESPONSE_R1, response); //0 1bit, 10=4bits
					if(error)
						return error;
				}
#endif					

				sd_mmc_info->bus_width = SD_BUS_SINGLE;
#ifdef SD_MMC_HW_CONTROL
				if(SD_WORK_MODE == CARD_HW_MODE)
				{
					unsigned long sdio_config = 0;
					SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
					sdio_config = READ_CBUS_REG(SDIO_CONFIG);
					config_reg->bus_width = 0;
					WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				}
#endif
			}
			else
			{
				sd_mmc_info->single_blk_failed = 1;
				return SD_MMC_NO_ERROR;
			}
		}
		else
		{
			return SD_MMC_NO_ERROR;
		}
	}

	//This card is working in single bus mode!
	memset(mbr_buf, 0, sd_mmc_info->blk_len);
	//read MBR information
	error = sd_mmc_read_data(sd_mmc_info, 0, sd_mmc_info->blk_len, mbr_buf);
	//For Kingston MMC mobile card
	error = sd_mmc_read_data(sd_mmc_info, 0, sd_mmc_info->blk_len, mbr_buf);
	if(error)
	{
		//error! retry again!
		error = sd_mmc_read_data(sd_mmc_info, 0, sd_mmc_info->blk_len, mbr_buf);
		if(error)
			return error;
	}
		
	//check MBR data consistency
	if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
	{
		//data consistency error! retry again!
		error = sd_mmc_read_data(sd_mmc_info, 0, sd_mmc_info->blk_len*2, mbr_buf);
		if(error)
			return error;

		//check MBR data consistency
		if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
		{
			return SD_MMC_ERROR_DATA_CRC;
		}
		else
		{
			sd_mmc_info->single_blk_failed = 1;
		}
	}

	return SD_MMC_NO_ERROR;
}

void sd_mmc_exit(SD_MMC_Card_Info_t *sd_mmc_info)
{
	if(sd_mmc_info->sd_mmc_io_release != NULL)
		sd_mmc_info->sd_mmc_io_release();

	if(sd_mmc_info->card_type == CARD_TYPE_SD)
		Debug_Printf("SD card unpluged!\n\n");
	else if(sd_mmc_info->card_type == CARD_TYPE_SDHC)
		Debug_Printf("SDHC card unpluged!\n\n");
	else if(sd_mmc_info->card_type == CARD_TYPE_SDIO)
		Debug_Printf("SDIO card unpluged!\n\n");
	else
		Debug_Printf("MMC card unpluged!\n\n");

	return;
}

void sd_mmc_prepare_init(SD_MMC_Card_Info_t *sd_mmc_info)
{
	if(sd_mmc_power_register != NULL)
		sd_mmc_info->sd_mmc_power = sd_mmc_power_register;
	if(sd_mmc_ins_register != NULL)	
		sd_mmc_info->sd_mmc_get_ins = sd_mmc_ins_register;
	if(sd_mmc_wp_register != NULL)	
		sd_mmc_info->sd_get_wp = sd_mmc_wp_register;
	if(sd_mmc_io_release_register != NULL)
		sd_mmc_info->sd_mmc_io_release = sd_mmc_io_release_register;
}

void sd_mmc_prepare_power(SD_MMC_Card_Info_t *sd_mmc_info)
{
    sd_gpio_enable(sd_mmc_info->io_pad_type);

	sd_set_cmd_output();
	sd_set_cmd_value(0);
	sd_set_clk_output();
	sd_set_clk_low();
	sd_set_dat0_3_output();
	sd_set_dat0_3_value(0);
}

void sd_mmc_set_input(SD_MMC_Card_Info_t *sd_mmc_info)
{
	sd_set_cmd_input();
	sd_set_clk_input();
	sd_set_dat0_3_input();
}

#define SWAP_SHORT(value)	(((value) >> 8) | (((value) & 0xFF) << 8))

static int try_uhs_mode(SD_MMC_Card_Info_t *sd_mmc_info, unsigned long arg, SD_UHS_I_MODE_t mode)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned char *status_data_buf = sd_mmc_info->sd_mmc_buf;
	SD_Switch_Function_Status_t *switch_funtion_status;

	unsigned short max_current_comsumption;
	unsigned short max_output_current = 400;

	SDXC_Clk_Reg_t *sdxc_clk_reg;
	unsigned long sdxc_clk;

	//sdxx	//reset to default mode
	sdxc_clk = 0;
	sdxc_clk_reg = (void *)&sdxc_clk;
	sdxc_clk_reg->clk_ctl_en = 0;
	WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
	sdxc_clk_reg->clk_in_sel = 1;
	sdxc_clk_reg->clk_div = CLK_DIV_IDENTIFY; //sdxx
	sdxc_clk_reg->clk_en = 1;
	sdxc_clk_reg->clk_ctl_en = 1;
	WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);

	switch_funtion_status = (SD_Switch_Function_Status_t *)status_data_buf;
	memset(status_data_buf, 0, 64);
	ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, arg, RESPONSE_R1, response, status_data_buf, 64, 1);
	if (ret)
		return ret;

	max_current_comsumption = SWAP_SHORT(switch_funtion_status->Max_Current_Consumption);
	if ((max_current_comsumption == 0) || (max_current_comsumption > max_output_current) || (switch_funtion_status->Function_Group_Status1 != (arg & 0x0F)))
		return SD_ERROR_SWITCH_FUNCTION_COMUNICATION;

	memset(status_data_buf, 0, 64);
	ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, arg | 0x80000000, RESPONSE_R1, response, status_data_buf, 64, 1);
	if (ret)
		return ret;

	max_current_comsumption = SWAP_SHORT(switch_funtion_status->Max_Current_Consumption);
	if ((max_current_comsumption == 0) || (max_current_comsumption > max_output_current) || (switch_funtion_status->Function_Group_Status1 != (arg & 0x0F)))
		return SD_ERROR_SWITCH_FUNCTION_COMUNICATION;

	sdxc_clk = 0;
	sdxc_clk_reg = (void *)&sdxc_clk;
	sdxc_clk_reg->clk_ctl_en = 0;
	WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
	sdxc_clk_reg->clk_in_sel = 1;
	switch (mode)
	{
		case SDR12 :
			sdxc_clk_reg->clk_div = CLK_DIV_25M;
			break;
		case SDR25 :
			sdxc_clk_reg->clk_div = CLK_DIV_50M;
			break;
		case SDR50 :
			sdxc_clk_reg->clk_div = CLK_DIV_100M;
			break;
		case SDR104 :
			sdxc_clk_reg->clk_div = CLK_DIV_208M;
			break;
		case DDR50 :	//same as SDR25
			sdxc_clk_reg->clk_div = CLK_DIV_50M;
			break;
		default : 
			sdxc_clk_reg->clk_div = CLK_DIV_IDENTIFY;
			break;
	}
	sdxc_clk_reg->clk_en = 1;
	sdxc_clk_reg->clk_ctl_en = 1;
	WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);

	sd_mmc_info->sdio_clk_unit = 1000/SD_MMC_TRANSFER_HIGHSPEED_CLK;
    sd_mmc_info->speed_class = HIGH_SPEED;	
    sd_mmc_info->uhs_mode = mode;
    
    return SD_MMC_NO_ERROR;
}

//total 64 bytes
static unsigned char sd_tuning_pattern[] = {
	0xFF, 0x0F, 0xFF, 0x00, 0xFF, 0xCC, 0xC3, 0xCC,
	0xC3, 0x3C, 0xCC, 0xFF, 0xFE, 0xFF, 0xFE, 0xEF,
	0xFF, 0xDF, 0xFF, 0xDD, 0xFF, 0xFB, 0xFF, 0xFB,
	0xBF, 0xFF, 0x7F, 0xFF, 0x77, 0xF7, 0xBD, 0xEF,
	0xFF, 0xF0, 0xFF, 0xF0, 0x0F, 0xFC, 0xCC, 0x3C,
	0xCC, 0x33, 0xCC, 0xCF, 0xFF, 0xEF, 0xFF, 0xEE,
	0xFF, 0xFD, 0xFF, 0xFD, 0xDF, 0xFF, 0xBF, 0xFF,
	0xBB, 0xFF, 0xF7, 0xFF, 0xF7, 0x7F, 0x7B, 0xDE
};

static int sd_check_tuning_pattern(unsigned char *pattern)
{
	int i;
	for (i = 0; i < 64; i++)
	{
		if (pattern[i] != sd_tuning_pattern[i])
			return -1;
	}
	return SD_MMC_NO_ERROR;
}

#define MAX_TUNING_TIMES	40

static int sd_tune_sample_clock(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int ret;
	int i;
	int start, end;
	int idx;
	unsigned char response[MAX_RESPONSE_BYTES];	
	unsigned char *tunning_pattern_buf = sd_mmc_info->sd_mmc_buf;
	unsigned char tuning_flag[MAX_TUNING_TIMES];

	for (i = 0; i < MAX_TUNING_TIMES; i++)
	{
		//set SDXC_Misc_Reg_t->dat_line_delay & SDXC_Misc_Reg_t->cmd_line_delay according to i

		ret = sd_send_cmd_hw(sd_mmc_info, SD_SEND_TUNNING_PATTERN, 0, RESPONSE_R1, response, tunning_pattern_buf, 64, 1);
		if (ret)
			return ret;

		if (sd_check_tuning_pattern(response) == 0)
			tuning_flag[i] = 1;
		else
			tuning_flag[i] = 0;
	}

	start = -1;
	end = -1;

	for (i = 0; i < MAX_TUNING_TIMES; i++)
	{
		if (tuning_flag[i] == 1)
		{
			start = i;
			break;
		}
	}

	if (start == -1)
		return -1;

	for (i = start + 1; i < MAX_TUNING_TIMES; i++)
	{
		if (tuning_flag[i] == 0)
		{
			end = i - 1;
		}
	}

	if (end == -1)
		end = MAX_TUNING_TIMES -1;

	idx = (start + end) / 2;

	//set  SDXC_Misc_Reg_t->dat_line_delay & SDXC_Misc_Reg_t->cmd_line_delay according to idx

	return SD_MMC_NO_ERROR;	
}

int sd_switch_to_uhs_mode(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned char *status_data_buf = sd_mmc_info->sd_mmc_buf;
	SD_Switch_Function_Status_t *switch_funtion_status;

	unsigned short max_current_comsumption;
	
	unsigned short support_current_limit;	//function group 4
	unsigned short support_driver_strength;	//funciton group 3
	unsigned short support_access_mode;		//function group 1

	switch_funtion_status = (SD_Switch_Function_Status_t *)status_data_buf;
	memset(status_data_buf, 0, 64);
	ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x00FFFFFF, RESPONSE_R1, response, status_data_buf, 64, 1);
	if (ret)
		return ret;

	max_current_comsumption = SWAP_SHORT(switch_funtion_status->Max_Current_Consumption);
	support_current_limit = SWAP_SHORT(switch_funtion_status->Function_Group[2]);
	support_driver_strength = SWAP_SHORT(switch_funtion_status->Function_Group[3]);
	support_access_mode = SWAP_SHORT(switch_funtion_status->Function_Group[5]);

	printk("max_current_comsumption : %dmA\n", max_current_comsumption);
	printk("support_current_limit : 0x%x\n", (unsigned int)support_current_limit);
	printk("support_driver_strength : 0x%x\n", (unsigned int)support_driver_strength);
	printk("support_access_mode : 0x%x\n", (unsigned int)support_access_mode);

	if (support_access_mode & DDR50)
	{
		if (try_uhs_mode(sd_mmc_info, 0x00FFFFF4, DDR50) == 0)
			return SD_MMC_NO_ERROR;
	}
	
	if (support_access_mode & SDR104)
	{
		if ((try_uhs_mode(sd_mmc_info, 0x00FFFFF3, SDR104) == 0) && (sd_tune_sample_clock(sd_mmc_info) == 0))
			return SD_MMC_NO_ERROR;
	}

	if (support_access_mode & SDR50)
	{
		if ((try_uhs_mode(sd_mmc_info, 0x00FFFFF2, SDR50) == 0) && (sd_tune_sample_clock(sd_mmc_info) == 0))
			return SD_MMC_NO_ERROR;
	}

	if (support_access_mode & SDR25)
	{
		if (try_uhs_mode(sd_mmc_info, 0x00FFFFF1, SDR25) == 0)
			return SD_MMC_NO_ERROR;
	}

	if (support_access_mode & SDR12)
	{
		if (try_uhs_mode(sd_mmc_info, 0x00FFFFF0, SDR12) == 0)
			return SD_MMC_NO_ERROR;
	}
	
	return SD_ERROR_SWITCH_FUNCTION_COMUNICATION;
}

static void my_disp_switch_status(SD_Switch_Function_Status_t *status)
{

	int i;
	sdpro_dbg("\n****************switch status****************\n");
	sdpro_dbg("Data_Struction_Verion : %d\n", status->Data_Struction_Verion);
	sdpro_dbg("Max_Current_Consumption : %d\n", SWAP_SHORT(status->Max_Current_Consumption));
	for (i = 0; i < 6; i++)
	{
		sdpro_dbg("Support[%d] : 0x%x	", i, SWAP_SHORT(status->Function_Group[i]));
		sdpro_dbg("Busy[%d] : 0x%x\n", i, SWAP_SHORT(status->Function_Status_In_Group[i]));
	}
	
	sdpro_dbg("Function_Group_Status6 : %d\n", status->Function_Group_Status6);
	sdpro_dbg("Function_Group_Status5 : %d\n", status->Function_Group_Status5);
	sdpro_dbg("Function_Group_Status4 : %d\n", status->Function_Group_Status4);
	sdpro_dbg("Function_Group_Status3 : %d\n", status->Function_Group_Status3);
	sdpro_dbg("Function_Group_Status2 : %d\n", status->Function_Group_Status2);
	sdpro_dbg("Function_Group_Status1 : %d\n\n", status->Function_Group_Status1);
/*
	unsigned short max_current_comsumption;
	
	unsigned short support_current_limit;
	unsigned short support_driver_strength;
	unsigned short support_access_mode;	
	
	max_current_comsumption = SWAP_SHORT(status->Max_Current_Consumption);
	support_current_limit = SWAP_SHORT(status->Function_Group[2]);
	support_driver_strength = SWAP_SHORT(status->Function_Group[3]);
	support_access_mode = SWAP_SHORT(status->Function_Group[5]);

	printk("max_current_comsumption : %dmA\n", max_current_comsumption);
	printk("support_current_limit : 0x%x\n", (unsigned int)support_current_limit);
	printk("support_driver_strength : 0x%x\n", (unsigned int)support_driver_strength);
	printk("support_access_mode : 0x%x\n", (unsigned int)support_access_mode);
*/
}

static void my_disp_ss(unsigned char *pss, int size)
{
	int i;
	for (i = 0 ; i < size; i++)
	{
		sdpro_dbg("%02x", pss[i]);
		if ((i % 4) == 3)
			sdpro_dbg(" ");
		if ((i % 16) == 15)
			sdpro_dbg("\n");
	}
}

int sd_mmc_switch_function(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned char *status_data_buf = sd_mmc_info->sd_mmc_buf;
	unsigned char *mmc_ext_csd_buf = sd_mmc_info->sd_mmc_buf;
	SD_Switch_Function_Status_t *switch_funtion_status;
	MMC_REG_EXT_CSD_t *mmc_ext_csd_reg;
	unsigned char read_reg_data;
	unsigned char write_reg_data;
	unsigned long sdio_config = 0;
	SDIO_Config_Reg_t *config_reg = NULL;

	if (sd_mmc_info->support_uhs_mode && using_sdxc_controller)
	{
		ret = sd_switch_to_uhs_mode(sd_mmc_info);
		if (ret)
			return ret;
	}
	else if(sd_mmc_info->spec_version || sd_mmc_info->card_type == CARD_TYPE_SDHC)
	{    
	    memset(status_data_buf, 0, 64);
		ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x00FFFF01, RESPONSE_R1, response, status_data_buf, 64, 1);
	    if(ret)
		    return ret;
		my_disp_ss(status_data_buf, 64);

	    switch_funtion_status = (SD_Switch_Function_Status_t *)status_data_buf;
	    if(switch_funtion_status->Max_Current_Consumption == 0)
		    return SD_ERROR_SWITCH_FUNCTION_COMUNICATION;

	    if(!((switch_funtion_status->Function_Group[5]>>8) & 0x02)) 
	    {	
		    return SD_ERROR_NO_FUNCTION_SWITCH;
	    }
	    else	
	    {
	    	my_disp_switch_status(switch_funtion_status);		//check disp
	    	
		    memset(status_data_buf, 0, 64);
			ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x80FFFF01, RESPONSE_R1, response, status_data_buf, 64, 1);	
		    if(ret)
			    return ret;
			my_disp_ss(status_data_buf, 64);

		    switch_funtion_status = (SD_Switch_Function_Status_t *)status_data_buf;
		    if(switch_funtion_status->Max_Current_Consumption == 0 || switch_funtion_status->Function_Group_Status1 != 0x01)
			    return SD_ERROR_SWITCH_FUNCTION_COMUNICATION;

			my_disp_switch_status(switch_funtion_status);		//switch disp

			if (using_sdxc_controller)
			{				
				unsigned long sdxc_clk = 0;
				SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
				sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
				sdxc_clk_reg->clk_ctl_en = 0;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				sdxc_clk_reg->clk_div = CLK_DIV_50M; //sdxx
				sdxc_clk_reg->clk_en = 1;
				sdxc_clk_reg->clk_ctl_en = 1;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			}
			else
			{
				sdio_config = 0;
				config_reg = (void *)&sdio_config;
				sdio_config = READ_CBUS_REG(SDIO_CONFIG);
				config_reg->cmd_clk_divide = 2;//aml_system_clk / (2*SD_MMC_TRANSFER_HIGHSPEED_CLK) -1;
				WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				sd_mmc_info->sdio_clk = clk_get_rate(clk_get_sys("clk81", NULL))/2000000/(config_reg->cmd_clk_divide + 1);
				sd_mmc_info->sdio_clk_unit = 1000/sd_mmc_info->sdio_clk;				
			}

            sd_mmc_info->speed_class = HIGH_SPEED;		
	    }
	}
    else if(sd_mmc_info->mmc_spec_version == SPEC_VERSION_40_41)
    {
    	sd_delay_ms(2);	  
    	 
        ret = sd_send_cmd_hw(sd_mmc_info,MMC_SEND_EXT_CSD, 0, RESPONSE_R1, response, mmc_ext_csd_buf, sizeof(MMC_REG_EXT_CSD_t), 1);
        if(ret)
            return ret;
            
        mmc_ext_csd_reg = (MMC_REG_EXT_CSD_t *)mmc_ext_csd_buf;
        if (mmc_ext_csd_reg->PARTITIONING_SUPPORT)
    	{
        	sd_mmc_info->emmc_boot_support = 1;
        	sd_mmc_info->emmc_boot_partition_size[0] = sd_mmc_info->emmc_boot_partition_size[1] = mmc_ext_csd_reg->BOOT_SIZE_MULTI * EMMC_BOOT_SIZE_UNIT;
        }
        ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b90100, RESPONSE_R1, response, 0, 0, 1);
        if(ret)
            return ret;

		if (using_sdxc_controller)
		{				
			unsigned long sdxc_clk = 0;
			SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
			sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
			sdxc_clk_reg->clk_ctl_en = 0;
			WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			sdxc_clk_reg->clk_div = CLK_DIV_HS_MMC; //sdxx
			sdxc_clk_reg->clk_en = 1;
			sdxc_clk_reg->clk_ctl_en = 1;
			WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
		}
		else
		{
	        sdio_config = 0;
	        config_reg = (void *)&sdio_config;
	        sdio_config = READ_CBUS_REG(SDIO_CONFIG);
	        config_reg->cmd_clk_divide = 2;
	        WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
			sd_mmc_info->sdio_clk = clk_get_rate(clk_get_sys("clk81", NULL))/2000000/(config_reg->cmd_clk_divide + 1);
			sd_mmc_info->sdio_clk_unit = 1000/sd_mmc_info->sdio_clk;	        
		}

        sd_mmc_info->speed_class = HIGH_SPEED;	
    }
    else if(sd_mmc_info->card_type == CARD_TYPE_SDIO)
    {
		ret = sdio_read_reg(sd_mmc_info, 0, High_Speed_REG, &read_reg_data);
    	if(read_reg_data & SDIO_Support_High_Speed & (!ret))
    	{
			write_reg_data = ((read_reg_data & 0xfd) | SDIO_Enable_High_Speed);
			ret = sdio_write_reg(sd_mmc_info, 0, High_Speed_REG, &write_reg_data, SDIO_Read_After_Write);
			if(!ret) 
			{
				if (using_sdxc_controller)
				{				
					unsigned long sdxc_clk = 0;
					SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
					sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
					sdxc_clk_reg->clk_ctl_en = 0;
					WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
					sdxc_clk_reg->clk_div = CLK_DIV_HS_SDIO; //sdxx
					sdxc_clk_reg->clk_en = 1;
					sdxc_clk_reg->clk_ctl_en = 1;
					WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				}
				else
				{				
		        	sdio_config = 0;
		        	config_reg = (void *)&sdio_config;
		        	sdio_config = READ_CBUS_REG(SDIO_CONFIG);
		        	config_reg->cmd_clk_divide = 3;
		        	WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
					sd_mmc_info->sdio_clk = clk_get_rate(clk_get_sys("clk81", NULL))/2000000/(config_reg->cmd_clk_divide + 1);
					sd_mmc_info->sdio_clk_unit = 1000/sd_mmc_info->sdio_clk;		        	
				}
				
	        	sd_mmc_info->speed_class = HIGH_SPEED;
        	}
        }
		else	//??? what about read High_Speed_REG error ???	//sdxx
		{
			if (using_sdxc_controller)
			{				
				unsigned long sdxc_clk = 0;
				SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
				sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
				sdxc_clk_reg->clk_ctl_en = 0;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				sdxc_clk_reg->clk_div = CLK_DIV_IDENTIFY; //sdxx
				sdxc_clk_reg->clk_en = 1;
				sdxc_clk_reg->clk_ctl_en = 1;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			}
			else
			{
				sdio_config = 0;
				config_reg = (void *)&sdio_config;
				sdio_config = READ_CBUS_REG(SDIO_CONFIG);
				config_reg->cmd_clk_divide = 5;
				WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				sd_mmc_info->sdio_clk = clk_get_rate(clk_get_sys("clk81", NULL))/2000000/(config_reg->cmd_clk_divide + 1);
				sd_mmc_info->sdio_clk_unit = 1000/sd_mmc_info->sdio_clk;
			}
		}
    }

	if(!config_reg){
		sdio_config = READ_CBUS_REG(SDIO_CONFIG);
		config_reg = (void *)&sdio_config;
	}
	
	return SD_MMC_NO_ERROR;
}

int sd_check_sdio_card_type(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int error, function_no, timeout_count = SDIO_FUNCTION_TIMEOUT;
	unsigned char read_reg_data;
	unsigned char write_reg_data;
	unsigned write_addr;

	for(function_no=1; function_no<sd_mmc_info->sdio_function_nums; function_no++)
	{
		sd_mmc_info->sdio_blk_len[function_no] = 1;
		read_reg_data = 0;
		error = sdio_read_reg(sd_mmc_info, 0, function_no<<8, &read_reg_data);
		if(error)
			return error;

		switch(read_reg_data) {

			case 0:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_NONE_SDIO;
				break;
				case 1:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_SDIO_STD_UART;
					break;
				case 2:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_SDIO_BT_TYPEA;
					break;
				case 3:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_SDIO_BT_TYPEB;
					break;
				case 4:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_SDIO_GPS;
					break;
				case 5:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_SDIO_CAMERA;
					break;
				case 6:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_SDIO_PHS;
					break;	
				case 7:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_SDIO_WLAN;
					break;
				case 8:
				case 9:
				case 10:
				case 11:
				case 12:
				case 13:
				case 14:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_NONE;
					break;
				case 15:
				sd_mmc_info->sdio_card_type[function_no] = CARD_TYPE_SDIO_OTHER_IF;
					break;
				default:
					break;
			}
	}

	error = sdio_read_reg(sd_mmc_info, 0, IO_READY_REG, &write_reg_data);
	if(error)
		return error;

	//function enable would do at up layer
	/*write_reg_data |= 2;//((1<<sd_mmc_info->sdio_function_nums)-2);
    error = sdio_write_reg(sd_mmc_info, 0, IO_ENABLE_REG, &write_reg_data, SDIO_Read_After_Write);
    if(error)
    	return error;	

	while(timeout_count--)
	{
		error = sdio_read_reg(sd_mmc_info, 0, IO_READY_REG, &read_reg_data);
		if(error)
			return error;

		if(read_reg_data == write_reg_data)
			break;
		else
			sd_delay_ms(1);
	}

#ifdef  SD_MMC_DEBUG
	Debug_Printf("#read_reg_data %x timeout_count %d \n", read_reg_data, timeout_count);
#endif*/

	if(timeout_count > 0)
	{
		error = sdio_read_reg(sd_mmc_info, 0, Card_Capability_REG, &read_reg_data);
		if(error)
			return error;

		if((read_reg_data) & SDIO_Support_Multi_Block)
		{
			if(SDIO_BLOCK_SIZE&0xFF)
			{
				write_reg_data = (SDIO_BLOCK_SIZE & 0xFF);
				error = sdio_write_reg(sd_mmc_info, 0, FN0_Block_Size_Low_REG, &write_reg_data, SDIO_Read_After_Write);
				if(error)
				{
					sd_mmc_info->sdio_blk_len[0] = 1;
					return SD_MMC_NO_ERROR;
				}

				write_addr = ((2 << 8) | 0x10);
				error = sdio_write_reg(sd_mmc_info, 0, write_addr, &write_reg_data, SDIO_Read_After_Write);
				if(error)
				{
					sd_mmc_info->sdio_blk_len[2] = 1;
					return SD_MMC_NO_ERROR;
				}
			}

			write_reg_data = (SDIO_BLOCK_SIZE>>8);
			error = sdio_write_reg(sd_mmc_info, 0, FN0_Block_Size_High_REG, &write_reg_data, SDIO_Read_After_Write);
			if(error)
			{
				sd_mmc_info->sdio_blk_len[0] = 1;
			}
			else
			{
				sd_mmc_info->sdio_blk_len[0] = SDIO_BLOCK_SIZE;
			}

			write_addr = ((2 << 8) | 0x11);
			error = sdio_write_reg(sd_mmc_info, 0, write_addr, &write_reg_data, SDIO_Read_After_Write);
			if(error)
			{
				sd_mmc_info->sdio_blk_len[1] = 1;
			}
			else
			{
				sd_mmc_info->sdio_blk_len[2] = SDIO_BLOCK_SIZE;
			}
		}
		else
		{
			return SD_SDIO_ERROR_NO_FUNCTION;
		}
	}
	else
	{
		return SD_SDIO_ERROR_NO_FUNCTION;
	}

	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sdio_open_interrupt()()\n", sd_error_to_string(error));
#endif
		//return error;
	}

	return SD_MMC_NO_ERROR;
}

int sdio_read_reg(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, unsigned long sdio_register, unsigned char *reg_data)
{
	int ret = 0;
	unsigned long sdio_direct_rw = 0;
	unsigned char response[MAX_RESPONSE_BYTES];
	SDIO_IO_RW_CMD_ARG_t *sdio_io_direct_rw;
	SDIO_RW_CMD_Response_R5_t * sdio_rw_response = (SDIO_RW_CMD_Response_R5_t *)response;

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if (sd_mmc_info->sd_save_hw_io_flag) {
    		WRITE_CBUS_REG(SDIO_CONFIG, sd_mmc_info->sd_save_hw_io_config);
      		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sd_mmc_info->sd_save_hw_io_mult_config);
      	}	
		if (sd_mmc_info->sdxc_save_hw_io_flag) {
			WRITE_CBUS_REG(SD_REG2_CNTL, sd_mmc_info->sdxc_save_hw_io_ctrl);
			WRITE_CBUS_REG(SD_REG4_CLKC, 0);
			WRITE_CBUS_REG(SD_REG4_CLKC, sd_mmc_info->sdxc_save_hw_io_clk);
		}
	}
#endif       
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		sd_mmc_io_config(sd_mmc_info);
#endif

	sdio_io_direct_rw = (void *)&sdio_direct_rw;

	sdio_io_direct_rw->Function_No = function_no;
	sdio_io_direct_rw->Register_Address = (sdio_register & 0x1FFFF);

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		ret = sd_send_cmd_hw(sd_mmc_info, IO_RW_DIRECT, sdio_direct_rw, RESPONSE_R5, response, 0, 0, 1);
#endif	
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		ret = sd_send_cmd_sw(sd_mmc_info, IO_RW_DIRECT, sdio_direct_rw, RESPONSE_R5, response);
#endif
	if(ret)
		return ret;

	*reg_data = sdio_rw_response->read_or_write_data;

	return SD_MMC_NO_ERROR;
}

int sdio_write_reg(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, unsigned int sdio_register, unsigned char *reg_data, unsigned read_after_write_flag)
{
	int ret = 0;
	unsigned long sdio_direct_rw = 0;
	SDIO_IO_RW_CMD_ARG_t *sdio_io_direct_rw;
	unsigned char response[MAX_RESPONSE_BYTES];
	SDIO_RW_CMD_Response_R5_t * sdio_rw_response = (SDIO_RW_CMD_Response_R5_t *)response;

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if (sd_mmc_info->sd_save_hw_io_flag) {
    		WRITE_CBUS_REG(SDIO_CONFIG, sd_mmc_info->sd_save_hw_io_config);
      		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sd_mmc_info->sd_save_hw_io_mult_config);
      	}		
		if (sd_mmc_info->sdxc_save_hw_io_flag) {
			WRITE_CBUS_REG(SD_REG2_CNTL, sd_mmc_info->sdxc_save_hw_io_ctrl);
			WRITE_CBUS_REG(SD_REG4_CLKC, 0);
			WRITE_CBUS_REG(SD_REG4_CLKC, sd_mmc_info->sdxc_save_hw_io_clk);
		}
	}
#endif       
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		sd_mmc_io_config(sd_mmc_info);
#endif

	sdio_io_direct_rw = (void *)&sdio_direct_rw;

    sdio_io_direct_rw->R_W_Flag = SDIO_Write_Data;
    sdio_io_direct_rw->RAW_Flag = read_after_write_flag;
	sdio_io_direct_rw->Function_No = function_no;
	sdio_io_direct_rw->write_data_bytes = (*reg_data);
	sdio_io_direct_rw->Register_Address = (sdio_register & 0x1FFFF);

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
		ret = sd_send_cmd_hw(sd_mmc_info, IO_RW_DIRECT, sdio_direct_rw, RESPONSE_R5, response, 0, 0, 0);
#endif	
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		ret = sd_send_cmd_sw(sd_mmc_info, IO_RW_DIRECT, sdio_direct_rw, RESPONSE_R5, response);
#endif

#ifdef  SD_MMC_DEBUG
	//Debug_Printf("#sdio_write_reg write: %x addr: %x read: %x function_no %d\n", (*reg_data), sdio_register, sdio_rw_response->read_or_write_data, function_no);
#endif

	if(ret)
		return ret;

	if(read_after_write_flag && (sdio_rw_response->read_or_write_data != (*reg_data))) {
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#this sdio card could not support read after write\n");
#endif
		return SD_MMC_NO_ERROR;
	}

	return SD_MMC_NO_ERROR;
}

int sdio_card_reset(SD_MMC_Card_Info_t *sd_mmc_info)
{
	int ret;
	unsigned char write_reg_data;

	write_reg_data = SDIO_RES_bit;
    ret = sdio_write_reg(sd_mmc_info, 0, IO_ABORT_REG, &write_reg_data, SDIO_DONT_Read_After_Write);
    if(ret)
    	return ret;

	return SD_MMC_NO_ERROR;
}

int sdio_data_transfer_abort(SD_MMC_Card_Info_t *sd_mmc_info, int function_no)
{
	int ret;
	unsigned char write_reg_data;

	write_reg_data = function_no;
    ret = sdio_write_reg(sd_mmc_info, 0, IO_ABORT_REG, &write_reg_data, SDIO_DONT_Read_After_Write);
    if(ret)
    	return ret;

	return SD_MMC_NO_ERROR;
}

int sdio_read_data_block_hw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long block_count, unsigned char *data_buf)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned long read_block_count, data_offset = 0;

	unsigned long sdio_extend_rw = 0;
	SDIO_IO_RW_EXTENDED_ARG *sdio_io_extend_rw = (void *)&sdio_extend_rw;

	while(block_count)
	{
		if(block_count > sd_mmc_info->max_blk_count)
			read_block_count = sd_mmc_info->max_blk_count;
		else
			read_block_count = block_count;

		sdio_io_extend_rw->R_W_Flag = SDIO_Read_Data;
		sdio_io_extend_rw->Block_Mode = SDIO_Block_MODE;
		sdio_io_extend_rw->OP_Code = buf_or_fifo;
		sdio_io_extend_rw->Function_No = function_no;
		sdio_io_extend_rw->Byte_Block_Count = read_block_count;
		sdio_io_extend_rw->Register_Address = ((sdio_addr+data_offset) & 0x1FFFF);

		ret = sd_send_cmd_hw(sd_mmc_info, IO_RW_EXTENDED, sdio_extend_rw, RESPONSE_R5, response, data_buf+data_offset, read_block_count*sd_mmc_info->blk_len, 0);
		if(ret)
			return ret;

		data_offset += read_block_count*sd_mmc_info->blk_len;
		block_count -= read_block_count;
	}

	return SD_MMC_NO_ERROR;
}

int sdio_read_data_block_sw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long block_count, unsigned char *data_buf)
{
	unsigned long data = 0, res = 0, temp = 0;
	int ret, data_busy = 1, res_busy = 1;
	
	unsigned long res_cnt = 0, data_cnt = 0, num_nac = 0, num_ncr = 0;
	
	unsigned short crc16_array[4] = {0, 0, 0, 0};
	unsigned short crc16 = 0;
	
	unsigned long loop_num,blk_cnt;
	
	int i,j;

#ifdef SD_MMC_CRC_CHECK
	unsigned short crc_check = 0, crc_check_array[4]={0,0,0,0};
	int error=0;
	unsigned char *org_buf=data_buf;
#endif

	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned long read_block_count, data_offset = 0;

	unsigned long sdio_extend_rw = 0;
	SDIO_IO_RW_EXTENDED_ARG *sdio_io_extend_rw = (void *)&sdio_extend_rw;

	while(block_count)
	{
		if(block_count > sd_mmc_info->max_blk_count)
			read_block_count = sd_mmc_info->max_blk_count;
		else
			read_block_count = block_count;

		sdio_io_extend_rw->R_W_Flag = SDIO_Read_Data;
		sdio_io_extend_rw->Block_Mode = SDIO_Block_MODE;
		sdio_io_extend_rw->OP_Code = buf_or_fifo;
		sdio_io_extend_rw->Function_No = function_no;
		sdio_io_extend_rw->Byte_Block_Count = read_block_count;
		sdio_io_extend_rw->Register_Address = ((sdio_addr+data_offset) & 0x1FFFF);

		ret = sd_send_cmd_sw(sd_mmc_info, IO_RW_EXTENDED, sdio_extend_rw, RESPONSE_NONE, NULL);
		if(ret)
			return ret;

		if(sd_mmc_info->bus_width == SD_BUS_WIDE)
		{
			sd_set_dat0_3_input();
		}
		else
		{
			sd_set_dat0_input();
		}

		sd_clear_response(response);
		sd_delay_clocks_z(sd_mmc_info, SD_MMC_Z_CMD_TO_RES);
	
		sd_set_cmd_input();
		//wait until both response and data is valid    
		do
		{
			sd_clk_transfer_low();
		
			res = sd_get_cmd_value();
			data = sd_get_dat0_value();
		
			if (res_busy)
			{
				if (res)
					num_ncr++;
				else
					res_busy = 0;
			}
			else
			{
				if (res_cnt < (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8))
				{
					response[res_cnt>>3] <<= 1;
					response[res_cnt>>3] |= res;
				
					res_cnt++;
				}
			}
		
			if (data_busy)
			{
				if (data)
					num_nac++;
				else
					data_busy = 0;
			}
			else
			{
				if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
				{
					data = sd_get_dat0_3_value();
					temp <<= 4;
					temp |= data;
#ifdef SD_MMC_CRC_CHECK
					SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
					SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
					SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
					SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
					if((data_cnt & 0x01) == 1)
					{
#ifdef AMLOGIC_CHIP_SUPPORT
						if((unsigned long)data_buf == 0x3400000)
						{
							WRITE_BYTE_TO_FIFO(temp);
						}
						else
#endif
						{
							*data_buf = temp;
							data_buf++;
						}

						temp = 0;   //one byte received, clear temp varialbe
					}                   
				}
				else                //only data0 lines
				{
					data = sd_get_dat0_value();
					temp <<= 1;
					temp |= data;
					if((data_cnt & 0x07) == 7)
					{
#ifdef AMLOGIC_CHIP_SUPPORT
						if((unsigned)data_buf == 0x3400000)
						{
							WRITE_BYTE_TO_FIFO(temp);
						}
						else
#endif
						{
							*data_buf = temp;
							data_buf++;
						}

#ifdef SD_MMC_CRC_CHECK
						crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
						temp = 0;   //one byte received, clear temp varialbe
					}
				}
				data_cnt++;
			}
			
			sd_clk_transfer_high();
		
			if(!res_busy && !data_busy)
			{
				if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
				{
					if((res_cnt >= (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8)) && ((data_cnt&0x01) == 0))
					{
						data_cnt >>= 1;
						break;
					}
				}
				else
				{
					if((res_cnt >= (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8)) && ((data_cnt&0x07) == 0))
					{
						data_cnt >>= 3;
						break;
					}
				}
			}

		}while((num_ncr < SD_MMC_TIME_NCR_MAX) && (num_nac < sd_mmc_info->clks_nac));
	
		if((num_ncr >= SD_MMC_TIME_NCR_MAX) || (num_nac >= sd_mmc_info->clks_nac))
			return SD_MMC_ERROR_TIMEOUT;

		//Read all data blocks
		loop_num = sd_mmc_info->blk_len;
		for (blk_cnt = 0; blk_cnt < block_count; blk_cnt++)
		{
		//wait until data is valid
			num_nac = 0;    
			do
			{   
				if(!data_busy)
					break;
				
				sd_clk_transfer_low();
		
				data = sd_get_dat0_value();
		
				if(data)
				{
					num_nac++;
				}
				else
				{
					data_busy = 0;
				}
		
				sd_clk_transfer_high();

			}while(data_busy && (num_nac < sd_mmc_info->clks_nac));
		
			if(num_nac >= sd_mmc_info->clks_nac)
				return SD_MMC_ERROR_TIMEOUT;
		
		//Read data
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
			{
#ifdef AMLOGIC_CHIP_SUPPORT
				if((unsigned long)data_buf == 0x3400000)
				{
					for(; data_cnt < loop_num; data_cnt++)
					{
						temp = 0;   //clear temp varialbe
				
						for(i = 0; i < 2; i++)
						{
							sd_clk_transfer_low();
		
							data = sd_get_dat0_3_value();
							temp <<= 4;
							temp |= data;
					
							sd_clk_transfer_high();
						
#ifdef SD_MMC_CRC_CHECK
							SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
							SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
							SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
							SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
						}
					
						WRITE_BYTE_TO_FIFO(temp);
					}
				}
				else
#endif
				{
					for(; data_cnt < loop_num; data_cnt++)
					{
						temp = 0;   //clear temp varialbe
				
						for(i = 0; i < 2; i++)
						{
							sd_clk_transfer_low();
						
							data = sd_get_dat0_3_value();
							temp <<= 4;
							temp |= data;
					
							sd_clk_transfer_high();
						
#ifdef SD_MMC_CRC_CHECK
							SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
							SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
							SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
							SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
						}
					
						*data_buf = temp;
						data_buf++;
					}
				}
			
				//Read CRC16 data
				for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
				{
					sd_clk_transfer_low();
		
					crc16_array[0] <<= 1;
					crc16_array[1] <<= 1;
					crc16_array[2] <<= 1;
					crc16_array[3] <<= 1;
					data = sd_get_dat0_3_value();
					crc16_array[0] |= (data & 0x01);
					crc16_array[1] |= ((data >> 1) & 0x01);
					crc16_array[2] |= ((data >> 2) & 0x01);
					crc16_array[3] |= ((data >> 3) & 0x01);
			
					sd_clk_transfer_high();
				}
			
#ifdef SD_MMC_CRC_CHECK
				for(i=0; i<4; i++)
				{
					//crc_check_array[i] = sd_cal_crc_mode(org_buf, sd_mmc_info->blk_len, char_mode[i]);
					if(crc16_array[i] != crc_check_array[i])
					{
						error = SD_MMC_ERROR_DATA_CRC;
						break;
					}
				}
#endif
			}
			else    //only data0 lines
			{
#ifdef AMLOGIC_CHIP_SUPPORT
				if((unsigned long)data_buf == 0x3400000)
				{
					for(; data_cnt < loop_num; data_cnt++)
					{
						temp = 0;   //clear temp varialbe
				
						for(j = 0; j < 8; j++)
						{
							sd_clk_transfer_low();
					
							data = sd_get_dat0_value();
							temp <<= 1;
							temp |= data;
					
							sd_clk_transfer_high();
						}
					
						WRITE_BYTE_TO_FIFO(temp);
					
#ifdef SD_MMC_CRC_CHECK
						crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
					}
				}
				else
#endif
				{
					for(; data_cnt < loop_num; data_cnt++)
					{
						temp = 0;   //clear temp varialbe
				
						for(j = 0; j < 8; j++)
						{
							sd_clk_transfer_low();
					
							data = sd_get_dat0_value();
							temp <<= 1;
							temp |= data;
					
							sd_clk_transfer_high();
						}
					
						*data_buf = temp;
						data_buf++;
					
#ifdef SD_MMC_CRC_CHECK
						crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
					}
				}

				//Read CRC16 data
				for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
				{
					sd_clk_transfer_low();
		
					data = sd_get_dat0_value();
					crc16 <<= 1;
					crc16 |= data;      
			
					sd_clk_transfer_high();
				}
			
#ifdef SD_MMC_CRC_CHECK
				if(crc16 != crc_check)
					error = SD_MMC_ERROR_DATA_CRC;
#endif
			}
		
			sd_clk_transfer_low();      //for end bit
			sd_clk_transfer_high();
		
			data_busy = 1;
			data_cnt = 0;
		
#ifdef SD_MMC_CRC_CHECK
			org_buf = data_buf;
			crc_check = 0;
			crc_check_array[0] = crc_check_array[1] = crc_check_array[2] = crc_check_array[3] = 0;
#endif
		}

		sd_delay_clocks_z(sd_mmc_info, SD_MMC_TIME_NRC_NCC);     //Clock delay, Z type
	
		data_offset += read_block_count*sd_mmc_info->blk_len;
		block_count -= read_block_count;
	}

	return SD_MMC_NO_ERROR;
}

unsigned char sdio_4bytes_buf[PAGE_SIZE];
int sdio_read_data_byte_hw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long byte_count, unsigned char *data_buf)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned long read_byte_count, four_byte_count, data_offset = 0;

	unsigned long sdio_extend_rw = 0;
	SDIO_IO_RW_EXTENDED_ARG *sdio_io_extend_rw = (void *)&sdio_extend_rw;

	while(byte_count)
	{
		if(byte_count > 512)
			read_byte_count = 512;
		else
			read_byte_count = byte_count;

		sdio_io_extend_rw->R_W_Flag = SDIO_Read_Data;
		sdio_io_extend_rw->Block_Mode = SDIO_Byte_MODE;
		sdio_io_extend_rw->OP_Code = buf_or_fifo;
		sdio_io_extend_rw->Function_No = function_no;
		sdio_io_extend_rw->Byte_Block_Count = read_byte_count;
		sdio_io_extend_rw->Register_Address = ((sdio_addr+data_offset) & 0x1FFFF);

		if(read_byte_count%4)
			sd_mmc_info->sdio_read_crc_close = 1;
		else
			sd_mmc_info->sdio_read_crc_close = 0;

		four_byte_count = (read_byte_count + 3) & (~0x3);
		
		ret = sd_send_cmd_hw(sd_mmc_info, IO_RW_EXTENDED, sdio_extend_rw, RESPONSE_R5, response, sdio_4bytes_buf, four_byte_count, 0);
		if(ret)
			return ret;
		memcpy(data_buf + data_offset, sdio_4bytes_buf, read_byte_count);

		data_offset += read_byte_count;
		byte_count -= read_byte_count;
	}

	sd_mmc_info->sdio_read_crc_close = 0;
	return SD_MMC_NO_ERROR;
}

int sdio_read_data_byte_sw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long byte_count, unsigned char *data_buf)
{
	unsigned long data = 0, res = 0, temp = 0;
	int ret, data_busy = 1, res_busy = 1;
	
	unsigned long res_cnt = 0, data_cnt = 0, num_nac = 0, num_ncr = 0, crc_data_cnt = 0;
	
	unsigned short crc16_array[4] = {0, 0, 0, 0};
	unsigned short crc16 = 0;
	
	unsigned long loop_num;
	
	int i,j;

#ifdef SD_MMC_CRC_CHECK
	unsigned short crc_check = 0, crc_check_array[4]={0,0,0,0};
	int error=0;
	//unsigned char *org_buf=data_buf;
#endif

	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned long read_byte_count, data_offset = 0;

	unsigned long sdio_extend_rw = 0;
	SDIO_IO_RW_EXTENDED_ARG *sdio_io_extend_rw = (void *)&sdio_extend_rw;

	while(byte_count)
	{
		if(byte_count > 512)
			read_byte_count = 512;
		else
			read_byte_count = byte_count;

		sdio_io_extend_rw->R_W_Flag = SDIO_Read_Data;
		sdio_io_extend_rw->Block_Mode = SDIO_Byte_MODE;
		sdio_io_extend_rw->OP_Code = buf_or_fifo;
		sdio_io_extend_rw->Function_No = function_no;
		sdio_io_extend_rw->Byte_Block_Count = read_byte_count;
		sdio_io_extend_rw->Register_Address = ((sdio_addr+data_offset) & 0x1FFFF);

		ret = sd_send_cmd_sw(sd_mmc_info, IO_RW_EXTENDED, sdio_extend_rw, RESPONSE_NONE, NULL);
		if(ret)
			return ret;

		if(sd_mmc_info->bus_width == SD_BUS_WIDE)
		{
			sd_set_dat0_3_input();
		}
		else
		{
			sd_set_dat0_input();
		}

		sd_clear_response(response);
		sd_delay_clocks_z(sd_mmc_info, SD_MMC_Z_CMD_TO_RES);
	
		sd_set_cmd_input();
		//wait until both response and data is valid    
		do
		{
			sd_clk_transfer_low();
		
			res = sd_get_cmd_value();
			data = sd_get_dat0_value();
		
			if(res_busy)
			{
				if (res)
					num_ncr++;
				else
					res_busy = 0;
			}
			else
			{
				if (res_cnt < (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8))
				{
					response[res_cnt>>3] <<= 1;
					response[res_cnt>>3] |= res;
				
					res_cnt++;
				}
			}
		
			if (data_busy)
			{
				if (data)
					num_nac++;
				else
					data_busy = 0;
			}
			else
			{
				if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
				{
					if(data_cnt < read_byte_count*2)
					{
					data = sd_get_dat0_3_value();
					temp <<= 4;
					temp |= data;
#ifdef SD_MMC_CRC_CHECK
					SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
					SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
					SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
					SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
					if((data_cnt & 0x01) == 1)
					{
#ifdef AMLOGIC_CHIP_SUPPORT
						if((unsigned long)data_buf == 0x3400000)
						{
							WRITE_BYTE_TO_FIFO(temp);
						}
						else
#endif
						{
							*data_buf = temp;
							data_buf++;
						}

						temp = 0;   //one byte received, clear temp varialbe
					}                   
						data_cnt++;
					}
					else if(crc_data_cnt < 16)
					{
						//Read CRC16 data
						crc16_array[0] <<= 1;
						crc16_array[1] <<= 1;
						crc16_array[2] <<= 1;
						crc16_array[3] <<= 1;
						data = sd_get_dat0_3_value();
						crc16_array[0] |= (data & 0x01);
						crc16_array[1] |= ((data >> 1) & 0x01);
						crc16_array[2] |= ((data >> 2) & 0x01);
						crc16_array[3] |= ((data >> 3) & 0x01);

						crc_data_cnt++;
					}                
				}
				else                //only data0 lines
				{
					if(data_cnt < read_byte_count*8)
					{
					data = sd_get_dat0_value();
					temp <<= 1;
					temp |= data;
					if((data_cnt & 0x07) == 7)
					{
#ifdef AMLOGIC_CHIP_SUPPORT
						if((unsigned)data_buf == 0x3400000)
						{
							WRITE_BYTE_TO_FIFO(temp);
						}
						else
#endif
						{
							*data_buf = temp;
							data_buf++;
						}

#ifdef SD_MMC_CRC_CHECK
						crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
						temp = 0;   //one byte received, clear temp varialbe
					}
				}
				data_cnt++;
			}
		
			}
		
			sd_clk_transfer_high();
		
			if(!res_busy && !data_busy)
			{
				if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
				{
					if((res_cnt >= (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8)) && ((data_cnt&0x01) == 0))
					{
						data_cnt >>= 1;
						break;
					}
				}
				else
				{
					if((res_cnt >= (RESPONSE_R1_R3_R4_R5_R6_R7_LENGTH*8)) && ((data_cnt&0x07) == 0))
					{
						data_cnt >>= 3;
						break;
					}
				}
			}

		}while((num_ncr < SD_MMC_TIME_NCR_MAX) && (num_nac < sd_mmc_info->clks_nac));
	
		if((num_ncr >= SD_MMC_TIME_NCR_MAX) || (num_nac >= sd_mmc_info->clks_nac))
			return SD_MMC_ERROR_TIMEOUT;

		//Read data and response
		loop_num = read_byte_count;
		if(sd_mmc_info->bus_width == SD_BUS_WIDE)          //4 data lines
		{
#ifdef AMLOGIC_CHIP_SUPPORT
			if((unsigned long)data_buf == 0x3400000)
			{
				for(; data_cnt < loop_num; data_cnt++)
				{
					temp = 0;   //clear temp varialbe

					for(i = 0; i < 2; i++)
					{
						sd_clk_transfer_low();

						data = sd_get_dat0_3_value();
						temp <<= 4;
						temp |= data;

						sd_clk_transfer_high();

#ifdef SD_MMC_CRC_CHECK
						SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
						SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
						SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
						SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
					}

					WRITE_BYTE_TO_FIFO(temp);
				}
			}
			else
#endif
			{
				for(; data_cnt < loop_num; data_cnt++)
				{
					temp = 0;   //clear temp varialbe

					for(i = 0; i < 2; i++)
					{
						sd_clk_transfer_low();

						data = sd_get_dat0_3_value();
						temp <<= 4;
						temp |= data;

						sd_clk_transfer_high();
					
#ifdef SD_MMC_CRC_CHECK
						SD_CAL_BIT_CRC(crc_check_array[0],data&0x01);
						SD_CAL_BIT_CRC(crc_check_array[1],data&0x02);
						SD_CAL_BIT_CRC(crc_check_array[2],data&0x04);
						SD_CAL_BIT_CRC(crc_check_array[3],data&0x08);
#endif
					}
				
					*data_buf = temp;
					data_buf++;
				}
			}
		
			//Read CRC16 data
			for(; crc_data_cnt < 16; crc_data_cnt++)    // 16 bits CRC
			{
				sd_clk_transfer_low();

				crc16_array[0] <<= 1;
				crc16_array[1] <<= 1;
				crc16_array[2] <<= 1;
				crc16_array[3] <<= 1;
				data = sd_get_dat0_3_value();
				crc16_array[0] |= (data & 0x01);
				crc16_array[1] |= ((data >> 1) & 0x01);
				crc16_array[2] |= ((data >> 2) & 0x01);
				crc16_array[3] |= ((data >> 3) & 0x01);

				sd_clk_transfer_high();
			}
		
#ifdef SD_MMC_CRC_CHECK
			for(i=0; i<4; i++)
			{
				//crc_check_array[i] = sd_cal_crc_mode(org_buf, sd_mmc_info->blk_len, char_mode[i]);
				if(crc16_array[i] != crc_check_array[i])
				{
					error = SD_MMC_ERROR_DATA_CRC;
					break;
				}
			}
#endif
		}
		else    //only data0 lines
		{
#ifdef AMLOGIC_CHIP_SUPPORT
			if((unsigned)data_buf == 0x3400000)
			{
				for(; data_cnt < loop_num; data_cnt++)
				{
					temp = 0;   //clear temp varialbe

					for(j = 0; j < 8; j++)
					{
						sd_clk_transfer_low();
				
						data = sd_get_dat0_value();
						temp <<= 1;
						temp |= data;

						sd_clk_transfer_high();
					}

					WRITE_BYTE_TO_FIFO(temp);
				
#ifdef SD_MMC_CRC_CHECK
					crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
				}
			}
			else
#endif
			{
				for(; data_cnt < loop_num; data_cnt++)
				{
					temp = 0;   //clear temp varialbe

					for(j = 0; j < 8; j++)
					{
						sd_clk_transfer_low();
				
						data = sd_get_dat0_value();
						temp <<= 1;
						temp |= data;

						sd_clk_transfer_high();
					}

					*data_buf = temp;
					data_buf++;
				
#ifdef SD_MMC_CRC_CHECK
					crc_check = (crc_check << 8) ^ sd_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
				}
			}

			//Read CRC16 data
			for(data_cnt = 0; data_cnt < 16; data_cnt++)    // 16 bits CRC
			{
				sd_clk_transfer_low();

				data = sd_get_dat0_value();
				crc16 <<= 1;
				crc16 |= data;

				sd_clk_transfer_high();
			}
		
#ifdef SD_MMC_CRC_CHECK
			if(crc16 != crc_check)
				error = SD_MMC_ERROR_DATA_CRC;
#endif
		}

		sd_clk_transfer_low();      //for end bit
		sd_clk_transfer_high();
	
		sd_delay_clocks_z(sd_mmc_info, SD_MMC_TIME_NRC_NCC);     //Clock delay, Z type

		data_offset += read_byte_count;
		byte_count -= read_byte_count;
	}

	return SD_MMC_NO_ERROR;
}

int sdio_read_data(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long byte_count, unsigned char *data_buf)
{
	int error = 0, ret;
	unsigned long block_nums, byte_nums;
	BUG_ON(sd_mmc_info->sdio_blk_len[function_no] == 0);

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if (sd_mmc_info->sd_save_hw_io_flag) {
    		WRITE_CBUS_REG(SDIO_CONFIG, sd_mmc_info->sd_save_hw_io_config);
      		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sd_mmc_info->sd_save_hw_io_mult_config);
    	}		
		if (sd_mmc_info->sdxc_save_hw_io_flag) {
			WRITE_CBUS_REG(SD_REG2_CNTL, sd_mmc_info->sdxc_save_hw_io_ctrl);
			WRITE_CBUS_REG(SD_REG4_CLKC, 0);
			WRITE_CBUS_REG(SD_REG4_CLKC, sd_mmc_info->sdxc_save_hw_io_clk);
		}
	}
#endif       
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		sd_mmc_io_config(sd_mmc_info);
#endif
	
	sd_mmc_info->blk_len = sd_mmc_info->sdio_blk_len[function_no];
	byte_nums = byte_count % sd_mmc_info->blk_len;
	block_nums = byte_count / sd_mmc_info->blk_len;

	if(block_nums == 0)
	{
		if(byte_nums)
		{
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
				error = sdio_read_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_nums, data_buf);
#endif
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
				error = sdio_read_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_nums, data_buf);
#endif
			if(error == SD_MMC_ERROR_TIMEOUT)
			{
				ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
				if(ret)
					return ret;

#ifdef SD_MMC_HW_CONTROL
				if(SD_WORK_MODE == CARD_HW_MODE)
					error = sdio_read_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_nums, data_buf);
#endif
#ifdef SD_MMC_SW_CONTROL
				if(SD_WORK_MODE == CARD_SW_MODE)
					error = sdio_read_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_nums, data_buf);
#endif
				if(error == SD_MMC_ERROR_TIMEOUT)
					return error;
			}
		}
		else
		{
			error = SD_MMC_ERROR_BLOCK_LEN;
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sdio_read_data()\n", sd_error_to_string(error));
#endif
			return error;
		}

		if(error)
		{			
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sdio_read_byte()\n", sd_error_to_string(error));
#endif
			ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
			if(ret)
				return ret;

			return error;
		}
	}
	else
	{
		if(sd_mmc_info->blk_len == sd_mmc_info->sdio_blk_len[function_no])
		{
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
			{
				error = sdio_read_data_block_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums, data_buf);
				if(error == SD_MMC_ERROR_TIMEOUT)
				{
					ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
					if(ret)
						return ret;

					error = sdio_read_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums*sd_mmc_info->blk_len, data_buf);
				}

				if(byte_nums)
				{
					error = sdio_read_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr + block_nums*sd_mmc_info->blk_len, byte_nums, data_buf + block_nums*sd_mmc_info->blk_len);
				}
			}
#endif
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
			{
				error = sdio_read_data_block_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums, data_buf);
				if(error == SD_MMC_ERROR_TIMEOUT)
				{
					ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
					if(ret)
						return ret;

					error = sdio_read_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums*sd_mmc_info->blk_len, data_buf);
				}

				if(byte_nums)
				{
					error = sdio_read_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr + block_nums*sd_mmc_info->blk_len, byte_nums, data_buf + block_nums*sd_mmc_info->blk_len);
				}
			}
#endif
		}
		else
		{
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
				error = sdio_read_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_count, data_buf);
#endif
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
				error = sdio_read_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_count, data_buf);
#endif
		}

		if(error)
		{			
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sdio_read_block()\n", sd_error_to_string(error));
#endif
			ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
			if(ret)
				return ret;

			return error;
		}
	}

	return SD_MMC_NO_ERROR;	
}

int sdio_write_data_block_hw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long block_count, unsigned char *data_buf)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned long write_block_count, data_offset = 0;

	unsigned long sdio_extend_rw = 0;
	SDIO_IO_RW_EXTENDED_ARG *sdio_io_extend_rw = (void *)&sdio_extend_rw;

	while(block_count)
	{
		if(block_count > sd_mmc_info->max_blk_count)
			write_block_count = sd_mmc_info->max_blk_count;
		else
			write_block_count = block_count;

		sdio_io_extend_rw->R_W_Flag = SDIO_Write_Data;
		sdio_io_extend_rw->Block_Mode = SDIO_Block_MODE;
		sdio_io_extend_rw->OP_Code = buf_or_fifo;
		sdio_io_extend_rw->Function_No = function_no;
		sdio_io_extend_rw->Byte_Block_Count = write_block_count;
		sdio_io_extend_rw->Register_Address = ((sdio_addr+data_offset) & 0x1FFFF);

		ret = sd_send_cmd_hw(sd_mmc_info, IO_RW_EXTENDED, sdio_extend_rw, RESPONSE_R5, response, data_buf+data_offset, write_block_count*sd_mmc_info->blk_len, 0);
		if(ret)
			return ret;

		data_offset += write_block_count*sd_mmc_info->blk_len;
		block_count -= write_block_count;
	}

	return SD_MMC_NO_ERROR;
}

int sdio_write_data_block_sw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long block_count, unsigned char *data_buf)
{
	unsigned long write_block_count, data_offset = 0;

	int ret,i,j;
	unsigned long crc_status, data;
	
	unsigned long data_cnt = 0;
	unsigned timeout;
	
	unsigned char * org_buf = data_buf;
	unsigned short crc16_array[4] = {0, 0, 0, 0};
	unsigned short crc16;
	unsigned char char_mode[4][3] = {{0x10, 0x01, 0},
					{0x20, 0x02, 0},
					{0x40, 0x04, 0},
					{0x80, 0x08, 0}};
	
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned long loop_num,blk_cnt;

	unsigned long sdio_extend_rw = 0;
	SDIO_IO_RW_EXTENDED_ARG *sdio_io_extend_rw = (void *)&sdio_extend_rw;

	while(block_count)
	{
		if(block_count > sd_mmc_info->max_blk_count)
			write_block_count = sd_mmc_info->max_blk_count;
		else
			write_block_count = block_count;

		sdio_io_extend_rw->R_W_Flag = SDIO_Write_Data;
		sdio_io_extend_rw->Block_Mode = SDIO_Block_MODE;
		sdio_io_extend_rw->OP_Code = buf_or_fifo;
		sdio_io_extend_rw->Function_No = function_no;
		sdio_io_extend_rw->Byte_Block_Count = write_block_count;
		sdio_io_extend_rw->Register_Address = ((sdio_addr+data_offset) & 0x1FFFF);

		ret = sd_send_cmd_sw(sd_mmc_info, IO_RW_EXTENDED, sdio_extend_rw, RESPONSE_R5, response);
		if(ret)
			return ret;

		loop_num = sd_mmc_info->blk_len;
		for(blk_cnt = 0; blk_cnt < write_block_count; blk_cnt++)
		{
			org_buf = data_buf;

			//Nwr cycles delay
			sd_delay_clocks_h(sd_mmc_info, SD_MMC_TIME_NWR);

			//Start bit
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)
			{
				sd_set_dat0_3_output();

				sd_clk_transfer_low();

				sd_set_dat0_3_value(0x00);

				sd_clk_transfer_high();
			}
			else
			{
				sd_set_dat0_output();

				sd_clk_transfer_low();

				sd_set_dat0_value(0x00);

				sd_clk_transfer_high();
			}

			//Write data
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)
			{
				for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
				{
					for(i=1; i>=0; i--)
					{
						sd_clk_transfer_low();

						data = (*data_buf >> (i<<2)) & 0x0F;
						sd_set_dat0_3_value(data);

						sd_clk_transfer_high();
					}
				
					data_buf++;
				}

				//Caculate CRC16 value and write to line
				for(i=0; i<4; i++)
				{
					crc16_array[i] = sd_cal_crc_mode(org_buf, sd_mmc_info->blk_len, char_mode[i]);
				}

				//Write CRC16
				for(i=15; i>=0; i--)
				{
					sd_clk_transfer_low();

					data = 0;
					for(j=3; j>=0; j--)
					{
						data <<= 1; 
						data |= (crc16_array[j] >> i) & 0x0001;
					}
					sd_set_dat0_3_value(data);

					sd_clk_transfer_high();
				}
			}
			else    // only dat0 line
			{
				for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
				{
					for(i=7; i>=0; i--)
					{
						sd_clk_transfer_low();

						data = (*data_buf >> i) & 0x01;
						sd_set_dat0_value(data);

						sd_clk_transfer_high();
					}

					data_buf++;
				}
			
				//Caculate CRC16 value and write to line
				crc16 = sd_cal_crc16(org_buf, sd_mmc_info->blk_len);

				//Write CRC16
				for(i=15; i>=0; i--)
				{
					sd_clk_transfer_low();

					data = (crc16 >> i) & 0x0001;
					sd_set_dat0_value(data);

					sd_clk_transfer_high();
				}
			}

			//End bit
			if(sd_mmc_info->bus_width == SD_BUS_WIDE)
			{
				sd_clk_transfer_low();

				sd_set_dat0_3_value(0x0F);

				sd_clk_transfer_high();

				sd_set_dat0_3_input();
			}
			else
			{
				sd_clk_transfer_low();

				sd_set_dat0_value(0x01);

				sd_clk_transfer_high();

				sd_set_dat0_input();
			}

			sd_delay_clocks_h(sd_mmc_info, SD_MMC_Z_CMD_TO_RES);

			crc_status = 0;
	
			//Check CRC status
			sd_set_dat0_input();
			for(i = 0; i < 5; i++)
			{
				sd_clk_transfer_low();

				data = sd_get_dat0_value();
				crc_status <<= 1;
				crc_status |= data; 

				sd_clk_transfer_high();
			}
			if (crc_status == 0x0A)         //1010, CRC error
				return SD_MMC_ERROR_DATA_CRC;
			else if (crc_status == 0x0F)        //1111, Programming error
				return SD_MMC_ERROR_DRIVER_FAILURE;
							//0101, CRC ok
		
			//Check busy
			timeout = 0;
			do
			{
				sd_clk_transfer_low();

				data = sd_get_dat0_value();

				sd_clk_transfer_high();

				if(data)
					break;

				sd_delay_ms(1);
			}while(timeout < SD_PROGRAMMING_TIMEOUT/TIMER_1MS);

			if(timeout >= SD_PROGRAMMING_TIMEOUT/TIMER_1MS)
				return SD_MMC_ERROR_TIMEOUT;
		}

		data_offset += write_block_count*sd_mmc_info->blk_len;
		block_count -= write_block_count;
	}

	return SD_MMC_NO_ERROR;
}

int sdio_write_data_byte_hw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long byte_count, unsigned char *data_buf)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned long write_byte_count, data_offset = 0;

	unsigned long sdio_extend_rw = 0;
	SDIO_IO_RW_EXTENDED_ARG *sdio_io_extend_rw = (void *)&sdio_extend_rw;

	while(byte_count)
	{
		if(byte_count > 512)
			write_byte_count = 512;
		else
			write_byte_count = byte_count;

		sdio_io_extend_rw->R_W_Flag = SDIO_Write_Data;
		sdio_io_extend_rw->Block_Mode = SDIO_Byte_MODE;
		sdio_io_extend_rw->OP_Code = buf_or_fifo;
		sdio_io_extend_rw->Function_No = function_no;
		sdio_io_extend_rw->Byte_Block_Count = write_byte_count;
		sdio_io_extend_rw->Register_Address = ((sdio_addr+data_offset) & 0x1FFFF);

		ret = sd_send_cmd_hw(sd_mmc_info, IO_RW_EXTENDED, sdio_extend_rw, RESPONSE_R5, response, data_buf+data_offset, write_byte_count, 0);
		if(ret)
			return ret;

		data_offset += write_byte_count;
		byte_count -= write_byte_count;
	}

	return SD_MMC_NO_ERROR;
}

int sdio_write_data_byte_sw(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long byte_count, unsigned char *data_buf)
{
	int ret,i,j;
	unsigned long crc_status, data;
	
	unsigned long data_cnt = 0;
	unsigned timeout;
	
	unsigned char * org_buf = data_buf;
	unsigned short crc16_array[4] = {0, 0, 0, 0};
	unsigned short crc16;
	unsigned char char_mode[4][3] = {{0x10, 0x01, 0},
					{0x20, 0x02, 0},
					{0x40, 0x04, 0},
					{0x80, 0x08, 0}};

	unsigned long loop_num;

	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned long write_byte_count, data_offset = 0;

	unsigned long sdio_extend_rw = 0;
	SDIO_IO_RW_EXTENDED_ARG *sdio_io_extend_rw = (void *)&sdio_extend_rw;

	while(byte_count)
	{
		if(byte_count > 512)
			write_byte_count = 512;
		else
			write_byte_count = byte_count;

		sdio_io_extend_rw->R_W_Flag = SDIO_Write_Data;
		sdio_io_extend_rw->Block_Mode = SDIO_Byte_MODE;
		sdio_io_extend_rw->OP_Code = buf_or_fifo;
		sdio_io_extend_rw->Function_No = function_no;
		sdio_io_extend_rw->Byte_Block_Count = write_byte_count;
		sdio_io_extend_rw->Register_Address = ((sdio_addr+data_offset) & 0x1FFFF);

		ret = sd_send_cmd_sw(sd_mmc_info, IO_RW_EXTENDED, sdio_extend_rw, RESPONSE_R5, response);
		if(ret)
			return ret;

		loop_num = write_byte_count;
		org_buf = data_buf;

		//Nwr cycles delay
		sd_delay_clocks_h(sd_mmc_info, SD_MMC_TIME_NWR);

		//Start bit
		if(sd_mmc_info->bus_width == SD_BUS_WIDE)
		{
			sd_set_dat0_3_output();

			sd_clk_transfer_low();

			sd_set_dat0_3_value(0x00);

			sd_clk_transfer_high();
		}
		else
		{
			sd_set_dat0_output();

			sd_clk_transfer_low();

			sd_set_dat0_value(0x00);

			sd_clk_transfer_high();
		}

		//Write data
		if(sd_mmc_info->bus_width == SD_BUS_WIDE)
		{
			for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
			{
				for(i=1; i>=0; i--)
				{
					sd_clk_transfer_low();

					data = (*data_buf >> (i<<2)) & 0x0F;
					sd_set_dat0_3_value(data);

					sd_clk_transfer_high();
				}
				
				data_buf++;
			}

			//Caculate CRC16 value and write to line

			for(i=0; i<4; i++)
			{
				crc16_array[i] = sd_cal_crc_mode(org_buf, write_byte_count, char_mode[i]);
			}

			//Write CRC16
			for(i=15; i>=0; i--)
			{
				sd_clk_transfer_low();

				data = 0;
				for(j=3; j>=0; j--)
				{
					data <<= 1; 
					data |= (crc16_array[j] >> i) & 0x0001;
				}
				sd_set_dat0_3_value(data);

				sd_clk_transfer_high();
			}
		}
		else    // only dat0 line
		{
			for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
			{
				for(i=7; i>=0; i--)
				{
					sd_clk_transfer_low();

					data = (*data_buf >> i) & 0x01;
					sd_set_dat0_value(data);

					sd_clk_transfer_high();
				}

				data_buf++;
			}
			
			//Caculate CRC16 value and write to line
			crc16 = sd_cal_crc16(org_buf, write_byte_count);

			//Write CRC16
			for(i=15; i>=0; i--)
			{
				sd_clk_transfer_low();

				data = (crc16 >> i) & 0x0001;
				sd_set_dat0_value(data);

				sd_clk_transfer_high();
			}
		}

		//End bit
		if(sd_mmc_info->bus_width == SD_BUS_WIDE)
		{
			sd_clk_transfer_low();

			sd_set_dat0_3_value(0x0F);

			sd_clk_transfer_high();

			sd_set_dat0_3_input();
		}
		else
		{
			sd_clk_transfer_low();

			sd_set_dat0_value(0x01);

			sd_clk_transfer_high();

			sd_set_dat0_input();
		}

		sd_delay_clocks_h(sd_mmc_info, SD_MMC_Z_CMD_TO_RES);

		crc_status = 0;
	
		//Check CRC status
		sd_set_dat0_input();
		for(i = 0; i < 5; i++)
		{
			sd_clk_transfer_low();

			data = sd_get_dat0_value();
			crc_status <<= 1;
			crc_status |= data; 

			sd_clk_transfer_high();
		}
		if (crc_status == 0x0A)         //1010, CRC error
			return SD_MMC_ERROR_DATA_CRC;
		else if (crc_status == 0x0F)        //1111, Programming error
			return SD_MMC_ERROR_DRIVER_FAILURE;
							//0101, CRC ok
		
		//Check busy
		timeout = 0;
		do
		{
			sd_clk_transfer_low();
			
			data = sd_get_dat0_value();
			
			sd_clk_transfer_high();
		
			if(data)
				break;

			sd_delay_ms(1);
		}while(timeout < SD_PROGRAMMING_TIMEOUT/TIMER_1MS);

		if(timeout >= SD_PROGRAMMING_TIMEOUT/TIMER_1MS)
			return SD_MMC_ERROR_TIMEOUT;

		data_offset += write_byte_count;
		byte_count -= write_byte_count;
	}

	return SD_MMC_NO_ERROR;
}

int sdio_write_data(SD_MMC_Card_Info_t *sd_mmc_info, int function_no, int buf_or_fifo, unsigned long sdio_addr, unsigned long byte_count, unsigned char *data_buf)
{
	int error = 0,ret;
	unsigned long block_nums, byte_nums;
	BUG_ON(sd_mmc_info->sdio_blk_len[function_no] == 0);

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if (sd_mmc_info->sd_save_hw_io_flag) {
    		WRITE_CBUS_REG(SDIO_CONFIG, sd_mmc_info->sd_save_hw_io_config);
      		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sd_mmc_info->sd_save_hw_io_mult_config);
    	}		
		if (sd_mmc_info->sdxc_save_hw_io_flag) {
			WRITE_CBUS_REG(SD_REG2_CNTL, sd_mmc_info->sdxc_save_hw_io_ctrl);
			WRITE_CBUS_REG(SD_REG4_CLKC, 0);
			WRITE_CBUS_REG(SD_REG4_CLKC, sd_mmc_info->sdxc_save_hw_io_clk);
		}
	}
#endif
#ifdef SD_MMC_SW_CONTROL
	if(SD_WORK_MODE == CARD_SW_MODE)
		sd_mmc_io_config(sd_mmc_info);
#endif

	sd_mmc_info->blk_len = sd_mmc_info->sdio_blk_len[function_no];
	block_nums = sd_mmc_info->blk_len;
	block_nums = byte_count / block_nums;
	byte_nums = byte_count % sd_mmc_info->blk_len;
	//printk("sdio write data addr %x at fun %d cnt: %d blk len %d\n", sdio_addr, function_no, byte_count, sd_mmc_info->blk_len);

	if(block_nums == 0)
	{
		if(byte_nums)
		{
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
				error = sdio_write_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_nums, data_buf);
#endif
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
				error = sdio_write_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_nums, data_buf);
#endif
			if(error)
			{
				ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
				if(ret)
					return ret;

#ifdef SD_MMC_HW_CONTROL
				if(SD_WORK_MODE == CARD_HW_MODE)
					error = sdio_write_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_nums, data_buf);
#endif
#ifdef SD_MMC_SW_CONTROL
				if(SD_WORK_MODE == CARD_SW_MODE)
					error = sdio_write_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_nums, data_buf);
#endif
				if(error)
					return error;
			}
		}
		else
		{
			error = SD_MMC_ERROR_BLOCK_LEN;
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sdio_write_data() blklen %d fun no %d\n", sd_error_to_string(error), sd_mmc_info->blk_len, function_no);
#endif
			return error;
		}

		if(error)
		{			
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sdio_write_byte()\n", sd_error_to_string(error));
#endif
			ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
			if(ret)
				return ret;

			return error;
		}
	}
	else
	{
		if(sd_mmc_info->blk_len == sd_mmc_info->sdio_blk_len[function_no])
		{
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
			{
				error = sdio_write_data_block_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums, data_buf);
				if(error)
				{
					ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
					if(ret)
						return ret;

					error = sdio_write_data_block_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums, data_buf);
					if(error)
					{
						ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
						if(ret)
							return ret;

						error = sdio_write_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums*sd_mmc_info->blk_len, data_buf);
					}
				}

				if(byte_nums)
				{
					error = sdio_write_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr+block_nums*sd_mmc_info->blk_len, byte_nums, data_buf+block_nums*sd_mmc_info->blk_len);
				}
			}
#endif
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
			{
				error = sdio_write_data_block_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums, data_buf);
				if(error)
				{
					ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
					if(ret)
						return ret;

					error = sdio_write_data_block_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums, data_buf);
					if(error)
					{
						ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
						if(ret)
							return ret;

						error = sdio_write_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, block_nums*sd_mmc_info->blk_len, data_buf);
					}
				}

				if(byte_nums)
				{
					error = sdio_write_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr+block_nums*sd_mmc_info->blk_len, byte_nums, data_buf+block_nums*sd_mmc_info->blk_len);
				}
			}
#endif
		}
		else
		{
#ifdef SD_MMC_HW_CONTROL
			if(SD_WORK_MODE == CARD_HW_MODE)
				error = sdio_write_data_byte_hw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_count, data_buf);
#endif
#ifdef SD_MMC_SW_CONTROL
			if(SD_WORK_MODE == CARD_SW_MODE)
				error = sdio_write_data_byte_sw(sd_mmc_info, function_no, buf_or_fifo, sdio_addr, byte_count, data_buf);
#endif
		}

		if(error)
		{			
#ifdef  SD_MMC_DEBUG
			Debug_Printf("#%s error occured in sdio_write_block() blklen %d fun no %d\n", sd_error_to_string(error), sd_mmc_info->blk_len, function_no);
#endif
			ret = sdio_data_transfer_abort(sd_mmc_info, function_no);
			if(ret)
				return ret;

			return error;
		}
	}

	return SD_MMC_NO_ERROR;
}

int sdio_open_target_interrupt(SD_MMC_Card_Info_t *sd_mmc_info, int function_no)
{
	int error = 0;
	unsigned char read_reg_data, write_reg_data;

	error = sdio_read_reg(sd_mmc_info, 0, Card_Capability_REG, &read_reg_data);
	if(error)
		return error;

	write_reg_data = ((1 << function_no) | SDIO_INT_EN_MASK);
	error = sdio_write_reg(sd_mmc_info, 0, INT_ENABLE_REG, &write_reg_data, SDIO_Read_After_Write);
	if(error)
    	return error;

	return SD_MMC_NO_ERROR;
}

int sdio_close_target_interrupt(SD_MMC_Card_Info_t *sd_mmc_info, int function_no)
{
	int error = 0;
	unsigned char read_reg_data, write_reg_data;

	error = sdio_read_reg(sd_mmc_info, 0, Card_Capability_REG, &read_reg_data);
	if(error)
		return error;

	write_reg_data = 0;
	error = sdio_write_reg(sd_mmc_info, 0, INT_ENABLE_REG, &write_reg_data, SDIO_Read_After_Write);
	if(error)
    	return error;

	return SD_MMC_NO_ERROR;
}

/*sdio_select_card & sdio_read_rca & sdio_rw_direct used by nrx600, 
    claim host in nano_download
*/
int sdio_select_card(struct memory_card *card)
{
    int ret;
    SD_MMC_Card_Info_t *sd_mmc_info;
    unsigned char response[MAX_RESPONSE_BYTES];
    BUG_ON(!card);
    sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;

    card->host->card_busy = card;
    ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SELECT_DESELECT_CARD,
                sd_mmc_info->card_rca<<16, RESPONSE_R1B, response, 0,
 0, 1);
    if(ret)
        printk("[sdio_select_card] ret = %d\n", ret);
    return ret;
}
EXPORT_SYMBOL(sdio_select_card);

int sdio_read_rca(struct memory_card *card, unsigned* rca)
{
    int ret, slot_id;
    unsigned char response[MAX_RESPONSE_BYTES];
    SD_MMC_Card_Info_t *sd_mmc_info;
    BUG_ON(!card);
    sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;

    card->host->card_busy = card;
    slot_id = 1;
    ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_RELATIVE_ADDR, slot_id<<16,
                RESPONSE_R6, response, 0, 0, 0);   ///* Send out a byte to read RCA*/
    if(ret)
        printk("[sdio_read_rca] ret = %d\n", ret);
    sd_mmc_info->card_rca = ((SD_Response_R6_t *)response)->rca_high << 8 |
                ((SD_Response_R6_t *)response)->rca_low;
    *rca = sd_mmc_info->card_rca;
    printk("*rca = %x\n", *rca);
    return ret;
}
EXPORT_SYMBOL(sdio_read_rca);


int sdio_rw_direct(struct memory_card  *card, unsigned int arg)
{
    int ret;
    unsigned char response[MAX_RESPONSE_BYTES];
    SD_MMC_Card_Info_t *sd_mmc_info;
    BUG_ON(!card);
    sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;

    card->host->card_busy = card;
    ret = sd_send_cmd_hw(sd_mmc_info, IO_RW_DIRECT, arg, RESPONSE_R5,
                    response, 0, 0, 1);
    if(ret)
        printk("[sdio_rw_direct] ret = %d\n", ret);
    return ret;
}
EXPORT_SYMBOL(sdio_rw_direct);

/*void sd_mmc_get_info(blkdev_stat_t *info)
{
	if(info->magic != BLKDEV_STAT_MAGIC)
		return;
	info->valid = 1;
	info->blk_size = sd_mmc_info->blk_len;
	info->blk_num = sd_mmc_info->blk_nums;
	info->serial_no = sd_mmc_info->card_psn;
	info->st_write_protect = sd_mmc_info->write_protected_flag;
	switch(sd_mmc_info->card_type)
	{
		case CARD_TYPE_SD:
			info->blkdev_name = "SD card";
			break;
		case CARD_TYPE_SDHC:
			info->blkdev_name = "SDHC card";
			break;
		case CARD_TYPE_MMC:
			info->blkdev_name = "MMC card";
			break;
		default:
			break;
	}
}*/

#if 0
//#if defined (SDXC_DEBUG)
//lin
#define SDXC_B_ENABLE	CBUS_REG_ADDR(0x201b)
#define SDXC_B_OUTPUT	CBUS_REG_ADDR(0x201c)
#define SDXC_B_INPUT	CBUS_REG_ADDR(0x201d)

static struct aml_card_info  my_card_info[] = {
		[0] = {
			.name			= "inand_card_sdxc",
			.work_mode		= CARD_HW_MODE,
			.io_pad_type		= SDXC_BOOT_0_11,
			.card_ins_en_reg	= 0,
			.card_ins_en_mask	= 0,
			.card_ins_input_reg = 0,
			.card_ins_input_mask	= 0,
			.card_power_en_reg	= BOOT_GPIO_ENABLE,
			.card_power_en_mask = PREG_IO_9_MASK,
			.card_power_output_reg	= BOOT_GPIO_OUTPUT,
			.card_power_output_mask = PREG_IO_9_MASK,
			.card_power_en_lev	= 1,
			.card_wp_en_reg 	= 0,
			.card_wp_en_mask	= 0,
			.card_wp_input_reg	= 0,
			.card_wp_input_mask = 0,
			.card_extern_init	= 0,
		},
		
		[1] = {
			.name			= "inand_card_sdhc",
			.work_mode		= CARD_HW_MODE,
			.io_pad_type		= SDHC_BOOT_0_11,
			.card_ins_en_reg	= 0,
			.card_ins_en_mask	= 0,
			.card_ins_input_reg = 0,
			.card_ins_input_mask	= 0,
			.card_power_en_reg	= BOOT_GPIO_ENABLE,
			.card_power_en_mask = PREG_IO_9_MASK,
			.card_power_output_reg	= BOOT_GPIO_OUTPUT,
			.card_power_output_mask = PREG_IO_9_MASK,
			.card_power_en_lev	= 1,
			.card_wp_en_reg 	= 0,
			.card_wp_en_mask	= 0,
			.card_wp_input_reg	= 0,
			.card_wp_input_mask = 0,
			.card_extern_init	= 0,
		},
		
		[3] = {		//m3 sdhc
			.name = "sd_card_sdxc",
			.work_mode = CARD_HW_MODE,
			.io_pad_type = SDXC_CARD_0_5,	//SDXC-B
			.card_ins_en_reg = SDXC_B_ENABLE,
			.card_ins_en_mask = PREG_IO_29_MASK,
			.card_ins_input_reg = SDXC_B_INPUT,
			.card_ins_input_mask = PREG_IO_29_MASK,
			.card_power_en_reg = SDXC_B_ENABLE,
			.card_power_en_mask = PREG_IO_31_MASK,
			.card_power_output_reg = SDXC_B_OUTPUT,
			.card_power_output_mask = PREG_IO_31_MASK,
			.card_power_en_lev = 0,
			.card_wp_en_reg = 0,
			.card_wp_en_mask = 0,
			.card_wp_input_reg = 0,
			.card_wp_input_mask = 0,
			.card_extern_init = 0,
		},

		[4] = {
			.name			= "sd_card_sdhc",
			.work_mode		= CARD_HW_MODE,
			.io_pad_type		= SDHC_CARD_0_5,	//SDHC-B
			.card_ins_en_reg	= CARD_GPIO_ENABLE,
			.card_ins_en_mask	= PREG_IO_29_MASK,
			.card_ins_input_reg = CARD_GPIO_INPUT,
			.card_ins_input_mask	= PREG_IO_29_MASK,
			.card_power_en_reg	= CARD_GPIO_ENABLE,
			.card_power_en_mask = PREG_IO_31_MASK,
			.card_power_output_reg	= CARD_GPIO_OUTPUT,
			.card_power_output_mask = PREG_IO_31_MASK,
			.card_power_en_lev	= 0,
			.card_wp_en_reg 	= 0,
			.card_wp_en_mask	= 0,
			.card_wp_input_reg	= 0,
			.card_wp_input_mask = 0,
			.card_extern_init	= 0,
		},

	    [2] = {		//m1 sdio
	        .name = "sd_card",
	        .work_mode = CARD_HW_MODE,
	        .io_pad_type = SDHC_CARD_0_5,
	        .card_ins_en_reg = CARD_GPIO_ENABLE,
	        .card_ins_en_mask = PREG_IO_29_MASK,
	        .card_ins_input_reg = CARD_GPIO_INPUT,
	        .card_ins_input_mask = PREG_IO_29_MASK,
	        .card_power_en_reg = CARD_GPIO_ENABLE,
	        .card_power_en_mask = PREG_IO_31_MASK,
	        .card_power_output_reg = CARD_GPIO_OUTPUT,
	        .card_power_output_mask = PREG_IO_31_MASK,
	        .card_power_en_lev = 0,
	        .card_wp_en_reg = 0,
	        .card_wp_en_mask = 0,
	        .card_wp_input_reg = 0,
	        .card_wp_input_mask = 0,
	        .card_extern_init = 0,
	    },
};

static int myidx = 0;
static struct memory_card *sdxc_card = NULL;

static unsigned int my_parse_num(char *buf, char **res_buf)
{
	unsigned int value;
	unsigned int c;

	value = 0;
	while(1)
	{
		c = *buf++;
		if (c >= '0' && c <= '9')
		{
			value = value * 10 + c - '0';
		}
		else
		{
			break;
		}
	}

	if (res_buf != NULL)
		*res_buf = buf;
	
	return value;
}

static void my_test(char *buf)
{
	char *pcur;
	int t1 = my_parse_num(buf, &pcur);
	int t2 = my_parse_num(pcur, &pcur);
	int t3 = my_parse_num(pcur, &pcur);
	int i;
				
	struct card_host *host = sdxc_card->host;				
	__card_claim_host(host, sdxc_card);
	///sd_io_init(sdxc_card);
	using_sdxc_controller = 0;	//reset for sd_io_init()

	switch (t1) {
					case 1 : 
						{
							printk("test gpio\n");
							sd_set_cmd_output();
							sd_set_cmd_value(t2);
							sd_set_clk_output();
							sd_set_clk_low();
							sd_set_dat0_3_output();
							sd_set_dat0_3_value(t3);
							msleep(10);
							i = 10;
							while (i--) {
								sd_set_dat0_3_value(--t3);
								msleep(2);
							}
						}
						break;

					case 2 : 
						{
							unsigned int value, value2, value3;
							
							value = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
							value2 = READ_CBUS_REG(PERIPHS_PIN_MUX_4);
							value3 = READ_CBUS_REG(PERIPHS_PIN_MUX_5);
							printk("MUX2-sdxc:[0x%08x]/[0x%02x], MUX4:[0x%08x]/[0x%02x], MUX5:[0x%08x]/[0x%02x]\n", 
								value, (value >> 4) & 0x1F, value2, (value2 >> 26) & 0x1F, value3, (value3 >> 10) & 0x1F);

							value = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
							value2 = READ_CBUS_REG(PERIPHS_PIN_MUX_6);
							value3 = READ_CBUS_REG(PERIPHS_PIN_MUX_8);
							printk("MUX2-sdhc:[0x%08x]/[0x%02x], MUX6:[0x%08x]/[0x%02x], MUX8:[0x%08x]/[0x%02x]\n", 
								value, (value >> 10) & 0x3F, value2, (value2 >> 24) & 0x3F, value3, (value3 >> 0) & 0x3F);
						}
						break;

					case 3 : 
						{
							unsigned int value, value2, value3;
							
							value = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
							value2 = READ_CBUS_REG(PERIPHS_PIN_MUX_3);
							value3 = READ_CBUS_REG(PERIPHS_PIN_MUX_5);
							printk("MUX2-sdxc:[0x%08x]/[0x%02x], MUX4:[0x%08x]/[0x%02x], MUX5:[0x%08x]/[0x%02x]\n", 
								value, (value >> 4) & 0x1F, value2, (value2 >> 26) & 0x1F, value3, (value3 >> 10) & 0x1F);

							value &= ~0x0FFF0000;
							WRITE_CBUS_REG(PERIPHS_PIN_MUX_2, value);

							value2 &= ~0x7FFFFFFF;
							WRITE_CBUS_REG(PERIPHS_PIN_MUX_3, value2);

							value = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
							value2 = READ_CBUS_REG(PERIPHS_PIN_MUX_3);
							value3 = READ_CBUS_REG(PERIPHS_PIN_MUX_8);
							printk("MUX2-sdhc:[0x%08x]/[0x%02x], MUX6:[0x%08x]/[0x%02x], MUX8:[0x%08x]/[0x%02x]\n", 
								value, (value >> 10) & 0x3F, value2, (value2 >> 24) & 0x3F, value3, (value3 >> 0) & 0x3F);
						}
						break;

					case 4 : 
						{
							unsigned int value4, value6, value2, value3;
							
							value4 = READ_CBUS_REG(PERIPHS_PIN_MUX_4);
							value6 = READ_CBUS_REG(PERIPHS_PIN_MUX_6);
							value2 = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
							value3 = READ_CBUS_REG(PERIPHS_PIN_MUX_3);
							printk("MUX4:[0x%08x]/[0x%02x], MUX6:[0x%08x]/[0x%02x], MUX2:[0x%08x]/[0x%03x], MUX3:[0x%08x]/[0x%02x]\n", 
								value4, (value4 >> 26) & 0x1F, value6, (value6 >> 24) & 0x3F, value2, (value2 >> 16) & 0xFFF, value3, (value3 >> 31) & 0x01);
						}
						break;

					case 5 : 
						{
							printk("PINMUX :\n[0x%08x], [0x%08x], [0x%08x], [0x%08x]\n[0x%08x], [0x%08x], [0x%08x], [0x%08x]\n[0x%08x], [0x%08x], [0x%08x], [0x%08x], [0x%08x]\n", 
								READ_CBUS_REG(PERIPHS_PIN_MUX_0), READ_CBUS_REG(PERIPHS_PIN_MUX_1), READ_CBUS_REG(PERIPHS_PIN_MUX_2), READ_CBUS_REG(PERIPHS_PIN_MUX_3),
								READ_CBUS_REG(PERIPHS_PIN_MUX_4), READ_CBUS_REG(PERIPHS_PIN_MUX_5), READ_CBUS_REG(PERIPHS_PIN_MUX_6), READ_CBUS_REG(PERIPHS_PIN_MUX_7),
								READ_CBUS_REG(PERIPHS_PIN_MUX_8), READ_CBUS_REG(PERIPHS_PIN_MUX_9), READ_CBUS_REG(PERIPHS_PIN_MUX_10), READ_CBUS_REG(PERIPHS_PIN_MUX_11), READ_CBUS_REG(PERIPHS_PIN_MUX_12));
						}
						break;
						
					case 6 : 	//clear t2[t3]
						{
							unsigned int value;
							printk("clear PIN_MUX_%d\n", t2);
							switch (t2) {
							case 0 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_0);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_0, value);
								break;
							
							case 1 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_1);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_1, value);
								break;
								
							case 2 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_2, value);
								break;

							case 3 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_3);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_3, value);
								break;
							
							case 4 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_4);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_4, value);
								break;

							case 5 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_5);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_5, value);
								break;

							case 6 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_6);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_6, value);
								break;

							case 7 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_7);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_7, value);
								break;

							case 8 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_8);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_8, value);
								break;

							case 9 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_9);
								value &= ~t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_9, value);
								break;
								
							default : 
								break;
							}							
						}
						break;

					case 7 : 	//set t2[t3]
						{
							unsigned int value;
							printk("set PIN_MUX_%d\n", t2);
							switch (t2) {
							case 2 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_2);
								value |= t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_2, value);
								break;

							case 3 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_3);
								value |= t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_3, value);
								break;
							
							case 4 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_4);
								value |= t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_4, value);
								break;

							case 6 : 
								value = READ_CBUS_REG(PERIPHS_PIN_MUX_6);
								value |= t3;
								WRITE_CBUS_REG(PERIPHS_PIN_MUX_6, value);
								break;
								
							default : 
								break;
							}							
						}
						break;

					case 8 : 
						printk("set myt1 from %d to %d\n", myt1, t2);
						myt1 = t2;
						break;

					default :
						break;
	}
	card_release_host(host);
}

static struct memory_card* sdxc_card_setup(struct card_host *host, int idx)
{
	//int err = 0;
	struct memory_card* card;
	//SD_MMC_Card_Info_t *sd_mmc_info;

	card = kzalloc(sizeof(struct memory_card), GFP_KERNEL);
	if (!card)
		return ERR_PTR(-ENOMEM);
	
	card_init_card(card, host);
	
	card->unit_state = CARD_UNIT_NOT_READY;
	strcpy(card->name, "sdxc");
	card->card_type = CARD_SECURE_DIGITAL;
	card->card_plat_info = &my_card_info[idx];
	printk("setup card %s\n", card->card_plat_info->name);

	if ((card->card_plat_info->io_pad_type == SDXC_CARD_0_5) || 
		(card->card_plat_info->io_pad_type == SDXC_BOOT_0_11) ||
		(card->card_plat_info->io_pad_type == SDXC_GPIOX_0_9))
		myidx = 0;
	else
		myidx = 1;

/*
	__card_claim_host(host, card);
	err = sd_mmc_probe(card);
	using_sdxc_controller = 0;		//reset for sd_io_init()

	if (err)
	{
		card_release_host(host);
		kfree(card);
		return NULL;
	}

	sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;
	if (myidx == 0)
		err = sdxc_mmc_staff_init(sd_mmc_info);
	else
		err = sd_mmc_staff_init(sd_mmc_info);
	if (err)
	{
		sd_gpio_enable(sd_mmc_info->io_pad_type);
		card_release_host(host);
		kfree(card);
		return NULL;

	}
	sd_mmc_set_input(sd_mmc_info);	//???

	sd_gpio_enable(sd_mmc_info->io_pad_type);
	card_release_host(host);
*/	
	return card;
}	

static int sdxc_card_present(struct memory_card* card)
{
	struct card_host *host = card->host;

	__card_claim_host(host, card);
	card->card_io_init(card);	//sd_io_init()
	card->card_detector(card);	//sd_insert_detector()
	using_sdxc_controller = 0;	//reset for sd_io_init()
	card_release_host(host);
	
	if (card->card_status == CARD_INSERTED)
		return 1;
	else
		return 0;
}

#define CHCEK_CONTINUE()	\
	{	\
	if (cursteps >= steps)	\
		goto error;		\
	printk("step %d\n", ++cursteps);	\
	}

static int sdxc_identify_verbose(SD_MMC_Card_Info_t *sd_mmc_info, int steps)
{
	int error = 0;
	int cursteps = 0;

	sdio_close_host_interrupt(SDIO_IF_INT);

#ifdef  SD_MMC_DEBUG
	Debug_Printf("\nSDXC initialization started......\n");
#endif
	sd_mmc_probe(sdxc_card);
	sd_mmc_info = sdxc_card->card_info;

	CHCEK_CONTINUE();	////1
	sdxc_mmc_staff_init(sd_mmc_info);

	CHCEK_CONTINUE();	////2
	sd_mmc_set_input(sd_mmc_info);
	error = sd_hw_reset(sd_mmc_info);
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_hw_reset()\n", sd_error_to_string(error));
#endif
		goto error;
	}

	sd_delay_ms(200);

	CHCEK_CONTINUE();	////3
	error = sd_voltage_validation(sd_mmc_info);
	
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_voltage_validation()\n", sd_error_to_string(error));
#endif
		goto error;
	}

	CHCEK_CONTINUE();	////4
	error = sd_identify_process(sd_mmc_info);
	if(error)
	{
#ifdef  SD_MMC_DEBUG
		Debug_Printf("#%s error occured in sd_identify_process()\n", sd_error_to_string(error));
#endif
		goto error;
	}

#ifdef SD_MMC_HW_CONTROL
	if(SD_WORK_MODE == CARD_HW_MODE)
	{
		if(!sd_mmc_info->disable_high_speed)	
		{
			CHCEK_CONTINUE();	////5
	    	error = sd_mmc_switch_function(sd_mmc_info);
	    	if(error)
	    	{
#ifdef  SD_MMC_DEBUG
		    	Debug_Printf("#%s error occured in sd_switch_funtion()\n", sd_error_to_string(error));
#endif
			//return error;
	    	}
		}
	}
#endif

	if(sd_mmc_info->card_type == CARD_TYPE_SDIO)
	{
		error = sd_check_sdio_card_type(sd_mmc_info);
		if(error)
	    {
#ifdef  SD_MMC_DEBUG
		    Debug_Printf("#%s error occured in sd_check_sdio_card_type()\n", sd_error_to_string(error));
#endif
			goto error;
	    }
	}

#ifdef SD_MMC_DEBUG
	{
		char *card_types[] = {
			"Unknown",
			"SD",
			"SDHC",
			"MMC",
			"EMMC",
			"SDIO"
		};
		char *card_str = card_types[sd_mmc_info->card_type];
		char *bus_str = sd_mmc_info->bus_width == SD_BUS_WIDE ? "Wide Bus" : "Single Bus";
		char *speed_str = sd_mmc_info->speed_class == HIGH_SPEED ? "High Speed" : "Normal Speed";
		Debug_Printf("sd_mmc_init() is completed successfully!\n");
		Debug_Printf("This %s card is working in %s and %s mode!\n\n", card_str, bus_str, speed_str);
	}
#endif

	CHCEK_CONTINUE();	////6
	sd_mmc_info->inited_flag = 1;
	sd_mmc_info->init_retry = 0;

	sd_mmc_info->sdxc_save_hw_io_flag = 1;
	sd_mmc_info->sdxc_save_hw_io_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
	sd_mmc_info->sdxc_save_hw_io_clk = READ_CBUS_REG(SD_REG4_CLKC);
	sd_gpio_enable(sd_mmc_info->io_pad_type);

	return SD_MMC_NO_ERROR;

error:
	sd_gpio_enable(sd_mmc_info->io_pad_type);
	return error;
}

static int sdxc_card_identify(struct memory_card* card, int steps)
{
	int ret;
	SD_MMC_Card_Info_t *sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;
	struct card_host *host = card->host;

	__card_claim_host(host, card);
	if (myidx == 0)
	{
		using_sdxc_controller = 1;
		ret = sdxc_identify_verbose(sd_mmc_info, steps);
		using_sdxc_controller = 0;
	}
	else
	{
		sd_mmc_probe(sdxc_card);
		sd_mmc_info = sdxc_card->card_info;
		ret = sd_mmc_init(sd_mmc_info);
	}
	card_release_host(host);

	return ret;
}

//for cmd "7"
static int sdxc_card_cmd(struct memory_card* card, unsigned int cmd)
{
	int ret;
	unsigned char response[MAX_RESPONSE_BYTES];
	SD_MMC_Card_Info_t *sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;
	struct card_host *host = card->host;

	__card_claim_host(host, card);
	sd_sdio_enable(sd_mmc_info->io_pad_type);
	
	if (myidx == 0)
		using_sdxc_controller = 1;

	switch (cmd)
	{
			case 0 : 	//SD_MMC_GO_IDLE_STATE
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_GO_IDLE_STATE, 0, RESPONSE_NONE, 0, 0, 0, 1);
				break;

//begin voltage validation
			case 8:	//SD_SEND_IF_COND
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SEND_IF_COND, 0x000001aa, RESPONSE_R7, response, 0, 0, 0);
				break;

			case 55:	//SD_APP_CMD
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, 0, RESPONSE_R1, response, 0, 0, 0);
				break;

			case 41:	//SD_APP_OP_COND	//acmd
			{
				SD_Response_R3_t * r3;
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, 0, RESPONSE_R1, response, 0, 0, 0);
				if (ret)
					break;
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_OP_COND, 0x40200000, RESPONSE_R3, response, 0, 0, 0);	//maybe check for ready
				r3 = (SD_Response_R3_t *)response;
				if(ret == SD_MMC_NO_ERROR && r3->ocr.Card_Busy)
				{
					printk("cmd41 : card ok\n");
				}
				else
				{
					printk("cmd41 : card still busy\n");
				}
				break;
			}

			case 42:	//SD_APP_OP_COND : loop until ready	//acmd
			{
				int loop = 0;
				do
				{
					SD_Response_R3_t * r3;
					
					ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, 0, RESPONSE_R1, response, 0, 0, 0);
					if (ret)
					{
						sd_delay_ms(10);
						loop++;
						continue;
					}
					
					ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_OP_COND, 0x40200000, RESPONSE_R3, response, 0, 0, 0);	
					r3 = (SD_Response_R3_t *)response;
					if(ret == SD_MMC_NO_ERROR && r3->ocr.Card_Busy)
					{
						printk("cmd41 : card ok -- loop%d\n", loop);
						break;
					}
					else
					{
						printk("cmd 41 : loop%d with %s\n", loop, sd_error_to_string(ret));
					}

					sd_delay_ms(10);
					loop++;					
				} while (loop < 10);
				
				break;
			}

			case 43:	//MMC_SEND_OP_COND : loop until ready	//acmd
			{
				int loop = 0;
				do
				{
					SD_Response_R3_t * r3;
					
					ret = sd_send_cmd_hw(sd_mmc_info, MMC_SEND_OP_COND, 0x40FF8000, RESPONSE_R3, response, 0, 0, 0); // 0x00200000: 3.3v~3.4v
					r3 = (SD_Response_R3_t *)response;
					if(ret == SD_MMC_NO_ERROR && r3->ocr.Card_Busy)
					{
						printk("cmd43 : card ok -- loop%d\n", loop);
						if(!r3->ocr.Card_Capacity_Status)
							sd_mmc_info->card_type = CARD_TYPE_MMC;
						else
							sd_mmc_info->card_type = CARD_TYPE_EMMC;
						break;
					}
					else
					{
						printk("cmd 43 : loop%d with %s\n", loop, sd_error_to_string(ret));
					}

					sd_delay_ms(10);
					loop++;					
				} while (loop < 10);
				
				break;
			}

//begin identify
			case 2:	//SD_MMC_ALL_SEND_CID
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_ALL_SEND_CID, 0, RESPONSE_R2_CID, response, 0, 0, 1);
				break;

			case 3:	//SD_MMC_SEND_RELATIVE_ADDR
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_RELATIVE_ADDR, 0, RESPONSE_R6, response, 0, 0, 0); 	//maybe save the RCA value
				sd_mmc_info->card_rca = ((SD_Response_R6_t *)response)->rca_high << 8 | ((SD_Response_R6_t *)response)->rca_low;
				printk("Got RCA=0x%x (%d)\n", sd_mmc_info->card_rca, sd_mmc_info->card_rca);
				break;

			case 10:	//SD_MMC_SEND_CID
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_CID, sd_mmc_info->card_rca<<16, RESPONSE_R2_CID, response, NULL, 0, 1);
				if (ret == SD_MMC_NO_ERROR)
				{
					my_disp_cid((MyCID*)&(((SD_Response_R2_CID_t *)response)->cid));
				}
				break;

			case 9:	//SD_MMC_SEND_CSD
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SEND_CSD, sd_mmc_info->card_rca<<16, RESPONSE_R2_CSD, response, 0, 0, 1);	//maybe parse the blk num & len
				sd_mmc_info->blk_len = 512;
				if (ret == SD_MMC_NO_ERROR)
				{
					my_disp_csdv2(&(((SDHC_Response_R2_CSD_t *)response)->csd));
				}
				break;

			case 7:	//SD_MMC_SELECT_DESELECT_CARD
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_SELECT_DESELECT_CARD, sd_mmc_info->card_rca<<16, RESPONSE_R1B, response, 0, 0, 1);
				break;

			case 51:	//SD_SEND_SCR 		//acmd
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, 0, 0, 1);  
				if (ret)
					break;
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SEND_SCR, 0, RESPONSE_R1, response, sd_mmc_info->sd_mmc_buf, sizeof(SD_REG_SCR_t), 1);	//may be parse spec_version & bus bit modes
				my_disp_scr((MySCR*)sd_mmc_info->sd_mmc_buf);
				break;
/*
			case 6:	//SD_SET_BUS_WIDTHS : 4 bits		//acmd
				sd_mmc_info->bus_width = SD_BUS_WIDE;
				if (using_sdxc_controller)
				{
					unsigned long sdxc_ctrl = 0;
					SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
					sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
					sdxc_ctrl_reg->dat_width = 1;
					WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
				}
				else
				{
					unsigned long sdio_config = 0;
					SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
					sdio_config = READ_CBUS_REG(SDIO_CONFIG);
					config_reg->bus_width = 1;
					WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				}
				sd_delay_ms(100);
				
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, 0, 0, 1);  
				if (ret)
				{
					sd_mmc_info->bus_width = SD_BUS_SINGLE;
					if (using_sdxc_controller)
					{
						unsigned long sdxc_ctrl = 0;
						SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
						sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
						sdxc_ctrl_reg->dat_width = 0;
						WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
					}
					else
					{
						unsigned long sdio_config = 0;
						SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
						sdio_config = READ_CBUS_REG(SDIO_CONFIG);
						config_reg->bus_width = 0;
						WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
					}					
					break;
				}
				
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x02, RESPONSE_R1, response, 0, 0, 1);
				if (ret == SD_MMC_NO_ERROR)
				{
				}
				else
				{
					sd_mmc_info->bus_width = SD_BUS_SINGLE;
					if (using_sdxc_controller)
					{
						unsigned long sdxc_ctrl = 0;
						SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
						sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
						sdxc_ctrl_reg->dat_width = 0;
						WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
					}
					else
					{
						unsigned long sdio_config = 0;
						SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
						sdio_config = READ_CBUS_REG(SDIO_CONFIG);
						config_reg->bus_width = 0;
						WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
					}
				}
				break;

			case 46:	//SD_SET_BUS_WIDTHS : 1 bit
				sd_mmc_info->bus_width = SD_BUS_SINGLE;
				if (using_sdxc_controller)
				{
					unsigned long sdxc_ctrl = 0;
					SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
					sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
					sdxc_ctrl_reg->dat_width = 0;
					WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
				}
				else
				{
					unsigned long sdio_config = 0;
					SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
					sdio_config = READ_CBUS_REG(SDIO_CONFIG);
					config_reg->bus_width = 0;
					WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				}

				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, 0, 0, 1);  
				if (ret)
					break;
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x00, RESPONSE_R1, response, 0, 0, 1);
				if (ret == SD_MMC_NO_ERROR)
				{
				}
				else
				{
				}
				break;
*/
			case 6: //SD_SET_BUS_WIDTHS : 4 bits		//acmd
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, 0, 0, 1);  
				if (ret)
					break;
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x02, RESPONSE_R1, response, 0, 0, 1);
				if (ret == SD_MMC_NO_ERROR)
				{
					sd_mmc_info->bus_width = SD_BUS_WIDE;
					if (using_sdxc_controller)
					{
						unsigned long sdxc_ctrl = 0;
						SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
						sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
						sdxc_ctrl_reg->dat_width = 1;
						WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
					}
					else
					{
						unsigned long sdio_config = 0;
						SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
						sdio_config = READ_CBUS_REG(SDIO_CONFIG);
						config_reg->bus_width = 1;
						WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
					}
				}
				break;

			case 46:	//SD_SET_BUS_WIDTHS : 1 bit		//acmd
				ret = sd_send_cmd_hw(sd_mmc_info, SD_APP_CMD, sd_mmc_info->card_rca<<16, RESPONSE_R1, response, 0, 0, 1);  
				if (ret)
					break;
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SET_BUS_WIDTHS, 0x00, RESPONSE_R1, response, 0, 0, 1);
				if (ret == SD_MMC_NO_ERROR)
				{
					sd_mmc_info->bus_width = SD_BUS_SINGLE;
					if (using_sdxc_controller)
					{
						unsigned long sdxc_ctrl = 0;
						SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
						sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
						sdxc_ctrl_reg->dat_width = 0;
						WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
					}
					else
					{
						unsigned long sdio_config = 0;
						SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
						sdio_config = READ_CBUS_REG(SDIO_CONFIG);
						config_reg->bus_width = 0;
						WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
					}
				}
				break;

			case 56 :	//SD_SWITCH_FUNCTION	//HS
			{
				unsigned char response[MAX_RESPONSE_BYTES];
				unsigned char *status_data_buf = sd_mmc_info->sd_mmc_buf;
				SD_Switch_Function_Status_t *switch_funtion_status;
				unsigned long sdio_config = 0;
				SDIO_Config_Reg_t *config_reg = NULL;
				
				memset(status_data_buf, 0, 64);
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x00FFFF01, RESPONSE_R1, response, status_data_buf, 64, 1);
				if(ret)
					break;
				
				switch_funtion_status = (SD_Switch_Function_Status_t *)status_data_buf;
				if(switch_funtion_status->Max_Current_Consumption == 0)
				{
					ret = SD_ERROR_SWITCH_FUNCTION_COMUNICATION;
					break;
				}
				
				my_disp_switch_status(switch_funtion_status);		//check disp
				
				memset(status_data_buf, 0, 64);
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x80FFFF01, RESPONSE_R1, response, status_data_buf, 64, 1);	
				if(ret)
					break;
				
				switch_funtion_status = (SD_Switch_Function_Status_t *)status_data_buf;
				if(switch_funtion_status->Max_Current_Consumption == 0 || switch_funtion_status->Function_Group_Status1 == 0xF)
				{
					ret = SD_ERROR_SWITCH_FUNCTION_COMUNICATION;
					break;
				}
				
				my_disp_switch_status(switch_funtion_status);		//switch disp
				
				if (using_sdxc_controller)
				{				
					unsigned long sdxc_clk = 0;
					SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
					sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
					sdxc_clk_reg->clk_ctl_en = 0;
					WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
					sdxc_clk_reg->clk_div = CLK_DIV_50M; //sdxx
					sdxc_clk_reg->clk_en = 1;
					sdxc_clk_reg->clk_ctl_en = 1;
					WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				}
				else
				{
					sdio_config = 0;
					config_reg = (void *)&sdio_config;
					sdio_config = READ_CBUS_REG(SDIO_CONFIG);
					config_reg->cmd_clk_divide = 1;//aml_system_clk / (2*SD_MMC_TRANSFER_HIGHSPEED_CLK) -1;
					WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				}
				
				sd_mmc_info->sdio_clk_unit = 1000/SD_MMC_TRANSFER_HIGHSPEED_CLK;
				sd_mmc_info->speed_class = HIGH_SPEED;
				break;
			}
			
			case 66 :	//SD_SWITCH_FUNCTION	//DS
			{
				unsigned char response[MAX_RESPONSE_BYTES];
				unsigned char *status_data_buf = sd_mmc_info->sd_mmc_buf;
				SD_Switch_Function_Status_t *switch_funtion_status;
				unsigned long sdio_config = 0;
				SDIO_Config_Reg_t *config_reg = NULL;
				
				memset(status_data_buf, 0, 64);
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x00FFFF00, RESPONSE_R1, response, status_data_buf, 64, 1);
				if(ret)
					break;
				
				switch_funtion_status = (SD_Switch_Function_Status_t *)status_data_buf;
				if(switch_funtion_status->Max_Current_Consumption == 0)
				{
					ret = SD_ERROR_SWITCH_FUNCTION_COMUNICATION;
					break;
				}

				my_disp_switch_status(switch_funtion_status);		//check disp
				
				memset(status_data_buf, 0, 64);
				ret = sd_send_cmd_hw(sd_mmc_info, SD_SWITCH_FUNCTION, 0x80FFFF00, RESPONSE_R1, response, status_data_buf, 64, 1);	
				if(ret)
					break;
				
				switch_funtion_status = (SD_Switch_Function_Status_t *)status_data_buf;
				if(switch_funtion_status->Max_Current_Consumption == 0 || switch_funtion_status->Function_Group_Status1 == 0xF)
				{
					ret = SD_ERROR_SWITCH_FUNCTION_COMUNICATION;
					break;
				}
				
				my_disp_switch_status(switch_funtion_status);		//switch disp
				
				if (using_sdxc_controller)
				{				
					unsigned long sdxc_clk = 0;
					SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
					sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
					sdxc_clk_reg->clk_ctl_en = 0;
					WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
					sdxc_clk_reg->clk_div = CLK_DIV_25M; //sdxx
					sdxc_clk_reg->clk_en = 1;
					sdxc_clk_reg->clk_ctl_en = 1;
					WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				}
				else
				{
					sdio_config = 0;
					config_reg = (void *)&sdio_config;
					sdio_config = READ_CBUS_REG(SDIO_CONFIG);
					config_reg->cmd_clk_divide = 4;//aml_system_clk / (2*SD_MMC_TRANSFER_HIGHSPEED_CLK) -1;
					WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				}
				
				sd_mmc_info->sdio_clk_unit = 1000/SD_MMC_TRANSFER_HIGHSPEED_CLK;
				sd_mmc_info->speed_class = NORMAL_SPEED;
				break;
			}

			default :
				printk("unknowd cmd %d\n", cmd);
				ret = SD_MMC_ERROR_GENERAL;
				break;
	}

	using_sdxc_controller = 0;
	
	sd_gpio_enable(sd_mmc_info->io_pad_type);
	card_release_host(host);

	return ret;
}

static int wr_cnt = 10;

//for cmd "9"
static int sdxc_card_rw(struct memory_card* card, unsigned int cmd, unsigned int lba, unsigned int cnt)
{
	int ret = 0;
	unsigned char response[MAX_RESPONSE_BYTES];
	unsigned char* data_buf;
	SD_MMC_Card_Info_t *sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;
	struct card_host *host = card->host;
	static unsigned char start_num = 0x55;

	__card_claim_host(host, card);
	sd_sdio_enable(sd_mmc_info->io_pad_type);
	
	if (myidx == 0)
		using_sdxc_controller = 1;

	data_buf = sd_mmc_info->sd_mmc_buf;

	switch (cmd)
	{
		case 20 :
			start_num = lba;
			printk("new start num = 0x%x -- %u\n", start_num, start_num);
			break;

		case 21 :
			sdxc_delay = lba;
			printk("new rw delay = %dus\n", sdxc_delay);
			break;

		case 22 : 
			sd_mmc_info->max_blk_count = lba;
			printk("new sd_mmc_info->max_blk_count = %d\n", sd_mmc_info->max_blk_count);
			break;

		case 23 :
			my_check = lba;
			printk("my_check=%d\n", my_check);
			break;

		case 3 : 
			printk("send stop\n");
			ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
			break;

		case 31 : 
			{
				printk("sd_mmc_prepare_power\n");
				sd_mmc_prepare_power(sd_mmc_info);
				msleep(5);
				break;
			}
		
		case 32 : 
			{
				printk("sd_mmc_set_input\n");
				sd_gpio_enable(sd_mmc_info->io_pad_type);
				sd_mmc_set_input(sd_mmc_info);
				msleep(5);
				break;
			}
		
		case 33 : 
			{
				printk("test STAT3\n");
				sd_gpio_enable(sd_mmc_info->io_pad_type);
				
				sd_set_cmd_output();
				sd_set_cmd_value(lba);
				sd_set_clk_output();
				sd_set_clk_low();
				sd_set_dat0_3_output();
				sd_set_dat0_3_value(cnt);
				msleep(5);
				break;
			}

/*
		case 31 : 
			{
				printk("\n");
				break;
			}
*/

		case 4 : 
			{				
				unsigned int pullup;
				pullup = READ_CBUS_REG(PAD_PULL_UP_REG3);
				printk("PAD_PULL_UP_REG3=0x%8x\n", pullup);
				break;
			}
		
		case 5 : 
			{				
				unsigned int pullup;
				pullup = READ_CBUS_REG(PAD_PULL_UP_REG3);
				pullup |= 0xFF;
				WRITE_CBUS_REG(PAD_PULL_UP_REG3, pullup);	
				printk("set PAD_PULL_UP_REG3 pullup\n");
				break;
			}
		
		case 6 : 
			{				
				unsigned int pullup;
				pullup = READ_CBUS_REG(PAD_PULL_UP_REG3);
				pullup &= ~0xFF;
				WRITE_CBUS_REG(PAD_PULL_UP_REG3, pullup);	
				printk("unset PAD_PULL_UP_REG3 pullup\n");
				break;
			}
		
		case 10 : 
			printk("switch to 1 bits for emmc\n");
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b70000, RESPONSE_R1, response, 0, 0, 1);
			if(ret)
			{
				sd_mmc_info->bus_width = SD_BUS_SINGLE;
			}
			else
			{
				sd_mmc_info->bus_width = SD_BUS_SINGLE;
				if (using_sdxc_controller)
				{
					unsigned long sdxc_ctrl = 0;
					SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
					sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
					sdxc_ctrl_reg->dat_width = 0;
					WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
				}
				else
				{
					unsigned long sdio_config = 0;
					SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
					sdio_config = READ_CBUS_REG(SDIO_CONFIG);
					config_reg->bus_width = 0;
					WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				}
			}
			break;

		case 11 : 
			printk("switch to 4 bits for emmc\n");
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b70100, RESPONSE_R1, response, 0, 0, 1);
			if(ret)
			{
				sd_mmc_info->bus_width = SD_BUS_SINGLE;
			}
			else
			{
				sd_mmc_info->bus_width = SD_BUS_WIDE;
				if (using_sdxc_controller)
				{
					unsigned long sdxc_ctrl = 0;
					SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
					sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
					sdxc_ctrl_reg->dat_width = 1;
					WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
				}
				else
				{
					unsigned long sdio_config = 0;
					SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
					sdio_config = READ_CBUS_REG(SDIO_CONFIG);
					config_reg->bus_width = 1;
					WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				}
			}
			break;
			
		case 12 : 
			printk("switch to 8 bits for emmc\n");
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b70200, RESPONSE_R1, response, 0, 0, 1);
			if(ret)
			{
				sd_mmc_info->bus_width = SD_BUS_SINGLE;
			}
			else
			{
				sd_mmc_info->bus_width = SD_BUS_WIDE;
				if (using_sdxc_controller)
				{
					unsigned long sdxc_ctrl = 0;
					SDXC_Ctrl_Reg_t *sdxc_ctrl_reg = (void *)&sdxc_ctrl;
					sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
					sdxc_ctrl_reg->dat_width = 2;
					printk("before set : %x\n", (unsigned int)sdxc_ctrl);
					WRITE_CBUS_REG(SD_REG2_CNTL, sdxc_ctrl);
					sdxc_ctrl = READ_CBUS_REG(SD_REG2_CNTL);
					printk("after  set : %x\n", (unsigned int)sdxc_ctrl);
				}
				else
				{
					unsigned long sdio_config = 0;
					SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
					sdio_config = READ_CBUS_REG(SDIO_CONFIG);
					config_reg->bus_width = 1;
					WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
				}
			}
			break;

		case 16 : 
			printk("switch to identify speed for emmc\n");
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b90000, RESPONSE_R1, response, 0, 0, 1);
			if(ret)
				break;
			
			if (using_sdxc_controller)
			{				
				unsigned long sdxc_clk = 0;
				SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
				sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
				sdxc_clk_reg->clk_ctl_en = 0;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				sdxc_clk_reg->clk_div = CLK_DIV_IDENTIFY; //sdxx
				sdxc_clk_reg->clk_en = 1;
				sdxc_clk_reg->clk_ctl_en = 1;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			}			
			sd_mmc_info->sdio_clk_unit = 1000/SD_MMC_IDENTIFY_CLK;
			sd_mmc_info->speed_class = NORMAL_SPEED;
			break;

		case 13 : 
			printk("switch to default speed for emmc : %d=%dK\n", lba, 500000/(lba+1));
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b90000, RESPONSE_R1, response, 0, 0, 1);
			if(ret)
				break;
			
			if (using_sdxc_controller)
			{				
				unsigned long sdxc_clk = 0;
				SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
				sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
				sdxc_clk_reg->clk_ctl_en = 0;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				sdxc_clk_reg->clk_div = lba; //sdxx
				sdxc_clk_reg->rx_clk_phase_sel = cnt;
				sdxc_clk_reg->clk_en = 1;
				sdxc_clk_reg->clk_ctl_en = 1;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			}			
			sd_mmc_info->sdio_clk_unit = 1000/SD_MMC_TRANSFER_CLK;
			sd_mmc_info->speed_class = NORMAL_SPEED;
			break;

		case 14 : 
			printk("switch to high speed for emmc : %d=%dK\n", lba, 500000/(lba+1));
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b90100, RESPONSE_R1, response, 0, 0, 1);
			if(ret)
				break;
			
			if (using_sdxc_controller)
			{				
				unsigned long sdxc_clk = 0;
				SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
				sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
				sdxc_clk_reg->clk_ctl_en = 0;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				sdxc_clk_reg->clk_div = lba; //sdxx
				sdxc_clk_reg->rx_clk_phase_sel = cnt;
				sdxc_clk_reg->clk_en = 1;
				sdxc_clk_reg->clk_ctl_en = 1;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			}
			sd_mmc_info->sdio_clk_unit = 1000/SD_MMC_TRANSFER_HIGHSPEED_CLK;
			sd_mmc_info->speed_class = HIGH_SPEED;
			break;

/*
		case 13 : 
			printk("switch to default speed for emmc\n");
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b90000, RESPONSE_R1, response, 0, 0, 1);
			if(ret)
				break;
			
			if (using_sdxc_controller)
			{				
				unsigned long sdxc_clk = 0;
				SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
				sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
				sdxc_clk_reg->clk_ctl_en = 0;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				sdxc_clk_reg->clk_div = CLK_DIV_25M; //sdxx
				sdxc_clk_reg->clk_en = 1;
				sdxc_clk_reg->clk_ctl_en = 1;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			}			
			sd_mmc_info->sdio_clk_unit = 1000/SD_MMC_TRANSFER_CLK;
			sd_mmc_info->speed_class = NORMAL_SPEED;
			break;

		case 14 : 
			printk("switch to high speed for emmc\n");
			ret = sd_send_cmd_hw(sd_mmc_info, MMC_SWITCH_FUNTION, 0x03b90100, RESPONSE_R1, response, 0, 0, 1);
			if(ret)
				break;
			
			if (using_sdxc_controller)
			{				
				unsigned long sdxc_clk = 0;
				SDXC_Clk_Reg_t *sdxc_clk_reg = (void *)&sdxc_clk;
				sdxc_clk = READ_CBUS_REG(SD_REG4_CLKC);
				sdxc_clk_reg->clk_ctl_en = 0;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
				sdxc_clk_reg->clk_div = CLK_DIV_HS_MMC; //sdxx
				sdxc_clk_reg->clk_en = 1;
				sdxc_clk_reg->clk_ctl_en = 1;
				WRITE_CBUS_REG(SD_REG4_CLKC, sdxc_clk);
			}
			sd_mmc_info->sdio_clk_unit = 1000/SD_MMC_TRANSFER_HIGHSPEED_CLK;
			sd_mmc_info->speed_class = HIGH_SPEED;
			break;
*/

		case 15 :
			printk("send csd_ext\n");
			{
				unsigned char *mmc_ext_csd_buf = sd_mmc_info->sd_mmc_buf;
				MMC_REG_EXT_CSD_t *mmc_ext_csd_reg;
				
				ret = sd_send_cmd_hw(sd_mmc_info,MMC_SEND_EXT_CSD, 0, RESPONSE_R1, response, mmc_ext_csd_buf, sizeof(MMC_REG_EXT_CSD_t), 1);
				mmc_ext_csd_reg = (MMC_REG_EXT_CSD_t *)mmc_ext_csd_buf;
				my_disp_csd_ext(mmc_ext_csd_reg);
			}
			break;

		case 17 : //SD_MMC_READ_SINGLE_BLOCK
		{	
			int i;
			for( i = 0; i < 512; i++)
				data_buf[i] = 0xdd;
			ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_READ_SINGLE_BLOCK, lba, RESPONSE_R1, response, data_buf, 512, 1);
			if (ret == SD_MMC_NO_ERROR)
			{
				printk("read data from lba 0x%x -- %u\n", lba, lba);
				for (i = 0; i < 512; i++)
				{
					printk("%02x", data_buf[i]);
					if ((i % 4) == 3)
						printk(" ");
					if ((i % 32) == 31)
						printk("\n");
				}
				printk("end of lba 0x%x -- %u\n", lba, lba);
			}
			break;
		}
		
		case 18 :	//SD_MMC_READ_MULTIPLE_BLOCK
		{	
			int i;
			for( i = 0; i < 512 * cnt; i++)
				data_buf[i] = 0xee;

			ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_READ_MULTIPLE_BLOCK, lba, RESPONSE_R1, response, data_buf, 512 * cnt, 1);
			if (ret == SD_MMC_NO_ERROR)
			{
	            ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
				printk("read %d sectors data from lba 0x%x -- %u\n", cnt, lba, lba);
				for (i = 0; i < 512 * cnt; i++)
				{
					printk("%02x", data_buf[i]);
					if ((i % 4) == 3)
						printk(" ");
					if ((i % 32) == 31)
						printk("\n");
					if ((i % 512) == 511)
						printk("\n");
				}
				printk("end of lba 0x%x -- %u\n", lba, lba);
			}
			/*
			ret = sd_read_multi_block_hw(sd_mmc_info, lba, cnt, data_buf);
			if (ret == SD_MMC_NO_ERROR)
			{
				printk("read %d sectors data from lba 0x%x -- %u\n", cnt, lba, lba);
				for (i = 0; i < 512 * cnt; i++)
				{
					printk("%02x", data_buf[i]);
					if ((i % 4) == 3)
						printk(" ");
					if ((i % 32) == 31)
						printk("\n");
					if ((i % 512) == 511)
						printk("\n");
				}
				printk("end of lba 0x%x -- %u\n", lba, lba);
			}
			*/
			break;
		}

		case 19 :	//SD_MMC_READ_MULTIPLE_BLOCK
		{	
			int i;
			int j = 0;

			memset(data_buf, 0xee, 512 * cnt);

			printk("read %d sectors data from lba 0x%x -- %u\n", cnt, lba, lba);
			while (j++ < wr_cnt) {
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_READ_MULTIPLE_BLOCK, lba, RESPONSE_R1, response, data_buf, 512 * cnt, 1);
				if (ret == SD_MMC_NO_ERROR) {
					i = 10;
		            ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
					while (ret != SD_MMC_NO_ERROR && i--) {
						ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
					}
					if (i != 10)
						printk("send stop retry %d times\n", 10 - i);

					if (ret)
						break;
				}
			}
			if (ret == SD_MMC_NO_ERROR) {
				for (i = 0; i < cnt; i++)
				{
					printk("%02x", data_buf[i * 512]);
					if ((i % 4) == 3)
						printk(" ");
					if ((i % 32) == 31)
						printk("\n");
					if ((i % 512) == 511)
						printk("\n");
				}
				printk("\nend of lba 0x%x -- %u\n", lba, lba);
			}
			
			/*
			ret = sd_read_multi_block_hw(sd_mmc_info, lba, cnt, data_buf);
			if (ret == SD_MMC_NO_ERROR)
			{
				printk("read %d sectors data from lba 0x%x -- %u\n", cnt, lba, lba);
				for (i = 0; i < 512 * cnt; i++)
				{
					printk("%02x", data_buf[i]);
					if ((i % 4) == 3)
						printk(" ");
					if ((i % 32) == 31)
						printk("\n");
					if ((i % 512) == 511)
						printk("\n");
				}
				printk("end of lba 0x%x -- %u\n", lba, lba);
			}
			*/
			break;
		}

		case 24 : //SD_MMC_WRITE_BLOCK
		{
			int i;
			for( i = 0; i < 512; i++)
				data_buf[i] = i + start_num;
			printk("write data 0x%x to lba 0x%x -- %u\n", start_num, lba, lba);
			ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_WRITE_BLOCK, lba, RESPONSE_R1, response, data_buf, 512, 1);
			break;
		}
		
		case 25 : //SD_MMC_WRITE_MULTIPLE_BLOCK
		{
			int i;

			for (i = 0; i < cnt; i++) {
				memset(data_buf + i * 512, start_num + i, 512);
			}
			printk("write %d sectors data 0x%x to lba 0x%x -- %u\n", cnt, start_num, lba, lba);
			ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_WRITE_MULTIPLE_BLOCK, lba, RESPONSE_R1, response, data_buf, 512 * cnt, 1);
			
			i = 10;
			if (ret == SD_MMC_NO_ERROR) {
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
				while (ret != SD_MMC_NO_ERROR && i--) {
					ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
				}
				printk("send stop retry %d times\n", 10 - i);
			}
			break;
		}

		case 26 : //SD_MMC_WRITE_MULTIPLE_BLOCK
		{
			int i;
			int j = 0;

			for (i = 0; i < cnt; i++) {
				memset(data_buf + i * 512, start_num + i, 512);
			}
			printk("write %d sectors data 0x%x to lba 0x%x -- %u\n", cnt, start_num, lba, lba);

			while (j++ < wr_cnt) {
				ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_WRITE_MULTIPLE_BLOCK, lba, RESPONSE_R1, response, data_buf, 512 * cnt, 1);
				
				i = 10;
				if (ret == SD_MMC_NO_ERROR) {
					ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
					while (ret != SD_MMC_NO_ERROR && i--) {
						ret = sd_send_cmd_hw(sd_mmc_info, SD_MMC_STOP_TRANSMISSION, 0, RESPONSE_R1B, response, NULL, 0, 0);
					}
					if (i != 10)
						printk("send stop retry %d times\n", 10 - i);
				}
				if (ret)
					break;
			}
			break;
		}

		case 27 :
			printk("change wr_cnt from %d to %d\n", wr_cnt, lba);
			wr_cnt = lba;
			break;

		default :
			printk("unknown cmd %d\n", cmd);
			ret = SD_ERROR_Reserved1;
			break;
	}
	
	using_sdxc_controller = 0;
	
	sd_gpio_enable(sd_mmc_info->io_pad_type);
	card_release_host(host);

	return ret;
}

static void my_disp_sdhc_regs(void)
{
	unsigned int value;
	const char *ptr;

	value = READ_CBUS_REG(SD_REG0_ARGU);
	printk("\nSD_REG0_ARGU=0x%08x -- %u\n", value, value);
	
	value = READ_CBUS_REG(SD_REG1_SEND);
	printk("SD_REG1_SEND=0x%08x -- %u\n", value, value);
	{
		SDXC_Send_Reg_t *send_reg = (SDXC_Send_Reg_t *)&value;
		printk("\t total_pack--%d \n", send_reg->total_pack);
		printk("\t data_stop--%d \n", send_reg->data_stop);
		printk("\t data_direction--%d(%s) \n", send_reg->data_direction, send_reg->data_direction ? "tx" : "rx");
		printk("\t response_no_crc--%d \n", send_reg->response_no_crc);
		printk("\t response_length--%d \n", send_reg->response_length);
		printk("\t command_has_data--%d \n", send_reg->command_has_data);
		printk("\t command_has_resp--%d \n", send_reg->command_has_resp);
		printk("\t command_index--0x%02x \n", send_reg->command_index);
	}

	value = READ_CBUS_REG(SD_REG2_CNTL);
	printk("SD_REG2_CNTL=0x%08x -- %u\n", value, value);
	{
		SDXC_Ctrl_Reg_t *ctrl_reg = (SDXC_Ctrl_Reg_t *)&value;
		printk("\t reserved2--%d \n", ctrl_reg->reserved2);
		printk("\t endian--%d \n", ctrl_reg->endian);
		printk("\t rc_period--%d \n", ctrl_reg->rc_period);
		printk("\t rx_timeout--%d \n", ctrl_reg->rx_timeout);
		printk("\t pack_len--%d \n", ctrl_reg->pack_len);
		printk("\t reserved1--%d \n", ctrl_reg->reserved1);
		printk("\t ddr_mode--%d \n", ctrl_reg->ddr_mode);
		if (ctrl_reg->dat_width == 0)
			ptr = "1bit";
		else if (ctrl_reg->dat_width == 1)
			ptr = "4bits";
		else if (ctrl_reg->dat_width == 2)
			ptr = "8bits";
		else
			ptr = "reserved";
		printk("\t dat_width--%d(%s) \n", ctrl_reg->dat_width, ptr);
	}

	value = READ_CBUS_REG(SD_REG3_STAT);
	printk("SD_REG3_STAT=0x%08x -- %u\n", value, value);
	{
		SDXC_Status_Reg_t *status_reg = (SDXC_Status_Reg_t *)&value;
		printk("\t reserved3--%d \n", status_reg->reserved3);
		printk("\t dat_7_4--%d \n", status_reg->dat_7_4);
		printk("\t tx_count--%d \n", status_reg->tx_count);
		printk("\t rx_count--%d \n", status_reg->rx_count);
		printk("\t reserved2--%d \n", status_reg->reserved2);
		printk("\t reserved1--%d \n", status_reg->reserved1);
		printk("\t cmd_state--%d \n", status_reg->cmd_state);
		printk("\t dat_3_0--%d \n", status_reg->dat_3_0);
		printk("\t busy--%d \n", status_reg->busy);
	}

	value = READ_CBUS_REG(SD_REG4_CLKC);
	printk("SD_REG4_CLKC=0x%08x -- %u\n", value, value);
	{
		SDXC_Clk_Reg_t *clk_reg = (SDXC_Clk_Reg_t *)&value;
		printk("\t reserved1--%d \n", clk_reg->reserved1);
		printk("\t clk_jic_control--%d \n", clk_reg->clk_jic_control);
		printk("\t clk_ctl_en--%d \n", clk_reg->clk_ctl_en);
		printk("\t rx_clk_feedback_en--%d \n", clk_reg->rx_clk_feedback_en);
		printk("\t rx_clk_phase_sel--%d \n", clk_reg->rx_clk_phase_sel);
		printk("\t clk_en--%d \n", clk_reg->clk_en);
		switch (clk_reg->clk_in_sel) {
			case  0: 
				ptr = "osc";
				break;

			case  1: 
				ptr = "sys pll";
				break;
			
			case  2: 
				ptr = "misc pll";
				break;
			
			case  3: 
				ptr = "ddr pll";
				break;
			
			case  4: 
				ptr = "aud pll";
				break;
			
			case  5: 
				ptr = "vid pll";
				break;
			
			case  6: 
				ptr = "vid2 pll";
				break;
			
			case  7: 
				ptr = "sys pll/2";
				break;
			default :
				ptr = "reserved";
				break;
			
		}
		printk("\t clk_in_sel--%d(%s) \n", clk_reg->clk_in_sel, ptr);
		printk("\t clk_div--%d \n", clk_reg->clk_div);
	}

	value = READ_CBUS_REG(SD_REG5_ADDR);
	printk("SD_REG5_ADDR=0x%08x -- %u\n", value, value);

	value = READ_CBUS_REG(SD_REG6_PDMA);
	printk("SD_REG6_PDMA=0x%08x -- %u\n", value, value);
	{
		SDXC_PDMA_Reg_t *pdma_reg = (SDXC_PDMA_Reg_t *)&value;
		printk("\t reserved1--%d \n", pdma_reg->reserved1);
		printk("\t rx_manual_flush--%d \n", pdma_reg->rx_manual_flush);
		printk("\t tx_threshold--%d \n", pdma_reg->tx_threshold);
		printk("\t rx_threshold--%d \n", pdma_reg->rx_threshold);
		printk("\t rd_burst--%d \n", pdma_reg->rd_burst);
		printk("\t wr_burst--%d \n", pdma_reg->wr_burst);
		printk("\t dma_urgent--%d \n", pdma_reg->dma_urgent);
		printk("\t pio_rd_resp--%d \n", pdma_reg->pio_rd_resp);
		printk("\t dma_mode--%d \n", pdma_reg->dma_mode);
	}

	value = READ_CBUS_REG(SD_REG7_MISC);
	printk("SD_REG7_MISC=0x%08x -- %u\n", value, value);
	{
		SDXC_Misc_Reg_t *misc_reg = (SDXC_Misc_Reg_t *)&value;
		printk("\t reserved1--%d \n", misc_reg->reserved1);
		printk("\t manual_stop--%d \n", misc_reg->manual_stop);
		printk("\t thread_id--%d \n", misc_reg->thread_id);
		printk("\t burst_num--%d \n", misc_reg->burst_num);
		printk("\t tx_empty_threshold--%d \n", misc_reg->tx_empty_threshold);
		printk("\t rx_full_threshold--%d \n", misc_reg->rx_full_threshold);
		printk("\t dat_line_delay--%d \n", misc_reg->dat_line_delay);
		printk("\t cmd_line_delay--%d \n", misc_reg->cmd_line_delay);
	}

	value = READ_CBUS_REG(SD_REG8_DATA);
	printk("SD_REG8_DATA=0x%08x -- %u\n", value, value);

	value = READ_CBUS_REG(SD_REG9_ICTL);
	printk("SD_REG9_ICTL=0x%08x -- %u\n", value, value);
	{
		SDXC_Ictl_Reg_t *ictl_reg = (SDXC_Ictl_Reg_t *)&value;
		printk("\t reserved2--%d \n", ictl_reg->reserved2);
		printk("\t sdio_dat1_mask_delay--%d \n", ictl_reg->sdio_dat1_mask_delay);
		printk("\t reserved1--%d \n", ictl_reg->reserved1);
		
		printk("\t additional_sdio_dat1_int_en--%d \n", ictl_reg->additional_sdio_dat1_int_en);		
		printk("\t tx_fifo_empty_int_en--%d \n", ictl_reg->tx_fifo_empty_int_en);
		printk("\t rx_fifo_full_int_en--%d \n", ictl_reg->rx_fifo_full_int_en);
		
		printk("\t dma_done_int_en--%d \n", ictl_reg->dma_done_int_en);
		printk("\t sdio_dat1_int_en--%d \n", ictl_reg->sdio_dat1_int_en);		
		printk("\t tx_fifo_int_en--%d \n", ictl_reg->tx_fifo_int_en);
		printk("\t rx_fifo_int_en--%d \n", ictl_reg->rx_fifo_int_en);
		
		printk("\t data_complete_int_en--%d \n", ictl_reg->data_complete_int_en);
		printk("\t pack_crc_err_int_en--%d \n", ictl_reg->pack_crc_err_int_en);		
		printk("\t pack_timeout_int_en--%d \n", ictl_reg->pack_timeout_int_en);
		printk("\t pack_complete_int_en--%d \n", ictl_reg->pack_complete_int_en);
		
		printk("\t dat0_ready_int_en--%d \n", ictl_reg->dat0_ready_int_en);
		printk("\t res_crc_err_int_en--%d \n", ictl_reg->res_crc_err_int_en);
		printk("\t res_timeout_int_en--%d \n", ictl_reg->res_timeout_int_en);
		printk("\t res_ok_int_en--%d \n", ictl_reg->res_ok_int_en);
	}
	
	value = READ_CBUS_REG(SD_REGA_ISTA);
	printk("SD_REGA_ISTA=0x%08x -- %u\n", value, value);
	{
		SDXC_Ista_Reg_t *ista_reg = (SDXC_Ista_Reg_t *)&value;
		printk("\t reserved1--%d \n", ista_reg->reserved1);
		
		printk("\t additional_sdio_dat1_int--%d \n", ista_reg->additional_sdio_dat1_int);
		printk("\t tx_fifo_empty_int--%d \n", ista_reg->tx_fifo_empty_int);
		printk("\t rx_fifo_full_int--%d \n", ista_reg->rx_fifo_full_int);
		
		printk("\t dma_done_int--%d \n", ista_reg->dma_done_int);
		printk("\t sdio_dat1_int*--%d \n", ista_reg->sdio_dat1_int);
		printk("\t tx_fifo_int--%d \n", ista_reg->tx_fifo_int);
		printk("\t rx_fifo_int--%d \n", ista_reg->rx_fifo_int);
		
		printk("\t data_complete_int*--%d \n", ista_reg->data_complete_int);
		printk("\t pack_crc_err_int--%d \n", ista_reg->pack_crc_err_int);
		printk("\t pack_timeout_int--%d \n", ista_reg->pack_timeout_int);
		printk("\t pack_complete_int--%d \n", ista_reg->pack_complete_int);
		
		printk("\t dat0_ready_int--%d \n", ista_reg->dat0_ready_int);
		printk("\t res_crc_err_int--%d \n", ista_reg->res_crc_err_int);
		printk("\t res_timeout_int--%d \n", ista_reg->res_timeout_int);
		printk("\t res_ok_int*--%d \n", ista_reg->res_ok_int);
	}
	
	value = READ_CBUS_REG(SD_REGB_SRST);
	printk("SD_REGB_SRST=0x%08x -- %u\n", value, value);	
	{
		SDXC_Srst_Reg_t *srst_reg = (SDXC_Srst_Reg_t *)&value;
		printk("\t reserved1--%d \n", srst_reg->reserved1);
		printk("\t dma_srst--%d \n", srst_reg->dma_srst);
		printk("\t tx_dphy_srst--%d \n", srst_reg->tx_dphy_srst);
		printk("\t rx_dphy_srst--%d \n", srst_reg->rx_dphy_srst);
		printk("\t tx_fifo_srst--%d \n", srst_reg->tx_fifo_srst);
		printk("\t rx_fifo_srst--%d \n", srst_reg->rx_fifo_srst);
		printk("\t main_ctrl_srst--%d \n\n", srst_reg->main_ctrl_srst);
	}
}

static void my_write_reg(char *buf)
{
	unsigned int addr;
	unsigned int value;

	if (!((buf[0] >= '0' && buf[0] <= '9') || buf[0] == 'A' || buf[0] == 'B'))
	{
		printk("wrong addr\n");
		return;
	}
	
	if (buf[0] >= '0' && buf[0] <= '9')
		addr = buf[0] - '0' + 0x2380;	//0x2380  is the sdhc controller's base address
	else
		addr = buf[0] - 'A' + 10 + 0x2380;
	value = my_parse_num(buf + 2, NULL);
	printk("cmd5 : set addr(0x%x) to 0x%08x -- %d\n", addr, value, value);

	switch (buf[0]) {
		case '0' : WRITE_CBUS_REG(SD_REG0_ARGU, value); break;
		case '1' : WRITE_CBUS_REG(SD_REG1_SEND, value); break;
		case '2' : WRITE_CBUS_REG(SD_REG2_CNTL, value); break;
		case '3' : WRITE_CBUS_REG(SD_REG3_STAT, value); break;
		case '4' : WRITE_CBUS_REG(SD_REG4_CLKC, value); break;
		case '5' : WRITE_CBUS_REG(SD_REG5_ADDR, value); break;
		case '6' : WRITE_CBUS_REG(SD_REG6_PDMA, value); break;
		case '7' : WRITE_CBUS_REG(SD_REG7_MISC, value); break;
		case '8' : WRITE_CBUS_REG(SD_REG8_DATA, value); break;
		case '9' : WRITE_CBUS_REG(SD_REG9_ICTL, value); break;
		case 'A' : WRITE_CBUS_REG(SD_REGA_ISTA, value); break;
		case 'B' : WRITE_CBUS_REG(SD_REGB_SRST, value); break;
		default : printk("wrong reg num\n"); break;
	}

/*	
	if (buf[0] >= '0' && buf[0] <= '9')
		addr = buf[0] - '0' + 0x2380;	//0x2380  is the sdhc controller's base address
	else
		addr = buf[0] - 'A' + 10 + 0x2380;
	value = my_parse_num(buf + 2, NULL);
	printk("cmd5 : set addr(0x%x) to 0x%08x -- %d\n", addr, value, value);
	WRITE_CBUS_REG(addr, value); 
*/
}

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <asm/uaccess.h>

static struct proc_dir_entry *my_dir,*my_file;

extern struct card_host * the_card_host;

//echo 10 > /proc/sdxc/cmd

///sdxc_send_cmd_hw()
static ssize_t my_write( struct file *file,
			  const char __user *buffer,
			  size_t len,
			  loff_t *offset )
{
	char command;
	
	char *tmp_buf = kmalloc(len + 1, GFP_KERNEL);
	if (tmp_buf == NULL)
		return -EINVAL;
	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;
	tmp_buf[len] = '\0';

	command = tmp_buf[0];

	switch (command)
	{
		case '0' :
			my_test(&tmp_buf[2]);
			break;
			
		case '1' : 	//setup : 1 0 or 1 1
			if (sdxc_card != NULL)
			{
				printk("card(%p) exist, will be freed first\n", sdxc_card);
				kfree(sdxc_card);
				sdxc_card = NULL;
			}
			
			sdxc_card = sdxc_card_setup(the_card_host, my_parse_num(&tmp_buf[2], NULL));
			
			if (sdxc_card)
				printk("card(%p) setup success\n", sdxc_card);
			else
				printk("card setup fail\n");
			break;

		case '2' : //check present : 2
			if (sdxc_card == NULL)
			{
				printk("send me command 1 first\n");
			}
			else
			{
				if (sdxc_card_present(sdxc_card))
					printk("card is present\n");
				else
					printk("card not present\n");
			}
			break;

		case '3' :	//identify : 36	//6 is steps (1~6)
			if (sdxc_card == NULL)
			{
				printk("send me command 1 first\n");
			}
			else
			{
				int steps;
				if (tmp_buf[1] >= '1' && tmp_buf[1] <= '9')
					steps = tmp_buf[1] - '0';
				else
					steps = 2;
					
				if (sdxc_card_identify(sdxc_card, steps) == 0)	///sdxc_identify_verbose()
					printk("steps=%d success\n", steps);
				else
					printk("steps=%d fail\n", steps);
			}
			break;

		case '4' :	//disp regs : 4
			my_disp_sdhc_regs();
			break;

		case '5' :	//write reg : 5 2 37	//2nd  is reg num (0~9,A,B), 3rd is value in decimal
			my_write_reg(&tmp_buf[2]);
			break;

		case '7' :	//send single cmd : 7 0		//2nd is cmd idx (0, 8, 41,   2, 3, 10, 9, 7, 51, 6)	//acmd41, acmd51, acmd6
					//other cmds : 55, 42, 46
			{
			unsigned int cmd;
			int ret;

			cmd = my_parse_num(&tmp_buf[2], NULL);
			ret = sdxc_card_cmd(sdxc_card, cmd);
			if (ret == 0)
				printk("send cmd%d success\n", cmd);
			else
				printk("send cmd%d fail with %s\n", cmd, sd_error_to_string(ret));
			break;
			}

		case '8' :	//send all identify cmds except acmd6 : 8
			{
			int ret = 0;
			unsigned int cmds[] = {0, 8, 42, 2, 3, 10, 9, 7, 51};
			int count = sizeof(cmds) / sizeof(cmds[0]);
			int i = 0;
			while (ret == 0 && i < count)
			{
				ret = sdxc_card_cmd(sdxc_card, cmds[i]);
				i++;
			}
			if (ret == 0)
				printk("card identify success\n");
			else
				printk("card identify fail\n");
			break;
			}

		case '9' :	//send rw cmds : 9 17 6550550		//2nd is cmd (17, 18, 24, 25),  3rd is lba, 4th is cnt
					//two other funcs(20, 21) : 9 20 wr_start_num,  9 21 rw_delay_us
			{
				int ret;
				unsigned int cmd;
				unsigned int lba;
				unsigned int cnt;
				char *pcur;

				cmd = my_parse_num(&tmp_buf[2], &pcur);
				lba = my_parse_num(pcur, &pcur);
				cnt = my_parse_num(pcur, &pcur);
				ret = sdxc_card_rw(sdxc_card, cmd, lba, cnt);
				if (ret == 0)
					printk("card rw success\n");
				else
					printk("card rw fail : %s\n", sd_error_to_string(ret));
					
				break;
			}

		default :
			printk("unknow command : %c\n", command);
			break;
	}
	
	kfree(tmp_buf);
	return len;
}

static ssize_t my_read( struct file *file,
			  char __user *buffer,
			  size_t len,
			  loff_t *offset )
{
	int ret;

	printk("params : buffer=%p, len=%d, offset=%d\n", buffer, len, (unsigned int)*offset);

	if (*offset == 0)
	{
		ret = sprintf(buffer, "sdxc_card(%p)\n", sdxc_card);
		*offset = ret;
	}
	else
	{
		ret = 0;
	}
	
	return ret;	
}

static int my_open( struct inode *inode,
				 struct file *file ) 
{
	return 0;
}

static int my_close( struct inode *inode, struct file *file )
{
	return 0;
}

static const struct file_operations my_ops = {
	.owner = THIS_MODULE,
	.read = my_read,
	.write = my_write,
	.open = my_open,
	.release = my_close
};

#define MYDIR "sdxc"
#define MYFILE "cmd"

int my_init(void)
{
  my_dir = (struct proc_dir_entry * )proc_mkdir(MYDIR,NULL);
  if(my_dir == NULL){
    printk("mkdir fail\n");
     return -1;
  }

  my_file = (struct proc_dir_entry * )proc_create_data(MYFILE,0777,my_dir, &my_ops, NULL);
  if(my_file == NULL){
     remove_proc_entry(MYDIR, NULL);
     my_dir = NULL;
     printk("mkfile fail\n");
     return -1;

  }  
  return 0;
}

void my_cleanup(void)
{
	if (my_file)
		remove_proc_entry(MYFILE,my_dir);
	if (my_dir)
		remove_proc_entry(MYDIR, NULL);
}

module_init(my_init);
module_exit(my_cleanup);

#endif /*SDXC_DEBUG*/
