/* Typhoon Radio Card driver for radio support
 * (c) 1999 Dr. Henrik Seidel <Henrik.Seidel@gmx.de>
 *
 * Card manufacturer:
 * http://194.18.155.92/idc/prod2.idc?nr=50753&lang=e
 *
 * Notes on the hardware
 *
 * This card has two output sockets, one for speakers and one for line.
 * The speaker output has volume control, but only in four discrete
 * steps. The line output has neither volume control nor mute.
 *
 * The card has auto-stereo according to its manual, although it all
 * sounds mono to me (even with the Win/DOS drivers). Maybe it's my
 * antenna - I really don't know for sure.
 *
 * Frequency control is done digitally.
 *
 * Volume control is done digitally, but there are only four different
 * possible values. So you should better always turn the volume up and
 * use line control. I got the best results by connecting line output
 * to the sound card microphone input. For such a configuration the
 * volume control has no effect, since volume control only influences
 * the speaker output.
 *
 * There is no explicit mute/unmute. So I set the radio frequency to a
 * value where I do expect just noise and turn the speaker volume down.
 * The frequency change is necessary since the card never seems to be
 * completely silent.
 */

#include <linux/module.h>	/* Modules                        */
#include <linux/init.h>		/* Initdata                       */
#include <linux/ioport.h>	/* request_region		  */
#include <linux/proc_fs.h>	/* radio card status report	  */
#include <asm/io.h>		/* outb, outb_p                   */
#include <asm/uaccess.h>	/* copy to/from user              */
#include <linux/videodev.h>	/* kernel radio structs           */
#include <linux/config.h>	/* CONFIG_RADIO_TYPHOON_*         */

#define BANNER "Typhoon Radio Card driver v0.1\n"

#ifndef CONFIG_RADIO_TYPHOON_PORT
#define CONFIG_RADIO_TYPHOON_PORT -1
#endif

#ifndef CONFIG_RADIO_TYPHOON_MUTEFREQ
#define CONFIG_RADIO_TYPHOON_MUTEFREQ 0
#endif

#ifndef CONFIG_PROC_FS
#undef CONFIG_RADIO_TYPHOON_PROC_FS
#endif

struct typhoon_device {
	int users;
	int iobase;
	int curvol;
	int muted;
	unsigned long curfreq;
	unsigned long mutefreq;
	struct semaphore lock;
};

static void typhoon_setvol_generic(struct typhoon_device *dev, int vol);
static int typhoon_setfreq_generic(struct typhoon_device *dev,
				   unsigned long frequency);
static int typhoon_setfreq(struct typhoon_device *dev, unsigned long frequency);
static void typhoon_mute(struct typhoon_device *dev);
static void typhoon_unmute(struct typhoon_device *dev);
static int typhoon_setvol(struct typhoon_device *dev, int vol);
static int typhoon_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg);
#ifdef CONFIG_RADIO_TYPHOON_PROC_FS
static int typhoon_get_info(char *buf, char **start, off_t offset, int len);
#endif

static void typhoon_setvol_generic(struct typhoon_device *dev, int vol)
{
	down(&dev->lock);
	vol >>= 14;				/* Map 16 bit to 2 bit */
	vol &= 3;
	outb_p(vol / 2, dev->iobase);		/* Set the volume, high bit. */
	outb_p(vol % 2, dev->iobase + 2);	/* Set the volume, low bit. */
	up(&dev->lock);
}

static int typhoon_setfreq_generic(struct typhoon_device *dev,
				   unsigned long frequency)
{
	unsigned long outval;
	unsigned long x;

	/*
	 * The frequency transfer curve is not linear. The best fit I could
	 * get is
	 *
	 * outval = -155 + exp((f + 15.55) * 0.057))
	 *
	 * where frequency f is in MHz. Since we don't have exp in the kernel,
	 * I approximate this function by a third order polynomial.
	 *
	 */

	down(&dev->lock);
	x = frequency / 160;
	outval = (x * x + 2500) / 5000;
	outval = (outval * x + 5000) / 10000;
	outval -= (10 * x * x + 10433) / 20866;
	outval += 4 * x - 11505;

	outb_p((outval >> 8) & 0x01, dev->iobase + 4);
	outb_p(outval >> 9, dev->iobase + 6);
	outb_p(outval & 0xff, dev->iobase + 8);
	up(&dev->lock);

	return 0;
}

