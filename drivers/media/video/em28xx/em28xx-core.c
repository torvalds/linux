/*
   em2820-core.c - driver for Empia EM2820/2840 USB video capture devices

   Copyright (C) 2005 Markus Rechberger <mrechberger@gmail.com>
                      Ludovico Cavedon <cavedon@sssup.it>
                      Mauro Carvalho Chehab <mchehab@brturbo.com.br>

   Based on the em2800 driver from Sascha Sommer <saschasommer@freenet.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>

#include "em2820.h"

/* #define ENABLE_DEBUG_ISOC_FRAMES */

unsigned int core_debug = 0;
module_param(core_debug,int,0644);
MODULE_PARM_DESC(core_debug,"enable debug messages [core]");

#define em2820_coredbg(fmt, arg...) do {\
        if (core_debug) \
                printk(KERN_INFO "%s %s :"fmt, \
                         dev->name, __FUNCTION__ , ##arg); } while (0)

unsigned int reg_debug = 0;
module_param(reg_debug,int,0644);
MODULE_PARM_DESC(reg_debug,"enable debug messages [URB reg]");

#define em2820_regdbg(fmt, arg...) do {\
        if (reg_debug) \
                printk(KERN_INFO "%s %s :"fmt, \
                         dev->name, __FUNCTION__ , ##arg); } while (0)

unsigned int isoc_debug = 0;
module_param(isoc_debug,int,0644);
MODULE_PARM_DESC(core_debug,"enable debug messages [isoc transfers]");

#define em2820_isocdbg(fmt, arg...) do {\
        if (isoc_debug) \
                printk(KERN_INFO "%s %s :"fmt, \
                         dev->name, __FUNCTION__ , ##arg); } while (0)

static int alt = EM2820_PINOUT;
module_param(alt, int, 0644);
MODULE_PARM_DESC(alt, "alternate setting to use for video endpoint");

/* ------------------------------------------------------------------ */
/* debug help functions                                               */

static const char *v4l1_ioctls[] = {
	"0", "CGAP", "GCHAN", "SCHAN", "GTUNER", "STUNER", "GPICT", "SPICT",
	"CCAPTURE", "GWIN", "SWIN", "GFBUF", "SFBUF", "KEY", "GFREQ",
	"SFREQ", "GAUDIO", "SAUDIO", "SYNC", "MCAPTURE", "GMBUF", "GUNIT",
	"GCAPTURE", "SCAPTURE", "SPLAYMODE", "SWRITEMODE", "GPLAYINFO",
	"SMICROCODE", "GVBIFMT", "SVBIFMT" };
#define V4L1_IOCTLS ARRAY_SIZE(v4l1_ioctls)

static const char *v4l2_ioctls[] = {
	"QUERYCAP", "1", "ENUM_PIXFMT", "ENUM_FBUFFMT", "G_FMT", "S_FMT",
	"G_COMP", "S_COMP", "REQBUFS", "QUERYBUF", "G_FBUF", "S_FBUF",
	"G_WIN", "S_WIN", "PREVIEW", "QBUF", "16", "DQBUF", "STREAMON",
	"STREAMOFF", "G_PERF", "G_PARM", "S_PARM", "G_STD", "S_STD",
	"ENUMSTD", "ENUMINPUT", "G_CTRL", "S_CTRL", "G_TUNER", "S_TUNER",
	"G_FREQ", "S_FREQ", "G_AUDIO", "S_AUDIO", "35", "QUERYCTRL",
	"QUERYMENU", "G_INPUT", "S_INPUT", "ENUMCVT", "41", "42", "43",
	"44", "45",  "G_OUTPUT", "S_OUTPUT", "ENUMOUTPUT", "G_AUDOUT",
	"S_AUDOUT", "ENUMFX", "G_EFFECT", "S_EFFECT", "G_MODULATOR",
	"S_MODULATOR"
};
#define V4L2_IOCTLS ARRAY_SIZE(v4l2_ioctls)

void em2820_print_ioctl(char *name, unsigned int cmd)
{
	char *dir;

	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:              dir = "--"; break;
	case _IOC_READ:              dir = "r-"; break;
	case _IOC_WRITE:             dir = "-w"; break;
	case _IOC_READ | _IOC_WRITE: dir = "rw"; break;
	default:                     dir = "??"; break;
	}
	switch (_IOC_TYPE(cmd)) {
	case 'v':
		printk(KERN_DEBUG "%s: ioctl 0x%08x (v4l1, %s, VIDIOC%s)\n",
		       name, cmd, dir, (_IOC_NR(cmd) < V4L1_IOCTLS) ?
		       v4l1_ioctls[_IOC_NR(cmd)] : "???");
		break;
	case 'V':
		printk(KERN_DEBUG "%s: ioctl 0x%08x (v4l2, %s, VIDIOC_%s)\n",
		       name, cmd, dir, (_IOC_NR(cmd) < V4L2_IOCTLS) ?
		       v4l2_ioctls[_IOC_NR(cmd)] : "???");
		break;
	default:
		printk(KERN_DEBUG "%s: ioctl 0x%08x (???, %s, #%d)\n",
		       name, cmd, dir, _IOC_NR(cmd));
	}
}

static void *rvmalloc(size_t size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);

	mem = vmalloc_32((unsigned long)size);
	if (!mem)
		return NULL;

	memset(mem, 0, size);

	adr = (unsigned long)mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void rvfree(void *mem, size_t size)
{
	unsigned long adr;

	if (!mem)
		return;

	size = PAGE_ALIGN(size);

	adr = (unsigned long)mem;
	while (size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vfree(mem);
}

/*
 * em2820_request_buffers()
 * allocate a number of buffers
 */
u32 em2820_request_buffers(struct em2820 *dev, u32 count)
{
	const size_t imagesize = PAGE_ALIGN(dev->frame_size);	/*needs to be page aligned cause the buffers can be mapped individually! */
	void *buff = NULL;
	u32 i;
	em2820_coredbg("requested %i buffers with size %i", count, imagesize);
	if (count > EM2820_NUM_FRAMES)
		count = EM2820_NUM_FRAMES;

	dev->num_frames = count;
	while (dev->num_frames > 0) {
		if ((buff = rvmalloc(dev->num_frames * imagesize)))
			break;
		dev->num_frames--;
	}

	for (i = 0; i < dev->num_frames; i++) {
		dev->frame[i].bufmem = buff + i * imagesize;
		dev->frame[i].buf.index = i;
		dev->frame[i].buf.m.offset = i * imagesize;
		dev->frame[i].buf.length = dev->frame_size;
		dev->frame[i].buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		dev->frame[i].buf.sequence = 0;
		dev->frame[i].buf.field = V4L2_FIELD_NONE;
		dev->frame[i].buf.memory = V4L2_MEMORY_MMAP;
		dev->frame[i].buf.flags = 0;
	}
	return dev->num_frames;
}

/*
 * em2820_queue_unusedframes()
 * add all frames that are not currently in use to the inbuffer queue
 */
void em2820_queue_unusedframes(struct em2820 *dev)
{
	unsigned long lock_flags;
	u32 i;

	for (i = 0; i < dev->num_frames; i++)
		if (dev->frame[i].state == F_UNUSED) {
			dev->frame[i].state = F_QUEUED;
			spin_lock_irqsave(&dev->queue_lock, lock_flags);
			list_add_tail(&dev->frame[i].frame, &dev->inqueue);
			spin_unlock_irqrestore(&dev->queue_lock, lock_flags);
		}
}

/*
 * em2820_release_buffers()
 * free frame buffers
 */
void em2820_release_buffers(struct em2820 *dev)
{
	if (dev->num_frames) {
		rvfree(dev->frame[0].bufmem,
		       dev->num_frames * PAGE_ALIGN(dev->frame[0].buf.length));
		dev->num_frames = 0;
	}
}

/*
 * em2820_read_reg_req()
 * reads data from the usb device specifying bRequest
 */
int em2820_read_reg_req_len(struct em2820 *dev, u8 req, u16 reg,
				   char *buf, int len)
{
	int ret, byte;

	em2820_regdbg("req=%02x, reg=%02x ", req, reg);

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0), req,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, buf, len, HZ);

	if (reg_debug){
		printk(ret < 0 ? " failed!\n" : "%02x values: ", ret);
		for (byte = 0; byte < len; byte++) {
			printk(" %02x", buf[byte]);
		}
		printk("\n");
	}

	return ret;
}

/*
 * em2820_read_reg_req()
 * reads data from the usb device specifying bRequest
 */
int em2820_read_reg_req(struct em2820 *dev, u8 req, u16 reg)
{
	u8 val;
	int ret;

	em2820_regdbg("req=%02x, reg=%02x:", req, reg);

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0), req,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, &val, 1, HZ);

	if (reg_debug)
		printk(ret < 0 ? " failed!\n" : "%02x\n", val);

	if (ret < 0)
		return ret;

	return val;
}

