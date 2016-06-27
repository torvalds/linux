/*
 * Linux Kernel Dump Test Module for testing kernel crashes conditions:
 * induces system failures at predefined crashpoints and under predefined
 * operational conditions in order to evaluate the reliability of kernel
 * sanity checking and crash dumps obtained using different dumping
 * solutions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
#define pr_fmt(fmt) "lkdtm: " fmt

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <scsi/scsi_cmnd.h>
#include <linux/debugfs.h>

#ifdef CONFIG_IDE
#include <linux/ide.h>
#endif

#include "lkdtm.h"

enum cname {
	CN_INVALID,
	CN_INT_HARDWARE_ENTRY,
	CN_INT_HW_IRQ_EN,
	CN_INT_TASKLET_ENTRY,
	CN_FS_DEVRW,
	CN_MEM_SWAPOUT,
	CN_TIMERADD,
	CN_SCSI_DISPATCH_CMD,
	CN_IDE_CORE_CP,
	CN_DIRECT,
};

enum ctype {
	CT_NONE,
	CT_PANIC,
	CT_BUG,
	CT_WARNING,
	CT_EXCEPTION,
	CT_LOOP,
	CT_OVERFLOW,
	CT_CORRUPT_STACK,
	CT_UNALIGNED_LOAD_STORE_WRITE,
	CT_OVERWRITE_ALLOCATION,
	CT_WRITE_AFTER_FREE,
	CT_READ_AFTER_FREE,
	CT_WRITE_BUDDY_AFTER_FREE,
	CT_READ_BUDDY_AFTER_FREE,
	CT_SOFTLOCKUP,
	CT_HARDLOCKUP,
	CT_SPINLOCKUP,
	CT_HUNG_TASK,
	CT_EXEC_DATA,
	CT_EXEC_STACK,
	CT_EXEC_KMALLOC,
	CT_EXEC_VMALLOC,
	CT_EXEC_RODATA,
	CT_EXEC_USERSPACE,
	CT_ACCESS_USERSPACE,
	CT_WRITE_RO,
	CT_WRITE_RO_AFTER_INIT,
	CT_WRITE_KERN,
	CT_ATOMIC_UNDERFLOW,
	CT_ATOMIC_OVERFLOW,
	CT_USERCOPY_HEAP_SIZE_TO,
	CT_USERCOPY_HEAP_SIZE_FROM,
	CT_USERCOPY_HEAP_FLAG_TO,
	CT_USERCOPY_HEAP_FLAG_FROM,
	CT_USERCOPY_STACK_FRAME_TO,
	CT_USERCOPY_STACK_FRAME_FROM,
	CT_USERCOPY_STACK_BEYOND,
	CT_USERCOPY_KERNEL,
};

static char* cp_name[] = {
	"INVALID",
	"INT_HARDWARE_ENTRY",
	"INT_HW_IRQ_EN",
	"INT_TASKLET_ENTRY",
	"FS_DEVRW",
	"MEM_SWAPOUT",
	"TIMERADD",
	"SCSI_DISPATCH_CMD",
	"IDE_CORE_CP",
	"DIRECT",
};

static char* cp_type[] = {
	"NONE",
	"PANIC",
	"BUG",
	"WARNING",
	"EXCEPTION",
	"LOOP",
	"OVERFLOW",
	"CORRUPT_STACK",
	"UNALIGNED_LOAD_STORE_WRITE",
	"OVERWRITE_ALLOCATION",
	"WRITE_AFTER_FREE",
	"READ_AFTER_FREE",
	"WRITE_BUDDY_AFTER_FREE",
	"READ_BUDDY_AFTER_FREE",
	"SOFTLOCKUP",
	"HARDLOCKUP",
	"SPINLOCKUP",
	"HUNG_TASK",
	"EXEC_DATA",
	"EXEC_STACK",
	"EXEC_KMALLOC",
	"EXEC_VMALLOC",
	"EXEC_RODATA",
	"EXEC_USERSPACE",
	"ACCESS_USERSPACE",
	"WRITE_RO",
	"WRITE_RO_AFTER_INIT",
	"WRITE_KERN",
	"ATOMIC_UNDERFLOW",
	"ATOMIC_OVERFLOW",
	"USERCOPY_HEAP_SIZE_TO",
	"USERCOPY_HEAP_SIZE_FROM",
	"USERCOPY_HEAP_FLAG_TO",
	"USERCOPY_HEAP_FLAG_FROM",
	"USERCOPY_STACK_FRAME_TO",
	"USERCOPY_STACK_FRAME_FROM",
	"USERCOPY_STACK_BEYOND",
	"USERCOPY_KERNEL",
};

static struct jprobe lkdtm;

static int lkdtm_parse_commandline(void);
static void lkdtm_handler(void);

#define DEFAULT_COUNT 10
static char* cpoint_name;
static char* cpoint_type;
static int cpoint_count = DEFAULT_COUNT;
static int recur_count = -1;
static int crash_count = DEFAULT_COUNT;
static DEFINE_SPINLOCK(crash_count_lock);

static enum cname cpoint = CN_INVALID;
static enum ctype cptype = CT_NONE;

module_param(recur_count, int, 0644);
MODULE_PARM_DESC(recur_count, " Recursion level for the stack overflow test");
module_param(cpoint_name, charp, 0444);
MODULE_PARM_DESC(cpoint_name, " Crash Point, where kernel is to be crashed");
module_param(cpoint_type, charp, 0444);
MODULE_PARM_DESC(cpoint_type, " Crash Point Type, action to be taken on "\
				"hitting the crash point");
module_param(cpoint_count, int, 0644);
MODULE_PARM_DESC(cpoint_count, " Crash Point Count, number of times the "\
				"crash point is to be hit to trigger action");

static unsigned int jp_do_irq(unsigned int irq)
{
	lkdtm_handler();
	jprobe_return();
	return 0;
}

static irqreturn_t jp_handle_irq_event(unsigned int irq,
				       struct irqaction *action)
{
	lkdtm_handler();
	jprobe_return();
	return 0;
}

static void jp_tasklet_action(struct softirq_action *a)
{
	lkdtm_handler();
	jprobe_return();
}

static void jp_ll_rw_block(int rw, int nr, struct buffer_head *bhs[])
{
	lkdtm_handler();
	jprobe_return();
}

struct scan_control;

static unsigned long jp_shrink_inactive_list(unsigned long max_scan,
					     struct zone *zone,
					     struct scan_control *sc)
{
	lkdtm_handler();
	jprobe_return();
	return 0;
}

static int jp_hrtimer_start(struct hrtimer *timer, ktime_t tim,
			    const enum hrtimer_mode mode)
{
	lkdtm_handler();
	jprobe_return();
	return 0;
}

static int jp_scsi_dispatch_cmd(struct scsi_cmnd *cmd)
{
	lkdtm_handler();
	jprobe_return();
	return 0;
}

#ifdef CONFIG_IDE
static int jp_generic_ide_ioctl(ide_drive_t *drive, struct file *file,
			struct block_device *bdev, unsigned int cmd,
			unsigned long arg)
{
	lkdtm_handler();
	jprobe_return();
	return 0;
}
#endif

/* Return the crashpoint number or NONE if the name is invalid */
static enum ctype parse_cp_type(const char *what, size_t count)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cp_type); i++) {
		if (!strcmp(what, cp_type[i]))
			return i;
	}

	return CT_NONE;
}

