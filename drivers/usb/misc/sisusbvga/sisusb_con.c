/*
 * sisusb - usb kernel driver for SiS315(E) based USB2VGA dongles
 *
 * VGA text mode console part
 *
 * Copyright (C) 2005 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, this code is licensed under the
 * terms of the GPL v2.
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) The name of the author may not be used to endorse or promote products
 * *    derived from this software without specific psisusbr written permission.
 * *
 * * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESSED OR
 * * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Thomas Winischhofer <thomas@winischhofer.net>
 *
 * Portions based on vgacon.c which are
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *      Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *      based on code Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *
 * A note on using in_atomic() in here: We can't handle console
 * calls from non-schedulable context due to our USB-dependend
 * nature. For now, this driver just ignores any calls if it
 * detects this state.
 *
 */

#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/fs.h>
#include <linux/usb.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>

#include "sisusb.h"
#include "sisusb_init.h"

#ifdef INCL_SISUSB_CON

#define sisusbcon_writew(val, addr)	(*(addr) = (val))
#define sisusbcon_readw(addr)		(*(addr))
#define sisusbcon_memmovew(d, s, c)	memmove(d, s, c)
#define sisusbcon_memcpyw(d, s, c)	memcpy(d, s, c)

/* vc_data -> sisusb conversion table */
static struct sisusb_usb_data *mysisusbs[MAX_NR_CONSOLES];

/* Forward declaration */
static const struct consw sisusb_con;

static inline void
sisusbcon_memsetw(u16 *s, u16 c, unsigned int count)
{
	count /= 2;
	while (count--)
		sisusbcon_writew(c, s++);
}

static inline void
sisusb_initialize(struct sisusb_usb_data *sisusb)
{
	/* Reset cursor and start address */
	if (sisusb_setidxreg(sisusb, SISCR, 0x0c, 0x00))
		return;
	if (sisusb_setidxreg(sisusb, SISCR, 0x0d, 0x00))
		return;
	if (sisusb_setidxreg(sisusb, SISCR, 0x0e, 0x00))
		return;
	sisusb_setidxreg(sisusb, SISCR, 0x0f, 0x00);
}

static inline void
sisusbcon_set_start_address(struct sisusb_usb_data *sisusb, struct vc_data *c)
{
	sisusb->cur_start_addr = (c->vc_visible_origin - sisusb->scrbuf) / 2;

	sisusb_setidxreg(sisusb, SISCR, 0x0c, (sisusb->cur_start_addr >> 8));
	sisusb_setidxreg(sisusb, SISCR, 0x0d, (sisusb->cur_start_addr & 0xff));
}

void
sisusb_set_cursor(struct sisusb_usb_data *sisusb, unsigned int location)
{
	if (sisusb->sisusb_cursor_loc == location)
		return;

	sisusb->sisusb_cursor_loc = location;

	/* Hardware bug: Text cursor appears twice or not at all
	 * at some positions. Work around it with the cursor skew
	 * bits.
	 */

	if ((location & 0x0007) == 0x0007) {
		sisusb->bad_cursor_pos = 1;
		location--;
		if (sisusb_setidxregandor(sisusb, SISCR, 0x0b, 0x1f, 0x20))
			return;
	} else if (sisusb->bad_cursor_pos) {
		if (sisusb_setidxregand(sisusb, SISCR, 0x0b, 0x1f))
			return;
		sisusb->bad_cursor_pos = 0;
	}

	if (sisusb_setidxreg(sisusb, SISCR, 0x0e, (location >> 8)))
		return;
	sisusb_setidxreg(sisusb, SISCR, 0x0f, (location & 0xff));
}

static inline struct sisusb_usb_data *
sisusb_get_sisusb(unsigned short console)
{
	return mysisusbs[console];
}

static inline int
sisusb_sisusb_valid(struct sisusb_usb_data *sisusb)
{
	if (!sisusb->present || !sisusb->ready || !sisusb->sisusb_dev)
		return 0;

	return 1;
}

static struct sisusb_usb_data *
sisusb_get_sisusb_lock_and_check(unsigned short console)
{
	struct sisusb_usb_data *sisusb;

	/* We can't handle console calls in non-schedulable
	 * context due to our locks and the USB transport.
	 * So we simply ignore them. This should only affect
	 * some calls to printk.
	 */
	if (in_atomic())
		return NULL;

	if (!(sisusb = sisusb_get_sisusb(console)))
		return NULL;

	mutex_lock(&sisusb->lock);

	if (!sisusb_sisusb_valid(sisusb) ||
	    !sisusb->havethisconsole[console]) {
		mutex_unlock(&sisusb->lock);
		return NULL;
	}

	return sisusb;
}

static int
sisusb_is_inactive(struct vc_data *c, struct sisusb_usb_data *sisusb)
{
	if (sisusb->is_gfx ||
	    sisusb->textmodedestroyed ||
	    c->vc_mode != KD_TEXT)
		return 1;

	return 0;
}

/* con_startup console interface routine */
static const char *
sisusbcon_startup(void)
{
	return "SISUSBCON";
}

