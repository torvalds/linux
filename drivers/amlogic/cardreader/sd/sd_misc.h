#ifndef _H_SD_MISC
#define _H_SD_MISC

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mii.h>
#include <linux/skbuff.h>

#include <mach/am_regs.h>
#include <mach/irqs.h>
#include <mach/card_io.h>    
 
#include "sd_port.h"
    
#define sd_get_timer_tick()			READ_CBUS_REG(ISA_TIMERE)	//unit: 10us or 1/100ms, max: 0 ~ 0xFFFFFF
#define SD_MAX_TIMER_TICK           0xFFFFFF
#define TIMER_1US					1
#define TIMER_10US					(10*TIMER_1US)
#define TIMER_1MS					(100*TIMER_10US)
void sd_start_timer(unsigned long time_value);
int sd_check_timer(void);
int sd_check_timeout(void);
#define Debug_Printf				printk
extern const unsigned short sd_crc_table[];
//#define inline _Inline
    
#define SD_CAL_BIT_CRC(crc_val,bit_mask_value)	{if(crc_val&0x8000){crc_val<<=1;crc_val^=0x1021;}else{crc_val<<=1;};if(bit_mask_value){crc_val^=0x1021;};}
    
#define WRITE_BYTE_TO_FIFO(DATA)	{WRITE_MPEG_REG(HFIFO_DATA,DATA);while((READ_MPEG_REG(BFIFO_LEVEL)>>8) >= 120){}}
    
#define SD_MMC_HW_CONTROL
#define SD_MMC_SW_CONTROL
    
#define SD_MMC_ALLOC_MEMORY
/*  there is problem if sd dma buffer is allocated by malloc, 
    so we #undef SD_MMC_ALLOC_MEMORY and define this address in bsp: MEMITEM_CARDREADER_BUF 
*/ 
    
#ifdef SD_MMC_ALLOC_MEMORY
#define sd_mmc_malloc				kzalloc
#define sd_mmc_free					kfree
#endif				/*  */
    
#define SD_MMC_DEBUG
    
#define SD_MMC_CRC_CHECK
    
#define sd_delay_us						udelay
#define sd_delay_ms						msleep
    
//Delay time in 1 us
//void sd_delay_us(unsigned long num_us);
//Delay time in 1 ms
//void sd_delay_ms(unsigned long num_ms);
    
//clock low delay in identification mode
#define sd_clk_delay_identify_low()         sd_delay_us(3)
//clock high delay in identification mode
#define sd_clk_delay_identify_high()        sd_delay_us(3)
//clock low delay in transfer mode
#define sd_clk_delay_tranfer_low()
//clock high delay in transfer mode
#define sd_clk_delay_tranfer_high()
    
#define sd_clk_identify_high()              {sd_set_clk_high();sd_clk_delay_identify_high();}
#define sd_clk_identify_low()               {sd_set_clk_low();sd_clk_delay_identify_low();}
#define sd_clk_transfer_high()              {sd_set_clk_high();sd_clk_delay_tranfer_high();}
#define sd_clk_transfer_low()               {sd_set_clk_low();sd_clk_delay_tranfer_low();}
    
//calculate Nac cycles in clock
unsigned long sd_cal_clks_nac(unsigned char TAAC, unsigned char NSAC);
unsigned char sd_verify_crc7(unsigned char *ptr, unsigned int len);
unsigned short sd_verify_crc16(unsigned char *ptr, unsigned int len);
unsigned char sd_cal_crc7(unsigned char *ptr, unsigned int len);
unsigned short sd_cal_crc16(unsigned char *ptr, unsigned int len);
unsigned short sd_cal_crc_mode(unsigned char *ptr, unsigned int len,
				unsigned char *mode);

#endif				//_H_SD_MISC
