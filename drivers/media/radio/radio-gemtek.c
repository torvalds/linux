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

#include <linux/version.h>	/* for KERNEL_VERSION MACRO	*/
#define RADIO_VERSION KERNEL_VERSION(0,0,3)
#define RADIO_BANNER "GemTek Radio card driver: v0.0.3"

/*
 * Module info.
 */

MODULE_AUTHOR("Jonas Munsin, Pekka Seppänen <pexu@kapsi.fi>");
MODULE_DESCRIPTION("A driver for the GemTek Radio card.");
MODULE_LICENSE("GPL");

/*
 * Module params.
 */

#ifndef CONFIG_RADIO_GEMTEK_PORT
#define CONFIG_RADIO_GEMTEK_PORT -1
#endif
#ifndef CONFIG_RADIO_GEMTEK_PROBE
#define CONFIG_RADIO_GEMTEK_PROBE 1
#endif

static int io		= CONFIG_RADIO_GEMTEK_PORT;
static int probe	= CONFIG_RADIO_GEMTEK_PROBE;
static int hardmute;
static int shutdown	= 1;
static int keepmuted	= 1;
static int initmute	= 1;
static int radio_nr	= -1;

module_param(io, int, 0444);
MODULE_PARM_DESC(io, "Force I/O port for the GemTek Radio card if automatic"
	 "probing is disabled or fails. The most common I/O ports are: 0x20c "
	 "0x30c, 0x24c or 0x34c (0x20c, 0x248 and 0x28c have been reported to "
	 " work for the combined sound/radiocard).");

module_param(probe, bool, 0444);
MODULE_PARM_DESC(probe, "Enable automatic device probing. Note: only the most "
	"common I/O ports used by the card are probed.");

module_param(hardmute, bool, 0644);
MODULE_PARM_DESC(hardmute, "Enable `hard muting' by shutting down PLL, may "
	 "reduce static noise.");

module_param(shutdown, bool, 0644);
MODULE_PARM_DESC(shutdown, "Enable shutting down PLL and muting line when "
	 "module is unloaded.");

module_param(keepmuted, bool, 0644);
MODULE_PARM_DESC(keepmuted, "Keep card muted even when frequency is changed.");

module_param(initmute, bool, 0444);
MODULE_PARM_DESC(initmute, "Mute card when module is loaded.");

module_param(radio_nr, int, 0444);

/*
 * Functions for controlling the card.
 */
#define GEMTEK_LOWFREQ	(87*16000)
#define GEMTEK_HIGHFREQ	(108*16000)

/*
 * Frequency calculation constants.  Intermediate frequency 10.52 MHz (nominal
 * value 10.7 MHz), reference divisor 6.39 kHz (nominal 6.25 kHz).
 */
#define FSCALE		8
#define IF_OFFSET	((unsigned int)(10.52 * 16000 * (1<<FSCALE)))
#define REF_FREQ	((unsigned int)(6.39 * 16 * (1<<FSCALE)))

#define GEMTEK_CK		0x01	/* Clock signal			*/
#define GEMTEK_DA		0x02	/* Serial data			*/
#define GEMTEK_CE		0x04	/* Chip enable			*/
#define GEMTEK_NS		0x08	/* No signal			*/
#define GEMTEK_MT		0x10	/* Line mute			*/
#define GEMTEK_STDF_3_125_KHZ	0x01	/* Standard frequency 3.125 kHz	*/
#define GEMTEK_PLL_OFF		0x07	/* PLL off			*/

#define BU2614_BUS_SIZE	32	/* BU2614 / BU2614FS bus size		*/

#define SHORT_DELAY 5		/* usec */
#define LONG_DELAY 75		/* usec */

struct gemtek_device {
	unsigned long lastfreq;
	int muted;
	u32 bu2614data;
};

#define BU2614_FREQ_BITS 	16 /* D0..D15, Frequency data		*/
#define BU2614_PORT_BITS	3 /* P0..P2, Output port control data	*/
#define BU2614_VOID_BITS	4 /* unused 				*/
#define BU2614_FMES_BITS	1 /* CT, Frequency measurement beginning data */
#define BU2614_STDF_BITS	3 /* R0..R2, Standard frequency data	*/
#define BU2614_SWIN_BITS	1 /* S, Switch between FMIN / AMIN	*/
#define BU2614_SWAL_BITS        1 /* PS, Swallow counter division (AMIN only)*/
#define BU2614_VOID2_BITS	1 /* unused				*/
#define BU2614_FMUN_BITS	1 /* GT, Frequency measurement time & unlock */
#define BU2614_TEST_BITS	1 /* TS, Test data is input		*/

