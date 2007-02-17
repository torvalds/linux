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
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <media/v4l2-common.h>
#include <linux/mutex.h>

static struct mutex lock;

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
		.step          = 1<<12,
		.default_value = 0xff,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}
};

#undef DEBUG
//#define DEBUG 1

#ifdef DEBUG
# define  debug_print(s) printk s
#else
# define  debug_print(s)
#endif

/* this should be static vars for module size */
struct fmr2_device
{
	int port;
	int curvol; /* 0-65535, if not volume 0 or 65535 */
	int mute;
	int stereo; /* card is producing stereo audio */
	unsigned long curfreq; /* freq in kHz */
	int card_type;
	__u32 flags;
};

static int io = 0x384;
static int radio_nr = -1;

/* hw precision is 12.5 kHz
 * It is only useful to give freq in intervall of 200 (=0.0125Mhz),
 * other bits will be truncated
 */
#define RSF16_ENCODE(x)	((x)/200+856)
#define RSF16_MINFREQ 87*16000
#define RSF16_MAXFREQ 108*16000

static inline void wait(int n,int port)
{
	for (;n;--n) inb(port);
}

static void outbits(int bits, unsigned int data, int nWait, int port)
{
	int bit;
	for(;--bits>=0;) {
		bit = (data>>bits) & 1;
		outb(bit,port);
		wait(nWait,port);
		outb(bit|2,port);
		wait(nWait,port);
		outb(bit,port);
		wait(nWait,port);
	}
}

static inline void fmr2_mute(int port)
{
	outb(0x00, port);
	wait(4,port);
}

static inline void fmr2_unmute(int port)
{
	outb(0x04, port);
	wait(4,port);
}

static inline int fmr2_stereo_mode(int port)
{
	int n = inb(port);
	outb(6,port);
	inb(port);
	n = ((n>>3)&1)^1;
	debug_print((KERN_DEBUG "stereo: %d\n", n));
	return n;
}

static int fmr2_product_info(struct fmr2_device *dev)
{
	int n = inb(dev->port);
	n &= 0xC1;
	if (n == 0)
	{
		/* this should support volume set */
		dev->card_type = 12;
		return 0;
	}
	/* not volume (mine is 11) */
	dev->card_type = (n==128)?11:0;
	return n;
}

static inline int fmr2_getsigstr(struct fmr2_device *dev)
{
	/* !!! work only if scanning freq */
	int port = dev->port, res = 0xffff;
	outb(5,port);
	wait(4,port);
	if (!(inb(port)&1)) res = 0;
	debug_print((KERN_DEBUG "signal: %d\n", res));
	return res;
}

/* set frequency and unmute card */
static int fmr2_setfreq(struct fmr2_device *dev)
{
	int port = dev->port;
	unsigned long freq = dev->curfreq;

	fmr2_mute(port);

	/* 0x42 for mono output
	 * 0x102 forward scanning
	 * 0x182 scansione avanti
	 */
	outbits(9,0x2,3,port);
	outbits(16,RSF16_ENCODE(freq),2,port);

	fmr2_unmute(port);

	/* wait 0.11 sec */
	msleep(110);

	/* NOTE if mute this stop radio
	   you must set freq on unmute */
	dev->stereo = fmr2_stereo_mode(port);
	return 0;
}

