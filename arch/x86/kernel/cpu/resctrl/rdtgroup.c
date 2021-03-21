// SPDX-License-Identifier: GPL-2.0-only
/*
 * User interface for Resource Allocation in Resource Director Technology(RDT)
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Fenghua Yu <fenghua.yu@intel.com>
 *
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/fs_parser.h>
#include <linux/sysfs.h>
#include <linux/kernfs.h>
#include <linux/seq_buf.h>
#include <linux/seq_file.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/user_namespace.h>

#include <uapi/linux/magic.h>

#include <asm/resctrl.h>
#include "internal.h"

DEFINE_STATIC_KEY_FALSE(rdt_enable_key);
DEFINE_STATIC_KEY_FALSE(rdt_mon_enable_key);
DEFINE_STATIC_KEY_FALSE(rdt_alloc_enable_key);
static struct kernfs_root *rdt_root;
struct rdtgroup rdtgroup_default;
LIST_HEAD(rdt_all_groups);

/* Kernel fs node for "info" directory under root */
static struct kernfs_node *kn_info;

/* Kernel fs node for "mon_groups" directory under root */
static struct kernfs_node *kn_mongrp;

/* Kernel fs node for "mon_data" directory under root */
static struct kernfs_node *kn_mondata;

static struct seq_buf last_cmd_status;
static char last_cmd_status_buf[512];

struct dentry *debugfs_resctrl;

void rdt_last_cmd_clear(void)
{
	lockdep_assert_held(&rdtgroup_mutex);
	seq_buf_clear(&last_cmd_status);
}

void rdt_last_cmd_puts(const char *s)
{
	lockdep_assert_held(&rdtgroup_mutex);
	seq_buf_puts(&last_cmd_status, s);
}

void rdt_last_cmd_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	lockdep_assert_held(&rdtgroup_mutex);
	seq_buf_vprintf(&last_cmd_status, fmt, ap);
	va_end(ap);
}

/*
 * Trivial allocator for CLOSIDs. Since h/w only supports a small number,
 * we can keep a bitmap of free CLOSIDs in a single integer.
 *
 * Using a global CLOSID across all resources has some advantages and
 * some drawbacks:
 * + We can simply set "current->closid" to assign a task to a resource
 *   group.
 * + Context switch code can avoid extra memory references deciding which
 *   CLOSID to load into the PQR_ASSOC MSR
 * - We give up some options in configuring resource groups across multi-socket
 *   systems.
 * - Our choices on how to configure each resource become progressively more
 *   limited as the number of resources grows.
 */
static int closid_free_map;
static int closid_free_map_len;

int closids_supported(void)
{
	return closid_free_map_len;
}

static void closid_init(void)
{
	struct rdt_resource *r;
	int rdt_min_closid = 32;

	/* Compute rdt_min_closid across all resources */
	for_each_alloc_enabled_rdt_resource(r)
		rdt_min_closid = min(rdt_min_closid, r->num_closid);

	closid_free_map = BIT_MASK(rdt_min_closid) - 1;

	/* CLOSID 0 is always reserved for the default group */
	closid_free_map &= ~1;
	closid_free_map_len = rdt_min_closid;
}

static int closid_alloc(void)
{
	u32 closid = ffs(closid_free_map);

	if (closid == 0)
		return -ENOSPC;
	closid--;
	closid_free_map &= ~(1 << closid);

	return closid;
}

void closid_free(int closid)
{
	closid_free_map |= 1 << closid;
}

/**
 * closid_allocated - test if provided closid is in use
 * @closid: closid to be tested
 *
 * Return: true if @closid is currently associated with a resource group,
 * false if @closid is free
 */
static bool closid_allocated(unsigned int closid)
{
	return (closid_free_map & (1 << closid)) == 0;
}

/**
 * rdtgroup_mode_by_closid - Return mode of resource group with closid
 * @closid: closid if the resource group
 *
 * Each resource group is associated with a @closid. Here the mode
 * of a resource group can be queried by searching for it using its closid.
 *
 * Return: mode as &enum rdtgrp_mode of resource group with closid @closid
 */
enum rdtgrp_mode rdtgroup_mode_by_closid(int closid)
{
	struct rdtgroup *rdtgrp;

	list_for_each_entry(rdtgrp, &rdt_all_groups, rdtgroup_list) {
		if (rdtgrp->closid == closid)
			return rdtgrp->mode;
	}

	return RDT_NUM_MODES;
}

static const char * const rdt_mode_str[] = {
	[RDT_MODE_SHAREABLE]		= "shareable",
	[RDT_MODE_EXCLUSIVE]		= "exclusive",
	[RDT_MODE_PSEUDO_LOCKSETUP]	= "pseudo-locksetup",
	[RDT_MODE_PSEUDO_LOCKED]	= "pseudo-locked",
};

/**
 * rdtgroup_mode_str - Return the string representation of mode
 * @mode: the resource group mode as &enum rdtgroup_mode
 *
 * Return: string representation of valid mode, "unknown" otherwise
 */
static const char *rdtgroup_mode_str(enum rdtgrp_mode mode)
{
	if (mode < RDT_MODE_SHAREABLE || mode >= RDT_NUM_MODES)
		return "unknown";

	return rdt_mode_str[mode];
}

/* set uid and gid of rdtgroup dirs and files to that of the creator */
static int rdtgroup_kn_set_ugid(struct kernfs_node *kn)
{
	struct iattr iattr = { .ia_valid = ATTR_UID | ATTR_GID,
				.ia_uid = current_fsuid(),
				.ia_gid = current_fsgid(), };

	if (uid_eq(iattr.ia_uid, GLOBAL_ROOT_UID) &&
	    gid_eq(iattr.ia_gid, GLOBAL_ROOT_GID))
		return 0;

	return kernfs_setattr(kn, &iattr);
}

static int rdtgroup_add_file(struct kernfs_node *parent_kn, struct rftype *rft)
{
	struct kernfs_node *kn;
	int ret;

	kn = __kernfs_create_file(parent_kn, rft->name, rft->mode,
				  GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
				  0, rft->kf_ops, rft, NULL, NULL);
	if (IS_ERR(kn))
		return PTR_ERR(kn);

	ret = rdtgroup_kn_set_ugid(kn);
	if (ret) {
		kernfs_remove(kn);
		return ret;
	}

	return 0;
}

static int rdtgroup_seqfile_show(struct seq_file *m, void *arg)
{
	struct kernfs_open_file *of = m->private;
	struct rftype *rft = of->kn->priv;

	if (rft->seq_show)
		return rft->seq_show(of, m, arg);
	return 0;
}

static ssize_t rdtgroup_file_write(struct kernfs_open_file *of, char *buf,
				   size_t nbytes, loff_t off)
{
	struct rftype *rft = of->kn->priv;

	if (rft->write)
		return rft->write(of, buf, nbytes, off);

	return -EINVAL;
}

static const struct kernfs_ops rdtgroup_kf_single_ops = {
	.atomic_write_len	= PAGE_SIZE,
	.write			= rdtgroup_file_write,
	.seq_show		= rdtgroup_seqfile_show,
};

static const struct kernfs_ops kf_mondata_ops = {
	.atomic_write_len	= PAGE_SIZE,
	.seq_show		= rdtgroup_mondata_show,
};

static bool is_cpu_list(struct kernfs_open_file *of)
{
	struct rftype *rft = of->kn->priv;

	return rft->flags & RFTYPE_FLAGS_CPUS_LIST;
}

static int rdtgroup_cpus_show(struct kernfs_open_file *of,
			      struct seq_file *s, void *v)
{
	struct rdtgroup *rdtgrp;
	struct cpumask *mask;
	int ret = 0;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);

	if (rdtgrp) {
		if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED) {
			if (!rdtgrp->plr->d) {
				rdt_last_cmd_clear();
				rdt_last_cmd_puts("Cache domain offline\n");
				ret = -ENODEV;
			} else {
				mask = &rdtgrp->plr->d->cpu_mask;
				seq_printf(s, is_cpu_list(of) ?
					   "%*pbl\n" : "%*pb\n",
					   cpumask_pr_args(mask));
			}
		} else {
			seq_printf(s, is_cpu_list(of) ? "%*pbl\n" : "%*pb\n",
				   cpumask_pr_args(&rdtgrp->cpu_mask));
		}
	} else {
		ret = -ENOENT;
	}
	rdtgroup_kn_unlock(of->kn);

	return ret;
}

/*
 * This is safe against resctrl_sched_in() called from __switch_to()
 * because __switch_to() is executed with interrupts disabled. A local call
 * from update_closid_rmid() is protected against __switch_to() because
 * preemption is disabled.
 */
static void update_cpu_closid_rmid(void *info)
{
	struct rdtgroup *r = info;

	if (r) {
		this_cpu_write(pqr_state.default_closid, r->closid);
		this_cpu_write(pqr_state.default_rmid, r->mon.rmid);
	}

	/*
	 * We cannot unconditionally write the MSR because the current
	 * executing task might have its own closid selected. Just reuse
	 * the context switch code.
	 */
	resctrl_sched_in();
}

/*
 * Update the PGR_ASSOC MSR on all cpus in @cpu_mask,
 *
 * Per task closids/rmids must have been set up before calling this function.
 */
static void
update_closid_rmid(const struct cpumask *cpu_mask, struct rdtgroup *r)
{
	int cpu = get_cpu();

	if (cpumask_test_cpu(cpu, cpu_mask))
		update_cpu_closid_rmid(r);
	smp_call_function_many(cpu_mask, update_cpu_closid_rmid, r, 1);
	put_cpu();
}

static int cpus_mon_write(struct rdtgroup *rdtgrp, cpumask_var_t newmask,
			  cpumask_var_t tmpmask)
{
	struct rdtgroup *prgrp = rdtgrp->mon.parent, *crgrp;
	struct list_head *head;

	/* Check whether cpus belong to parent ctrl group */
	cpumask_andnot(tmpmask, newmask, &prgrp->cpu_mask);
	if (cpumask_weight(tmpmask)) {
		rdt_last_cmd_puts("Can only add CPUs to mongroup that belong to parent\n");
		return -EINVAL;
	}

	/* Check whether cpus are dropped from this group */
	cpumask_andnot(tmpmask, &rdtgrp->cpu_mask, newmask);
	if (cpumask_weight(tmpmask)) {
		/* Give any dropped cpus to parent rdtgroup */
		cpumask_or(&prgrp->cpu_mask, &prgrp->cpu_mask, tmpmask);
		update_closid_rmid(tmpmask, prgrp);
	}

	/*
	 * If we added cpus, remove them from previous group that owned them
	 * and update per-cpu rmid
	 */
	cpumask_andnot(tmpmask, newmask, &rdtgrp->cpu_mask);
	if (cpumask_weight(tmpmask)) {
		head = &prgrp->mon.crdtgrp_list;
		list_for_each_entry(crgrp, head, mon.crdtgrp_list) {
			if (crgrp == rdtgrp)
				continue;
			cpumask_andnot(&crgrp->cpu_mask, &crgrp->cpu_mask,
				       tmpmask);
		}
		update_closid_rmid(tmpmask, rdtgrp);
	}

	/* Done pushing/pulling - update this group with new mask */
	cpumask_copy(&rdtgrp->cpu_mask, newmask);

	return 0;
}

static void cpumask_rdtgrp_clear(struct rdtgroup *r, struct cpumask *m)
{
	struct rdtgroup *crgrp;

	cpumask_andnot(&r->cpu_mask, &r->cpu_mask, m);
	/* update the child mon group masks as well*/
	list_for_each_entry(crgrp, &r->mon.crdtgrp_list, mon.crdtgrp_list)
		cpumask_and(&crgrp->cpu_mask, &r->cpu_mask, &crgrp->cpu_mask);
}

