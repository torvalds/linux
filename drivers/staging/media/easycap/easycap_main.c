/******************************************************************************
*                                                                             *
*  easycap_main.c                                                             *
*                                                                             *
*  Video driver for EasyCAP USB2.0 Video Capture Device DC60                  *
*                                                                             *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/

#include "easycap.h"
#include <linux/usb/audio.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("R.M. Thomas <rmthomas@sciolus.org>");
MODULE_DESCRIPTION(EASYCAP_DRIVER_DESCRIPTION);
MODULE_VERSION(EASYCAP_DRIVER_VERSION);

#ifdef CONFIG_EASYCAP_DEBUG
int easycap_debug;
module_param_named(debug, easycap_debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level: 0(default),1,2,...,9");
#endif /* CONFIG_EASYCAP_DEBUG */

bool easycap_readback;
module_param_named(readback, easycap_readback, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(readback, "read back written registers: (default false)");

static int easycap_bars = 1;
module_param_named(bars, easycap_bars, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bars,
	"Testcard bars on input signal failure: 0=>no, 1=>yes(default)");

static int easycap_gain = 16;
module_param_named(gain, easycap_gain, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(gain, "Audio gain: 0,...,16(default),...31");

static bool easycap_ntsc;
module_param_named(ntsc, easycap_ntsc, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ntsc, "NTSC default encoding (default PAL)");



struct easycap_dongle easycapdc60_dongle[DONGLE_MANY];
static struct mutex mutex_dongle;
static void easycap_complete(struct urb *purb);
static int reset(struct easycap *peasycap);
static int field2frame(struct easycap *peasycap);
static int redaub(struct easycap *peasycap,
		void *pad, void *pex, int much, int more,
		u8 mask, u8 margin, bool isuy);

const char *strerror(int err)
{
#define ERRNOSTR(_e) case _e: return # _e
	switch (err) {
	case 0: return "OK";
	ERRNOSTR(ENOMEM);
	ERRNOSTR(ENODEV);
	ERRNOSTR(ENXIO);
	ERRNOSTR(EINVAL);
	ERRNOSTR(EAGAIN);
	ERRNOSTR(EFBIG);
	ERRNOSTR(EPIPE);
	ERRNOSTR(EMSGSIZE);
	ERRNOSTR(ENOSPC);
	ERRNOSTR(EINPROGRESS);
	ERRNOSTR(ENOSR);
	ERRNOSTR(EOVERFLOW);
	ERRNOSTR(EPROTO);
	ERRNOSTR(EILSEQ);
	ERRNOSTR(ETIMEDOUT);
	ERRNOSTR(EOPNOTSUPP);
	ERRNOSTR(EPFNOSUPPORT);
	ERRNOSTR(EAFNOSUPPORT);
	ERRNOSTR(EADDRINUSE);
	ERRNOSTR(EADDRNOTAVAIL);
	ERRNOSTR(ENOBUFS);
	ERRNOSTR(EISCONN);
	ERRNOSTR(ENOTCONN);
	ERRNOSTR(ESHUTDOWN);
	ERRNOSTR(ENOENT);
	ERRNOSTR(ECONNRESET);
	ERRNOSTR(ETIME);
	ERRNOSTR(ECOMM);
	ERRNOSTR(EREMOTEIO);
	ERRNOSTR(EXDEV);
	ERRNOSTR(EPERM);
	default: return "unknown";
	}

#undef ERRNOSTR
}

/****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  THIS ROUTINE DOES NOT DETECT DUPLICATE OCCURRENCES OF POINTER peasycap
*/
/*---------------------------------------------------------------------------*/
int easycap_isdongle(struct easycap *peasycap)
{
	int k;
	if (!peasycap)
		return -2;
	for (k = 0; k < DONGLE_MANY; k++) {
		if (easycapdc60_dongle[k].peasycap == peasycap) {
			peasycap->isdongle = k;
			return k;
		}
	}
	return -1;
}
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
static int easycap_open(struct inode *inode, struct file *file)
{
	struct video_device *pvideo_device;
	struct easycap *peasycap;
	int rc;

	JOT(4, "\n");
	SAY("==========OPEN=========\n");

	pvideo_device = video_devdata(file);
	if (!pvideo_device) {
		SAY("ERROR: pvideo_device is NULL.\n");
		return -EFAULT;
	}
	peasycap = (struct easycap *)video_get_drvdata(pvideo_device);
	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}

	JOM(16, "peasycap->pusb_device=%p\n", peasycap->pusb_device);

	file->private_data = peasycap;
	rc = easycap_wakeup_device(peasycap->pusb_device);
	if (rc) {
		SAM("ERROR: wakeup_device() rc = %i\n", rc);
		if (-ENODEV == rc)
			SAM("ERROR: wakeup_device() returned -ENODEV\n");
		else
			SAM("ERROR: wakeup_device() rc = %i\n", rc);
		return rc;
	}
	JOM(8, "wakeup_device() OK\n");
	peasycap->input = 0;
	rc = reset(peasycap);
	if (rc) {
		SAM("ERROR: reset() rc = %i\n", rc);
		return -EFAULT;
	}
	return 0;
}

/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  RESET THE HARDWARE TO ITS REFERENCE STATE.
 *
 *  THIS ROUTINE MAY BE CALLED REPEATEDLY IF easycap_complete() DETECTS
 *  A BAD VIDEO FRAME SIZE.
*/
/*---------------------------------------------------------------------------*/
static int reset(struct easycap *peasycap)
{
	struct easycap_standard const *peasycap_standard;
	int fmtidx, input, rate;
	bool ntsc, other;
	int rc;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	input = peasycap->input;

/*---------------------------------------------------------------------------*/
/*
 *  IF THE SAA7113H HAS ALREADY ACQUIRED SYNC, USE ITS HARDWARE-DETECTED
 *  FIELD FREQUENCY TO DISTINGUISH NTSC FROM PAL.  THIS IS ESSENTIAL FOR
 *  gstreamer AND OTHER USERSPACE PROGRAMS WHICH MAY NOT ATTEMPT TO INITIATE
 *  A SWITCH BETWEEN PAL AND NTSC.
 *
 *  FUNCTION ready_saa() MAY REQUIRE A SUBSTANTIAL FRACTION OF A SECOND TO
 *  COMPLETE, SO SHOULD NOT BE INVOKED WITHOUT GOOD REASON.
*/
/*---------------------------------------------------------------------------*/
	other = false;
	JOM(8, "peasycap->ntsc=%d\n", peasycap->ntsc);

	rate = ready_saa(peasycap->pusb_device);
	if (rate < 0) {
		JOM(8, "not ready to capture after %i ms ...\n", PATIENCE);
		ntsc = !peasycap->ntsc;
		JOM(8, "... trying  %s ..\n", ntsc ? "NTSC" : "PAL");
		rc = setup_stk(peasycap->pusb_device, ntsc);
		if (rc) {
			SAM("ERROR: setup_stk() rc = %i\n", rc);
			return -EFAULT;
		}
		rc = setup_saa(peasycap->pusb_device, ntsc);
		if (rc) {
			SAM("ERROR: setup_saa() rc = %i\n", rc);
			return -EFAULT;
		}

		rate = ready_saa(peasycap->pusb_device);
		if (rate < 0) {
			JOM(8, "not ready to capture after %i ms\n", PATIENCE);
			JOM(8, "... saa register 0x1F has 0x%02X\n",
					read_saa(peasycap->pusb_device, 0x1F));
			ntsc = peasycap->ntsc;
		} else {
			JOM(8, "... success at second try:  %i=rate\n", rate);
			ntsc = (0 < (rate/2)) ? true : false ;
			other = true;
		}
	} else {
		JOM(8, "... success at first try:  %i=rate\n", rate);
		ntsc = (0 < rate/2) ? true : false ;
	}
	JOM(8, "ntsc=%d\n", ntsc);
/*---------------------------------------------------------------------------*/

	rc = setup_stk(peasycap->pusb_device, ntsc);
	if (rc) {
		SAM("ERROR: setup_stk() rc = %i\n", rc);
		return -EFAULT;
	}
	rc = setup_saa(peasycap->pusb_device, ntsc);
	if (rc) {
		SAM("ERROR: setup_saa() rc = %i\n", rc);
		return -EFAULT;
	}

	memset(peasycap->merit, 0, sizeof(peasycap->merit));

	peasycap->video_eof = 0;
	peasycap->audio_eof = 0;
/*---------------------------------------------------------------------------*/
/*
 * RESTORE INPUT AND FORCE REFRESH OF STANDARD, FORMAT, ETC.
 *
 * WHILE THIS PROCEDURE IS IN PROGRESS, SOME IOCTL COMMANDS WILL RETURN -EBUSY.
*/
/*---------------------------------------------------------------------------*/
	peasycap->input = -8192;
	peasycap->standard_offset = -8192;
	fmtidx = ntsc ? NTSC_M : PAL_BGHIN;
	if (other) {
		peasycap_standard = &easycap_standard[0];
		while (0xFFFF != peasycap_standard->mask) {
			if (fmtidx == peasycap_standard->v4l2_standard.index) {
				peasycap->inputset[input].standard_offset =
					peasycap_standard - easycap_standard;
				break;
			}
			peasycap_standard++;
		}
		if (0xFFFF == peasycap_standard->mask) {
			SAM("ERROR: standard not found\n");
			return -EINVAL;
		}
		JOM(8, "%i=peasycap->inputset[%i].standard_offset\n",
			peasycap->inputset[input].standard_offset, input);
	}
	peasycap->format_offset = -8192;
	peasycap->brightness = -8192;
	peasycap->contrast = -8192;
	peasycap->saturation = -8192;
	peasycap->hue = -8192;

	rc = easycap_newinput(peasycap, input);

	if (rc) {
		SAM("ERROR: newinput(.,%i) rc = %i\n", rc, input);
		return -EFAULT;
	}
	JOM(4, "restored input, standard and format\n");

	JOM(8, "true=peasycap->ntsc %d\n", peasycap->ntsc);

	if (0 > peasycap->input) {
		SAM("MISTAKE:  %i=peasycap->input\n", peasycap->input);
		return -ENOENT;
	}
	if (0 > peasycap->standard_offset) {
		SAM("MISTAKE:  %i=peasycap->standard_offset\n",
				peasycap->standard_offset);
		return -ENOENT;
	}
	if (0 > peasycap->format_offset) {
		SAM("MISTAKE:  %i=peasycap->format_offset\n",
				peasycap->format_offset);
		return -ENOENT;
	}
	if (0 > peasycap->brightness) {
		SAM("MISTAKE:  %i=peasycap->brightness\n",
				peasycap->brightness);
		return -ENOENT;
	}
	if (0 > peasycap->contrast) {
		SAM("MISTAKE:  %i=peasycap->contrast\n", peasycap->contrast);
		return -ENOENT;
	}
	if (0 > peasycap->saturation) {
		SAM("MISTAKE:  %i=peasycap->saturation\n",
				peasycap->saturation);
		return -ENOENT;
	}
	if (0 > peasycap->hue) {
		SAM("MISTAKE:  %i=peasycap->hue\n", peasycap->hue);
		return -ENOENT;
	}
	return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  IF THE REQUESTED INPUT IS THE SAME AS THE EXISTING INPUT, DO NOTHING.
 *  OTHERWISE:
 *      KILL URBS, CLEAR FIELD AND FRAME BUFFERS AND RESET THEIR
 *           _read AND _fill POINTERS.
 *      SELECT THE NEW INPUT.
 *      ADJUST THE STANDARD, FORMAT, BRIGHTNESS, CONTRAST, SATURATION AND HUE
 *          ON THE BASIS OF INFORMATION IN STRUCTURE easycap.inputset[input].
 *      RESUBMIT THE URBS IF STREAMING WAS ALREADY IN PROGRESS.
 *
 *  NOTE:
 *      THIS ROUTINE MAY BE CALLED FREQUENTLY BY ZONEMINDER VIA IOCTL,
 *      SO IT SHOULD WRITE ONLY SPARINGLY TO THE LOGFILE.
*/
/*---------------------------------------------------------------------------*/
int easycap_newinput(struct easycap *peasycap, int input)
{
	int rc, k, m, mood, off;
	int inputnow, video_idlenow, audio_idlenow;
	bool resubmit;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	JOM(8, "%i=input sought\n", input);

	if (0 > input && INPUT_MANY <= input)
		return -ENOENT;
	inputnow = peasycap->input;
	if (input == inputnow)
		return 0;
/*---------------------------------------------------------------------------*/
/*
 *  IF STREAMING IS IN PROGRESS THE URBS ARE KILLED AT THIS
 *  STAGE AND WILL BE RESUBMITTED PRIOR TO EXIT FROM THE ROUTINE.
 *  IF NO STREAMING IS IN PROGRESS NO URBS WILL BE SUBMITTED BY THE
 *  ROUTINE.
*/
/*---------------------------------------------------------------------------*/
	video_idlenow = peasycap->video_idle;
	audio_idlenow = peasycap->audio_idle;

	peasycap->video_idle = 1;
	peasycap->audio_idle = 1;
	if (peasycap->video_isoc_streaming) {
		resubmit = true;
		easycap_video_kill_urbs(peasycap);
	} else {
		resubmit = false;
	}
/*---------------------------------------------------------------------------*/
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -ENODEV;
	}
	rc = usb_set_interface(peasycap->pusb_device,
				peasycap->video_interface,
				peasycap->video_altsetting_off);
	if (rc) {
		SAM("ERROR: usb_set_interface() rc = %i\n", rc);
		return -EFAULT;
	}
	rc = stop_100(peasycap->pusb_device);
	if (rc) {
		SAM("ERROR: stop_100() rc = %i\n", rc);
		return -EFAULT;
	}
	for (k = 0; k < FIELD_BUFFER_MANY; k++) {
		for (m = 0; m < FIELD_BUFFER_SIZE/PAGE_SIZE; m++)
			memset(peasycap->field_buffer[k][m].pgo, 0, PAGE_SIZE);
	}
	for (k = 0; k < FRAME_BUFFER_MANY; k++) {
		for (m = 0; m < FRAME_BUFFER_SIZE/PAGE_SIZE; m++)
			memset(peasycap->frame_buffer[k][m].pgo, 0, PAGE_SIZE);
	}
	peasycap->field_page = 0;
	peasycap->field_read = 0;
	peasycap->field_fill = 0;

	peasycap->frame_read = 0;
	peasycap->frame_fill = 0;
	for (k = 0; k < peasycap->input; k++) {
		(peasycap->frame_fill)++;
		if (peasycap->frame_buffer_many <= peasycap->frame_fill)
			peasycap->frame_fill = 0;
	}
	peasycap->input = input;
	select_input(peasycap->pusb_device, peasycap->input, 9);
/*---------------------------------------------------------------------------*/
	if (input == peasycap->inputset[input].input) {
		off = peasycap->inputset[input].standard_offset;
		if (off != peasycap->standard_offset) {
			rc = adjust_standard(peasycap,
				easycap_standard[off].v4l2_standard.id);
			if (rc) {
				SAM("ERROR: adjust_standard() rc = %i\n", rc);
				return -EFAULT;
			}
			JOM(8, "%i=peasycap->standard_offset\n",
				peasycap->standard_offset);
		} else {
			JOM(8, "%i=peasycap->standard_offset unchanged\n",
						peasycap->standard_offset);
		}
		off = peasycap->inputset[input].format_offset;
		if (off != peasycap->format_offset) {
			struct v4l2_pix_format *pix =
				&easycap_format[off].v4l2_format.fmt.pix;
			rc = adjust_format(peasycap,
				pix->width, pix->height,
				pix->pixelformat, pix->field, false);
			if (0 > rc) {
				SAM("ERROR: adjust_format() rc = %i\n", rc);
				return -EFAULT;
			}
			JOM(8, "%i=peasycap->format_offset\n",
					peasycap->format_offset);
		} else {
			JOM(8, "%i=peasycap->format_offset unchanged\n",
					peasycap->format_offset);
		}
		mood = peasycap->inputset[input].brightness;
		if (mood != peasycap->brightness) {
			rc = adjust_brightness(peasycap, mood);
			if (rc) {
				SAM("ERROR: adjust_brightness rc = %i\n", rc);
				return -EFAULT;
			}
			JOM(8, "%i=peasycap->brightness\n",
					peasycap->brightness);
		}
		mood = peasycap->inputset[input].contrast;
		if (mood != peasycap->contrast) {
			rc = adjust_contrast(peasycap, mood);
			if (rc) {
				SAM("ERROR: adjust_contrast rc = %i\n", rc);
				return -EFAULT;
			}
			JOM(8, "%i=peasycap->contrast\n", peasycap->contrast);
		}
		mood = peasycap->inputset[input].saturation;
		if (mood != peasycap->saturation) {
			rc = adjust_saturation(peasycap, mood);
			if (rc) {
				SAM("ERROR: adjust_saturation rc = %i\n", rc);
				return -EFAULT;
			}
			JOM(8, "%i=peasycap->saturation\n",
					peasycap->saturation);
		}
		mood = peasycap->inputset[input].hue;
		if (mood != peasycap->hue) {
			rc = adjust_hue(peasycap, mood);
			if (rc) {
				SAM("ERROR: adjust_hue rc = %i\n", rc);
				return -EFAULT;
			}
			JOM(8, "%i=peasycap->hue\n", peasycap->hue);
		}
	} else {
		SAM("MISTAKE: easycap.inputset[%i] unpopulated\n", input);
		return -ENOENT;
	}
/*---------------------------------------------------------------------------*/
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -ENODEV;
	}
	rc = usb_set_interface(peasycap->pusb_device,
				peasycap->video_interface,
				peasycap->video_altsetting_on);
	if (rc) {
		SAM("ERROR: usb_set_interface() rc = %i\n", rc);
		return -EFAULT;
	}
	rc = start_100(peasycap->pusb_device);
	if (rc) {
		SAM("ERROR: start_100() rc = %i\n", rc);
		return -EFAULT;
	}
	if (resubmit)
		easycap_video_submit_urbs(peasycap);

	peasycap->video_isoc_sequence = VIDEO_ISOC_BUFFER_MANY - 1;
	peasycap->video_idle = video_idlenow;
	peasycap->audio_idle = audio_idlenow;
	peasycap->video_junk = 0;

	return 0;
}
/*****************************************************************************/
int easycap_video_submit_urbs(struct easycap *peasycap)
{
	struct data_urb *pdata_urb;
	struct urb *purb;
	struct list_head *plist_head;
	int j, isbad, nospc, m, rc;
	int isbuf;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}

	if (!peasycap->purb_video_head) {
		SAY("ERROR: peasycap->urb_video_head uninitialized\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAY("ERROR: peasycap->pusb_device is NULL\n");
		return -ENODEV;
	}
	if (!peasycap->video_isoc_streaming) {
		JOM(4, "submission of all video urbs\n");
		isbad = 0;  nospc = 0;  m = 0;
		list_for_each(plist_head, (peasycap->purb_video_head)) {
			pdata_urb = list_entry(plist_head,
						struct data_urb, list_head);
			if (pdata_urb && pdata_urb->purb) {
				purb = pdata_urb->purb;
				isbuf = pdata_urb->isbuf;
				purb->interval = 1;
				purb->dev = peasycap->pusb_device;
				purb->pipe =
					usb_rcvisocpipe(peasycap->pusb_device,
					peasycap->video_endpointnumber);
				purb->transfer_flags = URB_ISO_ASAP;
				purb->transfer_buffer =
					peasycap->video_isoc_buffer[isbuf].pgo;
				purb->transfer_buffer_length =
					peasycap->video_isoc_buffer_size;
				purb->complete = easycap_complete;
				purb->context = peasycap;
				purb->start_frame = 0;
				purb->number_of_packets =
					peasycap->video_isoc_framesperdesc;

				for (j = 0;  j < peasycap->video_isoc_framesperdesc; j++) {
					purb->iso_frame_desc[j]. offset =
						j * peasycap->video_isoc_maxframesize;
					purb->iso_frame_desc[j]. length =
						peasycap->video_isoc_maxframesize;
				}

				rc = usb_submit_urb(purb, GFP_KERNEL);
				if (rc) {
					isbad++;
					SAM("ERROR: usb_submit_urb() failed "
						"for urb with rc:-%s\n",
							strerror(rc));
					if (rc == -ENOSPC)
						nospc++;
				} else {
					m++;
				}
			} else {
				isbad++;
			}
		}
		if (nospc) {
			SAM("-ENOSPC=usb_submit_urb() for %i urbs\n", nospc);
			SAM(".....  possibly inadequate USB bandwidth\n");
			peasycap->video_eof = 1;
		}

		if (isbad)
			easycap_video_kill_urbs(peasycap);
		else
			peasycap->video_isoc_streaming = 1;
	} else {
		JOM(4, "already streaming video urbs\n");
	}
	return 0;
}
/*****************************************************************************/
int easycap_audio_kill_urbs(struct easycap *peasycap)
{
	int m;
	struct list_head *plist_head;
	struct data_urb *pdata_urb;

	if (!peasycap->audio_isoc_streaming)
		return 0;

	if (!peasycap->purb_audio_head) {
		SAM("ERROR: peasycap->purb_audio_head is NULL\n");
		return -EFAULT;
	}

	peasycap->audio_isoc_streaming = 0;
	m = 0;
	list_for_each(plist_head, peasycap->purb_audio_head) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if (pdata_urb && pdata_urb->purb) {
			usb_kill_urb(pdata_urb->purb);
			m++;
		}
	}

	JOM(4, "%i audio urbs killed\n", m);

	return 0;
}
int easycap_video_kill_urbs(struct easycap *peasycap)
{
	int m;
	struct list_head *plist_head;
	struct data_urb *pdata_urb;

	if (!peasycap->video_isoc_streaming)
		return 0;

	if (!peasycap->purb_video_head) {
		SAM("ERROR: peasycap->purb_video_head is NULL\n");
		return -EFAULT;
	}

	peasycap->video_isoc_streaming = 0;
	JOM(4, "killing video urbs\n");
	m = 0;
	list_for_each(plist_head, (peasycap->purb_video_head)) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if (pdata_urb && pdata_urb->purb) {
			usb_kill_urb(pdata_urb->purb);
			m++;
		}
	}
	JOM(4, "%i video urbs killed\n", m);

	return 0;
}
/****************************************************************************/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
/*--------------------------------------------------------------------------*/
static int easycap_open_noinode(struct file *file)
{
	return easycap_open(NULL, file);
}

