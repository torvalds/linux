/* The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Based on elements of hwmon and input subsystems.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/anon_inodes.h>
#include <linux/debugfs.h>
#include <linux/iio/iio.h>
#include "iio_core.h"
#include "iio_core_trigger.h"
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

/* IDA to assign each registered device a unique id*/
static DEFINE_IDA(iio_ida);

static dev_t iio_devt;

#define IIO_DEV_MAX 256
struct bus_type iio_bus_type = {
	.name = "iio",
};
EXPORT_SYMBOL(iio_bus_type);

static struct dentry *iio_debugfs_dentry;

static const char * const iio_direction[] = {
	[0] = "in",
	[1] = "out",
};

static const char * const iio_chan_type_name_spec[] = {
	[IIO_VOLTAGE] = "voltage",
	[IIO_CURRENT] = "current",
	[IIO_POWER] = "power",
	[IIO_ACCEL] = "accel",
	[IIO_ANGL_VEL] = "anglvel",
	[IIO_MAGN] = "magn",
	[IIO_LIGHT] = "illuminance",
	[IIO_INTENSITY] = "intensity",
	[IIO_PROXIMITY] = "proximity",
	[IIO_TEMP] = "temp",
	[IIO_INCLI] = "incli",
	[IIO_ROT] = "rot",
	[IIO_ANGL] = "angl",
	[IIO_TIMESTAMP] = "timestamp",
	[IIO_CAPACITANCE] = "capacitance",
	[IIO_ALTVOLTAGE] = "altvoltage",
};

static const char * const iio_modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
	[IIO_MOD_LIGHT_BOTH] = "both",
	[IIO_MOD_LIGHT_IR] = "ir",
};

/* relies on pairs of these shared then separate */
static const char * const iio_chan_info_postfix[] = {
	[IIO_CHAN_INFO_RAW] = "raw",
	[IIO_CHAN_INFO_PROCESSED] = "input",
	[IIO_CHAN_INFO_SCALE] = "scale",
	[IIO_CHAN_INFO_OFFSET] = "offset",
	[IIO_CHAN_INFO_CALIBSCALE] = "calibscale",
	[IIO_CHAN_INFO_CALIBBIAS] = "calibbias",
	[IIO_CHAN_INFO_PEAK] = "peak_raw",
	[IIO_CHAN_INFO_PEAK_SCALE] = "peak_scale",
	[IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW] = "quadrature_correction_raw",
	[IIO_CHAN_INFO_AVERAGE_RAW] = "mean_raw",
	[IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY]
	= "filter_low_pass_3db_frequency",
	[IIO_CHAN_INFO_SAMP_FREQ] = "sampling_frequency",
	[IIO_CHAN_INFO_FREQUENCY] = "frequency",
	[IIO_CHAN_INFO_PHASE] = "phase",
	[IIO_CHAN_INFO_HARDWAREGAIN] = "hardwaregain",
};

const struct iio_chan_spec
*iio_find_channel_from_si(struct iio_dev *indio_dev, int si)
{
	int i;

	for (i = 0; i < indio_dev->num_channels; i++)
		if (indio_dev->channels[i].scan_index == si)
			return &indio_dev->channels[i];
	return NULL;
}

/* This turns up an awful lot */
ssize_t iio_read_const_attr(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%s\n", to_iio_const_attr(attr)->string);
}
EXPORT_SYMBOL(iio_read_const_attr);

static int __init iio_init(void)
{
	int ret;

	/* Register sysfs bus */
	ret  = bus_register(&iio_bus_type);
	if (ret < 0) {
		printk(KERN_ERR
		       "%s could not register bus type\n",
			__FILE__);
		goto error_nothing;
	}

	ret = alloc_chrdev_region(&iio_devt, 0, IIO_DEV_MAX, "iio");
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to allocate char dev region\n",
		       __FILE__);
		goto error_unregister_bus_type;
	}

	iio_debugfs_dentry = debugfs_create_dir("iio", NULL);

	return 0;

error_unregister_bus_type:
	bus_unregister(&iio_bus_type);
error_nothing:
	return ret;
}

static void __exit iio_exit(void)
{
	if (iio_devt)
		unregister_chrdev_region(iio_devt, IIO_DEV_MAX);
	bus_unregister(&iio_bus_type);
	debugfs_remove(iio_debugfs_dentry);
}

