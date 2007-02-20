/*
 * sisusb - usb kernel driver for SiS315(E) based USB2VGA dongles
 *
 * Main part
 *
 * Copyright (C) 2005 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, this code is licensed under the
 * terms of the GPL v2.
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) The name of the author may not be used to endorse or promote products
 * *    derived from this software without specific psisusbr written permission.
 * *
 * * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESSED OR
 * * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>

#include "sisusb.h"
#include "sisusb_init.h"

#ifdef INCL_SISUSB_CON
#include <linux/font.h>
#endif

#define SISUSB_DONTSYNC

/* Forward declarations / clean-up routines */

#ifdef INCL_SISUSB_CON
static int sisusb_first_vc = 0;
static int sisusb_last_vc = 0;
module_param_named(first, sisusb_first_vc, int, 0);
module_param_named(last, sisusb_last_vc, int, 0);
MODULE_PARM_DESC(first, "Number of first console to take over (1 - MAX_NR_CONSOLES)");
MODULE_PARM_DESC(last, "Number of last console to take over (1 - MAX_NR_CONSOLES)");
#endif

static struct usb_driver sisusb_driver;

DEFINE_MUTEX(disconnect_mutex);

static void
sisusb_free_buffers(struct sisusb_usb_data *sisusb)
{
	int i;

	for (i = 0; i < NUMOBUFS; i++) {
		if (sisusb->obuf[i]) {
			usb_buffer_free(sisusb->sisusb_dev, sisusb->obufsize,
				sisusb->obuf[i], sisusb->transfer_dma_out[i]);
			sisusb->obuf[i] = NULL;
		}
	}
	if (sisusb->ibuf) {
		usb_buffer_free(sisusb->sisusb_dev, sisusb->ibufsize,
			sisusb->ibuf, sisusb->transfer_dma_in);
		sisusb->ibuf = NULL;
	}
}

static void
sisusb_free_urbs(struct sisusb_usb_data *sisusb)
{
	int i;

	for (i = 0; i < NUMOBUFS; i++) {
		usb_free_urb(sisusb->sisurbout[i]);
		sisusb->sisurbout[i] = NULL;
	}
	usb_free_urb(sisusb->sisurbin);
	sisusb->sisurbin = NULL;
}

/* Level 0: USB transport layer */

/* 1. out-bulks */

/* out-urb management */

/* Return 1 if all free, 0 otherwise */
static int
sisusb_all_free(struct sisusb_usb_data *sisusb)
{
	int i;

	for (i = 0; i < sisusb->numobufs; i++) {

		if (sisusb->urbstatus[i] & SU_URB_BUSY)
			return 0;

	}

	return 1;
}

/* Kill all busy URBs */
static void
sisusb_kill_all_busy(struct sisusb_usb_data *sisusb)
{
	int i;

	if (sisusb_all_free(sisusb))
		return;

	for (i = 0; i < sisusb->numobufs; i++) {

		if (sisusb->urbstatus[i] & SU_URB_BUSY)
			usb_kill_urb(sisusb->sisurbout[i]);

	}
}

/* Return 1 if ok, 0 if error (not all complete within timeout) */
static int
sisusb_wait_all_out_complete(struct sisusb_usb_data *sisusb)
{
	int timeout = 5 * HZ, i = 1;

	wait_event_timeout(sisusb->wait_q,
				(i = sisusb_all_free(sisusb)),
				 timeout);

	return i;
}

static int
sisusb_outurb_available(struct sisusb_usb_data *sisusb)
{
	int i;

	for (i = 0; i < sisusb->numobufs; i++) {

		if ((sisusb->urbstatus[i] & (SU_URB_BUSY|SU_URB_ALLOC)) == 0)
			return i;

	}

	return -1;
}

static int
sisusb_get_free_outbuf(struct sisusb_usb_data *sisusb)
{
	int i, timeout = 5 * HZ;

	wait_event_timeout(sisusb->wait_q,
				((i = sisusb_outurb_available(sisusb)) >= 0),
				timeout);

	return i;
}

static int
sisusb_alloc_outbuf(struct sisusb_usb_data *sisusb)
{
	int i;

	i = sisusb_outurb_available(sisusb);

	if (i >= 0)
		sisusb->urbstatus[i] |= SU_URB_ALLOC;

	return i;
}

static void
sisusb_free_outbuf(struct sisusb_usb_data *sisusb, int index)
{
	if ((index >= 0) && (index < sisusb->numobufs))
		sisusb->urbstatus[index] &= ~SU_URB_ALLOC;
}

/* completion callback */

static void
sisusb_bulk_completeout(struct urb *urb)
{
	struct sisusb_urb_context *context = urb->context;
	struct sisusb_usb_data *sisusb;

	if (!context)
		return;

	sisusb = context->sisusb;

	if (!sisusb || !sisusb->sisusb_dev || !sisusb->present)
		return;

#ifndef SISUSB_DONTSYNC
	if (context->actual_length)
		*(context->actual_length) += urb->actual_length;
#endif

	sisusb->urbstatus[context->urbindex] &= ~SU_URB_BUSY;
	wake_up(&sisusb->wait_q);
}

static int
sisusb_bulkout_msg(struct sisusb_usb_data *sisusb, int index, unsigned int pipe, void *data,
		int len, int *actual_length, int timeout, unsigned int tflags,
		dma_addr_t transfer_dma)
{
	struct urb *urb = sisusb->sisurbout[index];
	int retval, byteswritten = 0;

	/* Set up URB */
	urb->transfer_flags = 0;

	usb_fill_bulk_urb(urb, sisusb->sisusb_dev, pipe, data, len,
		sisusb_bulk_completeout, &sisusb->urbout_context[index]);

	urb->transfer_flags |= tflags;
	urb->actual_length = 0;

	if ((urb->transfer_dma = transfer_dma))
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* Set up context */
	sisusb->urbout_context[index].actual_length = (timeout) ?
						NULL : actual_length;

	/* Declare this urb/buffer in use */
	sisusb->urbstatus[index] |= SU_URB_BUSY;

	/* Submit URB */
	retval = usb_submit_urb(urb, GFP_ATOMIC);

	/* If OK, and if timeout > 0, wait for completion */
	if ((retval == 0) && timeout) {
		wait_event_timeout(sisusb->wait_q,
				   (!(sisusb->urbstatus[index] & SU_URB_BUSY)),
				   timeout);
		if (sisusb->urbstatus[index] & SU_URB_BUSY) {
			/* URB timed out... kill it and report error */
			usb_kill_urb(urb);
			retval = -ETIMEDOUT;
		} else {
			/* Otherwise, report urb status */
			retval = urb->status;
			byteswritten = urb->actual_length;
		}
	}

	if (actual_length)
		*actual_length = byteswritten;

	return retval;
}

/* 2. in-bulks */

/* completion callback */

static void
sisusb_bulk_completein(struct urb *urb)
{
	struct sisusb_usb_data *sisusb = urb->context;

	if (!sisusb || !sisusb->sisusb_dev || !sisusb->present)
		return;

	sisusb->completein = 1;
	wake_up(&sisusb->wait_q);
}

static int
sisusb_bulkin_msg(struct sisusb_usb_data *sisusb, unsigned int pipe, void *data, int len,
		int *actual_length, int timeout, unsigned int tflags, dma_addr_t transfer_dma)
{
	struct urb *urb = sisusb->sisurbin;
	int retval, readbytes = 0;

	urb->transfer_flags = 0;

	usb_fill_bulk_urb(urb, sisusb->sisusb_dev, pipe, data, len,
			sisusb_bulk_completein, sisusb);

	urb->transfer_flags |= tflags;
	urb->actual_length = 0;

	if ((urb->transfer_dma = transfer_dma))
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	sisusb->completein = 0;
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval == 0) {
		wait_event_timeout(sisusb->wait_q, sisusb->completein, timeout);
		if (!sisusb->completein) {
			/* URB timed out... kill it and report error */
			usb_kill_urb(urb);
			retval = -ETIMEDOUT;
		} else {
			/* URB completed within timout */
			retval = urb->status;
			readbytes = urb->actual_length;
		}
	}

	if (actual_length)
		*actual_length = readbytes;

	return retval;
}


/* Level 1:  */

/* Send a bulk message of variable size
 *
 * To copy the data from userspace, give pointer to "userbuffer",
 * to copy from (non-DMA) kernel memory, give "kernbuffer". If
 * both of these are NULL, it is assumed, that the transfer
 * buffer "sisusb->obuf[index]" is set up with the data to send.
 * Index is ignored if either kernbuffer or userbuffer is set.
 * If async is nonzero, URBs will be sent without waiting for
 * completion of the previous URB.
 *
 * (return 0 on success)
 */

static int sisusb_send_bulk_msg(struct sisusb_usb_data *sisusb, int ep, int len,
		char *kernbuffer, const char __user *userbuffer, int index,
		ssize_t *bytes_written, unsigned int tflags, int async)
{
	int result = 0, retry, count = len;
	int passsize, thispass, transferred_len = 0;
	int fromuser = (userbuffer != NULL) ? 1 : 0;
	int fromkern = (kernbuffer != NULL) ? 1 : 0;
	unsigned int pipe;
	char *buffer;

	(*bytes_written) = 0;

	/* Sanity check */
	if (!sisusb || !sisusb->present || !sisusb->sisusb_dev)
		return -ENODEV;

	/* If we copy data from kernel or userspace, force the
	 * allocation of a buffer/urb. If we have the data in
	 * the transfer buffer[index] already, reuse the buffer/URB
	 * if the length is > buffer size. (So, transmitting
	 * large data amounts directly from the transfer buffer
	 * treats the buffer as a ring buffer. However, we need
	 * to sync in this case.)
	 */
	if (fromuser || fromkern)
		index = -1;
	else if (len > sisusb->obufsize)
		async = 0;

	pipe = usb_sndbulkpipe(sisusb->sisusb_dev, ep);

	do {
		passsize = thispass = (sisusb->obufsize < count) ?
						sisusb->obufsize : count;

		if (index < 0)
			index = sisusb_get_free_outbuf(sisusb);

		if (index < 0)
			return -EIO;

		buffer = sisusb->obuf[index];

		if (fromuser) {

			if (copy_from_user(buffer, userbuffer, passsize))
				return -EFAULT;

			userbuffer += passsize;

		} else if (fromkern) {

			memcpy(buffer, kernbuffer, passsize);
			kernbuffer += passsize;

		}

		retry = 5;
		while (thispass) {

			if (!sisusb->sisusb_dev)
				return -ENODEV;

			result = sisusb_bulkout_msg(sisusb,
						index,
						pipe,
						buffer,
						thispass,
						&transferred_len,
						async ? 0 : 5 * HZ,
						tflags,
						sisusb->transfer_dma_out[index]);

			if (result == -ETIMEDOUT) {

				/* Will not happen if async */
				if (!retry--)
					return -ETIME;

				continue;

			} else if ((result == 0) && !async && transferred_len) {

				thispass -= transferred_len;
				if (thispass) {
					if (sisusb->transfer_dma_out) {
						/* If DMA, copy remaining
						 * to beginning of buffer
						 */
						memcpy(buffer,
						       buffer + transferred_len,
						       thispass);
					} else {
						/* If not DMA, simply increase
						 * the pointer
						 */
						buffer += transferred_len;
					}
				}

			} else
				break;
		};

		if (result)
			return result;

		(*bytes_written) += passsize;
		count            -= passsize;

		/* Force new allocation in next iteration */
		if (fromuser || fromkern)
			index = -1;

	} while (count > 0);

	if (async) {
#ifdef SISUSB_DONTSYNC
		(*bytes_written) = len;
		/* Some URBs/buffers might be busy */
#else
		sisusb_wait_all_out_complete(sisusb);
		(*bytes_written) = transferred_len;
		/* All URBs and all buffers are available */
#endif
	}

	return ((*bytes_written) == len) ? 0 : -EIO;
}

/* Receive a bulk message of variable size
 *
 * To copy the data to userspace, give pointer to "userbuffer",
 * to copy to kernel memory, give "kernbuffer". One of them
 * MUST be set. (There is no technique for letting the caller
 * read directly from the ibuf.)
 *
 */

static int sisusb_recv_bulk_msg(struct sisusb_usb_data *sisusb, int ep, int len,
		void *kernbuffer, char __user *userbuffer, ssize_t *bytes_read,
		unsigned int tflags)
{
	int result = 0, retry, count = len;
	int bufsize, thispass, transferred_len;
	unsigned int pipe;
	char *buffer;

	(*bytes_read) = 0;

	/* Sanity check */
	if (!sisusb || !sisusb->present || !sisusb->sisusb_dev)
		return -ENODEV;

	pipe = usb_rcvbulkpipe(sisusb->sisusb_dev, ep);
	buffer = sisusb->ibuf;
	bufsize = sisusb->ibufsize;

	retry = 5;

#ifdef SISUSB_DONTSYNC
	if (!(sisusb_wait_all_out_complete(sisusb)))
		return -EIO;
#endif

	while (count > 0) {

		if (!sisusb->sisusb_dev)
			return -ENODEV;

		thispass = (bufsize < count) ? bufsize : count;

		result = sisusb_bulkin_msg(sisusb,
					   pipe,
					   buffer,
					   thispass,
					   &transferred_len,
					   5 * HZ,
					   tflags,
					   sisusb->transfer_dma_in);

		if (transferred_len)
			thispass = transferred_len;

		else if (result == -ETIMEDOUT) {

			if (!retry--)
				return -ETIME;

			continue;

		} else
			return -EIO;


		if (thispass) {

			(*bytes_read) += thispass;
			count         -= thispass;

			if (userbuffer) {

				if (copy_to_user(userbuffer, buffer, thispass))
					return -EFAULT;

				userbuffer += thispass;

			} else {

				memcpy(kernbuffer, buffer, thispass);
				kernbuffer += thispass;

			}

		}

	}

	return ((*bytes_read) == len) ? 0 : -EIO;
}

