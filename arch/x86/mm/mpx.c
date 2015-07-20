/*
 * mpx.c - Memory Protection eXtensions
 *
 * Copyright (c) 2014, Intel Corporation.
 * Qiaowei Ren <qiaowei.ren@intel.com>
 * Dave Hansen <dave.hansen@intel.com>
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/sched/sysctl.h>

#include <asm/i387.h>
#include <asm/insn.h>
#include <asm/mman.h>
#include <asm/mmu_context.h>
#include <asm/mpx.h>
#include <asm/processor.h>
#include <asm/fpu-internal.h>

/*
 * This is really a simplified "vm_mmap". it only handles MPX
 * bounds tables (the bounds directory is user-allocated).
 */
static unsigned long mpx_mmap(unsigned long len)
{
	unsigned long ret;
	unsigned long addr, pgoff;
	struct mm_struct *mm = current->mm;
	vm_flags_t vm_flags;
	struct vm_area_struct *vma;

	/* Only bounds table and bounds directory can be allocated here */
	if (len != MPX_BD_SIZE_BYTES && len != MPX_BT_SIZE_BYTES)
		return -EINVAL;

	down_write(&mm->mmap_sem);

	/* Too many mappings? */
	if (mm->map_count > sysctl_max_map_count) {
		ret = -ENOMEM;
		goto out;
	}

	/* Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */
	addr = get_unmapped_area(NULL, 0, len, 0, MAP_ANONYMOUS | MAP_PRIVATE);
	if (addr & ~PAGE_MASK) {
		ret = addr;
		goto out;
	}

	vm_flags = VM_READ | VM_WRITE | VM_MPX |
			mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	/* Set pgoff according to addr for anon_vma */
	pgoff = addr >> PAGE_SHIFT;

	ret = mmap_region(NULL, addr, len, vm_flags, pgoff);
	if (IS_ERR_VALUE(ret))
		goto out;

	vma = find_vma(mm, ret);
	if (!vma) {
		ret = -ENOMEM;
		goto out;
	}

	if (vm_flags & VM_LOCKED) {
		up_write(&mm->mmap_sem);
		mm_populate(ret, len);
		return ret;
	}

out:
	up_write(&mm->mmap_sem);
	return ret;
}

enum reg_type {
	REG_TYPE_RM = 0,
	REG_TYPE_INDEX,
	REG_TYPE_BASE,
};

static int get_reg_offset(struct insn *insn, struct pt_regs *regs,
			  enum reg_type type)
{
	int regno = 0;

	static const int regoff[] = {
		offsetof(struct pt_regs, ax),
		offsetof(struct pt_regs, cx),
		offsetof(struct pt_regs, dx),
		offsetof(struct pt_regs, bx),
		offsetof(struct pt_regs, sp),
		offsetof(struct pt_regs, bp),
		offsetof(struct pt_regs, si),
		offsetof(struct pt_regs, di),
#ifdef CONFIG_X86_64
		offsetof(struct pt_regs, r8),
		offsetof(struct pt_regs, r9),
		offsetof(struct pt_regs, r10),
		offsetof(struct pt_regs, r11),
		offsetof(struct pt_regs, r12),
		offsetof(struct pt_regs, r13),
		offsetof(struct pt_regs, r14),
		offsetof(struct pt_regs, r15),
#endif
	};
	int nr_registers = ARRAY_SIZE(regoff);
	/*
	 * Don't possibly decode a 32-bit instructions as
	 * reading a 64-bit-only register.
	 */
	if (IS_ENABLED(CONFIG_X86_64) && !insn->x86_64)
		nr_registers -= 8;

	switch (type) {
	case REG_TYPE_RM:
		regno = X86_MODRM_RM(insn->modrm.value);
		if (X86_REX_B(insn->rex_prefix.value) == 1)
			regno += 8;
		break;

	case REG_TYPE_INDEX:
		regno = X86_SIB_INDEX(insn->sib.value);
		if (X86_REX_X(insn->rex_prefix.value) == 1)
			regno += 8;
		break;

	case REG_TYPE_BASE:
		regno = X86_SIB_BASE(insn->sib.value);
		if (X86_REX_B(insn->rex_prefix.value) == 1)
			regno += 8;
		break;

	default:
		pr_err("invalid register type");
		BUG();
		break;
	}

