/*
 *      uvc_debugfs.c --  USB Video Class driver - Debugging support
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
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "uvcvideo.h"

/* -----------------------------------------------------------------------------
 * Global and stream initialization/cleanup
 */

static struct dentry *uvc_debugfs_root_dir;

int uvc_debugfs_init_stream(struct uvc_streaming *stream)
{
	struct usb_device *udev = stream->dev->udev;
	struct dentry *dent;
	char dir_name[32];

	if (uvc_debugfs_root_dir == NULL)
		return -ENODEV;

	sprintf(dir_name, "%u-%u", udev->bus->busnum, udev->devnum);

	dent = debugfs_create_dir(dir_name, uvc_debugfs_root_dir);
	if (IS_ERR_OR_NULL(dent)) {
		uvc_printk(KERN_INFO, "Unable to create debugfs %s directory.\n",
			   dir_name);
		return -ENODEV;
	}

	stream->debugfs_dir = dent;

	return 0;
}

void uvc_debugfs_cleanup_stream(struct uvc_streaming *stream)
{
	if (stream->debugfs_dir == NULL)
		return;

	debugfs_remove_recursive(stream->debugfs_dir);
	stream->debugfs_dir = NULL;
}

int uvc_debugfs_init(void)
{
	struct dentry *dir;

	dir = debugfs_create_dir("uvcvideo", usb_debug_root);
	if (IS_ERR_OR_NULL(dir)) {
		uvc_printk(KERN_INFO, "Unable to create debugfs directory\n");
		return -ENODATA;
	}

	uvc_debugfs_root_dir = dir;
	return 0;
}

void uvc_debugfs_cleanup(void)
{
	if (uvc_debugfs_root_dir != NULL)
		debugfs_remove_recursive(uvc_debugfs_root_dir);
}
