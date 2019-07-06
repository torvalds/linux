// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux Kernel Dump Test Module for testing kernel crashes conditions:
 * induces system failures at predefined crashpoints and under predefined
 * operational conditions in order to evaluate the reliability of kernel
 * sanity checking and crash dumps obtained using different dumping
 * solutions.
 *
 * Copyright (C) IBM Corporation, 2006
 *
 * Author: Ankita Garg <ankita@in.ibm.com>
 *
 * It is adapted from the Linux Kernel Dump Test Tool by
 * Fernando Luis Vazquez Cao <http://lkdtt.sourceforge.net>
 *
 * Debugfs support added by Simon Kagstrom <simon.kagstrom@netinsight.net>
 *
 * See Documentation/fault-injection/provoke-crashes.txt for instructions
 */
#include "lkdtm.h"
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

#define DEFAULT_COUNT 10

static int lkdtm_debugfs_open(struct inode *inode, struct file *file);
static ssize_t lkdtm_debugfs_read(struct file *f, char __user *user_buf,
		size_t count, loff_t *off);
static ssize_t direct_entry(struct file *f, const char __user *user_buf,
			    size_t count, loff_t *off);

#ifdef CONFIG_KPROBES
static int lkdtm_kprobe_handler(struct kprobe *kp, struct pt_regs *regs);
static ssize_t lkdtm_debugfs_entry(struct file *f,
				   const char __user *user_buf,
				   size_t count, loff_t *off);
# define CRASHPOINT_KPROBE(_symbol)				\
		.kprobe = {					\
			.symbol_name = (_symbol),		\
			.pre_handler = lkdtm_kprobe_handler,	\
		},
# define CRASHPOINT_WRITE(_symbol)				\
		(_symbol) ? lkdtm_debugfs_entry : direct_entry
#else
# define CRASHPOINT_KPROBE(_symbol)
# define CRASHPOINT_WRITE(_symbol)		direct_entry
#endif

/* Crash points */
struct crashpoint {
	const char *name;
	const struct file_operations fops;
	struct kprobe kprobe;
};

#define CRASHPOINT(_name, _symbol)				\
	{							\
		.name = _name,					\
		.fops = {					\
			.read	= lkdtm_debugfs_read,		\
			.llseek	= generic_file_llseek,		\
			.open	= lkdtm_debugfs_open,		\
			.write	= CRASHPOINT_WRITE(_symbol)	\
		},						\
		CRASHPOINT_KPROBE(_symbol)			\
	}

/* Define the possible places where we can trigger a crash point. */
static struct crashpoint crashpoints[] = {
	CRASHPOINT("DIRECT",		 NULL),
#ifdef CONFIG_KPROBES
	CRASHPOINT("INT_HARDWARE_ENTRY", "do_IRQ"),
	CRASHPOINT("INT_HW_IRQ_EN",	 "handle_irq_event"),
	CRASHPOINT("INT_TASKLET_ENTRY",	 "tasklet_action"),
	CRASHPOINT("FS_DEVRW",		 "ll_rw_block"),
	CRASHPOINT("MEM_SWAPOUT",	 "shrink_inactive_list"),
	CRASHPOINT("TIMERADD",		 "hrtimer_start"),
	CRASHPOINT("SCSI_DISPATCH_CMD",	 "scsi_dispatch_cmd"),
	CRASHPOINT("IDE_CORE_CP",	 "generic_ide_ioctl"),
#endif
};


/* Crash types. */
struct crashtype {
	const char *name;
	void (*func)(void);
};

#define CRASHTYPE(_name)			\
	{					\
		.name = __stringify(_name),	\
		.func = lkdtm_ ## _name,	\
	}

