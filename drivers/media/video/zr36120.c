/*
    zr36120.c - Zoran 36120/36125 based framegrabbers

    Copyright (C) 1998-1999 Pauline Middelink <middelin@polyware.nl>

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
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/signal.h>
#include <linux/wait.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <linux/video_decoder.h>

#include <asm/uaccess.h>

#include "tuner.h"
#include "zr36120.h"
#include "zr36120_mem.h"

/* mark an required function argument unused - lintism */
#define	UNUSED(x)	(void)(x)

/* sensible default */
#ifndef CARDTYPE
#define CARDTYPE 0
#endif

/* Anybody who uses more than four? */
#define ZORAN_MAX 4

static unsigned int triton1=0;			/* triton1 chipset? */
static unsigned int cardtype[ZORAN_MAX]={ [ 0 ... ZORAN_MAX-1 ] = CARDTYPE };
static int video_nr = -1;
static int vbi_nr = -1;

static struct pci_device_id zr36120_pci_tbl[] = {
	{ PCI_VENDOR_ID_ZORAN,PCI_DEVICE_ID_ZORAN_36120,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, zr36120_pci_tbl);

MODULE_AUTHOR("Pauline Middelink <middelin@polyware.nl>");
MODULE_DESCRIPTION("Zoran ZR36120 based framegrabber");
MODULE_LICENSE("GPL");

MODULE_PARM(triton1,"i");
MODULE_PARM(cardtype,"1-" __MODULE_STRING(ZORAN_MAX) "i");
MODULE_PARM(video_nr,"i");
MODULE_PARM(vbi_nr,"i");

static int zoran_cards;
static struct zoran zorans[ZORAN_MAX];

/*
 * the meaning of each element can be found in zr36120.h
 * Determining the value of gpdir/gpval can be tricky. The
 * best way is to run the card under the original software
 * and read the values from the general purpose registers
 * 0x28 and 0x2C. How you do that is left as an exercise
 * to the impatient reader :)
 */
#define T 1	/* to separate the bools from the ints */
#define F 0
static struct tvcard tvcards[] = {
	/* reported working by <middelin@polyware.nl> */
/*0*/	{ "Trust Victor II",
	  2, 0, T, T, T, T, 0x7F, 0x80, { 1, SVHS(6) }, { 0 } },
	/* reported working by <Michael.Paxton@aihw.gov.au>  */
/*1*/   { "Aitech WaveWatcher TV-PCI",
	  3, 0, T, F, T, T, 0x7F, 0x80, { 1, TUNER(3), SVHS(6) }, { 0 } },
	/* reported working by ? */
/*2*/	{ "Genius Video Wonder PCI Video Capture Card",
	  2, 0, T, T, T, T, 0x7F, 0x80, { 1, SVHS(6) }, { 0 } },
	/* reported working by <Pascal.Gabriel@wanadoo.fr> */
/*3*/	{ "Guillemot Maxi-TV PCI",
	  2, 0, T, T, T, T, 0x7F, 0x80, { 1, SVHS(6) }, { 0 } },
	/* reported working by "Craig Whitmore <lennon@igrin.co.nz> */
/*4*/	{ "Quadrant Buster",
	  3, 3, T, F, T, T, 0x7F, 0x80, { SVHS(1), TUNER(2), 3 }, { 1, 2, 3 } },
	/* a debug entry which has all inputs mapped */
/*5*/	{ "ZR36120 based framegrabber (all inputs enabled)",
	  6, 0, T, T, T, T, 0x7F, 0x80, { 1, 2, 3, 4, 5, 6 }, { 0 } }
};
#undef T
#undef F
#define NRTVCARDS (sizeof(tvcards)/sizeof(tvcards[0]))

#ifdef __sparc__
#define	ENDIANESS	0
#else
#define	ENDIANESS	ZORAN_VFEC_LE
#endif

static struct { const char name[8]; uint mode; uint bpp; } palette2fmt[] = {
/* n/a     */	{ "n/a",     0, 0 },
/* GREY    */	{ "GRAY",    0, 0 },
/* HI240   */	{ "HI240",   0, 0 },
/* RGB565  */	{ "RGB565",  ZORAN_VFEC_RGB_RGB565|ENDIANESS, 2 },
/* RGB24   */	{ "RGB24",   ZORAN_VFEC_RGB_RGB888|ENDIANESS|ZORAN_VFEC_PACK24, 3 },
/* RGB32   */	{ "RGB32",   ZORAN_VFEC_RGB_RGB888|ENDIANESS, 4 },
/* RGB555  */	{ "RGB555",  ZORAN_VFEC_RGB_RGB555|ENDIANESS, 2 },
/* YUV422  */	{ "YUV422",  ZORAN_VFEC_RGB_YUV422|ENDIANESS, 2 },
/* YUYV    */	{ "YUYV",    0, 0 },
/* UYVY    */	{ "UYVY",    0, 0 },
/* YUV420  */	{ "YUV420",  0, 0 },
/* YUV411  */	{ "YUV411",  0, 0 },
/* RAW     */	{ "RAW",     0, 0 },
/* YUV422P */	{ "YUV422P", 0, 0 },
/* YUV411P */	{ "YUV411P", 0, 0 }};
#define NRPALETTES (sizeof(palette2fmt)/sizeof(palette2fmt[0]))
#undef ENDIANESS

/* ----------------------------------------------------------------------- */
/* ZORAN chipset detector                                                 */
/* shamelessly stolen from bttv.c                                         */
/* Reason for beeing here: we need to detect if we are running on a        */
/* Triton based chipset, and if so, enable a certain bit                   */
/* ----------------------------------------------------------------------- */
static
void __init handle_chipset(void)
{
	/* Just in case some nut set this to something dangerous */
	if (triton1)
		triton1 = ZORAN_VDC_TRICOM;

	if (pci_pci_problems & PCIPCI_TRITON) {
		printk(KERN_INFO "zoran: Host bridge 82437FX Triton PIIX\n");
		triton1 = ZORAN_VDC_TRICOM;
	}
}

/* ----------------------------------------------------------------------- */
/* ZORAN functions							   */
/* ----------------------------------------------------------------------- */

static void zoran_set_geo(struct zoran* ztv, struct vidinfo* i);

#if 0 /* unused */
static
void zoran_dump(struct zoran *ztv)
{
	char	str[256];
	char	*p=str; /* shut up, gcc! */
	int	i;

	for (i=0; i<0x60; i+=4) {
		if ((i % 16) == 0) {
			if (i) printk("%s\n",str);
			p = str;
			p+= sprintf(str, KERN_DEBUG "       %04x: ",i);
		}
		p += sprintf(p, "%08x ",zrread(i));
	}
}
#endif /* unused */

static
void reap_states(struct zoran* ztv)
{
	/* count frames */
	ztv->fieldnr++;

	/*
	 * Are we busy at all?
	 * This depends on if there is a workqueue AND the
	 * videotransfer is enabled on the chip...
	 */
	if (ztv->workqueue && (zrread(ZORAN_VDC) & ZORAN_VDC_VIDEN))
	{
		struct vidinfo* newitem;

		/* did we get a complete frame? */
		if (zrread(ZORAN_VSTR) & ZORAN_VSTR_GRAB)
			return;

DEBUG(printk(CARD_DEBUG "completed %s at %p\n",CARD,ztv->workqueue->kindof==FBUFFER_GRAB?"grab":"read",ztv->workqueue));

		/* we are done with this buffer, tell everyone */
		ztv->workqueue->status = FBUFFER_DONE;
		ztv->workqueue->fieldnr = ztv->fieldnr;
		/* not good, here for BTTV_FIELDNR reasons */
		ztv->lastfieldnr = ztv->fieldnr;

		switch (ztv->workqueue->kindof) {
		 case FBUFFER_GRAB:
			wake_up_interruptible(&ztv->grabq);
			break;
		 case FBUFFER_VBI:
			wake_up_interruptible(&ztv->vbiq);
			break;
		 default:
			printk(CARD_INFO "somebody killed the workqueue (kindof=%d)!\n",CARD,ztv->workqueue->kindof);
		}

		/* item completed, skip to next item in queue */
		write_lock(&ztv->lock);
		newitem = ztv->workqueue->next;
		ztv->workqueue->next = 0;	/* mark completed */
		ztv->workqueue = newitem;
		write_unlock(&ztv->lock);
	}

	/*
	 * ok, so it seems we have nothing in progress right now.
	 * Lets see if we can find some work.
	 */
	if (ztv->workqueue)
	{
		struct vidinfo* newitem;
again:

DEBUG(printk(CARD_DEBUG "starting %s at %p\n",CARD,ztv->workqueue->kindof==FBUFFER_GRAB?"grab":"read",ztv->workqueue));

		/* loadup the frame settings */
		read_lock(&ztv->lock);
		zoran_set_geo(ztv,ztv->workqueue);
		read_unlock(&ztv->lock);

		switch (ztv->workqueue->kindof) {
		 case FBUFFER_GRAB:
		 case FBUFFER_VBI:
			zrand(~ZORAN_OCR_OVLEN, ZORAN_OCR);
			zror(ZORAN_VSTR_SNAPSHOT,ZORAN_VSTR);
			zror(ZORAN_VDC_VIDEN,ZORAN_VDC);

			/* start single-shot grab */
			zror(ZORAN_VSTR_GRAB, ZORAN_VSTR);
			break;
		 default:
			printk(CARD_INFO "what is this doing on the queue? (kindof=%d)\n",CARD,ztv->workqueue->kindof);
			write_lock(&ztv->lock);
			newitem = ztv->workqueue->next;
			ztv->workqueue->next = 0;
			ztv->workqueue = newitem;
			write_unlock(&ztv->lock);
			if (newitem)
				goto again;	/* yeah, sure.. */
		}
		/* bye for now */
		return;
	}
DEBUG(printk(CARD_DEBUG "nothing in queue\n",CARD));

	/*
	 * What? Even the workqueue is empty? Am i really here
	 * for nothing? Did i come all that way to... do nothing?
	 */

	/* do we need to overlay? */
	if (test_bit(STATE_OVERLAY, &ztv->state))
	{
		/* are we already overlaying? */
		if (!(zrread(ZORAN_OCR) & ZORAN_OCR_OVLEN) ||
		    !(zrread(ZORAN_VDC) & ZORAN_VDC_VIDEN))
		{
DEBUG(printk(CARD_DEBUG "starting overlay\n",CARD));

			read_lock(&ztv->lock);
			zoran_set_geo(ztv,&ztv->overinfo);
			read_unlock(&ztv->lock);

			zror(ZORAN_OCR_OVLEN, ZORAN_OCR);
			zrand(~ZORAN_VSTR_SNAPSHOT,ZORAN_VSTR);
			zror(ZORAN_VDC_VIDEN,ZORAN_VDC);
		}

		/*
		 * leave overlaying on, but turn interrupts off.
		 */
		zrand(~ZORAN_ICR_EN,ZORAN_ICR);
		return;
	}

	/* do we have any VBI idle time processing? */
	if (test_bit(STATE_VBI, &ztv->state))
	{
		struct vidinfo* item;
		struct vidinfo* lastitem;

		/* protect the workqueue */
		write_lock(&ztv->lock);
		lastitem = ztv->workqueue;
		if (lastitem)
			while (lastitem->next) lastitem = lastitem->next;
		for (item=ztv->readinfo; item!=ztv->readinfo+ZORAN_VBI_BUFFERS; item++)
			if (item->next == 0 && item->status == FBUFFER_FREE)
			{
DEBUG(printk(CARD_DEBUG "%p added to queue\n",CARD,item));
				item->status = FBUFFER_BUSY;
				if (!lastitem)
					ztv->workqueue = item;
				else 
					lastitem->next = item;
				lastitem = item;
			}
		write_unlock(&ztv->lock);
		if (ztv->workqueue)
			goto again;	/* hey, _i_ graduated :) */
	}

	/*
	 * Then we must be realy IDLE
	 */
DEBUG(printk(CARD_DEBUG "turning off\n",CARD));
	/* nothing further to do, disable DMA and further IRQs */
	zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);
	zrand(~ZORAN_ICR_EN,ZORAN_ICR);
}

