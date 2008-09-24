/***************************************************************************
 * V4L2 driver for ZC0301[P] Image Processor and Control Chip              *
 *                                                                         *
 * Copyright (C) 2006-2007 by Luca Risolia <luca.risolia@studio.unibo.it>  *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program; if not, write to the Free Software             *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               *
 ***************************************************************************/

#ifndef _ZC0301_H_
#define _ZC0301_H_

#include <linux/version.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/kref.h>

#include "zc0301_sensor.h"

/*****************************************************************************/

#define ZC0301_DEBUG
#define ZC0301_DEBUG_LEVEL         2
#define ZC0301_MAX_DEVICES         64
#define ZC0301_FORCE_MUNMAP        0
#define ZC0301_MAX_FRAMES          32
#define ZC0301_COMPRESSION_QUALITY 0
#define ZC0301_URBS                2
#define ZC0301_ISO_PACKETS         7
#define ZC0301_ALTERNATE_SETTING   7
#define ZC0301_URB_TIMEOUT         msecs_to_jiffies(2 * ZC0301_ISO_PACKETS)
#define ZC0301_CTRL_TIMEOUT        100
#define ZC0301_FRAME_TIMEOUT       2

/*****************************************************************************/

ZC0301_ID_TABLE
ZC0301_SENSOR_TABLE

enum zc0301_frame_state {
	F_UNUSED,
	F_QUEUED,
	F_GRABBING,
	F_DONE,
	F_ERROR,
};

struct zc0301_frame_t {
	void* bufmem;
	struct v4l2_buffer buf;
	enum zc0301_frame_state state;
	struct list_head frame;
	unsigned long vma_use_count;
};

enum zc0301_dev_state {
	DEV_INITIALIZED = 0x01,
	DEV_DISCONNECTED = 0x02,
	DEV_MISCONFIGURED = 0x04,
};

enum zc0301_io_method {
	IO_NONE,
	IO_READ,
	IO_MMAP,
};

enum zc0301_stream_state {
	STREAM_OFF,
	STREAM_INTERRUPT,
	STREAM_ON,
};

struct zc0301_module_param {
	u8 force_munmap;
	u16 frame_timeout;
};

static DECLARE_RWSEM(zc0301_dev_lock);

struct zc0301_device {
	struct video_device* v4ldev;

	struct zc0301_sensor sensor;

	struct usb_device* usbdev;
	struct urb* urb[ZC0301_URBS];
	void* transfer_buffer[ZC0301_URBS];
	u8* control_buffer;

	struct zc0301_frame_t *frame_current, frame[ZC0301_MAX_FRAMES];
	struct list_head inqueue, outqueue;
	u32 frame_count, nbuffers, nreadbuffers;

	enum zc0301_io_method io;
	enum zc0301_stream_state stream;

	struct v4l2_jpegcompression compression;

	struct zc0301_module_param module_param;

	struct kref kref;
	enum zc0301_dev_state state;
	u8 users;

	struct completion probe;
	struct mutex open_mutex, fileop_mutex;
	spinlock_t queue_lock;
	wait_queue_head_t wait_open, wait_frame, wait_stream;
};

/*****************************************************************************/

struct zc0301_device*
zc0301_match_id(struct zc0301_device* cam, const struct usb_device_id *id)
{
	return usb_match_id(usb_ifnum_to_if(cam->usbdev, 0), id) ? cam : NULL;
}

void
zc0301_attach_sensor(struct zc0301_device* cam, struct zc0301_sensor* sensor)
{
	memcpy(&cam->sensor, sensor, sizeof(struct zc0301_sensor));
}

/*****************************************************************************/

#undef DBG
#undef KDBG
#ifdef ZC0301_DEBUG
#	define DBG(level, fmt, args...)                                       \
do {                                                                          \
	if (debug >= (level)) {                                               \
		if ((level) == 1)                                             \
			dev_err(&cam->usbdev->dev, fmt "\n", ## args);        \
		else if ((level) == 2)                                        \
			dev_info(&cam->usbdev->dev, fmt "\n", ## args);       \
		else if ((level) >= 3)                                        \
			dev_info(&cam->usbdev->dev, "[%s:%s:%d] " fmt "\n",   \
				 __FILE__, __func__, __LINE__ , ## args); \
	}                                                                     \
} while (0)
#	define KDBG(level, fmt, args...)                                      \
do {                                                                          \
	if (debug >= (level)) {                                               \
		if ((level) == 1 || (level) == 2)                             \
			pr_info("zc0301: " fmt "\n", ## args);                \
		else if ((level) == 3)                                        \
			pr_debug("sn9c102: [%s:%s:%d] " fmt "\n", __FILE__,   \
				 __func__, __LINE__ , ## args);           \
	}                                                                     \
} while (0)
#	define V4LDBG(level, name, cmd)                                       \
do {                                                                          \
	if (debug >= (level))                                                 \
		v4l_print_ioctl(name, cmd);                                   \
} while (0)
#else
#	define DBG(level, fmt, args...) do {;} while(0)
#	define KDBG(level, fmt, args...) do {;} while(0)
#	define V4LDBG(level, name, cmd) do {;} while(0)
#endif

#undef PDBG
#define PDBG(fmt, args...)                                                    \
dev_info(&cam->usbdev->dev, "[%s:%s:%d] " fmt "\n", __FILE__, __func__,   \
	 __LINE__ , ## args)

#undef PDBGG
#define PDBGG(fmt, args...) do {;} while(0) /* placeholder */

#endif /* _ZC0301_H_ */
