/* 
    planb - PlanB frame grabber driver

    PlanB is used in the 7x00/8x00 series of PowerMacintosh
    Computers as video input DMA controller.

    Copyright (C) 1998 Michel Lanners (mlan@cpu.lu)

    Based largely on the bttv driver by Ralph Metzler (rjkm@thp.uni-koeln.de)

    Additional debugging and coding by Takashi Oe (toe@unlserve.unl.edu)

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

/* $Id: planb.c,v 1.18 1999/05/02 17:36:34 mlan Exp $ */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/videodev.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/dbdma.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/irq.h>
#include <asm/semaphore.h>

#include "planb.h"
#include "saa7196.h"

/* Would you mind for some ugly debugging? */
#if 0
#define DEBUG(x...) printk(KERN_DEBUG ## x) /* Debug driver */
#else
#define DEBUG(x...) 		/* Don't debug driver */
#endif

#if 0
#define IDEBUG(x...) printk(KERN_DEBUG ## x) /* Debug interrupt part */
#else
#define IDEBUG(x...) 		/* Don't debug interrupt part */
#endif

/* Ever seen a Mac with more than 1 of these? */
#define PLANB_MAX 1

static int planb_num;
static struct planb planbs[PLANB_MAX];
static volatile struct planb_registers *planb_regs;

static int def_norm = PLANB_DEF_NORM;	/* default norm */
static int video_nr = -1;

MODULE_PARM(def_norm, "i");
MODULE_PARM_DESC(def_norm, "Default startup norm (0=PAL, 1=NTSC, 2=SECAM)");
MODULE_PARM(video_nr,"i");
MODULE_LICENSE("GPL");


/* ------------------ PlanB Exported Functions ------------------ */
static long planb_write(struct video_device *, const char *, unsigned long, int);
static long planb_read(struct video_device *, char *, unsigned long, int);
static int planb_open(struct video_device *, int);
static void planb_close(struct video_device *);
static int planb_ioctl(struct video_device *, unsigned int, void *);
static int planb_init_done(struct video_device *);
static int planb_mmap(struct video_device *, const char *, unsigned long);
static void planb_irq(int, void *, struct pt_regs *);
static void release_planb(void);
int init_planbs(struct video_init *);

/* ------------------ PlanB Internal Functions ------------------ */
static int planb_prepare_open(struct planb *);
static void planb_prepare_close(struct planb *);
static void saa_write_reg(unsigned char, unsigned char);
static unsigned char saa_status(int, struct planb *);
static void saa_set(unsigned char, unsigned char, struct planb *);
static void saa_init_regs(struct planb *);
static int grabbuf_alloc(struct planb *);
static int vgrab(struct planb *, struct video_mmap *);
static void add_clip(struct planb *, struct video_clip *);
static void fill_cmd_buff(struct planb *);
static void cmd_buff(struct planb *);
static volatile struct dbdma_cmd *setup_grab_cmd(int, struct planb *);
static void overlay_start(struct planb *);
static void overlay_stop(struct planb *);
static inline void tab_cmd_dbdma(volatile struct dbdma_cmd *, unsigned short,
	unsigned int);
static inline void tab_cmd_store(volatile struct dbdma_cmd *, unsigned int,
	unsigned int);
static inline void tab_cmd_gen(volatile struct dbdma_cmd *, unsigned short,
	unsigned short, unsigned int, unsigned int);
static int init_planb(struct planb *);
static int find_planb(void);
static void planb_pre_capture(int, int, struct planb *);
static volatile struct dbdma_cmd *cmd_geo_setup(volatile struct dbdma_cmd *,
					int, int, int, int, int, struct planb *);
static inline void planb_dbdma_stop(volatile struct dbdma_regs *);
static unsigned int saa_geo_setup(int, int, int, int, struct planb *);
static inline int overlay_is_active(struct planb *);

/*******************************/
/* Memory management functions */
/*******************************/

static int grabbuf_alloc(struct planb *pb)
{
	int i, npage;

	npage = MAX_GBUFFERS * ((PLANB_MAX_FBUF / PAGE_SIZE + 1)
#ifndef PLANB_GSCANLINE
		+ MAX_LNUM
#endif /* PLANB_GSCANLINE */
		);
	if ((pb->rawbuf = (unsigned char**) kmalloc (npage
				* sizeof(unsigned long), GFP_KERNEL)) == 0)
		return -ENOMEM;
	for (i = 0; i < npage; i++) {
		pb->rawbuf[i] = (unsigned char *)__get_free_pages(GFP_KERNEL
								|GFP_DMA, 0);
		if (!pb->rawbuf[i])
			break;
		SetPageReserved(virt_to_page(pb->rawbuf[i]));
	}
	if (i-- < npage) {
		printk(KERN_DEBUG "PlanB: init_grab: grab buffer not allocated\n");
		for (; i > 0; i--) {
			ClearPageReserved(virt_to_page(pb->rawbuf[i]));
			free_pages((unsigned long)pb->rawbuf[i], 0);
		}
		kfree(pb->rawbuf);
		return -ENOBUFS;
	}
	pb->rawbuf_size = npage;
	return 0;
}

/*****************************/
/* Hardware access functions */
/*****************************/

static void saa_write_reg(unsigned char addr, unsigned char val)
{
	planb_regs->saa_addr = addr; eieio();
	planb_regs->saa_regval = val; eieio();
	return;
}

/* return  status byte 0 or 1: */
static unsigned char saa_status(int byte, struct planb *pb)
{
	saa_regs[pb->win.norm][SAA7196_STDC] =
		(saa_regs[pb->win.norm][SAA7196_STDC] & ~2) | ((byte & 1) << 1);
	saa_write_reg (SAA7196_STDC, saa_regs[pb->win.norm][SAA7196_STDC]);

	/* Let's wait 30msec for this one */
	msleep_interruptible(30);

	return (unsigned char)in_8 (&planb_regs->saa_status);
}

static void saa_set(unsigned char addr, unsigned char val, struct planb *pb)
{
	if(saa_regs[pb->win.norm][addr] != val) {
		saa_regs[pb->win.norm][addr] = val;
		saa_write_reg (addr, val);
	}
	return;
}

static void saa_init_regs(struct planb *pb)
{
	int i;

	for (i = 0; i < SAA7196_NUMREGS; i++)
		saa_write_reg (i, saa_regs[pb->win.norm][i]);
}

static unsigned int saa_geo_setup(int width, int height, int interlace, int bpp,
	struct planb *pb)
{
	int ht, norm = pb->win.norm;

	switch(bpp) {
	case 2:
		/* RGB555+a 1x16-bit + 16-bit transparent */
		saa_regs[norm][SAA7196_FMTS] &= ~0x3;
		break;
	case 1:
	case 4:
		/* RGB888 1x24-bit + 8-bit transparent */
		saa_regs[norm][SAA7196_FMTS] &= ~0x1;
		saa_regs[norm][SAA7196_FMTS] |= 0x2;
		break;
	default:
		return -EINVAL;
	}
	ht = (interlace ? height / 2 : height);
	saa_regs[norm][SAA7196_OUTPIX] = (unsigned char) (width & 0x00ff);
	saa_regs[norm][SAA7196_HFILT] = (saa_regs[norm][SAA7196_HFILT] & ~0x3)
						| (width >> 8 & 0x3);
	saa_regs[norm][SAA7196_OUTLINE] = (unsigned char) (ht & 0xff);
	saa_regs[norm][SAA7196_VYP] = (saa_regs[norm][SAA7196_VYP] & ~0x3)
						| (ht >> 8 & 0x3);
	/* feed both fields if interlaced, or else feed only even fields */
	saa_regs[norm][SAA7196_FMTS] = (interlace) ?
					(saa_regs[norm][SAA7196_FMTS] & ~0x60)
					: (saa_regs[norm][SAA7196_FMTS] | 0x60);
	/* transparent mode; extended format enabled */
	saa_regs[norm][SAA7196_DPATH] |= 0x3;

	return 0;
}

/***************************/
/* DBDMA support functions */
/***************************/

static inline void planb_dbdma_restart(volatile struct dbdma_regs *ch)
{
	out_le32(&ch->control, PLANB_CLR(RUN));
	out_le32(&ch->control, PLANB_SET(RUN|WAKE) | PLANB_CLR(PAUSE));
}

static inline void planb_dbdma_stop(volatile struct dbdma_regs *ch)
{
	int i = 0;

	out_le32(&ch->control, PLANB_CLR(RUN) | PLANB_SET(FLUSH));
	while((in_le32(&ch->status) == (ACTIVE | FLUSH)) && (i < 999)) {
		IDEBUG("PlanB: waiting for DMA to stop\n");
		i++;
	}
}

static inline void tab_cmd_dbdma(volatile struct dbdma_cmd *ch,
	unsigned short command, unsigned int cmd_dep)
{
	st_le16(&ch->command, command);
	st_le32(&ch->cmd_dep, cmd_dep);
}

static inline void tab_cmd_store(volatile struct dbdma_cmd *ch,
	unsigned int phy_addr, unsigned int cmd_dep)
{
	st_le16(&ch->command, STORE_WORD | KEY_SYSTEM);
	st_le16(&ch->req_count, 4);
	st_le32(&ch->phy_addr, phy_addr);
	st_le32(&ch->cmd_dep, cmd_dep);
}

static inline void tab_cmd_gen(volatile struct dbdma_cmd *ch,
	unsigned short command, unsigned short req_count,
	unsigned int phy_addr, unsigned int cmd_dep)
{
	st_le16(&ch->command, command);
	st_le16(&ch->req_count, req_count);
	st_le32(&ch->phy_addr, phy_addr);
	st_le32(&ch->cmd_dep, cmd_dep);
}

