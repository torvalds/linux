/*
 * Copyright (c) 2013 Lubomir Rintel
 * All rights reserved.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Fushicai USBTV007 Audio-Video Grabber Driver
 *
 * Product web site:
 * http://www.fushicai.com/products_detail/&productId=d05449ee-b690-42f9-a661-aa7353894bed.html
 *
 * Following LWN articles were very useful in construction of this driver:
 * Video4Linux2 API series: http://lwn.net/Articles/203924/
 * videobuf2 API explanation: http://lwn.net/Articles/447435/
 * Thanks go to Jonathan Corbet for providing this quality documentation.
 * He is awesome.
 *
 * No physical hardware was harmed running Windows during the
 * reverse-engineering activity
 */

#include "usbtv.h"

int usbtv_set_regs(struct usbtv *usbtv, const u16 regs[][2], int size)
{
	int ret;
	int pipe = usb_rcvctrlpipe(usbtv->udev, 0);
	int i;

	for (i = 0; i < size; i++) {
		u16 index = regs[i][0];
		u16 value = regs[i][1];

		ret = usb_control_msg(usbtv->udev, pipe, USBTV_REQUEST_REG,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, NULL, 0, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int usbtv_probe(struct usb_interface *intf,
	const struct usb_device_id *id)
{
	int ret;
	int size;
	struct device *dev = &intf->dev;
	struct usbtv *usbtv;
	struct usb_host_endpoint *ep;

	/* Checks that the device is what we think it is. */
	if (intf->num_altsetting != 2)
		return -ENODEV;
	if (intf->altsetting[1].desc.bNumEndpoints != 4)
		return -ENODEV;

	ep = &intf->altsetting[1].endpoint[0];

	/* Packet size is split into 11 bits of base size and count of
	 * extra multiplies of it.*/
	size = usb_endpoint_maxp(&ep->desc);
	size = (size & 0x07ff) * usb_endpoint_maxp_mult(&ep->desc);

	/* Device structure */
	usbtv = kzalloc(sizeof(struct usbtv), GFP_KERNEL);
	if (usbtv == NULL)
		return -ENOMEM;
	usbtv->dev = dev;
	usbtv->udev = usb_get_dev(interface_to_usbdev(intf));

	usbtv->iso_size = size;

	usb_set_intfdata(intf, usbtv);

	ret = usbtv_video_init(usbtv);
	if (ret < 0)
		goto usbtv_video_fail;

	ret = usbtv_audio_init(usbtv);
	if (ret < 0)
		goto usbtv_audio_fail;

	/* for simplicity we exploit the v4l2_device reference counting */
	v4l2_device_get(&usbtv->v4l2_dev);

	dev_info(dev, "Fushicai USBTV007 Audio-Video Grabber\n");
	return 0;

usbtv_audio_fail:
	usbtv_video_free(usbtv);

usbtv_video_fail:
	usb_set_intfdata(intf, NULL);
	usb_put_dev(usbtv->udev);
	kfree(usbtv);

	return ret;
}

static void usbtv_disconnect(struct usb_interface *intf)
{
	struct usbtv *usbtv = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (!usbtv)
		return;

	usbtv_audio_free(usbtv);
	usbtv_video_free(usbtv);

	usb_put_dev(usbtv->udev);
	usbtv->udev = NULL;

	/* the usbtv structure will be deallocated when v4l2 will be
	   done using it */
	v4l2_device_put(&usbtv->v4l2_dev);
}

static const struct usb_device_id usbtv_id_table[] = {
	{ USB_DEVICE(0x1b71, 0x3002) },
	{ USB_DEVICE(0x1f71, 0x3301) },
	{}
};
MODULE_DEVICE_TABLE(usb, usbtv_id_table);

MODULE_AUTHOR("Lubomir Rintel, Federico Simoncelli");
MODULE_DESCRIPTION("Fushicai USBTV007 Audio-Video Grabber Driver");
MODULE_LICENSE("Dual BSD/GPL");

static struct usb_driver usbtv_usb_driver = {
	.name = "usbtv",
	.id_table = usbtv_id_table,
	.probe = usbtv_probe,
	.disconnect = usbtv_disconnect,
};

module_usb_driver(usbtv_usb_driver);
