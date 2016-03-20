/**************************************************************************
 *
 * Copyright © 2009-2014 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * This file contains virtual hardware defines for kernel space.
 */

#ifndef _VMWGFX_REG_H_
#define _VMWGFX_REG_H_

#include <linux/types.h>

#define VMWGFX_INDEX_PORT     0x0
#define VMWGFX_VALUE_PORT     0x1
#define VMWGFX_IRQSTATUS_PORT 0x8

struct svga_guest_mem_descriptor {
	u32 ppn;
	u32 num_pages;
};

struct svga_fifo_cmd_fence {
	u32 fence;
};

#define SVGA_SYNC_GENERIC         1
#define SVGA_SYNC_FIFOFULL        2

#include "device_include/svga3d_reg.h"

#endif
