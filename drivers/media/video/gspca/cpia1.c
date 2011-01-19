/*
 * cpia CPiA (1) gspca driver
 *
 * Copyright (C) 2010 Hans de Goede <hdegoede@redhat.com>
 *
 * This module is adapted from the in kernel v4l1 cpia driver which is :
 *
 * (C) Copyright 1999-2000 Peter Pregler
 * (C) Copyright 1999-2000 Scott J. Bertin
 * (C) Copyright 1999-2000 Johannes Erdfelt <johannes@erdfelt.com>
 * (C) Copyright 2000 STMicroelectronics
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define MODULE_NAME "cpia1"

#include "gspca.h"

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Vision CPiA");
MODULE_LICENSE("GPL");

/* constant value's */
#define MAGIC_0		0x19
#define MAGIC_1		0x68
#define DATA_IN		0xc0
#define DATA_OUT	0x40
#define VIDEOSIZE_QCIF	0	/* 176x144 */
#define VIDEOSIZE_CIF	1	/* 352x288 */
#define SUBSAMPLE_420	0
#define SUBSAMPLE_422	1
#define YUVORDER_YUYV	0
#define YUVORDER_UYVY	1
#define NOT_COMPRESSED	0
#define COMPRESSED	1
#define NO_DECIMATION	0
#define DECIMATION_ENAB	1
#define EOI		0xff	/* End Of Image */
#define EOL		0xfd	/* End Of Line */
#define FRAME_HEADER_SIZE	64

/* Image grab modes */
#define CPIA_GRAB_SINGLE	0
#define CPIA_GRAB_CONTINEOUS	1

/* Compression parameters */
#define CPIA_COMPRESSION_NONE	0
#define CPIA_COMPRESSION_AUTO	1
#define CPIA_COMPRESSION_MANUAL	2
#define CPIA_COMPRESSION_TARGET_QUALITY         0
#define CPIA_COMPRESSION_TARGET_FRAMERATE       1

/* Return offsets for GetCameraState */
#define SYSTEMSTATE	0
#define GRABSTATE	1
#define STREAMSTATE	2
#define FATALERROR	3
#define CMDERROR	4
#define DEBUGFLAGS	5
#define VPSTATUS	6
#define ERRORCODE	7

/* SystemState */
#define UNINITIALISED_STATE	0
#define PASS_THROUGH_STATE	1
#define LO_POWER_STATE		2
#define HI_POWER_STATE		3
#define WARM_BOOT_STATE		4

/* GrabState */
#define GRAB_IDLE		0
#define GRAB_ACTIVE		1
#define GRAB_DONE		2

/* StreamState */
#define STREAM_NOT_READY	0
#define STREAM_READY		1
#define STREAM_OPEN		2
#define STREAM_PAUSED		3
#define STREAM_FINISHED		4

/* Fatal Error, CmdError, and DebugFlags */
#define CPIA_FLAG	  1
#define SYSTEM_FLAG	  2
#define INT_CTRL_FLAG	  4
#define PROCESS_FLAG	  8
#define COM_FLAG	 16
#define VP_CTRL_FLAG	 32
#define CAPTURE_FLAG	 64
#define DEBUG_FLAG	128

/* VPStatus */
#define VP_STATE_OK			0x00

#define VP_STATE_FAILED_VIDEOINIT	0x01
#define VP_STATE_FAILED_AECACBINIT	0x02
#define VP_STATE_AEC_MAX		0x04
#define VP_STATE_ACB_BMAX		0x08

#define VP_STATE_ACB_RMIN		0x10
#define VP_STATE_ACB_GMIN		0x20
#define VP_STATE_ACB_RMAX		0x40
#define VP_STATE_ACB_GMAX		0x80

/* default (minimum) compensation values */
#define COMP_RED        220
#define COMP_GREEN1     214
#define COMP_GREEN2     COMP_GREEN1
#define COMP_BLUE       230

/* exposure status */
#define EXPOSURE_VERY_LIGHT 0
#define EXPOSURE_LIGHT      1
#define EXPOSURE_NORMAL     2
#define EXPOSURE_DARK       3
#define EXPOSURE_VERY_DARK  4

#define CPIA_MODULE_CPIA			(0 << 5)
#define CPIA_MODULE_SYSTEM			(1 << 5)
#define CPIA_MODULE_VP_CTRL			(5 << 5)
#define CPIA_MODULE_CAPTURE			(6 << 5)
#define CPIA_MODULE_DEBUG			(7 << 5)

#define INPUT (DATA_IN << 8)
#define OUTPUT (DATA_OUT << 8)

#define CPIA_COMMAND_GetCPIAVersion	(INPUT | CPIA_MODULE_CPIA | 1)
#define CPIA_COMMAND_GetPnPID		(INPUT | CPIA_MODULE_CPIA | 2)
#define CPIA_COMMAND_GetCameraStatus	(INPUT | CPIA_MODULE_CPIA | 3)
#define CPIA_COMMAND_GotoHiPower	(OUTPUT | CPIA_MODULE_CPIA | 4)
#define CPIA_COMMAND_GotoLoPower	(OUTPUT | CPIA_MODULE_CPIA | 5)
#define CPIA_COMMAND_GotoSuspend	(OUTPUT | CPIA_MODULE_CPIA | 7)
#define CPIA_COMMAND_GotoPassThrough	(OUTPUT | CPIA_MODULE_CPIA | 8)
#define CPIA_COMMAND_ModifyCameraStatus	(OUTPUT | CPIA_MODULE_CPIA | 10)

#define CPIA_COMMAND_ReadVCRegs		(INPUT | CPIA_MODULE_SYSTEM | 1)
#define CPIA_COMMAND_WriteVCReg		(OUTPUT | CPIA_MODULE_SYSTEM | 2)
#define CPIA_COMMAND_ReadMCPorts	(INPUT | CPIA_MODULE_SYSTEM | 3)
#define CPIA_COMMAND_WriteMCPort	(OUTPUT | CPIA_MODULE_SYSTEM | 4)
#define CPIA_COMMAND_SetBaudRate	(OUTPUT | CPIA_MODULE_SYSTEM | 5)
#define CPIA_COMMAND_SetECPTiming	(OUTPUT | CPIA_MODULE_SYSTEM | 6)
#define CPIA_COMMAND_ReadIDATA		(INPUT | CPIA_MODULE_SYSTEM | 7)
#define CPIA_COMMAND_WriteIDATA		(OUTPUT | CPIA_MODULE_SYSTEM | 8)
#define CPIA_COMMAND_GenericCall	(OUTPUT | CPIA_MODULE_SYSTEM | 9)
#define CPIA_COMMAND_I2CStart		(OUTPUT | CPIA_MODULE_SYSTEM | 10)
#define CPIA_COMMAND_I2CStop		(OUTPUT | CPIA_MODULE_SYSTEM | 11)
#define CPIA_COMMAND_I2CWrite		(OUTPUT | CPIA_MODULE_SYSTEM | 12)
#define CPIA_COMMAND_I2CRead		(INPUT | CPIA_MODULE_SYSTEM | 13)

#define CPIA_COMMAND_GetVPVersion	(INPUT | CPIA_MODULE_VP_CTRL | 1)
#define CPIA_COMMAND_ResetFrameCounter	(INPUT | CPIA_MODULE_VP_CTRL | 2)
#define CPIA_COMMAND_SetColourParams	(OUTPUT | CPIA_MODULE_VP_CTRL | 3)
#define CPIA_COMMAND_SetExposure	(OUTPUT | CPIA_MODULE_VP_CTRL | 4)
#define CPIA_COMMAND_SetColourBalance	(OUTPUT | CPIA_MODULE_VP_CTRL | 6)
#define CPIA_COMMAND_SetSensorFPS	(OUTPUT | CPIA_MODULE_VP_CTRL | 7)
#define CPIA_COMMAND_SetVPDefaults	(OUTPUT | CPIA_MODULE_VP_CTRL | 8)
#define CPIA_COMMAND_SetApcor		(OUTPUT | CPIA_MODULE_VP_CTRL | 9)
#define CPIA_COMMAND_SetFlickerCtrl	(OUTPUT | CPIA_MODULE_VP_CTRL | 10)
#define CPIA_COMMAND_SetVLOffset	(OUTPUT | CPIA_MODULE_VP_CTRL | 11)
#define CPIA_COMMAND_GetColourParams	(INPUT | CPIA_MODULE_VP_CTRL | 16)
#define CPIA_COMMAND_GetColourBalance	(INPUT | CPIA_MODULE_VP_CTRL | 17)
#define CPIA_COMMAND_GetExposure	(INPUT | CPIA_MODULE_VP_CTRL | 18)
#define CPIA_COMMAND_SetSensorMatrix	(OUTPUT | CPIA_MODULE_VP_CTRL | 19)
#define CPIA_COMMAND_ColourBars		(OUTPUT | CPIA_MODULE_VP_CTRL | 25)
#define CPIA_COMMAND_ReadVPRegs		(INPUT | CPIA_MODULE_VP_CTRL | 30)
#define CPIA_COMMAND_WriteVPReg		(OUTPUT | CPIA_MODULE_VP_CTRL | 31)

