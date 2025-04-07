// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright (c) 2023 Red Hat.
 * Author: Jocelyn Falempe <jfalempe@redhat.com>
 * inspired by the drm_log driver from David Herrmann <dh.herrmann@gmail.com>
 * Tux Ascii art taken from cowsay written by Tony Monroe
 */

#include <linux/font.h>
#include <linux/init.h>
#include <linux/iosys-map.h>
#include <linux/kdebug.h>
#include <linux/kmsg_dump.h>
#include <linux/linux_logo.h>
#include <linux/list.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/utsname.h>
#include <linux/zlib.h>

#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panic.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>

#include "drm_crtc_internal.h"
#include "drm_draw_internal.h"

MODULE_AUTHOR("Jocelyn Falempe");
MODULE_DESCRIPTION("DRM panic handler");
MODULE_LICENSE("GPL");

static char drm_panic_screen[16] = CONFIG_DRM_PANIC_SCREEN;
module_param_string(panic_screen, drm_panic_screen, sizeof(drm_panic_screen), 0644);
MODULE_PARM_DESC(panic_screen,
		 "Choose what will be displayed by drm_panic, 'user' or 'kmsg' [default="
		 CONFIG_DRM_PANIC_SCREEN "]");

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
	PANIC_LINE("KERNEL PANIC!"),
	PANIC_LINE(""),
	PANIC_LINE("Please reboot your computer."),
	PANIC_LINE(""),
	PANIC_LINE(""), /* will be replaced by the panic description */
};

static const size_t panic_msg_lines = ARRAY_SIZE(panic_msg);

static const struct drm_panic_line logo_ascii[] = {
	PANIC_LINE("     .--.        _"),
	PANIC_LINE("    |o_o |      | |"),
	PANIC_LINE("    |:_/ |      | |"),
	PANIC_LINE("   //   \\ \\     |_|"),
	PANIC_LINE("  (|     | )     _"),
	PANIC_LINE(" /'\\_   _/`\\    (_)"),
	PANIC_LINE(" \\___)=(___/"),
};

static const size_t logo_ascii_lines = ARRAY_SIZE(logo_ascii);

#if defined(CONFIG_LOGO) && !defined(MODULE)
static const struct linux_logo *logo_mono;

static int drm_panic_setup_logo(void)
{
	const struct linux_logo *logo = fb_find_logo(1);
	const unsigned char *logo_data;
	struct linux_logo *logo_dup;

	if (!logo || logo->type != LINUX_LOGO_MONO)
		return 0;

	/* The logo is __init, so we must make a copy for later use */
	logo_data = kmemdup(logo->data,
			    size_mul(DIV_ROUND_UP(logo->width, BITS_PER_BYTE), logo->height),
			    GFP_KERNEL);
	if (!logo_data)
		return -ENOMEM;

	logo_dup = kmemdup(logo, sizeof(*logo), GFP_KERNEL);
	if (!logo_dup) {
		kfree(logo_data);
		return -ENOMEM;
	}

	logo_dup->data = logo_data;
	logo_mono = logo_dup;

	return 0;
}

device_initcall(drm_panic_setup_logo);
#else
#define logo_mono	((const struct linux_logo *)NULL)
#endif

/*
 *  Blit & Fill functions
 */
static void drm_panic_blit_pixel(struct drm_scanout_buffer *sb, struct drm_rect *clip,
				 const u8 *sbuf8, unsigned int spitch, unsigned int scale,
				 u32 fg_color)
{
	unsigned int y, x;

	for (y = 0; y < drm_rect_height(clip); y++)
		for (x = 0; x < drm_rect_width(clip); x++)
			if (drm_draw_is_pixel_fg(sbuf8, spitch, x / scale, y / scale))
				sb->set_pixel(sb, clip->x1 + x, clip->y1 + y, fg_color);
}

/*
 * drm_panic_blit - convert a monochrome image to a linear framebuffer
 * @sb: destination scanout buffer
 * @clip: destination rectangle
 * @sbuf8: source buffer, in monochrome format, 8 pixels per byte.
 * @spitch: source pitch in bytes
 * @scale: integer scale, source buffer is scale time smaller than destination
 *         rectangle
 * @fg_color: foreground color, in destination format
 *
 * This can be used to draw a font character, which is a monochrome image, to a
 * framebuffer in other supported format.
 */
static void drm_panic_blit(struct drm_scanout_buffer *sb, struct drm_rect *clip,
			   const u8 *sbuf8, unsigned int spitch,
			   unsigned int scale, u32 fg_color)

