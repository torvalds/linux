/*
 * USBVISION.H
 *  usbvision header file
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *                         Dwaine Garden <dwainegarden@rogers.com>
 *
 *
 * Report problems to v4l MailingList : http://www.redhat.com/mailman/listinfo/video4linux-list
 *
 * This module is part of usbvision driver project.
 * Updates to driver completed by Dwaine P. Garden
 * v4l2 conversion by Thierry Merle <thierry.merle@free.fr>
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
#include <linux/usb.h>
#include <linux/i2c.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include <linux/videodev2.h>

#define USBVISION_DEBUG		/* Turn on debug messages */

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

#define USBVISION_NUM_HEADERMARKER	20
#define USBVISION_NUMFRAMES		3  /* Maximum number of frames an application can get */
#define USBVISION_NUMSBUF		2 /* Dimensioning the USB S buffering */

#define USBVISION_POWEROFF_TIME		3 * (HZ)		// 3 seconds


#define FRAMERATE_MIN	0
#define FRAMERATE_MAX	31

enum {
	ISOC_MODE_YUV422 = 0x03,
	ISOC_MODE_YUV420 = 0x14,
	ISOC_MODE_COMPRESS = 0x60,
};

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

#define I2C_USB_ADAP_MAX	16

/* ----------------------------------------------------------------- */
/* usbvision video structures                                        */
/* ----------------------------------------------------------------- */
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

/* stream states */
enum StreamState {
	Stream_Off,		/* Driver streaming is completely OFF */
	Stream_Idle,		/* Driver streaming is ready to be put ON by the application */
	Stream_Interrupt,	/* Driver streaming must be interrupted */
	Stream_On,		/* Driver streaming is put ON by the application */
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

struct usbvision_v4l2_format_st {
	int		supported;
	int		bytes_per_pixel;
	int		depth;
	int		format;
	char		*desc;
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

/* tvnorms */
struct usbvision_tvnorm {
	char *name;
	v4l2_std_id id;
	/* mode for saa7113h */
	int mode;
};

struct usbvision_frame {
	char *data;					/* Frame buffer */
	struct usbvision_frame_header isocHeader;	/* Header from stream */

	int width;					/* Width application is expecting */
	int height;					/* Height */
	int index;					/* Frame index */
	int frmwidth;					/* Width the frame actually is */
	int frmheight;					/* Height */

	volatile int grabstate;				/* State of grabbing */
	int scanstate;					/* State of scanning */

	struct list_head frame;

	int curline;					/* Line of frame we're working on */

	long scanlength;				/* uncompressed, raw data length of frame */
	long bytes_read;				/* amount of scanlength that has been read from data */
	struct usbvision_v4l2_format_st v4l2_format;	/* format the user needs*/
	int v4l2_linesize;				/* bytes for one videoline*/
	struct timeval timestamp;
	int sequence;					// How many video frames we send to user
};

#define CODEC_SAA7113	7113
#define CODEC_SAA7111	7111
#define BRIDGE_NT1003	1003
#define BRIDGE_NT1004	1004
#define BRIDGE_NT1005   1005

struct usbvision_device_data_st {
	__u64 VideoNorm;
	const char *ModelString;
	int Interface; /* to handle special interface number like BELKIN and Hauppauge WinTV-USB II */
	__u16 Codec;
	unsigned VideoChannels:3;
	unsigned AudioChannels:2;
	unsigned Radio:1;
	unsigned vbi:1;
	unsigned Tuner:1;
	unsigned Vin_Reg1_override:1;	/* Override default value with */
	unsigned Vin_Reg2_override:1;   /* Vin_Reg1, Vin_Reg2, etc. */
	unsigned Dvi_yuv_override:1;
	__u8 Vin_Reg1;
	__u8 Vin_Reg2;
	__u8 Dvi_yuv;
	__u8 TunerType;
	__s16 X_Offset;
	__s16 Y_Offset;
};

/* Declared on usbvision-cards.c */
extern struct usbvision_device_data_st usbvision_device_data[];
extern struct usb_device_id usbvision_table[];

struct usb_usbvision {
	struct video_device *vdev;         				/* Video Device */
	struct video_device *rdev;               			/* Radio Device */
	struct video_device *vbi; 					/* VBI Device   */

	/* i2c Declaration Section*/
	struct i2c_adapter i2c_adap;
	struct i2c_client i2c_client;

	struct urb *ctrlUrb;
	unsigned char ctrlUrbBuffer[8];
	int ctrlUrbBusy;
	struct usb_ctrlrequest ctrlUrbSetup;
	wait_queue_head_t ctrlUrb_wq;					// Processes waiting
	struct semaphore ctrlUrbLock;

	/* configuration part */
	int have_tuner;
	int tuner_type;
	int tuner_addr;
	int bridgeType;							// NT1003, NT1004, NT1005
	int channel;
	int radio;
	int video_inputs;						// # of inputs
	unsigned long freq;
	int AudioMute;
	int AudioChannel;
	int isocMode;							// format of video data for the usb isoc-transfer
	unsigned int nr;						// Number of the device