/* Define the possible types of crashes that can be triggered. */
static const struct crashtype crashtypes[] = {
	CRASHTYPE(PANIC),
	CRASHTYPE(BUG),
	CRASHTYPE(WARNING),
	CRASHTYPE(EXCEPTION),
	CRASHTYPE(LOOP),
	CRASHTYPE(OVERFLOW),
	CRASHTYPE(CORRUPT_LIST_ADD),
	CRASHTYPE(CORRUPT_LIST_DEL),
	CRASHTYPE(CORRUPT_USER_DS),
	CRASHTYPE(CORRUPT_STACK),
	CRASHTYPE(CORRUPT_STACK_STRONG),
	CRASHTYPE(STACK_GUARD_PAGE_LEADING),
	CRASHTYPE(STACK_GUARD_PAGE_TRAILING),
	CRASHTYPE(UNALIGNED_LOAD_STORE_WRITE),
	CRASHTYPE(OVERWRITE_ALLOCATION),
	CRASHTYPE(WRITE_AFTER_FREE),
	CRASHTYPE(READ_AFTER_FREE),
	CRASHTYPE(WRITE_BUDDY_AFTER_FREE),
	CRASHTYPE(READ_BUDDY_AFTER_FREE),
	CRASHTYPE(SOFTLOCKUP),
	CRASHTYPE(HARDLOCKUP),
	CRASHTYPE(SPINLOCKUP),
	CRASHTYPE(HUNG_TASK),
	CRASHTYPE(EXEC_DATA),
	CRASHTYPE(EXEC_STACK),
	CRASHTYPE(EXEC_KMALLOC),
	CRASHTYPE(EXEC_VMALLOC),
	CRASHTYPE(EXEC_RODATA),
	CRASHTYPE(EXEC_USERSPACE),
	CRASHTYPE(EXEC_NULL),
	CRASHTYPE(ACCESS_USERSPACE),
	CRASHTYPE(ACCESS_NULL),
	CRASHTYPE(WRITE_RO),
	CRASHTYPE(WRITE_RO_AFTER_INIT),
	CRASHTYPE(WRITE_KERN),
	CRASHTYPE(REFCOUNT_INC_OVERFLOW),
	CRASHTYPE(REFCOUNT_ADD_OVERFLOW),
	CRASHTYPE(REFCOUNT_INC_NOT_ZERO_OVERFLOW),
	CRASHTYPE(REFCOUNT_ADD_NOT_ZERO_OVERFLOW),
	CRASHTYPE(REFCOUNT_DEC_ZERO),
	CRASHTYPE(REFCOUNT_DEC_NEGATIVE),
	CRASHTYPE(REFCOUNT_DEC_AND_TEST_NEGATIVE),
	CRASHTYPE(REFCOUNT_SUB_AND_TEST_NEGATIVE),
	CRASHTYPE(REFCOUNT_INC_ZERO),
	CRASHTYPE(REFCOUNT_ADD_ZERO),
	CRASHTYPE(REFCOUNT_INC_SATURATED),
	CRASHTYPE(REFCOUNT_DEC_SATURATED),
	CRASHTYPE(REFCOUNT_ADD_SATURATED),
	CRASHTYPE(REFCOUNT_INC_NOT_ZERO_SATURATED),
	CRASHTYPE(REFCOUNT_ADD_NOT_ZERO_SATURATED),
	CRASHTYPE(REFCOUNT_DEC_AND_TEST_SATURATED),
	CRASHTYPE(REFCOUNT_SUB_AND_TEST_SATURATED),
	CRASHTYPE(REFCOUNT_TIMING),
	CRASHTYPE(ATOMIC_TIMING),
	CRASHTYPE(USERCOPY_HEAP_SIZE_TO),
	CRASHTYPE(USERCOPY_HEAP_SIZE_FROM),
	CRASHTYPE(USERCOPY_HEAP_WHITELIST_TO),
	CRASHTYPE(USERCOPY_HEAP_WHITELIST_FROM),
	CRASHTYPE(USERCOPY_STACK_FRAME_TO),
	CRASHTYPE(USERCOPY_STACK_FRAME_FROM),
	CRASHTYPE(USERCOPY_STACK_BEYOND),
	CRASHTYPE(USERCOPY_KERNEL),
	CRASHTYPE(USERCOPY_KERNEL_DS),
	CRASHTYPE(STACKLEAK_ERASING),
};


/* Global kprobe entry and crashtype. */
static struct kprobe *lkdtm_kprobe;
static struct crashpoint *lkdtm_crashpoint;
static const struct crashtype *lkdtm_crashtype;

/* Module parameters */
static int recur_count = -1;
module_param(recur_count, int, 0644);
MODULE_PARM_DESC(recur_count, " Recursion level for the stack overflow test");

static char* cpoint_name;
module_param(cpoint_name, charp, 0444);
MODULE_PARM_DESC(cpoint_name, " Crash Point, where kernel is to be crashed");

