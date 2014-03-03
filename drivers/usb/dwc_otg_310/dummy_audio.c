/*
 * zero.c -- Gadget Zero, for USB development
 *
 * Copyright (C) 2003-2004 David Brownell
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * Gadget Zero only needs two bulk endpoints, and is an example of how you
 * can write a hardware-agnostic gadget driver running inside a USB device.
 *
 * Hardware details are visible (see CONFIG_USB_ZERO_* below) but don't
 * affect most of the driver.
 *
 * Use it with the Linux host/master side "usbtest" driver to get a basic
 * functional test of your device-side usb stack, or with "usb-skeleton".
 *
 * It supports two similar configurations.  One sinks whatever the usb host
 * writes, and in return sources zeroes.  The other loops whatever the host
 * writes back, so the host can read it.  Module options include:
 *
 *   buflen=N		default N=4096, buffer size used
 *   qlen=N		default N=32, how many buffers in the loopback queue
 *   loopdefault	default false, list loopback config first
 *
 * Many drivers will only have one configuration, letting them be much
 * simpler if they also don't support high speed operation (like this
 * driver does).
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/uts.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>

#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>


/*-------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------*/


static int utf8_to_utf16le(const char *s, u16 *cp, unsigned len)
{
	int	count = 0;
	u8	c;
	u16	uchar;

	/* this insists on correct encodings, though not minimal ones.
	 * BUT it currently rejects legit 4-byte UTF-8 code points,
	 * which need surrogate pairs.  (Unicode 3.1 can use them.)
	 */
	while (len != 0 && (c = (u8) *s++) != 0) {
		if (unlikely(c & 0x80)) {
			// 2-byte sequence:
			// 00000yyyyyxxxxxx = 110yyyyy 10xxxxxx
			if ((c & 0xe0) == 0xc0) {
				uchar = (c & 0x1f) << 6;

				c = (u8) *s++;
				if ((c & 0xc0) != 0xc0)
					goto fail;
				c &= 0x3f;
				uchar |= c;

			// 3-byte sequence (most CJKV characters):
			// zzzzyyyyyyxxxxxx = 1110zzzz 10yyyyyy 10xxxxxx
			} else if ((c & 0xf0) == 0xe0) {
				uchar = (c & 0x0f) << 12;

				c = (u8) *s++;
				if ((c & 0xc0) != 0xc0)
					goto fail;
				c &= 0x3f;
				uchar |= c << 6;

				c = (u8) *s++;
				if ((c & 0xc0) != 0xc0)
					goto fail;
				c &= 0x3f;
				uchar |= c;

				/* no bogus surrogates */
				if (0xd800 <= uchar && uchar <= 0xdfff)
					goto fail;

			// 4-byte sequence (surrogate pairs, currently rare):
			// 11101110wwwwzzzzyy + 110111yyyyxxxxxx
			//     = 11110uuu 10uuzzzz 10yyyyyy 10xxxxxx
			// (uuuuu = wwww + 1)
			// FIXME accept the surrogate code points (only)

			} else
				goto fail;
		} else
			uchar = c;
		put_unaligned (cpu_to_le16 (uchar), cp++);
		count++;
		len--;
	}
	return count;
fail:
	return -1;
}


/**
 * usb_gadget_get_string - fill out a string descriptor 
 * @table: of c strings encoded using UTF-8
 * @id: string id, from low byte of wValue in get string descriptor
 * @buf: at least 256 bytes
 *
 * Finds the UTF-8 string matching the ID, and converts it into a
 * string descriptor in utf16-le.
 * Returns length of descriptor (always even) or negative errno
 *
 * If your driver needs stings in multiple languages, you'll probably
 * "switch (wIndex) { ... }"  in your ep0 string descriptor logic,
 * using this routine after choosing which set of UTF-8 strings to use.
 * Note that US-ASCII is a strict subset of UTF-8; any string bytes with
 * the eighth bit set will be multibyte UTF-8 characters, not ISO-8859/1
 * characters (which are also widely used in C strings).
 */
int
usb_gadget_get_string (struct usb_gadget_strings *table, int id, u8 *buf)
{
	struct usb_string	*s;
	int			len;

	/* descriptor 0 has the language id */
	if (id == 0) {
		buf [0] = 4;
		buf [1] = USB_DT_STRING;
		buf [2] = (u8) table->language;
		buf [3] = (u8) (table->language >> 8);
		return 4;
	}
	for (s = table->strings; s && s->s; s++)
		if (s->id == id)
			break;

	/* unrecognized: stall. */
	if (!s || !s->s)
		return -EINVAL;

	/* string descriptors have length, tag, then UTF16-LE text */
	len = min ((size_t) 126, strlen (s->s));
	memset (buf + 2, 0, 2 * len);	/* zero all the bytes */
	len = utf8_to_utf16le(s->s, (u16 *)&buf[2], len);
	if (len < 0)
		return -EINVAL;
	buf [0] = (len + 1) * 2;
	buf [1] = USB_DT_STRING;
	return buf [0];
}


/*-------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------*/


/**
 * usb_descriptor_fillbuf - fill buffer with descriptors
 * @buf: Buffer to be filled
 * @buflen: Size of buf
 * @src: Array of descriptor pointers, terminated by null pointer.
 *
 * Copies descriptors into the buffer, returning the length or a
 * negative error code if they can't all be copied.  Useful when
 * assembling descriptors for an associated set of interfaces used
 * as part of configuring a composite device; or in other cases where
 * sets of descriptors need to be marshaled.
 */
int
usb_descriptor_fillbuf(void *buf, unsigned buflen,
		const struct usb_descriptor_header **src)
{
	u8	*dest = buf;

	if (!src)
		return -EINVAL;

	/* fill buffer from src[] until null descriptor ptr */
	for (; 0 != *src; src++) {
		unsigned		len = (*src)->bLength;

		if (len > buflen)
			return -EINVAL;
		memcpy(dest, *src, len);
		buflen -= len;
		dest += len;
	}
	return dest - (u8 *)buf;
}


