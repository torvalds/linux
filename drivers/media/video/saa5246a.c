/*
 * Driver for the SAA5246A or SAA5281 Teletext (=Videotext) decoder chips from
 * Philips.
 *
 * Only capturing of Teletext pages is tested. The videotext chips also have a
 * TV output but my hardware doesn't use it. For this reason this driver does
 * not support changing any TV display settings.
 *
 * Copyright (C) 2004 Michael Geng <linux@MichaelGeng.de>
 *
 * Derived from
 *
 * saa5249 driver
 * Copyright (C) 1998 Richard Guenther
 * <richard.guenther@student.uni-tuebingen.de>
 *
 * with changes by
 * Alan Cox <Alan.Cox@linux.org>
 *
 * and
 *
 * vtx.c
 * Copyright (C) 1994-97 Martin Buck  <martin-2.buck@student.uni-ulm.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videotext.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <linux/mutex.h>

#include "saa5246a.h"

MODULE_AUTHOR("Michael Geng <linux@MichaelGeng.de>");
MODULE_DESCRIPTION("Philips SAA5246A, SAA5281 Teletext decoder driver");
MODULE_LICENSE("GPL");

struct saa5246a_device
{
	u8     pgbuf[NUM_DAUS][VTX_VIRTUALSIZE];
	int    is_searching[NUM_DAUS];
	struct i2c_client *client;
	struct mutex lock;
};

static struct video_device saa_template;	/* Declared near bottom */

/* Addresses to scan */
static unsigned short normal_i2c[]	 = { I2C_ADDRESS, I2C_CLIENT_END };
I2C_CLIENT_INSMOD;

static struct i2c_client client_template;

static int saa5246a_attach(struct i2c_adapter *adap, int addr, int kind)
{
	int pgbuf;
	int err;
	struct i2c_client *client;
	struct video_device *vd;
	struct saa5246a_device *t;

	printk(KERN_INFO "saa5246a: teletext chip found.\n");
	client=kmalloc(sizeof(*client), GFP_KERNEL);
	if(client==NULL)
		return -ENOMEM;
	client_template.adapter = adap;
	client_template.addr = addr;
	memcpy(client, &client_template, sizeof(*client));
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if(t==NULL)
	{
		kfree(client);
		return -ENOMEM;
	}
	strlcpy(client->name, IF_NAME, I2C_NAME_SIZE);
	mutex_init(&t->lock);

	/*
	 *	Now create a video4linux device
	 */

	vd = video_device_alloc();
	if(vd==NULL)
	{
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	i2c_set_clientdata(client, vd);
	memcpy(vd, &saa_template, sizeof(*vd));

	for (pgbuf = 0; pgbuf < NUM_DAUS; pgbuf++)
	{
		memset(t->pgbuf[pgbuf], ' ', sizeof(t->pgbuf[0]));
		t->is_searching[pgbuf] = false;
	}
	vd->priv=t;


	/*
	 *	Register it
	 */

	if((err=video_register_device(vd, VFL_TYPE_VTX,-1))<0)
	{
		kfree(t);
		kfree(client);
		video_device_release(vd);
		return err;
	}
	t->client = client;
	i2c_attach_client(client);
	return 0;
}

/*
 *	We do most of the hard work when we become a device on the i2c.
 */
static int saa5246a_probe(struct i2c_adapter *adap)
{
	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, saa5246a_attach);
	return 0;
}

static int saa5246a_detach(struct i2c_client *client)
{
	struct video_device *vd = i2c_get_clientdata(client);
	i2c_detach_client(client);
	video_unregister_device(vd);
	kfree(vd->priv);
	kfree(client);
	return 0;
}

/*
 *	I2C interfaces
 */

static struct i2c_driver i2c_driver_videotext =
{
	.driver = {
		.name 	= IF_NAME,		/* name */
	},
	.id 		= I2C_DRIVERID_SAA5249, /* in i2c.h */
	.attach_adapter = saa5246a_probe,
	.detach_client  = saa5246a_detach,
};

static struct i2c_client client_template = {
	.driver		= &i2c_driver_videotext,
	.name		= "(unset)",
};

static int i2c_sendbuf(struct saa5246a_device *t, int reg, int count, u8 *data)
{
	char buf[64];

	buf[0] = reg;
	memcpy(buf+1, data, count);

	if(i2c_master_send(t->client, buf, count+1)==count+1)
		return 0;
	return -1;
}

