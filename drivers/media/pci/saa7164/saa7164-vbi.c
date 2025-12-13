// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010-2015 Steven Toth <stoth@kernellabs.com>
 */

#include "saa7164.h"

/* Take the encoder configuration from the port struct and
 * flush it to the hardware.
 */
static void saa7164_vbi_configure(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	dprintk(DBGLVL_VBI, "%s()\n", __func__);

	port->vbi_params.width = port->enc_port->width;
	port->vbi_params.height = port->enc_port->height;
	port->vbi_params.is_50hz =
		(port->enc_port->encodernorm.id & V4L2_STD_625_50) != 0;

	/* Set up the DIF (enable it) for analog mode by default */
	saa7164_api_initialize_dif(port);
	dprintk(DBGLVL_VBI, "%s() ends\n", __func__);
}

static int saa7164_vbi_buffers_dealloc(struct saa7164_port *port)
{
	struct list_head *c, *n, *p, *q, *l, *v;
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct saa7164_user_buffer *ubuf;

	/* Remove any allocated buffers */
	mutex_lock(&port->dmaqueue_lock);

	dprintk(DBGLVL_VBI, "%s(port=%d) dmaqueue\n", __func__, port->nr);
	list_for_each_safe(c, n, &port->dmaqueue.list) {
		buf = list_entry(c, struct saa7164_buffer, list);
		list_del(c);
		saa7164_buffer_dealloc(buf);
	}

	dprintk(DBGLVL_VBI, "%s(port=%d) used\n", __func__, port->nr);
	list_for_each_safe(p, q, &port->list_buf_used.list) {
		ubuf = list_entry(p, struct saa7164_user_buffer, list);
		list_del(p);
		saa7164_buffer_dealloc_user(ubuf);
	}

	dprintk(DBGLVL_VBI, "%s(port=%d) free\n", __func__, port->nr);
	list_for_each_safe(l, v, &port->list_buf_free.list) {
		ubuf = list_entry(l, struct saa7164_user_buffer, list);
		list_del(l);
		saa7164_buffer_dealloc_user(ubuf);
	}

	mutex_unlock(&port->dmaqueue_lock);
	dprintk(DBGLVL_VBI, "%s(port=%d) done\n", __func__, port->nr);

	return 0;
}

/* Dynamic buffer switch at vbi start time */
static int saa7164_vbi_buffers_alloc(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct saa7164_user_buffer *ubuf;
	struct tmHWStreamParameters *params = &port->hw_streamingparams;
	int result = -ENODEV, i;
	int len = 0;

	dprintk(DBGLVL_VBI, "%s()\n", __func__);

	/* TODO: NTSC SPECIFIC */
	/* Init and establish defaults */
	params->samplesperline = 1440;
	params->numberoflines = 18;
	params->pitch = 1440;
	params->numpagetables = 2 +
		((params->numberoflines * params->pitch) / PAGE_SIZE);
	params->bitspersample = 8;
	params->linethreshold = 0;
	params->pagetablelistvirt = NULL;
	params->pagetablelistphys = NULL;
	params->numpagetableentries = port->hwcfg.buffercount;

	/* Allocate the PCI resources, buffers (hard) */
	for (i = 0; i < port->hwcfg.buffercount; i++) {
		buf = saa7164_buffer_alloc(port,
			params->numberoflines *
			params->pitch);

		if (!buf) {
			printk(KERN_ERR "%s() failed (errno = %d), unable to allocate buffer\n",
				__func__, result);
			result = -ENOMEM;
			goto failed;
		} else {

			mutex_lock(&port->dmaqueue_lock);
			list_add_tail(&buf->list, &port->dmaqueue.list);
			mutex_unlock(&port->dmaqueue_lock);

		}
	}

	/* Allocate some kernel buffers for copying
	 * to userpsace.
	 */
	len = params->numberoflines * params->pitch;

	if (vbi_buffers < 16)
		vbi_buffers = 16;
	if (vbi_buffers > 512)
		vbi_buffers = 512;

	for (i = 0; i < vbi_buffers; i++) {

		ubuf = saa7164_buffer_alloc_user(dev, len);
		if (ubuf) {
			mutex_lock(&port->dmaqueue_lock);
			list_add_tail(&ubuf->list, &port->list_buf_free.list);
			mutex_unlock(&port->dmaqueue_lock);
		}

	}

	result = 0;

failed:
	return result;
}


