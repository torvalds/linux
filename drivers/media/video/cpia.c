/*
 * cpia CPiA driver
 *
 * Supports CPiA based Video Camera's.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* define _CPIA_DEBUG_ for verbose debug output (see cpia.h) */
/* #define _CPIA_DEBUG_  1 */

#include <linux/config.h>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/pagemap.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/mutex.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include "cpia.h"

static int video_nr = -1;

#ifdef MODULE
module_param(video_nr, int, 0);
MODULE_AUTHOR("Scott J. Bertin <sbertin@securenym.net> & Peter Pregler <Peter_Pregler@email.com> & Johannes Erdfelt <johannes@erdfelt.com>");
MODULE_DESCRIPTION("V4L-driver for Vision CPiA based cameras");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("video");
#endif

static unsigned short colorspace_conv;
module_param(colorspace_conv, ushort, 0444);
MODULE_PARM_DESC(colorspace_conv,
		 " Colorspace conversion:"
		 "\n  0 = disable, 1 = enable"
		 "\n  Default value is 0"
		 );

#define ABOUT "V4L-Driver for Vision CPiA based cameras"

#ifndef VID_HARDWARE_CPIA
#define VID_HARDWARE_CPIA 24    /* FIXME -> from linux/videodev.h */
#endif

#define CPIA_MODULE_CPIA			(0<<5)
#define CPIA_MODULE_SYSTEM			(1<<5)
#define CPIA_MODULE_VP_CTRL			(5<<5)
#define CPIA_MODULE_CAPTURE			(6<<5)
#define CPIA_MODULE_DEBUG			(7<<5)

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

enum {
	FRAME_READY,		/* Ready to grab into */
	FRAME_GRABBING,		/* In the process of being grabbed into */
	FRAME_DONE,		/* Finished grabbing, but not been synced yet */
	FRAME_UNUSED,		/* Unused (no MCAPTURE) */
};

#define COMMAND_NONE			0x0000
#define COMMAND_SETCOMPRESSION		0x0001
#define COMMAND_SETCOMPRESSIONTARGET	0x0002
#define COMMAND_SETCOLOURPARAMS		0x0004
#define COMMAND_SETFORMAT		0x0008
#define COMMAND_PAUSE			0x0010
#define COMMAND_RESUME			0x0020
#define COMMAND_SETYUVTHRESH		0x0040
#define COMMAND_SETECPTIMING		0x0080
#define COMMAND_SETCOMPRESSIONPARAMS	0x0100
#define COMMAND_SETEXPOSURE		0x0200
#define COMMAND_SETCOLOURBALANCE	0x0400
#define COMMAND_SETSENSORFPS		0x0800
#define COMMAND_SETAPCOR		0x1000
#define COMMAND_SETFLICKERCTRL		0x2000
#define COMMAND_SETVLOFFSET		0x4000
#define COMMAND_SETLIGHTS		0x8000

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

/* Maximum number of 10ms loops to wait for the stream to become ready */
#define READY_TIMEOUT 100

/* Developer's Guide Table 5 p 3-34
 * indexed by [mains][sensorFps.baserate][sensorFps.divisor]*/
static u8 flicker_jumps[2][2][4] =
{ { { 76, 38, 19, 9 }, { 92, 46, 23, 11 } },
  { { 64, 32, 16, 8 }, { 76, 38, 19, 9} }
};

/* forward declaration of local function */
static void reset_camera_struct(struct cam_data *cam);
static int find_over_exposure(int brightness);
static void set_flicker(struct cam_params *params, volatile u32 *command_flags,
			int on);


/**********************************************************************
 *
 * Memory management
 *
 **********************************************************************/
static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

/**********************************************************************
 *
 * /proc interface
 *
 **********************************************************************/
#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *cpia_proc_root=NULL;

static int cpia_read_proc(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	char *out = page;
	int len, tmp;
	struct cam_data *cam = data;
	char tmpstr[29];

	/* IMPORTANT: This output MUST be kept under PAGE_SIZE
	 *            or we need to get more sophisticated. */

	out += sprintf(out, "read-only\n-----------------------\n");
	out += sprintf(out, "V4L Driver version:       %d.%d.%d\n",
		       CPIA_MAJ_VER, CPIA_MIN_VER, CPIA_PATCH_VER);
	out += sprintf(out, "CPIA Version:             %d.%02d (%d.%d)\n",
		       cam->params.version.firmwareVersion,
		       cam->params.version.firmwareRevision,
		       cam->params.version.vcVersion,
		       cam->params.version.vcRevision);
	out += sprintf(out, "CPIA PnP-ID:              %04x:%04x:%04x\n",
		       cam->params.pnpID.vendor, cam->params.pnpID.product,
		       cam->params.pnpID.deviceRevision);
	out += sprintf(out, "VP-Version:               %d.%d %04x\n",
		       cam->params.vpVersion.vpVersion,
		       cam->params.vpVersion.vpRevision,
		       cam->params.vpVersion.cameraHeadID);

	out += sprintf(out, "system_state:             %#04x\n",
		       cam->params.status.systemState);
	out += sprintf(out, "grab_state:               %#04x\n",
		       cam->params.status.grabState);
	out += sprintf(out, "stream_state:             %#04x\n",
		       cam->params.status.streamState);
	out += sprintf(out, "fatal_error:              %#04x\n",
		       cam->params.status.fatalError);
	out += sprintf(out, "cmd_error:                %#04x\n",
		       cam->params.status.cmdError);
	out += sprintf(out, "debug_flags:              %#04x\n",
		       cam->params.status.debugFlags);
	out += sprintf(out, "vp_status:                %#04x\n",
		       cam->params.status.vpStatus);
	out += sprintf(out, "error_code:               %#04x\n",
		       cam->params.status.errorCode);
	/* QX3 specific entries */
	if (cam->params.qx3.qx3_detected) {
		out += sprintf(out, "button:                   %4d\n",
			       cam->params.qx3.button);
		out += sprintf(out, "cradled:                  %4d\n",
			       cam->params.qx3.cradled);
	}
	out += sprintf(out, "video_size:               %s\n",
		       cam->params.format.videoSize == VIDEOSIZE_CIF ?
		       "CIF " : "QCIF");
	out += sprintf(out, "roi:                      (%3d, %3d) to (%3d, %3d)\n",
		       cam->params.roi.colStart*8,
		       cam->params.roi.rowStart*4,
		       cam->params.roi.colEnd*8,
		       cam->params.roi.rowEnd*4);
	out += sprintf(out, "actual_fps:               %3d\n", cam->fps);
	out += sprintf(out, "transfer_rate:            %4dkB/s\n",
		       cam->transfer_rate);

	out += sprintf(out, "\nread-write\n");
	out += sprintf(out, "-----------------------  current       min"
		       "       max   default  comment\n");
	out += sprintf(out, "brightness:             %8d  %8d  %8d  %8d\n",
		       cam->params.colourParams.brightness, 0, 100, 50);
	if (cam->params.version.firmwareVersion == 1 &&
	   cam->params.version.firmwareRevision == 2)
		/* 1-02 firmware limits contrast to 80 */
		tmp = 80;
	else
		tmp = 96;

	out += sprintf(out, "contrast:               %8d  %8d  %8d  %8d"
		       "  steps of 8\n",
		       cam->params.colourParams.contrast, 0, tmp, 48);
	out += sprintf(out, "saturation:             %8d  %8d  %8d  %8d\n",
		       cam->params.colourParams.saturation, 0, 100, 50);
	tmp = (25000+5000*cam->params.sensorFps.baserate)/
	      (1<<cam->params.sensorFps.divisor);
	out += sprintf(out, "sensor_fps:             %4d.%03d  %8d  %8d  %8d\n",
		       tmp/1000, tmp%1000, 3, 30, 15);
	out += sprintf(out, "stream_start_line:      %8d  %8d  %8d  %8d\n",
		       2*cam->params.streamStartLine, 0,
		       cam->params.format.videoSize == VIDEOSIZE_CIF ? 288:144,
		       cam->params.format.videoSize == VIDEOSIZE_CIF ? 240:120);
	out += sprintf(out, "sub_sample:             %8s  %8s  %8s  %8s\n",
		       cam->params.format.subSample == SUBSAMPLE_420 ?
		       "420" : "422", "420", "422", "422");
	out += sprintf(out, "yuv_order:              %8s  %8s  %8s  %8s\n",
		       cam->params.format.yuvOrder == YUVORDER_YUYV ?
		       "YUYV" : "UYVY", "YUYV" , "UYVY", "YUYV");
	out += sprintf(out, "ecp_timing:             %8s  %8s  %8s  %8s\n",
		       cam->params.ecpTiming ? "slow" : "normal", "slow",
		       "normal", "normal");

	if (cam->params.colourBalance.balanceMode == 2) {
		sprintf(tmpstr, "auto");
	} else {
		sprintf(tmpstr, "manual");
	}
	out += sprintf(out, "color_balance_mode:     %8s  %8s  %8s"
		       "  %8s\n",  tmpstr, "manual", "auto", "auto");
	out += sprintf(out, "red_gain:               %8d  %8d  %8d  %8d\n",
		       cam->params.colourBalance.redGain, 0, 212, 32);
	out += sprintf(out, "green_gain:             %8d  %8d  %8d  %8d\n",
		       cam->params.colourBalance.greenGain, 0, 212, 6);
	out += sprintf(out, "blue_gain:              %8d  %8d  %8d  %8d\n",
		       cam->params.colourBalance.blueGain, 0, 212, 92);

	if (cam->params.version.firmwareVersion == 1 &&
	   cam->params.version.firmwareRevision == 2)
		/* 1-02 firmware limits gain to 2 */
		sprintf(tmpstr, "%8d  %8d  %8d", 1, 2, 2);
	else
		sprintf(tmpstr, "%8d  %8d  %8d", 1, 8, 2);

	if (cam->params.exposure.gainMode == 0)
		out += sprintf(out, "max_gain:                unknown  %28s"
			       "  powers of 2\n", tmpstr);
	else
		out += sprintf(out, "max_gain:               %8d  %28s"
			       "  1,2,4 or 8 \n",
			       1<<(cam->params.exposure.gainMode-1), tmpstr);

	switch(cam->params.exposure.expMode) {
	case 1:
	case 3:
		sprintf(tmpstr, "manual");
		break;
	case 2:
		sprintf(tmpstr, "auto");
		break;
	default:
		sprintf(tmpstr, "unknown");
		break;
	}
	out += sprintf(out, "exposure_mode:          %8s  %8s  %8s"
		       "  %8s\n",  tmpstr, "manual", "auto", "auto");
	out += sprintf(out, "centre_weight:          %8s  %8s  %8s  %8s\n",
		       (2-cam->params.exposure.centreWeight) ? "on" : "off",
		       "off", "on", "on");
	out += sprintf(out, "gain:                   %8d  %8d  max_gain  %8d  1,2,4,8 possible\n",
		       1<<cam->params.exposure.gain, 1, 1);
	if (cam->params.version.firmwareVersion == 1 &&
	   cam->params.version.firmwareRevision == 2)
		/* 1-02 firmware limits fineExp/2 to 127 */
		tmp = 254;
	else
		tmp = 510;

	out += sprintf(out, "fine_exp:               %8d  %8d  %8d  %8d\n",
		       cam->params.exposure.fineExp*2, 0, tmp, 0);
	if (cam->params.version.firmwareVersion == 1 &&
	   cam->params.version.firmwareRevision == 2)
		/* 1-02 firmware limits coarseExpHi to 0 */
		tmp = MAX_EXP_102;
	else
		tmp = MAX_EXP;

	out += sprintf(out, "coarse_exp:             %8d  %8d  %8d"
		       "  %8d\n", cam->params.exposure.coarseExpLo+
		       256*cam->params.exposure.coarseExpHi, 0, tmp, 185);
	out += sprintf(out, "red_comp:               %8d  %8d  %8d  %8d\n",
		       cam->params.exposure.redComp, COMP_RED, 255, COMP_RED);
	out += sprintf(out, "green1_comp:            %8d  %8d  %8d  %8d\n",
		       cam->params.exposure.green1Comp, COMP_GREEN1, 255,
		       COMP_GREEN1);
	out += sprintf(out, "green2_comp:            %8d  %8d  %8d  %8d\n",
		       cam->params.exposure.green2Comp, COMP_GREEN2, 255,
		       COMP_GREEN2);
	out += sprintf(out, "blue_comp:              %8d  %8d  %8d  %8d\n",
		       cam->params.exposure.blueComp, COMP_BLUE, 255, COMP_BLUE);

	out += sprintf(out, "apcor_gain1:            %#8x  %#8x  %#8x  %#8x\n",
		       cam->params.apcor.gain1, 0, 0xff, 0x1c);
	out += sprintf(out, "apcor_gain2:            %#8x  %#8x  %#8x  %#8x\n",
		       cam->params.apcor.gain2, 0, 0xff, 0x1a);
	out += sprintf(out, "apcor_gain4:            %#8x  %#8x  %#8x  %#8x\n",
		       cam->params.apcor.gain4, 0, 0xff, 0x2d);
	out += sprintf(out, "apcor_gain8:            %#8x  %#8x  %#8x  %#8x\n",
		       cam->params.apcor.gain8, 0, 0xff, 0x2a);
	out += sprintf(out, "vl_offset_gain1:        %8d  %8d  %8d  %8d\n",
		       cam->params.vlOffset.gain1, 0, 255, 24);
	out += sprintf(out, "vl_offset_gain2:        %8d  %8d  %8d  %8d\n",
		       cam->params.vlOffset.gain2, 0, 255, 28);
	out += sprintf(out, "vl_offset_gain4:        %8d  %8d  %8d  %8d\n",
		       cam->params.vlOffset.gain4, 0, 255, 30);
	out += sprintf(out, "vl_offset_gain8:        %8d  %8d  %8d  %8d\n",
		       cam->params.vlOffset.gain8, 0, 255, 30);
	out += sprintf(out, "flicker_control:        %8s  %8s  %8s  %8s\n",
		       cam->params.flickerControl.flickerMode ? "on" : "off",
		       "off", "on", "off");
	out += sprintf(out, "mains_frequency:        %8d  %8d  %8d  %8d"
		       " only 50/60\n",
		       cam->mainsFreq ? 60 : 50, 50, 60, 50);
	if(cam->params.flickerControl.allowableOverExposure < 0)
		out += sprintf(out, "allowable_overexposure: %4dauto      auto  %8d      auto\n",
			       -cam->params.flickerControl.allowableOverExposure,
			       255);
	else
		out += sprintf(out, "allowable_overexposure: %8d      auto  %8d      auto\n",
			       cam->params.flickerControl.allowableOverExposure,
			       255);
	out += sprintf(out, "compression_mode:       ");
	switch(cam->params.compression.mode) {
	case CPIA_COMPRESSION_NONE:
		out += sprintf(out, "%8s", "none");
		break;
	case CPIA_COMPRESSION_AUTO:
		out += sprintf(out, "%8s", "auto");
		break;
	case CPIA_COMPRESSION_MANUAL:
		out += sprintf(out, "%8s", "manual");
		break;
	default:
		out += sprintf(out, "%8s", "unknown");
		break;
	}
	out += sprintf(out, "    none,auto,manual      auto\n");
	out += sprintf(out, "decimation_enable:      %8s  %8s  %8s  %8s\n",
		       cam->params.compression.decimation ==
		       DECIMATION_ENAB ? "on":"off", "off", "on",
		       "off");
	out += sprintf(out, "compression_target:    %9s %9s %9s %9s\n",
		       cam->params.compressionTarget.frTargeting  ==
		       CPIA_COMPRESSION_TARGET_FRAMERATE ?
		       "framerate":"quality",
		       "framerate", "quality", "quality");
	out += sprintf(out, "target_framerate:       %8d  %8d  %8d  %8d\n",
		       cam->params.compressionTarget.targetFR, 1, 30, 15);
	out += sprintf(out, "target_quality:         %8d  %8d  %8d  %8d\n",
		       cam->params.compressionTarget.targetQ, 1, 64, 5);
	out += sprintf(out, "y_threshold:            %8d  %8d  %8d  %8d\n",
		       cam->params.yuvThreshold.yThreshold, 0, 31, 6);
	out += sprintf(out, "uv_threshold:           %8d  %8d  %8d  %8d\n",
		       cam->params.yuvThreshold.uvThreshold, 0, 31, 6);
	out += sprintf(out, "hysteresis:             %8d  %8d  %8d  %8d\n",
		       cam->params.compressionParams.hysteresis, 0, 255, 3);
	out += sprintf(out, "threshold_max:          %8d  %8d  %8d  %8d\n",
		       cam->params.compressionParams.threshMax, 0, 255, 11);
	out += sprintf(out, "small_step:             %8d  %8d  %8d  %8d\n",
		       cam->params.compressionParams.smallStep, 0, 255, 1);
	out += sprintf(out, "large_step:             %8d  %8d  %8d  %8d\n",
		       cam->params.compressionParams.largeStep, 0, 255, 3);
	out += sprintf(out, "decimation_hysteresis:  %8d  %8d  %8d  %8d\n",
		       cam->params.compressionParams.decimationHysteresis,
		       0, 255, 2);
	out += sprintf(out, "fr_diff_step_thresh:    %8d  %8d  %8d  %8d\n",
		       cam->params.compressionParams.frDiffStepThresh,
		       0, 255, 5);
	out += sprintf(out, "q_diff_step_thresh:     %8d  %8d  %8d  %8d\n",
		       cam->params.compressionParams.qDiffStepThresh,
		       0, 255, 3);
	out += sprintf(out, "decimation_thresh_mod:  %8d  %8d  %8d  %8d\n",
		       cam->params.compressionParams.decimationThreshMod,
		       0, 255, 2);
	/* QX3 specific entries */
	if (cam->params.qx3.qx3_detected) {
		out += sprintf(out, "toplight:               %8s  %8s  %8s  %8s\n",
			       cam->params.qx3.toplight ? "on" : "off",
			       "off", "on", "off");
		out += sprintf(out, "bottomlight:            %8s  %8s  %8s  %8s\n",
			       cam->params.qx3.bottomlight ? "on" : "off",
			       "off", "on", "off");
	}

	len = out - page;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0) return 0;
	} else
		len = count;

	*start = page + off;
	return len;
}


