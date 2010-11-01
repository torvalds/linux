/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rwsem.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/cdev.h>
#include <linux/compat.h>
#include <asm/hardwall.h>
#include <asm/traps.h>
#include <asm/siginfo.h>
#include <asm/irq_regs.h>

#include <arch/interrupts.h>
#include <arch/spr_def.h>


/*
 * This data structure tracks the rectangle data, etc., associated
 * one-to-one with a "struct file *" from opening HARDWALL_FILE.
 * Note that the file's private data points back to this structure.
 */
struct hardwall_info {
	struct list_head list;             /* "rectangles" list */
	struct list_head task_head;        /* head of tasks in this hardwall */
	int ulhc_x;                        /* upper left hand corner x coord */
	int ulhc_y;                        /* upper left hand corner y coord */
	int width;                         /* rectangle width */
	int height;                        /* rectangle height */
	int teardown_in_progress;          /* are we tearing this one down? */
};

/* Currently allocated hardwall rectangles */
static LIST_HEAD(rectangles);

/*
 * Guard changes to the hardwall data structures.
 * This could be finer grained (e.g. one lock for the list of hardwall
 * rectangles, then separate embedded locks for each one's list of tasks),
 * but there are subtle correctness issues when trying to start with
 * a task's "hardwall" pointer and lock the correct rectangle's embedded
 * lock in the presence of a simultaneous deactivation, so it seems
 * easier to have a single lock, given that none of these data
 * structures are touched very frequently during normal operation.
 */
static DEFINE_SPINLOCK(hardwall_lock);

/* Allow disabling UDN access. */
static int udn_disabled;
static int __init noudn(char *str)
{
	pr_info("User-space UDN access is disabled\n");
	udn_disabled = 1;
	return 0;
}
early_param("noudn", noudn);


/*
 * Low-level primitives
 */

/* Set a CPU bit if the CPU is online. */
#define cpu_online_set(cpu, dst) do { \
	if (cpu_online(cpu))          \
		cpumask_set_cpu(cpu, dst);    \
} while (0)


/* Does the given rectangle contain the given x,y coordinate? */
static int contains(struct hardwall_info *r, int x, int y)
{
	return (x >= r->ulhc_x && x < r->ulhc_x + r->width) &&
		(y >= r->ulhc_y && y < r->ulhc_y + r->height);
}

/* Compute the rectangle parameters and validate the cpumask. */
static int setup_rectangle(struct hardwall_info *r, struct cpumask *mask)
{
	int x, y, cpu, ulhc, lrhc;

	/* The first cpu is the ULHC, the last the LRHC. */
	ulhc = find_first_bit(cpumask_bits(mask), nr_cpumask_bits);
	lrhc = find_last_bit(cpumask_bits(mask), nr_cpumask_bits);

	/* Compute the rectangle attributes from the cpus. */
	r->ulhc_x = cpu_x(ulhc);
	r->ulhc_y = cpu_y(ulhc);
	r->width = cpu_x(lrhc) - r->ulhc_x + 1;
	r->height = cpu_y(lrhc) - r->ulhc_y + 1;

	/* Width and height must be positive */
	if (r->width <= 0 || r->height <= 0)
		return -EINVAL;

	/* Confirm that the cpumask is exactly the rectangle. */
	for (y = 0, cpu = 0; y < smp_height; ++y)
		for (x = 0; x < smp_width; ++x, ++cpu)
			if (cpumask_test_cpu(cpu, mask) != contains(r, x, y))
				return -EINVAL;

	/*
	 * Note that offline cpus can't be drained when this UDN
	 * rectangle eventually closes.  We used to detect this
	 * situation and print a warning, but it annoyed users and
	 * they ignored it anyway, so now we just return without a
	 * warning.
	 */
	return 0;
}

