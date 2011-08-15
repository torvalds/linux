/* radio-cadet.c - A video4linux driver for the ADS Cadet AM/FM Radio Card
 *
 * by Fred Gleason <fredg@wava.com>
 * Version 0.3.3
 *
 * (Loosely) based on code for the Aztech radio card by
 *
 * Russell Kroll    (rkroll@exploits.org)
 * Quay Ly
 * Donald Song
 * Jason Lewis      (jlewis@twilight.vtc.vsc.edu)
 * Scott McGrath    (smcgrath@twilight.vtc.vsc.edu)
 * William McGrath  (wmcgrath@twilight.vtc.vsc.edu)
 *
 * History:
 * 2000-04-29	Russell Kroll <rkroll@exploits.org>
 *		Added ISAPnP detection for Linux 2.3/2.4
 *
 * 2001-01-10	Russell Kroll <rkroll@exploits.org>
 *		Removed dead CONFIG_RADIO_CADET_PORT code
 *		PnP detection on load is now default (no args necessary)
 *
 * 2002-01-17	Adam Belay <ambx1@neo.rr.com>
 *		Updated to latest pnp code
 *
 * 2003-01-31	Alan Cox <alan@lxorguk.ukuu.org.uk>
 *		Cleaned up locking, delay code, general odds and ends
 *
 * 2006-07-30	Hans J. Koch <koch@hjk-az.de>
 *		Changed API to V4L2
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <linux/videodev2.h>	/* V4L2 API defs		*/
#include <linux/param.h>
#include <linux/pnp.h>
#include <linux/sched.h>
#include <linux/io.h>		/* outb, outb_p			*/
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

MODULE_AUTHOR("Fred Gleason, Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the ADS Cadet AM/FM/RDS radio card.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.3.4");

static int io = -1;		/* default to isapnp activation */
static int radio_nr = -1;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of Cadet card (0x330,0x332,0x334,0x336,0x338,0x33a,0x33c,0x33e)");
module_param(radio_nr, int, 0);

#define RDS_BUFFER 256
#define RDS_RX_FLAG 1
#define MBS_RX_FLAG 2

struct cadet {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	int io;
	int users;
	int curtuner;
	int tunestat;
	int sigstrength;
	wait_queue_head_t read_queue;
	struct timer_list readtimer;
	__u8 rdsin, rdsout, rdsstat;
	unsigned char rdsbuf[RDS_BUFFER];
	struct mutex lock;
	int reading;
};

static struct cadet cadet_card;

/*
 * Signal Strength Threshold Values
 * The V4L API spec does not define any particular unit for the signal
 * strength value.  These values are in microvolts of RF at the tuner's input.
 */
static __u16 sigtable[2][4] = {
	{  5, 10, 30,  150 },
	{ 28, 40, 63, 1000 }
};


static int cadet_getstereo(struct cadet *dev)
{
	int ret = V4L2_TUNER_SUB_MONO;

	if (dev->curtuner != 0)	/* Only FM has stereo capability! */
		return V4L2_TUNER_SUB_MONO;

	mutex_lock(&dev->lock);
	outb(7, dev->io);          /* Select tuner control */
	if ((inb(dev->io + 1) & 0x40) == 0)
		ret = V4L2_TUNER_SUB_STEREO;
	mutex_unlock(&dev->lock);
	return ret;
}

static unsigned cadet_gettune(struct cadet *dev)
{
	int curvol, i;
	unsigned fifo = 0;

	/*
	 * Prepare for read
	 */

	mutex_lock(&dev->lock);

	outb(7, dev->io);       /* Select tuner control */
	curvol = inb(dev->io + 1); /* Save current volume/mute setting */
	outb(0x00, dev->io + 1);  /* Ensure WRITE-ENABLE is LOW */
	dev->tunestat = 0xffff;

	/*
	 * Read the shift register
	 */
	for (i = 0; i < 25; i++) {
		fifo = (fifo << 1) | ((inb(dev->io + 1) >> 7) & 0x01);
		if (i < 24) {
			outb(0x01, dev->io + 1);
			dev->tunestat &= inb(dev->io + 1);
			outb(0x00, dev->io + 1);
		}
	}

	/*
	 * Restore volume/mute setting
	 */
	outb(curvol, dev->io + 1);
	mutex_unlock(&dev->lock);

	return fifo;
}

