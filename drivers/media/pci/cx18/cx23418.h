/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 header containing common defines.
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 */

#ifndef CX23418_H
#define CX23418_H

#include <media/drv-intf/cx2341x.h>

#define MGR_CMD_MASK				0x40000000
/* The MSB of the command code indicates that this is the completion of a
   command */
#define MGR_CMD_MASK_ACK			(MGR_CMD_MASK | 0x80000000)

/* Description: This command creates a new instance of a certain task
   IN[0]  - Task ID. This is one of the XPU_CMD_MASK_YYY where XPU is
	    the processor on which the task YYY will be created
   OUT[0] - Task handle. This handle is passed along with commands to
	    dispatch to the right instance of the task
   ReturnCode - One of the ERR_SYS_... */
#define CX18_CREATE_TASK			(MGR_CMD_MASK | 0x0001)

/* Description: This command destroys an instance of a task
   IN[0] - Task handle. Hanlde of the task to destroy
   ReturnCode - One of the ERR_SYS_... */
#define CX18_DESTROY_TASK			(MGR_CMD_MASK | 0x0002)

/* All commands for CPU have the following mask set */
#define CPU_CMD_MASK				0x20000000
#define CPU_CMD_MASK_DEBUG			(CPU_CMD_MASK | 0x00000000)
#define CPU_CMD_MASK_ACK			(CPU_CMD_MASK | 0x80000000)
#define CPU_CMD_MASK_CAPTURE			(CPU_CMD_MASK | 0x00020000)
#define CPU_CMD_MASK_TS				(CPU_CMD_MASK | 0x00040000)

#define EPU_CMD_MASK				0x02000000
#define EPU_CMD_MASK_DEBUG			(EPU_CMD_MASK | 0x000000)
#define EPU_CMD_MASK_DE				(EPU_CMD_MASK | 0x040000)

#define APU_CMD_MASK				0x10000000
#define APU_CMD_MASK_ACK			(APU_CMD_MASK | 0x80000000)

#define CX18_APU_ENCODING_METHOD_MPEG		(0 << 28)
#define CX18_APU_ENCODING_METHOD_AC3		(1 << 28)

/* Description: Command APU to start audio
   IN[0] - audio parameters (same as CX18_CPU_SET_AUDIO_PARAMETERS?)
   IN[1] - caller buffer address, or 0
   ReturnCode - ??? */
#define CX18_APU_START				(APU_CMD_MASK | 0x01)

/* Description: Command APU to stop audio
   IN[0] - encoding method to stop
   ReturnCode - ??? */
#define CX18_APU_STOP				(APU_CMD_MASK | 0x02)

/* Description: Command APU to reset the AI
   ReturnCode - ??? */
#define CX18_APU_RESETAI			(APU_CMD_MASK | 0x05)

/* Description: This command indicates that a Memory Descriptor List has been
   filled with the requested channel type
   IN[0] - Task handle. Handle of the task
   IN[1] - Offset of the MDL_ACK from the beginning of the local DDR.
   IN[2] - Number of CNXT_MDL_ACK structures in the array pointed to by IN[1]
   ReturnCode - One of the ERR_DE_... */
#define CX18_EPU_DMA_DONE			(EPU_CMD_MASK_DE | 0x0001)

/* Something interesting happened
   IN[0] - A value to log
   IN[1] - An offset of a string in the MiniMe memory;
	   0/zero/NULL means "I have nothing to say" */
#define CX18_EPU_DEBUG				(EPU_CMD_MASK_DEBUG | 0x0003)

/* Reads memory/registers (32-bit)
   IN[0] - Address
   OUT[1] - Value */
#define CX18_CPU_DEBUG_PEEK32			(CPU_CMD_MASK_DEBUG | 0x0003)

