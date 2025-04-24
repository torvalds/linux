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

/* Mutex to protect rdtgroup access. */
DEFINE_MUTEX(rdtgroup_mutex);

static struct kernfs_root *rdt_root;
struct rdtgroup rdtgroup_default;
LIST_HEAD(rdt_all_groups);

/* list of entries for the schemata file */
LIST_HEAD(resctrl_schema_all);

/* The filesystem can only be mounted once. */
bool resctrl_mounted;

/* Kernel fs node for "info" directory under root */
static struct kernfs_node *kn_info;

/* Kernel fs node for "mon_groups" directory under root */
static struct kernfs_node *kn_mongrp;

/* Kernel fs node for "mon_data" directory under root */
static struct kernfs_node *kn_mondata;

/*
 * Used to store the max resource name width to display the schemata names in
 * a tabular format.
 */
int max_name_width;

static struct seq_buf last_cmd_status;
static char last_cmd_status_buf[512];

static int rdtgroup_setup_root(struct rdt_fs_context *ctx);
static void rdtgroup_destroy_root(void);

struct dentry *debugfs_resctrl;

/*
 * Memory bandwidth monitoring event to use for the default CTRL_MON group
 * and each new CTRL_MON group created by the user.  Only relevant when
 * the filesystem is mounted with the "mba_MBps" option so it does not
 * matter that it remains uninitialized on systems that do not support
 * the "mba_MBps" option.
 */
enum resctrl_event_id mba_mbps_default_event;

static bool resctrl_debug;

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

void rdt_staged_configs_clear(void)
{
	struct rdt_ctrl_domain *dom;
	struct rdt_resource *r;

	lockdep_assert_held(&rdtgroup_mutex);

	for_each_alloc_capable_rdt_resource(r) {
		list_for_each_entry(dom, &r->ctrl_domains, hdr.list)
			memset(dom->staged_config, 0, sizeof(dom->staged_config));
	}
}

static bool resctrl_is_mbm_enabled(void)
{
	return (resctrl_arch_is_mbm_total_enabled() ||
		resctrl_arch_is_mbm_local_enabled());
}

static bool resctrl_is_mbm_event(int e)
{
	return (e >= QOS_L3_MBM_TOTAL_EVENT_ID &&
		e <= QOS_L3_MBM_LOCAL_EVENT_ID);
}

/*
 * Trivial allocator for CLOSIDs. Since h/w only supports a small number,
 * we can keep a bitmap of free CLOSIDs in a single integer.
 *
 * Using a global CLOSID across all resources has some advantages and
 * some drawbacks:
 * + We can simply set current's closid to assign a task to a resource
 *   group.
 * + Context switch code can avoid extra memory references deciding which
 *   CLOSID to load into the PQR_ASSOC MSR
 * - We give up some options in configuring resource groups across multi-socket
 *   systems.
 * - Our choices on how to configure each resource become progressively more
 *   limited as the number of resources grows.
 */
static unsigned long closid_free_map;
static int closid_free_map_len;

int closids_supported(void)
{
	return closid_free_map_len;
}

static void closid_init(void)
{
	struct resctrl_schema *s;
	u32 rdt_min_closid = 32;

	/* Compute rdt_min_closid across all resources */
	list_for_each_entry(s, &resctrl_schema_all, list)
		rdt_min_closid = min(rdt_min_closid, s->num_closid);

	closid_free_map = BIT_MASK(rdt_min_closid) - 1;

	/* RESCTRL_RESERVED_CLOSID is always reserved for the default group */
	__clear_bit(RESCTRL_RESERVED_CLOSID, &closid_free_map);
	closid_free_map_len = rdt_min_closid;
}

static int closid_alloc(void)
{
	int cleanest_closid;
	u32 closid;

	lockdep_assert_held(&rdtgroup_mutex);

	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID) &&
	    resctrl_arch_is_llc_occupancy_enabled()) {
		cleanest_closid = resctrl_find_cleanest_closid();
		if (cleanest_closid < 0)
			return cleanest_closid;
		closid = cleanest_closid;
	} else {
		closid = ffs(closid_free_map);
		if (closid == 0)
			return -ENOSPC;
		closid--;
	}
	__clear_bit(closid, &closid_free_map);

	return closid;
}

void closid_free(int closid)
{
	lockdep_assert_held(&rdtgroup_mutex);

	__set_bit(closid, &closid_free_map);
}

/**
 * closid_allocated - test if provided closid is in use
 * @closid: closid to be tested
 *
 * Return: true if @closid is currently associated with a resource group,
 * false if @closid is free
 */
