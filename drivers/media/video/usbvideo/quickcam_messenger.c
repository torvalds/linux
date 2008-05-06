/*
 * Driver for Logitech Quickcam Messenger usb video camera
 * Copyright (C) Jaya Kumar
 *
 * This work was sponsored by CIS(M) Sdn Bhd.
 * History:
 * 05/08/2006 - Jaya Kumar
 * I wrote this based on the konicawc by Simon Evans.
 * -
 * Full credit for reverse engineering and creating an initial
 * working linux driver for the VV6422 goes to the qce-ga project by
 * Tuukka Toivonen, Jochen Hoenicke, Peter McConnell,
 * Cristiano De Michele, Georg Acher, Jean-Frederic Clere as well as
 * others.
 * ---
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/usb/input.h>

#include "usbvideo.h"
#include "quickcam_messenger.h"

/*
 * Version Information
 */

#ifdef CONFIG_USB_DEBUG
static int debug;
#define DEBUG(n, format, arg...) \
	if (n <= debug) {	 \
		printk(KERN_DEBUG __FILE__ ":%s(): " format "\n", __func__ , ## arg); \
	}
#else
#define DEBUG(n, arg...)
static const int debug;
#endif

#define DRIVER_VERSION "v0.01"
#define DRIVER_DESC "Logitech Quickcam Messenger USB"

#define USB_LOGITECH_VENDOR_ID	0x046D
#define USB_QCM_PRODUCT_ID	0x08F0

#define MAX_CAMERAS	1

#define MAX_COLOUR	32768
#define MAX_HUE		32768
#define MAX_BRIGHTNESS	32768
#define MAX_CONTRAST	32768
#define MAX_WHITENESS	32768

static int size = SIZE_320X240;
static int colour = MAX_COLOUR;
static int hue = MAX_HUE;
static int brightness =	MAX_BRIGHTNESS;
static int contrast =	MAX_CONTRAST;
static int whiteness =	MAX_WHITENESS;

static struct usbvideo *cams;

static struct usb_device_id qcm_table [] = {
	{ USB_DEVICE(USB_LOGITECH_VENDOR_ID, USB_QCM_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, qcm_table);

#ifdef CONFIG_INPUT
static void qcm_register_input(struct qcm *cam, struct usb_device *dev)
{
	struct input_dev *input_dev;
	int error;

	usb_make_path(dev, cam->input_physname, sizeof(cam->input_physname));
	strncat(cam->input_physname, "/input0", sizeof(cam->input_physname));

	cam->input = input_dev = input_allocate_device();
	if (!input_dev) {
		warn("insufficient mem for cam input device");
		return;
	}

	input_dev->name = "QCM button";
	input_dev->phys = cam->input_physname;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &dev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	input_dev->keybit[BIT_WORD(BTN_0)] = BIT_MASK(BTN_0);

	error = input_register_device(cam->input);
	if (error) {
		warn("Failed to register camera's input device, err: %d\n",
		     error);
		input_free_device(cam->input);
		cam->input = NULL;
	}
}

static void qcm_unregister_input(struct qcm *cam)
{
	if (cam->input) {
		input_unregister_device(cam->input);
		cam->input = NULL;
	}
}

static void qcm_report_buttonstat(struct qcm *cam)
{
	if (cam->input) {
		input_report_key(cam->input, BTN_0, cam->button_sts);
		input_sync(cam->input);
	}
}

static void qcm_int_irq(struct urb *urb)
{
	int ret;
	struct uvd *uvd = urb->context;
	struct qcm *cam;

	if (!CAMERA_IS_OPERATIONAL(uvd))
		return;

	if (!uvd->streaming)
		return;

	uvd->stats.urb_count++;

	if (urb->status < 0)
		uvd->stats.iso_err_count++;
	else {
		if (urb->actual_length > 0 ) {
			cam = (struct qcm *) uvd->user_data;
			if (cam->button_sts_buf == 0x88)
				cam->button_sts = 0x0;
			else if (cam->button_sts_buf == 0x80)
				cam->button_sts = 0x1;
			qcm_report_buttonstat(cam);
		}
	}

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret < 0)
		err("usb_submit_urb error (%d)", ret);
}

static int qcm_setup_input_int(struct qcm *cam, struct uvd *uvd)
{
	int errflag;
	usb_fill_int_urb(cam->button_urb, uvd->dev,
			usb_rcvintpipe(uvd->dev, uvd->video_endp + 1),
			&cam->button_sts_buf,
			1,
			qcm_int_irq,
			uvd, 16);

	errflag = usb_submit_urb(cam->button_urb, GFP_KERNEL);
	if (errflag)
		err ("usb_submit_int ret %d", errflag);
	return errflag;
}

static void qcm_stop_int_data(struct qcm *cam)
{
	usb_kill_urb(cam->button_urb);
}

static int qcm_alloc_int_urb(struct qcm *cam)
{
	cam->button_urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!cam->button_urb)
		return -ENOMEM;

	return 0;
}

static void qcm_free_int(struct qcm *cam)
{
	usb_free_urb(cam->button_urb);
}
#endif /* CONFIG_INPUT */

static int qcm_stv_setb(struct usb_device *dev, u16 reg, u8 val)
{
	int ret;

	/* we'll wait up to 3 slices but no more */
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		0x04, USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
		reg, 0, &val, 1, 3*HZ);
	return ret;
}

