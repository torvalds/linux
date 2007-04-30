/* GemTek radio card driver for Linux (C) 1998 Jonas Munsin <jmunsin@iki.fi>
 *
 * GemTek hasn't released any specs on the card, so the protocol had to
 * be reverse engineered with dosemu.
 *
 * Besides the protocol changes, this is mostly a copy of:
 *
 *    RadioTrack II driver for Linux radio support (C) 1998 Ben Pfaff
 *
 *    Based on RadioTrack I/RadioReveal (C) 1997 M. Kirkwood
 *    Converted to new API by Alan Cox <Alan.Cox@linux.org>
 *    Various bugfixes and enhancements by Russell Kroll <rkroll@exploits.org>
 *
 * TODO: Allow for more than one of these foolish entities :-)
 *
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <media/v4l2-common.h>
#include <linux/spinlock.h>

#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#define RADIO_VERSION KERNEL_VERSION(0,0,2)

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535,
		.default_value = 0xff,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}
};

#ifndef CONFIG_RADIO_GEMTEK_PORT
#define CONFIG_RADIO_GEMTEK_PORT -1
#endif

static int io = CONFIG_RADIO_GEMTEK_PORT;
static int radio_nr = -1;
static spinlock_t lock;

struct gemtek_device
{
	int port;
	unsigned long curfreq;
	int muted;
};


/* local things */

/* the correct way to mute the gemtek may be to write the last written
 * frequency || 0x10, but just writing 0x10 once seems to do it as well
 */
static void gemtek_mute(struct gemtek_device *dev)
{
	if(dev->muted)
		return;
	spin_lock(&lock);
	outb(0x10, io);
	spin_unlock(&lock);
	dev->muted = 1;
}

static void gemtek_unmute(struct gemtek_device *dev)
{
	if(dev->muted == 0)
		return;
	spin_lock(&lock);
	outb(0x20, io);
	spin_unlock(&lock);
	dev->muted = 0;
}

static void zero(void)
{
	outb_p(0x04, io);
	udelay(5);
	outb_p(0x05, io);
	udelay(5);
}

static void one(void)
{
	outb_p(0x06, io);
	udelay(5);
	outb_p(0x07, io);
	udelay(5);
}

static int gemtek_setfreq(struct gemtek_device *dev, unsigned long freq)
{
	int i;

/*        freq = 78.25*((float)freq/16000.0 + 10.52); */

	freq /= 16;
	freq += 10520;
	freq *= 7825;
	freq /= 100000;

	spin_lock(&lock);

	/* 2 start bits */
	outb_p(0x03, io);
	udelay(5);
	outb_p(0x07, io);
	udelay(5);

	/* 28 frequency bits (lsb first) */
	for (i = 0; i < 14; i++)
		if (freq & (1 << i))
			one();
		else
			zero();
	/* 36 unknown bits */
	for (i = 0; i < 11; i++)
		zero();
	one();
	for (i = 0; i < 4; i++)
		zero();
	one();
	zero();

	/* 2 end bits */
	outb_p(0x03, io);
	udelay(5);
	outb_p(0x07, io);
	udelay(5);

	spin_unlock(&lock);

	return 0;
}

static int gemtek_getsigstr(struct gemtek_device *dev)
{
	spin_lock(&lock);
	inb(io);
	udelay(5);
	spin_unlock(&lock);
	if (inb(io) & 8)		/* bit set = no signal present */
		return 0;
	return 1;		/* signal present */
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-gemtek", sizeof(v->driver));
	strlcpy(v->card, "GemTek", sizeof(v->card));
	sprintf(v->bus_info, "ISA");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct video_device *dev = video_devdata(file);
	struct gemtek_device *rt = dev->priv;

	if (v->index > 0)
		return -EINVAL;

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = (87*16000);
	v->rangehigh = (108*16000);
	v->rxsubchans = V4L2_TUNER_SUB_MONO;
	v->capability = V4L2_TUNER_CAP_LOW;
	v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xffff*gemtek_getsigstr(rt);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	if (v->index > 0)
		return -EINVAL;
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct video_device *dev = video_devdata(file);
	struct gemtek_device *rt = dev->priv;

	rt->curfreq = f->frequency;
	/* needs to be called twice in order for getsigstr to work */
	gemtek_setfreq(rt, rt->curfreq);
	gemtek_setfreq(rt, rt->curfreq);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct video_device *dev = video_devdata(file);
	struct gemtek_device *rt = dev->priv;

	f->type = V4L2_TUNER_RADIO;
	f->frequency = rt->curfreq;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
		if (qc->id && qc->id == radio_qctrl[i].id) {
			memcpy(qc, &(radio_qctrl[i]),
						sizeof(*qc));
			return 0;
		}
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct gemtek_device *rt = dev->priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = rt->muted;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		if (rt->muted)
			ctrl->value = 0;
		else
			ctrl->value = 65535;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct gemtek_device *rt = dev->priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			gemtek_mute(rt);
		else
			gemtek_unmute(rt);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		if (ctrl->value)
			gemtek_unmute(rt);
		else
			gemtek_mute(rt);
		return 0;
	}
	return -EINVAL;
}

static int vidioc_g_audio (struct file *file, void *priv,
					struct v4l2_audio *a)
{
	if (a->index > 1)
		return -EINVAL;

	strcpy(a->name, "Radio");
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	if (a->index != 0)
		return -EINVAL;
	return 0;
}

static struct gemtek_device gemtek_unit;

static const struct file_operations gemtek_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= video_ioctl2,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device gemtek_radio=
{
	.owner		= THIS_MODULE,
	.name		= "GemTek radio",
	.type		= VID_TYPE_TUNER,
	.hardware	= 0,
	.fops           = &gemtek_fops,
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
};

static int __init gemtek_init(void)
{
	if(io==-1)
	{
		printk(KERN_ERR "You must set an I/O address with io=0x20c, io=0x30c, io=0x24c or io=0x34c (io=0x020c or io=0x248 for the combined sound/radiocard)\n");
		return -EINVAL;
	}

	if (!request_region(io, 4, "gemtek"))
	{
		printk(KERN_ERR "gemtek: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	gemtek_radio.priv=&gemtek_unit;

	if(video_register_device(&gemtek_radio, VFL_TYPE_RADIO, radio_nr)==-1)
	{
		release_region(io, 4);
		return -EINVAL;
	}
	printk(KERN_INFO "GemTek Radio Card driver.\n");

	spin_lock_init(&lock);

	/* this is _maybe_ unnecessary */
	outb(0x01, io);

	/* mute card - prevents noisy bootups */
	gemtek_unit.muted = 0;
	gemtek_mute(&gemtek_unit);

	return 0;
}

MODULE_AUTHOR("Jonas Munsin");
MODULE_DESCRIPTION("A driver for the GemTek Radio Card");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the GemTek card (0x20c, 0x30c, 0x24c or 0x34c (0x20c or 0x248 have been reported to work for the combined sound/radiocard)).");
module_param(radio_nr, int, 0);

static void __exit gemtek_cleanup(void)
{
	video_unregister_device(&gemtek_radio);
	release_region(io,4);
}

module_init(gemtek_init);
module_exit(gemtek_cleanup);

/*
  Local variables:
  compile-command: "gcc -c -DMODVERSIONS -D__KERNEL__ -DMODULE -O6 -Wall -Wstrict-prototypes -I /home/blp/tmp/linux-2.1.111-rtrack/include radio-rtrack2.c"
  End:
*/
