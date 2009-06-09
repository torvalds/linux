/*
 *      uvc_driver.c  --  USB Video Class driver
 *
 *      Copyright (C) 2005-2009
 *          Laurent Pinchart (laurent.pinchart@skynet.be)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

/*
 * This driver aims to support video input and ouput devices compliant with the
 * 'USB Video Class' specification.
 *
 * The driver doesn't support the deprecated v4l1 interface. It implements the
 * mmap capture method only, and doesn't do any image format conversion in
 * software. If your user-space application doesn't support YUYV or MJPEG, fix
 * it :-). Please note that the MJPEG data have been stripped from their
 * Huffman tables (DHT marker), you will need to add it back if your JPEG
 * codec can't handle MJPEG data.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/unaligned.h>

#include <media/v4l2-common.h>

#include "uvcvideo.h"

#define DRIVER_AUTHOR		"Laurent Pinchart <laurent.pinchart@skynet.be>"
#define DRIVER_DESC		"USB Video Class driver"
#ifndef DRIVER_VERSION
#define DRIVER_VERSION		"v0.1.0"
#endif

unsigned int uvc_no_drop_param;
static unsigned int uvc_quirks_param;
unsigned int uvc_trace_param;

/* ------------------------------------------------------------------------
 * Video formats
 */

static struct uvc_format_desc uvc_fmts[] = {
	{
		.name		= "YUV 4:2:2 (YUYV)",
		.guid		= UVC_GUID_FORMAT_YUY2,
		.fcc		= V4L2_PIX_FMT_YUYV,
	},
	{
		.name		= "YUV 4:2:0 (NV12)",
		.guid		= UVC_GUID_FORMAT_NV12,
		.fcc		= V4L2_PIX_FMT_NV12,
	},
	{
		.name		= "MJPEG",
		.guid		= UVC_GUID_FORMAT_MJPEG,
		.fcc		= V4L2_PIX_FMT_MJPEG,
	},
	{
		.name		= "YVU 4:2:0 (YV12)",
		.guid		= UVC_GUID_FORMAT_YV12,
		.fcc		= V4L2_PIX_FMT_YVU420,
	},
	{
		.name		= "YUV 4:2:0 (I420)",
		.guid		= UVC_GUID_FORMAT_I420,
		.fcc		= V4L2_PIX_FMT_YUV420,
	},
	{
		.name		= "YUV 4:2:2 (UYVY)",
		.guid		= UVC_GUID_FORMAT_UYVY,
		.fcc		= V4L2_PIX_FMT_UYVY,
	},
	{
		.name		= "Greyscale",
		.guid		= UVC_GUID_FORMAT_Y800,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.name		= "RGB Bayer",
		.guid		= UVC_GUID_FORMAT_BY8,
		.fcc		= V4L2_PIX_FMT_SBGGR8,
	},
};

/* ------------------------------------------------------------------------
 * Utility functions
 */

struct usb_host_endpoint *uvc_find_endpoint(struct usb_host_interface *alts,
		__u8 epaddr)
{
	struct usb_host_endpoint *ep;
	unsigned int i;

	for (i = 0; i < alts->desc.bNumEndpoints; ++i) {
		ep = &alts->endpoint[i];
		if (ep->desc.bEndpointAddress == epaddr)
			return ep;
	}

	return NULL;
}

static struct uvc_format_desc *uvc_format_by_guid(const __u8 guid[16])
{
	unsigned int len = ARRAY_SIZE(uvc_fmts);
	unsigned int i;

	for (i = 0; i < len; ++i) {
		if (memcmp(guid, uvc_fmts[i].guid, 16) == 0)
			return &uvc_fmts[i];
	}

	return NULL;
}

static __u32 uvc_colorspace(const __u8 primaries)
{
	static const __u8 colorprimaries[] = {
		0,
		V4L2_COLORSPACE_SRGB,
		V4L2_COLORSPACE_470_SYSTEM_M,
		V4L2_COLORSPACE_470_SYSTEM_BG,
		V4L2_COLORSPACE_SMPTE170M,
		V4L2_COLORSPACE_SMPTE240M,
	};

	if (primaries < ARRAY_SIZE(colorprimaries))
		return colorprimaries[primaries];

	return 0;
}

/* Simplify a fraction using a simple continued fraction decomposition. The
 * idea here is to convert fractions such as 333333/10000000 to 1/30 using
 * 32 bit arithmetic only. The algorithm is not perfect and relies upon two
 * arbitrary parameters to remove non-significative terms from the simple
 * continued fraction decomposition. Using 8 and 333 for n_terms and threshold
 * respectively seems to give nice results.
 */
void uvc_simplify_fraction(uint32_t *numerator, uint32_t *denominator,
		unsigned int n_terms, unsigned int threshold)
{
	uint32_t *an;
	uint32_t x, y, r;
	unsigned int i, n;

	an = kmalloc(n_terms * sizeof *an, GFP_KERNEL);
	if (an == NULL)
		return;

	/* Convert the fraction to a simple continued fraction. See
	 * http://mathforum.org/dr.math/faq/faq.fractions.html
	 * Stop if the current term is bigger than or equal to the given
	 * threshold.
	 */
	x = *numerator;
	y = *denominator;

	for (n = 0; n < n_terms && y != 0; ++n) {
		an[n] = x / y;
		if (an[n] >= threshold) {
			if (n < 2)
				n++;
			break;
		}

		r = x - an[n] * y;
		x = y;
		y = r;
	}

	/* Expand the simple continued fraction back to an integer fraction. */
	x = 0;
	y = 1;

	for (i = n; i > 0; --i) {
		r = y;
		y = an[i-1] * y + x;
		x = r;
	}

	*numerator = y;
	*denominator = x;
	kfree(an);
}

/* Convert a fraction to a frame interval in 100ns multiples. The idea here is
 * to compute numerator / denominator * 10000000 using 32 bit fixed point
 * arithmetic only.
 */
uint32_t uvc_fraction_to_interval(uint32_t numerator, uint32_t denominator)
{
	uint32_t multiplier;

	/* Saturate the result if the operation would overflow. */
	if (denominator == 0 ||
	    numerator/denominator >= ((uint32_t)-1)/10000000)
		return (uint32_t)-1;

	/* Divide both the denominator and the multiplier by two until
	 * numerator * multiplier doesn't overflow. If anyone knows a better
	 * algorithm please let me know.
	 */
	multiplier = 10000000;
	while (numerator > ((uint32_t)-1)/multiplier) {
		multiplier /= 2;
		denominator /= 2;
	}

	return denominator ? numerator * multiplier / denominator : 0;
}

/* ------------------------------------------------------------------------
 * Terminal and unit management
 */

static struct uvc_entity *uvc_entity_by_id(struct uvc_device *dev, int id)
{
	struct uvc_entity *entity;

	list_for_each_entry(entity, &dev->entities, list) {
		if (entity->id == id)
			return entity;
	}

	return NULL;
}

static struct uvc_entity *uvc_entity_by_reference(struct uvc_device *dev,
	int id, struct uvc_entity *entity)
{
	unsigned int i;

	if (entity == NULL)
		entity = list_entry(&dev->entities, struct uvc_entity, list);

	list_for_each_entry_continue(entity, &dev->entities, list) {
		switch (UVC_ENTITY_TYPE(entity)) {
		case TT_STREAMING:
			if (entity->output.bSourceID == id)
				return entity;
			break;

		case VC_PROCESSING_UNIT:
			if (entity->processing.bSourceID == id)
				return entity;
			break;

		case VC_SELECTOR_UNIT:
			for (i = 0; i < entity->selector.bNrInPins; ++i)
				if (entity->selector.baSourceID[i] == id)
					return entity;
			break;

		case VC_EXTENSION_UNIT:
			for (i = 0; i < entity->extension.bNrInPins; ++i)
				if (entity->extension.baSourceID[i] == id)
					return entity;
			break;
		}
	}

	return NULL;
}

