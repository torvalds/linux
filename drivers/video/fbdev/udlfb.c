// SPDX-License-Identifier: GPL-2.0-only
/*
 * udlfb.c -- Framebuffer driver for DisplayLink USB controller
 *
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 *
 * Layout is based on skeletonfb by James Simmons and Geert Uytterhoeven,
 * usb-skeleton by GregKH.
 *
 * Device-specific portions based on information from Displaylink, with work
 * from Florian Echtler, Henrik Bjerregaard Pedersen, and others.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/unaligned.h>
#include <video/udlfb.h>
#include "edid.h"

#define OUT_EP_NUM	1	/* The endpoint number we will use */

static const struct fb_fix_screeninfo dlfb_fix = {
	.id =           "udlfb",
	.type =         FB_TYPE_PACKED_PIXELS,
	.visual =       FB_VISUAL_TRUECOLOR,
	.xpanstep =     0,
	.ypanstep =     0,
	.ywrapstep =    0,
	.accel =        FB_ACCEL_NONE,
};

static const u32 udlfb_info_flags = FBINFO_READS_FAST |
		FBINFO_VIRTFB |
		FBINFO_HWACCEL_IMAGEBLIT | FBINFO_HWACCEL_FILLRECT |
		FBINFO_HWACCEL_COPYAREA | FBINFO_MISC_ALWAYS_SETPAR;

/*
 * There are many DisplayLink-based graphics products, all with unique PIDs.
 * So we match on DisplayLink's VID + Vendor-Defined Interface Class (0xff)
 * We also require a match on SubClass (0x00) and Protocol (0x00),
 * which is compatible with all known USB 2.0 era graphics chips and firmware,
 * but allows DisplayLink to increment those for any future incompatible chips
 */
static const struct usb_device_id id_table[] = {
	{.idVendor = 0x17e9,
	 .bInterfaceClass = 0xff,
	 .bInterfaceSubClass = 0x00,
	 .bInterfaceProtocol = 0x00,
	 .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
		USB_DEVICE_ID_MATCH_INT_CLASS |
		USB_DEVICE_ID_MATCH_INT_SUBCLASS |
		USB_DEVICE_ID_MATCH_INT_PROTOCOL,
	},
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

/* module options */
static bool console = true; /* Allow fbcon to open framebuffer */
static bool fb_defio = true;  /* Detect mmap writes using page faults */
static bool shadow = true; /* Optionally disable shadow framebuffer */
static int pixel_limit; /* Optionally force a pixel resolution limit */

struct dlfb_deferred_free {
	struct list_head list;
	void *mem;
};

static int dlfb_realloc_framebuffer(struct dlfb_data *dlfb, struct fb_info *info, u32 new_len);

/* dlfb keeps a list of urbs for efficient bulk transfers */
static void dlfb_urb_completion(struct urb *urb);
static struct urb *dlfb_get_urb(struct dlfb_data *dlfb);
static int dlfb_submit_urb(struct dlfb_data *dlfb, struct urb * urb, size_t len);
static int dlfb_alloc_urb_list(struct dlfb_data *dlfb, int count, size_t size);
static void dlfb_free_urb_list(struct dlfb_data *dlfb);

/*
 * All DisplayLink bulk operations start with 0xAF, followed by specific code
 * All operations are written to buffers which then later get sent to device
 */
static char *dlfb_set_register(char *buf, u8 reg, u8 val)
{
	*buf++ = 0xAF;
	*buf++ = 0x20;
	*buf++ = reg;
	*buf++ = val;
	return buf;
}

static char *dlfb_vidreg_lock(char *buf)
{
	return dlfb_set_register(buf, 0xFF, 0x00);
}

static char *dlfb_vidreg_unlock(char *buf)
{
	return dlfb_set_register(buf, 0xFF, 0xFF);
}

/*
 * Map FB_BLANK_* to DisplayLink register
 * DLReg FB_BLANK_*
 * ----- -----------------------------
 *  0x00 FB_BLANK_UNBLANK (0)
 *  0x01 FB_BLANK (1)
 *  0x03 FB_BLANK_VSYNC_SUSPEND (2)
 *  0x05 FB_BLANK_HSYNC_SUSPEND (3)
 *  0x07 FB_BLANK_POWERDOWN (4) Note: requires modeset to come back
 */
static char *dlfb_blanking(char *buf, int fb_blank)
{
	u8 reg;

	switch (fb_blank) {
	case FB_BLANK_POWERDOWN:
		reg = 0x07;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		reg = 0x05;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		reg = 0x03;
		break;
	case FB_BLANK_NORMAL:
		reg = 0x01;
		break;
	default:
		reg = 0x00;
	}

	buf = dlfb_set_register(buf, 0x1F, reg);

	return buf;
}

static char *dlfb_set_color_depth(char *buf, u8 selection)
{
	return dlfb_set_register(buf, 0x00, selection);
}

static char *dlfb_set_base16bpp(char *wrptr, u32 base)
{
	/* the base pointer is 16 bits wide, 0x20 is hi byte. */
	wrptr = dlfb_set_register(wrptr, 0x20, base >> 16);
	wrptr = dlfb_set_register(wrptr, 0x21, base >> 8);
	return dlfb_set_register(wrptr, 0x22, base);
}

/*
 * DisplayLink HW has separate 16bpp and 8bpp framebuffers.
 * In 24bpp modes, the low 323 RGB bits go in the 8bpp framebuffer
 */
static char *dlfb_set_base8bpp(char *wrptr, u32 base)
{
	wrptr = dlfb_set_register(wrptr, 0x26, base >> 16);
	wrptr = dlfb_set_register(wrptr, 0x27, base >> 8);
	return dlfb_set_register(wrptr, 0x28, base);
}

static char *dlfb_set_register_16(char *wrptr, u8 reg, u16 value)
{
	wrptr = dlfb_set_register(wrptr, reg, value >> 8);
	return dlfb_set_register(wrptr, reg+1, value);
}

/*
 * This is kind of weird because the controller takes some
 * register values in a different byte order than other registers.
 */
static char *dlfb_set_register_16be(char *wrptr, u8 reg, u16 value)
{
	wrptr = dlfb_set_register(wrptr, reg, value);
	return dlfb_set_register(wrptr, reg+1, value >> 8);
}

/*
 * LFSR is linear feedback shift register. The reason we have this is
 * because the display controller needs to minimize the clock depth of
 * various counters used in the display path. So this code reverses the
 * provided value into the lfsr16 value by counting backwards to get
 * the value that needs to be set in the hardware comparator to get the
 * same actual count. This makes sense once you read above a couple of
 * times and think about it from a hardware perspective.
 */
static u16 dlfb_lfsr16(u16 actual_count)
{
	u32 lv = 0xFFFF; /* This is the lfsr value that the hw starts with */

	while (actual_count--) {
		lv =	 ((lv << 1) |
			(((lv >> 15) ^ (lv >> 4) ^ (lv >> 2) ^ (lv >> 1)) & 1))
			& 0xFFFF;
	}

	return (u16) lv;
}

/*
 * This does LFSR conversion on the value that is to be written.
 * See LFSR explanation above for more detail.
 */
static char *dlfb_set_register_lfsr16(char *wrptr, u8 reg, u16 value)
{
	return dlfb_set_register_16(wrptr, reg, dlfb_lfsr16(value));
}

/*
 * This takes a standard fbdev screeninfo struct and all of its monitor mode
 * details and converts them into the DisplayLink equivalent register commands.
 */
static char *dlfb_set_vid_cmds(char *wrptr, struct fb_var_screeninfo *var)
{
	u16 xds, yds;
	u16 xde, yde;
	u16 yec;

	/* x display start */
	xds = var->left_margin + var->hsync_len;
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x01, xds);
	/* x display end */
	xde = xds + var->xres;
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x03, xde);

	/* y display start */
	yds = var->upper_margin + var->vsync_len;
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x05, yds);
	/* y display end */
	yde = yds + var->yres;
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x07, yde);

	/* x end count is active + blanking - 1 */
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x09,
			xde + var->right_margin - 1);

	/* libdlo hardcodes hsync start to 1 */
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x0B, 1);

	/* hsync end is width of sync pulse + 1 */
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x0D, var->hsync_len + 1);

	/* hpixels is active pixels */
	wrptr = dlfb_set_register_16(wrptr, 0x0F, var->xres);

	/* yendcount is vertical active + vertical blanking */
	yec = var->yres + var->upper_margin + var->lower_margin +
			var->vsync_len;
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x11, yec);

	/* libdlo hardcodes vsync start to 0 */
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x13, 0);

	/* vsync end is width of vsync pulse */
	wrptr = dlfb_set_register_lfsr16(wrptr, 0x15, var->vsync_len);

	/* vpixels is active pixels */
	wrptr = dlfb_set_register_16(wrptr, 0x17, var->yres);

	/* convert picoseconds to 5kHz multiple for pclk5k = x * 1E12/5k */
	wrptr = dlfb_set_register_16be(wrptr, 0x1B,
			200*1000*1000/var->pixclock);

	return wrptr;
}

