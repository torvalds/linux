// SPDX-License-Identifier: GPL-2.0-or-later
/*

    bttv-risc.c  --  interfaces to other kernel modules

    bttv risc code handling
	- memory management
	- generation

    (c) 2000-2003 Gerd Knorr <kraxel@bytesex.org>


*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/pgtable.h>
#include <asm/page.h>
#include <media/v4l2-ioctl.h>

#include "bttvp.h"

#define VCR_HACK_LINES 4

/* ---------------------------------------------------------- */
/* risc code generators                                       */

int
bttv_risc_packed(struct bttv *btv, struct btcx_riscmem *risc,
		 struct scatterlist *sglist,
		 unsigned int offset, unsigned int bpl,
		 unsigned int padding, unsigned int skip_lines,
		 unsigned int store_lines)
{
	u32 instructions,line,todo;
	struct scatterlist *sg;
	__le32 *rp;
	int rc;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line + sync + jump (all 2 dwords).  padding
	   can cause next bpl to start close to a page border.  First DMA
	   region may be smaller than PAGE_SIZE */
	instructions  = skip_lines * 4;
	instructions += (1 + ((bpl + padding) * store_lines)
			 / PAGE_SIZE + store_lines) * 8;
	instructions += 2 * 8;
	if ((rc = btcx_riscmem_alloc(btv->c.pci,risc,instructions)) < 0)
		return rc;

	/* sync instruction */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1);
	*(rp++) = cpu_to_le32(0);

	while (skip_lines-- > 0) {
		*(rp++) = cpu_to_le32(BT848_RISC_SKIP | BT848_RISC_SOL |
				      BT848_RISC_EOL | bpl);
	}

	/* scan lines */
	sg = sglist;
	for (line = 0; line < store_lines; line++) {
		if ((line >= (store_lines - VCR_HACK_LINES)) &&
		    btv->opt_vcr_hack)
			continue;
		while (offset && offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			sg = sg_next(sg);
		}
		if (bpl <= sg_dma_len(sg)-offset) {
			/* fits into current chunk */
			*(rp++)=cpu_to_le32(BT848_RISC_WRITE|BT848_RISC_SOL|
					    BT848_RISC_EOL|bpl);
			*(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
			offset+=bpl;
		} else {
			/* scanline needs to be split */
			todo = bpl;
			*(rp++)=cpu_to_le32(BT848_RISC_WRITE|BT848_RISC_SOL|
					    (sg_dma_len(sg)-offset));
			*(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
			todo -= (sg_dma_len(sg)-offset);
			offset = 0;
			sg = sg_next(sg);
			while (todo > sg_dma_len(sg)) {
				*(rp++)=cpu_to_le32(BT848_RISC_WRITE|
						    sg_dma_len(sg));
				*(rp++)=cpu_to_le32(sg_dma_address(sg));
				todo -= sg_dma_len(sg);
				sg = sg_next(sg);
			}
			*(rp++)=cpu_to_le32(BT848_RISC_WRITE|BT848_RISC_EOL|
					    todo);
			*(rp++)=cpu_to_le32(sg_dma_address(sg));
			offset += todo;
		}
		offset += padding;
	}

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	WARN_ON((risc->jmp - risc->cpu + 2) * sizeof(*risc->cpu) > risc->size);
	return 0;
}

static int
bttv_risc_planar(struct bttv *btv, struct btcx_riscmem *risc,
		 struct scatterlist *sglist,
		 unsigned int yoffset,  unsigned int ybpl,
		 unsigned int ypadding, unsigned int ylines,
		 unsigned int uoffset,  unsigned int voffset,
		 unsigned int hshift,   unsigned int vshift,
		 unsigned int cpadding)
{
	unsigned int instructions,line,todo,ylen,chroma;
	__le32 *rp;
	u32 ri;
	struct scatterlist *ysg;
	struct scatterlist *usg;
	struct scatterlist *vsg;
	int topfield = (0 == yoffset);
	int rc;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line (5 dwords)
	   plus sync + jump (2 dwords) */
	instructions  = ((3 + (ybpl + ypadding) * ylines * 2)
			 / PAGE_SIZE) + ylines;
	instructions += 2;
	if ((rc = btcx_riscmem_alloc(btv->c.pci,risc,instructions*4*5)) < 0)
		return rc;

	/* sync instruction */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(BT848_RISC_SYNC|BT848_FIFO_STATUS_FM3);
	*(rp++) = cpu_to_le32(0);

	/* scan lines */
	ysg = sglist;
	usg = sglist;
	vsg = sglist;
	for (line = 0; line < ylines; line++) {
		if ((btv->opt_vcr_hack) &&
		    (line >= (ylines - VCR_HACK_LINES)))
			continue;
		switch (vshift) {
		case 0:
			chroma = 1;
			break;
		case 1:
			if (topfield)
				chroma = ((line & 1) == 0);
			else
				chroma = ((line & 1) == 1);
			break;
		case 2:
			if (topfield)
				chroma = ((line & 3) == 0);
			else
				chroma = ((line & 3) == 2);
			break;
		default:
			chroma = 0;
			break;
		}

		for (todo = ybpl; todo > 0; todo -= ylen) {
			/* go to next sg entry if needed */
			while (yoffset && yoffset >= sg_dma_len(ysg)) {
				yoffset -= sg_dma_len(ysg);
				ysg = sg_next(ysg);
			}

			/* calculate max number of bytes we can write */
			ylen = todo;
			if (yoffset + ylen > sg_dma_len(ysg))
				ylen = sg_dma_len(ysg) - yoffset;
			if (chroma) {
				while (uoffset && uoffset >= sg_dma_len(usg)) {
					uoffset -= sg_dma_len(usg);
					usg = sg_next(usg);
				}
				while (voffset && voffset >= sg_dma_len(vsg)) {
					voffset -= sg_dma_len(vsg);
					vsg = sg_next(vsg);
				}

				if (uoffset + (ylen>>hshift) > sg_dma_len(usg))
					ylen = (sg_dma_len(usg) - uoffset) << hshift;
				if (voffset + (ylen>>hshift) > sg_dma_len(vsg))
					ylen = (sg_dma_len(vsg) - voffset) << hshift;
				ri = BT848_RISC_WRITE123;
			} else {
				ri = BT848_RISC_WRITE1S23;
			}
			if (ybpl == todo)
				ri |= BT848_RISC_SOL;
			if (ylen == todo)
				ri |= BT848_RISC_EOL;

			/* write risc instruction */
			*(rp++)=cpu_to_le32(ri | ylen);
			*(rp++)=cpu_to_le32(((ylen >> hshift) << 16) |
					    (ylen >> hshift));
			*(rp++)=cpu_to_le32(sg_dma_address(ysg)+yoffset);
			yoffset += ylen;
			if (chroma) {
				*(rp++)=cpu_to_le32(sg_dma_address(usg)+uoffset);
				uoffset += ylen >> hshift;
				*(rp++)=cpu_to_le32(sg_dma_address(vsg)+voffset);
				voffset += ylen >> hshift;
			}
		}
		yoffset += ypadding;
		if (chroma) {
			uoffset += cpadding;
			voffset += cpadding;
		}
	}

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	WARN_ON((risc->jmp - risc->cpu + 2) * sizeof(*risc->cpu) > risc->size);
	return 0;
}

/* ---------------------------------------------------------- */

static void
bttv_calc_geo_old(struct bttv *btv, struct bttv_geometry *geo,
		  int width, int height, int interleaved,
		  const struct bttv_tvnorm *tvnorm)
{
	u32 xsf, sr;
	int vdelay;

	int swidth       = tvnorm->swidth;
	int totalwidth   = tvnorm->totalwidth;
	int scaledtwidth = tvnorm->scaledtwidth;

	if (btv->input == btv->dig) {
		swidth       = 720;
		totalwidth   = 858;
		scaledtwidth = 858;
	}

	vdelay = tvnorm->vdelay;

	xsf = (width*scaledtwidth)/swidth;
	geo->hscale =  ((totalwidth*4096UL)/xsf-4096);
	geo->hdelay =  tvnorm->hdelayx1;
	geo->hdelay =  (geo->hdelay*width)/swidth;
	geo->hdelay &= 0x3fe;
	sr = ((tvnorm->sheight >> (interleaved?0:1))*512)/height - 512;
	geo->vscale =  (0x10000UL-sr) & 0x1fff;
	geo->crop   =  ((width>>8)&0x03) | ((geo->hdelay>>6)&0x0c) |
		((tvnorm->sheight>>4)&0x30) | ((vdelay>>2)&0xc0);
	geo->vscale |= interleaved ? (BT848_VSCALE_INT<<8) : 0;
	geo->vdelay  =  vdelay;
	geo->width   =  width;
	geo->sheight =  tvnorm->sheight;
	geo->vtotal  =  tvnorm->vtotal;

	if (btv->opt_combfilter) {
		geo->vtc  = (width < 193) ? 2 : ((width < 385) ? 1 : 0);
		geo->comb = (width < 769) ? 1 : 0;
	} else {
		geo->vtc  = 0;
		geo->comb = 0;
	}
}

static void
bttv_calc_geo		(struct bttv *                  btv,
			 struct bttv_geometry *         geo,
			 unsigned int                   width,
			 unsigned int                   height,
			 int                            both_fields,
			 const struct bttv_tvnorm *     tvnorm,
			 const struct v4l2_rect *       crop)
{
	unsigned int c_width;
	unsigned int c_height;
	u32 sr;

	if ((crop->left == tvnorm->cropcap.defrect.left
	     && crop->top == tvnorm->cropcap.defrect.top
	     && crop->width == tvnorm->cropcap.defrect.width
	     && crop->height == tvnorm->cropcap.defrect.height
	     && width <= tvnorm->swidth /* see PAL-Nc et al */)
	    || btv->input == btv->dig) {
		bttv_calc_geo_old(btv, geo, width, height,
				  both_fields, tvnorm);
		return;
	}

	/* For bug compatibility the image size checks permit scale
	   factors > 16. See bttv_crop_calc_limits(). */
	c_width = min((unsigned int) crop->width, width * 16);
	c_height = min((unsigned int) crop->height, height * 16);

	geo->width = width;
	geo->hscale = (c_width * 4096U + (width >> 1)) / width - 4096;
	/* Even to store Cb first, odd for Cr. */
	geo->hdelay = ((crop->left * width + c_width) / c_width) & ~1;

	geo->sheight = c_height;
	geo->vdelay = crop->top - tvnorm->cropcap.bounds.top + MIN_VDELAY;
	sr = c_height >> !both_fields;
	sr = (sr * 512U + (height >> 1)) / height - 512;
	geo->vscale = (0x10000UL - sr) & 0x1fff;
	geo->vscale |= both_fields ? (BT848_VSCALE_INT << 8) : 0;
	geo->vtotal = tvnorm->vtotal;

	geo->crop = (((geo->width   >> 8) & 0x03) |
		     ((geo->hdelay  >> 6) & 0x0c) |
		     ((geo->sheight >> 4) & 0x30) |
		     ((geo->vdelay  >> 2) & 0xc0));

	if (btv->opt_combfilter) {
		geo->vtc  = (width < 193) ? 2 : ((width < 385) ? 1 : 0);
		geo->comb = (width < 769) ? 1 : 0;
	} else {
		geo->vtc  = 0;
		geo->comb = 0;
	}
}

static void
bttv_apply_geo(struct bttv *btv, struct bttv_geometry *geo, int odd)
{
	int off = odd ? 0x80 : 0x00;

	if (geo->comb)
		btor(BT848_VSCALE_COMB, BT848_E_VSCALE_HI+off);
	else
		btand(~BT848_VSCALE_COMB, BT848_E_VSCALE_HI+off);

	btwrite(geo->vtc,             BT848_E_VTC+off);
	btwrite(geo->hscale >> 8,     BT848_E_HSCALE_HI+off);
	btwrite(geo->hscale & 0xff,   BT848_E_HSCALE_LO+off);
	btaor((geo->vscale>>8), 0xe0, BT848_E_VSCALE_HI+off);
	btwrite(geo->vscale & 0xff,   BT848_E_VSCALE_LO+off);
	btwrite(geo->width & 0xff,    BT848_E_HACTIVE_LO+off);
	btwrite(geo->hdelay & 0xff,   BT848_E_HDELAY_LO+off);
	btwrite(geo->sheight & 0xff,  BT848_E_VACTIVE_LO+off);
	btwrite(geo->vdelay & 0xff,   BT848_E_VDELAY_LO+off);
	btwrite(geo->crop,            BT848_E_CROP+off);
	btwrite(geo->vtotal>>8,       BT848_VTOTAL_HI);
	btwrite(geo->vtotal & 0xff,   BT848_VTOTAL_LO);
}

/* ---------------------------------------------------------- */
/* risc group / risc main loop / dma management               */

static void bttv_set_risc_status(struct bttv *btv)
{
	unsigned long cmd = BT848_RISC_JUMP;
	if (btv->loop_irq) {
		cmd |= BT848_RISC_IRQ;
		cmd |= (btv->loop_irq  & 0x0f) << 16;
		cmd |= (~btv->loop_irq & 0x0f) << 20;
	}
	btv->main.cpu[RISC_SLOT_LOOP] = cpu_to_le32(cmd);
}

static void bttv_set_irq_timer(struct bttv *btv)
{
	if (btv->curr.frame_irq || btv->loop_irq || btv->cvbi)
		mod_timer(&btv->timeout, jiffies + BTTV_TIMEOUT);
	else
		del_timer(&btv->timeout);
}

static int bttv_set_capture_control(struct bttv *btv, int start_capture)
{
	int capctl = 0;

	if (btv->curr.top || btv->curr.bottom)
		capctl = BT848_CAP_CTL_CAPTURE_ODD |
			 BT848_CAP_CTL_CAPTURE_EVEN;

	if (btv->cvbi)
		capctl |= BT848_CAP_CTL_CAPTURE_VBI_ODD |
			  BT848_CAP_CTL_CAPTURE_VBI_EVEN;

	capctl |= start_capture;

	btaor(capctl, ~0x0f, BT848_CAP_CTL);

	return capctl;
}

static void bttv_start_dma(struct bttv *btv)
{
	if (btv->dma_on)
		return;
	btwrite(btv->main.dma, BT848_RISC_STRT_ADD);
	btor(BT848_GPIO_DMA_CTL_RISC_ENABLE | BT848_GPIO_DMA_CTL_FIFO_ENABLE,
	     BT848_GPIO_DMA_CTL);
	btv->dma_on = 1;
}

static void bttv_stop_dma(struct bttv *btv)
{
	if (!btv->dma_on)
		return;
	btand(~(BT848_GPIO_DMA_CTL_RISC_ENABLE |
		BT848_GPIO_DMA_CTL_FIFO_ENABLE), BT848_GPIO_DMA_CTL);
	btv->dma_on = 0;
}

void bttv_set_dma(struct bttv *btv, int start_capture)
{
	int capctl = 0;

	bttv_set_risc_status(btv);
	bttv_set_irq_timer(btv);
	capctl = bttv_set_capture_control(btv, start_capture);

	if (capctl)
		bttv_start_dma(btv);
	else
		bttv_stop_dma(btv);

	d2printk("%d: capctl=%x lirq=%d top=%08llx/%08llx even=%08llx/%08llx\n",
		 btv->c.nr,capctl,btv->loop_irq,
		 btv->cvbi         ? (unsigned long long)btv->cvbi->top.dma            : 0,
		 btv->curr.top     ? (unsigned long long)btv->curr.top->top.dma        : 0,
		 btv->cvbi         ? (unsigned long long)btv->cvbi->bottom.dma         : 0,
		 btv->curr.bottom  ? (unsigned long long)btv->curr.bottom->bottom.dma  : 0);
}

int
bttv_risc_init_main(struct bttv *btv)
{
	int rc;

	if ((rc = btcx_riscmem_alloc(btv->c.pci,&btv->main,PAGE_SIZE)) < 0)
		return rc;
	dprintk("%d: risc main @ %08llx\n",
		btv->c.nr, (unsigned long long)btv->main.dma);

	btv->main.cpu[0] = cpu_to_le32(BT848_RISC_SYNC | BT848_RISC_RESYNC |
				       BT848_FIFO_STATUS_VRE);
	btv->main.cpu[1] = cpu_to_le32(0);
	btv->main.cpu[2] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[3] = cpu_to_le32(btv->main.dma + (4<<2));

	/* top field */
	btv->main.cpu[4] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[5] = cpu_to_le32(btv->main.dma + (6<<2));
	btv->main.cpu[6] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[7] = cpu_to_le32(btv->main.dma + (8<<2));

	btv->main.cpu[8] = cpu_to_le32(BT848_RISC_SYNC | BT848_RISC_RESYNC |
				       BT848_FIFO_STATUS_VRO);
	btv->main.cpu[9] = cpu_to_le32(0);

	/* bottom field */
	btv->main.cpu[10] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[11] = cpu_to_le32(btv->main.dma + (12<<2));
	btv->main.cpu[12] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[13] = cpu_to_le32(btv->main.dma + (14<<2));

	/* jump back to top field */
	btv->main.cpu[14] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[15] = cpu_to_le32(btv->main.dma + (0<<2));

	return 0;
}

int
bttv_risc_hook(struct bttv *btv, int slot, struct btcx_riscmem *risc,
	       int irqflags)
{
	unsigned long cmd;
	unsigned long next = btv->main.dma + ((slot+2) << 2);

	if (NULL == risc) {
		d2printk("%d: risc=%p slot[%d]=NULL\n", btv->c.nr, risc, slot);
		btv->main.cpu[slot+1] = cpu_to_le32(next);
	} else {
		d2printk("%d: risc=%p slot[%d]=%08llx irq=%d\n",
			 btv->c.nr, risc, slot,
			 (unsigned long long)risc->dma, irqflags);
		cmd = BT848_RISC_JUMP;
		if (irqflags) {
			cmd |= BT848_RISC_IRQ;
			cmd |= (irqflags  & 0x0f) << 16;
			cmd |= (~irqflags & 0x0f) << 20;
		}
		risc->jmp[0] = cpu_to_le32(cmd);
		risc->jmp[1] = cpu_to_le32(next);
		btv->main.cpu[slot+1] = cpu_to_le32(risc->dma);
	}
	return 0;
}

int bttv_buffer_risc_vbi(struct bttv *btv, struct bttv_buffer *buf)
{
	int r = 0;
	unsigned int offset;
	unsigned int bpl = 2044; /* max. vbipack */
	unsigned int padding = VBI_BPL - bpl;
	unsigned int skip_lines0 = 0;
	unsigned int skip_lines1 = 0;
	unsigned int min_vdelay = MIN_VDELAY;

	const struct bttv_tvnorm *tvnorm = btv->vbi_fmt.tvnorm;
	struct sg_table *sgt = vb2_dma_sg_plane_desc(&buf->vbuf.vb2_buf, 0);
	struct scatterlist *list = sgt->sgl;

	if (btv->vbi_fmt.fmt.count[0] > 0)
		skip_lines0 = max(0, (btv->vbi_fmt.fmt.start[0] -
					tvnorm->vbistart[0]));
	if (btv->vbi_fmt.fmt.count[1] > 0)
		skip_lines1 = max(0, (btv->vbi_fmt.fmt.start[1] -
					tvnorm->vbistart[1]));

	if (btv->vbi_fmt.fmt.count[0] > 0) {
		r = bttv_risc_packed(btv, &buf->top, list, 0, bpl, padding,
				     skip_lines0, btv->vbi_fmt.fmt.count[0]);
		if (r)
			return r;
	}

	if (btv->vbi_fmt.fmt.count[1] > 0) {
		offset = btv->vbi_fmt.fmt.count[0] * VBI_BPL;
		r = bttv_risc_packed(btv, &buf->bottom, list, offset, bpl,
				     padding, skip_lines1,
				     btv->vbi_fmt.fmt.count[1]);
		if (r)
			return r;
	}

	if (btv->vbi_fmt.end >= tvnorm->cropcap.bounds.top)
		min_vdelay += btv->vbi_fmt.end - tvnorm->cropcap.bounds.top;

	/* For bttv_buffer_activate_vbi(). */
	buf->geo.vdelay = min_vdelay;

	return r;
}

int
bttv_buffer_activate_vbi(struct bttv *btv,
			 struct bttv_buffer *vbi)
{
	struct btcx_riscmem *top;
	struct btcx_riscmem *bottom;
	int top_irq_flags;
	int bottom_irq_flags;

	top = NULL;
	bottom = NULL;
	top_irq_flags = 0;
	bottom_irq_flags = 0;

	if (vbi) {
		unsigned int crop, vdelay;

		list_del(&vbi->list);

		/* VDELAY is start of video, end of VBI capturing. */
		crop = btread(BT848_E_CROP);
		vdelay = btread(BT848_E_VDELAY_LO) + ((crop & 0xc0) << 2);

		if (vbi->geo.vdelay > vdelay) {
			vdelay = vbi->geo.vdelay & 0xfe;
			crop = (crop & 0x3f) | ((vbi->geo.vdelay >> 2) & 0xc0);

			btwrite(vdelay, BT848_E_VDELAY_LO);
			btwrite(crop,	BT848_E_CROP);
			btwrite(vdelay, BT848_O_VDELAY_LO);
			btwrite(crop,	BT848_O_CROP);
		}

		if (btv->vbi_count[0] > 0) {
			top = &vbi->top;
			top_irq_flags = 4;
		}

		if (btv->vbi_count[1] > 0) {
			top_irq_flags = 0;
			bottom = &vbi->bottom;
			bottom_irq_flags = 4;
		}
	}

	bttv_risc_hook(btv, RISC_SLOT_O_VBI, top, top_irq_flags);
	bttv_risc_hook(btv, RISC_SLOT_E_VBI, bottom, bottom_irq_flags);

	return 0;
}

int
bttv_buffer_activate_video(struct bttv *btv,
			   struct bttv_buffer_set *set)
{
	/* video capture */
	if (NULL != set->top  &&  NULL != set->bottom) {
		if (set->top == set->bottom) {
			if (set->top->list.next)
				list_del(&set->top->list);
		} else {
			if (set->top->list.next)
				list_del(&set->top->list);
			if (set->bottom->list.next)
				list_del(&set->bottom->list);
		}
		bttv_apply_geo(btv, &set->top->geo, 1);
		bttv_apply_geo(btv, &set->bottom->geo,0);
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, &set->top->top,
			       set->top_irq);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, &set->bottom->bottom,
			       set->frame_irq);
		btaor((set->top->btformat & 0xf0) | (set->bottom->btformat & 0x0f),
		      ~0xff, BT848_COLOR_FMT);
		btaor((set->top->btswap & 0x0a) | (set->bottom->btswap & 0x05),
		      ~0x0f, BT848_COLOR_CTL);
	} else if (NULL != set->top) {
		if (set->top->list.next)
			list_del(&set->top->list);
		bttv_apply_geo(btv, &set->top->geo,1);
		bttv_apply_geo(btv, &set->top->geo,0);
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, &set->top->top,
			       set->frame_irq);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, NULL,           0);
		btaor(set->top->btformat & 0xff, ~0xff, BT848_COLOR_FMT);
		btaor(set->top->btswap & 0x0f,   ~0x0f, BT848_COLOR_CTL);
	} else if (NULL != set->bottom) {
		if (set->bottom->list.next)
			list_del(&set->bottom->list);
		bttv_apply_geo(btv, &set->bottom->geo,1);
		bttv_apply_geo(btv, &set->bottom->geo,0);
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, NULL, 0);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, &set->bottom->bottom,
			       set->frame_irq);
		btaor(set->bottom->btformat & 0xff, ~0xff, BT848_COLOR_FMT);
		btaor(set->bottom->btswap & 0x0f,   ~0x0f, BT848_COLOR_CTL);
	} else {
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, NULL, 0);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, NULL, 0);
	}
	return 0;
}

