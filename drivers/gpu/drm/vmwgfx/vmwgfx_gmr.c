// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2015 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/ttm/ttm_bo_driver.h>

#include "vmwgfx_drv.h"

#define VMW_PPN_SIZE (sizeof(unsigned long))
/* A future safe maximum remap size. */
#define VMW_PPN_PER_REMAP ((31 * 1024) / VMW_PPN_SIZE)
#define DMA_ADDR_INVALID ((dma_addr_t) 0)
#define DMA_PAGE_INVALID 0UL

static int vmw_gmr2_bind(struct vmw_private *dev_priv,
			 struct vmw_piter *iter,
			 unsigned long num_pages,
			 int gmr_id)
{
	SVGAFifoCmdDefineGMR2 define_cmd;
	SVGAFifoCmdRemapGMR2 remap_cmd;
	uint32_t *cmd;
	uint32_t *cmd_orig;
	uint32_t define_size = sizeof(define_cmd) + sizeof(*cmd);
	uint32_t remap_num = num_pages / VMW_PPN_PER_REMAP + ((num_pages % VMW_PPN_PER_REMAP) > 0);
	uint32_t remap_size = VMW_PPN_SIZE * num_pages + (sizeof(remap_cmd) + sizeof(*cmd)) * remap_num;
	uint32_t remap_pos = 0;
	uint32_t cmd_size = define_size + remap_size;
	uint32_t i;

	cmd_orig = cmd = VMW_FIFO_RESERVE(dev_priv, cmd_size);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	define_cmd.gmrId = gmr_id;
	define_cmd.numPages = num_pages;

	*cmd++ = SVGA_CMD_DEFINE_GMR2;
	memcpy(cmd, &define_cmd, sizeof(define_cmd));
	cmd += sizeof(define_cmd) / sizeof(*cmd);

	/*
	 * Need to split the command if there are too many
	 * pages that goes into the gmr.
	 */

	remap_cmd.gmrId = gmr_id;
	remap_cmd.flags = (VMW_PPN_SIZE > sizeof(*cmd)) ?
		SVGA_REMAP_GMR2_PPN64 : SVGA_REMAP_GMR2_PPN32;

	while (num_pages > 0) {
		unsigned long nr = min(num_pages, (unsigned long)VMW_PPN_PER_REMAP);

		remap_cmd.offsetPages = remap_pos;
		remap_cmd.numPages = nr;

		*cmd++ = SVGA_CMD_REMAP_GMR2;
		memcpy(cmd, &remap_cmd, sizeof(remap_cmd));
		cmd += sizeof(remap_cmd) / sizeof(*cmd);

		for (i = 0; i < nr; ++i) {
			if (VMW_PPN_SIZE <= 4)
				*cmd = vmw_piter_dma_addr(iter) >> PAGE_SHIFT;
			else
				*((uint64_t *)cmd) = vmw_piter_dma_addr(iter) >>
					PAGE_SHIFT;

			cmd += VMW_PPN_SIZE / sizeof(*cmd);
			vmw_piter_next(iter);
		}

		num_pages -= nr;
		remap_pos += nr;
	}

	BUG_ON(cmd != cmd_orig + cmd_size / sizeof(*cmd));

	vmw_fifo_commit(dev_priv, cmd_size);

	return 0;
}

static void vmw_gmr2_unbind(struct vmw_private *dev_priv,
			    int gmr_id)
{
	SVGAFifoCmdDefineGMR2 define_cmd;
	uint32_t define_size = sizeof(define_cmd) + 4;
	uint32_t *cmd;

	cmd = VMW_FIFO_RESERVE(dev_priv, define_size);
	if (unlikely(cmd == NULL))
		return;

	define_cmd.gmrId = gmr_id;
	define_cmd.numPages = 0;

	*cmd++ = SVGA_CMD_DEFINE_GMR2;
	memcpy(cmd, &define_cmd, sizeof(define_cmd));

	vmw_fifo_commit(dev_priv, define_size);
}


int vmw_gmr_bind(struct vmw_private *dev_priv,
		 const struct vmw_sg_table *vsgt,
		 unsigned long num_pages,
		 int gmr_id)
{
	struct vmw_piter data_iter;

	vmw_piter_start(&data_iter, vsgt, 0);

	if (unlikely(!vmw_piter_next(&data_iter)))
		return 0;

	if (unlikely(!(dev_priv->capabilities & SVGA_CAP_GMR2)))
		return -EINVAL;

	return vmw_gmr2_bind(dev_priv, &data_iter, num_pages, gmr_id);
}


void vmw_gmr_unbind(struct vmw_private *dev_priv, int gmr_id)
{
	if (likely(dev_priv->capabilities & SVGA_CAP_GMR2))
		vmw_gmr2_unbind(dev_priv, gmr_id);
}
