/*
 * Zoran ZR36060 basic configuration functions
 *
 * Copyright (C) 2002 Laurent Pinchart <laurent.pinchart@skynet.be>
 *
 * $Id: zr36060.c,v 1.1.2.22 2003/05/06 09:35:36 rbultje Exp $
 *
 * ------------------------------------------------------------------------
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * ------------------------------------------------------------------------
 */

#define ZR060_VERSION "v0.7"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/types.h>
#include <linux/wait.h>

/* includes for structures and defines regarding video 
   #include<linux/videodev.h> */

/* I/O commands, error codes */
#include<asm/io.h>
//#include<errno.h>

/* headerfile of this module */
#include"zr36060.h"

/* codec io API */
#include"videocodec.h"

/* it doesn't make sense to have more than 20 or so,
  just to prevent some unwanted loops */
#define MAX_CODECS 20

/* amount of chips attached via this driver */
static int zr36060_codecs = 0;

static int low_bitrate = 0;
module_param(low_bitrate, bool, 0);
MODULE_PARM_DESC(low_bitrate, "Buz compatibility option, halves bitrate");

/* debugging is available via module parameter */
static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-4)");

#define dprintk(num, format, args...) \
	do { \
		if (debug >= num) \
			printk(format, ##args); \
	} while (0)

/* =========================================================================
   Local hardware I/O functions:

   read/write via codec layer (registers are located in the master device)
   ========================================================================= */

/* read and write functions */
static u8
zr36060_read (struct zr36060 *ptr,
	      u16             reg)
{
	u8 value = 0;

	// just in case something is wrong...
	if (ptr->codec->master_data->readreg)
		value = (ptr->codec->master_data->readreg(ptr->codec,
							  reg)) & 0xff;
	else
		dprintk(1,
			KERN_ERR "%s: invalid I/O setup, nothing read!\n",
			ptr->name);

	//dprintk(4, "%s: reading from 0x%04x: %02x\n",ptr->name,reg,value);

	return value;
}

static void
zr36060_write(struct zr36060 *ptr,
	      u16             reg,
	      u8              value)
{
	//dprintk(4, "%s: writing 0x%02x to 0x%04x\n",ptr->name,value,reg);
	dprintk(4, "0x%02x @0x%04x\n", value, reg);

	// just in case something is wrong...
	if (ptr->codec->master_data->writereg)
		ptr->codec->master_data->writereg(ptr->codec, reg, value);
	else
		dprintk(1,
			KERN_ERR
			"%s: invalid I/O setup, nothing written!\n",
			ptr->name);
}

/* =========================================================================
   Local helper function:

   status read
   ========================================================================= */

/* status is kept in datastructure */
static u8
zr36060_read_status (struct zr36060 *ptr)
{
	ptr->status = zr36060_read(ptr, ZR060_CFSR);

	zr36060_read(ptr, 0);
	return ptr->status;
}

/* =========================================================================
   Local helper function:

   scale factor read
   ========================================================================= */

/* scale factor is kept in datastructure */
static u16
zr36060_read_scalefactor (struct zr36060 *ptr)
{
	ptr->scalefact = (zr36060_read(ptr, ZR060_SF_HI) << 8) |
			 (zr36060_read(ptr, ZR060_SF_LO) & 0xFF);

	/* leave 0 selected for an eventually GO from master */
	zr36060_read(ptr, 0);
	return ptr->scalefact;
}

/* =========================================================================
   Local helper function:

   wait if codec is ready to proceed (end of processing) or time is over
   ========================================================================= */

static void
zr36060_wait_end (struct zr36060 *ptr)
{
	int i = 0;

	while (zr36060_read_status(ptr) & ZR060_CFSR_Busy) {
		udelay(1);
		if (i++ > 200000) {	// 200ms, there is for shure something wrong!!!
			dprintk(1,
				"%s: timout at wait_end (last status: 0x%02x)\n",
				ptr->name, ptr->status);
			break;
		}
	}
}

/* =========================================================================
   Local helper function:

   basic test of "connectivity", writes/reads to/from memory the SOF marker 
   ========================================================================= */

static int
zr36060_basic_test (struct zr36060 *ptr)
{
	if ((zr36060_read(ptr, ZR060_IDR_DEV) != 0x33) &&
	    (zr36060_read(ptr, ZR060_IDR_REV) != 0x01)) {
		dprintk(1,
			KERN_ERR
			"%s: attach failed, can't connect to jpeg processor!\n",
			ptr->name);
		return -ENXIO;
	}

	zr36060_wait_end(ptr);
	if (ptr->status & ZR060_CFSR_Busy) {
		dprintk(1,
			KERN_ERR
			"%s: attach failed, jpeg processor failed (end flag)!\n",
			ptr->name);
		return -EBUSY;
	}

	return 0;		/* looks good! */
}

/* =========================================================================
   Local helper function:

   simple loop for pushing the init datasets
   ========================================================================= */

static int
zr36060_pushit (struct zr36060 *ptr,
	        u16             startreg,
	        u16             len,
	        const char     *data)
{
	int i = 0;

	dprintk(4, "%s: write data block to 0x%04x (len=%d)\n", ptr->name,
		startreg, len);
	while (i < len) {
		zr36060_write(ptr, startreg++, data[i++]);
	}

	return i;
}

/* =========================================================================
   Basic datasets:

   jpeg baseline setup data (you find it on lots places in internet, or just
   extract it from any regular .jpg image...)

   Could be variable, but until it's not needed it they are just fixed to save
   memory. Otherwise expand zr36060 structure with arrays, push the values to
   it and initalize from there, as e.g. the linux zr36057/60 driver does it.
   ========================================================================= */

static const char zr36060_dqt[0x86] = {
	0xff, 0xdb,		//Marker: DQT
	0x00, 0x84,		//Length: 2*65+2
	0x00,			//Pq,Tq first table
	0x10, 0x0b, 0x0c, 0x0e, 0x0c, 0x0a, 0x10, 0x0e,
	0x0d, 0x0e, 0x12, 0x11, 0x10, 0x13, 0x18, 0x28,
	0x1a, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23, 0x25,
	0x1d, 0x28, 0x3a, 0x33, 0x3d, 0x3c, 0x39, 0x33,
	0x38, 0x37, 0x40, 0x48, 0x5c, 0x4e, 0x40, 0x44,
	0x57, 0x45, 0x37, 0x38, 0x50, 0x6d, 0x51, 0x57,
	0x5f, 0x62, 0x67, 0x68, 0x67, 0x3e, 0x4d, 0x71,
	0x79, 0x70, 0x64, 0x78, 0x5c, 0x65, 0x67, 0x63,
	0x01,			//Pq,Tq second table
	0x11, 0x12, 0x12, 0x18, 0x15, 0x18, 0x2f, 0x1a,
	0x1a, 0x2f, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63
};

static const char zr36060_dht[0x1a4] = {
	0xff, 0xc4,		//Marker: DHT
	0x01, 0xa2,		//Length: 2*AC, 2*DC
	0x00,			//DC first table
	0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
	0x01,			//DC second table
	0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
	0x10,			//AC first table
	0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
	0x05, 0x05, 0x04, 0x04, 0x00, 0x00,
	0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11,
	0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
	0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1,
	0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24,
	0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34,
	0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56,
	0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66,
	0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
	0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
	0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
	0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9,
	0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
	0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
	0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
	0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
	0xF8, 0xF9, 0xFA,
	0x11,			//AC second table
	0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
	0x07, 0x05, 0x04, 0x04, 0x00, 0x01,
	0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
	0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62,
	0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25,
	0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A,
	0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56,
	0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66,
	0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
	0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
	0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
	0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
	0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
	0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
	0xF9, 0xFA
};

/* jpeg baseline setup, this is just fixed in this driver (YUV pictures) */
#define NO_OF_COMPONENTS          0x3	//Y,U,V
#define BASELINE_PRECISION        0x8	//MCU size (?)
static const char zr36060_tq[8] = { 0, 1, 1, 0, 0, 0, 0, 0 };	//table idx's QT
static const char zr36060_td[8] = { 0, 1, 1, 0, 0, 0, 0, 0 };	//table idx's DC
static const char zr36060_ta[8] = { 0, 1, 1, 0, 0, 0, 0, 0 };	//table idx's AC

/* horizontal 422 decimation setup (maybe we support 411 or so later, too) */
static const char zr36060_decimation_h[8] = { 2, 1, 1, 0, 0, 0, 0, 0 };
static const char zr36060_decimation_v[8] = { 1, 1, 1, 0, 0, 0, 0, 0 };

/* =========================================================================
   Local helper functions:

   calculation and setup of parameter-dependent JPEG baseline segments
   (needed for compression only)
   ========================================================================= */

/* ------------------------------------------------------------------------- */

/* SOF (start of frame) segment depends on width, height and sampling ratio
                         of each color component */

static int
zr36060_set_sof (struct zr36060 *ptr)
{
	char sof_data[34];	// max. size of register set
	int i;

	dprintk(3, "%s: write SOF (%dx%d, %d components)\n", ptr->name,
		ptr->width, ptr->height, NO_OF_COMPONENTS);
	sof_data[0] = 0xff;
	sof_data[1] = 0xc0;
	sof_data[2] = 0x00;
	sof_data[3] = (3 * NO_OF_COMPONENTS) + 8;
	sof_data[4] = BASELINE_PRECISION;	// only '8' possible with zr36060
	sof_data[5] = (ptr->height) >> 8;
	sof_data[6] = (ptr->height) & 0xff;
	sof_data[7] = (ptr->width) >> 8;
	sof_data[8] = (ptr->width) & 0xff;
	sof_data[9] = NO_OF_COMPONENTS;
	for (i = 0; i < NO_OF_COMPONENTS; i++) {
		sof_data[10 + (i * 3)] = i;	// index identifier
		sof_data[11 + (i * 3)] = (ptr->h_samp_ratio[i] << 4) |
					 (ptr->v_samp_ratio[i]); // sampling ratios
		sof_data[12 + (i * 3)] = zr36060_tq[i];	// Q table selection
	}
	return zr36060_pushit(ptr, ZR060_SOF_IDX,
			      (3 * NO_OF_COMPONENTS) + 10, sof_data);
}

/* ------------------------------------------------------------------------- */

/* SOS (start of scan) segment depends on the used scan components 
                        of each color component */

static int
zr36060_set_sos (struct zr36060 *ptr)
{
	char sos_data[16];	// max. size of register set
	int i;

	dprintk(3, "%s: write SOS\n", ptr->name);
	sos_data[0] = 0xff;
	sos_data[1] = 0xda;
	sos_data[2] = 0x00;
	sos_data[3] = 2 + 1 + (2 * NO_OF_COMPONENTS) + 3;
	sos_data[4] = NO_OF_COMPONENTS;
	for (i = 0; i < NO_OF_COMPONENTS; i++) {
		sos_data[5 + (i * 2)] = i;	// index
		sos_data[6 + (i * 2)] = (zr36060_td[i] << 4) |
				        zr36060_ta[i]; // AC/DC tbl.sel.
	}
	sos_data[2 + 1 + (2 * NO_OF_COMPONENTS) + 2] = 00;	// scan start
	sos_data[2 + 1 + (2 * NO_OF_COMPONENTS) + 3] = 0x3f;
	sos_data[2 + 1 + (2 * NO_OF_COMPONENTS) + 4] = 00;
	return zr36060_pushit(ptr, ZR060_SOS_IDX,
			      4 + 1 + (2 * NO_OF_COMPONENTS) + 3,
			      sos_data);
}

/* ------------------------------------------------------------------------- */

/* DRI (define restart interval) */

static int
zr36060_set_dri (struct zr36060 *ptr)
{
	char dri_data[6];	// max. size of register set

	dprintk(3, "%s: write DRI\n", ptr->name);
	dri_data[0] = 0xff;
	dri_data[1] = 0xdd;
	dri_data[2] = 0x00;
	dri_data[3] = 0x04;
	dri_data[4] = (ptr->dri) >> 8;
	dri_data[5] = (ptr->dri) & 0xff;
	return zr36060_pushit(ptr, ZR060_DRI_IDX, 6, dri_data);
}

/* =========================================================================
   Setup function:

   Setup compression/decompression of Zoran's JPEG processor
   ( see also zoran 36060 manual )

   ... sorry for the spaghetti code ...
   ========================================================================= */
static void
zr36060_init (struct zr36060 *ptr)
{
	int sum = 0;
	long bitcnt, tmp;

	if (ptr->mode == CODEC_DO_COMPRESSION) {
		dprintk(2, "%s: COMPRESSION SETUP\n", ptr->name);

		zr36060_write(ptr, ZR060_LOAD, ZR060_LOAD_SyncRst);

		/* 060 communicates with 067 in master mode */
		zr36060_write(ptr, ZR060_CIR, ZR060_CIR_CodeMstr);

		/* Compression with or without variable scale factor */
		/*FIXME: What about ptr->bitrate_ctrl? */
		zr36060_write(ptr, ZR060_CMR,
			      ZR060_CMR_Comp | ZR060_CMR_Pass2 |
			      ZR060_CMR_BRB);

		/* Must be zero */
		zr36060_write(ptr, ZR060_MBZ, 0x00);
		zr36060_write(ptr, ZR060_TCR_HI, 0x00);
		zr36060_write(ptr, ZR060_TCR_LO, 0x00);

		/* Disable all IRQs - no DataErr means autoreset */
		zr36060_write(ptr, ZR060_IMR, 0);

		/* volume control settings */
		zr36060_write(ptr, ZR060_SF_HI, ptr->scalefact >> 8);
		zr36060_write(ptr, ZR060_SF_LO, ptr->scalefact & 0xff);

		zr36060_write(ptr, ZR060_AF_HI, 0xff);
		zr36060_write(ptr, ZR060_AF_M, 0xff);
		zr36060_write(ptr, ZR060_AF_LO, 0xff);

		/* setup the variable jpeg tables */
		sum += zr36060_set_sof(ptr);
		sum += zr36060_set_sos(ptr);
		sum += zr36060_set_dri(ptr);

		/* setup the fixed jpeg tables - maybe variable, though -
		 * (see table init section above) */
		sum +=
		    zr36060_pushit(ptr, ZR060_DQT_IDX, sizeof(zr36060_dqt),
				   zr36060_dqt);
		sum +=
		    zr36060_pushit(ptr, ZR060_DHT_IDX, sizeof(zr36060_dht),
				   zr36060_dht);
		zr36060_write(ptr, ZR060_APP_IDX, 0xff);
		zr36060_write(ptr, ZR060_APP_IDX + 1, 0xe0 + ptr->app.appn);
		zr36060_write(ptr, ZR060_APP_IDX + 2, 0x00);
		zr36060_write(ptr, ZR060_APP_IDX + 3, ptr->app.len + 2);
		sum += zr36060_pushit(ptr, ZR060_APP_IDX + 4, 60,
				      ptr->app.data) + 4;
		zr36060_write(ptr, ZR060_COM_IDX, 0xff);
		zr36060_write(ptr, ZR060_COM_IDX + 1, 0xfe);
		zr36060_write(ptr, ZR060_COM_IDX + 2, 0x00);
		zr36060_write(ptr, ZR060_COM_IDX + 3, ptr->com.len + 2);
		sum += zr36060_pushit(ptr, ZR060_COM_IDX + 4, 60,
				      ptr->com.data) + 4;

		/* setup misc. data for compression (target code sizes) */

		/* size of compressed code to reach without header data */
		sum = ptr->real_code_vol - sum;
		bitcnt = sum << 3;	/* need the size in bits */

		tmp = bitcnt >> 16;
		dprintk(3,
			"%s: code: csize=%d, tot=%d, bit=%ld, highbits=%ld\n",
			ptr->name, sum, ptr->real_code_vol, bitcnt, tmp);
		zr36060_write(ptr, ZR060_TCV_NET_HI, tmp >> 8);
		zr36060_write(ptr, ZR060_TCV_NET_MH, tmp & 0xff);
		tmp = bitcnt & 0xffff;
		zr36060_write(ptr, ZR060_TCV_NET_ML, tmp >> 8);
		zr36060_write(ptr, ZR060_TCV_NET_LO, tmp & 0xff);

		bitcnt -= bitcnt >> 7;	// bits without stuffing
		bitcnt -= ((bitcnt * 5) >> 6);	// bits without eob

		tmp = bitcnt >> 16;
		dprintk(3, "%s: code: nettobit=%ld, highnettobits=%ld\n",
			ptr->name, bitcnt, tmp);
		zr36060_write(ptr, ZR060_TCV_DATA_HI, tmp >> 8);
		zr36060_write(ptr, ZR060_TCV_DATA_MH, tmp & 0xff);
		tmp = bitcnt & 0xffff;
		zr36060_write(ptr, ZR060_TCV_DATA_ML, tmp >> 8);
		zr36060_write(ptr, ZR060_TCV_DATA_LO, tmp & 0xff);

		/* JPEG markers to be included in the compressed stream */
		zr36060_write(ptr, ZR060_MER,
			      ZR060_MER_DQT | ZR060_MER_DHT |
			      ((ptr->com.len > 0) ? ZR060_MER_Com : 0) |
			      ((ptr->app.len > 0) ? ZR060_MER_App : 0));

		/* Setup the Video Frontend */
		/* Limit pixel range to 16..235 as per CCIR-601 */
		zr36060_write(ptr, ZR060_VCR, ZR060_VCR_Range);

	} else {
		dprintk(2, "%s: EXPANSION SETUP\n", ptr->name);

		zr36060_write(ptr, ZR060_LOAD, ZR060_LOAD_SyncRst);

		/* 060 communicates with 067 in master mode */
		zr36060_write(ptr, ZR060_CIR, ZR060_CIR_CodeMstr);

		/* Decompression */
		zr36060_write(ptr, ZR060_CMR, 0);

		/* Must be zero */
		zr36060_write(ptr, ZR060_MBZ, 0x00);
		zr36060_write(ptr, ZR060_TCR_HI, 0x00);
		zr36060_write(ptr, ZR060_TCR_LO, 0x00);

		/* Disable all IRQs - no DataErr means autoreset */
		zr36060_write(ptr, ZR060_IMR, 0);

		/* setup misc. data for expansion */
		zr36060_write(ptr, ZR060_MER, 0);

		/* setup the fixed jpeg tables - maybe variable, though -
		 * (see table init section above) */
		zr36060_pushit(ptr, ZR060_DHT_IDX, sizeof(zr36060_dht),
			       zr36060_dht);

		/* Setup the Video Frontend */
		//zr36060_write(ptr, ZR060_VCR, ZR060_VCR_FIExt);
		//this doesn't seem right and doesn't work...
		zr36060_write(ptr, ZR060_VCR, ZR060_VCR_Range);
	}

	/* Load the tables */
	zr36060_write(ptr, ZR060_LOAD,
		      ZR060_LOAD_SyncRst | ZR060_LOAD_Load);
	zr36060_wait_end(ptr);
	dprintk(2, "%s: Status after table preload: 0x%02x\n", ptr->name,
		ptr->status);

	if (ptr->status & ZR060_CFSR_Busy) {
		dprintk(1, KERN_ERR "%s: init aborted!\n", ptr->name);
		return;		// something is wrong, its timed out!!!!
	}
}

/* =========================================================================
   CODEC API FUNCTIONS

   this functions are accessed by the master via the API structure
   ========================================================================= */

/* set compression/expansion mode and launches codec -
   this should be the last call from the master before starting processing */
static int
zr36060_set_mode (struct videocodec *codec,
		  int                mode)
{
	struct zr36060 *ptr = (struct zr36060 *) codec->data;

	dprintk(2, "%s: set_mode %d call\n", ptr->name, mode);

	if ((mode != CODEC_DO_EXPANSION) && (mode != CODEC_DO_COMPRESSION))
		return -EINVAL;

	ptr->mode = mode;
	zr36060_init(ptr);

	return 0;
}

/* set picture size (norm is ignored as the codec doesn't know about it) */
static int
zr36060_set_video (struct videocodec   *codec,
		   struct tvnorm       *norm,
		   struct vfe_settings *cap,
		   struct vfe_polarity *pol)
{
	struct zr36060 *ptr = (struct zr36060 *) codec->data;
	u32 reg;
	int size;

	dprintk(2, "%s: set_video %d/%d-%dx%d (%%%d) call\n", ptr->name,
		cap->x, cap->y, cap->width, cap->height, cap->decimation);

	/* if () return -EINVAL;
	 * trust the master driver that it knows what it does - so
	 * we allow invalid startx/y and norm for now ... */
	ptr->width = cap->width / (cap->decimation & 0xff);
	ptr->height = cap->height / (cap->decimation >> 8);

	zr36060_write(ptr, ZR060_LOAD, ZR060_LOAD_SyncRst);

	/* Note that VSPol/HSPol bits in zr36060 have the opposite
	 * meaning of their zr360x7 counterparts with the same names
	 * N.b. for VSPol this is only true if FIVEdge = 0 (default,
	 * left unchanged here - in accordance with datasheet).
	*/
	reg = (!pol->vsync_pol ? ZR060_VPR_VSPol : 0)
	    | (!pol->hsync_pol ? ZR060_VPR_HSPol : 0)
	    | (pol->field_pol ? ZR060_VPR_FIPol : 0)
	    | (pol->blank_pol ? ZR060_VPR_BLPol : 0)
	    | (pol->subimg_pol ? ZR060_VPR_SImgPol : 0)
	    | (pol->poe_pol ? ZR060_VPR_PoePol : 0)
	    | (pol->pvalid_pol ? ZR060_VPR_PValPol : 0)
	    | (pol->vclk_pol ? ZR060_VPR_VCLKPol : 0);
	zr36060_write(ptr, ZR060_VPR, reg);

	reg = 0;
	switch (cap->decimation & 0xff) {
	default:
	case 1:
		break;

	case 2:
		reg |= ZR060_SR_HScale2;
		break;

	case 4:
		reg |= ZR060_SR_HScale4;
		break;
	}

	switch (cap->decimation >> 8) {
	default:
	case 1:
		break;

	case 2:
		reg |= ZR060_SR_VScale;
		break;
	}
	zr36060_write(ptr, ZR060_SR, reg);

	zr36060_write(ptr, ZR060_BCR_Y, 0x00);
	zr36060_write(ptr, ZR060_BCR_U, 0x80);
	zr36060_write(ptr, ZR060_BCR_V, 0x80);

	/* sync generator */

	reg = norm->Ht - 1;	/* Vtotal */
	zr36060_write(ptr, ZR060_SGR_VTOTAL_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_SGR_VTOTAL_LO, (reg >> 0) & 0xff);

	reg = norm->Wt - 1;	/* Htotal */
	zr36060_write(ptr, ZR060_SGR_HTOTAL_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_SGR_HTOTAL_LO, (reg >> 0) & 0xff);

	reg = 6 - 1;		/* VsyncSize */
	zr36060_write(ptr, ZR060_SGR_VSYNC, reg);

	//reg   = 30 - 1;               /* HsyncSize */
///*CP*/        reg = (zr->params.norm == 1 ? 57 : 68);
	reg = 68;
	zr36060_write(ptr, ZR060_SGR_HSYNC, reg);

	reg = norm->VStart - 1;	/* BVstart */
	zr36060_write(ptr, ZR060_SGR_BVSTART, reg);

	reg += norm->Ha / 2;	/* BVend */
	zr36060_write(ptr, ZR060_SGR_BVEND_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_SGR_BVEND_LO, (reg >> 0) & 0xff);

	reg = norm->HStart - 1;	/* BHstart */
	zr36060_write(ptr, ZR060_SGR_BHSTART, reg);

	reg += norm->Wa;	/* BHend */
	zr36060_write(ptr, ZR060_SGR_BHEND_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_SGR_BHEND_LO, (reg >> 0) & 0xff);

	/* active area */
	reg = cap->y + norm->VStart;	/* Vstart */
	zr36060_write(ptr, ZR060_AAR_VSTART_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_AAR_VSTART_LO, (reg >> 0) & 0xff);

	reg += cap->height;	/* Vend */
	zr36060_write(ptr, ZR060_AAR_VEND_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_AAR_VEND_LO, (reg >> 0) & 0xff);

	reg = cap->x + norm->HStart;	/* Hstart */
	zr36060_write(ptr, ZR060_AAR_HSTART_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_AAR_HSTART_LO, (reg >> 0) & 0xff);

	reg += cap->width;	/* Hend */
	zr36060_write(ptr, ZR060_AAR_HEND_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_AAR_HEND_LO, (reg >> 0) & 0xff);

	/* subimage area */
	reg = norm->VStart - 4;	/* SVstart */
	zr36060_write(ptr, ZR060_SWR_VSTART_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_SWR_VSTART_LO, (reg >> 0) & 0xff);

	reg += norm->Ha / 2 + 8;	/* SVend */
	zr36060_write(ptr, ZR060_SWR_VEND_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_SWR_VEND_LO, (reg >> 0) & 0xff);

	reg = norm->HStart /*+ 64 */  - 4;	/* SHstart */
	zr36060_write(ptr, ZR060_SWR_HSTART_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_SWR_HSTART_LO, (reg >> 0) & 0xff);

	reg += norm->Wa + 8;	/* SHend */
	zr36060_write(ptr, ZR060_SWR_HEND_HI, (reg >> 8) & 0xff);
	zr36060_write(ptr, ZR060_SWR_HEND_LO, (reg >> 0) & 0xff);

	size = ptr->width * ptr->height;
	/* Target compressed field size in bits: */
	size = size * 16;	/* uncompressed size in bits */
	/* (Ronald) by default, quality = 100 is a compression
	 * ratio 1:2. Setting low_bitrate (insmod option) sets
	 * it to 1:4 (instead of 1:2, zr36060 max) as limit because the
	 * buz can't handle more at decimation=1... Use low_bitrate if
	 * you have a Buz, unless you know what you're doing */
	size = size * cap->quality / (low_bitrate ? 400 : 200);
	/* Lower limit (arbitrary, 1 KB) */
	if (size < 8192)
		size = 8192;
	/* Upper limit: 7/8 of the code buffers */
	if (size > ptr->total_code_vol * 7)
		size = ptr->total_code_vol * 7;

	ptr->real_code_vol = size >> 3;	/* in bytes */

	/* the MBCVR is the *maximum* block volume, according to the
	 * JPEG ISO specs, this shouldn't be used, since that allows
	 * for the best encoding quality. So set it to it's max value */
	reg = ptr->max_block_vol;
	zr36060_write(ptr, ZR060_MBCVR, reg);

	return 0;
}

/* additional control functions */
static int
zr36060_control (struct videocodec *codec,
		 int                type,
		 int                size,
		 void              *data)
{
	struct zr36060 *ptr = (struct zr36060 *) codec->data;
	int *ival = (int *) data;

	dprintk(2, "%s: control %d call with %d byte\n", ptr->name, type,
		size);

	switch (type) {
	case CODEC_G_STATUS:	/* get last status */
		if (size != sizeof(int))
			return -EFAULT;
		zr36060_read_status(ptr);
		*ival = ptr->status;
		break;

	case CODEC_G_CODEC_MODE:
		if (size != sizeof(int))
			return -EFAULT;
		*ival = CODEC_MODE_BJPG;
		break;

	case CODEC_S_CODEC_MODE:
		if (size != sizeof(int))
			return -EFAULT;
		if (*ival != CODEC_MODE_BJPG)
			return -EINVAL;
		/* not needed, do nothing */
		return 0;

	case CODEC_G_VFE:
	case CODEC_S_VFE:
		/* not needed, do nothing */
		return 0;

	case CODEC_S_MMAP:
		/* not available, give an error */
		return -ENXIO;

	case CODEC_G_JPEG_TDS_BYTE:	/* get target volume in byte */
		if (size != sizeof(int))
			return -EFAULT;
		*ival = ptr->total_code_vol;
		break;

	case CODEC_S_JPEG_TDS_BYTE:	/* get target volume in byte */
		if (size != sizeof(int))
			return -EFAULT;
		ptr->total_code_vol = *ival;
		ptr->real_code_vol = (ptr->total_code_vol * 6) >> 3;
		break;

	case CODEC_G_JPEG_SCALE:	/* get scaling factor */
		if (size != sizeof(int))
			return -EFAULT;
		*ival = zr36060_read_scalefactor(ptr);
		break;

	case CODEC_S_JPEG_SCALE:	/* set scaling factor */
		if (size != sizeof(int))
			return -EFAULT;
		ptr->scalefact = *ival;
		break;

	case CODEC_G_JPEG_APP_DATA: {	/* get appn marker data */
		struct jpeg_app_marker *app = data;

		if (size != sizeof(struct jpeg_app_marker))
			return -EFAULT;

		*app = ptr->app;
		break;
	}

	case CODEC_S_JPEG_APP_DATA: {	/* set appn marker data */
		struct jpeg_app_marker *app = data;

		if (size != sizeof(struct jpeg_app_marker))
			return -EFAULT;

		ptr->app = *app;
		break;
	}

	case CODEC_G_JPEG_COM_DATA: {	/* get comment marker data */
		struct jpeg_com_marker *com = data;

		if (size != sizeof(struct jpeg_com_marker))
			return -EFAULT;

		*com = ptr->com;
		break;
	}

	case CODEC_S_JPEG_COM_DATA: {	/* set comment marker data */
		struct jpeg_com_marker *com = data;

		if (size != sizeof(struct jpeg_com_marker))
			return -EFAULT;

		ptr->com = *com;
		break;
	}

	default:
		return -EINVAL;
	}

	return size;
}

/* =========================================================================
   Exit and unregister function:

   Deinitializes Zoran's JPEG processor
   ========================================================================= */

static int
zr36060_unset (struct videocodec *codec)
{
	struct zr36060 *ptr = codec->data;

	if (ptr) {
		/* do wee need some codec deinit here, too ???? */

		dprintk(1, "%s: finished codec #%d\n", ptr->name,
			ptr->num);
		kfree(ptr);
		codec->data = NULL;

		zr36060_codecs--;
		return 0;
	}

	return -EFAULT;
}

/* =========================================================================
   Setup and registry function:

   Initializes Zoran's JPEG processor

   Also sets pixel size, average code size, mode (compr./decompr.)
   (the given size is determined by the processor with the video interface)
   ========================================================================= */

static int
zr36060_setup (struct videocodec *codec)
{
	struct zr36060 *ptr;
	int res;

	dprintk(2, "zr36060: initializing MJPEG subsystem #%d.\n",
		zr36060_codecs);

	if (zr36060_codecs == MAX_CODECS) {
		dprintk(1,
			KERN_ERR "zr36060: Can't attach more codecs!\n");
		return -ENOSPC;
	}
	//mem structure init
	codec->data = ptr = kzalloc(sizeof(struct zr36060), GFP_KERNEL);
	if (NULL == ptr) {
		dprintk(1, KERN_ERR "zr36060: Can't get enough memory!\n");
		return -ENOMEM;
	}

	snprintf(ptr->name, sizeof(ptr->name), "zr36060[%d]",
		 zr36060_codecs);
	ptr->num = zr36060_codecs++;
	ptr->codec = codec;

	//testing
	res = zr36060_basic_test(ptr);
	if (res < 0) {
		zr36060_unset(codec);
		return res;
	}
	//final setup
	memcpy(ptr->h_samp_ratio, zr36060_decimation_h, 8);
	memcpy(ptr->v_samp_ratio, zr36060_decimation_v, 8);

	ptr->bitrate_ctrl = 0;	/* 0 or 1 - fixed file size flag
				 * (what is the difference?) */
	ptr->mode = CODEC_DO_COMPRESSION;
	ptr->width = 384;
	ptr->height = 288;
	ptr->total_code_vol = 16000;	/* CHECKME */
	ptr->real_code_vol = (ptr->total_code_vol * 6) >> 3;
	ptr->max_block_vol = 240;	/* CHECKME, was 120 is 240 */
	ptr->scalefact = 0x100;
	ptr->dri = 1;		/* CHECKME, was 8 is 1 */

	/* by default, no COM or APP markers - app should set those */
	ptr->com.len = 0;
	ptr->app.appn = 0;
	ptr->app.len = 0;

	zr36060_init(ptr);

	dprintk(1, KERN_INFO "%s: codec attached and running\n",
		ptr->name);

	return 0;
}

static const struct videocodec zr36060_codec = {
	.owner = THIS_MODULE,
	.name = "zr36060",
	.magic = 0L,		// magic not used
	.flags =
	    CODEC_FLAG_JPEG | CODEC_FLAG_HARDWARE | CODEC_FLAG_ENCODER |
	    CODEC_FLAG_DECODER | CODEC_FLAG_VFE,
	.type = CODEC_TYPE_ZR36060,
	.setup = zr36060_setup,	// functionality
	.unset = zr36060_unset,
	.set_mode = zr36060_set_mode,
	.set_video = zr36060_set_video,
	.control = zr36060_control,
	// others are not used
};

/* =========================================================================
   HOOK IN DRIVER AS KERNEL MODULE
   ========================================================================= */

static int __init
zr36060_init_module (void)
{
	//dprintk(1, "zr36060 driver %s\n",ZR060_VERSION);
	zr36060_codecs = 0;
	return videocodec_register(&zr36060_codec);
}

static void __exit
zr36060_cleanup_module (void)
{
	if (zr36060_codecs) {
		dprintk(1,
			"zr36060: something's wrong - %d codecs left somehow.\n",
			zr36060_codecs);
	} 

	/* however, we can't just stay alive */
	videocodec_unregister(&zr36060_codec);
}

module_init(zr36060_init_module);
module_exit(zr36060_cleanup_module);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@skynet.be>");
MODULE_DESCRIPTION("Driver module for ZR36060 jpeg processors "
		   ZR060_VERSION);
MODULE_LICENSE("GPL");
