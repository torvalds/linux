/*
 * udlfb.c -- Framebuffer driver for DisplayLink USB controller
 *
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
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
#include <linux/mutex.h>
#include <linux/vmalloc.h>

#include "udlfb.h"

#define DRIVER_VERSION "DisplayLink Framebuffer Driver 0.4.1"

static struct fb_fix_screeninfo dlfb_fix = {
	.id =           "displaylinkfb",
	.type =         FB_TYPE_PACKED_PIXELS,
	.visual =       FB_VISUAL_TRUECOLOR,
	.xpanstep =     0,
	.ypanstep =     0,
	.ywrapstep =    0,
	.accel =        FB_ACCEL_NONE,
};

/*
 * There are many DisplayLink-based products, all with unique PIDs. We are able
 * to support all volume ones (circa 2009) with a single driver, so we match
 * globally on VID. TODO: Probe() needs to detect when we might be running
 * "future" chips, and bail on those, so a compatible driver can match.
 */
static struct usb_device_id id_table[] = {
	{.idVendor = 0x17e9, .match_flags = USB_DEVICE_ID_MATCH_VENDOR,},
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

/* dlfb keeps a list of urbs for efficient bulk transfers */
static void dlfb_urb_completion(struct urb *urb);
static struct urb *dlfb_get_urb(struct dlfb_data *dev);
static int dlfb_submit_urb(struct dlfb_data *dev, struct urb * urb, size_t len);
static int dlfb_alloc_urb_list(struct dlfb_data *dev, int count, size_t size);
static void dlfb_free_urb_list(struct dlfb_data *dev);

/*
 * Inserts a specific DisplayLink controller command into the provided
 * buffer.
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
 * Once you send this command, the DisplayLink framebuffer gets driven to the
 * display.
 */
static char *dlfb_enable_hvsync(char *buf)
{
	return dlfb_set_register(buf, 0x1F, 0x00);
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
static u16 lfsr16(u16 actual_count)
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
	return dlfb_set_register_16(wrptr, reg, lfsr16(value));
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
static int dlfb_set_video_mode(struct dlfb_data *dev,
				struct fb_var_screeninfo *var)
{
	char *buf;
	char *wrptr;
	int retval = 0;
	int writesize;

	buf = dev->buf;

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
	wrptr = dlfb_set_base8bpp(wrptr, dev->info->fix.smem_len);

	wrptr = dlfb_set_vid_cmds(wrptr, var);
	wrptr = dlfb_enable_hvsync(wrptr);
	wrptr = dlfb_vidreg_unlock(wrptr);

	writesize = wrptr - buf;

	mutex_lock(&dev->bulk_mutex);
	if (!dev->interface) {		/* disconnect() was called */
		mutex_unlock(&dev->bulk_mutex);
		retval = -ENODEV;
		goto error;
	}

	retval = dlfb_bulk_msg(dev, writesize);
	mutex_unlock(&dev->bulk_mutex);
	if (retval) {
		dev_err(&dev->udev->dev, "Problem %d with submit write bulk.\n",
					retval);
		goto error;
	}

	return 0;

error:
	return retval;
}


/*
 * Query EDID from the handware, then hand it off to fbdev's edid parse
 * routine which should give us back a filled in screeninfo structure.
 */
static int dlfb_get_var_from_edid(struct dlfb_data *dev,
					struct fb_var_screeninfo *var)
{
	int ret;

	dlfb_edid(dev);
	ret = fb_parse_edid(dev->edid, var);

	return ret;
}

static int dlfb_ops_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;

	printk("MMAP: %lu %u\n", offset + size, info->fix.smem_len);

	if (offset + size > info->fix.smem_len)
		return -EINVAL;

	pos = (unsigned long)info->fix.smem_start + offset;

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

	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */
	return 0;

}

/* ioctl structure */
struct dloarea {
	int x, y;
	int w, h;
	int x2, y2;
};

static struct usb_driver dlfb_driver;

/* thanks to Henrik Bjerregaard Pedersen for this function */
static char *rle_compress16(uint16_t * src, char *dst, int rem)
{

	int rl;
	uint16_t pix0;
	char *end_if_raw = dst + 6 + 2 * rem;

	dst += 6; /* header will be filled in if RLE is worth it */

	while (rem && dst < end_if_raw) {
		char *start = (char *)src;

		pix0 = *src++;
		rl = 1;
		rem--;
		while (rem && *src == pix0)
			rem--, rl++, src++;
		*dst++ = rl;
		*dst++ = start[1];
		*dst++ = start[0];
	}

	return dst;
}

/*
Thanks to Henrik Bjerregaard Pedersen for rle implementation
and code refactoring. Next step is huffman compression.
*/

static int
image_blit(struct dlfb_data *dev_info, int x, int y, int width, int height,
	   char *data)
{

	int i, j, base;
	int rem = width;
	int ret;

	int firstdiff, thistime;

	char *bufptr;

	if (x + width > dev_info->info->var.xres)
		return -EINVAL;

	if (y + height > dev_info->info->var.yres)
		return -EINVAL;

	mutex_lock(&dev_info->bulk_mutex);

	base =
	    dev_info->base16 + ((dev_info->info->var.xres * 2 * y) + (x * 2));

	data += (dev_info->info->var.xres * 2 * y) + (x * 2);

	/* printk("IMAGE_BLIT\n"); */

	bufptr = dev_info->buf;

	for (i = y; i < y + height; i++) {

		if (dev_info->bufend - bufptr < BUF_HIGH_WATER_MARK) {
			ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);
			bufptr = dev_info->buf;
		}

		rem = width;

		/* printk("WRITING LINE %d\n", i); */

		while (rem) {

			if (dev_info->bufend - bufptr < BUF_HIGH_WATER_MARK) {
				ret =
				    dlfb_bulk_msg(dev_info,
						  bufptr - dev_info->buf);
				bufptr = dev_info->buf;
			}
			/* number of pixels to consider this time */
			thistime = rem;
			if (thistime > 255)
				thistime = 255;

			if (dev_info->backing_buffer) {
				/* find first pixel that has changed */
				firstdiff = -1;
				for (j = 0; j < thistime * 2; j++) {
					if (dev_info->backing_buffer
					    [base - dev_info->base16 + j]
					    != data[j]) {
						firstdiff = j / 2;
						break;
					}
				}

			} else {
				firstdiff = 0;

			}

			if (firstdiff >= 0) {
				char *end_of_rle;

				end_of_rle =
				    rle_compress16((uint16_t *) (data +
								 firstdiff * 2),
						   bufptr,
						   thistime - firstdiff);

				if (end_of_rle <
				    bufptr + 6 + 2 * (thistime - firstdiff)) {
					bufptr[0] = 0xAF;
					bufptr[1] = 0x69;

					bufptr[2] =
					    (char)((base +
						    firstdiff * 2) >> 16);
					bufptr[3] =
					    (char)((base + firstdiff * 2) >> 8);
					bufptr[4] =
					    (char)(base + firstdiff * 2);
					bufptr[5] = thistime - firstdiff;

					bufptr = end_of_rle;

				} else {
					/* fallback to raw (or other?) */
					*bufptr++ = 0xAF;
					*bufptr++ = 0x68;

					*bufptr++ =
					    (char)((base +
						    firstdiff * 2) >> 16);
					*bufptr++ =
					    (char)((base + firstdiff * 2) >> 8);
					*bufptr++ =
					    (char)(base + firstdiff * 2);
					*bufptr++ = thistime - firstdiff;
					for (j = firstdiff * 2;
					     j < thistime * 2; j += 2) {
						*bufptr++ = data[j + 1];
						*bufptr++ = data[j];
					}
				}
			}

			base += thistime * 2;
			data += thistime * 2;
			rem -= thistime;
		}

		if (dev_info->backing_buffer)
			memcpy(dev_info->backing_buffer +
			       (base - dev_info->base16) -
			       (width * 2), data - (width * 2), width * 2);

		base += (dev_info->info->var.xres * 2) - (width * 2);
		data += (dev_info->info->var.xres * 2) - (width * 2);

	}

