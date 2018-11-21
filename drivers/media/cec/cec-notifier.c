// SPDX-License-Identifier: GPL-2.0-only
/*
 * cec-notifier.c - notify CEC drivers of physical address changes
 *
 * Copyright 2016 Russell King <rmk+kernel@arm.linux.org.uk>
 * Copyright 2016-2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/export.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kref.h>

#include <media/cec.h>
#include <media/cec-notifier.h>
#include <drm/drm_edid.h>

struct cec_notifier {
	struct mutex lock;
	struct list_head head;
	struct kref kref;
	struct device *dev;
	struct cec_adapter *cec_adap;
	void (*callback)(struct cec_adapter *adap, u16 pa);

	u16 phys_addr;
};

static LIST_HEAD(cec_notifiers);
static DEFINE_MUTEX(cec_notifiers_lock);

struct cec_notifier *cec_notifier_get(struct device *dev)
{
	struct cec_notifier *n;

	mutex_lock(&cec_notifiers_lock);
	list_for_each_entry(n, &cec_notifiers, head) {
		if (n->dev == dev) {
			kref_get(&n->kref);
			mutex_unlock(&cec_notifiers_lock);
			return n;
		}
	}
	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		goto unlock;
	n->dev = dev;
	n->phys_addr = CEC_PHYS_ADDR_INVALID;
	mutex_init(&n->lock);
	kref_init(&n->kref);
	list_add_tail(&n->head, &cec_notifiers);
unlock:
	mutex_unlock(&cec_notifiers_lock);
	return n;
}
EXPORT_SYMBOL_GPL(cec_notifier_get);

static void cec_notifier_release(struct kref *kref)
{
	struct cec_notifier *n =
		container_of(kref, struct cec_notifier, kref);

	list_del(&n->head);
	kfree(n);
}

void cec_notifier_put(struct cec_notifier *n)
{
	mutex_lock(&cec_notifiers_lock);
	kref_put(&n->kref, cec_notifier_release);
	mutex_unlock(&cec_notifiers_lock);
}
EXPORT_SYMBOL_GPL(cec_notifier_put);

void cec_notifier_set_phys_addr(struct cec_notifier *n, u16 pa)
{
	if (n == NULL)
		return;

	mutex_lock(&n->lock);
	n->phys_addr = pa;
	if (n->callback)
		n->callback(n->cec_adap, n->phys_addr);
	mutex_unlock(&n->lock);
}
EXPORT_SYMBOL_GPL(cec_notifier_set_phys_addr);

void cec_notifier_set_phys_addr_from_edid(struct cec_notifier *n,
					  const struct edid *edid)
{
	u16 pa = CEC_PHYS_ADDR_INVALID;

	if (n == NULL)
		return;

	if (edid && edid->extensions)
		pa = cec_get_edid_phys_addr((const u8 *)edid,
				EDID_LENGTH * (edid->extensions + 1), NULL);
	cec_notifier_set_phys_addr(n, pa);
}
EXPORT_SYMBOL_GPL(cec_notifier_set_phys_addr_from_edid);

void cec_notifier_register(struct cec_notifier *n,
			   struct cec_adapter *adap,
			   void (*callback)(struct cec_adapter *adap, u16 pa))
{
	kref_get(&n->kref);
	mutex_lock(&n->lock);
	n->cec_adap = adap;
	n->callback = callback;
	n->callback(adap, n->phys_addr);
	mutex_unlock(&n->lock);
}
EXPORT_SYMBOL_GPL(cec_notifier_register);

void cec_notifier_unregister(struct cec_notifier *n)
{
	mutex_lock(&n->lock);
	n->callback = NULL;
	mutex_unlock(&n->lock);
	cec_notifier_put(n);
}
EXPORT_SYMBOL_GPL(cec_notifier_unregister);
