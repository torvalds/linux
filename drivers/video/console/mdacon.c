/*
 *  linux/drivers/video/mdacon.c -- Low level MDA based console driver
 *
 *	(c) 1998 Andrew Apted <ajapted@netspace.net.au>
 *
 *      including portions (c) 1995-1998 Patrick Caulfield.
 *
 *      slight improvements (c) 2000 Edward Betts <edward@debian.org>
 *
 *  This file is based on the VGA console driver (vgacon.c):
 *	
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *
 *	Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *
 *  and on the old console.c, vga.c and vesa_blank.c drivers:
 *
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *  Changelog:
 *  Paul G. (03/2001) Fix mdacon= boot prompt to use __setup().
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/vt_kern.h>
#include <linux/vt_buffer.h>
#include <linux/selection.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/vga.h>

static DEFINE_SPINLOCK(mda_lock);

/* description of the hardware layout */

static u16		*mda_vram_base;		/* Base of video memory */
static unsigned long	mda_vram_len;		/* Size of video memory */
static unsigned int	mda_num_columns;	/* Number of text columns */
static unsigned int	mda_num_lines;		/* Number of text lines */

static unsigned int	mda_index_port;		/* Register select port */
static unsigned int	mda_value_port;		/* Register value port */
static unsigned int	mda_mode_port;		/* Mode control port */
static unsigned int	mda_status_port;	/* Status and Config port */
static unsigned int	mda_gfx_port;		/* Graphics control port */

/* current hardware state */

static int	mda_cursor_loc=-1;
static int	mda_cursor_size_from=-1;
static int	mda_cursor_size_to=-1;

static enum { TYPE_MDA, TYPE_HERC, TYPE_HERCPLUS, TYPE_HERCCOLOR } mda_type;
static char *mda_type_name;

/* console information */

static int	mda_first_vc = 13;
static int	mda_last_vc  = 16;

static struct vc_data	*mda_display_fg = NULL;

module_param(mda_first_vc, int, 0);
MODULE_PARM_DESC(mda_first_vc, "First virtual console. Default: 13");
module_param(mda_last_vc, int, 0);
MODULE_PARM_DESC(mda_last_vc, "Last virtual console. Default: 16");

/* MDA register values
 */

#define MDA_CURSOR_BLINKING	0x00
#define MDA_CURSOR_OFF		0x20
#define MDA_CURSOR_SLOWBLINK	0x60

#define MDA_MODE_GRAPHICS	0x02
#define MDA_MODE_VIDEO_EN	0x08
#define MDA_MODE_BLINK_EN	0x20
#define MDA_MODE_GFX_PAGE1	0x80

#define MDA_STATUS_HSYNC	0x01
#define MDA_STATUS_VSYNC	0x80
#define MDA_STATUS_VIDEO	0x08

#define MDA_CONFIG_COL132	0x08
#define MDA_GFX_MODE_EN		0x01
#define MDA_GFX_PAGE_EN		0x02


/*
 * MDA could easily be classified as "pre-dinosaur hardware".
 */

static void write_mda_b(unsigned int val, unsigned char reg)
{
	unsigned long flags;

	spin_lock_irqsave(&mda_lock, flags);	

	outb_p(reg, mda_index_port); 
	outb_p(val, mda_value_port);

	spin_unlock_irqrestore(&mda_lock, flags);
}

static void write_mda_w(unsigned int val, unsigned char reg)
{
	unsigned long flags;

	spin_lock_irqsave(&mda_lock, flags);

	outb_p(reg,   mda_index_port); outb_p(val >> 8,   mda_value_port);
	outb_p(reg+1, mda_index_port); outb_p(val & 0xff, mda_value_port);

	spin_unlock_irqrestore(&mda_lock, flags);
}

#ifdef TEST_MDA_B
static int test_mda_b(unsigned char val, unsigned char reg)
{
	unsigned long flags;

	spin_lock_irqsave(&mda_lock, flags);

	outb_p(reg, mda_index_port); 
	outb  (val, mda_value_port);

	udelay(20); val = (inb_p(mda_value_port) == val);

	spin_unlock_irqrestore(&mda_lock, flags);
	return val;
}
#endif

