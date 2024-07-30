// SPDX-License-Identifier: GPL-2.0-only
/*
 * newport_con.c: Abscon for newport hardware
 * 
 * (C) 1998 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 * (C) 1999 Ulf Carlsson (ulfc@thepuffingruop.com)
 * 
 * This driver is based on sgicons.c and cons_newport.
 * 
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/gio_device.h>

#include <video/newport.h>

#include <linux/linux_logo.h>
#include <linux/font.h>

#define NEWPORT_LEN	0x10000

#define FONT_DATA ((unsigned char *)font_vga_8x16.data)

static unsigned char *font_data[MAX_NR_CONSOLES];

static struct newport_regs *npregs;
static unsigned long newport_addr;

static int logo_active;
static int topscan;
static int xcurs_correction = 29;
static int newport_xsize;
static int newport_ysize;
static int newport_has_init;

static int newport_set_def_font(int unit, struct console_font *op);

#define BMASK(c) (c << 24)

#define RENDER(regs, cp) do { \
(regs)->go.zpattern = BMASK((cp)[0x0]); (regs)->go.zpattern = BMASK((cp)[0x1]); \
(regs)->go.zpattern = BMASK((cp)[0x2]); (regs)->go.zpattern = BMASK((cp)[0x3]); \
(regs)->go.zpattern = BMASK((cp)[0x4]); (regs)->go.zpattern = BMASK((cp)[0x5]); \
(regs)->go.zpattern = BMASK((cp)[0x6]); (regs)->go.zpattern = BMASK((cp)[0x7]); \
(regs)->go.zpattern = BMASK((cp)[0x8]); (regs)->go.zpattern = BMASK((cp)[0x9]); \
(regs)->go.zpattern = BMASK((cp)[0xa]); (regs)->go.zpattern = BMASK((cp)[0xb]); \
(regs)->go.zpattern = BMASK((cp)[0xc]); (regs)->go.zpattern = BMASK((cp)[0xd]); \
(regs)->go.zpattern = BMASK((cp)[0xe]); (regs)->go.zpattern = BMASK((cp)[0xf]); \
} while(0)

#define TESTVAL 0xdeadbeef
#define XSTI_TO_FXSTART(val) (((val) & 0xffff) << 11)

static inline void newport_render_background(int xstart, int ystart,
					     int xend, int yend, int ci)
{
	newport_wait(npregs);
	npregs->set.wrmask = 0xffffffff;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
				 NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX
				 | NPORT_DMODE0_STOPY);
	npregs->set.colori = ci;
	npregs->set.xystarti =
	    (xstart << 16) | ((ystart + topscan) & 0x3ff);
	npregs->go.xyendi =
	    ((xend + 7) << 16) | ((yend + topscan + 15) & 0x3ff);
}

static inline void newport_init_cmap(void)
{
	unsigned short i;

	for (i = 0; i < 16; i++) {
		newport_bfwait(npregs);
		newport_cmap_setaddr(npregs, color_table[i]);
		newport_cmap_setrgb(npregs,
				    default_red[i],
				    default_grn[i], default_blu[i]);
	}
}

static const struct linux_logo *newport_show_logo(void)
{
#ifdef CONFIG_LOGO_SGI_CLUT224
	const struct linux_logo *logo = fb_find_logo(8);
	const unsigned char *clut;
	const unsigned char *data;
	unsigned long i;

	if (!logo)
		return NULL;
	clut = logo->clut;
	data = logo->data;

	for (i = 0; i < logo->clutsize; i++) {
		newport_bfwait(npregs);
		newport_cmap_setaddr(npregs, i + 0x20);
		newport_cmap_setrgb(npregs, clut[0], clut[1], clut[2]);
		clut += 3;
	}

	newport_wait(npregs);
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
				 NPORT_DMODE0_CHOST);

	npregs->set.xystarti = ((newport_xsize - logo->width) << 16) | (0);
	npregs->set.xyendi = ((newport_xsize - 1) << 16);
	newport_wait(npregs);

	for (i = 0; i < logo->width*logo->height; i++)
		npregs->go.hostrw0 = *data++ << 24;

	return logo;
#else
	return NULL;
#endif /* CONFIG_LOGO_SGI_CLUT224 */
}