static int saa7164_vbi_initialize(struct saa7164_port *port)
{
	saa7164_vbi_configure(port);
	return 0;
}

/* -- V4L2 --------------------------------------------------------- */
static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);

	return saa7164_s_std(fh->port->enc_port, id);
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);

	return saa7164_g_std(fh->port->enc_port, id);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);

	return saa7164_g_input(fh->port->enc_port, i);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);

	return saa7164_s_input(fh->port->enc_port, i);
}

static int vidioc_g_frequency(struct file *file, void *priv,
	struct v4l2_frequency *f)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);

	return saa7164_g_frequency(fh->port->enc_port, f);
}

static int vidioc_s_frequency(struct file *file, void *priv,
	const struct v4l2_frequency *f)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);
	int ret = saa7164_s_frequency(fh->port->enc_port, f);

	if (ret == 0)
		saa7164_vbi_initialize(fh->port);
	return ret;
}

static int vidioc_querycap(struct file *file, void  *priv,
	struct v4l2_capability *cap)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);
	struct saa7164_port *port = fh->port;
	struct saa7164_dev *dev = port->dev;

	strscpy(cap->driver, dev->name, sizeof(cap->driver));
	strscpy(cap->card, saa7164_boards[dev->board].name,
		sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
			    V4L2_CAP_TUNER | V4L2_CAP_VBI_CAPTURE |
			    V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int saa7164_vbi_stop_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() stop transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_VBI, "%s()    Stopped\n", __func__);
		ret = 0;
	}

	return ret;
}

static int saa7164_vbi_acquire_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_ACQUIRE);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() acquire transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_VBI, "%s() Acquired\n", __func__);
		ret = 0;
	}

	return ret;
}

static int saa7164_vbi_pause_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_PAUSE);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() pause transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_VBI, "%s()   Paused\n", __func__);
		ret = 0;
	}

	return ret;
}

/* Firmware is very windows centric, meaning you have to transition
 * the part through AVStream / KS Windows stages, forwards or backwards.
 * States are: stopped, acquired (h/w), paused, started.
 * We have to leave here will all of the soft buffers on the free list,
 * else the cfg_post() func won't have soft buffers to correctly configure.
 */
static int saa7164_vbi_stop_streaming(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct saa7164_user_buffer *ubuf;
	struct list_head *c, *n;
	int ret;

	dprintk(DBGLVL_VBI, "%s(port=%d)\n", __func__, port->nr);

	ret = saa7164_vbi_pause_port(port);
	ret = saa7164_vbi_acquire_port(port);
	ret = saa7164_vbi_stop_port(port);

	dprintk(DBGLVL_VBI, "%s(port=%d) Hardware stopped\n", __func__,
		port->nr);

	/* Reset the state of any allocated buffer resources */
	mutex_lock(&port->dmaqueue_lock);

	/* Reset the hard and soft buffer state */
	list_for_each_safe(c, n, &port->dmaqueue.list) {
		buf = list_entry(c, struct saa7164_buffer, list);
		buf->flags = SAA7164_BUFFER_FREE;
		buf->pos = 0;
	}

	list_for_each_safe(c, n, &port->list_buf_used.list) {
		ubuf = list_entry(c, struct saa7164_user_buffer, list);
		ubuf->pos = 0;
		list_move_tail(&ubuf->list, &port->list_buf_free.list);
	}

	mutex_unlock(&port->dmaqueue_lock);

	/* Free any allocated resources */
	saa7164_vbi_buffers_dealloc(port);

	dprintk(DBGLVL_VBI, "%s(port=%d) Released\n", __func__, port->nr);

	return ret;
}