/* con_init console interface routine */
static void
sisusbcon_init(struct vc_data *c, int init)
{
	struct sisusb_usb_data *sisusb;
	int cols, rows;

	/* This is called by take_over_console(),
	 * ie by us/under our control. It is
	 * only called after text mode and fonts
	 * are set up/restored.
	 */

	if (!(sisusb = sisusb_get_sisusb(c->vc_num)))
		return;

	mutex_lock(&sisusb->lock);

	if (!sisusb_sisusb_valid(sisusb)) {
		mutex_unlock(&sisusb->lock);
		return;
	}

	c->vc_can_do_color = 1;

	c->vc_complement_mask = 0x7700;

	c->vc_hi_font_mask = sisusb->current_font_512 ? 0x0800 : 0;

	sisusb->haveconsole = 1;

	sisusb->havethisconsole[c->vc_num] = 1;

	/* We only support 640x400 */
	c->vc_scan_lines = 400;

	c->vc_font.height = sisusb->current_font_height;

	/* We only support width = 8 */
	cols = 80;
	rows = c->vc_scan_lines / c->vc_font.height;

	/* Increment usage count for our sisusb.
	 * Doing so saves us from upping/downing
	 * the disconnect semaphore; we can't
	 * lose our sisusb until this is undone
	 * in con_deinit. For all other console
	 * interface functions, it suffices to
	 * use sisusb->lock and do a quick check
	 * of sisusb for device disconnection.
	 */
	kref_get(&sisusb->kref);

	if (!*c->vc_uni_pagedir_loc)
		con_set_default_unimap(c);

	mutex_unlock(&sisusb->lock);

	if (init) {
		c->vc_cols = cols;
		c->vc_rows = rows;
	} else
		vc_resize(c, cols, rows);
}

/* con_deinit console interface routine */
static void
sisusbcon_deinit(struct vc_data *c)
{
	struct sisusb_usb_data *sisusb;
	int i;

	/* This is called by take_over_console()
	 * and others, ie not under our control.
	 */

	if (!(sisusb = sisusb_get_sisusb(c->vc_num)))
		return;

	mutex_lock(&sisusb->lock);

	/* Clear ourselves in mysisusbs */
	mysisusbs[c->vc_num] = NULL;

	sisusb->havethisconsole[c->vc_num] = 0;

	/* Free our font buffer if all consoles are gone */
	if (sisusb->font_backup) {
		for(i = 0; i < MAX_NR_CONSOLES; i++) {
			if (sisusb->havethisconsole[c->vc_num])
				break;
		}
		if (i == MAX_NR_CONSOLES) {
			vfree(sisusb->font_backup);
			sisusb->font_backup = NULL;
		}
	}

	mutex_unlock(&sisusb->lock);

	/* decrement the usage count on our sisusb */
	kref_put(&sisusb->kref, sisusb_delete);
}

/* interface routine */
static u8
sisusbcon_build_attr(struct vc_data *c, u8 color, u8 intensity,
			    u8 blink, u8 underline, u8 reverse, u8 unused)
{
	u8 attr = color;

	if (underline)
		attr = (attr & 0xf0) | c->vc_ulcolor;
	else if (intensity == 0)
		attr = (attr & 0xf0) | c->vc_halfcolor;

	if (reverse)
		attr = ((attr) & 0x88) |
		       ((((attr) >> 4) |
		       ((attr) << 4)) & 0x77);

	if (blink)
		attr ^= 0x80;

	if (intensity == 2)
		attr ^= 0x08;

	return attr;
}

/* Interface routine */
static void
sisusbcon_invert_region(struct vc_data *vc, u16 *p, int count)
{
	/* Invert a region. This is called with a pointer
	 * to the console's internal screen buffer. So we
	 * simply do the inversion there and rely on
	 * a call to putc(s) to update the real screen.
	 */

	while (count--) {
		u16 a = sisusbcon_readw(p);

		a = ((a) & 0x88ff)        |
		    (((a) & 0x7000) >> 4) |
		    (((a) & 0x0700) << 4);

		sisusbcon_writew(a, p++);
	}
}

#define SISUSB_VADDR(x,y) \
	((u16 *)c->vc_origin + \
	(y) * sisusb->sisusb_num_columns + \
	(x))

#define SISUSB_HADDR(x,y) \
	((u16 *)(sisusb->vrambase + (c->vc_origin - sisusb->scrbuf)) + \
	(y) * sisusb->sisusb_num_columns + \
	(x))

/* Interface routine */
static void
sisusbcon_putc(struct vc_data *c, int ch, int y, int x)
{
	struct sisusb_usb_data *sisusb;
	ssize_t written;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return;

	/* sisusb->lock is down */
	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return;
	}


	sisusb_copy_memory(sisusb, (char *)SISUSB_VADDR(x, y),
				(long)SISUSB_HADDR(x, y), 2, &written);

	mutex_unlock(&sisusb->lock);
}

/* Interface routine */
static void
sisusbcon_putcs(struct vc_data *c, const unsigned short *s,
		         int count, int y, int x)
{
	struct sisusb_usb_data *sisusb;
	ssize_t written;
	u16 *dest;
	int i;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return;

	/* sisusb->lock is down */

	/* Need to put the characters into the buffer ourselves,
	 * because the vt does this AFTER calling us.
	 */

	dest = SISUSB_VADDR(x, y);

	for (i = count; i > 0; i--)
		sisusbcon_writew(sisusbcon_readw(s++), dest++);

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return;
	}

	sisusb_copy_memory(sisusb, (char *)SISUSB_VADDR(x, y),
				(long)SISUSB_HADDR(x, y), count * 2, &written);

	mutex_unlock(&sisusb->lock);
}