static int qcm_stv_setw(struct usb_device *dev, u16 reg, u16 val)
{
	int ret;

	/* we'll wait up to 3 slices but no more */
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		0x04, USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
		reg, 0, &val, 2, 3*HZ);
	return ret;
}

static int qcm_stv_getw(struct usb_device *dev, unsigned short reg,
							__le16 *val)
{
	int ret;

	/* we'll wait up to 3 slices but no more */
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		0x04, USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_DEVICE,
		reg, 0, val, 2, 3*HZ);
	return ret;
}

static int qcm_camera_on(struct uvd *uvd)
{
	int ret;
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, STV_ISO_ENABLE, 0x01));
	return 0;
}

static int qcm_camera_off(struct uvd *uvd)
{
	int ret;
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, STV_ISO_ENABLE, 0x00));
	return 0;
}

static void qcm_hsv2rgb(u16 hue, u16 sat, u16 val, u16 *r, u16 *g, u16 *b)
{
	unsigned int segment, valsat;
	signed int   h = (signed int) hue;
	unsigned int s = (sat - 32768) * 2;	/* rescale */
	unsigned int v = val;
	unsigned int p;

	/*
	the registers controlling gain are 8 bit of which
	we affect only the last 4 bits with our gain.
	we know that if saturation is 0, (unsaturated) then
	we're grayscale (center axis of the colour cone) so
	we set rgb=value. we use a formula obtained from
	wikipedia to map the cone to the RGB plane. it's
	as follows for the human value case of h=0..360,
	s=0..1, v=0..1
	h_i = h/60 % 6 , f = h/60 - h_i , p = v(1-s)
	q = v(1 - f*s) , t = v(1 - (1-f)s)
	h_i==0 => r=v , g=t, b=p
	h_i==1 => r=q , g=v, b=p
	h_i==2 => r=p , g=v, b=t
	h_i==3 => r=p , g=q, b=v
	h_i==4 => r=t , g=p, b=v
	h_i==5 => r=v , g=p, b=q
	the bottom side (the point) and the stuff just up
	of that is black so we simplify those two cases.
	*/
	if (sat < 32768) {
		/* anything less than this is unsaturated */
		*r = val;
		*g = val;
		*b = val;
		return;
	}
	if (val <= (0xFFFF/8)) {
		/* anything less than this is black */
		*r = 0;
		*g = 0;
		*b = 0;
		return;
	}

	/* the rest of this code is copying tukkat's
	implementation of the hsv2rgb conversion as taken
	from qc-usb-messenger code. the 10923 is 0xFFFF/6
	to divide the cone into 6 sectors.  */

	segment = (h + 10923) & 0xFFFF;
	segment = segment*3 >> 16;		/* 0..2: 0=R, 1=G, 2=B */
	hue -= segment * 21845;			/* -10923..10923 */
	h = hue;
	h *= 3;
	valsat = v*s >> 16;			/* 0..65534 */
	p = v - valsat;
	if (h >= 0) {
		unsigned int t = v - (valsat * (32769 - h) >> 15);
		switch (segment) {
		case 0:	/* R-> */
			*r = v;
			*g = t;
			*b = p;
			break;
		case 1:	/* G-> */
			*r = p;
			*g = v;
			*b = t;
			break;
		case 2:	/* B-> */
			*r = t;
			*g = p;
			*b = v;
			break;
		}
	} else {
		unsigned int q = v - (valsat * (32769 + h) >> 15);
		switch (segment) {
		case 0:	/* ->R */
			*r = v;
			*g = p;
			*b = q;
			break;
		case 1:	/* ->G */
			*r = q;
			*g = v;
			*b = p;
			break;
		case 2:	/* ->B */
			*r = p;
			*g = q;
			*b = v;
			break;
		}
	}
}