static const char *cp_type_to_str(enum ctype type)
{
	if (type == CT_NONE || type < 0 || type > ARRAY_SIZE(cp_type))
		return "NONE";

	return cp_type[type];
}

static const char *cp_name_to_str(enum cname name)
{
	if (name == CN_INVALID || name < 0 || name > ARRAY_SIZE(cp_name))
		return "INVALID";

	return cp_name[name];
}


static int lkdtm_parse_commandline(void)
{
	int i;
	unsigned long flags;

	if (cpoint_count < 1 || recur_count < 1)
		return -EINVAL;

	spin_lock_irqsave(&crash_count_lock, flags);
	crash_count = cpoint_count;
	spin_unlock_irqrestore(&crash_count_lock, flags);

	/* No special parameters */
	if (!cpoint_type && !cpoint_name)
		return 0;

	/* Neither or both of these need to be set */
	if (!cpoint_type || !cpoint_name)
		return -EINVAL;

	cptype = parse_cp_type(cpoint_type, strlen(cpoint_type));
	if (cptype == CT_NONE)
		return -EINVAL;

	/* Refuse INVALID as a selectable crashpoint name. */
	if (!strcmp(cpoint_name, "INVALID"))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cp_name); i++) {
		if (!strcmp(cpoint_name, cp_name[i])) {
			cpoint = i;
			return 0;
		}
	}

	/* Could not find a valid crash point */
	return -EINVAL;
}

