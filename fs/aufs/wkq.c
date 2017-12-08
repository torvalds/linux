/*
 * Copyright (C) 2005-2017 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * workqueue for asynchronous/super-io operations
 * todo: try new dredential scheme
 */

#include <linux/module.h>
#include "aufs.h"

/* internal workqueue named AUFS_WKQ_NAME */

static struct workqueue_struct *au_wkq;

struct au_wkinfo {
	struct work_struct wk;
	struct kobject *kobj;

	unsigned int flags; /* see wkq.h */

	au_wkq_func_t func;
	void *args;

#ifdef CONFIG_LOCKDEP
	int dont_check;
	struct held_lock **hlock;
#endif

	struct completion *comp;
};

/* ---------------------------------------------------------------------- */
/*
 * Aufs passes some operations to the workqueue such as the internal copyup.
 * This scheme looks rather unnatural for LOCKDEP debugging feature, since the
 * job run by workqueue depends upon the locks acquired in the other task.
 * Delegating a small operation to the workqueue, aufs passes its lockdep
 * information too. And the job in the workqueue restores the info in order to
 * pretend as if it acquired those locks. This is just to make LOCKDEP work
 * correctly and expectedly.
 */

#ifndef CONFIG_LOCKDEP
AuStubInt0(au_wkq_lockdep_alloc, struct au_wkinfo *wkinfo);
AuStubVoid(au_wkq_lockdep_free, struct au_wkinfo *wkinfo);
AuStubVoid(au_wkq_lockdep_pre, struct au_wkinfo *wkinfo);
AuStubVoid(au_wkq_lockdep_post, struct au_wkinfo *wkinfo);
AuStubVoid(au_wkq_lockdep_init, struct au_wkinfo *wkinfo);
#else
static void au_wkq_lockdep_init(struct au_wkinfo *wkinfo)
{
	wkinfo->hlock = NULL;
	wkinfo->dont_check = 0;
}

/*
 * 1: matched
 * 0: unmatched
 */
static int au_wkq_lockdep_test(struct lock_class_key *key, const char *name)
{
	static DEFINE_SPINLOCK(spin);
	static struct {
		char *name;
		struct lock_class_key *key;
	} a[] = {
		{ .name = "&sbinfo->si_rwsem" },
		{ .name = "&finfo->fi_rwsem" },
		{ .name = "&dinfo->di_rwsem" },
		{ .name = "&iinfo->ii_rwsem" }
	};
	static int set;
	int i;

	/* lockless read from 'set.' see below */
	if (set == ARRAY_SIZE(a)) {
		for (i = 0; i < ARRAY_SIZE(a); i++)
			if (a[i].key == key)
				goto match;
		goto unmatch;
	}

	spin_lock(&spin);
	if (set)
		for (i = 0; i < ARRAY_SIZE(a); i++)
			if (a[i].key == key) {
				spin_unlock(&spin);
				goto match;
			}
	for (i = 0; i < ARRAY_SIZE(a); i++) {
		if (a[i].key) {
			if (unlikely(a[i].key == key)) { /* rare but possible */
				spin_unlock(&spin);
				goto match;
			} else
				continue;
		}
		if (strstr(a[i].name, name)) {
			/*
			 * the order of these three lines is important for the
			 * lockless read above.
			 */
			a[i].key = key;
			spin_unlock(&spin);
			set++;
			/* AuDbg("%d, %s\n", set, name); */
			goto match;
		}
	}
	spin_unlock(&spin);
	goto unmatch;

match:
	return 1;
unmatch:
	return 0;
}

static int au_wkq_lockdep_alloc(struct au_wkinfo *wkinfo)
{
	int err, n;
	struct task_struct *curr;
	struct held_lock **hl, *held_locks, *p;

	err = 0;
	curr = current;
	wkinfo->dont_check = lockdep_recursing(curr);
	if (wkinfo->dont_check)
		goto out;
	n = curr->lockdep_depth;
	if (!n)
		goto out;

	err = -ENOMEM;
	wkinfo->hlock = kmalloc_array(n + 1, sizeof(*wkinfo->hlock), GFP_NOFS);
	if (unlikely(!wkinfo->hlock))
		goto out;

	err = 0;
#if 0
	if (0 && au_debug_test()) /* left for debugging */
		lockdep_print_held_locks(curr);
#endif
	held_locks = curr->held_locks;
	hl = wkinfo->hlock;
	while (n--) {
		p = held_locks++;
		if (au_wkq_lockdep_test(p->instance->key, p->instance->name))
			*hl++ = p;
	}
	*hl = NULL;

out:
	return err;
}

static void au_wkq_lockdep_free(struct au_wkinfo *wkinfo)
{
	kfree(wkinfo->hlock);
}

static void au_wkq_lockdep_pre(struct au_wkinfo *wkinfo)
{
	struct held_lock *p, **hl = wkinfo->hlock;
	int subclass;

	if (wkinfo->dont_check)
		lockdep_off();
	if (!hl)
		return;
	while ((p = *hl++)) { /* assignment */
		subclass = lockdep_hlock_class(p)->subclass;
		/* AuDbg("%s, %d\n", p->instance->name, subclass); */
		if (p->read)
			rwsem_acquire_read(p->instance, subclass, 0,
					   /*p->acquire_ip*/_RET_IP_);
		else
			rwsem_acquire(p->instance, subclass, 0,
				      /*p->acquire_ip*/_RET_IP_);
	}
}

static void au_wkq_lockdep_post(struct au_wkinfo *wkinfo)
{
	struct held_lock *p, **hl = wkinfo->hlock;

	if (wkinfo->dont_check)
		lockdep_on();
	if (!hl)
		return;
	while ((p = *hl++)) /* assignment */
		rwsem_release(p->instance, 0, /*p->acquire_ip*/_RET_IP_);
}
#endif

