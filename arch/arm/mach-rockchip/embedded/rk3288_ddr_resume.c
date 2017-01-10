/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Chris Zhong <zyw@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/bug.h>		/* BUILD_BUG_ON */
#include <linux/kernel.h>

#include <asm/io.h>

#include "rk3288_ddr.h"
#include "rk3288_resume.h"
#include "sram_delay.h"

#include "../pm.h"

#define PMU_ADDR		0xff730000
#define PMU_PWRMODE_CON_ADDR	((void *)(PMU_ADDR + RK3288_PMU_PWRMODE_CON))
#define DDR_PCTRL0_ADDR		((void *)0xff610000)
#define DDR_PUBL0_ADDR		((void *)0xff620000) /* phy */
#define DDR_PCTRL1_ADDR		((void *)0xff630000)
#define DDR_PUBL1_ADDR		((void *)0xff640000) /* phy */

#define MSCH0_ADDR		((void *)0xffac0000)
#define MSCH1_ADDR		((void *)0xffac0080)

static void * const pctrl_addrs[] = { DDR_PCTRL0_ADDR, DDR_PCTRL1_ADDR };
static void * const phy_addrs[] = { DDR_PUBL0_ADDR, DDR_PUBL1_ADDR };
static void * const msch_addrs[] = { MSCH0_ADDR, MSCH1_ADDR };

static void reset_dll(void __iomem *phy_addr)
{
	static const u32 reg[] = { DDR_PUBL_ACDLLCR, DDR_PUBL_DX0DLLCR,
				   DDR_PUBL_DX1DLLCR, DDR_PUBL_DX2DLLCR,
				   DDR_PUBL_DX3DLLCR };
	int i;

	for (i = 0; i < ARRAY_SIZE(reg); i++)
		writel_relaxed(readl_relaxed(phy_addr + reg[i]) & ~DLLSRST,
			       phy_addr + reg[i]);

	sram_udelay(10);

	for (i = 0; i < ARRAY_SIZE(reg); i++)
		writel_relaxed(readl_relaxed(phy_addr + reg[i]) | DLLSRST,
			       phy_addr + reg[i]);

	sram_udelay(10);
}

static void phy_init(void __iomem *phy_addr)
{
	u32 val;

	val = readl_relaxed(phy_addr + DDR_PUBL_PIR);

	val |= PIR_INIT | PIR_DLLSRST | PIR_DLLLOCK |
	       PIR_ZCAL | PIR_ITMSRST | PIR_CLRSR;

	writel_relaxed(val, phy_addr + DDR_PUBL_PIR);

	sram_udelay(1);

	while ((readl_relaxed(phy_addr + DDR_PUBL_PGSR) &
		(PGSR_IDONE | PGSR_DLDONE | PGSR_ZCDONE)) !=
		(PGSR_IDONE | PGSR_DLDONE | PGSR_ZCDONE))
		;
}

static void memory_init(void __iomem *phy_addr)
{
	u32 val;

	val = readl_relaxed(phy_addr + DDR_PUBL_PIR);

	val |= PIR_INIT | PIR_DRAMINIT | PIR_LOCKBYP |
	       PIR_ZCALBYP | PIR_CLRSR | PIR_ICPC;

	writel_relaxed(val, phy_addr + DDR_PUBL_PIR);

	sram_udelay(1);
	while ((readl_relaxed(phy_addr + DDR_PUBL_PGSR) &
		(PGSR_IDONE | PGSR_DLDONE)) != (PGSR_IDONE | PGSR_DLDONE))
		;
}

static void move_to_lowpower_state(void __iomem *pctrl_addr,
				   void __iomem *phy_addr)
{
	u32 state;

	while (1) {
		state = readl_relaxed(pctrl_addr +
				      DDR_PCTL_STAT) & PCTL_STAT_MSK;

		switch (state) {
		case INIT_MEM:
			writel_relaxed(CFG_STATE, pctrl_addr + DDR_PCTL_SCTL);
			while ((readl_relaxed(pctrl_addr + DDR_PCTL_STAT) &
				PCTL_STAT_MSK) != CONFIG)
				;
			/* no break */
		case CONFIG:
			writel_relaxed(GO_STATE, pctrl_addr + DDR_PCTL_SCTL);
			while ((readl_relaxed(pctrl_addr + DDR_PCTL_STAT) &
				PCTL_STAT_MSK) != ACCESS)
				;
			/* no break */
		case ACCESS:
			writel_relaxed(SLEEP_STATE, pctrl_addr + DDR_PCTL_SCTL);
			while ((readl_relaxed(pctrl_addr + DDR_PCTL_STAT) &
				PCTL_STAT_MSK) != LOW_POWER)
				;
			/* no break */
		case LOW_POWER:
			return;
		default:
			break;
		}
	}
}

