#include "xd_port.h"
//#include "sm_port.h"
#include "xd_misc.h"
#include "xd_sm.h"

//Global struct variable, to hold all card information need to operate card
static XD_SM_Card_Info_t _xd_sm_info = {CARD_TYPE_NONE,         //card_type
									    0,                      //blk_len
									    0,                      //blk_nums
									    0,                      //read_only_flag
									    0,                      //xd_inited_flag
									    0,                      //xd_removed_flag
									    0,						//xd_init_retry
									    0,                      //sm_inited_flag
									    0,                      //sm_removed_flag
									    0,						//sm_init_retry
									    {0},						//raw_cid
									    NULL,					//xd_power
									    NULL,					//xd_get_ins
									    NULL,					//xd_io_release
									    NULL,					//sm_power
									    NULL,					//sm_get_ins
									    NULL,					//sm_get_wp
									    NULL					//sm_io_release
									   };

#ifndef XD_SM_ALLOC_MEMORY
static XD_SM_Card_Buffer_t _xd_sm_buf;
#endif

XD_SM_Card_Info_t *xd_sm_info = &_xd_sm_info;
//XD_SM_Card_Buffer_t *xd_sm_buf = &_xd_sm_buf;

#ifdef XD_SM_ALLOC_MEMORY
XD_SM_Card_Buffer_t *xd_sm_buf = NULL;
#else
XD_SM_Card_Buffer_t *xd_sm_buf = &_xd_sm_buf;
#endif

extern unsigned char xd_reset_flag_read, xd_reset_flag_write;

static char * xd_sm_error_string[] = {
	"XD_SM_NO_ERROR",
	"ERROR_DRIVER_FAILURE",
	"ERROR_BUSY",
	"ERROR_TIMEOUT",
	"ERROR_PARAMETER",
	"ERROR_ECC",
	"ERROR_BLOCK_ADDRESS",
	"ERROR_PHYSICAL_FORMAT",
	"ERROR_CONVERTSION_TABLE",
	"ERROR_UNSUPPORTED_CAPACITY",
	"ERROR_DEVICE_ID",
	"ERROR_CARD_ID",
	"ERROR_LOGICAL_FORMAT",
	"ERROR_NO_FREE_BLOCK",
	"ERROR_BLOCK_ERASE",
	"ERROR_COPY_PAGE",
	"ERROR_PAGE_PROGRAM",
	"ERROR_DATA",
	"ERROR_CARD_TYPE",
	"ERROR_NO_MEMORY"
};

unsigned char CIS_DATA_0_9[] = {0x01, 0x03, 0xD9, 0x01, 0xFF, 0x18, 0x02, 0xDF, 0x01, 0x20};

char xd_sm_capacity_1MB[] = {"1MB"};
char xd_sm_capacity_2MB[] = {"2MB"};
char xd_sm_capacity_4MB[] = {"4MB"};
char xd_sm_capacity_8MB[] = {"8MB"};
char xd_sm_capacity_16MB[] = {"16MB"};
char xd_sm_capacity_32MB[] = {"32MB"};
char xd_sm_capacity_64MB[] = {"64MB"};
char xd_sm_capacity_128MB[] = {"128MB"};
char xd_sm_capacity_256MB[] = {"256MB"};
char xd_sm_capacity_512MB[] = {"512MB"};
char xd_sm_capacity_1GB[] = {"1GB"};
char xd_sm_capacity_2GB[] = {"2GB"};

static xd_sm_io_config_t xd_sm_io_config = NULL;
static xd_sm_cmd_input_cycle_t xd_sm_cmd_input_cycle = NULL;
static xd_sm_addr_input_cycle_t xd_sm_addr_input_cycle = NULL;
static xd_sm_data_input_cycle_t xd_sm_data_input_cycle = NULL;
static xd_sm_serial_read_cycle_t xd_sm_serial_read_cycle = NULL;
static xd_sm_test_ready_t xd_sm_test_ready = NULL;

unsigned long xd_sm_total_zones = 0;
unsigned long xd_sm_totoal_physical_blks = 0;
unsigned long xd_sm_totoal_logical_blks = 0;
unsigned short xd_sm_physical_blks_perzone = 0;
unsigned short xd_sm_logical_blks_perzone = 0;
unsigned short xd_sm_pages_per_blk = 0;
unsigned short xd_sm_page_size = 0;
unsigned short xd_sm_redundant_size = 0;
unsigned short xd_sm_actually_zones = MAX_SUPPORTED_ZONES;

//All local function definitions, only used in this .C file

char * xd_sm_error_to_string(int errcode);

void xd_sm_function_init(void);
int xd_sm_wait_ready(unsigned long timeout);
int xd_sm_wait_ready_ms(unsigned long timeout);

int xd_sm_status_read(unsigned char cmd, unsigned char *status);
int xd_sm_read_cycle1(unsigned char column_addr, unsigned long page_addr, unsigned char *data_buf, unsigned long data_cnt, unsigned char *redundant_buf);
int xd_sm_read_cycle2(unsigned char column_addr, unsigned long page_addr, unsigned char *data_buf, unsigned long data_cnt, unsigned char *redundant_buf);
int xd_sm_read_cycle3(unsigned char column_addr, unsigned long page_addr, unsigned char *redundant_buf, unsigned long redundant_cnt);
int xd_sm_auto_page_program(unsigned long page_addr, unsigned char *data_buf, unsigned char *redundant_buf);
int xd_sm_auto_block_erase(unsigned long page_addr);
int xd_sm_reset(void);
int xd_sm_id_read(unsigned char cmd, unsigned char *data_buf);

int xd_sm_check_byte_bits(unsigned char value);
int xd_sm_check_page_ecc(unsigned char *page_buf, unsigned char *redundant_buf);
int xd_sm_check_unit_ecc(unsigned char *data_buf, unsigned char *data_ecc);
int xd_sm_format_redundant_area(unsigned char *page_buf, unsigned char *redundant_buf, unsigned short block_addr);

int xd_sm_copy_page(unsigned short zone_no, unsigned short block_src, unsigned short page_src, unsigned short block_dst, unsigned short page_dst);

void xd_sm_set_block_free(unsigned short zone, unsigned short block);
void xd_sm_clear_block_free(unsigned short zone, unsigned short block);
unsigned short xd_sm_find_free_block(unsigned short zone);
void xd_sm_convertion_table_init(void);
int xd_sm_set_defective_block(unsigned short zone, unsigned short block);
int xd_sm_check_back_block(unsigned short zone, unsigned short block);
int xd_sm_search_free_block(void);

int xd_sm_check_block_address_parity(unsigned short block_addr);
int xd_sm_check_block_address_range(unsigned short block_addr);
unsigned short xd_sm_parse_logical_address(unsigned short block_addr);
unsigned short xd_sm_trans_logical_address(unsigned short logical_addr);
int xd_sm_check_erased_block(unsigned char *redundant_buf);
int xd_sm_check_defective_block(unsigned char *redundant_buf);
int xd_sm_block_address_search(unsigned char *redundant_buf, unsigned short *block_addr);
int xd_sm_card_capacity_determin(void);
int xd_sm_card_indentification(void);
int xd_sm_create_logical_physical_table(void);
int xd_sm_logical_format_determin(void);
int xd_sm_physical_format_determin(void);
int xd_sm_reconstruct_logical_physical_table(void);
int xd_sm_erase_all_blocks(void);
int xd_sm_check_data_consistency(void);

void xd_sm_staff_init(void);
int xd_sm_cmd_test(void);

//Return the string buf address of specific errcode
char * xd_sm_error_to_string(int errcode)
{
	return xd_sm_error_string[errcode];
}

void xd_sm_function_init(void)
{
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_sm_io_config = xd_io_config;
		xd_sm_cmd_input_cycle = xd_cmd_input_cycle;
		xd_sm_addr_input_cycle = xd_addr_input_cycle;
		xd_sm_data_input_cycle = xd_data_input_cycle;
		xd_sm_serial_read_cycle = xd_serial_read_cycle;
		xd_sm_test_ready = xd_test_ready;
		
		xd_gpio_enable();
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		xd_sm_io_config = sm_io_config;
		xd_sm_cmd_input_cycle = sm_cmd_input_cycle;
		xd_sm_addr_input_cycle = sm_addr_input_cycle;
		xd_sm_data_input_cycle = sm_data_input_cycle;
		xd_sm_serial_read_cycle = sm_serial_read_cycle;
		xd_sm_test_ready = sm_test_ready;
		
		sm_gpio_enable();
	}
