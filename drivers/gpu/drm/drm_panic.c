// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright (c) 2023 Red Hat.
 * Author: Jocelyn Falempe <jfalempe@redhat.com>
 * inspired by the drm_log driver from David Herrmann <dh.herrmann@gmail.com>
 * Tux Ascii art taken from cowsay written by Tony Monroe
 */

#include <linux/font.h>
#include <linux/iosys-map.h>
#include <linux/kdebug.h>
#include <linux/kmsg_dump.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/types.h>

#include <drm/drm_drv.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panic.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

MODULE_AUTHOR("Jocelyn Falempe");
MODULE_DESCRIPTION("DRM panic handler");
MODULE_LICENSE("GPL");

/**
 * DOC: overview
 *
 * To enable DRM panic for a driver, the primary plane must implement a
 * &drm_plane_helper_funcs.get_scanout_buffer helper function. It is then
 * automatically registered to the drm panic handler.
 * When a panic occurs, the &drm_plane_helper_funcs.get_scanout_buffer will be
 * called, and the driver can provide a framebuffer so the panic handler can
 * draw the panic screen on it. Currently only linear buffer and a few color
 * formats are supported.
 * Optionally the driver can also provide a &drm_plane_helper_funcs.panic_flush
 * callback, that will be called after that, to send additional commands to the
 * hardware to make the scanout buffer visible.
 */

/*
 * This module displays a user friendly message on screen when a kernel panic
 * occurs. This is conflicting with fbcon, so you can only enable it when fbcon
 * is disabled.
 * It's intended for end-user, so have minimal technical/debug information.
 *
 * Implementation details:
 *
 * It is a panic handler, so it can't take lock, allocate memory, run tasks/irq,
 * or attempt to sleep. It's a best effort, and it may not be able to display
 * the message in all situations (like if the panic occurs in the middle of a
 * modesetting).
 * It will display only one static frame, so performance optimizations are low
 * priority as the machine is already in an unusable state.
 */

struct drm_panic_line {
	u32 len;
	const char *txt;
};

#define PANIC_LINE(s) {.len = sizeof(s) - 1, .txt = s}

static struct drm_panic_line panic_msg[] = {
	PANIC_LINE("KERNEL PANIC !"),
	PANIC_LINE(""),
	PANIC_LINE("Please reboot your computer."),
};

static const struct drm_panic_line logo[] = {
	PANIC_LINE("     .--.        _"),
	PANIC_LINE("    |o_o |      | |"),
	PANIC_LINE("    |:_/ |      | |"),
	PANIC_LINE("   //   \\ \\     |_|"),
	PANIC_LINE("  (|     | )     _"),
	PANIC_LINE(" /'\\_   _/`\\    (_)"),
	PANIC_LINE(" \\___)=(___/"),
};

/*
 * Color conversion
 */

static u16 convert_xrgb8888_to_rgb565(u32 pix)
{
	return ((pix & 0x00F80000) >> 8) |
	       ((pix & 0x0000FC00) >> 5) |
	       ((pix & 0x000000F8) >> 3);
}

static u16 convert_xrgb8888_to_rgba5551(u32 pix)
{
	return ((pix & 0x00f80000) >> 8) |
	       ((pix & 0x0000f800) >> 5) |
	       ((pix & 0x000000f8) >> 2) |
	       BIT(0); /* set alpha bit */
}

static u16 convert_xrgb8888_to_xrgb1555(u32 pix)
{
	return ((pix & 0x00f80000) >> 9) |
	       ((pix & 0x0000f800) >> 6) |
	       ((pix & 0x000000f8) >> 3);
}

static u16 convert_xrgb8888_to_argb1555(u32 pix)
{
	return BIT(15) | /* set alpha bit */
	       ((pix & 0x00f80000) >> 9) |
	       ((pix & 0x0000f800) >> 6) |
	       ((pix & 0x000000f8) >> 3);
}

static u32 convert_xrgb8888_to_argb8888(u32 pix)
{
	return pix | GENMASK(31, 24); /* fill alpha bits */
}