{
	struct iosys_map map;

	if (sb->set_pixel)
		return drm_panic_blit_pixel(sb, clip, sbuf8, spitch, scale, fg_color);

	map = sb->map[0];
	iosys_map_incr(&map, clip->y1 * sb->pitch[0] + clip->x1 * sb->format->cpp[0]);

	switch (sb->format->cpp[0]) {
	case 2:
		drm_draw_blit16(&map, sb->pitch[0], sbuf8, spitch,
				drm_rect_height(clip), drm_rect_width(clip), scale, fg_color);
	break;
	case 3:
		drm_draw_blit24(&map, sb->pitch[0], sbuf8, spitch,
				drm_rect_height(clip), drm_rect_width(clip), scale, fg_color);
	break;
	case 4:
		drm_draw_blit32(&map, sb->pitch[0], sbuf8, spitch,
				drm_rect_height(clip), drm_rect_width(clip), scale, fg_color);
	break;
	default:
		WARN_ONCE(1, "Can't blit with pixel width %d\n", sb->format->cpp[0]);
	}
}

static void drm_panic_fill_pixel(struct drm_scanout_buffer *sb,
				 struct drm_rect *clip,
				 u32 color)
{
	unsigned int y, x;

	for (y = 0; y < drm_rect_height(clip); y++)
		for (x = 0; x < drm_rect_width(clip); x++)
			sb->set_pixel(sb, clip->x1 + x, clip->y1 + y, color);
}

/*
 * drm_panic_fill - Fill a rectangle with a color
 * @sb: destination scanout buffer
 * @clip: destination rectangle
 * @color: foreground color, in destination format
 *
 * Fill a rectangle with a color, in a linear framebuffer.
 */
static void drm_panic_fill(struct drm_scanout_buffer *sb, struct drm_rect *clip,
			   u32 color)
{
	struct iosys_map map;

	if (sb->set_pixel)
		return drm_panic_fill_pixel(sb, clip, color);

	map = sb->map[0];
	iosys_map_incr(&map, clip->y1 * sb->pitch[0] + clip->x1 * sb->format->cpp[0]);

	switch (sb->format->cpp[0]) {
	case 2:
		drm_draw_fill16(&map, sb->pitch[0], drm_rect_height(clip),
				drm_rect_width(clip), color);
	break;
	case 3:
		drm_draw_fill24(&map, sb->pitch[0], drm_rect_height(clip),
				drm_rect_width(clip), color);
	break;
	case 4:
		drm_draw_fill32(&map, sb->pitch[0], drm_rect_height(clip),
				drm_rect_width(clip), color);
	break;
	default:
		WARN_ONCE(1, "Can't fill with pixel width %d\n", sb->format->cpp[0]);
	}
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
			       u32 color)
{
	int i, j;
	const u8 *src;
	size_t font_pitch = DIV_ROUND_UP(font->width, 8);
	struct drm_rect rec;

	msg_lines = min(msg_lines,  drm_rect_height(clip) / font->height);
	for (i = 0; i < msg_lines; i++) {
		size_t line_len = min(msg[i].len, drm_rect_width(clip) / font->width);

		rec.y1 = clip->y1 +  i * font->height;
		rec.y2 = rec.y1 + font->height;
		rec.x1 = clip->x1;

		if (centered)
			rec.x1 += (drm_rect_width(clip) - (line_len * font->width)) / 2;

		for (j = 0; j < line_len; j++) {
			src = drm_draw_get_char_bitmap(font, msg[i].txt[j], font_pitch);
			rec.x2 = rec.x1 + font->width;
			drm_panic_blit(sb, &rec, src, font_pitch, 1, color);
			rec.x1 += font->width;
		}
	}
}

static void drm_panic_logo_rect(struct drm_rect *rect, const struct font_desc *font)
{
	if (logo_mono) {
		drm_rect_init(rect, 0, 0, logo_mono->width, logo_mono->height);
	} else {
		int logo_width = get_max_line_len(logo_ascii, logo_ascii_lines) * font->width;

		drm_rect_init(rect, 0, 0, logo_width, logo_ascii_lines * font->height);
	}
}

static void drm_panic_logo_draw(struct drm_scanout_buffer *sb, struct drm_rect *rect,
				const struct font_desc *font, u32 fg_color)
{
	if (logo_mono)
		drm_panic_blit(sb, rect, logo_mono->data,
			       DIV_ROUND_UP(drm_rect_width(rect), 8), 1, fg_color);
	else
		draw_txt_rectangle(sb, font, logo_ascii, logo_ascii_lines, false, rect,
				   fg_color);
}

