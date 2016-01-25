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
 * This file also provides some functions for other parts of the kernel that
 * want to access the NVRAM: nvram_{read,write,check_checksum,set_checksum}.
 * Obviously this can be used only if this driver is always configured into
 * the kernel and is not a module. Since the functions are used by some Atari
 * drivers, this is the case on the Atari.
 *
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

#define PC		1
#define ATARI		2

/* select machine configuration */
#if defined(CONFIG_ATARI)
#  define MACH ATARI
#elif defined(__i386__) || defined(__x86_64__) || defined(__arm__)  /* and ?? */
#  define MACH PC
#else
#  error Cannot build nvram driver for this machine configuration.
#endif

#if MACH == PC

/* RTC in a PC */
#define CHECK_DRIVER_INIT()	1

/* On PCs, the checksum is built only over bytes 2..31 */
#define PC_CKS_RANGE_START	2
#define PC_CKS_RANGE_END	31
#define PC_CKS_LOC		32
#define NVRAM_BYTES		(128-NVRAM_FIRST_BYTE)

#define mach_check_checksum	pc_check_checksum
#define mach_set_checksum	pc_set_checksum
#define mach_proc_infos		pc_proc_infos

#endif

#if MACH == ATARI

/* Special parameters for RTC in Atari machines */
#include <asm/atarihw.h>
#include <asm/atariints.h>
#define RTC_PORT(x)		(TT_RTC_BAS + 2*(x))
#define CHECK_DRIVER_INIT()	(MACH_IS_ATARI && ATARIHW_PRESENT(TT_CLK))

#define NVRAM_BYTES		50

/* On Ataris, the checksum is over all bytes except the checksum bytes
 * themselves; these are at the very end */
#define ATARI_CKS_RANGE_START	0
#define ATARI_CKS_RANGE_END	47
#define ATARI_CKS_LOC		48

#define mach_check_checksum	atari_check_checksum
#define mach_set_checksum	atari_set_checksum
#define mach_proc_infos		atari_proc_infos

#endif

/* Note that *all* calls to CMOS_READ and CMOS_WRITE must be done with
 * rtc_lock held. Due to the index-port/data-port design of the RTC, we
 * don't want two different things trying to get to it at once. (e.g. the
 * periodic 11 min sync from kernel/time/ntp.c vs. this driver.)
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/mc146818rtc.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>


static DEFINE_MUTEX(nvram_mutex);
static DEFINE_SPINLOCK(nvram_state_lock);
static int nvram_open_cnt;	/* #times opened */
static int nvram_open_mode;	/* special open modes */
#define NVRAM_WRITE		1 /* opened for writing (exclusive) */
#define NVRAM_EXCL		2 /* opened with O_EXCL */

static int mach_check_checksum(void);
static void mach_set_checksum(void);

#ifdef CONFIG_PROC_FS
static void mach_proc_infos(unsigned char *contents, struct seq_file *seq,
								void *offset);
#endif

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

unsigned char __nvram_read_byte(int i)
{
	return CMOS_READ(NVRAM_FIRST_BYTE + i);
}
EXPORT_SYMBOL(__nvram_read_byte);

unsigned char nvram_read_byte(int i)
{
	unsigned long flags;
	unsigned char c;

	spin_lock_irqsave(&rtc_lock, flags);
	c = __nvram_read_byte(i);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return c;
}
EXPORT_SYMBOL(nvram_read_byte);

/* This races nicely with trying to read with checksum checking (nvram_read) */
void __nvram_write_byte(unsigned char c, int i)
{
	CMOS_WRITE(c, NVRAM_FIRST_BYTE + i);
}
EXPORT_SYMBOL(__nvram_write_byte);

void nvram_write_byte(unsigned char c, int i)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	__nvram_write_byte(c, i);
	spin_unlock_irqrestore(&rtc_lock, flags);
}
EXPORT_SYMBOL(nvram_write_byte);

int __nvram_check_checksum(void)
{
	return mach_check_checksum();
}
EXPORT_SYMBOL(__nvram_check_checksum);