/* Do the two given rectangles overlap on any cpu? */
static int overlaps(struct hardwall_info *a, struct hardwall_info *b)
{
	return a->ulhc_x + a->width > b->ulhc_x &&    /* A not to the left */
		b->ulhc_x + b->width > a->ulhc_x &&   /* B not to the left */
		a->ulhc_y + a->height > b->ulhc_y &&  /* A not above */
		b->ulhc_y + b->height > a->ulhc_y;    /* B not above */
}


/*
 * Hardware management of hardwall setup, teardown, trapping,
 * and enabling/disabling PL0 access to the networks.
 */

/* Bit field values to mask together for writes to SPR_XDN_DIRECTION_PROTECT */
enum direction_protect {
	N_PROTECT = (1 << 0),
	E_PROTECT = (1 << 1),
	S_PROTECT = (1 << 2),
	W_PROTECT = (1 << 3)
};

static void enable_firewall_interrupts(void)
{
	arch_local_irq_unmask_now(INT_UDN_FIREWALL);
}

static void disable_firewall_interrupts(void)
{
	arch_local_irq_mask_now(INT_UDN_FIREWALL);
}

/* Set up hardwall on this cpu based on the passed hardwall_info. */
static void hardwall_setup_ipi_func(void *info)
{
	struct hardwall_info *r = info;
	int cpu = smp_processor_id();
	int x = cpu % smp_width;
	int y = cpu / smp_width;
	int bits = 0;
	if (x == r->ulhc_x)
		bits |= W_PROTECT;
	if (x == r->ulhc_x + r->width - 1)
		bits |= E_PROTECT;
	if (y == r->ulhc_y)
		bits |= N_PROTECT;
	if (y == r->ulhc_y + r->height - 1)
		bits |= S_PROTECT;
	BUG_ON(bits == 0);
	__insn_mtspr(SPR_UDN_DIRECTION_PROTECT, bits);
	enable_firewall_interrupts();

}

/* Set up all cpus on edge of rectangle to enable/disable hardwall SPRs. */
static void hardwall_setup(struct hardwall_info *r)
{
	int x, y, cpu, delta;
	struct cpumask rect_cpus;

	cpumask_clear(&rect_cpus);

	/* First include the top and bottom edges */
	cpu = r->ulhc_y * smp_width + r->ulhc_x;
	delta = (r->height - 1) * smp_width;
	for (x = 0; x < r->width; ++x, ++cpu) {
		cpu_online_set(cpu, &rect_cpus);
		cpu_online_set(cpu + delta, &rect_cpus);
	}

	/* Then the left and right edges */
	cpu -= r->width;
	delta = r->width - 1;
	for (y = 0; y < r->height; ++y, cpu += smp_width) {
		cpu_online_set(cpu, &rect_cpus);
		cpu_online_set(cpu + delta, &rect_cpus);
	}

	/* Then tell all the cpus to set up their protection SPR */
	on_each_cpu_mask(&rect_cpus, hardwall_setup_ipi_func, r, 1);
}