/* Description: This command starts streaming with the set channel type
   IN[0] - Task handle. Handle of the task to start
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_CAPTURE_START			(CPU_CMD_MASK_CAPTURE | 0x0002)

/* Description: This command stops streaming with the set channel type
   IN[0] - Task handle. Handle of the task to stop
   IN[1] - 0 = stop at end of GOP, 1 = stop at end of frame (MPEG only)
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_CAPTURE_STOP			(CPU_CMD_MASK_CAPTURE | 0x0003)

/* Description: This command pauses streaming with the set channel type
   IN[0] - Task handle. Handle of the task to pause
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_CAPTURE_PAUSE			(CPU_CMD_MASK_CAPTURE | 0x0007)

/* Description: This command resumes streaming with the set channel type
   IN[0] - Task handle. Handle of the task to resume
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_CAPTURE_RESUME			(CPU_CMD_MASK_CAPTURE | 0x0008)

#define CAPTURE_CHANNEL_TYPE_NONE		0
#define CAPTURE_CHANNEL_TYPE_MPEG		1
#define CAPTURE_CHANNEL_TYPE_INDEX		2
#define CAPTURE_CHANNEL_TYPE_YUV		3
#define CAPTURE_CHANNEL_TYPE_PCM		4
#define CAPTURE_CHANNEL_TYPE_VBI		5
#define CAPTURE_CHANNEL_TYPE_SLICED_VBI		6
#define CAPTURE_CHANNEL_TYPE_TS			7
#define CAPTURE_CHANNEL_TYPE_MAX		15

/* Description: This command sets the channel type. This can only be done
   when stopped.
   IN[0] - Task handle. Handle of the task to start
   IN[1] - Channel Type. See Below.
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_CHANNEL_TYPE		(CPU_CMD_MASK_CAPTURE + 1)

/* Description: Set stream output type
   IN[0] - task handle. Handle of the task to start
   IN[1] - type
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_STREAM_OUTPUT_TYPE		(CPU_CMD_MASK_CAPTURE | 0x0012)

/* Description: Set video input resolution and frame rate
   IN[0] - task handle
   IN[1] - reserved
   IN[2] - reserved
   IN[3] - reserved
   IN[4] - reserved
   IN[5] - frame rate, 0 - 29.97f/s, 1 - 25f/s
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_VIDEO_IN			(CPU_CMD_MASK_CAPTURE | 0x0004)

/* Description: Set video frame rate
   IN[0] - task handle. Handle of the task to start
   IN[1] - video bit rate mode
   IN[2] - video average rate
   IN[3] - video peak rate
   IN[4] - system mux rate
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_VIDEO_RATE			(CPU_CMD_MASK_CAPTURE | 0x0005)

/* Description: Set video output resolution
   IN[0] - task handle
   IN[1] - horizontal size
   IN[2] - vertical size
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_VIDEO_RESOLUTION		(CPU_CMD_MASK_CAPTURE | 0x0006)

/* Description: This command set filter parameters
   IN[0] - Task handle. Handle of the task
   IN[1] - type, 0 - temporal, 1 - spatial, 2 - median
   IN[2] - mode,  temporal/spatial: 0 - disable, 1 - static, 2 - dynamic
			median:	0 = disable, 1 = horizontal, 2 = vertical,
				3 = horizontal/vertical, 4 = diagonal
   IN[3] - strength, temporal 0 - 31, spatial 0 - 15
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_FILTER_PARAM		(CPU_CMD_MASK_CAPTURE | 0x0009)

/* Description: This command set spatial filter type
   IN[0] - Task handle.
   IN[1] - luma type: 0 = disable, 1 = 1D horizontal only, 2 = 1D vertical only,
		      3 = 2D H/V separable, 4 = 2D symmetric non-separable
   IN[2] - chroma type: 0 - disable, 1 = 1D horizontal
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_SPATIAL_FILTER_TYPE	(CPU_CMD_MASK_CAPTURE | 0x000C)

/* Description: This command set coring levels for median filter
   IN[0] - Task handle.
   IN[1] - luma_high
   IN[2] - luma_low
   IN[3] - chroma_high
   IN[4] - chroma_low
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_MEDIAN_CORING		(CPU_CMD_MASK_CAPTURE | 0x000E)

/* Description: This command set the picture type mask for index file
   IN[0] - Task handle (ignored by firmware)
   IN[1] -	0 = disable index file output
			1 = output I picture
			2 = P picture
			4 = B picture
			other = illegal */
