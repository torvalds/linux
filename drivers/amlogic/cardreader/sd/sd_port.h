#ifndef _H_SD_PORT_COMMON
#define _H_SD_PORT_COMMON

#include <mach/io.h>
#include <mach/am_regs.h>
#include <mach/card_io.h>
    
//Following I/O configurations are just for default case if no any known PCB defined
    
#define SD_IO_EXTERNAL
    
//Port operation for SD BUS
//write it as such form that could be replaced by function later if needed
    
#ifdef SD_IO_EXTERNAL

extern unsigned SD_CMD_OUTPUT_EN_REG;
extern unsigned SD_CMD_OUTPUT_EN_MASK;
extern unsigned SD_CMD_INPUT_REG;
extern unsigned SD_CMD_INPUT_MASK;
extern unsigned SD_CMD_OUTPUT_REG;
extern unsigned SD_CMD_OUTPUT_MASK;

extern unsigned SD_CLK_OUTPUT_EN_REG;
extern unsigned SD_CLK_OUTPUT_EN_MASK;
extern unsigned SD_CLK_OUTPUT_REG;
extern unsigned SD_CLK_OUTPUT_MASK;


extern unsigned SD_DAT_OUTPUT_EN_REG;
extern unsigned SD_DAT0_OUTPUT_EN_MASK;
extern unsigned SD_DAT0_3_OUTPUT_EN_MASK;
extern unsigned SD_DAT_INPUT_REG;
extern unsigned SD_DAT_OUTPUT_REG;
extern unsigned SD_DAT0_INPUT_MASK;
extern unsigned SD_DAT0_OUTPUT_MASK;
extern unsigned SD_DAT0_3_INPUT_MASK;
extern unsigned SD_DAT0_3_OUTPUT_MASK;
extern unsigned SD_DAT_INPUT_OFFSET;
extern unsigned SD_DAT_OUTPUT_OFFSET;


extern unsigned SD_INS_OUTPUT_EN_REG;
extern unsigned SD_INS_OUTPUT_EN_MASK;
extern unsigned SD_INS_INPUT_REG;
extern unsigned SD_INS_INPUT_MASK;


extern unsigned SD_WP_OUTPUT_EN_REG;
extern unsigned SD_WP_OUTPUT_EN_MASK;
extern unsigned SD_WP_INPUT_REG;
extern unsigned SD_WP_INPUT_MASK;

extern unsigned SD_PWR_OUTPUT_EN_REG;
extern unsigned SD_PWR_OUTPUT_EN_MASK;
extern unsigned SD_PWR_OUTPUT_REG;
extern unsigned SD_PWR_OUTPUT_MASK;
extern unsigned SD_PWR_EN_LEVEL;

extern unsigned SD_WORK_MODE;
#define SD_MMC_POWER_CONTROL
//#define SD_MMC_WP_CHECK

extern void sd_sdio_enable(SDIO_Pad_Type_t io_pad_type);
extern void sd_gpio_enable(SDIO_Pad_Type_t io_pad_type);
extern void sd_gpio_enable_sdioa(void);

#else				//SD_IO_EXTERNAL

#if defined(CONFIG_MACH_MESON_8626M)   
	#define SD_CMD_OUTPUT_EN_REG 		EGPIO_GPIOA_ENABLE
	#define SD_CMD_OUTPUT_EN_MASK		PREG_IO_18_MASK
	#define SD_CMD_INPUT_REG			EGPIO_GPIOA_INPUT
	#define SD_CMD_INPUT_MASK			PREG_IO_18_MASK
	#define SD_CMD_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_CMD_OUTPUT_MASK			PREG_IO_18_MASK
	    
	#define SD_CLK_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_CLK_OUTPUT_EN_MASK		PREG_IO_17_MASK
	#define SD_CLK_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_CLK_OUTPUT_MASK			PREG_IO_17_MASK
	    
	#define SD_DAT_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_DAT0_OUTPUT_EN_MASK		PREG_IO_13_MASK
	#define SD_DAT0_3_OUTPUT_EN_MASK	PREG_IO_13_16_MASK
	#define SD_DAT_INPUT_REG			EGPIO_GPIOA_INPUT
	#define SD_DAT_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_DAT0_INPUT_MASK			PREG_IO_13_MASK
	#define SD_DAT0_OUTPUT_MASK			PREG_IO_13_MASK
	#define SD_DAT0_3_INPUT_MASK		PREG_IO_13_16_MASK
	#define SD_DAT0_3_OUTPUT_MASK		PREG_IO_13_16_MASK
	#define SD_DAT_INPUT_OFFSET			13
	#define SD_DAT_OUTPUT_OFFSET		13
	    
	#define SD_INS_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_INS_OUTPUT_EN_MASK		PREG_IO_11_MASK
	#define SD_INS_INPUT_REG			EGPIO_GPIOA_INPUT
	#define SD_INS_INPUT_MASK			PREG_IO_11_MASK
	    
	#define SD_WP_OUTPUT_EN_REG			EGPIO_GPIOA_ENABLE
	#define SD_WP_OUTPUT_EN_MASK		PREG_IO_12_MASK
	#define SD_WP_INPUT_REG				EGPIO_GPIOA_INPUT
	#define SD_WP_INPUT_MASK			PREG_IO_12_MASK
	    
	#define SD_PWR_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_PWR_OUTPUT_EN_MASK		PREG_IO_9_MASK
	#define SD_PWR_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_PWR_OUTPUT_MASK			PREG_IO_9_MASK
	#define SD_PWR_EN_LEVEL				0
	    
	#define SD_WORK_MODE				CARD_HW_MODE
	    
	#define SD_MMC_POWER_CONTROL