void __kprobes do_hardwall_trap(struct pt_regs* regs, int fault_num)
{
	struct hardwall_info *rect;
	struct task_struct *p;
	struct siginfo info;
	int x, y;
	int cpu = smp_processor_id();
	int found_processes;
	unsigned long flags;

	struct pt_regs *old_regs = set_irq_regs(regs);
	irq_enter();

	/* This tile trapped a network access; find the rectangle. */
	x = cpu % smp_width;
	y = cpu / smp_width;
	spin_lock_irqsave(&hardwall_lock, flags);
	list_for_each_entry(rect, &rectangles, list) {
		if (contains(rect, x, y))
			break;
	}

	/*
	 * It shouldn't be possible not to find this cpu on the
	 * rectangle list, since only cpus in rectangles get hardwalled.
	 * The hardwall is only removed after the UDN is drained.
	 */
	BUG_ON(&rect->list == &rectangles);

	/*
	 * If we already started teardown on this hardwall, don't worry;
	 * the abort signal has been sent and we are just waiting for things
	 * to quiesce.
	 */
	if (rect->teardown_in_progress) {
		pr_notice("cpu %d: detected hardwall violation %#lx"
		       " while teardown already in progress\n",
		       cpu, (long) __insn_mfspr(SPR_UDN_DIRECTION_PROTECT));
		goto done;
	}

	/*
	 * Kill off any process that is activated in this rectangle.
	 * We bypass security to deliver the signal, since it must be
	 * one of the activated processes that generated the UDN
	 * message that caused this trap, and all the activated
	 * processes shared a single open file so are pretty tightly
	 * bound together from a security point of view to begin with.
	 */
	rect->teardown_in_progress = 1;
	wmb(); /* Ensure visibility of rectangle before notifying processes. */
	pr_notice("cpu %d: detected hardwall violation %#lx...\n",
	       cpu, (long) __insn_mfspr(SPR_UDN_DIRECTION_PROTECT));
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_HARDWALL;
	found_processes = 0;
	list_for_each_entry(p, &rect->task_head, thread.hardwall_list) {
		BUG_ON(p->thread.hardwall != rect);
		if (p->sighand) {
			found_processes = 1;
			pr_notice("hardwall: killing %d\n", p->pid);
			spin_lock(&p->sighand->siglock);
			__group_send_sig_info(info.si_signo, &info, p);
			spin_unlock(&p->sighand->siglock);
		}
	}
	if (!found_processes)
		pr_notice("hardwall: no associated processes!\n");

 done:
	spin_unlock_irqrestore(&hardwall_lock, flags);

	/*
	 * We have to disable firewall interrupts now, or else when we
	 * return from this handler, we will simply re-interrupt back to
	 * it.  However, we can't clear the protection bits, since we
	 * haven't yet drained the network, and that would allow packets
	 * to cross out of the hardwall region.
	 */
	disable_firewall_interrupts();

	irq_exit();
	set_irq_regs(old_regs);
}

/* Allow access from user space to the UDN. */
void grant_network_mpls(void)
{
	__insn_mtspr(SPR_MPL_UDN_ACCESS_SET_0, 1);
	__insn_mtspr(SPR_MPL_UDN_AVAIL_SET_0, 1);
	__insn_mtspr(SPR_MPL_UDN_COMPLETE_SET_0, 1);
	__insn_mtspr(SPR_MPL_UDN_TIMER_SET_0, 1);
#if !CHIP_HAS_REV1_XDN()
	__insn_mtspr(SPR_MPL_UDN_REFILL_SET_0, 1);
	__insn_mtspr(SPR_MPL_UDN_CA_SET_0, 1);
#endif
}

/* Deny access from user space to the UDN. */
void restrict_network_mpls(void)
{
	__insn_mtspr(SPR_MPL_UDN_ACCESS_SET_1, 1);
	__insn_mtspr(SPR_MPL_UDN_AVAIL_SET_1, 1);
	__insn_mtspr(SPR_MPL_UDN_COMPLETE_SET_1, 1);
	__insn_mtspr(SPR_MPL_UDN_TIMER_SET_1, 1);
#if !CHIP_HAS_REV1_XDN()
	__insn_mtspr(SPR_MPL_UDN_REFILL_SET_1, 1);
	__insn_mtspr(SPR_MPL_UDN_CA_SET_1, 1);
#endif
}


/*
 * Code to create, activate, deactivate, and destroy hardwall rectangles.
 */