/* ------------------------------------------------------------------------
 * Descriptors handling
 */

static int uvc_parse_format(struct uvc_device *dev,
	struct uvc_streaming *streaming, struct uvc_format *format,
	__u32 **intervals, unsigned char *buffer, int buflen)
{
	struct usb_interface *intf = streaming->intf;
	struct usb_host_interface *alts = intf->cur_altsetting;
	struct uvc_format_desc *fmtdesc;
	struct uvc_frame *frame;
	const unsigned char *start = buffer;
	unsigned char *_buffer;
	unsigned int interval;
	unsigned int i, n;
	int _buflen;
	__u8 ftype;

	format->type = buffer[2];
	format->index = buffer[3];

	switch (buffer[2]) {
	case VS_FORMAT_UNCOMPRESSED:
	case VS_FORMAT_FRAME_BASED:
		n = buffer[2] == VS_FORMAT_UNCOMPRESSED ? 27 : 28;
		if (buflen < n) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming"
			       "interface %d FORMAT error\n",
			       dev->udev->devnum,
			       alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/* Find the format descriptor from its GUID. */
		fmtdesc = uvc_format_by_guid(&buffer[5]);

		if (fmtdesc != NULL) {
			strlcpy(format->name, fmtdesc->name,
				sizeof format->name);
			format->fcc = fmtdesc->fcc;
		} else {
			uvc_printk(KERN_INFO, "Unknown video format "
				UVC_GUID_FORMAT "\n",
				UVC_GUID_ARGS(&buffer[5]));
			snprintf(format->name, sizeof format->name,
				UVC_GUID_FORMAT, UVC_GUID_ARGS(&buffer[5]));
			format->fcc = 0;
		}

		format->bpp = buffer[21];
		if (buffer[2] == VS_FORMAT_UNCOMPRESSED) {
			ftype = VS_FRAME_UNCOMPRESSED;
		} else {
			ftype = VS_FRAME_FRAME_BASED;
			if (buffer[27])
				format->flags = UVC_FMT_FLAG_COMPRESSED;
		}
		break;

	case VS_FORMAT_MJPEG:
		if (buflen < 11) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming"
			       "interface %d FORMAT error\n",
			       dev->udev->devnum,
			       alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		strlcpy(format->name, "MJPEG", sizeof format->name);
		format->fcc = V4L2_PIX_FMT_MJPEG;
		format->flags = UVC_FMT_FLAG_COMPRESSED;
		format->bpp = 0;
		ftype = VS_FRAME_MJPEG;
		break;

	case VS_FORMAT_DV:
		if (buflen < 9) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming"
			       "interface %d FORMAT error\n",
			       dev->udev->devnum,
			       alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		switch (buffer[8] & 0x7f) {
		case 0:
			strlcpy(format->name, "SD-DV", sizeof format->name);
			break;
		case 1:
			strlcpy(format->name, "SDL-DV", sizeof format->name);
			break;
		case 2:
			strlcpy(format->name, "HD-DV", sizeof format->name);
			break;
		default:
			uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming"
			       "interface %d: unknown DV format %u\n",
			       dev->udev->devnum,
			       alts->desc.bInterfaceNumber, buffer[8]);
			return -EINVAL;
		}

		strlcat(format->name, buffer[8] & (1 << 7) ? " 60Hz" : " 50Hz",
			sizeof format->name);

		format->fcc = V4L2_PIX_FMT_DV;
		format->flags = UVC_FMT_FLAG_COMPRESSED | UVC_FMT_FLAG_STREAM;
		format->bpp = 0;
		ftype = 0;

		/* Create a dummy frame descriptor. */
		frame = &format->frame[0];
		memset(&format->frame[0], 0, sizeof format->frame[0]);
		frame->bFrameIntervalType = 1;
		frame->dwDefaultFrameInterval = 1;
		frame->dwFrameInterval = *intervals;
		*(*intervals)++ = 1;
		format->nframes = 1;
		break;

	case VS_FORMAT_MPEG2TS:
	case VS_FORMAT_STREAM_BASED:
		/* Not supported yet. */
	default:
		uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming"
		       "interface %d unsupported format %u\n",
		       dev->udev->devnum, alts->desc.bInterfaceNumber,
		       buffer[2]);
		return -EINVAL;
	}

	uvc_trace(UVC_TRACE_DESCR, "Found format %s.\n", format->name);

	buflen -= buffer[0];
	buffer += buffer[0];

	/* Count the number of frame descriptors to test the bFrameIndex
	 * field when parsing the descriptors. We can't rely on the
	 * bNumFrameDescriptors field as some cameras don't initialize it
	 * properly.
	 */
	for (_buflen = buflen, _buffer = buffer;
	     _buflen > 2 && _buffer[2] == ftype;
	     _buflen -= _buffer[0], _buffer += _buffer[0])
		format->nframes++;

	/* Parse the frame descriptors. Only uncompressed, MJPEG and frame
	 * based formats have frame descriptors.
	 */
	while (buflen > 2 && buffer[2] == ftype) {
		if (ftype != VS_FRAME_FRAME_BASED)
			n = buflen > 25 ? buffer[25] : 0;
		else
			n = buflen > 21 ? buffer[21] : 0;

		n = n ? n : 3;

		if (buflen < 26 + 4*n) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming"
			       "interface %d FRAME error\n", dev->udev->devnum,
			       alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		if (buffer[3] - 1 >= format->nframes) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming"
			       "interface %d frame index %u out of range\n",
			       dev->udev->devnum, alts->desc.bInterfaceNumber,
			       buffer[3]);
			return -EINVAL;
		}

		frame = &format->frame[buffer[3] - 1];

		frame->bFrameIndex = buffer[3];
		frame->bmCapabilities = buffer[4];
		frame->wWidth = get_unaligned_le16(&buffer[5]);
		frame->wHeight = get_unaligned_le16(&buffer[7]);
		frame->dwMinBitRate = get_unaligned_le32(&buffer[9]);
		frame->dwMaxBitRate = get_unaligned_le32(&buffer[13]);
		if (ftype != VS_FRAME_FRAME_BASED) {
			frame->dwMaxVideoFrameBufferSize =
				get_unaligned_le32(&buffer[17]);
			frame->dwDefaultFrameInterval =
				get_unaligned_le32(&buffer[21]);
			frame->bFrameIntervalType = buffer[25];
		} else {
			frame->dwMaxVideoFrameBufferSize = 0;
			frame->dwDefaultFrameInterval =
				get_unaligned_le32(&buffer[17]);
			frame->bFrameIntervalType = buffer[21];
		}
		frame->dwFrameInterval = *intervals;

		/* Several UVC chipsets screw up dwMaxVideoFrameBufferSize
		 * completely. Observed behaviours range from setting the
		 * value to 1.1x the actual frame size to hardwiring the
		 * 16 low bits to 0. This results in a higher than necessary
		 * memory usage as well as a wrong image size information. For
		 * uncompressed formats this can be fixed by computing the
		 * value from the frame size.
		 */
		if (!(format->flags & UVC_FMT_FLAG_COMPRESSED))
			frame->dwMaxVideoFrameBufferSize = format->bpp
				* frame->wWidth * frame->wHeight / 8;

		/* Some bogus devices report dwMinFrameInterval equal to
		 * dwMaxFrameInterval and have dwFrameIntervalStep set to
		 * zero. Setting all null intervals to 1 fixes the problem and
		 * some other divisions by zero that could happen.
		 */
		for (i = 0; i < n; ++i) {
			interval = get_unaligned_le32(&buffer[26+4*i]);
			*(*intervals)++ = interval ? interval : 1;
		}

		/* Make sure that the default frame interval stays between
		 * the boundaries.
		 */
		n -= frame->bFrameIntervalType ? 1 : 2;
		frame->dwDefaultFrameInterval =
			min(frame->dwFrameInterval[n],
			    max(frame->dwFrameInterval[0],
				frame->dwDefaultFrameInterval));

		uvc_trace(UVC_TRACE_DESCR, "- %ux%u (%u.%u fps)\n",
			frame->wWidth, frame->wHeight,
			10000000/frame->dwDefaultFrameInterval,
			(100000000/frame->dwDefaultFrameInterval)%10);

		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen > 2 && buffer[2] == VS_STILL_IMAGE_FRAME) {
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen > 2 && buffer[2] == VS_COLORFORMAT) {
		if (buflen < 6) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming"
			       "interface %d COLORFORMAT error\n",
			       dev->udev->devnum,
			       alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		format->colorspace = uvc_colorspace(buffer[3]);

		buflen -= buffer[0];
		buffer += buffer[0];
	}

	return buffer - start;
}

