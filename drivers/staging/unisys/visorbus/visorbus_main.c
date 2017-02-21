/* visorbus_main.c
 *
 * Copyright � 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include <linux/debugfs.h>
#include <linux/uuid.h>

#include "visorbus.h"
#include "visorbus_private.h"
#include "vmcallinterface.h"

#define MYDRVNAME "visorbus"

/* module parameters */
static int visorbus_forcematch;
static int visorbus_forcenomatch;

/* Display string that is guaranteed to be no longer the 99 characters*/
#define LINESIZE 99

#define CURRENT_FILE_PC VISOR_BUS_PC_visorbus_main_c
#define POLLJIFFIES_NORMALCHANNEL     10

static int busreg_rc = -ENODEV; /* stores the result from bus registration */
static struct dentry *visorbus_debugfs_dir;

/*
 * DEVICE type attributes
 *
 * The modalias file will contain the guid of the device.
 */
static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev;
	uuid_le guid;

	vdev = to_visor_device(dev);
	guid = visorchannel_get_uuid(vdev->visorchannel);
	return sprintf(buf, "visorbus:%pUl\n", &guid);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *visorbus_dev_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};

/* sysfs example for bridge-only sysfs files using device_type's */
static const struct attribute_group visorbus_dev_group = {
	.attrs = visorbus_dev_attrs,
};

static const struct attribute_group *visorbus_dev_groups[] = {
	&visorbus_dev_group,
	NULL,
};

/* filled in with info about parent chipset driver when we register with it */
static struct ultra_vbus_deviceinfo chipset_driverinfo;
/* filled in with info about this driver, wrt it servicing client busses */
static struct ultra_vbus_deviceinfo clientbus_driverinfo;

/* list of visor_device structs, linked via .list_all */
static LIST_HEAD(list_all_bus_instances);
/* list of visor_device structs, linked via .list_all */
static LIST_HEAD(list_all_device_instances);

static int
visorbus_uevent(struct device *xdev, struct kobj_uevent_env *env)
{
	struct visor_device *dev;
	uuid_le guid;

	dev = to_visor_device(xdev);
	guid = visorchannel_get_uuid(dev->visorchannel);

	if (add_uevent_var(env, "MODALIAS=visorbus:%pUl", &guid))
		return -ENOMEM;
	return 0;
}

/*
 * visorbus_match() - called automatically upon adding a visor_device
 *                    (device_add), or adding a visor_driver
 *                    (visorbus_register_visor_driver)
 * @xdev: struct device for the device being matched
 * @xdrv: struct device_driver for driver to match device against
 *
 * Return: 1 iff the provided driver can control the specified device
 */
static int
visorbus_match(struct device *xdev, struct device_driver *xdrv)
{
	uuid_le channel_type;
	int i;
	struct visor_device *dev;
	struct visor_driver *drv;

	dev = to_visor_device(xdev);
	drv = to_visor_driver(xdrv);
	channel_type = visorchannel_get_uuid(dev->visorchannel);

	if (visorbus_forcematch)
		return 1;
	if (visorbus_forcenomatch)
		return 0;
	if (!drv->channel_types)
		return 0;

	for (i = 0;
	     (uuid_le_cmp(drv->channel_types[i].guid, NULL_UUID_LE) != 0) ||
	     (drv->channel_types[i].name);
	     i++)
		if (uuid_le_cmp(drv->channel_types[i].guid,
				channel_type) == 0)
			return i + 1;

	return 0;
}

/*
 * This describes the TYPE of bus.
 *  (Don't confuse this with an INSTANCE of the bus.)
 */
struct bus_type visorbus_type = {
	.name = "visorbus",
	.match = visorbus_match,
	.uevent = visorbus_uevent,
	.dev_groups = visorbus_dev_groups,
};

/*
 * visorbus_release_busdevice() - called when device_unregister() is called for
 *                                the bus device instance, after all other tasks
 *                                involved with destroying the dev are complete
 * @xdev: struct device for the bus being released
 */
static void
visorbus_release_busdevice(struct device *xdev)
{
	struct visor_device *dev = dev_get_drvdata(xdev);

	debugfs_remove(dev->debugfs_client_bus_info);
	debugfs_remove_recursive(dev->debugfs_dir);
	kfree(dev);
}

/*
 * visorbus_release_device() - called when device_unregister() is called for
 *                             each child device instance
 * @xdev: struct device for the visor device being released
 */
static void
visorbus_release_device(struct device *xdev)
{
	struct visor_device *dev = to_visor_device(xdev);

	if (dev->visorchannel) {
		visorchannel_destroy(dev->visorchannel);
		dev->visorchannel = NULL;
	}
	kfree(dev);
}

/*
 * begin implementation of specific channel attributes to appear under
 * /sys/bus/visorbus<x>/dev<y>/channel
 */

static ssize_t physaddr_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);

	if (!vdev->visorchannel)
		return 0;
	return sprintf(buf, "0x%llx\n",
		       visorchannel_get_physaddr(vdev->visorchannel));
}
static DEVICE_ATTR_RO(physaddr);

