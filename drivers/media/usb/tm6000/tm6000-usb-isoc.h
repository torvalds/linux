/*
 * SPDX-License-Identifier: GPL-2.0
 * tm6000-buf.c - driver for TM5600/TM6000/TM6010 USB video capture devices
 *
 * Copyright (c) 2006-2007 Mauro Carvalho Chehab <mchehab@kernel.org>
 */

#include <linux/videodev2.h>

#define TM6000_URB_MSG_LEN 180

struct usb_isoc_ctl {
		/* max packet size of isoc transaction */
	int				max_pkt_size;

		/* number of allocated urbs */
	int				num_bufs;

		/* urb for isoc transfers */
	struct urb			**urb;

		/* transfer buffers for isoc transfer */
	char				**transfer_buffer;

		/* Last buffer command and region */
	u8				cmd;
	int				pos, size, pktsize;

		/* Last field: ODD or EVEN? */
	int				vfield, field;

		/* Stores incomplete commands */
	u32				tmp_buf;
	int				tmp_buf_len;

		/* Stores already requested buffers */
	struct tm6000_buffer		*buf;
};