static void lkdtm_do_action(enum ctype which)
{
	switch (which) {
	case CT_PANIC:
		lkdtm_PANIC();
		break;
	case CT_BUG:
		lkdtm_BUG();
		break;
	case CT_WARNING:
		lkdtm_WARNING();
		break;
	case CT_EXCEPTION:
		lkdtm_EXCEPTION();
		break;
	case CT_LOOP:
		lkdtm_LOOP();
		break;
	case CT_OVERFLOW:
		lkdtm_OVERFLOW();
		break;
	case CT_CORRUPT_STACK:
		lkdtm_CORRUPT_STACK();
		break;
	case CT_UNALIGNED_LOAD_STORE_WRITE:
		lkdtm_UNALIGNED_LOAD_STORE_WRITE();
		break;
	case CT_OVERWRITE_ALLOCATION:
		lkdtm_OVERWRITE_ALLOCATION();
		break;
	case CT_WRITE_AFTER_FREE:
		lkdtm_WRITE_AFTER_FREE();
		break;
	case CT_READ_AFTER_FREE:
		lkdtm_READ_AFTER_FREE();
		break;
	case CT_WRITE_BUDDY_AFTER_FREE:
		lkdtm_WRITE_BUDDY_AFTER_FREE();
		break;
	case CT_READ_BUDDY_AFTER_FREE:
		lkdtm_READ_BUDDY_AFTER_FREE();
		break;
	case CT_SOFTLOCKUP:
		lkdtm_SOFTLOCKUP();
		break;
	case CT_HARDLOCKUP:
		lkdtm_HARDLOCKUP();
		break;
	case CT_SPINLOCKUP:
		lkdtm_SPINLOCKUP();
		break;
	case CT_HUNG_TASK:
		lkdtm_HUNG_TASK();
		break;
	case CT_EXEC_DATA:
		lkdtm_EXEC_DATA();
		break;
	case CT_EXEC_STACK:
		lkdtm_EXEC_STACK();
		break;
	case CT_EXEC_KMALLOC:
		lkdtm_EXEC_KMALLOC();
		break;
	case CT_EXEC_VMALLOC:
		lkdtm_EXEC_VMALLOC();
		break;
	case CT_EXEC_RODATA:
		lkdtm_EXEC_RODATA();
		break;
	case CT_EXEC_USERSPACE:
		lkdtm_EXEC_USERSPACE();
		break;
	case CT_ACCESS_USERSPACE:
		lkdtm_ACCESS_USERSPACE();
		break;
	case CT_WRITE_RO:
		lkdtm_WRITE_RO();
		break;
	case CT_WRITE_RO_AFTER_INIT:
		lkdtm_WRITE_RO_AFTER_INIT();
		break;
	case CT_WRITE_KERN:
		lkdtm_WRITE_KERN();
		break;
	case CT_ATOMIC_UNDERFLOW:
		lkdtm_ATOMIC_UNDERFLOW();
		break;
	case CT_ATOMIC_OVERFLOW:
		lkdtm_ATOMIC_OVERFLOW();
		break;
	case CT_USERCOPY_HEAP_SIZE_TO:
		lkdtm_USERCOPY_HEAP_SIZE_TO();
		break;
	case CT_USERCOPY_HEAP_SIZE_FROM:
		lkdtm_USERCOPY_HEAP_SIZE_FROM();
		break;
	case CT_USERCOPY_HEAP_FLAG_TO:
		lkdtm_USERCOPY_HEAP_FLAG_TO();
		break;
	case CT_USERCOPY_HEAP_FLAG_FROM:
		lkdtm_USERCOPY_HEAP_FLAG_FROM();
		break;
	case CT_USERCOPY_STACK_FRAME_TO:
		lkdtm_USERCOPY_STACK_FRAME_TO();
		break;
	case CT_USERCOPY_STACK_FRAME_FROM:
		lkdtm_USERCOPY_STACK_FRAME_FROM();
		break;
	case CT_USERCOPY_STACK_BEYOND:
		lkdtm_USERCOPY_STACK_BEYOND();
		break;
	case CT_USERCOPY_KERNEL:
		lkdtm_USERCOPY_KERNEL();
		break;
	case CT_NONE:
	default:
		break;
	}

}

