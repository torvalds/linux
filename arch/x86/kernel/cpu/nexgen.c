#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/processor.h>

#include "cpu.h"

/*
 *	Detect a NexGen CPU running without BIOS hypercode new enough
 *	to have CPUID. (Thanks to Herbert Oppmann)
 */

static int __cpuinit deep_magic_nexgen_probe(void)
{
	int ret;

	__asm__ __volatile__ (
		"	movw	$0x5555, %%ax\n"
		"	xorw	%%dx,%%dx\n"
		"	movw	$2, %%cx\n"
		"	divw	%%cx\n"
		"	movl	$0, %%eax\n"
		"	jnz	1f\n"
		"	movl	$1, %%eax\n"
		"1:\n"
		: "=a" (ret) : : "cx", "dx");
	return  ret;
}

static void __cpuinit init_nexgen(struct cpuinfo_x86 *c)
{
	c->x86_cache_size = 256; /* A few had 1 MB... */
}

static void __cpuinit nexgen_identify(struct cpuinfo_x86 *c)
{
	/* Detect NexGen with old hypercode */
	if (deep_magic_nexgen_probe())
		strcpy(c->x86_vendor_id, "NexGenDriven");
}

static struct cpu_dev nexgen_cpu_dev __cpuinitdata = {
	.c_vendor	= "Nexgen",
	.c_ident	= { "NexGenDriven" },
	.c_models = {
			{ .vendor = X86_VENDOR_NEXGEN,
			  .family = 5,
			  .model_names = { [1] = "Nx586" }
			},
	},
	.c_init		= init_nexgen,
	.c_identify	= nexgen_identify,
};

int __init nexgen_init_cpu(void)
{
	cpu_devs[X86_VENDOR_NEXGEN] = &nexgen_cpu_dev;
	return 0;
}
