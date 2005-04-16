/*
 * SA1100 Power Management Routines
 *
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 *
 * History:
 *
 * 2001-02-06:	Cliff Brake         Initial code
 *
 * 2001-02-25:	Sukjae Cho <sjcho@east.isi.edu> &
 * 		Chester Kuo <chester@linux.org.tw>
 * 			Save more value for the resume function! Support
 * 			Bitsy/Assabet/Freebird board
 *
 * 2001-08-29:	Nicolas Pitre <nico@cam.org>
 * 			Cleaned up, pushed platform dependent stuff
 * 			in the platform specific files.
 *
 * 2002-05-27:	Nicolas Pitre	Killed sleep.h and the kmalloced save array.
 * 				Storage is local on the stack now.
 */
#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>

#include <asm/hardware.h>
#include <asm/memory.h>
#include <asm/system.h>
#include <asm/mach/time.h>

extern void sa1100_cpu_suspend(void);
extern void sa1100_cpu_resume(void);

#define SAVE(x)		sleep_save[SLEEP_SAVE_##x] = x
#define RESTORE(x)	x = sleep_save[SLEEP_SAVE_##x]

/*
 * List of global SA11x0 peripheral registers to preserve.
 * More ones like CP and general purpose register values are preserved
 * on the stack and then the stack pointer is stored last in sleep.S.
 */
enum {	SLEEP_SAVE_SP = 0,

	SLEEP_SAVE_GPDR, SLEEP_SAVE_GAFR,
	SLEEP_SAVE_PPDR, SLEEP_SAVE_PPSR, SLEEP_SAVE_PPAR, SLEEP_SAVE_PSDR,

	SLEEP_SAVE_Ser1SDCR0,

	SLEEP_SAVE_SIZE
};


static int sa11x0_pm_enter(suspend_state_t state)
{
	unsigned long gpio, sleep_save[SLEEP_SAVE_SIZE];
	struct timespec delta, rtc;

	if (state != PM_SUSPEND_MEM)
		return -EINVAL;

	/* preserve current time */
	rtc.tv_sec = RCNR;
	rtc.tv_nsec = 0;
	save_time_delta(&delta, &rtc);
	gpio = GPLR;

	/* save vital registers */
	SAVE(GPDR);
	SAVE(GAFR);

	SAVE(PPDR);
	SAVE(PPSR);
	SAVE(PPAR);
	SAVE(PSDR);

	SAVE(Ser1SDCR0);

	/* Clear previous reset status */
	RCSR = RCSR_HWR | RCSR_SWR | RCSR_WDR | RCSR_SMR;

	/* set resume return address */
	PSPR = virt_to_phys(sa1100_cpu_resume);

	/* go zzz */
	sa1100_cpu_suspend();

	/*
	 * Ensure not to come back here if it wasn't intended
	 */
	PSPR = 0;

	/*
	 * Ensure interrupt sources are disabled; we will re-init
	 * the interrupt subsystem via the device manager.
	 */
	ICLR = 0;
	ICCR = 1;
	ICMR = 0;

	/* restore registers */
	RESTORE(GPDR);
	RESTORE(GAFR);

	RESTORE(PPDR);
	RESTORE(PPSR);
	RESTORE(PPAR);
	RESTORE(PSDR);

	RESTORE(Ser1SDCR0);

	GPSR = gpio;
	GPCR = ~gpio;

	/*
	 * Clear the peripheral sleep-hold bit.
	 */
	PSSR = PSSR_PH;

	/* restore current time */
	rtc.tv_sec = RCNR;
	restore_time_delta(&delta, &rtc);

	return 0;
}

unsigned long sleep_phys_sp(void *sp)
{
	return virt_to_phys(sp);
}

/*
 * Called after processes are frozen, but before we shut down devices.
 */
static int sa11x0_pm_prepare(suspend_state_t state)
{
	return 0;
}

/*
 * Called after devices are re-setup, but before processes are thawed.
 */
static int sa11x0_pm_finish(suspend_state_t state)
{
	return 0;
}

/*
 * Set to PM_DISK_FIRMWARE so we can quickly veto suspend-to-disk.
 */
static struct pm_ops sa11x0_pm_ops = {
	.pm_disk_mode	= PM_DISK_FIRMWARE,
	.prepare	= sa11x0_pm_prepare,
	.enter		= sa11x0_pm_enter,
	.finish		= sa11x0_pm_finish,
};

static int __init sa11x0_pm_init(void)
{
	pm_set_ops(&sa11x0_pm_ops);
	return 0;
}

late_initcall(sa11x0_pm_init);