static void draw_panic_static_user(struct drm_scanout_buffer *sb)
{
	u32 fg_color = drm_draw_color_from_xrgb8888(CONFIG_DRM_PANIC_FOREGROUND_COLOR,
						    sb->format->format);
	u32 bg_color = drm_draw_color_from_xrgb8888(CONFIG_DRM_PANIC_BACKGROUND_COLOR,
						    sb->format->format);
	const struct font_desc *font = get_default_font(sb->width, sb->height, NULL, NULL);
	struct drm_rect r_screen, r_logo, r_msg;
	unsigned int msg_width, msg_height;

	if (!font)
		return;

	r_screen = DRM_RECT_INIT(0, 0, sb->width, sb->height);
	drm_panic_logo_rect(&r_logo, font);

	msg_width = min(get_max_line_len(panic_msg, panic_msg_lines) * font->width, sb->width);
	msg_height = min(panic_msg_lines * font->height, sb->height);
	r_msg = DRM_RECT_INIT(0, 0, msg_width, msg_height);

	/* Center the panic message */
	drm_rect_translate(&r_msg, (sb->width - r_msg.x2) / 2, (sb->height - r_msg.y2) / 2);

	/* Fill with the background color, and draw text on top */
	drm_panic_fill(sb, &r_screen, bg_color);

	if (!drm_rect_overlap(&r_logo, &r_msg))
		drm_panic_logo_draw(sb, &r_logo, font, fg_color);

	draw_txt_rectangle(sb, font, panic_msg, panic_msg_lines, true, &r_msg, fg_color);
}

/*
 * Draw one line of kmsg, and handle wrapping if it won't fit in the screen width.
 * Return the y-offset of the next line.
 */
static int draw_line_with_wrap(struct drm_scanout_buffer *sb, const struct font_desc *font,
			       struct drm_panic_line *line, int yoffset, u32 fg_color)
{
	int chars_per_row = sb->width / font->width;
	struct drm_rect r_txt = DRM_RECT_INIT(0, yoffset, sb->width, sb->height);
	struct drm_panic_line line_wrap;

	if (line->len > chars_per_row) {
		line_wrap.len = line->len % chars_per_row;
		line_wrap.txt = line->txt + line->len - line_wrap.len;
		draw_txt_rectangle(sb, font, &line_wrap, 1, false, &r_txt, fg_color);
		r_txt.y1 -= font->height;
		if (r_txt.y1 < 0)
			return r_txt.y1;
		while (line_wrap.txt > line->txt) {
			line_wrap.txt -= chars_per_row;
			line_wrap.len = chars_per_row;
			draw_txt_rectangle(sb, font, &line_wrap, 1, false, &r_txt, fg_color);
			r_txt.y1 -= font->height;
			if (r_txt.y1 < 0)
				return r_txt.y1;
		}
	} else {
		draw_txt_rectangle(sb, font, line, 1, false, &r_txt, fg_color);
		r_txt.y1 -= font->height;
	}
	return r_txt.y1;
}

/*
 * Draw the kmsg buffer to the screen, starting from the youngest message at the bottom,
 * and going up until reaching the top of the screen.
 */
static void draw_panic_static_kmsg(struct drm_scanout_buffer *sb)
{
	u32 fg_color = drm_draw_color_from_xrgb8888(CONFIG_DRM_PANIC_FOREGROUND_COLOR,
						    sb->format->format);
	u32 bg_color = drm_draw_color_from_xrgb8888(CONFIG_DRM_PANIC_BACKGROUND_COLOR,
						    sb->format->format);
	const struct font_desc *font = get_default_font(sb->width, sb->height, NULL, NULL);
	struct drm_rect r_screen = DRM_RECT_INIT(0, 0, sb->width, sb->height);
	struct kmsg_dump_iter iter;
	char kmsg_buf[512];
	size_t kmsg_len;
	struct drm_panic_line line;
	int yoffset;

	if (!font)
		return;

	yoffset = sb->height - font->height - (sb->height % font->height) / 2;

	/* Fill with the background color, and draw text on top */
	drm_panic_fill(sb, &r_screen, bg_color);

	kmsg_dump_rewind(&iter);
	while (kmsg_dump_get_buffer(&iter, false, kmsg_buf, sizeof(kmsg_buf), &kmsg_len)) {
		char *start;
		char *end;

		/* ignore terminating NUL and newline */
		start = kmsg_buf + kmsg_len - 2;
		end = kmsg_buf + kmsg_len - 1;
		while (start > kmsg_buf && yoffset >= 0) {
			while (start > kmsg_buf && *start != '\n')
				start--;
			/* don't count the newline character */
			line.txt = start + (start == kmsg_buf ? 0 : 1);
			line.len = end - line.txt;

			yoffset = draw_line_with_wrap(sb, font, &line, yoffset, fg_color);
			end = start;
			start--;
		}
	}
}

