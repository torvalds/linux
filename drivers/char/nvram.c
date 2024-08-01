// SPDX-License-Identifier: GPL-2.0-only
/*
 * CMOS/NV-RAM driver for Linux
 *
 * Copyright (C) 1997 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 * idea by and with help from Richard Jelinek <rj@suse.de>
 * Portions copyright (c) 2001,2002 Sun Microsystems (thockin@sun.com)
 *
 * This driver allows you to access the contents of the non-volatile memory in
 * the mc146818rtc.h real-time clock. This chip is built into all PCs and into
 * many Atari machines. In the former it's called "CMOS-RAM", in the latter
 * "NVRAM" (NV stands for non-volatile).
 *
 * The data are supplied as a (seekable) character device, /dev/nvram. The
 * size of this file is dependent on the controller.  The usual size is 114,
 * the number of freely available bytes in the memory (i.e., not used by the
 * RTC itself).
 *
 * Checksums over the NVRAM contents are managed by this driver. In case of a
 * bad checksum, reads and writes return -EIO. The checksum can be initialized
 * to a sane state either by ioctl(NVRAM_INIT) (clear whole NVRAM) or
 * ioctl(NVRAM_SETCKS) (doesn't change contents, just makes checksum valid
 * again; use with care!)
 *
 * 	1.1	Cesar Barros: SMP locking fixes
 * 		added changelog
 * 	1.2	Erik Gilling: Cobalt Networks support
 * 		Tim Hockin: general cleanup, Cobalt support
 * 	1.3	Wim Van Sebroeck: convert PRINT_PROC to seq_file
 */

#define NVRAM_VERSION	"1.3"

#include <linux/module.h>
#include <linux/nvram.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/mc146818rtc.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>

#ifdef CONFIG_PPC
#include <asm/nvram.h>
#endif

static DEFINE_MUTEX(nvram_mutex);
static DEFINE_SPINLOCK(nvram_state_lock);
static int nvram_open_cnt;	/* #times opened */
static int nvram_open_mode;	/* special open modes */
static ssize_t nvram_size;
#define NVRAM_WRITE		1 /* opened for writing (exclusive) */
#define NVRAM_EXCL		2 /* opened with O_EXCL */

#ifdef CONFIG_X86
/*
 * These functions are provided to be called internally or by other parts of
 * the kernel. It's up to the caller to ensure correct checksum before reading
 * or after writing (needs to be done only once).
 *
 * It is worth noting that these functions all access bytes of general
 * purpose memory in the NVRAM - that is to say, they all add the
 * NVRAM_FIRST_BYTE offset.  Pass them offsets into NVRAM as if you did not
 * know about the RTC cruft.
 */

#define NVRAM_BYTES		(128 - NVRAM_FIRST_BYTE)

/* Note that *all* calls to CMOS_READ and CMOS_WRITE must be done with
 * rtc_lock held. Due to the index-port/data-port design of the RTC, we
 * don't want two different things trying to get to it at once. (e.g. the
 * periodic 11 min sync from kernel/time/ntp.c vs. this driver.)
 */

static unsigned char __nvram_read_byte(int i)
{
	return CMOS_READ(NVRAM_FIRST_BYTE + i);
}

static unsigned char pc_nvram_read_byte(int i)
{
	unsigned long flags;
	unsigned char c;

	spin_lock_irqsave(&rtc_lock, flags);
	c = __nvram_read_byte(i);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return c;
}

/* This races nicely with trying to read with checksum checking (nvram_read) */
static void __nvram_write_byte(unsigned char c, int i)
{
	CMOS_WRITE(c, NVRAM_FIRST_BYTE + i);
}

static void pc_nvram_write_byte(unsigned char c, int i)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	__nvram_write_byte(c, i);
	spin_unlock_irqrestore(&rtc_lock, flags);
}

/* On PCs, the checksum is built only over bytes 2..31 */
#define PC_CKS_RANGE_START	2
#define PC_CKS_RANGE_END	31
#define PC_CKS_LOC		32

static int __nvram_check_checksum(void)
{
	int i;
	unsigned short sum = 0;
	unsigned short expect;

	for (i = PC_CKS_RANGE_START; i <= PC_CKS_RANGE_END; ++i)
		sum += __nvram_read_byte(i);
	expect = __nvram_read_byte(PC_CKS_LOC)<<8 |
	    __nvram_read_byte(PC_CKS_LOC+1);
	return (sum & 0xffff) == expect;
}