static int i2c_senddata(struct saa5246a_device *t, ...)
{
	unsigned char buf[64];
	int v;
	int ct=0;
	va_list argp;
	va_start(argp,t);

	while((v=va_arg(argp,int))!=-1)
		buf[ct++]=v;
	return i2c_sendbuf(t, buf[0], ct-1, buf+1);
}

/* Get count number of bytes from I²C-device at address adr, store them in buf.
 * Start & stop handshaking is done by this routine, ack will be sent after the
 * last byte to inhibit further sending of data. If uaccess is 'true', data is
 * written to user-space with put_user. Returns -1 if I²C-device didn't send
 * acknowledge, 0 otherwise
 */
static int i2c_getdata(struct saa5246a_device *t, int count, u8 *buf)
{
	if(i2c_master_recv(t->client, buf, count)!=count)
		return -1;
	return 0;
}

/* When a page is found then the not FOUND bit in one of the status registers
 * of the SAA5264A chip is cleared. Unfortunately this bit is not set
 * automatically when a new page is requested. Instead this function must be
 * called after a page has been requested.
 *
 * Return value: 0 if successful
 */
static int saa5246a_clear_found_bit(struct saa5246a_device *t,
	unsigned char dau_no)
{
	unsigned char row_25_column_8;

	if (i2c_senddata(t, SAA5246A_REGISTER_R8,

		dau_no |
		R8_DO_NOT_CLEAR_MEMORY,

		R9_CURSER_ROW_25,

		R10_CURSER_COLUMN_8,

		COMMAND_END) ||
		i2c_getdata(t, 1, &row_25_column_8))
	{
		return -EIO;
	}
	row_25_column_8 |= ROW25_COLUMN8_PAGE_NOT_FOUND;
	if (i2c_senddata(t, SAA5246A_REGISTER_R8,

		dau_no |
		R8_DO_NOT_CLEAR_MEMORY,

		R9_CURSER_ROW_25,

		R10_CURSER_COLUMN_8,

		row_25_column_8,

		COMMAND_END))
	{
		return -EIO;
	}

	return 0;
}

/* Requests one videotext page as described in req. The fields of req are
 * checked and an error is returned if something is invalid.
 *
 * Return value: 0 if successful
 */
static int saa5246a_request_page(struct saa5246a_device *t,
    vtx_pagereq_t *req)
{
	if (req->pagemask < 0 || req->pagemask >= PGMASK_MAX)
		return -EINVAL;
	if (req->pagemask & PGMASK_PAGE)
		if (req->page < 0 || req->page > PAGE_MAX)
			return -EINVAL;
	if (req->pagemask & PGMASK_HOUR)
		if (req->hour < 0 || req->hour > HOUR_MAX)
			return -EINVAL;
	if (req->pagemask & PGMASK_MINUTE)
		if (req->minute < 0 || req->minute > MINUTE_MAX)
			return -EINVAL;
	if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS)
		return -EINVAL;

	if (i2c_senddata(t, SAA5246A_REGISTER_R2,

		R2_IN_R3_SELECT_PAGE_HUNDREDS |
		req->pgbuf << 4 |
		R2_BANK_0 |
		R2_HAMMING_CHECK_OFF,

		HUNDREDS_OF_PAGE(req->page) |
		R3_HOLD_PAGE |
		(req->pagemask & PG_HUND ?
			R3_PAGE_HUNDREDS_DO_CARE :
			R3_PAGE_HUNDREDS_DO_NOT_CARE),

		TENS_OF_PAGE(req->page) |
		(req->pagemask & PG_TEN ?
			R3_PAGE_TENS_DO_CARE :
			R3_PAGE_TENS_DO_NOT_CARE),

		UNITS_OF_PAGE(req->page) |
		(req->pagemask & PG_UNIT ?
			R3_PAGE_UNITS_DO_CARE :
			R3_PAGE_UNITS_DO_NOT_CARE),

		TENS_OF_HOUR(req->hour) |
		(req->pagemask & HR_TEN ?
			R3_HOURS_TENS_DO_CARE :
			R3_HOURS_TENS_DO_NOT_CARE),

		UNITS_OF_HOUR(req->hour) |
		(req->pagemask & HR_UNIT ?
			R3_HOURS_UNITS_DO_CARE :
			R3_HOURS_UNITS_DO_NOT_CARE),

		TENS_OF_MINUTE(req->minute) |
		(req->pagemask & MIN_TEN ?
			R3_MINUTES_TENS_DO_CARE :
			R3_MINUTES_TENS_DO_NOT_CARE),

		UNITS_OF_MINUTE(req->minute) |
		(req->pagemask & MIN_UNIT ?
			R3_MINUTES_UNITS_DO_CARE :
			R3_MINUTES_UNITS_DO_NOT_CARE),

		COMMAND_END) || i2c_senddata(t, SAA5246A_REGISTER_R2,

		R2_IN_R3_SELECT_PAGE_HUNDREDS |
		req->pgbuf << 4 |
		R2_BANK_0 |
		R2_HAMMING_CHECK_OFF,

		HUNDREDS_OF_PAGE(req->page) |
		R3_UPDATE_PAGE |
		(req->pagemask & PG_HUND ?
			R3_PAGE_HUNDREDS_DO_CARE :
			R3_PAGE_HUNDREDS_DO_NOT_CARE),

		COMMAND_END))
	{
		return -EIO;
	}

	t->is_searching[req->pgbuf] = true;
	return 0;
}

