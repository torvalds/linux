/*
 *
 * (c) 2004 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include <media/v4l2-common.h>
#include <media/v4l2-event.h>

#include "saa7134-reg.h"
#include "saa7134.h"

/* ------------------------------------------------------------------ */

MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int empress_nr[] = {[0 ... (SAA7134_MAXBOARDS - 1)] = UNSET };

module_param_array(empress_nr, int, NULL, 0444);
MODULE_PARM_DESC(empress_nr,"ts device number");

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,"enable debug messages");

#define dprintk(fmt, arg...)	if (debug)			\
	printk(KERN_DEBUG "%s/empress: " fmt, dev->name , ## arg)

/* ------------------------------------------------------------------ */

static void ts_reset_encoder(struct saa7134_dev* dev)
{
	if (!dev->empress_started)
		return;

	saa_writeb(SAA7134_SPECIAL_MODE, 0x00);
	msleep(10);
	saa_writeb(SAA7134_SPECIAL_MODE, 0x01);
	msleep(100);
	dev->empress_started = 0;
}

static int ts_init_encoder(struct saa7134_dev* dev)
{
	u32 leading_null_bytes = 0;

	/* If more cards start to need this, then this
	   should probably be added to the card definitions. */
	switch (dev->board) {
	case SAA7134_BOARD_BEHOLD_M6:
	case SAA7134_BOARD_BEHOLD_M63:
	case SAA7134_BOARD_BEHOLD_M6_EXTRA:
		leading_null_bytes = 1;
		break;
	}
	ts_reset_encoder(dev);
	saa_call_all(dev, core, init, leading_null_bytes);
	dev->empress_started = 1;
	return 0;
}

/* ------------------------------------------------------------------ */

static int ts_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh;

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;

	v4l2_fh_init(&fh->fh, vdev);
	file->private_data = fh;
	fh->is_empress = true;
	v4l2_fh_add(&fh->fh);

	/* Unmute audio */
	saa_writeb(SAA7134_AUDIO_MUTE_CTRL,
		saa_readb(SAA7134_AUDIO_MUTE_CTRL) & ~(1 << 6));

	return 0;
}

static int ts_release(struct file *file)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh = file->private_data;

	if (res_check(fh, RESOURCE_EMPRESS)) {
		videobuf_stop(&dev->empress_tsq);
		videobuf_mmap_free(&dev->empress_tsq);

		/* stop the encoder */
		ts_reset_encoder(dev);

		/* Mute audio */
		saa_writeb(SAA7134_AUDIO_MUTE_CTRL,
				saa_readb(SAA7134_AUDIO_MUTE_CTRL) | (1 << 6));
	}

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	return 0;
}

static ssize_t
ts_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct saa7134_dev *dev = video_drvdata(file);

	if (res_locked(dev, RESOURCE_EMPRESS))
		return -EBUSY;
	if (!dev->empress_started)
		ts_init_encoder(dev);

	return videobuf_read_stream(&dev->empress_tsq,
				    data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

static unsigned int
ts_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned long req_events = poll_requested_events(wait);
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh = file->private_data;
	unsigned int rc = 0;

	if (v4l2_event_pending(&fh->fh))
		rc = POLLPRI;
	else if (req_events & POLLPRI)
		poll_wait(file, &fh->fh.wait, wait);
	return rc | videobuf_poll_stream(file, &dev->empress_tsq, wait);
}


static int
ts_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct saa7134_dev *dev = video_drvdata(file);

	return videobuf_mmap_mapper(&dev->empress_tsq, vma);
}

static int empress_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	strlcpy(f->description, "MPEG TS", sizeof(f->description));
	f->pixelformat = V4L2_PIX_FMT_MPEG;
	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	return 0;
}

static int empress_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct v4l2_mbus_framefmt mbus_fmt;

	saa_call_all(dev, video, g_mbus_fmt, &mbus_fmt);

	v4l2_fill_pix_format(&f->fmt.pix, &mbus_fmt);
	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.sizeimage    = TS_PACKET_SIZE * dev->ts.nr_packets;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.priv = 0;

	return 0;
}

static int empress_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct v4l2_mbus_framefmt mbus_fmt;

	v4l2_fill_mbus_format(&mbus_fmt, &f->fmt.pix, V4L2_MBUS_FMT_FIXED);
	saa_call_all(dev, video, s_mbus_fmt, &mbus_fmt);
	v4l2_fill_pix_format(&f->fmt.pix, &mbus_fmt);

	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.sizeimage    = TS_PACKET_SIZE * dev->ts.nr_packets;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.priv = 0;

	return 0;
}

static int empress_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct v4l2_mbus_framefmt mbus_fmt;

	v4l2_fill_mbus_format(&mbus_fmt, &f->fmt.pix, V4L2_MBUS_FMT_FIXED);
	saa_call_all(dev, video, try_mbus_fmt, &mbus_fmt);
	v4l2_fill_pix_format(&f->fmt.pix, &mbus_fmt);

	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.sizeimage    = TS_PACKET_SIZE * dev->ts.nr_packets;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.priv = 0;

	return 0;
}

