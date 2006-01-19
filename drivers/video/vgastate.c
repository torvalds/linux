/*
 * linux/drivers/video/vgastate.c -- VGA state save/restore
 *
 * Copyright 2002 James Simmons
 * 
 * Copyright history from vga16fb.c:
 *	Copyright 1999 Ben Pfaff and Petr Vandrovec
 *	Based on VGA info at http://www.goodnet.com/~tinara/FreeVGA/home.htm
 *	Based on VESA framebuffer (c) 1998 Gerd Knorr
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.  
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <video/vga.h>

struct regstate {
	__u8 *vga_font0;
	__u8 *vga_font1;
	__u8 *vga_text;
	__u8 *vga_cmap;
	__u8 *attr;
	__u8 *crtc;
	__u8 *gfx;
	__u8 *seq;
	__u8 misc;
};	

static inline unsigned char vga_rcrtcs(void __iomem *regbase, unsigned short iobase, 
				       unsigned char reg)
{
	vga_w(regbase, iobase + 0x4, reg);
	return vga_r(regbase, iobase + 0x5);
}

static inline void vga_wcrtcs(void __iomem *regbase, unsigned short iobase, 
			      unsigned char reg, unsigned char val)
{
	vga_w(regbase, iobase + 0x4, reg);
	vga_w(regbase, iobase + 0x5, val);
}

static void save_vga_text(struct vgastate *state, void __iomem *fbbase)
{
	struct regstate *saved = (struct regstate *) state->vidstate;
	int i;
	u8 misc, attr10, gr4, gr5, gr6, seq1, seq2, seq4;

	/* if in graphics mode, no need to save */
	attr10 = vga_rattr(state->vgabase, 0x10);
	if (attr10 & 1)
		return;
	
	/* save regs */
	misc = vga_r(state->vgabase, VGA_MIS_R);
	gr4 = vga_rgfx(state->vgabase, VGA_GFX_PLANE_READ);
	gr5 = vga_rgfx(state->vgabase, VGA_GFX_MODE);
	gr6 = vga_rgfx(state->vgabase, VGA_GFX_MISC);
	seq2 = vga_rseq(state->vgabase, VGA_SEQ_PLANE_WRITE);
	seq4 = vga_rseq(state->vgabase, VGA_SEQ_MEMORY_MODE);
	
	/* force graphics mode */
	vga_w(state->vgabase, VGA_MIS_W, misc | 1);

	/* blank screen */
	seq1 = vga_rseq(state->vgabase, VGA_SEQ_CLOCK_MODE);
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x1);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1 | 1 << 5);
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x3);

	/* save font at plane 2 */
	if (state->flags & VGA_SAVE_FONT0) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x4);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x2);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 4 * 8192; i++) 
			saved->vga_font0[i] = vga_r(fbbase, i);
	}

	/* save font at plane 3 */
	if (state->flags & VGA_SAVE_FONT1) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x8);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x3);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < state->memsize; i++) 
			saved->vga_font1[i] = vga_r(fbbase, i);
	}
	
	/* save font at plane 0/1 */
	if (state->flags & VGA_SAVE_TEXT) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x1);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 8192; i++) 
			saved->vga_text[i] = vga_r(fbbase, i);

		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x2);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x1);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 8192; i++) 
			saved->vga_text[8192+i] = vga_r(fbbase + 2 * 8192, i); 
	}

	/* restore regs */
	vga_wattr(state->vgabase, 0x10, attr10);

	vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, seq2);
	vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, seq4);

	vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, gr4);
	vga_wgfx(state->vgabase, VGA_GFX_MODE, gr5);
	vga_wgfx(state->vgabase, VGA_GFX_MISC, gr6);
	vga_w(state->vgabase, VGA_MIS_W, misc);

	/* unblank screen */
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x1);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1 & ~(1 << 5));
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x3);

	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1);
}

