#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/capability.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#define LINE_SIZE 80

#include <asm/mtrr.h>
#include "mtrr.h"

/* RED-PEN: this is accessed without any locking */
extern unsigned int *usage_table;


#define FILE_FCOUNT(f) (((struct seq_file *)((f)->private_data))->private)

static const char *const mtrr_strings[MTRR_NUM_TYPES] =
{
    "uncachable",               /* 0 */
    "write-combining",          /* 1 */
    "?",                        /* 2 */
    "?",                        /* 3 */
    "write-through",            /* 4 */
    "write-protect",            /* 5 */
    "write-back",               /* 6 */
};

const char *mtrr_attrib_to_str(int x)
{
	return (x <= 6) ? mtrr_strings[x] : "?";
}

#ifdef CONFIG_PROC_FS

static int
mtrr_file_add(unsigned long base, unsigned long size,
	      unsigned int type, char increment, struct file *file, int page)
{
	int reg, max;
	unsigned int *fcount = FILE_FCOUNT(file); 

	max = num_var_ranges;
	if (fcount == NULL) {
		fcount = kzalloc(max * sizeof *fcount, GFP_KERNEL);
		if (!fcount)
			return -ENOMEM;
		FILE_FCOUNT(file) = fcount;
	}
	if (!page) {
		if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1)))
			return -EINVAL;
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
	}
	reg = mtrr_add_page(base, size, type, 1);
	if (reg >= 0)
		++fcount[reg];
	return reg;
}

static int
mtrr_file_del(unsigned long base, unsigned long size,
	      struct file *file, int page)
{
	int reg;
	unsigned int *fcount = FILE_FCOUNT(file);

	if (!page) {
		if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1)))
			return -EINVAL;
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
	}
	reg = mtrr_del_page(-1, base, size);
	if (reg < 0)
		return reg;
	if (fcount == NULL)
		return reg;
	if (fcount[reg] < 1)
		return -EINVAL;
	--fcount[reg];
	return reg;
}

/* RED-PEN: seq_file can seek now. this is ignored. */
static ssize_t
mtrr_write(struct file *file, const char __user *buf, size_t len, loff_t * ppos)
/*  Format of control line:
    "base=%Lx size=%Lx type=%s"     OR:
    "disable=%d"
*/
{
	int i, err;
	unsigned long reg;
	unsigned long long base, size;
	char *ptr;
	char line[LINE_SIZE];
	size_t linelen;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!len)
		return -EINVAL;
	memset(line, 0, LINE_SIZE);
	if (len > LINE_SIZE)
		len = LINE_SIZE;
	if (copy_from_user(line, buf, len - 1))
		return -EFAULT;
	linelen = strlen(line);
	ptr = line + linelen - 1;
	if (linelen && *ptr == '\n')
		*ptr = '\0';
	if (!strncmp(line, "disable=", 8)) {
		reg = simple_strtoul(line + 8, &ptr, 0);
		err = mtrr_del_page(reg, 0, 0);
		if (err < 0)
			return err;
		return len;
	}
	if (strncmp(line, "base=", 5))
		return -EINVAL;
	base = simple_strtoull(line + 5, &ptr, 0);
	for (; isspace(*ptr); ++ptr) ;
	if (strncmp(ptr, "size=", 5))
		return -EINVAL;
	size = simple_strtoull(ptr + 5, &ptr, 0);
	if ((base & 0xfff) || (size & 0xfff))
		return -EINVAL;
	for (; isspace(*ptr); ++ptr) ;
	if (strncmp(ptr, "type=", 5))
		return -EINVAL;
	ptr += 5;
	for (; isspace(*ptr); ++ptr) ;
	for (i = 0; i < MTRR_NUM_TYPES; ++i) {
		if (strcmp(ptr, mtrr_strings[i]))
			continue;
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
		err =
		    mtrr_add_page((unsigned long) base, (unsigned long) size, i,
				  1);
		if (err < 0)
			return err;
		return len;
	}
	return -EINVAL;
}

