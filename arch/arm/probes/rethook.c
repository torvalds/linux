// SPDX-License-Identifier: GPL-2.0-only
/*
 * arm implementation of rethook. Mostly copied from arch/arm/probes/kprobes/core.c
 */

#include <linux/kprobes.h>
#include <linux/rethook.h>

/* Called from arch_rethook_trampoline */
static __used unsigned long arch_rethook_trampoline_callback(struct pt_regs *regs)
{
	return rethook_trampoline_handler(regs, regs->ARM_fp);
}
NOKPROBE_SYMBOL(arch_rethook_trampoline_callback);

/*
 * When a rethook'ed function returns, it returns to arch_rethook_trampoline
 * which calls rethook callback. We construct a struct pt_regs to
 * give a view of registers r0-r11, sp, lr, and pc to the user
 * return-handler. This is not a complete pt_regs structure, but that
 * should be enough for stacktrace from the return handler with or
 * without pt_regs.
 */
asm(
	".text\n"
	".global arch_rethook_trampoline\n"
	".type arch_rethook_trampoline, %function\n"
	"arch_rethook_trampoline:\n"
#ifdef CONFIG_FRAME_POINTER
	"ldr	lr, =arch_rethook_trampoline	\n\t"
	/* this makes a framepointer on pt_regs. */
#ifdef CONFIG_CC_IS_CLANG
	"stmdb	sp, {sp, lr, pc}	\n\t"
	"sub	sp, sp, #12		\n\t"
	/* In clang case, pt_regs->ip = lr. */
	"stmdb	sp!, {r0 - r11, lr}	\n\t"
	/* fp points regs->r11 (fp) */
	"add	fp, sp,	#44		\n\t"
#else /* !CONFIG_CC_IS_CLANG */
	/* In gcc case, pt_regs->ip = fp. */
	"stmdb	sp, {fp, sp, lr, pc}	\n\t"
	"sub	sp, sp, #16		\n\t"
	"stmdb	sp!, {r0 - r11}		\n\t"
	/* fp points regs->r15 (pc) */
	"add	fp, sp, #60		\n\t"
#endif /* CONFIG_CC_IS_CLANG */
#else /* !CONFIG_FRAME_POINTER */
	"sub	sp, sp, #16		\n\t"
	"stmdb	sp!, {r0 - r11}		\n\t"
#endif /* CONFIG_FRAME_POINTER */
	"mov	r0, sp			\n\t"
	"bl	arch_rethook_trampoline_callback	\n\t"
	"mov	lr, r0			\n\t"
	"ldmia	sp!, {r0 - r11}		\n\t"
	"add	sp, sp, #16		\n\t"
#ifdef CONFIG_THUMB2_KERNEL
	"bx	lr			\n\t"
#else
	"mov	pc, lr			\n\t"
#endif
	".size arch_rethook_trampoline, .-arch_rethook_trampoline\n"
);
NOKPROBE_SYMBOL(arch_rethook_trampoline);

/*
 * At the entry of function with mcount. The stack and registers are prepared
 * for the mcount function as below.
 *
 * mov     ip, sp
 * push    {fp, ip, lr, pc}
 * sub     fp, ip, #4	; FP[0] = PC, FP[-4] = LR, and FP[-12] = call-site FP.
 * push    {lr}
 * bl      <__gnu_mcount_nc> ; call ftrace
 *
 * And when returning from the function, call-site FP, SP and PC are restored
 * from stack as below;
 *
 * ldm     sp, {fp, sp, pc}
 *
 * Thus, if the arch_rethook_prepare() is called from real function entry,
 * it must change the LR and save FP in pt_regs. But if it is called via
 * mcount context (ftrace), it must change the LR on stack, which is next
 * to the PC (= FP[-4]), and save the FP value at FP[-12].
 */
void arch_rethook_prepare(struct rethook_node *rh, struct pt_regs *regs, bool mcount)
{
	unsigned long *ret_addr, *frame;

	if (mcount) {
		ret_addr = (unsigned long *)(regs->ARM_fp - 4);
		frame = (unsigned long *)(regs->ARM_fp - 12);
	} else {
		ret_addr = &regs->ARM_lr;
		frame = &regs->ARM_fp;
	}

	rh->ret_addr = *ret_addr;
	rh->frame = *frame;

	/* Replace the return addr with trampoline addr. */
	*ret_addr = (unsigned long)arch_rethook_trampoline;
}
NOKPROBE_SYMBOL(arch_rethook_prepare);