static int saa7164_vbi_start_streaming(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int result, ret = 0;

	dprintk(DBGLVL_VBI, "%s(port=%d)\n", __func__, port->nr);

	port->done_first_interrupt = 0;

	/* allocate all of the PCIe DMA buffer resources on the fly,
	 * allowing switching between TS and PS payloads without
	 * requiring a complete driver reload.
	 */
	saa7164_vbi_buffers_alloc(port);

	/* Configure the encoder with any cache values */
#if 0
	saa7164_api_set_encoder(port);
	saa7164_api_get_encoder(port);
#endif

	/* Place the empty buffers on the hardware */
	saa7164_buffer_cfg_port(port);

	/* Negotiate format */
	if (saa7164_api_set_vbi_format(port) != SAA_OK) {
		printk(KERN_ERR "%s() No supported VBI format\n", __func__);
		ret = -EIO;
		goto out;
	}

	/* Acquire the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_ACQUIRE);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() acquire transition failed, res = 0x%x\n",
			__func__, result);

		ret = -EIO;
		goto out;
	} else
		dprintk(DBGLVL_VBI, "%s()   Acquired\n", __func__);

	/* Pause the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_PAUSE);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() pause transition failed, res = 0x%x\n",
				__func__, result);

		/* Stop the hardware, regardless */
		result = saa7164_vbi_stop_port(port);
		if (result != SAA_OK) {
			printk(KERN_ERR "%s() pause/forced stop transition failed, res = 0x%x\n",
			       __func__, result);
		}

		ret = -EIO;
		goto out;
	} else
		dprintk(DBGLVL_VBI, "%s()   Paused\n", __func__);

	/* Start the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_RUN);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() run transition failed, result = 0x%x\n",
				__func__, result);

		/* Stop the hardware, regardless */
		result = saa7164_vbi_acquire_port(port);
		result = saa7164_vbi_stop_port(port);
		if (result != SAA_OK) {
			printk(KERN_ERR "%s() run/forced stop transition failed, res = 0x%x\n",
			       __func__, result);
		}

		ret = -EIO;
	} else
		dprintk(DBGLVL_VBI, "%s()   Running\n", __func__);

out:
	return ret;
}

static int saa7164_vbi_fmt(struct file *file, void *priv,
			   struct v4l2_format *f)
{
	/* ntsc */
	f->fmt.vbi.samples_per_line = 1440;
	f->fmt.vbi.sampling_rate = 27000000;
	f->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset = 0;
	f->fmt.vbi.flags = 0;
	f->fmt.vbi.start[0] = 10;
	f->fmt.vbi.count[0] = 18;
	f->fmt.vbi.start[1] = 263 + 10 + 1;
	f->fmt.vbi.count[1] = 18;
	memset(f->fmt.vbi.reserved, 0, sizeof(f->fmt.vbi.reserved));
	return 0;
}

static int fops_open(struct file *file)
{
	struct saa7164_dev *dev;
	struct saa7164_port *port;
	struct saa7164_vbi_fh *fh;

	port = (struct saa7164_port *)video_get_drvdata(video_devdata(file));
	if (!port)
		return -ENODEV;

	dev = port->dev;

	dprintk(DBGLVL_VBI, "%s()\n", __func__);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;

	fh->port = port;
	v4l2_fh_init(&fh->fh, video_devdata(file));
	v4l2_fh_add(&fh->fh, file);

	return 0;
}

static int fops_release(struct file *file)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);
	struct saa7164_port *port = fh->port;
	struct saa7164_dev *dev = port->dev;

	dprintk(DBGLVL_VBI, "%s()\n", __func__);

	/* Shut device down on last close */
	if (atomic_cmpxchg(&fh->v4l_reading, 1, 0) == 1) {
		if (atomic_dec_return(&port->v4l_reader_count) == 0) {
			/* stop vbi capture then cancel buffers */
			saa7164_vbi_stop_streaming(port);
		}
	}

	v4l2_fh_del(&fh->fh, file);
	v4l2_fh_exit(&fh->fh);
	kfree(fh);

	return 0;
}

static struct
saa7164_user_buffer *saa7164_vbi_next_buf(struct saa7164_port *port)
{
	struct saa7164_user_buffer *ubuf = NULL;
	struct saa7164_dev *dev = port->dev;
	u32 crc;

	mutex_lock(&port->dmaqueue_lock);
	if (!list_empty(&port->list_buf_used.list)) {
		ubuf = list_first_entry(&port->list_buf_used.list,
			struct saa7164_user_buffer, list);

		if (crc_checking) {
			crc = crc32(0, ubuf->data, ubuf->actual_size);
			if (crc != ubuf->crc) {
				printk(KERN_ERR "%s() ubuf %p crc became invalid, was 0x%x became 0x%x\n",
					__func__,
					ubuf, ubuf->crc, crc);
			}
		}

	}
	mutex_unlock(&port->dmaqueue_lock);

	dprintk(DBGLVL_VBI, "%s() returns %p\n", __func__, ubuf);

	return ubuf;
}