/* Create a hardwall for the given rectangle */
static struct hardwall_info *hardwall_create(
	size_t size, const unsigned char __user *bits)
{
	struct hardwall_info *iter, *rect;
	struct cpumask mask;
	unsigned long flags;
	int rc;

	/* Reject crazy sizes out of hand, a la sys_mbind(). */
	if (size > PAGE_SIZE)
		return ERR_PTR(-EINVAL);

	/* Copy whatever fits into a cpumask. */
	if (copy_from_user(&mask, bits, min(sizeof(struct cpumask), size)))
		return ERR_PTR(-EFAULT);

	/*
	 * If the size was short, clear the rest of the mask;
	 * otherwise validate that the rest of the user mask was zero
	 * (we don't try hard to be efficient when validating huge masks).
	 */
	if (size < sizeof(struct cpumask)) {
		memset((char *)&mask + size, 0, sizeof(struct cpumask) - size);
	} else if (size > sizeof(struct cpumask)) {
		size_t i;
		for (i = sizeof(struct cpumask); i < size; ++i) {
			char c;
			if (get_user(c, &bits[i]))
				return ERR_PTR(-EFAULT);
			if (c)
				return ERR_PTR(-EINVAL);
		}
	}

	/* Allocate a new rectangle optimistically. */
	rect = kmalloc(sizeof(struct hardwall_info),
			GFP_KERNEL | __GFP_ZERO);
	if (rect == NULL)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&rect->task_head);

	/* Compute the rectangle size and validate that it's plausible. */
	rc = setup_rectangle(rect, &mask);
	if (rc != 0) {
		kfree(rect);
		return ERR_PTR(rc);
	}

	/* Confirm it doesn't overlap and add it to the list. */
	spin_lock_irqsave(&hardwall_lock, flags);
	list_for_each_entry(iter, &rectangles, list) {
		if (overlaps(iter, rect)) {
			spin_unlock_irqrestore(&hardwall_lock, flags);
			kfree(rect);
			return ERR_PTR(-EBUSY);
		}
	}
	list_add_tail(&rect->list, &rectangles);
	spin_unlock_irqrestore(&hardwall_lock, flags);

	/* Set up appropriate hardwalling on all affected cpus. */
	hardwall_setup(rect);

	return rect;
}

/* Activate a given hardwall on this cpu for this process. */
static int hardwall_activate(struct hardwall_info *rect)
{
	int cpu, x, y;
	unsigned long flags;
	struct task_struct *p = current;
	struct thread_struct *ts = &p->thread;

	/* Require a rectangle. */
	if (rect == NULL)
		return -ENODATA;

	/* Not allowed to activate a rectangle that is being torn down. */
	if (rect->teardown_in_progress)
		return -EINVAL;

	/*
	 * Get our affinity; if we're not bound to this tile uniquely,
	 * we can't access the network registers.
	 */
	if (cpumask_weight(&p->cpus_allowed) != 1)
		return -EPERM;

	/* Make sure we are bound to a cpu in this rectangle. */
	cpu = smp_processor_id();
	BUG_ON(cpumask_first(&p->cpus_allowed) != cpu);
	x = cpu_x(cpu);
	y = cpu_y(cpu);
	if (!contains(rect, x, y))
		return -EINVAL;

	/* If we are already bound to this hardwall, it's a no-op. */
	if (ts->hardwall) {
		BUG_ON(ts->hardwall != rect);
		return 0;
	}

	/* Success!  This process gets to use the user networks on this cpu. */
	ts->hardwall = rect;
	spin_lock_irqsave(&hardwall_lock, flags);
	list_add(&ts->hardwall_list, &rect->task_head);
	spin_unlock_irqrestore(&hardwall_lock, flags);
	grant_network_mpls();
	printk(KERN_DEBUG "Pid %d (%s) activated for hardwall: cpu %d\n",
	       p->pid, p->comm, cpu);
	return 0;
}

/*
 * Deactivate a task's hardwall.  Must hold hardwall_lock.
 * This method may be called from free_task(), so we don't want to
 * rely on too many fields of struct task_struct still being valid.
 * We assume the cpus_allowed, pid, and comm fields are still valid.
 */
static void _hardwall_deactivate(struct task_struct *task)
{
	struct thread_struct *ts = &task->thread;

	if (cpumask_weight(&task->cpus_allowed) != 1) {
		pr_err("pid %d (%s) releasing networks with"
		       " an affinity mask containing %d cpus!\n",
		       task->pid, task->comm,
		       cpumask_weight(&task->cpus_allowed));
		BUG();
	}

	BUG_ON(ts->hardwall == NULL);
	ts->hardwall = NULL;
	list_del(&ts->hardwall_list);
	if (task == current)
		restrict_network_mpls();
}

