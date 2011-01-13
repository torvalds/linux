/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <asm/opcode-tile.h>
#include <asm/opcode_constants.h>
#include <asm/stack.h>
#include <asm/traps.h>

#include <arch/interrupts.h>
#include <arch/spr_def.h>

void __init trap_init(void)
{
	/* Nothing needed here since we link code at .intrpt1 */
}

int unaligned_fixup = 1;

static int __init setup_unaligned_fixup(char *str)
{
	/*
	 * Say "=-1" to completely disable it.  If you just do "=0", we
	 * will still parse the instruction, then fire a SIGBUS with
	 * the correct address from inside the single_step code.
	 */
	long val;
	if (strict_strtol(str, 0, &val) != 0)
		return 0;
	unaligned_fixup = val;
	pr_info("Fixups for unaligned data accesses are %s\n",
	       unaligned_fixup >= 0 ?
	       (unaligned_fixup ? "enabled" : "disabled") :
	       "completely disabled");
	return 1;
}
__setup("unaligned_fixup=", setup_unaligned_fixup);

#if CHIP_HAS_TILE_DMA()

static int dma_disabled;

static int __init nodma(char *str)
{
	pr_info("User-space DMA is disabled\n");
	dma_disabled = 1;
	return 1;
}
__setup("nodma", nodma);

/* How to decode SPR_GPV_REASON */
#define IRET_ERROR (1U << 31)
#define MT_ERROR   (1U << 30)
#define MF_ERROR   (1U << 29)
#define SPR_INDEX  ((1U << 15) - 1)
#define SPR_MPL_SHIFT  9  /* starting bit position for MPL encoded in SPR */

/*
 * See if this GPV is just to notify the kernel of SPR use and we can
 * retry the user instruction after adjusting some MPLs suitably.
 */
static int retry_gpv(unsigned int gpv_reason)
{
	int mpl;

	if (gpv_reason & IRET_ERROR)
		return 0;

	BUG_ON((gpv_reason & (MT_ERROR|MF_ERROR)) == 0);
	mpl = (gpv_reason & SPR_INDEX) >> SPR_MPL_SHIFT;
	if (mpl == INT_DMA_NOTIFY && !dma_disabled) {
		/* User is turning on DMA. Allow it and retry. */
		printk(KERN_DEBUG "Process %d/%s is now enabled for DMA\n",
		       current->pid, current->comm);
		BUG_ON(current->thread.tile_dma_state.enabled);
		current->thread.tile_dma_state.enabled = 1;
		grant_dma_mpls();
		return 1;
	}

	return 0;
}

#endif /* CHIP_HAS_TILE_DMA() */

#ifdef __tilegx__
#define bundle_bits tilegx_bundle_bits
#else
#define bundle_bits tile_bundle_bits
#endif

extern bundle_bits bpt_code;

asm(".pushsection .rodata.bpt_code,\"a\";"
    ".align 8;"
    "bpt_code: bpt;"
    ".size bpt_code,.-bpt_code;"
    ".popsection");

static int special_ill(bundle_bits bundle, int *sigp, int *codep)
{
	int sig, code, maxcode;

	if (bundle == bpt_code) {
		*sigp = SIGTRAP;
		*codep = TRAP_BRKPT;
		return 1;
	}

	/* If it's a "raise" bundle, then "ill" must be in pipe X1. */
#ifdef __tilegx__
	if ((bundle & TILEGX_BUNDLE_MODE_MASK) != 0)
		return 0;
	if (get_Opcode_X1(bundle) != RRR_0_OPCODE_X1)
		return 0;
	if (get_RRROpcodeExtension_X1(bundle) != UNARY_RRR_0_OPCODE_X1)
		return 0;
	if (get_UnaryOpcodeExtension_X1(bundle) != ILL_UNARY_OPCODE_X1)
		return 0;
#else
	if (bundle & TILE_BUNDLE_Y_ENCODING_MASK)
		return 0;
	if (get_Opcode_X1(bundle) != SHUN_0_OPCODE_X1)
		return 0;
	if (get_UnShOpcodeExtension_X1(bundle) != UN_0_SHUN_0_OPCODE_X1)
		return 0;
	if (get_UnOpcodeExtension_X1(bundle) != ILL_UN_0_SHUN_0_OPCODE_X1)
		return 0;
#endif

	/* Check that the magic distinguishers are set to mean "raise". */
	if (get_Dest_X1(bundle) != 29 || get_SrcA_X1(bundle) != 37)
		return 0;

	/* There must be an "addli zero, zero, VAL" in X0. */
	if (get_Opcode_X0(bundle) != ADDLI_OPCODE_X0)
		return 0;
	if (get_Dest_X0(bundle) != TREG_ZERO)
		return 0;
	if (get_SrcA_X0(bundle) != TREG_ZERO)
		return 0;

	/*
	 * Validate the proposed signal number and si_code value.
	 * Note that we embed these in the static instruction itself
	 * so that we perturb the register state as little as possible
	 * at the time of the actual fault; it's unlikely you'd ever
	 * need to dynamically choose which kind of fault to raise
	 * from user space.
	 */
	sig = get_Imm16_X0(bundle) & 0x3f;
	switch (sig) {
	case SIGILL:
		maxcode = NSIGILL;
		break;
	case SIGFPE:
		maxcode = NSIGFPE;
		break;
	case SIGSEGV:
		maxcode = NSIGSEGV;
		break;
	case SIGBUS:
		maxcode = NSIGBUS;
		break;
	case SIGTRAP:
		maxcode = NSIGTRAP;
		break;
	default:
		return 0;
	}
	code = (get_Imm16_X0(bundle) >> 6) & 0xf;
	if (code <= 0 || code > maxcode)
		return 0;

	/* Make it the requested signal. */
	*sigp = sig;
	*codep = code | __SI_FAULT;
	return 1;
}