static u32 convert_xrgb8888_to_xbgr8888(u32 pix)
{
	return ((pix & 0x00ff0000) >> 16) <<  0 |
	       ((pix & 0x0000ff00) >>  8) <<  8 |
	       ((pix & 0x000000ff) >>  0) << 16 |
	       ((pix & 0xff000000) >> 24) << 24;
}

static u32 convert_xrgb8888_to_abgr8888(u32 pix)
{
	return ((pix & 0x00ff0000) >> 16) <<  0 |
	       ((pix & 0x0000ff00) >>  8) <<  8 |
	       ((pix & 0x000000ff) >>  0) << 16 |
	       GENMASK(31, 24); /* fill alpha bits */
}

static u32 convert_xrgb8888_to_xrgb2101010(u32 pix)
{
	pix = ((pix & 0x000000FF) << 2) |
	      ((pix & 0x0000FF00) << 4) |
	      ((pix & 0x00FF0000) << 6);
	return pix | ((pix >> 8) & 0x00300C03);
}

static u32 convert_xrgb8888_to_argb2101010(u32 pix)
{
	pix = ((pix & 0x000000FF) << 2) |
	      ((pix & 0x0000FF00) << 4) |
	      ((pix & 0x00FF0000) << 6);
	return GENMASK(31, 30) /* set alpha bits */ | pix | ((pix >> 8) & 0x00300C03);
}

/*
 * convert_from_xrgb8888 - convert one pixel from xrgb8888 to the desired format
 * @color: input color, in xrgb8888 format
 * @format: output format
 *
 * Returns:
 * Color in the format specified, casted to u32.
 * Or 0 if the format is not supported.
 */
static u32 convert_from_xrgb8888(u32 color, u32 format)
{
	switch (format) {
	case DRM_FORMAT_RGB565:
		return convert_xrgb8888_to_rgb565(color);
	case DRM_FORMAT_RGBA5551:
		return convert_xrgb8888_to_rgba5551(color);
	case DRM_FORMAT_XRGB1555:
		return convert_xrgb8888_to_xrgb1555(color);
	case DRM_FORMAT_ARGB1555:
		return convert_xrgb8888_to_argb1555(color);
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		return color;
	case DRM_FORMAT_ARGB8888:
		return convert_xrgb8888_to_argb8888(color);
	case DRM_FORMAT_XBGR8888:
		return convert_xrgb8888_to_xbgr8888(color);
	case DRM_FORMAT_ABGR8888:
		return convert_xrgb8888_to_abgr8888(color);
	case DRM_FORMAT_XRGB2101010:
		return convert_xrgb8888_to_xrgb2101010(color);
	case DRM_FORMAT_ARGB2101010:
		return convert_xrgb8888_to_argb2101010(color);
	default:
		WARN_ONCE(1, "Can't convert to %p4cc\n", &format);
		return 0;
	}
}

/*
 * Blit & Fill
 */
static void drm_panic_blit16(struct iosys_map *dmap, unsigned int dpitch,
			     const u8 *sbuf8, unsigned int spitch,
			     unsigned int height, unsigned int width,
			     u16 fg16, u16 bg16)
{
	unsigned int y, x;
	u16 val16;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			val16 = (sbuf8[(y * spitch) + x / 8] & (0x80 >> (x % 8))) ? fg16 : bg16;
			iosys_map_wr(dmap, y * dpitch + x * sizeof(u16), u16, val16);
		}
	}
}

static void drm_panic_blit24(struct iosys_map *dmap, unsigned int dpitch,
			     const u8 *sbuf8, unsigned int spitch,
			     unsigned int height, unsigned int width,
			     u32 fg32, u32 bg32)
{
	unsigned int y, x;
	u32 val32;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			u32 off = y * dpitch + x * 3;

			val32 = (sbuf8[(y * spitch) + x / 8] & (0x80 >> (x % 8))) ? fg32 : bg32;

			/* write blue-green-red to output in little endianness */
			iosys_map_wr(dmap, off, u8, (val32 & 0x000000FF) >> 0);
			iosys_map_wr(dmap, off + 1, u8, (val32 & 0x0000FF00) >> 8);
			iosys_map_wr(dmap, off + 2, u8, (val32 & 0x00FF0000) >> 16);
		}
	}
}

