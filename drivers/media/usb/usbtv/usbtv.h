/*
 * Fushicai USBTV007 Video Grabber Driver
 *
 * Copyright (c) 2013 Lubomir Rintel
 * All rights reserved.
 * No physical hardware was harmed running Windows during the
 * reverse-engineering activity
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <media/v4l2-device.h>
#include <media/videobuf2-vmalloc.h>

/* Hardware. */
#define USBTV_VIDEO_ENDP	0x81
#define USBTV_BASE		0xc000
#define USBTV_REQUEST_REG	12

/* Number of concurrent isochronous urbs submitted.
 * Higher numbers was seen to overly saturate the USB bus. */
#define USBTV_ISOC_TRANSFERS	16
#define USBTV_ISOC_PACKETS	8

#define USBTV_CHUNK_SIZE	256
#define USBTV_CHUNK		240

/* Chunk header. */
#define USBTV_MAGIC_OK(chunk)	((be32_to_cpu(chunk[0]) & 0xff000000) \
							== 0x88000000)
#define USBTV_FRAME_ID(chunk)	((be32_to_cpu(chunk[0]) & 0x00ff0000) >> 16)
#define USBTV_ODD(chunk)	((be32_to_cpu(chunk[0]) & 0x0000f000) >> 15)
#define USBTV_CHUNK_NO(chunk)	(be32_to_cpu(chunk[0]) & 0x00000fff)

#define USBTV_TV_STD  (V4L2_STD_525_60 | V4L2_STD_PAL)

/* parameters for supported TV norms */
struct usbtv_norm_params {
	v4l2_std_id norm;
	int cap_width, cap_height;
};

/* A single videobuf2 frame buffer. */
struct usbtv_buf {
	struct vb2_buffer vb;
	struct list_head list;
};

/* Per-device structure. */
struct usbtv {
	struct device *dev;
	struct usb_device *udev;

	/* video */
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct vb2_queue vb2q;
	struct mutex v4l2_lock;
	struct mutex vb2q_lock;

	/* List of videobuf2 buffers protected by a lock. */
	spinlock_t buflock;
	struct list_head bufs;

	/* Number of currently processed frame, useful find
	 * out when a new one begins. */
	u32 frame_id;
	int chunks_done;

	enum {
		USBTV_COMPOSITE_INPUT,
		USBTV_SVIDEO_INPUT,
	} input;
	v4l2_std_id norm;
	int width, height;
	int n_chunks;
	int iso_size;
	unsigned int sequence;
	struct urb *isoc_urbs[USBTV_ISOC_TRANSFERS];
};

int usbtv_set_regs(struct usbtv *usbtv, const u16 regs[][2], int size);

int usbtv_video_init(struct usbtv *usbtv);
void usbtv_video_free(struct usbtv *usbtv);
