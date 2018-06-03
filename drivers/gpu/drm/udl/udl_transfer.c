/*
 * Copyright (C) 2012 Red Hat
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/prefetch.h>
#include <asm/unaligned.h>

#include <drm/drmP.h>
#include "udl_drv.h"

#define MAX_CMD_PIXELS		255

#define RLX_HEADER_BYTES	7
#define MIN_RLX_PIX_BYTES       4
#define MIN_RLX_CMD_BYTES	(RLX_HEADER_BYTES + MIN_RLX_PIX_BYTES)

#define RLE_HEADER_BYTES	6
#define MIN_RLE_PIX_BYTES	3
#define MIN_RLE_CMD_BYTES	(RLE_HEADER_BYTES + MIN_RLE_PIX_BYTES)

#define RAW_HEADER_BYTES	6
#define MIN_RAW_PIX_BYTES	2
#define MIN_RAW_CMD_BYTES	(RAW_HEADER_BYTES + MIN_RAW_PIX_BYTES)

/*
 * Trims identical data from front and back of line
 * Sets new front buffer address and width
 * And returns byte count of identical pixels
 * Assumes CPU natural alignment (unsigned long)
 * for back and front buffer ptrs and width
 */
#if 0
static int udl_trim_hline(const u8 *bback, const u8 **bfront, int *width_bytes)
{
	int j, k;
	const unsigned long *back = (const unsigned long *) bback;
	const unsigned long *front = (const unsigned long *) *bfront;
	const int width = *width_bytes / sizeof(unsigned long);
	int identical = width;
	int start = width;
	int end = width;

	prefetch((void *) front);
	prefetch((void *) back);

	for (j = 0; j < width; j++) {
		if (back[j] != front[j]) {
			start = j;
			break;
		}
	}

	for (k = width - 1; k > j; k--) {
		if (back[k] != front[k]) {
			end = k+1;
			break;
		}
	}

	identical = start + (width - end);
	*bfront = (u8 *) &front[start];
	*width_bytes = (end - start) * sizeof(unsigned long);

	return identical * sizeof(unsigned long);
}
#endif

static inline u16 pixel32_to_be16(const uint32_t pixel)
{
	return (((pixel >> 3) & 0x001f) |
		((pixel >> 5) & 0x07e0) |
		((pixel >> 8) & 0xf800));
}

static inline u16 get_pixel_val16(const uint8_t *pixel, int log_bpp)
{
	u16 pixel_val16;
	if (log_bpp == 1)
		pixel_val16 = *(const uint16_t *)pixel;
	else
		pixel_val16 = pixel32_to_be16(*(const uint32_t *)pixel);
	return pixel_val16;
}

/*
 * Render a command stream for an encoded horizontal line segment of pixels.
 *
 * A command buffer holds several commands.
 * It always begins with a fresh command header
 * (the protocol doesn't require this, but we enforce it to allow
 * multiple buffers to be potentially encoded and sent in parallel).
 * A single command encodes one contiguous horizontal line of pixels
 *
 * The function relies on the client to do all allocation, so that
 * rendering can be done directly to output buffers (e.g. USB URBs).
 * The function fills the supplied command buffer, providing information
 * on where it left off, so the client may call in again with additional
 * buffers if the line will take several buffers to complete.
 *
 * A single command can transmit a maximum of 256 pixels,
 * regardless of the compression ratio (protocol design limit).
 * To the hardware, 0 for a size byte means 256
 *
 * Rather than 256 pixel commands which are either rl or raw encoded,
 * the rlx command simply assumes alternating raw and rl spans within one cmd.
 * This has a slightly larger header overhead, but produces more even results.
 * It also processes all data (read and write) in a single pass.
 * Performance benchmarks of common cases show it having just slightly better
 * compression than 256 pixel raw or rle commands, with similar CPU consumpion.
 * But for very rl friendly data, will compress not quite as well.
 */
