/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_CPU_H
#define ARCH_X86_CPU_H

#include <asm/cpu.h>
#include <asm/topology.h>

#include "topology.h"

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
void tsx_ap_init(void);
void intel_unlock_cpuid_leafs(struct cpuinfo_x86 *c);
#else
static inline void tsx_init(void) { }
static inline void tsx_ap_init(void) { }
static inline void intel_unlock_cpuid_leafs(struct cpuinfo_x86 *c) { }
#endif /* CONFIG_CPU_SUP_INTEL */

extern void init_spectral_chicken(struct cpuinfo_x86 *c);

extern void get_cpu_cap(struct cpuinfo_x86 *c);
extern void get_cpu_address_sizes(struct cpuinfo_x86 *c);
extern void cpu_detect_cache_sizes(struct cpuinfo_x86 *c);
extern void init_scattered_cpuid_features(struct cpuinfo_x86 *c);
extern void init_intel_cacheinfo(struct cpuinfo_x86 *c);
extern void init_amd_cacheinfo(struct cpuinfo_x86 *c);
extern void init_hygon_cacheinfo(struct cpuinfo_x86 *c);

extern void check_null_seg_clears_base(struct cpuinfo_x86 *c);

void cacheinfo_amd_init_llc_id(struct cpuinfo_x86 *c, u16 die_id);
void cacheinfo_hygon_init_llc_id(struct cpuinfo_x86 *c);

#if defined(CONFIG_AMD_NB) && defined(CONFIG_SYSFS)
struct amd_northbridge *amd_init_l3_cache(int index);
#else
static inline struct amd_northbridge *amd_init_l3_cache(int index)
{
	return NULL;
}
#endif

unsigned int aperfmperf_get_khz(int cpu);
void cpu_select_mitigations(void);

extern void x86_spec_ctrl_setup_ap(void);
extern void update_srbds_msr(void);
extern void update_gds_msr(void);

extern enum spectre_v2_mitigation spectre_v2_enabled;

static inline bool spectre_v2_in_eibrs_mode(enum spectre_v2_mitigation mode)
{
	return mode == SPECTRE_V2_EIBRS ||
	       mode == SPECTRE_V2_EIBRS_RETPOLINE ||
	       mode == SPECTRE_V2_EIBRS_LFENCE;
}

#endif /* ARCH_X86_CPU_H */
