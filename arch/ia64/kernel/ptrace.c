/*
 * Kernel support for the ptrace() and syscall tracing interfaces.
 *
 * Copyright (C) 1999-2005 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2006 Intel Co
 *  2006-08-12	- IA64 Native Utrace implementation support added by
 *	Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *
 * Derived from the x86 and Alpha versions.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/audit.h>
#include <linux/signal.h>
#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/tracehook.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace_offsets.h>
#include <asm/rse.h>
#include <linux/uaccess.h>
#include <asm/unwind.h>
#ifdef CONFIG_PERFMON
#include <asm/perfmon.h>
#endif

#include "entry.h"

/*
 * Bits in the PSR that we allow ptrace() to change:
 *	be, up, ac, mfl, mfh (the user mask; five bits total)
 *	db (debug breakpoint fault; one bit)
 *	id (instruction debug fault disable; one bit)
 *	dd (data debug fault disable; one bit)
 *	ri (restart instruction; two bits)
 *	is (instruction set; one bit)
 */
#define IPSR_MASK (IA64_PSR_UM | IA64_PSR_DB | IA64_PSR_IS	\
		   | IA64_PSR_ID | IA64_PSR_DD | IA64_PSR_RI)

#define MASK(nbits)	((1UL << (nbits)) - 1)	/* mask with NBITS bits set */
#define PFM_MASK	MASK(38)

#define PTRACE_DEBUG	0

#if PTRACE_DEBUG
# define dprintk(format...)	printk(format)
# define inline
#else
# define dprintk(format...)
#endif

/* Return TRUE if PT was created due to kernel-entry via a system-call.  */

static inline int
in_syscall (struct pt_regs *pt)
{
	return (long) pt->cr_ifs >= 0;
}

/*
 * Collect the NaT bits for r1-r31 from scratch_unat and return a NaT
 * bitset where bit i is set iff the NaT bit of register i is set.
 */
