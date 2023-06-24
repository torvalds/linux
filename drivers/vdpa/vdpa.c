// SPDX-License-Identifier: GPL-2.0-only
/*
 * vDPA bus.
 *
 * Copyright (c) 2020, Red Hat. All rights reserved.
 *     Author: Jason Wang <jasowang@redhat.com>
 *
 */

#include <linux/module.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/vdpa.h>
#include <uapi/linux/vdpa.h>
#include <net/genetlink.h>
#include <linux/mod_devicetable.h>
#include <linux/virtio_ids.h>

static LIST_HEAD(mdev_head);
/* A global mutex that protects vdpa management device and device level operations. */
static DECLARE_RWSEM(vdpa_dev_lock);
static DEFINE_IDA(vdpa_index_ida);

void vdpa_set_status(struct vdpa_device *vdev, u8 status)
{
	down_write(&vdev->cf_lock);
	vdev->config->set_status(vdev, status);
	up_write(&vdev->cf_lock);
}
EXPORT_SYMBOL(vdpa_set_status);

static struct genl_family vdpa_nl_family;

static int vdpa_dev_probe(struct device *d)
{
	struct vdpa_device *vdev = dev_to_vdpa(d);
	struct vdpa_driver *drv = drv_to_vdpa(vdev->dev.driver);
	const struct vdpa_config_ops *ops = vdev->config;
	u32 max_num, min_num = 1;
	int ret = 0;

	max_num = ops->get_vq_num_max(vdev);
	if (ops->get_vq_num_min)
		min_num = ops->get_vq_num_min(vdev);
	if (max_num < min_num)
		return -EINVAL;

	if (drv && drv->probe)
		ret = drv->probe(vdev);

	return ret;
}

static void vdpa_dev_remove(struct device *d)
{
	struct vdpa_device *vdev = dev_to_vdpa(d);
	struct vdpa_driver *drv = drv_to_vdpa(vdev->dev.driver);

	if (drv && drv->remove)
		drv->remove(vdev);
}

static int vdpa_dev_match(struct device *dev, struct device_driver *drv)
{
	struct vdpa_device *vdev = dev_to_vdpa(dev);

	/* Check override first, and if set, only use the named driver */
	if (vdev->driver_override)
		return strcmp(vdev->driver_override, drv->name) == 0;

	/* Currently devices must be supported by all vDPA bus drivers */
	return 1;
}

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct vdpa_device *vdev = dev_to_vdpa(dev);
	int ret;

	ret = driver_set_override(dev, &vdev->driver_override, buf, count);
	if (ret)
		return ret;

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct vdpa_device *vdev = dev_to_vdpa(dev);
	ssize_t len;

	device_lock(dev);
	len = snprintf(buf, PAGE_SIZE, "%s\n", vdev->driver_override);
	device_unlock(dev);

	return len;
}
static DEVICE_ATTR_RW(driver_override);

static struct attribute *vdpa_dev_attrs[] = {
	&dev_attr_driver_override.attr,
	NULL,
};

static const struct attribute_group vdpa_dev_group = {
	.attrs  = vdpa_dev_attrs,
};
__ATTRIBUTE_GROUPS(vdpa_dev);

static struct bus_type vdpa_bus = {
	.name  = "vdpa",
	.dev_groups = vdpa_dev_groups,
	.match = vdpa_dev_match,
	.probe = vdpa_dev_probe,
	.remove = vdpa_dev_remove,
};

static void vdpa_release_dev(struct device *d)
{
	struct vdpa_device *vdev = dev_to_vdpa(d);
	const struct vdpa_config_ops *ops = vdev->config;

	if (ops->free)
		ops->free(vdev);

	ida_simple_remove(&vdpa_index_ida, vdev->index);
	kfree(vdev->driver_override);
	kfree(vdev);
}

/**
 * __vdpa_alloc_device - allocate and initilaize a vDPA device
 * This allows driver to some prepartion after device is
 * initialized but before registered.
 * @parent: the parent device
 * @config: the bus operations that is supported by this device
 * @ngroups: number of groups supported by this device
 * @nas: number of address spaces supported by this device
 * @size: size of the parent structure that contains private data
 * @name: name of the vdpa device; optional.
 * @use_va: indicate whether virtual address must be used by this device
 *
 * Driver should use vdpa_alloc_device() wrapper macro instead of
 * using this directly.
 *
 * Return: Returns an error when parent/config/dma_dev is not set or fail to get
 *	   ida.
 */