/* Deactivate a task's hardwall. */
int hardwall_deactivate(struct task_struct *task)
{
	unsigned long flags;
	int activated;

	spin_lock_irqsave(&hardwall_lock, flags);
	activated = (task->thread.hardwall != NULL);
	if (activated)
		_hardwall_deactivate(task);
	spin_unlock_irqrestore(&hardwall_lock, flags);

	if (!activated)
		return -EINVAL;

	printk(KERN_DEBUG "Pid %d (%s) deactivated for hardwall: cpu %d\n",
	       task->pid, task->comm, smp_processor_id());
	return 0;
}

/* Stop a UDN switch before draining the network. */
static void stop_udn_switch(void *ignored)
{
#if !CHIP_HAS_REV1_XDN()
	/* Freeze the switch and the demux. */
	__insn_mtspr(SPR_UDN_SP_FREEZE,
		     SPR_UDN_SP_FREEZE__SP_FRZ_MASK |
		     SPR_UDN_SP_FREEZE__DEMUX_FRZ_MASK |
		     SPR_UDN_SP_FREEZE__NON_DEST_EXT_MASK);
#endif
}

/* Drain all the state from a stopped switch. */
static void drain_udn_switch(void *ignored)
{
#if !CHIP_HAS_REV1_XDN()
	int i;
	int from_tile_words, ca_count;

	/* Empty out the 5 switch point fifos. */
	for (i = 0; i < 5; i++) {
		int words, j;
		__insn_mtspr(SPR_UDN_SP_FIFO_SEL, i);
		words = __insn_mfspr(SPR_UDN_SP_STATE) & 0xF;
		for (j = 0; j < words; j++)
			(void) __insn_mfspr(SPR_UDN_SP_FIFO_DATA);
		BUG_ON((__insn_mfspr(SPR_UDN_SP_STATE) & 0xF) != 0);
	}

	/* Dump out the 3 word fifo at top. */
	from_tile_words = (__insn_mfspr(SPR_UDN_DEMUX_STATUS) >> 10) & 0x3;
	for (i = 0; i < from_tile_words; i++)
		(void) __insn_mfspr(SPR_UDN_DEMUX_WRITE_FIFO);

	/* Empty out demuxes. */
	while (__insn_mfspr(SPR_UDN_DATA_AVAIL) & (1 << 0))
		(void) __tile_udn0_receive();
	while (__insn_mfspr(SPR_UDN_DATA_AVAIL) & (1 << 1))
		(void) __tile_udn1_receive();
	while (__insn_mfspr(SPR_UDN_DATA_AVAIL) & (1 << 2))
		(void) __tile_udn2_receive();
	while (__insn_mfspr(SPR_UDN_DATA_AVAIL) & (1 << 3))
		(void) __tile_udn3_receive();
	BUG_ON((__insn_mfspr(SPR_UDN_DATA_AVAIL) & 0xF) != 0);

	/* Empty out catch all. */
	ca_count = __insn_mfspr(SPR_UDN_DEMUX_CA_COUNT);
	for (i = 0; i < ca_count; i++)
		(void) __insn_mfspr(SPR_UDN_CA_DATA);
	BUG_ON(__insn_mfspr(SPR_UDN_DEMUX_CA_COUNT) != 0);

	/* Clear demux logic. */
	__insn_mtspr(SPR_UDN_DEMUX_CTL, 1);

	/*
	 * Write switch state; experimentation indicates that 0xc3000
	 * is an idle switch point.
	 */
	for (i = 0; i < 5; i++) {
		__insn_mtspr(SPR_UDN_SP_FIFO_SEL, i);
		__insn_mtspr(SPR_UDN_SP_STATE, 0xc3000);
	}
#endif
}

