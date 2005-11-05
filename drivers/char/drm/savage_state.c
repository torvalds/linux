/* savage_state.c -- State and drawing support for Savage
 *
 * Copyright 2004  Felix Kuehling
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL FELIX KUEHLING BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "drmP.h"
#include "savage_drm.h"
#include "savage_drv.h"

void savage_emit_clip_rect_s3d(drm_savage_private_t * dev_priv,
			       drm_clip_rect_t * pbox)
{
	uint32_t scstart = dev_priv->state.s3d.new_scstart;
	uint32_t scend = dev_priv->state.s3d.new_scend;
	scstart = (scstart & ~SAVAGE_SCISSOR_MASK_S3D) |
	    ((uint32_t) pbox->x1 & 0x000007ff) |
	    (((uint32_t) pbox->y1 << 16) & 0x07ff0000);
	scend = (scend & ~SAVAGE_SCISSOR_MASK_S3D) |
	    (((uint32_t) pbox->x2 - 1) & 0x000007ff) |
	    ((((uint32_t) pbox->y2 - 1) << 16) & 0x07ff0000);
	if (scstart != dev_priv->state.s3d.scstart ||
	    scend != dev_priv->state.s3d.scend) {
		DMA_LOCALS;
		BEGIN_DMA(4);
		DMA_WRITE(BCI_CMD_WAIT | BCI_CMD_WAIT_3D);
		DMA_SET_REGISTERS(SAVAGE_SCSTART_S3D, 2);
		DMA_WRITE(scstart);
		DMA_WRITE(scend);
		dev_priv->state.s3d.scstart = scstart;
		dev_priv->state.s3d.scend = scend;
		dev_priv->waiting = 1;
		DMA_COMMIT();
	}
}

void savage_emit_clip_rect_s4(drm_savage_private_t * dev_priv,
			      drm_clip_rect_t * pbox)
{
	uint32_t drawctrl0 = dev_priv->state.s4.new_drawctrl0;
	uint32_t drawctrl1 = dev_priv->state.s4.new_drawctrl1;
	drawctrl0 = (drawctrl0 & ~SAVAGE_SCISSOR_MASK_S4) |
	    ((uint32_t) pbox->x1 & 0x000007ff) |
	    (((uint32_t) pbox->y1 << 12) & 0x00fff000);
	drawctrl1 = (drawctrl1 & ~SAVAGE_SCISSOR_MASK_S4) |
	    (((uint32_t) pbox->x2 - 1) & 0x000007ff) |
	    ((((uint32_t) pbox->y2 - 1) << 12) & 0x00fff000);
	if (drawctrl0 != dev_priv->state.s4.drawctrl0 ||
	    drawctrl1 != dev_priv->state.s4.drawctrl1) {
		DMA_LOCALS;
		BEGIN_DMA(4);
		DMA_WRITE(BCI_CMD_WAIT | BCI_CMD_WAIT_3D);
		DMA_SET_REGISTERS(SAVAGE_DRAWCTRL0_S4, 2);
		DMA_WRITE(drawctrl0);
		DMA_WRITE(drawctrl1);
		dev_priv->state.s4.drawctrl0 = drawctrl0;
		dev_priv->state.s4.drawctrl1 = drawctrl1;
		dev_priv->waiting = 1;
		DMA_COMMIT();
	}
}

static int savage_verify_texaddr(drm_savage_private_t * dev_priv, int unit,
				 uint32_t addr)
{
	if ((addr & 6) != 2) {	/* reserved bits */
		DRM_ERROR("bad texAddr%d %08x (reserved bits)\n", unit, addr);
		return DRM_ERR(EINVAL);
	}
	if (!(addr & 1)) {	/* local */
		addr &= ~7;
		if (addr < dev_priv->texture_offset ||
		    addr >= dev_priv->texture_offset + dev_priv->texture_size) {
			DRM_ERROR
			    ("bad texAddr%d %08x (local addr out of range)\n",
			     unit, addr);
			return DRM_ERR(EINVAL);
		}
	} else {		/* AGP */
		if (!dev_priv->agp_textures) {
			DRM_ERROR("bad texAddr%d %08x (AGP not available)\n",
				  unit, addr);
			return DRM_ERR(EINVAL);
		}
		addr &= ~7;
		if (addr < dev_priv->agp_textures->offset ||
		    addr >= (dev_priv->agp_textures->offset +
			     dev_priv->agp_textures->size)) {
			DRM_ERROR
			    ("bad texAddr%d %08x (AGP addr out of range)\n",
			     unit, addr);
			return DRM_ERR(EINVAL);
		}
	}
	return 0;
}

#define SAVE_STATE(reg,where)			\
	if(start <= reg && start+count > reg)	\
		DRM_GET_USER_UNCHECKED(dev_priv->state.where, &regs[reg-start])
