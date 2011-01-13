/*
 *	Video4Linux Colour QuickCam driver
 *	Copyright 1997-2000 Philip Blundell <philb@gnu.org>
 *
 *    Module parameters:
 *
 *	parport=auto      -- probe all parports (default)
 *	parport=0         -- parport0 becomes qcam1
 *	parport=2,0,1     -- parports 2,0,1 are tried in that order
 *
 *	probe=0		  -- do no probing, assume camera is present
 *	probe=1		  -- use IEEE-1284 autoprobe data only (default)
 *	probe=2		  -- probe aggressively for cameras
 *
 *	force_rgb=1       -- force data format to RGB (default is BGR)
 *
 * The parport parameter controls which parports will be scanned.
 * Scanning all parports causes some printers to print a garbage page.
 *       -- March 14, 1999  Billy Donahue <billy@escape.com>
 *
 * Fixed data format to BGR, added force_rgb parameter. Added missing
 * parport_unregister_driver() on module removal.
 *       -- May 28, 2000  Claudio Matsuoka <claudio@conectiva.com>
 */

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
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <asm/uaccess.h>
#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

struct qcam {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct pardevice *pdev;
	struct parport *pport;
	int width, height;
	int ccd_width, ccd_height;
	int mode;
	int contrast, brightness, whitebal;
	int top, left;
	unsigned int bidirectional;
	struct mutex lock;
};

/* cameras maximum */
#define MAX_CAMS 4

/* The three possible QuickCam modes */
#define QC_MILLIONS	0x18
#define QC_BILLIONS	0x10
#define QC_THOUSANDS	0x08	/* with VIDEC compression (not supported) */

/* The three possible decimations */
#define QC_DECIMATION_1		0
#define QC_DECIMATION_2		2
#define QC_DECIMATION_4		4

#define BANNER "Colour QuickCam for Video4Linux v0.06"

static int parport[MAX_CAMS] = { [1 ... MAX_CAMS-1] = -1 };
static int probe = 2;
static int force_rgb;
static int video_nr = -1;

/* FIXME: parport=auto would never have worked, surely? --RR */
MODULE_PARM_DESC(parport, "parport=<auto|n[,n]...> for port detection method\n"
			  "probe=<0|1|2> for camera detection method\n"
			  "force_rgb=<0|1> for RGB data format (default BGR)");
module_param_array(parport, int, NULL, 0);
module_param(probe, int, 0);
module_param(force_rgb, bool, 0);
module_param(video_nr, int, 0);

static struct qcam *qcams[MAX_CAMS];
static unsigned int num_cams;

static inline void qcam_set_ack(struct qcam *qcam, unsigned int i)
{
	/* note: the QC specs refer to the PCAck pin by voltage, not
	   software level.  PC ports have builtin inverters. */
	parport_frob_control(qcam->pport, 8, i ? 8 : 0);
}

static inline unsigned int qcam_ready1(struct qcam *qcam)
{
	return (parport_read_status(qcam->pport) & 0x8) ? 1 : 0;
}

static inline unsigned int qcam_ready2(struct qcam *qcam)
{
	return (parport_read_data(qcam->pport) & 0x1) ? 1 : 0;
}

static unsigned int qcam_await_ready1(struct qcam *qcam, int value)
{
	struct v4l2_device *v4l2_dev = &qcam->v4l2_dev;
	unsigned long oldjiffies = jiffies;
	unsigned int i;

	for (oldjiffies = jiffies;
	     time_before(jiffies, oldjiffies + msecs_to_jiffies(40));)
		if (qcam_ready1(qcam) == value)
			return 0;

	/* If the camera didn't respond within 1/25 second, poll slowly
	   for a while. */
	for (i = 0; i < 50; i++) {
		if (qcam_ready1(qcam) == value)
			return 0;
		msleep_interruptible(100);
	}

	/* Probably somebody pulled the plug out.  Not much we can do. */
	v4l2_err(v4l2_dev, "ready1 timeout (%d) %x %x\n", value,
	       parport_read_status(qcam->pport),
	       parport_read_control(qcam->pport));
	return 1;
}

