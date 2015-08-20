/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file busfreq_lpddr2.c
 *
 * @brief iMX6 LPDDR2 frequency change specific file.
 *
 * @ingroup PM
 */
#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/tlb.h>
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include "hardware.h"


static struct device *busfreq_dev;
static int curr_ddr_rate;
static DEFINE_SPINLOCK(freq_lock);

void (*mx6_change_lpddr2_freq)(u32 ddr_freq, int bus_freq_mode) = NULL;

extern unsigned int ddr_normal_rate;
extern void mx6_lpddr2_freq_change(u32 freq, int bus_freq_mode);
extern void imx6_up_lpddr2_freq_change(u32 freq, int bus_freq_mode);
extern unsigned long save_ttbr1(void);
extern void restore_ttbr1(unsigned long ttbr1);
extern unsigned long ddr_freq_change_iram_base;
extern unsigned long imx6_lpddr2_freq_change_start asm("imx6_lpddr2_freq_change_start");
extern unsigned long imx6_lpddr2_freq_change_end asm("imx6_lpddr2_freq_change_end");

/* change the DDR frequency. */
int update_lpddr2_freq(int ddr_rate)
{
	unsigned long ttbr1, flags;
	int mode = get_bus_freq_mode();

	if (ddr_rate == curr_ddr_rate)
		return 0;

	printk(KERN_DEBUG "\nBus freq set to %d start...\n", ddr_rate);

	spin_lock_irqsave(&freq_lock, flags);
	/*
	 * Flush the TLB, to ensure no TLB maintenance occurs
	 * when DDR is in self-refresh.
	 */
	ttbr1 = save_ttbr1();

	/* Now change DDR frequency. */
	mx6_change_lpddr2_freq(ddr_rate,
		(mode == BUS_FREQ_LOW || mode == BUS_FREQ_ULTRA_LOW) ? 1 : 0);
	restore_ttbr1(ttbr1);

	curr_ddr_rate = ddr_rate;
	spin_unlock_irqrestore(&freq_lock, flags);

	printk(KERN_DEBUG "\nBus freq set to %d done...\n", ddr_rate);

	return 0;
}

int init_mmdc_lpddr2_settings(struct platform_device *busfreq_pdev)
{
	unsigned long ddr_code_size;
	busfreq_dev = &busfreq_pdev->dev;

	ddr_code_size = SZ_4K;

	if (cpu_is_imx6sx() || cpu_is_imx6ul())
		mx6_change_lpddr2_freq = (void *)fncpy(
			(void *)ddr_freq_change_iram_base,
			&imx6_up_lpddr2_freq_change, ddr_code_size);

	curr_ddr_rate = ddr_normal_rate;

	return 0;
}
