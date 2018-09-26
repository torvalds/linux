/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 * Copyright 1998-2015 VMware, Inc.
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
#include <linux/pci_ids.h>

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_USERLEVEL

#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"

#include "svga_types.h"

/*
 * SVGA_REG_ENABLE bit definitions.
 */
typedef enum {
   SVGA_REG_ENABLE_DISABLE = 0,
   SVGA_REG_ENABLE_ENABLE = (1 << 0),
   SVGA_REG_ENABLE_HIDE = (1 << 1),
} SvgaRegEnable;

typedef uint32 SVGAMobId;

/*
 * Arbitrary and meaningless limits. Please ignore these when writing
 * new drivers.
 */
#define SVGA_MAX_WIDTH                  2560
#define SVGA_MAX_HEIGHT                 1600


#define SVGA_MAX_BITS_PER_PIXEL         32
#define SVGA_MAX_DEPTH                  24
#define SVGA_MAX_DISPLAYS               10
#define SVGA_MAX_SCREEN_SIZE            8192
#define SVGA_SCREEN_ROOT_LIMIT (SVGA_MAX_SCREEN_SIZE * SVGA_MAX_DISPLAYS)


/*
 * Legal values for the SVGA_REG_CURSOR_ON register in old-fashioned
 * cursor bypass mode. This is still supported, but no new guest
 * drivers should use it.
 */
#define SVGA_CURSOR_ON_HIDE            0x0
#define SVGA_CURSOR_ON_SHOW            0x1

/*
 * Remove the cursor from the framebuffer
 * because we need to see what's under it
 */
#define SVGA_CURSOR_ON_REMOVE_FROM_FB  0x2

/* Put the cursor back in the framebuffer so the user can see it */
#define SVGA_CURSOR_ON_RESTORE_TO_FB   0x3

/*
 * The maximum framebuffer size that can traced for guests unless the
 * SVGA_CAP_GBOBJECTS is set in SVGA_REG_CAPABILITIES.  In that case
 * the full framebuffer can be traced independent of this limit.
 */
#define SVGA_FB_MAX_TRACEABLE_SIZE      0x1000000

#define SVGA_MAX_PSEUDOCOLOR_DEPTH      8
#define SVGA_MAX_PSEUDOCOLORS           (1 << SVGA_MAX_PSEUDOCOLOR_DEPTH)
#define SVGA_NUM_PALETTE_REGS           (3 * SVGA_MAX_PSEUDOCOLORS)

#define SVGA_MAGIC         0x900000UL
#define SVGA_MAKE_ID(ver)  (SVGA_MAGIC << 8 | (ver))

/* Version 2 let the address of the frame buffer be unsigned on Win32 */
#define SVGA_VERSION_2     2
#define SVGA_ID_2          SVGA_MAKE_ID(SVGA_VERSION_2)

/* Version 1 has new registers starting with SVGA_REG_CAPABILITIES so
   PALETTE_BASE has moved */
#define SVGA_VERSION_1     1
#define SVGA_ID_1          SVGA_MAKE_ID(SVGA_VERSION_1)

/* Version 0 is the initial version */
#define SVGA_VERSION_0     0
#define SVGA_ID_0          SVGA_MAKE_ID(SVGA_VERSION_0)

/*
 * "Invalid" value for all SVGA IDs.
 * (Version ID, screen object ID, surface ID...)
 */
#define SVGA_ID_INVALID    0xFFFFFFFF

/* Port offsets, relative to BAR0 */
#define SVGA_INDEX_PORT         0x0
#define SVGA_VALUE_PORT         0x1
#define SVGA_BIOS_PORT          0x2
#define SVGA_IRQSTATUS_PORT     0x8

/*
 * Interrupt source flags for IRQSTATUS_PORT and IRQMASK.
 *
 * Interrupts are only supported when the
 * SVGA_CAP_IRQMASK capability is present.
 */
#define SVGA_IRQFLAG_ANY_FENCE            0x1    /* Any fence was passed */
#define SVGA_IRQFLAG_FIFO_PROGRESS        0x2    /* Made forward progress in the FIFO */
#define SVGA_IRQFLAG_FENCE_GOAL           0x4    /* SVGA_FIFO_FENCE_GOAL reached */
#define SVGA_IRQFLAG_COMMAND_BUFFER       0x8    /* Command buffer completed */
#define SVGA_IRQFLAG_ERROR                0x10   /* Error while processing commands */

/*
 * Registers
 */

enum {
   SVGA_REG_ID = 0,
   SVGA_REG_ENABLE = 1,
   SVGA_REG_WIDTH = 2,
   SVGA_REG_HEIGHT = 3,
   SVGA_REG_MAX_WIDTH = 4,
   SVGA_REG_MAX_HEIGHT = 5,
   SVGA_REG_DEPTH = 6,
   SVGA_REG_BITS_PER_PIXEL = 7,       /* Current bpp in the guest */
   SVGA_REG_PSEUDOCOLOR = 8,
   SVGA_REG_RED_MASK = 9,
   SVGA_REG_GREEN_MASK = 10,
   SVGA_REG_BLUE_MASK = 11,
   SVGA_REG_BYTES_PER_LINE = 12,
   SVGA_REG_FB_START = 13,            /* (Deprecated) */
   SVGA_REG_FB_OFFSET = 14,
   SVGA_REG_VRAM_SIZE = 15,
   SVGA_REG_FB_SIZE = 16,

   /* ID 0 implementation only had the above registers, then the palette */
   SVGA_REG_ID_0_TOP = 17,

   SVGA_REG_CAPABILITIES = 17,
   SVGA_REG_MEM_START = 18,           /* (Deprecated) */
   SVGA_REG_MEM_SIZE = 19,
   SVGA_REG_CONFIG_DONE = 20,         /* Set when memory area configured */
   SVGA_REG_SYNC = 21,                /* See "FIFO Synchronization Registers" */
   SVGA_REG_BUSY = 22,                /* See "FIFO Synchronization Registers" */
   SVGA_REG_GUEST_ID = 23,            /* (Deprecated) */
   SVGA_REG_CURSOR_ID = 24,           /* (Deprecated) */
   SVGA_REG_CURSOR_X = 25,            /* (Deprecated) */
   SVGA_REG_CURSOR_Y = 26,            /* (Deprecated) */
   SVGA_REG_CURSOR_ON = 27,           /* (Deprecated) */
   SVGA_REG_HOST_BITS_PER_PIXEL = 28, /* (Deprecated) */
   SVGA_REG_SCRATCH_SIZE = 29,        /* Number of scratch registers */
   SVGA_REG_MEM_REGS = 30,            /* Number of FIFO registers */
   SVGA_REG_NUM_DISPLAYS = 31,        /* (Deprecated) */
   SVGA_REG_PITCHLOCK = 32,           /* Fixed pitch for all modes */
   SVGA_REG_IRQMASK = 33,             /* Interrupt mask */

   /* Legacy multi-monitor support */
   SVGA_REG_NUM_GUEST_DISPLAYS = 34,/* Number of guest displays in X/Y direction */
   SVGA_REG_DISPLAY_ID = 35,        /* Display ID for the following display attributes */
   SVGA_REG_DISPLAY_IS_PRIMARY = 36,/* Whether this is a primary display */
   SVGA_REG_DISPLAY_POSITION_X = 37,/* The display position x */
   SVGA_REG_DISPLAY_POSITION_Y = 38,/* The display position y */
   SVGA_REG_DISPLAY_WIDTH = 39,     /* The display's width */
   SVGA_REG_DISPLAY_HEIGHT = 40,    /* The display's height */

   /* See "Guest memory regions" below. */
   SVGA_REG_GMR_ID = 41,
   SVGA_REG_GMR_DESCRIPTOR = 42,
   SVGA_REG_GMR_MAX_IDS = 43,
   SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH = 44,

   SVGA_REG_TRACES = 45,            /* Enable trace-based updates even when FIFO is on */
   SVGA_REG_GMRS_MAX_PAGES = 46,    /* Maximum number of 4KB pages for all GMRs */
   SVGA_REG_MEMORY_SIZE = 47,       /* Total dedicated device memory excluding FIFO */
   SVGA_REG_COMMAND_LOW = 48,       /* Lower 32 bits and submits commands */
   SVGA_REG_COMMAND_HIGH = 49,      /* Upper 32 bits of command buffer PA */

   /*
    * Max primary memory.
    * See SVGA_CAP_NO_BB_RESTRICTION.
    */
   SVGA_REG_MAX_PRIMARY_MEM = 50,
   SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM = 50,

   SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB = 51, /* Sugested limit on mob mem */
   SVGA_REG_DEV_CAP = 52,           /* Write dev cap index, read value */
   SVGA_REG_CMD_PREPEND_LOW = 53,
   SVGA_REG_CMD_PREPEND_HIGH = 54,
   SVGA_REG_SCREENTARGET_MAX_WIDTH = 55,
   SVGA_REG_SCREENTARGET_MAX_HEIGHT = 56,
   SVGA_REG_MOB_MAX_SIZE = 57,
   SVGA_REG_BLANK_SCREEN_TARGETS = 58,
   SVGA_REG_CAP2 = 59,
   SVGA_REG_DEVEL_CAP = 60,
   SVGA_REG_TOP = 61,               /* Must be 1 more than the last register */

   SVGA_PALETTE_BASE = 1024,        /* Base of SVGA color map */
   /* Next 768 (== 256*3) registers exist for colormap */
   SVGA_SCRATCH_BASE = SVGA_PALETTE_BASE + SVGA_NUM_PALETTE_REGS
                                    /* Base of scratch registers */
   /* Next reg[SVGA_REG_SCRATCH_SIZE] registers exist for scratch usage:
      First 4 are reserved for VESA BIOS Extension; any remaining are for
      the use of the current SVGA driver. */
};

