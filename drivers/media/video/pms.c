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
 *	His driver works so send any reports to alan@lxorguk.ukuu.org.uk
 *	unless the userspace driver also doesn't work for you...
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
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>

MODULE_LICENSE("GPL");


#define MOTOROLA	1
#define PHILIPS2	2
#define PHILIPS1	3
#define MVVMEMORYWIDTH	0x40		/* 512 bytes */

struct i2c_info {
	u8 slave;
	u8 sub;
	u8 data;
	u8 hits;
};

struct pms {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct video_picture picture;
	int height;
	int width;
	unsigned long in_use;
	struct mutex lock;
	int i2c_count;
	struct i2c_info i2cinfo[64];

	int decoder;
	int standard;	/* 0 - auto 1 - ntsc 2 - pal 3 - secam */
	int io;
	int data;
	void __iomem *mem;
};

static struct pms pms_card;

/*
 *	I/O ports and Shared Memory
 */

static int io_port = 0x250;
module_param(io_port, int, 0);

static int mem_base = 0xc8000;
module_param(mem_base, int, 0);

static int video_nr = -1;
module_param(video_nr, int, 0);


static inline void mvv_write(struct pms *dev, u8 index, u8 value)
{
	outw(index | (value << 8), dev->io);
}

static inline u8 mvv_read(struct pms *dev, u8 index)
{
	outb(index, dev->io);
	return inb(dev->data);
}

static int pms_i2c_stat(struct pms *dev, u8 slave)
{
	int counter = 0;
	int i;

	outb(0x28, dev->io);

	while ((inb(dev->data) & 0x01) == 0)
		if (counter++ == 256)
			break;

	while ((inb(dev->data) & 0x01) != 0)
		if (counter++ == 256)
			break;

	outb(slave, dev->io);

	counter = 0;
	while ((inb(dev->data) & 0x01) == 0)
		if (counter++ == 256)
			break;

	while ((inb(dev->data) & 0x01) != 0)
		if (counter++ == 256)
			break;

	for (i = 0; i < 12; i++) {
		char st = inb(dev->data);

		if ((st & 2) != 0)
			return -1;
		if ((st & 1) == 0)
			break;
	}
	outb(0x29, dev->io);
	return inb(dev->data);
}

static int pms_i2c_write(struct pms *dev, u16 slave, u16 sub, u16 data)
{
	int skip = 0;
	int count;
	int i;

	for (i = 0; i < dev->i2c_count; i++) {
		if ((dev->i2cinfo[i].slave == slave) &&
		    (dev->i2cinfo[i].sub == sub)) {
			if (dev->i2cinfo[i].data == data)
				skip = 1;
			dev->i2cinfo[i].data = data;
			i = dev->i2c_count + 1;
		}
	}

	if (i == dev->i2c_count && dev->i2c_count < 64) {
		dev->i2cinfo[dev->i2c_count].slave = slave;
		dev->i2cinfo[dev->i2c_count].sub = sub;
		dev->i2cinfo[dev->i2c_count].data = data;
		dev->i2c_count++;
	}

	if (skip)
		return 0;

	mvv_write(dev, 0x29, sub);
	mvv_write(dev, 0x2A, data);
	mvv_write(dev, 0x28, slave);

	outb(0x28, dev->io);

	count = 0;
	while ((inb(dev->data) & 1) == 0)
		if (count > 255)
			break;
	while ((inb(dev->data) & 1) != 0)
		if (count > 255)
			break;

	count = inb(dev->data);

	if (count & 2)
		return -1;
	return count;
}

static int pms_i2c_read(struct pms *dev, int slave, int sub)
{
	int i;

	for (i = 0; i < dev->i2c_count; i++) {
		if (dev->i2cinfo[i].slave == slave && dev->i2cinfo[i].sub == sub)
			return dev->i2cinfo[i].data;
	}
	return 0;
}


static void pms_i2c_andor(struct pms *dev, int slave, int sub, int and, int or)
{
	u8 tmp;

	tmp = pms_i2c_read(dev, slave, sub);
	tmp = (tmp & and) | or;
	pms_i2c_write(dev, slave, sub, tmp);
}

/*
 *	Control functions
 */


