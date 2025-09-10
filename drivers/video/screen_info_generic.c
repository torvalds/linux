// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/screen_info.h>
#include <linux/string.h>

#include <video/pixel_format.h>

static void resource_init_named(struct resource *r,
				resource_size_t start, resource_size_t size,
				const char *name, unsigned int flags)
{
	memset(r, 0, sizeof(*r));

	r->start = start;
	r->end = start + size - 1;
	r->name = name;
	r->flags = flags;
}

static void resource_init_io_named(struct resource *r,
				   resource_size_t start, resource_size_t size,
				   const char *name)
{
	resource_init_named(r, start, size, name, IORESOURCE_IO);
}

static void resource_init_mem_named(struct resource *r,
				   resource_size_t start, resource_size_t size,
				   const char *name)
{
	resource_init_named(r, start, size, name, IORESOURCE_MEM);
}

static inline bool __screen_info_has_ega_gfx(unsigned int mode)
{
	switch (mode) {
	case 0x0d:	/* 320x200-4 */
	case 0x0e:	/* 640x200-4 */
	case 0x0f:	/* 640x350-1 */
	case 0x10:	/* 640x350-4 */
		return true;
	default:
		return false;
	}
}

static inline bool __screen_info_has_vga_gfx(unsigned int mode)
{
	switch (mode) {
	case 0x10:	/* 640x480-1 */
	case 0x12:	/* 640x480-4 */
	case 0x13:	/* 320-200-8 */
	case 0x6a:	/* 800x600-4 (VESA) */
		return true;
	default:
		return __screen_info_has_ega_gfx(mode);
	}
}

/**
 * screen_info_resources() - Get resources from screen_info structure
 * @si: the screen_info
 * @r: pointer to an array of resource structures
 * @num: number of elements in @r:
 *
 * Returns:
 * The number of resources stored in @r on success, or a negative errno code otherwise.
 *
 * A call to screen_info_resources() returns the resources consumed by the
 * screen_info's device or framebuffer. The result is stored in the caller-supplied
 * array @r with up to @num elements. The function returns the number of
 * initialized elements.
 */
ssize_t screen_info_resources(const struct screen_info *si, struct resource *r, size_t num)
{
	struct resource *pos = r;
	unsigned int type = screen_info_video_type(si);
	u64 base, size;

	switch (type) {
	case VIDEO_TYPE_MDA:
		if (num > 0)
			resource_init_io_named(pos++, 0x3b0, 12, "mda");
		if (num > 1)
			resource_init_io_named(pos++, 0x3bf, 0x01, "mda");
		if (num > 2)
			resource_init_mem_named(pos++, 0xb0000, 0x2000, "mda");
		break;
	case VIDEO_TYPE_CGA:
		if (num > 0)
			resource_init_io_named(pos++, 0x3d4, 0x02, "cga");
		if (num > 1)
			resource_init_mem_named(pos++, 0xb8000, 0x2000, "cga");
		break;
	case VIDEO_TYPE_EGAM:
		if (num > 0)
			resource_init_io_named(pos++, 0x3bf, 0x10, "ega");
		if (num > 1)
			resource_init_mem_named(pos++, 0xb0000, 0x8000, "ega");
		break;
	case VIDEO_TYPE_EGAC:
		if (num > 0)
			resource_init_io_named(pos++, 0x3c0, 0x20, "ega");
		if (num > 1) {
			if (__screen_info_has_ega_gfx(si->orig_video_mode))
				resource_init_mem_named(pos++, 0xa0000, 0x10000, "ega");
			else
				resource_init_mem_named(pos++, 0xb8000, 0x8000, "ega");
		}
		break;
	case VIDEO_TYPE_VGAC:
		if (num > 0)
			resource_init_io_named(pos++, 0x3c0, 0x20, "vga+");
		if (num > 1) {
			if (__screen_info_has_vga_gfx(si->orig_video_mode))
				resource_init_mem_named(pos++, 0xa0000, 0x10000, "vga+");
			else
				resource_init_mem_named(pos++, 0xb8000, 0x8000, "vga+");
		}
		break;
	case VIDEO_TYPE_VLFB:
	case VIDEO_TYPE_EFI:
		base = __screen_info_lfb_base(si);
		if (!base)
			break;
		size = __screen_info_lfb_size(si, type);
		if (!size)
			break;
		if (num > 0)
			resource_init_mem_named(pos++, base, size, "lfb");
		break;
	case VIDEO_TYPE_PICA_S3:
	case VIDEO_TYPE_MIPS_G364:
	case VIDEO_TYPE_SGI:
	case VIDEO_TYPE_TGAC:
	case VIDEO_TYPE_SUN:
	case VIDEO_TYPE_SUNPCI:
	case VIDEO_TYPE_PMAC:
	default:
		/* not supported */
		return -EINVAL;
	}

	return pos - r;
}
EXPORT_SYMBOL(screen_info_resources);

