/* $Id: promcon.c,v 1.17 2000/07/26 23:02:52 davem Exp $
 * Console driver utilizing PROM sun terminal emulation
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998  Jakub Jelinek  (jj@ultra.linux.cz)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kd.h>

#include <asm/oplib.h>
#include <asm/uaccess.h>

static short pw = 80 - 1, ph = 34 - 1;
static short px, py;
static unsigned long promcon_uni_pagedir[2];

extern u8 promfont_unicount[];
extern u16 promfont_unitable[];

#define PROMCON_COLOR 0

#if PROMCON_COLOR
#define inverted(s)	((((s) & 0x7700) == 0x0700) ? 0 : 1)
#else
#define inverted(s)	(((s) & 0x0800) ? 1 : 0)
#endif

static __inline__ void
promcon_puts(char *buf, int cnt)
{
	prom_printf("%*.*s", cnt, cnt, buf);
}

static int
promcon_start(struct vc_data *conp, char *b)
{
	unsigned short *s = (unsigned short *)
			(conp->vc_origin + py * conp->vc_size_row + (px << 1));
	u16 cs;

	cs = scr_readw(s);
	if (px == pw) {
		unsigned short *t = s - 1;
		u16 ct = scr_readw(t);

		if (inverted(cs) && inverted(ct))
			return sprintf(b, "\b\033[7m%c\b\033[@%c\033[m", cs,
				       ct);
		else if (inverted(cs))
			return sprintf(b, "\b\033[7m%c\033[m\b\033[@%c", cs,
				       ct);
		else if (inverted(ct))
			return sprintf(b, "\b%c\b\033[@\033[7m%c\033[m", cs,
				       ct);
		else
			return sprintf(b, "\b%c\b\033[@%c", cs, ct);
	}

	if (inverted(cs))
		return sprintf(b, "\033[7m%c\033[m\b", cs);
	else
		return sprintf(b, "%c\b", cs);
}

static int
promcon_end(struct vc_data *conp, char *b)
{
	unsigned short *s = (unsigned short *)
			(conp->vc_origin + py * conp->vc_size_row + (px << 1));
	char *p = b;
	u16 cs;

	b += sprintf(b, "\033[%d;%dH", py + 1, px + 1);

	cs = scr_readw(s);
	if (px == pw) {
		unsigned short *t = s - 1;
		u16 ct = scr_readw(t);

		if (inverted(cs) && inverted(ct))
			b += sprintf(b, "\b%c\b\033[@\033[7m%c\033[m", cs, ct);
		else if (inverted(cs))
			b += sprintf(b, "\b%c\b\033[@%c", cs, ct);
		else if (inverted(ct))
			b += sprintf(b, "\b\033[7m%c\b\033[@%c\033[m", cs, ct);
		else
			b += sprintf(b, "\b\033[7m%c\033[m\b\033[@%c", cs, ct);
		return b - p;
	}

	if (inverted(cs))
		b += sprintf(b, "%c\b", cs);
	else
		b += sprintf(b, "\033[7m%c\033[m\b", cs);
	return b - p;
}

const char *promcon_startup(void)
{
	const char *display_desc = "PROM";
	int node;
	char buf[40];
	
	node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "options");
	if (prom_getproperty(node,  "screen-#columns", buf, 40) != -1) {
		pw = simple_strtoul(buf, NULL, 0);
		if (pw < 10 || pw > 256)
			pw = 80;
		pw--;
	}
	if (prom_getproperty(node,  "screen-#rows", buf, 40) != -1) {
		ph = simple_strtoul(buf, NULL, 0);
		if (ph < 10 || ph > 256)
			ph = 34;
		ph--;
	}
	promcon_puts("\033[H\033[J", 6);
	return display_desc;
}

static void
promcon_init_unimap(struct vc_data *conp)
{
	mm_segment_t old_fs = get_fs();
	struct unipair *p, *p1;
	u16 *q;
	int i, j, k;
	
	p = kmalloc(256*sizeof(struct unipair), GFP_KERNEL);
	if (!p) return;
	
	q = promfont_unitable;
	p1 = p;
	k = 0;
	for (i = 0; i < 256; i++)
		for (j = promfont_unicount[i]; j; j--) {
			p1->unicode = *q++;
			p1->fontpos = i;
			p1++;
			k++;
		}
	set_fs(KERNEL_DS);
	con_clear_unimap(conp, NULL);
	con_set_unimap(conp, k, p);
	con_protect_unimap(conp, 1);
	set_fs(old_fs);
	kfree(p);
}

static void
promcon_init(struct vc_data *conp, int init)
{
	unsigned long p;
	
	conp->vc_can_do_color = PROMCON_COLOR;
	if (init) {
		conp->vc_cols = pw + 1;
		conp->vc_rows = ph + 1;
	}
	p = *conp->vc_uni_pagedir_loc;
	if (conp->vc_uni_pagedir_loc == &conp->vc_uni_pagedir ||
	    !--conp->vc_uni_pagedir_loc[1])
		con_free_unimap(conp);
	conp->vc_uni_pagedir_loc = promcon_uni_pagedir;
	promcon_uni_pagedir[1]++;
	if (!promcon_uni_pagedir[0] && p) {
		promcon_init_unimap(conp);
	}
	if (!init) {
		if (conp->vc_cols != pw + 1 || conp->vc_rows != ph + 1)
			vc_resize(conp, pw + 1, ph + 1);
	}
}

static void
promcon_deinit(struct vc_data *conp)
{
	/* When closing the last console, reset video origin */
	if (!--promcon_uni_pagedir[1])
		con_free_unimap(conp);
	conp->vc_uni_pagedir_loc = &conp->vc_uni_pagedir;
	con_set_default_unimap(conp);
}

