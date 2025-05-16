/* SPDX-License-Identifier: GPL-2.0 */
#ifndef XEN_OPS_H
#define XEN_OPS_H

#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/irqreturn.h>
#include <linux/linkage.h>

#include <xen/interface/xenpmu.h>
#include <xen/xen-ops.h>

#include <asm/page.h>

#include <trace/events/xen.h>

/* These are code, but not functions.  Defined in entry.S */
extern const char xen_failsafe_callback[];

void xen_entry_SYSENTER_compat(void);
#ifdef CONFIG_X86_64
void xen_entry_SYSCALL_64(void);
void xen_entry_SYSCALL_compat(void);
#endif

extern void *xen_initial_gdt;

struct trap_info;
void xen_copy_trap_info(struct trap_info *traps);

DECLARE_PER_CPU_ALIGNED(struct vcpu_info, xen_vcpu_info);
DECLARE_PER_CPU(unsigned long, xen_cr3);

extern struct start_info *xen_start_info;
extern struct shared_info xen_dummy_shared_info;
extern struct shared_info *HYPERVISOR_shared_info;

void xen_setup_mfn_list_list(void);
void xen_build_mfn_list_list(void);
void xen_setup_machphys_mapping(void);
void xen_setup_kernel_pagetable(pgd_t *pgd, unsigned long max_pfn);
void __init xen_reserve_special_pages(void);
void __init xen_pt_check_e820(void);

void xen_mm_pin_all(void);
void xen_mm_unpin_all(void);
#ifdef CONFIG_X86_64
void __init xen_relocate_p2m(void);
#endif
void __init xen_do_remap_nonram(void);
void __init xen_add_remap_nonram(phys_addr_t maddr, phys_addr_t paddr,
				 unsigned long size);

void __init xen_chk_is_e820_usable(phys_addr_t start, phys_addr_t size,
				   const char *component);
unsigned long __ref xen_chk_extra_mem(unsigned long pfn);
void __init xen_inv_extra_mem(void);
void __init xen_remap_memory(void);
phys_addr_t __init xen_find_free_area(phys_addr_t size);
char * __init xen_memory_setup(void);
void __init xen_arch_setup(void);
void xen_banner(void);
void xen_enable_sysenter(void);
void xen_enable_syscall(void);
void xen_vcpu_restore(void);

void xen_hvm_init_shared_info(void);
void xen_unplug_emulated_devices(void);

void __init xen_build_dynamic_phys_to_machine(void);
void __init xen_vmalloc_p2m_tree(void);

void xen_init_irq_ops(void);
void xen_setup_timer(int cpu);
void xen_setup_runstate_info(int cpu);
void xen_teardown_timer(int cpu);
void xen_setup_cpu_clockevents(void);
void xen_save_time_memory_area(void);
void xen_restore_time_memory_area(void);
void xen_init_time_ops(void);
void xen_hvm_init_time_ops(void);

bool xen_vcpu_stolen(int vcpu);

void xen_vcpu_setup(int cpu);
void xen_vcpu_info_reset(int cpu);
void xen_setup_vcpu_info_placement(void);

#ifdef CONFIG_SMP
void xen_smp_init(void);
void __init xen_hvm_smp_init(void);

extern cpumask_var_t xen_cpu_initialized_map;
#else
static inline void xen_smp_init(void) {}
static inline void xen_hvm_smp_init(void) {}
#endif

#ifdef CONFIG_PARAVIRT_SPINLOCKS
void __init xen_init_spinlocks(void);
void xen_init_lock_cpu(int cpu);
void xen_uninit_lock_cpu(int cpu);
#else
static inline void xen_init_spinlocks(void)
{
}
static inline void xen_init_lock_cpu(int cpu)
{
}
static inline void xen_uninit_lock_cpu(int cpu)
{
}
#endif

struct dom0_vga_console_info;

#ifdef CONFIG_XEN_DOM0
void __init xen_init_vga(const struct dom0_vga_console_info *, size_t size,
			 struct screen_info *);
#else
static inline void __init xen_init_vga(const struct dom0_vga_console_info *info,
				       size_t size, struct screen_info *si)
{
}
#endif

void xen_add_preferred_consoles(void);

void __init xen_init_apic(void);

#ifdef CONFIG_XEN_EFI
extern void xen_efi_init(struct boot_params *boot_params);
#else
static inline void __init xen_efi_init(struct boot_params *boot_params)
{
}
#endif

__visible void xen_irq_enable_direct(void);
__visible void xen_irq_disable_direct(void);
__visible unsigned long xen_save_fl_direct(void);

__visible unsigned long xen_read_cr2(void);
__visible unsigned long xen_read_cr2_direct(void);

/* These are not functions, and cannot be called normally */
__visible void xen_iret(void);

extern int xen_panic_handler_init(void);

int xen_cpuhp_setup(int (*cpu_up_prepare_cb)(unsigned int),
		    int (*cpu_dead_cb)(unsigned int));

void xen_pin_vcpu(int cpu);

void xen_emergency_restart(void);
void xen_force_evtchn_callback(void);

#ifdef CONFIG_XEN_PV
void xen_pv_pre_suspend(void);
void xen_pv_post_suspend(int suspend_cancelled);
void xen_start_kernel(struct start_info *si);
#else
static inline void xen_pv_pre_suspend(void) {}
static inline void xen_pv_post_suspend(int suspend_cancelled) {}
#endif