/*
 * Guest memory regions (GMRs):
 *
 * This is a new memory mapping feature available in SVGA devices
 * which have the SVGA_CAP_GMR bit set. Previously, there were two
 * fixed memory regions available with which to share data between the
 * device and the driver: the FIFO ('MEM') and the framebuffer. GMRs
 * are our name for an extensible way of providing arbitrary DMA
 * buffers for use between the driver and the SVGA device. They are a
 * new alternative to framebuffer memory, usable for both 2D and 3D
 * graphics operations.
 *
 * Since GMR mapping must be done synchronously with guest CPU
 * execution, we use a new pair of SVGA registers:
 *
 *   SVGA_REG_GMR_ID --
 *
 *     Read/write.
 *     This register holds the 32-bit ID (a small positive integer)
 *     of a GMR to create, delete, or redefine. Writing this register
 *     has no side-effects.
 *
 *   SVGA_REG_GMR_DESCRIPTOR --
 *
 *     Write-only.
 *     Writing this register will create, delete, or redefine the GMR
 *     specified by the above ID register. If this register is zero,
 *     the GMR is deleted. Any pointers into this GMR (including those
 *     currently being processed by FIFO commands) will be
 *     synchronously invalidated.
 *
 *     If this register is nonzero, it must be the physical page
 *     number (PPN) of a data structure which describes the physical
 *     layout of the memory region this GMR should describe. The
 *     descriptor structure will be read synchronously by the SVGA
 *     device when this register is written. The descriptor need not
 *     remain allocated for the lifetime of the GMR.
 *
 *     The guest driver should write SVGA_REG_GMR_ID first, then
 *     SVGA_REG_GMR_DESCRIPTOR.
 *
 *   SVGA_REG_GMR_MAX_IDS --
 *
 *     Read-only.
 *     The SVGA device may choose to support a maximum number of
 *     user-defined GMR IDs. This register holds the number of supported
 *     IDs. (The maximum supported ID plus 1)
 *
 *   SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH --
 *
 *     Read-only.
 *     The SVGA device may choose to put a limit on the total number
 *     of SVGAGuestMemDescriptor structures it will read when defining
 *     a single GMR.
 *
 * The descriptor structure is an array of SVGAGuestMemDescriptor
 * structures. Each structure may do one of three things:
 *
 *   - Terminate the GMR descriptor list.
 *     (ppn==0, numPages==0)
 *
 *   - Add a PPN or range of PPNs to the GMR's virtual address space.
 *     (ppn != 0, numPages != 0)
 *
 *   - Provide the PPN of the next SVGAGuestMemDescriptor, in order to
 *     support multi-page GMR descriptor tables without forcing the
 *     driver to allocate physically contiguous memory.
 *     (ppn != 0, numPages == 0)
 *
 * Note that each physical page of SVGAGuestMemDescriptor structures
 * can describe at least 2MB of guest memory. If the driver needs to
 * use more than one page of descriptor structures, it must use one of
 * its SVGAGuestMemDescriptors to point to an additional page.  The
 * device will never automatically cross a page boundary.
 *
 * Once the driver has described a GMR, it is immediately available
 * for use via any FIFO command that uses an SVGAGuestPtr structure.
 * These pointers include a GMR identifier plus an offset into that
 * GMR.
 *
 * The driver must check the SVGA_CAP_GMR bit before using the GMR
 * registers.
 */

/*
 * Special GMR IDs, allowing SVGAGuestPtrs to point to framebuffer
 * memory as well.  In the future, these IDs could even be used to
 * allow legacy memory regions to be redefined by the guest as GMRs.
 *
 * Using the guest framebuffer (GFB) at BAR1 for general purpose DMA
 * is being phased out. Please try to use user-defined GMRs whenever
 * possible.
 */
#define SVGA_GMR_NULL         ((uint32) -1)
#define SVGA_GMR_FRAMEBUFFER  ((uint32) -2)  /* Guest Framebuffer (GFB) */

typedef
#include "vmware_pack_begin.h"
struct SVGAGuestMemDescriptor {
   uint32 ppn;
   uint32 numPages;
}
#include "vmware_pack_end.h"
SVGAGuestMemDescriptor;