static int match(char *checkstr, char **buffer, unsigned long *count,
		 int *find_colon, int *err)
{
	int ret, colon_found = 1;
	int len = strlen(checkstr);
	ret = (len <= *count && strncmp(*buffer, checkstr, len) == 0);
	if (ret) {
		*buffer += len;
		*count -= len;
		if (*find_colon) {
			colon_found = 0;
			while (*count && (**buffer == ' ' || **buffer == '\t' ||
					  (!colon_found && **buffer == ':'))) {
				if (**buffer == ':')
					colon_found = 1;
				--*count;
				++*buffer;
			}
			if (!*count || !colon_found)
				*err = -EINVAL;
			*find_colon = 0;
		}
	}
	return ret;
}

static unsigned long int value(char **buffer, unsigned long *count, int *err)
{
	char *p;
	unsigned long int ret;
	ret = simple_strtoul(*buffer, &p, 0);
	if (p == *buffer)
		*err = -EINVAL;
	else {
		*count -= p - *buffer;
		*buffer = p;
	}
	return ret;
}

static int cpia_write_proc(struct file *file, const char __user *buf,
			   unsigned long count, void *data)
{
	struct cam_data *cam = data;
	struct cam_params new_params;
	char *page, *buffer;
	int retval, find_colon;
	int size = count;
	unsigned long val = 0;
	u32 command_flags = 0;
	u8 new_mains;

	/*
	 * This code to copy from buf to page is shamelessly copied
	 * from the comx driver
	 */
	if (count > PAGE_SIZE) {
		printk(KERN_ERR "count is %lu > %d!!!\n", count, (int)PAGE_SIZE);
		return -ENOSPC;
	}

	if (!(page = (char *)__get_free_page(GFP_KERNEL))) return -ENOMEM;

	if(copy_from_user(page, buf, count))
	{
		retval = -EFAULT;
		goto out;
	}

	if (page[count-1] == '\n')
		page[count-1] = '\0';
	else if (count < PAGE_SIZE)
		page[count] = '\0';
	else if (page[count]) {
		retval = -EINVAL;
		goto out;
	}

	buffer = page;

	if (mutex_lock_interruptible(&cam->param_lock))
		return -ERESTARTSYS;

	/*
	 * Skip over leading whitespace
	 */
	while (count && isspace(*buffer)) {
		--count;
		++buffer;
	}

	memcpy(&new_params, &cam->params, sizeof(struct cam_params));
	new_mains = cam->mainsFreq;

#define MATCH(x) (match(x, &buffer, &count, &find_colon, &retval))
#define VALUE (value(&buffer,&count, &retval))
#define FIRMWARE_VERSION(x,y) (new_params.version.firmwareVersion == (x) && \
			       new_params.version.firmwareRevision == (y))

	retval = 0;
	while (count && !retval) {
		find_colon = 1;
		if (MATCH("brightness")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 100)
					new_params.colourParams.brightness = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOLOURPARAMS;
			if(new_params.flickerControl.allowableOverExposure < 0)
				new_params.flickerControl.allowableOverExposure =
					-find_over_exposure(new_params.colourParams.brightness);
			if(new_params.flickerControl.flickerMode != 0)
				command_flags |= COMMAND_SETFLICKERCTRL;

		} else if (MATCH("contrast")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 100) {
					/* contrast is in steps of 8, so round*/
					val = ((val + 3) / 8) * 8;
					/* 1-02 firmware limits contrast to 80*/
					if (FIRMWARE_VERSION(1,2) && val > 80)
						val = 80;

					new_params.colourParams.contrast = val;
				} else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOLOURPARAMS;
		} else if (MATCH("saturation")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 100)
					new_params.colourParams.saturation = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOLOURPARAMS;
		} else if (MATCH("sensor_fps")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				/* find values so that sensorFPS is minimized,
				 * but >= val */
				if (val > 30)
					retval = -EINVAL;
				else if (val > 25) {
					new_params.sensorFps.divisor = 0;
					new_params.sensorFps.baserate = 1;
				} else if (val > 15) {
					new_params.sensorFps.divisor = 0;
					new_params.sensorFps.baserate = 0;
				} else if (val > 12) {
					new_params.sensorFps.divisor = 1;
					new_params.sensorFps.baserate = 1;
				} else if (val > 7) {
					new_params.sensorFps.divisor = 1;
					new_params.sensorFps.baserate = 0;
				} else if (val > 6) {
					new_params.sensorFps.divisor = 2;
					new_params.sensorFps.baserate = 1;
				} else if (val > 3) {
					new_params.sensorFps.divisor = 2;
					new_params.sensorFps.baserate = 0;
				} else {
					new_params.sensorFps.divisor = 3;
					/* Either base rate would work here */
					new_params.sensorFps.baserate = 1;
				}
				new_params.flickerControl.coarseJump =
					flicker_jumps[new_mains]
					[new_params.sensorFps.baserate]
					[new_params.sensorFps.divisor];
				if (new_params.flickerControl.flickerMode)
					command_flags |= COMMAND_SETFLICKERCTRL;
			}
			command_flags |= COMMAND_SETSENSORFPS;
			cam->exposure_status = EXPOSURE_NORMAL;
		} else if (MATCH("stream_start_line")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				int max_line = 288;

				if (new_params.format.videoSize == VIDEOSIZE_QCIF)
					max_line = 144;
				if (val <= max_line)
					new_params.streamStartLine = val/2;
				else
					retval = -EINVAL;
			}
		} else if (MATCH("sub_sample")) {
			if (!retval && MATCH("420"))
				new_params.format.subSample = SUBSAMPLE_420;
			else if (!retval && MATCH("422"))
				new_params.format.subSample = SUBSAMPLE_422;
			else
				retval = -EINVAL;

			command_flags |= COMMAND_SETFORMAT;
		} else if (MATCH("yuv_order")) {
			if (!retval && MATCH("YUYV"))
				new_params.format.yuvOrder = YUVORDER_YUYV;
			else if (!retval && MATCH("UYVY"))
				new_params.format.yuvOrder = YUVORDER_UYVY;
			else
				retval = -EINVAL;

			command_flags |= COMMAND_SETFORMAT;
		} else if (MATCH("ecp_timing")) {
			if (!retval && MATCH("normal"))
				new_params.ecpTiming = 0;
			else if (!retval && MATCH("slow"))
				new_params.ecpTiming = 1;
			else
				retval = -EINVAL;

			command_flags |= COMMAND_SETECPTIMING;
		} else if (MATCH("color_balance_mode")) {
			if (!retval && MATCH("manual"))
				new_params.colourBalance.balanceMode = 3;
			else if (!retval && MATCH("auto"))
				new_params.colourBalance.balanceMode = 2;
			else
				retval = -EINVAL;

			command_flags |= COMMAND_SETCOLOURBALANCE;
		} else if (MATCH("red_gain")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 212) {
					new_params.colourBalance.redGain = val;
					new_params.colourBalance.balanceMode = 1;
				} else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOLOURBALANCE;
		} else if (MATCH("green_gain")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 212) {
					new_params.colourBalance.greenGain = val;
					new_params.colourBalance.balanceMode = 1;
				} else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOLOURBALANCE;
		} else if (MATCH("blue_gain")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 212) {
					new_params.colourBalance.blueGain = val;
					new_params.colourBalance.balanceMode = 1;
				} else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOLOURBALANCE;
		} else if (MATCH("max_gain")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				/* 1-02 firmware limits gain to 2 */
				if (FIRMWARE_VERSION(1,2) && val > 2)
					val = 2;
				switch(val) {
				case 1:
					new_params.exposure.gainMode = 1;
					break;
				case 2:
					new_params.exposure.gainMode = 2;
					break;
				case 4:
					new_params.exposure.gainMode = 3;
					break;
				case 8:
					new_params.exposure.gainMode = 4;
					break;
				default:
					retval = -EINVAL;
					break;
				}
			}
			command_flags |= COMMAND_SETEXPOSURE;
		} else if (MATCH("exposure_mode")) {
			if (!retval && MATCH("auto"))
				new_params.exposure.expMode = 2;
			else if (!retval && MATCH("manual")) {
				if (new_params.exposure.expMode == 2)
					new_params.exposure.expMode = 3;
				if(new_params.flickerControl.flickerMode != 0)
					command_flags |= COMMAND_SETFLICKERCTRL;
				new_params.flickerControl.flickerMode = 0;
			} else
				retval = -EINVAL;

			command_flags |= COMMAND_SETEXPOSURE;
		} else if (MATCH("centre_weight")) {
			if (!retval && MATCH("on"))
				new_params.exposure.centreWeight = 1;
			else if (!retval && MATCH("off"))
				new_params.exposure.centreWeight = 2;
			else
				retval = -EINVAL;

			command_flags |= COMMAND_SETEXPOSURE;
		} else if (MATCH("gain")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				switch(val) {
				case 1:
					new_params.exposure.gain = 0;
					break;
				case 2:
					new_params.exposure.gain = 1;
					break;
				case 4:
					new_params.exposure.gain = 2;
					break;
				case 8:
					new_params.exposure.gain = 3;
					break;
				default:
					retval = -EINVAL;
					break;
				}
				new_params.exposure.expMode = 1;
				if(new_params.flickerControl.flickerMode != 0)
					command_flags |= COMMAND_SETFLICKERCTRL;
				new_params.flickerControl.flickerMode = 0;
				command_flags |= COMMAND_SETEXPOSURE;
				if (new_params.exposure.gain >
				    new_params.exposure.gainMode-1)
					retval = -EINVAL;
			}
		} else if (MATCH("fine_exp")) {
			if (!retval)
				val = VALUE/2;

			if (!retval) {
				if (val < 256) {
					/* 1-02 firmware limits fineExp/2 to 127*/
					if (FIRMWARE_VERSION(1,2) && val > 127)
						val = 127;
					new_params.exposure.fineExp = val;
					new_params.exposure.expMode = 1;
					command_flags |= COMMAND_SETEXPOSURE;
					if(new_params.flickerControl.flickerMode != 0)
						command_flags |= COMMAND_SETFLICKERCTRL;
					new_params.flickerControl.flickerMode = 0;
					command_flags |= COMMAND_SETFLICKERCTRL;
				} else
					retval = -EINVAL;
			}
		} else if (MATCH("coarse_exp")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= MAX_EXP) {
					if (FIRMWARE_VERSION(1,2) &&
					    val > MAX_EXP_102)
						val = MAX_EXP_102;
					new_params.exposure.coarseExpLo =
						val & 0xff;
					new_params.exposure.coarseExpHi =
						val >> 8;
					new_params.exposure.expMode = 1;
					command_flags |= COMMAND_SETEXPOSURE;
					if(new_params.flickerControl.flickerMode != 0)
						command_flags |= COMMAND_SETFLICKERCTRL;
					new_params.flickerControl.flickerMode = 0;
					command_flags |= COMMAND_SETFLICKERCTRL;
				} else
					retval = -EINVAL;
			}
		} else if (MATCH("red_comp")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val >= COMP_RED && val <= 255) {
					new_params.exposure.redComp = val;
					new_params.exposure.compMode = 1;
					command_flags |= COMMAND_SETEXPOSURE;
				} else
					retval = -EINVAL;
			}
		} else if (MATCH("green1_comp")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val >= COMP_GREEN1 && val <= 255) {
					new_params.exposure.green1Comp = val;
					new_params.exposure.compMode = 1;
					command_flags |= COMMAND_SETEXPOSURE;
				} else
					retval = -EINVAL;
			}
		} else if (MATCH("green2_comp")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val >= COMP_GREEN2 && val <= 255) {
					new_params.exposure.green2Comp = val;
					new_params.exposure.compMode = 1;
					command_flags |= COMMAND_SETEXPOSURE;
				} else
					retval = -EINVAL;
			}
		} else if (MATCH("blue_comp")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val >= COMP_BLUE && val <= 255) {
					new_params.exposure.blueComp = val;
					new_params.exposure.compMode = 1;
					command_flags |= COMMAND_SETEXPOSURE;
				} else
					retval = -EINVAL;
			}
		} else if (MATCH("apcor_gain1")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				command_flags |= COMMAND_SETAPCOR;
				if (val <= 0xff)
					new_params.apcor.gain1 = val;
				else
					retval = -EINVAL;
			}
		} else if (MATCH("apcor_gain2")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				command_flags |= COMMAND_SETAPCOR;
				if (val <= 0xff)
					new_params.apcor.gain2 = val;
				else
					retval = -EINVAL;
			}
		} else if (MATCH("apcor_gain4")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				command_flags |= COMMAND_SETAPCOR;
				if (val <= 0xff)
					new_params.apcor.gain4 = val;
				else
					retval = -EINVAL;
			}
		} else if (MATCH("apcor_gain8")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				command_flags |= COMMAND_SETAPCOR;
				if (val <= 0xff)
					new_params.apcor.gain8 = val;
				else
					retval = -EINVAL;
			}
		} else if (MATCH("vl_offset_gain1")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.vlOffset.gain1 = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETVLOFFSET;
		} else if (MATCH("vl_offset_gain2")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.vlOffset.gain2 = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETVLOFFSET;
		} else if (MATCH("vl_offset_gain4")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.vlOffset.gain4 = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETVLOFFSET;
		} else if (MATCH("vl_offset_gain8")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.vlOffset.gain8 = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETVLOFFSET;
		} else if (MATCH("flicker_control")) {
			if (!retval && MATCH("on")) {
				set_flicker(&new_params, &command_flags, 1);
			} else if (!retval && MATCH("off")) {
				set_flicker(&new_params, &command_flags, 0);
			} else
				retval = -EINVAL;

			command_flags |= COMMAND_SETFLICKERCTRL;
		} else if (MATCH("mains_frequency")) {
			if (!retval && MATCH("50")) {
				new_mains = 0;
				new_params.flickerControl.coarseJump =
					flicker_jumps[new_mains]
					[new_params.sensorFps.baserate]
					[new_params.sensorFps.divisor];
				if (new_params.flickerControl.flickerMode)
					command_flags |= COMMAND_SETFLICKERCTRL;
			} else if (!retval && MATCH("60")) {
				new_mains = 1;
				new_params.flickerControl.coarseJump =
					flicker_jumps[new_mains]
					[new_params.sensorFps.baserate]
					[new_params.sensorFps.divisor];
				if (new_params.flickerControl.flickerMode)
					command_flags |= COMMAND_SETFLICKERCTRL;
			} else
				retval = -EINVAL;
		} else if (MATCH("allowable_overexposure")) {
			if (!retval && MATCH("auto")) {
				new_params.flickerControl.allowableOverExposure =
					-find_over_exposure(new_params.colourParams.brightness);
				if(new_params.flickerControl.flickerMode != 0)
					command_flags |= COMMAND_SETFLICKERCTRL;
			} else {
				if (!retval)
					val = VALUE;

				if (!retval) {
					if (val <= 0xff) {
						new_params.flickerControl.
							allowableOverExposure = val;
						if(new_params.flickerControl.flickerMode != 0)
							command_flags |= COMMAND_SETFLICKERCTRL;
					} else
						retval = -EINVAL;
				}
			}
		} else if (MATCH("compression_mode")) {
			if (!retval && MATCH("none"))
				new_params.compression.mode =
					CPIA_COMPRESSION_NONE;
			else if (!retval && MATCH("auto"))
				new_params.compression.mode =
					CPIA_COMPRESSION_AUTO;
			else if (!retval && MATCH("manual"))
				new_params.compression.mode =
					CPIA_COMPRESSION_MANUAL;
			else
				retval = -EINVAL;

			command_flags |= COMMAND_SETCOMPRESSION;
		} else if (MATCH("decimation_enable")) {
			if (!retval && MATCH("off"))
				new_params.compression.decimation = 0;
			else if (!retval && MATCH("on"))
				new_params.compression.decimation = 1;
			else
				retval = -EINVAL;

			command_flags |= COMMAND_SETCOMPRESSION;
		} else if (MATCH("compression_target")) {
			if (!retval && MATCH("quality"))
				new_params.compressionTarget.frTargeting =
					CPIA_COMPRESSION_TARGET_QUALITY;
			else if (!retval && MATCH("framerate"))
				new_params.compressionTarget.frTargeting =
					CPIA_COMPRESSION_TARGET_FRAMERATE;
			else
				retval = -EINVAL;

			command_flags |= COMMAND_SETCOMPRESSIONTARGET;
		} else if (MATCH("target_framerate")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if(val > 0 && val <= 30)
					new_params.compressionTarget.targetFR = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONTARGET;
		} else if (MATCH("target_quality")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if(val > 0 && val <= 64)
					new_params.compressionTarget.targetQ = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONTARGET;
		} else if (MATCH("y_threshold")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val < 32)
					new_params.yuvThreshold.yThreshold = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETYUVTHRESH;
		} else if (MATCH("uv_threshold")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val < 32)
					new_params.yuvThreshold.uvThreshold = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETYUVTHRESH;
		} else if (MATCH("hysteresis")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.compressionParams.hysteresis = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONPARAMS;
		} else if (MATCH("threshold_max")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.compressionParams.threshMax = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONPARAMS;
		} else if (MATCH("small_step")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.compressionParams.smallStep = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONPARAMS;
		} else if (MATCH("large_step")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.compressionParams.largeStep = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONPARAMS;
		} else if (MATCH("decimation_hysteresis")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.compressionParams.decimationHysteresis = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONPARAMS;
		} else if (MATCH("fr_diff_step_thresh")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.compressionParams.frDiffStepThresh = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONPARAMS;
		} else if (MATCH("q_diff_step_thresh")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.compressionParams.qDiffStepThresh = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONPARAMS;
		} else if (MATCH("decimation_thresh_mod")) {
			if (!retval)
				val = VALUE;

			if (!retval) {
				if (val <= 0xff)
					new_params.compressionParams.decimationThreshMod = val;
				else
					retval = -EINVAL;
			}
			command_flags |= COMMAND_SETCOMPRESSIONPARAMS;
		} else if (MATCH("toplight")) {
			if (!retval && MATCH("on"))
				new_params.qx3.toplight = 1;
			else if (!retval && MATCH("off"))
				new_params.qx3.toplight = 0;
			else
				retval = -EINVAL;
			command_flags |= COMMAND_SETLIGHTS;
		} else if (MATCH("bottomlight")) {
			if (!retval && MATCH("on"))
				new_params.qx3.bottomlight = 1;
			else if (!retval && MATCH("off"))
				new_params.qx3.bottomlight = 0;
			else
				retval = -EINVAL;
			command_flags |= COMMAND_SETLIGHTS;
		} else {
			DBG("No match found\n");
			retval = -EINVAL;
		}

		if (!retval) {
			while (count && isspace(*buffer) && *buffer != '\n') {
				--count;
				++buffer;
			}
			if (count) {
				if (*buffer == '\0' && count != 1)
					retval = -EINVAL;
				else if (*buffer != '\n' && *buffer != ';' &&
					 *buffer != '\0')
					retval = -EINVAL;
				else {
					--count;
					++buffer;
				}
			}
		}
	}