static volatile struct dbdma_cmd *cmd_geo_setup(
	volatile struct dbdma_cmd *c1, int width, int height, int interlace,
	int bpp, int clip, struct planb *pb)
{
	int norm = pb->win.norm;

	if((saa_geo_setup(width, height, interlace, bpp, pb)) != 0)
		return (volatile struct dbdma_cmd *)NULL;
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_addr),
							SAA7196_FMTS);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_regval),
					saa_regs[norm][SAA7196_FMTS]);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_addr),
							SAA7196_DPATH);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_regval),
					saa_regs[norm][SAA7196_DPATH]);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->even),
					bpp | ((clip)? PLANB_CLIPMASK: 0));
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->odd),
					bpp | ((clip)? PLANB_CLIPMASK: 0));
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_addr),
							SAA7196_OUTPIX);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_regval),
					saa_regs[norm][SAA7196_OUTPIX]);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_addr),
							SAA7196_HFILT);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_regval),
					saa_regs[norm][SAA7196_HFILT]);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_addr),
							SAA7196_OUTLINE);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_regval),
					saa_regs[norm][SAA7196_OUTLINE]);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_addr),
							SAA7196_VYP);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->saa_regval),
					saa_regs[norm][SAA7196_VYP]);
	return c1;
}

/******************************/
/* misc. supporting functions */
/******************************/

static inline void planb_lock(struct planb *pb)
{
	down(&pb->lock);
}

static inline void planb_unlock(struct planb *pb)
{
	up(&pb->lock);
}

/***************/
/* Driver Core */
/***************/

static int planb_prepare_open(struct planb *pb)
{
	int	i, size;

	/* allocate memory for two plus alpha command buffers (size: max lines,
	   plus 40 commands handling, plus 1 alignment), plus dummy command buf,
	   plus clipmask buffer, plus frame grabbing status */
	size = (pb->tab_size*(2+MAX_GBUFFERS*TAB_FACTOR)+1+MAX_GBUFFERS
		* PLANB_DUMMY)*sizeof(struct dbdma_cmd)
		+(PLANB_MAXLINES*((PLANB_MAXPIXELS+7)& ~7))/8
		+MAX_GBUFFERS*sizeof(unsigned int);
	if ((pb->priv_space = kmalloc (size, GFP_KERNEL)) == 0)
		return -ENOMEM;
	memset ((void *) pb->priv_space, 0, size);
	pb->overlay_last1 = pb->ch1_cmd = (volatile struct dbdma_cmd *)
						DBDMA_ALIGN (pb->priv_space);
	pb->overlay_last2 = pb->ch2_cmd = pb->ch1_cmd + pb->tab_size;
	pb->ch1_cmd_phys = virt_to_bus(pb->ch1_cmd);
	pb->cap_cmd[0] = pb->ch2_cmd + pb->tab_size;
	pb->pre_cmd[0] = pb->cap_cmd[0] + pb->tab_size * TAB_FACTOR;
	for (i = 1; i < MAX_GBUFFERS; i++) {
		pb->cap_cmd[i] = pb->pre_cmd[i-1] + PLANB_DUMMY;
		pb->pre_cmd[i] = pb->cap_cmd[i] + pb->tab_size * TAB_FACTOR;
	}
	pb->frame_stat=(volatile unsigned int *)(pb->pre_cmd[MAX_GBUFFERS-1]
						+ PLANB_DUMMY);
	pb->mask = (unsigned char *)(pb->frame_stat+MAX_GBUFFERS);

	pb->rawbuf = NULL;
	pb->rawbuf_size = 0;
	pb->grabbing = 0;
	for (i = 0; i < MAX_GBUFFERS; i++) {
		pb->frame_stat[i] = GBUFFER_UNUSED;
		pb->gwidth[i] = 0;
		pb->gheight[i] = 0;
		pb->gfmt[i] = 0;
		pb->gnorm_switch[i] = 0;
#ifndef PLANB_GSCANLINE
		pb->lsize[i] = 0;
		pb->lnum[i] = 0;
#endif /* PLANB_GSCANLINE */
	}
	pb->gcount = 0;
	pb->suspend = 0;
	pb->last_fr = -999;
	pb->prev_last_fr = -999;

	/* Reset DMA controllers */
	planb_dbdma_stop(&pb->planb_base->ch2);
	planb_dbdma_stop(&pb->planb_base->ch1);

	return 0;
}

static void planb_prepare_close(struct planb *pb)
{
	int i;

	/* make sure the dma's are idle */
	planb_dbdma_stop(&pb->planb_base->ch2);
	planb_dbdma_stop(&pb->planb_base->ch1);
	/* free kernel memory of command buffers */
	if(pb->priv_space != 0) {
		kfree (pb->priv_space);
		pb->priv_space = 0;
		pb->cmd_buff_inited = 0;
	}
	if(pb->rawbuf) {
		for (i = 0; i < pb->rawbuf_size; i++) {
			ClearPageReserved(virt_to_page(pb->rawbuf[i]));
			free_pages((unsigned long)pb->rawbuf[i], 0);
		}
		kfree(pb->rawbuf);
	}
	pb->rawbuf = NULL;
}

/*****************************/
/* overlay support functions */
/*****************************/

static inline int overlay_is_active(struct planb *pb)
{
	unsigned int size = pb->tab_size * sizeof(struct dbdma_cmd);
	unsigned int caddr = (unsigned)in_le32(&pb->planb_base->ch1.cmdptr);

	return (in_le32(&pb->overlay_last1->cmd_dep) == pb->ch1_cmd_phys)
			&& (caddr < (pb->ch1_cmd_phys + size))
			&& (caddr >= (unsigned)pb->ch1_cmd_phys);
}

static void overlay_start(struct planb *pb)
{

	DEBUG("PlanB: overlay_start()\n");

	if(ACTIVE & in_le32(&pb->planb_base->ch1.status)) {

		DEBUG("PlanB: presumably, grabbing is in progress...\n");

		planb_dbdma_stop(&pb->planb_base->ch2);
		out_le32 (&pb->planb_base->ch2.cmdptr,
						virt_to_bus(pb->ch2_cmd));
		planb_dbdma_restart(&pb->planb_base->ch2);
		st_le16 (&pb->ch1_cmd->command, DBDMA_NOP);
		tab_cmd_dbdma(pb->last_cmd[pb->last_fr],
					DBDMA_NOP | BR_ALWAYS,
					virt_to_bus(pb->ch1_cmd));
		eieio();
		pb->prev_last_fr = pb->last_fr;
		pb->last_fr = -2;
		if(!(ACTIVE & in_le32(&pb->planb_base->ch1.status))) {
			IDEBUG("PlanB: became inactive "
				"in the mean time... reactivating\n");
			planb_dbdma_stop(&pb->planb_base->ch1);
			out_le32 (&pb->planb_base->ch1.cmdptr,
						virt_to_bus(pb->ch1_cmd));
			planb_dbdma_restart(&pb->planb_base->ch1);
		}
	} else {

		DEBUG("PlanB: currently idle, so can do whatever\n");

		planb_dbdma_stop(&pb->planb_base->ch2);
		planb_dbdma_stop(&pb->planb_base->ch1);
		st_le32 (&pb->planb_base->ch2.cmdptr,
						virt_to_bus(pb->ch2_cmd));
		st_le32 (&pb->planb_base->ch1.cmdptr,
						virt_to_bus(pb->ch1_cmd));
		out_le16 (&pb->ch1_cmd->command, DBDMA_NOP);
		planb_dbdma_restart(&pb->planb_base->ch2);
		planb_dbdma_restart(&pb->planb_base->ch1);
		pb->last_fr = -1;
	}
	return;
}

static void overlay_stop(struct planb *pb)
{
	DEBUG("PlanB: overlay_stop()\n");

	if(pb->last_fr == -1) {

		DEBUG("PlanB: no grabbing, it seems...\n");

		planb_dbdma_stop(&pb->planb_base->ch2);
		planb_dbdma_stop(&pb->planb_base->ch1);
		pb->last_fr = -999;
	} else if(pb->last_fr == -2) {
		unsigned int cmd_dep;
		tab_cmd_dbdma(pb->cap_cmd[pb->prev_last_fr], DBDMA_STOP, 0);
		eieio();
		cmd_dep = (unsigned int)in_le32(&pb->overlay_last1->cmd_dep);
		if(overlay_is_active(pb)) {

			DEBUG("PlanB: overlay is currently active\n");

			planb_dbdma_stop(&pb->planb_base->ch2);
			planb_dbdma_stop(&pb->planb_base->ch1);
			if(cmd_dep != pb->ch1_cmd_phys) {
				out_le32(&pb->planb_base->ch1.cmdptr,
						virt_to_bus(pb->overlay_last1));
				planb_dbdma_restart(&pb->planb_base->ch1);
			}
		}
		pb->last_fr = pb->prev_last_fr;
		pb->prev_last_fr = -999;
	}
	return;
}

static void suspend_overlay(struct planb *pb)
{
	int fr = -1;
	struct dbdma_cmd last;

	DEBUG("PlanB: suspend_overlay: %d\n", pb->suspend);

	if(pb->suspend++)
		return;
	if(ACTIVE & in_le32(&pb->planb_base->ch1.status)) {
		if(pb->last_fr == -2) {
			fr = pb->prev_last_fr;
			memcpy(&last, (void*)pb->last_cmd[fr], sizeof(last));
			tab_cmd_dbdma(pb->last_cmd[fr], DBDMA_STOP, 0);
		}
		if(overlay_is_active(pb)) {
			planb_dbdma_stop(&pb->planb_base->ch2);
			planb_dbdma_stop(&pb->planb_base->ch1);
			pb->suspended.overlay = 1;
			pb->suspended.frame = fr;
			memcpy(&pb->suspended.cmd, &last, sizeof(last));
			return;
		}
	}
	pb->suspended.overlay = 0;
	pb->suspended.frame = fr;
	memcpy(&pb->suspended.cmd, &last, sizeof(last));
	return;
}