static int qcm_sensor_set_gains(struct uvd *uvd, u16 hue,
	u16 saturation, u16 value)
{
	int ret;
	u16 r=0,g=0,b=0;

	/* this code is based on qc-usb-messenger */
	qcm_hsv2rgb(hue, saturation, value, &r, &g, &b);

	r >>= 12;
	g >>= 12;
	b >>= 12;

	/* min val is 8 */
	r = max((u16) 8, r);
	g = max((u16) 8, g);
	b = max((u16) 8, b);

	r |= 0x30;
	g |= 0x30;
	b |= 0x30;

	/* set the r,g,b gain registers */
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x0509, r));
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x050A, g));
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x050B, b));

	/* doing as qc-usb did */
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x050C, 0x2A));
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x050D, 0x01));
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x143F, 0x01));

	return 0;
}

static int qcm_sensor_set_exposure(struct uvd *uvd, int exposure)
{
	int ret;
	int formedval;

	/* calculation was from qc-usb-messenger driver */
	formedval = ( exposure >> 12 );

	/* max value for formedval is 14 */
	formedval = min(formedval, 14);

	CHECK_RET(ret, qcm_stv_setb(uvd->dev,
			0x143A, 0xF0 | formedval));
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x143F, 0x01));
	return 0;
}

static int qcm_sensor_setlevels(struct uvd *uvd, int brightness, int contrast,
					int hue, int colour)
{
	int ret;
	/* brightness is exposure, contrast is gain, colour is saturation */
	CHECK_RET(ret,
		qcm_sensor_set_exposure(uvd, brightness));
	CHECK_RET(ret, qcm_sensor_set_gains(uvd, hue, colour, contrast));

	return 0;
}

static int qcm_sensor_setsize(struct uvd *uvd, u8 size)
{
	int ret;

	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x1505, size));
	return 0;
}

static int qcm_sensor_set_shutter(struct uvd *uvd, int whiteness)
{
	int ret;
	/* some rescaling as done by the qc-usb-messenger code */
	if (whiteness > 0xC000)
		whiteness = 0xC000 + (whiteness & 0x3FFF)*8;

	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x143D,
				(whiteness >> 8) & 0xFF));
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x143E,
				(whiteness >> 16) & 0x03));
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x143F, 0x01));

	return 0;
}

static int qcm_sensor_init(struct uvd *uvd)
{
	struct qcm *cam = (struct qcm *) uvd->user_data;
	int ret;
	int i;

	for (i=0; i < ARRAY_SIZE(regval_table) ; i++) {
		CHECK_RET(ret, qcm_stv_setb(uvd->dev,
					regval_table[i].reg,
					regval_table[i].val));
	}

	CHECK_RET(ret, qcm_stv_setw(uvd->dev, 0x15c1,
				cpu_to_le16(ISOC_PACKET_SIZE)));
	CHECK_RET(ret, qcm_stv_setb(uvd->dev, 0x15c3, 0x08));
	CHECK_RET(ret, ret = qcm_stv_setb(uvd->dev, 0x143f, 0x01));

	CHECK_RET(ret, qcm_stv_setb(uvd->dev, STV_ISO_ENABLE, 0x00));

	CHECK_RET(ret, qcm_sensor_setsize(uvd, camera_sizes[cam->size].cmd));

	CHECK_RET(ret, qcm_sensor_setlevels(uvd, uvd->vpic.brightness,
			uvd->vpic.contrast, uvd->vpic.hue, uvd->vpic.colour));

	CHECK_RET(ret, qcm_sensor_set_shutter(uvd, uvd->vpic.whiteness));
	CHECK_RET(ret, qcm_sensor_setsize(uvd, camera_sizes[cam->size].cmd));

	return 0;
}

