// SPDX-Identifier: GPL-2.0
#include <linux/init.h>
#include <asm/bugs.h>
#include <asm/proc-fns.h>

void __init check_bugs(void)
{
	check_writebuffer_bugs();
}
