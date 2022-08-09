/**********************************************************
 * Copyright 1998-2021 VMware, Inc.
 * SPDX-License-Identifier: GPL-2.0 OR MIT
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

/*
 * svga_reg.h --
 *
 *    Virtual hardware definitions for the VMware SVGA II device.
 */



#ifndef _SVGA_REG_H_
#define _SVGA_REG_H_

#include "vm_basic_types.h"

typedef enum {
	SVGA_REG_ENABLE_DISABLE = 0,
	SVGA_REG_ENABLE_ENABLE = (1 << 0),
	SVGA_REG_ENABLE_HIDE = (1 << 1),
} SvgaRegEnable;

typedef uint32 SVGAMobId;

#define SVGA_MAX_WIDTH 2560
#define SVGA_MAX_HEIGHT 1600

#define SVGA_MAX_BITS_PER_PIXEL 32
#define SVGA_MAX_DEPTH 24
#define SVGA_MAX_DISPLAYS 10
#define SVGA_MAX_SCREEN_SIZE 8192
#define SVGA_SCREEN_ROOT_LIMIT (SVGA_MAX_SCREEN_SIZE * SVGA_MAX_DISPLAYS)

#define SVGA_CURSOR_ON_HIDE 0x0
#define SVGA_CURSOR_ON_SHOW 0x1

#define SVGA_CURSOR_ON_REMOVE_FROM_FB 0x2

#define SVGA_CURSOR_ON_RESTORE_TO_FB 0x3

#define SVGA_FB_MAX_TRACEABLE_SIZE 0x1000000

#define SVGA_MAX_PSEUDOCOLOR_DEPTH 8
#define SVGA_MAX_PSEUDOCOLORS (1 << SVGA_MAX_PSEUDOCOLOR_DEPTH)
#define SVGA_NUM_PALETTE_REGS (3 * SVGA_MAX_PSEUDOCOLORS)

#define SVGA_MAGIC 0x900000UL
#define SVGA_MAKE_ID(ver) (SVGA_MAGIC << 8 | (ver))

#define SVGA_VERSION_3 3
#define SVGA_ID_3 SVGA_MAKE_ID(SVGA_VERSION_3)

#define SVGA_VERSION_2 2
#define SVGA_ID_2 SVGA_MAKE_ID(SVGA_VERSION_2)

#define SVGA_VERSION_1 1
#define SVGA_ID_1 SVGA_MAKE_ID(SVGA_VERSION_1)

#define SVGA_VERSION_0 0
#define SVGA_ID_0 SVGA_MAKE_ID(SVGA_VERSION_0)

#define SVGA_ID_INVALID 0xFFFFFFFF

#define SVGA_INDEX_PORT 0x0
#define SVGA_VALUE_PORT 0x1
#define SVGA_BIOS_PORT 0x2
#define SVGA_IRQSTATUS_PORT 0x8

#define SVGA_IRQFLAG_ANY_FENCE (1 << 0)
#define SVGA_IRQFLAG_FIFO_PROGRESS (1 << 1)
#define SVGA_IRQFLAG_FENCE_GOAL (1 << 2)
#define SVGA_IRQFLAG_COMMAND_BUFFER (1 << 3)
#define SVGA_IRQFLAG_ERROR (1 << 4)
#define SVGA_IRQFLAG_REG_FENCE_GOAL (1 << 5)
#define SVGA_IRQFLAG_MAX (1 << 6)

#define SVGA_MAX_CURSOR_CMD_BYTES (40 * 1024)
#define SVGA_MAX_CURSOR_CMD_DIMENSION 1024

enum {
	SVGA_REG_ID = 0,
	SVGA_REG_ENABLE = 1,
	SVGA_REG_WIDTH = 2,
	SVGA_REG_HEIGHT = 3,
	SVGA_REG_MAX_WIDTH = 4,
	SVGA_REG_MAX_HEIGHT = 5,
	SVGA_REG_DEPTH = 6,
	SVGA_REG_BITS_PER_PIXEL = 7,
	SVGA_REG_PSEUDOCOLOR = 8,
	SVGA_REG_RED_MASK = 9,
	SVGA_REG_GREEN_MASK = 10,
	SVGA_REG_BLUE_MASK = 11,
	SVGA_REG_BYTES_PER_LINE = 12,
	SVGA_REG_FB_START = 13,
	SVGA_REG_FB_OFFSET = 14,
	SVGA_REG_VRAM_SIZE = 15,
	SVGA_REG_FB_SIZE = 16,

