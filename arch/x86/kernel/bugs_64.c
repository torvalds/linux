/*
 *  Copyright (C) 1994  Linus Torvalds
 *  Copyright (C) 2000  SuSE
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/alternative.h>
#include <asm/bugs.h>
#include <asm/processor.h>
#include <asm/mtrr.h>

void __init check_bugs(void)
{
	identify_boot_cpu();
#if !defined(CONFIG_SMP)
	printk("CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif
	alternative_instructions();
}
