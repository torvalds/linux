/*
 * konicawc.c - konica webcam driver
 *
 * Author: Simon Evans <spse@secret.org.uk>
 *
 * Copyright (C) 2002 Simon Evans
 *
 * Licence: GPL
 *
 * Driver for USB webcams based on Konica chipset. This
 * chipset is used in Intel YC76 camera.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>

#include "usbvideo.h"

#define MAX_BRIGHTNESS	108
#define MAX_CONTRAST	108
#define MAX_SATURATION	108
#define MAX_SHARPNESS	108
#define MAX_WHITEBAL	372
#define MAX_SPEED	6


#define MAX_CAMERAS	1

#define DRIVER_VERSION	"v1.4"
#define DRIVER_DESC	"Konica Webcam driver"

enum ctrl_req {
	SetWhitebal	= 0x01,
	SetBrightness	= 0x02,
	SetSharpness	= 0x03,
	SetContrast	= 0x04,
	SetSaturation	= 0x05,
};


enum frame_sizes {
	SIZE_160X120	= 0,
	SIZE_160X136	= 1,
	SIZE_176X144	= 2,
	SIZE_320X240	= 3,

};

#define MAX_FRAME_SIZE	SIZE_320X240

static struct usbvideo *cams;

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


/* Some default values for initial camera settings,
   can be set by modprobe */

static int size;
static int speed = 6;		/* Speed (fps) 0 (slowest) to 6 (fastest) */
static int brightness =	MAX_BRIGHTNESS/2;
static int contrast =	MAX_CONTRAST/2;
static int saturation =	MAX_SATURATION/2;
static int sharpness =	MAX_SHARPNESS/2;
static int whitebal =	3*(MAX_WHITEBAL/4);

static const int spd_to_iface[] = { 1, 0, 3, 2, 4, 5, 6 };

/* These FPS speeds are from the windows config box. They are
 * indexed on size (0-2) and speed (0-6). Divide by 3 to get the
 * real fps.
 */

static const int spd_to_fps[][7] = { { 24, 40, 48, 60, 72, 80, 100 },
			       { 24, 40, 48, 60, 72, 80, 100 },
			       { 18, 30, 36, 45, 54, 60, 75  },
			       { 6,  10, 12, 15, 18, 21, 25  } };

struct cam_size {
	u16	width;
	u16	height;
	u8	cmd;
};

static const struct cam_size camera_sizes[] = { { 160, 120, 0x7 },
					  { 160, 136, 0xa },
					  { 176, 144, 0x4 },
					  { 320, 240, 0x5 } };

struct konicawc {
	u8 brightness;		/* camera uses 0 - 9, x11 for real value */
	u8 contrast;		/* as above */
	u8 saturation;		/* as above */
	u8 sharpness;		/* as above */
	u8 white_bal;		/* 0 - 33, x11 for real value */
	u8 speed;		/* Stored as 0 - 6, used as index in spd_to_* (above) */
	u8 size;		/* Frame Size */
	int height;
	int width;
	struct urb *sts_urb[USBVIDEO_NUMSBUF];
	u8 sts_buf[USBVIDEO_NUMSBUF][FRAMES_PER_DESC];
	struct urb *last_data_urb;
	int lastframe;
	int cur_frame_size;	/* number of bytes in current frame size */
	int maxline;		/* number of lines per frame */
	int yplanesz;		/* Number of bytes in the Y plane */
	unsigned int buttonsts:1;
#ifdef CONFIG_INPUT
	struct input_dev *input;
	char input_physname[64];
#endif
};


#define konicawc_set_misc(uvd, req, value, index)		konicawc_ctrl_msg(uvd, USB_DIR_OUT, req, value, index, NULL, 0)
#define konicawc_get_misc(uvd, req, value, index, buf, sz)	konicawc_ctrl_msg(uvd, USB_DIR_IN, req, value, index, buf, sz)
#define konicawc_set_value(uvd, value, index)			konicawc_ctrl_msg(uvd, USB_DIR_OUT, 2, value, index, NULL, 0)


