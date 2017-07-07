#ifndef XEN_OPS_H
#define XEN_OPS_H

#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/irqreturn.h>
#include <xen/xen-ops.h>

/* These are code, but not functions.  Defined in entry.S */
extern const char xen_hypervisor_callback[];
extern const char xen_failsafe_callback[];

void xen_sysenter_target(void);
#ifdef CONFIG_X86_64
void xen_syscall_target(void);
void xen_syscall32_target(void);
#endif

extern void *xen_initial_gdt;

struct trap_info;
void xen_copy_trap_info(struct trap_info *traps);

DECLARE_PER_CPU(struct vcpu_info, xen_vcpu_info);
DECLARE_PER_CPU(unsigned long, xen_cr3);
DECLARE_PER_CPU(unsigned long, xen_current_cr3);

extern struct start_info *xen_start_info;
extern struct shared_info xen_dummy_shared_info;
extern struct shared_info *HYPERVISOR_shared_info;

void xen_setup_mfn_list_list(void);
void xen_setup_shared_info(void);
void xen_build_mfn_list_list(void);
void xen_setup_machphys_mapping(void);
void xen_setup_kernel_pagetable(pgd_t *pgd, unsigned long max_pfn);
void xen_reserve_top(void);
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
char * xen_auto_xlated_memory_setup(void);
void __init xen_arch_setup(void);
void xen_enable_sysenter(void);
void xen_enable_syscall(void);
void xen_vcpu_restore(void);

void xen_callback_vector(void);
void xen_hvm_init_shared_info(void);
void xen_unplug_emulated_devices(void);

void __init xen_build_dynamic_phys_to_machine(void);
void __init xen_vmalloc_p2m_tree(void);

void xen_init_irq_ops(void);
void xen_setup_timer(int cpu);
void xen_setup_runstate_info(int cpu);
void xen_teardown_timer(int cpu);
u64 xen_clocksource_read(void);
void xen_setup_cpu_clockevents(void);
void __init xen_init_time_ops(void);
void __init xen_hvm_init_time_ops(void);

irqreturn_t xen_debug_interrupt(int irq, void *dev_id);

bool xen_vcpu_stolen(int vcpu);

extern int xen_have_vcpu_info_placement;

int xen_vcpu_setup(int cpu);
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
void __init xen_init_vga(const struct dom0_vga_console_info *, size_t size);
#else
static inline void __init xen_init_vga(const struct dom0_vga_console_info *info,
				       size_t size)
{
}
#endif

void __init xen_init_apic(void);

#ifdef CONFIG_XEN_EFI
extern void xen_efi_init(void);
#else
static inline void __init xen_efi_init(void)
{
}
#endif

/* Declare an asm function, along with symbols needed to make it
   inlineable */
#define DECL_ASM(ret, name, ...)		\
	__visible ret name(__VA_ARGS__);	\
	extern char name##_end[] __visible;	\
	extern char name##_reloc[] __visible

DECL_ASM(void, xen_irq_enable_direct, void);
DECL_ASM(void, xen_irq_disable_direct, void);
DECL_ASM(unsigned long, xen_save_fl_direct, void);
DECL_ASM(void, xen_restore_fl_direct, unsigned long);

/* These are not functions, and cannot be called normally */
__visible void xen_iret(void);
__visible void xen_sysret32(void);
__visible void xen_sysret64(void);
__visible void xen_adjust_exception_frame(void);

extern int xen_panic_handler_init(void);

int xen_cpuhp_setup(int (*cpu_up_prepare_cb)(unsigned int),
		    int (*cpu_dead_cb)(unsigned int));

void xen_pin_vcpu(int cpu);

void xen_emergency_restart(void);
#ifdef CONFIG_XEN_PV
void xen_pv_pre_suspend(void);
void xen_pv_post_suspend(int suspend_cancelled);
#else
static inline void xen_pv_pre_suspend(void) {}
static inline void xen_pv_post_suspend(int suspend_cancelled) {}
#endif

#ifdef CONFIG_XEN_PVHVM
void xen_hvm_post_suspend(int suspend_cancelled);
#else
static inline void xen_hvm_post_suspend(int suspend_cancelled) {}
#endif

#endif /* XEN_OPS_H */