static int uvc_parse_streaming(struct uvc_device *dev,
	struct usb_interface *intf)
{
	struct uvc_streaming *streaming = NULL;
	struct uvc_format *format;
	struct uvc_frame *frame;
	struct usb_host_interface *alts = &intf->altsetting[0];
	unsigned char *_buffer, *buffer = alts->extra;
	int _buflen, buflen = alts->extralen;
	unsigned int nformats = 0, nframes = 0, nintervals = 0;
	unsigned int size, i, n, p;
	__u32 *interval;
	__u16 psize;
	int ret = -EINVAL;

	if (intf->cur_altsetting->desc.bInterfaceSubClass
		!= SC_VIDEOSTREAMING) {
		uvc_trace(UVC_TRACE_DESCR, "device %d interface %d isn't a "
			"video streaming interface\n", dev->udev->devnum,
			intf->altsetting[0].desc.bInterfaceNumber);
		return -EINVAL;
	}

	if (usb_driver_claim_interface(&uvc_driver.driver, intf, dev)) {
		uvc_trace(UVC_TRACE_DESCR, "device %d interface %d is already "
			"claimed\n", dev->udev->devnum,
			intf->altsetting[0].desc.bInterfaceNumber);
		return -EINVAL;
	}

	streaming = kzalloc(sizeof *streaming, GFP_KERNEL);
	if (streaming == NULL) {
		usb_driver_release_interface(&uvc_driver.driver, intf);
		return -EINVAL;
	}

	mutex_init(&streaming->mutex);
	streaming->intf = usb_get_intf(intf);
	streaming->intfnum = intf->cur_altsetting->desc.bInterfaceNumber;

	/* The Pico iMage webcam has its class-specific interface descriptors
	 * after the endpoint descriptors.
	 */
	if (buflen == 0) {
		for (i = 0; i < alts->desc.bNumEndpoints; ++i) {
			struct usb_host_endpoint *ep = &alts->endpoint[i];

			if (ep->extralen == 0)
				continue;

			if (ep->extralen > 2 &&
			    ep->extra[1] == USB_DT_CS_INTERFACE) {
				uvc_trace(UVC_TRACE_DESCR, "trying extra data "
					"from endpoint %u.\n", i);
				buffer = alts->endpoint[i].extra;
				buflen = alts->endpoint[i].extralen;
				break;
			}
		}
	}

	/* Skip the standard interface descriptors. */
	while (buflen > 2 && buffer[1] != USB_DT_CS_INTERFACE) {
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen <= 2) {
		uvc_trace(UVC_TRACE_DESCR, "no class-specific streaming "
			"interface descriptors found.\n");
		goto error;
	}

	/* Parse the header descriptor. */
	switch (buffer[2]) {
	case VS_OUTPUT_HEADER:
		streaming->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		size = 9;
		break;

	case VS_INPUT_HEADER:
		streaming->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		size = 13;
		break;

	default:
		uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming interface "
			"%d HEADER descriptor not found.\n", dev->udev->devnum,
			alts->desc.bInterfaceNumber);
		goto error;
	}

	p = buflen >= 4 ? buffer[3] : 0;
	n = buflen >= size ? buffer[size-1] : 0;

	if (buflen < size + p*n) {
		uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming "
			"interface %d HEADER descriptor is invalid.\n",
			dev->udev->devnum, alts->desc.bInterfaceNumber);
		goto error;
	}

	streaming->header.bNumFormats = p;
	streaming->header.bEndpointAddress = buffer[6];
	if (buffer[2] == VS_INPUT_HEADER) {
		streaming->header.bmInfo = buffer[7];
		streaming->header.bTerminalLink = buffer[8];
		streaming->header.bStillCaptureMethod = buffer[9];
		streaming->header.bTriggerSupport = buffer[10];
		streaming->header.bTriggerUsage = buffer[11];
	} else {
		streaming->header.bTerminalLink = buffer[7];
	}
	streaming->header.bControlSize = n;

	streaming->header.bmaControls = kmalloc(p*n, GFP_KERNEL);
	if (streaming->header.bmaControls == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	memcpy(streaming->header.bmaControls, &buffer[size], p*n);

	buflen -= buffer[0];
	buffer += buffer[0];

	_buffer = buffer;
	_buflen = buflen;

	/* Count the format and frame descriptors. */
	while (_buflen > 2) {
		switch (_buffer[2]) {
		case VS_FORMAT_UNCOMPRESSED:
		case VS_FORMAT_MJPEG:
		case VS_FORMAT_FRAME_BASED:
			nformats++;
			break;

		case VS_FORMAT_DV:
			/* DV format has no frame descriptor. We will create a
			 * dummy frame descriptor with a dummy frame interval.
			 */
			nformats++;
			nframes++;
			nintervals++;
			break;

		case VS_FORMAT_MPEG2TS:
		case VS_FORMAT_STREAM_BASED:
			uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming "
				"interface %d FORMAT %u is not supported.\n",
				dev->udev->devnum,
				alts->desc.bInterfaceNumber, _buffer[2]);
			break;

		case VS_FRAME_UNCOMPRESSED:
		case VS_FRAME_MJPEG:
			nframes++;
			if (_buflen > 25)
				nintervals += _buffer[25] ? _buffer[25] : 3;
			break;

		case VS_FRAME_FRAME_BASED:
			nframes++;
			if (_buflen > 21)
				nintervals += _buffer[21] ? _buffer[21] : 3;
			break;
		}

		_buflen -= _buffer[0];
		_buffer += _buffer[0];
	}

	if (nformats == 0) {
		uvc_trace(UVC_TRACE_DESCR, "device %d videostreaming interface "
			"%d has no supported formats defined.\n",
			dev->udev->devnum, alts->desc.bInterfaceNumber);
		goto error;
	}

	size = nformats * sizeof *format + nframes * sizeof *frame
	     + nintervals * sizeof *interval;
	format = kzalloc(size, GFP_KERNEL);
	if (format == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	frame = (struct uvc_frame *)&format[nformats];
	interval = (__u32 *)&frame[nframes];

	streaming->format = format;
	streaming->nformats = nformats;

	/* Parse the format descriptors. */
	while (buflen > 2) {
		switch (buffer[2]) {
		case VS_FORMAT_UNCOMPRESSED:
		case VS_FORMAT_MJPEG:
		case VS_FORMAT_DV:
		case VS_FORMAT_FRAME_BASED:
			format->frame = frame;
			ret = uvc_parse_format(dev, streaming, format,
				&interval, buffer, buflen);
			if (ret < 0)
				goto error;

			frame += format->nframes;
			format++;

			buflen -= ret;
			buffer += ret;
			continue;

		default:
			break;
		}

		buflen -= buffer[0];
		buffer += buffer[0];
	}

	/* Parse the alternate settings to find the maximum bandwidth. */
	for (i = 0; i < intf->num_altsetting; ++i) {
		struct usb_host_endpoint *ep;
		alts = &intf->altsetting[i];
		ep = uvc_find_endpoint(alts,
				streaming->header.bEndpointAddress);
		if (ep == NULL)
			continue;

		psize = le16_to_cpu(ep->desc.wMaxPacketSize);
		psize = (psize & 0x07ff) * (1 + ((psize >> 11) & 3));
		if (psize > streaming->maxpsize)
			streaming->maxpsize = psize;
	}

	list_add_tail(&streaming->list, &dev->streaming);
	return 0;

error:
	usb_driver_release_interface(&uvc_driver.driver, intf);
	usb_put_intf(intf);
	kfree(streaming->format);
	kfree(streaming->header.bmaControls);
	kfree(streaming);
	return ret;
}