/*
 * This takes a standard fbdev screeninfo struct that was fetched or prepared
 * and then generates the appropriate command sequence that then drives the
 * display controller.
 */
static int dlfb_set_video_mode(struct dlfb_data *dlfb,
				struct fb_var_screeninfo *var)
{
	char *buf;
	char *wrptr;
	int retval;
	int writesize;
	struct urb *urb;

	if (!atomic_read(&dlfb->usb_active))
		return -EPERM;

	urb = dlfb_get_urb(dlfb);
	if (!urb)
		return -ENOMEM;

	buf = (char *) urb->transfer_buffer;

	/*
	* This first section has to do with setting the base address on the
	* controller * associated with the display. There are 2 base
	* pointers, currently, we only * use the 16 bpp segment.
	*/
	wrptr = dlfb_vidreg_lock(buf);
	wrptr = dlfb_set_color_depth(wrptr, 0x00);
	/* set base for 16bpp segment to 0 */
	wrptr = dlfb_set_base16bpp(wrptr, 0);
	/* set base for 8bpp segment to end of fb */
	wrptr = dlfb_set_base8bpp(wrptr, dlfb->info->fix.smem_len);

	wrptr = dlfb_set_vid_cmds(wrptr, var);
	wrptr = dlfb_blanking(wrptr, FB_BLANK_UNBLANK);
	wrptr = dlfb_vidreg_unlock(wrptr);

	writesize = wrptr - buf;

	retval = dlfb_submit_urb(dlfb, urb, writesize);

	dlfb->blank_mode = FB_BLANK_UNBLANK;

	return retval;
}

static int dlfb_ops_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;

	if (info->fbdefio)
		return fb_deferred_io_mmap(info, vma);

	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	if (size > info->fix.smem_len)
		return -EINVAL;
	if (offset > info->fix.smem_len - size)
		return -EINVAL;

	pos = (unsigned long)info->fix.smem_start + offset;

	dev_dbg(info->dev, "mmap() framebuffer addr:%lu size:%lu\n",
		pos, size);

	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;

		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	return 0;
}

/*
 * Trims identical data from front and back of line
 * Sets new front buffer address and width
 * And returns byte count of identical pixels
 * Assumes CPU natural alignment (unsigned long)
 * for back and front buffer ptrs and width
 */
