/*
 *    QuickCam Driver For Video4Linux.
 *
 *	Video4Linux conversion work by Alan Cox.
 *	Parport compatibility by Phil Blundell.
 *	Busy loop avoidance by Mark Cooke.
 *
 *    Module parameters:
 *
 *	maxpoll=<1 - 5000>
 *
 *	  When polling the QuickCam for a response, busy-wait for a
 *	  maximum of this many loops. The default of 250 gives little
 *	  impact on interactive response.
 *
 *	  NOTE: If this parameter is set too high, the processor
 *		will busy wait until this loop times out, and then
 *		slowly poll for a further 5 seconds before failing
 *		the transaction. You have been warned.
 *
 *	yieldlines=<1 - 250>
 *
 *	  When acquiring a frame from the camera, the data gathering
 *	  loop will yield back to the scheduler after completing
 *	  this many lines. The default of 4 provides a trade-off
 *	  between increased frame acquisition time and impact on
 *	  interactive response.
 */

/* qcam-lib.c -- Library for programming with the Connectix QuickCam.
 * See the included documentation for usage instructions and details
 * of the protocol involved. */


/* Version 0.5, August 4, 1996 */
/* Version 0.7, August 27, 1996 */
/* Version 0.9, November 17, 1996 */


/******************************************************************

Copyright (C) 1996 by Scott Laird

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL SCOTT LAIRD BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/parport.h>
#include <linux/sched.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#include "bw-qcam.h"

static unsigned int maxpoll=250;   /* Maximum busy-loop count for qcam I/O */
static unsigned int yieldlines=4;  /* Yield after this many during capture */
static int video_nr = -1;
static unsigned int force_init;		/* Whether to probe aggressively */

module_param(maxpoll, int, 0);
module_param(yieldlines, int, 0);
module_param(video_nr, int, 0);

/* Set force_init=1 to avoid detection by polling status register and
 * immediately attempt to initialize qcam */
module_param(force_init, int, 0);

static inline int read_lpstatus(struct qcam_device *q)
{
	return parport_read_status(q->pport);
}

static inline int read_lpdata(struct qcam_device *q)
{
	return parport_read_data(q->pport);
}

static inline void write_lpdata(struct qcam_device *q, int d)
{
	parport_write_data(q->pport, d);
}

static inline void write_lpcontrol(struct qcam_device *q, int d)
{
	if (d & 0x20) {
		/* Set bidirectional mode to reverse (data in) */
		parport_data_reverse(q->pport);
	} else {
		/* Set bidirectional mode to forward (data out) */
		parport_data_forward(q->pport);
	}

	/* Now issue the regular port command, but strip out the
	 * direction flag */
	d &= ~0x20;
	parport_write_control(q->pport, d);
}

static int qc_waithand(struct qcam_device *q, int val);
static int qc_command(struct qcam_device *q, int command);
static int qc_readparam(struct qcam_device *q);
static int qc_setscanmode(struct qcam_device *q);
static int qc_readbytes(struct qcam_device *q, char buffer[]);

static struct video_device qcam_template;

static int qc_calibrate(struct qcam_device *q)
{
	/*
	 *	Bugfix by Hanno Mueller hmueller@kabel.de, Mai 21 96
	 *	The white balance is an individiual value for each
	 *	quickcam.
	 */

	int value;
	int count = 0;

	qc_command(q, 27);	/* AutoAdjustOffset */
	qc_command(q, 0);	/* Dummy Parameter, ignored by the camera */

	/* GetOffset (33) will read 255 until autocalibration */
	/* is finished. After that, a value of 1-254 will be */
	/* returned. */

	do {
		qc_command(q, 33);
		value = qc_readparam(q);
		mdelay(1);
		schedule();
		count++;
	} while (value == 0xff && count<2048);

	q->whitebal = value;
	return value;
}

/* Initialize the QuickCam driver control structure.  This is where
 * defaults are set for people who don't have a config file.*/

static struct qcam_device *qcam_init(struct parport *port)
{
	struct qcam_device *q;

