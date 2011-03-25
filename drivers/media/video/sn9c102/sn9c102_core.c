/***************************************************************************
 * V4L2 driver for SN9C1xx PC Camera Controllers                           *
 *                                                                         *
 * Copyright (C) 2004-2007 by Luca Risolia <luca.risolia@studio.unibo.it>  *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program; if not, write to the Free Software             *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               *
 ***************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/compiler.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/page-flags.h>
#include <asm/byteorder.h>
#include <asm/page.h>
#include <asm/uaccess.h>

#include "sn9c102.h"

/*****************************************************************************/

#define SN9C102_MODULE_NAME     "V4L2 driver for SN9C1xx PC Camera Controllers"
#define SN9C102_MODULE_ALIAS    "sn9c1xx"
#define SN9C102_MODULE_AUTHOR   "(C) 2004-2007 Luca Risolia"
#define SN9C102_AUTHOR_EMAIL    "<luca.risolia@studio.unibo.it>"
#define SN9C102_MODULE_LICENSE  "GPL"
#define SN9C102_MODULE_VERSION  "1:1.47pre49"
#define SN9C102_MODULE_VERSION_CODE  KERNEL_VERSION(1, 1, 47)

/*****************************************************************************/

MODULE_DEVICE_TABLE(usb, sn9c102_id_table);

MODULE_AUTHOR(SN9C102_MODULE_AUTHOR " " SN9C102_AUTHOR_EMAIL);
MODULE_DESCRIPTION(SN9C102_MODULE_NAME);
MODULE_ALIAS(SN9C102_MODULE_ALIAS);
MODULE_VERSION(SN9C102_MODULE_VERSION);
MODULE_LICENSE(SN9C102_MODULE_LICENSE);

static short video_nr[] = {[0 ... SN9C102_MAX_DEVICES-1] = -1};
module_param_array(video_nr, short, NULL, 0444);
MODULE_PARM_DESC(video_nr,
		 " <-1|n[,...]>"
		 "\nSpecify V4L2 minor mode number."
		 "\n-1 = use next available (default)"
		 "\n n = use minor number n (integer >= 0)"
		 "\nYou can specify up to "__MODULE_STRING(SN9C102_MAX_DEVICES)
		 " cameras this way."
		 "\nFor example:"
		 "\nvideo_nr=-1,2,-1 would assign minor number 2 to"
		 "\nthe second camera and use auto for the first"
		 "\none and for every other camera."
		 "\n");

static short force_munmap[] = {[0 ... SN9C102_MAX_DEVICES-1] =
			       SN9C102_FORCE_MUNMAP};
module_param_array(force_munmap, bool, NULL, 0444);
MODULE_PARM_DESC(force_munmap,
		 " <0|1[,...]>"
		 "\nForce the application to unmap previously"
		 "\nmapped buffer memory before calling any VIDIOC_S_CROP or"
		 "\nVIDIOC_S_FMT ioctl's. Not all the applications support"
		 "\nthis feature. This parameter is specific for each"
		 "\ndetected camera."
		 "\n0 = do not force memory unmapping"
		 "\n1 = force memory unmapping (save memory)"
		 "\nDefault value is "__MODULE_STRING(SN9C102_FORCE_MUNMAP)"."
		 "\n");

static unsigned int frame_timeout[] = {[0 ... SN9C102_MAX_DEVICES-1] =
				       SN9C102_FRAME_TIMEOUT};
module_param_array(frame_timeout, uint, NULL, 0644);
MODULE_PARM_DESC(frame_timeout,
		 " <0|n[,...]>"
		 "\nTimeout for a video frame in seconds before"
		 "\nreturning an I/O error; 0 for infinity."
		 "\nThis parameter is specific for each detected camera."
		 "\nDefault value is "__MODULE_STRING(SN9C102_FRAME_TIMEOUT)"."
		 "\n");

#ifdef SN9C102_DEBUG
static unsigned short debug = SN9C102_DEBUG_LEVEL;
module_param(debug, ushort, 0644);
MODULE_PARM_DESC(debug,
		 " <n>"
		 "\nDebugging information level, from 0 to 3:"
		 "\n0 = none (use carefully)"
		 "\n1 = critical errors"
		 "\n2 = significant informations"
		 "\n3 = more verbose messages"
		 "\nLevel 3 is useful for testing only."
		 "\nDefault value is "__MODULE_STRING(SN9C102_DEBUG_LEVEL)"."
		 "\n");
#endif

/*
   Add the probe entries to this table. Be sure to add the entry in the right
   place, since, on failure, the next probing routine is called according to
   the order of the list below, from top to bottom.
*/
static int (*sn9c102_sensor_table[])(struct sn9c102_device *) = {
	&sn9c102_probe_hv7131d, /* strong detection based on SENSOR ids */
	&sn9c102_probe_hv7131r, /* strong detection based on SENSOR ids */
	&sn9c102_probe_mi0343, /* strong detection based on SENSOR ids */
	&sn9c102_probe_mi0360, /* strong detection based on SENSOR ids */
	&sn9c102_probe_mt9v111, /* strong detection based on SENSOR ids */
	&sn9c102_probe_pas106b, /* strong detection based on SENSOR ids */
	&sn9c102_probe_pas202bcb, /* strong detection based on SENSOR ids */
	&sn9c102_probe_ov7630, /* strong detection based on SENSOR ids */
	&sn9c102_probe_ov7660, /* strong detection based on SENSOR ids */
	&sn9c102_probe_tas5110c1b, /* detection based on USB pid/vid */
	&sn9c102_probe_tas5110d, /* detection based on USB pid/vid */
	&sn9c102_probe_tas5130d1b, /* detection based on USB pid/vid */
};

/*****************************************************************************/

static u32
sn9c102_request_buffers(struct sn9c102_device* cam, u32 count,
			enum sn9c102_io_method io)
{
	struct v4l2_pix_format* p = &(cam->sensor.pix_format);
	struct v4l2_rect* r = &(cam->sensor.cropcap.bounds);
	size_t imagesize = cam->module_param.force_munmap || io == IO_READ ?
			   (p->width * p->height * p->priv) / 8 :
			   (r->width * r->height * p->priv) / 8;
	void* buff = NULL;
	u32 i;

	if (count > SN9C102_MAX_FRAMES)
		count = SN9C102_MAX_FRAMES;

	if (cam->bridge == BRIDGE_SN9C105 || cam->bridge == BRIDGE_SN9C120)
		imagesize += 589 + 2; /* length of JPEG header + EOI marker */

	cam->nbuffers = count;
	while (cam->nbuffers > 0) {
		if ((buff = vmalloc_32_user(cam->nbuffers *
					    PAGE_ALIGN(imagesize))))
			break;
		cam->nbuffers--;
	}

	for (i = 0; i < cam->nbuffers; i++) {
		cam->frame[i].bufmem = buff + i*PAGE_ALIGN(imagesize);
		cam->frame[i].buf.index = i;
		cam->frame[i].buf.m.offset = i*PAGE_ALIGN(imagesize);
		cam->frame[i].buf.length = imagesize;
		cam->frame[i].buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cam->frame[i].buf.sequence = 0;
		cam->frame[i].buf.field = V4L2_FIELD_NONE;
		cam->frame[i].buf.memory = V4L2_MEMORY_MMAP;
		cam->frame[i].buf.flags = 0;
	}

	return cam->nbuffers;
}


static void sn9c102_release_buffers(struct sn9c102_device* cam)
{
	if (cam->nbuffers) {
		vfree(cam->frame[0].bufmem);
		cam->nbuffers = 0;
	}
	cam->frame_current = NULL;
}


static void sn9c102_empty_framequeues(struct sn9c102_device* cam)
{
	u32 i;

	INIT_LIST_HEAD(&cam->inqueue);
	INIT_LIST_HEAD(&cam->outqueue);

	for (i = 0; i < SN9C102_MAX_FRAMES; i++) {
		cam->frame[i].state = F_UNUSED;
		cam->frame[i].buf.bytesused = 0;
	}
}


static void sn9c102_requeue_outqueue(struct sn9c102_device* cam)
{
	struct sn9c102_frame_t *i;

	list_for_each_entry(i, &cam->outqueue, frame) {
		i->state = F_QUEUED;
		list_add(&i->frame, &cam->inqueue);
	}

	INIT_LIST_HEAD(&cam->outqueue);
}


static void sn9c102_queue_unusedframes(struct sn9c102_device* cam)
{
	unsigned long lock_flags;
	u32 i;

	for (i = 0; i < cam->nbuffers; i++)
		if (cam->frame[i].state == F_UNUSED) {
			cam->frame[i].state = F_QUEUED;
			spin_lock_irqsave(&cam->queue_lock, lock_flags);
			list_add_tail(&cam->frame[i].frame, &cam->inqueue);
			spin_unlock_irqrestore(&cam->queue_lock, lock_flags);
		}
}

/*****************************************************************************/

/*
   Write a sequence of count value/register pairs. Returns -1 after the first
   failed write, or 0 for no errors.
*/
int sn9c102_write_regs(struct sn9c102_device* cam, const u8 valreg[][2],
		       int count)
{
	struct usb_device* udev = cam->usbdev;
	u8* buff = cam->control_buffer;
	int i, res;

	for (i = 0; i < count; i++) {
		u8 index = valreg[i][1];

		/*
		   index is a u8, so it must be <256 and can't be out of range.
		   If we put in a check anyway, gcc annoys us with a warning
		   hat our check is useless. People get all uppity when they
		   see warnings in the kernel compile.
		*/

		*buff = valreg[i][0];

		res = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x08,
				      0x41, index, 0, buff, 1,
				      SN9C102_CTRL_TIMEOUT);

		if (res < 0) {
			DBG(3, "Failed to write a register (value 0x%02X, "
			       "index 0x%02X, error %d)", *buff, index, res);
			return -1;
		}

		cam->reg[index] = *buff;
	}

	return 0;
}


int sn9c102_write_reg(struct sn9c102_device* cam, u8 value, u16 index)
{
	struct usb_device* udev = cam->usbdev;
	u8* buff = cam->control_buffer;
	int res;

	if (index >= ARRAY_SIZE(cam->reg))
		return -1;

	*buff = value;

	res = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x08, 0x41,
			      index, 0, buff, 1, SN9C102_CTRL_TIMEOUT);
	if (res < 0) {
		DBG(3, "Failed to write a register (value 0x%02X, index "
		       "0x%02X, error %d)", value, index, res);
		return -1;
	}

	cam->reg[index] = value;

	return 0;
}


/* NOTE: with the SN9C10[123] reading some registers always returns 0 */
int sn9c102_read_reg(struct sn9c102_device* cam, u16 index)
{
	struct usb_device* udev = cam->usbdev;
	u8* buff = cam->control_buffer;
	int res;

	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0x00, 0xc1,
			      index, 0, buff, 1, SN9C102_CTRL_TIMEOUT);
	if (res < 0)
		DBG(3, "Failed to read a register (index 0x%02X, error %d)",
		    index, res);

	return (res >= 0) ? (int)(*buff) : -1;
}


int sn9c102_pread_reg(struct sn9c102_device* cam, u16 index)
{
	if (index >= ARRAY_SIZE(cam->reg))
		return -1;

	return cam->reg[index];
}


static int
sn9c102_i2c_wait(struct sn9c102_device* cam,
		 const struct sn9c102_sensor* sensor)
{
	int i, r;

	for (i = 1; i <= 5; i++) {
		r = sn9c102_read_reg(cam, 0x08);
		if (r < 0)
			return -EIO;
		if (r & 0x04)
			return 0;
		if (sensor->frequency & SN9C102_I2C_400KHZ)
			udelay(5*16);
		else
			udelay(16*16);
	}
	return -EBUSY;
}


static int
sn9c102_i2c_detect_read_error(struct sn9c102_device* cam,
			      const struct sn9c102_sensor* sensor)
{
	int r , err = 0;

	r = sn9c102_read_reg(cam, 0x08);
	if (r < 0)
		err += r;

	if (cam->bridge == BRIDGE_SN9C101 || cam->bridge == BRIDGE_SN9C102) {
		if (!(r & 0x08))
			err += -1;
	} else {
		if (r & 0x08)
			err += -1;
	}

	return err ? -EIO : 0;
}


static int
sn9c102_i2c_detect_write_error(struct sn9c102_device* cam,
			       const struct sn9c102_sensor* sensor)
{
	int r;
	r = sn9c102_read_reg(cam, 0x08);
	return (r < 0 || (r >= 0 && (r & 0x08))) ? -EIO : 0;
}


