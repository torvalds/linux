//===-- xray_trampoline_x86.s -----------------------------------*- ASM -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This implements the X86-specific assembler for the trampolines.
//
//===----------------------------------------------------------------------===//

#include "../builtins/assembly.h"
#include "../sanitizer_common/sanitizer_asm.h"



.macro SAVE_REGISTERS
	pushfq
	subq $240, %rsp
	CFI_DEF_CFA_OFFSET(248)
	movq %rbp, 232(%rsp)
	movupd	%xmm0, 216(%rsp)
	movupd	%xmm1, 200(%rsp)
	movupd	%xmm2, 184(%rsp)
	movupd	%xmm3, 168(%rsp)
	movupd	%xmm4, 152(%rsp)
	movupd	%xmm5, 136(%rsp)
	movupd	%xmm6, 120(%rsp)
	movupd	%xmm7, 104(%rsp)
	movq	%rdi, 96(%rsp)
	movq	%rax, 88(%rsp)
	movq	%rdx, 80(%rsp)
	movq	%rsi, 72(%rsp)
	movq	%rcx, 64(%rsp)
	movq	%r8, 56(%rsp)
	movq	%r9, 48(%rsp)
	movq  %r10, 40(%rsp)
	movq  %r11, 32(%rsp)
	movq  %r12, 24(%rsp)
	movq  %r13, 16(%rsp)
	movq  %r14, 8(%rsp)
	movq  %r15, 0(%rsp)
.endm

.macro RESTORE_REGISTERS
	movq  232(%rsp), %rbp
	movupd	216(%rsp), %xmm0
	movupd	200(%rsp), %xmm1
	movupd	184(%rsp), %xmm2
	movupd	168(%rsp), %xmm3
	movupd	152(%rsp), %xmm4
	movupd	136(%rsp), %xmm5
	movupd	120(%rsp) , %xmm6
	movupd	104(%rsp) , %xmm7
	movq	96(%rsp), %rdi
	movq	88(%rsp), %rax
	movq	80(%rsp), %rdx
	movq	72(%rsp), %rsi
	movq	64(%rsp), %rcx
	movq	56(%rsp), %r8
	movq	48(%rsp), %r9
	movq  40(%rsp), %r10
	movq  32(%rsp), %r11
	movq  24(%rsp), %r12
	movq  16(%rsp), %r13
	movq  8(%rsp), %r14
	movq  0(%rsp), %r15
	addq	$240, %rsp
	popfq
	CFI_DEF_CFA_OFFSET(8)
.endm

.macro ALIGNED_CALL_RAX
	// Call the logging handler, after aligning the stack to a 16-byte boundary.
	// The approach we're taking here uses additional stack space to stash the
	// stack pointer twice before aligning the pointer to 16-bytes. If the stack
	// was 8-byte aligned, it will become 16-byte aligned -- when restoring the
	// pointer, we can always look -8 bytes from the current position to get
	// either of the values we've stashed in the first place.
	pushq %rsp
	pushq (%rsp)
	andq $-0x10, %rsp
  callq *%rax
	movq 8(%rsp), %rsp
.endm

	.text
#if !defined(__APPLE__)
	.section .text
	.file "xray_trampoline_x86.S"
#else
	.section __TEXT,__text
#endif

//===----------------------------------------------------------------------===//

	.globl ASM_SYMBOL(__xray_FunctionEntry)
	.align 16, 0x90
	ASM_TYPE_FUNCTION(__xray_FunctionEntry)
# LLVM-MCA-BEGIN __xray_FunctionEntry
ASM_SYMBOL(__xray_FunctionEntry):
	CFI_STARTPROC
	SAVE_REGISTERS

	// This load has to be atomic, it's concurrent with __xray_patch().
	// On x86/amd64, a simple (type-aligned) MOV instruction is enough.
	movq	ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)(%rip), %rax
	testq	%rax, %rax
	je	.Ltmp0

	// The patched function prologue puts its xray_instr_map index into %r10d.
	movl	%r10d, %edi
	xor	%esi,%esi
	ALIGNED_CALL_RAX

.Ltmp0:
	RESTORE_REGISTERS
	retq
# LLVM-MCA-END
	ASM_SIZE(__xray_FunctionEntry)
	CFI_ENDPROC

//===----------------------------------------------------------------------===//

	.globl ASM_SYMBOL(__xray_FunctionExit)
	.align 16, 0x90
	ASM_TYPE_FUNCTION(__xray_FunctionExit)
# LLVM-MCA-BEGIN __xray_FunctionExit
ASM_SYMBOL(__xray_FunctionExit):
	CFI_STARTPROC
	// Save the important registers first. Since we're assuming that this
	// function is only jumped into, we only preserve the registers for
	// returning.
	subq	$56, %rsp
	CFI_DEF_CFA_OFFSET(64)
	movq  %rbp, 48(%rsp)
	movupd	%xmm0, 32(%rsp)
	movupd	%xmm1, 16(%rsp)
	movq	%rax, 8(%rsp)
	movq	%rdx, 0(%rsp)
	movq	ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)(%rip), %rax
	testq %rax,%rax
	je	.Ltmp2

	movl	%r10d, %edi
	movl	$1, %esi
  ALIGNED_CALL_RAX

