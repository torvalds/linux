/*
 *	Media Vision Pro Movie Studio
 *			or
 *	"all you need is an I2C bus some RAM and a prayer"
 *
 *	This draws heavily on code
 *
 *	(c) Wolfgang Koehler,  wolf@first.gmd.de, Dec. 1994
 *	Kiefernring 15
 *	14478 Potsdam, Germany
 *
 *	Most of this code is directly derived from his userspace driver.
 *	His driver works so send any reports to alan@redhat.com unless the
 *	userspace driver also doesn't work for you...
 *
 *      Changes:
 *      08/07/2003        Daniele Bellucci <bellucda@tiscali.it>
 *                        - pms_capture: report back -EFAULT
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>


#define MOTOROLA	1
#define PHILIPS2	2
#define PHILIPS1	3
#define MVVMEMORYWIDTH	0x40		/* 512 bytes */

struct pms_device
{
	struct video_device v;
	struct video_picture picture;
	int height;
	int width;
	struct mutex lock;
};

struct i2c_info
{
	u8 slave;
	u8 sub;
	u8 data;
	u8 hits;
};

static int i2c_count 		= 0;
static struct i2c_info i2cinfo[64];

static int decoder 		= PHILIPS2;
static int standard 		= 0;	/* 0 - auto 1 - ntsc 2 - pal 3 - secam */

/*
 *	I/O ports and Shared Memory
 */

static int io_port		=	0x250;
static int data_port		=	0x251;
static int mem_base		=	0xC8000;
static void __iomem *mem;
static int video_nr             =       -1;



static inline void mvv_write(u8 index, u8 value)
{
	outw(index|(value<<8), io_port);
}

static inline u8 mvv_read(u8 index)
{
	outb(index, io_port);
	return inb(data_port);
}

static int pms_i2c_stat(u8 slave)
{
	int counter;
	int i;

	outb(0x28, io_port);

	counter=0;
	while((inb(data_port)&0x01)==0)
		if(counter++==256)
			break;

	while((inb(data_port)&0x01)!=0)
		if(counter++==256)
			break;

	outb(slave, io_port);

	counter=0;
	while((inb(data_port)&0x01)==0)
		if(counter++==256)
			break;

	while((inb(data_port)&0x01)!=0)
		if(counter++==256)
			break;

	for(i=0;i<12;i++)
	{
		char st=inb(data_port);
		if((st&2)!=0)
			return -1;
		if((st&1)==0)
			break;
	}
	outb(0x29, io_port);
	return inb(data_port);
}

static int pms_i2c_write(u16 slave, u16 sub, u16 data)
{
	int skip=0;
	int count;
	int i;

	for(i=0;i<i2c_count;i++)
	{
		if((i2cinfo[i].slave==slave) &&
		   (i2cinfo[i].sub == sub))
		{
			if(i2cinfo[i].data==data)
				skip=1;
			i2cinfo[i].data=data;
			i=i2c_count+1;
		}
	}

	if(i==i2c_count && i2c_count<64)
	{
		i2cinfo[i2c_count].slave=slave;
		i2cinfo[i2c_count].sub=sub;
		i2cinfo[i2c_count].data=data;
		i2c_count++;
	}

	if(skip)
		return 0;

	mvv_write(0x29, sub);
	mvv_write(0x2A, data);
	mvv_write(0x28, slave);

	outb(0x28, io_port);

	count=0;
	while((inb(data_port)&1)==0)
		if(count>255)
			break;
	while((inb(data_port)&1)!=0)
		if(count>255)
			break;

	count=inb(data_port);

	if(count&2)
		return -1;
	return count;
}

static int pms_i2c_read(int slave, int sub)
{
	int i=0;
	for(i=0;i<i2c_count;i++)
	{
		if(i2cinfo[i].slave==slave && i2cinfo[i].sub==sub)
			return i2cinfo[i].data;
	}
	return 0;
}


static void pms_i2c_andor(int slave, int sub, int and, int or)
{
	u8 tmp;

	tmp=pms_i2c_read(slave, sub);
	tmp = (tmp&and)|or;
	pms_i2c_write(slave, sub, tmp);
}

/*
 *	Control functions
 */


static void pms_videosource(short source)
{
	mvv_write(0x2E, source?0x31:0x30);
}

