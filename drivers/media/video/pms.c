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
 *	25-11-2009 	Hans Verkuil <hverkuil@xs4all.nl>
 * 			- converted to version 2 of the V4L API.
 *      08/07/2003      Daniele Bellucci <bellucda@tiscali.it>
 *                      - pms_capture: report back -EFAULT
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/isa.h>
#include <asm/io.h>

#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>

MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.5");

#define MOTOROLA	1
#define PHILIPS2	2               /* SAA7191 */
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
	struct v4l2_ctrl_handler hdl;
	int height;
	int width;
	int depth;
	int input;
	struct mutex lock;
	int i2c_count;
	struct i2c_info i2cinfo[64];

	int decoder;
	int standard;	/* 0 - auto 1 - ntsc 2 - pal 3 - secam */
	v4l2_std_id std;
	int io;
	int data;
	void __iomem *mem;
};

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
	switch (dev->decoder) {
	case MOTOROLA:
		break;
	case PHILIPS2:
		pms_i2c_andor(dev, 0x8a, 0x06, 0x7f, source ? 0x80 : 0);
		break;
	case PHILIPS1:
		break;
	}
	mvv_write(dev, 0x2E, 0x31);
	/* Was: mvv_write(dev, 0x2E, source ? 0x31 : 0x30);
	   But could not make this work correctly. Only Composite input
	   worked for me. */
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