static int konicawc_ctrl_msg(struct uvd *uvd, u8 dir, u8 request, u16 value, u16 index, void *buf, int len)
{
	int retval = usb_control_msg(uvd->dev,
		dir ? usb_rcvctrlpipe(uvd->dev, 0) : usb_sndctrlpipe(uvd->dev, 0),
		    request, 0x40 | dir, value, index, buf, len, 1000);
	return retval < 0 ? retval : 0;
}


static inline void konicawc_camera_on(struct uvd *uvd)
{
	DEBUG(0, "camera on");
	konicawc_set_misc(uvd, 0x2, 1, 0x0b);
}


static inline void konicawc_camera_off(struct uvd *uvd)
{
	DEBUG(0, "camera off");
	konicawc_set_misc(uvd, 0x2, 0, 0x0b);
}


static void konicawc_set_camera_size(struct uvd *uvd)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	konicawc_set_misc(uvd, 0x2, camera_sizes[cam->size].cmd, 0x08);
	cam->width = camera_sizes[cam->size].width;
	cam->height = camera_sizes[cam->size].height;
	cam->yplanesz = cam->height * cam->width;
	cam->cur_frame_size = (cam->yplanesz * 3) / 2;
	cam->maxline = cam->yplanesz / 256;
	uvd->videosize = VIDEOSIZE(cam->width, cam->height);
}


static int konicawc_setup_on_open(struct uvd *uvd)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	DEBUG(1, "setting brightness to %d (%d)", cam->brightness,
	    cam->brightness * 11);
	konicawc_set_value(uvd, cam->brightness, SetBrightness);
	DEBUG(1, "setting white balance to %d (%d)", cam->white_bal,
	    cam->white_bal * 11);
	konicawc_set_value(uvd, cam->white_bal, SetWhitebal);
	DEBUG(1, "setting contrast to %d (%d)", cam->contrast,
	    cam->contrast * 11);
	konicawc_set_value(uvd, cam->contrast, SetContrast);
	DEBUG(1, "setting saturation to %d (%d)", cam->saturation,
	    cam->saturation * 11);
	konicawc_set_value(uvd, cam->saturation, SetSaturation);
	DEBUG(1, "setting sharpness to %d (%d)", cam->sharpness,
	    cam->sharpness * 11);
	konicawc_set_value(uvd, cam->sharpness, SetSharpness);
	konicawc_set_camera_size(uvd);
	cam->lastframe = -2;
	cam->buttonsts = 0;
	return 0;
}


static void konicawc_adjust_picture(struct uvd *uvd)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	konicawc_camera_off(uvd);
	DEBUG(1, "new brightness: %d", uvd->vpic.brightness);
	uvd->vpic.brightness = (uvd->vpic.brightness > MAX_BRIGHTNESS) ? MAX_BRIGHTNESS : uvd->vpic.brightness;
	if(cam->brightness != uvd->vpic.brightness / 11) {
	   cam->brightness = uvd->vpic.brightness / 11;
	   DEBUG(1, "setting brightness to %d (%d)", cam->brightness,
	       cam->brightness * 11);
	   konicawc_set_value(uvd, cam->brightness, SetBrightness);
	}

	DEBUG(1, "new contrast: %d", uvd->vpic.contrast);
	uvd->vpic.contrast = (uvd->vpic.contrast > MAX_CONTRAST) ? MAX_CONTRAST : uvd->vpic.contrast;
	if(cam->contrast != uvd->vpic.contrast / 11) {
		cam->contrast = uvd->vpic.contrast / 11;
		DEBUG(1, "setting contrast to %d (%d)", cam->contrast,
		    cam->contrast * 11);
		konicawc_set_value(uvd, cam->contrast, SetContrast);
	}
	konicawc_camera_on(uvd);
}

#ifdef CONFIG_INPUT

static void konicawc_register_input(struct konicawc *cam, struct usb_device *dev)
{
	struct input_dev *input_dev;
	int error;

	usb_make_path(dev, cam->input_physname, sizeof(cam->input_physname));
	strlcat(cam->input_physname, "/input0", sizeof(cam->input_physname));

	cam->input = input_dev = input_allocate_device();
	if (!input_dev) {
		dev_warn(&dev->dev,
			 "Not enough memory for camera's input device\n");
		return;
	}

	input_dev->name = "Konicawc snapshot button";
	input_dev->phys = cam->input_physname;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &dev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	input_dev->keybit[BIT_WORD(KEY_CAMERA)] = BIT_MASK(KEY_CAMERA);

	error = input_register_device(cam->input);
	if (error) {
		dev_warn(&dev->dev,
			 "Failed to register camera's input device, err: %d\n",
			 error);
		input_free_device(cam->input);
		cam->input = NULL;
	}
}