int
sn9c102_i2c_try_raw_read(struct sn9c102_device* cam,
			 const struct sn9c102_sensor* sensor, u8 data0,
			 u8 data1, u8 n, u8 buffer[])
{
	struct usb_device* udev = cam->usbdev;
	u8* data = cam->control_buffer;
	int i = 0, err = 0, res;

	/* Write cycle */
	data[0] = ((sensor->interface == SN9C102_I2C_2WIRES) ? 0x80 : 0) |
		  ((sensor->frequency & SN9C102_I2C_400KHZ) ? 0x01 : 0) | 0x10;
	data[1] = data0; /* I2C slave id */
	data[2] = data1; /* address */
	data[7] = 0x10;
	res = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x08, 0x41,
			      0x08, 0, data, 8, SN9C102_CTRL_TIMEOUT);
	if (res < 0)
		err += res;

	err += sn9c102_i2c_wait(cam, sensor);

	/* Read cycle - n bytes */
	data[0] = ((sensor->interface == SN9C102_I2C_2WIRES) ? 0x80 : 0) |
		  ((sensor->frequency & SN9C102_I2C_400KHZ) ? 0x01 : 0) |
		  (n << 4) | 0x02;
	data[1] = data0;
	data[7] = 0x10;
	res = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x08, 0x41,
			      0x08, 0, data, 8, SN9C102_CTRL_TIMEOUT);
	if (res < 0)
		err += res;

	err += sn9c102_i2c_wait(cam, sensor);

	/* The first read byte will be placed in data[4] */
	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0x00, 0xc1,
			      0x0a, 0, data, 5, SN9C102_CTRL_TIMEOUT);
	if (res < 0)
		err += res;

	err += sn9c102_i2c_detect_read_error(cam, sensor);

	PDBGG("I2C read: address 0x%02X, first read byte: 0x%02X", data1,
	      data[4]);

	if (err) {
		DBG(3, "I2C read failed for %s image sensor", sensor->name);
		return -1;
	}

	if (buffer)
		for (i = 0; i < n && i < 5; i++)
			buffer[n-i-1] = data[4-i];

	return (int)data[4];
}


int
sn9c102_i2c_try_raw_write(struct sn9c102_device* cam,
			  const struct sn9c102_sensor* sensor, u8 n, u8 data0,
			  u8 data1, u8 data2, u8 data3, u8 data4, u8 data5)
{
	struct usb_device* udev = cam->usbdev;
	u8* data = cam->control_buffer;
	int err = 0, res;

	/* Write cycle. It usually is address + value */
	data[0] = ((sensor->interface == SN9C102_I2C_2WIRES) ? 0x80 : 0) |
		  ((sensor->frequency & SN9C102_I2C_400KHZ) ? 0x01 : 0)
		  | ((n - 1) << 4);
	data[1] = data0;
	data[2] = data1;
	data[3] = data2;
	data[4] = data3;
	data[5] = data4;
	data[6] = data5;
	data[7] = 0x17;
	res = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x08, 0x41,
			      0x08, 0, data, 8, SN9C102_CTRL_TIMEOUT);
	if (res < 0)
		err += res;

	err += sn9c102_i2c_wait(cam, sensor);
	err += sn9c102_i2c_detect_write_error(cam, sensor);

	if (err)
		DBG(3, "I2C write failed for %s image sensor", sensor->name);

	PDBGG("I2C raw write: %u bytes, data0 = 0x%02X, data1 = 0x%02X, "
	      "data2 = 0x%02X, data3 = 0x%02X, data4 = 0x%02X, data5 = 0x%02X",
	      n, data0, data1, data2, data3, data4, data5);

	return err ? -1 : 0;
}


int
sn9c102_i2c_try_read(struct sn9c102_device* cam,
		     const struct sn9c102_sensor* sensor, u8 address)
{
	return sn9c102_i2c_try_raw_read(cam, sensor, sensor->i2c_slave_id,
					address, 1, NULL);
}


static int sn9c102_i2c_try_write(struct sn9c102_device* cam,
				 const struct sn9c102_sensor* sensor,
				 u8 address, u8 value)
{
	return sn9c102_i2c_try_raw_write(cam, sensor, 3,
					 sensor->i2c_slave_id, address,
					 value, 0, 0, 0);
}


int sn9c102_i2c_read(struct sn9c102_device* cam, u8 address)
{
	return sn9c102_i2c_try_read(cam, &cam->sensor, address);
}


int sn9c102_i2c_write(struct sn9c102_device* cam, u8 address, u8 value)
{
	return sn9c102_i2c_try_write(cam, &cam->sensor, address, value);
}

/*****************************************************************************/

static size_t sn9c102_sof_length(struct sn9c102_device* cam)
{
	switch (cam->bridge) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
		return 12;
	case BRIDGE_SN9C103:
		return 18;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		return 62;
	}

	return 0;
}


static void*
sn9c102_find_sof_header(struct sn9c102_device* cam, void* mem, size_t len)
{
	static const char marker[6] = {0xff, 0xff, 0x00, 0xc4, 0xc4, 0x96};
	const char *m = mem;
	size_t soflen = 0, i, j;

	soflen = sn9c102_sof_length(cam);

	for (i = 0; i < len; i++) {
		size_t b;

		/* Read the variable part of the header */
		if (unlikely(cam->sof.bytesread >= sizeof(marker))) {
			cam->sof.header[cam->sof.bytesread] = *(m+i);
			if (++cam->sof.bytesread == soflen) {
				cam->sof.bytesread = 0;
				return mem + i;
			}
			continue;
		}

		/* Search for the SOF marker (fixed part) in the header */
		for (j = 0, b=cam->sof.bytesread; j+b < sizeof(marker); j++) {
			if (unlikely(i+j == len))
				return NULL;
			if (*(m+i+j) == marker[cam->sof.bytesread]) {
				cam->sof.header[cam->sof.bytesread] = *(m+i+j);
				if (++cam->sof.bytesread == sizeof(marker)) {
					PDBGG("Bytes to analyze: %zd. SOF "
					      "starts at byte #%zd", len, i);
					i += j+1;
					break;
				}
			} else {
				cam->sof.bytesread = 0;
				break;
			}
		}
	}

	return NULL;
}


static void*
sn9c102_find_eof_header(struct sn9c102_device* cam, void* mem, size_t len)
{
	static const u8 eof_header[4][4] = {
		{0x00, 0x00, 0x00, 0x00},
		{0x40, 0x00, 0x00, 0x00},
		{0x80, 0x00, 0x00, 0x00},
		{0xc0, 0x00, 0x00, 0x00},
	};
	size_t i, j;

	/* The EOF header does not exist in compressed data */
	if (cam->sensor.pix_format.pixelformat == V4L2_PIX_FMT_SN9C10X ||
	    cam->sensor.pix_format.pixelformat == V4L2_PIX_FMT_JPEG)
		return NULL;

	/*
	   The EOF header might cross the packet boundary, but this is not a
	   problem, since the end of a frame is determined by checking its size
	   in the first place.
	*/
	for (i = 0; (len >= 4) && (i <= len - 4); i++)
		for (j = 0; j < ARRAY_SIZE(eof_header); j++)
			if (!memcmp(mem + i, eof_header[j], 4))
				return mem + i;

	return NULL;
}


static void
sn9c102_write_jpegheader(struct sn9c102_device* cam, struct sn9c102_frame_t* f)
{
	static const u8 jpeg_header[589] = {
		0xff, 0xd8, 0xff, 0xdb, 0x00, 0x84, 0x00, 0x06, 0x04, 0x05,
		0x06, 0x05, 0x04, 0x06, 0x06, 0x05, 0x06, 0x07, 0x07, 0x06,
		0x08, 0x0a, 0x10, 0x0a, 0x0a, 0x09, 0x09, 0x0a, 0x14, 0x0e,
		0x0f, 0x0c, 0x10, 0x17, 0x14, 0x18, 0x18, 0x17, 0x14, 0x16,
		0x16, 0x1a, 0x1d, 0x25, 0x1f, 0x1a, 0x1b, 0x23, 0x1c, 0x16,
		0x16, 0x20, 0x2c, 0x20, 0x23, 0x26, 0x27, 0x29, 0x2a, 0x29,
		0x19, 0x1f, 0x2d, 0x30, 0x2d, 0x28, 0x30, 0x25, 0x28, 0x29,
		0x28, 0x01, 0x07, 0x07, 0x07, 0x0a, 0x08, 0x0a, 0x13, 0x0a,
		0x0a, 0x13, 0x28, 0x1a, 0x16, 0x1a, 0x28, 0x28, 0x28, 0x28,
		0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
		0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
		0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
		0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
		0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0xff, 0xc4, 0x01, 0xa2,
		0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
		0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x01,
		0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
		0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x10, 0x00,
		0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04,
		0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04,
		0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
		0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23,
		0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62,
		0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25,
		0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38,
		0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
		0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
		0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76,
		0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
		0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
		0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
		0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2,
		0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3,
		0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3,
		0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3,
		0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0x11, 0x00, 0x02,
		0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04,
		0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
		0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
		0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1,
		0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
		0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19,
		0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
		0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
		0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
		0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76,
		0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
		0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
		0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
		0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
		0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
		0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3,
		0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
		0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc0, 0x00, 0x11,
		0x08, 0x01, 0xe0, 0x02, 0x80, 0x03, 0x01, 0x21, 0x00, 0x02,
		0x11, 0x01, 0x03, 0x11, 0x01, 0xff, 0xda, 0x00, 0x0c, 0x03,
		0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00
	};
	u8 *pos = f->bufmem;

	memcpy(pos, jpeg_header, sizeof(jpeg_header));
	*(pos + 6) = 0x00;
	*(pos + 7 + 64) = 0x01;
	if (cam->compression.quality == 0) {
		memcpy(pos + 7, SN9C102_Y_QTABLE0, 64);
		memcpy(pos + 8 + 64, SN9C102_UV_QTABLE0, 64);
	} else if (cam->compression.quality == 1) {
		memcpy(pos + 7, SN9C102_Y_QTABLE1, 64);
		memcpy(pos + 8 + 64, SN9C102_UV_QTABLE1, 64);
	}
	*(pos + 564) = cam->sensor.pix_format.width & 0xFF;
	*(pos + 563) = (cam->sensor.pix_format.width >> 8) & 0xFF;
	*(pos + 562) = cam->sensor.pix_format.height & 0xFF;
	*(pos + 561) = (cam->sensor.pix_format.height >> 8) & 0xFF;
	*(pos + 567) = 0x21;

	f->buf.bytesused += sizeof(jpeg_header);
}