static int sisusb_send_packet(struct sisusb_usb_data *sisusb, int len,
						struct sisusb_packet *packet)
{
	int ret;
	ssize_t bytes_transferred = 0;
	__le32 tmp;

	if (len == 6)
		packet->data = 0;

#ifdef SISUSB_DONTSYNC
	if (!(sisusb_wait_all_out_complete(sisusb)))
		return 1;
#endif

	/* Eventually correct endianness */
	SISUSB_CORRECT_ENDIANNESS_PACKET(packet);

	/* 1. send the packet */
	ret = sisusb_send_bulk_msg(sisusb, SISUSB_EP_GFX_OUT, len,
			(char *)packet, NULL, 0, &bytes_transferred, 0, 0);

	if ((ret == 0) && (len == 6)) {

		/* 2. if packet len == 6, it means we read, so wait for 32bit
		 *    return value and write it to packet->data
		 */
		ret = sisusb_recv_bulk_msg(sisusb, SISUSB_EP_GFX_IN, 4,
				(char *)&tmp, NULL, &bytes_transferred, 0);

		packet->data = le32_to_cpu(tmp);
	}

	return ret;
}

static int sisusb_send_bridge_packet(struct sisusb_usb_data *sisusb, int len,
					struct sisusb_packet *packet,
					unsigned int tflags)
{
	int ret;
	ssize_t bytes_transferred = 0;
	__le32 tmp;

	if (len == 6)
		packet->data = 0;

#ifdef SISUSB_DONTSYNC
	if (!(sisusb_wait_all_out_complete(sisusb)))
		return 1;
#endif

	/* Eventually correct endianness */
	SISUSB_CORRECT_ENDIANNESS_PACKET(packet);

	/* 1. send the packet */
	ret = sisusb_send_bulk_msg(sisusb, SISUSB_EP_BRIDGE_OUT, len,
			(char *)packet, NULL, 0, &bytes_transferred, tflags, 0);

	if ((ret == 0) && (len == 6)) {

		/* 2. if packet len == 6, it means we read, so wait for 32bit
		 *    return value and write it to packet->data
		 */
		ret = sisusb_recv_bulk_msg(sisusb, SISUSB_EP_BRIDGE_IN, 4,
				(char *)&tmp, NULL, &bytes_transferred, 0);

		packet->data = le32_to_cpu(tmp);
	}

	return ret;
}

/* access video memory and mmio (return 0 on success) */

/* Low level */

/* The following routines assume being used to transfer byte, word,
 * long etc.
 * This means that
 *   - the write routines expect "data" in machine endianness format.
 *     The data will be converted to leXX in sisusb_xxx_packet.
 *   - the read routines can expect read data in machine-endianess.
 */

static int sisusb_write_memio_byte(struct sisusb_usb_data *sisusb, int type,
							u32 addr, u8 data)
{
	struct sisusb_packet packet;
	int ret;

	packet.header  = (1 << (addr & 3)) | (type << 6);
	packet.address = addr & ~3;
	packet.data    = data << ((addr & 3) << 3);
	ret = sisusb_send_packet(sisusb, 10, &packet);
	return ret;
}

static int sisusb_write_memio_word(struct sisusb_usb_data *sisusb, int type,
							u32 addr, u16 data)
{
	struct sisusb_packet packet;
	int ret = 0;

	packet.address = addr & ~3;

	switch (addr & 3) {
		case 0:
			packet.header = (type << 6) | 0x0003;
			packet.data   = (u32)data;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 1:
			packet.header = (type << 6) | 0x0006;
			packet.data   = (u32)data << 8;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 2:
			packet.header = (type << 6) | 0x000c;
			packet.data   = (u32)data << 16;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 3:
			packet.header = (type << 6) | 0x0008;
			packet.data   = (u32)data << 24;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			packet.header = (type << 6) | 0x0001;
			packet.address = (addr & ~3) + 4;
			packet.data   = (u32)data >> 8;
			ret |= sisusb_send_packet(sisusb, 10, &packet);
	}

	return ret;
}

static int sisusb_write_memio_24bit(struct sisusb_usb_data *sisusb, int type,
							u32 addr, u32 data)
{
	struct sisusb_packet packet;
	int ret = 0;

	packet.address = addr & ~3;

	switch (addr & 3) {
		case 0:
			packet.header  = (type << 6) | 0x0007;
			packet.data    = data & 0x00ffffff;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 1:
			packet.header  = (type << 6) | 0x000e;
			packet.data    = data << 8;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 2:
			packet.header  = (type << 6) | 0x000c;
			packet.data    = data << 16;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			packet.header  = (type << 6) | 0x0001;
			packet.address = (addr & ~3) + 4;
			packet.data    = (data >> 16) & 0x00ff;
			ret |= sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 3:
			packet.header  = (type << 6) | 0x0008;
			packet.data    = data << 24;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			packet.header  = (type << 6) | 0x0003;
			packet.address = (addr & ~3) + 4;
			packet.data    = (data >> 8) & 0xffff;
			ret |= sisusb_send_packet(sisusb, 10, &packet);
	}

	return ret;
}

static int sisusb_write_memio_long(struct sisusb_usb_data *sisusb, int type,
							u32 addr, u32 data)
{
	struct sisusb_packet packet;
	int ret = 0;

	packet.address = addr & ~3;

	switch (addr & 3) {
		case 0:
			packet.header  = (type << 6) | 0x000f;
			packet.data    = data;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 1:
			packet.header  = (type << 6) | 0x000e;
			packet.data    = data << 8;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			packet.header  = (type << 6) | 0x0001;
			packet.address = (addr & ~3) + 4;
			packet.data    = data >> 24;
			ret |= sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 2:
			packet.header  = (type << 6) | 0x000c;
			packet.data    = data << 16;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			packet.header  = (type << 6) | 0x0003;
			packet.address = (addr & ~3) + 4;
			packet.data    = data >> 16;
			ret |= sisusb_send_packet(sisusb, 10, &packet);
			break;
		case 3:
			packet.header  = (type << 6) | 0x0008;
			packet.data    = data << 24;
			ret = sisusb_send_packet(sisusb, 10, &packet);
			packet.header  = (type << 6) | 0x0007;
			packet.address = (addr & ~3) + 4;
			packet.data    = data >> 8;
			ret |= sisusb_send_packet(sisusb, 10, &packet);
	}

	return ret;
}

/* The xxx_bulk routines copy a buffer of variable size. They treat the
 * buffer as chars, therefore lsb/msb has to be corrected if using the
 * byte/word/long/etc routines for speed-up
 *
 * If data is from userland, set "userbuffer" (and clear "kernbuffer"),
 * if data is in kernel space, set "kernbuffer" (and clear "userbuffer");
 * if neither "kernbuffer" nor "userbuffer" are given, it is assumed
 * that the data already is in the transfer buffer "sisusb->obuf[index]".
 */

static int sisusb_write_mem_bulk(struct sisusb_usb_data *sisusb, u32 addr,
				char *kernbuffer, int length,
				const char __user *userbuffer, int index,
				ssize_t *bytes_written)
{
	struct sisusb_packet packet;
	int  ret = 0;
	static int msgcount = 0;
	u8   swap8, fromkern = kernbuffer ? 1 : 0;
	u16  swap16;
	u32  swap32, flag = (length >> 28) & 1;
	char buf[4];

	/* if neither kernbuffer not userbuffer are given, assume
	 * data in obuf
	 */
	if (!fromkern && !userbuffer)
		kernbuffer = sisusb->obuf[index];

	(*bytes_written = 0);

	length &= 0x00ffffff;

	while (length) {

	    switch (length) {

		case 1:
			if (userbuffer) {
				if (get_user(swap8, (u8 __user *)userbuffer))
					return -EFAULT;
			} else
				swap8 = kernbuffer[0];

			ret = sisusb_write_memio_byte(sisusb,
							SISUSB_TYPE_MEM,
							addr, swap8);

			if (!ret)
				(*bytes_written)++;

			return ret;

		case 2:
			if (userbuffer) {
				if (get_user(swap16, (u16 __user *)userbuffer))
					return -EFAULT;
			} else
				swap16 = *((u16 *)kernbuffer);

			ret = sisusb_write_memio_word(sisusb,
							SISUSB_TYPE_MEM,
							addr,
							swap16);

			if (!ret)
				(*bytes_written) += 2;

			return ret;

		case 3:
			if (userbuffer) {
				if (copy_from_user(&buf, userbuffer, 3))
					return -EFAULT;
#ifdef __BIG_ENDIAN
				swap32 = (buf[0] << 16) |
					 (buf[1] <<  8) |
					 buf[2];
#else
				swap32 = (buf[2] << 16) |
					 (buf[1] <<  8) |
					 buf[0];
#endif
			} else
#ifdef __BIG_ENDIAN
				swap32 = (kernbuffer[0] << 16) |
					 (kernbuffer[1] <<  8) |
					 kernbuffer[2];
#else
				swap32 = (kernbuffer[2] << 16) |
					 (kernbuffer[1] <<  8) |
					 kernbuffer[0];
#endif

			ret = sisusb_write_memio_24bit(sisusb,
							SISUSB_TYPE_MEM,
							addr,
							swap32);

			if (!ret)
				(*bytes_written) += 3;

			return ret;

		case 4:
			if (userbuffer) {
				if (get_user(swap32, (u32 __user *)userbuffer))
					return -EFAULT;
			} else
				swap32 = *((u32 *)kernbuffer);

			ret = sisusb_write_memio_long(sisusb,
							SISUSB_TYPE_MEM,
							addr,
							swap32);
			if (!ret)
				(*bytes_written) += 4;

			return ret;

		default:
			if ((length & ~3) > 0x10000) {

			   packet.header  = 0x001f;
			   packet.address = 0x000001d4;
			   packet.data    = addr;
			   ret = sisusb_send_bridge_packet(sisusb, 10,
								&packet, 0);
			   packet.header  = 0x001f;
			   packet.address = 0x000001d0;
			   packet.data    = (length & ~3);
			   ret |= sisusb_send_bridge_packet(sisusb, 10,
								&packet, 0);
			   packet.header  = 0x001f;
			   packet.address = 0x000001c0;
			   packet.data    = flag | 0x16;
			   ret |= sisusb_send_bridge_packet(sisusb, 10,
								&packet, 0);
			   if (userbuffer) {
				ret |= sisusb_send_bulk_msg(sisusb,
							SISUSB_EP_GFX_LBULK_OUT,
							(length & ~3),
							NULL, userbuffer, 0,
							bytes_written, 0, 1);
				userbuffer += (*bytes_written);
			   } else if (fromkern) {
				ret |= sisusb_send_bulk_msg(sisusb,
							SISUSB_EP_GFX_LBULK_OUT,
							(length & ~3),
							kernbuffer, NULL, 0,
							bytes_written, 0, 1);
				kernbuffer += (*bytes_written);
			   } else {
			ret |= sisusb_send_bulk_msg(sisusb,
							SISUSB_EP_GFX_LBULK_OUT,
							(length & ~3),
							NULL, NULL, index,
							bytes_written, 0, 1);
				kernbuffer += ((*bytes_written) &
						(sisusb->obufsize-1));
			   }

			} else {

			   packet.header  = 0x001f;
			   packet.address = 0x00000194;
			   packet.data    = addr;
			   ret = sisusb_send_bridge_packet(sisusb, 10,
			   					&packet, 0);
			   packet.header  = 0x001f;
			   packet.address = 0x00000190;
			   packet.data    = (length & ~3);
			   ret |= sisusb_send_bridge_packet(sisusb, 10,
			   					&packet, 0);
			   if (sisusb->flagb0 != 0x16) {
				packet.header  = 0x001f;
				packet.address = 0x00000180;
				packet.data    = flag | 0x16;
				ret |= sisusb_send_bridge_packet(sisusb, 10,
								&packet, 0);
				sisusb->flagb0 = 0x16;
			   }
			   if (userbuffer) {
				ret |= sisusb_send_bulk_msg(sisusb,
							SISUSB_EP_GFX_BULK_OUT,
							(length & ~3),
							NULL, userbuffer, 0,
							bytes_written, 0, 1);
				userbuffer += (*bytes_written);
			   } else if (fromkern) {
				ret |= sisusb_send_bulk_msg(sisusb,
							SISUSB_EP_GFX_BULK_OUT,
							(length & ~3),
							kernbuffer, NULL, 0,
							bytes_written, 0, 1);
				kernbuffer += (*bytes_written);
			   } else {
				ret |= sisusb_send_bulk_msg(sisusb,
							SISUSB_EP_GFX_BULK_OUT,
							(length & ~3),
							NULL, NULL, index,
							bytes_written, 0, 1);
				kernbuffer += ((*bytes_written) &
						(sisusb->obufsize-1));
			   }
			}
			if (ret) {
				msgcount++;
				if (msgcount < 500)
					printk(KERN_ERR
						"sisusbvga[%d]: Wrote %zd of "
						"%d bytes, error %d\n",
						sisusb->minor, *bytes_written,
						length, ret);
				else if (msgcount == 500)
					printk(KERN_ERR
						"sisusbvga[%d]: Too many errors"
						", logging stopped\n",
						sisusb->minor);
			}
			addr += (*bytes_written);
			length -= (*bytes_written);
	    }

	    if (ret)
	    	break;

	}

	return ret ? -EIO : 0;
}

/* Remember: Read data in packet is in machine-endianess! So for
 * byte, word, 24bit, long no endian correction is necessary.
 */

