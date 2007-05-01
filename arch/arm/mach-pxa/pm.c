/*
 * PXA250/210 Power Management Routines
 *
 * Original code for the SA11x0:
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * Modified for the PXA250 by Nicolas Pitre:
 * Copyright (c) 2002 Monta Vista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>

#include <asm/hardware.h>
#include <asm/memory.h>
#include <asm/system.h>
#include <asm/arch/pm.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/lubbock.h>
#include <asm/mach/time.h>


/*
 * Debug macros
 */
#undef DEBUG

#define SAVE(x)		sleep_save[SLEEP_SAVE_##x] = x
#define RESTORE(x)	x = sleep_save[SLEEP_SAVE_##x]

#define RESTORE_GPLEVEL(n) do { \
	GPSR##n = sleep_save[SLEEP_SAVE_GPLR##n]; \
	GPCR##n = ~sleep_save[SLEEP_SAVE_GPLR##n]; \
} while (0)

/*
 * List of global PXA peripheral registers to preserve.
 * More ones like CP and general purpose register values are preserved
 * with the stack pointer in sleep.S.
 */
enum {	SLEEP_SAVE_START = 0,

	SLEEP_SAVE_GPLR0, SLEEP_SAVE_GPLR1, SLEEP_SAVE_GPLR2, SLEEP_SAVE_GPLR3,
	SLEEP_SAVE_GPDR0, SLEEP_SAVE_GPDR1, SLEEP_SAVE_GPDR2, SLEEP_SAVE_GPDR3,
	SLEEP_SAVE_GRER0, SLEEP_SAVE_GRER1, SLEEP_SAVE_GRER2, SLEEP_SAVE_GRER3,
	SLEEP_SAVE_GFER0, SLEEP_SAVE_GFER1, SLEEP_SAVE_GFER2, SLEEP_SAVE_GFER3,
	SLEEP_SAVE_PGSR0, SLEEP_SAVE_PGSR1, SLEEP_SAVE_PGSR2, SLEEP_SAVE_PGSR3,

	SLEEP_SAVE_GAFR0_L, SLEEP_SAVE_GAFR0_U,
	SLEEP_SAVE_GAFR1_L, SLEEP_SAVE_GAFR1_U,
	SLEEP_SAVE_GAFR2_L, SLEEP_SAVE_GAFR2_U,
	SLEEP_SAVE_GAFR3_L, SLEEP_SAVE_GAFR3_U,

	SLEEP_SAVE_PSTR,

	SLEEP_SAVE_ICMR,
	SLEEP_SAVE_CKEN,

#ifdef CONFIG_PXA27x
 	SLEEP_SAVE_MDREFR,
 	SLEEP_SAVE_PWER, SLEEP_SAVE_PCFR, SLEEP_SAVE_PRER,
 	SLEEP_SAVE_PFER, SLEEP_SAVE_PKWR,
#endif

	SLEEP_SAVE_CKSUM,

	SLEEP_SAVE_SIZE
};


