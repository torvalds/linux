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
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "saa7134-reg.h"
#include "saa7134.h"

#include <media/saa6752hs.h>

/* ------------------------------------------------------------------ */

MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int empress_nr[] = {[0 ... (SAA7134_MAXBOARDS - 1)] = UNSET };
module_param_array(empress_nr, int, NULL, 0444);
MODULE_PARM_DESC(empress_nr,"ts device number");

static unsigned int debug = 0;
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
	ts_reset_encoder(dev);
	saa7134_i2c_call_clients(dev, VIDIOC_S_MPEGCOMP, NULL);
	dev->empress_started = 1;
	return 0;
}

/* ------------------------------------------------------------------ */

static int ts_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct saa7134_dev *h,*dev = NULL;
	struct list_head *list;
	int err;

	list_for_each(list,&saa7134_devlist) {
		h = list_entry(list, struct saa7134_dev, devlist);
		if (h->empress_dev && h->empress_dev->minor == minor)
			dev = h;
	}
	if (NULL == dev)
		return -ENODEV;

	dprintk("open minor=%d\n",minor);
	err = -EBUSY;
	if (down_trylock(&dev->empress_tsq.lock))
		goto done;
	if (dev->empress_users)
		goto done_up;

	dev->empress_users++;
	file->private_data = dev;
	err = 0;

done_up:
	up(&dev->empress_tsq.lock);
done:
	return err;
}

static int ts_release(struct inode *inode, struct file *file)
{
	struct saa7134_dev *dev = file->private_data;

	if (dev->empress_tsq.streaming)
		videobuf_streamoff(&dev->empress_tsq);
	down(&dev->empress_tsq.lock);
	if (dev->empress_tsq.reading)
		videobuf_read_stop(&dev->empress_tsq);
	videobuf_mmap_free(&dev->empress_tsq);
	dev->empress_users--;

	/* stop the encoder */
	ts_reset_encoder(dev);

	up(&dev->empress_tsq.lock);
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
static int ts_do_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, void *arg)
{
	struct saa7134_dev *dev = file->private_data;

	if (debug > 1)
		saa7134_print_ioctl(dev->name,cmd);
	switch (cmd) {
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
		strcpy(cap->driver, "saa7134");
		strlcpy(cap->card, saa7134_boards[dev->board].name,
			sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s",pci_name(dev->pci));
		cap->version = SAA7134_VERSION_CODE;
		cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_READWRITE |
			V4L2_CAP_STREAMING;
		return 0;
	}

	/* --- input switching --------------------------------------- */
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;

		if (i->index != 0)
			return -EINVAL;
		i->type = V4L2_INPUT_TYPE_CAMERA;
		strcpy(i->name,"CCIR656");
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *i = arg;
		*i = 0;
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		int *i = arg;

		if (*i != 0)
			return -EINVAL;
		return 0;
	}
	/* --- capture ioctls ---------------------------------------- */

	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		int index;

		index = f->index;
		if (index != 0)
			return -EINVAL;

		memset(f,0,sizeof(*f));
		f->index = index;
		strlcpy(f->description, "MPEG TS", sizeof(f->description));
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		f->pixelformat = V4L2_PIX_FMT_MPEG;
		return 0;
	}

	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;

		memset(f,0,sizeof(*f));
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		saa7134_i2c_call_clients(dev, cmd, arg);
		f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
		f->fmt.pix.sizeimage    = TS_PACKET_SIZE * dev->ts.nr_packets;
		return 0;
	}

	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;

		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		    return -EINVAL;

		saa7134_i2c_call_clients(dev, cmd, arg);
		f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
		f->fmt.pix.sizeimage    = TS_PACKET_SIZE* dev->ts.nr_packets;
		return 0;
	}

	case VIDIOC_REQBUFS:
		return videobuf_reqbufs(&dev->empress_tsq,arg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(&dev->empress_tsq,arg);

	case VIDIOC_QBUF:
		return videobuf_qbuf(&dev->empress_tsq,arg);

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(&dev->empress_tsq,arg,
				      file->f_flags & O_NONBLOCK);

	case VIDIOC_STREAMON:
		return videobuf_streamon(&dev->empress_tsq);

	case VIDIOC_STREAMOFF:
		return videobuf_streamoff(&dev->empress_tsq);

	case VIDIOC_QUERYCTRL:
	case VIDIOC_G_CTRL:
	case VIDIOC_S_CTRL:
		return saa7134_common_ioctl(dev, cmd, arg);

	case VIDIOC_S_MPEGCOMP:
		saa7134_i2c_call_clients(dev, VIDIOC_S_MPEGCOMP, arg);
		ts_init_encoder(dev);
		return 0;
	case VIDIOC_G_MPEGCOMP:
		saa7134_i2c_call_clients(dev, VIDIOC_G_MPEGCOMP, arg);
		return 0;

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int ts_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, ts_do_ioctl);
}

static struct file_operations ts_fops =
{
	.owner	  = THIS_MODULE,
	.open	  = ts_open,
	.release  = ts_release,
	.read	  = ts_read,
	.poll	  = ts_poll,
	.mmap	  = ts_mmap,
	.ioctl	  = ts_ioctl,
	.llseek   = no_llseek,
};

/* ----------------------------------------------------------- */

static struct video_device saa7134_empress_template =
{
	.name          = "saa7134-empress",
	.type          = 0 /* FIXME */,
	.type2         = 0 /* FIXME */,
	.hardware      = 0,
	.fops          = &ts_fops,
	.minor	       = -1,
};

static void empress_signal_update(void* data)
{
	struct saa7134_dev* dev = (struct saa7134_dev*) data;

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

	dprintk("%s: %s\n",dev->name,__FUNCTION__);
	dev->empress_dev = video_device_alloc();
	if (NULL == dev->empress_dev)
		return -ENOMEM;
	*(dev->empress_dev) = saa7134_empress_template;
	dev->empress_dev->dev     = &dev->pci->dev;
	dev->empress_dev->release = video_device_release;
	snprintf(dev->empress_dev->name, sizeof(dev->empress_dev->name),
		 "%s empress (%s)", dev->name,
		 saa7134_boards[dev->board].name);

	INIT_WORK(&dev->empress_workqueue, empress_signal_update, (void*) dev);

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

	videobuf_queue_init(&dev->empress_tsq, &saa7134_ts_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_ALTERNATE,
			    sizeof(struct saa7134_buf),
			    dev);

	empress_signal_update(dev);
	return 0;
}

static int empress_fini(struct saa7134_dev *dev)
{
	dprintk("%s: %s\n",dev->name,__FUNCTION__);

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
