#include <linux/init.h>
#include <linux/smp.h>

#include <asm/cpufeature.h>
#include <asm/processor.h>

#include "cpu.h"

static void __cpuinit early_init_centaur(struct cpuinfo_x86 *c)
{
	if (c->x86 == 0x6 && c->x86_model >= 0xf)
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);

	set_cpu_cap(c, X86_FEATURE_SYSENTER32);
}

static void __cpuinit init_centaur(struct cpuinfo_x86 *c)
{
	early_init_centaur(c);

	if (c->x86 == 0x6 && c->x86_model >= 0xf) {
		c->x86_cache_alignment = c->x86_clflush_size * 2;
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);
	}
	set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
}

static struct cpu_dev centaur_cpu_dev __cpuinitdata = {
	.c_vendor	= "Centaur",
	.c_ident	= { "CentaurHauls" },
	.c_early_init	= early_init_centaur,
	.c_init		= init_centaur,
	.c_x86_vendor	= X86_VENDOR_CENTAUR,
};

cpu_dev_register(centaur_cpu_dev);