static void lkdtm_handler(void)
{
	unsigned long flags;
	bool do_it = false;

	spin_lock_irqsave(&crash_count_lock, flags);
	crash_count--;
	pr_info("Crash point %s of type %s hit, trigger in %d rounds\n",
		cp_name_to_str(cpoint), cp_type_to_str(cptype), crash_count);

	if (crash_count == 0) {
		do_it = true;
		crash_count = cpoint_count;
	}
	spin_unlock_irqrestore(&crash_count_lock, flags);

	if (do_it)
		lkdtm_do_action(cptype);
}

static int lkdtm_register_cpoint(enum cname which)
{
	int ret;

	cpoint = CN_INVALID;
	if (lkdtm.entry != NULL)
		unregister_jprobe(&lkdtm);

	switch (which) {
	case CN_DIRECT:
		lkdtm_do_action(cptype);
		return 0;
	case CN_INT_HARDWARE_ENTRY:
		lkdtm.kp.symbol_name = "do_IRQ";
		lkdtm.entry = (kprobe_opcode_t*) jp_do_irq;
		break;
	case CN_INT_HW_IRQ_EN:
		lkdtm.kp.symbol_name = "handle_IRQ_event";
		lkdtm.entry = (kprobe_opcode_t*) jp_handle_irq_event;
		break;
	case CN_INT_TASKLET_ENTRY:
		lkdtm.kp.symbol_name = "tasklet_action";
		lkdtm.entry = (kprobe_opcode_t*) jp_tasklet_action;
		break;
	case CN_FS_DEVRW:
		lkdtm.kp.symbol_name = "ll_rw_block";
		lkdtm.entry = (kprobe_opcode_t*) jp_ll_rw_block;
		break;
	case CN_MEM_SWAPOUT:
		lkdtm.kp.symbol_name = "shrink_inactive_list";
		lkdtm.entry = (kprobe_opcode_t*) jp_shrink_inactive_list;
		break;
	case CN_TIMERADD:
		lkdtm.kp.symbol_name = "hrtimer_start";
		lkdtm.entry = (kprobe_opcode_t*) jp_hrtimer_start;
		break;
	case CN_SCSI_DISPATCH_CMD:
		lkdtm.kp.symbol_name = "scsi_dispatch_cmd";
		lkdtm.entry = (kprobe_opcode_t*) jp_scsi_dispatch_cmd;
		break;
	case CN_IDE_CORE_CP:
#ifdef CONFIG_IDE
		lkdtm.kp.symbol_name = "generic_ide_ioctl";
		lkdtm.entry = (kprobe_opcode_t*) jp_generic_ide_ioctl;
#else
		pr_info("Crash point not available\n");
		return -EINVAL;
#endif
		break;
	default:
		pr_info("Invalid Crash Point\n");
		return -EINVAL;
	}

	cpoint = which;
	if ((ret = register_jprobe(&lkdtm)) < 0) {
		pr_info("Couldn't register jprobe\n");
		cpoint = CN_INVALID;
	}

	return ret;
}

static ssize_t do_register_entry(enum cname which, struct file *f,
		const char __user *user_buf, size_t count, loff_t *off)
{
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

	cptype = parse_cp_type(buf, count);
	free_page((unsigned long) buf);

	if (cptype == CT_NONE)
		return -EINVAL;

	err = lkdtm_register_cpoint(which);
	if (err < 0)
		return err;

	*off += count;

	return count;
}