static void __nvram_set_checksum(void)
{
	int i;
	unsigned short sum = 0;

	for (i = PC_CKS_RANGE_START; i <= PC_CKS_RANGE_END; ++i)
		sum += __nvram_read_byte(i);
	__nvram_write_byte(sum >> 8, PC_CKS_LOC);
	__nvram_write_byte(sum & 0xff, PC_CKS_LOC + 1);
}

static long pc_nvram_set_checksum(void)
{
	spin_lock_irq(&rtc_lock);
	__nvram_set_checksum();
	spin_unlock_irq(&rtc_lock);
	return 0;
}

static long pc_nvram_initialize(void)
{
	ssize_t i;

	spin_lock_irq(&rtc_lock);
	for (i = 0; i < NVRAM_BYTES; ++i)
		__nvram_write_byte(0, i);
	__nvram_set_checksum();
	spin_unlock_irq(&rtc_lock);
	return 0;
}

static ssize_t pc_nvram_get_size(void)
{
	return NVRAM_BYTES;
}

static ssize_t pc_nvram_read(char *buf, size_t count, loff_t *ppos)
{
	char *p = buf;
	loff_t i;

	spin_lock_irq(&rtc_lock);
	if (!__nvram_check_checksum()) {
		spin_unlock_irq(&rtc_lock);
		return -EIO;
	}
	for (i = *ppos; count > 0 && i < NVRAM_BYTES; --count, ++i, ++p)
		*p = __nvram_read_byte(i);
	spin_unlock_irq(&rtc_lock);

	*ppos = i;
	return p - buf;
}

static ssize_t pc_nvram_write(char *buf, size_t count, loff_t *ppos)
{
	char *p = buf;
	loff_t i;

	spin_lock_irq(&rtc_lock);
	if (!__nvram_check_checksum()) {
		spin_unlock_irq(&rtc_lock);
		return -EIO;
	}
	for (i = *ppos; count > 0 && i < NVRAM_BYTES; --count, ++i, ++p)
		__nvram_write_byte(*p, i);
	__nvram_set_checksum();
	spin_unlock_irq(&rtc_lock);

	*ppos = i;
	return p - buf;
}

const struct nvram_ops arch_nvram_ops = {
	.read           = pc_nvram_read,
	.write          = pc_nvram_write,
	.read_byte      = pc_nvram_read_byte,
	.write_byte     = pc_nvram_write_byte,
	.get_size       = pc_nvram_get_size,
	.set_checksum   = pc_nvram_set_checksum,
	.initialize     = pc_nvram_initialize,
};
EXPORT_SYMBOL(arch_nvram_ops);
#endif /* CONFIG_X86 */

/*
 * The are the file operation function for user access to /dev/nvram
 */

static loff_t nvram_misc_llseek(struct file *file, loff_t offset, int origin)
{
	return generic_file_llseek_size(file, offset, origin, MAX_LFS_FILESIZE,
					nvram_size);
}

static ssize_t nvram_misc_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	char *tmp;
	ssize_t ret;


	if (*ppos >= nvram_size)
		return 0;

	count = min_t(size_t, count, nvram_size - *ppos);
	count = min_t(size_t, count, PAGE_SIZE);

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = nvram_read(tmp, count, ppos);
	if (ret <= 0)
		goto out;

	if (copy_to_user(buf, tmp, ret)) {
		*ppos -= ret;
		ret = -EFAULT;
	}

out:
	kfree(tmp);
	return ret;
}

static ssize_t nvram_misc_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char *tmp;
	ssize_t ret;

	if (*ppos >= nvram_size)
		return 0;

	count = min_t(size_t, count, nvram_size - *ppos);
	count = min_t(size_t, count, PAGE_SIZE);

	tmp = memdup_user(buf, count);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	ret = nvram_write(tmp, count, ppos);
	kfree(tmp);
	return ret;
}

