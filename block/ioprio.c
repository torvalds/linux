// SPDX-License-Identifier: GPL-2.0
/*
 * fs/ioprio.c
 *
 * Copyright (C) 2004 Jens Axboe <axboe@kernel.dk>
 *
 * Helper functions for setting/querying io priorities of processes. The
 * system calls closely mimmick getpriority/setpriority, see the man page for
 * those. The prio argument is a composite of prio class and prio data, where
 * the data argument has meaning within that class. The standard scheduling
 * classes have 8 distinct prio levels, with 0 being the highest prio and 7
 * being the lowest.
 *
 * IOW, setting BE scheduling class with prio 2 is done ala:
 *
 * unsigned int prio = (IOPRIO_CLASS_BE << IOPRIO_CLASS_SHIFT) | 2;
 *
 * ioprio_set(PRIO_PROCESS, pid, prio);
 *
 * See also Documentation/block/ioprio.rst
 *
 */
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/ioprio.h>
#include <linux/cred.h>
#include <linux/blkdev.h>
#include <linux/capability.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/pid_namespace.h>

int ioprio_check_cap(int ioprio)
{
	int class = IOPRIO_PRIO_CLASS(ioprio);
	int level = IOPRIO_PRIO_LEVEL(ioprio);

	switch (class) {
		case IOPRIO_CLASS_RT:
			/*
			 * Originally this only checked for CAP_SYS_ADMIN,
			 * which was implicitly allowed for pid 0 by security
			 * modules such as SELinux. Make sure we check
			 * CAP_SYS_ADMIN first to avoid a denial/avc for
			 * possibly missing CAP_SYS_NICE permission.
			 */
			if (!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_NICE))
				return -EPERM;
			fallthrough;
			/* rt has prio field too */
		case IOPRIO_CLASS_BE:
			if (level >= IOPRIO_NR_LEVELS)
				return -EINVAL;
			break;
		case IOPRIO_CLASS_IDLE:
			break;
		case IOPRIO_CLASS_NONE:
			if (level)
				return -EINVAL;
			break;
		case IOPRIO_CLASS_INVALID:
		default:
			return -EINVAL;
	}

	return 0;
}

SYSCALL_DEFINE3(ioprio_set, int, which, int, who, int, ioprio)
{
	struct task_struct *p, *g;
	struct user_struct *user;
	struct pid *pgrp;
	kuid_t uid;
	int ret;

	ret = ioprio_check_cap(ioprio);
	if (ret)
		return ret;

	ret = -ESRCH;
	rcu_read_lock();
	switch (which) {
		case IOPRIO_WHO_PROCESS:
			if (!who)
				p = current;
			else
				p = find_task_by_vpid(who);
			if (p)
				ret = set_task_ioprio(p, ioprio);
			break;
		case IOPRIO_WHO_PGRP:
			if (!who)
				pgrp = task_pgrp(current);
			else
				pgrp = find_vpid(who);

			read_lock(&tasklist_lock);
			do_each_pid_thread(pgrp, PIDTYPE_PGID, p) {
				ret = set_task_ioprio(p, ioprio);
				if (ret) {
					read_unlock(&tasklist_lock);
					goto out;
				}
			} while_each_pid_thread(pgrp, PIDTYPE_PGID, p);
			read_unlock(&tasklist_lock);

			break;
		case IOPRIO_WHO_USER:
			uid = make_kuid(current_user_ns(), who);
			if (!uid_valid(uid))
				break;
			if (!who)
				user = current_user();
			else
				user = find_user(uid);

			if (!user)
				break;

			for_each_process_thread(g, p) {
				if (!uid_eq(task_uid(p), uid) ||
				    !task_pid_vnr(p))
					continue;
				ret = set_task_ioprio(p, ioprio);
				if (ret)
					goto free_uid;
			}
free_uid:
			if (who)
				free_uid(user);
			break;
		default:
			ret = -EINVAL;
	}

out:
	rcu_read_unlock();
	return ret;
}

/*
 * If the task has set an I/O priority, use that. Otherwise, return
 * the default I/O priority.
 *
 * Expected to be called for current task or with task_lock() held to keep
 * io_context stable.
 */
int __get_task_ioprio(struct task_struct *p)
{
	struct io_context *ioc = p->io_context;
	int prio;

	if (p != current)
		lockdep_assert_held(&p->alloc_lock);
	if (ioc)
		prio = ioc->ioprio;
	else
		prio = IOPRIO_DEFAULT;

	if (IOPRIO_PRIO_CLASS(prio) == IOPRIO_CLASS_NONE)
		prio = IOPRIO_PRIO_VALUE(task_nice_ioclass(p),
					 task_nice_ioprio(p));
	return prio;
}
EXPORT_SYMBOL_GPL(__get_task_ioprio);

static int get_task_ioprio(struct task_struct *p)
{
	int ret;

	ret = security_task_getioprio(p);
	if (ret)
		goto out;
	task_lock(p);
	ret = __get_task_ioprio(p);
	task_unlock(p);
out:
	return ret;
}

/*
 * Return raw IO priority value as set by userspace. We use this for
 * ioprio_get(pid, IOPRIO_WHO_PROCESS) so that we keep historical behavior and
 * also so that userspace can distinguish unset IO priority (which just gets
 * overriden based on task's nice value) from IO priority set to some value.
 */
static int get_task_raw_ioprio(struct task_struct *p)
{
	int ret;

	ret = security_task_getioprio(p);
	if (ret)
		goto out;
	task_lock(p);
	if (p->io_context)
		ret = p->io_context->ioprio;
	else
		ret = IOPRIO_DEFAULT;
	task_unlock(p);
out:
	return ret;
}

static int ioprio_best(unsigned short aprio, unsigned short bprio)
{
	return min(aprio, bprio);
}

SYSCALL_DEFINE2(ioprio_get, int, which, int, who)
{
	struct task_struct *g, *p;
	struct user_struct *user;
	struct pid *pgrp;
	kuid_t uid;
	int ret = -ESRCH;
	int tmpio;

	rcu_read_lock();
	switch (which) {
		case IOPRIO_WHO_PROCESS:
			if (!who)
				p = current;
			else
				p = find_task_by_vpid(who);
			if (p)
				ret = get_task_raw_ioprio(p);
			break;
		case IOPRIO_WHO_PGRP:
			if (!who)
				pgrp = task_pgrp(current);
			else
				pgrp = find_vpid(who);
			read_lock(&tasklist_lock);
			do_each_pid_thread(pgrp, PIDTYPE_PGID, p) {
				tmpio = get_task_ioprio(p);
				if (tmpio < 0)
					continue;
				if (ret == -ESRCH)
					ret = tmpio;
				else
					ret = ioprio_best(ret, tmpio);
			} while_each_pid_thread(pgrp, PIDTYPE_PGID, p);
			read_unlock(&tasklist_lock);

			break;
		case IOPRIO_WHO_USER:
			uid = make_kuid(current_user_ns(), who);
			if (!who)
				user = current_user();
			else
				user = find_user(uid);

			if (!user)
				break;

			for_each_process_thread(g, p) {
				if (!uid_eq(task_uid(p), user->uid) ||
				    !task_pid_vnr(p))
					continue;
				tmpio = get_task_ioprio(p);
				if (tmpio < 0)
					continue;
				if (ret == -ESRCH)
					ret = tmpio;
				else
					ret = ioprio_best(ret, tmpio);
			}

			if (who)
				free_uid(user);
			break;
		default:
			ret = -EINVAL;
	}

	rcu_read_unlock();
	return ret;
}
