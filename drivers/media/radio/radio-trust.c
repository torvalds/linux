/* radio-trust.c - Trust FM Radio card driver for Linux 2.2
 * by Eric Lammerts <eric@scintilla.utwente.nl>
 *
 * Based on radio-aztech.c. Original notes:
 *
 * Adapted to support the Video for Linux API by
 * Russell Kroll <rkroll@exploits.org>.  Based on original tuner code by:
 *
 * Quay Ly
 * Donald Song
 * Jason Lewis      (jlewis@twilight.vtc.vsc.edu)
 * Scott McGrath    (smcgrath@twilight.vtc.vsc.edu)
 * William McGrath  (wmcgrath@twilight.vtc.vsc.edu)
 *
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#include <stdarg.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

MODULE_AUTHOR("Eric Lammerts, Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the Trust FM Radio card.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.3");

/* acceptable ports: 0x350 (JP3 shorted), 0x358 (JP3 open) */

#ifndef CONFIG_RADIO_TRUST_PORT
#define CONFIG_RADIO_TRUST_PORT -1
#endif

static int io = CONFIG_RADIO_TRUST_PORT;
static int radio_nr = -1;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the Trust FM Radio card (0x350 or 0x358)");
module_param(radio_nr, int, 0);

struct trust {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	int io;
	int ioval;
	__u16 curvol;
	__u16 curbass;
	__u16 curtreble;
	int muted;
	unsigned long curfreq;
	int curstereo;
	int curmute;
	struct mutex lock;
};

static struct trust trust_card;

/* i2c addresses */
#define TDA7318_ADDR 0x88
#define TSA6060T_ADDR 0xc4

#define TR_DELAY do { inb(tr->io); inb(tr->io); inb(tr->io); } while (0)
#define TR_SET_SCL outb(tr->ioval |= 2, tr->io)
#define TR_CLR_SCL outb(tr->ioval &= 0xfd, tr->io)
#define TR_SET_SDA outb(tr->ioval |= 1, tr->io)
#define TR_CLR_SDA outb(tr->ioval &= 0xfe, tr->io)

static void write_i2c(struct trust *tr, int n, ...)
{
	unsigned char val, mask;
	va_list args;

	va_start(args, n);

	/* start condition */
	TR_SET_SDA;
	TR_SET_SCL;
	TR_DELAY;
	TR_CLR_SDA;
	TR_CLR_SCL;
	TR_DELAY;

	for(; n; n--) {
		val = va_arg(args, unsigned);
		for(mask = 0x80; mask; mask >>= 1) {
			if(val & mask)
				TR_SET_SDA;
			else
				TR_CLR_SDA;
			TR_SET_SCL;
			TR_DELAY;
			TR_CLR_SCL;
			TR_DELAY;
		}
		/* acknowledge bit */
		TR_SET_SDA;
		TR_SET_SCL;
		TR_DELAY;
		TR_CLR_SCL;
		TR_DELAY;
	}

	/* stop condition */
	TR_CLR_SDA;
	TR_DELAY;
	TR_SET_SCL;
	TR_DELAY;
	TR_SET_SDA;
	TR_DELAY;

	va_end(args);
}

static void tr_setvol(struct trust *tr, __u16 vol)
{
	mutex_lock(&tr->lock);
	tr->curvol = vol / 2048;
	write_i2c(tr, 2, TDA7318_ADDR, tr->curvol ^ 0x1f);
	mutex_unlock(&tr->lock);
}

static int basstreble2chip[15] = {
	0, 1, 2, 3, 4, 5, 6, 7, 14, 13, 12, 11, 10, 9, 8
};

static void tr_setbass(struct trust *tr, __u16 bass)
{
	mutex_lock(&tr->lock);
	tr->curbass = bass / 4370;
	write_i2c(tr, 2, TDA7318_ADDR, 0x60 | basstreble2chip[tr->curbass]);
	mutex_unlock(&tr->lock);
}

static void tr_settreble(struct trust *tr, __u16 treble)
{
	mutex_lock(&tr->lock);
	tr->curtreble = treble / 4370;
	write_i2c(tr, 2, TDA7318_ADDR, 0x70 | basstreble2chip[tr->curtreble]);
	mutex_unlock(&tr->lock);
}

static void tr_setstereo(struct trust *tr, int stereo)
{
	mutex_lock(&tr->lock);
	tr->curstereo = !!stereo;
	tr->ioval = (tr->ioval & 0xfb) | (!tr->curstereo << 2);
	outb(tr->ioval, tr->io);
	mutex_unlock(&tr->lock);
}

static void tr_setmute(struct trust *tr, int mute)
{
	mutex_lock(&tr->lock);
	tr->curmute = !!mute;
	tr->ioval = (tr->ioval & 0xf7) | (tr->curmute << 3);
	outb(tr->ioval, tr->io);
	mutex_unlock(&tr->lock);
}

static int tr_getsigstr(struct trust *tr)
{
	int i, v;

	mutex_lock(&tr->lock);
	for (i = 0, v = 0; i < 100; i++)
		v |= inb(tr->io);
	mutex_unlock(&tr->lock);
	return (v & 1) ? 0 : 0xffff;
}

static int tr_getstereo(struct trust *tr)
{
	/* don't know how to determine it, just return the setting */
	return tr->curstereo;
}

