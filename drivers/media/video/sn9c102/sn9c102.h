/***************************************************************************
 * V4L2 driver for SN9C1xx PC Camera Controllers                           *
 *                                                                         *
 * Copyright (C) 2004-2006 by Luca Risolia <luca.risolia@studio.unibo.it>  *
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

#ifndef _SN9C102_H_
#define _SN9C102_H_

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
#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/kref.h>

#include "sn9c102_config.h"
#include "sn9c102_sensor.h"
#include "sn9c102_devtable.h"


enum sn9c102_frame_state {
	F_UNUSED,
	F_QUEUED,
	F_GRABBING,
	F_DONE,
	F_ERROR,
};

struct sn9c102_frame_t {
	void* bufmem;
	struct v4l2_buffer buf;
	enum sn9c102_frame_state state;
	struct list_head frame;
	unsigned long vma_use_count;
};

enum sn9c102_dev_state {
	DEV_INITIALIZED = 0x01,
	DEV_DISCONNECTED = 0x02,
	DEV_MISCONFIGURED = 0x04,
};

enum sn9c102_io_method {
	IO_NONE,
	IO_READ,
	IO_MMAP,
};

enum sn9c102_stream_state {
	STREAM_OFF,
	STREAM_INTERRUPT,
	STREAM_ON,
};

typedef char sn9c102_sof_header_t[62];

struct sn9c102_sof_t {
	sn9c102_sof_header_t header;
	u16 bytesread;
};

struct sn9c102_sysfs_attr {
	u16 reg, i2c_reg;
	sn9c102_sof_header_t frame_header;
};

struct sn9c102_module_param {
	u8 force_munmap;
	u16 frame_timeout;
};

static DEFINE_MUTEX(sn9c102_sysfs_lock);
static DECLARE_RWSEM(sn9c102_dev_lock);

struct sn9c102_device {
	struct video_device* v4ldev;

	enum sn9c102_bridge bridge;
	struct sn9c102_sensor sensor;

	struct usb_device* usbdev;
	struct urb* urb[SN9C102_URBS];
	void* transfer_buffer[SN9C102_URBS];
	u8* control_buffer;

	struct sn9c102_frame_t *frame_current, frame[SN9C102_MAX_FRAMES];
	struct list_head inqueue, outqueue;
	u32 frame_count, nbuffers, nreadbuffers;

	enum sn9c102_io_method io;
	enum sn9c102_stream_state stream;

	struct v4l2_jpegcompression compression;

	struct sn9c102_sysfs_attr sysfs;
	struct sn9c102_sof_t sof;
	u16 reg[384];

	struct sn9c102_module_param module_param;

	struct kref kref;
	enum sn9c102_dev_state state;
	u8 users;

	struct completion probe;
	struct mutex open_mutex, fileop_mutex;
	spinlock_t queue_lock;
	wait_queue_head_t wait_open, wait_frame, wait_stream;
};

/*****************************************************************************/

struct sn9c102_device*
sn9c102_match_id(struct sn9c102_device* cam, const struct usb_device_id *id)
{
	return usb_match_id(usb_ifnum_to_if(cam->usbdev, 0), id) ? cam : NULL;
}


void
sn9c102_attach_sensor(struct sn9c102_device* cam,
		      const struct sn9c102_sensor* sensor)
{
	memcpy(&cam->sensor, sensor, sizeof(struct sn9c102_sensor));
}


enum sn9c102_bridge
sn9c102_get_bridge(struct sn9c102_device* cam)
{
	return cam->bridge;
}


struct sn9c102_sensor* sn9c102_get_sensor(struct sn9c102_device* cam)
{
	return &cam->sensor;
}

/*****************************************************************************/

#undef DBG
#undef KDBG
#ifdef SN9C102_DEBUG
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
#	define V4LDBG(level, name, cmd)                                       \
do {                                                                          \
	if (debug >= (level))                                                 \
		v4l_print_ioctl(name, cmd);                                   \
} while (0)
#	define KDBG(level, fmt, args...)                                      \
do {                                                                          \
	if (debug >= (level)) {                                               \
		if ((level) == 1 || (level) == 2)                             \
			pr_info("sn9c102: " fmt "\n", ## args);               \
		else if ((level) == 3)                                        \
			pr_debug("sn9c102: [%s:%d] " fmt "\n",                \
				 __FUNCTION__, __LINE__ , ## args);           \
	}                                                                     \
} while (0)
#else
#	define DBG(level, fmt, args...) do {;} while(0)
#	define V4LDBG(level, name, cmd) do {;} while(0)
#	define KDBG(level, fmt, args...) do {;} while(0)
#endif

#undef PDBG
#define PDBG(fmt, args...)                                                    \
dev_info(&cam->usbdev->dev, "[%s:%s:%d] " fmt "\n", __FILE__, __FUNCTION__,   \
	 __LINE__ , ## args)

#undef PDBGG
#define PDBGG(fmt, args...) do {;} while(0) /* placeholder */

#endif /* _SN9C102_H_ */
