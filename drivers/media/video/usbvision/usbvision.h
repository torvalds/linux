/* 
 * USBVISION.H
 *  usbvision header file
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *
 * This module is part of usbvision driver project.
 * Updates to driver completed by Dwaine P. Garden
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
 */


#ifndef __LINUX_USBVISION_H
#define __LINUX_USBVISION_H

#include <linux/list.h>

#ifndef VID_HARDWARE_USBVISION
	#define VID_HARDWARE_USBVISION 34   /* USBVision Video Grabber */
#endif

#define USBVISION_PWR_REG		0x00
	#define USBVISION_SSPND_EN		(1 << 1)
	#define USBVISION_RES2			(1 << 2)
	#define USBVISION_PWR_VID		(1 << 5)
        #define USBVISION_E2_EN			(1 << 7)
#define USBVISION_CONFIG_REG		0x01
#define USBVISION_ADRS_REG		0x02
#define USBVISION_ALTER_REG		0x03
#define USBVISION_FORCE_ALTER_REG	0x04
#define USBVISION_STATUS_REG		0x05
#define USBVISION_IOPIN_REG		0x06
	#define USBVISION_IO_1			(1 << 0)
	#define USBVISION_IO_2			(1 << 1)
	#define USBVISION_AUDIO_IN		0
	#define USBVISION_AUDIO_TV		1
	#define USBVISION_AUDIO_RADIO		2
	#define USBVISION_AUDIO_MUTE		3
#define USBVISION_SER_MODE		0x07
#define USBVISION_SER_ADRS		0x08
#define USBVISION_SER_CONT		0x09
#define USBVISION_SER_DAT1		0x0A
#define USBVISION_SER_DAT2		0x0B
#define USBVISION_SER_DAT3		0x0C
#define USBVISION_SER_DAT4		0x0D
#define USBVISION_EE_DATA		0x0E
#define USBVISION_EE_LSBAD		0x0F
#define USBVISION_EE_CONT		0x10
#define USBVISION_DRM_CONT			0x12
	#define USBVISION_REF			(1 << 0)
	#define USBVISION_RES_UR		(1 << 2)
	#define USBVISION_RES_FDL		(1 << 3)
	#define USBVISION_RES_VDW		(1 << 4)
#define USBVISION_DRM_PRM1		0x13
#define USBVISION_DRM_PRM2		0x14
#define USBVISION_DRM_PRM3		0x15
#define USBVISION_DRM_PRM4		0x16
#define USBVISION_DRM_PRM5		0x17
#define USBVISION_DRM_PRM6		0x18
#define USBVISION_DRM_PRM7		0x19
#define USBVISION_DRM_PRM8		0x1A
#define USBVISION_VIN_REG1		0x1B
	#define USBVISION_8_422_SYNC		0x01
	#define USBVISION_16_422_SYNC		0x02
	#define USBVISION_VSNC_POL		(1 << 3)
	#define USBVISION_HSNC_POL		(1 << 4)
	#define USBVISION_FID_POL		(1 << 5)
	#define USBVISION_HVALID_PO		(1 << 6)
	#define USBVISION_VCLK_POL		(1 << 7)
#define USBVISION_VIN_REG2		0x1C
	#define USBVISION_AUTO_FID		(1 << 0)
	#define USBVISION_NONE_INTER		(1 << 1)
	#define USBVISION_NOHVALID		(1 << 2)
	#define USBVISION_UV_ID			(1 << 3)
	#define USBVISION_FIX_2C		(1 << 4)
	#define USBVISION_SEND_FID		(1 << 5)
	#define USBVISION_KEEP_BLANK		(1 << 7)
#define USBVISION_LXSIZE_I		0x1D
#define USBVISION_MXSIZE_I		0x1E
#define USBVISION_LYSIZE_I		0x1F
#define USBVISION_MYSIZE_I		0x20
#define USBVISION_LX_OFFST		0x21
#define USBVISION_MX_OFFST		0x22
#define USBVISION_LY_OFFST		0x23
#define USBVISION_MY_OFFST		0x24
#define USBVISION_FRM_RATE		0x25
#define USBVISION_LXSIZE_O		0x26
#define USBVISION_MXSIZE_O		0x27
#define USBVISION_LYSIZE_O		0x28
#define USBVISION_MYSIZE_O		0x29
#define USBVISION_FILT_CONT		0x2A
#define USBVISION_VO_MODE		0x2B
#define USBVISION_INTRA_CYC		0x2C
#define USBVISION_STRIP_SZ		0x2D
#define USBVISION_FORCE_INTRA		0x2E
#define USBVISION_FORCE_UP		0x2F
#define USBVISION_BUF_THR		0x30
#define USBVISION_DVI_YUV		0x31
#define USBVISION_AUDIO_CONT		0x32
#define USBVISION_AUD_PK_LEN		0x33
#define USBVISION_BLK_PK_LEN		0x34
#define USBVISION_PCM_THR1		0x38
#define USBVISION_PCM_THR2		0x39
#define USBVISION_DIST_THR_L		0x3A
#define USBVISION_DIST_THR_H		0x3B
#define USBVISION_MAX_DIST_L		0x3C
#define USBVISION_MAX_DIST_H		0x3D
#define USBVISION_OP_CODE		0x33