static void konicawc_unregister_input(struct konicawc *cam)
{
	if (cam->input) {
		input_unregister_device(cam->input);
		cam->input = NULL;
	}
}

static void konicawc_report_buttonstat(struct konicawc *cam)
{
	if (cam->input) {
		input_report_key(cam->input, KEY_CAMERA, cam->buttonsts);
		input_sync(cam->input);
	}
}

#else

static inline void konicawc_register_input(struct konicawc *cam, struct usb_device *dev) { }
static inline void konicawc_unregister_input(struct konicawc *cam) { }
static inline void konicawc_report_buttonstat(struct konicawc *cam) { }

#endif /* CONFIG_INPUT */

static int konicawc_compress_iso(struct uvd *uvd, struct urb *dataurb, struct urb *stsurb)
{
	char *cdata;
	int i, totlen = 0;
	unsigned char *status = stsurb->transfer_buffer;
	int keep = 0, discard = 0, bad = 0;
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	for (i = 0; i < dataurb->number_of_packets; i++) {
		int button = cam->buttonsts;
		unsigned char sts;
		int n = dataurb->iso_frame_desc[i].actual_length;
		int st = dataurb->iso_frame_desc[i].status;
		cdata = dataurb->transfer_buffer +
			dataurb->iso_frame_desc[i].offset;

		/* Detect and ignore errored packets */
		if (st < 0) {
			DEBUG(1, "Data error: packet=%d. len=%d. status=%d.",
			      i, n, st);
			uvd->stats.iso_err_count++;
			continue;
		}

		/* Detect and ignore empty packets */
		if (n <= 0) {
			uvd->stats.iso_skip_count++;
			continue;
		}

		/* See what the status data said about the packet */
		sts = *(status+stsurb->iso_frame_desc[i].offset);

		/* sts: 0x80-0xff: frame start with frame number (ie 0-7f)
		 * otherwise:
		 * bit 0 0: keep packet
		 *	 1: drop packet (padding data)
		 *
		 * bit 4 0 button not clicked
		 *       1 button clicked
		 * button is used to `take a picture' (in software)
		 */

		if(sts < 0x80) {
			button = !!(sts & 0x40);
			sts &= ~0x40;
		}

		/* work out the button status, but don't do
		   anything with it for now */

		if(button != cam->buttonsts) {
			DEBUG(2, "button: %sclicked", button ? "" : "un");
			cam->buttonsts = button;
			konicawc_report_buttonstat(cam);
		}

		if(sts == 0x01) { /* drop frame */
			discard++;
			continue;
		}

		if((sts > 0x01) && (sts < 0x80)) {
			dev_info(&uvd->dev->dev, "unknown status %2.2x\n",
				 sts);
			bad++;
			continue;
		}
		if(!sts && cam->lastframe == -2) {
			DEBUG(2, "dropping frame looking for image start");
			continue;
		}

		keep++;
		if(sts & 0x80) { /* frame start */
			unsigned char marker[] = { 0, 0xff, 0, 0x00 };

			if(cam->lastframe == -2) {
				DEBUG(2, "found initial image");
				cam->lastframe = -1;
			}

			marker[3] = sts & 0x7F;
			RingQueue_Enqueue(&uvd->dp, marker, 4);
			totlen += 4;
		}

		totlen += n;	/* Little local accounting */
		RingQueue_Enqueue(&uvd->dp, cdata, n);
	}
	DEBUG(8, "finished: keep = %d discard = %d bad = %d added %d bytes",
		    keep, discard, bad, totlen);
	return totlen;
}


static void resubmit_urb(struct uvd *uvd, struct urb *urb)
{
	int i, ret;
	for (i = 0; i < FRAMES_PER_DESC; i++) {
		urb->iso_frame_desc[i].status = 0;
	}
	urb->dev = uvd->dev;
	urb->status = 0;
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	DEBUG(3, "submitting urb of length %d", urb->transfer_buffer_length);
	if(ret)
		err("usb_submit_urb error (%d)", ret);

}


