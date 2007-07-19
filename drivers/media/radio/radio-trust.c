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
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>

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
		.step          = 2048,
		.default_value = 65535,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_BASS,
		.name          = "Bass",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 4370,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_TREBLE,
		.name          = "Treble",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 4370,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},
};

/* acceptable ports: 0x350 (JP3 shorted), 0x358 (JP3 open) */

#ifndef CONFIG_RADIO_TRUST_PORT
#define CONFIG_RADIO_TRUST_PORT -1
#endif

static int io = CONFIG_RADIO_TRUST_PORT;
static int radio_nr = -1;
static int ioval = 0xf;
static __u16 curvol;
static __u16 curbass;
static __u16 curtreble;
static unsigned long curfreq;
static int curstereo;
static int curmute;

/* i2c addresses */
#define TDA7318_ADDR 0x88
#define TSA6060T_ADDR 0xc4

#define TR_DELAY do { inb(io); inb(io); inb(io); } while(0)
#define TR_SET_SCL outb(ioval |= 2, io)
#define TR_CLR_SCL outb(ioval &= 0xfd, io)
#define TR_SET_SDA outb(ioval |= 1, io)
#define TR_CLR_SDA outb(ioval &= 0xfe, io)

static void write_i2c(int n, ...)
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

static void tr_setvol(__u16 vol)
{
	curvol = vol / 2048;
	write_i2c(2, TDA7318_ADDR, curvol ^ 0x1f);
}

static int basstreble2chip[15] = {
	0, 1, 2, 3, 4, 5, 6, 7, 14, 13, 12, 11, 10, 9, 8
};

static void tr_setbass(__u16 bass)
{
	curbass = bass / 4370;
	write_i2c(2, TDA7318_ADDR, 0x60 | basstreble2chip[curbass]);
}

static void tr_settreble(__u16 treble)
{
	curtreble = treble / 4370;
	write_i2c(2, TDA7318_ADDR, 0x70 | basstreble2chip[curtreble]);
}

static void tr_setstereo(int stereo)
{
	curstereo = !!stereo;
	ioval = (ioval & 0xfb) | (!curstereo << 2);
	outb(ioval, io);
}

static void tr_setmute(int mute)
{
	curmute = !!mute;
	ioval = (ioval & 0xf7) | (curmute << 3);
	outb(ioval, io);
}

static int tr_getsigstr(void)
{
	int i, v;

	for(i = 0, v = 0; i < 100; i++) v |= inb(io);
	return (v & 1)? 0 : 0xffff;
}

static int tr_getstereo(void)
{
	/* don't know how to determine it, just return the setting */
	return curstereo;
}

static void tr_setfreq(unsigned long f)
{
	f /= 160;	/* Convert to 10 kHz units	*/
	f += 1070;	/* Add 10.7 MHz IF			*/

	write_i2c(5, TSA6060T_ADDR, (f << 1) | 1, f >> 7, 0x60 | ((f >> 15) & 1), 0);
}

static int vidioc_querycap(struct file *file, void *priv,
				struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-trust", sizeof(v->driver));
	strlcpy(v->card, "Trust FM Radio", sizeof(v->card));
	sprintf(v->bus_info, "ISA");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	if (v->index > 0)
		return -EINVAL;

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = (87.5*16000);
	v->rangehigh = (108*16000);
	v->rxsubchans = V4L2_TUNER_SUB_MONO|V4L2_TUNER_SUB_STEREO;
	v->capability = V4L2_TUNER_CAP_LOW;
	if (tr_getstereo())
		v->audmode = V4L2_TUNER_MODE_STEREO;
	else
		v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = tr_getsigstr();
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
	curfreq = f->frequency;
	tr_setfreq(curfreq);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	f->type = V4L2_TUNER_RADIO;
	f->frequency = curfreq;
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
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = curmute;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = curvol * 2048;
		return 0;
	case V4L2_CID_AUDIO_BASS:
		ctrl->value = curbass * 4370;
		return 0;
	case V4L2_CID_AUDIO_TREBLE:
		ctrl->value = curtreble * 4370;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		tr_setmute(ctrl->value);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		tr_setvol(ctrl->value);
		return 0;
	case V4L2_CID_AUDIO_BASS:
		tr_setbass(ctrl->value);
		return 0;
	case V4L2_CID_AUDIO_TREBLE:
		tr_settreble(ctrl->value);
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

static const struct file_operations trust_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= video_ioctl2,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device trust_radio=
{
	.owner		= THIS_MODULE,
	.name		= "Trust FM Radio",
	.type		= VID_TYPE_TUNER,
	.fops           = &trust_fops,
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
	if(io == -1) {
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}
	if(!request_region(io, 2, "Trust FM Radio")) {
		printk(KERN_ERR "trust: port 0x%x already in use\n", io);
		return -EBUSY;
	}
	if(video_register_device(&trust_radio, VFL_TYPE_RADIO, radio_nr)==-1)
	{
		release_region(io, 2);
		return -EINVAL;
	}

	printk(KERN_INFO "Trust FM Radio card driver v1.0.\n");

	write_i2c(2, TDA7318_ADDR, 0x80);	/* speaker att. LF = 0 dB */
	write_i2c(2, TDA7318_ADDR, 0xa0);	/* speaker att. RF = 0 dB */
	write_i2c(2, TDA7318_ADDR, 0xc0);	/* speaker att. LR = 0 dB */
	write_i2c(2, TDA7318_ADDR, 0xe0);	/* speaker att. RR = 0 dB */
	write_i2c(2, TDA7318_ADDR, 0x40);	/* stereo 1 input, gain = 18.75 dB */

	tr_setvol(0x8000);
	tr_setbass(0x8000);
	tr_settreble(0x8000);
	tr_setstereo(1);

	/* mute card - prevents noisy bootups */
	tr_setmute(1);

	return 0;
}

MODULE_AUTHOR("Eric Lammerts, Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the Trust FM Radio card.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the Trust FM Radio card (0x350 or 0x358)");
module_param(radio_nr, int, 0);

static void __exit cleanup_trust_module(void)
{
	video_unregister_device(&trust_radio);
	release_region(io, 2);
}

module_init(trust_init);
module_exit(cleanup_trust_module);
