#ifndef cpia_h
#define cpia_h

/*
 * CPiA Parallel Port Video4Linux driver
 *
 * Supports CPiA based parallel port Video Camera's.
 *
 * (C) Copyright 1999 Bas Huisman,
 *                    Peter Pregler,
 *                    Scott J. Bertin,
 *                    VLSI Vision Ltd.
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

#define CPIA_MAJ_VER	1
#define CPIA_MIN_VER   2
#define CPIA_PATCH_VER	3

#define CPIA_PP_MAJ_VER       CPIA_MAJ_VER
#define CPIA_PP_MIN_VER       CPIA_MIN_VER
#define CPIA_PP_PATCH_VER     CPIA_PATCH_VER

#define CPIA_USB_MAJ_VER      CPIA_MAJ_VER
#define CPIA_USB_MIN_VER      CPIA_MIN_VER
#define CPIA_USB_PATCH_VER    CPIA_PATCH_VER

#define CPIA_MAX_FRAME_SIZE_UNALIGNED	(352 * 288 * 4)   /* CIF at RGB32 */
#define CPIA_MAX_FRAME_SIZE	((CPIA_MAX_FRAME_SIZE_UNALIGNED + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)) /* align above to PAGE_SIZE */

#ifdef __KERNEL__

#include <asm/uaccess.h>
#include <linux/videodev.h>
#include <linux/list.h>
#include <linux/smp_lock.h>
#include <linux/mutex.h>

struct cpia_camera_ops
{
	/* open sets privdata to point to structure for this camera.
         * Returns negative value on error, otherwise 0.
	 */
	int (*open)(void *privdata);
	
	/* Registers callback function cb to be called with cbdata
	 * when an image is ready.  If cb is NULL, only single image grabs
	 * should be used.  cb should immediately call streamRead to read
	 * the data or data may be lost. Returns negative value on error,
	 * otherwise 0.
	 */
	int (*registerCallback)(void *privdata, void (*cb)(void *cbdata),
	                        void *cbdata);
	
	/* transferCmd sends commands to the camera.  command MUST point to
	 * an  8 byte buffer in kernel space. data can be NULL if no extra
	 * data is needed.  The size of the data is given by the last 2
	 * bytes of command.  data must also point to memory in kernel space.
	 * Returns negative value on error, otherwise 0.
	 */
	int (*transferCmd)(void *privdata, u8 *command, u8 *data);

	/* streamStart initiates stream capture mode.
	 * Returns negative value on error, otherwise 0.
	 */
	int (*streamStart)(void *privdata);
	
	/* streamStop terminates stream capture mode.
	 * Returns negative value on error, otherwise 0.
	 */
	int (*streamStop)(void *privdata);
        
	/* streamRead reads a frame from the camera.  buffer points to a
         * buffer large enough to hold a complete frame in kernel space.
         * noblock indicates if this should be a non blocking read.
	 * Returns the number of bytes read, or negative value on error.
         */
	int (*streamRead)(void *privdata, u8 *buffer, int noblock);
	
	/* close disables the device until open() is called again.
	 * Returns negative value on error, otherwise 0.
	 */
	int (*close)(void *privdata);
	
	/* If wait_for_stream_ready is non-zero, wait until the streamState
	 * is STREAM_READY before calling streamRead.
	 */
	int wait_for_stream_ready;

	/* 
	 * Used to maintain lowlevel module usage counts
	 */
	struct module *owner;
};

struct cpia_frame {
	u8 *data;
	int count;
	int width;
	int height;
	volatile int state;
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
		int allowableOverExposure;
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

enum v4l_camstates {
	CPIA_V4L_IDLE = 0,
	CPIA_V4L_ERROR,
	CPIA_V4L_COMMAND,
	CPIA_V4L_GRABBING,
	CPIA_V4L_STREAMING,
	CPIA_V4L_STREAMING_PAUSED,
};

#define FRAME_NUM	2	/* double buffering for now */

struct cam_data {
	struct list_head cam_data_list;

        struct mutex busy_lock;     /* guard against SMP multithreading */
	struct cpia_camera_ops *ops;	/* lowlevel driver operations */
	void *lowlevel_data;		/* private data for lowlevel driver */
	u8 *raw_image;			/* buffer for raw image data */
	struct cpia_frame decompressed_frame;
                                        /* buffer to hold decompressed frame */
	int image_size;		        /* sizeof last decompressed image */
	int open_count;			/* # of process that have camera open */

