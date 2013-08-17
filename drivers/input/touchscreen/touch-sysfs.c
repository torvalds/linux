//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C Touchscreen driver
//  2012.01.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/device.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <asm/unaligned.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/fs.h>

#include <linux/string.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/input/touch-pdata.h>
#include "touch.h"

//[*]--------------------------------------------------------------------------------------------------[*]
//
//   sysfs function prototype define
//
//[*]--------------------------------------------------------------------------------------------------[*]
//   disablel (1 -> disable irq, cancel work, 0 -> enable irq), show irq state
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_disable			(struct device *dev, struct device_attribute *attr, char *buf);
static	ssize_t store_disable			(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	DEVICE_ATTR(disable, S_IRWXUGO, show_disable, store_disable);

//[*]--------------------------------------------------------------------------------------------------[*]
//  fw version display
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_fw_version			(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(fw_version, S_IRWXUGO, show_fw_version, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
//   fw data load : fw load status, fw data load
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_fw				(struct device *dev, struct device_attribute *attr, char *buf);
static	ssize_t store_fw			(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	DEVICE_ATTR(fw, S_IRWXUGO, show_fw, store_fw);

//[*]--------------------------------------------------------------------------------------------------[*]
//   fw status : fw status
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_fw_status		(struct device *dev, struct device_attribute *attr, char *buf);
static	ssize_t store_fw_status		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	DEVICE_ATTR(fw_status, S_IRWXUGO, show_fw_status, store_fw_status);

//[*]--------------------------------------------------------------------------------------------------[*]
//   update_fw : 1 -> update fw (load fw)
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t store_update_fw			(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	DEVICE_ATTR(update_fw, S_IRWXUGO, NULL, store_update_fw);

//[*]--------------------------------------------------------------------------------------------------[*]
//   calibration (1 -> update calibration, 0 -> nothing), show -> NULL
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t store_calibration		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	DEVICE_ATTR(calibration, S_IRWXUGO, NULL, store_calibration);

//[*]--------------------------------------------------------------------------------------------------[*]
//   hw reset
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t store_reset				(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	DEVICE_ATTR(reset, S_IRWXUGO, NULL, store_reset);

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static struct attribute *touch_sysfs_entries[] = {
	&dev_attr_disable.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_fw.attr,
	&dev_attr_fw_status.attr,
	&dev_attr_update_fw.attr,
	&dev_attr_calibration.attr,
	&dev_attr_reset.attr,
	NULL
};

static struct attribute_group touch_attr_group = {
	.name   = NULL,
	.attrs  = touch_sysfs_entries,
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t store_reset				(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct touch 	*ts = dev_get_drvdata(dev);
	unsigned long 	val;
	int 			err;

	if ((err = strict_strtoul(buf, 10, &val)))	return err;

	if((ts->pdata->reset_gpio) && (val == 1))	touch_hw_reset(ts);

	return count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//   disablel (1 -> disable irq, cancel work, 0 -> enable irq), show irq state
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_disable			(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct touch 	*ts = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ts->disabled);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t store_disable			(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct touch 	*ts = dev_get_drvdata(dev);
	unsigned long 	val;
	int 			err;

	if ((err = strict_strtoul(buf, 10, &val)))	return err;

	if (val) 	ts->pdata->disable(ts);	// interrupt disable
	else		ts->pdata->enable(ts);	// interrupt enable

	return count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
// firmware version display
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_fw_version			(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct touch 	*ts = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", ts->fw_version);
}

//[*]--------------------------------------------------------------------------------------------------[*]
//   update_fw 
//[*]--------------------------------------------------------------------------------------------------[*]

static	ssize_t store_fw		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct touch 	*ts = dev_get_drvdata(dev);

	if(ts->fw_buf != NULL)	{
		
		if(ts->fw_size < ts->pdata->fw_filesize)	memcpy(&ts->fw_buf[ts->fw_size], buf, count);

		ts->fw_size += count;
	}
	
	return count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_fw			(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct touch 	*ts = dev_get_drvdata(dev);

	return	sprintf(buf, "%d\n", ts->fw_size);
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t store_fw_status	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct touch 	*ts = dev_get_drvdata(dev);
	int 			err;
	unsigned long 	val;

	if ((err = strict_strtoul(buf, 10, &val)))		return err;

	if(ts->pdata->fw_control)	ts->pdata->fw_control(ts, val);
	
	return count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_fw_status	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct touch 	*ts = dev_get_drvdata(dev);

	return	sprintf(buf, "0x%08X\n", ts->fw_status);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t store_update_fw			(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long 	val;
	int 			err;
	struct touch 	*ts = dev_get_drvdata(dev);

	if ((err = strict_strtoul(buf, 10, &val)))	return 	err;

	if (val == 1)	{
		if(ts->pdata->flash_firmware)	{
			if(ts->pdata->fw_filename)	ts->pdata->flash_firmware(dev, ts->pdata->fw_filename);
		}
	}

	return count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//   calibration (1 -> update calibration, 0 -> nothing), show -> NULL
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t store_calibration		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct touch 	*ts = dev_get_drvdata(dev);
	unsigned long 	val;
	int 			err;

	if ((err = strict_strtoul(buf, 10, &val)))	return err;

	if (val == 1)	{
		ts->pdata->disable(ts);	// interrupt disable
		if(ts->pdata->calibration)		ts->pdata->calibration(ts);
		ts->pdata->enable(ts);	// interrupt enable
	}

	return count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
int		touch_sysfs_create		(struct device *dev)	
{
	return	sysfs_create_group(&dev->kobj, &touch_attr_group);
}

//[*]--------------------------------------------------------------------------------------------------[*]
void	touch_sysfs_remove		(struct device *dev)	
{
    sysfs_remove_group(&dev->kobj, &touch_attr_group);
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
