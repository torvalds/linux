// SPDX-License-Identifier: GPL-2.0
/*
 * mpx.c - Memory Protection eXtensions
 *
 * Copyright (c) 2014, Intel Corporation.
 * Qiaowei Ren <qiaowei.ren@intel.com>
 * Dave Hansen <dave.hansen@intel.com>
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm_types.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/sched/sysctl.h>

#include <asm/insn.h>
#include <asm/insn-eval.h>
#include <asm/mmu_context.h>
#include <asm/mpx.h>
#include <asm/processor.h>
#include <asm/fpu/internal.h>

#define CREATE_TRACE_POINTS
#include <asm/trace/mpx.h>

static inline unsigned long mpx_bd_size_bytes(struct mm_struct *mm)
{
	if (is_64bit_mm(mm))
		return MPX_BD_SIZE_BYTES_64;
	else
		return MPX_BD_SIZE_BYTES_32;
}

static inline unsigned long mpx_bt_size_bytes(struct mm_struct *mm)
{
	if (is_64bit_mm(mm))
		return MPX_BT_SIZE_BYTES_64;
	else
		return MPX_BT_SIZE_BYTES_32;
}

/*
 * This is really a simplified "vm_mmap". it only handles MPX
 * bounds tables (the bounds directory is user-allocated).
 */
static unsigned long mpx_mmap(unsigned long len)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr, populate;

	/* Only bounds table can be allocated here */
	if (len != mpx_bt_size_bytes(mm))
		return -EINVAL;

	down_write(&mm->mmap_sem);
	addr = do_mmap(NULL, 0, len, PROT_READ | PROT_WRITE,
		       MAP_ANONYMOUS | MAP_PRIVATE, VM_MPX, 0, &populate, NULL);
	up_write(&mm->mmap_sem);
	if (populate)
		mm_populate(addr, populate);

	return addr;
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
 */
int mpx_fault_info(struct mpx_fault_info *info, struct pt_regs *regs)
{
	const struct mpx_bndreg_state *bndregs;
	const struct mpx_bndreg *bndreg;
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
	/* get bndregs field from current task's xsave area */
	bndregs = get_xsave_field_ptr(XFEATURE_BNDREGS);
	if (!bndregs) {
		err = -EINVAL;
		goto err_out;
	}
	/* now go select the individual register in the set of 4 */
	bndreg = &bndregs->bndreg[bndregno];

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
	info->lower = (void __user *)(unsigned long)bndreg->lower_bound;
	info->upper = (void __user *)(unsigned long)~bndreg->upper_bound;
	info->addr  = insn_get_addr_ref(&insn, regs);

	/*
	 * We were not able to extract an address from the instruction,
	 * probably because there was something invalid in it.
	 */
	if (info->addr == (void __user *)-1) {
		err = -EINVAL;
		goto err_out;
	}
	trace_mpx_bounds_register_exception(info->addr, bndreg);
	return 0;
err_out:
	/* info might be NULL, but kfree() handles that */
	return err;
}