	SVGA_REG_ID_0_TOP = 17,

	SVGA_REG_CAPABILITIES = 17,
	SVGA_REG_MEM_START = 18,
	SVGA_REG_MEM_SIZE = 19,
	SVGA_REG_CONFIG_DONE = 20,
	SVGA_REG_SYNC = 21,
	SVGA_REG_BUSY = 22,
	SVGA_REG_GUEST_ID = 23,
	SVGA_REG_DEAD = 24,
	SVGA_REG_CURSOR_X = 25,
	SVGA_REG_CURSOR_Y = 26,
	SVGA_REG_CURSOR_ON = 27,
	SVGA_REG_HOST_BITS_PER_PIXEL = 28,
	SVGA_REG_SCRATCH_SIZE = 29,
	SVGA_REG_MEM_REGS = 30,
	SVGA_REG_NUM_DISPLAYS = 31,
	SVGA_REG_PITCHLOCK = 32,
	SVGA_REG_IRQMASK = 33,

	SVGA_REG_NUM_GUEST_DISPLAYS = 34,
	SVGA_REG_DISPLAY_ID = 35,
	SVGA_REG_DISPLAY_IS_PRIMARY = 36,
	SVGA_REG_DISPLAY_POSITION_X = 37,
	SVGA_REG_DISPLAY_POSITION_Y = 38,
	SVGA_REG_DISPLAY_WIDTH = 39,
	SVGA_REG_DISPLAY_HEIGHT = 40,

	SVGA_REG_GMR_ID = 41,
	SVGA_REG_GMR_DESCRIPTOR = 42,
	SVGA_REG_GMR_MAX_IDS = 43,
	SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH = 44,

	SVGA_REG_TRACES = 45,
	SVGA_REG_GMRS_MAX_PAGES = 46,
	SVGA_REG_MEMORY_SIZE = 47,
	SVGA_REG_COMMAND_LOW = 48,
	SVGA_REG_COMMAND_HIGH = 49,

	SVGA_REG_MAX_PRIMARY_MEM = 50,

	SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB = 51,

	SVGA_REG_DEV_CAP = 52,
	SVGA_REG_CMD_PREPEND_LOW = 53,
	SVGA_REG_CMD_PREPEND_HIGH = 54,
	SVGA_REG_SCREENTARGET_MAX_WIDTH = 55,
	SVGA_REG_SCREENTARGET_MAX_HEIGHT = 56,
	SVGA_REG_MOB_MAX_SIZE = 57,
	SVGA_REG_BLANK_SCREEN_TARGETS = 58,
	SVGA_REG_CAP2 = 59,
	SVGA_REG_DEVEL_CAP = 60,

	SVGA_REG_GUEST_DRIVER_ID = 61,
	SVGA_REG_GUEST_DRIVER_VERSION1 = 62,
	SVGA_REG_GUEST_DRIVER_VERSION2 = 63,
	SVGA_REG_GUEST_DRIVER_VERSION3 = 64,

	SVGA_REG_CURSOR_MOBID = 65,
	SVGA_REG_CURSOR_MAX_BYTE_SIZE = 66,
	SVGA_REG_CURSOR_MAX_DIMENSION = 67,

	SVGA_REG_FIFO_CAPS = 68,
	SVGA_REG_FENCE = 69,

	SVGA_REG_CURSOR4_ON = 70,
	SVGA_REG_CURSOR4_X = 71,
	SVGA_REG_CURSOR4_Y = 72,
	SVGA_REG_CURSOR4_SCREEN_ID = 73,
	SVGA_REG_CURSOR4_SUBMIT = 74,

	SVGA_REG_SCREENDMA = 75,

