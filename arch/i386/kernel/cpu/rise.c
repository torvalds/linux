#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <asm/processor.h>

#include "cpu.h"

static void __cpuinit init_rise(struct cpuinfo_x86 *c)
{
	printk("CPU: Rise iDragon");
	if (c->x86_model > 2)
		printk(" II");
	printk("\n");

	/* Unhide possibly hidden capability flags
	   The mp6 iDragon family don't have MSRs.
	   We switch on extra features with this cpuid weirdness: */
	__asm__ (
		"movl $0x6363452a, %%eax\n\t"
		"movl $0x3231206c, %%ecx\n\t"
		"movl $0x2a32313a, %%edx\n\t"
		"cpuid\n\t"
		"movl $0x63634523, %%eax\n\t"
		"movl $0x32315f6c, %%ecx\n\t"
		"movl $0x2333313a, %%edx\n\t"
		"cpuid\n\t" : : : "eax", "ebx", "ecx", "edx"
	);
	set_bit(X86_FEATURE_CX8, c->x86_capability);
}

static struct cpu_dev rise_cpu_dev __cpuinitdata = {
	.c_vendor	= "Rise",
	.c_ident	= { "RiseRiseRise" },
	.c_models = {
		{ .vendor = X86_VENDOR_RISE, .family = 5, .model_names = 
		  { 
			  [0] = "iDragon", 
			  [2] = "iDragon", 
			  [8] = "iDragon II", 
			  [9] = "iDragon II"
		  }
		},
	},
	.c_init		= init_rise,
};

int __init rise_init_cpu(void)
{
	cpu_devs[X86_VENDOR_RISE] = &rise_cpu_dev;
	return 0;
}