/* Interface routine */
static void
sisusbcon_clear(struct vc_data *c, int y, int x, int height, int width)
{
	struct sisusb_usb_data *sisusb;
	u16 eattr = c->vc_video_erase_char;
	ssize_t written;
	int i, length, cols;
	u16 *dest;

	if (width <= 0 || height <= 0)
		return;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return;

	/* sisusb->lock is down */

	/* Need to clear buffer ourselves, because the vt does
	 * this AFTER calling us.
	 */

	dest = SISUSB_VADDR(x, y);

	cols = sisusb->sisusb_num_columns;

	if (width > cols)
		width = cols;

	if (x == 0 && width >= c->vc_cols) {

		sisusbcon_memsetw(dest, eattr, height * cols * 2);

	} else {

		for (i = height; i > 0; i--, dest += cols)
			sisusbcon_memsetw(dest, eattr, width * 2);

	}

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return;
	}

	length = ((height * cols) - x - (cols - width - x)) * 2;


	sisusb_copy_memory(sisusb, (unsigned char *)SISUSB_VADDR(x, y),
				(long)SISUSB_HADDR(x, y), length, &written);

	mutex_unlock(&sisusb->lock);
}

/* Interface routine */
static void
sisusbcon_bmove(struct vc_data *c, int sy, int sx,
			 int dy, int dx, int height, int width)
{
	struct sisusb_usb_data *sisusb;
	ssize_t written;
	int cols, length;

	if (width <= 0 || height <= 0)
		return;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return;

	/* sisusb->lock is down */

	cols = sisusb->sisusb_num_columns;

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return;
	}

	length = ((height * cols) - dx - (cols - width - dx)) * 2;


	sisusb_copy_memory(sisusb, (unsigned char *)SISUSB_VADDR(dx, dy),
				(long)SISUSB_HADDR(dx, dy), length, &written);

	mutex_unlock(&sisusb->lock);
}

/* interface routine */
static int
sisusbcon_switch(struct vc_data *c)
{
	struct sisusb_usb_data *sisusb;
	ssize_t written;
	int length;

	/* Returnvalue 0 means we have fully restored screen,
	 *	and vt doesn't need to call do_update_region().
	 * Returnvalue != 0 naturally means the opposite.
	 */

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return 0;

	/* sisusb->lock is down */

	/* Don't write to screen if in gfx mode */
	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return 0;
	}

	/* That really should not happen. It would mean we are
	 * being called while the vc is using its private buffer
	 * as origin.
	 */
	if (c->vc_origin == (unsigned long)c->vc_screenbuf) {
		mutex_unlock(&sisusb->lock);
		dev_dbg(&sisusb->sisusb_dev->dev, "ASSERT ORIGIN != SCREENBUF!\n");
		return 0;
	}

	/* Check that we don't copy too much */
	length = min((int)c->vc_screenbuf_size,
			(int)(sisusb->scrbuf + sisusb->scrbuf_size - c->vc_origin));

	/* Restore the screen contents */
	sisusbcon_memcpyw((u16 *)c->vc_origin, (u16 *)c->vc_screenbuf,
								length);

	sisusb_copy_memory(sisusb, (unsigned char *)c->vc_origin,
				(long)SISUSB_HADDR(0, 0),
				length, &written);

	mutex_unlock(&sisusb->lock);

	return 0;
}

/* interface routine */
static void
sisusbcon_save_screen(struct vc_data *c)
{
	struct sisusb_usb_data *sisusb;
	int length;

	/* Save the current screen contents to vc's private
	 * buffer.
	 */

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return;

	/* sisusb->lock is down */

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return;
	}

	/* Check that we don't copy too much */
	length = min((int)c->vc_screenbuf_size,
			(int)(sisusb->scrbuf + sisusb->scrbuf_size - c->vc_origin));

	/* Save the screen contents to vc's private buffer */
	sisusbcon_memcpyw((u16 *)c->vc_screenbuf, (u16 *)c->vc_origin,
								length);

	mutex_unlock(&sisusb->lock);
}

/* interface routine */
static int
sisusbcon_set_palette(struct vc_data *c, unsigned char *table)
{
	struct sisusb_usb_data *sisusb;
	int i, j;

	/* Return value not used by vt */

	if (!CON_IS_VISIBLE(c))
		return -EINVAL;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return -EINVAL;

	/* sisusb->lock is down */

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return -EINVAL;
	}

	for (i = j = 0; i < 16; i++) {
		if (sisusb_setreg(sisusb, SISCOLIDX, table[i]))
			break;
		if (sisusb_setreg(sisusb, SISCOLDATA, c->vc_palette[j++] >> 2))
			break;
		if (sisusb_setreg(sisusb, SISCOLDATA, c->vc_palette[j++] >> 2))
			break;
		if (sisusb_setreg(sisusb, SISCOLDATA, c->vc_palette[j++] >> 2))
			break;
	}

	mutex_unlock(&sisusb->lock);

	return 0;
}