	SVGA_REG_GBOBJECT_MEM_SIZE_KB = 76,

	SVGA_REG_REGS_START_HIGH32 = 77,
	SVGA_REG_REGS_START_LOW32 = 78,
	SVGA_REG_FB_START_HIGH32 = 79,
	SVGA_REG_FB_START_LOW32 = 80,

	SVGA_REG_MSHINT = 81,

	SVGA_REG_IRQ_STATUS = 82,

	SVGA_REG_DIRTY_TRACKING = 83,
	SVGA_REG_FENCE_GOAL = 84,

	SVGA_REG_TOP = 85,

	SVGA_PALETTE_BASE = 1024,

	SVGA_SCRATCH_BASE = SVGA_PALETTE_BASE + SVGA_NUM_PALETTE_REGS

};

typedef enum SVGARegGuestDriverId {
	SVGA_REG_GUEST_DRIVER_ID_UNKNOWN = 0,
	SVGA_REG_GUEST_DRIVER_ID_WDDM = 1,
	SVGA_REG_GUEST_DRIVER_ID_LINUX = 2,
	SVGA_REG_GUEST_DRIVER_ID_MAX,

	SVGA_REG_GUEST_DRIVER_ID_SUBMIT = MAX_UINT32,
} SVGARegGuestDriverId;

typedef enum SVGARegMSHint {
	SVGA_REG_MSHINT_DISABLED = 0,
	SVGA_REG_MSHINT_FULL = 1,
	SVGA_REG_MSHINT_RESOLVED = 2,
} SVGARegMSHint;

typedef enum SVGARegDirtyTracking {
	SVGA_REG_DIRTY_TRACKING_PER_IMAGE = 0,
	SVGA_REG_DIRTY_TRACKING_PER_SURFACE = 1,
} SVGARegDirtyTracking;

#define SVGA_GMR_NULL ((uint32)-1)
#define SVGA_GMR_FRAMEBUFFER ((uint32)-2)

#pragma pack(push, 1)
typedef struct SVGAGuestMemDescriptor {
	uint32 ppn;
	uint32 numPages;
} SVGAGuestMemDescriptor;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct SVGAGuestPtr {
	uint32 gmrId;
	uint32 offset;
} SVGAGuestPtr;
#pragma pack(pop)

#define SVGA_CB_MAX_SIZE_DEFAULT (KBYTES_2_BYTES(512))
#define SVGA_CB_MAX_SIZE_4MB (MBYTES_2_BYTES(4))
#define SVGA_CB_MAX_SIZE SVGA_CB_MAX_SIZE_4MB
#define SVGA_CB_MAX_QUEUED_PER_CONTEXT 32
#define SVGA_CB_MAX_COMMAND_SIZE (32 * 1024)

#define SVGA_CB_CONTEXT_MASK 0x3f
typedef enum {
	SVGA_CB_CONTEXT_DEVICE = 0x3f,
	SVGA_CB_CONTEXT_0 = 0x0,
	SVGA_CB_CONTEXT_1 = 0x1,
	SVGA_CB_CONTEXT_MAX = 0x2,
} SVGACBContext;

typedef enum {

	SVGA_CB_STATUS_NONE = 0,

	SVGA_CB_STATUS_COMPLETED = 1,

	SVGA_CB_STATUS_QUEUE_FULL = 2,

	SVGA_CB_STATUS_COMMAND_ERROR = 3,

	SVGA_CB_STATUS_CB_HEADER_ERROR = 4,

	SVGA_CB_STATUS_PREEMPTED = 5,

	SVGA_CB_STATUS_SUBMISSION_ERROR = 6,

	SVGA_CB_STATUS_PARTIAL_COMPLETE = 7,
} SVGACBStatus;

typedef enum {
	SVGA_CB_FLAG_NONE = 0,
	SVGA_CB_FLAG_NO_IRQ = 1 << 0,
	SVGA_CB_FLAG_DX_CONTEXT = 1 << 1,
	SVGA_CB_FLAG_MOB = 1 << 2,
} SVGACBFlags;