#define CX18_CPU_SET_INDEXTABLE			(CPU_CMD_MASK_CAPTURE | 0x0010)

/* Description: Set audio parameters
   IN[0] - task handle. Handle of the task to start
   IN[1] - audio parameter
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_AUDIO_PARAMETERS		(CPU_CMD_MASK_CAPTURE | 0x0011)

/* Description: Set video mute
   IN[0] - task handle. Handle of the task to start
   IN[1] - bit31-24: muteYvalue
	   bit23-16: muteUvalue
	   bit15-8:  muteVvalue
	   bit0:     1:mute, 0: unmute
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_VIDEO_MUTE			(CPU_CMD_MASK_CAPTURE | 0x0013)

/* Description: Set audio mute
   IN[0] - task handle. Handle of the task to start
   IN[1] - mute/unmute
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_AUDIO_MUTE			(CPU_CMD_MASK_CAPTURE | 0x0014)

/* Description: Set stream output type
   IN[0] - task handle. Handle of the task to start
   IN[1] - subType
	    SET_INITIAL_SCR			1
	    SET_QUALITY_MODE            2
	    SET_VIM_PROTECT_MODE        3
	    SET_PTS_CORRECTION          4
	    SET_USB_FLUSH_MODE          5
	    SET_MERAQPAR_ENABLE         6
	    SET_NAV_PACK_INSERTION      7
	    SET_SCENE_CHANGE_ENABLE     8
   IN[2] - parameter 1
   IN[3] - parameter 2
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_MISC_PARAMETERS		(CPU_CMD_MASK_CAPTURE | 0x0015)

/* Description: Set raw VBI parameters
   IN[0] - Task handle
   IN[1] - No. of input lines per field:
				bit[15:0]: field 1,
				bit[31:16]: field 2
   IN[2] - No. of input bytes per line
   IN[3] - No. of output frames per transfer
   IN[4] - start code
   IN[5] - stop code
   ReturnCode */
#define CX18_CPU_SET_RAW_VBI_PARAM		(CPU_CMD_MASK_CAPTURE | 0x0016)

/* Description: Set capture line No.
   IN[0] - task handle. Handle of the task to start
   IN[1] - height1
   IN[2] - height2
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_CAPTURE_LINE_NO		(CPU_CMD_MASK_CAPTURE | 0x0017)

/* Description: Set copyright
   IN[0] - task handle. Handle of the task to start
   IN[1] - copyright
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_COPYRIGHT			(CPU_CMD_MASK_CAPTURE | 0x0018)

/* Description: Set audio PID
   IN[0] - task handle. Handle of the task to start
   IN[1] - PID
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_AUDIO_PID			(CPU_CMD_MASK_CAPTURE | 0x0019)

/* Description: Set video PID
   IN[0] - task handle. Handle of the task to start
   IN[1] - PID
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_VIDEO_PID			(CPU_CMD_MASK_CAPTURE | 0x001A)

/* Description: Set Vertical Crop Line
   IN[0] - task handle. Handle of the task to start
   IN[1] - Line
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_VER_CROP_LINE		(CPU_CMD_MASK_CAPTURE | 0x001B)

/* Description: Set COP structure
   IN[0] - task handle. Handle of the task to start
   IN[1] - M
   IN[2] - N
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_GOP_STRUCTURE		(CPU_CMD_MASK_CAPTURE | 0x001C)

/* Description: Set Scene Change Detection
   IN[0] - task handle. Handle of the task to start
   IN[1] - scene change
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_SCENE_CHANGE_DETECTION	(CPU_CMD_MASK_CAPTURE | 0x001D)

/* Description: Set Aspect Ratio
   IN[0] - task handle. Handle of the task to start
   IN[1] - AspectRatio
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_ASPECT_RATIO		(CPU_CMD_MASK_CAPTURE | 0x001E)

/* Description: Set Skip Input Frame
   IN[0] - task handle. Handle of the task to start
   IN[1] - skip input frames
   ReturnCode - One of the ERR_CAPTURE_... */
