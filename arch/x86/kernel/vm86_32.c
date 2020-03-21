// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1994  Linus Torvalds
 *
 *  29 dec 2001 - Fixed oopses caused by unchecked access to the vm86
 *                stack - Manfred Spraul <manfred@colorfullife.com>
 *
 *  22 mar 2002 - Manfred detected the stackfaults, but didn't handle
 *                them correctly. Now the emulation will be in a
 *                consistent state after stackfaults - Kasper Dupont
 *                <kasperd@daimi.au.dk>
 *
 *  22 mar 2002 - Added missing clear_IF in set_vflags_* Kasper Dupont
 *                <kasperd@daimi.au.dk>
 *
 *  ?? ??? 2002 - Fixed premature returns from handle_vm86_fault
 *                caused by Kasper Dupont's changes - Stas Sergeev
 *
 *   4 apr 2002 - Fixed CHECK_IF_IN_TRAP broken by Stas' changes.
 *                Kasper Dupont <kasperd@daimi.au.dk>
 *
 *   9 apr 2002 - Changed syntax of macros in handle_vm86_fault.
 *                Kasper Dupont <kasperd@daimi.au.dk>
 *
 *   9 apr 2002 - Changed stack access macros to jump to a label
 *                instead of returning to userspace. This simplifies
 *                do_int, and is needed by handle_vm6_fault. Kasper
 *                Dupont <kasperd@daimi.au.dk>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/audit.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/security.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/vm86.h>
#include <asm/switch_to.h>

/*
 * Known problems:
 *
 * Interrupt handling is not guaranteed:
 * - a real x86 will disable all interrupts for one instruction
 *   after a "mov ss,xx" to make stack handling atomic even without
 *   the 'lss' instruction. We can't guarantee this in v86 mode,
 *   as the next instruction might result in a page fault or similar.
 * - a real x86 will have interrupts disabled for one instruction
 *   past the 'sti' that enables them. We don't bother with all the
 *   details yet.
 *
 * Let's hope these problems do not actually matter for anything.
 */


/*
 * 8- and 16-bit register defines..
 */
#define AL(regs)	(((unsigned char *)&((regs)->pt.ax))[0])
#define AH(regs)	(((unsigned char *)&((regs)->pt.ax))[1])
#define IP(regs)	(*(unsigned short *)&((regs)->pt.ip))
#define SP(regs)	(*(unsigned short *)&((regs)->pt.sp))

/*
 * virtual flags (16 and 32-bit versions)
 */
#define VFLAGS	(*(unsigned short *)&(current->thread.vm86->veflags))
#define VEFLAGS	(current->thread.vm86->veflags)

#define set_flags(X, new, mask) \
((X) = ((X) & ~(mask)) | ((new) & (mask)))

#define SAFE_MASK	(0xDD5)
#define RETURN_MASK	(0xDFF)

void save_v86_state(struct kernel_vm86_regs *regs, int retval)
{
	struct task_struct *tsk = current;
	struct vm86plus_struct __user *user;
	struct vm86 *vm86 = current->thread.vm86;
	long err = 0;

	/*
	 * This gets called from entry.S with interrupts disabled, but
	 * from process context. Enable interrupts here, before trying
	 * to access user space.
	 */
	local_irq_enable();

	if (!vm86 || !vm86->user_vm86) {
		pr_alert("no user_vm86: BAD\n");
		do_exit(SIGSEGV);
	}
	set_flags(regs->pt.flags, VEFLAGS, X86_EFLAGS_VIF | vm86->veflags_mask);
	user = vm86->user_vm86;

	if (!access_ok(user, vm86->vm86plus.is_vm86pus ?
		       sizeof(struct vm86plus_struct) :
		       sizeof(struct vm86_struct))) {
		pr_alert("could not access userspace vm86 info\n");
		do_exit(SIGSEGV);
	}

	put_user_try {
		put_user_ex(regs->pt.bx, &user->regs.ebx);
		put_user_ex(regs->pt.cx, &user->regs.ecx);
		put_user_ex(regs->pt.dx, &user->regs.edx);
		put_user_ex(regs->pt.si, &user->regs.esi);
		put_user_ex(regs->pt.di, &user->regs.edi);
		put_user_ex(regs->pt.bp, &user->regs.ebp);
		put_user_ex(regs->pt.ax, &user->regs.eax);
		put_user_ex(regs->pt.ip, &user->regs.eip);
		put_user_ex(regs->pt.cs, &user->regs.cs);
		put_user_ex(regs->pt.flags, &user->regs.eflags);
		put_user_ex(regs->pt.sp, &user->regs.esp);
		put_user_ex(regs->pt.ss, &user->regs.ss);
		put_user_ex(regs->es, &user->regs.es);
		put_user_ex(regs->ds, &user->regs.ds);
		put_user_ex(regs->fs, &user->regs.fs);
		put_user_ex(regs->gs, &user->regs.gs);

		put_user_ex(vm86->screen_bitmap, &user->screen_bitmap);
	} put_user_catch(err);
	if (err) {
		pr_alert("could not access userspace vm86 info\n");
		do_exit(SIGSEGV);
	}

	preempt_disable();
	tsk->thread.sp0 = vm86->saved_sp0;
	tsk->thread.sysenter_cs = __KERNEL_CS;
	update_task_stack(tsk);
	refresh_sysenter_cs(&tsk->thread);
	vm86->saved_sp0 = 0;
	preempt_enable();

	memcpy(&regs->pt, &vm86->regs32, sizeof(struct pt_regs));

	lazy_load_gs(vm86->regs32.gs);

	regs->pt.ax = retval;
}

