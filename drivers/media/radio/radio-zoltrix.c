/* zoltrix radio plus driver for Linux radio support
 * (c) 1998 C. van Schaik <carl@leg.uct.ac.za>
 *
 * BUGS
 *  Due to the inconsistency in reading from the signal flags
 *  it is difficult to get an accurate tuned signal.
 *
 *  It seems that the card is not linear to 0 volume. It cuts off
 *  at a low volume, and it is not possible (at least I have not found)
 *  to get fine volume control over the low volume range.
 *
 *  Some code derived from code by Romolo Manfredini
 *				   romolo@bicnet.it
 *
 * 1999-05-06 - (C. van Schaik)
 *	      - Make signal strength and stereo scans
 *		kinder to cpu while in delay
 * 1999-01-05 - (C. van Schaik)
 *	      - Changed tuning to 1/160Mhz accuracy
 *	      - Added stereo support
 *		(card defaults to stereo)
 *		(can explicitly force mono on the card)
 *		(can detect if station is in stereo)
 *	      - Added unmute function
 *	      - Reworked ioctl functions
 * 2002-07-15 - Fix Stereo typo
 */

#include <linux/module.h>	/* Modules                        */
#include <linux/init.h>		/* Initdata                       */
#include <linux/ioport.h>	/* request_region		  */
#include <linux/delay.h>	/* udelay, msleep                 */
#include <asm/io.h>		/* outb, outb_p                   */
#include <asm/uaccess.h>	/* copy to/from user              */
#include <linux/videodev.h>	/* kernel radio structs           */
#include <linux/config.h>	/* CONFIG_RADIO_ZOLTRIX_PORT      */

#ifndef CONFIG_RADIO_ZOLTRIX_PORT
#define CONFIG_RADIO_ZOLTRIX_PORT -1
#endif

static int io = CONFIG_RADIO_ZOLTRIX_PORT;
static int radio_nr = -1;

struct zol_device {
	int port;
	int curvol;
	unsigned long curfreq;
	int muted;
	unsigned int stereo;
	struct mutex lock;
};

static int zol_setvol(struct zol_device *dev, int vol)
{
	dev->curvol = vol;
	if (dev->muted)
		return 0;

	mutex_lock(&dev->lock);
	if (vol == 0) {
		outb(0, io);
		outb(0, io);
		inb(io + 3);    /* Zoltrix needs to be read to confirm */
		mutex_unlock(&dev->lock);
		return 0;
	}

	outb(dev->curvol-1, io);
	msleep(10);
	inb(io + 2);
	mutex_unlock(&dev->lock);
	return 0;
}

static void zol_mute(struct zol_device *dev)
{
	dev->muted = 1;
	mutex_lock(&dev->lock);
	outb(0, io);
	outb(0, io);
	inb(io + 3);            /* Zoltrix needs to be read to confirm */
	mutex_unlock(&dev->lock);
}

static void zol_unmute(struct zol_device *dev)
{
	dev->muted = 0;
	zol_setvol(dev, dev->curvol);
}

static int zol_setfreq(struct zol_device *dev, unsigned long freq)
{
	/* tunes the radio to the desired frequency */
	unsigned long long bitmask, f, m;
	unsigned int stereo = dev->stereo;
	int i;

	if (freq == 0)
		return 1;
	m = (freq / 160 - 8800) * 2;
	f = (unsigned long long) m + 0x4d1c;

	bitmask = 0xc480402c10080000ull;
	i = 45;

	mutex_lock(&dev->lock);

	outb(0, io);
	outb(0, io);
	inb(io + 3);            /* Zoltrix needs to be read to confirm */

	outb(0x40, io);
	outb(0xc0, io);

	bitmask = (bitmask ^ ((f & 0xff) << 47) ^ ((f & 0xff00) << 30) ^ ( stereo << 31));
	while (i--) {
		if ((bitmask & 0x8000000000000000ull) != 0) {
			outb(0x80, io);
			udelay(50);
			outb(0x00, io);
			udelay(50);
			outb(0x80, io);
			udelay(50);
		} else {
			outb(0xc0, io);
			udelay(50);
			outb(0x40, io);
			udelay(50);
			outb(0xc0, io);
			udelay(50);
		}
		bitmask *= 2;
	}
	/* termination sequence */
	outb(0x80, io);
	outb(0xc0, io);
	outb(0x40, io);
	udelay(1000);
	inb(io+2);

	udelay(1000);

	if (dev->muted)
	{
		outb(0, io);
		outb(0, io);
		inb(io + 3);
		udelay(1000);
	}

	mutex_unlock(&dev->lock);

	if(!dev->muted)
	{
		zol_setvol(dev, dev->curvol);
	}
	return 0;
}

/* Get signal strength */

static int zol_getsigstr(struct zol_device *dev)
{
	int a, b;

	mutex_lock(&dev->lock);
	outb(0x00, io);         /* This stuff I found to do nothing */
	outb(dev->curvol, io);
	msleep(20);

	a = inb(io);
	msleep(10);
	b = inb(io);

	mutex_unlock(&dev->lock);

	if (a != b)
		return (0);

	if ((a == 0xcf) || (a == 0xdf)  /* I found this out by playing */
		|| (a == 0xef))       /* with a binary scanner on the card io */
		return (1);
	return (0);
}

