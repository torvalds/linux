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
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <asm/stack.h>
#include <asm/traps.h>
#include <asm/setup.h>

#include <arch/interrupts.h>
#include <arch/spr_def.h>
#include <arch/opcode.h>

void __init trap_init(void)
{
	/* Nothing needed here since we link code at .intrpt */
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
	if (bundle & TILEPRO_BUNDLE_Y_ENCODING_MASK)
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

static const char *const int_name[] = {
	[INT_MEM_ERROR] = "Memory error",
	[INT_ILL] = "Illegal instruction",
	[INT_GPV] = "General protection violation",
	[INT_UDN_ACCESS] = "UDN access",
	[INT_IDN_ACCESS] = "IDN access",
#if CHIP_HAS_SN()
	[INT_SN_ACCESS] = "SN access",
#endif
	[INT_SWINT_3] = "Software interrupt 3",
	[INT_SWINT_2] = "Software interrupt 2",
	[INT_SWINT_0] = "Software interrupt 0",
	[INT_UNALIGN_DATA] = "Unaligned data",
	[INT_DOUBLE_FAULT] = "Double fault",
#ifdef __tilegx__
	[INT_ILL_TRANS] = "Illegal virtual address",
#endif
};

static int do_bpt(struct pt_regs *regs)
{
	unsigned long bundle, bcode, bpt;

	bundle = *(unsigned long *)instruction_pointer(regs);

	/*
	 * bpt shoule be { bpt; nop }, which is 0x286a44ae51485000ULL.
	 * we encode the unused least significant bits for other purpose.
	 */
	bpt = bundle & ~((1ULL << 12) - 1);
	if (bpt != TILE_BPT_BUNDLE)
		return 0;

	bcode = bundle & ((1ULL << 12) - 1);
	/*
	 * notify the kprobe handlers, if instruction is likely to
	 * pertain to them.
	 */
	switch (bcode) {
	/* breakpoint_insn */
	case 0:
		notify_die(DIE_BREAK, "debug", regs, bundle,
			INT_ILL, SIGTRAP);
		break;
	/* breakpoint2_insn */
	case DIE_SSTEPBP:
		notify_die(DIE_SSTEPBP, "single_step", regs, bundle,
			INT_ILL, SIGTRAP);
		break;
	default:
		return 0;
	}

	return 1;
}

void __kprobes do_trap(struct pt_regs *regs, int fault_num,
		       unsigned long reason)
{
	siginfo_t info = { 0 };
	int signo, code;
	unsigned long address = 0;
	bundle_bits instr;
	int is_kernel = !user_mode(regs);

	/* Handle breakpoints, etc. */
	if (is_kernel && fault_num == INT_ILL && do_bpt(regs))
		return;

	/* Re-enable interrupts, if they were previously enabled. */
	if (!(regs->flags & PT_FLAGS_DISABLE_IRQ))
		local_irq_enable();

	/*
	 * If it hits in kernel mode and we can't fix it up, just exit the
	 * current process and hope for the best.
	 */
	if (is_kernel) {
		const char *name;
		char buf[100];
		if (fixup_exception(regs))  /* ILL_TRANS or UNALIGN_DATA */
			return;
		if (fault_num >= 0 &&
		    fault_num < sizeof(int_name)/sizeof(int_name[0]) &&
		    int_name[fault_num] != NULL)
			name = int_name[fault_num];
		else
			name = "Unknown interrupt";
		if (fault_num == INT_GPV)
			snprintf(buf, sizeof(buf), "; GPV_REASON %#lx", reason);
#ifdef __tilegx__
		else if (fault_num == INT_ILL_TRANS)
			snprintf(buf, sizeof(buf), "; address %#lx", reason);
#endif
		else
			buf[0] = '\0';
		pr_alert("Kernel took bad trap %d (%s) at PC %#lx%s\n",
			 fault_num, name, regs->pc, buf);
		show_regs(regs);
		do_exit(SIGKILL);  /* FIXME: implement i386 die() */
		return;
	}

	switch (fault_num) {
	case INT_MEM_ERROR:
		signo = SIGBUS;
		code = BUS_OBJERR;
		break;
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
	case INT_ILL_TRANS: {
		/* Avoid a hardware erratum with the return address stack. */
		fill_ra_stack();

		signo = SIGSEGV;
		address = reason;
		code = SEGV_MAPERR;
		break;
	}
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
	if (signo != SIGTRAP)
		trace_unhandled_signal("trap", regs, address, signo);
	force_sig_info(signo, &info, current);
}

void kernel_double_fault(int dummy, ulong pc, ulong lr, ulong sp, ulong r52)
{
	_dump_stack(dummy, pc, lr, sp, r52);
	pr_emerg("Double fault: exiting\n");
	machine_halt();
}