static void drm_panic_blit32(struct iosys_map *dmap, unsigned int dpitch,
			     const u8 *sbuf8, unsigned int spitch,
			     unsigned int height, unsigned int width,
			     u32 fg32, u32 bg32)
{
	unsigned int y, x;
	u32 val32;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			val32 = (sbuf8[(y * spitch) + x / 8] & (0x80 >> (x % 8))) ? fg32 : bg32;
			iosys_map_wr(dmap, y * dpitch + x * sizeof(u32), u32, val32);
		}
	}
}

/*
 * drm_panic_blit - convert a monochrome image to a linear framebuffer
 * @dmap: destination iosys_map
 * @dpitch: destination pitch in bytes
 * @sbuf8: source buffer, in monochrome format, 8 pixels per byte.
 * @spitch: source pitch in bytes
 * @height: height of the image to copy, in pixels
 * @width: width of the image to copy, in pixels
 * @fg_color: foreground color, in destination format
 * @bg_color: background color, in destination format
 * @pixel_width: pixel width in bytes.
 *
 * This can be used to draw a font character, which is a monochrome image, to a
 * framebuffer in other supported format.
 */
static void drm_panic_blit(struct iosys_map *dmap, unsigned int dpitch,
			   const u8 *sbuf8, unsigned int spitch,
			   unsigned int height, unsigned int width,
			   u32 fg_color, u32 bg_color,
			   unsigned int pixel_width)
{
	switch (pixel_width) {
	case 2:
		drm_panic_blit16(dmap, dpitch, sbuf8, spitch,
				 height, width, fg_color, bg_color);
	break;
	case 3:
		drm_panic_blit24(dmap, dpitch, sbuf8, spitch,
				 height, width, fg_color, bg_color);
	break;
	case 4:
		drm_panic_blit32(dmap, dpitch, sbuf8, spitch,
				 height, width, fg_color, bg_color);
	break;
	default:
		WARN_ONCE(1, "Can't blit with pixel width %d\n", pixel_width);
	}
}

static void drm_panic_fill16(struct iosys_map *dmap, unsigned int dpitch,
			     unsigned int height, unsigned int width,
			     u16 color)
{
	unsigned int y, x;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			iosys_map_wr(dmap, y * dpitch + x * sizeof(u16), u16, color);
}

static void drm_panic_fill24(struct iosys_map *dmap, unsigned int dpitch,
			     unsigned int height, unsigned int width,
			     u32 color)
{
	unsigned int y, x;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			unsigned int off = y * dpitch + x * 3;

			/* write blue-green-red to output in little endianness */
			iosys_map_wr(dmap, off, u8, (color & 0x000000FF) >> 0);
			iosys_map_wr(dmap, off + 1, u8, (color & 0x0000FF00) >> 8);
			iosys_map_wr(dmap, off + 2, u8, (color & 0x00FF0000) >> 16);
		}
	}
}

static void drm_panic_fill32(struct iosys_map *dmap, unsigned int dpitch,
			     unsigned int height, unsigned int width,
			     u32 color)
{
	unsigned int y, x;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			iosys_map_wr(dmap, y * dpitch + x * sizeof(u32), u32, color);
}

/*
 * drm_panic_fill - Fill a rectangle with a color
 * @dmap: destination iosys_map, pointing to the top left corner of the rectangle
 * @dpitch: destination pitch in bytes
 * @height: height of the rectangle, in pixels
 * @width: width of the rectangle, in pixels
 * @color: color to fill the rectangle.
 * @pixel_width: pixel width in bytes
 *
 * Fill a rectangle with a color, in a linear framebuffer.
 */
static void drm_panic_fill(struct iosys_map *dmap, unsigned int dpitch,
			   unsigned int height, unsigned int width,
			   u32 color, unsigned int pixel_width)
{
	switch (pixel_width) {
	case 2:
		drm_panic_fill16(dmap, dpitch, height, width, color);
	break;
	case 3:
		drm_panic_fill24(dmap, dpitch, height, width, color);
	break;
	case 4:
		drm_panic_fill32(dmap, dpitch, height, width, color);
	break;
	default:
		WARN_ONCE(1, "Can't fill with pixel width %d\n", pixel_width);
	}
}