static void pms_hue(short hue)
{
	switch(decoder)
	{
		case MOTOROLA:
			pms_i2c_write(0x8A, 0x00, hue);
			break;
		case PHILIPS2:
			pms_i2c_write(0x8A, 0x07, hue);
			break;
		case PHILIPS1:
			pms_i2c_write(0x42, 0x07, hue);
			break;
	}
}

static void pms_colour(short colour)
{
	switch(decoder)
	{
		case MOTOROLA:
			pms_i2c_write(0x8A, 0x00, colour);
			break;
		case PHILIPS1:
			pms_i2c_write(0x42, 0x12, colour);
			break;
	}
}


static void pms_contrast(short contrast)
{
	switch(decoder)
	{
		case MOTOROLA:
			pms_i2c_write(0x8A, 0x00, contrast);
			break;
		case PHILIPS1:
			pms_i2c_write(0x42, 0x13, contrast);
			break;
	}
}

static void pms_brightness(short brightness)
{
	switch(decoder)
	{
		case MOTOROLA:
			pms_i2c_write(0x8A, 0x00, brightness);
			pms_i2c_write(0x8A, 0x00, brightness);
			pms_i2c_write(0x8A, 0x00, brightness);
			break;
		case PHILIPS1:
			pms_i2c_write(0x42, 0x19, brightness);
			break;
	}
}


static void pms_format(short format)
{
	int target;
	standard = format;

	if(decoder==PHILIPS1)
		target=0x42;
	else if(decoder==PHILIPS2)
		target=0x8A;
	else
		return;

	switch(format)
	{
		case 0:	/* Auto */
			pms_i2c_andor(target, 0x0D, 0xFE,0x00);
			pms_i2c_andor(target, 0x0F, 0x3F,0x80);
			break;
		case 1: /* NTSC */
			pms_i2c_andor(target, 0x0D, 0xFE, 0x00);
			pms_i2c_andor(target, 0x0F, 0x3F, 0x40);
			break;
		case 2: /* PAL */
			pms_i2c_andor(target, 0x0D, 0xFE, 0x00);
			pms_i2c_andor(target, 0x0F, 0x3F, 0x00);
			break;
		case 3:	/* SECAM */
			pms_i2c_andor(target, 0x0D, 0xFE, 0x01);
			pms_i2c_andor(target, 0x0F, 0x3F, 0x00);
			break;
	}
}

#ifdef FOR_FUTURE_EXPANSION

/*
 *	These features of the PMS card are not currently exposes. They
 *	could become a private v4l ioctl for PMSCONFIG or somesuch if
 *	people need it. We also don't yet use the PMS interrupt.
 */

static void pms_hstart(short start)
{
	switch(decoder)
	{
		case PHILIPS1:
			pms_i2c_write(0x8A, 0x05, start);
			pms_i2c_write(0x8A, 0x18, start);
			break;
		case PHILIPS2:
			pms_i2c_write(0x42, 0x05, start);
			pms_i2c_write(0x42, 0x18, start);
			break;
	}
}

/*
 *	Bandpass filters
 */

static void pms_bandpass(short pass)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A, 0x06, 0xCF, (pass&0x03)<<4);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x06, 0xCF, (pass&0x03)<<4);
}

static void pms_antisnow(short snow)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A, 0x06, 0xF3, (snow&0x03)<<2);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x06, 0xF3, (snow&0x03)<<2);
}

static void pms_sharpness(short sharp)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A, 0x06, 0xFC, sharp&0x03);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x06, 0xFC, sharp&0x03);
}

static void pms_chromaagc(short agc)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A, 0x0C, 0x9F, (agc&0x03)<<5);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x0C, 0x9F, (agc&0x03)<<5);
}

static void pms_vertnoise(short noise)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A, 0x10, 0xFC, noise&3);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x10, 0xFC, noise&3);
}

static void pms_forcecolour(short colour)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A, 0x0C, 0x7F, (colour&1)<<7);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x0C, 0x7, (colour&1)<<7);
}

static void pms_antigamma(short gamma)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0xB8, 0x00, 0x7F, (gamma&1)<<7);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x20, 0x7, (gamma&1)<<7);
}

static void pms_prefilter(short filter)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A, 0x06, 0xBF, (filter&1)<<6);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x06, 0xBF, (filter&1)<<6);
}