#pragma pack(push, 1)
typedef struct {
	volatile SVGACBStatus status;
	volatile uint32 errorOffset;
	uint64 id;
	SVGACBFlags flags;
	uint32 length;
	union {
		PA pa;
		struct {
			SVGAMobId mobid;
			uint32 mobOffset;
		} mob;
	} ptr;
	uint32 offset;
	uint32 dxContext;
	uint32 mustBeZero[6];
} SVGACBHeader;
#pragma pack(pop)

typedef enum {
	SVGA_DC_CMD_NOP = 0,
	SVGA_DC_CMD_START_STOP_CONTEXT = 1,
	SVGA_DC_CMD_PREEMPT = 2,
	SVGA_DC_CMD_START_QUEUE = 3,
	SVGA_DC_CMD_ASYNC_STOP_QUEUE = 4,
	SVGA_DC_CMD_EMPTY_CONTEXT_QUEUE = 5,
	SVGA_DC_CMD_MAX = 6
} SVGADeviceContextCmdId;

typedef struct SVGADCCmdStartStop {
	uint32 enable;
	SVGACBContext context;
} SVGADCCmdStartStop;

typedef struct SVGADCCmdPreempt {
	SVGACBContext context;
	uint32 ignoreIDZero;
} SVGADCCmdPreempt;

typedef struct SVGADCCmdStartQueue {
	SVGACBContext context;
} SVGADCCmdStartQueue;

typedef struct SVGADCCmdAsyncStopQueue {
	SVGACBContext context;
} SVGADCCmdAsyncStopQueue;

typedef struct SVGADCCmdEmptyQueue {
	SVGACBContext context;
} SVGADCCmdEmptyQueue;

typedef struct SVGAGMRImageFormat {
	union {
		struct {
			uint32 bitsPerPixel : 8;
			uint32 colorDepth : 8;
			uint32 reserved : 16;
		};

		uint32 value;
	};
} SVGAGMRImageFormat;

#pragma pack(push, 1)
typedef struct SVGAGuestImage {
	SVGAGuestPtr ptr;

	uint32 pitch;
} SVGAGuestImage;
#pragma pack(pop)

typedef struct SVGAColorBGRX {
	union {
		struct {
			uint32 b : 8;
			uint32 g : 8;
			uint32 r : 8;
			uint32 x : 8;
		};

		uint32 value;
	};
} SVGAColorBGRX;

#pragma pack(push, 1)
typedef struct {
	int32 left;
	int32 top;
	int32 right;
	int32 bottom;
} SVGASignedRect;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	int32 x;
	int32 y;
} SVGASignedPoint;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 x;
	uint32 y;
} SVGAUnsignedPoint;
#pragma pack(pop)

#define SVGA_CAP_NONE 0x00000000
#define SVGA_CAP_RECT_COPY 0x00000002
#define SVGA_CAP_CURSOR 0x00000020
#define SVGA_CAP_CURSOR_BYPASS 0x00000040
#define SVGA_CAP_CURSOR_BYPASS_2 0x00000080
#define SVGA_CAP_8BIT_EMULATION 0x00000100
#define SVGA_CAP_ALPHA_CURSOR 0x00000200
#define SVGA_CAP_3D 0x00004000
#define SVGA_CAP_EXTENDED_FIFO 0x00008000
#define SVGA_CAP_MULTIMON 0x00010000
#define SVGA_CAP_PITCHLOCK 0x00020000
#define SVGA_CAP_IRQMASK 0x00040000
#define SVGA_CAP_DISPLAY_TOPOLOGY 0x00080000
#define SVGA_CAP_GMR 0x00100000
#define SVGA_CAP_TRACES 0x00200000
#define SVGA_CAP_GMR2 0x00400000
#define SVGA_CAP_SCREEN_OBJECT_2 0x00800000
#define SVGA_CAP_COMMAND_BUFFERS 0x01000000
#define SVGA_CAP_DEAD1 0x02000000
#define SVGA_CAP_CMD_BUFFERS_2 0x04000000
#define SVGA_CAP_GBOBJECTS 0x08000000
#define SVGA_CAP_DX 0x10000000
#define SVGA_CAP_HP_CMD_QUEUE 0x20000000
#define SVGA_CAP_NO_BB_RESTRICTION 0x40000000
#define SVGA_CAP_CAP2_REGISTER 0x80000000

