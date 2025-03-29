// SPDX-License-Identifier: GPL-2.0
/*
 * Access to PCI I/O memory from user space programs.
 *
 * Copyright IBM Corp. 2014
 * Author(s): Alexey Ishchuk <aishchuk@linux.vnet.ibm.com>
 */
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <asm/asm-extable.h>
#include <asm/pci_io.h>
#include <asm/pci_debug.h>
#include <asm/asm.h>

static inline void zpci_err_mmio(u8 cc, u8 status, u64 offset)
{
	struct {
		u64 offset;
		u8 cc;
		u8 status;
	} data = {offset, cc, status};

	zpci_err_hex(&data, sizeof(data));
}

static inline int __pcistb_mio_inuser(
		void __iomem *ioaddr, const void __user *src,
		u64 len, u8 *status)
{
	int cc, exception;

	exception = 1;
	asm volatile (
		"	sacf	256\n"
		"0:	.insn	rsy,0xeb00000000d4,%[len],%[ioaddr],%[src]\n"
		"1:	lhi	%[exc],0\n"
		"2:	sacf	768\n"
		CC_IPM(cc)
		EX_TABLE(0b, 2b) EX_TABLE(1b, 2b)
		: CC_OUT(cc, cc), [len] "+d" (len), [exc] "+d" (exception)
		: [ioaddr] "a" (ioaddr), [src] "Q" (*((u8 __force *)src))
		: CC_CLOBBER_LIST("memory"));
	*status = len >> 24 & 0xff;
	return exception ? -ENXIO : CC_TRANSFORM(cc);
}

static inline int __pcistg_mio_inuser(
		void __iomem *ioaddr, const void __user *src,
		u64 ulen, u8 *status)
{
	union register_pair ioaddr_len = {.even = (u64 __force)ioaddr, .odd = ulen};
	int cc, exception;
	u64 val = 0;
	u64 cnt = ulen;
	u8 tmp;

	/*
	 * copy 0 < @len <= 8 bytes from @src into the right most bytes of
	 * a register, then store it to PCI at @ioaddr while in secondary
	 * address space. pcistg then uses the user mappings.
	 */
	exception = 1;
	asm volatile (
		"	sacf	256\n"
		"0:	llgc	%[tmp],0(%[src])\n"
		"4:	sllg	%[val],%[val],8\n"
		"	aghi	%[src],1\n"
		"	ogr	%[val],%[tmp]\n"
		"	brctg	%[cnt],0b\n"
		"1:	.insn	rre,0xb9d40000,%[val],%[ioaddr_len]\n"
		"2:	lhi	%[exc],0\n"
		"3:	sacf	768\n"
		CC_IPM(cc)
		EX_TABLE(0b, 3b) EX_TABLE(4b, 3b) EX_TABLE(1b, 3b) EX_TABLE(2b, 3b)
		: [src] "+a" (src), [cnt] "+d" (cnt),
		  [val] "+d" (val), [tmp] "=d" (tmp), [exc] "+d" (exception),
		  CC_OUT(cc, cc), [ioaddr_len] "+&d" (ioaddr_len.pair)
		:
		: CC_CLOBBER_LIST("memory"));
	*status = ioaddr_len.odd >> 24 & 0xff;

	cc = exception ? -ENXIO : CC_TRANSFORM(cc);
	/* did we read everything from user memory? */
	if (!cc && cnt != 0)
		cc = -EFAULT;

	return cc;
}

static inline int __memcpy_toio_inuser(void __iomem *dst,
				   const void __user *src, size_t n)
{
	int size, rc = 0;
	u8 status = 0;

	if (!src)
		return -EINVAL;

	while (n > 0) {
		size = zpci_get_max_io_size((u64 __force) dst,
					    (u64 __force) src, n,
					    ZPCI_MAX_WRITE_SIZE);
		if (size > 8) /* main path */
			rc = __pcistb_mio_inuser(dst, src, size, &status);
		else
			rc = __pcistg_mio_inuser(dst, src, size, &status);
		if (rc)
			break;
		src += size;
		dst += size;
		n -= size;
	}
	if (rc)
		zpci_err_mmio(rc, status, (__force u64) dst);
	return rc;
}

