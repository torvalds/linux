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
 * Implement a per-cpu "hardwall" resource class such as UDN or IPI.
 * We use "hardwall" nomenclature throughout for historical reasons.
 * The lock here controls access to the list data structure as well as
 * to the items on the list.
 */
struct hardwall_type {
	int index;
	int is_xdn;
	int is_idn;
	int disabled;
	const char *name;
	struct list_head list;
	spinlock_t lock;
	struct proc_dir_entry *proc_dir;
};

enum hardwall_index {
	HARDWALL_UDN = 0,
#ifndef __tilepro__
	HARDWALL_IDN = 1,
	HARDWALL_IPI = 2,
#endif
	_HARDWALL_TYPES
};

static struct hardwall_type hardwall_types[] = {
	{  /* user-space access to UDN */
		0,
		1,
		0,
		0,
		"udn",
		LIST_HEAD_INIT(hardwall_types[HARDWALL_UDN].list),
		__SPIN_LOCK_UNLOCKED(hardwall_types[HARDWALL_UDN].lock),
		NULL
	},
#ifndef __tilepro__
	{  /* user-space access to IDN */
		1,
		1,
		1,
		1,  /* disabled pending hypervisor support */
		"idn",
		LIST_HEAD_INIT(hardwall_types[HARDWALL_IDN].list),
		__SPIN_LOCK_UNLOCKED(hardwall_types[HARDWALL_IDN].lock),
		NULL
	},
	{  /* access to user-space IPI */
		2,
		0,
		0,
		0,
		"ipi",
		LIST_HEAD_INIT(hardwall_types[HARDWALL_IPI].list),
		__SPIN_LOCK_UNLOCKED(hardwall_types[HARDWALL_IPI].lock),
		NULL
	},
#endif
};

/*
 * This data structure tracks the cpu data, etc., associated
 * one-to-one with a "struct file *" from opening a hardwall device file.
 * Note that the file's private data points back to this structure.
 */
struct hardwall_info {
	struct list_head list;             /* for hardwall_types.list */
	struct list_head task_head;        /* head of tasks in this hardwall */
	struct hardwall_type *type;        /* type of this resource */
	struct cpumask cpumask;            /* cpus reserved */
	int id;                            /* integer id for this hardwall */
	int teardown_in_progress;          /* are we tearing this one down? */

	/* Remaining fields only valid for user-network resources. */
	int ulhc_x;                        /* upper left hand corner x coord */
	int ulhc_y;                        /* upper left hand corner y coord */
	int width;                         /* rectangle width */
	int height;                        /* rectangle height */
#if CHIP_HAS_REV1_XDN()
	atomic_t xdn_pending_count;        /* cores in phase 1 of drain */
#endif
};


/* /proc/tile/hardwall */
static struct proc_dir_entry *hardwall_proc_dir;

/* Functions to manage files in /proc/tile/hardwall. */
static void hardwall_add_proc(struct hardwall_info *);
static void hardwall_remove_proc(struct hardwall_info *);

/* Allow disabling UDN access. */
static int __init noudn(char *str)
{
	pr_info("User-space UDN access is disabled\n");
	hardwall_types[HARDWALL_UDN].disabled = 1;
	return 0;
}
early_param("noudn", noudn);

#ifndef __tilepro__
/* Allow disabling IDN access. */
static int __init noidn(char *str)
{
	pr_info("User-space IDN access is disabled\n");
	hardwall_types[HARDWALL_IDN].disabled = 1;
	return 0;
}
early_param("noidn", noidn);

/* Allow disabling IPI access. */
static int __init noipi(char *str)
{
	pr_info("User-space IPI access is disabled\n");
	hardwall_types[HARDWALL_IPI].disabled = 1;
	return 0;
}
early_param("noipi", noipi);
#endif


/*
 * Low-level primitives for UDN/IDN
 */

