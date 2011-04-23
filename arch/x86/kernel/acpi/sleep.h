/*
 *	Variables and functions used by the code in sleep.c
 */

#include <asm/trampoline.h>

extern unsigned long saved_video_mode;
extern long saved_magic;

extern int wakeup_pmode_return;

extern unsigned long acpi_copy_wakeup_routine(unsigned long);
extern void wakeup_long64(void);

extern void do_suspend_lowlevel(void);