static inline void newport_clear_screen(int xstart, int ystart, int xend,
					int yend, int ci)
{
	if (logo_active)
		return;

	newport_wait(npregs);
	npregs->set.wrmask = 0xffffffff;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
				 NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX
				 | NPORT_DMODE0_STOPY);
	npregs->set.colori = ci;
	npregs->set.xystarti = (xstart << 16) | ystart;
	npregs->go.xyendi = (xend << 16) | yend;
}

static inline void newport_clear_lines(int ystart, int yend, int ci)
{
	ystart = ((ystart << 4) + topscan) & 0x3ff;
	yend = ((yend << 4) + topscan + 15) & 0x3ff;
	newport_clear_screen(0, ystart, 1280 + 63, yend, ci);
}

static void newport_reset(void)
{
	unsigned short treg;
	int i;

	newport_wait(npregs);
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL,
			(treg | VC2_CTRL_EVIDEO));

	treg = newport_vc2_get(npregs, VC2_IREG_CENTRY);
	newport_vc2_set(npregs, VC2_IREG_RADDR, treg);
	npregs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
			       NPORT_DMODE_W2 | VC2_PROTOCOL);
	for (i = 0; i < 128; i++) {
		newport_bfwait(npregs);
		if (i == 92 || i == 94)
			npregs->set.dcbdata0.byshort.s1 = 0xff00;
		else
			npregs->set.dcbdata0.byshort.s1 = 0x0000;
	}

	newport_init_cmap();

	/* turn off popup plane */
	npregs->set.dcbmode = (DCB_XMAP0 | R_DCB_XMAP9_PROTOCOL |
			       XM9_CRS_CONFIG | NPORT_DMODE_W1);
	npregs->set.dcbdata0.bybytes.b3 &= ~XM9_PUPMODE;
	npregs->set.dcbmode = (DCB_XMAP1 | R_DCB_XMAP9_PROTOCOL |
			       XM9_CRS_CONFIG | NPORT_DMODE_W1);
	npregs->set.dcbdata0.bybytes.b3 &= ~XM9_PUPMODE;

	topscan = 0;
	npregs->cset.topscan = 0x3ff;
	npregs->cset.xywin = (4096 << 16) | 4096;

	/* Clear the screen. */
	newport_clear_screen(0, 0, 1280 + 63, 1024, 0);
}

/*
 * calculate the actual screen size by reading
 * the video timing out of the VC2
 */
static void newport_get_screensize(void)
{
	int i, cols;
	unsigned short ventry, treg;
	unsigned short linetable[128];	/* should be enough */

	ventry = newport_vc2_get(npregs, VC2_IREG_VENTRY);
	newport_vc2_set(npregs, VC2_IREG_RADDR, ventry);
	npregs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
			       NPORT_DMODE_W2 | VC2_PROTOCOL);
	for (i = 0; i < 128; i++) {
		newport_bfwait(npregs);
		linetable[i] = npregs->set.dcbdata0.byshort.s1;
	}

	newport_xsize = newport_ysize = 0;
	for (i = 0; i < ARRAY_SIZE(linetable) - 1 && linetable[i + 1]; i += 2) {
		cols = 0;
		newport_vc2_set(npregs, VC2_IREG_RADDR, linetable[i]);
		npregs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
				       NPORT_DMODE_W2 | VC2_PROTOCOL);
		do {
			newport_bfwait(npregs);
			treg = npregs->set.dcbdata0.byshort.s1;
			if ((treg & 1) == 0)
				cols += (treg >> 7) & 0xfe;
			if ((treg & 0x80) == 0) {
				newport_bfwait(npregs);
				treg = npregs->set.dcbdata0.byshort.s1;
			}
		} while ((treg & 0x8000) == 0);
		if (cols) {
			if (cols > newport_xsize)
				newport_xsize = cols;
			newport_ysize += linetable[i + 1];
		}
	}
	printk("NG1: Screensize %dx%d\n", newport_xsize, newport_ysize);
}

