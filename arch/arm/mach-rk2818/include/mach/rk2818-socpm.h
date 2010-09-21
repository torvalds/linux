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
#ifndef _ROCKCHIP_SOC_PM_H
#define _ROCKCHIP_SOC_PM_H

#if defined (CONFIG_RK2818_SOC_PM)

#include <asm/tcm.h>
#include <mach/gpio.h>


#define RK2818_PM_PRT_ORIGINAL_REG
#define RK2818_PM_PRT_CHANGED_REG

#define PM_SAVE_REG_NUM 4

#define PM_NUM_BIT_MASK(b)	((0x1<<(b))-1)
#define PM_BIT_OFF_MASK(off,numbit)	(PM_NUM_BIT_MASK(numbit)<<(off))
#define PM_BIT_CLEAR(off,numbit)	(~(PM_BIT_OFF_MASK(off,numbit)))
#define PM_BIT_SET(off,val,numbit)	((val&PM_NUM_BIT_MASK(numbit))<<off)
#define PM_BIT_get(val,off,numbit)	((val&PM_NUM_BIT_MASK(numbit))>>off)

#if defined (CONFIG_RK2818_SOC_PM_DBG)

#define PM_SCU_ATTR_DBG_NUM 30
#define PM_GENERAL_ATTR_DBG_NUM 10
#define PM_GPIO0_ATTR_DBG_NUM 10
#define PM_GPIO1_ATTR_DBG_NUM 10


#define PM_DBG_CH_REG_INI 0
#define PM_DBG_CH_REG_UPDATA	1



#define PM_DBG_SET_ONCE 0xEE
#define PM_DBG_SET_ALWAY 0x99
#define PM_DBG_NOT_SET 0

enum PM_DBG_USER_EN{

	PM_DBG_USER_VALUE,
	PM_DBG_USER_REGOFF,
	PM_DBG_USER_BITS_OFF,
	PM_DBG_USER_BITS_NUM,
	PM_DBG_USER_FLAG,
	PM_DBG_USER_END,
};


#define PM_ATTR_CTR_ONCE 0xEE
#define PM_ATTR_CTR_ALWAY 0x99
#define PM_ATTR_NO_CTR 0x0

struct rk2818_pm_attr_dbg_st{
	unsigned int value;
	unsigned char regoff;
	unsigned char regbits_off;
	unsigned char bitsnum;
	unsigned char flag;
};

bool rk2818_socpm_attr_store(int type,const char *buf, size_t n);
ssize_t rk2818_socpm_attr_show(int type,char *buf);
#endif

typedef void (*pm_scu_suspend)(unsigned int *tempdata,int regoff);
typedef void (*pm_general_reg_suspend)(void);
typedef void (*pm_set_suspendvol)(void);
typedef void (*pm_resume_vol)(void);

struct rk2818_pm_soc_st{
unsigned int *reg_save;
unsigned int *reg_base_addr;
u16 reg_ctrbit;
u8 reg_num;
#ifdef RK2818_PM_PRT_CHANGED_REG
unsigned int *reg_ch;
#endif
#if defined (CONFIG_RK2818_SOC_PM_DBG)
u8 attr_num;
struct rk2818_pm_attr_dbg_st *attr_dbg;
u8 attr_flag;
#endif
};

struct rk2818_pm_callback_st{
int data;
pm_scu_suspend scu_suspend;
pm_general_reg_suspend general_reg_suspend;
pm_set_suspendvol set_suspendvol;
pm_resume_vol resume_vol;
};



struct rk2818_pm_st{
struct rk2818_pm_soc_st *scu;
unsigned int *scu_tempreg;
unsigned int scu_reg;
struct rk2818_pm_soc_st *general;
struct rk2818_pm_soc_st *gpio0;
struct rk2818_pm_soc_st *gpio1;
//struct rk2818_pm_callback_st *callback;
unsigned int *save_reg;
unsigned int *save_ch;
pm_scu_suspend scu_suspend;
pm_general_reg_suspend general_reg_suspend;
pm_set_suspendvol set_suspendvol;
pm_resume_vol resume_vol;
};