static long nvram_misc_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	long ret = -ENOTTY;

	switch (cmd) {
#ifdef CONFIG_PPC
	case OBSOLETE_PMAC_NVRAM_GET_OFFSET:
		pr_warn("nvram: Using obsolete PMAC_NVRAM_GET_OFFSET ioctl\n");
		fallthrough;
	case IOC_NVRAM_GET_OFFSET:
		ret = -EINVAL;
#ifdef CONFIG_PPC_PMAC
		if (machine_is(powermac)) {
			int part, offset;

			if (copy_from_user(&part, (void __user *)arg,
					   sizeof(part)) != 0)
				return -EFAULT;
			if (part < pmac_nvram_OF || part > pmac_nvram_NR)
				return -EINVAL;
			offset = pmac_get_partition(part);
			if (offset < 0)
				return -EINVAL;
			if (copy_to_user((void __user *)arg,
					 &offset, sizeof(offset)) != 0)
				return -EFAULT;
			ret = 0;
		}
#endif
		break;
#ifdef CONFIG_PPC32
	case IOC_NVRAM_SYNC:
		if (ppc_md.nvram_sync != NULL) {
			mutex_lock(&nvram_mutex);
			ppc_md.nvram_sync();
			mutex_unlock(&nvram_mutex);
		}
		ret = 0;
		break;
#endif
#elif defined(CONFIG_X86) || defined(CONFIG_M68K)
	case NVRAM_INIT:
		/* initialize NVRAM contents and checksum */
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;

		if (arch_nvram_ops.initialize != NULL) {
			mutex_lock(&nvram_mutex);
			ret = arch_nvram_ops.initialize();
			mutex_unlock(&nvram_mutex);
		}
		break;
	case NVRAM_SETCKS:
		/* just set checksum, contents unchanged (maybe useful after
		 * checksum garbaged somehow...) */
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;

		if (arch_nvram_ops.set_checksum != NULL) {
			mutex_lock(&nvram_mutex);
			ret = arch_nvram_ops.set_checksum();
			mutex_unlock(&nvram_mutex);
		}
		break;
#endif /* CONFIG_X86 || CONFIG_M68K */
	}
	return ret;
}

static int nvram_misc_open(struct inode *inode, struct file *file)
{
	spin_lock(&nvram_state_lock);

	/* Prevent multiple readers/writers if desired. */
	if ((nvram_open_cnt && (file->f_flags & O_EXCL)) ||
	    (nvram_open_mode & NVRAM_EXCL)) {
		spin_unlock(&nvram_state_lock);
		return -EBUSY;
	}

#if defined(CONFIG_X86) || defined(CONFIG_M68K)
	/* Prevent multiple writers if the set_checksum ioctl is implemented. */
	if ((arch_nvram_ops.set_checksum != NULL) &&
	    (file->f_mode & FMODE_WRITE) && (nvram_open_mode & NVRAM_WRITE)) {
		spin_unlock(&nvram_state_lock);
		return -EBUSY;
	}
#endif

	if (file->f_flags & O_EXCL)
		nvram_open_mode |= NVRAM_EXCL;
	if (file->f_mode & FMODE_WRITE)
		nvram_open_mode |= NVRAM_WRITE;
	nvram_open_cnt++;

	spin_unlock(&nvram_state_lock);

	return 0;
}

static int nvram_misc_release(struct inode *inode, struct file *file)
{
	spin_lock(&nvram_state_lock);

	nvram_open_cnt--;

	/* if only one instance is open, clear the EXCL bit */
	if (nvram_open_mode & NVRAM_EXCL)
		nvram_open_mode &= ~NVRAM_EXCL;
	if (file->f_mode & FMODE_WRITE)
		nvram_open_mode &= ~NVRAM_WRITE;

	spin_unlock(&nvram_state_lock);

	return 0;
}

#if defined(CONFIG_X86) && defined(CONFIG_PROC_FS)
static const char * const floppy_types[] = {
	"none", "5.25'' 360k", "5.25'' 1.2M", "3.5'' 720k", "3.5'' 1.44M",
	"3.5'' 2.88M", "3.5'' 2.88M"
};

static const char * const gfx_types[] = {
	"EGA, VGA, ... (with BIOS)",
	"CGA (40 cols)",
	"CGA (80 cols)",
	"monochrome",
};