#undef MATCH
#undef VALUE
#undef FIRMWARE_VERSION
	if (!retval) {
		if (command_flags & COMMAND_SETCOLOURPARAMS) {
			/* Adjust cam->vp to reflect these changes */
			cam->vp.brightness =
				new_params.colourParams.brightness*65535/100;
			cam->vp.contrast =
				new_params.colourParams.contrast*65535/100;
			cam->vp.colour =
				new_params.colourParams.saturation*65535/100;
		}
		if((command_flags & COMMAND_SETEXPOSURE) &&
		   new_params.exposure.expMode == 2)
			cam->exposure_status = EXPOSURE_NORMAL;

		memcpy(&cam->params, &new_params, sizeof(struct cam_params));
		cam->mainsFreq = new_mains;
		cam->cmd_queue |= command_flags;
		retval = size;
	} else
		DBG("error: %d\n", retval);

	mutex_unlock(&cam->param_lock);

out:
	free_page((unsigned long)page);
	return retval;
}

static void create_proc_cpia_cam(struct cam_data *cam)
{
	char name[7];
	struct proc_dir_entry *ent;

	if (!cpia_proc_root || !cam)
		return;

	sprintf(name, "video%d", cam->vdev.minor);

	ent = create_proc_entry(name, S_IFREG|S_IRUGO|S_IWUSR, cpia_proc_root);
	if (!ent)
		return;

	ent->data = cam;
	ent->read_proc = cpia_read_proc;
	ent->write_proc = cpia_write_proc;
	/*
	   size of the proc entry is 3736 bytes for the standard webcam;
	   the extra features of the QX3 microscope add 189 bytes.
	   (we have not yet probed the camera to see which type it is).
	*/
	ent->size = 3736 + 189;
	cam->proc_entry = ent;
}

static void destroy_proc_cpia_cam(struct cam_data *cam)
{
	char name[7];

	if (!cam || !cam->proc_entry)
		return;

	sprintf(name, "video%d", cam->vdev.minor);
	remove_proc_entry(name, cpia_proc_root);
	cam->proc_entry = NULL;
}

static void proc_cpia_create(void)
{
	cpia_proc_root = proc_mkdir("cpia", NULL);

	if (cpia_proc_root)
		cpia_proc_root->owner = THIS_MODULE;
	else
		LOG("Unable to initialise /proc/cpia\n");
}

static void __exit proc_cpia_destroy(void)
{
	remove_proc_entry("cpia", NULL);
}
#endif /* CONFIG_PROC_FS */

/* ----------------------- debug functions ---------------------- */

#define printstatus(cam) \
  DBG("%02x %02x %02x %02x %02x %02x %02x %02x\n",\
	cam->params.status.systemState, cam->params.status.grabState, \
	cam->params.status.streamState, cam->params.status.fatalError, \
	cam->params.status.cmdError, cam->params.status.debugFlags, \
	cam->params.status.vpStatus, cam->params.status.errorCode);

/* ----------------------- v4l helpers -------------------------- */

/* supported frame palettes and depths */
static inline int valid_mode(u16 palette, u16 depth)
{
	if ((palette == VIDEO_PALETTE_YUV422 && depth == 16) ||
	    (palette == VIDEO_PALETTE_YUYV && depth == 16))
		return 1;

	if (colorspace_conv)
		return (palette == VIDEO_PALETTE_GREY && depth == 8) ||
		       (palette == VIDEO_PALETTE_RGB555 && depth == 16) ||
		       (palette == VIDEO_PALETTE_RGB565 && depth == 16) ||
		       (palette == VIDEO_PALETTE_RGB24 && depth == 24) ||
		       (palette == VIDEO_PALETTE_RGB32 && depth == 32) ||
		       (palette == VIDEO_PALETTE_UYVY && depth == 16);

	return 0;
}

static int match_videosize( int width, int height )
{
	/* return the best match, where 'best' is as always
	 * the largest that is not bigger than what is requested. */
	if (width>=352 && height>=288)
		return VIDEOSIZE_352_288; /* CIF */

	if (width>=320 && height>=240)
		return VIDEOSIZE_320_240; /* SIF */

	if (width>=288 && height>=216)
		return VIDEOSIZE_288_216;

	if (width>=256 && height>=192)
		return VIDEOSIZE_256_192;

	if (width>=224 && height>=168)
		return VIDEOSIZE_224_168;

	if (width>=192 && height>=144)
		return VIDEOSIZE_192_144;

	if (width>=176 && height>=144)
		return VIDEOSIZE_176_144; /* QCIF */

	if (width>=160 && height>=120)
		return VIDEOSIZE_160_120; /* QSIF */

	if (width>=128 && height>=96)
		return VIDEOSIZE_128_96;

	if (width>=88 && height>=72)
		return VIDEOSIZE_88_72;

	if (width>=64 && height>=48)
		return VIDEOSIZE_64_48;

	if (width>=48 && height>=48)
		return VIDEOSIZE_48_48;

	return -1;
}

/* these are the capture sizes we support */
static void set_vw_size(struct cam_data *cam)
{
	/* the col/row/start/end values are the result of simple math    */
	/* study the SetROI-command in cpia developers guide p 2-22      */
	/* streamStartLine is set to the recommended value in the cpia   */
	/*  developers guide p 3-37                                      */
	switch(cam->video_size) {
	case VIDEOSIZE_CIF:
		cam->vw.width = 352;
		cam->vw.height = 288;
		cam->params.format.videoSize=VIDEOSIZE_CIF;
		cam->params.roi.colStart=0;
		cam->params.roi.rowStart=0;
		cam->params.streamStartLine = 120;
		break;
	case VIDEOSIZE_SIF:
		cam->vw.width = 320;
		cam->vw.height = 240;
		cam->params.format.videoSize=VIDEOSIZE_CIF;
		cam->params.roi.colStart=2;
		cam->params.roi.rowStart=6;
		cam->params.streamStartLine = 120;
		break;
	case VIDEOSIZE_288_216:
		cam->vw.width = 288;
		cam->vw.height = 216;
		cam->params.format.videoSize=VIDEOSIZE_CIF;
		cam->params.roi.colStart=4;
		cam->params.roi.rowStart=9;
		cam->params.streamStartLine = 120;
		break;
	case VIDEOSIZE_256_192:
		cam->vw.width = 256;
		cam->vw.height = 192;
		cam->params.format.videoSize=VIDEOSIZE_CIF;
		cam->params.roi.colStart=6;
		cam->params.roi.rowStart=12;
		cam->params.streamStartLine = 120;
		break;
	case VIDEOSIZE_224_168:
		cam->vw.width = 224;
		cam->vw.height = 168;
		cam->params.format.videoSize=VIDEOSIZE_CIF;
		cam->params.roi.colStart=8;
		cam->params.roi.rowStart=15;
		cam->params.streamStartLine = 120;
		break;
	case VIDEOSIZE_192_144:
		cam->vw.width = 192;
		cam->vw.height = 144;
		cam->params.format.videoSize=VIDEOSIZE_CIF;
		cam->params.roi.colStart=10;
		cam->params.roi.rowStart=18;
		cam->params.streamStartLine = 120;
		break;
	case VIDEOSIZE_QCIF:
		cam->vw.width = 176;
		cam->vw.height = 144;
		cam->params.format.videoSize=VIDEOSIZE_QCIF;
		cam->params.roi.colStart=0;
		cam->params.roi.rowStart=0;
		cam->params.streamStartLine = 60;
		break;
	case VIDEOSIZE_QSIF:
		cam->vw.width = 160;
		cam->vw.height = 120;
		cam->params.format.videoSize=VIDEOSIZE_QCIF;
		cam->params.roi.colStart=1;
		cam->params.roi.rowStart=3;
		cam->params.streamStartLine = 60;
		break;
	case VIDEOSIZE_128_96:
		cam->vw.width = 128;
		cam->vw.height = 96;
		cam->params.format.videoSize=VIDEOSIZE_QCIF;
		cam->params.roi.colStart=3;
		cam->params.roi.rowStart=6;
		cam->params.streamStartLine = 60;
		break;
	case VIDEOSIZE_88_72:
		cam->vw.width = 88;
		cam->vw.height = 72;
		cam->params.format.videoSize=VIDEOSIZE_QCIF;
		cam->params.roi.colStart=5;
		cam->params.roi.rowStart=9;
		cam->params.streamStartLine = 60;
		break;
	case VIDEOSIZE_64_48:
		cam->vw.width = 64;
		cam->vw.height = 48;
		cam->params.format.videoSize=VIDEOSIZE_QCIF;
		cam->params.roi.colStart=7;
		cam->params.roi.rowStart=12;
		cam->params.streamStartLine = 60;
		break;
	case VIDEOSIZE_48_48:
		cam->vw.width = 48;
		cam->vw.height = 48;
		cam->params.format.videoSize=VIDEOSIZE_QCIF;
		cam->params.roi.colStart=8;
		cam->params.roi.rowStart=6;
		cam->params.streamStartLine = 60;
		break;
	default:
		LOG("bad videosize value: %d\n", cam->video_size);
		return;
	}

	if(cam->vc.width == 0)
		cam->vc.width = cam->vw.width;
	if(cam->vc.height == 0)
		cam->vc.height = cam->vw.height;

	cam->params.roi.colStart += cam->vc.x >> 3;
	cam->params.roi.colEnd = cam->params.roi.colStart +
				 (cam->vc.width >> 3);
	cam->params.roi.rowStart += cam->vc.y >> 2;
	cam->params.roi.rowEnd = cam->params.roi.rowStart +
				 (cam->vc.height >> 2);

	return;
}

static int allocate_frame_buf(struct cam_data *cam)
{
	int i;

	cam->frame_buf = rvmalloc(FRAME_NUM * CPIA_MAX_FRAME_SIZE);
	if (!cam->frame_buf)
		return -ENOBUFS;

	for (i = 0; i < FRAME_NUM; i++)
		cam->frame[i].data = cam->frame_buf + i * CPIA_MAX_FRAME_SIZE;

	return 0;
}

static int free_frame_buf(struct cam_data *cam)
{
	int i;

	rvfree(cam->frame_buf, FRAME_NUM*CPIA_MAX_FRAME_SIZE);
	cam->frame_buf = NULL;
	for (i=0; i < FRAME_NUM; i++)
		cam->frame[i].data = NULL;

	return 0;
}


static inline void free_frames(struct cpia_frame frame[FRAME_NUM])
{
	int i;

	for (i=0; i < FRAME_NUM; i++)
		frame[i].state = FRAME_UNUSED;
	return;
}

/**********************************************************************
 *
 * General functions
 *
 **********************************************************************/