static int qcm_set_camera_size(struct uvd *uvd)
{
	int ret;
	struct qcm *cam = (struct qcm *) uvd->user_data;

	CHECK_RET(ret, qcm_sensor_setsize(uvd, camera_sizes[cam->size].cmd));
	cam->width = camera_sizes[cam->size].width;
	cam->height = camera_sizes[cam->size].height;
	uvd->videosize = VIDEOSIZE(cam->width, cam->height);

	return 0;
}

static int qcm_setup_on_open(struct uvd *uvd)
{
	int ret;

	CHECK_RET(ret, qcm_sensor_set_gains(uvd, uvd->vpic.hue,
				uvd->vpic.colour, uvd->vpic.contrast));
	CHECK_RET(ret, qcm_sensor_set_exposure(uvd, uvd->vpic.brightness));
	CHECK_RET(ret, qcm_sensor_set_shutter(uvd, uvd->vpic.whiteness));
	CHECK_RET(ret, qcm_set_camera_size(uvd));
	CHECK_RET(ret, qcm_camera_on(uvd));
	return 0;
}

static void qcm_adjust_picture(struct uvd *uvd)
{
	int ret;
	struct qcm *cam = (struct qcm *) uvd->user_data;

	ret = qcm_camera_off(uvd);
	if (ret) {
		err("can't turn camera off. abandoning pic adjustment");
		return;
	}

	/* if there's been a change in contrast, hue, or
	colour then we need to recalculate hsv in order
	to update gains */
	if ((cam->contrast != uvd->vpic.contrast) ||
		(cam->hue != uvd->vpic.hue) ||
		(cam->colour != uvd->vpic.colour)) {
		cam->contrast = uvd->vpic.contrast;
		cam->hue = uvd->vpic.hue;
		cam->colour = uvd->vpic.colour;
		ret = qcm_sensor_set_gains(uvd, cam->hue, cam->colour,
						cam->contrast);
		if (ret) {
			err("can't set gains. abandoning pic adjustment");
			return;
		}
	}

	if (cam->brightness != uvd->vpic.brightness) {
		cam->brightness = uvd->vpic.brightness;
		ret = qcm_sensor_set_exposure(uvd, cam->brightness);
		if (ret) {
			err("can't set exposure. abandoning pic adjustment");
			return;
		}
	}

	if (cam->whiteness != uvd->vpic.whiteness) {
		cam->whiteness = uvd->vpic.whiteness;
		qcm_sensor_set_shutter(uvd, cam->whiteness);
		if (ret) {
			err("can't set shutter. abandoning pic adjustment");
			return;
		}
	}

	ret = qcm_camera_on(uvd);
	if (ret) {
		err("can't reenable camera. pic adjustment failed");
		return;
	}
}

static int qcm_process_frame(struct uvd *uvd, u8 *cdata, int framelen)
{
	int datalen;
	int totaldata;
	struct framehdr {
		__be16 id;
		__be16 len;
	};
	struct framehdr *fhdr;

	totaldata = 0;
	while (framelen) {
		fhdr = (struct framehdr *) cdata;
		datalen = be16_to_cpu(fhdr->len);
		framelen -= 4;
		cdata += 4;

		if ((fhdr->id) == cpu_to_be16(0x8001)) {
			RingQueue_Enqueue(&uvd->dp, marker, 4);
			totaldata += 4;
			continue;
		}
		if ((fhdr->id & cpu_to_be16(0xFF00)) == cpu_to_be16(0x0200)) {
			RingQueue_Enqueue(&uvd->dp, cdata, datalen);
			totaldata += datalen;
		}
		framelen -= datalen;
		cdata += datalen;
	}
	return totaldata;
}

static int qcm_compress_iso(struct uvd *uvd, struct urb *dataurb)
{
	int totlen;
	int i;
	unsigned char *cdata;

	totlen=0;
	for (i = 0; i < dataurb->number_of_packets; i++) {
		int n = dataurb->iso_frame_desc[i].actual_length;
		int st = dataurb->iso_frame_desc[i].status;

		cdata = dataurb->transfer_buffer +
			dataurb->iso_frame_desc[i].offset;

		if (st < 0) {
			warn("Data error: packet=%d. len=%d. status=%d.",
			      i, n, st);
			uvd->stats.iso_err_count++;
			continue;
		}
		if (!n)
			continue;

		totlen += qcm_process_frame(uvd, cdata, n);
	}
	return totlen;
}