static unsigned int qcam_await_ready2(struct qcam *qcam, int value)
{
	struct v4l2_device *v4l2_dev = &qcam->v4l2_dev;
	unsigned long oldjiffies = jiffies;
	unsigned int i;

	for (oldjiffies = jiffies;
	     time_before(jiffies, oldjiffies + msecs_to_jiffies(40));)
		if (qcam_ready2(qcam) == value)
			return 0;

	/* If the camera didn't respond within 1/25 second, poll slowly
	   for a while. */
	for (i = 0; i < 50; i++) {
		if (qcam_ready2(qcam) == value)
			return 0;
		msleep_interruptible(100);
	}

	/* Probably somebody pulled the plug out.  Not much we can do. */
	v4l2_err(v4l2_dev, "ready2 timeout (%d) %x %x %x\n", value,
	       parport_read_status(qcam->pport),
	       parport_read_control(qcam->pport),
	       parport_read_data(qcam->pport));
	return 1;
}

static int qcam_read_data(struct qcam *qcam)
{
	unsigned int idata;

	qcam_set_ack(qcam, 0);
	if (qcam_await_ready1(qcam, 1))
		return -1;
	idata = parport_read_status(qcam->pport) & 0xf0;
	qcam_set_ack(qcam, 1);
	if (qcam_await_ready1(qcam, 0))
		return -1;
	idata |= parport_read_status(qcam->pport) >> 4;
	return idata;
}

static int qcam_write_data(struct qcam *qcam, unsigned int data)
{
	struct v4l2_device *v4l2_dev = &qcam->v4l2_dev;
	unsigned int idata;

	parport_write_data(qcam->pport, data);
	idata = qcam_read_data(qcam);
	if (data != idata) {
		v4l2_warn(v4l2_dev, "sent %x but received %x\n", data,
		       idata);
		return 1;
	}
	return 0;
}

static inline int qcam_set(struct qcam *qcam, unsigned int cmd, unsigned int data)
{
	if (qcam_write_data(qcam, cmd))
		return -1;
	if (qcam_write_data(qcam, data))
		return -1;
	return 0;
}

static inline int qcam_get(struct qcam *qcam, unsigned int cmd)
{
	if (qcam_write_data(qcam, cmd))
		return -1;
	return qcam_read_data(qcam);
}

static int qc_detect(struct qcam *qcam)
{
	unsigned int stat, ostat, i, count = 0;

	/* The probe routine below is not very reliable.  The IEEE-1284
	   probe takes precedence. */
	/* XXX Currently parport provides no way to distinguish between
	   "the IEEE probe was not done" and "the probe was done, but
	   no device was found".  Fix this one day. */
	if (qcam->pport->probe_info[0].class == PARPORT_CLASS_MEDIA
	    && qcam->pport->probe_info[0].model
	    && !strcmp(qcam->pdev->port->probe_info[0].model,
		       "Color QuickCam 2.0")) {
		printk(KERN_DEBUG "QuickCam: Found by IEEE1284 probe.\n");
		return 1;
	}

	if (probe < 2)
		return 0;

	parport_write_control(qcam->pport, 0xc);

	/* look for a heartbeat */
	ostat = stat = parport_read_status(qcam->pport);
	for (i = 0; i < 250; i++) {
		mdelay(1);
		stat = parport_read_status(qcam->pport);
		if (ostat != stat) {
			if (++count >= 3)
				return 1;
			ostat = stat;
		}
	}

	/* Reset the camera and try again */
	parport_write_control(qcam->pport, 0xc);
	parport_write_control(qcam->pport, 0x8);
	mdelay(1);
	parport_write_control(qcam->pport, 0xc);
	mdelay(1);
	count = 0;

	ostat = stat = parport_read_status(qcam->pport);
	for (i = 0; i < 250; i++) {
		mdelay(1);
		stat = parport_read_status(qcam->pport);
		if (ostat != stat) {
			if (++count >= 3)
				return 1;
			ostat = stat;
		}
	}

	/* no (or flatline) camera, give up */
	return 0;
}

static void qc_reset(struct qcam *qcam)
{
	parport_write_control(qcam->pport, 0xc);
	parport_write_control(qcam->pport, 0x8);
	mdelay(1);
	parport_write_control(qcam->pport, 0xc);
	mdelay(1);
}

/* Reset the QuickCam and program for brightness, contrast,
 * white-balance, and resolution. */

