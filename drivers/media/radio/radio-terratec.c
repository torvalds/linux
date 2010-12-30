/* Terratec ActiveRadio ISA Standalone card driver for Linux radio support
 * (c) 1999 R. Offermanns (rolf@offermanns.de)
 * based on the aimslab radio driver from M. Kirkwood
 * many thanks to Michael Becker and Friedhelm Birth (from TerraTec)
 *
 *
 * History:
 * 1999-05-21	First preview release
 *
 *  Notes on the hardware:
 *  There are two "main" chips on the card:
 *  - Philips OM5610 (http://www-us.semiconductors.philips.com/acrobat/datasheets/OM5610_2.pdf)
 *  - Philips SAA6588 (http://www-us.semiconductors.philips.com/acrobat/datasheets/SAA6588_1.pdf)
 *  (you can get the datasheet at the above links)
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *  Volume Control is done digitally
 *
 *  there is a I2C controlled RDS decoder (SAA6588)  onboard, which i would like to support someday
 *  (as soon i have understand how to get started :)
 *  If you can help me out with that, please contact me!!
 *
 *
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/mutex.h>
#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#include <linux/io.h>		/* outb, outb_p			*/
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

MODULE_AUTHOR("R.OFFERMANNS & others");
MODULE_DESCRIPTION("A driver for the TerraTec ActiveRadio Standalone radio card.");
MODULE_LICENSE("GPL");

#ifndef CONFIG_RADIO_TERRATEC_PORT
#define CONFIG_RADIO_TERRATEC_PORT 0x590
#endif

static int io = CONFIG_RADIO_TERRATEC_PORT;
static int radio_nr = -1;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the TerraTec ActiveRadio card (0x590 or 0x591)");
module_param(radio_nr, int, 0);

#define RADIO_VERSION KERNEL_VERSION(0, 0, 2)

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
		.maximum       = 0xff,
		.step          = 1,
		.default_value = 0xff,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}
};

#define WRT_DIS 	0x00
#define CLK_OFF		0x00
#define IIC_DATA	0x01
#define IIC_CLK		0x02
#define DATA		0x04
#define CLK_ON 		0x08
#define WRT_EN		0x10

struct terratec
{
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	int io;
	int curvol;
	unsigned long curfreq;
	int muted;
	struct mutex lock;
};

static struct terratec terratec_card;

/* local things */

static void tt_write_vol(struct terratec *tt, int volume)
{
	int i;

	volume = volume + (volume * 32); /* change both channels */
	mutex_lock(&tt->lock);
	for (i = 0; i < 8; i++) {
		if (volume & (0x80 >> i))
			outb(0x80, tt->io + 1);
		else
			outb(0x00, tt->io + 1);
	}
	mutex_unlock(&tt->lock);
}



static void tt_mute(struct terratec *tt)
{
	tt->muted = 1;
	tt_write_vol(tt, 0);
}

static int tt_setvol(struct terratec *tt, int vol)
{
	if (vol == tt->curvol) {	/* requested volume = current */
		if (tt->muted) {	/* user is unmuting the card  */
			tt->muted = 0;
			tt_write_vol(tt, vol);	/* enable card */
		}
		return 0;
	}

	if (vol == 0) {			/* volume = 0 means mute the card */
		tt_write_vol(tt, 0);	/* "turn off card" by setting vol to 0 */
		tt->curvol = vol;	/* track the volume state!	*/
		return 0;
	}

	tt->muted = 0;
	tt_write_vol(tt, vol);
	tt->curvol = vol;
	return 0;
}


/* this is the worst part in this driver */
/* many more or less strange things are going on here, but hey, it works :) */

static int tt_setfreq(struct terratec *tt, unsigned long freq1)
{
	int freq;
	int i;
	int p;
	int  temp;
	long rest;
	unsigned char buffer[25];		/* we have to bit shift 25 registers */

	mutex_lock(&tt->lock);

	tt->curfreq = freq1;

	freq = freq1 / 160;			/* convert the freq. to a nice to handle value */
	memset(buffer, 0, sizeof(buffer));

	rest = freq * 10 + 10700;	/* I once had understood what is going on here */
					/* maybe some wise guy (friedhelm?) can comment this stuff */
	i = 13;
	p = 10;
	temp = 102400;
	while (rest != 0) {
		if (rest % temp  == rest)
			buffer[i] = 0;
		else {
			buffer[i] = 1;
			rest = rest - temp;
		}
		i--;
		p--;
		temp = temp / 2;
	}

	for (i = 24; i > -1; i--) {	/* bit shift the values to the radiocard */
		if (buffer[i] == 1) {
			outb(WRT_EN | DATA, tt->io);
			outb(WRT_EN | DATA | CLK_ON, tt->io);
			outb(WRT_EN | DATA, tt->io);
		} else {
			outb(WRT_EN | 0x00, tt->io);
			outb(WRT_EN | 0x00 | CLK_ON, tt->io);
		}
	}
	outb(0x00, tt->io);

	mutex_unlock(&tt->lock);

	return 0;
}

