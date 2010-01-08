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
 * Alan Cox <alan@lxorguk.ukuu.org.uk>
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
#include <linux/mutex.h>
#include <linux/videotext.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-i2c-drv.h>

MODULE_AUTHOR("Michael Geng <linux@MichaelGeng.de>");
MODULE_DESCRIPTION("Philips SAA5246A, SAA5281 Teletext decoder driver");
MODULE_LICENSE("GPL");

#define MAJOR_VERSION 1		/* driver major version number */
#define MINOR_VERSION 8		/* driver minor version number */

/* Number of DAUs = number of pages that can be searched at the same time. */
#define NUM_DAUS 4

#define NUM_ROWS_PER_PAGE 40

/* first column is 0 (not 1) */
#define POS_TIME_START 32
#define POS_TIME_END 39

#define POS_HEADER_START 7
#define POS_HEADER_END 31

/* Returns 'true' if the part of the videotext page described with req contains
   (at least parts of) the time field */
#define REQ_CONTAINS_TIME(p_req) \
	((p_req)->start <= POS_TIME_END && \
	 (p_req)->end   >= POS_TIME_START)

/* Returns 'true' if the part of the videotext page described with req contains
   (at least parts of) the page header */
#define REQ_CONTAINS_HEADER(p_req) \
	((p_req)->start <= POS_HEADER_END && \
	 (p_req)->end   >= POS_HEADER_START)

/*****************************************************************************/
/* Mode register numbers of the SAA5246A				     */
/*****************************************************************************/
#define SAA5246A_REGISTER_R0    0
#define SAA5246A_REGISTER_R1    1
#define SAA5246A_REGISTER_R2    2
#define SAA5246A_REGISTER_R3    3
#define SAA5246A_REGISTER_R4    4
#define SAA5246A_REGISTER_R5    5
#define SAA5246A_REGISTER_R6    6
#define SAA5246A_REGISTER_R7    7
#define SAA5246A_REGISTER_R8    8
#define SAA5246A_REGISTER_R9    9
#define SAA5246A_REGISTER_R10  10
#define SAA5246A_REGISTER_R11  11
#define SAA5246A_REGISTER_R11B 11

/* SAA5246A mode registers often autoincrement to the next register.
   Therefore we use variable argument lists. The following macro indicates
   the end of a command list. */
#define COMMAND_END (-1)

/*****************************************************************************/
/* Contents of the mode registers of the SAA5246A			     */
/*****************************************************************************/
/* Register R0 (Advanced Control) */
#define R0_SELECT_R11					   0x00
#define R0_SELECT_R11B					   0x01

#define R0_PLL_TIME_CONSTANT_LONG			   0x00
#define R0_PLL_TIME_CONSTANT_SHORT			   0x02

#define R0_ENABLE_nODD_EVEN_OUTPUT			   0x00
#define R0_DISABLE_nODD_EVEN_OUTPUT			   0x04

#define R0_ENABLE_HDR_POLL				   0x00
#define R0_DISABLE_HDR_POLL				   0x10

#define R0_DO_NOT_FORCE_nODD_EVEN_LOW_IF_PICTURE_DISPLAYED 0x00
#define R0_FORCE_nODD_EVEN_LOW_IF_PICTURE_DISPLAYED	   0x20

#define R0_NO_FREE_RUN_PLL				   0x00
#define R0_FREE_RUN_PLL					   0x40

#define R0_NO_AUTOMATIC_FASTEXT_PROMPT			   0x00
#define R0_AUTOMATIC_FASTEXT_PROMPT			   0x80

/* Register R1 (Mode) */
#define R1_INTERLACED_312_AND_HALF_312_AND_HALF_LINES	   0x00
#define R1_NON_INTERLACED_312_313_LINES			   0x01
#define R1_NON_INTERLACED_312_312_LINES			   0x02
#define R1_FFB_LEADING_EDGE_IN_FIRST_BROAD_PULSE	   0x03
#define R1_FFB_LEADING_EDGE_IN_SECOND_BROAD_PULSE	   0x07

#define R1_DEW						   0x00
#define R1_FULL_FIELD					   0x08

#define R1_EXTENDED_PACKET_DISABLE			   0x00
#define R1_EXTENDED_PACKET_ENABLE			   0x10

#define R1_DAUS_ALL_ON					   0x00
#define R1_DAUS_ALL_OFF					   0x20