static void qc_setup(struct qcam *qcam)
{
	qc_reset(qcam);

	/* Set the brightness. */
	qcam_set(qcam, 11, qcam->brightness);

	/* Set the height and width.  These refer to the actual
	   CCD area *before* applying the selected decimation.  */
	qcam_set(qcam, 17, qcam->ccd_height);
	qcam_set(qcam, 19, qcam->ccd_width / 2);

	/* Set top and left.  */
	qcam_set(qcam, 0xd, qcam->top);
	qcam_set(qcam, 0xf, qcam->left);

	/* Set contrast and white balance.  */
	qcam_set(qcam, 0x19, qcam->contrast);
	qcam_set(qcam, 0x1f, qcam->whitebal);

	/* Set the speed.  */
	qcam_set(qcam, 45, 2);
}

/* Read some bytes from the camera and put them in the buffer.
   nbytes should be a multiple of 3, because bidirectional mode gives
   us three bytes at a time.  */

static unsigned int qcam_read_bytes(struct qcam *qcam, unsigned char *buf, unsigned int nbytes)
{
	unsigned int bytes = 0;

	qcam_set_ack(qcam, 0);
	if (qcam->bidirectional) {
		/* It's a bidirectional port */
		while (bytes < nbytes) {
			unsigned int lo1, hi1, lo2, hi2;
			unsigned char r, g, b;

			if (qcam_await_ready2(qcam, 1))
				return bytes;
			lo1 = parport_read_data(qcam->pport) >> 1;
			hi1 = ((parport_read_status(qcam->pport) >> 3) & 0x1f) ^ 0x10;
			qcam_set_ack(qcam, 1);
			if (qcam_await_ready2(qcam, 0))
				return bytes;
			lo2 = parport_read_data(qcam->pport) >> 1;
			hi2 = ((parport_read_status(qcam->pport) >> 3) & 0x1f) ^ 0x10;
			qcam_set_ack(qcam, 0);
			r = lo1 | ((hi1 & 1) << 7);
			g = ((hi1 & 0x1e) << 3) | ((hi2 & 0x1e) >> 1);
			b = lo2 | ((hi2 & 1) << 7);
			if (force_rgb) {
				buf[bytes++] = r;
				buf[bytes++] = g;
				buf[bytes++] = b;
			} else {
				buf[bytes++] = b;
				buf[bytes++] = g;
				buf[bytes++] = r;
			}
		}
	} else {
		/* It's a unidirectional port */
		int i = 0, n = bytes;
		unsigned char rgb[3];

		while (bytes < nbytes) {
			unsigned int hi, lo;

			if (qcam_await_ready1(qcam, 1))
				return bytes;
			hi = (parport_read_status(qcam->pport) & 0xf0);
			qcam_set_ack(qcam, 1);
			if (qcam_await_ready1(qcam, 0))
				return bytes;
			lo = (parport_read_status(qcam->pport) & 0xf0);
			qcam_set_ack(qcam, 0);
			/* flip some bits */
			rgb[(i = bytes++ % 3)] = (hi | (lo >> 4)) ^ 0x88;
			if (i >= 2) {
get_fragment:
				if (force_rgb) {
					buf[n++] = rgb[0];
					buf[n++] = rgb[1];
					buf[n++] = rgb[2];
				} else {
					buf[n++] = rgb[2];
					buf[n++] = rgb[1];
					buf[n++] = rgb[0];
				}
			}
		}
		if (i) {
			i = 0;
			goto get_fragment;
		}
	}
	return bytes;
}

#define BUFSZ	150