typedef
#include "vmware_pack_begin.h"
struct SVGAGuestPtr {
   uint32 gmrId;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGAGuestPtr;

/*
 * Register based command buffers --
 *
 * Provide an SVGA device interface that allows the guest to submit
 * command buffers to the SVGA device through an SVGA device register.
 * The metadata for each command buffer is contained in the
 * SVGACBHeader structure along with the return status codes.
 *
 * The SVGA device supports command buffers if
 * SVGA_CAP_COMMAND_BUFFERS is set in the device caps register.  The
 * fifo must be enabled for command buffers to be submitted.
 *
 * Command buffers are submitted when the guest writing the 64 byte
 * aligned physical address into the SVGA_REG_COMMAND_LOW and
 * SVGA_REG_COMMAND_HIGH.  SVGA_REG_COMMAND_HIGH contains the upper 32
 * bits of the physical address.  SVGA_REG_COMMAND_LOW contains the
 * lower 32 bits of the physical address, since the command buffer
 * headers are required to be 64 byte aligned the lower 6 bits are
 * used for the SVGACBContext value.  Writing to SVGA_REG_COMMAND_LOW
 * submits the command buffer to the device and queues it for
 * execution.  The SVGA device supports at least
 * SVGA_CB_MAX_QUEUED_PER_CONTEXT command buffers that can be queued
 * per context and if that limit is reached the device will write the
 * status SVGA_CB_STATUS_QUEUE_FULL to the status value of the command
 * buffer header synchronously and not raise any IRQs.
 *
 * It is invalid to submit a command buffer without a valid physical
 * address and results are undefined.
 *
 * The device guarantees that command buffers of size SVGA_CB_MAX_SIZE
 * will be supported.  If a larger command buffer is submitted results
 * are unspecified and the device will either complete the command
 * buffer or return an error.
 *
 * The device guarantees that any individual command in a command
 * buffer can be up to SVGA_CB_MAX_COMMAND_SIZE in size which is
 * enough to fit a 64x64 color-cursor definition.  If the command is
 * too large the device is allowed to process the command or return an
 * error.
 *
 * The device context is a special SVGACBContext that allows for
 * synchronous register like accesses with the flexibility of
 * commands.  There is a different command set defined by
 * SVGADeviceContextCmdId.  The commands in each command buffer is not
 * allowed to straddle physical pages.
 *
 * The offset field which is available starting with the
 * SVGA_CAP_CMD_BUFFERS_2 cap bit can be set by the guest to bias the
 * start of command processing into the buffer.  If an error is
 * encountered the errorOffset will still be relative to the specific
 * PA, not biased by the offset.  When the command buffer is finished
 * the guest should not read the offset field as there is no guarantee
 * what it will set to.
 *
 * When the SVGA_CAP_HP_CMD_QUEUE cap bit is set a new command queue
 * SVGA_CB_CONTEXT_1 is available.  Commands submitted to this queue
 * will be executed as quickly as possible by the SVGA device
 * potentially before already queued commands on SVGA_CB_CONTEXT_0.
 * The SVGA device guarantees that any command buffers submitted to
 * SVGA_CB_CONTEXT_0 will be executed after any _already_ submitted
 * command buffers to SVGA_CB_CONTEXT_1.
 */

#define SVGA_CB_MAX_SIZE (512 * 1024)  /* 512 KB */
#define SVGA_CB_MAX_QUEUED_PER_CONTEXT 32
#define SVGA_CB_MAX_COMMAND_SIZE (32 * 1024) /* 32 KB */

#define SVGA_CB_CONTEXT_MASK 0x3f
typedef enum {
   SVGA_CB_CONTEXT_DEVICE = 0x3f,
   SVGA_CB_CONTEXT_0      = 0x0,
   SVGA_CB_CONTEXT_1      = 0x1, /* Supported with SVGA_CAP_HP_CMD_QUEUE */
   SVGA_CB_CONTEXT_MAX    = 0x2,
   SVGA_CB_CONTEXT_HP_MAX = 0x2,
} SVGACBContext;


typedef enum {
   /*
    * The guest is supposed to write SVGA_CB_STATUS_NONE to the status
    * field before submitting the command buffer header, the host will
    * change the value when it is done with the command buffer.
    */
   SVGA_CB_STATUS_NONE             = 0,

   /*
    * Written by the host when a command buffer completes successfully.
    * The device raises an IRQ with SVGA_IRQFLAG_COMMAND_BUFFER unless
    * the SVGA_CB_FLAG_NO_IRQ flag is set.
    */
   SVGA_CB_STATUS_COMPLETED        = 1,

   /*
    * Written by the host synchronously with the command buffer
    * submission to indicate the command buffer was not submitted.  No
    * IRQ is raised.
    */
   SVGA_CB_STATUS_QUEUE_FULL       = 2,

   /*
    * Written by the host when an error was detected parsing a command
    * in the command buffer, errorOffset is written to contain the
    * offset to the first byte of the failing command.  The device
    * raises the IRQ with both SVGA_IRQFLAG_ERROR and
    * SVGA_IRQFLAG_COMMAND_BUFFER.  Some of the commands may have been
    * processed.
    */
   SVGA_CB_STATUS_COMMAND_ERROR    = 3,

   /*
    * Written by the host if there is an error parsing the command
    * buffer header.  The device raises the IRQ with both
    * SVGA_IRQFLAG_ERROR and SVGA_IRQFLAG_COMMAND_BUFFER.  The device
    * did not processes any of the command buffer.
    */
   SVGA_CB_STATUS_CB_HEADER_ERROR  = 4,

   /*
    * Written by the host if the guest requested the host to preempt
    * the command buffer.  The device will not raise any IRQs and the
    * command buffer was not processed.
    */
   SVGA_CB_STATUS_PREEMPTED        = 5,

   /*
    * Written by the host synchronously with the command buffer
    * submission to indicate the the command buffer was not submitted
    * due to an error.  No IRQ is raised.
    */
   SVGA_CB_STATUS_SUBMISSION_ERROR = 6,

   /*
    * Written by the host when the host finished a
    * SVGA_DC_CMD_ASYNC_STOP_QUEUE request for this command buffer
    * queue.  The offset of the first byte not processed is stored in
    * the errorOffset field of the command buffer header.  All guest
    * visible side effects of commands till that point are guaranteed
    * to be finished before this is written.  The
    * SVGA_IRQFLAG_COMMAND_BUFFER IRQ is raised as long as the
    * SVGA_CB_FLAG_NO_IRQ is not set.
    */
   SVGA_CB_STATUS_PARTIAL_COMPLETE = 7,
} SVGACBStatus;

typedef enum {
   SVGA_CB_FLAG_NONE       = 0,
   SVGA_CB_FLAG_NO_IRQ     = 1 << 0,
   SVGA_CB_FLAG_DX_CONTEXT = 1 << 1,
   SVGA_CB_FLAG_MOB        = 1 << 2,
} SVGACBFlags;

typedef
#include "vmware_pack_begin.h"
struct {
   volatile SVGACBStatus status; /* Modified by device. */
   volatile uint32 errorOffset;  /* Modified by device. */
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
   uint32 offset; /* Valid if CMD_BUFFERS_2 cap set, must be zero otherwise,
                   * modified by device.
                   */
   uint32 dxContext; /* Valid if DX_CONTEXT flag set, must be zero otherwise */
   uint32 mustBeZero[6];
}
#include "vmware_pack_end.h"
SVGACBHeader;

typedef enum {
   SVGA_DC_CMD_NOP                   = 0,
   SVGA_DC_CMD_START_STOP_CONTEXT    = 1,
   SVGA_DC_CMD_PREEMPT               = 2,
   SVGA_DC_CMD_START_QUEUE           = 3, /* Requires SVGA_CAP_HP_CMD_QUEUE */
   SVGA_DC_CMD_ASYNC_STOP_QUEUE      = 4, /* Requires SVGA_CAP_HP_CMD_QUEUE */
   SVGA_DC_CMD_EMPTY_CONTEXT_QUEUE   = 5, /* Requires SVGA_CAP_HP_CMD_QUEUE */
   SVGA_DC_CMD_MAX                   = 6,
} SVGADeviceContextCmdId;

/*
 * Starts or stops both SVGA_CB_CONTEXT_0 and SVGA_CB_CONTEXT_1.
 */

typedef struct SVGADCCmdStartStop {
   uint32 enable;
   SVGACBContext context; /* Must be zero */
} SVGADCCmdStartStop;

/*
 * SVGADCCmdPreempt --
 *
 * This command allows the guest to request that all command buffers
 * on SVGA_CB_CONTEXT_0 be preempted that can be.  After execution
 * of this command all command buffers that were preempted will
 * already have SVGA_CB_STATUS_PREEMPTED written into the status
 * field.  The device might still be processing a command buffer,
 * assuming execution of it started before the preemption request was
 * received.  Specifying the ignoreIDZero flag to TRUE will cause the
 * device to not preempt command buffers with the id field in the
 * command buffer header set to zero.
 */

typedef struct SVGADCCmdPreempt {
   SVGACBContext context; /* Must be zero */
   uint32 ignoreIDZero;
} SVGADCCmdPreempt;

/*
 * Starts the requested command buffer processing queue.  Valid only
 * if the SVGA_CAP_HP_CMD_QUEUE cap is set.
 *
 * For a command queue to be considered runnable it must be enabled
 * and any corresponding higher priority queues must also be enabled.
 * For example in order for command buffers to be processed on
 * SVGA_CB_CONTEXT_0 both SVGA_CB_CONTEXT_0 and SVGA_CB_CONTEXT_1 must
 * be enabled.  But for commands to be runnable on SVGA_CB_CONTEXT_1
 * only that queue must be enabled.
 */

typedef struct SVGADCCmdStartQueue {
   SVGACBContext context;
} SVGADCCmdStartQueue;

/*
 * Requests the SVGA device to stop processing the requested command
 * buffer queue as soon as possible.  The guest knows the stop has
 * completed when one of the following happens.
 *
 * 1) A command buffer status of SVGA_CB_STATUS_PARTIAL_COMPLETE is returned
 * 2) A command buffer error is encountered with would stop the queue
 *    regardless of the async stop request.
 * 3) All command buffers that have been submitted complete successfully.
 * 4) The stop completes synchronously if no command buffers are
 *    active on the queue when it is issued.
 *
 * If the command queue is not in a runnable state there is no
 * guarentee this async stop will finish.  For instance if the high
 * priority queue is not enabled and a stop is requested on the low
 * priority queue, the high priority queue must be reenabled to
 * guarantee that the async stop will finish.
 *
 * This command along with SVGA_DC_CMD_EMPTY_CONTEXT_QUEUE can be used
 * to implement mid command buffer preemption.
 *
 * Valid only if the SVGA_CAP_HP_CMD_QUEUE cap is set.
 */

typedef struct SVGADCCmdAsyncStopQueue {
   SVGACBContext context;
} SVGADCCmdAsyncStopQueue;

/*
 * Requests the SVGA device to throw away any full command buffers on
 * the requested command queue that have not been started.  For a
 * driver to know which command buffers were thrown away a driver
 * should only issue this command when the queue is stopped, for
 * whatever reason.
 */

typedef struct SVGADCCmdEmptyQueue {
   SVGACBContext context;
} SVGADCCmdEmptyQueue;


/*
 * SVGAGMRImageFormat --
 *
 *    This is a packed representation of the source 2D image format
 *    for a GMR-to-screen blit. Currently it is defined as an encoding
 *    of the screen's color depth and bits-per-pixel, however, 16 bits
 *    are reserved for future use to identify other encodings (such as
 *    RGBA or higher-precision images).
 *
 *    Currently supported formats:
 *
 *       bpp depth  Format Name
 *       --- -----  -----------
 *        32    24  32-bit BGRX
 *        24    24  24-bit BGR
 *        16    16  RGB 5-6-5
 *        16    15  RGB 5-5-5
 *
 */

typedef struct SVGAGMRImageFormat {
   union {
      struct {
         uint32 bitsPerPixel : 8;
         uint32 colorDepth   : 8;
         uint32 reserved     : 16;  /* Must be zero */
      };

      uint32 value;
   };
} SVGAGMRImageFormat;

typedef
#include "vmware_pack_begin.h"
struct SVGAGuestImage {
   SVGAGuestPtr         ptr;