#if defined(CONFIG_DRM_PANIC_SCREEN_QR_CODE)
/*
 * It is unwise to allocate memory in the panic callback, so the buffers are
 * pre-allocated. Only 2 buffers and the zlib workspace are needed.
 * Two buffers are enough, using the following buffer usage:
 * 1) kmsg messages are dumped in buffer1
 * 2) kmsg is zlib-compressed into buffer2
 * 3) compressed kmsg is encoded as QR-code Numeric stream in buffer1
 * 4) QR-code image is generated in buffer2
 * The Max QR code size is V40, 177x177, 4071 bytes for image, 2956 bytes for
 * data segments.
 *
 * Typically, ~7500 bytes of kmsg, are compressed into 2800 bytes, which fits in
 * a V40 QR-code (177x177).
 *
 * If CONFIG_DRM_PANIC_SCREEN_QR_CODE_URL is not set, the kmsg data will be put
 * directly in the QR code.
 * 1) kmsg messages are dumped in buffer1
 * 2) kmsg message is encoded as byte stream in buffer2
 * 3) QR-code image is generated in buffer1
 */

static uint panic_qr_version = CONFIG_DRM_PANIC_SCREEN_QR_VERSION;
module_param(panic_qr_version, uint, 0644);
MODULE_PARM_DESC(panic_qr_version, "maximum version (size) of the QR code");

#define MAX_QR_DATA 2956
#define MAX_ZLIB_RATIO 3
#define QR_BUFFER1_SIZE (MAX_ZLIB_RATIO * MAX_QR_DATA) /* Must also be > 4071  */
#define QR_BUFFER2_SIZE 4096
#define QR_MARGIN	4	/* 4 modules of foreground color around the qr code */

/* Compression parameters */
#define COMPR_LEVEL 6
#define WINDOW_BITS 12
#define MEM_LEVEL 4

static char *qrbuf1;
static char *qrbuf2;
static struct z_stream_s stream;

static void __init drm_panic_qr_init(void)
{
	qrbuf1 = kmalloc(QR_BUFFER1_SIZE, GFP_KERNEL);
	qrbuf2 = kmalloc(QR_BUFFER2_SIZE, GFP_KERNEL);
	stream.workspace = kmalloc(zlib_deflate_workspacesize(WINDOW_BITS, MEM_LEVEL),
				   GFP_KERNEL);
}

static void drm_panic_qr_exit(void)
{
	kfree(qrbuf1);
	qrbuf1 = NULL;
	kfree(qrbuf2);
	qrbuf2 = NULL;
	kfree(stream.workspace);
	stream.workspace = NULL;
}