static int videodev_release(struct video_device *pvideo_device)
{
	struct easycap *peasycap;

	peasycap = video_get_drvdata(pvideo_device);
	if (!peasycap) {
		SAY("ERROR:  peasycap is NULL\n");
		SAY("ending unsuccessfully\n");
		return -EFAULT;
	}
	if (easycap_video_kill_urbs(peasycap)) {
		SAM("ERROR: easycap_video_kill_urbs() failed\n");
		return -EFAULT;
	}
	JOM(4, "ending successfully\n");
	return 0;
}

/*****************************************************************************/
static unsigned int easycap_poll(struct file *file, poll_table *wait)
{
	struct easycap *peasycap;
	int rc, kd;

	JOT(8, "\n");

	if (NULL == ((poll_table *)wait))
		JOT(8, "WARNING:  poll table pointer is NULL ... continuing\n");
	if (!file) {
		SAY("ERROR:  file pointer is NULL\n");
		return -ERESTARTSYS;
	}
	peasycap = file->private_data;
	if (!peasycap) {
		SAY("ERROR:  peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAY("ERROR:  peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
/*---------------------------------------------------------------------------*/
	kd = easycap_isdongle(peasycap);
	if (0 <= kd && DONGLE_MANY > kd) {
		if (mutex_lock_interruptible(&easycapdc60_dongle[kd].mutex_video)) {
			SAY("ERROR: cannot down dongle[%i].mutex_video\n", kd);
			return -ERESTARTSYS;
		}
		JOM(4, "locked dongle[%i].mutex_video\n", kd);
	/*
	 *  MEANWHILE, easycap_usb_disconnect() MAY HAVE FREED POINTER
	 *  peasycap, IN WHICH CASE A REPEAT CALL TO isdongle() WILL FAIL.
	 *  IF NECESSARY, BAIL OUT.
	 */
		if (kd != easycap_isdongle(peasycap)) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ERESTARTSYS;
		}
		if (!file) {
			SAY("ERROR:  file is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ERESTARTSYS;
		}
		peasycap = file->private_data;
		if (!peasycap) {
			SAY("ERROR:  peasycap is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ERESTARTSYS;
		}
		if (!peasycap->pusb_device) {
			SAM("ERROR: peasycap->pusb_device is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ERESTARTSYS;
		}
	} else
	/*
	 *  IF easycap_usb_disconnect() HAS ALREADY FREED POINTER peasycap
	 *  BEFORE THE ATTEMPT TO ACQUIRE THE SEMAPHORE, isdongle() WILL
	 *  HAVE FAILED.  BAIL OUT.
	*/
		return -ERESTARTSYS;
/*---------------------------------------------------------------------------*/
	rc = easycap_video_dqbuf(peasycap, 0);
	peasycap->polled = 1;
	mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
	if (rc)
		return POLLERR;

	return POLLIN | POLLRDNORM;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  IF mode IS NONZERO THIS ROUTINE RETURNS -EAGAIN RATHER THAN BLOCKING.
 */
/*---------------------------------------------------------------------------*/
int easycap_video_dqbuf(struct easycap *peasycap, int mode)
{
	int input, ifield, miss, rc;


	if (!peasycap) {
		SAY("ERROR:  peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAY("ERROR:  peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	ifield = 0;
	JOM(8, "%i=ifield\n", ifield);
/*---------------------------------------------------------------------------*/
/*
 *  CHECK FOR LOST INPUT SIGNAL.
 *
 *  FOR THE FOUR-CVBS EasyCAP, THIS DOES NOT WORK AS EXPECTED.
 *  IF INPUT 0 IS PRESENT AND SYNC ACQUIRED, UNPLUGGING INPUT 4 DOES NOT
 *  RESULT IN SETTING BIT 0x40 ON REGISTER 0x1F, PRESUMABLY BECAUSE THERE
 *  IS FLYWHEELING ON INPUT 0.  THE UPSHOT IS:
 *
 *    INPUT 0   PLUGGED, INPUT 4   PLUGGED => SCREEN 0 OK,   SCREEN 4 OK
 *    INPUT 0   PLUGGED, INPUT 4 UNPLUGGED => SCREEN 0 OK,   SCREEN 4 BLACK
 *    INPUT 0 UNPLUGGED, INPUT 4   PLUGGED => SCREEN 0 BARS, SCREEN 4 OK
 *    INPUT 0 UNPLUGGED, INPUT 4 UNPLUGGED => SCREEN 0 BARS, SCREEN 4 BARS
*/
/*---------------------------------------------------------------------------*/
	input = peasycap->input;
	if (0 <= input && INPUT_MANY > input) {
		rc = read_saa(peasycap->pusb_device, 0x1F);
		if (0 <= rc) {
			if (rc & 0x40)
				peasycap->lost[input] += 1;
			else
				peasycap->lost[input] -= 2;

		if (0 > peasycap->lost[input])
			peasycap->lost[input] = 0;
		else if ((2 * VIDEO_LOST_TOLERATE) < peasycap->lost[input])
			peasycap->lost[input] = (2 * VIDEO_LOST_TOLERATE);
		}
	}
/*---------------------------------------------------------------------------*/
/*
 *  WAIT FOR FIELD ifield  (0 => TOP, 1 => BOTTOM)
 */
/*---------------------------------------------------------------------------*/
	miss = 0;
	while ((peasycap->field_read == peasycap->field_fill) ||
	       (0 != (0xFF00 & peasycap->field_buffer
					[peasycap->field_read][0].kount)) ||
	      (ifield != (0x00FF & peasycap->field_buffer
					[peasycap->field_read][0].kount))) {
		if (mode)
			return -EAGAIN;

		JOM(8, "first wait  on wq_video, %i=field_read %i=field_fill\n",
				peasycap->field_read, peasycap->field_fill);

		if (0 != (wait_event_interruptible(peasycap->wq_video,
				(peasycap->video_idle || peasycap->video_eof  ||
				((peasycap->field_read != peasycap->field_fill) &&
				(0 == (0xFF00 & peasycap->field_buffer[peasycap->field_read][0].kount)) &&
				(ifield == (0x00FF & peasycap->field_buffer[peasycap->field_read][0].kount))))))) {
			SAM("aborted by signal\n");
			return -EIO;
		}
		if (peasycap->video_idle) {
			JOM(8, "%i=peasycap->video_idle returning -EAGAIN\n",
							peasycap->video_idle);
			return -EAGAIN;
		}
		if (peasycap->video_eof) {
			JOM(8, "%i=peasycap->video_eof\n", peasycap->video_eof);
			#if defined(PERSEVERE)
			if (1 == peasycap->status) {
				JOM(8, "persevering ...\n");
				peasycap->video_eof = 0;
				peasycap->audio_eof = 0;
				if (0 != reset(peasycap)) {
					JOM(8, " ... failed  returning -EIO\n");
					peasycap->video_eof = 1;
					peasycap->audio_eof = 1;
					easycap_video_kill_urbs(peasycap);
					return -EIO;
				}
				peasycap->status = 0;
				JOM(8, " ... OK  returning -EAGAIN\n");
				return -EAGAIN;
			}
			#endif /*PERSEVERE*/
			peasycap->video_eof = 1;
			peasycap->audio_eof = 1;
			easycap_video_kill_urbs(peasycap);
			JOM(8, "returning -EIO\n");
			return -EIO;
		}
		miss++;
	}
	JOM(8, "first awakening on wq_video after %i waits\n", miss);

	rc = field2frame(peasycap);
	if (rc)
		SAM("ERROR: field2frame() rc = %i\n", rc);
/*---------------------------------------------------------------------------*/
/*
 *  WAIT FOR THE OTHER FIELD
 */
/*---------------------------------------------------------------------------*/
	if (ifield)
		ifield = 0;
	else
		ifield = 1;
	miss = 0;
	while ((peasycap->field_read == peasycap->field_fill) ||
	       (0 != (0xFF00 & peasycap->field_buffer[peasycap->field_read][0].kount)) ||
	       (ifield != (0x00FF & peasycap->field_buffer[peasycap->field_read][0].kount))) {
		if (mode)
			return -EAGAIN;

		JOM(8, "second wait on wq_video %i=field_read  %i=field_fill\n",
				peasycap->field_read, peasycap->field_fill);
		if (0 != (wait_event_interruptible(peasycap->wq_video,
			(peasycap->video_idle || peasycap->video_eof  ||
			((peasycap->field_read != peasycap->field_fill) &&
			 (0 == (0xFF00 & peasycap->field_buffer[peasycap->field_read][0].kount)) &&
			 (ifield == (0x00FF & peasycap->field_buffer[peasycap->field_read][0].kount))))))) {
			SAM("aborted by signal\n");
			return -EIO;
		}
		if (peasycap->video_idle) {
			JOM(8, "%i=peasycap->video_idle returning -EAGAIN\n",
							peasycap->video_idle);
			return -EAGAIN;
		}
		if (peasycap->video_eof) {
			JOM(8, "%i=peasycap->video_eof\n", peasycap->video_eof);
#if defined(PERSEVERE)
			if (1 == peasycap->status) {
				JOM(8, "persevering ...\n");
				peasycap->video_eof = 0;
				peasycap->audio_eof = 0;
				if (0 != reset(peasycap)) {
					JOM(8, " ... failed returning -EIO\n");
					peasycap->video_eof = 1;
					peasycap->audio_eof = 1;
					easycap_video_kill_urbs(peasycap);
					return -EIO;
				}
				peasycap->status = 0;
				JOM(8, " ... OK ... returning -EAGAIN\n");
				return -EAGAIN;
			}
#endif /*PERSEVERE*/
			peasycap->video_eof = 1;
			peasycap->audio_eof = 1;
			easycap_video_kill_urbs(peasycap);
			JOM(8, "returning -EIO\n");
			return -EIO;
		}
		miss++;
	}
	JOM(8, "second awakening on wq_video after %i waits\n", miss);

	rc = field2frame(peasycap);
	if (rc)
		SAM("ERROR: field2frame() rc = %i\n", rc);
/*---------------------------------------------------------------------------*/
/*
 *  WASTE THIS FRAME
*/
/*---------------------------------------------------------------------------*/
	if (peasycap->skip) {
		peasycap->skipped++;
		if (peasycap->skip != peasycap->skipped)
			return peasycap->skip - peasycap->skipped;
		else
			peasycap->skipped = 0;
	}
/*---------------------------------------------------------------------------*/
	peasycap->frame_read = peasycap->frame_fill;
	peasycap->queued[peasycap->frame_read] = 0;
	peasycap->done[peasycap->frame_read]   = V4L2_BUF_FLAG_DONE;

	peasycap->frame_fill++;
	if (peasycap->frame_buffer_many <= peasycap->frame_fill)
		peasycap->frame_fill = 0;

	if (0x01 & easycap_standard[peasycap->standard_offset].mask)
		peasycap->frame_buffer[peasycap->frame_read][0].kount =
							V4L2_FIELD_TOP;
	else
		peasycap->frame_buffer[peasycap->frame_read][0].kount =
							V4L2_FIELD_BOTTOM;


	JOM(8, "setting:    %i=peasycap->frame_read\n", peasycap->frame_read);
	JOM(8, "bumped to:  %i=peasycap->frame_fill\n", peasycap->frame_fill);

	return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  BY DEFINITION, odd IS true  FOR THE FIELD OCCUPYING LINES 1,3,5,...,479
 *                 odd IS false FOR THE FIELD OCCUPYING LINES 0,2,4,...,478
 *
 *  WHEN BOOLEAN PARAMETER decimatepixel IS true, ONLY THE FIELD FOR WHICH
 *  odd==false IS TRANSFERRED TO THE FRAME BUFFER.
 *
 */
/*---------------------------------------------------------------------------*/
static int field2frame(struct easycap *peasycap)
{

	void *pex, *pad;
	int kex, kad, mex, mad, rex, rad, rad2;
	int c2, c3, w2, w3, cz, wz;
	int rc, bytesperpixel, multiplier;
	int  much, more, over, rump, caches, input;
	u8 mask, margin;
	bool odd, isuy, decimatepixel, badinput;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}

	badinput = false;
	input = 0x07 & peasycap->field_buffer[peasycap->field_read][0].input;

	JOM(8, "=====  parity %i, input 0x%02X, field buffer %i --> "
							"frame buffer %i\n",
			peasycap->field_buffer[peasycap->field_read][0].kount,
			peasycap->field_buffer[peasycap->field_read][0].input,
			peasycap->field_read, peasycap->frame_fill);
	JOM(8, "=====  %i=bytesperpixel\n", peasycap->bytesperpixel);

/*---------------------------------------------------------------------------*/
/*
 *  REJECT OR CLEAN BAD FIELDS
 */
/*---------------------------------------------------------------------------*/
	if (peasycap->field_read == peasycap->field_fill) {
		SAM("ERROR: on entry, still filling field buffer %i\n",
						peasycap->field_read);
		return 0;
	}
#ifdef EASYCAP_TESTCARD
	easycap_testcard(peasycap, peasycap->field_read);
#else
	if (0 <= input && INPUT_MANY > input) {
		if (easycap_bars && VIDEO_LOST_TOLERATE <= peasycap->lost[input])
			easycap_testcard(peasycap, peasycap->field_read);
	}
#endif /*EASYCAP_TESTCARD*/
/*---------------------------------------------------------------------------*/

	bytesperpixel = peasycap->bytesperpixel;
	decimatepixel = peasycap->decimatepixel;

	if ((2 != bytesperpixel) &&
	    (3 != bytesperpixel) &&
	    (4 != bytesperpixel)) {
		SAM("MISTAKE: %i=bytesperpixel\n", bytesperpixel);
		return -EFAULT;
	}
	if (decimatepixel)
		multiplier = 2;
	else
		multiplier = 1;

	w2 = 2 * multiplier * (peasycap->width);
	w3 = bytesperpixel * multiplier * (peasycap->width);
	wz = multiplier * (peasycap->height) *
		multiplier * (peasycap->width);

	kex = peasycap->field_read;  mex = 0;
	kad = peasycap->frame_fill;  mad = 0;

	pex = peasycap->field_buffer[kex][0].pgo;  rex = PAGE_SIZE;
	pad = peasycap->frame_buffer[kad][0].pgo;  rad = PAGE_SIZE;
	odd = !!(peasycap->field_buffer[kex][0].kount);

	if (odd && (!decimatepixel)) {
		JOM(8, "initial skipping %4i bytes p.%4i\n",
					w3/multiplier, mad);
		pad += (w3 / multiplier); rad -= (w3 / multiplier);
	}
	isuy = true;
	mask = 0;  rump = 0;  caches = 0;

	cz = 0;
	while (cz < wz) {
		/*
		 *  PROCESS ONE LINE OF FRAME AT FULL RESOLUTION:
		 *  READ   w2   BYTES FROM FIELD BUFFER,
		 *  WRITE  w3   BYTES TO FRAME BUFFER
		 */
		if (!decimatepixel) {
			over = w2;
			do {
				much = over;  more = 0;
				margin = 0;  mask = 0x00;
				if (rex < much)
					much = rex;
				rump = 0;

				if (much % 2) {
					SAM("MISTAKE: much is odd\n");
					return -EFAULT;
				}

				more = (bytesperpixel *
						much) / 2;
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
				if (1 < bytesperpixel) {
					if (rad * 2 < much * bytesperpixel) {
						/*
						 * INJUDICIOUS ALTERATION OF
						 * THIS STATEMENT BLOCK WILL
						 * CAUSE BREAKAGE.  BEWARE.
						 */
						rad2 = rad + bytesperpixel - 1;
						much = ((((2 * rad2)/bytesperpixel)/2) * 2);
						rump = ((bytesperpixel * much) / 2) - rad;
						more = rad;
					}
					mask = (u8)rump;
					margin = 0;
					if (much == rex) {
						mask |= 0x04;
						if ((mex + 1) < FIELD_BUFFER_SIZE / PAGE_SIZE)
							margin = *((u8 *)(peasycap->field_buffer[kex][mex + 1].pgo));
						else
							mask |= 0x08;
					}
				} else {
					SAM("MISTAKE: %i=bytesperpixel\n",
							bytesperpixel);
					return -EFAULT;
				}
				if (rump)
					caches++;
					if (badinput) {
						JOM(8, "ERROR: 0x%02X=->field_buffer"
							"[%i][%i].input, "
							"0x%02X=(0x08|->input)\n",
							peasycap->field_buffer
							[kex][mex].input, kex, mex,
							(0x08|peasycap->input));
					}
				rc = redaub(peasycap, pad, pex, much, more,
								mask, margin, isuy);
				if (0 > rc) {
					SAM("ERROR: redaub() failed\n");
					return -EFAULT;
				}
				if (much % 4)
					isuy = !isuy;

				over -= much;   cz += much;
				pex  += much;  rex -= much;
				if (!rex) {
					mex++;
					pex = peasycap->field_buffer[kex][mex].pgo;
					rex = PAGE_SIZE;
					if (peasycap->field_buffer[kex][mex].input != (0x08|peasycap->input))
						badinput = true;
				}
				pad  += more;
				rad -= more;
				if (!rad) {
					mad++;
					pad = peasycap->frame_buffer[kad][mad].pgo;
					rad = PAGE_SIZE;
					if (rump) {
						pad += rump;
						rad -= rump;
					}
				}
			} while (over);
/*---------------------------------------------------------------------------*/
/*
 *  SKIP  w3 BYTES IN TARGET FRAME BUFFER,
 *  UNLESS IT IS THE LAST LINE OF AN ODD FRAME
 */
/*---------------------------------------------------------------------------*/
			if (!odd || (cz != wz)) {
				over = w3;
				do {
					if (!rad) {
						mad++;
						pad = peasycap->frame_buffer
							[kad][mad].pgo;
						rad = PAGE_SIZE;
					}
					more = over;
					if (rad < more)
						more = rad;
					over -= more;
					pad  += more;
					rad  -= more;
				} while (over);
			}
/*---------------------------------------------------------------------------*/
/*
 *  PROCESS ONE LINE OF FRAME AT REDUCED RESOLUTION:
 *  ONLY IF false==odd,
 *  READ   w2   BYTES FROM FIELD BUFFER,
 *  WRITE  w3 / 2  BYTES TO FRAME BUFFER
 */
/*---------------------------------------------------------------------------*/
		} else if (!odd) {
			over = w2;
			do {
				much = over;  more = 0;  margin = 0;  mask = 0x00;
				if (rex < much)
					much = rex;
				rump = 0;

				if (much % 2) {
					SAM("MISTAKE: much is odd\n");
					return -EFAULT;
				}

				more = (bytesperpixel * much) / 4;
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
				if (1 < bytesperpixel) {
					if (rad * 4 < much * bytesperpixel) {
						/*
						 * INJUDICIOUS ALTERATION OF
						 * THIS STATEMENT BLOCK
						 * WILL CAUSE BREAKAGE.
						 * BEWARE.
						 */
						rad2 = rad + bytesperpixel - 1;
						much = ((((2 * rad2) / bytesperpixel) / 2) * 4);
						rump = ((bytesperpixel * much) / 4) - rad;
						more = rad;
					}
					mask = (u8)rump;
					margin = 0;
					if (much == rex) {
						mask |= 0x04;
						if ((mex + 1) < FIELD_BUFFER_SIZE / PAGE_SIZE)
							margin = *((u8 *)(peasycap->field_buffer[kex][mex + 1].pgo));
						else
							mask |= 0x08;
					}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
				} else {
					SAM("MISTAKE: %i=bytesperpixel\n",
						bytesperpixel);
					return -EFAULT;
				}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
				if (rump)
					caches++;

					if (badinput) {
						JOM(8, "ERROR: 0x%02X=->field_buffer"
							"[%i][%i].input, "
							"0x%02X=(0x08|->input)\n",
							peasycap->field_buffer
							[kex][mex].input, kex, mex,
							(0x08|peasycap->input));
					}
				rc = redaub(peasycap, pad, pex, much, more,
							mask, margin, isuy);
				if (0 > rc) {
					SAM("ERROR: redaub() failed\n");
					return -EFAULT;
				}
				over -= much;   cz += much;
				pex  += much;  rex -= much;
				if (!rex) {
					mex++;
					pex = peasycap->field_buffer[kex][mex].pgo;
					rex = PAGE_SIZE;
					if (peasycap->field_buffer[kex][mex].input !=
							(0x08|peasycap->input))
						badinput = true;
				}
				pad  += more;
				rad -= more;
				if (!rad) {
					mad++;
					pad = peasycap->frame_buffer[kad][mad].pgo;
					rad = PAGE_SIZE;
					if (rump) {
						pad += rump;
						rad -= rump;
					}
				}
			} while (over);
/*---------------------------------------------------------------------------*/
/*
 *  OTHERWISE JUST
 *  READ   w2   BYTES FROM FIELD BUFFER AND DISCARD THEM
 */
/*---------------------------------------------------------------------------*/
		} else {
			over = w2;
			do {
				if (!rex) {
					mex++;
					pex = peasycap->field_buffer[kex][mex].pgo;
					rex = PAGE_SIZE;
					if (peasycap->field_buffer[kex][mex].input !=
							(0x08|peasycap->input)) {
						JOM(8, "ERROR: 0x%02X=->field_buffer"
							"[%i][%i].input, "
							"0x%02X=(0x08|->input)\n",
							peasycap->field_buffer
							[kex][mex].input, kex, mex,
							(0x08|peasycap->input));
						badinput = true;
					}
				}
				much = over;
				if (rex < much)
					much = rex;
				over -= much;
				cz += much;
				pex  += much;
				rex -= much;
			} while (over);
		}
	}
/*---------------------------------------------------------------------------*/
/*
 *  SANITY CHECKS
 */
/*---------------------------------------------------------------------------*/
	c2 = (mex + 1)*PAGE_SIZE - rex;
	if (cz != c2)
		SAM("ERROR: discrepancy %i in bytes read\n", c2 - cz);
	c3 = (mad + 1)*PAGE_SIZE - rad;

	if (!decimatepixel) {
		if (bytesperpixel * cz != c3)
			SAM("ERROR: discrepancy %i in bytes written\n",
					c3 - (bytesperpixel * cz));
	} else {
		if (!odd) {
			if (bytesperpixel *
				cz != (4 * c3))
				SAM("ERROR: discrepancy %i in bytes written\n",
					(2*c3)-(bytesperpixel * cz));
			} else {
				if (0 != c3)
					SAM("ERROR: discrepancy %i "
					    "in bytes written\n", c3);
			}
	}
	if (rump)
		SAM("WORRY: undischarged cache at end of line in frame buffer\n");

	JOM(8, "===== field2frame(): %i bytes --> %i bytes (incl skip)\n", c2, c3);
	JOM(8, "===== field2frame(): %i=mad  %i=rad\n", mad, rad);

	if (odd)
		JOM(8, "+++++ field2frame():  frame buffer %i is full\n", kad);

	if (peasycap->field_read == peasycap->field_fill)
		SAM("WARNING: on exit, filling field buffer %i\n",
						peasycap->field_read);

	if (caches)
		JOM(8, "%i=caches\n", caches);
	return 0;
}
/*---------------------------------------------------------------------------*/
/*
 *  DECIMATION AND COLOURSPACE CONVERSION.
 *
 *  THIS ROUTINE REQUIRES THAT ALL THE DATA TO BE READ RESIDES ON ONE PAGE
 *  AND THAT ALL THE DATA TO BE WRITTEN RESIDES ON ONE (DIFFERENT) PAGE.
 *  THE CALLING ROUTINE MUST ENSURE THAT THIS REQUIREMENT IS MET, AND MUST
 *  ALSO ENSURE THAT much IS EVEN.
 *
 *  much BYTES ARE READ, AT LEAST (bytesperpixel * much)/2 BYTES ARE WRITTEN
 *  IF THERE IS NO DECIMATION, HALF THIS AMOUNT IF THERE IS DECIMATION.
 *
 *  mask IS ZERO WHEN NO SPECIAL BEHAVIOUR REQUIRED. OTHERWISE IT IS SET THUS:
 *     0x03 & mask =  number of bytes to be written to cache instead of to
 *                    frame buffer
 *     0x04 & mask => use argument margin to set the chrominance for last pixel
 *     0x08 & mask => do not set the chrominance for last pixel
 *
 *  YUV to RGB CONVERSION IS (OR SHOULD BE) ITU-R BT 601.
 *
 *  THERE IS A LOT OF CODE REPETITION IN THIS ROUTINE IN ORDER TO AVOID
 *  INEFFICIENT SWITCHING INSIDE INNER LOOPS.  REARRANGING THE LOGIC TO
 *  REDUCE CODE LENGTH WILL GENERALLY IMPAIR RUNTIME PERFORMANCE.  BEWARE.
 */
/*---------------------------------------------------------------------------*/
static int redaub(struct easycap *peasycap,
		void *pad, void *pex, int much, int more,
		u8 mask, u8 margin, bool isuy)
{
	static s32 ay[256], bu[256], rv[256], gu[256], gv[256];
	u8 *pcache;
	u8 r, g, b, y, u, v, c, *p2, *p3, *pz, *pr;
	int  bytesperpixel;
	bool byteswaporder, decimatepixel, last;
	int j, rump;
	s32 tmp;

	if (much % 2) {
		SAM("MISTAKE: much is odd\n");
		return -EFAULT;
	}
	bytesperpixel = peasycap->bytesperpixel;
	byteswaporder = peasycap->byteswaporder;
	decimatepixel = peasycap->decimatepixel;

/*---------------------------------------------------------------------------*/
	if (!bu[255]) {
		for (j = 0; j < 112; j++) {
			tmp = (0xFF00 & (453 * j)) >> 8;
			bu[j + 128] =  tmp; bu[127 - j] = -tmp;
			tmp = (0xFF00 & (359 * j)) >> 8;
			rv[j + 128] =  tmp; rv[127 - j] = -tmp;
			tmp = (0xFF00 & (88 * j)) >> 8;
			gu[j + 128] =  tmp; gu[127 - j] = -tmp;
			tmp = (0xFF00 & (183 * j)) >> 8;
			gv[j + 128] =  tmp; gv[127 - j] = -tmp;
		}
		for (j = 0; j < 16; j++) {
			bu[j] = bu[16]; rv[j] = rv[16];
			gu[j] = gu[16]; gv[j] = gv[16];
		}
		for (j = 240; j < 256; j++) {
			bu[j] = bu[239]; rv[j] = rv[239];
			gu[j] = gu[239]; gv[j] = gv[239];
		}
		for (j =  16; j < 236; j++)
			ay[j] = j;
		for (j =   0; j <  16; j++)
			ay[j] = ay[16];
		for (j = 236; j < 256; j++)
			ay[j] = ay[235];
		JOM(8, "lookup tables are prepared\n");
	}
	pcache = peasycap->pcache;
	if (!pcache)
		pcache = &peasycap->cache[0];
/*---------------------------------------------------------------------------*/
/*
 *  TRANSFER CONTENTS OF CACHE TO THE FRAME BUFFER
 */
/*---------------------------------------------------------------------------*/
	if (!pcache) {
		SAM("MISTAKE: pcache is NULL\n");
		return -EFAULT;
	}

	if (pcache != &peasycap->cache[0])
		JOM(16, "cache has %i bytes\n", (int)(pcache - &peasycap->cache[0]));
	p2 = &peasycap->cache[0];
	p3 = (u8 *)pad - (int)(pcache - &peasycap->cache[0]);
	while (p2 < pcache) {
		*p3++ = *p2;  p2++;
	}
	pcache = &peasycap->cache[0];
	if (p3 != pad) {
		SAM("MISTAKE: pointer misalignment\n");
		return -EFAULT;
	}
/*---------------------------------------------------------------------------*/
	rump = (int)(0x03 & mask);
	u = 0; v = 0;
	p2 = (u8 *)pex;  pz = p2 + much;  pr = p3 + more;  last = false;
	p2++;

	if (isuy)
		u = *(p2 - 1);
	else
		v = *(p2 - 1);

	if (rump)
		JOM(16, "%4i=much  %4i=more  %i=rump\n", much, more, rump);

/*---------------------------------------------------------------------------*/
	switch (bytesperpixel) {
	case 2: {
		if (!decimatepixel) {
			memcpy(pad, pex, (size_t)much);
			if (!byteswaporder) {
				/* UYVY */
				return 0;
			} else {
				/* YUYV */
				p3 = (u8 *)pad;  pz = p3 + much;
				while  (pz > p3) {
					c = *p3;
					*p3 = *(p3 + 1);
					*(p3 + 1) = c;
					p3 += 2;
				}
				return 0;
			}
		} else {
			if (!byteswaporder) {
				/*  UYVY DECIMATED */
				p2 = (u8 *)pex;  p3 = (u8 *)pad;  pz = p2 + much;
				while (pz > p2) {
					*p3 = *p2;
					*(p3 + 1) = *(p2 + 1);
					*(p3 + 2) = *(p2 + 2);
					*(p3 + 3) = *(p2 + 3);
					p3 += 4;  p2 += 8;
				}
				return 0;
			} else {
				/* YUYV DECIMATED */
				p2 = (u8 *)pex;  p3 = (u8 *)pad;  pz = p2 + much;
				while (pz > p2) {
					*p3 = *(p2 + 1);
					*(p3 + 1) = *p2;
					*(p3 + 2) = *(p2 + 3);
					*(p3 + 3) = *(p2 + 2);
					p3 += 4;  p2 += 8;
				}
				return 0;
			}
		}
		break;
		}
	case 3:
		{
		if (!decimatepixel) {
			if (!byteswaporder) {
				/* RGB */
				while (pz > p2) {
					if (pr <= (p3 + bytesperpixel))
						last = true;
					else
						last = false;
					y = *p2;
					if (last && (0x0C & mask)) {
						if (0x04 & mask) {
							if (isuy)
								v = margin;
							else
								u = margin;
						} else
							if (0x08 & mask)
								;
					} else {
						if (isuy)
							v = *(p2 + 1);
						else
							u = *(p2 + 1);
					}

					tmp = ay[(int)y] + rv[(int)v];
					r = (255 < tmp) ? 255 : ((0 > tmp) ?
								0 : (u8)tmp);
					tmp = ay[(int)y] - gu[(int)u] - gv[(int)v];
					g = (255 < tmp) ? 255 : ((0 > tmp) ?
								0 : (u8)tmp);
					tmp = ay[(int)y] + bu[(int)u];
					b = (255 < tmp) ? 255 : ((0 > tmp) ?
								0 : (u8)tmp);

					if (last && rump) {
						pcache = &peasycap->cache[0];
						switch (bytesperpixel - rump) {
						case 1: {
							*p3 = r;
							*pcache++ = g;
							*pcache++ = b;
							break;
						}
						case 2: {
							*p3 = r;
							*(p3 + 1) = g;
							*pcache++ = b;
							break;
						}
						default: {
							SAM("MISTAKE: %i=rump\n",
								bytesperpixel - rump);
							return -EFAULT;
						}
						}
					} else {
						*p3 = r;
						*(p3 + 1) = g;
						*(p3 + 2) = b;
					}
					p2 += 2;
					if (isuy)
						isuy = false;
					else
						isuy = true;
					p3 += bytesperpixel;
				}
				return 0;
			} else {
				/* BGR */
				while (pz > p2) {
					if (pr <= (p3 + bytesperpixel))
						last = true;
					else
						last = false;
					y = *p2;
					if (last && (0x0C & mask)) {
						if (0x04 & mask) {
							if (isuy)
								v = margin;
							else
								u = margin;
						}
					else
						if (0x08 & mask)
							;
					} else {
						if (isuy)
							v = *(p2 + 1);
						else
							u = *(p2 + 1);
					}

					tmp = ay[(int)y] + rv[(int)v];
					r = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
					tmp = ay[(int)y] - gu[(int)u] - gv[(int)v];
					g = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
					tmp = ay[(int)y] + bu[(int)u];
					b = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);

					if (last && rump) {
						pcache = &peasycap->cache[0];
						switch (bytesperpixel - rump) {
						case 1: {
							*p3 = b;
							*pcache++ = g;
							*pcache++ = r;
							break;
						}
						case 2: {
							*p3 = b;
							*(p3 + 1) = g;
							*pcache++ = r;
							break;
						}
						default: {
							SAM("MISTAKE: %i=rump\n",
								bytesperpixel - rump);
							return -EFAULT;
						}
						}
					} else {
						*p3 = b;
						*(p3 + 1) = g;
						*(p3 + 2) = r;
						}
					p2 += 2;
					if (isuy)
						isuy = false;
					else
						isuy = true;
					p3 += bytesperpixel;
					}
				}
			return 0;
		} else {
			if (!byteswaporder) {
				/*  RGB DECIMATED */
				while (pz > p2) {
					if (pr <= (p3 + bytesperpixel))
						last = true;
					else
						last = false;
					y = *p2;
					if (last && (0x0C & mask)) {
						if (0x04 & mask) {
							if (isuy)
								v = margin;
							else
								u = margin;
						} else
							if (0x08 & mask)
								;
					} else {
						if (isuy)
							v = *(p2 + 1);
						else
							u = *(p2 + 1);
					}

					if (isuy) {
						tmp = ay[(int)y] + rv[(int)v];
						r = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
						tmp = ay[(int)y] - gu[(int)u] -
									gv[(int)v];
						g = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
						tmp = ay[(int)y] + bu[(int)u];
						b = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);

						if (last && rump) {
							pcache = &peasycap->cache[0];
							switch (bytesperpixel - rump) {
							case 1: {
								*p3 = r;
								*pcache++ = g;
								*pcache++ = b;
								break;
							}
							case 2: {
								*p3 = r;
								*(p3 + 1) = g;
								*pcache++ = b;
								break;
							}
							default: {
								SAM("MISTAKE: "
								"%i=rump\n",
								bytesperpixel - rump);
								return -EFAULT;
							}
							}
						} else {
							*p3 = r;
							*(p3 + 1) = g;
							*(p3 + 2) = b;
						}
						isuy = false;
						p3 += bytesperpixel;
					} else {
						isuy = true;
					}
					p2 += 2;
				}
				return 0;
			} else {
				/* BGR DECIMATED */
				while (pz > p2) {
					if (pr <= (p3 + bytesperpixel))
						last = true;
					else
						last = false;
					y = *p2;
					if (last && (0x0C & mask)) {
						if (0x04 & mask) {
							if (isuy)
								v = margin;
							else
								u = margin;
						} else
							if (0x08 & mask)
								;
					} else {
						if (isuy)
							v = *(p2 + 1);
						else
							u = *(p2 + 1);
					}

					if (isuy) {

						tmp = ay[(int)y] + rv[(int)v];
						r = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
						tmp = ay[(int)y] - gu[(int)u] -
									gv[(int)v];
						g = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
						tmp = ay[(int)y] + bu[(int)u];
						b = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);

						if (last && rump) {
							pcache = &peasycap->cache[0];
							switch (bytesperpixel - rump) {
							case 1: {
								*p3 = b;
								*pcache++ = g;
								*pcache++ = r;
								break;
							}
							case 2: {
								*p3 = b;
								*(p3 + 1) = g;
								*pcache++ = r;
								break;
							}
							default: {
								SAM("MISTAKE: "
								"%i=rump\n",
								bytesperpixel - rump);
								return -EFAULT;
							}
							}
						} else {
							*p3 = b;
							*(p3 + 1) = g;
							*(p3 + 2) = r;
							}
						isuy = false;
						p3 += bytesperpixel;
						}
					else
						isuy = true;
					p2 += 2;
					}
				return 0;
				}
			}
		break;
		}
	case 4:
		{
		if (!decimatepixel) {
			if (!byteswaporder) {
				/* RGBA */
				while (pz > p2) {
					if (pr <= (p3 + bytesperpixel))
						last = true;
					else
						last = false;
					y = *p2;
					if (last && (0x0C & mask)) {
						if (0x04 & mask) {
							if (isuy)
								v = margin;
							else
								u = margin;
						} else
							 if (0x08 & mask)
								;
					} else {
						if (isuy)
							v = *(p2 + 1);
						else
							u = *(p2 + 1);
					}

					tmp = ay[(int)y] + rv[(int)v];
					r = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
					tmp = ay[(int)y] - gu[(int)u] - gv[(int)v];
					g = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
					tmp = ay[(int)y] + bu[(int)u];
					b = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);

					if (last && rump) {
						pcache = &peasycap->cache[0];
						switch (bytesperpixel - rump) {
						case 1: {
							*p3 = r;
							*pcache++ = g;
							*pcache++ = b;
							*pcache++ = 0;
							break;
						}
						case 2: {
							*p3 = r;
							*(p3 + 1) = g;
							*pcache++ = b;
							*pcache++ = 0;
							break;
						}
						case 3: {
							*p3 = r;
							*(p3 + 1) = g;
							*(p3 + 2) = b;
							*pcache++ = 0;
							break;
						}
						default: {
							SAM("MISTAKE: %i=rump\n",
								bytesperpixel - rump);
							return -EFAULT;
						}
						}
					} else {
						*p3 = r;
						*(p3 + 1) = g;
						*(p3 + 2) = b;
						*(p3 + 3) = 0;
					}
					p2 += 2;
					if (isuy)
						isuy = false;
					else
						isuy = true;
					p3 += bytesperpixel;
				}
				return 0;
			} else {
				/*
				 *  BGRA
				 */
				while (pz > p2) {
					if (pr <= (p3 + bytesperpixel))
						last = true;
					else
						last = false;
					y = *p2;
					if (last && (0x0C & mask)) {
						if (0x04 & mask) {
							if (isuy)
								v = margin;
							else
								u = margin;
						} else
							 if (0x08 & mask)
								;
					} else {
						if (isuy)
							v = *(p2 + 1);
						else
							u = *(p2 + 1);
					}

					tmp = ay[(int)y] + rv[(int)v];
					r = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
					tmp = ay[(int)y] - gu[(int)u] - gv[(int)v];
					g = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
					tmp = ay[(int)y] + bu[(int)u];
					b = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);

					if (last && rump) {
						pcache = &peasycap->cache[0];
						switch (bytesperpixel - rump) {
						case 1: {
							*p3 = b;
							*pcache++ = g;
							*pcache++ = r;
							*pcache++ = 0;
							break;
						}
						case 2: {
							*p3 = b;
							*(p3 + 1) = g;
							*pcache++ = r;
							*pcache++ = 0;
							break;
						}
						case 3: {
							*p3 = b;
							*(p3 + 1) = g;
							*(p3 + 2) = r;
							*pcache++ = 0;
							break;
						}
						default:
							SAM("MISTAKE: %i=rump\n",
								bytesperpixel - rump);
							return -EFAULT;
						}
					} else {
						*p3 = b;
						*(p3 + 1) = g;
						*(p3 + 2) = r;
						*(p3 + 3) = 0;
					}
					p2 += 2;
					if (isuy)
						isuy = false;
					else
						isuy = true;
					p3 += bytesperpixel;
				}
			}
			return 0;
		} else {
			if (!byteswaporder) {
				/*
				 *  RGBA DECIMATED
				 */
				while (pz > p2) {
					if (pr <= (p3 + bytesperpixel))
						last = true;
					else
						last = false;
					y = *p2;
					if (last && (0x0C & mask)) {
						if (0x04 & mask) {
							if (isuy)
								v = margin;
							else
								u = margin;
						} else
							if (0x08 & mask)
								;
					} else {
						if (isuy)
							v = *(p2 + 1);
						else
							u = *(p2 + 1);
					}

					if (isuy) {

						tmp = ay[(int)y] + rv[(int)v];
						r = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
						tmp = ay[(int)y] - gu[(int)u] -
									gv[(int)v];
						g = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
						tmp = ay[(int)y] + bu[(int)u];
						b = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);

						if (last && rump) {
							pcache = &peasycap->cache[0];
							switch (bytesperpixel - rump) {
							case 1: {
								*p3 = r;
								*pcache++ = g;
								*pcache++ = b;
								*pcache++ = 0;
								break;
							}
							case 2: {
								*p3 = r;
								*(p3 + 1) = g;
								*pcache++ = b;
								*pcache++ = 0;
								break;
							}
							case 3: {
								*p3 = r;
								*(p3 + 1) = g;
								*(p3 + 2) = b;
								*pcache++ = 0;
								break;
							}
							default: {
								SAM("MISTAKE: "
								"%i=rump\n",
								bytesperpixel -
								rump);
								return -EFAULT;
								}
							}
						} else {
							*p3 = r;
							*(p3 + 1) = g;
							*(p3 + 2) = b;
							*(p3 + 3) = 0;
							}
						isuy = false;
						p3 += bytesperpixel;
					} else
						isuy = true;
					p2 += 2;
				}
				return 0;
			} else {
				/*
				 *  BGRA DECIMATED
				 */
				while (pz > p2) {
					if (pr <= (p3 + bytesperpixel))
						last = true;
					else
						last = false;
					y = *p2;
					if (last && (0x0C & mask)) {
						if (0x04 & mask) {
							if (isuy)
								v = margin;
							else
								u = margin;
						} else
							if (0x08 & mask)
								;
					} else {
						if (isuy)
							v = *(p2 + 1);
						else
							u = *(p2 + 1);
					}

					if (isuy) {
						tmp = ay[(int)y] + rv[(int)v];
						r = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
						tmp = ay[(int)y] - gu[(int)u] -
									gv[(int)v];
						g = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);
						tmp = ay[(int)y] + bu[(int)u];
						b = (255 < tmp) ? 255 : ((0 > tmp) ?
									0 : (u8)tmp);

						if (last && rump) {
							pcache = &peasycap->cache[0];
							switch (bytesperpixel - rump) {
							case 1: {
								*p3 = b;
								*pcache++ = g;
								*pcache++ = r;
								*pcache++ = 0;
								break;
							}
							case 2: {
								*p3 = b;
								*(p3 + 1) = g;
								*pcache++ = r;
								*pcache++ = 0;
								break;
							}
							case 3: {
								*p3 = b;
								*(p3 + 1) = g;
								*(p3 + 2) = r;
								*pcache++ = 0;
								break;
							}
							default: {
								SAM("MISTAKE: "
								"%i=rump\n",
								bytesperpixel - rump);
								return -EFAULT;
							}
							}
						} else {
							*p3 = b;
							*(p3 + 1) = g;
							*(p3 + 2) = r;
							*(p3 + 3) = 0;
						}
						isuy = false;
						p3 += bytesperpixel;
					} else
						isuy = true;
						p2 += 2;
					}
					return 0;
				}
			}
		break;
		}
	default: {
		SAM("MISTAKE: %i=bytesperpixel\n", bytesperpixel);
		return -EFAULT;
		}
	}
	return 0;
}
/*****************************************************************************/
/*
 *  SEE CORBET ET AL. "LINUX DEVICE DRIVERS", 3rd EDITION, PAGES 430-434
 */
