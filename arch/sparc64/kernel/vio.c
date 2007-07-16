/* vio.c: Virtual I/O channel devices probing infrastructure.
 *
 *    Copyright (c) 2003-2005 IBM Corp.
 *     Dave Engebretsen engebret@us.ibm.com
 *     Santiago Leon santil@us.ibm.com
 *     Hollis Blanchard <hollisb@us.ibm.com>
 *     Stephen Rothwell
 *
 * Adapted to sparc64 by David S. Miller davem@davemloft.net
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/init.h>

#include <asm/mdesc.h>
#include <asm/vio.h>

static inline int find_in_proplist(const char *list, const char *match,
				   int len)
{
	while (len > 0) {
		int l;

		if (!strcmp(list, match))
			return 1;
		l = strlen(list) + 1;
		list += l;
		len -= l;
	}
	return 0;
}

static const struct vio_device_id *vio_match_device(
	const struct vio_device_id *matches,
	const struct vio_dev *dev)
{
	const char *type, *compat;
	int len;

	type = dev->type;
	compat = dev->compat;
	len = dev->compat_len;

	while (matches->type[0] || matches->compat[0]) {
		int match = 1;
		if (matches->type[0])
			match &= !strcmp(matches->type, type);

		if (matches->compat[0]) {
			match &= len &&
				find_in_proplist(compat, matches->compat, len);
		}
		if (match)
			return matches;
		matches++;
	}
	return NULL;
}

static int vio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct vio_dev *vio_dev = to_vio_dev(dev);
	struct vio_driver *vio_drv = to_vio_driver(drv);
	const struct vio_device_id *matches = vio_drv->id_table;

	if (!matches)
		return 0;

	return vio_match_device(matches, vio_dev) != NULL;
}

static int vio_device_probe(struct device *dev)
{
	struct vio_dev *vdev = to_vio_dev(dev);
	struct vio_driver *drv = to_vio_driver(dev->driver);
	const struct vio_device_id *id;
	int error = -ENODEV;

	if (drv->probe) {
		id = vio_match_device(drv->id_table, vdev);
		if (id)
			error = drv->probe(vdev, id);
	}

	return error;
}

static int vio_device_remove(struct device *dev)
{
	struct vio_dev *vdev = to_vio_dev(dev);
	struct vio_driver *drv = to_vio_driver(dev->driver);

	if (drv->remove)
		return drv->remove(vdev);

	return 1;
}

static ssize_t devspec_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vio_dev *vdev = to_vio_dev(dev);
	const char *str = "none";

	if (!strcmp(vdev->type, "network"))
		str = "vnet";
	else if (!strcmp(vdev->type, "block"))
		str = "vdisk";

	return sprintf(buf, "%s\n", str);
}

static ssize_t type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vio_dev *vdev = to_vio_dev(dev);
	return sprintf(buf, "%s\n", vdev->type);
}

static struct device_attribute vio_dev_attrs[] = {
	__ATTR_RO(devspec),
	__ATTR_RO(type),
	__ATTR_NULL
};

static struct bus_type vio_bus_type = {
	.name		= "vio",
	.dev_attrs	= vio_dev_attrs,
	.match		= vio_bus_match,
	.probe		= vio_device_probe,
	.remove		= vio_device_remove,
};

int vio_register_driver(struct vio_driver *viodrv)
{
	viodrv->driver.bus = &vio_bus_type;

	return driver_register(&viodrv->driver);
}
EXPORT_SYMBOL(vio_register_driver);

void vio_unregister_driver(struct vio_driver *viodrv)
{
	driver_unregister(&viodrv->driver);
}
EXPORT_SYMBOL(vio_unregister_driver);

static void __devinit vio_dev_release(struct device *dev)
{
	kfree(to_vio_dev(dev));
}

static ssize_t
show_pciobppath_attr(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct vio_dev *vdev;
	struct device_node *dp;

	vdev = to_vio_dev(dev);
	dp = vdev->dp;

	return snprintf (buf, PAGE_SIZE, "%s\n", dp->full_name);
}

static DEVICE_ATTR(obppath, S_IRUSR | S_IRGRP | S_IROTH,
		   show_pciobppath_attr, NULL);

struct device_node *cdev_node;

static struct vio_dev *root_vdev;
static u64 cdev_cfg_handle;

static void vio_fill_channel_info(struct mdesc_handle *hp, u64 mp,
				  struct vio_dev *vdev)
{
	u64 a;

	mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_FWD) {
		const u64 *chan_id;
		const u64 *irq;
		u64 target;

		target = mdesc_arc_target(hp, a);

		irq = mdesc_get_property(hp, target, "tx-ino", NULL);
		if (irq)
			vdev->tx_irq = sun4v_build_virq(cdev_cfg_handle, *irq);

		irq = mdesc_get_property(hp, target, "rx-ino", NULL);
		if (irq)
			vdev->rx_irq = sun4v_build_virq(cdev_cfg_handle, *irq);

		chan_id = mdesc_get_property(hp, target, "id", NULL);
		if (chan_id)
			vdev->channel_id = *chan_id;
	}
}

static struct vio_dev *vio_create_one(struct mdesc_handle *hp, u64 mp,
				      struct device *parent)
{
	const char *type, *compat;
	struct device_node *dp;
	struct vio_dev *vdev;
	int err, tlen, clen;

	type = mdesc_get_property(hp, mp, "device-type", &tlen);
	if (!type) {
		type = mdesc_get_property(hp, mp, "name", &tlen);
		if (!type) {
			type = mdesc_node_name(hp, mp);
			tlen = strlen(type) + 1;
		}
	}
	if (tlen > VIO_MAX_TYPE_LEN) {
		printk(KERN_ERR "VIO: Type string [%s] is too long.\n",
		       type);
		return NULL;
	}

	compat = mdesc_get_property(hp, mp, "device-type", &clen);
	if (!compat) {
		clen = 0;
	} else if (clen > VIO_MAX_COMPAT_LEN) {
		printk(KERN_ERR "VIO: Compat len %d for [%s] is too long.\n",
		       clen, type);
		return NULL;
	}

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev) {
		printk(KERN_ERR "VIO: Could not allocate vio_dev\n");
		return NULL;
	}

	vdev->mp = mp;
	memcpy(vdev->type, type, tlen);
	if (compat)
		memcpy(vdev->compat, compat, clen);
	else
		memset(vdev->compat, 0, sizeof(vdev->compat));
	vdev->compat_len = clen;

	vdev->channel_id = ~0UL;
	vdev->tx_irq = ~0;
	vdev->rx_irq = ~0;

	vio_fill_channel_info(hp, mp, vdev);

	snprintf(vdev->dev.bus_id, BUS_ID_SIZE, "%lx", mp);
	vdev->dev.parent = parent;
	vdev->dev.bus = &vio_bus_type;
	vdev->dev.release = vio_dev_release;

	if (parent == NULL) {
		dp = cdev_node;
	} else if (to_vio_dev(parent) == root_vdev) {
		dp = of_get_next_child(cdev_node, NULL);
		while (dp) {
			if (!strcmp(dp->type, type))
				break;

			dp = of_get_next_child(cdev_node, dp);
		}
	} else {
		dp = to_vio_dev(parent)->dp;
	}
	vdev->dp = dp;

	err = device_register(&vdev->dev);
	if (err) {
		printk(KERN_ERR "VIO: Could not register device %s, err=%d\n",
		       vdev->dev.bus_id, err);
		kfree(vdev);
		return NULL;
	}
	if (vdev->dp)
		err = sysfs_create_file(&vdev->dev.kobj,
					&dev_attr_obppath.attr);

	return vdev;
}

static void walk_tree(struct mdesc_handle *hp, u64 n, struct vio_dev *parent)
{
	u64 a;

	mdesc_for_each_arc(a, hp, n, MDESC_ARC_TYPE_FWD) {
		struct vio_dev *vdev;
		u64 target;

		target = mdesc_arc_target(hp, a);
		vdev = vio_create_one(hp, target, &parent->dev);
		if (vdev)
			walk_tree(hp, target, vdev);
	}
}

static void create_devices(struct mdesc_handle *hp, u64 root)
{
	u64 mp;

	root_vdev = vio_create_one(hp, root, NULL);
	if (!root_vdev) {
		printk(KERN_ERR "VIO: Coult not create root device.\n");
		return;
	}

	walk_tree(hp, root, root_vdev);

	/* Domain services is odd as it doesn't sit underneath the
	 * channel-devices node, so we plug it in manually.
	 */
	mp = mdesc_node_by_name(hp, MDESC_NODE_NULL, "domain-services");
	if (mp != MDESC_NODE_NULL) {
		struct vio_dev *parent = vio_create_one(hp, mp,
							&root_vdev->dev);

		if (parent)
			walk_tree(hp, mp, parent);
	}
}

