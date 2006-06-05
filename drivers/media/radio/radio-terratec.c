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
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev.h>	/* kernel radio structs		*/
#include <media/v4l2-common.h>
#include <linux/config.h>	/* CONFIG_RADIO_TERRATEC_PORT 	*/
#include <linux/spinlock.h>

#ifndef CONFIG_RADIO_TERRATEC_PORT
#define CONFIG_RADIO_TERRATEC_PORT 0x590
#endif

/**************** this ones are for the terratec *******************/
#define BASEPORT 	0x590
#define VOLPORT 	0x591
#define WRT_DIS 	0x00
#define CLK_OFF		0x00
#define IIC_DATA	0x01
#define IIC_CLK		0x02
#define DATA		0x04
#define CLK_ON 		0x08
#define WRT_EN		0x10
/*******************************************************************/

static int io = CONFIG_RADIO_TERRATEC_PORT;
static int radio_nr = -1;
static spinlock_t lock;

struct tt_device
{
	int port;
	int curvol;
	unsigned long curfreq;
	int muted;
};


/* local things */

static void cardWriteVol(int volume)
{
	int i;
	volume = volume+(volume * 32); // change both channels
	spin_lock(&lock);
	for (i=0;i<8;i++)
	{
		if (volume & (0x80>>i))
			outb(0x80, VOLPORT);
		else outb(0x00, VOLPORT);
	}
	spin_unlock(&lock);
}



static void tt_mute(struct tt_device *dev)
{
	dev->muted = 1;
	cardWriteVol(0);
}

static int tt_setvol(struct tt_device *dev, int vol)
{

//	printk(KERN_ERR "setvol called, vol = %d\n", vol);

	if(vol == dev->curvol) {	/* requested volume = current */
		if (dev->muted) {	/* user is unmuting the card  */
			dev->muted = 0;
			cardWriteVol(vol);	/* enable card */
		}

		return 0;
	}

	if(vol == 0) {			/* volume = 0 means mute the card */
		cardWriteVol(0);	/* "turn off card" by setting vol to 0 */
		dev->curvol = vol;	/* track the volume state!	*/
		return 0;
	}

	dev->muted = 0;

	cardWriteVol(vol);

	dev->curvol = vol;

	return 0;

}


/* this is the worst part in this driver */
/* many more or less strange things are going on here, but hey, it works :) */

static int tt_setfreq(struct tt_device *dev, unsigned long freq1)
{
	int freq;
	int i;
	int p;
	int  temp;
	long rest;

	unsigned char buffer[25];		/* we have to bit shift 25 registers */
	freq = freq1/160;			/* convert the freq. to a nice to handle value */
	for(i=24;i>-1;i--)
		buffer[i]=0;

	rest = freq*10+10700;		/* i once had understood what is going on here */
					/* maybe some wise guy (friedhelm?) can comment this stuff */
	i=13;
	p=10;
	temp=102400;
	while (rest!=0)
	{
		if (rest%temp  == rest)
			buffer[i] = 0;
		else
		{
			buffer[i] = 1;
			rest = rest-temp;
		}
		i--;
		p--;
		temp = temp/2;
       }

	spin_lock(&lock);

	for (i=24;i>-1;i--)			/* bit shift the values to the radiocard */
	{
		if (buffer[i]==1)
		{
			outb(WRT_EN|DATA, BASEPORT);
			outb(WRT_EN|DATA|CLK_ON  , BASEPORT);
			outb(WRT_EN|DATA, BASEPORT);
		}
		else
		{
			outb(WRT_EN|0x00, BASEPORT);
			outb(WRT_EN|0x00|CLK_ON  , BASEPORT);
		}
	}
	outb(0x00, BASEPORT);

	spin_unlock(&lock);

	return 0;
}

static int tt_getsigstr(struct tt_device *dev)		/* TODO */
{
	if (inb(io) & 2)	/* bit set = no signal present	*/
		return 0;
	return 1;		/* signal present		*/
}


/* implement the video4linux api */

static int tt_do_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct tt_device *tt=dev->priv;

	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability *v = arg;
			memset(v,0,sizeof(*v));
			v->type=VID_TYPE_TUNER;
			v->channels=1;
			v->audios=1;
			strcpy(v->name, "ActiveRadio");
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner)	/* Only 1 tuner */
				return -EINVAL;
			v->rangelow=(87*16000);
			v->rangehigh=(108*16000);
			v->flags=VIDEO_TUNER_LOW;
			v->mode=VIDEO_MODE_AUTO;
			strcpy(v->name, "FM");
			v->signal=0xFFFF*tt_getsigstr(tt);
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
			*freq = tt->curfreq;
			return 0;
		}
		case VIDIOCSFREQ:
		{
			unsigned long *freq = arg;
			tt->curfreq = *freq;
			tt_setfreq(tt, tt->curfreq);
			return 0;
		}
		case VIDIOCGAUDIO:
		{
			struct video_audio *v = arg;
			memset(v,0, sizeof(*v));
			v->flags|=VIDEO_AUDIO_MUTABLE|VIDEO_AUDIO_VOLUME;
			v->volume=tt->curvol * 6554;
			v->step=6554;
			strcpy(v->name, "Radio");
			return 0;
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio *v = arg;
			if(v->audio)
				return -EINVAL;
			if(v->flags&VIDEO_AUDIO_MUTE)
				tt_mute(tt);
			else
				tt_setvol(tt,v->volume/6554);
			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int tt_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, tt_do_ioctl);
}

static struct tt_device terratec_unit;

static struct file_operations terratec_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= tt_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device terratec_radio=
{
	.owner		= THIS_MODULE,
	.name		= "TerraTec ActiveRadio",
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_TERRATEC,
	.fops           = &terratec_fops,
};

static int __init terratec_init(void)
{
	if(io==-1)
	{
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}
	if (!request_region(io, 2, "terratec"))
	{
		printk(KERN_ERR "TerraTec: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	terratec_radio.priv=&terratec_unit;

	spin_lock_init(&lock);

	if(video_register_device(&terratec_radio, VFL_TYPE_RADIO, radio_nr)==-1)
	{
		release_region(io,2);
		return -EINVAL;
	}

	printk(KERN_INFO "TERRATEC ActivRadio Standalone card driver.\n");

	/* mute card - prevents noisy bootups */

	/* this ensures that the volume is all the way down  */
	cardWriteVol(0);
	terratec_unit.curvol = 0;

	return 0;
}

MODULE_AUTHOR("R.OFFERMANNS & others");
MODULE_DESCRIPTION("A driver for the TerraTec ActiveRadio Standalone radio card.");
MODULE_LICENSE("GPL");
module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the TerraTec ActiveRadio card (0x590 or 0x591)");
module_param(radio_nr, int, 0);

static void __exit terratec_cleanup_module(void)
{
	video_unregister_device(&terratec_radio);
	release_region(io,2);
	printk(KERN_INFO "TERRATEC ActivRadio Standalone card driver unloaded.\n");
}

module_init(terratec_init);
module_exit(terratec_cleanup_module);