#define R1_7_BITS_PLUS_PARITY				   0x00
#define R1_8_BITS_NO_PARITY				   0x40

#define R1_VCS_TO_SCS					   0x00
#define R1_NO_VCS_TO_SCS				   0x80

/* Register R2 (Page request address) */
#define R2_IN_R3_SELECT_PAGE_HUNDREDS			   0x00
#define R2_IN_R3_SELECT_PAGE_TENS			   0x01
#define R2_IN_R3_SELECT_PAGE_UNITS			   0x02
#define R2_IN_R3_SELECT_HOURS_TENS			   0x03
#define R2_IN_R3_SELECT_HOURS_UNITS			   0x04
#define R2_IN_R3_SELECT_MINUTES_TENS			   0x05
#define R2_IN_R3_SELECT_MINUTES_UNITS			   0x06

#define R2_DAU_0					   0x00
#define R2_DAU_1					   0x10
#define R2_DAU_2					   0x20
#define R2_DAU_3					   0x30

#define R2_BANK_0					   0x00
#define R2_BANK 1					   0x40

#define R2_HAMMING_CHECK_ON				   0x80
#define R2_HAMMING_CHECK_OFF				   0x00

/* Register R3 (Page request data) */
#define R3_PAGE_HUNDREDS_0				   0x00
#define R3_PAGE_HUNDREDS_1				   0x01
#define R3_PAGE_HUNDREDS_2				   0x02
#define R3_PAGE_HUNDREDS_3				   0x03
#define R3_PAGE_HUNDREDS_4				   0x04
#define R3_PAGE_HUNDREDS_5				   0x05
#define R3_PAGE_HUNDREDS_6				   0x06
#define R3_PAGE_HUNDREDS_7				   0x07

#define R3_HOLD_PAGE					   0x00
#define R3_UPDATE_PAGE					   0x08

#define R3_PAGE_HUNDREDS_DO_NOT_CARE			   0x00
#define R3_PAGE_HUNDREDS_DO_CARE			   0x10

#define R3_PAGE_TENS_DO_NOT_CARE			   0x00
#define R3_PAGE_TENS_DO_CARE				   0x10

#define R3_PAGE_UNITS_DO_NOT_CARE			   0x00
#define R3_PAGE_UNITS_DO_CARE				   0x10

#define R3_HOURS_TENS_DO_NOT_CARE			   0x00
#define R3_HOURS_TENS_DO_CARE				   0x10

#define R3_HOURS_UNITS_DO_NOT_CARE			   0x00
#define R3_HOURS_UNITS_DO_CARE				   0x10

#define R3_MINUTES_TENS_DO_NOT_CARE			   0x00
#define R3_MINUTES_TENS_DO_CARE				   0x10

#define R3_MINUTES_UNITS_DO_NOT_CARE			   0x00
#define R3_MINUTES_UNITS_DO_CARE			   0x10

/* Register R4 (Display chapter) */
#define R4_DISPLAY_PAGE_0				   0x00
#define R4_DISPLAY_PAGE_1				   0x01
#define R4_DISPLAY_PAGE_2				   0x02
#define R4_DISPLAY_PAGE_3				   0x03
#define R4_DISPLAY_PAGE_4				   0x04
#define R4_DISPLAY_PAGE_5				   0x05
#define R4_DISPLAY_PAGE_6				   0x06
#define R4_DISPLAY_PAGE_7				   0x07

/* Register R5 (Normal display control) */
#define R5_PICTURE_INSIDE_BOXING_OFF			   0x00
#define R5_PICTURE_INSIDE_BOXING_ON			   0x01

#define R5_PICTURE_OUTSIDE_BOXING_OFF			   0x00
#define R5_PICTURE_OUTSIDE_BOXING_ON			   0x02

#define R5_TEXT_INSIDE_BOXING_OFF			   0x00
#define R5_TEXT_INSIDE_BOXING_ON			   0x04

#define R5_TEXT_OUTSIDE_BOXING_OFF			   0x00
#define R5_TEXT_OUTSIDE_BOXING_ON			   0x08

#define R5_CONTRAST_REDUCTION_INSIDE_BOXING_OFF		   0x00
#define R5_CONTRAST_REDUCTION_INSIDE_BOXING_ON		   0x10