static const u8 *get_char_bitmap(const struct font_desc *font, char c, size_t font_pitch)
{
	return font->data + (c * font->height) * font_pitch;
}

static unsigned int get_max_line_len(const struct drm_panic_line *lines, int len)
{
	int i;
	unsigned int max = 0;

	for (i = 0; i < len; i++)
		max = max(lines[i].len, max);
	return max;
}

/*
 * Draw a text in a rectangle on a framebuffer. The text is truncated if it overflows the rectangle
 */
static void draw_txt_rectangle(struct drm_scanout_buffer *sb,
			       const struct font_desc *font,
			       const struct drm_panic_line *msg,
			       unsigned int msg_lines,
			       bool centered,
			       struct drm_rect *clip,
			       u32 fg_color,
			       u32 bg_color)
{
	int i, j;
	const u8 *src;
	size_t font_pitch = DIV_ROUND_UP(font->width, 8);
	struct iosys_map dst;
	unsigned int px_width = sb->format->cpp[0];
	int left = 0;

	msg_lines = min(msg_lines,  drm_rect_height(clip) / font->height);
	for (i = 0; i < msg_lines; i++) {
		size_t line_len = min(msg[i].len, drm_rect_width(clip) / font->width);

		if (centered)
			left = (drm_rect_width(clip) - (line_len * font->width)) / 2;

		dst = sb->map[0];
		iosys_map_incr(&dst, (clip->y1 + i * font->height) * sb->pitch[0] +
				     (clip->x1 + left) * px_width);
		for (j = 0; j < line_len; j++) {
			src = get_char_bitmap(font, msg[i].txt[j], font_pitch);
			drm_panic_blit(&dst, sb->pitch[0], src, font_pitch,
				       font->height, font->width,
				       fg_color, bg_color, px_width);
			iosys_map_incr(&dst, font->width * px_width);
		}
	}
}

/*
 * Draw the panic message at the center of the screen
 */
static void draw_panic_static(struct drm_scanout_buffer *sb)
{
	size_t msg_lines = ARRAY_SIZE(panic_msg);
	size_t logo_lines = ARRAY_SIZE(logo);
	u32 fg_color = CONFIG_DRM_PANIC_FOREGROUND_COLOR;
	u32 bg_color = CONFIG_DRM_PANIC_BACKGROUND_COLOR;
	const struct font_desc *font = get_default_font(sb->width, sb->height, NULL, NULL);
	struct drm_rect r_logo, r_msg;

	if (!font)
		return;

	fg_color = convert_from_xrgb8888(fg_color, sb->format->format);
	bg_color = convert_from_xrgb8888(bg_color, sb->format->format);

	r_logo = DRM_RECT_INIT(0, 0,
			       get_max_line_len(logo, logo_lines) * font->width,
			       logo_lines * font->height);
	r_msg = DRM_RECT_INIT(0, 0,
			      min(get_max_line_len(panic_msg, msg_lines) * font->width, sb->width),
			      min(msg_lines * font->height, sb->height));

	/* Center the panic message */
	drm_rect_translate(&r_msg, (sb->width - r_msg.x2) / 2, (sb->height - r_msg.y2) / 2);

	/* Fill with the background color, and draw text on top */
	drm_panic_fill(&sb->map[0], sb->pitch[0], sb->height, sb->width,
		       bg_color, sb->format->cpp[0]);

	if ((r_msg.x1 >= drm_rect_width(&r_logo) || r_msg.y1 >= drm_rect_height(&r_logo)) &&
	    drm_rect_width(&r_logo) < sb->width && drm_rect_height(&r_logo) < sb->height) {
		draw_txt_rectangle(sb, font, logo, logo_lines, false, &r_logo, fg_color, bg_color);
	}
	draw_txt_rectangle(sb, font, panic_msg, msg_lines, true, &r_msg, fg_color, bg_color);
}