#define SAVE_STATE_MASK(reg,where,mask) do {			\
	if(start <= reg && start+count > reg) {			\
		uint32_t tmp;					\
		DRM_GET_USER_UNCHECKED(tmp, &regs[reg-start]);	\
		dev_priv->state.where = (tmp & (mask)) |	\
			(dev_priv->state.where & ~(mask));	\
	}							\
} while (0)
static int savage_verify_state_s3d(drm_savage_private_t * dev_priv,
				   unsigned int start, unsigned int count,
				   const uint32_t __user * regs)
{
	if (start < SAVAGE_TEXPALADDR_S3D ||
	    start + count - 1 > SAVAGE_DESTTEXRWWATERMARK_S3D) {
		DRM_ERROR("invalid register range (0x%04x-0x%04x)\n",
			  start, start + count - 1);
		return DRM_ERR(EINVAL);
	}

	SAVE_STATE_MASK(SAVAGE_SCSTART_S3D, s3d.new_scstart,
			~SAVAGE_SCISSOR_MASK_S3D);
	SAVE_STATE_MASK(SAVAGE_SCEND_S3D, s3d.new_scend,
			~SAVAGE_SCISSOR_MASK_S3D);

	/* if any texture regs were changed ... */
	if (start <= SAVAGE_TEXCTRL_S3D &&
	    start + count > SAVAGE_TEXPALADDR_S3D) {
		/* ... check texture state */
		SAVE_STATE(SAVAGE_TEXCTRL_S3D, s3d.texctrl);
		SAVE_STATE(SAVAGE_TEXADDR_S3D, s3d.texaddr);
		if (dev_priv->state.s3d.texctrl & SAVAGE_TEXCTRL_TEXEN_MASK)
			return savage_verify_texaddr(dev_priv, 0,
						     dev_priv->state.s3d.
						     texaddr);
	}

	return 0;
}

static int savage_verify_state_s4(drm_savage_private_t * dev_priv,
				  unsigned int start, unsigned int count,
				  const uint32_t __user * regs)
{
	int ret = 0;

	if (start < SAVAGE_DRAWLOCALCTRL_S4 ||
	    start + count - 1 > SAVAGE_TEXBLENDCOLOR_S4) {
		DRM_ERROR("invalid register range (0x%04x-0x%04x)\n",
			  start, start + count - 1);
		return DRM_ERR(EINVAL);
	}

	SAVE_STATE_MASK(SAVAGE_DRAWCTRL0_S4, s4.new_drawctrl0,
			~SAVAGE_SCISSOR_MASK_S4);
	SAVE_STATE_MASK(SAVAGE_DRAWCTRL1_S4, s4.new_drawctrl1,
			~SAVAGE_SCISSOR_MASK_S4);

	/* if any texture regs were changed ... */
	if (start <= SAVAGE_TEXDESCR_S4 && start + count > SAVAGE_TEXPALADDR_S4) {
		/* ... check texture state */
		SAVE_STATE(SAVAGE_TEXDESCR_S4, s4.texdescr);
		SAVE_STATE(SAVAGE_TEXADDR0_S4, s4.texaddr0);
		SAVE_STATE(SAVAGE_TEXADDR1_S4, s4.texaddr1);
		if (dev_priv->state.s4.texdescr & SAVAGE_TEXDESCR_TEX0EN_MASK)
			ret |=
			    savage_verify_texaddr(dev_priv, 0,
						  dev_priv->state.s4.texaddr0);
		if (dev_priv->state.s4.texdescr & SAVAGE_TEXDESCR_TEX1EN_MASK)
			ret |=
			    savage_verify_texaddr(dev_priv, 1,
						  dev_priv->state.s4.texaddr1);
	}

	return ret;
}

#undef SAVE_STATE
#undef SAVE_STATE_MASK

static int savage_dispatch_state(drm_savage_private_t * dev_priv,
				 const drm_savage_cmd_header_t * cmd_header,
				 const uint32_t __user * regs)
{
	unsigned int count = cmd_header->state.count;
	unsigned int start = cmd_header->state.start;
	unsigned int count2 = 0;
	unsigned int bci_size;
	int ret;
	DMA_LOCALS;

	if (!count)
		return 0;

	if (DRM_VERIFYAREA_READ(regs, count * 4))
		return DRM_ERR(EFAULT);

	if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
		ret = savage_verify_state_s3d(dev_priv, start, count, regs);
		if (ret != 0)
			return ret;
		/* scissor regs are emitted in savage_dispatch_draw */
		if (start < SAVAGE_SCSTART_S3D) {
			if (start + count > SAVAGE_SCEND_S3D + 1)
				count2 = count - (SAVAGE_SCEND_S3D + 1 - start);
			if (start + count > SAVAGE_SCSTART_S3D)
				count = SAVAGE_SCSTART_S3D - start;
		} else if (start <= SAVAGE_SCEND_S3D) {
			if (start + count > SAVAGE_SCEND_S3D + 1) {
				count -= SAVAGE_SCEND_S3D + 1 - start;
				start = SAVAGE_SCEND_S3D + 1;
			} else
				return 0;
		}
	} else {
		ret = savage_verify_state_s4(dev_priv, start, count, regs);
		if (ret != 0)
			return ret;
		/* scissor regs are emitted in savage_dispatch_draw */
		if (start < SAVAGE_DRAWCTRL0_S4) {
			if (start + count > SAVAGE_DRAWCTRL1_S4 + 1)
				count2 =
				    count - (SAVAGE_DRAWCTRL1_S4 + 1 - start);
			if (start + count > SAVAGE_DRAWCTRL0_S4)
				count = SAVAGE_DRAWCTRL0_S4 - start;
		} else if (start <= SAVAGE_DRAWCTRL1_S4) {
			if (start + count > SAVAGE_DRAWCTRL1_S4 + 1) {
				count -= SAVAGE_DRAWCTRL1_S4 + 1 - start;
				start = SAVAGE_DRAWCTRL1_S4 + 1;
			} else
				return 0;
		}
	}

	bci_size = count + (count + 254) / 255 + count2 + (count2 + 254) / 255;

	if (cmd_header->state.global) {
		BEGIN_DMA(bci_size + 1);
		DMA_WRITE(BCI_CMD_WAIT | BCI_CMD_WAIT_3D);
		dev_priv->waiting = 1;
	} else {
		BEGIN_DMA(bci_size);
	}

	do {
		while (count > 0) {
			unsigned int n = count < 255 ? count : 255;
			DMA_SET_REGISTERS(start, n);
			DMA_COPY_FROM_USER(regs, n);
			count -= n;
			start += n;
			regs += n;
		}
		start += 2;
		regs += 2;
		count = count2;
		count2 = 0;
	} while (count);

	DMA_COMMIT();

	return 0;
}

