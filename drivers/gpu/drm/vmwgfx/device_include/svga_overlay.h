/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2007-2021 VMware, Inc.
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
 */

/*
 * svga_overlay.h --
 *
 *    Definitions for video-overlay support.
 */



#ifndef _SVGA_OVERLAY_H_
#define _SVGA_OVERLAY_H_

#include "svga_reg.h"

#if defined __cplusplus
extern "C" {
#endif

#define VMWARE_FOURCC_YV12 0x32315659
#define VMWARE_FOURCC_YUY2 0x32595559
#define VMWARE_FOURCC_UYVY 0x59565955

typedef enum {
	SVGA_OVERLAY_FORMAT_INVALID = 0,
	SVGA_OVERLAY_FORMAT_YV12 = VMWARE_FOURCC_YV12,
	SVGA_OVERLAY_FORMAT_YUY2 = VMWARE_FOURCC_YUY2,
	SVGA_OVERLAY_FORMAT_UYVY = VMWARE_FOURCC_UYVY,
} SVGAOverlayFormat;

#define SVGA_VIDEO_COLORKEY_MASK 0x00ffffff

#define SVGA_ESCAPE_VMWARE_VIDEO 0x00020000

#define SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS 0x00020001

#define SVGA_ESCAPE_VMWARE_VIDEO_FLUSH 0x00020002

typedef struct SVGAEscapeVideoSetRegs {
	struct {
		uint32 cmdType;
		uint32 streamId;
	} header;

	struct {
		uint32 registerId;
		uint32 value;
	} items[1];
} SVGAEscapeVideoSetRegs;

typedef struct SVGAEscapeVideoFlush {
	uint32 cmdType;
	uint32 streamId;
} SVGAEscapeVideoFlush;

#pragma pack(push, 1)
typedef struct {
	uint32 command;
	uint32 overlay;
} SVGAFifoEscapeCmdVideoBase;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGAFifoEscapeCmdVideoBase videoCmd;
} SVGAFifoEscapeCmdVideoFlush;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGAFifoEscapeCmdVideoBase videoCmd;
	struct {
		uint32 regId;
		uint32 value;
	} items[1];
} SVGAFifoEscapeCmdVideoSetRegs;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	SVGAFifoEscapeCmdVideoBase videoCmd;
	struct {
		uint32 regId;
		uint32 value;
	} items[SVGA_VIDEO_NUM_REGS];
} SVGAFifoEscapeCmdVideoSetAllRegs;
#pragma pack(pop)

#if defined __cplusplus
}
#endif

#endif