static int typhoon_setfreq(struct typhoon_device *dev, unsigned long frequency)
{
	typhoon_setfreq_generic(dev, frequency);
	dev->curfreq = frequency;
	return 0;
}

static void typhoon_mute(struct typhoon_device *dev)
{
	if (dev->muted == 1)
		return;
	typhoon_setvol_generic(dev, 0);
	typhoon_setfreq_generic(dev, dev->mutefreq);
	dev->muted = 1;
}

static void typhoon_unmute(struct typhoon_device *dev)
{
	if (dev->muted == 0)
		return;
	typhoon_setfreq_generic(dev, dev->curfreq);
	typhoon_setvol_generic(dev, dev->curvol);
	dev->muted = 0;
}

static int typhoon_setvol(struct typhoon_device *dev, int vol)
{
	if (dev->muted && vol != 0) {	/* user is unmuting the card */
		dev->curvol = vol;
		typhoon_unmute(dev);
		return 0;
	}
	if (vol == dev->curvol)		/* requested volume == current */
		return 0;

	if (vol == 0) {			/* volume == 0 means mute the card */
		typhoon_mute(dev);
		dev->curvol = vol;
		return 0;
	}
	typhoon_setvol_generic(dev, vol);
	dev->curvol = vol;
	return 0;
}


static int typhoon_do_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct typhoon_device *typhoon = dev->priv;

	switch (cmd) {
	case VIDIOCGCAP:
		{
			struct video_capability *v = arg;
			memset(v,0,sizeof(*v));
			v->type = VID_TYPE_TUNER;
			v->channels = 1;
			v->audios = 1;
			strcpy(v->name, "Typhoon Radio");
			return 0;
		}
	case VIDIOCGTUNER:
		{
			struct video_tuner *v = arg;
			if (v->tuner)	/* Only 1 tuner */
				return -EINVAL;
			v->rangelow = 875 * 1600;
			v->rangehigh = 1080 * 1600;
			v->flags = VIDEO_TUNER_LOW;
			v->mode = VIDEO_MODE_AUTO;
			v->signal = 0xFFFF;	/* We can't get the signal strength */
			strcpy(v->name, "FM");
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
		*freq = typhoon->curfreq;
		return 0;
	}
	case VIDIOCSFREQ:
	{
		unsigned long *freq = arg;
		typhoon->curfreq = *freq;
		typhoon_setfreq(typhoon, typhoon->curfreq);
		return 0;
	}
	case VIDIOCGAUDIO:
		{
			struct video_audio *v = arg;
			memset(v, 0, sizeof(*v));
			v->flags |= VIDEO_AUDIO_MUTABLE | VIDEO_AUDIO_VOLUME;
			v->mode |= VIDEO_SOUND_MONO;
			v->volume = typhoon->curvol;
			v->step = 1 << 14;
			strcpy(v->name, "Typhoon Radio");
			return 0;
		}
	case VIDIOCSAUDIO:
		{
			struct video_audio *v = arg;
			if (v->audio)
				return -EINVAL;
			if (v->flags & VIDEO_AUDIO_MUTE)
				typhoon_mute(typhoon);
			else
				typhoon_unmute(typhoon);
			if (v->flags & VIDEO_AUDIO_VOLUME)
				typhoon_setvol(typhoon, v->volume);
			return 0;
		}
	default:
		return -ENOIOCTLCMD;
	}
}

static int typhoon_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, typhoon_do_ioctl);
}

static struct typhoon_device typhoon_unit =
{
	.iobase		= CONFIG_RADIO_TYPHOON_PORT,
	.curfreq	= CONFIG_RADIO_TYPHOON_MUTEFREQ,
	.mutefreq	= CONFIG_RADIO_TYPHOON_MUTEFREQ,
};

static struct file_operations typhoon_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= typhoon_ioctl,
	.llseek         = no_llseek,
};

static struct video_device typhoon_radio =
{
	.owner		= THIS_MODULE,
	.name		= "Typhoon Radio",
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_TYPHOON,
	.fops           = &typhoon_fops,
};

