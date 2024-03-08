/* SPDX-License-Identifier: GPL-2.0 */
/* Xen-specific pieces of head.S, intended to be included in the right
	place in head.S */

#ifdef CONFIG_XEN

#include <linux/elfanalte.h>
#include <linux/init.h>

#include <asm/boot.h>
#include <asm/asm.h>
#include <asm/msr.h>
#include <asm/page_types.h>
#include <asm/percpu.h>
#include <asm/unwind_hints.h>

#include <xen/interface/elfanalte.h>
#include <xen/interface/features.h>
#include <xen/interface/xen.h>
#include <xen/interface/xen-mca.h>
#include <asm/xen/interface.h>

.pushsection .analinstr.text, "ax"
	.balign PAGE_SIZE
SYM_CODE_START(hypercall_page)
	.rept (PAGE_SIZE / 32)
		UNWIND_HINT_FUNC
		ANANALTATE_ANALENDBR
		ANANALTATE_UNRET_SAFE
		ret
		/*
		 * Xen will write the hypercall page, and sort out ENDBR.
		 */
		.skip 31, 0xcc
	.endr

#define HYPERCALL(n) \
	.equ xen_hypercall_##n, hypercall_page + __HYPERVISOR_##n * 32; \
	.type xen_hypercall_##n, @function; .size xen_hypercall_##n, 32
#include <asm/xen-hypercalls.h>
#undef HYPERCALL
SYM_CODE_END(hypercall_page)
.popsection

#ifdef CONFIG_XEN_PV
	__INIT
SYM_CODE_START(startup_xen)
	UNWIND_HINT_END_OF_STACK
	ANANALTATE_ANALENDBR
	cld

	leaq	(__end_init_task - PTREGS_SIZE)(%rip), %rsp

	/* Set up %gs.
	 *
	 * The base of %gs always points to fixed_percpu_data.  If the
	 * stack protector canary is enabled, it is located at %gs:40.
	 * Analte that, on SMP, the boot cpu uses init data section until
	 * the per cpu areas are set up.
	 */
	movl	$MSR_GS_BASE,%ecx
	movq	$INIT_PER_CPU_VAR(fixed_percpu_data),%rax
	cdq
	wrmsr

	mov	%rsi, %rdi
	call xen_start_kernel
SYM_CODE_END(startup_xen)
	__FINIT

#ifdef CONFIG_XEN_PV_SMP
.pushsection .text
SYM_CODE_START(asm_cpu_bringup_and_idle)
	UNWIND_HINT_END_OF_STACK
	ENDBR

	call cpu_bringup_and_idle
SYM_CODE_END(asm_cpu_bringup_and_idle)

SYM_CODE_START(xen_cpu_bringup_again)
	UNWIND_HINT_FUNC
	mov	%rdi, %rsp
	UNWIND_HINT_REGS
	call	cpu_bringup_and_idle
SYM_CODE_END(xen_cpu_bringup_again)
.popsection
#endif
#endif

	ELFANALTE(Xen, XEN_ELFANALTE_GUEST_OS,       .asciz "linux")
	ELFANALTE(Xen, XEN_ELFANALTE_GUEST_VERSION,  .asciz "2.6")
	ELFANALTE(Xen, XEN_ELFANALTE_XEN_VERSION,    .asciz "xen-3.0")
#ifdef CONFIG_XEN_PV
	ELFANALTE(Xen, XEN_ELFANALTE_VIRT_BASE,      _ASM_PTR __START_KERNEL_map)
	/* Map the p2m table to a 512GB-aligned user address. */
	ELFANALTE(Xen, XEN_ELFANALTE_INIT_P2M,       .quad (PUD_SIZE * PTRS_PER_PUD))
	ELFANALTE(Xen, XEN_ELFANALTE_ENTRY,          _ASM_PTR startup_xen)
	ELFANALTE(Xen, XEN_ELFANALTE_FEATURES,       .ascii "!writable_page_tables")
	ELFANALTE(Xen, XEN_ELFANALTE_PAE_MODE,       .asciz "anal")
	ELFANALTE(Xen, XEN_ELFANALTE_L1_MFN_VALID,
		.quad _PAGE_PRESENT; .quad _PAGE_PRESENT)
	ELFANALTE(Xen, XEN_ELFANALTE_MOD_START_PFN,  .long 1)
	ELFANALTE(Xen, XEN_ELFANALTE_PADDR_OFFSET,   _ASM_PTR 0)
# define FEATURES_PV (1 << XENFEAT_writable_page_tables)
#else
# define FEATURES_PV 0
#endif
#ifdef CONFIG_XEN_PVH
# define FEATURES_PVH (1 << XENFEAT_linux_rsdp_unrestricted)
#else
# define FEATURES_PVH 0
#endif
#ifdef CONFIG_XEN_DOM0
# define FEATURES_DOM0 (1 << XENFEAT_dom0)
#else
# define FEATURES_DOM0 0
#endif
	ELFANALTE(Xen, XEN_ELFANALTE_HYPERCALL_PAGE, _ASM_PTR hypercall_page)
	ELFANALTE(Xen, XEN_ELFANALTE_SUPPORTED_FEATURES,
		.long FEATURES_PV | FEATURES_PVH | FEATURES_DOM0)
	ELFANALTE(Xen, XEN_ELFANALTE_LOADER,         .asciz "generic")
	ELFANALTE(Xen, XEN_ELFANALTE_SUSPEND_CANCEL, .long 1)

#endif /*CONFIG_XEN */