/* Generic read callback that just prints out the available crash types */
static ssize_t lkdtm_debugfs_read(struct file *f, char __user *user_buf,
		size_t count, loff_t *off)
{
	char *buf;
	int i, n, out;

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	n = snprintf(buf, PAGE_SIZE, "Available crash types:\n");
	for (i = 0; i < ARRAY_SIZE(cp_type); i++)
		n += snprintf(buf + n, PAGE_SIZE - n, "%s\n", cp_type[i]);
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


static ssize_t int_hardware_entry(struct file *f, const char __user *buf,
		size_t count, loff_t *off)
{
	return do_register_entry(CN_INT_HARDWARE_ENTRY, f, buf, count, off);
}

static ssize_t int_hw_irq_en(struct file *f, const char __user *buf,
		size_t count, loff_t *off)
{
	return do_register_entry(CN_INT_HW_IRQ_EN, f, buf, count, off);
}

static ssize_t int_tasklet_entry(struct file *f, const char __user *buf,
		size_t count, loff_t *off)
{
	return do_register_entry(CN_INT_TASKLET_ENTRY, f, buf, count, off);
}

static ssize_t fs_devrw_entry(struct file *f, const char __user *buf,
		size_t count, loff_t *off)
{
	return do_register_entry(CN_FS_DEVRW, f, buf, count, off);
}

static ssize_t mem_swapout_entry(struct file *f, const char __user *buf,
		size_t count, loff_t *off)
{
	return do_register_entry(CN_MEM_SWAPOUT, f, buf, count, off);
}

static ssize_t timeradd_entry(struct file *f, const char __user *buf,
		size_t count, loff_t *off)
{
	return do_register_entry(CN_TIMERADD, f, buf, count, off);
}

static ssize_t scsi_dispatch_cmd_entry(struct file *f,
		const char __user *buf, size_t count, loff_t *off)
{
	return do_register_entry(CN_SCSI_DISPATCH_CMD, f, buf, count, off);
}

static ssize_t ide_core_cp_entry(struct file *f, const char __user *buf,
		size_t count, loff_t *off)
{
	return do_register_entry(CN_IDE_CORE_CP, f, buf, count, off);
}

/* Special entry to just crash directly. Available without KPROBEs */
static ssize_t direct_entry(struct file *f, const char __user *user_buf,
		size_t count, loff_t *off)
{
	enum ctype type;
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

	type = parse_cp_type(buf, count);
	free_page((unsigned long) buf);
	if (type == CT_NONE)
		return -EINVAL;

	pr_info("Performing direct entry %s\n", cp_type_to_str(type));
	lkdtm_do_action(type);
	*off += count;

	return count;
}

struct crash_entry {
	const char *name;
	const struct file_operations fops;
};

static const struct crash_entry crash_entries[] = {
	{"DIRECT", {.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = direct_entry} },
	{"INT_HARDWARE_ENTRY", {.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = int_hardware_entry} },
	{"INT_HW_IRQ_EN", {.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = int_hw_irq_en} },
	{"INT_TASKLET_ENTRY", {.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = int_tasklet_entry} },
	{"FS_DEVRW", {.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = fs_devrw_entry} },
	{"MEM_SWAPOUT", {.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = mem_swapout_entry} },
	{"TIMERADD", {.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = timeradd_entry} },
	{"SCSI_DISPATCH_CMD", {.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = scsi_dispatch_cmd_entry} },
	{"IDE_CORE_CP",	{.read = lkdtm_debugfs_read,
			.llseek = generic_file_llseek,
			.open = lkdtm_debugfs_open,
			.write = ide_core_cp_entry} },
};

static struct dentry *lkdtm_debugfs_root;

static int __init lkdtm_module_init(void)
{
	int ret = -EINVAL;
	int n_debugfs_entries = 1; /* Assume only the direct entry */
	int i;

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

#ifdef CONFIG_KPROBES
	n_debugfs_entries = ARRAY_SIZE(crash_entries);
#endif

	for (i = 0; i < n_debugfs_entries; i++) {
		const struct crash_entry *cur = &crash_entries[i];
		struct dentry *de;

		de = debugfs_create_file(cur->name, 0644, lkdtm_debugfs_root,
				NULL, &cur->fops);
		if (de == NULL) {
			pr_err("could not create %s\n", cur->name);
			goto out_err;
		}
	}

	if (lkdtm_parse_commandline() == -EINVAL) {
		pr_info("Invalid command\n");
		goto out_err;
	}

	if (cpoint != CN_INVALID && cptype != CT_NONE) {
		ret = lkdtm_register_cpoint(cpoint);
		if (ret < 0) {
			pr_info("Invalid crash point %d\n", cpoint);
			goto out_err;
		}
		pr_info("Crash point %s of type %s registered\n",
			cpoint_name, cpoint_type);
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

	unregister_jprobe(&lkdtm);
	pr_info("Crash point unregistered\n");
}

module_init(lkdtm_module_init);
module_exit(lkdtm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kprobe module for testing crash dumps");
