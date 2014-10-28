/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Atheros Communications, Inc.,  All Rights Reserved.
 * Copyright (C) 2006 FON Technology, SL.
 * Copyright (C) 2006 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2012 Alexandros C. Couloumbis <alex@ozo.com>
 */

/*
 * Platform devices for Atheros AR2315 SoCs
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/time.h>

#include "devices.h"
#include "ar2315.h"
#include "ar2315_regs.h"

static void __iomem *ar2315_rst_base;

static inline u32 ar2315_rst_reg_read(u32 reg)
{
	return __raw_readl(ar2315_rst_base + reg);
}

static inline void ar2315_rst_reg_write(u32 reg, u32 val)
{
	__raw_writel(val, ar2315_rst_base + reg);
}

static inline void ar2315_rst_reg_mask(u32 reg, u32 mask, u32 val)
{
	u32 ret = ar2315_rst_reg_read(reg);

	ret &= ~mask;
	ret |= val;
	ar2315_rst_reg_write(reg, ret);
}

static void ar2315_restart(char *command)
{
	void (*mips_reset_vec)(void) = (void *)0xbfc00000;

	local_irq_disable();

	/* try reset the system via reset control */
	ar2315_rst_reg_write(AR2315_COLD_RESET, AR2317_RESET_SYSTEM);

	/* Cold reset does not work on the AR2315/6, use the GPIO reset bits
	 * a workaround. Give it some time to attempt a gpio based hardware
	 * reset (atheros reference design workaround) */

	/* TODO: implement the GPIO reset workaround */

	/* Some boards (e.g. Senao EOC-2610) don't implement the reset logic
	 * workaround. Attempt to jump to the mips reset location -
	 * the boot loader itself might be able to recover the system */
	mips_reset_vec();
}

/*
 * This table is indexed by bits 5..4 of the CLOCKCTL1 register
 * to determine the predevisor value.
 */
static int clockctl1_predivide_table[4] __initdata = { 1, 2, 4, 5 };
static int pllc_divide_table[5] __initdata = { 2, 3, 4, 6, 3 };

static unsigned __init ar2315_sys_clk(u32 clock_ctl)
{
	unsigned int pllc_ctrl, cpu_div;
	unsigned int pllc_out, refdiv, fdiv, divby2;
	unsigned int clk_div;

	pllc_ctrl = ar2315_rst_reg_read(AR2315_PLLC_CTL);
	refdiv = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_REF_DIV);
	refdiv = clockctl1_predivide_table[refdiv];
	fdiv = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_FDBACK_DIV);
	divby2 = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_ADD_FDBACK_DIV) + 1;
	pllc_out = (40000000 / refdiv) * (2 * divby2) * fdiv;

	/* clkm input selected */
	switch (clock_ctl & AR2315_CPUCLK_CLK_SEL_M) {
	case 0:
	case 1:
		clk_div = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_CLKM_DIV);
		clk_div = pllc_divide_table[clk_div];
		break;
	case 2:
		clk_div = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_CLKC_DIV);
		clk_div = pllc_divide_table[clk_div];
		break;
	default:
		pllc_out = 40000000;
		clk_div = 1;
		break;
	}

	cpu_div = ATH25_REG_MS(clock_ctl, AR2315_CPUCLK_CLK_DIV);
	cpu_div = cpu_div * 2 ?: 1;

	return pllc_out / (clk_div * cpu_div);
}

static inline unsigned ar2315_cpu_frequency(void)
{
	return ar2315_sys_clk(ar2315_rst_reg_read(AR2315_CPUCLK));
}

static inline unsigned ar2315_apb_frequency(void)
{
	return ar2315_sys_clk(ar2315_rst_reg_read(AR2315_AMBACLK));
}

void __init ar2315_plat_time_init(void)
{
	mips_hpt_frequency = ar2315_cpu_frequency() / 2;
}

void __init ar2315_plat_mem_setup(void)
{
	void __iomem *sdram_base;
	u32 memsize, memcfg;
	u32 config;

	/* Detect memory size */
	sdram_base = ioremap_nocache(AR2315_SDRAMCTL_BASE,
				     AR2315_SDRAMCTL_SIZE);
	memcfg = __raw_readl(sdram_base + AR2315_MEM_CFG);
	memsize   = 1 + ATH25_REG_MS(memcfg, AR2315_MEM_CFG_DATA_WIDTH);
	memsize <<= 1 + ATH25_REG_MS(memcfg, AR2315_MEM_CFG_COL_WIDTH);
	memsize <<= 1 + ATH25_REG_MS(memcfg, AR2315_MEM_CFG_ROW_WIDTH);
	memsize <<= 3;
	add_memory_region(0, memsize, BOOT_MEM_RAM);
	iounmap(sdram_base);

	ar2315_rst_base = ioremap_nocache(AR2315_RST_BASE, AR2315_RST_SIZE);

	/* Clear any lingering AHB errors */
	config = read_c0_config();
	write_c0_config(config & ~0x3);
	ar2315_rst_reg_write(AR2315_AHB_ERR0, AR2315_AHB_ERROR_DET);
	ar2315_rst_reg_read(AR2315_AHB_ERR1);
	ar2315_rst_reg_write(AR2315_WDT_CTRL, AR2315_WDT_CTRL_IGNORE);

	_machine_restart = ar2315_restart;
}