/**
 * usb_gadget_config_buf - builts a complete configuration descriptor
 * @config: Header for the descriptor, including characteristics such
 *	as power requirements and number of interfaces.
 * @desc: Null-terminated vector of pointers to the descriptors (interface,
 *	endpoint, etc) defining all functions in this device configuration.
 * @buf: Buffer for the resulting configuration descriptor.
 * @length: Length of buffer.  If this is not big enough to hold the
 *	entire configuration descriptor, an error code will be returned.
 *
 * This copies descriptors into the response buffer, building a descriptor
 * for that configuration.  It returns the buffer length or a negative
 * status code.  The config.wTotalLength field is set to match the length
 * of the result, but other descriptor fields (including power usage and
 * interface count) must be set by the caller.
 *
 * Gadget drivers could use this when constructing a config descriptor
 * in response to USB_REQ_GET_DESCRIPTOR.  They will need to patch the
 * resulting bDescriptorType value if USB_DT_OTHER_SPEED_CONFIG is needed.
 */
int usb_gadget_config_buf(
	const struct usb_config_descriptor	*config,
	void					*buf,
	unsigned				length,
	const struct usb_descriptor_header	**desc
)
{
	struct usb_config_descriptor		*cp = buf;
	int					len;

	/* config descriptor first */
	if (length < USB_DT_CONFIG_SIZE || !desc)
		return -EINVAL;
	*cp = *config; 

	/* then interface/endpoint/class/vendor/... */
	len = usb_descriptor_fillbuf(USB_DT_CONFIG_SIZE + (u8*)buf,
			length - USB_DT_CONFIG_SIZE, desc);
	if (len < 0)
		return len;
	len += USB_DT_CONFIG_SIZE;
	if (len > 0xffff)
		return -EINVAL;

	/* patch up the config descriptor */
	cp->bLength = USB_DT_CONFIG_SIZE;
	cp->bDescriptorType = USB_DT_CONFIG;
	cp->wTotalLength = cpu_to_le16(len);
	cp->bmAttributes |= USB_CONFIG_ATT_ONE;
	return len;
}

/*-------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------*/


#define RBUF_LEN (1024*1024)
static int rbuf_start;
static int rbuf_len;
static __u8 rbuf[RBUF_LEN];

/*-------------------------------------------------------------------------*/

#define DRIVER_VERSION		"St Patrick's Day 2004"

static const char shortname [] = "zero";
static const char longname [] = "YAMAHA YST-MS35D USB Speaker  ";

static const char source_sink [] = "source and sink data";
static const char loopback [] = "loop input to output";

/*-------------------------------------------------------------------------*/

/*
 * driver assumes self-powered hardware, and
 * has no way for users to trigger remote wakeup.
 *
 * this version autoconfigures as much as possible,
 * which is reasonable for most "bulk-only" drivers.
 */
static const char *EP_IN_NAME;		/* source */
static const char *EP_OUT_NAME;		/* sink */

/*-------------------------------------------------------------------------*/

/* big enough to hold our biggest descriptor */
#define USB_BUFSIZ	512

struct zero_dev {
	spinlock_t		lock;
	struct usb_gadget	*gadget;
	struct usb_request	*req;		/* for control responses */

	/* when configured, we have one of two configs:
	 * - source data (in to host) and sink it (out from host)
	 * - or loop it back (out from host back in to host)
	 */
	u8			config;
	struct usb_ep		*in_ep, *out_ep;

	/* autoresume timer */
	struct timer_list	resume;
};

#define xprintk(d,level,fmt,args...) \
	dev_printk(level , &(d)->gadget->dev , fmt , ## args)

#ifdef DEBUG
#define DBG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DBG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDBG	DBG
#else
#define VDBG(dev,fmt,args...) \
	do { } while (0)
#endif /* VERBOSE */

#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define WARN(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)

/*-------------------------------------------------------------------------*/

static unsigned buflen = 4096;
static unsigned qlen = 32;
static unsigned pattern = 0;

module_param (buflen, uint, S_IRUGO|S_IWUSR);
module_param (qlen, uint, S_IRUGO|S_IWUSR);
module_param (pattern, uint, S_IRUGO|S_IWUSR);

/*
 * if it's nonzero, autoresume says how many seconds to wait
 * before trying to wake up the host after suspend.
 */
static unsigned autoresume = 0;
module_param (autoresume, uint, 0);

/*
 * Normally the "loopback" configuration is second (index 1) so
 * it's not the default.  Here's where to change that order, to
 * work better with hosts where config changes are problematic.
 * Or controllers (like superh) that only support one config.
 */
static int loopdefault = 0;

module_param (loopdefault, bool, S_IRUGO|S_IWUSR);

/*-------------------------------------------------------------------------*/

/* Thanks to NetChip Technologies for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#ifndef	CONFIG_USB_ZERO_HNPTEST
#define DRIVER_VENDOR_NUM	0x0525		/* NetChip */
#define DRIVER_PRODUCT_NUM	0xa4a0		/* Linux-USB "Gadget Zero" */
#else
#define DRIVER_VENDOR_NUM	0x1a0a		/* OTG test device IDs */
#define DRIVER_PRODUCT_NUM	0xbadd
#endif

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full)
 * configuration descriptors are built on demand.
 */

/*
#define STRING_MANUFACTURER		25
#define STRING_PRODUCT			42
#define STRING_SERIAL			101
*/
#define STRING_MANUFACTURER		1
#define STRING_PRODUCT			2
#define STRING_SERIAL			3