/* interface routine */
static int
sisusbcon_blank(struct vc_data *c, int blank, int mode_switch)
{
	struct sisusb_usb_data *sisusb;
	u8 sr1, cr17, pmreg, cr63;
	ssize_t written;
	int ret = 0;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return 0;

	/* sisusb->lock is down */

	if (mode_switch)
		sisusb->is_gfx = blank ? 1 : 0;

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return 0;
	}

	switch (blank) {

	case 1:		/* Normal blanking: Clear screen */
	case -1:
		sisusbcon_memsetw((u16 *)c->vc_origin,
				c->vc_video_erase_char,
				c->vc_screenbuf_size);
		sisusb_copy_memory(sisusb,
				(unsigned char *)c->vc_origin,
				(u32)(sisusb->vrambase +
					(c->vc_origin - sisusb->scrbuf)),
				c->vc_screenbuf_size, &written);
		sisusb->con_blanked = 1;
		ret = 1;
		break;

	default:	/* VESA blanking */
		switch (blank) {
		case 0: /* Unblank */
			sr1   = 0x00;
			cr17  = 0x80;
			pmreg = 0x00;
			cr63  = 0x00;
			ret = 1;
			sisusb->con_blanked = 0;
			break;
		case VESA_VSYNC_SUSPEND + 1:
			sr1   = 0x20;
			cr17  = 0x80;
			pmreg = 0x80;
			cr63  = 0x40;
			break;
		case VESA_HSYNC_SUSPEND + 1:
			sr1   = 0x20;
			cr17  = 0x80;
			pmreg = 0x40;
			cr63  = 0x40;
			break;
		case VESA_POWERDOWN + 1:
			sr1   = 0x20;
			cr17  = 0x00;
			pmreg = 0xc0;
			cr63  = 0x40;
			break;
		default:
			mutex_unlock(&sisusb->lock);
			return -EINVAL;
		}

		sisusb_setidxregandor(sisusb, SISSR, 0x01, ~0x20, sr1);
		sisusb_setidxregandor(sisusb, SISCR, 0x17, 0x7f, cr17);
		sisusb_setidxregandor(sisusb, SISSR, 0x1f, 0x3f, pmreg);
		sisusb_setidxregandor(sisusb, SISCR, 0x63, 0xbf, cr63);

	}

	mutex_unlock(&sisusb->lock);

	return ret;
}

/* interface routine */
static int
sisusbcon_scrolldelta(struct vc_data *c, int lines)
{
	struct sisusb_usb_data *sisusb;
	int margin = c->vc_size_row * 4;
	int ul, we, p, st;

	/* The return value does not seem to be used */

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return 0;

	/* sisusb->lock is down */

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return 0;
	}

	if (!lines)		/* Turn scrollback off */
		c->vc_visible_origin = c->vc_origin;
	else {

		if (sisusb->con_rolled_over >
				(c->vc_scr_end - sisusb->scrbuf) + margin) {

			ul = c->vc_scr_end - sisusb->scrbuf;
			we = sisusb->con_rolled_over + c->vc_size_row;

		} else {

			ul = 0;
			we = sisusb->scrbuf_size;

		}

		p = (c->vc_visible_origin - sisusb->scrbuf - ul + we) % we +
				lines * c->vc_size_row;

		st = (c->vc_origin - sisusb->scrbuf - ul + we) % we;

		if (st < 2 * margin)
			margin = 0;

		if (p < margin)
			p = 0;

		if (p > st - margin)
			p = st;

		c->vc_visible_origin = sisusb->scrbuf + (p + ul) % we;
	}

	sisusbcon_set_start_address(sisusb, c);

	mutex_unlock(&sisusb->lock);

	return 1;
}

/* Interface routine */
static void
sisusbcon_cursor(struct vc_data *c, int mode)
{
	struct sisusb_usb_data *sisusb;
	int from, to, baseline;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return;

	/* sisusb->lock is down */

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return;
	}

	if (c->vc_origin != c->vc_visible_origin) {
		c->vc_visible_origin = c->vc_origin;
		sisusbcon_set_start_address(sisusb, c);
	}

	if (mode == CM_ERASE) {
		sisusb_setidxregor(sisusb, SISCR, 0x0a, 0x20);
		sisusb->sisusb_cursor_size_to = -1;
		mutex_unlock(&sisusb->lock);
		return;
	}

	sisusb_set_cursor(sisusb, (c->vc_pos - sisusb->scrbuf) / 2);

	baseline = c->vc_font.height - (c->vc_font.height < 10 ? 1 : 2);

	switch (c->vc_cursor_type & 0x0f) {
		case CUR_BLOCK:		from = 1;
					to   = c->vc_font.height;
					break;
		case CUR_TWO_THIRDS:	from = c->vc_font.height / 3;
					to   = baseline;
					break;
		case CUR_LOWER_HALF:	from = c->vc_font.height / 2;
					to   = baseline;
					break;
		case CUR_LOWER_THIRD:	from = (c->vc_font.height * 2) / 3;
					to   = baseline;
					break;
		case CUR_NONE:		from = 31;
					to = 30;
					break;
		default:
		case CUR_UNDERLINE:	from = baseline - 1;
					to   = baseline;
					break;
	}

	if (sisusb->sisusb_cursor_size_from != from ||
	    sisusb->sisusb_cursor_size_to != to) {

		sisusb_setidxreg(sisusb, SISCR, 0x0a, from);
		sisusb_setidxregandor(sisusb, SISCR, 0x0b, 0xe0, to);

		sisusb->sisusb_cursor_size_from = from;
		sisusb->sisusb_cursor_size_to   = to;
	}

	mutex_unlock(&sisusb->lock);
}

static int
sisusbcon_scroll_area(struct vc_data *c, struct sisusb_usb_data *sisusb,
					int t, int b, int dir, int lines)
{
	int cols = sisusb->sisusb_num_columns;
	int length = ((b - t) * cols) * 2;
	u16 eattr = c->vc_video_erase_char;
	ssize_t written;

	/* sisusb->lock is down */

	/* Scroll an area which does not match the
	 * visible screen's dimensions. This needs
	 * to be done separately, as it does not
	 * use hardware panning.
	 */

	switch (dir) {

		case SM_UP:
			sisusbcon_memmovew(SISUSB_VADDR(0, t),
					   SISUSB_VADDR(0, t + lines),
					   (b - t - lines) * cols * 2);
			sisusbcon_memsetw(SISUSB_VADDR(0, b - lines), eattr,
					  lines * cols * 2);
			break;

		case SM_DOWN:
			sisusbcon_memmovew(SISUSB_VADDR(0, t + lines),
					   SISUSB_VADDR(0, t),
					   (b - t - lines) * cols * 2);
			sisusbcon_memsetw(SISUSB_VADDR(0, t), eattr,
					  lines * cols * 2);
			break;
	}

	sisusb_copy_memory(sisusb, (char *)SISUSB_VADDR(0, t),
				(long)SISUSB_HADDR(0, t), length, &written);

	mutex_unlock(&sisusb->lock);

	return 1;
}