struct vdpa_device *__vdpa_alloc_device(struct device *parent,
					const struct vdpa_config_ops *config,
					unsigned int ngroups, unsigned int nas,
					size_t size, const char *name,
					bool use_va)
{
	struct vdpa_device *vdev;
	int err = -EINVAL;

	if (!config)
		goto err;

	if (!!config->dma_map != !!config->dma_unmap)
		goto err;

	/* It should only work for the device that use on-chip IOMMU */
	if (use_va && !(config->dma_map || config->set_map))
		goto err;

	err = -ENOMEM;
	vdev = kzalloc(size, GFP_KERNEL);
	if (!vdev)
		goto err;

	err = ida_alloc(&vdpa_index_ida, GFP_KERNEL);
	if (err < 0)
		goto err_ida;

	vdev->dev.bus = &vdpa_bus;
	vdev->dev.parent = parent;
	vdev->dev.release = vdpa_release_dev;
	vdev->index = err;
	vdev->config = config;
	vdev->features_valid = false;
	vdev->use_va = use_va;
	vdev->ngroups = ngroups;
	vdev->nas = nas;

	if (name)
		err = dev_set_name(&vdev->dev, "%s", name);
	else
		err = dev_set_name(&vdev->dev, "vdpa%u", vdev->index);
	if (err)
		goto err_name;

	init_rwsem(&vdev->cf_lock);
	device_initialize(&vdev->dev);

	return vdev;

err_name:
	ida_simple_remove(&vdpa_index_ida, vdev->index);
err_ida:
	kfree(vdev);
err:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(__vdpa_alloc_device);

static int vdpa_name_match(struct device *dev, const void *data)
{
	struct vdpa_device *vdev = container_of(dev, struct vdpa_device, dev);

	return (strcmp(dev_name(&vdev->dev), data) == 0);
}

static int __vdpa_register_device(struct vdpa_device *vdev, u32 nvqs)
{
	struct device *dev;

	vdev->nvqs = nvqs;

	lockdep_assert_held(&vdpa_dev_lock);
	dev = bus_find_device(&vdpa_bus, NULL, dev_name(&vdev->dev), vdpa_name_match);
	if (dev) {
		put_device(dev);
		return -EEXIST;
	}
	return device_add(&vdev->dev);
}

/**
 * _vdpa_register_device - register a vDPA device with vdpa lock held
 * Caller must have a succeed call of vdpa_alloc_device() before.
 * Caller must invoke this routine in the management device dev_add()
 * callback after setting up valid mgmtdev for this vdpa device.
 * @vdev: the vdpa device to be registered to vDPA bus
 * @nvqs: number of virtqueues supported by this device
 *
 * Return: Returns an error when fail to add device to vDPA bus
 */
int _vdpa_register_device(struct vdpa_device *vdev, u32 nvqs)
{
	if (!vdev->mdev)
		return -EINVAL;

	return __vdpa_register_device(vdev, nvqs);
}
EXPORT_SYMBOL_GPL(_vdpa_register_device);

/**
 * vdpa_register_device - register a vDPA device
 * Callers must have a succeed call of vdpa_alloc_device() before.
 * @vdev: the vdpa device to be registered to vDPA bus
 * @nvqs: number of virtqueues supported by this device
 *
 * Return: Returns an error when fail to add to vDPA bus
 */
int vdpa_register_device(struct vdpa_device *vdev, u32 nvqs)
{
	int err;

	down_write(&vdpa_dev_lock);
	err = __vdpa_register_device(vdev, nvqs);
	up_write(&vdpa_dev_lock);
	return err;
}
EXPORT_SYMBOL_GPL(vdpa_register_device);

/**
 * _vdpa_unregister_device - unregister a vDPA device
 * Caller must invoke this routine as part of management device dev_del()
 * callback.
 * @vdev: the vdpa device to be unregisted from vDPA bus
 */
void _vdpa_unregister_device(struct vdpa_device *vdev)
{
	lockdep_assert_held(&vdpa_dev_lock);
	WARN_ON(!vdev->mdev);
	device_unregister(&vdev->dev);
}
EXPORT_SYMBOL_GPL(_vdpa_unregister_device);

/**
 * vdpa_unregister_device - unregister a vDPA device
 * @vdev: the vdpa device to be unregisted from vDPA bus
 */
void vdpa_unregister_device(struct vdpa_device *vdev)
{
	down_write(&vdpa_dev_lock);
	device_unregister(&vdev->dev);
	up_write(&vdpa_dev_lock);
}
EXPORT_SYMBOL_GPL(vdpa_unregister_device);

/**
 * __vdpa_register_driver - register a vDPA device driver
 * @drv: the vdpa device driver to be registered
 * @owner: module owner of the driver
 *
 * Return: Returns an err when fail to do the registration
 */
int __vdpa_register_driver(struct vdpa_driver *drv, struct module *owner)
{
	drv->driver.bus = &vdpa_bus;
	drv->driver.owner = owner;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__vdpa_register_driver);

/**
 * vdpa_unregister_driver - unregister a vDPA device driver
 * @drv: the vdpa device driver to be unregistered
 */
void vdpa_unregister_driver(struct vdpa_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(vdpa_unregister_driver);

/**
 * vdpa_mgmtdev_register - register a vdpa management device
 *
 * @mdev: Pointer to vdpa management device
 * vdpa_mgmtdev_register() register a vdpa management device which supports
 * vdpa device management.
 * Return: Returns 0 on success or failure when required callback ops are not
 *         initialized.
 */
int vdpa_mgmtdev_register(struct vdpa_mgmt_dev *mdev)
{
	if (!mdev->device || !mdev->ops || !mdev->ops->dev_add || !mdev->ops->dev_del)
		return -EINVAL;

	INIT_LIST_HEAD(&mdev->list);
	down_write(&vdpa_dev_lock);
	list_add_tail(&mdev->list, &mdev_head);
	up_write(&vdpa_dev_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(vdpa_mgmtdev_register);

static int vdpa_match_remove(struct device *dev, void *data)
{
	struct vdpa_device *vdev = container_of(dev, struct vdpa_device, dev);
	struct vdpa_mgmt_dev *mdev = vdev->mdev;

	if (mdev == data)
		mdev->ops->dev_del(mdev, vdev);
	return 0;
}

void vdpa_mgmtdev_unregister(struct vdpa_mgmt_dev *mdev)
{
	down_write(&vdpa_dev_lock);

	list_del(&mdev->list);

	/* Filter out all the entries belong to this management device and delete it. */
	bus_for_each_dev(&vdpa_bus, NULL, mdev, vdpa_match_remove);

	up_write(&vdpa_dev_lock);
}
EXPORT_SYMBOL_GPL(vdpa_mgmtdev_unregister);

static void vdpa_get_config_unlocked(struct vdpa_device *vdev,
				     unsigned int offset,
				     void *buf, unsigned int len)
{
	const struct vdpa_config_ops *ops = vdev->config;

	/*
	 * Config accesses aren't supposed to trigger before features are set.
	 * If it does happen we assume a legacy guest.
	 */
	if (!vdev->features_valid)
		vdpa_set_features_unlocked(vdev, 0);
	ops->get_config(vdev, offset, buf, len);
}

/**
 * vdpa_get_config - Get one or more device configuration fields.
 * @vdev: vdpa device to operate on
 * @offset: starting byte offset of the field
 * @buf: buffer pointer to read to
 * @len: length of the configuration fields in bytes
 */
void vdpa_get_config(struct vdpa_device *vdev, unsigned int offset,
		     void *buf, unsigned int len)
{
	down_read(&vdev->cf_lock);
	vdpa_get_config_unlocked(vdev, offset, buf, len);
	up_read(&vdev->cf_lock);
}
EXPORT_SYMBOL_GPL(vdpa_get_config);

/**
 * vdpa_set_config - Set one or more device configuration fields.
 * @vdev: vdpa device to operate on
 * @offset: starting byte offset of the field
 * @buf: buffer pointer to read from
 * @length: length of the configuration fields in bytes
 */
void vdpa_set_config(struct vdpa_device *vdev, unsigned int offset,
		     const void *buf, unsigned int length)
{
	down_write(&vdev->cf_lock);
	vdev->config->set_config(vdev, offset, buf, length);
	up_write(&vdev->cf_lock);
}
EXPORT_SYMBOL_GPL(vdpa_set_config);

static bool mgmtdev_handle_match(const struct vdpa_mgmt_dev *mdev,
				 const char *busname, const char *devname)
{
	/* Bus name is optional for simulated management device, so ignore the
	 * device with bus if bus attribute is provided.
	 */
	if ((busname && !mdev->device->bus) || (!busname && mdev->device->bus))
		return false;

	if (!busname && strcmp(dev_name(mdev->device), devname) == 0)
		return true;

	if (busname && (strcmp(mdev->device->bus->name, busname) == 0) &&
	    (strcmp(dev_name(mdev->device), devname) == 0))
		return true;

	return false;
}

static struct vdpa_mgmt_dev *vdpa_mgmtdev_get_from_attr(struct nlattr **attrs)
{
	struct vdpa_mgmt_dev *mdev;
	const char *busname = NULL;
	const char *devname;

	if (!attrs[VDPA_ATTR_MGMTDEV_DEV_NAME])
		return ERR_PTR(-EINVAL);
	devname = nla_data(attrs[VDPA_ATTR_MGMTDEV_DEV_NAME]);
	if (attrs[VDPA_ATTR_MGMTDEV_BUS_NAME])
		busname = nla_data(attrs[VDPA_ATTR_MGMTDEV_BUS_NAME]);

	list_for_each_entry(mdev, &mdev_head, list) {
		if (mgmtdev_handle_match(mdev, busname, devname))
			return mdev;
	}
	return ERR_PTR(-ENODEV);
}

static int vdpa_nl_mgmtdev_handle_fill(struct sk_buff *msg, const struct vdpa_mgmt_dev *mdev)
{
	if (mdev->device->bus &&
	    nla_put_string(msg, VDPA_ATTR_MGMTDEV_BUS_NAME, mdev->device->bus->name))
		return -EMSGSIZE;
	if (nla_put_string(msg, VDPA_ATTR_MGMTDEV_DEV_NAME, dev_name(mdev->device)))
		return -EMSGSIZE;
	return 0;
}

static int vdpa_mgmtdev_fill(const struct vdpa_mgmt_dev *mdev, struct sk_buff *msg,
			     u32 portid, u32 seq, int flags)
{
	u64 supported_classes = 0;
	void *hdr;
	int i = 0;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &vdpa_nl_family, flags, VDPA_CMD_MGMTDEV_NEW);
	if (!hdr)
		return -EMSGSIZE;
	err = vdpa_nl_mgmtdev_handle_fill(msg, mdev);
	if (err)
		goto msg_err;

	while (mdev->id_table[i].device) {
		if (mdev->id_table[i].device <= 63)
			supported_classes |= BIT_ULL(mdev->id_table[i].device);
		i++;
	}

	if (nla_put_u64_64bit(msg, VDPA_ATTR_MGMTDEV_SUPPORTED_CLASSES,
			      supported_classes, VDPA_ATTR_UNSPEC)) {
		err = -EMSGSIZE;
		goto msg_err;
	}
	if (nla_put_u32(msg, VDPA_ATTR_DEV_MGMTDEV_MAX_VQS,
			mdev->max_supported_vqs)) {
		err = -EMSGSIZE;
		goto msg_err;
	}
	if (nla_put_u64_64bit(msg, VDPA_ATTR_DEV_SUPPORTED_FEATURES,
			      mdev->supported_features, VDPA_ATTR_PAD)) {
		err = -EMSGSIZE;
		goto msg_err;
	}

	genlmsg_end(msg, hdr);
	return 0;

msg_err:
	genlmsg_cancel(msg, hdr);
	return err;
}

static int vdpa_nl_cmd_mgmtdev_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct vdpa_mgmt_dev *mdev;
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	down_read(&vdpa_dev_lock);
	mdev = vdpa_mgmtdev_get_from_attr(info->attrs);
	if (IS_ERR(mdev)) {
		up_read(&vdpa_dev_lock);
		NL_SET_ERR_MSG_MOD(info->extack, "Fail to find the specified mgmt device");
		err = PTR_ERR(mdev);
		goto out;
	}

	err = vdpa_mgmtdev_fill(mdev, msg, info->snd_portid, info->snd_seq, 0);
	up_read(&vdpa_dev_lock);
	if (err)
		goto out;
	err = genlmsg_reply(msg, info);
	return err;

out:
	nlmsg_free(msg);
	return err;
}

static int
vdpa_nl_cmd_mgmtdev_get_dumpit(struct sk_buff *msg, struct netlink_callback *cb)
{
	struct vdpa_mgmt_dev *mdev;
	int start = cb->args[0];
	int idx = 0;
	int err;

	down_read(&vdpa_dev_lock);
	list_for_each_entry(mdev, &mdev_head, list) {
		if (idx < start) {
			idx++;
			continue;
		}
		err = vdpa_mgmtdev_fill(mdev, msg, NETLINK_CB(cb->skb).portid,
					cb->nlh->nlmsg_seq, NLM_F_MULTI);
		if (err)
			goto out;
		idx++;
	}
out:
	up_read(&vdpa_dev_lock);
	cb->args[0] = idx;
	return msg->len;
}

#define VDPA_DEV_NET_ATTRS_MASK (BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MACADDR) | \
				 BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MTU)     | \
				 BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MAX_VQP))

