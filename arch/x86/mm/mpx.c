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

#include <asm/mman.h>
#include <asm/mpx.h>

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
	struct insn insn;
	uint8_t bndregno;
	int err;
	siginfo_t *info;

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
	info->si_lower = (void __user *)(unsigned long)
		(xsave_buf->bndreg[bndregno].lower_bound);
	info->si_upper = (void __user *)(unsigned long)
		(~xsave_buf->bndreg[bndregno].upper_bound);
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
	return ERR_PTR(err);
}