static int cpus_ctrl_write(struct rdtgroup *rdtgrp, cpumask_var_t newmask,
			   cpumask_var_t tmpmask, cpumask_var_t tmpmask1)
{
	struct rdtgroup *r, *crgrp;
	struct list_head *head;

	/* Check whether cpus are dropped from this group */
	cpumask_andnot(tmpmask, &rdtgrp->cpu_mask, newmask);
	if (cpumask_weight(tmpmask)) {
		/* Can't drop from default group */
		if (rdtgrp == &rdtgroup_default) {
			rdt_last_cmd_puts("Can't drop CPUs from default group\n");
			return -EINVAL;
		}

		/* Give any dropped cpus to rdtgroup_default */
		cpumask_or(&rdtgroup_default.cpu_mask,
			   &rdtgroup_default.cpu_mask, tmpmask);
		update_closid_rmid(tmpmask, &rdtgroup_default);
	}

	/*
	 * If we added cpus, remove them from previous group and
	 * the prev group's child groups that owned them
	 * and update per-cpu closid/rmid.
	 */
	cpumask_andnot(tmpmask, newmask, &rdtgrp->cpu_mask);
	if (cpumask_weight(tmpmask)) {
		list_for_each_entry(r, &rdt_all_groups, rdtgroup_list) {
			if (r == rdtgrp)
				continue;
			cpumask_and(tmpmask1, &r->cpu_mask, tmpmask);
			if (cpumask_weight(tmpmask1))
				cpumask_rdtgrp_clear(r, tmpmask1);
		}
		update_closid_rmid(tmpmask, rdtgrp);
	}

	/* Done pushing/pulling - update this group with new mask */
	cpumask_copy(&rdtgrp->cpu_mask, newmask);

	/*
	 * Clear child mon group masks since there is a new parent mask
	 * now and update the rmid for the cpus the child lost.
	 */
	head = &rdtgrp->mon.crdtgrp_list;
	list_for_each_entry(crgrp, head, mon.crdtgrp_list) {
		cpumask_and(tmpmask, &rdtgrp->cpu_mask, &crgrp->cpu_mask);
		update_closid_rmid(tmpmask, rdtgrp);
		cpumask_clear(&crgrp->cpu_mask);
	}

	return 0;
}

static ssize_t rdtgroup_cpus_write(struct kernfs_open_file *of,
				   char *buf, size_t nbytes, loff_t off)
{
	cpumask_var_t tmpmask, newmask, tmpmask1;
	struct rdtgroup *rdtgrp;
	int ret;

	if (!buf)
		return -EINVAL;

	if (!zalloc_cpumask_var(&tmpmask, GFP_KERNEL))
		return -ENOMEM;
	if (!zalloc_cpumask_var(&newmask, GFP_KERNEL)) {
		free_cpumask_var(tmpmask);
		return -ENOMEM;
	}
	if (!zalloc_cpumask_var(&tmpmask1, GFP_KERNEL)) {
		free_cpumask_var(tmpmask);
		free_cpumask_var(newmask);
		return -ENOMEM;
	}

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (!rdtgrp) {
		ret = -ENOENT;
		goto unlock;
	}

	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED ||
	    rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
		ret = -EINVAL;
		rdt_last_cmd_puts("Pseudo-locking in progress\n");
		goto unlock;
	}

	if (is_cpu_list(of))
		ret = cpulist_parse(buf, newmask);
	else
		ret = cpumask_parse(buf, newmask);

	if (ret) {
		rdt_last_cmd_puts("Bad CPU list/mask\n");
		goto unlock;
	}

	/* check that user didn't specify any offline cpus */
	cpumask_andnot(tmpmask, newmask, cpu_online_mask);
	if (cpumask_weight(tmpmask)) {
		ret = -EINVAL;
		rdt_last_cmd_puts("Can only assign online CPUs\n");
		goto unlock;
	}

	if (rdtgrp->type == RDTCTRL_GROUP)
		ret = cpus_ctrl_write(rdtgrp, newmask, tmpmask, tmpmask1);
	else if (rdtgrp->type == RDTMON_GROUP)
		ret = cpus_mon_write(rdtgrp, newmask, tmpmask);
	else
		ret = -EINVAL;

unlock:
	rdtgroup_kn_unlock(of->kn);
	free_cpumask_var(tmpmask);
	free_cpumask_var(newmask);
	free_cpumask_var(tmpmask1);

	return ret ?: nbytes;
}

/**
 * rdtgroup_remove - the helper to remove resource group safely
 * @rdtgrp: resource group to remove
 *
 * On resource group creation via a mkdir, an extra kernfs_node reference is
 * taken to ensure that the rdtgroup structure remains accessible for the
 * rdtgroup_kn_unlock() calls where it is removed.
 *
 * Drop the extra reference here, then free the rdtgroup structure.
 *
 * Return: void
 */
static void rdtgroup_remove(struct rdtgroup *rdtgrp)
{
	kernfs_put(rdtgrp->kn);
	kfree(rdtgrp);
}

static void _update_task_closid_rmid(void *task)
{
	/*
	 * If the task is still current on this CPU, update PQR_ASSOC MSR.
	 * Otherwise, the MSR is updated when the task is scheduled in.
	 */
	if (task == current)
		resctrl_sched_in();
}

static void update_task_closid_rmid(struct task_struct *t)
{
	if (IS_ENABLED(CONFIG_SMP) && task_curr(t))
		smp_call_function_single(task_cpu(t), _update_task_closid_rmid, t, 1);
	else
		_update_task_closid_rmid(t);
}

static int __rdtgroup_move_task(struct task_struct *tsk,
				struct rdtgroup *rdtgrp)
{
	/* If the task is already in rdtgrp, no need to move the task. */
	if ((rdtgrp->type == RDTCTRL_GROUP && tsk->closid == rdtgrp->closid &&
	     tsk->rmid == rdtgrp->mon.rmid) ||
	    (rdtgrp->type == RDTMON_GROUP && tsk->rmid == rdtgrp->mon.rmid &&
	     tsk->closid == rdtgrp->mon.parent->closid))
		return 0;

	/*
	 * Set the task's closid/rmid before the PQR_ASSOC MSR can be
	 * updated by them.
	 *
	 * For ctrl_mon groups, move both closid and rmid.
	 * For monitor groups, can move the tasks only from
	 * their parent CTRL group.
	 */

	if (rdtgrp->type == RDTCTRL_GROUP) {
		WRITE_ONCE(tsk->closid, rdtgrp->closid);
		WRITE_ONCE(tsk->rmid, rdtgrp->mon.rmid);
	} else if (rdtgrp->type == RDTMON_GROUP) {
		if (rdtgrp->mon.parent->closid == tsk->closid) {
			WRITE_ONCE(tsk->rmid, rdtgrp->mon.rmid);
		} else {
			rdt_last_cmd_puts("Can't move task to different control group\n");
			return -EINVAL;
		}
	}

	/*
	 * Ensure the task's closid and rmid are written before determining if
	 * the task is current that will decide if it will be interrupted.
	 */
	barrier();

	/*
	 * By now, the task's closid and rmid are set. If the task is current
	 * on a CPU, the PQR_ASSOC MSR needs to be updated to make the resource
	 * group go into effect. If the task is not current, the MSR will be
	 * updated when the task is scheduled in.
	 */
	update_task_closid_rmid(tsk);

	return 0;
}

static bool is_closid_match(struct task_struct *t, struct rdtgroup *r)
{
	return (rdt_alloc_capable &&
	       (r->type == RDTCTRL_GROUP) && (t->closid == r->closid));
}

static bool is_rmid_match(struct task_struct *t, struct rdtgroup *r)
{
	return (rdt_mon_capable &&
	       (r->type == RDTMON_GROUP) && (t->rmid == r->mon.rmid));
}

/**
 * rdtgroup_tasks_assigned - Test if tasks have been assigned to resource group
 * @r: Resource group
 *
 * Return: 1 if tasks have been assigned to @r, 0 otherwise
 */
int rdtgroup_tasks_assigned(struct rdtgroup *r)
{
	struct task_struct *p, *t;
	int ret = 0;

	lockdep_assert_held(&rdtgroup_mutex);

	rcu_read_lock();
	for_each_process_thread(p, t) {
		if (is_closid_match(t, r) || is_rmid_match(t, r)) {
			ret = 1;
			break;
		}
	}
	rcu_read_unlock();

	return ret;
}

static int rdtgroup_task_write_permission(struct task_struct *task,
					  struct kernfs_open_file *of)
{
	const struct cred *tcred = get_task_cred(task);
	const struct cred *cred = current_cred();
	int ret = 0;

	/*
	 * Even if we're attaching all tasks in the thread group, we only
	 * need to check permissions on one of them.
	 */
	if (!uid_eq(cred->euid, GLOBAL_ROOT_UID) &&
	    !uid_eq(cred->euid, tcred->uid) &&
	    !uid_eq(cred->euid, tcred->suid)) {
		rdt_last_cmd_printf("No permission to move task %d\n", task->pid);
		ret = -EPERM;
	}

	put_cred(tcred);
	return ret;
}

static int rdtgroup_move_task(pid_t pid, struct rdtgroup *rdtgrp,
			      struct kernfs_open_file *of)
{
	struct task_struct *tsk;
	int ret;

	rcu_read_lock();
	if (pid) {
		tsk = find_task_by_vpid(pid);
		if (!tsk) {
			rcu_read_unlock();
			rdt_last_cmd_printf("No task %d\n", pid);
			return -ESRCH;
		}
	} else {
		tsk = current;
	}

	get_task_struct(tsk);
	rcu_read_unlock();

	ret = rdtgroup_task_write_permission(tsk, of);
	if (!ret)
		ret = __rdtgroup_move_task(tsk, rdtgrp);

	put_task_struct(tsk);
	return ret;
}

static ssize_t rdtgroup_tasks_write(struct kernfs_open_file *of,
				    char *buf, size_t nbytes, loff_t off)
{
	struct rdtgroup *rdtgrp;
	int ret = 0;
	pid_t pid;

	if (kstrtoint(strstrip(buf), 0, &pid) || pid < 0)
		return -EINVAL;
	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (!rdtgrp) {
		rdtgroup_kn_unlock(of->kn);
		return -ENOENT;
	}
	rdt_last_cmd_clear();

	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED ||
	    rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
		ret = -EINVAL;
		rdt_last_cmd_puts("Pseudo-locking in progress\n");
		goto unlock;
	}

	ret = rdtgroup_move_task(pid, rdtgrp, of);

unlock:
	rdtgroup_kn_unlock(of->kn);

	return ret ?: nbytes;
}

static void show_rdt_tasks(struct rdtgroup *r, struct seq_file *s)
{
	struct task_struct *p, *t;

	rcu_read_lock();
	for_each_process_thread(p, t) {
		if (is_closid_match(t, r) || is_rmid_match(t, r))
			seq_printf(s, "%d\n", t->pid);
	}
	rcu_read_unlock();
}

static int rdtgroup_tasks_show(struct kernfs_open_file *of,
			       struct seq_file *s, void *v)
{
	struct rdtgroup *rdtgrp;
	int ret = 0;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (rdtgrp)
		show_rdt_tasks(rdtgrp, s);
	else
		ret = -ENOENT;
	rdtgroup_kn_unlock(of->kn);

	return ret;
}

#ifdef CONFIG_PROC_CPU_RESCTRL

/*
 * A task can only be part of one resctrl control group and of one monitor
 * group which is associated to that control group.
 *
 * 1)   res:
 *      mon:
 *
 *    resctrl is not available.
 *
 * 2)   res:/
 *      mon:
 *
 *    Task is part of the root resctrl control group, and it is not associated
 *    to any monitor group.
 *
 * 3)  res:/
 *     mon:mon0
 *
 *    Task is part of the root resctrl control group and monitor group mon0.
 *
 * 4)  res:group0
 *     mon:
 *
 *    Task is part of resctrl control group group0, and it is not associated
 *    to any monitor group.
 *
 * 5) res:group0
 *    mon:mon1
 *
 *    Task is part of resctrl control group group0 and monitor group mon1.
 */
int proc_resctrl_show(struct seq_file *s, struct pid_namespace *ns,
		      struct pid *pid, struct task_struct *tsk)
{
	struct rdtgroup *rdtg;
	int ret = 0;

	mutex_lock(&rdtgroup_mutex);

	/* Return empty if resctrl has not been mounted. */
	if (!static_branch_unlikely(&rdt_enable_key)) {
		seq_puts(s, "res:\nmon:\n");
		goto unlock;
	}

