/*
 * drivers/net/gianfar_sysfs.c
 *
 * Gianfar Ethernet Driver
 * This driver is designed for the non-CPM ethernet controllers
 * on the 85xx and 83xx family of integrated processors
 * Based on 8260_io/fcc_enet.c
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala (galak@kernel.crashing.org)
 *
 * Copyright (c) 2002-2005 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Sysfs file creation and management
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/device.h>

#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>

#include "gianfar.h"

#define GFAR_ATTR(_name) \
static ssize_t gfar_show_##_name(struct class_device *cdev, char *buf); \
static ssize_t gfar_set_##_name(struct class_device *cdev, \
		const char *buf, size_t count); \
static CLASS_DEVICE_ATTR(_name, 0644, gfar_show_##_name, gfar_set_##_name)

#define GFAR_CREATE_FILE(_dev, _name) \
	class_device_create_file(&_dev->class_dev, &class_device_attr_##_name)

GFAR_ATTR(bd_stash);
GFAR_ATTR(rx_stash_size);
GFAR_ATTR(rx_stash_index);
GFAR_ATTR(fifo_threshold);
GFAR_ATTR(fifo_starve);
GFAR_ATTR(fifo_starve_off);

#define to_net_dev(cd) container_of(cd, struct net_device, class_dev)

static ssize_t gfar_show_bd_stash(struct class_device *cdev, char *buf)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);

	return sprintf(buf, "%s\n", priv->bd_stash_en? "on" : "off");
}

static ssize_t gfar_set_bd_stash(struct class_device *cdev,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);
	int new_setting = 0;
	u32 temp;
	unsigned long flags;

	/* Find out the new setting */
	if (!strncmp("on", buf, count-1) || !strncmp("1", buf, count-1))
		new_setting = 1;
	else if (!strncmp("off", buf, count-1) || !strncmp("0", buf, count-1))
		new_setting = 0;
	else
		return count;

	spin_lock_irqsave(&priv->rxlock, flags);

	/* Set the new stashing value */
	priv->bd_stash_en = new_setting;

	temp = gfar_read(&priv->regs->attr);
	
	if (new_setting)
		temp |= ATTR_BDSTASH;
	else
		temp &= ~(ATTR_BDSTASH);

	gfar_write(&priv->regs->attr, temp);

	spin_unlock_irqrestore(&priv->rxlock, flags);

	return count;
}

static ssize_t gfar_show_rx_stash_size(struct class_device *cdev, char *buf)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);

	return sprintf(buf, "%d\n", priv->rx_stash_size);
}

static ssize_t gfar_set_rx_stash_size(struct class_device *cdev,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);
	unsigned int length = simple_strtoul(buf, NULL, 0);
	u32 temp;
	unsigned long flags;

	spin_lock_irqsave(&priv->rxlock, flags);
	if (length > priv->rx_buffer_size)
		return count;

	if (length == priv->rx_stash_size)
		return count;

	priv->rx_stash_size = length;

	temp = gfar_read(&priv->regs->attreli);
	temp &= ~ATTRELI_EL_MASK;
	temp |= ATTRELI_EL(length);
	gfar_write(&priv->regs->attreli, temp);

	/* Turn stashing on/off as appropriate */
	temp = gfar_read(&priv->regs->attr);

	if (length)
		temp |= ATTR_BUFSTASH;
	else
		temp &= ~(ATTR_BUFSTASH);

	gfar_write(&priv->regs->attr, temp);

	spin_unlock_irqrestore(&priv->rxlock, flags);

	return count;
}


/* Stashing will only be enabled when rx_stash_size != 0 */
static ssize_t gfar_show_rx_stash_index(struct class_device *cdev, char *buf)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);

	return sprintf(buf, "%d\n", priv->rx_stash_index);
}

