/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The actual FRED entry points.
 */

#include <linux/export.h>

#include <asm/asm.h>
#include <asm/fred.h>
#include <asm/segment.h>

#include "calling.h"

	.code64
	.section .noinstr.text, "ax"

.macro FRED_ENTER
	UNWIND_HINT_END_OF_STACK
	ENDBR
	PUSH_AND_CLEAR_REGS
	movq	%rsp, %rdi	/* %rdi -> pt_regs */
.endm

.macro FRED_EXIT
	UNWIND_HINT_REGS
	POP_REGS
.endm

/*
 * The new RIP value that FRED event delivery establishes is
 * IA32_FRED_CONFIG & ~FFFH for events that occur in ring 3.
 * Thus the FRED ring 3 entry point must be 4K page aligned.
 */
	.align 4096

SYM_CODE_START_NOALIGN(asm_fred_entrypoint_user)
	FRED_ENTER
	call	fred_entry_from_user
SYM_INNER_LABEL(asm_fred_exit_user, SYM_L_GLOBAL)
	FRED_EXIT
1:	ERETU

	_ASM_EXTABLE_TYPE(1b, asm_fred_entrypoint_user, EX_TYPE_ERETU)
SYM_CODE_END(asm_fred_entrypoint_user)

/*
 * The new RIP value that FRED event delivery establishes is
 * (IA32_FRED_CONFIG & ~FFFH) + 256 for events that occur in
 * ring 0, i.e., asm_fred_entrypoint_user + 256.
 */
	.org asm_fred_entrypoint_user + 256, 0xcc
SYM_CODE_START_NOALIGN(asm_fred_entrypoint_kernel)
	FRED_ENTER
	call	fred_entry_from_kernel
	FRED_EXIT
	ERETS
SYM_CODE_END(asm_fred_entrypoint_kernel)

#if IS_ENABLED(CONFIG_KVM_INTEL)
SYM_FUNC_START(asm_fred_entry_from_kvm)
	ANNOTATE_NOENDBR
	push %rbp
	mov %rsp, %rbp

	UNWIND_HINT_SAVE

	/*
	 * Both IRQ and NMI from VMX can be handled on current task stack
	 * because there is no need to protect from reentrancy and the call
	 * stack leading to this helper is effectively constant and shallow
	 * (relatively speaking). Do the same when FRED is active, i.e., no
	 * need to check current stack level for a stack switch.
	 *
	 * Emulate the FRED-defined redzone and stack alignment.
	 */
	sub $(FRED_CONFIG_REDZONE_AMOUNT << 6), %rsp
	and $FRED_STACK_FRAME_RSP_MASK, %rsp

	/*
	 * Start to push a FRED stack frame, which is always 64 bytes:
	 *
	 * +--------+-----------------+
	 * | Bytes  | Usage           |
	 * +--------+-----------------+
	 * | 63:56  | Reserved        |
	 * | 55:48  | Event Data      |
	 * | 47:40  | SS + Event Info |
	 * | 39:32  | RSP             |
	 * | 31:24  | RFLAGS          |
	 * | 23:16  | CS + Aux Info   |
	 * |  15:8  | RIP             |
	 * |   7:0  | Error Code      |
	 * +--------+-----------------+
	 */
	push $0				/* Reserved, must be 0 */
	push $0				/* Event data, 0 for IRQ/NMI */
	push %rdi			/* fred_ss handed in by the caller */
	push %rbp
	pushf
	mov $__KERNEL_CS, %rax
	push %rax

	/*
	 * Unlike the IDT event delivery, FRED _always_ pushes an error code
	 * after pushing the return RIP, thus the CALL instruction CANNOT be
	 * used here to push the return RIP, otherwise there is no chance to
	 * push an error code before invoking the IRQ/NMI handler.
	 *
	 * Use LEA to get the return RIP and push it, then push an error code.
	 */
	lea 1f(%rip), %rax
	push %rax				/* Return RIP */
	push $0					/* Error code, 0 for IRQ/NMI */

	PUSH_AND_CLEAR_REGS clear_bp=0 unwind_hint=0
	movq %rsp, %rdi				/* %rdi -> pt_regs */
	call __fred_entry_from_kvm		/* Call the C entry point */
	POP_REGS
	ERETS
1:
	/*
	 * Objtool doesn't understand what ERETS does, this hint tells it that
	 * yes, we'll reach here and with what stack state. A save/restore pair
	 * isn't strictly needed, but it's the simplest form.
	 */
	UNWIND_HINT_RESTORE
	pop %rbp
	RET

SYM_FUNC_END(asm_fred_entry_from_kvm)
EXPORT_SYMBOL_GPL(asm_fred_entry_from_kvm);
#endif