static int vdpa_nl_cmd_dev_add_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct vdpa_dev_set_config config = {};
	struct nlattr **nl_attrs = info->attrs;
	struct vdpa_mgmt_dev *mdev;
	const u8 *macaddr;
	const char *name;
	int err = 0;

	if (!info->attrs[VDPA_ATTR_DEV_NAME])
		return -EINVAL;

	name = nla_data(info->attrs[VDPA_ATTR_DEV_NAME]);

	if (nl_attrs[VDPA_ATTR_DEV_NET_CFG_MACADDR]) {
		macaddr = nla_data(nl_attrs[VDPA_ATTR_DEV_NET_CFG_MACADDR]);
		memcpy(config.net.mac, macaddr, sizeof(config.net.mac));
		config.mask |= BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MACADDR);
	}
	if (nl_attrs[VDPA_ATTR_DEV_NET_CFG_MTU]) {
		config.net.mtu =
			nla_get_u16(nl_attrs[VDPA_ATTR_DEV_NET_CFG_MTU]);
		config.mask |= BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MTU);
	}
	if (nl_attrs[VDPA_ATTR_DEV_NET_CFG_MAX_VQP]) {
		config.net.max_vq_pairs =
			nla_get_u16(nl_attrs[VDPA_ATTR_DEV_NET_CFG_MAX_VQP]);
		if (!config.net.max_vq_pairs) {
			NL_SET_ERR_MSG_MOD(info->extack,
					   "At least one pair of VQs is required");
			return -EINVAL;
		}
		config.mask |= BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MAX_VQP);
	}
	if (nl_attrs[VDPA_ATTR_DEV_FEATURES]) {
		config.device_features =
			nla_get_u64(nl_attrs[VDPA_ATTR_DEV_FEATURES]);
		config.mask |= BIT_ULL(VDPA_ATTR_DEV_FEATURES);
	}

	/* Skip checking capability if user didn't prefer to configure any
	 * device networking attributes. It is likely that user might have used
	 * a device specific method to configure such attributes or using device
	 * default attributes.
	 */
	if ((config.mask & VDPA_DEV_NET_ATTRS_MASK) &&
	    !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	down_write(&vdpa_dev_lock);
	mdev = vdpa_mgmtdev_get_from_attr(info->attrs);
	if (IS_ERR(mdev)) {
		NL_SET_ERR_MSG_MOD(info->extack, "Fail to find the specified management device");
		err = PTR_ERR(mdev);
		goto err;
	}
	if ((config.mask & mdev->config_attr_mask) != config.mask) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "All provided attributes are not supported");
		err = -EOPNOTSUPP;
		goto err;
	}

	err = mdev->ops->dev_add(mdev, name, &config);
