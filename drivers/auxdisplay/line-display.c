// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Character line display core support
 *
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * Copyright (C) 2021 Glider bv
 */

#include <generated/utsrelease.h>

#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/idr.h>
#include <linux/jiffies.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/timer.h>

#include <linux/map_to_7segment.h>
#include <linux/map_to_14segment.h>

#include "line-display.h"

#define DEFAULT_SCROLL_RATE	(HZ / 2)

/**
 * linedisp_scroll() - scroll the display by a character
 * @t: really a pointer to the private data structure
 *
 * Scroll the current message along the display by one character, rearming the
 * timer if required.
 */
static void linedisp_scroll(struct timer_list *t)
{
	struct linedisp *linedisp = from_timer(linedisp, t, timer);
	unsigned int i, ch = linedisp->scroll_pos;
	unsigned int num_chars = linedisp->num_chars;

	/* update the current message string */
	for (i = 0; i < num_chars;) {
		/* copy as many characters from the string as possible */
		for (; i < num_chars && ch < linedisp->message_len; i++, ch++)
			linedisp->buf[i] = linedisp->message[ch];

		/* wrap around to the start of the string */
		ch = 0;
	}

	/* update the display */
	linedisp->ops->update(linedisp);

	/* move on to the next character */
	linedisp->scroll_pos++;
	linedisp->scroll_pos %= linedisp->message_len;

	/* rearm the timer */
	if (linedisp->message_len > num_chars && linedisp->scroll_rate)
		mod_timer(&linedisp->timer, jiffies + linedisp->scroll_rate);
}

/**
 * linedisp_display() - set the message to be displayed
 * @linedisp: pointer to the private data structure
 * @msg: the message to display
 * @count: length of msg, or -1
 *
 * Display a new message @msg on the display. @msg can be longer than the
 * number of characters the display can display, in which case it will begin
 * scrolling across the display.
 *
 * Return: 0 on success, -ENOMEM on memory allocation failure
 */
static int linedisp_display(struct linedisp *linedisp, const char *msg,
			    ssize_t count)
{
	char *new_msg;

	/* stop the scroll timer */
	del_timer_sync(&linedisp->timer);

	if (count == -1)
		count = strlen(msg);

	/* if the string ends with a newline, trim it */
	if (msg[count - 1] == '\n')
		count--;

	if (!count) {
		/* Clear the display */
		kfree(linedisp->message);
		linedisp->message = NULL;
		linedisp->message_len = 0;
		memset(linedisp->buf, ' ', linedisp->num_chars);
		linedisp->ops->update(linedisp);
		return 0;
	}

	new_msg = kmemdup_nul(msg, count, GFP_KERNEL);
	if (!new_msg)
		return -ENOMEM;

	kfree(linedisp->message);

	linedisp->message = new_msg;
	linedisp->message_len = count;
	linedisp->scroll_pos = 0;

	/* update the display */
	linedisp_scroll(&linedisp->timer);

	return 0;
}

/**
 * message_show() - read message via sysfs
 * @dev: the display device
 * @attr: the display message attribute
 * @buf: the buffer to read the message into
 *
 * Read the current message being displayed or scrolled across the display into
 * @buf, for reads from sysfs.
 *
 * Return: the number of characters written to @buf
 */
static ssize_t message_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct linedisp *linedisp = container_of(dev, struct linedisp, dev);

	return sysfs_emit(buf, "%s\n", linedisp->message);
}

/**
 * message_store() - write a new message via sysfs
 * @dev: the display device
 * @attr: the display message attribute
 * @buf: the buffer containing the new message
 * @count: the size of the message in @buf
 *
 * Write a new message to display or scroll across the display from sysfs.
 *
 * Return: the size of the message on success, else -ERRNO
 */
static ssize_t message_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct linedisp *linedisp = container_of(dev, struct linedisp, dev);
	int err;

	err = linedisp_display(linedisp, buf, count);
	return err ?: count;
}

static DEVICE_ATTR_RW(message);

static ssize_t scroll_step_ms_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct linedisp *linedisp = container_of(dev, struct linedisp, dev);

	return sysfs_emit(buf, "%u\n", jiffies_to_msecs(linedisp->scroll_rate));
}

static ssize_t scroll_step_ms_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct linedisp *linedisp = container_of(dev, struct linedisp, dev);
	unsigned int ms;
	int err;

	err = kstrtouint(buf, 10, &ms);
	if (err)
		return err;

	linedisp->scroll_rate = msecs_to_jiffies(ms);
	if (linedisp->message && linedisp->message_len > linedisp->num_chars) {
		del_timer_sync(&linedisp->timer);
		if (linedisp->scroll_rate)
			linedisp_scroll(&linedisp->timer);
	}

	return count;
}

static DEVICE_ATTR_RW(scroll_step_ms);

static ssize_t map_seg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct linedisp *linedisp = container_of(dev, struct linedisp, dev);
	struct linedisp_map *map = linedisp->map;

	memcpy(buf, &map->map, map->size);
	return map->size;
}

