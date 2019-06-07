// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      uvc_debugfs.c --  USB Video Class driver - Debugging support
 *
 *      Copyright (C) 2011
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "uvcvideo.h"

/* -----------------------------------------------------------------------------
 * Statistics
 */

#define UVC_DEBUGFS_BUF_SIZE	1024

struct uvc_debugfs_buffer {
	size_t count;
	char data[UVC_DEBUGFS_BUF_SIZE];
};

static int uvc_debugfs_stats_open(struct inode *inode, struct file *file)
{
	struct uvc_streaming *stream = inode->i_private;
	struct uvc_debugfs_buffer *buf;

	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf->count = uvc_video_stats_dump(stream, buf->data, sizeof(buf->data));

	file->private_data = buf;
	return 0;
}

static ssize_t uvc_debugfs_stats_read(struct file *file, char __user *user_buf,
				      size_t nbytes, loff_t *ppos)
{
	struct uvc_debugfs_buffer *buf = file->private_data;

	return simple_read_from_buffer(user_buf, nbytes, ppos, buf->data,
				       buf->count);
}

static int uvc_debugfs_stats_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations uvc_debugfs_stats_fops = {
	.owner = THIS_MODULE,
	.open = uvc_debugfs_stats_open,
	.llseek = no_llseek,
	.read = uvc_debugfs_stats_read,
	.release = uvc_debugfs_stats_release,
};

/* -----------------------------------------------------------------------------
 * Global and stream initialization/cleanup
 */

static struct dentry *uvc_debugfs_root_dir;

void uvc_debugfs_init_stream(struct uvc_streaming *stream)
{
	struct usb_device *udev = stream->dev->udev;
	struct dentry *dent;
	char dir_name[32];

	if (uvc_debugfs_root_dir == NULL)
		return;

	sprintf(dir_name, "%u-%u", udev->bus->busnum, udev->devnum);

	dent = debugfs_create_dir(dir_name, uvc_debugfs_root_dir);
	if (IS_ERR_OR_NULL(dent)) {
		uvc_printk(KERN_INFO, "Unable to create debugfs %s "
			   "directory.\n", dir_name);
		return;
	}

	stream->debugfs_dir = dent;

	dent = debugfs_create_file("stats", 0444, stream->debugfs_dir,
				   stream, &uvc_debugfs_stats_fops);
	if (IS_ERR_OR_NULL(dent)) {
		uvc_printk(KERN_INFO, "Unable to create debugfs stats file.\n");
		uvc_debugfs_cleanup_stream(stream);
		return;
	}
}

void uvc_debugfs_cleanup_stream(struct uvc_streaming *stream)
{
	debugfs_remove_recursive(stream->debugfs_dir);
	stream->debugfs_dir = NULL;
}

void uvc_debugfs_init(void)
{
	struct dentry *dir;

	dir = debugfs_create_dir("uvcvideo", usb_debug_root);
	if (IS_ERR_OR_NULL(dir)) {
		uvc_printk(KERN_INFO, "Unable to create debugfs directory\n");
		return;
	}

	uvc_debugfs_root_dir = dir;
}

void uvc_debugfs_cleanup(void)
{
	debugfs_remove_recursive(uvc_debugfs_root_dir);
}
