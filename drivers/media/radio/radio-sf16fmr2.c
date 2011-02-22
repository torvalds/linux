/* SF16FMR2 radio driver for Linux radio support
 * heavily based on fmi driver...
 * (c) 2000-2002 Ziglio Frediano, freddy77@angelfire.com
 *
 * Notes on the hardware
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *  No volume control - only mute/unmute - you have to use line volume
 *
 *  For read stereo/mono you must wait 0.1 sec after set frequency and
 *  card unmuted so I set frequency on unmute
 *  Signal handling seem to work only on autoscanning (not implemented)
 *
 *  Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/mutex.h>
#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#include <linux/io.h>		/* outb, outb_p			*/
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

MODULE_AUTHOR("Ziglio Frediano, freddy77@angelfire.com");
MODULE_DESCRIPTION("A driver for the SF16FMR2 radio.");
MODULE_LICENSE("GPL");

static int io = 0x384;
static int radio_nr = -1;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the SF16FMR2 card (should be 0x384, if do not work try 0x284)");
module_param(radio_nr, int, 0);

#define RADIO_VERSION KERNEL_VERSION(0,0,2)

#define AUD_VOL_INDEX 1

#undef DEBUG
//#define DEBUG 1

#ifdef DEBUG
# define  debug_print(s) printk s
#else
# define  debug_print(s)
#endif

/* this should be static vars for module size */
struct fmr2
{
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct mutex lock;
	int io;
	int curvol; /* 0-15 */
	int mute;
	int stereo; /* card is producing stereo audio */
	unsigned long curfreq; /* freq in kHz */
	int card_type;
};

static struct fmr2 fmr2_card;

/* hw precision is 12.5 kHz
 * It is only useful to give freq in interval of 200 (=0.0125Mhz),
 * other bits will be truncated
 */
#define RSF16_ENCODE(x)	((x) / 200 + 856)
#define RSF16_MINFREQ (87 * 16000)
#define RSF16_MAXFREQ (108 * 16000)

static inline void wait(int n, int io)
{
	for (; n; --n)
		inb(io);
}

static void outbits(int bits, unsigned int data, int nWait, int io)
{
	int bit;

	for (; --bits >= 0;) {
		bit = (data >> bits) & 1;
		outb(bit, io);
		wait(nWait, io);
		outb(bit | 2, io);
		wait(nWait, io);
		outb(bit, io);
		wait(nWait, io);
	}
}

static inline void fmr2_mute(int io)
{
	outb(0x00, io);
	wait(4, io);
}

static inline void fmr2_unmute(int io)
{
	outb(0x04, io);
	wait(4, io);
}

static inline int fmr2_stereo_mode(int io)
{
	int n = inb(io);

	outb(6, io);
	inb(io);
	n = ((n >> 3) & 1) ^ 1;
	debug_print((KERN_DEBUG "stereo: %d\n", n));
	return n;
}

static int fmr2_product_info(struct fmr2 *dev)
{
	int n = inb(dev->io);

	n &= 0xC1;
	if (n == 0) {
		/* this should support volume set */
		dev->card_type = 12;
		return 0;
	}
	/* not volume (mine is 11) */
	dev->card_type = (n == 128) ? 11 : 0;
	return n;
}

static inline int fmr2_getsigstr(struct fmr2 *dev)
{
	/* !!! works only if scanning freq */
	int res = 0xffff;

	outb(5, dev->io);
	wait(4, dev->io);
	if (!(inb(dev->io) & 1))
		res = 0;
	debug_print((KERN_DEBUG "signal: %d\n", res));
	return res;
}

/* set frequency and unmute card */
static int fmr2_setfreq(struct fmr2 *dev)
{
	unsigned long freq = dev->curfreq;

	fmr2_mute(dev->io);

	/* 0x42 for mono output
	 * 0x102 forward scanning
	 * 0x182 scansione avanti
	 */
	outbits(9, 0x2, 3, dev->io);
	outbits(16, RSF16_ENCODE(freq), 2, dev->io);

	fmr2_unmute(dev->io);

	/* wait 0.11 sec */
	msleep(110);

	/* NOTE if mute this stop radio
	   you must set freq on unmute */
	dev->stereo = fmr2_stereo_mode(dev->io);
	return 0;
}