/* send an arbitrary command to the camera */
static int do_command(struct cam_data *cam, u16 command, u8 a, u8 b, u8 c, u8 d)
{
	int retval, datasize;
	u8 cmd[8], data[8];

	switch(command) {
	case CPIA_COMMAND_GetCPIAVersion:
	case CPIA_COMMAND_GetPnPID:
	case CPIA_COMMAND_GetCameraStatus:
	case CPIA_COMMAND_GetVPVersion:
		datasize=8;
		break;
	case CPIA_COMMAND_GetColourParams:
	case CPIA_COMMAND_GetColourBalance:
	case CPIA_COMMAND_GetExposure:
		mutex_lock(&cam->param_lock);
		datasize=8;
		break;
	case CPIA_COMMAND_ReadMCPorts:
	case CPIA_COMMAND_ReadVCRegs:
		datasize = 4;
		break;
	default:
		datasize=0;
		break;
	}

	cmd[0] = command>>8;
	cmd[1] = command&0xff;
	cmd[2] = a;
	cmd[3] = b;
	cmd[4] = c;
	cmd[5] = d;
	cmd[6] = datasize;
	cmd[7] = 0;

	retval = cam->ops->transferCmd(cam->lowlevel_data, cmd, data);
	if (retval) {
		DBG("%x - failed, retval=%d\n", command, retval);
		if (command == CPIA_COMMAND_GetColourParams ||
		    command == CPIA_COMMAND_GetColourBalance ||
		    command == CPIA_COMMAND_GetExposure)
			mutex_unlock(&cam->param_lock);
	} else {
		switch(command) {
		case CPIA_COMMAND_GetCPIAVersion:
			cam->params.version.firmwareVersion = data[0];
			cam->params.version.firmwareRevision = data[1];
			cam->params.version.vcVersion = data[2];
			cam->params.version.vcRevision = data[3];
			break;
		case CPIA_COMMAND_GetPnPID:
			cam->params.pnpID.vendor = data[0]+(((u16)data[1])<<8);
			cam->params.pnpID.product = data[2]+(((u16)data[3])<<8);
			cam->params.pnpID.deviceRevision =
				data[4]+(((u16)data[5])<<8);
			break;
		case CPIA_COMMAND_GetCameraStatus:
			cam->params.status.systemState = data[0];
			cam->params.status.grabState = data[1];
			cam->params.status.streamState = data[2];
			cam->params.status.fatalError = data[3];
			cam->params.status.cmdError = data[4];
			cam->params.status.debugFlags = data[5];
			cam->params.status.vpStatus = data[6];
			cam->params.status.errorCode = data[7];
			break;
		case CPIA_COMMAND_GetVPVersion:
			cam->params.vpVersion.vpVersion = data[0];
			cam->params.vpVersion.vpRevision = data[1];
			cam->params.vpVersion.cameraHeadID =
				data[2]+(((u16)data[3])<<8);
			break;
		case CPIA_COMMAND_GetColourParams:
			cam->params.colourParams.brightness = data[0];
			cam->params.colourParams.contrast = data[1];
			cam->params.colourParams.saturation = data[2];
			mutex_unlock(&cam->param_lock);
			break;
		case CPIA_COMMAND_GetColourBalance:
			cam->params.colourBalance.redGain = data[0];
			cam->params.colourBalance.greenGain = data[1];
			cam->params.colourBalance.blueGain = data[2];
			mutex_unlock(&cam->param_lock);
			break;
		case CPIA_COMMAND_GetExposure:
			cam->params.exposure.gain = data[0];
			cam->params.exposure.fineExp = data[1];
			cam->params.exposure.coarseExpLo = data[2];
			cam->params.exposure.coarseExpHi = data[3];
			cam->params.exposure.redComp = data[4];
			cam->params.exposure.green1Comp = data[5];
			cam->params.exposure.green2Comp = data[6];
			cam->params.exposure.blueComp = data[7];
			mutex_unlock(&cam->param_lock);
			break;

		case CPIA_COMMAND_ReadMCPorts:
			if (!cam->params.qx3.qx3_detected)
				break;
			/* test button press */
			cam->params.qx3.button = ((data[1] & 0x02) == 0);
			if (cam->params.qx3.button) {
				/* button pressed - unlock the latch */
				do_command(cam,CPIA_COMMAND_WriteMCPort,3,0xDF,0xDF,0);
				do_command(cam,CPIA_COMMAND_WriteMCPort,3,0xFF,0xFF,0);
			}

			/* test whether microscope is cradled */
			cam->params.qx3.cradled = ((data[2] & 0x40) == 0);
			break;

		default:
			break;
		}
	}
	return retval;
}

/* send a command  to the camera with an additional data transaction */
static int do_command_extended(struct cam_data *cam, u16 command,
			       u8 a, u8 b, u8 c, u8 d,
			       u8 e, u8 f, u8 g, u8 h,
			       u8 i, u8 j, u8 k, u8 l)
{
	int retval;
	u8 cmd[8], data[8];

	cmd[0] = command>>8;
	cmd[1] = command&0xff;
	cmd[2] = a;
	cmd[3] = b;
	cmd[4] = c;
	cmd[5] = d;
	cmd[6] = 8;
	cmd[7] = 0;
	data[0] = e;
	data[1] = f;
	data[2] = g;
	data[3] = h;
	data[4] = i;
	data[5] = j;
	data[6] = k;
	data[7] = l;

	retval = cam->ops->transferCmd(cam->lowlevel_data, cmd, data);
	if (retval)
		DBG("%x - failed\n", command);

	return retval;
}

/**********************************************************************
 *
 * Colorspace conversion
 *
 **********************************************************************/
#define LIMIT(x) ((((x)>0xffffff)?0xff0000:(((x)<=0xffff)?0:(x)&0xff0000))>>16)

static int convert420(unsigned char *yuv, unsigned char *rgb, int out_fmt,
		      int linesize, int mmap_kludge)
{
	int y, u, v, r, g, b, y1;

	/* Odd lines use the same u and v as the previous line.
	 * Because of compression, it is necessary to get this
	 * information from the decoded image. */
	switch(out_fmt) {
	case VIDEO_PALETTE_RGB555:
		y = (*yuv++ - 16) * 76310;
		y1 = (*yuv - 16) * 76310;
		r = ((*(rgb+1-linesize)) & 0x7c) << 1;
		g = ((*(rgb-linesize)) & 0xe0) >> 4 |
		    ((*(rgb+1-linesize)) & 0x03) << 6;
		b = ((*(rgb-linesize)) & 0x1f) << 3;
		u = (-53294 * r - 104635 * g + 157929 * b) / 5756495;
		v = (157968 * r - 132278 * g - 25690 * b) / 5366159;
		r = 104635 * v;
		g = -25690 * u - 53294 * v;
		b = 132278 * u;
		*rgb++ = ((LIMIT(g+y) & 0xf8) << 2) | (LIMIT(b+y) >> 3);
		*rgb++ = ((LIMIT(r+y) & 0xf8) >> 1) | (LIMIT(g+y) >> 6);
		*rgb++ = ((LIMIT(g+y1) & 0xf8) << 2) | (LIMIT(b+y1) >> 3);
		*rgb = ((LIMIT(r+y1) & 0xf8) >> 1) | (LIMIT(g+y1) >> 6);
		return 4;
	case VIDEO_PALETTE_RGB565:
		y = (*yuv++ - 16) * 76310;
		y1 = (*yuv - 16) * 76310;
		r = (*(rgb+1-linesize)) & 0xf8;
		g = ((*(rgb-linesize)) & 0xe0) >> 3 |
		    ((*(rgb+1-linesize)) & 0x07) << 5;
		b = ((*(rgb-linesize)) & 0x1f) << 3;
		u = (-53294 * r - 104635 * g + 157929 * b) / 5756495;
		v = (157968 * r - 132278 * g - 25690 * b) / 5366159;
		r = 104635 * v;
		g = -25690 * u - 53294 * v;
		b = 132278 * u;
		*rgb++ = ((LIMIT(g+y) & 0xfc) << 3) | (LIMIT(b+y) >> 3);
		*rgb++ = (LIMIT(r+y) & 0xf8) | (LIMIT(g+y) >> 5);
		*rgb++ = ((LIMIT(g+y1) & 0xfc) << 3) | (LIMIT(b+y1) >> 3);
		*rgb = (LIMIT(r+y1) & 0xf8) | (LIMIT(g+y1) >> 5);
		return 4;
		break;
	case VIDEO_PALETTE_RGB24:
	case VIDEO_PALETTE_RGB32:
		y = (*yuv++ - 16) * 76310;
		y1 = (*yuv - 16) * 76310;
		if (mmap_kludge) {
			r = *(rgb+2-linesize);
			g = *(rgb+1-linesize);
			b = *(rgb-linesize);
		} else {
			r = *(rgb-linesize);
			g = *(rgb+1-linesize);
			b = *(rgb+2-linesize);
		}
		u = (-53294 * r - 104635 * g + 157929 * b) / 5756495;
		v = (157968 * r - 132278 * g - 25690 * b) / 5366159;
		r = 104635 * v;
		g = -25690 * u + -53294 * v;
		b = 132278 * u;
		if (mmap_kludge) {
			*rgb++ = LIMIT(b+y);
			*rgb++ = LIMIT(g+y);
			*rgb++ = LIMIT(r+y);
			if(out_fmt == VIDEO_PALETTE_RGB32)
				rgb++;
			*rgb++ = LIMIT(b+y1);
			*rgb++ = LIMIT(g+y1);
			*rgb = LIMIT(r+y1);
		} else {
			*rgb++ = LIMIT(r+y);
			*rgb++ = LIMIT(g+y);
			*rgb++ = LIMIT(b+y);
			if(out_fmt == VIDEO_PALETTE_RGB32)
				rgb++;
			*rgb++ = LIMIT(r+y1);
			*rgb++ = LIMIT(g+y1);
			*rgb = LIMIT(b+y1);
		}
		if(out_fmt == VIDEO_PALETTE_RGB32)
			return 8;
		return 6;
	case VIDEO_PALETTE_YUV422:
	case VIDEO_PALETTE_YUYV:
		y = *yuv++;
		u = *(rgb+1-linesize);
		y1 = *yuv;
		v = *(rgb+3-linesize);
		*rgb++ = y;
		*rgb++ = u;
		*rgb++ = y1;
		*rgb = v;
		return 4;
	case VIDEO_PALETTE_UYVY:
		u = *(rgb-linesize);
		y = *yuv++;
		v = *(rgb+2-linesize);
		y1 = *yuv;
		*rgb++ = u;
		*rgb++ = y;
		*rgb++ = v;
		*rgb = y1;
		return 4;
	case VIDEO_PALETTE_GREY:
		*rgb++ = *yuv++;
		*rgb = *yuv;
		return 2;
	default:
		DBG("Empty: %d\n", out_fmt);
		return 0;
	}
}


static int yuvconvert(unsigned char *yuv, unsigned char *rgb, int out_fmt,
		      int in_uyvy, int mmap_kludge)
{
	int y, u, v, r, g, b, y1;

	switch(out_fmt) {
	case VIDEO_PALETTE_RGB555:
	case VIDEO_PALETTE_RGB565:
	case VIDEO_PALETTE_RGB24:
	case VIDEO_PALETTE_RGB32:
		if (in_uyvy) {
			u = *yuv++ - 128;
			y = (*yuv++ - 16) * 76310;
			v = *yuv++ - 128;
			y1 = (*yuv - 16) * 76310;
		} else {
			y = (*yuv++ - 16) * 76310;
			u = *yuv++ - 128;
			y1 = (*yuv++ - 16) * 76310;
			v = *yuv - 128;
		}
		r = 104635 * v;
		g = -25690 * u + -53294 * v;
		b = 132278 * u;
		break;
	default:
		y = *yuv++;
		u = *yuv++;
		y1 = *yuv++;
		v = *yuv;
		/* Just to avoid compiler warnings */
		r = 0;
		g = 0;
		b = 0;
		break;
	}
	switch(out_fmt) {
	case VIDEO_PALETTE_RGB555:
		*rgb++ = ((LIMIT(g+y) & 0xf8) << 2) | (LIMIT(b+y) >> 3);
		*rgb++ = ((LIMIT(r+y) & 0xf8) >> 1) | (LIMIT(g+y) >> 6);
		*rgb++ = ((LIMIT(g+y1) & 0xf8) << 2) | (LIMIT(b+y1) >> 3);
		*rgb = ((LIMIT(r+y1) & 0xf8) >> 1) | (LIMIT(g+y1) >> 6);
		return 4;
	case VIDEO_PALETTE_RGB565:
		*rgb++ = ((LIMIT(g+y) & 0xfc) << 3) | (LIMIT(b+y) >> 3);
		*rgb++ = (LIMIT(r+y) & 0xf8) | (LIMIT(g+y) >> 5);
		*rgb++ = ((LIMIT(g+y1) & 0xfc) << 3) | (LIMIT(b+y1) >> 3);
		*rgb = (LIMIT(r+y1) & 0xf8) | (LIMIT(g+y1) >> 5);
		return 4;
	case VIDEO_PALETTE_RGB24:
		if (mmap_kludge) {
			*rgb++ = LIMIT(b+y);
			*rgb++ = LIMIT(g+y);
			*rgb++ = LIMIT(r+y);
			*rgb++ = LIMIT(b+y1);
			*rgb++ = LIMIT(g+y1);
			*rgb = LIMIT(r+y1);
		} else {
			*rgb++ = LIMIT(r+y);
			*rgb++ = LIMIT(g+y);
			*rgb++ = LIMIT(b+y);
			*rgb++ = LIMIT(r+y1);
			*rgb++ = LIMIT(g+y1);
			*rgb = LIMIT(b+y1);
		}
		return 6;
	case VIDEO_PALETTE_RGB32:
		if (mmap_kludge) {
			*rgb++ = LIMIT(b+y);
			*rgb++ = LIMIT(g+y);
			*rgb++ = LIMIT(r+y);
			rgb++;
			*rgb++ = LIMIT(b+y1);
			*rgb++ = LIMIT(g+y1);
			*rgb = LIMIT(r+y1);
		} else {
			*rgb++ = LIMIT(r+y);
			*rgb++ = LIMIT(g+y);
			*rgb++ = LIMIT(b+y);
			rgb++;
			*rgb++ = LIMIT(r+y1);
			*rgb++ = LIMIT(g+y1);
			*rgb = LIMIT(b+y1);
		}
		return 8;
	case VIDEO_PALETTE_GREY:
		*rgb++ = y;
		*rgb = y1;
		return 2;
	case VIDEO_PALETTE_YUV422:
	case VIDEO_PALETTE_YUYV:
		*rgb++ = y;
		*rgb++ = u;
		*rgb++ = y1;
		*rgb = v;
		return 4;
	case VIDEO_PALETTE_UYVY:
		*rgb++ = u;
		*rgb++ = y;
		*rgb++ = v;
		*rgb = y1;
		return 4;
	default:
		DBG("Empty: %d\n", out_fmt);
		return 0;
	}
}

static int skipcount(int count, int fmt)
{
	switch(fmt) {
	case VIDEO_PALETTE_GREY:
		return count;
	case VIDEO_PALETTE_RGB555:
	case VIDEO_PALETTE_RGB565:
	case VIDEO_PALETTE_YUV422:
	case VIDEO_PALETTE_YUYV:
	case VIDEO_PALETTE_UYVY:
		return 2*count;
	case VIDEO_PALETTE_RGB24:
		return 3*count;
	case VIDEO_PALETTE_RGB32:
		return 4*count;
	default:
		return 0;
	}
}