	if (bufptr > dev_info->buf) {
		ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);
	}

	mutex_unlock(&dev_info->bulk_mutex);

	return base;

}

static int
draw_rect(struct dlfb_data *dev_info, int x, int y, int width, int height,
	  unsigned char red, unsigned char green, unsigned char blue)
{

	int i, j, base;
	int ret;
	unsigned short col =
	    (((((red) & 0xF8) | ((green) >> 5)) & 0xFF) << 8) +
	    (((((green) & 0x1C) << 3) | ((blue) >> 3)) & 0xFF);
	int rem = width;

	char *bufptr;

	if (x + width > dev_info->info->var.xres)
		return -EINVAL;

	if (y + height > dev_info->info->var.yres)
		return -EINVAL;

	mutex_lock(&dev_info->bulk_mutex);

	base = dev_info->base16 + (dev_info->info->var.xres * 2 * y) + (x * 2);

	bufptr = dev_info->buf;

	for (i = y; i < y + height; i++) {

		if (dev_info->backing_buffer) {
			for (j = 0; j < width * 2; j += 2) {
				dev_info->backing_buffer
					[base - dev_info->base16 + j] =
					(char)(col >> 8);
				dev_info->backing_buffer
					[base - dev_info->base16 + j + 1] =
					(char)(col);
			}
		}

		if (dev_info->bufend - bufptr < BUF_HIGH_WATER_MARK) {
			ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);
			bufptr = dev_info->buf;
		}

		rem = width;

		while (rem) {

			if (dev_info->bufend - bufptr < BUF_HIGH_WATER_MARK) {
				ret =
				    dlfb_bulk_msg(dev_info,
						  bufptr - dev_info->buf);
				bufptr = dev_info->buf;
			}

			*bufptr++ = 0xAF;
			*bufptr++ = 0x69;

			*bufptr++ = (char)(base >> 16);
			*bufptr++ = (char)(base >> 8);
			*bufptr++ = (char)(base);

			if (rem > 255) {
				*bufptr++ = 255;
				*bufptr++ = 255;
				rem -= 255;
				base += 255 * 2;
			} else {
				*bufptr++ = rem;
				*bufptr++ = rem;
				base += rem * 2;
				rem = 0;
			}

			*bufptr++ = (char)(col >> 8);
			*bufptr++ = (char)(col);

		}

		base += (dev_info->info->var.xres * 2) - (width * 2);

	}

	if (bufptr > dev_info->buf)
		ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);

	mutex_unlock(&dev_info->bulk_mutex);

	return 1;
}