   /*
    * A note on interpretation of pitch: This value of pitch is the
    * number of bytes between vertically adjacent image
    * blocks. Normally this is the number of bytes between the first
    * pixel of two adjacent scanlines. With compressed textures,
    * however, this may represent the number of bytes between
    * compression blocks rather than between rows of pixels.
    *
    * XXX: Compressed textures currently must be tightly packed in guest memory.
    *
    * If the image is 1-dimensional, pitch is ignored.
    *
    * If 'pitch' is zero, the SVGA3D device calculates a pitch value
    * assuming each row of blocks is tightly packed.
    */
   uint32 pitch;
}
#include "vmware_pack_end.h"
SVGAGuestImage;

/*
 * SVGAColorBGRX --
 *
 *    A 24-bit color format (BGRX), which does not depend on the
 *    format of the legacy guest framebuffer (GFB) or the current
 *    GMRFB state.
 */

typedef struct SVGAColorBGRX {
   union {
      struct {
         uint32 b : 8;
         uint32 g : 8;
         uint32 r : 8;
	 uint32 x : 8;  /* Unused */
      };

      uint32 value;
   };
} SVGAColorBGRX;


/*
 * SVGASignedRect --
 * SVGASignedPoint --
 *
 *    Signed rectangle and point primitives. These are used by the new
 *    2D primitives for drawing to Screen Objects, which can occupy a
 *    signed virtual coordinate space.
 *
 *    SVGASignedRect specifies a half-open interval: the (left, top)
 *    pixel is part of the rectangle, but the (right, bottom) pixel is
 *    not.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   int32  left;
   int32  top;
   int32  right;
   int32  bottom;
}
#include "vmware_pack_end.h"
SVGASignedRect;

typedef
#include "vmware_pack_begin.h"
struct {
   int32  x;
   int32  y;
}
#include "vmware_pack_end.h"
SVGASignedPoint;


/*
 * SVGA Device Capabilities
 *
 * Note the holes in the bitfield. Missing bits have been deprecated,
 * and must not be reused. Those capabilities will never be reported
 * by new versions of the SVGA device.
 *
 * XXX: Add longer descriptions for each capability, including a list
 *      of the new features that each capability provides.
 *
 * SVGA_CAP_IRQMASK --
 *    Provides device interrupts.  Adds device register SVGA_REG_IRQMASK
 *    to set interrupt mask and direct I/O port SVGA_IRQSTATUS_PORT to
 *    set/clear pending interrupts.
 *
 * SVGA_CAP_GMR --
 *    Provides synchronous mapping of guest memory regions (GMR).
 *    Adds device registers SVGA_REG_GMR_ID, SVGA_REG_GMR_DESCRIPTOR,
 *    SVGA_REG_GMR_MAX_IDS, and SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH.
 *
 * SVGA_CAP_TRACES --
 *    Allows framebuffer trace-based updates even when FIFO is enabled.
 *    Adds device register SVGA_REG_TRACES.
 *
 * SVGA_CAP_GMR2 --
 *    Provides asynchronous commands to define and remap guest memory
 *    regions.  Adds device registers SVGA_REG_GMRS_MAX_PAGES and
 *    SVGA_REG_MEMORY_SIZE.
 *
 * SVGA_CAP_SCREEN_OBJECT_2 --
 *    Allow screen object support, and require backing stores from the
 *    guest for each screen object.
 *
 * SVGA_CAP_COMMAND_BUFFERS --
 *    Enable register based command buffer submission.
 *
 * SVGA_CAP_DEAD1 --
 *    This cap was incorrectly used by old drivers and should not be
 *    reused.
 *
 * SVGA_CAP_CMD_BUFFERS_2 --
 *    Enable support for the prepend command buffer submision
 *    registers.  SVGA_REG_CMD_PREPEND_LOW and
 *    SVGA_REG_CMD_PREPEND_HIGH.
 *
 * SVGA_CAP_GBOBJECTS --
 *    Enable guest-backed objects and surfaces.
 *
 * SVGA_CAP_DX --
 *    Enable support for DX commands, and command buffers in a mob.
 *
 * SVGA_CAP_HP_CMD_QUEUE --
 *    Enable support for the high priority command queue, and the
 *    ScreenCopy command.
 *
 * SVGA_CAP_NO_BB_RESTRICTION --
 *    Allow ScreenTargets to be defined without regard to the 32-bpp
 *    bounding-box memory restrictions. ie:
 *
 *    The summed memory usage of all screens (assuming they were defined as
 *    32-bpp) must always be less than the value of the
 *    SVGA_REG_MAX_PRIMARY_MEM register.
 *
 *    If this cap is not present, the 32-bpp bounding box around all screens
 *    must additionally be under the value of the SVGA_REG_MAX_PRIMARY_MEM
 *    register.
 *
 *    If the cap is present, the bounding box restriction is lifted (and only
 *    the screen-sum limit applies).
 *
 *    (Note that this is a slight lie... there is still a sanity limit on any
 *     dimension of the topology to be less than SVGA_SCREEN_ROOT_LIMIT, even
 *     when SVGA_CAP_NO_BB_RESTRICTION is present, but that should be
 *     large enough to express any possible topology without holes between
 *     monitors.)
 *
 * SVGA_CAP_CAP2_REGISTER --
 *    If this cap is present, the SVGA_REG_CAP2 register is supported.
 */

#define SVGA_CAP_NONE               0x00000000
#define SVGA_CAP_RECT_COPY          0x00000002
#define SVGA_CAP_CURSOR             0x00000020
#define SVGA_CAP_CURSOR_BYPASS      0x00000040
#define SVGA_CAP_CURSOR_BYPASS_2    0x00000080
#define SVGA_CAP_8BIT_EMULATION     0x00000100
#define SVGA_CAP_ALPHA_CURSOR       0x00000200
#define SVGA_CAP_3D                 0x00004000
#define SVGA_CAP_EXTENDED_FIFO      0x00008000
#define SVGA_CAP_MULTIMON           0x00010000
#define SVGA_CAP_PITCHLOCK          0x00020000
#define SVGA_CAP_IRQMASK            0x00040000
#define SVGA_CAP_DISPLAY_TOPOLOGY   0x00080000
#define SVGA_CAP_GMR                0x00100000
#define SVGA_CAP_TRACES             0x00200000
#define SVGA_CAP_GMR2               0x00400000
#define SVGA_CAP_SCREEN_OBJECT_2    0x00800000
#define SVGA_CAP_COMMAND_BUFFERS    0x01000000
#define SVGA_CAP_DEAD1              0x02000000
#define SVGA_CAP_CMD_BUFFERS_2      0x04000000
#define SVGA_CAP_GBOBJECTS          0x08000000
#define SVGA_CAP_DX                 0x10000000
#define SVGA_CAP_HP_CMD_QUEUE       0x20000000
#define SVGA_CAP_NO_BB_RESTRICTION  0x40000000
#define SVGA_CAP_CAP2_REGISTER      0x80000000

/*
 * The SVGA_REG_CAP2 register is an additional set of SVGA capability bits.
 *
 * SVGA_CAP2_GROW_OTABLE --
 *      Allow the GrowOTable/DXGrowCOTable commands.
 *
 * SVGA_CAP2_INTRA_SURFACE_COPY --
 *      Allow the IntraSurfaceCopy command.
 *
 * SVGA_CAP2_DX2 --
 *      Allow the DefineGBSurface_v3, WholeSurfaceCopy.
 *
 * SVGA_CAP2_RESERVED --
 *      Reserve the last bit for extending the SVGA capabilities to some
 *      future mechanisms.
 */
#define SVGA_CAP2_NONE               0x00000000
#define SVGA_CAP2_GROW_OTABLE        0x00000001
#define SVGA_CAP2_INTRA_SURFACE_COPY 0x00000002
#define SVGA_CAP2_DX2                0x00000004
#define SVGA_CAP2_RESERVED           0x80000000


/*
 * The Guest can optionally read some SVGA device capabilities through
 * the backdoor with command BDOOR_CMD_GET_SVGA_CAPABILITIES before
 * the SVGA device is initialized.  The type of capability the guest
 * is requesting from the SVGABackdoorCapType enum should be placed in
 * the upper 16 bits of the backdoor command id (ECX).  On success the
 * the value of EBX will be set to BDOOR_MAGIC and EAX will be set to
 * the requested capability.  If the command is not supported then EBX
 * will be left unchanged and EAX will be set to -1.  Because it is
 * possible that -1 is the value of the requested cap the correct way
 * to check if the command was successful is to check if EBX was changed
 * to BDOOR_MAGIC making sure to initialize the register to something
 * else first.
 */

typedef enum {
   SVGABackdoorCapDeviceCaps = 0,
   SVGABackdoorCapFifoCaps = 1,
   SVGABackdoorCap3dHWVersion = 2,
   SVGABackdoorCapDeviceCaps2 = 3,
   SVGABackdoorCapMax = 4,
} SVGABackdoorCapType;


/*
 * FIFO register indices.
 *
 * The FIFO is a chunk of device memory mapped into guest physmem.  It
 * is always treated as 32-bit words.
 *
 * The guest driver gets to decide how to partition it between
 * - FIFO registers (there are always at least 4, specifying where the
 *   following data area is and how much data it contains; there may be
 *   more registers following these, depending on the FIFO protocol
 *   version in use)
 * - FIFO data, written by the guest and slurped out by the VMX.
 * These indices are 32-bit word offsets into the FIFO.
 */

enum {
   /*
    * Block 1 (basic registers): The originally defined FIFO registers.
    * These exist and are valid for all versions of the FIFO protocol.
    */

   SVGA_FIFO_MIN = 0,
   SVGA_FIFO_MAX,       /* The distance from MIN to MAX must be at least 10K */
   SVGA_FIFO_NEXT_CMD,
   SVGA_FIFO_STOP,

   /*
    * Block 2 (extended registers): Mandatory registers for the extended
    * FIFO.  These exist if the SVGA caps register includes
    * SVGA_CAP_EXTENDED_FIFO; some of them are valid only if their
    * associated capability bit is enabled.
    *
    * Note that when originally defined, SVGA_CAP_EXTENDED_FIFO implied
    * support only for (FIFO registers) CAPABILITIES, FLAGS, and FENCE.
    * This means that the guest has to test individually (in most cases
    * using FIFO caps) for the presence of registers after this; the VMX
    * can define "extended FIFO" to mean whatever it wants, and currently
    * won't enable it unless there's room for that set and much more.
    */

   SVGA_FIFO_CAPABILITIES = 4,
   SVGA_FIFO_FLAGS,
   /* Valid with SVGA_FIFO_CAP_FENCE: */
   SVGA_FIFO_FENCE,

   /*
    * Block 3a (optional extended registers): Additional registers for the
    * extended FIFO, whose presence isn't actually implied by
    * SVGA_CAP_EXTENDED_FIFO; these exist if SVGA_FIFO_MIN is high enough to
    * leave room for them.
    *
    * These in block 3a, the VMX currently considers mandatory for the
    * extended FIFO.
    */

   /* Valid if exists (i.e. if extended FIFO enabled): */
   SVGA_FIFO_3D_HWVERSION,       /* See SVGA3dHardwareVersion in svga3d_reg.h */
   /* Valid with SVGA_FIFO_CAP_PITCHLOCK: */
   SVGA_FIFO_PITCHLOCK,

   /* Valid with SVGA_FIFO_CAP_CURSOR_BYPASS_3: */
   SVGA_FIFO_CURSOR_ON,          /* Cursor bypass 3 show/hide register */
   SVGA_FIFO_CURSOR_X,           /* Cursor bypass 3 x register */
   SVGA_FIFO_CURSOR_Y,           /* Cursor bypass 3 y register */
   SVGA_FIFO_CURSOR_COUNT,       /* Incremented when any of the other 3 change */
   SVGA_FIFO_CURSOR_LAST_UPDATED,/* Last time the host updated the cursor */

   /* Valid with SVGA_FIFO_CAP_RESERVE: */
   SVGA_FIFO_RESERVED,           /* Bytes past NEXT_CMD with real contents */

   /*
    * Valid with SVGA_FIFO_CAP_SCREEN_OBJECT or SVGA_FIFO_CAP_SCREEN_OBJECT_2:
    *
    * By default this is SVGA_ID_INVALID, to indicate that the cursor
    * coordinates are specified relative to the virtual root. If this
    * is set to a specific screen ID, cursor position is reinterpreted
    * as a signed offset relative to that screen's origin.
    */
   SVGA_FIFO_CURSOR_SCREEN_ID,

   /*
    * Valid with SVGA_FIFO_CAP_DEAD
    *
    * An arbitrary value written by the host, drivers should not use it.
    */
   SVGA_FIFO_DEAD,

   /*
    * Valid with SVGA_FIFO_CAP_3D_HWVERSION_REVISED:
    *
    * Contains 3D HWVERSION (see SVGA3dHardwareVersion in svga3d_reg.h)
    * on platforms that can enforce graphics resource limits.
    */
   SVGA_FIFO_3D_HWVERSION_REVISED,