/* !!! not tested, in my card this does't work !!! */
static int fmr2_setvolume(struct fmr2_device *dev)
{
	int i,a,n, port = dev->port;

	if (dev->card_type != 11) return 1;

	switch( (dev->curvol+(1<<11)) >> 12 )
	{
	case 0: case 1: n = 0x21; break;
	case 2: n = 0x84; break;
	case 3: n = 0x90; break;
	case 4: n = 0x104; break;
	case 5: n = 0x110; break;
	case 6: n = 0x204; break;
	case 7: n = 0x210; break;
	case 8: n = 0x402; break;
	case 9: n = 0x404; break;
	default:
	case 10: n = 0x408; break;
	case 11: n = 0x410; break;
	case 12: n = 0x801; break;
	case 13: n = 0x802; break;
	case 14: n = 0x804; break;
	case 15: n = 0x808; break;
	case 16: n = 0x810; break;
	}
	for(i=12;--i>=0;)
	{
		a = ((n >> i) & 1) << 6; /* if (a=0) a= 0; else a= 0x40; */
		outb(a|4, port);
		wait(4,port);
		outb(a|0x24, port);
		wait(4,port);
		outb(a|4, port);
		wait(4,port);
	}
	for(i=6;--i>=0;)
	{
		a = ((0x18 >> i) & 1) << 6;
		outb(a|4, port);
		wait(4,port);
		outb(a|0x24, port);
		wait(4,port);
		outb(a|4, port);
		wait(4,port);
	}
	wait(4,port);
	outb(0x14, port);

	return 0;
}

static int fmr2_do_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct fmr2_device *fmr2 = dev->priv;
	debug_print((KERN_DEBUG "freq %ld flags %d vol %d mute %d "
		"stereo %d type %d\n",
		fmr2->curfreq, fmr2->flags, fmr2->curvol, fmr2->mute,
		fmr2->stereo, fmr2->card_type));

	switch(cmd)
	{
		case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *v = arg;
			memset(v,0,sizeof(*v));
			strlcpy(v->driver, "radio-sf16fmr2", sizeof (v->driver));
			strlcpy(v->card, "SF16-FMR2 radio", sizeof (v->card));
			sprintf(v->bus_info,"ISA");
			v->version = RADIO_VERSION;
			v->capabilities = V4L2_CAP_TUNER;

			return 0;
		}
		case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *v = arg;
			int mult;

			if (v->index > 0)
				return -EINVAL;

			memset(v,0,sizeof(*v));
			strcpy(v->name, "FM");
			v->type = V4L2_TUNER_RADIO;

			mult = (fmr2->flags & V4L2_TUNER_CAP_LOW) ? 1 : 1000;
			v->rangelow = RSF16_MINFREQ/mult;
			v->rangehigh = RSF16_MAXFREQ/mult;
			v->rxsubchans =V4L2_TUNER_SUB_MONO | V4L2_TUNER_MODE_STEREO;
			v->capability=fmr2->flags&V4L2_TUNER_CAP_LOW;

			v->audmode = fmr2->stereo ? V4L2_TUNER_MODE_STEREO:
						    V4L2_TUNER_MODE_MONO;
			mutex_lock(&lock);
			v->signal = fmr2_getsigstr(fmr2);
			mutex_unlock(&lock);

			return 0;
		}
		case VIDIOC_S_TUNER:
		{
			struct v4l2_tuner *v = arg;

			if (v->index > 0)
				return -EINVAL;

			return 0;
		}
		case VIDIOC_S_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			if (!(fmr2->flags & V4L2_TUNER_CAP_LOW))
				f->frequency *= 1000;
			if (f->frequency < RSF16_MINFREQ ||
					f->frequency > RSF16_MAXFREQ )
				return -EINVAL;
			/*rounding in steps of 200 to match th freq
			  that will be used */
			fmr2->curfreq = (f->frequency/200)*200;

			/* set card freq (if not muted) */
			if (fmr2->curvol && !fmr2->mute)
			{
				mutex_lock(&lock);
				fmr2_setfreq(fmr2);
				mutex_unlock(&lock);
			}

			return 0;
		}
		case VIDIOC_G_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			f->type = V4L2_TUNER_RADIO;
			f->frequency = fmr2->curfreq;
			if (!(fmr2->flags & V4L2_TUNER_CAP_LOW))
				f->frequency /= 1000;

			return 0;
		}
		case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *qc = arg;
			int i;

			for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
				if ((fmr2->card_type != 11)
						&& V4L2_CID_AUDIO_VOLUME)
					radio_qctrl[i].step=65535;
				if (qc->id && qc->id == radio_qctrl[i].id) {
					memcpy(qc, &(radio_qctrl[i]),
								sizeof(*qc));
					return (0);
				}
			}
			return -EINVAL;
		}
		case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl= arg;

			switch (ctrl->id) {
				case V4L2_CID_AUDIO_MUTE:
					ctrl->value=fmr2->mute;
					return (0);
				case V4L2_CID_AUDIO_VOLUME:
					ctrl->value=fmr2->curvol;
					return (0);
			}
			return -EINVAL;
		}
		case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl= arg;

			switch (ctrl->id) {
				case V4L2_CID_AUDIO_MUTE:
					fmr2->mute=ctrl->value;
					if (fmr2->card_type != 11) {
						if (!fmr2->mute) {
							fmr2->curvol = 65535;
						} else {
							fmr2->curvol = 0;
						}
					}
					break;
				case V4L2_CID_AUDIO_VOLUME:
					fmr2->curvol = ctrl->value;
					if (fmr2->card_type != 11) {
						if (fmr2->curvol) {
							fmr2->curvol = 65535;
							fmr2->mute = 0;
						} else {
							fmr2->curvol = 0;
							fmr2->mute = 1;
						}
					}
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
			mutex_lock(&lock);
			if (fmr2->curvol && !fmr2->mute) {
				fmr2_setvolume(fmr2);
				fmr2_setfreq(fmr2);
			} else
				fmr2_mute(fmr2->port);
			mutex_unlock(&lock);
			return (0);
		}
		default:
			return v4l_compat_translate_ioctl(inode,file,cmd,arg,
							  fmr2_do_ioctl);

	}
}