static void swapfb(struct dlfb_data *dev_info)
{

	int tmpbase;
	char *bufptr;

	mutex_lock(&dev_info->bulk_mutex);

	tmpbase = dev_info->base16;

	dev_info->base16 = dev_info->base16d;
	dev_info->base16d = tmpbase;

	bufptr = dev_info->buf;

	bufptr = dlfb_set_register(bufptr, 0xFF, 0x00);

	/* set addresses */
	bufptr =
	    dlfb_set_register(bufptr, 0x20, (char)(dev_info->base16 >> 16));
	bufptr = dlfb_set_register(bufptr, 0x21, (char)(dev_info->base16 >> 8));
	bufptr = dlfb_set_register(bufptr, 0x22, (char)(dev_info->base16));

	bufptr = dlfb_set_register(bufptr, 0xFF, 0x00);

	dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);

	mutex_unlock(&dev_info->bulk_mutex);
}

static int copyfb(struct dlfb_data *dev_info)
{
	int base;
	int source;
	int rem;
	int i, ret;

	char *bufptr;

	base = dev_info->base16d;

	mutex_lock(&dev_info->bulk_mutex);

	source = dev_info->base16;

	bufptr = dev_info->buf;

	for (i = 0; i < dev_info->info->var.yres; i++) {

		if (dev_info->bufend - bufptr < BUF_HIGH_WATER_MARK) {
			ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);
			bufptr = dev_info->buf;
		}

		rem = dev_info->info->var.xres;

		while (rem) {

			if (dev_info->bufend - bufptr < BUF_HIGH_WATER_MARK) {
				ret =
				    dlfb_bulk_msg(dev_info,
						  bufptr - dev_info->buf);
				bufptr = dev_info->buf;

			}

			*bufptr++ = 0xAF;
			*bufptr++ = 0x6A;

			*bufptr++ = (char)(base >> 16);
			*bufptr++ = (char)(base >> 8);
			*bufptr++ = (char)(base);

			if (rem > 255) {
				*bufptr++ = 255;
				*bufptr++ = (char)(source >> 16);
				*bufptr++ = (char)(source >> 8);
				*bufptr++ = (char)(source);

				rem -= 255;
				base += 255 * 2;
				source += 255 * 2;

			} else {
				*bufptr++ = rem;
				*bufptr++ = (char)(source >> 16);
				*bufptr++ = (char)(source >> 8);
				*bufptr++ = (char)(source);

				base += rem * 2;
				source += rem * 2;
				rem = 0;
			}
		}
	}

	if (bufptr > dev_info->buf)
		ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);

	mutex_unlock(&dev_info->bulk_mutex);

	return 1;

}