static
void zoran_irq(int irq, void *dev_id, struct pt_regs * regs)
{
	u32 stat,estat;
	int count = 0;
	struct zoran *ztv = dev_id;

	UNUSED(irq); UNUSED(regs);
	for (;;) {
		/* get/clear interrupt status bits */
		stat=zrread(ZORAN_ISR);
		estat=stat & zrread(ZORAN_ICR);
		if (!estat)
			return;
		zrwrite(estat,ZORAN_ISR);
		IDEBUG(printk(CARD_DEBUG "estat %08x\n",CARD,estat));
		IDEBUG(printk(CARD_DEBUG " stat %08x\n",CARD,stat));

		if (estat & ZORAN_ISR_CODE)
		{
			IDEBUG(printk(CARD_DEBUG "CodReplIRQ\n",CARD));
		}
		if (estat & ZORAN_ISR_GIRQ0)
		{
			IDEBUG(printk(CARD_DEBUG "GIRQ0\n",CARD));
			if (!ztv->card->usegirq1)
				reap_states(ztv);
		}
		if (estat & ZORAN_ISR_GIRQ1)
		{
			IDEBUG(printk(CARD_DEBUG "GIRQ1\n",CARD));
			if (ztv->card->usegirq1)
				reap_states(ztv);
		}

		count++;
		if (count > 10)
			printk(CARD_ERR "irq loop %d (%x)\n",CARD,count,estat);
		if (count > 20)
		{
			zrwrite(0, ZORAN_ICR);
			printk(CARD_ERR "IRQ lockup, cleared int mask\n",CARD);
		}
	}
}

static
int zoran_muxsel(struct zoran* ztv, int channel, int norm)
{
	int	rv;

	/* set the new video norm */
	rv = i2c_control_device(&(ztv->i2c), I2C_DRIVERID_VIDEODECODER, DECODER_SET_NORM, &norm);
	if (rv)
		return rv;
	ztv->norm = norm;

	/* map the given channel to the cards decoder's channel */
	channel = ztv->card->video_mux[channel] & CHANNEL_MASK;

	/* set the new channel */
	rv = i2c_control_device(&(ztv->i2c), I2C_DRIVERID_VIDEODECODER, DECODER_SET_INPUT, &channel);
	return rv;
}

/* Tell the interrupt handler what to to.  */
static
void zoran_cap(struct zoran* ztv, int on)
{
DEBUG(printk(CARD_DEBUG "zoran_cap(%d) state=%x\n",CARD,on,ztv->state));

	if (on) {
		ztv->running = 1;

		/*
		 * turn interrupts (back) on. The DMA will be enabled
		 * inside the irq handler when it detects a restart.
		 */
		zror(ZORAN_ICR_EN,ZORAN_ICR);
	}
	else {
		/*
		 * turn both interrupts and DMA off
		 */
		zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);
		zrand(~ZORAN_ICR_EN,ZORAN_ICR);

		ztv->running = 0;
	}
}

static ulong dmask[] = {
	0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFC, 0xFFFFFFF8,
	0xFFFFFFF0, 0xFFFFFFE0, 0xFFFFFFC0, 0xFFFFFF80,
	0xFFFFFF00, 0xFFFFFE00, 0xFFFFFC00, 0xFFFFF800,
	0xFFFFF000, 0xFFFFE000, 0xFFFFC000, 0xFFFF8000,
	0xFFFF0000, 0xFFFE0000, 0xFFFC0000, 0xFFF80000,
	0xFFF00000, 0xFFE00000, 0xFFC00000, 0xFF800000,
	0xFF000000, 0xFE000000, 0xFC000000, 0xF8000000,
	0xF0000000, 0xE0000000, 0xC0000000, 0x80000000
};

static
void zoran_built_overlay(struct zoran* ztv, int count, struct video_clip *vcp)
{
	ulong*	mtop;
	int	ystep = (ztv->vidXshift + ztv->vidWidth+31)/32;	/* next DWORD */
	int	i;

DEBUG(printk(KERN_DEBUG "       overlay at %p, ystep=%d, clips=%d\n",ztv->overinfo.overlay,ystep,count));

	for (i=0; i<count; i++) {
		struct video_clip *vp = vcp+i;
		UNUSED(vp);
DEBUG(printk(KERN_DEBUG "       %d: clip(%d,%d,%d,%d)\n", i,vp->x,vp->y,vp->width,vp->height));
	}

	/*
	 * activate the visible portion of the screen
	 * Note we take some shortcuts here, because we
	 * know the width can never be < 32. (I.e. a DWORD)
	 * We also assume the overlay starts somewhere in
	 * the FIRST dword.
	 */
	{
		int start = ztv->vidXshift;
		ulong firstd = dmask[start];
		ulong lastd = ~dmask[(start + ztv->overinfo.w) & 31];
		mtop = ztv->overinfo.overlay;
		for (i=0; i<ztv->overinfo.h; i++) {
			int w = ztv->vidWidth;
			ulong* line = mtop;
			if (start & 31) {
				*line++ = firstd;
				w -= 32-(start&31);
			}
			memset(line, ~0, w/8);
			if (w & 31)
				line[w/32] = lastd;
			mtop += ystep;
		}
	}

	/* process clipping regions */
	for (i=0; i<count; i++) {
		int h;
		if (vcp->x < 0 || (uint)vcp->x > ztv->overinfo.w ||
		    vcp->y < 0 || vcp->y > ztv->overinfo.h ||
		    vcp->width < 0 || (uint)(vcp->x+vcp->width) > ztv->overinfo.w ||
		    vcp->height < 0 || (vcp->y+vcp->height) > ztv->overinfo.h)
		{
			DEBUG(printk(CARD_DEBUG "invalid clipzone (%d,%d,%d,%d) not in (0,0,%d,%d), adapting\n",CARD,vcp->x,vcp->y,vcp->width,vcp->height,ztv->overinfo.w,ztv->overinfo.h));
			if (vcp->x < 0) vcp->x = 0;
			if ((uint)vcp->x > ztv->overinfo.w) vcp->x = ztv->overinfo.w;
			if (vcp->y < 0) vcp->y = 0;
			if (vcp->y > ztv->overinfo.h) vcp->y = ztv->overinfo.h;
			if (vcp->width < 0) vcp->width = 0;
			if ((uint)(vcp->x+vcp->width) > ztv->overinfo.w) vcp->width = ztv->overinfo.w - vcp->x;
			if (vcp->height < 0) vcp->height = 0;
			if (vcp->y+vcp->height > ztv->overinfo.h) vcp->height = ztv->overinfo.h - vcp->y;
//			continue;
		}

		mtop = &ztv->overinfo.overlay[vcp->y*ystep];
		for (h=0; h<=vcp->height; h++) {
			int w;
			int x = ztv->vidXshift + vcp->x;
			for (w=0; w<=vcp->width; w++) {
				clear_bit(x&31, &mtop[x/32]);
				x++;
			}
			mtop += ystep;
		}
		++vcp;
	}

	mtop = ztv->overinfo.overlay;
	zrwrite(virt_to_bus(mtop), ZORAN_MTOP);
	zrwrite(virt_to_bus(mtop+ystep), ZORAN_MBOT);
	zraor((ztv->vidInterlace*ystep)<<0,~ZORAN_OCR_MASKSTRIDE,ZORAN_OCR);
}