	q = kmalloc(sizeof(struct qcam_device), GFP_KERNEL);
	if(q==NULL)
		return NULL;

	q->pport = port;
	q->pdev = parport_register_device(port, "bw-qcam", NULL, NULL,
					  NULL, 0, NULL);
	if (q->pdev == NULL)
	{
		printk(KERN_ERR "bw-qcam: couldn't register for %s.\n",
		       port->name);
		kfree(q);
		return NULL;
	}

	memcpy(&q->vdev, &qcam_template, sizeof(qcam_template));

	mutex_init(&q->lock);

	q->port_mode = (QC_ANY | QC_NOTSET);
	q->width = 320;
	q->height = 240;
	q->bpp = 4;
	q->transfer_scale = 2;
	q->contrast = 192;
	q->brightness = 180;
	q->whitebal = 105;
	q->top = 1;
	q->left = 14;
	q->mode = -1;
	q->status = QC_PARAM_CHANGE;
	return q;
}


/* qc_command is probably a bit of a misnomer -- it's used to send
 * bytes *to* the camera.  Generally, these bytes are either commands
 * or arguments to commands, so the name fits, but it still bugs me a
 * bit.  See the documentation for a list of commands. */

static int qc_command(struct qcam_device *q, int command)
{
	int n1, n2;
	int cmd;

	write_lpdata(q, command);
	write_lpcontrol(q, 6);

	n1 = qc_waithand(q, 1);

	write_lpcontrol(q, 0xe);
	n2 = qc_waithand(q, 0);

	cmd = (n1 & 0xf0) | ((n2 & 0xf0) >> 4);
	return cmd;
}

static int qc_readparam(struct qcam_device *q)
{
	int n1, n2;
	int cmd;

	write_lpcontrol(q, 6);
	n1 = qc_waithand(q, 1);

	write_lpcontrol(q, 0xe);
	n2 = qc_waithand(q, 0);

	cmd = (n1 & 0xf0) | ((n2 & 0xf0) >> 4);
	return cmd;
}

/* qc_waithand busy-waits for a handshake signal from the QuickCam.
 * Almost all communication with the camera requires handshaking. */

static int qc_waithand(struct qcam_device *q, int val)
{
	int status;
	int runs=0;

	if (val)
	{
		while (!((status = read_lpstatus(q)) & 8))
		{
			/* 1000 is enough spins on the I/O for all normal
			   cases, at that point we start to poll slowly
			   until the camera wakes up. However, we are
			   busy blocked until the camera responds, so
			   setting it lower is much better for interactive
			   response. */

			if(runs++>maxpoll)
			{
				msleep_interruptible(5);
			}
			if(runs>(maxpoll+1000)) /* 5 seconds */
				return -1;
		}
	}
	else
	{
		while (((status = read_lpstatus(q)) & 8))
		{
			/* 1000 is enough spins on the I/O for all normal
			   cases, at that point we start to poll slowly
			   until the camera wakes up. However, we are
			   busy blocked until the camera responds, so
			   setting it lower is much better for interactive
			   response. */

			if(runs++>maxpoll)
			{
				msleep_interruptible(5);
			}
			if(runs++>(maxpoll+1000)) /* 5 seconds */
				return -1;
		}
	}

	return status;
}

/* Waithand2 is used when the qcam is in bidirectional mode, and the
 * handshaking signal is CamRdy2 (bit 0 of data reg) instead of CamRdy1
 * (bit 3 of status register).  It also returns the last value read,
 * since this data is useful. */

static unsigned int qc_waithand2(struct qcam_device *q, int val)
{
	unsigned int status;
	int runs=0;

	do
	{
		status = read_lpdata(q);
		/* 1000 is enough spins on the I/O for all normal
		   cases, at that point we start to poll slowly
		   until the camera wakes up. However, we are
		   busy blocked until the camera responds, so
		   setting it lower is much better for interactive
		   response. */

		if(runs++>maxpoll)
		{
			msleep_interruptible(5);
		}
		if(runs++>(maxpoll+1000)) /* 5 seconds */
			return 0;
	}
	while ((status & 1) != val);

	return status;
}