static unsigned cadet_getfreq(struct cadet *dev)
{
	int i;
	unsigned freq = 0, test, fifo = 0;

	/*
	 * Read current tuning
	 */
	fifo = cadet_gettune(dev);

	/*
	 * Convert to actual frequency
	 */
	if (dev->curtuner == 0) {    /* FM */
		test = 12500;
		for (i = 0; i < 14; i++) {
			if ((fifo & 0x01) != 0)
				freq += test;
			test = test << 1;
			fifo = fifo >> 1;
		}
		freq -= 10700000;           /* IF frequency is 10.7 MHz */
		freq = (freq * 16) / 1000000;   /* Make it 1/16 MHz */
	}
	if (dev->curtuner == 1)    /* AM */
		freq = ((fifo & 0x7fff) - 2010) * 16;

	return freq;
}

static void cadet_settune(struct cadet *dev, unsigned fifo)
{
	int i;
	unsigned test;

	mutex_lock(&dev->lock);

	outb(7, dev->io);                /* Select tuner control */
	/*
	 * Write the shift register
	 */
	test = 0;
	test = (fifo >> 23) & 0x02;      /* Align data for SDO */
	test |= 0x1c;                /* SDM=1, SWE=1, SEN=1, SCK=0 */
	outb(7, dev->io);                /* Select tuner control */
	outb(test, dev->io + 1);           /* Initialize for write */
	for (i = 0; i < 25; i++) {
		test |= 0x01;              /* Toggle SCK High */
		outb(test, dev->io + 1);
		test &= 0xfe;              /* Toggle SCK Low */
		outb(test, dev->io + 1);
		fifo = fifo << 1;            /* Prepare the next bit */
		test = 0x1c | ((fifo >> 23) & 0x02);
		outb(test, dev->io + 1);
	}
	mutex_unlock(&dev->lock);
}

static void cadet_setfreq(struct cadet *dev, unsigned freq)
{
	unsigned fifo;
	int i, j, test;
	int curvol;

	/*
	 * Formulate a fifo command
	 */
	fifo = 0;
	if (dev->curtuner == 0) {    /* FM */
		test = 102400;
		freq = (freq * 1000) / 16;       /* Make it kHz */
		freq += 10700;               /* IF is 10700 kHz */
		for (i = 0; i < 14; i++) {
			fifo = fifo << 1;
			if (freq >= test) {
				fifo |= 0x01;
				freq -= test;
			}
			test = test >> 1;
		}
	}
	if (dev->curtuner == 1) {    /* AM */
		fifo = (freq / 16) + 2010;            /* Make it kHz */
		fifo |= 0x100000;            /* Select AM Band */
	}

	/*
	 * Save current volume/mute setting
	 */

	mutex_lock(&dev->lock);
	outb(7, dev->io);                /* Select tuner control */
	curvol = inb(dev->io + 1);
	mutex_unlock(&dev->lock);

	/*
	 * Tune the card
	 */
	for (j = 3; j > -1; j--) {
		cadet_settune(dev, fifo | (j << 16));

		mutex_lock(&dev->lock);
		outb(7, dev->io);         /* Select tuner control */
		outb(curvol, dev->io + 1);
		mutex_unlock(&dev->lock);

		msleep(100);

		cadet_gettune(dev);
		if ((dev->tunestat & 0x40) == 0) {   /* Tuned */
			dev->sigstrength = sigtable[dev->curtuner][j];
			return;
		}
	}
	dev->sigstrength = 0;
}