static void resume_overlay(struct planb *pb)
{

	DEBUG("PlanB: resume_overlay: %d\n", pb->suspend);

	if(pb->suspend > 1)
		return;
	if(pb->suspended.frame != -1) {
		memcpy((void*)pb->last_cmd[pb->suspended.frame],
				&pb->suspended.cmd, sizeof(pb->suspended.cmd));
	}
	if(ACTIVE & in_le32(&pb->planb_base->ch1.status)) {
		goto finish;
	}
	if(pb->suspended.overlay) {

		DEBUG("PlanB: overlay being resumed\n");

		st_le16 (&pb->ch1_cmd->command, DBDMA_NOP);
		st_le16 (&pb->ch2_cmd->command, DBDMA_NOP);
		/* Set command buffer addresses */
		st_le32(&pb->planb_base->ch1.cmdptr,
					virt_to_bus(pb->overlay_last1));
		out_le32(&pb->planb_base->ch2.cmdptr,
					virt_to_bus(pb->overlay_last2));
		/* Start the DMA controller */
		out_le32 (&pb->planb_base->ch2.control,
				PLANB_CLR(PAUSE) | PLANB_SET(RUN|WAKE));
		out_le32 (&pb->planb_base->ch1.control,
				PLANB_CLR(PAUSE) | PLANB_SET(RUN|WAKE));
	} else if(pb->suspended.frame != -1) {
		out_le32(&pb->planb_base->ch1.cmdptr,
				virt_to_bus(pb->last_cmd[pb->suspended.frame]));
		out_le32 (&pb->planb_base->ch1.control,
				PLANB_CLR(PAUSE) | PLANB_SET(RUN|WAKE));
	}

finish:
	pb->suspend--;
	wake_up_interruptible(&pb->suspendq);
}

static void add_clip(struct planb *pb, struct video_clip *clip) 
{
	volatile unsigned char	*base;
	int	xc = clip->x, yc = clip->y;
	int	wc = clip->width, hc = clip->height;
	int	ww = pb->win.width, hw = pb->win.height;
	int	x, y, xtmp1, xtmp2;

	DEBUG("PlanB: clip %dx%d+%d+%d\n", wc, hc, xc, yc);

	if(xc < 0) {
		wc += xc;
		xc = 0;
	}
	if(yc < 0) {
		hc += yc;
		yc = 0;
	}
	if(xc + wc > ww)
		wc = ww - xc;
	if(wc <= 0) /* Nothing to do */
		return;
	if(yc + hc > hw)
		hc = hw - yc;

	for (y = yc; y < yc+hc; y++) {
		xtmp1=xc>>3;
		xtmp2=(xc+wc)>>3;
		base = pb->mask + y*96;
		if(xc != 0 || wc >= 8)
			*(base + xtmp1) &= (unsigned char)(0x00ff &
				(0xff00 >> (xc&7)));
		for (x = xtmp1 + 1; x < xtmp2; x++) {
			*(base + x) = 0;
		}
		if(xc < (ww & ~0x7))
			*(base + xtmp2) &= (unsigned char)(0x00ff >>
				((xc+wc) & 7));
	}

	return;
}

static void fill_cmd_buff(struct planb *pb)
{
	int restore = 0;
	volatile struct dbdma_cmd last;

	DEBUG("PlanB: fill_cmd_buff()\n");

	if(pb->overlay_last1 != pb->ch1_cmd) {
		restore = 1;
		last = *(pb->overlay_last1);
	}
	memset ((void *) pb->ch1_cmd, 0, 2 * pb->tab_size
					* sizeof(struct dbdma_cmd));
	cmd_buff (pb);
	if(restore)
		*(pb->overlay_last1) = last;
	if(pb->suspended.overlay) {
		unsigned long jump_addr = in_le32(&pb->overlay_last1->cmd_dep);
		if(jump_addr != pb->ch1_cmd_phys) {
			int i;

			DEBUG("PlanB: adjusting ch1's jump address\n");

			for(i = 0; i < MAX_GBUFFERS; i++) {
				if(pb->need_pre_capture[i]) {
				    if(jump_addr == virt_to_bus(pb->pre_cmd[i]))
					goto found;
				} else {
				    if(jump_addr == virt_to_bus(pb->cap_cmd[i]))
					goto found;
				}
			}

			DEBUG("PlanB: not found...\n");

			goto out;
found:
			if(pb->need_pre_capture[i])
				out_le32(&pb->pre_cmd[i]->phy_addr,
						virt_to_bus(pb->overlay_last1));
			else
				out_le32(&pb->cap_cmd[i]->phy_addr,
						virt_to_bus(pb->overlay_last1));
		}
	}
out:
	pb->cmd_buff_inited = 1;

	return;
}

static void cmd_buff(struct planb *pb)
{
	int		i, bpp, count, nlines, stepsize, interlace;
	unsigned long	base, jump, addr_com, addr_dep;
	volatile struct dbdma_cmd *c1 = pb->ch1_cmd;
	volatile struct dbdma_cmd *c2 = pb->ch2_cmd;

	interlace = pb->win.interlace;
	bpp = pb->win.bpp;
	count = (bpp * ((pb->win.x + pb->win.width > pb->win.swidth) ?
		(pb->win.swidth - pb->win.x) : pb->win.width));
	nlines = ((pb->win.y + pb->win.height > pb->win.sheight) ?
		(pb->win.sheight - pb->win.y) : pb->win.height);

	/* Do video in: */

	/* Preamble commands: */
	addr_com = virt_to_bus(c1);
	addr_dep = virt_to_bus(&c1->cmd_dep);
	tab_cmd_dbdma(c1++, DBDMA_NOP, 0);
	jump = virt_to_bus(c1+16); /* 14 by cmd_geo_setup() and 2 for padding */
	if((c1 = cmd_geo_setup(c1, pb->win.width, pb->win.height, interlace,
					bpp, 1, pb)) == NULL) {
		printk(KERN_WARNING "PlanB: encountered serious problems\n");
		tab_cmd_dbdma(pb->ch1_cmd + 1, DBDMA_STOP, 0);
		tab_cmd_dbdma(pb->ch2_cmd + 1, DBDMA_STOP, 0);
		return;
	}
	tab_cmd_store(c1++, addr_com, (unsigned)(DBDMA_NOP | BR_ALWAYS) << 16);
	tab_cmd_store(c1++, addr_dep, jump);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.wait_sel),
							PLANB_SET(FIELD_SYNC));
		/* (1) wait for field sync to be set */
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFCLR, 0);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
							PLANB_SET(ODD_FIELD));
		/* wait for field sync to be cleared */
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFSET, 0);
		/* if not odd field, wait until field sync is set again */
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_IFSET, virt_to_bus(c1-3)); c1++;
		/* assert ch_sync to ch2 */
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch2.control),
							PLANB_SET(CH_SYNC));
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
							PLANB_SET(DMA_ABORT));

	base = (pb->frame_buffer_phys + pb->offset + pb->win.y * (pb->win.bpl
					+ pb->win.pad) + pb->win.x * bpp);

	if (interlace) {
		stepsize = 2;
		jump = virt_to_bus(c1 + (nlines + 1) / 2);
	} else {
		stepsize = 1;
		jump = virt_to_bus(c1 + nlines);
	}

	/* even field data: */
	for (i=0; i < nlines; i += stepsize, c1++)
		tab_cmd_gen(c1, INPUT_MORE | KEY_STREAM0 | BR_IFSET,
			count, base + i * (pb->win.bpl + pb->win.pad), jump);

	/* For non-interlaced, we use even fields only */
	if (!interlace)
		goto cmd_tab_data_end;

	/* Resync to odd field */
		/* (2) wait for field sync to be set */
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFCLR, 0);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
							PLANB_SET(ODD_FIELD));
		/* wait for field sync to be cleared */
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFSET, 0);
		/* if not odd field, wait until field sync is set again */
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_IFCLR, virt_to_bus(c1-3)); c1++;
		/* assert ch_sync to ch2 */
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch2.control),
							PLANB_SET(CH_SYNC));
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
							PLANB_SET(DMA_ABORT));
	
	/* odd field data: */
	jump = virt_to_bus(c1 + nlines / 2);
	for (i=1; i < nlines; i += stepsize, c1++)
		tab_cmd_gen(c1, INPUT_MORE | KEY_STREAM0 | BR_IFSET, count,
			base + i * (pb->win.bpl + pb->win.pad), jump);

	/* And jump back to the start */
cmd_tab_data_end:
	pb->overlay_last1 = c1;	/* keep a pointer to the last command */
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_ALWAYS, virt_to_bus(pb->ch1_cmd));

	/* Clipmask command buffer */

	/* Preamble commands: */
	tab_cmd_dbdma(c2++, DBDMA_NOP, 0);
	tab_cmd_store(c2++, (unsigned)(&pb->planb_base_phys->ch2.wait_sel),
							PLANB_SET(CH_SYNC));
		/* wait until ch1 asserts ch_sync */
	tab_cmd_dbdma(c2++, DBDMA_NOP | WAIT_IFCLR, 0);
		/* clear ch_sync asserted by ch1 */
	tab_cmd_store(c2++, (unsigned)(&pb->planb_base_phys->ch2.control),
							PLANB_CLR(CH_SYNC));
	tab_cmd_store(c2++, (unsigned)(&pb->planb_base_phys->ch2.wait_sel),
							PLANB_SET(FIELD_SYNC));
	tab_cmd_store(c2++, (unsigned)(&pb->planb_base_phys->ch2.br_sel),
							PLANB_SET(ODD_FIELD));

	/* jump to end of even field if appropriate */
	/* this points to (interlace)? pos. C: pos. B */
	jump = (interlace) ? virt_to_bus(c2 + (nlines + 1) / 2 + 2):
						virt_to_bus(c2 + nlines + 2);
		/* if odd field, skip over to odd field clipmasking */
	tab_cmd_dbdma(c2++, DBDMA_NOP | BR_IFSET, jump);

	/* even field mask: */
	tab_cmd_store(c2++, (unsigned)(&pb->planb_base_phys->ch2.br_sel),
							PLANB_SET(DMA_ABORT));
	/* this points to pos. B */
	jump = (interlace) ? virt_to_bus(c2 + nlines + 1):
						virt_to_bus(c2 + nlines);
	base = virt_to_bus(pb->mask);
	for (i=0; i < nlines; i += stepsize, c2++)
		tab_cmd_gen(c2, OUTPUT_MORE | KEY_STREAM0 | BR_IFSET, 96,
			base + i * 96, jump);

	/* For non-interlaced, we use only even fields */
	if(!interlace)
		goto cmd_tab_mask_end;

	/* odd field mask: */
/* C */	tab_cmd_store(c2++, (unsigned)(&pb->planb_base_phys->ch2.br_sel),
							PLANB_SET(DMA_ABORT));
	/* this points to pos. B */
	jump = virt_to_bus(c2 + nlines / 2);
	base = virt_to_bus(pb->mask);
	for (i=1; i < nlines; i += 2, c2++)     /* abort if set */
		tab_cmd_gen(c2, OUTPUT_MORE | KEY_STREAM0 | BR_IFSET, 96,
			base + i * 96, jump);

	/* Inform channel 1 and jump back to start */