static int dlfb_trim_hline(const u8 *bback, const u8 **bfront, int *width_bytes)
{
	int j, k;
	const unsigned long *back = (const unsigned long *) bback;
	const unsigned long *front = (const unsigned long *) *bfront;
	const int width = *width_bytes / sizeof(unsigned long);
	int identical;
	int start = width;
	int end = width;

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
static void dlfb_compress_hline(
	const uint16_t **pixel_start_ptr,
	const uint16_t *const pixel_end,
	uint32_t *device_address_ptr,
	uint8_t **command_buffer_ptr,
	const uint8_t *const cmd_buffer_end,
	unsigned long back_buffer_offset,
	int *ident_ptr)
{
	const uint16_t *pixel = *pixel_start_ptr;
	uint32_t dev_addr  = *device_address_ptr;
	uint8_t *cmd = *command_buffer_ptr;

	while ((pixel_end > pixel) &&
	       (cmd_buffer_end - MIN_RLX_CMD_BYTES > cmd)) {
		uint8_t *raw_pixels_count_byte = NULL;
		uint8_t *cmd_pixels_count_byte = NULL;
		const uint16_t *raw_pixel_start = NULL;
		const uint16_t *cmd_pixel_start, *cmd_pixel_end = NULL;

		if (back_buffer_offset &&
		    *pixel == *(u16 *)((u8 *)pixel + back_buffer_offset)) {
			pixel++;
			dev_addr += BPP;
			(*ident_ptr)++;
			continue;
		}

		*cmd++ = 0xAF;
		*cmd++ = 0x6B;
		*cmd++ = dev_addr >> 16;
		*cmd++ = dev_addr >> 8;
		*cmd++ = dev_addr;

		cmd_pixels_count_byte = cmd++; /*  we'll know this later */
		cmd_pixel_start = pixel;

		raw_pixels_count_byte = cmd++; /*  we'll know this later */
		raw_pixel_start = pixel;

		cmd_pixel_end = pixel + min3(MAX_CMD_PIXELS + 1UL,
					(unsigned long)(pixel_end - pixel),
					(unsigned long)(cmd_buffer_end - 1 - cmd) / BPP);

		if (back_buffer_offset) {
			/* note: the framebuffer may change under us, so we must test for underflow */
			while (cmd_pixel_end - 1 > pixel &&
			       *(cmd_pixel_end - 1) == *(u16 *)((u8 *)(cmd_pixel_end - 1) + back_buffer_offset))
				cmd_pixel_end--;
		}

		while (pixel < cmd_pixel_end) {
			const uint16_t * const repeating_pixel = pixel;
			u16 pixel_value = *pixel;

			put_unaligned_be16(pixel_value, cmd);
			if (back_buffer_offset)
				*(u16 *)((u8 *)pixel + back_buffer_offset) = pixel_value;
			cmd += 2;
			pixel++;

			if (unlikely((pixel < cmd_pixel_end) &&
				     (*pixel == pixel_value))) {
				/* go back and fill in raw pixel count */
				*raw_pixels_count_byte = ((repeating_pixel -
						raw_pixel_start) + 1) & 0xFF;

				do {
					if (back_buffer_offset)
						*(u16 *)((u8 *)pixel + back_buffer_offset) = pixel_value;
					pixel++;
				} while ((pixel < cmd_pixel_end) &&
					 (*pixel == pixel_value));

				/* immediately after raw data is repeat byte */
				*cmd++ = ((pixel - repeating_pixel) - 1) & 0xFF;

				/* Then start another raw pixel span */
				raw_pixel_start = pixel;
				raw_pixels_count_byte = cmd++;
			}
		}

		if (pixel > raw_pixel_start) {
			/* finalize last RAW span */
			*raw_pixels_count_byte = (pixel-raw_pixel_start) & 0xFF;
		} else {
			/* undo unused byte */
			cmd--;
		}

		*cmd_pixels_count_byte = (pixel - cmd_pixel_start) & 0xFF;
		dev_addr += (u8 *)pixel - (u8 *)cmd_pixel_start;
	}

	if (cmd_buffer_end - MIN_RLX_CMD_BYTES <= cmd) {
		/* Fill leftover bytes with no-ops */
		if (cmd_buffer_end > cmd)
			memset(cmd, 0xAF, cmd_buffer_end - cmd);
		cmd = (uint8_t *) cmd_buffer_end;
	}

	*command_buffer_ptr = cmd;
	*pixel_start_ptr = pixel;
	*device_address_ptr = dev_addr;
}

/*
 * There are 3 copies of every pixel: The front buffer that the fbdev
 * client renders to, the actual framebuffer across the USB bus in hardware
 * (that we can only write to, slowly, and can never read), and (optionally)
 * our shadow copy that tracks what's been sent to that hardware buffer.
 */
static int dlfb_render_hline(struct dlfb_data *dlfb, struct urb **urb_ptr,
			      const char *front, char **urb_buf_ptr,
			      u32 byte_offset, u32 byte_width,
			      int *ident_ptr, int *sent_ptr)
{
	const u8 *line_start, *line_end, *next_pixel;
	u32 dev_addr = dlfb->base16 + byte_offset;
	struct urb *urb = *urb_ptr;
	u8 *cmd = *urb_buf_ptr;
	u8 *cmd_end = (u8 *) urb->transfer_buffer + urb->transfer_buffer_length;
	unsigned long back_buffer_offset = 0;

	line_start = (u8 *) (front + byte_offset);
	next_pixel = line_start;
	line_end = next_pixel + byte_width;

	if (dlfb->backing_buffer) {
		int offset;
		const u8 *back_start = (u8 *) (dlfb->backing_buffer
						+ byte_offset);

		back_buffer_offset = (unsigned long)back_start - (unsigned long)line_start;

		*ident_ptr += dlfb_trim_hline(back_start, &next_pixel,
			&byte_width);

		offset = next_pixel - line_start;
		line_end = next_pixel + byte_width;
		dev_addr += offset;
		back_start += offset;
		line_start += offset;
	}

	while (next_pixel < line_end) {

		dlfb_compress_hline((const uint16_t **) &next_pixel,
			     (const uint16_t *) line_end, &dev_addr,
			(u8 **) &cmd, (u8 *) cmd_end, back_buffer_offset,
			ident_ptr);

		if (cmd >= cmd_end) {
			int len = cmd - (u8 *) urb->transfer_buffer;
			if (dlfb_submit_urb(dlfb, urb, len))
				return 1; /* lost pixels is set */
			*sent_ptr += len;
			urb = dlfb_get_urb(dlfb);
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

static int dlfb_handle_damage(struct dlfb_data *dlfb, int x, int y, int width, int height)
{
	int i, ret;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	struct urb *urb;
	int aligned_x;

	start_cycles = get_cycles();

	mutex_lock(&dlfb->render_mutex);

	aligned_x = DL_ALIGN_DOWN(x, sizeof(unsigned long));
	width = DL_ALIGN_UP(width + (x-aligned_x), sizeof(unsigned long));
	x = aligned_x;

	if ((width <= 0) ||
	    (x + width > dlfb->info->var.xres) ||
	    (y + height > dlfb->info->var.yres)) {
		ret = -EINVAL;
		goto unlock_ret;
	}

	if (!atomic_read(&dlfb->usb_active)) {
		ret = 0;
		goto unlock_ret;
	}

	urb = dlfb_get_urb(dlfb);
	if (!urb) {
		ret = 0;
		goto unlock_ret;
	}
	cmd = urb->transfer_buffer;

	for (i = y; i < y + height ; i++) {
		const int line_offset = dlfb->info->fix.line_length * i;
		const int byte_offset = line_offset + (x * BPP);

		if (dlfb_render_hline(dlfb, &urb,
				      (char *) dlfb->info->fix.smem_start,
				      &cmd, byte_offset, width * BPP,
				      &bytes_identical, &bytes_sent))
			goto error;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		int len;
		if (cmd < (char *) urb->transfer_buffer + urb->transfer_buffer_length)
			*cmd++ = 0xAF;
		/* Send partial buffer remaining before exiting */
		len = cmd - (char *) urb->transfer_buffer;
		dlfb_submit_urb(dlfb, urb, len);
		bytes_sent += len;
	} else
		dlfb_urb_completion(urb);

error:
	atomic_add(bytes_sent, &dlfb->bytes_sent);
	atomic_add(bytes_identical, &dlfb->bytes_identical);
	atomic_add(width*height*2, &dlfb->bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles)
		    >> 10)), /* Kcycles */
		   &dlfb->cpu_kcycles_used);

	ret = 0;

unlock_ret:
	mutex_unlock(&dlfb->render_mutex);
	return ret;
}

static void dlfb_init_damage(struct dlfb_data *dlfb)
{
	dlfb->damage_x = INT_MAX;
	dlfb->damage_x2 = 0;
	dlfb->damage_y = INT_MAX;
	dlfb->damage_y2 = 0;
}

static void dlfb_damage_work(struct work_struct *w)
{
	struct dlfb_data *dlfb = container_of(w, struct dlfb_data, damage_work);
	int x, x2, y, y2;

	spin_lock_irq(&dlfb->damage_lock);
	x = dlfb->damage_x;
	x2 = dlfb->damage_x2;
	y = dlfb->damage_y;
	y2 = dlfb->damage_y2;
	dlfb_init_damage(dlfb);
	spin_unlock_irq(&dlfb->damage_lock);

	if (x < x2 && y < y2)
		dlfb_handle_damage(dlfb, x, y, x2 - x, y2 - y);
}

static void dlfb_offload_damage(struct dlfb_data *dlfb, int x, int y, int width, int height)
{
	unsigned long flags;
	int x2 = x + width;
	int y2 = y + height;

	if (x >= x2 || y >= y2)
		return;

	spin_lock_irqsave(&dlfb->damage_lock, flags);
	dlfb->damage_x = min(x, dlfb->damage_x);
	dlfb->damage_x2 = max(x2, dlfb->damage_x2);
	dlfb->damage_y = min(y, dlfb->damage_y);
	dlfb->damage_y2 = max(y2, dlfb->damage_y2);
	spin_unlock_irqrestore(&dlfb->damage_lock, flags);

	schedule_work(&dlfb->damage_work);
}

/*
 * NOTE: fb_defio.c is holding info->fbdefio.mutex
 *   Touching ANY framebuffer memory that triggers a page fault
 *   in fb_defio will cause a deadlock, when it also tries to
 *   grab the same mutex.
 */
static void dlfb_dpy_deferred_io(struct fb_info *info, struct list_head *pagereflist)
{
	struct fb_deferred_io_pageref *pageref;
	struct dlfb_data *dlfb = info->par;
	struct urb *urb;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	int bytes_rendered = 0;

	mutex_lock(&dlfb->render_mutex);

	if (!fb_defio)
		goto unlock_ret;

	if (!atomic_read(&dlfb->usb_active))
		goto unlock_ret;

	start_cycles = get_cycles();

	urb = dlfb_get_urb(dlfb);
	if (!urb)
		goto unlock_ret;

	cmd = urb->transfer_buffer;

	/* walk the written page list and render each to device */
	list_for_each_entry(pageref, pagereflist, list) {
		if (dlfb_render_hline(dlfb, &urb, (char *) info->fix.smem_start,
				      &cmd, pageref->offset, PAGE_SIZE,
				      &bytes_identical, &bytes_sent))
			goto error;
		bytes_rendered += PAGE_SIZE;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		int len;
		if (cmd < (char *) urb->transfer_buffer + urb->transfer_buffer_length)
			*cmd++ = 0xAF;
		/* Send partial buffer remaining before exiting */
		len = cmd - (char *) urb->transfer_buffer;
		dlfb_submit_urb(dlfb, urb, len);
		bytes_sent += len;
	} else
		dlfb_urb_completion(urb);

error:
	atomic_add(bytes_sent, &dlfb->bytes_sent);
	atomic_add(bytes_identical, &dlfb->bytes_identical);
	atomic_add(bytes_rendered, &dlfb->bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles)
		    >> 10)), /* Kcycles */
		   &dlfb->cpu_kcycles_used);