	/* Device structure */
	struct usb_device *dev;
	/* usb transfer */
	int num_alt;		/* Number of alternative settings */
	unsigned int *alt_max_pkt_size;	/* array of wMaxPacketSize */
	unsigned char iface;						/* Video interface number */
	unsigned char ifaceAlt;			/* Alt settings */
	unsigned char Vin_Reg2_Preset;
	struct semaphore lock;
	struct timer_list powerOffTimer;
	struct work_struct powerOffWork;
	int power;							/* is the device powered on? */
	int user;							/* user count for exclusive use */
	int initialized;						/* Had we already sent init sequence? */
	int DevModel;							/* What type of USBVISION device we got? */
	enum StreamState streaming;					/* Are we streaming Isochronous? */
	int last_error;							/* What calamity struck us? */
	int curwidth;							/* width of the frame the device is currently set to*/
	int curheight;      						/* height of the frame the device is currently set to*/
	int stretch_width;						/* stretch-factor for frame width (from usb to screen)*/
	int stretch_height;						/* stretch-factor for frame height (from usb to screen)*/
	char *fbuf;							/* Videodev buffer area for mmap*/
	int max_frame_size;						/* Bytes in one video frame */
	int fbuf_size;							/* Videodev buffer size */
	spinlock_t queue_lock;						/* spinlock for protecting mods on inqueue and outqueue */
	struct list_head inqueue, outqueue;                             /* queued frame list and ready to dequeue frame list */
	wait_queue_head_t wait_frame;					/* Processes waiting */
	wait_queue_head_t wait_stream;					/* Processes waiting */
	struct usbvision_frame *curFrame;				// pointer to current frame, set by usbvision_find_header
	struct usbvision_frame frame[USBVISION_NUMFRAMES];		// frame buffer
	int num_frames;							// number of frames allocated
	struct usbvision_sbuf sbuf[USBVISION_NUMSBUF];			// S buffering
	volatile int remove_pending;					/* If set then about to exit */

	/* Scratch space from the Isochronous Pipe.*/
	unsigned char *scratch;
	int scratch_read_ptr;
	int scratch_write_ptr;
	int scratch_headermarker[USBVISION_NUM_HEADERMARKER];
	int scratch_headermarker_read_ptr;
	int scratch_headermarker_write_ptr;
	enum IsocState isocstate;
	struct usbvision_v4l2_format_st palette;

	struct v4l2_capability vcap;					/* Video capabilities */
	unsigned int ctl_input;						/* selected input */
	struct usbvision_tvnorm *tvnorm;				/* selected tv norm */
	unsigned char video_endp;					/* 0x82 for USBVISION devices based */

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


/* --------------------------------------------------------------- */
/* defined in usbvision-i2c.c                                      */
/* i2c-algo-usb declaration                                        */
/* --------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
/* usbvision specific I2C functions                                        */
/* ----------------------------------------------------------------------- */
int usbvision_i2c_register(struct usb_usbvision *usbvision);
int usbvision_i2c_unregister(struct usb_usbvision *usbvision);
void call_i2c_clients(struct usb_usbvision *usbvision, unsigned int cmd,void *arg);

/* defined in usbvision-core.c                                      */
int usbvision_read_reg(struct usb_usbvision *usbvision, unsigned char reg);
int usbvision_write_reg(struct usb_usbvision *usbvision, unsigned char reg,
			unsigned char value);

int usbvision_frames_alloc(struct usb_usbvision *usbvision, int number_of_frames);
void usbvision_frames_free(struct usb_usbvision *usbvision);
int usbvision_scratch_alloc(struct usb_usbvision *usbvision);
void usbvision_scratch_free(struct usb_usbvision *usbvision);
int usbvision_decompress_alloc(struct usb_usbvision *usbvision);
void usbvision_decompress_free(struct usb_usbvision *usbvision);

int usbvision_setup(struct usb_usbvision *usbvision,int format);
int usbvision_init_isoc(struct usb_usbvision *usbvision);
int usbvision_restart_isoc(struct usb_usbvision *usbvision);
void usbvision_stop_isoc(struct usb_usbvision *usbvision);
int usbvision_set_alternate(struct usb_usbvision *dev);

int usbvision_set_audio(struct usb_usbvision *usbvision, int AudioChannel);
int usbvision_audio_off(struct usb_usbvision *usbvision);

int usbvision_begin_streaming(struct usb_usbvision *usbvision);
void usbvision_empty_framequeues(struct usb_usbvision *dev);
int usbvision_stream_interrupt(struct usb_usbvision *dev);

int usbvision_muxsel(struct usb_usbvision *usbvision, int channel);
int usbvision_set_input(struct usb_usbvision *usbvision);
int usbvision_set_output(struct usb_usbvision *usbvision, int width, int height);

void usbvision_init_powerOffTimer(struct usb_usbvision *usbvision);
void usbvision_set_powerOffTimer(struct usb_usbvision *usbvision);
void usbvision_reset_powerOffTimer(struct usb_usbvision *usbvision);
int usbvision_power_off(struct usb_usbvision *usbvision);
int usbvision_power_on(struct usb_usbvision *usbvision);

#endif									/* __LINUX_USBVISION_H */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
