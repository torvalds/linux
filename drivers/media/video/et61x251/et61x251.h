/***************************************************************************
 * V4L2 driver for ET61X[12]51 PC Camera Controllers                       *
 *                                                                         *
 * Copyright (C) 2006 by Luca Risolia <luca.risolia@studio.unibo.it>       *
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

#ifndef _ET61X251_H_
#define _ET61X251_H_

#include <linux/version.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/stddef.h>
#include <linux/string.h>

#include "et61x251_sensor.h"

/*****************************************************************************/

#define ET61X251_DEBUG
#define ET61X251_DEBUG_LEVEL         2
#define ET61X251_MAX_DEVICES         64
#define ET61X251_PRESERVE_IMGSCALE   0
#define ET61X251_FORCE_MUNMAP        0
#define ET61X251_MAX_FRAMES          32
#define ET61X251_COMPRESSION_QUALITY 0
#define ET61X251_URBS                2
#define ET61X251_ISO_PACKETS         7
#define ET61X251_ALTERNATE_SETTING   13
#define ET61X251_URB_TIMEOUT         msecs_to_jiffies(2 * ET61X251_ISO_PACKETS)
#define ET61X251_CTRL_TIMEOUT        100
#define ET61X251_FRAME_TIMEOUT       2

/*****************************************************************************/

static const struct usb_device_id et61x251_id_table[] = {
	{ USB_DEVICE(0x102c, 0x6151), },
	{ USB_DEVICE(0x102c, 0x6251), },
	{ USB_DEVICE(0x102c, 0x6253), },
	{ USB_DEVICE(0x102c, 0x6254), },
	{ USB_DEVICE(0x102c, 0x6255), },
	{ USB_DEVICE(0x102c, 0x6256), },
	{ USB_DEVICE(0x102c, 0x6257), },
	{ USB_DEVICE(0x102c, 0x6258), },
	{ USB_DEVICE(0x102c, 0x6259), },
	{ USB_DEVICE(0x102c, 0x625a), },
	{ USB_DEVICE(0x102c, 0x625b), },
	{ USB_DEVICE(0x102c, 0x625c), },
	{ USB_DEVICE(0x102c, 0x625d), },
	{ USB_DEVICE(0x102c, 0x625e), },
	{ USB_DEVICE(0x102c, 0x625f), },
	{ USB_DEVICE(0x102c, 0x6260), },
	{ USB_DEVICE(0x102c, 0x6261), },
	{ USB_DEVICE(0x102c, 0x6262), },
	{ USB_DEVICE(0x102c, 0x6263), },
	{ USB_DEVICE(0x102c, 0x6264), },
	{ USB_DEVICE(0x102c, 0x6265), },
	{ USB_DEVICE(0x102c, 0x6266), },
	{ USB_DEVICE(0x102c, 0x6267), },
	{ USB_DEVICE(0x102c, 0x6268), },
	{ USB_DEVICE(0x102c, 0x6269), },
	{ }
};

ET61X251_SENSOR_TABLE

/*****************************************************************************/

enum et61x251_frame_state {
	F_UNUSED,
	F_QUEUED,
	F_GRABBING,
	F_DONE,
	F_ERROR,
};

struct et61x251_frame_t {
	void* bufmem;
	struct v4l2_buffer buf;
	enum et61x251_frame_state state;
	struct list_head frame;
	unsigned long vma_use_count;
};

enum et61x251_dev_state {
	DEV_INITIALIZED = 0x01,
	DEV_DISCONNECTED = 0x02,
	DEV_MISCONFIGURED = 0x04,
};

enum et61x251_io_method {
	IO_NONE,
	IO_READ,
	IO_MMAP,
};

enum et61x251_stream_state {
	STREAM_OFF,
	STREAM_INTERRUPT,
	STREAM_ON,
};

struct et61x251_sysfs_attr {
	u8 reg, i2c_reg;
};

struct et61x251_module_param {
	u8 force_munmap;
	u16 frame_timeout;
};

static DEFINE_MUTEX(et61x251_sysfs_lock);
static DECLARE_RWSEM(et61x251_disconnect);

struct et61x251_device {
	struct video_device* v4ldev;

	struct et61x251_sensor sensor;

	struct usb_device* usbdev;
	struct urb* urb[ET61X251_URBS];
	void* transfer_buffer[ET61X251_URBS];
	u8* control_buffer;

	struct et61x251_frame_t *frame_current, frame[ET61X251_MAX_FRAMES];
	struct list_head inqueue, outqueue;
	u32 frame_count, nbuffers, nreadbuffers;

	enum et61x251_io_method io;
	enum et61x251_stream_state stream;

	struct v4l2_jpegcompression compression;

	struct et61x251_sysfs_attr sysfs;
	struct et61x251_module_param module_param;

	enum et61x251_dev_state state;
	u8 users;

	struct mutex dev_mutex, fileop_mutex;
	spinlock_t queue_lock;
	wait_queue_head_t open, wait_frame, wait_stream;
};

/*****************************************************************************/

struct et61x251_device*
et61x251_match_id(struct et61x251_device* cam, const struct usb_device_id *id)
{
	if (usb_match_id(usb_ifnum_to_if(cam->usbdev, 0), id))
		return cam;

	return NULL;
}


void
et61x251_attach_sensor(struct et61x251_device* cam,
		       struct et61x251_sensor* sensor)
{
	memcpy(&cam->sensor, sensor, sizeof(struct et61x251_sensor));
}

/*****************************************************************************/

#undef DBG
#undef KDBG
#ifdef ET61X251_DEBUG
#	define DBG(level, fmt, args...)                                       \
do {                                                                          \
	if (debug >= (level)) {                                               \
		if ((level) == 1)                                             \
			dev_err(&cam->usbdev->dev, fmt "\n", ## args);        \
		else if ((level) == 2)                                        \
			dev_info(&cam->usbdev->dev, fmt "\n", ## args);       \
		else if ((level) >= 3)                                        \
			dev_info(&cam->usbdev->dev, "[%s:%d] " fmt "\n",      \
				 __FUNCTION__, __LINE__ , ## args);           \
	}                                                                     \
} while (0)
#	define KDBG(level, fmt, args...)                                      \
do {                                                                          \
	if (debug >= (level)) {                                               \
		if ((level) == 1 || (level) == 2)                             \
			pr_info("et61x251: " fmt "\n", ## args);              \
		else if ((level) == 3)                                        \
			pr_debug("et61x251: [%s:%d] " fmt "\n", __FUNCTION__, \
				 __LINE__ , ## args);                         \
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
dev_info(&cam->usbdev->dev, "[%s:%d] " fmt "\n",                              \
	 __FUNCTION__, __LINE__ , ## args)

#undef PDBGG
#define PDBGG(fmt, args...) do {;} while(0) /* placeholder */

#endif /* _ET61X251_H_ */
