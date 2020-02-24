/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Variables and functions used by the code in sleep.c
 */

#include <linux/linkage.h>

extern unsigned long saved_video_mode;
extern long saved_magic;

extern int wakeup_pmode_return;

extern u8 wake_sleep_flags;

extern unsigned long acpi_copy_wakeup_routine(unsigned long);
extern void wakeup_long64(void);

extern void do_suspend_lowlevel(void);

extern int x86_acpi_suspend_lowlevel(void);

acpi_status asmlinkage x86_acpi_enter_sleep_state(u8 state);