static int savage_dispatch_dma_prim(drm_savage_private_t * dev_priv,
				    const drm_savage_cmd_header_t * cmd_header,
				    const drm_buf_t * dmabuf)
{
	unsigned char reorder = 0;
	unsigned int prim = cmd_header->prim.prim;
	unsigned int skip = cmd_header->prim.skip;
	unsigned int n = cmd_header->prim.count;
	unsigned int start = cmd_header->prim.start;
	unsigned int i;
	BCI_LOCALS;

	if (!dmabuf) {
		DRM_ERROR("called without dma buffers!\n");
		return DRM_ERR(EINVAL);
	}

	if (!n)
		return 0;

	switch (prim) {
	case SAVAGE_PRIM_TRILIST_201:
		reorder = 1;
		prim = SAVAGE_PRIM_TRILIST;
	case SAVAGE_PRIM_TRILIST:
		if (n % 3 != 0) {
			DRM_ERROR("wrong number of vertices %u in TRILIST\n",
				  n);
			return DRM_ERR(EINVAL);
		}
		break;
	case SAVAGE_PRIM_TRISTRIP:
	case SAVAGE_PRIM_TRIFAN:
		if (n < 3) {
			DRM_ERROR
			    ("wrong number of vertices %u in TRIFAN/STRIP\n",
			     n);
			return DRM_ERR(EINVAL);
		}
		break;
	default:
		DRM_ERROR("invalid primitive type %u\n", prim);
		return DRM_ERR(EINVAL);
	}

	if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
		if (skip != 0) {
			DRM_ERROR("invalid skip flags 0x%04x for DMA\n", skip);
			return DRM_ERR(EINVAL);
		}
	} else {
		unsigned int size = 10 - (skip & 1) - (skip >> 1 & 1) -
		    (skip >> 2 & 1) - (skip >> 3 & 1) - (skip >> 4 & 1) -
		    (skip >> 5 & 1) - (skip >> 6 & 1) - (skip >> 7 & 1);
		if (skip > SAVAGE_SKIP_ALL_S4 || size != 8) {
			DRM_ERROR("invalid skip flags 0x%04x for DMA\n", skip);
			return DRM_ERR(EINVAL);
		}
		if (reorder) {
			DRM_ERROR("TRILIST_201 used on Savage4 hardware\n");
			return DRM_ERR(EINVAL);
		}
	}

	if (start + n > dmabuf->total / 32) {
		DRM_ERROR("vertex indices (%u-%u) out of range (0-%u)\n",
			  start, start + n - 1, dmabuf->total / 32);
		return DRM_ERR(EINVAL);
	}

	/* Vertex DMA doesn't work with command DMA at the same time,
	 * so we use BCI_... to submit commands here. Flush buffered
	 * faked DMA first. */
	DMA_FLUSH();

	if (dmabuf->bus_address != dev_priv->state.common.vbaddr) {
		BEGIN_BCI(2);
		BCI_SET_REGISTERS(SAVAGE_VERTBUFADDR, 1);
		BCI_WRITE(dmabuf->bus_address | dev_priv->dma_type);
		dev_priv->state.common.vbaddr = dmabuf->bus_address;
	}
	if (S3_SAVAGE3D_SERIES(dev_priv->chipset) && dev_priv->waiting) {
		/* Workaround for what looks like a hardware bug. If a
		 * WAIT_3D_IDLE was emitted some time before the
		 * indexed drawing command then the engine will lock
		 * up. There are two known workarounds:
		 * WAIT_IDLE_EMPTY or emit at least 63 NOPs. */
		BEGIN_BCI(63);
		for (i = 0; i < 63; ++i)
			BCI_WRITE(BCI_CMD_WAIT);
		dev_priv->waiting = 0;
	}

	prim <<= 25;
	while (n != 0) {
		/* Can emit up to 255 indices (85 triangles) at once. */
		unsigned int count = n > 255 ? 255 : n;
		if (reorder) {
			/* Need to reorder indices for correct flat
			 * shading while preserving the clock sense
			 * for correct culling. Only on Savage3D. */
			int reorder[3] = { -1, -1, -1 };
			reorder[start % 3] = 2;

			BEGIN_BCI((count + 1 + 1) / 2);
			BCI_DRAW_INDICES_S3D(count, prim, start + 2);

			for (i = start + 1; i + 1 < start + count; i += 2)
				BCI_WRITE((i + reorder[i % 3]) |
					  ((i + 1 +
					    reorder[(i + 1) % 3]) << 16));
			if (i < start + count)
				BCI_WRITE(i + reorder[i % 3]);
		} else if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
			BEGIN_BCI((count + 1 + 1) / 2);
			BCI_DRAW_INDICES_S3D(count, prim, start);

			for (i = start + 1; i + 1 < start + count; i += 2)
				BCI_WRITE(i | ((i + 1) << 16));
			if (i < start + count)
				BCI_WRITE(i);
		} else {
			BEGIN_BCI((count + 2 + 1) / 2);
			BCI_DRAW_INDICES_S4(count, prim, skip);

			for (i = start; i + 1 < start + count; i += 2)
				BCI_WRITE(i | ((i + 1) << 16));
			if (i < start + count)
				BCI_WRITE(i);
		}

		start += count;
		n -= count;

		prim |= BCI_CMD_DRAW_CONT;
	}

	return 0;
}