	list_for_each_entry(rdtg, &rdt_all_groups, rdtgroup_list) {
		struct rdtgroup *crg;

		/*
		 * Task information is only relevant for shareable
		 * and exclusive groups.
		 */
		if (rdtg->mode != RDT_MODE_SHAREABLE &&
		    rdtg->mode != RDT_MODE_EXCLUSIVE)
			continue;

		if (rdtg->closid != tsk->closid)
			continue;

		seq_printf(s, "res:%s%s\n", (rdtg == &rdtgroup_default) ? "/" : "",
			   rdtg->kn->name);
		seq_puts(s, "mon:");
		list_for_each_entry(crg, &rdtg->mon.crdtgrp_list,
				    mon.crdtgrp_list) {
			if (tsk->rmid != crg->mon.rmid)
				continue;
			seq_printf(s, "%s", crg->kn->name);
			break;
		}
		seq_putc(s, '\n');
		goto unlock;
	}
	/*
	 * The above search should succeed. Otherwise return
	 * with an error.
	 */
	ret = -ENOENT;
unlock:
	mutex_unlock(&rdtgroup_mutex);

	return ret;
}
#endif

static int rdt_last_cmd_status_show(struct kernfs_open_file *of,
				    struct seq_file *seq, void *v)
{
	int len;

	mutex_lock(&rdtgroup_mutex);
	len = seq_buf_used(&last_cmd_status);
	if (len)
		seq_printf(seq, "%.*s", len, last_cmd_status_buf);
	else
		seq_puts(seq, "ok\n");
	mutex_unlock(&rdtgroup_mutex);
	return 0;
}

static int rdt_num_closids_show(struct kernfs_open_file *of,
				struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%d\n", r->num_closid);
	return 0;
}

static int rdt_default_ctrl_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%x\n", r->default_ctrl);
	return 0;
}

static int rdt_min_cbm_bits_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%u\n", r->cache.min_cbm_bits);
	return 0;
}

static int rdt_shareable_bits_show(struct kernfs_open_file *of,
				   struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%x\n", r->cache.shareable_bits);
	return 0;
}

/**
 * rdt_bit_usage_show - Display current usage of resources
 *
 * A domain is a shared resource that can now be allocated differently. Here
 * we display the current regions of the domain as an annotated bitmask.
 * For each domain of this resource its allocation bitmask
 * is annotated as below to indicate the current usage of the corresponding bit:
 *   0 - currently unused
 *   X - currently available for sharing and used by software and hardware
 *   H - currently used by hardware only but available for software use
 *   S - currently used and shareable by software only
 *   E - currently used exclusively by one resource group
 *   P - currently pseudo-locked by one resource group
 */
static int rdt_bit_usage_show(struct kernfs_open_file *of,
			      struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;
	/*
	 * Use unsigned long even though only 32 bits are used to ensure
	 * test_bit() is used safely.
	 */
	unsigned long sw_shareable = 0, hw_shareable = 0;
	unsigned long exclusive = 0, pseudo_locked = 0;
	struct rdt_domain *dom;
	int i, hwb, swb, excl, psl;
	enum rdtgrp_mode mode;
	bool sep = false;
	u32 *ctrl;

	mutex_lock(&rdtgroup_mutex);
	hw_shareable = r->cache.shareable_bits;
	list_for_each_entry(dom, &r->domains, list) {
		if (sep)
			seq_putc(seq, ';');
		ctrl = dom->ctrl_val;
		sw_shareable = 0;
		exclusive = 0;
		seq_printf(seq, "%d=", dom->id);
		for (i = 0; i < closids_supported(); i++, ctrl++) {
			if (!closid_allocated(i))
				continue;
			mode = rdtgroup_mode_by_closid(i);
			switch (mode) {
			case RDT_MODE_SHAREABLE:
				sw_shareable |= *ctrl;
				break;
			case RDT_MODE_EXCLUSIVE:
				exclusive |= *ctrl;
				break;
			case RDT_MODE_PSEUDO_LOCKSETUP:
			/*
			 * RDT_MODE_PSEUDO_LOCKSETUP is possible
			 * here but not included since the CBM
			 * associated with this CLOSID in this mode
			 * is not initialized and no task or cpu can be
			 * assigned this CLOSID.
			 */
				break;
			case RDT_MODE_PSEUDO_LOCKED:
			case RDT_NUM_MODES:
				WARN(1,
				     "invalid mode for closid %d\n", i);
				break;
			}
		}
		for (i = r->cache.cbm_len - 1; i >= 0; i--) {
			pseudo_locked = dom->plr ? dom->plr->cbm : 0;
			hwb = test_bit(i, &hw_shareable);
			swb = test_bit(i, &sw_shareable);
			excl = test_bit(i, &exclusive);
			psl = test_bit(i, &pseudo_locked);
			if (hwb && swb)
				seq_putc(seq, 'X');
			else if (hwb && !swb)
				seq_putc(seq, 'H');
			else if (!hwb && swb)
				seq_putc(seq, 'S');
			else if (excl)
				seq_putc(seq, 'E');
			else if (psl)
				seq_putc(seq, 'P');
			else /* Unused bits remain */
				seq_putc(seq, '0');
		}
		sep = true;
	}
	seq_putc(seq, '\n');
	mutex_unlock(&rdtgroup_mutex);
	return 0;
}

static int rdt_min_bw_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%u\n", r->membw.min_bw);
	return 0;
}

static int rdt_num_rmids_show(struct kernfs_open_file *of,
			      struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%d\n", r->num_rmid);

	return 0;
}

static int rdt_mon_features_show(struct kernfs_open_file *of,
				 struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;
	struct mon_evt *mevt;

	list_for_each_entry(mevt, &r->evt_list, list)
		seq_printf(seq, "%s\n", mevt->name);

	return 0;
}

static int rdt_bw_gran_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%u\n", r->membw.bw_gran);
	return 0;
}

static int rdt_delay_linear_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%u\n", r->membw.delay_linear);
	return 0;
}

static int max_threshold_occ_show(struct kernfs_open_file *of,
				  struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	seq_printf(seq, "%u\n", resctrl_cqm_threshold * r->mon_scale);

	return 0;
}

static int rdt_thread_throttle_mode_show(struct kernfs_open_file *of,
					 struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	if (r->membw.throttle_mode == THREAD_THROTTLE_PER_THREAD)
		seq_puts(seq, "per-thread\n");
	else
		seq_puts(seq, "max\n");

	return 0;
}

static ssize_t max_threshold_occ_write(struct kernfs_open_file *of,
				       char *buf, size_t nbytes, loff_t off)
{
	struct rdt_resource *r = of->kn->parent->priv;
	unsigned int bytes;
	int ret;

	ret = kstrtouint(buf, 0, &bytes);
	if (ret)
		return ret;

	if (bytes > (boot_cpu_data.x86_cache_size * 1024))
		return -EINVAL;

	resctrl_cqm_threshold = bytes / r->mon_scale;

	return nbytes;
}

/*
 * rdtgroup_mode_show - Display mode of this resource group
 */
static int rdtgroup_mode_show(struct kernfs_open_file *of,
			      struct seq_file *s, void *v)
{
	struct rdtgroup *rdtgrp;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (!rdtgrp) {
		rdtgroup_kn_unlock(of->kn);
		return -ENOENT;
	}

	seq_printf(s, "%s\n", rdtgroup_mode_str(rdtgrp->mode));

	rdtgroup_kn_unlock(of->kn);
	return 0;
}

/**
 * rdt_cdp_peer_get - Retrieve CDP peer if it exists
 * @r: RDT resource to which RDT domain @d belongs
 * @d: Cache instance for which a CDP peer is requested
 * @r_cdp: RDT resource that shares hardware with @r (RDT resource peer)
 *         Used to return the result.
 * @d_cdp: RDT domain that shares hardware with @d (RDT domain peer)
 *         Used to return the result.
 *
 * RDT resources are managed independently and by extension the RDT domains
 * (RDT resource instances) are managed independently also. The Code and
 * Data Prioritization (CDP) RDT resources, while managed independently,
 * could refer to the same underlying hardware. For example,
 * RDT_RESOURCE_L2CODE and RDT_RESOURCE_L2DATA both refer to the L2 cache.
 *
 * When provided with an RDT resource @r and an instance of that RDT
 * resource @d rdt_cdp_peer_get() will return if there is a peer RDT
 * resource and the exact instance that shares the same hardware.
 *
 * Return: 0 if a CDP peer was found, <0 on error or if no CDP peer exists.
 *         If a CDP peer was found, @r_cdp will point to the peer RDT resource
 *         and @d_cdp will point to the peer RDT domain.
 */
static int rdt_cdp_peer_get(struct rdt_resource *r, struct rdt_domain *d,
			    struct rdt_resource **r_cdp,
			    struct rdt_domain **d_cdp)
{
	struct rdt_resource *_r_cdp = NULL;
	struct rdt_domain *_d_cdp = NULL;
	int ret = 0;

	switch (r->rid) {
	case RDT_RESOURCE_L3DATA:
		_r_cdp = &rdt_resources_all[RDT_RESOURCE_L3CODE];
		break;
	case RDT_RESOURCE_L3CODE:
		_r_cdp =  &rdt_resources_all[RDT_RESOURCE_L3DATA];
		break;
	case RDT_RESOURCE_L2DATA:
		_r_cdp =  &rdt_resources_all[RDT_RESOURCE_L2CODE];
		break;
	case RDT_RESOURCE_L2CODE:
		_r_cdp =  &rdt_resources_all[RDT_RESOURCE_L2DATA];
		break;
	default:
		ret = -ENOENT;
		goto out;
	}

	/*
	 * When a new CPU comes online and CDP is enabled then the new
	 * RDT domains (if any) associated with both CDP RDT resources
	 * are added in the same CPU online routine while the
	 * rdtgroup_mutex is held. It should thus not happen for one
	 * RDT domain to exist and be associated with its RDT CDP
	 * resource but there is no RDT domain associated with the
	 * peer RDT CDP resource. Hence the WARN.
	 */
	_d_cdp = rdt_find_domain(_r_cdp, d->id, NULL);
	if (WARN_ON(IS_ERR_OR_NULL(_d_cdp))) {
		_r_cdp = NULL;
		_d_cdp = NULL;
		ret = -EINVAL;
	}

out:
	*r_cdp = _r_cdp;
	*d_cdp = _d_cdp;

	return ret;
}

/**
 * __rdtgroup_cbm_overlaps - Does CBM for intended closid overlap with other
 * @r: Resource to which domain instance @d belongs.
 * @d: The domain instance for which @closid is being tested.
 * @cbm: Capacity bitmask being tested.
 * @closid: Intended closid for @cbm.
 * @exclusive: Only check if overlaps with exclusive resource groups
 *
 * Checks if provided @cbm intended to be used for @closid on domain
 * @d overlaps with any other closids or other hardware usage associated
 * with this domain. If @exclusive is true then only overlaps with
 * resource groups in exclusive mode will be considered. If @exclusive
 * is false then overlaps with any resource group or hardware entities
 * will be considered.
 *
 * @cbm is unsigned long, even if only 32 bits are used, to make the
 * bitmap functions work correctly.
 *
 * Return: false if CBM does not overlap, true if it does.
 */
static bool __rdtgroup_cbm_overlaps(struct rdt_resource *r, struct rdt_domain *d,
				    unsigned long cbm, int closid, bool exclusive)
{
	enum rdtgrp_mode mode;
	unsigned long ctrl_b;
	u32 *ctrl;
	int i;

	/* Check for any overlap with regions used by hardware directly */
	if (!exclusive) {
		ctrl_b = r->cache.shareable_bits;
		if (bitmap_intersects(&cbm, &ctrl_b, r->cache.cbm_len))
			return true;
	}

	/* Check for overlap with other resource groups */
	ctrl = d->ctrl_val;
	for (i = 0; i < closids_supported(); i++, ctrl++) {
		ctrl_b = *ctrl;
		mode = rdtgroup_mode_by_closid(i);
		if (closid_allocated(i) && i != closid &&
		    mode != RDT_MODE_PSEUDO_LOCKSETUP) {
			if (bitmap_intersects(&cbm, &ctrl_b, r->cache.cbm_len)) {
				if (exclusive) {
					if (mode == RDT_MODE_EXCLUSIVE)
						return true;
					continue;
				}
				return true;
			}
		}
	}

	return false;
}

/**
 * rdtgroup_cbm_overlaps - Does CBM overlap with other use of hardware
 * @r: Resource to which domain instance @d belongs.
 * @d: The domain instance for which @closid is being tested.
 * @cbm: Capacity bitmask being tested.
 * @closid: Intended closid for @cbm.
 * @exclusive: Only check if overlaps with exclusive resource groups
 *
 * Resources that can be allocated using a CBM can use the CBM to control
 * the overlap of these allocations. rdtgroup_cmb_overlaps() is the test
 * for overlap. Overlap test is not limited to the specific resource for
 * which the CBM is intended though - when dealing with CDP resources that
 * share the underlying hardware the overlap check should be performed on
 * the CDP resource sharing the hardware also.
 *
 * Refer to description of __rdtgroup_cbm_overlaps() for the details of the
 * overlap test.
 *
 * Return: true if CBM overlap detected, false if there is no overlap
 */