struct tvnorm 
{
	u16 Wt, Wa, Ht, Ha, HStart, VStart;
};

static struct tvnorm tvnorms[] = {
	/* PAL-BDGHI */
/*	{ 864, 720, 625, 576, 131, 21 },*/
/*00*/	{ 864, 768, 625, 576, 81, 17 },
	/* NTSC */
/*01*/	{ 858, 720, 525, 480, 121, 10 },
	/* SECAM */
/*02*/	{ 864, 720, 625, 576, 131, 21 },
	/* BW50 */
/*03*/	{ 864, 720, 625, 576, 131, 21 },
	/* BW60 */
/*04*/	{ 858, 720, 525, 480, 121, 10 }
};
#define TVNORMS (sizeof(tvnorms)/sizeof(tvnorm))

/*
 * Program the chip for a setup as described in the vidinfo struct.
 *
 * Side-effects: calculates vidXshift, vidInterlace,
 * vidHeight, vidWidth which are used in a later stage
 * to calculate the overlay mask
 *
 * This is an internal function, as such it does not check the
 * validity of the struct members... Spectaculair crashes will
 * follow /very/ quick when you're wrong and the chip right :)
 */
static
void zoran_set_geo(struct zoran* ztv, struct vidinfo* i)
{
	ulong	top, bot;
	int	stride;
	int	winWidth, winHeight;
	int	maxWidth, maxHeight, maxXOffset, maxYOffset;
	long	vfec;

DEBUG(printk(CARD_DEBUG "set_geo(rect=(%d,%d,%d,%d), norm=%d, format=%d, bpp=%d, bpl=%d, busadr=%lx, overlay=%p)\n",CARD,i->x,i->y,i->w,i->h,ztv->norm,i->format,i->bpp,i->bpl,i->busadr,i->overlay));

	/*
	 * make sure the DMA transfers are inhibited during our
	 * reprogramming of the chip
	 */
	zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);

	maxWidth = tvnorms[ztv->norm].Wa;
	maxHeight = tvnorms[ztv->norm].Ha/2;
	maxXOffset = tvnorms[ztv->norm].HStart;
	maxYOffset = tvnorms[ztv->norm].VStart;

	/* setup vfec register (keep ExtFl,TopField and VCLKPol settings) */
	vfec = (zrread(ZORAN_VFEC) & (ZORAN_VFEC_EXTFL|ZORAN_VFEC_TOPFIELD|ZORAN_VFEC_VCLKPOL)) |
	       (palette2fmt[i->format].mode & (ZORAN_VFEC_RGB|ZORAN_VFEC_ERRDIF|ZORAN_VFEC_LE|ZORAN_VFEC_PACK24));

	/*
	 * Set top, bottom ptrs. Since these must be DWORD aligned,
	 * possible adjust the x and the width of the window.
	 * so the endposition stay the same. The vidXshift will make
	 * sure we are not writing pixels before the requested x.
	 */
	ztv->vidXshift = 0;
	winWidth = i->w;
	if (winWidth < 0)
		winWidth = -winWidth;
	top = i->busadr + i->x*i->bpp + i->y*i->bpl;
	if (top & 3) {
		ztv->vidXshift = (top & 3) / i->bpp;
		winWidth += ztv->vidXshift;
		DEBUG(printk(KERN_DEBUG "       window-x shifted %d pixels left\n",ztv->vidXshift));
		top &= ~3;
	}

	/*
	 * bottom points to next frame but in interleaved mode we want
	 * to 'mix' the 2 frames to one capture, so 'bot' points to one
	 * (physical) line below the top line.
	 */
	bot = top + i->bpl;
	zrwrite(top,ZORAN_VTOP);
	zrwrite(bot,ZORAN_VBOT);

	/*
	 * Make sure the winWidth is DWORD aligned too,
	 * thereby automaticly making sure the stride to the
	 * next line is DWORD aligned too (as required by spec).
	 */
	if ((winWidth*i->bpp) & 3) {
DEBUG(printk(KERN_DEBUG "       window-width enlarged by %d pixels\n",(winWidth*i->bpp) & 3));
		winWidth += (winWidth*i->bpp) & 3;
	}

	/* determine the DispMode and stride */
	if (i->h >= 0 && i->h <= maxHeight) {
		/* single frame grab suffices for this height. */
		vfec |= ZORAN_VFEC_DISPMOD;
		ztv->vidInterlace = 0;
		stride = i->bpl - (winWidth*i->bpp);
		winHeight = i->h;
	}
	else {
		/* interleaving needed for this height */
		ztv->vidInterlace = 1;
		stride = i->bpl*2 - (winWidth*i->bpp);
		winHeight = i->h/2;
	}
	if (winHeight < 0)	/* can happen for VBI! */
		winHeight = -winHeight;

	/* safety net, sometimes bpl is too short??? */
	if (stride<0) {
DEBUG(printk(CARD_DEBUG "WARNING stride = %d\n",CARD,stride));
		stride = 0;
	}

	zraor((winHeight<<12)|(winWidth<<0),~(ZORAN_VDC_VIDWINHT|ZORAN_VDC_VIDWINWID), ZORAN_VDC);
	zraor(stride<<16,~ZORAN_VSTR_DISPSTRIDE,ZORAN_VSTR);

	/* remember vidWidth, vidHeight for overlay calculations */
	ztv->vidWidth = winWidth;
	ztv->vidHeight = winHeight;
DEBUG(printk(KERN_DEBUG "       top=%08lx, bottom=%08lx\n",top,bot));
DEBUG(printk(KERN_DEBUG "       winWidth=%d, winHeight=%d\n",winWidth,winHeight));
DEBUG(printk(KERN_DEBUG "       maxWidth=%d, maxHeight=%d\n",maxWidth,maxHeight));
DEBUG(printk(KERN_DEBUG "       stride=%d\n",stride));

	/*
	 * determine horizontal scales and crops
	 */
	if (i->w < 0) {
		int Hstart = 1;
		int Hend = Hstart + winWidth;
DEBUG(printk(KERN_DEBUG "       Y: scale=0, start=%d, end=%d\n", Hstart, Hend));
		zraor((Hstart<<10)|(Hend<<0),~(ZORAN_VFEH_HSTART|ZORAN_VFEH_HEND),ZORAN_VFEH);
	}
	else {
		int Wa = maxWidth;
		int X = (winWidth*64+Wa-1)/Wa;
		int We = winWidth*64/X;
		int HorDcm = 64-X;
		int hcrop1 = 2*(Wa-We)/4;
		/*
		 * BUGFIX: Juha Nurmela <junki@qn-lpr2-165.quicknet.inet.fi> 
		 * found the solution to the color phase shift.
		 * See ChangeLog for the full explanation)
		 */
		int Hstart = (maxXOffset + hcrop1) | 1;
		int Hend = Hstart + We - 1;

DEBUG(printk(KERN_DEBUG "       X: scale=%d, start=%d, end=%d\n", HorDcm, Hstart, Hend));

		zraor((Hstart<<10)|(Hend<<0),~(ZORAN_VFEH_HSTART|ZORAN_VFEH_HEND),ZORAN_VFEH);
		vfec |= HorDcm<<14;

		if (HorDcm<16)
			vfec |= ZORAN_VFEC_HFILTER_1; /* no filter */
		else if (HorDcm<32)
			vfec |= ZORAN_VFEC_HFILTER_3; /* 3 tap filter */
		else if (HorDcm<48)
			vfec |= ZORAN_VFEC_HFILTER_4; /* 4 tap filter */
		else	vfec |= ZORAN_VFEC_HFILTER_5; /* 5 tap filter */
	}

	/*
	 * Determine vertical scales and crops
	 *
	 * when height is negative, we want to read starting at line 0
	 * One day someone might need access to these lines...
	 */
	if (i->h < 0) {
		int Vstart = 0;
		int Vend = Vstart + winHeight;
DEBUG(printk(KERN_DEBUG "       Y: scale=0, start=%d, end=%d\n", Vstart, Vend));
		zraor((Vstart<<10)|(Vend<<0),~(ZORAN_VFEV_VSTART|ZORAN_VFEV_VEND),ZORAN_VFEV);
	}
	else {
		int Ha = maxHeight;
		int Y = (winHeight*64+Ha-1)/Ha;
		int He = winHeight*64/Y;
		int VerDcm = 64-Y;
		int vcrop1 = 2*(Ha-He)/4;
		int Vstart = maxYOffset + vcrop1;
		int Vend = Vstart + He - 1;

DEBUG(printk(KERN_DEBUG "       Y: scale=%d, start=%d, end=%d\n", VerDcm, Vstart, Vend));
		zraor((Vstart<<10)|(Vend<<0),~(ZORAN_VFEV_VSTART|ZORAN_VFEV_VEND),ZORAN_VFEV);
		vfec |= VerDcm<<8;
	}