static void move_to_access_state(void __iomem *pctrl_addr,
				 void __iomem *phy_addr)
{
	u32 state;

	while (1) {
		state = readl_relaxed(pctrl_addr +
				      DDR_PCTL_STAT) & PCTL_STAT_MSK;

		switch (state) {
		case LOW_POWER:
			if (LP_TRIG_VAL(readl_relaxed(pctrl_addr +
						      DDR_PCTL_STAT)) == 1)
				return;

			writel_relaxed(WAKEUP_STATE,
				       pctrl_addr + DDR_PCTL_SCTL);
			while ((readl_relaxed(pctrl_addr + DDR_PCTL_STAT) &
				PCTL_STAT_MSK) != ACCESS)
				;

			/* wait DLL lock */
			while ((readl_relaxed(phy_addr +
					      DDR_PUBL_PGSR) & PGSR_DLDONE)
				!= PGSR_DLDONE)
				;

			break;
		case INIT_MEM:
			writel_relaxed(CFG_STATE, pctrl_addr + DDR_PCTL_SCTL);
			while ((readl_relaxed(pctrl_addr + DDR_PCTL_STAT) &
				PCTL_STAT_MSK) != CONFIG)
				;
			 /* fallthrough here */
		case CONFIG:
			writel_relaxed(GO_STATE,  pctrl_addr + DDR_PCTL_SCTL);
			while ((readl_relaxed(pctrl_addr + DDR_PCTL_STAT) &
				PCTL_STAT_MSK) == CONFIG)
				;
			break;
		case ACCESS:
			return;
		default:
			break;
		}
	}
}

static void rk3288_ddr_reg_restore(void __iomem *regbase, const u32 reg_list[],
				   int num_reg, const u32 *vals)
{
	int i;

	for (i = 0; i < num_reg && reg_list[i] != RK3288_BOGUS_OFFSET; i++)
		writel_relaxed(vals[i], regbase + reg_list[i]);
}

void rk3288_ddr_resume_early(const struct rk3288_ddr_save_data *ddr_save_data)
{
	int ch;

	/* PWM saves full address, so base is NULL */
	rk3288_ddr_reg_restore(NULL, ddr_save_data->pwm_addrs,
			       RK3288_MAX_PWM_REGS, ddr_save_data->pwm_vals);

	/*
	 * PWM never runs higher than 1.2V giving a 2000uV/us ramp delay since
	 * we start from 1V. This is a very conservative ramp delay for the
	 * regulator.
	 */
	sram_udelay(100);

	for (ch = 0; ch < ARRAY_SIZE(pctrl_addrs); ch++) {
		/* DLL bypass */
		rk3288_ddr_reg_restore(phy_addrs[ch],
				       ddr_save_data->phy_dll_offsets,
				       RK3288_MAX_DDR_PHY_DLL_REGS,
				       ddr_save_data->phy_dll_vals[ch]);

		reset_dll(phy_addrs[ch]);

		/* ddr ctrl restore; NOTE: both channels must be the same */
		rk3288_ddr_reg_restore(pctrl_addrs[ch],
				       ddr_save_data->ctrl_offsets,
				       RK3288_MAX_DDR_CTRL_REGS,
				       ddr_save_data->ctrl_vals);

		/* ddr phy restore */
		rk3288_ddr_reg_restore(phy_addrs[ch],
				       ddr_save_data->phy_offsets,
				       RK3288_MAX_DDR_PHY_REGS,
				       ddr_save_data->phy_vals[ch]);

		/* msch restore */
		rk3288_ddr_reg_restore(msch_addrs[ch],
				       ddr_save_data->msch_offsets,
				       RK3288_MAX_DDR_MSCH_REGS,
				       ddr_save_data->msch_vals[ch]);

		phy_init(phy_addrs[ch]);

		/* power up ddr power */
		writel_relaxed(POWER_UP_START,
			       pctrl_addrs[ch] + DDR_PCTL_POWCTL);
		while (!(readl_relaxed(pctrl_addrs[ch] + DDR_PCTL_POWSTAT) &
			 POWER_UP_DONE))
			;

		/* zqcr restore; NOTE: both channels must be the same */
		rk3288_ddr_reg_restore(phy_addrs[ch],
				       ddr_save_data->phy_zqcr_offsets,
				       RK3288_MAX_DDR_PHY_ZQCR_REGS,
				       ddr_save_data->phy_zqcr_vals);

		memory_init(phy_addrs[ch]);

		move_to_lowpower_state(pctrl_addrs[ch], phy_addrs[ch]);
	}

	/* disable retention */
	writel_relaxed(readl_relaxed(PMU_PWRMODE_CON_ADDR) |
	       DDR0IO_RET_DE_REQ | DDR0I1_RET_DE_REQ,
	       PMU_PWRMODE_CON_ADDR);

	sram_udelay(1);

	/* disable self-refresh */
	for (ch = 0; ch < ARRAY_SIZE(pctrl_addrs); ch++)
		move_to_access_state(pctrl_addrs[ch], phy_addrs[ch]);
}
