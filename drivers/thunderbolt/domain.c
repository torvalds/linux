// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt bus support
 *
 * Copyright (C) 2017, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <crypto/hash.h>

#include "tb.h"

static DEFINE_IDA(tb_domain_ida);

static bool match_service_id(const struct tb_service_id *id,
			     const struct tb_service *svc)
{
	if (id->match_flags & TBSVC_MATCH_PROTOCOL_KEY) {
		if (strcmp(id->protocol_key, svc->key))
			return false;
	}

	if (id->match_flags & TBSVC_MATCH_PROTOCOL_ID) {
		if (id->protocol_id != svc->prtcid)
			return false;
	}

	if (id->match_flags & TBSVC_MATCH_PROTOCOL_VERSION) {
		if (id->protocol_version != svc->prtcvers)
			return false;
	}

	if (id->match_flags & TBSVC_MATCH_PROTOCOL_VERSION) {
		if (id->protocol_revision != svc->prtcrevs)
			return false;
	}

	return true;
}

static const struct tb_service_id *__tb_service_match(struct device *dev,
						      const struct device_driver *drv)
{
	const struct tb_service_driver *driver;
	const struct tb_service_id *ids;
	struct tb_service *svc;

	svc = tb_to_service(dev);
	if (!svc)
		return NULL;

	driver = container_of_const(drv, struct tb_service_driver, driver);
	if (!driver->id_table)
		return NULL;

	for (ids = driver->id_table; ids->match_flags != 0; ids++) {
		if (match_service_id(ids, svc))
			return ids;
	}

	return NULL;
}

static int tb_service_match(struct device *dev, const struct device_driver *drv)
{
	return !!__tb_service_match(dev, drv);
}

static int tb_service_probe(struct device *dev)
{
	struct tb_service *svc = tb_to_service(dev);
	struct tb_service_driver *driver;
	const struct tb_service_id *id;

	driver = container_of(dev->driver, struct tb_service_driver, driver);
	id = __tb_service_match(dev, &driver->driver);

	return driver->probe(svc, id);
}

static void tb_service_remove(struct device *dev)
{
	struct tb_service *svc = tb_to_service(dev);
	struct tb_service_driver *driver;

	driver = container_of(dev->driver, struct tb_service_driver, driver);
	if (driver->remove)
		driver->remove(svc);
}

static void tb_service_shutdown(struct device *dev)
{
	struct tb_service_driver *driver;
	struct tb_service *svc;

	svc = tb_to_service(dev);
	if (!svc || !dev->driver)
		return;

	driver = container_of(dev->driver, struct tb_service_driver, driver);
	if (driver->shutdown)
		driver->shutdown(svc);
}

static const char * const tb_security_names[] = {
	[TB_SECURITY_NONE] = "none",
	[TB_SECURITY_USER] = "user",
	[TB_SECURITY_SECURE] = "secure",
	[TB_SECURITY_DPONLY] = "dponly",
	[TB_SECURITY_USBONLY] = "usbonly",
	[TB_SECURITY_NOPCIE] = "nopcie",
};

static ssize_t boot_acl_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tb *tb = container_of(dev, struct tb, dev);
	uuid_t *uuids;
	ssize_t ret;
	int i;

	uuids = kcalloc(tb->nboot_acl, sizeof(uuid_t), GFP_KERNEL);
	if (!uuids)
		return -ENOMEM;

	pm_runtime_get_sync(&tb->dev);

	if (mutex_lock_interruptible(&tb->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}
	ret = tb->cm_ops->get_boot_acl(tb, uuids, tb->nboot_acl);
	if (ret) {
		mutex_unlock(&tb->lock);
		goto out;
	}
	mutex_unlock(&tb->lock);

	for (ret = 0, i = 0; i < tb->nboot_acl; i++) {
		if (!uuid_is_null(&uuids[i]))
			ret += sysfs_emit_at(buf, ret, "%pUb", &uuids[i]);

		ret += sysfs_emit_at(buf, ret, "%s", i < tb->nboot_acl - 1 ? "," : "\n");
	}

out:
	pm_runtime_mark_last_busy(&tb->dev);
	pm_runtime_put_autosuspend(&tb->dev);
	kfree(uuids);

	return ret;
}

