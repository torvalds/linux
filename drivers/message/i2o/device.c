/*
 *	Functions to handle I2O devices
 *
 *	Copyright (C) 2004	Markus Lidel <Markus.Lidel@shadowconnect.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	Fixes/additions:
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>
 *			initial version.
 */

#include <linux/module.h>
#include <linux/i2o.h>
#include <linux/delay.h>
#include "core.h"

/**
 *	i2o_device_issue_claim - claim or release a device
 *	@dev: I2O device to claim or release
 *	@cmd: claim or release command
 *	@type: type of claim
 *
 *	Issue I2O UTIL_CLAIM or UTIL_RELEASE messages. The message to be sent
 *	is set by cmd. dev is the I2O device which should be claim or
 *	released and the type is the claim type (see the I2O spec).
 *
 *	Returs 0 on success or negative error code on failure.
 */
static inline int i2o_device_issue_claim(struct i2o_device *dev, u32 cmd,
					 u32 type)
{
	struct i2o_message __iomem *msg;
	u32 m;

	m = i2o_msg_get_wait(dev->iop, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FIVE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(cmd << 24 | HOST_TID << 12 | dev->lct_data.tid, &msg->u.head[1]);
	writel(type, &msg->body[0]);

	return i2o_msg_post_wait(dev->iop, m, 60);
};

/**
 * 	i2o_device_claim - claim a device for use by an OSM
 *	@dev: I2O device to claim
 *	@drv: I2O driver which wants to claim the device
 *
 *	Do the leg work to assign a device to a given OSM. If the claim succeed
 *	the owner of the rimary. If the attempt fails a negative errno code
 *	is returned. On success zero is returned.
 */
int i2o_device_claim(struct i2o_device *dev)
{
	int rc = 0;

	down(&dev->lock);

	rc = i2o_device_issue_claim(dev, I2O_CMD_UTIL_CLAIM, I2O_CLAIM_PRIMARY);
	if (!rc)
		pr_debug("i2o: claim of device %d succeded\n",
			 dev->lct_data.tid);
	else
		pr_debug("i2o: claim of device %d failed %d\n",
			 dev->lct_data.tid, rc);

	up(&dev->lock);

	return rc;
};

/**
 *	i2o_device_claim_release - release a device that the OSM is using
 *	@dev: device to release
 *	@drv: driver which claimed the device
 *
 *	Drop a claim by an OSM on a given I2O device.
 *
 *	AC - some devices seem to want to refuse an unclaim until they have
 *	finished internal processing. It makes sense since you don't want a
 *	new device to go reconfiguring the entire system until you are done.
 *	Thus we are prepared to wait briefly.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_device_claim_release(struct i2o_device *dev)
{
	int tries;
	int rc = 0;

	down(&dev->lock);

	/*
	 *      If the controller takes a nonblocking approach to
	 *      releases we have to sleep/poll for a few times.
	 */
	for (tries = 0; tries < 10; tries++) {
		rc = i2o_device_issue_claim(dev, I2O_CMD_UTIL_RELEASE,
					    I2O_CLAIM_PRIMARY);
		if (!rc)
			break;

		ssleep(1);
	}

	if (!rc)
		pr_debug("i2o: claim release of device %d succeded\n",
			 dev->lct_data.tid);
	else
		pr_debug("i2o: claim release of device %d failed %d\n",
			 dev->lct_data.tid, rc);

	up(&dev->lock);

	return rc;
};

/**
 *	i2o_device_release - release the memory for a I2O device
 *	@dev: I2O device which should be released
 *
 *	Release the allocated memory. This function is called if refcount of
 *	device reaches 0 automatically.
 */
static void i2o_device_release(struct device *dev)
{
	struct i2o_device *i2o_dev = to_i2o_device(dev);

	pr_debug("i2o: device %s released\n", dev->bus_id);

	kfree(i2o_dev);
};

/**
 *	i2o_device_class_release - Remove I2O device attributes
 *	@cd: I2O class device which is added to the I2O device class
 *
 *	Removes attributes from the I2O device again. Also search each device
 *	on the controller for I2O devices which refert to this device as parent
 *	or user and remove this links also.
 */
