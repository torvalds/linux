/*
 * arch/arm/mach-tegra/common.c
 *
 * Copyright (c) 2013 NVIDIA Corporation. All rights reserved.
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/irqchip.h>
#include <linux/clk-provider.h>

#include <asm/hardware/cache-l2x0.h>

#include "board.h"
#include "common.h"
#include "cpuidle.h"
#include "fuse.h"
#include "iomap.h"
#include "irq.h"
#include "pmc.h"
#include "apbio.h"
#include "sleep.h"
#include "pm.h"
#include "reset.h"

/*
 * Storage for debug-macro.S's state.
 *
 * This must be in .data not .bss so that it gets initialized each time the
 * kernel is loaded. The data is declared here rather than debug-macro.S so
 * that multiple inclusions of debug-macro.S point at the same data.
 */
u32 tegra_uart_config[4] = {
	/* Debug UART initialization required */
	1,
	/* Debug UART physical address */
	0,
	/* Debug UART virtual address */
	0,
	/* Scratch space for debug macro */
	0,
};

#ifdef CONFIG_OF
void __init tegra_dt_init_irq(void)
{
	of_clk_init(NULL);
	tegra_pmc_init();
	tegra_init_irq();
	irqchip_init();
	tegra_legacy_irq_syscore_init();
}
#endif

void tegra_assert_system_reset(enum reboot_mode mode, const char *cmd)
{
	void __iomem *reset = IO_ADDRESS(TEGRA_PMC_BASE + 0);
	u32 reg;

	reg = readl_relaxed(reset);
	reg |= 0x10;
	writel_relaxed(reg, reset);
}

static void __init tegra_init_cache(void)
{
#ifdef CONFIG_CACHE_L2X0
	int ret;
	void __iomem *p = IO_ADDRESS(TEGRA_ARM_PERIF_BASE) + 0x3000;
	u32 aux_ctrl, cache_type;

	cache_type = readl(p + L2X0_CACHE_TYPE);
	aux_ctrl = (cache_type & 0x700) << (17-8);
	aux_ctrl |= 0x7C400001;

	ret = l2x0_of_init(aux_ctrl, 0x8200c3fe);
	if (!ret)
		l2x0_saved_regs_addr = virt_to_phys(&l2x0_saved_regs);
#endif

}

void __init tegra_init_early(void)
{
	tegra_cpu_reset_handler_init();
	tegra_apb_io_init();
	tegra_init_fuse();
	tegra_init_cache();
	tegra_powergate_init();
	tegra_hotplug_init();
}

void __init tegra_init_late(void)
{
	tegra_init_suspend();
	tegra_cpuidle_init();
	tegra_powergate_debugfs_init();
}