static int cadet_getvol(struct cadet *dev)
{
	int ret = 0;

	mutex_lock(&dev->lock);

	outb(7, dev->io);                /* Select tuner control */
	if ((inb(dev->io + 1) & 0x20) != 0)
		ret = 0xffff;

	mutex_unlock(&dev->lock);
	return ret;
}


static void cadet_setvol(struct cadet *dev, int vol)
{
	mutex_lock(&dev->lock);
	outb(7, dev->io);                /* Select tuner control */
	if (vol > 0)
		outb(0x20, dev->io + 1);
	else
		outb(0x00, dev->io + 1);
	mutex_unlock(&dev->lock);
}

static void cadet_handler(unsigned long data)
{
	struct cadet *dev = (void *)data;

	/* Service the RDS fifo */
	if (mutex_trylock(&dev->lock)) {
		outb(0x3, dev->io);       /* Select RDS Decoder Control */
		if ((inb(dev->io + 1) & 0x20) != 0)
			printk(KERN_CRIT "cadet: RDS fifo overflow\n");
		outb(0x80, dev->io);      /* Select RDS fifo */
		while ((inb(dev->io) & 0x80) != 0) {
			dev->rdsbuf[dev->rdsin] = inb(dev->io + 1);
			if (dev->rdsin == dev->rdsout)
				printk(KERN_WARNING "cadet: RDS buffer overflow\n");
			else
				dev->rdsin++;
		}
		mutex_unlock(&dev->lock);
	}

	/*
	 * Service pending read
	 */
	if (dev->rdsin != dev->rdsout)
		wake_up_interruptible(&dev->read_queue);

	/*
	 * Clean up and exit
	 */
	init_timer(&dev->readtimer);
	dev->readtimer.function = cadet_handler;
	dev->readtimer.data = (unsigned long)0;
	dev->readtimer.expires = jiffies + msecs_to_jiffies(50);
	add_timer(&dev->readtimer);
}


static ssize_t cadet_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct cadet *dev = video_drvdata(file);
	unsigned char readbuf[RDS_BUFFER];
	int i = 0;

	mutex_lock(&dev->lock);
	if (dev->rdsstat == 0) {
		dev->rdsstat = 1;
		outb(0x80, dev->io);        /* Select RDS fifo */
		init_timer(&dev->readtimer);
		dev->readtimer.function = cadet_handler;
		dev->readtimer.data = (unsigned long)dev;
		dev->readtimer.expires = jiffies + msecs_to_jiffies(50);
		add_timer(&dev->readtimer);
	}
	if (dev->rdsin == dev->rdsout) {
		mutex_unlock(&dev->lock);
		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;
		interruptible_sleep_on(&dev->read_queue);
		mutex_lock(&dev->lock);
	}
	while (i < count && dev->rdsin != dev->rdsout)
		readbuf[i++] = dev->rdsbuf[dev->rdsout++];
	mutex_unlock(&dev->lock);

	if (copy_to_user(data, readbuf, i))
		return -EFAULT;
	return i;
}


static int vidioc_querycap(struct file *file, void *priv,
				struct v4l2_capability *v)
{
	strlcpy(v->driver, "ADS Cadet", sizeof(v->driver));
	strlcpy(v->card, "ADS Cadet", sizeof(v->card));
	strlcpy(v->bus_info, "ISA", sizeof(v->bus_info));
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO |
			  V4L2_CAP_READWRITE | V4L2_CAP_RDS_CAPTURE;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct cadet *dev = video_drvdata(file);