/*
 * The meaning of depth and bpp for direct-color formats is
 * inconsistent:
 *
 *  - DRM format info specifies depth as the number of color
 *    bits; including alpha, but not including filler bits.
 *  - Linux' EFI platform code computes lfb_depth from the
 *    individual color channels, including the reserved bits.
 *  - VBE 1.1 defines lfb_depth for XRGB1555 as 16, but later
 *    versions use 15.
 *  - On the kernel command line, 'bpp' of 32 is usually
 *    XRGB8888 including the filler bits, but 15 is XRGB1555
 *    not including the filler bit.
 *
 * It is not easily possible to fix this in struct screen_info,
 * as this could break UAPI. The best solution is to compute
 * bits_per_pixel from the color bits, reserved bits and
 * reported lfb_depth, whichever is highest.
 */

u32 __screen_info_lfb_bits_per_pixel(const struct screen_info *si)
{
	u32 bits_per_pixel = si->lfb_depth;

	if (bits_per_pixel > 8) {
		bits_per_pixel = max(max3(si->red_size + si->red_pos,
					  si->green_size + si->green_pos,
					  si->blue_size + si->blue_pos),
				     si->rsvd_size + si->rsvd_pos);
		bits_per_pixel = max_t(u32, bits_per_pixel, si->lfb_depth);
	}

	return bits_per_pixel;
}
EXPORT_SYMBOL(__screen_info_lfb_bits_per_pixel);

static int __screen_info_lfb_pixel_format(const struct screen_info *si, struct pixel_format *f)
{
	u32 bits_per_pixel = __screen_info_lfb_bits_per_pixel(si);

	if (bits_per_pixel > U8_MAX)
		return -EINVAL;

	f->bits_per_pixel = bits_per_pixel;

	if (si->lfb_depth > 8) {
		f->indexed = false;
		f->alpha.offset = 0;
		f->alpha.length = 0;
		f->red.offset = si->red_pos;
		f->red.length = si->red_size;
		f->green.offset = si->green_pos;
		f->green.length = si->green_size;
		f->blue.offset = si->blue_pos;
		f->blue.length = si->blue_size;
	} else {
		f->indexed = true;
		f->index.offset = 0;
		f->index.length = si->lfb_depth;
	}

	return 0;
}

/**
 * screen_info_pixel_format - Returns the screen-info format as pixel-format description
 *
 * @si: the screen_info
 * @f: pointer to return pixel-format description
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int screen_info_pixel_format(const struct screen_info *si, struct pixel_format *f)
{
	unsigned int type = screen_info_video_type(si);

	/* TODO: Add support for additional types as needed. */
	switch (type) {
	case VIDEO_TYPE_VLFB:
	case VIDEO_TYPE_EFI:
		return __screen_info_lfb_pixel_format(si, f);
	}

	/* not supported */
	return -EINVAL;
}
EXPORT_SYMBOL(screen_info_pixel_format);
