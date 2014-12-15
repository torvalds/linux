#ifndef _H_CF_PORT_COMMON
#define _H_CF_PORT_COMMON

#include <asm/types.h>
#include <asm/io.h>
#include <asm/arch/am_regs.h>
#include <asm/drivers/cardreader/card_io.h>
    
//Following I/O configurations are just for default case if no any known PCB defined
    
#define CF_IO_EXTERNAL
    
//Port operation for CF BUS
//write it as such form that could be replaced by function later if needed
    
#ifdef CF_IO_EXTERNAL
extern unsigned CF_RST_OUTPUT_EN_REG;
extern unsigned CF_RST_OUTPUT_EN_MASK;
extern unsigned CF_RST_OUTPUT_REG;
extern unsigned CF_RST_OUTPUT_MASK;
extern unsigned CF_INS_OUTPUT_EN_REG;
extern unsigned CF_INS_OUTPUT_EN_MASK;
extern unsigned CF_INS_INPUT_REG;
extern unsigned CF_INS_INPUT_MASK;
extern unsigned CF_PWR_OUTPUT_EN_REG;
extern unsigned CF_PWR_OUTPUT_EN_MASK;
extern unsigned CF_PWR_OUTPUT_REG;
extern unsigned CF_PWR_OUTPUT_MASK;
extern unsigned CF_PWR_EN_LEVEL;

#define CF_POWER_CONTROL
void cf_atapi_enable(void);

#else				//CF_IO_EXTERNAL
    
#define CF_RST_OUTPUT_EN_REG			GPIOD1_GPIO_ENABLE
#define CF_RST_OUTPUT_EN_MASK			PREG_IO_3_MASK
#define CF_RST_OUTPUT_REG				GPIOD1_GPIO_OUTPUT
#define CF_RST_OUTPUT_MASK				PREG_IO_3_MASK
    
#define CF_INS_OUTPUT_EN_REG			GPIOD1_GPIO_ENABLE
#define CF_INS_OUTPUT_EN_MASK			PREG_IO_4_MASK
#define CF_INS_INPUT_REG				GPIOD1_GPIO_INPUT
#define CF_INS_INPUT_MASK				PREG_IO_4_MASK
    
#define CF_PWR_OUTPUT_EN_REG			CARD_GPIO_ENABLE
#define CF_PWR_OUTPUT_EN_MASK			PREG_IO_21_MASK
#define CF_PWR_OUTPUT_REG				CARD_GPIO_OUTPUT
#define CF_PWR_OUTPUT_MASK				PREG_IO_21_MASK
#define CF_PWR_EN_LEVEL           		0
    
#define CF_POWER_CONTROL
    
#define cf_atapi_enable()					{CLEAR_PERIPHS_REG_BITS(PERIPHS_PIN_MUX_2, 0x3F);CLEAR_PERIPHS_REG_BITS(PERIPHS_PIN_MUX_3, (1<<25));SET_PERIPHS_REG_BITS(PERIPHS_PIN_MUX_1, (1 << 28));}
    
#endif				//CF_IO_EXTERNAL
extern int i_GPIO_timer;
extern void (*cf_power_register) (int power_on);
extern void (*cf_reset_register) (int reset_high);
extern int (*cf_ins_register) (void);
extern void (*cf_io_release_register) (void);

#define cf_set_reset_output()				{*(volatile unsigned *)CF_RST_OUTPUT_EN_REG &= ~CF_RST_OUTPUT_EN_MASK;}
#define cf_set_reset_high()					{*(volatile unsigned *)CF_RST_OUTPUT_REG |= CF_RST_OUTPUT_MASK;}
#define cf_set_reset_low()					{*(volatile unsigned *)CF_RST_OUTPUT_REG &= ~CF_RST_OUTPUT_MASK;}
    
#define	cf_set_ins_input()					{*(volatile unsigned *)CF_INS_OUTPUT_EN_REG |= CF_INS_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define cf_get_ins_value()					((*(volatile unsigned *)CF_INS_INPUT_REG & CF_INS_INPUT_MASK)?1:0)
    
#define CF_POWER_CONTROL
    
#ifdef CF_POWER_CONTROL
#define cf_set_enable()         		{*(volatile unsigned *)CF_PWR_OUTPUT_EN_REG &= ~CF_PWR_OUTPUT_EN_MASK; if(CF_PWR_EN_LEVEL) {*(volatile unsigned *)CF_PWR_OUTPUT_REG |= CF_PWR_OUTPUT_MASK;} else {*(volatile unsigned *)CF_PWR_OUTPUT_REG &= ~CF_PWR_OUTPUT_MASK;}}
#define cf_set_disable()        		{*(volatile unsigned *)CF_PWR_OUTPUT_EN_REG &= ~CF_PWR_OUTPUT_EN_MASK; if(CF_PWR_EN_LEVEL) {*(volatile unsigned *)CF_PWR_OUTPUT_REG &= ~CF_PWR_OUTPUT_MASK;} else {*(volatile unsigned *)CF_PWR_OUTPUT_REG |= CF_PWR_OUTPUT_MASK;}}
#endif				/*  */
    
#endif				//_H_CF_PORT_COMMON
