/* SPDX-License-Identifier: GPL-2.0 */
/* Xen-specific pieces of head.S, intended to be included in the right
	place in head.S */

#ifdef CONFIG_XEN

#include <linux/elfnote.h>
#include <linux/init.h>
#include <linux/instrumentation.h>

#include <asm/boot.h>
#include <asm/asm.h>
#include <asm/frame.h>
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
	UNWIND_HINT_END_OF_STACK
	ANNOTATE_NOENDBR
	cld

	leaq	__top_init_kernel_stack(%rip), %rsp

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

	.pushsection .noinstr.text, "ax"
/*
 * Xen hypercall interface to the hypervisor.
 *
 * Input:
 *     %eax: hypercall number
 *   32-bit:
 *     %ebx, %ecx, %edx, %esi, %edi: args 1..5 for the hypercall
 *   64-bit:
 *     %rdi, %rsi, %rdx, %r10, %r8: args 1..5 for the hypercall
 * Output: %[er]ax
 */
SYM_FUNC_START(xen_hypercall_hvm)
	ENDBR
	FRAME_BEGIN
	/* Save all relevant registers (caller save and arguments). */
#ifdef CONFIG_X86_32
	push %eax
	push %ebx
	push %ecx
	push %edx
	push %esi
	push %edi
#else
	push %rax
	push %rcx
	push %rdx
	push %rdi
	push %rsi
	push %r11
	push %r10
	push %r9
	push %r8
#ifdef CONFIG_FRAME_POINTER
	pushq $0	/* Dummy push for stack alignment. */
#endif
#endif
	/* Set the vendor specific function. */
	call __xen_hypercall_setfunc
	/* Set ZF = 1 if AMD, Restore saved registers. */
#ifdef CONFIG_X86_32
	lea xen_hypercall_amd, %ebx
	cmp %eax, %ebx
	pop %edi
	pop %esi
	pop %edx
	pop %ecx
	pop %ebx
	pop %eax
#else
	lea xen_hypercall_amd(%rip), %rbx
	cmp %rax, %rbx
#ifdef CONFIG_FRAME_POINTER
	pop %rax	/* Dummy pop. */
#endif
	pop %r8
	pop %r9
	pop %r10
	pop %r11
	pop %rsi
	pop %rdi
	pop %rdx
	pop %rcx
	pop %rax
#endif
	/* Use correct hypercall function. */
	jz xen_hypercall_amd
	jmp xen_hypercall_intel
SYM_FUNC_END(xen_hypercall_hvm)

SYM_FUNC_START(xen_hypercall_amd)
	vmmcall
	RET
SYM_FUNC_END(xen_hypercall_amd)

SYM_FUNC_START(xen_hypercall_intel)
	vmcall
	RET
SYM_FUNC_END(xen_hypercall_intel)
	.popsection

	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_OS,       .asciz "linux")
	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_VERSION,  .asciz "2.6")
	ELFNOTE(Xen, XEN_ELFNOTE_XEN_VERSION,    .asciz "xen-3.0")
#ifdef CONFIG_XEN_PV
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR __START_KERNEL_map)
	/* Map the p2m table to a 512GB-aligned user address. */
	ELFNOTE(Xen, XEN_ELFNOTE_INIT_P2M,       .quad (PUD_SIZE * PTRS_PER_PUD))
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          _ASM_PTR startup_xen)
	ELFNOTE(Xen, XEN_ELFNOTE_FEATURES,       .ascii "!writable_page_tables")
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "yes")
	ELFNOTE(Xen, XEN_ELFNOTE_L1_MFN_VALID,
		.quad _PAGE_PRESENT; .quad _PAGE_PRESENT)
	ELFNOTE(Xen, XEN_ELFNOTE_MOD_START_PFN,  .long 1)
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   _ASM_PTR 0)
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
	ELFNOTE(Xen, XEN_ELFNOTE_SUPPORTED_FEATURES,
		.long FEATURES_PV | FEATURES_PVH | FEATURES_DOM0)
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")
	ELFNOTE(Xen, XEN_ELFNOTE_SUSPEND_CANCEL, .long 1)

#endif /*CONFIG_XEN */