DEBUG(printk(KERN_DEBUG "       F: format=%d(=%s)\n",i->format,palette2fmt[i->format].name));

	/* setup the requested format */
	zrwrite(vfec, ZORAN_VFEC);
}

static
void zoran_common_open(struct zoran* ztv, int flags)
{
	UNUSED(flags);

	/* already opened? */
	if (ztv->users++ != 0)
		return;

	/* unmute audio */
	/* /what/ audio? */

	ztv->state = 0;

	/* setup the encoder to the initial values */
	ztv->picture.colour=254<<7;
	ztv->picture.brightness=128<<8;
	ztv->picture.hue=128<<8;
	ztv->picture.contrast=216<<7;
	i2c_control_device(&ztv->i2c, I2C_DRIVERID_VIDEODECODER, DECODER_SET_PICTURE, &ztv->picture);

	/* default to the composite input since my camera is there */
	zoran_muxsel(ztv, 0, VIDEO_MODE_PAL);
}

static
void zoran_common_close(struct zoran* ztv)
{
	if (--ztv->users != 0)
		return;

	/* mute audio */
	/* /what/ audio? */

	/* stop the chip */
	zoran_cap(ztv, 0);
}

/*
 * Open a zoran card. Right now the flags are just a hack
 */
static int zoran_open(struct video_device *dev, int flags)
{
	struct zoran *ztv = (struct zoran*)dev;
	struct vidinfo* item;
	char* pos;

	DEBUG(printk(CARD_DEBUG "open(dev,%d)\n",CARD,flags));

	/*********************************************
	 * We really should be doing lazy allocing...
	 *********************************************/
	/* allocate a frame buffer */
	if (!ztv->fbuffer)
		ztv->fbuffer = bmalloc(ZORAN_MAX_FBUFSIZE);
	if (!ztv->fbuffer) {
		/* could not get a buffer, bail out */
		return -ENOBUFS;
	}
	/* at this time we _always_ have a framebuffer */
	memset(ztv->fbuffer,0,ZORAN_MAX_FBUFSIZE);

	if (!ztv->overinfo.overlay)
		ztv->overinfo.overlay = kmalloc(1024*1024/8, GFP_KERNEL);
	if (!ztv->overinfo.overlay) {
		/* could not get an overlay buffer, bail out */
		bfree(ztv->fbuffer, ZORAN_MAX_FBUFSIZE);
		return -ENOBUFS;
	}
	/* at this time we _always_ have a overlay */

	/* clear buffer status, and give them a DMAable address */
	pos = ztv->fbuffer;
	for (item=ztv->grabinfo; item!=ztv->grabinfo+ZORAN_MAX_FBUFFERS; item++)
	{
		item->status = FBUFFER_FREE;
		item->memadr = pos;
		item->busadr = virt_to_bus(pos);
		pos += ZORAN_MAX_FBUFFER;
	}

	/* do the common part of all open's */
	zoran_common_open(ztv, flags);

	return 0;
}

static
void zoran_close(struct video_device* dev)
{
	struct zoran *ztv = (struct zoran*)dev;

	DEBUG(printk(CARD_DEBUG "close(dev)\n",CARD));

	/* driver specific closure */
	clear_bit(STATE_OVERLAY, &ztv->state);

	zoran_common_close(ztv);

        /*
         *      This is sucky but right now I can't find a good way to
         *      be sure its safe to free the buffer. We wait 5-6 fields
         *      which is more than sufficient to be sure.
         */
        msleep(100);			/* Wait 1/10th of a second */

	/* free the allocated framebuffer */
	bfree(ztv->fbuffer, ZORAN_MAX_FBUFSIZE);
	ztv->fbuffer = 0;
	kfree(ztv->overinfo.overlay);
	ztv->overinfo.overlay = 0;

}

/*
 * This read function could be used reentrant in a SMP situation.
 *
 * This is made possible by the spinlock which is kept till we
 * found and marked a buffer for our own use. The lock must
 * be released as soon as possible to prevent lock contention.
 */
static
long zoran_read(struct video_device* dev, char* buf, unsigned long count, int nonblock)
{
	struct zoran *ztv = (struct zoran*)dev;
	unsigned long max;
	struct vidinfo* unused = 0;
	struct vidinfo* done = 0;

	DEBUG(printk(CARD_DEBUG "zoran_read(%p,%ld,%d)\n",CARD,buf,count,nonblock));

	/* find ourself a free or completed buffer */
	for (;;) {
		struct vidinfo* item;

		write_lock_irq(&ztv->lock);
		for (item=ztv->grabinfo; item!=ztv->grabinfo+ZORAN_MAX_FBUFFERS; item++)
		{
			if (!unused && item->status == FBUFFER_FREE)
				unused = item;
			if (!done && item->status == FBUFFER_DONE)
				done = item;
		}
		if (done || unused)
			break;

		/* no more free buffers, wait for them. */
		write_unlock_irq(&ztv->lock);
		if (nonblock)
			return -EWOULDBLOCK;
		interruptible_sleep_on(&ztv->grabq);
		if (signal_pending(current))
			return -EINTR;
	}

	/* Do we have 'ready' data? */
	if (!done) {
		/* no? than this will take a while... */
		if (nonblock) {
			write_unlock_irq(&ztv->lock);
			return -EWOULDBLOCK;
		}

		/* mark the unused buffer as wanted */
		unused->status = FBUFFER_BUSY;
		unused->w = 320;
		unused->h = 240;
		unused->format = VIDEO_PALETTE_RGB24;
		unused->bpp = palette2fmt[unused->format].bpp;
		unused->bpl = unused->w * unused->bpp;
		unused->next = 0;
		{ /* add to tail of queue */
		  struct vidinfo* oldframe = ztv->workqueue;
		  if (!oldframe) ztv->workqueue = unused;
		  else {
		    while (oldframe->next) oldframe = oldframe->next;
		    oldframe->next = unused;
		  }
		}
		write_unlock_irq(&ztv->lock);

		/* tell the state machine we want it filled /NOW/ */
		zoran_cap(ztv, 1);

		/* wait till this buffer gets grabbed */
		wait_event_interruptible(ztv->grabq,
				(unused->status != FBUFFER_BUSY));
		/* see if a signal did it */
		if (signal_pending(current))
			return -EINTR;
		done = unused;
	}
	else
		write_unlock_irq(&ztv->lock);

	/* Yes! we got data! */
	max = done->bpl * done->h;
	if (count > max)
		count = max;
	if (copy_to_user((void*)buf, done->memadr, count))
		count = -EFAULT;

	/* keep the engine running */
	done->status = FBUFFER_FREE;
//	zoran_cap(ztv,1);

	/* tell listeners this buffer became free */
	wake_up_interruptible(&ztv->grabq);

	/* goodbye */
	DEBUG(printk(CARD_DEBUG "zoran_read() returns %lu\n",CARD,count));
	return count;
}

static
long zoran_write(struct video_device* dev, const char* buf, unsigned long count, int nonblock)
{
	struct zoran *ztv = (struct zoran *)dev;
	UNUSED(ztv); UNUSED(dev); UNUSED(buf); UNUSED(count); UNUSED(nonblock);
	DEBUG(printk(CARD_DEBUG "zoran_write\n",CARD));
	return -EINVAL;
}

static
unsigned int zoran_poll(struct video_device *dev, struct file *file, poll_table *wait)
{
	struct zoran *ztv = (struct zoran *)dev;
	struct vidinfo* item;
	unsigned int mask = 0;

	poll_wait(file, &ztv->grabq, wait);

	for (item=ztv->grabinfo; item!=ztv->grabinfo+ZORAN_MAX_FBUFFERS; item++)
		if (item->status == FBUFFER_DONE)
		{
			mask |= (POLLIN | POLLRDNORM);
			break;
		}

	DEBUG(printk(CARD_DEBUG "zoran_poll()=%x\n",CARD,mask));

	return mask;
}

/* append a new clipregion to the vector of video_clips */
static
void new_clip(struct video_window* vw, struct video_clip* vcp, int x, int y, int w, int h)
{
	vcp[vw->clipcount].x = x;
	vcp[vw->clipcount].y = y;
	vcp[vw->clipcount].width = w;
	vcp[vw->clipcount].height = h;
	vw->clipcount++;
}

