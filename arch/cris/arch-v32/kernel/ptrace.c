/*
 * Copyright (C) 2000-2003, Axis Communications AB.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/signal.h>
#include <linux/security.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/arch/hwregs/supp_reg.h>

/*
 * Determines which bits in CCS the user has access to.
 * 1 = access, 0 = no access.
 */
#define CCS_MASK 0x00087c00     /* SXNZVC */

#define SBIT_USER (1 << (S_CCS_BITNR + CCS_SHIFT))

static int put_debugreg(long pid, unsigned int regno, long data);
static long get_debugreg(long pid, unsigned int regno);
static unsigned long get_pseudo_pc(struct task_struct *child);
void deconfigure_bp(long pid);

extern unsigned long cris_signal_return_page;

/*
 * Get contents of register REGNO in task TASK.
 */
long get_reg(struct task_struct *task, unsigned int regno)
{
	/* USP is a special case, it's not in the pt_regs struct but
	 * in the tasks thread struct
	 */
	unsigned long ret;

	if (regno <= PT_EDA)
		ret = ((unsigned long *)task_pt_regs(task))[regno];
	else if (regno == PT_USP)
		ret = task->thread.usp;
	else if (regno == PT_PPC)
		ret = get_pseudo_pc(task);
	else if (regno <= PT_MAX)
		ret = get_debugreg(task->pid, regno);
	else
		ret = 0;

	return ret;
}

/*
 * Write contents of register REGNO in task TASK.
 */
int put_reg(struct task_struct *task, unsigned int regno, unsigned long data)
{
	if (regno <= PT_EDA)
		((unsigned long *)task_pt_regs(task))[regno] = data;
	else if (regno == PT_USP)
		task->thread.usp = data;
	else if (regno == PT_PPC) {
		/* Write pseudo-PC to ERP only if changed. */
		if (data != get_pseudo_pc(task))
			task_pt_regs(task)->erp = data;
	} else if (regno <= PT_MAX)
		return put_debugreg(task->pid, regno, data);
	else
		return -1;
	return 0;
}

/*
 * Called by kernel/ptrace.c when detaching.
 *
 * Make sure the single step bit is not set.
 */
void
ptrace_disable(struct task_struct *child)
{
	unsigned long tmp;

	/* Deconfigure SPC and S-bit. */
	tmp = get_reg(child, PT_CCS) & ~SBIT_USER;
	put_reg(child, PT_CCS, tmp);
	put_reg(child, PT_SPC, 0);

	/* Deconfigure any watchpoints associated with the child. */
	deconfigure_bp(child->pid);
}


