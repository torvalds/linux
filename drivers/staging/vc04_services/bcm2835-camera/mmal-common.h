/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom BM2835 V4L2 driver
 *
 * Copyright © 2013 Raspberry Pi (Trading) Ltd.
 *
 * Authors: Vincent Sanders @ Collabora
 *          Dave Stevenson @ Broadcom
 *		(now dave.stevenson@raspberrypi.org)
 *          Simon Mellor @ Broadcom
 *          Luke Diamand @ Broadcom
 *
 * MMAL structures
 *
 */
#ifndef MMAL_COMMON_H
#define MMAL_COMMON_H

#define MMAL_FOURCC(a, b, c, d) ((a) | (b << 8) | (c << 16) | (d << 24))
#define MMAL_MAGIC MMAL_FOURCC('m', 'm', 'a', 'l')

/** Special value signalling that time is not known */
#define MMAL_TIME_UNKNOWN BIT_ULL(63)

struct mmal_msg_context;

/* mapping between v4l and mmal video modes */
struct mmal_fmt {
	u32   fourcc;          /* v4l2 format id */
	int   flags;           /* v4l2 flags field */
	u32   mmal;
	int   depth;
	u32   mmal_component;  /* MMAL component index to be used to encode */
	u32   ybbp;            /* depth of first Y plane for planar formats */
	bool  remove_padding;  /* Does the GPU have to remove padding,
				* or can we do hide padding via bytesperline.
				*/
};

/* buffer for one video frame */
struct mmal_buffer {
	/* v4l buffer data -- must be first */
	struct vb2_v4l2_buffer	vb;

	/* list of buffers available */
	struct list_head	list;

	void *buffer; /* buffer pointer */
	unsigned long buffer_size; /* size of allocated buffer */

	struct mmal_msg_context *msg_context;
};

/* */
struct mmal_colourfx {
	s32 enable;
	u32 u;
	u32 v;
};
#endif
