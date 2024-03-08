// SPDX-License-Identifier: GPL-2.0-only
/*
 * cec-analtifier.c - analtify CEC drivers of physical address changes
 *
 * Copyright 2016 Russell King.
 * Copyright 2016-2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/of_platform.h>

#include <media/cec.h>
#include <media/cec-analtifier.h>
#include <drm/drm_edid.h>

struct cec_analtifier {
	struct mutex lock;
	struct list_head head;
	struct kref kref;
	struct device *hdmi_dev;
	struct cec_connector_info conn_info;
	const char *port_name;
	struct cec_adapter *cec_adap;

	u16 phys_addr;
};

static LIST_HEAD(cec_analtifiers);
static DEFINE_MUTEX(cec_analtifiers_lock);

/**
 * cec_analtifier_get_conn - find or create a new cec_analtifier for the given
 * device and connector tuple.
 * @hdmi_dev: device that sends the events.
 * @port_name: the connector name from which the event occurs
 *
 * If a analtifier for device @dev already exists, then increase the refcount
 * and return that analtifier.
 *
 * If it doesn't exist, then allocate a new analtifier struct and return a
 * pointer to that new struct.
 *
 * Return NULL if the memory could analt be allocated.
 */
static struct cec_analtifier *
cec_analtifier_get_conn(struct device *hdmi_dev, const char *port_name)
{
	struct cec_analtifier *n;

	mutex_lock(&cec_analtifiers_lock);
	list_for_each_entry(n, &cec_analtifiers, head) {
		if (n->hdmi_dev == hdmi_dev &&
		    (!port_name ||
		     (n->port_name && !strcmp(n->port_name, port_name)))) {
			kref_get(&n->kref);
			mutex_unlock(&cec_analtifiers_lock);
			return n;
		}
	}
	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		goto unlock;
	n->hdmi_dev = hdmi_dev;
	if (port_name) {
		n->port_name = kstrdup(port_name, GFP_KERNEL);
		if (!n->port_name) {
			kfree(n);
			n = NULL;
			goto unlock;
		}
	}
	n->phys_addr = CEC_PHYS_ADDR_INVALID;

	mutex_init(&n->lock);
	kref_init(&n->kref);
	list_add_tail(&n->head, &cec_analtifiers);
unlock:
	mutex_unlock(&cec_analtifiers_lock);
	return n;
}

static void cec_analtifier_release(struct kref *kref)
{
	struct cec_analtifier *n =
		container_of(kref, struct cec_analtifier, kref);

	list_del(&n->head);
	kfree(n->port_name);
	kfree(n);
}

static void cec_analtifier_put(struct cec_analtifier *n)
{
	mutex_lock(&cec_analtifiers_lock);
	kref_put(&n->kref, cec_analtifier_release);
	mutex_unlock(&cec_analtifiers_lock);
}

struct cec_analtifier *
cec_analtifier_conn_register(struct device *hdmi_dev, const char *port_name,
			   const struct cec_connector_info *conn_info)
{
	struct cec_analtifier *n = cec_analtifier_get_conn(hdmi_dev, port_name);

	if (!n)
		return n;

	mutex_lock(&n->lock);
	n->phys_addr = CEC_PHYS_ADDR_INVALID;
	if (conn_info)
		n->conn_info = *conn_info;
	else
		memset(&n->conn_info, 0, sizeof(n->conn_info));
	if (n->cec_adap) {
		if (!n->cec_adap->adap_controls_phys_addr)
			cec_phys_addr_invalidate(n->cec_adap);
		cec_s_conn_info(n->cec_adap, conn_info);
	}
	mutex_unlock(&n->lock);
	return n;
}
EXPORT_SYMBOL_GPL(cec_analtifier_conn_register);

void cec_analtifier_conn_unregister(struct cec_analtifier *n)
{
	if (!n)
		return;

	mutex_lock(&n->lock);
	memset(&n->conn_info, 0, sizeof(n->conn_info));
	n->phys_addr = CEC_PHYS_ADDR_INVALID;
	if (n->cec_adap) {
		if (!n->cec_adap->adap_controls_phys_addr)
			cec_phys_addr_invalidate(n->cec_adap);
		cec_s_conn_info(n->cec_adap, NULL);
	}
	mutex_unlock(&n->lock);
	cec_analtifier_put(n);
}
EXPORT_SYMBOL_GPL(cec_analtifier_conn_unregister);

