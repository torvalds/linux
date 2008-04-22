/*
 *	Variables and functions used by the code in sleep.c
 */

#include <asm/trampoline.h>

extern char wakeup_code_start, wakeup_code_end;

extern unsigned long saved_video_mode;
extern long saved_magic;

extern int wakeup_pmode_return;
extern char swsusp_pg_dir[PAGE_SIZE];

extern unsigned long acpi_copy_wakeup_routine(unsigned long);
extern void wakeup_long64(void);