/* Parse vendor-specific extensions. */
static int uvc_parse_vendor_control(struct uvc_device *dev,
	const unsigned char *buffer, int buflen)
{
	struct usb_device *udev = dev->udev;
	struct usb_host_interface *alts = dev->intf->cur_altsetting;
	struct uvc_entity *unit;
	unsigned int n, p;
	int handled = 0;

	switch (le16_to_cpu(dev->udev->descriptor.idVendor)) {
	case 0x046d:		/* Logitech */
		if (buffer[1] != 0x41 || buffer[2] != 0x01)
			break;

		/* Logitech implements several vendor specific functions
		 * through vendor specific extension units (LXU).
		 *
		 * The LXU descriptors are similar to XU descriptors
		 * (see "USB Device Video Class for Video Devices", section
		 * 3.7.2.6 "Extension Unit Descriptor") with the following
		 * differences:
		 *
		 * ----------------------------------------------------------
		 * 0		bLength		1	 Number
		 *	Size of this descriptor, in bytes: 24+p+n*2
		 * ----------------------------------------------------------
		 * 23+p+n	bmControlsType	N	Bitmap
		 * 	Individual bits in the set are defined:
		 * 	0: Absolute
		 * 	1: Relative
		 *
		 * 	This bitset is mapped exactly the same as bmControls.
		 * ----------------------------------------------------------
		 * 23+p+n*2	bReserved	1	Boolean
		 * ----------------------------------------------------------
		 * 24+p+n*2	iExtension	1	Index
		 *	Index of a string descriptor that describes this
		 *	extension unit.
		 * ----------------------------------------------------------
		 */
		p = buflen >= 22 ? buffer[21] : 0;
		n = buflen >= 25 + p ? buffer[22+p] : 0;

		if (buflen < 25 + p + 2*n) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d EXTENSION_UNIT error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			break;
		}

		unit = kzalloc(sizeof *unit + p + 2*n, GFP_KERNEL);
		if (unit == NULL)
			return -ENOMEM;

		unit->id = buffer[3];
		unit->type = VC_EXTENSION_UNIT;
		memcpy(unit->extension.guidExtensionCode, &buffer[4], 16);
		unit->extension.bNumControls = buffer[20];
		unit->extension.bNrInPins = get_unaligned_le16(&buffer[21]);
		unit->extension.baSourceID = (__u8 *)unit + sizeof *unit;
		memcpy(unit->extension.baSourceID, &buffer[22], p);
		unit->extension.bControlSize = buffer[22+p];
		unit->extension.bmControls = (__u8 *)unit + sizeof *unit + p;
		unit->extension.bmControlsType = (__u8 *)unit + sizeof *unit
					       + p + n;
		memcpy(unit->extension.bmControls, &buffer[23+p], 2*n);

		if (buffer[24+p+2*n] != 0)
			usb_string(udev, buffer[24+p+2*n], unit->name,
				   sizeof unit->name);
		else
			sprintf(unit->name, "Extension %u", buffer[3]);

		list_add_tail(&unit->list, &dev->entities);
		handled = 1;
		break;
	}

	return handled;
}