#define CX18_CPU_SET_SKIP_INPUT_FRAME		(CPU_CMD_MASK_CAPTURE | 0x001F)

/* Description: Set sliced VBI parameters -
   Note This API will only apply to MPEG and Sliced VBI Channels
   IN[0] - Task handle
   IN[1] - output type, 0 - CC, 1 - Moji, 2 - Teletext
   IN[2] - start / stop line
			bit[15:0] start line number
			bit[31:16] stop line number
   IN[3] - number of output frames per interrupt
   IN[4] - VBI insertion mode
			bit 0:	output user data, 1 - enable
			bit 1:	output private stream, 1 - enable
			bit 2:	mux option, 0 - in GOP, 1 - in picture
			bit[7:0]	private stream ID
   IN[5] - insertion period while mux option is in picture
   ReturnCode - VBI data offset */
#define CX18_CPU_SET_SLICED_VBI_PARAM		(CPU_CMD_MASK_CAPTURE | 0x0020)

/* Description: Set the user data place holder
   IN[0] - type of data (0 for user)
   IN[1] - Stuffing period
   IN[2] - ID data size in word (less than 10)
   IN[3] - Pointer to ID buffer */
#define CX18_CPU_SET_USERDATA_PLACE_HOLDER	(CPU_CMD_MASK_CAPTURE | 0x0021)


/* Description:
   In[0] Task Handle
   return parameter:
   Out[0]  Reserved
   Out[1]  Video PTS bit[32:2] of last output video frame.
   Out[2]  Video PTS bit[ 1:0] of last output video frame.
   Out[3]  Hardware Video PTS counter bit[31:0],
	     these bits get incremented on every 90kHz clock tick.
   Out[4]  Hardware Video PTS counter bit32,
	     these bits get incremented on every 90kHz clock tick.
   ReturnCode */
#define CX18_CPU_GET_ENC_PTS			(CPU_CMD_MASK_CAPTURE | 0x0022)

/* Description: Set VFC parameters
   IN[0] - task handle
   IN[1] - VFC enable flag, 1 - enable, 0 - disable
*/
#define CX18_CPU_SET_VFC_PARAM                  (CPU_CMD_MASK_CAPTURE | 0x0023)

/* Below is the list of commands related to the data exchange */
#define CPU_CMD_MASK_DE				(CPU_CMD_MASK | 0x040000)

/* Description: This command provides the physical base address of the local
   DDR as viewed by EPU
   IN[0] - Physical offset where EPU has the local DDR mapped
   ReturnCode - One of the ERR_DE_... */
#define CPU_CMD_DE_SetBase			(CPU_CMD_MASK_DE | 0x0001)

/* Description: This command provides the offsets in the device memory where
   the 2 cx18_mdl_ack blocks reside
   IN[0] - Task handle. Handle of the task to start
   IN[1] - Offset of the first cx18_mdl_ack from the beginning of the
	   local DDR.
   IN[2] - Offset of the second cx18_mdl_ack from the beginning of the
	   local DDR.
   ReturnCode - One of the ERR_DE_... */
#define CX18_CPU_DE_SET_MDL_ACK			(CPU_CMD_MASK_DE | 0x0002)

/* Description: This command provides the offset to a Memory Descriptor List
   IN[0] - Task handle. Handle of the task to start
   IN[1] - Offset of the MDL from the beginning of the local DDR.
   IN[2] - Number of cx18_mdl_ent structures in the array pointed to by IN[1]
   IN[3] - Buffer ID
   IN[4] - Total buffer length
   ReturnCode - One of the ERR_DE_... */
#define CX18_CPU_DE_SET_MDL			(CPU_CMD_MASK_DE | 0x0005)

