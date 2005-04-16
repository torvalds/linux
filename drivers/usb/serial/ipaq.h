/*
 * USB Compaq iPAQ driver
 *
 *	Copyright (C) 2001 - 2002
 *	    Ganesh Varadarajan <ganesh@veritas.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#ifndef __LINUX_USB_SERIAL_IPAQ_H
#define __LINUX_USB_SERIAL_IPAQ_H

/*
 * Since we can't queue our bulk write urbs (don't know why - it just
 * doesn't work), we can send down only one write urb at a time. The simplistic
 * approach taken by the generic usbserial driver will work, but it's not good
 * for performance. Therefore, we buffer upto URBDATA_QUEUE_MAX bytes of write
 * requests coming from the line discipline. This is done by chaining them
 * in lists of struct ipaq_packet, each packet holding a maximum of
 * PACKET_SIZE bytes.
 *
 * ipaq_write() can be called from bottom half context; hence we can't
 * allocate memory for packets there. So we initialize a pool of packets at
 * the first open and maintain a freelist.
 *
 * The value of PACKET_SIZE was empirically determined by
 * checking the maximum write sizes sent down by the ppp ldisc.
 * URBDATA_QUEUE_MAX is set to 64K, which is the maximum TCP window size.
 */

struct ipaq_packet {
	char			*data;
	size_t			len;
	size_t			written;
	struct list_head	list;
};

struct ipaq_private {
	int			active;
	int			queue_len;
	int			free_len;
	struct list_head	queue;
	struct list_head	freelist;
};

#define URBDATA_SIZE		4096
#define URBDATA_QUEUE_MAX	(64 * 1024)
#define PACKET_SIZE		256

#endif