cmd_tab_mask_end:
	/* ok, I just realized this is kind of flawed. */
	/* this part is reached only after odd field clipmasking. */
	/* wanna clean up? */
		/* wait for field sync to be set */
		/* corresponds to fsync (1) of ch1 */
/* B */	tab_cmd_dbdma(c2++, DBDMA_NOP | WAIT_IFCLR, 0);
		/* restart ch1, meant to clear any dead bit or something */
	tab_cmd_store(c2++, (unsigned)(&pb->planb_base_phys->ch1.control),
							PLANB_CLR(RUN));
	tab_cmd_store(c2++, (unsigned)(&pb->planb_base_phys->ch1.control),
							PLANB_SET(RUN));
	pb->overlay_last2 = c2;	/* keep a pointer to the last command */
		/* start over even field clipmasking */
	tab_cmd_dbdma(c2, DBDMA_NOP | BR_ALWAYS, virt_to_bus(pb->ch2_cmd));

	eieio();
	return;
}

/*********************************/
/* grabdisplay support functions */
/*********************************/

static int palette2fmt[] = {
       0,
       PLANB_GRAY,
       0,
       0,
       0,
       PLANB_COLOUR32,
       PLANB_COLOUR15,
       0,
       0,
       0,
       0,
       0,
       0,
       0,
       0,
};

#define PLANB_PALETTE_MAX 15

static int vgrab(struct planb *pb, struct video_mmap *mp)
{
	unsigned int fr = mp->frame;
	unsigned int format;

	if(pb->rawbuf==NULL) {
		int err;
		if((err=grabbuf_alloc(pb)))
			return err;
	}

	IDEBUG("PlanB: grab %d: %dx%d(%u)\n", pb->grabbing,
						mp->width, mp->height, fr);

	if(pb->grabbing >= MAX_GBUFFERS)
		return -ENOBUFS;
	if(fr > (MAX_GBUFFERS - 1) || fr < 0)
		return -EINVAL;
	if(mp->height <= 0 || mp->width <= 0)
		return -EINVAL;
	if(mp->format < 0 || mp->format >= PLANB_PALETTE_MAX)
		return -EINVAL;
	if((format = palette2fmt[mp->format]) == 0)
		return -EINVAL;
	if (mp->height * mp->width * format > PLANB_MAX_FBUF) /* format = bpp */
		return -EINVAL;

	planb_lock(pb);
	if(mp->width != pb->gwidth[fr] || mp->height != pb->gheight[fr] ||
			format != pb->gfmt[fr] || (pb->gnorm_switch[fr])) {
		int i;
#ifndef PLANB_GSCANLINE
		unsigned int osize = pb->gwidth[fr] * pb->gheight[fr]
								* pb->gfmt[fr];
		unsigned int nsize = mp->width * mp->height * format;
#endif

		IDEBUG("PlanB: gwidth = %d, gheight = %d, mp->format = %u\n",
					mp->width, mp->height, mp->format);

#ifndef PLANB_GSCANLINE
		if(pb->gnorm_switch[fr])
			nsize = 0;
		if (nsize < osize) {
			for(i = pb->gbuf_idx[fr]; osize > 0; i++) {
				memset((void *)pb->rawbuf[i], 0, PAGE_SIZE);
				osize -= PAGE_SIZE;
			}
		}
		for(i = pb->l_fr_addr_idx[fr]; i < pb->l_fr_addr_idx[fr]
							+ pb->lnum[fr]; i++)
			memset((void *)pb->rawbuf[i], 0, PAGE_SIZE);
#else
/* XXX TODO */
/*
		if(pb->gnorm_switch[fr])
			memset((void *)pb->gbuffer[fr], 0,
					pb->gbytes_per_line * pb->gheight[fr]);
		else {
			if(mp->
			for(i = 0; i < pb->gheight[fr]; i++) {
				memset((void *)(pb->gbuffer[fr]
					+ pb->gbytes_per_line * i
			}
		}
*/
#endif
		pb->gwidth[fr] = mp->width;
		pb->gheight[fr] = mp->height;
		pb->gfmt[fr] = format;
		pb->last_cmd[fr] = setup_grab_cmd(fr, pb);
		planb_pre_capture(fr, pb->gfmt[fr], pb); /* gfmt = bpp */
		pb->need_pre_capture[fr] = 1;
		pb->gnorm_switch[fr] = 0;
	} else
		pb->need_pre_capture[fr] = 0;
	pb->frame_stat[fr] = GBUFFER_GRABBING;
	if(!(ACTIVE & in_le32(&pb->planb_base->ch1.status))) {

		IDEBUG("PlanB: ch1 inactive, initiating grabbing\n");

		planb_dbdma_stop(&pb->planb_base->ch1);
		if(pb->need_pre_capture[fr]) {

			IDEBUG("PlanB: padding pre-capture sequence\n");

			out_le32 (&pb->planb_base->ch1.cmdptr,
						virt_to_bus(pb->pre_cmd[fr]));
		} else {
			tab_cmd_dbdma(pb->last_cmd[fr], DBDMA_STOP, 0);
			tab_cmd_dbdma(pb->cap_cmd[fr], DBDMA_NOP, 0);
		/* let's be on the safe side. here is not timing critical. */
			tab_cmd_dbdma((pb->cap_cmd[fr] + 1), DBDMA_NOP, 0);
			out_le32 (&pb->planb_base->ch1.cmdptr,
						virt_to_bus(pb->cap_cmd[fr]));
		}
		planb_dbdma_restart(&pb->planb_base->ch1);
		pb->last_fr = fr;
	} else {
		int i;

		IDEBUG("PlanB: ch1 active, grabbing being queued\n");

		if((pb->last_fr == -1) || ((pb->last_fr == -2) &&
						overlay_is_active(pb))) {

			IDEBUG("PlanB: overlay is active, grabbing defered\n");

			tab_cmd_dbdma(pb->last_cmd[fr],
					DBDMA_NOP | BR_ALWAYS,
					virt_to_bus(pb->ch1_cmd));
			if(pb->need_pre_capture[fr]) {

				IDEBUG("PlanB: padding pre-capture sequence\n");

				tab_cmd_store(pb->pre_cmd[fr],
				    virt_to_bus(&pb->overlay_last1->cmd_dep),
						virt_to_bus(pb->ch1_cmd));
				eieio();
				out_le32 (&pb->overlay_last1->cmd_dep,
						virt_to_bus(pb->pre_cmd[fr]));
			} else {
				tab_cmd_store(pb->cap_cmd[fr],
				    virt_to_bus(&pb->overlay_last1->cmd_dep),
						virt_to_bus(pb->ch1_cmd));
				tab_cmd_dbdma((pb->cap_cmd[fr] + 1),
								DBDMA_NOP, 0);
				eieio();
				out_le32 (&pb->overlay_last1->cmd_dep,
						virt_to_bus(pb->cap_cmd[fr]));
			}
			for(i = 0; overlay_is_active(pb) && i < 999; i++)
				IDEBUG("PlanB: waiting for overlay done\n");
			tab_cmd_dbdma(pb->ch1_cmd, DBDMA_NOP, 0);
			pb->prev_last_fr = fr;
			pb->last_fr = -2;
		} else if(pb->last_fr == -2) {

			IDEBUG("PlanB: mixed mode detected, grabbing"
				" will be done before activating overlay\n");

			tab_cmd_dbdma(pb->ch1_cmd, DBDMA_NOP, 0);
			if(pb->need_pre_capture[fr]) {

				IDEBUG("PlanB: padding pre-capture sequence\n");

				tab_cmd_dbdma(pb->last_cmd[pb->prev_last_fr],
						DBDMA_NOP | BR_ALWAYS,
						virt_to_bus(pb->pre_cmd[fr]));
				eieio();
			} else {
				tab_cmd_dbdma(pb->cap_cmd[fr], DBDMA_NOP, 0);
				if(pb->gwidth[pb->prev_last_fr] !=
								pb->gwidth[fr]
					|| pb->gheight[pb->prev_last_fr] !=
								pb->gheight[fr]
					|| pb->gfmt[pb->prev_last_fr] !=
								pb->gfmt[fr])
					tab_cmd_dbdma((pb->cap_cmd[fr] + 1),
								DBDMA_NOP, 0);
				else
					tab_cmd_dbdma((pb->cap_cmd[fr] + 1),
					    DBDMA_NOP | BR_ALWAYS,
					    virt_to_bus(pb->cap_cmd[fr] + 16));
				tab_cmd_dbdma(pb->last_cmd[pb->prev_last_fr],
						DBDMA_NOP | BR_ALWAYS,
						virt_to_bus(pb->cap_cmd[fr]));
				eieio();
			}
			tab_cmd_dbdma(pb->last_cmd[fr],
					DBDMA_NOP | BR_ALWAYS,
					virt_to_bus(pb->ch1_cmd));
			eieio();
			pb->prev_last_fr = fr;
			pb->last_fr = -2;
		} else {

			IDEBUG("PlanB: active grabbing session detected\n");

			if(pb->need_pre_capture[fr]) {

				IDEBUG("PlanB: padding pre-capture sequence\n");

				tab_cmd_dbdma(pb->last_cmd[pb->last_fr],
						DBDMA_NOP | BR_ALWAYS,
						virt_to_bus(pb->pre_cmd[fr]));
				eieio();
			} else {
				tab_cmd_dbdma(pb->last_cmd[fr], DBDMA_STOP, 0);
				tab_cmd_dbdma(pb->cap_cmd[fr], DBDMA_NOP, 0);
				if(pb->gwidth[pb->last_fr] != pb->gwidth[fr]
					|| pb->gheight[pb->last_fr] !=
								pb->gheight[fr]
					|| pb->gfmt[pb->last_fr] !=
								pb->gfmt[fr])
					tab_cmd_dbdma((pb->cap_cmd[fr] + 1),
								DBDMA_NOP, 0);
				else
					tab_cmd_dbdma((pb->cap_cmd[fr] + 1),
					    DBDMA_NOP | BR_ALWAYS,
					    virt_to_bus(pb->cap_cmd[fr] + 16));
				tab_cmd_dbdma(pb->last_cmd[pb->last_fr],
						DBDMA_NOP | BR_ALWAYS,
						virt_to_bus(pb->cap_cmd[fr]));
				eieio();
			}
			pb->last_fr = fr;
		}
		if(!(ACTIVE & in_le32(&pb->planb_base->ch1.status))) {

			IDEBUG("PlanB: became inactive in the mean time..."
				"reactivating\n");

			planb_dbdma_stop(&pb->planb_base->ch1);
			out_le32 (&pb->planb_base->ch1.cmdptr,
						virt_to_bus(pb->cap_cmd[fr]));
			planb_dbdma_restart(&pb->planb_base->ch1);
		}
	}
	pb->grabbing++;
	planb_unlock(pb);

	return 0;
}