unsigned long
ia64_get_scratch_nat_bits (struct pt_regs *pt, unsigned long scratch_unat)
{
#	define GET_BITS(first, last, unat)				\
	({								\
		unsigned long bit = ia64_unat_pos(&pt->r##first);	\
		unsigned long nbits = (last - first + 1);		\
		unsigned long mask = MASK(nbits) << first;		\
		unsigned long dist;					\
		if (bit < first)					\
			dist = 64 + bit - first;			\
		else							\
			dist = bit - first;				\
		ia64_rotr(unat, dist) & mask;				\
	})
	unsigned long val;

	/*
	 * Registers that are stored consecutively in struct pt_regs
	 * can be handled in parallel.  If the register order in
	 * struct_pt_regs changes, this code MUST be updated.
	 */
	val  = GET_BITS( 1,  1, scratch_unat);
	val |= GET_BITS( 2,  3, scratch_unat);
	val |= GET_BITS(12, 13, scratch_unat);
	val |= GET_BITS(14, 14, scratch_unat);
	val |= GET_BITS(15, 15, scratch_unat);
	val |= GET_BITS( 8, 11, scratch_unat);
	val |= GET_BITS(16, 31, scratch_unat);
	return val;

#	undef GET_BITS
}

/*
 * Set the NaT bits for the scratch registers according to NAT and
 * return the resulting unat (assuming the scratch registers are
 * stored in PT).
 */
unsigned long
ia64_put_scratch_nat_bits (struct pt_regs *pt, unsigned long nat)
{
#	define PUT_BITS(first, last, nat)				\
	({								\
		unsigned long bit = ia64_unat_pos(&pt->r##first);	\
		unsigned long nbits = (last - first + 1);		\
		unsigned long mask = MASK(nbits) << first;		\
		long dist;						\
		if (bit < first)					\
			dist = 64 + bit - first;			\
		else							\
			dist = bit - first;				\
		ia64_rotl(nat & mask, dist);				\
	})
	unsigned long scratch_unat;

	/*
	 * Registers that are stored consecutively in struct pt_regs
	 * can be handled in parallel.  If the register order in
	 * struct_pt_regs changes, this code MUST be updated.
	 */
	scratch_unat  = PUT_BITS( 1,  1, nat);
	scratch_unat |= PUT_BITS( 2,  3, nat);
	scratch_unat |= PUT_BITS(12, 13, nat);
	scratch_unat |= PUT_BITS(14, 14, nat);
	scratch_unat |= PUT_BITS(15, 15, nat);
	scratch_unat |= PUT_BITS( 8, 11, nat);
	scratch_unat |= PUT_BITS(16, 31, nat);

	return scratch_unat;

#	undef PUT_BITS
}

#define IA64_MLX_TEMPLATE	0x2
#define IA64_MOVL_OPCODE	6

void
ia64_increment_ip (struct pt_regs *regs)
{
	unsigned long w0, ri = ia64_psr(regs)->ri + 1;

	if (ri > 2) {
		ri = 0;
		regs->cr_iip += 16;
	} else if (ri == 2) {
		get_user(w0, (char __user *) regs->cr_iip + 0);
		if (((w0 >> 1) & 0xf) == IA64_MLX_TEMPLATE) {
			/*
			 * rfi'ing to slot 2 of an MLX bundle causes
			 * an illegal operation fault.  We don't want
			 * that to happen...
			 */
			ri = 0;
			regs->cr_iip += 16;
		}
	}
	ia64_psr(regs)->ri = ri;
}

void
ia64_decrement_ip (struct pt_regs *regs)
{
	unsigned long w0, ri = ia64_psr(regs)->ri - 1;

	if (ia64_psr(regs)->ri == 0) {
		regs->cr_iip -= 16;
		ri = 2;
		get_user(w0, (char __user *) regs->cr_iip + 0);
		if (((w0 >> 1) & 0xf) == IA64_MLX_TEMPLATE) {
			/*
			 * rfi'ing to slot 2 of an MLX bundle causes
			 * an illegal operation fault.  We don't want
			 * that to happen...
			 */
			ri = 1;
		}
	}
	ia64_psr(regs)->ri = ri;
}

/*
 * This routine is used to read an rnat bits that are stored on the
 * kernel backing store.  Since, in general, the alignment of the user
 * and kernel are different, this is not completely trivial.  In
 * essence, we need to construct the user RNAT based on up to two
 * kernel RNAT values and/or the RNAT value saved in the child's
 * pt_regs.
 *
 * user rbs
 *
 * +--------+ <-- lowest address
 * | slot62 |
 * +--------+
 * |  rnat  | 0x....1f8
 * +--------+
 * | slot00 | \
 * +--------+ |
 * | slot01 | > child_regs->ar_rnat
 * +--------+ |
 * | slot02 | /				kernel rbs
 * +--------+				+--------+
 *	    <- child_regs->ar_bspstore	| slot61 | <-- krbs
 * +- - - - +				+--------+
 *					| slot62 |
 * +- - - - +				+--------+
 *					|  rnat	 |
 * +- - - - +				+--------+
 *   vrnat				| slot00 |
 * +- - - - +				+--------+
 *					=	 =
 *					+--------+
 *					| slot00 | \
 *					+--------+ |
 *					| slot01 | > child_stack->ar_rnat
 *					+--------+ |
 *					| slot02 | /
 *					+--------+
 *						  <--- child_stack->ar_bspstore
 *
 * The way to think of this code is as follows: bit 0 in the user rnat
 * corresponds to some bit N (0 <= N <= 62) in one of the kernel rnat
 * value.  The kernel rnat value holding this bit is stored in
 * variable rnat0.  rnat1 is loaded with the kernel rnat value that
 * form the upper bits of the user rnat value.
 *
 * Boundary cases:
 *
 * o when reading the rnat "below" the first rnat slot on the kernel
 *   backing store, rnat0/rnat1 are set to 0 and the low order bits are
 *   merged in from pt->ar_rnat.
 *
 * o when reading the rnat "above" the last rnat slot on the kernel
 *   backing store, rnat0/rnat1 gets its value from sw->ar_rnat.
 */
static unsigned long
get_rnat (struct task_struct *task, struct switch_stack *sw,
	  unsigned long *krbs, unsigned long *urnat_addr,
	  unsigned long *urbs_end)
{
	unsigned long rnat0 = 0, rnat1 = 0, urnat = 0, *slot0_kaddr;
	unsigned long umask = 0, mask, m;
	unsigned long *kbsp, *ubspstore, *rnat0_kaddr, *rnat1_kaddr, shift;
	long num_regs, nbits;
	struct pt_regs *pt;

	pt = task_pt_regs(task);
	kbsp = (unsigned long *) sw->ar_bspstore;
	ubspstore = (unsigned long *) pt->ar_bspstore;

	if (urbs_end < urnat_addr)
		nbits = ia64_rse_num_regs(urnat_addr - 63, urbs_end);
	else
		nbits = 63;
	mask = MASK(nbits);
	/*
	 * First, figure out which bit number slot 0 in user-land maps
	 * to in the kernel rnat.  Do this by figuring out how many
	 * register slots we're beyond the user's backingstore and
	 * then computing the equivalent address in kernel space.
	 */
	num_regs = ia64_rse_num_regs(ubspstore, urnat_addr + 1);
	slot0_kaddr = ia64_rse_skip_regs(krbs, num_regs);
	shift = ia64_rse_slot_num(slot0_kaddr);
	rnat1_kaddr = ia64_rse_rnat_addr(slot0_kaddr);
	rnat0_kaddr = rnat1_kaddr - 64;

	if (ubspstore + 63 > urnat_addr) {
		/* some bits need to be merged in from pt->ar_rnat */
		umask = MASK(ia64_rse_slot_num(ubspstore)) & mask;
		urnat = (pt->ar_rnat & umask);
		mask &= ~umask;
		if (!mask)
			return urnat;
	}

	m = mask << shift;
	if (rnat0_kaddr >= kbsp)
		rnat0 = sw->ar_rnat;
	else if (rnat0_kaddr > krbs)
		rnat0 = *rnat0_kaddr;
	urnat |= (rnat0 & m) >> shift;

	m = mask >> (63 - shift);
	if (rnat1_kaddr >= kbsp)
		rnat1 = sw->ar_rnat;
	else if (rnat1_kaddr > krbs)
		rnat1 = *rnat1_kaddr;
	urnat |= (rnat1 & m) << (63 - shift);
	return urnat;
}

/*
 * The reverse of get_rnat.
 */
static void
put_rnat (struct task_struct *task, struct switch_stack *sw,
	  unsigned long *krbs, unsigned long *urnat_addr, unsigned long urnat,
	  unsigned long *urbs_end)
{
	unsigned long rnat0 = 0, rnat1 = 0, *slot0_kaddr, umask = 0, mask, m;
	unsigned long *kbsp, *ubspstore, *rnat0_kaddr, *rnat1_kaddr, shift;
	long num_regs, nbits;
	struct pt_regs *pt;
	unsigned long cfm, *urbs_kargs;

	pt = task_pt_regs(task);
	kbsp = (unsigned long *) sw->ar_bspstore;
	ubspstore = (unsigned long *) pt->ar_bspstore;

	urbs_kargs = urbs_end;
	if (in_syscall(pt)) {
		/*
		 * If entered via syscall, don't allow user to set rnat bits
		 * for syscall args.
		 */
		cfm = pt->cr_ifs;
		urbs_kargs = ia64_rse_skip_regs(urbs_end, -(cfm & 0x7f));
	}

	if (urbs_kargs >= urnat_addr)
		nbits = 63;
	else {
		if ((urnat_addr - 63) >= urbs_kargs)
			return;
		nbits = ia64_rse_num_regs(urnat_addr - 63, urbs_kargs);
	}
	mask = MASK(nbits);

	/*
	 * First, figure out which bit number slot 0 in user-land maps
	 * to in the kernel rnat.  Do this by figuring out how many
	 * register slots we're beyond the user's backingstore and
	 * then computing the equivalent address in kernel space.
	 */
	num_regs = ia64_rse_num_regs(ubspstore, urnat_addr + 1);
	slot0_kaddr = ia64_rse_skip_regs(krbs, num_regs);
	shift = ia64_rse_slot_num(slot0_kaddr);
	rnat1_kaddr = ia64_rse_rnat_addr(slot0_kaddr);
	rnat0_kaddr = rnat1_kaddr - 64;

	if (ubspstore + 63 > urnat_addr) {
		/* some bits need to be place in pt->ar_rnat: */
		umask = MASK(ia64_rse_slot_num(ubspstore)) & mask;
		pt->ar_rnat = (pt->ar_rnat & ~umask) | (urnat & umask);
		mask &= ~umask;
		if (!mask)
			return;
	}
	/*
	 * Note: Section 11.1 of the EAS guarantees that bit 63 of an
	 * rnat slot is ignored. so we don't have to clear it here.
	 */
	rnat0 = (urnat << shift);
	m = mask << shift;
	if (rnat0_kaddr >= kbsp)
		sw->ar_rnat = (sw->ar_rnat & ~m) | (rnat0 & m);
	else if (rnat0_kaddr > krbs)
		*rnat0_kaddr = ((*rnat0_kaddr & ~m) | (rnat0 & m));

	rnat1 = (urnat >> (63 - shift));
	m = mask >> (63 - shift);
	if (rnat1_kaddr >= kbsp)
		sw->ar_rnat = (sw->ar_rnat & ~m) | (rnat1 & m);
	else if (rnat1_kaddr > krbs)
		*rnat1_kaddr = ((*rnat1_kaddr & ~m) | (rnat1 & m));
}

static inline int
on_kernel_rbs (unsigned long addr, unsigned long bspstore,
	       unsigned long urbs_end)
{
	unsigned long *rnat_addr = ia64_rse_rnat_addr((unsigned long *)
						      urbs_end);
	return (addr >= bspstore && addr <= (unsigned long) rnat_addr);
}

/*
 * Read a word from the user-level backing store of task CHILD.  ADDR
 * is the user-level address to read the word from, VAL a pointer to
 * the return value, and USER_BSP gives the end of the user-level
 * backing store (i.e., it's the address that would be in ar.bsp after
 * the user executed a "cover" instruction).
 *
 * This routine takes care of accessing the kernel register backing
 * store for those registers that got spilled there.  It also takes
 * care of calculating the appropriate RNaT collection words.
 */
long
ia64_peek (struct task_struct *child, struct switch_stack *child_stack,
	   unsigned long user_rbs_end, unsigned long addr, long *val)
{
	unsigned long *bspstore, *krbs, regnum, *laddr, *urbs_end, *rnat_addr;
	struct pt_regs *child_regs;
	size_t copied;
	long ret;

	urbs_end = (long *) user_rbs_end;
	laddr = (unsigned long *) addr;
	child_regs = task_pt_regs(child);
	bspstore = (unsigned long *) child_regs->ar_bspstore;
	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	if (on_kernel_rbs(addr, (unsigned long) bspstore,
			  (unsigned long) urbs_end))
	{
		/*
		 * Attempt to read the RBS in an area that's actually
		 * on the kernel RBS => read the corresponding bits in
		 * the kernel RBS.
		 */
		rnat_addr = ia64_rse_rnat_addr(laddr);
		ret = get_rnat(child, child_stack, krbs, rnat_addr, urbs_end);

		if (laddr == rnat_addr) {
			/* return NaT collection word itself */
			*val = ret;
			return 0;
		}

		if (((1UL << ia64_rse_slot_num(laddr)) & ret) != 0) {
			/*
			 * It is implementation dependent whether the
			 * data portion of a NaT value gets saved on a
			 * st8.spill or RSE spill (e.g., see EAS 2.6,
			 * 4.4.4.6 Register Spill and Fill).  To get
			 * consistent behavior across all possible
			 * IA-64 implementations, we return zero in
			 * this case.
			 */
			*val = 0;
			return 0;
		}

		if (laddr < urbs_end) {
			/*
			 * The desired word is on the kernel RBS and
			 * is not a NaT.
			 */
			regnum = ia64_rse_num_regs(bspstore, laddr);
			*val = *ia64_rse_skip_regs(krbs, regnum);
			return 0;
		}
	}
	copied = access_process_vm(child, addr, &ret, sizeof(ret), FOLL_FORCE);
	if (copied != sizeof(ret))
		return -EIO;
	*val = ret;
	return 0;
}

long
ia64_poke (struct task_struct *child, struct switch_stack *child_stack,
	   unsigned long user_rbs_end, unsigned long addr, long val)
{
	unsigned long *bspstore, *krbs, regnum, *laddr;
	unsigned long *urbs_end = (long *) user_rbs_end;
	struct pt_regs *child_regs;

	laddr = (unsigned long *) addr;
	child_regs = task_pt_regs(child);
	bspstore = (unsigned long *) child_regs->ar_bspstore;
	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	if (on_kernel_rbs(addr, (unsigned long) bspstore,
			  (unsigned long) urbs_end))
	{
		/*
		 * Attempt to write the RBS in an area that's actually
		 * on the kernel RBS => write the corresponding bits
		 * in the kernel RBS.
		 */
		if (ia64_rse_is_rnat_slot(laddr))
			put_rnat(child, child_stack, krbs, laddr, val,
				 urbs_end);
		else {
			if (laddr < urbs_end) {
				regnum = ia64_rse_num_regs(bspstore, laddr);
				*ia64_rse_skip_regs(krbs, regnum) = val;
			}
		}
	} else if (access_process_vm(child, addr, &val, sizeof(val),
				FOLL_FORCE | FOLL_WRITE)
		   != sizeof(val))
		return -EIO;
	return 0;
}

/*
 * Calculate the address of the end of the user-level register backing
 * store.  This is the address that would have been stored in ar.bsp
 * if the user had executed a "cover" instruction right before
 * entering the kernel.  If CFMP is not NULL, it is used to return the
 * "current frame mask" that was active at the time the kernel was
 * entered.
 */
unsigned long
ia64_get_user_rbs_end (struct task_struct *child, struct pt_regs *pt,
		       unsigned long *cfmp)
{
	unsigned long *krbs, *bspstore, cfm = pt->cr_ifs;
	long ndirty;

	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	bspstore = (unsigned long *) pt->ar_bspstore;
	ndirty = ia64_rse_num_regs(krbs, krbs + (pt->loadrs >> 19));

	if (in_syscall(pt))
		ndirty += (cfm & 0x7f);
	else
		cfm &= ~(1UL << 63);	/* clear valid bit */

	if (cfmp)
		*cfmp = cfm;
	return (unsigned long) ia64_rse_skip_regs(bspstore, ndirty);
}

/*
 * Synchronize (i.e, write) the RSE backing store living in kernel
 * space to the VM of the CHILD task.  SW and PT are the pointers to
 * the switch_stack and pt_regs structures, respectively.
 * USER_RBS_END is the user-level address at which the backing store
 * ends.
 */
long
ia64_sync_user_rbs (struct task_struct *child, struct switch_stack *sw,
		    unsigned long user_rbs_start, unsigned long user_rbs_end)
{
	unsigned long addr, val;
	long ret;

	/* now copy word for word from kernel rbs to user rbs: */
	for (addr = user_rbs_start; addr < user_rbs_end; addr += 8) {
		ret = ia64_peek(child, sw, user_rbs_end, addr, &val);
		if (ret < 0)
			return ret;
		if (access_process_vm(child, addr, &val, sizeof(val),
				FOLL_FORCE | FOLL_WRITE)
		    != sizeof(val))
			return -EIO;
	}
	return 0;
}

static long
ia64_sync_kernel_rbs (struct task_struct *child, struct switch_stack *sw,
		unsigned long user_rbs_start, unsigned long user_rbs_end)
{
	unsigned long addr, val;
	long ret;

	/* now copy word for word from user rbs to kernel rbs: */
	for (addr = user_rbs_start; addr < user_rbs_end; addr += 8) {
		if (access_process_vm(child, addr, &val, sizeof(val),
				FOLL_FORCE)
				!= sizeof(val))
			return -EIO;

		ret = ia64_poke(child, sw, user_rbs_end, addr, val);
		if (ret < 0)
			return ret;
	}
	return 0;
}

typedef long (*syncfunc_t)(struct task_struct *, struct switch_stack *,
			    unsigned long, unsigned long);

static void do_sync_rbs(struct unw_frame_info *info, void *arg)
{
	struct pt_regs *pt;
	unsigned long urbs_end;
	syncfunc_t fn = arg;

	if (unw_unwind_to_user(info) < 0)
		return;
	pt = task_pt_regs(info->task);
	urbs_end = ia64_get_user_rbs_end(info->task, pt, NULL);

	fn(info->task, info->sw, pt->ar_bspstore, urbs_end);
}

/*
 * when a thread is stopped (ptraced), debugger might change thread's user
 * stack (change memory directly), and we must avoid the RSE stored in kernel
 * to override user stack (user space's RSE is newer than kernel's in the
 * case). To workaround the issue, we copy kernel RSE to user RSE before the
 * task is stopped, so user RSE has updated data.  we then copy user RSE to
 * kernel after the task is resummed from traced stop and kernel will use the
 * newer RSE to return to user. TIF_RESTORE_RSE is the flag to indicate we need
 * synchronize user RSE to kernel.
 */
void ia64_ptrace_stop(void)
{
	if (test_and_set_tsk_thread_flag(current, TIF_RESTORE_RSE))
		return;
	set_notify_resume(current);
	unw_init_running(do_sync_rbs, ia64_sync_user_rbs);
}

/*
 * This is called to read back the register backing store.
 */
void ia64_sync_krbs(void)
{
	clear_tsk_thread_flag(current, TIF_RESTORE_RSE);

	unw_init_running(do_sync_rbs, ia64_sync_kernel_rbs);
}

/*
 * After PTRACE_ATTACH, a thread's register backing store area in user
 * space is assumed to contain correct data whenever the thread is
 * stopped.  arch_ptrace_stop takes care of this on tracing stops.
 * But if the child was already stopped for job control when we attach
 * to it, then it might not ever get into ptrace_stop by the time we
 * want to examine the user memory containing the RBS.
 */
void
ptrace_attach_sync_user_rbs (struct task_struct *child)
{
	int stopped = 0;
	struct unw_frame_info info;

	/*
	 * If the child is in TASK_STOPPED, we need to change that to
	 * TASK_TRACED momentarily while we operate on it.  This ensures
	 * that the child won't be woken up and return to user mode while
	 * we are doing the sync.  (It can only be woken up for SIGKILL.)
	 */

	read_lock(&tasklist_lock);
	if (child->sighand) {
		spin_lock_irq(&child->sighand->siglock);
		if (child->state == TASK_STOPPED &&
		    !test_and_set_tsk_thread_flag(child, TIF_RESTORE_RSE)) {
			set_notify_resume(child);

			child->state = TASK_TRACED;
			stopped = 1;
		}
		spin_unlock_irq(&child->sighand->siglock);
	}
	read_unlock(&tasklist_lock);

	if (!stopped)
		return;

	unw_init_from_blocked_task(&info, child);
	do_sync_rbs(&info, ia64_sync_user_rbs);

	/*
	 * Now move the child back into TASK_STOPPED if it should be in a
	 * job control stop, so that SIGCONT can be used to wake it up.
	 */
	read_lock(&tasklist_lock);
	if (child->sighand) {
		spin_lock_irq(&child->sighand->siglock);
		if (child->state == TASK_TRACED &&
		    (child->signal->flags & SIGNAL_STOP_STOPPED)) {
			child->state = TASK_STOPPED;
		}
		spin_unlock_irq(&child->sighand->siglock);
	}
	read_unlock(&tasklist_lock);
}

/*
 * Write f32-f127 back to task->thread.fph if it has been modified.
 */
inline void
ia64_flush_fph (struct task_struct *task)
{
	struct ia64_psr *psr = ia64_psr(task_pt_regs(task));

	/*
	 * Prevent migrating this task while
	 * we're fiddling with the FPU state
	 */
	preempt_disable();
	if (ia64_is_local_fpu_owner(task) && psr->mfh) {
		psr->mfh = 0;
		task->thread.flags |= IA64_THREAD_FPH_VALID;
		ia64_save_fpu(&task->thread.fph[0]);
	}
	preempt_enable();
}

/*
 * Sync the fph state of the task so that it can be manipulated
 * through thread.fph.  If necessary, f32-f127 are written back to
 * thread.fph or, if the fph state hasn't been used before, thread.fph
 * is cleared to zeroes.  Also, access to f32-f127 is disabled to
 * ensure that the task picks up the state from thread.fph when it
 * executes again.
 */
void
ia64_sync_fph (struct task_struct *task)
{
	struct ia64_psr *psr = ia64_psr(task_pt_regs(task));

	ia64_flush_fph(task);
	if (!(task->thread.flags & IA64_THREAD_FPH_VALID)) {
		task->thread.flags |= IA64_THREAD_FPH_VALID;
		memset(&task->thread.fph, 0, sizeof(task->thread.fph));
	}
	ia64_drop_fpu(task);
	psr->dfh = 1;
}

/*
 * Change the machine-state of CHILD such that it will return via the normal
 * kernel exit-path, rather than the syscall-exit path.
 */
static void
convert_to_non_syscall (struct task_struct *child, struct pt_regs  *pt,
			unsigned long cfm)
{
	struct unw_frame_info info, prev_info;
	unsigned long ip, sp, pr;

	unw_init_from_blocked_task(&info, child);
	while (1) {
		prev_info = info;
		if (unw_unwind(&info) < 0)
			return;

		unw_get_sp(&info, &sp);
		if ((long)((unsigned long)child + IA64_STK_OFFSET - sp)
		    < IA64_PT_REGS_SIZE) {
			dprintk("ptrace.%s: ran off the top of the kernel "
				"stack\n", __func__);
			return;
		}
		if (unw_get_pr (&prev_info, &pr) < 0) {
			unw_get_rp(&prev_info, &ip);
			dprintk("ptrace.%s: failed to read "
				"predicate register (ip=0x%lx)\n",
				__func__, ip);
			return;
		}
		if (unw_is_intr_frame(&info)
		    && (pr & (1UL << PRED_USER_STACK)))
			break;
	}

	/*
	 * Note: at the time of this call, the target task is blocked
	 * in notify_resume_user() and by clearling PRED_LEAVE_SYSCALL
	 * (aka, "pLvSys") we redirect execution from
	 * .work_pending_syscall_end to .work_processed_kernel.
	 */
	unw_get_pr(&prev_info, &pr);
	pr &= ~((1UL << PRED_SYSCALL) | (1UL << PRED_LEAVE_SYSCALL));
	pr |=  (1UL << PRED_NON_SYSCALL);
	unw_set_pr(&prev_info, pr);

	pt->cr_ifs = (1UL << 63) | cfm;
	/*
	 * Clear the memory that is NOT written on syscall-entry to
	 * ensure we do not leak kernel-state to user when execution
	 * resumes.
	 */
	pt->r2 = 0;
	pt->r3 = 0;
	pt->r14 = 0;
	memset(&pt->r16, 0, 16*8);	/* clear r16-r31 */
	memset(&pt->f6, 0, 6*16);	/* clear f6-f11 */
	pt->b7 = 0;
	pt->ar_ccv = 0;
	pt->ar_csd = 0;
	pt->ar_ssd = 0;
}

static int
access_nat_bits (struct task_struct *child, struct pt_regs *pt,
		 struct unw_frame_info *info,
		 unsigned long *data, int write_access)
{
	unsigned long regnum, nat_bits, scratch_unat, dummy = 0;
	char nat = 0;

	if (write_access) {
		nat_bits = *data;
		scratch_unat = ia64_put_scratch_nat_bits(pt, nat_bits);
		if (unw_set_ar(info, UNW_AR_UNAT, scratch_unat) < 0) {
			dprintk("ptrace: failed to set ar.unat\n");
			return -1;
		}
		for (regnum = 4; regnum <= 7; ++regnum) {
			unw_get_gr(info, regnum, &dummy, &nat);
			unw_set_gr(info, regnum, dummy,
				   (nat_bits >> regnum) & 1);
		}
	} else {
		if (unw_get_ar(info, UNW_AR_UNAT, &scratch_unat) < 0) {
			dprintk("ptrace: failed to read ar.unat\n");
			return -1;
		}
		nat_bits = ia64_get_scratch_nat_bits(pt, scratch_unat);
		for (regnum = 4; regnum <= 7; ++regnum) {
			unw_get_gr(info, regnum, &dummy, &nat);
			nat_bits |= (nat != 0) << regnum;
		}
		*data = nat_bits;
	}
	return 0;
}

static int
access_uarea (struct task_struct *child, unsigned long addr,
	      unsigned long *data, int write_access);

static long
ptrace_getregs (struct task_struct *child, struct pt_all_user_regs __user *ppr)
{
	unsigned long psr, ec, lc, rnat, bsp, cfm, nat_bits, val;
	struct unw_frame_info info;
	struct ia64_fpreg fpval;
	struct switch_stack *sw;
	struct pt_regs *pt;
	long ret, retval = 0;
	char nat = 0;
	int i;

	if (!access_ok(VERIFY_WRITE, ppr, sizeof(struct pt_all_user_regs)))
		return -EIO;

	pt = task_pt_regs(child);
	sw = (struct switch_stack *) (child->thread.ksp + 16);
	unw_init_from_blocked_task(&info, child);
	if (unw_unwind_to_user(&info) < 0) {
		return -EIO;
	}

	if (((unsigned long) ppr & 0x7) != 0) {
		dprintk("ptrace:unaligned register address %p\n", ppr);
		return -EIO;
	}

	if (access_uarea(child, PT_CR_IPSR, &psr, 0) < 0
	    || access_uarea(child, PT_AR_EC, &ec, 0) < 0
	    || access_uarea(child, PT_AR_LC, &lc, 0) < 0
	    || access_uarea(child, PT_AR_RNAT, &rnat, 0) < 0
	    || access_uarea(child, PT_AR_BSP, &bsp, 0) < 0
	    || access_uarea(child, PT_CFM, &cfm, 0)
	    || access_uarea(child, PT_NAT_BITS, &nat_bits, 0))
		return -EIO;

	/* control regs */

	retval |= __put_user(pt->cr_iip, &ppr->cr_iip);
	retval |= __put_user(psr, &ppr->cr_ipsr);

	/* app regs */

	retval |= __put_user(pt->ar_pfs, &ppr->ar[PT_AUR_PFS]);
	retval |= __put_user(pt->ar_rsc, &ppr->ar[PT_AUR_RSC]);
	retval |= __put_user(pt->ar_bspstore, &ppr->ar[PT_AUR_BSPSTORE]);
	retval |= __put_user(pt->ar_unat, &ppr->ar[PT_AUR_UNAT]);
	retval |= __put_user(pt->ar_ccv, &ppr->ar[PT_AUR_CCV]);
	retval |= __put_user(pt->ar_fpsr, &ppr->ar[PT_AUR_FPSR]);

	retval |= __put_user(ec, &ppr->ar[PT_AUR_EC]);
	retval |= __put_user(lc, &ppr->ar[PT_AUR_LC]);
	retval |= __put_user(rnat, &ppr->ar[PT_AUR_RNAT]);
	retval |= __put_user(bsp, &ppr->ar[PT_AUR_BSP]);
	retval |= __put_user(cfm, &ppr->cfm);

	/* gr1-gr3 */

	retval |= __copy_to_user(&ppr->gr[1], &pt->r1, sizeof(long));
	retval |= __copy_to_user(&ppr->gr[2], &pt->r2, sizeof(long) *2);

	/* gr4-gr7 */

	for (i = 4; i < 8; i++) {
		if (unw_access_gr(&info, i, &val, &nat, 0) < 0)
			return -EIO;
		retval |= __put_user(val, &ppr->gr[i]);
	}

	/* gr8-gr11 */

	retval |= __copy_to_user(&ppr->gr[8], &pt->r8, sizeof(long) * 4);

	/* gr12-gr15 */

	retval |= __copy_to_user(&ppr->gr[12], &pt->r12, sizeof(long) * 2);
	retval |= __copy_to_user(&ppr->gr[14], &pt->r14, sizeof(long));
	retval |= __copy_to_user(&ppr->gr[15], &pt->r15, sizeof(long));

	/* gr16-gr31 */

	retval |= __copy_to_user(&ppr->gr[16], &pt->r16, sizeof(long) * 16);

	/* b0 */

	retval |= __put_user(pt->b0, &ppr->br[0]);

	/* b1-b5 */

	for (i = 1; i < 6; i++) {
		if (unw_access_br(&info, i, &val, 0) < 0)
			return -EIO;
		__put_user(val, &ppr->br[i]);
	}

	/* b6-b7 */

	retval |= __put_user(pt->b6, &ppr->br[6]);
	retval |= __put_user(pt->b7, &ppr->br[7]);

	/* fr2-fr5 */

	for (i = 2; i < 6; i++) {
		if (unw_get_fr(&info, i, &fpval) < 0)
			return -EIO;
		retval |= __copy_to_user(&ppr->fr[i], &fpval, sizeof (fpval));
	}

	/* fr6-fr11 */

	retval |= __copy_to_user(&ppr->fr[6], &pt->f6,
				 sizeof(struct ia64_fpreg) * 6);

	/* fp scratch regs(12-15) */

	retval |= __copy_to_user(&ppr->fr[12], &sw->f12,
				 sizeof(struct ia64_fpreg) * 4);

	/* fr16-fr31 */

	for (i = 16; i < 32; i++) {
		if (unw_get_fr(&info, i, &fpval) < 0)
			return -EIO;
		retval |= __copy_to_user(&ppr->fr[i], &fpval, sizeof (fpval));
	}

	/* fph */

	ia64_flush_fph(child);
	retval |= __copy_to_user(&ppr->fr[32], &child->thread.fph,
				 sizeof(ppr->fr[32]) * 96);

	/*  preds */

	retval |= __put_user(pt->pr, &ppr->pr);

	/* nat bits */

	retval |= __put_user(nat_bits, &ppr->nat);

	ret = retval ? -EIO : 0;
	return ret;
}

static long
ptrace_setregs (struct task_struct *child, struct pt_all_user_regs __user *ppr)
{
	unsigned long psr, rsc, ec, lc, rnat, bsp, cfm, nat_bits, val = 0;
	struct unw_frame_info info;
	struct switch_stack *sw;
	struct ia64_fpreg fpval;
	struct pt_regs *pt;
	long ret, retval = 0;
	int i;

	memset(&fpval, 0, sizeof(fpval));

	if (!access_ok(VERIFY_READ, ppr, sizeof(struct pt_all_user_regs)))
		return -EIO;

	pt = task_pt_regs(child);
	sw = (struct switch_stack *) (child->thread.ksp + 16);
	unw_init_from_blocked_task(&info, child);
	if (unw_unwind_to_user(&info) < 0) {
		return -EIO;
	}

	if (((unsigned long) ppr & 0x7) != 0) {
		dprintk("ptrace:unaligned register address %p\n", ppr);
		return -EIO;
	}

	/* control regs */

	retval |= __get_user(pt->cr_iip, &ppr->cr_iip);
	retval |= __get_user(psr, &ppr->cr_ipsr);

	/* app regs */

	retval |= __get_user(pt->ar_pfs, &ppr->ar[PT_AUR_PFS]);
	retval |= __get_user(rsc, &ppr->ar[PT_AUR_RSC]);
	retval |= __get_user(pt->ar_bspstore, &ppr->ar[PT_AUR_BSPSTORE]);
	retval |= __get_user(pt->ar_unat, &ppr->ar[PT_AUR_UNAT]);
	retval |= __get_user(pt->ar_ccv, &ppr->ar[PT_AUR_CCV]);
	retval |= __get_user(pt->ar_fpsr, &ppr->ar[PT_AUR_FPSR]);

	retval |= __get_user(ec, &ppr->ar[PT_AUR_EC]);
	retval |= __get_user(lc, &ppr->ar[PT_AUR_LC]);
	retval |= __get_user(rnat, &ppr->ar[PT_AUR_RNAT]);
	retval |= __get_user(bsp, &ppr->ar[PT_AUR_BSP]);
	retval |= __get_user(cfm, &ppr->cfm);

	/* gr1-gr3 */

	retval |= __copy_from_user(&pt->r1, &ppr->gr[1], sizeof(long));
	retval |= __copy_from_user(&pt->r2, &ppr->gr[2], sizeof(long) * 2);

	/* gr4-gr7 */

	for (i = 4; i < 8; i++) {
		retval |= __get_user(val, &ppr->gr[i]);
		/* NaT bit will be set via PT_NAT_BITS: */
		if (unw_set_gr(&info, i, val, 0) < 0)
			return -EIO;
	}

	/* gr8-gr11 */

	retval |= __copy_from_user(&pt->r8, &ppr->gr[8], sizeof(long) * 4);

	/* gr12-gr15 */

	retval |= __copy_from_user(&pt->r12, &ppr->gr[12], sizeof(long) * 2);
	retval |= __copy_from_user(&pt->r14, &ppr->gr[14], sizeof(long));
	retval |= __copy_from_user(&pt->r15, &ppr->gr[15], sizeof(long));

	/* gr16-gr31 */

	retval |= __copy_from_user(&pt->r16, &ppr->gr[16], sizeof(long) * 16);

	/* b0 */

	retval |= __get_user(pt->b0, &ppr->br[0]);

	/* b1-b5 */

	for (i = 1; i < 6; i++) {
		retval |= __get_user(val, &ppr->br[i]);
		unw_set_br(&info, i, val);
	}

	/* b6-b7 */

	retval |= __get_user(pt->b6, &ppr->br[6]);
	retval |= __get_user(pt->b7, &ppr->br[7]);

	/* fr2-fr5 */

	for (i = 2; i < 6; i++) {
		retval |= __copy_from_user(&fpval, &ppr->fr[i], sizeof(fpval));
		if (unw_set_fr(&info, i, fpval) < 0)
			return -EIO;
	}

	/* fr6-fr11 */

	retval |= __copy_from_user(&pt->f6, &ppr->fr[6],
				   sizeof(ppr->fr[6]) * 6);

	/* fp scratch regs(12-15) */

	retval |= __copy_from_user(&sw->f12, &ppr->fr[12],
				   sizeof(ppr->fr[12]) * 4);

	/* fr16-fr31 */

	for (i = 16; i < 32; i++) {
		retval |= __copy_from_user(&fpval, &ppr->fr[i],
					   sizeof(fpval));
		if (unw_set_fr(&info, i, fpval) < 0)
			return -EIO;
	}

	/* fph */

	ia64_sync_fph(child);
	retval |= __copy_from_user(&child->thread.fph, &ppr->fr[32],
				   sizeof(ppr->fr[32]) * 96);

	/* preds */

	retval |= __get_user(pt->pr, &ppr->pr);

	/* nat bits */

	retval |= __get_user(nat_bits, &ppr->nat);

	retval |= access_uarea(child, PT_CR_IPSR, &psr, 1);
	retval |= access_uarea(child, PT_AR_RSC, &rsc, 1);
	retval |= access_uarea(child, PT_AR_EC, &ec, 1);
	retval |= access_uarea(child, PT_AR_LC, &lc, 1);
	retval |= access_uarea(child, PT_AR_RNAT, &rnat, 1);
	retval |= access_uarea(child, PT_AR_BSP, &bsp, 1);
	retval |= access_uarea(child, PT_CFM, &cfm, 1);
	retval |= access_uarea(child, PT_NAT_BITS, &nat_bits, 1);

	ret = retval ? -EIO : 0;
	return ret;
}

void
user_enable_single_step (struct task_struct *child)
{
	struct ia64_psr *child_psr = ia64_psr(task_pt_regs(child));

	set_tsk_thread_flag(child, TIF_SINGLESTEP);
	child_psr->ss = 1;
}

void
user_enable_block_step (struct task_struct *child)
{
	struct ia64_psr *child_psr = ia64_psr(task_pt_regs(child));

	set_tsk_thread_flag(child, TIF_SINGLESTEP);
	child_psr->tb = 1;
}

void
user_disable_single_step (struct task_struct *child)
{
	struct ia64_psr *child_psr = ia64_psr(task_pt_regs(child));

	/* make sure the single step/taken-branch trap bits are not set: */
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
	child_psr->ss = 0;
	child_psr->tb = 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void
ptrace_disable (struct task_struct *child)
{
	user_disable_single_step(child);
}

long
arch_ptrace (struct task_struct *child, long request,
	     unsigned long addr, unsigned long data)
{
	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		/* read word at location addr */
		if (ptrace_access_vm(child, addr, &data, sizeof(data),
				FOLL_FORCE)
		    != sizeof(data))
			return -EIO;
		/* ensure return value is not mistaken for error code */
		force_successful_syscall_return();
		return data;

	/* PTRACE_POKETEXT and PTRACE_POKEDATA is handled
	 * by the generic ptrace_request().
	 */

	case PTRACE_PEEKUSR:
		/* read the word at addr in the USER area */
		if (access_uarea(child, addr, &data, 0) < 0)
			return -EIO;
		/* ensure return value is not mistaken for error code */
		force_successful_syscall_return();
		return data;

	case PTRACE_POKEUSR:
		/* write the word at addr in the USER area */
		if (access_uarea(child, addr, &data, 1) < 0)
			return -EIO;
		return 0;

	case PTRACE_OLD_GETSIGINFO:
		/* for backwards-compatibility */
		return ptrace_request(child, PTRACE_GETSIGINFO, addr, data);

	case PTRACE_OLD_SETSIGINFO:
		/* for backwards-compatibility */
		return ptrace_request(child, PTRACE_SETSIGINFO, addr, data);

	case PTRACE_GETREGS:
		return ptrace_getregs(child,
				      (struct pt_all_user_regs __user *) data);

	case PTRACE_SETREGS:
		return ptrace_setregs(child,
				      (struct pt_all_user_regs __user *) data);

	default:
		return ptrace_request(child, request, addr, data);
	}
}


/* "asmlinkage" so the input arguments are preserved... */

asmlinkage long
syscall_trace_enter (long arg0, long arg1, long arg2, long arg3,
		     long arg4, long arg5, long arg6, long arg7,
		     struct pt_regs regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		if (tracehook_report_syscall_entry(&regs))
			return -ENOSYS;

	/* copy user rbs to kernel rbs */
	if (test_thread_flag(TIF_RESTORE_RSE))
		ia64_sync_krbs();


	audit_syscall_entry(regs.r15, arg0, arg1, arg2, arg3);

	return 0;
}

/* "asmlinkage" so the input arguments are preserved... */

asmlinkage void
syscall_trace_leave (long arg0, long arg1, long arg2, long arg3,
		     long arg4, long arg5, long arg6, long arg7,
		     struct pt_regs regs)
{
	int step;

	audit_syscall_exit(&regs);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(&regs, step);

	/* copy user rbs to kernel rbs */
	if (test_thread_flag(TIF_RESTORE_RSE))
		ia64_sync_krbs();
}

/* Utrace implementation starts here */
struct regset_get {
	void *kbuf;
	void __user *ubuf;
};

struct regset_set {
	const void *kbuf;
	const void __user *ubuf;
};

struct regset_getset {
	struct task_struct *target;
	const struct user_regset *regset;
	union {
		struct regset_get get;
		struct regset_set set;
	} u;
	unsigned int pos;
	unsigned int count;
	int ret;
};

static int
access_elf_gpreg(struct task_struct *target, struct unw_frame_info *info,
		unsigned long addr, unsigned long *data, int write_access)
{
	struct pt_regs *pt;
	unsigned long *ptr = NULL;
	int ret;
	char nat = 0;

	pt = task_pt_regs(target);
	switch (addr) {
	case ELF_GR_OFFSET(1):
		ptr = &pt->r1;
		break;
	case ELF_GR_OFFSET(2):
	case ELF_GR_OFFSET(3):
		ptr = (void *)&pt->r2 + (addr - ELF_GR_OFFSET(2));
		break;
	case ELF_GR_OFFSET(4) ... ELF_GR_OFFSET(7):
		if (write_access) {
			/* read NaT bit first: */
			unsigned long dummy;

			ret = unw_get_gr(info, addr/8, &dummy, &nat);
			if (ret < 0)
				return ret;
		}
		return unw_access_gr(info, addr/8, data, &nat, write_access);
	case ELF_GR_OFFSET(8) ... ELF_GR_OFFSET(11):
		ptr = (void *)&pt->r8 + addr - ELF_GR_OFFSET(8);
		break;
	case ELF_GR_OFFSET(12):
	case ELF_GR_OFFSET(13):
		ptr = (void *)&pt->r12 + addr - ELF_GR_OFFSET(12);
		break;
	case ELF_GR_OFFSET(14):
		ptr = &pt->r14;
		break;
	case ELF_GR_OFFSET(15):
		ptr = &pt->r15;
	}
	if (write_access)
		*ptr = *data;
	else
		*data = *ptr;
	return 0;
}

static int
access_elf_breg(struct task_struct *target, struct unw_frame_info *info,
		unsigned long addr, unsigned long *data, int write_access)
{
	struct pt_regs *pt;
	unsigned long *ptr = NULL;

	pt = task_pt_regs(target);
	switch (addr) {
	case ELF_BR_OFFSET(0):
		ptr = &pt->b0;
		break;
	case ELF_BR_OFFSET(1) ... ELF_BR_OFFSET(5):
		return unw_access_br(info, (addr - ELF_BR_OFFSET(0))/8,
				     data, write_access);
	case ELF_BR_OFFSET(6):
		ptr = &pt->b6;
		break;
	case ELF_BR_OFFSET(7):
		ptr = &pt->b7;
	}
	if (write_access)
		*ptr = *data;
	else
		*data = *ptr;
	return 0;
}

static int
access_elf_areg(struct task_struct *target, struct unw_frame_info *info,
		unsigned long addr, unsigned long *data, int write_access)
{
	struct pt_regs *pt;
	unsigned long cfm, urbs_end;
	unsigned long *ptr = NULL;

	pt = task_pt_regs(target);
	if (addr >= ELF_AR_RSC_OFFSET && addr <= ELF_AR_SSD_OFFSET) {
		switch (addr) {
		case ELF_AR_RSC_OFFSET:
			/* force PL3 */
			if (write_access)
				pt->ar_rsc = *data | (3 << 2);
			else
				*data = pt->ar_rsc;
			return 0;
		case ELF_AR_BSP_OFFSET:
			/*
			 * By convention, we use PT_AR_BSP to refer to
			 * the end of the user-level backing store.
			 * Use ia64_rse_skip_regs(PT_AR_BSP, -CFM.sof)
			 * to get the real value of ar.bsp at the time
			 * the kernel was entered.
			 *
			 * Furthermore, when changing the contents of
			 * PT_AR_BSP (or PT_CFM) while the task is
			 * blocked in a system call, convert the state
			 * so that the non-system-call exit
			 * path is used.  This ensures that the proper
			 * state will be picked up when resuming
			 * execution.  However, it *also* means that
			 * once we write PT_AR_BSP/PT_CFM, it won't be
			 * possible to modify the syscall arguments of
			 * the pending system call any longer.  This
			 * shouldn't be an issue because modifying
			 * PT_AR_BSP/PT_CFM generally implies that
			 * we're either abandoning the pending system
			 * call or that we defer it's re-execution
			 * (e.g., due to GDB doing an inferior
			 * function call).
			 */
			urbs_end = ia64_get_user_rbs_end(target, pt, &cfm);
			if (write_access) {
				if (*data != urbs_end) {
					if (in_syscall(pt))
						convert_to_non_syscall(target,
								       pt,
								       cfm);
					/*
					 * Simulate user-level write
					 * of ar.bsp:
					 */
					pt->loadrs = 0;
					pt->ar_bspstore = *data;
				}
			} else
				*data = urbs_end;
			return 0;
		case ELF_AR_BSPSTORE_OFFSET:
			ptr = &pt->ar_bspstore;
			break;
		case ELF_AR_RNAT_OFFSET:
			ptr = &pt->ar_rnat;
			break;
		case ELF_AR_CCV_OFFSET:
			ptr = &pt->ar_ccv;
			break;
		case ELF_AR_UNAT_OFFSET:
			ptr = &pt->ar_unat;
			break;
		case ELF_AR_FPSR_OFFSET:
			ptr = &pt->ar_fpsr;
			break;
		case ELF_AR_PFS_OFFSET:
			ptr = &pt->ar_pfs;
			break;
		case ELF_AR_LC_OFFSET:
			return unw_access_ar(info, UNW_AR_LC, data,
					     write_access);
		case ELF_AR_EC_OFFSET:
			return unw_access_ar(info, UNW_AR_EC, data,
					     write_access);
		case ELF_AR_CSD_OFFSET:
			ptr = &pt->ar_csd;
			break;
		case ELF_AR_SSD_OFFSET:
			ptr = &pt->ar_ssd;
		}
	} else if (addr >= ELF_CR_IIP_OFFSET && addr <= ELF_CR_IPSR_OFFSET) {
		switch (addr) {
		case ELF_CR_IIP_OFFSET:
			ptr = &pt->cr_iip;
			break;
		case ELF_CFM_OFFSET:
			urbs_end = ia64_get_user_rbs_end(target, pt, &cfm);
			if (write_access) {
				if (((cfm ^ *data) & PFM_MASK) != 0) {
					if (in_syscall(pt))
						convert_to_non_syscall(target,
								       pt,
								       cfm);
					pt->cr_ifs = ((pt->cr_ifs & ~PFM_MASK)
						      | (*data & PFM_MASK));
				}
			} else
				*data = cfm;
			return 0;
		case ELF_CR_IPSR_OFFSET:
			if (write_access) {
				unsigned long tmp = *data;
				/* psr.ri==3 is a reserved value: SDM 2:25 */
				if ((tmp & IA64_PSR_RI) == IA64_PSR_RI)
					tmp &= ~IA64_PSR_RI;
				pt->cr_ipsr = ((tmp & IPSR_MASK)
					       | (pt->cr_ipsr & ~IPSR_MASK));
			} else
				*data = (pt->cr_ipsr & IPSR_MASK);
			return 0;
		}
	} else if (addr == ELF_NAT_OFFSET)
		return access_nat_bits(target, pt, info,
				       data, write_access);
	else if (addr == ELF_PR_OFFSET)
		ptr = &pt->pr;
	else
		return -1;

	if (write_access)
		*ptr = *data;
	else
		*data = *ptr;

	return 0;
}

static int
access_elf_reg(struct task_struct *target, struct unw_frame_info *info,
		unsigned long addr, unsigned long *data, int write_access)
{
	if (addr >= ELF_GR_OFFSET(1) && addr <= ELF_GR_OFFSET(15))
		return access_elf_gpreg(target, info, addr, data, write_access);
	else if (addr >= ELF_BR_OFFSET(0) && addr <= ELF_BR_OFFSET(7))
		return access_elf_breg(target, info, addr, data, write_access);
	else
		return access_elf_areg(target, info, addr, data, write_access);
}

void do_gpregs_get(struct unw_frame_info *info, void *arg)
{
	struct pt_regs *pt;
	struct regset_getset *dst = arg;
	elf_greg_t tmp[16];
	unsigned int i, index, min_copy;

	if (unw_unwind_to_user(info) < 0)
		return;

	/*
	 * coredump format:
	 *      r0-r31
	 *      NaT bits (for r0-r31; bit N == 1 iff rN is a NaT)
	 *      predicate registers (p0-p63)
	 *      b0-b7
	 *      ip cfm user-mask
	 *      ar.rsc ar.bsp ar.bspstore ar.rnat
	 *      ar.ccv ar.unat ar.fpsr ar.pfs ar.lc ar.ec
	 */


	/* Skip r0 */
	if (dst->count > 0 && dst->pos < ELF_GR_OFFSET(1)) {
		dst->ret = user_regset_copyout_zero(&dst->pos, &dst->count,
						      &dst->u.get.kbuf,
						      &dst->u.get.ubuf,
						      0, ELF_GR_OFFSET(1));
		if (dst->ret || dst->count == 0)
			return;
	}

	/* gr1 - gr15 */
	if (dst->count > 0 && dst->pos < ELF_GR_OFFSET(16)) {
		index = (dst->pos - ELF_GR_OFFSET(1)) / sizeof(elf_greg_t);
		min_copy = ELF_GR_OFFSET(16) > (dst->pos + dst->count) ?
			 (dst->pos + dst->count) : ELF_GR_OFFSET(16);
		for (i = dst->pos; i < min_copy; i += sizeof(elf_greg_t),
				index++)
			if (access_elf_reg(dst->target, info, i,
						&tmp[index], 0) < 0) {
				dst->ret = -EIO;
				return;
			}
		dst->ret = user_regset_copyout(&dst->pos, &dst->count,
				&dst->u.get.kbuf, &dst->u.get.ubuf, tmp,
				ELF_GR_OFFSET(1), ELF_GR_OFFSET(16));
		if (dst->ret || dst->count == 0)
			return;
	}

	/* r16-r31 */
	if (dst->count > 0 && dst->pos < ELF_NAT_OFFSET) {
		pt = task_pt_regs(dst->target);
		dst->ret = user_regset_copyout(&dst->pos, &dst->count,
				&dst->u.get.kbuf, &dst->u.get.ubuf, &pt->r16,
				ELF_GR_OFFSET(16), ELF_NAT_OFFSET);
		if (dst->ret || dst->count == 0)
			return;
	}

	/* nat, pr, b0 - b7 */
	if (dst->count > 0 && dst->pos < ELF_CR_IIP_OFFSET) {
		index = (dst->pos - ELF_NAT_OFFSET) / sizeof(elf_greg_t);
		min_copy = ELF_CR_IIP_OFFSET > (dst->pos + dst->count) ?
			 (dst->pos + dst->count) : ELF_CR_IIP_OFFSET;
		for (i = dst->pos; i < min_copy; i += sizeof(elf_greg_t),
				index++)
			if (access_elf_reg(dst->target, info, i,
						&tmp[index], 0) < 0) {
				dst->ret = -EIO;
				return;
			}
		dst->ret = user_regset_copyout(&dst->pos, &dst->count,
				&dst->u.get.kbuf, &dst->u.get.ubuf, tmp,
				ELF_NAT_OFFSET, ELF_CR_IIP_OFFSET);
		if (dst->ret || dst->count == 0)
			return;
	}

	/* ip cfm psr ar.rsc ar.bsp ar.bspstore ar.rnat
	 * ar.ccv ar.unat ar.fpsr ar.pfs ar.lc ar.ec ar.csd ar.ssd
	 */
	if (dst->count > 0 && dst->pos < (ELF_AR_END_OFFSET)) {
		index = (dst->pos - ELF_CR_IIP_OFFSET) / sizeof(elf_greg_t);
		min_copy = ELF_AR_END_OFFSET > (dst->pos + dst->count) ?
			 (dst->pos + dst->count) : ELF_AR_END_OFFSET;
		for (i = dst->pos; i < min_copy; i += sizeof(elf_greg_t),
				index++)
			if (access_elf_reg(dst->target, info, i,
						&tmp[index], 0) < 0) {
				dst->ret = -EIO;
				return;
			}
		dst->ret = user_regset_copyout(&dst->pos, &dst->count,
				&dst->u.get.kbuf, &dst->u.get.ubuf, tmp,
				ELF_CR_IIP_OFFSET, ELF_AR_END_OFFSET);
	}
}

void do_gpregs_set(struct unw_frame_info *info, void *arg)
{
	struct pt_regs *pt;
	struct regset_getset *dst = arg;
	elf_greg_t tmp[16];
	unsigned int i, index;

	if (unw_unwind_to_user(info) < 0)
		return;

	/* Skip r0 */
	if (dst->count > 0 && dst->pos < ELF_GR_OFFSET(1)) {
		dst->ret = user_regset_copyin_ignore(&dst->pos, &dst->count,
						       &dst->u.set.kbuf,
						       &dst->u.set.ubuf,
						       0, ELF_GR_OFFSET(1));
		if (dst->ret || dst->count == 0)
			return;
	}

	/* gr1-gr15 */
	if (dst->count > 0 && dst->pos < ELF_GR_OFFSET(16)) {
		i = dst->pos;
		index = (dst->pos - ELF_GR_OFFSET(1)) / sizeof(elf_greg_t);
		dst->ret = user_regset_copyin(&dst->pos, &dst->count,
				&dst->u.set.kbuf, &dst->u.set.ubuf, tmp,
				ELF_GR_OFFSET(1), ELF_GR_OFFSET(16));
		if (dst->ret)
			return;
		for ( ; i < dst->pos; i += sizeof(elf_greg_t), index++)
			if (access_elf_reg(dst->target, info, i,
						&tmp[index], 1) < 0) {
				dst->ret = -EIO;
				return;
			}
		if (dst->count == 0)
			return;
	}

	/* gr16-gr31 */
	if (dst->count > 0 && dst->pos < ELF_NAT_OFFSET) {
		pt = task_pt_regs(dst->target);
		dst->ret = user_regset_copyin(&dst->pos, &dst->count,
				&dst->u.set.kbuf, &dst->u.set.ubuf, &pt->r16,
				ELF_GR_OFFSET(16), ELF_NAT_OFFSET);
		if (dst->ret || dst->count == 0)
			return;
	}

	/* nat, pr, b0 - b7 */
	if (dst->count > 0 && dst->pos < ELF_CR_IIP_OFFSET) {
		i = dst->pos;
		index = (dst->pos - ELF_NAT_OFFSET) / sizeof(elf_greg_t);
		dst->ret = user_regset_copyin(&dst->pos, &dst->count,
				&dst->u.set.kbuf, &dst->u.set.ubuf, tmp,
				ELF_NAT_OFFSET, ELF_CR_IIP_OFFSET);
		if (dst->ret)
			return;
		for (; i < dst->pos; i += sizeof(elf_greg_t), index++)
			if (access_elf_reg(dst->target, info, i,
						&tmp[index], 1) < 0) {
				dst->ret = -EIO;
				return;
			}
		if (dst->count == 0)
			return;
	}

	/* ip cfm psr ar.rsc ar.bsp ar.bspstore ar.rnat
	 * ar.ccv ar.unat ar.fpsr ar.pfs ar.lc ar.ec ar.csd ar.ssd
	 */
	if (dst->count > 0 && dst->pos < (ELF_AR_END_OFFSET)) {
		i = dst->pos;
		index = (dst->pos - ELF_CR_IIP_OFFSET) / sizeof(elf_greg_t);
		dst->ret = user_regset_copyin(&dst->pos, &dst->count,
				&dst->u.set.kbuf, &dst->u.set.ubuf, tmp,
				ELF_CR_IIP_OFFSET, ELF_AR_END_OFFSET);
		if (dst->ret)
			return;
		for ( ; i < dst->pos; i += sizeof(elf_greg_t), index++)
			if (access_elf_reg(dst->target, info, i,
						&tmp[index], 1) < 0) {
				dst->ret = -EIO;
				return;
			}
	}
}

#define ELF_FP_OFFSET(i)	(i * sizeof(elf_fpreg_t))

void do_fpregs_get(struct unw_frame_info *info, void *arg)
{
	struct regset_getset *dst = arg;
	struct task_struct *task = dst->target;
	elf_fpreg_t tmp[30];
	int index, min_copy, i;

	if (unw_unwind_to_user(info) < 0)
		return;

	/* Skip pos 0 and 1 */
	if (dst->count > 0 && dst->pos < ELF_FP_OFFSET(2)) {
		dst->ret = user_regset_copyout_zero(&dst->pos, &dst->count,
						      &dst->u.get.kbuf,
						      &dst->u.get.ubuf,
						      0, ELF_FP_OFFSET(2));
		if (dst->count == 0 || dst->ret)
			return;
	}

	/* fr2-fr31 */
	if (dst->count > 0 && dst->pos < ELF_FP_OFFSET(32)) {
		index = (dst->pos - ELF_FP_OFFSET(2)) / sizeof(elf_fpreg_t);

		min_copy = min(((unsigned int)ELF_FP_OFFSET(32)),
				dst->pos + dst->count);
		for (i = dst->pos; i < min_copy; i += sizeof(elf_fpreg_t),
				index++)
			if (unw_get_fr(info, i / sizeof(elf_fpreg_t),
					 &tmp[index])) {
				dst->ret = -EIO;
				return;
			}
		dst->ret = user_regset_copyout(&dst->pos, &dst->count,
				&dst->u.get.kbuf, &dst->u.get.ubuf, tmp,
				ELF_FP_OFFSET(2), ELF_FP_OFFSET(32));
		if (dst->count == 0 || dst->ret)
			return;
	}

	/* fph */
	if (dst->count > 0) {
		ia64_flush_fph(dst->target);
		if (task->thread.flags & IA64_THREAD_FPH_VALID)
			dst->ret = user_regset_copyout(
				&dst->pos, &dst->count,
				&dst->u.get.kbuf, &dst->u.get.ubuf,
				&dst->target->thread.fph,
				ELF_FP_OFFSET(32), -1);
		else
			/* Zero fill instead.  */
			dst->ret = user_regset_copyout_zero(
				&dst->pos, &dst->count,
				&dst->u.get.kbuf, &dst->u.get.ubuf,
				ELF_FP_OFFSET(32), -1);
	}
}

void do_fpregs_set(struct unw_frame_info *info, void *arg)
{
	struct regset_getset *dst = arg;
	elf_fpreg_t fpreg, tmp[30];
	int index, start, end;

	if (unw_unwind_to_user(info) < 0)
		return;

	/* Skip pos 0 and 1 */
	if (dst->count > 0 && dst->pos < ELF_FP_OFFSET(2)) {
		dst->ret = user_regset_copyin_ignore(&dst->pos, &dst->count,
						       &dst->u.set.kbuf,
						       &dst->u.set.ubuf,
						       0, ELF_FP_OFFSET(2));
		if (dst->count == 0 || dst->ret)
			return;
	}

	/* fr2-fr31 */
	if (dst->count > 0 && dst->pos < ELF_FP_OFFSET(32)) {
		start = dst->pos;
		end = min(((unsigned int)ELF_FP_OFFSET(32)),
			 dst->pos + dst->count);
		dst->ret = user_regset_copyin(&dst->pos, &dst->count,
				&dst->u.set.kbuf, &dst->u.set.ubuf, tmp,
				ELF_FP_OFFSET(2), ELF_FP_OFFSET(32));
		if (dst->ret)
			return;

		if (start & 0xF) { /* only write high part */
			if (unw_get_fr(info, start / sizeof(elf_fpreg_t),
					 &fpreg)) {
				dst->ret = -EIO;
				return;
			}
			tmp[start / sizeof(elf_fpreg_t) - 2].u.bits[0]
				= fpreg.u.bits[0];
			start &= ~0xFUL;
		}
		if (end & 0xF) { /* only write low part */
			if (unw_get_fr(info, end / sizeof(elf_fpreg_t),
					&fpreg)) {
				dst->ret = -EIO;
				return;
			}
			tmp[end / sizeof(elf_fpreg_t) - 2].u.bits[1]
				= fpreg.u.bits[1];
			end = (end + 0xF) & ~0xFUL;
		}

		for ( ;	start < end ; start += sizeof(elf_fpreg_t)) {
			index = start / sizeof(elf_fpreg_t);
			if (unw_set_fr(info, index, tmp[index - 2])) {
				dst->ret = -EIO;
				return;
			}
		}
		if (dst->ret || dst->count == 0)
			return;
	}

	/* fph */
	if (dst->count > 0 && dst->pos < ELF_FP_OFFSET(128)) {
		ia64_sync_fph(dst->target);
		dst->ret = user_regset_copyin(&dst->pos, &dst->count,
						&dst->u.set.kbuf,
						&dst->u.set.ubuf,
						&dst->target->thread.fph,
						ELF_FP_OFFSET(32), -1);
	}
}

static int
do_regset_call(void (*call)(struct unw_frame_info *, void *),
	       struct task_struct *target,
	       const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf)
{
	struct regset_getset info = { .target = target, .regset = regset,
				 .pos = pos, .count = count,
				 .u.set = { .kbuf = kbuf, .ubuf = ubuf },
				 .ret = 0 };

	if (target == current)
		unw_init_running(call, &info);
	else {
		struct unw_frame_info ufi;
		memset(&ufi, 0, sizeof(ufi));
		unw_init_from_blocked_task(&ufi, target);
		(*call)(&ufi, &info);
	}

	return info.ret;
}

static int
gpregs_get(struct task_struct *target,
	   const struct user_regset *regset,
	   unsigned int pos, unsigned int count,
	   void *kbuf, void __user *ubuf)
{
	return do_regset_call(do_gpregs_get, target, regset, pos, count,
		kbuf, ubuf);
}

static int gpregs_set(struct task_struct *target,
		const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf)
{
	return do_regset_call(do_gpregs_set, target, regset, pos, count,
		kbuf, ubuf);
}

static void do_gpregs_writeback(struct unw_frame_info *info, void *arg)
{
	do_sync_rbs(info, ia64_sync_user_rbs);
}

/*
 * This is called to write back the register backing store.
 * ptrace does this before it stops, so that a tracer reading the user
 * memory after the thread stops will get the current register data.
 */
static int
gpregs_writeback(struct task_struct *target,
		 const struct user_regset *regset,
		 int now)
{
	if (test_and_set_tsk_thread_flag(target, TIF_RESTORE_RSE))
		return 0;
	set_notify_resume(target);
	return do_regset_call(do_gpregs_writeback, target, regset, 0, 0,
		NULL, NULL);
}

static int
fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	return (target->thread.flags & IA64_THREAD_FPH_VALID) ? 128 : 32;
}

static int fpregs_get(struct task_struct *target,
		const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		void *kbuf, void __user *ubuf)
{
	return do_regset_call(do_fpregs_get, target, regset, pos, count,
		kbuf, ubuf);
}

static int fpregs_set(struct task_struct *target,
		const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf)
{
	return do_regset_call(do_fpregs_set, target, regset, pos, count,
		kbuf, ubuf);
}

static int
access_uarea(struct task_struct *child, unsigned long addr,
	      unsigned long *data, int write_access)
{
	unsigned int pos = -1; /* an invalid value */
	int ret;
	unsigned long *ptr, regnum;

	if ((addr & 0x7) != 0) {
		dprintk("ptrace: unaligned register address 0x%lx\n", addr);
		return -1;
	}
	if ((addr >= PT_NAT_BITS + 8 && addr < PT_F2) ||
		(addr >= PT_R7 + 8 && addr < PT_B1) ||
		(addr >= PT_AR_LC + 8 && addr < PT_CR_IPSR) ||
		(addr >= PT_AR_SSD + 8 && addr < PT_DBR)) {
		dprintk("ptrace: rejecting access to register "
					"address 0x%lx\n", addr);
		return -1;
	}

	switch (addr) {
	case PT_F32 ... (PT_F127 + 15):
		pos = addr - PT_F32 + ELF_FP_OFFSET(32);
		break;
	case PT_F2 ... (PT_F5 + 15):
		pos = addr - PT_F2 + ELF_FP_OFFSET(2);
		break;
	case PT_F10 ... (PT_F31 + 15):
		pos = addr - PT_F10 + ELF_FP_OFFSET(10);
		break;
	case PT_F6 ... (PT_F9 + 15):
		pos = addr - PT_F6 + ELF_FP_OFFSET(6);
		break;
	}

	if (pos != -1) {
		if (write_access)
			ret = fpregs_set(child, NULL, pos,
				sizeof(unsigned long), data, NULL);
		else
			ret = fpregs_get(child, NULL, pos,
				sizeof(unsigned long), data, NULL);
		if (ret != 0)
			return -1;
		return 0;
	}

	switch (addr) {
	case PT_NAT_BITS:
		pos = ELF_NAT_OFFSET;
		break;
	case PT_R4 ... PT_R7:
		pos = addr - PT_R4 + ELF_GR_OFFSET(4);
		break;
	case PT_B1 ... PT_B5:
		pos = addr - PT_B1 + ELF_BR_OFFSET(1);
		break;
	case PT_AR_EC:
		pos = ELF_AR_EC_OFFSET;
		break;
	case PT_AR_LC:
		pos = ELF_AR_LC_OFFSET;
		break;
	case PT_CR_IPSR:
		pos = ELF_CR_IPSR_OFFSET;
		break;
	case PT_CR_IIP:
		pos = ELF_CR_IIP_OFFSET;
		break;
	case PT_CFM:
		pos = ELF_CFM_OFFSET;
		break;
	case PT_AR_UNAT:
		pos = ELF_AR_UNAT_OFFSET;
		break;
	case PT_AR_PFS:
		pos = ELF_AR_PFS_OFFSET;
		break;
	case PT_AR_RSC:
		pos = ELF_AR_RSC_OFFSET;
		break;
	case PT_AR_RNAT:
		pos = ELF_AR_RNAT_OFFSET;
		break;
	case PT_AR_BSPSTORE:
		pos = ELF_AR_BSPSTORE_OFFSET;
		break;
	case PT_PR:
		pos = ELF_PR_OFFSET;
		break;
	case PT_B6:
		pos = ELF_BR_OFFSET(6);
		break;
	case PT_AR_BSP:
		pos = ELF_AR_BSP_OFFSET;
		break;
	case PT_R1 ... PT_R3:
		pos = addr - PT_R1 + ELF_GR_OFFSET(1);
		break;
	case PT_R12 ... PT_R15:
		pos = addr - PT_R12 + ELF_GR_OFFSET(12);
		break;
	case PT_R8 ... PT_R11:
		pos = addr - PT_R8 + ELF_GR_OFFSET(8);
		break;
	case PT_R16 ... PT_R31:
		pos = addr - PT_R16 + ELF_GR_OFFSET(16);
		break;
	case PT_AR_CCV:
		pos = ELF_AR_CCV_OFFSET;
		break;
	case PT_AR_FPSR:
		pos = ELF_AR_FPSR_OFFSET;
		break;
	case PT_B0:
		pos = ELF_BR_OFFSET(0);
		break;
	case PT_B7:
		pos = ELF_BR_OFFSET(7);
		break;
	case PT_AR_CSD:
		pos = ELF_AR_CSD_OFFSET;
		break;
	case PT_AR_SSD:
		pos = ELF_AR_SSD_OFFSET;
		break;
	}

	if (pos != -1) {
		if (write_access)
			ret = gpregs_set(child, NULL, pos,
				sizeof(unsigned long), data, NULL);
		else
			ret = gpregs_get(child, NULL, pos,
				sizeof(unsigned long), data, NULL);
		if (ret != 0)
			return -1;
		return 0;
	}

	/* access debug registers */
	if (addr >= PT_IBR) {
		regnum = (addr - PT_IBR) >> 3;
		ptr = &child->thread.ibr[0];
	} else {
		regnum = (addr - PT_DBR) >> 3;
		ptr = &child->thread.dbr[0];
	}

	if (regnum >= 8) {
		dprintk("ptrace: rejecting access to register "
				"address 0x%lx\n", addr);
		return -1;
	}
#ifdef CONFIG_PERFMON
	/*
	 * Check if debug registers are used by perfmon. This
	 * test must be done once we know that we can do the
	 * operation, i.e. the arguments are all valid, but
	 * before we start modifying the state.
	 *
	 * Perfmon needs to keep a count of how many processes
	 * are trying to modify the debug registers for system
	 * wide monitoring sessions.
	 *
	 * We also include read access here, because they may
	 * cause the PMU-installed debug register state
	 * (dbr[], ibr[]) to be reset. The two arrays are also
	 * used by perfmon, but we do not use
	 * IA64_THREAD_DBG_VALID. The registers are restored
	 * by the PMU context switch code.
	 */
	if (pfm_use_debug_registers(child))
		return -1;
#endif

	if (!(child->thread.flags & IA64_THREAD_DBG_VALID)) {
		child->thread.flags |= IA64_THREAD_DBG_VALID;
		memset(child->thread.dbr, 0,
				sizeof(child->thread.dbr));
		memset(child->thread.ibr, 0,
				sizeof(child->thread.ibr));
	}

	ptr += regnum;

	if ((regnum & 1) && write_access) {
		/* don't let the user set kernel-level breakpoints: */
		*ptr = *data & ~(7UL << 56);
		return 0;
	}
	if (write_access)
		*ptr = *data;
	else
		*data = *ptr;
	return 0;
}

static const struct user_regset native_regsets[] = {
	{
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t), .align = sizeof(elf_greg_t),
		.get = gpregs_get, .set = gpregs_set,
		.writeback = gpregs_writeback
	},
	{
		.core_note_type = NT_PRFPREG,
		.n = ELF_NFPREG,
		.size = sizeof(elf_fpreg_t), .align = sizeof(elf_fpreg_t),
		.get = fpregs_get, .set = fpregs_set, .active = fpregs_active
	},
};