static int
copyarea(struct dlfb_data *dev_info, int dx, int dy, int sx, int sy,
	 int width, int height)
{
	int base;
	int source;
	int rem;
	int i, ret;

	char *bufptr;

	if (dx + width > dev_info->info->var.xres)
		return -EINVAL;

	if (dy + height > dev_info->info->var.yres)
		return -EINVAL;

	mutex_lock(&dev_info->bulk_mutex);

	base =
	    dev_info->base16 + (dev_info->info->var.xres * 2 * dy) + (dx * 2);
	source = (dev_info->info->var.xres * 2 * sy) + (sx * 2);

	bufptr = dev_info->buf;

	for (i = sy; i < sy + height; i++) {

		memcpy(dev_info->backing_buffer + base - dev_info->base16,
		       dev_info->backing_buffer + source, width * 2);

		if (dev_info->bufend - bufptr < BUF_HIGH_WATER_MARK) {
			ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);
			bufptr = dev_info->buf;
		}

		rem = width;

		while (rem) {

			if (dev_info->bufend - bufptr < BUF_HIGH_WATER_MARK) {
				ret =
				    dlfb_bulk_msg(dev_info,
						  bufptr - dev_info->buf);
				bufptr = dev_info->buf;
			}

			*bufptr++ = 0xAF;
			*bufptr++ = 0x6A;

			*bufptr++ = (char)(base >> 16);
			*bufptr++ = (char)(base >> 8);
			*bufptr++ = (char)(base);

			if (rem > 255) {
				*bufptr++ = 255;
				*bufptr++ = (char)(source >> 16);
				*bufptr++ = (char)(source >> 8);
				*bufptr++ = (char)(source);

				rem -= 255;
				base += 255 * 2;
				source += 255 * 2;

			} else {
				*bufptr++ = rem;
				*bufptr++ = (char)(source >> 16);
				*bufptr++ = (char)(source >> 8);
				*bufptr++ = (char)(source);

				base += rem * 2;
				source += rem * 2;
				rem = 0;
			}
		}

		base += (dev_info->info->var.xres * 2) - (width * 2);
		source += (dev_info->info->var.xres * 2) - (width * 2);
	}

	if (bufptr > dev_info->buf)
		ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);

	mutex_unlock(&dev_info->bulk_mutex);

	return 1;
}

static void dlfb_ops_copyarea(struct fb_info *info,
			const struct fb_copyarea *area)
{
	struct dlfb_data *dev = info->par;

	copyarea(dev, area->dx, area->dy, area->sx, area->sy, area->width,
		 area->height);
}

static void dlfb_ops_imageblit(struct fb_info *info,
			const struct fb_image *image)
{
	int ret;
	struct dlfb_data *dev = info->par;
	cfb_imageblit(info, image);
	ret =
	    image_blit(dev, image->dx, image->dy, image->width, image->height,
		       info->screen_base);
}

static void dlfb_ops_fillrect(struct fb_info *info,
			  const struct fb_fillrect *region)
{

	unsigned char red, green, blue;
	struct dlfb_data *dev = info->par;

	memcpy(&red, &region->color, 1);
	memcpy(&green, &region->color + 1, 1);
	memcpy(&blue, &region->color + 2, 1);
	draw_rect(dev, region->dx, region->dy, region->width, region->height,
		  red, green, blue);
	/* printk("FILL RECT %d %d !!!\n", region->dx, region->dy); */

}

static void dlfb_get_edid(struct dlfb_data *dev)
{
	int i;
	int ret;
	char rbuf[2];

	for (i = 0; i < sizeof(dev->edid); i++) {
		ret = usb_control_msg(dev->udev,
				    usb_rcvctrlpipe(dev->udev, 0), (0x02),
				    (0x80 | (0x02 << 5)), i << 8, 0xA1, rbuf, 2,
				    0);
		dev->edid[i] = rbuf[1];
	}
}

