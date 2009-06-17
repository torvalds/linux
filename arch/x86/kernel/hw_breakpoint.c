/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2007 Alan Stern
 * Copyright (C) 2009 IBM Corporation
 */

/*
 * HW_breakpoint: a unified kernel/user-space hardware breakpoint facility,
 * using the CPU's debug registers.
 */

#include <linux/irqflags.h>
#include <linux/notifier.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/percpu.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/hw_breakpoint.h>
#include <asm/processor.h>
#include <asm/debugreg.h>

/* Unmasked kernel DR7 value */
static unsigned long kdr7;

/*
 * Masks for the bits corresponding to registers DR0 - DR3 in DR7 register.
 * Used to clear and verify the status of bits corresponding to DR0 - DR3
 */
static const unsigned long	dr7_masks[HBP_NUM] = {
	0x000f0003,	/* LEN0, R/W0, G0, L0 */
	0x00f0000c,	/* LEN1, R/W1, G1, L1 */
	0x0f000030,	/* LEN2, R/W2, G2, L2 */
	0xf00000c0	/* LEN3, R/W3, G3, L3 */
};


/*
 * Encode the length, type, Exact, and Enable bits for a particular breakpoint
 * as stored in debug register 7.
 */
static unsigned long encode_dr7(int drnum, unsigned int len, unsigned int type)
{
	unsigned long bp_info;

	bp_info = (len | type) & 0xf;
	bp_info <<= (DR_CONTROL_SHIFT + drnum * DR_CONTROL_SIZE);
	bp_info |= (DR_GLOBAL_ENABLE << (drnum * DR_ENABLE_SIZE)) |
				DR_GLOBAL_SLOWDOWN;
	return bp_info;
}

void arch_update_kernel_hw_breakpoint(void *unused)
{
	struct hw_breakpoint *bp;
	int i, cpu = get_cpu();
	unsigned long temp_kdr7 = 0;

	/* Don't allow debug exceptions while we update the registers */
	set_debugreg(0UL, 7);

	for (i = hbp_kernel_pos; i < HBP_NUM; i++) {
		per_cpu(this_hbp_kernel[i], cpu) = bp = hbp_kernel[i];
		if (bp) {
			temp_kdr7 |= encode_dr7(i, bp->info.len, bp->info.type);
			set_debugreg(bp->info.address, i);
		}
	}

	/* No need to set DR6. Update the debug registers with kernel-space
	 * breakpoint values from kdr7 and user-space requests from the
	 * current process
	 */
	kdr7 = temp_kdr7;
	set_debugreg(kdr7 | current->thread.debugreg7, 7);
	put_cpu();
}

/*
 * Install the thread breakpoints in their debug registers.
 */
void arch_install_thread_hw_breakpoint(struct task_struct *tsk)
{
	struct thread_struct *thread = &(tsk->thread);

	switch (hbp_kernel_pos) {
	case 4:
		set_debugreg(thread->debugreg[3], 3);
	case 3:
		set_debugreg(thread->debugreg[2], 2);
	case 2:
		set_debugreg(thread->debugreg[1], 1);
	case 1:
		set_debugreg(thread->debugreg[0], 0);
	default:
		break;
	}

	/* No need to set DR6 */
	set_debugreg((kdr7 | thread->debugreg7), 7);
}

/*
 * Install the debug register values for just the kernel, no thread.
 */
void arch_uninstall_thread_hw_breakpoint(void)
{
	/* Clear the user-space portion of debugreg7 by setting only kdr7 */
	set_debugreg(kdr7, 7);

}

static int get_hbp_len(u8 hbp_len)
{
	unsigned int len_in_bytes = 0;

	switch (hbp_len) {
	case HW_BREAKPOINT_LEN_1:
		len_in_bytes = 1;
		break;
	case HW_BREAKPOINT_LEN_2:
		len_in_bytes = 2;
		break;
	case HW_BREAKPOINT_LEN_4:
		len_in_bytes = 4;
		break;
#ifdef CONFIG_X86_64
	case HW_BREAKPOINT_LEN_8:
		len_in_bytes = 8;
		break;
#endif
	}
	return len_in_bytes;
}

/*
 * Check for virtual address in user space.
 */
int arch_check_va_in_userspace(unsigned long va, u8 hbp_len)
{
	unsigned int len;

	len = get_hbp_len(hbp_len);

	return (va <= TASK_SIZE - len);
}

/*
 * Check for virtual address in kernel space.
 */
static int arch_check_va_in_kernelspace(unsigned long va, u8 hbp_len)
{
	unsigned int len;

	len = get_hbp_len(hbp_len);

	return (va >= TASK_SIZE) && ((va + len - 1) >= TASK_SIZE);
}

/*
 * Store a breakpoint's encoded address, length, and type.
 */
static int arch_store_info(struct hw_breakpoint *bp, struct task_struct *tsk)
{
	/*
	 * User-space requests will always have the address field populated
	 * Symbol names from user-space are rejected
	 */
	if (tsk && bp->info.name)
		return -EINVAL;
	/*
	 * For kernel-addresses, either the address or symbol name can be
	 * specified.
	 */
	if (bp->info.name)
		bp->info.address = (unsigned long)
					kallsyms_lookup_name(bp->info.name);
	if (bp->info.address)
		return 0;
	return -EINVAL;
}

/*
 * Validate the arch-specific HW Breakpoint register settings
 */
int arch_validate_hwbkpt_settings(struct hw_breakpoint *bp,
						struct task_struct *tsk)
{
	unsigned int align;
	int ret = -EINVAL;