long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;
	unsigned long __user *datap = (unsigned long __user *)data;

	switch (request) {
		/* Read word at location address. */
		case PTRACE_PEEKTEXT:
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int copied;

			ret = -EIO;

			/* The signal trampoline page is outside the normal user-addressable
			 * space but still accessible. This is hack to make it possible to
			 * access the signal handler code in GDB.
			 */
			if ((addr & PAGE_MASK) == cris_signal_return_page) {
				/* The trampoline page is globally mapped, no page table to traverse.*/
				tmp = *(unsigned long*)addr;
			} else {
				copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);

				if (copied != sizeof(tmp))
					break;
			}

			ret = put_user(tmp,datap);
			break;
		}

		/* Read the word at location address in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;

			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr > PT_MAX << 2)
				break;

			tmp = get_reg(child, addr >> 2);
			ret = put_user(tmp, datap);
			break;
		}

		/* Write the word at location address. */
		case PTRACE_POKETEXT:
		case PTRACE_POKEDATA:
			ret = generic_ptrace_pokedata(child, addr, data);
			break;

 		/* Write the word at location address in the USER area. */
		case PTRACE_POKEUSR:
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr > PT_MAX << 2)
				break;

			addr >>= 2;

			if (addr == PT_CCS) {
				/* don't allow the tracing process to change stuff like
				 * interrupt enable, kernel/user bit, dma enables etc.
				 */
				data &= CCS_MASK;
				data |= get_reg(child, PT_CCS) & ~CCS_MASK;
			}
			if (put_reg(child, addr, data))
				break;
			ret = 0;
			break;

		case PTRACE_SYSCALL:
		case PTRACE_CONT:
			ret = -EIO;

			if (!valid_signal(data))
				break;

			/* Continue means no single-step. */
			put_reg(child, PT_SPC, 0);

			if (!get_debugreg(child->pid, PT_BP_CTRL)) {
				unsigned long tmp;
				/* If no h/w bp configured, disable S bit. */
				tmp = get_reg(child, PT_CCS) & ~SBIT_USER;
				put_reg(child, PT_CCS, tmp);
			}

			if (request == PTRACE_SYSCALL) {
				set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			}
			else {
				clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			}

			child->exit_code = data;

			/* TODO: make sure any pending breakpoint is killed */
			wake_up_process(child);
			ret = 0;

			break;

 		/* Make the child exit by sending it a sigkill. */
		case PTRACE_KILL:
			ret = 0;

			if (child->exit_state == EXIT_ZOMBIE)
				break;

			child->exit_code = SIGKILL;

			/* Deconfigure single-step and h/w bp. */
			ptrace_disable(child);

			/* TODO: make sure any pending breakpoint is killed */
			wake_up_process(child);
			break;

		/* Set the trap flag. */
		case PTRACE_SINGLESTEP:	{
			unsigned long tmp;
			ret = -EIO;

			/* Set up SPC if not set already (in which case we have
			   no other choice but to trust it). */
			if (!get_reg(child, PT_SPC)) {
				/* In case we're stopped in a delay slot. */
				tmp = get_reg(child, PT_ERP) & ~1;
				put_reg(child, PT_SPC, tmp);
			}
			tmp = get_reg(child, PT_CCS) | SBIT_USER;
			put_reg(child, PT_CCS, tmp);

			if (!valid_signal(data))
				break;

			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

			/* TODO: set some clever breakpoint mechanism... */

			child->exit_code = data;
			wake_up_process(child);
			ret = 0;
			break;

		}
		case PTRACE_DETACH:
			ret = ptrace_detach(child, data);
			break;

		/* Get all GP registers from the child. */
		case PTRACE_GETREGS: {
		  	int i;
			unsigned long tmp;

			for (i = 0; i <= PT_MAX; i++) {
				tmp = get_reg(child, i);

				if (put_user(tmp, datap)) {
					ret = -EFAULT;
					goto out_tsk;
				}

				datap++;
			}

			ret = 0;
			break;
		}

		/* Set all GP registers in the child. */
		case PTRACE_SETREGS: {
			int i;
			unsigned long tmp;

			for (i = 0; i <= PT_MAX; i++) {
				if (get_user(tmp, datap)) {
					ret = -EFAULT;
					goto out_tsk;
				}

				if (i == PT_CCS) {
					tmp &= CCS_MASK;
					tmp |= get_reg(child, PT_CCS) & ~CCS_MASK;
				}

				put_reg(child, i, tmp);
				datap++;
			}

			ret = 0;
			break;
		}

		default:
			ret = ptrace_request(child, request, addr, data);
			break;
	}

	return ret;
}

void do_syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;

	if (!(current->ptrace & PT_PTRACED))
		return;

	/* the 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));

	/*
	 * This isn't the same as continuing with a signal, but it will do for
	 * normal use.
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

/* Returns the size of an instruction that has a delay slot. */

static int insn_size(struct task_struct *child, unsigned long pc)
{
  unsigned long opcode;
  int copied;
  int opsize = 0;

  /* Read the opcode at pc (do what PTRACE_PEEKTEXT would do). */
  copied = access_process_vm(child, pc, &opcode, sizeof(opcode), 0);
  if (copied != sizeof(opcode))
    return 0;

  switch ((opcode & 0x0f00) >> 8) {
  case 0x0:
  case 0x9:
  case 0xb:
	  opsize = 2;
	  break;
  case 0xe:
  case 0xf:
	  opsize = 6;
	  break;
  case 0xd:
	  /* Could be 4 or 6; check more bits. */
	  if ((opcode & 0xff) == 0xff)
		  opsize = 4;
	  else
		  opsize = 6;
	  break;
  default:
	  panic("ERROR: Couldn't find size of opcode 0x%lx at 0x%lx\n",
		opcode, pc);
  }

  return opsize;
}

static unsigned long get_pseudo_pc(struct task_struct *child)
{
	/* Default value for PC is ERP. */
	unsigned long pc = get_reg(child, PT_ERP);

	if (pc & 0x1) {
		unsigned long spc = get_reg(child, PT_SPC);
		/* Delay slot bit set. Report as stopped on proper
		   instruction. */
		if (spc) {
			/* Rely on SPC if set. FIXME: We might want to check
			   that EXS indicates we stopped due to a single-step
			   exception. */
			pc = spc;
		} else {
			/* Calculate the PC from the size of the instruction
			   that the delay slot we're in belongs to. */
			pc += insn_size(child, pc & ~1) - 1;
		}
	}
	return pc;
}

