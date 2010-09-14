/*
 * rockchip-pcm.h - ALSA PCM interface for the Rockchip rk28 SoC
 *
 * Driver for rockchip iis audio
 *  Copyright (C) 2009 lhh
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ROCKCHIP_PM_H
#define _ROCKCHIP_PM_H

/***********************scu SCU_CLKSEL0 reg bit************************************/
#define PM_RESUME_BIT	0
#define PM_DEBUG_BIT	1
#define PM_SET_BIT(b) (1<<b)
#define PM_GET_BIT(val,b)	((val&(1<<b))>>b)

#define PM_BIT_CLEAR(off,b)	(~((1<<(b))-1)<<(off))


/***********************scu SCU_CLKSEL0 reg bit************************************/

#define SCU_GATE0CLK_ALL_EN 0
#define SCU_GATE0CLK_ALL_DIS 0XFFFFFFFF
#define DIS_RTC_PCLK (1<<27)
#define DIS_WDT_PCLK (1<<26)
#define DIS_TIMER_CLK (1<<25)
#define DIS_PWM_CLK (1<<24)
#define DIS_SPI1_CLK (1<<23)
#define DIS_SPI0_CLK (1<<22)
#define DIS_I2C1_CLK (1<<21)
#define DIS_I2C0_CLK (1<<20)
#define DIS_UART1_CLK (1<<19)
#define DIS_UART0_CLK (1<<18)
#define DIS_GPIO1_CLK (1<<17)
#define DIS_GPIO0_CLK (1<<16)
#define DIS_EMBEDED_ROM_CLK (1<<15)
#define DIS_LCDC_CLK (1<<11)
#define DIS_DEBLOCK_HCLK (1<<10)
#define DIS_INTC_CLK (1<<9)
#define DIS_NANDC_HCLK (1<<8)
#define DIS_DMA_CLK (1<<2)
#define DIS_DSP_CLK (1<<1)
#define DIS_ARM_CLK (1<<0)
/***********************scu SCU_CLKSEL1_CON reg bit************************************/

#define SCU_GATE1CLK_ALL_EN 0
//#define SCU_GATE1CLK_BASE_SET (0XFFFFFFFF&(~DIS_DDR_CLK)&(~DIS_DDR_HCLK)) // 注意AXI 位为1时为enable 

#define SCU_GATE1CLK_BASE_SET (0XFFFF8001&(~EN_AXI_CLK)) // 注意AXI 位为1时为enable 
//#define SCU_GATE1CLK_BASE_SET (0XFFFFFFFF)
#define EN_AXI_CLK (1<<27)
#define DIS_DDR_CLK (1<<23)
#define DIS_DDR_HCLK (1<<22)
#define DIS_LCDC_HCLK (1<<19)
#define DIS_LCDC_SHARE_MEM_CLK (1<<18)
#define DIS_MSDRAM_CTR_HCLK (1<<17)
#define DIS_SDRAM_CTR_HCLK (1<<16)
#define DIS_MAndSDRAM_CMM_HCLK (1<<15)
/***********************scu SCU_CLKSEL2_CON reg bit************************************/
#define SCU_GATE2CLK_ALL_EN 0
#define SCU_GATE2CLK_ALL_DIS 0X3FF
#define SCU_GATE2CLK_BASE_SET	(SCU_GATE2CLK_ALL_DIS&(~DIS_ARMIBUS_CLK)&(~DIS_ARMDBUS_CLK)&(~DIS_EXPAHBBUS_CLK)&(~DIS_APBBUS_CLK))

#define DIS_ITCMBUS_CLK      (1<<8)
#define DIS_DTCM0BUS_CLK      (1<<7)
#define DIS_DTCM1BUS_CLK      (1<<6)

#define DIS_APBBUS_CLK      (1<<4)
#define DIS_EXPAHBBUS_CLK	(1<<3)
#define DIS_ARMDBUS_CLK      (1<<1)
#define DIS_ARMIBUS_CLK	(1<<0)
/***********************scu SCU_APLL_CON reg bit************************************/
#define ARMPLL_POERDOWN (1<<22)
#define ARMPLL_BYPASSMODE (1<<0)
/***********************scu SCU_DPLL_CON reg bit************************************/

#define DSPPLL_POERDOWN (1<<22)

/***********************scu SCU_CPLL_CON reg bit************************************/
#define CPLL_POERDOWN (1<<22)