static void mark_screen_rdonly(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	spinlock_t *ptl;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int i;

	down_write(&mm->mmap_sem);
	pgd = pgd_offset(mm, 0xA0000);
	if (pgd_none_or_clear_bad(pgd))
		goto out;
	p4d = p4d_offset(pgd, 0xA0000);
	if (p4d_none_or_clear_bad(p4d))
		goto out;
	pud = pud_offset(p4d, 0xA0000);
	if (pud_none_or_clear_bad(pud))
		goto out;
	pmd = pmd_offset(pud, 0xA0000);

	if (pmd_trans_huge(*pmd)) {
		vma = find_vma(mm, 0xA0000);
		split_huge_pmd(vma, pmd, 0xA0000);
	}
	if (pmd_none_or_clear_bad(pmd))
		goto out;
	pte = pte_offset_map_lock(mm, pmd, 0xA0000, &ptl);
	for (i = 0; i < 32; i++) {
		if (pte_present(*pte))
			set_pte(pte, pte_wrprotect(*pte));
		pte++;
	}
	pte_unmap_unlock(pte, ptl);
out:
	up_write(&mm->mmap_sem);
	flush_tlb_mm_range(mm, 0xA0000, 0xA0000 + 32*PAGE_SIZE, PAGE_SHIFT, false);
}



static int do_vm86_irq_handling(int subfunction, int irqnumber);
static long do_sys_vm86(struct vm86plus_struct __user *user_vm86, bool plus);

SYSCALL_DEFINE1(vm86old, struct vm86_struct __user *, user_vm86)
{
	return do_sys_vm86((struct vm86plus_struct __user *) user_vm86, false);
}


SYSCALL_DEFINE2(vm86, unsigned long, cmd, unsigned long, arg)
{
	switch (cmd) {
	case VM86_REQUEST_IRQ:
	case VM86_FREE_IRQ:
	case VM86_GET_IRQ_BITS:
	case VM86_GET_AND_RESET_IRQ:
		return do_vm86_irq_handling(cmd, (int)arg);
	case VM86_PLUS_INSTALL_CHECK:
		/*
		 * NOTE: on old vm86 stuff this will return the error
		 *  from access_ok(), because the subfunction is
		 *  interpreted as (invalid) address to vm86_struct.
		 *  So the installation check works.
		 */
		return 0;
	}

	/* we come here only for functions VM86_ENTER, VM86_ENTER_NO_BYPASS */
	return do_sys_vm86((struct vm86plus_struct __user *) arg, true);
}