static int parse_picture(struct cam_data *cam, int size)
{
	u8 *obuf, *ibuf, *end_obuf;
	int ll, in_uyvy, compressed, decimation, even_line, origsize, out_fmt;
	int rows, cols, linesize, subsample_422;

	/* make sure params don't change while we are decoding */
	mutex_lock(&cam->param_lock);

	obuf = cam->decompressed_frame.data;
	end_obuf = obuf+CPIA_MAX_FRAME_SIZE;
	ibuf = cam->raw_image;
	origsize = size;
	out_fmt = cam->vp.palette;

	if ((ibuf[0] != MAGIC_0) || (ibuf[1] != MAGIC_1)) {
		LOG("header not found\n");
		mutex_unlock(&cam->param_lock);
		return -1;
	}

	if ((ibuf[16] != VIDEOSIZE_QCIF) && (ibuf[16] != VIDEOSIZE_CIF)) {
		LOG("wrong video size\n");
		mutex_unlock(&cam->param_lock);
		return -1;
	}

	if (ibuf[17] != SUBSAMPLE_420 && ibuf[17] != SUBSAMPLE_422) {
		LOG("illegal subtype %d\n",ibuf[17]);
		mutex_unlock(&cam->param_lock);
		return -1;
	}
	subsample_422 = ibuf[17] == SUBSAMPLE_422;

	if (ibuf[18] != YUVORDER_YUYV && ibuf[18] != YUVORDER_UYVY) {
		LOG("illegal yuvorder %d\n",ibuf[18]);
		mutex_unlock(&cam->param_lock);
		return -1;
	}
	in_uyvy = ibuf[18] == YUVORDER_UYVY;

	if ((ibuf[24] != cam->params.roi.colStart) ||
	    (ibuf[25] != cam->params.roi.colEnd) ||
	    (ibuf[26] != cam->params.roi.rowStart) ||
	    (ibuf[27] != cam->params.roi.rowEnd)) {
		LOG("ROI mismatch\n");
		mutex_unlock(&cam->param_lock);
		return -1;
	}
	cols = 8*(ibuf[25] - ibuf[24]);
	rows = 4*(ibuf[27] - ibuf[26]);


	if ((ibuf[28] != NOT_COMPRESSED) && (ibuf[28] != COMPRESSED)) {
		LOG("illegal compression %d\n",ibuf[28]);
		mutex_unlock(&cam->param_lock);
		return -1;
	}
	compressed = (ibuf[28] == COMPRESSED);

	if (ibuf[29] != NO_DECIMATION && ibuf[29] != DECIMATION_ENAB) {
		LOG("illegal decimation %d\n",ibuf[29]);
		mutex_unlock(&cam->param_lock);
		return -1;
	}
	decimation = (ibuf[29] == DECIMATION_ENAB);

	cam->params.yuvThreshold.yThreshold = ibuf[30];
	cam->params.yuvThreshold.uvThreshold = ibuf[31];
	cam->params.status.systemState = ibuf[32];
	cam->params.status.grabState = ibuf[33];
	cam->params.status.streamState = ibuf[34];
	cam->params.status.fatalError = ibuf[35];
	cam->params.status.cmdError = ibuf[36];
	cam->params.status.debugFlags = ibuf[37];
	cam->params.status.vpStatus = ibuf[38];
	cam->params.status.errorCode = ibuf[39];
	cam->fps = ibuf[41];
	mutex_unlock(&cam->param_lock);

	linesize = skipcount(cols, out_fmt);
	ibuf += FRAME_HEADER_SIZE;
	size -= FRAME_HEADER_SIZE;
	ll = ibuf[0] | (ibuf[1] << 8);
	ibuf += 2;
	even_line = 1;

	while (size > 0) {
		size -= (ll+2);
		if (size < 0) {
			LOG("Insufficient data in buffer\n");
			return -1;
		}

		while (ll > 1) {
			if (!compressed || (compressed && !(*ibuf & 1))) {
				if(subsample_422 || even_line) {
				obuf += yuvconvert(ibuf, obuf, out_fmt,
						   in_uyvy, cam->mmap_kludge);
				ibuf += 4;
				ll -= 4;
			} else {
					/* SUBSAMPLE_420 on an odd line */
					obuf += convert420(ibuf, obuf,
							   out_fmt, linesize,
							   cam->mmap_kludge);
					ibuf += 2;
					ll -= 2;
				}
			} else {
				/*skip compressed interval from previous frame*/
				obuf += skipcount(*ibuf >> 1, out_fmt);
				if (obuf > end_obuf) {
					LOG("Insufficient buffer size\n");
					return -1;
				}
				++ibuf;
				ll--;
			}
		}
		if (ll == 1) {
			if (*ibuf != EOL) {
				DBG("EOL not found giving up after %d/%d"
				    " bytes\n", origsize-size, origsize);
				return -1;
			}

			++ibuf; /* skip over EOL */

			if ((size > 3) && (ibuf[0] == EOI) && (ibuf[1] == EOI) &&
			   (ibuf[2] == EOI) && (ibuf[3] == EOI)) {
				size -= 4;
				break;
			}

			if(decimation) {
				/* skip the odd lines for now */
				obuf += linesize;
			}

			if (size > 1) {
				ll = ibuf[0] | (ibuf[1] << 8);
				ibuf += 2; /* skip over line length */
			}
			if(!decimation)
				even_line = !even_line;
		} else {
			LOG("line length was not 1 but %d after %d/%d bytes\n",
			    ll, origsize-size, origsize);
			return -1;
		}
	}

	if(decimation) {
		/* interpolate odd rows */
		int i, j;
		u8 *prev, *next;
		prev = cam->decompressed_frame.data;
		obuf = prev+linesize;
		next = obuf+linesize;
		for(i=1; i<rows-1; i+=2) {
			for(j=0; j<linesize; ++j) {
				*obuf++ = ((int)*prev++ + *next++) / 2;
			}
			prev += linesize;
			obuf += linesize;
			next += linesize;
		}
		/* last row is odd, just copy previous row */
		memcpy(obuf, prev, linesize);
	}

	cam->decompressed_frame.count = obuf-cam->decompressed_frame.data;

	return cam->decompressed_frame.count;
}

/* InitStreamCap wrapper to select correct start line */
static inline int init_stream_cap(struct cam_data *cam)
{
	return do_command(cam, CPIA_COMMAND_InitStreamCap,
			  0, cam->params.streamStartLine, 0, 0);
}


/*  find_over_exposure
 *    Finds a suitable value of OverExposure for use with SetFlickerCtrl
 *    Some calculation is required because this value changes with the brightness
 *    set with SetColourParameters
 *
 *  Parameters: Brightness  -  last brightness value set with SetColourParameters
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

	if (MaxAllowableOverExposure < FLICKER_ALLOWABLE_OVER_EXPOSURE) {
		OverExposure = MaxAllowableOverExposure;
	} else {
		OverExposure = FLICKER_ALLOWABLE_OVER_EXPOSURE;
	}

	return OverExposure;
}
#undef FLICKER_MAX_EXPOSURE
#undef FLICKER_ALLOWABLE_OVER_EXPOSURE
#undef FLICKER_BRIGHTNESS_CONSTANT

/* update various camera modes and settings */
static void dispatch_commands(struct cam_data *cam)
{
	mutex_lock(&cam->param_lock);
	if (cam->cmd_queue==COMMAND_NONE) {
		mutex_unlock(&cam->param_lock);
		return;
	}
	DEB_BYTE(cam->cmd_queue);
	DEB_BYTE(cam->cmd_queue>>8);
	if (cam->cmd_queue & COMMAND_SETFORMAT) {
		do_command(cam, CPIA_COMMAND_SetFormat,
			   cam->params.format.videoSize,
			   cam->params.format.subSample,
			   cam->params.format.yuvOrder, 0);
		do_command(cam, CPIA_COMMAND_SetROI,
			   cam->params.roi.colStart, cam->params.roi.colEnd,
			   cam->params.roi.rowStart, cam->params.roi.rowEnd);
		cam->first_frame = 1;
	}

	if (cam->cmd_queue & COMMAND_SETCOLOURPARAMS)
		do_command(cam, CPIA_COMMAND_SetColourParams,
			   cam->params.colourParams.brightness,
			   cam->params.colourParams.contrast,
			   cam->params.colourParams.saturation, 0);

	if (cam->cmd_queue & COMMAND_SETAPCOR)
		do_command(cam, CPIA_COMMAND_SetApcor,
			   cam->params.apcor.gain1,
			   cam->params.apcor.gain2,
			   cam->params.apcor.gain4,
			   cam->params.apcor.gain8);

	if (cam->cmd_queue & COMMAND_SETVLOFFSET)
		do_command(cam, CPIA_COMMAND_SetVLOffset,
			   cam->params.vlOffset.gain1,
			   cam->params.vlOffset.gain2,
			   cam->params.vlOffset.gain4,
			   cam->params.vlOffset.gain8);

	if (cam->cmd_queue & COMMAND_SETEXPOSURE) {
		do_command_extended(cam, CPIA_COMMAND_SetExposure,
				    cam->params.exposure.gainMode,
				    1,
				    cam->params.exposure.compMode,
				    cam->params.exposure.centreWeight,
				    cam->params.exposure.gain,
				    cam->params.exposure.fineExp,
				    cam->params.exposure.coarseExpLo,
				    cam->params.exposure.coarseExpHi,
				    cam->params.exposure.redComp,
				    cam->params.exposure.green1Comp,
				    cam->params.exposure.green2Comp,
				    cam->params.exposure.blueComp);
		if(cam->params.exposure.expMode != 1) {
			do_command_extended(cam, CPIA_COMMAND_SetExposure,
					    0,
					    cam->params.exposure.expMode,
					    0, 0,
					    cam->params.exposure.gain,
					    cam->params.exposure.fineExp,
					    cam->params.exposure.coarseExpLo,
					    cam->params.exposure.coarseExpHi,
					    0, 0, 0, 0);
		}
	}

	if (cam->cmd_queue & COMMAND_SETCOLOURBALANCE) {
		if (cam->params.colourBalance.balanceMode == 1) {
			do_command(cam, CPIA_COMMAND_SetColourBalance,
				   1,
				   cam->params.colourBalance.redGain,
				   cam->params.colourBalance.greenGain,
				   cam->params.colourBalance.blueGain);
			do_command(cam, CPIA_COMMAND_SetColourBalance,
				   3, 0, 0, 0);
		}
		if (cam->params.colourBalance.balanceMode == 2) {
			do_command(cam, CPIA_COMMAND_SetColourBalance,
				   2, 0, 0, 0);
		}
		if (cam->params.colourBalance.balanceMode == 3) {
			do_command(cam, CPIA_COMMAND_SetColourBalance,
				   3, 0, 0, 0);
		}
	}

	if (cam->cmd_queue & COMMAND_SETCOMPRESSIONTARGET)
		do_command(cam, CPIA_COMMAND_SetCompressionTarget,
			   cam->params.compressionTarget.frTargeting,
			   cam->params.compressionTarget.targetFR,
			   cam->params.compressionTarget.targetQ, 0);

	if (cam->cmd_queue & COMMAND_SETYUVTHRESH)
		do_command(cam, CPIA_COMMAND_SetYUVThresh,
			   cam->params.yuvThreshold.yThreshold,
			   cam->params.yuvThreshold.uvThreshold, 0, 0);

	if (cam->cmd_queue & COMMAND_SETCOMPRESSIONPARAMS)
		do_command_extended(cam, CPIA_COMMAND_SetCompressionParams,
			    0, 0, 0, 0,
			    cam->params.compressionParams.hysteresis,
			    cam->params.compressionParams.threshMax,
			    cam->params.compressionParams.smallStep,
			    cam->params.compressionParams.largeStep,
			    cam->params.compressionParams.decimationHysteresis,
			    cam->params.compressionParams.frDiffStepThresh,
			    cam->params.compressionParams.qDiffStepThresh,
			    cam->params.compressionParams.decimationThreshMod);

	if (cam->cmd_queue & COMMAND_SETCOMPRESSION)
		do_command(cam, CPIA_COMMAND_SetCompression,
			   cam->params.compression.mode,
			   cam->params.compression.decimation, 0, 0);

	if (cam->cmd_queue & COMMAND_SETSENSORFPS)
		do_command(cam, CPIA_COMMAND_SetSensorFPS,
			   cam->params.sensorFps.divisor,
			   cam->params.sensorFps.baserate, 0, 0);

	if (cam->cmd_queue & COMMAND_SETFLICKERCTRL)
		do_command(cam, CPIA_COMMAND_SetFlickerCtrl,
			   cam->params.flickerControl.flickerMode,
			   cam->params.flickerControl.coarseJump,
			   abs(cam->params.flickerControl.allowableOverExposure),
			   0);

	if (cam->cmd_queue & COMMAND_SETECPTIMING)
		do_command(cam, CPIA_COMMAND_SetECPTiming,
			   cam->params.ecpTiming, 0, 0, 0);

	if (cam->cmd_queue & COMMAND_PAUSE)
		do_command(cam, CPIA_COMMAND_EndStreamCap, 0, 0, 0, 0);

	if (cam->cmd_queue & COMMAND_RESUME)
		init_stream_cap(cam);

	if (cam->cmd_queue & COMMAND_SETLIGHTS && cam->params.qx3.qx3_detected)
	  {
	    int p1 = (cam->params.qx3.bottomlight == 0) << 1;
	    int p2 = (cam->params.qx3.toplight == 0) << 3;
	    do_command(cam, CPIA_COMMAND_WriteVCReg,  0x90, 0x8F, 0x50, 0);
	    do_command(cam, CPIA_COMMAND_WriteMCPort, 2, 0, (p1|p2|0xE0), 0);
	  }

	cam->cmd_queue = COMMAND_NONE;
	mutex_unlock(&cam->param_lock);
	return;
}



static void set_flicker(struct cam_params *params, volatile u32 *command_flags,
			int on)
{
	/* Everything in here is from the Windows driver */
#define FIRMWARE_VERSION(x,y) (params->version.firmwareVersion == (x) && \
			       params->version.firmwareRevision == (y))
/* define for compgain calculation */
#if 0
#define COMPGAIN(base, curexp, newexp) \
    (u8) ((((float) base - 128.0) * ((float) curexp / (float) newexp)) + 128.5)
#define EXP_FROM_COMP(basecomp, curcomp, curexp) \
    (u16)((float)curexp * (float)(u8)(curcomp + 128) / (float)(u8)(basecomp - 128))
#else
  /* equivalent functions without floating point math */
#define COMPGAIN(base, curexp, newexp) \
    (u8)(128 + (((u32)(2*(base-128)*curexp + newexp)) / (2* newexp)) )
#define EXP_FROM_COMP(basecomp, curcomp, curexp) \
     (u16)(((u32)(curexp * (u8)(curcomp + 128)) / (u8)(basecomp - 128)))
#endif


	int currentexp = params->exposure.coarseExpLo +
			 params->exposure.coarseExpHi*256;
	int startexp;
	if (on) {
		int cj = params->flickerControl.coarseJump;
		params->flickerControl.flickerMode = 1;
		params->flickerControl.disabled = 0;
		if(params->exposure.expMode != 2)
			*command_flags |= COMMAND_SETEXPOSURE;
		params->exposure.expMode = 2;
		currentexp = currentexp << params->exposure.gain;
		params->exposure.gain = 0;
		/* round down current exposure to nearest value */
		startexp = (currentexp + ROUND_UP_EXP_FOR_FLICKER) / cj;
		if(startexp < 1)
			startexp = 1;
		startexp = (startexp * cj) - 1;
		if(FIRMWARE_VERSION(1,2))
			while(startexp > MAX_EXP_102)
				startexp -= cj;
		else
			while(startexp > MAX_EXP)
				startexp -= cj;
		params->exposure.coarseExpLo = startexp & 0xff;
		params->exposure.coarseExpHi = startexp >> 8;
		if (currentexp > startexp) {
			if (currentexp > (2 * startexp))
				currentexp = 2 * startexp;
			params->exposure.redComp = COMPGAIN (COMP_RED, currentexp, startexp);
			params->exposure.green1Comp = COMPGAIN (COMP_GREEN1, currentexp, startexp);
			params->exposure.green2Comp = COMPGAIN (COMP_GREEN2, currentexp, startexp);
			params->exposure.blueComp = COMPGAIN (COMP_BLUE, currentexp, startexp);
		} else {
			params->exposure.redComp = COMP_RED;
			params->exposure.green1Comp = COMP_GREEN1;
			params->exposure.green2Comp = COMP_GREEN2;
			params->exposure.blueComp = COMP_BLUE;
		}
		if(FIRMWARE_VERSION(1,2))
			params->exposure.compMode = 0;
		else
			params->exposure.compMode = 1;

		params->apcor.gain1 = 0x18;
		params->apcor.gain2 = 0x18;
		params->apcor.gain4 = 0x16;
		params->apcor.gain8 = 0x14;
		*command_flags |= COMMAND_SETAPCOR;
	} else {
		params->flickerControl.flickerMode = 0;
		params->flickerControl.disabled = 1;
		/* Coarse = average of equivalent coarse for each comp channel */
		startexp = EXP_FROM_COMP(COMP_RED, params->exposure.redComp, currentexp);
		startexp += EXP_FROM_COMP(COMP_GREEN1, params->exposure.green1Comp, currentexp);
		startexp += EXP_FROM_COMP(COMP_GREEN2, params->exposure.green2Comp, currentexp);
		startexp += EXP_FROM_COMP(COMP_BLUE, params->exposure.blueComp, currentexp);
		startexp = startexp >> 2;
		while(startexp > MAX_EXP &&
		      params->exposure.gain < params->exposure.gainMode-1) {
			startexp = startexp >> 1;
			++params->exposure.gain;
		}
		if(FIRMWARE_VERSION(1,2) && startexp > MAX_EXP_102)
			startexp = MAX_EXP_102;
		if(startexp > MAX_EXP)
			startexp = MAX_EXP;
		params->exposure.coarseExpLo = startexp&0xff;
		params->exposure.coarseExpHi = startexp >> 8;
		params->exposure.redComp = COMP_RED;
		params->exposure.green1Comp = COMP_GREEN1;
		params->exposure.green2Comp = COMP_GREEN2;
		params->exposure.blueComp = COMP_BLUE;
		params->exposure.compMode = 1;
		*command_flags |= COMMAND_SETEXPOSURE;
		params->apcor.gain1 = 0x18;
		params->apcor.gain2 = 0x16;
		params->apcor.gain4 = 0x24;
		params->apcor.gain8 = 0x34;
		*command_flags |= COMMAND_SETAPCOR;
	}
	params->vlOffset.gain1 = 20;
	params->vlOffset.gain2 = 24;
	params->vlOffset.gain4 = 26;
	params->vlOffset.gain8 = 26;
	*command_flags |= COMMAND_SETVLOFFSET;
#undef FIRMWARE_VERSION
#undef EXP_FROM_COMP
#undef COMPGAIN
}

#define FIRMWARE_VERSION(x,y) (cam->params.version.firmwareVersion == (x) && \
			       cam->params.version.firmwareRevision == (y))
