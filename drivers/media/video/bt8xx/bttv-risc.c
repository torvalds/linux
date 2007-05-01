/*

    bttv-risc.c  --  interfaces to other kernel modules

    bttv risc code handling
	- memory management
	- generation

    (c) 2000-2003 Gerd Knorr <kraxel@bytesex.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <asm/page.h>
#include <asm/pgtable.h>

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
	u32 *rp;
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
		if ((btv->opt_vcr_hack) &&
		    (line >= (store_lines - VCR_HACK_LINES)))
			continue;
		while (offset && offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			sg++;
		}
		if (bpl <= sg_dma_len(sg)-offset) {
			/* fits into current chunk */
			*(rp++)=cpu_to_le32(BT848_RISC_WRITE|BT848_RISC_SOL|
					    BT848_RISC_EOL|bpl);
			*(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
			offset+=bpl;
		} else {
			/* scanline needs to be splitted */
			todo = bpl;
			*(rp++)=cpu_to_le32(BT848_RISC_WRITE|BT848_RISC_SOL|
					    (sg_dma_len(sg)-offset));
			*(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
			todo -= (sg_dma_len(sg)-offset);
			offset = 0;
			sg++;
			while (todo > sg_dma_len(sg)) {
				*(rp++)=cpu_to_le32(BT848_RISC_WRITE|
						    sg_dma_len(sg));
				*(rp++)=cpu_to_le32(sg_dma_address(sg));
				todo -= sg_dma_len(sg);
				sg++;
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
	BUG_ON((risc->jmp - risc->cpu + 2) * sizeof(*risc->cpu) > risc->size);
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
	u32 *rp,ri;
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
				ysg++;
			}
			while (uoffset && uoffset >= sg_dma_len(usg)) {
				uoffset -= sg_dma_len(usg);
				usg++;
			}
			while (voffset && voffset >= sg_dma_len(vsg)) {
				voffset -= sg_dma_len(vsg);
				vsg++;
			}

			/* calculate max number of bytes we can write */
			ylen = todo;
			if (yoffset + ylen > sg_dma_len(ysg))
				ylen = sg_dma_len(ysg) - yoffset;
			if (chroma) {
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
	BUG_ON((risc->jmp - risc->cpu + 2) * sizeof(*risc->cpu) > risc->size);
	return 0;
}

static int
bttv_risc_overlay(struct bttv *btv, struct btcx_riscmem *risc,
		  const struct bttv_format *fmt, struct bttv_overlay *ov,
		  int skip_even, int skip_odd)
{
	int dwords,rc,line,maxy,start,end,skip,nskips;
	struct btcx_skiplist *skips;
	u32 *rp,ri,ra;
	u32 addr;

	/* skip list for window clipping */
	if (NULL == (skips = kmalloc(sizeof(*skips) * ov->nclips,GFP_KERNEL)))
		return -ENOMEM;

	/* estimate risc mem: worst case is (1.5*clip+1) * lines instructions
	   + sync + jump (all 2 dwords) */
	dwords  = (3 * ov->nclips + 2) *
		((skip_even || skip_odd) ? (ov->w.height+1)>>1 :  ov->w.height);
	dwords += 4;
	if ((rc = btcx_riscmem_alloc(btv->c.pci,risc,dwords*4)) < 0) {
		kfree(skips);
		return rc;
	}

	/* sync instruction */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1);
	*(rp++) = cpu_to_le32(0);

	addr  = (unsigned long)btv->fbuf.base;
	addr += btv->fbuf.fmt.bytesperline * ov->w.top;
	addr += (fmt->depth >> 3)          * ov->w.left;

	/* scan lines */
	for (maxy = -1, line = 0; line < ov->w.height;
	     line++, addr += btv->fbuf.fmt.bytesperline) {
		if ((btv->opt_vcr_hack) &&
		     (line >= (ov->w.height - VCR_HACK_LINES)))
			continue;
		if ((line%2) == 0  &&  skip_even)
			continue;
		if ((line%2) == 1  &&  skip_odd)
			continue;

		/* calculate clipping */
		if (line > maxy)
			btcx_calc_skips(line, ov->w.width, &maxy,
					skips, &nskips, ov->clips, ov->nclips);

		/* write out risc code */
		for (start = 0, skip = 0; start < ov->w.width; start = end) {
			if (skip >= nskips) {
				ri  = BT848_RISC_WRITE;
				end = ov->w.width;
			} else if (start < skips[skip].start) {
				ri  = BT848_RISC_WRITE;
				end = skips[skip].start;
			} else {
				ri  = BT848_RISC_SKIP;
				end = skips[skip].end;
				skip++;
			}
			if (BT848_RISC_WRITE == ri)
				ra = addr + (fmt->depth>>3)*start;
			else
				ra = 0;

			if (0 == start)
				ri |= BT848_RISC_SOL;
			if (ov->w.width == end)
				ri |= BT848_RISC_EOL;
			ri |= (fmt->depth>>3) * (end-start);

			*(rp++)=cpu_to_le32(ri);
			if (0 != ra)
				*(rp++)=cpu_to_le32(ra);
		}
	}

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	BUG_ON((risc->jmp - risc->cpu + 2) * sizeof(*risc->cpu) > risc->size);
	kfree(skips);
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

	if (bttv_tvcards[btv->c.type].muxsel[btv->input] < 0) {
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
	    || bttv_tvcards[btv->c.type].muxsel[btv->input] < 0) {
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

void
bttv_set_dma(struct bttv *btv, int override)
{
	unsigned long cmd;
	int capctl;

	btv->cap_ctl = 0;
	if (NULL != btv->curr.top)      btv->cap_ctl |= 0x02;
	if (NULL != btv->curr.bottom)   btv->cap_ctl |= 0x01;
	if (NULL != btv->cvbi)          btv->cap_ctl |= 0x0c;

	capctl  = 0;
	capctl |= (btv->cap_ctl & 0x03) ? 0x03 : 0x00;  /* capture  */
	capctl |= (btv->cap_ctl & 0x0c) ? 0x0c : 0x00;  /* vbi data */
	capctl |= override;

	d2printk(KERN_DEBUG
		 "bttv%d: capctl=%x lirq=%d top=%08Lx/%08Lx even=%08Lx/%08Lx\n",
		 btv->c.nr,capctl,btv->loop_irq,
		 btv->cvbi         ? (unsigned long long)btv->cvbi->top.dma            : 0,
		 btv->curr.top     ? (unsigned long long)btv->curr.top->top.dma        : 0,
		 btv->cvbi         ? (unsigned long long)btv->cvbi->bottom.dma         : 0,
		 btv->curr.bottom  ? (unsigned long long)btv->curr.bottom->bottom.dma  : 0);

	cmd = BT848_RISC_JUMP;
	if (btv->loop_irq) {
		cmd |= BT848_RISC_IRQ;
		cmd |= (btv->loop_irq  & 0x0f) << 16;
		cmd |= (~btv->loop_irq & 0x0f) << 20;
	}
	if (btv->curr.frame_irq || btv->loop_irq || btv->cvbi) {
		mod_timer(&btv->timeout, jiffies+BTTV_TIMEOUT);
	} else {
		del_timer(&btv->timeout);
	}
	btv->main.cpu[RISC_SLOT_LOOP] = cpu_to_le32(cmd);

	btaor(capctl, ~0x0f, BT848_CAP_CTL);
	if (capctl) {
		if (btv->dma_on)
			return;
		btwrite(btv->main.dma, BT848_RISC_STRT_ADD);
		btor(3, BT848_GPIO_DMA_CTL);
		btv->dma_on = 1;
	} else {
		if (!btv->dma_on)
			return;
		btand(~3, BT848_GPIO_DMA_CTL);
		btv->dma_on = 0;
	}
	return;
}

int
bttv_risc_init_main(struct bttv *btv)
{
	int rc;

	if ((rc = btcx_riscmem_alloc(btv->c.pci,&btv->main,PAGE_SIZE)) < 0)
		return rc;
	dprintk(KERN_DEBUG "bttv%d: risc main @ %08Lx\n",
		btv->c.nr,(unsigned long long)btv->main.dma);

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
		d2printk(KERN_DEBUG "bttv%d: risc=%p slot[%d]=NULL\n",
			 btv->c.nr,risc,slot);
		btv->main.cpu[slot+1] = cpu_to_le32(next);
	} else {
		d2printk(KERN_DEBUG "bttv%d: risc=%p slot[%d]=%08Lx irq=%d\n",
			 btv->c.nr,risc,slot,(unsigned long long)risc->dma,irqflags);
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

void
bttv_dma_free(struct videobuf_queue *q,struct bttv *btv, struct bttv_buffer *buf)
{
	BUG_ON(in_interrupt());
	videobuf_waiton(&buf->vb,0,0);
	videobuf_dma_unmap(q, &buf->vb.dma);
	videobuf_dma_free(&buf->vb.dma);
	btcx_riscmem_free(btv->c.pci,&buf->bottom);
	btcx_riscmem_free(btv->c.pci,&buf->top);
	buf->vb.state = STATE_NEEDS_INIT;
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

		vbi->vb.state = STATE_ACTIVE;
		list_del(&vbi->vb.queue);

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

		if (vbi->vbi_count[0] > 0) {
			top = &vbi->top;
			top_irq_flags = 4;
		}

		if (vbi->vbi_count[1] > 0) {
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
			set->top->vb.state    = STATE_ACTIVE;
			if (set->top->vb.queue.next)
				list_del(&set->top->vb.queue);
		} else {
			set->top->vb.state    = STATE_ACTIVE;
			set->bottom->vb.state = STATE_ACTIVE;
			if (set->top->vb.queue.next)
				list_del(&set->top->vb.queue);
			if (set->bottom->vb.queue.next)
				list_del(&set->bottom->vb.queue);
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
		set->top->vb.state  = STATE_ACTIVE;
		if (set->top->vb.queue.next)
			list_del(&set->top->vb.queue);
		bttv_apply_geo(btv, &set->top->geo,1);
		bttv_apply_geo(btv, &set->top->geo,0);
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, &set->top->top,
			       set->frame_irq);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, NULL,           0);
		btaor(set->top->btformat & 0xff, ~0xff, BT848_COLOR_FMT);
		btaor(set->top->btswap & 0x0f,   ~0x0f, BT848_COLOR_CTL);
	} else if (NULL != set->bottom) {
		set->bottom->vb.state = STATE_ACTIVE;
		if (set->bottom->vb.queue.next)
			list_del(&set->bottom->vb.queue);
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
	const struct bttv_tvnorm *tvnorm = bttv_tvnorms + buf->tvnorm;

	dprintk(KERN_DEBUG
		"bttv%d: buffer field: %s  format: %s  size: %dx%d\n",
		btv->c.nr, v4l2_field_names[buf->vb.field],
		buf->fmt->name, buf->vb.width, buf->vb.height);

	/* packed pixel modes */
	if (buf->fmt->flags & FORMAT_FLAGS_PACKED) {
		int bpl = (buf->fmt->depth >> 3) * buf->vb.width;
		int bpf = bpl * (buf->vb.height >> 1);

		bttv_calc_geo(btv,&buf->geo,buf->vb.width,buf->vb.height,
			      V4L2_FIELD_HAS_BOTH(buf->vb.field),
			      tvnorm,&buf->crop);

		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			bttv_risc_packed(btv,&buf->top,buf->vb.dma.sglist,
					 /* offset */ 0,bpl,
					 /* padding */ 0,/* skip_lines */ 0,
					 buf->vb.height);
			break;
		case V4L2_FIELD_BOTTOM:
			bttv_risc_packed(btv,&buf->bottom,buf->vb.dma.sglist,
					 0,bpl,0,0,buf->vb.height);
			break;
		case V4L2_FIELD_INTERLACED:
			bttv_risc_packed(btv,&buf->top,buf->vb.dma.sglist,
					 0,bpl,bpl,0,buf->vb.height >> 1);
			bttv_risc_packed(btv,&buf->bottom,buf->vb.dma.sglist,
					 bpl,bpl,bpl,0,buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			bttv_risc_packed(btv,&buf->top,buf->vb.dma.sglist,
					 0,bpl,0,0,buf->vb.height >> 1);
			bttv_risc_packed(btv,&buf->bottom,buf->vb.dma.sglist,
					 bpf,bpl,0,0,buf->vb.height >> 1);
			break;
		default:
			BUG();
		}
	}

	/* planar modes */
	if (buf->fmt->flags & FORMAT_FLAGS_PLANAR) {
		int uoffset, voffset;
		int ypadding, cpadding, lines;

		/* calculate chroma offsets */
		uoffset = buf->vb.width * buf->vb.height;
		voffset = buf->vb.width * buf->vb.height;
		if (buf->fmt->flags & FORMAT_FLAGS_CrCb) {
			/* Y-Cr-Cb plane order */
			uoffset >>= buf->fmt->hshift;
			uoffset >>= buf->fmt->vshift;
			uoffset  += voffset;
		} else {
			/* Y-Cb-Cr plane order */
			voffset >>= buf->fmt->hshift;
			voffset >>= buf->fmt->vshift;
			voffset  += uoffset;
		}

		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			bttv_calc_geo(btv,&buf->geo,buf->vb.width,
				      buf->vb.height,/* both_fields */ 0,
				      tvnorm,&buf->crop);
			bttv_risc_planar(btv, &buf->top, buf->vb.dma.sglist,
					 0,buf->vb.width,0,buf->vb.height,
					 uoffset,voffset,buf->fmt->hshift,
					 buf->fmt->vshift,0);
			break;
		case V4L2_FIELD_BOTTOM:
			bttv_calc_geo(btv,&buf->geo,buf->vb.width,
				      buf->vb.height,0,
				      tvnorm,&buf->crop);
			bttv_risc_planar(btv, &buf->bottom, buf->vb.dma.sglist,
					 0,buf->vb.width,0,buf->vb.height,
					 uoffset,voffset,buf->fmt->hshift,
					 buf->fmt->vshift,0);
			break;
		case V4L2_FIELD_INTERLACED:
			bttv_calc_geo(btv,&buf->geo,buf->vb.width,
				      buf->vb.height,1,
				      tvnorm,&buf->crop);
			lines    = buf->vb.height >> 1;
			ypadding = buf->vb.width;
			cpadding = buf->vb.width >> buf->fmt->hshift;
			bttv_risc_planar(btv,&buf->top,
					 buf->vb.dma.sglist,
					 0,buf->vb.width,ypadding,lines,
					 uoffset,voffset,
					 buf->fmt->hshift,
					 buf->fmt->vshift,
					 cpadding);
			bttv_risc_planar(btv,&buf->bottom,
					 buf->vb.dma.sglist,
					 ypadding,buf->vb.width,ypadding,lines,
					 uoffset+cpadding,
					 voffset+cpadding,
					 buf->fmt->hshift,
					 buf->fmt->vshift,
					 cpadding);
			break;
		case V4L2_FIELD_SEQ_TB:
			bttv_calc_geo(btv,&buf->geo,buf->vb.width,
				      buf->vb.height,1,
				      tvnorm,&buf->crop);
			lines    = buf->vb.height >> 1;
			ypadding = buf->vb.width;
			cpadding = buf->vb.width >> buf->fmt->hshift;
			bttv_risc_planar(btv,&buf->top,
					 buf->vb.dma.sglist,
					 0,buf->vb.width,0,lines,
					 uoffset >> 1,
					 voffset >> 1,
					 buf->fmt->hshift,
					 buf->fmt->vshift,
					 0);
			bttv_risc_planar(btv,&buf->bottom,
					 buf->vb.dma.sglist,
					 lines * ypadding,buf->vb.width,0,lines,
					 lines * ypadding + (uoffset >> 1),
					 lines * ypadding + (voffset >> 1),
					 buf->fmt->hshift,
					 buf->fmt->vshift,
					 0);
			break;
		default:
			BUG();
		}
	}

	/* raw data */
	if (buf->fmt->flags & FORMAT_FLAGS_RAW) {
		/* build risc code */
		buf->vb.field = V4L2_FIELD_SEQ_TB;
		bttv_calc_geo(btv,&buf->geo,tvnorm->swidth,tvnorm->sheight,
			      1,tvnorm,&buf->crop);
		bttv_risc_packed(btv, &buf->top,  buf->vb.dma.sglist,
				 /* offset */ 0, RAW_BPL, /* padding */ 0,
				 /* skip_lines */ 0, RAW_LINES);
		bttv_risc_packed(btv, &buf->bottom, buf->vb.dma.sglist,
				 buf->vb.size/2 , RAW_BPL, 0, 0, RAW_LINES);
	}

	/* copy format info */
	buf->btformat = buf->fmt->btformat;
	buf->btswap   = buf->fmt->btswap;
	return 0;
}

/* ---------------------------------------------------------- */

/* calculate geometry, build risc code */
int
bttv_overlay_risc(struct bttv *btv,
		  struct bttv_overlay *ov,
		  const struct bttv_format *fmt,
		  struct bttv_buffer *buf)
{
	/* check interleave, bottom+top fields */
	dprintk(KERN_DEBUG
		"bttv%d: overlay fields: %s format: %s  size: %dx%d\n",
		btv->c.nr, v4l2_field_names[buf->vb.field],
		fmt->name,ov->w.width,ov->w.height);

	/* calculate geometry */
	bttv_calc_geo(btv,&buf->geo,ov->w.width,ov->w.height,
		      V4L2_FIELD_HAS_BOTH(ov->field),
		      &bttv_tvnorms[ov->tvnorm],&buf->crop);

	/* build risc code */
	switch (ov->field) {
	case V4L2_FIELD_TOP:
		bttv_risc_overlay(btv, &buf->top,    fmt, ov, 0, 0);
		break;
	case V4L2_FIELD_BOTTOM:
		bttv_risc_overlay(btv, &buf->bottom, fmt, ov, 0, 0);
		break;
	case V4L2_FIELD_INTERLACED:
		bttv_risc_overlay(btv, &buf->top,    fmt, ov, 0, 1);
		bttv_risc_overlay(btv, &buf->bottom, fmt, ov, 1, 0);
		break;
	default:
		BUG();
	}

	/* copy format info */
	buf->btformat = fmt->btformat;
	buf->btswap   = fmt->btswap;
	buf->vb.field = ov->field;
	return 0;
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
