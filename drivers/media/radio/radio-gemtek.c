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
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev.h>	/* kernel radio structs		*/
#include <linux/config.h>	/* CONFIG_RADIO_GEMTEK_PORT 	*/
#include <linux/spinlock.h>

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

static int gemtek_do_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct gemtek_device *rt=dev->priv;

	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability *v = arg;
			memset(v,0,sizeof(*v));
			v->type=VID_TYPE_TUNER;
			v->channels=1;
			v->audios=1;
			strcpy(v->name, "GemTek");
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner)	/* Only 1 tuner */
				return -EINVAL;
			v->rangelow=87*16000;
			v->rangehigh=108*16000;
			v->flags=VIDEO_TUNER_LOW;
			v->mode=VIDEO_MODE_AUTO;
			v->signal=0xFFFF*gemtek_getsigstr(rt);
			strcpy(v->name, "FM");
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner!=0)
				return -EINVAL;
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
		case VIDIOCGFREQ:
		{
			unsigned long *freq = arg;
			*freq = rt->curfreq;
			return 0;
		}
		case VIDIOCSFREQ:
		{
			unsigned long *freq = arg;
			rt->curfreq = *freq;
			/* needs to be called twice in order for getsigstr to work */
			gemtek_setfreq(rt, rt->curfreq);
			gemtek_setfreq(rt, rt->curfreq);
			return 0;
		}
		case VIDIOCGAUDIO:
		{
			struct video_audio *v = arg;
			memset(v,0, sizeof(*v));
			v->flags|=VIDEO_AUDIO_MUTABLE;
			v->volume=1;
			v->step=65535;
			strcpy(v->name, "Radio");
			return 0;
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio *v = arg;
			if(v->audio)
				return -EINVAL;

			if(v->flags&VIDEO_AUDIO_MUTE)
				gemtek_mute(rt);
			else
				gemtek_unmute(rt);

			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int gemtek_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, gemtek_do_ioctl);
}

static struct gemtek_device gemtek_unit;

static struct file_operations gemtek_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= gemtek_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device gemtek_radio=
{
	.owner		= THIS_MODULE,
	.name		= "GemTek radio",
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_GEMTEK,
	.fops           = &gemtek_fops,
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