/* Try to detect a QuickCam.  It appears to flash the upper 4 bits of
   the status register at 5-10 Hz.  This is only used in the autoprobe
   code.  Be aware that this isn't the way Connectix detects the
   camera (they send a reset and try to handshake), but this should be
   almost completely safe, while their method screws up my printer if
   I plug it in before the camera. */

static int qc_detect(struct qcam_device *q)
{
	int reg, lastreg;
	int count = 0;
	int i;

	if (force_init)
		return 1;

	lastreg = reg = read_lpstatus(q) & 0xf0;

	for (i = 0; i < 500; i++)
	{
		reg = read_lpstatus(q) & 0xf0;
		if (reg != lastreg)
			count++;
		lastreg = reg;
		mdelay(2);
	}


#if 0
	/* Force camera detection during testing. Sometimes the camera
	   won't be flashing these bits. Possibly unloading the module
	   in the middle of a grab? Or some timeout condition?
	   I've seen this parameter as low as 19 on my 450Mhz box - mpc */
	printk("Debugging: QCam detection counter <30-200 counts as detected>: %d\n", count);
	return 1;
#endif

	/* Be (even more) liberal in what you accept...  */

	if (count > 20 && count < 400) {
		return 1;	/* found */
	} else {
		printk(KERN_ERR "No Quickcam found on port %s\n",
			q->pport->name);
		printk(KERN_DEBUG "Quickcam detection counter: %u\n", count);
		return 0;	/* not found */
	}
}


/* Reset the QuickCam.  This uses the same sequence the Windows
 * QuickPic program uses.  Someone with a bi-directional port should
 * check that bi-directional mode is detected right, and then
 * implement bi-directional mode in qc_readbyte(). */

static void qc_reset(struct qcam_device *q)
{
	switch (q->port_mode & QC_FORCE_MASK)
	{
		case QC_FORCE_UNIDIR:
			q->port_mode = (q->port_mode & ~QC_MODE_MASK) | QC_UNIDIR;
			break;

		case QC_FORCE_BIDIR:
			q->port_mode = (q->port_mode & ~QC_MODE_MASK) | QC_BIDIR;
			break;

		case QC_ANY:
			write_lpcontrol(q, 0x20);
			write_lpdata(q, 0x75);

			if (read_lpdata(q) != 0x75) {
				q->port_mode = (q->port_mode & ~QC_MODE_MASK) | QC_BIDIR;
			} else {
				q->port_mode = (q->port_mode & ~QC_MODE_MASK) | QC_UNIDIR;
			}
			break;
	}

	write_lpcontrol(q, 0xb);
	udelay(250);
	write_lpcontrol(q, 0xe);
	qc_setscanmode(q);		/* in case port_mode changed */
}


/* Decide which scan mode to use.  There's no real requirement that
 * the scanmode match the resolution in q->height and q-> width -- the
 * camera takes the picture at the resolution specified in the
 * "scanmode" and then returns the image at the resolution specified
 * with the resolution commands.  If the scan is bigger than the
 * requested resolution, the upper-left hand corner of the scan is
 * returned.  If the scan is smaller, then the rest of the image
 * returned contains garbage. */

static int qc_setscanmode(struct qcam_device *q)
{
	int old_mode = q->mode;

	switch (q->transfer_scale)
	{
		case 1:
			q->mode = 0;
			break;
		case 2:
			q->mode = 4;
			break;
		case 4:
			q->mode = 8;
			break;
	}

	switch (q->bpp)
	{
		case 4:
			break;
		case 6:
			q->mode += 2;
			break;
	}

	switch (q->port_mode & QC_MODE_MASK)
	{
		case QC_BIDIR:
			q->mode += 1;
			break;
		case QC_NOTSET:
		case QC_UNIDIR:
			break;
	}

	if (q->mode != old_mode)
		q->status |= QC_PARAM_CHANGE;

	return 0;
}


/* Reset the QuickCam and program for brightness, contrast,
 * white-balance, and resolution. */