const char *channel_devices_node = "channel-devices";
const char *channel_devices_compat = "SUNW,sun4v-channel-devices";
const char *cfg_handle_prop = "cfg-handle";

static int __init vio_init(void)
{
	struct mdesc_handle *hp;
	const char *compat;
	const u64 *cfg_handle;
	int err, len;
	u64 root;

	err = bus_register(&vio_bus_type);
	if (err) {
		printk(KERN_ERR "VIO: Could not register bus type err=%d\n",
		       err);
		return err;
	}

	hp = mdesc_grab();
	if (!hp)
		return 0;

	root = mdesc_node_by_name(hp, MDESC_NODE_NULL, channel_devices_node);
	if (root == MDESC_NODE_NULL) {
		printk(KERN_INFO "VIO: No channel-devices MDESC node.\n");
		mdesc_release(hp);
		return 0;
	}

	cdev_node = of_find_node_by_name(NULL, "channel-devices");
	err = -ENODEV;
	if (!cdev_node) {
		printk(KERN_INFO "VIO: No channel-devices OBP node.\n");
		goto out_release;
	}

	compat = mdesc_get_property(hp, root, "compatible", &len);
	if (!compat) {
		printk(KERN_ERR "VIO: Channel devices lacks compatible "
		       "property\n");
		goto out_release;
	}
	if (!find_in_proplist(compat, channel_devices_compat, len)) {
		printk(KERN_ERR "VIO: Channel devices node lacks (%s) "
		       "compat entry.\n", channel_devices_compat);
		goto out_release;
	}

	cfg_handle = mdesc_get_property(hp, root, cfg_handle_prop, NULL);
	if (!cfg_handle) {
		printk(KERN_ERR "VIO: Channel devices lacks %s property\n",
		       cfg_handle_prop);
		goto out_release;
	}

	cdev_cfg_handle = *cfg_handle;

	create_devices(hp, root);

	mdesc_release(hp);

	return 0;

out_release:
	mdesc_release(hp);
	return err;
}

postcore_initcall(vio_init);