static const struct user_regset_view user_ia64_view = {
	.name = "ia64",
	.e_machine = EM_IA_64,
	.regsets = native_regsets, .n = ARRAY_SIZE(native_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *tsk)
{
	return &user_ia64_view;
}

struct syscall_get_set_args {
	unsigned int i;
	unsigned int n;
	unsigned long *args;
	struct pt_regs *regs;
	int rw;
};

static void syscall_get_set_args_cb(struct unw_frame_info *info, void *data)
{
	struct syscall_get_set_args *args = data;
	struct pt_regs *pt = args->regs;
	unsigned long *krbs, cfm, ndirty;
	int i, count;

	if (unw_unwind_to_user(info) < 0)
		return;

	cfm = pt->cr_ifs;
	krbs = (unsigned long *)info->task + IA64_RBS_OFFSET/8;
	ndirty = ia64_rse_num_regs(krbs, krbs + (pt->loadrs >> 19));

	count = 0;
	if (in_syscall(pt))
		count = min_t(int, args->n, cfm & 0x7f);

	for (i = 0; i < count; i++) {
		if (args->rw)
			*ia64_rse_skip_regs(krbs, ndirty + i + args->i) =
				args->args[i];
		else
			args->args[i] = *ia64_rse_skip_regs(krbs,
				ndirty + i + args->i);
	}

	if (!args->rw) {
		while (i < args->n) {
			args->args[i] = 0;
			i++;
		}
	}
}

void ia64_syscall_get_set_arguments(struct task_struct *task,
	struct pt_regs *regs, unsigned int i, unsigned int n,
	unsigned long *args, int rw)
{
	struct syscall_get_set_args data = {
		.i = i,
		.n = n,
		.args = args,
		.regs = regs,
		.rw = rw,
	};

	if (task == current)
		unw_init_running(syscall_get_set_args_cb, &data);
	else {
		struct unw_frame_info ufi;
		memset(&ufi, 0, sizeof(ufi));
		unw_init_from_blocked_task(&ufi, task);
		syscall_get_set_args_cb(&ufi, &data);
	}
}