err:
	up_write(&vdpa_dev_lock);
	return err;
}

static int vdpa_nl_cmd_dev_del_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct vdpa_mgmt_dev *mdev;
	struct vdpa_device *vdev;
	struct device *dev;
	const char *name;
	int err = 0;

	if (!info->attrs[VDPA_ATTR_DEV_NAME])
		return -EINVAL;
	name = nla_data(info->attrs[VDPA_ATTR_DEV_NAME]);

	down_write(&vdpa_dev_lock);
	dev = bus_find_device(&vdpa_bus, NULL, name, vdpa_name_match);
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "device not found");
		err = -ENODEV;
		goto dev_err;
	}
	vdev = container_of(dev, struct vdpa_device, dev);
	if (!vdev->mdev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Only user created device can be deleted by user");
		err = -EINVAL;
		goto mdev_err;
	}
	mdev = vdev->mdev;
	mdev->ops->dev_del(mdev, vdev);
mdev_err:
	put_device(dev);
dev_err:
	up_write(&vdpa_dev_lock);
	return err;
}

static int
vdpa_dev_fill(struct vdpa_device *vdev, struct sk_buff *msg, u32 portid, u32 seq,
	      int flags, struct netlink_ext_ack *extack)
{
	u16 max_vq_size;
	u16 min_vq_size = 1;
	u32 device_id;
	u32 vendor_id;
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &vdpa_nl_family, flags, VDPA_CMD_DEV_NEW);
	if (!hdr)
		return -EMSGSIZE;

	err = vdpa_nl_mgmtdev_handle_fill(msg, vdev->mdev);
	if (err)
		goto msg_err;

	device_id = vdev->config->get_device_id(vdev);
	vendor_id = vdev->config->get_vendor_id(vdev);
	max_vq_size = vdev->config->get_vq_num_max(vdev);
	if (vdev->config->get_vq_num_min)
		min_vq_size = vdev->config->get_vq_num_min(vdev);

	err = -EMSGSIZE;
	if (nla_put_string(msg, VDPA_ATTR_DEV_NAME, dev_name(&vdev->dev)))
		goto msg_err;
	if (nla_put_u32(msg, VDPA_ATTR_DEV_ID, device_id))
		goto msg_err;
	if (nla_put_u32(msg, VDPA_ATTR_DEV_VENDOR_ID, vendor_id))
		goto msg_err;
	if (nla_put_u32(msg, VDPA_ATTR_DEV_MAX_VQS, vdev->nvqs))
		goto msg_err;
	if (nla_put_u16(msg, VDPA_ATTR_DEV_MAX_VQ_SIZE, max_vq_size))
		goto msg_err;
	if (nla_put_u16(msg, VDPA_ATTR_DEV_MIN_VQ_SIZE, min_vq_size))
		goto msg_err;

	genlmsg_end(msg, hdr);
	return 0;