static void sn9c102_urb_complete(struct urb *urb)
{
	struct sn9c102_device* cam = urb->context;
	struct sn9c102_frame_t** f;
	size_t imagesize, soflen;
	u8 i;
	int err = 0;

	if (urb->status == -ENOENT)
		return;

	f = &cam->frame_current;

	if (cam->stream == STREAM_INTERRUPT) {
		cam->stream = STREAM_OFF;
		if ((*f))
			(*f)->state = F_QUEUED;
		cam->sof.bytesread = 0;
		DBG(3, "Stream interrupted by application");
		wake_up(&cam->wait_stream);
	}

	if (cam->state & DEV_DISCONNECTED)
		return;

	if (cam->state & DEV_MISCONFIGURED) {
		wake_up_interruptible(&cam->wait_frame);
		return;
	}

	if (cam->stream == STREAM_OFF || list_empty(&cam->inqueue))
		goto resubmit_urb;

	if (!(*f))
		(*f) = list_entry(cam->inqueue.next, struct sn9c102_frame_t,
				  frame);

	imagesize = (cam->sensor.pix_format.width *
		     cam->sensor.pix_format.height *
		     cam->sensor.pix_format.priv) / 8;
	if (cam->sensor.pix_format.pixelformat == V4L2_PIX_FMT_JPEG)
		imagesize += 589; /* length of jpeg header */
	soflen = sn9c102_sof_length(cam);

	for (i = 0; i < urb->number_of_packets; i++) {
		unsigned int img, len, status;
		void *pos, *sof, *eof;

		len = urb->iso_frame_desc[i].actual_length;
		status = urb->iso_frame_desc[i].status;
		pos = urb->iso_frame_desc[i].offset + urb->transfer_buffer;

		if (status) {
			DBG(3, "Error in isochronous frame");
			(*f)->state = F_ERROR;
			cam->sof.bytesread = 0;
			continue;
		}

		PDBGG("Isochrnous frame: length %u, #%u i", len, i);

redo:
		sof = sn9c102_find_sof_header(cam, pos, len);
		if (likely(!sof)) {
			eof = sn9c102_find_eof_header(cam, pos, len);
			if ((*f)->state == F_GRABBING) {
end_of_frame:
				img = len;

				if (eof)
					img = (eof > pos) ? eof - pos - 1 : 0;

				if ((*f)->buf.bytesused + img > imagesize) {
					u32 b;
					b = (*f)->buf.bytesused + img -
					    imagesize;
					img = imagesize - (*f)->buf.bytesused;
					PDBGG("Expected EOF not found: video "
					      "frame cut");
					if (eof)
						DBG(3, "Exceeded limit: +%u "
						       "bytes", (unsigned)(b));
				}

				memcpy((*f)->bufmem + (*f)->buf.bytesused, pos,
				       img);

				if ((*f)->buf.bytesused == 0)
					do_gettimeofday(&(*f)->buf.timestamp);

				(*f)->buf.bytesused += img;

				if ((*f)->buf.bytesused == imagesize ||
				    ((cam->sensor.pix_format.pixelformat ==
				      V4L2_PIX_FMT_SN9C10X ||
				      cam->sensor.pix_format.pixelformat ==
				      V4L2_PIX_FMT_JPEG) && eof)) {
					u32 b;

					b = (*f)->buf.bytesused;
					(*f)->state = F_DONE;
					(*f)->buf.sequence= ++cam->frame_count;

					spin_lock(&cam->queue_lock);
					list_move_tail(&(*f)->frame,
						       &cam->outqueue);
					if (!list_empty(&cam->inqueue))
						(*f) = list_entry(
							cam->inqueue.next,
							struct sn9c102_frame_t,
							frame );
					else
						(*f) = NULL;
					spin_unlock(&cam->queue_lock);

					memcpy(cam->sysfs.frame_header,
					       cam->sof.header, soflen);

					DBG(3, "Video frame captured: %lu "
					       "bytes", (unsigned long)(b));

					if (!(*f))
						goto resubmit_urb;

				} else if (eof) {
					(*f)->state = F_ERROR;
					DBG(3, "Not expected EOF after %lu "
					       "bytes of image data",
					    (unsigned long)
					    ((*f)->buf.bytesused));
				}

				if (sof) /* (1) */
					goto start_of_frame;

			} else if (eof) {
				DBG(3, "EOF without SOF");
				continue;

			} else {
				PDBGG("Ignoring pointless isochronous frame");
				continue;
			}

		} else if ((*f)->state == F_QUEUED || (*f)->state == F_ERROR) {
start_of_frame:
			(*f)->state = F_GRABBING;
			(*f)->buf.bytesused = 0;
			len -= (sof - pos);
			pos = sof;
			if (cam->sensor.pix_format.pixelformat ==
			    V4L2_PIX_FMT_JPEG)
				sn9c102_write_jpegheader(cam, (*f));
			DBG(3, "SOF detected: new video frame");
			if (len)
				goto redo;

		} else if ((*f)->state == F_GRABBING) {
			eof = sn9c102_find_eof_header(cam, pos, len);
			if (eof && eof < sof)
				goto end_of_frame; /* (1) */
			else {
				if (cam->sensor.pix_format.pixelformat ==
				    V4L2_PIX_FMT_SN9C10X ||
				    cam->sensor.pix_format.pixelformat ==
				    V4L2_PIX_FMT_JPEG) {
					if (sof - pos >= soflen) {
						eof = sof - soflen;
					} else { /* remove header */
						eof = pos;
						(*f)->buf.bytesused -=
							(soflen - (sof - pos));
					}
					goto end_of_frame;
				} else {
					DBG(3, "SOF before expected EOF after "
					       "%lu bytes of image data",
					    (unsigned long)
					    ((*f)->buf.bytesused));
					goto start_of_frame;
				}
			}
		}
	}

resubmit_urb:
	urb->dev = cam->usbdev;
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0 && err != -EPERM) {
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "usb_submit_urb() failed");
	}

	wake_up_interruptible(&cam->wait_frame);
}


static int sn9c102_start_transfer(struct sn9c102_device* cam)
{
	struct usb_device *udev = cam->usbdev;
	struct urb* urb;
	struct usb_host_interface* altsetting = usb_altnum_to_altsetting(
						    usb_ifnum_to_if(udev, 0),
						    SN9C102_ALTERNATE_SETTING);
	const unsigned int psz = le16_to_cpu(altsetting->
					     endpoint[0].desc.wMaxPacketSize);
	s8 i, j;
	int err = 0;

	for (i = 0; i < SN9C102_URBS; i++) {
		cam->transfer_buffer[i] = kzalloc(SN9C102_ISO_PACKETS * psz,
						  GFP_KERNEL);
		if (!cam->transfer_buffer[i]) {
			err = -ENOMEM;
			DBG(1, "Not enough memory");
			goto free_buffers;
		}
	}

	for (i = 0; i < SN9C102_URBS; i++) {
		urb = usb_alloc_urb(SN9C102_ISO_PACKETS, GFP_KERNEL);
		cam->urb[i] = urb;
		if (!urb) {
			err = -ENOMEM;
			DBG(1, "usb_alloc_urb() failed");
			goto free_urbs;
		}
		urb->dev = udev;
		urb->context = cam;
		urb->pipe = usb_rcvisocpipe(udev, 1);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->number_of_packets = SN9C102_ISO_PACKETS;
		urb->complete = sn9c102_urb_complete;
		urb->transfer_buffer = cam->transfer_buffer[i];
		urb->transfer_buffer_length = psz * SN9C102_ISO_PACKETS;
		urb->interval = 1;
		for (j = 0; j < SN9C102_ISO_PACKETS; j++) {
			urb->iso_frame_desc[j].offset = psz * j;
			urb->iso_frame_desc[j].length = psz;
		}
	}

	/* Enable video */
	if (!(cam->reg[0x01] & 0x04)) {
		err = sn9c102_write_reg(cam, cam->reg[0x01] | 0x04, 0x01);
		if (err) {
			err = -EIO;
			DBG(1, "I/O hardware error");
			goto free_urbs;
		}
	}

	err = usb_set_interface(udev, 0, SN9C102_ALTERNATE_SETTING);
	if (err) {
		DBG(1, "usb_set_interface() failed");
		goto free_urbs;
	}

	cam->frame_current = NULL;
	cam->sof.bytesread = 0;

	for (i = 0; i < SN9C102_URBS; i++) {
		err = usb_submit_urb(cam->urb[i], GFP_KERNEL);
		if (err) {
			for (j = i-1; j >= 0; j--)
				usb_kill_urb(cam->urb[j]);
			DBG(1, "usb_submit_urb() failed, error %d", err);
			goto free_urbs;
		}
	}

	return 0;

free_urbs:
	for (i = 0; (i < SN9C102_URBS) && cam->urb[i]; i++)
		usb_free_urb(cam->urb[i]);

free_buffers:
	for (i = 0; (i < SN9C102_URBS) && cam->transfer_buffer[i]; i++)
		kfree(cam->transfer_buffer[i]);

	return err;
}


static int sn9c102_stop_transfer(struct sn9c102_device* cam)
{
	struct usb_device *udev = cam->usbdev;
	s8 i;
	int err = 0;

	if (cam->state & DEV_DISCONNECTED)
		return 0;

	for (i = SN9C102_URBS-1; i >= 0; i--) {
		usb_kill_urb(cam->urb[i]);
		usb_free_urb(cam->urb[i]);
		kfree(cam->transfer_buffer[i]);
	}

	err = usb_set_interface(udev, 0, 0); /* 0 Mb/s */
	if (err)
		DBG(3, "usb_set_interface() failed");

	return err;
}


static int sn9c102_stream_interrupt(struct sn9c102_device* cam)
{
	long timeout;

	cam->stream = STREAM_INTERRUPT;
	timeout = wait_event_timeout(cam->wait_stream,
				     (cam->stream == STREAM_OFF) ||
				     (cam->state & DEV_DISCONNECTED),
				     SN9C102_URB_TIMEOUT);
	if (cam->state & DEV_DISCONNECTED)
		return -ENODEV;
	else if (cam->stream != STREAM_OFF) {
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "URB timeout reached. The camera is misconfigured. "
		       "To use it, close and open %s again.",
		    video_device_node_name(cam->v4ldev));
		return -EIO;
	}

	return 0;
}

/*****************************************************************************/

#ifdef CONFIG_VIDEO_ADV_DEBUG
static u16 sn9c102_strtou16(const char* buff, size_t len, ssize_t* count)
{
	char str[7];
	char* endp;
	unsigned long val;

	if (len < 6) {
		strncpy(str, buff, len);
		str[len] = '\0';
	} else {
		strncpy(str, buff, 6);
		str[6] = '\0';
	}

	val = simple_strtoul(str, &endp, 0);

	*count = 0;
	if (val <= 0xffff)
		*count = (ssize_t)(endp - str);
	if ((*count) && (len == *count+1) && (buff[*count] == '\n'))
		*count += 1;

	return (u16)val;
}

/*
   NOTE 1: being inside one of the following methods implies that the v4l
	   device exists for sure (see kobjects and reference counters)
   NOTE 2: buffers are PAGE_SIZE long
*/

static ssize_t sn9c102_show_reg(struct device* cd,
				struct device_attribute *attr, char* buf)
{
	struct sn9c102_device* cam;
	ssize_t count;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	count = sprintf(buf, "%u\n", cam->sysfs.reg);

	mutex_unlock(&sn9c102_sysfs_lock);

	return count;
}


static ssize_t
sn9c102_store_reg(struct device* cd, struct device_attribute *attr,
		  const char* buf, size_t len)
{
	struct sn9c102_device* cam;
	u16 index;
	ssize_t count;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	index = sn9c102_strtou16(buf, len, &count);
	if (index >= ARRAY_SIZE(cam->reg) || !count) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -EINVAL;
	}

	cam->sysfs.reg = index;

	DBG(2, "Moved SN9C1XX register index to 0x%02X", cam->sysfs.reg);
	DBG(3, "Written bytes: %zd", count);

	mutex_unlock(&sn9c102_sysfs_lock);

	return count;
}


static ssize_t sn9c102_show_val(struct device* cd,
				struct device_attribute *attr, char* buf)
{
	struct sn9c102_device* cam;
	ssize_t count;
	int val;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	if ((val = sn9c102_read_reg(cam, cam->sysfs.reg)) < 0) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -EIO;
	}

	count = sprintf(buf, "%d\n", val);

	DBG(3, "Read bytes: %zd, value: %d", count, val);

	mutex_unlock(&sn9c102_sysfs_lock);

	return count;
}


static ssize_t
sn9c102_store_val(struct device* cd, struct device_attribute *attr,
		  const char* buf, size_t len)
{
	struct sn9c102_device* cam;
	u16 value;
	ssize_t count;
	int err;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	value = sn9c102_strtou16(buf, len, &count);
	if (!count) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -EINVAL;
	}

	err = sn9c102_write_reg(cam, value, cam->sysfs.reg);
	if (err) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -EIO;
	}

	DBG(2, "Written SN9C1XX reg. 0x%02X, val. 0x%02X",
	    cam->sysfs.reg, value);
	DBG(3, "Written bytes: %zd", count);

	mutex_unlock(&sn9c102_sysfs_lock);

	return count;
}


static ssize_t sn9c102_show_i2c_reg(struct device* cd,
				    struct device_attribute *attr, char* buf)
{
	struct sn9c102_device* cam;
	ssize_t count;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	count = sprintf(buf, "%u\n", cam->sysfs.i2c_reg);

	DBG(3, "Read bytes: %zd", count);

	mutex_unlock(&sn9c102_sysfs_lock);

	return count;
}


static ssize_t
sn9c102_store_i2c_reg(struct device* cd, struct device_attribute *attr,
		      const char* buf, size_t len)
{
	struct sn9c102_device* cam;
	u16 index;
	ssize_t count;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	index = sn9c102_strtou16(buf, len, &count);
	if (!count) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -EINVAL;
	}

	cam->sysfs.i2c_reg = index;

	DBG(2, "Moved sensor register index to 0x%02X", cam->sysfs.i2c_reg);
	DBG(3, "Written bytes: %zd", count);

	mutex_unlock(&sn9c102_sysfs_lock);

	return count;
}


static ssize_t sn9c102_show_i2c_val(struct device* cd,
				    struct device_attribute *attr, char* buf)
{
	struct sn9c102_device* cam;
	ssize_t count;
	int val;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	if (!(cam->sensor.sysfs_ops & SN9C102_I2C_READ)) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENOSYS;
	}

	if ((val = sn9c102_i2c_read(cam, cam->sysfs.i2c_reg)) < 0) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -EIO;
	}

	count = sprintf(buf, "%d\n", val);

	DBG(3, "Read bytes: %zd, value: %d", count, val);

	mutex_unlock(&sn9c102_sysfs_lock);

	return count;
}


