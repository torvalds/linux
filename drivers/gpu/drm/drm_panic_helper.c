// SPDX-License-Identifier: GPL-2.0 or MIT

#include <linux/export.h>
#include <linux/font.h>
#include <linux/highmem.h>
#include <linux/iosys-map.h>
#include <linux/linux_logo.h>
#include <linux/utsname.h>
#include <linux/zlib.h>

#include <kunit/visibility.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panic.h>
#include <drm/drm_panic_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_rect.h>

#include "drm_draw_internal.h"
#include "drm_panic_internal.h"

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

static int __init drm_panic_helper_setup_logo(void)
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
#else
#define logo_mono	((const struct linux_logo *)NULL)
static int __init drm_panic_helper_setup_logo(void)
{
	return 0;
}
#endif

/*
 *  Blit & Fill functions
 */
static void drm_panic_helper_blit_pixel(struct drm_scanout_buffer *sb, struct drm_rect *clip,
					const u8 *sbuf8, unsigned int spitch, unsigned int scale,
					u32 fg_color)
{
	unsigned int y, x;

	for (y = 0; y < drm_rect_height(clip); y++)
		for (x = 0; x < drm_rect_width(clip); x++)
			if (drm_draw_is_pixel_fg(sbuf8, spitch, x / scale, y / scale))
				sb->set_pixel(sb, clip->x1 + x, clip->y1 + y, fg_color);
}

static void drm_panic_helper_write_pixel16(void *vaddr, unsigned int offset, u16 color)
{
	u16 *p = vaddr + offset;

	*p = color;
}

static void drm_panic_helper_write_pixel24(void *vaddr, unsigned int offset, u32 color)
{
	u8 *p = vaddr + offset;

	*p++ = color & 0xff;
	color >>= 8;
	*p++ = color & 0xff;
	color >>= 8;
	*p = color & 0xff;
}

/*
 * Special case if the pixel crosses page boundaries
 */
static void drm_panic_helper_write_pixel24_xpage(void *vaddr, struct page *next_page,
						 unsigned int offset, u32 color)
{
	u8 *vaddr2;
	u8 *p = vaddr + offset;

	vaddr2 = kmap_local_page_try_from_panic(next_page);
	if (!vaddr2)
		return;

	*p++ = color & 0xff;
	color >>= 8;

	if (offset == PAGE_SIZE - 1)
		p = vaddr2;

	*p++ = color & 0xff;
	color >>= 8;

	if (offset == PAGE_SIZE - 2)
		p = vaddr2;

	*p = color & 0xff;
	kunmap_local(vaddr2);
}

static void drm_panic_helper_write_pixel32(void *vaddr, unsigned int offset, u32 color)
{
	u32 *p = vaddr + offset;

	*p = color;
}

static void drm_panic_helper_write_pixel(void *vaddr, unsigned int offset, u32 color,
					 unsigned int cpp)
{
	switch (cpp) {
	case 2:
		drm_panic_helper_write_pixel16(vaddr, offset, color);
		break;
	case 3:
		drm_panic_helper_write_pixel24(vaddr, offset, color);
		break;
	case 4:
		drm_panic_helper_write_pixel32(vaddr, offset, color);
		break;
	default:
		pr_debug_once("Can't blit with pixel width %d\n", cpp);
	}
}

/*
 * The scanout buffer pages are not mapped, so for each pixel,
 * use kmap_local_page_try_from_panic() to map the page, and write the pixel.
 * Try to keep the map from the previous pixel, to avoid too much map/unmap.
 */
