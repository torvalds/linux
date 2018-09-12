/*
 * Copyright (C) 2013 Intel Corporation; author Matt Fleming
 *
 *  This file is part of the Linux kernel, and is made available under
 *  the terms of the GNU General Public License version 2.
 */

#include <linux/console.h>
#include <linux/efi.h>
#include <linux/font.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <asm/setup.h>

static const struct font_desc *font;
static u32 efi_x, efi_y;
static void *efi_fb;
static bool early_efi_keep;

/*
 * efi earlyprintk need use early_ioremap to map the framebuffer.
 * But early_ioremap is not usable for earlyprintk=efi,keep, ioremap should
 * be used instead. ioremap will be available after paging_init() which is
 * earlier than initcall callbacks. Thus adding this early initcall function
 * early_efi_map_fb to map the whole efi framebuffer.
 */
static __init int early_efi_map_fb(void)
{
	u64 base, size;

	if (!early_efi_keep)
		return 0;

	base = boot_params.screen_info.lfb_base;
	if (boot_params.screen_info.capabilities & VIDEO_CAPABILITY_64BIT_BASE)
		base |= (u64)boot_params.screen_info.ext_lfb_base << 32;
	size = boot_params.screen_info.lfb_size;
	efi_fb = ioremap(base, size);

	return efi_fb ? 0 : -ENOMEM;
}
early_initcall(early_efi_map_fb);

/*
 * early_efi_map maps efi framebuffer region [start, start + len -1]
 * In case earlyprintk=efi,keep we have the whole framebuffer mapped already
 * so just return the offset efi_fb + start.
 */
static __ref void *early_efi_map(unsigned long start, unsigned long len)
{
	u64 base;

	base = boot_params.screen_info.lfb_base;
	if (boot_params.screen_info.capabilities & VIDEO_CAPABILITY_64BIT_BASE)
		base |= (u64)boot_params.screen_info.ext_lfb_base << 32;

	if (efi_fb)
		return (efi_fb + start);
	else
		return early_ioremap(base + start, len);
}

static __ref void early_efi_unmap(void *addr, unsigned long len)
{
	if (!efi_fb)
		early_iounmap(addr, len);
}

static void early_efi_clear_scanline(unsigned int y)
{
	unsigned long *dst;
	u16 len;

	len = boot_params.screen_info.lfb_linelength;
	dst = early_efi_map(y*len, len);
	if (!dst)
		return;

	memset(dst, 0, len);
	early_efi_unmap(dst, len);
}

static void early_efi_scroll_up(void)
{
	unsigned long *dst, *src;
	u16 len;
	u32 i, height;

	len = boot_params.screen_info.lfb_linelength;
	height = boot_params.screen_info.lfb_height;

	for (i = 0; i < height - font->height; i++) {
		dst = early_efi_map(i*len, len);
		if (!dst)
			return;

		src = early_efi_map((i + font->height) * len, len);
		if (!src) {
			early_efi_unmap(dst, len);
			return;
		}

		memmove(dst, src, len);

		early_efi_unmap(src, len);
		early_efi_unmap(dst, len);
	}
}

static void early_efi_write_char(u32 *dst, unsigned char c, unsigned int h)
{
	const u32 color_black = 0x00000000;
	const u32 color_white = 0x00ffffff;
	const u8 *src;
	u8 s8;
	int m;

	src = font->data + c * font->height;
	s8 = *(src + h);

	for (m = 0; m < 8; m++) {
		if ((s8 >> (7 - m)) & 1)
			*dst = color_white;
		else
			*dst = color_black;
		dst++;
	}
}

static void
early_efi_write(struct console *con, const char *str, unsigned int num)
{
	struct screen_info *si;
	unsigned int len;
	const char *s;
	void *dst;

	si = &boot_params.screen_info;
	len = si->lfb_linelength;

	while (num) {
		unsigned int linemax;
		unsigned int h, count = 0;

		for (s = str; *s && *s != '\n'; s++) {
			if (count == num)
				break;
			count++;
		}

		linemax = (si->lfb_width - efi_x) / font->width;
		if (count > linemax)
			count = linemax;

		for (h = 0; h < font->height; h++) {
			unsigned int n, x;

			dst = early_efi_map((efi_y + h) * len, len);
			if (!dst)
				return;

			s = str;
			n = count;
			x = efi_x;

			while (n-- > 0) {
				early_efi_write_char(dst + x*4, *s, h);
				x += font->width;
				s++;
			}

			early_efi_unmap(dst, len);
		}

		num -= count;
		efi_x += count * font->width;
		str += count;

		if (num > 0 && *s == '\n') {
			efi_x = 0;
			efi_y += font->height;
			str++;
			num--;
		}

		if (efi_x >= si->lfb_width) {
			efi_x = 0;
			efi_y += font->height;
		}

		if (efi_y + font->height > si->lfb_height) {
			u32 i;

			efi_y -= font->height;
			early_efi_scroll_up();

			for (i = 0; i < font->height; i++)
				early_efi_clear_scanline(efi_y + i);
		}
	}
}

static __init int early_efi_setup(struct console *con, char *options)
{
	struct screen_info *si;
	u16 xres, yres;
	u32 i;

	si = &boot_params.screen_info;
	xres = si->lfb_width;
	yres = si->lfb_height;

	/*
	 * early_efi_write_char() implicitly assumes a framebuffer with
	 * 32-bits per pixel.
	 */
	if (si->lfb_depth != 32)
		return -ENODEV;

	font = get_default_font(xres, yres, -1, -1);
	if (!font)
		return -ENODEV;

	efi_y = rounddown(yres, font->height) - font->height;
	for (i = 0; i < (yres - efi_y) / font->height; i++)
		early_efi_scroll_up();

	/* early_console_register will unset CON_BOOT in case ,keep */
	if (!(con->flags & CON_BOOT))
		early_efi_keep = true;
	return 0;
}

struct console early_efi_console = {
	.name =		"earlyefi",
	.write =	early_efi_write,
	.setup =	early_efi_setup,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};