static int
promcon_switch(struct vc_data *conp)
{
	return 1;
}

static unsigned short *
promcon_repaint_line(unsigned short *s, unsigned char *buf, unsigned char **bp)
{
	int cnt = pw + 1;
	int attr = -1;
	unsigned char *b = *bp;

	while (cnt--) {
		u16 c = scr_readw(s);
		if (attr != inverted(c)) {
			attr = inverted(c);
			if (attr) {
				strcpy (b, "\033[7m");
				b += 4;
			} else {
				strcpy (b, "\033[m");
				b += 3;
			}
		}
		*b++ = c;
		s++;
		if (b - buf >= 224) {
			promcon_puts(buf, b - buf);
			b = buf;
		}
	}
	*bp = b;
	return s;
}

static void
promcon_putcs(struct vc_data *conp, const unsigned short *s,
	      int count, int y, int x)
{
	unsigned char buf[256], *b = buf;
	unsigned short attr = scr_readw(s);
	unsigned char save;
	int i, last = 0;

	if (console_blanked)
		return;
	
	if (count <= 0)
		return;

	b += promcon_start(conp, b);

	if (x + count >= pw + 1) {
		if (count == 1) {
			x -= 1;
			save = scr_readw((unsigned short *)(conp->vc_origin
						   + y * conp->vc_size_row
						   + (x << 1)));

			if (px != x || py != y) {
				b += sprintf(b, "\033[%d;%dH", y + 1, x + 1);
				px = x;
				py = y;
			}

			if (inverted(attr))
				b += sprintf(b, "\033[7m%c\033[m", scr_readw(s++));
			else
				b += sprintf(b, "%c", scr_readw(s++));

			strcpy(b, "\b\033[@");
			b += 4;

			if (inverted(save))
				b += sprintf(b, "\033[7m%c\033[m", save);
			else
				b += sprintf(b, "%c", save);

			px++;

			b += promcon_end(conp, b);
			promcon_puts(buf, b - buf);
			return;
		} else {
			last = 1;
			count = pw - x - 1;
		}
	}

	if (inverted(attr)) {
		strcpy(b, "\033[7m");
		b += 4;
	}

	if (px != x || py != y) {
		b += sprintf(b, "\033[%d;%dH", y + 1, x + 1);
		px = x;
		py = y;
	}

	for (i = 0; i < count; i++) {
		if (b - buf >= 224) {
			promcon_puts(buf, b - buf);
			b = buf;
		}
		*b++ = scr_readw(s++);
	}

	px += count;

	if (last) {
		save = scr_readw(s++);
		b += sprintf(b, "%c\b\033[@%c", scr_readw(s++), save);
		px++;
	}

	if (inverted(attr)) {
		strcpy(b, "\033[m");
		b += 3;
	}

	b += promcon_end(conp, b);
	promcon_puts(buf, b - buf);
}

