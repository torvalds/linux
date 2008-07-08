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
#include <linux/slab.h>
#include <linux/delay.h>

#include "saa7134-reg.h"
#include "saa7134.h"

#include <media/saa6752hs.h>
#include <media/v4l2-common.h>

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
	struct v4l2_ext_controls ctrls = { V4L2_CTRL_CLASS_MPEG, 0 };

	ts_reset_encoder(dev);
	saa7134_i2c_call_clients(dev, VIDIOC_S_EXT_CTRLS, &ctrls);
	dev->empress_started = 1;
	return 0;
}

/* ------------------------------------------------------------------ */

static int ts_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct saa7134_dev *dev;
	int err;

	list_for_each_entry(dev, &saa7134_devlist, devlist)
		if (dev->empress_dev && dev->empress_dev->minor == minor)
			goto found;
	return -ENODEV;
 found:

	dprintk("open minor=%d\n",minor);
	err = -EBUSY;
	if (!mutex_trylock(&dev->empress_tsq.vb_lock))
		goto done;
	if (dev->empress_users)
		goto done_up;

	/* Unmute audio */
	saa_writeb(SAA7134_AUDIO_MUTE_CTRL,
		saa_readb(SAA7134_AUDIO_MUTE_CTRL) & ~(1 << 6));

	dev->empress_users++;
	file->private_data = dev;
	err = 0;

done_up:
	mutex_unlock(&dev->empress_tsq.vb_lock);
done:
	return err;
}

static int ts_release(struct inode *inode, struct file *file)
{
	struct saa7134_dev *dev = file->private_data;

	mutex_lock(&dev->empress_tsq.vb_lock);

	videobuf_stop(&dev->empress_tsq);
	videobuf_mmap_free(&dev->empress_tsq);

	/* stop the encoder */
	ts_reset_encoder(dev);

	/* Mute audio */
	saa_writeb(SAA7134_AUDIO_MUTE_CTRL,
		saa_readb(SAA7134_AUDIO_MUTE_CTRL) | (1 << 6));

	dev->empress_users--;

	mutex_unlock(&dev->empress_tsq.vb_lock);

	return 0;
}

static ssize_t
ts_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct saa7134_dev *dev = file->private_data;

	if (!dev->empress_started)
		ts_init_encoder(dev);

	return videobuf_read_stream(&dev->empress_tsq,
				    data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

static unsigned int
ts_poll(struct file *file, struct poll_table_struct *wait)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_poll_stream(file, &dev->empress_tsq, wait);
}


static int
ts_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_mmap_mapper(&dev->empress_tsq, vma);
}

/*
 * This function is _not_ called directly, but from
 * video_generic_ioctl (and maybe others).  userspace
 * copying is done already, arg is a kernel pointer.
 */

static int empress_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct saa7134_dev *dev = file->private_data;

	strcpy(cap->driver, "saa7134");
	strlcpy(cap->card, saa7134_boards[dev->board].name,
		sizeof(cap->card));
	sprintf(cap->bus_info, "PCI:%s", pci_name(dev->pci));
	cap->version = SAA7134_VERSION_CODE;
	cap->capabilities =
		V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE |
		V4L2_CAP_STREAMING;
	return 0;
}

static int empress_enum_input(struct file *file, void *priv,
					struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;
	strcpy(i->name, "CCIR656");

	return 0;
}

static int empress_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int empress_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;

	return 0;
}

static int empress_enum_fmt_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	strlcpy(f->description, "MPEG TS", sizeof(f->description));
	f->pixelformat = V4L2_PIX_FMT_MPEG;

	return 0;
}

static int empress_g_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7134_dev *dev = file->private_data;

	saa7134_i2c_call_clients(dev, VIDIOC_G_FMT, f);

	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.sizeimage    = TS_PACKET_SIZE * dev->ts.nr_packets;

	return 0;
}

static int empress_s_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7134_dev *dev = file->private_data;

	saa7134_i2c_call_clients(dev, VIDIOC_S_FMT, f);

	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.sizeimage    = TS_PACKET_SIZE * dev->ts.nr_packets;

	return 0;
}


static int empress_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *p)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_reqbufs(&dev->empress_tsq, p);
}

static int empress_querybuf(struct file *file, void *priv,
					struct v4l2_buffer *b)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_querybuf(&dev->empress_tsq, b);
}

static int empress_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_qbuf(&dev->empress_tsq, b);
}

static int empress_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_dqbuf(&dev->empress_tsq, b,
				file->f_flags & O_NONBLOCK);
}

static int empress_streamon(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_streamon(&dev->empress_tsq);
}

static int empress_streamoff(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct saa7134_dev *dev = file->private_data;

	return videobuf_streamoff(&dev->empress_tsq);
}

static int empress_s_ext_ctrls(struct file *file, void *priv,
			       struct v4l2_ext_controls *ctrls)
{
	struct saa7134_dev *dev = file->private_data;