static ssize_t fops_read(struct file *file, char __user *buffer,
	size_t count, loff_t *pos)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);
	struct saa7164_port *port = fh->port;
	struct saa7164_user_buffer *ubuf = NULL;
	struct saa7164_dev *dev = port->dev;
	int ret = 0;
	int rem, cnt;
	u8 *p;

	port->last_read_msecs_diff = port->last_read_msecs;
	port->last_read_msecs = jiffies_to_msecs(jiffies);
	port->last_read_msecs_diff = port->last_read_msecs -
		port->last_read_msecs_diff;

	saa7164_histogram_update(&port->read_interval,
		port->last_read_msecs_diff);

	if (*pos) {
		printk(KERN_ERR "%s() ESPIPE\n", __func__);
		return -ESPIPE;
	}

	if (atomic_cmpxchg(&fh->v4l_reading, 0, 1) == 0) {
		if (atomic_inc_return(&port->v4l_reader_count) == 1) {

			if (saa7164_vbi_initialize(port) < 0) {
				printk(KERN_ERR "%s() EINVAL\n", __func__);
				return -EINVAL;
			}

			saa7164_vbi_start_streaming(port);
			msleep(200);
		}
	}

	/* blocking wait for buffer */
	if ((file->f_flags & O_NONBLOCK) == 0) {
		if (wait_event_interruptible(port->wait_read,
			saa7164_vbi_next_buf(port))) {
				printk(KERN_ERR "%s() ERESTARTSYS\n", __func__);
				return -ERESTARTSYS;
		}
	}

	/* Pull the first buffer from the used list */
	ubuf = saa7164_vbi_next_buf(port);

	while ((count > 0) && ubuf) {

		/* set remaining bytes to copy */
		rem = ubuf->actual_size - ubuf->pos;
		cnt = rem > count ? count : rem;

		p = ubuf->data + ubuf->pos;

		dprintk(DBGLVL_VBI,
			"%s() count=%d cnt=%d rem=%d buf=%p buf->pos=%d\n",
			__func__, (int)count, cnt, rem, ubuf, ubuf->pos);

		if (copy_to_user(buffer, p, cnt)) {
			printk(KERN_ERR "%s() copy_to_user failed\n", __func__);
			if (!ret) {
				printk(KERN_ERR "%s() EFAULT\n", __func__);
				ret = -EFAULT;
			}
			goto err;
		}

		ubuf->pos += cnt;
		count -= cnt;
		buffer += cnt;
		ret += cnt;

		if (ubuf->pos > ubuf->actual_size)
			printk(KERN_ERR "read() pos > actual, huh?\n");

		if (ubuf->pos == ubuf->actual_size) {

			/* finished with current buffer, take next buffer */

			/* Requeue the buffer on the free list */
			ubuf->pos = 0;

			mutex_lock(&port->dmaqueue_lock);
			list_move_tail(&ubuf->list, &port->list_buf_free.list);
			mutex_unlock(&port->dmaqueue_lock);

			/* Dequeue next */
			if ((file->f_flags & O_NONBLOCK) == 0) {
				if (wait_event_interruptible(port->wait_read,
					saa7164_vbi_next_buf(port))) {
						break;
				}
			}
			ubuf = saa7164_vbi_next_buf(port);
		}
	}
err:
	if (!ret && !ubuf) {
		printk(KERN_ERR "%s() EAGAIN\n", __func__);
		ret = -EAGAIN;
	}

	return ret;
}

static __poll_t fops_poll(struct file *file, poll_table *wait)
{
	struct saa7164_vbi_fh *fh = to_saa7164_vbi_fh(file);
	struct saa7164_port *port = fh->port;
	__poll_t mask = 0;

	port->last_poll_msecs_diff = port->last_poll_msecs;
	port->last_poll_msecs = jiffies_to_msecs(jiffies);
	port->last_poll_msecs_diff = port->last_poll_msecs -
		port->last_poll_msecs_diff;

	saa7164_histogram_update(&port->poll_interval,
		port->last_poll_msecs_diff);

	if (!video_is_registered(port->v4l_device))
		return EPOLLERR;

	if (atomic_cmpxchg(&fh->v4l_reading, 0, 1) == 0) {
		if (atomic_inc_return(&port->v4l_reader_count) == 1) {
			if (saa7164_vbi_initialize(port) < 0)
				return EPOLLERR;
			saa7164_vbi_start_streaming(port);
			msleep(200);
		}
	}

	/* blocking wait for buffer */
	if ((file->f_flags & O_NONBLOCK) == 0) {
		if (wait_event_interruptible(port->wait_read,
			saa7164_vbi_next_buf(port))) {
				return EPOLLERR;
		}
	}

	/* Pull the first buffer from the used list */
	if (!list_empty(&port->list_buf_used.list))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}