/* Reset random UDN state registers at boot up and during hardwall teardown. */
void reset_network_state(void)
{
#if !CHIP_HAS_REV1_XDN()
	/* Reset UDN coordinates to their standard value */
	unsigned int cpu = smp_processor_id();
	unsigned int x = cpu % smp_width;
	unsigned int y = cpu / smp_width;
#endif

	if (udn_disabled)
		return;

#if !CHIP_HAS_REV1_XDN()
	__insn_mtspr(SPR_UDN_TILE_COORD, (x << 18) | (y << 7));

	/* Set demux tags to predefined values and enable them. */
	__insn_mtspr(SPR_UDN_TAG_VALID, 0xf);
	__insn_mtspr(SPR_UDN_TAG_0, (1 << 0));
	__insn_mtspr(SPR_UDN_TAG_1, (1 << 1));
	__insn_mtspr(SPR_UDN_TAG_2, (1 << 2));
	__insn_mtspr(SPR_UDN_TAG_3, (1 << 3));
#endif

	/* Clear out other random registers so we have a clean slate. */
	__insn_mtspr(SPR_UDN_AVAIL_EN, 0);
	__insn_mtspr(SPR_UDN_DEADLOCK_TIMEOUT, 0);
#if !CHIP_HAS_REV1_XDN()
	__insn_mtspr(SPR_UDN_REFILL_EN, 0);
	__insn_mtspr(SPR_UDN_DEMUX_QUEUE_SEL, 0);
	__insn_mtspr(SPR_UDN_SP_FIFO_SEL, 0);
#endif

	/* Start the switch and demux. */
#if !CHIP_HAS_REV1_XDN()
	__insn_mtspr(SPR_UDN_SP_FREEZE, 0);
#endif
}

/* Restart a UDN switch after draining. */
static void restart_udn_switch(void *ignored)
{
	reset_network_state();

	/* Disable firewall interrupts. */
	__insn_mtspr(SPR_UDN_DIRECTION_PROTECT, 0);
	disable_firewall_interrupts();
}

/* Build a struct cpumask containing all valid tiles in bounding rectangle. */
static void fill_mask(struct hardwall_info *r, struct cpumask *result)
{
	int x, y, cpu;

	cpumask_clear(result);

	cpu = r->ulhc_y * smp_width + r->ulhc_x;
	for (y = 0; y < r->height; ++y, cpu += smp_width - r->width) {
		for (x = 0; x < r->width; ++x, ++cpu)
			cpu_online_set(cpu, result);
	}
}

/* Last reference to a hardwall is gone, so clear the network. */
static void hardwall_destroy(struct hardwall_info *rect)
{
	struct task_struct *task;
	unsigned long flags;
	struct cpumask mask;

	/* Make sure this file actually represents a rectangle. */
	if (rect == NULL)
		return;

	/*
	 * Deactivate any remaining tasks.  It's possible to race with
	 * some other thread that is exiting and hasn't yet called
	 * deactivate (when freeing its thread_info), so we carefully
	 * deactivate any remaining tasks before freeing the
	 * hardwall_info object itself.
	 */
	spin_lock_irqsave(&hardwall_lock, flags);
	list_for_each_entry(task, &rect->task_head, thread.hardwall_list)
		_hardwall_deactivate(task);
	spin_unlock_irqrestore(&hardwall_lock, flags);

	/* Drain the UDN. */
	printk(KERN_DEBUG "Clearing hardwall rectangle %dx%d %d,%d\n",
	       rect->width, rect->height, rect->ulhc_x, rect->ulhc_y);
	fill_mask(rect, &mask);
	on_each_cpu_mask(&mask, stop_udn_switch, NULL, 1);
	on_each_cpu_mask(&mask, drain_udn_switch, NULL, 1);

	/* Restart switch and disable firewall. */
	on_each_cpu_mask(&mask, restart_udn_switch, NULL, 1);

	/* Now free the rectangle from the list. */
	spin_lock_irqsave(&hardwall_lock, flags);
	BUG_ON(!list_empty(&rect->task_head));
	list_del(&rect->list);
	spin_unlock_irqrestore(&hardwall_lock, flags);
	kfree(rect);
}


/*
 * Dump hardwall state via /proc; initialized in arch/tile/sys/proc.c.
 */