#define BU2614_FREQ_SHIFT 	0
#define BU2614_PORT_SHIFT	(BU2614_FREQ_BITS + BU2614_FREQ_SHIFT)
#define BU2614_VOID_SHIFT	(BU2614_PORT_BITS + BU2614_PORT_SHIFT)
#define BU2614_FMES_SHIFT	(BU2614_VOID_BITS + BU2614_VOID_SHIFT)
#define BU2614_STDF_SHIFT	(BU2614_FMES_BITS + BU2614_FMES_SHIFT)
#define BU2614_SWIN_SHIFT	(BU2614_STDF_BITS + BU2614_STDF_SHIFT)
#define BU2614_SWAL_SHIFT	(BU2614_SWIN_BITS + BU2614_SWIN_SHIFT)
#define BU2614_VOID2_SHIFT	(BU2614_SWAL_BITS + BU2614_SWAL_SHIFT)
#define BU2614_FMUN_SHIFT	(BU2614_VOID2_BITS + BU2614_VOID2_SHIFT)
#define BU2614_TEST_SHIFT	(BU2614_FMUN_BITS + BU2614_FMUN_SHIFT)

#define MKMASK(field)	(((1<<BU2614_##field##_BITS) - 1) << \
			BU2614_##field##_SHIFT)
#define BU2614_PORT_MASK	MKMASK(PORT)
#define BU2614_FREQ_MASK	MKMASK(FREQ)
#define BU2614_VOID_MASK	MKMASK(VOID)
#define BU2614_FMES_MASK	MKMASK(FMES)
#define BU2614_STDF_MASK	MKMASK(STDF)
#define BU2614_SWIN_MASK	MKMASK(SWIN)
#define BU2614_SWAL_MASK	MKMASK(SWAL)
#define BU2614_VOID2_MASK	MKMASK(VOID2)
#define BU2614_FMUN_MASK	MKMASK(FMUN)
#define BU2614_TEST_MASK	MKMASK(TEST)

static struct gemtek_device gemtek_unit;

static spinlock_t lock;

/*
 * Set data which will be sent to BU2614FS.
 */