	if (regno > nr_registers) {
		WARN_ONCE(1, "decoded an instruction with an invalid register");
		return -EINVAL;
	}
	return regoff[regno];
}

/*
 * return the address being referenced be instruction
 * for rm=3 returning the content of the rm reg
 * for rm!=3 calculates the address using SIB and Disp
 */
static void __user *mpx_get_addr_ref(struct insn *insn, struct pt_regs *regs)
{
	unsigned long addr, base, indx;
	int addr_offset, base_offset, indx_offset;
	insn_byte_t sib;

	insn_get_modrm(insn);
	insn_get_sib(insn);
	sib = insn->sib.value;

	if (X86_MODRM_MOD(insn->modrm.value) == 3) {
		addr_offset = get_reg_offset(insn, regs, REG_TYPE_RM);
		if (addr_offset < 0)
			goto out_err;
		addr = regs_get_register(regs, addr_offset);
	} else {
		if (insn->sib.nbytes) {
			base_offset = get_reg_offset(insn, regs, REG_TYPE_BASE);
			if (base_offset < 0)
				goto out_err;

			indx_offset = get_reg_offset(insn, regs, REG_TYPE_INDEX);
			if (indx_offset < 0)
				goto out_err;

			base = regs_get_register(regs, base_offset);
			indx = regs_get_register(regs, indx_offset);
			addr = base + indx * (1 << X86_SIB_SCALE(sib));
		} else {
			addr_offset = get_reg_offset(insn, regs, REG_TYPE_RM);
			if (addr_offset < 0)
				goto out_err;
			addr = regs_get_register(regs, addr_offset);
		}
		addr += insn->displacement.value;
	}
	return (void __user *)addr;
out_err:
	return (void __user *)-1;
}

static int mpx_insn_decode(struct insn *insn,
			   struct pt_regs *regs)
{
	unsigned char buf[MAX_INSN_SIZE];
	int x86_64 = !test_thread_flag(TIF_IA32);
	int not_copied;
	int nr_copied;

	not_copied = copy_from_user(buf, (void __user *)regs->ip, sizeof(buf));
	nr_copied = sizeof(buf) - not_copied;
	/*
	 * The decoder _should_ fail nicely if we pass it a short buffer.
	 * But, let's not depend on that implementation detail.  If we
	 * did not get anything, just error out now.
	 */
	if (!nr_copied)
		return -EFAULT;
	insn_init(insn, buf, nr_copied, x86_64);
	insn_get_length(insn);
	/*
	 * copy_from_user() tries to get as many bytes as we could see in
	 * the largest possible instruction.  If the instruction we are
	 * after is shorter than that _and_ we attempt to copy from
	 * something unreadable, we might get a short read.  This is OK
	 * as long as the read did not stop in the middle of the
	 * instruction.  Check to see if we got a partial instruction.
	 */
	if (nr_copied < insn->length)
		return -EFAULT;

	insn_get_opcode(insn);
	/*
	 * We only _really_ need to decode bndcl/bndcn/bndcu
	 * Error out on anything else.
	 */
	if (insn->opcode.bytes[0] != 0x0f)
		goto bad_opcode;
	if ((insn->opcode.bytes[1] != 0x1a) &&
	    (insn->opcode.bytes[1] != 0x1b))
		goto bad_opcode;

	return 0;
bad_opcode:
	return -EINVAL;
}

/*
 * If a bounds overflow occurs then a #BR is generated. This
 * function decodes MPX instructions to get violation address
 * and set this address into extended struct siginfo.
 *
 * Note that this is not a super precise way of doing this.
 * Userspace could have, by the time we get here, written
 * anything it wants in to the instructions.  We can not
 * trust anything about it.  They might not be valid
 * instructions or might encode invalid registers, etc...
 *
 * The caller is expected to kfree() the returned siginfo_t.
 */
siginfo_t *mpx_generate_siginfo(struct pt_regs *regs,
				struct xsave_struct *xsave_buf)
{
	struct bndreg *bndregs, *bndreg;
	siginfo_t *info = NULL;
	struct insn insn;
	uint8_t bndregno;
	int err;

	err = mpx_insn_decode(&insn, regs);
	if (err)
		goto err_out;