int proc_tile_hardwall_show(struct seq_file *sf, void *v)
{
	struct hardwall_info *r;

	if (udn_disabled) {
		seq_printf(sf, "%dx%d 0,0 pids:\n", smp_width, smp_height);
		return 0;
	}

	spin_lock_irq(&hardwall_lock);
	list_for_each_entry(r, &rectangles, list) {
		struct task_struct *p;
		seq_printf(sf, "%dx%d %d,%d pids:",
			   r->width, r->height, r->ulhc_x, r->ulhc_y);
		list_for_each_entry(p, &r->task_head, thread.hardwall_list) {
			unsigned int cpu = cpumask_first(&p->cpus_allowed);
			unsigned int x = cpu % smp_width;
			unsigned int y = cpu / smp_width;
			seq_printf(sf, " %d@%d,%d", p->pid, x, y);
		}
		seq_printf(sf, "\n");
	}
	spin_unlock_irq(&hardwall_lock);
	return 0;
}


/*
 * Character device support via ioctl/close.
 */

static long hardwall_ioctl(struct file *file, unsigned int a, unsigned long b)
{
	struct hardwall_info *rect = file->private_data;

	if (_IOC_TYPE(a) != HARDWALL_IOCTL_BASE)
		return -EINVAL;

	switch (_IOC_NR(a)) {
	case _HARDWALL_CREATE:
		if (udn_disabled)
			return -ENOSYS;
		if (rect != NULL)
			return -EALREADY;
		rect = hardwall_create(_IOC_SIZE(a),
					(const unsigned char __user *)b);
		if (IS_ERR(rect))
			return PTR_ERR(rect);
		file->private_data = rect;
		return 0;

	case _HARDWALL_ACTIVATE:
		return hardwall_activate(rect);

	case _HARDWALL_DEACTIVATE:
		if (current->thread.hardwall != rect)
			return -EINVAL;
		return hardwall_deactivate(current);

	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_COMPAT
static long hardwall_compat_ioctl(struct file *file,
				  unsigned int a, unsigned long b)
{
	/* Sign-extend the argument so it can be used as a pointer. */
	return hardwall_ioctl(file, a, (unsigned long)compat_ptr(b));
}
#endif

/* The user process closed the file; revoke access to user networks. */
static int hardwall_flush(struct file *file, fl_owner_t owner)
{
	struct hardwall_info *rect = file->private_data;
	struct task_struct *task, *tmp;
	unsigned long flags;

	if (rect) {
		/*
		 * NOTE: if multiple threads are activated on this hardwall
		 * file, the other threads will continue having access to the
		 * UDN until they are context-switched out and back in again.
		 *
		 * NOTE: A NULL files pointer means the task is being torn
		 * down, so in that case we also deactivate it.
		 */
		spin_lock_irqsave(&hardwall_lock, flags);
		list_for_each_entry_safe(task, tmp, &rect->task_head,
					 thread.hardwall_list) {
			if (task->files == owner || task->files == NULL)
				_hardwall_deactivate(task);
		}
		spin_unlock_irqrestore(&hardwall_lock, flags);
	}

	return 0;
}

/* This hardwall is gone, so destroy it. */
static int hardwall_release(struct inode *inode, struct file *file)
{
	hardwall_destroy(file->private_data);
	return 0;
}

static const struct file_operations dev_hardwall_fops = {
	.open           = nonseekable_open,
	.unlocked_ioctl = hardwall_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = hardwall_compat_ioctl,
#endif
	.flush          = hardwall_flush,
	.release        = hardwall_release,
};

static struct cdev hardwall_dev;

static int __init dev_hardwall_init(void)
{
	int rc;
	dev_t dev;

	rc = alloc_chrdev_region(&dev, 0, 1, "hardwall");
	if (rc < 0)
		return rc;
	cdev_init(&hardwall_dev, &dev_hardwall_fops);
	rc = cdev_add(&hardwall_dev, dev, 1);
	if (rc < 0)
		return rc;

	return 0;
}
late_initcall(dev_hardwall_init);