static void planb_pre_capture(int fr, int bpp, struct planb *pb)
{
	volatile struct dbdma_cmd *c1 = pb->pre_cmd[fr];
	int interlace = (pb->gheight[fr] > pb->maxlines/2)? 1: 0;

	tab_cmd_dbdma(c1++, DBDMA_NOP, 0);
	if((c1 = cmd_geo_setup(c1, pb->gwidth[fr], pb->gheight[fr], interlace,
						bpp, 0, pb)) == NULL) {
		printk(KERN_WARNING "PlanB: encountered some problems\n");
		tab_cmd_dbdma(pb->pre_cmd[fr] + 1, DBDMA_STOP, 0);
		return;
	}
	/* Sync to even field */
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.wait_sel),
		PLANB_SET(FIELD_SYNC));
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFCLR, 0);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
		PLANB_SET(ODD_FIELD));
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFSET, 0);
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_IFSET, virt_to_bus(c1-3)); c1++;
	tab_cmd_dbdma(c1++, DBDMA_NOP | INTR_ALWAYS, 0);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
		PLANB_SET(DMA_ABORT));
	/* For non-interlaced, we use even fields only */
	if (pb->gheight[fr] <= pb->maxlines/2)
		goto cmd_tab_data_end;
	/* Sync to odd field */
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFCLR, 0);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
		PLANB_SET(ODD_FIELD));
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFSET, 0);
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_IFCLR, virt_to_bus(c1-3)); c1++;
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
		PLANB_SET(DMA_ABORT));
cmd_tab_data_end:
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_ALWAYS, virt_to_bus(pb->cap_cmd[fr]));

	eieio();
}

static volatile struct dbdma_cmd *setup_grab_cmd(int fr, struct planb *pb)
{
	int		i, bpp, count, nlines, stepsize, interlace;
#ifdef PLANB_GSCANLINE
	int		scanline;
#else
	int		nlpp, leftover1;
	unsigned long	base;
#endif
	unsigned long	jump;
	int		pagei;
	volatile struct dbdma_cmd *c1;
	volatile struct dbdma_cmd *jump_addr;

	c1 = pb->cap_cmd[fr];
	interlace = (pb->gheight[fr] > pb->maxlines/2)? 1: 0;
	bpp = pb->gfmt[fr];	/* gfmt = bpp */
	count = bpp * pb->gwidth[fr];
	nlines = pb->gheight[fr];
#ifdef PLANB_GSCANLINE
	scanline = pb->gbytes_per_line;
#else
	pb->lsize[fr] = count;
	pb->lnum[fr] = 0;
#endif

	/* Do video in: */

	/* Preamble commands: */
	tab_cmd_dbdma(c1++, DBDMA_NOP, 0);
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_ALWAYS, virt_to_bus(c1 + 16)); c1++;
	if((c1 = cmd_geo_setup(c1, pb->gwidth[fr], pb->gheight[fr], interlace,
						bpp, 0, pb)) == NULL) {
		printk(KERN_WARNING "PlanB: encountered serious problems\n");
		tab_cmd_dbdma(pb->cap_cmd[fr] + 1, DBDMA_STOP, 0);
		return (pb->cap_cmd[fr] + 2);
	}
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.wait_sel),
		PLANB_SET(FIELD_SYNC));
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFCLR, 0);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
		PLANB_SET(ODD_FIELD));
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFSET, 0);
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_IFSET, virt_to_bus(c1-3)); c1++;
	tab_cmd_dbdma(c1++, DBDMA_NOP | INTR_ALWAYS, 0);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
		PLANB_SET(DMA_ABORT));

	if (interlace) {
		stepsize = 2;
		jump_addr = c1 + TAB_FACTOR * (nlines + 1) / 2;
	} else {
		stepsize = 1;
		jump_addr = c1 + TAB_FACTOR * nlines;
	}
	jump = virt_to_bus(jump_addr);

	/* even field data: */

	pagei = pb->gbuf_idx[fr];
#ifdef PLANB_GSCANLINE
	for (i = 0; i < nlines; i += stepsize) {
		tab_cmd_gen(c1++, INPUT_MORE | BR_IFSET, count,
					virt_to_bus(pb->rawbuf[pagei
					+ i * scanline / PAGE_SIZE]), jump);
	}
#else
	i = 0;
	leftover1 = 0;
	do {
	    int j;

	    base = virt_to_bus(pb->rawbuf[pagei]);
	    nlpp = (PAGE_SIZE - leftover1) / count / stepsize;
	    for(j = 0; j < nlpp && i < nlines; j++, i += stepsize, c1++)
		tab_cmd_gen(c1, INPUT_MORE | KEY_STREAM0 | BR_IFSET,
			  count, base + count * j * stepsize + leftover1, jump);
	    if(i < nlines) {
		int lov0 = PAGE_SIZE - count * nlpp * stepsize - leftover1;

		if(lov0 == 0)
		    leftover1 = 0;
		else {
		    if(lov0 >= count) {
			tab_cmd_gen(c1++, INPUT_MORE | BR_IFSET, count, base
				+ count * nlpp * stepsize + leftover1, jump);
		    } else {
			pb->l_to_addr[fr][pb->lnum[fr]] = pb->rawbuf[pagei]
					+ count * nlpp * stepsize + leftover1;
			pb->l_to_next_idx[fr][pb->lnum[fr]] = pagei + 1;
			pb->l_to_next_size[fr][pb->lnum[fr]] = count - lov0;
			tab_cmd_gen(c1++, INPUT_MORE | BR_IFSET, count,
				virt_to_bus(pb->rawbuf[pb->l_fr_addr_idx[fr]
						+ pb->lnum[fr]]), jump);
			if(++pb->lnum[fr] > MAX_LNUM)
				pb->lnum[fr]--;
		    }
		    leftover1 = count * stepsize - lov0;
		    i += stepsize;
		}
	    }
	    pagei++;
	} while(i < nlines);
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_ALWAYS, jump);
	c1 = jump_addr;
#endif /* PLANB_GSCANLINE */

	/* For non-interlaced, we use even fields only */
	if (!interlace)
		goto cmd_tab_data_end;

	/* Sync to odd field */
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFCLR, 0);
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
		PLANB_SET(ODD_FIELD));
	tab_cmd_dbdma(c1++, DBDMA_NOP | WAIT_IFSET, 0);
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_IFCLR, virt_to_bus(c1-3)); c1++;
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->ch1.br_sel),
		PLANB_SET(DMA_ABORT));
	
	/* odd field data: */
	jump_addr = c1 + TAB_FACTOR * nlines / 2;
	jump = virt_to_bus(jump_addr);
#ifdef PLANB_GSCANLINE
	for (i = 1; i < nlines; i += stepsize) {
		tab_cmd_gen(c1++, INPUT_MORE | BR_IFSET, count,
					virt_to_bus(pb->rawbuf[pagei
					+ i * scanline / PAGE_SIZE]), jump);
	}
#else
	i = 1;
	leftover1 = 0;
	pagei = pb->gbuf_idx[fr];
	if(nlines <= 1)
	    goto skip;
	do {
	    int j;

	    base = virt_to_bus(pb->rawbuf[pagei]);
	    nlpp = (PAGE_SIZE - leftover1) / count / stepsize;
	    if(leftover1 >= count) {
		tab_cmd_gen(c1++, INPUT_MORE | KEY_STREAM0 | BR_IFSET, count,
						base + leftover1 - count, jump);
		i += stepsize;
	    }
	    for(j = 0; j < nlpp && i < nlines; j++, i += stepsize, c1++)
		tab_cmd_gen(c1, INPUT_MORE | KEY_STREAM0 | BR_IFSET, count,
			base + count * (j * stepsize + 1) + leftover1, jump);
	    if(i < nlines) {
		int lov0 = PAGE_SIZE - count * nlpp * stepsize - leftover1;

		if(lov0 == 0)
		    leftover1 = 0;
		else {
		    if(lov0 > count) {
			pb->l_to_addr[fr][pb->lnum[fr]] = pb->rawbuf[pagei]
				+ count * (nlpp * stepsize + 1) + leftover1;
			pb->l_to_next_idx[fr][pb->lnum[fr]] = pagei + 1;
			pb->l_to_next_size[fr][pb->lnum[fr]] = count * stepsize
									- lov0;
			tab_cmd_gen(c1++, INPUT_MORE | BR_IFSET, count,
				virt_to_bus(pb->rawbuf[pb->l_fr_addr_idx[fr]
							+ pb->lnum[fr]]), jump);
			if(++pb->lnum[fr] > MAX_LNUM)
				pb->lnum[fr]--;
			i += stepsize;
		    }
		    leftover1 = count * stepsize - lov0;
		}
	    }
	    pagei++;
	} while(i < nlines);
skip:
	tab_cmd_dbdma(c1, DBDMA_NOP | BR_ALWAYS, jump);
	c1 = jump_addr;
#endif /* PLANB_GSCANLINE */

cmd_tab_data_end:
	tab_cmd_store(c1++, (unsigned)(&pb->planb_base_phys->intr_stat),
			(fr << 9) | PLANB_FRM_IRQ | PLANB_GEN_IRQ);
	/* stop it */
	tab_cmd_dbdma(c1, DBDMA_STOP, 0);

	eieio();
	return c1;
}

