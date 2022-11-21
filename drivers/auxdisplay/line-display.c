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

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/timer.h>

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
	linedisp->update(linedisp);

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
		linedisp->update(linedisp);
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

	if (kstrtouint(buf, 10, &ms) != 0)
		return -EINVAL;

	linedisp->scroll_rate = msecs_to_jiffies(ms);
	if (linedisp->message && linedisp->message_len > linedisp->num_chars) {
		del_timer_sync(&linedisp->timer);
		if (linedisp->scroll_rate)
			linedisp_scroll(&linedisp->timer);
	}

	return count;
}

static DEVICE_ATTR_RW(scroll_step_ms);

static struct attribute *linedisp_attrs[] = {
	&dev_attr_message.attr,
	&dev_attr_scroll_step_ms.attr,
	NULL,
};
ATTRIBUTE_GROUPS(linedisp);

static const struct device_type linedisp_type = {
	.groups	= linedisp_groups,
};

/**
 * linedisp_register - register a character line display
 * @linedisp: pointer to character line display structure
 * @parent: parent device
 * @num_chars: the number of characters that can be displayed
 * @buf: pointer to a buffer that can hold @num_chars characters
 * @update: Function called to update the display.  This must not sleep!
 *
 * Return: zero on success, else a negative error code.
 */
int linedisp_register(struct linedisp *linedisp, struct device *parent,
		      unsigned int num_chars, char *buf,
		      void (*update)(struct linedisp *linedisp))
{
	static atomic_t linedisp_id = ATOMIC_INIT(-1);
	int err;

	memset(linedisp, 0, sizeof(*linedisp));
	linedisp->dev.parent = parent;
	linedisp->dev.type = &linedisp_type;
	linedisp->update = update;
	linedisp->buf = buf;
	linedisp->num_chars = num_chars;
	linedisp->scroll_rate = DEFAULT_SCROLL_RATE;

	device_initialize(&linedisp->dev);
	dev_set_name(&linedisp->dev, "linedisp.%lu",
		     (unsigned long)atomic_inc_return(&linedisp_id));

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
	put_device(&linedisp->dev);
	return err;
}
EXPORT_SYMBOL_GPL(linedisp_register);

/**
 * linedisp_unregister - unregister a character line display
 * @linedisp: pointer to character line display structure registered previously
 *	      with linedisp_register()
 */
void linedisp_unregister(struct linedisp *linedisp)
{
	device_del(&linedisp->dev);
	del_timer_sync(&linedisp->timer);
	kfree(linedisp->message);
	put_device(&linedisp->dev);
}
EXPORT_SYMBOL_GPL(linedisp_unregister);

MODULE_LICENSE("GPL");
