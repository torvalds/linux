/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __VIDEO_ADF_ADF_FOPS_H
#define __VIDEO_ADF_ADF_FOPS_H

#include <linux/bitmap.h>
#include <linux/fs.h>

extern const struct file_operations adf_fops;

struct adf_file {
	struct list_head head;
	struct adf_obj *obj;

	DECLARE_BITMAP(event_subscriptions, ADF_EVENT_TYPE_MAX);
	u8 event_buf[4096];
	int event_head;
	int event_tail;
	wait_queue_head_t event_wait;
};

void adf_file_queue_event(struct adf_file *file, struct adf_event *event);
long adf_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif /* __VIDEO_ADF_ADF_FOPS_H */