static int savage_dispatch_vb_prim(drm_savage_private_t * dev_priv,
				   const drm_savage_cmd_header_t * cmd_header,
				   const uint32_t __user * vtxbuf,
				   unsigned int vb_size, unsigned int vb_stride)
{
	unsigned char reorder = 0;
	unsigned int prim = cmd_header->prim.prim;
	unsigned int skip = cmd_header->prim.skip;
	unsigned int n = cmd_header->prim.count;
	unsigned int start = cmd_header->prim.start;
	unsigned int vtx_size;
	unsigned int i;
	DMA_LOCALS;

	if (!n)
		return 0;

	switch (prim) {
	case SAVAGE_PRIM_TRILIST_201:
		reorder = 1;
		prim = SAVAGE_PRIM_TRILIST;
	case SAVAGE_PRIM_TRILIST:
		if (n % 3 != 0) {
			DRM_ERROR("wrong number of vertices %u in TRILIST\n",
				  n);
			return DRM_ERR(EINVAL);
		}
		break;
	case SAVAGE_PRIM_TRISTRIP:
	case SAVAGE_PRIM_TRIFAN:
		if (n < 3) {
			DRM_ERROR
			    ("wrong number of vertices %u in TRIFAN/STRIP\n",
			     n);
			return DRM_ERR(EINVAL);
		}
		break;
	default:
		DRM_ERROR("invalid primitive type %u\n", prim);
		return DRM_ERR(EINVAL);
	}

	if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
		if (skip > SAVAGE_SKIP_ALL_S3D) {
			DRM_ERROR("invalid skip flags 0x%04x\n", skip);
			return DRM_ERR(EINVAL);
		}
		vtx_size = 8;	/* full vertex */
	} else {
		if (skip > SAVAGE_SKIP_ALL_S4) {
			DRM_ERROR("invalid skip flags 0x%04x\n", skip);
			return DRM_ERR(EINVAL);
		}
		vtx_size = 10;	/* full vertex */
	}

	vtx_size -= (skip & 1) + (skip >> 1 & 1) +
	    (skip >> 2 & 1) + (skip >> 3 & 1) + (skip >> 4 & 1) +
	    (skip >> 5 & 1) + (skip >> 6 & 1) + (skip >> 7 & 1);

	if (vtx_size > vb_stride) {
		DRM_ERROR("vertex size greater than vb stride (%u > %u)\n",
			  vtx_size, vb_stride);
		return DRM_ERR(EINVAL);
	}

	if (start + n > vb_size / (vb_stride * 4)) {
		DRM_ERROR("vertex indices (%u-%u) out of range (0-%u)\n",
			  start, start + n - 1, vb_size / (vb_stride * 4));
		return DRM_ERR(EINVAL);
	}

	prim <<= 25;
	while (n != 0) {
		/* Can emit up to 255 vertices (85 triangles) at once. */
		unsigned int count = n > 255 ? 255 : n;
		if (reorder) {
			/* Need to reorder vertices for correct flat
			 * shading while preserving the clock sense
			 * for correct culling. Only on Savage3D. */
			int reorder[3] = { -1, -1, -1 };
			reorder[start % 3] = 2;

			BEGIN_DMA(count * vtx_size + 1);
			DMA_DRAW_PRIMITIVE(count, prim, skip);

			for (i = start; i < start + count; ++i) {
				unsigned int j = i + reorder[i % 3];
				DMA_COPY_FROM_USER(&vtxbuf[vb_stride * j],
						   vtx_size);
			}

			DMA_COMMIT();
		} else {
			BEGIN_DMA(count * vtx_size + 1);
			DMA_DRAW_PRIMITIVE(count, prim, skip);

			if (vb_stride == vtx_size) {
				DMA_COPY_FROM_USER(&vtxbuf[vb_stride * start],
						   vtx_size * count);
			} else {
				for (i = start; i < start + count; ++i) {
					DMA_COPY_FROM_USER(&vtxbuf
							   [vb_stride * i],
							   vtx_size);
				}
			}

			DMA_COMMIT();
		}

		start += count;
		n -= count;

		prim |= BCI_CMD_DRAW_CONT;
	}

	return 0;
}