	/*
	 * We know at this point that we are only dealing with
	 * MPX instructions.
	 */
	insn_get_modrm(&insn);
	bndregno = X86_MODRM_REG(insn.modrm.value);
	if (bndregno > 3) {
		err = -EINVAL;
		goto err_out;
	}
	/* get the bndregs _area_ of the xsave structure */
	bndregs = get_xsave_addr(xsave_buf, XSTATE_BNDREGS);
	if (!bndregs) {
		err = -EINVAL;
		goto err_out;
	}
	/* now go select the individual register in the set of 4 */
	bndreg = &bndregs[bndregno];

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto err_out;
	}
	/*
	 * The registers are always 64-bit, but the upper 32
	 * bits are ignored in 32-bit mode.  Also, note that the
	 * upper bounds are architecturally represented in 1's
	 * complement form.
	 *
	 * The 'unsigned long' cast is because the compiler
	 * complains when casting from integers to different-size
	 * pointers.
	 */
	info->si_lower = (void __user *)(unsigned long)bndreg->lower_bound;
	info->si_upper = (void __user *)(unsigned long)~bndreg->upper_bound;
	info->si_addr_lsb = 0;
	info->si_signo = SIGSEGV;
	info->si_errno = 0;
	info->si_code = SEGV_BNDERR;
	info->si_addr = mpx_get_addr_ref(&insn, regs);
	/*
	 * We were not able to extract an address from the instruction,
	 * probably because there was something invalid in it.
	 */
	if (info->si_addr == (void *)-1) {
		err = -EINVAL;
		goto err_out;
	}
	return info;
err_out:
	/* info might be NULL, but kfree() handles that */
	kfree(info);
	return ERR_PTR(err);
}

static __user void *task_get_bounds_dir(struct task_struct *tsk)
{
	struct bndcsr *bndcsr;

	if (!cpu_feature_enabled(X86_FEATURE_MPX))
		return MPX_INVALID_BOUNDS_DIR;

	/*
	 * 32-bit binaries on 64-bit kernels are currently
	 * unsupported.
	 */
	if (IS_ENABLED(CONFIG_X86_64) && test_thread_flag(TIF_IA32))
		return MPX_INVALID_BOUNDS_DIR;
	/*
	 * The bounds directory pointer is stored in a register
	 * only accessible if we first do an xsave.
	 */
	fpu_save_init(&tsk->thread.fpu);
	bndcsr = get_xsave_addr(&tsk->thread.fpu.state->xsave, XSTATE_BNDCSR);
	if (!bndcsr)
		return MPX_INVALID_BOUNDS_DIR;

	/*
	 * Make sure the register looks valid by checking the
	 * enable bit.
	 */
	if (!(bndcsr->bndcfgu & MPX_BNDCFG_ENABLE_FLAG))
		return MPX_INVALID_BOUNDS_DIR;

	/*
	 * Lastly, mask off the low bits used for configuration
	 * flags, and return the address of the bounds table.
	 */
	return (void __user *)(unsigned long)
		(bndcsr->bndcfgu & MPX_BNDCFG_ADDR_MASK);
}

int mpx_enable_management(struct task_struct *tsk)
{
	void __user *bd_base = MPX_INVALID_BOUNDS_DIR;
	struct mm_struct *mm = tsk->mm;
	int ret = 0;

	/*
	 * runtime in the userspace will be responsible for allocation of
	 * the bounds directory. Then, it will save the base of the bounds
	 * directory into XSAVE/XRSTOR Save Area and enable MPX through
	 * XRSTOR instruction.
	 *
	 * fpu_xsave() is expected to be very expensive. Storing the bounds
	 * directory here means that we do not have to do xsave in the unmap
	 * path; we can just use mm->bd_addr instead.
	 */
	bd_base = task_get_bounds_dir(tsk);
	down_write(&mm->mmap_sem);
	mm->bd_addr = bd_base;
	if (mm->bd_addr == MPX_INVALID_BOUNDS_DIR)
		ret = -ENXIO;

	up_write(&mm->mmap_sem);
	return ret;
}

int mpx_disable_management(struct task_struct *tsk)
{
	struct mm_struct *mm = current->mm;

	if (!cpu_feature_enabled(X86_FEATURE_MPX))
		return -ENXIO;

	down_write(&mm->mmap_sem);
	mm->bd_addr = MPX_INVALID_BOUNDS_DIR;
	up_write(&mm->mmap_sem);
	return 0;
}

