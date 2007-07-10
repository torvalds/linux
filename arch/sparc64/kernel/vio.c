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
		if (matches->type[0]) {
			match &= type
				&& !strcmp(matches->type, type);
		}
		if (matches->compat[0]) {
			match &= compat &&
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

	if (vdev->type) {
		if (!strcmp(vdev->type, "network"))
			str = "vnet";
		else if (!strcmp(vdev->type, "block"))
			str = "vdisk";
	}

	return sprintf(buf, "%s\n", str);
}

static struct device_attribute vio_dev_attrs[] = {
	__ATTR_RO(devspec),
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

struct mdesc_node *vio_find_endpoint(struct vio_dev *vdev)
{
	struct mdesc_node *endp, *mp = vdev->mp;
	int i;

	endp = NULL;
	for (i = 0; i < mp->num_arcs; i++) {
		struct mdesc_node *t;

		if (strcmp(mp->arcs[i].name, "fwd"))
			continue;

		t = mp->arcs[i].arc;
		if (strcmp(t->name, "channel-endpoint"))
			continue;

		endp = t;
		break;
	}

	return endp;
}
EXPORT_SYMBOL(vio_find_endpoint);

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

static struct vio_dev *vio_create_one(struct mdesc_node *mp,
				      struct device *parent)
{
	const char *type, *compat;
	struct device_node *dp;
	struct vio_dev *vdev;
	const u64 *irq;
	int err, clen;

	type = md_get_property(mp, "device-type", NULL);
	if (!type)
		type = md_get_property(mp, "name", NULL);
	compat = md_get_property(mp, "device-type", &clen);

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev) {
		printk(KERN_ERR "VIO: Could not allocate vio_dev\n");
		return NULL;
	}

	vdev->mp = mp;
	vdev->type = type;
	vdev->compat = compat;
	vdev->compat_len = clen;

	irq = md_get_property(mp, "tx-ino", NULL);
	if (irq)
		mp->irqs[0] = sun4v_build_virq(cdev_cfg_handle, *irq);

	irq = md_get_property(mp, "rx-ino", NULL);
	if (irq)
		mp->irqs[1] = sun4v_build_virq(cdev_cfg_handle, *irq);

	snprintf(vdev->dev.bus_id, BUS_ID_SIZE, "%lx", mp->node);
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

static void walk_tree(struct mdesc_node *n, struct vio_dev *parent)
{
	int i;

	for (i = 0; i < n->num_arcs; i++) {
		struct mdesc_node *mp;
		struct vio_dev *vdev;

		if (strcmp(n->arcs[i].name, "fwd"))
			continue;

		mp = n->arcs[i].arc;

		vdev = vio_create_one(mp, &parent->dev);
		if (vdev && mp->num_arcs)
			walk_tree(mp, vdev);
	}
}

static void create_devices(struct mdesc_node *root)
{
	root_vdev = vio_create_one(root, NULL);
	if (!root_vdev) {
		printk(KERN_ERR "VIO: Coult not create root device.\n");
		return;
	}

	walk_tree(root, root_vdev);
}

const char *channel_devices_node = "channel-devices";
const char *channel_devices_compat = "SUNW,sun4v-channel-devices";
const char *cfg_handle_prop = "cfg-handle";

static int __init vio_init(void)
{
	struct mdesc_node *root;
	const char *compat;
	const u64 *cfg_handle;
	int err, len;

	root = md_find_node_by_name(NULL, channel_devices_node);
	if (!root) {
		printk(KERN_INFO "VIO: No channel-devices MDESC node.\n");
		return 0;
	}

	cdev_node = of_find_node_by_name(NULL, "channel-devices");
	if (!cdev_node) {
		printk(KERN_INFO "VIO: No channel-devices OBP node.\n");
		return -ENODEV;
	}

	compat = md_get_property(root, "compatible", &len);
	if (!compat) {
		printk(KERN_ERR "VIO: Channel devices lacks compatible "
		       "property\n");
		return -ENODEV;
	}
	if (!find_in_proplist(compat, channel_devices_compat, len)) {
		printk(KERN_ERR "VIO: Channel devices node lacks (%s) "
		       "compat entry.\n", channel_devices_compat);
		return -ENODEV;
	}

	cfg_handle = md_get_property(root, cfg_handle_prop, NULL);
	if (!cfg_handle) {
		printk(KERN_ERR "VIO: Channel devices lacks %s property\n",
		       cfg_handle_prop);
		return -ENODEV;
	}

	cdev_cfg_handle = *cfg_handle;

	err = bus_register(&vio_bus_type);
	if (err) {
		printk(KERN_ERR "VIO: Could not register bus type err=%d\n",
		       err);
		return err;
	}

	create_devices(root);

	return 0;
}

postcore_initcall(vio_init);