/* Interface routine */
static int
sisusbcon_scroll(struct vc_data *c, int t, int b, int dir, int lines)
{
	struct sisusb_usb_data *sisusb;
	u16 eattr = c->vc_video_erase_char;
	ssize_t written;
	int copyall = 0;
	unsigned long oldorigin;
	unsigned int delta = lines * c->vc_size_row;
	u32 originoffset;

	/* Returning != 0 means we have done the scrolling successfully.
	 * Returning 0 makes vt do the scrolling on its own.
	 * Note that con_scroll is only called if the console is
	 * visible. In that case, the origin should be our buffer,
	 * not the vt's private one.
	 */

	if (!lines)
		return 1;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return 0;

	/* sisusb->lock is down */

	if (sisusb_is_inactive(c, sisusb)) {
		mutex_unlock(&sisusb->lock);
		return 0;
	}

	/* Special case */
	if (t || b != c->vc_rows)
		return sisusbcon_scroll_area(c, sisusb, t, b, dir, lines);

	if (c->vc_origin != c->vc_visible_origin) {
		c->vc_visible_origin = c->vc_origin;
		sisusbcon_set_start_address(sisusb, c);
	}

	/* limit amount to maximum realistic size */
	if (lines > c->vc_rows)
		lines = c->vc_rows;

	oldorigin = c->vc_origin;

	switch (dir) {

	case SM_UP:

		if (c->vc_scr_end + delta >=
				sisusb->scrbuf + sisusb->scrbuf_size) {
			sisusbcon_memcpyw((u16 *)sisusb->scrbuf,
					  (u16 *)(oldorigin + delta),
					  c->vc_screenbuf_size - delta);
			c->vc_origin = sisusb->scrbuf;
			sisusb->con_rolled_over = oldorigin - sisusb->scrbuf;
			copyall = 1;
		} else
			c->vc_origin += delta;

		sisusbcon_memsetw(
			(u16 *)(c->vc_origin + c->vc_screenbuf_size - delta),
					eattr, delta);

		break;

	case SM_DOWN:

		if (oldorigin - delta < sisusb->scrbuf) {
			sisusbcon_memmovew((u16 *)(sisusb->scrbuf +
							sisusb->scrbuf_size -
							c->vc_screenbuf_size +
							delta),
					   (u16 *)oldorigin,
					   c->vc_screenbuf_size - delta);
			c->vc_origin = sisusb->scrbuf +
					sisusb->scrbuf_size -
					c->vc_screenbuf_size;
			sisusb->con_rolled_over = 0;
			copyall = 1;
		} else
			c->vc_origin -= delta;

		c->vc_scr_end = c->vc_origin + c->vc_screenbuf_size;

		scr_memsetw((u16 *)(c->vc_origin), eattr, delta);

		break;
	}

	originoffset = (u32)(c->vc_origin - sisusb->scrbuf);

	if (copyall)
		sisusb_copy_memory(sisusb,
			(char *)c->vc_origin,
			(u32)(sisusb->vrambase + originoffset),
			c->vc_screenbuf_size, &written);
	else if (dir == SM_UP)
		sisusb_copy_memory(sisusb,
			(char *)c->vc_origin + c->vc_screenbuf_size - delta,
			(u32)sisusb->vrambase + originoffset +
					c->vc_screenbuf_size - delta,
			delta, &written);
	else
		sisusb_copy_memory(sisusb,
			(char *)c->vc_origin,
			(u32)(sisusb->vrambase + originoffset),
			delta, &written);

	c->vc_scr_end = c->vc_origin + c->vc_screenbuf_size;
	c->vc_visible_origin = c->vc_origin;

	sisusbcon_set_start_address(sisusb, c);

	c->vc_pos = c->vc_pos - oldorigin + c->vc_origin;

	mutex_unlock(&sisusb->lock);

	return 1;
}

/* Interface routine */
static int
sisusbcon_set_origin(struct vc_data *c)
{
	struct sisusb_usb_data *sisusb;

	/* Returning != 0 means we were successful.
	 * Returning 0 will vt make to use its own
	 *	screenbuffer as the origin.
	 */

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return 0;

	/* sisusb->lock is down */

	if (sisusb_is_inactive(c, sisusb) || sisusb->con_blanked) {
		mutex_unlock(&sisusb->lock);
		return 0;
	}

	c->vc_origin = c->vc_visible_origin = sisusb->scrbuf;

	sisusbcon_set_start_address(sisusb, c);

	sisusb->con_rolled_over = 0;

	mutex_unlock(&sisusb->lock);

	return 1;
}