int em2820_read_reg(struct em2820 *dev, u16 reg)
{
	return em2820_read_reg_req(dev, USB_REQ_GET_STATUS, reg);
}

/*
 * em2820_write_regs_req()
 * sends data to the usb device, specifying bRequest
 */
int em2820_write_regs_req(struct em2820 *dev, u8 req, u16 reg, char *buf,
				 int len)
{
	int ret;

	/*usb_control_msg seems to expect a kmalloced buffer */
	unsigned char *bufs = kmalloc(len, GFP_KERNEL);

	em2820_regdbg("req=%02x reg=%02x:", req, reg);

	if (reg_debug) {
		int i;
		for (i = 0; i < len; ++i)
			printk (" %02x", (unsigned char)buf[i]);
		printk ("\n");
	}

	if (!bufs)
		return -ENOMEM;
	memcpy(bufs, buf, len);
	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), req,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0000, reg, bufs, len, HZ);
	mdelay(5);		/* FIXME: magic number */
	kfree(bufs);
	return ret;
}

int em2820_write_regs(struct em2820 *dev, u16 reg, char *buf, int len)
{
	return em2820_write_regs_req(dev, USB_REQ_GET_STATUS, reg, buf, len);
}

/*
 * em2820_write_reg_bits()
 * sets only some bits (specified by bitmask) of a register, by first reading
 * the actual value
 */