static void pms_videosource(struct pms *dev, short source)
{
	mvv_write(dev, 0x2E, source ? 0x31 : 0x30);
}

static void pms_hue(struct pms *dev, short hue)
{
	switch (dev->decoder) {
	case MOTOROLA:
		pms_i2c_write(dev, 0x8a, 0x00, hue);
		break;
	case PHILIPS2:
		pms_i2c_write(dev, 0x8a, 0x07, hue);
		break;
	case PHILIPS1:
		pms_i2c_write(dev, 0x42, 0x07, hue);
		break;
	}
}

static void pms_colour(struct pms *dev, short colour)
{
	switch (dev->decoder) {
	case MOTOROLA:
		pms_i2c_write(dev, 0x8a, 0x00, colour);
		break;
	case PHILIPS1:
		pms_i2c_write(dev, 0x42, 0x12, colour);
		break;
	}
}


static void pms_contrast(struct pms *dev, short contrast)
{
	switch (dev->decoder) {
	case MOTOROLA:
		pms_i2c_write(dev, 0x8a, 0x00, contrast);
		break;
	case PHILIPS1:
		pms_i2c_write(dev, 0x42, 0x13, contrast);
		break;
	}
}

static void pms_brightness(struct pms *dev, short brightness)
{
	switch (dev->decoder) {
	case MOTOROLA:
		pms_i2c_write(dev, 0x8a, 0x00, brightness);
		pms_i2c_write(dev, 0x8a, 0x00, brightness);
		pms_i2c_write(dev, 0x8a, 0x00, brightness);
		break;
	case PHILIPS1:
		pms_i2c_write(dev, 0x42, 0x19, brightness);
		break;
	}
}


static void pms_format(struct pms *dev, short format)
{
	int target;

	dev->standard = format;

	if (dev->decoder == PHILIPS1)
		target = 0x42;
	else if (dev->decoder == PHILIPS2)
		target = 0x8a;
	else
		return;

	switch (format) {
	case 0:	/* Auto */
		pms_i2c_andor(dev, target, 0x0d, 0xfe, 0x00);
		pms_i2c_andor(dev, target, 0x0f, 0x3f, 0x80);
		break;
	case 1: /* NTSC */
		pms_i2c_andor(dev, target, 0x0d, 0xfe, 0x00);
		pms_i2c_andor(dev, target, 0x0f, 0x3f, 0x40);
		break;
	case 2: /* PAL */
		pms_i2c_andor(dev, target, 0x0d, 0xfe, 0x00);
		pms_i2c_andor(dev, target, 0x0f, 0x3f, 0x00);
		break;
	case 3:	/* SECAM */
		pms_i2c_andor(dev, target, 0x0d, 0xfe, 0x01);
		pms_i2c_andor(dev, target, 0x0f, 0x3f, 0x00);
		break;
	}
}

#ifdef FOR_FUTURE_EXPANSION

/*
 *	These features of the PMS card are not currently exposes. They
 *	could become a private v4l ioctl for PMSCONFIG or somesuch if
 *	people need it. We also don't yet use the PMS interrupt.
 */

static void pms_hstart(struct pms *dev, short start)
{
	switch (dev->decoder) {
	case PHILIPS1:
		pms_i2c_write(dev, 0x8a, 0x05, start);
		pms_i2c_write(dev, 0x8a, 0x18, start);
		break;
	case PHILIPS2:
		pms_i2c_write(dev, 0x42, 0x05, start);
		pms_i2c_write(dev, 0x42, 0x18, start);
		break;
	}
}

/*
 *	Bandpass filters
 */

static void pms_bandpass(struct pms *dev, short pass)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x06, 0xcf, (pass & 0x03) << 4);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x06, 0xcf, (pass & 0x03) << 4);
}

static void pms_antisnow(struct pms *dev, short snow)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x06, 0xf3, (snow & 0x03) << 2);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x06, 0xf3, (snow & 0x03) << 2);
}

static void pms_sharpness(struct pms *dev, short sharp)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x06, 0xfc, sharp & 0x03);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x06, 0xfc, sharp & 0x03);
}