unlock_ret:
	mutex_unlock(&dlfb->render_mutex);
}

static int dlfb_get_edid(struct dlfb_data *dlfb, char *edid, int len)
{
	int i, ret;
	char *rbuf;

	rbuf = kmalloc(2, GFP_KERNEL);
	if (!rbuf)
		return 0;

	for (i = 0; i < len; i++) {
		ret = usb_control_msg(dlfb->udev,
				      usb_rcvctrlpipe(dlfb->udev, 0), 0x02,
				      (0x80 | (0x02 << 5)), i << 8, 0xA1,
				      rbuf, 2, USB_CTRL_GET_TIMEOUT);
		if (ret < 2) {
			dev_err(&dlfb->udev->dev,
				"Read EDID byte %d failed: %d\n", i, ret);
			i--;
			break;
		}
		edid[i] = rbuf[1];
	}

	kfree(rbuf);

	return i;
}

static int dlfb_ops_ioctl(struct fb_info *info, unsigned int cmd,
				unsigned long arg)
{

	struct dlfb_data *dlfb = info->par;

	if (!atomic_read(&dlfb->usb_active))
		return 0;

	/* TODO: Update X server to get this from sysfs instead */
	if (cmd == DLFB_IOCTL_RETURN_EDID) {
		void __user *edid = (void __user *)arg;
		if (copy_to_user(edid, dlfb->edid, dlfb->edid_size))
			return -EFAULT;
		return 0;
	}

	/* TODO: Help propose a standard fb.h ioctl to report mmap damage */
	if (cmd == DLFB_IOCTL_REPORT_DAMAGE) {
		struct dloarea area;

		if (copy_from_user(&area, (void __user *)arg,
				  sizeof(struct dloarea)))
			return -EFAULT;

		/*
		 * If we have a damage-aware client, turn fb_defio "off"
		 * To avoid perf imact of unnecessary page fault handling.
		 * Done by resetting the delay for this fb_info to a very
		 * long period. Pages will become writable and stay that way.
		 * Reset to normal value when all clients have closed this fb.
		 */
		if (info->fbdefio)
			info->fbdefio->delay = DL_DEFIO_WRITE_DISABLE;

		if (area.x < 0)
			area.x = 0;

		if (area.x > info->var.xres)
			area.x = info->var.xres;

		if (area.y < 0)
			area.y = 0;

		if (area.y > info->var.yres)
			area.y = info->var.yres;

		dlfb_handle_damage(dlfb, area.x, area.y, area.w, area.h);
	}

	return 0;
}