static ssize_t map_seg_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct linedisp *linedisp = container_of(dev, struct linedisp, dev);
	struct linedisp_map *map = linedisp->map;

	if (count != map->size)
		return -EINVAL;

	memcpy(&map->map, buf, count);
	return count;
}

static const SEG7_DEFAULT_MAP(initial_map_seg7);
static DEVICE_ATTR(map_seg7, 0644, map_seg_show, map_seg_store);

static const SEG14_DEFAULT_MAP(initial_map_seg14);
static DEVICE_ATTR(map_seg14, 0644, map_seg_show, map_seg_store);

static struct attribute *linedisp_attrs[] = {
	&dev_attr_message.attr,
	&dev_attr_scroll_step_ms.attr,
	&dev_attr_map_seg7.attr,
	&dev_attr_map_seg14.attr,
	NULL
};

static umode_t linedisp_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct linedisp *linedisp = container_of(dev, struct linedisp, dev);
	struct linedisp_map *map = linedisp->map;
	umode_t mode = attr->mode;

	if (attr == &dev_attr_map_seg7.attr) {
		if (!map)
			return 0;
		if (map->type != LINEDISP_MAP_SEG7)
			return 0;
	}

	if (attr == &dev_attr_map_seg14.attr) {
		if (!map)
			return 0;
		if (map->type != LINEDISP_MAP_SEG14)
			return 0;
	}

	return mode;
};

static const struct attribute_group linedisp_group = {
	.is_visible	= linedisp_attr_is_visible,
	.attrs		= linedisp_attrs,
};
__ATTRIBUTE_GROUPS(linedisp);

static DEFINE_IDA(linedisp_id);

static void linedisp_release(struct device *dev)
{
	struct linedisp *linedisp = container_of(dev, struct linedisp, dev);

	kfree(linedisp->map);
	kfree(linedisp->message);
	kfree(linedisp->buf);
	ida_free(&linedisp_id, linedisp->id);
}

static const struct device_type linedisp_type = {
	.groups	= linedisp_groups,
	.release = linedisp_release,
};

static int linedisp_init_map(struct linedisp *linedisp)
{
	struct linedisp_map *map;
	int err;

	if (!linedisp->ops->get_map_type)
		return 0;

	err = linedisp->ops->get_map_type(linedisp);
	if (err < 0)
		return err;

	map = kmalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->type = err;

	/* assign initial mapping */
	switch (map->type) {
	case LINEDISP_MAP_SEG7:
		map->map.seg7 = initial_map_seg7;
		map->size = sizeof(map->map.seg7);
		break;
	case LINEDISP_MAP_SEG14:
		map->map.seg14 = initial_map_seg14;
		map->size = sizeof(map->map.seg14);
		break;
	default:
		kfree(map);
		return -EINVAL;
	}

	linedisp->map = map;

	return 0;
}

/**
 * linedisp_register - register a character line display
 * @linedisp: pointer to character line display structure
 * @parent: parent device
 * @num_chars: the number of characters that can be displayed
 * @ops: character line display operations
 *
 * Return: zero on success, else a negative error code.
 */
int linedisp_register(struct linedisp *linedisp, struct device *parent,
		      unsigned int num_chars, const struct linedisp_ops *ops)
{
	int err;

	memset(linedisp, 0, sizeof(*linedisp));
	linedisp->dev.parent = parent;
	linedisp->dev.type = &linedisp_type;
	linedisp->ops = ops;
	linedisp->num_chars = num_chars;
	linedisp->scroll_rate = DEFAULT_SCROLL_RATE;

	err = ida_alloc(&linedisp_id, GFP_KERNEL);
	if (err < 0)
		return err;
	linedisp->id = err;

	device_initialize(&linedisp->dev);
	dev_set_name(&linedisp->dev, "linedisp.%u", linedisp->id);

	err = -ENOMEM;
	linedisp->buf = kzalloc(linedisp->num_chars, GFP_KERNEL);
	if (!linedisp->buf)
		goto out_put_device;

	/* initialise a character mapping, if required */
	err = linedisp_init_map(linedisp);
	if (err)
		goto out_put_device;

	/* initialise a timer for scrolling the message */
	timer_setup(&linedisp->timer, linedisp_scroll, 0);

	err = device_add(&linedisp->dev);
	if (err)
		goto out_del_timer;

	/* display a default message */
	err = linedisp_display(linedisp, "Linux " UTS_RELEASE "       ", -1);
	if (err)
		goto out_del_dev;

	return 0;

out_del_dev:
	device_del(&linedisp->dev);
out_del_timer:
	del_timer_sync(&linedisp->timer);
out_put_device:
	put_device(&linedisp->dev);
	return err;
}
EXPORT_SYMBOL_NS_GPL(linedisp_register, LINEDISP);

/**
 * linedisp_unregister - unregister a character line display
 * @linedisp: pointer to character line display structure registered previously
 *	      with linedisp_register()
 */
void linedisp_unregister(struct linedisp *linedisp)
{
	device_del(&linedisp->dev);
	del_timer_sync(&linedisp->timer);
	put_device(&linedisp->dev);
}
EXPORT_SYMBOL_NS_GPL(linedisp_unregister, LINEDISP);

MODULE_LICENSE("GPL");
