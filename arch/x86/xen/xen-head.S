/* SPDX-License-Identifier: GPL-2.0 */
/* Xen-specific pieces of head.S, intended to be included in the right
	place in head.S */

#ifdef CONFIG_XEN

#include <linux/elfnote.h>
#include <linux/init.h>

#include <asm/boot.h>
#include <asm/asm.h>
#include <asm/msr.h>
#include <asm/page_types.h>
#include <asm/percpu.h>
#include <asm/unwind_hints.h>

#include <xen/interface/elfnote.h>
#include <xen/interface/features.h>
#include <xen/interface/xen.h>
#include <xen/interface/xen-mca.h>
#include <asm/xen/interface.h>

#ifdef CONFIG_XEN_PV
	__INIT
SYM_CODE_START(startup_xen)
	UNWIND_HINT_EMPTY
	cld

	/* Clear .bss */
	xor %eax,%eax
	mov $__bss_start, %_ASM_DI
	mov $__bss_stop, %_ASM_CX
	sub %_ASM_DI, %_ASM_CX
	shr $__ASM_SEL(2, 3), %_ASM_CX
	rep __ASM_SIZE(stos)

	mov %_ASM_SI, xen_start_info
#ifdef CONFIG_X86_64
	mov initial_stack(%rip), %rsp
#else
	mov initial_stack, %esp
#endif

#ifdef CONFIG_X86_64
	/* Set up %gs.
	 *
	 * The base of %gs always points to fixed_percpu_data.  If the
	 * stack protector canary is enabled, it is located at %gs:40.
	 * Note that, on SMP, the boot cpu uses init data section until
	 * the per cpu areas are set up.
	 */
	movl	$MSR_GS_BASE,%ecx
	movq	$INIT_PER_CPU_VAR(fixed_percpu_data),%rax
	cdq
	wrmsr
#endif

	call xen_start_kernel
SYM_CODE_END(startup_xen)
	__FINIT

#ifdef CONFIG_XEN_PV_SMP
.pushsection .text
SYM_CODE_START(asm_cpu_bringup_and_idle)
	UNWIND_HINT_EMPTY

	call cpu_bringup_and_idle
SYM_CODE_END(asm_cpu_bringup_and_idle)
.popsection
#endif
#endif

.pushsection .text
	.balign PAGE_SIZE
SYM_CODE_START(hypercall_page)
	.rept (PAGE_SIZE / 32)
		UNWIND_HINT_EMPTY
		.skip 32
	.endr

#define HYPERCALL(n) \
	.equ xen_hypercall_##n, hypercall_page + __HYPERVISOR_##n * 32; \
	.type xen_hypercall_##n, @function; .size xen_hypercall_##n, 32
#include <asm/xen-hypercalls.h>
#undef HYPERCALL
SYM_CODE_END(hypercall_page)
.popsection

	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_OS,       .asciz "linux")
	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_VERSION,  .asciz "2.6")
	ELFNOTE(Xen, XEN_ELFNOTE_XEN_VERSION,    .asciz "xen-3.0")
#ifdef CONFIG_X86_32
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR __PAGE_OFFSET)
#else
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR __START_KERNEL_map)
	/* Map the p2m table to a 512GB-aligned user address. */
	ELFNOTE(Xen, XEN_ELFNOTE_INIT_P2M,       .quad (PUD_SIZE * PTRS_PER_PUD))
#endif
#ifdef CONFIG_XEN_PV
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          _ASM_PTR startup_xen)
#endif
	ELFNOTE(Xen, XEN_ELFNOTE_HYPERCALL_PAGE, _ASM_PTR hypercall_page)
	ELFNOTE(Xen, XEN_ELFNOTE_FEATURES,
		.ascii "!writable_page_tables|pae_pgdir_above_4gb")
	ELFNOTE(Xen, XEN_ELFNOTE_SUPPORTED_FEATURES,
		.long (1 << XENFEAT_writable_page_tables) |       \
		      (1 << XENFEAT_dom0) |                       \
		      (1 << XENFEAT_linux_rsdp_unrestricted))
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "yes")
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")
	ELFNOTE(Xen, XEN_ELFNOTE_L1_MFN_VALID,
		.quad _PAGE_PRESENT; .quad _PAGE_PRESENT)
	ELFNOTE(Xen, XEN_ELFNOTE_SUSPEND_CANCEL, .long 1)
	ELFNOTE(Xen, XEN_ELFNOTE_MOD_START_PFN,  .long 1)
	ELFNOTE(Xen, XEN_ELFNOTE_HV_START_LOW,   _ASM_PTR __HYPERVISOR_VIRT_START)
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   _ASM_PTR 0)

#endif /*CONFIG_XEN */