static int savage_dispatch_dma_idx(drm_savage_private_t * dev_priv,
				   const drm_savage_cmd_header_t * cmd_header,
				   const uint16_t __user * usr_idx,
				   const drm_buf_t * dmabuf)
{
	unsigned char reorder = 0;
	unsigned int prim = cmd_header->idx.prim;
	unsigned int skip = cmd_header->idx.skip;
	unsigned int n = cmd_header->idx.count;
	unsigned int i;
	BCI_LOCALS;

	if (!dmabuf) {
		DRM_ERROR("called without dma buffers!\n");
		return DRM_ERR(EINVAL);
	}

	if (!n)
		return 0;

	switch (prim) {
	case SAVAGE_PRIM_TRILIST_201:
		reorder = 1;
		prim = SAVAGE_PRIM_TRILIST;
	case SAVAGE_PRIM_TRILIST:
		if (n % 3 != 0) {
			DRM_ERROR("wrong number of indices %u in TRILIST\n", n);
			return DRM_ERR(EINVAL);
		}
		break;
	case SAVAGE_PRIM_TRISTRIP:
	case SAVAGE_PRIM_TRIFAN:
		if (n < 3) {
			DRM_ERROR
			    ("wrong number of indices %u in TRIFAN/STRIP\n", n);
			return DRM_ERR(EINVAL);
		}
		break;
	default:
		DRM_ERROR("invalid primitive type %u\n", prim);
		return DRM_ERR(EINVAL);
	}

	if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
		if (skip != 0) {
			DRM_ERROR("invalid skip flags 0x%04x for DMA\n", skip);
			return DRM_ERR(EINVAL);
		}
	} else {
		unsigned int size = 10 - (skip & 1) - (skip >> 1 & 1) -
		    (skip >> 2 & 1) - (skip >> 3 & 1) - (skip >> 4 & 1) -
		    (skip >> 5 & 1) - (skip >> 6 & 1) - (skip >> 7 & 1);
		if (skip > SAVAGE_SKIP_ALL_S4 || size != 8) {
			DRM_ERROR("invalid skip flags 0x%04x for DMA\n", skip);
			return DRM_ERR(EINVAL);
		}
		if (reorder) {
			DRM_ERROR("TRILIST_201 used on Savage4 hardware\n");
			return DRM_ERR(EINVAL);
		}
	}

	/* Vertex DMA doesn't work with command DMA at the same time,
	 * so we use BCI_... to submit commands here. Flush buffered
	 * faked DMA first. */
	DMA_FLUSH();

	if (dmabuf->bus_address != dev_priv->state.common.vbaddr) {
		BEGIN_BCI(2);
		BCI_SET_REGISTERS(SAVAGE_VERTBUFADDR, 1);
		BCI_WRITE(dmabuf->bus_address | dev_priv->dma_type);
		dev_priv->state.common.vbaddr = dmabuf->bus_address;
	}
	if (S3_SAVAGE3D_SERIES(dev_priv->chipset) && dev_priv->waiting) {
		/* Workaround for what looks like a hardware bug. If a
		 * WAIT_3D_IDLE was emitted some time before the
		 * indexed drawing command then the engine will lock
		 * up. There are two known workarounds:
		 * WAIT_IDLE_EMPTY or emit at least 63 NOPs. */
		BEGIN_BCI(63);
		for (i = 0; i < 63; ++i)
			BCI_WRITE(BCI_CMD_WAIT);
		dev_priv->waiting = 0;
	}

	prim <<= 25;
	while (n != 0) {
		/* Can emit up to 255 indices (85 triangles) at once. */
		unsigned int count = n > 255 ? 255 : n;
		/* Is it ok to allocate 510 bytes on the stack in an ioctl? */
		uint16_t idx[255];

		/* Copy and check indices */
		DRM_COPY_FROM_USER_UNCHECKED(idx, usr_idx, count * 2);
		for (i = 0; i < count; ++i) {
			if (idx[i] > dmabuf->total / 32) {
				DRM_ERROR("idx[%u]=%u out of range (0-%u)\n",
					  i, idx[i], dmabuf->total / 32);
				return DRM_ERR(EINVAL);
			}
		}

		if (reorder) {
			/* Need to reorder indices for correct flat
			 * shading while preserving the clock sense
			 * for correct culling. Only on Savage3D. */
			int reorder[3] = { 2, -1, -1 };

			BEGIN_BCI((count + 1 + 1) / 2);
			BCI_DRAW_INDICES_S3D(count, prim, idx[2]);

			for (i = 1; i + 1 < count; i += 2)
				BCI_WRITE(idx[i + reorder[i % 3]] |
					  (idx[i + 1 + reorder[(i + 1) % 3]] <<
					   16));
			if (i < count)
				BCI_WRITE(idx[i + reorder[i % 3]]);
		} else if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
			BEGIN_BCI((count + 1 + 1) / 2);
			BCI_DRAW_INDICES_S3D(count, prim, idx[0]);

			for (i = 1; i + 1 < count; i += 2)
				BCI_WRITE(idx[i] | (idx[i + 1] << 16));
			if (i < count)
				BCI_WRITE(idx[i]);
		} else {
			BEGIN_BCI((count + 2 + 1) / 2);
			BCI_DRAW_INDICES_S4(count, prim, skip);

			for (i = 0; i + 1 < count; i += 2)
				BCI_WRITE(idx[i] | (idx[i + 1] << 16));
			if (i < count)
				BCI_WRITE(idx[i]);
		}

		usr_idx += count;
		n -= count;

		prim |= BCI_CMD_DRAW_CONT;
	}

	return 0;
}