#define gemtek_bu2614_set(dev, field, data) ((dev)->bu2614data = \
	((dev)->bu2614data & ~field##_MASK) | ((data) << field##_SHIFT))

/*
 * Transmit settings to BU2614FS over GemTek IC.
 */
static void gemtek_bu2614_transmit(struct gemtek_device *dev)
{
	int i, bit, q, mute;

	spin_lock(&lock);

	mute = dev->muted ? GEMTEK_MT : 0x00;

	outb_p(mute | GEMTEK_DA | GEMTEK_CK, io);
	udelay(SHORT_DELAY);
	outb_p(mute | GEMTEK_CE | GEMTEK_DA | GEMTEK_CK, io);
	udelay(LONG_DELAY);

	for (i = 0, q = dev->bu2614data; i < 32; i++, q >>= 1) {
	    bit = (q & 1) ? GEMTEK_DA : 0;
	    outb_p(mute | GEMTEK_CE | bit, io);
	    udelay(SHORT_DELAY);
	    outb_p(mute | GEMTEK_CE | bit | GEMTEK_CK, io);
	    udelay(SHORT_DELAY);
	}

	outb_p(mute | GEMTEK_DA | GEMTEK_CK, io);
	udelay(SHORT_DELAY);
	outb_p(mute | GEMTEK_CE | GEMTEK_DA | GEMTEK_CK, io);
	udelay(LONG_DELAY);

	spin_unlock(&lock);
}

/*
 * Calculate divisor from FM-frequency for BU2614FS (3.125 KHz STDF expected).
 */
static unsigned long gemtek_convfreq(unsigned long freq)
{
	return ((freq<<FSCALE) + IF_OFFSET + REF_FREQ/2) / REF_FREQ;
}

/*
 * Set FM-frequency.
 */
static void gemtek_setfreq(struct gemtek_device *dev, unsigned long freq)
{

	if (keepmuted && hardmute && dev->muted)
		return;

	if (freq < GEMTEK_LOWFREQ)
		freq = GEMTEK_LOWFREQ;
	else if (freq > GEMTEK_HIGHFREQ)
		freq = GEMTEK_HIGHFREQ;

	dev->lastfreq = freq;
	dev->muted = 0;

	gemtek_bu2614_set(dev, BU2614_PORT, 0);
	gemtek_bu2614_set(dev, BU2614_FMES, 0);
	gemtek_bu2614_set(dev, BU2614_SWIN, 0);	/* FM-mode	*/
	gemtek_bu2614_set(dev, BU2614_SWAL, 0);
	gemtek_bu2614_set(dev, BU2614_FMUN, 1);	/* GT bit set	*/
	gemtek_bu2614_set(dev, BU2614_TEST, 0);

	gemtek_bu2614_set(dev, BU2614_STDF, GEMTEK_STDF_3_125_KHZ);
	gemtek_bu2614_set(dev, BU2614_FREQ, gemtek_convfreq(freq));

	gemtek_bu2614_transmit(dev);
}

/*
 * Set mute flag.
 */
static void gemtek_mute(struct gemtek_device *dev)
{
	int i;
	dev->muted = 1;

	if (hardmute) {
		/* Turn off PLL, disable data output */
		gemtek_bu2614_set(dev, BU2614_PORT, 0);
		gemtek_bu2614_set(dev, BU2614_FMES, 0);	/* CT bit off	*/
		gemtek_bu2614_set(dev, BU2614_SWIN, 0);	/* FM-mode	*/
		gemtek_bu2614_set(dev, BU2614_SWAL, 0);
		gemtek_bu2614_set(dev, BU2614_FMUN, 0);	/* GT bit off	*/
		gemtek_bu2614_set(dev, BU2614_TEST, 0);
		gemtek_bu2614_set(dev, BU2614_STDF, GEMTEK_PLL_OFF);
		gemtek_bu2614_set(dev, BU2614_FREQ, 0);
		gemtek_bu2614_transmit(dev);
	} else {
		spin_lock(&lock);

		/* Read bus contents (CE, CK and DA). */
		i = inb_p(io);
		/* Write it back with mute flag set. */
		outb_p((i >> 5) | GEMTEK_MT, io);
		udelay(SHORT_DELAY);

		spin_unlock(&lock);
	}
}

/*
 * Unset mute flag.
 */
static void gemtek_unmute(struct gemtek_device *dev)
{
	int i;
	dev->muted = 0;

	if (hardmute) {
		/* Turn PLL back on. */
		gemtek_setfreq(dev, dev->lastfreq);
	} else {
		spin_lock(&lock);

		i = inb_p(io);
		outb_p(i >> 5, io);
		udelay(SHORT_DELAY);

		spin_unlock(&lock);
	}
}

/*
 * Get signal strength (= stereo status).
 */
static inline int gemtek_getsigstr(void)
{
	return inb_p(io) & GEMTEK_NS ? 0 : 1;
}

/*
 * Check if requested card acts like GemTek Radio card.
 */
static int gemtek_verify(int port)
{
	static int verified = -1;
	int i, q;

	if (verified == port)
		return 1;

	spin_lock(&lock);

	q = inb_p(port);	/* Read bus contents before probing. */
	/* Try to turn on CE, CK and DA respectively and check if card responds
	   properly. */
	for (i = 0; i < 3; ++i) {
		outb_p(1 << i, port);
		udelay(SHORT_DELAY);

		if ((inb_p(port) & (~GEMTEK_NS)) != (0x17 | (1 << (i + 5)))) {
			spin_unlock(&lock);
			return 0;
		}
	}
	outb_p(q >> 5, port);	/* Write bus contents back. */
	udelay(SHORT_DELAY);

	spin_unlock(&lock);
	verified = port;

	return 1;
}

/*
 * Automatic probing for card.
 */
static int gemtek_probe(void)
{
	int ioports[] = { 0x20c, 0x30c, 0x24c, 0x34c, 0x248, 0x28c };
	int i;

	if (!probe) {
		printk(KERN_INFO "Automatic device probing disabled.\n");
		return -1;
	}

	printk(KERN_INFO "Automatic device probing enabled.\n");

	for (i = 0; i < ARRAY_SIZE(ioports); ++i) {
		printk(KERN_INFO "Trying I/O port 0x%x...\n", ioports[i]);

		if (!request_region(ioports[i], 1, "gemtek-probe")) {
			printk(KERN_WARNING "I/O port 0x%x busy!\n",
			       ioports[i]);
			continue;
		}

		if (gemtek_verify(ioports[i])) {
			printk(KERN_INFO "Card found from I/O port "
			       "0x%x!\n", ioports[i]);

			release_region(ioports[i], 1);

			io = ioports[i];
			return io;
		}

		release_region(ioports[i], 1);
	}

	printk(KERN_ERR "Automatic probing failed!\n");

	return -1;
}

/*
 * Video 4 Linux stuff.
 */

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id = V4L2_CID_AUDIO_MUTE,
		.name = "Mute",
		.minimum = 0,
		.maximum = 1,
		.default_value = 1,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
	}, {
		.id = V4L2_CID_AUDIO_VOLUME,
		.name = "Volume",
		.minimum = 0,
		.maximum = 65535,
		.step = 65535,
		.default_value = 0xff,
		.type = V4L2_CTRL_TYPE_INTEGER,
	}
};