static void qc_set(struct qcam_device *q)
{
	int val;
	int val2;

	qc_reset(q);

	/* Set the brightness.  Yes, this is repetitive, but it works.
	 * Shorter versions seem to fail subtly.  Feel free to try :-). */
	/* I think the problem was in qc_command, not here -- bls */

	qc_command(q, 0xb);
	qc_command(q, q->brightness);

	val = q->height / q->transfer_scale;
	qc_command(q, 0x11);
	qc_command(q, val);
	if ((q->port_mode & QC_MODE_MASK) == QC_UNIDIR && q->bpp == 6) {
		/* The normal "transfers per line" calculation doesn't seem to work
		   as expected here (and yet it works fine in qc_scan).  No idea
		   why this case is the odd man out.  Fortunately, Laird's original
		   working version gives me a good way to guess at working values.
		   -- bls */
		val = q->width;
		val2 = q->transfer_scale * 4;
	} else {
		val = q->width * q->bpp;
		val2 = (((q->port_mode & QC_MODE_MASK) == QC_BIDIR) ? 24 : 8) *
		    q->transfer_scale;
	}
	val = (val + val2 - 1) / val2;
	qc_command(q, 0x13);
	qc_command(q, val);

	/* Setting top and left -- bls */
	qc_command(q, 0xd);
	qc_command(q, q->top);
	qc_command(q, 0xf);
	qc_command(q, q->left / 2);

	qc_command(q, 0x19);
	qc_command(q, q->contrast);
	qc_command(q, 0x1f);
	qc_command(q, q->whitebal);

	/* Clear flag that we must update the grabbing parameters on the camera
	   before we grab the next frame */
	q->status &= (~QC_PARAM_CHANGE);
}

/* Qc_readbytes reads some bytes from the QC and puts them in
   the supplied buffer.  It returns the number of bytes read,
   or -1 on error. */

static inline int qc_readbytes(struct qcam_device *q, char buffer[])
{
	int ret=1;
	unsigned int hi, lo;
	unsigned int hi2, lo2;
	static int state = 0;

	if (buffer == NULL)
	{
		state = 0;
		return 0;
	}

	switch (q->port_mode & QC_MODE_MASK)
	{
		case QC_BIDIR:		/* Bi-directional Port */
			write_lpcontrol(q, 0x26);
			lo = (qc_waithand2(q, 1) >> 1);
			hi = (read_lpstatus(q) >> 3) & 0x1f;
			write_lpcontrol(q, 0x2e);
			lo2 = (qc_waithand2(q, 0) >> 1);
			hi2 = (read_lpstatus(q) >> 3) & 0x1f;
			switch (q->bpp)
			{
				case 4:
					buffer[0] = lo & 0xf;
					buffer[1] = ((lo & 0x70) >> 4) | ((hi & 1) << 3);
					buffer[2] = (hi & 0x1e) >> 1;
					buffer[3] = lo2 & 0xf;
					buffer[4] = ((lo2 & 0x70) >> 4) | ((hi2 & 1) << 3);
					buffer[5] = (hi2 & 0x1e) >> 1;
					ret = 6;
					break;
				case 6:
					buffer[0] = lo & 0x3f;
					buffer[1] = ((lo & 0x40) >> 6) | (hi << 1);
					buffer[2] = lo2 & 0x3f;
					buffer[3] = ((lo2 & 0x40) >> 6) | (hi2 << 1);
					ret = 4;
					break;
			}
			break;

		case QC_UNIDIR:	/* Unidirectional Port */
			write_lpcontrol(q, 6);
			lo = (qc_waithand(q, 1) & 0xf0) >> 4;
			write_lpcontrol(q, 0xe);
			hi = (qc_waithand(q, 0) & 0xf0) >> 4;

			switch (q->bpp)
			{
				case 4:
					buffer[0] = lo;
					buffer[1] = hi;
					ret = 2;
					break;
				case 6:
					switch (state)
					{
						case 0:
							buffer[0] = (lo << 2) | ((hi & 0xc) >> 2);
							q->saved_bits = (hi & 3) << 4;
							state = 1;
							ret = 1;
							break;
						case 1:
							buffer[0] = lo | q->saved_bits;
							q->saved_bits = hi << 2;
							state = 2;
							ret = 1;
							break;
						case 2:
							buffer[0] = ((lo & 0xc) >> 2) | q->saved_bits;
							buffer[1] = ((lo & 3) << 4) | hi;
							state = 0;
							ret = 2;
							break;
					}
					break;
			}
			break;
	}
	return ret;
}