static void konicawc_isoc_irq(struct urb *urb)
{
	struct uvd *uvd = urb->context;
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	/* We don't want to do anything if we are about to be removed! */
	if (!CAMERA_IS_OPERATIONAL(uvd))
		return;

	if (!uvd->streaming) {
		DEBUG(1, "Not streaming, but interrupt!");
		return;
	}

	DEBUG(3, "got frame %d len = %d buflen =%d", urb->start_frame, urb->actual_length, urb->transfer_buffer_length);

	uvd->stats.urb_count++;

	if (urb->transfer_buffer_length > 32) {
		cam->last_data_urb = urb;
		return;
	}
	/* Copy the data received into ring queue */
	if(cam->last_data_urb) {
		int len = 0;
		if(urb->start_frame != cam->last_data_urb->start_frame)
			err("Lost sync on frames");
		else if (!urb->status && !cam->last_data_urb->status)
			len = konicawc_compress_iso(uvd, cam->last_data_urb, urb);

		resubmit_urb(uvd, cam->last_data_urb);
		resubmit_urb(uvd, urb);
		cam->last_data_urb = NULL;
		uvd->stats.urb_length = len;
		uvd->stats.data_count += len;
		if(len)
			RingQueue_WakeUpInterruptible(&uvd->dp);
		return;
	}
	return;
}


static int konicawc_start_data(struct uvd *uvd)
{
	struct usb_device *dev = uvd->dev;
	int i, errFlag;
	struct konicawc *cam = (struct konicawc *)uvd->user_data;
	int pktsz;
	struct usb_interface *intf;
	struct usb_host_interface *interface = NULL;

	intf = usb_ifnum_to_if(dev, uvd->iface);
	if (intf)
		interface = usb_altnum_to_altsetting(intf,
				spd_to_iface[cam->speed]);
	if (!interface)
		return -ENXIO;
	pktsz = le16_to_cpu(interface->endpoint[1].desc.wMaxPacketSize);
	DEBUG(1, "pktsz = %d", pktsz);
	if (!CAMERA_IS_OPERATIONAL(uvd)) {
		err("Camera is not operational");
		return -EFAULT;
	}
	uvd->curframe = -1;
	konicawc_camera_on(uvd);
	/* Alternate interface 1 is is the biggest frame size */
	i = usb_set_interface(dev, uvd->iface, uvd->ifaceAltActive);
	if (i < 0) {
		err("usb_set_interface error");
		uvd->last_error = i;
		return -EBUSY;
	}

	/* We double buffer the Iso lists */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		int j, k;
		struct urb *urb = uvd->sbuf[i].urb;
		urb->dev = dev;
		urb->context = uvd;
		urb->pipe = usb_rcvisocpipe(dev, uvd->video_endp);
		urb->interval = 1;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = uvd->sbuf[i].data;
		urb->complete = konicawc_isoc_irq;
		urb->number_of_packets = FRAMES_PER_DESC;
		urb->transfer_buffer_length = pktsz * FRAMES_PER_DESC;
		for (j=k=0; j < FRAMES_PER_DESC; j++, k += pktsz) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = pktsz;
		}

		urb = cam->sts_urb[i];
		urb->dev = dev;
		urb->context = uvd;
		urb->pipe = usb_rcvisocpipe(dev, uvd->video_endp-1);
		urb->interval = 1;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = cam->sts_buf[i];
		urb->complete = konicawc_isoc_irq;
		urb->number_of_packets = FRAMES_PER_DESC;
		urb->transfer_buffer_length = FRAMES_PER_DESC;
		for (j=0; j < FRAMES_PER_DESC; j++) {
			urb->iso_frame_desc[j].offset = j;
			urb->iso_frame_desc[j].length = 1;
		}
	}

	cam->last_data_urb = NULL;

	/* Submit all URBs */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		errFlag = usb_submit_urb(cam->sts_urb[i], GFP_KERNEL);
		if (errFlag)
			err("usb_submit_isoc(%d) ret %d", i, errFlag);

		errFlag = usb_submit_urb(uvd->sbuf[i].urb, GFP_KERNEL);
		if (errFlag)
			err ("usb_submit_isoc(%d) ret %d", i, errFlag);
	}

	uvd->streaming = 1;
	DEBUG(1, "streaming=1 video_endp=$%02x", uvd->video_endp);
	return 0;
}