#endif
}

int xd_sm_wait_ready(unsigned long timeout)
{
	xd_sm_start_timer(timeout);
	
	do
	{
		if(xd_sm_test_ready() == XD_SM_STATUS_READY)
			return XD_SM_NO_ERROR;
			
		xd_sm_check_timer();
	} while(!xd_sm_check_timeout());
	
	return XD_SM_ERROR_TIMEOUT;
}

int xd_sm_wait_ready_ms(unsigned long timeout)
{
	unsigned long i, timecnt;
	
	timecnt = timeout / TIMER_1MS;
	for(i=0; i<timecnt; i++)
	{
		if(xd_sm_test_ready() == XD_SM_STATUS_READY)
			return XD_SM_NO_ERROR;
		
		xd_sm_delay_ms(1);
	}
	
	return XD_SM_ERROR_TIMEOUT;
}

int xd_sm_status_read(unsigned char cmd, unsigned char *status)
{
	if((cmd != XD_SM_STATUS_READ1) && (cmd != XD_STATUS_READ2))
		return XD_SM_ERROR_PARAMETER;
		
	xd_sm_cmd_input_cycle(cmd, XD_SM_WRITE_DISABLED);

#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_set_ce_enable();
		xd_set_dat0_7_input();
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		sm_set_ce_enable();
		sm_set_dat0_7_input();
	}
#endif
	
	xd_sm_delay_20ns();		// Tcls = 20ns
	
	xd_sm_serial_read_cycle(status, 1, NULL, 0);

#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
		xd_set_ce_disable();
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
		sm_set_ce_disable();
#endif
		
	return XD_SM_NO_ERROR;
}

int xd_sm_read_cycle1(unsigned char column_addr, unsigned long page_addr, unsigned char *data_buf, unsigned long data_cnt, unsigned char *redundant_buf)
{
	int err;
	unsigned long data_nums = 0, data_offset = 0, read_cnt;
	
	xd_sm_cmd_input_cycle(XD_SM_READ1, XD_SM_WRITE_DISABLED);

	xd_sm_addr_input_cycle((page_addr << 8)|column_addr, xd_sm_buf->addr_cycles);

#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_set_dat0_7_input();
		xd_sm_delay_60ns();		// Tar2 = 50ns
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		sm_set_dat0_7_input();
		xd_sm_delay_100ns(2);	// Tar2 = 150ns
	}
#endif

	read_cnt = xd_sm_page_size - column_addr;
	while(data_nums < data_cnt)
	{
		err = xd_sm_wait_ready(BUSY_TIME_R);
		if(err)
		{
#ifdef XD_CARD_SUPPORTED
			if(xd_sm_info->card_type == CARD_TYPE_XD)
				xd_set_ce_disable();
#endif
#ifdef SM_CARD_SUPPORTED
			if(xd_sm_info->card_type == CARD_TYPE_SM)
				sm_set_ce_disable();
#endif
				
			return err;
		}

		xd_sm_delay_20ns();		// Trr = 20ns

#ifdef XD_SM_ECC_CHECK
		xd_sm_serial_read_cycle(xd_sm_buf->page_buf, read_cnt, redundant_buf, xd_sm_redundant_size);
		
		err = xd_sm_check_page_ecc(xd_sm_buf->page_buf, redundant_buf);
		if(err)
		{
			if(err == ECC_ERROR_CORRECTED)
			{
			#ifdef  XD_SM_DEBUG
				Debug_Printf("Data ECC error occured, but corrected!\n");
			#endif
			}
			else
			{
			#ifdef  XD_SM_DEBUG
				err = XD_SM_ERROR_ECC;
				Debug_Printf("#%s error occured in xd_sm_read_data()\n", xd_sm_error_to_string(err));
			#endif
			}
		}
		
		#ifdef AMLOGIC_CHIP_SUPPORT
		if((unsigned long)data_buf == 0x3400000)
		{
			for(int i=0; i<read_cnt; i++)
			{
				WRITE_BYTE_TO_FIFO(xd_sm_buf->page_buf[i]);
				xd_sm_delay_100ns(1);
			}
		}
		else
		#endif
		{
			memcpy(data_buf+data_offset, xd_sm_buf->page_buf, read_cnt);
		}
#else
		xd_sm_serial_read_cycle(data_buf+data_offset, read_cnt, redundant_buf, xd_sm_redundant_size);
#endif
		//Improve stability for Fujufilm 512MB and above
		xd_sm_delay_us(5);

#ifdef AMLOGIC_CHIP_SUPPORT
		data_offset += ((unsigned long)data_buf == 0x3400000 ? 0 : read_cnt);
#else
		data_offset += read_cnt;
#endif

		redundant_buf += xd_sm_redundant_size;
		
		data_nums += read_cnt;
		read_cnt = xd_sm_page_size;
	}
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_set_ce_disable();
		xd_sm_delay_100ns(1);		// Tceh = 100ns
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		sm_set_ce_disable();
		xd_sm_delay_100ns(3);		// Tceh = 250ns
	}
#endif

	err = xd_sm_wait_ready(BUSY_TIME_TCRY);
	
	return err;
}

int xd_sm_read_cycle2(unsigned char column_addr, unsigned long page_addr, unsigned char *data_buf, unsigned long data_cnt, unsigned char *redundant_buf)
{
	int err;
	unsigned long data_nums = 0, data_offset = 0, read_cnt;
	
	if(xd_sm_page_size != XD_SM_SECTOR_SIZE)
		return XD_SM_ERROR_PARAMETER;
		
	xd_sm_cmd_input_cycle(XD_SM_READ2, XD_SM_WRITE_DISABLED);
	
	xd_sm_addr_input_cycle((page_addr << 8)|column_addr, xd_sm_buf->addr_cycles);
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_set_dat0_7_input();
		xd_sm_delay_60ns();		// Tar2 = 50ns
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		sm_set_dat0_7_input();
		xd_sm_delay_100ns(2);	// Tar2 = 150ns
	}
#endif
	
	read_cnt = xd_sm_page_size - (XD_SM_SECTOR_SIZE/2 + column_addr);
	while(data_nums < data_cnt)
	{
		err = xd_sm_wait_ready(XD_BUSY_TIME_R);
		if(err)
		{
#ifdef XD_CARD_SUPPORTED
			if(xd_sm_info->card_type == CARD_TYPE_XD)
				xd_set_ce_disable();
#endif
#ifdef SM_CARD_SUPPORTED
			if(xd_sm_info->card_type == CARD_TYPE_SM)
				sm_set_ce_disable();
#endif
				
			return err;
		}
		
		xd_sm_delay_20ns();		// Trr = 20ns

#ifdef XD_SM_ECC_CHECK
		xd_sm_serial_read_cycle(xd_sm_buf->page_buf, read_cnt, redundant_buf, xd_sm_redundant_size);
		
		err = xd_sm_check_page_ecc(xd_sm_buf->page_buf, redundant_buf);
		if(err)
		{
			if(err == ECC_ERROR_CORRECTED)
			{
			#ifdef  XD_SM_DEBUG
				Debug_Printf("Data ECC error occured, but corrected!\n");
			#endif
			}
			else
			{
			#ifdef  XD_SM_DEBUG
				err = XD_SM_ERROR_ECC;
				Debug_Printf("#%s error occured in xd_sm_read_data()\n", xd_sm_error_to_string(err));
			#endif
			}
		}
		
		#ifdef AMLOGIC_CHIP_SUPPORT
		if((unsigned long)data_buf == 0x3400000)
		{
			for(int i=0; i<read_cnt; i++)
			{
				WRITE_BYTE_TO_FIFO(xd_sm_buf->page_buf[i]);
				xd_sm_delay_100ns(1);
			}
		}
		else
		#endif
		{
			memcpy(data_buf+data_offset, xd_sm_buf->page_buf, read_cnt);
		}
#else		
		xd_sm_serial_read_cycle(data_buf+data_offset, read_cnt, redundant_buf, xd_sm_redundant_size);
#endif
		
#ifdef AMLOGIC_CHIP_SUPPORT
		data_offset += ((unsigned long)data_buf == 0x3400000 ? 0 : read_cnt);
#else
		data_offset += read_cnt;
#endif

		redundant_buf += xd_sm_redundant_size;
		
		data_nums += read_cnt;
		read_cnt = xd_sm_page_size;
	}
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_set_ce_disable();
		xd_sm_delay_100ns(1);		// Tceh = 100ns
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		sm_set_ce_disable();
		xd_sm_delay_100ns(3);		// Tceh = 250ns
	}
