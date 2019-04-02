/*
 *      uvc_defs.c --  USB Video Class driver - Deging support
 *
 *      Copyright (C) 2011
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/defs.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "uvcvideo.h"

/* -----------------------------------------------------------------------------
 * Statistics
 */

#define UVC_DEFS_BUF_SIZE	1024

struct uvc_defs_buffer {
	size_t count;
	char data[UVC_DEFS_BUF_SIZE];
};

static int uvc_defs_stats_open(struct inode *inode, struct file *file)
{
	struct uvc_streaming *stream = inode->i_private;
	struct uvc_defs_buffer *buf;

	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf->count = uvc_video_stats_dump(stream, buf->data, sizeof(buf->data));

	file->private_data = buf;
	return 0;
}

static ssize_t uvc_defs_stats_read(struct file *file, char __user *user_buf,
				      size_t nbytes, loff_t *ppos)
{
	struct uvc_defs_buffer *buf = file->private_data;

	return simple_read_from_buffer(user_buf, nbytes, ppos, buf->data,
				       buf->count);
}

static int uvc_defs_stats_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations uvc_defs_stats_fops = {
	.owner = THIS_MODULE,
	.open = uvc_defs_stats_open,
	.llseek = no_llseek,
	.read = uvc_defs_stats_read,
	.release = uvc_defs_stats_release,
};

/* -----------------------------------------------------------------------------
 * Global and stream initialization/cleanup
 */

static struct dentry *uvc_defs_root_dir;

void uvc_defs_init_stream(struct uvc_streaming *stream)
{
	struct usb_device *udev = stream->dev->udev;
	struct dentry *dent;
	char dir_name[32];

	if (uvc_defs_root_dir == NULL)
		return;

	sprintf(dir_name, "%u-%u", udev->bus->busnum, udev->devnum);

	dent = defs_create_dir(dir_name, uvc_defs_root_dir);
	if (IS_ERR_OR_NULL(dent)) {
		uvc_printk(KERN_INFO, "Unable to create defs %s "
			   "directory.\n", dir_name);
		return;
	}

	stream->defs_dir = dent;

	dent = defs_create_file("stats", 0444, stream->defs_dir,
				   stream, &uvc_defs_stats_fops);
	if (IS_ERR_OR_NULL(dent)) {
		uvc_printk(KERN_INFO, "Unable to create defs stats file.\n");
		uvc_defs_cleanup_stream(stream);
		return;
	}
}

void uvc_defs_cleanup_stream(struct uvc_streaming *stream)
{
	defs_remove_recursive(stream->defs_dir);
	stream->defs_dir = NULL;
}

void uvc_defs_init(void)
{
	struct dentry *dir;

	dir = defs_create_dir("uvcvideo", usb_de_root);
	if (IS_ERR_OR_NULL(dir)) {
		uvc_printk(KERN_INFO, "Unable to create defs directory\n");
		return;
	}

	uvc_defs_root_dir = dir;
}

void uvc_defs_cleanup(void)
{
	defs_remove_recursive(uvc_defs_root_dir);
}