#ifdef CONFIG_XEN_PVHVM
void xen_hvm_post_suspend(int suspend_cancelled);
#else
static inline void xen_hvm_post_suspend(int suspend_cancelled) {}
#endif

/*
 * The maximum amount of extra memory compared to the base size.  The
 * main scaling factor is the size of struct page.  At extreme ratios
 * of base:extra, all the base memory can be filled with page
 * structures for the extra memory, leaving no space for anything
 * else.
 *
 * 10x seems like a reasonable balance between scaling flexibility and
 * leaving a practically usable system.
 */
#define EXTRA_MEM_RATIO		(10)

void xen_add_extra_mem(unsigned long start_pfn, unsigned long n_pfns);

struct dentry * __init xen_init_debugfs(void);

enum pt_level {
	PT_PGD,
	PT_P4D,
	PT_PUD,
	PT_PMD,
	PT_PTE
};

bool __set_phys_to_machine(unsigned long pfn, unsigned long mfn);
void set_pte_mfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags);
unsigned long xen_read_cr2_direct(void);
void xen_init_mmu_ops(void);
void xen_hvm_init_mmu_ops(void);

/* Multicalls */
struct multicall_space
{
	struct multicall_entry *mc;
	void *args;
};

/* Allocate room for a multicall and its args */
struct multicall_space __xen_mc_entry(size_t args);

DECLARE_PER_CPU(unsigned long, xen_mc_irq_flags);

/* Call to start a batch of multiple __xen_mc_entry()s.  Must be
   paired with xen_mc_issue() */
static inline void xen_mc_batch(void)
{
	unsigned long flags;

	/* need to disable interrupts until this entry is complete */
	local_irq_save(flags);
	trace_xen_mc_batch(xen_get_lazy_mode());
	__this_cpu_write(xen_mc_irq_flags, flags);
}

static inline struct multicall_space xen_mc_entry(size_t args)
{
	xen_mc_batch();
	return __xen_mc_entry(args);
}

/* Flush all pending multicalls */
void xen_mc_flush(void);

/* Issue a multicall if we're not in a lazy mode */
static inline void xen_mc_issue(unsigned mode)
{
	trace_xen_mc_issue(mode);

	if ((xen_get_lazy_mode() & mode) == 0)
		xen_mc_flush();

	/* restore flags saved in xen_mc_batch */
	local_irq_restore(this_cpu_read(xen_mc_irq_flags));
}

/* Set up a callback to be called when the current batch is flushed */
void xen_mc_callback(void (*fn)(void *), void *data);

/*
 * Try to extend the arguments of the previous multicall command.  The
 * previous command's op must match.  If it does, then it attempts to
 * extend the argument space allocated to the multicall entry by
 * arg_size bytes.
 *
 * The returned multicall_space will return with mc pointing to the
 * command on success, or NULL on failure, and args pointing to the
 * newly allocated space.
 */
struct multicall_space xen_mc_extend_args(unsigned long op, size_t arg_size);

extern bool is_xen_pmu;

irqreturn_t xen_pmu_irq_handler(int irq, void *dev_id);
#ifdef CONFIG_XEN_HAVE_VPMU
void xen_pmu_init(int cpu);
void xen_pmu_finish(int cpu);
#else
static inline void xen_pmu_init(int cpu) {}
static inline void xen_pmu_finish(int cpu) {}
#endif
bool pmu_msr_read(unsigned int msr, uint64_t *val, int *err);
bool pmu_msr_write(unsigned int msr, uint32_t low, uint32_t high, int *err);
int pmu_apic_update(uint32_t reg);
unsigned long long xen_read_pmc(int counter);

#ifdef CONFIG_SMP

void asm_cpu_bringup_and_idle(void);
asmlinkage void cpu_bringup_and_idle(void);

extern void xen_send_IPI_mask(const struct cpumask *mask,
			      int vector);
extern void xen_send_IPI_mask_allbutself(const struct cpumask *mask,
				int vector);
extern void xen_send_IPI_allbutself(int vector);
extern void xen_send_IPI_all(int vector);
extern void xen_send_IPI_self(int vector);

extern int xen_smp_intr_init(unsigned int cpu);
extern void xen_smp_intr_free(unsigned int cpu);
int xen_smp_intr_init_pv(unsigned int cpu);
void xen_smp_intr_free_pv(unsigned int cpu);

void xen_smp_count_cpus(void);
void xen_smp_cpus_done(unsigned int max_cpus);

void xen_smp_send_reschedule(int cpu);
void xen_smp_send_call_function_ipi(const struct cpumask *mask);
void xen_smp_send_call_function_single_ipi(int cpu);

void __noreturn xen_cpu_bringup_again(unsigned long stack);

struct xen_common_irq {
	int irq;
	char *name;
};
#else /* CONFIG_SMP */

static inline int xen_smp_intr_init(unsigned int cpu)
{
	return 0;
}
static inline void xen_smp_intr_free(unsigned int cpu) {}

static inline int xen_smp_intr_init_pv(unsigned int cpu)
{
	return 0;
}
static inline void xen_smp_intr_free_pv(unsigned int cpu) {}
static inline void xen_smp_count_cpus(void) { }
#endif /* CONFIG_SMP */

#ifdef CONFIG_XEN_PV
void xen_hypercall_pv(void);
#endif
void xen_hypercall_hvm(void);
void xen_hypercall_amd(void);
void xen_hypercall_intel(void);
void xen_hypercall_setfunc(void);
void *__xen_hypercall_setfunc(void);

#endif /* XEN_OPS_H */