#endif
	
	err = xd_sm_wait_ready(BUSY_TIME_TCRY);
	
	return err;
}

int xd_sm_read_cycle3(unsigned char column_addr, unsigned long page_addr, unsigned char *redundant_buf, unsigned long redundant_cnt)
{
	int err;
	unsigned long redundant_offset = column_addr;
	unsigned long redundant_nums = 0;
	
	xd_sm_cmd_input_cycle(XD_SM_READ3, XD_SM_WRITE_DISABLED);
	
	xd_sm_addr_input_cycle((page_addr << 8)|column_addr, xd_sm_buf->addr_cycles);
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_set_dat0_7_input();
		xd_sm_delay_60ns();		// Tar2 = 50ns
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		sm_set_dat0_7_input();
		xd_sm_delay_100ns(2);	// Tar2 = 150ns
	}
#endif
	
	while(redundant_nums < redundant_cnt)
	{
		err = xd_sm_wait_ready(BUSY_TIME_R);
		if(err)
		{
#ifdef XD_CARD_SUPPORTED
			if(xd_sm_info->card_type == CARD_TYPE_XD)
				xd_set_ce_disable();
#endif
#ifdef SM_CARD_SUPPORTED
			if(xd_sm_info->card_type == CARD_TYPE_SM)
				sm_set_ce_disable();
#endif
				
			return err;
		}
		
		xd_sm_delay_20ns();		// Trr = 20ns
		
		xd_sm_serial_read_cycle(NULL, 0, redundant_buf, xd_sm_redundant_size-redundant_offset);
		redundant_buf += (xd_sm_redundant_size - redundant_offset);
		
		redundant_nums += (xd_sm_redundant_size - redundant_offset);
		redundant_offset = 0;
	}
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_set_ce_disable();
		xd_sm_delay_100ns(1);		// Tceh = 100ns
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		sm_set_ce_disable();
		xd_sm_delay_100ns(3);		// Tceh = 250ns
	}
#endif
	
	err = xd_sm_wait_ready(BUSY_TIME_TCRY);
	
	return err;
}

int xd_sm_auto_page_program(unsigned long page_addr, unsigned char *data_buf, unsigned char *redundant_buf)
{
	int err;
	unsigned char column_addr = 0;
	XD_SM_Status1_t status1;
		
	xd_sm_cmd_input_cycle(XD_SM_SERIAL_DATA_INPUT, XD_SM_WRITE_ENABLED);
	
	xd_sm_addr_input_cycle((page_addr << 8)|column_addr, xd_sm_buf->addr_cycles);
	
	xd_sm_delay_20ns();		// Tals = 20ns
	
	xd_sm_data_input_cycle(data_buf, xd_sm_page_size, redundant_buf, xd_sm_redundant_size);
	
	xd_sm_cmd_input_cycle(XD_SM_TRUE_PAGE_PROGRAM, XD_SM_WRITE_DISABLED);
	
	err = xd_sm_wait_ready_ms(BUSY_TIME_PROG);
	if(err)
	{
#ifdef XD_CARD_SUPPORTED
		if(xd_sm_info->card_type == CARD_TYPE_XD)
			xd_set_wp_enable();
#endif
#ifdef SM_CARD_SUPPORTED
		if(xd_sm_info->card_type == CARD_TYPE_SM)
			sm_set_wp_enable();
#endif
			
		return XD_SM_ERROR_TIMEOUT;
	}
	
	xd_sm_delay_100ns(1);		// Tww = 100ns
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
		xd_set_wp_enable();
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
		sm_set_wp_enable();
#endif

	xd_sm_delay_100ns(1);		// Tww = 100ns
	
	err = xd_sm_status_read(XD_SM_STATUS_READ1, (unsigned char *)&status1);
	if(err)
		return err;
		
	if((status1.Ready_Busy == XD_SM_STATUS_READY) && (status1.Pass_Fail == XD_SM_STATUS_PASS))
		return XD_SM_NO_ERROR;
		
	return XD_SM_ERROR_PAGE_PROGRAM;
}

int xd_sm_auto_block_erase(unsigned long page_addr)
{
	int err;
	XD_SM_Status1_t status1;
	
	xd_sm_cmd_input_cycle(XD_SM_BLOCK_ERASE_SETUP, XD_SM_WRITE_ENABLED);
	
	xd_sm_addr_input_cycle(page_addr, xd_sm_buf->addr_cycles-1);
	
	xd_sm_cmd_input_cycle(XD_SM_BLOCK_ERASE_EXECUTE, XD_SM_WRITE_DISABLED);
	
	err = xd_sm_wait_ready_ms(BUSY_TIME_BERASE);
	if(err)
	{
#ifdef XD_CARD_SUPPORTED
		if(xd_sm_info->card_type == CARD_TYPE_XD)
			xd_set_wp_enable();
#endif
#ifdef SM_CARD_SUPPORTED
		if(xd_sm_info->card_type == CARD_TYPE_SM)
			sm_set_wp_enable();
#endif
			
		return XD_SM_ERROR_TIMEOUT;
	}
	
	xd_sm_delay_100ns(1);		// Tww = 100ns
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
		xd_set_wp_enable();
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
		sm_set_wp_enable();
#endif

	err = xd_sm_status_read(XD_SM_STATUS_READ1, (unsigned char *)&status1);
	if(err)
		return err;
		
	if((status1.Ready_Busy == XD_SM_STATUS_READY) && (status1.Pass_Fail == XD_SM_STATUS_PASS))
		return XD_SM_NO_ERROR;
		
	return XD_SM_ERROR_BLOCK_ERASE;
}

int xd_sm_reset(void)
{
	xd_sm_cmd_input_cycle(XD_SM_RESET, XD_SM_WRITE_DISABLED);
	
	return xd_sm_wait_ready(BUSY_TIME_RST);
}

int xd_sm_id_read(unsigned char cmd, unsigned char *data_buf)
{
	int data_cnt = 4;
	
	if((cmd != XD_SM_ID_READ90) && (cmd != XD_SM_ID_READ91) && (cmd != XD_ID_READ9A))
		return XD_SM_ERROR_PARAMETER;
		
	if(cmd == XD_SM_ID_READ91)
		data_cnt = 5;
	
	if(xd_sm_test_ready() == XD_SM_STATUS_BUSY)
		return XD_SM_ERROR_BUSY;
			
	xd_sm_cmd_input_cycle(cmd, XD_SM_WRITE_DISABLED);
	
	xd_sm_addr_input_cycle(0x00, 1);
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_set_dat0_7_input();
		xd_sm_delay_100ns(1);		// Tar1 = Tcr = 100ns
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		sm_set_dat0_7_input();
		xd_sm_delay_100ns(2);		// Tar1 = Tcr = 200ns
	}
#endif

	xd_sm_serial_read_cycle(data_buf, data_cnt, NULL, 0);
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
		xd_set_ce_disable();
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
		sm_set_ce_disable();
#endif
	
	return XD_SM_NO_ERROR;
}

int xd_sm_check_byte_bits(unsigned char value)
{
	unsigned char mask = 0x01;
	int cnt,i;
	
	cnt = 0;
	for(i=0; i<8; i++)
	{
		mask <<= i;
		if(value & mask)
			cnt++;
	}
	
	return cnt;
}