static ssize_t boot_acl_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct tb *tb = container_of(dev, struct tb, dev);
	char *str, *s, *uuid_str;
	ssize_t ret = 0;
	uuid_t *acl;
	int i = 0;

	/*
	 * Make sure the value is not bigger than tb->nboot_acl * UUID
	 * length + commas and optional "\n". Also the smallest allowable
	 * string is tb->nboot_acl * ",".
	 */
	if (count > (UUID_STRING_LEN + 1) * tb->nboot_acl + 1)
		return -EINVAL;
	if (count < tb->nboot_acl - 1)
		return -EINVAL;

	str = kstrdup(buf, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	acl = kcalloc(tb->nboot_acl, sizeof(uuid_t), GFP_KERNEL);
	if (!acl) {
		ret = -ENOMEM;
		goto err_free_str;
	}

	uuid_str = strim(str);
	while ((s = strsep(&uuid_str, ",")) != NULL && i < tb->nboot_acl) {
		size_t len = strlen(s);

		if (len) {
			if (len != UUID_STRING_LEN) {
				ret = -EINVAL;
				goto err_free_acl;
			}
			ret = uuid_parse(s, &acl[i]);
			if (ret)
				goto err_free_acl;
		}

		i++;
	}

	if (s || i < tb->nboot_acl) {
		ret = -EINVAL;
		goto err_free_acl;
	}

	pm_runtime_get_sync(&tb->dev);

	if (mutex_lock_interruptible(&tb->lock)) {
		ret = -ERESTARTSYS;
		goto err_rpm_put;
	}
	ret = tb->cm_ops->set_boot_acl(tb, acl, tb->nboot_acl);
	if (!ret) {
		/* Notify userspace about the change */
		kobject_uevent(&tb->dev.kobj, KOBJ_CHANGE);
	}
	mutex_unlock(&tb->lock);

err_rpm_put:
	pm_runtime_mark_last_busy(&tb->dev);
	pm_runtime_put_autosuspend(&tb->dev);
err_free_acl:
	kfree(acl);
err_free_str:
	kfree(str);

	return ret ?: count;
}
static DEVICE_ATTR_RW(boot_acl);

static ssize_t deauthorization_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	const struct tb *tb = container_of(dev, struct tb, dev);
	bool deauthorization = false;

	/* Only meaningful if authorization is supported */
	if (tb->security_level == TB_SECURITY_USER ||
	    tb->security_level == TB_SECURITY_SECURE)
		deauthorization = !!tb->cm_ops->disapprove_switch;

	return sysfs_emit(buf, "%d\n", deauthorization);
}
static DEVICE_ATTR_RO(deauthorization);

static ssize_t iommu_dma_protection_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tb *tb = container_of(dev, struct tb, dev);

	return sysfs_emit(buf, "%d\n", tb->nhi->iommu_dma_protection);
}
static DEVICE_ATTR_RO(iommu_dma_protection);

static ssize_t security_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tb *tb = container_of(dev, struct tb, dev);
	const char *name = "unknown";

	if (tb->security_level < ARRAY_SIZE(tb_security_names))
		name = tb_security_names[tb->security_level];

	return sysfs_emit(buf, "%s\n", name);
}
static DEVICE_ATTR_RO(security);

static struct attribute *domain_attrs[] = {
	&dev_attr_boot_acl.attr,
	&dev_attr_deauthorization.attr,
	&dev_attr_iommu_dma_protection.attr,
	&dev_attr_security.attr,
	NULL,
};

static umode_t domain_attr_is_visible(struct kobject *kobj,
				      struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct tb *tb = container_of(dev, struct tb, dev);

	if (attr == &dev_attr_boot_acl.attr) {
		if (tb->nboot_acl &&
		    tb->cm_ops->get_boot_acl &&
		    tb->cm_ops->set_boot_acl)
			return attr->mode;
		return 0;
	}

	return attr->mode;
}

static const struct attribute_group domain_attr_group = {
	.is_visible = domain_attr_is_visible,
	.attrs = domain_attrs,
};

static const struct attribute_group *domain_attr_groups[] = {
	&domain_attr_group,
	NULL,
};

const struct bus_type tb_bus_type = {
	.name = "thunderbolt",
	.match = tb_service_match,
	.probe = tb_service_probe,
	.remove = tb_service_remove,
	.shutdown = tb_service_shutdown,
};

static void tb_domain_release(struct device *dev)
{
	struct tb *tb = container_of(dev, struct tb, dev);

	tb_ctl_free(tb->ctl);
	destroy_workqueue(tb->wq);
	ida_free(&tb_domain_ida, tb->index);
	mutex_destroy(&tb->lock);
	kfree(tb);
}