static ssize_t nbytes_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);

	if (!vdev->visorchannel)
		return 0;
	return sprintf(buf, "0x%lx\n",
			visorchannel_get_nbytes(vdev->visorchannel));
}
static DEVICE_ATTR_RO(nbytes);

static ssize_t clientpartition_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);

	if (!vdev->visorchannel)
		return 0;
	return sprintf(buf, "0x%llx\n",
		       visorchannel_get_clientpartition(vdev->visorchannel));
}
static DEVICE_ATTR_RO(clientpartition);

static ssize_t typeguid_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);
	char typeid[LINESIZE];

	if (!vdev->visorchannel)
		return 0;
	return sprintf(buf, "%s\n",
		       visorchannel_id(vdev->visorchannel, typeid));
}
static DEVICE_ATTR_RO(typeguid);

static ssize_t zoneguid_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);
	char zoneid[LINESIZE];

	if (!vdev->visorchannel)
		return 0;
	return sprintf(buf, "%s\n",
		       visorchannel_zoneid(vdev->visorchannel, zoneid));
}
static DEVICE_ATTR_RO(zoneguid);

static ssize_t typename_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);
	int i = 0;
	struct bus_type *xbus = dev->bus;
	struct device_driver *xdrv = dev->driver;
	struct visor_driver *drv = NULL;

	if (!vdev->visorchannel || !xbus || !xdrv)
		return 0;
	i = xbus->match(dev, xdrv);
	if (!i)
		return 0;
	drv = to_visor_driver(xdrv);
	return sprintf(buf, "%s\n", drv->channel_types[i - 1].name);
}
static DEVICE_ATTR_RO(typename);

static struct attribute *channel_attrs[] = {
		&dev_attr_physaddr.attr,
		&dev_attr_nbytes.attr,
		&dev_attr_clientpartition.attr,
		&dev_attr_typeguid.attr,
		&dev_attr_zoneguid.attr,
		&dev_attr_typename.attr,
		NULL
};

static struct attribute_group channel_attr_grp = {
		.name = "channel",
		.attrs = channel_attrs,
};

static const struct attribute_group *visorbus_channel_groups[] = {
		&channel_attr_grp,
		NULL
};

/* end implementation of specific channel attributes */

/*
 *  BUS instance attributes
 *
 *  define & implement display of bus attributes under
 *  /sys/bus/visorbus/devices/visorbus<n>.
 */

static ssize_t partition_handle_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	u64 handle = visorchannel_get_clientpartition(vdev->visorchannel);

	return sprintf(buf, "0x%llx\n", handle);
}
static DEVICE_ATTR_RO(partition_handle);

static ssize_t partition_guid_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf) {
	struct visor_device *vdev = to_visor_device(dev);

	return sprintf(buf, "{%pUb}\n", &vdev->partition_uuid);
}
static DEVICE_ATTR_RO(partition_guid);

static ssize_t partition_name_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf) {
	struct visor_device *vdev = to_visor_device(dev);

	return sprintf(buf, "%s\n", vdev->name);
}
static DEVICE_ATTR_RO(partition_name);

static ssize_t channel_addr_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	u64 addr = visorchannel_get_physaddr(vdev->visorchannel);

	return sprintf(buf, "0x%llx\n", addr);
}
static DEVICE_ATTR_RO(channel_addr);

static ssize_t channel_bytes_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	u64 nbytes = visorchannel_get_nbytes(vdev->visorchannel);

	return sprintf(buf, "0x%llx\n", nbytes);
}
static DEVICE_ATTR_RO(channel_bytes);

static ssize_t channel_id_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	int len = 0;

	if (vdev->visorchannel) {
		visorchannel_id(vdev->visorchannel, buf);
		len = strlen(buf);
		buf[len++] = '\n';
	}
	return len;
}
static DEVICE_ATTR_RO(channel_id);

static struct attribute *dev_attrs[] = {
		&dev_attr_partition_handle.attr,
		&dev_attr_partition_guid.attr,
		&dev_attr_partition_name.attr,
		&dev_attr_channel_addr.attr,
		&dev_attr_channel_bytes.attr,
		&dev_attr_channel_id.attr,
		NULL
};

static struct attribute_group dev_attr_grp = {
		.attrs = dev_attrs,
};

static const struct attribute_group *visorbus_groups[] = {
		&dev_attr_grp,
		NULL
};

/*
 *  BUS debugfs entries
 *
 *  define & implement display of debugfs attributes under
 *  /sys/kernel/debug/visorbus/visorbus<n>.
 */

static int client_bus_info_debugfs_show(struct seq_file *seq, void *v)
{
	struct visor_device *vdev = seq->private;
	struct visorchannel *channel = vdev->visorchannel;

	int i;
	unsigned long off;
	struct ultra_vbus_deviceinfo dev_info;

	if (!channel)
		return 0;

	seq_printf(seq,
		   "Client device / client driver info for %s partition (vbus #%u):\n",
		   ((vdev->name) ? (char *)(vdev->name) : ""),
		   vdev->chipset_bus_no);
	if (visorchannel_read(channel,
			      offsetof(struct spar_vbus_channel_protocol,
				       chp_info),
			      &dev_info, sizeof(dev_info)) >= 0)
		vbuschannel_print_devinfo(&dev_info, seq, -1);
	if (visorchannel_read(channel,
			      offsetof(struct spar_vbus_channel_protocol,
				       bus_info),
			      &dev_info, sizeof(dev_info)) >= 0)
		vbuschannel_print_devinfo(&dev_info, seq, -1);
	off = offsetof(struct spar_vbus_channel_protocol, dev_info);
	i = 0;
	while (off + sizeof(dev_info) <= visorchannel_get_nbytes(channel)) {
		if (visorchannel_read(channel, off, &dev_info,
				      sizeof(dev_info)) >= 0)
			vbuschannel_print_devinfo(&dev_info, seq, i);
		off += sizeof(dev_info);
		i++;
	}

	return 0;
}