static void pms_chromaagc(struct pms *dev, short agc)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x0c, 0x9f, (agc & 0x03) << 5);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x0c, 0x9f, (agc & 0x03) << 5);
}

static void pms_vertnoise(struct pms *dev, short noise)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x10, 0xfc, noise & 3);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x10, 0xfc, noise & 3);
}

static void pms_forcecolour(struct pms *dev, short colour)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x0c, 0x7f, (colour & 1) << 7);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x0c, 0x7, (colour & 1) << 7);
}

static void pms_antigamma(struct pms *dev, short gamma)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0xb8, 0x00, 0x7f, (gamma & 1) << 7);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x20, 0x7, (gamma & 1) << 7);
}

static void pms_prefilter(struct pms *dev, short filter)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x06, 0xbf, (filter & 1) << 6);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x06, 0xbf, (filter & 1) << 6);
}

static void pms_hfilter(struct pms *dev, short filter)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0xb8, 0x04, 0x1f, (filter & 7) << 5);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x24, 0x1f, (filter & 7) << 5);
}

static void pms_vfilter(struct pms *dev, short filter)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0xb8, 0x08, 0x9f, (filter & 3) << 5);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x28, 0x9f, (filter & 3) << 5);
}

static void pms_killcolour(struct pms *dev, short colour)
{
	if (dev->decoder == PHILIPS2) {
		pms_i2c_andor(dev, 0x8a, 0x08, 0x07, (colour & 0x1f) << 3);
		pms_i2c_andor(dev, 0x8a, 0x09, 0x07, (colour & 0x1f) << 3);
	} else if (dev->decoder == PHILIPS1) {
		pms_i2c_andor(dev, 0x42, 0x08, 0x07, (colour & 0x1f) << 3);
		pms_i2c_andor(dev, 0x42, 0x09, 0x07, (colour & 0x1f) << 3);
	}
}

static void pms_chromagain(struct pms *dev, short chroma)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_write(dev, 0x8a, 0x11, chroma);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_write(dev, 0x42, 0x11, chroma);
}


static void pms_spacialcompl(struct pms *dev, short data)
{
	mvv_write(dev, 0x3b, data);
}

static void pms_spacialcomph(struct pms *dev, short data)
{
	mvv_write(dev, 0x3a, data);
}

static void pms_vstart(struct pms *dev, short start)
{
	mvv_write(dev, 0x16, start);
	mvv_write(dev, 0x17, (start >> 8) & 0x01);
}

#endif

static void pms_secamcross(struct pms *dev, short cross)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x0f, 0xdf, (cross & 1) << 5);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x0f, 0xdf, (cross & 1) << 5);
}


static void pms_swsense(struct pms *dev, short sense)
{
	if (dev->decoder == PHILIPS2) {
		pms_i2c_write(dev, 0x8a, 0x0a, sense);
		pms_i2c_write(dev, 0x8a, 0x0b, sense);
	} else if (dev->decoder == PHILIPS1) {
		pms_i2c_write(dev, 0x42, 0x0a, sense);
		pms_i2c_write(dev, 0x42, 0x0b, sense);
	}
}


static void pms_framerate(struct pms *dev, short frr)
{
	int fps = (dev->standard == 1) ? 30 : 25;

	if (frr == 0)
		return;
	fps = fps/frr;
	mvv_write(dev, 0x14, 0x80 | fps);
	mvv_write(dev, 0x15, 1);
}

static void pms_vert(struct pms *dev, u8 deciden, u8 decinum)
{
	mvv_write(dev, 0x1c, deciden);	/* Denominator */
	mvv_write(dev, 0x1d, decinum);	/* Numerator */
}

/*
 *	Turn 16bit ratios into best small ratio the chipset can grok
 */

static void pms_vertdeci(struct pms *dev, unsigned short decinum, unsigned short deciden)
{
	/* Knock it down by / 5 once */
	if (decinum % 5 == 0) {
		deciden /= 5;
		decinum /= 5;
	}
	/*
	 *	3's
	 */
	while (decinum % 3 == 0 && deciden % 3 == 0) {
		deciden /= 3;
		decinum /= 3;
	}
	/*
	 *	2's
	 */
	while (decinum % 2 == 0 && deciden % 2 == 0) {
		decinum /= 2;
		deciden /= 2;
	}
	/*
	 *	Fudgyify
	 */
	while (deciden > 32) {
		deciden /= 2;
		decinum = (decinum + 1) / 2;
	}
	if (deciden == 32)
		deciden--;
	pms_vert(dev, deciden, decinum);
}