static void drm_panic_helper_blit_page(struct page **pages, unsigned int dpitch,
				       unsigned int cpp, const u8 *sbuf8,
				       unsigned int spitch, struct drm_rect *clip,
				       unsigned int scale, u32 fg32)
{
	unsigned int y, x;
	unsigned int page = ~0;
	unsigned int height = drm_rect_height(clip);
	unsigned int width = drm_rect_width(clip);
	void *vaddr = NULL;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			if (drm_draw_is_pixel_fg(sbuf8, spitch, x / scale, y / scale)) {
				unsigned int new_page;
				unsigned int offset;

				offset = (y + clip->y1) * dpitch + (x + clip->x1) * cpp;
				new_page = offset >> PAGE_SHIFT;
				offset = offset % PAGE_SIZE;
				if (new_page != page) {
					if (!pages[new_page])
						continue;
					if (vaddr)
						kunmap_local(vaddr);
					page = new_page;
					vaddr = kmap_local_page_try_from_panic(pages[page]);
				}
				if (!vaddr)
					continue;

				// Special case for 24bit, as a pixel might cross page boundaries
				if (cpp == 3 && offset + 3 > PAGE_SIZE)
					drm_panic_helper_write_pixel24_xpage(vaddr,
									     pages[page + 1],
									     offset, fg32);
				else
					drm_panic_helper_write_pixel(vaddr, offset, fg32, cpp);
			}
		}
	}
	if (vaddr)
		kunmap_local(vaddr);
}

/*
 * drm_panic_helper_blit - convert a monochrome image to a linear framebuffer
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
static void drm_panic_helper_blit(struct drm_scanout_buffer *sb, struct drm_rect *clip,
				  const u8 *sbuf8, unsigned int spitch,
				  unsigned int scale, u32 fg_color)

{
	struct iosys_map map;

	if (sb->set_pixel)
		return drm_panic_helper_blit_pixel(sb, clip, sbuf8, spitch, scale, fg_color);

	if (sb->pages)
		return drm_panic_helper_blit_page(sb->pages, sb->pitch[0], sb->format->cpp[0],
						  sbuf8, spitch, clip, scale, fg_color);

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

static void drm_panic_helper_fill_pixel(struct drm_scanout_buffer *sb,
					struct drm_rect *clip,
					u32 color)
{
	unsigned int y, x;

	for (y = 0; y < drm_rect_height(clip); y++)
		for (x = 0; x < drm_rect_width(clip); x++)
			sb->set_pixel(sb, clip->x1 + x, clip->y1 + y, color);
}

static void drm_panic_helper_fill_page(struct page **pages, unsigned int dpitch,
				       unsigned int cpp, struct drm_rect *clip,
				       u32 color)
{
	unsigned int y, x;
	unsigned int page = ~0;
	void *vaddr = NULL;

	for (y = clip->y1; y < clip->y2; y++) {
		for (x = clip->x1; x < clip->x2; x++) {
			unsigned int new_page;
			unsigned int offset;

			offset = y * dpitch + x * cpp;
			new_page = offset >> PAGE_SHIFT;
			offset = offset % PAGE_SIZE;
			if (new_page != page) {
				if (vaddr)
					kunmap_local(vaddr);
				page = new_page;
				vaddr = kmap_local_page_try_from_panic(pages[page]);
			}
			if (!vaddr)
				continue;

			// Special case for 24bit, as a pixel might cross page boundaries
			if (cpp == 3 && offset + 3 > PAGE_SIZE)
				drm_panic_helper_write_pixel24_xpage(vaddr, pages[page + 1],
								     offset, color);
			else
				drm_panic_helper_write_pixel(vaddr, offset, color, cpp);
		}
	}
	if (vaddr)
		kunmap_local(vaddr);
}

/*
 * drm_panic_helper_fill - Fill a rectangle with a color
 * @sb: destination scanout buffer
 * @clip: destination rectangle
 * @color: foreground color, in destination format
 *
 * Fill a rectangle with a color, in a linear framebuffer.
 */