bool closid_allocated(unsigned int closid)
{
	lockdep_assert_held(&rdtgroup_mutex);

	return !test_bit(closid, &closid_free_map);
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
				mask = &rdtgrp->plr->d->hdr.cpu_mask;
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
void resctrl_arch_sync_cpu_closid_rmid(void *info)
{
	struct resctrl_cpu_defaults *r = info;

	if (r) {
		this_cpu_write(pqr_state.default_closid, r->closid);
		this_cpu_write(pqr_state.default_rmid, r->rmid);
	}

	/*
	 * We cannot unconditionally write the MSR because the current
	 * executing task might have its own closid selected. Just reuse
	 * the context switch code.
	 */
	resctrl_sched_in(current);
}

/*
 * Update the PGR_ASSOC MSR on all cpus in @cpu_mask,
 *
 * Per task closids/rmids must have been set up before calling this function.
 * @r may be NULL.
 */
static void
update_closid_rmid(const struct cpumask *cpu_mask, struct rdtgroup *r)
{
	struct resctrl_cpu_defaults defaults, *p = NULL;

	if (r) {
		defaults.closid = r->closid;
		defaults.rmid = r->mon.rmid;
		p = &defaults;
	}

	on_each_cpu_mask(cpu_mask, resctrl_arch_sync_cpu_closid_rmid, p, 1);
}

static int cpus_mon_write(struct rdtgroup *rdtgrp, cpumask_var_t newmask,
			  cpumask_var_t tmpmask)
{
	struct rdtgroup *prgrp = rdtgrp->mon.parent, *crgrp;
	struct list_head *head;

	/* Check whether cpus belong to parent ctrl group */
	cpumask_andnot(tmpmask, newmask, &prgrp->cpu_mask);
	if (!cpumask_empty(tmpmask)) {
		rdt_last_cmd_puts("Can only add CPUs to mongroup that belong to parent\n");
		return -EINVAL;
	}

	/* Check whether cpus are dropped from this group */
	cpumask_andnot(tmpmask, &rdtgrp->cpu_mask, newmask);
	if (!cpumask_empty(tmpmask)) {
		/* Give any dropped cpus to parent rdtgroup */
		cpumask_or(&prgrp->cpu_mask, &prgrp->cpu_mask, tmpmask);
		update_closid_rmid(tmpmask, prgrp);
	}

	/*
	 * If we added cpus, remove them from previous group that owned them
	 * and update per-cpu rmid
	 */
	cpumask_andnot(tmpmask, newmask, &rdtgrp->cpu_mask);
	if (!cpumask_empty(tmpmask)) {
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
	if (!cpumask_empty(tmpmask)) {
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
	if (!cpumask_empty(tmpmask)) {
		list_for_each_entry(r, &rdt_all_groups, rdtgroup_list) {
			if (r == rdtgrp)
				continue;
			cpumask_and(tmpmask1, &r->cpu_mask, tmpmask);
			if (!cpumask_empty(tmpmask1))
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
	if (!cpumask_empty(tmpmask)) {
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
		resctrl_sched_in(task);
}

static void update_task_closid_rmid(struct task_struct *t)
{
	if (IS_ENABLED(CONFIG_SMP) && task_curr(t))
		smp_call_function_single(task_cpu(t), _update_task_closid_rmid, t, 1);
	else
		_update_task_closid_rmid(t);
}

static bool task_in_rdtgroup(struct task_struct *tsk, struct rdtgroup *rdtgrp)
{
	u32 closid, rmid = rdtgrp->mon.rmid;

	if (rdtgrp->type == RDTCTRL_GROUP)
		closid = rdtgrp->closid;
	else if (rdtgrp->type == RDTMON_GROUP)
		closid = rdtgrp->mon.parent->closid;
	else
		return false;

	return resctrl_arch_match_closid(tsk, closid) &&
	       resctrl_arch_match_rmid(tsk, closid, rmid);
}

static int __rdtgroup_move_task(struct task_struct *tsk,
				struct rdtgroup *rdtgrp)
{
	/* If the task is already in rdtgrp, no need to move the task. */
	if (task_in_rdtgroup(tsk, rdtgrp))
		return 0;

	/*
	 * Set the task's closid/rmid before the PQR_ASSOC MSR can be
	 * updated by them.
	 *
	 * For ctrl_mon groups, move both closid and rmid.
	 * For monitor groups, can move the tasks only from
	 * their parent CTRL group.
	 */
	if (rdtgrp->type == RDTMON_GROUP &&
	    !resctrl_arch_match_closid(tsk, rdtgrp->mon.parent->closid)) {
		rdt_last_cmd_puts("Can't move task to different control group\n");
		return -EINVAL;
	}

	if (rdtgrp->type == RDTMON_GROUP)
		resctrl_arch_set_closid_rmid(tsk, rdtgrp->mon.parent->closid,
					     rdtgrp->mon.rmid);
	else
		resctrl_arch_set_closid_rmid(tsk, rdtgrp->closid,
					     rdtgrp->mon.rmid);

	/*
	 * Ensure the task's closid and rmid are written before determining if
	 * the task is current that will decide if it will be interrupted.
	 * This pairs with the full barrier between the rq->curr update and
	 * resctrl_sched_in() during context switch.
	 */
	smp_mb();

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
	return (resctrl_arch_alloc_capable() && (r->type == RDTCTRL_GROUP) &&
		resctrl_arch_match_closid(t, r->closid));
}

static bool is_rmid_match(struct task_struct *t, struct rdtgroup *r)
{
	return (resctrl_arch_mon_capable() && (r->type == RDTMON_GROUP) &&
		resctrl_arch_match_rmid(t, r->mon.parent->closid,
					r->mon.rmid));
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
	char *pid_str;
	int ret = 0;
	pid_t pid;

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

	while (buf && buf[0] != '\0' && buf[0] != '\n') {
		pid_str = strim(strsep(&buf, ","));

		if (kstrtoint(pid_str, 0, &pid)) {
			rdt_last_cmd_printf("Task list parsing error pid %s\n", pid_str);
			ret = -EINVAL;
			break;
		}

		if (pid < 0) {
			rdt_last_cmd_printf("Invalid pid %d\n", pid);
			ret = -EINVAL;
			break;
		}

		ret = rdtgroup_move_task(pid, rdtgrp, of);
		if (ret) {
			rdt_last_cmd_printf("Error while processing task %d\n", pid);
			break;
		}
	}

unlock:
	rdtgroup_kn_unlock(of->kn);

	return ret ?: nbytes;
}

static void show_rdt_tasks(struct rdtgroup *r, struct seq_file *s)
{
	struct task_struct *p, *t;
	pid_t pid;

	rcu_read_lock();
	for_each_process_thread(p, t) {
		if (is_closid_match(t, r) || is_rmid_match(t, r)) {
			pid = task_pid_vnr(t);
			if (pid)
				seq_printf(s, "%d\n", pid);
		}
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

static int rdtgroup_closid_show(struct kernfs_open_file *of,
				struct seq_file *s, void *v)
{
	struct rdtgroup *rdtgrp;
	int ret = 0;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (rdtgrp)
		seq_printf(s, "%u\n", rdtgrp->closid);
	else
		ret = -ENOENT;
	rdtgroup_kn_unlock(of->kn);

	return ret;
}

static int rdtgroup_rmid_show(struct kernfs_open_file *of,
			      struct seq_file *s, void *v)
{
	struct rdtgroup *rdtgrp;
	int ret = 0;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (rdtgrp)
		seq_printf(s, "%u\n", rdtgrp->mon.rmid);
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
	if (!resctrl_mounted) {
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

		if (!resctrl_arch_match_closid(tsk, rdtg->closid))
			continue;

		seq_printf(s, "res:%s%s\n", (rdtg == &rdtgroup_default) ? "/" : "",
			   rdtg->kn->name);
		seq_puts(s, "mon:");
		list_for_each_entry(crg, &rdtg->mon.crdtgrp_list,
				    mon.crdtgrp_list) {
			if (!resctrl_arch_match_rmid(tsk, crg->mon.parent->closid,
						     crg->mon.rmid))
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
	struct resctrl_schema *s = of->kn->parent->priv;

	seq_printf(seq, "%u\n", s->num_closid);
	return 0;
}

static int rdt_default_ctrl_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct resctrl_schema *s = of->kn->parent->priv;
	struct rdt_resource *r = s->res;

	seq_printf(seq, "%x\n", resctrl_get_default_ctrl(r));
	return 0;
}

static int rdt_min_cbm_bits_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct resctrl_schema *s = of->kn->parent->priv;
	struct rdt_resource *r = s->res;

	seq_printf(seq, "%u\n", r->cache.min_cbm_bits);
	return 0;
}

static int rdt_shareable_bits_show(struct kernfs_open_file *of,
				   struct seq_file *seq, void *v)
{
	struct resctrl_schema *s = of->kn->parent->priv;
	struct rdt_resource *r = s->res;

	seq_printf(seq, "%x\n", r->cache.shareable_bits);
	return 0;
}

/*
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
	struct resctrl_schema *s = of->kn->parent->priv;
	/*
	 * Use unsigned long even though only 32 bits are used to ensure
	 * test_bit() is used safely.
	 */
	unsigned long sw_shareable = 0, hw_shareable = 0;
	unsigned long exclusive = 0, pseudo_locked = 0;
	struct rdt_resource *r = s->res;
	struct rdt_ctrl_domain *dom;
	int i, hwb, swb, excl, psl;
	enum rdtgrp_mode mode;
	bool sep = false;
	u32 ctrl_val;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);
	hw_shareable = r->cache.shareable_bits;
	list_for_each_entry(dom, &r->ctrl_domains, hdr.list) {
		if (sep)
			seq_putc(seq, ';');
		sw_shareable = 0;
		exclusive = 0;
		seq_printf(seq, "%d=", dom->hdr.id);
		for (i = 0; i < closids_supported(); i++) {
			if (!closid_allocated(i))
				continue;
			ctrl_val = resctrl_arch_get_config(r, dom, i,
							   s->conf_type);
			mode = rdtgroup_mode_by_closid(i);
			switch (mode) {
			case RDT_MODE_SHAREABLE:
				sw_shareable |= ctrl_val;
				break;
			case RDT_MODE_EXCLUSIVE:
				exclusive |= ctrl_val;
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
	cpus_read_unlock();
	return 0;
}

static int rdt_min_bw_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct resctrl_schema *s = of->kn->parent->priv;
	struct rdt_resource *r = s->res;

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

	list_for_each_entry(mevt, &r->evt_list, list) {
		seq_printf(seq, "%s\n", mevt->name);
		if (mevt->configurable)
			seq_printf(seq, "%s_config\n", mevt->name);
	}

	return 0;
}

static int rdt_bw_gran_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct resctrl_schema *s = of->kn->parent->priv;
	struct rdt_resource *r = s->res;

	seq_printf(seq, "%u\n", r->membw.bw_gran);
	return 0;
}

static int rdt_delay_linear_show(struct kernfs_open_file *of,
			     struct seq_file *seq, void *v)
{
	struct resctrl_schema *s = of->kn->parent->priv;
	struct rdt_resource *r = s->res;

	seq_printf(seq, "%u\n", r->membw.delay_linear);
	return 0;
}

static int max_threshold_occ_show(struct kernfs_open_file *of,
				  struct seq_file *seq, void *v)
{
	seq_printf(seq, "%u\n", resctrl_rmid_realloc_threshold);

	return 0;
}

static int rdt_thread_throttle_mode_show(struct kernfs_open_file *of,
					 struct seq_file *seq, void *v)
{
	struct resctrl_schema *s = of->kn->parent->priv;
	struct rdt_resource *r = s->res;

	switch (r->membw.throttle_mode) {
	case THREAD_THROTTLE_PER_THREAD:
		seq_puts(seq, "per-thread\n");
		return 0;
	case THREAD_THROTTLE_MAX:
		seq_puts(seq, "max\n");
		return 0;
	case THREAD_THROTTLE_UNDEFINED:
		seq_puts(seq, "undefined\n");
		return 0;
	}

	WARN_ON_ONCE(1);

	return 0;
}

static ssize_t max_threshold_occ_write(struct kernfs_open_file *of,
				       char *buf, size_t nbytes, loff_t off)
{
	unsigned int bytes;
	int ret;

	ret = kstrtouint(buf, 0, &bytes);
	if (ret)
		return ret;

	if (bytes > resctrl_rmid_realloc_limit)
		return -EINVAL;

	resctrl_rmid_realloc_threshold = resctrl_arch_round_mon_val(bytes);

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

static enum resctrl_conf_type resctrl_peer_type(enum resctrl_conf_type my_type)
{
	switch (my_type) {
	case CDP_CODE:
		return CDP_DATA;
	case CDP_DATA:
		return CDP_CODE;
	default:
	case CDP_NONE:
		return CDP_NONE;
	}
}

static int rdt_has_sparse_bitmasks_show(struct kernfs_open_file *of,
					struct seq_file *seq, void *v)
{
	struct resctrl_schema *s = of->kn->parent->priv;
	struct rdt_resource *r = s->res;

	seq_printf(seq, "%u\n", r->cache.arch_has_sparse_bitmasks);

	return 0;
}

/**
 * __rdtgroup_cbm_overlaps - Does CBM for intended closid overlap with other
 * @r: Resource to which domain instance @d belongs.
 * @d: The domain instance for which @closid is being tested.
 * @cbm: Capacity bitmask being tested.
 * @closid: Intended closid for @cbm.
 * @type: CDP type of @r.
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
static bool __rdtgroup_cbm_overlaps(struct rdt_resource *r, struct rdt_ctrl_domain *d,
				    unsigned long cbm, int closid,
				    enum resctrl_conf_type type, bool exclusive)
{
	enum rdtgrp_mode mode;
	unsigned long ctrl_b;
	int i;

	/* Check for any overlap with regions used by hardware directly */
	if (!exclusive) {
		ctrl_b = r->cache.shareable_bits;
		if (bitmap_intersects(&cbm, &ctrl_b, r->cache.cbm_len))
			return true;
	}

	/* Check for overlap with other resource groups */
	for (i = 0; i < closids_supported(); i++) {
		ctrl_b = resctrl_arch_get_config(r, d, i, type);
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
 * @s: Schema for the resource to which domain instance @d belongs.
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
bool rdtgroup_cbm_overlaps(struct resctrl_schema *s, struct rdt_ctrl_domain *d,
			   unsigned long cbm, int closid, bool exclusive)
{
	enum resctrl_conf_type peer_type = resctrl_peer_type(s->conf_type);
	struct rdt_resource *r = s->res;

	if (__rdtgroup_cbm_overlaps(r, d, cbm, closid, s->conf_type,
				    exclusive))
		return true;

	if (!resctrl_arch_get_cdp_enabled(r->rid))
		return false;
	return  __rdtgroup_cbm_overlaps(r, d, cbm, closid, peer_type, exclusive);
}

/**
 * rdtgroup_mode_test_exclusive - Test if this resource group can be exclusive
 * @rdtgrp: Resource group identified through its closid.
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
	struct rdt_ctrl_domain *d;
	struct resctrl_schema *s;
	struct rdt_resource *r;
	bool has_cache = false;
	u32 ctrl;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	list_for_each_entry(s, &resctrl_schema_all, list) {
		r = s->res;
		if (r->rid == RDT_RESOURCE_MBA || r->rid == RDT_RESOURCE_SMBA)
			continue;
		has_cache = true;
		list_for_each_entry(d, &r->ctrl_domains, hdr.list) {
			ctrl = resctrl_arch_get_config(r, d, closid,
						       s->conf_type);
			if (rdtgroup_cbm_overlaps(s, d, ctrl, closid, false)) {
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

/*
 * rdtgroup_mode_write - Modify the resource group's mode
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
	} else if (IS_ENABLED(CONFIG_RESCTRL_FS_PSEUDO_LOCK) &&
		   !strcmp(buf, "pseudo-locksetup")) {
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
				  struct rdt_ctrl_domain *d, unsigned long cbm)
{
	unsigned int size = 0;
	struct cacheinfo *ci;
	int num_b;

	if (WARN_ON_ONCE(r->ctrl_scope != RESCTRL_L2_CACHE && r->ctrl_scope != RESCTRL_L3_CACHE))
		return size;

	num_b = bitmap_weight(&cbm, r->cache.cbm_len);
	ci = get_cpu_cacheinfo_level(cpumask_any(&d->hdr.cpu_mask), r->ctrl_scope);
	if (ci)
		size = ci->size / r->cache.cbm_len * num_b;

	return size;
}

/*
 * rdtgroup_size_show - Display size in bytes of allocated regions
 *
 * The "size" file mirrors the layout of the "schemata" file, printing the
 * size in bytes of each region instead of the capacity bitmask.
 */
static int rdtgroup_size_show(struct kernfs_open_file *of,
			      struct seq_file *s, void *v)
{
	struct resctrl_schema *schema;
	enum resctrl_conf_type type;
	struct rdt_ctrl_domain *d;
	struct rdtgroup *rdtgrp;
	struct rdt_resource *r;
	unsigned int size;
	int ret = 0;
	u32 closid;
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
				   rdtgrp->plr->s->name);
			size = rdtgroup_cbm_to_size(rdtgrp->plr->s->res,
						    rdtgrp->plr->d,
						    rdtgrp->plr->cbm);
			seq_printf(s, "%d=%u\n", rdtgrp->plr->d->hdr.id, size);
		}
		goto out;
	}

	closid = rdtgrp->closid;

	list_for_each_entry(schema, &resctrl_schema_all, list) {
		r = schema->res;
		type = schema->conf_type;
		sep = false;
		seq_printf(s, "%*s:", max_name_width, schema->name);
		list_for_each_entry(d, &r->ctrl_domains, hdr.list) {
			if (sep)
				seq_putc(s, ';');
			if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
				size = 0;
			} else {
				if (is_mba_sc(r))
					ctrl = d->mbps_val[closid];
				else
					ctrl = resctrl_arch_get_config(r, d,
								       closid,
								       type);
				if (r->rid == RDT_RESOURCE_MBA ||
				    r->rid == RDT_RESOURCE_SMBA)
					size = ctrl;
				else
					size = rdtgroup_cbm_to_size(r, d, ctrl);
			}
			seq_printf(s, "%d=%u", d->hdr.id, size);
			sep = true;
		}
		seq_putc(s, '\n');
	}

out:
	rdtgroup_kn_unlock(of->kn);

	return ret;
}

#define INVALID_CONFIG_INDEX   UINT_MAX

/**
 * mon_event_config_index_get - get the hardware index for the
 *                              configurable event
 * @evtid: event id.
 *
 * Return: 0 for evtid == QOS_L3_MBM_TOTAL_EVENT_ID
 *         1 for evtid == QOS_L3_MBM_LOCAL_EVENT_ID
 *         INVALID_CONFIG_INDEX for invalid evtid
 */
static inline unsigned int mon_event_config_index_get(u32 evtid)
{
	switch (evtid) {
	case QOS_L3_MBM_TOTAL_EVENT_ID:
		return 0;
	case QOS_L3_MBM_LOCAL_EVENT_ID:
		return 1;
	default:
		/* Should never reach here */
		return INVALID_CONFIG_INDEX;
	}
}

void resctrl_arch_mon_event_config_read(void *_config_info)
{
	struct resctrl_mon_config_info *config_info = _config_info;
	unsigned int index;
	u64 msrval;

	index = mon_event_config_index_get(config_info->evtid);
	if (index == INVALID_CONFIG_INDEX) {
		pr_warn_once("Invalid event id %d\n", config_info->evtid);
		return;
	}
	rdmsrl(MSR_IA32_EVT_CFG_BASE + index, msrval);

	/* Report only the valid event configuration bits */
	config_info->mon_config = msrval & MAX_EVT_CONFIG_BITS;
}

static void mondata_config_read(struct resctrl_mon_config_info *mon_info)
{
	smp_call_function_any(&mon_info->d->hdr.cpu_mask,
			      resctrl_arch_mon_event_config_read, mon_info, 1);
}

static int mbm_config_show(struct seq_file *s, struct rdt_resource *r, u32 evtid)
{
	struct resctrl_mon_config_info mon_info;
	struct rdt_mon_domain *dom;
	bool sep = false;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	list_for_each_entry(dom, &r->mon_domains, hdr.list) {
		if (sep)
			seq_puts(s, ";");

		memset(&mon_info, 0, sizeof(struct resctrl_mon_config_info));
		mon_info.r = r;
		mon_info.d = dom;
		mon_info.evtid = evtid;
		mondata_config_read(&mon_info);

		seq_printf(s, "%d=0x%02x", dom->hdr.id, mon_info.mon_config);
		sep = true;
	}
	seq_puts(s, "\n");

	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();

	return 0;
}

static int mbm_total_bytes_config_show(struct kernfs_open_file *of,
				       struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	mbm_config_show(seq, r, QOS_L3_MBM_TOTAL_EVENT_ID);

	return 0;
}

static int mbm_local_bytes_config_show(struct kernfs_open_file *of,
				       struct seq_file *seq, void *v)
{
	struct rdt_resource *r = of->kn->parent->priv;

	mbm_config_show(seq, r, QOS_L3_MBM_LOCAL_EVENT_ID);

	return 0;
}

void resctrl_arch_mon_event_config_write(void *_config_info)
{
	struct resctrl_mon_config_info *config_info = _config_info;
	unsigned int index;

	index = mon_event_config_index_get(config_info->evtid);
	if (index == INVALID_CONFIG_INDEX) {
		pr_warn_once("Invalid event id %d\n", config_info->evtid);
		return;
	}
	wrmsr(MSR_IA32_EVT_CFG_BASE + index, config_info->mon_config, 0);
}

static void mbm_config_write_domain(struct rdt_resource *r,
				    struct rdt_mon_domain *d, u32 evtid, u32 val)
{
	struct resctrl_mon_config_info mon_info = {0};

	/*
	 * Read the current config value first. If both are the same then
	 * no need to write it again.
	 */
	mon_info.r = r;
	mon_info.d = d;
	mon_info.evtid = evtid;
	mondata_config_read(&mon_info);
	if (mon_info.mon_config == val)
		return;

	mon_info.mon_config = val;

	/*
	 * Update MSR_IA32_EVT_CFG_BASE MSR on one of the CPUs in the
	 * domain. The MSRs offset from MSR MSR_IA32_EVT_CFG_BASE
	 * are scoped at the domain level. Writing any of these MSRs
	 * on one CPU is observed by all the CPUs in the domain.
	 */
	smp_call_function_any(&d->hdr.cpu_mask, resctrl_arch_mon_event_config_write,
			      &mon_info, 1);

	/*
	 * When an Event Configuration is changed, the bandwidth counters
	 * for all RMIDs and Events will be cleared by the hardware. The
	 * hardware also sets MSR_IA32_QM_CTR.Unavailable (bit 62) for
	 * every RMID on the next read to any event for every RMID.
	 * Subsequent reads will have MSR_IA32_QM_CTR.Unavailable (bit 62)
	 * cleared while it is tracked by the hardware. Clear the
	 * mbm_local and mbm_total counts for all the RMIDs.
	 */
	resctrl_arch_reset_rmid_all(r, d);
}

static int mon_config_write(struct rdt_resource *r, char *tok, u32 evtid)
{
	char *dom_str = NULL, *id_str;
	unsigned long dom_id, val;
	struct rdt_mon_domain *d;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

next:
	if (!tok || tok[0] == '\0')
		return 0;

	/* Start processing the strings for each domain */
	dom_str = strim(strsep(&tok, ";"));
	id_str = strsep(&dom_str, "=");

	if (!id_str || kstrtoul(id_str, 10, &dom_id)) {
		rdt_last_cmd_puts("Missing '=' or non-numeric domain id\n");
		return -EINVAL;
	}

	if (!dom_str || kstrtoul(dom_str, 16, &val)) {
		rdt_last_cmd_puts("Non-numeric event configuration value\n");
		return -EINVAL;
	}

	/* Value from user cannot be more than the supported set of events */
	if ((val & r->mbm_cfg_mask) != val) {
		rdt_last_cmd_printf("Invalid event configuration: max valid mask is 0x%02x\n",
				    r->mbm_cfg_mask);
		return -EINVAL;
	}

	list_for_each_entry(d, &r->mon_domains, hdr.list) {
		if (d->hdr.id == dom_id) {
			mbm_config_write_domain(r, d, evtid, val);
			goto next;
		}
	}

	return -EINVAL;
}

static ssize_t mbm_total_bytes_config_write(struct kernfs_open_file *of,
					    char *buf, size_t nbytes,
					    loff_t off)
{
	struct rdt_resource *r = of->kn->parent->priv;
	int ret;

	/* Valid input requires a trailing newline */
	if (nbytes == 0 || buf[nbytes - 1] != '\n')
		return -EINVAL;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	rdt_last_cmd_clear();

	buf[nbytes - 1] = '\0';

	ret = mon_config_write(r, buf, QOS_L3_MBM_TOTAL_EVENT_ID);

	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();

	return ret ?: nbytes;
}

static ssize_t mbm_local_bytes_config_write(struct kernfs_open_file *of,
					    char *buf, size_t nbytes,
					    loff_t off)
{
	struct rdt_resource *r = of->kn->parent->priv;
	int ret;

	/* Valid input requires a trailing newline */
	if (nbytes == 0 || buf[nbytes - 1] != '\n')
		return -EINVAL;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	rdt_last_cmd_clear();

	buf[nbytes - 1] = '\0';

	ret = mon_config_write(r, buf, QOS_L3_MBM_LOCAL_EVENT_ID);

	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();

	return ret ?: nbytes;
}

/* rdtgroup information files for one cache resource. */
static struct rftype res_common_files[] = {
	{
		.name		= "last_cmd_status",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_last_cmd_status_show,
		.fflags		= RFTYPE_TOP_INFO,
	},
	{
		.name		= "num_closids",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_num_closids_show,
		.fflags		= RFTYPE_CTRL_INFO,
	},
	{
		.name		= "mon_features",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_mon_features_show,
		.fflags		= RFTYPE_MON_INFO,
	},
	{
		.name		= "num_rmids",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_num_rmids_show,
		.fflags		= RFTYPE_MON_INFO,
	},
	{
		.name		= "cbm_mask",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_default_ctrl_show,
		.fflags		= RFTYPE_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "min_cbm_bits",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_min_cbm_bits_show,
		.fflags		= RFTYPE_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "shareable_bits",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_shareable_bits_show,
		.fflags		= RFTYPE_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "bit_usage",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_bit_usage_show,
		.fflags		= RFTYPE_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "min_bandwidth",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_min_bw_show,
		.fflags		= RFTYPE_CTRL_INFO | RFTYPE_RES_MB,
	},
	{
		.name		= "bandwidth_gran",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_bw_gran_show,
		.fflags		= RFTYPE_CTRL_INFO | RFTYPE_RES_MB,
	},
	{
		.name		= "delay_linear",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_delay_linear_show,
		.fflags		= RFTYPE_CTRL_INFO | RFTYPE_RES_MB,
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
		.fflags		= RFTYPE_MON_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "mbm_total_bytes_config",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= mbm_total_bytes_config_show,
		.write		= mbm_total_bytes_config_write,
	},
	{
		.name		= "mbm_local_bytes_config",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= mbm_local_bytes_config_show,
		.write		= mbm_local_bytes_config_write,
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
		.name		= "mon_hw_id",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdtgroup_rmid_show,
		.fflags		= RFTYPE_MON_BASE | RFTYPE_DEBUG,
	},
	{
		.name		= "schemata",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= rdtgroup_schemata_write,
		.seq_show	= rdtgroup_schemata_show,
		.fflags		= RFTYPE_CTRL_BASE,
	},
	{
		.name		= "mba_MBps_event",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= rdtgroup_mba_mbps_event_write,
		.seq_show	= rdtgroup_mba_mbps_event_show,
	},
	{
		.name		= "mode",
		.mode		= 0644,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.write		= rdtgroup_mode_write,
		.seq_show	= rdtgroup_mode_show,
		.fflags		= RFTYPE_CTRL_BASE,
	},
	{
		.name		= "size",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdtgroup_size_show,
		.fflags		= RFTYPE_CTRL_BASE,
	},
	{
		.name		= "sparse_masks",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdt_has_sparse_bitmasks_show,
		.fflags		= RFTYPE_CTRL_INFO | RFTYPE_RES_CACHE,
	},
	{
		.name		= "ctrl_hw_id",
		.mode		= 0444,
		.kf_ops		= &rdtgroup_kf_single_ops,
		.seq_show	= rdtgroup_closid_show,
		.fflags		= RFTYPE_CTRL_BASE | RFTYPE_DEBUG,
	},

};

static int rdtgroup_add_files(struct kernfs_node *kn, unsigned long fflags)
{
	struct rftype *rfts, *rft;
	int ret, len;

	rfts = res_common_files;
	len = ARRAY_SIZE(res_common_files);

	lockdep_assert_held(&rdtgroup_mutex);

	if (resctrl_debug)
		fflags |= RFTYPE_DEBUG;

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

static void thread_throttle_mode_init(void)
{
	enum membw_throttle_mode throttle_mode = THREAD_THROTTLE_UNDEFINED;
	struct rdt_resource *r_mba, *r_smba;

	r_mba = resctrl_arch_get_resource(RDT_RESOURCE_MBA);
	if (r_mba->alloc_capable &&
	    r_mba->membw.throttle_mode != THREAD_THROTTLE_UNDEFINED)
		throttle_mode = r_mba->membw.throttle_mode;

	r_smba = resctrl_arch_get_resource(RDT_RESOURCE_SMBA);
	if (r_smba->alloc_capable &&
	    r_smba->membw.throttle_mode != THREAD_THROTTLE_UNDEFINED)
		throttle_mode = r_smba->membw.throttle_mode;

	if (throttle_mode == THREAD_THROTTLE_UNDEFINED)
		return;

	resctrl_file_fflags_init("thread_throttle_mode",
				 RFTYPE_CTRL_INFO | RFTYPE_RES_MB);
}

void resctrl_file_fflags_init(const char *config, unsigned long fflags)
{
	struct rftype *rft;

	rft = rdtgroup_get_rftype_by_name(config);
	if (rft)
		rft->fflags = fflags;
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

static int rdtgroup_mkdir_info_resdir(void *priv, char *name,
				      unsigned long fflags)
{
	struct kernfs_node *kn_subdir;
	int ret;

	kn_subdir = kernfs_create_dir(kn_info, name,
				      kn_info->mode, priv);
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

static unsigned long fflags_from_resource(struct rdt_resource *r)
{
	switch (r->rid) {
	case RDT_RESOURCE_L3:
	case RDT_RESOURCE_L2:
		return RFTYPE_RES_CACHE;
	case RDT_RESOURCE_MBA:
	case RDT_RESOURCE_SMBA:
		return RFTYPE_RES_MB;
	}

	return WARN_ON_ONCE(1);
}

static int rdtgroup_create_info_dir(struct kernfs_node *parent_kn)
{
	struct resctrl_schema *s;
	struct rdt_resource *r;
	unsigned long fflags;
	char name[32];
	int ret;

	/* create the directory */
	kn_info = kernfs_create_dir(parent_kn, "info", parent_kn->mode, NULL);
	if (IS_ERR(kn_info))
		return PTR_ERR(kn_info);

	ret = rdtgroup_add_files(kn_info, RFTYPE_TOP_INFO);
	if (ret)
		goto out_destroy;

	/* loop over enabled controls, these are all alloc_capable */
	list_for_each_entry(s, &resctrl_schema_all, list) {
		r = s->res;
		fflags = fflags_from_resource(r) | RFTYPE_CTRL_INFO;
		ret = rdtgroup_mkdir_info_resdir(s, s->name, fflags);
		if (ret)
			goto out_destroy;
	}

	for_each_mon_capable_rdt_resource(r) {
		fflags = fflags_from_resource(r) | RFTYPE_MON_INFO;
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
	return resctrl_arch_get_resource(RDT_RESOURCE_MBA)->membw.delay_linear;
}

static int set_cache_qos_cfg(int level, bool enable)
{
	void (*update)(void *arg);
	struct rdt_ctrl_domain *d;
	struct rdt_resource *r_l;
	cpumask_var_t cpu_mask;
	int cpu;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	if (level == RDT_RESOURCE_L3)
		update = l3_qos_cfg_update;
	else if (level == RDT_RESOURCE_L2)
		update = l2_qos_cfg_update;
	else
		return -EINVAL;

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	r_l = &rdt_resources_all[level].r_resctrl;
	list_for_each_entry(d, &r_l->ctrl_domains, hdr.list) {
		if (r_l->cache.arch_has_per_cpu_cfg)
			/* Pick all the CPUs in the domain instance */
			for_each_cpu(cpu, &d->hdr.cpu_mask)
				cpumask_set_cpu(cpu, cpu_mask);
		else
			/* Pick one CPU from each domain instance to update MSR */
			cpumask_set_cpu(cpumask_any(&d->hdr.cpu_mask), cpu_mask);
	}

	/* Update QOS_CFG MSR on all the CPUs in cpu_mask */
	on_each_cpu_mask(cpu_mask, update, &enable, 1);

	free_cpumask_var(cpu_mask);

	return 0;
}

/* Restore the qos cfg state when a domain comes online */
void rdt_domain_reconfigure_cdp(struct rdt_resource *r)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	if (!r->cdp_capable)
		return;

	if (r->rid == RDT_RESOURCE_L2)
		l2_qos_cfg_update(&hw_res->cdp_enabled);

	if (r->rid == RDT_RESOURCE_L3)
		l3_qos_cfg_update(&hw_res->cdp_enabled);
}

static int mba_sc_domain_allocate(struct rdt_resource *r, struct rdt_ctrl_domain *d)
{
	u32 num_closid = resctrl_arch_get_num_closid(r);
	int cpu = cpumask_any(&d->hdr.cpu_mask);
	int i;

	d->mbps_val = kcalloc_node(num_closid, sizeof(*d->mbps_val),
				   GFP_KERNEL, cpu_to_node(cpu));
	if (!d->mbps_val)
		return -ENOMEM;

	for (i = 0; i < num_closid; i++)
		d->mbps_val[i] = MBA_MAX_MBPS;

	return 0;
}

static void mba_sc_domain_destroy(struct rdt_resource *r,
				  struct rdt_ctrl_domain *d)
{
	kfree(d->mbps_val);
	d->mbps_val = NULL;
}

/*
 * MBA software controller is supported only if
 * MBM is supported and MBA is in linear scale,
 * and the MBM monitor scope is the same as MBA
 * control scope.
 */
static bool supports_mba_mbps(void)
{
	struct rdt_resource *rmbm = resctrl_arch_get_resource(RDT_RESOURCE_L3);
	struct rdt_resource *r = resctrl_arch_get_resource(RDT_RESOURCE_MBA);

	return (resctrl_is_mbm_enabled() &&
		r->alloc_capable && is_mba_linear() &&
		r->ctrl_scope == rmbm->mon_scope);
}

/*
 * Enable or disable the MBA software controller
 * which helps user specify bandwidth in MBps.
 */
static int set_mba_sc(bool mba_sc)
{
	struct rdt_resource *r = resctrl_arch_get_resource(RDT_RESOURCE_MBA);
	u32 num_closid = resctrl_arch_get_num_closid(r);
	struct rdt_ctrl_domain *d;
	unsigned long fflags;
	int i;

	if (!supports_mba_mbps() || mba_sc == is_mba_sc(r))
		return -EINVAL;

	r->membw.mba_sc = mba_sc;

	rdtgroup_default.mba_mbps_event = mba_mbps_default_event;

	list_for_each_entry(d, &r->ctrl_domains, hdr.list) {
		for (i = 0; i < num_closid; i++)
			d->mbps_val[i] = MBA_MAX_MBPS;
	}

	fflags = mba_sc ? RFTYPE_CTRL_BASE | RFTYPE_MON_BASE : 0;
	resctrl_file_fflags_init("mba_MBps_event", fflags);

	return 0;
}

static int cdp_enable(int level)
{
	struct rdt_resource *r_l = &rdt_resources_all[level].r_resctrl;
	int ret;

	if (!r_l->alloc_capable)
		return -EINVAL;

	ret = set_cache_qos_cfg(level, true);
	if (!ret)
		rdt_resources_all[level].cdp_enabled = true;

	return ret;
}

static void cdp_disable(int level)
{
	struct rdt_hw_resource *r_hw = &rdt_resources_all[level];

	if (r_hw->cdp_enabled) {
		set_cache_qos_cfg(level, false);
		r_hw->cdp_enabled = false;
	}
}

int resctrl_arch_set_cdp_enabled(enum resctrl_res_level l, bool enable)
{
	struct rdt_hw_resource *hw_res = &rdt_resources_all[l];

	if (!hw_res->r_resctrl.cdp_capable)
		return -EINVAL;

	if (enable)
		return cdp_enable(l);

	cdp_disable(l);

	return 0;
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

static void rdtgroup_kn_get(struct rdtgroup *rdtgrp, struct kernfs_node *kn)
{
	atomic_inc(&rdtgrp->waitcount);
	kernfs_break_active_protection(kn);
}

static void rdtgroup_kn_put(struct rdtgroup *rdtgrp, struct kernfs_node *kn)
{
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

struct rdtgroup *rdtgroup_kn_lock_live(struct kernfs_node *kn)
{
	struct rdtgroup *rdtgrp = kernfs_to_rdtgroup(kn);

	if (!rdtgrp)
		return NULL;

	rdtgroup_kn_get(rdtgrp, kn);

	cpus_read_lock();
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
	cpus_read_unlock();

	rdtgroup_kn_put(rdtgrp, kn);
}

static int mkdir_mondata_all(struct kernfs_node *parent_kn,
			     struct rdtgroup *prgrp,
			     struct kernfs_node **mon_data_kn);

static void rdt_disable_ctx(void)
{
	resctrl_arch_set_cdp_enabled(RDT_RESOURCE_L3, false);
	resctrl_arch_set_cdp_enabled(RDT_RESOURCE_L2, false);
	set_mba_sc(false);

	resctrl_debug = false;
}

static int rdt_enable_ctx(struct rdt_fs_context *ctx)
{
	int ret = 0;

	if (ctx->enable_cdpl2) {
		ret = resctrl_arch_set_cdp_enabled(RDT_RESOURCE_L2, true);
		if (ret)
			goto out_done;
	}

	if (ctx->enable_cdpl3) {
		ret = resctrl_arch_set_cdp_enabled(RDT_RESOURCE_L3, true);
		if (ret)
			goto out_cdpl2;
	}

	if (ctx->enable_mba_mbps) {
		ret = set_mba_sc(true);
		if (ret)
			goto out_cdpl3;
	}

	if (ctx->enable_debug)
		resctrl_debug = true;

	return 0;

out_cdpl3:
	resctrl_arch_set_cdp_enabled(RDT_RESOURCE_L3, false);
out_cdpl2:
	resctrl_arch_set_cdp_enabled(RDT_RESOURCE_L2, false);
out_done:
	return ret;
}

static int schemata_list_add(struct rdt_resource *r, enum resctrl_conf_type type)
{
	struct resctrl_schema *s;
	const char *suffix = "";
	int ret, cl;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->res = r;
	s->num_closid = resctrl_arch_get_num_closid(r);
	if (resctrl_arch_get_cdp_enabled(r->rid))
		s->num_closid /= 2;

	s->conf_type = type;
	switch (type) {
	case CDP_CODE:
		suffix = "CODE";
		break;
	case CDP_DATA:
		suffix = "DATA";
		break;
	case CDP_NONE:
		suffix = "";
		break;
	}

	ret = snprintf(s->name, sizeof(s->name), "%s%s", r->name, suffix);
	if (ret >= sizeof(s->name)) {
		kfree(s);
		return -EINVAL;
	}

	cl = strlen(s->name);

	/*
	 * If CDP is supported by this resource, but not enabled,
	 * include the suffix. This ensures the tabular format of the
	 * schemata file does not change between mounts of the filesystem.
	 */
	if (r->cdp_capable && !resctrl_arch_get_cdp_enabled(r->rid))
		cl += 4;

	if (cl > max_name_width)
		max_name_width = cl;

	switch (r->schema_fmt) {
	case RESCTRL_SCHEMA_BITMAP:
		s->fmt_str = "%d=%x";
		break;
	case RESCTRL_SCHEMA_RANGE:
		s->fmt_str = "%d=%u";
		break;
	}

	if (WARN_ON_ONCE(!s->fmt_str)) {
		kfree(s);
		return -EINVAL;
	}

	INIT_LIST_HEAD(&s->list);
	list_add(&s->list, &resctrl_schema_all);

	return 0;
}

static int schemata_list_create(void)
{
	struct rdt_resource *r;
	int ret = 0;

	for_each_alloc_capable_rdt_resource(r) {
		if (resctrl_arch_get_cdp_enabled(r->rid)) {
			ret = schemata_list_add(r, CDP_CODE);
			if (ret)
				break;

			ret = schemata_list_add(r, CDP_DATA);
		} else {
			ret = schemata_list_add(r, CDP_NONE);
		}

		if (ret)
			break;
	}

	return ret;
}

static void schemata_list_destroy(void)
{
	struct resctrl_schema *s, *tmp;

	list_for_each_entry_safe(s, tmp, &resctrl_schema_all, list) {
		list_del(&s->list);
		kfree(s);
	}
}

static int rdt_get_tree(struct fs_context *fc)
{
	struct rdt_fs_context *ctx = rdt_fc2context(fc);
	unsigned long flags = RFTYPE_CTRL_BASE;
	struct rdt_mon_domain *dom;
	struct rdt_resource *r;
	int ret;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);
	/*
	 * resctrl file system can only be mounted once.
	 */
	if (resctrl_mounted) {
		ret = -EBUSY;
		goto out;
	}

	ret = rdtgroup_setup_root(ctx);
	if (ret)
		goto out;

	ret = rdt_enable_ctx(ctx);
	if (ret)
		goto out_root;

	ret = schemata_list_create();
	if (ret) {
		schemata_list_destroy();
		goto out_ctx;
	}

	closid_init();

	if (resctrl_arch_mon_capable())
		flags |= RFTYPE_MON;

	ret = rdtgroup_add_files(rdtgroup_default.kn, flags);
	if (ret)
		goto out_schemata_free;

	kernfs_activate(rdtgroup_default.kn);

	ret = rdtgroup_create_info_dir(rdtgroup_default.kn);
	if (ret < 0)
		goto out_schemata_free;

	if (resctrl_arch_mon_capable()) {
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

	if (resctrl_arch_alloc_capable())
		resctrl_arch_enable_alloc();
	if (resctrl_arch_mon_capable())
		resctrl_arch_enable_mon();

	if (resctrl_arch_alloc_capable() || resctrl_arch_mon_capable())
		resctrl_mounted = true;

	if (resctrl_is_mbm_enabled()) {
		r = resctrl_arch_get_resource(RDT_RESOURCE_L3);
		list_for_each_entry(dom, &r->mon_domains, hdr.list)
			mbm_setup_overflow_handler(dom, MBM_OVERFLOW_INTERVAL,
						   RESCTRL_PICK_ANY_CPU);
	}

	goto out;

out_psl:
	rdt_pseudo_lock_release();
out_mondata:
	if (resctrl_arch_mon_capable())
		kernfs_remove(kn_mondata);
out_mongrp:
	if (resctrl_arch_mon_capable())
		kernfs_remove(kn_mongrp);
out_info:
	kernfs_remove(kn_info);
out_schemata_free:
	schemata_list_destroy();
out_ctx:
	rdt_disable_ctx();
out_root:
	rdtgroup_destroy_root();
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
	Opt_debug,
	nr__rdt_params
};

static const struct fs_parameter_spec rdt_fs_parameters[] = {
	fsparam_flag("cdp",		Opt_cdp),
	fsparam_flag("cdpl2",		Opt_cdpl2),
	fsparam_flag("mba_MBps",	Opt_mba_mbps),
	fsparam_flag("debug",		Opt_debug),
	{}
};

static int rdt_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct rdt_fs_context *ctx = rdt_fc2context(fc);
	struct fs_parse_result result;
	const char *msg;
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
		msg = "mba_MBps requires MBM and linear scale MBA at L3 scope";
		if (!supports_mba_mbps())
			return invalfc(fc, msg);
		ctx->enable_mba_mbps = true;
		return 0;
	case Opt_debug:
		ctx->enable_debug = true;
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

	ctx->kfc.magic = RDTGROUP_SUPER_MAGIC;
	fc->fs_private = &ctx->kfc;
	fc->ops = &rdt_fs_context_ops;
	put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(&init_user_ns);
	fc->global = true;
	return 0;
}

void resctrl_arch_reset_all_ctrls(struct rdt_resource *r)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	struct rdt_hw_ctrl_domain *hw_dom;
	struct msr_param msr_param;
	struct rdt_ctrl_domain *d;
	int i;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	msr_param.res = r;
	msr_param.low = 0;
	msr_param.high = hw_res->num_closid;

	/*
	 * Disable resource control for this resource by setting all
	 * CBMs in all ctrl_domains to the maximum mask value. Pick one CPU
	 * from each domain to update the MSRs below.
	 */
	list_for_each_entry(d, &r->ctrl_domains, hdr.list) {
		hw_dom = resctrl_to_arch_ctrl_dom(d);

		for (i = 0; i < hw_res->num_closid; i++)
			hw_dom->ctrl_val[i] = resctrl_get_default_ctrl(r);
		msr_param.dom = d;
		smp_call_function_any(&d->hdr.cpu_mask, rdt_ctrl_update, &msr_param, 1);
	}

	return;
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
			resctrl_arch_set_closid_rmid(t, to->closid,
						     to->mon.rmid);

			/*
			 * Order the closid/rmid stores above before the loads
			 * in task_curr(). This pairs with the full barrier
			 * between the rq->curr update and resctrl_sched_in()
			 * during context switch.
			 */
			smp_mb();

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
		free_rmid(sentry->closid, sentry->mon.rmid);
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

		free_rmid(rdtgrp->closid, rdtgrp->mon.rmid);

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

	rdt_disable_ctx();

	/* Put everything back to default values. */
	for_each_alloc_capable_rdt_resource(r)
		resctrl_arch_reset_all_ctrls(r);

	rmdir_all_sub();
	rdt_pseudo_lock_release();
	rdtgroup_default.mode = RDT_MODE_SHAREABLE;
	schemata_list_destroy();
	rdtgroup_destroy_root();
	if (resctrl_arch_alloc_capable())
		resctrl_arch_disable_alloc();
	if (resctrl_arch_mon_capable())
		resctrl_arch_disable_mon();
	resctrl_mounted = false;
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

static void mon_rmdir_one_subdir(struct kernfs_node *pkn, char *name, char *subname)
{
	struct kernfs_node *kn;

	kn = kernfs_find_and_get(pkn, name);
	if (!kn)
		return;
	kernfs_put(kn);

	if (kn->dir.subdirs <= 1)
		kernfs_remove(kn);
	else
		kernfs_remove_by_name(kn, subname);
}

/*
 * Remove all subdirectories of mon_data of ctrl_mon groups
 * and monitor groups for the given domain.
 * Remove files and directories containing "sum" of domain data
 * when last domain being summed is removed.
 */
static void rmdir_mondata_subdir_allrdtgrp(struct rdt_resource *r,
					   struct rdt_mon_domain *d)
{
	struct rdtgroup *prgrp, *crgrp;
	char subname[32];
	bool snc_mode;
	char name[32];

	snc_mode = r->mon_scope == RESCTRL_L3_NODE;
	sprintf(name, "mon_%s_%02d", r->name, snc_mode ? d->ci->id : d->hdr.id);
	if (snc_mode)
		sprintf(subname, "mon_sub_%s_%02d", r->name, d->hdr.id);

	list_for_each_entry(prgrp, &rdt_all_groups, rdtgroup_list) {
		mon_rmdir_one_subdir(prgrp->mon.mon_data_kn, name, subname);

		list_for_each_entry(crgrp, &prgrp->mon.crdtgrp_list, mon.crdtgrp_list)
			mon_rmdir_one_subdir(crgrp->mon.mon_data_kn, name, subname);
	}
}

static int mon_add_all_files(struct kernfs_node *kn, struct rdt_mon_domain *d,
			     struct rdt_resource *r, struct rdtgroup *prgrp,
			     bool do_sum)
{
	struct rmid_read rr = {0};
	union mon_data_bits priv;
	struct mon_evt *mevt;
	int ret;

	if (WARN_ON(list_empty(&r->evt_list)))
		return -EPERM;

	priv.u.rid = r->rid;
	priv.u.domid = do_sum ? d->ci->id : d->hdr.id;
	priv.u.sum = do_sum;
	list_for_each_entry(mevt, &r->evt_list, list) {
		priv.u.evtid = mevt->evtid;
		ret = mon_addfile(kn, mevt->name, priv.priv);
		if (ret)
			return ret;

		if (!do_sum && resctrl_is_mbm_event(mevt->evtid))
			mon_event_read(&rr, r, d, prgrp, &d->hdr.cpu_mask, mevt->evtid, true);
	}

	return 0;
}

static int mkdir_mondata_subdir(struct kernfs_node *parent_kn,
				struct rdt_mon_domain *d,
				struct rdt_resource *r, struct rdtgroup *prgrp)
{
	struct kernfs_node *kn, *ckn;
	char name[32];
	bool snc_mode;
	int ret = 0;

	lockdep_assert_held(&rdtgroup_mutex);

	snc_mode = r->mon_scope == RESCTRL_L3_NODE;
	sprintf(name, "mon_%s_%02d", r->name, snc_mode ? d->ci->id : d->hdr.id);
	kn = kernfs_find_and_get(parent_kn, name);
	if (kn) {
		/*
		 * rdtgroup_mutex will prevent this directory from being
		 * removed. No need to keep this hold.
		 */
		kernfs_put(kn);
	} else {
		kn = kernfs_create_dir(parent_kn, name, parent_kn->mode, prgrp);
		if (IS_ERR(kn))
			return PTR_ERR(kn);

		ret = rdtgroup_kn_set_ugid(kn);
		if (ret)
			goto out_destroy;
		ret = mon_add_all_files(kn, d, r, prgrp, snc_mode);
		if (ret)
			goto out_destroy;
	}

	if (snc_mode) {
		sprintf(name, "mon_sub_%s_%02d", r->name, d->hdr.id);
		ckn = kernfs_create_dir(kn, name, parent_kn->mode, prgrp);
		if (IS_ERR(ckn)) {
			ret = -EINVAL;
			goto out_destroy;
		}

		ret = rdtgroup_kn_set_ugid(ckn);
		if (ret)
			goto out_destroy;

		ret = mon_add_all_files(ckn, d, r, prgrp, false);
		if (ret)
			goto out_destroy;
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
static void mkdir_mondata_subdir_allrdtgrp(struct rdt_resource *r,
					   struct rdt_mon_domain *d)
{
	struct kernfs_node *parent_kn;
	struct rdtgroup *prgrp, *crgrp;
	struct list_head *head;

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
	struct rdt_mon_domain *dom;
	int ret;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	list_for_each_entry(dom, &r->mon_domains, hdr.list) {
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
	for_each_mon_capable_rdt_resource(r) {
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
static int __init_one_rdt_domain(struct rdt_ctrl_domain *d, struct resctrl_schema *s,
				 u32 closid)
{
	enum resctrl_conf_type peer_type = resctrl_peer_type(s->conf_type);
	enum resctrl_conf_type t = s->conf_type;
	struct resctrl_staged_config *cfg;
	struct rdt_resource *r = s->res;
	u32 used_b = 0, unused_b = 0;
	unsigned long tmp_cbm;
	enum rdtgrp_mode mode;
	u32 peer_ctl, ctrl_val;
	int i;

	cfg = &d->staged_config[t];
	cfg->have_new_ctrl = false;
	cfg->new_ctrl = r->cache.shareable_bits;
	used_b = r->cache.shareable_bits;
	for (i = 0; i < closids_supported(); i++) {
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
			if (resctrl_arch_get_cdp_enabled(r->rid))
				peer_ctl = resctrl_arch_get_config(r, d, i,
								   peer_type);
			else
				peer_ctl = 0;
			ctrl_val = resctrl_arch_get_config(r, d, i,
							   s->conf_type);
			used_b |= ctrl_val | peer_ctl;
			if (mode == RDT_MODE_SHAREABLE)
				cfg->new_ctrl |= ctrl_val | peer_ctl;
		}
	}
	if (d->plr && d->plr->cbm > 0)
		used_b |= d->plr->cbm;
	unused_b = used_b ^ (BIT_MASK(r->cache.cbm_len) - 1);
	unused_b &= BIT_MASK(r->cache.cbm_len) - 1;
	cfg->new_ctrl |= unused_b;
	/*
	 * Force the initial CBM to be valid, user can
	 * modify the CBM based on system availability.
	 */
	cfg->new_ctrl = cbm_ensure_valid(cfg->new_ctrl, r);
	/*
	 * Assign the u32 CBM to an unsigned long to ensure that
	 * bitmap_weight() does not access out-of-bound memory.
	 */
	tmp_cbm = cfg->new_ctrl;
	if (bitmap_weight(&tmp_cbm, r->cache.cbm_len) < r->cache.min_cbm_bits) {
		rdt_last_cmd_printf("No space on %s:%d\n", s->name, d->hdr.id);
		return -ENOSPC;
	}
	cfg->have_new_ctrl = true;

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
static int rdtgroup_init_cat(struct resctrl_schema *s, u32 closid)
{
	struct rdt_ctrl_domain *d;
	int ret;

	list_for_each_entry(d, &s->res->ctrl_domains, hdr.list) {
		ret = __init_one_rdt_domain(d, s, closid);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* Initialize MBA resource with default values. */
static void rdtgroup_init_mba(struct rdt_resource *r, u32 closid)
{
	struct resctrl_staged_config *cfg;
	struct rdt_ctrl_domain *d;

	list_for_each_entry(d, &r->ctrl_domains, hdr.list) {
		if (is_mba_sc(r)) {
			d->mbps_val[closid] = MBA_MAX_MBPS;
			continue;
		}

		cfg = &d->staged_config[CDP_NONE];
		cfg->new_ctrl = resctrl_get_default_ctrl(r);
		cfg->have_new_ctrl = true;
	}
}

/* Initialize the RDT group's allocations. */
static int rdtgroup_init_alloc(struct rdtgroup *rdtgrp)
{
	struct resctrl_schema *s;
	struct rdt_resource *r;
	int ret = 0;

	rdt_staged_configs_clear();

	list_for_each_entry(s, &resctrl_schema_all, list) {
		r = s->res;
		if (r->rid == RDT_RESOURCE_MBA ||
		    r->rid == RDT_RESOURCE_SMBA) {
			rdtgroup_init_mba(r, rdtgrp->closid);
			if (is_mba_sc(r))
				continue;
		} else {
			ret = rdtgroup_init_cat(s, rdtgrp->closid);
			if (ret < 0)
				goto out;
		}

		ret = resctrl_arch_update_domains(r, rdtgrp->closid);
		if (ret < 0) {
			rdt_last_cmd_puts("Failed to initialize allocations\n");
			goto out;
		}

	}

	rdtgrp->mode = RDT_MODE_SHAREABLE;

out:
	rdt_staged_configs_clear();
	return ret;
}

static int mkdir_rdt_prepare_rmid_alloc(struct rdtgroup *rdtgrp)
{
	int ret;

	if (!resctrl_arch_mon_capable())
		return 0;

	ret = alloc_rmid(rdtgrp->closid);
	if (ret < 0) {
		rdt_last_cmd_puts("Out of RMIDs\n");
		return ret;
	}
	rdtgrp->mon.rmid = ret;

	ret = mkdir_mondata_all(rdtgrp->kn, rdtgrp, &rdtgrp->mon.mon_data_kn);
	if (ret) {
		rdt_last_cmd_puts("kernfs subdir error\n");
		free_rmid(rdtgrp->closid, rdtgrp->mon.rmid);
		return ret;
	}

	return 0;
}

static void mkdir_rdt_prepare_rmid_free(struct rdtgroup *rgrp)
{
	if (resctrl_arch_mon_capable())
		free_rmid(rgrp->closid, rgrp->mon.rmid);
}

static int mkdir_rdt_prepare(struct kernfs_node *parent_kn,
			     const char *name, umode_t mode,
			     enum rdt_group_type rtype, struct rdtgroup **r)
{
	struct rdtgroup *prdtgrp, *rdtgrp;
	unsigned long files = 0;
	struct kernfs_node *kn;
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

	if (rtype == RDTCTRL_GROUP) {
		files = RFTYPE_BASE | RFTYPE_CTRL;
		if (resctrl_arch_mon_capable())
			files |= RFTYPE_MON;
	} else {
		files = RFTYPE_BASE | RFTYPE_MON;
	}

	ret = rdtgroup_add_files(kn, files);
	if (ret) {
		rdt_last_cmd_puts("kernfs fill error\n");
		goto out_destroy;
	}

	/*
	 * The caller unlocks the parent_kn upon success.
	 */
	return 0;

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

	ret = mkdir_rdt_prepare_rmid_alloc(rdtgrp);
	if (ret) {
		mkdir_rdt_prepare_clean(rdtgrp);
		goto out_unlock;
	}

	kernfs_activate(rdtgrp->kn);

	/*
	 * Add the rdtgrp to the list of rdtgrps the parent
	 * ctrl_mon group has to track.
	 */
	list_add_tail(&rdtgrp->mon.crdtgrp_list, &prgrp->mon.crdtgrp_list);

out_unlock:
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

	ret = mkdir_rdt_prepare_rmid_alloc(rdtgrp);
	if (ret)
		goto out_closid_free;

	kernfs_activate(rdtgrp->kn);

	ret = rdtgroup_init_alloc(rdtgrp);
	if (ret < 0)
		goto out_rmid_free;

	list_add(&rdtgrp->rdtgroup_list, &rdt_all_groups);

	if (resctrl_arch_mon_capable()) {
		/*
		 * Create an empty mon_groups directory to hold the subset
		 * of tasks and cpus to monitor.
		 */
		ret = mongroup_create_dir(kn, rdtgrp, "mon_groups", NULL);
		if (ret) {
			rdt_last_cmd_puts("kernfs subdir error\n");
			goto out_del_list;
		}
		if (is_mba_sc(NULL))
			rdtgrp->mba_mbps_event = mba_mbps_default_event;
	}

	goto out_unlock;

out_del_list:
	list_del(&rdtgrp->rdtgroup_list);
out_rmid_free:
	mkdir_rdt_prepare_rmid_free(rdtgrp);
out_closid_free:
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
	if (resctrl_arch_alloc_capable() && parent_kn == rdtgroup_default.kn)
		return rdtgroup_mkdir_ctrl_mon(parent_kn, name, mode);

	/*
	 * If RDT monitoring is supported and the parent directory is a valid
	 * "mon_groups" directory, add a monitoring subdirectory.
	 */
	if (resctrl_arch_mon_capable() && is_mon_groups(parent_kn, name))
		return rdtgroup_mkdir_mon(parent_kn, name, mode);

	return -EPERM;
}

static int rdtgroup_rmdir_mon(struct rdtgroup *rdtgrp, cpumask_var_t tmpmask)
{
	struct rdtgroup *prdtgrp = rdtgrp->mon.parent;
	u32 closid, rmid;
	int cpu;

	/* Give any tasks back to the parent group */
	rdt_move_group_tasks(rdtgrp, prdtgrp, tmpmask);

	/*
	 * Update per cpu closid/rmid of the moved CPUs first.
	 * Note: the closid will not change, but the arch code still needs it.
	 */
	closid = prdtgrp->closid;
	rmid = prdtgrp->mon.rmid;
	for_each_cpu(cpu, &rdtgrp->cpu_mask)
		resctrl_arch_set_cpu_default_closid_rmid(cpu, closid, rmid);

	/*
	 * Update the MSR on moved CPUs and CPUs which have moved
	 * task running on them.
	 */
	cpumask_or(tmpmask, tmpmask, &rdtgrp->cpu_mask);
	update_closid_rmid(tmpmask, NULL);

	rdtgrp->flags = RDT_DELETED;
	free_rmid(rdtgrp->closid, rdtgrp->mon.rmid);

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
	u32 closid, rmid;
	int cpu;

	/* Give any tasks back to the default group */
	rdt_move_group_tasks(rdtgrp, &rdtgroup_default, tmpmask);

	/* Give any CPUs back to the default group */
	cpumask_or(&rdtgroup_default.cpu_mask,
		   &rdtgroup_default.cpu_mask, &rdtgrp->cpu_mask);

	/* Update per cpu closid and rmid of the moved CPUs first */
	closid = rdtgroup_default.closid;
	rmid = rdtgroup_default.mon.rmid;
	for_each_cpu(cpu, &rdtgrp->cpu_mask)
		resctrl_arch_set_cpu_default_closid_rmid(cpu, closid, rmid);

	/*
	 * Update the MSR on moved CPUs and CPUs which have moved
	 * task running on them.
	 */
	cpumask_or(tmpmask, tmpmask, &rdtgrp->cpu_mask);
	update_closid_rmid(tmpmask, NULL);

	free_rmid(rdtgrp->closid, rdtgrp->mon.rmid);
	closid_free(rdtgrp->closid);

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

/**
 * mongrp_reparent() - replace parent CTRL_MON group of a MON group
 * @rdtgrp:		the MON group whose parent should be replaced
 * @new_prdtgrp:	replacement parent CTRL_MON group for @rdtgrp
 * @cpus:		cpumask provided by the caller for use during this call
 *
 * Replaces the parent CTRL_MON group for a MON group, resulting in all member
 * tasks' CLOSID immediately changing to that of the new parent group.
 * Monitoring data for the group is unaffected by this operation.
 */
static void mongrp_reparent(struct rdtgroup *rdtgrp,
			    struct rdtgroup *new_prdtgrp,
			    cpumask_var_t cpus)
{
	struct rdtgroup *prdtgrp = rdtgrp->mon.parent;

	WARN_ON(rdtgrp->type != RDTMON_GROUP);
	WARN_ON(new_prdtgrp->type != RDTCTRL_GROUP);

	/* Nothing to do when simply renaming a MON group. */
	if (prdtgrp == new_prdtgrp)
		return;

	WARN_ON(list_empty(&prdtgrp->mon.crdtgrp_list));
	list_move_tail(&rdtgrp->mon.crdtgrp_list,
		       &new_prdtgrp->mon.crdtgrp_list);

	rdtgrp->mon.parent = new_prdtgrp;
	rdtgrp->closid = new_prdtgrp->closid;

	/* Propagate updated closid to all tasks in this group. */
	rdt_move_group_tasks(rdtgrp, rdtgrp, cpus);

	update_closid_rmid(cpus, NULL);
}

static int rdtgroup_rename(struct kernfs_node *kn,
			   struct kernfs_node *new_parent, const char *new_name)
{
	struct rdtgroup *new_prdtgrp;
	struct rdtgroup *rdtgrp;
	cpumask_var_t tmpmask;
	int ret;

	rdtgrp = kernfs_to_rdtgroup(kn);
	new_prdtgrp = kernfs_to_rdtgroup(new_parent);
	if (!rdtgrp || !new_prdtgrp)
		return -ENOENT;

	/* Release both kernfs active_refs before obtaining rdtgroup mutex. */
	rdtgroup_kn_get(rdtgrp, kn);
	rdtgroup_kn_get(new_prdtgrp, new_parent);

	mutex_lock(&rdtgroup_mutex);

	rdt_last_cmd_clear();

	/*
	 * Don't allow kernfs_to_rdtgroup() to return a parent rdtgroup if
	 * either kernfs_node is a file.
	 */
	if (kernfs_type(kn) != KERNFS_DIR ||
	    kernfs_type(new_parent) != KERNFS_DIR) {
		rdt_last_cmd_puts("Source and destination must be directories");
		ret = -EPERM;
		goto out;
	}

	if ((rdtgrp->flags & RDT_DELETED) || (new_prdtgrp->flags & RDT_DELETED)) {
		ret = -ENOENT;
		goto out;
	}

	if (rdtgrp->type != RDTMON_GROUP || !kn->parent ||
	    !is_mon_groups(kn->parent, kn->name)) {
		rdt_last_cmd_puts("Source must be a MON group\n");
		ret = -EPERM;
		goto out;
	}

	if (!is_mon_groups(new_parent, new_name)) {
		rdt_last_cmd_puts("Destination must be a mon_groups subdirectory\n");
		ret = -EPERM;
		goto out;
	}

	/*
	 * If the MON group is monitoring CPUs, the CPUs must be assigned to the
	 * current parent CTRL_MON group and therefore cannot be assigned to
	 * the new parent, making the move illegal.
	 */
	if (!cpumask_empty(&rdtgrp->cpu_mask) &&
	    rdtgrp->mon.parent != new_prdtgrp) {
		rdt_last_cmd_puts("Cannot move a MON group that monitors CPUs\n");
		ret = -EPERM;
		goto out;
	}

	/*
	 * Allocate the cpumask for use in mongrp_reparent() to avoid the
	 * possibility of failing to allocate it after kernfs_rename() has
	 * succeeded.
	 */
	if (!zalloc_cpumask_var(&tmpmask, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Perform all input validation and allocations needed to ensure
	 * mongrp_reparent() will succeed before calling kernfs_rename(),
	 * otherwise it would be necessary to revert this call if
	 * mongrp_reparent() failed.
	 */
	ret = kernfs_rename(kn, new_parent, new_name);
	if (!ret)
		mongrp_reparent(rdtgrp, new_prdtgrp, tmpmask);

	free_cpumask_var(tmpmask);

out:
	mutex_unlock(&rdtgroup_mutex);
	rdtgroup_kn_put(rdtgrp, kn);
	rdtgroup_kn_put(new_prdtgrp, new_parent);
	return ret;
}

static int rdtgroup_show_options(struct seq_file *seq, struct kernfs_root *kf)
{
	if (resctrl_arch_get_cdp_enabled(RDT_RESOURCE_L3))
		seq_puts(seq, ",cdp");

	if (resctrl_arch_get_cdp_enabled(RDT_RESOURCE_L2))
		seq_puts(seq, ",cdpl2");

	if (is_mba_sc(resctrl_arch_get_resource(RDT_RESOURCE_MBA)))
		seq_puts(seq, ",mba_MBps");

	if (resctrl_debug)
		seq_puts(seq, ",debug");

	return 0;
}

static struct kernfs_syscall_ops rdtgroup_kf_syscall_ops = {
	.mkdir		= rdtgroup_mkdir,
	.rmdir		= rdtgroup_rmdir,
	.rename		= rdtgroup_rename,
	.show_options	= rdtgroup_show_options,
};

static int rdtgroup_setup_root(struct rdt_fs_context *ctx)
{
	rdt_root = kernfs_create_root(&rdtgroup_kf_syscall_ops,
				      KERNFS_ROOT_CREATE_DEACTIVATED |
				      KERNFS_ROOT_EXTRA_OPEN_PERM_CHECK,
				      &rdtgroup_default);
	if (IS_ERR(rdt_root))
		return PTR_ERR(rdt_root);

	ctx->kfc.root = rdt_root;
	rdtgroup_default.kn = kernfs_root_to_node(rdt_root);

	return 0;
}

static void rdtgroup_destroy_root(void)
{
	kernfs_destroy_root(rdt_root);
	rdtgroup_default.kn = NULL;
}

static void __init rdtgroup_setup_default(void)
{
	mutex_lock(&rdtgroup_mutex);

	rdtgroup_default.closid = RESCTRL_RESERVED_CLOSID;
	rdtgroup_default.mon.rmid = RESCTRL_RESERVED_RMID;
	rdtgroup_default.type = RDTCTRL_GROUP;
	INIT_LIST_HEAD(&rdtgroup_default.mon.crdtgrp_list);

	list_add(&rdtgroup_default.rdtgroup_list, &rdt_all_groups);

	mutex_unlock(&rdtgroup_mutex);
}

static void domain_destroy_mon_state(struct rdt_mon_domain *d)
{
	bitmap_free(d->rmid_busy_llc);
	kfree(d->mbm_total);
	kfree(d->mbm_local);
}

void resctrl_offline_ctrl_domain(struct rdt_resource *r, struct rdt_ctrl_domain *d)
{
	mutex_lock(&rdtgroup_mutex);

	if (supports_mba_mbps() && r->rid == RDT_RESOURCE_MBA)
		mba_sc_domain_destroy(r, d);

	mutex_unlock(&rdtgroup_mutex);
}

void resctrl_offline_mon_domain(struct rdt_resource *r, struct rdt_mon_domain *d)
{
	mutex_lock(&rdtgroup_mutex);

	/*
	 * If resctrl is mounted, remove all the
	 * per domain monitor data directories.
	 */
	if (resctrl_mounted && resctrl_arch_mon_capable())
		rmdir_mondata_subdir_allrdtgrp(r, d);

	if (resctrl_is_mbm_enabled())
		cancel_delayed_work(&d->mbm_over);
	if (resctrl_arch_is_llc_occupancy_enabled() && has_busy_rmid(d)) {
		/*
		 * When a package is going down, forcefully
		 * decrement rmid->ebusy. There is no way to know
		 * that the L3 was flushed and hence may lead to
		 * incorrect counts in rare scenarios, but leaving
		 * the RMID as busy creates RMID leaks if the
		 * package never comes back.
		 */
		__check_limbo(d, true);
		cancel_delayed_work(&d->cqm_limbo);
	}

	domain_destroy_mon_state(d);

	mutex_unlock(&rdtgroup_mutex);
}

/**
 * domain_setup_mon_state() -  Initialise domain monitoring structures.
 * @r:	The resource for the newly online domain.
 * @d:	The newly online domain.
 *
 * Allocate monitor resources that belong to this domain.
 * Called when the first CPU of a domain comes online, regardless of whether
 * the filesystem is mounted.
 * During boot this may be called before global allocations have been made by
 * resctrl_mon_resource_init().
 *
 * Returns 0 for success, or -ENOMEM.
 */
static int domain_setup_mon_state(struct rdt_resource *r, struct rdt_mon_domain *d)
{
	u32 idx_limit = resctrl_arch_system_num_rmid_idx();
	size_t tsize;

	if (resctrl_arch_is_llc_occupancy_enabled()) {
		d->rmid_busy_llc = bitmap_zalloc(idx_limit, GFP_KERNEL);
		if (!d->rmid_busy_llc)
			return -ENOMEM;
	}
	if (resctrl_arch_is_mbm_total_enabled()) {
		tsize = sizeof(*d->mbm_total);
		d->mbm_total = kcalloc(idx_limit, tsize, GFP_KERNEL);
		if (!d->mbm_total) {
			bitmap_free(d->rmid_busy_llc);
			return -ENOMEM;
		}
	}
	if (resctrl_arch_is_mbm_local_enabled()) {
		tsize = sizeof(*d->mbm_local);
		d->mbm_local = kcalloc(idx_limit, tsize, GFP_KERNEL);
		if (!d->mbm_local) {
			bitmap_free(d->rmid_busy_llc);
			kfree(d->mbm_total);
			return -ENOMEM;
		}
	}

	return 0;
}

int resctrl_online_ctrl_domain(struct rdt_resource *r, struct rdt_ctrl_domain *d)
{
	int err = 0;

	mutex_lock(&rdtgroup_mutex);

	if (supports_mba_mbps() && r->rid == RDT_RESOURCE_MBA) {
		/* RDT_RESOURCE_MBA is never mon_capable */
		err = mba_sc_domain_allocate(r, d);
	}

	mutex_unlock(&rdtgroup_mutex);

	return err;
}

int resctrl_online_mon_domain(struct rdt_resource *r, struct rdt_mon_domain *d)
{
	int err;

	mutex_lock(&rdtgroup_mutex);

	err = domain_setup_mon_state(r, d);
	if (err)
		goto out_unlock;

	if (resctrl_is_mbm_enabled()) {
		INIT_DELAYED_WORK(&d->mbm_over, mbm_handle_overflow);
		mbm_setup_overflow_handler(d, MBM_OVERFLOW_INTERVAL,
					   RESCTRL_PICK_ANY_CPU);
	}

	if (resctrl_arch_is_llc_occupancy_enabled())
		INIT_DELAYED_WORK(&d->cqm_limbo, cqm_handle_limbo);

	/*
	 * If the filesystem is not mounted then only the default resource group
	 * exists. Creation of its directories is deferred until mount time
	 * by rdt_get_tree() calling mkdir_mondata_all().
	 * If resctrl is mounted, add per domain monitor data directories.
	 */
	if (resctrl_mounted && resctrl_arch_mon_capable())
		mkdir_mondata_subdir_allrdtgrp(r, d);

out_unlock:
	mutex_unlock(&rdtgroup_mutex);

	return err;
}

void resctrl_online_cpu(unsigned int cpu)
{
	mutex_lock(&rdtgroup_mutex);
	/* The CPU is set in default rdtgroup after online. */
	cpumask_set_cpu(cpu, &rdtgroup_default.cpu_mask);
	mutex_unlock(&rdtgroup_mutex);
}

static void clear_childcpus(struct rdtgroup *r, unsigned int cpu)
{
	struct rdtgroup *cr;

	list_for_each_entry(cr, &r->mon.crdtgrp_list, mon.crdtgrp_list) {
		if (cpumask_test_and_clear_cpu(cpu, &cr->cpu_mask))
			break;
	}
}

static struct rdt_mon_domain *get_mon_domain_from_cpu(int cpu,
						      struct rdt_resource *r)
{
	struct rdt_mon_domain *d;

	lockdep_assert_cpus_held();

	list_for_each_entry(d, &r->mon_domains, hdr.list) {
		/* Find the domain that contains this CPU */
		if (cpumask_test_cpu(cpu, &d->hdr.cpu_mask))
			return d;
	}

	return NULL;
}

void resctrl_offline_cpu(unsigned int cpu)
{
	struct rdt_resource *l3 = resctrl_arch_get_resource(RDT_RESOURCE_L3);
	struct rdt_mon_domain *d;
	struct rdtgroup *rdtgrp;

	mutex_lock(&rdtgroup_mutex);
	list_for_each_entry(rdtgrp, &rdt_all_groups, rdtgroup_list) {
		if (cpumask_test_and_clear_cpu(cpu, &rdtgrp->cpu_mask)) {
			clear_childcpus(rdtgrp, cpu);
			break;
		}
	}

	if (!l3->mon_capable)
		goto out_unlock;

	d = get_mon_domain_from_cpu(cpu, l3);
	if (d) {
		if (resctrl_is_mbm_enabled() && cpu == d->mbm_work_cpu) {
			cancel_delayed_work(&d->mbm_over);
			mbm_setup_overflow_handler(d, 0, cpu);
		}
		if (resctrl_arch_is_llc_occupancy_enabled() &&
		    cpu == d->cqm_work_cpu && has_busy_rmid(d)) {
			cancel_delayed_work(&d->cqm_limbo);
			cqm_setup_limbo_handler(d, 0, cpu);
		}
	}

out_unlock:
	mutex_unlock(&rdtgroup_mutex);
}

/*
 * resctrl_init - resctrl filesystem initialization
 *
 * Setup resctrl file system including set up root, create mount point,
 * register resctrl filesystem, and initialize files under root directory.
 *
 * Return: 0 on success or -errno
 */
int __init resctrl_init(void)
{
	int ret = 0;

	seq_buf_init(&last_cmd_status, last_cmd_status_buf,
		     sizeof(last_cmd_status_buf));

	rdtgroup_setup_default();

	thread_throttle_mode_init();

	ret = resctrl_mon_resource_init();
	if (ret)
		return ret;

	ret = sysfs_create_mount_point(fs_kobj, "resctrl");
	if (ret) {
		resctrl_mon_resource_exit();
		return ret;
	}

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
	resctrl_mon_resource_exit();

	return ret;
}

void __exit resctrl_exit(void)
{
	debugfs_remove_recursive(debugfs_resctrl);
	unregister_filesystem(&rdt_fs_type);
	sysfs_remove_mount_point(fs_kobj, "resctrl");

	resctrl_mon_resource_exit();
}
