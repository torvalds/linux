/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/init.h>
#include <linux/bootmem.h>
#include <asm/bootinfo.h>
#include <bcm63xx_board.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_gpio.h>

void __init prom_init(void)
{
	u32 reg, mask;

	bcm63xx_cpu_init();

	/* stop any running watchdog */
	bcm_wdt_writel(WDT_STOP_1, WDT_CTL_REG);
	bcm_wdt_writel(WDT_STOP_2, WDT_CTL_REG);

	/* disable all hardware blocks clock for now */
	if (BCMCPU_IS_6338())
		mask = CKCTL_6338_ALL_SAFE_EN;
	else if (BCMCPU_IS_6345())
		mask = CKCTL_6345_ALL_SAFE_EN;
	else if (BCMCPU_IS_6348())
		mask = CKCTL_6348_ALL_SAFE_EN;
	else if (BCMCPU_IS_6358())
		mask = CKCTL_6358_ALL_SAFE_EN;
	else if (BCMCPU_IS_6368())
		mask = CKCTL_6368_ALL_SAFE_EN;
	else
		mask = 0;

	reg = bcm_perf_readl(PERF_CKCTL_REG);
	reg &= ~mask;
	bcm_perf_writel(reg, PERF_CKCTL_REG);

	/* register gpiochip */
	bcm63xx_gpio_init();

	/* do low level board init */
	board_prom_init();
}

void __init prom_free_prom_memory(void)
{
}