#define SVGA_CAP2_NONE 0x00000000
#define SVGA_CAP2_GROW_OTABLE 0x00000001
#define SVGA_CAP2_INTRA_SURFACE_COPY 0x00000002
#define SVGA_CAP2_DX2 0x00000004
#define SVGA_CAP2_GB_MEMSIZE_2 0x00000008
#define SVGA_CAP2_SCREENDMA_REG 0x00000010
#define SVGA_CAP2_OTABLE_PTDEPTH_2 0x00000020
#define SVGA_CAP2_NON_MS_TO_MS_STRETCHBLT 0x00000040
#define SVGA_CAP2_CURSOR_MOB 0x00000080
#define SVGA_CAP2_MSHINT 0x00000100
#define SVGA_CAP2_CB_MAX_SIZE_4MB 0x00000200
#define SVGA_CAP2_DX3 0x00000400
#define SVGA_CAP2_FRAME_TYPE 0x00000800
#define SVGA_CAP2_COTABLE_COPY 0x00001000
#define SVGA_CAP2_TRACE_FULL_FB 0x00002000
#define SVGA_CAP2_EXTRA_REGS 0x00004000
#define SVGA_CAP2_LO_STAGING 0x00008000
#define SVGA_CAP2_RESERVED 0x80000000

typedef enum {
	SVGABackdoorCapDeviceCaps = 0,
	SVGABackdoorCapFifoCaps = 1,
	SVGABackdoorCap3dHWVersion = 2,
	SVGABackdoorCapDeviceCaps2 = 3,
	SVGABackdoorCapDevelCaps = 4,
	SVGABackdoorDevelRenderer = 5,
	SVGABackdoorDevelUsingISB = 6,
	SVGABackdoorCapMax = 7,
} SVGABackdoorCapType;

enum {

	SVGA_FIFO_MIN = 0,
	SVGA_FIFO_MAX,
	SVGA_FIFO_NEXT_CMD,
	SVGA_FIFO_STOP,

	SVGA_FIFO_CAPABILITIES = 4,
	SVGA_FIFO_FLAGS,

	SVGA_FIFO_FENCE,

	SVGA_FIFO_3D_HWVERSION,

	SVGA_FIFO_PITCHLOCK,

	SVGA_FIFO_CURSOR_ON,
	SVGA_FIFO_CURSOR_X,
	SVGA_FIFO_CURSOR_Y,
	SVGA_FIFO_CURSOR_COUNT,
	SVGA_FIFO_CURSOR_LAST_UPDATED,

	SVGA_FIFO_RESERVED,

	SVGA_FIFO_CURSOR_SCREEN_ID,

	SVGA_FIFO_DEAD,

	SVGA_FIFO_3D_HWVERSION_REVISED,

	SVGA_FIFO_3D_CAPS = 32,
	SVGA_FIFO_3D_CAPS_LAST = 32 + 255,

	SVGA_FIFO_GUEST_3D_HWVERSION,
	SVGA_FIFO_FENCE_GOAL,
	SVGA_FIFO_BUSY,

	SVGA_FIFO_NUM_REGS
};

#define SVGA_FIFO_3D_CAPS_SIZE (SVGA_FIFO_3D_CAPS_LAST - SVGA_FIFO_3D_CAPS + 1)

#define SVGA3D_FIFO_CAPS_RECORD_DEVCAPS 0x100
typedef uint32 SVGA3dFifoCapsRecordType;

typedef uint32 SVGA3dFifoCapPair[2];

#pragma pack(push, 1)
typedef struct SVGA3dFifoCapsRecordHeader {
	uint32 length;
	SVGA3dFifoCapsRecordType type;

} SVGA3dFifoCapsRecordHeader;
#pragma pack(pop)