int em2820_write_reg_bits(struct em2820 *dev, u16 reg, u8 val,
				 u8 bitmask)
{
	int oldval;
	u8 newval;
	if ((oldval = em2820_read_reg(dev, reg)) < 0)
		return oldval;
	newval = (((u8) oldval) & ~bitmask) | (val & bitmask);
	return em2820_write_regs(dev, reg, &newval, 1);
}

/*
 * em2820_write_ac97()
 * write a 16 bit value to the specified AC97 address (LSB first!)
 */
int em2820_write_ac97(struct em2820 *dev, u8 reg, u8 * val)
{
	int ret;
	u8 addr = reg & 0x7f;
	if ((ret = em2820_write_regs(dev, AC97LSB_REG, val, 2)) < 0)
		return ret;
	if ((ret = em2820_write_regs(dev, AC97ADDR_REG, &addr, 1)) < 0)
		return ret;
	if ((ret = em2820_read_reg(dev, AC97BUSY_REG)) < 0)
		return ret;
	else if (((u8) ret) & 0x01) {
		em2820_warn ("AC97 command still being exectuted: not handled properly!\n");
	}
	return 0;
}

int em2820_audio_analog_set(struct em2820 *dev)
{
	char s[2] = { 0x00, 0x00 };
	s[0] |= 0x1f - dev->volume;
	s[1] |= 0x1f - dev->volume;
	if (dev->mute)
		s[1] |= 0x80;
	return em2820_write_ac97(dev, MASTER_AC97, s);
}