int xd_sm_check_page_ecc(unsigned char *page_buf, unsigned char *redundant_buf)
{
	int err;
	unsigned char cal_ecc1, cal_ecc2, cal_ecc3;
	XD_SM_Redundant_Area_t *redundant = (void *)redundant_buf;
	
	ecc_calculate_ecc(ecc_table, page_buf+STORING_AREA1_OFFSET, &cal_ecc1, &cal_ecc2, &cal_ecc3);
	err = ecc_correct_data(page_buf+STORING_AREA1_OFFSET, redundant->ECC1, cal_ecc1, cal_ecc2, cal_ecc3);
	if((err != ECC_NO_ERROR) && (err != ECC_ERROR_CORRECTED))
		return err;
		
	ecc_calculate_ecc(ecc_table, page_buf+STORING_AREA2_OFFSET, &cal_ecc1, &cal_ecc2, &cal_ecc3);
	err = ecc_correct_data(page_buf+STORING_AREA2_OFFSET, redundant->ECC2, cal_ecc1, cal_ecc2, cal_ecc3);
	
	return err;
}

int xd_sm_check_unit_ecc(unsigned char *data_buf, unsigned char *data_ecc)
{
	unsigned char cal_ecc1, cal_ecc2, cal_ecc3;
	
	ecc_calculate_ecc(ecc_table, data_buf, &cal_ecc1, &cal_ecc2, &cal_ecc3);
	
	return ecc_correct_data(data_buf, data_ecc, cal_ecc1, cal_ecc2, cal_ecc3);
}

int xd_sm_format_redundant_area(unsigned char *page_buf, unsigned char *redundant_buf, unsigned short block_addr)
{
	int i;
	unsigned char cal_ecc1, cal_ecc2, cal_ecc3;
	XD_SM_Redundant_Area_t *redundant = (void *)redundant_buf;
	
	for(i=0; i<xd_sm_redundant_size; i++)
		redundant_buf[i] = 0xFF;
		
	ecc_calculate_ecc(ecc_table, page_buf+STORING_AREA1_OFFSET, &cal_ecc1, &cal_ecc2, &cal_ecc3);
	redundant->ECC1[0] = cal_ecc2;
	redundant->ECC1[1] = cal_ecc1;
	redundant->ECC1[2] = cal_ecc3;
	
	redundant->Block_Address1[0] = (block_addr >> 8) & 0xFF;
	redundant->Block_Address1[1] = block_addr & 0xFF;
	
	ecc_calculate_ecc(ecc_table, page_buf+STORING_AREA2_OFFSET, &cal_ecc1, &cal_ecc2, &cal_ecc3);
	redundant->ECC2[0] = cal_ecc2;
	redundant->ECC2[1] = cal_ecc1;
	redundant->ECC2[2] = cal_ecc3;
	
	redundant->Block_Address2[0] = (block_addr >> 8) & 0xFF;
	redundant->Block_Address2[1] = block_addr & 0xFF;
	
	return ECC_NO_ERROR;
}

int xd_sm_copy_page(unsigned short zone_no, unsigned short block_src, unsigned short page_src, unsigned short block_dst, unsigned short page_dst)
{
	int err;
	unsigned long page_addr;
	
	page_addr = (zone_no * xd_sm_physical_blks_perzone + block_src) * xd_sm_pages_per_blk + page_src;
	err = xd_sm_read_cycle1(0, page_addr, xd_sm_buf->page_buf, xd_sm_page_size, xd_sm_buf->redundant_buf);
	if(err)
		return err;

	page_addr = (zone_no * xd_sm_physical_blks_perzone + block_dst) * xd_sm_pages_per_blk + page_dst;
	err = xd_sm_auto_page_program(page_addr, xd_sm_buf->page_buf, xd_sm_buf->redundant_buf);
	if(err)
		return err;
		
	return XD_SM_NO_ERROR;
}

void xd_sm_set_block_free(unsigned short zone, unsigned short block)
{
	unsigned short byte_offset, bit_offset, mask;
	
	byte_offset = block >> 3;
	bit_offset = block % 8;
	mask = 0x80 >> bit_offset;
	
	xd_sm_buf->free_block_table[zone][byte_offset] |= mask;
}

void xd_sm_clear_block_free(unsigned short zone, unsigned short block)
{
	unsigned short byte_offset, bit_offset, mask;
	
	byte_offset = block >> 3;
	bit_offset = block % 8;
	mask = 0x80 >> bit_offset;
	
	xd_sm_buf->free_block_table[zone][byte_offset] &= ~mask;
}

unsigned short xd_sm_find_free_block(unsigned short zone)
{
	unsigned short byte_offset, bit_offset, mask, block;
	
	for(block=0; block<xd_sm_physical_blks_perzone; block++)
	{
		byte_offset = block >> 3;
		bit_offset = block % 8;
		mask = 0x80 >> bit_offset;
		
		if(xd_sm_buf->free_block_table[zone][byte_offset] & mask)
			return block;
	}

	return INVALID_BLOCK_ADDRESS;
}

void xd_sm_convertion_table_init(void)
{
	unsigned short i,j;
	
	//Mark all logical address as not assigned
	for(i=0; i<xd_sm_actually_zones; i++)
		for(j=0; j<xd_sm_logical_blks_perzone; j++)
			xd_sm_buf->logical_physical_table[i][j] = INVALID_BLOCK_ADDRESS;
			
	//Mark all physical blocks as assigned
	for(i=0; i<xd_sm_actually_zones; i++)
		for(j=0; j<(xd_sm_physical_blks_perzone/8); j++)
			xd_sm_buf->free_block_table[i][j] = 0;
}

int xd_sm_set_defective_block(unsigned short zone, unsigned short block)
{
	unsigned long page_addr;
	
	XD_SM_Redundant_Area_t *redundant = (void *)xd_sm_buf->redundant_buf;
	
	page_addr = (zone * xd_sm_physical_blks_perzone + block) * xd_sm_pages_per_blk;
	memset(xd_sm_buf->page_buf, 0, xd_sm_page_size);
	redundant->Data_Status_Flag = DATA_STATUS_INVALID;
	redundant->Block_Status_Flag = BLOCK_STATUS_MARKED_DEFECTIVE;
	
	return xd_sm_auto_page_program(page_addr, xd_sm_buf->page_buf, xd_sm_buf->redundant_buf);
}

int xd_sm_check_back_block(unsigned short zone, unsigned short block)
{
	int err;
	unsigned long page_addr;
	unsigned short logical_addr, block_addr;
	
	memset(xd_sm_buf->page_buf, 0, xd_sm_page_size);
	page_addr = (zone * xd_sm_physical_blks_perzone + block) * xd_sm_pages_per_blk;
	
	err = xd_sm_auto_block_erase(page_addr);
	if(err)
		return err;
	
	xd_sm_buf->page_buf[0] = 0xAA;
	xd_sm_buf->page_buf[1] = 0x55;
	logical_addr = xd_sm_physical_blks_perzone-1;
	block_addr = xd_sm_trans_logical_address(logical_addr);
	err = xd_sm_format_redundant_area(xd_sm_buf->page_buf, xd_sm_buf->redundant_buf, block_addr);
	if(err)
		return err;
	
	xd_sm_buf->page_buf[0] = 0x00;
	xd_sm_buf->page_buf[1] = 0x00;
	err = xd_sm_read_cycle1(0, page_addr, xd_sm_buf->page_buf, xd_sm_page_size, xd_sm_buf->redundant_buf);
	if(err)
		return err;
		
	if((xd_sm_buf->page_buf[0] != 0xAA) || (xd_sm_buf->page_buf[1] != 0x55))
		return XD_SM_ERROR_DATA;
		
	xd_sm_buf->page_buf[0] = 0x55;
	xd_sm_buf->page_buf[1] = 0xAA;
	logical_addr = xd_sm_physical_blks_perzone-1;
	block_addr = xd_sm_trans_logical_address(logical_addr);
	err = xd_sm_format_redundant_area(xd_sm_buf->page_buf, xd_sm_buf->redundant_buf, block_addr);
	if(err)
		return err;
	
	xd_sm_buf->page_buf[0] = 0x00;
	xd_sm_buf->page_buf[1] = 0x00;
	err = xd_sm_read_cycle1(0, page_addr, xd_sm_buf->page_buf, xd_sm_page_size, xd_sm_buf->redundant_buf);
	if(err)
		return err;
		
	if((xd_sm_buf->page_buf[0] != 0x55) || (xd_sm_buf->page_buf[1] != 0xAA))
		return XD_SM_ERROR_DATA;
	
	err = xd_sm_auto_block_erase(page_addr);
	if(err)
		return err;
		
	return XD_SM_NO_ERROR;
}