static void restore_vga_text(struct vgastate *state, void __iomem *fbbase)
{
	struct regstate *saved = (struct regstate *) state->vidstate;
	int i;
	u8 misc, gr1, gr3, gr4, gr5, gr6, gr8; 
	u8 seq1, seq2, seq4;

	/* save regs */
	misc = vga_r(state->vgabase, VGA_MIS_R);
	gr1 = vga_rgfx(state->vgabase, VGA_GFX_SR_ENABLE);
	gr3 = vga_rgfx(state->vgabase, VGA_GFX_DATA_ROTATE);
	gr4 = vga_rgfx(state->vgabase, VGA_GFX_PLANE_READ);
	gr5 = vga_rgfx(state->vgabase, VGA_GFX_MODE);
	gr6 = vga_rgfx(state->vgabase, VGA_GFX_MISC);
	gr8 = vga_rgfx(state->vgabase, VGA_GFX_BIT_MASK);
	seq2 = vga_rseq(state->vgabase, VGA_SEQ_PLANE_WRITE);
	seq4 = vga_rseq(state->vgabase, VGA_SEQ_MEMORY_MODE);
	
	/* force graphics mode */
	vga_w(state->vgabase, VGA_MIS_W, misc | 1);

	/* blank screen */
	seq1 = vga_rseq(state->vgabase, VGA_SEQ_CLOCK_MODE);
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x1);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1 | 1 << 5);
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x3);

	if (state->depth == 4) {
		vga_wgfx(state->vgabase, VGA_GFX_DATA_ROTATE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_BIT_MASK, 0xff);
		vga_wgfx(state->vgabase, VGA_GFX_SR_ENABLE, 0x00);
	}
	
	/* restore font at plane 2 */
	if (state->flags & VGA_SAVE_FONT0) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x4);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x2);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 4 * 8192; i++) 
			vga_w(fbbase, i, saved->vga_font0[i]);
	}

	/* restore font at plane 3 */
	if (state->flags & VGA_SAVE_FONT1) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x8);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x3);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < state->memsize; i++) 
			vga_w(fbbase, i, saved->vga_font1[i]);
	}
	
	/* restore font at plane 0/1 */
	if (state->flags & VGA_SAVE_TEXT) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x1);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 8192; i++) 
			vga_w(fbbase, i, saved->vga_text[i]);
		
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x2);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x1);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 8192; i++) 
			vga_w(fbbase, i, saved->vga_text[8192+i]); 
	}

	/* unblank screen */
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x1);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1 & ~(1 << 5));
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x3);

	/* restore regs */
	vga_w(state->vgabase, VGA_MIS_W, misc);

	vga_wgfx(state->vgabase, VGA_GFX_SR_ENABLE, gr1);
	vga_wgfx(state->vgabase, VGA_GFX_DATA_ROTATE, gr3);
	vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, gr4);
	vga_wgfx(state->vgabase, VGA_GFX_MODE, gr5);
	vga_wgfx(state->vgabase, VGA_GFX_MISC, gr6);
	vga_wgfx(state->vgabase, VGA_GFX_BIT_MASK, gr8);

	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1);
	vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, seq2);
	vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, seq4);
}
			      
static void save_vga_mode(struct vgastate *state)
{
	struct regstate *saved = (struct regstate *) state->vidstate;
	unsigned short iobase;
	int i;

	saved->misc = vga_r(state->vgabase, VGA_MIS_R);
	if (saved->misc & 1)
		iobase = 0x3d0;
	else
		iobase = 0x3b0;

	for (i = 0; i < state->num_crtc; i++) 
		saved->crtc[i] = vga_rcrtcs(state->vgabase, iobase, i);
	
	vga_r(state->vgabase, iobase + 0xa); 
	vga_w(state->vgabase, VGA_ATT_W, 0x00);
	for (i = 0; i < state->num_attr; i++) {
		vga_r(state->vgabase, iobase + 0xa);
		saved->attr[i] = vga_rattr(state->vgabase, i);
	}
	vga_r(state->vgabase, iobase + 0xa);
	vga_w(state->vgabase, VGA_ATT_W, 0x20);

	for (i = 0; i < state->num_gfx; i++) 
		saved->gfx[i] = vga_rgfx(state->vgabase, i);

	for (i = 0; i < state->num_seq; i++) 
		saved->seq[i] = vga_rseq(state->vgabase, i);
}

static void restore_vga_mode(struct vgastate *state)
{
	struct regstate *saved = (struct regstate *) state->vidstate;
	unsigned short iobase;
	int i;

	vga_w(state->vgabase, VGA_MIS_W, saved->misc);

	if (saved->misc & 1)
		iobase = 0x3d0;
	else
		iobase = 0x3b0;

	/* turn off display */
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, 
		 saved->seq[VGA_SEQ_CLOCK_MODE] | 0x20);

	/* disable sequencer */
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x01);
	
	/* enable palette addressing */
	vga_r(state->vgabase, iobase + 0xa);
	vga_w(state->vgabase, VGA_ATT_W, 0x00);

	for (i = 2; i < state->num_seq; i++) 
		vga_wseq(state->vgabase, i, saved->seq[i]);


	/* unprotect vga regs */
	vga_wcrtcs(state->vgabase, iobase, 17, saved->crtc[17] & ~0x80);
	for (i = 0; i < state->num_crtc; i++) 
		vga_wcrtcs(state->vgabase, iobase, i, saved->crtc[i]);
	
	for (i = 0; i < state->num_gfx; i++) 
		vga_wgfx(state->vgabase, i, saved->gfx[i]);

	for (i = 0; i < state->num_attr; i++) {
		vga_r(state->vgabase, iobase + 0xa);
		vga_wattr(state->vgabase, i, saved->attr[i]);
	}

	/* reenable sequencer */
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x03);
	/* turn display on */
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, 
		 saved->seq[VGA_SEQ_CLOCK_MODE] & ~(1 << 5));

	/* disable video/palette source */
	vga_r(state->vgabase, iobase + 0xa);
	vga_w(state->vgabase, VGA_ATT_W, 0x20);
}

