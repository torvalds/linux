/*
 *  arch/x86_64/kernel/bugs.c
 *
 *  Copyright (C) 1994  Linus Torvalds
 *  Copyright (C) 2000  SuSE
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/alternative.h>
#include <asm/processor.h>

void __init check_bugs(void)
{
	identify_cpu(&boot_cpu_data);
#if !defined(CONFIG_SMP)
	printk("CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif
	alternative_instructions();
}