static int dlfb_ops_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	struct dlfb_data *dev_info = info->par;
	struct dloarea *area = NULL;

	if (cmd == 0xAD) {
		char *edid = (char *)arg;
		dlfb_edid(dev_info);
		if (copy_to_user(edid, dev_info->edid, 128)) {
			return -EFAULT;
		}
		return 0;
	}

	if (cmd == 0xAA || cmd == 0xAB || cmd == 0xAC) {

		area = (struct dloarea *)arg;

		if (area->x < 0)
			area->x = 0;

		if (area->x > info->var.xres)
			area->x = info->var.xres;

		if (area->y < 0)
			area->y = 0;

		if (area->y > info->var.yres)
			area->y = info->var.yres;
	}

	if (cmd == 0xAA) {
		image_blit(dev_info, area->x, area->y, area->w, area->h,
			   info->screen_base);
	}
	if (cmd == 0xAC) {
		copyfb(dev_info);
		image_blit(dev_info, area->x, area->y, area->w, area->h,
			   info->screen_base);
		swapfb(dev_info);
	} else if (cmd == 0xAB) {

		if (area->x2 < 0)
			area->x2 = 0;

		if (area->y2 < 0)
			area->y2 = 0;

		copyarea(dev_info,
			 area->x2, area->y2, area->x, area->y, area->w,
			 area->h);
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

static int dlfb_ops_release(struct fb_info *info, int user)
{
	struct dlfb_data *dev_info = info->par;
	image_blit(dev_info, 0, 0, info->var.xres, info->var.yres,
		   info->screen_base);
	return 0;
}

/*
 * Called when all client interfaces to start transactions have been disabled,
 * and all references to our device instance (dlfb_data) are released.
 * Every transaction must have a reference, so we know are fully spun down
 */
static void dlfb_delete(struct kref *kref)
{
	struct dlfb_data *dev = container_of(kref, struct dlfb_data, kref);

	if (dev->backing_buffer)
		vfree(dev->backing_buffer);

	kfree(dev);
}

/*
 * Check whether a video mode is supported by the DisplayLink chip
 * We start from monitor's modes, so don't need to filter that here
 */
static int dlfb_is_valid_mode(struct fb_videomode *mode,
		struct fb_info *info)
{
	struct dlfb_data *dev = info->par;

	if (mode->xres * mode->yres > dev->sku_pixel_limit)
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

static int dlfb_ops_blank(int blank_mode, struct fb_info *info)
{
	struct dlfb_data *dev_info = info->par;
	char *bufptr = dev_info->buf;

	bufptr = dlfb_set_register(bufptr, 0xFF, 0x00);
	if (blank_mode != FB_BLANK_UNBLANK) {
		bufptr = dlfb_set_register(bufptr, 0x1F, 0x01);
	} else {
		bufptr = dlfb_set_register(bufptr, 0x1F, 0x00);
	}
	bufptr = dlfb_set_register(bufptr, 0xFF, 0xFF);

	dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);

	return 0;
}

static struct fb_ops dlfb_ops = {
	.fb_setcolreg = dlfb_ops_setcolreg,
	.fb_fillrect = dlfb_ops_fillrect,
	.fb_copyarea = dlfb_ops_copyarea,
	.fb_imageblit = dlfb_ops_imageblit,
	.fb_mmap = dlfb_ops_mmap,
	.fb_ioctl = dlfb_ops_ioctl,
	.fb_release = dlfb_ops_release,
	.fb_blank = dlfb_ops_blank,
};

/*
 * Calls dlfb_get_edid() to query the EDID of attached monitor via usb cmds
 * Then parses EDID into three places used by various parts of fbdev:
 * fb_var_screeninfo contains the timing of the monitor's preferred mode
 * fb_info.monspecs is full parsed EDID info, including monspecs.modedb
 * fb_info.modelist is a linked list of all monitor & VESA modes which work
 *
 * If EDID is not readable/valid, then modelist is all VESA modes,
 * monspecs is NULL, and fb_var_screeninfo is set to safe VESA mode
 * Returns 0 if EDID parses successfully
 */
static int dlfb_parse_edid(struct dlfb_data *dev,
			    struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	int i;
	const struct fb_videomode *default_vmode = NULL;
	int result = 0;

	fb_destroy_modelist(&info->modelist);
	memset(&info->monspecs, 0, sizeof(info->monspecs));

	dlfb_get_edid(dev);
	fb_edid_to_monspecs(dev->edid, &info->monspecs);

	if (info->monspecs.modedb_len > 0) {

		for (i = 0; i < info->monspecs.modedb_len; i++) {
			if (dlfb_is_valid_mode(&info->monspecs.modedb[i], info))
				fb_add_videomode(&info->monspecs.modedb[i],
					&info->modelist);
		}

		default_vmode = fb_find_best_display(&info->monspecs,
						     &info->modelist);
	} else {
		struct fb_videomode fb_vmode = {0};

		dl_err("Unable to get valid EDID from device/display\n");
		result = 1;

		/*
		 * Add the standard VESA modes to our modelist
		 * Since we don't have EDID, there may be modes that
		 * overspec monitor and/or are incorrect aspect ratio, etc.
		 * But at least the user has a chance to choose
		 */
		for (i = 0; i < VESA_MODEDB_SIZE; i++) {
			if (dlfb_is_valid_mode((struct fb_videomode *)
						&vesa_modes[i], info))
				fb_add_videomode(&vesa_modes[i],
						 &info->modelist);
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

	fb_videomode_to_var(var, default_vmode);
	dlfb_var_color_format(var);

	return result;
}

static ssize_t metrics_bytes_rendered_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&dev->bytes_rendered));
}

static ssize_t metrics_bytes_identical_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&dev->bytes_identical));
}

static ssize_t metrics_bytes_sent_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&dev->bytes_sent));
}

static ssize_t metrics_cpu_kcycles_used_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&dev->cpu_kcycles_used));
}

static ssize_t metrics_misc_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;
	return snprintf(buf, PAGE_SIZE,
			"Calls to\ndamage: %u\nblit: %u\n"
			"defio faults: %u\ncopy: %u\n"
			"fill: %u\n\n"
			"active framebuffer clients: %d\n"
			"urbs available %d(%d)\n"
			"Shadow framebuffer in use? %s\n"
			"Any lost pixels? %s\n",
			atomic_read(&dev->damage_count),
			atomic_read(&dev->blit_count),
			atomic_read(&dev->defio_fault_count),
			atomic_read(&dev->copy_count),
			atomic_read(&dev->fill_count),
			dev->fb_count,
			dev->urbs.available, dev->urbs.limit_sem.count,
			(dev->backing_buffer) ? "yes" : "no",
			atomic_read(&dev->lost_pixels) ? "yes" : "no");
}