static void drm_panic_helper_fill(struct drm_scanout_buffer *sb, struct drm_rect *clip,
				  u32 color)
{
	struct iosys_map map;

	if (sb->set_pixel)
		return drm_panic_helper_fill_pixel(sb, clip, color);

	if (sb->pages)
		return drm_panic_helper_fill_page(sb->pages, sb->pitch[0], sb->format->cpp[0],
						  clip, color);

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
			src = font_data_glyph_buf(font->data, font->width, font->height,
						  (unsigned char)msg[i].txt[j]);
			rec.x2 = rec.x1 + font->width;
			if (src)
				drm_panic_helper_blit(sb, &rec, src, font_pitch, 1, color);
			rec.x1 += font->width;
		}
	}
}

static void drm_panic_helper_logo_rect(struct drm_rect *rect, const struct font_desc *font)
{
	if (logo_mono) {
		drm_rect_init(rect, 0, 0, logo_mono->width, logo_mono->height);
	} else {
		int logo_width = get_max_line_len(logo_ascii, logo_ascii_lines) * font->width;

		drm_rect_init(rect, 0, 0, logo_width, logo_ascii_lines * font->height);
	}
}

static void drm_panic_helper_logo_draw(struct drm_scanout_buffer *sb, struct drm_rect *rect,
				       const struct font_desc *font, u32 fg_color)
{
	if (rect->x2 > sb->width || rect->y2 > sb->height)
		return;

	if (logo_mono)
		drm_panic_helper_blit(sb, rect, logo_mono->data,
				      DIV_ROUND_UP(drm_rect_width(rect), 8), 1, fg_color);
	else
		draw_txt_rectangle(sb, font, logo_ascii, logo_ascii_lines, false, rect,
				   fg_color);
}

