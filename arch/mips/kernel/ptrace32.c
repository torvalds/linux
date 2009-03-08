/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999 MIPS Technologies, Inc.
 * Copyright (C) 2000 Ulf Carlsson
 *
 * At this time Linux/MIPS64 only supports syscall tracing, even for 32-bit
 * binaries.
 */
#include <linux/compiler.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/user.h>
#include <linux/security.h>

#include <asm/cpu.h>
#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bootinfo.h>

/*
 * Tracing a 32-bit process with a 64-bit strace and vice versa will not
 * work.  I don't know how to fix this.
 */
long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t caddr, compat_ulong_t cdata)
{
	int addr = caddr;
	int data = cdata;
	int ret;

	switch (request) {

	/*
	 * Read 4 bytes of the other process' storage
	 *  data is a pointer specifying where the user wants the
	 *	4 bytes copied into
	 *  addr is a pointer in the user's storage that contains an 8 byte
	 *	address in the other process of the 4 bytes that is to be read
	 * (this is run in a 32-bit process looking at a 64-bit process)
	 * when I and D space are separate, these will need to be fixed.
	 */
	case PTRACE_PEEKTEXT_3264:
	case PTRACE_PEEKDATA_3264: {
		u32 tmp;
		int copied;
		u32 __user * addrOthers;

		ret = -EIO;

		/* Get the addr in the other process that we want to read */
		if (get_user(addrOthers, (u32 __user * __user *) (unsigned long) addr) != 0)
			break;

		copied = access_process_vm(child, (u64)addrOthers, &tmp,
				sizeof(tmp), 0);
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp, (u32 __user *) (unsigned long) data);
		break;
	}

	/* Read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		unsigned int tmp;

		regs = task_pt_regs(child);
		ret = 0;  /* Default return value. */

		switch (addr) {
		case 0 ... 31:
			tmp = regs->regs[addr];
			break;
		case FPR_BASE ... FPR_BASE + 31:
			if (tsk_used_math(child)) {
				fpureg_t *fregs = get_fpu_regs(child);

				/*
				 * The odd registers are actually the high
				 * order bits of the values stored in the even
				 * registers - unless we're using r2k_switch.S.
				 */
				if (addr & 1)
					tmp = (unsigned long) (fregs[((addr & ~1) - 32)] >> 32);
				else
					tmp = (unsigned long) (fregs[(addr - 32)] & 0xffffffff);
			} else {
				tmp = -1;	/* FP not yet used  */
			}
			break;
		case PC:
			tmp = regs->cp0_epc;
			break;
		case CAUSE:
			tmp = regs->cp0_cause;
			break;
		case BADVADDR:
			tmp = regs->cp0_badvaddr;
			break;
		case MMHI:
			tmp = regs->hi;
			break;
		case MMLO:
			tmp = regs->lo;
			break;
		case FPC_CSR:
			tmp = child->thread.fpu.fcr31;
			break;
		case FPC_EIR: {	/* implementation / version register */
			unsigned int flags;
#ifdef CONFIG_MIPS_MT_SMTC
			unsigned int irqflags;
			unsigned int mtflags;
#endif /* CONFIG_MIPS_MT_SMTC */

			preempt_disable();
			if (!cpu_has_fpu) {
				preempt_enable();
				tmp = 0;
				break;
			}

#ifdef CONFIG_MIPS_MT_SMTC
			/* Read-modify-write of Status must be atomic */
			local_irq_save(irqflags);
			mtflags = dmt();
#endif /* CONFIG_MIPS_MT_SMTC */

			if (cpu_has_mipsmt) {
				unsigned int vpflags = dvpe();
				flags = read_c0_status();
				__enable_fpu();
				__asm__ __volatile__("cfc1\t%0,$0": "=r" (tmp));
				write_c0_status(flags);
				evpe(vpflags);
			} else {
				flags = read_c0_status();
				__enable_fpu();
				__asm__ __volatile__("cfc1\t%0,$0": "=r" (tmp));
				write_c0_status(flags);
			}
#ifdef CONFIG_MIPS_MT_SMTC
			emt(mtflags);
			local_irq_restore(irqflags);
#endif /* CONFIG_MIPS_MT_SMTC */
			preempt_enable();
			break;
		}
		case DSP_BASE ... DSP_BASE + 5: {
			dspreg_t *dregs;

			if (!cpu_has_dsp) {
				tmp = 0;
				ret = -EIO;
				goto out;
			}
			dregs = __get_dsp_regs(child);
			tmp = (unsigned long) (dregs[addr - DSP_BASE]);
			break;
		}
		case DSP_CONTROL:
			if (!cpu_has_dsp) {
				tmp = 0;
				ret = -EIO;
				goto out;
			}
			tmp = child->thread.dsp.dspcontrol;
			break;
		default:
			tmp = 0;
			ret = -EIO;
			goto out;
		}
		ret = put_user(tmp, (unsigned __user *) (unsigned long) data);
		break;
	}

	/*
	 * Write 4 bytes into the other process' storage
	 *  data is the 4 bytes that the user wants written
	 *  addr is a pointer in the user's storage that contains an
	 *	8 byte address in the other process where the 4 bytes
	 *	that is to be written
	 * (this is run in a 32-bit process looking at a 64-bit process)
	 * when I and D space are separate, these will need to be fixed.
	 */
	case PTRACE_POKETEXT_3264:
	case PTRACE_POKEDATA_3264: {
		u32 __user * addrOthers;

		/* Get the addr in the other process that we want to write into */
		ret = -EIO;
		if (get_user(addrOthers, (u32 __user * __user *) (unsigned long) addr) != 0)
			break;
		ret = 0;
		if (access_process_vm(child, (u64)addrOthers, &data,
					sizeof(data), 1) == sizeof(data))
			break;
		ret = -EIO;
		break;
	}

	case PTRACE_POKEUSR: {
		struct pt_regs *regs;
		ret = 0;
		regs = task_pt_regs(child);

		switch (addr) {
		case 0 ... 31:
			regs->regs[addr] = data;
			break;
		case FPR_BASE ... FPR_BASE + 31: {
			fpureg_t *fregs = get_fpu_regs(child);

			if (!tsk_used_math(child)) {
				/* FP not yet used  */
				memset(&child->thread.fpu, ~0,
				       sizeof(child->thread.fpu));
				child->thread.fpu.fcr31 = 0;
			}
			/*
			 * The odd registers are actually the high order bits
			 * of the values stored in the even registers - unless
			 * we're using r2k_switch.S.
			 */
			if (addr & 1) {
				fregs[(addr & ~1) - FPR_BASE] &= 0xffffffff;
				fregs[(addr & ~1) - FPR_BASE] |= ((unsigned long long) data) << 32;
			} else {
				fregs[addr - FPR_BASE] &= ~0xffffffffLL;
				/* Must cast, lest sign extension fill upper
				   bits!  */
				fregs[addr - FPR_BASE] |= (unsigned int)data;
			}
			break;
		}
		case PC:
			regs->cp0_epc = data;
			break;
		case MMHI:
			regs->hi = data;
			break;
		case MMLO:
			regs->lo = data;
			break;
		case FPC_CSR:
			child->thread.fpu.fcr31 = data;
			break;
		case DSP_BASE ... DSP_BASE + 5: {
			dspreg_t *dregs;

			if (!cpu_has_dsp) {
				ret = -EIO;
				break;
			}

			dregs = __get_dsp_regs(child);
			dregs[addr - DSP_BASE] = data;
			break;
		}
		case DSP_CONTROL:
			if (!cpu_has_dsp) {
				ret = -EIO;
				break;
			}
			child->thread.dsp.dspcontrol = data;
			break;
		default:
			/* The rest are not allowed. */
			ret = -EIO;
			break;
		}
		break;
		}

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, (__s64 __user *) (__u64) data);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, (__s64 __user *) (__u64) data);
		break;

	case PTRACE_GETFPREGS:
		ret = ptrace_getfpregs(child, (__u32 __user *) (__u64) data);
		break;

	case PTRACE_SETFPREGS:
		ret = ptrace_setfpregs(child, (__u32 __user *) (__u64) data);
		break;

	case PTRACE_GET_THREAD_AREA:
		ret = put_user(task_thread_info(child)->tp_value,
				(unsigned int __user *) (unsigned long) data);
		break;

	case PTRACE_GET_THREAD_AREA_3264:
		ret = put_user(task_thread_info(child)->tp_value,
				(unsigned long __user *) (unsigned long) data);
		break;

	case PTRACE_GET_WATCH_REGS:
		ret = ptrace_get_watch_regs(child,
			(struct pt_watch_regs __user *) (unsigned long) addr);
		break;

	case PTRACE_SET_WATCH_REGS:
		ret = ptrace_set_watch_regs(child,
			(struct pt_watch_regs __user *) (unsigned long) addr);
		break;

	default:
		ret = compat_ptrace_request(child, request, addr, data);
		break;
	}
out:
	return ret;
}
