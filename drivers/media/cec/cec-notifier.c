// SPDX-License-Identifier: GPL-2.0-only
/*
 * cec-yestifier.c - yestify CEC drivers of physical address changes
 *
 * Copyright 2016 Russell King <rmk+kernel@arm.linux.org.uk>
 * Copyright 2016-2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/export.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/of_platform.h>

#include <media/cec.h>
#include <media/cec-yestifier.h>
#include <drm/drm_edid.h>

struct cec_yestifier {
	struct mutex lock;
	struct list_head head;
	struct kref kref;
	struct device *hdmi_dev;
	struct cec_connector_info conn_info;
	const char *conn_name;
	struct cec_adapter *cec_adap;
	void (*callback)(struct cec_adapter *adap, u16 pa);

	u16 phys_addr;
};

static LIST_HEAD(cec_yestifiers);
static DEFINE_MUTEX(cec_yestifiers_lock);

struct cec_yestifier *
cec_yestifier_get_conn(struct device *hdmi_dev, const char *conn_name)
{
	struct cec_yestifier *n;

	mutex_lock(&cec_yestifiers_lock);
	list_for_each_entry(n, &cec_yestifiers, head) {
		if (n->hdmi_dev == hdmi_dev &&
		    (!conn_name ||
		     (n->conn_name && !strcmp(n->conn_name, conn_name)))) {
			kref_get(&n->kref);
			mutex_unlock(&cec_yestifiers_lock);
			return n;
		}
	}
	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		goto unlock;
	n->hdmi_dev = hdmi_dev;
	if (conn_name) {
		n->conn_name = kstrdup(conn_name, GFP_KERNEL);
		if (!n->conn_name) {
			kfree(n);
			n = NULL;
			goto unlock;
		}
	}
	n->phys_addr = CEC_PHYS_ADDR_INVALID;

	mutex_init(&n->lock);
	kref_init(&n->kref);
	list_add_tail(&n->head, &cec_yestifiers);
unlock:
	mutex_unlock(&cec_yestifiers_lock);
	return n;
}
EXPORT_SYMBOL_GPL(cec_yestifier_get_conn);

static void cec_yestifier_release(struct kref *kref)
{
	struct cec_yestifier *n =
		container_of(kref, struct cec_yestifier, kref);

	list_del(&n->head);
	kfree(n->conn_name);
	kfree(n);
}

void cec_yestifier_put(struct cec_yestifier *n)
{
	mutex_lock(&cec_yestifiers_lock);
	kref_put(&n->kref, cec_yestifier_release);
	mutex_unlock(&cec_yestifiers_lock);
}
EXPORT_SYMBOL_GPL(cec_yestifier_put);

struct cec_yestifier *
cec_yestifier_conn_register(struct device *hdmi_dev, const char *conn_name,
			   const struct cec_connector_info *conn_info)
{
	struct cec_yestifier *n = cec_yestifier_get_conn(hdmi_dev, conn_name);

	if (!n)
		return n;

	mutex_lock(&n->lock);
	n->phys_addr = CEC_PHYS_ADDR_INVALID;
	if (conn_info)
		n->conn_info = *conn_info;
	else
		memset(&n->conn_info, 0, sizeof(n->conn_info));
	if (n->cec_adap) {
		cec_phys_addr_invalidate(n->cec_adap);
		cec_s_conn_info(n->cec_adap, conn_info);
	}
	mutex_unlock(&n->lock);
	return n;
}
EXPORT_SYMBOL_GPL(cec_yestifier_conn_register);

void cec_yestifier_conn_unregister(struct cec_yestifier *n)
{
	if (!n)
		return;

	mutex_lock(&n->lock);
	memset(&n->conn_info, 0, sizeof(n->conn_info));
	n->phys_addr = CEC_PHYS_ADDR_INVALID;
	if (n->cec_adap) {
		cec_phys_addr_invalidate(n->cec_adap);
		cec_s_conn_info(n->cec_adap, NULL);
	}
	mutex_unlock(&n->lock);
	cec_yestifier_put(n);
}
EXPORT_SYMBOL_GPL(cec_yestifier_conn_unregister);

struct cec_yestifier *
cec_yestifier_cec_adap_register(struct device *hdmi_dev, const char *conn_name,
			       struct cec_adapter *adap)
{
	struct cec_yestifier *n;

	if (WARN_ON(!adap))
		return NULL;

	n = cec_yestifier_get_conn(hdmi_dev, conn_name);
	if (!n)
		return n;

	mutex_lock(&n->lock);
	n->cec_adap = adap;
	adap->conn_info = n->conn_info;
	adap->yestifier = n;
	cec_s_phys_addr(adap, n->phys_addr, false);
	mutex_unlock(&n->lock);
	return n;
}
EXPORT_SYMBOL_GPL(cec_yestifier_cec_adap_register);

void cec_yestifier_cec_adap_unregister(struct cec_yestifier *n,
				      struct cec_adapter *adap)
{
	if (!n)
		return;

	mutex_lock(&n->lock);
	adap->yestifier = NULL;
	n->cec_adap = NULL;
	n->callback = NULL;
	mutex_unlock(&n->lock);
	cec_yestifier_put(n);
}
EXPORT_SYMBOL_GPL(cec_yestifier_cec_adap_unregister);

void cec_yestifier_set_phys_addr(struct cec_yestifier *n, u16 pa)
{
	if (n == NULL)
		return;

	mutex_lock(&n->lock);
	n->phys_addr = pa;
	if (n->callback)
		n->callback(n->cec_adap, n->phys_addr);
	else if (n->cec_adap)
		cec_s_phys_addr(n->cec_adap, n->phys_addr, false);
	mutex_unlock(&n->lock);
}
EXPORT_SYMBOL_GPL(cec_yestifier_set_phys_addr);

void cec_yestifier_set_phys_addr_from_edid(struct cec_yestifier *n,
					  const struct edid *edid)
{
	u16 pa = CEC_PHYS_ADDR_INVALID;

	if (n == NULL)
		return;

	if (edid && edid->extensions)
		pa = cec_get_edid_phys_addr((const u8 *)edid,
				EDID_LENGTH * (edid->extensions + 1), NULL);
	cec_yestifier_set_phys_addr(n, pa);
}
EXPORT_SYMBOL_GPL(cec_yestifier_set_phys_addr_from_edid);

void cec_yestifier_register(struct cec_yestifier *n,
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
EXPORT_SYMBOL_GPL(cec_yestifier_register);

void cec_yestifier_unregister(struct cec_yestifier *n)
{
	/* Do yesthing unless cec_yestifier_register was called first */
	if (!n->callback)
		return;

	mutex_lock(&n->lock);
	n->callback = NULL;
	n->cec_adap->yestifier = NULL;
	n->cec_adap = NULL;
	mutex_unlock(&n->lock);
	cec_yestifier_put(n);
}
EXPORT_SYMBOL_GPL(cec_yestifier_unregister);

struct device *cec_yestifier_parse_hdmi_phandle(struct device *dev)
{
	struct platform_device *hdmi_pdev;
	struct device *hdmi_dev = NULL;
	struct device_yesde *np;

	np = of_parse_phandle(dev->of_yesde, "hdmi-phandle", 0);

	if (!np) {
		dev_err(dev, "Failed to find HDMI yesde in device tree\n");
		return ERR_PTR(-ENODEV);
	}
	hdmi_pdev = of_find_device_by_yesde(np);
	of_yesde_put(np);
	if (hdmi_pdev) {
		hdmi_dev = &hdmi_pdev->dev;
		/*
		 * Note that the device struct is only used as a key into the
		 * cec_yestifiers list, it is never actually accessed.
		 * So we decrement the reference here so we don't leak
		 * memory.
		 */
		put_device(hdmi_dev);
		return hdmi_dev;
	}
	return ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL_GPL(cec_yestifier_parse_hdmi_phandle);