static void i2o_device_class_release(struct class_device *cd)
{
	struct i2o_device *i2o_dev, *tmp;
	struct i2o_controller *c;

	i2o_dev = to_i2o_device(cd->dev);
	c = i2o_dev->iop;

	sysfs_remove_link(&i2o_dev->device.kobj, "parent");
	sysfs_remove_link(&i2o_dev->device.kobj, "user");

	list_for_each_entry(tmp, &c->devices, list) {
		if (tmp->lct_data.parent_tid == i2o_dev->lct_data.tid)
			sysfs_remove_link(&tmp->device.kobj, "parent");
		if (tmp->lct_data.user_tid == i2o_dev->lct_data.tid)
			sysfs_remove_link(&tmp->device.kobj, "user");
	}
};

/* I2O device class */
static struct class i2o_device_class = {
	.name = "i2o_device",
	.release = i2o_device_class_release
};

/**
 *	i2o_device_alloc - Allocate a I2O device and initialize it
 *
 *	Allocate the memory for a I2O device and initialize locks and lists
 *
 *	Returns the allocated I2O device or a negative error code if the device
 *	could not be allocated.
 */
static struct i2o_device *i2o_device_alloc(void)
{
	struct i2o_device *dev;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	memset(dev, 0, sizeof(*dev));

	INIT_LIST_HEAD(&dev->list);
	init_MUTEX(&dev->lock);

	dev->device.bus = &i2o_bus_type;
	dev->device.release = &i2o_device_release;
	dev->classdev.class = &i2o_device_class;
	dev->classdev.dev = &dev->device;

	return dev;
};

/**
 *	i2o_device_add - allocate a new I2O device and add it to the IOP
 *	@iop: I2O controller where the device is on
 *	@entry: LCT entry of the I2O device
 *
 *	Allocate a new I2O device and initialize it with the LCT entry. The
 *	device is appended to the device list of the controller.
 *
 *	Returns a pointer to the I2O device on success or negative error code
 *	on failure.
 */
static struct i2o_device *i2o_device_add(struct i2o_controller *c,
					 i2o_lct_entry * entry)
{
	struct i2o_device *dev;

	dev = i2o_device_alloc();
	if (IS_ERR(dev)) {
		printk(KERN_ERR "i2o: unable to allocate i2o device\n");
		return dev;
	}

	dev->lct_data = *entry;

	snprintf(dev->device.bus_id, BUS_ID_SIZE, "%d:%03x", c->unit,
		 dev->lct_data.tid);

	snprintf(dev->classdev.class_id, BUS_ID_SIZE, "%d:%03x", c->unit,
		 dev->lct_data.tid);

	dev->iop = c;
	dev->device.parent = &c->device;

	device_register(&dev->device);

	list_add_tail(&dev->list, &c->devices);

	class_device_register(&dev->classdev);

	i2o_driver_notify_device_add_all(dev);

	pr_debug("i2o: device %s added\n", dev->device.bus_id);

	return dev;
};

/**
 *	i2o_device_remove - remove an I2O device from the I2O core
 *	@dev: I2O device which should be released
 *
 *	Is used on I2O controller removal or LCT modification, when the device
 *	is removed from the system. Note that the device could still hang
 *	around until the refcount reaches 0.
 */
void i2o_device_remove(struct i2o_device *i2o_dev)
{
	i2o_driver_notify_device_remove_all(i2o_dev);
	class_device_unregister(&i2o_dev->classdev);
	list_del(&i2o_dev->list);
	device_unregister(&i2o_dev->device);
};