#define MAX_BYTES_PER_PIXEL		4

#define MIN_FRAME_WIDTH			64
#define MAX_USB_WIDTH			320  //384
#define MAX_FRAME_WIDTH			320  //384			/*streching sometimes causes crashes*/

#define MIN_FRAME_HEIGHT		48
#define MAX_USB_HEIGHT			240  //288
#define MAX_FRAME_HEIGHT		240  //288			/*Streching sometimes causes crashes*/

#define MAX_FRAME_SIZE     		(MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * MAX_BYTES_PER_PIXEL)
#define USBVISION_CLIPMASK_SIZE		(MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT / 8) //bytesize of clipmask

#define USBVISION_URB_FRAMES		32
#define USBVISION_MAX_ISOC_PACKET_SIZE 	959			// NT1003 Specs Document says 1023

#define USBVISION_NUM_HEADERMARKER	20
#define USBVISION_NUMFRAMES		2
#define USBVISION_NUMSBUF		2

#define USBVISION_POWEROFF_TIME		3 * (HZ)		// 3 seconds

/* This macro restricts an int variable to an inclusive range */
#define RESTRICT_TO_RANGE(v,mi,ma) { if ((v) < (mi)) (v) = (mi); else if ((v) > (ma)) (v) = (ma); }

/*
 * We use macros to do YUV -> RGB conversion because this is
 * very important for speed and totally unimportant for size.
 *
 * YUV -> RGB Conversion
 * ---------------------
 *
 * B = 1.164*(Y-16)		    + 2.018*(V-128)
 * G = 1.164*(Y-16) - 0.813*(U-128) - 0.391*(V-128)
 * R = 1.164*(Y-16) + 1.596*(U-128)
 *
 * If you fancy integer arithmetics (as you should), hear this:
 *
 * 65536*B = 76284*(Y-16)		  + 132252*(V-128)
 * 65536*G = 76284*(Y-16) -  53281*(U-128) -  25625*(V-128)
 * 65536*R = 76284*(Y-16) + 104595*(U-128)
 *
 * Make sure the output values are within [0..255] range.
 */
#define LIMIT_RGB(x) (((x) < 0) ? 0 : (((x) > 255) ? 255 : (x)))
#define YUV_TO_RGB_BY_THE_BOOK(my,mu,mv,mr,mg,mb) { \
    int mm_y, mm_yc, mm_u, mm_v, mm_r, mm_g, mm_b; \
    mm_y = (my) - 16;  \
    mm_u = (mu) - 128; \
    mm_v = (mv) - 128; \
    mm_yc= mm_y * 76284; \
    mm_b = (mm_yc		+ 132252*mm_v	) >> 16; \
    mm_g = (mm_yc -  53281*mm_u -  25625*mm_v	) >> 16; \
    mm_r = (mm_yc + 104595*mm_u			) >> 16; \
    mb = LIMIT_RGB(mm_b); \
    mg = LIMIT_RGB(mm_g); \
    mr = LIMIT_RGB(mm_r); \
}



/* Debugging aid */
#define USBVISION_SAY_AND_WAIT(what) { \
	wait_queue_head_t wq; \
	init_waitqueue_head(&wq); \
	printk(KERN_INFO "Say: %s\n", what); \
	interruptible_sleep_on_timeout (&wq, HZ*3); \
}

/*
 * This macro checks if usbvision is still operational. The 'usbvision'
 * pointer must be valid, usbvision->dev must be valid, we are not
 * removing the device and the device has not erred on us.
 */
#define USBVISION_IS_OPERATIONAL(udevice) (\
	(udevice != NULL) && \
	((udevice)->dev != NULL) && \
	((udevice)->last_error == 0) && \
	(!(udevice)->remove_pending))


enum ScanState {
	ScanState_Scanning,	/* Scanning for header */
	ScanState_Lines		/* Parsing lines */
};

