/*
 *	Configuration OSM
 *
 *	Copyright (C) 2005	Markus Lidel <Markus.Lidel@shadowconnect.com>
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
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

#define OSM_NAME	"config-osm"
#define OSM_VERSION	"1.248"
#define OSM_DESCRIPTION	"I2O Configuration OSM"

/* access mode user rw */
#define S_IWRSR (S_IRUSR | S_IWUSR)

static struct i2o_driver i2o_config_driver;

/* Special file operations for sysfs */
struct fops_attribute {
	struct bin_attribute bin;
	struct file_operations fops;
};

/**
 *	sysfs_read_dummy
 */
static ssize_t sysfs_read_dummy(struct kobject *kobj, char *buf, loff_t offset,
				size_t count)
{
	return 0;
};

/**
 *	sysfs_write_dummy
 */
static ssize_t sysfs_write_dummy(struct kobject *kobj, char *buf, loff_t offset,
				 size_t count)
{
	return 0;
};

/**
 *	sysfs_create_fops_file - Creates attribute with special file operations
 *	@kobj: kobject which should contains the attribute
 *	@attr: attributes which should be used to create file
 *
 *	First creates attribute @attr in kobject @kobj. If it is the first time
 *	this function is called, merge old fops from sysfs with new one and
 *	write it back. Afterwords the new fops will be set for the created
 *	attribute.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int sysfs_create_fops_file(struct kobject *kobj,
				  struct fops_attribute *attr)
{
	struct file_operations tmp, *fops;
	struct dentry *d;
	struct qstr qstr;
	int rc;

	fops = &attr->fops;

	if (fops->read)
		attr->bin.read = sysfs_read_dummy;

	if (fops->write)
		attr->bin.write = sysfs_write_dummy;

	if ((rc = sysfs_create_bin_file(kobj, &attr->bin)))
		return rc;

	qstr.name = attr->bin.attr.name;
	qstr.len = strlen(qstr.name);
	qstr.hash = full_name_hash(qstr.name, qstr.len);

	if ((d = lookup_hash(&qstr, kobj->dentry))) {
		if (!fops->owner) {
			memcpy(&tmp, d->d_inode->i_fop, sizeof(tmp));
			if (fops->read)
				tmp.read = fops->read;
			if (fops->write)
				tmp.write = fops->write;
			memcpy(fops, &tmp, sizeof(tmp));
		}

		d->d_inode->i_fop = fops;
	} else
		sysfs_remove_bin_file(kobj, &attr->bin);

	return -ENOENT;
};

/**
 *	sysfs_remove_fops_file - Remove attribute with special file operations
 *	@kobj: kobject which contains the attribute
 *	@attr: attributes which are used to create file
 *
 *	Only wrapper arround sysfs_remove_bin_file()
 *
 *	Returns 0 on success or negative error code on failure.
 */
static inline int sysfs_remove_fops_file(struct kobject *kobj,
					 struct fops_attribute *attr)
{
	return sysfs_remove_bin_file(kobj, &attr->bin);
};

/**
 *	i2o_config_read_hrt - Returns the HRT of the controller
 *	@kob: kernel object handle
 *	@buf: buffer into which the HRT should be copied
 *	@off: file offset
 *	@count: number of bytes to read
 *
 *	Put @count bytes starting at @off into @buf from the HRT of the I2O
 *	controller corresponding to @kobj.
 *
 *	Returns number of bytes copied into buffer.
 */
static ssize_t i2o_config_read_hrt(struct kobject *kobj, char *buf,
				   loff_t offset, size_t count)
{
	struct i2o_controller *c = kobj_to_i2o_device(kobj)->iop;
	i2o_hrt *hrt = c->hrt.virt;

	u32 size = (hrt->num_entries * hrt->entry_len + 2) * 4;

	if (offset > size)
		return 0;

	if (offset + count > size)
		count = size - offset;

	memcpy(buf, (u8 *) hrt + offset, count);

	return count;
};

/**
 *	i2o_config_read_lct - Returns the LCT of the controller
 *	@kob: kernel object handle
 *	@buf: buffer into which the LCT should be copied
 *	@off: file offset
 *	@count: number of bytes to read
 *
 *	Put @count bytes starting at @off into @buf from the LCT of the I2O
 *	controller corresponding to @kobj.
 *
 *	Returns number of bytes copied into buffer.
 */
static ssize_t i2o_config_read_lct(struct kobject *kobj, char *buf,
				   loff_t offset, size_t count)
{
	struct i2o_controller *c = kobj_to_i2o_device(kobj)->iop;
	u32 size = c->lct->table_size * 4;

	if (offset > size)
		return 0;

	if (offset + count > size)
		count = size - offset;

	memcpy(buf, (u8 *) c->lct + offset, count);

	return count;
};