static long
mtrr_ioctl(struct file *file, unsigned int cmd, unsigned long __arg)
{
	int err = 0;
	mtrr_type type;
	unsigned long size;
	struct mtrr_sentry sentry;
	struct mtrr_gentry gentry;
	void __user *arg = (void __user *) __arg;

	switch (cmd) {
	case MTRRIOC_ADD_ENTRY:
	case MTRRIOC_SET_ENTRY:
	case MTRRIOC_DEL_ENTRY:
	case MTRRIOC_KILL_ENTRY:
	case MTRRIOC_ADD_PAGE_ENTRY:
	case MTRRIOC_SET_PAGE_ENTRY:
	case MTRRIOC_DEL_PAGE_ENTRY:
	case MTRRIOC_KILL_PAGE_ENTRY:
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		break;
	case MTRRIOC_GET_ENTRY:
	case MTRRIOC_GET_PAGE_ENTRY:
		if (copy_from_user(&gentry, arg, sizeof gentry))
			return -EFAULT;
		break;
#ifdef CONFIG_COMPAT
	case MTRRIOC32_ADD_ENTRY:
	case MTRRIOC32_SET_ENTRY:
	case MTRRIOC32_DEL_ENTRY:
	case MTRRIOC32_KILL_ENTRY:
	case MTRRIOC32_ADD_PAGE_ENTRY:
	case MTRRIOC32_SET_PAGE_ENTRY:
	case MTRRIOC32_DEL_PAGE_ENTRY:
	case MTRRIOC32_KILL_PAGE_ENTRY: {
		struct mtrr_sentry32 __user *s32 = (struct mtrr_sentry32 __user *)__arg;
		err = get_user(sentry.base, &s32->base);
		err |= get_user(sentry.size, &s32->size);
		err |= get_user(sentry.type, &s32->type);
		if (err)
			return err;
		break;
	}
	case MTRRIOC32_GET_ENTRY:
	case MTRRIOC32_GET_PAGE_ENTRY: {
		struct mtrr_gentry32 __user *g32 = (struct mtrr_gentry32 __user *)__arg;
		err = get_user(gentry.regnum, &g32->regnum);
		err |= get_user(gentry.base, &g32->base);
		err |= get_user(gentry.size, &g32->size);
		err |= get_user(gentry.type, &g32->type);
		if (err)
			return err;
		break;
	}
#endif
	}

	switch (cmd) {
	default:
		return -ENOTTY;
	case MTRRIOC_ADD_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_ADD_ENTRY:
#endif
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		err =
		    mtrr_file_add(sentry.base, sentry.size, sentry.type, 1,
				  file, 0);
		break;
	case MTRRIOC_SET_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_SET_ENTRY:
#endif
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		err = mtrr_add(sentry.base, sentry.size, sentry.type, 0);
		break;
	case MTRRIOC_DEL_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_DEL_ENTRY:
#endif
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		err = mtrr_file_del(sentry.base, sentry.size, file, 0);
		break;
	case MTRRIOC_KILL_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_KILL_ENTRY:
#endif
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		err = mtrr_del(-1, sentry.base, sentry.size);
		break;
	case MTRRIOC_GET_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_GET_ENTRY:
#endif
		if (gentry.regnum >= num_var_ranges)
			return -EINVAL;
		mtrr_if->get(gentry.regnum, &gentry.base, &size, &type);

		/* Hide entries that go above 4GB */
		if (gentry.base + size - 1 >= (1UL << (8 * sizeof(gentry.size) - PAGE_SHIFT))
		    || size >= (1UL << (8 * sizeof(gentry.size) - PAGE_SHIFT)))
			gentry.base = gentry.size = gentry.type = 0;
		else {
			gentry.base <<= PAGE_SHIFT;
			gentry.size = size << PAGE_SHIFT;
			gentry.type = type;
		}

		break;
	case MTRRIOC_ADD_PAGE_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_ADD_PAGE_ENTRY:
#endif
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		err =
		    mtrr_file_add(sentry.base, sentry.size, sentry.type, 1,
				  file, 1);
		break;
	case MTRRIOC_SET_PAGE_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_SET_PAGE_ENTRY:
#endif
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		err = mtrr_add_page(sentry.base, sentry.size, sentry.type, 0);
		break;
	case MTRRIOC_DEL_PAGE_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_DEL_PAGE_ENTRY:
#endif
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		err = mtrr_file_del(sentry.base, sentry.size, file, 1);
		break;
	case MTRRIOC_KILL_PAGE_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_KILL_PAGE_ENTRY:
#endif
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		err = mtrr_del_page(-1, sentry.base, sentry.size);
		break;
	case MTRRIOC_GET_PAGE_ENTRY:
#ifdef CONFIG_COMPAT
	case MTRRIOC32_GET_PAGE_ENTRY:
#endif
		if (gentry.regnum >= num_var_ranges)
			return -EINVAL;
		mtrr_if->get(gentry.regnum, &gentry.base, &size, &type);
		/* Hide entries that would overflow */
		if (size != (__typeof__(gentry.size))size)
			gentry.base = gentry.size = gentry.type = 0;
		else {
			gentry.size = size;
			gentry.type = type;
		}
		break;
	}

	if (err)
		return err;

	switch(cmd) {
	case MTRRIOC_GET_ENTRY:
	case MTRRIOC_GET_PAGE_ENTRY:
		if (copy_to_user(arg, &gentry, sizeof gentry))
			err = -EFAULT;
		break;
#ifdef CONFIG_COMPAT
	case MTRRIOC32_GET_ENTRY:
	case MTRRIOC32_GET_PAGE_ENTRY: {
		struct mtrr_gentry32 __user *g32 = (struct mtrr_gentry32 __user *)__arg;
		err = put_user(gentry.base, &g32->base);
		err |= put_user(gentry.size, &g32->size);
		err |= put_user(gentry.regnum, &g32->regnum);
		err |= put_user(gentry.type, &g32->type);
		break;
	}
#endif
	}
	return err;
}