#if defined(CONFIG_DEBUG_FS)
static ssize_t iio_debugfs_read_reg(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	struct iio_dev *indio_dev = file->private_data;
	char buf[20];
	unsigned val = 0;
	ssize_t len;
	int ret;

	ret = indio_dev->info->debugfs_reg_access(indio_dev,
						  indio_dev->cached_reg_addr,
						  0, &val);
	if (ret)
		dev_err(indio_dev->dev.parent, "%s: read failed\n", __func__);

	len = snprintf(buf, sizeof(buf), "0x%X\n", val);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t iio_debugfs_write_reg(struct file *file,
		     const char __user *userbuf, size_t count, loff_t *ppos)
{
	struct iio_dev *indio_dev = file->private_data;
	unsigned reg, val;
	char buf[80];
	int ret;

	count = min_t(size_t, count, (sizeof(buf)-1));
	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;

	buf[count] = 0;

	ret = sscanf(buf, "%i %i", &reg, &val);

	switch (ret) {
	case 1:
		indio_dev->cached_reg_addr = reg;
		break;
	case 2:
		indio_dev->cached_reg_addr = reg;
		ret = indio_dev->info->debugfs_reg_access(indio_dev, reg,
							  val, NULL);
		if (ret) {
			dev_err(indio_dev->dev.parent, "%s: write failed\n",
				__func__);
			return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static const struct file_operations iio_debugfs_reg_fops = {
	.open = simple_open,
	.read = iio_debugfs_read_reg,
	.write = iio_debugfs_write_reg,
};

static void iio_device_unregister_debugfs(struct iio_dev *indio_dev)
{
	debugfs_remove_recursive(indio_dev->debugfs_dentry);
}

static int iio_device_register_debugfs(struct iio_dev *indio_dev)
{
	struct dentry *d;

	if (indio_dev->info->debugfs_reg_access == NULL)
		return 0;

	if (!iio_debugfs_dentry)
		return 0;

	indio_dev->debugfs_dentry =
		debugfs_create_dir(dev_name(&indio_dev->dev),
				   iio_debugfs_dentry);
	if (indio_dev->debugfs_dentry == NULL) {
		dev_warn(indio_dev->dev.parent,
			 "Failed to create debugfs directory\n");
		return -EFAULT;
	}

	d = debugfs_create_file("direct_reg_access", 0644,
				indio_dev->debugfs_dentry,
				indio_dev, &iio_debugfs_reg_fops);
	if (!d) {
		iio_device_unregister_debugfs(indio_dev);
		return -ENOMEM;
	}

	return 0;
}
#else
static int iio_device_register_debugfs(struct iio_dev *indio_dev)
{
	return 0;
}

static void iio_device_unregister_debugfs(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_DEBUG_FS */

static ssize_t iio_read_channel_ext_info(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	const struct iio_chan_spec_ext_info *ext_info;

	ext_info = &this_attr->c->ext_info[this_attr->address];

	return ext_info->read(indio_dev, ext_info->private, this_attr->c, buf);
}

static ssize_t iio_write_channel_ext_info(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
					 size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	const struct iio_chan_spec_ext_info *ext_info;

	ext_info = &this_attr->c->ext_info[this_attr->address];

	return ext_info->write(indio_dev, ext_info->private,
			       this_attr->c, buf, len);
}

static ssize_t iio_read_channel_info(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int val, val2;
	bool scale_db = false;
	int ret = indio_dev->info->read_raw(indio_dev, this_attr->c,
					    &val, &val2, this_attr->address);

	if (ret < 0)
		return ret;

	switch (ret) {
	case IIO_VAL_INT:
		return sprintf(buf, "%d\n", val);
	case IIO_VAL_INT_PLUS_MICRO_DB:
		scale_db = true;
	case IIO_VAL_INT_PLUS_MICRO:
		if (val2 < 0)
			return sprintf(buf, "-%d.%06u%s\n", val, -val2,
				scale_db ? " dB" : "");
		else
			return sprintf(buf, "%d.%06u%s\n", val, val2,
				scale_db ? " dB" : "");
	case IIO_VAL_INT_PLUS_NANO:
		if (val2 < 0)
			return sprintf(buf, "-%d.%09u\n", val, -val2);
		else
			return sprintf(buf, "%d.%09u\n", val, val2);
	default:
		return 0;
	}
}

static ssize_t iio_write_channel_info(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret, integer = 0, fract = 0, fract_mult = 100000;
	bool integer_part = true, negative = false;

	/* Assumes decimal - precision based on number of digits */
	if (!indio_dev->info->write_raw)
		return -EINVAL;

	if (indio_dev->info->write_raw_get_fmt)
		switch (indio_dev->info->write_raw_get_fmt(indio_dev,
			this_attr->c, this_attr->address)) {
		case IIO_VAL_INT_PLUS_MICRO:
			fract_mult = 100000;
			break;
		case IIO_VAL_INT_PLUS_NANO:
			fract_mult = 100000000;
			break;
		default:
			return -EINVAL;
		}

	if (buf[0] == '-') {
		negative = true;
		buf++;
	}

	while (*buf) {
		if ('0' <= *buf && *buf <= '9') {
			if (integer_part)
				integer = integer*10 + *buf - '0';
			else {
				fract += fract_mult*(*buf - '0');
				if (fract_mult == 1)
					break;
				fract_mult /= 10;
			}
		} else if (*buf == '\n') {
			if (*(buf + 1) == '\0')
				break;
			else
				return -EINVAL;
		} else if (*buf == '.') {
			integer_part = false;
		} else {
			return -EINVAL;
		}
		buf++;
	}
	if (negative) {
		if (integer)
			integer = -integer;
		else
			fract = -fract;
	}

	ret = indio_dev->info->write_raw(indio_dev, this_attr->c,
					 integer, fract, this_attr->address);
	if (ret)
		return ret;

	return len;
}

static
int __iio_device_attr_init(struct device_attribute *dev_attr,
			   const char *postfix,
			   struct iio_chan_spec const *chan,
			   ssize_t (*readfunc)(struct device *dev,
					       struct device_attribute *attr,
					       char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   bool generic)
{
	int ret;
	char *name_format, *full_postfix;
	sysfs_attr_init(&dev_attr->attr);

	/* Build up postfix of <extend_name>_<modifier>_postfix */
	if (chan->modified && !generic) {
		if (chan->extend_name)
			full_postfix = kasprintf(GFP_KERNEL, "%s_%s_%s",
						 iio_modifier_names[chan
								    ->channel2],
						 chan->extend_name,
						 postfix);
		else
			full_postfix = kasprintf(GFP_KERNEL, "%s_%s",
						 iio_modifier_names[chan
								    ->channel2],
						 postfix);
	} else {
		if (chan->extend_name == NULL)
			full_postfix = kstrdup(postfix, GFP_KERNEL);
		else
			full_postfix = kasprintf(GFP_KERNEL,
						 "%s_%s",
						 chan->extend_name,
						 postfix);
	}
	if (full_postfix == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	if (chan->differential) { /* Differential can not have modifier */
		if (generic)
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s-%s_%s",
					    iio_direction[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    iio_chan_type_name_spec[chan->type],
					    full_postfix);
		else if (chan->indexed)
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s%d-%s%d_%s",
					    iio_direction[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    chan->channel,
					    iio_chan_type_name_spec[chan->type],
					    chan->channel2,
					    full_postfix);
		else {
			WARN_ON("Differential channels must be indexed\n");
			ret = -EINVAL;
			goto error_free_full_postfix;
		}
	} else { /* Single ended */
		if (generic)
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s_%s",
					    iio_direction[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    full_postfix);
		else if (chan->indexed)
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s%d_%s",
					    iio_direction[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    chan->channel,
					    full_postfix);
		else
			name_format
				= kasprintf(GFP_KERNEL, "%s_%s_%s",
					    iio_direction[chan->output],
					    iio_chan_type_name_spec[chan->type],
					    full_postfix);
	}
	if (name_format == NULL) {
		ret = -ENOMEM;
		goto error_free_full_postfix;
	}
	dev_attr->attr.name = kasprintf(GFP_KERNEL,
					name_format,
					chan->channel,
					chan->channel2);
	if (dev_attr->attr.name == NULL) {
		ret = -ENOMEM;
		goto error_free_name_format;
	}

	if (readfunc) {
		dev_attr->attr.mode |= S_IRUGO;
		dev_attr->show = readfunc;
	}

	if (writefunc) {
		dev_attr->attr.mode |= S_IWUSR;
		dev_attr->store = writefunc;
	}
	kfree(name_format);
	kfree(full_postfix);

	return 0;

error_free_name_format:
	kfree(name_format);
error_free_full_postfix:
	kfree(full_postfix);
error_ret:
	return ret;
}

static void __iio_device_attr_deinit(struct device_attribute *dev_attr)
{
	kfree(dev_attr->attr.name);
}

int __iio_add_chan_devattr(const char *postfix,
			   struct iio_chan_spec const *chan,
			   ssize_t (*readfunc)(struct device *dev,
					       struct device_attribute *attr,
					       char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   u64 mask,
			   bool generic,
			   struct device *dev,
			   struct list_head *attr_list)
{
	int ret;
	struct iio_dev_attr *iio_attr, *t;

	iio_attr = kzalloc(sizeof *iio_attr, GFP_KERNEL);
	if (iio_attr == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = __iio_device_attr_init(&iio_attr->dev_attr,
				     postfix, chan,
				     readfunc, writefunc, generic);
	if (ret)
		goto error_iio_dev_attr_free;
	iio_attr->c = chan;
	iio_attr->address = mask;
	list_for_each_entry(t, attr_list, l)
		if (strcmp(t->dev_attr.attr.name,
			   iio_attr->dev_attr.attr.name) == 0) {
			if (!generic)
				dev_err(dev, "tried to double register : %s\n",
					t->dev_attr.attr.name);
			ret = -EBUSY;
			goto error_device_attr_deinit;
		}
	list_add(&iio_attr->l, attr_list);

	return 0;

error_device_attr_deinit:
	__iio_device_attr_deinit(&iio_attr->dev_attr);
error_iio_dev_attr_free:
	kfree(iio_attr);
error_ret:
	return ret;
}

static int iio_device_add_channel_sysfs(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan)
{
	int ret, attrcount = 0;
	int i;
	const struct iio_chan_spec_ext_info *ext_info;

	if (chan->channel < 0)
		return 0;
	for_each_set_bit(i, &chan->info_mask, sizeof(long)*8) {
		ret = __iio_add_chan_devattr(iio_chan_info_postfix[i/2],
					     chan,
					     &iio_read_channel_info,
					     &iio_write_channel_info,
					     i/2,
					     !(i%2),
					     &indio_dev->dev,
					     &indio_dev->channel_attr_list);
		if (ret == -EBUSY && (i%2 == 0)) {
			ret = 0;
			continue;
		}
		if (ret < 0)
			goto error_ret;
		attrcount++;
	}

	if (chan->ext_info) {
		unsigned int i = 0;
		for (ext_info = chan->ext_info; ext_info->name; ext_info++) {
			ret = __iio_add_chan_devattr(ext_info->name,
					chan,
					ext_info->read ?
					    &iio_read_channel_ext_info : NULL,
					ext_info->write ?
					    &iio_write_channel_ext_info : NULL,
					i,
					ext_info->shared,
					&indio_dev->dev,
					&indio_dev->channel_attr_list);
			i++;
			if (ret == -EBUSY && ext_info->shared)
				continue;

			if (ret)
				goto error_ret;

			attrcount++;
		}
	}

	ret = attrcount;
error_ret:
	return ret;
}

static void iio_device_remove_and_free_read_attr(struct iio_dev *indio_dev,
						 struct iio_dev_attr *p)
{
	kfree(p->dev_attr.attr.name);
	kfree(p);
}

static ssize_t iio_show_dev_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	return sprintf(buf, "%s\n", indio_dev->name);
}

static DEVICE_ATTR(name, S_IRUGO, iio_show_dev_name, NULL);

static int iio_device_register_sysfs(struct iio_dev *indio_dev)
{
	int i, ret = 0, attrcount, attrn, attrcount_orig = 0;
	struct iio_dev_attr *p, *n;
	struct attribute **attr;

	/* First count elements in any existing group */
	if (indio_dev->info->attrs) {
		attr = indio_dev->info->attrs->attrs;
		while (*attr++ != NULL)
			attrcount_orig++;
	}
	attrcount = attrcount_orig;
	/*
	 * New channel registration method - relies on the fact a group does
	 * not need to be initialized if it is name is NULL.
	 */
	if (indio_dev->channels)
		for (i = 0; i < indio_dev->num_channels; i++) {
			ret = iio_device_add_channel_sysfs(indio_dev,
							   &indio_dev
							   ->channels[i]);
			if (ret < 0)
				goto error_clear_attrs;
			attrcount += ret;
		}

	if (indio_dev->name)
		attrcount++;

	indio_dev->chan_attr_group.attrs = kcalloc(attrcount + 1,
						   sizeof(indio_dev->chan_attr_group.attrs[0]),
						   GFP_KERNEL);
	if (indio_dev->chan_attr_group.attrs == NULL) {
		ret = -ENOMEM;
		goto error_clear_attrs;
	}
	/* Copy across original attributes */
	if (indio_dev->info->attrs)
		memcpy(indio_dev->chan_attr_group.attrs,
		       indio_dev->info->attrs->attrs,
		       sizeof(indio_dev->chan_attr_group.attrs[0])
		       *attrcount_orig);
	attrn = attrcount_orig;
	/* Add all elements from the list. */
	list_for_each_entry(p, &indio_dev->channel_attr_list, l)
		indio_dev->chan_attr_group.attrs[attrn++] = &p->dev_attr.attr;
	if (indio_dev->name)
		indio_dev->chan_attr_group.attrs[attrn++] = &dev_attr_name.attr;

	indio_dev->groups[indio_dev->groupcounter++] =
		&indio_dev->chan_attr_group;

	return 0;

error_clear_attrs:
	list_for_each_entry_safe(p, n,
				 &indio_dev->channel_attr_list, l) {
		list_del(&p->l);
		iio_device_remove_and_free_read_attr(indio_dev, p);
	}

	return ret;
}

static void iio_device_unregister_sysfs(struct iio_dev *indio_dev)
{

	struct iio_dev_attr *p, *n;

	list_for_each_entry_safe(p, n, &indio_dev->channel_attr_list, l) {
		list_del(&p->l);
		iio_device_remove_and_free_read_attr(indio_dev, p);
	}
	kfree(indio_dev->chan_attr_group.attrs);
}

static void iio_dev_release(struct device *device)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(device);
	if (indio_dev->chrdev.dev)
		cdev_del(&indio_dev->chrdev);
	if (indio_dev->modes & INDIO_BUFFER_TRIGGERED)
		iio_device_unregister_trigger_consumer(indio_dev);
	iio_device_unregister_eventset(indio_dev);
	iio_device_unregister_sysfs(indio_dev);
	iio_device_unregister_debugfs(indio_dev);

	ida_simple_remove(&iio_ida, indio_dev->id);
	kfree(indio_dev);
}

static struct device_type iio_dev_type = {
	.name = "iio_device",
	.release = iio_dev_release,
};

struct iio_dev *iio_device_alloc(int sizeof_priv)
{
	struct iio_dev *dev;
	size_t alloc_size;

	alloc_size = sizeof(struct iio_dev);
	if (sizeof_priv) {
		alloc_size = ALIGN(alloc_size, IIO_ALIGN);
		alloc_size += sizeof_priv;
	}
	/* ensure 32-byte alignment of whole construct ? */
	alloc_size += IIO_ALIGN - 1;

	dev = kzalloc(alloc_size, GFP_KERNEL);

	if (dev) {
		dev->dev.groups = dev->groups;
		dev->dev.type = &iio_dev_type;
		dev->dev.bus = &iio_bus_type;
		device_initialize(&dev->dev);
		dev_set_drvdata(&dev->dev, (void *)dev);
		mutex_init(&dev->mlock);
		mutex_init(&dev->info_exist_lock);
		INIT_LIST_HEAD(&dev->channel_attr_list);

		dev->id = ida_simple_get(&iio_ida, 0, 0, GFP_KERNEL);
		if (dev->id < 0) {
			/* cannot use a dev_err as the name isn't available */
			printk(KERN_ERR "Failed to get id\n");
			kfree(dev);
			return NULL;
		}
		dev_set_name(&dev->dev, "iio:device%d", dev->id);
	}

	return dev;
}
EXPORT_SYMBOL(iio_device_alloc);

void iio_device_free(struct iio_dev *dev)
{
	if (dev)
		put_device(&dev->dev);
}
EXPORT_SYMBOL(iio_device_free);

/**
 * iio_chrdev_open() - chrdev file open for buffer access and ioctls
 **/
static int iio_chrdev_open(struct inode *inode, struct file *filp)
{
	struct iio_dev *indio_dev = container_of(inode->i_cdev,
						struct iio_dev, chrdev);

	if (test_and_set_bit(IIO_BUSY_BIT_POS, &indio_dev->flags))
		return -EBUSY;

	filp->private_data = indio_dev;

	return 0;
}

/**
 * iio_chrdev_release() - chrdev file close buffer access and ioctls
 **/
static int iio_chrdev_release(struct inode *inode, struct file *filp)
{
	struct iio_dev *indio_dev = container_of(inode->i_cdev,
						struct iio_dev, chrdev);
	clear_bit(IIO_BUSY_BIT_POS, &indio_dev->flags);
	return 0;
}

/* Somewhat of a cross file organization violation - ioctls here are actually
 * event related */
static long iio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct iio_dev *indio_dev = filp->private_data;
	int __user *ip = (int __user *)arg;
	int fd;

	if (cmd == IIO_GET_EVENT_FD_IOCTL) {
		fd = iio_event_getfd(indio_dev);
		if (copy_to_user(ip, &fd, sizeof(fd)))
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}

static const struct file_operations iio_buffer_fileops = {
	.read = iio_buffer_read_first_n_outer_addr,
	.release = iio_chrdev_release,
	.open = iio_chrdev_open,
	.poll = iio_buffer_poll_addr,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.unlocked_ioctl = iio_ioctl,
	.compat_ioctl = iio_ioctl,
};

static const struct iio_buffer_setup_ops noop_ring_setup_ops;

int iio_device_register(struct iio_dev *indio_dev)
{
	int ret;

	/* configure elements for the chrdev */
	indio_dev->dev.devt = MKDEV(MAJOR(iio_devt), indio_dev->id);

	ret = iio_device_register_debugfs(indio_dev);
	if (ret) {
		dev_err(indio_dev->dev.parent,
			"Failed to register debugfs interfaces\n");
		goto error_ret;
	}
	ret = iio_device_register_sysfs(indio_dev);
	if (ret) {
		dev_err(indio_dev->dev.parent,
			"Failed to register sysfs interfaces\n");
		goto error_unreg_debugfs;
	}
	ret = iio_device_register_eventset(indio_dev);
	if (ret) {
		dev_err(indio_dev->dev.parent,
			"Failed to register event set\n");
		goto error_free_sysfs;
	}
	if (indio_dev->modes & INDIO_BUFFER_TRIGGERED)
		iio_device_register_trigger_consumer(indio_dev);

	if ((indio_dev->modes & INDIO_ALL_BUFFER_MODES) &&
		indio_dev->setup_ops == NULL)
		indio_dev->setup_ops = &noop_ring_setup_ops;

	ret = device_add(&indio_dev->dev);
	if (ret < 0)
		goto error_unreg_eventset;
	cdev_init(&indio_dev->chrdev, &iio_buffer_fileops);
	indio_dev->chrdev.owner = indio_dev->info->driver_module;
	ret = cdev_add(&indio_dev->chrdev, indio_dev->dev.devt, 1);
	if (ret < 0)
		goto error_del_device;
	return 0;

error_del_device:
	device_del(&indio_dev->dev);
error_unreg_eventset:
	iio_device_unregister_eventset(indio_dev);
error_free_sysfs:
	iio_device_unregister_sysfs(indio_dev);
error_unreg_debugfs:
	iio_device_unregister_debugfs(indio_dev);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_device_register);

void iio_device_unregister(struct iio_dev *indio_dev)
{
	mutex_lock(&indio_dev->info_exist_lock);
	indio_dev->info = NULL;
	mutex_unlock(&indio_dev->info_exist_lock);
	device_del(&indio_dev->dev);
}
EXPORT_SYMBOL(iio_device_unregister);
subsys_initcall(iio_init);
module_exit(iio_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("Industrial I/O core");
MODULE_LICENSE("GPL");
