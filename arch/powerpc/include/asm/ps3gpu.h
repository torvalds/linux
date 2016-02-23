/*
 *  PS3 GPU declarations.
 *
 *  Copyright 2009 Sony Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ASM_POWERPC_PS3GPU_H
#define _ASM_POWERPC_PS3GPU_H

#include <linux/mutex.h>

#include <asm/lv1call.h>


#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC	0x101
#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP	0x102

#define L1GPU_CONTEXT_ATTRIBUTE_FB_SETUP	0x600
#define L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT		0x601
#define L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT_SYNC	0x602
#define L1GPU_CONTEXT_ATTRIBUTE_FB_CLOSE	0x603

#define L1GPU_FB_BLIT_WAIT_FOR_COMPLETION	(1ULL << 32)

#define L1GPU_DISPLAY_SYNC_HSYNC		1
#define L1GPU_DISPLAY_SYNC_VSYNC		2


/* mutex synchronizing GPU accesses and video mode changes */
extern struct mutex ps3_gpu_mutex;


static inline int lv1_gpu_display_sync(u64 context_handle, u64 head,
				       u64 ddr_offset)
{
	return lv1_gpu_context_attribute(context_handle,
					 L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC,
					 head, ddr_offset, 0, 0);
}

static inline int lv1_gpu_display_flip(u64 context_handle, u64 head,
				       u64 ddr_offset)
{
	return lv1_gpu_context_attribute(context_handle,
					 L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP,
					 head, ddr_offset, 0, 0);
}

static inline int lv1_gpu_fb_setup(u64 context_handle, u64 xdr_lpar,
				   u64 xdr_size, u64 ioif_offset)
{
	return lv1_gpu_context_attribute(context_handle,
					 L1GPU_CONTEXT_ATTRIBUTE_FB_SETUP,
					 xdr_lpar, xdr_size, ioif_offset, 0);
}

static inline int lv1_gpu_fb_blit(u64 context_handle, u64 ddr_offset,
				  u64 ioif_offset, u64 sync_width, u64 pitch)
{
	return lv1_gpu_context_attribute(context_handle,
					 L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT,
					 ddr_offset, ioif_offset, sync_width,
					 pitch);
}

static inline int lv1_gpu_fb_close(u64 context_handle)
{
	return lv1_gpu_context_attribute(context_handle,
					 L1GPU_CONTEXT_ATTRIBUTE_FB_CLOSE, 0,
					 0, 0, 0);
}

#endif /* _ASM_POWERPC_PS3GPU_H */