static inline void mda_set_cursor(unsigned int location) 
{
	if (mda_cursor_loc == location)
		return;

	write_mda_w(location >> 1, 0x0e);

	mda_cursor_loc = location;
}

static inline void mda_set_cursor_size(int from, int to)
{
	if (mda_cursor_size_from==from && mda_cursor_size_to==to)
		return;
	
	if (from > to) {
		write_mda_b(MDA_CURSOR_OFF, 0x0a);	/* disable cursor */
	} else {
		write_mda_b(from, 0x0a);	/* cursor start */
		write_mda_b(to,   0x0b);	/* cursor end */
	}

	mda_cursor_size_from = from;
	mda_cursor_size_to   = to;
}


#ifndef MODULE
static int __init mdacon_setup(char *str)
{
	/* command line format: mdacon=<first>,<last> */

	int ints[3];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	if (ints[0] < 2)
		return 0;

	if (ints[1] < 1 || ints[1] > MAX_NR_CONSOLES || 
	    ints[2] < 1 || ints[2] > MAX_NR_CONSOLES)
		return 0;

	mda_first_vc = ints[1];
	mda_last_vc  = ints[2];
	return 1;
}

__setup("mdacon=", mdacon_setup);
#endif

static int mda_detect(void)
{
	int count=0;
	u16 *p, p_save;
	u16 *q, q_save;

	/* do a memory check */

	p = mda_vram_base;
	q = mda_vram_base + 0x01000 / 2;

	p_save = scr_readw(p);
	q_save = scr_readw(q);

	scr_writew(0xAA55, p);
	if (scr_readw(p) == 0xAA55)
		count++;

	scr_writew(0x55AA, p);
	if (scr_readw(p) == 0x55AA)
		count++;

	scr_writew(p_save, p);

	if (count != 2) {
		return 0;
	}

	/* check if we have 4K or 8K */

	scr_writew(0xA55A, q);
	scr_writew(0x0000, p);
	if (scr_readw(q) == 0xA55A)
		count++;
	
	scr_writew(0x5AA5, q);
	scr_writew(0x0000, p);
	if (scr_readw(q) == 0x5AA5)
		count++;

	scr_writew(p_save, p);
	scr_writew(q_save, q);
	
	if (count == 4) {
		mda_vram_len = 0x02000;
	}
	
	/* Ok, there is definitely a card registering at the correct
	 * memory location, so now we do an I/O port test.
	 */

#ifdef TEST_MDA_B
	/* Edward: These two mess `tests' mess up my cursor on bootup */

	/* cursor low register */
	if (!test_mda_b(0x66, 0x0f))
		return 0;

	/* cursor low register */
	if (!test_mda_b(0x99, 0x0f))
		return 0;
#endif

	/* See if the card is a Hercules, by checking whether the vsync
	 * bit of the status register is changing.  This test lasts for
	 * approximately 1/10th of a second.
	 */
	
	p_save = q_save = inb_p(mda_status_port) & MDA_STATUS_VSYNC;

	for (count = 0; count < 50000 && p_save == q_save; count++) {
		q_save = inb(mda_status_port) & MDA_STATUS_VSYNC;
		udelay(2);
	}

	if (p_save != q_save) {
		switch (inb_p(mda_status_port) & 0x70) {
		case 0x10:
			mda_type = TYPE_HERCPLUS;
			mda_type_name = "HerculesPlus";
			break;
		case 0x50:
			mda_type = TYPE_HERCCOLOR;
			mda_type_name = "HerculesColor";
			break;
		default:
			mda_type = TYPE_HERC;
			mda_type_name = "Hercules";
			break;
		}
	}

	return 1;
}