static void resubmit_urb(struct uvd *uvd, struct urb *urb)
{
	int ret;

	urb->dev = uvd->dev;
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret)
		err("usb_submit_urb error (%d)", ret);
}

static void qcm_isoc_irq(struct urb *urb)
{
	int len;
	struct uvd *uvd = urb->context;

	if (!CAMERA_IS_OPERATIONAL(uvd))
		return;

	if (!uvd->streaming)
		return;

	uvd->stats.urb_count++;

	if (!urb->actual_length) {
		resubmit_urb(uvd, urb);
		return;
	}

	len = qcm_compress_iso(uvd, urb);
	resubmit_urb(uvd, urb);
	uvd->stats.urb_length = len;
	uvd->stats.data_count += len;
	if (len)
		RingQueue_WakeUpInterruptible(&uvd->dp);
}

static int qcm_start_data(struct uvd *uvd)
{
	struct qcm *cam = (struct qcm *) uvd->user_data;
	int i;
	int errflag;
	int pktsz;
	int err;

	pktsz = uvd->iso_packet_len;
	if (!CAMERA_IS_OPERATIONAL(uvd)) {
		err("Camera is not operational");
		return -EFAULT;
	}

	err = usb_set_interface(uvd->dev, uvd->iface, uvd->ifaceAltActive);
	if (err < 0) {
		err("usb_set_interface error");
		uvd->last_error = err;
		return -EBUSY;
	}

	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		int j, k;
		struct urb *urb = uvd->sbuf[i].urb;
		urb->dev = uvd->dev;
		urb->context = uvd;
		urb->pipe = usb_rcvisocpipe(uvd->dev, uvd->video_endp);
		urb->interval = 1;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = uvd->sbuf[i].data;
		urb->complete = qcm_isoc_irq;
		urb->number_of_packets = FRAMES_PER_DESC;
		urb->transfer_buffer_length = pktsz * FRAMES_PER_DESC;
		for (j=k=0; j < FRAMES_PER_DESC; j++, k += pktsz) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = pktsz;
		}
	}

	uvd->streaming = 1;
	uvd->curframe = -1;
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		errflag = usb_submit_urb(uvd->sbuf[i].urb, GFP_KERNEL);
		if (errflag)
			err ("usb_submit_isoc(%d) ret %d", i, errflag);
	}

	CHECK_RET(err, qcm_setup_input_int(cam, uvd));
	CHECK_RET(err, qcm_camera_on(uvd));
	return 0;
}

static void qcm_stop_data(struct uvd *uvd)
{
	struct qcm *cam = (struct qcm *) uvd->user_data;
	int i, j;
	int ret;

	if ((uvd == NULL) || (!uvd->streaming) || (uvd->dev == NULL))
		return;

	ret = qcm_camera_off(uvd);
	if (ret)
		warn("couldn't turn the cam off.");

	uvd->streaming = 0;

	/* Unschedule all of the iso td's */
	for (i=0; i < USBVIDEO_NUMSBUF; i++)
		usb_kill_urb(uvd->sbuf[i].urb);

	qcm_stop_int_data(cam);

	if (!uvd->remove_pending) {
		/* Set packet size to 0 */
		j = usb_set_interface(uvd->dev, uvd->iface,
					uvd->ifaceAltInactive);
		if (j < 0) {
			err("usb_set_interface() error %d.", j);
			uvd->last_error = j;
		}
	}
}