static void pms_hfilter(short filter)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0xB8, 0x04, 0x1F, (filter&7)<<5);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x24, 0x1F, (filter&7)<<5);
}

static void pms_vfilter(short filter)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0xB8, 0x08, 0x9F, (filter&3)<<5);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x28, 0x9F, (filter&3)<<5);
}

static void pms_killcolour(short colour)
{
	if(decoder==PHILIPS2)
	{
		pms_i2c_andor(0x8A, 0x08, 0x07, (colour&0x1F)<<3);
		pms_i2c_andor(0x8A, 0x09, 0x07, (colour&0x1F)<<3);
	}
	else if(decoder==PHILIPS1)
	{
		pms_i2c_andor(0x42, 0x08, 0x07, (colour&0x1F)<<3);
		pms_i2c_andor(0x42, 0x09, 0x07, (colour&0x1F)<<3);
	}
}

static void pms_chromagain(short chroma)
{
	if(decoder==PHILIPS2)
	{
		pms_i2c_write(0x8A, 0x11, chroma);
	}
	else if(decoder==PHILIPS1)
	{
		pms_i2c_write(0x42, 0x11, chroma);
	}
}


static void pms_spacialcompl(short data)
{
	mvv_write(0x3B, data);
}

static void pms_spacialcomph(short data)
{
	mvv_write(0x3A, data);
}

static void pms_vstart(short start)
{
	mvv_write(0x16, start);
	mvv_write(0x17, (start>>8)&0x01);
}

#endif

static void pms_secamcross(short cross)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A, 0x0F, 0xDF, (cross&1)<<5);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42, 0x0F, 0xDF, (cross&1)<<5);
}


static void pms_swsense(short sense)
{
	if(decoder==PHILIPS2)
	{
		pms_i2c_write(0x8A, 0x0A, sense);
		pms_i2c_write(0x8A, 0x0B, sense);
	}
	else if(decoder==PHILIPS1)
	{
		pms_i2c_write(0x42, 0x0A, sense);
		pms_i2c_write(0x42, 0x0B, sense);
	}
}


static void pms_framerate(short frr)
{
	int fps=(standard==1)?30:25;
	if(frr==0)
		return;
	fps=fps/frr;
	mvv_write(0x14,0x80|fps);
	mvv_write(0x15,1);
}

static void pms_vert(u8 deciden, u8 decinum)
{
	mvv_write(0x1C, deciden);	/* Denominator */
	mvv_write(0x1D, decinum);	/* Numerator */
}

/*
 *	Turn 16bit ratios into best small ratio the chipset can grok
 */

static void pms_vertdeci(unsigned short decinum, unsigned short deciden)
{
	/* Knock it down by /5 once */
	if(decinum%5==0)
	{
		deciden/=5;
		decinum/=5;
	}
	/*
	 *	3's
	 */
	while(decinum%3==0 && deciden%3==0)
	{
		deciden/=3;
		decinum/=3;
	}
	/*
	 *	2's
	 */
	while(decinum%2==0 && deciden%2==0)
	{
		decinum/=2;
		deciden/=2;
	}
	/*
	 *	Fudgyify
	 */
	while(deciden>32)
	{
		deciden/=2;
		decinum=(decinum+1)/2;
	}
	if(deciden==32)
		deciden--;
	pms_vert(deciden,decinum);
}

static void pms_horzdeci(short decinum, short deciden)
{
	if(decinum<=512)
	{
		if(decinum%5==0)
		{
			decinum/=5;
			deciden/=5;
		}
	}
	else
	{
		decinum=512;
		deciden=640;	/* 768 would be ideal */
	}

	while(((decinum|deciden)&1)==0)
	{
		decinum>>=1;
		deciden>>=1;
	}
	while(deciden>32)
	{
		deciden>>=1;
		decinum=(decinum+1)>>1;
	}
	if(deciden==32)
		deciden--;

	mvv_write(0x24, 0x80|deciden);
	mvv_write(0x25, decinum);
}