static long bp_owner = 0;

/* Reachable from exit_thread in signal.c, so not static. */
void deconfigure_bp(long pid)
{
	int bp;

	/* Only deconfigure if the pid is the owner. */
	if (bp_owner != pid)
		return;

	for (bp = 0; bp < 6; bp++) {
		unsigned long tmp;
		/* Deconfigure start and end address (also gets rid of ownership). */
		put_debugreg(pid, PT_BP + 3 + (bp * 2), 0);
		put_debugreg(pid, PT_BP + 4 + (bp * 2), 0);

		/* Deconfigure relevant bits in control register. */
		tmp = get_debugreg(pid, PT_BP_CTRL) & ~(3 << (2 + (bp * 4)));
		put_debugreg(pid, PT_BP_CTRL, tmp);
	}
	/* No owner now. */
	bp_owner = 0;
}

static int put_debugreg(long pid, unsigned int regno, long data)
{
	int ret = 0;
	register int old_srs;

#ifdef CONFIG_ETRAX_KGDB
	/* Ignore write, but pretend it was ok if value is 0
	   (we don't want POKEUSR/SETREGS failing unnessecarily). */
	return (data == 0) ? ret : -1;
#endif

	/* Simple owner management. */
	if (!bp_owner)
		bp_owner = pid;
	else if (bp_owner != pid) {
		/* Ignore write, but pretend it was ok if value is 0
		   (we don't want POKEUSR/SETREGS failing unnessecarily). */
		return (data == 0) ? ret : -1;
	}

	/* Remember old SRS. */
	SPEC_REG_RD(SPEC_REG_SRS, old_srs);
	/* Switch to BP bank. */
	SUPP_BANK_SEL(BANK_BP);

	switch (regno - PT_BP) {
	case 0:
		SUPP_REG_WR(0, data); break;
	case 1:
	case 2:
		if (data)
			ret = -1;
		break;
	case 3:
		SUPP_REG_WR(3, data); break;
	case 4:
		SUPP_REG_WR(4, data); break;
	case 5:
		SUPP_REG_WR(5, data); break;
	case 6:
		SUPP_REG_WR(6, data); break;
	case 7:
		SUPP_REG_WR(7, data); break;
	case 8:
		SUPP_REG_WR(8, data); break;
	case 9:
		SUPP_REG_WR(9, data); break;
	case 10:
		SUPP_REG_WR(10, data); break;
	case 11:
		SUPP_REG_WR(11, data); break;
	case 12:
		SUPP_REG_WR(12, data); break;
	case 13:
		SUPP_REG_WR(13, data); break;
	case 14:
		SUPP_REG_WR(14, data); break;
	default:
		ret = -1;
		break;
	}

	/* Restore SRS. */
	SPEC_REG_WR(SPEC_REG_SRS, old_srs);
	/* Just for show. */
	NOP();
	NOP();
	NOP();

	return ret;
}

static long get_debugreg(long pid, unsigned int regno)
{
	register int old_srs;
	register long data;

	if (pid != bp_owner) {
		return 0;
	}

	/* Remember old SRS. */
	SPEC_REG_RD(SPEC_REG_SRS, old_srs);
	/* Switch to BP bank. */
	SUPP_BANK_SEL(BANK_BP);

	switch (regno - PT_BP) {
	case 0:
		SUPP_REG_RD(0, data); break;
	case 1:
	case 2:
		/* error return value? */
		data = 0;
		break;
	case 3:
		SUPP_REG_RD(3, data); break;
	case 4:
		SUPP_REG_RD(4, data); break;
	case 5:
		SUPP_REG_RD(5, data); break;
	case 6:
		SUPP_REG_RD(6, data); break;
	case 7:
		SUPP_REG_RD(7, data); break;
	case 8:
		SUPP_REG_RD(8, data); break;
	case 9:
		SUPP_REG_RD(9, data); break;
	case 10:
		SUPP_REG_RD(10, data); break;
	case 11:
		SUPP_REG_RD(11, data); break;
	case 12:
		SUPP_REG_RD(12, data); break;
	case 13:
		SUPP_REG_RD(13, data); break;
	case 14:
		SUPP_REG_RD(14, data); break;
	default:
		/* error return value? */
		data = 0;
	}

	/* Restore SRS. */
	SPEC_REG_WR(SPEC_REG_SRS, old_srs);
	/* Just for show. */
	NOP();
	NOP();
	NOP();

	return data;
}