//	#define SD_MMC_WP_CHECK
	    
	#define sd_sdio_enable()			{SET_CBUS_REG_MASK(CARD_PIN_MUX_0, (0x3F<<23));SET_CBUS_REG_MASK(SDIO_MULT_CONFIG, (0));}
	#define sd_gpio_enable()			{CLEAR_CBUS_REG_MASK(CARD_PIN_MUX_0, (0x3F<<23));CLEAR_CBUS_REG_MASK(SDIO_MULT_CONFIG, (0));}
#elif defined (CONFIG_MACH_MESON_6236M)
	#define SD_CMD_OUTPUT_EN_REG 		EGPIO_GPIOA_ENABLE
	#define SD_CMD_OUTPUT_EN_MASK		PREG_IO_18_MASK
	#define SD_CMD_INPUT_REG			EGPIO_GPIOA_INPUT
	#define SD_CMD_INPUT_MASK			PREG_IO_18_MASK
	#define SD_CMD_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_CMD_OUTPUT_MASK			PREG_IO_18_MASK
	    
	#define SD_CLK_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_CLK_OUTPUT_EN_MASK		PREG_IO_17_MASK
	#define SD_CLK_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_CLK_OUTPUT_MASK			PREG_IO_17_MASK
	    
	#define SD_DAT_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_DAT0_OUTPUT_EN_MASK		PREG_IO_13_MASK
	#define SD_DAT0_3_OUTPUT_EN_MASK	PREG_IO_13_16_MASK
	#define SD_DAT_INPUT_REG			EGPIO_GPIOA_INPUT
	#define SD_DAT_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_DAT0_INPUT_MASK			PREG_IO_13_MASK
	#define SD_DAT0_OUTPUT_MASK			PREG_IO_13_MASK
	#define SD_DAT0_3_INPUT_MASK		PREG_IO_13_16_MASK
	#define SD_DAT0_3_OUTPUT_MASK		PREG_IO_13_16_MASK
	#define SD_DAT_INPUT_OFFSET			13
	#define SD_DAT_OUTPUT_OFFSET		13
	
	#define SD_INS_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_INS_OUTPUT_EN_MASK		PREG_IO_3_MASK
	#define SD_INS_INPUT_REG			EGPIO_GPIOA_INPUT
	#define SD_INS_INPUT_MASK			PREG_IO_3_MASK
	    
	#define SD_WP_OUTPUT_EN_REG			EGPIO_GPIOA_ENABLE
	#define SD_WP_OUTPUT_EN_MASK		PREG_IO_11_MASK
	#define SD_WP_INPUT_REG				EGPIO_GPIOA_INPUT
	#define SD_WP_INPUT_MASK			PREG_IO_11_MASK
	    
	#define SD_PWR_OUTPUT_EN_REG		JTAG_GPIO_ENABLE
	#define SD_PWR_OUTPUT_EN_MASK		PREG_IO_16_MASK
	#define SD_PWR_OUTPUT_REG			JTAG_GPIO_OUTPUT
	#define SD_PWR_OUTPUT_MASK			PREG_IO_20_MASK
	#define SD_PWR_EN_LEVEL				0
	    
	#define SD_WORK_MODE				CARD_HW_MODE
	    
	#define SD_MMC_POWER_CONTROL
	    	
//	#define SD_MMC_WP_CHECK
	  
	#define sd_sdio_enable()			{SET_CBUS_REG_MASK(CARD_PIN_MUX_0, (0x3F<<23));SET_CBUS_REG_MASK(SDIO_MULT_CONFIG, (0));}
	#define sd_gpio_enable()			{CLEAR_CBUS_REG_MASK(CARD_PIN_MUX_0, (0x3F<<23));CLEAR_CBUS_REG_MASK(SDIO_MULT_CONFIG, (0));}
