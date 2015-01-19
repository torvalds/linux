#ifndef _H_XD_PORT_COMMON
#define _H_XD_PORT_COMMON

#include <asm/types.h>
#include <asm/io.h>
#include <asm/arch/am_regs.h>
    
#include <asm/drivers/cardreader/card_io.h>
    
//Following I/O configurations are just for default case if no any known PCB defined
    
#define XD_IO_EXTERNAL
    
//Port operation for XD BUS
//write it as such form that could be replaced by function later if needed
    
#ifdef XD_IO_EXTERNAL
extern unsigned XD_DAT_OUTPUT_EN_REG;
extern unsigned XD_DAT0_7_OUTPUT_EN_MASK;
extern unsigned XD_DAT_INPUT_REG;
extern unsigned XD_DAT_OUTPUT_REG;
extern unsigned XD_DAT0_7_INPUT_MASK;
extern unsigned XD_DAT0_7_OUTPUT_MASK;
extern unsigned XD_DAT_INPUT_OFFSET;
extern unsigned XD_DAT_OUTPUT_OFFSET;
extern unsigned XD_RB_OUTPUT_EN_REG;
extern unsigned XD_RB_OUTPUT_EN_MASK;
extern unsigned XD_RB_INPUT_REG;
extern unsigned XD_RB_INPUT_MASK;
extern unsigned XD_RE_OUTPUT_EN_REG;
extern unsigned XD_RE_OUTPUT_EN_MASK;
extern unsigned XD_RE_OUTPUT_REG;
extern unsigned XD_RE_OUTPUT_MASK;
extern unsigned XD_CE_OUTPUT_EN_REG;
extern unsigned XD_CE_OUTPUT_EN_MASK;
extern unsigned XD_CE_OUTPUT_REG;
extern unsigned XD_CE_OUTPUT_MASK;
extern unsigned XD_ALE_OUTPUT_EN_REG;
extern unsigned XD_ALE_OUTPUT_EN_MASK;
extern unsigned XD_ALE_OUTPUT_REG;
extern unsigned XD_ALE_OUTPUT_MASK;
extern unsigned XD_CLE_OUTPUT_EN_REG;
extern unsigned XD_CLE_OUTPUT_EN_MASK;
extern unsigned XD_CLE_OUTPUT_REG;
extern unsigned XD_CLE_OUTPUT_MASK;
extern unsigned XD_WE_OUTPUT_EN_REG;
extern unsigned XD_WE_OUTPUT_EN_MASK;
extern unsigned XD_WE_OUTPUT_REG;
extern unsigned XD_WE_OUTPUT_MASK;
extern unsigned XD_WP_OUTPUT_EN_REG;
extern unsigned XD_WP_OUTPUT_EN_MASK;
extern unsigned XD_WP_OUTPUT_REG;
extern unsigned XD_WP_OUTPUT_MASK;
extern unsigned XD_INS_OUTPUT_EN_REG;
extern unsigned XD_INS_OUTPUT_EN_MASK;
extern unsigned XD_INS_INPUT_REG;
extern unsigned XD_INS_INPUT_MASK;
extern unsigned XD_PWR_OUTPUT_EN_REG;
extern unsigned XD_PWR_OUTPUT_EN_MASK;
extern unsigned XD_PWR_OUTPUT_REG;
extern unsigned XD_PWR_OUTPUT_MASK;
extern unsigned XD_PWR_EN_LEVEL;

#define XD_POWER_CONTROL
extern unsigned XD_WORK_MODE;

    //extern void (*xd_power_register)(int power_on);
    //extern int (*xd_ins_register)(void);
    //extern void (*xd_io_release_register)(void);
void xd_gpio_enable(void);

#else				//XD_IO_EXTERNAL
    
#define XD_DAT_OUTPUT_EN_REG			CARD_GPIO_ENABLE
#define XD_DAT0_7_OUTPUT_EN_MASK		PREG_IO_9_16_MASK
#define XD_DAT_INPUT_REG				CARD_GPIO_INPUT
#define XD_DAT_OUTPUT_REG				CARD_GPIO_OUTPUT
#define XD_DAT0_7_INPUT_MASK			PREG_IO_9_16_MASK
#define XD_DAT0_7_OUTPUT_MASK			PREG_IO_9_16_MASK
#define XD_DAT_INPUT_OFFSET				9
#define XD_DAT_OUTPUT_OFFSET			9
    