static void qcm_process_isoc(struct uvd *uvd, struct usbvideo_frame *frame)
{
	struct qcm *cam = (struct qcm *) uvd->user_data;
	int x;
	struct rgb *rgbL0;
	struct rgb *rgbL1;
	struct bayL0 *bayL0;
	struct bayL1 *bayL1;
	int hor,ver,hordel,verdel;
	assert(frame != NULL);

	switch (cam->size) {
	case SIZE_160X120:
		hor = 162; ver = 124; hordel = 1; verdel = 2;
		break;
	case SIZE_320X240:
	default:
		hor = 324; ver = 248; hordel = 2; verdel = 4;
		break;
	}

	if (frame->scanstate == ScanState_Scanning) {
		while (RingQueue_GetLength(&uvd->dp) >=
			 4 + (hor*verdel + hordel)) {
			if ((RING_QUEUE_PEEK(&uvd->dp, 0) == 0x00) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 1) == 0xff) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 2) == 0x00) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 3) == 0xff)) {
				frame->curline = 0;
				frame->scanstate = ScanState_Lines;
				frame->frameState = FrameState_Grabbing;
				RING_QUEUE_DEQUEUE_BYTES(&uvd->dp, 4);
			/*
			* if we're starting, we need to discard the first
			* 4 lines of y bayer data
			* and the first 2 gr elements of x bayer data
			*/
				RING_QUEUE_DEQUEUE_BYTES(&uvd->dp,
							(hor*verdel + hordel));
				break;
			}
			RING_QUEUE_DEQUEUE_BYTES(&uvd->dp, 1);
		}
	}

	if (frame->scanstate == ScanState_Scanning)
		return;

	/* now we can start processing bayer data so long as we have at least
	* 2 lines worth of data. this is the simplest demosaicing method that
	* I could think of. I use each 2x2 bayer element without interpolation
	* to generate 4 rgb pixels.
	*/
	while ( frame->curline < cam->height &&
		(RingQueue_GetLength(&uvd->dp) >= hor*2)) {
		/* get 2 lines of bayer for demosaicing
		 * into 2 lines of RGB */
		RingQueue_Dequeue(&uvd->dp, cam->scratch, hor*2);
		bayL0 = (struct bayL0 *) cam->scratch;
		bayL1 = (struct bayL1 *) (cam->scratch + hor);
		/* frame->curline is the rgb y line */
		rgbL0 = (struct rgb *)
				( frame->data + (cam->width*3*frame->curline));
		/* w/2 because we're already doing 2 pixels */
		rgbL1 = rgbL0 + (cam->width/2);

		for (x=0; x < cam->width; x+=2) {
			rgbL0->r = bayL0->r;
			rgbL0->g = bayL0->g;
			rgbL0->b = bayL1->b;

			rgbL0->r2 = bayL0->r;
			rgbL0->g2 = bayL1->g;
			rgbL0->b2 = bayL1->b;

			rgbL1->r = bayL0->r;
			rgbL1->g = bayL1->g;
			rgbL1->b = bayL1->b;

			rgbL1->r2 = bayL0->r;
			rgbL1->g2 = bayL1->g;
			rgbL1->b2 = bayL1->b;

			rgbL0++;
			rgbL1++;

			bayL0++;
			bayL1++;
		}

		frame->seqRead_Length += cam->width*3*2;
		frame->curline += 2;
	}
	/* See if we filled the frame */
	if (frame->curline == cam->height) {
		frame->frameState = FrameState_Done_Hold;
		frame->curline = 0;
		uvd->curframe = -1;
		uvd->stats.frame_num++;
	}
}

/* taken from konicawc */
static int qcm_set_video_mode(struct uvd *uvd, struct video_window *vw)
{
	int ret;
	int newsize;
	int oldsize;
	int x = vw->width;
	int y = vw->height;
	struct qcm *cam = (struct qcm *) uvd->user_data;

	if (x > 0 && y > 0) {
		DEBUG(2, "trying to find size %d,%d", x, y);
		for (newsize = 0; newsize <= MAX_FRAME_SIZE; newsize++) {
			if ((camera_sizes[newsize].width == x) &&
				(camera_sizes[newsize].height == y))
				break;
		}
	} else
		newsize = cam->size;

	if (newsize > MAX_FRAME_SIZE) {
		DEBUG(1, "couldn't find size %d,%d", x, y);
		return -EINVAL;
	}

	if (newsize == cam->size) {
		DEBUG(1, "Nothing to do");
		return 0;
	}

	qcm_stop_data(uvd);

	if (cam->size != newsize) {
		oldsize = cam->size;
		cam->size = newsize;
		ret = qcm_set_camera_size(uvd);
		if (ret) {
			err("Couldn't set camera size, err=%d",ret);
			/* restore the original size */
			cam->size = oldsize;
			return ret;
		}
	}

	/* Flush the input queue and clear any current frame in progress */

	RingQueue_Flush(&uvd->dp);
	if (uvd->curframe != -1) {
		uvd->frame[uvd->curframe].curline = 0;
		uvd->frame[uvd->curframe].seqRead_Length = 0;
		uvd->frame[uvd->curframe].seqRead_Index = 0;
	}

	CHECK_RET(ret, qcm_start_data(uvd));
	return 0;
}

