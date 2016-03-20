/*
 * Kprobe module for testing crash dumps
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
 * This module induces system failures at predefined crashpoints to
 * evaluate the reliability of crash dumps obtained using different dumping
 * solutions.
 *
 * It is adapted from the Linux Kernel Dump Test Tool by
 * Fernando Luis Vazquez Cao <http://lkdtt.sourceforge.net>
 *
 * Debugfs support added by Simon Kagstrom <simon.kagstrom@netinsight.net>
 *
 * See Documentation/fault-injection/provoke-crashes.txt for instructions
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_IDE
#include <linux/ide.h>
#endif

/*
 * Make sure our attempts to over run the kernel stack doesn't trigger
 * a compiler warning when CONFIG_FRAME_WARN is set. Then make sure we
 * recurse past the end of THREAD_SIZE by default.
 */
#if defined(CONFIG_FRAME_WARN) && (CONFIG_FRAME_WARN > 0)
#define REC_STACK_SIZE (CONFIG_FRAME_WARN / 2)
#else
#define REC_STACK_SIZE (THREAD_SIZE / 8)
#endif
#define REC_NUM_DEFAULT ((THREAD_SIZE / REC_STACK_SIZE) * 2)

#define DEFAULT_COUNT 10
#define EXEC_SIZE 64

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
	CT_EXEC_USERSPACE,
	CT_ACCESS_USERSPACE,
	CT_WRITE_RO,
	CT_WRITE_RO_AFTER_INIT,
	CT_WRITE_KERN,
	CT_WRAP_ATOMIC
};

static char* cp_name[] = {
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
	"EXEC_USERSPACE",
	"ACCESS_USERSPACE",
	"WRITE_RO",
	"WRITE_RO_AFTER_INIT",
	"WRITE_KERN",
	"WRAP_ATOMIC"
};

static struct jprobe lkdtm;

static int lkdtm_parse_commandline(void);
static void lkdtm_handler(void);

static char* cpoint_name;
static char* cpoint_type;
static int cpoint_count = DEFAULT_COUNT;
static int recur_count = REC_NUM_DEFAULT;

static enum cname cpoint = CN_INVALID;
static enum ctype cptype = CT_NONE;
static int count = DEFAULT_COUNT;
static DEFINE_SPINLOCK(count_lock);
static DEFINE_SPINLOCK(lock_me_up);

static u8 data_area[EXEC_SIZE];

static const unsigned long rodata = 0xAA55AA55;
static unsigned long ro_after_init __ro_after_init = 0x55AA5500;

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
			return i + 1;
	}

	return CT_NONE;
}

static const char *cp_type_to_str(enum ctype type)
{
	if (type == CT_NONE || type < 0 || type > ARRAY_SIZE(cp_type))
		return "None";

	return cp_type[type - 1];
}

static const char *cp_name_to_str(enum cname name)
{
	if (name == CN_INVALID || name < 0 || name > ARRAY_SIZE(cp_name))
		return "INVALID";

	return cp_name[name - 1];
}


static int lkdtm_parse_commandline(void)
{
	int i;
	unsigned long flags;

	if (cpoint_count < 1 || recur_count < 1)
		return -EINVAL;

	spin_lock_irqsave(&count_lock, flags);
	count = cpoint_count;
	spin_unlock_irqrestore(&count_lock, flags);

	/* No special parameters */
	if (!cpoint_type && !cpoint_name)
		return 0;

	/* Neither or both of these need to be set */
	if (!cpoint_type || !cpoint_name)
		return -EINVAL;

	cptype = parse_cp_type(cpoint_type, strlen(cpoint_type));
	if (cptype == CT_NONE)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cp_name); i++) {
		if (!strcmp(cpoint_name, cp_name[i])) {
			cpoint = i + 1;
			return 0;
		}
	}

	/* Could not find a valid crash point */
	return -EINVAL;
}

static int recursive_loop(int remaining)
{
	char buf[REC_STACK_SIZE];

	/* Make sure compiler does not optimize this away. */
	memset(buf, (remaining & 0xff) | 0x1, REC_STACK_SIZE);
	if (!remaining)
		return 0;
	else
		return recursive_loop(remaining - 1);
}

static void do_nothing(void)
{
	return;
}

/* Must immediately follow do_nothing for size calculuations to work out. */
static void do_overwritten(void)
{
	pr_info("do_overwritten wasn't overwritten!\n");
	return;
}

static noinline void corrupt_stack(void)
{
	/* Use default char array length that triggers stack protection. */
	char data[8];

	memset((void *)data, 0, 64);
}

static void noinline execute_location(void *dst)
{
	void (*func)(void) = dst;

	pr_info("attempting ok execution at %p\n", do_nothing);
	do_nothing();

	memcpy(dst, do_nothing, EXEC_SIZE);
	flush_icache_range((unsigned long)dst, (unsigned long)dst + EXEC_SIZE);
	pr_info("attempting bad execution at %p\n", func);
	func();
}