static void konicawc_stop_data(struct uvd *uvd)
{
	int i, j;
	struct konicawc *cam;

	if ((uvd == NULL) || (!uvd->streaming) || (uvd->dev == NULL))
		return;

	konicawc_camera_off(uvd);
	uvd->streaming = 0;
	cam = (struct konicawc *)uvd->user_data;
	cam->last_data_urb = NULL;

	/* Unschedule all of the iso td's */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		usb_kill_urb(uvd->sbuf[i].urb);
		usb_kill_urb(cam->sts_urb[i]);
	}

	if (!uvd->remove_pending) {
		/* Set packet size to 0 */
		j = usb_set_interface(uvd->dev, uvd->iface, uvd->ifaceAltInactive);
		if (j < 0) {
			err("usb_set_interface() error %d.", j);
			uvd->last_error = j;
		}
	}
}


static void konicawc_process_isoc(struct uvd *uvd, struct usbvideo_frame *frame)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;
	int maxline = cam->maxline;
	int yplanesz = cam->yplanesz;

	assert(frame != NULL);

	DEBUG(5, "maxline = %d yplanesz = %d", maxline, yplanesz);
	DEBUG(3, "Frame state = %d", frame->scanstate);

	if(frame->scanstate == ScanState_Scanning) {
		int drop = 0;
		int curframe;
		int fdrops = 0;
		DEBUG(3, "Searching for marker, queue len = %d", RingQueue_GetLength(&uvd->dp));
		while(RingQueue_GetLength(&uvd->dp) >= 4) {
			if ((RING_QUEUE_PEEK(&uvd->dp, 0) == 0x00) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 1) == 0xff) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 2) == 0x00) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 3) < 0x80)) {
				curframe = RING_QUEUE_PEEK(&uvd->dp, 3);
				if(cam->lastframe >= 0) {
					fdrops = (0x80 + curframe - cam->lastframe) & 0x7F;
					fdrops--;
					if(fdrops) {
						dev_info(&uvd->dev->dev,
							 "Dropped %d frames "
							 "(%d -> %d)\n",
							 fdrops,
							 cam->lastframe,
							 curframe);
					}
				}
				cam->lastframe = curframe;
				frame->curline = 0;
				frame->scanstate = ScanState_Lines;
				RING_QUEUE_DEQUEUE_BYTES(&uvd->dp, 4);
				break;
			}
			RING_QUEUE_DEQUEUE_BYTES(&uvd->dp, 1);
			drop++;
		}
		if(drop)
			DEBUG(2, "dropped %d bytes looking for new frame", drop);
	}

	if(frame->scanstate == ScanState_Scanning)
		return;

	/* Try to move data from queue into frame buffer
	 * We get data in blocks of 384 bytes made up of:
	 * 256 Y, 64 U, 64 V.
	 * This needs to be written out as a Y plane, a U plane and a V plane.
	 */

	while ( frame->curline < maxline && (RingQueue_GetLength(&uvd->dp) >= 384)) {
		/* Y */
		RingQueue_Dequeue(&uvd->dp, frame->data + (frame->curline * 256), 256);
		/* U */
		RingQueue_Dequeue(&uvd->dp, frame->data + yplanesz + (frame->curline * 64), 64);
		/* V */
		RingQueue_Dequeue(&uvd->dp, frame->data + (5 * yplanesz)/4 + (frame->curline * 64), 64);
		frame->seqRead_Length += 384;
		frame->curline++;
	}
	/* See if we filled the frame */
	if (frame->curline == maxline) {
		DEBUG(5, "got whole frame");

		frame->frameState = FrameState_Done_Hold;
		frame->curline = 0;
		uvd->curframe = -1;
		uvd->stats.frame_num++;
	}
}