static void planb_irq(int irq, void *dev_id, struct pt_regs * regs)
{
	unsigned int stat, astat;
	struct planb *pb = (struct planb *)dev_id;

	IDEBUG("PlanB: planb_irq()\n");

	/* get/clear interrupt status bits */
	eieio();
	stat = in_le32(&pb->planb_base->intr_stat);
	astat = stat & pb->intr_mask;
	out_le32(&pb->planb_base->intr_stat, PLANB_FRM_IRQ
					& ~astat & stat & ~PLANB_GEN_IRQ);
	IDEBUG("PlanB: stat = %X, astat = %X\n", stat, astat);

	if(astat & PLANB_FRM_IRQ) {
		unsigned int fr = stat >> 9;
#ifndef PLANB_GSCANLINE
		int i;
#endif
		IDEBUG("PlanB: PLANB_FRM_IRQ\n");

		pb->gcount++;

		IDEBUG("PlanB: grab %d: fr = %d, gcount = %d\n",
				pb->grabbing, fr, pb->gcount);
#ifndef PLANB_GSCANLINE
		IDEBUG("PlanB: %d * %d bytes are being copied over\n",
				pb->lnum[fr], pb->lsize[fr]);
		for(i = 0; i < pb->lnum[fr]; i++) {
			int first = pb->lsize[fr] - pb->l_to_next_size[fr][i];

			memcpy(pb->l_to_addr[fr][i],
				pb->rawbuf[pb->l_fr_addr_idx[fr] + i],
				first);
			memcpy(pb->rawbuf[pb->l_to_next_idx[fr][i]],
				pb->rawbuf[pb->l_fr_addr_idx[fr] + i] + first,
						pb->l_to_next_size[fr][i]);
		}
#endif
		pb->frame_stat[fr] = GBUFFER_DONE;
		pb->grabbing--;
		wake_up_interruptible(&pb->capq);
		return;
	}
	/* incorrect interrupts? */
	pb->intr_mask = PLANB_CLR_IRQ;
	out_le32(&pb->planb_base->intr_stat, PLANB_CLR_IRQ);
	printk(KERN_ERR "PlanB: IRQ lockup, cleared intrrupts"
							" unconditionally\n");
}

/*******************************
 * Device Operations functions *
 *******************************/

static int planb_open(struct video_device *dev, int mode)
{
	struct planb *pb = (struct planb *)dev;

	if (pb->user == 0) {
		int err;
		if((err = planb_prepare_open(pb)) != 0)
			return err;
	}
	pb->user++;

	DEBUG("PlanB: device opened\n");
	return 0;   
}

static void planb_close(struct video_device *dev)
{
	struct planb *pb = (struct planb *)dev;

	if(pb->user < 1) /* ??? */
		return;
	planb_lock(pb);
	if (pb->user == 1) {
		if (pb->overlay) {
			planb_dbdma_stop(&pb->planb_base->ch2);
			planb_dbdma_stop(&pb->planb_base->ch1);
			pb->overlay = 0;
		}
		planb_prepare_close(pb);
	}
	pb->user--;
	planb_unlock(pb);

	DEBUG("PlanB: device closed\n");
}

static long planb_read(struct video_device *v, char *buf, unsigned long count,
				int nonblock)
{
	DEBUG("planb: read request\n");
	return -EINVAL;
}

static long planb_write(struct video_device *v, const char *buf,
				unsigned long count, int nonblock)
{
	DEBUG("planb: write request\n");
	return -EINVAL;
}

