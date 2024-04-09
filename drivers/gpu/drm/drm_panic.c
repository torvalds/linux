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

static void drm_panic_fill32(struct iosys_map *map, unsigned int pitch,
			     unsigned int height, unsigned int width,
			     u32 color)
{
	unsigned int y, x;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			iosys_map_wr(map, y * pitch + x * sizeof(u32), u32, color);
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
			drm_panic_blit32(&dst, sb->pitch[0], src, font_pitch,
					 font->height, font->width,
					 fg_color, bg_color);
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

	r_logo = DRM_RECT_INIT(0, 0,
			       get_max_line_len(logo, logo_lines) * font->width,
			       logo_lines * font->height);
	r_msg = DRM_RECT_INIT(0, 0,
			      min(get_max_line_len(panic_msg, msg_lines) * font->width, sb->width),
			      min(msg_lines * font->height, sb->height));

	/* Center the panic message */
	drm_rect_translate(&r_msg, (sb->width - r_msg.x2) / 2, (sb->height - r_msg.y2) / 2);

	/* Fill with the background color, and draw text on top */
	drm_panic_fill32(&sb->map[0], sb->pitch[0], sb->height, sb->width, bg_color);

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
	return format->format == DRM_FORMAT_XRGB8888;
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
		else
			registered_plane++;
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