static void pms_resolution(short width, short height)
{
	int fg_height;

	fg_height=height;
	if(fg_height>280)
		fg_height=280;

	mvv_write(0x18, fg_height);
	mvv_write(0x19, fg_height>>8);

	if(standard==1)
	{
		mvv_write(0x1A, 0xFC);
		mvv_write(0x1B, 0x00);
		if(height>fg_height)
			pms_vertdeci(240,240);
		else
			pms_vertdeci(fg_height,240);
	}
	else
	{
		mvv_write(0x1A, 0x1A);
		mvv_write(0x1B, 0x01);
		if(fg_height>256)
			pms_vertdeci(270,270);
		else
			pms_vertdeci(fg_height, 270);
	}
	mvv_write(0x12,0);
	mvv_write(0x13, MVVMEMORYWIDTH);
	mvv_write(0x42, 0x00);
	mvv_write(0x43, 0x00);
	mvv_write(0x44, MVVMEMORYWIDTH);

	mvv_write(0x22, width+8);
	mvv_write(0x23, (width+8)>> 8);

	if(standard==1)
		pms_horzdeci(width,640);
	else
		pms_horzdeci(width+8, 768);

	mvv_write(0x30, mvv_read(0x30)&0xFE);
	mvv_write(0x08, mvv_read(0x08)|0x01);
	mvv_write(0x01, mvv_read(0x01)&0xFD);
	mvv_write(0x32, 0x00);
	mvv_write(0x33, MVVMEMORYWIDTH);
}


/*
 *	Set Input
 */

static void pms_vcrinput(short input)
{
	if(decoder==PHILIPS2)
		pms_i2c_andor(0x8A,0x0D,0x7F,(input&1)<<7);
	else if(decoder==PHILIPS1)
		pms_i2c_andor(0x42,0x0D,0x7F,(input&1)<<7);
}


static int pms_capture(struct pms_device *dev, char __user *buf, int rgb555, int count)
{
	int y;
	int dw = 2*dev->width;

	char tmp[dw+32]; /* using a temp buffer is faster than direct  */
	int cnt = 0;
	int len=0;
	unsigned char r8 = 0x5;  /* value for reg8  */

	if (rgb555)
		r8 |= 0x20; /* else use untranslated rgb = 565 */
	mvv_write(0x08,r8); /* capture rgb555/565, init DRAM, PC enable */

/*	printf("%d %d %d %d %d %x %x\n",width,height,voff,nom,den,mvv_buf); */

	for (y = 0; y < dev->height; y++ )
	{
		writeb(0, mem);  /* synchronisiert neue Zeile */

		/*
		 *	This is in truth a fifo, be very careful as if you
		 *	forgot this odd things will occur 8)
		 */

		memcpy_fromio(tmp, mem, dw+32); /* discard 16 word   */
		cnt -= dev->height;
		while (cnt <= 0)
		{
			/*
			 *	Don't copy too far
			 */
			int dt=dw;
			if(dt+len>count)
				dt=count-len;
			cnt += dev->height;
			if (copy_to_user(buf, tmp+32, dt))
				return len ? len : -EFAULT;
			buf += dt;
			len += dt;
		}
	}
	return len;
}


/*
 *	Video4linux interfacing
 */