/* Interface routine */
static int
sisusbcon_resize(struct vc_data *c, unsigned int newcols, unsigned int newrows,
		 unsigned int user)
{
	struct sisusb_usb_data *sisusb;
	int fh;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return -ENODEV;

	fh = sisusb->current_font_height;

	mutex_unlock(&sisusb->lock);

	/* We are quite unflexible as regards resizing. The vt code
	 * handles sizes where the line length isn't equal the pitch
	 * quite badly. As regards the rows, our panning tricks only
	 * work well if the number of rows equals the visible number
	 * of rows.
	 */

	if (newcols != 80 || c->vc_scan_lines / fh != newrows)
		return -EINVAL;

	return 0;
}

int
sisusbcon_do_font_op(struct sisusb_usb_data *sisusb, int set, int slot,
			u8 *arg, int cmapsz, int ch512, int dorecalc,
			struct vc_data *c, int fh, int uplock)
{
	int font_select = 0x00, i, err = 0;
	u32 offset = 0;
	u8 dummy;

	/* sisusb->lock is down */

	/*
	 * The default font is kept in slot 0.
	 * A user font is loaded in slot 2 (256 ch)
	 * or 2+3 (512 ch).
	 */

	if ((slot != 0 && slot != 2) || !fh) {
		if (uplock)
			mutex_unlock(&sisusb->lock);
		return -EINVAL;
	}

	if (set)
		sisusb->font_slot = slot;

	/* Default font is always 256 */
	if (slot == 0)
		ch512 = 0;
	else
		offset = 4 * cmapsz;

	font_select = (slot == 0) ? 0x00 : (ch512 ? 0x0e : 0x0a);

	err |= sisusb_setidxreg(sisusb, SISSR, 0x00, 0x01); /* Reset */
	err |= sisusb_setidxreg(sisusb, SISSR, 0x02, 0x04); /* Write to plane 2 */
	err |= sisusb_setidxreg(sisusb, SISSR, 0x04, 0x07); /* Memory mode a0-bf */
	err |= sisusb_setidxreg(sisusb, SISSR, 0x00, 0x03); /* Reset */

	if (err)
		goto font_op_error;

	err |= sisusb_setidxreg(sisusb, SISGR, 0x04, 0x03); /* Select plane read 2 */
	err |= sisusb_setidxreg(sisusb, SISGR, 0x05, 0x00); /* Disable odd/even */
	err |= sisusb_setidxreg(sisusb, SISGR, 0x06, 0x00); /* Address range a0-bf */

	if (err)
		goto font_op_error;

	if (arg) {
		if (set)
			for (i = 0; i < cmapsz; i++) {
				err |= sisusb_writeb(sisusb,
					sisusb->vrambase + offset + i,
					arg[i]);
				if (err)
					break;
			}
		else
			for (i = 0; i < cmapsz; i++) {
				err |= sisusb_readb(sisusb,
					sisusb->vrambase + offset + i,
					&arg[i]);
				if (err)
					break;
			}

		/*
		 * In 512-character mode, the character map is not contiguous if
		 * we want to remain EGA compatible -- which we do
		 */

		if (ch512) {
			if (set)
				for (i = 0; i < cmapsz; i++) {
					err |= sisusb_writeb(sisusb,
						sisusb->vrambase + offset +
							(2 * cmapsz) + i,
						arg[cmapsz + i]);
					if (err)
						break;
				}
			else
				for (i = 0; i < cmapsz; i++) {
					err |= sisusb_readb(sisusb,
						sisusb->vrambase + offset +
							(2 * cmapsz) + i,
						&arg[cmapsz + i]);
					if (err)
						break;
				}
		}
	}

	if (err)
		goto font_op_error;

	err |= sisusb_setidxreg(sisusb, SISSR, 0x00, 0x01); /* Reset */
	err |= sisusb_setidxreg(sisusb, SISSR, 0x02, 0x03); /* Write to planes 0+1 */
	err |= sisusb_setidxreg(sisusb, SISSR, 0x04, 0x03); /* Memory mode a0-bf */
	if (set)
		sisusb_setidxreg(sisusb, SISSR, 0x03, font_select);
	err |= sisusb_setidxreg(sisusb, SISSR, 0x00, 0x03); /* Reset end */

	if (err)
		goto font_op_error;

	err |= sisusb_setidxreg(sisusb, SISGR, 0x04, 0x00); /* Select plane read 0 */
	err |= sisusb_setidxreg(sisusb, SISGR, 0x05, 0x10); /* Enable odd/even */
	err |= sisusb_setidxreg(sisusb, SISGR, 0x06, 0x06); /* Address range b8-bf */

	if (err)
		goto font_op_error;

	if ((set) && (ch512 != sisusb->current_font_512)) {

		/* Font is shared among all our consoles.
		 * And so is the hi_font_mask.
		 */
		for (i = 0; i < MAX_NR_CONSOLES; i++) {
			struct vc_data *c = vc_cons[i].d;
			if (c && c->vc_sw == &sisusb_con)
				c->vc_hi_font_mask = ch512 ? 0x0800 : 0;
		}

		sisusb->current_font_512 = ch512;

		/* color plane enable register:
			256-char: enable intensity bit
			512-char: disable intensity bit */
		sisusb_getreg(sisusb, SISINPSTAT, &dummy);
		sisusb_setreg(sisusb, SISAR, 0x12);
		sisusb_setreg(sisusb, SISAR, ch512 ? 0x07 : 0x0f);

		sisusb_getreg(sisusb, SISINPSTAT, &dummy);
		sisusb_setreg(sisusb, SISAR, 0x20);
		sisusb_getreg(sisusb, SISINPSTAT, &dummy);
	}