static const struct v4l2_file_operations vbi_fops = {
	.owner		= THIS_MODULE,
	.open		= fops_open,
	.release	= fops_release,
	.read		= fops_read,
	.poll		= fops_poll,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops vbi_ioctl_ops = {
	.vidioc_s_std		 = vidioc_s_std,
	.vidioc_g_std		 = vidioc_g_std,
	.vidioc_enum_input	 = saa7164_enum_input,
	.vidioc_g_input		 = vidioc_g_input,
	.vidioc_s_input		 = vidioc_s_input,
	.vidioc_g_tuner		 = saa7164_g_tuner,
	.vidioc_s_tuner		 = saa7164_s_tuner,
	.vidioc_g_frequency	 = vidioc_g_frequency,
	.vidioc_s_frequency	 = vidioc_s_frequency,
	.vidioc_querycap	 = vidioc_querycap,
	.vidioc_g_fmt_vbi_cap	 = saa7164_vbi_fmt,
	.vidioc_try_fmt_vbi_cap	 = saa7164_vbi_fmt,
	.vidioc_s_fmt_vbi_cap	 = saa7164_vbi_fmt,
};

static struct video_device saa7164_vbi_template = {
	.name          = "saa7164",
	.fops          = &vbi_fops,
	.ioctl_ops     = &vbi_ioctl_ops,
	.minor         = -1,
	.tvnorms       = SAA7164_NORMS,
	.device_caps   = V4L2_CAP_VBI_CAPTURE | V4L2_CAP_READWRITE |
			 V4L2_CAP_TUNER,
};

static struct video_device *saa7164_vbi_alloc(
	struct saa7164_port *port,
	struct pci_dev *pci,
	struct video_device *template,
	char *type)
{
	struct video_device *vfd;
	struct saa7164_dev *dev = port->dev;

	dprintk(DBGLVL_VBI, "%s()\n", __func__);

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;

	*vfd = *template;
	snprintf(vfd->name, sizeof(vfd->name), "%s %s (%s)", dev->name,
		type, saa7164_boards[dev->board].name);

	vfd->v4l2_dev  = &dev->v4l2_dev;
	vfd->release = video_device_release;
	return vfd;
}

int saa7164_vbi_register(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int result = -ENODEV;

	dprintk(DBGLVL_VBI, "%s()\n", __func__);

	BUG_ON(port->type != SAA7164_MPEG_VBI);

	/* Sanity check that the PCI configuration space is active */
	if (port->hwcfg.BARLocation == 0) {
		printk(KERN_ERR "%s() failed (errno = %d), NO PCI configuration\n",
			__func__, result);
		result = -ENOMEM;
		goto failed;
	}

	/* Establish VBI defaults here */

	/* Allocate and register the video device node */
	port->v4l_device = saa7164_vbi_alloc(port,
		dev->pci, &saa7164_vbi_template, "vbi");

	if (!port->v4l_device) {
		printk(KERN_INFO "%s: can't allocate vbi device\n",
			dev->name);
		result = -ENOMEM;
		goto failed;
	}

	port->enc_port = &dev->ports[port->nr - 2];
	video_set_drvdata(port->v4l_device, port);
	result = video_register_device(port->v4l_device,
		VFL_TYPE_VBI, -1);
	if (result < 0) {
		printk(KERN_INFO "%s: can't register vbi device\n",
			dev->name);
		/* TODO: We're going to leak here if we don't dealloc
		 The buffers above. The unreg function can't deal wit it.
		*/
		goto failed;
	}

	printk(KERN_INFO "%s: registered device vbi%d [vbi]\n",
		dev->name, port->v4l_device->num);

	/* Configure the hardware defaults */

	result = 0;
failed:
	return result;
}

void saa7164_vbi_unregister(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;

	dprintk(DBGLVL_VBI, "%s(port=%d)\n", __func__, port->nr);

	BUG_ON(port->type != SAA7164_MPEG_VBI);

	if (port->v4l_device) {
		if (port->v4l_device->minor != -1)
			video_unregister_device(port->v4l_device);
		else
			video_device_release(port->v4l_device);

		port->v4l_device = NULL;
	}

}