#define R5_CONTRAST_REDUCTION_OUTSIDE_BOXING_OFF	   0x00
#define R5_CONTRAST_REDUCTION_OUTSIDE_BOXING_ON		   0x20

#define R5_BACKGROUND_COLOR_INSIDE_BOXING_OFF		   0x00
#define R5_BACKGROUND_COLOR_INSIDE_BOXING_ON		   0x40

#define R5_BACKGROUND_COLOR_OUTSIDE_BOXING_OFF		   0x00
#define R5_BACKGROUND_COLOR_OUTSIDE_BOXING_ON		   0x80

/* Register R6 (Newsflash display) */
#define R6_NEWSFLASH_PICTURE_INSIDE_BOXING_OFF		   0x00
#define R6_NEWSFLASH_PICTURE_INSIDE_BOXING_ON		   0x01

#define R6_NEWSFLASH_PICTURE_OUTSIDE_BOXING_OFF		   0x00
#define R6_NEWSFLASH_PICTURE_OUTSIDE_BOXING_ON		   0x02

#define R6_NEWSFLASH_TEXT_INSIDE_BOXING_OFF		   0x00
#define R6_NEWSFLASH_TEXT_INSIDE_BOXING_ON		   0x04

#define R6_NEWSFLASH_TEXT_OUTSIDE_BOXING_OFF		   0x00
#define R6_NEWSFLASH_TEXT_OUTSIDE_BOXING_ON		   0x08

#define R6_NEWSFLASH_CONTRAST_REDUCTION_INSIDE_BOXING_OFF  0x00
#define R6_NEWSFLASH_CONTRAST_REDUCTION_INSIDE_BOXING_ON   0x10

#define R6_NEWSFLASH_CONTRAST_REDUCTION_OUTSIDE_BOXING_OFF 0x00
#define R6_NEWSFLASH_CONTRAST_REDUCTION_OUTSIDE_BOXING_ON  0x20

#define R6_NEWSFLASH_BACKGROUND_COLOR_INSIDE_BOXING_OFF    0x00
#define R6_NEWSFLASH_BACKGROUND_COLOR_INSIDE_BOXING_ON	   0x40

#define R6_NEWSFLASH_BACKGROUND_COLOR_OUTSIDE_BOXING_OFF   0x00
#define R6_NEWSFLASH_BACKGROUND_COLOR_OUTSIDE_BOXING_ON    0x80

/* Register R7 (Display mode) */
#define R7_BOX_OFF_ROW_0				   0x00
#define R7_BOX_ON_ROW_0					   0x01

#define R7_BOX_OFF_ROW_1_TO_23				   0x00
#define R7_BOX_ON_ROW_1_TO_23				   0x02

#define R7_BOX_OFF_ROW_24				   0x00
#define R7_BOX_ON_ROW_24				   0x04

#define R7_SINGLE_HEIGHT				   0x00
#define R7_DOUBLE_HEIGHT				   0x08

#define R7_TOP_HALF					   0x00
#define R7_BOTTOM_HALF					   0x10

#define R7_REVEAL_OFF					   0x00
#define R7_REVEAL_ON					   0x20

#define R7_CURSER_OFF					   0x00
#define R7_CURSER_ON					   0x40

#define R7_STATUS_BOTTOM				   0x00
#define R7_STATUS_TOP					   0x80

/* Register R8 (Active chapter) */
#define R8_ACTIVE_CHAPTER_0				   0x00
#define R8_ACTIVE_CHAPTER_1				   0x01
#define R8_ACTIVE_CHAPTER_2				   0x02
#define R8_ACTIVE_CHAPTER_3				   0x03
#define R8_ACTIVE_CHAPTER_4				   0x04
#define R8_ACTIVE_CHAPTER_5				   0x05
#define R8_ACTIVE_CHAPTER_6				   0x06
#define R8_ACTIVE_CHAPTER_7				   0x07

#define R8_CLEAR_MEMORY					   0x08
#define R8_DO_NOT_CLEAR_MEMORY				   0x00

/* Register R9 (Curser row) */
#define R9_CURSER_ROW_0					   0x00
#define R9_CURSER_ROW_1					   0x01
#define R9_CURSER_ROW_2					   0x02
#define R9_CURSER_ROW_25				   0x19

/* Register R10 (Curser column) */
#define R10_CURSER_COLUMN_0				   0x00
#define R10_CURSER_COLUMN_6				   0x06
#define R10_CURSER_COLUMN_8				   0x08