static struct file_operations gemtek_fops = {
	.owner		= THIS_MODULE,
	.open		= video_exclusive_open,
	.release	= video_exclusive_release,
	.ioctl		= video_ioctl2,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek		= no_llseek
};

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-gemtek", sizeof(v->driver));
	strlcpy(v->card, "GemTek", sizeof(v->card));
	sprintf(v->bus_info, "ISA");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv, struct v4l2_tuner *v)
{
	if (v->index > 0)
		return -EINVAL;

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = GEMTEK_LOWFREQ;
	v->rangehigh = GEMTEK_HIGHFREQ;
	v->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO;
	v->signal = 0xffff * gemtek_getsigstr();
	if (v->signal) {
		v->audmode = V4L2_TUNER_MODE_STEREO;
		v->rxsubchans = V4L2_TUNER_SUB_STEREO;
	} else {
		v->audmode = V4L2_TUNER_MODE_MONO;
		v->rxsubchans = V4L2_TUNER_SUB_MONO;
	}

	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv, struct v4l2_tuner *v)
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

	gemtek_setfreq(rt, f->frequency);

	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
			      struct v4l2_frequency *f)
{
	struct video_device *dev = video_devdata(file);
	struct gemtek_device *rt = dev->priv;

	f->type = V4L2_TUNER_RADIO;
	f->frequency = rt->lastfreq;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); ++i) {
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

static int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
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

static int vidioc_s_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	if (a->index != 0)
		return -EINVAL;
	return 0;
}

static struct video_device gemtek_radio = {
	.owner			= THIS_MODULE,
	.name			= "GemTek Radio card",
	.type			= VID_TYPE_TUNER,
	.hardware		= VID_HARDWARE_GEMTEK,
	.fops			= &gemtek_fops,
	.vidioc_querycap	= vidioc_querycap,
	.vidioc_g_tuner		= vidioc_g_tuner,
	.vidioc_s_tuner		= vidioc_s_tuner,
	.vidioc_g_audio		= vidioc_g_audio,
	.vidioc_s_audio		= vidioc_s_audio,
	.vidioc_g_input		= vidioc_g_input,
	.vidioc_s_input		= vidioc_s_input,
	.vidioc_g_frequency	= vidioc_g_frequency,
	.vidioc_s_frequency	= vidioc_s_frequency,
	.vidioc_queryctrl	= vidioc_queryctrl,
	.vidioc_g_ctrl		= vidioc_g_ctrl,
	.vidioc_s_ctrl		= vidioc_s_ctrl
};

/*
 * Initialization / cleanup related stuff.
 */

/*
 * Initilize card.
 */
static int __init gemtek_init(void)
{
	printk(KERN_INFO RADIO_BANNER "\n");

	spin_lock_init(&lock);

	gemtek_probe();
	if (io) {
		if (!request_region(io, 1, "gemtek")) {
			printk(KERN_ERR "I/O port 0x%x already in use.\n", io);
			return -EBUSY;
		}

		if (!gemtek_verify(io))
			printk(KERN_WARNING "Card at I/O port 0x%x does not "
			       "respond properly, check your "
			       "configuration.\n", io);
		else
			printk(KERN_INFO "Using I/O port 0x%x.\n", io);
	} else if (probe) {
		printk(KERN_ERR "Automatic probing failed and no "
		       "fixed I/O port defined.\n");
		return -ENODEV;
	} else {
		printk(KERN_ERR "Automatic probing disabled but no fixed "
		       "I/O port defined.");
		return -EINVAL;
	}

	gemtek_radio.priv = &gemtek_unit;

	if (video_register_device(&gemtek_radio, VFL_TYPE_RADIO,
		radio_nr) == -1) {
		release_region(io, 1);
		return -EBUSY;
	}

	/* Set defaults */
	gemtek_unit.lastfreq = GEMTEK_LOWFREQ;
	gemtek_unit.bu2614data = 0;

	if (initmute)
		gemtek_mute(&gemtek_unit);

	return 0;
}

/*
 * Module cleanup
 */
static void __exit gemtek_exit(void)
{
	if (shutdown) {
		hardmute = 1;	/* Turn off PLL */
		gemtek_mute(&gemtek_unit);
	} else {
		printk(KERN_INFO "Module unloaded but card not muted!\n");
	}

	video_unregister_device(&gemtek_radio);
	release_region(io, 1);
}

module_init(gemtek_init);
module_exit(gemtek_exit);