const struct device_type tb_domain_type = {
	.name = "thunderbolt_domain",
	.release = tb_domain_release,
};

static bool tb_domain_event_cb(void *data, enum tb_cfg_pkg_type type,
			       const void *buf, size_t size)
{
	struct tb *tb = data;

	if (!tb->cm_ops->handle_event) {
		tb_warn(tb, "domain does not have event handler\n");
		return true;
	}

	switch (type) {
	case TB_CFG_PKG_XDOMAIN_REQ:
	case TB_CFG_PKG_XDOMAIN_RESP:
		if (tb_is_xdomain_enabled())
			return tb_xdomain_handle_request(tb, type, buf, size);
		break;

	default:
		tb->cm_ops->handle_event(tb, type, buf, size);
	}

	return true;
}

/**
 * tb_domain_alloc() - Allocate a domain
 * @nhi: Pointer to the host controller
 * @timeout_msec: Control channel timeout for non-raw messages
 * @privsize: Size of the connection manager private data
 *
 * Allocates and initializes a new Thunderbolt domain. Connection
 * managers are expected to call this and then fill in @cm_ops
 * accordingly.
 *
 * Call tb_domain_put() to release the domain before it has been added
 * to the system.
 *
 * Return: allocated domain structure on %NULL in case of error
 */
struct tb *tb_domain_alloc(struct tb_nhi *nhi, int timeout_msec, size_t privsize)
{
	struct tb *tb;

	/*
	 * Make sure the structure sizes map with that the hardware
	 * expects because bit-fields are being used.
	 */
	BUILD_BUG_ON(sizeof(struct tb_regs_switch_header) != 5 * 4);
	BUILD_BUG_ON(sizeof(struct tb_regs_port_header) != 8 * 4);
	BUILD_BUG_ON(sizeof(struct tb_regs_hop) != 2 * 4);

	tb = kzalloc(sizeof(*tb) + privsize, GFP_KERNEL);
	if (!tb)
		return NULL;

	tb->nhi = nhi;
	mutex_init(&tb->lock);

	tb->index = ida_alloc(&tb_domain_ida, GFP_KERNEL);
	if (tb->index < 0)
		goto err_free;

	tb->wq = alloc_ordered_workqueue("thunderbolt%d", 0, tb->index);
	if (!tb->wq)
		goto err_remove_ida;

	tb->ctl = tb_ctl_alloc(nhi, tb->index, timeout_msec, tb_domain_event_cb, tb);
	if (!tb->ctl)
		goto err_destroy_wq;

	tb->dev.parent = &nhi->pdev->dev;
	tb->dev.bus = &tb_bus_type;
	tb->dev.type = &tb_domain_type;
	tb->dev.groups = domain_attr_groups;
	dev_set_name(&tb->dev, "domain%d", tb->index);
	device_initialize(&tb->dev);

	return tb;

err_destroy_wq:
	destroy_workqueue(tb->wq);
err_remove_ida:
	ida_free(&tb_domain_ida, tb->index);
err_free:
	kfree(tb);

	return NULL;
}

/**
 * tb_domain_add() - Add domain to the system
 * @tb: Domain to add
 * @reset: Issue reset to the host router
 *
 * Starts the domain and adds it to the system. Hotplugging devices will
 * work after this has been returned successfully. In order to remove
 * and release the domain after this function has been called, call
 * tb_domain_remove().
 *
 * Return: %0 in case of success and negative errno in case of error
 */
int tb_domain_add(struct tb *tb, bool reset)
{
	int ret;

	if (WARN_ON(!tb->cm_ops))
		return -EINVAL;

	mutex_lock(&tb->lock);
	/*
	 * tb_schedule_hotplug_handler may be called as soon as the config
	 * channel is started. Thats why we have to hold the lock here.
	 */
	tb_ctl_start(tb->ctl);

	if (tb->cm_ops->driver_ready) {
		ret = tb->cm_ops->driver_ready(tb);
		if (ret)
			goto err_ctl_stop;
	}

	tb_dbg(tb, "security level set to %s\n",
	       tb_security_names[tb->security_level]);

	ret = device_add(&tb->dev);
	if (ret)
		goto err_ctl_stop;

	/* Start the domain */
	if (tb->cm_ops->start) {
		ret = tb->cm_ops->start(tb, reset);
		if (ret)
			goto err_domain_del;
	}

	/* This starts event processing */
	mutex_unlock(&tb->lock);

	device_init_wakeup(&tb->dev, true);

	pm_runtime_no_callbacks(&tb->dev);
	pm_runtime_set_active(&tb->dev);
	pm_runtime_enable(&tb->dev);
	pm_runtime_set_autosuspend_delay(&tb->dev, TB_AUTOSUSPEND_DELAY);
	pm_runtime_mark_last_busy(&tb->dev);
	pm_runtime_use_autosuspend(&tb->dev);

	return 0;

err_domain_del:
	device_del(&tb->dev);
err_ctl_stop:
	tb_ctl_stop(tb->ctl);
	mutex_unlock(&tb->lock);

	return ret;
}