#define I2O_CONFIG_SW_ATTR(_name,_mode,_type,_swid) \
static ssize_t i2o_config_##_name##_read(struct file *file, char __user *buf, size_t count, loff_t * offset) { \
	return i2o_config_sw_read(file, buf, count, offset, _type, _swid); \
};\
\
static ssize_t i2o_config_##_name##_write(struct file *file, const char __user *buf, size_t count, loff_t * offset) { \
	return i2o_config_sw_write(file, buf, count, offset, _type, _swid); \
}; \
\
static struct fops_attribute i2o_config_attr_##_name = { \
	.bin = { .attr = { .name = __stringify(_name), .mode = _mode, \
			   .owner = THIS_MODULE }, \
		 .size = 0, }, \
	.fops = { .write = i2o_config_##_name##_write, \
		  .read = i2o_config_##_name##_read} \
};

#ifdef CONFIG_I2O_EXT_ADAPTEC

/**
 *	i2o_config_dpt_reagion - Converts type and id to flash region
 *	@swtype: type of software module reading
 *	@swid: id of software which should be read
 *
 *	Converts type and id from I2O spec to the matching region for DPT /
 *	Adaptec controllers.
 *
 *	Returns region which match type and id or -1 on error.
 */
static u32 i2o_config_dpt_region(u8 swtype, u8 swid)
{
	switch (swtype) {
	case I2O_SOFTWARE_MODULE_IRTOS:
		/*
		 * content: operation firmware
		 * region size:
		 *      0xbc000 for 2554, 3754, 2564, 3757
		 *      0x170000 for 2865
		 *      0x17c000 for 3966
		 */
		if (!swid)
			return 0;

		break;

	case I2O_SOFTWARE_MODULE_IOP_PRIVATE:
		/*
		 * content: BIOS and SMOR
		 * BIOS size: first 0x8000 bytes
		 * region size:
		 *      0x40000 for 2554, 3754, 2564, 3757
		 *      0x80000 for 2865, 3966
		 */
		if (!swid)
			return 1;

		break;

	case I2O_SOFTWARE_MODULE_IOP_CONFIG:
		switch (swid) {
		case 0:
			/*
			 * content: NVRAM defaults
			 * region size: 0x2000 bytes
			 */
			return 2;
		case 1:
			/*
			 * content: serial number
			 * region size: 0x2000 bytes
			 */
			return 3;
		}
		break;
	}

	return -1;
};

#endif

/**
 *	i2o_config_sw_read - Read a software module from controller
 *	@file: file pointer
 *	@buf: buffer into which the data should be copied
 *	@count: number of bytes to read
 *	@off: file offset
 *	@swtype: type of software module reading
 *	@swid: id of software which should be read
 *
 *	Transfers @count bytes at offset @offset from IOP into buffer using
 *	type @swtype and id @swid as described in I2O spec.
 *
 *	Returns number of bytes copied into buffer or error code on failure.
 */
static ssize_t i2o_config_sw_read(struct file *file, char __user * buf,
				  size_t count, loff_t * offset, u8 swtype,
				  u32 swid)
{
	struct sysfs_dirent *sd = file->f_dentry->d_parent->d_fsdata;
	struct kobject *kobj = sd->s_element;
	struct i2o_controller *c = kobj_to_i2o_device(kobj)->iop;
	u32 m, function = I2O_CMD_SW_UPLOAD;
	struct i2o_dma buffer;
	struct i2o_message __iomem *msg;
	u32 __iomem *mptr;
	int rc, status;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -EBUSY;

	mptr = &msg->body[3];

	if ((rc = i2o_dma_alloc(&c->pdev->dev, &buffer, count, GFP_KERNEL))) {
		i2o_msg_nop(c, m);
		return rc;
	}
#ifdef CONFIG_I2O_EXT_ADAPTEC
	if (c->adaptec) {
		mptr = &msg->body[4];
		function = I2O_CMD_PRIVATE;

		writel(TEN_WORD_MSG_SIZE | SGL_OFFSET_8, &msg->u.head[0]);

		writel(I2O_VENDOR_DPT << 16 | I2O_DPT_FLASH_READ,
		       &msg->body[0]);
		writel(i2o_config_dpt_region(swtype, swid), &msg->body[1]);
		writel(*offset, &msg->body[2]);
		writel(count, &msg->body[3]);
	} else
#endif
		writel(NINE_WORD_MSG_SIZE | SGL_OFFSET_7, &msg->u.head[0]);

	writel(0xD0000000 | count, mptr++);
	writel(buffer.phys, mptr);

	writel(function << 24 | HOST_TID << 12 | ADAPTER_TID, &msg->u.head[1]);
	writel(i2o_config_driver.context, &msg->u.head[2]);
	writel(0, &msg->u.head[3]);

#ifdef CONFIG_I2O_EXT_ADAPTEC
	if (!c->adaptec)
#endif
	{
		writel((u32) swtype << 16 | (u32) 1 << 8, &msg->body[0]);
		writel(0, &msg->body[1]);
		writel(swid, &msg->body[2]);
	}

	status = i2o_msg_post_wait_mem(c, m, 60, &buffer);

	if (status == I2O_POST_WAIT_OK) {
		if (!(rc = copy_to_user(buf, buffer.virt, count))) {
			rc = count;
			*offset += count;
		}
	} else
		rc = -EIO;

	if (status != -ETIMEDOUT)
		i2o_dma_free(&c->pdev->dev, &buffer);

	return rc;
};