#define CPIA_COMMAND_GrabFrame		(OUTPUT | CPIA_MODULE_CAPTURE | 1)
#define CPIA_COMMAND_UploadFrame	(OUTPUT | CPIA_MODULE_CAPTURE | 2)
#define CPIA_COMMAND_SetGrabMode	(OUTPUT | CPIA_MODULE_CAPTURE | 3)
#define CPIA_COMMAND_InitStreamCap	(OUTPUT | CPIA_MODULE_CAPTURE | 4)
#define CPIA_COMMAND_FiniStreamCap	(OUTPUT | CPIA_MODULE_CAPTURE | 5)
#define CPIA_COMMAND_StartStreamCap	(OUTPUT | CPIA_MODULE_CAPTURE | 6)
#define CPIA_COMMAND_EndStreamCap	(OUTPUT | CPIA_MODULE_CAPTURE | 7)
#define CPIA_COMMAND_SetFormat		(OUTPUT | CPIA_MODULE_CAPTURE | 8)
#define CPIA_COMMAND_SetROI		(OUTPUT | CPIA_MODULE_CAPTURE | 9)
#define CPIA_COMMAND_SetCompression	(OUTPUT | CPIA_MODULE_CAPTURE | 10)
#define CPIA_COMMAND_SetCompressionTarget (OUTPUT | CPIA_MODULE_CAPTURE | 11)
#define CPIA_COMMAND_SetYUVThresh	(OUTPUT | CPIA_MODULE_CAPTURE | 12)
#define CPIA_COMMAND_SetCompressionParams (OUTPUT | CPIA_MODULE_CAPTURE | 13)
#define CPIA_COMMAND_DiscardFrame	(OUTPUT | CPIA_MODULE_CAPTURE | 14)
#define CPIA_COMMAND_GrabReset		(OUTPUT | CPIA_MODULE_CAPTURE | 15)

#define CPIA_COMMAND_OutputRS232	(OUTPUT | CPIA_MODULE_DEBUG | 1)
#define CPIA_COMMAND_AbortProcess	(OUTPUT | CPIA_MODULE_DEBUG | 4)
#define CPIA_COMMAND_SetDramPage	(OUTPUT | CPIA_MODULE_DEBUG | 5)
#define CPIA_COMMAND_StartDramUpload	(OUTPUT | CPIA_MODULE_DEBUG | 6)
#define CPIA_COMMAND_StartDummyDtream	(OUTPUT | CPIA_MODULE_DEBUG | 8)
#define CPIA_COMMAND_AbortStream	(OUTPUT | CPIA_MODULE_DEBUG | 9)
#define CPIA_COMMAND_DownloadDRAM	(OUTPUT | CPIA_MODULE_DEBUG | 10)
#define CPIA_COMMAND_Null		(OUTPUT | CPIA_MODULE_DEBUG | 11)

#define ROUND_UP_EXP_FOR_FLICKER 15

/* Constants for automatic frame rate adjustment */
#define MAX_EXP       302
#define MAX_EXP_102   255
#define LOW_EXP       140
#define VERY_LOW_EXP   70
#define TC             94
#define	EXP_ACC_DARK   50
#define	EXP_ACC_LIGHT  90
#define HIGH_COMP_102 160
#define MAX_COMP      239
#define DARK_TIME       3
#define LIGHT_TIME      3

#define FIRMWARE_VERSION(x, y) (sd->params.version.firmwareVersion == (x) && \
				sd->params.version.firmwareRevision == (y))

/* Developer's Guide Table 5 p 3-34
 * indexed by [mains][sensorFps.baserate][sensorFps.divisor]*/
static u8 flicker_jumps[2][2][4] =
{ { { 76, 38, 19, 9 }, { 92, 46, 23, 11 } },
  { { 64, 32, 16, 8 }, { 76, 38, 19, 9} }
};

