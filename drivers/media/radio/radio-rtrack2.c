/* RadioTrack II driver for Linux radio support (C) 1998 Ben Pfaff
 *
 * Based on RadioTrack I/RadioReveal (C) 1997 M. Kirkwood
 * Converted to new API by Alan Cox <alan@lxorguk.ukuu.org.uk>
 * Various bugfixes and enhancements by Russell Kroll <rkroll@exploits.org>
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
#include <media/v4l2-ioctl.h>
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

#ifndef CONFIG_RADIO_RTRACK2_PORT
#define CONFIG_RADIO_RTRACK2_PORT -1
#endif

static int io = CONFIG_RADIO_RTRACK2_PORT;
static int radio_nr = -1;
static spinlock_t lock;

struct rt_device
{
	unsigned long in_use;
	int port;
	unsigned long curfreq;
	int muted;
};


/* local things */

static void rt_mute(struct rt_device *dev)
{
	if(dev->muted)
		return;
	spin_lock(&lock);
	outb(1, io);
	spin_unlock(&lock);
	dev->muted = 1;
}

static void rt_unmute(struct rt_device *dev)
{
	if(dev->muted == 0)
		return;
	spin_lock(&lock);
	outb(0, io);
	spin_unlock(&lock);
	dev->muted = 0;
}

static void zero(void)
{
	outb_p(1, io);
	outb_p(3, io);
	outb_p(1, io);
}

static void one(void)
{
	outb_p(5, io);
	outb_p(7, io);
	outb_p(5, io);
}

static int rt_setfreq(struct rt_device *dev, unsigned long freq)
{
	int i;

	freq = freq / 200 + 856;

	spin_lock(&lock);

	outb_p(0xc8, io);
	outb_p(0xc9, io);
	outb_p(0xc9, io);

	for (i = 0; i < 10; i++)
		zero ();

	for (i = 14; i >= 0; i--)
		if (freq & (1 << i))
			one ();
		else
			zero ();

	outb_p(0xc8, io);
	if (!dev->muted)
		outb_p(0, io);

	spin_unlock(&lock);
	return 0;
}

static int vidioc_querycap(struct file *file, void *priv,
				struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-rtrack2", sizeof(v->driver));
	strlcpy(v->card, "RadioTrack II", sizeof(v->card));
	sprintf(v->bus_info, "ISA");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	if (v->index > 0)
		return -EINVAL;

	return 0;
}

static int rt_getsigstr(struct rt_device *dev)
{
	if (inb(io) & 2)	/* bit set = no signal present	*/
		return 0;
	return 1;		/* signal present		*/
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct rt_device *rt = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = (88*16000);
	v->rangehigh = (108*16000);
	v->rxsubchans = V4L2_TUNER_SUB_MONO;
	v->capability = V4L2_TUNER_CAP_LOW;
	v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xFFFF*rt_getsigstr(rt);
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct rt_device *rt = video_drvdata(file);

	rt->curfreq = f->frequency;
	rt_setfreq(rt, rt->curfreq);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct rt_device *rt = video_drvdata(file);

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
	struct rt_device *rt = video_drvdata(file);

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
	struct rt_device *rt = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			rt_mute(rt);
		else
			rt_unmute(rt);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		if (ctrl->value)
			rt_unmute(rt);
		else
			rt_mute(rt);
		return 0;
	}
	return -EINVAL;
}

static int vidioc_g_audio(struct file *file, void *priv,
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

static struct rt_device rtrack2_unit;

static int rtrack2_exclusive_open(struct file *file)
{
	return test_and_set_bit(0, &rtrack2_unit.in_use) ? -EBUSY : 0;
}

static int rtrack2_exclusive_release(struct file *file)
{
	clear_bit(0, &rtrack2_unit.in_use);
	return 0;
}

static const struct v4l2_file_operations rtrack2_fops = {
	.owner		= THIS_MODULE,
	.open           = rtrack2_exclusive_open,
	.release        = rtrack2_exclusive_release,
	.ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops rtrack2_ioctl_ops = {
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

static struct video_device rtrack2_radio = {
	.name		= "RadioTrack II radio",
	.fops           = &rtrack2_fops,
	.ioctl_ops 	= &rtrack2_ioctl_ops,
	.release	= video_device_release_empty,
};

static int __init rtrack2_init(void)
{
	if(io==-1)
	{
		printk(KERN_ERR "You must set an I/O address with io=0x20c or io=0x30c\n");
		return -EINVAL;
	}
	if (!request_region(io, 4, "rtrack2"))
	{
		printk(KERN_ERR "rtrack2: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	video_set_drvdata(&rtrack2_radio, &rtrack2_unit);

	spin_lock_init(&lock);
	if (video_register_device(&rtrack2_radio, VFL_TYPE_RADIO, radio_nr) < 0) {
		release_region(io, 4);
		return -EINVAL;
	}

	printk(KERN_INFO "AIMSlab Radiotrack II card driver.\n");

	/* mute card - prevents noisy bootups */
	outb(1, io);
	rtrack2_unit.muted = 1;

	return 0;
}

MODULE_AUTHOR("Ben Pfaff");
MODULE_DESCRIPTION("A driver for the RadioTrack II radio card.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the RadioTrack card (0x20c or 0x30c)");
module_param(radio_nr, int, 0);

static void __exit rtrack2_cleanup_module(void)
{
	video_unregister_device(&rtrack2_radio);
	release_region(io,4);
}

module_init(rtrack2_init);
module_exit(rtrack2_cleanup_module);

/*
  Local variables:
  compile-command: "mmake"
  End:
*/
