/* SPDX-License-Identifier: GPL-2.0 */
#ifndef XEN_OPS_H
#define XEN_OPS_H

#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/irqreturn.h>
#include <xen/xen-ops.h>

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
DECLARE_PER_CPU(unsigned long, xen_current_cr3);

extern struct start_info *xen_start_info;
extern struct shared_info xen_dummy_shared_info;
extern struct shared_info *HYPERVISOR_shared_info;

extern bool xen_fifo_events;

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

bool __init xen_is_e820_reserved(phys_addr_t start, phys_addr_t size);
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

#endif /* XEN_OPS_H */