static long qc_capture(struct qcam *qcam, char __user *buf, unsigned long len)
{
	struct v4l2_device *v4l2_dev = &qcam->v4l2_dev;
	unsigned lines, pixelsperline, bitsperxfer;
	unsigned int is_bi_dir = qcam->bidirectional;
	size_t wantlen, outptr = 0;
	char tmpbuf[BUFSZ];

	if (!access_ok(VERIFY_WRITE, buf, len))
		return -EFAULT;

	/* Wait for camera to become ready */
	for (;;) {
		int i = qcam_get(qcam, 41);

		if (i == -1) {
			qc_setup(qcam);
			return -EIO;
		}
		if ((i & 0x80) == 0)
			break;
		schedule();
	}

	if (qcam_set(qcam, 7, (qcam->mode | (is_bi_dir ? 1 : 0)) + 1))
		return -EIO;

	lines = qcam->height;
	pixelsperline = qcam->width;
	bitsperxfer = (is_bi_dir) ? 24 : 8;

	if (is_bi_dir) {
		/* Turn the port around */
		parport_data_reverse(qcam->pport);
		mdelay(3);
		qcam_set_ack(qcam, 0);
		if (qcam_await_ready1(qcam, 1)) {
			qc_setup(qcam);
			return -EIO;
		}
		qcam_set_ack(qcam, 1);
		if (qcam_await_ready1(qcam, 0)) {
			qc_setup(qcam);
			return -EIO;
		}
	}

	wantlen = lines * pixelsperline * 24 / 8;

	while (wantlen) {
		size_t t, s;

		s = (wantlen > BUFSZ) ? BUFSZ : wantlen;
		t = qcam_read_bytes(qcam, tmpbuf, s);
		if (outptr < len) {
			size_t sz = len - outptr;

			if (sz > t)
				sz = t;
			if (__copy_to_user(buf + outptr, tmpbuf, sz))
				break;
			outptr += sz;
		}
		wantlen -= t;
		if (t < s)
			break;
		cond_resched();
	}

	len = outptr;

	if (wantlen) {
		v4l2_err(v4l2_dev, "short read.\n");
		if (is_bi_dir)
			parport_data_forward(qcam->pport);
		qc_setup(qcam);
		return len;
	}

	if (is_bi_dir) {
		int l;

		do {
			l = qcam_read_bytes(qcam, tmpbuf, 3);
			cond_resched();
		} while (l && (tmpbuf[0] == 0x7e || tmpbuf[1] == 0x7e || tmpbuf[2] == 0x7e));
		if (force_rgb) {
			if (tmpbuf[0] != 0xe || tmpbuf[1] != 0x0 || tmpbuf[2] != 0xf)
				v4l2_err(v4l2_dev, "bad EOF\n");
		} else {
			if (tmpbuf[0] != 0xf || tmpbuf[1] != 0x0 || tmpbuf[2] != 0xe)
				v4l2_err(v4l2_dev, "bad EOF\n");
		}
		qcam_set_ack(qcam, 0);
		if (qcam_await_ready1(qcam, 1)) {
			v4l2_err(v4l2_dev, "no ack after EOF\n");
			parport_data_forward(qcam->pport);
			qc_setup(qcam);
			return len;
		}
		parport_data_forward(qcam->pport);
		mdelay(3);
		qcam_set_ack(qcam, 1);
		if (qcam_await_ready1(qcam, 0)) {
			v4l2_err(v4l2_dev, "no ack to port turnaround\n");
			qc_setup(qcam);
			return len;
		}
	} else {
		int l;

		do {
			l = qcam_read_bytes(qcam, tmpbuf, 1);
			cond_resched();
		} while (l && tmpbuf[0] == 0x7e);
		l = qcam_read_bytes(qcam, tmpbuf + 1, 2);
		if (force_rgb) {
			if (tmpbuf[0] != 0xe || tmpbuf[1] != 0x0 || tmpbuf[2] != 0xf)
				v4l2_err(v4l2_dev, "bad EOF\n");
		} else {
			if (tmpbuf[0] != 0xf || tmpbuf[1] != 0x0 || tmpbuf[2] != 0xe)
				v4l2_err(v4l2_dev, "bad EOF\n");
		}
	}

	qcam_write_data(qcam, 0);
	return len;
}

/*
 *	Video4linux interfacing
 */

static int qcam_querycap(struct file *file, void  *priv,
					struct v4l2_capability *vcap)
{
	struct qcam *qcam = video_drvdata(file);

	strlcpy(vcap->driver, qcam->v4l2_dev.name, sizeof(vcap->driver));
	strlcpy(vcap->card, "Color Quickcam", sizeof(vcap->card));
	strlcpy(vcap->bus_info, "parport", sizeof(vcap->bus_info));
	vcap->version = KERNEL_VERSION(0, 0, 3);
	vcap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
	return 0;
}

static int qcam_enum_input(struct file *file, void *fh, struct v4l2_input *vin)
{
	if (vin->index > 0)
		return -EINVAL;
	strlcpy(vin->name, "Camera", sizeof(vin->name));
	vin->type = V4L2_INPUT_TYPE_CAMERA;
	vin->audioset = 0;
	vin->tuner = 0;
	vin->std = 0;
	vin->status = 0;
	return 0;
}

static int qcam_g_input(struct file *file, void *fh, unsigned int *inp)
{
	*inp = 0;
	return 0;
}

static int qcam_s_input(struct file *file, void *fh, unsigned int inp)
{
	return (inp > 0) ? -EINVAL : 0;
}

static int qcam_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 240);
	case V4L2_CID_CONTRAST:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 192);
	case V4L2_CID_GAMMA:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 128);
	}
	return -EINVAL;
}