#ifdef __tilepro__
#define mtspr_XDN(hwt, name, val) \
	do { (void)(hwt); __insn_mtspr(SPR_UDN_##name, (val)); } while (0)
#define mtspr_MPL_XDN(hwt, name, val) \
	do { (void)(hwt); __insn_mtspr(SPR_MPL_UDN_##name, (val)); } while (0)
#define mfspr_XDN(hwt, name) \
	((void)(hwt), __insn_mfspr(SPR_UDN_##name))
#else
#define mtspr_XDN(hwt, name, val)					\
	do {								\
		if ((hwt)->is_idn)					\
			__insn_mtspr(SPR_IDN_##name, (val));		\
		else							\
			__insn_mtspr(SPR_UDN_##name, (val));		\
	} while (0)
#define mtspr_MPL_XDN(hwt, name, val)					\
	do {								\
		if ((hwt)->is_idn)					\
			__insn_mtspr(SPR_MPL_IDN_##name, (val));	\
		else							\
			__insn_mtspr(SPR_MPL_UDN_##name, (val));	\
	} while (0)
#define mfspr_XDN(hwt, name) \
  ((hwt)->is_idn ? __insn_mfspr(SPR_IDN_##name) : __insn_mfspr(SPR_UDN_##name))
#endif

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
static int check_rectangle(struct hardwall_info *r, struct cpumask *mask)
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
	 * Note that offline cpus can't be drained when this user network
	 * rectangle eventually closes.  We used to detect this
	 * situation and print a warning, but it annoyed users and
	 * they ignored it anyway, so now we just return without a
	 * warning.
	 */
	return 0;
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
	W_PROTECT = (1 << 3),
	C_PROTECT = (1 << 4),
};

static inline int xdn_which_interrupt(struct hardwall_type *hwt)
{
#ifndef __tilepro__
	if (hwt->is_idn)
		return INT_IDN_FIREWALL;
#endif
	return INT_UDN_FIREWALL;
}

static void enable_firewall_interrupts(struct hardwall_type *hwt)
{
	arch_local_irq_unmask_now(xdn_which_interrupt(hwt));
}

static void disable_firewall_interrupts(struct hardwall_type *hwt)
{
	arch_local_irq_mask_now(xdn_which_interrupt(hwt));
}

/* Set up hardwall on this cpu based on the passed hardwall_info. */
static void hardwall_setup_func(void *info)
{
	struct hardwall_info *r = info;
	struct hardwall_type *hwt = r->type;

	int cpu = smp_processor_id();  /* on_each_cpu disables preemption */
	int x = cpu_x(cpu);
	int y = cpu_y(cpu);
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
	mtspr_XDN(hwt, DIRECTION_PROTECT, bits);
	enable_firewall_interrupts(hwt);
}

/* Set up all cpus on edge of rectangle to enable/disable hardwall SPRs. */
static void hardwall_protect_rectangle(struct hardwall_info *r)
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
	on_each_cpu_mask(&rect_cpus, hardwall_setup_func, r, 1);
}

/* Entered from INT_xDN_FIREWALL interrupt vector with irqs disabled. */
void __kprobes do_hardwall_trap(struct pt_regs* regs, int fault_num)
{
	struct hardwall_info *rect;
	struct hardwall_type *hwt;
	struct task_struct *p;
	struct siginfo info;
	int cpu = smp_processor_id();
	int found_processes;
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	/* Figure out which network trapped. */
	switch (fault_num) {
#ifndef __tilepro__
	case INT_IDN_FIREWALL:
		hwt = &hardwall_types[HARDWALL_IDN];
		break;
#endif
	case INT_UDN_FIREWALL:
		hwt = &hardwall_types[HARDWALL_UDN];
		break;
	default:
		BUG();
	}
	BUG_ON(hwt->disabled);

	/* This tile trapped a network access; find the rectangle. */
	spin_lock(&hwt->lock);
	list_for_each_entry(rect, &hwt->list, list) {
		if (cpumask_test_cpu(cpu, &rect->cpumask))
			break;
	}

	/*
	 * It shouldn't be possible not to find this cpu on the
	 * rectangle list, since only cpus in rectangles get hardwalled.
	 * The hardwall is only removed after the user network is drained.
	 */
	BUG_ON(&rect->list == &hwt->list);

	/*
	 * If we already started teardown on this hardwall, don't worry;
	 * the abort signal has been sent and we are just waiting for things
	 * to quiesce.
	 */
	if (rect->teardown_in_progress) {
		pr_notice("cpu %d: detected %s hardwall violation %#lx while teardown already in progress\n",
			  cpu, hwt->name,
			  (long)mfspr_XDN(hwt, DIRECTION_PROTECT));
		goto done;
	}

	/*
	 * Kill off any process that is activated in this rectangle.
	 * We bypass security to deliver the signal, since it must be
	 * one of the activated processes that generated the user network
	 * message that caused this trap, and all the activated
	 * processes shared a single open file so are pretty tightly
	 * bound together from a security point of view to begin with.
	 */
	rect->teardown_in_progress = 1;
	wmb(); /* Ensure visibility of rectangle before notifying processes. */
	pr_notice("cpu %d: detected %s hardwall violation %#lx...\n",
		  cpu, hwt->name, (long)mfspr_XDN(hwt, DIRECTION_PROTECT));
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_HARDWALL;
	found_processes = 0;
	list_for_each_entry(p, &rect->task_head,
			    thread.hardwall[hwt->index].list) {
		BUG_ON(p->thread.hardwall[hwt->index].info != rect);
		if (!(p->flags & PF_EXITING)) {
			found_processes = 1;
			pr_notice("hardwall: killing %d\n", p->pid);
			do_send_sig_info(info.si_signo, &info, p, false);
		}
	}
	if (!found_processes)
		pr_notice("hardwall: no associated processes!\n");

 done:
	spin_unlock(&hwt->lock);

	/*
	 * We have to disable firewall interrupts now, or else when we
	 * return from this handler, we will simply re-interrupt back to
	 * it.  However, we can't clear the protection bits, since we
	 * haven't yet drained the network, and that would allow packets
	 * to cross out of the hardwall region.
	 */
	disable_firewall_interrupts(hwt);

	irq_exit();
	set_irq_regs(old_regs);
}

/* Allow access from user space to the user network. */
void grant_hardwall_mpls(struct hardwall_type *hwt)
{
#ifndef __tilepro__
	if (!hwt->is_xdn) {
		__insn_mtspr(SPR_MPL_IPI_0_SET_0, 1);
		return;
	}
#endif
	mtspr_MPL_XDN(hwt, ACCESS_SET_0, 1);
	mtspr_MPL_XDN(hwt, AVAIL_SET_0, 1);
	mtspr_MPL_XDN(hwt, COMPLETE_SET_0, 1);
	mtspr_MPL_XDN(hwt, TIMER_SET_0, 1);
#if !CHIP_HAS_REV1_XDN()
	mtspr_MPL_XDN(hwt, REFILL_SET_0, 1);
	mtspr_MPL_XDN(hwt, CA_SET_0, 1);
#endif
}

/* Deny access from user space to the user network. */
void restrict_hardwall_mpls(struct hardwall_type *hwt)
{
#ifndef __tilepro__
	if (!hwt->is_xdn) {
		__insn_mtspr(SPR_MPL_IPI_0_SET_1, 1);
		return;
	}
#endif
	mtspr_MPL_XDN(hwt, ACCESS_SET_1, 1);
	mtspr_MPL_XDN(hwt, AVAIL_SET_1, 1);
	mtspr_MPL_XDN(hwt, COMPLETE_SET_1, 1);
	mtspr_MPL_XDN(hwt, TIMER_SET_1, 1);
#if !CHIP_HAS_REV1_XDN()
	mtspr_MPL_XDN(hwt, REFILL_SET_1, 1);
	mtspr_MPL_XDN(hwt, CA_SET_1, 1);
#endif
}

/* Restrict or deny as necessary for the task we're switching to. */
void hardwall_switch_tasks(struct task_struct *prev,
			   struct task_struct *next)
{
	int i;
	for (i = 0; i < HARDWALL_TYPES; ++i) {
		if (prev->thread.hardwall[i].info != NULL) {
			if (next->thread.hardwall[i].info == NULL)
				restrict_hardwall_mpls(&hardwall_types[i]);
		} else if (next->thread.hardwall[i].info != NULL) {
			grant_hardwall_mpls(&hardwall_types[i]);
		}
	}
}

/* Does this task have the right to IPI the given cpu? */
int hardwall_ipi_valid(int cpu)
{
#ifdef __tilegx__
	struct hardwall_info *info =
		current->thread.hardwall[HARDWALL_IPI].info;
	return info && cpumask_test_cpu(cpu, &info->cpumask);
#else
	return 0;
#endif
}

/*
 * Code to create, activate, deactivate, and destroy hardwall resources.
 */

/* Create a hardwall for the given resource */
static struct hardwall_info *hardwall_create(struct hardwall_type *hwt,
					     size_t size,
					     const unsigned char __user *bits)
{
	struct hardwall_info *iter, *info;
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

	/* Allocate a new hardwall_info optimistically. */
	info = kmalloc(sizeof(struct hardwall_info),
			GFP_KERNEL | __GFP_ZERO);
	if (info == NULL)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&info->task_head);
	info->type = hwt;

	/* Compute the rectangle size and validate that it's plausible. */
	cpumask_copy(&info->cpumask, &mask);
	info->id = find_first_bit(cpumask_bits(&mask), nr_cpumask_bits);
	if (hwt->is_xdn) {
		rc = check_rectangle(info, &mask);
		if (rc != 0) {
			kfree(info);
			return ERR_PTR(rc);
		}
	}

	/*
	 * Eliminate cpus that are not part of this Linux client.
	 * Note that this allows for configurations that we might not want to
	 * support, such as one client on every even cpu, another client on
	 * every odd cpu.
	 */
	cpumask_and(&info->cpumask, &info->cpumask, cpu_online_mask);

	/* Confirm it doesn't overlap and add it to the list. */
	spin_lock_irqsave(&hwt->lock, flags);
	list_for_each_entry(iter, &hwt->list, list) {
		if (cpumask_intersects(&iter->cpumask, &info->cpumask)) {
			spin_unlock_irqrestore(&hwt->lock, flags);
			kfree(info);
			return ERR_PTR(-EBUSY);
		}
	}
	list_add_tail(&info->list, &hwt->list);
	spin_unlock_irqrestore(&hwt->lock, flags);

	/* Set up appropriate hardwalling on all affected cpus. */
	if (hwt->is_xdn)
		hardwall_protect_rectangle(info);

	/* Create a /proc/tile/hardwall entry. */
	hardwall_add_proc(info);

	return info;
}

/* Activate a given hardwall on this cpu for this process. */
static int hardwall_activate(struct hardwall_info *info)
{
	int cpu;
	unsigned long flags;
	struct task_struct *p = current;
	struct thread_struct *ts = &p->thread;
	struct hardwall_type *hwt;

	/* Require a hardwall. */
	if (info == NULL)
		return -ENODATA;

	/* Not allowed to activate a hardwall that is being torn down. */
	if (info->teardown_in_progress)
		return -EINVAL;

	/*
	 * Get our affinity; if we're not bound to this tile uniquely,
	 * we can't access the network registers.
	 */
	if (cpumask_weight(&p->cpus_allowed) != 1)
		return -EPERM;

	/* Make sure we are bound to a cpu assigned to this resource. */
	cpu = smp_processor_id();
	BUG_ON(cpumask_first(&p->cpus_allowed) != cpu);
	if (!cpumask_test_cpu(cpu, &info->cpumask))
		return -EINVAL;

	/* If we are already bound to this hardwall, it's a no-op. */
	hwt = info->type;
	if (ts->hardwall[hwt->index].info) {
		BUG_ON(ts->hardwall[hwt->index].info != info);
		return 0;
	}

	/* Success!  This process gets to use the resource on this cpu. */
	ts->hardwall[hwt->index].info = info;
	spin_lock_irqsave(&hwt->lock, flags);
	list_add(&ts->hardwall[hwt->index].list, &info->task_head);
	spin_unlock_irqrestore(&hwt->lock, flags);
	grant_hardwall_mpls(hwt);
	printk(KERN_DEBUG "Pid %d (%s) activated for %s hardwall: cpu %d\n",
	       p->pid, p->comm, hwt->name, cpu);
	return 0;
}

/*
 * Deactivate a task's hardwall.  Must hold lock for hardwall_type.
 * This method may be called from exit_thread(), so we don't want to
 * rely on too many fields of struct task_struct still being valid.
 * We assume the cpus_allowed, pid, and comm fields are still valid.
 */
static void _hardwall_deactivate(struct hardwall_type *hwt,
				 struct task_struct *task)
{
	struct thread_struct *ts = &task->thread;

	if (cpumask_weight(&task->cpus_allowed) != 1) {
		pr_err("pid %d (%s) releasing %s hardwall with an affinity mask containing %d cpus!\n",
		       task->pid, task->comm, hwt->name,
		       cpumask_weight(&task->cpus_allowed));
		BUG();
	}

	BUG_ON(ts->hardwall[hwt->index].info == NULL);
	ts->hardwall[hwt->index].info = NULL;
	list_del(&ts->hardwall[hwt->index].list);
	if (task == current)
		restrict_hardwall_mpls(hwt);
}

/* Deactivate a task's hardwall. */
static int hardwall_deactivate(struct hardwall_type *hwt,
			       struct task_struct *task)
{
	unsigned long flags;
	int activated;

	spin_lock_irqsave(&hwt->lock, flags);
	activated = (task->thread.hardwall[hwt->index].info != NULL);
	if (activated)
		_hardwall_deactivate(hwt, task);
	spin_unlock_irqrestore(&hwt->lock, flags);

	if (!activated)
		return -EINVAL;

	printk(KERN_DEBUG "Pid %d (%s) deactivated for %s hardwall: cpu %d\n",
	       task->pid, task->comm, hwt->name, raw_smp_processor_id());
	return 0;
}

void hardwall_deactivate_all(struct task_struct *task)
{
	int i;
	for (i = 0; i < HARDWALL_TYPES; ++i)
		if (task->thread.hardwall[i].info)
			hardwall_deactivate(&hardwall_types[i], task);
}

/* Stop the switch before draining the network. */
static void stop_xdn_switch(void *arg)
{
#if !CHIP_HAS_REV1_XDN()
	/* Freeze the switch and the demux. */
	__insn_mtspr(SPR_UDN_SP_FREEZE,
		     SPR_UDN_SP_FREEZE__SP_FRZ_MASK |
		     SPR_UDN_SP_FREEZE__DEMUX_FRZ_MASK |
		     SPR_UDN_SP_FREEZE__NON_DEST_EXT_MASK);
#else
	/*
	 * Drop all packets bound for the core or off the edge.
	 * We rely on the normal hardwall protection setup code
	 * to have set the low four bits to trigger firewall interrupts,
	 * and shift those bits up to trigger "drop on send" semantics,
	 * plus adding "drop on send to core" for all switches.
	 * In practice it seems the switches latch the DIRECTION_PROTECT
	 * SPR so they won't start dropping if they're already
	 * delivering the last message to the core, but it doesn't
	 * hurt to enable it here.
	 */
	struct hardwall_type *hwt = arg;
	unsigned long protect = mfspr_XDN(hwt, DIRECTION_PROTECT);
	mtspr_XDN(hwt, DIRECTION_PROTECT, (protect | C_PROTECT) << 5);
#endif
}

static void empty_xdn_demuxes(struct hardwall_type *hwt)
{
#ifndef __tilepro__
	if (hwt->is_idn) {
		while (__insn_mfspr(SPR_IDN_DATA_AVAIL) & (1 << 0))
			(void) __tile_idn0_receive();
		while (__insn_mfspr(SPR_IDN_DATA_AVAIL) & (1 << 1))
			(void) __tile_idn1_receive();
		return;
	}
#endif
	while (__insn_mfspr(SPR_UDN_DATA_AVAIL) & (1 << 0))
		(void) __tile_udn0_receive();
	while (__insn_mfspr(SPR_UDN_DATA_AVAIL) & (1 << 1))
		(void) __tile_udn1_receive();
	while (__insn_mfspr(SPR_UDN_DATA_AVAIL) & (1 << 2))
		(void) __tile_udn2_receive();
	while (__insn_mfspr(SPR_UDN_DATA_AVAIL) & (1 << 3))
		(void) __tile_udn3_receive();
}

/* Drain all the state from a stopped switch. */
static void drain_xdn_switch(void *arg)
{
	struct hardwall_info *info = arg;
	struct hardwall_type *hwt = info->type;

#if CHIP_HAS_REV1_XDN()
	/*
	 * The switches have been configured to drop any messages
	 * destined for cores (or off the edge of the rectangle).
	 * But the current message may continue to be delivered,
	 * so we wait until all the cores have finished any pending
	 * messages before we stop draining.
	 */
	int pending = mfspr_XDN(hwt, PENDING);
	while (pending--) {
		empty_xdn_demuxes(hwt);
		if (hwt->is_idn)
			__tile_idn_send(0);
		else
			__tile_udn_send(0);
	}
	atomic_dec(&info->xdn_pending_count);
	while (atomic_read(&info->xdn_pending_count))
		empty_xdn_demuxes(hwt);
#else
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
	empty_xdn_demuxes(hwt);

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

/* Reset random XDN state registers at boot up and during hardwall teardown. */
static void reset_xdn_network_state(struct hardwall_type *hwt)
{
	if (hwt->disabled)
		return;

	/* Clear out other random registers so we have a clean slate. */
	mtspr_XDN(hwt, DIRECTION_PROTECT, 0);
	mtspr_XDN(hwt, AVAIL_EN, 0);
	mtspr_XDN(hwt, DEADLOCK_TIMEOUT, 0);

#if !CHIP_HAS_REV1_XDN()
	/* Reset UDN coordinates to their standard value */
	{
		unsigned int cpu = smp_processor_id();
		unsigned int x = cpu_x(cpu);
		unsigned int y = cpu_y(cpu);
		__insn_mtspr(SPR_UDN_TILE_COORD, (x << 18) | (y << 7));
	}

	/* Set demux tags to predefined values and enable them. */
	__insn_mtspr(SPR_UDN_TAG_VALID, 0xf);
	__insn_mtspr(SPR_UDN_TAG_0, (1 << 0));
	__insn_mtspr(SPR_UDN_TAG_1, (1 << 1));
	__insn_mtspr(SPR_UDN_TAG_2, (1 << 2));
	__insn_mtspr(SPR_UDN_TAG_3, (1 << 3));

	/* Set other rev0 random registers to a clean state. */
	__insn_mtspr(SPR_UDN_REFILL_EN, 0);
	__insn_mtspr(SPR_UDN_DEMUX_QUEUE_SEL, 0);
	__insn_mtspr(SPR_UDN_SP_FIFO_SEL, 0);

	/* Start the switch and demux. */
	__insn_mtspr(SPR_UDN_SP_FREEZE, 0);
#endif
}

void reset_network_state(void)
{
	reset_xdn_network_state(&hardwall_types[HARDWALL_UDN]);
#ifndef __tilepro__
	reset_xdn_network_state(&hardwall_types[HARDWALL_IDN]);
#endif
}

/* Restart an XDN switch after draining. */
static void restart_xdn_switch(void *arg)
{
	struct hardwall_type *hwt = arg;

#if CHIP_HAS_REV1_XDN()
	/* One last drain step to avoid races with injection and draining. */
	empty_xdn_demuxes(hwt);
#endif

	reset_xdn_network_state(hwt);

	/* Disable firewall interrupts. */
	disable_firewall_interrupts(hwt);
}

/* Last reference to a hardwall is gone, so clear the network. */
static void hardwall_destroy(struct hardwall_info *info)
{
	struct task_struct *task;
	struct hardwall_type *hwt;
	unsigned long flags;

	/* Make sure this file actually represents a hardwall. */
	if (info == NULL)
		return;

	/*
	 * Deactivate any remaining tasks.  It's possible to race with
	 * some other thread that is exiting and hasn't yet called
	 * deactivate (when freeing its thread_info), so we carefully
	 * deactivate any remaining tasks before freeing the
	 * hardwall_info object itself.
	 */
	hwt = info->type;
	info->teardown_in_progress = 1;
	spin_lock_irqsave(&hwt->lock, flags);
	list_for_each_entry(task, &info->task_head,
			    thread.hardwall[hwt->index].list)
		_hardwall_deactivate(hwt, task);
	spin_unlock_irqrestore(&hwt->lock, flags);

	if (hwt->is_xdn) {
		/* Configure the switches for draining the user network. */
		printk(KERN_DEBUG
		       "Clearing %s hardwall rectangle %dx%d %d,%d\n",
		       hwt->name, info->width, info->height,
		       info->ulhc_x, info->ulhc_y);
		on_each_cpu_mask(&info->cpumask, stop_xdn_switch, hwt, 1);

		/* Drain the network. */
#if CHIP_HAS_REV1_XDN()
		atomic_set(&info->xdn_pending_count,
			   cpumask_weight(&info->cpumask));
		on_each_cpu_mask(&info->cpumask, drain_xdn_switch, info, 0);
#else
		on_each_cpu_mask(&info->cpumask, drain_xdn_switch, info, 1);
#endif

		/* Restart switch and disable firewall. */
		on_each_cpu_mask(&info->cpumask, restart_xdn_switch, hwt, 1);
	}

	/* Remove the /proc/tile/hardwall entry. */
	hardwall_remove_proc(info);

	/* Now free the hardwall from the list. */
	spin_lock_irqsave(&hwt->lock, flags);
	BUG_ON(!list_empty(&info->task_head));
	list_del(&info->list);
	spin_unlock_irqrestore(&hwt->lock, flags);
	kfree(info);
}


static int hardwall_proc_show(struct seq_file *sf, void *v)
{
	struct hardwall_info *info = sf->private;

	seq_printf(sf, "%*pbl\n", cpumask_pr_args(&info->cpumask));
	return 0;
}

static int hardwall_proc_open(struct inode *inode,
			      struct file *file)
{
	return single_open(file, hardwall_proc_show, PDE_DATA(inode));
}

static const struct file_operations hardwall_proc_fops = {
	.open		= hardwall_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void hardwall_add_proc(struct hardwall_info *info)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%d", info->id);
	proc_create_data(buf, 0444, info->type->proc_dir,
			 &hardwall_proc_fops, info);
}

static void hardwall_remove_proc(struct hardwall_info *info)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%d", info->id);
	remove_proc_entry(buf, info->type->proc_dir);
}

int proc_pid_hardwall(struct seq_file *m, struct pid_namespace *ns,
		      struct pid *pid, struct task_struct *task)
{
	int i;
	int n = 0;
	for (i = 0; i < HARDWALL_TYPES; ++i) {
		struct hardwall_info *info = task->thread.hardwall[i].info;
		if (info)
			seq_printf(m, "%s: %d\n", info->type->name, info->id);
	}
	return n;
}

void proc_tile_hardwall_init(struct proc_dir_entry *root)
{
	int i;
	for (i = 0; i < HARDWALL_TYPES; ++i) {
		struct hardwall_type *hwt = &hardwall_types[i];
		if (hwt->disabled)
			continue;
		if (hardwall_proc_dir == NULL)
			hardwall_proc_dir = proc_mkdir("hardwall", root);
		hwt->proc_dir = proc_mkdir(hwt->name, hardwall_proc_dir);
	}
}


/*
 * Character device support via ioctl/close.
 */

static long hardwall_ioctl(struct file *file, unsigned int a, unsigned long b)
{
	struct hardwall_info *info = file->private_data;
	int minor = iminor(file->f_mapping->host);
	struct hardwall_type* hwt;

	if (_IOC_TYPE(a) != HARDWALL_IOCTL_BASE)
		return -EINVAL;

	BUILD_BUG_ON(HARDWALL_TYPES != _HARDWALL_TYPES);
	BUILD_BUG_ON(HARDWALL_TYPES !=
		     sizeof(hardwall_types)/sizeof(hardwall_types[0]));

	if (minor < 0 || minor >= HARDWALL_TYPES)
		return -EINVAL;
	hwt = &hardwall_types[minor];
	WARN_ON(info && hwt != info->type);

	switch (_IOC_NR(a)) {
	case _HARDWALL_CREATE:
		if (hwt->disabled)
			return -ENOSYS;
		if (info != NULL)
			return -EALREADY;
		info = hardwall_create(hwt, _IOC_SIZE(a),
				       (const unsigned char __user *)b);
		if (IS_ERR(info))
			return PTR_ERR(info);
		file->private_data = info;
		return 0;

	case _HARDWALL_ACTIVATE:
		return hardwall_activate(info);

	case _HARDWALL_DEACTIVATE:
		if (current->thread.hardwall[hwt->index].info != info)
			return -EINVAL;
		return hardwall_deactivate(hwt, current);

	case _HARDWALL_GET_ID:
		return info ? info->id : -EINVAL;

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
	struct hardwall_info *info = file->private_data;
	struct task_struct *task, *tmp;
	unsigned long flags;

	if (info) {
		/*
		 * NOTE: if multiple threads are activated on this hardwall
		 * file, the other threads will continue having access to the
		 * user network until they are context-switched out and back
		 * in again.
		 *
		 * NOTE: A NULL files pointer means the task is being torn
		 * down, so in that case we also deactivate it.
		 */
		struct hardwall_type *hwt = info->type;
		spin_lock_irqsave(&hwt->lock, flags);
		list_for_each_entry_safe(task, tmp, &info->task_head,
					 thread.hardwall[hwt->index].list) {
			if (task->files == owner || task->files == NULL)
				_hardwall_deactivate(hwt, task);
		}
		spin_unlock_irqrestore(&hwt->lock, flags);
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

	rc = alloc_chrdev_region(&dev, 0, HARDWALL_TYPES, "hardwall");
	if (rc < 0)
		return rc;
	cdev_init(&hardwall_dev, &dev_hardwall_fops);
	rc = cdev_add(&hardwall_dev, dev, HARDWALL_TYPES);
	if (rc < 0)
		return rc;

	return 0;
}
late_initcall(dev_hardwall_init);
