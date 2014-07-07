/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *      void check_bugs(void);
 */
#ifndef __ASM_AVR32_BUGS_H
#define __ASM_AVR32_BUGS_H

static void __init check_bugs(void)
{
	boot_cpu_data.loops_per_jiffy = loops_per_jiffy;
}

#endif /* __ASM_AVR32_BUGS_H */