int nvram_check_checksum(void)
{
	unsigned long flags;
	int rv;

	spin_lock_irqsave(&rtc_lock, flags);
	rv = __nvram_check_checksum();
	spin_unlock_irqrestore(&rtc_lock, flags);
	return rv;
}
EXPORT_SYMBOL(nvram_check_checksum);

static void __nvram_set_checksum(void)
{
	mach_set_checksum();
}

#if 0
void nvram_set_checksum(void)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	__nvram_set_checksum();
	spin_unlock_irqrestore(&rtc_lock, flags);
}
#endif  /*  0  */

/*
 * The are the file operation function for user access to /dev/nvram
 */

static loff_t nvram_llseek(struct file *file, loff_t offset, int origin)
{
	return generic_file_llseek_size(file, offset, origin, MAX_LFS_FILESIZE,
					NVRAM_BYTES);
}

static ssize_t nvram_read(struct file *file, char __user *buf,
						size_t count, loff_t *ppos)
{
	unsigned char contents[NVRAM_BYTES];
	unsigned i = *ppos;
	unsigned char *tmp;

	spin_lock_irq(&rtc_lock);

	if (!__nvram_check_checksum())
		goto checksum_err;

	for (tmp = contents; count-- > 0 && i < NVRAM_BYTES; ++i, ++tmp)
		*tmp = __nvram_read_byte(i);

	spin_unlock_irq(&rtc_lock);

	if (copy_to_user(buf, contents, tmp - contents))
		return -EFAULT;

	*ppos = i;

	return tmp - contents;

checksum_err:
	spin_unlock_irq(&rtc_lock);
	return -EIO;
}

static ssize_t nvram_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	unsigned char contents[NVRAM_BYTES];
	unsigned i = *ppos;
	unsigned char *tmp;

	if (i >= NVRAM_BYTES)
		return 0;	/* Past EOF */

	if (count > NVRAM_BYTES - i)
		count = NVRAM_BYTES - i;
	if (count > NVRAM_BYTES)
		return -EFAULT;	/* Can't happen, but prove it to gcc */

	if (copy_from_user(contents, buf, count))
		return -EFAULT;

	spin_lock_irq(&rtc_lock);

	if (!__nvram_check_checksum())
		goto checksum_err;

	for (tmp = contents; count--; ++i, ++tmp)
		__nvram_write_byte(*tmp, i);

	__nvram_set_checksum();

	spin_unlock_irq(&rtc_lock);

	*ppos = i;

	return tmp - contents;

checksum_err:
	spin_unlock_irq(&rtc_lock);
	return -EIO;
}

static long nvram_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int i;

	switch (cmd) {

	case NVRAM_INIT:
		/* initialize NVRAM contents and checksum */
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;

		mutex_lock(&nvram_mutex);
		spin_lock_irq(&rtc_lock);

		for (i = 0; i < NVRAM_BYTES; ++i)
			__nvram_write_byte(0, i);
		__nvram_set_checksum();

		spin_unlock_irq(&rtc_lock);
		mutex_unlock(&nvram_mutex);
		return 0;

	case NVRAM_SETCKS:
		/* just set checksum, contents unchanged (maybe useful after
		 * checksum garbaged somehow...) */
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;

		mutex_lock(&nvram_mutex);
		spin_lock_irq(&rtc_lock);
		__nvram_set_checksum();
		spin_unlock_irq(&rtc_lock);
		mutex_unlock(&nvram_mutex);
		return 0;

	default:
		return -ENOTTY;
	}
}

static int nvram_open(struct inode *inode, struct file *file)
{
	spin_lock(&nvram_state_lock);

	if ((nvram_open_cnt && (file->f_flags & O_EXCL)) ||
	    (nvram_open_mode & NVRAM_EXCL) ||
	    ((file->f_mode & FMODE_WRITE) && (nvram_open_mode & NVRAM_WRITE))) {
		spin_unlock(&nvram_state_lock);
		return -EBUSY;
	}

	if (file->f_flags & O_EXCL)
		nvram_open_mode |= NVRAM_EXCL;
	if (file->f_mode & FMODE_WRITE)
		nvram_open_mode |= NVRAM_WRITE;
	nvram_open_cnt++;

	spin_unlock(&nvram_state_lock);

	return 0;
}