static void udl_compress_hline16(
	const u8 **pixel_start_ptr,
	const u8 *const pixel_end,
	uint32_t *device_address_ptr,
	uint8_t **command_buffer_ptr,
	const uint8_t *const cmd_buffer_end, int log_bpp)
{
	const int bpp = 1 << log_bpp;
	const u8 *pixel = *pixel_start_ptr;
	uint32_t dev_addr  = *device_address_ptr;
	uint8_t *cmd = *command_buffer_ptr;

	while ((pixel_end > pixel) &&
	       (cmd_buffer_end - MIN_RLX_CMD_BYTES > cmd)) {
		uint8_t *raw_pixels_count_byte = NULL;
		uint8_t *cmd_pixels_count_byte = NULL;
		const u8 *raw_pixel_start = NULL;
		const u8 *cmd_pixel_start, *cmd_pixel_end = NULL;
		uint16_t pixel_val16;

		prefetchw((void *) cmd); /* pull in one cache line at least */

		*cmd++ = 0xaf;
		*cmd++ = 0x6b;
		*cmd++ = (uint8_t) ((dev_addr >> 16) & 0xFF);
		*cmd++ = (uint8_t) ((dev_addr >> 8) & 0xFF);
		*cmd++ = (uint8_t) ((dev_addr) & 0xFF);

		cmd_pixels_count_byte = cmd++; /*  we'll know this later */
		cmd_pixel_start = pixel;

		raw_pixels_count_byte = cmd++; /*  we'll know this later */
		raw_pixel_start = pixel;

		cmd_pixel_end = pixel + (min3(MAX_CMD_PIXELS + 1UL,
					(unsigned long)(pixel_end - pixel) >> log_bpp,
					(unsigned long)(cmd_buffer_end - 1 - cmd) / 2) << log_bpp);

		prefetch_range((void *) pixel, cmd_pixel_end - pixel);
		pixel_val16 = get_pixel_val16(pixel, log_bpp);

		while (pixel < cmd_pixel_end) {
			const u8 *const start = pixel;
			const uint16_t repeating_pixel_val16 = pixel_val16;

			put_unaligned_be16(pixel_val16, cmd);

			cmd += 2;
			pixel += bpp;

			while (pixel < cmd_pixel_end) {
				pixel_val16 = get_pixel_val16(pixel, log_bpp);
				if (pixel_val16 != repeating_pixel_val16)
					break;
				pixel += bpp;
			}

			if (unlikely(pixel > start + bpp)) {
				/* go back and fill in raw pixel count */
				*raw_pixels_count_byte = (((start -
						raw_pixel_start) >> log_bpp) + 1) & 0xFF;

				/* immediately after raw data is repeat byte */
				*cmd++ = (((pixel - start) >> log_bpp) - 1) & 0xFF;

				/* Then start another raw pixel span */
				raw_pixel_start = pixel;
				raw_pixels_count_byte = cmd++;
			}
		}

		if (pixel > raw_pixel_start) {
			/* finalize last RAW span */
			*raw_pixels_count_byte = ((pixel - raw_pixel_start) >> log_bpp) & 0xFF;
		} else {
			/* undo unused byte */
			cmd--;
		}

		*cmd_pixels_count_byte = ((pixel - cmd_pixel_start) >> log_bpp) & 0xFF;
		dev_addr += ((pixel - cmd_pixel_start) >> log_bpp) * 2;
	}

	if (cmd_buffer_end <= MIN_RLX_CMD_BYTES + cmd) {
		/* Fill leftover bytes with no-ops */
		if (cmd_buffer_end > cmd)
			memset(cmd, 0xAF, cmd_buffer_end - cmd);
		cmd = (uint8_t *) cmd_buffer_end;
	}

	*command_buffer_ptr = cmd;
	*pixel_start_ptr = pixel;
	*device_address_ptr = dev_addr;

	return;
}

/*
 * There are 3 copies of every pixel: The front buffer that the fbdev
 * client renders to, the actual framebuffer across the USB bus in hardware
 * (that we can only write to, slowly, and can never read), and (optionally)
 * our shadow copy that tracks what's been sent to that hardware buffer.
 */
int udl_render_hline(struct drm_device *dev, int log_bpp, struct urb **urb_ptr,
		     const char *front, char **urb_buf_ptr,
		     u32 byte_offset, u32 device_byte_offset,
		     u32 byte_width,
		     int *ident_ptr, int *sent_ptr)
{
	const u8 *line_start, *line_end, *next_pixel;
	u32 base16 = 0 + (device_byte_offset >> log_bpp) * 2;
	struct urb *urb = *urb_ptr;
	u8 *cmd = *urb_buf_ptr;
	u8 *cmd_end = (u8 *) urb->transfer_buffer + urb->transfer_buffer_length;

	BUG_ON(!(log_bpp == 1 || log_bpp == 2));

	line_start = (u8 *) (front + byte_offset);
	next_pixel = line_start;
	line_end = next_pixel + byte_width;

	while (next_pixel < line_end) {

		udl_compress_hline16(&next_pixel,
			     line_end, &base16,
			     (u8 **) &cmd, (u8 *) cmd_end, log_bpp);

		if (cmd >= cmd_end) {
			int len = cmd - (u8 *) urb->transfer_buffer;
			if (udl_submit_urb(dev, urb, len))
				return 1; /* lost pixels is set */
			*sent_ptr += len;
			urb = udl_get_urb(dev);
			if (!urb)
				return 1; /* lost_pixels is set */
			*urb_ptr = urb;
			cmd = urb->transfer_buffer;
			cmd_end = &cmd[urb->transfer_buffer_length];
		}
	}

	*urb_buf_ptr = cmd;

	return 0;
}