static int client_bus_info_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, client_bus_info_debugfs_show,
			   inode->i_private);
}

static const struct file_operations client_bus_info_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = client_bus_info_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void
dev_periodic_work(unsigned long __opaque)
{
	struct visor_device *dev = (struct visor_device *)__opaque;
	struct visor_driver *drv = to_visor_driver(dev->device.driver);

	drv->channel_interrupt(dev);
	mod_timer(&dev->timer, jiffies + POLLJIFFIES_NORMALCHANNEL);
}

static void
dev_start_periodic_work(struct visor_device *dev)
{
	if (dev->being_removed || dev->timer_active)
		return;
	/* now up by at least 2 */
	get_device(&dev->device);
	dev->timer.expires = jiffies + POLLJIFFIES_NORMALCHANNEL;
	add_timer(&dev->timer);
	dev->timer_active = true;
}

static void
dev_stop_periodic_work(struct visor_device *dev)
{
	if (!dev->timer_active)
		return;
	del_timer_sync(&dev->timer);
	dev->timer_active = false;
	put_device(&dev->device);
}

/*
 * visordriver_remove_device() - handle visor device going away
 * @xdev: struct device for the visor device being removed
 *
 * This is called when device_unregister() is called for each child device
 * instance, to notify the appropriate visorbus function driver that the device
 * is going away, and to decrease the reference count of the device.
 *
 * Return: 0 iff successful
 */
static int
visordriver_remove_device(struct device *xdev)
{
	struct visor_device *dev;
	struct visor_driver *drv;

	dev = to_visor_device(xdev);
	drv = to_visor_driver(xdev->driver);
	mutex_lock(&dev->visordriver_callback_lock);
	dev->being_removed = true;
	if (drv->remove)
		drv->remove(dev);
	mutex_unlock(&dev->visordriver_callback_lock);
	dev_stop_periodic_work(dev);

	put_device(&dev->device);
	return 0;
}

/**
 * visorbus_unregister_visor_driver() - unregisters the provided driver
 * @drv: the driver to unregister
 *
 * A visor function driver calls this function to unregister the driver,
 * i.e., within its module_exit function.
 */