static ssize_t
sn9c102_store_i2c_val(struct device* cd, struct device_attribute *attr,
		      const char* buf, size_t len)
{
	struct sn9c102_device* cam;
	u16 value;
	ssize_t count;
	int err;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	if (!(cam->sensor.sysfs_ops & SN9C102_I2C_WRITE)) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENOSYS;
	}

	value = sn9c102_strtou16(buf, len, &count);
	if (!count) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -EINVAL;
	}

	err = sn9c102_i2c_write(cam, cam->sysfs.i2c_reg, value);
	if (err) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -EIO;
	}

	DBG(2, "Written sensor reg. 0x%02X, val. 0x%02X",
	    cam->sysfs.i2c_reg, value);
	DBG(3, "Written bytes: %zd", count);

	mutex_unlock(&sn9c102_sysfs_lock);

	return count;
}


static ssize_t
sn9c102_store_green(struct device* cd, struct device_attribute *attr,
		    const char* buf, size_t len)
{
	struct sn9c102_device* cam;
	enum sn9c102_bridge bridge;
	ssize_t res = 0;
	u16 value;
	ssize_t count;

	if (mutex_lock_interruptible(&sn9c102_sysfs_lock))
		return -ERESTARTSYS;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam) {
		mutex_unlock(&sn9c102_sysfs_lock);
		return -ENODEV;
	}

	bridge = cam->bridge;

	mutex_unlock(&sn9c102_sysfs_lock);

	value = sn9c102_strtou16(buf, len, &count);
	if (!count)
		return -EINVAL;

	switch (bridge) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
		if (value > 0x0f)
			return -EINVAL;
		if ((res = sn9c102_store_reg(cd, attr, "0x11", 4)) >= 0)
			res = sn9c102_store_val(cd, attr, buf, len);
		break;
	case BRIDGE_SN9C103:
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		if (value > 0x7f)
			return -EINVAL;
		if ((res = sn9c102_store_reg(cd, attr, "0x07", 4)) >= 0)
			res = sn9c102_store_val(cd, attr, buf, len);
		break;
	}

	return res;
}


static ssize_t
sn9c102_store_blue(struct device* cd, struct device_attribute *attr,
		   const char* buf, size_t len)
{
	ssize_t res = 0;
	u16 value;
	ssize_t count;

	value = sn9c102_strtou16(buf, len, &count);
	if (!count || value > 0x7f)
		return -EINVAL;

	if ((res = sn9c102_store_reg(cd, attr, "0x06", 4)) >= 0)
		res = sn9c102_store_val(cd, attr, buf, len);

	return res;
}


static ssize_t
sn9c102_store_red(struct device* cd, struct device_attribute *attr,
		  const char* buf, size_t len)
{
	ssize_t res = 0;
	u16 value;
	ssize_t count;

	value = sn9c102_strtou16(buf, len, &count);
	if (!count || value > 0x7f)
		return -EINVAL;

	if ((res = sn9c102_store_reg(cd, attr, "0x05", 4)) >= 0)
		res = sn9c102_store_val(cd, attr, buf, len);

	return res;
}


static ssize_t sn9c102_show_frame_header(struct device* cd,
					 struct device_attribute *attr,
					 char* buf)
{
	struct sn9c102_device* cam;
	ssize_t count;

	cam = video_get_drvdata(container_of(cd, struct video_device, dev));
	if (!cam)
		return -ENODEV;

	count = sizeof(cam->sysfs.frame_header);
	memcpy(buf, cam->sysfs.frame_header, count);

	DBG(3, "Frame header, read bytes: %zd", count);

	return count;
}


static DEVICE_ATTR(reg, S_IRUGO | S_IWUSR, sn9c102_show_reg, sn9c102_store_reg);
static DEVICE_ATTR(val, S_IRUGO | S_IWUSR, sn9c102_show_val, sn9c102_store_val);
static DEVICE_ATTR(i2c_reg, S_IRUGO | S_IWUSR,
		   sn9c102_show_i2c_reg, sn9c102_store_i2c_reg);
static DEVICE_ATTR(i2c_val, S_IRUGO | S_IWUSR,
		   sn9c102_show_i2c_val, sn9c102_store_i2c_val);
static DEVICE_ATTR(green, S_IWUSR, NULL, sn9c102_store_green);
static DEVICE_ATTR(blue, S_IWUSR, NULL, sn9c102_store_blue);
static DEVICE_ATTR(red, S_IWUSR, NULL, sn9c102_store_red);
static DEVICE_ATTR(frame_header, S_IRUGO, sn9c102_show_frame_header, NULL);


static int sn9c102_create_sysfs(struct sn9c102_device* cam)
{
	struct device *dev = &(cam->v4ldev->dev);
	int err = 0;

	if ((err = device_create_file(dev, &dev_attr_reg)))
		goto err_out;
	if ((err = device_create_file(dev, &dev_attr_val)))
		goto err_reg;
	if ((err = device_create_file(dev, &dev_attr_frame_header)))
		goto err_val;

	if (cam->sensor.sysfs_ops) {
		if ((err = device_create_file(dev, &dev_attr_i2c_reg)))
			goto err_frame_header;
		if ((err = device_create_file(dev, &dev_attr_i2c_val)))
			goto err_i2c_reg;
	}

	if (cam->bridge == BRIDGE_SN9C101 || cam->bridge == BRIDGE_SN9C102) {
		if ((err = device_create_file(dev, &dev_attr_green)))
			goto err_i2c_val;
	} else {
		if ((err = device_create_file(dev, &dev_attr_blue)))
			goto err_i2c_val;
		if ((err = device_create_file(dev, &dev_attr_red)))
			goto err_blue;
	}

	return 0;

err_blue:
	device_remove_file(dev, &dev_attr_blue);
err_i2c_val:
	if (cam->sensor.sysfs_ops)
		device_remove_file(dev, &dev_attr_i2c_val);
err_i2c_reg:
	if (cam->sensor.sysfs_ops)
		device_remove_file(dev, &dev_attr_i2c_reg);
err_frame_header:
	device_remove_file(dev, &dev_attr_frame_header);
err_val:
	device_remove_file(dev, &dev_attr_val);
err_reg:
	device_remove_file(dev, &dev_attr_reg);
err_out:
	return err;
}
#endif /* CONFIG_VIDEO_ADV_DEBUG */

/*****************************************************************************/

static int
sn9c102_set_pix_format(struct sn9c102_device* cam, struct v4l2_pix_format* pix)
{
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SN9C10X ||
	    pix->pixelformat == V4L2_PIX_FMT_JPEG) {
		switch (cam->bridge) {
		case BRIDGE_SN9C101:
		case BRIDGE_SN9C102:
		case BRIDGE_SN9C103:
			err += sn9c102_write_reg(cam, cam->reg[0x18] | 0x80,
						 0x18);
			break;
		case BRIDGE_SN9C105:
		case BRIDGE_SN9C120:
			err += sn9c102_write_reg(cam, cam->reg[0x18] & 0x7f,
						 0x18);
			break;
		}
	} else {
		switch (cam->bridge) {
		case BRIDGE_SN9C101:
		case BRIDGE_SN9C102:
		case BRIDGE_SN9C103:
			err += sn9c102_write_reg(cam, cam->reg[0x18] & 0x7f,
						 0x18);
			break;
		case BRIDGE_SN9C105:
		case BRIDGE_SN9C120:
			err += sn9c102_write_reg(cam, cam->reg[0x18] | 0x80,
						 0x18);
			break;
		}
	}

	return err ? -EIO : 0;
}


static int
sn9c102_set_compression(struct sn9c102_device* cam,
			struct v4l2_jpegcompression* compression)
{
	int i, err = 0;

	switch (cam->bridge) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
	case BRIDGE_SN9C103:
		if (compression->quality == 0)
			err += sn9c102_write_reg(cam, cam->reg[0x17] | 0x01,
						 0x17);
		else if (compression->quality == 1)
			err += sn9c102_write_reg(cam, cam->reg[0x17] & 0xfe,
						 0x17);
		break;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		if (compression->quality == 0) {
			for (i = 0; i <= 63; i++) {
				err += sn9c102_write_reg(cam,
							 SN9C102_Y_QTABLE1[i],
							 0x100 + i);
				err += sn9c102_write_reg(cam,
							 SN9C102_UV_QTABLE1[i],
							 0x140 + i);
			}
			err += sn9c102_write_reg(cam, cam->reg[0x18] & 0xbf,
						 0x18);
		} else if (compression->quality == 1) {
			for (i = 0; i <= 63; i++) {
				err += sn9c102_write_reg(cam,
							 SN9C102_Y_QTABLE1[i],
							 0x100 + i);
				err += sn9c102_write_reg(cam,
							 SN9C102_UV_QTABLE1[i],
							 0x140 + i);
			}
			err += sn9c102_write_reg(cam, cam->reg[0x18] | 0x40,
						 0x18);
		}
		break;
	}

	return err ? -EIO : 0;
}


static int sn9c102_set_scale(struct sn9c102_device* cam, u8 scale)
{
	u8 r = 0;
	int err = 0;

	if (scale == 1)
		r = cam->reg[0x18] & 0xcf;
	else if (scale == 2) {
		r = cam->reg[0x18] & 0xcf;
		r |= 0x10;
	} else if (scale == 4)
		r = cam->reg[0x18] | 0x20;

	err += sn9c102_write_reg(cam, r, 0x18);
	if (err)
		return -EIO;

	PDBGG("Scaling factor: %u", scale);

	return 0;
}


static int sn9c102_set_crop(struct sn9c102_device* cam, struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = &cam->sensor;
	u8 h_start = (u8)(rect->left - s->cropcap.bounds.left),
	   v_start = (u8)(rect->top - s->cropcap.bounds.top),
	   h_size = (u8)(rect->width / 16),
	   v_size = (u8)(rect->height / 16);
	int err = 0;

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);
	err += sn9c102_write_reg(cam, h_size, 0x15);
	err += sn9c102_write_reg(cam, v_size, 0x16);
	if (err)
		return -EIO;

	PDBGG("h_start, v_start, h_size, v_size, ho_size, vo_size "
	      "%u %u %u %u", h_start, v_start, h_size, v_size);

	return 0;
}


static int sn9c102_init(struct sn9c102_device* cam)
{
	struct sn9c102_sensor* s = &cam->sensor;
	struct v4l2_control ctrl;
	struct v4l2_queryctrl *qctrl;
	struct v4l2_rect* rect;
	u8 i = 0;
	int err = 0;

	if (!(cam->state & DEV_INITIALIZED)) {
		mutex_init(&cam->open_mutex);
		init_waitqueue_head(&cam->wait_open);
		qctrl = s->qctrl;
		rect = &(s->cropcap.defrect);
	} else { /* use current values */
		qctrl = s->_qctrl;
		rect = &(s->_rect);
	}

	err += sn9c102_set_scale(cam, rect->width / s->pix_format.width);
	err += sn9c102_set_crop(cam, rect);
	if (err)
		return err;

	if (s->init) {
		err = s->init(cam);
		if (err) {
			DBG(3, "Sensor initialization failed");
			return err;
		}
	}

	if (!(cam->state & DEV_INITIALIZED))
		if (cam->bridge == BRIDGE_SN9C101 ||
		    cam->bridge == BRIDGE_SN9C102 ||
		    cam->bridge == BRIDGE_SN9C103) {
			if (s->pix_format.pixelformat == V4L2_PIX_FMT_JPEG)
				s->pix_format.pixelformat= V4L2_PIX_FMT_SBGGR8;
			cam->compression.quality =  cam->reg[0x17] & 0x01 ?
						    0 : 1;
		} else {
			if (s->pix_format.pixelformat == V4L2_PIX_FMT_SN9C10X)
				s->pix_format.pixelformat = V4L2_PIX_FMT_JPEG;
			cam->compression.quality =  cam->reg[0x18] & 0x40 ?
						    0 : 1;
			err += sn9c102_set_compression(cam, &cam->compression);
		}
	else
		err += sn9c102_set_compression(cam, &cam->compression);
	err += sn9c102_set_pix_format(cam, &s->pix_format);
	if (s->set_pix_format)
		err += s->set_pix_format(cam, &s->pix_format);
	if (err)
		return err;

	if (s->pix_format.pixelformat == V4L2_PIX_FMT_SN9C10X ||
	    s->pix_format.pixelformat == V4L2_PIX_FMT_JPEG)
		DBG(3, "Compressed video format is active, quality %d",
		    cam->compression.quality);
	else
		DBG(3, "Uncompressed video format is active");

	if (s->set_crop)
		if ((err = s->set_crop(cam, rect))) {
			DBG(3, "set_crop() failed");
			return err;
		}

	if (s->set_ctrl) {
		for (i = 0; i < ARRAY_SIZE(s->qctrl); i++)
			if (s->qctrl[i].id != 0 &&
			    !(s->qctrl[i].flags & V4L2_CTRL_FLAG_DISABLED)) {
				ctrl.id = s->qctrl[i].id;
				ctrl.value = qctrl[i].default_value;
				err = s->set_ctrl(cam, &ctrl);
				if (err) {
					DBG(3, "Set %s control failed",
					    s->qctrl[i].name);
					return err;
				}
				DBG(3, "Image sensor supports '%s' control",
				    s->qctrl[i].name);
			}
	}

	if (!(cam->state & DEV_INITIALIZED)) {
		mutex_init(&cam->fileop_mutex);
		spin_lock_init(&cam->queue_lock);
		init_waitqueue_head(&cam->wait_frame);
		init_waitqueue_head(&cam->wait_stream);
		cam->nreadbuffers = 2;
		memcpy(s->_qctrl, s->qctrl, sizeof(s->qctrl));
		memcpy(&(s->_rect), &(s->cropcap.defrect),
		       sizeof(struct v4l2_rect));
		cam->state |= DEV_INITIALIZED;
	}

	DBG(2, "Initialization succeeded");
	return 0;
}