	switch (bp->info.type) {
	/*
	 * Ptrace-refactoring code
	 * For now, we'll allow instruction breakpoint only for user-space
	 * addresses
	 */
	case HW_BREAKPOINT_EXECUTE:
		if ((!arch_check_va_in_userspace(bp->info.address,
							bp->info.len)) &&
			bp->info.len != HW_BREAKPOINT_LEN_EXECUTE)
			return ret;
		break;
	case HW_BREAKPOINT_WRITE:
		break;
	case HW_BREAKPOINT_RW:
		break;
	default:
		return ret;
	}

	switch (bp->info.len) {
	case HW_BREAKPOINT_LEN_1:
		align = 0;
		break;
	case HW_BREAKPOINT_LEN_2:
		align = 1;
		break;
	case HW_BREAKPOINT_LEN_4:
		align = 3;
		break;
#ifdef CONFIG_X86_64
	case HW_BREAKPOINT_LEN_8:
		align = 7;
		break;
#endif
	default:
		return ret;
	}

	if (bp->triggered)
		ret = arch_store_info(bp, tsk);

	if (ret < 0)
		return ret;
	/*
	 * Check that the low-order bits of the address are appropriate
	 * for the alignment implied by len.
	 */
	if (bp->info.address & align)
		return -EINVAL;

	/* Check that the virtual address is in the proper range */
	if (tsk) {
		if (!arch_check_va_in_userspace(bp->info.address, bp->info.len))
			return -EFAULT;
	} else {
		if (!arch_check_va_in_kernelspace(bp->info.address,
								bp->info.len))
			return -EFAULT;
	}
	return 0;
}

void arch_update_user_hw_breakpoint(int pos, struct task_struct *tsk)
{
	struct thread_struct *thread = &(tsk->thread);
	struct hw_breakpoint *bp = thread->hbp[pos];

	thread->debugreg7 &= ~dr7_masks[pos];
	if (bp) {
		thread->debugreg[pos] = bp->info.address;
		thread->debugreg7 |= encode_dr7(pos, bp->info.len,
							bp->info.type);
	} else
		thread->debugreg[pos] = 0;
}

void arch_flush_thread_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *thread = &(tsk->thread);

	thread->debugreg7 = 0;
	for (i = 0; i < HBP_NUM; i++)
		thread->debugreg[i] = 0;
}

/*
 * Handle debug exception notifications.
 *
 * Return value is either NOTIFY_STOP or NOTIFY_DONE as explained below.
 *
 * NOTIFY_DONE returned if one of the following conditions is true.
 * i) When the causative address is from user-space and the exception
 * is a valid one, i.e. not triggered as a result of lazy debug register
 * switching
 * ii) When there are more bits than trap<n> set in DR6 register (such
 * as BD, BS or BT) indicating that more than one debug condition is
 * met and requires some more action in do_debug().
 *
 * NOTIFY_STOP returned for all other cases
 *
 */
static int __kprobes hw_breakpoint_handler(struct die_args *args)
{
	int i, cpu, rc = NOTIFY_STOP;
	struct hw_breakpoint *bp;
	unsigned long dr7, dr6;
	unsigned long *dr6_p;

	/* The DR6 value is pointed by args->err */
	dr6_p = (unsigned long *)ERR_PTR(args->err);
	dr6 = *dr6_p;

	/* Do an early return if no trap bits are set in DR6 */
	if ((dr6 & DR_TRAP_BITS) == 0)
		return NOTIFY_DONE;

	/* Lazy debug register switching */
	if (!test_tsk_thread_flag(current, TIF_DEBUG))
		arch_uninstall_thread_hw_breakpoint();

	get_debugreg(dr7, 7);
	/* Disable breakpoints during exception handling */
	set_debugreg(0UL, 7);
	/*
	 * Assert that local interrupts are disabled
	 * Reset the DRn bits in the virtualized register value.
	 * The ptrace trigger routine will add in whatever is needed.
	 */
	current->thread.debugreg6 &= ~DR_TRAP_BITS;
	cpu = get_cpu();

	/* Handle all the breakpoints that were triggered */
	for (i = 0; i < HBP_NUM; ++i) {
		if (likely(!(dr6 & (DR_TRAP0 << i))))
			continue;
		/*
		 * Find the corresponding hw_breakpoint structure and
		 * invoke its triggered callback.
		 */
		if (i >= hbp_kernel_pos)
			bp = per_cpu(this_hbp_kernel[i], cpu);
		else {
			bp = current->thread.hbp[i];
			if (bp)
				rc = NOTIFY_DONE;
		}
		/*
		 * Reset the 'i'th TRAP bit in dr6 to denote completion of
		 * exception handling
		 */
		(*dr6_p) &= ~(DR_TRAP0 << i);
		/*
		 * bp can be NULL due to lazy debug register switching
		 * or due to the delay between updates of hbp_kernel_pos
		 * and this_hbp_kernel.
		 */
		if (!bp)
			continue;

		(bp->triggered)(bp, args->regs);
	}
	if (dr6 & (~DR_TRAP_BITS))
		rc = NOTIFY_DONE;

	set_debugreg(dr7, 7);
	put_cpu();
	return rc;
}

/*
 * Handle debug exception notifications.
 */
int __kprobes hw_breakpoint_exceptions_notify(
		struct notifier_block *unused, unsigned long val, void *data)
{
	if (val != DIE_DEBUG)
		return NOTIFY_DONE;

	return hw_breakpoint_handler(data);
}