static int sisusb_read_memio_byte(struct sisusb_usb_data *sisusb, int type,
							u32 addr, u8 *data)
{
	struct sisusb_packet packet;
	int ret;

	CLEARPACKET(&packet);
	packet.header  = (1 << (addr & 3)) | (type << 6);
	packet.address = addr & ~3;
	ret = sisusb_send_packet(sisusb, 6, &packet);
	*data = (u8)(packet.data >> ((addr & 3) << 3));
	return ret;
}

static int sisusb_read_memio_word(struct sisusb_usb_data *sisusb, int type,
							u32 addr, u16 *data)
{
	struct sisusb_packet packet;
	int ret = 0;

	CLEARPACKET(&packet);

	packet.address = addr & ~3;

	switch (addr & 3) {
		case 0:
			packet.header = (type << 6) | 0x0003;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = (u16)(packet.data);
			break;
		case 1:
			packet.header = (type << 6) | 0x0006;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = (u16)(packet.data >> 8);
			break;
		case 2:
			packet.header = (type << 6) | 0x000c;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = (u16)(packet.data >> 16);
			break;
		case 3:
			packet.header = (type << 6) | 0x0008;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = (u16)(packet.data >> 24);
			packet.header = (type << 6) | 0x0001;
			packet.address = (addr & ~3) + 4;
			ret |= sisusb_send_packet(sisusb, 6, &packet);
			*data |= (u16)(packet.data << 8);
	}

	return ret;
}

static int sisusb_read_memio_24bit(struct sisusb_usb_data *sisusb, int type,
							u32 addr, u32 *data)
{
	struct sisusb_packet packet;
	int ret = 0;

	packet.address = addr & ~3;

	switch (addr & 3) {
		case 0:
			packet.header  = (type << 6) | 0x0007;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = packet.data & 0x00ffffff;
			break;
		case 1:
			packet.header  = (type << 6) | 0x000e;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = packet.data >> 8;
			break;
		case 2:
			packet.header  = (type << 6) | 0x000c;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = packet.data >> 16;
			packet.header  = (type << 6) | 0x0001;
			packet.address = (addr & ~3) + 4;
			ret |= sisusb_send_packet(sisusb, 6, &packet);
			*data |= ((packet.data & 0xff) << 16);
			break;
		case 3:
			packet.header  = (type << 6) | 0x0008;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = packet.data >> 24;
			packet.header  = (type << 6) | 0x0003;
			packet.address = (addr & ~3) + 4;
			ret |= sisusb_send_packet(sisusb, 6, &packet);
			*data |= ((packet.data & 0xffff) << 8);
	}

	return ret;
}

static int sisusb_read_memio_long(struct sisusb_usb_data *sisusb, int type,
							u32 addr, u32 *data)
{
	struct sisusb_packet packet;
	int ret = 0;

	packet.address = addr & ~3;

	switch (addr & 3) {
		case 0:
			packet.header  = (type << 6) | 0x000f;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = packet.data;
			break;
		case 1:
			packet.header  = (type << 6) | 0x000e;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = packet.data >> 8;
			packet.header  = (type << 6) | 0x0001;
			packet.address = (addr & ~3) + 4;
			ret |= sisusb_send_packet(sisusb, 6, &packet);
			*data |= (packet.data << 24);
			break;
		case 2:
			packet.header  = (type << 6) | 0x000c;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = packet.data >> 16;
			packet.header  = (type << 6) | 0x0003;
			packet.address = (addr & ~3) + 4;
			ret |= sisusb_send_packet(sisusb, 6, &packet);
			*data |= (packet.data << 16);
			break;
		case 3:
			packet.header  = (type << 6) | 0x0008;
			ret = sisusb_send_packet(sisusb, 6, &packet);
			*data = packet.data >> 24;
			packet.header  = (type << 6) | 0x0007;
			packet.address = (addr & ~3) + 4;
			ret |= sisusb_send_packet(sisusb, 6, &packet);
			*data |= (packet.data << 8);
	}

	return ret;
}

static int sisusb_read_mem_bulk(struct sisusb_usb_data *sisusb, u32 addr,
				char *kernbuffer, int length,
				char __user *userbuffer, ssize_t *bytes_read)
{
	int ret = 0;
	char buf[4];
	u16 swap16;
	u32 swap32;

	(*bytes_read = 0);

	length &= 0x00ffffff;

	while (length) {

	    switch (length) {

		case 1:

			ret |= sisusb_read_memio_byte(sisusb, SISUSB_TYPE_MEM,
								addr, &buf[0]);
			if (!ret) {
				(*bytes_read)++;
				if (userbuffer) {
					if (put_user(buf[0],
						(u8 __user *)userbuffer)) {
						return -EFAULT;
					}
				} else {
					kernbuffer[0] = buf[0];
				}
			}
			return ret;

		case 2:
			ret |= sisusb_read_memio_word(sisusb, SISUSB_TYPE_MEM,
								addr, &swap16);
			if (!ret) {
				(*bytes_read) += 2;
				if (userbuffer) {
					if (put_user(swap16,
						(u16 __user *)userbuffer))
						return -EFAULT;
				} else {
					*((u16 *)kernbuffer) = swap16;
				}
			}
			return ret;

		case 3:
			ret |= sisusb_read_memio_24bit(sisusb, SISUSB_TYPE_MEM,
								addr, &swap32);
			if (!ret) {
				(*bytes_read) += 3;
#ifdef __BIG_ENDIAN
				buf[0] = (swap32 >> 16) & 0xff;
				buf[1] = (swap32 >> 8) & 0xff;
				buf[2] = swap32 & 0xff;
#else
				buf[2] = (swap32 >> 16) & 0xff;
				buf[1] = (swap32 >> 8) & 0xff;
				buf[0] = swap32 & 0xff;
#endif
				if (userbuffer) {
					if (copy_to_user(userbuffer, &buf[0], 3))
						return -EFAULT;
				} else {
					kernbuffer[0] = buf[0];
					kernbuffer[1] = buf[1];
					kernbuffer[2] = buf[2];
				}
			}
			return ret;

		default:
			ret |= sisusb_read_memio_long(sisusb, SISUSB_TYPE_MEM,
								addr, &swap32);
			if (!ret) {
				(*bytes_read) += 4;
				if (userbuffer) {
					if (put_user(swap32,
						(u32 __user *)userbuffer))
						return -EFAULT;

					userbuffer += 4;
				} else {
					*((u32 *)kernbuffer) = swap32;
					kernbuffer += 4;
				}
				addr += 4;
				length -= 4;
			}
#if 0		/* That does not work, as EP 2 is an OUT EP! */
		default:
			CLEARPACKET(&packet);
			packet.header  = 0x001f;
			packet.address = 0x000001a0;
			packet.data    = 0x00000006;
			ret |= sisusb_send_bridge_packet(sisusb, 10,
								&packet, 0);
			packet.header  = 0x001f;
			packet.address = 0x000001b0;
			packet.data    = (length & ~3) | 0x40000000;
			ret |= sisusb_send_bridge_packet(sisusb, 10,
								&packet, 0);
			packet.header  = 0x001f;
			packet.address = 0x000001b4;
			packet.data    = addr;
			ret |= sisusb_send_bridge_packet(sisusb, 10,
								&packet, 0);
			packet.header  = 0x001f;
			packet.address = 0x000001a4;
			packet.data    = 0x00000001;
			ret |= sisusb_send_bridge_packet(sisusb, 10,
								&packet, 0);
			if (userbuffer) {
				ret |= sisusb_recv_bulk_msg(sisusb,
							SISUSB_EP_GFX_BULK_IN,
							(length & ~3),
							NULL, userbuffer,
							bytes_read, 0);
				if (!ret) userbuffer += (*bytes_read);
			} else {
				ret |= sisusb_recv_bulk_msg(sisusb,
							SISUSB_EP_GFX_BULK_IN,
							(length & ~3),
							kernbuffer, NULL,
							bytes_read, 0);
				if (!ret) kernbuffer += (*bytes_read);
			}
			addr += (*bytes_read);
			length -= (*bytes_read);
#endif
	    }

	    if (ret)
	    	break;
	}

	return ret;
}

/* High level: Gfx (indexed) register access */

#ifdef INCL_SISUSB_CON
int
sisusb_setreg(struct sisusb_usb_data *sisusb, int port, u8 data)
{
	return sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, port, data);
}

int
sisusb_getreg(struct sisusb_usb_data *sisusb, int port, u8 *data)
{
	return sisusb_read_memio_byte(sisusb, SISUSB_TYPE_IO, port, data);
}
#endif

int
sisusb_setidxreg(struct sisusb_usb_data *sisusb, int port, u8 index, u8 data)
{
	int ret;
	ret = sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, port, index);
	ret |= sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, port + 1, data);
	return ret;
}

int
sisusb_getidxreg(struct sisusb_usb_data *sisusb, int port, u8 index, u8 *data)
{
	int ret;
	ret = sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, port, index);
	ret |= sisusb_read_memio_byte(sisusb, SISUSB_TYPE_IO, port + 1, data);
	return ret;
}

int
sisusb_setidxregandor(struct sisusb_usb_data *sisusb, int port, u8 idx,
							u8 myand, u8 myor)
{
	int ret;
	u8 tmp;

	ret = sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, port, idx);
	ret |= sisusb_read_memio_byte(sisusb, SISUSB_TYPE_IO, port + 1, &tmp);
	tmp &= myand;
	tmp |= myor;
	ret |= sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, port + 1, tmp);
	return ret;
}

static int
sisusb_setidxregmask(struct sisusb_usb_data *sisusb, int port, u8 idx,
							u8 data, u8 mask)
{
	int ret;
	u8 tmp;
	ret = sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, port, idx);
	ret |= sisusb_read_memio_byte(sisusb, SISUSB_TYPE_IO, port + 1, &tmp);
	tmp &= ~(mask);
	tmp |= (data & mask);
	ret |= sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, port + 1, tmp);
	return ret;
}

int
sisusb_setidxregor(struct sisusb_usb_data *sisusb, int port, u8 index, u8 myor)
{
	return(sisusb_setidxregandor(sisusb, port, index, 0xff, myor));
}

int
sisusb_setidxregand(struct sisusb_usb_data *sisusb, int port, u8 idx, u8 myand)
{
	return(sisusb_setidxregandor(sisusb, port, idx, myand, 0x00));
}

/* Write/read video ram */

#ifdef INCL_SISUSB_CON
int
sisusb_writeb(struct sisusb_usb_data *sisusb, u32 adr, u8 data)
{
	return(sisusb_write_memio_byte(sisusb, SISUSB_TYPE_MEM, adr, data));
}

int
sisusb_readb(struct sisusb_usb_data *sisusb, u32 adr, u8 *data)
{
	return(sisusb_read_memio_byte(sisusb, SISUSB_TYPE_MEM, adr, data));
}

#if 0

int
sisusb_writew(struct sisusb_usb_data *sisusb, u32 adr, u16 data)
{
	return(sisusb_write_memio_word(sisusb, SISUSB_TYPE_MEM, adr, data));
}

int
sisusb_readw(struct sisusb_usb_data *sisusb, u32 adr, u16 *data)
{
	return(sisusb_read_memio_word(sisusb, SISUSB_TYPE_MEM, adr, data));
}

#endif  /*  0  */

int
sisusb_copy_memory(struct sisusb_usb_data *sisusb, char *src,
			u32 dest, int length, size_t *bytes_written)
{
	return(sisusb_write_mem_bulk(sisusb, dest, src, length, NULL, 0, bytes_written));
}

#ifdef SISUSBENDIANTEST
int
sisusb_read_memory(struct sisusb_usb_data *sisusb, char *dest,
			u32 src, int length, size_t *bytes_written)
{
	return(sisusb_read_mem_bulk(sisusb, src, dest, length, NULL, bytes_written));
}
#endif
#endif

#ifdef SISUSBENDIANTEST
static void
sisusb_testreadwrite(struct sisusb_usb_data *sisusb)
{
    static char srcbuffer[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };
    char destbuffer[10];
    size_t dummy;
    int i,j;

    sisusb_copy_memory(sisusb, srcbuffer, sisusb->vrambase, 7, &dummy);

    for(i = 1; i <= 7; i++) {
        printk(KERN_DEBUG "sisusb: rwtest %d bytes\n", i);
	sisusb_read_memory(sisusb, destbuffer, sisusb->vrambase, i, &dummy);
	for(j = 0; j < i; j++) {
	     printk(KERN_DEBUG "sisusb: rwtest read[%d] = %x\n", j, destbuffer[j]);
	}
    }
}
#endif

/* access pci config registers (reg numbers 0, 4, 8, etc) */

static int
sisusb_write_pci_config(struct sisusb_usb_data *sisusb, int regnum, u32 data)
{
	struct sisusb_packet packet;
	int ret;

	packet.header = 0x008f;
	packet.address = regnum | 0x10000;
	packet.data = data;
	ret = sisusb_send_packet(sisusb, 10, &packet);
	return ret;
}

static int
sisusb_read_pci_config(struct sisusb_usb_data *sisusb, int regnum, u32 *data)
{
	struct sisusb_packet packet;
	int ret;

	packet.header = 0x008f;
	packet.address = (u32)regnum | 0x10000;
	ret = sisusb_send_packet(sisusb, 6, &packet);
	*data = packet.data;
	return ret;
}

/* Clear video RAM */

