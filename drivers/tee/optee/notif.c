// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, Linaro Limited
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/arm-smccc.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tee_drv.h>
#include "optee_private.h"

struct notif_entry {
	struct list_head link;
	struct completion c;
	u_int key;
};

static bool have_key(struct optee *optee, u_int key)
{
	struct notif_entry *entry;

	list_for_each_entry(entry, &optee->notif.db, link)
		if (entry->key == key)
			return true;

	return false;
}

int optee_notif_wait(struct optee *optee, u_int key)
{
	unsigned long flags;
	struct notif_entry *entry;
	int rc = 0;

	if (key > optee->notif.max_key)
		return -EINVAL;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	init_completion(&entry->c);
	entry->key = key;

	spin_lock_irqsave(&optee->notif.lock, flags);

	/*
	 * If the bit is already set it means that the key has already
	 * been posted and we must not wait.
	 */
	if (test_bit(key, optee->notif.bitmap)) {
		clear_bit(key, optee->notif.bitmap);
		goto out;
	}

	/*
	 * Check if someone is already waiting for this key. If there is
	 * it's a programming error.
	 */
	if (have_key(optee, key)) {
		rc = -EBUSY;
		goto out;
	}

	list_add_tail(&entry->link, &optee->notif.db);

	/*
	 * Unlock temporarily and wait for completion.
	 */
	spin_unlock_irqrestore(&optee->notif.lock, flags);
	wait_for_completion(&entry->c);
	spin_lock_irqsave(&optee->notif.lock, flags);

	list_del(&entry->link);
out:
	spin_unlock_irqrestore(&optee->notif.lock, flags);

	kfree(entry);

	return rc;
}

int optee_notif_send(struct optee *optee, u_int key)
{
	unsigned long flags;
	struct notif_entry *entry;

	if (key > optee->notif.max_key)
		return -EINVAL;

	spin_lock_irqsave(&optee->notif.lock, flags);

	list_for_each_entry(entry, &optee->notif.db, link)
		if (entry->key == key) {
			complete(&entry->c);
			goto out;
		}

	/* Only set the bit in case there where nobody waiting */
	set_bit(key, optee->notif.bitmap);
out:
	spin_unlock_irqrestore(&optee->notif.lock, flags);

	return 0;
}

int optee_notif_init(struct optee *optee, u_int max_key)
{
	spin_lock_init(&optee->notif.lock);
	INIT_LIST_HEAD(&optee->notif.db);
	optee->notif.bitmap = bitmap_zalloc(max_key, GFP_KERNEL);
	if (!optee->notif.bitmap)
		return -ENOMEM;

	optee->notif.max_key = max_key;

	return 0;
}

void optee_notif_uninit(struct optee *optee)
{
	kfree(optee->notif.bitmap);
}