#else
	#define SD_CMD_OUTPUT_EN_REG 		EGPIO_GPIOA_ENABLE
	#define SD_CMD_OUTPUT_EN_MASK		PREG_IO_18_MASK
	#define SD_CMD_INPUT_REG			EGPIO_GPIOA_INPUT
	#define SD_CMD_INPUT_MASK			PREG_IO_18_MASK
	#define SD_CMD_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_CMD_OUTPUT_MASK			PREG_IO_18_MASK
	    
	#define SD_CLK_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_CLK_OUTPUT_EN_MASK		PREG_IO_17_MASK
	#define SD_CLK_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_CLK_OUTPUT_MASK			PREG_IO_17_MASK
	    
	#define SD_DAT_OUTPUT_EN_REG		EGPIO_GPIOA_ENABLE
	#define SD_DAT0_OUTPUT_EN_MASK		PREG_IO_13_MASK
	#define SD_DAT0_3_OUTPUT_EN_MASK	PREG_IO_13_16_MASK
	#define SD_DAT_INPUT_REG			EGPIO_GPIOA_INPUT
	#define SD_DAT_OUTPUT_REG			EGPIO_GPIOA_OUTPUT
	#define SD_DAT0_INPUT_MASK			PREG_IO_13_MASK
	#define SD_DAT0_OUTPUT_MASK			PREG_IO_13_MASK
	#define SD_DAT0_3_INPUT_MASK		PREG_IO_13_16_MASK
	#define SD_DAT0_3_OUTPUT_MASK		PREG_IO_13_16_MASK
	#define SD_DAT_INPUT_OFFSET			13
	#define SD_DAT_OUTPUT_OFFSET		13
	    
	#define SD_INS_OUTPUT_EN_REG		EGPIO_GPIOC_ENABLE
	#define SD_INS_OUTPUT_EN_MASK		PREG_IO_22_MASK
	#define SD_INS_INPUT_REG			EGPIO_GPIOC_INPUT
	#define SD_INS_INPUT_MASK			PREG_IO_22_MASK
	    
	#define SD_WP_OUTPUT_EN_REG			EGPIO_GPIOC_ENABLE
	#define SD_WP_OUTPUT_EN_MASK		PREG_IO_21_MASK
	#define SD_WP_INPUT_REG				EGPIO_GPIOC_INPUT
	#define SD_WP_INPUT_MASK			PREG_IO_21_MASK
	    
	#define SD_PWR_OUTPUT_EN_REG		JTAG_GPIO_ENABLE
	#define SD_PWR_OUTPUT_EN_MASK		PREG_IO_16_MASK
	#define SD_PWR_OUTPUT_REG			JTAG_GPIO_OUTPUT
	#define SD_PWR_OUTPUT_MASK			PREG_IO_20_MASK
	#define SD_PWR_EN_LEVEL				0
	    
	#define SD_WORK_MODE				CARD_HW_MODE
	    
	#define SD_MMC_POWER_CONTROL
//	#define SD_MMC_WP_CHECK
	  
	#define sd_sdio_enable()			{SET_CBUS_REG_MASK(CARD_PIN_MUX_0, (0x3F<<23));SET_CBUS_REG_MASK(SDIO_MULT_CONFIG, (0));}
	#define sd_gpio_enable()			{CLEAR_CBUS_REG_MASK(CARD_PIN_MUX_0, (0x3F<<23));CLEAR_CBUS_REG_MASK(SDIO_MULT_CONFIG, (0));}
#endif    
#endif				//SD_IO_EXTERNAL

extern void (*sd_mmc_power_register) (int power_on);
extern int (*sd_mmc_ins_register) (void);
extern int (*sd_mmc_wp_register) (void);
extern void (*sd_mmc_io_release_register) (void);

extern int i_GPIO_timer;

#define sd_set_cmd_input()				{(*(volatile unsigned int *)SD_CMD_OUTPUT_EN_REG) |= SD_CMD_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define sd_set_cmd_output()				{(*(volatile unsigned int *)SD_CMD_OUTPUT_EN_REG) &= (~SD_CMD_OUTPUT_EN_MASK);}
#define sd_set_cmd_value(data)			{if(data){(*(volatile unsigned int *)SD_CMD_OUTPUT_REG) |= SD_CMD_OUTPUT_MASK;}else{(*(volatile unsigned int *)SD_CMD_OUTPUT_REG) &= (~SD_CMD_OUTPUT_MASK);}}
#define sd_get_cmd_value()				((*(volatile unsigned int *)SD_CMD_INPUT_REG & SD_CMD_INPUT_MASK)?1:0)