#define SVGA_FIFO_EXTENDED_MANDATORY_REGS (SVGA_FIFO_3D_CAPS_LAST + 1)

#define SVGA_FIFO_CAP_NONE 0
#define SVGA_FIFO_CAP_FENCE (1 << 0)
#define SVGA_FIFO_CAP_ACCELFRONT (1 << 1)
#define SVGA_FIFO_CAP_PITCHLOCK (1 << 2)
#define SVGA_FIFO_CAP_VIDEO (1 << 3)
#define SVGA_FIFO_CAP_CURSOR_BYPASS_3 (1 << 4)
#define SVGA_FIFO_CAP_ESCAPE (1 << 5)
#define SVGA_FIFO_CAP_RESERVE (1 << 6)
#define SVGA_FIFO_CAP_SCREEN_OBJECT (1 << 7)
#define SVGA_FIFO_CAP_GMR2 (1 << 8)
#define SVGA_FIFO_CAP_3D_HWVERSION_REVISED SVGA_FIFO_CAP_GMR2
#define SVGA_FIFO_CAP_SCREEN_OBJECT_2 (1 << 9)
#define SVGA_FIFO_CAP_DEAD (1 << 10)

#define SVGA_FIFO_FLAG_NONE 0
#define SVGA_FIFO_FLAG_ACCELFRONT (1 << 0)
#define SVGA_FIFO_FLAG_RESERVED (1 << 31)

#define SVGA_FIFO_RESERVED_UNKNOWN 0xffffffff

#define SVGA_SCREENDMA_REG_UNDEFINED 0
#define SVGA_SCREENDMA_REG_NOT_PRESENT 1
#define SVGA_SCREENDMA_REG_PRESENT 2
#define SVGA_SCREENDMA_REG_MAX 3

#define SVGA_NUM_OVERLAY_UNITS 32

#define SVGA_VIDEO_FLAG_COLORKEY 0x0001

enum {
	SVGA_VIDEO_ENABLED = 0,
	SVGA_VIDEO_FLAGS,
	SVGA_VIDEO_DATA_OFFSET,
	SVGA_VIDEO_FORMAT,
	SVGA_VIDEO_COLORKEY,
	SVGA_VIDEO_SIZE,
	SVGA_VIDEO_WIDTH,
	SVGA_VIDEO_HEIGHT,
	SVGA_VIDEO_SRC_X,
	SVGA_VIDEO_SRC_Y,
	SVGA_VIDEO_SRC_WIDTH,
	SVGA_VIDEO_SRC_HEIGHT,
	SVGA_VIDEO_DST_X,
	SVGA_VIDEO_DST_Y,
	SVGA_VIDEO_DST_WIDTH,
	SVGA_VIDEO_DST_HEIGHT,
	SVGA_VIDEO_PITCH_1,
	SVGA_VIDEO_PITCH_2,
	SVGA_VIDEO_PITCH_3,
	SVGA_VIDEO_DATA_GMRID,
	SVGA_VIDEO_DST_SCREEN_ID,
	SVGA_VIDEO_NUM_REGS
};

#pragma pack(push, 1)
typedef struct SVGAOverlayUnit {
	uint32 enabled;
	uint32 flags;
	uint32 dataOffset;
	uint32 format;
	uint32 colorKey;
	uint32 size;
	uint32 width;
	uint32 height;
	uint32 srcX;
	uint32 srcY;
	uint32 srcWidth;
	uint32 srcHeight;
	int32 dstX;
	int32 dstY;
	uint32 dstWidth;
	uint32 dstHeight;
	uint32 pitches[3];
	uint32 dataGMRId;
	uint32 dstScreenId;
} SVGAOverlayUnit;
#pragma pack(pop)

#define SVGA_INVALID_DISPLAY_ID ((uint32)-1)

typedef struct SVGADisplayTopology {
	uint16 displayId;
	uint16 isPrimary;
	uint32 width;
	uint32 height;
	uint32 positionX;
	uint32 positionY;
} SVGADisplayTopology;