static char* cpoint_type;
module_param(cpoint_type, charp, 0444);
MODULE_PARM_DESC(cpoint_type, " Crash Point Type, action to be taken on "\
				"hitting the crash point");

static int cpoint_count = DEFAULT_COUNT;
module_param(cpoint_count, int, 0644);
MODULE_PARM_DESC(cpoint_count, " Crash Point Count, number of times the "\
				"crash point is to be hit to trigger action");


/* Return the crashtype number or NULL if the name is invalid */
static const struct crashtype *find_crashtype(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(crashtypes); i++) {
		if (!strcmp(name, crashtypes[i].name))
			return &crashtypes[i];
	}

	return NULL;
}

/*
 * This is forced noinline just so it distinctly shows up in the stackdump
 * which makes validation of expected lkdtm crashes easier.
 */
static noinline void lkdtm_do_action(const struct crashtype *crashtype)
{
	if (WARN_ON(!crashtype || !crashtype->func))
		return;
	crashtype->func();
}

static int lkdtm_register_cpoint(struct crashpoint *crashpoint,
				 const struct crashtype *crashtype)
{
	int ret;

	/* If this doesn't have a symbol, just call immediately. */
	if (!crashpoint->kprobe.symbol_name) {
		lkdtm_do_action(crashtype);
		return 0;
	}

	if (lkdtm_kprobe != NULL)
		unregister_kprobe(lkdtm_kprobe);

	lkdtm_crashpoint = crashpoint;
	lkdtm_crashtype = crashtype;
	lkdtm_kprobe = &crashpoint->kprobe;
	ret = register_kprobe(lkdtm_kprobe);
	if (ret < 0) {
		pr_info("Couldn't register kprobe %s\n",
			crashpoint->kprobe.symbol_name);
		lkdtm_kprobe = NULL;
		lkdtm_crashpoint = NULL;
		lkdtm_crashtype = NULL;
	}

	return ret;
}

#ifdef CONFIG_KPROBES
/* Global crash counter and spinlock. */
static int crash_count = DEFAULT_COUNT;
static DEFINE_SPINLOCK(crash_count_lock);

/* Called by kprobe entry points. */
static int lkdtm_kprobe_handler(struct kprobe *kp, struct pt_regs *regs)
{
	unsigned long flags;
	bool do_it = false;

	if (WARN_ON(!lkdtm_crashpoint || !lkdtm_crashtype))
		return 0;

	spin_lock_irqsave(&crash_count_lock, flags);
	crash_count--;
	pr_info("Crash point %s of type %s hit, trigger in %d rounds\n",
		lkdtm_crashpoint->name, lkdtm_crashtype->name, crash_count);

	if (crash_count == 0) {
		do_it = true;
		crash_count = cpoint_count;
	}
	spin_unlock_irqrestore(&crash_count_lock, flags);

	if (do_it)
		lkdtm_do_action(lkdtm_crashtype);

	return 0;
}

static ssize_t lkdtm_debugfs_entry(struct file *f,
				   const char __user *user_buf,
				   size_t count, loff_t *off)
{
	struct crashpoint *crashpoint = file_inode(f)->i_private;
	const struct crashtype *crashtype = NULL;
	char *buf;
	int err;

	if (count >= PAGE_SIZE)
		return -EINVAL;

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (copy_from_user(buf, user_buf, count)) {
		free_page((unsigned long) buf);
		return -EFAULT;
	}
	/* NULL-terminate and remove enter */
	buf[count] = '\0';
	strim(buf);

	crashtype = find_crashtype(buf);
	free_page((unsigned long)buf);

	if (!crashtype)
		return -EINVAL;

	err = lkdtm_register_cpoint(crashpoint, crashtype);
	if (err < 0)
		return err;

	*off += count;

	return count;
}
#endif

/* Generic read callback that just prints out the available crash types */
static ssize_t lkdtm_debugfs_read(struct file *f, char __user *user_buf,
		size_t count, loff_t *off)
{
	char *buf;
	int i, n, out;

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	n = scnprintf(buf, PAGE_SIZE, "Available crash types:\n");
	for (i = 0; i < ARRAY_SIZE(crashtypes); i++) {
		n += scnprintf(buf + n, PAGE_SIZE - n, "%s\n",
			      crashtypes[i].name);
	}
	buf[n] = '\0';

	out = simple_read_from_buffer(user_buf, count, off,
				      buf, n);
	free_page((unsigned long) buf);

	return out;
}