int pxa_pm_enter(suspend_state_t state)
{
	unsigned long sleep_save[SLEEP_SAVE_SIZE];
	unsigned long checksum = 0;
	struct timespec delta, rtc;
	int i;
	extern void pxa_cpu_pm_enter(suspend_state_t state);

#ifdef CONFIG_IWMMXT
	/* force any iWMMXt context to ram **/
	if (elf_hwcap & HWCAP_IWMMXT)
		iwmmxt_task_disable(NULL);
#endif

	/* preserve current time */
	rtc.tv_sec = RCNR;
	rtc.tv_nsec = 0;
	save_time_delta(&delta, &rtc);

	SAVE(GPLR0); SAVE(GPLR1); SAVE(GPLR2);
	SAVE(GPDR0); SAVE(GPDR1); SAVE(GPDR2);
	SAVE(GRER0); SAVE(GRER1); SAVE(GRER2);
	SAVE(GFER0); SAVE(GFER1); SAVE(GFER2);
	SAVE(PGSR0); SAVE(PGSR1); SAVE(PGSR2);

	SAVE(GAFR0_L); SAVE(GAFR0_U);
	SAVE(GAFR1_L); SAVE(GAFR1_U);
	SAVE(GAFR2_L); SAVE(GAFR2_U);

#ifdef CONFIG_PXA27x
	SAVE(MDREFR);
	SAVE(GPLR3); SAVE(GPDR3); SAVE(GRER3); SAVE(GFER3); SAVE(PGSR3);
	SAVE(GAFR3_L); SAVE(GAFR3_U);
	SAVE(PWER); SAVE(PCFR); SAVE(PRER);
	SAVE(PFER); SAVE(PKWR);
#endif

	SAVE(ICMR);
	ICMR = 0;

	SAVE(CKEN);
	SAVE(PSTR);

	/* Note: wake up source are set up in each machine specific files */

	/* clear GPIO transition detect  bits */
	GEDR0 = GEDR0; GEDR1 = GEDR1; GEDR2 = GEDR2;
#ifdef CONFIG_PXA27x
	GEDR3 = GEDR3;
#endif

	/* Clear sleep reset status */
	RCSR = RCSR_SMR;

	/* before sleeping, calculate and save a checksum */
	for (i = 0; i < SLEEP_SAVE_SIZE - 1; i++)
		checksum += sleep_save[i];
	sleep_save[SLEEP_SAVE_CKSUM] = checksum;

	/* *** go zzz *** */
	pxa_cpu_pm_enter(state);

	cpu_init();

	/* after sleeping, validate the checksum */
	checksum = 0;
	for (i = 0; i < SLEEP_SAVE_SIZE - 1; i++)
		checksum += sleep_save[i];

	/* if invalid, display message and wait for a hardware reset */
	if (checksum != sleep_save[SLEEP_SAVE_CKSUM]) {
#ifdef CONFIG_ARCH_LUBBOCK
		LUB_HEXLED = 0xbadbadc5;
#endif
		while (1)
			pxa_cpu_pm_enter(state);
	}

	/* ensure not to come back here if it wasn't intended */
	PSPR = 0;

	/* restore registers */
	RESTORE_GPLEVEL(0); RESTORE_GPLEVEL(1); RESTORE_GPLEVEL(2);
	RESTORE(GPDR0); RESTORE(GPDR1); RESTORE(GPDR2);
	RESTORE(GAFR0_L); RESTORE(GAFR0_U);
	RESTORE(GAFR1_L); RESTORE(GAFR1_U);
	RESTORE(GAFR2_L); RESTORE(GAFR2_U);
	RESTORE(GRER0); RESTORE(GRER1); RESTORE(GRER2);
	RESTORE(GFER0); RESTORE(GFER1); RESTORE(GFER2);
	RESTORE(PGSR0); RESTORE(PGSR1); RESTORE(PGSR2);

#ifdef CONFIG_PXA27x
	RESTORE(MDREFR);
	RESTORE_GPLEVEL(3); RESTORE(GPDR3);
	RESTORE(GAFR3_L); RESTORE(GAFR3_U);
	RESTORE(GRER3); RESTORE(GFER3); RESTORE(PGSR3);
	RESTORE(PWER); RESTORE(PCFR); RESTORE(PRER);
	RESTORE(PFER); RESTORE(PKWR);
#endif

	PSSR = PSSR_RDH | PSSR_PH;

	RESTORE(CKEN);

	ICLR = 0;
	ICCR = 1;
	RESTORE(ICMR);

	RESTORE(PSTR);

	/* restore current time */
	rtc.tv_sec = RCNR;
	restore_time_delta(&delta, &rtc);

#ifdef DEBUG
	printk(KERN_DEBUG "*** made it back from resume\n");
#endif

	return 0;
}

EXPORT_SYMBOL_GPL(pxa_pm_enter);

unsigned long sleep_phys_sp(void *sp)
{
	return virt_to_phys(sp);
}

/*
 * Called after processes are frozen, but before we shut down devices.
 */
int pxa_pm_prepare(suspend_state_t state)
{
	extern int pxa_cpu_pm_prepare(suspend_state_t state);

	return pxa_cpu_pm_prepare(state);
}

EXPORT_SYMBOL_GPL(pxa_pm_prepare);

/*
 * Called after devices are re-setup, but before processes are thawed.
 */
int pxa_pm_finish(suspend_state_t state)
{
	return 0;
}

EXPORT_SYMBOL_GPL(pxa_pm_finish);

static struct pm_ops pxa_pm_ops = {
	.prepare	= pxa_pm_prepare,
	.enter		= pxa_pm_enter,
	.finish		= pxa_pm_finish,
	.valid		= pm_valid_only_mem,
};

static int __init pxa_pm_init(void)
{
	pm_set_ops(&pxa_pm_ops);
	return 0;
}

device_initcall(pxa_pm_init);