void
visorbus_unregister_visor_driver(struct visor_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(visorbus_unregister_visor_driver);

/**
 * visorbus_read_channel() - reads from the designated channel into
 *                           the provided buffer
 * @dev:    the device whose channel is read from
 * @offset: the offset into the channel at which reading starts
 * @dest:   the destination buffer that is written into from the channel
 * @nbytes: the number of bytes to read from the channel
 *
 * If receiving a message, use the visorchannel_signalremove()
 * function instead.
 *
 * Return: integer indicating success (zero) or failure (non-zero)
 */
int
visorbus_read_channel(struct visor_device *dev, unsigned long offset,
		      void *dest, unsigned long nbytes)
{
	return visorchannel_read(dev->visorchannel, offset, dest, nbytes);
}
EXPORT_SYMBOL_GPL(visorbus_read_channel);

/**
 * visorbus_write_channel() - writes the provided buffer into the designated
 *                            channel
 * @dev:    the device whose channel is written to
 * @offset: the offset into the channel at which writing starts
 * @src:    the source buffer that is written into the channel
 * @nbytes: the number of bytes to write into the channel
 *
 * If sending a message, use the visorchannel_signalinsert()
 * function instead.
 *
 * Return: integer indicating success (zero) or failure (non-zero)
 */
int
visorbus_write_channel(struct visor_device *dev, unsigned long offset,
		       void *src, unsigned long nbytes)
{
	return visorchannel_write(dev->visorchannel, offset, src, nbytes);
}
EXPORT_SYMBOL_GPL(visorbus_write_channel);

/**
 * visorbus_enable_channel_interrupts() - enables interrupts on the
 *                                        designated device
 * @dev: the device on which to enable interrupts
 *
 * Currently we don't yet have a real interrupt, so for now we just call the
 * interrupt function periodically via a timer.
 */
void
visorbus_enable_channel_interrupts(struct visor_device *dev)
{
	struct visor_driver *drv = to_visor_driver(dev->device.driver);

	if (!drv->channel_interrupt) {
		dev_err(&dev->device, "%s no interrupt function!\n", __func__);
		return;
	}

	dev_start_periodic_work(dev);
}
EXPORT_SYMBOL_GPL(visorbus_enable_channel_interrupts);

/**
 * visorbus_disable_channel_interrupts() - disables interrupts on the
 *                                         designated device
 * @dev: the device on which to disable interrupts
 */
void
visorbus_disable_channel_interrupts(struct visor_device *dev)
{
	dev_stop_periodic_work(dev);
}
EXPORT_SYMBOL_GPL(visorbus_disable_channel_interrupts);

/*
 * create_visor_device() - create visor device as a result of receiving the
 *                         controlvm device_create message for a new device
 * @dev: a freshly-zeroed struct visor_device, containing only filled-in values
 *       for chipset_bus_no and chipset_dev_no, that will be initialized
 *
 * This is how everything starts from the device end.
 * This function is called when a channel first appears via a ControlVM
 * message.  In response, this function allocates a visor_device to
 * correspond to the new channel, and attempts to connect it the appropriate
 * driver.  If the appropriate driver is found, the visor_driver.probe()
 * function for that driver will be called, and will be passed the new
 * visor_device that we just created.
 *
 * It's ok if the appropriate driver is not yet loaded, because in that case
 * the new device struct will just stick around in the bus' list of devices.
 * When the appropriate driver calls visorbus_register_visor_driver(), the
 * visor_driver.probe() for the new driver will be called with the new
 * device.
 *
 * Return: 0 if successful, otherwise the negative value returned by
 *         device_add() indicating the reason for failure
 */
static int
create_visor_device(struct visor_device *dev)
{
	int err;
	u32 chipset_bus_no = dev->chipset_bus_no;
	u32 chipset_dev_no = dev->chipset_dev_no;

	POSTCODE_LINUX(DEVICE_CREATE_ENTRY_PC, chipset_dev_no, chipset_bus_no,
		       DIAG_SEVERITY_PRINT);

	mutex_init(&dev->visordriver_callback_lock);
	dev->device.bus = &visorbus_type;
	dev->device.groups = visorbus_channel_groups;
	device_initialize(&dev->device);
	dev->device.release = visorbus_release_device;
	/* keep a reference just for us (now 2) */
	get_device(&dev->device);
	setup_timer(&dev->timer, dev_periodic_work, (unsigned long)dev);

	/*
	 * bus_id must be a unique name with respect to this bus TYPE
	 * (NOT bus instance).  That's why we need to include the bus
	 * number within the name.
	 */
	dev_set_name(&dev->device, "vbus%u:dev%u",
		     chipset_bus_no, chipset_dev_no);

	/*
	 * device_add does this:
	 *    bus_add_device(dev)
	 *    ->device_attach(dev)
	 *      ->for each driver drv registered on the bus that dev is on
	 *          if (dev.drv)  **  device already has a driver **
	 *            ** not sure we could ever get here... **
	 *          else
	 *            if (bus.match(dev,drv)) [visorbus_match]
	 *              dev.drv = drv
	 *              if (!drv.probe(dev))  [visordriver_probe_device]
	 *                dev.drv = NULL
	 *
	 *  Note that device_add does NOT fail if no driver failed to
	 *  claim the device.  The device will be linked onto
	 *  bus_type.klist_devices regardless (use bus_for_each_dev).
	 */
	err = device_add(&dev->device);
	if (err < 0) {
		POSTCODE_LINUX(DEVICE_ADD_PC, 0, chipset_bus_no,
			       DIAG_SEVERITY_ERR);
		goto err_put;
	}

	list_add_tail(&dev->list_all, &list_all_device_instances);
	return 0; /* success: reference kept via unmatched get_device() */

err_put:
	put_device(&dev->device);
	return err;
}

static void
remove_visor_device(struct visor_device *dev)
{
	list_del(&dev->list_all);
	put_device(&dev->device);
	device_unregister(&dev->device);
}

static int
get_vbus_header_info(struct visorchannel *chan,
		     struct spar_vbus_headerinfo *hdr_info)
{
	if (!SPAR_VBUS_CHANNEL_OK_CLIENT(visorchannel_get_header(chan)))
		return -EINVAL;

	if (visorchannel_read(chan, sizeof(struct channel_header), hdr_info,
			      sizeof(*hdr_info)) < 0) {
		return -EIO;
	}
	if (hdr_info->struct_bytes < sizeof(struct spar_vbus_headerinfo))
		return -EINVAL;

	if (hdr_info->device_info_struct_bytes <
	    sizeof(struct ultra_vbus_deviceinfo)) {
		return -EINVAL;
	}
	return 0;
}

/*
 * write_vbus_chp_info() - write the contents of <info> to the struct
 *                         spar_vbus_channel_protocol.chp_info
 * @chan:     indentifies the s-Par channel that will be updated
 * @hdr_info: used to find appropriate channel offset to write data
 * @info:     contains the information to write
 *
 * Writes chipset info into the channel memory to be used for diagnostic
 * purposes.
 *
 * Returns no value since this is debug information and not needed for
 * device functionality.
 */
static void
write_vbus_chp_info(struct visorchannel *chan,
		    struct spar_vbus_headerinfo *hdr_info,
		    struct ultra_vbus_deviceinfo *info)
{
	int off = sizeof(struct channel_header) + hdr_info->chp_info_offset;

	if (hdr_info->chp_info_offset == 0)
		return;

	visorchannel_write(chan, off, info, sizeof(*info));
}

/*
 * write_vbus_bus_info() - write the contents of <info> to the struct
 *                         spar_vbus_channel_protocol.bus_info
 * @chan:     indentifies the s-Par channel that will be updated
 * @hdr_info: used to find appropriate channel offset to write data
 * @info:     contains the information to write
 *
 * Writes bus info into the channel memory to be used for diagnostic
 * purposes.
 *
 * Returns no value since this is debug information and not needed for
 * device functionality.
 */
static void
write_vbus_bus_info(struct visorchannel *chan,
		    struct spar_vbus_headerinfo *hdr_info,
		    struct ultra_vbus_deviceinfo *info)
{
	int off = sizeof(struct channel_header) + hdr_info->bus_info_offset;

	if (hdr_info->bus_info_offset == 0)
		return;

	visorchannel_write(chan, off, info, sizeof(*info));
}

/*
 * write_vbus_dev_info() - write the contents of <info> to the struct
 *                         spar_vbus_channel_protocol.dev_info[<devix>]
 * @chan:     indentifies the s-Par channel that will be updated
 * @hdr_info: used to find appropriate channel offset to write data
 * @info:     contains the information to write
 * @devix:    the relative device number (0..n-1) of the device on the bus
 *
 * Writes device info into the channel memory to be used for diagnostic
 * purposes.
 *
 * Returns no value since this is debug information and not needed for
 * device functionality.
 */
static void
write_vbus_dev_info(struct visorchannel *chan,
		    struct spar_vbus_headerinfo *hdr_info,
		    struct ultra_vbus_deviceinfo *info, unsigned int devix)
{
	int off =
	    (sizeof(struct channel_header) + hdr_info->dev_info_offset) +
	    (hdr_info->device_info_struct_bytes * devix);

	if (hdr_info->dev_info_offset == 0)
		return;

	visorchannel_write(chan, off, info, sizeof(*info));
}

/*
 * fix_vbus_dev_info() - for a child device just created on a client bus, fill
 *                       in information about the driver that is controlling
 *                       this device into the the appropriate slot within the
 *                       vbus channel of the bus instance
 * @visordev: struct visor_device for the desired device
 */
static void
fix_vbus_dev_info(struct visor_device *visordev)
{
	int i;
	struct visor_device *bdev;
	struct visor_driver *visordrv;
	u32 bus_no = visordev->chipset_bus_no;
	u32 dev_no = visordev->chipset_dev_no;
	struct ultra_vbus_deviceinfo dev_info;
	const char *chan_type_name = NULL;
	struct spar_vbus_headerinfo *hdr_info;

	if (!visordev->device.driver)
		return;

	bdev = visorbus_get_device_by_id(bus_no, BUS_ROOT_DEVICE, NULL);
	if (!bdev)
		return;
	hdr_info = (struct spar_vbus_headerinfo *)bdev->vbus_hdr_info;
	if (!hdr_info)
		return;
	visordrv = to_visor_driver(visordev->device.driver);

	/*
	 * Within the list of device types (by GUID) that the driver
	 * says it supports, find out which one of those types matches
	 * the type of this device, so that we can include the device
	 * type name
	 */
	for (i = 0; visordrv->channel_types[i].name; i++) {
		if (memcmp(&visordrv->channel_types[i].guid,
			   &visordev->channel_type_guid,
			   sizeof(visordrv->channel_types[i].guid)) == 0) {
			chan_type_name = visordrv->channel_types[i].name;
			break;
		}
	}

	bus_device_info_init(&dev_info, chan_type_name, visordrv->name);
	write_vbus_dev_info(bdev->visorchannel, hdr_info, &dev_info, dev_no);

	/*
	 * Re-write bus+chipset info, because it is possible that this
	 * was previously written by our evil counterpart, virtpci.
	 */
	write_vbus_chp_info(bdev->visorchannel, hdr_info, &chipset_driverinfo);
	write_vbus_bus_info(bdev->visorchannel, hdr_info,
			    &clientbus_driverinfo);
}

/*
 * visordriver_probe_device() - handle new visor device coming online
 * @xdev: struct device for the visor device being probed
 *
 * This is called automatically upon adding a visor_device (device_add), or
 * adding a visor_driver (visorbus_register_visor_driver), but only after
 * visorbus_match() has returned 1 to indicate a successful match between
 * driver and device.
 *
 * If successful, a reference to the device will be held onto via get_device().
 *
 * Return: 0 if successful, meaning the function driver's probe() function
 *         was successful with this device, otherwise a negative errno
 *         value indicating failure reason
 */
static int
visordriver_probe_device(struct device *xdev)
{
	int res;
	struct visor_driver *drv;
	struct visor_device *dev;

	drv = to_visor_driver(xdev->driver);
	dev = to_visor_device(xdev);

	if (!drv->probe)
		return -ENODEV;

	mutex_lock(&dev->visordriver_callback_lock);
	dev->being_removed = false;

	res = drv->probe(dev);
	if (res >= 0) {
		/* success: reference kept via unmatched get_device() */
		get_device(&dev->device);
		fix_vbus_dev_info(dev);
	}

	mutex_unlock(&dev->visordriver_callback_lock);
	return res;
}

/**
 * visorbus_register_visor_driver() - registers the provided visor driver
 *                                    for handling one or more visor device
 *                                    types (channel_types)
 * @drv: the driver to register
 *
 * A visor function driver calls this function to register
 * the driver.  The caller MUST fill in the following fields within the
 * #drv structure:
 *     name, version, owner, channel_types, probe, remove
 *
 * Here's how the whole Linux bus / driver / device model works.
 *
 * At system start-up, the visorbus kernel module is loaded, which registers
 * visorbus_type as a bus type, using bus_register().
 *
 * All kernel modules that support particular device types on a
 * visorbus bus are loaded.  Each of these kernel modules calls
 * visorbus_register_visor_driver() in their init functions, passing a
 * visor_driver struct.  visorbus_register_visor_driver() in turn calls
 * register_driver(&visor_driver.driver).  This .driver member is
 * initialized with generic methods (like probe), whose sole responsibility
 * is to act as a broker for the real methods, which are within the
 * visor_driver struct.  (This is the way the subclass behavior is
 * implemented, since visor_driver is essentially a subclass of the
 * generic driver.)  Whenever a driver_register() happens, core bus code in
 * the kernel does (see device_attach() in drivers/base/dd.c):
 *
 *     for each dev associated with the bus (the bus that driver is on) that
 *     does not yet have a driver
 *         if bus.match(dev,newdriver) == yes_matched  ** .match specified
 *                                                ** during bus_register().
 *             newdriver.probe(dev)  ** for visor drivers, this will call
 *                   ** the generic driver.probe implemented in visorbus.c,
 *                   ** which in turn calls the probe specified within the
 *                   ** struct visor_driver (which was specified by the
 *                   ** actual device driver as part of
 *                   ** visorbus_register_visor_driver()).
 *
 * The above dance also happens when a new device appears.
 * So the question is, how are devices created within the system?
 * Basically, just call device_add(dev).  See pci_bus_add_devices().
 * pci_scan_device() shows an example of how to build a device struct.  It
 * returns the newly-created struct to pci_scan_single_device(), who adds it
 * to the list of devices at PCIBUS.devices.  That list of devices is what
 * is traversed by pci_bus_add_devices().
 *
 * Return: integer indicating success (zero) or failure (non-zero)
 */
int visorbus_register_visor_driver(struct visor_driver *drv)
{
	int rc = 0;

	if (busreg_rc < 0)
		return -ENODEV; /*can't register on a nonexistent bus*/

	drv->driver.name = drv->name;
	drv->driver.bus = &visorbus_type;
	drv->driver.probe = visordriver_probe_device;
	drv->driver.remove = visordriver_remove_device;
	drv->driver.owner = drv->owner;

	/*
	 * driver_register does this:
	 *   bus_add_driver(drv)
	 *   ->if (drv.bus)  ** (bus_type) **
	 *       driver_attach(drv)
	 *         for each dev with bus type of drv.bus
	 *           if (!dev.drv)  ** no driver assigned yet **
	 *             if (bus.match(dev,drv))  [visorbus_match]
	 *               dev.drv = drv
	 *               if (!drv.probe(dev))   [visordriver_probe_device]
	 *                 dev.drv = NULL
	 */

	rc = driver_register(&drv->driver);
	if (rc < 0)
		driver_unregister(&drv->driver);
	return rc;
}
EXPORT_SYMBOL_GPL(visorbus_register_visor_driver);

/*
 * create_bus_instance() - create a device instance for the visor bus itself
 * @dev: struct visor_device indicating the bus instance
 *
 * Return: 0 for success, otherwise negative errno value indicating reason for
 *         failure
 */
static int
create_bus_instance(struct visor_device *dev)
{
	int id = dev->chipset_bus_no;
	int err;
	struct spar_vbus_headerinfo *hdr_info;

	POSTCODE_LINUX(BUS_CREATE_ENTRY_PC, 0, 0, DIAG_SEVERITY_PRINT);

	hdr_info = kzalloc(sizeof(*hdr_info), GFP_KERNEL);
	if (!hdr_info)
		return -ENOMEM;

	dev_set_name(&dev->device, "visorbus%d", id);
	dev->device.bus = &visorbus_type;
	dev->device.groups = visorbus_groups;
	dev->device.release = visorbus_release_busdevice;

	dev->debugfs_dir = debugfs_create_dir(dev_name(&dev->device),
					      visorbus_debugfs_dir);
	if (!dev->debugfs_dir) {
		err = -ENOMEM;
		goto err_hdr_info;
	}
	dev->debugfs_client_bus_info =
		debugfs_create_file("client_bus_info", 0440,
				    dev->debugfs_dir, dev,
				    &client_bus_info_debugfs_fops);
	if (!dev->debugfs_client_bus_info) {
		err = -ENOMEM;
		goto err_debugfs_dir;
	}

	if (device_register(&dev->device) < 0) {
		POSTCODE_LINUX(DEVICE_CREATE_FAILURE_PC, 0, id,
			       DIAG_SEVERITY_ERR);
		err = -ENODEV;
		goto err_debugfs_created;
	}

	if (get_vbus_header_info(dev->visorchannel, hdr_info) >= 0) {
		dev->vbus_hdr_info = (void *)hdr_info;
		write_vbus_chp_info(dev->visorchannel, hdr_info,
				    &chipset_driverinfo);
		write_vbus_bus_info(dev->visorchannel, hdr_info,
				    &clientbus_driverinfo);
	} else {
		kfree(hdr_info);
	}
	list_add_tail(&dev->list_all, &list_all_bus_instances);
	dev_set_drvdata(&dev->device, dev);
	return 0;

err_debugfs_created:
	debugfs_remove(dev->debugfs_client_bus_info);

err_debugfs_dir:
	debugfs_remove_recursive(dev->debugfs_dir);

err_hdr_info:
	kfree(hdr_info);
	return err;
}

/*
 * remove_bus_instance() - remove a device instance for the visor bus itself
 * @dev: struct visor_device indentifying the bus to remove
 */
static void
remove_bus_instance(struct visor_device *dev)
{
	/*
	 * Note that this will result in the release method for
	 * dev->dev being called, which will call
	 * visorbus_release_busdevice().  This has something to do with
	 * the put_device() done in device_unregister(), but I have never
	 * successfully been able to trace thru the code to see where/how
	 * release() gets called.  But I know it does.
	 */
	if (dev->visorchannel) {
		visorchannel_destroy(dev->visorchannel);
		dev->visorchannel = NULL;
	}
	kfree(dev->vbus_hdr_info);
	list_del(&dev->list_all);
	device_unregister(&dev->device);
}

/*
 * create_bus_type() - create and register the one-and-only one instance of
 *                     the visor bus type (visorbus_type)
 * Return: 0 for success, otherwise negative errno value returned by
 *         bus_register() indicating the reason for failure
 */
static int
create_bus_type(void)
{
	busreg_rc = bus_register(&visorbus_type);
	return busreg_rc;
}

/*
 * remove_bus_type() - remove the one-and-only one instance of the visor bus
 *                     type (visorbus_type)
 */
static void
remove_bus_type(void)
{
	bus_unregister(&visorbus_type);
}

/*
 * remove_all_visor_devices() - remove all child visor bus device instances
 */
static void
remove_all_visor_devices(void)
{
	struct list_head *listentry, *listtmp;

	list_for_each_safe(listentry, listtmp, &list_all_device_instances) {
		struct visor_device *dev = list_entry(listentry,
						      struct visor_device,
						      list_all);
		remove_visor_device(dev);
	}
}

void
chipset_bus_create(struct visor_device *dev)
{
	int rc;
	u32 bus_no = dev->chipset_bus_no;

	POSTCODE_LINUX(BUS_CREATE_ENTRY_PC, 0, bus_no, DIAG_SEVERITY_PRINT);
	rc = create_bus_instance(dev);
	POSTCODE_LINUX(BUS_CREATE_EXIT_PC, 0, bus_no, DIAG_SEVERITY_PRINT);

	if (rc < 0)
		POSTCODE_LINUX(BUS_CREATE_FAILURE_PC, 0, bus_no,
			       DIAG_SEVERITY_ERR);
	else
		POSTCODE_LINUX(CHIPSET_INIT_SUCCESS_PC, 0, bus_no,
			       DIAG_SEVERITY_PRINT);

	bus_create_response(dev, rc);
}

void
chipset_bus_destroy(struct visor_device *dev)
{
	remove_bus_instance(dev);
	bus_destroy_response(dev, 0);
}

void
chipset_device_create(struct visor_device *dev_info)
{
	int rc;
	u32 bus_no = dev_info->chipset_bus_no;
	u32 dev_no = dev_info->chipset_dev_no;

	POSTCODE_LINUX(DEVICE_CREATE_ENTRY_PC, dev_no, bus_no,
		       DIAG_SEVERITY_PRINT);

	rc = create_visor_device(dev_info);
	device_create_response(dev_info, rc);

	if (rc < 0)
		POSTCODE_LINUX(DEVICE_CREATE_FAILURE_PC, dev_no, bus_no,
			       DIAG_SEVERITY_ERR);
	else
		POSTCODE_LINUX(DEVICE_CREATE_SUCCESS_PC, dev_no, bus_no,
			       DIAG_SEVERITY_PRINT);
}

void
chipset_device_destroy(struct visor_device *dev_info)
{
	remove_visor_device(dev_info);

	device_destroy_response(dev_info, 0);
}

/*
 * pause_state_change_complete() - the callback function to be called by a
 *                                 visorbus function driver when a
 *                                 pending "pause device" operation has
 *                                 completed
 * @dev: struct visor_device identifying the paused device
 * @status: 0 iff the pause state change completed successfully, otherwise
 *          a negative errno value indicating the reason for failure
 */
static void
pause_state_change_complete(struct visor_device *dev, int status)
{
	if (!dev->pausing)
		return;

	dev->pausing = false;

	device_pause_response(dev, status);
}

/*
 * resume_state_change_complete() - the callback function to be called by a
 *                                  visorbus function driver when a
 *                                  pending "resume device" operation has
 *                                  completed
 * @dev: struct visor_device identifying the resumed device
 * @status: 0 iff the resume state change completed successfully, otherwise
 *          a negative errno value indicating the reason for failure
 */
static void
resume_state_change_complete(struct visor_device *dev, int status)
{
	if (!dev->resuming)
		return;

	dev->resuming = false;

	/*
	 * Notify the chipset driver that the resume is complete,
	 * which will presumably want to send some sort of response to
	 * the initiator.
	 */
	device_resume_response(dev, status);
}

/*
 * initiate_chipset_device_pause_resume() - start a pause or resume operation
 *                                          for a visor device
 * @dev: struct visor_device identifying the device being paused or resumed
 * @is_pause: true to indicate pause operation, false to indicate resume
 *
 * Tell the subordinate function driver for a specific device to pause
 * or resume that device.  Success/failure result is returned asynchronously
 * via a callback function; see pause_state_change_complete() and
 * resume_state_change_complete().
 */
static void
initiate_chipset_device_pause_resume(struct visor_device *dev, bool is_pause)
{
	int rc;
	struct visor_driver *drv = NULL;
	void (*notify_func)(struct visor_device *dev, int response) = NULL;

	if (is_pause)
		notify_func = device_pause_response;
	else
		notify_func = device_resume_response;
	if (!notify_func)
		return;

	drv = to_visor_driver(dev->device.driver);
	if (!drv) {
		(*notify_func)(dev, -ENODEV);
		return;
	}

	if (dev->pausing || dev->resuming) {
		(*notify_func)(dev, -EBUSY);
		return;
	}

	/*
	 * Note that even though both drv->pause() and drv->resume
	 * specify a callback function, it is NOT necessary for us to
	 * increment our local module usage count.  Reason is, there
	 * is already a linkage dependency between child function
	 * drivers and visorbus, so it is already IMPOSSIBLE to unload
	 * visorbus while child function drivers are still running.
	 */
	if (is_pause) {
		if (!drv->pause) {
			(*notify_func)(dev, -EINVAL);
			return;
		}

		dev->pausing = true;
		rc = drv->pause(dev, pause_state_change_complete);
	} else {
		/* This should be done at BUS resume time, but an
		 * existing problem prevents us from ever getting a bus
		 * resume...  This hack would fail to work should we
		 * ever have a bus that contains NO devices, since we
		 * would never even get here in that case.
		 */
		fix_vbus_dev_info(dev);
		if (!drv->resume) {
			(*notify_func)(dev, -EINVAL);
			return;
		}

		dev->resuming = true;
		rc = drv->resume(dev, resume_state_change_complete);
	}
	if (rc < 0) {
		if (is_pause)
			dev->pausing = false;
		else
			dev->resuming = false;
		(*notify_func)(dev, -EINVAL);
	}
}

/**
 * chipset_device_pause() - start a pause operation for a visor device
 * @dev_info: struct visor_device identifying the device being paused
 *
 * Tell the subordinate function driver for a specific device to pause
 * that device.  Success/failure result is returned asynchronously
 * via a callback function; see pause_state_change_complete().
 */
void
chipset_device_pause(struct visor_device *dev_info)
{
	initiate_chipset_device_pause_resume(dev_info, true);
}

/**
 * chipset_device_resume() - start a resume operation for a visor device
 * @dev_info: struct visor_device identifying the device being resumed
 *
 * Tell the subordinate function driver for a specific device to resume
 * that device.  Success/failure result is returned asynchronously
 * via a callback function; see resume_state_change_complete().
 */
void
chipset_device_resume(struct visor_device *dev_info)
{
	initiate_chipset_device_pause_resume(dev_info, false);
}

int
visorbus_init(void)
{
	int err;

	POSTCODE_LINUX(DRIVER_ENTRY_PC, 0, 0, DIAG_SEVERITY_PRINT);

	visorbus_debugfs_dir = debugfs_create_dir("visorbus", NULL);
	if (!visorbus_debugfs_dir)
		return -ENOMEM;

	bus_device_info_init(&clientbus_driverinfo, "clientbus", "visorbus");

	err = create_bus_type();
	if (err < 0) {
		POSTCODE_LINUX(BUS_CREATE_ENTRY_PC, 0, 0, DIAG_SEVERITY_ERR);
		goto error;
	}

	bus_device_info_init(&chipset_driverinfo, "chipset", "visorchipset");

	return 0;

error:
	POSTCODE_LINUX(CHIPSET_INIT_FAILURE_PC, 0, err, DIAG_SEVERITY_ERR);
	return err;
}

void
visorbus_exit(void)
{
	struct list_head *listentry, *listtmp;

	remove_all_visor_devices();

	list_for_each_safe(listentry, listtmp, &list_all_bus_instances) {
		struct visor_device *dev = list_entry(listentry,
						      struct visor_device,
						      list_all);
		remove_bus_instance(dev);
	}
	remove_bus_type();
	debugfs_remove_recursive(visorbus_debugfs_dir);
}

module_param_named(forcematch, visorbus_forcematch, int, 0444);
MODULE_PARM_DESC(visorbus_forcematch,
		 "1 to force a successful dev <--> drv match");

module_param_named(forcenomatch, visorbus_forcenomatch, int, 0444);
MODULE_PARM_DESC(visorbus_forcenomatch,
		 "1 to force an UNsuccessful dev <--> drv match");