/**
 *	i2o_device_parse_lct - Parse a previously fetched LCT and create devices
 *	@c: I2O controller from which the LCT should be parsed.
 *
 *	The Logical Configuration Table tells us what we can talk to on the
 *	board. For every entry we create an I2O device, which is registered in
 *	the I2O core.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_device_parse_lct(struct i2o_controller *c)
{
	struct i2o_device *dev, *tmp;
	i2o_lct *lct;
	int i;
	int max;

	down(&c->lct_lock);

	kfree(c->lct);

	lct = c->dlct.virt;

	c->lct = kmalloc(lct->table_size * 4, GFP_KERNEL);
	if (!c->lct) {
		up(&c->lct_lock);
		return -ENOMEM;
	}

	if (lct->table_size * 4 > c->dlct.len) {
		memcpy(c->lct, c->dlct.virt, c->dlct.len);
		up(&c->lct_lock);
		return -EAGAIN;
	}

	memcpy(c->lct, c->dlct.virt, lct->table_size * 4);

	lct = c->lct;

	max = (lct->table_size - 3) / 9;

	pr_debug("%s: LCT has %d entries (LCT size: %d)\n", c->name, max,
		 lct->table_size);

	/* remove devices, which are not in the LCT anymore */
	list_for_each_entry_safe(dev, tmp, &c->devices, list) {
		int found = 0;

		for (i = 0; i < max; i++) {
			if (lct->lct_entry[i].tid == dev->lct_data.tid) {
				found = 1;
				break;
			}
		}

		if (!found)
			i2o_device_remove(dev);
	}

	/* add new devices, which are new in the LCT */
	for (i = 0; i < max; i++) {
		int found = 0;

		list_for_each_entry_safe(dev, tmp, &c->devices, list) {
			if (lct->lct_entry[i].tid == dev->lct_data.tid) {
				found = 1;
				break;
			}
		}

		if (!found)
			i2o_device_add(c, &lct->lct_entry[i]);
	}
	up(&c->lct_lock);

	return 0;
};

/**
 *	i2o_device_class_show_class_id - Displays class id of I2O device
 *	@cd: class device of which the class id should be displayed
 *	@buf: buffer into which the class id should be printed
 *
 *	Returns the number of bytes which are printed into the buffer.
 */
static ssize_t i2o_device_class_show_class_id(struct class_device *cd,
					      char *buf)
{
	struct i2o_device *dev = to_i2o_device(cd->dev);

	sprintf(buf, "0x%03x\n", dev->lct_data.class_id);
	return strlen(buf) + 1;
};

/**
 *	i2o_device_class_show_tid - Displays TID of I2O device
 *	@cd: class device of which the TID should be displayed
 *	@buf: buffer into which the class id should be printed
 *
 *	Returns the number of bytes which are printed into the buffer.
 */
static ssize_t i2o_device_class_show_tid(struct class_device *cd, char *buf)
{
	struct i2o_device *dev = to_i2o_device(cd->dev);

	sprintf(buf, "0x%03x\n", dev->lct_data.tid);
	return strlen(buf) + 1;
};

/* I2O device class attributes */
static CLASS_DEVICE_ATTR(class_id, S_IRUGO, i2o_device_class_show_class_id,
			 NULL);
static CLASS_DEVICE_ATTR(tid, S_IRUGO, i2o_device_class_show_tid, NULL);

/**
 *	i2o_device_class_add - Adds attributes to the I2O device
 *	@cd: I2O class device which is added to the I2O device class
 *
 *	This function get called when a I2O device is added to the class. It
 *	creates the attributes for each device and creates user/parent symlink
 *	if necessary.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_device_class_add(struct class_device *cd)
{
	struct i2o_device *i2o_dev, *tmp;
	struct i2o_controller *c;

	i2o_dev = to_i2o_device(cd->dev);
	c = i2o_dev->iop;

	class_device_create_file(cd, &class_device_attr_class_id);
	class_device_create_file(cd, &class_device_attr_tid);

	/* create user entries for this device */
	tmp = i2o_iop_find_device(i2o_dev->iop, i2o_dev->lct_data.user_tid);
	if (tmp && (tmp != i2o_dev))
		sysfs_create_link(&i2o_dev->device.kobj, &tmp->device.kobj,
				  "user");

	/* create user entries refering to this device */
	list_for_each_entry(tmp, &c->devices, list)
	    if ((tmp->lct_data.user_tid == i2o_dev->lct_data.tid)
		&& (tmp != i2o_dev))
		sysfs_create_link(&tmp->device.kobj,
				  &i2o_dev->device.kobj, "user");

	/* create parent entries for this device */
	tmp = i2o_iop_find_device(i2o_dev->iop, i2o_dev->lct_data.parent_tid);
	if (tmp && (tmp != i2o_dev))
		sysfs_create_link(&i2o_dev->device.kobj, &tmp->device.kobj,
				  "parent");

	/* create parent entries refering to this device */
	list_for_each_entry(tmp, &c->devices, list)
	    if ((tmp->lct_data.parent_tid == i2o_dev->lct_data.tid)
		&& (tmp != i2o_dev))
		sysfs_create_link(&tmp->device.kobj,
				  &i2o_dev->device.kobj, "parent");

	return 0;
};