static long do_sys_vm86(struct vm86plus_struct __user *user_vm86, bool plus)
{
	struct task_struct *tsk = current;
	struct vm86 *vm86 = tsk->thread.vm86;
	struct kernel_vm86_regs vm86regs;
	struct pt_regs *regs = current_pt_regs();
	unsigned long err = 0;

	err = security_mmap_addr(0);
	if (err) {
		/*
		 * vm86 cannot virtualize the address space, so vm86 users
		 * need to manage the low 1MB themselves using mmap.  Given
		 * that BIOS places important data in the first page, vm86
		 * is essentially useless if mmap_min_addr != 0.  DOSEMU,
		 * for example, won't even bother trying to use vm86 if it
		 * can't map a page at virtual address 0.
		 *
		 * To reduce the available kernel attack surface, simply
		 * disallow vm86(old) for users who cannot mmap at va 0.
		 *
		 * The implementation of security_mmap_addr will allow
		 * suitably privileged users to map va 0 even if
		 * vm.mmap_min_addr is set above 0, and we want this
		 * behavior for vm86 as well, as it ensures that legacy
		 * tools like vbetool will not fail just because of
		 * vm.mmap_min_addr.
		 */
		pr_info_once("Denied a call to vm86(old) from %s[%d] (uid: %d).  Set the vm.mmap_min_addr sysctl to 0 and/or adjust LSM mmap_min_addr policy to enable vm86 if you are using a vm86-based DOS emulator.\n",
			     current->comm, task_pid_nr(current),
			     from_kuid_munged(&init_user_ns, current_uid()));
		return -EPERM;
	}

	if (!vm86) {
		if (!(vm86 = kzalloc(sizeof(*vm86), GFP_KERNEL)))
			return -ENOMEM;
		tsk->thread.vm86 = vm86;
	}
	if (vm86->saved_sp0)
		return -EPERM;

	if (!access_ok(user_vm86, plus ?
		       sizeof(struct vm86_struct) :
		       sizeof(struct vm86plus_struct)))
		return -EFAULT;

	memset(&vm86regs, 0, sizeof(vm86regs));
	get_user_try {
		unsigned short seg;
		get_user_ex(vm86regs.pt.bx, &user_vm86->regs.ebx);
		get_user_ex(vm86regs.pt.cx, &user_vm86->regs.ecx);
		get_user_ex(vm86regs.pt.dx, &user_vm86->regs.edx);
		get_user_ex(vm86regs.pt.si, &user_vm86->regs.esi);
		get_user_ex(vm86regs.pt.di, &user_vm86->regs.edi);
		get_user_ex(vm86regs.pt.bp, &user_vm86->regs.ebp);
		get_user_ex(vm86regs.pt.ax, &user_vm86->regs.eax);
		get_user_ex(vm86regs.pt.ip, &user_vm86->regs.eip);
		get_user_ex(seg, &user_vm86->regs.cs);
		vm86regs.pt.cs = seg;
		get_user_ex(vm86regs.pt.flags, &user_vm86->regs.eflags);
		get_user_ex(vm86regs.pt.sp, &user_vm86->regs.esp);
		get_user_ex(seg, &user_vm86->regs.ss);
		vm86regs.pt.ss = seg;
		get_user_ex(vm86regs.es, &user_vm86->regs.es);
		get_user_ex(vm86regs.ds, &user_vm86->regs.ds);
		get_user_ex(vm86regs.fs, &user_vm86->regs.fs);
		get_user_ex(vm86regs.gs, &user_vm86->regs.gs);

		get_user_ex(vm86->flags, &user_vm86->flags);
		get_user_ex(vm86->screen_bitmap, &user_vm86->screen_bitmap);
		get_user_ex(vm86->cpu_type, &user_vm86->cpu_type);
	} get_user_catch(err);
	if (err)
		return err;

	if (copy_from_user(&vm86->int_revectored,
			   &user_vm86->int_revectored,
			   sizeof(struct revectored_struct)))
		return -EFAULT;
	if (copy_from_user(&vm86->int21_revectored,
			   &user_vm86->int21_revectored,
			   sizeof(struct revectored_struct)))
		return -EFAULT;
	if (plus) {
		if (copy_from_user(&vm86->vm86plus, &user_vm86->vm86plus,
				   sizeof(struct vm86plus_info_struct)))
			return -EFAULT;
		vm86->vm86plus.is_vm86pus = 1;
	} else
		memset(&vm86->vm86plus, 0,
		       sizeof(struct vm86plus_info_struct));

	memcpy(&vm86->regs32, regs, sizeof(struct pt_regs));
	vm86->user_vm86 = user_vm86;

/*
 * The flags register is also special: we cannot trust that the user
 * has set it up safely, so this makes sure interrupt etc flags are
 * inherited from protected mode.
 */
	VEFLAGS = vm86regs.pt.flags;
	vm86regs.pt.flags &= SAFE_MASK;
	vm86regs.pt.flags |= regs->flags & ~SAFE_MASK;
	vm86regs.pt.flags |= X86_VM_MASK;

	vm86regs.pt.orig_ax = regs->orig_ax;

	switch (vm86->cpu_type) {
	case CPU_286:
		vm86->veflags_mask = 0;
		break;
	case CPU_386:
		vm86->veflags_mask = X86_EFLAGS_NT | X86_EFLAGS_IOPL;
		break;
	case CPU_486:
		vm86->veflags_mask = X86_EFLAGS_AC | X86_EFLAGS_NT | X86_EFLAGS_IOPL;
		break;
	default:
		vm86->veflags_mask = X86_EFLAGS_ID | X86_EFLAGS_AC | X86_EFLAGS_NT | X86_EFLAGS_IOPL;
		break;
	}

/*
 * Save old state
 */
	vm86->saved_sp0 = tsk->thread.sp0;
	lazy_save_gs(vm86->regs32.gs);

	/* make room for real-mode segments */
	preempt_disable();
	tsk->thread.sp0 += 16;

	if (boot_cpu_has(X86_FEATURE_SEP)) {
		tsk->thread.sysenter_cs = 0;
		refresh_sysenter_cs(&tsk->thread);
	}

	update_task_stack(tsk);
	preempt_enable();

	if (vm86->flags & VM86_SCREEN_BITMAP)
		mark_screen_rdonly(tsk->mm);

	memcpy((struct kernel_vm86_regs *)regs, &vm86regs, sizeof(vm86regs));
	return regs->ax;
}