   /*
    * XXX: The gap here, up until SVGA_FIFO_3D_CAPS, can be used for new
    * registers, but this must be done carefully and with judicious use of
    * capability bits, since comparisons based on SVGA_FIFO_MIN aren't
    * enough to tell you whether the register exists: we've shipped drivers
    * and products that used SVGA_FIFO_3D_CAPS but didn't know about some of
    * the earlier ones.  The actual order of introduction was:
    * - PITCHLOCK
    * - 3D_CAPS
    * - CURSOR_* (cursor bypass 3)
    * - RESERVED
    * So, code that wants to know whether it can use any of the
    * aforementioned registers, or anything else added after PITCHLOCK and
    * before 3D_CAPS, needs to reason about something other than
    * SVGA_FIFO_MIN.
    */

   /*
    * 3D caps block space; valid with 3D hardware version >=
    * SVGA3D_HWVERSION_WS6_B1.
    */
   SVGA_FIFO_3D_CAPS      = 32,
   SVGA_FIFO_3D_CAPS_LAST = 32 + 255,

   /*
    * End of VMX's current definition of "extended-FIFO registers".
    * Registers before here are always enabled/disabled as a block; either
    * the extended FIFO is enabled and includes all preceding registers, or
    * it's disabled entirely.
    *
    * Block 3b (truly optional extended registers): Additional registers for
    * the extended FIFO, which the VMX already knows how to enable and
    * disable with correct granularity.
    *
    * Registers after here exist if and only if the guest SVGA driver
    * sets SVGA_FIFO_MIN high enough to leave room for them.
    */

   /* Valid if register exists: */
   SVGA_FIFO_GUEST_3D_HWVERSION, /* Guest driver's 3D version */
   SVGA_FIFO_FENCE_GOAL,         /* Matching target for SVGA_IRQFLAG_FENCE_GOAL */
   SVGA_FIFO_BUSY,               /* See "FIFO Synchronization Registers" */