static int
sisusb_clear_vram(struct sisusb_usb_data *sisusb, u32 address, int length)
{
	int ret, i;
	ssize_t j;

	if (address < sisusb->vrambase)
		return 1;

	if (address >= sisusb->vrambase + sisusb->vramsize)
		return 1;

	if (address + length > sisusb->vrambase + sisusb->vramsize)
		length = sisusb->vrambase + sisusb->vramsize - address;

	if (length <= 0)
		return 0;

	/* allocate free buffer/urb and clear the buffer */
	if ((i = sisusb_alloc_outbuf(sisusb)) < 0)
		return -EBUSY;

	memset(sisusb->obuf[i], 0, sisusb->obufsize);

	/* We can write a length > buffer size here. The buffer
	 * data will simply be re-used (like a ring-buffer).
	 */
	ret = sisusb_write_mem_bulk(sisusb, address, NULL, length, NULL, i, &j);

	/* Free the buffer/urb */
	sisusb_free_outbuf(sisusb, i);

	return ret;
}

/* Initialize the graphics core (return 0 on success)
 * This resets the graphics hardware and puts it into
 * a defined mode (640x480@60Hz)
 */

#define GETREG(r,d)     sisusb_read_memio_byte(sisusb, SISUSB_TYPE_IO, r, d)
#define SETREG(r,d)	sisusb_write_memio_byte(sisusb, SISUSB_TYPE_IO, r, d)
#define SETIREG(r,i,d)	sisusb_setidxreg(sisusb, r, i, d)
#define GETIREG(r,i,d)  sisusb_getidxreg(sisusb, r, i, d)
#define SETIREGOR(r,i,o)	sisusb_setidxregor(sisusb, r, i, o)
#define SETIREGAND(r,i,a)	sisusb_setidxregand(sisusb, r, i, a)
#define SETIREGANDOR(r,i,a,o)	sisusb_setidxregandor(sisusb, r, i, a, o)
#define READL(a,d)	sisusb_read_memio_long(sisusb, SISUSB_TYPE_MEM, a, d)
#define WRITEL(a,d) 	sisusb_write_memio_long(sisusb, SISUSB_TYPE_MEM, a, d)
#define READB(a,d)	sisusb_read_memio_byte(sisusb, SISUSB_TYPE_MEM, a, d)
#define WRITEB(a,d) 	sisusb_write_memio_byte(sisusb, SISUSB_TYPE_MEM, a, d)

static int
sisusb_triggersr16(struct sisusb_usb_data *sisusb, u8 ramtype)
{
	int ret;
	u8 tmp8;

	ret = GETIREG(SISSR, 0x16, &tmp8);
	if (ramtype <= 1) {
		tmp8 &= 0x3f;
		ret |= SETIREG(SISSR, 0x16, tmp8);
		tmp8 |= 0x80;
		ret |= SETIREG(SISSR, 0x16, tmp8);
	} else {
		tmp8 |= 0xc0;
		ret |= SETIREG(SISSR, 0x16, tmp8);
		tmp8 &= 0x0f;
		ret |= SETIREG(SISSR, 0x16, tmp8);
		tmp8 |= 0x80;
		ret |= SETIREG(SISSR, 0x16, tmp8);
		tmp8 &= 0x0f;
		ret |= SETIREG(SISSR, 0x16, tmp8);
		tmp8 |= 0xd0;
		ret |= SETIREG(SISSR, 0x16, tmp8);
		tmp8 &= 0x0f;
		ret |= SETIREG(SISSR, 0x16, tmp8);
		tmp8 |= 0xa0;
		ret |= SETIREG(SISSR, 0x16, tmp8);
	}
	return ret;
}

static int
sisusb_getbuswidth(struct sisusb_usb_data *sisusb, int *bw, int *chab)
{
	int ret;
	u8  ramtype, done = 0;
	u32 t0, t1, t2, t3;
	u32 ramptr = SISUSB_PCI_MEMBASE;

	ret = GETIREG(SISSR, 0x3a, &ramtype);
	ramtype &= 3;

	ret |= SETIREG(SISSR, 0x13, 0x00);

	if (ramtype <= 1) {
		ret |= SETIREG(SISSR, 0x14, 0x12);
		ret |= SETIREGAND(SISSR, 0x15, 0xef);
	} else {
		ret |= SETIREG(SISSR, 0x14, 0x02);
	}

	ret |= sisusb_triggersr16(sisusb, ramtype);
	ret |= WRITEL(ramptr +  0, 0x01234567);
	ret |= WRITEL(ramptr +  4, 0x456789ab);
	ret |= WRITEL(ramptr +  8, 0x89abcdef);
	ret |= WRITEL(ramptr + 12, 0xcdef0123);
	ret |= WRITEL(ramptr + 16, 0x55555555);
	ret |= WRITEL(ramptr + 20, 0x55555555);
	ret |= WRITEL(ramptr + 24, 0xffffffff);
	ret |= WRITEL(ramptr + 28, 0xffffffff);
	ret |= READL(ramptr +  0, &t0);
	ret |= READL(ramptr +  4, &t1);
	ret |= READL(ramptr +  8, &t2);
	ret |= READL(ramptr + 12, &t3);

	if (ramtype <= 1) {

		*chab = 0; *bw = 64;

		if ((t3 != 0xcdef0123) || (t2 != 0x89abcdef)) {
			if ((t1 == 0x456789ab) && (t0 == 0x01234567)) {
				*chab = 0; *bw = 64;
				ret |= SETIREGAND(SISSR, 0x14, 0xfd);
			}
		}
		if ((t1 != 0x456789ab) || (t0 != 0x01234567)) {
			*chab = 1; *bw = 64;
			ret |= SETIREGANDOR(SISSR, 0x14, 0xfc,0x01);

			ret |= sisusb_triggersr16(sisusb, ramtype);
			ret |= WRITEL(ramptr +  0, 0x89abcdef);
			ret |= WRITEL(ramptr +  4, 0xcdef0123);
			ret |= WRITEL(ramptr +  8, 0x55555555);
			ret |= WRITEL(ramptr + 12, 0x55555555);
			ret |= WRITEL(ramptr + 16, 0xaaaaaaaa);
			ret |= WRITEL(ramptr + 20, 0xaaaaaaaa);
			ret |= READL(ramptr +  4, &t1);

			if (t1 != 0xcdef0123) {
				*bw = 32;
				ret |= SETIREGOR(SISSR, 0x15, 0x10);
			}
		}

	} else {

		*chab = 0; *bw = 64;	/* default: cha, bw = 64 */

		done = 0;

		if (t1 == 0x456789ab) {
			if (t0 == 0x01234567) {
				*chab = 0; *bw = 64;
				done = 1;
			}
		} else {
			if (t0 == 0x01234567) {
				*chab = 0; *bw = 32;
				ret |= SETIREG(SISSR, 0x14, 0x00);
				done = 1;
			}
		}

		if (!done) {
			ret |= SETIREG(SISSR, 0x14, 0x03);
			ret |= sisusb_triggersr16(sisusb, ramtype);

			ret |= WRITEL(ramptr +  0, 0x01234567);
			ret |= WRITEL(ramptr +  4, 0x456789ab);
			ret |= WRITEL(ramptr +  8, 0x89abcdef);
			ret |= WRITEL(ramptr + 12, 0xcdef0123);
			ret |= WRITEL(ramptr + 16, 0x55555555);
			ret |= WRITEL(ramptr + 20, 0x55555555);
			ret |= WRITEL(ramptr + 24, 0xffffffff);
			ret |= WRITEL(ramptr + 28, 0xffffffff);
			ret |= READL(ramptr +  0, &t0);
			ret |= READL(ramptr +  4, &t1);

			if (t1 == 0x456789ab) {
				if (t0 == 0x01234567) {
					*chab = 1; *bw = 64;
					return ret;
				} /* else error */
			} else {
				if (t0 == 0x01234567) {
					*chab = 1; *bw = 32;
					ret |= SETIREG(SISSR, 0x14, 0x01);
				} /* else error */
			}
		}
	}
	return ret;
}

static int
sisusb_verify_mclk(struct sisusb_usb_data *sisusb)
{
	int ret = 0;
	u32 ramptr = SISUSB_PCI_MEMBASE;
	u8 tmp1, tmp2, i, j;

	ret |= WRITEB(ramptr, 0xaa);
	ret |= WRITEB(ramptr + 16, 0x55);
	ret |= READB(ramptr, &tmp1);
	ret |= READB(ramptr + 16, &tmp2);
	if ((tmp1 != 0xaa) || (tmp2 != 0x55)) {
		for (i = 0, j = 16; i < 2; i++, j += 16) {
			ret |= GETIREG(SISSR, 0x21, &tmp1);
			ret |= SETIREGAND(SISSR, 0x21, (tmp1 & 0xfb));
			ret |= SETIREGOR(SISSR, 0x3c, 0x01);  /* not on 330 */
			ret |= SETIREGAND(SISSR, 0x3c, 0xfe); /* not on 330 */
			ret |= SETIREG(SISSR, 0x21, tmp1);
			ret |= WRITEB(ramptr + 16 + j, j);
			ret |= READB(ramptr + 16 + j, &tmp1);
			if (tmp1 == j) {
				ret |= WRITEB(ramptr + j, j);
				break;
			}
		}
	}
	return ret;
}

static int
sisusb_set_rank(struct sisusb_usb_data *sisusb, int *iret, int index,
			u8 rankno, u8 chab, const u8 dramtype[][5],
			int bw)
{
	int ret = 0, ranksize;
	u8 tmp;

	*iret = 0;

	if ((rankno == 2) && (dramtype[index][0] == 2))
		return ret;

	ranksize = dramtype[index][3] / 2 * bw / 32;

	if ((ranksize * rankno) > 128)
		return ret;

	tmp = 0;
	while ((ranksize >>= 1) > 0) tmp += 0x10;
	tmp |= ((rankno - 1) << 2);
	tmp |= ((bw / 64) & 0x02);
	tmp |= (chab & 0x01);

	ret = SETIREG(SISSR, 0x14, tmp);
	ret |= sisusb_triggersr16(sisusb, 0); /* sic! */

	*iret = 1;

	return ret;
}

static int
sisusb_check_rbc(struct sisusb_usb_data *sisusb, int *iret, u32 inc, int testn)
{
	int ret = 0, i;
	u32 j, tmp;

	*iret = 0;

	for (i = 0, j = 0; i < testn; i++) {
		ret |= WRITEL(sisusb->vrambase + j, j);
		j += inc;
	}

	for (i = 0, j = 0; i < testn; i++) {
		ret |= READL(sisusb->vrambase + j, &tmp);
		if (tmp != j) return ret;
		j += inc;
	}

	*iret = 1;
	return ret;
}

static int
sisusb_check_ranks(struct sisusb_usb_data *sisusb, int *iret, int rankno,
					int idx, int bw, const u8 rtype[][5])
{
	int ret = 0, i, i2ret;
	u32 inc;

	*iret = 0;

	for (i = rankno; i >= 1; i--) {
		inc = 1 << (rtype[idx][2] +
			    rtype[idx][1] +
			    rtype[idx][0] +
			    bw / 64 + i);
		ret |= sisusb_check_rbc(sisusb, &i2ret, inc, 2);
		if (!i2ret)
			return ret;
	}

	inc = 1 << (rtype[idx][2] + bw / 64 + 2);
	ret |= sisusb_check_rbc(sisusb, &i2ret, inc, 4);
	if (!i2ret)
		return ret;

	inc = 1 << (10 + bw / 64);
	ret |= sisusb_check_rbc(sisusb, &i2ret, inc, 2);
	if (!i2ret)
		return ret;

	*iret = 1;
	return ret;
}

static int
sisusb_get_sdram_size(struct sisusb_usb_data *sisusb, int *iret, int bw,
								int chab)
{
	int ret = 0, i2ret = 0, i, j;
	static const u8 sdramtype[13][5] = {
		{ 2, 12, 9, 64, 0x35 },
		{ 1, 13, 9, 64, 0x44 },
		{ 2, 12, 8, 32, 0x31 },
		{ 2, 11, 9, 32, 0x25 },
		{ 1, 12, 9, 32, 0x34 },
		{ 1, 13, 8, 32, 0x40 },
		{ 2, 11, 8, 16, 0x21 },
		{ 1, 12, 8, 16, 0x30 },
		{ 1, 11, 9, 16, 0x24 },
		{ 1, 11, 8,  8, 0x20 },
		{ 2,  9, 8,  4, 0x01 },
		{ 1, 10, 8,  4, 0x10 },
		{ 1,  9, 8,  2, 0x00 }
	};

	*iret = 1; /* error */

	for (i = 0; i < 13; i++) {
		ret |= SETIREGANDOR(SISSR, 0x13, 0x80, sdramtype[i][4]);
		for (j = 2; j > 0; j--) {
			ret |= sisusb_set_rank(sisusb, &i2ret, i, j,
						chab, sdramtype, bw);
			if (!i2ret)
				continue;

			ret |= sisusb_check_ranks(sisusb, &i2ret, j, i,
						bw, sdramtype);
			if (i2ret) {
				*iret = 0;	/* ram size found */
				return ret;
			}
		}
	}

	return ret;
}

static int
sisusb_setup_screen(struct sisusb_usb_data *sisusb, int clrall, int drwfr)
{
	int ret = 0;
	u32 address;
	int i, length, modex, modey, bpp;

	modex = 640; modey = 480; bpp = 2;

	address = sisusb->vrambase;	/* Clear video ram */

	if (clrall)
		length = sisusb->vramsize;
	else
		length = modex * bpp * modey;

	ret = sisusb_clear_vram(sisusb, address, length);

	if (!ret && drwfr) {
		for (i = 0; i < modex; i++) {
			address = sisusb->vrambase + (i * bpp);
			ret |= sisusb_write_memio_word(sisusb, SISUSB_TYPE_MEM,
							address, 0xf100);
			address += (modex * (modey-1) * bpp);
			ret |= sisusb_write_memio_word(sisusb, SISUSB_TYPE_MEM,
							address, 0xf100);
		}
		for (i = 0; i < modey; i++) {
			address = sisusb->vrambase + ((i * modex) * bpp);
			ret |= sisusb_write_memio_word(sisusb, SISUSB_TYPE_MEM,
							address, 0xf100);
			address += ((modex - 1) * bpp);
			ret |= sisusb_write_memio_word(sisusb, SISUSB_TYPE_MEM,
							address, 0xf100);
		}
	}

	return ret;
}