static void newport_get_revisions(void)
{
	unsigned int tmp;
	unsigned int board_rev;
	unsigned int rex3_rev;
	unsigned int vc2_rev;
	unsigned int cmap_rev;
	unsigned int xmap9_rev;
	unsigned int bt445_rev;
	unsigned int bitplanes;

	rex3_rev = npregs->cset.status & NPORT_STAT_VERS;

	npregs->set.dcbmode = (DCB_CMAP0 | NCMAP_PROTOCOL |
			       NCMAP_REGADDR_RREG | NPORT_DMODE_W1);
	tmp = npregs->set.dcbdata0.bybytes.b3;
	cmap_rev = tmp & 7;
	board_rev = (tmp >> 4) & 7;
	bitplanes = ((board_rev > 1) && (tmp & 0x80)) ? 8 : 24;

	npregs->set.dcbmode = (DCB_CMAP1 | NCMAP_PROTOCOL |
			       NCMAP_REGADDR_RREG | NPORT_DMODE_W1);
	tmp = npregs->set.dcbdata0.bybytes.b3;
	if ((tmp & 7) < cmap_rev)
		cmap_rev = (tmp & 7);

	vc2_rev = (newport_vc2_get(npregs, VC2_IREG_CONFIG) >> 5) & 7;

	npregs->set.dcbmode = (DCB_XMAP0 | R_DCB_XMAP9_PROTOCOL |
			       XM9_CRS_REVISION | NPORT_DMODE_W1);
	xmap9_rev = npregs->set.dcbdata0.bybytes.b3 & 7;

	npregs->set.dcbmode = (DCB_BT445 | BT445_PROTOCOL |
			       BT445_CSR_ADDR_REG | NPORT_DMODE_W1);
	npregs->set.dcbdata0.bybytes.b3 = BT445_REVISION_REG;
	npregs->set.dcbmode = (DCB_BT445 | BT445_PROTOCOL |
			       BT445_CSR_REVISION | NPORT_DMODE_W1);
	bt445_rev = (npregs->set.dcbdata0.bybytes.b3 >> 4) - 0x0a;

#define L(a)     (char)('A'+(a))
	printk
	    ("NG1: Revision %d, %d bitplanes, REX3 revision %c, VC2 revision %c, xmap9 revision %c, cmap revision %c, bt445 revision %c\n",
	     board_rev, bitplanes, L(rex3_rev), L(vc2_rev), L(xmap9_rev),
	     L(cmap_rev ? (cmap_rev + 1) : 0), L(bt445_rev));
#undef L

	if (board_rev == 3)	/* I don't know all affected revisions */
		xcurs_correction = 21;
}

static void newport_exit(void)
{
	int i;

	/* free memory used by user font */
	for (i = 0; i < MAX_NR_CONSOLES; i++)
		newport_set_def_font(i, NULL);
}

/* Can't be __init, do_take_over_console may call it later */
static const char *newport_startup(void)
{
	int i;

	npregs->cset.config = NPORT_CFG_GD0;

	if (newport_wait(npregs))
		goto out_unmap;

	npregs->set.xstarti = TESTVAL;
	if (npregs->set._xstart.word != XSTI_TO_FXSTART(TESTVAL))
		goto out_unmap;

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		font_data[i] = FONT_DATA;

	newport_reset();
	newport_get_revisions();
	newport_get_screensize();
	newport_has_init = 1;

	return "SGI Newport";

out_unmap:
	return NULL;
}

static void newport_init(struct vc_data *vc, bool init)
{
	int cols, rows;

	cols = newport_xsize / 8;
	rows = newport_ysize / 16;
	vc->vc_can_do_color = 1;
	if (init) {
		vc->vc_cols = cols;
		vc->vc_rows = rows;
	} else
		vc_resize(vc, cols, rows);
}

static void newport_deinit(struct vc_data *c)
{
	if (!con_is_bound(&newport_con) && newport_has_init) {
		newport_exit();
		newport_has_init = 0;
	}
}