/**
 * tb_domain_remove() - Removes and releases a domain
 * @tb: Domain to remove
 *
 * Stops the domain, removes it from the system and releases all
 * resources once the last reference has been released.
 */
void tb_domain_remove(struct tb *tb)
{
	mutex_lock(&tb->lock);
	if (tb->cm_ops->stop)
		tb->cm_ops->stop(tb);
	/* Stop the domain control traffic */
	tb_ctl_stop(tb->ctl);
	mutex_unlock(&tb->lock);

	flush_workqueue(tb->wq);

	if (tb->cm_ops->deinit)
		tb->cm_ops->deinit(tb);

	device_unregister(&tb->dev);
}

/**
 * tb_domain_suspend_noirq() - Suspend a domain
 * @tb: Domain to suspend
 *
 * Suspends all devices in the domain and stops the control channel.
 */
int tb_domain_suspend_noirq(struct tb *tb)
{
	int ret = 0;

	/*
	 * The control channel interrupt is left enabled during suspend
	 * and taking the lock here prevents any events happening before
	 * we actually have stopped the domain and the control channel.
	 */
	mutex_lock(&tb->lock);
	if (tb->cm_ops->suspend_noirq)
		ret = tb->cm_ops->suspend_noirq(tb);
	if (!ret)
		tb_ctl_stop(tb->ctl);
	mutex_unlock(&tb->lock);

	return ret;
}

/**
 * tb_domain_resume_noirq() - Resume a domain
 * @tb: Domain to resume
 *
 * Re-starts the control channel, and resumes all devices connected to
 * the domain.
 */
int tb_domain_resume_noirq(struct tb *tb)
{
	int ret = 0;

	mutex_lock(&tb->lock);
	tb_ctl_start(tb->ctl);
	if (tb->cm_ops->resume_noirq)
		ret = tb->cm_ops->resume_noirq(tb);
	mutex_unlock(&tb->lock);

	return ret;
}

int tb_domain_suspend(struct tb *tb)
{
	return tb->cm_ops->suspend ? tb->cm_ops->suspend(tb) : 0;
}

int tb_domain_freeze_noirq(struct tb *tb)
{
	int ret = 0;

	mutex_lock(&tb->lock);
	if (tb->cm_ops->freeze_noirq)
		ret = tb->cm_ops->freeze_noirq(tb);
	if (!ret)
		tb_ctl_stop(tb->ctl);
	mutex_unlock(&tb->lock);

	return ret;
}

int tb_domain_thaw_noirq(struct tb *tb)
{
	int ret = 0;

	mutex_lock(&tb->lock);
	tb_ctl_start(tb->ctl);
	if (tb->cm_ops->thaw_noirq)
		ret = tb->cm_ops->thaw_noirq(tb);
	mutex_unlock(&tb->lock);

	return ret;
}

void tb_domain_complete(struct tb *tb)
{
	if (tb->cm_ops->complete)
		tb->cm_ops->complete(tb);
}

int tb_domain_runtime_suspend(struct tb *tb)
{
	if (tb->cm_ops->runtime_suspend) {
		int ret = tb->cm_ops->runtime_suspend(tb);
		if (ret)
			return ret;
	}
	tb_ctl_stop(tb->ctl);
	return 0;
}