static int
sisusb_set_default_mode(struct sisusb_usb_data *sisusb, int touchengines)
{
	int ret = 0, i, j, modex, modey, bpp, du;
	u8 sr31, cr63, tmp8;
	static const char attrdata[] = {
		0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
		0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
		0x01,0x00,0x00,0x00
	};
	static const char crtcrdata[] = {
		0x5f,0x4f,0x50,0x82,0x54,0x80,0x0b,0x3e,
		0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
		0xea,0x8c,0xdf,0x28,0x40,0xe7,0x04,0xa3,
		0xff
	};
	static const char grcdata[] = {
		0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0f,
		0xff
	};
	static const char crtcdata[] = {
		0x5f,0x4f,0x4f,0x83,0x55,0x81,0x0b,0x3e,
		0xe9,0x8b,0xdf,0xe8,0x0c,0x00,0x00,0x05,
		0x00
	};

	modex = 640; modey = 480; bpp = 2;

	GETIREG(SISSR, 0x31, &sr31);
	GETIREG(SISCR, 0x63, &cr63);
	SETIREGOR(SISSR, 0x01, 0x20);
	SETIREG(SISCR, 0x63, cr63 & 0xbf);
	SETIREGOR(SISCR, 0x17, 0x80);
	SETIREGOR(SISSR, 0x1f, 0x04);
	SETIREGAND(SISSR, 0x07, 0xfb);
	SETIREG(SISSR, 0x00, 0x03);	/* seq */
	SETIREG(SISSR, 0x01, 0x21);
	SETIREG(SISSR, 0x02, 0x0f);
	SETIREG(SISSR, 0x03, 0x00);
	SETIREG(SISSR, 0x04, 0x0e);
	SETREG(SISMISCW, 0x23);		/* misc */
	for (i = 0; i <= 0x18; i++) {	/* crtc */
		SETIREG(SISCR, i, crtcrdata[i]);
	}
	for (i = 0; i <= 0x13; i++) {	/* att */
		GETREG(SISINPSTAT, &tmp8);
		SETREG(SISAR, i);
		SETREG(SISAR, attrdata[i]);
	}
	GETREG(SISINPSTAT, &tmp8);
	SETREG(SISAR, 0x14);
	SETREG(SISAR, 0x00);
	GETREG(SISINPSTAT, &tmp8);
	SETREG(SISAR, 0x20);
	GETREG(SISINPSTAT, &tmp8);
	for (i = 0; i <= 0x08; i++) {	/* grc */
		SETIREG(SISGR, i, grcdata[i]);
	}
	SETIREGAND(SISGR, 0x05, 0xbf);
	for (i = 0x0A; i <= 0x0E; i++) {	/* clr ext */
		SETIREG(SISSR, i, 0x00);
	}
	SETIREGAND(SISSR, 0x37, 0xfe);
	SETREG(SISMISCW, 0xef);		/* sync */
	SETIREG(SISCR, 0x11, 0x00);	/* crtc */
	for (j = 0x00, i = 0; i <= 7; i++, j++) {
		SETIREG(SISCR, j, crtcdata[i]);
	}
	for (j = 0x10; i <= 10; i++, j++) {
		SETIREG(SISCR, j, crtcdata[i]);
	}
	for (j = 0x15; i <= 12; i++, j++) {
		SETIREG(SISCR, j, crtcdata[i]);
	}
	for (j = 0x0A; i <= 15; i++, j++) {
		SETIREG(SISSR, j, crtcdata[i]);
	}
	SETIREG(SISSR, 0x0E, (crtcdata[16] & 0xE0));
	SETIREGANDOR(SISCR, 0x09, 0x5f, ((crtcdata[16] & 0x01) << 5));
	SETIREG(SISCR, 0x14, 0x4f);
	du = (modex / 16) * (bpp * 2);	/* offset/pitch */
	if (modex % 16) du += bpp;
	SETIREGANDOR(SISSR, 0x0e, 0xf0, ((du >> 8) & 0x0f));
	SETIREG(SISCR, 0x13, (du & 0xff));
	du <<= 5;
	tmp8 = du >> 8;
	if (du & 0xff) tmp8++;
	SETIREG(SISSR, 0x10, tmp8);
	SETIREG(SISSR, 0x31, 0x00);	/* VCLK */
	SETIREG(SISSR, 0x2b, 0x1b);
	SETIREG(SISSR, 0x2c, 0xe1);
	SETIREG(SISSR, 0x2d, 0x01);
	SETIREGAND(SISSR, 0x3d, 0xfe);	/* FIFO */
	SETIREG(SISSR, 0x08, 0xae);
	SETIREGAND(SISSR, 0x09, 0xf0);
	SETIREG(SISSR, 0x08, 0x34);
	SETIREGOR(SISSR, 0x3d, 0x01);
	SETIREGAND(SISSR, 0x1f, 0x3f);	/* mode regs */
	SETIREGANDOR(SISSR, 0x06, 0xc0, 0x0a);
	SETIREG(SISCR, 0x19, 0x00);
	SETIREGAND(SISCR, 0x1a, 0xfc);
	SETIREGAND(SISSR, 0x0f, 0xb7);
	SETIREGAND(SISSR, 0x31, 0xfb);
	SETIREGANDOR(SISSR, 0x21, 0x1f, 0xa0);
	SETIREGAND(SISSR, 0x32, 0xf3);
	SETIREGANDOR(SISSR, 0x07, 0xf8, 0x03);
	SETIREG(SISCR, 0x52, 0x6c);

	SETIREG(SISCR, 0x0d, 0x00);	/* adjust frame */
	SETIREG(SISCR, 0x0c, 0x00);
	SETIREG(SISSR, 0x0d, 0x00);
	SETIREGAND(SISSR, 0x37, 0xfe);

	SETIREG(SISCR, 0x32, 0x20);
	SETIREGAND(SISSR, 0x01, 0xdf);	/* enable display */
	SETIREG(SISCR, 0x63, (cr63 & 0xbf));
	SETIREG(SISSR, 0x31, (sr31 & 0xfb));

	if (touchengines) {
		SETIREG(SISSR, 0x20, 0xa1);	/* enable engines */
		SETIREGOR(SISSR, 0x1e, 0x5a);

		SETIREG(SISSR, 0x26, 0x01);	/* disable cmdqueue */
		SETIREG(SISSR, 0x27, 0x1f);
		SETIREG(SISSR, 0x26, 0x00);
	}

	SETIREG(SISCR, 0x34, 0x44);  	/* we just set std mode #44 */

	return ret;
}

static int
sisusb_init_gfxcore(struct sisusb_usb_data *sisusb)
{
	int ret = 0, i, j, bw, chab, iret, retry = 3;
	u8 tmp8, ramtype;
	u32 tmp32;
	static const char mclktable[] = {
		0x3b, 0x22, 0x01, 143,
		0x3b, 0x22, 0x01, 143,
		0x3b, 0x22, 0x01, 143,
		0x3b, 0x22, 0x01, 143
	};
	static const char eclktable[] = {
		0x3b, 0x22, 0x01, 143,
		0x3b, 0x22, 0x01, 143,
		0x3b, 0x22, 0x01, 143,
		0x3b, 0x22, 0x01, 143
	};
	static const char ramtypetable1[] = {
		0x00, 0x04, 0x60, 0x60,
		0x0f, 0x0f, 0x1f, 0x1f,
		0xba, 0xba, 0xba, 0xba,
		0xa9, 0xa9, 0xac, 0xac,
		0xa0, 0xa0, 0xa0, 0xa8,
		0x00, 0x00, 0x02, 0x02,
		0x30, 0x30, 0x40, 0x40
	};
	static const char ramtypetable2[] = {
		0x77, 0x77, 0x44, 0x44,
		0x77, 0x77, 0x44, 0x44,
		0x00, 0x00, 0x00, 0x00,
		0x5b, 0x5b, 0xab, 0xab,
		0x00, 0x00, 0xf0, 0xf8
	};

	while (retry--) {

		/* Enable VGA */
		ret = GETREG(SISVGAEN, &tmp8);
		ret |= SETREG(SISVGAEN, (tmp8 | 0x01));

		/* Enable GPU access to VRAM */
		ret |= GETREG(SISMISCR, &tmp8);
		ret |= SETREG(SISMISCW, (tmp8 | 0x01));

		if (ret) continue;

		/* Reset registers */
		ret |= SETIREGAND(SISCR, 0x5b, 0xdf);
		ret |= SETIREG(SISSR, 0x05, 0x86);
		ret |= SETIREGOR(SISSR, 0x20, 0x01);

		ret |= SETREG(SISMISCW, 0x67);

		for (i = 0x06; i <= 0x1f; i++) {
			ret |= SETIREG(SISSR, i, 0x00);
		}
		for (i = 0x21; i <= 0x27; i++) {
			ret |= SETIREG(SISSR, i, 0x00);
		}
		for (i = 0x31; i <= 0x3d; i++) {
			ret |= SETIREG(SISSR, i, 0x00);
		}
		for (i = 0x12; i <= 0x1b; i++) {
			ret |= SETIREG(SISSR, i, 0x00);
		}
		for (i = 0x79; i <= 0x7c; i++) {
			ret |= SETIREG(SISCR, i, 0x00);
		}

		if (ret) continue;

		ret |= SETIREG(SISCR, 0x63, 0x80);

		ret |= GETIREG(SISSR, 0x3a, &ramtype);
		ramtype &= 0x03;

		ret |= SETIREG(SISSR, 0x28, mclktable[ramtype * 4]);
		ret |= SETIREG(SISSR, 0x29, mclktable[(ramtype * 4) + 1]);
		ret |= SETIREG(SISSR, 0x2a, mclktable[(ramtype * 4) + 2]);

		ret |= SETIREG(SISSR, 0x2e, eclktable[ramtype * 4]);
		ret |= SETIREG(SISSR, 0x2f, eclktable[(ramtype * 4) + 1]);
		ret |= SETIREG(SISSR, 0x30, eclktable[(ramtype * 4) + 2]);

		ret |= SETIREG(SISSR, 0x07, 0x18);
		ret |= SETIREG(SISSR, 0x11, 0x0f);

		if (ret) continue;

		for (i = 0x15, j = 0; i <= 0x1b; i++, j++) {
			ret |= SETIREG(SISSR, i, ramtypetable1[(j*4) + ramtype]);
		}
		for (i = 0x40, j = 0; i <= 0x44; i++, j++) {
			ret |= SETIREG(SISCR, i, ramtypetable2[(j*4) + ramtype]);
		}

		ret |= SETIREG(SISCR, 0x49, 0xaa);

		ret |= SETIREG(SISSR, 0x1f, 0x00);
		ret |= SETIREG(SISSR, 0x20, 0xa0);
		ret |= SETIREG(SISSR, 0x23, 0xf6);
		ret |= SETIREG(SISSR, 0x24, 0x0d);
		ret |= SETIREG(SISSR, 0x25, 0x33);

		ret |= SETIREG(SISSR, 0x11, 0x0f);

		ret |= SETIREGOR(SISPART1, 0x2f, 0x01);

		ret |= SETIREGAND(SISCAP, 0x3f, 0xef);

		if (ret) continue;

		ret |= SETIREG(SISPART1, 0x00, 0x00);

		ret |= GETIREG(SISSR, 0x13, &tmp8);
		tmp8 >>= 4;

		ret |= SETIREG(SISPART1, 0x02, 0x00);
		ret |= SETIREG(SISPART1, 0x2e, 0x08);

		ret |= sisusb_read_pci_config(sisusb, 0x50, &tmp32);
		tmp32 &= 0x00f00000;
		tmp8 = (tmp32 == 0x100000) ? 0x33 : 0x03;
		ret |= SETIREG(SISSR, 0x25, tmp8);
		tmp8 = (tmp32 == 0x100000) ? 0xaa : 0x88;
		ret |= SETIREG(SISCR, 0x49, tmp8);

		ret |= SETIREG(SISSR, 0x27, 0x1f);
		ret |= SETIREG(SISSR, 0x31, 0x00);
		ret |= SETIREG(SISSR, 0x32, 0x11);
		ret |= SETIREG(SISSR, 0x33, 0x00);

		if (ret) continue;

		ret |= SETIREG(SISCR, 0x83, 0x00);

		ret |= sisusb_set_default_mode(sisusb, 0);

		ret |= SETIREGAND(SISSR, 0x21, 0xdf);
		ret |= SETIREGOR(SISSR, 0x01, 0x20);
		ret |= SETIREGOR(SISSR, 0x16, 0x0f);

		ret |= sisusb_triggersr16(sisusb, ramtype);

		/* Disable refresh */
		ret |= SETIREGAND(SISSR, 0x17, 0xf8);
		ret |= SETIREGOR(SISSR, 0x19, 0x03);

		ret |= sisusb_getbuswidth(sisusb, &bw, &chab);
		ret |= sisusb_verify_mclk(sisusb);

		if (ramtype <= 1) {
			ret |= sisusb_get_sdram_size(sisusb, &iret, bw, chab);
			if (iret) {
				printk(KERN_ERR "sisusbvga[%d]: RAM size "
					"detection failed, "
					"assuming 8MB video RAM\n",
					sisusb->minor);
				ret |= SETIREG(SISSR,0x14,0x31);
				/* TODO */
			}
		} else {
			printk(KERN_ERR "sisusbvga[%d]: DDR RAM device found, "
					"assuming 8MB video RAM\n",
					sisusb->minor);
			ret |= SETIREG(SISSR,0x14,0x31);
			/* *** TODO *** */
		}

		/* Enable refresh */
		ret |= SETIREG(SISSR, 0x16, ramtypetable1[4 + ramtype]);
		ret |= SETIREG(SISSR, 0x17, ramtypetable1[8 + ramtype]);
		ret |= SETIREG(SISSR, 0x19, ramtypetable1[16 + ramtype]);

		ret |= SETIREGOR(SISSR, 0x21, 0x20);

		ret |= SETIREG(SISSR, 0x22, 0xfb);
		ret |= SETIREG(SISSR, 0x21, 0xa5);

		if (ret == 0)
			break;
	}

	return ret;
}