int em2820_colorlevels_set_default(struct em2820 *dev)
{
	em2820_write_regs(dev, YGAIN_REG, "\x10", 1);	/* contrast */
	em2820_write_regs(dev, YOFFSET_REG, "\x00", 1);	/* brightness */
	em2820_write_regs(dev, UVGAIN_REG, "\x10", 1);	/* saturation */
	em2820_write_regs(dev, UOFFSET_REG, "\x00", 1);
	em2820_write_regs(dev, VOFFSET_REG, "\x00", 1);
	em2820_write_regs(dev, SHARPNESS_REG, "\x00", 1);

	em2820_write_regs(dev, GAMMA_REG, "\x20", 1);
	em2820_write_regs(dev, RGAIN_REG, "\x20", 1);
	em2820_write_regs(dev, GGAIN_REG, "\x20", 1);
	em2820_write_regs(dev, BGAIN_REG, "\x20", 1);
	em2820_write_regs(dev, ROFFSET_REG, "\x00", 1);
	em2820_write_regs(dev, GOFFSET_REG, "\x00", 1);
	return em2820_write_regs(dev, BOFFSET_REG, "\x00", 1);
}

int em2820_capture_start(struct em2820 *dev, int start)
{
	int ret;
	/* FIXME: which is the best order? */
	/* video registers are sampled by VREF */
	if ((ret = em2820_write_reg_bits(dev, USBSUSP_REG, start ? 0x10 : 0x00,
					  0x10)) < 0)
		return ret;
	/* enable video capture */
	return em2820_write_regs(dev, VINENABLE_REG, start ? "\x67" : "\x27", 1);
}

int em2820_outfmt_set_yuv422(struct em2820 *dev)
{
	em2820_write_regs(dev, OUTFMT_REG, "\x34", 1);
	em2820_write_regs(dev, VINMODE_REG, "\x10", 1);
	return em2820_write_regs(dev, VINCTRL_REG, "\x11", 1);
}

int em2820_accumulator_set(struct em2820 *dev, u8 xmin, u8 xmax, u8 ymin,
				  u8 ymax)
{
	em2820_coredbg("em2820 Scale: (%d,%d)-(%d,%d)\n", xmin, ymin, xmax, ymax);

	em2820_write_regs(dev, XMIN_REG, &xmin, 1);
	em2820_write_regs(dev, XMAX_REG, &xmax, 1);
	em2820_write_regs(dev, YMIN_REG, &ymin, 1);
	return em2820_write_regs(dev, YMAX_REG, &ymax, 1);
}

int em2820_capture_area_set(struct em2820 *dev, u8 hstart, u8 vstart,
				   u16 width, u16 height)
{
	u8 cwidth = width;
	u8 cheight = height;
	u8 overflow = (height >> 7 & 0x02) | (width >> 8 & 0x01);

	em2820_coredbg("em2820 Area Set: (%d,%d)\n", (width | (overflow & 2) << 7),
			(height | (overflow & 1) << 8));

	em2820_write_regs(dev, HSTART_REG, &hstart, 1);
	em2820_write_regs(dev, VSTART_REG, &vstart, 1);
	em2820_write_regs(dev, CWIDTH_REG, &cwidth, 1);
	em2820_write_regs(dev, CHEIGHT_REG, &cheight, 1);
	return em2820_write_regs(dev, OFLOW_REG, &overflow, 1);
}

int em2820_scaler_set(struct em2820 *dev, u16 h, u16 v)
{
	u8 buf[2];
	buf[0] = h;
	buf[1] = h >> 8;
	em2820_write_regs(dev, HSCALELOW_REG, (char *)buf, 2);
	buf[0] = v;
	buf[1] = v >> 8;
	em2820_write_regs(dev, VSCALELOW_REG, (char *)buf, 2);
	/* when H and V mixershould be used? */
	/*	return em2820_write_reg_bits(dev, COMPR_REG, (h ? 0x20 : 0x00) | (v ? 0x10 : 0x00), 0x30); */
	/* it seems that both H and V scalers must be active to work correctly */
	return em2820_write_reg_bits(dev, COMPR_REG, h
			|| v ? 0x30 : 0x00, 0x30);
}

/* FIXME: this only function read values from dev */
int em2820_resolution_set(struct em2820 *dev)
{
	int width, height;
	width = norm_maxw(dev);
	height = norm_maxh(dev) >> 1;

	em2820_outfmt_set_yuv422(dev);
	em2820_accumulator_set(dev, 1, (width - 4) >> 2, 1, (height - 4) >> 2);
	em2820_capture_area_set(dev, 0, 0, width >> 2, height >> 2);
	return em2820_scaler_set(dev, dev->hscale, dev->vscale);
}


