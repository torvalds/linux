/*
 * sh7372 Power management support
 *
 *  Copyright (C) 2011 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include <mach/common.h>

#define SMFRAM 0xe6a70000
#define SYSTBCR 0xe6150024
#define SBAR 0xe6180020
#define APARMBAREA 0xe6f10020

#ifdef CONFIG_SUSPEND
static void sh7372_enter_core_standby(void)
{
	void __iomem *smfram = (void __iomem *)SMFRAM;

	__raw_writel(0, APARMBAREA); /* translate 4k */
	__raw_writel(__pa(sh7372_cpu_resume), SBAR); /* set reset vector */
	__raw_writel(0x10, SYSTBCR); /* enable core standby */

	__raw_writel(0, smfram + 0x3c); /* clear page table address */

	sh7372_cpu_suspend();
	cpu_init();

	/* if page table address is non-NULL then we have been powered down */
	if (__raw_readl(smfram + 0x3c)) {
		__raw_writel(__raw_readl(smfram + 0x40),
			     __va(__raw_readl(smfram + 0x3c)));

		flush_tlb_all();
		set_cr(__raw_readl(smfram + 0x38));
	}

	__raw_writel(0, SYSTBCR); /* disable core standby */
	__raw_writel(0, SBAR); /* disable reset vector translation */
}

static int sh7372_enter_suspend(suspend_state_t suspend_state)
{
	sh7372_enter_core_standby();
	return 0;
}

static void sh7372_suspend_init(void)
{
	shmobile_suspend_ops.enter = sh7372_enter_suspend;
}
#else
static void sh7372_suspend_init(void) {}
#endif

#define DBGREG1 0xe6100020
#define DBGREG9 0xe6100040

void __init sh7372_pm_init(void)
{
	/* enable DBG hardware block to kick SYSC */
	__raw_writel(0x0000a500, DBGREG9);
	__raw_writel(0x0000a501, DBGREG9);
	__raw_writel(0x00000000, DBGREG1);

	sh7372_suspend_init();
}
