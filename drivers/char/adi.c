// SPDX-License-Identifier: GPL-2.0
/*
 * Privileged ADI driver for sparc64
 *
 * Author: Tom Hromatka <tom.hromatka@oracle.com>
 */
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/asi.h>

#define MAX_BUF_SZ	PAGE_SIZE

static int read_mcd_tag(unsigned long addr)
{
	long err;
	int ver;

	__asm__ __volatile__(
		"1:	ldxa [%[addr]] %[asi], %[ver]\n"
		"	mov 0, %[err]\n"
		"2:\n"
		"	.section .fixup,#alloc,#execinstr\n"
		"	.align 4\n"
		"3:	sethi %%hi(2b), %%g1\n"
		"	jmpl  %%g1 + %%lo(2b), %%g0\n"
		"	mov %[invalid], %[err]\n"
		"	.previous\n"
		"	.section __ex_table, \"a\"\n"
		"	.align 4\n"
		"	.word  1b, 3b\n"
		"	.previous\n"
		: [ver] "=r" (ver), [err] "=r" (err)
		: [addr] "r"  (addr), [invalid] "i" (EFAULT),
		  [asi] "i" (ASI_MCD_REAL)
		: "memory", "g1"
		);

	if (err)
		return -EFAULT;
	else
		return ver;
}

static ssize_t adi_read(struct file *file, char __user *buf,
			size_t count, loff_t *offp)
{
	size_t ver_buf_sz, bytes_read = 0;
	int ver_buf_idx = 0;
	loff_t offset;
	u8 *ver_buf;
	ssize_t ret;

	ver_buf_sz = min_t(size_t, count, MAX_BUF_SZ);
	ver_buf = kmalloc(ver_buf_sz, GFP_KERNEL);
	if (!ver_buf)
		return -ENOMEM;

	offset = (*offp) * adi_blksize();

	while (bytes_read < count) {
		ret = read_mcd_tag(offset);
		if (ret < 0)
			goto out;

		ver_buf[ver_buf_idx] = (u8)ret;
		ver_buf_idx++;
		offset += adi_blksize();

		if (ver_buf_idx >= ver_buf_sz) {
			if (copy_to_user(buf + bytes_read, ver_buf,
					 ver_buf_sz)) {
				ret = -EFAULT;
				goto out;
			}

			bytes_read += ver_buf_sz;
			ver_buf_idx = 0;

			ver_buf_sz = min(count - bytes_read,
					 (size_t)MAX_BUF_SZ);
		}
	}

	(*offp) += bytes_read;
	ret = bytes_read;
out:
	kfree(ver_buf);
	return ret;
}

static int set_mcd_tag(unsigned long addr, u8 ver)
{
	long err;

	__asm__ __volatile__(
		"1:	stxa %[ver], [%[addr]] %[asi]\n"
		"	mov 0, %[err]\n"
		"2:\n"
		"	.section .fixup,#alloc,#execinstr\n"
		"	.align 4\n"
		"3:	sethi %%hi(2b), %%g1\n"
		"	jmpl %%g1 + %%lo(2b), %%g0\n"
		"	mov %[invalid], %[err]\n"
		"	.previous\n"
		"	.section __ex_table, \"a\"\n"
		"	.align 4\n"
		"	.word 1b, 3b\n"
		"	.previous\n"
		: [err] "=r" (err)
		: [ver] "r" (ver), [addr] "r"  (addr),
		  [invalid] "i" (EFAULT), [asi] "i" (ASI_MCD_REAL)
		: "memory", "g1"
		);

	if (err)
		return -EFAULT;
	else
		return ver;
}

static ssize_t adi_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *offp)
{
	size_t ver_buf_sz, bytes_written = 0;
	loff_t offset;
	u8 *ver_buf;
	ssize_t ret;
	int i;

	if (count <= 0)
		return -EINVAL;

	ver_buf_sz = min_t(size_t, count, MAX_BUF_SZ);
	ver_buf = kmalloc(ver_buf_sz, GFP_KERNEL);
	if (!ver_buf)
		return -ENOMEM;

	offset = (*offp) * adi_blksize();

	do {
		if (copy_from_user(ver_buf, &buf[bytes_written],
				   ver_buf_sz)) {
			ret = -EFAULT;
			goto out;
		}

		for (i = 0; i < ver_buf_sz; i++) {
			ret = set_mcd_tag(offset, ver_buf[i]);
			if (ret < 0)
				goto out;

			offset += adi_blksize();
		}

		bytes_written += ver_buf_sz;
		ver_buf_sz = min(count - bytes_written, (size_t)MAX_BUF_SZ);
	} while (bytes_written < count);

	(*offp) += bytes_written;
	ret = bytes_written;
out:
	__asm__ __volatile__("membar #Sync");
	kfree(ver_buf);
	return ret;
}

static loff_t adi_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t ret = -EINVAL;

	switch (whence) {
	case SEEK_END:
	case SEEK_DATA:
	case SEEK_HOLE:
		/* unsupported */
		return -EINVAL;
	case SEEK_CUR:
		if (offset == 0)
			return file->f_pos;

		offset += file->f_pos;
		break;
	case SEEK_SET:
		break;
	}

	if (offset != file->f_pos) {
		file->f_pos = offset;
		ret = offset;
	}

	return ret;
}

static const struct file_operations adi_fops = {
	.owner		= THIS_MODULE,
	.llseek		= adi_llseek,
	.read		= adi_read,
	.write		= adi_write,
	.fop_flags	= FOP_UNSIGNED_OFFSET,
};

static struct miscdevice adi_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = KBUILD_MODNAME,
	.fops = &adi_fops,
};

static int __init adi_init(void)
{
	if (!adi_capable())
		return -EPERM;

	return misc_register(&adi_miscdev);
}

static void __exit adi_exit(void)
{
	misc_deregister(&adi_miscdev);
}

module_init(adi_init);
module_exit(adi_exit);

MODULE_AUTHOR("Tom Hromatka <tom.hromatka@oracle.com>");
MODULE_DESCRIPTION("Privileged interface to ADI");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
