/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPU_H
#define _ASM_X86_CPU_H

#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/nodemask.h>
#include <linux/percpu.h>
#include <asm/ibt.h>

#ifdef CONFIG_SMP

extern void prefill_possible_map(void);

#else /* CONFIG_SMP */

static inline void prefill_possible_map(void) {}

#define cpu_physical_id(cpu)			boot_cpu_physical_apicid
#define cpu_acpi_id(cpu)			0
#define safe_smp_processor_id()			0

#endif /* CONFIG_SMP */

struct x86_cpu {
	struct cpu cpu;
};

#ifdef CONFIG_HOTPLUG_CPU
extern void soft_restart_cpu(void);
#endif

extern void ap_init_aperfmperf(void);

int mwait_usable(const struct cpuinfo_x86 *);

unsigned int x86_family(unsigned int sig);
unsigned int x86_model(unsigned int sig);
unsigned int x86_stepping(unsigned int sig);
#ifdef CONFIG_CPU_SUP_INTEL
extern void __init sld_setup(struct cpuinfo_x86 *c);
extern bool handle_user_split_lock(struct pt_regs *regs, long error_code);
extern bool handle_guest_split_lock(unsigned long ip);
extern void handle_bus_lock(struct pt_regs *regs);
u8 get_this_hybrid_cpu_type(void);
#else
static inline void __init sld_setup(struct cpuinfo_x86 *c) {}
static inline bool handle_user_split_lock(struct pt_regs *regs, long error_code)
{
	return false;
}

static inline bool handle_guest_split_lock(unsigned long ip)
{
	return false;
}

static inline void handle_bus_lock(struct pt_regs *regs) {}

static inline u8 get_this_hybrid_cpu_type(void)
{
	return 0;
}
#endif
#ifdef CONFIG_IA32_FEAT_CTL
void init_ia32_feat_ctl(struct cpuinfo_x86 *c);
#else
static inline void init_ia32_feat_ctl(struct cpuinfo_x86 *c) {}
#endif

extern __noendbr void cet_disable(void);

struct cpu_signature;

void intel_collect_cpu_info(struct cpu_signature *sig);

extern u64 x86_read_arch_cap_msr(void);
bool intel_find_matching_signature(void *mc, struct cpu_signature *sig);
int intel_microcode_sanity_check(void *mc, bool print_err, int hdr_type);

extern struct cpumask cpus_stop_mask;

#endif /* _ASM_X86_CPU_H */