/* I2O device class interface */
static struct class_interface i2o_device_class_interface = {
	.class = &i2o_device_class,
	.add = i2o_device_class_add
};

/*
 *	Run time support routines
 */

/*	Issue UTIL_PARAMS_GET or UTIL_PARAMS_SET
 *
 *	This function can be used for all UtilParamsGet/Set operations.
 *	The OperationList is given in oplist-buffer,
 *	and results are returned in reslist-buffer.
 *	Note that the minimum sized reslist is 8 bytes and contains
 *	ResultCount, ErrorInfoSize, BlockStatus and BlockSize.
 */
int i2o_parm_issue(struct i2o_device *i2o_dev, int cmd, void *oplist,
			  int oplen, void *reslist, int reslen)
{
	struct i2o_message __iomem *msg;
	u32 m;
	u32 *res32 = (u32 *) reslist;
	u32 *restmp = (u32 *) reslist;
	int len = 0;
	int i = 0;
	int rc;
	struct i2o_dma res;
	struct i2o_controller *c = i2o_dev->iop;
	struct device *dev = &c->pdev->dev;

	res.virt = NULL;

	if (i2o_dma_alloc(dev, &res, reslen, GFP_KERNEL))
		return -ENOMEM;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY) {
		i2o_dma_free(dev, &res);
		return -ETIMEDOUT;
	}

	i = 0;
	writel(cmd << 24 | HOST_TID << 12 | i2o_dev->lct_data.tid,
	       &msg->u.head[1]);
	writel(0, &msg->body[i++]);
	writel(0x4C000000 | oplen, &msg->body[i++]);	/* OperationList */
	memcpy_toio(&msg->body[i], oplist, oplen);
	i += (oplen / 4 + (oplen % 4 ? 1 : 0));
	writel(0xD0000000 | res.len, &msg->body[i++]);	/* ResultList */
	writel(res.phys, &msg->body[i++]);

	writel(I2O_MESSAGE_SIZE(i + sizeof(struct i2o_message) / 4) |
	       SGL_OFFSET_5, &msg->u.head[0]);

	rc = i2o_msg_post_wait_mem(c, m, 10, &res);

	/* This only looks like a memory leak - don't "fix" it. */
	if (rc == -ETIMEDOUT)
		return rc;

	memcpy(reslist, res.virt, res.len);
	i2o_dma_free(dev, &res);

	/* Query failed */
	if (rc)
		return rc;
	/*
	 * Calculate number of bytes of Result LIST
	 * We need to loop through each Result BLOCK and grab the length
	 */
	restmp = res32 + 1;
	len = 1;
	for (i = 0; i < (res32[0] & 0X0000FFFF); i++) {
		if (restmp[0] & 0x00FF0000) {	/* BlockStatus != SUCCESS */
			printk(KERN_WARNING
			       "%s - Error:\n  ErrorInfoSize = 0x%02x, "
			       "BlockStatus = 0x%02x, BlockSize = 0x%04x\n",
			       (cmd ==
				I2O_CMD_UTIL_PARAMS_SET) ? "PARAMS_SET" :
			       "PARAMS_GET", res32[1] >> 24,
			       (res32[1] >> 16) & 0xFF, res32[1] & 0xFFFF);

			/*
			 *      If this is the only request,than we return an error
			 */
			if ((res32[0] & 0x0000FFFF) == 1) {
				return -((res32[1] >> 16) & 0xFF);	/* -BlockStatus */
			}
		}
		len += restmp[0] & 0x0000FFFF;	/* Length of res BLOCK */
		restmp += restmp[0] & 0x0000FFFF;	/* Skip to next BLOCK */
	}
	return (len << 2);	/* bytes used by result list */
}

