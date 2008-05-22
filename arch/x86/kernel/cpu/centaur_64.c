#include <linux/init.h>
#include <linux/smp.h>

#include <asm/cpufeature.h>
#include <asm/processor.h>

void __cpuinit early_init_centaur(struct cpuinfo_x86 *c)
{
	if (c->x86 == 0x6 && c->x86_model >= 0xf)
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
}

void __cpuinit init_centaur(struct cpuinfo_x86 *c)
{
	/* Cache sizes */
	unsigned n;

	n = c->extended_cpuid_level;
	if (n >= 0x80000008) {
		unsigned eax = cpuid_eax(0x80000008);
		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
	}

	if (c->x86 == 0x6 && c->x86_model >= 0xf) {
		c->x86_cache_alignment = c->x86_clflush_size * 2;
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);
	}
	set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
}