/* monitor the exposure and adjust the sensor frame rate if needed */
static void monitor_exposure(struct cam_data *cam)
{
	u8 exp_acc, bcomp, gain, coarseL, cmd[8], data[8];
	int retval, light_exp, dark_exp, very_dark_exp;
	int old_exposure, new_exposure, framerate;

	/* get necessary stats and register settings from camera */
	/* do_command can't handle this, so do it ourselves */
	cmd[0] = CPIA_COMMAND_ReadVPRegs>>8;
	cmd[1] = CPIA_COMMAND_ReadVPRegs&0xff;
	cmd[2] = 30;
	cmd[3] = 4;
	cmd[4] = 9;
	cmd[5] = 8;
	cmd[6] = 8;
	cmd[7] = 0;
	retval = cam->ops->transferCmd(cam->lowlevel_data, cmd, data);
	if (retval) {
		LOG("ReadVPRegs(30,4,9,8) - failed, retval=%d\n",
		    retval);
		return;
	}
	exp_acc = data[0];
	bcomp = data[1];
	gain = data[2];
	coarseL = data[3];

	mutex_lock(&cam->param_lock);
	light_exp = cam->params.colourParams.brightness +
		    TC - 50 + EXP_ACC_LIGHT;
	if(light_exp > 255)
		light_exp = 255;
	dark_exp = cam->params.colourParams.brightness +
		   TC - 50 - EXP_ACC_DARK;
	if(dark_exp < 0)
		dark_exp = 0;
	very_dark_exp = dark_exp/2;

	old_exposure = cam->params.exposure.coarseExpHi * 256 +
		       cam->params.exposure.coarseExpLo;

	if(!cam->params.flickerControl.disabled) {
		/* Flicker control on */
		int max_comp = FIRMWARE_VERSION(1,2) ? MAX_COMP : HIGH_COMP_102;
		bcomp += 128;	/* decode */
		if(bcomp >= max_comp && exp_acc < dark_exp) {
			/* dark */
			if(exp_acc < very_dark_exp) {
				/* very dark */
				if(cam->exposure_status == EXPOSURE_VERY_DARK)
					++cam->exposure_count;
				else {
					cam->exposure_status = EXPOSURE_VERY_DARK;
					cam->exposure_count = 1;
				}
			} else {
				/* just dark */
				if(cam->exposure_status == EXPOSURE_DARK)
					++cam->exposure_count;
				else {
					cam->exposure_status = EXPOSURE_DARK;
					cam->exposure_count = 1;
				}
			}
		} else if(old_exposure <= LOW_EXP || exp_acc > light_exp) {
			/* light */
			if(old_exposure <= VERY_LOW_EXP) {
				/* very light */
				if(cam->exposure_status == EXPOSURE_VERY_LIGHT)
					++cam->exposure_count;
				else {
					cam->exposure_status = EXPOSURE_VERY_LIGHT;
					cam->exposure_count = 1;
				}
			} else {
				/* just light */
				if(cam->exposure_status == EXPOSURE_LIGHT)
					++cam->exposure_count;
				else {
					cam->exposure_status = EXPOSURE_LIGHT;
					cam->exposure_count = 1;
				}
			}
		} else {
			/* not dark or light */
			cam->exposure_status = EXPOSURE_NORMAL;
		}
	} else {
		/* Flicker control off */
		if(old_exposure >= MAX_EXP && exp_acc < dark_exp) {
			/* dark */
			if(exp_acc < very_dark_exp) {
				/* very dark */
				if(cam->exposure_status == EXPOSURE_VERY_DARK)
					++cam->exposure_count;
				else {
					cam->exposure_status = EXPOSURE_VERY_DARK;
					cam->exposure_count = 1;
				}
			} else {
				/* just dark */
				if(cam->exposure_status == EXPOSURE_DARK)
					++cam->exposure_count;
				else {
					cam->exposure_status = EXPOSURE_DARK;
					cam->exposure_count = 1;
				}
			}
		} else if(old_exposure <= LOW_EXP || exp_acc > light_exp) {
			/* light */
			if(old_exposure <= VERY_LOW_EXP) {
				/* very light */
				if(cam->exposure_status == EXPOSURE_VERY_LIGHT)
					++cam->exposure_count;
				else {
					cam->exposure_status = EXPOSURE_VERY_LIGHT;
					cam->exposure_count = 1;
				}
			} else {
				/* just light */
				if(cam->exposure_status == EXPOSURE_LIGHT)
					++cam->exposure_count;
				else {
					cam->exposure_status = EXPOSURE_LIGHT;
					cam->exposure_count = 1;
				}
			}
		} else {
			/* not dark or light */
			cam->exposure_status = EXPOSURE_NORMAL;
		}
	}

	framerate = cam->fps;
	if(framerate > 30 || framerate < 1)
		framerate = 1;

	if(!cam->params.flickerControl.disabled) {
		/* Flicker control on */
		if((cam->exposure_status == EXPOSURE_VERY_DARK ||
		    cam->exposure_status == EXPOSURE_DARK) &&
		   cam->exposure_count >= DARK_TIME*framerate &&
		   cam->params.sensorFps.divisor < 3) {

			/* dark for too long */
			++cam->params.sensorFps.divisor;
			cam->cmd_queue |= COMMAND_SETSENSORFPS;

			cam->params.flickerControl.coarseJump =
				flicker_jumps[cam->mainsFreq]
					     [cam->params.sensorFps.baserate]
					     [cam->params.sensorFps.divisor];
			cam->cmd_queue |= COMMAND_SETFLICKERCTRL;

			new_exposure = cam->params.flickerControl.coarseJump-1;
			while(new_exposure < old_exposure/2)
				new_exposure += cam->params.flickerControl.coarseJump;
			cam->params.exposure.coarseExpLo = new_exposure & 0xff;
			cam->params.exposure.coarseExpHi = new_exposure >> 8;
			cam->cmd_queue |= COMMAND_SETEXPOSURE;
			cam->exposure_status = EXPOSURE_NORMAL;
			LOG("Automatically decreasing sensor_fps\n");

		} else if((cam->exposure_status == EXPOSURE_VERY_LIGHT ||
		    cam->exposure_status == EXPOSURE_LIGHT) &&
		   cam->exposure_count >= LIGHT_TIME*framerate &&
		   cam->params.sensorFps.divisor > 0) {

			/* light for too long */
			int max_exp = FIRMWARE_VERSION(1,2) ? MAX_EXP_102 : MAX_EXP ;

			--cam->params.sensorFps.divisor;
			cam->cmd_queue |= COMMAND_SETSENSORFPS;

			cam->params.flickerControl.coarseJump =
				flicker_jumps[cam->mainsFreq]
					     [cam->params.sensorFps.baserate]
					     [cam->params.sensorFps.divisor];
			cam->cmd_queue |= COMMAND_SETFLICKERCTRL;

			new_exposure = cam->params.flickerControl.coarseJump-1;
			while(new_exposure < 2*old_exposure &&
			      new_exposure+
			      cam->params.flickerControl.coarseJump < max_exp)
				new_exposure += cam->params.flickerControl.coarseJump;
			cam->params.exposure.coarseExpLo = new_exposure & 0xff;
			cam->params.exposure.coarseExpHi = new_exposure >> 8;
			cam->cmd_queue |= COMMAND_SETEXPOSURE;
			cam->exposure_status = EXPOSURE_NORMAL;
			LOG("Automatically increasing sensor_fps\n");
		}
	} else {
		/* Flicker control off */
		if((cam->exposure_status == EXPOSURE_VERY_DARK ||
		    cam->exposure_status == EXPOSURE_DARK) &&
		   cam->exposure_count >= DARK_TIME*framerate &&
		   cam->params.sensorFps.divisor < 3) {

			/* dark for too long */
			++cam->params.sensorFps.divisor;
			cam->cmd_queue |= COMMAND_SETSENSORFPS;

			if(cam->params.exposure.gain > 0) {
				--cam->params.exposure.gain;
				cam->cmd_queue |= COMMAND_SETEXPOSURE;
			}
			cam->exposure_status = EXPOSURE_NORMAL;
			LOG("Automatically decreasing sensor_fps\n");

		} else if((cam->exposure_status == EXPOSURE_VERY_LIGHT ||
		    cam->exposure_status == EXPOSURE_LIGHT) &&
		   cam->exposure_count >= LIGHT_TIME*framerate &&
		   cam->params.sensorFps.divisor > 0) {

			/* light for too long */
			--cam->params.sensorFps.divisor;
			cam->cmd_queue |= COMMAND_SETSENSORFPS;

			if(cam->params.exposure.gain <
			   cam->params.exposure.gainMode-1) {
				++cam->params.exposure.gain;
				cam->cmd_queue |= COMMAND_SETEXPOSURE;
			}
			cam->exposure_status = EXPOSURE_NORMAL;
			LOG("Automatically increasing sensor_fps\n");
		}
	}
	mutex_unlock(&cam->param_lock);
}

/*-----------------------------------------------------------------*/
/* if flicker is switched off, this function switches it back on.It checks,
   however, that conditions are suitable before restarting it.
   This should only be called for firmware version 1.2.

   It also adjust the colour balance when an exposure step is detected - as
   long as flicker is running
*/
static void restart_flicker(struct cam_data *cam)
{
	int cam_exposure, old_exp;
	if(!FIRMWARE_VERSION(1,2))
		return;
	mutex_lock(&cam->param_lock);
	if(cam->params.flickerControl.flickerMode == 0 ||
	   cam->raw_image[39] == 0) {
		mutex_unlock(&cam->param_lock);
		return;
	}
	cam_exposure = cam->raw_image[39]*2;
	old_exp = cam->params.exposure.coarseExpLo +
		  cam->params.exposure.coarseExpHi*256;
	/*
	  see how far away camera exposure is from a valid
	  flicker exposure value
	*/
	cam_exposure %= cam->params.flickerControl.coarseJump;
	if(!cam->params.flickerControl.disabled &&
	   cam_exposure <= cam->params.flickerControl.coarseJump - 3) {
		/* Flicker control auto-disabled */
		cam->params.flickerControl.disabled = 1;
	}

	if(cam->params.flickerControl.disabled &&
	   cam->params.flickerControl.flickerMode &&
	   old_exp > cam->params.flickerControl.coarseJump +
		     ROUND_UP_EXP_FOR_FLICKER) {
		/* exposure is now high enough to switch
		   flicker control back on */
		set_flicker(&cam->params, &cam->cmd_queue, 1);
		if((cam->cmd_queue & COMMAND_SETEXPOSURE) &&
		   cam->params.exposure.expMode == 2)
			cam->exposure_status = EXPOSURE_NORMAL;

	}
	mutex_unlock(&cam->param_lock);
}
#undef FIRMWARE_VERSION

static int clear_stall(struct cam_data *cam)
{
	/* FIXME: Does this actually work? */
	LOG("Clearing stall\n");

	cam->ops->streamRead(cam->lowlevel_data, cam->raw_image, 0);
	do_command(cam, CPIA_COMMAND_GetCameraStatus,0,0,0,0);
	return cam->params.status.streamState != STREAM_PAUSED;
}

/* kernel thread function to read image from camera */
static int fetch_frame(void *data)
{
	int image_size, retry;
	struct cam_data *cam = (struct cam_data *)data;
	unsigned long oldjif, rate, diff;

	/* Allow up to two bad images in a row to be read and
	 * ignored before an error is reported */
	for (retry = 0; retry < 3; ++retry) {
		if (retry)
			DBG("retry=%d\n", retry);

		if (!cam->ops)
			continue;

		/* load first frame always uncompressed */
		if (cam->first_frame &&
		    cam->params.compression.mode != CPIA_COMPRESSION_NONE) {
			do_command(cam, CPIA_COMMAND_SetCompression,
				   CPIA_COMPRESSION_NONE,
				   NO_DECIMATION, 0, 0);
			/* Trial & error - Discarding a frame prevents the
			   first frame from having an error in the data. */
			do_command(cam, CPIA_COMMAND_DiscardFrame, 0, 0, 0, 0);
		}

		/* init camera upload */
		if (do_command(cam, CPIA_COMMAND_GrabFrame, 0,
			       cam->params.streamStartLine, 0, 0))
			continue;

		if (cam->ops->wait_for_stream_ready) {
			/* loop until image ready */
			int count = 0;
			do_command(cam, CPIA_COMMAND_GetCameraStatus,0,0,0,0);
			while (cam->params.status.streamState != STREAM_READY) {
				if(++count > READY_TIMEOUT)
					break;
				if(cam->params.status.streamState ==
				   STREAM_PAUSED) {
					/* Bad news */
					if(!clear_stall(cam))
						return -EIO;
				}

				cond_resched();

				/* sleep for 10 ms, hopefully ;) */
				msleep_interruptible(10);
				if (signal_pending(current))
					return -EINTR;

				do_command(cam, CPIA_COMMAND_GetCameraStatus,
					   0, 0, 0, 0);
			}
			if(cam->params.status.streamState != STREAM_READY) {
				continue;
			}
		}

		cond_resched();

		/* grab image from camera */
		oldjif = jiffies;
		image_size = cam->ops->streamRead(cam->lowlevel_data,
						  cam->raw_image, 0);
		if (image_size <= 0) {
			DBG("streamRead failed: %d\n", image_size);
			continue;
		}

		rate = image_size * HZ / 1024;
		diff = jiffies-oldjif;
		cam->transfer_rate = diff==0 ? rate : rate/diff;
			/* diff==0 ? unlikely but possible */

		/* Switch flicker control back on if it got turned off */
		restart_flicker(cam);

		/* If AEC is enabled, monitor the exposure and
		   adjust the sensor frame rate if needed */
		if(cam->params.exposure.expMode == 2)
			monitor_exposure(cam);

		/* camera idle now so dispatch queued commands */
		dispatch_commands(cam);

		/* Update our knowledge of the camera state */
		do_command(cam, CPIA_COMMAND_GetColourBalance, 0, 0, 0, 0);
		do_command(cam, CPIA_COMMAND_GetExposure, 0, 0, 0, 0);
		do_command(cam, CPIA_COMMAND_ReadMCPorts, 0, 0, 0, 0);

		/* decompress and convert image to by copying it from
		 * raw_image to decompressed_frame
		 */

		cond_resched();

		cam->image_size = parse_picture(cam, image_size);
		if (cam->image_size <= 0) {
			DBG("parse_picture failed %d\n", cam->image_size);
			if(cam->params.compression.mode !=
			   CPIA_COMPRESSION_NONE) {
				/* Compression may not work right if we
				   had a bad frame, get the next one
				   uncompressed. */
				cam->first_frame = 1;
				do_command(cam, CPIA_COMMAND_SetGrabMode,
					   CPIA_GRAB_SINGLE, 0, 0, 0);
				/* FIXME: Trial & error - need up to 70ms for
				   the grab mode change to complete ? */
				msleep_interruptible(70);
				if (signal_pending(current))
					return -EINTR;
			}
		} else
			break;
	}

	if (retry < 3) {
		/* FIXME: this only works for double buffering */
		if (cam->frame[cam->curframe].state == FRAME_READY) {
			memcpy(cam->frame[cam->curframe].data,
			       cam->decompressed_frame.data,
			       cam->decompressed_frame.count);
			cam->frame[cam->curframe].state = FRAME_DONE;
		} else
			cam->decompressed_frame.state = FRAME_DONE;

		if (cam->first_frame) {
			cam->first_frame = 0;
			do_command(cam, CPIA_COMMAND_SetCompression,
				   cam->params.compression.mode,
				   cam->params.compression.decimation, 0, 0);

			/* Switch from single-grab to continuous grab */
			do_command(cam, CPIA_COMMAND_SetGrabMode,
				   CPIA_GRAB_CONTINUOUS, 0, 0, 0);
		}
		return 0;
	}
	return -EIO;
}

static int capture_frame(struct cam_data *cam, struct video_mmap *vm)
{
	if (!cam->frame_buf) {
		/* we do lazy allocation */
		int err;
		if ((err = allocate_frame_buf(cam)))
			return err;
	}

	cam->curframe = vm->frame;
	cam->frame[cam->curframe].state = FRAME_READY;
	return fetch_frame(cam);
}

static int goto_high_power(struct cam_data *cam)
{
	if (do_command(cam, CPIA_COMMAND_GotoHiPower, 0, 0, 0, 0))
		return -EIO;
	msleep_interruptible(40);	/* windows driver does it too */
	if(signal_pending(current))
		return -EINTR;
	if (do_command(cam, CPIA_COMMAND_GetCameraStatus, 0, 0, 0, 0))
		return -EIO;
	if (cam->params.status.systemState == HI_POWER_STATE) {
		DBG("camera now in HIGH power state\n");
		return 0;
	}
	printstatus(cam);
	return -EIO;
}