static __user void *mpx_get_bounds_dir(void)
{
	const struct mpx_bndcsr *bndcsr;

	if (!cpu_feature_enabled(X86_FEATURE_MPX))
		return MPX_INVALID_BOUNDS_DIR;

	/*
	 * The bounds directory pointer is stored in a register
	 * only accessible if we first do an xsave.
	 */
	bndcsr = get_xsave_field_ptr(XFEATURE_BNDCSR);
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

int mpx_enable_management(void)
{
	void __user *bd_base = MPX_INVALID_BOUNDS_DIR;
	struct mm_struct *mm = current->mm;
	int ret = 0;

	/*
	 * runtime in the userspace will be responsible for allocation of
	 * the bounds directory. Then, it will save the base of the bounds
	 * directory into XSAVE/XRSTOR Save Area and enable MPX through
	 * XRSTOR instruction.
	 *
	 * The copy_xregs_to_kernel() beneath get_xsave_field_ptr() is
	 * expected to be relatively expensive. Storing the bounds
	 * directory here means that we do not have to do xsave in the
	 * unmap path; we can just use mm->context.bd_addr instead.
	 */
	bd_base = mpx_get_bounds_dir();
	down_write(&mm->mmap_sem);

	/* MPX doesn't support addresses above 47 bits yet. */
	if (find_vma(mm, DEFAULT_MAP_WINDOW)) {
		pr_warn_once("%s (%d): MPX cannot handle addresses "
				"above 47-bits. Disabling.",
				current->comm, current->pid);
		ret = -ENXIO;
		goto out;
	}
	mm->context.bd_addr = bd_base;
	if (mm->context.bd_addr == MPX_INVALID_BOUNDS_DIR)
		ret = -ENXIO;
out:
	up_write(&mm->mmap_sem);
	return ret;
}

int mpx_disable_management(void)
{
	struct mm_struct *mm = current->mm;

	if (!cpu_feature_enabled(X86_FEATURE_MPX))
		return -ENXIO;

	down_write(&mm->mmap_sem);
	mm->context.bd_addr = MPX_INVALID_BOUNDS_DIR;
	up_write(&mm->mmap_sem);
	return 0;
}

static int mpx_cmpxchg_bd_entry(struct mm_struct *mm,
		unsigned long *curval,
		unsigned long __user *addr,
		unsigned long old_val, unsigned long new_val)
{
	int ret;
	/*
	 * user_atomic_cmpxchg_inatomic() actually uses sizeof()
	 * the pointer that we pass to it to figure out how much
	 * data to cmpxchg.  We have to be careful here not to
	 * pass a pointer to a 64-bit data type when we only want
	 * a 32-bit copy.
	 */
	if (is_64bit_mm(mm)) {
		ret = user_atomic_cmpxchg_inatomic(curval,
				addr, old_val, new_val);
	} else {
		u32 uninitialized_var(curval_32);
		u32 old_val_32 = old_val;
		u32 new_val_32 = new_val;
		u32 __user *addr_32 = (u32 __user *)addr;

		ret = user_atomic_cmpxchg_inatomic(&curval_32,
				addr_32, old_val_32, new_val_32);
		*curval = curval_32;
	}
	return ret;
}

/*
 * With 32-bit mode, a bounds directory is 4MB, and the size of each
 * bounds table is 16KB. With 64-bit mode, a bounds directory is 2GB,
 * and the size of each bounds table is 4MB.
 */
static int allocate_bt(struct mm_struct *mm, long __user *bd_entry)
{
	unsigned long expected_old_val = 0;
	unsigned long actual_old_val = 0;
	unsigned long bt_addr;
	unsigned long bd_new_entry;
	int ret = 0;

	/*
	 * Carve the virtual space out of userspace for the new
	 * bounds table:
	 */
	bt_addr = mpx_mmap(mpx_bt_size_bytes(mm));
	if (IS_ERR((void *)bt_addr))
		return PTR_ERR((void *)bt_addr);
	/*
	 * Set the valid flag (kinda like _PAGE_PRESENT in a pte)
	 */
	bd_new_entry = bt_addr | MPX_BD_ENTRY_VALID_FLAG;

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
	ret = mpx_cmpxchg_bd_entry(mm, &actual_old_val,	bd_entry,
				   expected_old_val, bd_new_entry);
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
	trace_mpx_new_bounds_table(bt_addr);
	return 0;
out_unmap:
	vm_munmap(bt_addr, mpx_bt_size_bytes(mm));
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
static int do_mpx_bt_fault(void)
{
	unsigned long bd_entry, bd_base;
	const struct mpx_bndcsr *bndcsr;
	struct mm_struct *mm = current->mm;

	bndcsr = get_xsave_field_ptr(XFEATURE_BNDCSR);
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
	    (bd_entry >= bd_base + mpx_bd_size_bytes(mm)))
		return -EINVAL;

	return allocate_bt(mm, (long __user *)bd_entry);
}

int mpx_handle_bd_fault(void)
{
	/*
	 * Userspace never asked us to manage the bounds tables,
	 * so refuse to help.
	 */
	if (!kernel_managing_mpx_tables(current->mm))
		return -EINVAL;

	return do_mpx_bt_fault();
}

/*
 * A thin wrapper around get_user_pages().  Returns 0 if the
 * fault was resolved or -errno if not.
 */
static int mpx_resolve_fault(long __user *addr, int write)
{
	long gup_ret;
	int nr_pages = 1;

	gup_ret = get_user_pages((unsigned long)addr, nr_pages,
			write ? FOLL_WRITE : 0,	NULL, NULL);
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

static unsigned long mpx_bd_entry_to_bt_addr(struct mm_struct *mm,
					     unsigned long bd_entry)
{
	unsigned long bt_addr = bd_entry;
	int align_to_bytes;
	/*
	 * Bit 0 in a bt_entry is always the valid bit.
	 */
	bt_addr &= ~MPX_BD_ENTRY_VALID_FLAG;
	/*
	 * Tables are naturally aligned at 8-byte boundaries
	 * on 64-bit and 4-byte boundaries on 32-bit.  The
	 * documentation makes it appear that the low bits
	 * are ignored by the hardware, so we do the same.
	 */
	if (is_64bit_mm(mm))
		align_to_bytes = 8;
	else
		align_to_bytes = 4;
	bt_addr &= ~(align_to_bytes-1);
	return bt_addr;
}

/*
 * We only want to do a 4-byte get_user() on 32-bit.  Otherwise,
 * we might run off the end of the bounds table if we are on
 * a 64-bit kernel and try to get 8 bytes.
 */
static int get_user_bd_entry(struct mm_struct *mm, unsigned long *bd_entry_ret,
		long __user *bd_entry_ptr)
{
	u32 bd_entry_32;
	int ret;

	if (is_64bit_mm(mm))
		return get_user(*bd_entry_ret, bd_entry_ptr);

	/*
	 * Note that get_user() uses the type of the *pointer* to
	 * establish the size of the get, not the destination.
	 */
	ret = get_user(bd_entry_32, (u32 __user *)bd_entry_ptr);
	*bd_entry_ret = bd_entry_32;
	return ret;
}

/*
 * Get the base of bounds tables pointed by specific bounds
 * directory entry.
 */
static int get_bt_addr(struct mm_struct *mm,
			long __user *bd_entry_ptr,
			unsigned long *bt_addr_result)
{
	int ret;
	int valid_bit;
	unsigned long bd_entry;
	unsigned long bt_addr;

	if (!access_ok((bd_entry_ptr), sizeof(*bd_entry_ptr)))
		return -EFAULT;

	while (1) {
		int need_write = 0;

		pagefault_disable();
		ret = get_user_bd_entry(mm, &bd_entry, bd_entry_ptr);
		pagefault_enable();
		if (!ret)
			break;
		if (ret == -EFAULT)
			ret = mpx_resolve_fault(bd_entry_ptr, need_write);
		/*
		 * If we could not resolve the fault, consider it
		 * userspace's fault and error out.
		 */
		if (ret)
			return ret;
	}

	valid_bit = bd_entry & MPX_BD_ENTRY_VALID_FLAG;
	bt_addr = mpx_bd_entry_to_bt_addr(mm, bd_entry);

	/*
	 * When the kernel is managing bounds tables, a bounds directory
	 * entry will either have a valid address (plus the valid bit)
	 * *OR* be completely empty. If we see a !valid entry *and* some
	 * data in the address field, we know something is wrong. This
	 * -EINVAL return will cause a SIGSEGV.
	 */
	if (!valid_bit && bt_addr)
		return -EINVAL;
	/*
	 * Do we have an completely zeroed bt entry?  That is OK.  It
	 * just means there was no bounds table for this memory.  Make
	 * sure to distinguish this from -EINVAL, which will cause
	 * a SEGV.
	 */
	if (!valid_bit)
		return -ENOENT;

	*bt_addr_result = bt_addr;
	return 0;
}

static inline int bt_entry_size_bytes(struct mm_struct *mm)
{
	if (is_64bit_mm(mm))
		return MPX_BT_ENTRY_BYTES_64;
	else
		return MPX_BT_ENTRY_BYTES_32;
}

/*
 * Take a virtual address and turns it in to the offset in bytes
 * inside of the bounds table where the bounds table entry
 * controlling 'addr' can be found.
 */
static unsigned long mpx_get_bt_entry_offset_bytes(struct mm_struct *mm,
		unsigned long addr)
{
	unsigned long bt_table_nr_entries;
	unsigned long offset = addr;

	if (is_64bit_mm(mm)) {
		/* Bottom 3 bits are ignored on 64-bit */
		offset >>= 3;
		bt_table_nr_entries = MPX_BT_NR_ENTRIES_64;
	} else {
		/* Bottom 2 bits are ignored on 32-bit */
		offset >>= 2;
		bt_table_nr_entries = MPX_BT_NR_ENTRIES_32;
	}
	/*
	 * We know the size of the table in to which we are
	 * indexing, and we have eliminated all the low bits
	 * which are ignored for indexing.
	 *
	 * Mask out all the high bits which we do not need
	 * to index in to the table.  Note that the tables
	 * are always powers of two so this gives us a proper
	 * mask.
	 */
	offset &= (bt_table_nr_entries-1);
	/*
	 * We now have an entry offset in terms of *entries* in
	 * the table.  We need to scale it back up to bytes.
	 */
	offset *= bt_entry_size_bytes(mm);
	return offset;
}

/*
 * How much virtual address space does a single bounds
 * directory entry cover?
 *
 * Note, we need a long long because 4GB doesn't fit in
 * to a long on 32-bit.
 */
static inline unsigned long bd_entry_virt_space(struct mm_struct *mm)
{
	unsigned long long virt_space;
	unsigned long long GB = (1ULL << 30);

	/*
	 * This covers 32-bit emulation as well as 32-bit kernels
	 * running on 64-bit hardware.
	 */
	if (!is_64bit_mm(mm))
		return (4ULL * GB) / MPX_BD_NR_ENTRIES_32;

	/*
	 * 'x86_virt_bits' returns what the hardware is capable
	 * of, and returns the full >32-bit address space when
	 * running 32-bit kernels on 64-bit hardware.
	 */
	virt_space = (1ULL << boot_cpu_data.x86_virt_bits);
	return virt_space / MPX_BD_NR_ENTRIES_64;
}

/*
 * Free the backing physical pages of bounds table 'bt_addr'.
 * Assume start...end is within that bounds table.
 */
static noinline int zap_bt_entries_mapping(struct mm_struct *mm,
		unsigned long bt_addr,
		unsigned long start_mapping, unsigned long end_mapping)
{
	struct vm_area_struct *vma;
	unsigned long addr, len;
	unsigned long start;
	unsigned long end;

	/*
	 * if we 'end' on a boundary, the offset will be 0 which
	 * is not what we want.  Back it up a byte to get the
	 * last bt entry.  Then once we have the entry itself,
	 * move 'end' back up by the table entry size.
	 */
	start = bt_addr + mpx_get_bt_entry_offset_bytes(mm, start_mapping);
	end   = bt_addr + mpx_get_bt_entry_offset_bytes(mm, end_mapping - 1);
	/*
	 * Move end back up by one entry.  Among other things
	 * this ensures that it remains page-aligned and does
	 * not screw up zap_page_range()
	 */
	end += bt_entry_size_bytes(mm);

	/*
	 * Find the first overlapping vma. If vma->vm_start > start, there
	 * will be a hole in the bounds table. This -EINVAL return will
	 * cause a SIGSEGV.
	 */
	vma = find_vma(mm, start);
	if (!vma || vma->vm_start > start)
		return -EINVAL;

	/*
	 * A NUMA policy on a VM_MPX VMA could cause this bounds table to
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
		zap_page_range(vma, addr, len);
		trace_mpx_unmap_zap(addr, addr+len);

		vma = vma->vm_next;
		addr = vma->vm_start;
	}
	return 0;
}

static unsigned long mpx_get_bd_entry_offset(struct mm_struct *mm,
		unsigned long addr)
{
	/*
	 * There are several ways to derive the bd offsets.  We
	 * use the following approach here:
	 * 1. We know the size of the virtual address space
	 * 2. We know the number of entries in a bounds table
	 * 3. We know that each entry covers a fixed amount of
	 *    virtual address space.
	 * So, we can just divide the virtual address by the
	 * virtual space used by one entry to determine which
	 * entry "controls" the given virtual address.
	 */
	if (is_64bit_mm(mm)) {
		int bd_entry_size = 8; /* 64-bit pointer */
		/*
		 * Take the 64-bit addressing hole in to account.
		 */
		addr &= ((1UL << boot_cpu_data.x86_virt_bits) - 1);
		return (addr / bd_entry_virt_space(mm)) * bd_entry_size;
	} else {
		int bd_entry_size = 4; /* 32-bit pointer */
		/*
		 * 32-bit has no hole so this case needs no mask
		 */
		return (addr / bd_entry_virt_space(mm)) * bd_entry_size;
	}
	/*
	 * The two return calls above are exact copies.  If we
	 * pull out a single copy and put it in here, gcc won't
	 * realize that we're doing a power-of-2 divide and use
	 * shifts.  It uses a real divide.  If we put them up
	 * there, it manages to figure it out (gcc 4.8.3).
	 */
}

static int unmap_entire_bt(struct mm_struct *mm,
		long __user *bd_entry, unsigned long bt_addr)
{
	unsigned long expected_old_val = bt_addr | MPX_BD_ENTRY_VALID_FLAG;
	unsigned long uninitialized_var(actual_old_val);
	int ret;

	while (1) {
		int need_write = 1;
		unsigned long cleared_bd_entry = 0;

		pagefault_disable();
		ret = mpx_cmpxchg_bd_entry(mm, &actual_old_val,
				bd_entry, expected_old_val, cleared_bd_entry);
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
		 * That is OK, since we were both trying to do
		 * the same thing.  Declare success.
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
	return do_munmap(mm, bt_addr, mpx_bt_size_bytes(mm), NULL);
}

static int try_unmap_single_bt(struct mm_struct *mm,
	       unsigned long start, unsigned long end)
{
	struct vm_area_struct *next;
	struct vm_area_struct *prev;
	/*
	 * "bta" == Bounds Table Area: the area controlled by the
	 * bounds table that we are unmapping.
	 */
	unsigned long bta_start_vaddr = start & ~(bd_entry_virt_space(mm)-1);
	unsigned long bta_end_vaddr = bta_start_vaddr + bd_entry_virt_space(mm);
	unsigned long uninitialized_var(bt_addr);
	void __user *bde_vaddr;
	int ret;
	/*
	 * We already unlinked the VMAs from the mm's rbtree so 'start'
	 * is guaranteed to be in a hole. This gets us the first VMA
	 * before the hole in to 'prev' and the next VMA after the hole
	 * in to 'next'.
	 */
	next = find_vma_prev(mm, start, &prev);
	/*
	 * Do not count other MPX bounds table VMAs as neighbors.
	 * Although theoretically possible, we do not allow bounds
	 * tables for bounds tables so our heads do not explode.
	 * If we count them as neighbors here, we may end up with
	 * lots of tables even though we have no actual table
	 * entries in use.
	 */
	while (next && (next->vm_flags & VM_MPX))
		next = next->vm_next;
	while (prev && (prev->vm_flags & VM_MPX))
		prev = prev->vm_prev;
	/*
	 * We know 'start' and 'end' lie within an area controlled
	 * by a single bounds table.  See if there are any other
	 * VMAs controlled by that bounds table.  If there are not
	 * then we can "expand" the are we are unmapping to possibly
	 * cover the entire table.
	 */
	next = find_vma_prev(mm, start, &prev);
	if ((!prev || prev->vm_end <= bta_start_vaddr) &&
	    (!next || next->vm_start >= bta_end_vaddr)) {
		/*
		 * No neighbor VMAs controlled by same bounds
		 * table.  Try to unmap the whole thing
		 */
		start = bta_start_vaddr;
		end = bta_end_vaddr;
	}

	bde_vaddr = mm->context.bd_addr + mpx_get_bd_entry_offset(mm, start);
	ret = get_bt_addr(mm, bde_vaddr, &bt_addr);
	/*
	 * No bounds table there, so nothing to unmap.
	 */
	if (ret == -ENOENT) {
		ret = 0;
		return 0;
	}
	if (ret)
		return ret;
	/*
	 * We are unmapping an entire table.  Either because the
	 * unmap that started this whole process was large enough
	 * to cover an entire table, or that the unmap was small
	 * but was the area covered by a bounds table.
	 */
	if ((start == bta_start_vaddr) &&
	    (end == bta_end_vaddr))
		return unmap_entire_bt(mm, bde_vaddr, bt_addr);
	return zap_bt_entries_mapping(mm, bt_addr, start, end);
}

static int mpx_unmap_tables(struct mm_struct *mm,
		unsigned long start, unsigned long end)
{
	unsigned long one_unmap_start;
	trace_mpx_unmap_search(start, end);

	one_unmap_start = start;
	while (one_unmap_start < end) {
		int ret;
		unsigned long next_unmap_start = ALIGN(one_unmap_start+1,
						       bd_entry_virt_space(mm));
		unsigned long one_unmap_end = end;
		/*
		 * if the end is beyond the current bounds table,
		 * move it back so we only deal with a single one
		 * at a time
		 */
		if (one_unmap_end > next_unmap_start)
			one_unmap_end = next_unmap_start;
		ret = try_unmap_single_bt(mm, one_unmap_start, one_unmap_end);
		if (ret)
			return ret;

		one_unmap_start = next_unmap_start;
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

/* MPX cannot handle addresses above 47 bits yet. */
unsigned long mpx_unmapped_area_check(unsigned long addr, unsigned long len,
		unsigned long flags)
{
	if (!kernel_managing_mpx_tables(current->mm))
		return addr;
	if (addr + len <= DEFAULT_MAP_WINDOW)
		return addr;
	if (flags & MAP_FIXED)
		return -ENOMEM;

	/*
	 * Requested len is larger than the whole area we're allowed to map in.
	 * Resetting hinting address wouldn't do much good -- fail early.
	 */
	if (len > DEFAULT_MAP_WINDOW)
		return -ENOMEM;

	/* Look for unmap area within DEFAULT_MAP_WINDOW */
	return 0;
}