/*
 *	 Query one field group value or a whole scalar group.
 */
int i2o_parm_field_get(struct i2o_device *i2o_dev, int group, int field,
		       void *buf, int buflen)
{
	u16 opblk[] = { 1, 0, I2O_PARAMS_FIELD_GET, group, 1, field };
	u8 *resblk;		/* 8 bytes for header */
	int size;

	if (field == -1)	/* whole group */
		opblk[4] = -1;

	resblk = kmalloc(buflen + 8, GFP_KERNEL | GFP_ATOMIC);
	if (!resblk)
		return -ENOMEM;

	size = i2o_parm_issue(i2o_dev, I2O_CMD_UTIL_PARAMS_GET, opblk,
			      sizeof(opblk), resblk, buflen + 8);

	memcpy(buf, resblk + 8, buflen);	/* cut off header */

	kfree(resblk);

	if (size > buflen)
		return buflen;

	return size;
}

/*
 * 	if oper == I2O_PARAMS_TABLE_GET, get from all rows
 * 		if fieldcount == -1 return all fields
 *			ibuf and ibuflen are unused (use NULL, 0)
 * 		else return specific fields
 *  			ibuf contains fieldindexes
 *
 * 	if oper == I2O_PARAMS_LIST_GET, get from specific rows
 * 		if fieldcount == -1 return all fields
 *			ibuf contains rowcount, keyvalues
 * 		else return specific fields
 *			fieldcount is # of fieldindexes
 *  			ibuf contains fieldindexes, rowcount, keyvalues
 *
 *	You could also use directly function i2o_issue_params().
 */
int i2o_parm_table_get(struct i2o_device *dev, int oper, int group,
		       int fieldcount, void *ibuf, int ibuflen, void *resblk,
		       int reslen)
{
	u16 *opblk;
	int size;

	size = 10 + ibuflen;
	if (size % 4)
		size += 4 - size % 4;

	opblk = kmalloc(size, GFP_KERNEL);
	if (opblk == NULL) {
		printk(KERN_ERR "i2o: no memory for query buffer.\n");
		return -ENOMEM;
	}

	opblk[0] = 1;		/* operation count */
	opblk[1] = 0;		/* pad */
	opblk[2] = oper;
	opblk[3] = group;
	opblk[4] = fieldcount;
	memcpy(opblk + 5, ibuf, ibuflen);	/* other params */

	size = i2o_parm_issue(dev, I2O_CMD_UTIL_PARAMS_GET, opblk,
			      size, resblk, reslen);

	kfree(opblk);
	if (size > reslen)
		return reslen;

	return size;
}

/**
 *	i2o_device_init - Initialize I2O devices
 *
 *	Registers the I2O device class.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_device_init(void)
{
	int rc;

	rc = class_register(&i2o_device_class);
	if (rc)
		return rc;

	return class_interface_register(&i2o_device_class_interface);
};

/**
 *	i2o_device_exit - I2O devices exit function
 *
 *	Unregisters the I2O device class.
 */
void i2o_device_exit(void)
{
	class_interface_register(&i2o_device_class_interface);
	class_unregister(&i2o_device_class);
};

EXPORT_SYMBOL(i2o_device_claim);
EXPORT_SYMBOL(i2o_device_claim_release);
EXPORT_SYMBOL(i2o_parm_field_get);
EXPORT_SYMBOL(i2o_parm_table_get);
EXPORT_SYMBOL(i2o_parm_issue);