msg_err:
	genlmsg_cancel(msg, hdr);
	return err;
}

static int vdpa_nl_cmd_dev_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct vdpa_device *vdev;
	struct sk_buff *msg;
	const char *devname;
	struct device *dev;
	int err;

	if (!info->attrs[VDPA_ATTR_DEV_NAME])
		return -EINVAL;
	devname = nla_data(info->attrs[VDPA_ATTR_DEV_NAME]);
	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	down_read(&vdpa_dev_lock);
	dev = bus_find_device(&vdpa_bus, NULL, devname, vdpa_name_match);
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "device not found");
		err = -ENODEV;
		goto err;
	}
	vdev = container_of(dev, struct vdpa_device, dev);
	if (!vdev->mdev) {
		err = -EINVAL;
		goto mdev_err;
	}
	err = vdpa_dev_fill(vdev, msg, info->snd_portid, info->snd_seq, 0, info->extack);
	if (err)
		goto mdev_err;

	err = genlmsg_reply(msg, info);
	put_device(dev);
	up_read(&vdpa_dev_lock);
	return err;

mdev_err:
	put_device(dev);
err:
	up_read(&vdpa_dev_lock);
	nlmsg_free(msg);
	return err;
}

struct vdpa_dev_dump_info {
	struct sk_buff *msg;
	struct netlink_callback *cb;
	int start_idx;
	int idx;
};