static void mda_initialize(void)
{
	write_mda_b(97, 0x00);		/* horizontal total */
	write_mda_b(80, 0x01);		/* horizontal displayed */
	write_mda_b(82, 0x02);		/* horizontal sync pos */
	write_mda_b(15, 0x03);		/* horizontal sync width */

	write_mda_b(25, 0x04);		/* vertical total */
	write_mda_b(6,  0x05);		/* vertical total adjust */
	write_mda_b(25, 0x06);		/* vertical displayed */
	write_mda_b(25, 0x07);		/* vertical sync pos */

	write_mda_b(2,  0x08);		/* interlace mode */
	write_mda_b(13, 0x09);		/* maximum scanline */
	write_mda_b(12, 0x0a);		/* cursor start */
	write_mda_b(13, 0x0b);		/* cursor end */

	write_mda_w(0x0000, 0x0c);	/* start address */
	write_mda_w(0x0000, 0x0e);	/* cursor location */

	outb_p(MDA_MODE_VIDEO_EN | MDA_MODE_BLINK_EN, mda_mode_port);
	outb_p(0x00, mda_status_port);
	outb_p(0x00, mda_gfx_port);
}

static const char *mdacon_startup(void)
{
	mda_num_columns = 80;
	mda_num_lines   = 25;

	mda_vram_len  = 0x01000;
	mda_vram_base = (u16 *)VGA_MAP_MEM(0xb0000, mda_vram_len);

	mda_index_port  = 0x3b4;
	mda_value_port  = 0x3b5;
	mda_mode_port   = 0x3b8;
	mda_status_port = 0x3ba;
	mda_gfx_port    = 0x3bf;

	mda_type = TYPE_MDA;
	mda_type_name = "MDA";

	if (! mda_detect()) {
		printk("mdacon: MDA card not detected.\n");
		return NULL;
	}

	if (mda_type != TYPE_MDA) {
		mda_initialize();
	}

	/* cursor looks ugly during boot-up, so turn it off */
	mda_set_cursor(mda_vram_len - 1);

	printk("mdacon: %s with %ldK of memory detected.\n",
		mda_type_name, mda_vram_len/1024);

	return "MDA-2";
}

static void mdacon_init(struct vc_data *c, bool init)
{
	c->vc_complement_mask = 0x0800;	 /* reverse video */
	c->vc_display_fg = &mda_display_fg;

	if (init) {
		c->vc_cols = mda_num_columns;
		c->vc_rows = mda_num_lines;
	} else
		vc_resize(c, mda_num_columns, mda_num_lines);

	/* make the first MDA console visible */

	if (mda_display_fg == NULL)
		mda_display_fg = c;
}

static void mdacon_deinit(struct vc_data *c)
{
	/* con_set_default_unimap(c->vc_num); */

	if (mda_display_fg == c)
		mda_display_fg = NULL;
}

static inline u16 mda_convert_attr(u16 ch)
{
	u16 attr = 0x0700;

	/* Underline and reverse-video are mutually exclusive on MDA.
	 * Since reverse-video is used for cursors and selected areas,
	 * it takes precedence. 
	 */

	if (ch & 0x0800)	attr = 0x7000;	/* reverse */
	else if (ch & 0x0400)	attr = 0x0100;	/* underline */

	return ((ch & 0x0200) << 2) | 		/* intensity */ 
		(ch & 0x8000) |			/* blink */ 
		(ch & 0x00ff) | attr;
}

static u8 mdacon_build_attr(struct vc_data *c, u8 color,
			    enum vc_intensity intensity,
			    bool blink, bool underline, bool reverse,
			    bool italic)
{
	/* The attribute is just a bit vector:
	 *
	 *	Bit 0..1 : intensity (0..2)
	 *	Bit 2    : underline
	 *	Bit 3    : reverse
	 *	Bit 7    : blink
	 */

	return (intensity & VCI_MASK) |
		(underline << 2) |
		(reverse << 3) |
		(italic << 4) |
		(blink << 7);
}

static void mdacon_invert_region(struct vc_data *c, u16 *p, int count)
{
	for (; count > 0; count--) {
		scr_writew(scr_readw(p) ^ 0x0800, p);
		p++;
	}
}

static inline u16 *mda_addr(unsigned int x, unsigned int y)
{
	return mda_vram_base + y * mda_num_columns + x;
}

