/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_PT_H
#define _ASM_X86_INTEL_PT_H

#define PT_CPUID_LEAVES		2
#define PT_CPUID_REGS_NUM	4 /* number of regsters (eax, ebx, ecx, edx) */

enum pt_capabilities {
	PT_CAP_max_subleaf = 0,
	PT_CAP_cr3_filtering,
	PT_CAP_psb_cyc,
	PT_CAP_ip_filtering,
	PT_CAP_mtc,
	PT_CAP_ptwrite,
	PT_CAP_power_event_trace,
	PT_CAP_topa_output,
	PT_CAP_topa_multiple_entries,
	PT_CAP_single_range_output,
	PT_CAP_payloads_lip,
	PT_CAP_num_address_ranges,
	PT_CAP_mtc_periods,
	PT_CAP_cycle_thresholds,
	PT_CAP_psb_periods,
};

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_INTEL)
void cpu_emergency_stop_pt(void);
extern u32 intel_pt_validate_hw_cap(enum pt_capabilities cap);
#else
static inline void cpu_emergency_stop_pt(void) {}
static inline u32 intel_pt_validate_hw_cap(enum pt_capabilities cap) { return 0; }
#endif

#endif /* _ASM_X86_INTEL_PT_H */