bool rdtgroup_cbm_overlaps(struct rdt_resource *r, struct rdt_domain *d,
			   unsigned long cbm, int closid, bool exclusive)
{
	struct rdt_resource *r_cdp;
	struct rdt_domain *d_cdp;

	if (__rdtgroup_cbm_overlaps(r, d, cbm, closid, exclusive))
		return true;

	if (rdt_cdp_peer_get(r, d, &r_cdp, &d_cdp) < 0)
		return false;

	return  __rdtgroup_cbm_overlaps(r_cdp, d_cdp, cbm, closid, exclusive);
}

/**
 * rdtgroup_mode_test_exclusive - Test if this resource group can be exclusive
 *
 * An exclusive resource group implies that there should be no sharing of
 * its allocated resources. At the time this group is considered to be
 * exclusive this test can determine if its current schemata supports this
 * setting by testing for overlap with all other resource groups.
 *
 * Return: true if resource group can be exclusive, false if there is overlap
 * with allocations of other resource groups and thus this resource group
 * cannot be exclusive.
 */
static bool rdtgroup_mode_test_exclusive(struct rdtgroup *rdtgrp)
{
	int closid = rdtgrp->closid;
	struct rdt_resource *r;
	bool has_cache = false;
	struct rdt_domain *d;

	for_each_alloc_enabled_rdt_resource(r) {
		if (r->rid == RDT_RESOURCE_MBA)
			continue;
		has_cache = true;
		list_for_each_entry(d, &r->domains, list) {
			if (rdtgroup_cbm_overlaps(r, d, d->ctrl_val[closid],
						  rdtgrp->closid, false)) {
				rdt_last_cmd_puts("Schemata overlaps\n");
				return false;
			}
		}
	}

	if (!has_cache) {
		rdt_last_cmd_puts("Cannot be exclusive without CAT/CDP\n");
		return false;
	}

	return true;
}

/**
 * rdtgroup_mode_write - Modify the resource group's mode
 *
 */
static ssize_t rdtgroup_mode_write(struct kernfs_open_file *of,
				   char *buf, size_t nbytes, loff_t off)
{
	struct rdtgroup *rdtgrp;
	enum rdtgrp_mode mode;
	int ret = 0;

	/* Valid input requires a trailing newline */
	if (nbytes == 0 || buf[nbytes - 1] != '\n')
		return -EINVAL;
	buf[nbytes - 1] = '\0';

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (!rdtgrp) {
		rdtgroup_kn_unlock(of->kn);
		return -ENOENT;
	}

	rdt_last_cmd_clear();

	mode = rdtgrp->mode;

	if ((!strcmp(buf, "shareable") && mode == RDT_MODE_SHAREABLE) ||
	    (!strcmp(buf, "exclusive") && mode == RDT_MODE_EXCLUSIVE) ||
	    (!strcmp(buf, "pseudo-locksetup") &&
	     mode == RDT_MODE_PSEUDO_LOCKSETUP) ||
	    (!strcmp(buf, "pseudo-locked") && mode == RDT_MODE_PSEUDO_LOCKED))
		goto out;

	if (mode == RDT_MODE_PSEUDO_LOCKED) {
		rdt_last_cmd_puts("Cannot change pseudo-locked group\n");
		ret = -EINVAL;
		goto out;
	}

	if (!strcmp(buf, "shareable")) {
		if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
			ret = rdtgroup_locksetup_exit(rdtgrp);
			if (ret)
				goto out;
		}
		rdtgrp->mode = RDT_MODE_SHAREABLE;
	} else if (!strcmp(buf, "exclusive")) {
		if (!rdtgroup_mode_test_exclusive(rdtgrp)) {
			ret = -EINVAL;
			goto out;
		}
		if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
			ret = rdtgroup_locksetup_exit(rdtgrp);
			if (ret)
				goto out;
		}
		rdtgrp->mode = RDT_MODE_EXCLUSIVE;
	} else if (!strcmp(buf, "pseudo-locksetup")) {
		ret = rdtgroup_locksetup_enter(rdtgrp);
		if (ret)
			goto out;
		rdtgrp->mode = RDT_MODE_PSEUDO_LOCKSETUP;
	} else {
		rdt_last_cmd_puts("Unknown or unsupported mode\n");
		ret = -EINVAL;
	}

out:
	rdtgroup_kn_unlock(of->kn);
	return ret ?: nbytes;
}

/**
 * rdtgroup_cbm_to_size - Translate CBM to size in bytes
 * @r: RDT resource to which @d belongs.
 * @d: RDT domain instance.
 * @cbm: bitmask for which the size should be computed.
 *
 * The bitmask provided associated with the RDT domain instance @d will be
 * translated into how many bytes it represents. The size in bytes is
 * computed by first dividing the total cache size by the CBM length to
 * determine how many bytes each bit in the bitmask represents. The result
 * is multiplied with the number of bits set in the bitmask.
 *
 * @cbm is unsigned long, even if only 32 bits are used to make the
 * bitmap functions work correctly.
 */
unsigned int rdtgroup_cbm_to_size(struct rdt_resource *r,
				  struct rdt_domain *d, unsigned long cbm)
{
	struct cpu_cacheinfo *ci;
	unsigned int size = 0;
	int num_b, i;

	num_b = bitmap_weight(&cbm, r->cache.cbm_len);
	ci = get_cpu_cacheinfo(cpumask_any(&d->cpu_mask));
	for (i = 0; i < ci->num_leaves; i++) {
		if (ci->info_list[i].level == r->cache_level) {
			size = ci->info_list[i].size / r->cache.cbm_len * num_b;
			break;
		}
	}

	return size;
}

/**
 * rdtgroup_size_show - Display size in bytes of allocated regions
 *
 * The "size" file mirrors the layout of the "schemata" file, printing the
 * size in bytes of each region instead of the capacity bitmask.
 *
 */
static int rdtgroup_size_show(struct kernfs_open_file *of,
			      struct seq_file *s, void *v)
{
	struct rdtgroup *rdtgrp;
	struct rdt_resource *r;
	struct rdt_domain *d;
	unsigned int size;
	int ret = 0;
	bool sep;
	u32 ctrl;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (!rdtgrp) {
		rdtgroup_kn_unlock(of->kn);
		return -ENOENT;
	}

	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED) {
		if (!rdtgrp->plr->d) {
			rdt_last_cmd_clear();
			rdt_last_cmd_puts("Cache domain offline\n");
			ret = -ENODEV;
		} else {
			seq_printf(s, "%*s:", max_name_width,
				   rdtgrp->plr->r->name);
			size = rdtgroup_cbm_to_size(rdtgrp->plr->r,
						    rdtgrp->plr->d,
						    rdtgrp->plr->cbm);
			seq_printf(s, "%d=%u\n", rdtgrp->plr->d->id, size);
		}
		goto out;
	}

	for_each_alloc_enabled_rdt_resource(r) {
		sep = false;
		seq_printf(s, "%*s:", max_name_width, r->name);
		list_for_each_entry(d, &r->domains, list) {
			if (sep)
				seq_putc(s, ';');
			if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
				size = 0;
			} else {
				ctrl = (!is_mba_sc(r) ?
						d->ctrl_val[rdtgrp->closid] :
						d->mbps_val[rdtgrp->closid]);
				if (r->rid == RDT_RESOURCE_MBA)
					size = ctrl;
				else
					size = rdtgroup_cbm_to_size(r, d, ctrl);
			}
			seq_printf(s, "%d=%u", d->id, size);
			sep = true;
		}
		seq_putc(s, '\n');
	}

out:
	rdtgroup_kn_unlock(of->kn);

	return ret;
}

/* rdtgroup information files for one cache resource. */
static struct rftype res_common_files[] = {
	{
		.name		= "last_cmd_status",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_last_cmd_status_show,
		.fflags		= RF_TOP_INFO,
	},
	{
		.name		= "num_closids",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_num_closids_show,
		.fflags		= RF_CTRL_INFO,
	},
	{
		.name		= "mon_features",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_mon_features_show,
		.fflags		= RF_MON_INFO,
	},
	{
		.name		= "num_rmids",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_num_rmids_show,
		.fflags		= RF_MON_INFO,
	},
	{
		.name		= "cbm_mask",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_default_ctrl_show,
		.fflags		= RF_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "min_cbm_bits",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_min_cbm_bits_show,
		.fflags		= RF_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "shareable_bits",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_shareable_bits_show,
		.fflags		= RF_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "bit_usage",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_bit_usage_show,
		.fflags		= RF_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "min_bandwidth",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_min_bw_show,
		.fflags		= RF_CTRL_INFO | RFTYPE_RES_MB,
	},
	{
		.name		= "bandwidth_gran",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_bw_gran_show,
		.fflags		= RF_CTRL_INFO | RFTYPE_RES_MB,
	},
	{
		.name		= "delay_linear",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_delay_linear_show,
		.fflags		= RF_CTRL_INFO | RFTYPE_RES_MB,
	},
	/*
	 * Platform specific which (if any) capabilities are provided by
	 * thread_throttle_mode. Defer "fflags" initialization to platform
	 * discovery.
	 */
	{
		.name		= "thread_throttle_mode",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_thread_throttle_mode_show,
	},
	{
		.name		= "max_threshold_occupancy",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= max_threshold_occ_write,
		.seq_show	= max_threshold_occ_show,
		.fflags		= RF_MON_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "cpus",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= rdtgroup_cpus_write,
		.seq_show	= rdtgroup_cpus_show,
		.fflags		= RFTYPE_BASE,
	},
	{
		.name		= "cpus_list",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= rdtgroup_cpus_write,
		.seq_show	= rdtgroup_cpus_show,
		.flags		= RFTYPE_FLAGS_CPUS_LIST,
		.fflags		= RFTYPE_BASE,
	},
	{
		.name		= "tasks",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= rdtgroup_tasks_write,
		.seq_show	= rdtgroup_tasks_show,
		.fflags		= RFTYPE_BASE,
	},
	{
		.name		= "schemata",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= rdtgroup_schemata_write,
		.seq_show	= rdtgroup_schemata_show,
		.fflags		= RF_CTRL_BASE,
	},
	{
		.name		= "mode",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= rdtgroup_mode_write,
		.seq_show	= rdtgroup_mode_show,
		.fflags		= RF_CTRL_BASE,
	},
	{
		.name		= "size",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdtgroup_size_show,
		.fflags		= RF_CTRL_BASE,
	},

};

static int rdtgroup_add_files(struct kernfs_node *kn, unsigned long fflags)
{
	struct rftype *rfts, *rft;
	int ret, len;

	rfts = res_common_files;
	len = ARRAY_SIZE(res_common_files);

	lockdep_assert_held(&rdtgroup_mutex);

	for (rft = rfts; rft < rfts + len; rft++) {
		if (rft->fflags && ((fflags & rft->fflags) == rft->fflags)) {
			ret = rdtgroup_add_file(kn, rft);
			if (ret)
				goto error;
		}
	}

	return 0;
error:
	pr_warn("Failed to add %s, err=%d\n", rft->name, ret);
	while (--rft >= rfts) {
		if ((fflags & rft->fflags) == rft->fflags)
			kernfs_remove_by_name(kn, rft->name);
	}
	return ret;
}

static struct rftype *rdtgroup_get_rftype_by_name(const char *name)
{
	struct rftype *rfts, *rft;
	int len;

	rfts = res_common_files;
	len = ARRAY_SIZE(res_common_files);

	for (rft = rfts; rft < rfts + len; rft++) {
		if (!strcmp(rft->name, name))
			return rft;
	}

	return NULL;
}

void __init thread_throttle_mode_init(void)
{
	struct rftype *rft;

	rft = rdtgroup_get_rftype_by_name("thread_throttle_mode");
	if (!rft)
		return;

	rft->fflags = RF_CTRL_INFO | RFTYPE_RES_MB;
}

