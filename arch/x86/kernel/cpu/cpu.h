/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_CPU_H
#define ARCH_X86_CPU_H

/* attempt to consolidate cpu attributes */
struct cpu_dev {
	const char	*c_vendor;

	/* some have two possibilities for cpuid string */
	const char	*c_ident[2];

	void            (*c_early_init)(struct cpuinfo_x86 *);
	void		(*c_bsp_init)(struct cpuinfo_x86 *);
	void		(*c_init)(struct cpuinfo_x86 *);
	void		(*c_identify)(struct cpuinfo_x86 *);
	void		(*c_detect_tlb)(struct cpuinfo_x86 *);
	void		(*c_bsp_resume)(struct cpuinfo_x86 *);
	int		c_x86_vendor;
#ifdef CONFIG_X86_32
	/* Optional vendor specific routine to obtain the cache size. */
	unsigned int	(*legacy_cache_size)(struct cpuinfo_x86 *,
					     unsigned int);

	/* Family/stepping-based lookup table for model names. */
	struct legacy_cpu_model_info {
		int		family;
		const char	*model_names[16];
	}		legacy_models[5];
#endif
};

struct _tlb_table {
	unsigned char descriptor;
	char tlb_type;
	unsigned int entries;
	/* unsigned int ways; */
	char info[128];
};

#define cpu_dev_register(cpu_devX) \
	static const struct cpu_dev *const __cpu_dev_##cpu_devX __used \
	__attribute__((__section__(".x86_cpu_dev.init"))) = \
	&cpu_devX;

extern const struct cpu_dev *const __x86_cpu_dev_start[],
			    *const __x86_cpu_dev_end[];

extern void get_cpu_cap(struct cpuinfo_x86 *c);
extern void cpu_detect_cache_sizes(struct cpuinfo_x86 *c);
extern void init_scattered_cpuid_features(struct cpuinfo_x86 *c);
extern u32 get_scattered_cpuid_leaf(unsigned int level,
				    unsigned int sub_leaf,
				    enum cpuid_regs_idx reg);
extern unsigned int init_intel_cacheinfo(struct cpuinfo_x86 *c);
extern void init_amd_cacheinfo(struct cpuinfo_x86 *c);

extern int detect_extended_topology(struct cpuinfo_x86 *c);
extern void detect_ht(struct cpuinfo_x86 *c);

unsigned int aperfmperf_get_khz(int cpu);

#endif /* ARCH_X86_CPU_H */