/*
 * With 32-bit mode, MPX_BT_SIZE_BYTES is 4MB, and the size of each
 * bounds table is 16KB. With 64-bit mode, MPX_BT_SIZE_BYTES is 2GB,
 * and the size of each bounds table is 4MB.
 */
static int allocate_bt(long __user *bd_entry)
{
	unsigned long expected_old_val = 0;
	unsigned long actual_old_val = 0;
	unsigned long bt_addr;
	int ret = 0;

	/*
	 * Carve the virtual space out of userspace for the new
	 * bounds table:
	 */
	bt_addr = mpx_mmap(MPX_BT_SIZE_BYTES);
	if (IS_ERR((void *)bt_addr))
		return PTR_ERR((void *)bt_addr);
	/*
	 * Set the valid flag (kinda like _PAGE_PRESENT in a pte)
	 */
	bt_addr = bt_addr | MPX_BD_ENTRY_VALID_FLAG;

	/*
	 * Go poke the address of the new bounds table in to the
	 * bounds directory entry out in userspace memory.  Note:
	 * we may race with another CPU instantiating the same table.
	 * In that case the cmpxchg will see an unexpected
	 * 'actual_old_val'.
	 *
	 * This can fault, but that's OK because we do not hold
	 * mmap_sem at this point, unlike some of the other part
	 * of the MPX code that have to pagefault_disable().
	 */
	ret = user_atomic_cmpxchg_inatomic(&actual_old_val, bd_entry,
					   expected_old_val, bt_addr);
	if (ret)
		goto out_unmap;

	/*
	 * The user_atomic_cmpxchg_inatomic() will only return nonzero
	 * for faults, *not* if the cmpxchg itself fails.  Now we must
	 * verify that the cmpxchg itself completed successfully.
	 */
	/*
	 * We expected an empty 'expected_old_val', but instead found
	 * an apparently valid entry.  Assume we raced with another
	 * thread to instantiate this table and desclare succecss.
	 */
	if (actual_old_val & MPX_BD_ENTRY_VALID_FLAG) {
		ret = 0;
		goto out_unmap;
	}
	/*
	 * We found a non-empty bd_entry but it did not have the
	 * VALID_FLAG set.  Return an error which will result in
	 * a SEGV since this probably means that somebody scribbled
	 * some invalid data in to a bounds table.
	 */
	if (expected_old_val != actual_old_val) {
		ret = -EINVAL;
		goto out_unmap;
	}
	return 0;
out_unmap:
	vm_munmap(bt_addr & MPX_BT_ADDR_MASK, MPX_BT_SIZE_BYTES);
	return ret;
}

/*
 * When a BNDSTX instruction attempts to save bounds to a bounds
 * table, it will first attempt to look up the table in the
 * first-level bounds directory.  If it does not find a table in
 * the directory, a #BR is generated and we get here in order to
 * allocate a new table.
 *
 * With 32-bit mode, the size of BD is 4MB, and the size of each
 * bound table is 16KB. With 64-bit mode, the size of BD is 2GB,
 * and the size of each bound table is 4MB.
 */
static int do_mpx_bt_fault(struct xsave_struct *xsave_buf)
{
	unsigned long bd_entry, bd_base;
	struct bndcsr *bndcsr;

	bndcsr = get_xsave_addr(xsave_buf, XSTATE_BNDCSR);
	if (!bndcsr)
		return -EINVAL;
	/*
	 * Mask off the preserve and enable bits
	 */
	bd_base = bndcsr->bndcfgu & MPX_BNDCFG_ADDR_MASK;
	/*
	 * The hardware provides the address of the missing or invalid
	 * entry via BNDSTATUS, so we don't have to go look it up.
	 */
	bd_entry = bndcsr->bndstatus & MPX_BNDSTA_ADDR_MASK;
	/*
	 * Make sure the directory entry is within where we think
	 * the directory is.
	 */
	if ((bd_entry < bd_base) ||
	    (bd_entry >= bd_base + MPX_BD_SIZE_BYTES))
		return -EINVAL;

	return allocate_bt((long __user *)bd_entry);
}