#define sd_set_clk_input()				{(*(volatile unsigned int *)SD_CLK_OUTPUT_EN_REG) |= SD_CLK_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}    
#define sd_set_clk_output()    			{(*(volatile unsigned int *)SD_CLK_OUTPUT_EN_REG) &= (~SD_CLK_OUTPUT_EN_MASK);}
#define sd_set_clk_high()				{(*(volatile unsigned int *)SD_CLK_OUTPUT_REG) |= SD_CLK_OUTPUT_MASK;}
#define sd_set_clk_low()				{(*(volatile unsigned int *)SD_CLK_OUTPUT_REG) &= (~SD_CLK_OUTPUT_MASK);}
    
#define sd_set_dat0_input()				{(*(volatile unsigned int *)SD_DAT_OUTPUT_EN_REG) |= SD_DAT0_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define sd_set_dat0_output()			{(*(volatile unsigned int *)SD_DAT_OUTPUT_EN_REG) &= (~SD_DAT0_OUTPUT_EN_MASK);}
#define sd_set_dat0_value(data)			{if(data){*(volatile unsigned int *)SD_DAT_OUTPUT_REG |= SD_DAT0_OUTPUT_MASK;}else{*(volatile unsigned int *)SD_DAT_OUTPUT_REG &= (~SD_DAT0_OUTPUT_MASK);}}
#define sd_get_dat0_value()				((*(volatile unsigned int *)SD_DAT_INPUT_REG & SD_DAT0_INPUT_MASK)?1:0)
    
#define sd_set_dat0_3_input()			{(*(volatile unsigned int *)SD_DAT_OUTPUT_EN_REG) |= (SD_DAT0_3_OUTPUT_EN_MASK); for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define sd_set_dat0_3_output()			{(*(volatile unsigned int *)SD_DAT_OUTPUT_EN_REG) &= (~SD_DAT0_3_OUTPUT_EN_MASK);}
#define sd_set_dat0_3_value(data)		{(*(volatile unsigned int *)SD_DAT_OUTPUT_REG) = ((*(volatile unsigned int *)SD_DAT_OUTPUT_REG) & ((~SD_DAT0_3_OUTPUT_MASK) | (data << SD_DAT_OUTPUT_OFFSET)));}
#define sd_get_dat0_3_value()			((*(volatile unsigned int *)SD_DAT_INPUT_REG & SD_DAT0_3_INPUT_MASK) >> SD_DAT_OUTPUT_OFFSET)
    
#define	sd_set_ins_input()				{(*(volatile unsigned int *)SD_INS_OUTPUT_EN_REG) |= SD_INS_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define sd_get_ins_value()				((*(volatile unsigned int *)SD_INS_INPUT_REG & SD_INS_INPUT_MASK)?1:0)
    
//#define SD_MMC_WP_CHECK
    
#ifdef SD_MMC_WP_CHECK
#define sd_set_wp_input()           {(*(volatile unsigned int *)SD_WP_OUTPUT_EN_REG) |= SD_WP_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#define sd_get_wp_value()           ((*(volatile unsigned int *)SD_WP_INPUT_REG & SD_WP_INPUT_MASK)?1:0)
#endif				/* 
 */
    
#define SD_MMC_POWER_CONTROL
    
#ifdef SD_MMC_POWER_CONTROL
#define sd_set_enable()         	{(*(volatile unsigned int *)SD_PWR_OUTPUT_EN_REG) &= (~SD_PWR_OUTPUT_EN_MASK); if(SD_PWR_EN_LEVEL){(*(volatile unsigned int *)SD_PWR_OUTPUT_REG) |= SD_PWR_OUTPUT_MASK;} else {(*(volatile unsigned int *)SD_PWR_OUTPUT_REG) &= (~SD_PWR_OUTPUT_MASK);}}
#define sd_set_disable()        	{(*(volatile unsigned int *)SD_PWR_OUTPUT_EN_REG) &= (~SD_PWR_OUTPUT_EN_MASK); if(SD_PWR_EN_LEVEL){(*(volatile unsigned int *)SD_PWR_OUTPUT_REG) &= (~SD_PWR_OUTPUT_MASK);} else {(*(volatile unsigned int *)SD_PWR_OUTPUT_REG) |= SD_PWR_OUTPUT_MASK;}}
#define sd_set_pwr_input()     {(*(volatile unsigned int *)SD_PWR_OUTPUT_EN_REG) |= SD_PWR_OUTPUT_EN_MASK; for(i_GPIO_timer=0;i_GPIO_timer<15;i_GPIO_timer++);}
#endif				/* 
 */
    
#endif				//_H_SD_PORT_COMMON