#define STRING_SOURCE_SINK		250
#define STRING_LOOPBACK			251

/*
 * This device advertises two configurations; these numbers work
 * on a pxa250 as well as more flexible hardware.
 */
#define	CONFIG_SOURCE_SINK	3
#define	CONFIG_LOOPBACK		2

/*
static struct usb_device_descriptor
device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	.bDeviceClass =		USB_CLASS_VENDOR_SPEC,

	.idVendor =		__constant_cpu_to_le16 (DRIVER_VENDOR_NUM),
	.idProduct =		__constant_cpu_to_le16 (DRIVER_PRODUCT_NUM),
	.iManufacturer =	STRING_MANUFACTURER,
	.iProduct =		STRING_PRODUCT,
	.iSerialNumber =	STRING_SERIAL,
	.bNumConfigurations =	2,
};
*/
static struct usb_device_descriptor
device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		__constant_cpu_to_le16 (0x0100),
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass =      0,
	.bDeviceProtocol =      0,
	.bMaxPacketSize0 =      64,
	.bcdDevice =            __constant_cpu_to_le16 (0x0100),
	.idVendor =		__constant_cpu_to_le16 (0x0499),
	.idProduct =		__constant_cpu_to_le16 (0x3002),
	.iManufacturer =	STRING_MANUFACTURER,
	.iProduct =		STRING_PRODUCT,
	.iSerialNumber =	STRING_SERIAL,
	.bNumConfigurations =	1,
};

static struct usb_config_descriptor
z_config = {
	.bLength =		sizeof z_config,
	.bDescriptorType =	USB_DT_CONFIG,

	/* compute wTotalLength on the fly */
	.bNumInterfaces =	2,
	.bConfigurationValue =	1,
	.iConfiguration =	0,
	.bmAttributes =		0x40,
	.bMaxPower =		0,	/* self-powered */
};


static struct usb_otg_descriptor
otg_descriptor = {
	.bLength =		sizeof otg_descriptor,
	.bDescriptorType =	USB_DT_OTG,

	.bmAttributes =		USB_OTG_SRP,
};

/* one interface in each configuration */
#ifdef	CONFIG_USB_GADGET_DUALSPEED

/*
 * usb 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 *
 * that means alternate endpoint descriptors (bigger packets)
 * and a "device qualifier" ... plus more construction options
 * for the config descriptor.
 */

static struct usb_qualifier_descriptor
dev_qualifier = {
	.bLength =		sizeof dev_qualifier,
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,

	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	.bDeviceClass =		USB_CLASS_VENDOR_SPEC,

	.bNumConfigurations =	2,
};


struct usb_cs_as_general_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;

	__u8  bDescriptorSubType;
	__u8  bTerminalLink;
	__u8  bDelay;
	__u16  wFormatTag;
} __attribute__ ((packed));

struct usb_cs_as_format_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;

	__u8  bDescriptorSubType;
	__u8  bFormatType;
	__u8  bNrChannels;
	__u8  bSubframeSize;
	__u8  bBitResolution;
	__u8  bSamfreqType;
	__u8  tLowerSamFreq[3];
	__u8  tUpperSamFreq[3];
} __attribute__ ((packed));

static const struct usb_interface_descriptor
z_audio_control_if_desc = {
	.bLength =		sizeof z_audio_control_if_desc,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = 0x1,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
};

static const struct usb_interface_descriptor
z_audio_if_desc = {
	.bLength =		sizeof z_audio_if_desc,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = 0x2,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
};

static const struct usb_interface_descriptor
z_audio_if_desc2 = {
	.bLength =		sizeof z_audio_if_desc,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 1,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = 0x2,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
};

static const struct usb_cs_as_general_descriptor
z_audio_cs_as_if_desc = {
	.bLength = 7,
	.bDescriptorType = 0x24,
	
	.bDescriptorSubType = 0x01,
	.bTerminalLink = 0x01,
	.bDelay = 0x0,
	.wFormatTag = __constant_cpu_to_le16 (0x0001)
};


static const struct usb_cs_as_format_descriptor 
z_audio_cs_as_format_desc = {
	.bLength = 0xe,
	.bDescriptorType = 0x24,
	
	.bDescriptorSubType = 2,
	.bFormatType = 1,
	.bNrChannels = 1,
	.bSubframeSize = 1,
	.bBitResolution = 8,
	.bSamfreqType = 0,
	.tLowerSamFreq = {0x7e, 0x13, 0x00},
	.tUpperSamFreq = {0xe2, 0xd6, 0x00},
};

static const struct usb_endpoint_descriptor 
z_iso_ep = {
	.bLength = 0x09,
	.bDescriptorType = 0x05,
	.bEndpointAddress = 0x04,
	.bmAttributes = 0x09,
	.wMaxPacketSize = 0x0038,
	.bInterval = 0x01,
	.bRefresh = 0x00,
	.bSynchAddress = 0x00,	
};

static char z_iso_ep2[] = {0x07, 0x25, 0x01, 0x00, 0x02, 0x00, 0x02};

// 9 bytes
static char z_ac_interface_header_desc[] = 
{ 0x09, 0x24, 0x01, 0x00, 0x01, 0x2b, 0x00, 0x01, 0x01 };

// 12 bytes
static char z_0[] = {0x0c, 0x24, 0x02, 0x01, 0x01, 0x01, 0x00, 0x02, 
		     0x03, 0x00, 0x00, 0x00};
// 13 bytes
static char z_1[] = {0x0d, 0x24, 0x06, 0x02, 0x01, 0x02, 0x15, 0x00, 
		     0x02, 0x00, 0x02, 0x00, 0x00};
// 9 bytes
static char z_2[] = {0x09, 0x24, 0x03, 0x03, 0x01, 0x03, 0x00, 0x02, 
		     0x00};