/*****************************************************************************/

static void sn9c102_release_resources(struct kref *kref)
{
	struct sn9c102_device *cam;

	mutex_lock(&sn9c102_sysfs_lock);

	cam = container_of(kref, struct sn9c102_device, kref);

	DBG(2, "V4L2 device %s deregistered",
	    video_device_node_name(cam->v4ldev));
	video_set_drvdata(cam->v4ldev, NULL);
	video_unregister_device(cam->v4ldev);
	usb_put_dev(cam->usbdev);
	kfree(cam->control_buffer);
	kfree(cam);

	mutex_unlock(&sn9c102_sysfs_lock);

}


static int sn9c102_open(struct file *filp)
{
	struct sn9c102_device* cam;
	int err = 0;

	/*
	   A read_trylock() in open() is the only safe way to prevent race
	   conditions with disconnect(), one close() and multiple (not
	   necessarily simultaneous) attempts to open(). For example, it
	   prevents from waiting for a second access, while the device
	   structure is being deallocated, after a possible disconnect() and
	   during a following close() holding the write lock: given that, after
	   this deallocation, no access will be possible anymore, using the
	   non-trylock version would have let open() gain the access to the
	   device structure improperly.
	   For this reason the lock must also not be per-device.
	*/
	if (!down_read_trylock(&sn9c102_dev_lock))
		return -ERESTARTSYS;

	cam = video_drvdata(filp);

	if (wait_for_completion_interruptible(&cam->probe)) {
		up_read(&sn9c102_dev_lock);
		return -ERESTARTSYS;
	}

	kref_get(&cam->kref);

	/*
	    Make sure to isolate all the simultaneous opens.
	*/
	if (mutex_lock_interruptible(&cam->open_mutex)) {
		kref_put(&cam->kref, sn9c102_release_resources);
		up_read(&sn9c102_dev_lock);
		return -ERESTARTSYS;
	}

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		err = -ENODEV;
		goto out;
	}

	if (cam->users) {
		DBG(2, "Device %s is already in use",
		    video_device_node_name(cam->v4ldev));
		DBG(3, "Simultaneous opens are not supported");
		/*
		   open() must follow the open flags and should block
		   eventually while the device is in use.
		*/
		if ((filp->f_flags & O_NONBLOCK) ||
		    (filp->f_flags & O_NDELAY)) {
			err = -EWOULDBLOCK;
			goto out;
		}
		DBG(2, "A blocking open() has been requested. Wait for the "
		       "device to be released...");
		up_read(&sn9c102_dev_lock);
		/*
		   We will not release the "open_mutex" lock, so that only one
		   process can be in the wait queue below. This way the process
		   will be sleeping while holding the lock, without loosing its
		   priority after any wake_up().
		*/
		err = wait_event_interruptible_exclusive(cam->wait_open,
						(cam->state & DEV_DISCONNECTED)
							 || !cam->users);
		down_read(&sn9c102_dev_lock);
		if (err)
			goto out;
		if (cam->state & DEV_DISCONNECTED) {
			err = -ENODEV;
			goto out;
		}
	}

	if (cam->state & DEV_MISCONFIGURED) {
		err = sn9c102_init(cam);
		if (err) {
			DBG(1, "Initialization failed again. "
			       "I will retry on next open().");
			goto out;
		}
		cam->state &= ~DEV_MISCONFIGURED;
	}

	if ((err = sn9c102_start_transfer(cam)))
		goto out;

	filp->private_data = cam;
	cam->users++;
	cam->io = IO_NONE;
	cam->stream = STREAM_OFF;
	cam->nbuffers = 0;
	cam->frame_count = 0;
	sn9c102_empty_framequeues(cam);

	DBG(3, "Video device %s is open", video_device_node_name(cam->v4ldev));

out:
	mutex_unlock(&cam->open_mutex);
	if (err)
		kref_put(&cam->kref, sn9c102_release_resources);

	up_read(&sn9c102_dev_lock);
	return err;
}


static int sn9c102_release(struct file *filp)
{
	struct sn9c102_device* cam;

	down_write(&sn9c102_dev_lock);

	cam = video_drvdata(filp);

	sn9c102_stop_transfer(cam);
	sn9c102_release_buffers(cam);
	cam->users--;
	wake_up_interruptible_nr(&cam->wait_open, 1);

	DBG(3, "Video device %s closed", video_device_node_name(cam->v4ldev));

	kref_put(&cam->kref, sn9c102_release_resources);

	up_write(&sn9c102_dev_lock);

	return 0;
}


static ssize_t
sn9c102_read(struct file* filp, char __user * buf, size_t count, loff_t* f_pos)
{
	struct sn9c102_device *cam = video_drvdata(filp);
	struct sn9c102_frame_t* f, * i;
	unsigned long lock_flags;
	long timeout;
	int err = 0;

	if (mutex_lock_interruptible(&cam->fileop_mutex))
		return -ERESTARTSYS;

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		mutex_unlock(&cam->fileop_mutex);
		return -ENODEV;
	}

	if (cam->state & DEV_MISCONFIGURED) {
		DBG(1, "The camera is misconfigured. Close and open it "
		       "again.");
		mutex_unlock(&cam->fileop_mutex);
		return -EIO;
	}

	if (cam->io == IO_MMAP) {
		DBG(3, "Close and open the device again to choose "
		       "the read method");
		mutex_unlock(&cam->fileop_mutex);
		return -EBUSY;
	}

	if (cam->io == IO_NONE) {
		if (!sn9c102_request_buffers(cam,cam->nreadbuffers, IO_READ)) {
			DBG(1, "read() failed, not enough memory");
			mutex_unlock(&cam->fileop_mutex);
			return -ENOMEM;
		}
		cam->io = IO_READ;
		cam->stream = STREAM_ON;
	}

	if (list_empty(&cam->inqueue)) {
		if (!list_empty(&cam->outqueue))
			sn9c102_empty_framequeues(cam);
		sn9c102_queue_unusedframes(cam);
	}

	if (!count) {
		mutex_unlock(&cam->fileop_mutex);
		return 0;
	}

	if (list_empty(&cam->outqueue)) {
		if (filp->f_flags & O_NONBLOCK) {
			mutex_unlock(&cam->fileop_mutex);
			return -EAGAIN;
		}
		if (!cam->module_param.frame_timeout) {
			err = wait_event_interruptible
			      ( cam->wait_frame,
				(!list_empty(&cam->outqueue)) ||
				(cam->state & DEV_DISCONNECTED) ||
				(cam->state & DEV_MISCONFIGURED) );
			if (err) {
				mutex_unlock(&cam->fileop_mutex);
				return err;
			}
		} else {
			timeout = wait_event_interruptible_timeout
				  ( cam->wait_frame,
				    (!list_empty(&cam->outqueue)) ||
				    (cam->state & DEV_DISCONNECTED) ||
				    (cam->state & DEV_MISCONFIGURED),
				    msecs_to_jiffies(
					cam->module_param.frame_timeout * 1000
				    )
				  );
			if (timeout < 0) {
				mutex_unlock(&cam->fileop_mutex);
				return timeout;
			} else if (timeout == 0 &&
				   !(cam->state & DEV_DISCONNECTED)) {
				DBG(1, "Video frame timeout elapsed");
				mutex_unlock(&cam->fileop_mutex);
				return -EIO;
			}
		}
		if (cam->state & DEV_DISCONNECTED) {
			mutex_unlock(&cam->fileop_mutex);
			return -ENODEV;
		}
		if (cam->state & DEV_MISCONFIGURED) {
			mutex_unlock(&cam->fileop_mutex);
			return -EIO;
		}
	}

	f = list_entry(cam->outqueue.prev, struct sn9c102_frame_t, frame);

	if (count > f->buf.bytesused)
		count = f->buf.bytesused;

	if (copy_to_user(buf, f->bufmem, count)) {
		err = -EFAULT;
		goto exit;
	}
	*f_pos += count;

exit:
	spin_lock_irqsave(&cam->queue_lock, lock_flags);
	list_for_each_entry(i, &cam->outqueue, frame)
		i->state = F_UNUSED;
	INIT_LIST_HEAD(&cam->outqueue);
	spin_unlock_irqrestore(&cam->queue_lock, lock_flags);

	sn9c102_queue_unusedframes(cam);

	PDBGG("Frame #%lu, bytes read: %zu",
	      (unsigned long)f->buf.index, count);

	mutex_unlock(&cam->fileop_mutex);

	return count;
}


static unsigned int sn9c102_poll(struct file *filp, poll_table *wait)
{
	struct sn9c102_device *cam = video_drvdata(filp);
	struct sn9c102_frame_t* f;
	unsigned long lock_flags;
	unsigned int mask = 0;

	if (mutex_lock_interruptible(&cam->fileop_mutex))
		return POLLERR;

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		goto error;
	}

	if (cam->state & DEV_MISCONFIGURED) {
		DBG(1, "The camera is misconfigured. Close and open it "
		       "again.");
		goto error;
	}

	if (cam->io == IO_NONE) {
		if (!sn9c102_request_buffers(cam, cam->nreadbuffers,
					     IO_READ)) {
			DBG(1, "poll() failed, not enough memory");
			goto error;
		}
		cam->io = IO_READ;
		cam->stream = STREAM_ON;
	}

	if (cam->io == IO_READ) {
		spin_lock_irqsave(&cam->queue_lock, lock_flags);
		list_for_each_entry(f, &cam->outqueue, frame)
			f->state = F_UNUSED;
		INIT_LIST_HEAD(&cam->outqueue);
		spin_unlock_irqrestore(&cam->queue_lock, lock_flags);
		sn9c102_queue_unusedframes(cam);
	}

	poll_wait(filp, &cam->wait_frame, wait);

	if (!list_empty(&cam->outqueue))
		mask |= POLLIN | POLLRDNORM;

	mutex_unlock(&cam->fileop_mutex);

	return mask;

error:
	mutex_unlock(&cam->fileop_mutex);
	return POLLERR;
}


static void sn9c102_vm_open(struct vm_area_struct* vma)
{
	struct sn9c102_frame_t* f = vma->vm_private_data;
	f->vma_use_count++;
}


static void sn9c102_vm_close(struct vm_area_struct* vma)
{
	/* NOTE: buffers are not freed here */
	struct sn9c102_frame_t* f = vma->vm_private_data;
	f->vma_use_count--;
}


static const struct vm_operations_struct sn9c102_vm_ops = {
	.open = sn9c102_vm_open,
	.close = sn9c102_vm_close,
};