static ssize_t edid_show(struct kobject *kobj, struct bin_attribute *a,
			 char *buf, loff_t off, size_t count) {
	struct device *fbdev = container_of(kobj, struct device, kobj);
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;
	char *edid = &dev->edid[0];
	const size_t size = sizeof(dev->edid);

	if (dlfb_parse_edid(dev, &fb_info->var, fb_info))
		return 0;

	if (off >= size)
		return 0;

	if (off + count > size)
		count = size - off;
	memcpy(buf, edid + off, count);

	return count;
}


static ssize_t metrics_reset_store(struct device *fbdev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;

	atomic_set(&dev->bytes_rendered, 0);
	atomic_set(&dev->bytes_identical, 0);
	atomic_set(&dev->bytes_sent, 0);
	atomic_set(&dev->cpu_kcycles_used, 0);
	atomic_set(&dev->blit_count, 0);
	atomic_set(&dev->copy_count, 0);
	atomic_set(&dev->fill_count, 0);
	atomic_set(&dev->defio_fault_count, 0);
	atomic_set(&dev->damage_count, 0);

	return count;
}

static ssize_t use_defio_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;
	return snprintf(buf, PAGE_SIZE, "%d\n",
			atomic_read(&dev->use_defio));
}

static ssize_t use_defio_store(struct device *fbdev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct dlfb_data *dev = fb_info->par;

	if (count > 0) {
		if (buf[0] == '0')
			atomic_set(&dev->use_defio, 0);
		if (buf[0] == '1')
			atomic_set(&dev->use_defio, 1);
	}
	return count;
}

static struct bin_attribute edid_attr = {
	.attr.name = "edid",
	.attr.mode = 0444,
	.size = 128,
	.read = edid_show,
};

static struct device_attribute fb_device_attrs[] = {
	__ATTR_RO(metrics_bytes_rendered),
	__ATTR_RO(metrics_bytes_identical),
	__ATTR_RO(metrics_bytes_sent),
	__ATTR_RO(metrics_cpu_kcycles_used),
	__ATTR_RO(metrics_misc),
	__ATTR(metrics_reset, S_IWUGO, NULL, metrics_reset_store),
	__ATTR_RW(use_defio),
};

/*
 * This is necessary before we can communicate with the display controller.
 */
static int dlfb_select_std_channel(struct dlfb_data *dev)
{
	int ret;
	u8 set_def_chn[] = {	   0x57, 0xCD, 0xDC, 0xA7,
				0x1C, 0x88, 0x5E, 0x15,
				0x60, 0xFE, 0xC6, 0x97,
				0x16, 0x3D, 0x47, 0xF2  };

	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
			NR_USB_REQUEST_CHANNEL,
			(USB_DIR_OUT | USB_TYPE_VENDOR), 0, 0,
			set_def_chn, sizeof(set_def_chn), USB_CTRL_SET_TIMEOUT);
	return ret;
}