static char za_0[] = {0x09, 0x04, 0x01, 0x02, 0x01, 0x01, 0x02, 0x00, 
		      0x00};

static char za_1[] = {0x07, 0x24, 0x01, 0x01, 0x00, 0x01, 0x00};

static char za_2[] = {0x0e, 0x24, 0x02, 0x01, 0x02, 0x01, 0x08, 0x00, 
		      0x7e, 0x13, 0x00, 0xe2, 0xd6, 0x00};

static char za_3[] = {0x09, 0x05, 0x04, 0x09, 0x70, 0x00, 0x01, 0x00,
		      0x00};

static char za_4[] = {0x07, 0x25, 0x01, 0x00, 0x02, 0x00, 0x02};

static char za_5[] = {0x09, 0x04, 0x01, 0x03, 0x01, 0x01, 0x02, 0x00,
		      0x00};

static char za_6[] = {0x07, 0x24, 0x01, 0x01, 0x00, 0x01, 0x00};

static char za_7[] = {0x0e, 0x24, 0x02, 0x01, 0x01, 0x02, 0x10, 0x00,
		      0x7e, 0x13, 0x00, 0xe2, 0xd6, 0x00};

static char za_8[] = {0x09, 0x05, 0x04, 0x09, 0x70, 0x00, 0x01, 0x00,
		      0x00};

static char za_9[] = {0x07, 0x25, 0x01, 0x00, 0x02, 0x00, 0x02};

static char za_10[] = {0x09, 0x04, 0x01, 0x04, 0x01, 0x01, 0x02, 0x00,
		       0x00};

static char za_11[] = {0x07, 0x24, 0x01, 0x01, 0x00, 0x01, 0x00};

static char za_12[] = {0x0e, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x00,
		       0x73, 0x13, 0x00, 0xe2, 0xd6, 0x00};

static char za_13[] = {0x09, 0x05, 0x04, 0x09, 0xe0, 0x00, 0x01, 0x00,
		       0x00};

static char za_14[] = {0x07, 0x25, 0x01, 0x00, 0x02, 0x00, 0x02};

static char za_15[] = {0x09, 0x04, 0x01, 0x05, 0x01, 0x01, 0x02, 0x00, 
		       0x00};

static char za_16[] = {0x07, 0x24, 0x01, 0x01, 0x00, 0x01, 0x00};

static char za_17[] = {0x0e, 0x24, 0x02, 0x01, 0x01, 0x03, 0x14, 0x00, 
		       0x7e, 0x13, 0x00, 0xe2, 0xd6, 0x00};

static char za_18[] = {0x09, 0x05, 0x04, 0x09, 0xa8, 0x00, 0x01, 0x00,
		       0x00};

static char za_19[] = {0x07, 0x25, 0x01, 0x00, 0x02, 0x00, 0x02};

static char za_20[] = {0x09, 0x04, 0x01, 0x06, 0x01, 0x01, 0x02, 0x00,
		       0x00};

static char za_21[] = {0x07, 0x24, 0x01, 0x01, 0x00, 0x01, 0x00};

static char za_22[] = {0x0e, 0x24, 0x02, 0x01, 0x02, 0x03, 0x14, 0x00, 
		       0x7e, 0x13, 0x00, 0xe2, 0xd6, 0x00};

static char za_23[] = {0x09, 0x05, 0x04, 0x09, 0x50, 0x01, 0x01, 0x00,
		       0x00};

static char za_24[] = {0x07, 0x25, 0x01, 0x00, 0x02, 0x00, 0x02};



static const struct usb_descriptor_header *z_function [] = {
	(struct usb_descriptor_header *) &z_audio_control_if_desc,
	(struct usb_descriptor_header *) &z_ac_interface_header_desc,
	(struct usb_descriptor_header *) &z_0,
	(struct usb_descriptor_header *) &z_1,
	(struct usb_descriptor_header *) &z_2,
	(struct usb_descriptor_header *) &z_audio_if_desc,
	(struct usb_descriptor_header *) &z_audio_if_desc2,
	(struct usb_descriptor_header *) &z_audio_cs_as_if_desc,
	(struct usb_descriptor_header *) &z_audio_cs_as_format_desc,
	(struct usb_descriptor_header *) &z_iso_ep,
	(struct usb_descriptor_header *) &z_iso_ep2,
	(struct usb_descriptor_header *) &za_0,
	(struct usb_descriptor_header *) &za_1,
	(struct usb_descriptor_header *) &za_2,
	(struct usb_descriptor_header *) &za_3,
	(struct usb_descriptor_header *) &za_4,
	(struct usb_descriptor_header *) &za_5,
	(struct usb_descriptor_header *) &za_6,
	(struct usb_descriptor_header *) &za_7,
	(struct usb_descriptor_header *) &za_8,
	(struct usb_descriptor_header *) &za_9,
	(struct usb_descriptor_header *) &za_10,
	(struct usb_descriptor_header *) &za_11,
	(struct usb_descriptor_header *) &za_12,
	(struct usb_descriptor_header *) &za_13,
	(struct usb_descriptor_header *) &za_14,
	(struct usb_descriptor_header *) &za_15,
	(struct usb_descriptor_header *) &za_16,
	(struct usb_descriptor_header *) &za_17,
	(struct usb_descriptor_header *) &za_18,
	(struct usb_descriptor_header *) &za_19,
	(struct usb_descriptor_header *) &za_20,
	(struct usb_descriptor_header *) &za_21,
	(struct usb_descriptor_header *) &za_22,
	(struct usb_descriptor_header *) &za_23,
	(struct usb_descriptor_header *) &za_24,
	NULL,
};

/* maxpacket and other transfer characteristics vary by speed. */
#define ep_desc(g,hs,fs) (((g)->speed==USB_SPEED_HIGH)?(hs):(fs))