/***********************scu SCU_CLKSEL0 reg bit************************************/
#define PM_RESUME_BIT	0
#define PM_DEBUG_BIT	1
#define PM_SET_BIT(b) (1<<b)
#define PM_GET_BIT(val,b)	((val&(1<<b))>>b)






/***********************scu SCU_CLKSEL0 reg bit************************************/

#define SCU_GATE0CLK_ALL_EN 0
#define SCU_GATE0CLK_ALL_DIS 0XFFFFFFFF
#define DIS_TIMER_CLK (1<<25)
#define DIS_UART1_CLK (1<<19)
#define DIS_UART0_CLK (1<<18)
#define DIS_GPIO1_CLK (1<<17)
#define DIS_GPIO0_CLK (1<<16)
#define DIS_INTC_CLK (1<<9)
#define DIS_ARM_CLK (1<<0)
/***********************scu SCU_CLKSEL1_CON reg bit************************************/

#define SCU_GATE1CLK_ALL_EN 0
//#define SCU_GATE1CLK_BASE_SET (0XFFFFFFFF&(~DIS_DDR_CLK)&(~DIS_DDR_HCLK)) // 注意AXI 位为1时为enable 

#define SCU_GATE1CLK_BASE_SET (0XFFFF8001&(~EN_AXI_CLK)) // 注意AXI 位为1时为enable 
//#define SCU_GATE1CLK_BASE_SET (0XFFFFFFFF)
#define EN_AXI_CLK (1<<27)
#define DIS_DDR_CLK (1<<23)
#define DIS_DDR_HCLK (1<<22)
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
#define CPU_STOP_MODE (1<<4)



/***********************scu SCU_PMU_CON reg bit************************************/

#define LCDC_POWER_DOWN (1<<3)
#define DSP_POWER_DOWN (1<<0)
 #define DDR_POWER_DOWN (1<<2)


/***********************scu PM_SCU_CLKSEL0_CON reg bit************************************/
#define CLKSEL0_HCLK (0)
#define CLKSEL0_PCLK (2)

#define CLK_ARM1_H1 (0)
#define CLK_ARM2_H1 (1)
#define CLK_ARM3_H1 (2)
#define CLK_ARM4_H1 (3)

#define CLK_HCLK1_P1 (0)
#define CLK_HCLK2_P1 (1)
#define CLK_HCLK4_P1 (2)

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
#define PM_CLEAR_IO_PU(pin) (~((3)<<pin))
#define PM_GPIO_NRO(pin) (0<<pin)
#define PM_GPIO_UP(pin) (1<<pin)
#define PM_GPIO_DN(pin) (2<<pin)

#define GPIO0_AB_NORMAL (0X0)
#define GPIO0_CD_NORMAL (0X0)
#define GPIO1_AB_NORMAL (0X0)
#define GPIO1_CD_NORMAL (0X0)

#define GPIO0_A0 (0)
#define GPIO0_A3 (3*2)

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

void rk2818_socpm_int(pm_scu_suspend scu,pm_general_reg_suspend general,
	pm_set_suspendvol setvol,pm_resume_vol resumevol);
	extern struct rk2818_pm_st __tcmdata rk2818_soc_pm;
extern int  __tcmfunc rk2818_socpm_gpio_pullupdown(unsigned int gpio,eGPIOPullType_t GPIOPullUpDown);
extern int  __tcmfunc rk2818_socpm_set_gpio(unsigned int gpio,unsigned int output,unsigned int level);



void __tcmfunc rk2818_socpm_suspend_first(void);
void __tcmfunc rk2818_socpm_suspend(void);

void __tcmfunc rk2818_socpm_resume_first(void);
void __tcmfunc rk2818_socpm_resume(void);
void rk2818_socpm_print(void);
#else
#define rk2818_socpm_int(a,b,c,d)
#define rk2818_socpm_gpio_pullupdown(a,b)
#define rk2818_socpm_set_gpio(a,b,c)
#define rk2818_socpm_suspend_first()
#define rk2818_socpm_suspend()
#define rk2818_socpm_resume_first()
#define rk2818_socpm_resume()
#define rk2818_socpm_print()

#endif


#endif