int xd_sm_check_block_address_parity(unsigned short block_addr)
{
	int bit,i;
	unsigned short mask;
	
	mask = 0x001;
	bit = 0;
	for(i=0; i<16; i++)
	{
		if((mask << i) & block_addr)
			bit ^= 1;
	}
	
	if(bit)
		return 0;
	else
		return 1;
}

int xd_sm_check_block_address_range(unsigned short block_addr)
{
	if((block_addr & 0xF800) != 0x1000)
		return 0;
		
	block_addr &= 0x07FF;
	block_addr >>= 1;
	
	if(block_addr < xd_sm_logical_blks_perzone)
		return 1;
	else
		return 0;
}

unsigned short xd_sm_parse_logical_address(unsigned short block_addr)
{
	unsigned long logical_addr = block_addr;
	
	logical_addr &= 0x07FF;
	logical_addr >>= 1;
	
	return logical_addr;
}

unsigned short xd_sm_trans_logical_address(unsigned short logical_addr)
{
	unsigned long block_addr = logical_addr;
	int bit = 0;
	unsigned short mask = 0x0001;
	int i;
	
	block_addr <<= 1;
	
	block_addr &= 0x07FE;
	block_addr |= 0x1000;
	
	for(i=1; i<16; i++)
	{
		if((mask << i) & block_addr)
			bit ^= 1;
	}
	if(bit)
		block_addr |= 0x0001;

	return block_addr;
}

int xd_sm_check_erased_block(unsigned char *redundant_buf)
{
	int err = 0;
	int i;
	
	for(i=0; i<xd_sm_redundant_size; i++)
	{
		if(redundant_buf[i] != 0xFF)
		{
			err = 1;
			break;
		}
	}
	
	if(err)
		return 0;
	else
		return 1;
}

int xd_sm_check_defective_block(unsigned char *redundant_buf)
{
	XD_SM_Redundant_Area_t *redundant = (void *)redundant_buf;
	
	if((redundant->Block_Status_Flag != BLOCK_STATUS_NORMAL)
		&& (xd_sm_check_byte_bits(redundant->Block_Status_Flag) < 2))
	{
		return 1;
	}
	
	if((redundant->Data_Status_Flag != DATA_STATUS_VALID)
		&& (xd_sm_check_byte_bits(redundant->Data_Status_Flag) < 4))
	{
		return 1;
	}
	
	return 0;
}

int xd_sm_block_address_search(unsigned char *redundant_buf, unsigned short *block_addr)
{
	XD_SM_Redundant_Area_t *redundant = (void *)redundant_buf;
	
	unsigned short block_addr1 = (redundant->Block_Address1[0] << 8) | redundant->Block_Address1[1];
	unsigned short block_addr2 = (redundant->Block_Address2[0] << 8) | redundant->Block_Address2[1];
	
	if(block_addr1 != block_addr2)
	{
		if(xd_sm_check_block_address_parity(block_addr2) && xd_sm_check_block_address_range(block_addr2))
		{
			if(!xd_sm_check_block_address_parity(block_addr1) || !xd_sm_check_block_address_range(block_addr1))
			{
				*block_addr = xd_sm_parse_logical_address(block_addr2);
				return XD_SM_NO_ERROR;
			}
			
			//Erase block or skip
			return XD_SM_ERROR_BLOCK_ADDRESS;
		}
	}
	
	if(!xd_sm_check_block_address_parity(block_addr1) || !xd_sm_check_block_address_range(block_addr1))
	{
		//Erase block or skip
		return XD_SM_ERROR_BLOCK_ADDRESS;
	}
	
	*block_addr = xd_sm_parse_logical_address(block_addr1);
	return XD_SM_NO_ERROR;
}

int xd_sm_card_capacity_determin(void)
{
	int err;
	unsigned long temp;
	XD_SM_ID_90_t id_90;
	
	err = xd_sm_id_read(XD_SM_ID_READ90, (unsigned char *)&id_90);
	if(err)
		return err;
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		err = xd_card_capacity_determin(id_90.Device_Code);
		if(err)
			return err;
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		err = sm_card_capacity_determin(id_90.Device_Code);
		if(err)
			return err;
	}
#endif
	if(xd_sm_info->card_type == CARD_TYPE_NONE)
		return XD_SM_ERROR_CARD_TYPE;
	
	if(xd_sm_total_zones > xd_sm_actually_zones)
		return XD_SM_ERROR_UNSUPPORTED_CAPACITY;

	xd_sm_info->blk_len = XD_SM_SECTOR_SIZE;
	temp = xd_sm_totoal_logical_blks;
	temp *= xd_sm_pages_per_blk;
	xd_sm_info->blk_nums = temp;
	
	return XD_SM_NO_ERROR;
}

int xd_sm_card_indentification(void)
{
	int err;
	XD_ID_9A_t id_9a;
	
	if(xd_sm_info->card_type == CARD_TYPE_SM)
		return XD_SM_NO_ERROR;
		
	err = xd_sm_id_read(XD_ID_READ9A, (unsigned char *)&id_9a);
	if(err)
		return err;
	
	if(id_9a.XD_CARD_ID != 0xB5)
	{
#ifdef  XD_SM_DEBUG
		Debug_Printf("#XD_CARD_ID error\n");
#endif		
		//return XD_SM_ERROR_CARD_ID;
	}
	xd_sm_info->raw_cid = id_9a;

	return XD_SM_NO_ERROR;
}

int xd_sm_create_logical_physical_table(void)
{
	int err, err_count = 0;
	unsigned short zone, block, block_temp;
	unsigned short logical_addr, logical_temp;
	unsigned long page_addr;
	
	xd_sm_convertion_table_init();
				
	for(zone=0; zone<xd_sm_total_zones; zone++)
	{
		for(block=0; block<xd_sm_physical_blks_perzone; block++)
		{
			//Is it CIS area in case of Zone 0?
			if((zone == 0) && (block == xd_sm_buf->cis_block_no))
				continue;

			//Read the data in the redundant area in the first sector
			page_addr = (zone * xd_sm_physical_blks_perzone + block) * xd_sm_pages_per_blk;
			err = xd_sm_read_cycle3(0, page_addr, xd_sm_buf->redundant_buf, xd_sm_redundant_size);
			if(err)
			{	
				err_count++;
				if(err_count > 10)
					return err;
				else
					continue;
			}
			
			//Is it unused block?
			if(xd_sm_check_erased_block(xd_sm_buf->redundant_buf))
			{
				xd_sm_set_block_free(zone, block);
				continue;
			}
			
			//Is is defective block?
			if(xd_sm_check_defective_block(xd_sm_buf->redundant_buf))
				continue;
			
			//Check the logical block address
			err = xd_sm_block_address_search(xd_sm_buf->redundant_buf, &logical_addr);
			if(err)
			{
				//xd_sm_set_block_free(zone, block);
				continue;
			}
			
			//Already entried on the convertsion table?
			if(xd_sm_buf->logical_physical_table[zone][logical_addr] == INVALID_BLOCK_ADDRESS)
			{
				xd_sm_buf->logical_physical_table[zone][logical_addr] = block;
				continue;
			}
			
			//Read the data in the redundant area in the last sector
			page_addr += (xd_sm_pages_per_blk - 1);
			err = xd_sm_read_cycle3(0, page_addr, xd_sm_buf->redundant_buf, xd_sm_redundant_size);
			if(err)
			{	
				err_count++;
				if(err_count > 10)
					return err;
				else
					continue;
			}

			//Check the logical block address
			err = xd_sm_block_address_search(xd_sm_buf->redundant_buf, &logical_temp);
			if(err)
				continue;

			//Erase or skip current physical block
			if(logical_addr != logical_temp)
			{
				page_addr = (zone * xd_sm_physical_blks_perzone + block) * xd_sm_pages_per_blk;
				err = 1;//xd_sm_auto_block_erase(page_addr);
				if(!err)
				{
					xd_sm_set_block_free(zone, block);
				}
				
				continue;
			}
			
			block_temp = xd_sm_buf->logical_physical_table[zone][logical_addr];
			
			//Read the redundant area in the last sector of physical block entried on current convertsion table
			page_addr = (zone * xd_sm_physical_blks_perzone + block_temp) * xd_sm_pages_per_blk;
			page_addr += (xd_sm_pages_per_blk - 1);
			err = xd_sm_read_cycle3(0, page_addr, xd_sm_buf->redundant_buf, xd_sm_redundant_size);
			if(err)
				continue;
				
			//Check the logical block address
			err = xd_sm_block_address_search(xd_sm_buf->redundant_buf, &logical_temp);
			if(err)
				continue;

			//Erase or skip entried block as having same logical address on the conversion table
			if((logical_addr != logical_temp)
				|| ((logical_addr == logical_temp) && (block_temp > block)))
			{
				page_addr = (zone * xd_sm_physical_blks_perzone + block_temp) * xd_sm_pages_per_blk;
				err = 1;//xd_sm_auto_block_erase(page_addr);
				if(!err)
				{
					xd_sm_set_block_free(zone, block_temp);
				}
				xd_sm_buf->logical_physical_table[zone][logical_addr] = block;
			}
			else
			{
				//Erase or skip current physical block
				page_addr = (zone * xd_sm_physical_blks_perzone + block) * xd_sm_pages_per_blk;
				err = 1;//xd_sm_auto_block_erase(page_addr);
				if(!err)
				{
					xd_sm_set_block_free(zone, block);
				}
			}
		}
	}

	return XD_SM_NO_ERROR;
}