static int nvram_release(struct inode *inode, struct file *file)
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

#ifndef CONFIG_PROC_FS
static int nvram_add_proc_fs(void)
{
	return 0;
}

#else

static int nvram_proc_read(struct seq_file *seq, void *offset)
{
	unsigned char contents[NVRAM_BYTES];
	int i = 0;

	spin_lock_irq(&rtc_lock);
	for (i = 0; i < NVRAM_BYTES; ++i)
		contents[i] = __nvram_read_byte(i);
	spin_unlock_irq(&rtc_lock);

	mach_proc_infos(contents, seq, offset);

	return 0;
}

static int nvram_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvram_proc_read, NULL);
}

static const struct file_operations nvram_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= nvram_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int nvram_add_proc_fs(void)
{
	if (!proc_create("driver/nvram", 0, NULL, &nvram_proc_fops))
		return -ENOMEM;
	return 0;
}

#endif /* CONFIG_PROC_FS */

static const struct file_operations nvram_fops = {
	.owner		= THIS_MODULE,
	.llseek		= nvram_llseek,
	.read		= nvram_read,
	.write		= nvram_write,
	.unlocked_ioctl	= nvram_ioctl,
	.open		= nvram_open,
	.release	= nvram_release,
};

static struct miscdevice nvram_dev = {
	NVRAM_MINOR,
	"nvram",
	&nvram_fops
};

static int __init nvram_init(void)
{
	int ret;

	/* First test whether the driver should init at all */
	if (!CHECK_DRIVER_INIT())
		return -ENODEV;

	ret = misc_register(&nvram_dev);
	if (ret) {
		printk(KERN_ERR "nvram: can't misc_register on minor=%d\n",
		    NVRAM_MINOR);
		goto out;
	}
	ret = nvram_add_proc_fs();
	if (ret) {
		printk(KERN_ERR "nvram: can't create /proc/driver/nvram\n");
		goto outmisc;
	}
	ret = 0;
	printk(KERN_INFO "Non-volatile memory driver v" NVRAM_VERSION "\n");
out:
	return ret;
outmisc:
	misc_deregister(&nvram_dev);
	goto out;
}

static void __exit nvram_cleanup_module(void)
{
	remove_proc_entry("driver/nvram", NULL);
	misc_deregister(&nvram_dev);
}

module_init(nvram_init);
module_exit(nvram_cleanup_module);

/*
 * Machine specific functions
 */

#if MACH == PC

static int pc_check_checksum(void)
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

static void pc_set_checksum(void)
{
	int i;
	unsigned short sum = 0;

	for (i = PC_CKS_RANGE_START; i <= PC_CKS_RANGE_END; ++i)
		sum += __nvram_read_byte(i);
	__nvram_write_byte(sum >> 8, PC_CKS_LOC);
	__nvram_write_byte(sum & 0xff, PC_CKS_LOC + 1);
}

#ifdef CONFIG_PROC_FS

static char *floppy_types[] = {
	"none", "5.25'' 360k", "5.25'' 1.2M", "3.5'' 720k", "3.5'' 1.44M",
	"3.5'' 2.88M", "3.5'' 2.88M"
};

static char *gfx_types[] = {
	"EGA, VGA, ... (with BIOS)",
	"CGA (40 cols)",
	"CGA (80 cols)",
	"monochrome",
};