/**
 * rdtgroup_kn_mode_restrict - Restrict user access to named resctrl file
 * @r: The resource group with which the file is associated.
 * @name: Name of the file
 *
 * The permissions of named resctrl file, directory, or link are modified
 * to not allow read, write, or execute by any user.
 *
 * WARNING: This function is intended to communicate to the user that the
 * resctrl file has been locked down - that it is not relevant to the
 * particular state the system finds itself in. It should not be relied
 * on to protect from user access because after the file's permissions
 * are restricted the user can still change the permissions using chmod
 * from the command line.
 *
 * Return: 0 on success, <0 on failure.
 */
int rdtgroup_kn_mode_restrict(struct rdtgroup *r, const char *name)
{
	struct iattr iattr = {.ia_valid = ATTR_MODE,};
	struct kernfs_node *kn;
	int ret = 0;

	kn = kernfs_find_and_get_ns(r->kn, name, NULL);
	if (!kn)
		return -ENOENT;

	switch (kernfs_type(kn)) {
	case KERNFS_DIR:
		iattr.ia_mode = S_IFDIR;
		break;
	case KERNFS_FILE:
		iattr.ia_mode = S_IFREG;
		break;
	case KERNFS_LINK:
		iattr.ia_mode = S_IFLNK;
		break;
	}

	ret = kernfs_setattr(kn, &iattr);
	kernfs_put(kn);
	return ret;
}

/**
 * rdtgroup_kn_mode_restore - Restore user access to named resctrl file
 * @r: The resource group with which the file is associated.
 * @name: Name of the file
 * @mask: Mask of permissions that should be restored
 *
 * Restore the permissions of the named file. If @name is a directory the
 * permissions of its parent will be used.
 *
 * Return: 0 on success, <0 on failure.
 */
int rdtgroup_kn_mode_restore(struct rdtgroup *r, const char *name,
			     umode_t mask)
{
	struct iattr iattr = {.ia_valid = ATTR_MODE,};
	struct kernfs_node *kn, *parent;
	struct rftype *rfts, *rft;
	int ret, len;

	rfts = res_common_files;
	len = ARRAY_SIZE(res_common_files);

	for (rft = rfts; rft < rfts + len; rft++) {
		if (!strcmp(rft->name, name))
			iattr.ia_mode = rft->mode & mask;
	}

	kn = kernfs_find_and_get_ns(r->kn, name, NULL);
	if (!kn)
		return -ENOENT;

	switch (kernfs_type(kn)) {
	case KERNFS_DIR:
		parent = kernfs_get_parent(kn);
		if (parent) {
			iattr.ia_mode |= parent->mode;
			kernfs_put(parent);
		}
		iattr.ia_mode |= S_IFDIR;
		break;
	case KERNFS_FILE:
		iattr.ia_mode |= S_IFREG;
		break;
	case KERNFS_LINK:
		iattr.ia_mode |= S_IFLNK;
		break;
	}

	ret = kernfs_setattr(kn, &iattr);
	kernfs_put(kn);
	return ret;
}

static int rdtgroup_mkdir_info_resdir(struct rdt_resource *r, char *name,
				      unsigned long fflags)
{
	struct kernfs_node *kn_subdir;
	int ret;

	kn_subdir = kernfs_create_dir(kn_info, name,
				      kn_info->mode, r);
	if (IS_ERR(kn_subdir))
		return PTR_ERR(kn_subdir);

	ret = rdtgroup_kn_set_ugid(kn_subdir);
	if (ret)
		return ret;

	ret = rdtgroup_add_files(kn_subdir, fflags);
	if (!ret)
		kernfs_activate(kn_subdir);

	return ret;
}

static int rdtgroup_create_info_dir(struct kernfs_node *parent_kn)
{
	struct rdt_resource *r;
	unsigned long fflags;
	char name[32];
	int ret;

	/* create the directory */
	kn_info = kernfs_create_dir(parent_kn, "info", parent_kn->mode, NULL);
	if (IS_ERR(kn_info))
		return PTR_ERR(kn_info);

	ret = rdtgroup_add_files(kn_info, RF_TOP_INFO);
	if (ret)
		goto out_destroy;

	for_each_alloc_enabled_rdt_resource(r) {
		fflags =  r->fflags | RF_CTRL_INFO;
		ret = rdtgroup_mkdir_info_resdir(r, r->name, fflags);
		if (ret)
			goto out_destroy;
	}

	for_each_mon_enabled_rdt_resource(r) {
		fflags =  r->fflags | RF_MON_INFO;
		sprintf(name, "%s_MON", r->name);
		ret = rdtgroup_mkdir_info_resdir(r, name, fflags);
		if (ret)
			goto out_destroy;
	}

	ret = rdtgroup_kn_set_ugid(kn_info);
	if (ret)
		goto out_destroy;

	kernfs_activate(kn_info);

	return 0;

out_destroy:
	kernfs_remove(kn_info);
	return ret;
}

static int
mongroup_create_dir(struct kernfs_node *parent_kn, struct rdtgroup *prgrp,
		    char *name, struct kernfs_node **dest_kn)
{
	struct kernfs_node *kn;
	int ret;

	/* create the directory */
	kn = kernfs_create_dir(parent_kn, name, parent_kn->mode, prgrp);
	if (IS_ERR(kn))
		return PTR_ERR(kn);

	if (dest_kn)
		*dest_kn = kn;

	ret = rdtgroup_kn_set_ugid(kn);
	if (ret)
		goto out_destroy;

	kernfs_activate(kn);

	return 0;

out_destroy:
	kernfs_remove(kn);
	return ret;
}

static void l3_qos_cfg_update(void *arg)
{
	bool *enable = arg;

	wrmsrl(MSR_IA32_L3_QOS_CFG, *enable ? L3_QOS_CDP_ENABLE : 0ULL);
}

static void l2_qos_cfg_update(void *arg)
{
	bool *enable = arg;

	wrmsrl(MSR_IA32_L2_QOS_CFG, *enable ? L2_QOS_CDP_ENABLE : 0ULL);
}

static inline bool is_mba_linear(void)
{
	return rdt_resources_all[RDT_RESOURCE_MBA].membw.delay_linear;
}

static int set_cache_qos_cfg(int level, bool enable)
{
	void (*update)(void *arg);
	struct rdt_resource *r_l;
	cpumask_var_t cpu_mask;
	struct rdt_domain *d;
	int cpu;

	if (level == RDT_RESOURCE_L3)
		update = l3_qos_cfg_update;
	else if (level == RDT_RESOURCE_L2)
		update = l2_qos_cfg_update;
	else
		return -EINVAL;

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	r_l = &rdt_resources_all[level];
	list_for_each_entry(d, &r_l->domains, list) {
		if (r_l->cache.arch_has_per_cpu_cfg)
			/* Pick all the CPUs in the domain instance */
			for_each_cpu(cpu, &d->cpu_mask)
				cpumask_set_cpu(cpu, cpu_mask);
		else
			/* Pick one CPU from each domain instance to update MSR */
			cpumask_set_cpu(cpumask_any(&d->cpu_mask), cpu_mask);
	}
	cpu = get_cpu();
	/* Update QOS_CFG MSR on this cpu if it's in cpu_mask. */
	if (cpumask_test_cpu(cpu, cpu_mask))
		update(&enable);
	/* Update QOS_CFG MSR on all other cpus in cpu_mask. */
	smp_call_function_many(cpu_mask, update, &enable, 1);
	put_cpu();

	free_cpumask_var(cpu_mask);

	return 0;
}

/* Restore the qos cfg state when a domain comes online */
void rdt_domain_reconfigure_cdp(struct rdt_resource *r)
{
	if (!r->alloc_capable)
		return;

	if (r == &rdt_resources_all[RDT_RESOURCE_L2DATA])
		l2_qos_cfg_update(&r->alloc_enabled);

	if (r == &rdt_resources_all[RDT_RESOURCE_L3DATA])
		l3_qos_cfg_update(&r->alloc_enabled);
}

/*
 * Enable or disable the MBA software controller
 * which helps user specify bandwidth in MBps.
 * MBA software controller is supported only if
 * MBM is supported and MBA is in linear scale.
 */
static int set_mba_sc(bool mba_sc)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_MBA];
	struct rdt_domain *d;

	if (!is_mbm_enabled() || !is_mba_linear() ||
	    mba_sc == is_mba_sc(r))
		return -EINVAL;

	r->membw.mba_sc = mba_sc;
	list_for_each_entry(d, &r->domains, list)
		setup_default_ctrlval(r, d->ctrl_val, d->mbps_val);

	return 0;
}

static int cdp_enable(int level, int data_type, int code_type)
{
	struct rdt_resource *r_ldata = &rdt_resources_all[data_type];
	struct rdt_resource *r_lcode = &rdt_resources_all[code_type];
	struct rdt_resource *r_l = &rdt_resources_all[level];
	int ret;

	if (!r_l->alloc_capable || !r_ldata->alloc_capable ||
	    !r_lcode->alloc_capable)
		return -EINVAL;

	ret = set_cache_qos_cfg(level, true);
	if (!ret) {
		r_l->alloc_enabled = false;
		r_ldata->alloc_enabled = true;
		r_lcode->alloc_enabled = true;
	}
	return ret;
}

static int cdpl3_enable(void)
{
	return cdp_enable(RDT_RESOURCE_L3, RDT_RESOURCE_L3DATA,
			  RDT_RESOURCE_L3CODE);
}

static int cdpl2_enable(void)
{
	return cdp_enable(RDT_RESOURCE_L2, RDT_RESOURCE_L2DATA,
			  RDT_RESOURCE_L2CODE);
}

static void cdp_disable(int level, int data_type, int code_type)
{
	struct rdt_resource *r = &rdt_resources_all[level];

	r->alloc_enabled = r->alloc_capable;

	if (rdt_resources_all[data_type].alloc_enabled) {
		rdt_resources_all[data_type].alloc_enabled = false;
		rdt_resources_all[code_type].alloc_enabled = false;
		set_cache_qos_cfg(level, false);
	}
}

static void cdpl3_disable(void)
{
	cdp_disable(RDT_RESOURCE_L3, RDT_RESOURCE_L3DATA, RDT_RESOURCE_L3CODE);
}

static void cdpl2_disable(void)
{
	cdp_disable(RDT_RESOURCE_L2, RDT_RESOURCE_L2DATA, RDT_RESOURCE_L2CODE);
}

static void cdp_disable_all(void)
{
	if (rdt_resources_all[RDT_RESOURCE_L3DATA].alloc_enabled)
		cdpl3_disable();
	if (rdt_resources_all[RDT_RESOURCE_L2DATA].alloc_enabled)
		cdpl2_disable();
}

/*
 * We don't allow rdtgroup directories to be created anywhere
 * except the root directory. Thus when looking for the rdtgroup
 * structure for a kernfs node we are either looking at a directory,
 * in which case the rdtgroup structure is pointed at by the "priv"
 * field, otherwise we have a file, and need only look to the parent
 * to find the rdtgroup.
 */
static struct rdtgroup *kernfs_to_rdtgroup(struct kernfs_node *kn)
{
	if (kernfs_type(kn) == KERNFS_DIR) {
		/*
		 * All the resource directories use "kn->priv"
		 * to point to the "struct rdtgroup" for the
		 * resource. "info" and its subdirectories don't
		 * have rdtgroup structures, so return NULL here.
		 */
		if (kn == kn_info || kn->parent == kn_info)
			return NULL;
		else
			return kn->priv;
	} else {
		return kn->parent->priv;
	}
}

struct rdtgroup *rdtgroup_kn_lock_live(struct kernfs_node *kn)
{
	struct rdtgroup *rdtgrp = kernfs_to_rdtgroup(kn);

	if (!rdtgrp)
		return NULL;

	atomic_inc(&rdtgrp->waitcount);
	kernfs_break_active_protection(kn);

	mutex_lock(&rdtgroup_mutex);

	/* Was this group deleted while we waited? */
	if (rdtgrp->flags & RDT_DELETED)
		return NULL;

	return rdtgrp;
}

void rdtgroup_kn_unlock(struct kernfs_node *kn)
{
	struct rdtgroup *rdtgrp = kernfs_to_rdtgroup(kn);

	if (!rdtgrp)
		return;

	mutex_unlock(&rdtgroup_mutex);

	if (atomic_dec_and_test(&rdtgrp->waitcount) &&
	    (rdtgrp->flags & RDT_DELETED)) {
		if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP ||
		    rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED)
			rdtgroup_pseudo_lock_remove(rdtgrp);
		kernfs_unbreak_active_protection(kn);
		rdtgroup_remove(rdtgrp);
	} else {
		kernfs_unbreak_active_protection(kn);
	}
}