int xd_sm_logical_format_determin(void)
{
	int err;
	unsigned char *mbr_buf, *pbr_buf;
	unsigned long pbr_sector;
	
	unsigned short bytes_per_sector, total_sectors;
	unsigned char cluster_factor;
	unsigned long cluster_number, extended_sectors, sectors_per_cluster;
	
	mbr_buf = xd_sm_buf->page_buf;
	pbr_buf = xd_sm_buf->page_buf;
	
	memset(mbr_buf, 0, xd_sm_info->blk_len);
	err = xd_sm_read_data(0, xd_sm_info->blk_len, mbr_buf);
	if(err)
		return err;
		
	if((mbr_buf[0x1FE] != 0x55) || (mbr_buf[0x1FF] != 0xAA))
		return XD_SM_ERROR_LOGICAL_FORMAT;
		
	pbr_sector = (((((mbr_buf[0x1C9] << 8) | mbr_buf[0x1C8]) << 8) | mbr_buf[0x1C7]) << 8) | mbr_buf[0x1C6];
	if(pbr_sector > xd_sm_totoal_logical_blks)
		return XD_SM_ERROR_LOGICAL_FORMAT;
		
	err = xd_sm_read_data(pbr_sector, xd_sm_info->blk_len, pbr_buf);
	if(err)
		return err;
	
	if((pbr_buf[0x1FE] != 0x55) || (pbr_buf[0x1FF] != 0xAA))
		return XD_SM_ERROR_LOGICAL_FORMAT;
	
	bytes_per_sector = (pbr_buf[0x0C] << 8) | pbr_buf[0x0B];
	if(bytes_per_sector != 0x0200)
		return XD_SM_ERROR_LOGICAL_FORMAT;
		
	cluster_factor = pbr_buf[0x0D];
	sectors_per_cluster = cluster_factor;
	if(sectors_per_cluster != xd_sm_pages_per_blk)
		return XD_SM_ERROR_LOGICAL_FORMAT;
	
	total_sectors = (pbr_buf[0x14] << 8) | pbr_buf[0x13];
	extended_sectors = (((((pbr_buf[0x23] << 8) | pbr_buf[0x22]) << 8) | pbr_buf[0x21]) << 8) | pbr_buf[0x20];
	if(total_sectors)
		cluster_number = total_sectors / sectors_per_cluster;
	else
		cluster_number = extended_sectors / sectors_per_cluster;
	if(cluster_number > 65525)
		return XD_SM_ERROR_LOGICAL_FORMAT;
		
	return XD_SM_NO_ERROR;
}

int xd_sm_reconstruct_logical_physical_table(void)
{
	int err;
	unsigned short zone, block, i;
	unsigned short block_addr, logical_addr;
	unsigned long page_addr;
	
	xd_sm_erase_all_blocks();
	
	for(zone=0; zone<xd_sm_total_zones; zone++)
	{
		logical_addr = 0;
		
		for(block=0; block<xd_sm_physical_blks_perzone; block++)
		{
			//Is it CIS area in case of Zone 0?
			if((zone == 0) && (block == xd_sm_buf->cis_block_no))
				continue;
			
			page_addr = (zone * xd_sm_physical_blks_perzone + block) * xd_sm_pages_per_blk;
			//Read the data in the redundant area in the first sector
			err = xd_sm_read_cycle3(0, page_addr, xd_sm_buf->redundant_buf, xd_sm_redundant_size);
			if(err)
				continue;
			
			//Is is defective block?
			if(xd_sm_check_defective_block(xd_sm_buf->redundant_buf))
				continue;
			
			//Erase this block
			//err = xd_sm_auto_block_erase(page_addr);
			
			//Set logical address
			if(logical_addr < xd_sm_logical_blks_perzone)
			{
				memset(xd_sm_buf->page_buf, 0xFF, xd_sm_page_size);
				block_addr = xd_sm_trans_logical_address(logical_addr);
				xd_sm_format_redundant_area(xd_sm_buf->page_buf, xd_sm_buf->redundant_buf, block_addr);
				
				for(i=0; i<xd_sm_pages_per_blk; i++)
				{
					page_addr = (zone * xd_sm_physical_blks_perzone + block) * xd_sm_pages_per_blk + i;
					xd_sm_auto_page_program(page_addr, xd_sm_buf->page_buf, xd_sm_buf->redundant_buf);
				}
				
				page_addr = (zone * xd_sm_physical_blks_perzone + block) * xd_sm_pages_per_blk;
				//Read the data in the redundant area in the first sector
				err = xd_sm_read_cycle3(0, page_addr, xd_sm_buf->redundant_buf, xd_sm_redundant_size);
				if(err)
					continue;
				
				if(xd_sm_check_defective_block(xd_sm_buf->redundant_buf))
					continue;
					
				logical_addr++;
			}
		}
	}
	
	return xd_sm_create_logical_physical_table();
}

int xd_sm_erase_all_blocks(void)
{
	unsigned short zone, block;

	for(zone=0; zone<xd_sm_total_zones; zone++)
	{
		for(block=0; block<xd_sm_physical_blks_perzone; block++)
		{
			//Is it CIS area in case of Zone 0?
			if((zone == 0) && (block == xd_sm_buf->cis_block_no))
				continue;
			
			xd_sm_check_back_block(zone, block);
		}
	}
	
	return XD_SM_NO_ERROR;
}

int xd_sm_physical_format_determin(void)
{
	int err,i;
	unsigned short block, sector;
	XD_SM_Redundant_Area_t *redundant = (void *)xd_sm_buf->redundant_buf;
	int cis_offset;
	unsigned long page_addr;
	
	for(block=0; block<xd_sm_buf->cis_search_max; block++)
	{
		sector = 0;
		while(sector < 8)
		{
			page_addr = block * xd_sm_pages_per_blk + sector;
			err = xd_sm_read_cycle3(0, page_addr, xd_sm_buf->redundant_buf, xd_sm_redundant_size);
			if(err)
			{
				sector++;
				continue;
			}
			
			if(xd_sm_check_defective_block(xd_sm_buf->redundant_buf))
			{
				sector++;
				continue;
			}
			else
			{
				break;
			}
		}
		if(sector >= 8)
			continue;
		
		page_addr = block * xd_sm_pages_per_blk + sector;
		err = xd_sm_read_cycle1(0, page_addr, xd_sm_buf->page_buf, xd_sm_page_size, xd_sm_buf->redundant_buf);
		if(err)
			continue;
		
		cis_offset = CIS_FIELD1_OFFSET;
		err = xd_sm_check_unit_ecc(xd_sm_buf->page_buf+cis_offset, redundant->ECC1);
		if((err != ECC_NO_ERROR) && (err != ECC_ERROR_CORRECTED))
		{
			cis_offset = CIS_FIELD2_OFFSET;
			err = xd_sm_check_unit_ecc(xd_sm_buf->page_buf+cis_offset, redundant->ECC2);
			if((err != ECC_NO_ERROR) && (err != ECC_ERROR_CORRECTED))
			{
				continue;
			}
		}
		
		err = 0;
		for(i=0; i<sizeof(CIS_DATA_0_9); i++)
		{
			if(*(xd_sm_buf->page_buf+cis_offset+i) != CIS_DATA_0_9[i])
			{
				err = 1;
				break;
			}
		}
		if(!err)
		{
			xd_sm_buf->cis_block_no = block;
			return XD_SM_NO_ERROR;
		}
	}
	
	xd_sm_buf->cis_block_no = INVALID_BLOCK_ADDRESS;
	return XD_SM_ERROR_PHYSICAL_FORMAT;
}