static void newport_clear(struct vc_data *vc, unsigned int sy, unsigned int sx,
			  unsigned int width)
{
	int xend = ((sx + width) << 3) - 1;
	int ystart = ((sy << 4) + topscan) & 0x3ff;
	int yend = (((sy + 1) << 4) + topscan - 1) & 0x3ff;

	if (logo_active)
		return;

	if (ystart < yend) {
		newport_clear_screen(sx << 3, ystart, xend, yend,
				     (vc->state.color & 0xf0) >> 4);
	} else {
		newport_clear_screen(sx << 3, ystart, xend, 1023,
				     (vc->state.color & 0xf0) >> 4);
		newport_clear_screen(sx << 3, 0, xend, yend,
				     (vc->state.color & 0xf0) >> 4);
	}
}

static void newport_putc(struct vc_data *vc, u16 charattr, unsigned int ypos,
			 unsigned int xpos)
{
	unsigned char *p;

	p = &font_data[vc->vc_num][(charattr & 0xff) << 4];
	charattr = (charattr >> 8) & 0xff;
	xpos <<= 3;
	ypos <<= 4;

	newport_render_background(xpos, ypos, xpos, ypos,
				  (charattr & 0xf0) >> 4);

	/* Set the color and drawing mode. */
	newport_wait(npregs);
	npregs->set.colori = charattr & 0xf;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
				 NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
				 NPORT_DMODE0_L32);

	/* Set coordinates for bitmap operation. */
	npregs->set.xystarti = (xpos << 16) | ((ypos + topscan) & 0x3ff);
	npregs->set.xyendi = ((xpos + 7) << 16);
	newport_wait(npregs);

	/* Go, baby, go... */
	RENDER(npregs, p);
}

static void newport_putcs(struct vc_data *vc, const u16 *s,
			  unsigned int count, unsigned int ypos,
			  unsigned int xpos)
{
	unsigned char *p;
	unsigned int i;
	u16 charattr;

	charattr = (scr_readw(s) >> 8) & 0xff;

	xpos <<= 3;
	ypos <<= 4;

	if (!logo_active)
		/* Clear the area behing the string */
		newport_render_background(xpos, ypos,
					  xpos + ((count - 1) << 3), ypos,
					  (charattr & 0xf0) >> 4);

	newport_wait(npregs);

	/* Set the color and drawing mode. */
	npregs->set.colori = charattr & 0xf;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
				 NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
				 NPORT_DMODE0_L32);

	for (i = 0; i < count; i++, xpos += 8) {
		p = &font_data[vc->vc_num][(scr_readw(s++) & 0xff) << 4];

		newport_wait(npregs);

		/* Set coordinates for bitmap operation. */
		npregs->set.xystarti =
		    (xpos << 16) | ((ypos + topscan) & 0x3ff);
		npregs->set.xyendi = ((xpos + 7) << 16);

		/* Go, baby, go... */
		RENDER(npregs, p);
	}
}

static void newport_cursor(struct vc_data *vc, bool enable)
{
	unsigned short treg;
	int xcurs, ycurs;

	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);

	if (!enable) {
		newport_vc2_set(npregs, VC2_IREG_CONTROL,
				(treg & ~(VC2_CTRL_ECDISP)));
		return;
	}

	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_ECDISP));
	xcurs = (vc->vc_pos - vc->vc_visible_origin) / 2;
	ycurs = ((xcurs / vc->vc_cols) << 4) + 31;
	xcurs = ((xcurs % vc->vc_cols) << 3) + xcurs_correction;
	newport_vc2_set(npregs, VC2_IREG_CURSX, xcurs);
	newport_vc2_set(npregs, VC2_IREG_CURSY, ycurs);
}

static bool newport_switch(struct vc_data *vc)
{
	static int logo_drawn = 0;

	topscan = 0;
	npregs->cset.topscan = 0x3ff;

	if (!logo_drawn) {
		if (newport_show_logo()) {
			logo_drawn = 1;
			logo_active = 1;
		}
	}

	return true;
}