/* requests a scan from the camera.  It sends the correct instructions
 * to the camera and then reads back the correct number of bytes.  In
 * previous versions of this routine the return structure contained
 * the raw output from the camera, and there was a 'qc_convertscan'
 * function that converted that to a useful format.  In version 0.3 I
 * rolled qc_convertscan into qc_scan and now I only return the
 * converted scan.  The format is just an one-dimensional array of
 * characters, one for each pixel, with 0=black up to n=white, where
 * n=2^(bit depth)-1.  Ask me for more details if you don't understand
 * this. */

static long qc_capture(struct qcam_device * q, char __user *buf, unsigned long len)
{
	int i, j, k, yield;
	int bytes;
	int linestotrans, transperline;
	int divisor;
	int pixels_per_line;
	int pixels_read = 0;
	int got=0;
	char buffer[6];
	int  shift=8-q->bpp;
	char invert;

	if (q->mode == -1)
		return -ENXIO;

	qc_command(q, 0x7);
	qc_command(q, q->mode);

	if ((q->port_mode & QC_MODE_MASK) == QC_BIDIR)
	{
		write_lpcontrol(q, 0x2e);	/* turn port around */
		write_lpcontrol(q, 0x26);
		(void) qc_waithand(q, 1);
		write_lpcontrol(q, 0x2e);
		(void) qc_waithand(q, 0);
	}

	/* strange -- should be 15:63 below, but 4bpp is odd */
	invert = (q->bpp == 4) ? 16 : 63;

	linestotrans = q->height / q->transfer_scale;
	pixels_per_line = q->width / q->transfer_scale;
	transperline = q->width * q->bpp;
	divisor = (((q->port_mode & QC_MODE_MASK) == QC_BIDIR) ? 24 : 8) *
	    q->transfer_scale;
	transperline = (transperline + divisor - 1) / divisor;

	for (i = 0, yield = yieldlines; i < linestotrans; i++)
	{
		for (pixels_read = j = 0; j < transperline; j++)
		{
			bytes = qc_readbytes(q, buffer);
			for (k = 0; k < bytes && (pixels_read + k) < pixels_per_line; k++)
			{
				int o;
				if (buffer[k] == 0 && invert == 16)
				{
					/* 4bpp is odd (again) -- inverter is 16, not 15, but output
					   must be 0-15 -- bls */
					buffer[k] = 16;
				}
				o=i*pixels_per_line + pixels_read + k;
				if(o<len)
				{
					got++;
					put_user((invert - buffer[k])<<shift, buf+o);
				}
			}
			pixels_read += bytes;
		}
		(void) qc_readbytes(q, NULL);	/* reset state machine */

		/* Grabbing an entire frame from the quickcam is a lengthy
		   process. We don't (usually) want to busy-block the
		   processor for the entire frame. yieldlines is a module
		   parameter. If we yield every line, the minimum frame
		   time will be 240 / 200 = 1.2 seconds. The compile-time
		   default is to yield every 4 lines. */
		if (i >= yield) {
			msleep_interruptible(5);
			yield = i + yieldlines;
		}
	}

	if ((q->port_mode & QC_MODE_MASK) == QC_BIDIR)
	{
		write_lpcontrol(q, 2);
		write_lpcontrol(q, 6);
		udelay(3);
		write_lpcontrol(q, 0xe);
	}
	if(got<len)
		return got;
	return len;
}

/*
 *	Video4linux interfacing
 */