	v->type = V4L2_TUNER_RADIO;
	switch (v->index) {
	case 0:
		strlcpy(v->name, "FM", sizeof(v->name));
		v->capability = V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_RDS |
			V4L2_TUNER_CAP_RDS_BLOCK_IO;
		v->rangelow = 1400;     /* 87.5 MHz */
		v->rangehigh = 1728;    /* 108.0 MHz */
		v->rxsubchans = cadet_getstereo(dev);
		switch (v->rxsubchans) {
		case V4L2_TUNER_SUB_MONO:
			v->audmode = V4L2_TUNER_MODE_MONO;
			break;
		case V4L2_TUNER_SUB_STEREO:
			v->audmode = V4L2_TUNER_MODE_STEREO;
			break;
		default:
			break;
		}
		v->rxsubchans |= V4L2_TUNER_SUB_RDS;
		break;
	case 1:
		strlcpy(v->name, "AM", sizeof(v->name));
		v->capability = V4L2_TUNER_CAP_LOW;
		v->rangelow = 8320;      /* 520 kHz */
		v->rangehigh = 26400;    /* 1650 kHz */
		v->rxsubchans = V4L2_TUNER_SUB_MONO;
		v->audmode = V4L2_TUNER_MODE_MONO;
		break;
	default:
		return -EINVAL;
	}
	v->signal = dev->sigstrength; /* We might need to modify scaling of this */
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct cadet *dev = video_drvdata(file);

	if (v->index != 0 && v->index != 1)
		return -EINVAL;
	dev->curtuner = v->index;
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct cadet *dev = video_drvdata(file);

	f->tuner = dev->curtuner;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = cadet_getfreq(dev);
	return 0;
}


static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct cadet *dev = video_drvdata(file);

	if (f->type != V4L2_TUNER_RADIO)
		return -EINVAL;
	if (dev->curtuner == 0 && (f->frequency < 1400 || f->frequency > 1728))
		return -EINVAL;
	if (dev->curtuner == 1 && (f->frequency < 8320 || f->frequency > 26400))
		return -EINVAL;
	cadet_setfreq(dev, f->frequency);
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
	case V4L2_CID_AUDIO_VOLUME:
		return v4l2_ctrl_query_fill(qc, 0, 0xff, 1, 0xff);
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct cadet *dev = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE: /* TODO: Handle this correctly */
		ctrl->value = (cadet_getvol(dev) == 0);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = cadet_getvol(dev);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct cadet *dev = video_drvdata(file);

	switch (ctrl->id){
	case V4L2_CID_AUDIO_MUTE: /* TODO: Handle this correctly */
		if (ctrl->value)
			cadet_setvol(dev, 0);
		else
			cadet_setvol(dev, 0xffff);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		cadet_setvol(dev, ctrl->value);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int vidioc_g_audio(struct file *file, void *priv,
				struct v4l2_audio *a)
{
	a->index = 0;
	strlcpy(a->name, "Radio", sizeof(a->name));
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv,
				struct v4l2_audio *a)
{
	return a->index ? -EINVAL : 0;
}

static int cadet_open(struct file *file)
{
	struct cadet *dev = video_drvdata(file);

	mutex_lock(&dev->lock);
	dev->users++;
	if (1 == dev->users)
		init_waitqueue_head(&dev->read_queue);
	mutex_unlock(&dev->lock);
	return 0;
}

static int cadet_release(struct file *file)
{
	struct cadet *dev = video_drvdata(file);

	mutex_lock(&dev->lock);
	dev->users--;
	if (0 == dev->users) {
		del_timer_sync(&dev->readtimer);
		dev->rdsstat = 0;
	}
	mutex_unlock(&dev->lock);
	return 0;
}

static unsigned int cadet_poll(struct file *file, struct poll_table_struct *wait)
{
	struct cadet *dev = video_drvdata(file);

	poll_wait(file, &dev->read_queue, wait);
	if (dev->rdsin != dev->rdsout)
		return POLLIN | POLLRDNORM;
	return 0;
}


static const struct v4l2_file_operations cadet_fops = {
	.owner		= THIS_MODULE,
	.open		= cadet_open,
	.release       	= cadet_release,
	.read		= cadet_read,
	.unlocked_ioctl	= video_ioctl2,
	.poll		= cadet_poll,
};

static const struct v4l2_ioctl_ops cadet_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
};

#ifdef CONFIG_PNP

static struct pnp_device_id cadet_pnp_devices[] = {
	/* ADS Cadet AM/FM Radio Card */
	{.id = "MSM0c24", .driver_data = 0},
	{.id = ""}
};