static int konicawc_find_fps(int size, int fps)
{
	int i;

	fps *= 3;
	DEBUG(1, "konica_find_fps: size = %d fps = %d", size, fps);
	if(fps <= spd_to_fps[size][0])
		return 0;

	if(fps >= spd_to_fps[size][MAX_SPEED])
		return MAX_SPEED;

	for(i = 0; i < MAX_SPEED; i++) {
		if((fps >= spd_to_fps[size][i]) && (fps <= spd_to_fps[size][i+1])) {
			DEBUG(2, "fps %d between %d and %d", fps, i, i+1);
			if( (fps - spd_to_fps[size][i]) < (spd_to_fps[size][i+1] - fps))
				return i;
			else
				return i+1;
		}
	}
	return MAX_SPEED+1;
}


static int konicawc_set_video_mode(struct uvd *uvd, struct video_window *vw)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;
	int newspeed = cam->speed;
	int newsize;
	int x = vw->width;
	int y = vw->height;
	int fps = vw->flags;

	if(x > 0 && y > 0) {
		DEBUG(2, "trying to find size %d,%d", x, y);
		for(newsize = 0; newsize <= MAX_FRAME_SIZE; newsize++) {
			if((camera_sizes[newsize].width == x) && (camera_sizes[newsize].height == y))
				break;
		}
	} else {
		newsize = cam->size;
	}

	if(newsize > MAX_FRAME_SIZE) {
		DEBUG(1, "couldn't find size %d,%d", x, y);
		return -EINVAL;
	}

	if(fps > 0) {
		DEBUG(1, "trying to set fps to %d", fps);
		newspeed = konicawc_find_fps(newsize, fps);
		DEBUG(1, "find_fps returned %d (%d)", newspeed, spd_to_fps[newsize][newspeed]);
	}

	if(newspeed > MAX_SPEED)
		return -EINVAL;

	DEBUG(1, "setting size to %d speed to %d", newsize, newspeed);
	if((newsize == cam->size) && (newspeed == cam->speed)) {
		DEBUG(1, "Nothing to do");
		return 0;
	}
	DEBUG(0, "setting to  %dx%d @ %d fps", camera_sizes[newsize].width,
	     camera_sizes[newsize].height, spd_to_fps[newsize][newspeed]/3);

	konicawc_stop_data(uvd);
	uvd->ifaceAltActive = spd_to_iface[newspeed];
	DEBUG(1, "new interface = %d", uvd->ifaceAltActive);
	cam->speed = newspeed;

	if(cam->size != newsize) {
		cam->size = newsize;
		konicawc_set_camera_size(uvd);
	}

	/* Flush the input queue and clear any current frame in progress */

	RingQueue_Flush(&uvd->dp);
	cam->lastframe = -2;
	if(uvd->curframe != -1) {
		uvd->frame[uvd->curframe].curline = 0;
		uvd->frame[uvd->curframe].seqRead_Length = 0;
		uvd->frame[uvd->curframe].seqRead_Index = 0;
	}

	konicawc_start_data(uvd);
	return 0;
}


static int konicawc_calculate_fps(struct uvd *uvd)
{
	struct konicawc *cam = uvd->user_data;
	return spd_to_fps[cam->size][cam->speed]/3;
}


static void konicawc_configure_video(struct uvd *uvd)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;
	u8 buf[2];

	memset(&uvd->vpic, 0, sizeof(uvd->vpic));
	memset(&uvd->vpic_old, 0x55, sizeof(uvd->vpic_old));

	RESTRICT_TO_RANGE(brightness, 0, MAX_BRIGHTNESS);
	RESTRICT_TO_RANGE(contrast, 0, MAX_CONTRAST);
	RESTRICT_TO_RANGE(saturation, 0, MAX_SATURATION);
	RESTRICT_TO_RANGE(sharpness, 0, MAX_SHARPNESS);
	RESTRICT_TO_RANGE(whitebal, 0, MAX_WHITEBAL);

	cam->brightness = brightness / 11;
	cam->contrast = contrast / 11;
	cam->saturation = saturation / 11;
	cam->sharpness = sharpness / 11;
	cam->white_bal = whitebal / 11;

	uvd->vpic.colour = 108;
	uvd->vpic.hue = 108;
	uvd->vpic.brightness = brightness;
	uvd->vpic.contrast = contrast;
	uvd->vpic.whiteness = whitebal;
	uvd->vpic.depth = 6;
	uvd->vpic.palette = VIDEO_PALETTE_YUV420P;

	memset(&uvd->vcap, 0, sizeof(uvd->vcap));
	strcpy(uvd->vcap.name, "Konica Webcam");
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

	/* Talk to device */
	DEBUG(1, "device init");
	if(!konicawc_get_misc(uvd, 0x3, 0, 0x10, buf, 2))
		DEBUG(2, "3,10 -> %2.2x %2.2x", buf[0], buf[1]);
	if(!konicawc_get_misc(uvd, 0x3, 0, 0x10, buf, 2))
		DEBUG(2, "3,10 -> %2.2x %2.2x", buf[0], buf[1]);
	if(konicawc_set_misc(uvd, 0x2, 0, 0xd))
		DEBUG(2, "2,0,d failed");
	DEBUG(1, "setting initial values");
}