static int fmr2_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
 {
	return video_usercopy(inode, file, cmd, arg, fmr2_do_ioctl);
}

static struct fmr2_device fmr2_unit;

static const struct file_operations fmr2_fops = {
	.owner          = THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl          = fmr2_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device fmr2_radio=
{
	.owner		= THIS_MODULE,
	.name		= "SF16FMR2 radio",
	. type		= VID_TYPE_TUNER,
	.hardware	= 0,
	.fops		= &fmr2_fops,
};

static int __init fmr2_init(void)
{
	fmr2_unit.port = io;
	fmr2_unit.curvol = 0;
	fmr2_unit.mute = 0;
	fmr2_unit.curfreq = 0;
	fmr2_unit.stereo = 1;
	fmr2_unit.flags = V4L2_TUNER_CAP_LOW;
	fmr2_unit.card_type = 0;
	fmr2_radio.priv = &fmr2_unit;

	mutex_init(&lock);

	if (request_region(io, 2, "sf16fmr2"))
	{
		printk(KERN_ERR "fmr2: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	if(video_register_device(&fmr2_radio, VFL_TYPE_RADIO, radio_nr)==-1)
	{
		release_region(io, 2);
		return -EINVAL;
	}

	printk(KERN_INFO "SF16FMR2 radio card driver at 0x%x.\n", io);
	/* mute card - prevents noisy bootups */
	mutex_lock(&lock);
	fmr2_mute(io);
	fmr2_product_info(&fmr2_unit);
	mutex_unlock(&lock);
	debug_print((KERN_DEBUG "card_type %d\n", fmr2_unit.card_type));
	return 0;
}

MODULE_AUTHOR("Ziglio Frediano, freddy77@angelfire.com");
MODULE_DESCRIPTION("A driver for the SF16FMR2 radio.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the SF16FMR2 card (should be 0x384, if do not work try 0x284)");
module_param(radio_nr, int, 0);

static void __exit fmr2_cleanup_module(void)
{
	video_unregister_device(&fmr2_radio);
	release_region(io,2);
}

module_init(fmr2_init);
module_exit(fmr2_cleanup_module);

#ifndef MODULE

static int __init fmr2_setup_io(char *str)
{
	get_option(&str, &io);
	return 1;
}

__setup("sf16fmr2=", fmr2_setup_io);

#endif