/* taken from vesafb */
static int
dlfb_ops_setcolreg(unsigned regno, unsigned red, unsigned green,
	       unsigned blue, unsigned transp, struct fb_info *info)
{
	int err = 0;

	if (regno >= info->cmap.len)
		return 1;

	if (regno < 16) {
		if (info->var.red.offset == 10) {
			/* 1:5:5:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800) >> 1) |
			    ((green & 0xf800) >> 6) | ((blue & 0xf800) >> 11);
		} else {
			/* 0:5:6:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800)) |
			    ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		}
	}

	return err;
}

/*
 * It's common for several clients to have framebuffer open simultaneously.
 * e.g. both fbcon and X. Makes things interesting.
 * Assumes caller is holding info->lock (for open and release at least)
 */
static int dlfb_ops_open(struct fb_info *info, int user)
{
	struct dlfb_data *dlfb = info->par;

	/*
	 * fbcon aggressively connects to first framebuffer it finds,
	 * preventing other clients (X) from working properly. Usually
	 * not what the user wants. Fail by default with option to enable.
	 */
	if ((user == 0) && (!console))
		return -EBUSY;

	/* If the USB device is gone, we don't accept new opens */
	if (dlfb->virtualized)
		return -ENODEV;

	dlfb->fb_count++;

	if (fb_defio && (info->fbdefio == NULL)) {
		/* enable defio at last moment if not disabled by client */

		struct fb_deferred_io *fbdefio;

		fbdefio = kzalloc(sizeof(struct fb_deferred_io), GFP_KERNEL);

		if (fbdefio) {
			fbdefio->delay = DL_DEFIO_WRITE_DELAY;
			fbdefio->sort_pagereflist = true;
			fbdefio->deferred_io = dlfb_dpy_deferred_io;
		}

		info->fbdefio = fbdefio;
		fb_deferred_io_init(info);
	}

	dev_dbg(info->dev, "open, user=%d fb_info=%p count=%d\n",
		user, info, dlfb->fb_count);

	return 0;
}

static void dlfb_ops_destroy(struct fb_info *info)
{
	struct dlfb_data *dlfb = info->par;

	cancel_work_sync(&dlfb->damage_work);

	mutex_destroy(&dlfb->render_mutex);

	if (info->cmap.len != 0)
		fb_dealloc_cmap(&info->cmap);
	if (info->monspecs.modedb)
		fb_destroy_modedb(info->monspecs.modedb);
	vfree(info->screen_buffer);

	fb_destroy_modelist(&info->modelist);

	while (!list_empty(&dlfb->deferred_free)) {
		struct dlfb_deferred_free *d = list_entry(dlfb->deferred_free.next, struct dlfb_deferred_free, list);
		list_del(&d->list);
		vfree(d->mem);
		kfree(d);
	}
	vfree(dlfb->backing_buffer);
	kfree(dlfb->edid);
	dlfb_free_urb_list(dlfb);
	usb_put_dev(dlfb->udev);
	kfree(dlfb);

	/* Assume info structure is freed after this point */
	framebuffer_release(info);
}

/*
 * Assumes caller is holding info->lock mutex (for open and release at least)
 */
static int dlfb_ops_release(struct fb_info *info, int user)
{
	struct dlfb_data *dlfb = info->par;

	dlfb->fb_count--;

	if ((dlfb->fb_count == 0) && (info->fbdefio)) {
		fb_deferred_io_cleanup(info);
		kfree(info->fbdefio);
		info->fbdefio = NULL;
	}

	dev_dbg(info->dev, "release, user=%d count=%d\n", user, dlfb->fb_count);

	return 0;
}

/*
 * Check whether a video mode is supported by the DisplayLink chip
 * We start from monitor's modes, so don't need to filter that here
 */
static int dlfb_is_valid_mode(struct fb_videomode *mode, struct dlfb_data *dlfb)
{
	if (mode->xres * mode->yres > dlfb->sku_pixel_limit)
		return 0;

	return 1;
}

static void dlfb_var_color_format(struct fb_var_screeninfo *var)
{
	const struct fb_bitfield red = { 11, 5, 0 };
	const struct fb_bitfield green = { 5, 6, 0 };
	const struct fb_bitfield blue = { 0, 5, 0 };

	var->bits_per_pixel = 16;
	var->red = red;
	var->green = green;
	var->blue = blue;
}

static int dlfb_ops_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	struct fb_videomode mode;
	struct dlfb_data *dlfb = info->par;

	/* set device-specific elements of var unrelated to mode */
	dlfb_var_color_format(var);

	fb_var_to_videomode(&mode, var);

	if (!dlfb_is_valid_mode(&mode, dlfb))
		return -EINVAL;

	return 0;
}

static int dlfb_ops_set_par(struct fb_info *info)
{
	struct dlfb_data *dlfb = info->par;
	int result;
	u16 *pix_framebuffer;
	int i;
	struct fb_var_screeninfo fvs;
	u32 line_length = info->var.xres * (info->var.bits_per_pixel / 8);

	/* clear the activate field because it causes spurious miscompares */
	fvs = info->var;
	fvs.activate = 0;
	fvs.vmode &= ~FB_VMODE_SMOOTH_XPAN;

	if (!memcmp(&dlfb->current_mode, &fvs, sizeof(struct fb_var_screeninfo)))
		return 0;

	result = dlfb_realloc_framebuffer(dlfb, info, info->var.yres * line_length);
	if (result)
		return result;

	result = dlfb_set_video_mode(dlfb, &info->var);

	if (result)
		return result;

	dlfb->current_mode = fvs;
	info->fix.line_length = line_length;

	if (dlfb->fb_count == 0) {

		/* paint greenscreen */

		pix_framebuffer = (u16 *)info->screen_buffer;
		for (i = 0; i < info->fix.smem_len / 2; i++)
			pix_framebuffer[i] = 0x37e6;
	}

	dlfb_handle_damage(dlfb, 0, 0, info->var.xres, info->var.yres);

	return 0;
}

/* To fonzi the jukebox (e.g. make blanking changes take effect) */
static char *dlfb_dummy_render(char *buf)
{
	*buf++ = 0xAF;
	*buf++ = 0x6A; /* copy */
	*buf++ = 0x00; /* from address*/
	*buf++ = 0x00;
	*buf++ = 0x00;
	*buf++ = 0x01; /* one pixel */
	*buf++ = 0x00; /* to address */
	*buf++ = 0x00;
	*buf++ = 0x00;
	return buf;
}

/*
 * In order to come back from full DPMS off, we need to set the mode again
 */
static int dlfb_ops_blank(int blank_mode, struct fb_info *info)
{
	struct dlfb_data *dlfb = info->par;
	char *bufptr;
	struct urb *urb;

	dev_dbg(info->dev, "blank, mode %d --> %d\n",
		dlfb->blank_mode, blank_mode);

	if ((dlfb->blank_mode == FB_BLANK_POWERDOWN) &&
	    (blank_mode != FB_BLANK_POWERDOWN)) {

		/* returning from powerdown requires a fresh modeset */
		dlfb_set_video_mode(dlfb, &info->var);
	}

	urb = dlfb_get_urb(dlfb);
	if (!urb)
		return 0;

	bufptr = (char *) urb->transfer_buffer;
	bufptr = dlfb_vidreg_lock(bufptr);
	bufptr = dlfb_blanking(bufptr, blank_mode);
	bufptr = dlfb_vidreg_unlock(bufptr);

	/* seems like a render op is needed to have blank change take effect */
	bufptr = dlfb_dummy_render(bufptr);

	dlfb_submit_urb(dlfb, urb, bufptr -
			(char *) urb->transfer_buffer);

	dlfb->blank_mode = blank_mode;

	return 0;
}

static void dlfb_ops_damage_range(struct fb_info *info, off_t off, size_t len)
{
	struct dlfb_data *dlfb = info->par;
	int start = max((int)(off / info->fix.line_length), 0);
	int lines = min((u32)((len / info->fix.line_length) + 1), (u32)info->var.yres);

	dlfb_handle_damage(dlfb, 0, start, info->var.xres, lines);
}

static void dlfb_ops_damage_area(struct fb_info *info, u32 x, u32 y, u32 width, u32 height)
{
	struct dlfb_data *dlfb = info->par;

	dlfb_offload_damage(dlfb, x, y, width, height);
}

FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS(dlfb_ops,
				   dlfb_ops_damage_range,
				   dlfb_ops_damage_area)

static const struct fb_ops dlfb_ops = {
	.owner = THIS_MODULE,
	__FB_DEFAULT_DEFERRED_OPS_RDWR(dlfb_ops),
	.fb_setcolreg = dlfb_ops_setcolreg,
	__FB_DEFAULT_DEFERRED_OPS_DRAW(dlfb_ops),
	.fb_mmap = dlfb_ops_mmap,
	.fb_ioctl = dlfb_ops_ioctl,
	.fb_open = dlfb_ops_open,
	.fb_release = dlfb_ops_release,
	.fb_blank = dlfb_ops_blank,
	.fb_check_var = dlfb_ops_check_var,
	.fb_set_par = dlfb_ops_set_par,
	.fb_destroy = dlfb_ops_destroy,
};


static void dlfb_deferred_vfree(struct dlfb_data *dlfb, void *mem)
{
	struct dlfb_deferred_free *d = kmalloc(sizeof(struct dlfb_deferred_free), GFP_KERNEL);
	if (!d)
		return;
	d->mem = mem;
	list_add(&d->list, &dlfb->deferred_free);
}

/*
 * Assumes &info->lock held by caller
 * Assumes no active clients have framebuffer open
 */
static int dlfb_realloc_framebuffer(struct dlfb_data *dlfb, struct fb_info *info, u32 new_len)
{
	u32 old_len = info->fix.smem_len;
	const void *old_fb = info->screen_buffer;
	unsigned char *new_fb;
	unsigned char *new_back = NULL;

	new_len = PAGE_ALIGN(new_len);

	if (new_len > old_len) {
		/*
		 * Alloc system memory for virtual framebuffer
		 */
		new_fb = vmalloc(new_len);
		if (!new_fb) {
			dev_err(info->dev, "Virtual framebuffer alloc failed\n");
			return -ENOMEM;
		}
		memset(new_fb, 0xff, new_len);

		if (info->screen_buffer) {
			memcpy(new_fb, old_fb, old_len);
			dlfb_deferred_vfree(dlfb, info->screen_buffer);
		}

		info->screen_buffer = new_fb;
		info->fix.smem_len = new_len;
		info->fix.smem_start = (unsigned long) new_fb;
		info->flags = udlfb_info_flags;

		/*
		 * Second framebuffer copy to mirror the framebuffer state
		 * on the physical USB device. We can function without this.
		 * But with imperfect damage info we may send pixels over USB
		 * that were, in fact, unchanged - wasting limited USB bandwidth
		 */
		if (shadow)
			new_back = vzalloc(new_len);
		if (!new_back)
			dev_info(info->dev,
				 "No shadow/backing buffer allocated\n");
		else {
			dlfb_deferred_vfree(dlfb, dlfb->backing_buffer);
			dlfb->backing_buffer = new_back;
		}
	}
	return 0;
}

/*
 * 1) Get EDID from hw, or use sw default
 * 2) Parse into various fb_info structs
 * 3) Allocate virtual framebuffer memory to back highest res mode
 *
 * Parses EDID into three places used by various parts of fbdev:
 * fb_var_screeninfo contains the timing of the monitor's preferred mode
 * fb_info.monspecs is full parsed EDID info, including monspecs.modedb
 * fb_info.modelist is a linked list of all monitor & VESA modes which work
 *
 * If EDID is not readable/valid, then modelist is all VESA modes,
 * monspecs is NULL, and fb_var_screeninfo is set to safe VESA mode
 * Returns 0 if successful
 */
static int dlfb_setup_modes(struct dlfb_data *dlfb,
			   struct fb_info *info,
			   char *default_edid, size_t default_edid_size)
{
	char *edid;
	int i, result = 0, tries = 3;
	struct device *dev = info->device;
	struct fb_videomode *mode;
	const struct fb_videomode *default_vmode = NULL;

	if (info->dev) {
		/* only use mutex if info has been registered */
		mutex_lock(&info->lock);
		/* parent device is used otherwise */
		dev = info->dev;
	}

	edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!edid) {
		result = -ENOMEM;
		goto error;
	}

	fb_destroy_modelist(&info->modelist);
	memset(&info->monspecs, 0, sizeof(info->monspecs));

	/*
	 * Try to (re)read EDID from hardware first
	 * EDID data may return, but not parse as valid
	 * Try again a few times, in case of e.g. analog cable noise
	 */
	while (tries--) {

		i = dlfb_get_edid(dlfb, edid, EDID_LENGTH);

		if (i >= EDID_LENGTH)
			fb_edid_to_monspecs(edid, &info->monspecs);

		if (info->monspecs.modedb_len > 0) {
			dlfb->edid = edid;
			dlfb->edid_size = i;
			break;
		}
	}

	/* If that fails, use a previously returned EDID if available */
	if (info->monspecs.modedb_len == 0) {
		dev_err(dev, "Unable to get valid EDID from device/display\n");

		if (dlfb->edid) {
			fb_edid_to_monspecs(dlfb->edid, &info->monspecs);
			if (info->monspecs.modedb_len > 0)
				dev_err(dev, "Using previously queried EDID\n");
		}
	}

	/* If that fails, use the default EDID we were handed */
	if (info->monspecs.modedb_len == 0) {
		if (default_edid_size >= EDID_LENGTH) {
			fb_edid_to_monspecs(default_edid, &info->monspecs);
			if (info->monspecs.modedb_len > 0) {
				memcpy(edid, default_edid, default_edid_size);
				dlfb->edid = edid;
				dlfb->edid_size = default_edid_size;
				dev_err(dev, "Using default/backup EDID\n");
			}
		}
	}

	/* If we've got modes, let's pick a best default mode */
	if (info->monspecs.modedb_len > 0) {

		for (i = 0; i < info->monspecs.modedb_len; i++) {
			mode = &info->monspecs.modedb[i];
			if (dlfb_is_valid_mode(mode, dlfb)) {
				fb_add_videomode(mode, &info->modelist);
			} else {
				dev_dbg(dev, "Specified mode %dx%d too big\n",
					mode->xres, mode->yres);
				if (i == 0)
					/* if we've removed top/best mode */
					info->monspecs.misc
						&= ~FB_MISC_1ST_DETAIL;
			}
		}

		default_vmode = fb_find_best_display(&info->monspecs,
						     &info->modelist);
	}

	/* If everything else has failed, fall back to safe default mode */
	if (default_vmode == NULL) {

		struct fb_videomode fb_vmode = {0};

		/*
		 * Add the standard VESA modes to our modelist
		 * Since we don't have EDID, there may be modes that
		 * overspec monitor and/or are incorrect aspect ratio, etc.
		 * But at least the user has a chance to choose
		 */
		for (i = 0; i < VESA_MODEDB_SIZE; i++) {
			mode = (struct fb_videomode *)&vesa_modes[i];
			if (dlfb_is_valid_mode(mode, dlfb))
				fb_add_videomode(mode, &info->modelist);
			else
				dev_dbg(dev, "VESA mode %dx%d too big\n",
					mode->xres, mode->yres);
		}

		/*
		 * default to resolution safe for projectors
		 * (since they are most common case without EDID)
		 */
		fb_vmode.xres = 800;
		fb_vmode.yres = 600;
		fb_vmode.refresh = 60;
		default_vmode = fb_find_nearest_mode(&fb_vmode,
						     &info->modelist);
	}

	/* If we have good mode and no active clients*/
	if ((default_vmode != NULL) && (dlfb->fb_count == 0)) {

		fb_videomode_to_var(&info->var, default_vmode);
		dlfb_var_color_format(&info->var);

		/*
		 * with mode size info, we can now alloc our framebuffer.
		 */
		memcpy(&info->fix, &dlfb_fix, sizeof(dlfb_fix));
	} else
		result = -EINVAL;

error:
	if (edid && (dlfb->edid != edid))
		kfree(edid);

	if (info->dev)
		mutex_unlock(&info->lock);

	return result;
}

static ssize_t metrics_bytes_rendered_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dlfb = fb_info->par;
	return sysfs_emit(buf, "%u\n",
			atomic_read(&dlfb->bytes_rendered));
}