#undef SETREG
#undef GETREG
#undef SETIREG
#undef GETIREG
#undef SETIREGOR
#undef SETIREGAND
#undef SETIREGANDOR
#undef READL
#undef WRITEL

static void
sisusb_get_ramconfig(struct sisusb_usb_data *sisusb)
{
	u8 tmp8, tmp82, ramtype;
	int bw = 0;
	char *ramtypetext1 = NULL;
	const char *ramtypetext2[] = {	"SDR SDRAM", "SDR SGRAM",
					"DDR SDRAM", "DDR SGRAM" };
	static const int busSDR[4]  = {64, 64, 128, 128};
	static const int busDDR[4]  = {32, 32,  64,  64};
	static const int busDDRA[4] = {64+32, 64+32 , (64+32)*2, (64+32)*2};

	sisusb_getidxreg(sisusb, SISSR, 0x14, &tmp8);
	sisusb_getidxreg(sisusb, SISSR, 0x15, &tmp82);
	sisusb_getidxreg(sisusb, SISSR, 0x3a, &ramtype);
	sisusb->vramsize = (1 << ((tmp8 & 0xf0) >> 4)) * 1024 * 1024;
	ramtype &= 0x03;
	switch ((tmp8 >> 2) & 0x03) {
	case 0: ramtypetext1 = "1 ch/1 r";
		if (tmp82 & 0x10) {
			bw = 32;
		} else {
			bw = busSDR[(tmp8 & 0x03)];
		}
		break;
	case 1: ramtypetext1 = "1 ch/2 r";
		sisusb->vramsize <<= 1;
		bw = busSDR[(tmp8 & 0x03)];
		break;
	case 2: ramtypetext1 = "asymmeric";
		sisusb->vramsize += sisusb->vramsize/2;
		bw = busDDRA[(tmp8 & 0x03)];
		break;
	case 3: ramtypetext1 = "2 channel";
		sisusb->vramsize <<= 1;
		bw = busDDR[(tmp8 & 0x03)];
		break;
	}

	printk(KERN_INFO "sisusbvga[%d]: %dMB %s %s, bus width %d\n",
			sisusb->minor, (sisusb->vramsize >> 20), ramtypetext1,
			ramtypetext2[ramtype], bw);
}

static int
sisusb_do_init_gfxdevice(struct sisusb_usb_data *sisusb)
{
	struct sisusb_packet packet;
	int ret;
	u32 tmp32;

	/* Do some magic */
	packet.header  = 0x001f;
	packet.address = 0x00000324;
	packet.data    = 0x00000004;
	ret = sisusb_send_bridge_packet(sisusb, 10, &packet, 0);

	packet.header  = 0x001f;
	packet.address = 0x00000364;
	packet.data    = 0x00000004;
	ret |= sisusb_send_bridge_packet(sisusb, 10, &packet, 0);

	packet.header  = 0x001f;
	packet.address = 0x00000384;
	packet.data    = 0x00000004;
	ret |= sisusb_send_bridge_packet(sisusb, 10, &packet, 0);

	packet.header  = 0x001f;
	packet.address = 0x00000100;
	packet.data    = 0x00000700;
	ret |= sisusb_send_bridge_packet(sisusb, 10, &packet, 0);

	packet.header  = 0x000f;
	packet.address = 0x00000004;
	ret |= sisusb_send_bridge_packet(sisusb, 6, &packet, 0);
	packet.data |= 0x17;
	ret |= sisusb_send_bridge_packet(sisusb, 10, &packet, 0);

	/* Init BAR 0 (VRAM) */
	ret |= sisusb_read_pci_config(sisusb, 0x10, &tmp32);
	ret |= sisusb_write_pci_config(sisusb, 0x10, 0xfffffff0);
	ret |= sisusb_read_pci_config(sisusb, 0x10, &tmp32);
	tmp32 &= 0x0f;
	tmp32 |= SISUSB_PCI_MEMBASE;
	ret |= sisusb_write_pci_config(sisusb, 0x10, tmp32);

	/* Init BAR 1 (MMIO) */
	ret |= sisusb_read_pci_config(sisusb, 0x14, &tmp32);
	ret |= sisusb_write_pci_config(sisusb, 0x14, 0xfffffff0);
	ret |= sisusb_read_pci_config(sisusb, 0x14, &tmp32);
	tmp32 &= 0x0f;
	tmp32 |= SISUSB_PCI_MMIOBASE;
	ret |= sisusb_write_pci_config(sisusb, 0x14, tmp32);

	/* Init BAR 2 (i/o ports) */
	ret |= sisusb_read_pci_config(sisusb, 0x18, &tmp32);
	ret |= sisusb_write_pci_config(sisusb, 0x18, 0xfffffff0);
	ret |= sisusb_read_pci_config(sisusb, 0x18, &tmp32);
	tmp32 &= 0x0f;
	tmp32 |= SISUSB_PCI_IOPORTBASE;
	ret |= sisusb_write_pci_config(sisusb, 0x18, tmp32);

	/* Enable memory and i/o access */
	ret |= sisusb_read_pci_config(sisusb, 0x04, &tmp32);
	tmp32 |= 0x3;
	ret |= sisusb_write_pci_config(sisusb, 0x04, tmp32);

	if (ret == 0) {
		/* Some further magic */
		packet.header  = 0x001f;
		packet.address = 0x00000050;
		packet.data    = 0x000000ff;
		ret |= sisusb_send_bridge_packet(sisusb, 10, &packet, 0);
	}

	return ret;
}

/* Initialize the graphics device (return 0 on success)
 * This initializes the net2280 as well as the PCI registers
 * of the graphics board.
 */

static int
sisusb_init_gfxdevice(struct sisusb_usb_data *sisusb, int initscreen)
{
	int ret = 0, test = 0;
	u32 tmp32;

	if (sisusb->devinit == 1) {
		/* Read PCI BARs and see if they have been set up */
		ret |= sisusb_read_pci_config(sisusb, 0x10, &tmp32);
		if (ret) return ret;
		if ((tmp32 & 0xfffffff0) == SISUSB_PCI_MEMBASE) test++;

		ret |= sisusb_read_pci_config(sisusb, 0x14, &tmp32);
		if (ret) return ret;
		if ((tmp32 & 0xfffffff0) == SISUSB_PCI_MMIOBASE) test++;

		ret |= sisusb_read_pci_config(sisusb, 0x18, &tmp32);
		if (ret) return ret;
		if ((tmp32 & 0xfffffff0) == SISUSB_PCI_IOPORTBASE) test++;
	}

	/* No? So reset the device */
	if ((sisusb->devinit == 0) || (test != 3)) {

		ret |= sisusb_do_init_gfxdevice(sisusb);

		if (ret == 0)
			sisusb->devinit = 1;

	}

	if (sisusb->devinit) {
		/* Initialize the graphics core */
		if (sisusb_init_gfxcore(sisusb) == 0) {
			sisusb->gfxinit = 1;
			sisusb_get_ramconfig(sisusb);
			ret |= sisusb_set_default_mode(sisusb, 1);
			ret |= sisusb_setup_screen(sisusb, 1, initscreen);
		}
	}

	return ret;
}


#ifdef INCL_SISUSB_CON

/* Set up default text mode:
   - Set text mode (0x03)
   - Upload default font
   - Upload user font (if available)
*/

int
sisusb_reset_text_mode(struct sisusb_usb_data *sisusb, int init)
{
	int ret = 0, slot = sisusb->font_slot, i;
	const struct font_desc *myfont;
	u8 *tempbuf;
	u16 *tempbufb;
	size_t written;
	static const char bootstring[] = "SiSUSB VGA text console, (C) 2005 Thomas Winischhofer.";
	static const char bootlogo[] = "(o_ //\\ V_/_";

	/* sisusb->lock is down */

	if (!sisusb->SiS_Pr)
		return 1;

	sisusb->SiS_Pr->IOAddress = SISUSB_PCI_IOPORTBASE + 0x30;
	sisusb->SiS_Pr->sisusb = (void *)sisusb;

	/* Set mode 0x03 */
	SiSUSBSetMode(sisusb->SiS_Pr, 0x03);

	if (!(myfont = find_font("VGA8x16")))
		return 1;

	if (!(tempbuf = vmalloc(8192)))
		return 1;

	for (i = 0; i < 256; i++)
		memcpy(tempbuf + (i * 32), myfont->data + (i * 16), 16);

	/* Upload default font */
	ret = sisusbcon_do_font_op(sisusb, 1, 0, tempbuf, 8192, 0, 1, NULL, 16, 0);

	vfree(tempbuf);

	/* Upload user font (and reset current slot) */
	if (sisusb->font_backup) {
		ret |= sisusbcon_do_font_op(sisusb, 1, 2, sisusb->font_backup,
				8192, sisusb->font_backup_512, 1, NULL,
				sisusb->font_backup_height, 0);
		if (slot != 2)
			sisusbcon_do_font_op(sisusb, 1, 0, NULL, 0, 0, 1,
					NULL, 16, 0);
	}

	if (init && !sisusb->scrbuf) {

		if ((tempbuf = vmalloc(8192))) {

			i = 4096;
			tempbufb = (u16 *)tempbuf;
			while (i--)
				*(tempbufb++) = 0x0720;

			i = 0;
			tempbufb = (u16 *)tempbuf;
			while (bootlogo[i]) {
				*(tempbufb++) = 0x0700 | bootlogo[i++];
				if (!(i % 4))
					tempbufb += 76;
			}

			i = 0;
			tempbufb = (u16 *)tempbuf + 6;
			while (bootstring[i])
				*(tempbufb++) = 0x0700 | bootstring[i++];

			ret |= sisusb_copy_memory(sisusb, tempbuf,
				sisusb->vrambase, 8192, &written);

			vfree(tempbuf);

		}

	} else if (sisusb->scrbuf) {

		ret |= sisusb_copy_memory(sisusb, (char *)sisusb->scrbuf,
				sisusb->vrambase, sisusb->scrbuf_size, &written);

	}

	if (sisusb->sisusb_cursor_size_from >= 0 &&
	    sisusb->sisusb_cursor_size_to >= 0) {
		sisusb_setidxreg(sisusb, SISCR, 0x0a,
				sisusb->sisusb_cursor_size_from);
		sisusb_setidxregandor(sisusb, SISCR, 0x0b, 0xe0,
				sisusb->sisusb_cursor_size_to);
	} else {
		sisusb_setidxreg(sisusb, SISCR, 0x0a, 0x2d);
		sisusb_setidxreg(sisusb, SISCR, 0x0b, 0x0e);
		sisusb->sisusb_cursor_size_to = -1;
	}

	slot = sisusb->sisusb_cursor_loc;
	if(slot < 0) slot = 0;

	sisusb->sisusb_cursor_loc = -1;
	sisusb->bad_cursor_pos = 1;

	sisusb_set_cursor(sisusb, slot);

	sisusb_setidxreg(sisusb, SISCR, 0x0c, (sisusb->cur_start_addr >> 8));
	sisusb_setidxreg(sisusb, SISCR, 0x0d, (sisusb->cur_start_addr & 0xff));

	sisusb->textmodedestroyed = 0;

	/* sisusb->lock is down */

	return ret;
}

#endif

/* fops */

static int
sisusb_open(struct inode *inode, struct file *file)
{
	struct sisusb_usb_data *sisusb;
	struct usb_interface *interface;
	int subminor = iminor(inode);

	mutex_lock(&disconnect_mutex);

	if (!(interface = usb_find_interface(&sisusb_driver, subminor))) {
		printk(KERN_ERR "sisusb[%d]: Failed to find interface\n",
				subminor);
		mutex_unlock(&disconnect_mutex);
		return -ENODEV;
	}

	if (!(sisusb = usb_get_intfdata(interface))) {
		mutex_unlock(&disconnect_mutex);
		return -ENODEV;
	}

	mutex_lock(&sisusb->lock);

	if (!sisusb->present || !sisusb->ready) {
		mutex_unlock(&sisusb->lock);
		mutex_unlock(&disconnect_mutex);
		return -ENODEV;
	}

	if (sisusb->isopen) {
		mutex_unlock(&sisusb->lock);
		mutex_unlock(&disconnect_mutex);
		return -EBUSY;
	}

	if (!sisusb->devinit) {
		if (sisusb->sisusb_dev->speed == USB_SPEED_HIGH) {
			if (sisusb_init_gfxdevice(sisusb, 0)) {
				mutex_unlock(&sisusb->lock);
				mutex_unlock(&disconnect_mutex);
				printk(KERN_ERR
					"sisusbvga[%d]: Failed to initialize "
					"device\n",
					sisusb->minor);
				return -EIO;
			}
		} else {
			mutex_unlock(&sisusb->lock);
			mutex_unlock(&disconnect_mutex);
			printk(KERN_ERR
				"sisusbvga[%d]: Device not attached to "
				"USB 2.0 hub\n",
				sisusb->minor);
			return -EIO;
		}
	}

	/* Increment usage count for our sisusb */
	kref_get(&sisusb->kref);

	sisusb->isopen = 1;

	file->private_data = sisusb;

	mutex_unlock(&sisusb->lock);

	mutex_unlock(&disconnect_mutex);

	return 0;
}