static void pms_horzdeci(struct pms *dev, short decinum, short deciden)
{
	if (decinum <= 512) {
		if (decinum % 5 == 0) {
			decinum /= 5;
			deciden /= 5;
		}
	} else {
		decinum = 512;
		deciden = 640;	/* 768 would be ideal */
	}

	while (((decinum | deciden) & 1) == 0) {
		decinum >>= 1;
		deciden >>= 1;
	}
	while (deciden > 32) {
		deciden >>= 1;
		decinum = (decinum + 1) >> 1;
	}
	if (deciden == 32)
		deciden--;

	mvv_write(dev, 0x24, 0x80 | deciden);
	mvv_write(dev, 0x25, decinum);
}

static void pms_resolution(struct pms *dev, short width, short height)
{
	int fg_height;

	fg_height = height;
	if (fg_height > 280)
		fg_height = 280;

	mvv_write(dev, 0x18, fg_height);
	mvv_write(dev, 0x19, fg_height >> 8);

	if (dev->standard == 1) {
		mvv_write(dev, 0x1a, 0xfc);
		mvv_write(dev, 0x1b, 0x00);
		if (height > fg_height)
			pms_vertdeci(dev, 240, 240);
		else
			pms_vertdeci(dev, fg_height, 240);
	} else {
		mvv_write(dev, 0x1a, 0x1a);
		mvv_write(dev, 0x1b, 0x01);
		if (fg_height > 256)
			pms_vertdeci(dev, 270, 270);
		else
			pms_vertdeci(dev, fg_height, 270);
	}
	mvv_write(dev, 0x12, 0);
	mvv_write(dev, 0x13, MVVMEMORYWIDTH);
	mvv_write(dev, 0x42, 0x00);
	mvv_write(dev, 0x43, 0x00);
	mvv_write(dev, 0x44, MVVMEMORYWIDTH);

	mvv_write(dev, 0x22, width + 8);
	mvv_write(dev, 0x23, (width + 8) >> 8);

	if (dev->standard == 1)
		pms_horzdeci(dev, width, 640);
	else
		pms_horzdeci(dev, width + 8, 768);

	mvv_write(dev, 0x30, mvv_read(dev, 0x30) & 0xfe);
	mvv_write(dev, 0x08, mvv_read(dev, 0x08) | 0x01);
	mvv_write(dev, 0x01, mvv_read(dev, 0x01) & 0xfd);
	mvv_write(dev, 0x32, 0x00);
	mvv_write(dev, 0x33, MVVMEMORYWIDTH);
}


/*
 *	Set Input
 */

static void pms_vcrinput(struct pms *dev, short input)
{
	if (dev->decoder == PHILIPS2)
		pms_i2c_andor(dev, 0x8a, 0x0d, 0x7f, (input & 1) << 7);
	else if (dev->decoder == PHILIPS1)
		pms_i2c_andor(dev, 0x42, 0x0d, 0x7f, (input & 1) << 7);
}