	/* count == 0 is abused in saa6752hs.c, so that special
		case is handled here explicitly. */
	if (ctrls->count == 0)
		return 0;

	if (ctrls->ctrl_class != V4L2_CTRL_CLASS_MPEG)
		return -EINVAL;

	saa7134_i2c_call_clients(dev, VIDIOC_S_EXT_CTRLS, ctrls);
	ts_init_encoder(dev);

	return 0;
}

static int empress_g_ext_ctrls(struct file *file, void *priv,
			       struct v4l2_ext_controls *ctrls)
{
	struct saa7134_dev *dev = file->private_data;

	if (ctrls->ctrl_class != V4L2_CTRL_CLASS_MPEG)
		return -EINVAL;
	saa7134_i2c_call_clients(dev, VIDIOC_G_EXT_CTRLS, ctrls);

	return 0;
}

static const struct file_operations ts_fops =
{
	.owner	  = THIS_MODULE,
	.open	  = ts_open,
	.release  = ts_release,
	.read	  = ts_read,
	.poll	  = ts_poll,
	.mmap	  = ts_mmap,
	.ioctl	  = video_ioctl2,
	.llseek   = no_llseek,
};

/* ----------------------------------------------------------- */

static struct video_device saa7134_empress_template =
{
	.name          = "saa7134-empress",
	.type          = 0 /* FIXME */,
	.type2         = 0 /* FIXME */,
	.fops          = &ts_fops,
	.minor	       = -1,

	.vidioc_querycap		= empress_querycap,
	.vidioc_enum_fmt_cap		= empress_enum_fmt_cap,
	.vidioc_s_fmt_cap		= empress_s_fmt_cap,
	.vidioc_g_fmt_cap		= empress_g_fmt_cap,
	.vidioc_reqbufs			= empress_reqbufs,
	.vidioc_querybuf		= empress_querybuf,
	.vidioc_qbuf			= empress_qbuf,
	.vidioc_dqbuf			= empress_dqbuf,
	.vidioc_streamon		= empress_streamon,
	.vidioc_streamoff		= empress_streamoff,
	.vidioc_s_ext_ctrls		= empress_s_ext_ctrls,
	.vidioc_g_ext_ctrls		= empress_g_ext_ctrls,
	.vidioc_enum_input		= empress_enum_input,
	.vidioc_g_input			= empress_g_input,
	.vidioc_s_input			= empress_s_input,

	.vidioc_queryctrl		= saa7134_queryctrl,
	.vidioc_g_ctrl			= saa7134_g_ctrl,
	.vidioc_s_ctrl			= saa7134_s_ctrl,

	.tvnorms			= SAA7134_NORMS,
	.current_norm			= V4L2_STD_PAL,
};

static void empress_signal_update(struct work_struct *work)
{
	struct saa7134_dev* dev =
		container_of(work, struct saa7134_dev, empress_workqueue);

	if (dev->nosignal) {
		dprintk("no video signal\n");
		ts_reset_encoder(dev);
	} else {
		dprintk("video signal acquired\n");
		if (dev->empress_users)
			ts_init_encoder(dev);
	}
}

static void empress_signal_change(struct saa7134_dev *dev)
{
	schedule_work(&dev->empress_workqueue);
}


static int empress_init(struct saa7134_dev *dev)
{
	int err;

	dprintk("%s: %s\n",dev->name,__func__);
	dev->empress_dev = video_device_alloc();
	if (NULL == dev->empress_dev)
		return -ENOMEM;
	*(dev->empress_dev) = saa7134_empress_template;
	dev->empress_dev->dev     = &dev->pci->dev;
	dev->empress_dev->release = video_device_release;
	snprintf(dev->empress_dev->name, sizeof(dev->empress_dev->name),
		 "%s empress (%s)", dev->name,
		 saa7134_boards[dev->board].name);

	INIT_WORK(&dev->empress_workqueue, empress_signal_update);

	err = video_register_device(dev->empress_dev,VFL_TYPE_GRABBER,
				    empress_nr[dev->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n",
		       dev->name);
		video_device_release(dev->empress_dev);
		dev->empress_dev = NULL;
		return err;
	}
	printk(KERN_INFO "%s: registered device video%d [mpeg]\n",
	       dev->name,dev->empress_dev->minor & 0x1f);

	videobuf_queue_sg_init(&dev->empress_tsq, &saa7134_ts_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_ALTERNATE,
			    sizeof(struct saa7134_buf),
			    dev);

	empress_signal_update(&dev->empress_workqueue);
	return 0;
}

static int empress_fini(struct saa7134_dev *dev)
{
	dprintk("%s: %s\n",dev->name,__func__);

	if (NULL == dev->empress_dev)
		return 0;
	flush_scheduled_work();
	video_unregister_device(dev->empress_dev);
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