static int savage_dispatch_vb_idx(drm_savage_private_t * dev_priv,
				  const drm_savage_cmd_header_t * cmd_header,
				  const uint16_t __user * usr_idx,
				  const uint32_t __user * vtxbuf,
				  unsigned int vb_size, unsigned int vb_stride)
{
	unsigned char reorder = 0;
	unsigned int prim = cmd_header->idx.prim;
	unsigned int skip = cmd_header->idx.skip;
	unsigned int n = cmd_header->idx.count;
	unsigned int vtx_size;
	unsigned int i;
	DMA_LOCALS;

	if (!n)
		return 0;

	switch (prim) {
	case SAVAGE_PRIM_TRILIST_201:
		reorder = 1;
		prim = SAVAGE_PRIM_TRILIST;
	case SAVAGE_PRIM_TRILIST:
		if (n % 3 != 0) {
			DRM_ERROR("wrong number of indices %u in TRILIST\n", n);
			return DRM_ERR(EINVAL);
		}
		break;
	case SAVAGE_PRIM_TRISTRIP:
	case SAVAGE_PRIM_TRIFAN:
		if (n < 3) {
			DRM_ERROR
			    ("wrong number of indices %u in TRIFAN/STRIP\n", n);
			return DRM_ERR(EINVAL);
		}
		break;
	default:
		DRM_ERROR("invalid primitive type %u\n", prim);
		return DRM_ERR(EINVAL);
	}

	if (S3_SAVAGE3D_SERIES(dev_priv->chipset)) {
		if (skip > SAVAGE_SKIP_ALL_S3D) {
			DRM_ERROR("invalid skip flags 0x%04x\n", skip);
			return DRM_ERR(EINVAL);
		}
		vtx_size = 8;	/* full vertex */
	} else {
		if (skip > SAVAGE_SKIP_ALL_S4) {
			DRM_ERROR("invalid skip flags 0x%04x\n", skip);
			return DRM_ERR(EINVAL);
		}
		vtx_size = 10;	/* full vertex */
	}

	vtx_size -= (skip & 1) + (skip >> 1 & 1) +
	    (skip >> 2 & 1) + (skip >> 3 & 1) + (skip >> 4 & 1) +
	    (skip >> 5 & 1) + (skip >> 6 & 1) + (skip >> 7 & 1);

	if (vtx_size > vb_stride) {
		DRM_ERROR("vertex size greater than vb stride (%u > %u)\n",
			  vtx_size, vb_stride);
		return DRM_ERR(EINVAL);
	}

	prim <<= 25;
	while (n != 0) {
		/* Can emit up to 255 vertices (85 triangles) at once. */
		unsigned int count = n > 255 ? 255 : n;
		/* Is it ok to allocate 510 bytes on the stack in an ioctl? */
		uint16_t idx[255];

		/* Copy and check indices */
		DRM_COPY_FROM_USER_UNCHECKED(idx, usr_idx, count * 2);
		for (i = 0; i < count; ++i) {
			if (idx[i] > vb_size / (vb_stride * 4)) {
				DRM_ERROR("idx[%u]=%u out of range (0-%u)\n",
					  i, idx[i], vb_size / (vb_stride * 4));
				return DRM_ERR(EINVAL);
			}
		}

		if (reorder) {
			/* Need to reorder vertices for correct flat
			 * shading while preserving the clock sense
			 * for correct culling. Only on Savage3D. */
			int reorder[3] = { 2, -1, -1 };

			BEGIN_DMA(count * vtx_size + 1);
			DMA_DRAW_PRIMITIVE(count, prim, skip);

			for (i = 0; i < count; ++i) {
				unsigned int j = idx[i + reorder[i % 3]];
				DMA_COPY_FROM_USER(&vtxbuf[vb_stride * j],
						   vtx_size);
			}

			DMA_COMMIT();
		} else {
			BEGIN_DMA(count * vtx_size + 1);
			DMA_DRAW_PRIMITIVE(count, prim, skip);

			for (i = 0; i < count; ++i) {
				unsigned int j = idx[i];
				DMA_COPY_FROM_USER(&vtxbuf[vb_stride * j],
						   vtx_size);
			}

			DMA_COMMIT();
		}

		usr_idx += count;
		n -= count;

		prim |= BCI_CMD_DRAW_CONT;
	}

	return 0;
}

static int savage_dispatch_clear(drm_savage_private_t * dev_priv,
				 const drm_savage_cmd_header_t * cmd_header,
				 const drm_savage_cmd_header_t __user * data,
				 unsigned int nbox,
				 const drm_clip_rect_t __user * usr_boxes)
{
	unsigned int flags = cmd_header->clear0.flags, mask, value;
	unsigned int clear_cmd;
	unsigned int i, nbufs;
	DMA_LOCALS;

	if (nbox == 0)
		return 0;

	DRM_GET_USER_UNCHECKED(mask, &data->clear1.mask);
	DRM_GET_USER_UNCHECKED(value, &data->clear1.value);

	clear_cmd = BCI_CMD_RECT | BCI_CMD_RECT_XP | BCI_CMD_RECT_YP |
	    BCI_CMD_SEND_COLOR | BCI_CMD_DEST_PBD_NEW;
	BCI_CMD_SET_ROP(clear_cmd, 0xCC);

	nbufs = ((flags & SAVAGE_FRONT) ? 1 : 0) +
	    ((flags & SAVAGE_BACK) ? 1 : 0) + ((flags & SAVAGE_DEPTH) ? 1 : 0);
	if (nbufs == 0)
		return 0;

	if (mask != 0xffffffff) {
		/* set mask */
		BEGIN_DMA(2);
		DMA_SET_REGISTERS(SAVAGE_BITPLANEWTMASK, 1);
		DMA_WRITE(mask);
		DMA_COMMIT();
	}
	for (i = 0; i < nbox; ++i) {
		drm_clip_rect_t box;
		unsigned int x, y, w, h;
		unsigned int buf;
		DRM_COPY_FROM_USER_UNCHECKED(&box, &usr_boxes[i], sizeof(box));
		x = box.x1, y = box.y1;
		w = box.x2 - box.x1;
		h = box.y2 - box.y1;
		BEGIN_DMA(nbufs * 6);
		for (buf = SAVAGE_FRONT; buf <= SAVAGE_DEPTH; buf <<= 1) {
			if (!(flags & buf))
				continue;
			DMA_WRITE(clear_cmd);
			switch (buf) {
			case SAVAGE_FRONT:
				DMA_WRITE(dev_priv->front_offset);
				DMA_WRITE(dev_priv->front_bd);
				break;
			case SAVAGE_BACK:
				DMA_WRITE(dev_priv->back_offset);
				DMA_WRITE(dev_priv->back_bd);
				break;
			case SAVAGE_DEPTH:
				DMA_WRITE(dev_priv->depth_offset);
				DMA_WRITE(dev_priv->depth_bd);
				break;
			}
			DMA_WRITE(value);
			DMA_WRITE(BCI_X_Y(x, y));
			DMA_WRITE(BCI_W_H(w, h));
		}
		DMA_COMMIT();
	}
	if (mask != 0xffffffff) {
		/* reset mask */
		BEGIN_DMA(2);
		DMA_SET_REGISTERS(SAVAGE_BITPLANEWTMASK, 1);
		DMA_WRITE(0xffffffff);
		DMA_COMMIT();
	}

	return 0;
}