static int konicawc_probe(struct usb_interface *intf, const struct usb_device_id *devid)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct uvd *uvd = NULL;
	int ix, i, nas;
	int actInterface=-1, inactInterface=-1, maxPS=0;
	unsigned char video_ep = 0;

	DEBUG(1, "konicawc_probe(%p)", intf);

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return -ENODEV;

	dev_info(&intf->dev, "Konica Webcam (rev. 0x%04x)\n",
		 le16_to_cpu(dev->descriptor.bcdDevice));
	RESTRICT_TO_RANGE(speed, 0, MAX_SPEED);

	/* Validate found interface: must have one ISO endpoint */
	nas = intf->num_altsetting;
	if (nas != 8) {
		err("Incorrect number of alternate settings (%d) for this camera!", nas);
		return -ENODEV;
	}
	/* Validate all alternate settings */
	for (ix=0; ix < nas; ix++) {
		const struct usb_host_interface *interface;
		const struct usb_endpoint_descriptor *endpoint;

		interface = &intf->altsetting[ix];
		i = interface->desc.bAlternateSetting;
		if (interface->desc.bNumEndpoints != 2) {
			err("Interface %d. has %u. endpoints!",
			    interface->desc.bInterfaceNumber,
			    (unsigned)(interface->desc.bNumEndpoints));
			return -ENODEV;
		}
		endpoint = &interface->endpoint[1].desc;
		DEBUG(1, "found endpoint: addr: 0x%2.2x maxps = 0x%4.4x",
		    endpoint->bEndpointAddress, le16_to_cpu(endpoint->wMaxPacketSize));
		if (video_ep == 0)
			video_ep = endpoint->bEndpointAddress;
		else if (video_ep != endpoint->bEndpointAddress) {
			err("Alternate settings have different endpoint addresses!");
			return -ENODEV;
		}
		if (!usb_endpoint_xfer_isoc(endpoint)) {
			err("Interface %d. has non-ISO endpoint!",
			    interface->desc.bInterfaceNumber);
			return -ENODEV;
		}
		if (usb_endpoint_dir_out(endpoint)) {
			err("Interface %d. has ISO OUT endpoint!",
			    interface->desc.bInterfaceNumber);
			return -ENODEV;
		}
		if (le16_to_cpu(endpoint->wMaxPacketSize) == 0) {
			if (inactInterface < 0)
				inactInterface = i;
			else {
				err("More than one inactive alt. setting!");
				return -ENODEV;
			}
		} else {
			if (i == spd_to_iface[speed]) {
				/* This one is the requested one */
				actInterface = i;
			}
		}
		if (le16_to_cpu(endpoint->wMaxPacketSize) > maxPS)
			maxPS = le16_to_cpu(endpoint->wMaxPacketSize);
	}
	if(actInterface == -1) {
		err("Cant find required endpoint");
		return -ENODEV;
	}

	DEBUG(1, "Selecting requested active setting=%d. maxPS=%d.", actInterface, maxPS);

	uvd = usbvideo_AllocateDevice(cams);
	if (uvd != NULL) {
		struct konicawc *cam = (struct konicawc *)(uvd->user_data);
		/* Here uvd is a fully allocated uvd object */
		for(i = 0; i < USBVIDEO_NUMSBUF; i++) {
			cam->sts_urb[i] = usb_alloc_urb(FRAMES_PER_DESC, GFP_KERNEL);
			if(cam->sts_urb[i] == NULL) {
				while(i--) {
					usb_free_urb(cam->sts_urb[i]);
				}
				err("can't allocate urbs");
				return -ENOMEM;
			}
		}
		cam->speed = speed;
		RESTRICT_TO_RANGE(size, SIZE_160X120, SIZE_320X240);
		cam->width = camera_sizes[size].width;
		cam->height = camera_sizes[size].height;
		cam->size = size;

		uvd->flags = 0;
		uvd->debug = debug;
		uvd->dev = dev;
		uvd->iface = intf->altsetting->desc.bInterfaceNumber;
		uvd->ifaceAltInactive = inactInterface;
		uvd->ifaceAltActive = actInterface;
		uvd->video_endp = video_ep;
		uvd->iso_packet_len = maxPS;
		uvd->paletteBits = 1L << VIDEO_PALETTE_YUV420P;
		uvd->defaultPalette = VIDEO_PALETTE_YUV420P;
		uvd->canvas = VIDEOSIZE(320, 240);
		uvd->videosize = VIDEOSIZE(cam->width, cam->height);

		/* Initialize konicawc specific data */
		konicawc_configure_video(uvd);

		i = usbvideo_RegisterVideoDevice(uvd);
		uvd->max_frame_size = (320 * 240 * 3)/2;
		if (i != 0) {
			err("usbvideo_RegisterVideoDevice() failed.");
			uvd = NULL;
		}

		konicawc_register_input(cam, dev);
	}

	if (uvd) {
		usb_set_intfdata (intf, uvd);
		return 0;
	}
	return -EIO;
}