static int qcm_configure_video(struct uvd *uvd)
{
	int ret;
	memset(&uvd->vpic, 0, sizeof(uvd->vpic));
	memset(&uvd->vpic_old, 0x55, sizeof(uvd->vpic_old));

	uvd->vpic.colour = colour;
	uvd->vpic.hue = hue;
	uvd->vpic.brightness = brightness;
	uvd->vpic.contrast = contrast;
	uvd->vpic.whiteness = whiteness;
	uvd->vpic.depth = 24;
	uvd->vpic.palette = VIDEO_PALETTE_RGB24;

	memset(&uvd->vcap, 0, sizeof(uvd->vcap));
	strcpy(uvd->vcap.name, "QCM USB Camera");
	uvd->vcap.type = VID_TYPE_CAPTURE;
	uvd->vcap.channels = 1;
	uvd->vcap.audios = 0;

	uvd->vcap.minwidth = camera_sizes[SIZE_160X120].width;
	uvd->vcap.minheight = camera_sizes[SIZE_160X120].height;
	uvd->vcap.maxwidth = camera_sizes[SIZE_320X240].width;
	uvd->vcap.maxheight = camera_sizes[SIZE_320X240].height;

	memset(&uvd->vchan, 0, sizeof(uvd->vchan));
	uvd->vchan.flags = 0 ;
	uvd->vchan.tuners = 0;
	uvd->vchan.channel = 0;
	uvd->vchan.type = VIDEO_TYPE_CAMERA;
	strcpy(uvd->vchan.name, "Camera");

	CHECK_RET(ret, qcm_sensor_init(uvd));
	return 0;
}

static int qcm_probe(struct usb_interface *intf,
			const struct usb_device_id *devid)
{
	int err;
	struct uvd *uvd;
	struct usb_device *dev = interface_to_usbdev(intf);
	struct qcm *cam;
	size_t buffer_size;
	unsigned char video_ep;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	int i,j;
	unsigned int ifacenum, ifacenum_inact=0;
	__le16 sensor_id;

	/* we don't support multiconfig cams */
	if (dev->descriptor.bNumConfigurations != 1)
		return -ENODEV;

	/* first check for the video interface and not
	* the audio interface */
	interface = &intf->cur_altsetting[0];
	if ((interface->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC)
		|| (interface->desc.bInterfaceSubClass !=
			USB_CLASS_VENDOR_SPEC))
		return -ENODEV;

	/*
	walk through each endpoint in each setting in the interface
	stop when we find the one that's an isochronous IN endpoint.
	*/
	for (i=0; i < intf->num_altsetting; i++) {
		interface = &intf->cur_altsetting[i];
		ifacenum = interface->desc.bAlternateSetting;
		/* walk the end points */
		for (j=0; j < interface->desc.bNumEndpoints; j++) {
			endpoint = &interface->endpoint[j].desc;

			if ((endpoint->bEndpointAddress &
				USB_ENDPOINT_DIR_MASK) != USB_DIR_IN)
				continue; /* not input then not good */

			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			if (!buffer_size) {
				ifacenum_inact = ifacenum;
				continue; /* 0 pkt size is not what we want */
			}

			if ((endpoint->bmAttributes &
				USB_ENDPOINT_XFERTYPE_MASK) ==
				USB_ENDPOINT_XFER_ISOC) {
				video_ep = endpoint->bEndpointAddress;
				/* break out of the search */
				goto good_videoep;
			}
		}
	}
	/* failed out since nothing useful was found */
	err("No suitable endpoint was found\n");
	return -ENODEV;

good_videoep:
	/* disable isochronous stream before doing anything else */
	err = qcm_stv_setb(dev, STV_ISO_ENABLE, 0);
	if (err < 0) {
		err("Failed to disable sensor stream");
		return -EIO;
	}

	/*
	Check that this is the same unknown sensor that is known to work. This
	sensor is suspected to be the ST VV6422C001. I'll check the same value
	that the qc-usb driver checks. This value is probably not even the
	sensor ID since it matches the USB dev ID. Oh well. If it doesn't
	match, it's probably a diff sensor so exit and apologize.
	*/
	err = qcm_stv_getw(dev, CMOS_SENSOR_IDREV, &sensor_id);
	if (err < 0) {
		err("Couldn't read sensor values. Err %d\n",err);
		return err;
	}
	if (sensor_id != cpu_to_le16(0x08F0)) {
		err("Sensor ID %x != %x. Unsupported. Sorry\n",
			le16_to_cpu(sensor_id), (0x08F0));
		return -ENODEV;
	}

	uvd = usbvideo_AllocateDevice(cams);
	if (!uvd)
		return -ENOMEM;

	cam = (struct qcm *) uvd->user_data;

	/* buf for doing demosaicing */
	cam->scratch = kmalloc(324*2, GFP_KERNEL);
	if (!cam->scratch) /* uvd freed in dereg */
		return -ENOMEM;

	/* yes, if we fail after here, cam->scratch gets freed
	by qcm_free_uvd */

	err = qcm_alloc_int_urb(cam);
	if (err < 0)
		return err;

	/* yes, if we fail after here, int urb gets freed
	by qcm_free_uvd */

	RESTRICT_TO_RANGE(size, SIZE_160X120, SIZE_320X240);
	cam->width = camera_sizes[size].width;
	cam->height = camera_sizes[size].height;
	cam->size = size;

	uvd->debug = debug;
	uvd->flags = 0;
	uvd->dev = dev;
	uvd->iface = intf->altsetting->desc.bInterfaceNumber;
	uvd->ifaceAltActive = ifacenum;
	uvd->ifaceAltInactive = ifacenum_inact;
	uvd->video_endp = video_ep;
	uvd->iso_packet_len = buffer_size;
	uvd->paletteBits = 1L << VIDEO_PALETTE_RGB24;
	uvd->defaultPalette = VIDEO_PALETTE_RGB24;
	uvd->canvas = VIDEOSIZE(320, 240);
	uvd->videosize = VIDEOSIZE(cam->width, cam->height);
	err = qcm_configure_video(uvd);
	if (err) {
		err("failed to configure video settings");
		return err;
	}

	err = usbvideo_RegisterVideoDevice(uvd);
	if (err) { /* the uvd gets freed in Deregister */
		err("usbvideo_RegisterVideoDevice() failed.");
		return err;
	}

	uvd->max_frame_size = (320 * 240 * 3);
	qcm_register_input(cam, dev);
	usb_set_intfdata(intf, uvd);
	return 0;
}