static int tt_getsigstr(struct terratec *tt)
{
	if (inb(tt->io) & 2)	/* bit set = no signal present	*/
		return 0;
	return 1;		/* signal present		*/
}

static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-terratec", sizeof(v->driver));
	strlcpy(v->card, "ActiveRadio", sizeof(v->card));
	strlcpy(v->bus_info, "ISA", sizeof(v->bus_info));
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct terratec *tt = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	strlcpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = 87 * 16000;
	v->rangehigh = 108 * 16000;
	v->rxsubchans = V4L2_TUNER_SUB_MONO;
	v->capability = V4L2_TUNER_CAP_LOW;
	v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xFFFF * tt_getsigstr(tt);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	return v->index ? -EINVAL : 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct terratec *tt = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;
	tt_setfreq(tt, f->frequency);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct terratec *tt = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = tt->curfreq;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
		if (qc->id && qc->id == radio_qctrl[i].id) {
			memcpy(qc, &(radio_qctrl[i]), sizeof(*qc));
			return 0;
		}
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct terratec *tt = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (tt->muted)
			ctrl->value = 1;
		else
			ctrl->value = 0;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = tt->curvol * 6554;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct terratec *tt = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			tt_mute(tt);
		else
			tt_setvol(tt,tt->curvol);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		tt_setvol(tt,ctrl->value);
		return 0;
	}
	return -EINVAL;
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

static const struct v4l2_file_operations terratec_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops terratec_ioctl_ops = {
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

static int __init terratec_init(void)
{
	struct terratec *tt = &terratec_card;
	struct v4l2_device *v4l2_dev = &tt->v4l2_dev;
	int res;

	strlcpy(v4l2_dev->name, "terratec", sizeof(v4l2_dev->name));
	tt->io = io;
	if (tt->io == -1) {
		v4l2_err(v4l2_dev, "you must set an I/O address with io=0x590 or 0x591\n");
		return -EINVAL;
	}
	if (!request_region(tt->io, 2, "terratec")) {
		v4l2_err(v4l2_dev, "port 0x%x already in use\n", io);
		return -EBUSY;
	}

	res = v4l2_device_register(NULL, v4l2_dev);
	if (res < 0) {
		release_region(tt->io, 2);
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		return res;
	}

	strlcpy(tt->vdev.name, v4l2_dev->name, sizeof(tt->vdev.name));
	tt->vdev.v4l2_dev = v4l2_dev;
	tt->vdev.fops = &terratec_fops;
	tt->vdev.ioctl_ops = &terratec_ioctl_ops;
	tt->vdev.release = video_device_release_empty;
	video_set_drvdata(&tt->vdev, tt);

	mutex_init(&tt->lock);

	/* mute card - prevents noisy bootups */
	tt_write_vol(tt, 0);

	if (video_register_device(&tt->vdev, VFL_TYPE_RADIO, radio_nr) < 0) {
		v4l2_device_unregister(&tt->v4l2_dev);
		release_region(tt->io, 2);
		return -EINVAL;
	}

	v4l2_info(v4l2_dev, "TERRATEC ActivRadio Standalone card driver.\n");
	return 0;
}

static void __exit terratec_exit(void)
{
	struct terratec *tt = &terratec_card;
	struct v4l2_device *v4l2_dev = &tt->v4l2_dev;

	video_unregister_device(&tt->vdev);
	v4l2_device_unregister(&tt->v4l2_dev);
	release_region(tt->io, 2);
	v4l2_info(v4l2_dev, "TERRATEC ActivRadio Standalone card driver unloaded.\n");
}

module_init(terratec_init);
module_exit(terratec_exit);

