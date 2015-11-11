/**********************************************************
 * Copyright 2007-2015 VMware, Inc.  All rights reserved.
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
 * svga_overlay.h --
 *
 *    Definitions for video-overlay support.
 */

#ifndef _SVGA_OVERLAY_H_
#define _SVGA_OVERLAY_H_

#include "svga_reg.h"

/*
 * Video formats we support
 */

#define VMWARE_FOURCC_YV12 0x32315659 /* 'Y' 'V' '1' '2' */
#define VMWARE_FOURCC_YUY2 0x32595559 /* 'Y' 'U' 'Y' '2' */
#define VMWARE_FOURCC_UYVY 0x59565955 /* 'U' 'Y' 'V' 'Y' */

typedef enum {
   SVGA_OVERLAY_FORMAT_INVALID = 0,
   SVGA_OVERLAY_FORMAT_YV12 = VMWARE_FOURCC_YV12,
   SVGA_OVERLAY_FORMAT_YUY2 = VMWARE_FOURCC_YUY2,
   SVGA_OVERLAY_FORMAT_UYVY = VMWARE_FOURCC_UYVY,
} SVGAOverlayFormat;

#define SVGA_VIDEO_COLORKEY_MASK             0x00ffffff

#define SVGA_ESCAPE_VMWARE_VIDEO             0x00020000

#define SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS    0x00020001
        /* FIFO escape layout:
         * Type, Stream Id, (Register Id, Value) pairs */

#define SVGA_ESCAPE_VMWARE_VIDEO_FLUSH       0x00020002
        /* FIFO escape layout:
         * Type, Stream Id */

typedef
struct SVGAEscapeVideoSetRegs {
   struct {
      uint32 cmdType;
      uint32 streamId;
   } header;

   /* May include zero or more items. */
   struct {
      uint32 registerId;
      uint32 value;
   } items[1];
} SVGAEscapeVideoSetRegs;

typedef
struct SVGAEscapeVideoFlush {
   uint32 cmdType;
   uint32 streamId;
} SVGAEscapeVideoFlush;


/*
 * Struct definitions for the video overlay commands built on
 * SVGAFifoCmdEscape.
 */
typedef
struct {
   uint32 command;
   uint32 overlay;
} SVGAFifoEscapeCmdVideoBase;

typedef
struct {
   SVGAFifoEscapeCmdVideoBase videoCmd;
} SVGAFifoEscapeCmdVideoFlush;

typedef
struct {
   SVGAFifoEscapeCmdVideoBase videoCmd;
   struct {
      uint32 regId;
      uint32 value;
   } items[1];
} SVGAFifoEscapeCmdVideoSetRegs;

typedef
struct {
   SVGAFifoEscapeCmdVideoBase videoCmd;
   struct {
      uint32 regId;
      uint32 value;
   } items[SVGA_VIDEO_NUM_REGS];
} SVGAFifoEscapeCmdVideoSetAllRegs;


/*
 *----------------------------------------------------------------------
 *
 * VMwareVideoGetAttributes --
 *
 *      Computes the size, pitches and offsets for YUV frames.
 *
 * Results:
 *      TRUE on success; otherwise FALSE on failure.
 *
 * Side effects:
 *      Pitches and offsets for the given YUV frame are put in 'pitches'
 *      and 'offsets' respectively. They are both optional though.
 *
 *----------------------------------------------------------------------
 */

static inline bool
VMwareVideoGetAttributes(const SVGAOverlayFormat format,    /* IN */
                         uint32 *width,                     /* IN / OUT */
                         uint32 *height,                    /* IN / OUT */
                         uint32 *size,                      /* OUT */
                         uint32 *pitches,                   /* OUT (optional) */
                         uint32 *offsets)                   /* OUT (optional) */
{
    int tmp;

    *width = (*width + 1) & ~1;

    if (offsets) {
        offsets[0] = 0;
    }

    switch (format) {
    case VMWARE_FOURCC_YV12:
       *height = (*height + 1) & ~1;
       *size = (*width) * (*height);

       if (pitches) {
          pitches[0] = *width;
       }

       if (offsets) {
          offsets[1] = *size;
       }

       tmp = *width >> 1;

       if (pitches) {
          pitches[1] = pitches[2] = tmp;
       }

       tmp *= (*height >> 1);
       *size += tmp;

       if (offsets) {
          offsets[2] = *size;
       }

       *size += tmp;
       break;

    case VMWARE_FOURCC_YUY2:
    case VMWARE_FOURCC_UYVY:
       *size = *width * 2;

       if (pitches) {
          pitches[0] = *size;
       }

       *size *= *height;
       break;

    default:
       return false;
    }

    return true;
}

#endif /* _SVGA_OVERLAY_H_ */
