/*
 * Copyright (C) 2001, 2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Adapted from arch/i386/kernel/ldt.c
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>

#include "ia32priv.h"

/*
 * read_ldt() is not really atomic - this is not a problem since synchronization of reads
 * and writes done to the LDT has to be assured by user-space anyway. Writes are atomic,
 * to protect the security checks done on new descriptors.
 */
static int
read_ldt (void __user *ptr, unsigned long bytecount)
{
	unsigned long bytes_left, n;
	char __user *src, *dst;
	char buf[256];	/* temporary buffer (don't overflow kernel stack!) */

	if (bytecount > IA32_LDT_ENTRIES*IA32_LDT_ENTRY_SIZE)
		bytecount = IA32_LDT_ENTRIES*IA32_LDT_ENTRY_SIZE;

	bytes_left = bytecount;

	src = (void __user *) IA32_LDT_OFFSET;
	dst = ptr;

	while (bytes_left) {
		n = sizeof(buf);
		if (n > bytes_left)
			n = bytes_left;

		/*
		 * We know we're reading valid memory, but we still must guard against
		 * running out of memory.
		 */
		if (__copy_from_user(buf, src, n))
			return -EFAULT;

		if (copy_to_user(dst, buf, n))
			return -EFAULT;

		src += n;
		dst += n;
		bytes_left -= n;
	}
	return bytecount;
}

static int
read_default_ldt (void __user * ptr, unsigned long bytecount)
{
	unsigned long size;
	int err;

	/* XXX fix me: should return equivalent of default_ldt[0] */
	err = 0;
	size = 8;
	if (size > bytecount)
		size = bytecount;

	err = size;
	if (clear_user(ptr, size))
		err = -EFAULT;

	return err;
}

static int
write_ldt (void __user * ptr, unsigned long bytecount, int oldmode)
{
	struct ia32_user_desc ldt_info;
	__u64 entry;
	int ret;

	if (bytecount != sizeof(ldt_info))
		return -EINVAL;
	if (copy_from_user(&ldt_info, ptr, sizeof(ldt_info)))
		return -EFAULT;

	if (ldt_info.entry_number >= IA32_LDT_ENTRIES)
		return -EINVAL;
	if (ldt_info.contents == 3) {
		if (oldmode)
			return -EINVAL;
		if (ldt_info.seg_not_present == 0)
			return -EINVAL;
	}

	if (ldt_info.base_addr == 0 && ldt_info.limit == 0
	    && (oldmode || (ldt_info.contents == 0 && ldt_info.read_exec_only == 1
			    && ldt_info.seg_32bit == 0 && ldt_info.limit_in_pages == 0
			    && ldt_info.seg_not_present == 1 && ldt_info.useable == 0)))
		/* allow LDTs to be cleared by the user */
		entry = 0;
	else
		/* we must set the "Accessed" bit as IVE doesn't emulate it */
		entry = IA32_SEG_DESCRIPTOR(ldt_info.base_addr, ldt_info.limit,
					    (((ldt_info.read_exec_only ^ 1) << 1)
					     | (ldt_info.contents << 2)) | 1,
					    1, 3, ldt_info.seg_not_present ^ 1,
					    (oldmode ? 0 : ldt_info.useable),
					    ldt_info.seg_32bit,
					    ldt_info.limit_in_pages);
	/*
	 * Install the new entry.  We know we're accessing valid (mapped) user-level
	 * memory, but we still need to guard against out-of-memory, hence we must use
	 * put_user().
	 */
	ret = __put_user(entry, (__u64 __user *) IA32_LDT_OFFSET + ldt_info.entry_number);
	ia32_load_segment_descriptors(current);
	return ret;
}

asmlinkage int
sys32_modify_ldt (int func, unsigned int ptr, unsigned int bytecount)
{
	int ret = -ENOSYS;

	switch (func) {
	      case 0:
		ret = read_ldt(compat_ptr(ptr), bytecount);
		break;
	      case 1:
		ret = write_ldt(compat_ptr(ptr), bytecount, 1);
		break;
	      case 2:
		ret = read_default_ldt(compat_ptr(ptr), bytecount);
		break;
	      case 0x11:
		ret = write_ldt(compat_ptr(ptr), bytecount, 0);
		break;
	}
	return ret;
}