int mpx_handle_bd_fault(struct xsave_struct *xsave_buf)
{
	/*
	 * Userspace never asked us to manage the bounds tables,
	 * so refuse to help.
	 */
	if (!kernel_managing_mpx_tables(current->mm))
		return -EINVAL;

	if (do_mpx_bt_fault(xsave_buf)) {
		force_sig(SIGSEGV, current);
		/*
		 * The force_sig() is essentially "handling" this
		 * exception, so we do not pass up the error
		 * from do_mpx_bt_fault().
		 */
	}
	return 0;
}

/*
 * A thin wrapper around get_user_pages().  Returns 0 if the
 * fault was resolved or -errno if not.
 */
static int mpx_resolve_fault(long __user *addr, int write)
{
	long gup_ret;
	int nr_pages = 1;
	int force = 0;

	gup_ret = get_user_pages(current, current->mm, (unsigned long)addr,
				 nr_pages, write, force, NULL, NULL);
	/*
	 * get_user_pages() returns number of pages gotten.
	 * 0 means we failed to fault in and get anything,
	 * probably because 'addr' is bad.
	 */
	if (!gup_ret)
		return -EFAULT;
	/* Other error, return it */
	if (gup_ret < 0)
		return gup_ret;
	/* must have gup'd a page and gup_ret>0, success */
	return 0;
}

/*
 * Get the base of bounds tables pointed by specific bounds
 * directory entry.
 */
static int get_bt_addr(struct mm_struct *mm,
			long __user *bd_entry, unsigned long *bt_addr)
{
	int ret;
	int valid_bit;

	if (!access_ok(VERIFY_READ, (bd_entry), sizeof(*bd_entry)))
		return -EFAULT;

	while (1) {
		int need_write = 0;

		pagefault_disable();
		ret = get_user(*bt_addr, bd_entry);
		pagefault_enable();
		if (!ret)
			break;
		if (ret == -EFAULT)
			ret = mpx_resolve_fault(bd_entry, need_write);
		/*
		 * If we could not resolve the fault, consider it
		 * userspace's fault and error out.
		 */
		if (ret)
			return ret;
	}

	valid_bit = *bt_addr & MPX_BD_ENTRY_VALID_FLAG;
	*bt_addr &= MPX_BT_ADDR_MASK;

	/*
	 * When the kernel is managing bounds tables, a bounds directory
	 * entry will either have a valid address (plus the valid bit)
	 * *OR* be completely empty. If we see a !valid entry *and* some
	 * data in the address field, we know something is wrong. This
	 * -EINVAL return will cause a SIGSEGV.
	 */
	if (!valid_bit && *bt_addr)
		return -EINVAL;
	/*
	 * Do we have an completely zeroed bt entry?  That is OK.  It
	 * just means there was no bounds table for this memory.  Make
	 * sure to distinguish this from -EINVAL, which will cause
	 * a SEGV.
	 */
	if (!valid_bit)
		return -ENOENT;

	return 0;
}

/*
 * Free the backing physical pages of bounds table 'bt_addr'.
 * Assume start...end is within that bounds table.
 */
static int zap_bt_entries(struct mm_struct *mm,
		unsigned long bt_addr,
		unsigned long start, unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long addr, len;

	/*
	 * Find the first overlapping vma. If vma->vm_start > start, there
	 * will be a hole in the bounds table. This -EINVAL return will
	 * cause a SIGSEGV.
	 */
	vma = find_vma(mm, start);
	if (!vma || vma->vm_start > start)
		return -EINVAL;

	/*
	 * A NUMA policy on a VM_MPX VMA could cause this bouds table to
	 * be split. So we need to look across the entire 'start -> end'
	 * range of this bounds table, find all of the VM_MPX VMAs, and
	 * zap only those.
	 */
	addr = start;
	while (vma && vma->vm_start < end) {
		/*
		 * We followed a bounds directory entry down
		 * here.  If we find a non-MPX VMA, that's bad,
		 * so stop immediately and return an error.  This
		 * probably results in a SIGSEGV.
		 */
		if (!(vma->vm_flags & VM_MPX))
			return -EINVAL;

		len = min(vma->vm_end, end) - addr;
		zap_page_range(vma, addr, len, NULL);

		vma = vma->vm_next;
		addr = vma->vm_start;
	}

	return 0;
}