/***********************scu PM_SCU_MODE_CON reg bit************************************/
#define CPU_SLOW_MODE (~(3<<2))


/***********************scu SCU_PMU_CON reg bit************************************/

#define LCDC_POWER_DOWN (1<<3)
#define DSP_POWER_DOWN (1<<0)
 #define DDR_POWER_DOWN (1<<2)


/***********************scu PM_SCU_CLKSEL0_CON reg bit************************************/
#define CLKSEL0_HCLK (0)
#define CLKSEL0_PCLK (2)

#define CLKSEL0_HCLK21 (0x01<CLKSEL0_HCLK)
#define CLKSEL0_PCLK21 (0x01<CLKSEL0_PCLK)

/***********************scu PM_SCU_SOFTRST_CON] reg bit************************************/
#define RST_ALL 0xFFFFFFFF

#define RST_DDR_BUS (1<<31)
#define RST_DDR_CORE_LOGIC (1<<30)

#define RST_ARM (1<<23)

enum
{
	PM_SCU_APLL_CON,
	PM_SCU_DPLL_CON,
	PM_SCU_CPLL_CON,
	PM_SCU_MODE_CON,
	PM_SCU_PMU_CON,
	PM_SCU_CLKSEL0_CON,
	PM_SCU_CLKSEL1_CON,
	PM_SCU_CLKGATE0_CON,
	PM_SCU_CLKGATE1_CON,
	PM_SCU_CLKGATE2_CON,
	PM_SCU_SOFTRST_CON,
	PM_SCU_CHIPCFG_CON,
	PM_SCU_CPUPD,
	PM_CLKSEL2_CON,
	PM_SCU_REG_NUM
};

/***********************general cpu reg bit************************************/
#define RK2818GPIO_TOTAL	(PIN_BASE+NUM_GROUP*MAX_GPIO_BANKS)

/***********************general cpu reg  IOMUX_A bit************************************/

#define PM_I2C0 (30)
#define PM_I2C1 (28)
#define PM_UART1_OUT (26)    
#define PM_UART1_IN (24)
#define PM_SDIO1_CMD (23)
#define PM_SDIO1_DATA (22)
#define PM_UART0_OUT (14)    
#define PM_UART0_IN (12)

#define PM_SDIO0_CMD (5)
#define PM_SDIO0_DATA (4)



/***********************general cpu reg  IOMUX_B bit************************************/

#define PM_UART0_RTS (13)    
#define PM_UART0_CTS (12)






/***********************general cpu reg  PU bit************************************/
#define CLEAR_IO_PU(pin) (~((3)<<pin))
#define PMGPIO_NRO(pin) (0<<pin)
#define PMGPIO_UP(pin) (1<<pin)
#define PMGPIO_DN(pin) (2<<pin)
#define GPIO0_AB_NORMAL (0X0)

#define GPIO0_A0 (0)
#define GPIO0_A3 (6)


#define GPIO0_CD_NORMAL (0X0)
#define GPIO1_AB_NORMAL (0X0)
#define GPIO1_CD_NORMAL (0X0)

enum
{
    PM_CPU_APB_REG0,
    PM_CPU_APB_REG1,
    PM_CPU_APB_REG2,
    PM_CPU_APB_REG3,
    PM_CPU_APB_REG4,
    PM_CPU_APB_REG5,
    PM_CPU_APB_REG6,
    PM_CPU_APB_REG7,
    PM_IOMUX_A_CON,
    PM_IOMUX_B_CON,
    PM_GPIO0_AB_PU_CON,
    PM_GPIO0_CD_PU_CON,
    PM_GPIO1_AB_PU_CON,
    PM_GPIO1_CD_PU_CON,
    PM_OTGPHY_CON0,
    PM_OTGPHY_CON1,
    PM_GENERAL_CPU_REG
};
/***********************gpio cpu reg bit************************************/

enum
{
	PM_GPIO_SWPORTA_DR,
	PM_GPIO_SWPORTA_DDR,
	PM_GPIO_SWPORTA_NULL,
	PM_GPIO_SWPORTB_DR,
	PM_GPIO_SWPORTB_DDR,
	PM_GPIO_SWPORTB_NULL,
	PM_GPIO_SWPORTC_DR,
	PM_GPIO_SWPORTC_DDR,
	PM_GPIO_SWPORTC_NULL,
	PM_GPIO_SWPORTD_DR,
	PM_GPIO_SWPORTD_DDR,
	PM_SCU_GPIO_SWPORTC_NUM
};


#endif