static int dlfb_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct device *mydev;
	struct usb_device *usbdev;
	struct dlfb_data *dev;
	struct fb_info *info;
	int videomemorysize;
	unsigned char *videomemory;
	int retval = -ENOMEM;
	struct fb_var_screeninfo *var;
	struct fb_bitfield red = { 11, 5, 0 };
	struct fb_bitfield green = { 5, 6, 0 };
	struct fb_bitfield blue = { 0, 5, 0 };

	usbdev = usb_get_dev(interface_to_usbdev(interface));
	mydev = &usbdev->dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(mydev, "failed alloc of dev struct\n");
		goto err_devalloc;
	}

	mutex_init(&dev->bulk_mutex);
	dev->udev = usbdev;
	dev->gdev = &usbdev->dev; /* our generic struct device * */
	dev->interface = interface;
	usb_set_intfdata(interface, dev);

	dev_info(mydev, "dlfb_probe: setting up DisplayLink device\n");

	/*
	 * TODO: replace single 64K buffer with buffer list
	 * and async dispatch
	 */
	dev->buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (dev->buf == NULL) {
		dev_err(mydev, "unable to allocate memory for dlfb commands\n");
		goto err_usballoc;
	}
	dev->bufend = dev->buf + BUF_SIZE;

	dev->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	usb_fill_bulk_urb(dev->tx_urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, 1), dev->buf, 0,
			  dlfb_bulk_callback, dev);

	/* allocates framebuffer driver structure, not framebuffer memory */
	info = framebuffer_alloc(0, mydev);
	if (!info)
		goto err_fballoc;

	dev->info = info;
	info->par = dev;
	info->pseudo_palette = dev->pseudo_palette;

	var = &info->var;
	retval = dlfb_get_var_from_edid(dev, var);
	if (retval) {
		/* had a problem getting edid. so fallback to 640x480 */
		dev_err(mydev, "Problem %d with EDID.\n", retval);
		var->xres = 640;
		var->yres = 480;
	}

	/*
	 * ok, now that we've got the size info, we can alloc our framebuffer.
	 * We are using 16bpp.
	 */
	info->var.bits_per_pixel = 16;
	info->fix = dlfb_fix;
	info->fix.line_length = var->xres * (var->bits_per_pixel / 8);
	videomemorysize = info->fix.line_length * var->yres;

	/*
	 * The big chunk of system memory we use as a virtual framebuffer.
	 * Pages don't need to be set RESERVED (non-swap) immediately on 2.6
	 * remap_pfn_page() syscall in our mmap and/or defio will handle.
	 */
	videomemory = vmalloc(videomemorysize);
	if (!videomemory)
		goto err_vidmem;
	memset(videomemory, 0, videomemorysize);

	info->screen_base = videomemory;
	info->fix.smem_len = PAGE_ALIGN(videomemorysize);
	info->fix.smem_start = (unsigned long) videomemory;
	info->flags =
	    FBINFO_DEFAULT | FBINFO_READS_FAST | FBINFO_HWACCEL_IMAGEBLIT |
	    FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT;

	/*
	 * Second framebuffer copy, mirroring the state of the framebuffer
	 * on the physical USB device. We can function without this.
	 * But with imperfect damage info we may end up sending pixels over USB
	 * that were, in fact, unchanged -- wasting limited USB bandwidth
	 */
	dev->backing_buffer = vmalloc(dev->screen_size);
	if (!dev->backing_buffer)
		dev_info(mydev, "No backing buffer allocated!\n");

	info->fbops = &dlfb_ops;

	var->vmode = FB_VMODE_NONINTERLACED;
	var->red = red;
	var->green = green;
	var->blue = blue;

	/*
	 * TODO: Enable FB_CONFIG_DEFIO support

	 info->fbdefio = &dlfb_defio;
	 fb_deferred_io_init(info);

	 */

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0) {
		dev_err(mydev, "Failed to allocate colormap\n");
		goto err_cmap;
	}

	dlfb_select_std_channel(dev);
	dlfb_set_video_mode(dev, var);
	/* TODO: dlfb_dpy_update(dev); */

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err_regfb;

	/* paint "successful" green screen */
	draw_rect(dev, 0, 0, dev->info->var.xres,
		  dev->info->var.yres, 0x30, 0xff, 0x30);

	dev_info(mydev, "DisplayLink USB device %d now attached, "
			"using %dK of memory\n", info->node,
		 ((dev->backing_buffer) ?
		  videomemorysize * 2 : videomemorysize) >> 10);
	return 0;

err_regfb:
	fb_dealloc_cmap(&info->cmap);
err_cmap:
	/* TODO: fb_deferred_io_cleanup(info); */
	vfree(videomemory);
err_vidmem:
	framebuffer_release(info);
err_fballoc:
	kfree(dev->buf);
err_usballoc:
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);
err_devalloc:
	return retval;
}

static void dlfb_disconnect(struct usb_interface *interface)
{
	struct dlfb_data *dev;
	struct fb_info *info;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);

	/*
	 * TODO: since, upon usb disconnect(), usb will cancel in-flight urbs
	 * and error out any new ones, look at eliminating need for mutex
	 */
	mutex_lock(&dev->bulk_mutex);
	dev->interface = NULL;
	info = dev->info;
	mutex_unlock(&dev->bulk_mutex);

	if (info) {
		dev_info(&interface->dev, "Detaching DisplayLink device %d.\n",
						info->node);
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		/* TODO: fb_deferred_io_cleanup(info); */
		fb_dealloc_cmap(&info->cmap);
		vfree((void __force *)info->screen_base);
		framebuffer_release(info);
	}

	if (dev->backing_buffer)
		vfree(dev->backing_buffer);

	kfree(dev);
}

static struct usb_driver dlfb_driver = {
	.name = "udlfb",
	.probe = dlfb_probe,
	.disconnect = dlfb_disconnect,
	.id_table = id_table,
};

static int __init dlfb_init(void)
{
	int res;

	res = usb_register(&dlfb_driver);
	if (res)
		err("usb_register failed. Error number %d", res);

	printk("VMODES initialized\n");

	return res;
}

static void __exit dlfb_exit(void)
{
	usb_deregister(&dlfb_driver);
}

module_init(dlfb_init);
module_exit(dlfb_exit);