static int qcam_do_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct qcam_device *qcam=(struct qcam_device *)dev;

	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability *b = arg;
			strcpy(b->name, "Quickcam");
			b->type = VID_TYPE_CAPTURE|VID_TYPE_SCALES|VID_TYPE_MONOCHROME;
			b->channels = 1;
			b->audios = 0;
			b->maxwidth = 320;
			b->maxheight = 240;
			b->minwidth = 80;
			b->minheight = 60;
			return 0;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel *v = arg;
			if(v->channel!=0)
				return -EINVAL;
			v->flags=0;
			v->tuners=0;
			/* Good question.. its composite or SVHS so.. */
			v->type = VIDEO_TYPE_CAMERA;
			strcpy(v->name, "Camera");
			return 0;
		}
		case VIDIOCSCHAN:
		{
			struct video_channel *v = arg;
			if(v->channel!=0)
				return -EINVAL;
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner)
				return -EINVAL;
			strcpy(v->name, "Format");
			v->rangelow=0;
			v->rangehigh=0;
			v->flags= 0;
			v->mode = VIDEO_MODE_AUTO;
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner)
				return -EINVAL;
			if(v->mode!=VIDEO_MODE_AUTO)
				return -EINVAL;
			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture *p = arg;
			p->colour=0x8000;
			p->hue=0x8000;
			p->brightness=qcam->brightness<<8;
			p->contrast=qcam->contrast<<8;
			p->whiteness=qcam->whitebal<<8;
			p->depth=qcam->bpp;
			p->palette=VIDEO_PALETTE_GREY;
			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture *p = arg;
			if(p->palette!=VIDEO_PALETTE_GREY)
				return -EINVAL;
			if(p->depth!=4 && p->depth!=6)
				return -EINVAL;

			/*
			 *	Now load the camera.
			 */

			qcam->brightness = p->brightness>>8;
			qcam->contrast = p->contrast>>8;
			qcam->whitebal = p->whiteness>>8;
			qcam->bpp = p->depth;

			mutex_lock(&qcam->lock);
			qc_setscanmode(qcam);
			mutex_unlock(&qcam->lock);
			qcam->status |= QC_PARAM_CHANGE;

			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window *vw = arg;
			if(vw->flags)
				return -EINVAL;
			if(vw->clipcount)
				return -EINVAL;
			if(vw->height<60||vw->height>240)
				return -EINVAL;
			if(vw->width<80||vw->width>320)
				return -EINVAL;

			qcam->width = 320;
			qcam->height = 240;
			qcam->transfer_scale = 4;

			if(vw->width>=160 && vw->height>=120)
			{
				qcam->transfer_scale = 2;
			}
			if(vw->width>=320 && vw->height>=240)
			{
				qcam->width = 320;
				qcam->height = 240;
				qcam->transfer_scale = 1;
			}
			mutex_lock(&qcam->lock);
			qc_setscanmode(qcam);
			mutex_unlock(&qcam->lock);

			/* We must update the camera before we grab. We could
			   just have changed the grab size */
			qcam->status |= QC_PARAM_CHANGE;

			/* Ok we figured out what to use from our wide choice */
			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window *vw = arg;
			memset(vw, 0, sizeof(*vw));
			vw->width=qcam->width/qcam->transfer_scale;
			vw->height=qcam->height/qcam->transfer_scale;
			return 0;
		}
		case VIDIOCKEY:
			return 0;
		case VIDIOCCAPTURE:
		case VIDIOCGFBUF:
		case VIDIOCSFBUF:
		case VIDIOCGFREQ:
		case VIDIOCSFREQ:
		case VIDIOCGAUDIO:
		case VIDIOCSAUDIO:
			return -EINVAL;
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static int qcam_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, qcam_do_ioctl);
}

static ssize_t qcam_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct video_device *v = video_devdata(file);
	struct qcam_device *qcam=(struct qcam_device *)v;
	int len;
	parport_claim_or_block(qcam->pdev);

	mutex_lock(&qcam->lock);

	qc_reset(qcam);

	/* Update the camera parameters if we need to */
	if (qcam->status & QC_PARAM_CHANGE)
		qc_set(qcam);

	len=qc_capture(qcam, buf,count);

	mutex_unlock(&qcam->lock);

	parport_release(qcam->pdev);
	return len;
}