/* This routine decodes the page number from the infobits contained in line 25.
 *
 * Parameters:
 * infobits: must be bits 0 to 9 of column 25
 *
 * Return value: page number coded in hexadecimal, i. e. page 123 is coded 0x123
 */
static inline int saa5246a_extract_pagenum_from_infobits(
    unsigned char infobits[10])
{
	int page_hundreds, page_tens, page_units;

	page_units    = infobits[0] & ROW25_COLUMN0_PAGE_UNITS;
	page_tens     = infobits[1] & ROW25_COLUMN1_PAGE_TENS;
	page_hundreds = infobits[8] & ROW25_COLUMN8_PAGE_HUNDREDS;

	/* page 0x.. means page 8.. */
	if (page_hundreds == 0)
		page_hundreds = 8;

	return((page_hundreds << 8) | (page_tens << 4) | page_units);
}

/* Decodes the hour from the infobits contained in line 25.
 *
 * Parameters:
 * infobits: must be bits 0 to 9 of column 25
 *
 * Return: hour coded in hexadecimal, i. e. 12h is coded 0x12
 */
static inline int saa5246a_extract_hour_from_infobits(
    unsigned char infobits[10])
{
	int hour_tens, hour_units;

	hour_units = infobits[4] & ROW25_COLUMN4_HOUR_UNITS;
	hour_tens  = infobits[5] & ROW25_COLUMN5_HOUR_TENS;

	return((hour_tens << 4) | hour_units);
}

/* Decodes the minutes from the infobits contained in line 25.
 *
 * Parameters:
 * infobits: must be bits 0 to 9 of column 25
 *
 * Return: minutes coded in hexadecimal, i. e. 10min is coded 0x10
 */
static inline int saa5246a_extract_minutes_from_infobits(
    unsigned char infobits[10])
{
	int minutes_tens, minutes_units;

	minutes_units = infobits[2] & ROW25_COLUMN2_MINUTES_UNITS;
	minutes_tens  = infobits[3] & ROW25_COLUMN3_MINUTES_TENS;

	return((minutes_tens << 4) | minutes_units);
}

/* Reads the status bits contained in the first 10 columns of the first line
 * and extracts the information into info.
 *
 * Return value: 0 if successful
 */