static int unmap_single_bt(struct mm_struct *mm,
		long __user *bd_entry, unsigned long bt_addr)
{
	unsigned long expected_old_val = bt_addr | MPX_BD_ENTRY_VALID_FLAG;
	unsigned long actual_old_val = 0;
	int ret;

	while (1) {
		int need_write = 1;

		pagefault_disable();
		ret = user_atomic_cmpxchg_inatomic(&actual_old_val, bd_entry,
						   expected_old_val, 0);
		pagefault_enable();
		if (!ret)
			break;
		if (ret == -EFAULT)
			ret = mpx_resolve_fault(bd_entry, need_write);
		/*
		 * If we could not resolve the fault, consider it
		 * userspace's fault and error out.
		 */
		if (ret)
			return ret;
	}
	/*
	 * The cmpxchg was performed, check the results.
	 */
	if (actual_old_val != expected_old_val) {
		/*
		 * Someone else raced with us to unmap the table.
		 * There was no bounds table pointed to by the
		 * directory, so declare success.  Somebody freed
		 * it.
		 */
		if (!actual_old_val)
			return 0;
		/*
		 * Something messed with the bounds directory
		 * entry.  We hold mmap_sem for read or write
		 * here, so it could not be a _new_ bounds table
		 * that someone just allocated.  Something is
		 * wrong, so pass up the error and SIGSEGV.
		 */
		return -EINVAL;
	}

	/*
	 * Note, we are likely being called under do_munmap() already. To
	 * avoid recursion, do_munmap() will check whether it comes
	 * from one bounds table through VM_MPX flag.
	 */
	return do_munmap(mm, bt_addr, MPX_BT_SIZE_BYTES);
}

/*
 * If the bounds table pointed by bounds directory 'bd_entry' is
 * not shared, unmap this whole bounds table. Otherwise, only free
 * those backing physical pages of bounds table entries covered
 * in this virtual address region start...end.
 */
static int unmap_shared_bt(struct mm_struct *mm,
		long __user *bd_entry, unsigned long start,
		unsigned long end, bool prev_shared, bool next_shared)
{
	unsigned long bt_addr;
	int ret;

	ret = get_bt_addr(mm, bd_entry, &bt_addr);
	/*
	 * We could see an "error" ret for not-present bounds
	 * tables (not really an error), or actual errors, but
	 * stop unmapping either way.
	 */
	if (ret)
		return ret;

	if (prev_shared && next_shared)
		ret = zap_bt_entries(mm, bt_addr,
				bt_addr+MPX_GET_BT_ENTRY_OFFSET(start),
				bt_addr+MPX_GET_BT_ENTRY_OFFSET(end));
	else if (prev_shared)
		ret = zap_bt_entries(mm, bt_addr,
				bt_addr+MPX_GET_BT_ENTRY_OFFSET(start),
				bt_addr+MPX_BT_SIZE_BYTES);
	else if (next_shared)
		ret = zap_bt_entries(mm, bt_addr, bt_addr,
				bt_addr+MPX_GET_BT_ENTRY_OFFSET(end));
	else
		ret = unmap_single_bt(mm, bd_entry, bt_addr);

	return ret;
}

/*
 * A virtual address region being munmap()ed might share bounds table
 * with adjacent VMAs. We only need to free the backing physical
 * memory of these shared bounds tables entries covered in this virtual
 * address region.
 */
