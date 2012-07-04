/*
 * arch/arm/mach-pnx4008/pm.c
 *
 * Power Management driver for PNX4008
 *
 * Authors: Vitaly Wool, Dmitry Chigirev <source@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/pm.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <asm/cacheflush.h>

#include <mach/hardware.h>
#include <mach/pm.h>
#include <mach/clock.h>

#define SRAM_VA IO_ADDRESS(PNX4008_IRAM_BASE)

static void *saved_sram;

static struct clk *pll4_clk;

static inline void pnx4008_standby(void)
{
	void (*pnx4008_cpu_standby_ptr) (void);

	local_irq_disable();
	local_fiq_disable();

	clk_disable(pll4_clk);

	/*saving portion of SRAM to be used by suspend function. */
	memcpy(saved_sram, (void *)SRAM_VA, pnx4008_cpu_standby_sz);

	/*make sure SRAM copy gets physically written into SDRAM.
	   SDRAM will be placed into self-refresh during power down */
	flush_cache_all();

	/*copy suspend function into SRAM */
	memcpy((void *)SRAM_VA, pnx4008_cpu_standby, pnx4008_cpu_standby_sz);

	/*do suspend */
	pnx4008_cpu_standby_ptr = (void *)SRAM_VA;
	pnx4008_cpu_standby_ptr();

	/*restoring portion of SRAM that was used by suspend function */
	memcpy((void *)SRAM_VA, saved_sram, pnx4008_cpu_standby_sz);

	clk_enable(pll4_clk);

	local_fiq_enable();
	local_irq_enable();
}

static inline void pnx4008_suspend(void)
{
	void (*pnx4008_cpu_suspend_ptr) (void);

	local_irq_disable();
	local_fiq_disable();

	clk_disable(pll4_clk);

	__raw_writel(0xffffffff, START_INT_RSR_REG(SE_PIN_BASE_INT));
	__raw_writel(0xffffffff, START_INT_RSR_REG(SE_INT_BASE_INT));

	/*saving portion of SRAM to be used by suspend function. */
	memcpy(saved_sram, (void *)SRAM_VA, pnx4008_cpu_suspend_sz);

	/*make sure SRAM copy gets physically written into SDRAM.
	   SDRAM will be placed into self-refresh during power down */
	flush_cache_all();

	/*copy suspend function into SRAM */
	memcpy((void *)SRAM_VA, pnx4008_cpu_suspend, pnx4008_cpu_suspend_sz);

	/*do suspend */
	pnx4008_cpu_suspend_ptr = (void *)SRAM_VA;
	pnx4008_cpu_suspend_ptr();

	/*restoring portion of SRAM that was used by suspend function */
	memcpy((void *)SRAM_VA, saved_sram, pnx4008_cpu_suspend_sz);

	clk_enable(pll4_clk);

	local_fiq_enable();
	local_irq_enable();
}

static int pnx4008_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		pnx4008_standby();
		break;
	case PM_SUSPEND_MEM:
		pnx4008_suspend();
		break;
	}
	return 0;
}

static int pnx4008_pm_valid(suspend_state_t state)
{
	return (state == PM_SUSPEND_STANDBY) ||
	       (state == PM_SUSPEND_MEM);
}

static const struct platform_suspend_ops pnx4008_pm_ops = {
	.enter = pnx4008_pm_enter,
	.valid = pnx4008_pm_valid,
};

int __init pnx4008_pm_init(void)
{
	u32 sram_size_to_allocate;

	pll4_clk = clk_get(0, "ck_pll4");
	if (IS_ERR(pll4_clk)) {
		printk(KERN_ERR
		       "PM Suspend cannot acquire ARM(PLL4) clock control\n");
		return PTR_ERR(pll4_clk);
	}

	if (pnx4008_cpu_standby_sz > pnx4008_cpu_suspend_sz)
		sram_size_to_allocate = pnx4008_cpu_standby_sz;
	else
		sram_size_to_allocate = pnx4008_cpu_suspend_sz;

	saved_sram = kmalloc(sram_size_to_allocate, GFP_ATOMIC);
	if (!saved_sram) {
		printk(KERN_ERR
		       "PM Suspend: cannot allocate memory to save portion of SRAM\n");
		clk_put(pll4_clk);
		return -ENOMEM;
	}

	suspend_set_ops(&pnx4008_pm_ops);
	return 0;
}