static int lkdtm_debugfs_open(struct inode *inode, struct file *file)
{
	return 0;
}

/* Special entry to just crash directly. Available without KPROBEs */
static ssize_t direct_entry(struct file *f, const char __user *user_buf,
		size_t count, loff_t *off)
{
	const struct crashtype *crashtype;
	char *buf;

	if (count >= PAGE_SIZE)
		return -EINVAL;
	if (count < 1)
		return -EINVAL;

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (copy_from_user(buf, user_buf, count)) {
		free_page((unsigned long) buf);
		return -EFAULT;
	}
	/* NULL-terminate and remove enter */
	buf[count] = '\0';
	strim(buf);

	crashtype = find_crashtype(buf);
	free_page((unsigned long) buf);
	if (!crashtype)
		return -EINVAL;

	pr_info("Performing direct entry %s\n", crashtype->name);
	lkdtm_do_action(crashtype);
	*off += count;

	return count;
}

static struct dentry *lkdtm_debugfs_root;

static int __init lkdtm_module_init(void)
{
	struct crashpoint *crashpoint = NULL;
	const struct crashtype *crashtype = NULL;
	int ret = -EINVAL;
	int i;

	/* Neither or both of these need to be set */
	if ((cpoint_type || cpoint_name) && !(cpoint_type && cpoint_name)) {
		pr_err("Need both cpoint_type and cpoint_name or neither\n");
		return -EINVAL;
	}

	if (cpoint_type) {
		crashtype = find_crashtype(cpoint_type);
		if (!crashtype) {
			pr_err("Unknown crashtype '%s'\n", cpoint_type);
			return -EINVAL;
		}
	}

	if (cpoint_name) {
		for (i = 0; i < ARRAY_SIZE(crashpoints); i++) {
			if (!strcmp(cpoint_name, crashpoints[i].name))
				crashpoint = &crashpoints[i];
		}

		/* Refuse unknown crashpoints. */
		if (!crashpoint) {
			pr_err("Invalid crashpoint %s\n", cpoint_name);
			return -EINVAL;
		}
	}

#ifdef CONFIG_KPROBES
	/* Set crash count. */
	crash_count = cpoint_count;
#endif

	/* Handle test-specific initialization. */
	lkdtm_bugs_init(&recur_count);
	lkdtm_perms_init();
	lkdtm_usercopy_init();

	/* Register debugfs interface */
	lkdtm_debugfs_root = debugfs_create_dir("provoke-crash", NULL);
	if (!lkdtm_debugfs_root) {
		pr_err("creating root dir failed\n");
		return -ENODEV;
	}

	/* Install debugfs trigger files. */
	for (i = 0; i < ARRAY_SIZE(crashpoints); i++) {
		struct crashpoint *cur = &crashpoints[i];
		struct dentry *de;

		de = debugfs_create_file(cur->name, 0644, lkdtm_debugfs_root,
					 cur, &cur->fops);
		if (de == NULL) {
			pr_err("could not create crashpoint %s\n", cur->name);
			goto out_err;
		}
	}

	/* Install crashpoint if one was selected. */
	if (crashpoint) {
		ret = lkdtm_register_cpoint(crashpoint, crashtype);
		if (ret < 0) {
			pr_info("Invalid crashpoint %s\n", crashpoint->name);
			goto out_err;
		}
		pr_info("Crash point %s of type %s registered\n",
			crashpoint->name, cpoint_type);
	} else {
		pr_info("No crash points registered, enable through debugfs\n");
	}

	return 0;

out_err:
	debugfs_remove_recursive(lkdtm_debugfs_root);
	return ret;
}

static void __exit lkdtm_module_exit(void)
{
	debugfs_remove_recursive(lkdtm_debugfs_root);

	/* Handle test-specific clean-up. */
	lkdtm_usercopy_exit();

	if (lkdtm_kprobe != NULL)
		unregister_kprobe(lkdtm_kprobe);

	pr_info("Crash point unregistered\n");
}

module_init(lkdtm_module_init);
module_exit(lkdtm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel crash testing module");
