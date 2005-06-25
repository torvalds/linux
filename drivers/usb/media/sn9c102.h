/***************************************************************************
 * V4L2 driver for SN9C10x PC Camera Controllers                           *
 *                                                                         *
 * Copyright (C) 2004-2005 by Luca Risolia <luca.risolia@studio.unibo.it>  *
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
#include <linux/videodev.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/rwsem.h>
#include <asm/semaphore.h>

#include "sn9c102_sensor.h"

/*****************************************************************************/

#define SN9C102_DEBUG
#define SN9C102_DEBUG_LEVEL       2
#define SN9C102_MAX_DEVICES       64
#define SN9C102_PRESERVE_IMGSCALE 0
#define SN9C102_FORCE_MUNMAP      0
#define SN9C102_MAX_FRAMES        32
#define SN9C102_URBS              2
#define SN9C102_ISO_PACKETS       7
#define SN9C102_ALTERNATE_SETTING 8
#define SN9C102_URB_TIMEOUT       msecs_to_jiffies(2 * SN9C102_ISO_PACKETS)
#define SN9C102_CTRL_TIMEOUT      300

/*****************************************************************************/

#define SN9C102_MODULE_NAME     "V4L2 driver for SN9C10x PC Camera Controllers"
#define SN9C102_MODULE_AUTHOR   "(C) 2004-2005 Luca Risolia"
#define SN9C102_AUTHOR_EMAIL    "<luca.risolia@studio.unibo.it>"
#define SN9C102_MODULE_LICENSE  "GPL"
#define SN9C102_MODULE_VERSION  "1:1.24a"
#define SN9C102_MODULE_VERSION_CODE  KERNEL_VERSION(1, 0, 24)

enum sn9c102_bridge {
	BRIDGE_SN9C101 = 0x01,
	BRIDGE_SN9C102 = 0x02,
	BRIDGE_SN9C103 = 0x04,
};

SN9C102_ID_TABLE
SN9C102_SENSOR_TABLE

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

typedef char sn9c102_sof_header_t[12];
typedef char sn9c102_eof_header_t[4];

struct sn9c102_sysfs_attr {
	u8 reg, i2c_reg;
	sn9c102_sof_header_t frame_header;
};

struct sn9c102_module_param {
	u8 force_munmap;
};

static DECLARE_MUTEX(sn9c102_sysfs_lock);
static DECLARE_RWSEM(sn9c102_disconnect);

struct sn9c102_device {
	struct device dev;

	struct video_device* v4ldev;

	enum sn9c102_bridge bridge;
	struct sn9c102_sensor* sensor;

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
	sn9c102_sof_header_t sof_header;
	u16 reg[32];

	struct sn9c102_module_param module_param;

	enum sn9c102_dev_state state;
	u8 users;

	struct semaphore dev_sem, fileop_sem;
	spinlock_t queue_lock;
	wait_queue_head_t open, wait_frame, wait_stream;
};

/*****************************************************************************/

void
sn9c102_attach_sensor(struct sn9c102_device* cam,
                      struct sn9c102_sensor* sensor)
{
	cam->sensor = sensor;
	cam->sensor->dev = &cam->dev;
	cam->sensor->usbdev = cam->usbdev;
}

/*****************************************************************************/

#undef DBG
#undef KDBG
#ifdef SN9C102_DEBUG
#	define DBG(level, fmt, args...)                                       \
{                                                                             \
	if (debug >= (level)) {                                               \
		if ((level) == 1)                                             \
			dev_err(&cam->dev, fmt "\n", ## args);                \
		else if ((level) == 2)                                        \
			dev_info(&cam->dev, fmt "\n", ## args);               \
		else if ((level) >= 3)                                        \
			dev_info(&cam->dev, "[%s:%d] " fmt "\n",              \
			         __FUNCTION__, __LINE__ , ## args);           \
	}                                                                     \
}
#	define KDBG(level, fmt, args...)                                      \
{                                                                             \
	if (debug >= (level)) {                                               \
		if ((level) == 1 || (level) == 2)                             \
			pr_info("sn9c102: " fmt "\n", ## args);               \
		else if ((level) == 3)                                        \
			pr_debug("sn9c102: [%s:%d] " fmt "\n", __FUNCTION__,  \
			         __LINE__ , ## args);                         \
	}                                                                     \
}
#else
#	define KDBG(level, fmt, args...) do {;} while(0);
#	define DBG(level, fmt, args...) do {;} while(0);
#endif

#undef PDBG
#define PDBG(fmt, args...)                                                    \
dev_info(&cam->dev, "[%s:%d] " fmt "\n", __FUNCTION__, __LINE__ , ## args);

#undef PDBGG
#define PDBGG(fmt, args...) do {;} while(0); /* placeholder */

#endif /* _SN9C102_H_ */
