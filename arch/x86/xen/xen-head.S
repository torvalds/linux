/* Xen-specific pieces of head.S, intended to be included in the right
	place in head.S */

#ifdef CONFIG_XEN

#include <linux/elfnote.h>
#include <linux/init.h>

#include <asm/boot.h>
#include <asm/asm.h>
#include <asm/page_types.h>

#include <xen/interface/elfnote.h>
#include <xen/interface/features.h>
#include <asm/xen/interface.h>

#ifdef CONFIG_XEN_PVH
#define PVH_FEATURES_STR  "|writable_descriptor_tables|auto_translated_physmap|supervisor_mode_kernel"
/* Note the lack of 'hvm_callback_vector'. Older hypervisor will
 * balk at this being part of XEN_ELFNOTE_FEATURES, so we put it in
 * XEN_ELFNOTE_SUPPORTED_FEATURES which older hypervisors will ignore.
 */
#define PVH_FEATURES ((1 << XENFEAT_writable_page_tables) | \
		      (1 << XENFEAT_auto_translated_physmap) | \
		      (1 << XENFEAT_supervisor_mode_kernel) | \
		      (1 << XENFEAT_hvm_callback_vector))
/* The XENFEAT_writable_page_tables is not stricly neccessary as we set that
 * up regardless whether this CONFIG option is enabled or not, but it
 * clarifies what the right flags need to be.
 */
#else
#define PVH_FEATURES_STR  ""
#define PVH_FEATURES (0)
#endif

	__INIT
ENTRY(startup_xen)
	cld
#ifdef CONFIG_X86_32
	mov %esi,xen_start_info
	mov $init_thread_union+THREAD_SIZE,%esp
#else
	mov %rsi,xen_start_info
	mov $init_thread_union+THREAD_SIZE,%rsp
#endif
	jmp xen_start_kernel

	__FINIT

#ifdef CONFIG_XEN_PVH
/*
 * xen_pvh_early_cpu_init() - early PVH VCPU initialization
 * @cpu:   this cpu number (%rdi)
 * @entry: true if this is a secondary vcpu coming up on this entry
 *         point, false if this is the boot CPU being initialized for
 *         the first time (%rsi)
 *
 * Note: This is called as a function on the boot CPU, and is the entry point
 *       on the secondary CPU.
 */
ENTRY(xen_pvh_early_cpu_init)
	mov     %rsi, %r11

	/* Gather features to see if NX implemented. */
	mov     $0x80000001, %eax
	cpuid
	mov     %edx, %esi

	mov     $MSR_EFER, %ecx
	rdmsr
	bts     $_EFER_SCE, %eax

	bt      $20, %esi
	jnc     1f      	/* No NX, skip setting it */
	bts     $_EFER_NX, %eax
1:	wrmsr
#ifdef CONFIG_SMP
	cmp     $0, %r11b
	jne     cpu_bringup_and_idle
#endif
	ret

#endif /* CONFIG_XEN_PVH */

.pushsection .text
	.balign PAGE_SIZE
ENTRY(hypercall_page)
#define NEXT_HYPERCALL(x) \
	ENTRY(xen_hypercall_##x) \
	.skip 32

NEXT_HYPERCALL(set_trap_table)
NEXT_HYPERCALL(mmu_update)
NEXT_HYPERCALL(set_gdt)
NEXT_HYPERCALL(stack_switch)
NEXT_HYPERCALL(set_callbacks)
NEXT_HYPERCALL(fpu_taskswitch)
NEXT_HYPERCALL(sched_op_compat)
NEXT_HYPERCALL(platform_op)
NEXT_HYPERCALL(set_debugreg)
NEXT_HYPERCALL(get_debugreg)
NEXT_HYPERCALL(update_descriptor)
NEXT_HYPERCALL(ni)
NEXT_HYPERCALL(memory_op)
NEXT_HYPERCALL(multicall)
NEXT_HYPERCALL(update_va_mapping)
NEXT_HYPERCALL(set_timer_op)
NEXT_HYPERCALL(event_channel_op_compat)
NEXT_HYPERCALL(xen_version)
NEXT_HYPERCALL(console_io)
NEXT_HYPERCALL(physdev_op_compat)
NEXT_HYPERCALL(grant_table_op)
NEXT_HYPERCALL(vm_assist)
NEXT_HYPERCALL(update_va_mapping_otherdomain)
NEXT_HYPERCALL(iret)
NEXT_HYPERCALL(vcpu_op)
NEXT_HYPERCALL(set_segment_base)
NEXT_HYPERCALL(mmuext_op)
NEXT_HYPERCALL(xsm_op)
NEXT_HYPERCALL(nmi_op)
NEXT_HYPERCALL(sched_op)
NEXT_HYPERCALL(callback_op)
NEXT_HYPERCALL(xenoprof_op)
NEXT_HYPERCALL(event_channel_op)
NEXT_HYPERCALL(physdev_op)
NEXT_HYPERCALL(hvm_op)
NEXT_HYPERCALL(sysctl)
NEXT_HYPERCALL(domctl)
NEXT_HYPERCALL(kexec_op)
NEXT_HYPERCALL(tmem_op) /* 38 */
ENTRY(xen_hypercall_rsvr)
	.skip 320
NEXT_HYPERCALL(mca) /* 48 */
NEXT_HYPERCALL(arch_1)
NEXT_HYPERCALL(arch_2)
NEXT_HYPERCALL(arch_3)
NEXT_HYPERCALL(arch_4)
NEXT_HYPERCALL(arch_5)
NEXT_HYPERCALL(arch_6)
	.balign PAGE_SIZE
.popsection

	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_OS,       .asciz "linux")
	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_VERSION,  .asciz "2.6")
	ELFNOTE(Xen, XEN_ELFNOTE_XEN_VERSION,    .asciz "xen-3.0")
#ifdef CONFIG_X86_32
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR __PAGE_OFFSET)
#else
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR __START_KERNEL_map)
#endif
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          _ASM_PTR startup_xen)
	ELFNOTE(Xen, XEN_ELFNOTE_HYPERCALL_PAGE, _ASM_PTR hypercall_page)
	ELFNOTE(Xen, XEN_ELFNOTE_FEATURES,       .ascii "!writable_page_tables|pae_pgdir_above_4gb"; .asciz PVH_FEATURES_STR)
	ELFNOTE(Xen, XEN_ELFNOTE_SUPPORTED_FEATURES, .long (PVH_FEATURES) |
						(1 << XENFEAT_writable_page_tables) |
						(1 << XENFEAT_dom0))
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "yes")
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")
	ELFNOTE(Xen, XEN_ELFNOTE_L1_MFN_VALID,
		.quad _PAGE_PRESENT; .quad _PAGE_PRESENT)
	ELFNOTE(Xen, XEN_ELFNOTE_SUSPEND_CANCEL, .long 1)
	ELFNOTE(Xen, XEN_ELFNOTE_MOD_START_PFN,  .long 1)
	ELFNOTE(Xen, XEN_ELFNOTE_HV_START_LOW,   _ASM_PTR __HYPERVISOR_VIRT_START)
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   _ASM_PTR 0)

#endif /*CONFIG_XEN */