static const struct v4l2_file_operations ts_fops =
{
	.owner	  = THIS_MODULE,
	.open	  = ts_open,
	.release  = ts_release,
	.read	  = ts_read,
	.poll	  = ts_poll,
	.mmap	  = ts_mmap,
	.ioctl	  = video_ioctl2,
};

static const struct v4l2_ioctl_ops ts_ioctl_ops = {
	.vidioc_querycap		= saa7134_querycap,
	.vidioc_enum_fmt_vid_cap	= empress_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= empress_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= empress_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= empress_g_fmt_vid_cap,
	.vidioc_reqbufs			= saa7134_reqbufs,
	.vidioc_querybuf		= saa7134_querybuf,
	.vidioc_qbuf			= saa7134_qbuf,
	.vidioc_dqbuf			= saa7134_dqbuf,
	.vidioc_streamon		= saa7134_streamon,
	.vidioc_streamoff		= saa7134_streamoff,
	.vidioc_g_frequency		= saa7134_g_frequency,
	.vidioc_s_frequency		= saa7134_s_frequency,
	.vidioc_g_tuner			= saa7134_g_tuner,
	.vidioc_s_tuner			= saa7134_s_tuner,
	.vidioc_enum_input		= saa7134_enum_input,
	.vidioc_g_input			= saa7134_g_input,
	.vidioc_s_input			= saa7134_s_input,
	.vidioc_s_std			= saa7134_s_std,
	.vidioc_g_std			= saa7134_g_std,
	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* ----------------------------------------------------------- */

static struct video_device saa7134_empress_template = {
	.name          = "saa7134-empress",
	.fops          = &ts_fops,
	.ioctl_ops     = &ts_ioctl_ops,

	.tvnorms			= SAA7134_NORMS,
};

static void empress_signal_update(struct work_struct *work)
{
	struct saa7134_dev* dev =
		container_of(work, struct saa7134_dev, empress_workqueue);

	if (dev->nosignal) {
		dprintk("no video signal\n");
	} else {
		dprintk("video signal acquired\n");
	}
}

static void empress_signal_change(struct saa7134_dev *dev)
{
	schedule_work(&dev->empress_workqueue);
}

static bool empress_ctrl_filter(const struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_PRIVATE_INVERT:
	case V4L2_CID_PRIVATE_AUTOMUTE:
		return true;
	default:
		return false;
	}
}

static int empress_init(struct saa7134_dev *dev)
{
	struct v4l2_ctrl_handler *hdl = &dev->empress_ctrl_handler;
	int err;

	dprintk("%s: %s\n",dev->name,__func__);
	dev->empress_dev = video_device_alloc();
	if (NULL == dev->empress_dev)
		return -ENOMEM;
	*(dev->empress_dev) = saa7134_empress_template;
	dev->empress_dev->v4l2_dev  = &dev->v4l2_dev;
	dev->empress_dev->release = video_device_release;
	snprintf(dev->empress_dev->name, sizeof(dev->empress_dev->name),
		 "%s empress (%s)", dev->name,
		 saa7134_boards[dev->board].name);
	set_bit(V4L2_FL_USE_FH_PRIO, &dev->empress_dev->flags);
	v4l2_ctrl_handler_init(hdl, 21);
	v4l2_ctrl_add_handler(hdl, &dev->ctrl_handler, empress_ctrl_filter);
	if (dev->empress_sd)
		v4l2_ctrl_add_handler(hdl, dev->empress_sd->ctrl_handler, NULL);
	if (hdl->error) {
		video_device_release(dev->empress_dev);
		return hdl->error;
	}
	dev->empress_dev->ctrl_handler = hdl;

	INIT_WORK(&dev->empress_workqueue, empress_signal_update);

	video_set_drvdata(dev->empress_dev, dev);
	err = video_register_device(dev->empress_dev,VFL_TYPE_GRABBER,
				    empress_nr[dev->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n",
		       dev->name);
		video_device_release(dev->empress_dev);
		dev->empress_dev = NULL;
		return err;
	}
	printk(KERN_INFO "%s: registered device %s [mpeg]\n",
	       dev->name, video_device_node_name(dev->empress_dev));

	videobuf_queue_sg_init(&dev->empress_tsq, &saa7134_ts_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_ALTERNATE,
			    sizeof(struct saa7134_buf),
			    dev, NULL);

	empress_signal_update(&dev->empress_workqueue);
	return 0;
}

static int empress_fini(struct saa7134_dev *dev)
{
	dprintk("%s: %s\n",dev->name,__func__);

	if (NULL == dev->empress_dev)
		return 0;
	flush_work(&dev->empress_workqueue);
	video_unregister_device(dev->empress_dev);
	v4l2_ctrl_handler_free(&dev->empress_ctrl_handler);
	dev->empress_dev = NULL;
	return 0;
}

static struct saa7134_mpeg_ops empress_ops = {
	.type          = SAA7134_MPEG_EMPRESS,
	.init          = empress_init,
	.fini          = empress_fini,
	.signal_change = empress_signal_change,
};

static int __init empress_register(void)
{
	return saa7134_ts_register(&empress_ops);
}

static void __exit empress_unregister(void)
{
	saa7134_ts_unregister(&empress_ops);
}

module_init(empress_register);
module_exit(empress_unregister);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