   /*
    * Always keep this last.  This defines the maximum number of
    * registers we know about.  At power-on, this value is placed in
    * the SVGA_REG_MEM_REGS register, and we expect the guest driver
    * to allocate this much space in FIFO memory for registers.
    */
    SVGA_FIFO_NUM_REGS
};


/*
 * Definition of registers included in extended FIFO support.
 *
 * The guest SVGA driver gets to allocate the FIFO between registers
 * and data.  It must always allocate at least 4 registers, but old
 * drivers stopped there.
 *
 * The VMX will enable extended FIFO support if and only if the guest
 * left enough room for all registers defined as part of the mandatory
 * set for the extended FIFO.
 *
 * Note that the guest drivers typically allocate the FIFO only at
 * initialization time, not at mode switches, so it's likely that the
 * number of FIFO registers won't change without a reboot.
 *
 * All registers less than this value are guaranteed to be present if
 * svgaUser->fifo.extended is set. Any later registers must be tested
 * individually for compatibility at each use (in the VMX).
 *
 * This value is used only by the VMX, so it can change without
 * affecting driver compatibility; keep it that way?
 */
#define SVGA_FIFO_EXTENDED_MANDATORY_REGS  (SVGA_FIFO_3D_CAPS_LAST + 1)


/*
 * FIFO Synchronization Registers
 *
 *  This explains the relationship between the various FIFO
 *  sync-related registers in IOSpace and in FIFO space.
 *
 *  SVGA_REG_SYNC --
 *
 *       The SYNC register can be used in two different ways by the guest:
 *
 *         1. If the guest wishes to fully sync (drain) the FIFO,
 *            it will write once to SYNC then poll on the BUSY
 *            register. The FIFO is sync'ed once BUSY is zero.
 *
 *         2. If the guest wants to asynchronously wake up the host,
 *            it will write once to SYNC without polling on BUSY.
 *            Ideally it will do this after some new commands have
 *            been placed in the FIFO, and after reading a zero
 *            from SVGA_FIFO_BUSY.
 *
 *       (1) is the original behaviour that SYNC was designed to
 *       support.  Originally, a write to SYNC would implicitly
 *       trigger a read from BUSY. This causes us to synchronously
 *       process the FIFO.
 *
 *       This behaviour has since been changed so that writing SYNC
 *       will *not* implicitly cause a read from BUSY. Instead, it
 *       makes a channel call which asynchronously wakes up the MKS
 *       thread.
 *
 *       New guests can use this new behaviour to implement (2)
 *       efficiently. This lets guests get the host's attention
 *       without waiting for the MKS to poll, which gives us much
 *       better CPU utilization on SMP hosts and on UP hosts while
 *       we're blocked on the host GPU.
 *
 *       Old guests shouldn't notice the behaviour change. SYNC was
 *       never guaranteed to process the entire FIFO, since it was
 *       bounded to a particular number of CPU cycles. Old guests will
 *       still loop on the BUSY register until the FIFO is empty.
 *
 *       Writing to SYNC currently has the following side-effects:
 *
 *         - Sets SVGA_REG_BUSY to TRUE (in the monitor)
 *         - Asynchronously wakes up the MKS thread for FIFO processing
 *         - The value written to SYNC is recorded as a "reason", for
 *           stats purposes.
 *
 *       If SVGA_FIFO_BUSY is available, drivers are advised to only
 *       write to SYNC if SVGA_FIFO_BUSY is FALSE. Drivers should set
 *       SVGA_FIFO_BUSY to TRUE after writing to SYNC. The MKS will
 *       eventually set SVGA_FIFO_BUSY on its own, but this approach
 *       lets the driver avoid sending multiple asynchronous wakeup
 *       messages to the MKS thread.
 *
 *  SVGA_REG_BUSY --
 *
 *       This register is set to TRUE when SVGA_REG_SYNC is written,
 *       and it reads as FALSE when the FIFO has been completely
 *       drained.
 *
 *       Every read from this register causes us to synchronously
 *       process FIFO commands. There is no guarantee as to how many
 *       commands each read will process.
 *
 *       CPU time spent processing FIFO commands will be billed to
 *       the guest.
 *
 *       New drivers should avoid using this register unless they
 *       need to guarantee that the FIFO is completely drained. It
 *       is overkill for performing a sync-to-fence. Older drivers
 *       will use this register for any type of synchronization.
 *
 *  SVGA_FIFO_BUSY --
 *
 *       This register is a fast way for the guest driver to check
 *       whether the FIFO is already being processed. It reads and
 *       writes at normal RAM speeds, with no monitor intervention.
 *
 *       If this register reads as TRUE, the host is guaranteeing that
 *       any new commands written into the FIFO will be noticed before
 *       the MKS goes back to sleep.
 *
 *       If this register reads as FALSE, no such guarantee can be
 *       made.
 *
 *       The guest should use this register to quickly determine
 *       whether or not it needs to wake up the host. If the guest
 *       just wrote a command or group of commands that it would like
 *       the host to begin processing, it should:
 *
 *         1. Read SVGA_FIFO_BUSY. If it reads as TRUE, no further
 *            action is necessary.
 *
 *         2. Write TRUE to SVGA_FIFO_BUSY. This informs future guest
 *            code that we've already sent a SYNC to the host and we
 *            don't need to send a duplicate.
 *
 *         3. Write a reason to SVGA_REG_SYNC. This will send an
 *            asynchronous wakeup to the MKS thread.
 */


/*
 * FIFO Capabilities
 *
 *      Fence -- Fence register and command are supported
 *      Accel Front -- Front buffer only commands are supported
 *      Pitch Lock -- Pitch lock register is supported
 *      Video -- SVGA Video overlay units are supported
 *      Escape -- Escape command is supported
 *
 * XXX: Add longer descriptions for each capability, including a list
 *      of the new features that each capability provides.
 *
 * SVGA_FIFO_CAP_SCREEN_OBJECT --
 *
 *    Provides dynamic multi-screen rendering, for improved Unity and
 *    multi-monitor modes. With Screen Object, the guest can
 *    dynamically create and destroy 'screens', which can represent
 *    Unity windows or virtual monitors. Screen Object also provides
 *    strong guarantees that DMA operations happen only when
 *    guest-initiated. Screen Object deprecates the BAR1 guest
 *    framebuffer (GFB) and all commands that work only with the GFB.
 *
 *    New registers:
 *       FIFO_CURSOR_SCREEN_ID, VIDEO_DATA_GMRID, VIDEO_DST_SCREEN_ID
 *
 *    New 2D commands:
 *       DEFINE_SCREEN, DESTROY_SCREEN, DEFINE_GMRFB, BLIT_GMRFB_TO_SCREEN,
 *       BLIT_SCREEN_TO_GMRFB, ANNOTATION_FILL, ANNOTATION_COPY
 *
 *    New 3D commands:
 *       BLIT_SURFACE_TO_SCREEN
 *
 *    New guarantees:
 *
 *       - The host will not read or write guest memory, including the GFB,
 *         except when explicitly initiated by a DMA command.
 *
 *       - All DMA, including legacy DMA like UPDATE and PRESENT_READBACK,
 *         is guaranteed to complete before any subsequent FENCEs.
 *
 *       - All legacy commands which affect a Screen (UPDATE, PRESENT,
 *         PRESENT_READBACK) as well as new Screen blit commands will
 *         all behave consistently as blits, and memory will be read
 *         or written in FIFO order.
 *
 *         For example, if you PRESENT from one SVGA3D surface to multiple
 *         places on the screen, the data copied will always be from the
 *         SVGA3D surface at the time the PRESENT was issued in the FIFO.
 *         This was not necessarily true on devices without Screen Object.
 *
 *         This means that on devices that support Screen Object, the
 *         PRESENT_READBACK command should not be necessary unless you
 *         actually want to read back the results of 3D rendering into
 *         system memory. (And for that, the BLIT_SCREEN_TO_GMRFB
 *         command provides a strict superset of functionality.)
 *
 *       - When a screen is resized, either using Screen Object commands or
 *         legacy multimon registers, its contents are preserved.
 *
 * SVGA_FIFO_CAP_GMR2 --
 *
 *    Provides new commands to define and remap guest memory regions (GMR).
 *
 *    New 2D commands:
 *       DEFINE_GMR2, REMAP_GMR2.
 *
 * SVGA_FIFO_CAP_3D_HWVERSION_REVISED --
 *
 *    Indicates new register SVGA_FIFO_3D_HWVERSION_REVISED exists.
 *    This register may replace SVGA_FIFO_3D_HWVERSION on platforms
 *    that enforce graphics resource limits.  This allows the platform
 *    to clear SVGA_FIFO_3D_HWVERSION and disable 3D in legacy guest
 *    drivers that do not limit their resources.
 *
 *    Note this is an alias to SVGA_FIFO_CAP_GMR2 because these indicators
 *    are codependent (and thus we use a single capability bit).
 *
 * SVGA_FIFO_CAP_SCREEN_OBJECT_2 --
 *
 *    Modifies the DEFINE_SCREEN command to include a guest provided
 *    backing store in GMR memory and the bytesPerLine for the backing
 *    store.  This capability requires the use of a backing store when
 *    creating screen objects.  However if SVGA_FIFO_CAP_SCREEN_OBJECT
 *    is present then backing stores are optional.
 *
 * SVGA_FIFO_CAP_DEAD --
 *
 *    Drivers should not use this cap bit.  This cap bit can not be
 *    reused since some hosts already expose it.
 */

#define SVGA_FIFO_CAP_NONE                  0
#define SVGA_FIFO_CAP_FENCE             (1<<0)
#define SVGA_FIFO_CAP_ACCELFRONT        (1<<1)
#define SVGA_FIFO_CAP_PITCHLOCK         (1<<2)
#define SVGA_FIFO_CAP_VIDEO             (1<<3)
#define SVGA_FIFO_CAP_CURSOR_BYPASS_3   (1<<4)
#define SVGA_FIFO_CAP_ESCAPE            (1<<5)
#define SVGA_FIFO_CAP_RESERVE           (1<<6)
#define SVGA_FIFO_CAP_SCREEN_OBJECT     (1<<7)
#define SVGA_FIFO_CAP_GMR2              (1<<8)
#define SVGA_FIFO_CAP_3D_HWVERSION_REVISED  SVGA_FIFO_CAP_GMR2
#define SVGA_FIFO_CAP_SCREEN_OBJECT_2   (1<<9)
#define SVGA_FIFO_CAP_DEAD              (1<<10)


/*
 * FIFO Flags
 *
 *      Accel Front -- Driver should use front buffer only commands
 */

#define SVGA_FIFO_FLAG_NONE                 0
#define SVGA_FIFO_FLAG_ACCELFRONT       (1<<0)
#define SVGA_FIFO_FLAG_RESERVED        (1<<31) /* Internal use only */

/*
 * FIFO reservation sentinel value
 */

#define SVGA_FIFO_RESERVED_UNKNOWN      0xffffffff


/*
 * Video overlay support
 */

#define SVGA_NUM_OVERLAY_UNITS 32


/*
 * Video capabilities that the guest is currently using
 */

#define SVGA_VIDEO_FLAG_COLORKEY        0x0001


/*
 * Offsets for the video overlay registers
 */

enum {
   SVGA_VIDEO_ENABLED = 0,
   SVGA_VIDEO_FLAGS,
   SVGA_VIDEO_DATA_OFFSET,
   SVGA_VIDEO_FORMAT,
   SVGA_VIDEO_COLORKEY,
   SVGA_VIDEO_SIZE,          /* Deprecated */
   SVGA_VIDEO_WIDTH,
   SVGA_VIDEO_HEIGHT,
   SVGA_VIDEO_SRC_X,
   SVGA_VIDEO_SRC_Y,
   SVGA_VIDEO_SRC_WIDTH,
   SVGA_VIDEO_SRC_HEIGHT,
   SVGA_VIDEO_DST_X,         /* Signed int32 */
   SVGA_VIDEO_DST_Y,         /* Signed int32 */
   SVGA_VIDEO_DST_WIDTH,
   SVGA_VIDEO_DST_HEIGHT,
   SVGA_VIDEO_PITCH_1,
   SVGA_VIDEO_PITCH_2,
   SVGA_VIDEO_PITCH_3,
   SVGA_VIDEO_DATA_GMRID,    /* Optional, defaults to SVGA_GMR_FRAMEBUFFER */
   SVGA_VIDEO_DST_SCREEN_ID, /* Optional, defaults to virtual coords */
                             /* (SVGA_ID_INVALID) */
   SVGA_VIDEO_NUM_REGS
};


/*
 * SVGA Overlay Units
 *
 *      width and height relate to the entire source video frame.
 *      srcX, srcY, srcWidth and srcHeight represent subset of the source
 *      video frame to be displayed.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGAOverlayUnit {
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
   int32  dstX;
   int32  dstY;
   uint32 dstWidth;
   uint32 dstHeight;
   uint32 pitches[3];
   uint32 dataGMRId;
   uint32 dstScreenId;
}
#include "vmware_pack_end.h"
SVGAOverlayUnit;


/*
 * Guest display topology
 *
 * XXX: This structure is not part of the SVGA device's interface, and
 * doesn't really belong here.
 */
#define SVGA_INVALID_DISPLAY_ID ((uint32)-1)

typedef struct SVGADisplayTopology {
   uint16 displayId;
   uint16 isPrimary;
   uint32 width;
   uint32 height;
   uint32 positionX;
   uint32 positionY;
} SVGADisplayTopology;


/*
 * SVGAScreenObject --
 *
 *    This is a new way to represent a guest's multi-monitor screen or
 *    Unity window. Screen objects are only supported if the
 *    SVGA_FIFO_CAP_SCREEN_OBJECT capability bit is set.
 *
 *    If Screen Objects are supported, they can be used to fully
 *    replace the functionality provided by the framebuffer registers
 *    (SVGA_REG_WIDTH, HEIGHT, etc.) and by SVGA_CAP_DISPLAY_TOPOLOGY.
 *
 *    The screen object is a struct with guaranteed binary
 *    compatibility. New flags can be added, and the struct may grow,
 *    but existing fields must retain their meaning.
 *
 *    Added with SVGA_FIFO_CAP_SCREEN_OBJECT_2 are required fields of
 *    a SVGAGuestPtr that is used to back the screen contents.  This
 *    memory must come from the GFB.  The guest is not allowed to
 *    access the memory and doing so will have undefined results.  The
 *    backing store is required to be page aligned and the size is
 *    padded to the next page boundry.  The number of pages is:
 *       (bytesPerLine * size.width * 4 + PAGE_SIZE - 1) / PAGE_SIZE
 *
 *    The pitch in the backingStore is required to be at least large
 *    enough to hold a 32bbp scanline.  It is recommended that the
 *    driver pad bytesPerLine for a potential performance win.
 *
 *    The cloneCount field is treated as a hint from the guest that
 *    the user wants this display to be cloned, countCount times.  A
 *    value of zero means no cloning should happen.
 */

#define SVGA_SCREEN_MUST_BE_SET     (1 << 0)
#define SVGA_SCREEN_HAS_ROOT SVGA_SCREEN_MUST_BE_SET /* Deprecated */
#define SVGA_SCREEN_IS_PRIMARY      (1 << 1)
#define SVGA_SCREEN_FULLSCREEN_HINT (1 << 2)

/*
 * Added with SVGA_FIFO_CAP_SCREEN_OBJECT_2.  When the screen is
 * deactivated the base layer is defined to lose all contents and
 * become black.  When a screen is deactivated the backing store is
 * optional.  When set backingPtr and bytesPerLine will be ignored.
 */
#define SVGA_SCREEN_DEACTIVATE  (1 << 3)

/*
 * Added with SVGA_FIFO_CAP_SCREEN_OBJECT_2.  When this flag is set
 * the screen contents will be outputted as all black to the user
 * though the base layer contents is preserved.  The screen base layer
 * can still be read and written to like normal though the no visible
 * effect will be seen by the user.  When the flag is changed the
 * screen will be blanked or redrawn to the current contents as needed
 * without any extra commands from the driver.  This flag only has an
 * effect when the screen is not deactivated.
 */
#define SVGA_SCREEN_BLANKING (1 << 4)

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 structSize;   /* sizeof(SVGAScreenObject) */
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

   /*
    * Added and required by SVGA_FIFO_CAP_SCREEN_OBJECT_2, optional
    * with SVGA_FIFO_CAP_SCREEN_OBJECT.
    */
   SVGAGuestImage backingStore;

   /*
    * The cloneCount field is treated as a hint from the guest that
    * the user wants this display to be cloned, cloneCount times.
    *
    * A value of zero means no cloning should happen.
    */
   uint32 cloneCount;
}
#include "vmware_pack_end.h"
SVGAScreenObject;


/*
 *  Commands in the command FIFO:
 *
 *  Command IDs defined below are used for the traditional 2D FIFO
 *  communication (not all commands are available for all versions of the
 *  SVGA FIFO protocol).
 *
 *  Note the holes in the command ID numbers: These commands have been
 *  deprecated, and the old IDs must not be reused.
 *
 *  Command IDs from 1000 to 2999 are reserved for use by the SVGA3D
 *  protocol.
 *
 *  Each command's parameters are described by the comments and
 *  structs below.
 */

