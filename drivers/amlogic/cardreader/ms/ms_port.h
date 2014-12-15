#ifndef _H_MS_PORT_COMMON
#define _H_MS_PORT_COMMON

#include <asm/types.h>
#include <asm/io.h>
#include <mach/am_regs.h>
#include <mach/card_io.h>
    
//Following I/O configurations are just for default case if no any known PCB defined
    
#define MS_IO_EXTERNAL
    
//Port operation for MS BUS
//write it as such form that could be replaced by function later if needed
extern int i_GPIO_timer;
extern void (*ms_mspro_power_register) (int power_on);
extern int (*ms_mspro_ins_register) (void);
extern int (*ms_mspro_wp_register) (void);
extern void (*ms_mspro_io_release_register) (void);

extern void sd_sdio_enable(SDIO_Pad_Type_t io_pad_type);
extern void sd_gpio_enable(SDIO_Pad_Type_t io_pad_type);

#ifdef MS_IO_EXTERNAL
extern unsigned MS_BS_OUTPUT_EN_REG;
extern unsigned MS_BS_OUTPUT_EN_MASK;
extern unsigned MS_BS_OUTPUT_REG;
extern unsigned MS_BS_OUTPUT_MASK;
extern unsigned MS_CLK_OUTPUT_EN_REG;
extern unsigned MS_CLK_OUTPUT_EN_MASK;
extern unsigned MS_CLK_OUTPUT_REG;
extern unsigned MS_CLK_OUTPUT_MASK;
extern unsigned MS_DAT_OUTPUT_EN_REG;
extern unsigned MS_DAT0_OUTPUT_EN_MASK;
extern unsigned MS_DAT0_3_OUTPUT_EN_MASK;
extern unsigned MS_DAT_INPUT_REG;
extern unsigned MS_DAT_OUTPUT_REG;
extern unsigned MS_DAT0_INPUT_MASK;
extern unsigned MS_DAT0_OUTPUT_MASK;
extern unsigned MS_DAT0_3_INPUT_MASK;
extern unsigned MS_DAT0_3_OUTPUT_MASK;
extern unsigned MS_DAT_INPUT_OFFSET;
extern unsigned MS_DAT_OUTPUT_OFFSET;
extern unsigned MS_INS_OUTPUT_EN_REG;
extern unsigned MS_INS_OUTPUT_EN_MASK;
extern unsigned MS_INS_INPUT_REG;
extern unsigned MS_INS_INPUT_MASK;
extern unsigned MS_PWR_OUTPUT_EN_REG;
extern unsigned MS_PWR_OUTPUT_EN_MASK;
extern unsigned MS_PWR_OUTPUT_REG;
extern unsigned MS_PWR_OUTPUT_MASK;
extern unsigned MS_PWR_EN_LEVEL;
extern unsigned MS_WORK_MODE;

    //extern void (*ms_mspro_power_register)(int power_on);
    //extern int (*ms_mspro_ins_register)(void);
    //extern void (*ms_mspro_io_release_register)(void);
    
#define MS_MSPRO_POWER_CONTROL
//void ms_sdio_enable(void);
//void ms_gpio_enable(void);
#define ms_sdio_enable sd_sdio_enable
#define ms_gpio_enable sd_gpio_enable

#else				//MS_IO_EXTERNAL
    
#define MS_BS_OUTPUT_EN_REG				CARD_GPIO_ENABLE
#define MS_BS_OUTPUT_EN_MASK			PREG_IO_19_MASK 
#define MS_BS_OUTPUT_REG				CARD_GPIO_OUTPUT 
#define MS_BS_OUTPUT_MASK				PREG_IO_19_MASK 
    
#define MS_CLK_OUTPUT_EN_REG			CARD_GPIO_ENABLE
#define MS_CLK_OUTPUT_EN_MASK			PREG_IO_18_MASK 
#define MS_CLK_OUTPUT_REG				CARD_GPIO_OUTPUT
#define MS_CLK_OUTPUT_MASK				PREG_IO_18_MASK 
    
#define MS_DAT_OUTPUT_EN_REG			CARD_GPIO_ENABLE   
#define MS_DAT0_OUTPUT_EN_MASK			PREG_IO_13_MASK    
#define MS_DAT0_3_OUTPUT_EN_MASK		PREG_IO_13_16_MASK 
#define MS_DAT_INPUT_REG				CARD_GPIO_INPUT    
#define MS_DAT_OUTPUT_REG				CARD_GPIO_OUTPUT   
#define MS_DAT0_INPUT_MASK				PREG_IO_13_MASK    
#define MS_DAT0_OUTPUT_MASK				PREG_IO_13_MASK    
#define MS_DAT0_3_INPUT_MASK			PREG_IO_13_16_MASK 
#define MS_DAT0_3_OUTPUT_MASK			PREG_IO_13_16_MASK 
#define MS_DAT_INPUT_OFFSET				13                 
#define MS_DAT_OUTPUT_OFFSET			13                 
    
#define MS_INS_OUTPUT_EN_REG			CARD_GPIO_ENABLE
#define MS_INS_OUTPUT_EN_MASK			PREG_IO_20_MASK 
#define MS_INS_INPUT_REG				CARD_GPIO_INPUT 
#define MS_INS_INPUT_MASK				PREG_IO_20_MASK 
    