static bool newport_blank(struct vc_data *c, enum vesa_blank_mode blank,
			  bool mode_switch)
{
	unsigned short treg;

	if (blank == VESA_NO_BLANKING) {
		/* unblank console */
		treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
		newport_vc2_set(npregs, VC2_IREG_CONTROL,
				(treg | VC2_CTRL_EDISP));
	} else {
		/* blank console */
		treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
		newport_vc2_set(npregs, VC2_IREG_CONTROL,
				(treg & ~(VC2_CTRL_EDISP)));
	}

	return true;
}

static int newport_set_font(int unit, const struct console_font *op,
			    unsigned int vpitch)
{
	int w = op->width;
	int h = op->height;
	int size = h * op->charcount;
	int i;
	unsigned char *new_data, *data = op->data, *p;

	/* ladis: when I grow up, there will be a day... and more sizes will
	 * be supported ;-) */
	if ((w != 8) || (h != 16) || (vpitch != 32)
	    || (op->charcount != 256 && op->charcount != 512))
		return -EINVAL;

	if (!(new_data = kmalloc(FONT_EXTRA_WORDS * sizeof(int) + size,
	     GFP_USER))) return -ENOMEM;

	new_data += FONT_EXTRA_WORDS * sizeof(int);
	FNTSIZE(new_data) = size;
	FNTCHARCNT(new_data) = op->charcount;
	REFCOUNT(new_data) = 0;	/* usage counter */
	FNTSUM(new_data) = 0;

	p = new_data;
	for (i = 0; i < op->charcount; i++) {
		memcpy(p, data, h);
		data += 32;
		p += h;
	}

	/* check if font is already used by other console */
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (font_data[i] != FONT_DATA
		    && FNTSIZE(font_data[i]) == size
		    && !memcmp(font_data[i], new_data, size)) {
			kfree(new_data - FONT_EXTRA_WORDS * sizeof(int));
			/* current font is the same as the new one */
			if (i == unit)
				return 0;
			new_data = font_data[i];
			break;
		}
	}
	/* old font is user font */
	if (font_data[unit] != FONT_DATA) {
		if (--REFCOUNT(font_data[unit]) == 0)
			kfree(font_data[unit] -
			      FONT_EXTRA_WORDS * sizeof(int));
	}
	REFCOUNT(new_data)++;
	font_data[unit] = new_data;

	return 0;
}

static int newport_set_def_font(int unit, struct console_font *op)
{
	if (font_data[unit] != FONT_DATA) {
		if (--REFCOUNT(font_data[unit]) == 0)
			kfree(font_data[unit] -
			      FONT_EXTRA_WORDS * sizeof(int));
		font_data[unit] = FONT_DATA;
	}

	return 0;
}

static int newport_font_default(struct vc_data *vc, struct console_font *op,
				const char *name)
{
	return newport_set_def_font(vc->vc_num, op);
}

static int newport_font_set(struct vc_data *vc, const struct console_font *font,
			    unsigned int vpitch, unsigned int flags)
{
	return newport_set_font(vc->vc_num, font, vpitch);
}

static bool newport_scroll(struct vc_data *vc, unsigned int t, unsigned int b,
		enum con_scroll dir, unsigned int lines)
{
	int count, x, y;
	unsigned short *s, *d;
	unsigned short chattr;

	logo_active = 0;	/* it's time to disable the logo now.. */

	if (t == 0 && b == vc->vc_rows) {
		if (dir == SM_UP) {
			topscan = (topscan + (lines << 4)) & 0x3ff;
			newport_clear_lines(vc->vc_rows - lines,
					    vc->vc_rows - 1,
					    (vc->state.color & 0xf0) >> 4);
		} else {
			topscan = (topscan + (-lines << 4)) & 0x3ff;
			newport_clear_lines(0, lines - 1,
					    (vc->state.color & 0xf0) >> 4);
		}
		npregs->cset.topscan = (topscan - 1) & 0x3ff;
		return false;
	}