#define XD_RB_OUTPUT_EN_REG				CARD_GPIO_ENABLE
#define XD_RB_OUTPUT_EN_MASK			PREG_IO_6_MASK
#define XD_RB_INPUT_REG					CARD_GPIO_INPUT
#define XD_RB_INPUT_MASK				PREG_IO_6_MASK
    
#define XD_RE_OUTPUT_EN_REG				CARD_GPIO_ENABLE
#define XD_RE_OUTPUT_EN_MASK			PREG_IO_18_MASK
#define XD_RE_OUTPUT_REG				CARD_GPIO_OUTPUT
#define XD_RE_OUTPUT_MASK				PREG_IO_18_MASK
    
#define XD_CE_OUTPUT_EN_REG				CARD_GPIO_ENABLE
#define XD_CE_OUTPUT_EN_MASK			PREG_IO_17_MASK
#define XD_CE_OUTPUT_REG				CARD_GPIO_OUTPUT
#define XD_CE_OUTPUT_MASK				PREG_IO_17_MASK
    
#define XD_ALE_OUTPUT_EN_REG			CARD_GPIO_ENABLE
#define XD_ALE_OUTPUT_EN_MASK			PREG_IO_1_MASK
#define XD_ALE_OUTPUT_REG				CARD_GPIO_OUTPUT
#define XD_ALE_OUTPUT_MASK				PREG_IO_1_MASK
    
#define XD_CLE_OUTPUT_EN_REG			CARD_GPIO_ENABLE
#define XD_CLE_OUTPUT_EN_MASK			PREG_IO_2_MASK
#define XD_CLE_OUTPUT_REG				CARD_GPIO_OUTPUT
#define XD_CLE_OUTPUT_MASK				PREG_IO_2_MASK
    
#define XD_WE_OUTPUT_EN_REG				CARD_GPIO_ENABLE
#define XD_WE_OUTPUT_EN_MASK			PREG_IO_19_MASK
#define XD_WE_OUTPUT_REG				CARD_GPIO_OUTPUT
#define XD_WE_OUTPUT_MASK				PREG_IO_19_MASK
    
#define XD_WP_OUTPUT_EN_REG				CARD_GPIO_ENABLE
#define XD_WP_OUTPUT_EN_MASK			PREG_IO_22_MASK
#define XD_WP_OUTPUT_REG				CARD_GPIO_OUTPUT
#define XD_WP_OUTPUT_MASK				PREG_IO_22_MASK
    
#define XD_INS_OUTPUT_EN_REG			CARD_GPIO_ENABLE
#define XD_INS_OUTPUT_EN_MASK			PREG_IO_20_MASK
#define XD_INS_INPUT_REG				CARD_GPIO_INPUT
#define XD_INS_INPUT_MASK				PREG_IO_20_MASK
    
#define XD_PWR_OUTPUT_EN_REG			CARD_GPIO_ENABLE
#define XD_PWR_OUTPUT_EN_MASK			PREG_IO_23_MASK
#define XD_PWR_OUTPUT_REG				CARD_GPIO_OUTPUT
#define XD_PWR_OUTPUT_MASK				PREG_IO_23_MASK
#define XD_PWR_EN_LEVEL					0
    
#define XD_WORK_MODE				CARD_SW_MODE
#define xd_gpio_enable()				{CLEAR_PERIPHS_REG_BITS(PERIPHS_PIN_MUX_2, 0x3F);CLEAR_PERIPHS_REG_BITS(SDIO_MULT_CONFIG, (1));}
    
#endif				//XD_IO_EXTERNAL
extern int i_GPIO_timer;

#define xd_set_dat0_7_input()				{(*(volatile unsigned *)XD_DAT_OUTPUT_EN_REG) |= XD_DAT0_7_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define xd_set_dat0_7_output()				{(*(volatile unsigned *)XD_DAT_OUTPUT_EN_REG) &= (~XD_DAT0_7_OUTPUT_EN_MASK);}
#define xd_set_dat0_7_value(data)			{(*(volatile unsigned *)XD_DAT_OUTPUT_REG) = (((*(volatile unsigned *)XD_DAT_OUTPUT_REG) & (~XD_DAT0_7_OUTPUT_MASK)) | (data << XD_DAT_OUTPUT_OFFSET));}
#define xd_get_dat0_7_value()				(((*(volatile unsigned *)XD_DAT_INPUT_REG) & XD_DAT0_7_INPUT_MASK) >> XD_DAT_INPUT_OFFSET)
    
#define xd_set_rb_input()       			{(*(volatile unsigned *)XD_RB_OUTPUT_EN_REG) |= XD_RB_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define xd_get_rb_value()       			((*(volatile unsigned *)XD_RB_INPUT_REG & XD_RB_INPUT_MASK)?1:0)
    