static int pms_capture(struct pms *dev, char __user *buf, int rgb555, int count)
{
	int y;
	int dw = 2 * dev->width;
	char tmp[dw + 32]; /* using a temp buffer is faster than direct  */
	int cnt = 0;
	int len = 0;
	unsigned char r8 = 0x5;  /* value for reg8  */

	if (rgb555)
		r8 |= 0x20; /* else use untranslated rgb = 565 */
	mvv_write(dev, 0x08, r8); /* capture rgb555/565, init DRAM, PC enable */

/*	printf("%d %d %d %d %d %x %x\n",width,height,voff,nom,den,mvv_buf); */

	for (y = 0; y < dev->height; y++) {
		writeb(0, dev->mem);  /* synchronisiert neue Zeile */

		/*
		 *	This is in truth a fifo, be very careful as if you
		 *	forgot this odd things will occur 8)
		 */

		memcpy_fromio(tmp, dev->mem, dw + 32); /* discard 16 word   */
		cnt -= dev->height;
		while (cnt <= 0) {
			/*
			 *	Don't copy too far
			 */
			int dt = dw;
			if (dt + len > count)
				dt = count - len;
			cnt += dev->height;
			if (copy_to_user(buf, tmp + 32, dt))
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

static long pms_do_ioctl(struct file *file, unsigned int cmd, void *arg)
{
	struct pms *dev = video_drvdata(file);

	switch (cmd) {
	case VIDIOCGCAP: {
		struct video_capability *b = arg;

		strcpy(b->name, "Mediavision PMS");
		b->type = VID_TYPE_CAPTURE | VID_TYPE_SCALES;
		b->channels = 4;
		b->audios = 0;
		b->maxwidth = 640;
		b->maxheight = 480;
		b->minwidth = 16;
		b->minheight = 16;
		return 0;
	}
	case VIDIOCGCHAN: {
		struct video_channel *v = arg;

		if (v->channel < 0 || v->channel > 3)
			return -EINVAL;
		v->flags = 0;
		v->tuners = 1;
		/* Good question.. its composite or SVHS so.. */
		v->type = VIDEO_TYPE_CAMERA;
		switch (v->channel) {
		case 0:
			strcpy(v->name, "Composite");
			break;
		case 1:
			strcpy(v->name, "SVideo");
			break;
		case 2:
			strcpy(v->name, "Composite(VCR)");
			break;
		case 3:
			strcpy(v->name, "SVideo(VCR)");
			break;
		}
		return 0;
	}
	case VIDIOCSCHAN: {
		struct video_channel *v = arg;

		if (v->channel < 0 || v->channel > 3)
			return -EINVAL;
		mutex_lock(&dev->lock);
		pms_videosource(dev, v->channel & 1);
		pms_vcrinput(dev, v->channel >> 1);
		mutex_unlock(&dev->lock);
		return 0;
	}
	case VIDIOCGTUNER: {
		struct video_tuner *v = arg;

		if (v->tuner)
			return -EINVAL;
		strcpy(v->name, "Format");
		v->rangelow = 0;
		v->rangehigh = 0;
		v->flags = VIDEO_TUNER_PAL | VIDEO_TUNER_NTSC | VIDEO_TUNER_SECAM;
		switch (dev->standard) {
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
	case VIDIOCSTUNER: {
		struct video_tuner *v = arg;

		if (v->tuner)
			return -EINVAL;
		mutex_lock(&dev->lock);
		switch (v->mode) {
		case VIDEO_MODE_AUTO:
			pms_framerate(dev, 25);
			pms_secamcross(dev, 0);
			pms_format(dev, 0);
			break;
		case VIDEO_MODE_NTSC:
			pms_framerate(dev, 30);
			pms_secamcross(dev, 0);
			pms_format(dev, 1);
			break;
		case VIDEO_MODE_PAL:
			pms_framerate(dev, 25);
			pms_secamcross(dev, 0);
			pms_format(dev, 2);
			break;
		case VIDEO_MODE_SECAM:
			pms_framerate(dev, 25);
			pms_secamcross(dev, 1);
			pms_format(dev, 2);
			break;
		default:
			mutex_unlock(&dev->lock);
			return -EINVAL;
		}
		mutex_unlock(&dev->lock);
		return 0;
	}
	case VIDIOCGPICT: {
		struct video_picture *p = arg;

		*p = dev->picture;
		return 0;
	}
	case VIDIOCSPICT: {
		struct video_picture *p = arg;

		if (!((p->palette == VIDEO_PALETTE_RGB565 && p->depth == 16) ||
		      (p->palette == VIDEO_PALETTE_RGB555 && p->depth == 15)))
			return -EINVAL;
		dev->picture = *p;

		/*
		 *	Now load the card.
		 */

		mutex_lock(&dev->lock);
		pms_brightness(dev, p->brightness >> 8);
		pms_hue(dev, p->hue >> 8);
		pms_colour(dev, p->colour >> 8);
		pms_contrast(dev, p->contrast >> 8);
		mutex_unlock(&dev->lock);
		return 0;
	}
	case VIDIOCSWIN: {
		struct video_window *vw = arg;

		if (vw->flags)
			return -EINVAL;
		if (vw->clipcount)
			return -EINVAL;
		if (vw->height < 16 || vw->height > 480)
			return -EINVAL;
		if (vw->width < 16 || vw->width > 640)
			return -EINVAL;
		dev->width = vw->width;
		dev->height = vw->height;
		mutex_lock(&dev->lock);
		pms_resolution(dev, dev->width, dev->height);
		/* Ok we figured out what to use from our wide choice */
		mutex_unlock(&dev->lock);
		return 0;
	}
	case VIDIOCGWIN: {
		struct video_window *vw = arg;

		memset(vw, 0, sizeof(*vw));
		vw->width = dev->width;
		vw->height = dev->height;
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

static long pms_ioctl(struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(file, cmd, arg, pms_do_ioctl);
}

static ssize_t pms_read(struct file *file, char __user *buf,
		    size_t count, loff_t *ppos)
{
	struct pms *dev = video_drvdata(file);
	int len;

	mutex_lock(&dev->lock);
	len = pms_capture(dev, buf, (dev->picture.depth == 16) ? 0 : 1, count);
	mutex_unlock(&dev->lock);
	return len;
}

static int pms_exclusive_open(struct file *file)
{
	struct pms *dev = video_drvdata(file);

	return test_and_set_bit(0, &dev->in_use) ? -EBUSY : 0;
}

static int pms_exclusive_release(struct file *file)
{
	struct pms *dev = video_drvdata(file);

	clear_bit(0, &dev->in_use);
	return 0;
}

static const struct v4l2_file_operations pms_fops = {
	.owner		= THIS_MODULE,
	.open           = pms_exclusive_open,
	.release        = pms_exclusive_release,
	.ioctl          = pms_ioctl,
	.read           = pms_read,
};


/*
 *	Probe for and initialise the Mediavision PMS
 */

static int init_mediavision(struct pms *dev)
{
	int id;
	int idec, decst;
	int i;
	static const unsigned char i2c_defs[] = {
		0x4c, 0x30, 0x00, 0xe8,
		0xb6, 0xe2, 0x00, 0x00,
		0xff, 0xff, 0x00, 0x00,
		0x00, 0x00, 0x78, 0x98,
		0x00, 0x00, 0x00, 0x00,
		0x34, 0x0a, 0xf4, 0xce,
		0xe4
	};

	dev->mem = ioremap(mem_base, 0x800);
	if (!dev->mem)
		return -ENOMEM;

	if (!request_region(0x9a01, 1, "Mediavision PMS config")) {
		printk(KERN_WARNING "mediavision: unable to detect: 0x9a01 in use.\n");
		iounmap(dev->mem);
		return -EBUSY;
	}
	if (!request_region(dev->io, 3, "Mediavision PMS")) {
		printk(KERN_WARNING "mediavision: I/O port %d in use.\n", dev->io);
		release_region(0x9a01, 1);
		iounmap(dev->mem);
		return -EBUSY;
	}
	outb(0xb8, 0x9a01);		/* Unlock */
	outb(dev->io >> 4, 0x9a01);	/* Set IO port */


	id = mvv_read(dev, 3);
	decst = pms_i2c_stat(dev, 0x43);

	if (decst != -1)
		idec = 2;
	else if (pms_i2c_stat(dev, 0xb9) != -1)
		idec = 3;
	else if (pms_i2c_stat(dev, 0x8b) != -1)
		idec = 1;
	else
		idec = 0;

	printk(KERN_INFO "PMS type is %d\n", idec);
	if (idec == 0) {
		release_region(dev->io, 3);
		release_region(0x9a01, 1);
		iounmap(dev->mem);
		return -ENODEV;
	}

	/*
	 *	Ok we have a PMS of some sort
	 */

	mvv_write(dev, 0x04, mem_base >> 12);	/* Set the memory area */

	/* Ok now load the defaults */

	for (i = 0; i < 0x19; i++) {
		if (i2c_defs[i] == 0xff)
			pms_i2c_andor(dev, 0x8a, i, 0x07, 0x00);
		else
			pms_i2c_write(dev, 0x8a, i, i2c_defs[i]);
	}

	pms_i2c_write(dev, 0xb8, 0x00, 0x12);
	pms_i2c_write(dev, 0xb8, 0x04, 0x00);
	pms_i2c_write(dev, 0xb8, 0x07, 0x00);
	pms_i2c_write(dev, 0xb8, 0x08, 0x00);
	pms_i2c_write(dev, 0xb8, 0x09, 0xff);
	pms_i2c_write(dev, 0xb8, 0x0a, 0x00);
	pms_i2c_write(dev, 0xb8, 0x0b, 0x10);
	pms_i2c_write(dev, 0xb8, 0x10, 0x03);

	mvv_write(dev, 0x01, 0x00);
	mvv_write(dev, 0x05, 0xa0);
	mvv_write(dev, 0x08, 0x25);
	mvv_write(dev, 0x09, 0x00);
	mvv_write(dev, 0x0a, 0x20 | MVVMEMORYWIDTH);

	mvv_write(dev, 0x10, 0x02);
	mvv_write(dev, 0x1e, 0x0c);
	mvv_write(dev, 0x1f, 0x03);
	mvv_write(dev, 0x26, 0x06);

	mvv_write(dev, 0x2b, 0x00);
	mvv_write(dev, 0x2c, 0x20);
	mvv_write(dev, 0x2d, 0x00);
	mvv_write(dev, 0x2f, 0x70);
	mvv_write(dev, 0x32, 0x00);
	mvv_write(dev, 0x33, MVVMEMORYWIDTH);
	mvv_write(dev, 0x34, 0x00);
	mvv_write(dev, 0x35, 0x00);
	mvv_write(dev, 0x3a, 0x80);
	mvv_write(dev, 0x3b, 0x10);
	mvv_write(dev, 0x20, 0x00);
	mvv_write(dev, 0x21, 0x00);
	mvv_write(dev, 0x30, 0x22);
	return 0;
}

/*
 *	Initialization and module stuff
 */

#ifndef MODULE
static int enable;
module_param(enable, int, 0);
#endif

static int __init pms_init(void)
{
	struct pms *dev = &pms_card;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int res;

	strlcpy(v4l2_dev->name, "pms", sizeof(v4l2_dev->name));

	v4l2_info(v4l2_dev, "Mediavision Pro Movie Studio driver 0.03\n");

#ifndef MODULE
	if (!enable) {
		v4l2_err(v4l2_dev,
			"PMS: not enabled, use pms.enable=1 to probe\n");
		return -ENODEV;
	}
#endif

	dev->decoder = PHILIPS2;
	dev->io = io_port;
	dev->data = io_port + 1;

	if (init_mediavision(dev)) {
		v4l2_err(v4l2_dev, "Board not found.\n");
		return -ENODEV;
	}

	res = v4l2_device_register(NULL, v4l2_dev);
	if (res < 0) {
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		return res;
	}

	strlcpy(dev->vdev.name, v4l2_dev->name, sizeof(dev->vdev.name));
	dev->vdev.v4l2_dev = v4l2_dev;
	dev->vdev.fops = &pms_fops;
	dev->vdev.release = video_device_release_empty;
	video_set_drvdata(&dev->vdev, dev);
	mutex_init(&dev->lock);
	dev->height = 240;
	dev->width = 320;
	pms_swsense(dev, 75);
	pms_resolution(dev, 320, 240);
	pms_videosource(dev, 0);
	pms_vcrinput(dev, 0);
	if (video_register_device(&dev->vdev, VFL_TYPE_GRABBER, video_nr) < 0) {
		v4l2_device_unregister(&dev->v4l2_dev);
		release_region(dev->io, 3);
		release_region(0x9a01, 1);
		iounmap(dev->mem);
		return -EINVAL;
	}
	return 0;
}

static void __exit pms_exit(void)
{
	struct pms *dev = &pms_card;

	video_unregister_device(&dev->vdev);
	release_region(dev->io, 3);
	release_region(0x9a01, 1);
	iounmap(dev->mem);
}

module_init(pms_init);
module_exit(pms_exit);