VISIBLE_IF_KUNIT int drm_panic_helper_draw_screen_user(struct drm_scanout_buffer *sb,
						       u32 fg_color, u32 bg_color)
{
	const struct font_desc *font = get_default_font(sb->width, sb->height, NULL, NULL);
	struct drm_rect r_screen, r_logo, r_msg;
	unsigned int msg_width, msg_height;

	if (!font || font->width > sb->width || font->height > sb->height)
		return -EINVAL;

	fg_color = drm_draw_color_from_xrgb8888(fg_color, sb->format->format);
	bg_color = drm_draw_color_from_xrgb8888(bg_color, sb->format->format);

	r_screen = DRM_RECT_INIT(0, 0, sb->width, sb->height);
	drm_panic_helper_logo_rect(&r_logo, font);

	msg_width = min(get_max_line_len(panic_msg, panic_msg_lines) * font->width, sb->width);
	msg_height = min(panic_msg_lines * font->height, sb->height);
	r_msg = DRM_RECT_INIT(0, 0, msg_width, msg_height);

	/* Center the panic message */
	drm_rect_translate(&r_msg, (sb->width - r_msg.x2) / 2, (sb->height - r_msg.y2) / 2);

	/* Fill with the background color, and draw text on top */
	drm_panic_helper_fill(sb, &r_screen, bg_color);

	if (!drm_rect_overlap(&r_logo, &r_msg))
		drm_panic_helper_logo_draw(sb, &r_logo, font, fg_color);

	draw_txt_rectangle(sb, font, panic_msg, panic_msg_lines, true, &r_msg, fg_color);

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(drm_panic_helper_draw_screen_user);

/*
 * Draw one line of kmsg, and handle wrapping if it won't fit in the screen width.
 * Return the y-offset of the next line.
 */
static int draw_line_with_wrap(struct drm_scanout_buffer *sb, const struct font_desc *font,
			       struct drm_panic_line *line, int yoffset, u32 fg_color)
{
	int chars_per_row = sb->width / font->width;
	struct drm_rect r_txt = DRM_RECT_INIT(0, yoffset, sb->width, font->height);
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
VISIBLE_IF_KUNIT int drm_panic_helper_draw_screen_kmsg(struct drm_scanout_buffer *sb,
						       u32 fg_color, u32 bg_color)
{
	const struct font_desc *font = get_default_font(sb->width, sb->height, NULL, NULL);
	struct drm_rect r_screen = DRM_RECT_INIT(0, 0, sb->width, sb->height);
	struct kmsg_dump_iter iter;
	char kmsg_buf[512];
	size_t kmsg_len;
	struct drm_panic_line line;
	int yoffset;

	if (!font || font->width > sb->width || font->height > sb->height)
		return -EINVAL;

	fg_color = drm_draw_color_from_xrgb8888(fg_color, sb->format->format);
	bg_color = drm_draw_color_from_xrgb8888(bg_color, sb->format->format);

	yoffset = sb->height - font->height - (sb->height % font->height) / 2;

	/* Fill with the background color, and draw text on top */
	drm_panic_helper_fill(sb, &r_screen, bg_color);

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

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(drm_panic_helper_draw_screen_kmsg);

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

static void __init drm_panic_helper_qr_init(void)
{
	qrbuf1 = kmalloc(QR_BUFFER1_SIZE, GFP_KERNEL);
	qrbuf2 = kmalloc(QR_BUFFER2_SIZE, GFP_KERNEL);
	stream.workspace = kmalloc(zlib_deflate_workspacesize(WINDOW_BITS, MEM_LEVEL),
				   GFP_KERNEL);
}

static void drm_panic_helper_qr_exit(void)
{
	kfree(qrbuf1);
	qrbuf1 = NULL;
	kfree(qrbuf2);
	qrbuf2 = NULL;
	kfree(stream.workspace);
	stream.workspace = NULL;
}

static int drm_panic_helper_get_qr_code_url(u8 **qr_image, unsigned int qr_version)
{
	struct kmsg_dump_iter iter;
	char url[256];
	size_t kmsg_len, max_kmsg_size;
	char *kmsg;
	int max_qr_data_size, url_len;

	url_len = snprintf(url, sizeof(url), "%s?a=%s&v=%s&z=",
			   CONFIG_DRM_PANIC_SCREEN_QR_CODE_URL,
			   utsname()->machine, utsname()->release);

	max_qr_data_size = drm_panic_helper_qr_max_data_size(qr_version, url_len);
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
	return drm_panic_helper_qr_generate(url, qrbuf2, stream.total_out, QR_BUFFER2_SIZE,
					    qrbuf1, QR_BUFFER1_SIZE);
}

static int drm_panic_helper_get_qr_code_raw(u8 **qr_image, unsigned int qr_version)
{
	struct kmsg_dump_iter iter;
	size_t kmsg_len;
	size_t max_kmsg_size = min(drm_panic_helper_qr_max_data_size(qr_version, 0),
				   QR_BUFFER1_SIZE);

	kmsg_dump_rewind(&iter);
	kmsg_dump_get_buffer(&iter, false, qrbuf1, max_kmsg_size, &kmsg_len);
	if (!kmsg_len)
		return -ENODATA;

	*qr_image = qrbuf1;
	return drm_panic_helper_qr_generate(NULL, qrbuf1, kmsg_len, QR_BUFFER1_SIZE,
					    qrbuf2, QR_BUFFER2_SIZE);
}

static int drm_panic_helper_get_qr_code(u8 **qr_image, unsigned int qr_version)
{
	if (strlen(CONFIG_DRM_PANIC_SCREEN_QR_CODE_URL) > 0)
		return drm_panic_helper_get_qr_code_url(qr_image, qr_version);
	else
		return drm_panic_helper_get_qr_code_raw(qr_image, qr_version);
}

/*
 * Draw the panic message at the center of the screen, with a QR Code
 */
VISIBLE_IF_KUNIT int drm_panic_helper_draw_screen_qr_code(struct drm_scanout_buffer *sb,
							  u32 fg_color, u32 bg_color,
							  unsigned int qr_version)
{
	const struct font_desc *font = get_default_font(sb->width, sb->height, NULL, NULL);
	struct drm_rect r_screen, r_logo, r_msg, r_qr, r_qr_canvas;
	unsigned int max_qr_size, scale;
	unsigned int msg_width, msg_height;
	int qr_width, qr_canvas_width, qr_pitch, v_margin;
	u8 *qr_image;

	if (!qrbuf1 || !qrbuf2 || !stream.workspace)
		return -ENOMEM;
	if (!font)
		return -EINVAL;

	fg_color = drm_draw_color_from_xrgb8888(fg_color, sb->format->format);
	bg_color = drm_draw_color_from_xrgb8888(bg_color, sb->format->format);

	r_screen = DRM_RECT_INIT(0, 0, sb->width, sb->height);

	drm_panic_helper_logo_rect(&r_logo, font);

	msg_width = min(get_max_line_len(panic_msg, panic_msg_lines) * font->width, sb->width);
	msg_height = min(panic_msg_lines * font->height, sb->height);
	r_msg = DRM_RECT_INIT(0, 0, msg_width, msg_height);

	max_qr_size = min(3 * sb->width / 4, 3 * sb->height / 4);

	qr_width = drm_panic_helper_get_qr_code(&qr_image, qr_version);
	if (qr_width < 0)
		return qr_width;
	else if (!qr_width)
		return -ENOSPC;

	qr_canvas_width = qr_width + QR_MARGIN * 2;
	scale = max_qr_size / qr_canvas_width;
	/* QR code is not readable if not scaled at least by 2 */
	if (scale < 2)
		return -ENOSPC;

	pr_debug("QR width %d and scale %d\n", qr_width, scale);
	r_qr_canvas = DRM_RECT_INIT(0, 0, qr_canvas_width * scale, qr_canvas_width * scale);

	v_margin = sb->height - drm_rect_height(&r_qr_canvas) - drm_rect_height(&r_msg);
	if (v_margin < 0)
		return -ENOSPC;
	v_margin /= 5;

	drm_rect_translate(&r_qr_canvas, (sb->width - r_qr_canvas.x2) / 2, 2 * v_margin);
	r_qr = DRM_RECT_INIT(r_qr_canvas.x1 + QR_MARGIN * scale, r_qr_canvas.y1 + QR_MARGIN * scale,
			     qr_width * scale, qr_width * scale);

	/* Center the panic message */
	drm_rect_translate(&r_msg, (sb->width - r_msg.x2) / 2,
			   3 * v_margin + drm_rect_height(&r_qr_canvas));

	/* Fill with the background color, and draw text on top */
	drm_panic_helper_fill(sb, &r_screen, bg_color);

	if (!drm_rect_overlap(&r_logo, &r_msg) && !drm_rect_overlap(&r_logo, &r_qr_canvas))
		drm_panic_helper_logo_draw(sb, &r_logo, font, fg_color);

	draw_txt_rectangle(sb, font, panic_msg, panic_msg_lines, true, &r_msg, fg_color);

	/* Draw the qr code */
	qr_pitch = DIV_ROUND_UP(qr_width, 8);
	drm_panic_helper_fill(sb, &r_qr_canvas, fg_color);
	drm_panic_helper_fill(sb, &r_qr, bg_color);
	drm_panic_helper_blit(sb, &r_qr, qr_image, qr_pitch, scale, fg_color);
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(drm_panic_helper_draw_screen_qr_code);
#else
static void __init drm_panic_helper_qr_init(void) { };
static void __exit drm_panic_helper_qr_exit(void) { };
#endif

/*
 * drm_panic_helper_format_is_supported()
 * @format: a fourcc color code
 * Returns: true if supported, false otherwise.
 *
 * Check if drm_panic will be able to use this color format.
 */
static bool drm_panic_helper_format_is_supported(const struct drm_format_info *format)
{
	if (format->num_planes != 1)
		return false;
	return drm_draw_can_convert_from_xrgb8888(format->format);
}

static int draw_panic_helper_dispatch(struct drm_scanout_buffer *sb,
				      enum drm_panic_type panic_type,
				      u32 fg_color, u32 bg_color,
				      unsigned int qr_version)
{
	int ret;

retry:
	switch (panic_type) {
	case DRM_PANIC_TYPE_KMSG:
		ret = drm_panic_helper_draw_screen_kmsg(sb, fg_color, bg_color);
		if (ret) {
			panic_type = DRM_PANIC_TYPE_USER;
			goto retry;
		}
		break;
#if IS_ENABLED(CONFIG_DRM_PANIC_SCREEN_QR_CODE)
	case DRM_PANIC_TYPE_QR:
		ret = drm_panic_helper_draw_screen_qr_code(sb, fg_color, bg_color, qr_version);
		if (ret) {
			panic_type = DRM_PANIC_TYPE_USER;
			goto retry;
		}
		break;
#endif
	case DRM_PANIC_TYPE_USER:
	default:
		ret = drm_panic_helper_draw_screen_user(sb, fg_color, bg_color);
	}

	return ret;
}

VISIBLE_IF_KUNIT void drm_panic_helper_set_description(const char *description)
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
EXPORT_SYMBOL_IF_KUNIT(drm_panic_helper_set_description);

VISIBLE_IF_KUNIT void drm_panic_helper_clear_description(void)
{
	struct drm_panic_line *desc_line = &panic_msg[panic_msg_lines - 1];

	desc_line->len = 0;
	desc_line->txt = NULL;
}
EXPORT_SYMBOL_IF_KUNIT(drm_panic_helper_clear_description);

/**
 * drm_plane_helper_display_panic_screen - Displays a panic screen according to the given settings
 * @plane: the DRM plane to display to
 * @description: error message to display
 * @panic_type: type of panic screen
 * @fg_color: text foreground color
 * @bg_color: text background color
 * @qr_version: version of the QR code, if any
 *
 * This helper display a panic screen on common primary planes. The panic
 * screen can either display a kernel message, a user message or a QR code.
 *
 * The helper uses struct drm_plane_helper_funcs.get_scanout_buffer, where
 * the plane can provide a scanout buffer that the panic handler can draw to.
 * Currently only linear buffer and a few color formats are supported.
 *
 * Optionally the plane can also provide a &drm_plane_helper_funcs.panic_flush
 * callback, which the DRM panic handler calls after drawing to send additional
 * commands to the hardware to make the scanout buffer visible.
 */
int drm_plane_helper_display_panic_screen(struct drm_plane *plane, const char *description,
					  enum drm_panic_type panic_type,
					  u32 fg_color, u32 bg_color, unsigned int qr_version)
{
	struct drm_scanout_buffer sb = { };
	int ret;

	if (!plane->helper_private || !plane->helper_private->get_scanout_buffer)
		return -EINVAL;

	ret = plane->helper_private->get_scanout_buffer(plane, &sb);
	if (ret)
		return ret;

	if (!drm_panic_helper_format_is_supported(sb.format))
		return -EINVAL;

	/* One of these should be set, or it can't draw pixels */
	if (!sb.set_pixel && !sb.pages && iosys_map_is_null(&sb.map[0]))
		return -EINVAL;

	drm_panic_helper_set_description(description);

	ret = draw_panic_helper_dispatch(&sb, panic_type, fg_color, bg_color, qr_version);
	if (!ret) {
		/*
		 * Only flush if we have a panic screen to display. Otherwise
		 * it's probably better to leave the display output as-is.
		 */
		if (plane->helper_private->panic_flush)
			plane->helper_private->panic_flush(plane);
	}

	drm_panic_helper_clear_description();

	return ret;
}
EXPORT_SYMBOL(drm_plane_helper_display_panic_screen);

int __init drm_panic_helper_init(void)
{
	drm_panic_helper_setup_logo();
	drm_panic_helper_qr_init();

	return 0;
}

void __exit drm_panic_helper_exit(void)
{
	drm_panic_helper_qr_exit();
}