#ifdef CONFIG_RADIO_TYPHOON_PROC_FS

static int typhoon_get_info(char *buf, char **start, off_t offset, int len)
{
	char *out = buf;

	#ifdef MODULE
	    #define MODULEPROCSTRING "Driver loaded as a module"
	#else
	    #define MODULEPROCSTRING "Driver compiled into kernel"
	#endif

	/* output must be kept under PAGE_SIZE */
	out += sprintf(out, BANNER);
	out += sprintf(out, "Load type: " MODULEPROCSTRING "\n\n");
	out += sprintf(out, "frequency = %lu kHz\n",
		typhoon_unit.curfreq >> 4);
	out += sprintf(out, "volume = %d\n", typhoon_unit.curvol);
	out += sprintf(out, "mute = %s\n", typhoon_unit.muted ?
		"on" : "off");
	out += sprintf(out, "iobase = 0x%x\n", typhoon_unit.iobase);
	out += sprintf(out, "mute frequency = %lu kHz\n",
		typhoon_unit.mutefreq >> 4);
	return out - buf;
}

#endif /* CONFIG_RADIO_TYPHOON_PROC_FS */

MODULE_AUTHOR("Dr. Henrik Seidel");
MODULE_DESCRIPTION("A driver for the Typhoon radio card (a.k.a. EcoRadio).");
MODULE_LICENSE("GPL");

static int io = -1;
static int radio_nr = -1;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the Typhoon card (0x316 or 0x336)");
module_param(radio_nr, int, 0);

#ifdef MODULE
static unsigned long mutefreq = 0;
module_param(mutefreq, ulong, 0);
MODULE_PARM_DESC(mutefreq, "Frequency used when muting the card (in kHz)");
#endif

static int __init typhoon_init(void)
{
#ifdef MODULE
	if (io == -1) {
		printk(KERN_ERR "radio-typhoon: You must set an I/O address with io=0x316 or io=0x336\n");
		return -EINVAL;
	}
	typhoon_unit.iobase = io;

	if (mutefreq < 87000 || mutefreq > 108500) {
		printk(KERN_ERR "radio-typhoon: You must set a frequency (in kHz) used when muting the card,\n");
		printk(KERN_ERR "radio-typhoon: e.g. with \"mutefreq=87500\" (87000 <= mutefreq <= 108500)\n");
		return -EINVAL;
	}
	typhoon_unit.mutefreq = mutefreq;
#endif /* MODULE */

	printk(KERN_INFO BANNER);
	init_MUTEX(&typhoon_unit.lock);
	io = typhoon_unit.iobase;
	if (!request_region(io, 8, "typhoon")) {
		printk(KERN_ERR "radio-typhoon: port 0x%x already in use\n",
		       typhoon_unit.iobase);
		return -EBUSY;
	}

	typhoon_radio.priv = &typhoon_unit;
	if (video_register_device(&typhoon_radio, VFL_TYPE_RADIO, radio_nr) == -1)
	{
		release_region(io, 8);
		return -EINVAL;
	}
	printk(KERN_INFO "radio-typhoon: port 0x%x.\n", typhoon_unit.iobase);
	printk(KERN_INFO "radio-typhoon: mute frequency is %lu kHz.\n",
	       typhoon_unit.mutefreq);
	typhoon_unit.mutefreq <<= 4;

	/* mute card - prevents noisy bootups */
	typhoon_mute(&typhoon_unit);

#ifdef CONFIG_RADIO_TYPHOON_PROC_FS
	if (!create_proc_info_entry("driver/radio-typhoon", 0, NULL,
				    typhoon_get_info)) 
	    	printk(KERN_ERR "radio-typhoon: registering /proc/driver/radio-typhoon failed\n");
#endif

	return 0;
}

static void __exit typhoon_cleanup_module(void)
{

#ifdef CONFIG_RADIO_TYPHOON_PROC_FS
	remove_proc_entry("driver/radio-typhoon", NULL);
#endif

	video_unregister_device(&typhoon_radio);
	release_region(io, 8);
}

module_init(typhoon_init);
module_exit(typhoon_cleanup_module);