/* Completion states of the data parser */
enum ParseState {
	ParseState_Continue,	/* Just parse next item */
	ParseState_NextFrame,	/* Frame done, send it to V4L */
	ParseState_Out,		/* Not enough data for frame */
	ParseState_EndParse	/* End parsing */
};

enum FrameState {
	FrameState_Unused,	/* Unused (no MCAPTURE) */
	FrameState_Ready,	/* Ready to start grabbing */
	FrameState_Grabbing,	/* In the process of being grabbed into */
	FrameState_Done,	/* Finished grabbing, but not been synced yet */
	FrameState_DoneHold,	/* Are syncing or reading */
	FrameState_Error,	/* Something bad happened while processing */
};

enum IsocState {
	IsocState_InFrame,	/* Isoc packet is member of frame */
	IsocState_NoFrame,	/* Isoc packet is not member of any frame */
};

struct usb_device;

struct usbvision_sbuf {
	char *data;
	struct urb *urb;
};

#define USBVISION_MAGIC_1      			0x55
#define USBVISION_MAGIC_2      			0xAA
#define USBVISION_HEADER_LENGTH			0x0c
#define USBVISION_SAA7111_ADDR			0x48
#define USBVISION_SAA7113_ADDR			0x4a
#define USBVISION_IIC_LRACK			0x20
#define USBVISION_IIC_LRNACK			0x30
#define USBVISION_FRAME_FORMAT_PARAM_INTRA	(1<<7)

static struct usbvision_v4l2_format_st {
	int		supported;
	int		bytes_per_pixel;
	int		depth;
	int		format;
	char		*desc;
} usbvision_v4l2_format[] = {
  { 1, 1,  8, V4L2_PIX_FMT_GREY    , "GREY" },
  { 1, 2, 16, V4L2_PIX_FMT_RGB565  , "RGB565" },
  { 1, 3, 24, V4L2_PIX_FMT_RGB24   , "RGB24" },
  { 1, 4, 32, V4L2_PIX_FMT_RGB32   , "RGB32" },
  { 1, 2, 16, V4L2_PIX_FMT_RGB555  , "RGB555" },
  { 1, 2, 16, V4L2_PIX_FMT_YUYV    , "YUV422" },
  { 1, 2, 12, V4L2_PIX_FMT_YVU420  , "YUV420P" }, // 1.5 !
  { 1, 2, 16, V4L2_PIX_FMT_YUV422P , "YUV422P" }
};
#define USBVISION_SUPPORTED_PALETTES ARRAY_SIZE(usbvision_v4l2_format)

struct usbvision_frame_header {
	unsigned char magic_1;				/* 0 magic */
	unsigned char magic_2;				/* 1  magic */
	unsigned char headerLength;			/* 2 */
	unsigned char frameNum;				/* 3 */
	unsigned char framePhase;			/* 4 */
	unsigned char frameLatency;			/* 5 */
	unsigned char dataFormat;			/* 6 */
	unsigned char formatParam;			/* 7 */
	unsigned char frameWidthLo;			/* 8 */
	unsigned char frameWidthHi;			/* 9 */
	unsigned char frameHeightLo;			/* 10 */
	unsigned char frameHeightHi;			/* 11 */
	__u16 frameWidth;				/* 8 - 9 after endian correction*/
	__u16 frameHeight;				/* 10 - 11 after endian correction*/
};

struct usbvision_frame {
	char *data;					/* Frame buffer */
	struct usbvision_frame_header isocHeader;	/* Header from stream */

	int width;					/* Width application is expecting */
	int height;					/* Height */

	int frmwidth;					/* Width the frame actually is */
	int frmheight;					/* Height */

	volatile int grabstate;				/* State of grabbing */
	int scanstate;					/* State of scanning */

	int curline;					/* Line of frame we're working on */

	long scanlength;				/* uncompressed, raw data length of frame */
	long bytes_read;				/* amount of scanlength that has been read from data */
	struct usbvision_v4l2_format_st v4l2_format;	/* format the user needs*/
	int v4l2_linesize;				/* bytes for one videoline*/
        struct timeval timestamp;
	wait_queue_head_t wq;				/* Processes waiting */
	int sequence;					// How many video frames we send to user
};

#define CODEC_SAA7113	7113
#define CODEC_SAA7111	7111
#define BRIDGE_NT1003	1003
#define BRIDGE_NT1004	1004
#define BRIDGE_NT1005   1005

/* Supported Devices: A table for usbvision.c*/