	count = (b - t - lines) * vc->vc_cols;
	if (dir == SM_UP) {
		x = 0;
		y = t;
		s = (unsigned short *) (vc->vc_origin +
					vc->vc_size_row * (t + lines));
		d = (unsigned short *) (vc->vc_origin +
					vc->vc_size_row * t);
		while (count--) {
			chattr = scr_readw(s++);
			if (chattr != scr_readw(d)) {
				newport_putc(vc, chattr, y, x);
				scr_writew(chattr, d);
			}
			d++;
			if (++x == vc->vc_cols) {
				x = 0;
				y++;
			}
		}
		d = (unsigned short *) (vc->vc_origin +
					vc->vc_size_row * (b - lines));
		x = 0;
		y = b - lines;
		for (count = 0; count < (lines * vc->vc_cols); count++) {
			if (scr_readw(d) != vc->vc_video_erase_char) {
				newport_putc(vc, vc->vc_video_erase_char,
					     y, x);
				scr_writew(vc->vc_video_erase_char, d);
			}
			d++;
			if (++x == vc->vc_cols) {
				x = 0;
				y++;
			}
		}
	} else {
		x = vc->vc_cols - 1;
		y = b - 1;
		s = (unsigned short *) (vc->vc_origin +
					vc->vc_size_row * (b - lines) - 2);
		d = (unsigned short *) (vc->vc_origin +
					vc->vc_size_row * b - 2);
		while (count--) {
			chattr = scr_readw(s--);
			if (chattr != scr_readw(d)) {
				newport_putc(vc, chattr, y, x);
				scr_writew(chattr, d);
			}
			d--;
			if (x-- == 0) {
				x = vc->vc_cols - 1;
				y--;
			}
		}
		d = (unsigned short *) (vc->vc_origin +
					vc->vc_size_row * t);
		x = 0;
		y = t;
		for (count = 0; count < (lines * vc->vc_cols); count++) {
			if (scr_readw(d) != vc->vc_video_erase_char) {
				newport_putc(vc, vc->vc_video_erase_char,
					     y, x);
				scr_writew(vc->vc_video_erase_char, d);
			}
			d++;
			if (++x == vc->vc_cols) {
				x = 0;
				y++;
			}
		}
	}
	return true;
}

static void newport_save_screen(struct vc_data *vc) { }

const struct consw newport_con = {
	.owner		  = THIS_MODULE,
	.con_startup	  = newport_startup,
	.con_init	  = newport_init,
	.con_deinit	  = newport_deinit,
	.con_clear	  = newport_clear,
	.con_putc	  = newport_putc,
	.con_putcs	  = newport_putcs,
	.con_cursor	  = newport_cursor,
	.con_scroll	  = newport_scroll,
	.con_switch	  = newport_switch,
	.con_blank	  = newport_blank,
	.con_font_set	  = newport_font_set,
	.con_font_default = newport_font_default,
	.con_save_screen  = newport_save_screen
};

static int newport_probe(struct gio_device *dev,
			 const struct gio_device_id *id)
{
	int err;

	if (!dev->resource.start)
		return -EINVAL;

	if (npregs)
		return -EBUSY; /* we only support one Newport as console */

	newport_addr = dev->resource.start + 0xF0000;
	if (!request_mem_region(newport_addr, NEWPORT_LEN, "Newport"))
		return -ENODEV;

	npregs = (struct newport_regs *)/* ioremap cannot fail */
		ioremap(newport_addr, sizeof(struct newport_regs));
	console_lock();
	err = do_take_over_console(&newport_con, 0, MAX_NR_CONSOLES - 1, 1);
	console_unlock();

	if (err) {
		iounmap((void *)npregs);
		release_mem_region(newport_addr, NEWPORT_LEN);
	}
	return err;
}

static void newport_remove(struct gio_device *dev)
{
	give_up_console(&newport_con);
	iounmap((void *)npregs);
	release_mem_region(newport_addr, NEWPORT_LEN);
}

static struct gio_device_id newport_ids[] = {
	{ .id = 0x7e },
	{ .id = 0xff }
};

MODULE_ALIAS("gio:7e");

static struct gio_driver newport_driver = {
	.name = "newport",
	.id_table = newport_ids,
	.probe = newport_probe,
	.remove = newport_remove,
};
module_driver(newport_driver, gio_register_driver, gio_unregister_driver);

MODULE_DESCRIPTION("SGI Newport console driver");
MODULE_LICENSE("GPL");