static inline void set_IF(struct kernel_vm86_regs *regs)
{
	VEFLAGS |= X86_EFLAGS_VIF;
}

static inline void clear_IF(struct kernel_vm86_regs *regs)
{
	VEFLAGS &= ~X86_EFLAGS_VIF;
}

static inline void clear_TF(struct kernel_vm86_regs *regs)
{
	regs->pt.flags &= ~X86_EFLAGS_TF;
}

static inline void clear_AC(struct kernel_vm86_regs *regs)
{
	regs->pt.flags &= ~X86_EFLAGS_AC;
}

/*
 * It is correct to call set_IF(regs) from the set_vflags_*
 * functions. However someone forgot to call clear_IF(regs)
 * in the opposite case.
 * After the command sequence CLI PUSHF STI POPF you should
 * end up with interrupts disabled, but you ended up with
 * interrupts enabled.
 *  ( I was testing my own changes, but the only bug I
 *    could find was in a function I had not changed. )
 * [KD]
 */

static inline void set_vflags_long(unsigned long flags, struct kernel_vm86_regs *regs)
{
	set_flags(VEFLAGS, flags, current->thread.vm86->veflags_mask);
	set_flags(regs->pt.flags, flags, SAFE_MASK);
	if (flags & X86_EFLAGS_IF)
		set_IF(regs);
	else
		clear_IF(regs);
}

static inline void set_vflags_short(unsigned short flags, struct kernel_vm86_regs *regs)
{
	set_flags(VFLAGS, flags, current->thread.vm86->veflags_mask);
	set_flags(regs->pt.flags, flags, SAFE_MASK);
	if (flags & X86_EFLAGS_IF)
		set_IF(regs);
	else
		clear_IF(regs);
}

static inline unsigned long get_vflags(struct kernel_vm86_regs *regs)
{
	unsigned long flags = regs->pt.flags & RETURN_MASK;

	if (VEFLAGS & X86_EFLAGS_VIF)
		flags |= X86_EFLAGS_IF;
	flags |= X86_EFLAGS_IOPL;
	return flags | (VEFLAGS & current->thread.vm86->veflags_mask);
}

static inline int is_revectored(int nr, struct revectored_struct *bitmap)
{
	return test_bit(nr, bitmap->__map);
}

#define val_byte(val, n) (((__u8 *)&val)[n])

#define pushb(base, ptr, val, err_label) \
	do { \
		__u8 __val = val; \
		ptr--; \
		if (put_user(__val, base + ptr) < 0) \
			goto err_label; \
	} while (0)

#define pushw(base, ptr, val, err_label) \
	do { \
		__u16 __val = val; \
		ptr--; \
		if (put_user(val_byte(__val, 1), base + ptr) < 0) \
			goto err_label; \
		ptr--; \
		if (put_user(val_byte(__val, 0), base + ptr) < 0) \
			goto err_label; \
	} while (0)

