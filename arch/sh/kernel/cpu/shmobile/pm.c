/*
 * arch/sh/kernel/cpu/sh4a/pm-sh_mobile.c
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
 *
 * All modes should be tied in with cpuidle. But before that can
 * happen we need to keep track of enabled hardware blocks so we
 * can avoid entering sleep modes that stop clocks to hardware
 * blocks that are in use even though the cpu core is idle.
 */

extern const unsigned char sh_mobile_standby[];
extern const unsigned int sh_mobile_standby_size;

static void sh_mobile_call_standby(unsigned long mode)
{
	extern void *vbr_base;
	void *onchip_mem = (void *)0xe5200000; /* ILRAM */
	void (*standby_onchip_mem)(unsigned long) = onchip_mem;

	/* Note: Wake up from sleep may generate exceptions!
	 * Setup VBR to point to on-chip ram if self-refresh is
	 * going to be used.
	 */
	if (mode & SUSP_SH_SF)
		asm volatile("ldc %0, vbr" : : "r" (onchip_mem) : "memory");

	/* Copy the assembly snippet to the otherwise ununsed ILRAM */
	memcpy(onchip_mem, sh_mobile_standby, sh_mobile_standby_size);
	wmb();
	ctrl_barrier();

	/* Let assembly snippet in on-chip memory handle the rest */
	standby_onchip_mem(mode);

	/* Put VBR back in System RAM again */
	if (mode & SUSP_SH_SF)
		asm volatile("ldc %0, vbr" : : "r" (&vbr_base) : "memory");
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
	suspend_set_ops(&sh_pm_ops);
	return 0;
}

late_initcall(sh_pm_init);