static int zol_is_stereo (struct zol_device *dev)
{
	int x1, x2;

	mutex_lock(&dev->lock);

	outb(0x00, io);
	outb(dev->curvol, io);
	msleep(20);

	x1 = inb(io);
	msleep(10);
	x2 = inb(io);

	mutex_unlock(&dev->lock);

	if ((x1 == x2) && (x1 == 0xcf))
		return 1;
	return 0;
}

static int zol_do_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct zol_device *zol = dev->priv;

	switch (cmd) {
	case VIDIOCGCAP:
		{
			struct video_capability *v = arg;

			memset(v,0,sizeof(*v));
			v->type = VID_TYPE_TUNER;
			v->channels = 1 + zol->stereo;
			v->audios = 1;
			strcpy(v->name, "Zoltrix Radio");
			return 0;
		}
	case VIDIOCGTUNER:
		{
			struct video_tuner *v = arg;
			if (v->tuner)
				return -EINVAL;
			strcpy(v->name, "FM");
			v->rangelow = (int) (88.0 * 16000);
			v->rangehigh = (int) (108.0 * 16000);
			v->flags = zol_is_stereo(zol)
					? VIDEO_TUNER_STEREO_ON : 0;
			v->flags |= VIDEO_TUNER_LOW;
			v->mode = VIDEO_MODE_AUTO;
			v->signal = 0xFFFF * zol_getsigstr(zol);
			return 0;
		}
	case VIDIOCSTUNER:
		{
			struct video_tuner *v = arg;
			if (v->tuner != 0)
				return -EINVAL;
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
	case VIDIOCGFREQ:
	{
		unsigned long *freq = arg;
		*freq = zol->curfreq;
		return 0;
	}
	case VIDIOCSFREQ:
	{
		unsigned long *freq = arg;
		zol->curfreq = *freq;
		zol_setfreq(zol, zol->curfreq);
		return 0;
	}
	case VIDIOCGAUDIO:
		{
			struct video_audio *v = arg;
			memset(v, 0, sizeof(*v));
			v->flags |= VIDEO_AUDIO_MUTABLE | VIDEO_AUDIO_VOLUME;
			v->mode |= zol_is_stereo(zol)
				? VIDEO_SOUND_STEREO : VIDEO_SOUND_MONO;
			v->volume = zol->curvol * 4096;
			v->step = 4096;
			strcpy(v->name, "Zoltrix Radio");
			return 0;
		}
	case VIDIOCSAUDIO:
		{
			struct video_audio *v = arg;
			if (v->audio)
				return -EINVAL;

			if (v->flags & VIDEO_AUDIO_MUTE)
				zol_mute(zol);
			else {
				zol_unmute(zol);
				zol_setvol(zol, v->volume / 4096);
			}

			if (v->mode & VIDEO_SOUND_STEREO) {
				zol->stereo = 1;
				zol_setfreq(zol, zol->curfreq);
			}
			if (v->mode & VIDEO_SOUND_MONO) {
				zol->stereo = 0;
				zol_setfreq(zol, zol->curfreq);
			}
			return 0;
		}
	default:
		return -ENOIOCTLCMD;
	}
}

static int zol_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, zol_do_ioctl);
}

static struct zol_device zoltrix_unit;

static struct file_operations zoltrix_fops =
{
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= zol_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device zoltrix_radio =
{
	.owner		= THIS_MODULE,
	.name		= "Zoltrix Radio Plus",
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_ZOLTRIX,
	.fops           = &zoltrix_fops,
};

static int __init zoltrix_init(void)
{
	if (io == -1) {
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}
	if ((io != 0x20c) && (io != 0x30c)) {
		printk(KERN_ERR "zoltrix: invalid port, try 0x20c or 0x30c\n");
		return -ENXIO;
	}

	zoltrix_radio.priv = &zoltrix_unit;
	if (!request_region(io, 2, "zoltrix")) {
		printk(KERN_ERR "zoltrix: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	if (video_register_device(&zoltrix_radio, VFL_TYPE_RADIO, radio_nr) == -1)
	{
		release_region(io, 2);
		return -EINVAL;
	}
	printk(KERN_INFO "Zoltrix Radio Plus card driver.\n");

	mutex_init(&zoltrix_unit.lock);

	/* mute card - prevents noisy bootups */

	/* this ensures that the volume is all the way down  */

	outb(0, io);
	outb(0, io);
	msleep(20);
	inb(io + 3);

	zoltrix_unit.curvol = 0;
	zoltrix_unit.stereo = 1;

	return 0;
}

MODULE_AUTHOR("C.van Schaik");
MODULE_DESCRIPTION("A driver for the Zoltrix Radio Plus.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the Zoltrix Radio Plus (0x20c or 0x30c)");
module_param(radio_nr, int, 0);

static void __exit zoltrix_cleanup_module(void)
{
	video_unregister_device(&zoltrix_radio);
	release_region(io, 2);
}

module_init(zoltrix_init);
module_exit(zoltrix_cleanup_module);