/*
 * drm_panic_is_format_supported()
 * @format: a fourcc color code
 * Returns: true if supported, false otherwise.
 *
 * Check if drm_panic will be able to use this color format.
 */
static bool drm_panic_is_format_supported(const struct drm_format_info *format)
{
	if (format->num_planes != 1)
		return false;
	return convert_from_xrgb8888(0xffffff, format->format) != 0;
}

static void draw_panic_plane(struct drm_plane *plane)
{
	struct drm_scanout_buffer sb;
	int ret;
	unsigned long flags;

	if (!drm_panic_trylock(plane->dev, flags))
		return;

	ret = plane->helper_private->get_scanout_buffer(plane, &sb);

	if (!ret && drm_panic_is_format_supported(sb.format)) {
		draw_panic_static(&sb);
		if (plane->helper_private->panic_flush)
			plane->helper_private->panic_flush(plane);
	}
	drm_panic_unlock(plane->dev, flags);
}

static struct drm_plane *to_drm_plane(struct kmsg_dumper *kd)
{
	return container_of(kd, struct drm_plane, kmsg_panic);
}

static void drm_panic(struct kmsg_dumper *dumper, enum kmsg_dump_reason reason)
{
	struct drm_plane *plane = to_drm_plane(dumper);

	if (reason == KMSG_DUMP_PANIC)
		draw_panic_plane(plane);
}


/*
 * DEBUG FS, This is currently unsafe.
 * Create one file per plane, so it's possible to debug one plane at a time.
 * TODO: It would be better to emulate an NMI context.
 */
#ifdef CONFIG_DRM_PANIC_DEBUG
#include <linux/debugfs.h>

static ssize_t debugfs_trigger_write(struct file *file, const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	bool run;

	if (kstrtobool_from_user(user_buf, count, &run) == 0 && run) {
		struct drm_plane *plane = file->private_data;

		draw_panic_plane(plane);
	}
	return count;
}

static const struct file_operations dbg_drm_panic_ops = {
	.owner = THIS_MODULE,
	.write = debugfs_trigger_write,
	.open = simple_open,
};

static void debugfs_register_plane(struct drm_plane *plane, int index)
{
	char fname[32];

	snprintf(fname, 32, "drm_panic_plane_%d", index);
	debugfs_create_file(fname, 0200, plane->dev->debugfs_root,
			    plane, &dbg_drm_panic_ops);
}
#else
static void debugfs_register_plane(struct drm_plane *plane, int index) {}
#endif /* CONFIG_DRM_PANIC_DEBUG */

/**
 * drm_panic_register() - Initialize DRM panic for a device
 * @dev: the drm device on which the panic screen will be displayed.
 */
void drm_panic_register(struct drm_device *dev)
{
	struct drm_plane *plane;
	int registered_plane = 0;

	if (!dev->mode_config.num_total_plane)
		return;

	drm_for_each_plane(plane, dev) {
		if (!plane->helper_private || !plane->helper_private->get_scanout_buffer)
			continue;
		plane->kmsg_panic.dump = drm_panic;
		plane->kmsg_panic.max_reason = KMSG_DUMP_PANIC;
		if (kmsg_dump_register(&plane->kmsg_panic))
			drm_warn(dev, "Failed to register panic handler\n");
		else {
			debugfs_register_plane(plane, registered_plane);
			registered_plane++;
		}
	}
	if (registered_plane)
		drm_info(dev, "Registered %d planes with drm panic\n", registered_plane);
}
EXPORT_SYMBOL(drm_panic_register);

/**
 * drm_panic_unregister()
 * @dev: the drm device previously registered.
 */
void drm_panic_unregister(struct drm_device *dev)
{
	struct drm_plane *plane;

	if (!dev->mode_config.num_total_plane)
		return;

	drm_for_each_plane(plane, dev) {
		if (!plane->helper_private || !plane->helper_private->get_scanout_buffer)
			continue;
		kmsg_dump_unregister(&plane->kmsg_panic);
	}
}
EXPORT_SYMBOL(drm_panic_unregister);