static int goto_low_power(struct cam_data *cam)
{
	if (do_command(cam, CPIA_COMMAND_GotoLoPower, 0, 0, 0, 0))
		return -1;
	if (do_command(cam, CPIA_COMMAND_GetCameraStatus, 0, 0, 0, 0))
		return -1;
	if (cam->params.status.systemState == LO_POWER_STATE) {
		DBG("camera now in LOW power state\n");
		return 0;
	}
	printstatus(cam);
	return -1;
}

static void save_camera_state(struct cam_data *cam)
{
	if(!(cam->cmd_queue & COMMAND_SETCOLOURBALANCE))
		do_command(cam, CPIA_COMMAND_GetColourBalance, 0, 0, 0, 0);
	if(!(cam->cmd_queue & COMMAND_SETEXPOSURE))
		do_command(cam, CPIA_COMMAND_GetExposure, 0, 0, 0, 0);

	DBG("%d/%d/%d/%d/%d/%d/%d/%d\n",
	     cam->params.exposure.gain,
	     cam->params.exposure.fineExp,
	     cam->params.exposure.coarseExpLo,
	     cam->params.exposure.coarseExpHi,
	     cam->params.exposure.redComp,
	     cam->params.exposure.green1Comp,
	     cam->params.exposure.green2Comp,
	     cam->params.exposure.blueComp);
	DBG("%d/%d/%d\n",
	     cam->params.colourBalance.redGain,
	     cam->params.colourBalance.greenGain,
	     cam->params.colourBalance.blueGain);
}

static int set_camera_state(struct cam_data *cam)
{
	cam->cmd_queue = COMMAND_SETCOMPRESSION |
			 COMMAND_SETCOMPRESSIONTARGET |
			 COMMAND_SETCOLOURPARAMS |
			 COMMAND_SETFORMAT |
			 COMMAND_SETYUVTHRESH |
			 COMMAND_SETECPTIMING |
			 COMMAND_SETCOMPRESSIONPARAMS |
			 COMMAND_SETEXPOSURE |
			 COMMAND_SETCOLOURBALANCE |
			 COMMAND_SETSENSORFPS |
			 COMMAND_SETAPCOR |
			 COMMAND_SETFLICKERCTRL |
			 COMMAND_SETVLOFFSET;

	do_command(cam, CPIA_COMMAND_SetGrabMode, CPIA_GRAB_SINGLE,0,0,0);
	dispatch_commands(cam);

	/* Wait 6 frames for the sensor to get all settings and
	   AEC/ACB to settle */
	msleep_interruptible(6*(cam->params.sensorFps.baserate ? 33 : 40) *
			       (1 << cam->params.sensorFps.divisor) + 10);

	if(signal_pending(current))
		return -EINTR;

	save_camera_state(cam);

	return 0;
}

static void get_version_information(struct cam_data *cam)
{
	/* GetCPIAVersion */
	do_command(cam, CPIA_COMMAND_GetCPIAVersion, 0, 0, 0, 0);

	/* GetPnPID */
	do_command(cam, CPIA_COMMAND_GetPnPID, 0, 0, 0, 0);
}

/* initialize camera */
static int reset_camera(struct cam_data *cam)
{
	int err;
	/* Start the camera in low power mode */
	if (goto_low_power(cam)) {
		if (cam->params.status.systemState != WARM_BOOT_STATE)
			return -ENODEV;

		/* FIXME: this is just dirty trial and error */
		err = goto_high_power(cam);
		if(err)
			return err;
		do_command(cam, CPIA_COMMAND_DiscardFrame, 0, 0, 0, 0);
		if (goto_low_power(cam))
			return -ENODEV;
	}

	/* procedure described in developer's guide p3-28 */

	/* Check the firmware version. */
	cam->params.version.firmwareVersion = 0;
	get_version_information(cam);
	if (cam->params.version.firmwareVersion != 1)
		return -ENODEV;

	/* A bug in firmware 1-02 limits gainMode to 2 */
	if(cam->params.version.firmwareRevision <= 2 &&
	   cam->params.exposure.gainMode > 2) {
		cam->params.exposure.gainMode = 2;
	}

	/* set QX3 detected flag */
	cam->params.qx3.qx3_detected = (cam->params.pnpID.vendor == 0x0813 &&
					cam->params.pnpID.product == 0x0001);

	/* The fatal error checking should be done after
	 * the camera powers up (developer's guide p 3-38) */

	/* Set streamState before transition to high power to avoid bug
	 * in firmware 1-02 */
	do_command(cam, CPIA_COMMAND_ModifyCameraStatus, STREAMSTATE, 0,
		   STREAM_NOT_READY, 0);

	/* GotoHiPower */
	err = goto_high_power(cam);
	if (err)
		return err;

	/* Check the camera status */
	if (do_command(cam, CPIA_COMMAND_GetCameraStatus, 0, 0, 0, 0))
		return -EIO;

	if (cam->params.status.fatalError) {
		DBG("fatal_error:              %#04x\n",
		    cam->params.status.fatalError);
		DBG("vp_status:                %#04x\n",
		    cam->params.status.vpStatus);
		if (cam->params.status.fatalError & ~(COM_FLAG|CPIA_FLAG)) {
			/* Fatal error in camera */
			return -EIO;
		} else if (cam->params.status.fatalError & (COM_FLAG|CPIA_FLAG)) {
			/* Firmware 1-02 may do this for parallel port cameras,
			 * just clear the flags (developer's guide p 3-38) */
			do_command(cam, CPIA_COMMAND_ModifyCameraStatus,
				   FATALERROR, ~(COM_FLAG|CPIA_FLAG), 0, 0);
		}
	}

	/* Check the camera status again */
	if (cam->params.status.fatalError) {
		if (cam->params.status.fatalError)
			return -EIO;
	}

	/* VPVersion can't be retrieved before the camera is in HiPower,
	 * so get it here instead of in get_version_information. */
	do_command(cam, CPIA_COMMAND_GetVPVersion, 0, 0, 0, 0);

	/* set camera to a known state */
	return set_camera_state(cam);
}

static void put_cam(struct cpia_camera_ops* ops)
{
	if (ops->owner)
		module_put(ops->owner);
}

/* ------------------------- V4L interface --------------------- */
static int cpia_open(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct cam_data *cam = dev->priv;
	int err;

	if (!cam) {
		DBG("Internal error, cam_data not found!\n");
		return -ENODEV;
	}

	if (cam->open_count > 0) {
		DBG("Camera already open\n");
		return -EBUSY;
	}

	if (!try_module_get(cam->ops->owner))
		return -ENODEV;

	mutex_lock(&cam->busy_lock);
	err = -ENOMEM;
	if (!cam->raw_image) {
		cam->raw_image = rvmalloc(CPIA_MAX_IMAGE_SIZE);
		if (!cam->raw_image)
			goto oops;
	}

	if (!cam->decompressed_frame.data) {
		cam->decompressed_frame.data = rvmalloc(CPIA_MAX_FRAME_SIZE);
		if (!cam->decompressed_frame.data)
			goto oops;
	}

	/* open cpia */
	err = -ENODEV;
	if (cam->ops->open(cam->lowlevel_data))
		goto oops;

	/* reset the camera */
	if ((err = reset_camera(cam)) != 0) {
		cam->ops->close(cam->lowlevel_data);
		goto oops;
	}

	err = -EINTR;
	if(signal_pending(current))
		goto oops;

	/* Set ownership of /proc/cpia/videoX to current user */
	if(cam->proc_entry)
		cam->proc_entry->uid = current->uid;

	/* set mark for loading first frame uncompressed */
	cam->first_frame = 1;

	/* init it to something */
	cam->mmap_kludge = 0;

	++cam->open_count;
	file->private_data = dev;
	mutex_unlock(&cam->busy_lock);
	return 0;

 oops:
	if (cam->decompressed_frame.data) {
		rvfree(cam->decompressed_frame.data, CPIA_MAX_FRAME_SIZE);
		cam->decompressed_frame.data = NULL;
	}
	if (cam->raw_image) {
		rvfree(cam->raw_image, CPIA_MAX_IMAGE_SIZE);
		cam->raw_image = NULL;
	}
	mutex_unlock(&cam->busy_lock);
	put_cam(cam->ops);
	return err;
}

static int cpia_close(struct inode *inode, struct file *file)
{
	struct  video_device *dev = file->private_data;
	struct cam_data *cam = dev->priv;

	if (cam->ops) {
		/* Return ownership of /proc/cpia/videoX to root */
		if(cam->proc_entry)
			cam->proc_entry->uid = 0;

		/* save camera state for later open (developers guide ch 3.5.3) */
		save_camera_state(cam);

		/* GotoLoPower */
		goto_low_power(cam);

		/* Update the camera status */
		do_command(cam, CPIA_COMMAND_GetCameraStatus, 0, 0, 0, 0);

		/* cleanup internal state stuff */
		free_frames(cam->frame);

		/* close cpia */
		cam->ops->close(cam->lowlevel_data);

		put_cam(cam->ops);
	}

	if (--cam->open_count == 0) {
		/* clean up capture-buffers */
		if (cam->raw_image) {
			rvfree(cam->raw_image, CPIA_MAX_IMAGE_SIZE);
			cam->raw_image = NULL;
		}

		if (cam->decompressed_frame.data) {
			rvfree(cam->decompressed_frame.data, CPIA_MAX_FRAME_SIZE);
			cam->decompressed_frame.data = NULL;
		}

		if (cam->frame_buf)
			free_frame_buf(cam);

		if (!cam->ops)
			kfree(cam);
	}
	file->private_data = NULL;

	return 0;
}

static ssize_t cpia_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct video_device *dev = file->private_data;
	struct cam_data *cam = dev->priv;
	int err;

	/* make this _really_ smp and multithread-safe */
	if (mutex_lock_interruptible(&cam->busy_lock))
		return -EINTR;

	if (!buf) {
		DBG("buf NULL\n");
		mutex_unlock(&cam->busy_lock);
		return -EINVAL;
	}

	if (!count) {
		DBG("count 0\n");
		mutex_unlock(&cam->busy_lock);
		return 0;
	}

	if (!cam->ops) {
		DBG("ops NULL\n");
		mutex_unlock(&cam->busy_lock);
		return -ENODEV;
	}

	/* upload frame */
	cam->decompressed_frame.state = FRAME_READY;
	cam->mmap_kludge=0;
	if((err = fetch_frame(cam)) != 0) {
		DBG("ERROR from fetch_frame: %d\n", err);
		mutex_unlock(&cam->busy_lock);
		return err;
	}
	cam->decompressed_frame.state = FRAME_UNUSED;

	/* copy data to user space */
	if (cam->decompressed_frame.count > count) {
		DBG("count wrong: %d, %lu\n", cam->decompressed_frame.count,
		    (unsigned long) count);
		mutex_unlock(&cam->busy_lock);
		return -EFAULT;
	}
	if (copy_to_user(buf, cam->decompressed_frame.data,
			cam->decompressed_frame.count)) {
		DBG("copy_to_user failed\n");
		mutex_unlock(&cam->busy_lock);
		return -EFAULT;
	}

	mutex_unlock(&cam->busy_lock);
	return cam->decompressed_frame.count;
}