/* !!! not tested, in my card this does't work !!! */
static int fmr2_setvolume(struct fmr2 *dev)
{
	int vol[16] = { 0x021, 0x084, 0x090, 0x104,
			0x110, 0x204, 0x210, 0x402,
			0x404, 0x408, 0x410, 0x801,
			0x802, 0x804, 0x808, 0x810 };
	int i, a;
	int n = vol[dev->curvol & 0x0f];

	if (dev->card_type != 11)
		return 1;

	for (i = 12; --i >= 0; ) {
		a = ((n >> i) & 1) << 6; /* if (a==0) a = 0; else a = 0x40; */
		outb(a | 4, dev->io);
		wait(4, dev->io);
		outb(a | 0x24, dev->io);
		wait(4, dev->io);
		outb(a | 4, dev->io);
		wait(4, dev->io);
	}
	for (i = 6; --i >= 0; ) {
		a = ((0x18 >> i) & 1) << 6;
		outb(a | 4, dev->io);
		wait(4, dev->io);
		outb(a | 0x24, dev->io);
		wait(4, dev->io);
		outb(a | 4, dev->io);
		wait(4, dev->io);
	}
	wait(4, dev->io);
	outb(0x14, dev->io);
	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-sf16fmr2", sizeof(v->driver));
	strlcpy(v->card, "SF16-FMR2 radio", sizeof(v->card));
	strlcpy(v->bus_info, "ISA", sizeof(v->bus_info));
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct fmr2 *fmr2 = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	strlcpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;

	v->rangelow = RSF16_MINFREQ;
	v->rangehigh = RSF16_MAXFREQ;
	v->rxsubchans = fmr2->stereo ? V4L2_TUNER_SUB_STEREO :
					V4L2_TUNER_SUB_MONO;
	v->capability = V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LOW;
	v->audmode = V4L2_TUNER_MODE_STEREO;
	mutex_lock(&fmr2->lock);
	v->signal = fmr2_getsigstr(fmr2);
	mutex_unlock(&fmr2->lock);
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
	struct fmr2 *fmr2 = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;
	if (f->frequency < RSF16_MINFREQ ||
			f->frequency > RSF16_MAXFREQ)
		return -EINVAL;
	/* rounding in steps of 200 to match the freq
	   that will be used */
	fmr2->curfreq = (f->frequency / 200) * 200;

	/* set card freq (if not muted) */
	if (fmr2->curvol && !fmr2->mute) {
		mutex_lock(&fmr2->lock);
		fmr2_setfreq(fmr2);
		mutex_unlock(&fmr2->lock);
	}
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct fmr2 *fmr2 = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = fmr2->curfreq;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	struct fmr2 *fmr2 = video_drvdata(file);

	switch (qc->id) {
	case V4L2_CID_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
	case V4L2_CID_AUDIO_VOLUME:
		/* Only card_type == 11 implements volume */
		if (fmr2->card_type == 11)
			return v4l2_ctrl_query_fill(qc, 0, 15, 1, 0);
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 0);
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct fmr2 *fmr2 = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = fmr2->mute;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = fmr2->curvol;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct fmr2 *fmr2 = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		fmr2->mute = ctrl->value;
		break;
	case V4L2_CID_AUDIO_VOLUME:
		fmr2->curvol = ctrl->value;
		break;
	default:
		return -EINVAL;
	}

#ifdef DEBUG
	if (fmr2->curvol && !fmr2->mute)
		printk(KERN_DEBUG "unmute\n");
	else
		printk(KERN_DEBUG "mute\n");
#endif

	mutex_lock(&fmr2->lock);
	if (fmr2->curvol && !fmr2->mute) {
		fmr2_setvolume(fmr2);
		/* Set frequency and unmute card */
		fmr2_setfreq(fmr2);
	} else
		fmr2_mute(fmr2->io);
	mutex_unlock(&fmr2->lock);
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

static const struct v4l2_file_operations fmr2_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops fmr2_ioctl_ops = {
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

static int __init fmr2_init(void)
{
	struct fmr2 *fmr2 = &fmr2_card;
	struct v4l2_device *v4l2_dev = &fmr2->v4l2_dev;
	int res;

	strlcpy(v4l2_dev->name, "sf16fmr2", sizeof(v4l2_dev->name));
	fmr2->io = io;
	fmr2->stereo = 1;
	mutex_init(&fmr2->lock);

	if (!request_region(fmr2->io, 2, "sf16fmr2")) {
		v4l2_err(v4l2_dev, "request_region failed!\n");
		return -EBUSY;
	}

	res = v4l2_device_register(NULL, v4l2_dev);
	if (res < 0) {
		release_region(fmr2->io, 2);
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		return res;
	}

	strlcpy(fmr2->vdev.name, v4l2_dev->name, sizeof(fmr2->vdev.name));
	fmr2->vdev.v4l2_dev = v4l2_dev;
	fmr2->vdev.fops = &fmr2_fops;
	fmr2->vdev.ioctl_ops = &fmr2_ioctl_ops;
	fmr2->vdev.release = video_device_release_empty;
	video_set_drvdata(&fmr2->vdev, fmr2);

	/* mute card - prevents noisy bootups */
	fmr2_mute(fmr2->io);
	fmr2_product_info(fmr2);

	if (video_register_device(&fmr2->vdev, VFL_TYPE_RADIO, radio_nr) < 0) {
		v4l2_device_unregister(v4l2_dev);
		release_region(fmr2->io, 2);
		return -EINVAL;
	}

	v4l2_info(v4l2_dev, "SF16FMR2 radio card driver at 0x%x.\n", fmr2->io);
	debug_print((KERN_DEBUG "card_type %d\n", fmr2->card_type));
	return 0;
}

static void __exit fmr2_exit(void)
{
	struct fmr2 *fmr2 = &fmr2_card;

	video_unregister_device(&fmr2->vdev);
	v4l2_device_unregister(&fmr2->v4l2_dev);
	release_region(fmr2->io, 2);
}

module_init(fmr2_init);
module_exit(fmr2_exit);

#ifndef MODULE

static int __init fmr2_setup_io(char *str)
{
	get_option(&str, &io);
	return 1;
}

__setup("sf16fmr2=", fmr2_setup_io);

#endif