/*****************************************************************************/
/* Row 25 control data in column 0 to 9					     */
/*****************************************************************************/
#define ROW25_COLUMN0_PAGE_UNITS			   0x0F

#define ROW25_COLUMN1_PAGE_TENS				   0x0F

#define ROW25_COLUMN2_MINUTES_UNITS			   0x0F

#define ROW25_COLUMN3_MINUTES_TENS			   0x07
#define ROW25_COLUMN3_DELETE_PAGE			   0x08

#define ROW25_COLUMN4_HOUR_UNITS			   0x0F

#define ROW25_COLUMN5_HOUR_TENS				   0x03
#define ROW25_COLUMN5_INSERT_HEADLINE			   0x04
#define ROW25_COLUMN5_INSERT_SUBTITLE			   0x08

#define ROW25_COLUMN6_SUPPRESS_HEADER			   0x01
#define ROW25_COLUMN6_UPDATE_PAGE			   0x02
#define ROW25_COLUMN6_INTERRUPTED_SEQUENCE		   0x04
#define ROW25_COLUMN6_SUPPRESS_DISPLAY			   0x08

#define ROW25_COLUMN7_SERIAL_MODE			   0x01
#define ROW25_COLUMN7_CHARACTER_SET			   0x0E

#define ROW25_COLUMN8_PAGE_HUNDREDS			   0x07
#define ROW25_COLUMN8_PAGE_NOT_FOUND			   0x10

#define ROW25_COLUMN9_PAGE_BEING_LOOKED_FOR		   0x20

#define ROW25_COLUMN0_TO_7_HAMMING_ERROR		   0x10

/*****************************************************************************/
/* Helper macros for extracting page, hour and minute digits		     */
/*****************************************************************************/
/* BYTE_POS  0 is at row 0, column 0,
   BYTE_POS  1 is at row 0, column 1,
   BYTE_POS 40 is at row 1, column 0, (with NUM_ROWS_PER_PAGE = 40)
   BYTE_POS 41 is at row 1, column 1, (with NUM_ROWS_PER_PAGE = 40),
   ... */
#define ROW(BYTE_POS)    (BYTE_POS / NUM_ROWS_PER_PAGE)
#define COLUMN(BYTE_POS) (BYTE_POS % NUM_ROWS_PER_PAGE)

/*****************************************************************************/
/* Helper macros for extracting page, hour and minute digits		     */
/*****************************************************************************/
/* Macros for extracting hundreds, tens and units of a page number which
   must be in the range 0 ... 0x799.
   Note that page is coded in hexadecimal, i.e. 0x123 means page 123.
   page 0x.. means page 8.. */
#define HUNDREDS_OF_PAGE(page) (((page) / 0x100) & 0x7)
#define TENS_OF_PAGE(page)     (((page) / 0x10)  & 0xF)
#define UNITS_OF_PAGE(page)     ((page) & 0xF)

/* Macros for extracting tens and units of a hour information which
   must be in the range 0 ... 0x24.
   Note that hour is coded in hexadecimal, i.e. 0x12 means 12 hours */
#define TENS_OF_HOUR(hour)  ((hour) / 0x10)
#define UNITS_OF_HOUR(hour) ((hour) & 0xF)

/* Macros for extracting tens and units of a minute information which
   must be in the range 0 ... 0x59.
   Note that minute is coded in hexadecimal, i.e. 0x12 means 12 minutes */
#define TENS_OF_MINUTE(minute)  ((minute) / 0x10)
#define UNITS_OF_MINUTE(minute) ((minute) & 0xF)

#define HOUR_MAX   0x23
#define MINUTE_MAX 0x59
#define PAGE_MAX   0x8FF


struct saa5246a_device
{
	struct v4l2_subdev sd;
	struct video_device *vdev;
	u8     pgbuf[NUM_DAUS][VTX_VIRTUALSIZE];
	int    is_searching[NUM_DAUS];
	unsigned long in_use;
	struct mutex lock;
};

static inline struct saa5246a_device *to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct saa5246a_device, sd);
}

static struct video_device saa_template;	/* Declared near bottom */

/*
 *	I2C interfaces
 */

