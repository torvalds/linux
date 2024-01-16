// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 */

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sched/signal.h>
#include <linux/delay.h>
#include <linux/export.h>

#include "w1_internal.h"

DEFINE_SPINLOCK(w1_flock);
static LIST_HEAD(w1_families);

/**
 * w1_register_family() - register a device family driver
 * @newf:	family to register
 */
int w1_register_family(struct w1_family *newf)
{
	struct list_head *ent, *n;
	struct w1_family *f;
	int ret = 0;

	spin_lock(&w1_flock);
	list_for_each_safe(ent, n, &w1_families) {
		f = list_entry(ent, struct w1_family, family_entry);

		if (f->fid == newf->fid) {
			ret = -EEXIST;
			break;
		}
	}

	if (!ret) {
		atomic_set(&newf->refcnt, 0);
		list_add_tail(&newf->family_entry, &w1_families);
	}
	spin_unlock(&w1_flock);

	/* check default devices against the new set of drivers */
	w1_reconnect_slaves(newf, 1);

	return ret;
}
EXPORT_SYMBOL(w1_register_family);

/**
 * w1_unregister_family() - unregister a device family driver
 * @fent:	family to unregister
 */
void w1_unregister_family(struct w1_family *fent)
{
	struct list_head *ent, *n;
	struct w1_family *f;

	spin_lock(&w1_flock);
	list_for_each_safe(ent, n, &w1_families) {
		f = list_entry(ent, struct w1_family, family_entry);

		if (f->fid == fent->fid) {
			list_del(&fent->family_entry);
			break;
		}
	}
	spin_unlock(&w1_flock);

	/* deatch devices using this family code */
	w1_reconnect_slaves(fent, 0);

	while (atomic_read(&fent->refcnt)) {
		pr_info("Waiting for family %u to become free: refcnt=%d.\n",
				fent->fid, atomic_read(&fent->refcnt));

		if (msleep_interruptible(1000))
			flush_signals(current);
	}
}
EXPORT_SYMBOL(w1_unregister_family);

/*
 * Should be called under w1_flock held.
 */
struct w1_family * w1_family_registered(u8 fid)
{
	struct list_head *ent, *n;
	struct w1_family *f = NULL;
	int ret = 0;

	list_for_each_safe(ent, n, &w1_families) {
		f = list_entry(ent, struct w1_family, family_entry);

		if (f->fid == fid) {
			ret = 1;
			break;
		}
	}

	return (ret) ? f : NULL;
}

static void __w1_family_put(struct w1_family *f)
{
	atomic_dec(&f->refcnt);
}

void w1_family_put(struct w1_family *f)
{
	spin_lock(&w1_flock);
	__w1_family_put(f);
	spin_unlock(&w1_flock);
}

#if 0
void w1_family_get(struct w1_family *f)
{
	spin_lock(&w1_flock);
	__w1_family_get(f);
	spin_unlock(&w1_flock);
}
#endif  /*  0  */

void __w1_family_get(struct w1_family *f)
{
	smp_mb__before_atomic();
	atomic_inc(&f->refcnt);
	smp_mb__after_atomic();
}