void __kprobes do_trap(struct pt_regs *regs, int fault_num,
		       unsigned long reason)
{
	siginfo_t info = { 0 };
	int signo, code;
	unsigned long address;
	bundle_bits instr;

	/* Re-enable interrupts. */
	local_irq_enable();

	/*
	 * If it hits in kernel mode and we can't fix it up, just exit the
	 * current process and hope for the best.
	 */
	if (!user_mode(regs)) {
		if (fixup_exception(regs))  /* only UNALIGN_DATA in practice */
			return;
		pr_alert("Kernel took bad trap %d at PC %#lx\n",
		       fault_num, regs->pc);
		if (fault_num == INT_GPV)
			pr_alert("GPV_REASON is %#lx\n", reason);
		show_regs(regs);
		do_exit(SIGKILL);  /* FIXME: implement i386 die() */
		return;
	}

	switch (fault_num) {
	case INT_ILL:
		if (copy_from_user(&instr, (void __user *)regs->pc,
				   sizeof(instr))) {
			pr_err("Unreadable instruction for INT_ILL:"
			       " %#lx\n", regs->pc);
			do_exit(SIGKILL);
			return;
		}
		if (!special_ill(instr, &signo, &code)) {
			signo = SIGILL;
			code = ILL_ILLOPC;
		}
		address = regs->pc;
		break;
	case INT_GPV:
#if CHIP_HAS_TILE_DMA()
		if (retry_gpv(reason))
			return;
#endif
		/*FALLTHROUGH*/
	case INT_UDN_ACCESS:
	case INT_IDN_ACCESS:
#if CHIP_HAS_SN()
	case INT_SN_ACCESS:
#endif
		signo = SIGILL;
		code = ILL_PRVREG;
		address = regs->pc;
		break;
	case INT_SWINT_3:
	case INT_SWINT_2:
	case INT_SWINT_0:
		signo = SIGILL;
		code = ILL_ILLTRP;
		address = regs->pc;
		break;
	case INT_UNALIGN_DATA:
#ifndef __tilegx__  /* Emulated support for single step debugging */
		if (unaligned_fixup >= 0) {
			struct single_step_state *state =
				current_thread_info()->step_state;
			if (!state ||
			    (void __user *)(regs->pc) != state->buffer) {
				single_step_once(regs);
				return;
			}
		}
#endif
		signo = SIGBUS;
		code = BUS_ADRALN;
		address = 0;
		break;
	case INT_DOUBLE_FAULT:
		/*
		 * For double fault, "reason" is actually passed as
		 * SYSTEM_SAVE_K_2, the hypervisor's double-fault info, so
		 * we can provide the original fault number rather than
		 * the uninteresting "INT_DOUBLE_FAULT" so the user can
		 * learn what actually struck while PL0 ICS was set.
		 */
		fault_num = reason;
		signo = SIGILL;
		code = ILL_DBLFLT;
		address = regs->pc;
		break;
#ifdef __tilegx__
	case INT_ILL_TRANS:
		signo = SIGSEGV;
		code = SEGV_MAPERR;
		if (reason & SPR_ILL_TRANS_REASON__I_STREAM_VA_RMASK)
			address = regs->pc;
		else
			address = 0;  /* FIXME: GX: single-step for address */
		break;
#endif
	default:
		panic("Unexpected do_trap interrupt number %d", fault_num);
		return;
	}

	info.si_signo = signo;
	info.si_code = code;
	info.si_addr = (void __user *)address;
	if (signo == SIGILL)
		info.si_trapno = fault_num;
	force_sig_info(signo, &info, current);
}

void kernel_double_fault(int dummy, ulong pc, ulong lr, ulong sp, ulong r52)
{
	_dump_stack(dummy, pc, lr, sp, r52);
	pr_emerg("Double fault: exiting\n");
	machine_halt();
}