/**
 *	i2o_config_sw_write - Write a software module to controller
 *	@file: file pointer
 *	@buf: buffer into which the data should be copied
 *	@count: number of bytes to read
 *	@off: file offset
 *	@swtype: type of software module writing
 *	@swid: id of software which should be written
 *
 *	Transfers @count bytes at offset @offset from buffer to IOP using
 *	type @swtype and id @swid as described in I2O spec.
 *
 *	Returns number of bytes copied from buffer or error code on failure.
 */
static ssize_t i2o_config_sw_write(struct file *file, const char __user * buf,
				   size_t count, loff_t * offset, u8 swtype,
				   u32 swid)
{
	struct sysfs_dirent *sd = file->f_dentry->d_parent->d_fsdata;
	struct kobject *kobj = sd->s_element;
	struct i2o_controller *c = kobj_to_i2o_device(kobj)->iop;
	u32 m, function = I2O_CMD_SW_DOWNLOAD;
	struct i2o_dma buffer;
	struct i2o_message __iomem *msg;
	u32 __iomem *mptr;
	int rc, status;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -EBUSY;

	mptr = &msg->body[3];

	if ((rc = i2o_dma_alloc(&c->pdev->dev, &buffer, count, GFP_KERNEL)))
		goto nop_msg;

	if ((rc = copy_from_user(buffer.virt, buf, count)))
		goto free_buffer;

#ifdef CONFIG_I2O_EXT_ADAPTEC
	if (c->adaptec) {
		mptr = &msg->body[4];
		function = I2O_CMD_PRIVATE;

		writel(TEN_WORD_MSG_SIZE | SGL_OFFSET_8, &msg->u.head[0]);

		writel(I2O_VENDOR_DPT << 16 | I2O_DPT_FLASH_WRITE,
		       &msg->body[0]);
		writel(i2o_config_dpt_region(swtype, swid), &msg->body[1]);
		writel(*offset, &msg->body[2]);
		writel(count, &msg->body[3]);
	} else
#endif
		writel(NINE_WORD_MSG_SIZE | SGL_OFFSET_7, &msg->u.head[0]);

	writel(0xD4000000 | count, mptr++);
	writel(buffer.phys, mptr);

	writel(function << 24 | HOST_TID << 12 | ADAPTER_TID, &msg->u.head[1]);
	writel(i2o_config_driver.context, &msg->u.head[2]);
	writel(0, &msg->u.head[3]);

#ifdef CONFIG_I2O_EXT_ADAPTEC
	if (!c->adaptec)
#endif
	{
		writel((u32) swtype << 16 | (u32) 1 << 8, &msg->body[0]);
		writel(0, &msg->body[1]);
		writel(swid, &msg->body[2]);
	}

	status = i2o_msg_post_wait_mem(c, m, 60, &buffer);

	if (status != -ETIMEDOUT)
		i2o_dma_free(&c->pdev->dev, &buffer);

	if (status != I2O_POST_WAIT_OK)
		return -EIO;

	*offset += count;

	return count;

      free_buffer:
	i2o_dma_free(&c->pdev->dev, &buffer);

      nop_msg:
	i2o_msg_nop(c, m);

	return rc;
};

/* attribute for HRT in sysfs */
static struct bin_attribute i2o_config_hrt_attr = {
	.attr = {
		 .name = "hrt",
		 .mode = S_IRUGO,
		 .owner = THIS_MODULE},
	.size = 0,
	.read = i2o_config_read_hrt
};

