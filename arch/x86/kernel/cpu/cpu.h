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
	__section(".x86_cpu_dev.init") = \
	&cpu_devX;

extern const struct cpu_dev *const __x86_cpu_dev_start[],
			    *const __x86_cpu_dev_end[];

#ifdef CONFIG_CPU_SUP_INTEL
enum tsx_ctrl_states {
	TSX_CTRL_ENABLE,
	TSX_CTRL_DISABLE,
	TSX_CTRL_RTM_ALWAYS_ABORT,
	TSX_CTRL_NOT_SUPPORTED,
};

extern __ro_after_init enum tsx_ctrl_states tsx_ctrl_state;

extern void __init tsx_init(void);
extern void tsx_enable(void);
extern void tsx_disable(void);
extern void tsx_clear_cpuid(void);
#else
static inline void tsx_init(void) { }
#endif /* CONFIG_CPU_SUP_INTEL */

extern void get_cpu_cap(struct cpuinfo_x86 *c);
extern void get_cpu_address_sizes(struct cpuinfo_x86 *c);
extern void cpu_detect_cache_sizes(struct cpuinfo_x86 *c);
extern void init_scattered_cpuid_features(struct cpuinfo_x86 *c);
extern void init_intel_cacheinfo(struct cpuinfo_x86 *c);
extern void init_amd_cacheinfo(struct cpuinfo_x86 *c);
extern void init_hygon_cacheinfo(struct cpuinfo_x86 *c);

extern void detect_num_cpu_cores(struct cpuinfo_x86 *c);
extern int detect_extended_topology_early(struct cpuinfo_x86 *c);
extern int detect_extended_topology(struct cpuinfo_x86 *c);
extern int detect_ht_early(struct cpuinfo_x86 *c);
extern void detect_ht(struct cpuinfo_x86 *c);
extern void check_null_seg_clears_base(struct cpuinfo_x86 *c);

unsigned int aperfmperf_get_khz(int cpu);

extern void x86_spec_ctrl_setup_ap(void);
extern void update_srbds_msr(void);

extern u64 x86_read_arch_cap_msr(void);

#endif /* ARCH_X86_CPU_H */