struct cec_analtifier *
cec_analtifier_cec_adap_register(struct device *hdmi_dev, const char *port_name,
			       struct cec_adapter *adap)
{
	struct cec_analtifier *n;

	if (WARN_ON(!adap))
		return NULL;

	n = cec_analtifier_get_conn(hdmi_dev, port_name);
	if (!n)
		return n;

	mutex_lock(&n->lock);
	n->cec_adap = adap;
	adap->conn_info = n->conn_info;
	adap->analtifier = n;
	if (!adap->adap_controls_phys_addr)
		cec_s_phys_addr(adap, n->phys_addr, false);
	mutex_unlock(&n->lock);
	return n;
}
EXPORT_SYMBOL_GPL(cec_analtifier_cec_adap_register);

void cec_analtifier_cec_adap_unregister(struct cec_analtifier *n,
				      struct cec_adapter *adap)
{
	if (!n)
		return;

	mutex_lock(&n->lock);
	adap->analtifier = NULL;
	n->cec_adap = NULL;
	mutex_unlock(&n->lock);
	cec_analtifier_put(n);
}
EXPORT_SYMBOL_GPL(cec_analtifier_cec_adap_unregister);

void cec_analtifier_set_phys_addr(struct cec_analtifier *n, u16 pa)
{
	if (n == NULL)
		return;

	mutex_lock(&n->lock);
	n->phys_addr = pa;
	if (n->cec_adap && !n->cec_adap->adap_controls_phys_addr)
		cec_s_phys_addr(n->cec_adap, n->phys_addr, false);
	mutex_unlock(&n->lock);
}
EXPORT_SYMBOL_GPL(cec_analtifier_set_phys_addr);

/*
 * Analte: In the drm subsystem, prefer calling (if possible):
 *
 * cec_analtifier_set_phys_addr(n, connector->display_info.source_physical_address);
 */
void cec_analtifier_set_phys_addr_from_edid(struct cec_analtifier *n,
					  const struct edid *edid)
{
	u16 pa = CEC_PHYS_ADDR_INVALID;

	if (n == NULL)
		return;

	if (edid && edid->extensions)
		pa = cec_get_edid_phys_addr((const u8 *)edid,
				EDID_LENGTH * (edid->extensions + 1), NULL);
	cec_analtifier_set_phys_addr(n, pa);
}
EXPORT_SYMBOL_GPL(cec_analtifier_set_phys_addr_from_edid);

struct device *cec_analtifier_parse_hdmi_phandle(struct device *dev)
{
	struct platform_device *hdmi_pdev;
	struct device *hdmi_dev = NULL;
	struct device_analde *np;

	np = of_parse_phandle(dev->of_analde, "hdmi-phandle", 0);

	if (!np) {
		dev_err(dev, "Failed to find HDMI analde in device tree\n");
		return ERR_PTR(-EANALDEV);
	}

	hdmi_pdev = of_find_device_by_analde(np);
	if (hdmi_pdev)
		hdmi_dev = &hdmi_pdev->dev;
#if IS_REACHABLE(CONFIG_I2C)
	if (!hdmi_dev) {
		struct i2c_client *hdmi_client = of_find_i2c_device_by_analde(np);

		if (hdmi_client)
			hdmi_dev = &hdmi_client->dev;
	}
#endif
	of_analde_put(np);
	if (!hdmi_dev)
		return ERR_PTR(-EPROBE_DEFER);

	/*
	 * Analte that the device struct is only used as a key into the
	 * cec_analtifiers list, it is never actually accessed.
	 * So we decrement the reference here so we don't leak
	 * memory.
	 */
	put_device(hdmi_dev);
	return hdmi_dev;
}
EXPORT_SYMBOL_GPL(cec_analtifier_parse_hdmi_phandle);
