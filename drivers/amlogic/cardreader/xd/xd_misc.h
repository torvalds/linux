#ifndef _H_XD_MISC
#define _H_XD_MISC

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
    
#include <asm/arch/am_regs.h>
#include <asm/cacheflush.h>
#include <asm/delay.h>
    
#define AML_ATHENA
    
#ifdef AML_ATHENA
#define xd_sm_get_timer_tick()      READ_ISA_REG(IREG_TIMER_E_COUNT)	//unit: 10us or 1/100ms, max: 0 ~ 0xFFFFFF
#define XD_SM_MAX_TIMER_TICK        0xFFFFFF
#define TIMER_1US					1
#define TIMER_10US					(10*TIMER_1US)
#define TIMER_1MS					(100*TIMER_10US)
#else				/*  */
#define xd_sm_get_timer_tick()		READ_ISA_REG(IREG_TIMER_A)	//unit: 10us or 1/100ms, max: 0 ~ 0xFFFF
#define XD_SM_MAX_TIMER_TICK		0xFFFF
#define TIMER_10US					1
#define TIMER_1MS					(100*TIMER_10US)
#endif				/*  */
void xd_sm_start_timer(unsigned long time_value);
int xd_sm_check_timer(void);
int xd_sm_check_timeout(void);

#define Debug_Printf				printk
    
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
    
//Definition for debug
#if ((!defined __ROM_) || (defined __ROM_ && defined __ROMDBG_))
//#define XD_SM_DEBUG
    //#define XD_SM_ECC_CHECK
#endif				/*  */
    
//#if (defined T3510) || (defined T3511) || (defined AVOS)
#define XD_CARD_SUPPORTED
    //#define SM_CARD_SUPPORTED
//#else //T3295
//      #ifdef XDCARD
//              #define XD_CARD_SUPPORTED
//      #endif
//      #ifdef SMCARD
//              #define SM_CARD_SUPPORTED
//      #endif
//#endif
    
//#ifdef AVOS
#define XD_SM_ALLOC_MEMORY
#ifdef XD_SM_ALLOC_MEMORY
#define XD_SM_NUM_POINTER
#endif				/*  */
//#endif
    
#ifdef XD_SM_ALLOC_MEMORY
#define xd_sm_malloc			kmalloc
#define xd_sm_free				kfree
#endif				/*  */
    
//XD IO redfine
#define xd_set_re_enable()				xd_set_re_low()
#define xd_set_re_disable()				xd_set_re_high()
    
#define	xd_set_ce_enable()				xd_set_ce_low()
#define	xd_set_ce_disable()				xd_set_ce_high()
    
#define	xd_set_ale_enable()				xd_set_ale_high()
#define	xd_set_ale_disable()			xd_set_ale_low()
    
#define	xd_set_cle_enable()				xd_set_cle_high()
#define xd_set_cle_disable()			xd_set_cle_low()
    
#define	xd_set_we_enable()				xd_set_we_low()
#define	xd_set_we_disable()				xd_set_we_high()
    
#define	xd_set_wp_enable()				xd_set_wp_low()
#define	xd_set_wp_disable()				xd_set_wp_high()
    
//SM IO redfine
#define sm_set_re_enable()				sm_set_re_low()
#define sm_set_re_disable()				sm_set_re_high()
    
#define	sm_set_ce_enable()				sm_set_ce_low()
#define	sm_set_ce_disable()				sm_set_ce_high()
    
#define	sm_set_ale_enable()				sm_set_ale_high()
#define	sm_set_ale_disable()			sm_set_ale_low()
    
#define	sm_set_cle_enable()				sm_set_cle_high()
#define sm_set_cle_disable()			sm_set_cle_low()
    
#define	sm_set_we_enable()				sm_set_we_low()
#define	sm_set_we_disable()				sm_set_we_high()
    
#define	sm_set_wp_enable()				sm_set_wp_low()
#define	sm_set_wp_disable()				sm_set_wp_high()
    
//Time delay
#define xd_sm_delay_20ns()				{__asm__("nop");__asm__("nop");__asm__("nop");}
#define xd_sm_delay_40ns()				{xd_sm_delay_20ns(); xd_sm_delay_20ns();}
#define xd_sm_delay_60ns()				{xd_sm_delay_20ns(); xd_sm_delay_20ns(); xd_sm_delay_20ns();}
    
//Delay time in 100 ns
void xd_sm_delay_100ns(unsigned long num_100ns);

//Delay time in 1 us
void xd_sm_delay_us(unsigned long num_us);

//Delay time in 1 ms
void xd_sm_delay_ms(unsigned long num_ms);

//ECC routines
extern unsigned char ecc_table[];
void ecc_trans_result(unsigned char reg2, unsigned char reg3,
		       unsigned char *ecc1, unsigned char *ecc2);
void ecc_calculate_ecc(unsigned char *table, unsigned char *data,
			unsigned char *ecc1, unsigned char *ecc2,
			unsigned char *ecc3);
unsigned char ecc_correct_data(unsigned char *data, unsigned char *data_ecc,
				unsigned char ecc1, unsigned char ecc2,
				unsigned char ecc3);

#endif				//_H_sm_MISC
