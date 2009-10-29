/*
 * arch/sh/kernel/cpu/shmobile/pm.c
 *
 * Power management support code for SuperH Mobile
 *
 *  Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <asm/suspend.h>
#include <asm/uaccess.h>

/*
 * Notifier lists for pre/post sleep notification
 */
ATOMIC_NOTIFIER_HEAD(sh_mobile_pre_sleep_notifier_list);
ATOMIC_NOTIFIER_HEAD(sh_mobile_post_sleep_notifier_list);

/*
 * Sleep modes available on SuperH Mobile:
 *
 * Sleep mode is just plain "sleep" instruction
 * Sleep Self-Refresh mode is above plus RAM put in Self-Refresh
 * Standby Self-Refresh mode is above plus stopped clocks
 */
#define SUSP_MODE_SLEEP		(SUSP_SH_SLEEP)
#define SUSP_MODE_SLEEP_SF	(SUSP_SH_SLEEP | SUSP_SH_SF)
#define SUSP_MODE_STANDBY_SF	(SUSP_SH_STANDBY | SUSP_SH_SF)

/*
 * The following modes are not there yet:
 *
 * R-standby mode is unsupported, but will be added in the future
 * U-standby mode is low priority since it needs bootloader hacks
 */

#define ILRAM_BASE 0xe5200000

extern const unsigned char sh_mobile_standby[];
extern const unsigned int sh_mobile_standby_size;

void sh_mobile_call_standby(unsigned long mode)
{
	void *onchip_mem = (void *)ILRAM_BASE;
	void (*standby_onchip_mem)(unsigned long, unsigned long) = onchip_mem;

	atomic_notifier_call_chain(&sh_mobile_pre_sleep_notifier_list,
				   mode, NULL);

	/* Let assembly snippet in on-chip memory handle the rest */
	standby_onchip_mem(mode, ILRAM_BASE);

	atomic_notifier_call_chain(&sh_mobile_post_sleep_notifier_list,
				   mode, NULL);
}

void sh_mobile_register_self_refresh(unsigned long flags,
				     void *pre_start, void *pre_end,
				     void *post_start, void *post_end)
{
}

static int sh_pm_enter(suspend_state_t state)
{
	local_irq_disable();
	set_bl_bit();
	sh_mobile_call_standby(SUSP_MODE_STANDBY_SF);
	local_irq_disable();
	clear_bl_bit();
	return 0;
}

static struct platform_suspend_ops sh_pm_ops = {
	.enter          = sh_pm_enter,
	.valid          = suspend_valid_only_mem,
};

static int __init sh_pm_init(void)
{
	void *onchip_mem = (void *)ILRAM_BASE;

	/* Copy the assembly snippet to the otherwise ununsed ILRAM */
	memcpy(onchip_mem, sh_mobile_standby, sh_mobile_standby_size);
	wmb();
	ctrl_barrier();

	suspend_set_ops(&sh_pm_ops);
	sh_mobile_setup_cpuidle();
	return 0;
}

late_initcall(sh_pm_init);