static int vdpa_dev_dump(struct device *dev, void *data)
{
	struct vdpa_device *vdev = container_of(dev, struct vdpa_device, dev);
	struct vdpa_dev_dump_info *info = data;
	int err;

	if (!vdev->mdev)
		return 0;
	if (info->idx < info->start_idx) {
		info->idx++;
		return 0;
	}
	err = vdpa_dev_fill(vdev, info->msg, NETLINK_CB(info->cb->skb).portid,
			    info->cb->nlh->nlmsg_seq, NLM_F_MULTI, info->cb->extack);
	if (err)
		return err;

	info->idx++;
	return 0;
}

static int vdpa_nl_cmd_dev_get_dumpit(struct sk_buff *msg, struct netlink_callback *cb)
{
	struct vdpa_dev_dump_info info;

	info.msg = msg;
	info.cb = cb;
	info.start_idx = cb->args[0];
	info.idx = 0;

	down_read(&vdpa_dev_lock);
	bus_for_each_dev(&vdpa_bus, NULL, &info, vdpa_dev_dump);
	up_read(&vdpa_dev_lock);
	cb->args[0] = info.idx;
	return msg->len;
}

static int vdpa_dev_net_mq_config_fill(struct sk_buff *msg, u64 features,
				       const struct virtio_net_config *config)
{
	u16 val_u16;

	if ((features & BIT_ULL(VIRTIO_NET_F_MQ)) == 0 &&
	    (features & BIT_ULL(VIRTIO_NET_F_RSS)) == 0)
		return 0;

	val_u16 = __virtio16_to_cpu(true, config->max_virtqueue_pairs);

	return nla_put_u16(msg, VDPA_ATTR_DEV_NET_CFG_MAX_VQP, val_u16);
}

static int vdpa_dev_net_mtu_config_fill(struct sk_buff *msg, u64 features,
					const struct virtio_net_config *config)
{
	u16 val_u16;

	if ((features & BIT_ULL(VIRTIO_NET_F_MTU)) == 0)
		return 0;

	val_u16 = __virtio16_to_cpu(true, config->mtu);

	return nla_put_u16(msg, VDPA_ATTR_DEV_NET_CFG_MTU, val_u16);
}

static int vdpa_dev_net_mac_config_fill(struct sk_buff *msg, u64 features,
					const struct virtio_net_config *config)
{
	if ((features & BIT_ULL(VIRTIO_NET_F_MAC)) == 0)
		return 0;

	return  nla_put(msg, VDPA_ATTR_DEV_NET_CFG_MACADDR,
			sizeof(config->mac), config->mac);
}

static int vdpa_dev_net_config_fill(struct vdpa_device *vdev, struct sk_buff *msg)
{
	struct virtio_net_config config = {};
	u64 features_device;
	u16 val_u16;

	vdev->config->get_config(vdev, 0, &config, sizeof(config));

	val_u16 = __virtio16_to_cpu(true, config.status);
	if (nla_put_u16(msg, VDPA_ATTR_DEV_NET_STATUS, val_u16))
		return -EMSGSIZE;

	features_device = vdev->config->get_device_features(vdev);

	if (nla_put_u64_64bit(msg, VDPA_ATTR_VDPA_DEV_SUPPORTED_FEATURES, features_device,
			      VDPA_ATTR_PAD))
		return -EMSGSIZE;

	if (vdpa_dev_net_mtu_config_fill(msg, features_device, &config))
		return -EMSGSIZE;

	if (vdpa_dev_net_mac_config_fill(msg, features_device, &config))
		return -EMSGSIZE;

	return vdpa_dev_net_mq_config_fill(msg, features_device, &config);
}