static int i2c_sendbuf(struct saa5246a_device *t, int reg, int count, u8 *data)
{
	struct i2c_client *client = v4l2_get_subdevdata(&t->sd);
	char buf[64];

	buf[0] = reg;
	memcpy(buf+1, data, count);

	if (i2c_master_send(client, buf, count + 1) == count + 1)
		return 0;
	return -1;
}

static int i2c_senddata(struct saa5246a_device *t, ...)
{
	unsigned char buf[64];
	int v;
	int ct = 0;
	va_list argp;
	va_start(argp, t);

	while ((v = va_arg(argp, int)) != -1)
		buf[ct++] = v;

	va_end(argp);
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
	struct i2c_client *client = v4l2_get_subdevdata(&t->sd);

	if (i2c_master_recv(client, buf, count) != count)
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
static long do_saa5246a_ioctl(struct file *file, unsigned int cmd, void *arg)
{
	struct saa5246a_device *t = video_drvdata(file);

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
static long saa5246a_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct saa5246a_device *t = video_drvdata(file);
	long err;

	cmd = vtx_fix_command(cmd);
	mutex_lock(&t->lock);
	err = video_usercopy(file, cmd, arg, do_saa5246a_ioctl);
	mutex_unlock(&t->lock);
	return err;
}

static int saa5246a_open(struct file *file)
{
	struct saa5246a_device *t = video_drvdata(file);

	if (test_and_set_bit(0, &t->in_use))
		return -EBUSY;

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
		clear_bit(0, &t->in_use);
		return -EIO;
	}
	return 0;
}

static int saa5246a_release(struct file *file)
{
	struct saa5246a_device *t = video_drvdata(file);

	/* Stop all acquisition circuits. */
	i2c_senddata(t, SAA5246A_REGISTER_R1,

		R1_INTERLACED_312_AND_HALF_312_AND_HALF_LINES |
		R1_DEW |
		R1_EXTENDED_PACKET_DISABLE |
		R1_DAUS_ALL_OFF |
		R1_8_BITS_NO_PARITY |
		R1_VCS_TO_SCS,

		COMMAND_END);
	clear_bit(0, &t->in_use);
	return 0;
}

static const struct v4l2_file_operations saa_fops = {
	.owner	 = THIS_MODULE,
	.open	 = saa5246a_open,
	.release = saa5246a_release,
	.ioctl	 = saa5246a_ioctl,
};

static struct video_device saa_template =
{
	.name	  = "saa5246a",
	.fops	  = &saa_fops,
	.release  = video_device_release,
};

static int saa5246a_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SAA5246A, 0);
}

static const struct v4l2_subdev_core_ops saa5246a_core_ops = {
	.g_chip_ident = saa5246a_g_chip_ident,
};

static const struct v4l2_subdev_ops saa5246a_ops = {
	.core = &saa5246a_core_ops,
};


static int saa5246a_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int pgbuf;
	int err;
	struct saa5246a_device *t;
	struct v4l2_subdev *sd;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
	v4l_info(client, "VideoText version %d.%d\n",
			MAJOR_VERSION, MINOR_VERSION);
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOMEM;
	sd = &t->sd;
	v4l2_i2c_subdev_init(sd, client, &saa5246a_ops);
	mutex_init(&t->lock);

	/* Now create a video4linux device */
	t->vdev = video_device_alloc();
	if (t->vdev == NULL) {
		kfree(t);
		return -ENOMEM;
	}
	memcpy(t->vdev, &saa_template, sizeof(*t->vdev));

	for (pgbuf = 0; pgbuf < NUM_DAUS; pgbuf++) {
		memset(t->pgbuf[pgbuf], ' ', sizeof(t->pgbuf[0]));
		t->is_searching[pgbuf] = false;
	}
	video_set_drvdata(t->vdev, t);

	/* Register it */
	err = video_register_device(t->vdev, VFL_TYPE_VTX, -1);
	if (err < 0) {
		video_device_release(t->vdev);
		kfree(t);
		return err;
	}
	return 0;
}

static int saa5246a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct saa5246a_device *t = to_dev(sd);

	video_unregister_device(t->vdev);
	v4l2_device_unregister_subdev(sd);
	kfree(t);
	return 0;
}

static const struct i2c_device_id saa5246a_id[] = {
	{ "saa5246a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa5246a_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "saa5246a",
	.probe = saa5246a_probe,
	.remove = saa5246a_remove,
	.id_table = saa5246a_id,
};