static int
mtrr_close(struct inode *ino, struct file *file)
{
	int i, max;
	unsigned int *fcount = FILE_FCOUNT(file);

	if (fcount != NULL) {
		max = num_var_ranges;
		for (i = 0; i < max; ++i) {
			while (fcount[i] > 0) {
				mtrr_del(i, 0, 0);
				--fcount[i];
			}
		}
		kfree(fcount);
		FILE_FCOUNT(file) = NULL;
	}
	return single_release(ino, file);
}

static int mtrr_seq_show(struct seq_file *seq, void *offset);

static int mtrr_open(struct inode *inode, struct file *file)
{
	if (!mtrr_if) 
		return -EIO;
	if (!mtrr_if->get) 
		return -ENXIO; 
	return single_open(file, mtrr_seq_show, NULL);
}

static const struct file_operations mtrr_fops = {
	.owner   = THIS_MODULE,
	.open	 = mtrr_open, 
	.read    = seq_read,
	.llseek  = seq_lseek,
	.write   = mtrr_write,
	.unlocked_ioctl = mtrr_ioctl,
	.compat_ioctl = mtrr_ioctl,
	.release = mtrr_close,
};


static struct proc_dir_entry *proc_root_mtrr;


static int mtrr_seq_show(struct seq_file *seq, void *offset)
{
	char factor;
	int i, max, len;
	mtrr_type type;
	unsigned long base, size;

	len = 0;
	max = num_var_ranges;
	for (i = 0; i < max; i++) {
		mtrr_if->get(i, &base, &size, &type);
		if (size == 0)
			usage_table[i] = 0;
		else {
			if (size < (0x100000 >> PAGE_SHIFT)) {
				/* less than 1MB */
				factor = 'K';
				size <<= PAGE_SHIFT - 10;
			} else {
				factor = 'M';
				size >>= 20 - PAGE_SHIFT;
			}
			/* RED-PEN: base can be > 32bit */ 
			len += seq_printf(seq, 
				   "reg%02i: base=0x%05lx000 (%4luMB), size=%4lu%cB: %s, count=%d\n",
			     i, base, base >> (20 - PAGE_SHIFT), size, factor,
			     mtrr_attrib_to_str(type), usage_table[i]);
		}
	}
	return 0;
}

static int __init mtrr_if_init(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if ((!cpu_has(c, X86_FEATURE_MTRR)) &&
	    (!cpu_has(c, X86_FEATURE_K6_MTRR)) &&
	    (!cpu_has(c, X86_FEATURE_CYRIX_ARR)) &&
	    (!cpu_has(c, X86_FEATURE_CENTAUR_MCR)))
		return -ENODEV;

	proc_root_mtrr =
	    create_proc_entry("mtrr", S_IWUSR | S_IRUGO, &proc_root);
	if (proc_root_mtrr) {
		proc_root_mtrr->owner = THIS_MODULE;
		proc_root_mtrr->proc_fops = &mtrr_fops;
	}
	return 0;
}

arch_initcall(mtrr_if_init);
#endif			/*  CONFIG_PROC_FS  */