static int unmap_edge_bts(struct mm_struct *mm,
		unsigned long start, unsigned long end)
{
	int ret;
	long __user *bde_start, *bde_end;
	struct vm_area_struct *prev, *next;
	bool prev_shared = false, next_shared = false;

	bde_start = mm->bd_addr + MPX_GET_BD_ENTRY_OFFSET(start);
	bde_end = mm->bd_addr + MPX_GET_BD_ENTRY_OFFSET(end-1);

	/*
	 * Check whether bde_start and bde_end are shared with adjacent
	 * VMAs.
	 *
	 * We already unliked the VMAs from the mm's rbtree so 'start'
	 * is guaranteed to be in a hole. This gets us the first VMA
	 * before the hole in to 'prev' and the next VMA after the hole
	 * in to 'next'.
	 */
	next = find_vma_prev(mm, start, &prev);
	if (prev && (mm->bd_addr + MPX_GET_BD_ENTRY_OFFSET(prev->vm_end-1))
			== bde_start)
		prev_shared = true;
	if (next && (mm->bd_addr + MPX_GET_BD_ENTRY_OFFSET(next->vm_start))
			== bde_end)
		next_shared = true;

	/*
	 * This virtual address region being munmap()ed is only
	 * covered by one bounds table.
	 *
	 * In this case, if this table is also shared with adjacent
	 * VMAs, only part of the backing physical memory of the bounds
	 * table need be freeed. Otherwise the whole bounds table need
	 * be unmapped.
	 */
	if (bde_start == bde_end) {
		return unmap_shared_bt(mm, bde_start, start, end,
				prev_shared, next_shared);
	}

	/*
	 * If more than one bounds tables are covered in this virtual
	 * address region being munmap()ed, we need to separately check
	 * whether bde_start and bde_end are shared with adjacent VMAs.
	 */
	ret = unmap_shared_bt(mm, bde_start, start, end, prev_shared, false);
	if (ret)
		return ret;
	ret = unmap_shared_bt(mm, bde_end, start, end, false, next_shared);
	if (ret)
		return ret;

	return 0;
}

static int mpx_unmap_tables(struct mm_struct *mm,
		unsigned long start, unsigned long end)
{
	int ret;
	long __user *bd_entry, *bde_start, *bde_end;
	unsigned long bt_addr;

	/*
	 * "Edge" bounds tables are those which are being used by the region
	 * (start -> end), but that may be shared with adjacent areas.  If they
	 * turn out to be completely unshared, they will be freed.  If they are
	 * shared, we will free the backing store (like an MADV_DONTNEED) for
	 * areas used by this region.
	 */
	ret = unmap_edge_bts(mm, start, end);
	switch (ret) {
		/* non-present tables are OK */
		case 0:
		case -ENOENT:
			/* Success, or no tables to unmap */
			break;
		case -EINVAL:
		case -EFAULT:
		default:
			return ret;
	}

	/*
	 * Only unmap the bounds table that are
	 *   1. fully covered
	 *   2. not at the edges of the mapping, even if full aligned
	 */
	bde_start = mm->bd_addr + MPX_GET_BD_ENTRY_OFFSET(start);
	bde_end = mm->bd_addr + MPX_GET_BD_ENTRY_OFFSET(end-1);
	for (bd_entry = bde_start + 1; bd_entry < bde_end; bd_entry++) {
		ret = get_bt_addr(mm, bd_entry, &bt_addr);
		switch (ret) {
			case 0:
				break;
			case -ENOENT:
				/* No table here, try the next one */
				continue;
			case -EINVAL:
			case -EFAULT:
			default:
				/*
				 * Note: we are being strict here.
				 * Any time we run in to an issue
				 * unmapping tables, we stop and
				 * SIGSEGV.
				 */
				return ret;
		}

		ret = unmap_single_bt(mm, bd_entry, bt_addr);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Free unused bounds tables covered in a virtual address region being
 * munmap()ed. Assume end > start.
 *
 * This function will be called by do_munmap(), and the VMAs covering
 * the virtual address region start...end have already been split if
 * necessary, and the 'vma' is the first vma in this range (start -> end).
 */
void mpx_notify_unmap(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	int ret;

	/*
	 * Refuse to do anything unless userspace has asked
	 * the kernel to help manage the bounds tables,
	 */
	if (!kernel_managing_mpx_tables(current->mm))
		return;
	/*
	 * This will look across the entire 'start -> end' range,
	 * and find all of the non-VM_MPX VMAs.
	 *
	 * To avoid recursion, if a VM_MPX vma is found in the range
	 * (start->end), we will not continue follow-up work. This
	 * recursion represents having bounds tables for bounds tables,
	 * which should not occur normally. Being strict about it here
	 * helps ensure that we do not have an exploitable stack overflow.
	 */
	do {
		if (vma->vm_flags & VM_MPX)
			return;
		vma = vma->vm_next;
	} while (vma && vma->vm_start < end);

	ret = mpx_unmap_tables(mm, start, end);
	if (ret)
		force_sig(SIGSEGV, current);
}