static
int zoran_ioctl(struct video_device* dev, unsigned int cmd, void *arg)
{
	struct zoran* ztv = (struct zoran*)dev;

	switch (cmd) {
	 case VIDIOCGCAP:
	 {
		struct video_capability c;
		DEBUG(printk(CARD_DEBUG "VIDIOCGCAP\n",CARD));

		strcpy(c.name,ztv->video_dev.name);
		c.type = VID_TYPE_CAPTURE|
			 VID_TYPE_OVERLAY|
			 VID_TYPE_CLIPPING|
			 VID_TYPE_FRAMERAM|
			 VID_TYPE_SCALES;
		if (ztv->have_tuner)
			c.type |= VID_TYPE_TUNER;
		if (ztv->have_decoder) {
			c.channels = ztv->card->video_inputs;
			c.audios = ztv->card->audio_inputs;
		} else
			/* no decoder -> no channels */
			c.channels = c.audios = 0;
		c.maxwidth = 768;
		c.maxheight = 576;
		c.minwidth = 32;
		c.minheight = 32;
		if (copy_to_user(arg,&c,sizeof(c)))
			return -EFAULT;
		break;
	 }

	 case VIDIOCGCHAN:
	 {
		struct video_channel v;
		int mux;
		if (copy_from_user(&v, arg,sizeof(v)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCGCHAN(%d)\n",CARD,v.channel));
		v.flags=VIDEO_VC_AUDIO
#ifdef VIDEO_VC_NORM
			|VIDEO_VC_NORM
#endif
			;
		v.tuners=0;
		v.type=VIDEO_TYPE_CAMERA;
#ifdef I_EXPECT_POSSIBLE_NORMS_IN_THE_API
		v.norm=VIDEO_MODE_PAL|
		       VIDEO_MODE_NTSC|
		       VIDEO_MODE_SECAM;
#else
		v.norm=VIDEO_MODE_PAL;
#endif
		/* too many inputs? no decoder -> no channels */
		if (!ztv->have_decoder || v.channel < 0 ||  v.channel >= ztv->card->video_inputs)
			return -EINVAL;

		/* now determine the name of the channel */
		mux = ztv->card->video_mux[v.channel];
		if (mux & IS_TUNER) {
			/* lets assume only one tuner, yes? */
			strcpy(v.name,"Television");
			v.type = VIDEO_TYPE_TV;
			if (ztv->have_tuner) {
				v.flags |= VIDEO_VC_TUNER;
				v.tuners = 1;
			}
		}
		else if (mux & IS_SVHS)
			sprintf(v.name,"S-Video-%d",v.channel);
		else
			sprintf(v.name,"CVBS-%d",v.channel);

		if (copy_to_user(arg,&v,sizeof(v)))
			return -EFAULT;
		break;
	 }
	 case VIDIOCSCHAN:
	 {	/* set video channel */
		struct video_channel v;
		if (copy_from_user(&v, arg,sizeof(v)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCSCHAN(%d,%d)\n",CARD,v.channel,v.norm));

		/* too many inputs? no decoder -> no channels */
		if (!ztv->have_decoder || v.channel >= ztv->card->video_inputs || v.channel < 0)
			return -EINVAL;

		if (v.norm != VIDEO_MODE_PAL &&
		    v.norm != VIDEO_MODE_NTSC &&
		    v.norm != VIDEO_MODE_SECAM &&
		    v.norm != VIDEO_MODE_AUTO)
			return -EOPNOTSUPP;

		/* make it happen, nr1! */
		return zoran_muxsel(ztv,v.channel,v.norm);
	 }

	 case VIDIOCGTUNER:
	 {
		struct video_tuner v;
		if (copy_from_user(&v, arg,sizeof(v)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCGTUNER(%d)\n",CARD,v.tuner));

		/* Only no or one tuner for now */
		if (!ztv->have_tuner || v.tuner)
			return -EINVAL;

		strcpy(v.name,"Television");
		v.rangelow  = 0;
		v.rangehigh = ~0;
		v.flags     = VIDEO_TUNER_PAL|VIDEO_TUNER_NTSC|VIDEO_TUNER_SECAM;
		v.mode      = ztv->norm;
		v.signal    = 0xFFFF; /* unknown */

		if (copy_to_user(arg,&v,sizeof(v)))
			return -EFAULT;
		break;
	 }
	 case VIDIOCSTUNER:
	 {
		struct video_tuner v;
		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCSTUNER(%d,%d)\n",CARD,v.tuner,v.mode));

		/* Only no or one tuner for now */
		if (!ztv->have_tuner || v.tuner)
			return -EINVAL;

		/* and it only has certain valid modes */
		if( v.mode != VIDEO_MODE_PAL &&
		    v.mode != VIDEO_MODE_NTSC &&
		    v.mode != VIDEO_MODE_SECAM)
			return -EOPNOTSUPP;

		/* engage! */
		return zoran_muxsel(ztv,v.tuner,v.mode);
	 }

	 case VIDIOCGPICT:
	 {
		struct video_picture p = ztv->picture;
		DEBUG(printk(CARD_DEBUG "VIDIOCGPICT\n",CARD));
		p.depth = ztv->depth;
		switch (p.depth) {
		 case  8: p.palette=VIDEO_PALETTE_YUV422;
			  break;
		 case 15: p.palette=VIDEO_PALETTE_RGB555;
			  break;
		 case 16: p.palette=VIDEO_PALETTE_RGB565;
			  break;
		 case 24: p.palette=VIDEO_PALETTE_RGB24;
			  break;
		 case 32: p.palette=VIDEO_PALETTE_RGB32;
			  break;
		}
		if (copy_to_user(arg, &p, sizeof(p)))
			return -EFAULT;
		break;
	 }
	 case VIDIOCSPICT:
	 {
		struct video_picture p;
		if (copy_from_user(&p, arg,sizeof(p)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCSPICT(%d,%d,%d,%d,%d,%d,%d)\n",CARD,p.brightness,p.hue,p.colour,p.contrast,p.whiteness,p.depth,p.palette));

		/* depth must match with framebuffer */
		if (p.depth != ztv->depth)
			return -EINVAL;

		/* check if palette matches this bpp */
		if (p.palette>NRPALETTES ||
		    palette2fmt[p.palette].bpp != ztv->overinfo.bpp)
			return -EINVAL;

		write_lock_irq(&ztv->lock);
		ztv->overinfo.format = p.palette;
		ztv->picture = p;
		write_unlock_irq(&ztv->lock);

		/* tell the decoder */
		i2c_control_device(&ztv->i2c, I2C_DRIVERID_VIDEODECODER, DECODER_SET_PICTURE, &p);
		break;
	 }

	 case VIDIOCGWIN:
	 {
		struct video_window vw;
		DEBUG(printk(CARD_DEBUG "VIDIOCGWIN\n",CARD));
		read_lock(&ztv->lock);
		vw.x      = ztv->overinfo.x;
		vw.y      = ztv->overinfo.y;
		vw.width  = ztv->overinfo.w;
		vw.height = ztv->overinfo.h;
		vw.chromakey= 0;
		vw.flags  = 0;
		if (ztv->vidInterlace)
			vw.flags|=VIDEO_WINDOW_INTERLACE;
		read_unlock(&ztv->lock);
		if (copy_to_user(arg,&vw,sizeof(vw)))
			return -EFAULT;
		break;
	 }
	 case VIDIOCSWIN:
	 {
		struct video_window vw;
		struct video_clip *vcp;
		int on;
		if (copy_from_user(&vw,arg,sizeof(vw)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCSWIN(%d,%d,%d,%d,%x,%d)\n",CARD,vw.x,vw.y,vw.width,vw.height,vw.flags,vw.clipcount));

		if (vw.flags)
			return -EINVAL;

		if (vw.clipcount <0 || vw.clipcount>256)
			return -EDOM;   /* Too many! */

		/*
		 *      Do any clips.
		 */
		vcp = vmalloc(sizeof(struct video_clip)*(vw.clipcount+4));
		if (vcp==NULL)
			return -ENOMEM;
		if (vw.clipcount && copy_from_user(vcp,vw.clips,sizeof(struct video_clip)*vw.clipcount)) {
			vfree(vcp);
			return -EFAULT;
		}

		on = ztv->running;
		if (on)
			zoran_cap(ztv, 0);

		/*
		 * strange, it seems xawtv sometimes calls us with 0
		 * width and/or height. Ignore these values
		 */
		if (vw.x == 0)
			vw.x = ztv->overinfo.x;
		if (vw.y == 0)
			vw.y = ztv->overinfo.y;

		/* by now we are committed to the new data... */
		write_lock_irq(&ztv->lock);
		ztv->overinfo.x = vw.x;
		ztv->overinfo.y = vw.y;
		ztv->overinfo.w = vw.width;
		ztv->overinfo.h = vw.height;
		write_unlock_irq(&ztv->lock);

		/*
		 *      Impose display clips
		 */
		if (vw.x+vw.width > ztv->swidth)
			new_clip(&vw, vcp, ztv->swidth-vw.x, 0, vw.width-1, vw.height-1);
		if (vw.y+vw.height > ztv->sheight)
			new_clip(&vw, vcp, 0, ztv->sheight-vw.y, vw.width-1, vw.height-1);

		/* built the requested clipping zones */
		zoran_set_geo(ztv, &ztv->overinfo);
		zoran_built_overlay(ztv, vw.clipcount, vcp);
		vfree(vcp);

		/* if we were on, restart the video engine */
		if (on)
			zoran_cap(ztv, 1);
		break;
	 }

	 case VIDIOCCAPTURE:
	 {
		int v;
		if (get_user(v, (int *)arg))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCCAPTURE(%d)\n",CARD,v));

		if (v==0) {
			clear_bit(STATE_OVERLAY, &ztv->state);
			zoran_cap(ztv, 1);
		}
		else {
			/* is VIDIOCSFBUF, VIDIOCSWIN done? */
			if (ztv->overinfo.busadr==0 || ztv->overinfo.w==0 || ztv->overinfo.h==0)
				return -EINVAL;

			set_bit(STATE_OVERLAY, &ztv->state);
			zoran_cap(ztv, 1);
		}
		break;
	 }

	 case VIDIOCGFBUF:
	 {
		struct video_buffer v;
		DEBUG(printk(CARD_DEBUG "VIDIOCGFBUF\n",CARD));
		read_lock(&ztv->lock);
		v.base   = (void *)ztv->overinfo.busadr;
		v.height = ztv->sheight;
		v.width  = ztv->swidth;
		v.depth  = ztv->depth;
		v.bytesperline = ztv->overinfo.bpl;
		read_unlock(&ztv->lock);
		if(copy_to_user(arg, &v,sizeof(v)))
			return -EFAULT;
		break;
	 }
	 case VIDIOCSFBUF:
	 {
		struct video_buffer v;
		if(!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&v, arg,sizeof(v)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCSFBUF(%p,%d,%d,%d,%d)\n",CARD,v.base, v.width,v.height,v.depth,v.bytesperline));

		if (v.depth!=15 && v.depth!=16 && v.depth!=24 && v.depth!=32)
			return -EINVAL;
		if (v.bytesperline<1)
			return -EINVAL;
		if (ztv->running)
			return -EBUSY;
		write_lock_irq(&ztv->lock);
		ztv->overinfo.busadr  = (ulong)v.base;
		ztv->sheight      = v.height;
		ztv->swidth       = v.width;
		ztv->depth        = v.depth;		/* bits per pixel */
		ztv->overinfo.bpp = ((v.depth+1)&0x38)/8;/* bytes per pixel */
		ztv->overinfo.bpl = v.bytesperline;	/* bytes per line */
		write_unlock_irq(&ztv->lock);
		break;
	 }

	 case VIDIOCKEY:
	 {
		/* Will be handled higher up .. */
		break;
	 }

	 case VIDIOCSYNC:
	 {
		int i;
		if (get_user(i, (int *) arg))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDEOCSYNC(%d)\n",CARD,i));
		if (i<0 || i>ZORAN_MAX_FBUFFERS)
			return -EINVAL;
		switch (ztv->grabinfo[i].status) {
		 case FBUFFER_FREE:
			return -EINVAL;
		 case FBUFFER_BUSY:
			/* wait till this buffer gets grabbed */
			wait_event_interruptible(ztv->grabq,
					(ztv->grabinfo[i].status != FBUFFER_BUSY));
			/* see if a signal did it */
			if (signal_pending(current))
				return -EINTR;
			/* don't fall through; a DONE buffer is not UNUSED */
			break;
		 case FBUFFER_DONE:
			ztv->grabinfo[i].status = FBUFFER_FREE;
			/* tell ppl we have a spare buffer */
			wake_up_interruptible(&ztv->grabq);
			break;
		}
		DEBUG(printk(CARD_DEBUG "VIDEOCSYNC(%d) returns\n",CARD,i));
		break;
	 }

	 case VIDIOCMCAPTURE:
	 {
		struct video_mmap vm;
		struct vidinfo* frame;
		if (copy_from_user(&vm,arg,sizeof(vm)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCMCAPTURE(%d,(%d,%d),%d)\n",CARD,vm.frame,vm.width,vm.height,vm.format));
		if (vm.frame<0 || vm.frame>ZORAN_MAX_FBUFFERS ||
		    vm.width<32 || vm.width>768 ||
		    vm.height<32 || vm.height>576 ||
		    vm.format>NRPALETTES ||
		    palette2fmt[vm.format].mode == 0)
			return -EINVAL;

		/* we are allowed to take over UNUSED and DONE buffers */
		frame = &ztv->grabinfo[vm.frame];
		if (frame->status == FBUFFER_BUSY)
			return -EBUSY;

		/* setup the other parameters if they are given */
		write_lock_irq(&ztv->lock);
		frame->w = vm.width;
		frame->h = vm.height;
		frame->format = vm.format;
		frame->bpp = palette2fmt[frame->format].bpp;
		frame->bpl = frame->w*frame->bpp;
		frame->status = FBUFFER_BUSY;
		frame->next = 0;
		{ /* add to tail of queue */
		  struct vidinfo* oldframe = ztv->workqueue;
		  if (!oldframe) ztv->workqueue = frame;
		  else {
		    while (oldframe->next) oldframe = oldframe->next;
		    oldframe->next = frame;
		  }
		}
		write_unlock_irq(&ztv->lock);
		zoran_cap(ztv, 1);
		break;
	 }

	 case VIDIOCGMBUF:
	 {
		struct video_mbuf mb;
		int i;
		DEBUG(printk(CARD_DEBUG "VIDIOCGMBUF\n",CARD));
		mb.size = ZORAN_MAX_FBUFSIZE;
		mb.frames = ZORAN_MAX_FBUFFERS;
		for (i=0; i<ZORAN_MAX_FBUFFERS; i++)
			mb.offsets[i] = i*ZORAN_MAX_FBUFFER;
		if(copy_to_user(arg, &mb,sizeof(mb)))
			return -EFAULT;
		break;
	 }

	 case VIDIOCGUNIT:
	 {
		struct video_unit vu;
		DEBUG(printk(CARD_DEBUG "VIDIOCGUNIT\n",CARD));
		vu.video = ztv->video_dev.minor;
		vu.vbi = ztv->vbi_dev.minor;
		vu.radio = VIDEO_NO_UNIT;
		vu.audio = VIDEO_NO_UNIT;
		vu.teletext = VIDEO_NO_UNIT;
		if(copy_to_user(arg, &vu,sizeof(vu)))
			return -EFAULT;
		break;
	 }

	 case VIDIOCGFREQ:
	 {
		unsigned long v = ztv->tuner_freq;
		if (copy_to_user(arg,&v,sizeof(v)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCGFREQ\n",CARD));
		break;
	 }
	 case VIDIOCSFREQ:
	 {
		unsigned long v;
		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCSFREQ\n",CARD));

		if (ztv->have_tuner) {
			int fixme = v;
			if (i2c_control_device(&(ztv->i2c), I2C_DRIVERID_TUNER, TUNER_SET_TVFREQ, &fixme) < 0)
				return -EAGAIN;
		}
		ztv->tuner_freq = v;
		break;
	 }

	 /* Why isn't this in the API?
	  * And why doesn't it take a buffer number?
	 case BTTV_FIELDNR: 
	 {
		unsigned long v = ztv->lastfieldnr;
		if (copy_to_user(arg,&v,sizeof(v)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "BTTV_FIELDNR\n",CARD));
		break;
	 }
	 */

	 default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static
int zoran_mmap(struct vm_area_struct *vma, struct video_device* dev, const char* adr, unsigned long size)
{
	struct zoran* ztv = (struct zoran*)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long pos;

	DEBUG(printk(CARD_DEBUG "zoran_mmap(0x%p,%ld)\n",CARD,adr,size));

	/* sanity checks */
	if (size > ZORAN_MAX_FBUFSIZE || !ztv->fbuffer)
		return -EINVAL;

	/* start mapping the whole shabang to user memory */
	pos = (unsigned long)ztv->fbuffer;
	while (size>0) {
		unsigned long pfn = virt_to_phys((void*)pos) >> PAGE_SHIFT;
		if (remap_pfn_range(vma, start, pfn, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	return 0;
}

static struct video_device zr36120_template=
{
	.owner		= THIS_MODULE,
	.name		= "UNSET",
	.type		= VID_TYPE_TUNER|VID_TYPE_CAPTURE|VID_TYPE_OVERLAY,
	.hardware	= VID_HARDWARE_ZR36120,
	.open		= zoran_open,
	.close		= zoran_close,
	.read		= zoran_read,
	.write		= zoran_write,
	.poll		= zoran_poll,
	.ioctl		= zoran_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.mmap		= zoran_mmap,
	.minor		= -1,
};

static
int vbi_open(struct video_device *dev, int flags)
{
	struct zoran *ztv = dev->priv;
	struct vidinfo* item;

	DEBUG(printk(CARD_DEBUG "vbi_open(dev,%d)\n",CARD,flags));

	/*
	 * During VBI device open, we continiously grab VBI-like
	 * data in the vbi buffer when we have nothing to do.
	 * Only when there is an explicit request for VBI data
	 * (read call) we /force/ a read.
	 */

	/* allocate buffers */
	for (item=ztv->readinfo; item!=ztv->readinfo+ZORAN_VBI_BUFFERS; item++)
	{
		item->status = FBUFFER_FREE;

		/* alloc */
		if (!item->memadr) {
			item->memadr = bmalloc(ZORAN_VBI_BUFSIZE);
			if (!item->memadr) {
				/* could not get a buffer, bail out */
				while (item != ztv->readinfo) {
					item--;
					bfree(item->memadr, ZORAN_VBI_BUFSIZE);
					item->memadr = 0;
					item->busadr = 0;
				}
				return -ENOBUFS;
			}
		}

		/* determine the DMAable address */
		item->busadr = virt_to_bus(item->memadr);
	}

	/* do the common part of all open's */
	zoran_common_open(ztv, flags);

	set_bit(STATE_VBI, &ztv->state);
	/* start read-ahead */
	zoran_cap(ztv, 1);

	return 0;
}

static
void vbi_close(struct video_device *dev)
{
	struct zoran *ztv = dev->priv;
	struct vidinfo* item;

	DEBUG(printk(CARD_DEBUG "vbi_close(dev)\n",CARD));

	/* driver specific closure */
	clear_bit(STATE_VBI, &ztv->state);

	zoran_common_close(ztv);

        /*
         *      This is sucky but right now I can't find a good way to
         *      be sure its safe to free the buffer. We wait 5-6 fields
         *      which is more than sufficient to be sure.
         */
        msleep(100);			/* Wait 1/10th of a second */

	for (item=ztv->readinfo; item!=ztv->readinfo+ZORAN_VBI_BUFFERS; item++)
	{
		if (item->memadr)
			bfree(item->memadr, ZORAN_VBI_BUFSIZE);
		item->memadr = 0;
	}

}

/*
 * This read function could be used reentrant in a SMP situation.
 *
 * This is made possible by the spinlock which is kept till we
 * found and marked a buffer for our own use. The lock must
 * be released as soon as possible to prevent lock contention.
 */
static
long vbi_read(struct video_device* dev, char* buf, unsigned long count, int nonblock)
{
	struct zoran *ztv = dev->priv;
	unsigned long max;
	struct vidinfo* unused = 0;
	struct vidinfo* done = 0;

	DEBUG(printk(CARD_DEBUG "vbi_read(0x%p,%ld,%d)\n",CARD,buf,count,nonblock));

	/* find ourself a free or completed buffer */
	for (;;) {
		struct vidinfo* item;

		write_lock_irq(&ztv->lock);
		for (item=ztv->readinfo; item!=ztv->readinfo+ZORAN_VBI_BUFFERS; item++) {
			if (!unused && item->status == FBUFFER_FREE)
				unused = item;
			if (!done && item->status == FBUFFER_DONE)
				done = item;
		}
		if (done || unused)
			break;

		/* no more free buffers, wait for them. */
		write_unlock_irq(&ztv->lock);
		if (nonblock)
			return -EWOULDBLOCK;
		interruptible_sleep_on(&ztv->vbiq);
		if (signal_pending(current))
			return -EINTR;
	}

	/* Do we have 'ready' data? */
	if (!done) {
		/* no? than this will take a while... */
		if (nonblock) {
			write_unlock_irq(&ztv->lock);
			return -EWOULDBLOCK;
		}
		
		/* mark the unused buffer as wanted */
		unused->status = FBUFFER_BUSY;
		unused->next = 0;
		{ /* add to tail of queue */
		  struct vidinfo* oldframe = ztv->workqueue;
		  if (!oldframe) ztv->workqueue = unused;
		  else {
		    while (oldframe->next) oldframe = oldframe->next;
		    oldframe->next = unused;
		  }
		}
		write_unlock_irq(&ztv->lock);

		/* tell the state machine we want it filled /NOW/ */
		zoran_cap(ztv, 1);

		/* wait till this buffer gets grabbed */
		wait_event_interruptible(ztv->vbiq,
				(unused->status != FBUFFER_BUSY));
		/* see if a signal did it */
		if (signal_pending(current))
			return -EINTR;
		done = unused;
	}
	else
		write_unlock_irq(&ztv->lock);

	/* Yes! we got data! */
	max = done->bpl * -done->h;
	if (count > max)
		count = max;

	/* check if the user gave us enough room to write the data */
	if (!access_ok(VERIFY_WRITE, buf, count)) {
		count = -EFAULT;
		goto out;
	}

	/*
	 * Now transform/strip the data from YUV to Y-only
	 * NB. Assume the Y is in the LSB of the YUV data.
	 */
	{
	unsigned char* optr = buf;
	unsigned char* eptr = buf+count;

	/* are we beeing accessed from an old driver? */
	if (count == 2*19*2048) {
		/*
		 * Extreme HACK, old VBI programs expect 2048 points
		 * of data, and we only got 864 orso. Double each 
		 * datapoint and clear the rest of the line.
		 * This way we have appear to have a
		 * sample_frequency of 29.5 Mc.
		 */
		int x,y;
		unsigned char* iptr = done->memadr+1;
		for (y=done->h; optr<eptr && y<0; y++)
		{
			/* copy to doubled data to userland */
			for (x=0; optr+1<eptr && x<-done->w; x++)
			{
				unsigned char a = iptr[x*2];
				__put_user(a, optr++);
				__put_user(a, optr++);
			}
			/* and clear the rest of the line */
			for (x*=2; optr<eptr && x<done->bpl; x++)
				__put_user(0, optr++);
			/* next line */
			iptr += done->bpl;
		}
	}
	else {
		/*
		 * Other (probably newer) programs asked
		 * us what geometry we are using, and are
		 * reading the correct size.
		 */
		int x,y;
		unsigned char* iptr = done->memadr+1;
		for (y=done->h; optr<eptr && y<0; y++)
		{
			/* copy to doubled data to userland */
			for (x=0; optr<eptr && x<-done->w; x++)
				__put_user(iptr[x*2], optr++);
			/* and clear the rest of the line */
			for (;optr<eptr && x<done->bpl; x++)
				__put_user(0, optr++);
			/* next line */
			iptr += done->bpl;
		}
	}

	/* API compliance:
	 * place the framenumber (half fieldnr) in the last long
	 */
	__put_user(done->fieldnr/2, ((ulong*)eptr)[-1]);
	}

	/* keep the engine running */
	done->status = FBUFFER_FREE;
	zoran_cap(ztv, 1);

	/* tell listeners this buffer just became free */
	wake_up_interruptible(&ztv->vbiq);

	/* goodbye */
out:
	DEBUG(printk(CARD_DEBUG "vbi_read() returns %lu\n",CARD,count));
	return count;
}

static
unsigned int vbi_poll(struct video_device *dev, struct file *file, poll_table *wait)
{
	struct zoran *ztv = dev->priv;
	struct vidinfo* item;
	unsigned int mask = 0;

	poll_wait(file, &ztv->vbiq, wait);

	for (item=ztv->readinfo; item!=ztv->readinfo+ZORAN_VBI_BUFFERS; item++)
		if (item->status == FBUFFER_DONE)
		{
			mask |= (POLLIN | POLLRDNORM);
			break;
		}

	DEBUG(printk(CARD_DEBUG "vbi_poll()=%x\n",CARD,mask));

	return mask;
}

static
int vbi_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct zoran* ztv = dev->priv;

	switch (cmd) {
	 case VIDIOCGVBIFMT:
	 {
		struct vbi_format f;
		DEBUG(printk(CARD_DEBUG "VIDIOCGVBIINFO\n",CARD));
		f.sampling_rate = 14750000UL;
		f.samples_per_line = -ztv->readinfo[0].w;
		f.sample_format = VIDEO_PALETTE_RAW;
		f.start[0] = f.start[1] = ztv->readinfo[0].y;
		f.start[1] += 312;
		f.count[0] = f.count[1] = -ztv->readinfo[0].h;
		f.flags = VBI_INTERLACED;
		if (copy_to_user(arg,&f,sizeof(f)))
			return -EFAULT;
		break;
	 }
	 case VIDIOCSVBIFMT:
	 {
		struct vbi_format f;
		int i;
		if (copy_from_user(&f, arg,sizeof(f)))
			return -EFAULT;
		DEBUG(printk(CARD_DEBUG "VIDIOCSVBIINFO(%d,%d,%d,%d,%d,%d,%d,%x)\n",CARD,f.sampling_rate,f.samples_per_line,f.sample_format,f.start[0],f.start[1],f.count[0],f.count[1],f.flags));

		/* lots of parameters are fixed... (PAL) */
		if (f.sampling_rate != 14750000UL ||
		    f.samples_per_line > 864 ||
		    f.sample_format != VIDEO_PALETTE_RAW ||
		    f.start[0] < 0 ||
		    f.start[0] != f.start[1]-312 ||
		    f.count[0] != f.count[1] ||
		    f.start[0]+f.count[0] >= 288 ||
		    f.flags != VBI_INTERLACED)
			return -EINVAL;

		write_lock_irq(&ztv->lock);
		ztv->readinfo[0].y = f.start[0];
		ztv->readinfo[0].w = -f.samples_per_line;
		ztv->readinfo[0].h = -f.count[0];
		ztv->readinfo[0].bpl = f.samples_per_line*ztv->readinfo[0].bpp;
		for (i=1; i<ZORAN_VBI_BUFFERS; i++)
			ztv->readinfo[i] = ztv->readinfo[i];
		write_unlock_irq(&ztv->lock);
		break;
	 }
	 default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static struct video_device vbi_template=
{
	.owner		= THIS_MODULE,
	.name		= "UNSET",
	.type		= VID_TYPE_CAPTURE|VID_TYPE_TELETEXT,
	.hardware	= VID_HARDWARE_ZR36120,
	.open		= vbi_open,
	.close		= vbi_close,
	.read		= vbi_read,
	.write		= zoran_write,
	.poll		= vbi_poll,
	.ioctl		= vbi_ioctl,
	.minor		= -1,
};

/*
 *      Scan for a Zoran chip, request the irq and map the io memory
 */
static
int __init find_zoran(void)
{
	int result;
	struct zoran *ztv;
	struct pci_dev *dev = NULL;
	unsigned char revision;
	int zoran_num=0;

	while ((dev = pci_find_device(PCI_VENDOR_ID_ZORAN,PCI_DEVICE_ID_ZORAN_36120, dev)))
	{
		/* Ok, a ZR36120/ZR36125 found! */
		ztv = &zorans[zoran_num];
		ztv->dev = dev;

		if (pci_enable_device(dev))
			return -EIO;

		pci_read_config_byte(dev, PCI_CLASS_REVISION, &revision);
		printk(KERN_INFO "zoran: Zoran %x (rev %d) ",
			dev->device, revision);
		printk("bus: %d, devfn: %d, irq: %d, ",
			dev->bus->number, dev->devfn, dev->irq);
		printk("memory: 0x%08lx.\n", ztv->zoran_adr);

		ztv->zoran_mem = ioremap(ztv->zoran_adr, 0x1000);
		DEBUG(printk(KERN_DEBUG "zoran: mapped-memory at 0x%p\n",ztv->zoran_mem));

		result = request_irq(dev->irq, zoran_irq,
			SA_SHIRQ|SA_INTERRUPT,"zoran", ztv);
		if (result==-EINVAL)
		{
			iounmap(ztv->zoran_mem);
			printk(KERN_ERR "zoran: Bad irq number or handler\n");
			return -EINVAL;
		}
		if (result==-EBUSY)
			printk(KERN_ERR "zoran: IRQ %d busy, change your PnP config in BIOS\n",dev->irq);
		if (result < 0) {
			iounmap(ztv->zoran_mem);
			return result;
		}
		/* Enable bus-mastering */
		pci_set_master(dev);

		zoran_num++;
	}
	if(zoran_num)
		printk(KERN_INFO "zoran: %d Zoran card(s) found.\n",zoran_num);
	return zoran_num;
}

static
int __init init_zoran(int card)
{
	struct zoran *ztv = &zorans[card];
	int	i;

	/* if the given cardtype valid? */
	if (cardtype[card]>=NRTVCARDS) {
		printk(KERN_INFO "invalid cardtype(%d) detected\n",cardtype[card]);
		return -1;
	}

	/* reset the zoran */
	zrand(~ZORAN_PCI_SOFTRESET,ZORAN_PCI);
	udelay(10);
	zror(ZORAN_PCI_SOFTRESET,ZORAN_PCI);
	udelay(10);

	/* zoran chip specific details */
	ztv->card = tvcards+cardtype[card];	/* point to the selected card */
	ztv->norm = 0;				/* PAL */
	ztv->tuner_freq = 0;

	/* videocard details */
	ztv->swidth = 800;
	ztv->sheight = 600;
	ztv->depth = 16;

	/* State details */
	ztv->fbuffer = 0;
	ztv->overinfo.kindof = FBUFFER_OVERLAY;
	ztv->overinfo.status = FBUFFER_FREE;
	ztv->overinfo.x = 0;
	ztv->overinfo.y = 0;
	ztv->overinfo.w = 768; /* 640 */
	ztv->overinfo.h = 576; /* 480 */
	ztv->overinfo.format = VIDEO_PALETTE_RGB565;
	ztv->overinfo.bpp = palette2fmt[ztv->overinfo.format].bpp;
	ztv->overinfo.bpl = ztv->overinfo.bpp*ztv->swidth;
	ztv->overinfo.busadr = 0;
	ztv->overinfo.memadr = 0;
	ztv->overinfo.overlay = 0;
	for (i=0; i<ZORAN_MAX_FBUFFERS; i++) {
		ztv->grabinfo[i] = ztv->overinfo;
		ztv->grabinfo[i].kindof = FBUFFER_GRAB;
	}
	init_waitqueue_head(&ztv->grabq);

	/* VBI details */
	ztv->readinfo[0] = ztv->overinfo;
	ztv->readinfo[0].kindof = FBUFFER_VBI;
	ztv->readinfo[0].w = -864;
	ztv->readinfo[0].h = -38;
	ztv->readinfo[0].format = VIDEO_PALETTE_YUV422;
	ztv->readinfo[0].bpp = palette2fmt[ztv->readinfo[0].format].bpp;
	ztv->readinfo[0].bpl = 1024*ztv->readinfo[0].bpp;
	for (i=1; i<ZORAN_VBI_BUFFERS; i++)
		ztv->readinfo[i] = ztv->readinfo[0];
	init_waitqueue_head(&ztv->vbiq);

	/* maintenance data */
	ztv->have_decoder = 0;
	ztv->have_tuner = 0;
	ztv->tuner_type = 0;
	ztv->running = 0;
	ztv->users = 0;
	rwlock_init(&ztv->lock);
	ztv->workqueue = 0;
	ztv->fieldnr = 0;
	ztv->lastfieldnr = 0;

	if (triton1)
		zrand(~ZORAN_VDC_TRICOM, ZORAN_VDC);

	/* external FL determines TOP frame */
	zror(ZORAN_VFEC_EXTFL, ZORAN_VFEC); 

	/* set HSpol */
	if (ztv->card->hsync_pos)
		zrwrite(ZORAN_VFEH_HSPOL, ZORAN_VFEH);
	/* set VSpol */
	if (ztv->card->vsync_pos)
		zrwrite(ZORAN_VFEV_VSPOL, ZORAN_VFEV);

	/* Set the proper General Purpuse register bits */
	/* implicit: no softreset, 0 waitstates */
	zrwrite(ZORAN_PCI_SOFTRESET|(ztv->card->gpdir<<0),ZORAN_PCI);
	/* implicit: 3 duration and recovery PCI clocks on guest 0-3 */
	zrwrite(ztv->card->gpval<<24,ZORAN_GUEST);

	/* clear interrupt status */
	zrwrite(~0, ZORAN_ISR);

	/*
	 * i2c template
	 */
	ztv->i2c = zoran_i2c_bus_template;
	sprintf(ztv->i2c.name,"zoran-%d",card);
	ztv->i2c.data = ztv;

	/*
	 * Now add the template and register the device unit
	 */
	ztv->video_dev = zr36120_template;
	strcpy(ztv->video_dev.name, ztv->i2c.name);
	ztv->video_dev.priv = ztv;
	if (video_register_device(&ztv->video_dev, VFL_TYPE_GRABBER, video_nr) < 0)
		return -1;

	ztv->vbi_dev = vbi_template;
	strcpy(ztv->vbi_dev.name, ztv->i2c.name);
	ztv->vbi_dev.priv = ztv;
	if (video_register_device(&ztv->vbi_dev, VFL_TYPE_VBI, vbi_nr) < 0) {
		video_unregister_device(&ztv->video_dev);
		return -1;
	}
	i2c_register_bus(&ztv->i2c);

	/* set interrupt mask - the PIN enable will be set later */
	zrwrite(ZORAN_ICR_GIRQ0|ZORAN_ICR_GIRQ1|ZORAN_ICR_CODE, ZORAN_ICR);

	printk(KERN_INFO "%s: installed %s\n",ztv->i2c.name,ztv->card->name);
	return 0;
}

static
void release_zoran(int max)
{
	struct zoran *ztv;
	int i;

	for (i=0;i<max; i++) 
	{
		ztv = &zorans[i];

		/* turn off all capturing, DMA and IRQs */
		/* reset the zoran */
		zrand(~ZORAN_PCI_SOFTRESET,ZORAN_PCI);
		udelay(10);
		zror(ZORAN_PCI_SOFTRESET,ZORAN_PCI);
		udelay(10);

		/* first disable interrupts before unmapping the memory! */
		zrwrite(0, ZORAN_ICR);
		zrwrite(0xffffffffUL,ZORAN_ISR);

		/* free it */
		free_irq(ztv->dev->irq,ztv);
 
    		/* unregister i2c_bus */
		i2c_unregister_bus((&ztv->i2c));

		/* unmap and free memory */
		if (ztv->zoran_mem)
			iounmap(ztv->zoran_mem);

		video_unregister_device(&ztv->video_dev);
		video_unregister_device(&ztv->vbi_dev);
	}
}

void __exit zr36120_exit(void)
{
	release_zoran(zoran_cards);
}

int __init zr36120_init(void)
{
	int	card;
 
	handle_chipset();
	zoran_cards = find_zoran();
	if (zoran_cards<0)
		/* no cards found, no need for a driver */
		return -EIO;

	/* initialize Zorans */
	for (card=0; card<zoran_cards; card++) {
		if (init_zoran(card)<0) {
			/* only release the zorans we have registered */
			release_zoran(card);
			return -EIO;
		} 
	}
	return 0;
}

module_init(zr36120_init);
module_exit(zr36120_exit);