static ssize_t gfar_set_rx_stash_index(struct class_device *cdev,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);
	unsigned short index = simple_strtoul(buf, NULL, 0);
	u32 temp;
	unsigned long flags;

	spin_lock_irqsave(&priv->rxlock, flags);
	if (index > priv->rx_stash_size)
		return count;

	if (index == priv->rx_stash_index)
		return count;

	priv->rx_stash_index = index;

	temp = gfar_read(&priv->regs->attreli);
	temp &= ~ATTRELI_EI_MASK;
	temp |= ATTRELI_EI(index);
	gfar_write(&priv->regs->attreli, flags);

	spin_unlock_irqrestore(&priv->rxlock, flags);

	return count;
}

static ssize_t gfar_show_fifo_threshold(struct class_device *cdev, char *buf)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);

	return sprintf(buf, "%d\n", priv->fifo_threshold);
}

static ssize_t gfar_set_fifo_threshold(struct class_device *cdev,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);
	unsigned int length = simple_strtoul(buf, NULL, 0);
	u32 temp;
	unsigned long flags;

	if (length > GFAR_MAX_FIFO_THRESHOLD)
		return count;

	spin_lock_irqsave(&priv->txlock, flags);

	priv->fifo_threshold = length;

	temp = gfar_read(&priv->regs->fifo_tx_thr);
	temp &= ~FIFO_TX_THR_MASK;
	temp |= length;
	gfar_write(&priv->regs->fifo_tx_thr, temp);

	spin_unlock_irqrestore(&priv->txlock, flags);

	return count;
}

static ssize_t gfar_show_fifo_starve(struct class_device *cdev, char *buf)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);

	return sprintf(buf, "%d\n", priv->fifo_starve);
}


static ssize_t gfar_set_fifo_starve(struct class_device *cdev,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);
	unsigned int num = simple_strtoul(buf, NULL, 0);
	u32 temp;
	unsigned long flags;

	if (num > GFAR_MAX_FIFO_STARVE)
		return count;

	spin_lock_irqsave(&priv->txlock, flags);

	priv->fifo_starve = num;

	temp = gfar_read(&priv->regs->fifo_tx_starve);
	temp &= ~FIFO_TX_STARVE_MASK;
	temp |= num;
	gfar_write(&priv->regs->fifo_tx_starve, temp);

	spin_unlock_irqrestore(&priv->txlock, flags);

	return count;
}

static ssize_t gfar_show_fifo_starve_off(struct class_device *cdev, char *buf)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);

	return sprintf(buf, "%d\n", priv->fifo_starve_off);
}

static ssize_t gfar_set_fifo_starve_off(struct class_device *cdev,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(cdev);
	struct gfar_private *priv = netdev_priv(dev);
	unsigned int num = simple_strtoul(buf, NULL, 0);
	u32 temp;
	unsigned long flags;

	if (num > GFAR_MAX_FIFO_STARVE_OFF)
		return count;

	spin_lock_irqsave(&priv->txlock, flags);

	priv->fifo_starve_off = num;

	temp = gfar_read(&priv->regs->fifo_tx_starve_shutoff);
	temp &= ~FIFO_TX_STARVE_OFF_MASK;
	temp |= num;
	gfar_write(&priv->regs->fifo_tx_starve_shutoff, temp);

	spin_unlock_irqrestore(&priv->txlock, flags);

	return count;
}

void gfar_init_sysfs(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	/* Initialize the default values */
	priv->rx_stash_size = DEFAULT_STASH_LENGTH;
	priv->rx_stash_index = DEFAULT_STASH_INDEX;
	priv->fifo_threshold = DEFAULT_FIFO_TX_THR;
	priv->fifo_starve = DEFAULT_FIFO_TX_STARVE;
	priv->fifo_starve_off = DEFAULT_FIFO_TX_STARVE_OFF;
	priv->bd_stash_en = DEFAULT_BD_STASH;

	/* Create our sysfs files */
	GFAR_CREATE_FILE(dev, bd_stash);
	GFAR_CREATE_FILE(dev, rx_stash_size);
	GFAR_CREATE_FILE(dev, rx_stash_index);
	GFAR_CREATE_FILE(dev, fifo_threshold);
	GFAR_CREATE_FILE(dev, fifo_starve);
	GFAR_CREATE_FILE(dev, fifo_starve_off);

}