int tb_domain_runtime_resume(struct tb *tb)
{
	tb_ctl_start(tb->ctl);
	if (tb->cm_ops->runtime_resume) {
		int ret = tb->cm_ops->runtime_resume(tb);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * tb_domain_disapprove_switch() - Disapprove switch
 * @tb: Domain the switch belongs to
 * @sw: Switch to disapprove
 *
 * This will disconnect PCIe tunnel from parent to this @sw.
 *
 * Return: %0 on success and negative errno in case of failure.
 */
int tb_domain_disapprove_switch(struct tb *tb, struct tb_switch *sw)
{
	if (!tb->cm_ops->disapprove_switch)
		return -EPERM;

	return tb->cm_ops->disapprove_switch(tb, sw);
}

/**
 * tb_domain_approve_switch() - Approve switch
 * @tb: Domain the switch belongs to
 * @sw: Switch to approve
 *
 * This will approve switch by connection manager specific means. In
 * case of success the connection manager will create PCIe tunnel from
 * parent to @sw.
 */
int tb_domain_approve_switch(struct tb *tb, struct tb_switch *sw)
{
	struct tb_switch *parent_sw;

	if (!tb->cm_ops->approve_switch)
		return -EPERM;

	/* The parent switch must be authorized before this one */
	parent_sw = tb_to_switch(sw->dev.parent);
	if (!parent_sw || !parent_sw->authorized)
		return -EINVAL;

	return tb->cm_ops->approve_switch(tb, sw);
}

/**
 * tb_domain_approve_switch_key() - Approve switch and add key
 * @tb: Domain the switch belongs to
 * @sw: Switch to approve
 *
 * For switches that support secure connect, this function first adds
 * key to the switch NVM using connection manager specific means. If
 * adding the key is successful, the switch is approved and connected.
 *
 * Return: %0 on success and negative errno in case of failure.
 */
int tb_domain_approve_switch_key(struct tb *tb, struct tb_switch *sw)
{
	struct tb_switch *parent_sw;
	int ret;

	if (!tb->cm_ops->approve_switch || !tb->cm_ops->add_switch_key)
		return -EPERM;

	/* The parent switch must be authorized before this one */
	parent_sw = tb_to_switch(sw->dev.parent);
	if (!parent_sw || !parent_sw->authorized)
		return -EINVAL;

	ret = tb->cm_ops->add_switch_key(tb, sw);
	if (ret)
		return ret;

	return tb->cm_ops->approve_switch(tb, sw);
}

/**
 * tb_domain_challenge_switch_key() - Challenge and approve switch
 * @tb: Domain the switch belongs to
 * @sw: Switch to approve
 *
 * For switches that support secure connect, this function generates
 * random challenge and sends it to the switch. The switch responds to
 * this and if the response matches our random challenge, the switch is
 * approved and connected.
 *
 * Return: %0 on success and negative errno in case of failure.
 */
int tb_domain_challenge_switch_key(struct tb *tb, struct tb_switch *sw)
{
	u8 challenge[TB_SWITCH_KEY_SIZE];
	u8 response[TB_SWITCH_KEY_SIZE];
	u8 hmac[TB_SWITCH_KEY_SIZE];
	struct tb_switch *parent_sw;
	struct crypto_shash *tfm;
	struct shash_desc *shash;
	int ret;

	if (!tb->cm_ops->approve_switch || !tb->cm_ops->challenge_switch_key)
		return -EPERM;

	/* The parent switch must be authorized before this one */
	parent_sw = tb_to_switch(sw->dev.parent);
	if (!parent_sw || !parent_sw->authorized)
		return -EINVAL;

	get_random_bytes(challenge, sizeof(challenge));
	ret = tb->cm_ops->challenge_switch_key(tb, sw, challenge, response);
	if (ret)
		return ret;

	tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	ret = crypto_shash_setkey(tfm, sw->key, TB_SWITCH_KEY_SIZE);
	if (ret)
		goto err_free_tfm;

	shash = kzalloc(sizeof(*shash) + crypto_shash_descsize(tfm),
			GFP_KERNEL);
	if (!shash) {
		ret = -ENOMEM;
		goto err_free_tfm;
	}

	shash->tfm = tfm;

	memset(hmac, 0, sizeof(hmac));
	ret = crypto_shash_digest(shash, challenge, sizeof(hmac), hmac);
	if (ret)
		goto err_free_shash;

	/* The returned HMAC must match the one we calculated */
	if (memcmp(response, hmac, sizeof(hmac))) {
		ret = -EKEYREJECTED;
		goto err_free_shash;
	}

	crypto_free_shash(tfm);
	kfree(shash);

	return tb->cm_ops->approve_switch(tb, sw);

err_free_shash:
	kfree(shash);
err_free_tfm:
	crypto_free_shash(tfm);

	return ret;
}

/**
 * tb_domain_disconnect_pcie_paths() - Disconnect all PCIe paths
 * @tb: Domain whose PCIe paths to disconnect
 *
 * This needs to be called in preparation for NVM upgrade of the host
 * controller. Makes sure all PCIe paths are disconnected.
 *
 * Return %0 on success and negative errno in case of error.
 */
int tb_domain_disconnect_pcie_paths(struct tb *tb)
{
	if (!tb->cm_ops->disconnect_pcie_paths)
		return -EPERM;

	return tb->cm_ops->disconnect_pcie_paths(tb);
}

/**
 * tb_domain_approve_xdomain_paths() - Enable DMA paths for XDomain
 * @tb: Domain enabling the DMA paths
 * @xd: XDomain DMA paths are created to
 * @transmit_path: HopID we are using to send out packets
 * @transmit_ring: DMA ring used to send out packets
 * @receive_path: HopID the other end is using to send packets to us
 * @receive_ring: DMA ring used to receive packets from @receive_path
 *
 * Calls connection manager specific method to enable DMA paths to the
 * XDomain in question.
 *
 * Return: 0% in case of success and negative errno otherwise. In
 * particular returns %-ENOTSUPP if the connection manager
 * implementation does not support XDomains.
 */
int tb_domain_approve_xdomain_paths(struct tb *tb, struct tb_xdomain *xd,
				    int transmit_path, int transmit_ring,
				    int receive_path, int receive_ring)
{
	if (!tb->cm_ops->approve_xdomain_paths)
		return -ENOTSUPP;

	return tb->cm_ops->approve_xdomain_paths(tb, xd, transmit_path,
			transmit_ring, receive_path, receive_ring);
}

/**
 * tb_domain_disconnect_xdomain_paths() - Disable DMA paths for XDomain
 * @tb: Domain disabling the DMA paths
 * @xd: XDomain whose DMA paths are disconnected
 * @transmit_path: HopID we are using to send out packets
 * @transmit_ring: DMA ring used to send out packets
 * @receive_path: HopID the other end is using to send packets to us
 * @receive_ring: DMA ring used to receive packets from @receive_path
 *
 * Calls connection manager specific method to disconnect DMA paths to
 * the XDomain in question.
 *
 * Return: 0% in case of success and negative errno otherwise. In
 * particular returns %-ENOTSUPP if the connection manager
 * implementation does not support XDomains.
 */
int tb_domain_disconnect_xdomain_paths(struct tb *tb, struct tb_xdomain *xd,
				       int transmit_path, int transmit_ring,
				       int receive_path, int receive_ring)
{
	if (!tb->cm_ops->disconnect_xdomain_paths)
		return -ENOTSUPP;

	return tb->cm_ops->disconnect_xdomain_paths(tb, xd, transmit_path,
			transmit_ring, receive_path, receive_ring);
}

static int disconnect_xdomain(struct device *dev, void *data)
{
	struct tb_xdomain *xd;
	struct tb *tb = data;
	int ret = 0;

	xd = tb_to_xdomain(dev);
	if (xd && xd->tb == tb)
		ret = tb_xdomain_disable_all_paths(xd);

	return ret;
}

/**
 * tb_domain_disconnect_all_paths() - Disconnect all paths for the domain
 * @tb: Domain whose paths are disconnected
 *
 * This function can be used to disconnect all paths (PCIe, XDomain) for
 * example in preparation for host NVM firmware upgrade. After this is
 * called the paths cannot be established without resetting the switch.
 *
 * Return: %0 in case of success and negative errno otherwise.
 */
int tb_domain_disconnect_all_paths(struct tb *tb)
{
	int ret;

	ret = tb_domain_disconnect_pcie_paths(tb);
	if (ret)
		return ret;

	return bus_for_each_dev(&tb_bus_type, NULL, tb, disconnect_xdomain);
}

int tb_domain_init(void)
{
	int ret;

	tb_debugfs_init();
	tb_acpi_init();

	ret = tb_xdomain_init();
	if (ret)
		goto err_acpi;
	ret = bus_register(&tb_bus_type);
	if (ret)
		goto err_xdomain;

	return 0;

err_xdomain:
	tb_xdomain_exit();
err_acpi:
	tb_acpi_exit();
	tb_debugfs_exit();

	return ret;
}

void tb_domain_exit(void)
{
	bus_unregister(&tb_bus_type);
	ida_destroy(&tb_domain_ida);
	tb_nvm_exit();
	tb_xdomain_exit();
	tb_acpi_exit();
	tb_debugfs_exit();
}