static int uvc_parse_standard_control(struct uvc_device *dev,
	const unsigned char *buffer, int buflen)
{
	struct usb_device *udev = dev->udev;
	struct uvc_entity *unit, *term;
	struct usb_interface *intf;
	struct usb_host_interface *alts = dev->intf->cur_altsetting;
	unsigned int i, n, p, len;
	__u16 type;

	switch (buffer[2]) {
	case VC_HEADER:
		n = buflen >= 12 ? buffer[11] : 0;

		if (buflen < 12 || buflen < 12 + n) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d HEADER error\n", udev->devnum,
				alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		dev->uvc_version = get_unaligned_le16(&buffer[3]);
		dev->clock_frequency = get_unaligned_le32(&buffer[7]);

		/* Parse all USB Video Streaming interfaces. */
		for (i = 0; i < n; ++i) {
			intf = usb_ifnum_to_if(udev, buffer[12+i]);
			if (intf == NULL) {
				uvc_trace(UVC_TRACE_DESCR, "device %d "
					"interface %d doesn't exists\n",
					udev->devnum, i);
				continue;
			}

			uvc_parse_streaming(dev, intf);
		}
		break;

	case VC_INPUT_TERMINAL:
		if (buflen < 8) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d INPUT_TERMINAL error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/* Make sure the terminal type MSB is not null, otherwise it
		 * could be confused with a unit.
		 */
		type = get_unaligned_le16(&buffer[4]);
		if ((type & 0xff00) == 0) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d INPUT_TERMINAL %d has invalid "
				"type 0x%04x, skipping\n", udev->devnum,
				alts->desc.bInterfaceNumber,
				buffer[3], type);
			return 0;
		}

		n = 0;
		p = 0;
		len = 8;

		if (type == ITT_CAMERA) {
			n = buflen >= 15 ? buffer[14] : 0;
			len = 15;

		} else if (type == ITT_MEDIA_TRANSPORT_INPUT) {
			n = buflen >= 9 ? buffer[8] : 0;
			p = buflen >= 10 + n ? buffer[9+n] : 0;
			len = 10;
		}

		if (buflen < len + n + p) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d INPUT_TERMINAL error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		term = kzalloc(sizeof *term + n + p, GFP_KERNEL);
		if (term == NULL)
			return -ENOMEM;

		term->id = buffer[3];
		term->type = type | UVC_TERM_INPUT;

		if (UVC_ENTITY_TYPE(term) == ITT_CAMERA) {
			term->camera.bControlSize = n;
			term->camera.bmControls = (__u8 *)term + sizeof *term;
			term->camera.wObjectiveFocalLengthMin =
				get_unaligned_le16(&buffer[8]);
			term->camera.wObjectiveFocalLengthMax =
				get_unaligned_le16(&buffer[10]);
			term->camera.wOcularFocalLength =
				get_unaligned_le16(&buffer[12]);
			memcpy(term->camera.bmControls, &buffer[15], n);
		} else if (UVC_ENTITY_TYPE(term) == ITT_MEDIA_TRANSPORT_INPUT) {
			term->media.bControlSize = n;
			term->media.bmControls = (__u8 *)term + sizeof *term;
			term->media.bTransportModeSize = p;
			term->media.bmTransportModes = (__u8 *)term
						     + sizeof *term + n;
			memcpy(term->media.bmControls, &buffer[9], n);
			memcpy(term->media.bmTransportModes, &buffer[10+n], p);
		}

		if (buffer[7] != 0)
			usb_string(udev, buffer[7], term->name,
				   sizeof term->name);
		else if (UVC_ENTITY_TYPE(term) == ITT_CAMERA)
			sprintf(term->name, "Camera %u", buffer[3]);
		else if (UVC_ENTITY_TYPE(term) == ITT_MEDIA_TRANSPORT_INPUT)
			sprintf(term->name, "Media %u", buffer[3]);
		else
			sprintf(term->name, "Input %u", buffer[3]);

		list_add_tail(&term->list, &dev->entities);
		break;

	case VC_OUTPUT_TERMINAL:
		if (buflen < 9) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d OUTPUT_TERMINAL error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/* Make sure the terminal type MSB is not null, otherwise it
		 * could be confused with a unit.
		 */
		type = get_unaligned_le16(&buffer[4]);
		if ((type & 0xff00) == 0) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d OUTPUT_TERMINAL %d has invalid "
				"type 0x%04x, skipping\n", udev->devnum,
				alts->desc.bInterfaceNumber, buffer[3], type);
			return 0;
		}

		term = kzalloc(sizeof *term, GFP_KERNEL);
		if (term == NULL)
			return -ENOMEM;

		term->id = buffer[3];
		term->type = type | UVC_TERM_OUTPUT;
		term->output.bSourceID = buffer[7];

		if (buffer[8] != 0)
			usb_string(udev, buffer[8], term->name,
				   sizeof term->name);
		else
			sprintf(term->name, "Output %u", buffer[3]);

		list_add_tail(&term->list, &dev->entities);
		break;

	case VC_SELECTOR_UNIT:
		p = buflen >= 5 ? buffer[4] : 0;

		if (buflen < 5 || buflen < 6 + p) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d SELECTOR_UNIT error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = kzalloc(sizeof *unit + p, GFP_KERNEL);
		if (unit == NULL)
			return -ENOMEM;

		unit->id = buffer[3];
		unit->type = buffer[2];
		unit->selector.bNrInPins = buffer[4];
		unit->selector.baSourceID = (__u8 *)unit + sizeof *unit;
		memcpy(unit->selector.baSourceID, &buffer[5], p);

		if (buffer[5+p] != 0)
			usb_string(udev, buffer[5+p], unit->name,
				   sizeof unit->name);
		else
			sprintf(unit->name, "Selector %u", buffer[3]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	case VC_PROCESSING_UNIT:
		n = buflen >= 8 ? buffer[7] : 0;
		p = dev->uvc_version >= 0x0110 ? 10 : 9;

		if (buflen < p + n) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d PROCESSING_UNIT error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = kzalloc(sizeof *unit + n, GFP_KERNEL);
		if (unit == NULL)
			return -ENOMEM;

		unit->id = buffer[3];
		unit->type = buffer[2];
		unit->processing.bSourceID = buffer[4];
		unit->processing.wMaxMultiplier =
			get_unaligned_le16(&buffer[5]);
		unit->processing.bControlSize = buffer[7];
		unit->processing.bmControls = (__u8 *)unit + sizeof *unit;
		memcpy(unit->processing.bmControls, &buffer[8], n);
		if (dev->uvc_version >= 0x0110)
			unit->processing.bmVideoStandards = buffer[9+n];

		if (buffer[8+n] != 0)
			usb_string(udev, buffer[8+n], unit->name,
				   sizeof unit->name);
		else
			sprintf(unit->name, "Processing %u", buffer[3]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	case VC_EXTENSION_UNIT:
		p = buflen >= 22 ? buffer[21] : 0;
		n = buflen >= 24 + p ? buffer[22+p] : 0;

		if (buflen < 24 + p + n) {
			uvc_trace(UVC_TRACE_DESCR, "device %d videocontrol "
				"interface %d EXTENSION_UNIT error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = kzalloc(sizeof *unit + p + n, GFP_KERNEL);
		if (unit == NULL)
			return -ENOMEM;

		unit->id = buffer[3];
		unit->type = buffer[2];
		memcpy(unit->extension.guidExtensionCode, &buffer[4], 16);
		unit->extension.bNumControls = buffer[20];
		unit->extension.bNrInPins = get_unaligned_le16(&buffer[21]);
		unit->extension.baSourceID = (__u8 *)unit + sizeof *unit;
		memcpy(unit->extension.baSourceID, &buffer[22], p);
		unit->extension.bControlSize = buffer[22+p];
		unit->extension.bmControls = (__u8 *)unit + sizeof *unit + p;
		memcpy(unit->extension.bmControls, &buffer[23+p], n);

		if (buffer[23+p+n] != 0)
			usb_string(udev, buffer[23+p+n], unit->name,
				   sizeof unit->name);
		else
			sprintf(unit->name, "Extension %u", buffer[3]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	default:
		uvc_trace(UVC_TRACE_DESCR, "Found an unknown CS_INTERFACE "
			"descriptor (%u)\n", buffer[2]);
		break;
	}

	return 0;
}

static int uvc_parse_control(struct uvc_device *dev)
{
	struct usb_host_interface *alts = dev->intf->cur_altsetting;
	unsigned char *buffer = alts->extra;
	int buflen = alts->extralen;
	int ret;

	/* Parse the default alternate setting only, as the UVC specification
	 * defines a single alternate setting, the default alternate setting
	 * zero.
	 */

	while (buflen > 2) {
		if (uvc_parse_vendor_control(dev, buffer, buflen) ||
		    buffer[1] != USB_DT_CS_INTERFACE)
			goto next_descriptor;

		if ((ret = uvc_parse_standard_control(dev, buffer, buflen)) < 0)
			return ret;

next_descriptor:
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	/* Check if the optional status endpoint is present. Built-in iSight
	 * webcams have an interrupt endpoint but spit proprietary data that
	 * don't conform to the UVC status endpoint messages. Don't try to
	 * handle the interrupt endpoint for those cameras.
	 */
	if (alts->desc.bNumEndpoints == 1 &&
	    !(dev->quirks & UVC_QUIRK_BUILTIN_ISIGHT)) {
		struct usb_host_endpoint *ep = &alts->endpoint[0];
		struct usb_endpoint_descriptor *desc = &ep->desc;

		if (usb_endpoint_is_int_in(desc) &&
		    le16_to_cpu(desc->wMaxPacketSize) >= 8 &&
		    desc->bInterval != 0) {
			uvc_trace(UVC_TRACE_DESCR, "Found a Status endpoint "
				"(addr %02x).\n", desc->bEndpointAddress);
			dev->int_ep = ep;
		}
	}

	return 0;
}

/* ------------------------------------------------------------------------
 * USB probe and disconnect
 */

/*
 * Unregister the video devices.
 */
static void uvc_unregister_video(struct uvc_device *dev)
{
	if (dev->video.vdev) {
		if (dev->video.vdev->minor == -1)
			video_device_release(dev->video.vdev);
		else
			video_unregister_device(dev->video.vdev);
		dev->video.vdev = NULL;
	}
}

/*
 * Scan the UVC descriptors to locate a chain starting at an Output Terminal
 * and containing the following units:
 *
 * - one Output Terminal (USB Streaming or Display)
 * - zero or one Processing Unit
 * - zero, one or mode single-input Selector Units
 * - zero or one multiple-input Selector Units, provided all inputs are
 *   connected to input terminals
 * - zero, one or mode single-input Extension Units
 * - one or more Input Terminals (Camera, External or USB Streaming)
 *
 * A side forward scan is made on each detected entity to check for additional
 * extension units.
 */
static int uvc_scan_chain_entity(struct uvc_video_device *video,
	struct uvc_entity *entity)
{
	switch (UVC_ENTITY_TYPE(entity)) {
	case VC_EXTENSION_UNIT:
		if (uvc_trace_param & UVC_TRACE_PROBE)
			printk(" <- XU %d", entity->id);

		if (entity->extension.bNrInPins != 1) {
			uvc_trace(UVC_TRACE_DESCR, "Extension unit %d has more "
				"than 1 input pin.\n", entity->id);
			return -1;
		}

		list_add_tail(&entity->chain, &video->extensions);
		break;

	case VC_PROCESSING_UNIT:
		if (uvc_trace_param & UVC_TRACE_PROBE)
			printk(" <- PU %d", entity->id);

		if (video->processing != NULL) {
			uvc_trace(UVC_TRACE_DESCR, "Found multiple "
				"Processing Units in chain.\n");
			return -1;
		}

		video->processing = entity;
		break;

	case VC_SELECTOR_UNIT:
		if (uvc_trace_param & UVC_TRACE_PROBE)
			printk(" <- SU %d", entity->id);

		/* Single-input selector units are ignored. */
		if (entity->selector.bNrInPins == 1)
			break;

		if (video->selector != NULL) {
			uvc_trace(UVC_TRACE_DESCR, "Found multiple Selector "
				"Units in chain.\n");
			return -1;
		}

		video->selector = entity;
		break;

	case ITT_VENDOR_SPECIFIC:
	case ITT_CAMERA:
	case ITT_MEDIA_TRANSPORT_INPUT:
		if (uvc_trace_param & UVC_TRACE_PROBE)
			printk(" <- IT %d\n", entity->id);

		list_add_tail(&entity->chain, &video->iterms);
		break;

	case TT_STREAMING:
		if (uvc_trace_param & UVC_TRACE_PROBE)
			printk(" <- IT %d\n", entity->id);

		if (!UVC_ENTITY_IS_ITERM(entity)) {
			uvc_trace(UVC_TRACE_DESCR, "Unsupported input "
				"terminal %u.\n", entity->id);
			return -1;
		}

		if (video->sterm != NULL) {
			uvc_trace(UVC_TRACE_DESCR, "Found multiple streaming "
				"entities in chain.\n");
			return -1;
		}

		list_add_tail(&entity->chain, &video->iterms);
		video->sterm = entity;
		break;

	default:
		uvc_trace(UVC_TRACE_DESCR, "Unsupported entity type "
			"0x%04x found in chain.\n", UVC_ENTITY_TYPE(entity));
		return -1;
	}

	return 0;
}

static int uvc_scan_chain_forward(struct uvc_video_device *video,
	struct uvc_entity *entity, struct uvc_entity *prev)
{
	struct uvc_entity *forward;
	int found;

	/* Forward scan */
	forward = NULL;
	found = 0;

	while (1) {
		forward = uvc_entity_by_reference(video->dev, entity->id,
			forward);
		if (forward == NULL)
			break;

		if (UVC_ENTITY_TYPE(forward) != VC_EXTENSION_UNIT ||
		    forward == prev)
			continue;

		if (forward->extension.bNrInPins != 1) {
			uvc_trace(UVC_TRACE_DESCR, "Extension unit %d has"
				"more than 1 input pin.\n", entity->id);
			return -1;
		}

		list_add_tail(&forward->chain, &video->extensions);
		if (uvc_trace_param & UVC_TRACE_PROBE) {
			if (!found)
				printk(" (-> XU");

			printk(" %d", forward->id);
			found = 1;
		}
	}
	if (found)
		printk(")");

	return 0;
}

static int uvc_scan_chain_backward(struct uvc_video_device *video,
	struct uvc_entity *entity)
{
	struct uvc_entity *term;
	int id = -1, i;

	switch (UVC_ENTITY_TYPE(entity)) {
	case VC_EXTENSION_UNIT:
		id = entity->extension.baSourceID[0];
		break;

	case VC_PROCESSING_UNIT:
		id = entity->processing.bSourceID;
		break;

	case VC_SELECTOR_UNIT:
		/* Single-input selector units are ignored. */
		if (entity->selector.bNrInPins == 1) {
			id = entity->selector.baSourceID[0];
			break;
		}

		if (uvc_trace_param & UVC_TRACE_PROBE)
			printk(" <- IT");

		video->selector = entity;
		for (i = 0; i < entity->selector.bNrInPins; ++i) {
			id = entity->selector.baSourceID[i];
			term = uvc_entity_by_id(video->dev, id);
			if (term == NULL || !UVC_ENTITY_IS_ITERM(term)) {
				uvc_trace(UVC_TRACE_DESCR, "Selector unit %d "
					"input %d isn't connected to an "
					"input terminal\n", entity->id, i);
				return -1;
			}

			if (uvc_trace_param & UVC_TRACE_PROBE)
				printk(" %d", term->id);

			list_add_tail(&term->chain, &video->iterms);
			uvc_scan_chain_forward(video, term, entity);
		}

		if (uvc_trace_param & UVC_TRACE_PROBE)
			printk("\n");

		id = 0;
		break;
	}

	return id;
}

static int uvc_scan_chain(struct uvc_video_device *video)
{
	struct uvc_entity *entity, *prev;
	int id;

	entity = video->oterm;
	uvc_trace(UVC_TRACE_PROBE, "Scanning UVC chain: OT %d", entity->id);

	if (UVC_ENTITY_TYPE(entity) == TT_STREAMING)
		video->sterm = entity;

	id = entity->output.bSourceID;
	while (id != 0) {
		prev = entity;
		entity = uvc_entity_by_id(video->dev, id);
		if (entity == NULL) {
			uvc_trace(UVC_TRACE_DESCR, "Found reference to "
				"unknown entity %d.\n", id);
			return -1;
		}

		/* Process entity */
		if (uvc_scan_chain_entity(video, entity) < 0)
			return -1;

		/* Forward scan */
		if (uvc_scan_chain_forward(video, entity, prev) < 0)
			return -1;

		/* Stop when a terminal is found. */
		if (!UVC_ENTITY_IS_UNIT(entity))
			break;

		/* Backward scan */
		id = uvc_scan_chain_backward(video, entity);
		if (id < 0)
			return id;
	}

	if (video->sterm == NULL) {
		uvc_trace(UVC_TRACE_DESCR, "No streaming entity found in "
			"chain.\n");
		return -1;
	}

	return 0;
}

/*
 * Register the video devices.
 *
 * The driver currently supports a single video device per control interface
 * only. The terminal and units must match the following structure:
 *
 * ITT_* -> VC_PROCESSING_UNIT -> VC_EXTENSION_UNIT{0,n} -> TT_STREAMING
 * TT_STREAMING -> VC_PROCESSING_UNIT -> VC_EXTENSION_UNIT{0,n} -> OTT_*
 *
 * The Extension Units, if present, must have a single input pin. The
 * Processing Unit and Extension Units can be in any order. Additional
 * Extension Units connected to the main chain as single-unit branches are
 * also supported.
 */
static int uvc_register_video(struct uvc_device *dev)
{
	struct video_device *vdev;
	struct uvc_entity *term;
	int found = 0, ret;

	/* Check if the control interface matches the structure we expect. */
	list_for_each_entry(term, &dev->entities, list) {
		struct uvc_streaming *streaming;

		if (!UVC_ENTITY_IS_TERM(term) || !UVC_ENTITY_IS_OTERM(term))
			continue;

		memset(&dev->video, 0, sizeof dev->video);
		mutex_init(&dev->video.ctrl_mutex);
		INIT_LIST_HEAD(&dev->video.iterms);
		INIT_LIST_HEAD(&dev->video.extensions);
		dev->video.oterm = term;
		dev->video.dev = dev;
		if (uvc_scan_chain(&dev->video) < 0)
			continue;

		list_for_each_entry(streaming, &dev->streaming, list) {
			if (streaming->header.bTerminalLink ==
			    dev->video.sterm->id) {
				dev->video.streaming = streaming;
				found = 1;
				break;
			}
		}

		if (found)
			break;
	}

	if (!found) {
		uvc_printk(KERN_INFO, "No valid video chain found.\n");
		return -1;
	}

	if (uvc_trace_param & UVC_TRACE_PROBE) {
		uvc_printk(KERN_INFO, "Found a valid video chain (");
		list_for_each_entry(term, &dev->video.iterms, chain) {
			printk("%d", term->id);
			if (term->chain.next != &dev->video.iterms)
				printk(",");
		}
		printk(" -> %d).\n", dev->video.oterm->id);
	}

	/* Initialize the video buffers queue. */
	uvc_queue_init(&dev->video.queue, dev->video.streaming->type);

	/* Initialize the streaming interface with default streaming
	 * parameters.
	 */
	if ((ret = uvc_video_init(&dev->video)) < 0) {
		uvc_printk(KERN_ERR, "Failed to initialize the device "
			"(%d).\n", ret);
		return ret;
	}

	/* Register the device with V4L. */
	vdev = video_device_alloc();
	if (vdev == NULL)
		return -1;

	/* We already hold a reference to dev->udev. The video device will be
	 * unregistered before the reference is released, so we don't need to
	 * get another one.
	 */
	vdev->parent = &dev->intf->dev;
	vdev->minor = -1;
	vdev->fops = &uvc_fops;
	vdev->release = video_device_release;
	strlcpy(vdev->name, dev->name, sizeof vdev->name);

	/* Set the driver data before calling video_register_device, otherwise
	 * uvc_v4l2_open might race us.
	 */
	dev->video.vdev = vdev;
	video_set_drvdata(vdev, &dev->video);

	if (video_register_device(vdev, VFL_TYPE_GRABBER, -1) < 0) {
		dev->video.vdev = NULL;
		video_device_release(vdev);
		return -1;
	}

	return 0;
}

/*
 * Delete the UVC device.
 *
 * Called by the kernel when the last reference to the uvc_device structure
 * is released.
 *
 * Unregistering the video devices is done here because every opened instance
 * must be closed before the device can be unregistered. An alternative would
 * have been to use another reference count for uvc_v4l2_open/uvc_release, and
 * unregister the video devices on disconnect when that reference count drops
 * to zero.
 *
 * As this function is called after or during disconnect(), all URBs have
 * already been canceled by the USB core. There is no need to kill the
 * interrupt URB manually.
 */
void uvc_delete(struct kref *kref)
{
	struct uvc_device *dev = container_of(kref, struct uvc_device, kref);
	struct list_head *p, *n;

	/* Unregister the video device. */
	uvc_unregister_video(dev);
	usb_put_intf(dev->intf);
	usb_put_dev(dev->udev);

	uvc_status_cleanup(dev);
	uvc_ctrl_cleanup_device(dev);

	list_for_each_safe(p, n, &dev->entities) {
		struct uvc_entity *entity;
		entity = list_entry(p, struct uvc_entity, list);
		kfree(entity);
	}

	list_for_each_safe(p, n, &dev->streaming) {
		struct uvc_streaming *streaming;
		streaming = list_entry(p, struct uvc_streaming, list);
		usb_driver_release_interface(&uvc_driver.driver,
			streaming->intf);
		usb_put_intf(streaming->intf);
		kfree(streaming->format);
		kfree(streaming->header.bmaControls);
		kfree(streaming);
	}

	kfree(dev);
}

static int uvc_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct uvc_device *dev;
	int ret;

	if (id->idVendor && id->idProduct)
		uvc_trace(UVC_TRACE_PROBE, "Probing known UVC device %s "
				"(%04x:%04x)\n", udev->devpath, id->idVendor,
				id->idProduct);
	else
		uvc_trace(UVC_TRACE_PROBE, "Probing generic UVC device %s\n",
				udev->devpath);

	/* Allocate memory for the device and initialize it. */
	if ((dev = kzalloc(sizeof *dev, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&dev->entities);
	INIT_LIST_HEAD(&dev->streaming);
	kref_init(&dev->kref);

	dev->udev = usb_get_dev(udev);
	dev->intf = usb_get_intf(intf);
	dev->intfnum = intf->cur_altsetting->desc.bInterfaceNumber;
	dev->quirks = id->driver_info | uvc_quirks_param;

	if (udev->product != NULL)
		strlcpy(dev->name, udev->product, sizeof dev->name);
	else
		snprintf(dev->name, sizeof dev->name,
			"UVC Camera (%04x:%04x)",
			le16_to_cpu(udev->descriptor.idVendor),
			le16_to_cpu(udev->descriptor.idProduct));

	/* Parse the Video Class control descriptor. */
	if (uvc_parse_control(dev) < 0) {
		uvc_trace(UVC_TRACE_PROBE, "Unable to parse UVC "
			"descriptors.\n");
		goto error;
	}

	uvc_printk(KERN_INFO, "Found UVC %u.%02x device %s (%04x:%04x)\n",
		dev->uvc_version >> 8, dev->uvc_version & 0xff,
		udev->product ? udev->product : "<unnamed>",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct));

	if (uvc_quirks_param != 0) {
		uvc_printk(KERN_INFO, "Forcing device quirks 0x%x by module "
			"parameter for testing purpose.\n", uvc_quirks_param);
		uvc_printk(KERN_INFO, "Please report required quirks to the "
			"linux-uvc-devel mailing list.\n");
	}

	/* Initialize controls. */
	if (uvc_ctrl_init_device(dev) < 0)
		goto error;

	/* Register the video devices. */
	if (uvc_register_video(dev) < 0)
		goto error;

	/* Save our data pointer in the interface data. */
	usb_set_intfdata(intf, dev);

	/* Initialize the interrupt URB. */
	if ((ret = uvc_status_init(dev)) < 0) {
		uvc_printk(KERN_INFO, "Unable to initialize the status "
			"endpoint (%d), status interrupt will not be "
			"supported.\n", ret);
	}

	uvc_trace(UVC_TRACE_PROBE, "UVC device initialized.\n");
	return 0;

error:
	kref_put(&dev->kref, uvc_delete);
	return -ENODEV;
}

static void uvc_disconnect(struct usb_interface *intf)
{
	struct uvc_device *dev = usb_get_intfdata(intf);

	/* Set the USB interface data to NULL. This can be done outside the
	 * lock, as there's no other reader.
	 */
	usb_set_intfdata(intf, NULL);

	if (intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOSTREAMING)
		return;

	/* uvc_v4l2_open() might race uvc_disconnect(). A static driver-wide
	 * lock is needed to prevent uvc_disconnect from releasing its
	 * reference to the uvc_device instance after uvc_v4l2_open() received
	 * the pointer to the device (video_devdata) but before it got the
	 * chance to increase the reference count (kref_get).
	 *
	 * Note that the reference can't be released with the lock held,
	 * otherwise a AB-BA deadlock can occur with videodev_lock that
	 * videodev acquires in videodev_open() and video_unregister_device().
	 */
	mutex_lock(&uvc_driver.open_mutex);
	dev->state |= UVC_DEV_DISCONNECTED;
	mutex_unlock(&uvc_driver.open_mutex);

	kref_put(&dev->kref, uvc_delete);
}

static int uvc_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct uvc_device *dev = usb_get_intfdata(intf);

	uvc_trace(UVC_TRACE_SUSPEND, "Suspending interface %u\n",
		intf->cur_altsetting->desc.bInterfaceNumber);

	/* Controls are cached on the fly so they don't need to be saved. */
	if (intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOCONTROL)
		return uvc_status_suspend(dev);

	if (dev->video.streaming->intf != intf) {
		uvc_trace(UVC_TRACE_SUSPEND, "Suspend: video streaming USB "
				"interface mismatch.\n");
		return -EINVAL;
	}

	return uvc_video_suspend(&dev->video);
}

static int __uvc_resume(struct usb_interface *intf, int reset)
{
	struct uvc_device *dev = usb_get_intfdata(intf);

	uvc_trace(UVC_TRACE_SUSPEND, "Resuming interface %u\n",
		intf->cur_altsetting->desc.bInterfaceNumber);

	if (intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOCONTROL) {
		if (reset) {
			int ret = uvc_ctrl_resume_device(dev);

			if (ret < 0)
				return ret;
		}

		return uvc_status_resume(dev);
	}

	if (dev->video.streaming->intf != intf) {
		uvc_trace(UVC_TRACE_SUSPEND, "Resume: video streaming USB "
				"interface mismatch.\n");
		return -EINVAL;
	}

	return uvc_video_resume(&dev->video);
}

static int uvc_resume(struct usb_interface *intf)
{
	return __uvc_resume(intf, 0);
}

static int uvc_reset_resume(struct usb_interface *intf)
{
	return __uvc_resume(intf, 1);
}

/* ------------------------------------------------------------------------
 * Driver initialization and cleanup
 */

/*
 * The Logitech cameras listed below have their interface class set to
 * VENDOR_SPEC because they don't announce themselves as UVC devices, even
 * though they are compliant.
 */
static struct usb_device_id uvc_ids[] = {
	/* Microsoft Lifecam NX-6000 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x045e,
	  .idProduct		= 0x00f8,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_PROBE_MINMAX },
	/* Microsoft Lifecam VX-7000 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x045e,
	  .idProduct		= 0x0723,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_PROBE_MINMAX },
	/* Logitech Quickcam Fusion */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c1,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam Orbit MP */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c2,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam Pro for Notebook */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c3,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam Pro 5000 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c5,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam OEM Dell Notebook */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c6,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam OEM Cisco VT Camera II */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c7,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Alcor Micro AU3820 (Future Boy PC USB Webcam) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x058f,
	  .idProduct		= 0x3820,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_PROBE_MINMAX },
	/* Apple Built-In iSight */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05ac,
	  .idProduct		= 0x8501,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info 		= UVC_QUIRK_PROBE_MINMAX
				| UVC_QUIRK_BUILTIN_ISIGHT },
	/* Genesys Logic USB 2.0 PC Camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05e3,
	  .idProduct		= 0x0505,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_STREAM_NO_FID },
	/* ViMicro */
	{ .match_flags		= USB_DEVICE_ID_MATCH_VENDOR
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0ac8,
	  .idProduct		= 0x0000,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_FIX_BANDWIDTH },
	/* MT6227 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0e8d,
	  .idProduct		= 0x0004,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_PROBE_MINMAX },
	/* Syntek (HP Spartan) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x5212,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_STREAM_NO_FID },
	/* Syntek (Samsung Q310) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x5931,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_STREAM_NO_FID },
	/* Syntek (Asus F9SG) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x8a31,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_STREAM_NO_FID },
	/* Syntek (Asus U3S) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x8a33,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_STREAM_NO_FID },
	/* Syntek (JAOtech Smart Terminal) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x8a34,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_STREAM_NO_FID },
	/* Lenovo Thinkpad SL500 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x17ef,
	  .idProduct		= 0x480b,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_STREAM_NO_FID },
	/* Ecamm Pico iMage */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x18cd,
	  .idProduct		= 0xcafe,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_PROBE_EXTRAFIELDS },
	/* Bodelin ProScopeHR */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_DEV_HI
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x19ab,
	  .idProduct		= 0x1000,
	  .bcdDevice_hi		= 0x0126,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_STATUS_INTERVAL },
	/* SiGma Micro USB Web Camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1c4f,
	  .idProduct		= 0x3000,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_QUIRK_PROBE_MINMAX
				| UVC_QUIRK_IGNORE_SELECTOR_UNIT
				| UVC_QUIRK_PRUNE_CONTROLS },
	/* Generic USB Video Class */
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 1, 0) },
	{}
};