void xd_sm_staff_init(void)
{
	xd_sm_power_on();

	xd_sm_io_config();
	
	xd_sm_info->blk_len = 0;
	xd_sm_info->blk_nums = 0;

	xd_sm_info->xd_inited_flag = 0;
	xd_sm_info->xd_removed_flag = 0;
	xd_sm_info->sm_inited_flag = 0;
	xd_sm_info->sm_removed_flag = 0;
	
	xd_sm_info->read_only_flag = 0;
}

int xd_sm_cmd_test(void)
{
	int err;
	XD_SM_Status1_t status1;
		
	xd_sm_io_config();
	err = xd_sm_status_read(XD_SM_STATUS_READ1, (unsigned char *)&status1);
	if(err)
		return err;
		
	return XD_SM_NO_ERROR;
}

//check data lines consistency
int xd_sm_check_data_consistency(void)
{
	int error;
	unsigned char *mbr_buf = xd_sm_buf->page_buf;
	
	memset(mbr_buf, 0, xd_sm_info->blk_len);
	//read MBR information
	error = xd_sm_read_data(0, xd_sm_info->blk_len, mbr_buf);
	if(error)
	{
		//error! retry again!
		error = xd_sm_read_data(0, xd_sm_info->blk_len, mbr_buf);
		if(error)
			return error;
	}
		
	//check MBR data consistency
	if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
	{
		//data consistency error! retry again!
		error = xd_sm_read_data(0, xd_sm_info->blk_len, mbr_buf);
		if(error)
			return error;
			
		//check MBR data consistency
		if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
		{
			return XD_SM_ERROR_LOGICAL_FORMAT;
		}
	}
	
	return XD_SM_NO_ERROR;
}

//XD Initialization...
int xd_sm_init(XD_SM_Card_Info_t * card_info)
{
	int error;
	
	xd_sm_function_init();
	
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		if(xd_sm_info->xd_inited_flag && !xd_sm_info->xd_removed_flag)
		{
			error = xd_sm_cmd_test();
			if(!error)
				return error;
		}
		
		if(++xd_sm_info->xd_init_retry > XD_SM_INIT_RETRY)
			return XD_SM_ERROR_DRIVER_FAILURE;
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		if(xd_sm_info->sm_inited_flag && !xd_sm_info->sm_removed_flag)
		{
			error = xd_sm_cmd_test();
			if(!error)
				return error;
		}
		
		if(++xd_sm_info->sm_init_retry > XD_SM_INIT_RETRY)
			return XD_SM_ERROR_DRIVER_FAILURE;
	}
#endif

#ifdef  XD_SM_DEBUG
	Debug_Printf("\nXD/SM initialization started......\n");
#endif

	xd_sm_staff_init();
	
	error = xd_sm_reset();
	if(error)
	{
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_reset()\n", xd_sm_error_to_string(error));
#endif
		return error;
	}
	
	error = xd_sm_card_capacity_determin();
	if(error)
	{
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_card_capacity_determin()\n", xd_sm_error_to_string(error));
#endif
		return error;
	}

#ifdef  XD_SM_DEBUG	
	#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
		Debug_Printf("The capacitty of this XD card is %s!\n", xd_sm_buf->capacity_str);
	#endif
	
	#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
		Debug_Printf("The capacitty of this SM card is %s!\n", xd_sm_buf->capacity_str);
#endif
#endif
	
	error = xd_sm_card_indentification();
	if(error)
	{
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_card_indentification()\n", xd_sm_error_to_string(error));
#endif
		return error;
	}
	
	error = xd_sm_physical_format_determin();
	if(error)
	{
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_physical_format_determin()\n", xd_sm_error_to_string(error));
#endif
		//return error;
	}
	
	error = xd_sm_create_logical_physical_table();
	if(error)
	{
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_create_logical_physical_table()\n", xd_sm_error_to_string(error));
#endif
		#if 0
#ifdef  XD_SM_DEBUG
		Debug_Printf("Try to reconstruct logical-physical convertsion table...\n");
#endif
		error = xd_sm_reconstruct_logical_physical_table();
		if(error)
		{
#ifdef  XD_SM_DEBUG
			Debug_Printf("Reconstruct logical-physical convertsion table faild!\n");
#endif
			return error;
		}
		#else
		return error;
		#endif
	}
	
	error = xd_sm_check_data_consistency();
	if(error)
	{
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_check_data_consistency()\n", xd_sm_error_to_string(error));
#endif
		return error;
	}
	
	error = xd_sm_logical_format_determin();
	if(error)
	{
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_logical_format_determin()\n", xd_sm_error_to_string(error));
#endif
		//return error;
	}
	
#ifdef XD_SM_DEBUG
	Debug_Printf("xd_sm_init() is completed successfully!\n\n");
#endif

#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
	{
		xd_sm_info->xd_inited_flag = 1;
		xd_sm_info->xd_init_retry = 0;
	}
#endif
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
	{
		xd_sm_info->sm_inited_flag = 1;
		xd_sm_info->sm_init_retry = 0;
	}
#endif
	
	memcpy(card_info, xd_sm_info, sizeof(XD_SM_Card_Info_t));
	
	return XD_SM_NO_ERROR;
}

//Read data from XD card
int xd_sm_read_data(unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error;
	
	unsigned long data_offset,page_addr;
	unsigned short logical_no,physical_no,page_no,page_nums,zone_no,page_cnt;

	if(byte_cnt == 0)
	{
		error = XD_SM_ERROR_PARAMETER;
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_read_data()\n", xd_sm_error_to_string(error));
#endif
		return error;
	}
	
	if(xd_reset_flag_read)
	{
		xd_power_on();
	}

	xd_sm_io_config();

	if(xd_reset_flag_read)
	{
		error = xd_sm_reset();
		if (error)
			return error;
	}
	
	data_offset = 0;
	page_nums = (byte_cnt + xd_sm_page_size - 1) / xd_sm_page_size;
	
	while(page_nums)
	{
		logical_no = lba / xd_sm_pages_per_blk;
		zone_no = logical_no / xd_sm_logical_blks_perzone;
		logical_no %= xd_sm_logical_blks_perzone;
		
		if((logical_no >= xd_sm_logical_blks_perzone) || (zone_no >= xd_sm_actually_zones))
		{		
			error = XD_SM_ERROR_PARAMETER;
#ifdef  XD_SM_DEBUG
			Debug_Printf("#%s error occured in xd_sm_read_data()\n", xd_sm_error_to_string(error));
#endif
			return error;
		}
		
		physical_no = xd_sm_buf->logical_physical_table[zone_no][logical_no];
		
		page_no = lba % xd_sm_pages_per_blk;
		page_cnt = page_nums > (xd_sm_pages_per_blk - page_no) ? (xd_sm_pages_per_blk - page_no) : page_nums;
		page_addr = (zone_no * xd_sm_physical_blks_perzone + physical_no) * xd_sm_pages_per_blk + page_no;
		
		//In a flash memory device, there might be a logic block that is not allocate to a physcial 
		//block due to the block not being used. all data shoude be set to FFH when access this logical block
		if(physical_no == INVALID_BLOCK_ADDRESS && xd_sm_search_free_block())
		{
			memset(data_buf+data_offset, 0xFF, page_cnt*xd_sm_page_size);
			
			data_offset += page_cnt*xd_sm_page_size;
			lba += page_cnt;
			page_nums -= page_cnt;
			continue;
		}

		if(physical_no >= xd_sm_physical_blks_perzone)
		{
			error = XD_SM_ERROR_PARAMETER;
#ifdef  XD_SM_DEBUG
			Debug_Printf("#%s error occured in xd_sm_read_data()\n", xd_sm_error_to_string(error));
#endif
			return error;
		}
		
		error = xd_sm_read_cycle1(0, page_addr, data_buf+data_offset, page_cnt*xd_sm_page_size, xd_sm_buf->redundant_buf);
		if(error)
		{
#ifdef  XD_SM_DEBUG
			Debug_Printf("#%s error occured in xd_sm_read_data()\n", xd_sm_error_to_string(error));
#endif
			return error;
		}
		
#ifdef AMLOGIC_CHIP_SUPPORT
		data_offset += ((unsigned long)data_buf == 0x3400000) ? 0 : page_cnt*xd_sm_page_size;
#else
		data_offset += page_cnt*xd_sm_page_size;
#endif
		lba += page_cnt;
		page_nums -= page_cnt;
	}
	return XD_SM_NO_ERROR;
}