static void tr_setfreq(struct trust *tr, unsigned long f)
{
	mutex_lock(&tr->lock);
	tr->curfreq = f;
	f /= 160;	/* Convert to 10 kHz units	*/
	f += 1070;	/* Add 10.7 MHz IF		*/
	write_i2c(tr, 5, TSA6060T_ADDR, (f << 1) | 1, f >> 7, 0x60 | ((f >> 15) & 1), 0);
	mutex_unlock(&tr->lock);
}

static int vidioc_querycap(struct file *file, void *priv,
				struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-trust", sizeof(v->driver));
	strlcpy(v->card, "Trust FM Radio", sizeof(v->card));
	strlcpy(v->bus_info, "ISA", sizeof(v->bus_info));
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct trust *tr = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	strlcpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = 87.5 * 16000;
	v->rangehigh = 108 * 16000;
	v->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	v->capability = V4L2_TUNER_CAP_LOW;
	if (tr_getstereo(tr))
		v->audmode = V4L2_TUNER_MODE_STEREO;
	else
		v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = tr_getsigstr(tr);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct trust *tr = video_drvdata(file);

	if (v->index)
		return -EINVAL;
	tr_setstereo(tr, v->audmode == V4L2_TUNER_MODE_STEREO);
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct trust *tr = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;
	tr_setfreq(tr, f->frequency);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct trust *tr = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = tr->curfreq;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
	case V4L2_CID_AUDIO_VOLUME:
		return v4l2_ctrl_query_fill(qc, 0, 65535, 2048, 65535);
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
		return v4l2_ctrl_query_fill(qc, 0, 65535, 4370, 32768);
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct trust *tr = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = tr->curmute;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = tr->curvol * 2048;
		return 0;
	case V4L2_CID_AUDIO_BASS:
		ctrl->value = tr->curbass * 4370;
		return 0;
	case V4L2_CID_AUDIO_TREBLE:
		ctrl->value = tr->curtreble * 4370;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct trust *tr = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		tr_setmute(tr, ctrl->value);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		tr_setvol(tr, ctrl->value);
		return 0;
	case V4L2_CID_AUDIO_BASS:
		tr_setbass(tr, ctrl->value);
		return 0;
	case V4L2_CID_AUDIO_TREBLE:
		tr_settreble(tr, ctrl->value);
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

static const struct v4l2_file_operations trust_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops trust_ioctl_ops = {
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

static int __init trust_init(void)
{
	struct trust *tr = &trust_card;
	struct v4l2_device *v4l2_dev = &tr->v4l2_dev;
	int res;

	strlcpy(v4l2_dev->name, "trust", sizeof(v4l2_dev->name));
	tr->io = io;
	tr->ioval = 0xf;
	mutex_init(&tr->lock);

	if (tr->io == -1) {
		v4l2_err(v4l2_dev, "You must set an I/O address with io=0x0x350 or 0x358\n");
		return -EINVAL;
	}
	if (!request_region(tr->io, 2, "Trust FM Radio")) {
		v4l2_err(v4l2_dev, "port 0x%x already in use\n", tr->io);
		return -EBUSY;
	}

	res = v4l2_device_register(NULL, v4l2_dev);
	if (res < 0) {
		release_region(tr->io, 2);
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		return res;
	}

	strlcpy(tr->vdev.name, v4l2_dev->name, sizeof(tr->vdev.name));
	tr->vdev.v4l2_dev = v4l2_dev;
	tr->vdev.fops = &trust_fops;
	tr->vdev.ioctl_ops = &trust_ioctl_ops;
	tr->vdev.release = video_device_release_empty;
	video_set_drvdata(&tr->vdev, tr);

	write_i2c(tr, 2, TDA7318_ADDR, 0x80);	/* speaker att. LF = 0 dB */
	write_i2c(tr, 2, TDA7318_ADDR, 0xa0);	/* speaker att. RF = 0 dB */
	write_i2c(tr, 2, TDA7318_ADDR, 0xc0);	/* speaker att. LR = 0 dB */
	write_i2c(tr, 2, TDA7318_ADDR, 0xe0);	/* speaker att. RR = 0 dB */
	write_i2c(tr, 2, TDA7318_ADDR, 0x40);	/* stereo 1 input, gain = 18.75 dB */

	tr_setvol(tr, 0xffff);
	tr_setbass(tr, 0x8000);
	tr_settreble(tr, 0x8000);
	tr_setstereo(tr, 1);

	/* mute card - prevents noisy bootups */
	tr_setmute(tr, 1);

	if (video_register_device(&tr->vdev, VFL_TYPE_RADIO, radio_nr) < 0) {
		v4l2_device_unregister(v4l2_dev);
		release_region(tr->io, 2);
		return -EINVAL;
	}

	v4l2_info(v4l2_dev, "Trust FM Radio card driver v1.0.\n");

	return 0;
}

static void __exit cleanup_trust_module(void)
{
	struct trust *tr = &trust_card;

	video_unregister_device(&tr->vdev);
	v4l2_device_unregister(&tr->v4l2_dev);
	release_region(tr->io, 2);
}

module_init(trust_init);
module_exit(cleanup_trust_module);