MODULE_DEVICE_TABLE(pnp, cadet_pnp_devices);

static int cadet_pnp_probe(struct pnp_dev *dev, const struct pnp_device_id *dev_id)
{
	if (!dev)
		return -ENODEV;
	/* only support one device */
	if (io > 0)
		return -EBUSY;

	if (!pnp_port_valid(dev, 0))
		return -ENODEV;

	io = pnp_port_start(dev, 0);

	printk(KERN_INFO "radio-cadet: PnP reports device at %#x\n", io);

	return io;
}

static struct pnp_driver cadet_pnp_driver = {
	.name		= "radio-cadet",
	.id_table	= cadet_pnp_devices,
	.probe		= cadet_pnp_probe,
	.remove		= NULL,
};

#else
static struct pnp_driver cadet_pnp_driver;
#endif

static void cadet_probe(struct cadet *dev)
{
	static int iovals[8] = { 0x330, 0x332, 0x334, 0x336, 0x338, 0x33a, 0x33c, 0x33e };
	int i;

	for (i = 0; i < 8; i++) {
		dev->io = iovals[i];
		if (request_region(dev->io, 2, "cadet-probe")) {
			cadet_setfreq(dev, 1410);
			if (cadet_getfreq(dev) == 1410) {
				release_region(dev->io, 2);
				return;
			}
			release_region(dev->io, 2);
		}
	}
	dev->io = -1;
}

/*
 * io should only be set if the user has used something like
 * isapnp (the userspace program) to initialize this card for us
 */

static int __init cadet_init(void)
{
	struct cadet *dev = &cadet_card;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int res;

	strlcpy(v4l2_dev->name, "cadet", sizeof(v4l2_dev->name));
	mutex_init(&dev->lock);

	/* If a probe was requested then probe ISAPnP first (safest) */
	if (io < 0)
		pnp_register_driver(&cadet_pnp_driver);
	dev->io = io;

	/* If that fails then probe unsafely if probe is requested */
	if (dev->io < 0)
		cadet_probe(dev);

	/* Else we bail out */
	if (dev->io < 0) {
#ifdef MODULE
		v4l2_err(v4l2_dev, "you must set an I/O address with io=0x330, 0x332, 0x334,\n");
		v4l2_err(v4l2_dev, "0x336, 0x338, 0x33a, 0x33c or 0x33e\n");
#endif
		goto fail;
	}
	if (!request_region(dev->io, 2, "cadet"))
		goto fail;

	res = v4l2_device_register(NULL, v4l2_dev);
	if (res < 0) {
		release_region(dev->io, 2);
		v4l2_err(v4l2_dev, "could not register v4l2_device\n");
		goto fail;
	}

	strlcpy(dev->vdev.name, v4l2_dev->name, sizeof(dev->vdev.name));
	dev->vdev.v4l2_dev = v4l2_dev;
	dev->vdev.fops = &cadet_fops;
	dev->vdev.ioctl_ops = &cadet_ioctl_ops;
	dev->vdev.release = video_device_release_empty;
	video_set_drvdata(&dev->vdev, dev);

	if (video_register_device(&dev->vdev, VFL_TYPE_RADIO, radio_nr) < 0) {
		v4l2_device_unregister(v4l2_dev);
		release_region(dev->io, 2);
		goto fail;
	}
	v4l2_info(v4l2_dev, "ADS Cadet Radio Card at 0x%x\n", dev->io);
	return 0;
fail:
	pnp_unregister_driver(&cadet_pnp_driver);
	return -ENODEV;
}

static void __exit cadet_exit(void)
{
	struct cadet *dev = &cadet_card;

	video_unregister_device(&dev->vdev);
	v4l2_device_unregister(&dev->v4l2_dev);
	release_region(dev->io, 2);
	pnp_unregister_driver(&cadet_pnp_driver);
}

module_init(cadet_init);
module_exit(cadet_exit);