#else

/* if there's no high speed support, maxpacket doesn't change. */
#define ep_desc(g,hs,fs) fs

#endif	/* !CONFIG_USB_GADGET_DUALSPEED */

static char				manufacturer [40];
//static char				serial [40];
static char				serial [] = "Ser 00 em";

/* static strings, in UTF-8 */
static struct usb_string		strings [] = {
	{ STRING_MANUFACTURER, manufacturer, },
	{ STRING_PRODUCT, longname, },
	{ STRING_SERIAL, serial, },
	{ STRING_LOOPBACK, loopback, },
	{ STRING_SOURCE_SINK, source_sink, },
	{  }			/* end of list */
};

static struct usb_gadget_strings	stringtab = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings,
};

/*
 * config descriptors are also handcrafted.  these must agree with code
 * that sets configurations, and with code managing interfaces and their
 * altsettings.  other complexity may come from:
 *
 *  - high speed support, including "other speed config" rules
 *  - multiple configurations
 *  - interfaces with alternate settings
 *  - embedded class or vendor-specific descriptors
 *
 * this handles high speed, and has a second config that could as easily
 * have been an alternate interface setting (on most hardware).
 *
 * NOTE:  to demonstrate (and test) more USB capabilities, this driver
 * should include an altsetting to test interrupt transfers, including
 * high bandwidth modes at high speed.  (Maybe work like Intel's test
 * device?)
 */
static int
config_buf (struct usb_gadget *gadget, u8 *buf, u8 type, unsigned index)
{
	int len;
	const struct usb_descriptor_header **function;
	
	function = z_function;
	len = usb_gadget_config_buf (&z_config, buf, USB_BUFSIZ, function);
	if (len < 0)
		return len;
	((struct usb_config_descriptor *) buf)->bDescriptorType = type;
	return len;
}

/*-------------------------------------------------------------------------*/

static struct usb_request *
alloc_ep_req (struct usb_ep *ep, unsigned length)
{
	struct usb_request	*req;

	req = usb_ep_alloc_request (ep, GFP_ATOMIC);
	if (req) {
		req->length = length;
		req->buf = usb_ep_alloc_buffer (ep, length,
				&req->dma, GFP_ATOMIC);
		if (!req->buf) {
			usb_ep_free_request (ep, req);
			req = NULL;
		}
	}
	return req;
}

static void free_ep_req (struct usb_ep *ep, struct usb_request *req)
{
	if (req->buf)
		usb_ep_free_buffer (ep, req->buf, req->dma, req->length);
	usb_ep_free_request (ep, req);
}

/*-------------------------------------------------------------------------*/

/* optionally require specific source/sink data patterns  */

static int
check_read_data (
	struct zero_dev		*dev,
	struct usb_ep		*ep,
	struct usb_request	*req
)
{
	unsigned	i;
	u8		*buf = req->buf;

	for (i = 0; i < req->actual; i++, buf++) {
		switch (pattern) {
		/* all-zeroes has no synchronization issues */
		case 0:
			if (*buf == 0)
				continue;
			break;
		/* mod63 stays in sync with short-terminated transfers,
		 * or otherwise when host and gadget agree on how large
		 * each usb transfer request should be.  resync is done
		 * with set_interface or set_config.
		 */
		case 1:
			if (*buf == (u8)(i % 63))
				continue;
			break;
		}
		ERROR (dev, "bad OUT byte, buf [%d] = %d\n", i, *buf);
		usb_ep_set_halt (ep);
		return -EINVAL;
	}
	return 0;
}

/*-------------------------------------------------------------------------*/

static void zero_reset_config (struct zero_dev *dev)
{
	if (dev->config == 0)
		return;

	DBG (dev, "reset config\n");

	/* just disable endpoints, forcing completion of pending i/o.
	 * all our completion handlers free their requests in this case.
	 */
	if (dev->in_ep) {
		usb_ep_disable (dev->in_ep);
		dev->in_ep = NULL;
	}
	if (dev->out_ep) {
		usb_ep_disable (dev->out_ep);
		dev->out_ep = NULL;
	}
	dev->config = 0;
	del_timer (&dev->resume);
}

#define _write(f, buf, sz) (f->f_op->write(f, buf, sz, &f->f_pos))