static const struct file_operations qcam_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl          = qcam_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.read		= qcam_read,
	.llseek         = no_llseek,
};
static struct video_device qcam_template=
{
	.owner		= THIS_MODULE,
	.name		= "Connectix Quickcam",
	.type		= VID_TYPE_CAPTURE,
	.fops           = &qcam_fops,
};

#define MAX_CAMS 4
static struct qcam_device *qcams[MAX_CAMS];
static unsigned int num_cams = 0;

static int init_bwqcam(struct parport *port)
{
	struct qcam_device *qcam;

	if (num_cams == MAX_CAMS)
	{
		printk(KERN_ERR "Too many Quickcams (max %d)\n", MAX_CAMS);
		return -ENOSPC;
	}

	qcam=qcam_init(port);
	if(qcam==NULL)
		return -ENODEV;

	parport_claim_or_block(qcam->pdev);

	qc_reset(qcam);

	if(qc_detect(qcam)==0)
	{
		parport_release(qcam->pdev);
		parport_unregister_device(qcam->pdev);
		kfree(qcam);
		return -ENODEV;
	}
	qc_calibrate(qcam);

	parport_release(qcam->pdev);

	printk(KERN_INFO "Connectix Quickcam on %s\n", qcam->pport->name);

	if(video_register_device(&qcam->vdev, VFL_TYPE_GRABBER, video_nr)==-1)
	{
		parport_unregister_device(qcam->pdev);
		kfree(qcam);
		return -ENODEV;
	}

	qcams[num_cams++] = qcam;

	return 0;
}

static void close_bwqcam(struct qcam_device *qcam)
{
	video_unregister_device(&qcam->vdev);
	parport_unregister_device(qcam->pdev);
	kfree(qcam);
}

/* The parport parameter controls which parports will be scanned.
 * Scanning all parports causes some printers to print a garbage page.
 *       -- March 14, 1999  Billy Donahue <billy@escape.com> */
#ifdef MODULE
static char *parport[MAX_CAMS] = { NULL, };
module_param_array(parport, charp, NULL, 0);
#endif

static int accept_bwqcam(struct parport *port)
{
#ifdef MODULE
	int n;

	if (parport[0] && strncmp(parport[0], "auto", 4) != 0) {
		/* user gave parport parameters */
		for(n=0; parport[n] && n<MAX_CAMS; n++){
			char *ep;
			unsigned long r;
			r = simple_strtoul(parport[n], &ep, 0);
			if (ep == parport[n]) {
				printk(KERN_ERR
					"bw-qcam: bad port specifier \"%s\"\n",
					parport[n]);
				continue;
			}
			if (r == port->number)
				return 1;
		}
		return 0;
	}
#endif
	return 1;
}

static void bwqcam_attach(struct parport *port)
{
	if (accept_bwqcam(port))
		init_bwqcam(port);
}

static void bwqcam_detach(struct parport *port)
{
	int i;
	for (i = 0; i < num_cams; i++) {
		struct qcam_device *qcam = qcams[i];
		if (qcam && qcam->pdev->port == port) {
			qcams[i] = NULL;
			close_bwqcam(qcam);
		}
	}
}

static struct parport_driver bwqcam_driver = {
	.name	= "bw-qcam",
	.attach	= bwqcam_attach,
	.detach	= bwqcam_detach,
};

static void __exit exit_bw_qcams(void)
{
	parport_unregister_driver(&bwqcam_driver);
}

static int __init init_bw_qcams(void)
{
#ifdef MODULE
	/* Do some sanity checks on the module parameters. */
	if (maxpoll > 5000) {
		printk("Connectix Quickcam max-poll was above 5000. Using 5000.\n");
		maxpoll = 5000;
	}

	if (yieldlines < 1) {
		printk("Connectix Quickcam yieldlines was less than 1. Using 1.\n");
		yieldlines = 1;
	}
#endif
	return parport_register_driver(&bwqcam_driver);
}

module_init(init_bw_qcams);
module_exit(exit_bw_qcams);

MODULE_LICENSE("GPL");