static ssize_t metrics_bytes_identical_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dlfb = fb_info->par;
	return sysfs_emit(buf, "%u\n",
			atomic_read(&dlfb->bytes_identical));
}

static ssize_t metrics_bytes_sent_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dlfb = fb_info->par;
	return sysfs_emit(buf, "%u\n",
			atomic_read(&dlfb->bytes_sent));
}

static ssize_t metrics_cpu_kcycles_used_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dlfb = fb_info->par;
	return sysfs_emit(buf, "%u\n",
			atomic_read(&dlfb->cpu_kcycles_used));
}

static ssize_t edid_show(
			struct file *filp,
			struct kobject *kobj, struct bin_attribute *a,
			 char *buf, loff_t off, size_t count) {
	struct device *fbdev = kobj_to_dev(kobj);
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dlfb = fb_info->par;

	if (dlfb->edid == NULL)
		return 0;

	if ((off >= dlfb->edid_size) || (count > dlfb->edid_size))
		return 0;

	if (off + count > dlfb->edid_size)
		count = dlfb->edid_size - off;

	memcpy(buf, dlfb->edid, count);

	return count;
}

static ssize_t edid_store(
			struct file *filp,
			struct kobject *kobj, struct bin_attribute *a,
			char *src, loff_t src_off, size_t src_size) {
	struct device *fbdev = kobj_to_dev(kobj);
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dlfb = fb_info->par;
	int ret;

	/* We only support write of entire EDID at once, no offset*/
	if ((src_size != EDID_LENGTH) || (src_off != 0))
		return -EINVAL;

	ret = dlfb_setup_modes(dlfb, fb_info, src, src_size);
	if (ret)
		return ret;

	if (!dlfb->edid || memcmp(src, dlfb->edid, src_size))
		return -EINVAL;

	ret = dlfb_ops_set_par(fb_info);
	if (ret)
		return ret;

	return src_size;
}

static ssize_t metrics_reset_store(struct device *fbdev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dlfb = fb_info->par;

	atomic_set(&dlfb->bytes_rendered, 0);
	atomic_set(&dlfb->bytes_identical, 0);
	atomic_set(&dlfb->bytes_sent, 0);
	atomic_set(&dlfb->cpu_kcycles_used, 0);

	return count;
}