/******************* isoc transfer handling ****************************/

#ifdef ENABLE_DEBUG_ISOC_FRAMES
static void em2820_isoc_dump(struct urb *urb, struct pt_regs *regs)
{
	int len = 0;
	int ntrans = 0;
	int i;

	printk(KERN_DEBUG "isocIrq: sf=%d np=%d ec=%x\n",
	       urb->start_frame, urb->number_of_packets,
	       urb->error_count);
	for (i = 0; i < urb->number_of_packets; i++) {
		unsigned char *buf =
				urb->transfer_buffer +
				urb->iso_frame_desc[i].offset;
		int alen = urb->iso_frame_desc[i].actual_length;
		if (alen > 0) {
			if (buf[0] == 0x88) {
				ntrans++;
				len += alen;
			} else if (buf[0] == 0x22) {
				printk(KERN_DEBUG
						"= l=%d nt=%d bpp=%d\n",
				len - 4 * ntrans, ntrans,
				ntrans == 0 ? 0 : len / ntrans);
				ntrans = 1;
				len = alen;
			} else
				printk(KERN_DEBUG "!\n");
		}
		printk(KERN_DEBUG "   n=%d s=%d al=%d %x\n", i,
		       urb->iso_frame_desc[i].status,
		       urb->iso_frame_desc[i].actual_length,
		       (unsigned int)
				       *((unsigned char *)(urb->transfer_buffer +
				       urb->iso_frame_desc[i].
				       offset)));
	}
}
#endif

static inline int em2820_isoc_video(struct em2820 *dev,struct em2820_frame_t **f,
				    unsigned long *lock_flags, unsigned char buf)
{
	if (!(buf & 0x01)) {
		if ((*f)->state == F_GRABBING) {
			/*previous frame is incomplete */
			if ((*f)->fieldbytesused < dev->field_size) {
				(*f)->state = F_ERROR;
				em2820_isocdbg ("dropping incomplete bottom field (%i missing bytes)",
					 dev->field_size-(*f)->fieldbytesused);
			} else {
				(*f)->state = F_DONE;
				(*f)->buf.bytesused = dev->frame_size;
			}
		}
		if ((*f)->state == F_DONE || (*f)->state == F_ERROR) {
			/* move current frame to outqueue and get next free buffer from inqueue */
			spin_lock_irqsave(&dev-> queue_lock, *lock_flags);
			list_move_tail(&(*f)->frame, &dev->outqueue);
			if (!list_empty(&dev->inqueue))
				(*f) = list_entry(dev-> inqueue.next,
			struct em2820_frame_t,frame);
			else
				(*f) = NULL;
			spin_unlock_irqrestore(&dev->queue_lock,*lock_flags);
		}
		if (!(*f)) {
			em2820_isocdbg ("new frame but no buffer is free");
			return -1;
		}
		do_gettimeofday(&(*f)->buf.timestamp);
		(*f)->buf.sequence = ++dev->frame_count;
		(*f)->buf.field = V4L2_FIELD_INTERLACED;
		(*f)->state = F_GRABBING;
		(*f)->buf.bytesused = 0;
		(*f)->top_field = 1;
		(*f)->fieldbytesused = 0;
	} else {
					/* acquiring bottom field */
		if ((*f)->state == F_GRABBING) {
			if (!(*f)->top_field) {
				(*f)->state = F_ERROR;
				em2820_isocdbg ("unexpected begin of bottom field; discarding it");
			} else if ((*f)-> fieldbytesused < dev->field_size - 172) {
				(*f)->state = F_ERROR;
				em2820_isocdbg ("dropping incomplete top field (%i missing bytes)",
					 dev->field_size-(*f)->fieldbytesused);
			} else {
				(*f)->top_field = 0;
				(*f)->fieldbytesused = 0;
			}
		}
	}
	return (0);
}

static inline void em2820_isoc_video_copy(struct em2820 *dev,
					  struct em2820_frame_t **f, unsigned char *buf, int len)
{
	void *fieldstart, *startwrite, *startread;
	int linesdone, currlinedone, offset, lencopy,remain;