#define xd_set_re_output()					{(*(volatile unsigned *)XD_RE_OUTPUT_EN_REG) &= (~XD_RE_OUTPUT_EN_MASK);}
#define xd_set_re_high()        			{(*(volatile unsigned *)XD_RE_OUTPUT_REG) |= XD_RE_OUTPUT_MASK;}
#define xd_set_re_low()         			{(*(volatile unsigned *)XD_RE_OUTPUT_REG) &= (~XD_RE_OUTPUT_MASK);}
    
#define xd_set_ce_output()					{(*(volatile unsigned *)XD_CE_OUTPUT_EN_REG) &= (~XD_CE_OUTPUT_EN_MASK);}
#define xd_set_ce_high()        			{(*(volatile unsigned *)XD_CE_OUTPUT_REG) |= XD_CE_OUTPUT_MASK;}
#define xd_set_ce_low()         			{(*(volatile unsigned *)XD_CE_OUTPUT_REG) &= (~XD_CE_OUTPUT_MASK);}
    
#define xd_set_ale_output()					{(*(volatile unsigned *)XD_ALE_OUTPUT_EN_REG) &= (~XD_ALE_OUTPUT_EN_MASK);}
#define xd_set_ale_high()       			{(*(volatile unsigned *)XD_ALE_OUTPUT_REG) |= XD_ALE_OUTPUT_MASK;}
#define xd_set_ale_low()        			{(*(volatile unsigned *)XD_ALE_OUTPUT_REG) &= (~XD_ALE_OUTPUT_MASK);}
    
#define xd_set_cle_output()					{(*(volatile unsigned *)XD_CLE_OUTPUT_EN_REG) &= (~XD_CLE_OUTPUT_EN_MASK);}
#define xd_set_cle_high()       			{(*(volatile unsigned *)XD_CLE_OUTPUT_REG) |= XD_CLE_OUTPUT_MASK;}
#define xd_set_cle_low()        			{(*(volatile unsigned *)XD_CLE_OUTPUT_REG) &= (~XD_CLE_OUTPUT_MASK);}
    
#define xd_set_we_output()					{(*(volatile unsigned *)XD_WE_OUTPUT_EN_REG) &= (~XD_WE_OUTPUT_EN_MASK);}
#define xd_set_we_high()        			{(*(volatile unsigned *)XD_WE_OUTPUT_REG) |= XD_WE_OUTPUT_MASK;}
#define xd_set_we_low()         			{(*(volatile unsigned *)XD_WE_OUTPUT_REG) &= (~XD_WE_OUTPUT_MASK);}
    
#define xd_set_wp_output()      			{(*(volatile unsigned *)XD_WP_OUTPUT_EN_REG) &= (~XD_WP_OUTPUT_EN_MASK);}
#define xd_set_wp_high()        			{(*(volatile unsigned *)XD_WP_OUTPUT_REG) |= XD_WP_OUTPUT_MASK;}
#define xd_set_wp_low()         			{(*(volatile unsigned *)XD_WP_OUTPUT_REG) &= (~XD_WP_OUTPUT_MASK);}
    
#define	xd_set_ins_input()					{(*(volatile unsigned *)XD_INS_OUTPUT_EN_REG) |= XD_INS_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define xd_get_ins_value()					(((*(volatile unsigned *)XD_INS_INPUT_REG) & XD_INS_INPUT_MASK)?1:0)
    
#define XD_POWER_CONTROL                	
    
#ifdef XD_POWER_CONTROL                 	
#define xd_set_enable()         		{(*(volatile unsigned *)XD_PWR_OUTPUT_EN_REG) &= (~XD_PWR_OUTPUT_EN_MASK); if(XD_PWR_EN_LEVEL){*(volatile unsigned *)XD_PWR_OUTPUT_REG |= XD_PWR_OUTPUT_MASK;} else {*(volatile unsigned *)XD_PWR_OUTPUT_REG &= ~XD_PWR_OUTPUT_MASK;}}
#define xd_set_disable()        		{(*(volatile unsigned *)XD_PWR_OUTPUT_EN_REG) &= (~XD_PWR_OUTPUT_EN_MASK); if(XD_PWR_EN_LEVEL){*(volatile unsigned *)XD_PWR_OUTPUT_REG &= ~XD_PWR_OUTPUT_MASK;} else {*(volatile unsigned *)XD_PWR_OUTPUT_REG |= XD_PWR_OUTPUT_MASK;}}
#endif				/*  */
    
#endif				//_H_XD_PORT_COMMON