/*****************************************************************************/
static void easycap_vma_open(struct vm_area_struct *pvma)
{
	struct easycap *peasycap;

	peasycap = pvma->vm_private_data;
	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return;
	}
	peasycap->vma_many++;
	JOT(8, "%i=peasycap->vma_many\n", peasycap->vma_many);
	return;
}
/*****************************************************************************/
static void easycap_vma_close(struct vm_area_struct *pvma)
{
	struct easycap *peasycap;

	peasycap = pvma->vm_private_data;
	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return;
	}
	peasycap->vma_many--;
	JOT(8, "%i=peasycap->vma_many\n", peasycap->vma_many);
	return;
}
/*****************************************************************************/
static int easycap_vma_fault(struct vm_area_struct *pvma, struct vm_fault *pvmf)
{
	int k, m, retcode;
	void *pbuf;
	struct page *page;
	struct easycap *peasycap;

	retcode = VM_FAULT_NOPAGE;

	if (!pvma) {
		SAY("pvma is NULL\n");
		return retcode;
	}
	if (!pvmf) {
		SAY("pvmf is NULL\n");
		return retcode;
	}

	k = (pvmf->pgoff) / (FRAME_BUFFER_SIZE/PAGE_SIZE);
	m = (pvmf->pgoff) % (FRAME_BUFFER_SIZE/PAGE_SIZE);

	if (!m)
		JOT(4, "%4i=k, %4i=m\n", k, m);
	else
		JOT(16, "%4i=k, %4i=m\n", k, m);

	if ((0 > k) || (FRAME_BUFFER_MANY <= k)) {
		SAY("ERROR: buffer index %i out of range\n", k);
		return retcode;
	}
	if ((0 > m) || (FRAME_BUFFER_SIZE/PAGE_SIZE <= m)) {
		SAY("ERROR: page number  %i out of range\n", m);
		return retcode;
	}
	peasycap = pvma->vm_private_data;
	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return retcode;
	}