static int mkdir_mondata_all(struct kernfs_node *parent_kn,
			     struct rdtgroup *prgrp,
			     struct kernfs_node **mon_data_kn);

static int rdt_enable_ctx(struct rdt_fs_context *ctx)
{
	int ret = 0;

	if (ctx->enable_cdpl2)
		ret = cdpl2_enable();

	if (!ret && ctx->enable_cdpl3)
		ret = cdpl3_enable();

	if (!ret && ctx->enable_mba_mbps)
		ret = set_mba_sc(true);

	return ret;
}

static int rdt_get_tree(struct fs_context *fc)
{
	struct rdt_fs_context *ctx = rdt_fc2context(fc);
	struct rdt_domain *dom;
	struct rdt_resource *r;
	int ret;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);
	/*
	 * resctrl file system can only be mounted once.
	 */
	if (static_branch_unlikely(&rdt_enable_key)) {
		ret = -EBUSY;
		goto out;
	}

	ret = rdt_enable_ctx(ctx);
	if (ret < 0)
		goto out_cdp;

	closid_init();

	ret = rdtgroup_create_info_dir(rdtgroup_default.kn);
	if (ret < 0)
		goto out_mba;

	if (rdt_mon_capable) {
		ret = mongroup_create_dir(rdtgroup_default.kn,
					  &rdtgroup_default, "mon_groups",
					  &kn_mongrp);
		if (ret < 0)
			goto out_info;

		ret = mkdir_mondata_all(rdtgroup_default.kn,
					&rdtgroup_default, &kn_mondata);
		if (ret < 0)
			goto out_mongrp;
		rdtgroup_default.mon.mon_data_kn = kn_mondata;
	}

	ret = rdt_pseudo_lock_init();
	if (ret)
		goto out_mondata;

	ret = kernfs_get_tree(fc);
	if (ret < 0)
		goto out_psl;

	if (rdt_alloc_capable)
		static_branch_enable_cpuslocked(&rdt_alloc_enable_key);
	if (rdt_mon_capable)
		static_branch_enable_cpuslocked(&rdt_mon_enable_key);

	if (rdt_alloc_capable || rdt_mon_capable)
		static_branch_enable_cpuslocked(&rdt_enable_key);

	if (is_mbm_enabled()) {
		r = &rdt_resources_all[RDT_RESOURCE_L3];
		list_for_each_entry(dom, &r->domains, list)
			mbm_setup_overflow_handler(dom, MBM_OVERFLOW_INTERVAL);
	}

	goto out;

out_psl:
	rdt_pseudo_lock_release();
out_mondata:
	if (rdt_mon_capable)
		kernfs_remove(kn_mondata);
out_mongrp:
	if (rdt_mon_capable)
		kernfs_remove(kn_mongrp);
out_info:
	kernfs_remove(kn_info);
out_mba:
	if (ctx->enable_mba_mbps)
		set_mba_sc(false);
out_cdp:
	cdp_disable_all();
out:
	rdt_last_cmd_clear();
	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();
	return ret;
}

enum rdt_param {
	Opt_cdp,
	Opt_cdpl2,
	Opt_mba_mbps,
	nr__rdt_params
};

static const struct fs_parameter_spec rdt_fs_parameters[] = {
	fsparam_flag("cdp",		Opt_cdp),
	fsparam_flag("cdpl2",		Opt_cdpl2),
	fsparam_flag("mba_MBps",	Opt_mba_mbps),
	{}
};

static int rdt_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct rdt_fs_context *ctx = rdt_fc2context(fc);
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, rdt_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_cdp:
		ctx->enable_cdpl3 = true;
		return 0;
	case Opt_cdpl2:
		ctx->enable_cdpl2 = true;
		return 0;
	case Opt_mba_mbps:
		if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
			return -EINVAL;
		ctx->enable_mba_mbps = true;
		return 0;
	}

	return -EINVAL;
}

static void rdt_fs_context_free(struct fs_context *fc)
{
	struct rdt_fs_context *ctx = rdt_fc2context(fc);

	kernfs_free_fs_context(fc);
	kfree(ctx);
}

static const struct fs_context_operations rdt_fs_context_ops = {
	.free		= rdt_fs_context_free,
	.parse_param	= rdt_parse_param,
	.get_tree	= rdt_get_tree,
};