static int qcam_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct qcam *qcam = video_drvdata(file);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = qcam->brightness;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = qcam->contrast;
		break;
	case V4L2_CID_GAMMA:
		ctrl->value = qcam->whitebal;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int qcam_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct qcam *qcam = video_drvdata(file);
	int ret = 0;

	mutex_lock(&qcam->lock);
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		qcam->brightness = ctrl->value;
		break;
	case V4L2_CID_CONTRAST:
		qcam->contrast = ctrl->value;
		break;
	case V4L2_CID_GAMMA:
		qcam->whitebal = ctrl->value;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret == 0) {
		parport_claim_or_block(qcam->pdev);
		qc_setup(qcam);
		parport_release(qcam->pdev);
	}
	mutex_unlock(&qcam->lock);
	return ret;
}

static int qcam_g_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct qcam *qcam = video_drvdata(file);
	struct v4l2_pix_format *pix = &fmt->fmt.pix;

	pix->width = qcam->width;
	pix->height = qcam->height;
	pix->pixelformat = V4L2_PIX_FMT_RGB24;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = 3 * qcam->width;
	pix->sizeimage = 3 * qcam->width * qcam->height;
	/* Just a guess */
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	return 0;
}

static int qcam_try_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct v4l2_pix_format *pix = &fmt->fmt.pix;

	if (pix->height < 60 || pix->width < 80) {
		pix->height = 60;
		pix->width = 80;
	} else if (pix->height < 120 || pix->width < 160) {
		pix->height = 120;
		pix->width = 160;
	} else {
		pix->height = 240;
		pix->width = 320;
	}
	pix->pixelformat = V4L2_PIX_FMT_RGB24;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = 3 * pix->width;
	pix->sizeimage = 3 * pix->width * pix->height;
	/* Just a guess */
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	return 0;
}

static int qcam_s_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct qcam *qcam = video_drvdata(file);
	struct v4l2_pix_format *pix = &fmt->fmt.pix;
	int ret = qcam_try_fmt_vid_cap(file, fh, fmt);

	if (ret)
		return ret;
	switch (pix->height) {
	case 60:
		qcam->mode = QC_DECIMATION_4;
		break;
	case 120:
		qcam->mode = QC_DECIMATION_2;
		break;
	default:
		qcam->mode = QC_DECIMATION_1;
		break;
	}

	mutex_lock(&qcam->lock);
	qcam->mode |= QC_MILLIONS;
	qcam->height = pix->height;
	qcam->width = pix->width;
	parport_claim_or_block(qcam->pdev);
	qc_setup(qcam);
	parport_release(qcam->pdev);
	mutex_unlock(&qcam->lock);
	return 0;
}

static int qcam_enum_fmt_vid_cap(struct file *file, void *fh, struct v4l2_fmtdesc *fmt)
{
	static struct v4l2_fmtdesc formats[] = {
		{ 0, 0, 0,
		  "RGB 8:8:8", V4L2_PIX_FMT_RGB24,
		  { 0, 0, 0, 0 }
		},
	};
	enum v4l2_buf_type type = fmt->type;

	if (fmt->index > 0)
		return -EINVAL;

	*fmt = formats[fmt->index];
	fmt->type = type;
	return 0;
}

static ssize_t qcam_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct qcam *qcam = video_drvdata(file);
	int len;

	mutex_lock(&qcam->lock);
	parport_claim_or_block(qcam->pdev);
	/* Probably should have a semaphore against multiple users */
	len = qc_capture(qcam, buf, count);
	parport_release(qcam->pdev);
	mutex_unlock(&qcam->lock);
	return len;
}

static const struct v4l2_file_operations qcam_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.read		= qcam_read,
};

static const struct v4l2_ioctl_ops qcam_ioctl_ops = {
	.vidioc_querycap    		    = qcam_querycap,
	.vidioc_g_input      		    = qcam_g_input,
	.vidioc_s_input      		    = qcam_s_input,
	.vidioc_enum_input   		    = qcam_enum_input,
	.vidioc_queryctrl 		    = qcam_queryctrl,
	.vidioc_g_ctrl  		    = qcam_g_ctrl,
	.vidioc_s_ctrl 			    = qcam_s_ctrl,
	.vidioc_enum_fmt_vid_cap 	    = qcam_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap 		    = qcam_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap  		    = qcam_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap  	    = qcam_try_fmt_vid_cap,
};

/* Initialize the QuickCam driver control structure. */

