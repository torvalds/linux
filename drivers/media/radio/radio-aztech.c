/* radio-aztech.c - Aztech radio card driver for Linux 2.2 
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
 * The basis for this code may be found at http://bigbang.vtc.vsc.edu/fmradio/
 * along with more information on the card itself.
 *
 * History:
 * 1999-02-24	Russell Kroll <rkroll@exploits.org>
 *		Fine tuning/VIDEO_TUNER_LOW
 * 		Range expanded to 87-108 MHz (from 87.9-107.8)
 *
 * Notable changes from the original source:
 * - includes stripped down to the essentials
 * - for loops used as delays replaced with udelay()
 * - #defines removed, changed to static values
 * - tuning structure changed - no more character arrays, other changes
*/

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* check_region, request_region	*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev.h>	/* kernel radio structs		*/
#include <linux/config.h>	/* CONFIG_RADIO_AZTECH_PORT 	*/

/* acceptable ports: 0x350 (JP3 shorted), 0x358 (JP3 open) */

#ifndef CONFIG_RADIO_AZTECH_PORT
#define CONFIG_RADIO_AZTECH_PORT -1
#endif

static int io = CONFIG_RADIO_AZTECH_PORT; 
static int radio_nr = -1;
static int radio_wait_time = 1000;
static struct semaphore lock;

struct az_device
{
	int curvol;
	unsigned long curfreq;
	int stereo;
};

static int volconvert(int level)
{
	level>>=14;	 	/* Map 16bits down to 2 bit */
 	level&=3;
	
	/* convert to card-friendly values */
	switch (level) 
	{
		case 0: 
			return 0;
		case 1: 
			return 1;
		case 2:
			return 4;
		case 3:
			return 5;
	}
	return 0;	/* Quieten gcc */
}

static void send_0_byte (struct az_device *dev)
{
	udelay(radio_wait_time);
	outb_p(2+volconvert(dev->curvol), io);
	outb_p(64+2+volconvert(dev->curvol), io);
}

static void send_1_byte (struct az_device *dev)
{
	udelay (radio_wait_time);
	outb_p(128+2+volconvert(dev->curvol), io);
	outb_p(128+64+2+volconvert(dev->curvol), io);
}

static int az_setvol(struct az_device *dev, int vol)
{
	down(&lock);
	outb (volconvert(vol), io);
	up(&lock);
	return 0;
}

/* thanks to Michael Dwyer for giving me a dose of clues in
 * the signal strength department..
 *
 * This card has a stereo bit - bit 0 set = mono, not set = stereo
 * It also has a "signal" bit - bit 1 set = bad signal, not set = good
 *
 */

static int az_getsigstr(struct az_device *dev)
{
	if (inb(io) & 2)	/* bit set = no signal present */
		return 0;
	return 1;		/* signal present */
}

static int az_getstereo(struct az_device *dev)
{
	if (inb(io) & 1) 	/* bit set = mono */
		return 0;
	return 1;		/* stereo */
}

static int az_setfreq(struct az_device *dev, unsigned long frequency)
{
	int  i;

	frequency += 171200;		/* Add 10.7 MHz IF		*/
	frequency /= 800;		/* Convert to 50 kHz units	*/
					
	down(&lock);
	
	send_0_byte (dev);		/*  0: LSB of frequency       */

	for (i = 0; i < 13; i++)	/*   : frequency bits (1-13)  */
		if (frequency & (1 << i))
			send_1_byte (dev);
		else
			send_0_byte (dev);

	send_0_byte (dev);		/* 14: test bit - always 0    */
	send_0_byte (dev);		/* 15: test bit - always 0    */
	send_0_byte (dev);		/* 16: band data 0 - always 0 */
	if (dev->stereo)		/* 17: stereo (1 to enable)   */
		send_1_byte (dev);
	else
		send_0_byte (dev);

	send_1_byte (dev);		/* 18: band data 1 - unknown  */
	send_0_byte (dev);		/* 19: time base - always 0   */
	send_0_byte (dev);		/* 20: spacing (0 = 25 kHz)   */
	send_1_byte (dev);		/* 21: spacing (1 = 25 kHz)   */
	send_0_byte (dev);		/* 22: spacing (0 = 25 kHz)   */
	send_1_byte (dev);		/* 23: AM/FM (FM = 1, always) */

	/* latch frequency */

	udelay (radio_wait_time);
	outb_p(128+64+volconvert(dev->curvol), io);
	
	up(&lock);

	return 0;
}

static int az_do_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct az_device *az = dev->priv;
	
	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability *v = arg;
			memset(v,0,sizeof(*v));
			v->type=VID_TYPE_TUNER;
			v->channels=1;
			v->audios=1;
			strcpy(v->name, "Aztech Radio");
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
			v->signal=0xFFFF*az_getsigstr(az);
			if(az_getstereo(az))
				v->flags|=VIDEO_TUNER_STEREO_ON;
			strcpy(v->name, "FM");
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner!=0)
				return -EINVAL;
			return 0;
		}
		case VIDIOCGFREQ:
		{
			unsigned long *freq = arg;
			*freq = az->curfreq;
			return 0;
		}
		case VIDIOCSFREQ:
		{
			unsigned long *freq = arg;
			az->curfreq = *freq;
			az_setfreq(az, az->curfreq);
			return 0;
		}
		case VIDIOCGAUDIO:
		{	
			struct video_audio *v = arg;
			memset(v,0, sizeof(*v));
			v->flags|=VIDEO_AUDIO_MUTABLE|VIDEO_AUDIO_VOLUME;
			if(az->stereo)
				v->mode=VIDEO_SOUND_STEREO;
			else
				v->mode=VIDEO_SOUND_MONO;
			v->volume=az->curvol;
			v->step=16384;
			strcpy(v->name, "Radio");
			return 0;			
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio *v = arg;
			if(v->audio) 
				return -EINVAL;
			az->curvol=v->volume;

			az->stereo=(v->mode&VIDEO_SOUND_STEREO)?1:0;
			if(v->flags&VIDEO_AUDIO_MUTE) 
				az_setvol(az,0);
			else
				az_setvol(az,az->curvol);
			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int az_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, az_do_ioctl);
}

static struct az_device aztech_unit;

static struct file_operations aztech_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= az_ioctl,
	.llseek         = no_llseek,
};

static struct video_device aztech_radio=
{
	.owner		= THIS_MODULE,
	.name		= "Aztech radio",
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_AZTECH,
	.fops           = &aztech_fops,
};

static int __init aztech_init(void)
{
	if(io==-1)
	{
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}

	if (!request_region(io, 2, "aztech")) 
	{
		printk(KERN_ERR "aztech: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	init_MUTEX(&lock);
	aztech_radio.priv=&aztech_unit;
	
	if(video_register_device(&aztech_radio, VFL_TYPE_RADIO, radio_nr)==-1)
	{
		release_region(io,2);
		return -EINVAL;
	}
		
	printk(KERN_INFO "Aztech radio card driver v1.00/19990224 rkroll@exploits.org\n");
	/* mute card - prevents noisy bootups */
	outb (0, io);
	return 0;
}

MODULE_AUTHOR("Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the Aztech radio card.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(io, "I/O address of the Aztech card (0x350 or 0x358)");

static void __exit aztech_cleanup(void)
{
	video_unregister_device(&aztech_radio);
	release_region(io,2);
}

module_init(aztech_init);
module_exit(aztech_cleanup);