static struct usbvision_device_data_st {
	int idVendor;
	int idProduct;
	int Interface;					/* to handle special interface number like BELKIN and Hauppauge WinTV-USB II */
	int Codec;
	int VideoChannels;
	__u64 VideoNorm;
	int AudioChannels;
	int Radio;
	int vbi;
	int Tuner;
	int TunerType;
	int Vin_Reg1;
	int Vin_Reg2;
	int X_Offset;
	int Y_Offset;
	int Dvi_yuv;
	char *ModelString;
} usbvision_device_data[] = {
	{0xFFF0, 0xFFF0, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Custom Dummy USBVision Device"},  
	{0x0A6F, 0x0400, -1, CODEC_SAA7113, 4, V4L2_STD_NTSC,  1, 0, 1, 0, 0,                          -1, -1, -1, -1, -1, "Xanboo"}, 
	{0x050D, 0x0208, -1, CODEC_SAA7113, 2, V4L2_STD_PAL,   1, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Belkin USBView II"},
	{0x0571, 0x0002,  0, CODEC_SAA7111, 2, V4L2_STD_PAL,   0, 0, 1, 0, 0,                          -1, -1, -1, -1,  7, "echoFX InterView Lite"}, 
	{0x0573, 0x0003, -1, CODEC_SAA7111, 2, V4L2_STD_NTSC,  1, 0, 1, 0, 0,                          -1, -1, -1, -1, -1, "USBGear USBG-V1 resp. HAMA USB"},
	{0x0573, 0x0400, -1, CODEC_SAA7113, 4, V4L2_STD_NTSC,  0, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "D-Link V100"},
	{0x0573, 0x2000, -1, CODEC_SAA7111, 2, V4L2_STD_NTSC,  1, 0, 1, 0, 0,                          -1, -1, -1, -1, -1, "X10 USB Camera"},
	{0x0573, 0x2d00, -1, CODEC_SAA7111, 2, V4L2_STD_PAL,   1, 0, 1, 0, 0,                          -1, -1, -1,  3,  7, "Osprey 50"}, 
	{0x0573, 0x2d01, -1, CODEC_SAA7113, 2, V4L2_STD_NTSC,	 0, 0, 1, 0, 0,			         -1, -1,  0,  3,  7, "Hauppauge USB-Live Model 600"},
	{0x0573, 0x2101, -1, CODEC_SAA7113, 2, V4L2_STD_PAL, 	 2, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Zoran Co. PMD (Nogatech) AV-grabber Manhattan"},
	{0x0573, 0x4100, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, 20, -1, "Nogatech USB-TV (NTSC) FM"},
    	{0x0573, 0x4110, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, 20, -1, "PNY USB-TV (NTSC) FM"}, 
	{0x0573, 0x4450,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "PixelView PlayTv-USB PRO (PAL) FM"},
	{0x0573, 0x4550,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "ZTV ZT-721 2.4GHz USB A/V Receiver"},
	{0x0573, 0x4d00, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 0, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, 20, -1, "Hauppauge WinTv-USB USA"},
	{0x0573, 0x4d01, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 0, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Hauppauge WinTv-USB"},
	{0x0573, 0x4d02, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 0, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (NTSC)"},
	{0x0573, 0x4d03, -1, CODEC_SAA7111, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (SECAM) "},
	{0x0573, 0x4d10, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (NTSC) FM"},
	{0x0573, 0x4d11, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (PAL) FM"},
	{0x0573, 0x4d12, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (PAL) FM"},
	{0x0573, 0x4d2a,  0, CODEC_SAA7113, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_MICROTUNE_4049FM5,    -1, -1,  0,  3,  7, "Hauppauge WinTv USB (NTSC) FM Model 602 40201 Rev B285"}, 
	{0x0573, 0x4d2b,  0, CODEC_SAA7113, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_MICROTUNE_4049FM5,    -1, -1,  0,  3,  7, "Hauppauge WinTv USB (NTSC) FM Model 602 40201 Rev B282"},
	{0x0573, 0x4d2c,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_FM1216ME_MK3, -1, -1,  0,  3,  7, "Hauppauge WinTv USB (PAL/SECAM) 40209 Rev E1A5"}, 
	{0x0573, 0x4d20,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB II (PAL) FM Model 40201 Rev B226"},
	{0x0573, 0x4d21,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB II (PAL)"},
	{0x0573, 0x4d22,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB II (PAL) MODEL 566"},
	{0x0573, 0x4d23, -1, CODEC_SAA7113, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1,  0,  3,  7, "Hauppauge WinTv-USB (SECAM) 4D23"},
	{0x0573, 0x4d25, -1, CODEC_SAA7113, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1,  0,  3,  7, "Hauppauge WinTv-USB (SECAM) Model 40209 Rev B234"},
	{0x0573, 0x4d26, -1, CODEC_SAA7113, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1,  0,  3,  7, "Hauppauge WinTv-USB (SECAM) Model 40209 Rev B243"},
	{0x0573, 0x4d27, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_ALPS_TSBE1_PAL,       -1, -1,  0,  3,  7, "Hauppauge WinTv-USB Model 40204 Rev B281"},
	{0x0573, 0x4d28, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_ALPS_TSBE1_PAL,       -1, -1,  0,  3,  7, "Hauppauge WinTv-USB Model 40204 Rev B283"}, 
	{0x0573, 0x4d29, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB Model 40205 Rev B298"}, 
	{0x0573, 0x4d30, -1, CODEC_SAA7113, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1,  0,  3,  7, "Hauppauge WinTv-USB FM Model 40211 Rev B123"},
	{0x0573, 0x4d31,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB III (PAL) FM Model 568"},
        {0x0573, 0x4d32,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB III (PAL) FM Model 573"},
	{0x0573, 0x4d35,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_MICROTUNE_4049FM5,    -1, -1,  0,  3,  7, "Hauppauge WinTv-USB III (PAL) FM Model 40219 Rev B252"},
	{0x0573, 0x4d37,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_FM1216ME_MK3, -1, -1,  0,  3,  7, "Hauppauge WinTV USB device Model 40219 Rev E189"},
	{0x0768, 0x0006, -1, CODEC_SAA7113, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1,  5,  5, -1, "Camtel Technology USB TV Genie Pro FM Model TVB330"},
	{0x07d0, 0x0001, -1, CODEC_SAA7113, 2, V4L2_STD_PAL,   0, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Digital Video Creator I"},
	{0x07d0, 0x0002, -1, CODEC_SAA7111, 2, V4L2_STD_NTSC,  0, 0, 1, 0, 0,   		         -1, -1, 82, 20,  7, "Global Village GV-007 (NTSC)"},
	{0x07d0, 0x0003,  0, CODEC_SAA7113, 2, V4L2_STD_NTSC,  0, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Dazzle Fusion Model DVC-50 Rev 1 (NTSC)"},
	{0x07d0, 0x0004,  0, CODEC_SAA7113, 2, V4L2_STD_PAL,   0, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Dazzle Fusion Model DVC-80 Rev 1 (PAL)"},
	{0x07d0, 0x0005,  0, CODEC_SAA7113, 2, V4L2_STD_SECAM, 0, 0, 1, 0, 0,	   		         -1, -1,  0,  3,  7, "Dazzle Fusion Model DVC-90 Rev 1 (SECAM)"}, 
        {0x2304, 0x010d, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 0, 0, 1, TUNER_TEMIC_4066FY5_PAL_I,  -1, -1, -1, -1, -1, "Pinnacle Studio PCTV USB (PAL)"},
	{0x2304, 0x0109, -1, CODEC_SAA7111, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1, -1, -1, -1, "Pinnacle Studio PCTV USB (SECAM)"},
	{0x2304, 0x0110, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,128, 23, -1, "Pinnacle Studio PCTV USB (PAL) FM"},
	{0x2304, 0x0111, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_PAL,          -1, -1, -1, -1, -1, "Miro PCTV USB"},
	{0x2304, 0x0112, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Pinnacle Studio PCTV USB (NTSC) FM"},
	{0x2304, 0x0210, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_TEMIC_4009FR5_PAL,    -1, -1,  0,  3,  7, "Pinnacle Studio PCTV USB (PAL) FM"},
	{0x2304, 0x0212, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_TEMIC_4039FR5_NTSC,   -1, -1,  0,  3,  7, "Pinnacle Studio PCTV USB (NTSC) FM"},
	{0x2304, 0x0214, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_TEMIC_4009FR5_PAL,    -1, -1,  0,  3,  7, "Pinnacle Studio PCTV USB (PAL) FM"},
	{0x2304, 0x0300, -1, CODEC_SAA7113, 2, V4L2_STD_NTSC,  1, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Pinnacle Studio Linx Video input cable (NTSC)"},
	{0x2304, 0x0301, -1, CODEC_SAA7113, 2, V4L2_STD_PAL,   1, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Pinnacle Studio Linx Video input cable (PAL)"}, 
	{0x2304, 0x0419, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_TEMIC_4009FR5_PAL,    -1, -1,  0,  3,  7, "Pinnacle PCTV Bungee USB (PAL) FM"},
	{0x2400, 0x4200, -1, CODEC_SAA7111, 3, VIDEO_MODE_NTSC,  1, 0, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Hauppauge WinTv-USB"},
	{}  /* Terminating entry */
};


/* Supported Devices: A table for the usb.c*/

static struct usb_device_id usbvision_table [] = {
	{ USB_DEVICE(0xFFF0, 0xFFF0) },  /* Custom Dummy USBVision Device */
	{ USB_DEVICE(0x0A6F, 0x0400) },  /* Xanboo */ 
	{ USB_DEVICE(0x050d, 0x0208) },  /* Belkin USBView II */
	{ USB_DEVICE(0x0571, 0x0002) },  /* echoFX InterView Lite */
	{ USB_DEVICE(0x0573, 0x0003) },  /* USBGear USBG-V1 */
	{ USB_DEVICE(0x0573, 0x0400) },  /* D-Link V100 */
	{ USB_DEVICE(0x0573, 0x2000) },  /* X10 USB Camera */
	{ USB_DEVICE(0x0573, 0x2d00) },  /* Osprey 50 */ 
	{ USB_DEVICE(0x0573, 0x2d01) },  /* Hauppauge USB-Live Model 600 */
	{ USB_DEVICE(0x0573, 0x2101) },  /* Zoran Co. PMD (Nogatech) AV-grabber Manhattan */
	{ USB_DEVICE(0x0573, 0x4100) },  /* Nogatech USB-TV FM (NTSC) */
	{ USB_DEVICE(0x0573, 0x4110) },  /* PNY USB-TV (NTSC) FM */
	{ USB_DEVICE(0x0573, 0x4450) },  /* PixelView PlayTv-USB PRO (PAL) FM */
	{ USB_DEVICE(0x0573, 0x4550) },  /* ZTV ZT-721 2.4GHz USB A/V Receiver */
	{ USB_DEVICE(0x0573, 0x4d00) },  /* Hauppauge WinTv-USB USA */
	{ USB_DEVICE(0x0573, 0x4d01) },  /* Hauppauge WinTv-USB */
	{ USB_DEVICE(0x0573, 0x4d02) },  /* Hauppauge WinTv-USB UK */
	{ USB_DEVICE(0x0573, 0x4d03) },  /* Hauppauge WinTv-USB France */
	{ USB_DEVICE(0x0573, 0x4d10) },  /* Hauppauge WinTv-USB with FM USA radio */
	{ USB_DEVICE(0x0573, 0x4d11) },  /* Hauppauge WinTv-USB (PAL) with FM radio */
	{ USB_DEVICE(0x0573, 0x4d12) },  /* Hauppauge WinTv-USB UK with FM Radio */
	{ USB_DEVICE(0x0573, 0x4d2a) },  /* Hauppague WinTv USB Model 602 40201 Rev B285 */ 
	{ USB_DEVICE(0x0573, 0x4d2b) },  /* Hauppague WinTv USB Model 602 40201 Rev B282 */
	{ USB_DEVICE(0x0573, 0x4d2c) },  /* Hauppague WinTv USB Model 40209 Rev. E1A5 PAL*/ 
	{ USB_DEVICE(0x0573, 0x4d20) },  /* Hauppauge WinTv-USB II (PAL) FM Model 40201 Rev B226 */
	{ USB_DEVICE(0x0573, 0x4d21) },  /* Hauppauge WinTv-USB II (PAL) with FM radio*/
	{ USB_DEVICE(0x0573, 0x4d22) },  /* Hauppauge WinTv-USB II (PAL) Model 566 */
	{ USB_DEVICE(0x0573, 0x4d23) },  /* Hauppauge WinTv-USB France 4D23*/
	{ USB_DEVICE(0x0573, 0x4d25) },  /* Hauppauge WinTv-USB Model 40209 rev B234 */
	{ USB_DEVICE(0x0573, 0x4d26) },  /* Hauppauge WinTv-USB Model 40209 Rev B243 */
	{ USB_DEVICE(0x0573, 0x4d27) },  /* Hauppauge WinTv-USB Model 40204 Rev B281 */
	{ USB_DEVICE(0x0573, 0x4d28) },  /* Hauppauge WinTv-USB Model 40204 Rev B283 */
	{ USB_DEVICE(0x0573, 0x4d29) },  /* Hauppauge WinTv-USB Model 40205 Rev B298 */ 
	{ USB_DEVICE(0x0573, 0x4d30) },  /* Hauppauge WinTv-USB FM Model 40211 Rev B123 */
	{ USB_DEVICE(0x0573, 0x4d31) },  /* Hauppauge WinTv-USB III (PAL) with FM radio Model 568 */
	{ USB_DEVICE(0x0573, 0x4d32) },  /* Hauppauge WinTv-USB III (PAL) FM Model 573 */
	{ USB_DEVICE(0x0573, 0x4d35) },  /* Hauppauge WinTv-USB III (SECAM) FM Model 40219 Rev B252 */
	{ USB_DEVICE(0x0573, 0x4d37) },  /* Hauppauge WinTv-USB Model 40219 Rev E189 */
	{ USB_DEVICE(0x0768, 0x0006) },  /* Camtel Technology USB TV Genie Pro FM Model TVB330 */
	{ USB_DEVICE(0x07d0, 0x0001) },  /* Digital Video Creator I */
	{ USB_DEVICE(0x07d0, 0x0002) },  /* Global Village GV-007 (NTSC) */
	{ USB_DEVICE(0x07d0, 0x0003) },  /* Dazzle Fusion Model DVC-50 Rev 1 (NTSC) */
	{ USB_DEVICE(0x07d0, 0x0004) },  /* Dazzle Fusion Model DVC-80 Rev 1 (PAL) */
	{ USB_DEVICE(0x07d0, 0x0005) },  /* Dazzle Fusion Model DVC-90 Rev 1 (SECAM) */ 
	{ USB_DEVICE(0x2304, 0x010d) },  /* Pinnacle Studio PCTV USB (PAL) */
	{ USB_DEVICE(0x2304, 0x0109) },  /* Pinnacle Studio PCTV USB (SECAM) */
	{ USB_DEVICE(0x2304, 0x0110) },  /* Pinnacle Studio PCTV USB (PAL) */
	{ USB_DEVICE(0x2304, 0x0111) },  /* Miro PCTV USB */
	{ USB_DEVICE(0x2304, 0x0112) },  /* Pinnacle Studio PCTV USB (NTSC) with FM radio */
	{ USB_DEVICE(0x2304, 0x0210) },  /* Pinnacle Studio PCTV USB (PAL) with FM radio */
	{ USB_DEVICE(0x2304, 0x0212) },  /* Pinnacle Studio PCTV USB (NTSC) with FM radio */
	{ USB_DEVICE(0x2304, 0x0214) },  /* Pinnacle Studio PCTV USB (PAL) with FM radio */
	{ USB_DEVICE(0x2304, 0x0300) },  /* Pinnacle Studio Linx Video input cable (NTSC) */
	{ USB_DEVICE(0x2304, 0x0301) },  /* Pinnacle Studio Linx Video input cable (PAL) */ 
	{ USB_DEVICE(0x2304, 0x0419) },  /* Pinnacle PCTV Bungee USB (PAL) FM */

	{ USB_DEVICE(0x2400, 0x4200) },  /* Hauppauge WinTv-USB2 Model 42012 */

	{ }  /* Terminating entry */
};


#define USBVISION_I2C_CLIENTS_MAX		8

struct usb_usbvision {
	struct video_device *vdev;         				/* Video Device */
	struct video_device *rdev;               			/* Radio Device */
	struct video_device *vbi; 					/* VBI Device   */
	struct video_audio audio_dev;	        			/* Current audio params */

	/* i2c Declaration Section*/
	struct i2c_adapter i2c_adap;
	struct i2c_algo_usb_data i2c_algo;
	struct i2c_client i2c_client;
	int i2c_state, i2c_ok;
	struct i2c_client *i2c_clients[USBVISION_I2C_CLIENTS_MAX];

	struct urb *ctrlUrb;
	unsigned char ctrlUrbBuffer[8];
	int ctrlUrbBusy;
	struct usb_ctrlrequest ctrlUrbSetup;
	wait_queue_head_t ctrlUrb_wq;					// Processes waiting
	struct semaphore ctrlUrbLock;

	int have_tuner;
	int tuner_type;
	int bridgeType;							// NT1003, NT1004, NT1005
	int channel;
	int radio;
	int video_inputs;						// # of inputs
	unsigned long freq;
	int AudioMute;
	int AudioChannel;
	int isocMode;							// format of video data for the usb isoc-transfer
	unsigned int nr;						// Number of the device < MAX_USBVISION

	/* Device structure */
	struct usb_device *dev;
	unsigned char iface;						/* Video interface number */
	unsigned char ifaceAltActive, ifaceAltInactive;			/* Alt settings */
	unsigned char Vin_Reg2_Preset;
	struct semaphore lock;
	struct timer_list powerOffTimer;
	struct work_struct powerOffWork;
	int power;							/* is the device powered on? */
	int user;							/* user count for exclusive use */
	int usbvision_used;						/* Is this structure in use? */
	int initialized;						/* Had we already sent init sequence? */
	int DevModel;							/* What type of USBVISION device we got? */
	int streaming;							/* Are we streaming Isochronous? */
	int last_error;							/* What calamity struck us? */
	int curwidth;							/* width of the frame the device is currently set to*/
	int curheight;      						/* height of the frame the device is currently set to*/
	int stretch_width;						/* stretch-factor for frame width (from usb to screen)*/
	int stretch_height;						/* stretch-factor for frame height (from usb to screen)*/
	char *fbuf;							/* Videodev buffer area for mmap*/
	int max_frame_size;						/* Bytes in one video frame */
	int fbuf_size;							/* Videodev buffer size */
	int curFrameNum;						// number of current frame in frame buffer mode
	struct usbvision_frame *curFrame;				// pointer to current frame, set by usbvision_find_header
	struct usbvision_frame frame[USBVISION_NUMFRAMES];		// frame buffer
	int curSbufNum;							// number of current receiving sbuf
	struct usbvision_sbuf sbuf[USBVISION_NUMSBUF];			// S buffering
	volatile int remove_pending;					/* If set then about to exit */

	/* Scratch space from the Isochronous Pipe.*/
	unsigned char *scratch;
	int scratch_read_ptr;
	int scratch_write_ptr;
	int scratch_headermarker[USBVISION_NUM_HEADERMARKER];
	int scratch_headermarker_read_ptr;
	int scratch_headermarker_write_ptr;
	int isocstate;
        /* color controls */
	int saturation;
	int hue;
	int brightness;
        int contrast;
	int depth;
	struct usbvision_v4l2_format_st palette;

	struct v4l2_capability vcap;					/* Video capabilities */
	struct v4l2_input input;					/* May be used for tuner support */
	unsigned char video_endp;					/* 0x82 for USBVISION devices based */

	// Overlay stuff:
        struct v4l2_framebuffer vid_buf;
	struct v4l2_format vid_win;
	int vid_buf_valid;						// Status: video buffer is valid (set)
	int vid_win_valid;						// Status: video window is valid (set)
	int	overlay;						/*Status: Are we overlaying? */
	unsigned int 	clipmask[USBVISION_CLIPMASK_SIZE / 4];
	unsigned char   *overlay_base;					/* Virtual base address of video buffer */
	unsigned char   *overlay_win;					/* virt start address of overlay window */
	struct usbvision_frame overlay_frame;

	// Decompression stuff:
	unsigned char *IntraFrameBuffer;				/* Buffer for reference frame */
	int BlockPos; 							//for test only
	int requestIntra;						// 0 = normal; 1 = intra frame is requested;
	int lastIsocFrameNum;						// check for lost isoc frames
	int isocPacketSize;						// need to calculate usedBandwidth
	int usedBandwidth;						// used bandwidth 0-100%, need to set comprLevel
	int comprLevel;							// How strong (100) or weak (0) is compression
	int lastComprLevel;						// How strong (100) or weak (0) was compression
	int usb_bandwidth;						/* Mbit/s */

	/* /proc entries, relative to /proc/video/usbvision/ */
	struct proc_dir_entry *proc_devdir;		/* Per-device proc directory */
	struct proc_dir_entry *proc_info;		/* <minor#>/info entry */
	struct proc_dir_entry *proc_register;		/* <minor#>/register entry */
	struct proc_dir_entry *proc_freq; 		/* <minor#>/freq entry */ 
	struct proc_dir_entry *proc_input; 		/* <minor#>/input entry */ 
	struct proc_dir_entry *proc_frame;		/* <minor#>/frame entry */
	struct proc_dir_entry *proc_button;		/* <minor#>/button entry */
	struct proc_dir_entry *proc_control;		/* <minor#>/control entry */

	/* Statistics that can be overlayed on the screen */
	unsigned long isocUrbCount;			// How many URBs we received so far
	unsigned long urb_length;			/* Length of last URB */
	unsigned long isocDataCount;			/* How many bytes we received */
	unsigned long header_count;			/* How many frame headers we found */
	unsigned long scratch_ovf_count;		/* How many times we overflowed scratch */
	unsigned long isocSkipCount;			/* How many empty ISO packets received */
	unsigned long isocErrCount;			/* How many bad ISO packets received */
	unsigned long isocPacketCount;			// How many packets we totally got
	unsigned long timeInIrq;			// How long do we need for interrupt
	int isocMeasureBandwidthCount;
	int frame_num;					// How many video frames we send to user
	int maxStripLen;				// How big is the biggest strip
	int comprBlockPos;
	int stripLenErrors;				// How many times was BlockPos greater than StripLen
	int stripMagicErrors;
	int stripLineNumberErrors;
	int ComprBlockTypes[4];
};

#endif									/* __LINUX_USBVISION_H */

