/* Linux driver for Philips webcam
   USB and Video4Linux interface part.
   (C) 1999-2004 Nemosoft Unv.
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/io.h>

#include "pwc.h"

static struct v4l2_queryctrl pwc_controls[] = {
	{
	    .id      = V4L2_CID_BRIGHTNESS,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Brightness",
	    .minimum = 0,
	    .maximum = 128,
	    .step    = 1,
	    .default_value = 64,
	},
	{
	    .id      = V4L2_CID_CONTRAST,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Contrast",
	    .minimum = 0,
	    .maximum = 64,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_SATURATION,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Saturation",
	    .minimum = -100,
	    .maximum = 100,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_GAMMA,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Gamma",
	    .minimum = 0,
	    .maximum = 32,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_RED_BALANCE,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Red Gain",
	    .minimum = 0,
	    .maximum = 256,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_BLUE_BALANCE,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Blue Gain",
	    .minimum = 0,
	    .maximum = 256,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_AUTO_WHITE_BALANCE,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "Auto White Balance",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_EXPOSURE,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Shutter Speed (Exposure)",
	    .minimum = 0,
	    .maximum = 256,
	    .step    = 1,
	    .default_value = 200,
	},
	{
	    .id      = V4L2_CID_AUTOGAIN,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "Auto Gain Enabled",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
	    .default_value = 1,
	},
	{
	    .id      = V4L2_CID_GAIN,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Gain Level",
	    .minimum = 0,
	    .maximum = 256,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_PRIVATE_SAVE_USER,
	    .type    = V4L2_CTRL_TYPE_BUTTON,
	    .name    = "Save User Settings",
	    .minimum = 0,
	    .maximum = 0,
	    .step    = 0,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_PRIVATE_RESTORE_USER,
	    .type    = V4L2_CTRL_TYPE_BUTTON,
	    .name    = "Restore User Settings",
	    .minimum = 0,
	    .maximum = 0,
	    .step    = 0,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_PRIVATE_RESTORE_FACTORY,
	    .type    = V4L2_CTRL_TYPE_BUTTON,
	    .name    = "Restore Factory Settings",
	    .minimum = 0,
	    .maximum = 0,
	    .step    = 0,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_PRIVATE_COLOUR_MODE,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "Colour mode",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_PRIVATE_AUTOCONTOUR,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "Auto contour",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_PRIVATE_CONTOUR,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Contour",
	    .minimum = 0,
	    .maximum = 63,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_PRIVATE_BACKLIGHT,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "Backlight compensation",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	  .id      = V4L2_CID_PRIVATE_FLICKERLESS,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "Flickerless",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
	    .default_value = 0,
	},
	{
	    .id      = V4L2_CID_PRIVATE_NOISE_REDUCTION,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Noise reduction",
	    .minimum = 0,
	    .maximum = 3,
	    .step    = 1,
	    .default_value = 0,
	},
};


static void pwc_vidioc_fill_fmt(const struct pwc_device *pdev, struct v4l2_format *f)
{
	memset(&f->fmt.pix, 0, sizeof(struct v4l2_pix_format));
	f->fmt.pix.width        = pdev->view.x;
	f->fmt.pix.height       = pdev->view.y;
	f->fmt.pix.field        = V4L2_FIELD_NONE;
	if (pdev->vpalette == VIDEO_PALETTE_YUV420P) {
		f->fmt.pix.pixelformat  = V4L2_PIX_FMT_YUV420;
		f->fmt.pix.bytesperline = (f->fmt.pix.width * 3)/2;
		f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	} else {
		/* vbandlength contains 4 lines ...  */
		f->fmt.pix.bytesperline = pdev->vbandlength/4;
		f->fmt.pix.sizeimage = pdev->frame_size + sizeof(struct pwc_raw_frame);
		if (DEVICE_USE_CODEC1(pdev->type))
			f->fmt.pix.pixelformat  = V4L2_PIX_FMT_PWC1;
		else
			f->fmt.pix.pixelformat  = V4L2_PIX_FMT_PWC2;
	}
	PWC_DEBUG_IOCTL("pwc_vidioc_fill_fmt() "
			"width=%d, height=%d, bytesperline=%d, sizeimage=%d, pixelformat=%c%c%c%c\n",
			f->fmt.pix.width,
			f->fmt.pix.height,
			f->fmt.pix.bytesperline,
			f->fmt.pix.sizeimage,
			(f->fmt.pix.pixelformat)&255,
			(f->fmt.pix.pixelformat>>8)&255,
			(f->fmt.pix.pixelformat>>16)&255,
			(f->fmt.pix.pixelformat>>24)&255);
}