/*---------------------------------------------------------------------------*/
	pbuf = peasycap->frame_buffer[k][m].pgo;
	if (!pbuf) {
		SAM("ERROR:  pbuf is NULL\n");
		return retcode;
	}
	page = virt_to_page(pbuf);
	if (!page) {
		SAM("ERROR:  page is NULL\n");
		return retcode;
	}
	get_page(page);
/*---------------------------------------------------------------------------*/
	if (!page) {
		SAM("ERROR:  page is NULL after get_page(page)\n");
	} else {
		pvmf->page = page;
		retcode = VM_FAULT_MINOR;
	}
	return retcode;
}

static const struct vm_operations_struct easycap_vm_ops = {
	.open  = easycap_vma_open,
	.close = easycap_vma_close,
	.fault = easycap_vma_fault,
};

static int easycap_mmap(struct file *file, struct vm_area_struct *pvma)
{
	JOT(8, "\n");

	pvma->vm_ops = &easycap_vm_ops;
	pvma->vm_flags |= VM_RESERVED;
	if (file)
		pvma->vm_private_data = file->private_data;
	easycap_vma_open(pvma);
	return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  ON COMPLETION OF A VIDEO URB ITS DATA IS COPIED TO THE FIELD BUFFERS
 *  PROVIDED peasycap->video_idle IS ZERO.  REGARDLESS OF THIS BEING TRUE,
 *  IT IS RESUBMITTED PROVIDED peasycap->video_isoc_streaming IS NOT ZERO.
 *
 *  THIS FUNCTION IS AN INTERRUPT SERVICE ROUTINE AND MUST NOT SLEEP.
 *
 *  INFORMATION ABOUT THE VALIDITY OF THE CONTENTS OF THE FIELD BUFFER ARE
 *  STORED IN THE TWO-BYTE STATUS PARAMETER
 *        peasycap->field_buffer[peasycap->field_fill][0].kount
 *  NOTICE THAT THE INFORMATION IS STORED ONLY WITH PAGE 0 OF THE FIELD BUFFER.
 *
 *  THE LOWER BYTE CONTAINS THE FIELD PARITY BYTE FURNISHED BY THE SAA7113H
 *  CHIP.
 *
 *  THE UPPER BYTE IS ZERO IF NO PROBLEMS, OTHERWISE:
 *      0 != (kount & 0x8000)   => AT LEAST ONE URB COMPLETED WITH ERRORS
 *      0 != (kount & 0x4000)   => BUFFER HAS TOO MUCH DATA
 *      0 != (kount & 0x2000)   => BUFFER HAS NOT ENOUGH DATA
 *      0 != (kount & 0x1000)   => BUFFER HAS DATA FROM DISPARATE INPUTS
 *      0 != (kount & 0x0400)   => RESERVED
 *      0 != (kount & 0x0200)   => FIELD BUFFER NOT YET CHECKED
 *      0 != (kount & 0x0100)   => BUFFER HAS TWO EXTRA BYTES - WHY?
 */
/*---------------------------------------------------------------------------*/
static void easycap_complete(struct urb *purb)
{
	struct easycap *peasycap;
	struct data_buffer *pfield_buffer;
	char errbuf[16];
	int i, more, much, leap, rc, last;
	int videofieldamount;
	unsigned int override, bad;
	int framestatus, framelength, frameactual, frameoffset;
	u8 *pu;

	if (!purb) {
		SAY("ERROR: easycap_complete(): purb is NULL\n");
		return;
	}
	peasycap = purb->context;
	if (!peasycap) {
		SAY("ERROR: easycap_complete(): peasycap is NULL\n");
		return;
	}
	if (peasycap->video_eof)
		return;
	for (i = 0; i < VIDEO_ISOC_BUFFER_MANY; i++)
		if (purb->transfer_buffer == peasycap->video_isoc_buffer[i].pgo)
			break;
	JOM(16, "%2i=urb\n", i);
	last = peasycap->video_isoc_sequence;
	if ((((VIDEO_ISOC_BUFFER_MANY - 1) == last) && (0 != i)) ||
	     (((VIDEO_ISOC_BUFFER_MANY - 1) != last) && ((last + 1) != i))) {
		JOM(16, "ERROR: out-of-order urbs %i,%i ... continuing\n",
						last, i);
	}
	peasycap->video_isoc_sequence = i;

	if (peasycap->video_idle) {
		JOM(16, "%i=video_idle  %i=video_isoc_streaming\n",
				peasycap->video_idle, peasycap->video_isoc_streaming);
		if (peasycap->video_isoc_streaming) {
			rc = usb_submit_urb(purb, GFP_ATOMIC);
			if (rc) {
				SAM("%s:%d ENOMEM\n", strerror(rc), rc);
				if (-ENODEV != rc)
					SAM("ERROR: while %i=video_idle, "
								"usb_submit_urb() "
								"failed with rc:\n",
								peasycap->video_idle);
			}
		}
	return;
	}
	override = 0;
/*---------------------------------------------------------------------------*/
	if (FIELD_BUFFER_MANY <= peasycap->field_fill) {
		SAM("ERROR: bad peasycap->field_fill\n");
		return;
	}
	if (purb->status) {
		if ((-ESHUTDOWN == purb->status) || (-ENOENT == purb->status)) {
			JOM(8, "urb status -ESHUTDOWN or -ENOENT\n");
			return;
		}

		(peasycap->field_buffer[peasycap->field_fill][0].kount) |= 0x8000 ;
		SAM("ERROR: bad urb status -%s: %d\n",
				strerror(purb->status), purb->status);
/*---------------------------------------------------------------------------*/
	} else {
		for (i = 0;  i < purb->number_of_packets; i++) {
			if (0 != purb->iso_frame_desc[i].status) {
				(peasycap->field_buffer
					[peasycap->field_fill][0].kount) |= 0x8000 ;
				/* FIXME: 1. missing '-' check boundaries */
				strcpy(&errbuf[0],
					strerror(purb->iso_frame_desc[i].status));
			}
			framestatus = purb->iso_frame_desc[i].status;
			framelength = purb->iso_frame_desc[i].length;
			frameactual = purb->iso_frame_desc[i].actual_length;
			frameoffset = purb->iso_frame_desc[i].offset;

			JOM(16, "frame[%2i]:"
					"%4i=status "
					"%4i=actual "
					"%4i=length "
					"%5i=offset\n",
				i, framestatus, frameactual, framelength, frameoffset);
			if (!purb->iso_frame_desc[i].status) {
				more = purb->iso_frame_desc[i].actual_length;
				pfield_buffer = &peasycap->field_buffer
					  [peasycap->field_fill][peasycap->field_page];
				videofieldamount = (peasycap->field_page *
					PAGE_SIZE) +
					(int)(pfield_buffer->pto - pfield_buffer->pgo);
			if (4 == more)
				peasycap->video_mt++;
			if (4 < more) {
				if (peasycap->video_mt) {
					JOM(8, "%4i empty video urb frames\n",
								peasycap->video_mt);
					peasycap->video_mt = 0;
				}
				if (FIELD_BUFFER_MANY <= peasycap->field_fill) {
					SAM("ERROR: bad peasycap->field_fill\n");
					return;
				}
				if (FIELD_BUFFER_SIZE/PAGE_SIZE <=
								peasycap->field_page) {
					SAM("ERROR: bad peasycap->field_page\n");
					return;
				}
				pfield_buffer = &peasycap->field_buffer
					[peasycap->field_fill][peasycap->field_page];
				pu = (u8 *)(purb->transfer_buffer +
						purb->iso_frame_desc[i].offset);
				if (0x80 & *pu)
					leap = 8;
				else
					leap = 4;
/*--------------------------------------------------------------------------*/
/*
 *  EIGHT-BYTE END-OF-VIDEOFIELD MARKER.
 *  NOTE:  A SUCCESSION OF URB FRAMES FOLLOWING THIS ARE EMPTY,
 *         CORRESPONDING TO THE FIELD FLYBACK (VERTICAL BLANKING) PERIOD.
 *
 *  PROVIDED THE FIELD BUFFER CONTAINS GOOD DATA AS INDICATED BY A ZERO UPPER
 *  BYTE OF
 *        peasycap->field_buffer[peasycap->field_fill][0].kount
 *  THE CONTENTS OF THE FIELD BUFFER ARE OFFERED TO dqbuf(), field_read IS
 *  UPDATED AND field_fill IS BUMPED.  IF THE FIELD BUFFER CONTAINS BAD DATA
 *  NOTHING IS OFFERED TO dqbuf().
 *
 *  THE DECISION ON WHETHER THE PARITY OF THE OFFERED FIELD BUFFER IS RIGHT
 *  RESTS WITH dqbuf().
 */
/*---------------------------------------------------------------------------*/
				if ((8 == more) || override) {
					if (videofieldamount >
							peasycap->videofieldamount) {
						if (2 == videofieldamount -
								peasycap->
								videofieldamount) {
							(peasycap->field_buffer
							[peasycap->field_fill]
								[0].kount) |= 0x0100;
							peasycap->video_junk += (1 +
								VIDEO_JUNK_TOLERATE);
						} else
							(peasycap->field_buffer
							[peasycap->field_fill]
								[0].kount) |= 0x4000;
						} else if (videofieldamount <
								peasycap->
								videofieldamount) {
							(peasycap->field_buffer
							[peasycap->field_fill]
								[0].kount) |= 0x2000;
						}
						bad = 0xFF00 & peasycap->field_buffer
							[peasycap->field_fill]
							[0].kount;
						if (!bad) {
							(peasycap->video_junk)--;
							if (-VIDEO_JUNK_TOLERATE >
								peasycap->video_junk)
								peasycap->video_junk =
								-VIDEO_JUNK_TOLERATE;
							peasycap->field_read =
								(peasycap->
									field_fill)++;
							if (FIELD_BUFFER_MANY <=
									peasycap->
									field_fill)
								peasycap->
									field_fill = 0;
							peasycap->field_page = 0;
							pfield_buffer = &peasycap->
								field_buffer
								[peasycap->
								field_fill]
								[peasycap->
								field_page];
							pfield_buffer->pto =
								pfield_buffer->pgo;
							JOM(8, "bumped to: %i="
								"peasycap->"
								"field_fill  %i="
								"parity\n",
								peasycap->field_fill,
								0x00FF &
								pfield_buffer->kount);
							JOM(8, "field buffer %i has "
								"%i bytes fit to be "
								"read\n",
								peasycap->field_read,
								videofieldamount);
							JOM(8, "wakeup call to "
								"wq_video, "
								"%i=field_read "
								"%i=field_fill "
								"%i=parity\n",
								peasycap->field_read,
								peasycap->field_fill,
								0x00FF & peasycap->
								field_buffer
								[peasycap->
								field_read][0].kount);
							wake_up_interruptible
								(&(peasycap->
									 wq_video));
						} else {
						peasycap->video_junk++;
						if (bad & 0x0010)
							peasycap->video_junk +=
							(1 + VIDEO_JUNK_TOLERATE/2);
						JOM(8, "field buffer %i had %i "
							"bytes, now discarded: "
							"0x%04X\n",
							peasycap->field_fill,
							videofieldamount,
							(0xFF00 &
							peasycap->field_buffer
							[peasycap->field_fill][0].
							kount));
						(peasycap->field_fill)++;

						if (FIELD_BUFFER_MANY <=
								peasycap->field_fill)
							peasycap->field_fill = 0;
						peasycap->field_page = 0;
						pfield_buffer =
							&peasycap->field_buffer
							[peasycap->field_fill]
							[peasycap->field_page];
						pfield_buffer->pto =
								pfield_buffer->pgo;

						JOM(8, "bumped to: %i=peasycap->"
							"field_fill  %i=parity\n",
							peasycap->field_fill,
							0x00FF & pfield_buffer->kount);
					}
					if (8 == more) {
						JOM(8, "end-of-field: received "
							"parity byte 0x%02X\n",
							(0xFF & *pu));
						if (0x40 & *pu)
							pfield_buffer->kount = 0x0000;
						else
							pfield_buffer->kount = 0x0001;
						pfield_buffer->input = 0x08 |
							(0x07 & peasycap->input);
						JOM(8, "end-of-field: 0x%02X=kount\n",
							0xFF & pfield_buffer->kount);
					}
				}
/*---------------------------------------------------------------------------*/
/*
 *  COPY more BYTES FROM ISOC BUFFER TO FIELD BUFFER
 */
/*---------------------------------------------------------------------------*/
				pu += leap;
				more -= leap;

				if (FIELD_BUFFER_MANY <= peasycap->field_fill) {
					SAM("ERROR: bad peasycap->field_fill\n");
					return;
				}
				if (FIELD_BUFFER_SIZE/PAGE_SIZE <= peasycap->field_page) {
					SAM("ERROR: bad peasycap->field_page\n");
					return;
				}
				pfield_buffer = &peasycap->field_buffer
					[peasycap->field_fill][peasycap->field_page];
				while (more) {
					pfield_buffer = &peasycap->field_buffer
							[peasycap->field_fill]
							[peasycap->field_page];
					if (PAGE_SIZE < (pfield_buffer->pto -
								pfield_buffer->pgo)) {
						SAM("ERROR: bad pfield_buffer->pto\n");
						return;
					}
					if (PAGE_SIZE == (pfield_buffer->pto -
								pfield_buffer->pgo)) {
						(peasycap->field_page)++;
						if (FIELD_BUFFER_SIZE/PAGE_SIZE <=
								peasycap->field_page) {
							JOM(16, "wrapping peasycap->"
								"field_page\n");
							peasycap->field_page = 0;
						}
						pfield_buffer = &peasycap->
								field_buffer
								[peasycap->field_fill]
								[peasycap->field_page];
						pfield_buffer->pto = pfield_buffer->pgo;
						pfield_buffer->input = 0x08 |
							(0x07 & peasycap->input);
						if ((peasycap->field_buffer[peasycap->
								field_fill][0]).
									input !=
								pfield_buffer->input)
							(peasycap->field_buffer
								[peasycap->field_fill]
								[0]).kount |= 0x1000;
					}

					much = PAGE_SIZE -
						(int)(pfield_buffer->pto -
							pfield_buffer->pgo);

					if (much > more)
						much = more;
					memcpy(pfield_buffer->pto, pu, much);
					pu += much;
					(pfield_buffer->pto) += much;
					more -= much;
					}
				}
			}
		}
	}
/*---------------------------------------------------------------------------*/
/*
 *  RESUBMIT THIS URB, UNLESS A SEVERE PERSISTENT ERROR CONDITION EXISTS.
 *
 *  IF THE WAIT QUEUES ARE NOT CLEARED IN RESPONSE TO AN ERROR CONDITION
 *  THE USERSPACE PROGRAM, E.G. mplayer, MAY HANG ON EXIT.   BEWARE.
 */
/*---------------------------------------------------------------------------*/
	if (VIDEO_ISOC_BUFFER_MANY <= peasycap->video_junk) {
		SAM("easycap driver shutting down on condition green\n");
		peasycap->status = 1;
		peasycap->video_eof = 1;
		peasycap->video_junk = 0;
		wake_up_interruptible(&peasycap->wq_video);
#if !defined(PERSEVERE)
		peasycap->audio_eof = 1;
		wake_up_interruptible(&peasycap->wq_audio);
#endif /*PERSEVERE*/
		return;
	}
	if (peasycap->video_isoc_streaming) {
		rc = usb_submit_urb(purb, GFP_ATOMIC);
		if (rc) {
			SAM("%s: %d\n", strerror(rc), rc);
			if (-ENODEV != rc)
				SAM("ERROR: while %i=video_idle, "
					"usb_submit_urb() "
					"failed with rc:\n",
					peasycap->video_idle);
		}
	}
	return;
}

static struct easycap *alloc_easycap(u8 bInterfaceNumber)
{
	struct easycap *peasycap;
	int i;

	peasycap = kzalloc(sizeof(struct easycap), GFP_KERNEL);
	if (!peasycap) {
		SAY("ERROR: Could not allocate peasycap\n");
		return NULL;
	}

	if (mutex_lock_interruptible(&mutex_dongle)) {
		SAY("ERROR: cannot lock mutex_dongle\n");
		kfree(peasycap);
		return NULL;
	}

	/* Find a free dongle in easycapdc60_dongle array */
	for (i = 0; i < DONGLE_MANY; i++) {

		if ((!easycapdc60_dongle[i].peasycap) &&
		    (!mutex_is_locked(&easycapdc60_dongle[i].mutex_video)) &&
		    (!mutex_is_locked(&easycapdc60_dongle[i].mutex_audio))) {

			easycapdc60_dongle[i].peasycap = peasycap;
			peasycap->isdongle = i;
			JOM(8, "intf[%i]: peasycap-->easycap"
				"_dongle[%i].peasycap\n",
				bInterfaceNumber, i);
			break;
		}
	}

	mutex_unlock(&mutex_dongle);

	if (i >= DONGLE_MANY) {
		SAM("ERROR: too many dongles\n");
		kfree(peasycap);
		return NULL;
	}

	return peasycap;
}

static void free_easycap(struct easycap *peasycap)
{
	int allocation_video_urb;
	int allocation_video_page;
	int allocation_video_struct;
	int allocation_audio_urb;
	int allocation_audio_page;
	int allocation_audio_struct;
	int registered_video, registered_audio;
	int kd;

	JOM(4, "freeing easycap structure.\n");
	allocation_video_urb    = peasycap->allocation_video_urb;
	allocation_video_page   = peasycap->allocation_video_page;
	allocation_video_struct = peasycap->allocation_video_struct;
	registered_video        = peasycap->registered_video;
	allocation_audio_urb    = peasycap->allocation_audio_urb;
	allocation_audio_page   = peasycap->allocation_audio_page;
	allocation_audio_struct = peasycap->allocation_audio_struct;
	registered_audio        = peasycap->registered_audio;

	kd = easycap_isdongle(peasycap);
	if (0 <= kd && DONGLE_MANY > kd) {
		if (mutex_lock_interruptible(&mutex_dongle)) {
			SAY("ERROR: cannot down mutex_dongle\n");
		} else {
			JOM(4, "locked mutex_dongle\n");
			easycapdc60_dongle[kd].peasycap = NULL;
			mutex_unlock(&mutex_dongle);
			JOM(4, "unlocked mutex_dongle\n");
			JOT(4, "   null-->dongle[%i].peasycap\n", kd);
			allocation_video_struct -= sizeof(struct easycap);
		}
	} else {
		SAY("ERROR: cannot purge dongle[].peasycap");
	}

	/* Free device structure */
	kfree(peasycap);

	SAY("%8i=video urbs    after all deletions\n", allocation_video_urb);
	SAY("%8i=video pages   after all deletions\n", allocation_video_page);
	SAY("%8i=video structs after all deletions\n", allocation_video_struct);
	SAY("%8i=video devices after all deletions\n", registered_video);
	SAY("%8i=audio urbs    after all deletions\n", allocation_audio_urb);
	SAY("%8i=audio pages   after all deletions\n", allocation_audio_page);
	SAY("%8i=audio structs after all deletions\n", allocation_audio_struct);
	SAY("%8i=audio devices after all deletions\n", registered_audio);
}

/*
 * FIXME: Identify the appropriate pointer peasycap for interfaces
 * 1 and 2. The address of peasycap->pusb_device is reluctantly used
 * for this purpose.
 */
static struct easycap *get_easycap(struct usb_device *usbdev,
				   u8 bInterfaceNumber)
{
	int i;
	struct easycap *peasycap;

	for (i = 0; i < DONGLE_MANY; i++) {
		if (easycapdc60_dongle[i].peasycap->pusb_device == usbdev) {
			peasycap = easycapdc60_dongle[i].peasycap;
			JOT(8, "intf[%i]: dongle[%i].peasycap\n",
					bInterfaceNumber, i);
			break;
		}
	}
	if (i >= DONGLE_MANY) {
		SAY("ERROR: peasycap is unknown when probing interface %i\n",
			bInterfaceNumber);
		return NULL;
	}
	if (!peasycap) {
		SAY("ERROR: peasycap is NULL when probing interface %i\n",
			bInterfaceNumber);
		return NULL;
	}

	return peasycap;
}

static void init_easycap(struct easycap *peasycap,
			 struct usb_device *usbdev,
			 struct usb_interface *intf,
			 u8 bInterfaceNumber)
{
	/* Save usb_device and usb_interface */
	peasycap->pusb_device = usbdev;
	peasycap->pusb_interface = intf;

	peasycap->minor = -1;
	kref_init(&peasycap->kref);
	JOM(8, "intf[%i]: after kref_init(..._video) "
		"%i=peasycap->kref.refcount.counter\n",
		bInterfaceNumber, peasycap->kref.refcount.counter);

	/* module params */
	peasycap->gain = (s8)clamp(easycap_gain, 0, 31);

	init_waitqueue_head(&peasycap->wq_video);
	init_waitqueue_head(&peasycap->wq_audio);
	init_waitqueue_head(&peasycap->wq_trigger);

	peasycap->allocation_video_struct = sizeof(struct easycap);

	peasycap->microphone = false;

	peasycap->video_interface = -1;
	peasycap->video_altsetting_on = -1;
	peasycap->video_altsetting_off = -1;
	peasycap->video_endpointnumber = -1;
	peasycap->video_isoc_maxframesize = -1;
	peasycap->video_isoc_buffer_size = -1;

	peasycap->audio_interface = -1;
	peasycap->audio_altsetting_on = -1;
	peasycap->audio_altsetting_off = -1;
	peasycap->audio_endpointnumber = -1;
	peasycap->audio_isoc_maxframesize = -1;
	peasycap->audio_isoc_buffer_size = -1;

	peasycap->frame_buffer_many = FRAME_BUFFER_MANY;

	peasycap->ntsc = easycap_ntsc;
	JOM(8, "defaulting initially to %s\n",
		easycap_ntsc ? "NTSC" : "PAL");
}

static int populate_inputset(struct easycap *peasycap)
{
	struct inputset *inputset;
	struct easycap_format *peasycap_format;
	struct v4l2_pix_format *pix;
	int m, i, k, mask, fmtidx;
	s32 value;

	inputset = peasycap->inputset;

	fmtidx = peasycap->ntsc ? NTSC_M : PAL_BGHIN;

	m = 0;
	mask = 0;
	for (i = 0; easycap_standard[i].mask != 0xffff; i++) {
		if (fmtidx == easycap_standard[i].v4l2_standard.index) {
			m++;
			for (k = 0; k < INPUT_MANY; k++)
				inputset[k].standard_offset = i;
			mask = easycap_standard[i].mask;
		}
	}

	if (m != 1) {
		SAM("ERROR: inputset->standard_offset unpopulated, %i=m\n", m);
		return -ENOENT;
	}

	peasycap_format = &easycap_format[0];
	m = 0;
	for (i = 0; peasycap_format->v4l2_format.fmt.pix.width; i++) {
		pix = &peasycap_format->v4l2_format.fmt.pix;
		if (((peasycap_format->mask & 0x0F) == (mask & 0x0F))
			&& pix->field == V4L2_FIELD_NONE
			&& pix->pixelformat == V4L2_PIX_FMT_UYVY
			&& pix->width  == 640 && pix->height == 480) {
			m++;
			for (k = 0; k < INPUT_MANY; k++)
				inputset[k].format_offset = i;
			break;
		}
		peasycap_format++;
	}
	if (m != 1) {
		SAM("ERROR: inputset[]->format_offset unpopulated\n");
		return -ENOENT;
	}

	m = 0;
	for (i = 0; easycap_control[i].id != 0xffffffff; i++) {
		value = easycap_control[i].default_value;
		if (V4L2_CID_BRIGHTNESS == easycap_control[i].id) {
			m++;
			for (k = 0; k < INPUT_MANY; k++)
				inputset[k].brightness = value;
		} else if (V4L2_CID_CONTRAST == easycap_control[i].id) {
			m++;
			for (k = 0; k < INPUT_MANY; k++)
				inputset[k].contrast = value;
		} else if (V4L2_CID_SATURATION == easycap_control[i].id) {
			m++;
			for (k = 0; k < INPUT_MANY; k++)
				inputset[k].saturation = value;
		} else if (V4L2_CID_HUE == easycap_control[i].id) {
			m++;
			for (k = 0; k < INPUT_MANY; k++)
				inputset[k].hue = value;
		}
	}

	if (m != 4) {
		SAM("ERROR: inputset[]->brightness underpopulated\n");
		return -ENOENT;
	}

	for (k = 0; k < INPUT_MANY; k++)
		inputset[k].input = k;
	JOM(4, "populated inputset[]\n");

	return 0;
}

static int alloc_framebuffers(struct easycap *peasycap)
{
	int i, j;
	void *pbuf;

	JOM(4, "allocating %i frame buffers of size %li\n",
			FRAME_BUFFER_MANY, (long int)FRAME_BUFFER_SIZE);
	JOM(4, ".... each scattered over %li pages\n",
			FRAME_BUFFER_SIZE/PAGE_SIZE);

	for (i = 0; i < FRAME_BUFFER_MANY; i++) {
		for (j = 0; j < FRAME_BUFFER_SIZE/PAGE_SIZE; j++) {
			if (peasycap->frame_buffer[i][j].pgo)
				SAM("attempting to reallocate framebuffers\n");
			else {
				pbuf = (void *)__get_free_page(GFP_KERNEL);
				if (!pbuf) {
					SAM("ERROR: Could not allocate "
					"framebuffer %i page %i\n", i, j);
					return -ENOMEM;
				}
				peasycap->allocation_video_page += 1;
				peasycap->frame_buffer[i][j].pgo = pbuf;
			}
			peasycap->frame_buffer[i][j].pto =
			    peasycap->frame_buffer[i][j].pgo;
		}
	}

	peasycap->frame_fill = 0;
	peasycap->frame_read = 0;
	JOM(4, "allocation of frame buffers done: %i pages\n", i*j);

	return 0;
}

static void free_framebuffers(struct easycap *peasycap)
{
	int k, m, gone;

	JOM(4, "freeing video frame buffers.\n");
	gone = 0;
	for (k = 0;  k < FRAME_BUFFER_MANY;  k++) {
		for (m = 0;  m < FRAME_BUFFER_SIZE/PAGE_SIZE;  m++) {
			if (peasycap->frame_buffer[k][m].pgo) {
				free_page((unsigned long)
					peasycap->frame_buffer[k][m].pgo);
				peasycap->frame_buffer[k][m].pgo = NULL;
				peasycap->allocation_video_page -= 1;
				gone++;
			}
		}
	}
	JOM(4, "video frame buffers freed: %i pages\n", gone);
}

static int alloc_fieldbuffers(struct easycap *peasycap)
{
	int i, j;
	void *pbuf;

	JOM(4, "allocating %i field buffers of size %li\n",
			FIELD_BUFFER_MANY, (long int)FIELD_BUFFER_SIZE);
	JOM(4, ".... each scattered over %li pages\n",
			FIELD_BUFFER_SIZE/PAGE_SIZE);

	for (i = 0; i < FIELD_BUFFER_MANY; i++) {
		for (j = 0; j < FIELD_BUFFER_SIZE/PAGE_SIZE; j++) {
			if (peasycap->field_buffer[i][j].pgo) {
				SAM("ERROR: attempting to reallocate "
					"fieldbuffers\n");
			} else {
				pbuf = (void *) __get_free_page(GFP_KERNEL);
				if (!pbuf) {
					SAM("ERROR: Could not allocate "
					"fieldbuffer %i page %i\n", i, j);
					return -ENOMEM;
				}
				peasycap->allocation_video_page += 1;
				peasycap->field_buffer[i][j].pgo = pbuf;
			}
			peasycap->field_buffer[i][j].pto =
				peasycap->field_buffer[i][j].pgo;
		}
		/* TODO: Hardcoded 0x0200 meaning? */
		peasycap->field_buffer[i][0].kount = 0x0200;
	}
	peasycap->field_fill = 0;
	peasycap->field_page = 0;
	peasycap->field_read = 0;
	JOM(4, "allocation of field buffers done:  %i pages\n", i*j);

	return 0;
}

static void free_fieldbuffers(struct easycap *peasycap)
{
	int k, m, gone;

	JOM(4, "freeing video field buffers.\n");
	gone = 0;
	for (k = 0;  k < FIELD_BUFFER_MANY;  k++) {
		for (m = 0;  m < FIELD_BUFFER_SIZE/PAGE_SIZE;  m++) {
			if (peasycap->field_buffer[k][m].pgo) {
				free_page((unsigned long)
					  peasycap->field_buffer[k][m].pgo);
				peasycap->field_buffer[k][m].pgo = NULL;
				peasycap->allocation_video_page -= 1;
				gone++;
			}
		}
	}
	JOM(4, "video field buffers freed: %i pages\n", gone);
}

static int alloc_isocbuffers(struct easycap *peasycap)
{
	int i;
	void *pbuf;

	JOM(4, "allocating %i isoc video buffers of size %i\n",
			VIDEO_ISOC_BUFFER_MANY,
			peasycap->video_isoc_buffer_size);
	JOM(4, ".... each occupying contiguous memory pages\n");

	for (i = 0; i < VIDEO_ISOC_BUFFER_MANY; i++) {
		pbuf = (void *)__get_free_pages(GFP_KERNEL,
				VIDEO_ISOC_ORDER);
		if (!pbuf) {
			SAM("ERROR: Could not allocate isoc "
				"video buffer %i\n", i);
			return -ENOMEM;
		}
		peasycap->allocation_video_page += BIT(VIDEO_ISOC_ORDER);

		peasycap->video_isoc_buffer[i].pgo = pbuf;
		peasycap->video_isoc_buffer[i].pto =
			pbuf + peasycap->video_isoc_buffer_size;
		peasycap->video_isoc_buffer[i].kount = i;
	}
	JOM(4, "allocation of isoc video buffers done: %i pages\n",
			i * (0x01 << VIDEO_ISOC_ORDER));
	return 0;
}

static void free_isocbuffers(struct easycap *peasycap)
{
	int k, m;

	JOM(4, "freeing video isoc buffers.\n");
	m = 0;
	for (k = 0;  k < VIDEO_ISOC_BUFFER_MANY;  k++) {
		if (peasycap->video_isoc_buffer[k].pgo) {
			free_pages((unsigned long)
				   peasycap->video_isoc_buffer[k].pgo,
					VIDEO_ISOC_ORDER);
			peasycap->video_isoc_buffer[k].pgo = NULL;
			peasycap->allocation_video_page -=
						BIT(VIDEO_ISOC_ORDER);
			m++;
		}
	}
	JOM(4, "isoc video buffers freed: %i pages\n",
			m * (0x01 << VIDEO_ISOC_ORDER));
}

static int create_video_urbs(struct easycap *peasycap)
{
	struct urb *purb;
	struct data_urb *pdata_urb;
	int i, j;

	JOM(4, "allocating %i struct urb.\n", VIDEO_ISOC_BUFFER_MANY);
	JOM(4, "using %i=peasycap->video_isoc_framesperdesc\n",
			peasycap->video_isoc_framesperdesc);
	JOM(4, "using %i=peasycap->video_isoc_maxframesize\n",
			peasycap->video_isoc_maxframesize);
	JOM(4, "using %i=peasycap->video_isoc_buffer_sizen",
			peasycap->video_isoc_buffer_size);

	for (i = 0; i < VIDEO_ISOC_BUFFER_MANY; i++) {
		purb = usb_alloc_urb(peasycap->video_isoc_framesperdesc,
				GFP_KERNEL);
		if (!purb) {
			SAM("ERROR: usb_alloc_urb returned NULL for buffer "
				"%i\n", i);
			return -ENOMEM;
		}

		peasycap->allocation_video_urb += 1;
		pdata_urb = kzalloc(sizeof(struct data_urb), GFP_KERNEL);
		if (!pdata_urb) {
			SAM("ERROR: Could not allocate struct data_urb.\n");
			return -ENOMEM;
		}

		peasycap->allocation_video_struct +=
			sizeof(struct data_urb);

		pdata_urb->purb = purb;
		pdata_urb->isbuf = i;
		pdata_urb->length = 0;
		list_add_tail(&(pdata_urb->list_head),
				peasycap->purb_video_head);

		if (!i) {
			JOM(4, "initializing video urbs thus:\n");
			JOM(4, "  purb->interval = 1;\n");
			JOM(4, "  purb->dev = peasycap->pusb_device;\n");
			JOM(4, "  purb->pipe = usb_rcvisocpipe"
					"(peasycap->pusb_device,%i);\n",
					peasycap->video_endpointnumber);
			JOM(4, "  purb->transfer_flags = URB_ISO_ASAP;\n");
			JOM(4, "  purb->transfer_buffer = peasycap->"
					"video_isoc_buffer[.].pgo;\n");
			JOM(4, "  purb->transfer_buffer_length = %i;\n",
					peasycap->video_isoc_buffer_size);
			JOM(4, "  purb->complete = easycap_complete;\n");
			JOM(4, "  purb->context = peasycap;\n");
			JOM(4, "  purb->start_frame = 0;\n");
			JOM(4, "  purb->number_of_packets = %i;\n",
					peasycap->video_isoc_framesperdesc);
			JOM(4, "  for (j = 0; j < %i; j++)\n",
					peasycap->video_isoc_framesperdesc);
			JOM(4, "    {\n");
			JOM(4, "    purb->iso_frame_desc[j].offset = j*%i;\n",
					peasycap->video_isoc_maxframesize);
			JOM(4, "    purb->iso_frame_desc[j].length = %i;\n",
					peasycap->video_isoc_maxframesize);
			JOM(4, "    }\n");
		}

		purb->interval = 1;
		purb->dev = peasycap->pusb_device;
		purb->pipe = usb_rcvisocpipe(peasycap->pusb_device,
				peasycap->video_endpointnumber);

		purb->transfer_flags = URB_ISO_ASAP;
		purb->transfer_buffer = peasycap->video_isoc_buffer[i].pgo;
		purb->transfer_buffer_length =
			peasycap->video_isoc_buffer_size;

		purb->complete = easycap_complete;
		purb->context = peasycap;
		purb->start_frame = 0;
		purb->number_of_packets = peasycap->video_isoc_framesperdesc;

		for (j = 0; j < peasycap->video_isoc_framesperdesc; j++) {
			purb->iso_frame_desc[j].offset =
				j * peasycap->video_isoc_maxframesize;
			purb->iso_frame_desc[j].length =
				peasycap->video_isoc_maxframesize;
		}
	}
	JOM(4, "allocation of %i struct urb done.\n", i);
	return 0;
}

static void free_video_urbs(struct easycap *peasycap)
{
	struct list_head *plist_head, *plist_next;
	struct data_urb *pdata_urb;
	int m;

	if (peasycap->purb_video_head) {
		m = 0;
		list_for_each(plist_head, peasycap->purb_video_head) {
			pdata_urb = list_entry(plist_head,
					struct data_urb, list_head);
			if (pdata_urb && pdata_urb->purb) {
				usb_free_urb(pdata_urb->purb);
				pdata_urb->purb = NULL;
				peasycap->allocation_video_urb--;
				m++;
			}
		}

		JOM(4, "%i video urbs freed\n", m);
		JOM(4, "freeing video data_urb structures.\n");
		m = 0;
		list_for_each_safe(plist_head, plist_next,
					peasycap->purb_video_head) {
			pdata_urb = list_entry(plist_head,
					struct data_urb, list_head);
			if (pdata_urb) {
				peasycap->allocation_video_struct -=
					sizeof(struct data_urb);
				kfree(pdata_urb);
				m++;
			}
		}
		JOM(4, "%i video data_urb structures freed\n", m);
		JOM(4, "setting peasycap->purb_video_head=NULL\n");
		peasycap->purb_video_head = NULL;
	}
}

static int alloc_audio_buffers(struct easycap *peasycap)
{
	void *pbuf;
	int k;

	JOM(4, "allocating %i isoc audio buffers of size %i\n",
		AUDIO_ISOC_BUFFER_MANY,
		peasycap->audio_isoc_buffer_size);
	JOM(4, ".... each occupying contiguous memory pages\n");

	for (k = 0;  k < AUDIO_ISOC_BUFFER_MANY;  k++) {
		pbuf = (void *)__get_free_pages(GFP_KERNEL, AUDIO_ISOC_ORDER);
		if (!pbuf) {
			SAM("ERROR: Could not allocate isoc audio buffer %i\n",
			    k);
				return -ENOMEM;
		}
		peasycap->allocation_audio_page += BIT(AUDIO_ISOC_ORDER);

		peasycap->audio_isoc_buffer[k].pgo = pbuf;
		peasycap->audio_isoc_buffer[k].pto =
			pbuf + peasycap->audio_isoc_buffer_size;
		peasycap->audio_isoc_buffer[k].kount = k;
	}

	JOM(4, "allocation of isoc audio buffers done.\n");
	return 0;
}

static void free_audio_buffers(struct easycap *peasycap)
{
	int k, m;

	JOM(4, "freeing audio isoc buffers.\n");
	m = 0;
	for (k = 0;  k < AUDIO_ISOC_BUFFER_MANY;  k++) {
		if (peasycap->audio_isoc_buffer[k].pgo) {
			free_pages((unsigned long)
					(peasycap->audio_isoc_buffer[k].pgo),
					AUDIO_ISOC_ORDER);
			peasycap->audio_isoc_buffer[k].pgo = NULL;
			peasycap->allocation_audio_page -=
					BIT(AUDIO_ISOC_ORDER);
			m++;
		}
	}
	JOM(4, "easyoss_delete(): isoc audio buffers freed: %i pages\n",
					m * (0x01 << AUDIO_ISOC_ORDER));
}

static int create_audio_urbs(struct easycap *peasycap)
{
	struct urb *purb;
	struct data_urb *pdata_urb;
	int k, j;

	JOM(4, "allocating %i struct urb.\n", AUDIO_ISOC_BUFFER_MANY);
	JOM(4, "using %i=peasycap->audio_isoc_framesperdesc\n",
		peasycap->audio_isoc_framesperdesc);
	JOM(4, "using %i=peasycap->audio_isoc_maxframesize\n",
		peasycap->audio_isoc_maxframesize);
	JOM(4, "using %i=peasycap->audio_isoc_buffer_size\n",
		peasycap->audio_isoc_buffer_size);

	for (k = 0;  k < AUDIO_ISOC_BUFFER_MANY; k++) {
		purb = usb_alloc_urb(peasycap->audio_isoc_framesperdesc,
				     GFP_KERNEL);
		if (!purb) {
			SAM("ERROR: usb_alloc_urb returned NULL for buffer "
			     "%i\n", k);
			return -ENOMEM;
		}
		peasycap->allocation_audio_urb += 1 ;
		pdata_urb = kzalloc(sizeof(struct data_urb), GFP_KERNEL);
		if (!pdata_urb) {
			usb_free_urb(purb);
			SAM("ERROR: Could not allocate struct data_urb.\n");
			return -ENOMEM;
		}
		peasycap->allocation_audio_struct +=
			sizeof(struct data_urb);

		pdata_urb->purb = purb;
		pdata_urb->isbuf = k;
		pdata_urb->length = 0;
		list_add_tail(&(pdata_urb->list_head),
				peasycap->purb_audio_head);

		if (!k) {
			JOM(4, "initializing audio urbs thus:\n");
			JOM(4, "  purb->interval = 1;\n");
			JOM(4, "  purb->dev = peasycap->pusb_device;\n");
			JOM(4, "  purb->pipe = usb_rcvisocpipe(peasycap->"
				"pusb_device,%i);\n",
				peasycap->audio_endpointnumber);
			JOM(4, "  purb->transfer_flags = URB_ISO_ASAP;\n");
			JOM(4, "  purb->transfer_buffer = "
				"peasycap->audio_isoc_buffer[.].pgo;\n");
			JOM(4, "  purb->transfer_buffer_length = %i;\n",
				peasycap->audio_isoc_buffer_size);
			JOM(4, "  purb->complete = easycap_alsa_complete;\n");
			JOM(4, "  purb->context = peasycap;\n");
			JOM(4, "  purb->start_frame = 0;\n");
			JOM(4, "  purb->number_of_packets = %i;\n",
				peasycap->audio_isoc_framesperdesc);
			JOM(4, "  for (j = 0; j < %i; j++)\n",
				peasycap->audio_isoc_framesperdesc);
			JOM(4, "    {\n");
			JOM(4, "    purb->iso_frame_desc[j].offset = j*%i;\n",
				peasycap->audio_isoc_maxframesize);
			JOM(4, "    purb->iso_frame_desc[j].length = %i;\n",
				peasycap->audio_isoc_maxframesize);
			JOM(4, "    }\n");
		}

		purb->interval = 1;
		purb->dev = peasycap->pusb_device;
		purb->pipe = usb_rcvisocpipe(peasycap->pusb_device,
					     peasycap->audio_endpointnumber);
		purb->transfer_flags = URB_ISO_ASAP;
		purb->transfer_buffer = peasycap->audio_isoc_buffer[k].pgo;
		purb->transfer_buffer_length =
			peasycap->audio_isoc_buffer_size;
		purb->complete = easycap_alsa_complete;
		purb->context = peasycap;
		purb->start_frame = 0;
		purb->number_of_packets = peasycap->audio_isoc_framesperdesc;
		for (j = 0;  j < peasycap->audio_isoc_framesperdesc; j++) {
			purb->iso_frame_desc[j].offset =
				j * peasycap->audio_isoc_maxframesize;
			purb->iso_frame_desc[j].length =
				peasycap->audio_isoc_maxframesize;
		}
	}
	JOM(4, "allocation of %i struct urb done.\n", k);
	return 0;
}

static void free_audio_urbs(struct easycap *peasycap)
{
	struct list_head *plist_head, *plist_next;
	struct data_urb *pdata_urb;
	int m;

	if (peasycap->purb_audio_head) {
		JOM(4, "freeing audio urbs\n");
		m = 0;
		list_for_each(plist_head, (peasycap->purb_audio_head)) {
			pdata_urb = list_entry(plist_head,
					struct data_urb, list_head);
			if (pdata_urb && pdata_urb->purb) {
				usb_free_urb(pdata_urb->purb);
				pdata_urb->purb = NULL;
				peasycap->allocation_audio_urb--;
				m++;
			}
		}
		JOM(4, "%i audio urbs freed\n", m);
		JOM(4, "freeing audio data_urb structures.\n");
		m = 0;
		list_for_each_safe(plist_head, plist_next,
					peasycap->purb_audio_head) {
			pdata_urb = list_entry(plist_head,
					struct data_urb, list_head);
			if (pdata_urb) {
				peasycap->allocation_audio_struct -=
							sizeof(struct data_urb);
				kfree(pdata_urb);
				m++;
			}
		}
		JOM(4, "%i audio data_urb structures freed\n", m);
		JOM(4, "setting peasycap->purb_audio_head=NULL\n");
		peasycap->purb_audio_head = NULL;
	}
}

static void config_easycap(struct easycap *peasycap,
			   u8 bInterfaceNumber,
			   u8 bInterfaceClass,
			   u8 bInterfaceSubClass)
{
	if ((USB_CLASS_VIDEO == bInterfaceClass) ||
	    (USB_CLASS_VENDOR_SPEC == bInterfaceClass)) {
		if (-1 == peasycap->video_interface) {
			peasycap->video_interface = bInterfaceNumber;
			JOM(4, "setting peasycap->video_interface=%i\n",
				peasycap->video_interface);
		} else {
			if (peasycap->video_interface != bInterfaceNumber) {
				SAM("ERROR: attempting to reset "
				    "peasycap->video_interface\n");
				SAM("...... continuing with "
				    "%i=peasycap->video_interface\n",
				    peasycap->video_interface);
			}
		}
	} else if ((USB_CLASS_AUDIO == bInterfaceClass) &&
		   (USB_SUBCLASS_AUDIOSTREAMING == bInterfaceSubClass)) {
		if (-1 == peasycap->audio_interface) {
			peasycap->audio_interface = bInterfaceNumber;
			JOM(4, "setting peasycap->audio_interface=%i\n",
				peasycap->audio_interface);
		} else {
			if (peasycap->audio_interface != bInterfaceNumber) {
				SAM("ERROR: attempting to reset "
				    "peasycap->audio_interface\n");
				SAM("...... continuing with "
				    "%i=peasycap->audio_interface\n",
				    peasycap->audio_interface);
			}
		}
	}
}

/*
 * This function is called from within easycap_usb_disconnect() and is
 * protected by semaphores set and cleared by easycap_usb_disconnect().
 * By this stage the device has already been physically unplugged,
 * so peasycap->pusb_device is no longer valid.
 */
static void easycap_delete(struct kref *pkref)
{
	struct easycap *peasycap;

	peasycap = container_of(pkref, struct easycap, kref);
	if (!peasycap) {
		SAM("ERROR: peasycap is NULL: cannot perform deletions\n");
		return;
	}

	/* Free video urbs */
	free_video_urbs(peasycap);

	/* Free video isoc buffers */
	free_isocbuffers(peasycap);

	/* Free video field buffers */
	free_fieldbuffers(peasycap);

	/* Free video frame buffers */
	free_framebuffers(peasycap);

	/* Free audio urbs */
	free_audio_urbs(peasycap);

	/* Free audio isoc buffers */
	free_audio_buffers(peasycap);

	free_easycap(peasycap);

	JOT(4, "ending.\n");
}

static const struct v4l2_file_operations v4l2_fops = {
	.owner		= THIS_MODULE,
	.open		= easycap_open_noinode,
	.unlocked_ioctl	= easycap_unlocked_ioctl,
	.poll		= easycap_poll,
	.mmap		= easycap_mmap,
};

static int easycap_register_video(struct easycap *peasycap)
{
	/*
	 * FIXME: This is believed to be harmless,
	 * but may well be unnecessary or wrong.
	 */
	peasycap->video_device.v4l2_dev = NULL;

	strcpy(&peasycap->video_device.name[0], "easycapdc60");
	peasycap->video_device.fops = &v4l2_fops;
	peasycap->video_device.minor = -1;
	peasycap->video_device.release = (void *)(&videodev_release);

	video_set_drvdata(&(peasycap->video_device), (void *)peasycap);

	if (0 != (video_register_device(&(peasycap->video_device),
					VFL_TYPE_GRABBER, -1))) {
		err("Not able to register with videodev");
		videodev_release(&(peasycap->video_device));
		return -ENODEV;
	}

	peasycap->registered_video++;

	SAM("registered with videodev: %i=minor\n",
	    peasycap->video_device.minor);
	    peasycap->minor = peasycap->video_device.minor;

	return 0;
}

/*
 * When the device is plugged, this function is called three times,
 * one for each interface.
 */
static int easycap_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	struct usb_host_interface *alt;
	struct usb_endpoint_descriptor *ep;
	struct usb_interface_descriptor *interface;
	struct easycap *peasycap;
	int i, j, rc;
	u8 bInterfaceNumber;
	u8 bInterfaceClass;
	u8 bInterfaceSubClass;
	int okalt[8], isokalt;
	int okepn[8];
	int okmps[8];
	int maxpacketsize;

	usbdev = interface_to_usbdev(intf);

	alt = usb_altnum_to_altsetting(intf, 0);
	if (!alt) {
		SAY("ERROR: usb_host_interface not found\n");
		return -EFAULT;
	}

	interface = &alt->desc;
	if (!interface) {
		SAY("ERROR: intf_descriptor is NULL\n");
		return -EFAULT;
	}

	/* Get properties of probed interface */
	bInterfaceNumber = interface->bInterfaceNumber;
	bInterfaceClass = interface->bInterfaceClass;
	bInterfaceSubClass = interface->bInterfaceSubClass;

	JOT(4, "intf[%i]: num_altsetting=%i\n",
			bInterfaceNumber, intf->num_altsetting);
	JOT(4, "intf[%i]: cur_altsetting - altsetting=%li\n",
		bInterfaceNumber,
		(long int)(intf->cur_altsetting - intf->altsetting));
	JOT(4, "intf[%i]: bInterfaceClass=0x%02X bInterfaceSubClass=0x%02X\n",
			bInterfaceNumber, bInterfaceClass, bInterfaceSubClass);

	/*
	 * A new struct easycap is always allocated when interface 0 is probed.
	 * It is not possible here to free any existing struct easycap.
	 * This should have been done by easycap_delete() when the device was
	 * physically unplugged.
	 * The allocated struct easycap is saved for later usage when
	 * interfaces 1 and 2 are probed.
	 */
	if (0 == bInterfaceNumber) {
		/*
		 * Alloc structure and save it in a free slot in
		 * easycapdc60_dongle array
		 */
		peasycap = alloc_easycap(bInterfaceNumber);
		if (!peasycap)
			return -ENOMEM;

		/* Perform basic struct initialization */
		init_easycap(peasycap, usbdev, intf, bInterfaceNumber);

		/* Dynamically fill in the available formats */
		rc = easycap_video_fillin_formats();
		if (0 > rc) {
			SAM("ERROR: fillin_formats() rc = %i\n", rc);
			return -EFAULT;
		}
		JOM(4, "%i formats available\n", rc);

		/* Populate easycap.inputset[] */
		rc = populate_inputset(peasycap);
		if (rc < 0)
			return rc;
		JOM(4, "finished initialization\n");
	} else {
		peasycap = get_easycap(usbdev, bInterfaceNumber);
		if (!peasycap)
			return -ENODEV;
	}

	config_easycap(peasycap, bInterfaceNumber,
				 bInterfaceClass,
				 bInterfaceSubClass);

	/*
	 * Investigate all altsettings. This is done in detail
	 * because USB device 05e1:0408 has disparate incarnations.
	 */
	isokalt = 0;
	for (i = 0; i < intf->num_altsetting; i++) {
		alt = usb_altnum_to_altsetting(intf, i);
		if (!alt) {
			SAM("ERROR: alt is NULL\n");
			return -EFAULT;
		}
		interface = &alt->desc;
		if (!interface) {
			SAM("ERROR: intf_descriptor is NULL\n");
			return -EFAULT;
		}

		if (0 == interface->bNumEndpoints)
			JOM(4, "intf[%i]alt[%i] has no endpoints\n",
						bInterfaceNumber, i);
		for (j = 0; j < interface->bNumEndpoints; j++) {
			ep = &alt->endpoint[j].desc;
			if (!ep) {
				SAM("ERROR:  ep is NULL.\n");
				SAM("...... skipping\n");
				continue;
			}

			if (!usb_endpoint_is_isoc_in(ep)) {
				JOM(4, "intf[%i]alt[%i]end[%i] is a %d endpoint\n",
						bInterfaceNumber,
						i, j, ep->bmAttributes);
				if (usb_endpoint_dir_out(ep)) {
					SAM("ERROR: OUT endpoint unexpected\n");
					SAM("...... continuing\n");
				}
				continue;
			}
			switch (bInterfaceClass) {
			case USB_CLASS_VIDEO:
			case USB_CLASS_VENDOR_SPEC: {
				if (ep->wMaxPacketSize) {
					if (8 > isokalt) {
						okalt[isokalt] = i;
						JOM(4,
						"%i=okalt[%i]\n",
						okalt[isokalt],
						isokalt);
						okepn[isokalt] =
						ep->
						bEndpointAddress &
						0x0F;
						JOM(4,
						"%i=okepn[%i]\n",
						okepn[isokalt],
						isokalt);
						okmps[isokalt] =
						le16_to_cpu(ep->
						wMaxPacketSize);
						JOM(4,
						"%i=okmps[%i]\n",
						okmps[isokalt],
						isokalt);
						isokalt++;
					}
				} else {
					if (-1 == peasycap->
						video_altsetting_off) {
						peasycap->
						video_altsetting_off =
								 i;
						JOM(4, "%i=video_"
						"altsetting_off "
							"<====\n",
						peasycap->
						video_altsetting_off);
					} else {
						SAM("ERROR: peasycap"
						"->video_altsetting_"
						"off already set\n");
						SAM("...... "
						"continuing with "
						"%i=peasycap->video_"
						"altsetting_off\n",
						peasycap->
						video_altsetting_off);
					}
				}
				break;
			}
			case USB_CLASS_AUDIO: {
				if (bInterfaceSubClass !=
				    USB_SUBCLASS_AUDIOSTREAMING)
					break;
				if (!peasycap) {
					SAM("MISTAKE: "
					"peasycap is NULL\n");
					return -EFAULT;
				}
				if (ep->wMaxPacketSize) {
					if (8 > isokalt) {
						okalt[isokalt] = i ;
						JOM(4,
						"%i=okalt[%i]\n",
						okalt[isokalt],
						isokalt);
						okepn[isokalt] =
						ep->
						bEndpointAddress &
						0x0F;
						JOM(4,
						"%i=okepn[%i]\n",
						okepn[isokalt],
						isokalt);
						okmps[isokalt] =
						le16_to_cpu(ep->
						wMaxPacketSize);
						JOM(4,
						"%i=okmps[%i]\n",
						okmps[isokalt],
						isokalt);
						isokalt++;
					}
				} else {
					if (-1 == peasycap->
						audio_altsetting_off) {
						peasycap->
						audio_altsetting_off =
								 i;
						JOM(4, "%i=audio_"
						"altsetting_off "
						"<====\n",
						peasycap->
						audio_altsetting_off);
					} else {
						SAM("ERROR: peasycap"
						"->audio_altsetting_"
						"off already set\n");
						SAM("...... "
						"continuing with "
						"%i=peasycap->"
						"audio_altsetting_"
						"off\n",
						peasycap->
						audio_altsetting_off);
					}
				}
			break;
			}
			default:
				break;
			}
			if (0 == ep->wMaxPacketSize) {
				JOM(4, "intf[%i]alt[%i]end[%i] "
							"has zero packet size\n",
							bInterfaceNumber, i, j);
			}
		}
	}

	/* Perform initialization of the probed interface */
	JOM(4, "initialization begins for interface %i\n",
		interface->bInterfaceNumber);
	switch (bInterfaceNumber) {
	/* 0: Video interface */
	case 0: {
		if (!peasycap) {
			SAM("MISTAKE: peasycap is NULL\n");
			return -EFAULT;
		}
		if (!isokalt) {
			SAM("ERROR:  no viable video_altsetting_on\n");
			return -ENOENT;
		}
		peasycap->video_altsetting_on = okalt[isokalt - 1];
		JOM(4, "%i=video_altsetting_on <====\n",
					peasycap->video_altsetting_on);

		/* Decide video streaming parameters */
		peasycap->video_endpointnumber = okepn[isokalt - 1];
		JOM(4, "%i=video_endpointnumber\n", peasycap->video_endpointnumber);
		maxpacketsize = okmps[isokalt - 1];

		peasycap->video_isoc_maxframesize =
				min(maxpacketsize, USB_2_0_MAXPACKETSIZE);
		if (0 >= peasycap->video_isoc_maxframesize) {
			SAM("ERROR:  bad video_isoc_maxframesize\n");
			SAM("        possibly because port is USB 1.1\n");
			return -ENOENT;
		}
		JOM(4, "%i=video_isoc_maxframesize\n",
					peasycap->video_isoc_maxframesize);

		peasycap->video_isoc_framesperdesc = VIDEO_ISOC_FRAMESPERDESC;
		JOM(4, "%i=video_isoc_framesperdesc\n",
					peasycap->video_isoc_framesperdesc);
		if (0 >= peasycap->video_isoc_framesperdesc) {
			SAM("ERROR:  bad video_isoc_framesperdesc\n");
			return -ENOENT;
		}
		peasycap->video_isoc_buffer_size =
					peasycap->video_isoc_maxframesize *
					peasycap->video_isoc_framesperdesc;
		JOM(4, "%i=video_isoc_buffer_size\n",
					peasycap->video_isoc_buffer_size);
		if ((PAGE_SIZE << VIDEO_ISOC_ORDER) <
					peasycap->video_isoc_buffer_size) {
			SAM("MISTAKE: peasycap->video_isoc_buffer_size too big\n");
			return -EFAULT;
		}
		if (-1 == peasycap->video_interface) {
			SAM("MISTAKE:  video_interface is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->video_altsetting_on) {
			SAM("MISTAKE:  video_altsetting_on is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->video_altsetting_off) {
			SAM("MISTAKE:  video_interface_off is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->video_endpointnumber) {
			SAM("MISTAKE:  video_endpointnumber is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->video_isoc_maxframesize) {
			SAM("MISTAKE:  video_isoc_maxframesize is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->video_isoc_buffer_size) {
			SAM("MISTAKE:  video_isoc_buffer_size is unset\n");
			return -EFAULT;
		}

		/*
		 * Allocate memory for video buffers.
		 * Lists must be initialized first.
		 */
		INIT_LIST_HEAD(&(peasycap->urb_video_head));
		peasycap->purb_video_head = &(peasycap->urb_video_head);

		rc = alloc_framebuffers(peasycap);
		if (rc < 0)
			return rc;

		rc = alloc_fieldbuffers(peasycap);
		if (rc < 0)
			return rc;

		rc = alloc_isocbuffers(peasycap);
		if (rc < 0)
			return rc;

		/* Allocate and initialize video urbs */
		rc = create_video_urbs(peasycap);
		if (rc < 0)
			return rc;

		/* Save pointer peasycap in this interface */
		usb_set_intfdata(intf, peasycap);

		/*
		 * It is essential to initialize the hardware before,
		 * rather than after, the device is registered,
		 * because some udev rules triggers easycap_open()
		 * immediately after registration, causing a clash.
		 */
		rc = reset(peasycap);
		if (rc) {
			SAM("ERROR: reset() rc = %i\n", rc);
			return -EFAULT;
		}

		/* The video device can now be registered */
		if (v4l2_device_register(&intf->dev, &peasycap->v4l2_device)) {
			SAM("v4l2_device_register() failed\n");
			return -ENODEV;
		}
		JOM(4, "registered device instance: %s\n",
			peasycap->v4l2_device.name);

		rc = easycap_register_video(peasycap);
		if (rc < 0)
			return -ENODEV;
		break;
	}
	/* 1: Audio control */
	case 1: {
		if (!peasycap) {
			SAM("MISTAKE: peasycap is NULL\n");
			return -EFAULT;
		}
		/* Save pointer peasycap in this interface */
		usb_set_intfdata(intf, peasycap);
		JOM(4, "no initialization required for interface %i\n",
					interface->bInterfaceNumber);
		break;
	}
	/* 2: Audio streaming */
	case 2: {
		if (!peasycap) {
			SAM("MISTAKE: peasycap is NULL\n");
			return -EFAULT;
		}
		if (!isokalt) {
			SAM("ERROR:  no viable audio_altsetting_on\n");
			return -ENOENT;
		}
		peasycap->audio_altsetting_on = okalt[isokalt - 1];
		JOM(4, "%i=audio_altsetting_on <====\n",
						peasycap->audio_altsetting_on);

		peasycap->audio_endpointnumber = okepn[isokalt - 1];
		JOM(4, "%i=audio_endpointnumber\n", peasycap->audio_endpointnumber);

		peasycap->audio_isoc_maxframesize = okmps[isokalt - 1];
		JOM(4, "%i=audio_isoc_maxframesize\n",
						peasycap->audio_isoc_maxframesize);
		if (0 >= peasycap->audio_isoc_maxframesize) {
			SAM("ERROR:  bad audio_isoc_maxframesize\n");
			return -ENOENT;
		}
		if (9 == peasycap->audio_isoc_maxframesize) {
			peasycap->ilk |= 0x02;
			SAM("audio hardware is microphone\n");
			peasycap->microphone = true;
			peasycap->audio_pages_per_fragment =
					PAGES_PER_AUDIO_FRAGMENT;
		} else if (256 == peasycap->audio_isoc_maxframesize) {
			peasycap->ilk &= ~0x02;
			SAM("audio hardware is AC'97\n");
			peasycap->microphone = false;
			peasycap->audio_pages_per_fragment =
					PAGES_PER_AUDIO_FRAGMENT;
		} else {
			SAM("hardware is unidentified:\n");
			SAM("%i=audio_isoc_maxframesize\n",
				peasycap->audio_isoc_maxframesize);
			return -ENOENT;
		}

		peasycap->audio_bytes_per_fragment =
				peasycap->audio_pages_per_fragment * PAGE_SIZE;
		peasycap->audio_buffer_page_many = (AUDIO_FRAGMENT_MANY *
				peasycap->audio_pages_per_fragment);

		JOM(4, "%6i=AUDIO_FRAGMENT_MANY\n", AUDIO_FRAGMENT_MANY);
		JOM(4, "%6i=audio_pages_per_fragment\n",
						peasycap->audio_pages_per_fragment);
		JOM(4, "%6i=audio_bytes_per_fragment\n",
						peasycap->audio_bytes_per_fragment);
		JOM(4, "%6i=audio_buffer_page_many\n",
						peasycap->audio_buffer_page_many);

		peasycap->audio_isoc_framesperdesc = AUDIO_ISOC_FRAMESPERDESC;

		JOM(4, "%i=audio_isoc_framesperdesc\n",
						peasycap->audio_isoc_framesperdesc);
		if (0 >= peasycap->audio_isoc_framesperdesc) {
			SAM("ERROR:  bad audio_isoc_framesperdesc\n");
			return -ENOENT;
		}

		peasycap->audio_isoc_buffer_size =
					peasycap->audio_isoc_maxframesize *
					peasycap->audio_isoc_framesperdesc;
		JOM(4, "%i=audio_isoc_buffer_size\n",
						peasycap->audio_isoc_buffer_size);
		if (AUDIO_ISOC_BUFFER_SIZE < peasycap->audio_isoc_buffer_size) {
				SAM("MISTAKE:  audio_isoc_buffer_size bigger "
				"than %li=AUDIO_ISOC_BUFFER_SIZE\n",
							AUDIO_ISOC_BUFFER_SIZE);
			return -EFAULT;
		}
		if (-1 == peasycap->audio_interface) {
			SAM("MISTAKE:  audio_interface is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->audio_altsetting_on) {
			SAM("MISTAKE:  audio_altsetting_on is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->audio_altsetting_off) {
			SAM("MISTAKE:  audio_interface_off is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->audio_endpointnumber) {
			SAM("MISTAKE:  audio_endpointnumber is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->audio_isoc_maxframesize) {
			SAM("MISTAKE:  audio_isoc_maxframesize is unset\n");
			return -EFAULT;
		}
		if (-1 == peasycap->audio_isoc_buffer_size) {
			SAM("MISTAKE:  audio_isoc_buffer_size is unset\n");
			return -EFAULT;
		}

		/*
		 * Allocate memory for audio buffers.
		 * Lists must be initialized first.
		 */
		INIT_LIST_HEAD(&(peasycap->urb_audio_head));
		peasycap->purb_audio_head = &(peasycap->urb_audio_head);

		alloc_audio_buffers(peasycap);
		if (rc < 0)
			return rc;

		/* Allocate and initialize urbs */
		rc = create_audio_urbs(peasycap);
		if (rc < 0)
			return rc;

		/* Save pointer peasycap in this interface */
		usb_set_intfdata(intf, peasycap);

		/* The audio device can now be registered */
		JOM(4, "initializing ALSA card\n");

		rc = easycap_alsa_probe(peasycap);
		if (rc) {
			err("easycap_alsa_probe() rc = %i\n", rc);
			return -ENODEV;
		}


		JOM(8, "kref_get() with %i=kref.refcount.counter\n",
				peasycap->kref.refcount.counter);
		kref_get(&peasycap->kref);
		peasycap->registered_audio++;
		break;
	}
	/* Interfaces other than 0,1,2 are unexpected */
	default:
		JOM(4, "ERROR: unexpected interface %i\n", bInterfaceNumber);
		return -EINVAL;
	}
	SAM("ends successfully for interface %i\n", bInterfaceNumber);
	return 0;
}

/*
 * When this function is called the device has already been
 * physically unplugged.
 * Hence, peasycap->pusb_device is no longer valid.
 * This function affects alsa.
 */
static void easycap_usb_disconnect(struct usb_interface *pusb_interface)
{
	struct usb_host_interface *pusb_host_interface;
	struct usb_interface_descriptor *pusb_interface_descriptor;
	struct easycap *peasycap;
	int minor, kd;
	u8 bInterfaceNumber;

	JOT(4, "\n");

	pusb_host_interface = pusb_interface->cur_altsetting;
	if (!pusb_host_interface) {
		JOT(4, "ERROR: pusb_host_interface is NULL\n");
		return;
	}
	pusb_interface_descriptor = &(pusb_host_interface->desc);
	if (!pusb_interface_descriptor) {
		JOT(4, "ERROR: pusb_interface_descriptor is NULL\n");
		return;
	}
	bInterfaceNumber = pusb_interface_descriptor->bInterfaceNumber;
	minor = pusb_interface->minor;
	JOT(4, "intf[%i]: minor=%i\n", bInterfaceNumber, minor);

	/* There is nothing to do for Interface Number 1 */
	if (1 == bInterfaceNumber)
		return;

	peasycap = usb_get_intfdata(pusb_interface);
	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return;
	}

	/* If the waitqueues are not cleared a deadlock is possible */
	peasycap->video_eof = 1;
	peasycap->audio_eof = 1;
	wake_up_interruptible(&(peasycap->wq_video));
	wake_up_interruptible(&(peasycap->wq_audio));

	switch (bInterfaceNumber) {
	case 0:
		easycap_video_kill_urbs(peasycap);
		break;
	case 2:
		easycap_audio_kill_urbs(peasycap);
		break;
	default:
		break;
	}

	/*
	 * Deregister
	 * This procedure will block until easycap_poll(),
	 * video and audio ioctl are all unlocked.
	 * If this is not done an oops can occur when an easycap
	 * is unplugged while the urbs are running.
	 */
	kd = easycap_isdongle(peasycap);
	switch (bInterfaceNumber) {
	case 0: {
		if (0 <= kd && DONGLE_MANY > kd) {
			wake_up_interruptible(&peasycap->wq_video);
			JOM(4, "about to lock dongle[%i].mutex_video\n", kd);
			if (mutex_lock_interruptible(&easycapdc60_dongle[kd].
								mutex_video)) {
				SAY("ERROR: "
				    "cannot lock dongle[%i].mutex_video\n", kd);
				return;
			}
			JOM(4, "locked dongle[%i].mutex_video\n", kd);
		} else {
			SAY("ERROR: %i=kd is bad: cannot lock dongle\n", kd);
		}
		if (!peasycap->v4l2_device.name[0]) {
			SAM("ERROR: peasycap->v4l2_device.name is empty\n");
			if (0 <= kd && DONGLE_MANY > kd)
				mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return;
		}
		v4l2_device_disconnect(&peasycap->v4l2_device);
		JOM(4, "v4l2_device_disconnect() OK\n");
		v4l2_device_unregister(&peasycap->v4l2_device);
		JOM(4, "v4l2_device_unregister() OK\n");

		video_unregister_device(&peasycap->video_device);
		JOM(4, "intf[%i]: video_unregister_device() minor=%i\n",
				bInterfaceNumber, minor);
		peasycap->registered_video--;

		if (0 <= kd && DONGLE_MANY > kd) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			JOM(4, "unlocked dongle[%i].mutex_video\n", kd);
		}
		break;
	}
	case 2: {
		if (0 <= kd && DONGLE_MANY > kd) {
			wake_up_interruptible(&peasycap->wq_audio);
			JOM(4, "about to lock dongle[%i].mutex_audio\n", kd);
			if (mutex_lock_interruptible(&easycapdc60_dongle[kd].
								mutex_audio)) {
				SAY("ERROR: "
				    "cannot lock dongle[%i].mutex_audio\n", kd);
				return;
			}
			JOM(4, "locked dongle[%i].mutex_audio\n", kd);
		} else
			SAY("ERROR: %i=kd is bad: cannot lock dongle\n", kd);
		if (0 != snd_card_free(peasycap->psnd_card)) {
			SAY("ERROR: snd_card_free() failed\n");
		} else {
			peasycap->psnd_card = NULL;
			(peasycap->registered_audio)--;
		}
		if (0 <= kd && DONGLE_MANY > kd) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			JOM(4, "unlocked dongle[%i].mutex_audio\n", kd);
		}
		break;
	}
	default:
		break;
	}

	/*
	 * If no remaining references to peasycap,
	 * call easycap_delete.
	 * (Also when alsa has been in use)
	 */
	if (!peasycap->kref.refcount.counter) {
		SAM("ERROR: peasycap->kref.refcount.counter is zero "
							"so cannot call kref_put()\n");
		SAM("ending unsuccessfully: may cause memory leak\n");
		return;
	}
	if (0 <= kd && DONGLE_MANY > kd) {
		JOM(4, "about to lock dongle[%i].mutex_video\n", kd);
		if (mutex_lock_interruptible(&easycapdc60_dongle[kd].mutex_video)) {
			SAY("ERROR: cannot lock dongle[%i].mutex_video\n", kd);
			SAM("ending unsuccessfully: may cause memory leak\n");
			return;
		}
		JOM(4, "locked dongle[%i].mutex_video\n", kd);
		JOM(4, "about to lock dongle[%i].mutex_audio\n", kd);
		if (mutex_lock_interruptible(&easycapdc60_dongle[kd].mutex_audio)) {
			SAY("ERROR: cannot lock dongle[%i].mutex_audio\n", kd);
			mutex_unlock(&(easycapdc60_dongle[kd].mutex_video));
			JOM(4, "unlocked dongle[%i].mutex_video\n", kd);
			SAM("ending unsuccessfully: may cause memory leak\n");
			return;
		}
		JOM(4, "locked dongle[%i].mutex_audio\n", kd);
	}
	JOM(4, "intf[%i]: %i=peasycap->kref.refcount.counter\n",
			bInterfaceNumber, (int)peasycap->kref.refcount.counter);
	kref_put(&peasycap->kref, easycap_delete);
	JOT(4, "intf[%i]: kref_put() done.\n", bInterfaceNumber);
	if (0 <= kd && DONGLE_MANY > kd) {
		mutex_unlock(&(easycapdc60_dongle[kd].mutex_audio));
		JOT(4, "unlocked dongle[%i].mutex_audio\n", kd);
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		JOT(4, "unlocked dongle[%i].mutex_video\n", kd);
	}
	JOM(4, "ends\n");
	return;
}

/* Devices supported by this driver */
static struct usb_device_id easycap_usb_device_id_table[] = {
	{USB_DEVICE(USB_EASYCAP_VENDOR_ID, USB_EASYCAP_PRODUCT_ID)},
	{ }
};

MODULE_DEVICE_TABLE(usb, easycap_usb_device_id_table);
static struct usb_driver easycap_usb_driver = {
	.name = "easycap",
	.id_table = easycap_usb_device_id_table,
	.probe = easycap_usb_probe,
	.disconnect = easycap_usb_disconnect,
};

static int __init easycap_module_init(void)
{
	int k, rc;

	printk(KERN_INFO "Easycap version: "EASYCAP_DRIVER_VERSION "\n");

	JOT(4, "begins.  %i=debug %i=bars %i=gain\n",
		easycap_debug, easycap_bars, easycap_gain);

	mutex_init(&mutex_dongle);
	for (k = 0; k < DONGLE_MANY; k++) {
		easycapdc60_dongle[k].peasycap = NULL;
		mutex_init(&easycapdc60_dongle[k].mutex_video);
		mutex_init(&easycapdc60_dongle[k].mutex_audio);
	}
	rc = usb_register(&easycap_usb_driver);
	if (rc)
		printk(KERN_ERR "Easycap: usb_register failed rc=%d\n", rc);

	return rc;
}

static void __exit easycap_module_exit(void)
{
	usb_deregister(&easycap_usb_driver);
}

module_init(easycap_module_init);
module_exit(easycap_module_exit);