SYSCALL_DEFINE3(s390_pci_mmio_write, unsigned long, mmio_addr,
		const void __user *, user_buffer, size_t, length)
{
	struct follow_pfnmap_args args = { };
	u8 local_buf[64];
	void __iomem *io_addr;
	void *buf;
	struct vm_area_struct *vma;
	long ret;

	if (!zpci_is_enabled())
		return -ENODEV;

	if (length <= 0 || PAGE_SIZE - (mmio_addr & ~PAGE_MASK) < length)
		return -EINVAL;

	/*
	 * We only support write access to MIO capable devices if we are on
	 * a MIO enabled system. Otherwise we would have to check for every
	 * address if it is a special ZPCI_ADDR and would have to do
	 * a pfn lookup which we don't need for MIO capable devices.  Currently
	 * ISM devices are the only devices without MIO support and there is no
	 * known need for accessing these from userspace.
	 */
	if (static_branch_likely(&have_mio)) {
		ret = __memcpy_toio_inuser((void  __iomem *) mmio_addr,
					user_buffer,
					length);
		return ret;
	}

	if (length > 64) {
		buf = kmalloc(length, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	} else
		buf = local_buf;

	ret = -EFAULT;
	if (copy_from_user(buf, user_buffer, length))
		goto out_free;

	mmap_read_lock(current->mm);
	ret = -EINVAL;
	vma = vma_lookup(current->mm, mmio_addr);
	if (!vma)
		goto out_unlock_mmap;
	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		goto out_unlock_mmap;
	ret = -EACCES;
	if (!(vma->vm_flags & VM_WRITE))
		goto out_unlock_mmap;

	args.address = mmio_addr;
	args.vma = vma;
	ret = follow_pfnmap_start(&args);
	if (ret) {
		fixup_user_fault(current->mm, mmio_addr, FAULT_FLAG_WRITE, NULL);
		ret = follow_pfnmap_start(&args);
		if (ret)
			goto out_unlock_mmap;
	}

	io_addr = (void __iomem *)((args.pfn << PAGE_SHIFT) |
			(mmio_addr & ~PAGE_MASK));

	if ((unsigned long) io_addr < ZPCI_IOMAP_ADDR_BASE)
		goto out_unlock_pt;

	ret = zpci_memcpy_toio(io_addr, buf, length);
out_unlock_pt:
	follow_pfnmap_end(&args);
out_unlock_mmap:
	mmap_read_unlock(current->mm);
out_free:
	if (buf != local_buf)
		kfree(buf);
	return ret;
}

static inline int __pcilg_mio_inuser(
		void __user *dst, const void __iomem *ioaddr,
		u64 ulen, u8 *status)
{
	union register_pair ioaddr_len = {.even = (u64 __force)ioaddr, .odd = ulen};
	u64 cnt = ulen;
	int shift = ulen * 8;
	int cc, exception;
	u64 val, tmp;

	/*
	 * read 0 < @len <= 8 bytes from the PCI memory mapped at @ioaddr (in
	 * user space) into a register using pcilg then store these bytes at
	 * user address @dst
	 */
	exception = 1;
	asm volatile (
		"	sacf	256\n"
		"0:	.insn	rre,0xb9d60000,%[val],%[ioaddr_len]\n"
		"1:	lhi	%[exc],0\n"
		"	jne	4f\n"
		"2:	ahi	%[shift],-8\n"
		"	srlg	%[tmp],%[val],0(%[shift])\n"
		"3:	stc	%[tmp],0(%[dst])\n"
		"5:	aghi	%[dst],1\n"
		"	brctg	%[cnt],2b\n"
		/*
		 * Use xr to clear exc and set condition code to zero
		 * to ensure flag output is correct for this branch.
		 */
		"	xr	%[exc],%[exc]\n"
		"4:	sacf	768\n"
		CC_IPM(cc)
		EX_TABLE(0b, 4b) EX_TABLE(1b, 4b) EX_TABLE(3b, 4b) EX_TABLE(5b, 4b)
		: [ioaddr_len] "+&d" (ioaddr_len.pair), [exc] "+d" (exception),
		  CC_OUT(cc, cc), [val] "=d" (val),
		  [dst] "+a" (dst), [cnt] "+d" (cnt), [tmp] "=d" (tmp),
		  [shift] "+d" (shift)
		:
		: CC_CLOBBER_LIST("memory"));

	cc = exception ? -ENXIO : CC_TRANSFORM(cc);
	/* did we write everything to the user space buffer? */
	if (!cc && cnt != 0)
		cc = -EFAULT;

	*status = ioaddr_len.odd >> 24 & 0xff;
	return cc;
}

static inline int __memcpy_fromio_inuser(void __user *dst,
				     const void __iomem *src,
				     unsigned long n)
{
	int size, rc = 0;
	u8 status;

	while (n > 0) {
		size = zpci_get_max_io_size((u64 __force) src,
					    (u64 __force) dst, n,
					    ZPCI_MAX_READ_SIZE);
		rc = __pcilg_mio_inuser(dst, src, size, &status);
		if (rc)
			break;
		src += size;
		dst += size;
		n -= size;
	}
	if (rc)
		zpci_err_mmio(rc, status, (__force u64) dst);
	return rc;
}

SYSCALL_DEFINE3(s390_pci_mmio_read, unsigned long, mmio_addr,
		void __user *, user_buffer, size_t, length)
{
	struct follow_pfnmap_args args = { };
	u8 local_buf[64];
	void __iomem *io_addr;
	void *buf;
	struct vm_area_struct *vma;
	long ret;

	if (!zpci_is_enabled())
		return -ENODEV;

	if (length <= 0 || PAGE_SIZE - (mmio_addr & ~PAGE_MASK) < length)
		return -EINVAL;

	/*
	 * We only support read access to MIO capable devices if we are on
	 * a MIO enabled system. Otherwise we would have to check for every
	 * address if it is a special ZPCI_ADDR and would have to do
	 * a pfn lookup which we don't need for MIO capable devices.  Currently
	 * ISM devices are the only devices without MIO support and there is no
	 * known need for accessing these from userspace.
	 */
	if (static_branch_likely(&have_mio)) {
		ret = __memcpy_fromio_inuser(
				user_buffer, (const void __iomem *)mmio_addr,
				length);
		return ret;
	}

	if (length > 64) {
		buf = kmalloc(length, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	} else {
		buf = local_buf;
	}

	mmap_read_lock(current->mm);
	ret = -EINVAL;
	vma = vma_lookup(current->mm, mmio_addr);
	if (!vma)
		goto out_unlock_mmap;
	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		goto out_unlock_mmap;
	ret = -EACCES;
	if (!(vma->vm_flags & VM_READ))
		goto out_unlock_mmap;

	args.vma = vma;
	args.address = mmio_addr;
	ret = follow_pfnmap_start(&args);
	if (ret) {
		fixup_user_fault(current->mm, mmio_addr, 0, NULL);
		ret = follow_pfnmap_start(&args);
		if (ret)
			goto out_unlock_mmap;
	}

	io_addr = (void __iomem *)((args.pfn << PAGE_SHIFT) |
			(mmio_addr & ~PAGE_MASK));

	if ((unsigned long) io_addr < ZPCI_IOMAP_ADDR_BASE) {
		ret = -EFAULT;
		goto out_unlock_pt;
	}
	ret = zpci_memcpy_fromio(buf, io_addr, length);

out_unlock_pt:
	follow_pfnmap_end(&args);
out_unlock_mmap:
	mmap_read_unlock(current->mm);

	if (!ret && copy_to_user(user_buffer, buf, length))
		ret = -EFAULT;

	if (buf != local_buf)
		kfree(buf);
	return ret;
}
