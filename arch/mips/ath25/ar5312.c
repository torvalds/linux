/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Atheros Communications, Inc.,  All Rights Reserved.
 * Copyright (C) 2006 FON Technology, SL.
 * Copyright (C) 2006 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2006-2009 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2012 Alexandros C. Couloumbis <alex@ozo.com>
 */

/*
 * Platform devices for Atheros AR5312 SoCs
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/time.h>

#include "devices.h"
#include "ar5312.h"
#include "ar5312_regs.h"

static void __iomem *ar5312_rst_base;

static inline u32 ar5312_rst_reg_read(u32 reg)
{
	return __raw_readl(ar5312_rst_base + reg);
}

static inline void ar5312_rst_reg_write(u32 reg, u32 val)
{
	__raw_writel(val, ar5312_rst_base + reg);
}

static inline void ar5312_rst_reg_mask(u32 reg, u32 mask, u32 val)
{
	u32 ret = ar5312_rst_reg_read(reg);

	ret &= ~mask;
	ret |= val;
	ar5312_rst_reg_write(reg, ret);
}

static void ar5312_restart(char *command)
{
	/* reset the system */
	local_irq_disable();
	while (1)
		ar5312_rst_reg_write(AR5312_RESET, AR5312_RESET_SYSTEM);
}

/*
 * This table is indexed by bits 5..4 of the CLOCKCTL1 register
 * to determine the predevisor value.
 */
static unsigned clockctl1_predivide_table[4] __initdata = { 1, 2, 4, 5 };

static unsigned __init ar5312_cpu_frequency(void)
{
	u32 scratch, devid, clock_ctl1;
	u32 predivide_mask, multiplier_mask, doubler_mask;
	unsigned predivide_shift, multiplier_shift;
	unsigned predivide_select, predivisor, multiplier;

	/* Trust the bootrom's idea of cpu frequency. */
	scratch = ar5312_rst_reg_read(AR5312_SCRATCH);
	if (scratch)
		return scratch;

	devid = ar5312_rst_reg_read(AR5312_REV);
	devid = (devid & AR5312_REV_MAJ) >> AR5312_REV_MAJ_S;
	if (devid == AR5312_REV_MAJ_AR2313) {
		predivide_mask = AR2313_CLOCKCTL1_PREDIVIDE_MASK;
		predivide_shift = AR2313_CLOCKCTL1_PREDIVIDE_SHIFT;
		multiplier_mask = AR2313_CLOCKCTL1_MULTIPLIER_MASK;
		multiplier_shift = AR2313_CLOCKCTL1_MULTIPLIER_SHIFT;
		doubler_mask = AR2313_CLOCKCTL1_DOUBLER_MASK;
	} else { /* AR5312 and AR2312 */
		predivide_mask = AR5312_CLOCKCTL1_PREDIVIDE_MASK;
		predivide_shift = AR5312_CLOCKCTL1_PREDIVIDE_SHIFT;
		multiplier_mask = AR5312_CLOCKCTL1_MULTIPLIER_MASK;
		multiplier_shift = AR5312_CLOCKCTL1_MULTIPLIER_SHIFT;
		doubler_mask = AR5312_CLOCKCTL1_DOUBLER_MASK;
	}

	/*
	 * Clocking is derived from a fixed 40MHz input clock.
	 *
	 *  cpu_freq = input_clock * MULT (where MULT is PLL multiplier)
	 *  sys_freq = cpu_freq / 4	  (used for APB clock, serial,
	 *				   flash, Timer, Watchdog Timer)
	 *
	 *  cnt_freq = cpu_freq / 2	  (use for CPU count/compare)
	 *
	 * So, for example, with a PLL multiplier of 5, we have
	 *
	 *  cpu_freq = 200MHz
	 *  sys_freq = 50MHz
	 *  cnt_freq = 100MHz
	 *
	 * We compute the CPU frequency, based on PLL settings.
	 */

	clock_ctl1 = ar5312_rst_reg_read(AR5312_CLOCKCTL1);
	predivide_select = (clock_ctl1 & predivide_mask) >> predivide_shift;
	predivisor = clockctl1_predivide_table[predivide_select];
	multiplier = (clock_ctl1 & multiplier_mask) >> multiplier_shift;

	if (clock_ctl1 & doubler_mask)
		multiplier <<= 1;

	return (40000000 / predivisor) * multiplier;
}

static inline unsigned ar5312_sys_frequency(void)
{
	return ar5312_cpu_frequency() / 4;
}

void __init ar5312_plat_time_init(void)
{
	mips_hpt_frequency = ar5312_cpu_frequency() / 2;
}

void __init ar5312_plat_mem_setup(void)
{
	void __iomem *sdram_base;
	u32 memsize, memcfg, bank0_ac, bank1_ac;

	/* Detect memory size */
	sdram_base = ioremap_nocache(AR5312_SDRAMCTL_BASE,
				     AR5312_SDRAMCTL_SIZE);
	memcfg = __raw_readl(sdram_base + AR5312_MEM_CFG1);
	bank0_ac = ATH25_REG_MS(memcfg, AR5312_MEM_CFG1_AC0);
	bank1_ac = ATH25_REG_MS(memcfg, AR5312_MEM_CFG1_AC1);
	memsize = (bank0_ac ? (1 << (bank0_ac + 1)) : 0) +
		  (bank1_ac ? (1 << (bank1_ac + 1)) : 0);
	memsize <<= 20;
	add_memory_region(0, memsize, BOOT_MEM_RAM);
	iounmap(sdram_base);

	ar5312_rst_base = ioremap_nocache(AR5312_RST_BASE, AR5312_RST_SIZE);

	/* Clear any lingering AHB errors */
	ar5312_rst_reg_read(AR5312_PROCADDR);
	ar5312_rst_reg_read(AR5312_DMAADDR);
	ar5312_rst_reg_write(AR5312_WDT_CTRL, AR5312_WDT_CTRL_IGNORE);

	_machine_restart = ar5312_restart;
}