static void pms_saturation(struct pms *dev, short sat)
{
	switch (dev->decoder) {
	case MOTOROLA:
		pms_i2c_write(dev, 0x8a, 0x00, sat);
		break;
	case PHILIPS1:
		pms_i2c_write(dev, 0x42, 0x12, sat);
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
	int fps = (dev->std & V4L2_STD_525_60) ? 30 : 25;

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

	if (dev->std & V4L2_STD_525_60) {
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

	if (dev->std & V4L2_STD_525_60)
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

static int pms_querycap(struct file *file, void  *priv,
					struct v4l2_capability *vcap)
{
	struct pms *dev = video_drvdata(file);

	strlcpy(vcap->driver, dev->v4l2_dev.name, sizeof(vcap->driver));
	strlcpy(vcap->card, "Mediavision PMS", sizeof(vcap->card));
	snprintf(vcap->bus_info, sizeof(vcap->bus_info),
			"ISA:%s", dev->v4l2_dev.name);
	vcap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
	vcap->capabilities = vcap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int pms_enum_input(struct file *file, void *fh, struct v4l2_input *vin)
{
	static const char *inputs[4] = {
		"Composite",
		"S-Video",
		"Composite (VCR)",
		"S-Video (VCR)"
	};

	if (vin->index > 3)
		return -EINVAL;
	strlcpy(vin->name, inputs[vin->index], sizeof(vin->name));
	vin->type = V4L2_INPUT_TYPE_CAMERA;
	vin->audioset = 0;
	vin->tuner = 0;
	vin->std = V4L2_STD_ALL;
	vin->status = 0;
	return 0;
}

static int pms_g_input(struct file *file, void *fh, unsigned int *inp)
{
	struct pms *dev = video_drvdata(file);

	*inp = dev->input;
	return 0;
}

static int pms_s_input(struct file *file, void *fh, unsigned int inp)
{
	struct pms *dev = video_drvdata(file);

	if (inp > 3)
		return -EINVAL;

	dev->input = inp;
	pms_videosource(dev, inp & 1);
	pms_vcrinput(dev, inp >> 1);
	return 0;
}

static int pms_g_std(struct file *file, void *fh, v4l2_std_id *std)
{
	struct pms *dev = video_drvdata(file);

	*std = dev->std;
	return 0;
}

static int pms_s_std(struct file *file, void *fh, v4l2_std_id *std)
{
	struct pms *dev = video_drvdata(file);
	int ret = 0;

	dev->std = *std;
	if (dev->std & V4L2_STD_NTSC) {
		pms_framerate(dev, 30);
		pms_secamcross(dev, 0);
		pms_format(dev, 1);
	} else if (dev->std & V4L2_STD_PAL) {
		pms_framerate(dev, 25);
		pms_secamcross(dev, 0);
		pms_format(dev, 2);
	} else if (dev->std & V4L2_STD_SECAM) {
		pms_framerate(dev, 25);
		pms_secamcross(dev, 1);
		pms_format(dev, 2);
	} else {
		ret = -EINVAL;
	}
	/*
	switch (v->mode) {
	case VIDEO_MODE_AUTO:
		pms_framerate(dev, 25);
		pms_secamcross(dev, 0);
		pms_format(dev, 0);
		break;
	}*/
	return ret;
}

static int pms_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct pms *dev = container_of(ctrl->handler, struct pms, hdl);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		pms_brightness(dev, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		pms_contrast(dev, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		pms_saturation(dev, ctrl->val);
		break;
	case V4L2_CID_HUE:
		pms_hue(dev, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int pms_g_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct pms *dev = video_drvdata(file);
	struct v4l2_pix_format *pix = &fmt->fmt.pix;

	pix->width = dev->width;
	pix->height = dev->height;
	pix->pixelformat = dev->width == 15 ?
			    V4L2_PIX_FMT_RGB555 : V4L2_PIX_FMT_RGB565;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = 2 * dev->width;
	pix->sizeimage = 2 * dev->width * dev->height;
	/* Just a guess */
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	return 0;
}

static int pms_try_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct v4l2_pix_format *pix = &fmt->fmt.pix;

	if (pix->height < 16 || pix->height > 480)
		return -EINVAL;
	if (pix->width < 16 || pix->width > 640)
		return -EINVAL;
	if (pix->pixelformat != V4L2_PIX_FMT_RGB555 &&
	    pix->pixelformat != V4L2_PIX_FMT_RGB565)
		return -EINVAL;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = 2 * pix->width;
	pix->sizeimage = 2 * pix->width * pix->height;
	/* Just a guess */
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	return 0;
}

static int pms_s_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct pms *dev = video_drvdata(file);
	struct v4l2_pix_format *pix = &fmt->fmt.pix;
	int ret = pms_try_fmt_vid_cap(file, fh, fmt);

	if (ret)
		return ret;
	dev->width = pix->width;
	dev->height = pix->height;
	dev->depth = (pix->pixelformat == V4L2_PIX_FMT_RGB555) ? 15 : 16;
	pms_resolution(dev, dev->width, dev->height);
	/* Ok we figured out what to use from our wide choice */
	return 0;
}

static int pms_enum_fmt_vid_cap(struct file *file, void *fh, struct v4l2_fmtdesc *fmt)
{
	static struct v4l2_fmtdesc formats[] = {
		{ 0, 0, 0,
		  "RGB 5:5:5", V4L2_PIX_FMT_RGB555,
		  { 0, 0, 0, 0 }
		},
		{ 1, 0, 0,
		  "RGB 5:6:5", V4L2_PIX_FMT_RGB565,
		  { 0, 0, 0, 0 }
		},
	};
	enum v4l2_buf_type type = fmt->type;

	if (fmt->index > 1)
		return -EINVAL;

	*fmt = formats[fmt->index];
	fmt->type = type;
	return 0;
}

static ssize_t pms_read(struct file *file, char __user *buf,
		    size_t count, loff_t *ppos)
{
	struct pms *dev = video_drvdata(file);
	int len;

	len = pms_capture(dev, buf, (dev->depth == 15), count);
	return len;
}

static unsigned int pms_poll(struct file *file, struct poll_table_struct *wait)
{
	struct v4l2_fh *fh = file->private_data;
	unsigned int res = POLLIN | POLLRDNORM;

	if (v4l2_event_pending(fh))
		res |= POLLPRI;
	poll_wait(file, &fh->wait, wait);
	return res;
}

static const struct v4l2_file_operations pms_fops = {
	.owner		= THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = v4l2_fh_release,
	.poll           = pms_poll,
	.unlocked_ioctl	= video_ioctl2,
	.read           = pms_read,
};

static const struct v4l2_ioctl_ops pms_ioctl_ops = {
	.vidioc_querycap	    = pms_querycap,
	.vidioc_g_input		    = pms_g_input,
	.vidioc_s_input		    = pms_s_input,
	.vidioc_enum_input	    = pms_enum_input,
	.vidioc_g_std		    = pms_g_std,
	.vidioc_s_std		    = pms_s_std,
	.vidioc_enum_fmt_vid_cap    = pms_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	    = pms_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	    = pms_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap     = pms_try_fmt_vid_cap,
	.vidioc_subscribe_event     = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event   = v4l2_event_unsubscribe,
};

/*
 *	Probe for and initialise the Mediavision PMS
 */

static int init_mediavision(struct pms *dev)
{
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

static const struct v4l2_ctrl_ops pms_ctrl_ops = {
	.s_ctrl = pms_s_ctrl,
};

static int pms_probe(struct device *pdev, unsigned int card)
{
	struct pms *dev;
	struct v4l2_device *v4l2_dev;
	struct v4l2_ctrl_handler *hdl;
	int res;

#ifndef MODULE
	if (!enable) {
		pr_err("PMS: not enabled, use pms.enable=1 to probe\n");
		return -ENODEV;
	}
#endif

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	dev->decoder = PHILIPS2;
	dev->io = io_port;
	dev->data = io_port + 1;
	v4l2_dev = &dev->v4l2_dev;
	hdl = &dev->hdl;

	res = v4l2_device_register(pdev, v4l2_dev);
	if (res < 0) {
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		goto free_dev;
	}
	v4l2_info(v4l2_dev, "Mediavision Pro Movie Studio driver 0.05\n");

	res = init_mediavision(dev);
	if (res) {
		v4l2_err(v4l2_dev, "Board not found.\n");
		goto free_io;
	}

	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &pms_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 139);
	v4l2_ctrl_new_std(hdl, &pms_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 70);
	v4l2_ctrl_new_std(hdl, &pms_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 64);
	v4l2_ctrl_new_std(hdl, &pms_ctrl_ops,
			V4L2_CID_HUE, 0, 255, 1, 0);
	if (hdl->error) {
		res = hdl->error;
		goto free_hdl;
	}

	mutex_init(&dev->lock);
	strlcpy(dev->vdev.name, v4l2_dev->name, sizeof(dev->vdev.name));
	dev->vdev.v4l2_dev = v4l2_dev;
	dev->vdev.ctrl_handler = hdl;
	dev->vdev.fops = &pms_fops;
	dev->vdev.ioctl_ops = &pms_ioctl_ops;
	dev->vdev.release = video_device_release_empty;
	dev->vdev.lock = &dev->lock;
	dev->vdev.tvnorms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM;
	set_bit(V4L2_FL_USE_FH_PRIO, &dev->vdev.flags);
	video_set_drvdata(&dev->vdev, dev);
	dev->std = V4L2_STD_NTSC_M;
	dev->height = 240;
	dev->width = 320;
	dev->depth = 16;
	pms_swsense(dev, 75);
	pms_resolution(dev, 320, 240);
	pms_videosource(dev, 0);
	pms_vcrinput(dev, 0);
	v4l2_ctrl_handler_setup(hdl);
	res = video_register_device(&dev->vdev, VFL_TYPE_GRABBER, video_nr);
	if (res >= 0)
		return 0;

free_hdl:
	v4l2_ctrl_handler_free(hdl);
	v4l2_device_unregister(&dev->v4l2_dev);
free_io:
	release_region(dev->io, 3);
	release_region(0x9a01, 1);
	iounmap(dev->mem);
free_dev:
	kfree(dev);
	return res;
}

static int pms_remove(struct device *pdev, unsigned int card)
{
	struct pms *dev = dev_get_drvdata(pdev);

	video_unregister_device(&dev->vdev);
	v4l2_ctrl_handler_free(&dev->hdl);
	release_region(dev->io, 3);
	release_region(0x9a01, 1);
	iounmap(dev->mem);
	return 0;
}

static struct isa_driver pms_driver = {
	.probe		= pms_probe,
	.remove		= pms_remove,
	.driver		= {
		.name	= "pms",
	},
};

static int __init pms_init(void)
{
	return isa_register_driver(&pms_driver, 1);
}

static void __exit pms_exit(void)
{
	isa_unregister_driver(&pms_driver);
}

module_init(pms_init);
module_exit(pms_exit);