/* Description: This command requests return of all current Memory
   Descriptor Lists to the driver
   IN[0] - Task handle. Handle of the task to start
   ReturnCode - One of the ERR_DE_... */
#define CX18_CPU_DE_RELEASE_MDL			(CPU_CMD_MASK_DE | 0x0006)

/* Description: This command signals the cpu that the dat buffer has been
   consumed and ready for re-use.
   IN[0] - Task handle. Handle of the task
   IN[1] - Offset of the data block from the beginning of the local DDR.
   IN[2] - Number of bytes in the data block
   ReturnCode - One of the ERR_DE_... */
/* #define CX18_CPU_DE_RELEASE_BUFFER           (CPU_CMD_MASK_DE | 0x0007) */

/* No Error / Success */
#define CNXT_OK                 0x000000

/* Received unknown command */
#define CXERR_UNK_CMD           0x000001

/* First parameter in the command is invalid */
#define CXERR_INVALID_PARAM1    0x000002

/* Second parameter in the command is invalid */
#define CXERR_INVALID_PARAM2    0x000003

/* Device interface is not open/found */
#define CXERR_DEV_NOT_FOUND     0x000004

/* Requested function is not implemented/available */
#define CXERR_NOTSUPPORTED      0x000005

/* Invalid pointer is provided */
#define CXERR_BADPTR            0x000006

/* Unable to allocate memory */
#define CXERR_NOMEM             0x000007

/* Object/Link not found */
#define CXERR_LINK              0x000008

/* Device busy, command cannot be executed */
#define CXERR_BUSY              0x000009

/* File/device/handle is not open. */
#define CXERR_NOT_OPEN          0x00000A

/* Value is out of range */
#define CXERR_OUTOFRANGE        0x00000B

/* Buffer overflow */
#define CXERR_OVERFLOW          0x00000C

/* Version mismatch */
#define CXERR_BADVER            0x00000D

/* Operation timed out */
#define CXERR_TIMEOUT           0x00000E

/* Operation aborted */
#define CXERR_ABORT             0x00000F

/* Specified I2C device not found for read/write */
#define CXERR_I2CDEV_NOTFOUND   0x000010

/* Error in I2C data xfer (but I2C device is present) */
#define CXERR_I2CDEV_XFERERR    0x000011

/* Channel changing component not ready */
#define CXERR_CHANNELNOTREADY   0x000012

/* PPU (Presensation/Decoder) mail box is corrupted */
#define CXERR_PPU_MB_CORRUPT    0x000013

/* CPU (Capture/Encoder) mail box is corrupted */
#define CXERR_CPU_MB_CORRUPT    0x000014

/* APU (Audio) mail box is corrupted */
#define CXERR_APU_MB_CORRUPT    0x000015

/* Unable to open file for reading */
#define CXERR_FILE_OPEN_READ    0x000016

/* Unable to open file for writing */
#define CXERR_FILE_OPEN_WRITE   0x000017

/* Unable to find the I2C section specified */
#define CXERR_I2C_BADSECTION    0x000018

/* Error in I2C data xfer (but I2C device is present) */
#define CXERR_I2CDEV_DATALOW    0x000019

/* Error in I2C data xfer (but I2C device is present) */
#define CXERR_I2CDEV_CLOCKLOW   0x00001A

/* No Interrupt received from HW (for I2C access) */
#define CXERR_NO_HW_I2C_INTR    0x00001B

/* RPU is not ready to accept commands! */
#define CXERR_RPU_NOT_READY     0x00001C

/* RPU is not ready to accept commands! */
#define CXERR_RPU_NO_ACK        0x00001D

/* The are no buffers ready. Try again soon! */
#define CXERR_NODATA_AGAIN      0x00001E

/* The stream is stopping. Function not allowed now! */
#define CXERR_STOPPING_STATUS   0x00001F

/* Trying to access hardware when the power is turned OFF */
#define CXERR_DEVPOWER_OFF      0x000020

#endif /* CX23418_H */