	if ((*f)->fieldbytesused + len > dev->field_size)
		len =dev->field_size - (*f)->fieldbytesused;
	remain = len;
	startread = buf + 4;
	if ((*f)->top_field)
		fieldstart = (*f)->bufmem;
	else
		fieldstart = (*f)->bufmem + dev->bytesperline;

	linesdone = (*f)->fieldbytesused / dev->bytesperline;
	currlinedone = (*f)->fieldbytesused % dev->bytesperline;
	offset = linesdone * dev->bytesperline * 2 + currlinedone;
	startwrite = fieldstart + offset;
	lencopy = dev->bytesperline - currlinedone;
	lencopy = lencopy > remain ? remain : lencopy;

	memcpy(startwrite, startread, lencopy);
	remain -= lencopy;

	while (remain > 0) {
		startwrite += lencopy + dev->bytesperline;
		startread += lencopy;
		if (dev->bytesperline > remain)
			lencopy = remain;
		else
			lencopy = dev->bytesperline;

		memcpy(startwrite, startread, lencopy);
		remain -= lencopy;
	}

	(*f)->fieldbytesused += len;
}

/*
 * em2820_isoIrq()
 * handles the incoming isoc urbs and fills the frames from our inqueue
 */
void em2820_isocIrq(struct urb *urb, struct pt_regs *regs)
{
	struct em2820 *dev = urb->context;
	int i, status;
	struct em2820_frame_t **f;
	unsigned long lock_flags;

	if (!dev)
		return;
#ifdef ENABLE_DEBUG_ISOC_FRAMES
	if (isoc_debug>1)
		em2820_isoc_dump(urb, regs);
#endif

	if (urb->status == -ENOENT)
		return;

	f = &dev->frame_current;

	if (dev->stream == STREAM_INTERRUPT) {
		dev->stream = STREAM_OFF;
		if ((*f))
			(*f)->state = F_QUEUED;
		em2820_isocdbg("stream interrupted");
		wake_up_interruptible(&dev->wait_stream);
	}

	if ((dev->state & DEV_DISCONNECTED) || (dev->state & DEV_MISCONFIGURED))
		return;

	if (dev->stream == STREAM_ON && !list_empty(&dev->inqueue)) {
		if (!(*f))
			(*f) = list_entry(dev->inqueue.next,
		struct em2820_frame_t, frame);

		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned char *buf = urb->transfer_buffer +
					urb->iso_frame_desc[i].offset;
			int len = urb->iso_frame_desc[i].actual_length - 4;

			if (urb->iso_frame_desc[i].status) {
				em2820_isocdbg("data error: [%d] len=%d, status=%d", i,
					urb->iso_frame_desc[i].actual_length,
					urb->iso_frame_desc[i].status);
				continue;
			}
			if (urb->iso_frame_desc[i].actual_length <= 0) {
				em2820_isocdbg("packet %d is empty",i);
				continue;
			}
			if (urb->iso_frame_desc[i].actual_length >
						 dev->max_pkt_size) {
				em2820_isocdbg("packet bigger than packet size");
				continue;
			}
			/*new frame */
			if (buf[0] == 0x22 && buf[1] == 0x5a) {
				em2820_isocdbg("Video frame, length=%i!",len);

				if (em2820_isoc_video(dev,f,&lock_flags,buf[2]))
				break;
			} else if (buf[0]==0x33 && buf[1]==0x95 && buf[2]==0x00) {
				em2820_isocdbg("VBI HEADER!!!");
			}

			/* actual copying */
			if ((*f)->state == F_GRABBING) {
				em2820_isoc_video_copy(dev,f,buf, len);
			}
		}
	}

	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

	urb->status = 0;
	if ((status = usb_submit_urb(urb, GFP_ATOMIC))) {
		em2820_errdev("resubmit of urb failed (error=%i)\n", status);
		dev->state |= DEV_MISCONFIGURED;
	}
	wake_up_interruptible(&dev->wait_frame);
	return;
}

/*
 * em2820_uninit_isoc()
 * deallocates the buffers and urbs allocated during em2820_init_iosc()
 */
