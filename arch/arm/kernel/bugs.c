// SPDX-Identifier: GPL-2.0
#include <linux/init.h>
#include <asm/s.h>
#include <asm/proc-fns.h>

void check_other_s(void)
{
#ifdef MULTI_CPU
	if (cpu_check_s)
		cpu_check_s();
#endif
}

void __init check_s(void)
{
	check_writebuffer_s();
	check_other_s();
}