static int drm_panic_get_qr_code_url(u8 **qr_image)
{
	struct kmsg_dump_iter iter;
	char url[256];
	size_t kmsg_len, max_kmsg_size;
	char *kmsg;
	int max_qr_data_size, url_len;

	url_len = snprintf(url, sizeof(url), CONFIG_DRM_PANIC_SCREEN_QR_CODE_URL "?a=%s&v=%s&z=",
			   utsname()->machine, utsname()->release);

	max_qr_data_size = drm_panic_qr_max_data_size(panic_qr_version, url_len);
	max_kmsg_size = min(MAX_ZLIB_RATIO * max_qr_data_size, QR_BUFFER1_SIZE);

	/* get kmsg to buffer 1 */
	kmsg_dump_rewind(&iter);
	kmsg_dump_get_buffer(&iter, false, qrbuf1, max_kmsg_size, &kmsg_len);

	if (!kmsg_len)
		return -ENODATA;
	kmsg = qrbuf1;

try_again:
	if (zlib_deflateInit2(&stream, COMPR_LEVEL, Z_DEFLATED, WINDOW_BITS,
			      MEM_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK)
		return -EINVAL;

	stream.next_in = kmsg;
	stream.avail_in = kmsg_len;
	stream.total_in = 0;
	stream.next_out = qrbuf2;
	stream.avail_out = QR_BUFFER2_SIZE;
	stream.total_out = 0;

	if (zlib_deflate(&stream, Z_FINISH) != Z_STREAM_END)
		return -EINVAL;

	if (zlib_deflateEnd(&stream) != Z_OK)
		return -EINVAL;

	if (stream.total_out > max_qr_data_size) {
		/* too much data for the QR code, so skip the first line and try again */
		kmsg = strchr(kmsg, '\n');
		if (!kmsg)
			return -EINVAL;
		/* skip the first \n */
		kmsg += 1;
		kmsg_len = strlen(kmsg);
		goto try_again;
	}
	*qr_image = qrbuf2;

	/* generate qr code image in buffer2 */
	return drm_panic_qr_generate(url, qrbuf2, stream.total_out, QR_BUFFER2_SIZE,
				     qrbuf1, QR_BUFFER1_SIZE);
}

static int drm_panic_get_qr_code_raw(u8 **qr_image)
{
	struct kmsg_dump_iter iter;
	size_t kmsg_len;
	size_t max_kmsg_size = min(drm_panic_qr_max_data_size(panic_qr_version, 0),
				   QR_BUFFER1_SIZE);

	kmsg_dump_rewind(&iter);
	kmsg_dump_get_buffer(&iter, false, qrbuf1, max_kmsg_size, &kmsg_len);
	if (!kmsg_len)
		return -ENODATA;

	*qr_image = qrbuf1;
	return drm_panic_qr_generate(NULL, qrbuf1, kmsg_len, QR_BUFFER1_SIZE,
				     qrbuf2, QR_BUFFER2_SIZE);
}

static int drm_panic_get_qr_code(u8 **qr_image)
{
	if (strlen(CONFIG_DRM_PANIC_SCREEN_QR_CODE_URL) > 0)
		return drm_panic_get_qr_code_url(qr_image);
	else
		return drm_panic_get_qr_code_raw(qr_image);
}

/*
 * Draw the panic message at the center of the screen, with a QR Code
 */
static int _draw_panic_static_qr_code(struct drm_scanout_buffer *sb)
{
	u32 fg_color = drm_draw_color_from_xrgb8888(CONFIG_DRM_PANIC_FOREGROUND_COLOR,
						    sb->format->format);
	u32 bg_color = drm_draw_color_from_xrgb8888(CONFIG_DRM_PANIC_BACKGROUND_COLOR,
						    sb->format->format);
	const struct font_desc *font = get_default_font(sb->width, sb->height, NULL, NULL);
	struct drm_rect r_screen, r_logo, r_msg, r_qr, r_qr_canvas;
	unsigned int max_qr_size, scale;
	unsigned int msg_width, msg_height;
	int qr_width, qr_canvas_width, qr_pitch, v_margin;
	u8 *qr_image;

	if (!font || !qrbuf1 || !qrbuf2 || !stream.workspace)
		return -ENOMEM;

	r_screen = DRM_RECT_INIT(0, 0, sb->width, sb->height);

	drm_panic_logo_rect(&r_logo, font);

	msg_width = min(get_max_line_len(panic_msg, panic_msg_lines) * font->width, sb->width);
	msg_height = min(panic_msg_lines * font->height, sb->height);
	r_msg = DRM_RECT_INIT(0, 0, msg_width, msg_height);

	max_qr_size = min(3 * sb->width / 4, 3 * sb->height / 4);

	qr_width = drm_panic_get_qr_code(&qr_image);
	if (qr_width <= 0)
		return -ENOSPC;

	qr_canvas_width = qr_width + QR_MARGIN * 2;
	scale = max_qr_size / qr_canvas_width;
	/* QR code is not readable if not scaled at least by 2 */
	if (scale < 2)
		return -ENOSPC;

	pr_debug("QR width %d and scale %d\n", qr_width, scale);
	r_qr_canvas = DRM_RECT_INIT(0, 0, qr_canvas_width * scale, qr_canvas_width * scale);

	v_margin = (sb->height - drm_rect_height(&r_qr_canvas) - drm_rect_height(&r_msg)) / 5;

	drm_rect_translate(&r_qr_canvas, (sb->width - r_qr_canvas.x2) / 2, 2 * v_margin);
	r_qr = DRM_RECT_INIT(r_qr_canvas.x1 + QR_MARGIN * scale, r_qr_canvas.y1 + QR_MARGIN * scale,
			     qr_width * scale, qr_width * scale);

	/* Center the panic message */
	drm_rect_translate(&r_msg, (sb->width - r_msg.x2) / 2,
			   3 * v_margin + drm_rect_height(&r_qr_canvas));

	/* Fill with the background color, and draw text on top */
	drm_panic_fill(sb, &r_screen, bg_color);

	if (!drm_rect_overlap(&r_logo, &r_msg) && !drm_rect_overlap(&r_logo, &r_qr))
		drm_panic_logo_draw(sb, &r_logo, font, fg_color);

	draw_txt_rectangle(sb, font, panic_msg, panic_msg_lines, true, &r_msg, fg_color);

	/* Draw the qr code */
	qr_pitch = DIV_ROUND_UP(qr_width, 8);
	drm_panic_fill(sb, &r_qr_canvas, fg_color);
	drm_panic_fill(sb, &r_qr, bg_color);
	drm_panic_blit(sb, &r_qr, qr_image, qr_pitch, scale, fg_color);
	return 0;
}

static void draw_panic_static_qr_code(struct drm_scanout_buffer *sb)
{
	if (_draw_panic_static_qr_code(sb))
		draw_panic_static_user(sb);
}
#else
static void draw_panic_static_qr_code(struct drm_scanout_buffer *sb)
{
	draw_panic_static_user(sb);
}

static void drm_panic_qr_init(void) {};
static void drm_panic_qr_exit(void) {};
#endif

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
	return drm_draw_color_from_xrgb8888(0xffffff, format->format) != 0;
}