static int savage_dispatch_swap(drm_savage_private_t * dev_priv,
				unsigned int nbox,
				const drm_clip_rect_t __user * usr_boxes)
{
	unsigned int swap_cmd;
	unsigned int i;
	DMA_LOCALS;

	if (nbox == 0)
		return 0;

	swap_cmd = BCI_CMD_RECT | BCI_CMD_RECT_XP | BCI_CMD_RECT_YP |
	    BCI_CMD_SRC_PBD_COLOR_NEW | BCI_CMD_DEST_GBD;
	BCI_CMD_SET_ROP(swap_cmd, 0xCC);

	for (i = 0; i < nbox; ++i) {
		drm_clip_rect_t box;
		DRM_COPY_FROM_USER_UNCHECKED(&box, &usr_boxes[i], sizeof(box));

		BEGIN_DMA(6);
		DMA_WRITE(swap_cmd);
		DMA_WRITE(dev_priv->back_offset);
		DMA_WRITE(dev_priv->back_bd);
		DMA_WRITE(BCI_X_Y(box.x1, box.y1));
		DMA_WRITE(BCI_X_Y(box.x1, box.y1));
		DMA_WRITE(BCI_W_H(box.x2 - box.x1, box.y2 - box.y1));
		DMA_COMMIT();
	}

	return 0;
}

static int savage_dispatch_draw(drm_savage_private_t * dev_priv,
				const drm_savage_cmd_header_t __user * start,
				const drm_savage_cmd_header_t __user * end,
				const drm_buf_t * dmabuf,
				const unsigned int __user * usr_vtxbuf,
				unsigned int vb_size, unsigned int vb_stride,
				unsigned int nbox,
				const drm_clip_rect_t __user * usr_boxes)
{
	unsigned int i, j;
	int ret;

	for (i = 0; i < nbox; ++i) {
		drm_clip_rect_t box;
		const drm_savage_cmd_header_t __user *usr_cmdbuf;
		DRM_COPY_FROM_USER_UNCHECKED(&box, &usr_boxes[i], sizeof(box));
		dev_priv->emit_clip_rect(dev_priv, &box);

		usr_cmdbuf = start;
		while (usr_cmdbuf < end) {
			drm_savage_cmd_header_t cmd_header;
			DRM_COPY_FROM_USER_UNCHECKED(&cmd_header, usr_cmdbuf,
						     sizeof(cmd_header));
			usr_cmdbuf++;
			switch (cmd_header.cmd.cmd) {
			case SAVAGE_CMD_DMA_PRIM:
				ret =
				    savage_dispatch_dma_prim(dev_priv,
							     &cmd_header,
							     dmabuf);
				break;
			case SAVAGE_CMD_VB_PRIM:
				ret =
				    savage_dispatch_vb_prim(dev_priv,
							    &cmd_header,
							    (const uint32_t
							     __user *)
							    usr_vtxbuf, vb_size,
							    vb_stride);
				break;
			case SAVAGE_CMD_DMA_IDX:
				j = (cmd_header.idx.count + 3) / 4;
				/* j was check in savage_bci_cmdbuf */
				ret =
				    savage_dispatch_dma_idx(dev_priv,
							    &cmd_header,
							    (const uint16_t
							     __user *)
							    usr_cmdbuf, dmabuf);
				usr_cmdbuf += j;
				break;
			case SAVAGE_CMD_VB_IDX:
				j = (cmd_header.idx.count + 3) / 4;
				/* j was check in savage_bci_cmdbuf */
				ret =
				    savage_dispatch_vb_idx(dev_priv,
							   &cmd_header,
							   (const uint16_t
							    __user *)usr_cmdbuf,
							   (const uint32_t
							    __user *)usr_vtxbuf,
							   vb_size, vb_stride);
				usr_cmdbuf += j;
				break;
			default:
				/* What's the best return code? EFAULT? */
				DRM_ERROR("IMPLEMENTATION ERROR: "
					  "non-drawing-command %d\n",
					  cmd_header.cmd.cmd);
				return DRM_ERR(EINVAL);
			}

			if (ret != 0)
				return ret;
		}
	}

	return 0;
}