#define MS_PWR_OUTPUT_EN_REG			CARD_GPIO_ENABLE 
#define MS_PWR_OUTPUT_EN_MASK			PREG_IO_23_MASK  
#define MS_PWR_OUTPUT_REG				CARD_GPIO_OUTPUT 
#define MS_PWR_OUTPUT_MASK				PREG_IO_23_MASK  
#define MS_PWR_EN_LEVEL             	0                
    
#define MS_WORK_MODE					CARD_HW_MODE     
    
#define MS_MSPRO_POWER_CONTROL          
    
#define ms_sdio_enable()			{SET_CBUS_REG_MASK(CARD_PIN_MUX_0, (0x3F<<23));SET_CBUS_REG_MASK(SDIO_MULT_CONFIG, (0));}
#define ms_gpio_enable()			{CLEAR_CBUS_REG_MASK(CARD_PIN_MUX_0, (0x3F<<23));CLEAR_CBUS_REG_MASK(SDIO_MULT_CONFIG, (0));}
    
#endif				//MS_IO_EXTERNAL                     
    
#define ms_set_bs_output()					{(*(volatile unsigned *)MS_BS_OUTPUT_EN_REG) &= ~MS_BS_OUTPUT_EN_MASK;}
#define ms_set_bs_high()					{(*(volatile unsigned *)MS_BS_OUTPUT_REG) |= MS_BS_OUTPUT_MASK;}
#define ms_set_bs_low()			 			{(*(volatile unsigned *)MS_BS_OUTPUT_REG) &= ~MS_BS_OUTPUT_MASK;}
#define ms_set_bs_state(number)				{if(number&0x01){ms_set_bs_high()}else{ms_set_bs_low()}}
    
#define ms_set_clk_output()					{(*(volatile unsigned *)MS_CLK_OUTPUT_EN_REG) &= ~MS_CLK_OUTPUT_EN_MASK;}
#define ms_set_clk_high()					{(*(volatile unsigned *)MS_CLK_OUTPUT_REG) |= MS_CLK_OUTPUT_MASK;}
#define ms_set_clk_low()					{(*(volatile unsigned *)MS_CLK_OUTPUT_REG) &= ~MS_CLK_OUTPUT_MASK;}
    
#define ms_set_dat0_input()					{(*(volatile unsigned *)MS_DAT_OUTPUT_EN_REG) |= MS_DAT0_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define ms_set_dat0_output()				{(*(volatile unsigned *)MS_DAT_OUTPUT_EN_REG) &= ~MS_DAT0_OUTPUT_EN_MASK;}
#define ms_set_dat0_value(data)				{if(data){*(volatile unsigned *)MS_DAT_OUTPUT_REG |= MS_DAT0_OUTPUT_MASK;}else{*(volatile unsigned *)MS_DAT_OUTPUT_REG &= ~MS_DAT0_OUTPUT_MASK;}}
#define ms_get_dat0_value()					(((*(volatile unsigned *)MS_DAT_INPUT_REG) & MS_DAT0_INPUT_MASK)?1:0)
    
#define ms_set_dat0_3_input()				{(*(volatile unsigned *)MS_DAT_OUTPUT_EN_REG) |= MS_DAT0_3_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define ms_set_dat0_3_output()				{(*(volatile unsigned *)MS_DAT_OUTPUT_EN_REG) &= ~MS_DAT0_3_OUTPUT_EN_MASK;}
#define ms_set_dat0_3_value(data)			{(*(volatile unsigned *)MS_DAT_OUTPUT_REG) = (((*(volatile unsigned *)MS_DAT_OUTPUT_REG) & (~MS_DAT0_3_OUTPUT_MASK)) | (data << MS_DAT_OUTPUT_OFFSET));}
#define ms_get_dat0_3_value()				(((*(volatile unsigned *)MS_DAT_INPUT_REG) & MS_DAT0_3_INPUT_MASK) >> MS_DAT_INPUT_OFFSET)
    
#define	ms_set_ins_input()					{(*(volatile unsigned *)MS_INS_OUTPUT_EN_REG) |= MS_INS_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++); }
#define ms_get_ins_value()					((*(volatile unsigned *)MS_INS_INPUT_REG & MS_INS_INPUT_MASK)?1:0)
    
#define MS_MSPRO_POWER_CONTROL
    
#ifdef MS_MSPRO_POWER_CONTROL
#define ms_set_enable()						{(*(volatile unsigned *)MS_PWR_OUTPUT_EN_REG) &= ~MS_PWR_OUTPUT_EN_MASK; if(MS_PWR_EN_LEVEL){*(volatile unsigned *)MS_PWR_OUTPUT_REG |= MS_PWR_OUTPUT_MASK;} else {*(volatile unsigned *)MS_PWR_OUTPUT_REG &= ~MS_PWR_OUTPUT_MASK;}}
#define ms_set_disable()					{(*(volatile unsigned *)MS_PWR_OUTPUT_EN_REG) &= ~MS_PWR_OUTPUT_EN_MASK; if(MS_PWR_EN_LEVEL){*(volatile unsigned *)MS_PWR_OUTPUT_REG &= ~MS_PWR_OUTPUT_MASK;} else {*(volatile unsigned *)MS_PWR_OUTPUT_REG |= MS_PWR_OUTPUT_MASK;}}
#endif				/*  */
    
#endif				//_H_MS_PORT_COMMON