static void pc_nvram_proc_read(unsigned char *nvram, struct seq_file *seq,
			       void *offset)
{
	int checksum;
	int type;

	spin_lock_irq(&rtc_lock);
	checksum = __nvram_check_checksum();
	spin_unlock_irq(&rtc_lock);

	seq_printf(seq, "Checksum status: %svalid\n", checksum ? "" : "not ");

	seq_printf(seq, "# floppies     : %d\n",
	    (nvram[6] & 1) ? (nvram[6] >> 6) + 1 : 0);
	seq_printf(seq, "Floppy 0 type  : ");
	type = nvram[2] >> 4;
	if (type < ARRAY_SIZE(floppy_types))
		seq_printf(seq, "%s\n", floppy_types[type]);
	else
		seq_printf(seq, "%d (unknown)\n", type);
	seq_printf(seq, "Floppy 1 type  : ");
	type = nvram[2] & 0x0f;
	if (type < ARRAY_SIZE(floppy_types))
		seq_printf(seq, "%s\n", floppy_types[type]);
	else
		seq_printf(seq, "%d (unknown)\n", type);

	seq_printf(seq, "HD 0 type      : ");
	type = nvram[4] >> 4;
	if (type)
		seq_printf(seq, "%02x\n", type == 0x0f ? nvram[11] : type);
	else
		seq_printf(seq, "none\n");

	seq_printf(seq, "HD 1 type      : ");
	type = nvram[4] & 0x0f;
	if (type)
		seq_printf(seq, "%02x\n", type == 0x0f ? nvram[12] : type);
	else
		seq_printf(seq, "none\n");

	seq_printf(seq, "HD type 48 data: %d/%d/%d C/H/S, precomp %d, lz %d\n",
	    nvram[18] | (nvram[19] << 8),
	    nvram[20], nvram[25],
	    nvram[21] | (nvram[22] << 8), nvram[23] | (nvram[24] << 8));
	seq_printf(seq, "HD type 49 data: %d/%d/%d C/H/S, precomp %d, lz %d\n",
	    nvram[39] | (nvram[40] << 8),
	    nvram[41], nvram[46],
	    nvram[42] | (nvram[43] << 8), nvram[44] | (nvram[45] << 8));

	seq_printf(seq, "DOS base memory: %d kB\n", nvram[7] | (nvram[8] << 8));
	seq_printf(seq, "Extended memory: %d kB (configured), %d kB (tested)\n",
	    nvram[9] | (nvram[10] << 8), nvram[34] | (nvram[35] << 8));

	seq_printf(seq, "Gfx adapter    : %s\n",
	    gfx_types[(nvram[6] >> 4) & 3]);

	seq_printf(seq, "FPU            : %sinstalled\n",
	    (nvram[6] & 2) ? "" : "not ");

	return;
}

static int nvram_proc_read(struct seq_file *seq, void *offset)
{
	unsigned char contents[NVRAM_BYTES];
	int i = 0;

	spin_lock_irq(&rtc_lock);
	for (i = 0; i < NVRAM_BYTES; ++i)
		contents[i] = __nvram_read_byte(i);
	spin_unlock_irq(&rtc_lock);

	pc_nvram_proc_read(contents, seq, offset);

	return 0;
}
#endif /* CONFIG_X86 && CONFIG_PROC_FS */

static const struct file_operations nvram_misc_fops = {
	.owner		= THIS_MODULE,
	.llseek		= nvram_misc_llseek,
	.read		= nvram_misc_read,
	.write		= nvram_misc_write,
	.unlocked_ioctl	= nvram_misc_ioctl,
	.open		= nvram_misc_open,
	.release	= nvram_misc_release,
};

static struct miscdevice nvram_misc = {
	NVRAM_MINOR,
	"nvram",
	&nvram_misc_fops,
};

static int __init nvram_module_init(void)
{
	int ret;

	nvram_size = nvram_get_size();
	if (nvram_size < 0)
		return nvram_size;

	ret = misc_register(&nvram_misc);
	if (ret) {
		pr_err("nvram: can't misc_register on minor=%d\n", NVRAM_MINOR);
		return ret;
	}

#if defined(CONFIG_X86) && defined(CONFIG_PROC_FS)
	if (!proc_create_single("driver/nvram", 0, NULL, nvram_proc_read)) {
		pr_err("nvram: can't create /proc/driver/nvram\n");
		misc_deregister(&nvram_misc);
		return -ENOMEM;
	}
#endif

	pr_info("Non-volatile memory driver v" NVRAM_VERSION "\n");
	return 0;
}

static void __exit nvram_module_exit(void)
{
#if defined(CONFIG_X86) && defined(CONFIG_PROC_FS)
	remove_proc_entry("driver/nvram", NULL);
#endif
	misc_deregister(&nvram_misc);
}

module_init(nvram_module_init);
module_exit(nvram_module_exit);

MODULE_DESCRIPTION("CMOS/NV-RAM driver for Linux");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(NVRAM_MINOR);
MODULE_ALIAS("devname:nvram");