static const struct bin_attribute edid_attr = {
	.attr.name = "edid",
	.attr.mode = 0666,
	.size = EDID_LENGTH,
	.read = edid_show,
	.write = edid_store
};

static const struct device_attribute fb_device_attrs[] = {
	__ATTR_RO(metrics_bytes_rendered),
	__ATTR_RO(metrics_bytes_identical),
	__ATTR_RO(metrics_bytes_sent),
	__ATTR_RO(metrics_cpu_kcycles_used),
	__ATTR(metrics_reset, S_IWUSR, NULL, metrics_reset_store),
};

/*
 * This is necessary before we can communicate with the display controller.
 */
static int dlfb_select_std_channel(struct dlfb_data *dlfb)
{
	int ret;
	static const u8 set_def_chn[] = {
				0x57, 0xCD, 0xDC, 0xA7,
				0x1C, 0x88, 0x5E, 0x15,
				0x60, 0xFE, 0xC6, 0x97,
				0x16, 0x3D, 0x47, 0xF2  };

	ret = usb_control_msg_send(dlfb->udev, 0, NR_USB_REQUEST_CHANNEL,
			(USB_DIR_OUT | USB_TYPE_VENDOR), 0, 0,
			&set_def_chn, sizeof(set_def_chn), USB_CTRL_SET_TIMEOUT,
			GFP_KERNEL);

	return ret;
}

static int dlfb_parse_vendor_descriptor(struct dlfb_data *dlfb,
					struct usb_interface *intf)
{
	char *desc;
	char *buf;
	char *desc_end;
	int total_len;

	buf = kzalloc(MAX_VENDOR_DESCRIPTOR_SIZE, GFP_KERNEL);
	if (!buf)
		return false;
	desc = buf;

	total_len = usb_get_descriptor(interface_to_usbdev(intf),
					0x5f, /* vendor specific */
					0, desc, MAX_VENDOR_DESCRIPTOR_SIZE);

	/* if not found, look in configuration descriptor */
	if (total_len < 0) {
		if (0 == usb_get_extra_descriptor(intf->cur_altsetting,
			0x5f, &desc))
			total_len = (int) desc[0];
	}

	if (total_len > 5) {
		dev_info(&intf->dev,
			 "vendor descriptor length: %d data: %11ph\n",
			 total_len, desc);

		if ((desc[0] != total_len) || /* descriptor length */
		    (desc[1] != 0x5f) ||   /* vendor descriptor type */
		    (desc[2] != 0x01) ||   /* version (2 bytes) */
		    (desc[3] != 0x00) ||
		    (desc[4] != total_len - 2)) /* length after type */
			goto unrecognized;

		desc_end = desc + total_len;
		desc += 5; /* the fixed header we've already parsed */

		while (desc < desc_end) {
			u8 length;
			u16 key;

			key = *desc++;
			key |= (u16)*desc++ << 8;
			length = *desc++;

			switch (key) {
			case 0x0200: { /* max_area */
				u32 max_area = *desc++;
				max_area |= (u32)*desc++ << 8;
				max_area |= (u32)*desc++ << 16;
				max_area |= (u32)*desc++ << 24;
				dev_warn(&intf->dev,
					 "DL chip limited to %d pixel modes\n",
					 max_area);
				dlfb->sku_pixel_limit = max_area;
				break;
			}
			default:
				break;
			}
			desc += length;
		}
	} else {
		dev_info(&intf->dev, "vendor descriptor not available (%d)\n",
			 total_len);
	}

	goto success;

unrecognized:
	/* allow udlfb to load for now even if firmware unrecognized */
	dev_err(&intf->dev, "Unrecognized vendor firmware descriptor\n");

success:
	kfree(buf);
	return true;
}

static int dlfb_usb_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	int i;
	const struct device_attribute *attr;
	struct dlfb_data *dlfb;
	struct fb_info *info;
	int retval;
	struct usb_device *usbdev = interface_to_usbdev(intf);
	static u8 out_ep[] = {OUT_EP_NUM + USB_DIR_OUT, 0};

	/* usb initialization */
	dlfb = kzalloc(sizeof(*dlfb), GFP_KERNEL);
	if (!dlfb) {
		dev_err(&intf->dev, "%s: failed to allocate dlfb\n", __func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&dlfb->deferred_free);

	dlfb->udev = usb_get_dev(usbdev);
	usb_set_intfdata(intf, dlfb);

	if (!usb_check_bulk_endpoints(intf, out_ep)) {
		dev_err(&intf->dev, "Invalid DisplayLink device!\n");
		retval = -EINVAL;
		goto error;
	}

	dev_dbg(&intf->dev, "console enable=%d\n", console);
	dev_dbg(&intf->dev, "fb_defio enable=%d\n", fb_defio);
	dev_dbg(&intf->dev, "shadow enable=%d\n", shadow);

	dlfb->sku_pixel_limit = 2048 * 1152; /* default to maximum */

	if (!dlfb_parse_vendor_descriptor(dlfb, intf)) {
		dev_err(&intf->dev,
			"firmware not recognized, incompatible device?\n");
		retval = -ENODEV;
		goto error;
	}

	if (pixel_limit) {
		dev_warn(&intf->dev,
			 "DL chip limit of %d overridden to %d\n",
			 dlfb->sku_pixel_limit, pixel_limit);
		dlfb->sku_pixel_limit = pixel_limit;
	}


	/* allocates framebuffer driver structure, not framebuffer memory */
	info = framebuffer_alloc(0, &dlfb->udev->dev);
	if (!info) {
		retval = -ENOMEM;
		goto error;
	}

	dlfb->info = info;
	info->par = dlfb;
	info->pseudo_palette = dlfb->pseudo_palette;
	dlfb->ops = dlfb_ops;
	info->fbops = &dlfb->ops;

	mutex_init(&dlfb->render_mutex);
	dlfb_init_damage(dlfb);
	spin_lock_init(&dlfb->damage_lock);
	INIT_WORK(&dlfb->damage_work, dlfb_damage_work);

	INIT_LIST_HEAD(&info->modelist);

	if (!dlfb_alloc_urb_list(dlfb, WRITES_IN_FLIGHT, MAX_TRANSFER)) {
		retval = -ENOMEM;
		dev_err(&intf->dev, "unable to allocate urb list\n");
		goto error;
	}

	/* We don't register a new USB class. Our client interface is dlfbev */

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0) {
		dev_err(info->device, "cmap allocation failed: %d\n", retval);
		goto error;
	}

	retval = dlfb_setup_modes(dlfb, info, NULL, 0);
	if (retval != 0) {
		dev_err(info->device,
			"unable to find common mode for display and adapter\n");
		goto error;
	}

	/* ready to begin using device */

	atomic_set(&dlfb->usb_active, 1);
	dlfb_select_std_channel(dlfb);

	dlfb_ops_check_var(&info->var, info);
	retval = dlfb_ops_set_par(info);
	if (retval)
		goto error;

	retval = register_framebuffer(info);
	if (retval < 0) {
		dev_err(info->device, "unable to register framebuffer: %d\n",
			retval);
		goto error;
	}

	for (i = 0; i < ARRAY_SIZE(fb_device_attrs); i++) {
		attr = &fb_device_attrs[i];
		retval = device_create_file(info->dev, attr);
		if (retval)
			dev_warn(info->device,
				 "failed to create '%s' attribute: %d\n",
				 attr->attr.name, retval);
	}

	retval = device_create_bin_file(info->dev, &edid_attr);
	if (retval)
		dev_warn(info->device, "failed to create '%s' attribute: %d\n",
			 edid_attr.attr.name, retval);

	dev_info(info->device,
		 "%s is DisplayLink USB device (%dx%d, %dK framebuffer memory)\n",
		 dev_name(info->dev), info->var.xres, info->var.yres,
		 ((dlfb->backing_buffer) ?
		 info->fix.smem_len * 2 : info->fix.smem_len) >> 10);
	return 0;