#define SVGA_SCREEN_MUST_BE_SET (1 << 0)
#define SVGA_SCREEN_HAS_ROOT SVGA_SCREEN_MUST_BE_SET
#define SVGA_SCREEN_IS_PRIMARY (1 << 1)
#define SVGA_SCREEN_FULLSCREEN_HINT (1 << 2)

#define SVGA_SCREEN_DEACTIVATE (1 << 3)

#define SVGA_SCREEN_BLANKING (1 << 4)

#pragma pack(push, 1)
typedef struct {
	uint32 structSize;
	uint32 id;
	uint32 flags;
	struct {
		uint32 width;
		uint32 height;
	} size;
	struct {
		int32 x;
		int32 y;
	} root;

	SVGAGuestImage backingStore;

	uint32 cloneCount;
} SVGAScreenObject;
#pragma pack(pop)

typedef enum {
	SVGA_CMD_INVALID_CMD = 0,
	SVGA_CMD_UPDATE = 1,
	SVGA_CMD_RECT_COPY = 3,
	SVGA_CMD_RECT_ROP_COPY = 14,
	SVGA_CMD_DEFINE_CURSOR = 19,
	SVGA_CMD_DEFINE_ALPHA_CURSOR = 22,
	SVGA_CMD_UPDATE_VERBOSE = 25,
	SVGA_CMD_FRONT_ROP_FILL = 29,
	SVGA_CMD_FENCE = 30,
	SVGA_CMD_ESCAPE = 33,
	SVGA_CMD_DEFINE_SCREEN = 34,
	SVGA_CMD_DESTROY_SCREEN = 35,
	SVGA_CMD_DEFINE_GMRFB = 36,
	SVGA_CMD_BLIT_GMRFB_TO_SCREEN = 37,
	SVGA_CMD_BLIT_SCREEN_TO_GMRFB = 38,
	SVGA_CMD_ANNOTATION_FILL = 39,
	SVGA_CMD_ANNOTATION_COPY = 40,
	SVGA_CMD_DEFINE_GMR2 = 41,
	SVGA_CMD_REMAP_GMR2 = 42,
	SVGA_CMD_DEAD = 43,
	SVGA_CMD_DEAD_2 = 44,
	SVGA_CMD_NOP = 45,
	SVGA_CMD_NOP_ERROR = 46,
	SVGA_CMD_MAX
} SVGAFifoCmdId;

#define SVGA_CMD_MAX_DATASIZE (256 * 1024)
#define SVGA_CMD_MAX_ARGS 64

#pragma pack(push, 1)
typedef struct {
	uint32 x;
	uint32 y;
	uint32 width;
	uint32 height;
} SVGAFifoCmdUpdate;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 srcX;
	uint32 srcY;
	uint32 destX;
	uint32 destY;
	uint32 width;
	uint32 height;
} SVGAFifoCmdRectCopy;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 srcX;
	uint32 srcY;
	uint32 destX;
	uint32 destY;
	uint32 width;
	uint32 height;
	uint32 rop;
} SVGAFifoCmdRectRopCopy;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 id;
	uint32 hotspotX;
	uint32 hotspotY;
	uint32 width;
	uint32 height;
	uint32 andMaskDepth;
	uint32 xorMaskDepth;

} SVGAFifoCmdDefineCursor;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 id;
	uint32 hotspotX;
	uint32 hotspotY;
	uint32 width;
	uint32 height;

} SVGAFifoCmdDefineAlphaCursor;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 hotspotX;
	uint32 hotspotY;
	uint32 width;
	uint32 height;
	uint32 andMaskDepth;
	uint32 xorMaskDepth;

} SVGAGBColorCursorHeader;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 hotspotX;
	uint32 hotspotY;
	uint32 width;
	uint32 height;

} SVGAGBAlphaCursorHeader;
#pragma pack(pop)

typedef enum {
	SVGA_COLOR_CURSOR = 0,
	SVGA_ALPHA_CURSOR = 1,
} SVGAGBCursorType;