	if (dorecalc) {

		/*
		 * Adjust the screen to fit a font of a certain height
		 */

		unsigned char ovr, vde, fsr;
		int rows = 0, maxscan = 0;

		if (c) {

			/* Number of video rows */
			rows = c->vc_scan_lines / fh;
			/* Scan lines to actually display-1 */
			maxscan = rows * fh - 1;

			/*printk(KERN_DEBUG "sisusb recalc rows %d maxscan %d fh %d sl %d\n",
				rows, maxscan, fh, c->vc_scan_lines);*/

			sisusb_getidxreg(sisusb, SISCR, 0x07, &ovr);
			vde = maxscan & 0xff;
			ovr = (ovr & 0xbd) |
			      ((maxscan & 0x100) >> 7) |
			      ((maxscan & 0x200) >> 3);
			sisusb_setidxreg(sisusb, SISCR, 0x07, ovr);
			sisusb_setidxreg(sisusb, SISCR, 0x12, vde);

		}

		sisusb_getidxreg(sisusb, SISCR, 0x09, &fsr);
		fsr = (fsr & 0xe0) | (fh - 1);
		sisusb_setidxreg(sisusb, SISCR, 0x09, fsr);
		sisusb->current_font_height = fh;

		sisusb->sisusb_cursor_size_from = -1;
		sisusb->sisusb_cursor_size_to   = -1;

	}

	if (uplock)
		mutex_unlock(&sisusb->lock);

	if (dorecalc && c) {
		int i, rows = c->vc_scan_lines / fh;

		/* Now adjust our consoles' size */

		for (i = 0; i < MAX_NR_CONSOLES; i++) {
			struct vc_data *vc = vc_cons[i].d;

			if (vc && vc->vc_sw == &sisusb_con) {
				if (CON_IS_VISIBLE(vc)) {
					vc->vc_sw->con_cursor(vc, CM_DRAW);
				}
				vc->vc_font.height = fh;
				vc_resize(vc, 0, rows);
			}
		}
	}

	return 0;

font_op_error:
	if (uplock)
		mutex_unlock(&sisusb->lock);

	return -EIO;
}

/* Interface routine */
static int
sisusbcon_font_set(struct vc_data *c, struct console_font *font,
							unsigned flags)
{
	struct sisusb_usb_data *sisusb;
	unsigned charcount = font->charcount;

	if (font->width != 8 || (charcount != 256 && charcount != 512))
		return -EINVAL;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return -ENODEV;

	/* sisusb->lock is down */

	/* Save the user-provided font into a buffer. This
	 * is used for restoring text mode after quitting
	 * from X and for the con_getfont routine.
	 */
	if (sisusb->font_backup) {
		if (sisusb->font_backup_size < charcount) {
			vfree(sisusb->font_backup);
			sisusb->font_backup = NULL;
		}
	}

	if (!sisusb->font_backup)
		sisusb->font_backup = vmalloc(charcount * 32);

	if (sisusb->font_backup) {
		memcpy(sisusb->font_backup, font->data, charcount * 32);
		sisusb->font_backup_size = charcount;
		sisusb->font_backup_height = font->height;
		sisusb->font_backup_512 = (charcount == 512) ? 1 : 0;
	}

	/* do_font_op ups sisusb->lock */

	return sisusbcon_do_font_op(sisusb, 1, 2, font->data,
			8192, (charcount == 512),
			(!(flags & KD_FONT_FLAG_DONT_RECALC)) ? 1 : 0,
			c, font->height, 1);
}

/* Interface routine */
static int
sisusbcon_font_get(struct vc_data *c, struct console_font *font)
{
	struct sisusb_usb_data *sisusb;

	if (!(sisusb = sisusb_get_sisusb_lock_and_check(c->vc_num)))
		return -ENODEV;

	/* sisusb->lock is down */

	font->width = 8;
	font->height = c->vc_font.height;
	font->charcount = 256;

	if (!font->data) {
		mutex_unlock(&sisusb->lock);
		return 0;
	}

	if (!sisusb->font_backup) {
		mutex_unlock(&sisusb->lock);
		return -ENODEV;
	}

	/* Copy 256 chars only, like vgacon */
	memcpy(font->data, sisusb->font_backup, 256 * 32);

	mutex_unlock(&sisusb->lock);

	return 0;
}

/*
 *  The console `switch' structure for the sisusb console
 */

static const struct consw sisusb_con = {
	.owner =		THIS_MODULE,
	.con_startup =		sisusbcon_startup,
	.con_init =		sisusbcon_init,
	.con_deinit =		sisusbcon_deinit,
	.con_clear =		sisusbcon_clear,
	.con_putc =		sisusbcon_putc,
	.con_putcs =		sisusbcon_putcs,
	.con_cursor =		sisusbcon_cursor,
	.con_scroll =		sisusbcon_scroll,
	.con_bmove =		sisusbcon_bmove,
	.con_switch =		sisusbcon_switch,
	.con_blank =		sisusbcon_blank,
	.con_font_set =		sisusbcon_font_set,
	.con_font_get =		sisusbcon_font_get,
	.con_set_palette =	sisusbcon_set_palette,
	.con_scrolldelta =	sisusbcon_scrolldelta,
	.con_build_attr =	sisusbcon_build_attr,
	.con_invert_region =	sisusbcon_invert_region,
	.con_set_origin =	sisusbcon_set_origin,
	.con_save_screen =	sisusbcon_save_screen,
	.con_resize =		sisusbcon_resize,
};

/* Our very own dummy console driver */

static const char *sisusbdummycon_startup(void)
{
    return "SISUSBVGADUMMY";
}

