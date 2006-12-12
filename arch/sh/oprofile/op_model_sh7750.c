/*
 * arch/sh/oprofile/op_model_sh7750.c
 *
 * OProfile support for SH7750/SH7750S Performance Counters
 *
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/profile.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/notifier.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define PM_CR_BASE	0xff000084	/* 16-bit */
#define PM_CTR_BASE	0xff100004	/* 32-bit */

#define PMCR1		(PM_CR_BASE  + 0x00)
#define PMCR2		(PM_CR_BASE  + 0x04)
#define PMCTR1H		(PM_CTR_BASE + 0x00)
#define PMCTR1L		(PM_CTR_BASE + 0x04)
#define PMCTR2H		(PM_CTR_BASE + 0x08)
#define PMCTR2L		(PM_CTR_BASE + 0x0c)

#define PMCR_PMM_MASK	0x0000003f

#define PMCR_CLKF	0x00000100
#define PMCR_PMCLR	0x00002000
#define PMCR_PMST	0x00004000
#define PMCR_PMEN	0x00008000

#define PMCR_ENABLE	(PMCR_PMST | PMCR_PMEN)

/*
 * SH7750/SH7750S have 2 perf counters
 */
#define NR_CNTRS	2

extern const char *get_cpu_subtype(void);

struct op_counter_config {
	unsigned long enabled;
	unsigned long event;
	unsigned long count;

	/* Dummy values for userspace tool compliance */
	unsigned long kernel;
	unsigned long user;
	unsigned long unit_mask;
};

static struct op_counter_config ctr[NR_CNTRS];

/*
 * There are a number of events supported by each counter (33 in total).
 * Since we have 2 counters, each counter will take the event code as it
 * corresponds to the PMCR PMM setting. Each counter can be configured
 * independently.
 *
 *	Event Code	Description
 *	----------	-----------
 *
 *	0x01		Operand read access
 *	0x02		Operand write access
 *	0x03		UTLB miss
 *	0x04		Operand cache read miss
 *	0x05		Operand cache write miss
 *	0x06		Instruction fetch (w/ cache)
 *	0x07		Instruction TLB miss
 *	0x08		Instruction cache miss
 *	0x09		All operand accesses
 *	0x0a		All instruction accesses
 *	0x0b		OC RAM operand access
 *	0x0d		On-chip I/O space access
 *	0x0e		Operand access (r/w)
 *	0x0f		Operand cache miss (r/w)
 *	0x10		Branch instruction
 *	0x11		Branch taken
 *	0x12		BSR/BSRF/JSR
 *	0x13		Instruction execution
 *	0x14		Instruction execution in parallel
 *	0x15		FPU Instruction execution
 *	0x16		Interrupt
 *	0x17		NMI
 *	0x18		trapa instruction execution
 *	0x19		UBCA match
 *	0x1a		UBCB match
 *	0x21		Instruction cache fill
 *	0x22		Operand cache fill
 *	0x23		Elapsed time
 *	0x24		Pipeline freeze by I-cache miss
 *	0x25		Pipeline freeze by D-cache miss
 *	0x27		Pipeline freeze by branch instruction
 *	0x28		Pipeline freeze by CPU register
 *	0x29		Pipeline freeze by FPU
 *
 * Unfortunately we don't have a native exception or interrupt for counter
 * overflow (although since these counters can run for 16.3 days without
 * overflowing, it's not really necessary).
 *
 * OProfile on the other hand likes to have samples taken periodically, so
 * for now we just piggyback the timer interrupt to get the expected
 * behavior.
 */

static int sh7750_timer_notify(struct notifier_block *self,
			       unsigned long val, void *regs)
{
	oprofile_add_sample((struct pt_regs *)regs, 0);
	return 0;
}

static struct notifier_block sh7750_timer_notifier = {
	.notifier_call		= sh7750_timer_notify,
};

static u64 sh7750_read_counter(int counter)
{
	u32 hi, lo;

	hi = (counter == 0) ? ctrl_inl(PMCTR1H) : ctrl_inl(PMCTR2H);
	lo = (counter == 0) ? ctrl_inl(PMCTR1L) : ctrl_inl(PMCTR2L);

	return (u64)((u64)(hi & 0xffff) << 32) | lo;
}

/*
 * Files will be in a path like:
 *
 *  /<oprofilefs mount point>/<counter number>/<file>
 *
 * So when dealing with <file>, we look to the parent dentry for the counter
 * number.
 */
