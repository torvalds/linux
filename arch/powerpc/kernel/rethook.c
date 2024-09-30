// SPDX-License-Identifier: GPL-2.0-only
/*
 * PowerPC implementation of rethook. This depends on kprobes.
 */

#include <linux/kprobes.h>
#include <linux/rethook.h>

/*
 * Function return trampoline:
 *     - init_kprobes() establishes a probepoint here
 *     - When the probed function returns, this probe
 *         causes the handlers to fire
 */
asm(".global arch_rethook_trampoline\n"
	".type arch_rethook_trampoline, @function\n"
	"arch_rethook_trampoline:\n"
	"nop\n"
	"blr\n"
	".size arch_rethook_trampoline, .-arch_rethook_trampoline\n");

/*
 * Called when the probe at kretprobe trampoline is hit
 */
static int trampoline_rethook_handler(struct kprobe *p, struct pt_regs *regs)
{
	return !rethook_trampoline_handler(regs, regs->gpr[1]);
}
NOKPROBE_SYMBOL(trampoline_rethook_handler);

void arch_rethook_prepare(struct rethook_node *rh, struct pt_regs *regs, bool mcount)
{
	rh->ret_addr = regs->link;
	rh->frame = regs->gpr[1];

	/* Replace the return addr with trampoline addr */
	regs->link = (unsigned long)arch_rethook_trampoline;
}
NOKPROBE_SYMBOL(arch_rethook_prepare);

/* This is called from rethook_trampoline_handler(). */
void arch_rethook_fixup_return(struct pt_regs *regs, unsigned long orig_ret_address)
{
	/*
	 * We get here through one of two paths:
	 * 1. by taking a trap -> kprobe_handler() -> here
	 * 2. by optprobe branch -> optimized_callback() -> opt_pre_handler() -> here
	 *
	 * When going back through (1), we need regs->nip to be setup properly
	 * as it is used to determine the return address from the trap.
	 * For (2), since nip is not honoured with optprobes, we instead setup
	 * the link register properly so that the subsequent 'blr' in
	 * arch_rethook_trampoline jumps back to the right instruction.
	 *
	 * For nip, we should set the address to the previous instruction since
	 * we end up emulating it in kprobe_handler(), which increments the nip
	 * again.
	 */
	regs_set_return_ip(regs, orig_ret_address - 4);
	regs->link = orig_ret_address;
}
NOKPROBE_SYMBOL(arch_rethook_fixup_return);

static struct kprobe trampoline_p = {
	.addr = (kprobe_opcode_t *) &arch_rethook_trampoline,
	.pre_handler = trampoline_rethook_handler
};

/* rethook initializer */
int __init arch_init_kprobes(void)
{
	return register_kprobe(&trampoline_p);
}