static int sn9c102_mmap(struct file* filp, struct vm_area_struct *vma)
{
	struct sn9c102_device *cam = video_drvdata(filp);
	unsigned long size = vma->vm_end - vma->vm_start,
		      start = vma->vm_start;
	void *pos;
	u32 i;

	if (mutex_lock_interruptible(&cam->fileop_mutex))
		return -ERESTARTSYS;

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		mutex_unlock(&cam->fileop_mutex);
		return -ENODEV;
	}

	if (cam->state & DEV_MISCONFIGURED) {
		DBG(1, "The camera is misconfigured. Close and open it "
		       "again.");
		mutex_unlock(&cam->fileop_mutex);
		return -EIO;
	}

	if (!(vma->vm_flags & (VM_WRITE | VM_READ))) {
		mutex_unlock(&cam->fileop_mutex);
		return -EACCES;
	}

	if (cam->io != IO_MMAP ||
	    size != PAGE_ALIGN(cam->frame[0].buf.length)) {
		mutex_unlock(&cam->fileop_mutex);
		return -EINVAL;
	}

	for (i = 0; i < cam->nbuffers; i++) {
		if ((cam->frame[i].buf.m.offset>>PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}
	if (i == cam->nbuffers) {
		mutex_unlock(&cam->fileop_mutex);
		return -EINVAL;
	}

	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;

	pos = cam->frame[i].bufmem;
	while (size > 0) { /* size is page-aligned */
		if (vm_insert_page(vma, start, vmalloc_to_page(pos))) {
			mutex_unlock(&cam->fileop_mutex);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vma->vm_ops = &sn9c102_vm_ops;
	vma->vm_private_data = &cam->frame[i];
	sn9c102_vm_open(vma);

	mutex_unlock(&cam->fileop_mutex);

	return 0;
}

/*****************************************************************************/

static int
sn9c102_vidioc_querycap(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_capability cap = {
		.driver = "sn9c102",
		.version = SN9C102_MODULE_VERSION_CODE,
		.capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
				V4L2_CAP_STREAMING,
	};

	strlcpy(cap.card, cam->v4ldev->name, sizeof(cap.card));
	if (usb_make_path(cam->usbdev, cap.bus_info, sizeof(cap.bus_info)) < 0)
		strlcpy(cap.bus_info, dev_name(&cam->usbdev->dev),
			sizeof(cap.bus_info));

	if (copy_to_user(arg, &cap, sizeof(cap)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_enuminput(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_input i;

	if (copy_from_user(&i, arg, sizeof(i)))
		return -EFAULT;

	if (i.index)
		return -EINVAL;

	memset(&i, 0, sizeof(i));
	strcpy(i.name, "Camera");
	i.type = V4L2_INPUT_TYPE_CAMERA;
	i.capabilities = V4L2_IN_CAP_STD;

	if (copy_to_user(arg, &i, sizeof(i)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_g_input(struct sn9c102_device* cam, void __user * arg)
{
	int index = 0;

	if (copy_to_user(arg, &index, sizeof(index)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_s_input(struct sn9c102_device* cam, void __user * arg)
{
	int index;

	if (copy_from_user(&index, arg, sizeof(index)))
		return -EFAULT;

	if (index != 0)
		return -EINVAL;

	return 0;
}


static int
sn9c102_vidioc_query_ctrl(struct sn9c102_device* cam, void __user * arg)
{
	struct sn9c102_sensor* s = &cam->sensor;
	struct v4l2_queryctrl qc;
	u8 i;

	if (copy_from_user(&qc, arg, sizeof(qc)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(s->qctrl); i++)
		if (qc.id && qc.id == s->qctrl[i].id) {
			memcpy(&qc, &(s->qctrl[i]), sizeof(qc));
			if (copy_to_user(arg, &qc, sizeof(qc)))
				return -EFAULT;
			return 0;
		}

	return -EINVAL;
}


static int
sn9c102_vidioc_g_ctrl(struct sn9c102_device* cam, void __user * arg)
{
	struct sn9c102_sensor* s = &cam->sensor;
	struct v4l2_control ctrl;
	int err = 0;
	u8 i;

	if (!s->get_ctrl && !s->set_ctrl)
		return -EINVAL;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;

	if (!s->get_ctrl) {
		for (i = 0; i < ARRAY_SIZE(s->qctrl); i++)
			if (ctrl.id && ctrl.id == s->qctrl[i].id) {
				ctrl.value = s->_qctrl[i].default_value;
				goto exit;
			}
		return -EINVAL;
	} else
		err = s->get_ctrl(cam, &ctrl);

exit:
	if (copy_to_user(arg, &ctrl, sizeof(ctrl)))
		return -EFAULT;

	PDBGG("VIDIOC_G_CTRL: id %lu, value %lu",
	      (unsigned long)ctrl.id, (unsigned long)ctrl.value);

	return err;
}


static int
sn9c102_vidioc_s_ctrl(struct sn9c102_device* cam, void __user * arg)
{
	struct sn9c102_sensor* s = &cam->sensor;
	struct v4l2_control ctrl;
	u8 i;
	int err = 0;

	if (!s->set_ctrl)
		return -EINVAL;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(s->qctrl); i++) {
		if (ctrl.id == s->qctrl[i].id) {
			if (s->qctrl[i].flags & V4L2_CTRL_FLAG_DISABLED)
				return -EINVAL;
			if (ctrl.value < s->qctrl[i].minimum ||
			    ctrl.value > s->qctrl[i].maximum)
				return -ERANGE;
			ctrl.value -= ctrl.value % s->qctrl[i].step;
			break;
		}
	}
	if (i == ARRAY_SIZE(s->qctrl))
		return -EINVAL;
	if ((err = s->set_ctrl(cam, &ctrl)))
		return err;

	s->_qctrl[i].default_value = ctrl.value;

	PDBGG("VIDIOC_S_CTRL: id %lu, value %lu",
	      (unsigned long)ctrl.id, (unsigned long)ctrl.value);

	return 0;
}


static int
sn9c102_vidioc_cropcap(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_cropcap* cc = &(cam->sensor.cropcap);

	cc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cc->pixelaspect.numerator = 1;
	cc->pixelaspect.denominator = 1;

	if (copy_to_user(arg, cc, sizeof(*cc)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_g_crop(struct sn9c102_device* cam, void __user * arg)
{
	struct sn9c102_sensor* s = &cam->sensor;
	struct v4l2_crop crop = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};

	memcpy(&(crop.c), &(s->_rect), sizeof(struct v4l2_rect));

	if (copy_to_user(arg, &crop, sizeof(crop)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_s_crop(struct sn9c102_device* cam, void __user * arg)
{
	struct sn9c102_sensor* s = &cam->sensor;
	struct v4l2_crop crop;
	struct v4l2_rect* rect;
	struct v4l2_rect* bounds = &(s->cropcap.bounds);
	struct v4l2_pix_format* pix_format = &(s->pix_format);
	u8 scale;
	const enum sn9c102_stream_state stream = cam->stream;
	const u32 nbuffers = cam->nbuffers;
	u32 i;
	int err = 0;

	if (copy_from_user(&crop, arg, sizeof(crop)))
		return -EFAULT;

	rect = &(crop.c);

	if (crop.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (cam->module_param.force_munmap)
		for (i = 0; i < cam->nbuffers; i++)
			if (cam->frame[i].vma_use_count) {
				DBG(3, "VIDIOC_S_CROP failed. "
				       "Unmap the buffers first.");
				return -EBUSY;
			}

	/* Preserve R,G or B origin */
	rect->left = (s->_rect.left & 1L) ? rect->left | 1L : rect->left & ~1L;
	rect->top = (s->_rect.top & 1L) ? rect->top | 1L : rect->top & ~1L;

	if (rect->width < 16)
		rect->width = 16;
	if (rect->height < 16)
		rect->height = 16;
	if (rect->width > bounds->width)
		rect->width = bounds->width;
	if (rect->height > bounds->height)
		rect->height = bounds->height;
	if (rect->left < bounds->left)
		rect->left = bounds->left;
	if (rect->top < bounds->top)
		rect->top = bounds->top;
	if (rect->left + rect->width > bounds->left + bounds->width)
		rect->left = bounds->left+bounds->width - rect->width;
	if (rect->top + rect->height > bounds->top + bounds->height)
		rect->top = bounds->top+bounds->height - rect->height;

	rect->width &= ~15L;
	rect->height &= ~15L;

	if (SN9C102_PRESERVE_IMGSCALE) {
		/* Calculate the actual scaling factor */
		u32 a, b;
		a = rect->width * rect->height;
		b = pix_format->width * pix_format->height;
		scale = b ? (u8)((a / b) < 4 ? 1 : ((a / b) < 16 ? 2 : 4)) : 1;
	} else
		scale = 1;

	if (cam->stream == STREAM_ON)
		if ((err = sn9c102_stream_interrupt(cam)))
			return err;

	if (copy_to_user(arg, &crop, sizeof(crop))) {
		cam->stream = stream;
		return -EFAULT;
	}

	if (cam->module_param.force_munmap || cam->io == IO_READ)
		sn9c102_release_buffers(cam);

	err = sn9c102_set_crop(cam, rect);
	if (s->set_crop)
		err += s->set_crop(cam, rect);
	err += sn9c102_set_scale(cam, scale);

	if (err) { /* atomic, no rollback in ioctl() */
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_CROP failed because of hardware problems. To "
		       "use the camera, close and open %s again.",
		    video_device_node_name(cam->v4ldev));
		return -EIO;
	}

	s->pix_format.width = rect->width/scale;
	s->pix_format.height = rect->height/scale;
	memcpy(&(s->_rect), rect, sizeof(*rect));

	if ((cam->module_param.force_munmap || cam->io == IO_READ) &&
	    nbuffers != sn9c102_request_buffers(cam, nbuffers, cam->io)) {
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_CROP failed because of not enough memory. To "
		       "use the camera, close and open %s again.",
		    video_device_node_name(cam->v4ldev));
		return -ENOMEM;
	}

	if (cam->io == IO_READ)
		sn9c102_empty_framequeues(cam);
	else if (cam->module_param.force_munmap)
		sn9c102_requeue_outqueue(cam);

	cam->stream = stream;

	return 0;
}


static int
sn9c102_vidioc_enum_framesizes(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_frmsizeenum frmsize;

	if (copy_from_user(&frmsize, arg, sizeof(frmsize)))
		return -EFAULT;

	if (frmsize.index != 0)
		return -EINVAL;

	switch (cam->bridge) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
	case BRIDGE_SN9C103:
		if (frmsize.pixel_format != V4L2_PIX_FMT_SN9C10X &&
		    frmsize.pixel_format != V4L2_PIX_FMT_SBGGR8)
			return -EINVAL;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		if (frmsize.pixel_format != V4L2_PIX_FMT_JPEG &&
		    frmsize.pixel_format != V4L2_PIX_FMT_SBGGR8)
			return -EINVAL;
	}

	frmsize.type = V4L2_FRMSIZE_TYPE_STEPWISE;
	frmsize.stepwise.min_width = frmsize.stepwise.step_width = 16;
	frmsize.stepwise.min_height = frmsize.stepwise.step_height = 16;
	frmsize.stepwise.max_width = cam->sensor.cropcap.bounds.width;
	frmsize.stepwise.max_height = cam->sensor.cropcap.bounds.height;
	memset(&frmsize.reserved, 0, sizeof(frmsize.reserved));

	if (copy_to_user(arg, &frmsize, sizeof(frmsize)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_enum_fmt(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_fmtdesc fmtd;

	if (copy_from_user(&fmtd, arg, sizeof(fmtd)))
		return -EFAULT;

	if (fmtd.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (fmtd.index == 0) {
		strcpy(fmtd.description, "bayer rgb");
		fmtd.pixelformat = V4L2_PIX_FMT_SBGGR8;
	} else if (fmtd.index == 1) {
		switch (cam->bridge) {
		case BRIDGE_SN9C101:
		case BRIDGE_SN9C102:
		case BRIDGE_SN9C103:
			strcpy(fmtd.description, "compressed");
			fmtd.pixelformat = V4L2_PIX_FMT_SN9C10X;
			break;
		case BRIDGE_SN9C105:
		case BRIDGE_SN9C120:
			strcpy(fmtd.description, "JPEG");
			fmtd.pixelformat = V4L2_PIX_FMT_JPEG;
			break;
		}
		fmtd.flags = V4L2_FMT_FLAG_COMPRESSED;
	} else
		return -EINVAL;

	fmtd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	memset(&fmtd.reserved, 0, sizeof(fmtd.reserved));

	if (copy_to_user(arg, &fmtd, sizeof(fmtd)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_g_fmt(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_format format;
	struct v4l2_pix_format* pfmt = &(cam->sensor.pix_format);

	if (copy_from_user(&format, arg, sizeof(format)))
		return -EFAULT;

	if (format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	pfmt->colorspace = (pfmt->pixelformat == V4L2_PIX_FMT_JPEG) ?
			   V4L2_COLORSPACE_JPEG : V4L2_COLORSPACE_SRGB;
	pfmt->bytesperline = (pfmt->pixelformat == V4L2_PIX_FMT_SN9C10X ||
			      pfmt->pixelformat == V4L2_PIX_FMT_JPEG)
			     ? 0 : (pfmt->width * pfmt->priv) / 8;
	pfmt->sizeimage = pfmt->height * ((pfmt->width*pfmt->priv)/8);
	pfmt->field = V4L2_FIELD_NONE;
	memcpy(&(format.fmt.pix), pfmt, sizeof(*pfmt));

	if (copy_to_user(arg, &format, sizeof(format)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_try_s_fmt(struct sn9c102_device* cam, unsigned int cmd,
			 void __user * arg)
{
	struct sn9c102_sensor* s = &cam->sensor;
	struct v4l2_format format;
	struct v4l2_pix_format* pix;
	struct v4l2_pix_format* pfmt = &(s->pix_format);
	struct v4l2_rect* bounds = &(s->cropcap.bounds);
	struct v4l2_rect rect;
	u8 scale;
	const enum sn9c102_stream_state stream = cam->stream;
	const u32 nbuffers = cam->nbuffers;
	u32 i;
	int err = 0;

	if (copy_from_user(&format, arg, sizeof(format)))
		return -EFAULT;

	pix = &(format.fmt.pix);

	if (format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memcpy(&rect, &(s->_rect), sizeof(rect));

	{ /* calculate the actual scaling factor */
		u32 a, b;
		a = rect.width * rect.height;
		b = pix->width * pix->height;
		scale = b ? (u8)((a / b) < 4 ? 1 : ((a / b) < 16 ? 2 : 4)) : 1;
	}

	rect.width = scale * pix->width;
	rect.height = scale * pix->height;

	if (rect.width < 16)
		rect.width = 16;
	if (rect.height < 16)
		rect.height = 16;
	if (rect.width > bounds->left + bounds->width - rect.left)
		rect.width = bounds->left + bounds->width - rect.left;
	if (rect.height > bounds->top + bounds->height - rect.top)
		rect.height = bounds->top + bounds->height - rect.top;

	rect.width &= ~15L;
	rect.height &= ~15L;

	{ /* adjust the scaling factor */
		u32 a, b;
		a = rect.width * rect.height;
		b = pix->width * pix->height;
		scale = b ? (u8)((a / b) < 4 ? 1 : ((a / b) < 16 ? 2 : 4)) : 1;
	}

	pix->width = rect.width / scale;
	pix->height = rect.height / scale;

	switch (cam->bridge) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
	case BRIDGE_SN9C103:
		if (pix->pixelformat != V4L2_PIX_FMT_SN9C10X &&
		    pix->pixelformat != V4L2_PIX_FMT_SBGGR8)
			pix->pixelformat = pfmt->pixelformat;
		break;
	case BRIDGE_SN9C105:
	case BRIDGE_SN9C120:
		if (pix->pixelformat != V4L2_PIX_FMT_JPEG &&
		    pix->pixelformat != V4L2_PIX_FMT_SBGGR8)
			pix->pixelformat = pfmt->pixelformat;
		break;
	}
	pix->priv = pfmt->priv; /* bpp */
	pix->colorspace = (pix->pixelformat == V4L2_PIX_FMT_JPEG) ?
			  V4L2_COLORSPACE_JPEG : V4L2_COLORSPACE_SRGB;
	pix->bytesperline = (pix->pixelformat == V4L2_PIX_FMT_SN9C10X ||
			     pix->pixelformat == V4L2_PIX_FMT_JPEG)
			    ? 0 : (pix->width * pix->priv) / 8;
	pix->sizeimage = pix->height * ((pix->width * pix->priv) / 8);
	pix->field = V4L2_FIELD_NONE;

	if (cmd == VIDIOC_TRY_FMT) {
		if (copy_to_user(arg, &format, sizeof(format)))
			return -EFAULT;
		return 0;
	}

	if (cam->module_param.force_munmap)
		for (i = 0; i < cam->nbuffers; i++)
			if (cam->frame[i].vma_use_count) {
				DBG(3, "VIDIOC_S_FMT failed. Unmap the "
				       "buffers first.");
				return -EBUSY;
			}

	if (cam->stream == STREAM_ON)
		if ((err = sn9c102_stream_interrupt(cam)))
			return err;

	if (copy_to_user(arg, &format, sizeof(format))) {
		cam->stream = stream;
		return -EFAULT;
	}

	if (cam->module_param.force_munmap  || cam->io == IO_READ)
		sn9c102_release_buffers(cam);

	err += sn9c102_set_pix_format(cam, pix);
	err += sn9c102_set_crop(cam, &rect);
	if (s->set_pix_format)
		err += s->set_pix_format(cam, pix);
	if (s->set_crop)
		err += s->set_crop(cam, &rect);
	err += sn9c102_set_scale(cam, scale);

	if (err) { /* atomic, no rollback in ioctl() */
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_FMT failed because of hardware problems. To "
		       "use the camera, close and open %s again.",
		    video_device_node_name(cam->v4ldev));
		return -EIO;
	}

	memcpy(pfmt, pix, sizeof(*pix));
	memcpy(&(s->_rect), &rect, sizeof(rect));

	if ((cam->module_param.force_munmap  || cam->io == IO_READ) &&
	    nbuffers != sn9c102_request_buffers(cam, nbuffers, cam->io)) {
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_FMT failed because of not enough memory. To "
		       "use the camera, close and open %s again.",
		    video_device_node_name(cam->v4ldev));
		return -ENOMEM;
	}

	if (cam->io == IO_READ)
		sn9c102_empty_framequeues(cam);
	else if (cam->module_param.force_munmap)
		sn9c102_requeue_outqueue(cam);

	cam->stream = stream;

	return 0;
}


static int
sn9c102_vidioc_g_jpegcomp(struct sn9c102_device* cam, void __user * arg)
{
	if (copy_to_user(arg, &cam->compression, sizeof(cam->compression)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_s_jpegcomp(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_jpegcompression jc;
	const enum sn9c102_stream_state stream = cam->stream;
	int err = 0;

	if (copy_from_user(&jc, arg, sizeof(jc)))
		return -EFAULT;

	if (jc.quality != 0 && jc.quality != 1)
		return -EINVAL;

	if (cam->stream == STREAM_ON)
		if ((err = sn9c102_stream_interrupt(cam)))
			return err;

	err += sn9c102_set_compression(cam, &jc);
	if (err) { /* atomic, no rollback in ioctl() */
		cam->state |= DEV_MISCONFIGURED;
		DBG(1, "VIDIOC_S_JPEGCOMP failed because of hardware problems. "
		       "To use the camera, close and open %s again.",
		    video_device_node_name(cam->v4ldev));
		return -EIO;
	}

	cam->compression.quality = jc.quality;

	cam->stream = stream;

	return 0;
}


static int
sn9c102_vidioc_reqbufs(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_requestbuffers rb;
	u32 i;
	int err;

	if (copy_from_user(&rb, arg, sizeof(rb)))
		return -EFAULT;

	if (rb.type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    rb.memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	if (cam->io == IO_READ) {
		DBG(3, "Close and open the device again to choose the mmap "
		       "I/O method");
		return -EBUSY;
	}

	for (i = 0; i < cam->nbuffers; i++)
		if (cam->frame[i].vma_use_count) {
			DBG(3, "VIDIOC_REQBUFS failed. Previous buffers are "
			       "still mapped.");
			return -EBUSY;
		}

	if (cam->stream == STREAM_ON)
		if ((err = sn9c102_stream_interrupt(cam)))
			return err;

	sn9c102_empty_framequeues(cam);

	sn9c102_release_buffers(cam);
	if (rb.count)
		rb.count = sn9c102_request_buffers(cam, rb.count, IO_MMAP);

	if (copy_to_user(arg, &rb, sizeof(rb))) {
		sn9c102_release_buffers(cam);
		cam->io = IO_NONE;
		return -EFAULT;
	}

	cam->io = rb.count ? IO_MMAP : IO_NONE;

	return 0;
}


static int
sn9c102_vidioc_querybuf(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_buffer b;

	if (copy_from_user(&b, arg, sizeof(b)))
		return -EFAULT;

	if (b.type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    b.index >= cam->nbuffers || cam->io != IO_MMAP)
		return -EINVAL;

	memcpy(&b, &cam->frame[b.index].buf, sizeof(b));

	if (cam->frame[b.index].vma_use_count)
		b.flags |= V4L2_BUF_FLAG_MAPPED;

	if (cam->frame[b.index].state == F_DONE)
		b.flags |= V4L2_BUF_FLAG_DONE;
	else if (cam->frame[b.index].state != F_UNUSED)
		b.flags |= V4L2_BUF_FLAG_QUEUED;

	if (copy_to_user(arg, &b, sizeof(b)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_qbuf(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_buffer b;
	unsigned long lock_flags;

	if (copy_from_user(&b, arg, sizeof(b)))
		return -EFAULT;

	if (b.type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    b.index >= cam->nbuffers || cam->io != IO_MMAP)
		return -EINVAL;

	if (cam->frame[b.index].state != F_UNUSED)
		return -EINVAL;

	cam->frame[b.index].state = F_QUEUED;

	spin_lock_irqsave(&cam->queue_lock, lock_flags);
	list_add_tail(&cam->frame[b.index].frame, &cam->inqueue);
	spin_unlock_irqrestore(&cam->queue_lock, lock_flags);

	PDBGG("Frame #%lu queued", (unsigned long)b.index);

	return 0;
}


static int
sn9c102_vidioc_dqbuf(struct sn9c102_device* cam, struct file* filp,
		     void __user * arg)
{
	struct v4l2_buffer b;
	struct sn9c102_frame_t *f;
	unsigned long lock_flags;
	long timeout;
	int err = 0;

	if (copy_from_user(&b, arg, sizeof(b)))
		return -EFAULT;

	if (b.type != V4L2_BUF_TYPE_VIDEO_CAPTURE || cam->io != IO_MMAP)
		return -EINVAL;

	if (list_empty(&cam->outqueue)) {
		if (cam->stream == STREAM_OFF)
			return -EINVAL;
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (!cam->module_param.frame_timeout) {
			err = wait_event_interruptible
			      ( cam->wait_frame,
				(!list_empty(&cam->outqueue)) ||
				(cam->state & DEV_DISCONNECTED) ||
				(cam->state & DEV_MISCONFIGURED) );
			if (err)
				return err;
		} else {
			timeout = wait_event_interruptible_timeout
				  ( cam->wait_frame,
				    (!list_empty(&cam->outqueue)) ||
				    (cam->state & DEV_DISCONNECTED) ||
				    (cam->state & DEV_MISCONFIGURED),
				    cam->module_param.frame_timeout *
				    1000 * msecs_to_jiffies(1) );
			if (timeout < 0)
				return timeout;
			else if (timeout == 0 &&
				 !(cam->state & DEV_DISCONNECTED)) {
				DBG(1, "Video frame timeout elapsed");
				return -EIO;
			}
		}
		if (cam->state & DEV_DISCONNECTED)
			return -ENODEV;
		if (cam->state & DEV_MISCONFIGURED)
			return -EIO;
	}

	spin_lock_irqsave(&cam->queue_lock, lock_flags);
	f = list_entry(cam->outqueue.next, struct sn9c102_frame_t, frame);
	list_del(cam->outqueue.next);
	spin_unlock_irqrestore(&cam->queue_lock, lock_flags);

	f->state = F_UNUSED;

	memcpy(&b, &f->buf, sizeof(b));
	if (f->vma_use_count)
		b.flags |= V4L2_BUF_FLAG_MAPPED;

	if (copy_to_user(arg, &b, sizeof(b)))
		return -EFAULT;

	PDBGG("Frame #%lu dequeued", (unsigned long)f->buf.index);

	return 0;
}


static int
sn9c102_vidioc_streamon(struct sn9c102_device* cam, void __user * arg)
{
	int type;

	if (copy_from_user(&type, arg, sizeof(type)))
		return -EFAULT;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE || cam->io != IO_MMAP)
		return -EINVAL;

	cam->stream = STREAM_ON;

	DBG(3, "Stream on");

	return 0;
}


static int
sn9c102_vidioc_streamoff(struct sn9c102_device* cam, void __user * arg)
{
	int type, err;

	if (copy_from_user(&type, arg, sizeof(type)))
		return -EFAULT;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE || cam->io != IO_MMAP)
		return -EINVAL;

	if (cam->stream == STREAM_ON)
		if ((err = sn9c102_stream_interrupt(cam)))
			return err;

	sn9c102_empty_framequeues(cam);

	DBG(3, "Stream off");

	return 0;
}


static int
sn9c102_vidioc_g_parm(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_streamparm sp;

	if (copy_from_user(&sp, arg, sizeof(sp)))
		return -EFAULT;

	if (sp.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sp.parm.capture.extendedmode = 0;
	sp.parm.capture.readbuffers = cam->nreadbuffers;

	if (copy_to_user(arg, &sp, sizeof(sp)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_s_parm(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_streamparm sp;

	if (copy_from_user(&sp, arg, sizeof(sp)))
		return -EFAULT;

	if (sp.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sp.parm.capture.extendedmode = 0;

	if (sp.parm.capture.readbuffers == 0)
		sp.parm.capture.readbuffers = cam->nreadbuffers;

	if (sp.parm.capture.readbuffers > SN9C102_MAX_FRAMES)
		sp.parm.capture.readbuffers = SN9C102_MAX_FRAMES;

	if (copy_to_user(arg, &sp, sizeof(sp)))
		return -EFAULT;

	cam->nreadbuffers = sp.parm.capture.readbuffers;

	return 0;
}


static int
sn9c102_vidioc_enumaudio(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_audio audio;

	if (cam->bridge == BRIDGE_SN9C101 || cam->bridge == BRIDGE_SN9C102)
		return -EINVAL;

	if (copy_from_user(&audio, arg, sizeof(audio)))
		return -EFAULT;

	if (audio.index != 0)
		return -EINVAL;

	strcpy(audio.name, "Microphone");
	audio.capability = 0;
	audio.mode = 0;

	if (copy_to_user(arg, &audio, sizeof(audio)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_g_audio(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_audio audio;

	if (cam->bridge == BRIDGE_SN9C101 || cam->bridge == BRIDGE_SN9C102)
		return -EINVAL;

	if (copy_from_user(&audio, arg, sizeof(audio)))
		return -EFAULT;

	memset(&audio, 0, sizeof(audio));
	strcpy(audio.name, "Microphone");

	if (copy_to_user(arg, &audio, sizeof(audio)))
		return -EFAULT;

	return 0;
}


static int
sn9c102_vidioc_s_audio(struct sn9c102_device* cam, void __user * arg)
{
	struct v4l2_audio audio;

	if (cam->bridge == BRIDGE_SN9C101 || cam->bridge == BRIDGE_SN9C102)
		return -EINVAL;

	if (copy_from_user(&audio, arg, sizeof(audio)))
		return -EFAULT;

	if (audio.index != 0)
		return -EINVAL;

	return 0;
}


static long sn9c102_ioctl_v4l2(struct file *filp,
			      unsigned int cmd, void __user *arg)
{
	struct sn9c102_device *cam = video_drvdata(filp);

	switch (cmd) {

	case VIDIOC_QUERYCAP:
		return sn9c102_vidioc_querycap(cam, arg);

	case VIDIOC_ENUMINPUT:
		return sn9c102_vidioc_enuminput(cam, arg);

	case VIDIOC_G_INPUT:
		return sn9c102_vidioc_g_input(cam, arg);

	case VIDIOC_S_INPUT:
		return sn9c102_vidioc_s_input(cam, arg);

	case VIDIOC_QUERYCTRL:
		return sn9c102_vidioc_query_ctrl(cam, arg);

	case VIDIOC_G_CTRL:
		return sn9c102_vidioc_g_ctrl(cam, arg);

	case VIDIOC_S_CTRL:
		return sn9c102_vidioc_s_ctrl(cam, arg);

	case VIDIOC_CROPCAP:
		return sn9c102_vidioc_cropcap(cam, arg);

	case VIDIOC_G_CROP:
		return sn9c102_vidioc_g_crop(cam, arg);

	case VIDIOC_S_CROP:
		return sn9c102_vidioc_s_crop(cam, arg);

	case VIDIOC_ENUM_FRAMESIZES:
		return sn9c102_vidioc_enum_framesizes(cam, arg);

	case VIDIOC_ENUM_FMT:
		return sn9c102_vidioc_enum_fmt(cam, arg);

	case VIDIOC_G_FMT:
		return sn9c102_vidioc_g_fmt(cam, arg);

	case VIDIOC_TRY_FMT:
	case VIDIOC_S_FMT:
		return sn9c102_vidioc_try_s_fmt(cam, cmd, arg);

	case VIDIOC_G_JPEGCOMP:
		return sn9c102_vidioc_g_jpegcomp(cam, arg);

	case VIDIOC_S_JPEGCOMP:
		return sn9c102_vidioc_s_jpegcomp(cam, arg);

	case VIDIOC_REQBUFS:
		return sn9c102_vidioc_reqbufs(cam, arg);

	case VIDIOC_QUERYBUF:
		return sn9c102_vidioc_querybuf(cam, arg);

	case VIDIOC_QBUF:
		return sn9c102_vidioc_qbuf(cam, arg);

	case VIDIOC_DQBUF:
		return sn9c102_vidioc_dqbuf(cam, filp, arg);

	case VIDIOC_STREAMON:
		return sn9c102_vidioc_streamon(cam, arg);

	case VIDIOC_STREAMOFF:
		return sn9c102_vidioc_streamoff(cam, arg);

	case VIDIOC_G_PARM:
		return sn9c102_vidioc_g_parm(cam, arg);

	case VIDIOC_S_PARM:
		return sn9c102_vidioc_s_parm(cam, arg);

	case VIDIOC_ENUMAUDIO:
		return sn9c102_vidioc_enumaudio(cam, arg);

	case VIDIOC_G_AUDIO:
		return sn9c102_vidioc_g_audio(cam, arg);

	case VIDIOC_S_AUDIO:
		return sn9c102_vidioc_s_audio(cam, arg);

	case VIDIOC_G_STD:
	case VIDIOC_S_STD:
	case VIDIOC_QUERYSTD:
	case VIDIOC_ENUMSTD:
	case VIDIOC_QUERYMENU:
	case VIDIOC_ENUM_FRAMEINTERVALS:
		return -EINVAL;

	default:
		return -EINVAL;

	}
}


static long sn9c102_ioctl(struct file *filp,
			 unsigned int cmd, unsigned long arg)
{
	struct sn9c102_device *cam = video_drvdata(filp);
	int err = 0;

	if (mutex_lock_interruptible(&cam->fileop_mutex))
		return -ERESTARTSYS;

	if (cam->state & DEV_DISCONNECTED) {
		DBG(1, "Device not present");
		mutex_unlock(&cam->fileop_mutex);
		return -ENODEV;
	}

	if (cam->state & DEV_MISCONFIGURED) {
		DBG(1, "The camera is misconfigured. Close and open it "
		       "again.");
		mutex_unlock(&cam->fileop_mutex);
		return -EIO;
	}

	V4LDBG(3, "sn9c102", cmd);

	err = sn9c102_ioctl_v4l2(filp, cmd, (void __user *)arg);

	mutex_unlock(&cam->fileop_mutex);

	return err;
}

/*****************************************************************************/

static const struct v4l2_file_operations sn9c102_fops = {
	.owner = THIS_MODULE,
	.open = sn9c102_open,
	.release = sn9c102_release,
	.unlocked_ioctl = sn9c102_ioctl,
	.read = sn9c102_read,
	.poll = sn9c102_poll,
	.mmap = sn9c102_mmap,
};

/*****************************************************************************/

/* It exists a single interface only. We do not need to validate anything. */
static int
sn9c102_usb_probe(struct usb_interface* intf, const struct usb_device_id* id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct sn9c102_device* cam;
	static unsigned int dev_nr;
	unsigned int i;
	int err = 0, r;

	if (!(cam = kzalloc(sizeof(struct sn9c102_device), GFP_KERNEL)))
		return -ENOMEM;

	cam->usbdev = udev;

	if (!(cam->control_buffer = kzalloc(8, GFP_KERNEL))) {
		DBG(1, "kzalloc() failed");
		err = -ENOMEM;
		goto fail;
	}

	if (!(cam->v4ldev = video_device_alloc())) {
		DBG(1, "video_device_alloc() failed");
		err = -ENOMEM;
		goto fail;
	}

	r = sn9c102_read_reg(cam, 0x00);
	if (r < 0 || (r != 0x10 && r != 0x11 && r != 0x12)) {
		DBG(1, "Sorry, this is not a SN9C1xx-based camera "
		       "(vid:pid 0x%04X:0x%04X)", id->idVendor, id->idProduct);
		err = -ENODEV;
		goto fail;
	}

	cam->bridge = id->driver_info;
	switch (cam->bridge) {
	case BRIDGE_SN9C101:
	case BRIDGE_SN9C102:
		DBG(2, "SN9C10[12] PC Camera Controller detected "
		       "(vid:pid 0x%04X:0x%04X)", id->idVendor, id->idProduct);
		break;
	case BRIDGE_SN9C103:
		DBG(2, "SN9C103 PC Camera Controller detected "
		       "(vid:pid 0x%04X:0x%04X)", id->idVendor, id->idProduct);
		break;
	case BRIDGE_SN9C105:
		DBG(2, "SN9C105 PC Camera Controller detected "
		       "(vid:pid 0x%04X:0x%04X)", id->idVendor, id->idProduct);
		break;
	case BRIDGE_SN9C120:
		DBG(2, "SN9C120 PC Camera Controller detected "
		       "(vid:pid 0x%04X:0x%04X)", id->idVendor, id->idProduct);
		break;
	}

	for  (i = 0; i < ARRAY_SIZE(sn9c102_sensor_table); i++) {
		err = sn9c102_sensor_table[i](cam);
		if (!err)
			break;
	}

	if (!err) {
		DBG(2, "%s image sensor detected", cam->sensor.name);
		DBG(3, "Support for %s maintained by %s",
		    cam->sensor.name, cam->sensor.maintainer);
	} else {
		DBG(1, "No supported image sensor detected for this bridge");
		err = -ENODEV;
		goto fail;
	}

	if (!(cam->bridge & cam->sensor.supported_bridge)) {
		DBG(1, "Bridge not supported");
		err = -ENODEV;
		goto fail;
	}

	if (sn9c102_init(cam)) {
		DBG(1, "Initialization failed. I will retry on open().");
		cam->state |= DEV_MISCONFIGURED;
	}

	strcpy(cam->v4ldev->name, "SN9C1xx PC Camera");
	cam->v4ldev->fops = &sn9c102_fops;
	cam->v4ldev->release = video_device_release;
	cam->v4ldev->parent = &udev->dev;

	init_completion(&cam->probe);

	err = video_register_device(cam->v4ldev, VFL_TYPE_GRABBER,
				    video_nr[dev_nr]);
	if (err) {
		DBG(1, "V4L2 device registration failed");
		if (err == -ENFILE && video_nr[dev_nr] == -1)
			DBG(1, "Free /dev/videoX node not found");
		video_nr[dev_nr] = -1;
		dev_nr = (dev_nr < SN9C102_MAX_DEVICES-1) ? dev_nr+1 : 0;
		complete_all(&cam->probe);
		goto fail;
	}

	DBG(2, "V4L2 device registered as %s",
	    video_device_node_name(cam->v4ldev));

	video_set_drvdata(cam->v4ldev, cam);
	cam->module_param.force_munmap = force_munmap[dev_nr];
	cam->module_param.frame_timeout = frame_timeout[dev_nr];

	dev_nr = (dev_nr < SN9C102_MAX_DEVICES-1) ? dev_nr+1 : 0;

#ifdef CONFIG_VIDEO_ADV_DEBUG
	err = sn9c102_create_sysfs(cam);
	if (!err)
		DBG(2, "Optional device control through 'sysfs' "
		       "interface ready");
	else
		DBG(2, "Failed to create optional 'sysfs' interface for "
		       "device controlling. Error #%d", err);
#else
	DBG(2, "Optional device control through 'sysfs' interface disabled");
	DBG(3, "Compile the kernel with the 'CONFIG_VIDEO_ADV_DEBUG' "
	       "configuration option to enable it.");
#endif

	usb_set_intfdata(intf, cam);
	kref_init(&cam->kref);
	usb_get_dev(cam->usbdev);

	complete_all(&cam->probe);

	return 0;

fail:
	if (cam) {
		kfree(cam->control_buffer);
		if (cam->v4ldev)
			video_device_release(cam->v4ldev);
		kfree(cam);
	}
	return err;
}


static void sn9c102_usb_disconnect(struct usb_interface* intf)
{
	struct sn9c102_device* cam;

	down_write(&sn9c102_dev_lock);

	cam = usb_get_intfdata(intf);

	DBG(2, "Disconnecting %s...", cam->v4ldev->name);

	if (cam->users) {
		DBG(2, "Device %s is open! Deregistration and memory "
		       "deallocation are deferred.",
		    video_device_node_name(cam->v4ldev));
		cam->state |= DEV_MISCONFIGURED;
		sn9c102_stop_transfer(cam);
		cam->state |= DEV_DISCONNECTED;
		wake_up_interruptible(&cam->wait_frame);
		wake_up(&cam->wait_stream);
	} else
		cam->state |= DEV_DISCONNECTED;

	wake_up_interruptible_all(&cam->wait_open);

	kref_put(&cam->kref, sn9c102_release_resources);

	up_write(&sn9c102_dev_lock);
}


static struct usb_driver sn9c102_usb_driver = {
	.name =       "sn9c102",
	.id_table =   sn9c102_id_table,
	.probe =      sn9c102_usb_probe,
	.disconnect = sn9c102_usb_disconnect,
};

/*****************************************************************************/

static int __init sn9c102_module_init(void)
{
	int err = 0;

	KDBG(2, SN9C102_MODULE_NAME " v" SN9C102_MODULE_VERSION);
	KDBG(3, SN9C102_MODULE_AUTHOR);

	if ((err = usb_register(&sn9c102_usb_driver)))
		KDBG(1, "usb_register() failed");

	return err;
}


static void __exit sn9c102_module_exit(void)
{
	usb_deregister(&sn9c102_usb_driver);
}


module_init(sn9c102_module_init);
module_exit(sn9c102_module_exit);