static void execute_user_location(void *dst)
{
	/* Intentionally crossing kernel/user memory boundary. */
	void (*func)(void) = dst;

	pr_info("attempting ok execution at %p\n", do_nothing);
	do_nothing();

	if (copy_to_user((void __user *)dst, do_nothing, EXEC_SIZE))
		return;
	flush_icache_range((unsigned long)dst, (unsigned long)dst + EXEC_SIZE);
	pr_info("attempting bad execution at %p\n", func);
	func();
}

static void lkdtm_do_action(enum ctype which)
{
	switch (which) {
	case CT_PANIC:
		panic("dumptest");
		break;
	case CT_BUG:
		BUG();
		break;
	case CT_WARNING:
		WARN_ON(1);
		break;
	case CT_EXCEPTION:
		*((int *) 0) = 0;
		break;
	case CT_LOOP:
		for (;;)
			;
		break;
	case CT_OVERFLOW:
		(void) recursive_loop(recur_count);
		break;
	case CT_CORRUPT_STACK:
		corrupt_stack();
		break;
	case CT_UNALIGNED_LOAD_STORE_WRITE: {
		static u8 data[5] __attribute__((aligned(4))) = {1, 2,
				3, 4, 5};
		u32 *p;
		u32 val = 0x12345678;

		p = (u32 *)(data + 1);
		if (*p == 0)
			val = 0x87654321;
		*p = val;
		 break;
	}
	case CT_OVERWRITE_ALLOCATION: {
		size_t len = 1020;
		u32 *data = kmalloc(len, GFP_KERNEL);

		data[1024 / sizeof(u32)] = 0x12345678;
		kfree(data);
		break;
	}
	case CT_WRITE_AFTER_FREE: {
		int *base, *again;
		size_t len = 1024;
		/*
		 * The slub allocator uses the first word to store the free
		 * pointer in some configurations. Use the middle of the
		 * allocation to avoid running into the freelist
		 */
		size_t offset = (len / sizeof(*base)) / 2;

		base = kmalloc(len, GFP_KERNEL);
		pr_info("Allocated memory %p-%p\n", base, &base[offset * 2]);
		pr_info("Attempting bad write to freed memory at %p\n",
			&base[offset]);
		kfree(base);
		base[offset] = 0x0abcdef0;
		/* Attempt to notice the overwrite. */
		again = kmalloc(len, GFP_KERNEL);
		kfree(again);
		if (again != base)
			pr_info("Hmm, didn't get the same memory range.\n");

		break;
	}
	case CT_READ_AFTER_FREE: {
		int *base, *val, saw;
		size_t len = 1024;
		/*
		 * The slub allocator uses the first word to store the free
		 * pointer in some configurations. Use the middle of the
		 * allocation to avoid running into the freelist
		 */
		size_t offset = (len / sizeof(*base)) / 2;

		base = kmalloc(len, GFP_KERNEL);
		if (!base)
			break;

		val = kmalloc(len, GFP_KERNEL);
		if (!val)
			break;

		*val = 0x12345678;
		base[offset] = *val;
		pr_info("Value in memory before free: %x\n", base[offset]);

		kfree(base);

		pr_info("Attempting bad read from freed memory\n");
		saw = base[offset];
		if (saw != *val) {
			/* Good! Poisoning happened, so declare a win. */
			pr_info("Memory correctly poisoned (%x)\n", saw);
			BUG();
		}
		pr_info("Memory was not poisoned\n");

		kfree(val);
		break;
	}
	case CT_WRITE_BUDDY_AFTER_FREE: {
		unsigned long p = __get_free_page(GFP_KERNEL);
		if (!p)
			break;
		pr_info("Writing to the buddy page before free\n");
		memset((void *)p, 0x3, PAGE_SIZE);
		free_page(p);
		schedule();
		pr_info("Attempting bad write to the buddy page after free\n");
		memset((void *)p, 0x78, PAGE_SIZE);
		/* Attempt to notice the overwrite. */
		p = __get_free_page(GFP_KERNEL);
		free_page(p);
		schedule();

		break;
	}
	case CT_READ_BUDDY_AFTER_FREE: {
		unsigned long p = __get_free_page(GFP_KERNEL);
		int saw, *val = kmalloc(1024, GFP_KERNEL);
		int *base;

		if (!p)
			break;

		if (!val)
			break;

		base = (int *)p;

		*val = 0x12345678;
		base[0] = *val;
		pr_info("Value in memory before free: %x\n", base[0]);
		free_page(p);
		pr_info("Attempting to read from freed memory\n");
		saw = base[0];
		if (saw != *val) {
			/* Good! Poisoning happened, so declare a win. */
			pr_info("Memory correctly poisoned (%x)\n", saw);
			BUG();
		}
		pr_info("Buddy page was not poisoned\n");

		kfree(val);
		break;
	}
	case CT_SOFTLOCKUP:
		preempt_disable();
		for (;;)
			cpu_relax();
		break;
	case CT_HARDLOCKUP:
		local_irq_disable();
		for (;;)
			cpu_relax();
		break;
	case CT_SPINLOCKUP:
		/* Must be called twice to trigger. */
		spin_lock(&lock_me_up);
		/* Let sparse know we intended to exit holding the lock. */
		__release(&lock_me_up);
		break;
	case CT_HUNG_TASK:
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
		break;
	case CT_EXEC_DATA:
		execute_location(data_area);
		break;
	case CT_EXEC_STACK: {
		u8 stack_area[EXEC_SIZE];
		execute_location(stack_area);
		break;
	}
	case CT_EXEC_KMALLOC: {
		u32 *kmalloc_area = kmalloc(EXEC_SIZE, GFP_KERNEL);
		execute_location(kmalloc_area);
		kfree(kmalloc_area);
		break;
	}
	case CT_EXEC_VMALLOC: {
		u32 *vmalloc_area = vmalloc(EXEC_SIZE);
		execute_location(vmalloc_area);
		vfree(vmalloc_area);
		break;
	}
	case CT_EXEC_USERSPACE: {
		unsigned long user_addr;

		user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
				    PROT_READ | PROT_WRITE | PROT_EXEC,
				    MAP_ANONYMOUS | MAP_PRIVATE, 0);
		if (user_addr >= TASK_SIZE) {
			pr_warn("Failed to allocate user memory\n");
			return;
		}
		execute_user_location((void *)user_addr);
		vm_munmap(user_addr, PAGE_SIZE);
		break;
	}
	case CT_ACCESS_USERSPACE: {
		unsigned long user_addr, tmp = 0;
		unsigned long *ptr;

		user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
				    PROT_READ | PROT_WRITE | PROT_EXEC,
				    MAP_ANONYMOUS | MAP_PRIVATE, 0);
		if (user_addr >= TASK_SIZE) {
			pr_warn("Failed to allocate user memory\n");
			return;
		}

		if (copy_to_user((void __user *)user_addr, &tmp, sizeof(tmp))) {
			pr_warn("copy_to_user failed\n");
			vm_munmap(user_addr, PAGE_SIZE);
			return;
		}

		ptr = (unsigned long *)user_addr;

		pr_info("attempting bad read at %p\n", ptr);
		tmp = *ptr;
		tmp += 0xc0dec0de;

		pr_info("attempting bad write at %p\n", ptr);
		*ptr = tmp;

		vm_munmap(user_addr, PAGE_SIZE);

		break;
	}
	case CT_WRITE_RO: {
		/* Explicitly cast away "const" for the test. */
		unsigned long *ptr = (unsigned long *)&rodata;

		pr_info("attempting bad rodata write at %p\n", ptr);
		*ptr ^= 0xabcd1234;

		break;
	}
	case CT_WRITE_RO_AFTER_INIT: {
		unsigned long *ptr = &ro_after_init;

		/*
		 * Verify we were written to during init. Since an Oops
		 * is considered a "success", a failure is to just skip the
		 * real test.
		 */
		if ((*ptr & 0xAA) != 0xAA) {
			pr_info("%p was NOT written during init!?\n", ptr);
			break;
		}

		pr_info("attempting bad ro_after_init write at %p\n", ptr);
		*ptr ^= 0xabcd1234;

		break;
	}
	case CT_WRITE_KERN: {
		size_t size;
		unsigned char *ptr;

		size = (unsigned long)do_overwritten -
		       (unsigned long)do_nothing;
		ptr = (unsigned char *)do_overwritten;

		pr_info("attempting bad %zu byte write at %p\n", size, ptr);
		memcpy(ptr, (unsigned char *)do_nothing, size);
		flush_icache_range((unsigned long)ptr,
				   (unsigned long)(ptr + size));

		do_overwritten();
		break;
	}
	case CT_WRAP_ATOMIC: {
		atomic_t under = ATOMIC_INIT(INT_MIN);
		atomic_t over = ATOMIC_INIT(INT_MAX);

		pr_info("attempting atomic underflow\n");
		atomic_dec(&under);
		pr_info("attempting atomic overflow\n");
		atomic_inc(&over);

		return;
	}
	case CT_NONE:
	default:
		break;
	}

}

static void lkdtm_handler(void)
{
	unsigned long flags;
	bool do_it = false;

	spin_lock_irqsave(&count_lock, flags);
	count--;
	pr_info("Crash point %s of type %s hit, trigger in %d rounds\n",
		cp_name_to_str(cpoint), cp_type_to_str(cptype), count);

	if (count == 0) {
		do_it = true;
		count = cpoint_count;
	}
	spin_unlock_irqrestore(&count_lock, flags);

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

	/* Make sure we can write to __ro_after_init values during __init */
	ro_after_init |= 0xAA;

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

	unregister_jprobe(&lkdtm);
	pr_info("Crash point unregistered\n");
}

module_init(lkdtm_module_init);
module_exit(lkdtm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kprobe module for testing crash dumps");