static void save_vga_cmap(struct vgastate *state)
{
	struct regstate *saved = (struct regstate *) state->vidstate;
	int i;

	vga_w(state->vgabase, VGA_PEL_MSK, 0xff);
	
	/* assumes DAC is readable and writable */
	vga_w(state->vgabase, VGA_PEL_IR, 0x00);
	for (i = 0; i < 768; i++)
		saved->vga_cmap[i] = vga_r(state->vgabase, VGA_PEL_D);
}

static void restore_vga_cmap(struct vgastate *state)
{
	struct regstate *saved = (struct regstate *) state->vidstate;
	int i;

	vga_w(state->vgabase, VGA_PEL_MSK, 0xff);

	/* assumes DAC is readable and writable */
	vga_w(state->vgabase, VGA_PEL_IW, 0x00);
	for (i = 0; i < 768; i++)
		vga_w(state->vgabase, VGA_PEL_D, saved->vga_cmap[i]);
}

static void vga_cleanup(struct vgastate *state)
{
	if (state->vidstate != NULL) {
		struct regstate *saved = (struct regstate *) state->vidstate;

		vfree(saved->vga_font0);
		vfree(saved->vga_font1);
		vfree(saved->vga_text);
		vfree(saved->vga_cmap);
		vfree(saved->attr);
		kfree(saved);
		state->vidstate = NULL;
	}
}
		
int save_vga(struct vgastate *state)
{
	struct regstate *saved;

	saved = kzalloc(sizeof(struct regstate), GFP_KERNEL);

	if (saved == NULL)
		return 1;

	state->vidstate = (void *)saved;
		
	if (state->flags & VGA_SAVE_CMAP) {
		saved->vga_cmap = vmalloc(768);
		if (!saved->vga_cmap) {
			vga_cleanup(state);
			return 1;
		}
		save_vga_cmap(state);
	}

	if (state->flags & VGA_SAVE_MODE) {
		int total;

		if (state->num_attr < 21)
			state->num_attr = 21;
		if (state->num_crtc < 25)
			state->num_crtc = 25;
		if (state->num_gfx < 9)
			state->num_gfx = 9;
		if (state->num_seq < 5)
			state->num_seq = 5;
		total = state->num_attr + state->num_crtc +
			state->num_gfx + state->num_seq;

		saved->attr = vmalloc(total);
		if (!saved->attr) {
			vga_cleanup(state);
			return 1;
		}
		saved->crtc = saved->attr + state->num_attr;
		saved->gfx = saved->crtc + state->num_crtc;
		saved->seq = saved->gfx + state->num_gfx;

		save_vga_mode(state);
	}

	if (state->flags & VGA_SAVE_FONTS) {
		void __iomem *fbbase;

		/* exit if window is less than 32K */
		if (state->memsize && state->memsize < 4 * 8192) {
			vga_cleanup(state);
			return 1;
		}
		if (!state->memsize)
			state->memsize = 8 * 8192;
		
		if (!state->membase)
			state->membase = 0xA0000;

		fbbase = ioremap(state->membase, state->memsize);

		if (!fbbase) {
			vga_cleanup(state);
			return 1;
		}

		/* 
		 * save only first 32K used by vgacon
		 */
		if (state->flags & VGA_SAVE_FONT0) {
			saved->vga_font0 = vmalloc(4 * 8192);
			if (!saved->vga_font0) {
				iounmap(fbbase);
				vga_cleanup(state);
				return 1;
			}
		}
		/* 
		 * largely unused, but if required by the caller
		 * we'll just save everything.
		 */
		if (state->flags & VGA_SAVE_FONT1) {
			saved->vga_font1 = vmalloc(state->memsize);
			if (!saved->vga_font1) {
				iounmap(fbbase);
				vga_cleanup(state);
				return 1;
			}
		}
		/*
		 * Save 8K at plane0[0], and 8K at plane1[16K]
		 */
		if (state->flags & VGA_SAVE_TEXT) {
			saved->vga_text = vmalloc(8192 * 2);
			if (!saved->vga_text) {
				iounmap(fbbase);
				vga_cleanup(state);
				return 1;
			}
		}
		
		save_vga_text(state, fbbase);
		iounmap(fbbase);
	}
	return 0;
}

int restore_vga (struct vgastate *state)
{
	if (state->vidstate == NULL)
		return 1;

	if (state->flags & VGA_SAVE_MODE)
		restore_vga_mode(state);

	if (state->flags & VGA_SAVE_FONTS) {
		void __iomem *fbbase = ioremap(state->membase, state->memsize);

		if (!fbbase) {
			vga_cleanup(state);
			return 1;
		}
		restore_vga_text(state, fbbase);
		iounmap(fbbase);
	}

	if (state->flags & VGA_SAVE_CMAP)
		restore_vga_cmap(state);

	vga_cleanup(state);
	return 0;
}

EXPORT_SYMBOL(save_vga);
EXPORT_SYMBOL(restore_vga);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("VGA State Save/Restore");
MODULE_LICENSE("GPL");

