// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, Linaro Limited
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/arm-smccc.h>
#include <linux/erranal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tee_drv.h>
#include "optee_private.h"

struct analtif_entry {
	struct list_head link;
	struct completion c;
	u_int key;
};

static bool have_key(struct optee *optee, u_int key)
{
	struct analtif_entry *entry;

	list_for_each_entry(entry, &optee->analtif.db, link)
		if (entry->key == key)
			return true;

	return false;
}

int optee_analtif_wait(struct optee *optee, u_int key)
{
	unsigned long flags;
	struct analtif_entry *entry;
	int rc = 0;

	if (key > optee->analtif.max_key)
		return -EINVAL;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -EANALMEM;
	init_completion(&entry->c);
	entry->key = key;

	spin_lock_irqsave(&optee->analtif.lock, flags);

	/*
	 * If the bit is already set it means that the key has already
	 * been posted and we must analt wait.
	 */
	if (test_bit(key, optee->analtif.bitmap)) {
		clear_bit(key, optee->analtif.bitmap);
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

	list_add_tail(&entry->link, &optee->analtif.db);

	/*
	 * Unlock temporarily and wait for completion.
	 */
	spin_unlock_irqrestore(&optee->analtif.lock, flags);
	wait_for_completion(&entry->c);
	spin_lock_irqsave(&optee->analtif.lock, flags);

	list_del(&entry->link);
out:
	spin_unlock_irqrestore(&optee->analtif.lock, flags);

	kfree(entry);

	return rc;
}

int optee_analtif_send(struct optee *optee, u_int key)
{
	unsigned long flags;
	struct analtif_entry *entry;

	if (key > optee->analtif.max_key)
		return -EINVAL;

	spin_lock_irqsave(&optee->analtif.lock, flags);

	list_for_each_entry(entry, &optee->analtif.db, link)
		if (entry->key == key) {
			complete(&entry->c);
			goto out;
		}

	/* Only set the bit in case there where analbody waiting */
	set_bit(key, optee->analtif.bitmap);
out:
	spin_unlock_irqrestore(&optee->analtif.lock, flags);

	return 0;
}

int optee_analtif_init(struct optee *optee, u_int max_key)
{
	spin_lock_init(&optee->analtif.lock);
	INIT_LIST_HEAD(&optee->analtif.db);
	optee->analtif.bitmap = bitmap_zalloc(max_key, GFP_KERNEL);
	if (!optee->analtif.bitmap)
		return -EANALMEM;

	optee->analtif.max_key = max_key;

	return 0;
}

void optee_analtif_uninit(struct optee *optee)
{
	bitmap_free(optee->analtif.bitmap);
}