static void 
zero_isoc_complete (struct usb_ep *ep, struct usb_request *req)
{
	struct zero_dev	*dev = ep->driver_data;
	int		status = req->status;
	int i, j;

	switch (status) {

	case 0: 			/* normal completion? */
		//printk ("\nzero ---------------> isoc normal completion %d bytes\n", req->actual);
		for (i=0, j=rbuf_start; i<req->actual; i++) {
			//printk ("%02x ", ((__u8*)req->buf)[i]);
			rbuf[j] = ((__u8*)req->buf)[i];
			j++;
			if (j >= RBUF_LEN) j=0;
		}
		rbuf_start = j;
		//printk ("\n\n");

		if (rbuf_len < RBUF_LEN) {
			rbuf_len += req->actual;
			if (rbuf_len > RBUF_LEN) {
				rbuf_len = RBUF_LEN;
			}
		}

		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED: 		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		VDBG (dev, "%s gone (%d), %d/%d\n", ep->name, status,
				req->actual, req->length);
		if (ep == dev->out_ep)
			check_read_data (dev, ep, req);
		free_ep_req (ep, req);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
	default:
#if 1
		DBG (dev, "%s complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
#endif
	case -EREMOTEIO:		/* short read */
		break;
	}

	status = usb_ep_queue (ep, req, GFP_ATOMIC);
	if (status) {
		ERROR (dev, "kill %s:  resubmit %d bytes --> %d\n",
				ep->name, req->length, status);
		usb_ep_set_halt (ep);
		/* FIXME recover later ... somehow */
	}
}

static struct usb_request *
zero_start_isoc_ep (struct usb_ep *ep, int gfp_flags)
{
	struct usb_request	*req;
	int			status;

	req = alloc_ep_req (ep, 512);
	if (!req)
		return NULL;

	req->complete = zero_isoc_complete;

	status = usb_ep_queue (ep, req, gfp_flags);
	if (status) {
		struct zero_dev	*dev = ep->driver_data;

		ERROR (dev, "start %s --> %d\n", ep->name, status);
		free_ep_req (ep, req);
		req = NULL;
	}

	return req;
}

/* change our operational config.  this code must agree with the code
 * that returns config descriptors, and altsetting code.
 *
 * it's also responsible for power management interactions. some
 * configurations might not work with our current power sources.
 *
 * note that some device controller hardware will constrain what this
 * code can do, perhaps by disallowing more than one configuration or
 * by limiting configuration choices (like the pxa2xx).
 */
static int
zero_set_config (struct zero_dev *dev, unsigned number, int gfp_flags)
{
	int			result = 0;
	struct usb_gadget	*gadget = dev->gadget;
	const struct usb_endpoint_descriptor	*d;
	struct usb_ep		*ep;

	if (number == dev->config)
		return 0;

	zero_reset_config (dev);

	gadget_for_each_ep (ep, gadget) {

		if (strcmp (ep->name, "ep4") == 0) {

			d = (struct usb_endpoint_descripter *)&za_23; // isoc ep desc for audio i/f alt setting 6
			result = usb_ep_enable (ep, d);

			if (result == 0) {
				ep->driver_data = dev;
				dev->in_ep = ep;

				if (zero_start_isoc_ep (ep, gfp_flags) != 0) {

					dev->in_ep = ep;
					continue;
				}

				usb_ep_disable (ep);
				result = -EIO;
			}
		}

	}

	dev->config = number;
	return result;
}

/*-------------------------------------------------------------------------*/

static void zero_setup_complete (struct usb_ep *ep, struct usb_request *req)
{
	if (req->status || req->actual != req->length)
		DBG ((struct zero_dev *) ep->driver_data,
				"setup complete --> %d, %d/%d\n",
				req->status, req->actual, req->length);
}

/*
 * The setup() callback implements all the ep0 functionality that's
 * not handled lower down, in hardware or the hardware driver (like
 * device and endpoint feature flags, and their status).  It's all
 * housekeeping for the gadget function we're implementing.  Most of
 * the work is in config-specific setup.
 */
static int
zero_setup (struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	struct zero_dev		*dev = get_gadget_data (gadget);
	struct usb_request	*req = dev->req;
	int			value = -EOPNOTSUPP;

	/* usually this stores reply data in the pre-allocated ep0 buffer,
	 * but config change events will reconfigure hardware.
	 */
	req->zero = 0;
	switch (ctrl->bRequest) {

	case USB_REQ_GET_DESCRIPTOR:

		switch (ctrl->wValue >> 8) {

		case USB_DT_DEVICE:
			value = min (ctrl->wLength, (u16) sizeof device_desc);
			memcpy (req->buf, &device_desc, value);
			break;
#ifdef CONFIG_USB_GADGET_DUALSPEED
		case USB_DT_DEVICE_QUALIFIER:
			if (!gadget->is_dualspeed)
				break;
			value = min (ctrl->wLength, (u16) sizeof dev_qualifier);
			memcpy (req->buf, &dev_qualifier, value);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
			if (!gadget->is_dualspeed)
				break;
			// FALLTHROUGH
#endif /* CONFIG_USB_GADGET_DUALSPEED */
		case USB_DT_CONFIG:
			value = config_buf (gadget, req->buf,
					ctrl->wValue >> 8,
					ctrl->wValue & 0xff);
			if (value >= 0)
				value = min (ctrl->wLength, (u16) value);
			break;

		case USB_DT_STRING:
			/* wIndex == language code.
			 * this driver only handles one language, you can
			 * add string tables for other languages, using
			 * any UTF-8 characters
			 */
			value = usb_gadget_get_string (&stringtab,
					ctrl->wValue & 0xff, req->buf);
			if (value >= 0) {
				value = min (ctrl->wLength, (u16) value);
			}
			break;
		}
		break;

	/* currently two configs, two speeds */
	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0)
			goto unknown;

		spin_lock (&dev->lock);
		value = zero_set_config (dev, ctrl->wValue, GFP_ATOMIC);
		spin_unlock (&dev->lock);
		break;
	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != USB_DIR_IN)
			goto unknown;
		*(u8 *)req->buf = dev->config;
		value = min (ctrl->wLength, (u16) 1);
		break;

	/* until we add altsetting support, or other interfaces,
	 * only 0/0 are possible.  pxa2xx only supports 0/0 (poorly)
	 * and already killed pending endpoint I/O.
	 */
	case USB_REQ_SET_INTERFACE:

		if (ctrl->bRequestType != USB_RECIP_INTERFACE)
			goto unknown;
		spin_lock (&dev->lock);
		if (dev->config) {
			u8		config = dev->config;

			/* resets interface configuration, forgets about
			 * previous transaction state (queued bufs, etc)
			 * and re-inits endpoint state (toggle etc)
			 * no response queued, just zero status == success.
			 * if we had more than one interface we couldn't
			 * use this "reset the config" shortcut.
			 */
			zero_reset_config (dev);
			zero_set_config (dev, config, GFP_ATOMIC);
			value = 0;
		}
		spin_unlock (&dev->lock);
		break;
	case USB_REQ_GET_INTERFACE:
		if ((ctrl->bRequestType == 0x21) && (ctrl->wIndex == 0x02)) {
			value = ctrl->wLength;
			break;
		}
		else {
			if (ctrl->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE))
				goto unknown;
			if (!dev->config)
				break;
			if (ctrl->wIndex != 0) {
				value = -EDOM;
				break;
			}
			*(u8 *)req->buf = 0;
			value = min (ctrl->wLength, (u16) 1);
		}
		break;

	/*
	 * These are the same vendor-specific requests supported by
	 * Intel's USB 2.0 compliance test devices.  We exceed that
	 * device spec by allowing multiple-packet requests.
	 */
	case 0x5b:	/* control WRITE test -- fill the buffer */
		if (ctrl->bRequestType != (USB_DIR_OUT|USB_TYPE_VENDOR))
			goto unknown;
		if (ctrl->wValue || ctrl->wIndex)
			break;
		/* just read that many bytes into the buffer */
		if (ctrl->wLength > USB_BUFSIZ)
			break;
		value = ctrl->wLength;
		break;
	case 0x5c:	/* control READ test -- return the buffer */
		if (ctrl->bRequestType != (USB_DIR_IN|USB_TYPE_VENDOR))
			goto unknown;
		if (ctrl->wValue || ctrl->wIndex)
			break;
		/* expect those bytes are still in the buffer; send back */
		if (ctrl->wLength > USB_BUFSIZ
				|| ctrl->wLength != req->length)
			break;
		value = ctrl->wLength;
		break;

	case 0x01: // SET_CUR
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
		value = ctrl->wLength;
		break;
	case 0x81:
		switch (ctrl->wValue) {
		case 0x0201:
		case 0x0202:
			((u8*)req->buf)[0] = 0x00;
			((u8*)req->buf)[1] = 0xe3;
			break;
		case 0x0300:
		case 0x0500:
			((u8*)req->buf)[0] = 0x00;
			break;
		}
		//((u8*)req->buf)[0] = 0x81;
		//((u8*)req->buf)[1] = 0x81;
		value = ctrl->wLength;
		break;
	case 0x82:
		switch (ctrl->wValue) {
		case 0x0201:
		case 0x0202:
			((u8*)req->buf)[0] = 0x00;
			((u8*)req->buf)[1] = 0xc3;
			break;
		case 0x0300:
		case 0x0500:
			((u8*)req->buf)[0] = 0x00;
			break;
		}
		//((u8*)req->buf)[0] = 0x82;
		//((u8*)req->buf)[1] = 0x82;
		value = ctrl->wLength;
		break;
	case 0x83:
		switch (ctrl->wValue) {
		case 0x0201:
		case 0x0202:
			((u8*)req->buf)[0] = 0x00;
			((u8*)req->buf)[1] = 0x00;
			break;
		case 0x0300:
			((u8*)req->buf)[0] = 0x60;
			break;
		case 0x0500:	
			((u8*)req->buf)[0] = 0x18;
			break;
		}
		//((u8*)req->buf)[0] = 0x83;
		//((u8*)req->buf)[1] = 0x83;
		value = ctrl->wLength;
		break;
	case 0x84:
		switch (ctrl->wValue) {
		case 0x0201:
		case 0x0202:
			((u8*)req->buf)[0] = 0x00;
			((u8*)req->buf)[1] = 0x01;
			break;
		case 0x0300:
		case 0x0500:
			((u8*)req->buf)[0] = 0x08;
			break;
		}
		//((u8*)req->buf)[0] = 0x84;
		//((u8*)req->buf)[1] = 0x84;
		value = ctrl->wLength;
		break;
	case 0x85:
		((u8*)req->buf)[0] = 0x85;
		((u8*)req->buf)[1] = 0x85;
		value = ctrl->wLength;
		break;

	
	default:
unknown:
		printk("unknown control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			ctrl->wValue, ctrl->wIndex, ctrl->wLength);
	}

	/* respond with data transfer before status phase? */
	if (value >= 0) {
		req->length = value;
		req->zero = value < ctrl->wLength
				&& (value % gadget->ep0->maxpacket) == 0;
		value = usb_ep_queue (gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			DBG (dev, "ep_queue < 0 --> %d\n", value);
			req->status = 0;
			zero_setup_complete (gadget->ep0, req);
		}
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static void
zero_disconnect (struct usb_gadget *gadget)
{
	struct zero_dev		*dev = get_gadget_data (gadget);
	unsigned long		flags;

	spin_lock_irqsave (&dev->lock, flags);
	zero_reset_config (dev);

	/* a more significant application might have some non-usb
	 * activities to quiesce here, saving resources like power
	 * or pushing the notification up a network stack.
	 */
	spin_unlock_irqrestore (&dev->lock, flags);

	/* next we may get setup() calls to enumerate new connections;
	 * or an unbind() during shutdown (including removing module).
	 */
}

static void
zero_autoresume (unsigned long _dev)
{
	struct zero_dev	*dev = (struct zero_dev *) _dev;
	int		status;

	/* normally the host would be woken up for something
	 * more significant than just a timer firing...
	 */
	if (dev->gadget->speed != USB_SPEED_UNKNOWN) {
		status = usb_gadget_wakeup (dev->gadget);
		DBG (dev, "wakeup --> %d\n", status);
	}
}

/*-------------------------------------------------------------------------*/

static void
zero_unbind (struct usb_gadget *gadget)
{
	struct zero_dev		*dev = get_gadget_data (gadget);

	DBG (dev, "unbind\n");

	/* we've already been disconnected ... no i/o is active */
	if (dev->req)
		free_ep_req (gadget->ep0, dev->req);
	del_timer_sync (&dev->resume);
	kfree (dev);
	set_gadget_data (gadget, NULL);
}

static int
zero_bind (struct usb_gadget *gadget)
{
	struct zero_dev		*dev;
	//struct usb_ep		*ep;

	printk("binding\n");
	/*
	 * DRIVER POLICY CHOICE:  you may want to do this differently.
	 * One thing to avoid is reusing a bcdDevice revision code
	 * with different host-visible configurations or behavior
	 * restrictions -- using ep1in/ep2out vs ep1out/ep3in, etc
	 */
	//device_desc.bcdDevice = __constant_cpu_to_le16 (0x0201);


	/* ok, we made sense of the hardware ... */
	dev = kmalloc (sizeof *dev, SLAB_KERNEL);
	if (!dev)
		return -ENOMEM;
	memset (dev, 0, sizeof *dev);
	spin_lock_init (&dev->lock);
	dev->gadget = gadget;
	set_gadget_data (gadget, dev);

	/* preallocate control response and buffer */
	dev->req = usb_ep_alloc_request (gadget->ep0, GFP_KERNEL);
	if (!dev->req)
		goto enomem;
	dev->req->buf = usb_ep_alloc_buffer (gadget->ep0, USB_BUFSIZ,
				&dev->req->dma, GFP_KERNEL);
	if (!dev->req->buf)
		goto enomem;

	dev->req->complete = zero_setup_complete;

	device_desc.bMaxPacketSize0 = gadget->ep0->maxpacket;

#ifdef CONFIG_USB_GADGET_DUALSPEED
	/* assume ep0 uses the same value for both speeds ... */
	dev_qualifier.bMaxPacketSize0 = device_desc.bMaxPacketSize0;

	/* and that all endpoints are dual-speed */
	//hs_source_desc.bEndpointAddress = fs_source_desc.bEndpointAddress;
	//hs_sink_desc.bEndpointAddress = fs_sink_desc.bEndpointAddress;
#endif

	usb_gadget_set_selfpowered (gadget);

	init_timer (&dev->resume);
	dev->resume.function = zero_autoresume;
	dev->resume.data = (unsigned long) dev;

	gadget->ep0->driver_data = dev;

	INFO (dev, "%s, version: " DRIVER_VERSION "\n", longname);
	INFO (dev, "using %s, OUT %s IN %s\n", gadget->name,
		EP_OUT_NAME, EP_IN_NAME);

	snprintf (manufacturer, sizeof manufacturer,
		UTS_SYSNAME " " UTS_RELEASE " with %s",
		gadget->name);

	return 0;

enomem:
	zero_unbind (gadget);
	return -ENOMEM;
}

/*-------------------------------------------------------------------------*/

static void
zero_suspend (struct usb_gadget *gadget)
{
	struct zero_dev		*dev = get_gadget_data (gadget);

	if (gadget->speed == USB_SPEED_UNKNOWN)
		return;

	if (autoresume) {
		mod_timer (&dev->resume, jiffies + (HZ * autoresume));
		DBG (dev, "suspend, wakeup in %d seconds\n", autoresume);
	} else
		DBG (dev, "suspend\n");
}

static void
zero_resume (struct usb_gadget *gadget)
{
	struct zero_dev		*dev = get_gadget_data (gadget);

	DBG (dev, "resume\n");
	del_timer (&dev->resume);
}


/*-------------------------------------------------------------------------*/

static struct usb_gadget_driver zero_driver = {
#ifdef CONFIG_USB_GADGET_DUALSPEED
	.speed		= USB_SPEED_HIGH,
#else
	.speed		= USB_SPEED_FULL,
#endif
	.function	= (char *) longname,
	.bind		= zero_bind,
	.unbind		= zero_unbind,

	.setup		= zero_setup,
	.disconnect	= zero_disconnect,

	.suspend	= zero_suspend,
	.resume		= zero_resume,

	.driver 	= {
		.name		= (char *) shortname,
		// .shutdown = ...
		// .suspend = ...
		// .resume = ...
	},
};

MODULE_AUTHOR ("David Brownell");
MODULE_LICENSE ("Dual BSD/GPL");

static struct proc_dir_entry *pdir, *pfile;

static int isoc_read_data (char *page, char **start,
			   off_t off, int count,
			   int *eof, void *data)
{
	int i;
	static int c = 0;
	static int done = 0;
	static int s = 0;

/*
	printk ("\ncount: %d\n", count);
	printk ("rbuf_start: %d\n", rbuf_start);
	printk ("rbuf_len: %d\n", rbuf_len);
	printk ("off: %d\n", off);
	printk ("start: %p\n\n", *start);
*/
	if (done) {
		c = 0;
		done = 0;
		*eof = 1;
		return 0;
	}

	if (c == 0) {
		if (rbuf_len == RBUF_LEN)
			s = rbuf_start;
		else s = 0;
	}

	for (i=0; i<count && c<rbuf_len; i++, c++) {
		page[i] = rbuf[(c+s) % RBUF_LEN];
	}
	*start = page;
	
	if (c >= rbuf_len) {
		*eof = 1;
		done = 1;
	}


	return i;
}

static int __init init (void)
{

	int retval = 0;

	pdir = proc_mkdir("isoc_test", NULL);
	if(pdir == NULL) {
		retval = -ENOMEM;
		printk("Error creating dir\n");
		goto done;
	}
	pdir->owner = THIS_MODULE;

	pfile = create_proc_read_entry("isoc_data",
				       0444, pdir,
				       isoc_read_data,
				       NULL);
	if (pfile == NULL) {
		retval = -ENOMEM;
		printk("Error creating file\n");
		goto no_file;
	}
	pfile->owner = THIS_MODULE;

	return usb_gadget_register_driver (&zero_driver);

 no_file:
	remove_proc_entry("isoc_data", NULL);
 done:
	return retval;
}
module_init (init);

static void __exit cleanup (void)
{

	usb_gadget_unregister_driver (&zero_driver);
	
	remove_proc_entry("isoc_data", pdir);
	remove_proc_entry("isoc_test", NULL);
}
module_exit (cleanup);