/* ioctl(VIDIOC_TRY_FMT) */
static int pwc_vidioc_try_fmt(struct pwc_device *pdev, struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		PWC_DEBUG_IOCTL("Bad video type must be V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}

	switch (f->fmt.pix.pixelformat) {
		case V4L2_PIX_FMT_YUV420:
			break;
		case V4L2_PIX_FMT_PWC1:
			if (DEVICE_USE_CODEC23(pdev->type)) {
				PWC_DEBUG_IOCTL("codec1 is only supported for old pwc webcam\n");
				return -EINVAL;
			}
			break;
		case V4L2_PIX_FMT_PWC2:
			if (DEVICE_USE_CODEC1(pdev->type)) {
				PWC_DEBUG_IOCTL("codec23 is only supported for new pwc webcam\n");
				return -EINVAL;
			}
			break;
		default:
			PWC_DEBUG_IOCTL("Unsupported pixel format\n");
			return -EINVAL;

	}

	if (f->fmt.pix.width > pdev->view_max.x)
		f->fmt.pix.width = pdev->view_max.x;
	else if (f->fmt.pix.width < pdev->view_min.x)
		f->fmt.pix.width = pdev->view_min.x;

	if (f->fmt.pix.height > pdev->view_max.y)
		f->fmt.pix.height = pdev->view_max.y;
	else if (f->fmt.pix.height < pdev->view_min.y)
		f->fmt.pix.height = pdev->view_min.y;

	return 0;
}

/* ioctl(VIDIOC_SET_FMT) */
static int pwc_vidioc_set_fmt(struct pwc_device *pdev, struct v4l2_format *f)
{
	int ret, fps, snapshot, compression, pixelformat;

	ret = pwc_vidioc_try_fmt(pdev, f);
	if (ret<0)
		return ret;

	pixelformat = f->fmt.pix.pixelformat;
	compression = pdev->vcompression;
	snapshot = 0;
	fps = pdev->vframes;
	if (f->fmt.pix.priv) {
		compression = (f->fmt.pix.priv & PWC_QLT_MASK) >> PWC_QLT_SHIFT;
		snapshot = !!(f->fmt.pix.priv & PWC_FPS_SNAPSHOT);
		fps = (f->fmt.pix.priv & PWC_FPS_FRMASK) >> PWC_FPS_SHIFT;
		if (fps == 0)
			fps = pdev->vframes;
	}

	if (pixelformat == V4L2_PIX_FMT_YUV420)
		pdev->vpalette = VIDEO_PALETTE_YUV420P;
	else
		pdev->vpalette = VIDEO_PALETTE_RAW;

	PWC_DEBUG_IOCTL("Try to change format to: width=%d height=%d fps=%d "
			"compression=%d snapshot=%d format=%c%c%c%c\n",
			f->fmt.pix.width, f->fmt.pix.height, fps,
			compression, snapshot,
			(pixelformat)&255,
			(pixelformat>>8)&255,
			(pixelformat>>16)&255,
			(pixelformat>>24)&255);

	ret = pwc_try_video_mode(pdev,
				 f->fmt.pix.width,
				 f->fmt.pix.height,
				 fps,
				 compression,
				 snapshot);

	PWC_DEBUG_IOCTL("pwc_try_video_mode(), return=%d\n", ret);

	if (ret)
		return ret;

	pwc_vidioc_fill_fmt(pdev, f);

	return 0;

}

long pwc_video_do_ioctl(struct file *file, unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct pwc_device *pdev;
	DECLARE_WAITQUEUE(wait, current);

	if (vdev == NULL)
		return -EFAULT;
	pdev = video_get_drvdata(vdev);
	if (pdev == NULL)
		return -EFAULT;

#ifdef CONFIG_USB_PWC_DEBUG
	if (PWC_DEBUG_LEVEL_IOCTL & pwc_trace) {
		v4l_printk_ioctl(cmd);
		printk("\n");
	}
#endif


	switch (cmd) {
		/* Query cabapilities */
		case VIDIOCGCAP:
		{
			struct video_capability *caps = arg;

			strcpy(caps->name, vdev->name);
			caps->type = VID_TYPE_CAPTURE;
			caps->channels = 1;
			caps->audios = 1;
			caps->minwidth  = pdev->view_min.x;
			caps->minheight = pdev->view_min.y;
			caps->maxwidth  = pdev->view_max.x;
			caps->maxheight = pdev->view_max.y;
			break;
		}

		/* Channel functions (simulate 1 channel) */
		case VIDIOCGCHAN:
		{
			struct video_channel *v = arg;

			if (v->channel != 0)
				return -EINVAL;
			v->flags = 0;
			v->tuners = 0;
			v->type = VIDEO_TYPE_CAMERA;
			strcpy(v->name, "Webcam");
			return 0;
		}

		case VIDIOCSCHAN:
		{
			/* The spec says the argument is an integer, but
			   the bttv driver uses a video_channel arg, which
			   makes sense becasue it also has the norm flag.
			 */
			struct video_channel *v = arg;
			if (v->channel != 0)
				return -EINVAL;
			return 0;
		}


		/* Picture functions; contrast etc. */
		case VIDIOCGPICT:
		{
			struct video_picture *p = arg;
			int val;

			val = pwc_get_brightness(pdev);
			if (val >= 0)
				p->brightness = (val<<9);
			else
				p->brightness = 0xffff;
			val = pwc_get_contrast(pdev);
			if (val >= 0)
				p->contrast = (val<<10);
			else
				p->contrast = 0xffff;
			/* Gamma, Whiteness, what's the difference? :) */
			val = pwc_get_gamma(pdev);
			if (val >= 0)
				p->whiteness = (val<<11);
			else
				p->whiteness = 0xffff;
			if (pwc_get_saturation(pdev, &val)<0)
				p->colour = 0xffff;
			else
				p->colour = 32768 + val * 327;
			p->depth = 24;
			p->palette = pdev->vpalette;
			p->hue = 0xFFFF; /* N/A */
			break;
		}

		case VIDIOCSPICT:
		{
			struct video_picture *p = arg;
			/*
			 *	FIXME:	Suppose we are mid read
				ANSWER: No problem: the firmware of the camera
					can handle brightness/contrast/etc
					changes at _any_ time, and the palette
					is used exactly once in the uncompress
					routine.
			 */
			pwc_set_brightness(pdev, p->brightness);
			pwc_set_contrast(pdev, p->contrast);
			pwc_set_gamma(pdev, p->whiteness);
			pwc_set_saturation(pdev, (p->colour-32768)/327);
			if (p->palette && p->palette != pdev->vpalette) {
				switch (p->palette) {
					case VIDEO_PALETTE_YUV420P:
					case VIDEO_PALETTE_RAW:
						pdev->vpalette = p->palette;
						return pwc_try_video_mode(pdev, pdev->image.x, pdev->image.y, pdev->vframes, pdev->vcompression, pdev->vsnapshot);
						break;
					default:
						return -EINVAL;
						break;
				}
			}
			break;
		}

		/* Window/size parameters */
		case VIDIOCGWIN:
		{
			struct video_window *vw = arg;

			vw->x = 0;
			vw->y = 0;
			vw->width = pdev->view.x;
			vw->height = pdev->view.y;
			vw->chromakey = 0;
			vw->flags = (pdev->vframes << PWC_FPS_SHIFT) |
				   (pdev->vsnapshot ? PWC_FPS_SNAPSHOT : 0);
			break;
		}

		case VIDIOCSWIN:
		{
			struct video_window *vw = arg;
			int fps, snapshot, ret;

			fps = (vw->flags & PWC_FPS_FRMASK) >> PWC_FPS_SHIFT;
			snapshot = vw->flags & PWC_FPS_SNAPSHOT;
			if (fps == 0)
				fps = pdev->vframes;
			if (pdev->view.x == vw->width && pdev->view.y && fps == pdev->vframes && snapshot == pdev->vsnapshot)
				return 0;
			ret = pwc_try_video_mode(pdev, vw->width, vw->height, fps, pdev->vcompression, snapshot);
			if (ret)
				return ret;
			break;
		}

		/* We don't have overlay support (yet) */
		case VIDIOCGFBUF:
		{
			struct video_buffer *vb = arg;

			memset(vb,0,sizeof(*vb));
			break;
		}

		/* mmap() functions */
		case VIDIOCGMBUF:
		{
			/* Tell the user program how much memory is needed for a mmap() */
			struct video_mbuf *vm = arg;
			int i;

			memset(vm, 0, sizeof(*vm));
			vm->size = pwc_mbufs * pdev->len_per_image;
			vm->frames = pwc_mbufs; /* double buffering should be enough for most applications */
			for (i = 0; i < pwc_mbufs; i++)
				vm->offsets[i] = i * pdev->len_per_image;
			break;
		}

		case VIDIOCMCAPTURE:
		{
			/* Start capture into a given image buffer (called 'frame' in video_mmap structure) */
			struct video_mmap *vm = arg;

			PWC_DEBUG_READ("VIDIOCMCAPTURE: %dx%d, frame %d, format %d\n", vm->width, vm->height, vm->frame, vm->format);
			if (vm->frame < 0 || vm->frame >= pwc_mbufs)
				return -EINVAL;

			/* xawtv is nasty. It probes the available palettes
			   by setting a very small image size and trying
			   various palettes... The driver doesn't support
			   such small images, so I'm working around it.
			 */
			if (vm->format)
			{
				switch (vm->format)
				{
					case VIDEO_PALETTE_YUV420P:
					case VIDEO_PALETTE_RAW:
						break;
					default:
						return -EINVAL;
						break;
				}
			}

			if ((vm->width != pdev->view.x || vm->height != pdev->view.y) &&
			    (vm->width >= pdev->view_min.x && vm->height >= pdev->view_min.y)) {
				int ret;

				PWC_DEBUG_OPEN("VIDIOCMCAPTURE: changing size to please xawtv :-(.\n");
				ret = pwc_try_video_mode(pdev, vm->width, vm->height, pdev->vframes, pdev->vcompression, pdev->vsnapshot);
				if (ret)
					return ret;
			} /* ... size mismatch */

			/* FIXME: should we lock here? */
			if (pdev->image_used[vm->frame])
				return -EBUSY;	/* buffer wasn't available. Bummer */
			pdev->image_used[vm->frame] = 1;

			/* Okay, we're done here. In the SYNC call we wait until a
			   frame comes available, then expand image into the given
			   buffer.
			   In contrast to the CPiA cam the Philips cams deliver a
			   constant stream, almost like a grabber card. Also,
			   we have separate buffers for the rawdata and the image,
			   meaning we can nearly always expand into the requested buffer.
			 */
			PWC_DEBUG_READ("VIDIOCMCAPTURE done.\n");
			break;
		}

		case VIDIOCSYNC:
		{
			/* The doc says: "Whenever a buffer is used it should
			   call VIDIOCSYNC to free this frame up and continue."

			   The only odd thing about this whole procedure is
			   that MCAPTURE flags the buffer as "in use", and
			   SYNC immediately unmarks it, while it isn't
			   after SYNC that you know that the buffer actually
			   got filled! So you better not start a CAPTURE in
			   the same frame immediately (use double buffering).
			   This is not a problem for this cam, since it has
			   extra intermediate buffers, but a hardware
			   grabber card will then overwrite the buffer
			   you're working on.
			 */
			int *mbuf = arg;
			int ret;

			PWC_DEBUG_READ("VIDIOCSYNC called (%d).\n", *mbuf);

			/* bounds check */
			if (*mbuf < 0 || *mbuf >= pwc_mbufs)
				return -EINVAL;
			/* check if this buffer was requested anyway */
			if (pdev->image_used[*mbuf] == 0)
				return -EINVAL;

			/* Add ourselves to the frame wait-queue.

			   FIXME: needs auditing for safety.
			   QUESTION: In what respect? I think that using the
				     frameq is safe now.
			 */
			add_wait_queue(&pdev->frameq, &wait);
			while (pdev->full_frames == NULL) {
				/* Check for unplugged/etc. here */
				if (pdev->error_status) {
					remove_wait_queue(&pdev->frameq, &wait);
					set_current_state(TASK_RUNNING);
					return -pdev->error_status;
				}

				if (signal_pending(current)) {
					remove_wait_queue(&pdev->frameq, &wait);
					set_current_state(TASK_RUNNING);
					return -ERESTARTSYS;
				}
				schedule();
				set_current_state(TASK_INTERRUPTIBLE);
			}
			remove_wait_queue(&pdev->frameq, &wait);
			set_current_state(TASK_RUNNING);

			/* The frame is ready. Expand in the image buffer
			   requested by the user. I don't care if you
			   mmap() 5 buffers and request data in this order:
			   buffer 4 2 3 0 1 2 3 0 4 3 1 . . .
			   Grabber hardware may not be so forgiving.
			 */
			PWC_DEBUG_READ("VIDIOCSYNC: frame ready.\n");
			pdev->fill_image = *mbuf; /* tell in which buffer we want the image to be expanded */
			/* Decompress, etc */
			ret = pwc_handle_frame(pdev);
			pdev->image_used[*mbuf] = 0;
			if (ret)
				return -EFAULT;
			break;
		}

		case VIDIOCGAUDIO:
		{
			struct video_audio *v = arg;

			strcpy(v->name, "Microphone");
			v->audio = -1; /* unknown audio minor */
			v->flags = 0;
			v->mode = VIDEO_SOUND_MONO;
			v->volume = 0;
			v->bass = 0;
			v->treble = 0;
			v->balance = 0x8000;
			v->step = 1;
			break;
		}

		case VIDIOCSAUDIO:
		{
			/* Dummy: nothing can be set */
			break;
		}

		case VIDIOCGUNIT:
		{
			struct video_unit *vu = arg;

			vu->video = pdev->vdev->minor & 0x3F;
			vu->audio = -1; /* not known yet */
			vu->vbi = -1;
			vu->radio = -1;
			vu->teletext = -1;
			break;
		}

		/* V4L2 Layer */
		case VIDIOC_QUERYCAP:
		{
		    struct v4l2_capability *cap = arg;

		    PWC_DEBUG_IOCTL("ioctl(VIDIOC_QUERYCAP) This application "\
				       "try to use the v4l2 layer\n");
		    strcpy(cap->driver,PWC_NAME);
		    strlcpy(cap->card, vdev->name, sizeof(cap->card));
		    usb_make_path(pdev->udev,cap->bus_info,sizeof(cap->bus_info));
		    cap->version = PWC_VERSION_CODE;
		    cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE	|
			V4L2_CAP_STREAMING	|
			V4L2_CAP_READWRITE;
		    return 0;
		}

		case VIDIOC_ENUMINPUT:
		{
		    struct v4l2_input *i = arg;

		    if ( i->index )	/* Only one INPUT is supported */
			  return -EINVAL;

		    memset(i, 0, sizeof(struct v4l2_input));
		    strcpy(i->name, "usb");
		    return 0;
		}

		case VIDIOC_G_INPUT:
		{
		    int *i = arg;
		    *i = 0;	/* Only one INPUT is supported */
		    return 0;
		}
		case VIDIOC_S_INPUT:
		{
			int *i = arg;

			if ( *i ) {	/* Only one INPUT is supported */
				PWC_DEBUG_IOCTL("Only one input source is"\
					" supported with this webcam.\n");
				return -EINVAL;
			}
			return 0;
		}

		/* TODO: */
		case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *c = arg;
			int i;

			PWC_DEBUG_IOCTL("ioctl(VIDIOC_QUERYCTRL) query id=%d\n", c->id);
			for (i=0; i<sizeof(pwc_controls)/sizeof(struct v4l2_queryctrl); i++) {
				if (pwc_controls[i].id == c->id) {
					PWC_DEBUG_IOCTL("ioctl(VIDIOC_QUERYCTRL) found\n");
					memcpy(c,&pwc_controls[i],sizeof(struct v4l2_queryctrl));
					return 0;
				}
			}
			PWC_DEBUG_IOCTL("ioctl(VIDIOC_QUERYCTRL) not found\n");

			return -EINVAL;
		}
		case VIDIOC_G_CTRL:
		{
			struct v4l2_control *c = arg;
			int ret;

			switch (c->id)
			{
				case V4L2_CID_BRIGHTNESS:
					c->value = pwc_get_brightness(pdev);
					if (c->value<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_CONTRAST:
					c->value = pwc_get_contrast(pdev);
					if (c->value<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_SATURATION:
					ret = pwc_get_saturation(pdev, &c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_GAMMA:
					c->value = pwc_get_gamma(pdev);
					if (c->value<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_RED_BALANCE:
					ret = pwc_get_red_gain(pdev, &c->value);
					if (ret<0)
						return -EINVAL;
					c->value >>= 8;
					return 0;
				case V4L2_CID_BLUE_BALANCE:
					ret = pwc_get_blue_gain(pdev, &c->value);
					if (ret<0)
						return -EINVAL;
					c->value >>= 8;
					return 0;
				case V4L2_CID_AUTO_WHITE_BALANCE:
					ret = pwc_get_awb(pdev);
					if (ret<0)
						return -EINVAL;
					c->value = (ret == PWC_WB_MANUAL)?0:1;
					return 0;
				case V4L2_CID_GAIN:
					ret = pwc_get_agc(pdev, &c->value);
					if (ret<0)
						return -EINVAL;
					c->value >>= 8;
					return 0;
				case V4L2_CID_AUTOGAIN:
					ret = pwc_get_agc(pdev, &c->value);
					if (ret<0)
						return -EINVAL;
					c->value = (c->value < 0)?1:0;
					return 0;
				case V4L2_CID_EXPOSURE:
					ret = pwc_get_shutter_speed(pdev, &c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_PRIVATE_COLOUR_MODE:
					ret = pwc_get_colour_mode(pdev, &c->value);
					if (ret < 0)
						return -EINVAL;
					return 0;
				case V4L2_CID_PRIVATE_AUTOCONTOUR:
					ret = pwc_get_contour(pdev, &c->value);
					if (ret < 0)
						return -EINVAL;
					c->value=(c->value == -1?1:0);
					return 0;
				case V4L2_CID_PRIVATE_CONTOUR:
					ret = pwc_get_contour(pdev, &c->value);
					if (ret < 0)
						return -EINVAL;
					c->value >>= 10;
					return 0;
				case V4L2_CID_PRIVATE_BACKLIGHT:
					ret = pwc_get_backlight(pdev, &c->value);
					if (ret < 0)
						return -EINVAL;
					return 0;
				case V4L2_CID_PRIVATE_FLICKERLESS:
					ret = pwc_get_flicker(pdev, &c->value);
					if (ret < 0)
						return -EINVAL;
					c->value=(c->value?1:0);
					return 0;
				case V4L2_CID_PRIVATE_NOISE_REDUCTION:
					ret = pwc_get_dynamic_noise(pdev, &c->value);
					if (ret < 0)
						return -EINVAL;
					return 0;

				case V4L2_CID_PRIVATE_SAVE_USER:
				case V4L2_CID_PRIVATE_RESTORE_USER:
				case V4L2_CID_PRIVATE_RESTORE_FACTORY:
					return -EINVAL;
			}
			return -EINVAL;
		}
		case VIDIOC_S_CTRL:
		{
			struct v4l2_control *c = arg;
			int ret;

			switch (c->id)
			{
				case V4L2_CID_BRIGHTNESS:
					c->value <<= 9;
					ret = pwc_set_brightness(pdev, c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_CONTRAST:
					c->value <<= 10;
					ret = pwc_set_contrast(pdev, c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_SATURATION:
					ret = pwc_set_saturation(pdev, c->value);
					if (ret<0)
					  return -EINVAL;
					return 0;
				case V4L2_CID_GAMMA:
					c->value <<= 11;
					ret = pwc_set_gamma(pdev, c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_RED_BALANCE:
					c->value <<= 8;
					ret = pwc_set_red_gain(pdev, c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_BLUE_BALANCE:
					c->value <<= 8;
					ret = pwc_set_blue_gain(pdev, c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_AUTO_WHITE_BALANCE:
					c->value = (c->value == 0)?PWC_WB_MANUAL:PWC_WB_AUTO;
					ret = pwc_set_awb(pdev, c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_EXPOSURE:
					c->value <<= 8;
					ret = pwc_set_shutter_speed(pdev, c->value?0:1, c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_AUTOGAIN:
					/* autogain off means nothing without a gain */
					if (c->value == 0)
						return 0;
					ret = pwc_set_agc(pdev, c->value, 0);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_GAIN:
					c->value <<= 8;
					ret = pwc_set_agc(pdev, 0, c->value);
					if (ret<0)
						return -EINVAL;
					return 0;
				case V4L2_CID_PRIVATE_SAVE_USER:
					if (pwc_save_user(pdev))
						return -EINVAL;
					return 0;
				case V4L2_CID_PRIVATE_RESTORE_USER:
					if (pwc_restore_user(pdev))
						return -EINVAL;
					return 0;
				case V4L2_CID_PRIVATE_RESTORE_FACTORY:
					if (pwc_restore_factory(pdev))
						return -EINVAL;
					return 0;
				case V4L2_CID_PRIVATE_COLOUR_MODE:
					ret = pwc_set_colour_mode(pdev, c->value);
					if (ret < 0)
					  return -EINVAL;
					return 0;
				case V4L2_CID_PRIVATE_AUTOCONTOUR:
				  c->value=(c->value == 1)?-1:0;
				  ret = pwc_set_contour(pdev, c->value);
				  if (ret < 0)
				    return -EINVAL;
				  return 0;
				case V4L2_CID_PRIVATE_CONTOUR:
				  c->value <<= 10;
				  ret = pwc_set_contour(pdev, c->value);
				  if (ret < 0)
				    return -EINVAL;
				  return 0;
				case V4L2_CID_PRIVATE_BACKLIGHT:
				  ret = pwc_set_backlight(pdev, c->value);
				  if (ret < 0)
				    return -EINVAL;
				  return 0;
				case V4L2_CID_PRIVATE_FLICKERLESS:
				  ret = pwc_set_flicker(pdev, c->value);
				  if (ret < 0)
				    return -EINVAL;
				case V4L2_CID_PRIVATE_NOISE_REDUCTION:
				  ret = pwc_set_dynamic_noise(pdev, c->value);
				  if (ret < 0)
				    return -EINVAL;
				  return 0;

			}
			return -EINVAL;
		}

		case VIDIOC_ENUM_FMT:
		{
			struct v4l2_fmtdesc *f = arg;
			int index;

			if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			      return -EINVAL;

			/* We only support two format: the raw format, and YUV */
			index = f->index;
			memset(f,0,sizeof(struct v4l2_fmtdesc));
			f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			f->index = index;
			switch(index)
			{
				case 0:
					/* RAW format */
					f->pixelformat = pdev->type<=646?V4L2_PIX_FMT_PWC1:V4L2_PIX_FMT_PWC2;
					f->flags = V4L2_FMT_FLAG_COMPRESSED;
					strlcpy(f->description,"Raw Philips Webcam",sizeof(f->description));
					break;
				case 1:
					f->pixelformat = V4L2_PIX_FMT_YUV420;
					strlcpy(f->description,"4:2:0, planar, Y-Cb-Cr",sizeof(f->description));
					break;
				default:
					return -EINVAL;
			}
			return 0;
		}

		case VIDIOC_G_FMT:
		{
			struct v4l2_format *f = arg;

			PWC_DEBUG_IOCTL("ioctl(VIDIOC_G_FMT) return size %dx%d\n",pdev->image.x,pdev->image.y);
			if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			      return -EINVAL;

			pwc_vidioc_fill_fmt(pdev, f);

			return 0;
		}

		case VIDIOC_TRY_FMT:
			return pwc_vidioc_try_fmt(pdev, arg);

		case VIDIOC_S_FMT:
			return pwc_vidioc_set_fmt(pdev, arg);

		case VIDIOC_G_STD:
		{
			v4l2_std_id *std = arg;
			*std = V4L2_STD_UNKNOWN;
			return 0;
		}

		case VIDIOC_S_STD:
		{
			v4l2_std_id *std = arg;
			if (*std != V4L2_STD_UNKNOWN)
				return -EINVAL;
			return 0;
		}

		case VIDIOC_ENUMSTD:
		{
			struct v4l2_standard *std = arg;
			if (std->index != 0)
				return -EINVAL;
			std->id = V4L2_STD_UNKNOWN;
			strncpy(std->name, "webcam", sizeof(std->name));
			return 0;
		}

		case VIDIOC_REQBUFS:
		{
			struct v4l2_requestbuffers *rb = arg;
			int nbuffers;

			PWC_DEBUG_IOCTL("ioctl(VIDIOC_REQBUFS) count=%d\n",rb->count);
			if (rb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
				return -EINVAL;
			if (rb->memory != V4L2_MEMORY_MMAP)
				return -EINVAL;

			nbuffers = rb->count;
			if (nbuffers < 2)
				nbuffers = 2;
			else if (nbuffers > pwc_mbufs)
				nbuffers = pwc_mbufs;
			/* Force to use our # of buffers */
			rb->count = pwc_mbufs;
			return 0;
		}

		case VIDIOC_QUERYBUF:
		{
			struct v4l2_buffer *buf = arg;
			int index;

			PWC_DEBUG_IOCTL("ioctl(VIDIOC_QUERYBUF) index=%d\n",buf->index);
			if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
				PWC_DEBUG_IOCTL("ioctl(VIDIOC_QUERYBUF) Bad type\n");
				return -EINVAL;
			}
			if (buf->memory != V4L2_MEMORY_MMAP) {
				PWC_DEBUG_IOCTL("ioctl(VIDIOC_QUERYBUF) Bad memory type\n");
				return -EINVAL;
			}
			index = buf->index;
			if (index < 0 || index >= pwc_mbufs) {
				PWC_DEBUG_IOCTL("ioctl(VIDIOC_QUERYBUF) Bad index %d\n", buf->index);
				return -EINVAL;
			}

			memset(buf, 0, sizeof(struct v4l2_buffer));
			buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf->index = index;
			buf->m.offset = index * pdev->len_per_image;
			if (pdev->vpalette == VIDEO_PALETTE_RAW)
				buf->bytesused = pdev->frame_size + sizeof(struct pwc_raw_frame);
			else
				buf->bytesused = pdev->view.size;
			buf->field = V4L2_FIELD_NONE;
			buf->memory = V4L2_MEMORY_MMAP;
			//buf->flags = V4L2_BUF_FLAG_MAPPED;
			buf->length = pdev->len_per_image;

			PWC_DEBUG_READ("VIDIOC_QUERYBUF: index=%d\n",buf->index);
			PWC_DEBUG_READ("VIDIOC_QUERYBUF: m.offset=%d\n",buf->m.offset);
			PWC_DEBUG_READ("VIDIOC_QUERYBUF: bytesused=%d\n",buf->bytesused);

			return 0;
		}

		case VIDIOC_QBUF:
		{
			struct v4l2_buffer *buf = arg;

			PWC_DEBUG_IOCTL("ioctl(VIDIOC_QBUF) index=%d\n",buf->index);
			if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
				return -EINVAL;
			if (buf->memory != V4L2_MEMORY_MMAP)
				return -EINVAL;
			if (buf->index >= pwc_mbufs)
				return -EINVAL;

			buf->flags |= V4L2_BUF_FLAG_QUEUED;
			buf->flags &= ~V4L2_BUF_FLAG_DONE;

			return 0;
		}

		case VIDIOC_DQBUF:
		{
			struct v4l2_buffer *buf = arg;
			int ret;

			PWC_DEBUG_IOCTL("ioctl(VIDIOC_DQBUF)\n");

			if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
				return -EINVAL;

			/* Add ourselves to the frame wait-queue.

			   FIXME: needs auditing for safety.
			   QUESTION: In what respect? I think that using the
				     frameq is safe now.
			 */
			add_wait_queue(&pdev->frameq, &wait);
			while (pdev->full_frames == NULL) {
				if (pdev->error_status) {
					remove_wait_queue(&pdev->frameq, &wait);
					set_current_state(TASK_RUNNING);
					return -pdev->error_status;
				}

				if (signal_pending(current)) {
					remove_wait_queue(&pdev->frameq, &wait);
					set_current_state(TASK_RUNNING);
					return -ERESTARTSYS;
				}
				schedule();
				set_current_state(TASK_INTERRUPTIBLE);
			}
			remove_wait_queue(&pdev->frameq, &wait);
			set_current_state(TASK_RUNNING);

			PWC_DEBUG_IOCTL("VIDIOC_DQBUF: frame ready.\n");
			/* Decompress data in pdev->images[pdev->fill_image] */
			ret = pwc_handle_frame(pdev);
			if (ret)
				return -EFAULT;
			PWC_DEBUG_IOCTL("VIDIOC_DQBUF: after pwc_handle_frame\n");

			buf->index = pdev->fill_image;
			if (pdev->vpalette == VIDEO_PALETTE_RAW)
				buf->bytesused = pdev->frame_size + sizeof(struct pwc_raw_frame);
			else
				buf->bytesused = pdev->view.size;
			buf->flags = V4L2_BUF_FLAG_MAPPED;
			buf->field = V4L2_FIELD_NONE;
			do_gettimeofday(&buf->timestamp);
			buf->sequence = 0;
			buf->memory = V4L2_MEMORY_MMAP;
			buf->m.offset = pdev->fill_image * pdev->len_per_image;
			buf->length = pdev->len_per_image;
			pwc_next_image(pdev);

			PWC_DEBUG_IOCTL("VIDIOC_DQBUF: buf->index=%d\n",buf->index);
			PWC_DEBUG_IOCTL("VIDIOC_DQBUF: buf->length=%d\n",buf->length);
			PWC_DEBUG_IOCTL("VIDIOC_DQBUF: m.offset=%d\n",buf->m.offset);
			PWC_DEBUG_IOCTL("VIDIOC_DQBUF: bytesused=%d\n",buf->bytesused);
			PWC_DEBUG_IOCTL("VIDIOC_DQBUF: leaving\n");
			return 0;

		}

		case VIDIOC_STREAMON:
		{
			/* WARNING: pwc_try_video_mode() called pwc_isoc_init */
			pwc_isoc_init(pdev);
			return 0;
		}

		case VIDIOC_STREAMOFF:
		{
			pwc_isoc_cleanup(pdev);
			return 0;
		}

		case VIDIOC_ENUM_FRAMESIZES:
		{
			struct v4l2_frmsizeenum *fsize = arg;
			unsigned int i = 0, index = fsize->index;

			if (fsize->pixel_format == V4L2_PIX_FMT_YUV420) {
				for (i = 0; i < PSZ_MAX; i++) {
					if (pdev->image_mask & (1UL << i)) {
						if (!index--) {
							fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
							fsize->discrete.width = pwc_image_sizes[i].x;
							fsize->discrete.height = pwc_image_sizes[i].y;
							return 0;
						}
					}
				}
			} else if (fsize->index == 0 &&
				   ((fsize->pixel_format == V4L2_PIX_FMT_PWC1 && DEVICE_USE_CODEC1(pdev->type)) ||
				    (fsize->pixel_format == V4L2_PIX_FMT_PWC2 && DEVICE_USE_CODEC23(pdev->type)))) {

				fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
				fsize->discrete.width = pdev->abs_max.x;
				fsize->discrete.height = pdev->abs_max.y;
				return 0;
			}
			return -EINVAL;
		}

		case VIDIOC_ENUM_FRAMEINTERVALS:
		{
			struct v4l2_frmivalenum *fival = arg;
			int size = -1;
			unsigned int i;

			for (i = 0; i < PSZ_MAX; i++) {
				if (pwc_image_sizes[i].x == fival->width &&
				    pwc_image_sizes[i].y == fival->height) {
					size = i;
					break;
				}
			}

			/* TODO: Support raw format */
			if (size < 0 || fival->pixel_format != V4L2_PIX_FMT_YUV420) {
				return -EINVAL;
			}

			i = pwc_get_fps(pdev, fival->index, size);
			if (!i)
				return -EINVAL;

			fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
			fival->discrete.numerator = 1;
			fival->discrete.denominator = i;

			return 0;
		}

		default:
			return pwc_ioctl(pdev, cmd, arg);
	} /* ..switch */
	return 0;
}

/* vim: set cino= formatoptions=croql cindent shiftwidth=8 tabstop=8: */