static int pms_do_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct pms_device *pd=(struct pms_device *)dev;

	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability *b = arg;
			strcpy(b->name, "Mediavision PMS");
			b->type = VID_TYPE_CAPTURE|VID_TYPE_SCALES;
			b->channels = 4;
			b->audios = 0;
			b->maxwidth = 640;
			b->maxheight = 480;
			b->minwidth = 16;
			b->minheight = 16;
			return 0;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel *v = arg;
			if(v->channel<0 || v->channel>3)
				return -EINVAL;
			v->flags=0;
			v->tuners=1;
			/* Good question.. its composite or SVHS so.. */
			v->type = VIDEO_TYPE_CAMERA;
			switch(v->channel)
			{
				case 0:
					strcpy(v->name, "Composite");break;
				case 1:
					strcpy(v->name, "SVideo");break;
				case 2:
					strcpy(v->name, "Composite(VCR)");break;
				case 3:
					strcpy(v->name, "SVideo(VCR)");break;
			}
			return 0;
		}
		case VIDIOCSCHAN:
		{
			struct video_channel *v = arg;
			if(v->channel<0 || v->channel>3)
				return -EINVAL;
			mutex_lock(&pd->lock);
			pms_videosource(v->channel&1);
			pms_vcrinput(v->channel>>1);
			mutex_unlock(&pd->lock);
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner)
				return -EINVAL;
			strcpy(v->name, "Format");
			v->rangelow=0;
			v->rangehigh=0;
			v->flags= VIDEO_TUNER_PAL|VIDEO_TUNER_NTSC|VIDEO_TUNER_SECAM;
			switch(standard)
			{
				case 0:
					v->mode = VIDEO_MODE_AUTO;
					break;
				case 1:
					v->mode = VIDEO_MODE_NTSC;
					break;
				case 2:
					v->mode = VIDEO_MODE_PAL;
					break;
				case 3:
					v->mode = VIDEO_MODE_SECAM;
					break;
			}
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner)
				return -EINVAL;
			mutex_lock(&pd->lock);
			switch(v->mode)
			{
				case VIDEO_MODE_AUTO:
					pms_framerate(25);
					pms_secamcross(0);
					pms_format(0);
					break;
				case VIDEO_MODE_NTSC:
					pms_framerate(30);
					pms_secamcross(0);
					pms_format(1);
					break;
				case VIDEO_MODE_PAL:
					pms_framerate(25);
					pms_secamcross(0);
					pms_format(2);
					break;
				case VIDEO_MODE_SECAM:
					pms_framerate(25);
					pms_secamcross(1);
					pms_format(2);
					break;
				default:
					mutex_unlock(&pd->lock);
					return -EINVAL;
			}
			mutex_unlock(&pd->lock);
			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture *p = arg;
			*p = pd->picture;
			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture *p = arg;
			if(!((p->palette==VIDEO_PALETTE_RGB565 && p->depth==16)
			    ||(p->palette==VIDEO_PALETTE_RGB555 && p->depth==15)))
				return -EINVAL;
			pd->picture= *p;

			/*
			 *	Now load the card.
			 */

			mutex_lock(&pd->lock);
			pms_brightness(p->brightness>>8);
			pms_hue(p->hue>>8);
			pms_colour(p->colour>>8);
			pms_contrast(p->contrast>>8);
			mutex_unlock(&pd->lock);
			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window *vw = arg;
			if(vw->flags)
				return -EINVAL;
			if(vw->clipcount)
				return -EINVAL;
			if(vw->height<16||vw->height>480)
				return -EINVAL;
			if(vw->width<16||vw->width>640)
				return -EINVAL;
			pd->width=vw->width;
			pd->height=vw->height;
			mutex_lock(&pd->lock);
			pms_resolution(pd->width, pd->height);
			mutex_unlock(&pd->lock);			/* Ok we figured out what to use from our wide choice */
			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window *vw = arg;
			memset(vw,0,sizeof(*vw));
			vw->width=pd->width;
			vw->height=pd->height;
			return 0;
		}
		case VIDIOCKEY:
			return 0;
		case VIDIOCCAPTURE:
		case VIDIOCGFBUF:
		case VIDIOCSFBUF:
		case VIDIOCGFREQ:
		case VIDIOCSFREQ:
		case VIDIOCGAUDIO:
		case VIDIOCSAUDIO:
			return -EINVAL;
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static int pms_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, pms_do_ioctl);
}

static ssize_t pms_read(struct file *file, char __user *buf,
		    size_t count, loff_t *ppos)
{
	struct video_device *v = video_devdata(file);
	struct pms_device *pd=(struct pms_device *)v;
	int len;

	mutex_lock(&pd->lock);
	len=pms_capture(pd, buf, (pd->picture.depth==16)?0:1,count);
	mutex_unlock(&pd->lock);
	return len;
}

static const struct file_operations pms_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl          = pms_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.read           = pms_read,
	.llseek         = no_llseek,
};

static struct video_device pms_template=
{
	.owner		= THIS_MODULE,
	.name		= "Mediavision PMS",
	.type		= VID_TYPE_CAPTURE,
	.fops           = &pms_fops,
};

static struct pms_device pms_device;


/*
 *	Probe for and initialise the Mediavision PMS
 */

