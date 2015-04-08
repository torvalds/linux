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

static long get_pfn(unsigned long user_addr, unsigned long access,
		    unsigned long *pfn)
{
	struct vm_area_struct *vma;
	long ret;

	down_read(&current->mm->mmap_sem);
	ret = -EINVAL;
	vma = find_vma(current->mm, user_addr);
	if (!vma)
		goto out;
	ret = -EACCES;
	if (!(vma->vm_flags & access))
		goto out;
	ret = follow_pfn(vma, user_addr, pfn);
out:
	up_read(&current->mm->mmap_sem);
	return ret;
}

SYSCALL_DEFINE3(s390_pci_mmio_write, unsigned long, mmio_addr,
		const void __user *, user_buffer, size_t, length)
{
	u8 local_buf[64];
	void __iomem *io_addr;
	void *buf;
	unsigned long pfn;
	long ret;

	if (!zpci_is_enabled())
		return -ENODEV;

	if (length <= 0 || PAGE_SIZE - (mmio_addr & ~PAGE_MASK) < length)
		return -EINVAL;
	if (length > 64) {
		buf = kmalloc(length, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	} else
		buf = local_buf;

	ret = get_pfn(mmio_addr, VM_WRITE, &pfn);
	if (ret)
		goto out;
	io_addr = (void __iomem *)((pfn << PAGE_SHIFT) | (mmio_addr & ~PAGE_MASK));

	ret = -EFAULT;
	if ((unsigned long) io_addr < ZPCI_IOMAP_ADDR_BASE)
		goto out;

	if (copy_from_user(buf, user_buffer, length))
		goto out;

	ret = zpci_memcpy_toio(io_addr, buf, length);
out:
	if (buf != local_buf)
		kfree(buf);
	return ret;
}

SYSCALL_DEFINE3(s390_pci_mmio_read, unsigned long, mmio_addr,
		void __user *, user_buffer, size_t, length)
{
	u8 local_buf[64];
	void __iomem *io_addr;
	void *buf;
	unsigned long pfn;
	long ret;

	if (!zpci_is_enabled())
		return -ENODEV;

	if (length <= 0 || PAGE_SIZE - (mmio_addr & ~PAGE_MASK) < length)
		return -EINVAL;
	if (length > 64) {
		buf = kmalloc(length, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	} else
		buf = local_buf;

	ret = get_pfn(mmio_addr, VM_READ, &pfn);
	if (ret)
		goto out;
	io_addr = (void __iomem *)((pfn << PAGE_SHIFT) | (mmio_addr & ~PAGE_MASK));

	if ((unsigned long) io_addr < ZPCI_IOMAP_ADDR_BASE) {
		ret = -EFAULT;
		goto out;
	}
	ret = zpci_memcpy_fromio(buf, io_addr, length);
	if (ret)
		goto out;
	if (copy_to_user(user_buffer, buf, length))
		ret = -EFAULT;

out:
	if (buf != local_buf)
		kfree(buf);
	return ret;
}
