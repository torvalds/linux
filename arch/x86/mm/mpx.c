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
#include <asm/mpx.h>
#include <asm/processor.h>
#include <asm/fpu-internal.h>

static const char *mpx_mapping_name(struct vm_area_struct *vma)
{
	return "[mpx]";
}

static struct vm_operations_struct mpx_vma_ops = {
	.name = mpx_mapping_name,
};

/*
 * This is really a simplified "vm_mmap". it only handles MPX
 * bounds tables (the bounds directory is user-allocated).
 *
 * Later on, we use the vma->vm_ops to uniquely identify these
 * VMAs.
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
	vma->vm_ops = &mpx_vma_ops;

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

static unsigned long get_reg_offset(struct insn *insn, struct pt_regs *regs,
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
	unsigned long addr, addr_offset;
	unsigned long base, base_offset;
	unsigned long indx, indx_offset;
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