static void mdacon_putcs(struct vc_data *c, const u16 *s, unsigned int count,
			 unsigned int y, unsigned int x)
{
	u16 *dest = mda_addr(x, y);

	for (; count > 0; count--) {
		scr_writew(mda_convert_attr(scr_readw(s++)), dest++);
	}
}

static void mdacon_clear(struct vc_data *c, unsigned int y, unsigned int x,
			 unsigned int width)
{
	u16 *dest = mda_addr(x, y);
	u16 eattr = mda_convert_attr(c->vc_video_erase_char);

	scr_memsetw(dest, eattr, width * 2);
}

static bool mdacon_switch(struct vc_data *c)
{
	return true;	/* redrawing needed */
}

static bool mdacon_blank(struct vc_data *c, enum vesa_blank_mode blank,
			 bool mode_switch)
{
	if (mda_type == TYPE_MDA) {
		if (blank) 
			scr_memsetw(mda_vram_base,
				mda_convert_attr(c->vc_video_erase_char),
				c->vc_screenbuf_size);
		/* Tell console.c that it has to restore the screen itself */
		return true;
	} else {
		if (blank)
			outb_p(0x00, mda_mode_port);	/* disable video */
		else
			outb_p(MDA_MODE_VIDEO_EN | MDA_MODE_BLINK_EN, 
				mda_mode_port);
		return false;
	}
}

static void mdacon_cursor(struct vc_data *c, bool enable)
{
	if (!enable) {
		mda_set_cursor(mda_vram_len - 1);
		return;
	}

	mda_set_cursor(c->state.y * mda_num_columns * 2 + c->state.x * 2);

	switch (CUR_SIZE(c->vc_cursor_type)) {

		case CUR_LOWER_THIRD:	mda_set_cursor_size(10, 13); break;
		case CUR_LOWER_HALF:	mda_set_cursor_size(7,  13); break;
		case CUR_TWO_THIRDS:	mda_set_cursor_size(4,  13); break;
		case CUR_BLOCK:		mda_set_cursor_size(1,  13); break;
		case CUR_NONE:		mda_set_cursor_size(14, 13); break;
		default:		mda_set_cursor_size(12, 13); break;
	}
}

static bool mdacon_scroll(struct vc_data *c, unsigned int t, unsigned int b,
		enum con_scroll dir, unsigned int lines)
{
	u16 eattr = mda_convert_attr(c->vc_video_erase_char);

	if (!lines)
		return false;

	if (lines > c->vc_rows)   /* maximum realistic size */
		lines = c->vc_rows;

	switch (dir) {

	case SM_UP:
		scr_memmovew(mda_addr(0, t), mda_addr(0, t + lines),
				(b-t-lines)*mda_num_columns*2);
		scr_memsetw(mda_addr(0, b - lines), eattr,
				lines*mda_num_columns*2);
		break;

	case SM_DOWN:
		scr_memmovew(mda_addr(0, t + lines), mda_addr(0, t),
				(b-t-lines)*mda_num_columns*2);
		scr_memsetw(mda_addr(0, t), eattr, lines*mda_num_columns*2);
		break;
	}

	return false;
}


/*
 *  The console `switch' structure for the MDA based console
 */

static const struct consw mda_con = {
	.owner =		THIS_MODULE,
	.con_startup =		mdacon_startup,
	.con_init =		mdacon_init,
	.con_deinit =		mdacon_deinit,
	.con_clear =		mdacon_clear,
	.con_putcs =		mdacon_putcs,
	.con_cursor =		mdacon_cursor,
	.con_scroll =		mdacon_scroll,
	.con_switch =		mdacon_switch,
	.con_blank =		mdacon_blank,
	.con_build_attr =	mdacon_build_attr,
	.con_invert_region =	mdacon_invert_region,
};

int __init mda_console_init(void)
{
	int err;

	if (mda_first_vc > mda_last_vc)
		return 1;
	console_lock();
	err = do_take_over_console(&mda_con, mda_first_vc-1, mda_last_vc-1, 0);
	console_unlock();
	return err;
}

static void __exit mda_console_exit(void)
{
	give_up_console(&mda_con);
}

module_init(mda_console_init);
module_exit(mda_console_exit);

MODULE_LICENSE("GPL");

