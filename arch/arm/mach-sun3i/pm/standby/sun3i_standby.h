/*
 * arch/arm/mach-sun3i/pm/standby/sun3i_standby.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef _ACTIONS_STANDBY_H_
#define _ACTIONS_STANDBY_H_

#include <sun3i_pm.h>

/**********************************************
MACRO CONTROL
**********************************************/
#define  __SRAM_DEBUG__				0
#define MODIFY_AHB_APB_EN  			0
#define EN_POWER_D						1

/**********************************************
SDR STATUS
**********************************************/
#define SDR_ENTER_SELFRFH		(1<<1)
#define SDR_SELFRFH_STATUS	(1<<15)
#define SDR_CLOCK_GATE_EN		(1<<15)

/**********************************************
KEY IN SRAM
**********************************************/
/*used for ndc key*/
#define WAKEUP_KEY_DOWN			1
#define BOARD_KEY_DOWN			0

/*used for adc key*/
#define AMU_KEY_ADC_EN				(1<<8)
#define ADC_CLK_EN					(1<<18)
#define KEY_CLK_EN					(1<<25)
#define KEY_PARA_MASK				(0xffffff)
#define KEY_SCAN_EN				(1<<19)
#define KEY_HOLD_IRQ_EN			(1<<20)
#define ADC_KEY_EN					(1<<2)

#define ADC_KEY_CLK_MASK			0xfffffffc
#define ADC_KEY_CLK_1K				0
#define ADC_KEY_CLK_2K				1
#define ADC_KEY_CLK_4K				2
#define ADC_KEY_CLK_8K				3

/*used for ir key*/
#define IR_ALARM_MASK				0xfffffff0
#define IR_EN						(1<<15)
#define IR_ALARM_EN					(1<<3)

struct cmu_reg_t{
	unsigned int core_pll;
	unsigned int aud_hosc;
	unsigned int ddr_pll;
	unsigned int bus_clk;
#if MODIFY_AHB_APB_EN
	unsigned int ahb_clk;
	unsigned int apb_clk;
#endif
};

typedef struct tag_twic_reg
{
    volatile unsigned int reg_saddr;
    volatile unsigned int reg_xsaddr;
    volatile unsigned int reg_data;
    volatile unsigned int reg_ctl;
    volatile unsigned int reg_status;
    volatile unsigned int reg_clkr;
    volatile unsigned int reg_reset;
    volatile unsigned int reg_efr;
    volatile unsigned int reg_lctl;

}__twic_reg_t;

struct sys_reg_t{
	struct cmu_reg_t cmu_regs;
	__twic_reg_t twi_regs;
};

/**********************************************
BUS IN SRAM
**********************************************/
#define BUS_CCLK_MASK		(0xfffff0ff)
#define BUS_CCLK_32K		(0<<9)
#define BUS_CCLK_24M		(1<<9)
#define BUS_CCLK_COREPLL	(2<<9)

#define BUS_DCLK_MASK		(0xfffffbff)
#define BUS_DCLK_DDRPLL	(0<<10)
#define BUS_DCLK_CCLK		(1<<10)

#define COREPLL_DISABLE	(0xfffffffe)
#define COREPLL_ENABLE		1
#define DDRPLL_DISABLE		(0xfffffffe)
#define DDRPLL_ENABLE		1
#define HOSC_DISABLE		(0xfffffffe)
#define HOSC_ENABLE		1

#define BUS_KEYE_WAKE_EN	(1<<31)
#define BUS_RTC_WAKE_EN	(1<<30)
#define BUS_SIRE_WAKE_EN	(1<<29)
#define BUS_TPE_WAKE_EN	(1<<28)
#define BUS_USBE_WAKE_EN	(1<<27)
#define BUS_IRE_WAKE_EN	(1<<26)
#define BUS_TKC_WAKE_EN	(1<<25)
#define BUS_KEYB_WAKE_EN	(1<<24)


#define WAIT_FOR_IRQ(x)	__asm __volatile__ ("mcr p15, 0, %0, c7, c0, 4\n\t" \
							: "=r"(x))

#define aw_readl(a)	(*(volatile unsigned int *)(a))
#define aw_writel(v,a)	(*(volatile unsigned int *)(a) = (v))

void standby_save_env(struct sys_reg_t *save_env);
void standby_restore_env(struct sys_reg_t *restore_env);
void standby_enter_low(void);
void standby_exit_low(void);
void standby_loop(struct aw_pm_arg *arg);

/**********************************
I2C op
**********************************/
#define TWI_OP_RD			(0)
#define TWI_OP_WR			(1)
#define EPDK_OK				(0)
#define EPDK_FAIL			(-1)


#endif //_ACTIONS_STANDBY_H_