void
sisusb_delete(struct kref *kref)
{
	struct sisusb_usb_data *sisusb = to_sisusb_dev(kref);

	if (!sisusb)
		return;

	if (sisusb->sisusb_dev)
		usb_put_dev(sisusb->sisusb_dev);

	sisusb->sisusb_dev = NULL;
	sisusb_free_buffers(sisusb);
	sisusb_free_urbs(sisusb);
#ifdef INCL_SISUSB_CON
	kfree(sisusb->SiS_Pr);
#endif
	kfree(sisusb);
}

static int
sisusb_release(struct inode *inode, struct file *file)
{
	struct sisusb_usb_data *sisusb;
	int myminor;

	mutex_lock(&disconnect_mutex);

	if (!(sisusb = (struct sisusb_usb_data *)file->private_data)) {
		mutex_unlock(&disconnect_mutex);
		return -ENODEV;
	}

	mutex_lock(&sisusb->lock);

	if (sisusb->present) {
		/* Wait for all URBs to finish if device still present */
		if (!sisusb_wait_all_out_complete(sisusb))
			sisusb_kill_all_busy(sisusb);
	}

	myminor = sisusb->minor;

	sisusb->isopen = 0;
	file->private_data = NULL;

	mutex_unlock(&sisusb->lock);

	/* decrement the usage count on our device */
	kref_put(&sisusb->kref, sisusb_delete);

	mutex_unlock(&disconnect_mutex);

	return 0;
}

static ssize_t
sisusb_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct sisusb_usb_data *sisusb;
	ssize_t bytes_read = 0;
	int errno = 0;
	u8 buf8;
	u16 buf16;
	u32 buf32, address;

	if (!(sisusb = (struct sisusb_usb_data *)file->private_data))
		return -ENODEV;

	mutex_lock(&sisusb->lock);

	/* Sanity check */
	if (!sisusb->present || !sisusb->ready || !sisusb->sisusb_dev) {
		mutex_unlock(&sisusb->lock);
		return -ENODEV;
	}

	if ((*ppos) >= SISUSB_PCI_PSEUDO_IOPORTBASE &&
	    (*ppos) <  SISUSB_PCI_PSEUDO_IOPORTBASE + 128) {

		address = (*ppos) -
			SISUSB_PCI_PSEUDO_IOPORTBASE +
			SISUSB_PCI_IOPORTBASE;

		/* Read i/o ports
		 * Byte, word and long(32) can be read. As this
		 * emulates inX instructions, the data returned is
		 * in machine-endianness.
		 */
		switch (count) {

			case 1:
				if (sisusb_read_memio_byte(sisusb,
							SISUSB_TYPE_IO,
							address, &buf8))
					errno = -EIO;
				else if (put_user(buf8, (u8 __user *)buffer))
					errno = -EFAULT;
				else
					bytes_read = 1;

				break;

			case 2:
				if (sisusb_read_memio_word(sisusb,
							SISUSB_TYPE_IO,
							address, &buf16))
					errno = -EIO;
				else if (put_user(buf16, (u16 __user *)buffer))
					errno = -EFAULT;
				else
					bytes_read = 2;

				break;

			case 4:
				if (sisusb_read_memio_long(sisusb,
							SISUSB_TYPE_IO,
							address, &buf32))
					errno = -EIO;
				else if (put_user(buf32, (u32 __user *)buffer))
					errno = -EFAULT;
				else
					bytes_read = 4;

				break;

			default:
				errno = -EIO;

		}

	} else if ((*ppos) >= SISUSB_PCI_PSEUDO_MEMBASE &&
		   (*ppos) <  SISUSB_PCI_PSEUDO_MEMBASE + sisusb->vramsize) {

		address = (*ppos) -
			SISUSB_PCI_PSEUDO_MEMBASE +
			SISUSB_PCI_MEMBASE;

		/* Read video ram
		 * Remember: Data delivered is never endian-corrected
		 */
		errno = sisusb_read_mem_bulk(sisusb, address,
					NULL, count, buffer, &bytes_read);

		if (bytes_read)
			errno = bytes_read;

	} else  if ((*ppos) >= SISUSB_PCI_PSEUDO_MMIOBASE &&
		    (*ppos) <  SISUSB_PCI_PSEUDO_MMIOBASE + SISUSB_PCI_MMIOSIZE) {

		address = (*ppos) -
			SISUSB_PCI_PSEUDO_MMIOBASE +
			SISUSB_PCI_MMIOBASE;

		/* Read MMIO
		 * Remember: Data delivered is never endian-corrected
		 */
		errno = sisusb_read_mem_bulk(sisusb, address,
					NULL, count, buffer, &bytes_read);

		if (bytes_read)
			errno = bytes_read;

	} else  if ((*ppos) >= SISUSB_PCI_PSEUDO_PCIBASE &&
		    (*ppos) <= SISUSB_PCI_PSEUDO_PCIBASE + 0x5c) {

		if (count != 4) {
			mutex_unlock(&sisusb->lock);
			return -EINVAL;
		}

		address = (*ppos) - SISUSB_PCI_PSEUDO_PCIBASE;

		/* Read PCI config register
		 * Return value delivered in machine endianness.
		 */
		if (sisusb_read_pci_config(sisusb, address, &buf32))
			errno = -EIO;
		else if (put_user(buf32, (u32 __user *)buffer))
			errno = -EFAULT;
		else
			bytes_read = 4;

	} else {

		errno = -EBADFD;

	}

	(*ppos) += bytes_read;

	mutex_unlock(&sisusb->lock);

	return errno ? errno : bytes_read;
}

static ssize_t
sisusb_write(struct file *file, const char __user *buffer, size_t count,
								loff_t *ppos)
{
	struct sisusb_usb_data *sisusb;
	int errno = 0;
	ssize_t bytes_written = 0;
	u8 buf8;
	u16 buf16;
	u32 buf32, address;

	if (!(sisusb = (struct sisusb_usb_data *)file->private_data))
		return -ENODEV;

	mutex_lock(&sisusb->lock);

	/* Sanity check */
	if (!sisusb->present || !sisusb->ready || !sisusb->sisusb_dev) {
		mutex_unlock(&sisusb->lock);
		return -ENODEV;
	}

	if ((*ppos) >= SISUSB_PCI_PSEUDO_IOPORTBASE &&
	    (*ppos) <  SISUSB_PCI_PSEUDO_IOPORTBASE + 128) {

		address = (*ppos) -
			SISUSB_PCI_PSEUDO_IOPORTBASE +
			SISUSB_PCI_IOPORTBASE;

		/* Write i/o ports
		 * Byte, word and long(32) can be written. As this
		 * emulates outX instructions, the data is expected
		 * in machine-endianness.
		 */
		switch (count) {

			case 1:
				if (get_user(buf8, (u8 __user *)buffer))
					errno = -EFAULT;
				else if (sisusb_write_memio_byte(sisusb,
							SISUSB_TYPE_IO,
							address, buf8))
					errno = -EIO;
				else
					bytes_written = 1;

				break;

			case 2:
				if (get_user(buf16, (u16 __user *)buffer))
					errno = -EFAULT;
				else if (sisusb_write_memio_word(sisusb,
							SISUSB_TYPE_IO,
							address, buf16))
					errno = -EIO;
				else
					bytes_written = 2;

				break;

			case 4:
				if (get_user(buf32, (u32 __user *)buffer))
					errno = -EFAULT;
				else if (sisusb_write_memio_long(sisusb,
							SISUSB_TYPE_IO,
							address, buf32))
					errno = -EIO;
				else
					bytes_written = 4;

				break;

			default:
				errno = -EIO;
		}

	} else if ((*ppos) >= SISUSB_PCI_PSEUDO_MEMBASE &&
		   (*ppos) <  SISUSB_PCI_PSEUDO_MEMBASE + sisusb->vramsize) {

		address = (*ppos) -
			SISUSB_PCI_PSEUDO_MEMBASE +
			SISUSB_PCI_MEMBASE;

		/* Write video ram.
		 * Buffer is copied 1:1, therefore, on big-endian
		 * machines, the data must be swapped by userland
		 * in advance (if applicable; no swapping in 8bpp
		 * mode or if YUV data is being transferred).
		 */
		errno = sisusb_write_mem_bulk(sisusb, address, NULL,
					count, buffer, 0, &bytes_written);

		if (bytes_written)
			errno = bytes_written;

	} else  if ((*ppos) >= SISUSB_PCI_PSEUDO_MMIOBASE &&
		    (*ppos) <  SISUSB_PCI_PSEUDO_MMIOBASE + SISUSB_PCI_MMIOSIZE) {

		address = (*ppos) -
			SISUSB_PCI_PSEUDO_MMIOBASE +
			SISUSB_PCI_MMIOBASE;

		/* Write MMIO.
		 * Buffer is copied 1:1, therefore, on big-endian
		 * machines, the data must be swapped by userland
		 * in advance.
		 */
		errno = sisusb_write_mem_bulk(sisusb, address, NULL,
					count, buffer, 0, &bytes_written);

		if (bytes_written)
			errno = bytes_written;

	} else  if ((*ppos) >= SISUSB_PCI_PSEUDO_PCIBASE &&
		    (*ppos) <= SISUSB_PCI_PSEUDO_PCIBASE + SISUSB_PCI_PCONFSIZE) {

		if (count != 4) {
			mutex_unlock(&sisusb->lock);
			return -EINVAL;
		}

		address = (*ppos) - SISUSB_PCI_PSEUDO_PCIBASE;

		/* Write PCI config register.
		 * Given value expected in machine endianness.
		 */
		if (get_user(buf32, (u32 __user *)buffer))
			errno = -EFAULT;
		else if (sisusb_write_pci_config(sisusb, address, buf32))
			errno = -EIO;
		else
			bytes_written = 4;


	} else {

		/* Error */
		errno = -EBADFD;

	}

	(*ppos) += bytes_written;

	mutex_unlock(&sisusb->lock);

	return errno ? errno : bytes_written;
}

static loff_t
sisusb_lseek(struct file *file, loff_t offset, int orig)
{
	struct sisusb_usb_data *sisusb;
	loff_t ret;

	if (!(sisusb = (struct sisusb_usb_data *)file->private_data))
		return -ENODEV;

	mutex_lock(&sisusb->lock);

	/* Sanity check */
	if (!sisusb->present || !sisusb->ready || !sisusb->sisusb_dev) {
		mutex_unlock(&sisusb->lock);
		return -ENODEV;
	}

	switch (orig) {
		case 0:
			file->f_pos = offset;
			ret = file->f_pos;
			/* never negative, no force_successful_syscall needed */
			break;
		case 1:
			file->f_pos += offset;
			ret = file->f_pos;
			/* never negative, no force_successful_syscall needed */
			break;
		default:
			/* seeking relative to "end of file" is not supported */
			ret = -EINVAL;
	}

	mutex_unlock(&sisusb->lock);
	return ret;
}

static int
sisusb_handle_command(struct sisusb_usb_data *sisusb, struct sisusb_command *y,
							unsigned long arg)
{
	int 	retval, port, length;
	u32	address;

	/* All our commands require the device
	 * to be initialized.
	 */
	if (!sisusb->devinit)
		return -ENODEV;

	port = y->data3 -
		SISUSB_PCI_PSEUDO_IOPORTBASE +
		SISUSB_PCI_IOPORTBASE;

	switch (y->operation) {
		case SUCMD_GET:
			retval = sisusb_getidxreg(sisusb, port,
							 y->data0, &y->data1);
			if (!retval) {
				if (copy_to_user((void __user *)arg, y,
							sizeof(*y)))
					retval = -EFAULT;
			}
			break;

		case SUCMD_SET:
			retval = sisusb_setidxreg(sisusb, port,
						y->data0, y->data1);
			break;

		case SUCMD_SETOR:
			retval = sisusb_setidxregor(sisusb, port,
						y->data0, y->data1);
			break;

		case SUCMD_SETAND:
			retval = sisusb_setidxregand(sisusb, port,
						y->data0, y->data1);
			break;

		case SUCMD_SETANDOR:
			retval = sisusb_setidxregandor(sisusb, port,
						y->data0, y->data1, y->data2);
			break;

		case SUCMD_SETMASK:
			retval = sisusb_setidxregmask(sisusb, port,
						y->data0, y->data1, y->data2);
			break;

		case SUCMD_CLRSCR:
			/* Gfx core must be initialized */
			if (!sisusb->gfxinit)
				return -ENODEV;

			length = (y->data0 << 16) | (y->data1 << 8) | y->data2;
			address = y->data3 -
				SISUSB_PCI_PSEUDO_MEMBASE +
				SISUSB_PCI_MEMBASE;
			retval = sisusb_clear_vram(sisusb, address, length);
			break;

		case SUCMD_HANDLETEXTMODE:
			retval = 0;
#ifdef INCL_SISUSB_CON
			/* Gfx core must be initialized, SiS_Pr must exist */
			if (!sisusb->gfxinit || !sisusb->SiS_Pr)
				return -ENODEV;

			switch (y->data0) {
			case 0:
				retval = sisusb_reset_text_mode(sisusb, 0);
				break;
			case 1:
				sisusb->textmodedestroyed = 1;
				break;
			}
#endif
			break;

#ifdef INCL_SISUSB_CON
		case SUCMD_SETMODE:
			/* Gfx core must be initialized, SiS_Pr must exist */
			if (!sisusb->gfxinit || !sisusb->SiS_Pr)
				return -ENODEV;

			retval = 0;

			sisusb->SiS_Pr->IOAddress = SISUSB_PCI_IOPORTBASE + 0x30;
			sisusb->SiS_Pr->sisusb = (void *)sisusb;

			if (SiSUSBSetMode(sisusb->SiS_Pr, y->data3))
				retval = -EINVAL;

			break;

		case SUCMD_SETVESAMODE:
			/* Gfx core must be initialized, SiS_Pr must exist */
			if (!sisusb->gfxinit || !sisusb->SiS_Pr)
				return -ENODEV;

			retval = 0;

			sisusb->SiS_Pr->IOAddress = SISUSB_PCI_IOPORTBASE + 0x30;
			sisusb->SiS_Pr->sisusb = (void *)sisusb;

			if (SiSUSBSetVESAMode(sisusb->SiS_Pr, y->data3))
				retval = -EINVAL;

			break;
#endif

		default:
			retval = -EINVAL;
	}

	if (retval > 0)
		retval = -EIO;

	return retval;
}