error:
	if (dlfb->info) {
		dlfb_ops_destroy(dlfb->info);
	} else {
		usb_put_dev(dlfb->udev);
		kfree(dlfb);
	}
	return retval;
}

static void dlfb_usb_disconnect(struct usb_interface *intf)
{
	struct dlfb_data *dlfb;
	struct fb_info *info;
	int i;

	dlfb = usb_get_intfdata(intf);
	info = dlfb->info;

	dev_dbg(&intf->dev, "USB disconnect starting\n");

	/* we virtualize until all fb clients release. Then we free */
	dlfb->virtualized = true;

	/* When non-active we'll update virtual framebuffer, but no new urbs */
	atomic_set(&dlfb->usb_active, 0);

	/* this function will wait for all in-flight urbs to complete */
	dlfb_free_urb_list(dlfb);

	/* remove udlfb's sysfs interfaces */
	for (i = 0; i < ARRAY_SIZE(fb_device_attrs); i++)
		device_remove_file(info->dev, &fb_device_attrs[i]);
	device_remove_bin_file(info->dev, &edid_attr);

	unregister_framebuffer(info);
}

static struct usb_driver dlfb_driver = {
	.name = "udlfb",
	.probe = dlfb_usb_probe,
	.disconnect = dlfb_usb_disconnect,
	.id_table = id_table,
};

module_usb_driver(dlfb_driver);

static void dlfb_urb_completion(struct urb *urb)
{
	struct urb_node *unode = urb->context;
	struct dlfb_data *dlfb = unode->dlfb;
	unsigned long flags;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* sync/async unlink faults aren't errors */
		break;
	default:
		dev_err(&dlfb->udev->dev,
			"%s - nonzero write bulk status received: %d\n",
			__func__, urb->status);
		atomic_set(&dlfb->lost_pixels, 1);
		break;
	}

	urb->transfer_buffer_length = dlfb->urbs.size; /* reset to actual */

	spin_lock_irqsave(&dlfb->urbs.lock, flags);
	list_add_tail(&unode->entry, &dlfb->urbs.list);
	dlfb->urbs.available++;
	spin_unlock_irqrestore(&dlfb->urbs.lock, flags);

	up(&dlfb->urbs.limit_sem);
}

static void dlfb_free_urb_list(struct dlfb_data *dlfb)
{
	int count = dlfb->urbs.count;
	struct list_head *node;
	struct urb_node *unode;
	struct urb *urb;

	/* keep waiting and freeing, until we've got 'em all */
	while (count--) {
		down(&dlfb->urbs.limit_sem);

		spin_lock_irq(&dlfb->urbs.lock);

		node = dlfb->urbs.list.next; /* have reserved one with sem */
		list_del_init(node);

		spin_unlock_irq(&dlfb->urbs.lock);

		unode = list_entry(node, struct urb_node, entry);
		urb = unode->urb;

		/* Free each separately allocated piece */
		usb_free_coherent(urb->dev, dlfb->urbs.size,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		kfree(node);
	}

	dlfb->urbs.count = 0;
}

static int dlfb_alloc_urb_list(struct dlfb_data *dlfb, int count, size_t size)
{
	struct urb *urb;
	struct urb_node *unode;
	char *buf;
	size_t wanted_size = count * size;

	spin_lock_init(&dlfb->urbs.lock);

retry:
	dlfb->urbs.size = size;
	INIT_LIST_HEAD(&dlfb->urbs.list);

	sema_init(&dlfb->urbs.limit_sem, 0);
	dlfb->urbs.count = 0;
	dlfb->urbs.available = 0;

	while (dlfb->urbs.count * size < wanted_size) {
		unode = kzalloc(sizeof(*unode), GFP_KERNEL);
		if (!unode)
			break;
		unode->dlfb = dlfb;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			kfree(unode);
			break;
		}
		unode->urb = urb;

		buf = usb_alloc_coherent(dlfb->udev, size, GFP_KERNEL,
					 &urb->transfer_dma);
		if (!buf) {
			kfree(unode);
			usb_free_urb(urb);
			if (size > PAGE_SIZE) {
				size /= 2;
				dlfb_free_urb_list(dlfb);
				goto retry;
			}
			break;
		}

		/* urb->transfer_buffer_length set to actual before submit */
		usb_fill_bulk_urb(urb, dlfb->udev,
			usb_sndbulkpipe(dlfb->udev, OUT_EP_NUM),
			buf, size, dlfb_urb_completion, unode);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &dlfb->urbs.list);

		up(&dlfb->urbs.limit_sem);
		dlfb->urbs.count++;
		dlfb->urbs.available++;
	}

	return dlfb->urbs.count;
}

static struct urb *dlfb_get_urb(struct dlfb_data *dlfb)
{
	int ret;
	struct list_head *entry;
	struct urb_node *unode;

	/* Wait for an in-flight buffer to complete and get re-queued */
	ret = down_timeout(&dlfb->urbs.limit_sem, GET_URB_TIMEOUT);
	if (ret) {
		atomic_set(&dlfb->lost_pixels, 1);
		dev_warn(&dlfb->udev->dev,
			 "wait for urb interrupted: %d available: %d\n",
			 ret, dlfb->urbs.available);
		return NULL;
	}

	spin_lock_irq(&dlfb->urbs.lock);

	BUG_ON(list_empty(&dlfb->urbs.list)); /* reserved one with limit_sem */
	entry = dlfb->urbs.list.next;
	list_del_init(entry);
	dlfb->urbs.available--;

	spin_unlock_irq(&dlfb->urbs.lock);

	unode = list_entry(entry, struct urb_node, entry);
	return unode->urb;
}

static int dlfb_submit_urb(struct dlfb_data *dlfb, struct urb *urb, size_t len)
{
	int ret;

	BUG_ON(len > dlfb->urbs.size);

	urb->transfer_buffer_length = len; /* set to actual payload len */
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		dlfb_urb_completion(urb); /* because no one else will */
		atomic_set(&dlfb->lost_pixels, 1);
		dev_err(&dlfb->udev->dev, "submit urb error: %d\n", ret);
	}
	return ret;
}

module_param(console, bool, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(console, "Allow fbcon to open framebuffer");

module_param(fb_defio, bool, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(fb_defio, "Page fault detection of mmap writes");

module_param(shadow, bool, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(shadow, "Shadow vid mem. Disable to save mem but lose perf");

module_param(pixel_limit, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(pixel_limit, "Force limit on max mode (in x*y pixels)");

MODULE_AUTHOR("Roberto De Ioris <roberto@unbit.it>, "
	      "Jaya Kumar <jayakumar.lkml@gmail.com>, "
	      "Bernie Thompson <bernie@plugable.com>");
MODULE_DESCRIPTION("DisplayLink kernel framebuffer driver");
MODULE_LICENSE("GPL");