typedef enum {
   SVGA_CMD_INVALID_CMD           = 0,
   SVGA_CMD_UPDATE                = 1,
   SVGA_CMD_RECT_COPY             = 3,
   SVGA_CMD_RECT_ROP_COPY         = 14,
   SVGA_CMD_DEFINE_CURSOR         = 19,
   SVGA_CMD_DEFINE_ALPHA_CURSOR   = 22,
   SVGA_CMD_UPDATE_VERBOSE        = 25,
   SVGA_CMD_FRONT_ROP_FILL        = 29,
   SVGA_CMD_FENCE                 = 30,
   SVGA_CMD_ESCAPE                = 33,
   SVGA_CMD_DEFINE_SCREEN         = 34,
   SVGA_CMD_DESTROY_SCREEN        = 35,
   SVGA_CMD_DEFINE_GMRFB          = 36,
   SVGA_CMD_BLIT_GMRFB_TO_SCREEN  = 37,
   SVGA_CMD_BLIT_SCREEN_TO_GMRFB  = 38,
   SVGA_CMD_ANNOTATION_FILL       = 39,
   SVGA_CMD_ANNOTATION_COPY       = 40,
   SVGA_CMD_DEFINE_GMR2           = 41,
   SVGA_CMD_REMAP_GMR2            = 42,
   SVGA_CMD_DEAD                  = 43,
   SVGA_CMD_DEAD_2                = 44,
   SVGA_CMD_NOP                   = 45,
   SVGA_CMD_NOP_ERROR             = 46,
   SVGA_CMD_MAX
} SVGAFifoCmdId;

#define SVGA_CMD_MAX_DATASIZE       (256 * 1024)
#define SVGA_CMD_MAX_ARGS           64


/*
 * SVGA_CMD_UPDATE --
 *
 *    This is a DMA transfer which copies from the Guest Framebuffer
 *    (GFB) at BAR1 + SVGA_REG_FB_OFFSET to any screens which
 *    intersect with the provided virtual rectangle.
 *
 *    This command does not support using arbitrary guest memory as a
 *    data source- it only works with the pre-defined GFB memory.
 *    This command also does not support signed virtual coordinates.
 *    If you have defined screens (using SVGA_CMD_DEFINE_SCREEN) with
 *    negative root x/y coordinates, the negative portion of those
 *    screens will not be reachable by this command.
 *
 *    This command is not necessary when using framebuffer
 *    traces. Traces are automatically enabled if the SVGA FIFO is
 *    disabled, and you may explicitly enable/disable traces using
 *    SVGA_REG_TRACES. With traces enabled, any write to the GFB will
 *    automatically act as if a subsequent SVGA_CMD_UPDATE was issued.
 *
 *    Traces and SVGA_CMD_UPDATE are the only supported ways to render
 *    pseudocolor screen updates. The newer Screen Object commands
 *    only support true color formats.
 *
 * Availability:
 *    Always available.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 x;
   uint32 y;
   uint32 width;
   uint32 height;
}
#include "vmware_pack_end.h"
SVGAFifoCmdUpdate;


/*
 * SVGA_CMD_RECT_COPY --
 *
 *    Perform a rectangular DMA transfer from one area of the GFB to
 *    another, and copy the result to any screens which intersect it.
 *
 * Availability:
 *    SVGA_CAP_RECT_COPY
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 srcX;
   uint32 srcY;
   uint32 destX;
   uint32 destY;
   uint32 width;
   uint32 height;
}
#include "vmware_pack_end.h"
SVGAFifoCmdRectCopy;


/*
 * SVGA_CMD_RECT_ROP_COPY --
 *
 *    Perform a rectangular DMA transfer from one area of the GFB to
 *    another, and copy the result to any screens which intersect it.
 *    The value of ROP may only be SVGA_ROP_COPY, and this command is
 *    only supported for backwards compatibility reasons.
 *
 * Availability:
 *    SVGA_CAP_RECT_COPY
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 srcX;
   uint32 srcY;
   uint32 destX;
   uint32 destY;
   uint32 width;
   uint32 height;
   uint32 rop;
}
#include "vmware_pack_end.h"
SVGAFifoCmdRectRopCopy;


/*
 * SVGA_CMD_DEFINE_CURSOR --
 *
 *    Provide a new cursor image, as an AND/XOR mask.
 *
 *    The recommended way to position the cursor overlay is by using
 *    the SVGA_FIFO_CURSOR_* registers, supported by the
 *    SVGA_FIFO_CAP_CURSOR_BYPASS_3 capability.
 *
 * Availability:
 *    SVGA_CAP_CURSOR
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 id;             /* Reserved, must be zero. */
   uint32 hotspotX;
   uint32 hotspotY;
   uint32 width;
   uint32 height;
   uint32 andMaskDepth;   /* Value must be 1 or equal to BITS_PER_PIXEL */
   uint32 xorMaskDepth;   /* Value must be 1 or equal to BITS_PER_PIXEL */
   /*
    * Followed by scanline data for AND mask, then XOR mask.
    * Each scanline is padded to a 32-bit boundary.
   */
}
#include "vmware_pack_end.h"
SVGAFifoCmdDefineCursor;


/*
 * SVGA_CMD_DEFINE_ALPHA_CURSOR --
 *
 *    Provide a new cursor image, in 32-bit BGRA format.
 *
 *    The recommended way to position the cursor overlay is by using
 *    the SVGA_FIFO_CURSOR_* registers, supported by the
 *    SVGA_FIFO_CAP_CURSOR_BYPASS_3 capability.
 *
 * Availability:
 *    SVGA_CAP_ALPHA_CURSOR
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 id;             /* Reserved, must be zero. */
   uint32 hotspotX;
   uint32 hotspotY;
   uint32 width;
   uint32 height;
   /* Followed by scanline data */
}
#include "vmware_pack_end.h"
SVGAFifoCmdDefineAlphaCursor;


/*
 * SVGA_CMD_UPDATE_VERBOSE --
 *
 *    Just like SVGA_CMD_UPDATE, but also provide a per-rectangle
 *    'reason' value, an opaque cookie which is used by internal
 *    debugging tools. Third party drivers should not use this
 *    command.
 *
 * Availability:
 *    SVGA_CAP_EXTENDED_FIFO
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 x;
   uint32 y;
   uint32 width;
   uint32 height;
   uint32 reason;
}
#include "vmware_pack_end.h"
SVGAFifoCmdUpdateVerbose;


/*
 * SVGA_CMD_FRONT_ROP_FILL --
 *
 *    This is a hint which tells the SVGA device that the driver has
 *    just filled a rectangular region of the GFB with a solid
 *    color. Instead of reading these pixels from the GFB, the device
 *    can assume that they all equal 'color'. This is primarily used
 *    for remote desktop protocols.
 *
 * Availability:
 *    SVGA_FIFO_CAP_ACCELFRONT
 */

#define  SVGA_ROP_COPY                    0x03

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 color;     /* In the same format as the GFB */
   uint32 x;
   uint32 y;
   uint32 width;
   uint32 height;
   uint32 rop;       /* Must be SVGA_ROP_COPY */
}
#include "vmware_pack_end.h"
SVGAFifoCmdFrontRopFill;


/*
 * SVGA_CMD_FENCE --
 *
 *    Insert a synchronization fence.  When the SVGA device reaches
 *    this command, it will copy the 'fence' value into the
 *    SVGA_FIFO_FENCE register. It will also compare the fence against
 *    SVGA_FIFO_FENCE_GOAL. If the fence matches the goal and the
 *    SVGA_IRQFLAG_FENCE_GOAL interrupt is enabled, the device will
 *    raise this interrupt.
 *
 * Availability:
 *    SVGA_FIFO_FENCE for this command,
 *    SVGA_CAP_IRQMASK for SVGA_FIFO_FENCE_GOAL.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 fence;
}
#include "vmware_pack_end.h"
SVGAFifoCmdFence;


/*
 * SVGA_CMD_ESCAPE --
 *
 *    Send an extended or vendor-specific variable length command.
 *    This is used for video overlay, third party plugins, and
 *    internal debugging tools. See svga_escape.h
 *
 * Availability:
 *    SVGA_FIFO_CAP_ESCAPE
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 nsid;
   uint32 size;
   /* followed by 'size' bytes of data */
}
#include "vmware_pack_end.h"
SVGAFifoCmdEscape;


/*
 * SVGA_CMD_DEFINE_SCREEN --
 *
 *    Define or redefine an SVGAScreenObject. See the description of
 *    SVGAScreenObject above.  The video driver is responsible for
 *    generating new screen IDs. They should be small positive
 *    integers. The virtual device will have an implementation
 *    specific upper limit on the number of screen IDs
 *    supported. Drivers are responsible for recycling IDs. The first
 *    valid ID is zero.
 *
 *    - Interaction with other registers:
 *
 *    For backwards compatibility, when the GFB mode registers (WIDTH,
 *    HEIGHT, PITCHLOCK, BITS_PER_PIXEL) are modified, the SVGA device
 *    deletes all screens other than screen #0, and redefines screen
 *    #0 according to the specified mode. Drivers that use
 *    SVGA_CMD_DEFINE_SCREEN should destroy or redefine screen #0.
 *
 *    If you use screen objects, do not use the legacy multi-mon
 *    registers (SVGA_REG_NUM_GUEST_DISPLAYS, SVGA_REG_DISPLAY_*).
 *
 * Availability:
 *    SVGA_FIFO_CAP_SCREEN_OBJECT or SVGA_FIFO_CAP_SCREEN_OBJECT_2
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAScreenObject screen;   /* Variable-length according to version */
}
#include "vmware_pack_end.h"
SVGAFifoCmdDefineScreen;