static int
vdpa_dev_config_fill(struct vdpa_device *vdev, struct sk_buff *msg, u32 portid, u32 seq,
		     int flags, struct netlink_ext_ack *extack)
{
	u64 features_driver;
	u8 status = 0;
	u32 device_id;
	void *hdr;
	int err;

	down_read(&vdev->cf_lock);
	hdr = genlmsg_put(msg, portid, seq, &vdpa_nl_family, flags,
			  VDPA_CMD_DEV_CONFIG_GET);
	if (!hdr) {
		err = -EMSGSIZE;
		goto out;
	}

	if (nla_put_string(msg, VDPA_ATTR_DEV_NAME, dev_name(&vdev->dev))) {
		err = -EMSGSIZE;
		goto msg_err;
	}

	device_id = vdev->config->get_device_id(vdev);
	if (nla_put_u32(msg, VDPA_ATTR_DEV_ID, device_id)) {
		err = -EMSGSIZE;
		goto msg_err;
	}

	/* only read driver features after the feature negotiation is done */
	status = vdev->config->get_status(vdev);
	if (status & VIRTIO_CONFIG_S_FEATURES_OK) {
		features_driver = vdev->config->get_driver_features(vdev);
		if (nla_put_u64_64bit(msg, VDPA_ATTR_DEV_NEGOTIATED_FEATURES, features_driver,
				      VDPA_ATTR_PAD)) {
			err = -EMSGSIZE;
			goto msg_err;
		}
	}

	switch (device_id) {
	case VIRTIO_ID_NET:
		err = vdpa_dev_net_config_fill(vdev, msg);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	if (err)
		goto msg_err;

	up_read(&vdev->cf_lock);
	genlmsg_end(msg, hdr);
	return 0;

msg_err:
	genlmsg_cancel(msg, hdr);
out:
	up_read(&vdev->cf_lock);
	return err;
}

static int vdpa_fill_stats_rec(struct vdpa_device *vdev, struct sk_buff *msg,
			       struct genl_info *info, u32 index)
{
	struct virtio_net_config config = {};
	u64 features;
	u16 max_vqp;
	u8 status;
	int err;

	status = vdev->config->get_status(vdev);
	if (!(status & VIRTIO_CONFIG_S_FEATURES_OK)) {
		NL_SET_ERR_MSG_MOD(info->extack, "feature negotiation not complete");
		return -EAGAIN;
	}
	vdpa_get_config_unlocked(vdev, 0, &config, sizeof(config));

	max_vqp = __virtio16_to_cpu(true, config.max_virtqueue_pairs);
	if (nla_put_u16(msg, VDPA_ATTR_DEV_NET_CFG_MAX_VQP, max_vqp))
		return -EMSGSIZE;

	features = vdev->config->get_driver_features(vdev);
	if (nla_put_u64_64bit(msg, VDPA_ATTR_DEV_NEGOTIATED_FEATURES,
			      features, VDPA_ATTR_PAD))
		return -EMSGSIZE;

	if (nla_put_u32(msg, VDPA_ATTR_DEV_QUEUE_INDEX, index))
		return -EMSGSIZE;

	err = vdev->config->get_vendor_vq_stats(vdev, index, msg, info->extack);
	if (err)
		return err;

	return 0;
}

static int vendor_stats_fill(struct vdpa_device *vdev, struct sk_buff *msg,
			     struct genl_info *info, u32 index)
{
	int err;

	down_read(&vdev->cf_lock);
	if (!vdev->config->get_vendor_vq_stats) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = vdpa_fill_stats_rec(vdev, msg, info, index);
out:
	up_read(&vdev->cf_lock);
	return err;
}

static int vdpa_dev_vendor_stats_fill(struct vdpa_device *vdev,
				      struct sk_buff *msg,
				      struct genl_info *info, u32 index)
{
	u32 device_id;
	void *hdr;
	int err;
	u32 portid = info->snd_portid;
	u32 seq = info->snd_seq;
	u32 flags = 0;

	hdr = genlmsg_put(msg, portid, seq, &vdpa_nl_family, flags,
			  VDPA_CMD_DEV_VSTATS_GET);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_string(msg, VDPA_ATTR_DEV_NAME, dev_name(&vdev->dev))) {
		err = -EMSGSIZE;
		goto undo_msg;
	}

	device_id = vdev->config->get_device_id(vdev);
	if (nla_put_u32(msg, VDPA_ATTR_DEV_ID, device_id)) {
		err = -EMSGSIZE;
		goto undo_msg;
	}

	switch (device_id) {
	case VIRTIO_ID_NET:
		if (index > VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX) {
			NL_SET_ERR_MSG_MOD(info->extack, "queue index excceeds max value");
			err = -ERANGE;
			break;
		}

		err = vendor_stats_fill(vdev, msg, info, index);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	genlmsg_end(msg, hdr);

	return err;

undo_msg:
	genlmsg_cancel(msg, hdr);
	return err;
}

static int vdpa_nl_cmd_dev_config_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct vdpa_device *vdev;
	struct sk_buff *msg;
	const char *devname;
	struct device *dev;
	int err;

	if (!info->attrs[VDPA_ATTR_DEV_NAME])
		return -EINVAL;
	devname = nla_data(info->attrs[VDPA_ATTR_DEV_NAME]);
	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	down_read(&vdpa_dev_lock);
	dev = bus_find_device(&vdpa_bus, NULL, devname, vdpa_name_match);
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "device not found");
		err = -ENODEV;
		goto dev_err;
	}
	vdev = container_of(dev, struct vdpa_device, dev);
	if (!vdev->mdev) {
		NL_SET_ERR_MSG_MOD(info->extack, "unmanaged vdpa device");
		err = -EINVAL;
		goto mdev_err;
	}
	err = vdpa_dev_config_fill(vdev, msg, info->snd_portid, info->snd_seq,
				   0, info->extack);
	if (!err)
		err = genlmsg_reply(msg, info);

mdev_err:
	put_device(dev);
dev_err:
	up_read(&vdpa_dev_lock);
	if (err)
		nlmsg_free(msg);
	return err;
}

static int vdpa_dev_config_dump(struct device *dev, void *data)
{
	struct vdpa_device *vdev = container_of(dev, struct vdpa_device, dev);
	struct vdpa_dev_dump_info *info = data;
	int err;

	if (!vdev->mdev)
		return 0;
	if (info->idx < info->start_idx) {
		info->idx++;
		return 0;
	}
	err = vdpa_dev_config_fill(vdev, info->msg, NETLINK_CB(info->cb->skb).portid,
				   info->cb->nlh->nlmsg_seq, NLM_F_MULTI,
				   info->cb->extack);
	if (err)
		return err;

	info->idx++;
	return 0;
}