static void
promcon_putc(struct vc_data *conp, int c, int y, int x)
{
	unsigned short s;

	if (console_blanked)
		return;
	
	scr_writew(c, &s);
	promcon_putcs(conp, &s, 1, y, x);
}

static void
promcon_clear(struct vc_data *conp, int sy, int sx, int height, int width)
{
	unsigned char buf[256], *b = buf;
	int i, j;

	if (console_blanked)
		return;
	
	b += promcon_start(conp, b);

	if (!sx && width == pw + 1) {

		if (!sy && height == ph + 1) {
			strcpy(b, "\033[H\033[J");
			b += 6;
			b += promcon_end(conp, b);
			promcon_puts(buf, b - buf);
			return;
		} else if (sy + height == ph + 1) {
			b += sprintf(b, "\033[%dH\033[J", sy + 1);
			b += promcon_end(conp, b);
			promcon_puts(buf, b - buf);
			return;
		}

		b += sprintf(b, "\033[%dH", sy + 1);
		for (i = 1; i < height; i++) {
			strcpy(b, "\033[K\n");
			b += 4;
		}

		strcpy(b, "\033[K");
		b += 3;

		b += promcon_end(conp, b);
		promcon_puts(buf, b - buf);
		return;

	} else if (sx + width == pw + 1) {

		b += sprintf(b, "\033[%d;%dH", sy + 1, sx + 1);
		for (i = 1; i < height; i++) {
			strcpy(b, "\033[K\n");
			b += 4;
		}

		strcpy(b, "\033[K");
		b += 3;

		b += promcon_end(conp, b);
		promcon_puts(buf, b - buf);
		return;
	}

	for (i = sy + 1; i <= sy + height; i++) {
		b += sprintf(b, "\033[%d;%dH", i, sx + 1);
		for (j = 0; j < width; j++)
			*b++ = ' ';
		if (b - buf + width >= 224) {
			promcon_puts(buf, b - buf);
			b = buf;
		}
	}

	b += promcon_end(conp, b);
	promcon_puts(buf, b - buf);
}
                        
static void
promcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
	      int height, int width)
{
	char buf[256], *b = buf;

	if (console_blanked)
		return;
	
	b += promcon_start(conp, b);
	if (sy == dy && height == 1) {
		if (dx > sx && dx + width == conp->vc_cols)
			b += sprintf(b, "\033[%d;%dH\033[%d@\033[%d;%dH",
				     sy + 1, sx + 1, dx - sx, py + 1, px + 1);
		else if (dx < sx && sx + width == conp->vc_cols)
			b += sprintf(b, "\033[%d;%dH\033[%dP\033[%d;%dH",
				     dy + 1, dx + 1, sx - dx, py + 1, px + 1);

		b += promcon_end(conp, b);
		promcon_puts(buf, b - buf);
		return;
	}

	/*
	 * FIXME: What to do here???
	 * Current console.c should not call it like that ever.
	 */
	prom_printf("\033[7mFIXME: bmove not handled\033[m\n");
}

static void
promcon_cursor(struct vc_data *conp, int mode)
{
	char buf[32], *b = buf;

	switch (mode) {
	case CM_ERASE:
		break;

	case CM_MOVE:
	case CM_DRAW:
		b += promcon_start(conp, b);
		if (px != conp->vc_x || py != conp->vc_y) {
			px = conp->vc_x;
			py = conp->vc_y;
			b += sprintf(b, "\033[%d;%dH", py + 1, px + 1);
		}
		promcon_puts(buf, b - buf);
		break;
	}
}