MODULE_DEVICE_TABLE(usb, uvc_ids);

struct uvc_driver uvc_driver = {
	.driver = {
		.name		= "uvcvideo",
		.probe		= uvc_probe,
		.disconnect	= uvc_disconnect,
		.suspend	= uvc_suspend,
		.resume		= uvc_resume,
		.reset_resume	= uvc_reset_resume,
		.id_table	= uvc_ids,
		.supports_autosuspend = 1,
	},
};

static int __init uvc_init(void)
{
	int result;

	INIT_LIST_HEAD(&uvc_driver.devices);
	INIT_LIST_HEAD(&uvc_driver.controls);
	mutex_init(&uvc_driver.open_mutex);
	mutex_init(&uvc_driver.ctrl_mutex);

	uvc_ctrl_init();

	result = usb_register(&uvc_driver.driver);
	if (result == 0)
		printk(KERN_INFO DRIVER_DESC " (" DRIVER_VERSION ")\n");
	return result;
}

static void __exit uvc_cleanup(void)
{
	usb_deregister(&uvc_driver.driver);
}

module_init(uvc_init);
module_exit(uvc_cleanup);

module_param_named(nodrop, uvc_no_drop_param, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(nodrop, "Don't drop incomplete frames");
module_param_named(quirks, uvc_quirks_param, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(quirks, "Forced device quirks");
module_param_named(trace, uvc_trace_param, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(trace, "Trace level bitmask");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