struct cam_params {
	struct {
		u8 firmwareVersion;
		u8 firmwareRevision;
		u8 vcVersion;
		u8 vcRevision;
	} version;
	struct {
		u16 vendor;
		u16 product;
		u16 deviceRevision;
	} pnpID;
	struct {
		u8 vpVersion;
		u8 vpRevision;
		u16 cameraHeadID;
	} vpVersion;
	struct {
		u8 systemState;
		u8 grabState;
		u8 streamState;
		u8 fatalError;
		u8 cmdError;
		u8 debugFlags;
		u8 vpStatus;
		u8 errorCode;
	} status;
	struct {
		u8 brightness;
		u8 contrast;
		u8 saturation;
	} colourParams;
	struct {
		u8 gainMode;
		u8 expMode;
		u8 compMode;
		u8 centreWeight;
		u8 gain;
		u8 fineExp;
		u8 coarseExpLo;
		u8 coarseExpHi;
		u8 redComp;
		u8 green1Comp;
		u8 green2Comp;
		u8 blueComp;
	} exposure;
	struct {
		u8 balanceMode;
		u8 redGain;
		u8 greenGain;
		u8 blueGain;
	} colourBalance;
	struct {
		u8 divisor;
		u8 baserate;
	} sensorFps;
	struct {
		u8 gain1;
		u8 gain2;
		u8 gain4;
		u8 gain8;
	} apcor;
	struct {
		u8 disabled;
		u8 flickerMode;
		u8 coarseJump;
		u8 allowableOverExposure;
	} flickerControl;
	struct {
		u8 gain1;
		u8 gain2;
		u8 gain4;
		u8 gain8;
	} vlOffset;
	struct {
		u8 mode;
		u8 decimation;
	} compression;
	struct {
		u8 frTargeting;
		u8 targetFR;
		u8 targetQ;
	} compressionTarget;
	struct {
		u8 yThreshold;
		u8 uvThreshold;
	} yuvThreshold;
	struct {
		u8 hysteresis;
		u8 threshMax;
		u8 smallStep;
		u8 largeStep;
		u8 decimationHysteresis;
		u8 frDiffStepThresh;
		u8 qDiffStepThresh;
		u8 decimationThreshMod;
	} compressionParams;
	struct {
		u8 videoSize;		/* CIF/QCIF */
		u8 subSample;
		u8 yuvOrder;
	} format;
	struct {                        /* Intel QX3 specific data */
		u8 qx3_detected;        /* a QX3 is present */
		u8 toplight;            /* top light lit , R/W */
		u8 bottomlight;         /* bottom light lit, R/W */
		u8 button;              /* snapshot button pressed (R/O) */
		u8 cradled;             /* microscope is in cradle (R/O) */
	} qx3;
	struct {
		u8 colStart;		/* skip first 8*colStart pixels */
		u8 colEnd;		/* finish at 8*colEnd pixels */
		u8 rowStart;		/* skip first 4*rowStart lines */
		u8 rowEnd;		/* finish at 4*rowEnd lines */
	} roi;
	u8 ecpTiming;
	u8 streamStartLine;
};

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */
	struct cam_params params;		/* camera settings */

	atomic_t cam_exposure;
	atomic_t fps;
	int exposure_count;
	u8 exposure_status;
	u8 mainsFreq;				/* 0 = 50hz, 1 = 60hz */
	u8 first_frame;
	u8 freq;
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setsaturation(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getsaturation(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setfreq(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getfreq(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcomptarget(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcomptarget(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setilluminator1(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getilluminator1(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setilluminator2(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getilluminator2(struct gspca_dev *gspca_dev, __s32 *val);

static const struct ctrl sd_ctrls[] = {
	{
#define BRIGHTNESS_IDX 0
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 100,
		.step = 1,
#define BRIGHTNESS_DEF 50
		.default_value = BRIGHTNESS_DEF,
		.flags = 0,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
#define CONTRAST_IDX 1
	{
	    {
		.id      = V4L2_CID_CONTRAST,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Contrast",
		.minimum = 0,
		.maximum = 96,
		.step    = 8,
#define CONTRAST_DEF 48
		.default_value = CONTRAST_DEF,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
#define SATURATION_IDX 2
	{
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Saturation",
		.minimum = 0,
		.maximum = 100,
		.step    = 1,
#define SATURATION_DEF 50
		.default_value = SATURATION_DEF,
	    },
	    .set = sd_setsaturation,
	    .get = sd_getsaturation,
	},
#define POWER_LINE_FREQUENCY_IDX 3
	{
		{
			.id	 = V4L2_CID_POWER_LINE_FREQUENCY,
			.type    = V4L2_CTRL_TYPE_MENU,
			.name    = "Light frequency filter",
			.minimum = 0,
			.maximum = 2,	/* 0: 0, 1: 50Hz, 2:60Hz */
			.step    = 1,
#define FREQ_DEF 1
			.default_value = FREQ_DEF,
		},
		.set = sd_setfreq,
		.get = sd_getfreq,
	},
#define ILLUMINATORS_1_IDX 4
	{
		{
			.id	 = V4L2_CID_ILLUMINATORS_1,
			.type    = V4L2_CTRL_TYPE_BOOLEAN,
			.name    = "Illuminator 1",
			.minimum = 0,
			.maximum = 1,
			.step    = 1,
#define ILLUMINATORS_1_DEF 0
			.default_value = ILLUMINATORS_1_DEF,
		},
		.set = sd_setilluminator1,
		.get = sd_getilluminator1,
	},
#define ILLUMINATORS_2_IDX 5
	{
		{
			.id	 = V4L2_CID_ILLUMINATORS_2,
			.type    = V4L2_CTRL_TYPE_BOOLEAN,
			.name    = "Illuminator 2",
			.minimum = 0,
			.maximum = 1,
			.step    = 1,
#define ILLUMINATORS_2_DEF 0
			.default_value = ILLUMINATORS_2_DEF,
		},
		.set = sd_setilluminator2,
		.get = sd_getilluminator2,
	},
#define COMP_TARGET_IDX 6
	{
		{
#define V4L2_CID_COMP_TARGET V4L2_CID_PRIVATE_BASE
			.id	 = V4L2_CID_COMP_TARGET,
			.type    = V4L2_CTRL_TYPE_MENU,
			.name    = "Compression Target",
			.minimum = 0,
			.maximum = 1,
			.step    = 1,
#define COMP_TARGET_DEF CPIA_COMPRESSION_TARGET_QUALITY
			.default_value = COMP_TARGET_DEF,
		},
		.set = sd_setcomptarget,
		.get = sd_getcomptarget,
	},
};

static const struct v4l2_pix_format mode[] = {
	{160, 120, V4L2_PIX_FMT_CPIA1, V4L2_FIELD_NONE,
		/* The sizeimage is trial and error, as with low framerates
		   the camera will pad out usb frames, making the image
		   data larger then strictly necessary */
		.bytesperline = 160,
		.sizeimage = 65536,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3},
	{176, 144, V4L2_PIX_FMT_CPIA1, V4L2_FIELD_NONE,
		.bytesperline = 172,
		.sizeimage = 65536,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{320, 240, V4L2_PIX_FMT_CPIA1, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 262144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_CPIA1, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 262144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

/**********************************************************************
 *
 * General functions
 *
 **********************************************************************/

static int cpia_usb_transferCmd(struct gspca_dev *gspca_dev, u8 *command)
{
	u8 requesttype;
	unsigned int pipe;
	int ret, databytes = command[6] | (command[7] << 8);
	/* Sometimes we see spurious EPIPE errors */
	int retries = 3;

	if (command[0] == DATA_IN) {
		pipe = usb_rcvctrlpipe(gspca_dev->dev, 0);
		requesttype = USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	} else if (command[0] == DATA_OUT) {
		pipe = usb_sndctrlpipe(gspca_dev->dev, 0);
		requesttype = USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	} else {
		PDEBUG(D_ERR, "Unexpected first byte of command: %x",
		       command[0]);
		return -EINVAL;
	}

retry:
	ret = usb_control_msg(gspca_dev->dev, pipe,
			      command[1],
			      requesttype,
			      command[2] | (command[3] << 8),
			      command[4] | (command[5] << 8),
			      gspca_dev->usb_buf, databytes, 1000);

	if (ret < 0)
		err("usb_control_msg %02x, error %d", command[1],
		       ret);

	if (ret == -EPIPE && retries > 0) {
		retries--;
		goto retry;
	}

	return (ret < 0) ? ret : 0;
}

/* send an arbitrary command to the camera */
static int do_command(struct gspca_dev *gspca_dev, u16 command,
		      u8 a, u8 b, u8 c, u8 d)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret, datasize;
	u8 cmd[8];

	switch (command) {
	case CPIA_COMMAND_GetCPIAVersion:
	case CPIA_COMMAND_GetPnPID:
	case CPIA_COMMAND_GetCameraStatus:
	case CPIA_COMMAND_GetVPVersion:
	case CPIA_COMMAND_GetColourParams:
	case CPIA_COMMAND_GetColourBalance:
	case CPIA_COMMAND_GetExposure:
		datasize = 8;
		break;
	case CPIA_COMMAND_ReadMCPorts:
	case CPIA_COMMAND_ReadVCRegs:
		datasize = 4;
		break;
	default:
		datasize = 0;
		break;
	}

	cmd[0] = command >> 8;
	cmd[1] = command & 0xff;
	cmd[2] = a;
	cmd[3] = b;
	cmd[4] = c;
	cmd[5] = d;
	cmd[6] = datasize;
	cmd[7] = 0;

	ret = cpia_usb_transferCmd(gspca_dev, cmd);
	if (ret)
		return ret;

	switch (command) {
	case CPIA_COMMAND_GetCPIAVersion:
		sd->params.version.firmwareVersion = gspca_dev->usb_buf[0];
		sd->params.version.firmwareRevision = gspca_dev->usb_buf[1];
		sd->params.version.vcVersion = gspca_dev->usb_buf[2];
		sd->params.version.vcRevision = gspca_dev->usb_buf[3];
		break;
	case CPIA_COMMAND_GetPnPID:
		sd->params.pnpID.vendor =
			gspca_dev->usb_buf[0] | (gspca_dev->usb_buf[1] << 8);
		sd->params.pnpID.product =
			gspca_dev->usb_buf[2] | (gspca_dev->usb_buf[3] << 8);
		sd->params.pnpID.deviceRevision =
			gspca_dev->usb_buf[4] | (gspca_dev->usb_buf[5] << 8);
		break;
	case CPIA_COMMAND_GetCameraStatus:
		sd->params.status.systemState = gspca_dev->usb_buf[0];
		sd->params.status.grabState = gspca_dev->usb_buf[1];
		sd->params.status.streamState = gspca_dev->usb_buf[2];
		sd->params.status.fatalError = gspca_dev->usb_buf[3];
		sd->params.status.cmdError = gspca_dev->usb_buf[4];
		sd->params.status.debugFlags = gspca_dev->usb_buf[5];
		sd->params.status.vpStatus = gspca_dev->usb_buf[6];
		sd->params.status.errorCode = gspca_dev->usb_buf[7];
		break;
	case CPIA_COMMAND_GetVPVersion:
		sd->params.vpVersion.vpVersion = gspca_dev->usb_buf[0];
		sd->params.vpVersion.vpRevision = gspca_dev->usb_buf[1];
		sd->params.vpVersion.cameraHeadID =
			gspca_dev->usb_buf[2] | (gspca_dev->usb_buf[3] << 8);
		break;
	case CPIA_COMMAND_GetColourParams:
		sd->params.colourParams.brightness = gspca_dev->usb_buf[0];
		sd->params.colourParams.contrast = gspca_dev->usb_buf[1];
		sd->params.colourParams.saturation = gspca_dev->usb_buf[2];
		break;
	case CPIA_COMMAND_GetColourBalance:
		sd->params.colourBalance.redGain = gspca_dev->usb_buf[0];
		sd->params.colourBalance.greenGain = gspca_dev->usb_buf[1];
		sd->params.colourBalance.blueGain = gspca_dev->usb_buf[2];
		break;
	case CPIA_COMMAND_GetExposure:
		sd->params.exposure.gain = gspca_dev->usb_buf[0];
		sd->params.exposure.fineExp = gspca_dev->usb_buf[1];
		sd->params.exposure.coarseExpLo = gspca_dev->usb_buf[2];
		sd->params.exposure.coarseExpHi = gspca_dev->usb_buf[3];
		sd->params.exposure.redComp = gspca_dev->usb_buf[4];
		sd->params.exposure.green1Comp = gspca_dev->usb_buf[5];
		sd->params.exposure.green2Comp = gspca_dev->usb_buf[6];
		sd->params.exposure.blueComp = gspca_dev->usb_buf[7];
		break;

	case CPIA_COMMAND_ReadMCPorts:
		if (!sd->params.qx3.qx3_detected)
			break;
		/* test button press */
		sd->params.qx3.button = ((gspca_dev->usb_buf[1] & 0x02) == 0);
		if (sd->params.qx3.button) {
			/* button pressed - unlock the latch */
			do_command(gspca_dev, CPIA_COMMAND_WriteMCPort,
				   3, 0xdf, 0xdf, 0);
			do_command(gspca_dev, CPIA_COMMAND_WriteMCPort,
				   3, 0xff, 0xff, 0);
		}

		/* test whether microscope is cradled */
		sd->params.qx3.cradled = ((gspca_dev->usb_buf[2] & 0x40) == 0);
		break;
	}

	return 0;
}

/* send a command to the camera with an additional data transaction */
static int do_command_extended(struct gspca_dev *gspca_dev, u16 command,
			       u8 a, u8 b, u8 c, u8 d,
			       u8 e, u8 f, u8 g, u8 h,
			       u8 i, u8 j, u8 k, u8 l)
{
	u8 cmd[8];

	cmd[0] = command >> 8;
	cmd[1] = command & 0xff;
	cmd[2] = a;
	cmd[3] = b;
	cmd[4] = c;
	cmd[5] = d;
	cmd[6] = 8;
	cmd[7] = 0;
	gspca_dev->usb_buf[0] = e;
	gspca_dev->usb_buf[1] = f;
	gspca_dev->usb_buf[2] = g;
	gspca_dev->usb_buf[3] = h;
	gspca_dev->usb_buf[4] = i;
	gspca_dev->usb_buf[5] = j;
	gspca_dev->usb_buf[6] = k;
	gspca_dev->usb_buf[7] = l;

	return cpia_usb_transferCmd(gspca_dev, cmd);
}

/*  find_over_exposure
 *  Finds a suitable value of OverExposure for use with SetFlickerCtrl
 *  Some calculation is required because this value changes with the brightness
 *  set with SetColourParameters
 *
 *  Parameters: Brightness - last brightness value set with SetColourParameters
 *
 *  Returns: OverExposure value to use with SetFlickerCtrl
 */
#define FLICKER_MAX_EXPOSURE                    250
#define FLICKER_ALLOWABLE_OVER_EXPOSURE         146
#define FLICKER_BRIGHTNESS_CONSTANT             59
static int find_over_exposure(int brightness)
{
	int MaxAllowableOverExposure, OverExposure;

	MaxAllowableOverExposure = FLICKER_MAX_EXPOSURE - brightness -
				   FLICKER_BRIGHTNESS_CONSTANT;

	if (MaxAllowableOverExposure < FLICKER_ALLOWABLE_OVER_EXPOSURE)
		OverExposure = MaxAllowableOverExposure;
	else
		OverExposure = FLICKER_ALLOWABLE_OVER_EXPOSURE;

	return OverExposure;
}
#undef FLICKER_MAX_EXPOSURE
#undef FLICKER_ALLOWABLE_OVER_EXPOSURE
#undef FLICKER_BRIGHTNESS_CONSTANT

/* initialise cam_data structure  */
static void reset_camera_params(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam_params *params = &sd->params;

	/* The following parameter values are the defaults from
	 * "Software Developer's Guide for CPiA Cameras".  Any changes
	 * to the defaults are noted in comments. */
	params->colourParams.brightness = BRIGHTNESS_DEF;
	params->colourParams.contrast = CONTRAST_DEF;
	params->colourParams.saturation = SATURATION_DEF;
	params->exposure.gainMode = 4;
	params->exposure.expMode = 2;		/* AEC */
	params->exposure.compMode = 1;
	params->exposure.centreWeight = 1;
	params->exposure.gain = 0;
	params->exposure.fineExp = 0;
	params->exposure.coarseExpLo = 185;
	params->exposure.coarseExpHi = 0;
	params->exposure.redComp = COMP_RED;
	params->exposure.green1Comp = COMP_GREEN1;
	params->exposure.green2Comp = COMP_GREEN2;
	params->exposure.blueComp = COMP_BLUE;
	params->colourBalance.balanceMode = 2;	/* ACB */
	params->colourBalance.redGain = 32;
	params->colourBalance.greenGain = 6;
	params->colourBalance.blueGain = 92;
	params->apcor.gain1 = 0x18;
	params->apcor.gain2 = 0x16;
	params->apcor.gain4 = 0x24;
	params->apcor.gain8 = 0x34;
	params->flickerControl.flickerMode = 0;
	params->flickerControl.disabled = 1;

	params->flickerControl.coarseJump =
		flicker_jumps[sd->mainsFreq]
			     [params->sensorFps.baserate]
			     [params->sensorFps.divisor];
	params->flickerControl.allowableOverExposure =
		find_over_exposure(params->colourParams.brightness);
	params->vlOffset.gain1 = 20;
	params->vlOffset.gain2 = 24;
	params->vlOffset.gain4 = 26;
	params->vlOffset.gain8 = 26;
	params->compressionParams.hysteresis = 3;
	params->compressionParams.threshMax = 11;
	params->compressionParams.smallStep = 1;
	params->compressionParams.largeStep = 3;
	params->compressionParams.decimationHysteresis = 2;
	params->compressionParams.frDiffStepThresh = 5;
	params->compressionParams.qDiffStepThresh = 3;
	params->compressionParams.decimationThreshMod = 2;
	/* End of default values from Software Developer's Guide */

	/* Set Sensor FPS to 15fps. This seems better than 30fps
	 * for indoor lighting. */
	params->sensorFps.divisor = 1;
	params->sensorFps.baserate = 1;

	params->yuvThreshold.yThreshold = 6; /* From windows driver */
	params->yuvThreshold.uvThreshold = 6; /* From windows driver */

	params->format.subSample = SUBSAMPLE_420;
	params->format.yuvOrder = YUVORDER_YUYV;

	params->compression.mode = CPIA_COMPRESSION_AUTO;
	params->compression.decimation = NO_DECIMATION;

	params->compressionTarget.frTargeting = COMP_TARGET_DEF;
	params->compressionTarget.targetFR = 15; /* From windows driver */
	params->compressionTarget.targetQ = 5; /* From windows driver */

	params->qx3.qx3_detected = 0;
	params->qx3.toplight = 0;
	params->qx3.bottomlight = 0;
	params->qx3.button = 0;
	params->qx3.cradled = 0;
}

static void printstatus(struct cam_params *params)
{
	PDEBUG(D_PROBE, "status: %02x %02x %02x %02x %02x %02x %02x %02x",
	       params->status.systemState, params->status.grabState,
	       params->status.streamState, params->status.fatalError,
	       params->status.cmdError, params->status.debugFlags,
	       params->status.vpStatus, params->status.errorCode);
}

static int goto_low_power(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	ret = do_command(gspca_dev, CPIA_COMMAND_GotoLoPower, 0, 0, 0, 0);
	if (ret)
		return ret;

	ret = do_command(gspca_dev, CPIA_COMMAND_GetCameraStatus, 0, 0, 0, 0);
	if (ret)
		return ret;

	if (sd->params.status.systemState != LO_POWER_STATE) {
		if (sd->params.status.systemState != WARM_BOOT_STATE) {
			PDEBUG(D_ERR,
			       "unexpected state after lo power cmd: %02x",
			       sd->params.status.systemState);
			printstatus(&sd->params);
		}
		return -EIO;
	}

	PDEBUG(D_CONF, "camera now in LOW power state");
	return 0;
}

static int goto_high_power(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	ret = do_command(gspca_dev, CPIA_COMMAND_GotoHiPower, 0, 0, 0, 0);
	if (ret)
		return ret;

	msleep_interruptible(40);	/* windows driver does it too */

	if (signal_pending(current))
		return -EINTR;

	do_command(gspca_dev, CPIA_COMMAND_GetCameraStatus, 0, 0, 0, 0);
	if (ret)
		return ret;

	if (sd->params.status.systemState != HI_POWER_STATE) {
		PDEBUG(D_ERR, "unexpected state after hi power cmd: %02x",
			       sd->params.status.systemState);
		printstatus(&sd->params);
		return -EIO;
	}

	PDEBUG(D_CONF, "camera now in HIGH power state");
	return 0;
}

static int get_version_information(struct gspca_dev *gspca_dev)
{
	int ret;

	/* GetCPIAVersion */
	ret = do_command(gspca_dev, CPIA_COMMAND_GetCPIAVersion, 0, 0, 0, 0);
	if (ret)
		return ret;

	/* GetPnPID */
	return do_command(gspca_dev, CPIA_COMMAND_GetPnPID, 0, 0, 0, 0);
}

static int save_camera_state(struct gspca_dev *gspca_dev)
{
	int ret;

	ret = do_command(gspca_dev, CPIA_COMMAND_GetColourBalance, 0, 0, 0, 0);
	if (ret)
		return ret;

	return do_command(gspca_dev, CPIA_COMMAND_GetExposure, 0, 0, 0, 0);
}

static int command_setformat(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	ret = do_command(gspca_dev, CPIA_COMMAND_SetFormat,
			 sd->params.format.videoSize,
			 sd->params.format.subSample,
			 sd->params.format.yuvOrder, 0);
	if (ret)
		return ret;

	return do_command(gspca_dev, CPIA_COMMAND_SetROI,
			  sd->params.roi.colStart, sd->params.roi.colEnd,
			  sd->params.roi.rowStart, sd->params.roi.rowEnd);
}

static int command_setcolourparams(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	return do_command(gspca_dev, CPIA_COMMAND_SetColourParams,
			  sd->params.colourParams.brightness,
			  sd->params.colourParams.contrast,
			  sd->params.colourParams.saturation, 0);
}

static int command_setapcor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	return do_command(gspca_dev, CPIA_COMMAND_SetApcor,
			  sd->params.apcor.gain1,
			  sd->params.apcor.gain2,
			  sd->params.apcor.gain4,
			  sd->params.apcor.gain8);
}

static int command_setvloffset(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	return do_command(gspca_dev, CPIA_COMMAND_SetVLOffset,
			  sd->params.vlOffset.gain1,
			  sd->params.vlOffset.gain2,
			  sd->params.vlOffset.gain4,
			  sd->params.vlOffset.gain8);
}

static int command_setexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	ret = do_command_extended(gspca_dev, CPIA_COMMAND_SetExposure,
				  sd->params.exposure.gainMode,
				  1,
				  sd->params.exposure.compMode,
				  sd->params.exposure.centreWeight,
				  sd->params.exposure.gain,
				  sd->params.exposure.fineExp,
				  sd->params.exposure.coarseExpLo,
				  sd->params.exposure.coarseExpHi,
				  sd->params.exposure.redComp,
				  sd->params.exposure.green1Comp,
				  sd->params.exposure.green2Comp,
				  sd->params.exposure.blueComp);
	if (ret)
		return ret;

	if (sd->params.exposure.expMode != 1) {
		ret = do_command_extended(gspca_dev, CPIA_COMMAND_SetExposure,
					  0,
					  sd->params.exposure.expMode,
					  0, 0,
					  sd->params.exposure.gain,
					  sd->params.exposure.fineExp,
					  sd->params.exposure.coarseExpLo,
					  sd->params.exposure.coarseExpHi,
					  0, 0, 0, 0);
	}

	return ret;
}

static int command_setcolourbalance(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->params.colourBalance.balanceMode == 1) {
		int ret;

		ret = do_command(gspca_dev, CPIA_COMMAND_SetColourBalance,
				 1,
				 sd->params.colourBalance.redGain,
				 sd->params.colourBalance.greenGain,
				 sd->params.colourBalance.blueGain);
		if (ret)
			return ret;

		return do_command(gspca_dev, CPIA_COMMAND_SetColourBalance,
				  3, 0, 0, 0);
	}
	if (sd->params.colourBalance.balanceMode == 2) {
		return do_command(gspca_dev, CPIA_COMMAND_SetColourBalance,
				  2, 0, 0, 0);
	}
	if (sd->params.colourBalance.balanceMode == 3) {
		return do_command(gspca_dev, CPIA_COMMAND_SetColourBalance,
				  3, 0, 0, 0);
	}

	return -EINVAL;
}

static int command_setcompressiontarget(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return do_command(gspca_dev, CPIA_COMMAND_SetCompressionTarget,
			  sd->params.compressionTarget.frTargeting,
			  sd->params.compressionTarget.targetFR,
			  sd->params.compressionTarget.targetQ, 0);
}

static int command_setyuvtresh(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return do_command(gspca_dev, CPIA_COMMAND_SetYUVThresh,
			  sd->params.yuvThreshold.yThreshold,
			  sd->params.yuvThreshold.uvThreshold, 0, 0);
}

static int command_setcompressionparams(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return do_command_extended(gspca_dev,
			    CPIA_COMMAND_SetCompressionParams,
			    0, 0, 0, 0,
			    sd->params.compressionParams.hysteresis,
			    sd->params.compressionParams.threshMax,
			    sd->params.compressionParams.smallStep,
			    sd->params.compressionParams.largeStep,
			    sd->params.compressionParams.decimationHysteresis,
			    sd->params.compressionParams.frDiffStepThresh,
			    sd->params.compressionParams.qDiffStepThresh,
			    sd->params.compressionParams.decimationThreshMod);
}

static int command_setcompression(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return do_command(gspca_dev, CPIA_COMMAND_SetCompression,
			  sd->params.compression.mode,
			  sd->params.compression.decimation, 0, 0);
}

static int command_setsensorfps(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return do_command(gspca_dev, CPIA_COMMAND_SetSensorFPS,
			  sd->params.sensorFps.divisor,
			  sd->params.sensorFps.baserate, 0, 0);
}

static int command_setflickerctrl(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return do_command(gspca_dev, CPIA_COMMAND_SetFlickerCtrl,
			  sd->params.flickerControl.flickerMode,
			  sd->params.flickerControl.coarseJump,
			  sd->params.flickerControl.allowableOverExposure,
			  0);
}

static int command_setecptiming(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return do_command(gspca_dev, CPIA_COMMAND_SetECPTiming,
			  sd->params.ecpTiming, 0, 0, 0);
}

static int command_pause(struct gspca_dev *gspca_dev)
{
	return do_command(gspca_dev, CPIA_COMMAND_EndStreamCap, 0, 0, 0, 0);
}

static int command_resume(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return do_command(gspca_dev, CPIA_COMMAND_InitStreamCap,
			  0, sd->params.streamStartLine, 0, 0);
}

static int command_setlights(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret, p1, p2;

	if (!sd->params.qx3.qx3_detected)
		return 0;

	p1 = (sd->params.qx3.bottomlight == 0) << 1;
	p2 = (sd->params.qx3.toplight == 0) << 3;

	ret = do_command(gspca_dev, CPIA_COMMAND_WriteVCReg,
			 0x90, 0x8f, 0x50, 0);
	if (ret)
		return ret;

	return do_command(gspca_dev, CPIA_COMMAND_WriteMCPort, 2, 0,
			  p1 | p2 | 0xe0, 0);
}

static int set_flicker(struct gspca_dev *gspca_dev, int on, int apply)
{
	/* Everything in here is from the Windows driver */
/* define for compgain calculation */
#if 0
#define COMPGAIN(base, curexp, newexp) \
    (u8) ((((float) base - 128.0) * ((float) curexp / (float) newexp)) + 128.5)
#define EXP_FROM_COMP(basecomp, curcomp, curexp) \
    (u16)((float)curexp * (float)(u8)(curcomp + 128) / \
    (float)(u8)(basecomp - 128))
#else
  /* equivalent functions without floating point math */
#define COMPGAIN(base, curexp, newexp) \
    (u8)(128 + (((u32)(2*(base-128)*curexp + newexp)) / (2 * newexp)))
#define EXP_FROM_COMP(basecomp, curcomp, curexp) \
    (u16)(((u32)(curexp * (u8)(curcomp + 128)) / (u8)(basecomp - 128)))
#endif

	struct sd *sd = (struct sd *) gspca_dev;
	int currentexp = sd->params.exposure.coarseExpLo +
			 sd->params.exposure.coarseExpHi * 256;
	int ret, startexp;

	if (on) {
		int cj = sd->params.flickerControl.coarseJump;
		sd->params.flickerControl.flickerMode = 1;
		sd->params.flickerControl.disabled = 0;
		if (sd->params.exposure.expMode != 2) {
			sd->params.exposure.expMode = 2;
			sd->exposure_status = EXPOSURE_NORMAL;
		}
		currentexp = currentexp << sd->params.exposure.gain;
		sd->params.exposure.gain = 0;
		/* round down current exposure to nearest value */
		startexp = (currentexp + ROUND_UP_EXP_FOR_FLICKER) / cj;
		if (startexp < 1)
			startexp = 1;
		startexp = (startexp * cj) - 1;
		if (FIRMWARE_VERSION(1, 2))
			while (startexp > MAX_EXP_102)
				startexp -= cj;
		else
			while (startexp > MAX_EXP)
				startexp -= cj;
		sd->params.exposure.coarseExpLo = startexp & 0xff;
		sd->params.exposure.coarseExpHi = startexp >> 8;
		if (currentexp > startexp) {
			if (currentexp > (2 * startexp))
				currentexp = 2 * startexp;
			sd->params.exposure.redComp =
				COMPGAIN(COMP_RED, currentexp, startexp);
			sd->params.exposure.green1Comp =
				COMPGAIN(COMP_GREEN1, currentexp, startexp);
			sd->params.exposure.green2Comp =
				COMPGAIN(COMP_GREEN2, currentexp, startexp);
			sd->params.exposure.blueComp =
				COMPGAIN(COMP_BLUE, currentexp, startexp);
		} else {
			sd->params.exposure.redComp = COMP_RED;
			sd->params.exposure.green1Comp = COMP_GREEN1;
			sd->params.exposure.green2Comp = COMP_GREEN2;
			sd->params.exposure.blueComp = COMP_BLUE;
		}
		if (FIRMWARE_VERSION(1, 2))
			sd->params.exposure.compMode = 0;
		else
			sd->params.exposure.compMode = 1;

		sd->params.apcor.gain1 = 0x18;
		sd->params.apcor.gain2 = 0x18;
		sd->params.apcor.gain4 = 0x16;
		sd->params.apcor.gain8 = 0x14;
	} else {
		sd->params.flickerControl.flickerMode = 0;
		sd->params.flickerControl.disabled = 1;
		/* Average equivalent coarse for each comp channel */
		startexp = EXP_FROM_COMP(COMP_RED,
				sd->params.exposure.redComp, currentexp);
		startexp += EXP_FROM_COMP(COMP_GREEN1,
				sd->params.exposure.green1Comp, currentexp);
		startexp += EXP_FROM_COMP(COMP_GREEN2,
				sd->params.exposure.green2Comp, currentexp);
		startexp += EXP_FROM_COMP(COMP_BLUE,
				sd->params.exposure.blueComp, currentexp);
		startexp = startexp >> 2;
		while (startexp > MAX_EXP && sd->params.exposure.gain <
		       sd->params.exposure.gainMode - 1) {
			startexp = startexp >> 1;
			++sd->params.exposure.gain;
		}
		if (FIRMWARE_VERSION(1, 2) && startexp > MAX_EXP_102)
			startexp = MAX_EXP_102;
		if (startexp > MAX_EXP)
			startexp = MAX_EXP;
		sd->params.exposure.coarseExpLo = startexp & 0xff;
		sd->params.exposure.coarseExpHi = startexp >> 8;
		sd->params.exposure.redComp = COMP_RED;
		sd->params.exposure.green1Comp = COMP_GREEN1;
		sd->params.exposure.green2Comp = COMP_GREEN2;
		sd->params.exposure.blueComp = COMP_BLUE;
		sd->params.exposure.compMode = 1;
		sd->params.apcor.gain1 = 0x18;
		sd->params.apcor.gain2 = 0x16;
		sd->params.apcor.gain4 = 0x24;
		sd->params.apcor.gain8 = 0x34;
	}
	sd->params.vlOffset.gain1 = 20;
	sd->params.vlOffset.gain2 = 24;
	sd->params.vlOffset.gain4 = 26;
	sd->params.vlOffset.gain8 = 26;

	if (apply) {
		ret = command_setexposure(gspca_dev);
		if (ret)
			return ret;

		ret = command_setapcor(gspca_dev);
		if (ret)
			return ret;

		ret = command_setvloffset(gspca_dev);
		if (ret)
			return ret;

		ret = command_setflickerctrl(gspca_dev);
		if (ret)
			return ret;
	}

	return 0;
#undef EXP_FROM_COMP
#undef COMPGAIN
}

/* monitor the exposure and adjust the sensor frame rate if needed */
static void monitor_exposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 exp_acc, bcomp, gain, coarseL, cmd[8];
	int ret, light_exp, dark_exp, very_dark_exp;
	int old_exposure, new_exposure, framerate;
	int setfps = 0, setexp = 0, setflicker = 0;

	/* get necessary stats and register settings from camera */
	/* do_command can't handle this, so do it ourselves */
	cmd[0] = CPIA_COMMAND_ReadVPRegs >> 8;
	cmd[1] = CPIA_COMMAND_ReadVPRegs & 0xff;
	cmd[2] = 30;
	cmd[3] = 4;
	cmd[4] = 9;
	cmd[5] = 8;
	cmd[6] = 8;
	cmd[7] = 0;
	ret = cpia_usb_transferCmd(gspca_dev, cmd);
	if (ret) {
		err("ReadVPRegs(30,4,9,8) - failed: %d", ret);
		return;
	}
	exp_acc = gspca_dev->usb_buf[0];
	bcomp = gspca_dev->usb_buf[1];
	gain = gspca_dev->usb_buf[2];
	coarseL = gspca_dev->usb_buf[3];

	light_exp = sd->params.colourParams.brightness +
		    TC - 50 + EXP_ACC_LIGHT;
	if (light_exp > 255)
		light_exp = 255;
	dark_exp = sd->params.colourParams.brightness +
		   TC - 50 - EXP_ACC_DARK;
	if (dark_exp < 0)
		dark_exp = 0;
	very_dark_exp = dark_exp / 2;

	old_exposure = sd->params.exposure.coarseExpHi * 256 +
		       sd->params.exposure.coarseExpLo;

	if (!sd->params.flickerControl.disabled) {
		/* Flicker control on */
		int max_comp = FIRMWARE_VERSION(1, 2) ? MAX_COMP :
							HIGH_COMP_102;
		bcomp += 128;	/* decode */
		if (bcomp >= max_comp && exp_acc < dark_exp) {
			/* dark */
			if (exp_acc < very_dark_exp) {
				/* very dark */
				if (sd->exposure_status == EXPOSURE_VERY_DARK)
					++sd->exposure_count;
				else {
					sd->exposure_status =
						EXPOSURE_VERY_DARK;
					sd->exposure_count = 1;
				}
			} else {
				/* just dark */
				if (sd->exposure_status == EXPOSURE_DARK)
					++sd->exposure_count;
				else {
					sd->exposure_status = EXPOSURE_DARK;
					sd->exposure_count = 1;
				}
			}
		} else if (old_exposure <= LOW_EXP || exp_acc > light_exp) {
			/* light */
			if (old_exposure <= VERY_LOW_EXP) {
				/* very light */
				if (sd->exposure_status == EXPOSURE_VERY_LIGHT)
					++sd->exposure_count;
				else {
					sd->exposure_status =
						EXPOSURE_VERY_LIGHT;
					sd->exposure_count = 1;
				}
			} else {
				/* just light */
				if (sd->exposure_status == EXPOSURE_LIGHT)
					++sd->exposure_count;
				else {
					sd->exposure_status = EXPOSURE_LIGHT;
					sd->exposure_count = 1;
				}
			}
		} else {
			/* not dark or light */
			sd->exposure_status = EXPOSURE_NORMAL;
		}
	} else {
		/* Flicker control off */
		if (old_exposure >= MAX_EXP && exp_acc < dark_exp) {
			/* dark */
			if (exp_acc < very_dark_exp) {
				/* very dark */
				if (sd->exposure_status == EXPOSURE_VERY_DARK)
					++sd->exposure_count;
				else {
					sd->exposure_status =
						EXPOSURE_VERY_DARK;
					sd->exposure_count = 1;
				}
			} else {
				/* just dark */
				if (sd->exposure_status == EXPOSURE_DARK)
					++sd->exposure_count;
				else {
					sd->exposure_status = EXPOSURE_DARK;
					sd->exposure_count = 1;
				}
			}
		} else if (old_exposure <= LOW_EXP || exp_acc > light_exp) {
			/* light */
			if (old_exposure <= VERY_LOW_EXP) {
				/* very light */
				if (sd->exposure_status == EXPOSURE_VERY_LIGHT)
					++sd->exposure_count;
				else {
					sd->exposure_status =
						EXPOSURE_VERY_LIGHT;
					sd->exposure_count = 1;
				}
			} else {
				/* just light */
				if (sd->exposure_status == EXPOSURE_LIGHT)
					++sd->exposure_count;
				else {
					sd->exposure_status = EXPOSURE_LIGHT;
					sd->exposure_count = 1;
				}
			}
		} else {
			/* not dark or light */
			sd->exposure_status = EXPOSURE_NORMAL;
		}
	}

	framerate = atomic_read(&sd->fps);
	if (framerate > 30 || framerate < 1)
		framerate = 1;

	if (!sd->params.flickerControl.disabled) {
		/* Flicker control on */
		if ((sd->exposure_status == EXPOSURE_VERY_DARK ||
		     sd->exposure_status == EXPOSURE_DARK) &&
		    sd->exposure_count >= DARK_TIME * framerate &&
		    sd->params.sensorFps.divisor < 3) {

			/* dark for too long */
			++sd->params.sensorFps.divisor;
			setfps = 1;

			sd->params.flickerControl.coarseJump =
				flicker_jumps[sd->mainsFreq]
					     [sd->params.sensorFps.baserate]
					     [sd->params.sensorFps.divisor];
			setflicker = 1;

			new_exposure = sd->params.flickerControl.coarseJump-1;
			while (new_exposure < old_exposure / 2)
				new_exposure +=
					sd->params.flickerControl.coarseJump;
			sd->params.exposure.coarseExpLo = new_exposure & 0xff;
			sd->params.exposure.coarseExpHi = new_exposure >> 8;
			setexp = 1;
			sd->exposure_status = EXPOSURE_NORMAL;
			PDEBUG(D_CONF, "Automatically decreasing sensor_fps");

		} else if ((sd->exposure_status == EXPOSURE_VERY_LIGHT ||
			    sd->exposure_status == EXPOSURE_LIGHT) &&
			   sd->exposure_count >= LIGHT_TIME * framerate &&
			   sd->params.sensorFps.divisor > 0) {

			/* light for too long */
			int max_exp = FIRMWARE_VERSION(1, 2) ? MAX_EXP_102 :
							       MAX_EXP;
			--sd->params.sensorFps.divisor;
			setfps = 1;

			sd->params.flickerControl.coarseJump =
				flicker_jumps[sd->mainsFreq]
					     [sd->params.sensorFps.baserate]
					     [sd->params.sensorFps.divisor];
			setflicker = 1;

			new_exposure = sd->params.flickerControl.coarseJump-1;
			while (new_exposure < 2 * old_exposure &&
			       new_exposure +
			       sd->params.flickerControl.coarseJump < max_exp)
				new_exposure +=
					sd->params.flickerControl.coarseJump;
			sd->params.exposure.coarseExpLo = new_exposure & 0xff;
			sd->params.exposure.coarseExpHi = new_exposure >> 8;
			setexp = 1;
			sd->exposure_status = EXPOSURE_NORMAL;
			PDEBUG(D_CONF, "Automatically increasing sensor_fps");
		}
	} else {
		/* Flicker control off */
		if ((sd->exposure_status == EXPOSURE_VERY_DARK ||
		     sd->exposure_status == EXPOSURE_DARK) &&
		    sd->exposure_count >= DARK_TIME * framerate &&
		    sd->params.sensorFps.divisor < 3) {

			/* dark for too long */
			++sd->params.sensorFps.divisor;
			setfps = 1;

			if (sd->params.exposure.gain > 0) {
				--sd->params.exposure.gain;
				setexp = 1;
			}
			sd->exposure_status = EXPOSURE_NORMAL;
			PDEBUG(D_CONF, "Automatically decreasing sensor_fps");

		} else if ((sd->exposure_status == EXPOSURE_VERY_LIGHT ||
			    sd->exposure_status == EXPOSURE_LIGHT) &&
			   sd->exposure_count >= LIGHT_TIME * framerate &&
			   sd->params.sensorFps.divisor > 0) {

			/* light for too long */
			--sd->params.sensorFps.divisor;
			setfps = 1;

			if (sd->params.exposure.gain <
			    sd->params.exposure.gainMode - 1) {
				++sd->params.exposure.gain;
				setexp = 1;
			}
			sd->exposure_status = EXPOSURE_NORMAL;
			PDEBUG(D_CONF, "Automatically increasing sensor_fps");
		}
	}

	if (setexp)
		command_setexposure(gspca_dev);

	if (setfps)
		command_setsensorfps(gspca_dev);

	if (setflicker)
		command_setflickerctrl(gspca_dev);
}

/*-----------------------------------------------------------------*/
/* if flicker is switched off, this function switches it back on.It checks,
   however, that conditions are suitable before restarting it.
   This should only be called for firmware version 1.2.

   It also adjust the colour balance when an exposure step is detected - as
   long as flicker is running
*/
static void restart_flicker(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int cam_exposure, old_exp;

	if (!FIRMWARE_VERSION(1, 2))
		return;

	cam_exposure = atomic_read(&sd->cam_exposure);

	if (sd->params.flickerControl.flickerMode == 0 ||
	    cam_exposure == 0)
		return;

	old_exp = sd->params.exposure.coarseExpLo +
		  sd->params.exposure.coarseExpHi*256;
	/*
	  see how far away camera exposure is from a valid
	  flicker exposure value
	*/
	cam_exposure %= sd->params.flickerControl.coarseJump;
	if (!sd->params.flickerControl.disabled &&
	    cam_exposure <= sd->params.flickerControl.coarseJump - 3) {
		/* Flicker control auto-disabled */
		sd->params.flickerControl.disabled = 1;
	}

	if (sd->params.flickerControl.disabled &&
	    old_exp > sd->params.flickerControl.coarseJump +
		      ROUND_UP_EXP_FOR_FLICKER) {
		/* exposure is now high enough to switch
		   flicker control back on */
		set_flicker(gspca_dev, 1, 1);
	}
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct cam *cam;

	reset_camera_params(gspca_dev);

	PDEBUG(D_PROBE, "cpia CPiA camera detected (vid/pid 0x%04X:0x%04X)",
	       id->idVendor, id->idProduct);

	cam = &gspca_dev->cam;
	cam->cam_mode = mode;
	cam->nmodes = ARRAY_SIZE(mode);

	sd_setfreq(gspca_dev, FREQ_DEF);

	return 0;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int priv, ret;

	/* Start the camera in low power mode */
	if (goto_low_power(gspca_dev)) {
		if (sd->params.status.systemState != WARM_BOOT_STATE) {
			PDEBUG(D_ERR, "unexpected systemstate: %02x",
			       sd->params.status.systemState);
			printstatus(&sd->params);
			return -ENODEV;
		}

		/* FIXME: this is just dirty trial and error */
		ret = goto_high_power(gspca_dev);
		if (ret)
			return ret;

		ret = do_command(gspca_dev, CPIA_COMMAND_DiscardFrame,
				 0, 0, 0, 0);
		if (ret)
			return ret;

		ret = goto_low_power(gspca_dev);
		if (ret)
			return ret;
	}

	/* procedure described in developer's guide p3-28 */

	/* Check the firmware version. */
	sd->params.version.firmwareVersion = 0;
	get_version_information(gspca_dev);
	if (sd->params.version.firmwareVersion != 1) {
		PDEBUG(D_ERR, "only firmware version 1 is supported (got: %d)",
		       sd->params.version.firmwareVersion);
		return -ENODEV;
	}

	/* A bug in firmware 1-02 limits gainMode to 2 */
	if (sd->params.version.firmwareRevision <= 2 &&
	    sd->params.exposure.gainMode > 2) {
		sd->params.exposure.gainMode = 2;
	}

	/* set QX3 detected flag */
	sd->params.qx3.qx3_detected = (sd->params.pnpID.vendor == 0x0813 &&
				       sd->params.pnpID.product == 0x0001);

	/* The fatal error checking should be done after
	 * the camera powers up (developer's guide p 3-38) */

	/* Set streamState before transition to high power to avoid bug
	 * in firmware 1-02 */
	ret = do_command(gspca_dev, CPIA_COMMAND_ModifyCameraStatus,
			 STREAMSTATE, 0, STREAM_NOT_READY, 0);
	if (ret)
		return ret;

	/* GotoHiPower */
	ret = goto_high_power(gspca_dev);
	if (ret)
		return ret;

	/* Check the camera status */
	ret = do_command(gspca_dev, CPIA_COMMAND_GetCameraStatus, 0, 0, 0, 0);
	if (ret)
		return ret;

	if (sd->params.status.fatalError) {
		PDEBUG(D_ERR, "fatal_error: %04x, vp_status: %04x",
		       sd->params.status.fatalError,
		       sd->params.status.vpStatus);
		return -EIO;
	}

	/* VPVersion can't be retrieved before the camera is in HiPower,
	 * so get it here instead of in get_version_information. */
	ret = do_command(gspca_dev, CPIA_COMMAND_GetVPVersion, 0, 0, 0, 0);
	if (ret)
		return ret;

	/* Determine video mode settings */
	sd->params.streamStartLine = 120;

	priv = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	if (priv & 0x01) { /* crop */
		sd->params.roi.colStart = 2;
		sd->params.roi.rowStart = 6;
	} else {
		sd->params.roi.colStart = 0;
		sd->params.roi.rowStart = 0;
	}

	if (priv & 0x02) { /* quarter */
		sd->params.format.videoSize = VIDEOSIZE_QCIF;
		sd->params.roi.colStart /= 2;
		sd->params.roi.rowStart /= 2;
		sd->params.streamStartLine /= 2;
	} else
		sd->params.format.videoSize = VIDEOSIZE_CIF;

	sd->params.roi.colEnd = sd->params.roi.colStart +
				(gspca_dev->width >> 3);
	sd->params.roi.rowEnd = sd->params.roi.rowStart +
				(gspca_dev->height >> 2);

	/* And now set the camera to a known state */
	ret = do_command(gspca_dev, CPIA_COMMAND_SetGrabMode,
			 CPIA_GRAB_CONTINEOUS, 0, 0, 0);
	if (ret)
		return ret;
	/* We start with compression disabled, as we need one uncompressed
	   frame to handle later compressed frames */
	ret = do_command(gspca_dev, CPIA_COMMAND_SetCompression,
			 CPIA_COMPRESSION_NONE,
			 NO_DECIMATION, 0, 0);
	if (ret)
		return ret;
	ret = command_setcompressiontarget(gspca_dev);
	if (ret)
		return ret;
	ret = command_setcolourparams(gspca_dev);
	if (ret)
		return ret;
	ret = command_setformat(gspca_dev);
	if (ret)
		return ret;
	ret = command_setyuvtresh(gspca_dev);
	if (ret)
		return ret;
	ret = command_setecptiming(gspca_dev);
	if (ret)
		return ret;
	ret = command_setcompressionparams(gspca_dev);
	if (ret)
		return ret;
	ret = command_setexposure(gspca_dev);
	if (ret)
		return ret;
	ret = command_setcolourbalance(gspca_dev);
	if (ret)
		return ret;
	ret = command_setsensorfps(gspca_dev);
	if (ret)
		return ret;
	ret = command_setapcor(gspca_dev);
	if (ret)
		return ret;
	ret = command_setflickerctrl(gspca_dev);
	if (ret)
		return ret;
	ret = command_setvloffset(gspca_dev);
	if (ret)
		return ret;

	/* Start stream */
	ret = command_resume(gspca_dev);
	if (ret)
		return ret;

	/* Wait 6 frames before turning compression on for the sensor to get
	   all settings and AEC/ACB to settle */
	sd->first_frame = 6;
	sd->exposure_status = EXPOSURE_NORMAL;
	sd->exposure_count = 0;
	atomic_set(&sd->cam_exposure, 0);
	atomic_set(&sd->fps, 0);

	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	command_pause(gspca_dev);

	/* save camera state for later open (developers guide ch 3.5.3) */
	save_camera_state(gspca_dev);

	/* GotoLoPower */
	goto_low_power(gspca_dev);

	/* Update the camera status */
	do_command(gspca_dev, CPIA_COMMAND_GetCameraStatus, 0, 0, 0, 0);
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
#ifdef GSPCA_DEBUG
	struct sd *sd = (struct sd *) gspca_dev;
#endif
	int ret;

	/* Start / Stop the camera to make sure we are talking to
	   a supported camera, and to get some information from it
	   to print. */
	ret = sd_start(gspca_dev);
	if (ret)
		return ret;

	/* Ensure the QX3 illuminators' states are restored upon resume,
	   or disable the illuminator controls, if this isn't a QX3 */
	if (sd->params.qx3.qx3_detected)
		command_setlights(gspca_dev);
	else
		gspca_dev->ctrl_dis |=
			((1 << ILLUMINATORS_1_IDX) | (1 << ILLUMINATORS_2_IDX));

	sd_stopN(gspca_dev);

	PDEBUG(D_PROBE, "CPIA Version:             %d.%02d (%d.%d)",
			sd->params.version.firmwareVersion,
			sd->params.version.firmwareRevision,
			sd->params.version.vcVersion,
			sd->params.version.vcRevision);
	PDEBUG(D_PROBE, "CPIA PnP-ID:              %04x:%04x:%04x",
			sd->params.pnpID.vendor, sd->params.pnpID.product,
			sd->params.pnpID.deviceRevision);
	PDEBUG(D_PROBE, "VP-Version:               %d.%d %04x",
			sd->params.vpVersion.vpVersion,
			sd->params.vpVersion.vpRevision,
			sd->params.vpVersion.cameraHeadID);

	return 0;
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,
			int len)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* Check for SOF */
	if (len >= 64 &&
	    data[0] == MAGIC_0 && data[1] == MAGIC_1 &&
	    data[16] == sd->params.format.videoSize &&
	    data[17] == sd->params.format.subSample &&
	    data[18] == sd->params.format.yuvOrder &&
	    data[24] == sd->params.roi.colStart &&
	    data[25] == sd->params.roi.colEnd &&
	    data[26] == sd->params.roi.rowStart &&
	    data[27] == sd->params.roi.rowEnd) {
		u8 *image;

		atomic_set(&sd->cam_exposure, data[39] * 2);
		atomic_set(&sd->fps, data[41]);

		/* Check for proper EOF for last frame */
		image = gspca_dev->image;
		if (image != NULL &&
		    gspca_dev->image_len > 4 &&
		    image[gspca_dev->image_len - 4] == 0xff &&
		    image[gspca_dev->image_len - 3] == 0xff &&
		    image[gspca_dev->image_len - 2] == 0xff &&
		    image[gspca_dev->image_len - 1] == 0xff)
			gspca_frame_add(gspca_dev, LAST_PACKET,
						NULL, 0);

		gspca_frame_add(gspca_dev, FIRST_PACKET, data, len);
		return;
	}

	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

static void sd_dq_callback(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* Set the normal compression settings once we have captured a
	   few uncompressed frames (and AEC has hopefully settled) */
	if (sd->first_frame) {
		sd->first_frame--;
		if (sd->first_frame == 0)
			command_setcompression(gspca_dev);
	}

	/* Switch flicker control back on if it got turned off */
	restart_flicker(gspca_dev);

	/* If AEC is enabled, monitor the exposure and
	   adjust the sensor frame rate if needed */
	if (sd->params.exposure.expMode == 2)
		monitor_exposure(gspca_dev);

	/* Update our knowledge of the camera state */
	do_command(gspca_dev, CPIA_COMMAND_GetExposure, 0, 0, 0, 0);
	if (sd->params.qx3.qx3_detected)
		do_command(gspca_dev, CPIA_COMMAND_ReadMCPorts, 0, 0, 0, 0);
}

static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	sd->params.colourParams.brightness = val;
	sd->params.flickerControl.allowableOverExposure =
		find_over_exposure(sd->params.colourParams.brightness);
	if (gspca_dev->streaming) {
		ret = command_setcolourparams(gspca_dev);
		if (ret)
			return ret;
		return command_setflickerctrl(gspca_dev);
	}
	return 0;
}

static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->params.colourParams.brightness;
	return 0;
}

static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->params.colourParams.contrast = val;
	if (gspca_dev->streaming)
		return command_setcolourparams(gspca_dev);

	return 0;
}

static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->params.colourParams.contrast;
	return 0;
}

static int sd_setsaturation(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->params.colourParams.saturation = val;
	if (gspca_dev->streaming)
		return command_setcolourparams(gspca_dev);

	return 0;
}

static int sd_getsaturation(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->params.colourParams.saturation;
	return 0;
}

static int sd_setfreq(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int on;

	switch (val) {
	case 0:		/* V4L2_CID_POWER_LINE_FREQUENCY_DISABLED */
		on = 0;
		break;
	case 1:		/* V4L2_CID_POWER_LINE_FREQUENCY_50HZ */
		on = 1;
		sd->mainsFreq = 0;
		break;
	case 2:		/* V4L2_CID_POWER_LINE_FREQUENCY_60HZ */
		on = 1;
		sd->mainsFreq = 1;
		break;
	default:
		return -EINVAL;
	}

	sd->freq = val;
	sd->params.flickerControl.coarseJump =
		flicker_jumps[sd->mainsFreq]
			     [sd->params.sensorFps.baserate]
			     [sd->params.sensorFps.divisor];

	return set_flicker(gspca_dev, on, gspca_dev->streaming);
}

static int sd_getfreq(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->freq;
	return 0;
}

static int sd_setcomptarget(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->params.compressionTarget.frTargeting = val;
	if (gspca_dev->streaming)
		return command_setcompressiontarget(gspca_dev);

	return 0;
}

static int sd_getcomptarget(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->params.compressionTarget.frTargeting;
	return 0;
}

static int sd_setilluminator(struct gspca_dev *gspca_dev, __s32 val, int n)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	if (!sd->params.qx3.qx3_detected)
		return -EINVAL;

	switch (n) {
	case 1:
		sd->params.qx3.bottomlight = val ? 1 : 0;
		break;
	case 2:
		sd->params.qx3.toplight = val ? 1 : 0;
		break;
	default:
		return -EINVAL;
	}

	ret = command_setlights(gspca_dev);
	if (ret && ret != -EINVAL)
		ret = -EBUSY;

	return ret;
}

static int sd_setilluminator1(struct gspca_dev *gspca_dev, __s32 val)
{
	return sd_setilluminator(gspca_dev, val, 1);
}

static int sd_setilluminator2(struct gspca_dev *gspca_dev, __s32 val)
{
	return sd_setilluminator(gspca_dev, val, 2);
}

static int sd_getilluminator(struct gspca_dev *gspca_dev, __s32 *val, int n)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (!sd->params.qx3.qx3_detected)
		return -EINVAL;

	switch (n) {
	case 1:
		*val = sd->params.qx3.bottomlight;
		break;
	case 2:
		*val = sd->params.qx3.toplight;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sd_getilluminator1(struct gspca_dev *gspca_dev, __s32 *val)
{
	return sd_getilluminator(gspca_dev, val, 1);
}

static int sd_getilluminator2(struct gspca_dev *gspca_dev, __s32 *val)
{
	return sd_getilluminator(gspca_dev, val, 2);
}

static int sd_querymenu(struct gspca_dev *gspca_dev,
			struct v4l2_querymenu *menu)
{
	switch (menu->id) {
	case V4L2_CID_POWER_LINE_FREQUENCY:
		switch (menu->index) {
		case 0:		/* V4L2_CID_POWER_LINE_FREQUENCY_DISABLED */
			strcpy((char *) menu->name, "NoFliker");
			return 0;
		case 1:		/* V4L2_CID_POWER_LINE_FREQUENCY_50HZ */
			strcpy((char *) menu->name, "50 Hz");
			return 0;
		case 2:		/* V4L2_CID_POWER_LINE_FREQUENCY_60HZ */
			strcpy((char *) menu->name, "60 Hz");
			return 0;
		}
		break;
	case V4L2_CID_COMP_TARGET:
		switch (menu->index) {
		case CPIA_COMPRESSION_TARGET_QUALITY:
			strcpy((char *) menu->name, "Quality");
			return 0;
		case CPIA_COMPRESSION_TARGET_FRAMERATE:
			strcpy((char *) menu->name, "Framerate");
			return 0;
		}
		break;
	}
	return -EINVAL;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.dq_callback = sd_dq_callback,
	.pkt_scan = sd_pkt_scan,
	.querymenu = sd_querymenu,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0553, 0x0002)},
	{USB_DEVICE(0x0813, 0x0001)},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
				THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
#endif
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	return usb_register(&sd_driver);
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