static inline int to_counter(struct file *file)
{
	const unsigned char *name = file->f_path.dentry->d_parent->d_name.name;

	return (int)simple_strtol(name, NULL, 10);
}

/*
 * XXX: We have 48-bit counters, so we're probably going to want something
 * more along the lines of oprofilefs_ullong_to_user().. Truncating to
 * unsigned long works fine for now though, as long as we don't attempt to
 * profile for too horribly long.
 */
static ssize_t sh7750_read_count(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	int counter = to_counter(file);
	u64 val = sh7750_read_counter(counter);

	return oprofilefs_ulong_to_user((unsigned long)val, buf, count, ppos);
}

static ssize_t sh7750_write_count(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	int counter = to_counter(file);
	unsigned long val;

	if (oprofilefs_ulong_from_user(&val, buf, count))
		return -EFAULT;

	/*
	 * Any write will clear the counter, although only 0 should be
	 * written for this purpose, as we do not support setting the
	 * counter to an arbitrary value.
	 */
	WARN_ON(val != 0);

	if (counter == 0) {
		ctrl_outw(ctrl_inw(PMCR1) | PMCR_PMCLR, PMCR1);
	} else {
		ctrl_outw(ctrl_inw(PMCR2) | PMCR_PMCLR, PMCR2);
	}

	return count;
}

static struct file_operations count_fops = {
	.read		= sh7750_read_count,
	.write		= sh7750_write_count,
};

static int sh7750_perf_counter_create_files(struct super_block *sb, struct dentry *root)
{
	int i;

	for (i = 0; i < NR_CNTRS; i++) {
		struct dentry *dir;
		char buf[4];

		snprintf(buf, sizeof(buf), "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);

		oprofilefs_create_ulong(sb, dir, "enabled", &ctr[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &ctr[i].event);
		oprofilefs_create_file(sb, dir, "count", &count_fops);

		/* Dummy entries */
		oprofilefs_create_ulong(sb, dir, "kernel", &ctr[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &ctr[i].user);
		oprofilefs_create_ulong(sb, dir, "unit_mask", &ctr[i].unit_mask);
	}

	return 0;
}

static int sh7750_perf_counter_start(void)
{
	u16 pmcr;

	/* Enable counter 1 */
	if (ctr[0].enabled) {
		pmcr = ctrl_inw(PMCR1);
		WARN_ON(pmcr & PMCR_PMEN);

		pmcr &= ~PMCR_PMM_MASK;
		pmcr |= ctr[0].event;
		ctrl_outw(pmcr | PMCR_ENABLE, PMCR1);
	}

	/* Enable counter 2 */
	if (ctr[1].enabled) {
		pmcr = ctrl_inw(PMCR2);
		WARN_ON(pmcr & PMCR_PMEN);

		pmcr &= ~PMCR_PMM_MASK;
		pmcr |= ctr[1].event;
		ctrl_outw(pmcr | PMCR_ENABLE, PMCR2);
	}

	return register_profile_notifier(&sh7750_timer_notifier);
}

static void sh7750_perf_counter_stop(void)
{
	ctrl_outw(ctrl_inw(PMCR1) & ~PMCR_PMEN, PMCR1);
	ctrl_outw(ctrl_inw(PMCR2) & ~PMCR_PMEN, PMCR2);

	unregister_profile_notifier(&sh7750_timer_notifier);
}

static struct oprofile_operations sh7750_perf_counter_ops = {
	.create_files	= sh7750_perf_counter_create_files,
	.start		= sh7750_perf_counter_start,
	.stop		= sh7750_perf_counter_stop,
};

int __init oprofile_arch_init(struct oprofile_operations **ops)
{
	if (!(cpu_data->flags & CPU_HAS_PERF_COUNTER))
		return -ENODEV;

	sh7750_perf_counter_ops.cpu_type = (char *)get_cpu_subtype();
	*ops = &sh7750_perf_counter_ops;

	printk(KERN_INFO "oprofile: using SH-4 (%s) performance monitoring.\n",
	       sh7750_perf_counter_ops.cpu_type);

	/* Clear the counters */
	ctrl_outw(ctrl_inw(PMCR1) | PMCR_PMCLR, PMCR1);
	ctrl_outw(ctrl_inw(PMCR2) | PMCR_PMCLR, PMCR2);

	return 0;
}

void oprofile_arch_exit(void)
{
}