#define pushl(base, ptr, val, err_label) \
	do { \
		__u32 __val = val; \
		ptr--; \
		if (put_user(val_byte(__val, 3), base + ptr) < 0) \
			goto err_label; \
		ptr--; \
		if (put_user(val_byte(__val, 2), base + ptr) < 0) \
			goto err_label; \
		ptr--; \
		if (put_user(val_byte(__val, 1), base + ptr) < 0) \
			goto err_label; \
		ptr--; \
		if (put_user(val_byte(__val, 0), base + ptr) < 0) \
			goto err_label; \
	} while (0)

#define popb(base, ptr, err_label) \
	({ \
		__u8 __res; \
		if (get_user(__res, base + ptr) < 0) \
			goto err_label; \
		ptr++; \
		__res; \
	})

#define popw(base, ptr, err_label) \
	({ \
		__u16 __res; \
		if (get_user(val_byte(__res, 0), base + ptr) < 0) \
			goto err_label; \
		ptr++; \
		if (get_user(val_byte(__res, 1), base + ptr) < 0) \
			goto err_label; \
		ptr++; \
		__res; \
	})

#define popl(base, ptr, err_label) \
	({ \
		__u32 __res; \
		if (get_user(val_byte(__res, 0), base + ptr) < 0) \
			goto err_label; \
		ptr++; \
		if (get_user(val_byte(__res, 1), base + ptr) < 0) \
			goto err_label; \
		ptr++; \
		if (get_user(val_byte(__res, 2), base + ptr) < 0) \
			goto err_label; \
		ptr++; \
		if (get_user(val_byte(__res, 3), base + ptr) < 0) \
			goto err_label; \
		ptr++; \
		__res; \
	})

/* There are so many possible reasons for this function to return
 * VM86_INTx, so adding another doesn't bother me. We can expect
 * userspace programs to be able to handle it. (Getting a problem
 * in userspace is always better than an Oops anyway.) [KD]
 */
static void do_int(struct kernel_vm86_regs *regs, int i,
    unsigned char __user *ssp, unsigned short sp)
{
	unsigned long __user *intr_ptr;
	unsigned long segoffs;
	struct vm86 *vm86 = current->thread.vm86;

	if (regs->pt.cs == BIOSSEG)
		goto cannot_handle;
	if (is_revectored(i, &vm86->int_revectored))
		goto cannot_handle;
	if (i == 0x21 && is_revectored(AH(regs), &vm86->int21_revectored))
		goto cannot_handle;
	intr_ptr = (unsigned long __user *) (i << 2);
	if (get_user(segoffs, intr_ptr))
		goto cannot_handle;
	if ((segoffs >> 16) == BIOSSEG)
		goto cannot_handle;
	pushw(ssp, sp, get_vflags(regs), cannot_handle);
	pushw(ssp, sp, regs->pt.cs, cannot_handle);
	pushw(ssp, sp, IP(regs), cannot_handle);
	regs->pt.cs = segoffs >> 16;
	SP(regs) -= 6;
	IP(regs) = segoffs & 0xffff;
	clear_TF(regs);
	clear_IF(regs);
	clear_AC(regs);
	return;

cannot_handle:
	save_v86_state(regs, VM86_INTx + (i << 8));
}

int handle_vm86_trap(struct kernel_vm86_regs *regs, long error_code, int trapno)
{
	struct vm86 *vm86 = current->thread.vm86;

	if (vm86->vm86plus.is_vm86pus) {
		if ((trapno == 3) || (trapno == 1)) {
			save_v86_state(regs, VM86_TRAP + (trapno << 8));
			return 0;
		}
		do_int(regs, trapno, (unsigned char __user *) (regs->pt.ss << 4), SP(regs));
		return 0;
	}
	if (trapno != 1)
		return 1; /* we let this handle by the calling routine */
	current->thread.trap_nr = trapno;
	current->thread.error_code = error_code;
	force_sig(SIGTRAP);
	return 0;
}