static int rdt_init_fs_context(struct fs_context *fc)
{
	struct rdt_fs_context *ctx;

	ctx = kzalloc(sizeof(struct rdt_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->kfc.root = rdt_root;
	ctx->kfc.magic = RDTGROUP_SUPER_MAGIC;
	fc->fs_private = &ctx->kfc;
	fc->ops = &rdt_fs_context_ops;
	put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(&init_user_ns);
	fc->global = true;
	return 0;
}

static int reset_all_ctrls(struct rdt_resource *r)
{
	struct msr_param msr_param;
	cpumask_var_t cpu_mask;
	struct rdt_domain *d;
	int i, cpu;

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	msr_param.res = r;
	msr_param.low = 0;
	msr_param.high = r->num_closid;

	/*
	 * Disable resource control for this resource by setting all
	 * CBMs in all domains to the maximum mask value. Pick one CPU
	 * from each domain to update the MSRs below.
	 */
	list_for_each_entry(d, &r->domains, list) {
		cpumask_set_cpu(cpumask_any(&d->cpu_mask), cpu_mask);

		for (i = 0; i < r->num_closid; i++)
			d->ctrl_val[i] = r->default_ctrl;
	}
	cpu = get_cpu();
	/* Update CBM on this cpu if it's in cpu_mask. */
	if (cpumask_test_cpu(cpu, cpu_mask))
		rdt_ctrl_update(&msr_param);
	/* Update CBM on all other cpus in cpu_mask. */
	smp_call_function_many(cpu_mask, rdt_ctrl_update, &msr_param, 1);
	put_cpu();

	free_cpumask_var(cpu_mask);

	return 0;
}

/*
 * Move tasks from one to the other group. If @from is NULL, then all tasks
 * in the systems are moved unconditionally (used for teardown).
 *
 * If @mask is not NULL the cpus on which moved tasks are running are set
 * in that mask so the update smp function call is restricted to affected
 * cpus.
 */
static void rdt_move_group_tasks(struct rdtgroup *from, struct rdtgroup *to,
				 struct cpumask *mask)
{
	struct task_struct *p, *t;

	read_lock(&tasklist_lock);
	for_each_process_thread(p, t) {
		if (!from || is_closid_match(t, from) ||
		    is_rmid_match(t, from)) {
			WRITE_ONCE(t->closid, to->closid);
			WRITE_ONCE(t->rmid, to->mon.rmid);

			/*
			 * If the task is on a CPU, set the CPU in the mask.
			 * The detection is inaccurate as tasks might move or
			 * schedule before the smp function call takes place.
			 * In such a case the function call is pointless, but
			 * there is no other side effect.
			 */
			if (IS_ENABLED(CONFIG_SMP) && mask && task_curr(t))
				cpumask_set_cpu(task_cpu(t), mask);
		}
	}
	read_unlock(&tasklist_lock);
}

static void free_all_child_rdtgrp(struct rdtgroup *rdtgrp)
{
	struct rdtgroup *sentry, *stmp;
	struct list_head *head;

	head = &rdtgrp->mon.crdtgrp_list;
	list_for_each_entry_safe(sentry, stmp, head, mon.crdtgrp_list) {
		free_rmid(sentry->mon.rmid);
		list_del(&sentry->mon.crdtgrp_list);

		if (atomic_read(&sentry->waitcount) != 0)
			sentry->flags = RDT_DELETED;
		else
			rdtgroup_remove(sentry);
	}
}

/*
 * Forcibly remove all of subdirectories under root.
 */
static void rmdir_all_sub(void)
{
	struct rdtgroup *rdtgrp, *tmp;

	/* Move all tasks to the default resource group */
	rdt_move_group_tasks(NULL, &rdtgroup_default, NULL);

	list_for_each_entry_safe(rdtgrp, tmp, &rdt_all_groups, rdtgroup_list) {
		/* Free any child rmids */
		free_all_child_rdtgrp(rdtgrp);

		/* Remove each rdtgroup other than root */
		if (rdtgrp == &rdtgroup_default)
			continue;

		if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP ||
		    rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED)
			rdtgroup_pseudo_lock_remove(rdtgrp);

		/*
		 * Give any CPUs back to the default group. We cannot copy
		 * cpu_online_mask because a CPU might have executed the
		 * offline callback already, but is still marked online.
		 */
		cpumask_or(&rdtgroup_default.cpu_mask,
			   &rdtgroup_default.cpu_mask, &rdtgrp->cpu_mask);

		free_rmid(rdtgrp->mon.rmid);

		kernfs_remove(rdtgrp->kn);
		list_del(&rdtgrp->rdtgroup_list);

		if (atomic_read(&rdtgrp->waitcount) != 0)
			rdtgrp->flags = RDT_DELETED;
		else
			rdtgroup_remove(rdtgrp);
	}
	/* Notify online CPUs to update per cpu storage and PQR_ASSOC MSR */
	update_closid_rmid(cpu_online_mask, &rdtgroup_default);

	kernfs_remove(kn_info);
	kernfs_remove(kn_mongrp);
	kernfs_remove(kn_mondata);
}

static void rdt_kill_sb(struct super_block *sb)
{
	struct rdt_resource *r;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	set_mba_sc(false);

	/*Put everything back to default values. */
	for_each_alloc_enabled_rdt_resource(r)
		reset_all_ctrls(r);
	cdp_disable_all();
	rmdir_all_sub();
	rdt_pseudo_lock_release();
	rdtgroup_default.mode = RDT_MODE_SHAREABLE;
	static_branch_disable_cpuslocked(&rdt_alloc_enable_key);
	static_branch_disable_cpuslocked(&rdt_mon_enable_key);
	static_branch_disable_cpuslocked(&rdt_enable_key);
	kernfs_kill_sb(sb);
	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();
}

static struct file_system_type rdt_fs_type = {
	.name			= "resctrl",
	.init_fs_context	= rdt_init_fs_context,
	.parameters		= rdt_fs_parameters,
	.kill_sb		= rdt_kill_sb,
};

static int mon_addfile(struct kernfs_node *parent_kn, const char *name,
		       void *priv)
{
	struct kernfs_node *kn;
	int ret = 0;

	kn = __kernfs_create_file(parent_kn, name, 0444,
				  GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, 0,
				  &kf_mondata_ops, priv, NULL, NULL);
	if (IS_ERR(kn))
		return PTR_ERR(kn);

	ret = rdtgroup_kn_set_ugid(kn);
	if (ret) {
		kernfs_remove(kn);
		return ret;
	}

	return ret;
}

/*
 * Remove all subdirectories of mon_data of ctrl_mon groups
 * and monitor groups with given domain id.
 */
void rmdir_mondata_subdir_allrdtgrp(struct rdt_resource *r, unsigned int dom_id)
{
	struct rdtgroup *prgrp, *crgrp;
	char name[32];

	if (!r->mon_enabled)
		return;

	list_for_each_entry(prgrp, &rdt_all_groups, rdtgroup_list) {
		sprintf(name, "mon_%s_%02d", r->name, dom_id);
		kernfs_remove_by_name(prgrp->mon.mon_data_kn, name);

		list_for_each_entry(crgrp, &prgrp->mon.crdtgrp_list, mon.crdtgrp_list)
			kernfs_remove_by_name(crgrp->mon.mon_data_kn, name);
	}
}

static int mkdir_mondata_subdir(struct kernfs_node *parent_kn,
				struct rdt_domain *d,
				struct rdt_resource *r, struct rdtgroup *prgrp)
{
	union mon_data_bits priv;
	struct kernfs_node *kn;
	struct mon_evt *mevt;
	struct rmid_read rr;
	char name[32];
	int ret;

	sprintf(name, "mon_%s_%02d", r->name, d->id);
	/* create the directory */
	kn = kernfs_create_dir(parent_kn, name, parent_kn->mode, prgrp);
	if (IS_ERR(kn))
		return PTR_ERR(kn);

	ret = rdtgroup_kn_set_ugid(kn);
	if (ret)
		goto out_destroy;

	if (WARN_ON(list_empty(&r->evt_list))) {
		ret = -EPERM;
		goto out_destroy;
	}

	priv.u.rid = r->rid;
	priv.u.domid = d->id;
	list_for_each_entry(mevt, &r->evt_list, list) {
		priv.u.evtid = mevt->evtid;
		ret = mon_addfile(kn, mevt->name, priv.priv);
		if (ret)
			goto out_destroy;

		if (is_mbm_event(mevt->evtid))
			mon_event_read(&rr, r, d, prgrp, mevt->evtid, true);
	}
	kernfs_activate(kn);
	return 0;

out_destroy:
	kernfs_remove(kn);
	return ret;
}

/*
 * Add all subdirectories of mon_data for "ctrl_mon" groups
 * and "monitor" groups with given domain id.
 */
void mkdir_mondata_subdir_allrdtgrp(struct rdt_resource *r,
				    struct rdt_domain *d)
{
	struct kernfs_node *parent_kn;
	struct rdtgroup *prgrp, *crgrp;
	struct list_head *head;

	if (!r->mon_enabled)
		return;

	list_for_each_entry(prgrp, &rdt_all_groups, rdtgroup_list) {
		parent_kn = prgrp->mon.mon_data_kn;
		mkdir_mondata_subdir(parent_kn, d, r, prgrp);

		head = &prgrp->mon.crdtgrp_list;
		list_for_each_entry(crgrp, head, mon.crdtgrp_list) {
			parent_kn = crgrp->mon.mon_data_kn;
			mkdir_mondata_subdir(parent_kn, d, r, crgrp);
		}
	}
}

static int mkdir_mondata_subdir_alldom(struct kernfs_node *parent_kn,
				       struct rdt_resource *r,
				       struct rdtgroup *prgrp)
{
	struct rdt_domain *dom;
	int ret;

	list_for_each_entry(dom, &r->domains, list) {
		ret = mkdir_mondata_subdir(parent_kn, dom, r, prgrp);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * This creates a directory mon_data which contains the monitored data.
 *
 * mon_data has one directory for each domain which are named
 * in the format mon_<domain_name>_<domain_id>. For ex: A mon_data
 * with L3 domain looks as below:
 * ./mon_data:
 * mon_L3_00
 * mon_L3_01
 * mon_L3_02
 * ...
 *
 * Each domain directory has one file per event:
 * ./mon_L3_00/:
 * llc_occupancy
 *
 */
static int mkdir_mondata_all(struct kernfs_node *parent_kn,
			     struct rdtgroup *prgrp,
			     struct kernfs_node **dest_kn)
{
	struct rdt_resource *r;
	struct kernfs_node *kn;
	int ret;

	/*
	 * Create the mon_data directory first.
	 */
	ret = mongroup_create_dir(parent_kn, prgrp, "mon_data", &kn);
	if (ret)
		return ret;

	if (dest_kn)
		*dest_kn = kn;

	/*
	 * Create the subdirectories for each domain. Note that all events
	 * in a domain like L3 are grouped into a resource whose domain is L3
	 */
	for_each_mon_enabled_rdt_resource(r) {
		ret = mkdir_mondata_subdir_alldom(kn, r, prgrp);
		if (ret)
			goto out_destroy;
	}

	return 0;

out_destroy:
	kernfs_remove(kn);
	return ret;
}

/**
 * cbm_ensure_valid - Enforce validity on provided CBM
 * @_val:	Candidate CBM
 * @r:		RDT resource to which the CBM belongs
 *
 * The provided CBM represents all cache portions available for use. This
 * may be represented by a bitmap that does not consist of contiguous ones
 * and thus be an invalid CBM.
 * Here the provided CBM is forced to be a valid CBM by only considering
 * the first set of contiguous bits as valid and clearing all bits.
 * The intention here is to provide a valid default CBM with which a new
 * resource group is initialized. The user can follow this with a
 * modification to the CBM if the default does not satisfy the
 * requirements.
 */
static u32 cbm_ensure_valid(u32 _val, struct rdt_resource *r)
{
	unsigned int cbm_len = r->cache.cbm_len;
	unsigned long first_bit, zero_bit;
	unsigned long val = _val;

	if (!val)
		return 0;

	first_bit = find_first_bit(&val, cbm_len);
	zero_bit = find_next_zero_bit(&val, cbm_len, first_bit);

	/* Clear any remaining bits to ensure contiguous region */
	bitmap_clear(&val, zero_bit, cbm_len - zero_bit);
	return (u32)val;
}

/*
 * Initialize cache resources per RDT domain
 *
 * Set the RDT domain up to start off with all usable allocations. That is,
 * all shareable and unused bits. All-zero CBM is invalid.
 */
static int __init_one_rdt_domain(struct rdt_domain *d, struct rdt_resource *r,
				 u32 closid)
{
	struct rdt_resource *r_cdp = NULL;
	struct rdt_domain *d_cdp = NULL;
	u32 used_b = 0, unused_b = 0;
	unsigned long tmp_cbm;
	enum rdtgrp_mode mode;
	u32 peer_ctl, *ctrl;
	int i;

	rdt_cdp_peer_get(r, d, &r_cdp, &d_cdp);
	d->have_new_ctrl = false;
	d->new_ctrl = r->cache.shareable_bits;
	used_b = r->cache.shareable_bits;
	ctrl = d->ctrl_val;
	for (i = 0; i < closids_supported(); i++, ctrl++) {
		if (closid_allocated(i) && i != closid) {
			mode = rdtgroup_mode_by_closid(i);
			if (mode == RDT_MODE_PSEUDO_LOCKSETUP)
				/*
				 * ctrl values for locksetup aren't relevant
				 * until the schemata is written, and the mode
				 * becomes RDT_MODE_PSEUDO_LOCKED.
				 */
				continue;
			/*
			 * If CDP is active include peer domain's
			 * usage to ensure there is no overlap
			 * with an exclusive group.
			 */
			if (d_cdp)
				peer_ctl = d_cdp->ctrl_val[i];
			else
				peer_ctl = 0;
			used_b |= *ctrl | peer_ctl;
			if (mode == RDT_MODE_SHAREABLE)
				d->new_ctrl |= *ctrl | peer_ctl;
		}
	}
	if (d->plr && d->plr->cbm > 0)
		used_b |= d->plr->cbm;
	unused_b = used_b ^ (BIT_MASK(r->cache.cbm_len) - 1);
	unused_b &= BIT_MASK(r->cache.cbm_len) - 1;
	d->new_ctrl |= unused_b;
	/*
	 * Force the initial CBM to be valid, user can
	 * modify the CBM based on system availability.
	 */
	d->new_ctrl = cbm_ensure_valid(d->new_ctrl, r);
	/*
	 * Assign the u32 CBM to an unsigned long to ensure that
	 * bitmap_weight() does not access out-of-bound memory.
	 */
	tmp_cbm = d->new_ctrl;
	if (bitmap_weight(&tmp_cbm, r->cache.cbm_len) < r->cache.min_cbm_bits) {
		rdt_last_cmd_printf("No space on %s:%d\n", r->name, d->id);
		return -ENOSPC;
	}
	d->have_new_ctrl = true;

	return 0;
}

/*
 * Initialize cache resources with default values.
 *
 * A new RDT group is being created on an allocation capable (CAT)
 * supporting system. Set this group up to start off with all usable
 * allocations.
 *
 * If there are no more shareable bits available on any domain then
 * the entire allocation will fail.
 */
static int rdtgroup_init_cat(struct rdt_resource *r, u32 closid)
{
	struct rdt_domain *d;
	int ret;

	list_for_each_entry(d, &r->domains, list) {
		ret = __init_one_rdt_domain(d, r, closid);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* Initialize MBA resource with default values. */
static void rdtgroup_init_mba(struct rdt_resource *r)
{
	struct rdt_domain *d;

	list_for_each_entry(d, &r->domains, list) {
		d->new_ctrl = is_mba_sc(r) ? MBA_MAX_MBPS : r->default_ctrl;
		d->have_new_ctrl = true;
	}
}

/* Initialize the RDT group's allocations. */
static int rdtgroup_init_alloc(struct rdtgroup *rdtgrp)
{
	struct rdt_resource *r;
	int ret;

	for_each_alloc_enabled_rdt_resource(r) {
		if (r->rid == RDT_RESOURCE_MBA) {
			rdtgroup_init_mba(r);
		} else {
			ret = rdtgroup_init_cat(r, rdtgrp->closid);
			if (ret < 0)
				return ret;
		}

		ret = update_domains(r, rdtgrp->closid);
		if (ret < 0) {
			rdt_last_cmd_puts("Failed to initialize allocations\n");
			return ret;
		}

	}

	rdtgrp->mode = RDT_MODE_SHAREABLE;

	return 0;
}

static int mkdir_rdt_prepare(struct kernfs_node *parent_kn,
			     const char *name, umode_t mode,
			     enum rdt_group_type rtype, struct rdtgroup **r)
{
	struct rdtgroup *prdtgrp, *rdtgrp;
	struct kernfs_node *kn;
	uint files = 0;
	int ret;

	prdtgrp = rdtgroup_kn_lock_live(parent_kn);
	if (!prdtgrp) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (rtype == RDTMON_GROUP &&
	    (prdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP ||
	     prdtgrp->mode == RDT_MODE_PSEUDO_LOCKED)) {
		ret = -EINVAL;
		rdt_last_cmd_puts("Pseudo-locking in progress\n");
		goto out_unlock;
	}

	/* allocate the rdtgroup. */
	rdtgrp = kzalloc(sizeof(*rdtgrp), GFP_KERNEL);
	if (!rdtgrp) {
		ret = -ENOSPC;
		rdt_last_cmd_puts("Kernel out of memory\n");
		goto out_unlock;
	}
	*r = rdtgrp;
	rdtgrp->mon.parent = prdtgrp;
	rdtgrp->type = rtype;
	INIT_LIST_HEAD(&rdtgrp->mon.crdtgrp_list);

	/* kernfs creates the directory for rdtgrp */
	kn = kernfs_create_dir(parent_kn, name, mode, rdtgrp);
	if (IS_ERR(kn)) {
		ret = PTR_ERR(kn);
		rdt_last_cmd_puts("kernfs create error\n");
		goto out_free_rgrp;
	}
	rdtgrp->kn = kn;

	/*
	 * kernfs_remove() will drop the reference count on "kn" which
	 * will free it. But we still need it to stick around for the
	 * rdtgroup_kn_unlock(kn) call. Take one extra reference here,
	 * which will be dropped by kernfs_put() in rdtgroup_remove().
	 */
	kernfs_get(kn);

	ret = rdtgroup_kn_set_ugid(kn);
	if (ret) {
		rdt_last_cmd_puts("kernfs perm error\n");
		goto out_destroy;
	}

	files = RFTYPE_BASE | BIT(RF_CTRLSHIFT + rtype);
	ret = rdtgroup_add_files(kn, files);
	if (ret) {
		rdt_last_cmd_puts("kernfs fill error\n");
		goto out_destroy;
	}

	if (rdt_mon_capable) {
		ret = alloc_rmid();
		if (ret < 0) {
			rdt_last_cmd_puts("Out of RMIDs\n");
			goto out_destroy;
		}
		rdtgrp->mon.rmid = ret;

		ret = mkdir_mondata_all(kn, rdtgrp, &rdtgrp->mon.mon_data_kn);
		if (ret) {
			rdt_last_cmd_puts("kernfs subdir error\n");
			goto out_idfree;
		}
	}
	kernfs_activate(kn);

	/*
	 * The caller unlocks the parent_kn upon success.
	 */
	return 0;

out_idfree:
	free_rmid(rdtgrp->mon.rmid);
out_destroy:
	kernfs_put(rdtgrp->kn);
	kernfs_remove(rdtgrp->kn);
out_free_rgrp:
	kfree(rdtgrp);
out_unlock:
	rdtgroup_kn_unlock(parent_kn);
	return ret;
}

static void mkdir_rdt_prepare_clean(struct rdtgroup *rgrp)
{
	kernfs_remove(rgrp->kn);
	free_rmid(rgrp->mon.rmid);
	rdtgroup_remove(rgrp);
}

/*
 * Create a monitor group under "mon_groups" directory of a control
 * and monitor group(ctrl_mon). This is a resource group
 * to monitor a subset of tasks and cpus in its parent ctrl_mon group.
 */
static int rdtgroup_mkdir_mon(struct kernfs_node *parent_kn,
			      const char *name, umode_t mode)
{
	struct rdtgroup *rdtgrp, *prgrp;
	int ret;

	ret = mkdir_rdt_prepare(parent_kn, name, mode, RDTMON_GROUP, &rdtgrp);
	if (ret)
		return ret;

	prgrp = rdtgrp->mon.parent;
	rdtgrp->closid = prgrp->closid;

	/*
	 * Add the rdtgrp to the list of rdtgrps the parent
	 * ctrl_mon group has to track.
	 */
	list_add_tail(&rdtgrp->mon.crdtgrp_list, &prgrp->mon.crdtgrp_list);

	rdtgroup_kn_unlock(parent_kn);
	return ret;
}

/*
 * These are rdtgroups created under the root directory. Can be used
 * to allocate and monitor resources.
 */
static int rdtgroup_mkdir_ctrl_mon(struct kernfs_node *parent_kn,
				   const char *name, umode_t mode)
{
	struct rdtgroup *rdtgrp;
	struct kernfs_node *kn;
	u32 closid;
	int ret;

	ret = mkdir_rdt_prepare(parent_kn, name, mode, RDTCTRL_GROUP, &rdtgrp);
	if (ret)
		return ret;

	kn = rdtgrp->kn;
	ret = closid_alloc();
	if (ret < 0) {
		rdt_last_cmd_puts("Out of CLOSIDs\n");
		goto out_common_fail;
	}
	closid = ret;
	ret = 0;

	rdtgrp->closid = closid;
	ret = rdtgroup_init_alloc(rdtgrp);
	if (ret < 0)
		goto out_id_free;

	list_add(&rdtgrp->rdtgroup_list, &rdt_all_groups);

	if (rdt_mon_capable) {
		/*
		 * Create an empty mon_groups directory to hold the subset
		 * of tasks and cpus to monitor.
		 */
		ret = mongroup_create_dir(kn, rdtgrp, "mon_groups", NULL);
		if (ret) {
			rdt_last_cmd_puts("kernfs subdir error\n");
			goto out_del_list;
		}
	}

	goto out_unlock;

out_del_list:
	list_del(&rdtgrp->rdtgroup_list);
out_id_free:
	closid_free(closid);
out_common_fail:
	mkdir_rdt_prepare_clean(rdtgrp);
out_unlock:
	rdtgroup_kn_unlock(parent_kn);
	return ret;
}

/*
 * We allow creating mon groups only with in a directory called "mon_groups"
 * which is present in every ctrl_mon group. Check if this is a valid
 * "mon_groups" directory.
 *
 * 1. The directory should be named "mon_groups".
 * 2. The mon group itself should "not" be named "mon_groups".
 *   This makes sure "mon_groups" directory always has a ctrl_mon group
 *   as parent.
 */
static bool is_mon_groups(struct kernfs_node *kn, const char *name)
{
	return (!strcmp(kn->name, "mon_groups") &&
		strcmp(name, "mon_groups"));
}

static int rdtgroup_mkdir(struct kernfs_node *parent_kn, const char *name,
			  umode_t mode)
{
	/* Do not accept '\n' to avoid unparsable situation. */
	if (strchr(name, '\n'))
		return -EINVAL;

	/*
	 * If the parent directory is the root directory and RDT
	 * allocation is supported, add a control and monitoring
	 * subdirectory
	 */
	if (rdt_alloc_capable && parent_kn == rdtgroup_default.kn)
		return rdtgroup_mkdir_ctrl_mon(parent_kn, name, mode);

	/*
	 * If RDT monitoring is supported and the parent directory is a valid
	 * "mon_groups" directory, add a monitoring subdirectory.
	 */
	if (rdt_mon_capable && is_mon_groups(parent_kn, name))
		return rdtgroup_mkdir_mon(parent_kn, name, mode);

	return -EPERM;
}

static int rdtgroup_rmdir_mon(struct rdtgroup *rdtgrp, cpumask_var_t tmpmask)
{
	struct rdtgroup *prdtgrp = rdtgrp->mon.parent;
	int cpu;

	/* Give any tasks back to the parent group */
	rdt_move_group_tasks(rdtgrp, prdtgrp, tmpmask);

	/* Update per cpu rmid of the moved CPUs first */
	for_each_cpu(cpu, &rdtgrp->cpu_mask)
		per_cpu(pqr_state.default_rmid, cpu) = prdtgrp->mon.rmid;
	/*
	 * Update the MSR on moved CPUs and CPUs which have moved
	 * task running on them.
	 */
	cpumask_or(tmpmask, tmpmask, &rdtgrp->cpu_mask);
	update_closid_rmid(tmpmask, NULL);

	rdtgrp->flags = RDT_DELETED;
	free_rmid(rdtgrp->mon.rmid);

	/*
	 * Remove the rdtgrp from the parent ctrl_mon group's list
	 */
	WARN_ON(list_empty(&prdtgrp->mon.crdtgrp_list));
	list_del(&rdtgrp->mon.crdtgrp_list);

	kernfs_remove(rdtgrp->kn);

	return 0;
}

static int rdtgroup_ctrl_remove(struct rdtgroup *rdtgrp)
{
	rdtgrp->flags = RDT_DELETED;
	list_del(&rdtgrp->rdtgroup_list);

	kernfs_remove(rdtgrp->kn);
	return 0;
}

static int rdtgroup_rmdir_ctrl(struct rdtgroup *rdtgrp, cpumask_var_t tmpmask)
{
	int cpu;

	/* Give any tasks back to the default group */
	rdt_move_group_tasks(rdtgrp, &rdtgroup_default, tmpmask);

	/* Give any CPUs back to the default group */
	cpumask_or(&rdtgroup_default.cpu_mask,
		   &rdtgroup_default.cpu_mask, &rdtgrp->cpu_mask);

	/* Update per cpu closid and rmid of the moved CPUs first */
	for_each_cpu(cpu, &rdtgrp->cpu_mask) {
		per_cpu(pqr_state.default_closid, cpu) = rdtgroup_default.closid;
		per_cpu(pqr_state.default_rmid, cpu) = rdtgroup_default.mon.rmid;
	}

	/*
	 * Update the MSR on moved CPUs and CPUs which have moved
	 * task running on them.
	 */
	cpumask_or(tmpmask, tmpmask, &rdtgrp->cpu_mask);
	update_closid_rmid(tmpmask, NULL);

	closid_free(rdtgrp->closid);
	free_rmid(rdtgrp->mon.rmid);

	rdtgroup_ctrl_remove(rdtgrp);

	/*
	 * Free all the child monitor group rmids.
	 */
	free_all_child_rdtgrp(rdtgrp);

	return 0;
}

static int rdtgroup_rmdir(struct kernfs_node *kn)
{
	struct kernfs_node *parent_kn = kn->parent;
	struct rdtgroup *rdtgrp;
	cpumask_var_t tmpmask;
	int ret = 0;

	if (!zalloc_cpumask_var(&tmpmask, GFP_KERNEL))
		return -ENOMEM;

	rdtgrp = rdtgroup_kn_lock_live(kn);
	if (!rdtgrp) {
		ret = -EPERM;
		goto out;
	}

	/*
	 * If the rdtgroup is a ctrl_mon group and parent directory
	 * is the root directory, remove the ctrl_mon group.
	 *
	 * If the rdtgroup is a mon group and parent directory
	 * is a valid "mon_groups" directory, remove the mon group.
	 */
	if (rdtgrp->type == RDTCTRL_GROUP && parent_kn == rdtgroup_default.kn &&
	    rdtgrp != &rdtgroup_default) {
		if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP ||
		    rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED) {
			ret = rdtgroup_ctrl_remove(rdtgrp);
		} else {
			ret = rdtgroup_rmdir_ctrl(rdtgrp, tmpmask);
		}
	} else if (rdtgrp->type == RDTMON_GROUP &&
		 is_mon_groups(parent_kn, kn->name)) {
		ret = rdtgroup_rmdir_mon(rdtgrp, tmpmask);
	} else {
		ret = -EPERM;
	}

out:
	rdtgroup_kn_unlock(kn);
	free_cpumask_var(tmpmask);
	return ret;
}

static int rdtgroup_show_options(struct seq_file *seq, struct kernfs_root *kf)
{
	if (rdt_resources_all[RDT_RESOURCE_L3DATA].alloc_enabled)
		seq_puts(seq, ",cdp");

	if (rdt_resources_all[RDT_RESOURCE_L2DATA].alloc_enabled)
		seq_puts(seq, ",cdpl2");

	if (is_mba_sc(&rdt_resources_all[RDT_RESOURCE_MBA]))
		seq_puts(seq, ",mba_MBps");

	return 0;
}

static struct kernfs_syscall_ops rdtgroup_kf_syscall_ops = {
	.mkdir		= rdtgroup_mkdir,
	.rmdir		= rdtgroup_rmdir,
	.show_options	= rdtgroup_show_options,
};

static int __init rdtgroup_setup_root(void)
{
	int ret;

	rdt_root = kernfs_create_root(&rdtgroup_kf_syscall_ops,
				      KERNFS_ROOT_CREATE_DEACTIVATED |
				      KERNFS_ROOT_EXTRA_OPEN_PERM_CHECK,
				      &rdtgroup_default);
	if (IS_ERR(rdt_root))
		return PTR_ERR(rdt_root);

	mutex_lock(&rdtgroup_mutex);

	rdtgroup_default.closid = 0;
	rdtgroup_default.mon.rmid = 0;
	rdtgroup_default.type = RDTCTRL_GROUP;
	INIT_LIST_HEAD(&rdtgroup_default.mon.crdtgrp_list);

	list_add(&rdtgroup_default.rdtgroup_list, &rdt_all_groups);

	ret = rdtgroup_add_files(rdt_root->kn, RF_CTRL_BASE);
	if (ret) {
		kernfs_destroy_root(rdt_root);
		goto out;
	}

	rdtgroup_default.kn = rdt_root->kn;
	kernfs_activate(rdtgroup_default.kn);

out:
	mutex_unlock(&rdtgroup_mutex);

	return ret;
}

/*
 * rdtgroup_init - rdtgroup initialization
 *
 * Setup resctrl file system including set up root, create mount point,
 * register rdtgroup filesystem, and initialize files under root directory.
 *
 * Return: 0 on success or -errno
 */
int __init rdtgroup_init(void)
{
	int ret = 0;

	seq_buf_init(&last_cmd_status, last_cmd_status_buf,
		     sizeof(last_cmd_status_buf));

	ret = rdtgroup_setup_root();
	if (ret)
		return ret;

	ret = sysfs_create_mount_point(fs_kobj, "resctrl");
	if (ret)
		goto cleanup_root;

	ret = register_filesystem(&rdt_fs_type);
	if (ret)
		goto cleanup_mountpoint;

	/*
	 * Adding the resctrl debugfs directory here may not be ideal since
	 * it would let the resctrl debugfs directory appear on the debugfs
	 * filesystem before the resctrl filesystem is mounted.
	 * It may also be ok since that would enable debugging of RDT before
	 * resctrl is mounted.
	 * The reason why the debugfs directory is created here and not in
	 * rdt_get_tree() is because rdt_get_tree() takes rdtgroup_mutex and
	 * during the debugfs directory creation also &sb->s_type->i_mutex_key
	 * (the lockdep class of inode->i_rwsem). Other filesystem
	 * interactions (eg. SyS_getdents) have the lock ordering:
	 * &sb->s_type->i_mutex_key --> &mm->mmap_lock
	 * During mmap(), called with &mm->mmap_lock, the rdtgroup_mutex
	 * is taken, thus creating dependency:
	 * &mm->mmap_lock --> rdtgroup_mutex for the latter that can cause
	 * issues considering the other two lock dependencies.
	 * By creating the debugfs directory here we avoid a dependency
	 * that may cause deadlock (even though file operations cannot
	 * occur until the filesystem is mounted, but I do not know how to
	 * tell lockdep that).
	 */
	debugfs_resctrl = debugfs_create_dir("resctrl", NULL);

	return 0;

cleanup_mountpoint:
	sysfs_remove_mount_point(fs_kobj, "resctrl");
cleanup_root:
	kernfs_destroy_root(rdt_root);

	return ret;
}

void __exit rdtgroup_exit(void)
{
	debugfs_remove_recursive(debugfs_resctrl);
	unregister_filesystem(&rdt_fs_type);
	sysfs_remove_mount_point(fs_kobj, "resctrl");
	kernfs_destroy_root(rdt_root);
}