/* ---------------------------------------------------------- */

/* calculate geometry, build risc code */
int
bttv_buffer_risc(struct bttv *btv, struct bttv_buffer *buf)
{
	int r = 0;
	const struct bttv_tvnorm *tvnorm = bttv_tvnorms + btv->tvnorm;
	struct sg_table *sgt = vb2_dma_sg_plane_desc(&buf->vbuf.vb2_buf, 0);
	struct scatterlist *list = sgt->sgl;
	unsigned long size = (btv->fmt->depth * btv->width * btv->height) >> 3;

	/* packed pixel modes */
	if (btv->fmt->flags & FORMAT_FLAGS_PACKED) {
		int bpl = (btv->fmt->depth >> 3) * btv->width;
		int bpf = bpl * (btv->height >> 1);

		bttv_calc_geo(btv, &buf->geo, btv->width, btv->height,
			      V4L2_FIELD_HAS_BOTH(buf->vbuf.field), tvnorm,
			      &btv->crop[!!btv->do_crop].rect);
		switch (buf->vbuf.field) {
		case V4L2_FIELD_TOP:
			r = bttv_risc_packed(btv, &buf->top, list, 0, bpl, 0,
					     0, btv->height);
			break;
		case V4L2_FIELD_BOTTOM:
			r = bttv_risc_packed(btv, &buf->bottom, list, 0, bpl,
					     0, 0, btv->height);
			break;
		case V4L2_FIELD_INTERLACED:
			r = bttv_risc_packed(btv, &buf->top, list, 0, bpl,
					     bpl, 0, btv->height >> 1);
			r = bttv_risc_packed(btv, &buf->bottom, list, bpl,
					     bpl, bpl, 0, btv->height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			r = bttv_risc_packed(btv, &buf->top, list, 0, bpl, 0,
					     0, btv->height >> 1);
			r = bttv_risc_packed(btv, &buf->bottom, list, bpf,
					     bpl, 0, 0, btv->height >> 1);
			break;
		default:
			WARN_ON(1);
			return -EINVAL;
		}
	}
	/* planar modes */
	if (btv->fmt->flags & FORMAT_FLAGS_PLANAR) {
		int uoffset, voffset;
		int ypadding, cpadding, lines;

		/* calculate chroma offsets */
		uoffset = btv->width * btv->height;
		voffset = btv->width * btv->height;
		if (btv->fmt->flags & FORMAT_FLAGS_CrCb) {
			/* Y-Cr-Cb plane order */
			uoffset >>= btv->fmt->hshift;
			uoffset >>= btv->fmt->vshift;
			uoffset  += voffset;
		} else {
			/* Y-Cb-Cr plane order */
			voffset >>= btv->fmt->hshift;
			voffset >>= btv->fmt->vshift;
			voffset  += uoffset;
		}
		switch (buf->vbuf.field) {
		case V4L2_FIELD_TOP:
			bttv_calc_geo(btv, &buf->geo, btv->width, btv->height,
				      0, tvnorm,
				      &btv->crop[!!btv->do_crop].rect);
			r = bttv_risc_planar(btv, &buf->top, list, 0,
					     btv->width, 0, btv->height,
					     uoffset, voffset,
					     btv->fmt->hshift,
					     btv->fmt->vshift, 0);
			break;
		case V4L2_FIELD_BOTTOM:
			bttv_calc_geo(btv, &buf->geo, btv->width, btv->height,
				      0, tvnorm,
				      &btv->crop[!!btv->do_crop].rect);
			r = bttv_risc_planar(btv, &buf->bottom, list, 0,
					     btv->width, 0, btv->height,
					     uoffset, voffset,
					     btv->fmt->hshift,
					     btv->fmt->vshift, 0);
			break;
		case V4L2_FIELD_INTERLACED:
			bttv_calc_geo(btv, &buf->geo, btv->width, btv->height,
				      1, tvnorm,
				      &btv->crop[!!btv->do_crop].rect);
			lines = btv->height >> 1;
			ypadding = btv->width;
			cpadding = btv->width >> btv->fmt->hshift;
			r = bttv_risc_planar(btv, &buf->top, list, 0,
					     btv->width, ypadding, lines,
					     uoffset, voffset,
					     btv->fmt->hshift,
					     btv->fmt->vshift, cpadding);

			r = bttv_risc_planar(btv, &buf->bottom, list,
					     ypadding, btv->width, ypadding,
					     lines,  uoffset + cpadding,
					     voffset + cpadding,
					     btv->fmt->hshift,
					     btv->fmt->vshift, cpadding);
			break;
		case V4L2_FIELD_SEQ_TB:
			bttv_calc_geo(btv, &buf->geo, btv->width, btv->height,
				      1, tvnorm,
				      &btv->crop[!!btv->do_crop].rect);
			lines = btv->height >> 1;
			ypadding = btv->width;
			cpadding = btv->width >> btv->fmt->hshift;
			r = bttv_risc_planar(btv, &buf->top, list, 0,
					     btv->width, 0, lines,
					     uoffset >> 1, voffset >> 1,
					     btv->fmt->hshift,
					     btv->fmt->vshift, 0);
			r = bttv_risc_planar(btv, &buf->bottom, list,
					     lines * ypadding,
					     btv->width, 0, lines,
					     lines * ypadding + (uoffset >> 1),
					     lines * ypadding + (voffset >> 1),
					     btv->fmt->hshift,
					     btv->fmt->vshift, 0);
			break;
		default:
			WARN_ON(1);
			return -EINVAL;
		}
	}
	/* raw data */
	if (btv->fmt->flags & FORMAT_FLAGS_RAW) {
		/* build risc code */
		buf->vbuf.field = V4L2_FIELD_SEQ_TB;
		bttv_calc_geo(btv, &buf->geo, tvnorm->swidth, tvnorm->sheight,
			      1, tvnorm, &btv->crop[!!btv->do_crop].rect);
		r = bttv_risc_packed(btv, &buf->top, list, 0, RAW_BPL, 0, 0,
				     RAW_LINES);
		r = bttv_risc_packed(btv, &buf->bottom, list, size / 2,
				     RAW_BPL, 0, 0, RAW_LINES);
	}

	/* copy format info */
	buf->btformat = btv->fmt->btformat;
	buf->btswap   = btv->fmt->btswap;

	return r;
}