void handle_vm86_fault(struct kernel_vm86_regs *regs, long error_code)
{
	unsigned char opcode;
	unsigned char __user *csp;
	unsigned char __user *ssp;
	unsigned short ip, sp, orig_flags;
	int data32, pref_done;
	struct vm86plus_info_struct *vmpi = &current->thread.vm86->vm86plus;

#define CHECK_IF_IN_TRAP \
	if (vmpi->vm86dbg_active && vmpi->vm86dbg_TFpendig) \
		newflags |= X86_EFLAGS_TF

	orig_flags = *(unsigned short *)&regs->pt.flags;

	csp = (unsigned char __user *) (regs->pt.cs << 4);
	ssp = (unsigned char __user *) (regs->pt.ss << 4);
	sp = SP(regs);
	ip = IP(regs);

	data32 = 0;
	pref_done = 0;
	do {
		switch (opcode = popb(csp, ip, simulate_sigsegv)) {
		case 0x66:      /* 32-bit data */     data32 = 1; break;
		case 0x67:      /* 32-bit address */  break;
		case 0x2e:      /* CS */              break;
		case 0x3e:      /* DS */              break;
		case 0x26:      /* ES */              break;
		case 0x36:      /* SS */              break;
		case 0x65:      /* GS */              break;
		case 0x64:      /* FS */              break;
		case 0xf2:      /* repnz */       break;
		case 0xf3:      /* rep */             break;
		default: pref_done = 1;
		}
	} while (!pref_done);

	switch (opcode) {

	/* pushf */
	case 0x9c:
		if (data32) {
			pushl(ssp, sp, get_vflags(regs), simulate_sigsegv);
			SP(regs) -= 4;
		} else {
			pushw(ssp, sp, get_vflags(regs), simulate_sigsegv);
			SP(regs) -= 2;
		}
		IP(regs) = ip;
		goto vm86_fault_return;

	/* popf */
	case 0x9d:
		{
		unsigned long newflags;
		if (data32) {
			newflags = popl(ssp, sp, simulate_sigsegv);
			SP(regs) += 4;
		} else {
			newflags = popw(ssp, sp, simulate_sigsegv);
			SP(regs) += 2;
		}
		IP(regs) = ip;
		CHECK_IF_IN_TRAP;
		if (data32)
			set_vflags_long(newflags, regs);
		else
			set_vflags_short(newflags, regs);

		goto check_vip;
		}

	/* int xx */
	case 0xcd: {
		int intno = popb(csp, ip, simulate_sigsegv);
		IP(regs) = ip;
		if (vmpi->vm86dbg_active) {
			if ((1 << (intno & 7)) & vmpi->vm86dbg_intxxtab[intno >> 3]) {
				save_v86_state(regs, VM86_INTx + (intno << 8));
				return;
			}
		}
		do_int(regs, intno, ssp, sp);
		return;
	}

	/* iret */
	case 0xcf:
		{
		unsigned long newip;
		unsigned long newcs;
		unsigned long newflags;
		if (data32) {
			newip = popl(ssp, sp, simulate_sigsegv);
			newcs = popl(ssp, sp, simulate_sigsegv);
			newflags = popl(ssp, sp, simulate_sigsegv);
			SP(regs) += 12;
		} else {
			newip = popw(ssp, sp, simulate_sigsegv);
			newcs = popw(ssp, sp, simulate_sigsegv);
			newflags = popw(ssp, sp, simulate_sigsegv);
			SP(regs) += 6;
		}
		IP(regs) = newip;
		regs->pt.cs = newcs;
		CHECK_IF_IN_TRAP;
		if (data32) {
			set_vflags_long(newflags, regs);
		} else {
			set_vflags_short(newflags, regs);
		}
		goto check_vip;
		}

	/* cli */
	case 0xfa:
		IP(regs) = ip;
		clear_IF(regs);
		goto vm86_fault_return;

	/* sti */
	/*
	 * Damn. This is incorrect: the 'sti' instruction should actually
	 * enable interrupts after the /next/ instruction. Not good.
	 *
	 * Probably needs some horsing around with the TF flag. Aiee..
	 */
	case 0xfb:
		IP(regs) = ip;
		set_IF(regs);
		goto check_vip;

	default:
		save_v86_state(regs, VM86_UNKNOWN);
	}

	return;

check_vip:
	if ((VEFLAGS & (X86_EFLAGS_VIP | X86_EFLAGS_VIF)) ==
	    (X86_EFLAGS_VIP | X86_EFLAGS_VIF)) {
		save_v86_state(regs, VM86_STI);
		return;
	}

vm86_fault_return:
	if (vmpi->force_return_for_pic  && (VEFLAGS & (X86_EFLAGS_IF | X86_EFLAGS_VIF))) {
		save_v86_state(regs, VM86_PICRETURN);
		return;
	}
	if (orig_flags & X86_EFLAGS_TF)
		handle_vm86_trap(regs, 0, X86_TRAP_DB);
	return;

simulate_sigsegv:
	/* FIXME: After a long discussion with Stas we finally
	 *        agreed, that this is wrong. Here we should
	 *        really send a SIGSEGV to the user program.
	 *        But how do we create the correct context? We
	 *        are inside a general protection fault handler
	 *        and has just returned from a page fault handler.
	 *        The correct context for the signal handler
	 *        should be a mixture of the two, but how do we
	 *        get the information? [KD]
	 */
	save_v86_state(regs, VM86_UNKNOWN);
}