#pragma pack(push, 1)
typedef struct {
	SVGAGBCursorType type;
	union {
		SVGAGBColorCursorHeader colorHeader;
		SVGAGBAlphaCursorHeader alphaHeader;
	} header;
	uint32 sizeInBytes;

} SVGAGBCursorHeader;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 x;
	uint32 y;
	uint32 width;
	uint32 height;
	uint32 reason;
} SVGAFifoCmdUpdateVerbose;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 color;
	uint32 x;
	uint32 y;
	uint32 width;
	uint32 height;
	uint32 rop;
} SVGAFifoCmdFrontRopFill;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 fence;
} SVGAFifoCmdFence;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 nsid;
	uint32 size;

} SVGAFifoCmdEscape;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGAScreenObject screen;
} SVGAFifoCmdDefineScreen;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 screenId;
} SVGAFifoCmdDestroyScreen;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGAGuestPtr ptr;
	uint32 bytesPerLine;
	SVGAGMRImageFormat format;
} SVGAFifoCmdDefineGMRFB;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGASignedPoint srcOrigin;
	SVGASignedRect destRect;
	uint32 destScreenId;
} SVGAFifoCmdBlitGMRFBToScreen;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGASignedPoint destOrigin;
	SVGASignedRect srcRect;
	uint32 srcScreenId;
} SVGAFifoCmdBlitScreenToGMRFB;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGAColorBGRX color;
} SVGAFifoCmdAnnotationFill;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGASignedPoint srcOrigin;
	uint32 srcScreenId;
} SVGAFifoCmdAnnotationCopy;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32 gmrId;
	uint32 numPages;
} SVGAFifoCmdDefineGMR2;
#pragma pack(pop)

typedef enum {
	SVGA_REMAP_GMR2_PPN32 = 0,
	SVGA_REMAP_GMR2_VIA_GMR = (1 << 0),
	SVGA_REMAP_GMR2_PPN64 = (1 << 1),
	SVGA_REMAP_GMR2_SINGLE_PPN = (1 << 2),
} SVGARemapGMR2Flags;

#pragma pack(push, 1)
typedef struct {
	uint32 gmrId;
	SVGARemapGMR2Flags flags;
	uint32 offsetPages;
	uint32 numPages;

} SVGAFifoCmdRemapGMR2;
#pragma pack(pop)

#define SVGA_VRAM_MIN_SIZE (4 * 640 * 480)
#define SVGA_VRAM_MIN_SIZE_3D (16 * 1024 * 1024)
#define SVGA_VRAM_MAX_SIZE (128 * 1024 * 1024)
#define SVGA_MEMORY_SIZE_MAX (1024 * 1024 * 1024)
#define SVGA_FIFO_SIZE_MAX (2 * 1024 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_MIN (32 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_MAX_2GB (2 * 1024 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_MAX_3GB (3 * 1024 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_MAX_4GB (4 * 1024 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_MAX_8GB (8 * 1024 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_DEFAULT (256 * 1024)

#define SVGA_VRAM_SIZE_W2K (64 * 1024 * 1024)

#if defined(VMX86_SERVER)
#define SVGA_VRAM_SIZE (4 * 1024 * 1024)
#define SVGA_VRAM_SIZE_3D (64 * 1024 * 1024)
#define SVGA_FIFO_SIZE (256 * 1024)
#define SVGA_FIFO_SIZE_3D (516 * 1024)
#define SVGA_MEMORY_SIZE_DEFAULT (160 * 1024 * 1024)
#define SVGA_AUTODETECT_DEFAULT FALSE
#else
#define SVGA_VRAM_SIZE (16 * 1024 * 1024)
#define SVGA_VRAM_SIZE_3D SVGA_VRAM_MAX_SIZE
#define SVGA_FIFO_SIZE (2 * 1024 * 1024)
#define SVGA_FIFO_SIZE_3D SVGA_FIFO_SIZE
#define SVGA_MEMORY_SIZE_DEFAULT (768 * 1024 * 1024)
#define SVGA_AUTODETECT_DEFAULT TRUE
#endif

#define SVGA_FIFO_SIZE_GBOBJECTS (256 * 1024)
#define SVGA_VRAM_SIZE_GBOBJECTS (4 * 1024 * 1024)

#endif