/*
 * SVGA_CMD_DESTROY_SCREEN --
 *
 *    Destroy an SVGAScreenObject. Its ID is immediately available for
 *    re-use.
 *
 * Availability:
 *    SVGA_FIFO_CAP_SCREEN_OBJECT or SVGA_FIFO_CAP_SCREEN_OBJECT_2
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 screenId;
}
#include "vmware_pack_end.h"
SVGAFifoCmdDestroyScreen;


/*
 * SVGA_CMD_DEFINE_GMRFB --
 *
 *    This command sets a piece of SVGA device state called the
 *    Guest Memory Region Framebuffer, or GMRFB. The GMRFB is a
 *    piece of light-weight state which identifies the location and
 *    format of an image in guest memory or in BAR1. The GMRFB has
 *    an arbitrary size, and it doesn't need to match the geometry
 *    of the GFB or any screen object.
 *
 *    The GMRFB can be redefined as often as you like. You could
 *    always use the same GMRFB, you could redefine it before
 *    rendering from a different guest screen, or you could even
 *    redefine it before every blit.
 *
 *    There are multiple ways to use this command. The simplest way is
 *    to use it to move the framebuffer either to elsewhere in the GFB
 *    (BAR1) memory region, or to a user-defined GMR. This lets a
 *    driver use a framebuffer allocated entirely out of normal system
 *    memory, which we encourage.
 *
 *    Another way to use this command is to set up a ring buffer of
 *    updates in GFB memory. If a driver wants to ensure that no
 *    frames are skipped by the SVGA device, it is important that the
 *    driver not modify the source data for a blit until the device is
 *    done processing the command. One efficient way to accomplish
 *    this is to use a ring of small DMA buffers. Each buffer is used
 *    for one blit, then we move on to the next buffer in the
 *    ring. The FENCE mechanism is used to protect each buffer from
 *    re-use until the device is finished with that buffer's
 *    corresponding blit.
 *
 *    This command does not affect the meaning of SVGA_CMD_UPDATE.
 *    UPDATEs always occur from the legacy GFB memory area. This
 *    command has no support for pseudocolor GMRFBs. Currently only
 *    true-color 15, 16, and 24-bit depths are supported. Future
 *    devices may expose capabilities for additional framebuffer
 *    formats.
 *
 *    The default GMRFB value is undefined. Drivers must always send
 *    this command at least once before performing any blit from the
 *    GMRFB.
 *
 * Availability:
 *    SVGA_FIFO_CAP_SCREEN_OBJECT or SVGA_FIFO_CAP_SCREEN_OBJECT_2
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAGuestPtr        ptr;
   uint32              bytesPerLine;
   SVGAGMRImageFormat  format;
}
#include "vmware_pack_end.h"
SVGAFifoCmdDefineGMRFB;


/*
 * SVGA_CMD_BLIT_GMRFB_TO_SCREEN --
 *
 *    This is a guest-to-host blit. It performs a DMA operation to
 *    copy a rectangular region of pixels from the current GMRFB to
 *    a ScreenObject.
 *
 *    The destination coordinate may be specified relative to a
 *    screen's origin.  The provided screen ID must be valid.
 *
 *    The SVGA device is guaranteed to finish reading from the GMRFB
 *    by the time any subsequent FENCE commands are reached.
 *
 *    This command consumes an annotation. See the
 *    SVGA_CMD_ANNOTATION_* commands for details.
 *
 * Availability:
 *    SVGA_FIFO_CAP_SCREEN_OBJECT or SVGA_FIFO_CAP_SCREEN_OBJECT_2
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGASignedPoint  srcOrigin;
   SVGASignedRect   destRect;
   uint32           destScreenId;
}
#include "vmware_pack_end.h"
SVGAFifoCmdBlitGMRFBToScreen;


/*
 * SVGA_CMD_BLIT_SCREEN_TO_GMRFB --
 *
 *    This is a host-to-guest blit. It performs a DMA operation to
 *    copy a rectangular region of pixels from a single ScreenObject
 *    back to the current GMRFB.
 *
 *    The source coordinate is specified relative to a screen's
 *    origin.  The provided screen ID must be valid. If any parameters
 *    are invalid, the resulting pixel values are undefined.
 *
 *    The SVGA device is guaranteed to finish writing to the GMRFB by
 *    the time any subsequent FENCE commands are reached.
 *
 * Availability:
 *    SVGA_FIFO_CAP_SCREEN_OBJECT or SVGA_FIFO_CAP_SCREEN_OBJECT_2
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGASignedPoint  destOrigin;
   SVGASignedRect   srcRect;
   uint32           srcScreenId;
}
#include "vmware_pack_end.h"
SVGAFifoCmdBlitScreenToGMRFB;


/*
 * SVGA_CMD_ANNOTATION_FILL --
 *
 *    The annotation commands have been deprecated, should not be used
 *    by new drivers.  They used to provide performance hints to the SVGA
 *    device about the content of screen updates, but newer SVGA devices
 *    ignore these.
 *
 * Availability:
 *    SVGA_FIFO_CAP_SCREEN_OBJECT or SVGA_FIFO_CAP_SCREEN_OBJECT_2
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAColorBGRX  color;
}
#include "vmware_pack_end.h"
SVGAFifoCmdAnnotationFill;


/*
 * SVGA_CMD_ANNOTATION_COPY --
 *
 *    The annotation commands have been deprecated, should not be used
 *    by new drivers.  They used to provide performance hints to the SVGA
 *    device about the content of screen updates, but newer SVGA devices
 *    ignore these.
 *
 * Availability:
 *    SVGA_FIFO_CAP_SCREEN_OBJECT or SVGA_FIFO_CAP_SCREEN_OBJECT_2
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGASignedPoint  srcOrigin;
   uint32           srcScreenId;
}
#include "vmware_pack_end.h"
SVGAFifoCmdAnnotationCopy;


/*
 * SVGA_CMD_DEFINE_GMR2 --
 *
 *    Define guest memory region v2.  See the description of GMRs above.
 *
 * Availability:
 *    SVGA_CAP_GMR2
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 gmrId;
   uint32 numPages;
}
#include "vmware_pack_end.h"
SVGAFifoCmdDefineGMR2;


/*
 * SVGA_CMD_REMAP_GMR2 --
 *
 *    Remap guest memory region v2.  See the description of GMRs above.
 *
 *    This command allows guest to modify a portion of an existing GMR by
 *    invalidating it or reassigning it to different guest physical pages.
 *    The pages are identified by physical page number (PPN).  The pages
 *    are assumed to be pinned and valid for DMA operations.
 *
 *    Description of command flags:
 *
 *    SVGA_REMAP_GMR2_VIA_GMR: If enabled, references a PPN list in a GMR.
 *       The PPN list must not overlap with the remap region (this can be
 *       handled trivially by referencing a separate GMR).  If flag is
 *       disabled, PPN list is appended to SVGARemapGMR command.
 *
 *    SVGA_REMAP_GMR2_PPN64: If set, PPN list is in PPN64 format, otherwise
 *       it is in PPN32 format.
 *
 *    SVGA_REMAP_GMR2_SINGLE_PPN: If set, PPN list contains a single entry.
 *       A single PPN can be used to invalidate a portion of a GMR or
 *       map it to to a single guest scratch page.
 *
 * Availability:
 *    SVGA_CAP_GMR2
 */

typedef enum {
   SVGA_REMAP_GMR2_PPN32         = 0,
   SVGA_REMAP_GMR2_VIA_GMR       = (1 << 0),
   SVGA_REMAP_GMR2_PPN64         = (1 << 1),
   SVGA_REMAP_GMR2_SINGLE_PPN    = (1 << 2),
} SVGARemapGMR2Flags;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 gmrId;
   SVGARemapGMR2Flags flags;
   uint32 offsetPages; /* offset in pages to begin remap */
   uint32 numPages; /* number of pages to remap */
   /*
    * Followed by additional data depending on SVGARemapGMR2Flags.
    *
    * If flag SVGA_REMAP_GMR2_VIA_GMR is set, single SVGAGuestPtr follows.
    * Otherwise an array of page descriptors in PPN32 or PPN64 format
    * (according to flag SVGA_REMAP_GMR2_PPN64) follows.  If flag
    * SVGA_REMAP_GMR2_SINGLE_PPN is set, array contains a single entry.
    */
}
#include "vmware_pack_end.h"
SVGAFifoCmdRemapGMR2;


/*
 * Size of SVGA device memory such as frame buffer and FIFO.
 */
#define SVGA_VRAM_MIN_SIZE             (4 * 640 * 480) /* bytes */
#define SVGA_VRAM_MIN_SIZE_3D       (16 * 1024 * 1024)
#define SVGA_VRAM_MAX_SIZE         (128 * 1024 * 1024)
#define SVGA_MEMORY_SIZE_MAX      (1024 * 1024 * 1024)
#define SVGA_FIFO_SIZE_MAX           (2 * 1024 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_MIN       (32 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_MAX       (2 * 1024 * 1024)
#define SVGA_GRAPHICS_MEMORY_KB_DEFAULT   (256 * 1024)

#define SVGA_VRAM_SIZE_W2K          (64 * 1024 * 1024) /* 64 MB */

#if defined(VMX86_SERVER)
#define SVGA_VRAM_SIZE               (4 * 1024 * 1024)
#define SVGA_VRAM_SIZE_3D           (64 * 1024 * 1024)
#define SVGA_FIFO_SIZE                    (256 * 1024)
#define SVGA_FIFO_SIZE_3D                 (516 * 1024)
#define SVGA_MEMORY_SIZE_DEFAULT   (160 * 1024 * 1024)
#define SVGA_AUTODETECT_DEFAULT                  FALSE
#else
#define SVGA_VRAM_SIZE              (16 * 1024 * 1024)
#define SVGA_VRAM_SIZE_3D           SVGA_VRAM_MAX_SIZE
#define SVGA_FIFO_SIZE               (2 * 1024 * 1024)
#define SVGA_FIFO_SIZE_3D               SVGA_FIFO_SIZE
#define SVGA_MEMORY_SIZE_DEFAULT   (768 * 1024 * 1024)
#define SVGA_AUTODETECT_DEFAULT                   TRUE
#endif

#define SVGA_FIFO_SIZE_GBOBJECTS          (256 * 1024)
#define SVGA_VRAM_SIZE_GBOBJECTS     (4 * 1024 * 1024)

#endif