void em2820_uninit_isoc(struct em2820 *dev)
{
	int i;

	for (i = 0; i < EM2820_NUM_BUFS; i++) {
		if (dev->urb[i]) {
			usb_kill_urb(dev->urb[i]);
			usb_free_urb(dev->urb[i]);
		}
		dev->urb[i] = NULL;
		if (dev->transfer_buffer[i])
			kfree(dev->transfer_buffer[i]);
		dev->transfer_buffer[i] = NULL;
	}
	em2820_capture_start(dev, 0);
}

/*
 * em2820_init_isoc()
 * allocates transfer buffers and submits the urbs for isoc transfer
 */
int em2820_init_isoc(struct em2820 *dev)
{
	/* change interface to 3 which allowes the biggest packet sizes */
	int i, errCode;
	const int sb_size = EM2820_NUM_PACKETS * dev->max_pkt_size;

	/* reset streaming vars */
	dev->frame_current = NULL;
	dev->frame_count = 0;

	/* allocate urbs */
	for (i = 0; i < EM2820_NUM_BUFS; i++) {
		struct urb *urb;
		int j, k;
		/* allocate transfer buffer */
		dev->transfer_buffer[i] = kmalloc(sb_size, GFP_KERNEL);
		if (!dev->transfer_buffer[i]) {
			em2820_errdev
					("unable to allocate %i bytes for transfer buffer %i\n",
					 sb_size, i);
			em2820_uninit_isoc(dev);
			return -ENOMEM;
		}
		memset(dev->transfer_buffer[i], 0, sb_size);
		urb = usb_alloc_urb(EM2820_NUM_PACKETS, GFP_KERNEL);
		if (urb) {
			urb->dev = dev->udev;
			urb->context = dev;
			urb->pipe = usb_rcvisocpipe(dev->udev, 0x82);
			urb->transfer_flags = URB_ISO_ASAP;
			urb->interval = 1;
			urb->transfer_buffer = dev->transfer_buffer[i];
			urb->complete = em2820_isocIrq;
			urb->number_of_packets = EM2820_NUM_PACKETS;
			urb->transfer_buffer_length = sb_size;
			for (j = k = 0; j < EM2820_NUM_PACKETS;
						  j++, k += dev->max_pkt_size) {
							  urb->iso_frame_desc[j].offset = k;
							  urb->iso_frame_desc[j].length =
									  dev->max_pkt_size;
						  }
						  dev->urb[i] = urb;
		} else {
			em2820_errdev("cannot alloc urb %i\n", i);
			em2820_uninit_isoc(dev);
			return -ENOMEM;
		}
	}

	/* submit urbs */
	for (i = 0; i < EM2820_NUM_BUFS; i++) {
		errCode = usb_submit_urb(dev->urb[i], GFP_KERNEL);
		if (errCode) {
			em2820_errdev("submit of urb %i failed (error=%i)\n", i,
				      errCode);
			em2820_uninit_isoc(dev);
			return errCode;
		}
	}

	return 0;
}

int em2820_set_alternate(struct em2820 *dev)
{
	int errCode, prev_alt = dev->alt;
	dev->alt = alt;
	if (dev->alt == 0) {
		int i;
		unsigned int min_pkt_size = dev->field_size / 137;	/* FIXME: empiric magic number */
		em2820_coredbg("minimum isoc packet size: %u", min_pkt_size);
		dev->alt = 7;
		for (i = 1; i < EM2820_MAX_ALT; i += 2)	/* FIXME: skip even alternate: why do they not work? */
			if (dev->alt_max_pkt_size[i] >= min_pkt_size) {
			dev->alt = i;
			break;
			}
	}

	if (dev->alt != prev_alt) {
		dev->max_pkt_size = dev->alt_max_pkt_size[dev->alt];
		em2820_coredbg("setting alternate %d with wMaxPacketSize=%u", dev->alt,
		       dev->max_pkt_size);
		errCode = usb_set_interface(dev->udev, 0, dev->alt);
		if (errCode < 0) {
			em2820_errdev
					("cannot change alternate number to %d (error=%i)\n",
					 dev->alt, errCode);
			return errCode;
		}
	}
	return 0;
}