static void pc_proc_infos(unsigned char *nvram, struct seq_file *seq,
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
#endif

#endif /* MACH == PC */

#if MACH == ATARI

static int atari_check_checksum(void)
{
	int i;
	unsigned char sum = 0;

	for (i = ATARI_CKS_RANGE_START; i <= ATARI_CKS_RANGE_END; ++i)
		sum += __nvram_read_byte(i);
	return (__nvram_read_byte(ATARI_CKS_LOC) == (~sum & 0xff)) &&
	    (__nvram_read_byte(ATARI_CKS_LOC + 1) == (sum & 0xff));
}

static void atari_set_checksum(void)
{
	int i;
	unsigned char sum = 0;

	for (i = ATARI_CKS_RANGE_START; i <= ATARI_CKS_RANGE_END; ++i)
		sum += __nvram_read_byte(i);
	__nvram_write_byte(~sum, ATARI_CKS_LOC);
	__nvram_write_byte(sum, ATARI_CKS_LOC + 1);
}

#ifdef CONFIG_PROC_FS

static struct {
	unsigned char val;
	char *name;
} boot_prefs[] = {
	{ 0x80, "TOS" },
	{ 0x40, "ASV" },
	{ 0x20, "NetBSD (?)" },
	{ 0x10, "Linux" },
	{ 0x00, "unspecified" }
};

static char *languages[] = {
	"English (US)",
	"German",
	"French",
	"English (UK)",
	"Spanish",
	"Italian",
	"6 (undefined)",
	"Swiss (French)",
	"Swiss (German)"
};

static char *dateformat[] = {
	"MM%cDD%cYY",
	"DD%cMM%cYY",
	"YY%cMM%cDD",
	"YY%cDD%cMM",
	"4 (undefined)",
	"5 (undefined)",
	"6 (undefined)",
	"7 (undefined)"
};

static char *colors[] = {
	"2", "4", "16", "256", "65536", "??", "??", "??"
};

static void atari_proc_infos(unsigned char *nvram, struct seq_file *seq,
								void *offset)
{
	int checksum = nvram_check_checksum();
	int i;
	unsigned vmode;

	seq_printf(seq, "Checksum status  : %svalid\n", checksum ? "" : "not ");

	seq_printf(seq, "Boot preference  : ");
	for (i = ARRAY_SIZE(boot_prefs) - 1; i >= 0; --i) {
		if (nvram[1] == boot_prefs[i].val) {
			seq_printf(seq, "%s\n", boot_prefs[i].name);
			break;
		}
	}
	if (i < 0)
		seq_printf(seq, "0x%02x (undefined)\n", nvram[1]);

	seq_printf(seq, "SCSI arbitration : %s\n",
	    (nvram[16] & 0x80) ? "on" : "off");
	seq_printf(seq, "SCSI host ID     : ");
	if (nvram[16] & 0x80)
		seq_printf(seq, "%d\n", nvram[16] & 7);
	else
		seq_printf(seq, "n/a\n");

	/* the following entries are defined only for the Falcon */
	if ((atari_mch_cookie >> 16) != ATARI_MCH_FALCON)
		return;

	seq_printf(seq, "OS language      : ");
	if (nvram[6] < ARRAY_SIZE(languages))
		seq_printf(seq, "%s\n", languages[nvram[6]]);
	else
		seq_printf(seq, "%u (undefined)\n", nvram[6]);
	seq_printf(seq, "Keyboard language: ");
	if (nvram[7] < ARRAY_SIZE(languages))
		seq_printf(seq, "%s\n", languages[nvram[7]]);
	else
		seq_printf(seq, "%u (undefined)\n", nvram[7]);
	seq_printf(seq, "Date format      : ");
	seq_printf(seq, dateformat[nvram[8] & 7],
	    nvram[9] ? nvram[9] : '/', nvram[9] ? nvram[9] : '/');
	seq_printf(seq, ", %dh clock\n", nvram[8] & 16 ? 24 : 12);
	seq_printf(seq, "Boot delay       : ");
	if (nvram[10] == 0)
		seq_printf(seq, "default");
	else
		seq_printf(seq, "%ds%s\n", nvram[10],
		    nvram[10] < 8 ? ", no memory test" : "");

	vmode = (nvram[14] << 8) | nvram[15];
	seq_printf(seq,
	    "Video mode       : %s colors, %d columns, %s %s monitor\n",
	    colors[vmode & 7],
	    vmode & 8 ? 80 : 40,
	    vmode & 16 ? "VGA" : "TV", vmode & 32 ? "PAL" : "NTSC");
	seq_printf(seq, "                   %soverscan, compat. mode %s%s\n",
	    vmode & 64 ? "" : "no ",
	    vmode & 128 ? "on" : "off",
	    vmode & 256 ?
	    (vmode & 16 ? ", line doubling" : ", half screen") : "");

	return;
}
#endif

#endif /* MACH == ATARI */

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(NVRAM_MINOR);