				/* camera status */
	int fps;			/* actual fps reported by the camera */
	int transfer_rate;		/* transfer rate from camera in kB/s */
	u8 mainsFreq;			/* for flicker control */

				/* proc interface */
	struct mutex param_lock;	/* params lock for this camera */
	struct cam_params params;	/* camera settings */
	struct proc_dir_entry *proc_entry;	/* /proc/cpia/videoX */
	
					/* v4l */
	int video_size;			/* VIDEO_SIZE_ */
	volatile enum v4l_camstates camstate;	/* v4l layer status */
	struct video_device vdev;	/* v4l videodev */
	struct video_picture vp;	/* v4l camera settings */
	struct video_window vw;		/* v4l capture area */
	struct video_capture vc;       	/* v4l subcapture area */

				/* mmap interface */
	int curframe;			/* the current frame to grab into */
	u8 *frame_buf;			/* frame buffer data */
        struct cpia_frame frame[FRAME_NUM];
				/* FRAME_NUM-buffering, so we need a array */

	int first_frame;
	int mmap_kludge;		/* 'wrong' byte order for mmap */
	volatile u32 cmd_queue;		/* queued commands */
	int exposure_status;		/* EXPOSURE_* */
	int exposure_count;		/* number of frames at this status */
};

/* cpia_register_camera is called by low level driver for each camera.
 * A unique camera number is returned, or a negative value on error */
struct cam_data *cpia_register_camera(struct cpia_camera_ops *ops, void *lowlevel);

/* cpia_unregister_camera is called by low level driver when a camera
 * is removed.  This must not fail. */
void cpia_unregister_camera(struct cam_data *cam);

/* raw CIF + 64 byte header + (2 bytes line_length + EOL) per line + 4*EOI +
 * one byte 16bit DMA alignment
 */
#define CPIA_MAX_IMAGE_SIZE ((352*288*2)+64+(288*3)+5)

/* constant value's */
#define MAGIC_0		0x19
#define MAGIC_1		0x68
#define DATA_IN		0xC0
#define DATA_OUT	0x40
#define VIDEOSIZE_QCIF	0	/* 176x144 */
#define VIDEOSIZE_CIF	1	/* 352x288 */
#define VIDEOSIZE_SIF	2	/* 320x240 */
#define VIDEOSIZE_QSIF	3	/* 160x120 */
#define VIDEOSIZE_48_48		4 /* where no one has gone before, iconsize! */
#define VIDEOSIZE_64_48		5
#define VIDEOSIZE_128_96	6
#define VIDEOSIZE_160_120	VIDEOSIZE_QSIF
#define VIDEOSIZE_176_144	VIDEOSIZE_QCIF
#define VIDEOSIZE_192_144	7
#define VIDEOSIZE_224_168	8
#define VIDEOSIZE_256_192	9
#define VIDEOSIZE_288_216	10
#define VIDEOSIZE_320_240	VIDEOSIZE_SIF
#define VIDEOSIZE_352_288	VIDEOSIZE_CIF
#define VIDEOSIZE_88_72		11 /* quarter CIF */
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
#define CPIA_GRAB_CONTINUOUS	1

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

/* ErrorCode */
#define ERROR_FLICKER_BELOW_MIN_EXP     0x01 /*flicker exposure got below minimum exposure */
#define ALOG(fmt,args...) printk(fmt, ##args)
#define LOG(fmt,args...) ALOG(KERN_INFO __FILE__ ":%s(%d):" fmt, __FUNCTION__ , __LINE__ , ##args)

#ifdef _CPIA_DEBUG_
#define ADBG(fmt,args...) printk(fmt, jiffies, ##args)
#define DBG(fmt,args...) ADBG(KERN_DEBUG __FILE__" (%ld):%s(%d):" fmt, __FUNCTION__, __LINE__ , ##args)
#else
#define DBG(fmn,args...) do {} while(0)
#endif

#define DEB_BYTE(p)\
  DBG("%1d %1d %1d %1d %1d %1d %1d %1d \n",\
      (p)&0x80?1:0, (p)&0x40?1:0, (p)&0x20?1:0, (p)&0x10?1:0,\
        (p)&0x08?1:0, (p)&0x04?1:0, (p)&0x02?1:0, (p)&0x01?1:0);

#endif /* __KERNEL__ */

#endif /* cpia_h */
