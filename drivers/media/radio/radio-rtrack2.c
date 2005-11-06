/* RadioTrack II driver for Linux radio support (C) 1998 Ben Pfaff
 * 
 * Based on RadioTrack I/RadioReveal (C) 1997 M. Kirkwood
 * Converted to new API by Alan Cox <Alan.Cox@linux.org>
 * Various bugfixes and enhancements by Russell Kroll <rkroll@exploits.org>
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
#include <linux/config.h>	/* CONFIG_RADIO_RTRACK2_PORT 	*/
#include <linux/spinlock.h>

#ifndef CONFIG_RADIO_RTRACK2_PORT
#define CONFIG_RADIO_RTRACK2_PORT -1
#endif

static int io = CONFIG_RADIO_RTRACK2_PORT; 
static int radio_nr = -1;
static spinlock_t lock;

struct rt_device
{
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

static int rt_getsigstr(struct rt_device *dev)
{
	if (inb(io) & 2)	/* bit set = no signal present	*/
		return 0;
	return 1;		/* signal present		*/
}

static int rt_do_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct rt_device *rt=dev->priv;

	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability *v = arg;
			memset(v,0,sizeof(*v));
			v->type=VID_TYPE_TUNER;
			v->channels=1;
			v->audios=1;
			strcpy(v->name, "RadioTrack II");
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner)	/* Only 1 tuner */ 
				return -EINVAL;
			v->rangelow=88*16000;
			v->rangehigh=108*16000;
			v->flags=VIDEO_TUNER_LOW;
			v->mode=VIDEO_MODE_AUTO;
			v->signal=0xFFFF*rt_getsigstr(rt);
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
			rt_setfreq(rt, rt->curfreq);
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
				rt_mute(rt);
			else
			        rt_unmute(rt);

			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int rt_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, rt_do_ioctl);
}

static struct rt_device rtrack2_unit;

static struct file_operations rtrack2_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= rt_ioctl,
	.llseek         = no_llseek,
};

static struct video_device rtrack2_radio=
{
	.owner		= THIS_MODULE,
	.name		= "RadioTrack II radio",
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_RTRACK2,
	.fops           = &rtrack2_fops,
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

	rtrack2_radio.priv=&rtrack2_unit;

	spin_lock_init(&lock);	
	if(video_register_device(&rtrack2_radio, VFL_TYPE_RADIO, radio_nr)==-1)
	{
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