static void draw_panic_dispatch(struct drm_scanout_buffer *sb)
{
	if (!strcmp(drm_panic_screen, "kmsg")) {
		draw_panic_static_kmsg(sb);
	} else if (!strcmp(drm_panic_screen, "qr_code")) {
		draw_panic_static_qr_code(sb);
	} else {
		draw_panic_static_user(sb);
	}
}

static void drm_panic_set_description(const char *description)
{
	u32 len;

	if (description) {
		struct drm_panic_line *desc_line = &panic_msg[panic_msg_lines - 1];

		desc_line->txt = description;
		len = strlen(description);
		/* ignore the last newline character */
		if (len && description[len - 1] == '\n')
			len -= 1;
		desc_line->len = len;
	}
}

static void drm_panic_clear_description(void)
{
	struct drm_panic_line *desc_line = &panic_msg[panic_msg_lines - 1];

	desc_line->len = 0;
	desc_line->txt = NULL;
}

static void draw_panic_plane(struct drm_plane *plane, const char *description)
{
	struct drm_scanout_buffer sb = { };
	int ret;
	unsigned long flags;

	if (!drm_panic_trylock(plane->dev, flags))
		return;

	drm_panic_set_description(description);

	ret = plane->helper_private->get_scanout_buffer(plane, &sb);

	if (!ret && drm_panic_is_format_supported(sb.format)) {
		draw_panic_dispatch(&sb);
		if (plane->helper_private->panic_flush)
			plane->helper_private->panic_flush(plane);
	}
	drm_panic_clear_description();
	drm_panic_unlock(plane->dev, flags);
}

static struct drm_plane *to_drm_plane(struct kmsg_dumper *kd)
{
	return container_of(kd, struct drm_plane, kmsg_panic);
}

static void drm_panic(struct kmsg_dumper *dumper, struct kmsg_dump_detail *detail)
{
	struct drm_plane *plane = to_drm_plane(dumper);

	if (detail->reason == KMSG_DUMP_PANIC)
		draw_panic_plane(plane, detail->description);
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

		draw_panic_plane(plane, "Test from debugfs");
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
 * drm_panic_is_enabled
 * @dev: the drm device that may supports drm_panic
 *
 * returns true if the drm device supports drm_panic
 */
bool drm_panic_is_enabled(struct drm_device *dev)
{
	struct drm_plane *plane;

	if (!dev->mode_config.num_total_plane)
		return false;

	drm_for_each_plane(plane, dev)
		if (plane->helper_private && plane->helper_private->get_scanout_buffer)
			return true;
	return false;
}
EXPORT_SYMBOL(drm_panic_is_enabled);

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

/**
 * drm_panic_init() - initialize DRM panic.
 */
void __init drm_panic_init(void)
{
	drm_panic_qr_init();
}

/**
 * drm_panic_exit() - Free the resources taken by drm_panic_exit()
 */
void drm_panic_exit(void)
{
	drm_panic_qr_exit();
}