/* ---------------- vm86 special IRQ passing stuff ----------------- */

#define VM86_IRQNAME		"vm86irq"

static struct vm86_irqs {
	struct task_struct *tsk;
	int sig;
} vm86_irqs[16];

static DEFINE_SPINLOCK(irqbits_lock);
static int irqbits;

#define ALLOWED_SIGS (1 /* 0 = don't send a signal */ \
	| (1 << SIGUSR1) | (1 << SIGUSR2) | (1 << SIGIO)  | (1 << SIGURG) \
	| (1 << SIGUNUSED))

static irqreturn_t irq_handler(int intno, void *dev_id)
{
	int irq_bit;
	unsigned long flags;

	spin_lock_irqsave(&irqbits_lock, flags);
	irq_bit = 1 << intno;
	if ((irqbits & irq_bit) || !vm86_irqs[intno].tsk)
		goto out;
	irqbits |= irq_bit;
	if (vm86_irqs[intno].sig)
		send_sig(vm86_irqs[intno].sig, vm86_irqs[intno].tsk, 1);
	/*
	 * IRQ will be re-enabled when user asks for the irq (whether
	 * polling or as a result of the signal)
	 */
	disable_irq_nosync(intno);
	spin_unlock_irqrestore(&irqbits_lock, flags);
	return IRQ_HANDLED;

out:
	spin_unlock_irqrestore(&irqbits_lock, flags);
	return IRQ_NONE;
}

static inline void free_vm86_irq(int irqnumber)
{
	unsigned long flags;

	free_irq(irqnumber, NULL);
	vm86_irqs[irqnumber].tsk = NULL;

	spin_lock_irqsave(&irqbits_lock, flags);
	irqbits &= ~(1 << irqnumber);
	spin_unlock_irqrestore(&irqbits_lock, flags);
}

void release_vm86_irqs(struct task_struct *task)
{
	int i;
	for (i = FIRST_VM86_IRQ ; i <= LAST_VM86_IRQ; i++)
	    if (vm86_irqs[i].tsk == task)
		free_vm86_irq(i);
}

static inline int get_and_reset_irq(int irqnumber)
{
	int bit;
	unsigned long flags;
	int ret = 0;

	if (invalid_vm86_irq(irqnumber)) return 0;
	if (vm86_irqs[irqnumber].tsk != current) return 0;
	spin_lock_irqsave(&irqbits_lock, flags);
	bit = irqbits & (1 << irqnumber);
	irqbits &= ~bit;
	if (bit) {
		enable_irq(irqnumber);
		ret = 1;
	}

	spin_unlock_irqrestore(&irqbits_lock, flags);
	return ret;
}


static int do_vm86_irq_handling(int subfunction, int irqnumber)
{
	int ret;
	switch (subfunction) {
		case VM86_GET_AND_RESET_IRQ: {
			return get_and_reset_irq(irqnumber);
		}
		case VM86_GET_IRQ_BITS: {
			return irqbits;
		}
		case VM86_REQUEST_IRQ: {
			int sig = irqnumber >> 8;
			int irq = irqnumber & 255;
			if (!capable(CAP_SYS_ADMIN)) return -EPERM;
			if (!((1 << sig) & ALLOWED_SIGS)) return -EPERM;
			if (invalid_vm86_irq(irq)) return -EPERM;
			if (vm86_irqs[irq].tsk) return -EPERM;
			ret = request_irq(irq, &irq_handler, 0, VM86_IRQNAME, NULL);
			if (ret) return ret;
			vm86_irqs[irq].sig = sig;
			vm86_irqs[irq].tsk = current;
			return irq;
		}
		case  VM86_FREE_IRQ: {
			if (invalid_vm86_irq(irqnumber)) return -EPERM;
			if (!vm86_irqs[irqnumber].tsk) return 0;
			if (vm86_irqs[irqnumber].tsk != current) return -EPERM;
			free_vm86_irq(irqnumber);
			return 0;
		}
	}
	return -EINVAL;
}

