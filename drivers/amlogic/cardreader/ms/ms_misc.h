#ifndef _H_MS_MISC
#define _H_MS_MISC

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
    
#include <mach/am_regs.h>
#include <asm/cacheflush.h>
#include <asm/delay.h>
#include <mach/am_regs.h>

#include "ms_protocol.h"
#include "mspro_protocol.h"
#include "ms_mspro.h"
    
//#define ms_get_timer_tick()         READ_ISA_REG(IREG_TIMER_E_COUNT)	//unit: 10us or 1/100ms, max: 0 ~ 0xFFFFFF
#define ms_get_timer_tick()		READ_CBUS_REG(ISA_TIMERE)
#define MS_MAX_TIMER_TICK           0xFFFFFF
//#define TIMER_1US					1
//#define TIMER_10US					(10*TIMER_1US)
//#define TIMER_1MS					(100*TIMER_10US)
#define TIMER_1MS					1

void ms_start_timer(unsigned long time_value);
int ms_check_timer(void);
int ms_check_timeout(void);

#define MS_MSPRO_DEBUG
#define Debug_Printf				printk
extern const unsigned short ms_crc_table[];

//#define inline _Inline
    
//Definition to use block address 0x3400000
//#define AMLOGIC_CHIP_SUPPORT
    
#ifdef AMLOGIC_CHIP_SUPPORT
#ifdef AVOS
#define WRITE_BYTE_TO_FIFO(DATA)	{WRITE_MPEG_REG(HFIFO_DATA,DATA);while((READ_MPEG_REG(BFIFO_LEVEL)>>8) >= 120){}}
#else				/*  */
#define WRITE_BYTE_TO_FIFO(DATA)    {Wr(HFIFO_DATA,DATA);while((Rd(BFIFO_LEVEL)>>8) >= 120){}}
#endif				/*  */
#endif				/*  */
    
#define MS_MSPRO_HW_CONTROL
#define MS_MSPRO_SW_CONTROL
    
#define MS_MSPRO_ALLOC_MEMORY
    
#ifdef MS_MSPRO_ALLOC_MEMORY
#define ms_mspro_malloc				kzalloc
#define ms_mspro_free				kfree
#endif				/*  */
    
//Definition for debug
#if ((!defined __ROM_) || (defined __ROM_ && defined __ROMDBG_))
#define MS_MSPRO_DEBUG
    //#define MS_MSPRO_CRC_CHECK
#endif				/*  */
#define MS_MSPRO_CRC_CHECK
//Delay time in 1 us
void ms_delay_us(unsigned long num_us);

//Delay time in 1 ms
void ms_delay_ms(unsigned long num_ms);

//Maximum 20Mhz, Period = 50ns
#define ms_clk_delay_serial_low()
#define ms_clk_delay_serial_high()
//Maximum 40Mhz, Period = 25ns
#define ms_clk_delay_parallel_low()
#define ms_clk_delay_parallel_high()
    
#define ms_clk_serial_low()         	{ms_set_clk_low();ms_clk_delay_serial_low();}
#define ms_clk_serial_high()        	{ms_set_clk_high();ms_clk_delay_serial_high();}
#define ms_clk_parallel_low()       	{ms_set_clk_low();ms_clk_delay_parallel_low();}
#define ms_clk_parallel_high()      	{ms_set_clk_high();ms_clk_delay_parallel_high();}
unsigned short ms_verify_crc16(unsigned char *ptr, unsigned int len);
unsigned short ms_cal_crc16(unsigned char *ptr, unsigned int len);

/**************************************************************/
int ms_media_type_identification(MS_MSPRO_Card_Info_t *ms_mspro_info);
int ms_search_boot_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char *data_buf);
int ms_check_boot_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char *data_buf);
int ms_check_disabled_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char *data_buf);
int ms_boot_area_protection(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char *data_buf);
int ms_logical_physical_table_creation(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned short seg_no);

//int ms_read_boot_idi(unsigned char * data_buf);
int ms_read_page(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr,
		  unsigned char *data_buf);
int ms_write_page(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr,
		   unsigned char *data_buf);
int ms_copy_page(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long source_block_addr,
		  unsigned char source_page_addr, unsigned long dest_block_addr,
		  unsigned char dest_page_addr, unsigned char *data_buf);
int ms_read_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr,
		   unsigned short page_nums, unsigned char *data_buf);
int ms_write_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr,
		    unsigned short page_nums, unsigned char *data_buf);
int ms_erase_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr);
int ms_read_extra_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr);
int ms_write_extra_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr);
int ms_overwrite_extra_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr,
			     unsigned char mask_data);
int ms_sleep(void);
int ms_clear_buffer(void);
int ms_flash_stop(void);
int ms_reset(MS_MSPRO_Card_Info_t *ms_mspro_info);
/***************************************************************/

int mspro_media_type_identification(MS_MSPRO_Card_Info_t *ms_mspro_info);
int mspro_cpu_startup(MS_MSPRO_Card_Info_t *ms_mspro_info);
int mspro_confirm_attribute_information(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char *data_buf);
int mspro_confirm_system_information(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char *data_buf);
int mspro_recognize_file_system(void);
int mspro_read_user_sector(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long sector_addr,
			     unsigned short sector_count,
			     unsigned char *data_buf);
int mspro_write_user_sector(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long sector_addr,
			     unsigned short sector_count,
			     unsigned char *data_buf);
int mspro_erase_user_sector(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long sector_addr,
			     unsigned short sector_count);
int mspro_read_attribute_sector(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long sector_addr,
				 unsigned short sector_count,
				 unsigned char *data_buf);
int mspro_read_information_block(void);
int mspro_update_imformation_block(void);
int mspro_format(void);
int mspro_sleep(void);

/**************************************************************/

//Following functions only used in ms_protocol.c and mspro_protocol.c
int ms_mspro_wait_int(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet);
int ms_mspro_wait_rdy(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet);
int ms_mspro_write_tpc(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet);
int ms_mspro_read_data_line(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet);
int ms_mspro_write_data_line(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet);
int ms_mspro_packet_communicate(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet);
char *ms_mspro_error_to_string(int errcode);
void ms_mspro_endian_convert(Endian_Type_t data_type, void *data);

//Following functions are the API used for outside routinue
//void ms_mspro_get_info(blkdev_stat_t *info);
int ms_mspro_init(MS_MSPRO_Card_Info_t * card_info);
void ms_mspro_exit(MS_MSPRO_Card_Info_t *ms_mspro_info);
void ms_mspro_prepare_init(MS_MSPRO_Card_Info_t *ms_mspro_info);
int ms_mspro_check_insert(MS_MSPRO_Card_Info_t *ms_mspro_info);
int ms_mspro_read_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long lba, unsigned long byte_cnt,
			unsigned char *data_buf);
int ms_mspro_write_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long lba, unsigned long byte_cnt,
			 unsigned char *data_buf);
void ms_mspro_power_on(MS_MSPRO_Card_Info_t *ms_mspro_info);
void ms_mspro_power_off(MS_MSPRO_Card_Info_t *ms_mspro_info);

#endif				//_H_MS_MISC