/* attribute for LCT in sysfs */
static struct bin_attribute i2o_config_lct_attr = {
	.attr = {
		 .name = "lct",
		 .mode = S_IRUGO,
		 .owner = THIS_MODULE},
	.size = 0,
	.read = i2o_config_read_lct
};

/* IRTOS firmware access */
I2O_CONFIG_SW_ATTR(irtos, S_IWRSR, I2O_SOFTWARE_MODULE_IRTOS, 0);

#ifdef CONFIG_I2O_EXT_ADAPTEC

/*
 * attribute for BIOS / SMOR, nvram and serial number access on DPT / Adaptec
 * controllers
 */
I2O_CONFIG_SW_ATTR(bios, S_IWRSR, I2O_SOFTWARE_MODULE_IOP_PRIVATE, 0);
I2O_CONFIG_SW_ATTR(nvram, S_IWRSR, I2O_SOFTWARE_MODULE_IOP_CONFIG, 0);
I2O_CONFIG_SW_ATTR(serial, S_IWRSR, I2O_SOFTWARE_MODULE_IOP_CONFIG, 1);

#endif

/**
 *	i2o_config_notify_controller_add - Notify of added controller
 *	@c: the controller which was added
 *
 *	If a I2O controller is added, we catch the notification to add sysfs
 *	entries.
 */
static void i2o_config_notify_controller_add(struct i2o_controller *c)
{
	struct kobject *kobj = &c->exec->device.kobj;

	sysfs_create_bin_file(kobj, &i2o_config_hrt_attr);
	sysfs_create_bin_file(kobj, &i2o_config_lct_attr);

	sysfs_create_fops_file(kobj, &i2o_config_attr_irtos);
#ifdef CONFIG_I2O_EXT_ADAPTEC
	if (c->adaptec) {
		sysfs_create_fops_file(kobj, &i2o_config_attr_bios);
		sysfs_create_fops_file(kobj, &i2o_config_attr_nvram);
		sysfs_create_fops_file(kobj, &i2o_config_attr_serial);
	}
#endif
};

/**
 *	i2o_config_notify_controller_remove - Notify of removed controller
 *	@c: the controller which was removed
 *
 *	If a I2O controller is removed, we catch the notification to remove the
 *	sysfs entries.
 */
static void i2o_config_notify_controller_remove(struct i2o_controller *c)
{
	struct kobject *kobj = &c->exec->device.kobj;

#ifdef CONFIG_I2O_EXT_ADAPTEC
	if (c->adaptec) {
		sysfs_remove_fops_file(kobj, &i2o_config_attr_serial);
		sysfs_remove_fops_file(kobj, &i2o_config_attr_nvram);
		sysfs_remove_fops_file(kobj, &i2o_config_attr_bios);
	}
#endif
	sysfs_remove_fops_file(kobj, &i2o_config_attr_irtos);

	sysfs_remove_bin_file(kobj, &i2o_config_lct_attr);
	sysfs_remove_bin_file(kobj, &i2o_config_hrt_attr);
};

/* Config OSM driver struct */
static struct i2o_driver i2o_config_driver = {
	.name = OSM_NAME,
	.notify_controller_add = i2o_config_notify_controller_add,
	.notify_controller_remove = i2o_config_notify_controller_remove
};

#ifdef CONFIG_I2O_CONFIG_OLD_IOCTL
#include "i2o_config.c"
#endif

/**
 *	i2o_config_init - Configuration OSM initialization function
 *
 *	Registers Configuration OSM in the I2O core and if old ioctl's are
 *	compiled in initialize them.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_config_init(void)
{
	printk(KERN_INFO OSM_DESCRIPTION " v" OSM_VERSION "\n");

	if (i2o_driver_register(&i2o_config_driver)) {
		osm_err("handler register failed.\n");
		return -EBUSY;
	}
#ifdef CONFIG_I2O_CONFIG_OLD_IOCTL
	if (i2o_config_old_init())
		i2o_driver_unregister(&i2o_config_driver);
#endif

	return 0;
}

/**
 *	i2o_config_exit - Configuration OSM exit function
 *
 *	If old ioctl's are compiled in exit remove them and unregisters
 *	Configuration OSM from I2O core.
 */
static void i2o_config_exit(void)
{
#ifdef CONFIG_I2O_CONFIG_OLD_IOCTL
	i2o_config_old_exit();
#endif

	i2o_driver_unregister(&i2o_config_driver);
}

MODULE_AUTHOR("Markus Lidel <Markus.Lidel@shadowconnect.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(OSM_DESCRIPTION);
MODULE_VERSION(OSM_VERSION);

module_init(i2o_config_init);
module_exit(i2o_config_exit);