static void qcm_free_uvd(struct uvd *uvd)
{
	struct qcm *cam = (struct qcm *) uvd->user_data;

	kfree(cam->scratch);
	qcm_unregister_input(cam);
	qcm_free_int(cam);
}

static struct usbvideo_cb qcm_driver = {
	.probe = 		qcm_probe,
	.setupOnOpen = 		qcm_setup_on_open,
	.processData = 		qcm_process_isoc,
	.setVideoMode = 	qcm_set_video_mode,
	.startDataPump = 	qcm_start_data,
	.stopDataPump = 	qcm_stop_data,
	.adjustPicture = 	qcm_adjust_picture,
	.userFree = 		qcm_free_uvd
};

static int __init qcm_init(void)
{
	info(DRIVER_DESC " " DRIVER_VERSION);

	return usbvideo_register(
		&cams,
		MAX_CAMERAS,
		sizeof(struct qcm),
		"QCM",
		&qcm_driver,
		THIS_MODULE,
		qcm_table);
}

static void __exit qcm_exit(void)
{
	usbvideo_Deregister(&cams);
}

module_param(size, int, 0);
MODULE_PARM_DESC(size, "Initial Size 0: 160x120 1: 320x240");
module_param(colour, int, 0);
MODULE_PARM_DESC(colour, "Initial colour");
module_param(hue, int, 0);
MODULE_PARM_DESC(hue, "Initial hue");
module_param(brightness, int, 0);
MODULE_PARM_DESC(brightness, "Initial brightness");
module_param(contrast, int, 0);
MODULE_PARM_DESC(contrast, "Initial contrast");
module_param(whiteness, int, 0);
MODULE_PARM_DESC(whiteness, "Initial whiteness");

#ifdef CONFIG_USB_DEBUG
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level: 0-9 (default=0)");
#endif

module_init(qcm_init);
module_exit(qcm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaya Kumar");
MODULE_DESCRIPTION("QCM USB Camera");
MODULE_SUPPORTED_DEVICE("QCM USB Camera");