static int
sisusb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	     						unsigned long arg)
{
	struct sisusb_usb_data *sisusb;
	struct sisusb_info x;
	struct sisusb_command y;
	int 	retval = 0;
	u32 __user *argp = (u32 __user *)arg;

	if (!(sisusb = (struct sisusb_usb_data *)file->private_data))
		return -ENODEV;

	mutex_lock(&sisusb->lock);

	/* Sanity check */
	if (!sisusb->present || !sisusb->ready || !sisusb->sisusb_dev) {
		retval = -ENODEV;
		goto err_out;
	}

	switch (cmd) {

		case SISUSB_GET_CONFIG_SIZE:

			if (put_user(sizeof(x), argp))
				retval = -EFAULT;

			break;

		case SISUSB_GET_CONFIG:

			x.sisusb_id   	    = SISUSB_ID;
			x.sisusb_version    = SISUSB_VERSION;
			x.sisusb_revision   = SISUSB_REVISION;
			x.sisusb_patchlevel = SISUSB_PATCHLEVEL;
			x.sisusb_gfxinit    = sisusb->gfxinit;
			x.sisusb_vrambase   = SISUSB_PCI_PSEUDO_MEMBASE;
			x.sisusb_mmiobase   = SISUSB_PCI_PSEUDO_MMIOBASE;
			x.sisusb_iobase     = SISUSB_PCI_PSEUDO_IOPORTBASE;
			x.sisusb_pcibase    = SISUSB_PCI_PSEUDO_PCIBASE;
			x.sisusb_vramsize   = sisusb->vramsize;
			x.sisusb_minor	    = sisusb->minor;
			x.sisusb_fbdevactive= 0;
#ifdef INCL_SISUSB_CON
			x.sisusb_conactive  = sisusb->haveconsole ? 1 : 0;
#else
			x.sisusb_conactive  = 0;
#endif

			if (copy_to_user((void __user *)arg, &x, sizeof(x)))
				retval = -EFAULT;

			break;

		case SISUSB_COMMAND:

			if (copy_from_user(&y, (void __user *)arg, sizeof(y)))
				retval = -EFAULT;
			else
				retval = sisusb_handle_command(sisusb, &y, arg);

			break;

		default:
			retval = -ENOTTY;
			break;
	}

err_out:
	mutex_unlock(&sisusb->lock);
	return retval;
}

#ifdef SISUSB_NEW_CONFIG_COMPAT
static long
sisusb_compat_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	long retval;

	switch (cmd) {
		case SISUSB_GET_CONFIG_SIZE:
		case SISUSB_GET_CONFIG:
		case SISUSB_COMMAND:
			lock_kernel();
			retval = sisusb_ioctl(f->f_path.dentry->d_inode, f, cmd, arg);
			unlock_kernel();
			return retval;

		default:
			return -ENOIOCTLCMD;
	}
}
#endif

static const struct file_operations usb_sisusb_fops = {
	.owner =	THIS_MODULE,
	.open =		sisusb_open,
	.release =	sisusb_release,
	.read =		sisusb_read,
	.write =	sisusb_write,
	.llseek = 	sisusb_lseek,
#ifdef SISUSB_NEW_CONFIG_COMPAT
	.compat_ioctl = sisusb_compat_ioctl,
#endif
	.ioctl =	sisusb_ioctl
};

static struct usb_class_driver usb_sisusb_class = {
	.name =		"sisusbvga%d",
	.fops =		&usb_sisusb_fops,
	.minor_base =	SISUSB_MINOR
};

static int sisusb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct sisusb_usb_data *sisusb;
	int retval = 0, i;
	const char *memfail =
		KERN_ERR
		"sisusbvga[%d]: Failed to allocate memory for %s buffer\n";

	printk(KERN_INFO "sisusb: USB2VGA dongle found at address %d\n",
		dev->devnum);

	/* Allocate memory for our private */
	if (!(sisusb = kzalloc(sizeof(*sisusb), GFP_KERNEL))) {
		printk(KERN_ERR
			"sisusb: Failed to allocate memory for private data\n");
		return -ENOMEM;
	}
	kref_init(&sisusb->kref);

	mutex_init(&(sisusb->lock));

	/* Register device */
	if ((retval = usb_register_dev(intf, &usb_sisusb_class))) {
		printk(KERN_ERR
			"sisusb: Failed to get a minor for device %d\n",
			dev->devnum);
		retval = -ENODEV;
		goto error_1;
	}

	sisusb->sisusb_dev = dev;
	sisusb->minor      = intf->minor;
	sisusb->vrambase   = SISUSB_PCI_MEMBASE;
	sisusb->mmiobase   = SISUSB_PCI_MMIOBASE;
	sisusb->mmiosize   = SISUSB_PCI_MMIOSIZE;
	sisusb->ioportbase = SISUSB_PCI_IOPORTBASE;
	/* Everything else is zero */

	/* Allocate buffers */
	sisusb->ibufsize = SISUSB_IBUF_SIZE;
	if (!(sisusb->ibuf = usb_buffer_alloc(dev, SISUSB_IBUF_SIZE,
					GFP_KERNEL, &sisusb->transfer_dma_in))) {
		printk(memfail, "input", sisusb->minor);
		retval = -ENOMEM;
		goto error_2;
	}

	sisusb->numobufs = 0;
	sisusb->obufsize = SISUSB_OBUF_SIZE;
	for (i = 0; i < NUMOBUFS; i++) {
		if (!(sisusb->obuf[i] = usb_buffer_alloc(dev, SISUSB_OBUF_SIZE,
					GFP_KERNEL,
					&sisusb->transfer_dma_out[i]))) {
			if (i == 0) {
				printk(memfail, "output", sisusb->minor);
				retval = -ENOMEM;
				goto error_3;
			}
			break;
		} else
			sisusb->numobufs++;

	}

	/* Allocate URBs */
	if (!(sisusb->sisurbin = usb_alloc_urb(0, GFP_KERNEL))) {
		printk(KERN_ERR
			"sisusbvga[%d]: Failed to allocate URBs\n",
			sisusb->minor);
		retval = -ENOMEM;
		goto error_3;
	}
	sisusb->completein = 1;

	for (i = 0; i < sisusb->numobufs; i++) {
		if (!(sisusb->sisurbout[i] = usb_alloc_urb(0, GFP_KERNEL))) {
			printk(KERN_ERR
				"sisusbvga[%d]: Failed to allocate URBs\n",
				sisusb->minor);
			retval = -ENOMEM;
			goto error_4;
		}
		sisusb->urbout_context[i].sisusb = (void *)sisusb;
		sisusb->urbout_context[i].urbindex = i;
		sisusb->urbstatus[i] = 0;
	}

	printk(KERN_INFO "sisusbvga[%d]: Allocated %d output buffers\n",
					sisusb->minor, sisusb->numobufs);

#ifdef INCL_SISUSB_CON
	/* Allocate our SiS_Pr */
	if (!(sisusb->SiS_Pr = kmalloc(sizeof(struct SiS_Private), GFP_KERNEL))) {
		printk(KERN_ERR
			"sisusbvga[%d]: Failed to allocate SiS_Pr\n",
			sisusb->minor);
	}
#endif

	/* Do remaining init stuff */

	init_waitqueue_head(&sisusb->wait_q);

	usb_set_intfdata(intf, sisusb);

	usb_get_dev(sisusb->sisusb_dev);

	sisusb->present = 1;

#ifdef SISUSB_OLD_CONFIG_COMPAT
	{
	int ret;
	/* Our ioctls are all "32/64bit compatible" */
	ret =  register_ioctl32_conversion(SISUSB_GET_CONFIG_SIZE, NULL);
	ret |= register_ioctl32_conversion(SISUSB_GET_CONFIG,      NULL);
	ret |= register_ioctl32_conversion(SISUSB_COMMAND,         NULL);
	if (ret)
		printk(KERN_ERR
			"sisusbvga[%d]: Error registering ioctl32 "
			"translations\n",
			sisusb->minor);
	else
		sisusb->ioctl32registered = 1;
	}
#endif

	if (dev->speed == USB_SPEED_HIGH) {
		int initscreen = 1;
#ifdef INCL_SISUSB_CON
		if (sisusb_first_vc > 0 &&
		    sisusb_last_vc > 0 &&
		    sisusb_first_vc <= sisusb_last_vc &&
		    sisusb_last_vc <= MAX_NR_CONSOLES)
			initscreen = 0;
#endif
		if (sisusb_init_gfxdevice(sisusb, initscreen))
			printk(KERN_ERR
				"sisusbvga[%d]: Failed to early "
				"initialize device\n",
				sisusb->minor);

	} else
		printk(KERN_INFO
			"sisusbvga[%d]: Not attached to USB 2.0 hub, "
			"deferring init\n",
			sisusb->minor);

	sisusb->ready = 1;

#ifdef SISUSBENDIANTEST
	printk(KERN_DEBUG "sisusb: *** RWTEST ***\n");
	sisusb_testreadwrite(sisusb);
	printk(KERN_DEBUG "sisusb: *** RWTEST END ***\n");
#endif

#ifdef INCL_SISUSB_CON
	sisusb_console_init(sisusb, sisusb_first_vc, sisusb_last_vc);
#endif

	return 0;

error_4:
	sisusb_free_urbs(sisusb);
error_3:
	sisusb_free_buffers(sisusb);
error_2:
	usb_deregister_dev(intf, &usb_sisusb_class);
error_1:
	kfree(sisusb);
	return retval;
}

static void sisusb_disconnect(struct usb_interface *intf)
{
	struct sisusb_usb_data *sisusb;
	int minor;

	/* This should *not* happen */
	if (!(sisusb = usb_get_intfdata(intf)))
		return;

#ifdef INCL_SISUSB_CON
	sisusb_console_exit(sisusb);
#endif

	/* The above code doesn't need the disconnect
	 * semaphore to be down; its meaning is to
	 * protect all other routines from the disconnect
	 * case, not the other way round.
	 */
	mutex_lock(&disconnect_mutex);

	mutex_lock(&sisusb->lock);

	/* Wait for all URBs to complete and kill them in case (MUST do) */
	if (!sisusb_wait_all_out_complete(sisusb))
		sisusb_kill_all_busy(sisusb);

	minor = sisusb->minor;

	usb_set_intfdata(intf, NULL);

	usb_deregister_dev(intf, &usb_sisusb_class);

#ifdef SISUSB_OLD_CONFIG_COMPAT
	if (sisusb->ioctl32registered) {
		int ret;
		sisusb->ioctl32registered = 0;
		ret =  unregister_ioctl32_conversion(SISUSB_GET_CONFIG_SIZE);
		ret |= unregister_ioctl32_conversion(SISUSB_GET_CONFIG);
		ret |= unregister_ioctl32_conversion(SISUSB_COMMAND);
		if (ret) {
			printk(KERN_ERR
				"sisusbvga[%d]: Error unregistering "
				"ioctl32 translations\n",
				minor);
		}
	}
#endif

	sisusb->present = 0;
	sisusb->ready = 0;

	mutex_unlock(&sisusb->lock);

	/* decrement our usage count */
	kref_put(&sisusb->kref, sisusb_delete);

	mutex_unlock(&disconnect_mutex);

	printk(KERN_INFO "sisusbvga[%d]: Disconnected\n", minor);
}

static struct usb_device_id sisusb_table [] = {
	{ USB_DEVICE(0x0711, 0x0900) },
	{ USB_DEVICE(0x0711, 0x0901) },
	{ USB_DEVICE(0x0711, 0x0902) },
	{ USB_DEVICE(0x182d, 0x021c) },
	{ USB_DEVICE(0x182d, 0x0269) },
	{ }
};

MODULE_DEVICE_TABLE (usb, sisusb_table);

static struct usb_driver sisusb_driver = {
	.name =		"sisusb",
	.probe =	sisusb_probe,
	.disconnect =	sisusb_disconnect,
	.id_table =	sisusb_table,
};

static int __init usb_sisusb_init(void)
{
	int retval;

#ifdef INCL_SISUSB_CON
	sisusb_init_concode();
#endif

	if (!(retval = usb_register(&sisusb_driver))) {

		printk(KERN_INFO "sisusb: Driver version %d.%d.%d\n",
			SISUSB_VERSION, SISUSB_REVISION, SISUSB_PATCHLEVEL);
		printk(KERN_INFO
			"sisusb: Copyright (C) 2005 Thomas Winischhofer\n");

	}

	return retval;
}

static void __exit usb_sisusb_exit(void)
{
	usb_deregister(&sisusb_driver);
}

module_init(usb_sisusb_init);
module_exit(usb_sisusb_exit);

MODULE_AUTHOR("Thomas Winischhofer <thomas@winischhofer.net>");
MODULE_DESCRIPTION("sisusbvga - Driver for Net2280/SiS315-based USB2VGA dongles");
MODULE_LICENSE("GPL");