//Write data to XD card
int xd_sm_write_data(unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error,i,j,k;
	
	unsigned long data_offset,page_addr,offset;
	unsigned short logical_no,physical_no,page_no,page_nums,zone_no,page_max,page_cnt;
	unsigned short free_blk_no,block_addr;
	unsigned char *page_buf;
	
	if(byte_cnt == 0)
	{
		error = XD_SM_ERROR_PARAMETER;
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
		return error;
	}
	
	if(xd_sm_info->read_only_flag)
	{
		error = XD_SM_ERROR_DRIVER_FAILURE;
#ifdef  XD_SM_DEBUG
		Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
		return error;
	}
	
	if(xd_reset_flag_write)
	{
		xd_power_on();
	}

	xd_sm_io_config();

	if(xd_reset_flag_write)
	{
		error = xd_sm_reset();
		if (error)
			return error;
	}
	
	data_offset = 0;
	page_nums = (byte_cnt + xd_sm_page_size - 1) / xd_sm_page_size;
	
	while(page_nums)
	{
		logical_no = lba / xd_sm_pages_per_blk;
		zone_no = logical_no / xd_sm_logical_blks_perzone;
		logical_no %= xd_sm_logical_blks_perzone;
		page_no = lba % xd_sm_pages_per_blk;
		block_addr = xd_sm_trans_logical_address(logical_no);
		
		if((logical_no >= xd_sm_logical_blks_perzone) || (zone_no >= xd_sm_actually_zones))
		{
			error = XD_SM_ERROR_PARAMETER;
#ifdef  XD_SM_DEBUG
			Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
			return error;
		}
		
		free_blk_no = xd_sm_find_free_block(zone_no);
		if(free_blk_no == INVALID_BLOCK_ADDRESS)
		{		
			error = XD_SM_ERROR_NO_FREE_BLOCK;
#ifdef  XD_SM_DEBUG
			Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
			return error;
		}
		
		page_addr = (zone_no * xd_sm_physical_blks_perzone + free_blk_no) * xd_sm_pages_per_blk;
		error = xd_sm_auto_block_erase(page_addr);
		if(error)
		{
			xd_sm_clear_block_free(zone_no, free_blk_no);
			//continue;
			return error;
		}
		
		physical_no = xd_sm_buf->logical_physical_table[zone_no][logical_no];
		if(physical_no == INVALID_BLOCK_ADDRESS)
		{
			memset(xd_sm_buf->page_buf, 0xFF, xd_sm_page_size);
			
			page_max = (page_no+page_nums) < xd_sm_pages_per_blk ? (page_no+page_nums) : xd_sm_pages_per_blk;
			page_cnt = 0;
			offset = data_offset;

			for(i=0; i<xd_sm_pages_per_blk; i++)
			{
				page_addr = (zone_no * xd_sm_physical_blks_perzone + free_blk_no) * xd_sm_pages_per_blk + i;
				if((i >= page_no) && (i < page_max))
				{
					page_buf = data_buf+offset;
					offset += xd_sm_page_size;
					page_cnt++;
				}
				else
				{
					page_buf = xd_sm_buf->page_buf;
				}
				
				xd_sm_format_redundant_area(page_buf, xd_sm_buf->redundant_buf, block_addr);
				error = xd_sm_auto_page_program(page_addr, page_buf, xd_sm_buf->redundant_buf);
				if(error)
				{
#ifdef  XD_SM_DEBUG
					Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
					//return error;
					break;
				}
			}
			if(error)
				continue;
			
			data_offset = offset;
			lba += page_cnt;
			page_nums -= page_cnt;
			
			xd_sm_clear_block_free(zone_no, free_blk_no);
			xd_sm_buf->logical_physical_table[zone_no][logical_no] = free_blk_no;
		}
		else
		{
			page_max = (page_no+page_nums) < xd_sm_pages_per_blk ? (page_no+page_nums) : xd_sm_pages_per_blk;
			page_cnt = 0;
			offset = data_offset;
			
			for(i=0; i<page_no; i++)
			{
				error = xd_sm_copy_page(zone_no, physical_no, i, free_blk_no, i);
				if(error)
				{
					error = XD_SM_ERROR_COPY_PAGE;
#ifdef  XD_SM_DEBUG
					Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
					//return error;
					break;
				}
			}
		
			for(j=page_no; j<page_max; j++)
			{
				xd_sm_format_redundant_area(data_buf+offset, xd_sm_buf->redundant_buf, block_addr);
					
				page_addr = (zone_no * xd_sm_physical_blks_perzone + free_blk_no) * xd_sm_pages_per_blk + j;
				error = xd_sm_auto_page_program(page_addr, data_buf+offset, xd_sm_buf->redundant_buf);
				if(error)
				{
#ifdef  XD_SM_DEBUG
					Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
					//return error;
					break;
				}
					
				offset += xd_sm_page_size;
				page_cnt++;
			}
			if(error)
				continue;

			for(k=page_max; k<xd_sm_pages_per_blk; k++)
			{
				error = xd_sm_copy_page(zone_no, physical_no, k, free_blk_no, k);
				if(error)
				{
					error = XD_SM_ERROR_COPY_PAGE;
#ifdef  XD_SM_DEBUG
					Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
					//return error;
					break;
				}
			}
			if(error)
				continue;
			
			data_offset = offset;
			lba += page_cnt;
			page_nums -= page_cnt;
			
			page_addr = (zone_no * xd_sm_physical_blks_perzone + physical_no) * xd_sm_pages_per_blk;
			error = xd_sm_auto_block_erase(page_addr);
			if(error)
			{
				error = XD_SM_ERROR_BLOCK_ERASE;
#ifdef  XD_SM_DEBUG
				Debug_Printf("#%s error occured in xd_sm_write_data()\n", xd_sm_error_to_string(error));
#endif
				//return error;
			}
			
			xd_sm_set_block_free(zone_no, physical_no);
			xd_sm_clear_block_free(zone_no, free_blk_no);
			xd_sm_buf->logical_physical_table[zone_no][logical_no] = free_blk_no;
		}
	}
	return XD_SM_NO_ERROR;
}

//XD Power on/off
void xd_sm_power_on(void)
{
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
		xd_power_on();
#endif
		
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
		sm_power_on();
#endif
}

void xd_sm_power_off()
{
#ifdef XD_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_XD)
		xd_power_off();
#endif
		
#ifdef SM_CARD_SUPPORTED
	if(xd_sm_info->card_type == CARD_TYPE_SM)
		sm_power_off();
#endif
}

int xd_sm_search_free_block(void)
{
	unsigned zone, block;
	
	for(zone=0; zone<xd_sm_total_zones; zone++)
	{
		for(block=0; block<xd_sm_physical_blks_perzone/8; block++)
		{
			if(xd_sm_buf->free_block_table[zone][block])
				return 1;
		}
	}
	
	return 0;
}