static inline int saa5246a_get_status(struct saa5246a_device *t,
    vtx_pageinfo_t *info, unsigned char dau_no)
{
	unsigned char infobits[10];
	int column;

	if (dau_no >= NUM_DAUS)
		return -EINVAL;

	if (i2c_senddata(t, SAA5246A_REGISTER_R8,

		dau_no |
		R8_DO_NOT_CLEAR_MEMORY,

		R9_CURSER_ROW_25,

		R10_CURSER_COLUMN_0,

		COMMAND_END) ||
		i2c_getdata(t, 10, infobits))
	{
		return -EIO;
	}

	info->pagenum = saa5246a_extract_pagenum_from_infobits(infobits);
	info->hour    = saa5246a_extract_hour_from_infobits(infobits);
	info->minute  = saa5246a_extract_minutes_from_infobits(infobits);
	info->charset = ((infobits[7] & ROW25_COLUMN7_CHARACTER_SET) >> 1);
	info->delete = !!(infobits[3] & ROW25_COLUMN3_DELETE_PAGE);
	info->headline = !!(infobits[5] & ROW25_COLUMN5_INSERT_HEADLINE);
	info->subtitle = !!(infobits[5] & ROW25_COLUMN5_INSERT_SUBTITLE);
	info->supp_header = !!(infobits[6] & ROW25_COLUMN6_SUPPRESS_HEADER);
	info->update = !!(infobits[6] & ROW25_COLUMN6_UPDATE_PAGE);
	info->inter_seq = !!(infobits[6] & ROW25_COLUMN6_INTERRUPTED_SEQUENCE);
	info->dis_disp = !!(infobits[6] & ROW25_COLUMN6_SUPPRESS_DISPLAY);
	info->serial = !!(infobits[7] & ROW25_COLUMN7_SERIAL_MODE);
	info->notfound = !!(infobits[8] & ROW25_COLUMN8_PAGE_NOT_FOUND);
	info->pblf = !!(infobits[9] & ROW25_COLUMN9_PAGE_BEING_LOOKED_FOR);
	info->hamming = 0;
	for (column = 0; column <= 7; column++) {
		if (infobits[column] & ROW25_COLUMN0_TO_7_HAMMING_ERROR) {
			info->hamming = 1;
			break;
		}
	}
	if (!info->hamming && !info->notfound)
		t->is_searching[dau_no] = false;
	return 0;
}

/* Reads 1 videotext page buffer of the SAA5246A.
 *
 * req is used both as input and as output. It contains information which part
 * must be read. The videotext page is copied into req->buffer.
 *
 * Return value: 0 if successful
 */
static inline int saa5246a_get_page(struct saa5246a_device *t,
	vtx_pagereq_t *req)
{
	int start, end, size;
	char *buf;
	int err;

	if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS ||
	    req->start < 0 || req->start > req->end || req->end >= VTX_PAGESIZE)
		return -EINVAL;

	buf = kmalloc(VTX_PAGESIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Read "normal" part of page */
	err = -EIO;

	end = min(req->end, VTX_PAGESIZE - 1);
	if (i2c_senddata(t, SAA5246A_REGISTER_R8,
			req->pgbuf | R8_DO_NOT_CLEAR_MEMORY,
			ROW(req->start), COLUMN(req->start), COMMAND_END))
		goto out;
	if (i2c_getdata(t, end - req->start + 1, buf))
		goto out;
	err = -EFAULT;
	if (copy_to_user(req->buffer, buf, end - req->start + 1))
		goto out;

	/* Always get the time from buffer 4, since this stupid SAA5246A only
	 * updates the currently displayed buffer...
	 */
	if (REQ_CONTAINS_TIME(req)) {
		start = max(req->start, POS_TIME_START);
		end   = min(req->end,   POS_TIME_END);
		size = end - start + 1;
		err = -EINVAL;
		if (size < 0)
			goto out;
		err = -EIO;
		if (i2c_senddata(t, SAA5246A_REGISTER_R8,
				R8_ACTIVE_CHAPTER_4 | R8_DO_NOT_CLEAR_MEMORY,
				R9_CURSER_ROW_0, start, COMMAND_END))
			goto out;
		if (i2c_getdata(t, size, buf))
			goto out;
		err = -EFAULT;
		if (copy_to_user(req->buffer + start - req->start, buf, size))
			goto out;
	}
	/* Insert the header from buffer 4 only, if acquisition circuit is still searching for a page */
	if (REQ_CONTAINS_HEADER(req) && t->is_searching[req->pgbuf]) {
		start = max(req->start, POS_HEADER_START);
		end   = min(req->end,   POS_HEADER_END);
		size = end - start + 1;
		err = -EINVAL;
		if (size < 0)
			goto out;
		err = -EIO;
		if (i2c_senddata(t, SAA5246A_REGISTER_R8,
				R8_ACTIVE_CHAPTER_4 | R8_DO_NOT_CLEAR_MEMORY,
				R9_CURSER_ROW_0, start, COMMAND_END))
			goto out;
		if (i2c_getdata(t, end - start + 1, buf))
			goto out;
		err = -EFAULT;
		if (copy_to_user(req->buffer + start - req->start, buf, size))
			goto out;
	}
	err = 0;
out:
	kfree(buf);
	return err;
}