static int
vdpa_nl_cmd_dev_config_get_dumpit(struct sk_buff *msg, struct netlink_callback *cb)
{
	struct vdpa_dev_dump_info info;

	info.msg = msg;
	info.cb = cb;
	info.start_idx = cb->args[0];
	info.idx = 0;

	down_read(&vdpa_dev_lock);
	bus_for_each_dev(&vdpa_bus, NULL, &info, vdpa_dev_config_dump);
	up_read(&vdpa_dev_lock);
	cb->args[0] = info.idx;
	return msg->len;
}

static int vdpa_nl_cmd_dev_stats_get_doit(struct sk_buff *skb,
					  struct genl_info *info)
{
	struct vdpa_device *vdev;
	struct sk_buff *msg;
	const char *devname;
	struct device *dev;
	u32 index;
	int err;

	if (!info->attrs[VDPA_ATTR_DEV_NAME])
		return -EINVAL;

	if (!info->attrs[VDPA_ATTR_DEV_QUEUE_INDEX])
		return -EINVAL;

	devname = nla_data(info->attrs[VDPA_ATTR_DEV_NAME]);
	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	index = nla_get_u32(info->attrs[VDPA_ATTR_DEV_QUEUE_INDEX]);
	down_read(&vdpa_dev_lock);
	dev = bus_find_device(&vdpa_bus, NULL, devname, vdpa_name_match);
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "device not found");
		err = -ENODEV;
		goto dev_err;
	}
	vdev = container_of(dev, struct vdpa_device, dev);
	if (!vdev->mdev) {
		NL_SET_ERR_MSG_MOD(info->extack, "unmanaged vdpa device");
		err = -EINVAL;
		goto mdev_err;
	}
	err = vdpa_dev_vendor_stats_fill(vdev, msg, info, index);
	if (err)
		goto mdev_err;

	err = genlmsg_reply(msg, info);

	put_device(dev);
	up_read(&vdpa_dev_lock);

	return err;

mdev_err:
	put_device(dev);
dev_err:
	nlmsg_free(msg);
	up_read(&vdpa_dev_lock);
	return err;
}

static const struct nla_policy vdpa_nl_policy[VDPA_ATTR_MAX + 1] = {
	[VDPA_ATTR_MGMTDEV_BUS_NAME] = { .type = NLA_NUL_STRING },
	[VDPA_ATTR_MGMTDEV_DEV_NAME] = { .type = NLA_STRING },
	[VDPA_ATTR_DEV_NAME] = { .type = NLA_STRING },
	[VDPA_ATTR_DEV_NET_CFG_MACADDR] = NLA_POLICY_ETH_ADDR,
	/* virtio spec 1.1 section 5.1.4.1 for valid MTU range */
	[VDPA_ATTR_DEV_NET_CFG_MTU] = NLA_POLICY_MIN(NLA_U16, 68),
};

static const struct genl_ops vdpa_nl_ops[] = {
	{
		.cmd = VDPA_CMD_MGMTDEV_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = vdpa_nl_cmd_mgmtdev_get_doit,
		.dumpit = vdpa_nl_cmd_mgmtdev_get_dumpit,
	},
	{
		.cmd = VDPA_CMD_DEV_NEW,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = vdpa_nl_cmd_dev_add_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = VDPA_CMD_DEV_DEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = vdpa_nl_cmd_dev_del_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = VDPA_CMD_DEV_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = vdpa_nl_cmd_dev_get_doit,
		.dumpit = vdpa_nl_cmd_dev_get_dumpit,
	},
	{
		.cmd = VDPA_CMD_DEV_CONFIG_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = vdpa_nl_cmd_dev_config_get_doit,
		.dumpit = vdpa_nl_cmd_dev_config_get_dumpit,
	},
	{
		.cmd = VDPA_CMD_DEV_VSTATS_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = vdpa_nl_cmd_dev_stats_get_doit,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family vdpa_nl_family __ro_after_init = {
	.name = VDPA_GENL_NAME,
	.version = VDPA_GENL_VERSION,
	.maxattr = VDPA_ATTR_MAX,
	.policy = vdpa_nl_policy,
	.netnsok = false,
	.module = THIS_MODULE,
	.ops = vdpa_nl_ops,
	.n_ops = ARRAY_SIZE(vdpa_nl_ops),
	.resv_start_op = VDPA_CMD_DEV_VSTATS_GET + 1,
};

static int vdpa_init(void)
{
	int err;

	err = bus_register(&vdpa_bus);
	if (err)
		return err;
	err = genl_register_family(&vdpa_nl_family);
	if (err)
		goto err;
	return 0;

err:
	bus_unregister(&vdpa_bus);
	return err;
}

static void __exit vdpa_exit(void)
{
	genl_unregister_family(&vdpa_nl_family);
	bus_unregister(&vdpa_bus);
	ida_destroy(&vdpa_index_ida);
}
core_initcall(vdpa_init);
module_exit(vdpa_exit);

MODULE_AUTHOR("Jason Wang <jasowang@redhat.com>");
MODULE_LICENSE("GPL v2");