.Ltmp2:
	// Restore the important registers.
	movq  48(%rsp), %rbp
	movupd	32(%rsp), %xmm0
	movupd	16(%rsp), %xmm1
	movq	8(%rsp), %rax
	movq	0(%rsp), %rdx
	addq	$56, %rsp
	CFI_DEF_CFA_OFFSET(8)
	retq
# LLVM-MCA-END
	ASM_SIZE(__xray_FunctionExit)
	CFI_ENDPROC

//===----------------------------------------------------------------------===//

	.globl ASM_SYMBOL(__xray_FunctionTailExit)
	.align 16, 0x90
	ASM_TYPE_FUNCTION(__xray_FunctionTailExit)
# LLVM-MCA-BEGIN __xray_FunctionTailExit
ASM_SYMBOL(__xray_FunctionTailExit):
	CFI_STARTPROC
	SAVE_REGISTERS

	movq	ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)(%rip), %rax
	testq %rax,%rax
	je	.Ltmp4

	movl	%r10d, %edi
	movl	$2, %esi

  ALIGNED_CALL_RAX

.Ltmp4:
	RESTORE_REGISTERS
	retq
# LLVM-MCA-END
	ASM_SIZE(__xray_FunctionTailExit)
	CFI_ENDPROC

//===----------------------------------------------------------------------===//

	.globl ASM_SYMBOL(__xray_ArgLoggerEntry)
	.align 16, 0x90
	ASM_TYPE_FUNCTION(__xray_ArgLoggerEntry)
# LLVM-MCA-BEGIN __xray_ArgLoggerEntry
ASM_SYMBOL(__xray_ArgLoggerEntry):
	CFI_STARTPROC
	SAVE_REGISTERS

	// Again, these function pointer loads must be atomic; MOV is fine.
	movq	ASM_SYMBOL(_ZN6__xray13XRayArgLoggerE)(%rip), %rax
	testq	%rax, %rax
	jne	.Larg1entryLog

	// If [arg1 logging handler] not set, defer to no-arg logging.
	movq	ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)(%rip), %rax
	testq	%rax, %rax
	je	.Larg1entryFail

.Larg1entryLog:

	// First argument will become the third
	movq	%rdi, %rdx

	// XRayEntryType::LOG_ARGS_ENTRY into the second
	mov	$0x3, %esi

	// 32-bit function ID becomes the first
	movl	%r10d, %edi
	ALIGNED_CALL_RAX

.Larg1entryFail:
	RESTORE_REGISTERS
	retq
# LLVM-MCA-END
	ASM_SIZE(__xray_ArgLoggerEntry)
	CFI_ENDPROC

//===----------------------------------------------------------------------===//

	.global ASM_SYMBOL(__xray_CustomEvent)
	.align 16, 0x90
	ASM_TYPE_FUNCTION(__xray_CustomEvent)
# LLVM-MCA-BEGIN __xray_CustomEvent
ASM_SYMBOL(__xray_CustomEvent):
	CFI_STARTPROC
	SAVE_REGISTERS

	// We take two arguments to this trampoline, which should be in rdi	and rsi
	// already.
	movq ASM_SYMBOL(_ZN6__xray22XRayPatchedCustomEventE)(%rip), %rax
	testq %rax,%rax
	je .LcustomEventCleanup

	ALIGNED_CALL_RAX

.LcustomEventCleanup:
	RESTORE_REGISTERS
	retq
# LLVM-MCA-END
	ASM_SIZE(__xray_CustomEvent)
	CFI_ENDPROC

//===----------------------------------------------------------------------===//

	.global ASM_SYMBOL(__xray_TypedEvent)
	.align 16, 0x90
	ASM_TYPE_FUNCTION(__xray_TypedEvent)
# LLVM-MCA-BEGIN __xray_TypedEvent
ASM_SYMBOL(__xray_TypedEvent):
	CFI_STARTPROC
	SAVE_REGISTERS

	// We pass three arguments to this trampoline, which should be in rdi, rsi
	// and rdx without our intervention.
	movq ASM_SYMBOL(_ZN6__xray21XRayPatchedTypedEventE)(%rip), %rax
	testq %rax,%rax
	je .LtypedEventCleanup

	ALIGNED_CALL_RAX

.LtypedEventCleanup:
	RESTORE_REGISTERS
	retq
# LLVM-MCA-END
	ASM_SIZE(__xray_TypedEvent)
	CFI_ENDPROC

//===----------------------------------------------------------------------===//

NO_EXEC_STACK_DIRECTIVE