/* Stops the acquisition circuit given in dau_no. The page buffer associated
 * with this acquisition circuit will no more be updated. The other daus are
 * not affected.
 *
 * Return value: 0 if successful
 */
static inline int saa5246a_stop_dau(struct saa5246a_device *t,
    unsigned char dau_no)
{
	if (dau_no >= NUM_DAUS)
		return -EINVAL;
	if (i2c_senddata(t, SAA5246A_REGISTER_R2,

		R2_IN_R3_SELECT_PAGE_HUNDREDS |
		dau_no << 4 |
		R2_BANK_0 |
		R2_HAMMING_CHECK_OFF,

		R3_PAGE_HUNDREDS_0 |
		R3_HOLD_PAGE |
		R3_PAGE_HUNDREDS_DO_NOT_CARE,

		COMMAND_END))
	{
		return -EIO;
	}
	t->is_searching[dau_no] = false;
	return 0;
}

/*  Handles ioctls defined in videotext.h
 *
 *  Returns 0 if successful
 */
static int do_saa5246a_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg)
{
	struct video_device *vd = video_devdata(file);
	struct saa5246a_device *t=vd->priv;
	switch(cmd)
	{
		case VTXIOCGETINFO:
		{
			vtx_info_t *info = arg;

			info->version_major = MAJOR_VERSION;
			info->version_minor = MINOR_VERSION;
			info->numpages = NUM_DAUS;
			return 0;
		}

		case VTXIOCCLRPAGE:
		{
			vtx_pagereq_t *req = arg;

			if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS)
				return -EINVAL;
			memset(t->pgbuf[req->pgbuf], ' ', sizeof(t->pgbuf[0]));
			return 0;
		}

		case VTXIOCCLRFOUND:
		{
			vtx_pagereq_t *req = arg;

			if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS)
				return -EINVAL;
			return(saa5246a_clear_found_bit(t, req->pgbuf));
		}

		case VTXIOCPAGEREQ:
		{
			vtx_pagereq_t *req = arg;

			return(saa5246a_request_page(t, req));
		}

		case VTXIOCGETSTAT:
		{
			vtx_pagereq_t *req = arg;
			vtx_pageinfo_t info;
			int rval;

			if ((rval = saa5246a_get_status(t, &info, req->pgbuf)))
				return rval;
			if(copy_to_user(req->buffer, &info,
				sizeof(vtx_pageinfo_t)))
				return -EFAULT;
			return 0;
		}

		case VTXIOCGETPAGE:
		{
			vtx_pagereq_t *req = arg;

			return(saa5246a_get_page(t, req));
		}

		case VTXIOCSTOPDAU:
		{
			vtx_pagereq_t *req = arg;

			return(saa5246a_stop_dau(t, req->pgbuf));
		}

		case VTXIOCPUTPAGE:
		case VTXIOCSETDISP:
		case VTXIOCPUTSTAT:
			return 0;

		case VTXIOCCLRCACHE:
		{
			return 0;
		}

		case VTXIOCSETVIRT:
		{
			/* I do not know what "virtual mode" means */
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * Translates old vtx IOCTLs to new ones
 *
 * This keeps new kernel versions compatible with old userspace programs.
 */
static inline unsigned int vtx_fix_command(unsigned int cmd)
{
	switch (cmd) {
	case VTXIOCGETINFO_OLD:
		cmd = VTXIOCGETINFO;
		break;
	case VTXIOCCLRPAGE_OLD:
		cmd = VTXIOCCLRPAGE;
		break;
	case VTXIOCCLRFOUND_OLD:
		cmd = VTXIOCCLRFOUND;
		break;
	case VTXIOCPAGEREQ_OLD:
		cmd = VTXIOCPAGEREQ;
		break;
	case VTXIOCGETSTAT_OLD:
		cmd = VTXIOCGETSTAT;
		break;
	case VTXIOCGETPAGE_OLD:
		cmd = VTXIOCGETPAGE;
		break;
	case VTXIOCSTOPDAU_OLD:
		cmd = VTXIOCSTOPDAU;
		break;
	case VTXIOCPUTPAGE_OLD:
		cmd = VTXIOCPUTPAGE;
		break;
	case VTXIOCSETDISP_OLD:
		cmd = VTXIOCSETDISP;
		break;
	case VTXIOCPUTSTAT_OLD:
		cmd = VTXIOCPUTSTAT;
		break;
	case VTXIOCCLRCACHE_OLD:
		cmd = VTXIOCCLRCACHE;
		break;
	case VTXIOCSETVIRT_OLD:
		cmd = VTXIOCSETVIRT;
		break;
	}
	return cmd;
}

/*
 *	Handle the locking
 */
static int saa5246a_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct video_device *vd = video_devdata(file);
	struct saa5246a_device *t = vd->priv;
	int err;

	cmd = vtx_fix_command(cmd);
	mutex_lock(&t->lock);
	err = video_usercopy(inode, file, cmd, arg, do_saa5246a_ioctl);
	mutex_unlock(&t->lock);
	return err;
}