static void wkq_func(struct work_struct *wk)
{
	struct au_wkinfo *wkinfo = container_of(wk, struct au_wkinfo, wk);

	AuDebugOn(!uid_eq(current_fsuid(), GLOBAL_ROOT_UID));
	AuDebugOn(rlimit(RLIMIT_FSIZE) != RLIM_INFINITY);

	au_wkq_lockdep_pre(wkinfo);
	wkinfo->func(wkinfo->args);
	au_wkq_lockdep_post(wkinfo);
	if (au_ftest_wkq(wkinfo->flags, WAIT))
		complete(wkinfo->comp);
	else {
		kobject_put(wkinfo->kobj);
		module_put(THIS_MODULE); /* todo: ?? */
		kfree(wkinfo);
	}
}

/*
 * Since struct completion is large, try allocating it dynamically.
 */
#if 1 /* defined(CONFIG_4KSTACKS) || defined(AuTest4KSTACKS) */
#define AuWkqCompDeclare(name)	struct completion *comp = NULL

static int au_wkq_comp_alloc(struct au_wkinfo *wkinfo, struct completion **comp)
{
	*comp = kmalloc(sizeof(**comp), GFP_NOFS);
	if (*comp) {
		init_completion(*comp);
		wkinfo->comp = *comp;
		return 0;
	}
	return -ENOMEM;
}

static void au_wkq_comp_free(struct completion *comp)
{
	kfree(comp);
}

#else

/* no braces */
#define AuWkqCompDeclare(name) \
	DECLARE_COMPLETION_ONSTACK(_ ## name); \
	struct completion *comp = &_ ## name

static int au_wkq_comp_alloc(struct au_wkinfo *wkinfo, struct completion **comp)
{
	wkinfo->comp = *comp;
	return 0;
}

static void au_wkq_comp_free(struct completion *comp __maybe_unused)
{
	/* empty */
}
#endif /* 4KSTACKS */

static void au_wkq_run(struct au_wkinfo *wkinfo)
{
	if (au_ftest_wkq(wkinfo->flags, NEST)) {
		if (au_wkq_test()) {
			AuWarn1("wkq from wkq, unless silly-rename on NFS,"
				" due to a dead dir by UDBA?\n");
			AuDebugOn(au_ftest_wkq(wkinfo->flags, WAIT));
		}
	} else
		au_dbg_verify_kthread();

	if (au_ftest_wkq(wkinfo->flags, WAIT)) {
		INIT_WORK_ONSTACK(&wkinfo->wk, wkq_func);
		queue_work(au_wkq, &wkinfo->wk);
	} else {
		INIT_WORK(&wkinfo->wk, wkq_func);
		schedule_work(&wkinfo->wk);
	}
}

/*
 * Be careful. It is easy to make deadlock happen.
 * processA: lock, wkq and wait
 * processB: wkq and wait, lock in wkq
 * --> deadlock
 */
int au_wkq_do_wait(unsigned int flags, au_wkq_func_t func, void *args)
{
	int err;
	AuWkqCompDeclare(comp);
	struct au_wkinfo wkinfo = {
		.flags	= flags,
		.func	= func,
		.args	= args
	};

	err = au_wkq_comp_alloc(&wkinfo, &comp);
	if (unlikely(err))
		goto out;
	err = au_wkq_lockdep_alloc(&wkinfo);
	if (unlikely(err))
		goto out_comp;
	if (!err) {
		au_wkq_run(&wkinfo);
		/* no timeout, no interrupt */
		wait_for_completion(wkinfo.comp);
	}
	au_wkq_lockdep_free(&wkinfo);

out_comp:
	au_wkq_comp_free(comp);
out:
	destroy_work_on_stack(&wkinfo.wk);
	return err;
}

/*
 * Note: dget/dput() in func for aufs dentries are not supported. It will be a
 * problem in a concurrent umounting.
 */
int au_wkq_nowait(au_wkq_func_t func, void *args, struct super_block *sb,
		  unsigned int flags)
{
	int err;
	struct au_wkinfo *wkinfo;

	atomic_inc(&au_sbi(sb)->si_nowait.nw_len);

	/*
	 * wkq_func() must free this wkinfo.
	 * it highly depends upon the implementation of workqueue.
	 */
	err = 0;
	wkinfo = kmalloc(sizeof(*wkinfo), GFP_NOFS);
	if (wkinfo) {
		wkinfo->kobj = &au_sbi(sb)->si_kobj;
		wkinfo->flags = flags & ~AuWkq_WAIT;
		wkinfo->func = func;
		wkinfo->args = args;
		wkinfo->comp = NULL;
		au_wkq_lockdep_init(wkinfo);
		kobject_get(wkinfo->kobj);
		__module_get(THIS_MODULE); /* todo: ?? */

		au_wkq_run(wkinfo);
	} else {
		err = -ENOMEM;
		au_nwt_done(&au_sbi(sb)->si_nowait);
	}

	return err;
}

/* ---------------------------------------------------------------------- */

void au_nwt_init(struct au_nowait_tasks *nwt)
{
	atomic_set(&nwt->nw_len, 0);
	/* smp_mb(); */ /* atomic_set */
	init_waitqueue_head(&nwt->nw_wq);
}

void au_wkq_fin(void)
{
	destroy_workqueue(au_wkq);
}

int __init au_wkq_init(void)
{
	int err;

	err = 0;
	au_wkq = alloc_workqueue(AUFS_WKQ_NAME, 0, WQ_DFL_ACTIVE);
	if (IS_ERR(au_wkq))
		err = PTR_ERR(au_wkq);
	else if (!au_wkq)
		err = -ENOMEM;

	return err;
}