static struct qcam *qcam_init(struct parport *port)
{
	struct qcam *qcam;
	struct v4l2_device *v4l2_dev;

	qcam = kzalloc(sizeof(*qcam), GFP_KERNEL);
	if (qcam == NULL)
		return NULL;

	v4l2_dev = &qcam->v4l2_dev;
	strlcpy(v4l2_dev->name, "c-qcam", sizeof(v4l2_dev->name));

	if (v4l2_device_register(NULL, v4l2_dev) < 0) {
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		return NULL;
	}

	qcam->pport = port;
	qcam->pdev = parport_register_device(port, "c-qcam", NULL, NULL,
					  NULL, 0, NULL);

	qcam->bidirectional = (qcam->pport->modes & PARPORT_MODE_TRISTATE) ? 1 : 0;

	if (qcam->pdev == NULL) {
		v4l2_err(v4l2_dev, "couldn't register for %s.\n", port->name);
		kfree(qcam);
		return NULL;
	}

	strlcpy(qcam->vdev.name, "Colour QuickCam", sizeof(qcam->vdev.name));
	qcam->vdev.v4l2_dev = v4l2_dev;
	qcam->vdev.fops = &qcam_fops;
	qcam->vdev.ioctl_ops = &qcam_ioctl_ops;
	qcam->vdev.release = video_device_release_empty;
	video_set_drvdata(&qcam->vdev, qcam);

	mutex_init(&qcam->lock);
	qcam->width = qcam->ccd_width = 320;
	qcam->height = qcam->ccd_height = 240;
	qcam->mode = QC_MILLIONS | QC_DECIMATION_1;
	qcam->contrast = 192;
	qcam->brightness = 240;
	qcam->whitebal = 128;
	qcam->top = 1;
	qcam->left = 14;
	return qcam;
}

static int init_cqcam(struct parport *port)
{
	struct qcam *qcam;
	struct v4l2_device *v4l2_dev;

	if (parport[0] != -1) {
		/* The user gave specific instructions */
		int i, found = 0;

		for (i = 0; i < MAX_CAMS && parport[i] != -1; i++) {
			if (parport[0] == port->number)
				found = 1;
		}
		if (!found)
			return -ENODEV;
	}

	if (num_cams == MAX_CAMS)
		return -ENOSPC;

	qcam = qcam_init(port);
	if (qcam == NULL)
		return -ENODEV;

	v4l2_dev = &qcam->v4l2_dev;

	parport_claim_or_block(qcam->pdev);

	qc_reset(qcam);

	if (probe && qc_detect(qcam) == 0) {
		parport_release(qcam->pdev);
		parport_unregister_device(qcam->pdev);
		kfree(qcam);
		return -ENODEV;
	}

	qc_setup(qcam);

	parport_release(qcam->pdev);

	if (video_register_device(&qcam->vdev, VFL_TYPE_GRABBER, video_nr) < 0) {
		v4l2_err(v4l2_dev, "Unable to register Colour QuickCam on %s\n",
		       qcam->pport->name);
		parport_unregister_device(qcam->pdev);
		kfree(qcam);
		return -ENODEV;
	}

	v4l2_info(v4l2_dev, "%s: Colour QuickCam found on %s\n",
	       video_device_node_name(&qcam->vdev), qcam->pport->name);

	qcams[num_cams++] = qcam;

	return 0;
}

static void close_cqcam(struct qcam *qcam)
{
	video_unregister_device(&qcam->vdev);
	parport_unregister_device(qcam->pdev);
	kfree(qcam);
}

static void cq_attach(struct parport *port)
{
	init_cqcam(port);
}

static void cq_detach(struct parport *port)
{
	/* Write this some day. */
}

static struct parport_driver cqcam_driver = {
	.name = "cqcam",
	.attach = cq_attach,
	.detach = cq_detach,
};

static int __init cqcam_init(void)
{
	printk(KERN_INFO BANNER "\n");

	return parport_register_driver(&cqcam_driver);
}

static void __exit cqcam_cleanup(void)
{
	unsigned int i;

	for (i = 0; i < num_cams; i++)
		close_cqcam(qcams[i]);

	parport_unregister_driver(&cqcam_driver);
}

MODULE_AUTHOR("Philip Blundell <philb@gnu.org>");
MODULE_DESCRIPTION(BANNER);
MODULE_LICENSE("GPL");

module_init(cqcam_init);
module_exit(cqcam_cleanup);