static int
promcon_blank(struct vc_data *conp, int blank, int mode_switch)
{
	if (blank) {
		promcon_puts("\033[H\033[J\033[7m \033[m\b", 15);
		return 0;
	} else {
		/* Let console.c redraw */
		return 1;
	}
}

static int
promcon_scroll(struct vc_data *conp, int t, int b, int dir, int count)
{
	unsigned char buf[256], *p = buf;
	unsigned short *s;
	int i;

	if (console_blanked)
		return 0;
	
	p += promcon_start(conp, p);

	switch (dir) {
	case SM_UP:
		if (b == ph + 1) {
			p += sprintf(p, "\033[%dH\033[%dM", t + 1, count);
			px = 0;
			py = t;
			p += promcon_end(conp, p);
			promcon_puts(buf, p - buf);
			break;
		}

		s = (unsigned short *)(conp->vc_origin
				       + (t + count) * conp->vc_size_row);

		p += sprintf(p, "\033[%dH", t + 1);

		for (i = t; i < b - count; i++)
			s = promcon_repaint_line(s, buf, &p);

		for (; i < b - 1; i++) {
			strcpy(p, "\033[K\n");
			p += 4;
			if (p - buf >= 224) {
				promcon_puts(buf, p - buf);
				p = buf;
			}
		}

		strcpy(p, "\033[K");
		p += 3;

		p += promcon_end(conp, p);
		promcon_puts(buf, p - buf);
		break;

	case SM_DOWN:
		if (b == ph + 1) {
			p += sprintf(p, "\033[%dH\033[%dL", t + 1, count);
			px = 0;
			py = t;
			p += promcon_end(conp, p);
			promcon_puts(buf, p - buf);
			break;
		}

		s = (unsigned short *)(conp->vc_origin + t * conp->vc_size_row);

		p += sprintf(p, "\033[%dH", t + 1);

		for (i = t; i < t + count; i++) {
			strcpy(p, "\033[K\n");
			p += 4;
			if (p - buf >= 224) {
				promcon_puts(buf, p - buf);
				p = buf;
			}
		}

		for (; i < b; i++)
			s = promcon_repaint_line(s, buf, &p);

		p += promcon_end(conp, p);
		promcon_puts(buf, p - buf);
		break;
	}

	return 0;
}

#if !(PROMCON_COLOR)
static u8 promcon_build_attr(struct vc_data *conp, u8 _color, u8 _intensity,
    u8 _blink, u8 _underline, u8 _reverse, u8 _italic)
{
	return (_reverse) ? 0xf : 0x7;
}
#endif

/*
 *  The console 'switch' structure for the VGA based console
 */

static int promcon_dummy(void)
{
        return 0;
}

#define DUMMY (void *) promcon_dummy

const struct consw prom_con = {
	.owner =		THIS_MODULE,
	.con_startup =		promcon_startup,
	.con_init =		promcon_init,
	.con_deinit =		promcon_deinit,
	.con_clear =		promcon_clear,
	.con_putc =		promcon_putc,
	.con_putcs =		promcon_putcs,
	.con_cursor =		promcon_cursor,
	.con_scroll =		promcon_scroll,
	.con_bmove =		promcon_bmove,
	.con_switch =		promcon_switch,
	.con_blank =		promcon_blank,
	.con_set_palette =	DUMMY,
	.con_scrolldelta =	DUMMY,
#if !(PROMCON_COLOR)
	.con_build_attr =	promcon_build_attr,
#endif
};

void __init prom_con_init(void)
{
#ifdef CONFIG_DUMMY_CONSOLE
	if (conswitchp == &dummy_con)
		take_over_console(&prom_con, 0, MAX_NR_CONSOLES-1, 1);
	else
#endif
	if (conswitchp == &prom_con)
		promcon_init_unimap(vc_cons[fg_console].d);
}
