/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright 2021 VMware, Inc., Palo Alto, CA., USA
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

#ifndef _VMWGFX_DEVCAPS_H_
#define _VMWGFX_DEVCAPS_H_

#include "vmwgfx_drv.h"

#include "device_include/svga_reg.h"

int vmw_devcaps_create(struct vmw_private *vmw);
void vmw_devcaps_destroy(struct vmw_private *vmw);
uint32_t vmw_devcaps_size(const struct vmw_private *vmw, bool gb_aware);
int vmw_devcaps_copy(struct vmw_private *vmw, bool gb_aware,
		     void *dst, uint32_t dst_size);

static inline uint32_t vmw_devcap_get(struct vmw_private *vmw,
				      uint32_t devcap)
{
	bool gb_objects = !!(vmw->capabilities & SVGA_CAP_GBOBJECTS);
	if (gb_objects)
		return vmw->devcaps[devcap];
	return 0;
}

#endif