static int planb_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct planb *pb=(struct planb *)dev;
  	
	switch (cmd)
	{	
		case VIDIOCGCAP:
		{
			struct video_capability b;

			DEBUG("PlanB: IOCTL VIDIOCGCAP\n");

			strcpy (b.name, pb->video_dev.name);
			b.type = VID_TYPE_OVERLAY | VID_TYPE_CLIPPING |
				 VID_TYPE_FRAMERAM | VID_TYPE_SCALES |
				 VID_TYPE_CAPTURE;
			b.channels = 2;	/* composite & svhs */
			b.audios = 0;
			b.maxwidth = PLANB_MAXPIXELS;
                        b.maxheight = PLANB_MAXLINES;
                        b.minwidth = 32; /* wild guess */
                        b.minheight = 32;
                        if (copy_to_user(arg,&b,sizeof(b)))
                                return -EFAULT;
			return 0;
		}
		case VIDIOCSFBUF:
		{
                        struct video_buffer v;
			unsigned short bpp;
			unsigned int fmt;

			DEBUG("PlanB: IOCTL VIDIOCSFBUF\n");

                        if (!capable(CAP_SYS_ADMIN)
			|| !capable(CAP_SYS_RAWIO))
                                return -EPERM;
                        if (copy_from_user(&v, arg,sizeof(v)))
                                return -EFAULT;
			planb_lock(pb);
			switch(v.depth) {
			case 8:
				bpp = 1;
				fmt = PLANB_GRAY;
				break;
			case 15:
			case 16:
				bpp = 2;
				fmt = PLANB_COLOUR15;
				break;
			case 24:
			case 32:
				bpp = 4;
				fmt = PLANB_COLOUR32;
				break;
			default:
				planb_unlock(pb);
                                return -EINVAL;
			}
			if (bpp * v.width > v.bytesperline) {
				planb_unlock(pb);
				return -EINVAL;
			}
			pb->win.bpp = bpp;
			pb->win.color_fmt = fmt;
			pb->frame_buffer_phys = (unsigned long) v.base;
			pb->win.sheight = v.height;
			pb->win.swidth = v.width;
			pb->picture.depth = pb->win.depth = v.depth;
			pb->win.bpl = pb->win.bpp * pb->win.swidth;
			pb->win.pad = v.bytesperline - pb->win.bpl;

                        DEBUG("PlanB: Display at %p is %d by %d, bytedepth %d,"
				" bpl %d (+ %d)\n", v.base, v.width,v.height,
				pb->win.bpp, pb->win.bpl, pb->win.pad);

			pb->cmd_buff_inited = 0;
			if(pb->overlay) {
				suspend_overlay(pb);
				fill_cmd_buff(pb);
				resume_overlay(pb);
			}
			planb_unlock(pb);
			return 0;		
		}
		case VIDIOCGFBUF:
		{
                        struct video_buffer v;

			DEBUG("PlanB: IOCTL VIDIOCGFBUF\n");

			v.base = (void *)pb->frame_buffer_phys;
			v.height = pb->win.sheight;
			v.width = pb->win.swidth;
			v.depth = pb->win.depth;
			v.bytesperline = pb->win.bpl + pb->win.pad;
			if (copy_to_user(arg, &v, sizeof(v)))
                                return -EFAULT;
			return 0;
		}
		case VIDIOCCAPTURE:
		{
			int i;

                        if(copy_from_user(&i, arg, sizeof(i)))
                                return -EFAULT;
			if(i==0) {
				DEBUG("PlanB: IOCTL VIDIOCCAPTURE Stop\n");

				if (!(pb->overlay))
					return 0;
				planb_lock(pb);
				pb->overlay = 0;
				overlay_stop(pb);
				planb_unlock(pb);
			} else {
				DEBUG("PlanB: IOCTL VIDIOCCAPTURE Start\n");

				if (pb->frame_buffer_phys == 0 ||
					  pb->win.width == 0 ||
					  pb->win.height == 0)
					return -EINVAL;
				if (pb->overlay)
					return 0;
				planb_lock(pb);
				pb->overlay = 1;
				if(!(pb->cmd_buff_inited))
					fill_cmd_buff(pb);
				overlay_start(pb);
				planb_unlock(pb);
			}
			return 0;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel v;

			DEBUG("PlanB: IOCTL VIDIOCGCHAN\n");

			if(copy_from_user(&v, arg,sizeof(v)))
				return -EFAULT;
			v.flags = 0;
			v.tuners = 0;
			v.type = VIDEO_TYPE_CAMERA;
			v.norm = pb->win.norm;
			switch(v.channel)
			{
			case 0:
				strcpy(v.name,"Composite");
				break;
			case 1:
				strcpy(v.name,"SVHS");
				break;
			default:
				return -EINVAL;
				break;
			}
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCSCHAN:
		{
			struct video_channel v;

			DEBUG("PlanB: IOCTL VIDIOCSCHAN\n");

			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;

			if (v.norm != pb->win.norm) {
				int i, maxlines;

				switch (v.norm)
				{
				case VIDEO_MODE_PAL:
				case VIDEO_MODE_SECAM:
					maxlines = PLANB_MAXLINES;
					break;
				case VIDEO_MODE_NTSC:
					maxlines = PLANB_NTSC_MAXLINES;
					break;
				default:
					return -EINVAL;
					break;
				}
				planb_lock(pb);
				/* empty the grabbing queue */
				wait_event(pb->capq, !pb->grabbing);
				pb->maxlines = maxlines;
				pb->win.norm = v.norm;
				/* Stop overlay if running */
				suspend_overlay(pb);
				for(i = 0; i < MAX_GBUFFERS; i++)
					pb->gnorm_switch[i] = 1;
				/* I know it's an overkill, but.... */
				fill_cmd_buff(pb);
				/* ok, now init it accordingly */
				saa_init_regs (pb);
				/* restart overlay if it was running */
				resume_overlay(pb);
				planb_unlock(pb);
			}

			switch(v.channel)
			{
			case 0:	/* Composite	*/
				saa_set (SAA7196_IOCC,
					((saa_regs[pb->win.norm][SAA7196_IOCC] &
					  ~7) | 3), pb);
				break;
			case 1:	/* SVHS		*/
				saa_set (SAA7196_IOCC,
					((saa_regs[pb->win.norm][SAA7196_IOCC] &
					  ~7) | 4), pb);
				break;
			default:
				return -EINVAL;
				break;
			}

			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture vp = pb->picture;

			DEBUG("PlanB: IOCTL VIDIOCGPICT\n");

			switch(pb->win.color_fmt) {
			case PLANB_GRAY:
				vp.palette = VIDEO_PALETTE_GREY;
			case PLANB_COLOUR15:
				vp.palette = VIDEO_PALETTE_RGB555;
				break;
			case PLANB_COLOUR32:
				vp.palette = VIDEO_PALETTE_RGB32;
				break;
			default:
				vp.palette = 0;
				break;
			}

			if(copy_to_user(arg,&vp,sizeof(vp)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture vp;

			DEBUG("PlanB: IOCTL VIDIOCSPICT\n");

			if(copy_from_user(&vp,arg,sizeof(vp)))
				return -EFAULT;
			pb->picture = vp;
			/* Should we do sanity checks here? */
			saa_set (SAA7196_BRIG, (unsigned char)
			    ((pb->picture.brightness) >> 8), pb);
			saa_set (SAA7196_HUEC, (unsigned char)
			    ((pb->picture.hue) >> 8) ^ 0x80, pb);
			saa_set (SAA7196_CSAT, (unsigned char)
			    ((pb->picture.colour) >> 9), pb);
			saa_set (SAA7196_CONT, (unsigned char)
			    ((pb->picture.contrast) >> 9), pb);

			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window	vw;
			struct video_clip	clip;
			int 			i;
			
			DEBUG("PlanB: IOCTL VIDIOCSWIN\n");

			if(copy_from_user(&vw,arg,sizeof(vw)))
				return -EFAULT;

			planb_lock(pb);
			/* Stop overlay if running */
			suspend_overlay(pb);
			pb->win.interlace = (vw.height > pb->maxlines/2)? 1: 0;
			if (pb->win.x != vw.x ||
			    pb->win.y != vw.y ||
			    pb->win.width != vw.width ||
			    pb->win.height != vw.height ||
			    !pb->cmd_buff_inited) {
				pb->win.x = vw.x;
				pb->win.y = vw.y;
				pb->win.width = vw.width;
				pb->win.height = vw.height;
				fill_cmd_buff(pb);
			}
			/* Reset clip mask */
			memset ((void *) pb->mask, 0xff, (pb->maxlines
					* ((PLANB_MAXPIXELS + 7) & ~7)) / 8);
			/* Add any clip rects */
			for (i = 0; i < vw.clipcount; i++) {
				if (copy_from_user(&clip, vw.clips + i,
						sizeof(struct video_clip)))
					return -EFAULT;
				add_clip(pb, &clip);
			}
			/* restart overlay if it was running */
			resume_overlay(pb);
			planb_unlock(pb);
			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;

			DEBUG("PlanB: IOCTL VIDIOCGWIN\n");

			vw.x=pb->win.x;
			vw.y=pb->win.y;
			vw.width=pb->win.width;
			vw.height=pb->win.height;
			vw.chromakey=0;
			vw.flags=0;
			if(pb->win.interlace)
				vw.flags|=VIDEO_WINDOW_INTERLACE;
			if(copy_to_user(arg,&vw,sizeof(vw)))
				return -EFAULT;
			return 0;
		}
	        case VIDIOCSYNC: {
			int i;

			IDEBUG("PlanB: IOCTL VIDIOCSYNC\n");

			if(copy_from_user((void *)&i,arg,sizeof(int)))
				return -EFAULT;

			IDEBUG("PlanB: sync to frame %d\n", i);

                        if(i > (MAX_GBUFFERS - 1) || i < 0)
                                return -EINVAL;
chk_grab:
                        switch (pb->frame_stat[i]) {
                        case GBUFFER_UNUSED:
                                return -EINVAL;
			case GBUFFER_GRABBING:
				IDEBUG("PlanB: waiting for grab"
							" done (%d)\n", i);
 			        interruptible_sleep_on(&pb->capq);
				if(signal_pending(current))
					return -EINTR;
				goto chk_grab;
                        case GBUFFER_DONE:
                                pb->frame_stat[i] = GBUFFER_UNUSED;
                                break;
                        }
                        return 0;
		}

	        case VIDIOCMCAPTURE:
		{
                        struct video_mmap vm;
			volatile unsigned int status;

			IDEBUG("PlanB: IOCTL VIDIOCMCAPTURE\n");

			if(copy_from_user((void *) &vm,(void *)arg,sizeof(vm)))
				return -EFAULT;
                        status = pb->frame_stat[vm.frame];
                        if (status != GBUFFER_UNUSED)
                                return -EBUSY;

		        return vgrab(pb, &vm);
		}
		
		case VIDIOCGMBUF:
		{
			int i;
			struct video_mbuf vm;

			DEBUG("PlanB: IOCTL VIDIOCGMBUF\n");

			memset(&vm, 0 , sizeof(vm));
			vm.size = PLANB_MAX_FBUF * MAX_GBUFFERS;
			vm.frames = MAX_GBUFFERS;
			for(i = 0; i<MAX_GBUFFERS; i++)
				vm.offsets[i] = PLANB_MAX_FBUF * i;
			if(copy_to_user((void *)arg, (void *)&vm, sizeof(vm)))
				return -EFAULT;
			return 0;
		}
		
		case PLANBIOCGSAAREGS:
		{
			struct planb_saa_regs preg;

			DEBUG("PlanB: IOCTL PLANBIOCGSAAREGS\n");

			if(copy_from_user(&preg, arg, sizeof(preg)))
				return -EFAULT;
			if(preg.addr >= SAA7196_NUMREGS)
				return -EINVAL;
			preg.val = saa_regs[pb->win.norm][preg.addr];
			if(copy_to_user((void *)arg, (void *)&preg,
								sizeof(preg)))
				return -EFAULT;
			return 0;
		}
		
		case PLANBIOCSSAAREGS:
		{
			struct planb_saa_regs preg;

			DEBUG("PlanB: IOCTL PLANBIOCSSAAREGS\n");

			if(copy_from_user(&preg, arg, sizeof(preg)))
				return -EFAULT;
			if(preg.addr >= SAA7196_NUMREGS)
				return -EINVAL;
			saa_set (preg.addr, preg.val, pb);
			return 0;
		}
		
		case PLANBIOCGSTAT:
		{
			struct planb_stat_regs pstat;

			DEBUG("PlanB: IOCTL PLANBIOCGSTAT\n");

			pstat.ch1_stat = in_le32(&pb->planb_base->ch1.status);
			pstat.ch2_stat = in_le32(&pb->planb_base->ch2.status);
			pstat.saa_stat0 = saa_status(0, pb);
			pstat.saa_stat1 = saa_status(1, pb);

			if(copy_to_user((void *)arg, (void *)&pstat,
							sizeof(pstat)))
				return -EFAULT;
			return 0;
		}
		
		case PLANBIOCSMODE: {
			int v;

			DEBUG("PlanB: IOCTL PLANBIOCSMODE\n");

			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;

			switch(v)
			{
			case PLANB_TV_MODE:
				saa_set (SAA7196_STDC,
					(saa_regs[pb->win.norm][SAA7196_STDC] &
					  0x7f), pb);
				break;
			case PLANB_VTR_MODE:
				saa_set (SAA7196_STDC,
					(saa_regs[pb->win.norm][SAA7196_STDC] |
					  0x80), pb);
				break;
			default:
				return -EINVAL;
				break;
			}
			pb->win.mode = v;
			return 0;
		}
		case PLANBIOCGMODE: {
			int v=pb->win.mode;

			DEBUG("PlanB: IOCTL PLANBIOCGMODE\n");

			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
#ifdef PLANB_GSCANLINE
		case PLANBG_GRAB_BPL: {
			int v=pb->gbytes_per_line;

			DEBUG("PlanB: IOCTL PLANBG_GRAB_BPL\n");

			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
#endif /* PLANB_GSCANLINE */
		case PLANB_INTR_DEBUG: {
			int i;

			DEBUG("PlanB: IOCTL PLANB_INTR_DEBUG\n");

			if(copy_from_user(&i, arg, sizeof(i)))
				return -EFAULT;

			/* avoid hang ups all together */
			for (i = 0; i < MAX_GBUFFERS; i++) {
				if(pb->frame_stat[i] == GBUFFER_GRABBING) {
					pb->frame_stat[i] = GBUFFER_DONE;
				}
			}
			if(pb->grabbing)
				pb->grabbing--;
			wake_up_interruptible(&pb->capq);
			return 0;
		}
		case PLANB_INV_REGS: {
			int i;
			struct planb_any_regs any;

			DEBUG("PlanB: IOCTL PLANB_INV_REGS\n");

			if(copy_from_user(&any, arg, sizeof(any)))
				return -EFAULT;
			if(any.offset < 0 || any.offset + any.bytes > 0x400)
				return -EINVAL;
			if(any.bytes > 128)
				return -EINVAL;
			for (i = 0; i < any.bytes; i++) {
				any.data[i] =
					in_8((unsigned char *)pb->planb_base
							+ any.offset + i);
			}
			if(copy_to_user(arg,&any,sizeof(any)))
				return -EFAULT;
			return 0;
		}
		default:
		{
			DEBUG("PlanB: Unimplemented IOCTL\n");
			return -ENOIOCTLCMD;
		}
	/* Some IOCTLs are currently unsupported on PlanB */
		case VIDIOCGTUNER: {
		DEBUG("PlanB: IOCTL VIDIOCGTUNER\n");
			goto unimplemented; }
		case VIDIOCSTUNER: {
		DEBUG("PlanB: IOCTL VIDIOCSTUNER\n");
			goto unimplemented; }
		case VIDIOCSFREQ: {
		DEBUG("PlanB: IOCTL VIDIOCSFREQ\n");
			goto unimplemented; }
		case VIDIOCGFREQ: {
		DEBUG("PlanB: IOCTL VIDIOCGFREQ\n");
			goto unimplemented; }
		case VIDIOCKEY: {
		DEBUG("PlanB: IOCTL VIDIOCKEY\n");
			goto unimplemented; }
		case VIDIOCSAUDIO: {
		DEBUG("PlanB: IOCTL VIDIOCSAUDIO\n");
			goto unimplemented; }
		case VIDIOCGAUDIO: {
		DEBUG("PlanB: IOCTL VIDIOCGAUDIO\n");
			goto unimplemented; }
unimplemented:
		DEBUG("       Unimplemented\n");
			return -ENOIOCTLCMD;
	}
	return 0;
}

static int planb_mmap(struct vm_area_struct *vma, struct video_device *dev, const char *adr, unsigned long size)
{
	int i;
	struct planb *pb = (struct planb *)dev;
        unsigned long start = (unsigned long)adr;

	if (size > MAX_GBUFFERS * PLANB_MAX_FBUF)
	        return -EINVAL;
	if (!pb->rawbuf) {
		int err;
		if((err=grabbuf_alloc(pb)))
			return err;
	}
	for (i = 0; i < pb->rawbuf_size; i++) {
		unsigned long pfn;

		pfn = virt_to_phys((void *)pb->rawbuf[i]) >> PAGE_SHIFT;
		if (remap_pfn_range(vma, start, pfn, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		start += PAGE_SIZE;
		if (size <= PAGE_SIZE)
			break;
		size -= PAGE_SIZE;
	}
	return 0;
}

static struct video_device planb_template=
{
	.owner		= THIS_MODULE,
	.name		= PLANB_DEVICE_NAME,
	.type		= VID_TYPE_OVERLAY,
	.hardware	= VID_HARDWARE_PLANB,
	.open		= planb_open,
	.close		= planb_close,
	.read		= planb_read,
	.write		= planb_write,
	.ioctl		= planb_ioctl,
	.mmap		= planb_mmap,	/* mmap? */
};

static int init_planb(struct planb *pb)
{
	unsigned char saa_rev;
	int i, result;

	memset ((void *) &pb->win, 0, sizeof (struct planb_window));
	/* Simple sanity check */
	if(def_norm >= NUM_SUPPORTED_NORM || def_norm < 0) {
		printk(KERN_ERR "PlanB: Option(s) invalid\n");
		return -2;
	}
	pb->win.norm = def_norm;
	pb->win.mode = PLANB_TV_MODE;	/* TV mode */
	pb->win.interlace=1;
	pb->win.x=0;
	pb->win.y=0;
	pb->win.width=768; /* 640 */
	pb->win.height=576; /* 480 */
	pb->maxlines=576;
#if 0
	btv->win.cropwidth=768; /* 640 */
	btv->win.cropheight=576; /* 480 */
	btv->win.cropx=0;
	btv->win.cropy=0;
#endif
	pb->win.pad=0;
	pb->win.bpp=4;
	pb->win.depth=32;
	pb->win.color_fmt=PLANB_COLOUR32;
	pb->win.bpl=1024*pb->win.bpp;
	pb->win.swidth=1024;
	pb->win.sheight=768;
#ifdef PLANB_GSCANLINE
	if((pb->gbytes_per_line = PLANB_MAXPIXELS * 4) > PAGE_SIZE
				|| (pb->gbytes_per_line <= 0))
		return -3;
	else {
		/* page align pb->gbytes_per_line for DMA purpose */
		for(i = PAGE_SIZE; pb->gbytes_per_line < (i>>1);)
			i>>=1;
		pb->gbytes_per_line = i;
	}
#endif
	pb->tab_size = PLANB_MAXLINES + 40;
	pb->suspend = 0;
	init_MUTEX(&pb->lock);
	pb->ch1_cmd = 0;
	pb->ch2_cmd = 0;
	pb->mask = 0;
	pb->priv_space = 0;
	pb->offset = 0;
	pb->user = 0;
	pb->overlay = 0;
	init_waitqueue_head(&pb->suspendq);
	pb->cmd_buff_inited = 0;
	pb->frame_buffer_phys = 0;

	/* Reset DMA controllers */
	planb_dbdma_stop(&pb->planb_base->ch2);
	planb_dbdma_stop(&pb->planb_base->ch1);

	saa_rev =  (saa_status(0, pb) & 0xf0) >> 4;
	printk(KERN_INFO "PlanB: SAA7196 video processor rev. %d\n", saa_rev);
	/* Initialize the SAA registers in memory and on chip */
	saa_init_regs (pb);

	/* clear interrupt mask */
	pb->intr_mask = PLANB_CLR_IRQ;

        result = request_irq(pb->irq, planb_irq, 0, "PlanB", (void *)pb);
        if (result < 0) {
	        if (result==-EINVAL)
	                printk(KERN_ERR "PlanB: Bad irq number (%d) "
						"or handler\n", (int)pb->irq);
		else if (result==-EBUSY)
			printk(KERN_ERR "PlanB: I don't know why, "
					"but IRQ %d is busy\n", (int)pb->irq);
		return result;
	}
	disable_irq(pb->irq);
        
	/* Now add the template and register the device unit. */
	memcpy(&pb->video_dev,&planb_template,sizeof(planb_template));

	pb->picture.brightness=0x90<<8;
	pb->picture.contrast = 0x70 << 8;
	pb->picture.colour = 0x70<<8;
	pb->picture.hue = 0x8000;
	pb->picture.whiteness = 0;
	pb->picture.depth = pb->win.depth;

	pb->frame_stat=NULL;
	init_waitqueue_head(&pb->capq);
	for(i=0; i<MAX_GBUFFERS; i++) {
		pb->gbuf_idx[i] = PLANB_MAX_FBUF * i / PAGE_SIZE;
		pb->gwidth[i]=0;
		pb->gheight[i]=0;
		pb->gfmt[i]=0;
		pb->cap_cmd[i]=NULL;
#ifndef PLANB_GSCANLINE
		pb->l_fr_addr_idx[i] = MAX_GBUFFERS * (PLANB_MAX_FBUF
						/ PAGE_SIZE + 1) + MAX_LNUM * i;
		pb->lsize[i] = 0;
		pb->lnum[i] = 0;
#endif
	}
	pb->rawbuf=NULL;
	pb->grabbing=0;

	/* enable interrupts */
	out_le32(&pb->planb_base->intr_stat, PLANB_CLR_IRQ);
	pb->intr_mask = PLANB_FRM_IRQ;
	enable_irq(pb->irq);

	if(video_register_device(&pb->video_dev, VFL_TYPE_GRABBER, video_nr)<0)
		return -1;

	return 0;
}

/*
 *	Scan for a PlanB controller, request the irq and map the io memory 
 */

static int find_planb(void)
{
	struct planb		*pb;
	struct device_node	*planb_devices;
	unsigned char		dev_fn, confreg, bus;
	unsigned int		old_base, new_base;
	unsigned int		irq;
	struct pci_dev 		*pdev;
	int rc;

	if (_machine != _MACH_Pmac)
		return 0;

	planb_devices = find_devices("planb");
	if (planb_devices == 0) {
		planb_num=0;
		printk(KERN_WARNING "PlanB: no device found!\n");
		return planb_num;
	}

	if (planb_devices->next != NULL)
		printk(KERN_ERR "Warning: only using first PlanB device!\n");
	pb = &planbs[0];
	planb_num = 1;

        if (planb_devices->n_addrs != 1) {
                printk (KERN_WARNING "PlanB: expecting 1 address for planb "
                	"(got %d)", planb_devices->n_addrs);
		return 0;
	}

	if (planb_devices->n_intrs == 0) {
		printk(KERN_WARNING "PlanB: no intrs for device %s\n",
		       planb_devices->full_name);
		return 0;
	} else {
		irq = planb_devices->intrs[0].line;
	}

	/* Initialize PlanB's PCI registers */

	/* There is a bug with the way OF assigns addresses
	   to the devices behind the chaos bridge.
	   control needs only 0x1000 of space, but decodes only
	   the upper 16 bits. It therefore occupies a full 64K.
	   OF assigns the planb controller memory within this space;
	   so we need to change that here in order to access planb. */

	/* We remap to 0xf1000000 in hope that nobody uses it ! */

	bus = (planb_devices->addrs[0].space >> 16) & 0xff;
	dev_fn = (planb_devices->addrs[0].space >> 8) & 0xff;
	confreg = planb_devices->addrs[0].space & 0xff;
	old_base = planb_devices->addrs[0].address;
	new_base = 0xf1000000;

	DEBUG("PlanB: Found on bus %d, dev %d, func %d, "
		"membase 0x%x (base reg. 0x%x)\n",
		bus, PCI_SLOT(dev_fn), PCI_FUNC(dev_fn), old_base, confreg);

	pdev = pci_find_slot (bus, dev_fn);
	if (!pdev) {
		printk(KERN_ERR "planb: cannot find slot\n");
		goto err_out;
	}

	/* Enable response in memory space, bus mastering,
	   use memory write and invalidate */
	rc = pci_enable_device(pdev);
	if (rc) {
		printk(KERN_ERR "planb: cannot enable PCI device %s\n",
		       pci_name(pdev));
		goto err_out;
	}
	rc = pci_set_mwi(pdev);
	if (rc) {
		printk(KERN_ERR "planb: cannot enable MWI on PCI device %s\n",
		       pci_name(pdev));
		goto err_out_disable;
	}
	pci_set_master(pdev);

	/* Set the new base address */
	pci_write_config_dword (pdev, confreg, new_base);

	planb_regs = (volatile struct planb_registers *)
						ioremap (new_base, 0x400);
	pb->planb_base = planb_regs;
	pb->planb_base_phys = (struct planb_registers *)new_base;
	pb->irq	= irq;
	
	return planb_num;

err_out_disable:
	pci_disable_device(pdev);
err_out:
	/* FIXME handle error */   /* comment moved from pci_find_slot, above */
	return 0;
}

static void release_planb(void)
{
	int i;
	struct planb *pb;

	for (i=0;i<planb_num; i++) 
	{
		pb=&planbs[i];

		/* stop and flash DMAs unconditionally */
		planb_dbdma_stop(&pb->planb_base->ch2);
		planb_dbdma_stop(&pb->planb_base->ch1);

		/* clear and free interrupts */
		pb->intr_mask = PLANB_CLR_IRQ;
		out_le32 (&pb->planb_base->intr_stat, PLANB_CLR_IRQ);
		free_irq(pb->irq, pb);

		/* make sure all allocated memory are freed */
		planb_prepare_close(pb);

		printk(KERN_INFO "PlanB: unregistering with v4l\n");
		video_unregister_device(&pb->video_dev);

		/* note that iounmap() does nothing on the PPC right now */
		iounmap ((void *)pb->planb_base);
	}
}

static int __init init_planbs(void)
{
	int i;
  
	if (find_planb()<=0)
		return -EIO;

	for (i=0; i<planb_num; i++) {
		if (init_planb(&planbs[i])<0) {
			printk(KERN_ERR "PlanB: error registering device %d"
							" with v4l\n", i);
			release_planb();
			return -EIO;
		} 
		printk(KERN_INFO "PlanB: registered device %d with v4l\n", i);
	}  
	return 0;
}

static void __exit exit_planbs(void)
{
	release_planb();
}

module_init(init_planbs);
module_exit(exit_planbs);