static int init_mediavision(void)
{
	int id;
	int idec, decst;
	int i;

	unsigned char i2c_defs[]={
		0x4C,0x30,0x00,0xE8,
		0xB6,0xE2,0x00,0x00,
		0xFF,0xFF,0x00,0x00,
		0x00,0x00,0x78,0x98,
		0x00,0x00,0x00,0x00,
		0x34,0x0A,0xF4,0xCE,
		0xE4
	};

	mem = ioremap(mem_base, 0x800);
	if (!mem)
		return -ENOMEM;

	if (!request_region(0x9A01, 1, "Mediavision PMS config"))
	{
		printk(KERN_WARNING "mediavision: unable to detect: 0x9A01 in use.\n");
		iounmap(mem);
		return -EBUSY;
	}
	if (!request_region(io_port, 3, "Mediavision PMS"))
	{
		printk(KERN_WARNING "mediavision: I/O port %d in use.\n", io_port);
		release_region(0x9A01, 1);
		iounmap(mem);
		return -EBUSY;
	}
	outb(0xB8, 0x9A01);		/* Unlock */
	outb(io_port>>4, 0x9A01);	/* Set IO port */


	id=mvv_read(3);
	decst=pms_i2c_stat(0x43);

	if(decst!=-1)
		idec=2;
	else if(pms_i2c_stat(0xb9)!=-1)
		idec=3;
	else if(pms_i2c_stat(0x8b)!=-1)
		idec=1;
	else
		idec=0;

	printk(KERN_INFO "PMS type is %d\n", idec);
	if(idec == 0) {
		release_region(io_port, 3);
		release_region(0x9A01, 1);
		iounmap(mem);
		return -ENODEV;
	}

	/*
	 *	Ok we have a PMS of some sort
	 */

	mvv_write(0x04, mem_base>>12);	/* Set the memory area */

	/* Ok now load the defaults */

	for(i=0;i<0x19;i++)
	{
		if(i2c_defs[i]==0xFF)
			pms_i2c_andor(0x8A, i, 0x07,0x00);
		else
			pms_i2c_write(0x8A, i, i2c_defs[i]);
	}

	pms_i2c_write(0xB8,0x00,0x12);
	pms_i2c_write(0xB8,0x04,0x00);
	pms_i2c_write(0xB8,0x07,0x00);
	pms_i2c_write(0xB8,0x08,0x00);
	pms_i2c_write(0xB8,0x09,0xFF);
	pms_i2c_write(0xB8,0x0A,0x00);
	pms_i2c_write(0xB8,0x0B,0x10);
	pms_i2c_write(0xB8,0x10,0x03);

	mvv_write(0x01, 0x00);
	mvv_write(0x05, 0xA0);
	mvv_write(0x08, 0x25);
	mvv_write(0x09, 0x00);
	mvv_write(0x0A, 0x20|MVVMEMORYWIDTH);

	mvv_write(0x10, 0x02);
	mvv_write(0x1E, 0x0C);
	mvv_write(0x1F, 0x03);
	mvv_write(0x26, 0x06);

	mvv_write(0x2B, 0x00);
	mvv_write(0x2C, 0x20);
	mvv_write(0x2D, 0x00);
	mvv_write(0x2F, 0x70);
	mvv_write(0x32, 0x00);
	mvv_write(0x33, MVVMEMORYWIDTH);
	mvv_write(0x34, 0x00);
	mvv_write(0x35, 0x00);
	mvv_write(0x3A, 0x80);
	mvv_write(0x3B, 0x10);
	mvv_write(0x20, 0x00);
	mvv_write(0x21, 0x00);
	mvv_write(0x30, 0x22);
	return 0;
}

/*
 *	Initialization and module stuff
 */

static int __init init_pms_cards(void)
{
	printk(KERN_INFO "Mediavision Pro Movie Studio driver 0.02\n");

	data_port = io_port +1;

	if(init_mediavision())
	{
		printk(KERN_INFO "Board not found.\n");
		return -ENODEV;
	}
	memcpy(&pms_device, &pms_template, sizeof(pms_template));
	mutex_init(&pms_device.lock);
	pms_device.height=240;
	pms_device.width=320;
	pms_swsense(75);
	pms_resolution(320,240);
	return video_register_device((struct video_device *)&pms_device, VFL_TYPE_GRABBER, video_nr);
}

module_param(io_port, int, 0);
module_param(mem_base, int, 0);
module_param(video_nr, int, 0);
MODULE_LICENSE("GPL");


static void __exit shutdown_mediavision(void)
{
	release_region(io_port,3);
	release_region(0x9A01, 1);
}

static void __exit cleanup_pms_module(void)
{
	shutdown_mediavision();
	video_unregister_device((struct video_device *)&pms_device);
	iounmap(mem);
}

module_init(init_pms_cards);
module_exit(cleanup_pms_module);