static int cpia_do_ioctl(struct inode *inode, struct file *file,
			 unsigned int ioctlnr, void *arg)
{
	struct video_device *dev = file->private_data;
	struct cam_data *cam = dev->priv;
	int retval = 0;

	if (!cam || !cam->ops)
		return -ENODEV;

	/* make this _really_ smp-safe */
	if (mutex_lock_interruptible(&cam->busy_lock))
		return -EINTR;

	//DBG("cpia_ioctl: %u\n", ioctlnr);

	switch (ioctlnr) {
	/* query capabilities */
	case VIDIOCGCAP:
	{
		struct video_capability *b = arg;

		DBG("VIDIOCGCAP\n");
		strcpy(b->name, "CPiA Camera");
		b->type = VID_TYPE_CAPTURE | VID_TYPE_SUBCAPTURE;
		b->channels = 1;
		b->audios = 0;
		b->maxwidth = 352;	/* VIDEOSIZE_CIF */
		b->maxheight = 288;
		b->minwidth = 48;	/* VIDEOSIZE_48_48 */
		b->minheight = 48;
		break;
	}

	/* get/set video source - we are a camera and nothing else */
	case VIDIOCGCHAN:
	{
		struct video_channel *v = arg;

		DBG("VIDIOCGCHAN\n");
		if (v->channel != 0) {
			retval = -EINVAL;
			break;
		}

		v->channel = 0;
		strcpy(v->name, "Camera");
		v->tuners = 0;
		v->flags = 0;
		v->type = VIDEO_TYPE_CAMERA;
		v->norm = 0;
		break;
	}

	case VIDIOCSCHAN:
	{
		struct video_channel *v = arg;

		DBG("VIDIOCSCHAN\n");
		if (v->channel != 0)
			retval = -EINVAL;
		break;
	}

	/* image properties */
	case VIDIOCGPICT:
	{
		struct video_picture *pic = arg;
		DBG("VIDIOCGPICT\n");
		*pic = cam->vp;
		break;
	}

	case VIDIOCSPICT:
	{
		struct video_picture *vp = arg;

		DBG("VIDIOCSPICT\n");

		/* check validity */
		DBG("palette: %d\n", vp->palette);
		DBG("depth: %d\n", vp->depth);
		if (!valid_mode(vp->palette, vp->depth)) {
			retval = -EINVAL;
			break;
		}

		mutex_lock(&cam->param_lock);
		/* brightness, colour, contrast need no check 0-65535 */
		cam->vp = *vp;
		/* update cam->params.colourParams */
		cam->params.colourParams.brightness = vp->brightness*100/65535;
		cam->params.colourParams.contrast = vp->contrast*100/65535;
		cam->params.colourParams.saturation = vp->colour*100/65535;
		/* contrast is in steps of 8, so round */
		cam->params.colourParams.contrast =
			((cam->params.colourParams.contrast + 3) / 8) * 8;
		if (cam->params.version.firmwareVersion == 1 &&
		    cam->params.version.firmwareRevision == 2 &&
		    cam->params.colourParams.contrast > 80) {
			/* 1-02 firmware limits contrast to 80 */
			cam->params.colourParams.contrast = 80;
		}

		/* Adjust flicker control if necessary */
		if(cam->params.flickerControl.allowableOverExposure < 0)
			cam->params.flickerControl.allowableOverExposure =
				-find_over_exposure(cam->params.colourParams.brightness);
		if(cam->params.flickerControl.flickerMode != 0)
			cam->cmd_queue |= COMMAND_SETFLICKERCTRL;


		/* queue command to update camera */
		cam->cmd_queue |= COMMAND_SETCOLOURPARAMS;
		mutex_unlock(&cam->param_lock);
		DBG("VIDIOCSPICT: %d / %d // %d / %d / %d / %d\n",
		    vp->depth, vp->palette, vp->brightness, vp->hue, vp->colour,
		    vp->contrast);
		break;
	}

	/* get/set capture window */
	case VIDIOCGWIN:
	{
		struct video_window *vw = arg;
		DBG("VIDIOCGWIN\n");

		*vw = cam->vw;
		break;
	}

	case VIDIOCSWIN:
	{
		/* copy_from_user, check validity, copy to internal structure */
		struct video_window *vw = arg;
		DBG("VIDIOCSWIN\n");

		if (vw->clipcount != 0) {    /* clipping not supported */
			retval = -EINVAL;
			break;
		}
		if (vw->clips != NULL) {     /* clipping not supported */
			retval = -EINVAL;
			break;
		}

		/* we set the video window to something smaller or equal to what
		* is requested by the user???
		*/
		mutex_lock(&cam->param_lock);
		if (vw->width != cam->vw.width || vw->height != cam->vw.height) {
			int video_size = match_videosize(vw->width, vw->height);

			if (video_size < 0) {
				retval = -EINVAL;
				mutex_unlock(&cam->param_lock);
				break;
			}
			cam->video_size = video_size;

			/* video size is changing, reset the subcapture area */
			memset(&cam->vc, 0, sizeof(cam->vc));

			set_vw_size(cam);
			DBG("%d / %d\n", cam->vw.width, cam->vw.height);
			cam->cmd_queue |= COMMAND_SETFORMAT;
		}

		mutex_unlock(&cam->param_lock);

		/* setformat ignored by camera during streaming,
		 * so stop/dispatch/start */
		if (cam->cmd_queue & COMMAND_SETFORMAT) {
			DBG("\n");
			dispatch_commands(cam);
		}
		DBG("%d/%d:%d\n", cam->video_size,
		    cam->vw.width, cam->vw.height);
		break;
	}

	/* mmap interface */
	case VIDIOCGMBUF:
	{
		struct video_mbuf *vm = arg;
		int i;

		DBG("VIDIOCGMBUF\n");
		memset(vm, 0, sizeof(*vm));
		vm->size = CPIA_MAX_FRAME_SIZE*FRAME_NUM;
		vm->frames = FRAME_NUM;
		for (i = 0; i < FRAME_NUM; i++)
			vm->offsets[i] = CPIA_MAX_FRAME_SIZE * i;
		break;
	}

	case VIDIOCMCAPTURE:
	{
		struct video_mmap *vm = arg;
		int video_size;

		DBG("VIDIOCMCAPTURE: %d / %d / %dx%d\n", vm->format, vm->frame,
		    vm->width, vm->height);
		if (vm->frame<0||vm->frame>=FRAME_NUM) {
			retval = -EINVAL;
			break;
		}

		/* set video format */
		cam->vp.palette = vm->format;
		switch(vm->format) {
		case VIDEO_PALETTE_GREY:
			cam->vp.depth=8;
			break;
		case VIDEO_PALETTE_RGB555:
		case VIDEO_PALETTE_RGB565:
		case VIDEO_PALETTE_YUV422:
		case VIDEO_PALETTE_YUYV:
		case VIDEO_PALETTE_UYVY:
			cam->vp.depth = 16;
			break;
		case VIDEO_PALETTE_RGB24:
			cam->vp.depth = 24;
			break;
		case VIDEO_PALETTE_RGB32:
			cam->vp.depth = 32;
			break;
		default:
			retval = -EINVAL;
			break;
		}
		if (retval)
			break;

		/* set video size */
		video_size = match_videosize(vm->width, vm->height);
		if (video_size < 0) {
			retval = -EINVAL;
			break;
		}
		if (video_size != cam->video_size) {
			cam->video_size = video_size;

			/* video size is changing, reset the subcapture area */
			memset(&cam->vc, 0, sizeof(cam->vc));

			set_vw_size(cam);
			cam->cmd_queue |= COMMAND_SETFORMAT;
			dispatch_commands(cam);
		}
		/* according to v4l-spec we must start streaming here */
		cam->mmap_kludge = 1;
		retval = capture_frame(cam, vm);

		break;
	}

	case VIDIOCSYNC:
	{
		int *frame = arg;

		//DBG("VIDIOCSYNC: %d\n", *frame);

		if (*frame<0 || *frame >= FRAME_NUM) {
			retval = -EINVAL;
			break;
		}

		switch (cam->frame[*frame].state) {
		case FRAME_UNUSED:
		case FRAME_READY:
		case FRAME_GRABBING:
			DBG("sync to unused frame %d\n", *frame);
			retval = -EINVAL;
			break;

		case FRAME_DONE:
			cam->frame[*frame].state = FRAME_UNUSED;
			//DBG("VIDIOCSYNC: %d synced\n", *frame);
			break;
		}
		if (retval == -EINTR) {
			/* FIXME - xawtv does not handle this nice */
			retval = 0;
		}
		break;
	}

	case VIDIOCGCAPTURE:
	{
		struct video_capture *vc = arg;

		DBG("VIDIOCGCAPTURE\n");

		*vc = cam->vc;

		break;
	}

	case VIDIOCSCAPTURE:
	{
		struct video_capture *vc = arg;

		DBG("VIDIOCSCAPTURE\n");

		if (vc->decimation != 0) {    /* How should this be used? */
			retval = -EINVAL;
			break;
		}
		if (vc->flags != 0) {     /* Even/odd grab not supported */
			retval = -EINVAL;
			break;
		}

		/* Clip to the resolution we can set for the ROI
		   (every 8 columns and 4 rows) */
		vc->x      = vc->x      & ~(__u32)7;
		vc->y      = vc->y      & ~(__u32)3;
		vc->width  = vc->width  & ~(__u32)7;
		vc->height = vc->height & ~(__u32)3;

		if(vc->width == 0 || vc->height == 0 ||
		   vc->x + vc->width  > cam->vw.width ||
		   vc->y + vc->height > cam->vw.height) {
			retval = -EINVAL;
			break;
		}

		DBG("%d,%d/%dx%d\n", vc->x,vc->y,vc->width, vc->height);

		mutex_lock(&cam->param_lock);

		cam->vc.x      = vc->x;
		cam->vc.y      = vc->y;
		cam->vc.width  = vc->width;
		cam->vc.height = vc->height;

		set_vw_size(cam);
		cam->cmd_queue |= COMMAND_SETFORMAT;

		mutex_unlock(&cam->param_lock);

		/* setformat ignored by camera during streaming,
		 * so stop/dispatch/start */
		dispatch_commands(cam);
		break;
	}

	case VIDIOCGUNIT:
	{
		struct video_unit *vu = arg;

		DBG("VIDIOCGUNIT\n");

		vu->video    = cam->vdev.minor;
		vu->vbi      = VIDEO_NO_UNIT;
		vu->radio    = VIDEO_NO_UNIT;
		vu->audio    = VIDEO_NO_UNIT;
		vu->teletext = VIDEO_NO_UNIT;

		break;
	}


	/* pointless to implement overlay with this camera */
	case VIDIOCCAPTURE:
	case VIDIOCGFBUF:
	case VIDIOCSFBUF:
	case VIDIOCKEY:
	/* tuner interface - we have none */
	case VIDIOCGTUNER:
	case VIDIOCSTUNER:
	case VIDIOCGFREQ:
	case VIDIOCSFREQ:
	/* audio interface - we have none */
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
		retval = -EINVAL;
		break;
	default:
		retval = -ENOIOCTLCMD;
		break;
	}

	mutex_unlock(&cam->busy_lock);
	return retval;
}

static int cpia_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, cpia_do_ioctl);
}


/* FIXME */
static int cpia_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *dev = file->private_data;
	unsigned long start = vma->vm_start;
	unsigned long size  = vma->vm_end - vma->vm_start;
	unsigned long page, pos;
	struct cam_data *cam = dev->priv;
	int retval;

	if (!cam || !cam->ops)
		return -ENODEV;

	DBG("cpia_mmap: %ld\n", size);

	if (size > FRAME_NUM*CPIA_MAX_FRAME_SIZE)
		return -EINVAL;

	if (!cam || !cam->ops)
		return -ENODEV;

	/* make this _really_ smp-safe */
	if (mutex_lock_interruptible(&cam->busy_lock))
		return -EINTR;

	if (!cam->frame_buf) {	/* we do lazy allocation */
		if ((retval = allocate_frame_buf(cam))) {
			mutex_unlock(&cam->busy_lock);
			return retval;
		}
	}

	pos = (unsigned long)(cam->frame_buf);
	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
			mutex_unlock(&cam->busy_lock);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	DBG("cpia_mmap: %ld\n", size);
	mutex_unlock(&cam->busy_lock);

	return 0;
}

static struct file_operations cpia_fops = {
	.owner		= THIS_MODULE,
	.open		= cpia_open,
	.release       	= cpia_close,
	.read		= cpia_read,
	.mmap		= cpia_mmap,
	.ioctl          = cpia_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device cpia_template = {
	.owner		= THIS_MODULE,
	.name		= "CPiA Camera",
	.type		= VID_TYPE_CAPTURE,
	.hardware	= VID_HARDWARE_CPIA,
	.fops           = &cpia_fops,
};

/* initialise cam_data structure  */
static void reset_camera_struct(struct cam_data *cam)
{
	/* The following parameter values are the defaults from
	 * "Software Developer's Guide for CPiA Cameras".  Any changes
	 * to the defaults are noted in comments. */
	cam->params.colourParams.brightness = 50;
	cam->params.colourParams.contrast = 48;
	cam->params.colourParams.saturation = 50;
	cam->params.exposure.gainMode = 4;
	cam->params.exposure.expMode = 2;		/* AEC */
	cam->params.exposure.compMode = 1;
	cam->params.exposure.centreWeight = 1;
	cam->params.exposure.gain = 0;
	cam->params.exposure.fineExp = 0;
	cam->params.exposure.coarseExpLo = 185;
	cam->params.exposure.coarseExpHi = 0;
	cam->params.exposure.redComp = COMP_RED;
	cam->params.exposure.green1Comp = COMP_GREEN1;
	cam->params.exposure.green2Comp = COMP_GREEN2;
	cam->params.exposure.blueComp = COMP_BLUE;
	cam->params.colourBalance.balanceMode = 2;	/* ACB */
	cam->params.colourBalance.redGain = 32;
	cam->params.colourBalance.greenGain = 6;
	cam->params.colourBalance.blueGain = 92;
	cam->params.apcor.gain1 = 0x18;
	cam->params.apcor.gain2 = 0x16;
	cam->params.apcor.gain4 = 0x24;
	cam->params.apcor.gain8 = 0x34;
	cam->params.flickerControl.flickerMode = 0;
	cam->params.flickerControl.disabled = 1;

	cam->params.flickerControl.coarseJump =
		flicker_jumps[cam->mainsFreq]
			     [cam->params.sensorFps.baserate]
			     [cam->params.sensorFps.divisor];
	cam->params.flickerControl.allowableOverExposure =
		-find_over_exposure(cam->params.colourParams.brightness);
	cam->params.vlOffset.gain1 = 20;
	cam->params.vlOffset.gain2 = 24;
	cam->params.vlOffset.gain4 = 26;
	cam->params.vlOffset.gain8 = 26;
	cam->params.compressionParams.hysteresis = 3;
	cam->params.compressionParams.threshMax = 11;
	cam->params.compressionParams.smallStep = 1;
	cam->params.compressionParams.largeStep = 3;
	cam->params.compressionParams.decimationHysteresis = 2;
	cam->params.compressionParams.frDiffStepThresh = 5;
	cam->params.compressionParams.qDiffStepThresh = 3;
	cam->params.compressionParams.decimationThreshMod = 2;
	/* End of default values from Software Developer's Guide */

	cam->transfer_rate = 0;
	cam->exposure_status = EXPOSURE_NORMAL;

	/* Set Sensor FPS to 15fps. This seems better than 30fps
	 * for indoor lighting. */
	cam->params.sensorFps.divisor = 1;
	cam->params.sensorFps.baserate = 1;

	cam->params.yuvThreshold.yThreshold = 6; /* From windows driver */
	cam->params.yuvThreshold.uvThreshold = 6; /* From windows driver */

	cam->params.format.subSample = SUBSAMPLE_422;
	cam->params.format.yuvOrder = YUVORDER_YUYV;

	cam->params.compression.mode = CPIA_COMPRESSION_AUTO;
	cam->params.compressionTarget.frTargeting =
		CPIA_COMPRESSION_TARGET_QUALITY;
	cam->params.compressionTarget.targetFR = 15; /* From windows driver */
	cam->params.compressionTarget.targetQ = 5; /* From windows driver */

	cam->params.qx3.qx3_detected = 0;
	cam->params.qx3.toplight = 0;
	cam->params.qx3.bottomlight = 0;
	cam->params.qx3.button = 0;
	cam->params.qx3.cradled = 0;

	cam->video_size = VIDEOSIZE_CIF;

	cam->vp.colour = 32768;      /* 50% */
	cam->vp.hue = 32768;         /* 50% */
	cam->vp.brightness = 32768;  /* 50% */
	cam->vp.contrast = 32768;    /* 50% */
	cam->vp.whiteness = 0;       /* not used -> grayscale only */
	cam->vp.depth = 24;          /* to be set by user */
	cam->vp.palette = VIDEO_PALETTE_RGB24; /* to be set by user */

	cam->vc.x = 0;
	cam->vc.y = 0;
	cam->vc.width = 0;
	cam->vc.height = 0;

	cam->vw.x = 0;
	cam->vw.y = 0;
	set_vw_size(cam);
	cam->vw.chromakey = 0;
	cam->vw.flags = 0;
	cam->vw.clipcount = 0;
	cam->vw.clips = NULL;

	cam->cmd_queue = COMMAND_NONE;
	cam->first_frame = 1;

	return;
}

/* initialize cam_data structure  */
static void init_camera_struct(struct cam_data *cam,
			       struct cpia_camera_ops *ops )
{
	int i;

	/* Default everything to 0 */
	memset(cam, 0, sizeof(struct cam_data));

	cam->ops = ops;
	mutex_init(&cam->param_lock);
	mutex_init(&cam->busy_lock);

	reset_camera_struct(cam);

	cam->proc_entry = NULL;

	memcpy(&cam->vdev, &cpia_template, sizeof(cpia_template));
	cam->vdev.priv = cam;

	cam->curframe = 0;
	for (i = 0; i < FRAME_NUM; i++) {
		cam->frame[i].width = 0;
		cam->frame[i].height = 0;
		cam->frame[i].state = FRAME_UNUSED;
		cam->frame[i].data = NULL;
	}
	cam->decompressed_frame.width = 0;
	cam->decompressed_frame.height = 0;
	cam->decompressed_frame.state = FRAME_UNUSED;
	cam->decompressed_frame.data = NULL;
}

struct cam_data *cpia_register_camera(struct cpia_camera_ops *ops, void *lowlevel)
{
	struct cam_data *camera;

	if ((camera = kmalloc(sizeof(struct cam_data), GFP_KERNEL)) == NULL)
		return NULL;


	init_camera_struct( camera, ops );
	camera->lowlevel_data = lowlevel;

	/* register v4l device */
	if (video_register_device(&camera->vdev, VFL_TYPE_GRABBER, video_nr) == -1) {
		kfree(camera);
		printk(KERN_DEBUG "video_register_device failed\n");
		return NULL;
	}

	/* get version information from camera: open/reset/close */

	/* open cpia */
	if (camera->ops->open(camera->lowlevel_data))
		return camera;

	/* reset the camera */
	if (reset_camera(camera) != 0) {
		camera->ops->close(camera->lowlevel_data);
		return camera;
	}

	/* close cpia */
	camera->ops->close(camera->lowlevel_data);

#ifdef CONFIG_PROC_FS
	create_proc_cpia_cam(camera);
#endif

	printk(KERN_INFO "  CPiA Version: %d.%02d (%d.%d)\n",
	       camera->params.version.firmwareVersion,
	       camera->params.version.firmwareRevision,
	       camera->params.version.vcVersion,
	       camera->params.version.vcRevision);
	printk(KERN_INFO "  CPiA PnP-ID: %04x:%04x:%04x\n",
	       camera->params.pnpID.vendor,
	       camera->params.pnpID.product,
	       camera->params.pnpID.deviceRevision);
	printk(KERN_INFO "  VP-Version: %d.%d %04x\n",
	       camera->params.vpVersion.vpVersion,
	       camera->params.vpVersion.vpRevision,
	       camera->params.vpVersion.cameraHeadID);

	return camera;
}

void cpia_unregister_camera(struct cam_data *cam)
{
	DBG("unregistering video\n");
	video_unregister_device(&cam->vdev);
	if (cam->open_count) {
		put_cam(cam->ops);
		DBG("camera open -- setting ops to NULL\n");
		cam->ops = NULL;
	}

#ifdef CONFIG_PROC_FS
	DBG("destroying /proc/cpia/video%d\n", cam->vdev.minor);
	destroy_proc_cpia_cam(cam);
#endif
	if (!cam->open_count) {
		DBG("freeing camera\n");
		kfree(cam);
	}
}

static int __init cpia_init(void)
{
	printk(KERN_INFO "%s v%d.%d.%d\n", ABOUT,
	       CPIA_MAJ_VER, CPIA_MIN_VER, CPIA_PATCH_VER);

	printk(KERN_WARNING "Since in-kernel colorspace conversion is not "
	       "allowed, it is disabled by default now. Users should fix the "
	       "applications in case they don't work without conversion "
	       "reenabled by setting the 'colorspace_conv' module "
	       "parameter to 1\n");

#ifdef CONFIG_PROC_FS
	proc_cpia_create();
#endif

	return 0;
}

static void __exit cpia_exit(void)
{
#ifdef CONFIG_PROC_FS
	proc_cpia_destroy();
#endif
}

module_init(cpia_init);
module_exit(cpia_exit);

/* Exported symbols for modules. */

EXPORT_SYMBOL(cpia_register_camera);
EXPORT_SYMBOL(cpia_unregister_camera);