int savage_bci_cmdbuf(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_savage_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *dmabuf;
	drm_savage_cmdbuf_t cmdbuf;
	drm_savage_cmd_header_t __user *usr_cmdbuf;
	drm_savage_cmd_header_t __user *first_draw_cmd;
	unsigned int __user *usr_vtxbuf;
	drm_clip_rect_t __user *usr_boxes;
	unsigned int i, j;
	int ret = 0;

	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, filp);

	DRM_COPY_FROM_USER_IOCTL(cmdbuf, (drm_savage_cmdbuf_t __user *) data,
				 sizeof(cmdbuf));

	if (dma && dma->buflist) {
		if (cmdbuf.dma_idx > dma->buf_count) {
			DRM_ERROR
			    ("vertex buffer index %u out of range (0-%u)\n",
			     cmdbuf.dma_idx, dma->buf_count - 1);
			return DRM_ERR(EINVAL);
		}
		dmabuf = dma->buflist[cmdbuf.dma_idx];
	} else {
		dmabuf = NULL;
	}

	usr_cmdbuf = (drm_savage_cmd_header_t __user *) cmdbuf.cmd_addr;
	usr_vtxbuf = (unsigned int __user *)cmdbuf.vb_addr;
	usr_boxes = (drm_clip_rect_t __user *) cmdbuf.box_addr;
	if ((cmdbuf.size && DRM_VERIFYAREA_READ(usr_cmdbuf, cmdbuf.size * 8)) ||
	    (cmdbuf.vb_size && DRM_VERIFYAREA_READ(usr_vtxbuf, cmdbuf.vb_size))
	    || (cmdbuf.nbox
		&& DRM_VERIFYAREA_READ(usr_boxes,
				       cmdbuf.nbox * sizeof(drm_clip_rect_t))))
		return DRM_ERR(EFAULT);

	/* Make sure writes to DMA buffers are finished before sending
	 * DMA commands to the graphics hardware. */
	DRM_MEMORYBARRIER();

	/* Coming from user space. Don't know if the Xserver has
	 * emitted wait commands. Assuming the worst. */
	dev_priv->waiting = 1;

	i = 0;
	first_draw_cmd = NULL;
	while (i < cmdbuf.size) {
		drm_savage_cmd_header_t cmd_header;
		DRM_COPY_FROM_USER_UNCHECKED(&cmd_header, usr_cmdbuf,
					     sizeof(cmd_header));
		usr_cmdbuf++;
		i++;

		/* Group drawing commands with same state to minimize
		 * iterations over clip rects. */
		j = 0;
		switch (cmd_header.cmd.cmd) {
		case SAVAGE_CMD_DMA_IDX:
		case SAVAGE_CMD_VB_IDX:
			j = (cmd_header.idx.count + 3) / 4;
			if (i + j > cmdbuf.size) {
				DRM_ERROR("indexed drawing command extends "
					  "beyond end of command buffer\n");
				DMA_FLUSH();
				return DRM_ERR(EINVAL);
			}
			/* fall through */
		case SAVAGE_CMD_DMA_PRIM:
		case SAVAGE_CMD_VB_PRIM:
			if (!first_draw_cmd)
				first_draw_cmd = usr_cmdbuf - 1;
			usr_cmdbuf += j;
			i += j;
			break;
		default:
			if (first_draw_cmd) {
				ret =
				    savage_dispatch_draw(dev_priv,
							 first_draw_cmd,
							 usr_cmdbuf - 1, dmabuf,
							 usr_vtxbuf,
							 cmdbuf.vb_size,
							 cmdbuf.vb_stride,
							 cmdbuf.nbox,
							 usr_boxes);
				if (ret != 0)
					return ret;
				first_draw_cmd = NULL;
			}
		}
		if (first_draw_cmd)
			continue;

		switch (cmd_header.cmd.cmd) {
		case SAVAGE_CMD_STATE:
			j = (cmd_header.state.count + 1) / 2;
			if (i + j > cmdbuf.size) {
				DRM_ERROR("command SAVAGE_CMD_STATE extends "
					  "beyond end of command buffer\n");
				DMA_FLUSH();
				return DRM_ERR(EINVAL);
			}
			ret = savage_dispatch_state(dev_priv, &cmd_header,
						    (uint32_t __user *)
						    usr_cmdbuf);
			usr_cmdbuf += j;
			i += j;
			break;
		case SAVAGE_CMD_CLEAR:
			if (i + 1 > cmdbuf.size) {
				DRM_ERROR("command SAVAGE_CMD_CLEAR extends "
					  "beyond end of command buffer\n");
				DMA_FLUSH();
				return DRM_ERR(EINVAL);
			}
			ret = savage_dispatch_clear(dev_priv, &cmd_header,
						    usr_cmdbuf,
						    cmdbuf.nbox, usr_boxes);
			usr_cmdbuf++;
			i++;
			break;
		case SAVAGE_CMD_SWAP:
			ret = savage_dispatch_swap(dev_priv,
						   cmdbuf.nbox, usr_boxes);
			break;
		default:
			DRM_ERROR("invalid command 0x%x\n", cmd_header.cmd.cmd);
			DMA_FLUSH();
			return DRM_ERR(EINVAL);
		}

		if (ret != 0) {
			DMA_FLUSH();
			return ret;
		}
	}

	if (first_draw_cmd) {
		ret =
		    savage_dispatch_draw(dev_priv, first_draw_cmd, usr_cmdbuf,
					 dmabuf, usr_vtxbuf, cmdbuf.vb_size,
					 cmdbuf.vb_stride, cmdbuf.nbox,
					 usr_boxes);
		if (ret != 0) {
			DMA_FLUSH();
			return ret;
		}
	}

	DMA_FLUSH();

	if (dmabuf && cmdbuf.discard) {
		drm_savage_buf_priv_t *buf_priv = dmabuf->dev_private;
		uint16_t event;
		event = savage_bci_emit_event(dev_priv, SAVAGE_WAIT_3D);
		SET_AGE(&buf_priv->age, event, dev_priv->event_wrap);
		savage_freelist_put(dev, dmabuf);
	}

	return 0;
}