static void dlfb_urb_completion(struct urb *urb)
{
	struct urb_node *unode = urb->context;
	struct dlfb_data *dev = unode->dev;
	unsigned long flags;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN)) {
			dl_err("%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);
			atomic_set(&dev->lost_pixels, 1);
		}
	}

	urb->transfer_buffer_length = dev->urbs.size; /* reset to actual */

	spin_lock_irqsave(&dev->urbs.lock, flags);
	list_add_tail(&unode->entry, &dev->urbs.list);
	dev->urbs.available++;
	spin_unlock_irqrestore(&dev->urbs.lock, flags);

	up(&dev->urbs.limit_sem);
}

static void dlfb_free_urb_list(struct dlfb_data *dev)
{
	int count = dev->urbs.count;
	struct list_head *node;
	struct urb_node *unode;
	struct urb *urb;
	int ret;
	unsigned long flags;

	dl_notice("Waiting for completes and freeing all render urbs\n");

	/* keep waiting and freeing, until we've got 'em all */
	while (count--) {
		/* Timeout means a memory leak and/or fault */
		ret = down_timeout(&dev->urbs.limit_sem, FREE_URB_TIMEOUT);
		if (ret) {
			BUG_ON(ret);
			break;
		}
		spin_lock_irqsave(&dev->urbs.lock, flags);

		node = dev->urbs.list.next; /* have reserved one with sem */
		list_del_init(node);

		spin_unlock_irqrestore(&dev->urbs.lock, flags);

		unode = list_entry(node, struct urb_node, entry);
		urb = unode->urb;

		/* Free each separately allocated piece */
		usb_buffer_free(urb->dev, dev->urbs.size,
			urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		kfree(node);
	}

	kref_put(&dev->kref, dlfb_delete);

}

static int dlfb_alloc_urb_list(struct dlfb_data *dev, int count, size_t size)
{
	int i = 0;
	struct urb *urb;
	struct urb_node *unode;
	char *buf;

	spin_lock_init(&dev->urbs.lock);

	dev->urbs.size = size;
	INIT_LIST_HEAD(&dev->urbs.list);

	while (i < count) {
		unode = kzalloc(sizeof(struct urb_node), GFP_KERNEL);
		if (!unode)
			break;
		unode->dev = dev;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			kfree(unode);
			break;
		}
		unode->urb = urb;

		buf = usb_buffer_alloc(dev->udev, MAX_TRANSFER, GFP_KERNEL,
					&urb->transfer_dma);
		if (!buf) {
			kfree(unode);
			usb_free_urb(urb);
			break;
		}

		/* urb->transfer_buffer_length set to actual before submit */
		usb_fill_bulk_urb(urb, dev->udev, usb_sndbulkpipe(dev->udev, 1),
			buf, size, dlfb_urb_completion, unode);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &dev->urbs.list);

		i++;
	}

	sema_init(&dev->urbs.limit_sem, i);
	dev->urbs.count = i;
	dev->urbs.available = i;

	kref_get(&dev->kref); /* released in free_render_urbs() */

	dl_notice("allocated %d %d byte urbs \n", i, (int) size);

	return i;
}

static struct urb *dlfb_get_urb(struct dlfb_data *dev)
{
	int ret = 0;
	struct list_head *entry;
	struct urb_node *unode;
	struct urb *urb = NULL;
	unsigned long flags;

	/* Wait for an in-flight buffer to complete and get re-queued */
	ret = down_timeout(&dev->urbs.limit_sem, GET_URB_TIMEOUT);
	if (ret) {
		atomic_set(&dev->lost_pixels, 1);
		dl_err("wait for urb interrupted: %x\n", ret);
		goto error;
	}

	spin_lock_irqsave(&dev->urbs.lock, flags);

	BUG_ON(list_empty(&dev->urbs.list)); /* reserved one with limit_sem */
	entry = dev->urbs.list.next;
	list_del_init(entry);
	dev->urbs.available--;

	spin_unlock_irqrestore(&dev->urbs.lock, flags);

	unode = list_entry(entry, struct urb_node, entry);
	urb = unode->urb;

error:
	return urb;
}

static int dlfb_submit_urb(struct dlfb_data *dev, struct urb *urb, size_t len)
{
	int ret;

	BUG_ON(len > dev->urbs.size);

	urb->transfer_buffer_length = len; /* set to actual payload len */
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		dlfb_urb_completion(urb); /* because no one else will */
		atomic_set(&dev->lost_pixels, 1);
		dl_err("usb_submit_urb error %x\n", ret);
	}
	return ret;
}

MODULE_AUTHOR("Roberto De Ioris <roberto@unbit.it>, "
	      "Jaya Kumar <jayakumar.lkml@gmail.com>");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