static void konicawc_free_uvd(struct uvd *uvd)
{
	int i;
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	konicawc_unregister_input(cam);

	for (i = 0; i < USBVIDEO_NUMSBUF; i++) {
		usb_free_urb(cam->sts_urb[i]);
		cam->sts_urb[i] = NULL;
	}
}


static struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x04c8, 0x0720) }, /* Intel YC 76 */
	{ }  /* Terminating entry */
};


static int __init konicawc_init(void)
{
	struct usbvideo_cb cbTbl;
	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
	       DRIVER_DESC "\n");
	memset(&cbTbl, 0, sizeof(cbTbl));
	cbTbl.probe = konicawc_probe;
	cbTbl.setupOnOpen = konicawc_setup_on_open;
	cbTbl.processData = konicawc_process_isoc;
	cbTbl.getFPS = konicawc_calculate_fps;
	cbTbl.setVideoMode = konicawc_set_video_mode;
	cbTbl.startDataPump = konicawc_start_data;
	cbTbl.stopDataPump = konicawc_stop_data;
	cbTbl.adjustPicture = konicawc_adjust_picture;
	cbTbl.userFree = konicawc_free_uvd;
	return usbvideo_register(
		&cams,
		MAX_CAMERAS,
		sizeof(struct konicawc),
		"konicawc",
		&cbTbl,
		THIS_MODULE,
		id_table);
}


static void __exit konicawc_cleanup(void)
{
	usbvideo_Deregister(&cams);
}


MODULE_DEVICE_TABLE(usb, id_table);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Evans <spse@secret.org.uk>");
MODULE_DESCRIPTION(DRIVER_DESC);
module_param(speed, int, 0);
MODULE_PARM_DESC(speed, "Initial speed: 0 (slowest) - 6 (fastest)");
module_param(size, int, 0);
MODULE_PARM_DESC(size, "Initial Size 0: 160x120 1: 160x136 2: 176x144 3: 320x240");
module_param(brightness, int, 0);
MODULE_PARM_DESC(brightness, "Initial brightness 0 - 108");
module_param(contrast, int, 0);
MODULE_PARM_DESC(contrast, "Initial contrast 0 - 108");
module_param(saturation, int, 0);
MODULE_PARM_DESC(saturation, "Initial saturation 0 - 108");
module_param(sharpness, int, 0);
MODULE_PARM_DESC(sharpness, "Initial brightness 0 - 108");
module_param(whitebal, int, 0);
MODULE_PARM_DESC(whitebal, "Initial white balance 0 - 363");

#ifdef CONFIG_USB_DEBUG
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level: 0-9 (default=0)");
#endif

module_init(konicawc_init);
module_exit(konicawc_cleanup);