static void sisusbdummycon_init(struct vc_data *vc, int init)
{
    vc->vc_can_do_color = 1;
    if (init) {
	vc->vc_cols = 80;
	vc->vc_rows = 25;
    } else
	vc_resize(vc, 80, 25);
}

static int sisusbdummycon_dummy(void)
{
    return 0;
}

#define SISUSBCONDUMMY	(void *)sisusbdummycon_dummy

static const struct consw sisusb_dummy_con = {
	.owner =		THIS_MODULE,
	.con_startup =		sisusbdummycon_startup,
	.con_init =		sisusbdummycon_init,
	.con_deinit =		SISUSBCONDUMMY,
	.con_clear =		SISUSBCONDUMMY,
	.con_putc =		SISUSBCONDUMMY,
	.con_putcs =		SISUSBCONDUMMY,
	.con_cursor =		SISUSBCONDUMMY,
	.con_scroll =		SISUSBCONDUMMY,
	.con_bmove =		SISUSBCONDUMMY,
	.con_switch =		SISUSBCONDUMMY,
	.con_blank =		SISUSBCONDUMMY,
	.con_font_set =		SISUSBCONDUMMY,
	.con_font_get =		SISUSBCONDUMMY,
	.con_font_default =	SISUSBCONDUMMY,
	.con_font_copy =	SISUSBCONDUMMY,
	.con_set_palette =	SISUSBCONDUMMY,
	.con_scrolldelta =	SISUSBCONDUMMY,
};

int
sisusb_console_init(struct sisusb_usb_data *sisusb, int first, int last)
{
	int i, ret;

	mutex_lock(&sisusb->lock);

	/* Erm.. that should not happen */
	if (sisusb->haveconsole || !sisusb->SiS_Pr) {
		mutex_unlock(&sisusb->lock);
		return 1;
	}

	sisusb->con_first = first;
	sisusb->con_last  = last;

	if (first > last ||
	    first > MAX_NR_CONSOLES ||
	    last > MAX_NR_CONSOLES) {
		mutex_unlock(&sisusb->lock);
		return 1;
	}

	/* If gfxcore not initialized or no consoles given, quit graciously */
	if (!sisusb->gfxinit || first < 1 || last < 1) {
		mutex_unlock(&sisusb->lock);
		return 0;
	}

	sisusb->sisusb_cursor_loc       = -1;
	sisusb->sisusb_cursor_size_from = -1;
	sisusb->sisusb_cursor_size_to   = -1;

	/* Set up text mode (and upload  default font) */
	if (sisusb_reset_text_mode(sisusb, 1)) {
		mutex_unlock(&sisusb->lock);
		dev_err(&sisusb->sisusb_dev->dev, "Failed to set up text mode\n");
		return 1;
	}

	/* Initialize some gfx registers */
	sisusb_initialize(sisusb);

	for (i = first - 1; i <= last - 1; i++) {
		/* Save sisusb for our interface routines */
		mysisusbs[i] = sisusb;
	}

	/* Initial console setup */
	sisusb->sisusb_num_columns = 80;

	/* Use a 32K buffer (matches b8000-bffff area) */
	sisusb->scrbuf_size = 32 * 1024;

	/* Allocate screen buffer */
	if (!(sisusb->scrbuf = (unsigned long)vmalloc(sisusb->scrbuf_size))) {
		mutex_unlock(&sisusb->lock);
		dev_err(&sisusb->sisusb_dev->dev, "Failed to allocate screen buffer\n");
		return 1;
	}

	mutex_unlock(&sisusb->lock);

	/* Now grab the desired console(s) */
	ret = take_over_console(&sisusb_con, first - 1, last - 1, 0);

	if (!ret)
		sisusb->haveconsole = 1;
	else {
		for (i = first - 1; i <= last - 1; i++)
			mysisusbs[i] = NULL;
	}

	return ret;
}

void
sisusb_console_exit(struct sisusb_usb_data *sisusb)
{
	int i;

	/* This is called if the device is disconnected
	 * and while disconnect and lock semaphores
	 * are up. This should be save because we
	 * can't lose our sisusb any other way but by
	 * disconnection (and hence, the disconnect
	 * sema is for protecting all other access
	 * functions from disconnection, not the
	 * other way round).
	 */

	/* Now what do we do in case of disconnection:
	 * One alternative would be to simply call
	 * give_up_console(). Nah, not a good idea.
	 * give_up_console() is obviously buggy as it
	 * only discards the consw pointer from the
	 * driver_map, but doesn't adapt vc->vc_sw
	 * of the affected consoles. Hence, the next
	 * call to any of the console functions will
	 * eventually take a trip to oops county.
	 * Also, give_up_console for some reason
	 * doesn't decrement our module refcount.
	 * Instead, we switch our consoles to a private
	 * dummy console. This, of course, keeps our
	 * refcount up as well, but it works perfectly.
	 */

	if (sisusb->haveconsole) {
		for (i = 0; i < MAX_NR_CONSOLES; i++)
			if (sisusb->havethisconsole[i])
				take_over_console(&sisusb_dummy_con, i, i, 0);
				/* At this point, con_deinit for all our
				 * consoles is executed by take_over_console().
				 */
		sisusb->haveconsole = 0;
	}

	vfree((void *)sisusb->scrbuf);
	sisusb->scrbuf = 0;

	vfree(sisusb->font_backup);
	sisusb->font_backup = NULL;
}

void __init sisusb_init_concode(void)
{
	int i;

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		mysisusbs[i] = NULL;
}

#endif /* INCL_CON */



