/*
 * arch/sh/drivers/dma/dma-sysfs.c
 *
 * sysfs interface for SH DMA API
 *
 * Copyright (C) 2004, 2005  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/string.h>
#include <asm/dma.h>

static struct sysdev_class dma_sysclass = {
	set_kset_name("dma"),
};

EXPORT_SYMBOL(dma_sysclass);

static ssize_t dma_show_devices(struct sys_device *dev, char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		struct dma_info *info = get_dma_info(i);
		struct dma_channel *channel = &info->channels[i];

		len += sprintf(buf + len, "%2d: %14s    %s\n",
			       channel->chan, info->name,
			       channel->dev_id);
	}

	return len;
}

static SYSDEV_ATTR(devices, S_IRUGO, dma_show_devices, NULL);

static int __init dma_sysclass_init(void)
{
	int ret;

	ret = sysdev_class_register(&dma_sysclass);
	if (ret == 0)
		sysfs_create_file(&dma_sysclass.kset.kobj, &attr_devices.attr);

	return ret;
}

postcore_initcall(dma_sysclass_init);

static ssize_t dma_show_dev_id(struct sys_device *dev, char *buf)
{
	struct dma_channel *channel = to_dma_channel(dev);
	return sprintf(buf, "%s\n", channel->dev_id);
}

static ssize_t dma_store_dev_id(struct sys_device *dev,
				const char *buf, size_t count)
{
	struct dma_channel *channel = to_dma_channel(dev);
	strcpy(channel->dev_id, buf);
	return count;
}

static SYSDEV_ATTR(dev_id, S_IRUGO | S_IWUSR, dma_show_dev_id, dma_store_dev_id);

static ssize_t dma_store_config(struct sys_device *dev,
				const char *buf, size_t count)
{
	struct dma_channel *channel = to_dma_channel(dev);
	unsigned long config;

	config = simple_strtoul(buf, NULL, 0);
	dma_configure_channel(channel->vchan, config);

	return count;
}

static SYSDEV_ATTR(config, S_IWUSR, NULL, dma_store_config);

static ssize_t dma_show_mode(struct sys_device *dev, char *buf)
{
	struct dma_channel *channel = to_dma_channel(dev);
	return sprintf(buf, "0x%08x\n", channel->mode);
}

static ssize_t dma_store_mode(struct sys_device *dev,
			      const char *buf, size_t count)
{
	struct dma_channel *channel = to_dma_channel(dev);
	channel->mode = simple_strtoul(buf, NULL, 0);
	return count;
}

static SYSDEV_ATTR(mode, S_IRUGO | S_IWUSR, dma_show_mode, dma_store_mode);

#define dma_ro_attr(field, fmt)						\
static ssize_t dma_show_##field(struct sys_device *dev, char *buf)	\
{									\
	struct dma_channel *channel = to_dma_channel(dev);		\
	return sprintf(buf, fmt, channel->field);			\
}									\
static SYSDEV_ATTR(field, S_IRUGO, dma_show_##field, NULL);

dma_ro_attr(count, "0x%08x\n");
dma_ro_attr(flags, "0x%08lx\n");

int dma_create_sysfs_files(struct dma_channel *chan, struct dma_info *info)
{
	struct sys_device *dev = &chan->dev;
	char name[16];
	int ret;

	dev->id  = chan->vchan;
	dev->cls = &dma_sysclass;

	ret = sysdev_register(dev);
	if (ret)
		return ret;

	sysdev_create_file(dev, &attr_dev_id);
	sysdev_create_file(dev, &attr_count);
	sysdev_create_file(dev, &attr_mode);
	sysdev_create_file(dev, &attr_flags);
	sysdev_create_file(dev, &attr_config);

	snprintf(name, sizeof(name), "dma%d", chan->chan);
	return sysfs_create_link(&info->pdev->dev.kobj, &dev->kobj, name);
}

void dma_remove_sysfs_files(struct dma_channel *chan, struct dma_info *info)
{
	struct sys_device *dev = &chan->dev;
	char name[16];

	sysdev_remove_file(dev, &attr_dev_id);
	sysdev_remove_file(dev, &attr_count);
	sysdev_remove_file(dev, &attr_mode);
	sysdev_remove_file(dev, &attr_flags);
	sysdev_remove_file(dev, &attr_config);

	snprintf(name, sizeof(name), "dma%d", chan->chan);
	sysfs_remove_link(&info->pdev->dev.kobj, name);

	sysdev_unregister(dev);
}