static int saa5246a_open(struct inode *inode, struct file *file)
{
	struct video_device *vd = video_devdata(file);
	struct saa5246a_device *t = vd->priv;
	int err;

	err = video_exclusive_open(inode,file);
	if (err < 0)
		return err;

	if (t->client==NULL) {
		err = -ENODEV;
		goto fail;
	}

	if (i2c_senddata(t, SAA5246A_REGISTER_R0,

		R0_SELECT_R11 |
		R0_PLL_TIME_CONSTANT_LONG |
		R0_ENABLE_nODD_EVEN_OUTPUT |
		R0_ENABLE_HDR_POLL |
		R0_DO_NOT_FORCE_nODD_EVEN_LOW_IF_PICTURE_DISPLAYED |
		R0_NO_FREE_RUN_PLL |
		R0_NO_AUTOMATIC_FASTEXT_PROMPT,

		R1_NON_INTERLACED_312_312_LINES |
		R1_DEW |
		R1_EXTENDED_PACKET_DISABLE |
		R1_DAUS_ALL_ON |
		R1_8_BITS_NO_PARITY |
		R1_VCS_TO_SCS,

		COMMAND_END) ||
		i2c_senddata(t, SAA5246A_REGISTER_R4,

		/* We do not care much for the TV display but nevertheless we
		 * need the currently displayed page later because only on that
		 * page the time is updated. */
		R4_DISPLAY_PAGE_4,

		COMMAND_END))
	{
		err = -EIO;
		goto fail;
	}

	return 0;

fail:
	video_exclusive_release(inode,file);
	return err;
}

static int saa5246a_release(struct inode *inode, struct file *file)
{
	struct video_device *vd = video_devdata(file);
	struct saa5246a_device *t = vd->priv;

	/* Stop all acquisition circuits. */
	i2c_senddata(t, SAA5246A_REGISTER_R1,

		R1_INTERLACED_312_AND_HALF_312_AND_HALF_LINES |
		R1_DEW |
		R1_EXTENDED_PACKET_DISABLE |
		R1_DAUS_ALL_OFF |
		R1_8_BITS_NO_PARITY |
		R1_VCS_TO_SCS,

		COMMAND_END);
	video_exclusive_release(inode,file);
	return 0;
}

static int __init init_saa_5246a (void)
{
	printk(KERN_INFO
		"SAA5246A (or compatible) Teletext decoder driver version %d.%d\n",
		MAJOR_VERSION, MINOR_VERSION);
	return i2c_add_driver(&i2c_driver_videotext);
}

static void __exit cleanup_saa_5246a (void)
{
	i2c_del_driver(&i2c_driver_videotext);
}

module_init(init_saa_5246a);
module_exit(cleanup_saa_5246a);

static const struct file_operations saa_fops = {
	.owner	 = THIS_MODULE,
	.open	 = saa5246a_open,
	.release = saa5246a_release,
	.ioctl	 = saa5246a_ioctl,
	.llseek	 = no_llseek,
};

static struct video_device saa_template =
{
	.owner	  = THIS_MODULE,
	.name	  = IF_NAME,
	.type	  = VID_TYPE_TELETEXT,
	.fops	  = &saa_fops,
	.release  = video_device_release,
	.minor    = -1,
};
