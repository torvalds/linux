/*
 *  linux/drivers/video/fbmem.c
 *
 *  Copyright (C) 1994 Martin Schaller
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>

#include <linux/compat.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/vt.h>
#include <linux/init.h>
#include <linux/linux_logo.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/kmod.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/efi.h>
#include <linux/fb.h>
#include <linux/fbcon.h>
#include <linux/mem_encrypt.h>
#include <linux/pci.h>

#include <video/nomodeset.h>
#include <video/vga.h>

    /*
     *  Frame buffer device initialization and setup routines
     */

#define FBPIXMAPSIZE	(1024 * 8)

static DEFINE_MUTEX(registration_lock);

struct fb_info *registered_fb[FB_MAX] __read_mostly;
int num_registered_fb __read_mostly;
#define for_each_registered_fb(i)		\
	for (i = 0; i < FB_MAX; i++)		\
		if (!registered_fb[i]) {} else

bool fb_center_logo __read_mostly;

int fb_logo_count __read_mostly = -1;

static struct fb_info *get_fb_info(unsigned int idx)
{
	struct fb_info *fb_info;

	if (idx >= FB_MAX)
		return ERR_PTR(-ENODEV);

	mutex_lock(&registration_lock);
	fb_info = registered_fb[idx];
	if (fb_info)
		refcount_inc(&fb_info->count);
	mutex_unlock(&registration_lock);

	return fb_info;
}

static void put_fb_info(struct fb_info *fb_info)
{
	if (!refcount_dec_and_test(&fb_info->count))
		return;
	if (fb_info->fbops->fb_destroy)
		fb_info->fbops->fb_destroy(fb_info);
}

/*
 * Helpers
 */

int fb_get_color_depth(struct fb_var_screeninfo *var,
		       struct fb_fix_screeninfo *fix)
{
	int depth = 0;

	if (fix->visual == FB_VISUAL_MONO01 ||
	    fix->visual == FB_VISUAL_MONO10)
		depth = 1;
	else {
		if (var->green.length == var->blue.length &&
		    var->green.length == var->red.length &&
		    var->green.offset == var->blue.offset &&
		    var->green.offset == var->red.offset)
			depth = var->green.length;
		else
			depth = var->green.length + var->red.length +
				var->blue.length;
	}

	return depth;
}
EXPORT_SYMBOL(fb_get_color_depth);

/*
 * Data padding functions.
 */
void fb_pad_aligned_buffer(u8 *dst, u32 d_pitch, u8 *src, u32 s_pitch, u32 height)
{
	__fb_pad_aligned_buffer(dst, d_pitch, src, s_pitch, height);
}
EXPORT_SYMBOL(fb_pad_aligned_buffer);

void fb_pad_unaligned_buffer(u8 *dst, u32 d_pitch, u8 *src, u32 idx, u32 height,
				u32 shift_high, u32 shift_low, u32 mod)
{
	u8 mask = (u8) (0xfff << shift_high), tmp;
	int i, j;

	for (i = height; i--; ) {
		for (j = 0; j < idx; j++) {
			tmp = dst[j];
			tmp &= mask;
			tmp |= *src >> shift_low;
			dst[j] = tmp;
			tmp = *src << shift_high;
			dst[j+1] = tmp;
			src++;
		}
		tmp = dst[idx];
		tmp &= mask;
		tmp |= *src >> shift_low;
		dst[idx] = tmp;
		if (shift_high < mod) {
			tmp = *src << shift_high;
			dst[idx+1] = tmp;
		}
		src++;
		dst += d_pitch;
	}
}
EXPORT_SYMBOL(fb_pad_unaligned_buffer);

/*
 * we need to lock this section since fb_cursor
 * may use fb_imageblit()
 */
char* fb_get_buffer_offset(struct fb_info *info, struct fb_pixmap *buf, u32 size)
{
	u32 align = buf->buf_align - 1, offset;
	char *addr = buf->addr;

	/* If IO mapped, we need to sync before access, no sharing of
	 * the pixmap is done
	 */
	if (buf->flags & FB_PIXMAP_IO) {
		if (info->fbops->fb_sync && (buf->flags & FB_PIXMAP_SYNC))
			info->fbops->fb_sync(info);
		return addr;
	}

	/* See if we fit in the remaining pixmap space */
	offset = buf->offset + align;
	offset &= ~align;
	if (offset + size > buf->size) {
		/* We do not fit. In order to be able to re-use the buffer,
		 * we must ensure no asynchronous DMA'ing or whatever operation
		 * is in progress, we sync for that.
		 */
		if (info->fbops->fb_sync && (buf->flags & FB_PIXMAP_SYNC))
			info->fbops->fb_sync(info);
		offset = 0;
	}
	buf->offset = offset + size;
	addr += offset;

	return addr;
}
EXPORT_SYMBOL(fb_get_buffer_offset);

#ifdef CONFIG_LOGO

static inline unsigned safe_shift(unsigned d, int n)
{
	return n < 0 ? d >> -n : d << n;
}

static void fb_set_logocmap(struct fb_info *info,
				   const struct linux_logo *logo)
{
	struct fb_cmap palette_cmap;
	u16 palette_green[16];
	u16 palette_blue[16];
	u16 palette_red[16];
	int i, j, n;
	const unsigned char *clut = logo->clut;

	palette_cmap.start = 0;
	palette_cmap.len = 16;
	palette_cmap.red = palette_red;
	palette_cmap.green = palette_green;
	palette_cmap.blue = palette_blue;
	palette_cmap.transp = NULL;

	for (i = 0; i < logo->clutsize; i += n) {
		n = logo->clutsize - i;
		/* palette_cmap provides space for only 16 colors at once */
		if (n > 16)
			n = 16;
		palette_cmap.start = 32 + i;
		palette_cmap.len = n;
		for (j = 0; j < n; ++j) {
			palette_cmap.red[j] = clut[0] << 8 | clut[0];
			palette_cmap.green[j] = clut[1] << 8 | clut[1];
			palette_cmap.blue[j] = clut[2] << 8 | clut[2];
			clut += 3;
		}
		fb_set_cmap(&palette_cmap, info);
	}
}

static void  fb_set_logo_truepalette(struct fb_info *info,
					    const struct linux_logo *logo,
					    u32 *palette)
{
	static const unsigned char mask[] = { 0,0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff };
	unsigned char redmask, greenmask, bluemask;
	int redshift, greenshift, blueshift;
	int i;
	const unsigned char *clut = logo->clut;

	/*
	 * We have to create a temporary palette since console palette is only
	 * 16 colors long.
	 */
	/* Bug: Doesn't obey msb_right ... (who needs that?) */
	redmask   = mask[info->var.red.length   < 8 ? info->var.red.length   : 8];
	greenmask = mask[info->var.green.length < 8 ? info->var.green.length : 8];
	bluemask  = mask[info->var.blue.length  < 8 ? info->var.blue.length  : 8];
	redshift   = info->var.red.offset   - (8 - info->var.red.length);
	greenshift = info->var.green.offset - (8 - info->var.green.length);
	blueshift  = info->var.blue.offset  - (8 - info->var.blue.length);

	for ( i = 0; i < logo->clutsize; i++) {
		palette[i+32] = (safe_shift((clut[0] & redmask), redshift) |
				 safe_shift((clut[1] & greenmask), greenshift) |
				 safe_shift((clut[2] & bluemask), blueshift));
		clut += 3;
	}
}

static void fb_set_logo_directpalette(struct fb_info *info,
					     const struct linux_logo *logo,
					     u32 *palette)
{
	int redshift, greenshift, blueshift;
	int i;

	redshift = info->var.red.offset;
	greenshift = info->var.green.offset;
	blueshift = info->var.blue.offset;

	for (i = 32; i < 32 + logo->clutsize; i++)
		palette[i] = i << redshift | i << greenshift | i << blueshift;
}

static void fb_set_logo(struct fb_info *info,
			       const struct linux_logo *logo, u8 *dst,
			       int depth)
{
	int i, j, k;
	const u8 *src = logo->data;
	u8 xor = (info->fix.visual == FB_VISUAL_MONO01) ? 0xff : 0;
	u8 fg = 1, d;

	switch (fb_get_color_depth(&info->var, &info->fix)) {
	case 1:
		fg = 1;
		break;
	case 2:
		fg = 3;
		break;
	default:
		fg = 7;
		break;
	}

	if (info->fix.visual == FB_VISUAL_MONO01 ||
	    info->fix.visual == FB_VISUAL_MONO10)
		fg = ~((u8) (0xfff << info->var.green.length));

	switch (depth) {
	case 4:
		for (i = 0; i < logo->height; i++)
			for (j = 0; j < logo->width; src++) {
				*dst++ = *src >> 4;
				j++;
				if (j < logo->width) {
					*dst++ = *src & 0x0f;
					j++;
				}
			}
		break;
	case 1:
		for (i = 0; i < logo->height; i++) {
			for (j = 0; j < logo->width; src++) {
				d = *src ^ xor;
				for (k = 7; k >= 0 && j < logo->width; k--) {
					*dst++ = ((d >> k) & 1) ? fg : 0;
					j++;
				}
			}
		}
		break;
	}
}

/*
 * Three (3) kinds of logo maps exist.  linux_logo_clut224 (>16 colors),
 * linux_logo_vga16 (16 colors) and linux_logo_mono (2 colors).  Depending on
 * the visual format and color depth of the framebuffer, the DAC, the
 * pseudo_palette, and the logo data will be adjusted accordingly.
 *
 * Case 1 - linux_logo_clut224:
 * Color exceeds the number of console colors (16), thus we set the hardware DAC
 * using fb_set_cmap() appropriately.  The "needs_cmapreset"  flag will be set.
 *
 * For visuals that require color info from the pseudo_palette, we also construct
 * one for temporary use. The "needs_directpalette" or "needs_truepalette" flags
 * will be set.
 *
 * Case 2 - linux_logo_vga16:
 * The number of colors just matches the console colors, thus there is no need
 * to set the DAC or the pseudo_palette.  However, the bitmap is packed, ie,
 * each byte contains color information for two pixels (upper and lower nibble).
 * To be consistent with fb_imageblit() usage, we therefore separate the two
 * nibbles into separate bytes. The "depth" flag will be set to 4.
 *
 * Case 3 - linux_logo_mono:
 * This is similar with Case 2.  Each byte contains information for 8 pixels.
 * We isolate each bit and expand each into a byte. The "depth" flag will
 * be set to 1.
 */
static struct logo_data {
	int depth;
	int needs_directpalette;
	int needs_truepalette;
	int needs_cmapreset;
	const struct linux_logo *logo;
} fb_logo __read_mostly;

static void fb_rotate_logo_ud(const u8 *in, u8 *out, u32 width, u32 height)
{
	u32 size = width * height, i;

	out += size - 1;

	for (i = size; i--; )
		*out-- = *in++;
}

static void fb_rotate_logo_cw(const u8 *in, u8 *out, u32 width, u32 height)
{
	int i, j, h = height - 1;

	for (i = 0; i < height; i++)
		for (j = 0; j < width; j++)
				out[height * j + h - i] = *in++;
}

static void fb_rotate_logo_ccw(const u8 *in, u8 *out, u32 width, u32 height)
{
	int i, j, w = width - 1;

	for (i = 0; i < height; i++)
		for (j = 0; j < width; j++)
			out[height * (w - j) + i] = *in++;
}

static void fb_rotate_logo(struct fb_info *info, u8 *dst,
			   struct fb_image *image, int rotate)
{
	u32 tmp;

	if (rotate == FB_ROTATE_UD) {
		fb_rotate_logo_ud(image->data, dst, image->width,
				  image->height);
		image->dx = info->var.xres - image->width - image->dx;
		image->dy = info->var.yres - image->height - image->dy;
	} else if (rotate == FB_ROTATE_CW) {
		fb_rotate_logo_cw(image->data, dst, image->width,
				  image->height);
		swap(image->width, image->height);
		tmp = image->dy;
		image->dy = image->dx;
		image->dx = info->var.xres - image->width - tmp;
	} else if (rotate == FB_ROTATE_CCW) {
		fb_rotate_logo_ccw(image->data, dst, image->width,
				   image->height);
		swap(image->width, image->height);
		tmp = image->dx;
		image->dx = image->dy;
		image->dy = info->var.yres - image->height - tmp;
	}

	image->data = dst;
}

static void fb_do_show_logo(struct fb_info *info, struct fb_image *image,
			    int rotate, unsigned int num)
{
	unsigned int x;

	if (image->width > info->var.xres || image->height > info->var.yres)
		return;

	if (rotate == FB_ROTATE_UR) {
		for (x = 0;
		     x < num && image->dx + image->width <= info->var.xres;
		     x++) {
			info->fbops->fb_imageblit(info, image);
			image->dx += image->width + 8;
		}
	} else if (rotate == FB_ROTATE_UD) {
		u32 dx = image->dx;

		for (x = 0; x < num && image->dx <= dx; x++) {
			info->fbops->fb_imageblit(info, image);
			image->dx -= image->width + 8;
		}
	} else if (rotate == FB_ROTATE_CW) {
		for (x = 0;
		     x < num && image->dy + image->height <= info->var.yres;
		     x++) {
			info->fbops->fb_imageblit(info, image);
			image->dy += image->height + 8;
		}
	} else if (rotate == FB_ROTATE_CCW) {
		u32 dy = image->dy;

		for (x = 0; x < num && image->dy <= dy; x++) {
			info->fbops->fb_imageblit(info, image);
			image->dy -= image->height + 8;
		}
	}
}

static int fb_show_logo_line(struct fb_info *info, int rotate,
			     const struct linux_logo *logo, int y,
			     unsigned int n)
{
	u32 *palette = NULL, *saved_pseudo_palette = NULL;
	unsigned char *logo_new = NULL, *logo_rotate = NULL;
	struct fb_image image;

	/* Return if the frame buffer is not mapped or suspended */
	if (logo == NULL || info->state != FBINFO_STATE_RUNNING ||
	    info->fbops->owner)
		return 0;

	image.depth = 8;
	image.data = logo->data;

	if (fb_logo.needs_cmapreset)
		fb_set_logocmap(info, logo);

	if (fb_logo.needs_truepalette ||
	    fb_logo.needs_directpalette) {
		palette = kmalloc(256 * 4, GFP_KERNEL);
		if (palette == NULL)
			return 0;

		if (fb_logo.needs_truepalette)
			fb_set_logo_truepalette(info, logo, palette);
		else
			fb_set_logo_directpalette(info, logo, palette);

		saved_pseudo_palette = info->pseudo_palette;
		info->pseudo_palette = palette;
	}

	if (fb_logo.depth <= 4) {
		logo_new = kmalloc_array(logo->width, logo->height,
					 GFP_KERNEL);
		if (logo_new == NULL) {
			kfree(palette);
			if (saved_pseudo_palette)
				info->pseudo_palette = saved_pseudo_palette;
			return 0;
		}
		image.data = logo_new;
		fb_set_logo(info, logo, logo_new, fb_logo.depth);
	}

	if (fb_center_logo) {
		int xres = info->var.xres;
		int yres = info->var.yres;

		if (rotate == FB_ROTATE_CW || rotate == FB_ROTATE_CCW) {
			xres = info->var.yres;
			yres = info->var.xres;
		}

		while (n && (n * (logo->width + 8) - 8 > xres))
			--n;
		image.dx = (xres - (n * (logo->width + 8) - 8)) / 2;
		image.dy = y ?: (yres - logo->height) / 2;
	} else {
		image.dx = 0;
		image.dy = y;
	}

	image.width = logo->width;
	image.height = logo->height;

	if (rotate) {
		logo_rotate = kmalloc_array(logo->width, logo->height,
					    GFP_KERNEL);
		if (logo_rotate)
			fb_rotate_logo(info, logo_rotate, &image, rotate);
	}

	fb_do_show_logo(info, &image, rotate, n);

	kfree(palette);
	if (saved_pseudo_palette != NULL)
		info->pseudo_palette = saved_pseudo_palette;
	kfree(logo_new);
	kfree(logo_rotate);
	return image.dy + logo->height;
}


#ifdef CONFIG_FB_LOGO_EXTRA

#define FB_LOGO_EX_NUM_MAX 10
static struct logo_data_extra {
	const struct linux_logo *logo;
	unsigned int n;
} fb_logo_ex[FB_LOGO_EX_NUM_MAX];
static unsigned int fb_logo_ex_num;

void fb_append_extra_logo(const struct linux_logo *logo, unsigned int n)
{
	if (!n || fb_logo_ex_num == FB_LOGO_EX_NUM_MAX)
		return;

	fb_logo_ex[fb_logo_ex_num].logo = logo;
	fb_logo_ex[fb_logo_ex_num].n = n;
	fb_logo_ex_num++;
}

static int fb_prepare_extra_logos(struct fb_info *info, unsigned int height,
				  unsigned int yres)
{
	unsigned int i;

	/* FIXME: logo_ex supports only truecolor fb. */
	if (info->fix.visual != FB_VISUAL_TRUECOLOR)
		fb_logo_ex_num = 0;

	for (i = 0; i < fb_logo_ex_num; i++) {
		if (fb_logo_ex[i].logo->type != fb_logo.logo->type) {
			fb_logo_ex[i].logo = NULL;
			continue;
		}
		height += fb_logo_ex[i].logo->height;
		if (height > yres) {
			height -= fb_logo_ex[i].logo->height;
			fb_logo_ex_num = i;
			break;
		}
	}
	return height;
}

static int fb_show_extra_logos(struct fb_info *info, int y, int rotate)
{
	unsigned int i;

	for (i = 0; i < fb_logo_ex_num; i++)
		y = fb_show_logo_line(info, rotate,
				      fb_logo_ex[i].logo, y, fb_logo_ex[i].n);

	return y;
}

#else /* !CONFIG_FB_LOGO_EXTRA */

static inline int fb_prepare_extra_logos(struct fb_info *info,
					 unsigned int height,
					 unsigned int yres)
{
	return height;
}

static inline int fb_show_extra_logos(struct fb_info *info, int y, int rotate)
{
	return y;
}

#endif /* CONFIG_FB_LOGO_EXTRA */


int fb_prepare_logo(struct fb_info *info, int rotate)
{
	int depth = fb_get_color_depth(&info->var, &info->fix);
	unsigned int yres;
	int height;

	memset(&fb_logo, 0, sizeof(struct logo_data));

	if (info->flags & FBINFO_MISC_TILEBLITTING ||
	    info->fbops->owner || !fb_logo_count)
		return 0;

	if (info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		depth = info->var.blue.length;
		if (info->var.red.length < depth)
			depth = info->var.red.length;
		if (info->var.green.length < depth)
			depth = info->var.green.length;
	}

	if (info->fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR && depth > 4) {
		/* assume console colormap */
		depth = 4;
	}

	/* Return if no suitable logo was found */
	fb_logo.logo = fb_find_logo(depth);

	if (!fb_logo.logo) {
		return 0;
	}

	if (rotate == FB_ROTATE_UR || rotate == FB_ROTATE_UD)
		yres = info->var.yres;
	else
		yres = info->var.xres;

	if (fb_logo.logo->height > yres) {
		fb_logo.logo = NULL;
		return 0;
	}

	/* What depth we asked for might be different from what we get */
	if (fb_logo.logo->type == LINUX_LOGO_CLUT224)
		fb_logo.depth = 8;
	else if (fb_logo.logo->type == LINUX_LOGO_VGA16)
		fb_logo.depth = 4;
	else
		fb_logo.depth = 1;


	if (fb_logo.depth > 4 && depth > 4) {
		switch (info->fix.visual) {
		case FB_VISUAL_TRUECOLOR:
			fb_logo.needs_truepalette = 1;
			break;
		case FB_VISUAL_DIRECTCOLOR:
			fb_logo.needs_directpalette = 1;
			fb_logo.needs_cmapreset = 1;
			break;
		case FB_VISUAL_PSEUDOCOLOR:
			fb_logo.needs_cmapreset = 1;
			break;
		}
	}

	height = fb_logo.logo->height;
	if (fb_center_logo)
		height += (yres - fb_logo.logo->height) / 2;

	return fb_prepare_extra_logos(info, height, yres);
}

int fb_show_logo(struct fb_info *info, int rotate)
{
	unsigned int count;
	int y;

	if (!fb_logo_count)
		return 0;

	count = fb_logo_count < 0 ? num_online_cpus() : fb_logo_count;
	y = fb_show_logo_line(info, rotate, fb_logo.logo, 0, count);
	y = fb_show_extra_logos(info, y, rotate);

	return y;
}
#else
int fb_prepare_logo(struct fb_info *info, int rotate) { return 0; }
int fb_show_logo(struct fb_info *info, int rotate) { return 0; }
#endif /* CONFIG_LOGO */
EXPORT_SYMBOL(fb_prepare_logo);
EXPORT_SYMBOL(fb_show_logo);

static void *fb_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&registration_lock);
	return (*pos < FB_MAX) ? pos : NULL;
}

static void *fb_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return (*pos < FB_MAX) ? pos : NULL;
}

static void fb_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&registration_lock);
}

static int fb_seq_show(struct seq_file *m, void *v)
{
	int i = *(loff_t *)v;
	struct fb_info *fi = registered_fb[i];

	if (fi)
		seq_printf(m, "%d %s\n", fi->node, fi->fix.id);
	return 0;
}

static const struct seq_operations __maybe_unused proc_fb_seq_ops = {
	.start	= fb_seq_start,
	.next	= fb_seq_next,
	.stop	= fb_seq_stop,
	.show	= fb_seq_show,
};

/*
 * We hold a reference to the fb_info in file->private_data,
 * but if the current registered fb has changed, we don't
 * actually want to use it.
 *
 * So look up the fb_info using the inode minor number,
 * and just verify it against the reference we have.
 */
static struct fb_info *file_fb_info(struct file *file)
{
	struct inode *inode = file_inode(file);
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];

	if (info != file->private_data)
		info = NULL;
	return info;
}

static ssize_t
fb_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct fb_info *info = file_fb_info(file);

	if (!info)
		return -ENODEV;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	if (info->fbops->fb_read)
		return info->fbops->fb_read(info, buf, count, ppos);

	return fb_io_read(info, buf, count, ppos);
}

static ssize_t
fb_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct fb_info *info = file_fb_info(file);

	if (!info)
		return -ENODEV;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	if (info->fbops->fb_write)
		return info->fbops->fb_write(info, buf, count, ppos);

	return fb_io_write(info, buf, count, ppos);
}

int
fb_pan_display(struct fb_info *info, struct fb_var_screeninfo *var)
{
	struct fb_fix_screeninfo *fix = &info->fix;
	unsigned int yres = info->var.yres;
	int err = 0;

	if (var->yoffset > 0) {
		if (var->vmode & FB_VMODE_YWRAP) {
			if (!fix->ywrapstep || (var->yoffset % fix->ywrapstep))
				err = -EINVAL;
			else
				yres = 0;
		} else if (!fix->ypanstep || (var->yoffset % fix->ypanstep))
			err = -EINVAL;
	}

	if (var->xoffset > 0 && (!fix->xpanstep ||
				 (var->xoffset % fix->xpanstep)))
		err = -EINVAL;

	if (err || !info->fbops->fb_pan_display ||
	    var->yoffset > info->var.yres_virtual - yres ||
	    var->xoffset > info->var.xres_virtual - info->var.xres)
		return -EINVAL;

	if ((err = info->fbops->fb_pan_display(var, info)))
		return err;
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}
EXPORT_SYMBOL(fb_pan_display);

static int fb_check_caps(struct fb_info *info, struct fb_var_screeninfo *var,
			 u32 activate)
{
	struct fb_blit_caps caps, fbcaps;
	int err = 0;

	memset(&caps, 0, sizeof(caps));
	memset(&fbcaps, 0, sizeof(fbcaps));
	caps.flags = (activate & FB_ACTIVATE_ALL) ? 1 : 0;
	fbcon_get_requirement(info, &caps);
	info->fbops->fb_get_caps(info, &fbcaps, var);

	if (((fbcaps.x ^ caps.x) & caps.x) ||
	    ((fbcaps.y ^ caps.y) & caps.y) ||
	    (fbcaps.len < caps.len))
		err = -EINVAL;

	return err;
}

int
fb_set_var(struct fb_info *info, struct fb_var_screeninfo *var)
{
	int ret = 0;
	u32 activate;
	struct fb_var_screeninfo old_var;
	struct fb_videomode mode;
	struct fb_event event;
	u32 unused;

	if (var->activate & FB_ACTIVATE_INV_MODE) {
		struct fb_videomode mode1, mode2;

		fb_var_to_videomode(&mode1, var);
		fb_var_to_videomode(&mode2, &info->var);
		/* make sure we don't delete the videomode of current var */
		ret = fb_mode_is_equal(&mode1, &mode2);
		if (!ret) {
			ret = fbcon_mode_deleted(info, &mode1);
			if (!ret)
				fb_delete_videomode(&mode1, &info->modelist);
		}

		return ret ? -EINVAL : 0;
	}

	if (!(var->activate & FB_ACTIVATE_FORCE) &&
	    !memcmp(&info->var, var, sizeof(struct fb_var_screeninfo)))
		return 0;

	activate = var->activate;

	/* When using FOURCC mode, make sure the red, green, blue and
	 * transp fields are set to 0.
	 */
	if ((info->fix.capabilities & FB_CAP_FOURCC) &&
	    var->grayscale > 1) {
		if (var->red.offset     || var->green.offset    ||
		    var->blue.offset    || var->transp.offset   ||
		    var->red.length     || var->green.length    ||
		    var->blue.length    || var->transp.length   ||
		    var->red.msb_right  || var->green.msb_right ||
		    var->blue.msb_right || var->transp.msb_right)
			return -EINVAL;
	}

	if (!info->fbops->fb_check_var) {
		*var = info->var;
		return 0;
	}

	/* bitfill_aligned() assumes that it's at least 8x8 */
	if (var->xres < 8 || var->yres < 8)
		return -EINVAL;

	/* Too huge resolution causes multiplication overflow. */
	if (check_mul_overflow(var->xres, var->yres, &unused) ||
	    check_mul_overflow(var->xres_virtual, var->yres_virtual, &unused))
		return -EINVAL;

	ret = info->fbops->fb_check_var(var, info);

	if (ret)
		return ret;

	/* verify that virtual resolution >= physical resolution */
	if (var->xres_virtual < var->xres ||
	    var->yres_virtual < var->yres) {
		pr_warn("WARNING: fbcon: Driver '%s' missed to adjust virtual screen size (%ux%u vs. %ux%u)\n",
			info->fix.id,
			var->xres_virtual, var->yres_virtual,
			var->xres, var->yres);
		return -EINVAL;
	}

	if ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW)
		return 0;

	if (info->fbops->fb_get_caps) {
		ret = fb_check_caps(info, var, activate);

		if (ret)
			return ret;
	}

	old_var = info->var;
	info->var = *var;

	if (info->fbops->fb_set_par) {
		ret = info->fbops->fb_set_par(info);

		if (ret) {
			info->var = old_var;
			printk(KERN_WARNING "detected "
				"fb_set_par error, "
				"error code: %d\n", ret);
			return ret;
		}
	}

	fb_pan_display(info, &info->var);
	fb_set_cmap(&info->cmap, info);
	fb_var_to_videomode(&mode, &info->var);

	if (info->modelist.prev && info->modelist.next &&
	    !list_empty(&info->modelist))
		ret = fb_add_videomode(&mode, &info->modelist);

	if (ret)
		return ret;

	event.info = info;
	event.data = &mode;
	fb_notifier_call_chain(FB_EVENT_MODE_CHANGE, &event);

	return 0;
}
EXPORT_SYMBOL(fb_set_var);

int
fb_blank(struct fb_info *info, int blank)
{
	struct fb_event event;
	int ret = -EINVAL;

	if (blank > FB_BLANK_POWERDOWN)
		blank = FB_BLANK_POWERDOWN;

	event.info = info;
	event.data = &blank;

	if (info->fbops->fb_blank)
		ret = info->fbops->fb_blank(blank, info);

	if (!ret)
		fb_notifier_call_chain(FB_EVENT_BLANK, &event);

	return ret;
}
EXPORT_SYMBOL(fb_blank);

static long do_fb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	const struct fb_ops *fb;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct fb_cmap cmap_from;
	struct fb_cmap_user cmap;
	void __user *argp = (void __user *)arg;
	long ret = 0;

	switch (cmd) {
	case FBIOGET_VSCREENINFO:
		lock_fb_info(info);
		var = info->var;
		unlock_fb_info(info);

		ret = copy_to_user(argp, &var, sizeof(var)) ? -EFAULT : 0;
		break;
	case FBIOPUT_VSCREENINFO:
		if (copy_from_user(&var, argp, sizeof(var)))
			return -EFAULT;
		/* only for kernel-internal use */
		var.activate &= ~FB_ACTIVATE_KD_TEXT;
		console_lock();
		lock_fb_info(info);
		ret = fbcon_modechange_possible(info, &var);
		if (!ret)
			ret = fb_set_var(info, &var);
		if (!ret)
			fbcon_update_vcs(info, var.activate & FB_ACTIVATE_ALL);
		unlock_fb_info(info);
		console_unlock();
		if (!ret && copy_to_user(argp, &var, sizeof(var)))
			ret = -EFAULT;
		break;
	case FBIOGET_FSCREENINFO:
		lock_fb_info(info);
		memcpy(&fix, &info->fix, sizeof(fix));
		if (info->flags & FBINFO_HIDE_SMEM_START)
			fix.smem_start = 0;
		unlock_fb_info(info);

		ret = copy_to_user(argp, &fix, sizeof(fix)) ? -EFAULT : 0;
		break;
	case FBIOPUTCMAP:
		if (copy_from_user(&cmap, argp, sizeof(cmap)))
			return -EFAULT;
		ret = fb_set_user_cmap(&cmap, info);
		break;
	case FBIOGETCMAP:
		if (copy_from_user(&cmap, argp, sizeof(cmap)))
			return -EFAULT;
		lock_fb_info(info);
		cmap_from = info->cmap;
		unlock_fb_info(info);
		ret = fb_cmap_to_user(&cmap_from, &cmap);
		break;
	case FBIOPAN_DISPLAY:
		if (copy_from_user(&var, argp, sizeof(var)))
			return -EFAULT;
		console_lock();
		lock_fb_info(info);
		ret = fb_pan_display(info, &var);
		unlock_fb_info(info);
		console_unlock();
		if (ret == 0 && copy_to_user(argp, &var, sizeof(var)))
			return -EFAULT;
		break;
	case FBIO_CURSOR:
		ret = -EINVAL;
		break;
	case FBIOGET_CON2FBMAP:
		ret = fbcon_get_con2fb_map_ioctl(argp);
		break;
	case FBIOPUT_CON2FBMAP:
		ret = fbcon_set_con2fb_map_ioctl(argp);
		break;
	case FBIOBLANK:
		if (arg > FB_BLANK_POWERDOWN)
			return -EINVAL;
		console_lock();
		lock_fb_info(info);
		ret = fb_blank(info, arg);
		/* might again call into fb_blank */
		fbcon_fb_blanked(info, arg);
		unlock_fb_info(info);
		console_unlock();
		break;
	default:
		lock_fb_info(info);
		fb = info->fbops;
		if (fb->fb_ioctl)
			ret = fb->fb_ioctl(info, cmd, arg);
		else
			ret = -ENOTTY;
		unlock_fb_info(info);
	}
	return ret;
}

static long fb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct fb_info *info = file_fb_info(file);

	if (!info)
		return -ENODEV;
	return do_fb_ioctl(info, cmd, arg);
}

#ifdef CONFIG_COMPAT
struct fb_fix_screeninfo32 {
	char			id[16];
	compat_caddr_t		smem_start;
	u32			smem_len;
	u32			type;
	u32			type_aux;
	u32			visual;
	u16			xpanstep;
	u16			ypanstep;
	u16			ywrapstep;
	u32			line_length;
	compat_caddr_t		mmio_start;
	u32			mmio_len;
	u32			accel;
	u16			reserved[3];
};

struct fb_cmap32 {
	u32			start;
	u32			len;
	compat_caddr_t	red;
	compat_caddr_t	green;
	compat_caddr_t	blue;
	compat_caddr_t	transp;
};

static int fb_getput_cmap(struct fb_info *info, unsigned int cmd,
			  unsigned long arg)
{
	struct fb_cmap32 cmap32;
	struct fb_cmap cmap_from;
	struct fb_cmap_user cmap;

	if (copy_from_user(&cmap32, compat_ptr(arg), sizeof(cmap32)))
		return -EFAULT;

	cmap = (struct fb_cmap_user) {
		.start	= cmap32.start,
		.len	= cmap32.len,
		.red	= compat_ptr(cmap32.red),
		.green	= compat_ptr(cmap32.green),
		.blue	= compat_ptr(cmap32.blue),
		.transp	= compat_ptr(cmap32.transp),
	};

	if (cmd == FBIOPUTCMAP)
		return fb_set_user_cmap(&cmap, info);

	lock_fb_info(info);
	cmap_from = info->cmap;
	unlock_fb_info(info);

	return fb_cmap_to_user(&cmap_from, &cmap);
}

static int do_fscreeninfo_to_user(struct fb_fix_screeninfo *fix,
				  struct fb_fix_screeninfo32 __user *fix32)
{
	__u32 data;
	int err;

	err = copy_to_user(&fix32->id, &fix->id, sizeof(fix32->id));

	data = (__u32) (unsigned long) fix->smem_start;
	err |= put_user(data, &fix32->smem_start);

	err |= put_user(fix->smem_len, &fix32->smem_len);
	err |= put_user(fix->type, &fix32->type);
	err |= put_user(fix->type_aux, &fix32->type_aux);
	err |= put_user(fix->visual, &fix32->visual);
	err |= put_user(fix->xpanstep, &fix32->xpanstep);
	err |= put_user(fix->ypanstep, &fix32->ypanstep);
	err |= put_user(fix->ywrapstep, &fix32->ywrapstep);
	err |= put_user(fix->line_length, &fix32->line_length);

	data = (__u32) (unsigned long) fix->mmio_start;
	err |= put_user(data, &fix32->mmio_start);

	err |= put_user(fix->mmio_len, &fix32->mmio_len);
	err |= put_user(fix->accel, &fix32->accel);
	err |= copy_to_user(fix32->reserved, fix->reserved,
			    sizeof(fix->reserved));

	if (err)
		return -EFAULT;
	return 0;
}

static int fb_get_fscreeninfo(struct fb_info *info, unsigned int cmd,
			      unsigned long arg)
{
	struct fb_fix_screeninfo fix;

	lock_fb_info(info);
	fix = info->fix;
	if (info->flags & FBINFO_HIDE_SMEM_START)
		fix.smem_start = 0;
	unlock_fb_info(info);
	return do_fscreeninfo_to_user(&fix, compat_ptr(arg));
}

static long fb_compat_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct fb_info *info = file_fb_info(file);
	const struct fb_ops *fb;
	long ret = -ENOIOCTLCMD;

	if (!info)
		return -ENODEV;
	fb = info->fbops;
	switch(cmd) {
	case FBIOGET_VSCREENINFO:
	case FBIOPUT_VSCREENINFO:
	case FBIOPAN_DISPLAY:
	case FBIOGET_CON2FBMAP:
	case FBIOPUT_CON2FBMAP:
		arg = (unsigned long) compat_ptr(arg);
		fallthrough;
	case FBIOBLANK:
		ret = do_fb_ioctl(info, cmd, arg);
		break;

	case FBIOGET_FSCREENINFO:
		ret = fb_get_fscreeninfo(info, cmd, arg);
		break;

	case FBIOGETCMAP:
	case FBIOPUTCMAP:
		ret = fb_getput_cmap(info, cmd, arg);
		break;

	default:
		if (fb->fb_compat_ioctl)
			ret = fb->fb_compat_ioctl(info, cmd, arg);
		break;
	}
	return ret;
}
#endif

static int
fb_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct fb_info *info = file_fb_info(file);
	unsigned long mmio_pgoff;
	unsigned long start;
	u32 len;

	if (!info)
		return -ENODEV;
	mutex_lock(&info->mm_lock);

	if (info->fbops->fb_mmap) {
		int res;

		/*
		 * The framebuffer needs to be accessed decrypted, be sure
		 * SME protection is removed ahead of the call
		 */
		vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);
		res = info->fbops->fb_mmap(info, vma);
		mutex_unlock(&info->mm_lock);
		return res;
#if IS_ENABLED(CONFIG_FB_DEFERRED_IO)
	} else if (info->fbdefio) {
		/*
		 * FB deferred I/O wants you to handle mmap in your drivers. At a
		 * minimum, point struct fb_ops.fb_mmap to fb_deferred_io_mmap().
		 */
		dev_warn_once(info->dev, "fbdev mmap not set up for deferred I/O.\n");
		mutex_unlock(&info->mm_lock);
		return -ENODEV;
#endif
	}

	/*
	 * Ugh. This can be either the frame buffer mapping, or
	 * if pgoff points past it, the mmio mapping.
	 */
	start = info->fix.smem_start;
	len = info->fix.smem_len;
	mmio_pgoff = PAGE_ALIGN((start & ~PAGE_MASK) + len) >> PAGE_SHIFT;
	if (vma->vm_pgoff >= mmio_pgoff) {
		if (info->var.accel_flags) {
			mutex_unlock(&info->mm_lock);
			return -EINVAL;
		}

		vma->vm_pgoff -= mmio_pgoff;
		start = info->fix.mmio_start;
		len = info->fix.mmio_len;
	}
	mutex_unlock(&info->mm_lock);

	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	fb_pgprotect(file, vma, start);

	return vm_iomap_memory(vma, start, len);
}

static int
fb_open(struct inode *inode, struct file *file)
__acquires(&info->lock)
__releases(&info->lock)
{
	int fbidx = iminor(inode);
	struct fb_info *info;
	int res = 0;

	info = get_fb_info(fbidx);
	if (!info) {
		request_module("fb%d", fbidx);
		info = get_fb_info(fbidx);
		if (!info)
			return -ENODEV;
	}
	if (IS_ERR(info))
		return PTR_ERR(info);

	lock_fb_info(info);
	if (!try_module_get(info->fbops->owner)) {
		res = -ENODEV;
		goto out;
	}
	file->private_data = info;
	if (info->fbops->fb_open) {
		res = info->fbops->fb_open(info,1);
		if (res)
			module_put(info->fbops->owner);
	}
#ifdef CONFIG_FB_DEFERRED_IO
	if (info->fbdefio)
		fb_deferred_io_open(info, inode, file);
#endif
out:
	unlock_fb_info(info);
	if (res)
		put_fb_info(info);
	return res;
}

static int
fb_release(struct inode *inode, struct file *file)
__acquires(&info->lock)
__releases(&info->lock)
{
	struct fb_info * const info = file->private_data;

	lock_fb_info(info);
#if IS_ENABLED(CONFIG_FB_DEFERRED_IO)
	if (info->fbdefio)
		fb_deferred_io_release(info);
#endif
	if (info->fbops->fb_release)
		info->fbops->fb_release(info,1);
	module_put(info->fbops->owner);
	unlock_fb_info(info);
	put_fb_info(info);
	return 0;
}

#if defined(CONFIG_FB_PROVIDE_GET_FB_UNMAPPED_AREA) && !defined(CONFIG_MMU)
static unsigned long get_fb_unmapped_area(struct file *filp,
				   unsigned long addr, unsigned long len,
				   unsigned long pgoff, unsigned long flags)
{
	struct fb_info * const info = filp->private_data;
	unsigned long fb_size = PAGE_ALIGN(info->fix.smem_len);

	if (pgoff > fb_size || len > fb_size - pgoff)
		return -EINVAL;

	return (unsigned long)info->screen_base + pgoff;
}
#endif

static const struct file_operations fb_fops = {
	.owner =	THIS_MODULE,
	.read =		fb_read,
	.write =	fb_write,
	.unlocked_ioctl = fb_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = fb_compat_ioctl,
#endif
	.mmap =		fb_mmap,
	.open =		fb_open,
	.release =	fb_release,
#if defined(HAVE_ARCH_FB_UNMAPPED_AREA) || \
	(defined(CONFIG_FB_PROVIDE_GET_FB_UNMAPPED_AREA) && \
	 !defined(CONFIG_MMU))
	.get_unmapped_area = get_fb_unmapped_area,
#endif
#ifdef CONFIG_FB_DEFERRED_IO
	.fsync =	fb_deferred_io_fsync,
#endif
	.llseek =	default_llseek,
};

struct class *fb_class;
EXPORT_SYMBOL(fb_class);

static int fb_check_foreignness(struct fb_info *fi)
{
	const bool foreign_endian = fi->flags & FBINFO_FOREIGN_ENDIAN;

	fi->flags &= ~FBINFO_FOREIGN_ENDIAN;

#ifdef __BIG_ENDIAN
	fi->flags |= foreign_endian ? 0 : FBINFO_BE_MATH;
#else
	fi->flags |= foreign_endian ? FBINFO_BE_MATH : 0;
#endif /* __BIG_ENDIAN */

	if (fi->flags & FBINFO_BE_MATH && !fb_be_math(fi)) {
		pr_err("%s: enable CONFIG_FB_BIG_ENDIAN to "
		       "support this framebuffer\n", fi->fix.id);
		return -ENOSYS;
	} else if (!(fi->flags & FBINFO_BE_MATH) && fb_be_math(fi)) {
		pr_err("%s: enable CONFIG_FB_LITTLE_ENDIAN to "
		       "support this framebuffer\n", fi->fix.id);
		return -ENOSYS;
	}

	return 0;
}

static int do_register_framebuffer(struct fb_info *fb_info)
{
	int i;
	struct fb_videomode mode;

	if (fb_check_foreignness(fb_info))
		return -ENOSYS;

	if (num_registered_fb == FB_MAX)
		return -ENXIO;

	num_registered_fb++;
	for (i = 0 ; i < FB_MAX; i++)
		if (!registered_fb[i])
			break;
	fb_info->node = i;
	refcount_set(&fb_info->count, 1);
	mutex_init(&fb_info->lock);
	mutex_init(&fb_info->mm_lock);

	fb_info->dev = device_create(fb_class, fb_info->device,
				     MKDEV(FB_MAJOR, i), NULL, "fb%d", i);
	if (IS_ERR(fb_info->dev)) {
		/* Not fatal */
		printk(KERN_WARNING "Unable to create device for framebuffer %d; errno = %ld\n", i, PTR_ERR(fb_info->dev));
		fb_info->dev = NULL;
	} else
		fb_init_device(fb_info);

	if (fb_info->pixmap.addr == NULL) {
		fb_info->pixmap.addr = kmalloc(FBPIXMAPSIZE, GFP_KERNEL);
		if (fb_info->pixmap.addr) {
			fb_info->pixmap.size = FBPIXMAPSIZE;
			fb_info->pixmap.buf_align = 1;
			fb_info->pixmap.scan_align = 1;
			fb_info->pixmap.access_align = 32;
			fb_info->pixmap.flags = FB_PIXMAP_DEFAULT;
		}
	}
	fb_info->pixmap.offset = 0;

	if (!fb_info->pixmap.blit_x)
		fb_info->pixmap.blit_x = ~(u32)0;

	if (!fb_info->pixmap.blit_y)
		fb_info->pixmap.blit_y = ~(u32)0;

	if (!fb_info->modelist.prev || !fb_info->modelist.next)
		INIT_LIST_HEAD(&fb_info->modelist);

	if (fb_info->skip_vt_switch)
		pm_vt_switch_required(fb_info->dev, false);
	else
		pm_vt_switch_required(fb_info->dev, true);

	fb_var_to_videomode(&mode, &fb_info->var);
	fb_add_videomode(&mode, &fb_info->modelist);
	registered_fb[i] = fb_info;

#ifdef CONFIG_GUMSTIX_AM200EPD
	{
		struct fb_event event;
		event.info = fb_info;
		fb_notifier_call_chain(FB_EVENT_FB_REGISTERED, &event);
	}
#endif

	return fbcon_fb_registered(fb_info);
}

static void unbind_console(struct fb_info *fb_info)
{
	int i = fb_info->node;

	if (WARN_ON(i < 0 || i >= FB_MAX || registered_fb[i] != fb_info))
		return;

	fbcon_fb_unbind(fb_info);
}

static void unlink_framebuffer(struct fb_info *fb_info)
{
	int i;

	i = fb_info->node;
	if (WARN_ON(i < 0 || i >= FB_MAX || registered_fb[i] != fb_info))
		return;

	if (!fb_info->dev)
		return;

	device_destroy(fb_class, MKDEV(FB_MAJOR, i));

	pm_vt_switch_unregister(fb_info->dev);

	unbind_console(fb_info);

	fb_info->dev = NULL;
}

static void do_unregister_framebuffer(struct fb_info *fb_info)
{
	unlink_framebuffer(fb_info);
	if (fb_info->pixmap.addr &&
	    (fb_info->pixmap.flags & FB_PIXMAP_DEFAULT)) {
		kfree(fb_info->pixmap.addr);
		fb_info->pixmap.addr = NULL;
	}

	fb_destroy_modelist(&fb_info->modelist);
	registered_fb[fb_info->node] = NULL;
	num_registered_fb--;
	fb_cleanup_device(fb_info);
#ifdef CONFIG_GUMSTIX_AM200EPD
	{
		struct fb_event event;
		event.info = fb_info;
		fb_notifier_call_chain(FB_EVENT_FB_UNREGISTERED, &event);
	}
#endif
	fbcon_fb_unregistered(fb_info);

	/* this may free fb info */
	put_fb_info(fb_info);
}

/**
 *	register_framebuffer - registers a frame buffer device
 *	@fb_info: frame buffer info structure
 *
 *	Registers a frame buffer device @fb_info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */
int
register_framebuffer(struct fb_info *fb_info)
{
	int ret;

	mutex_lock(&registration_lock);
	ret = do_register_framebuffer(fb_info);
	mutex_unlock(&registration_lock);

	return ret;
}
EXPORT_SYMBOL(register_framebuffer);

/**
 *	unregister_framebuffer - releases a frame buffer device
 *	@fb_info: frame buffer info structure
 *
 *	Unregisters a frame buffer device @fb_info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 *      This function will also notify the framebuffer console
 *      to release the driver.
 *
 *      This is meant to be called within a driver's module_exit()
 *      function. If this is called outside module_exit(), ensure
 *      that the driver implements fb_open() and fb_release() to
 *      check that no processes are using the device.
 */
void
unregister_framebuffer(struct fb_info *fb_info)
{
	mutex_lock(&registration_lock);
	do_unregister_framebuffer(fb_info);
	mutex_unlock(&registration_lock);
}
EXPORT_SYMBOL(unregister_framebuffer);

/**
 *	fb_set_suspend - low level driver signals suspend
 *	@info: framebuffer affected
 *	@state: 0 = resuming, !=0 = suspending
 *
 *	This is meant to be used by low level drivers to
 * 	signal suspend/resume to the core & clients.
 *	It must be called with the console semaphore held
 */
void fb_set_suspend(struct fb_info *info, int state)
{
	WARN_CONSOLE_UNLOCKED();

	if (state) {
		fbcon_suspended(info);
		info->state = FBINFO_STATE_SUSPENDED;
	} else {
		info->state = FBINFO_STATE_RUNNING;
		fbcon_resumed(info);
	}
}
EXPORT_SYMBOL(fb_set_suspend);

/**
 *	fbmem_init - init frame buffer subsystem
 *
 *	Initialize the frame buffer subsystem.
 *
 *	NOTE: This function is _only_ to be called by drivers/char/mem.c.
 *
 */

static int __init
fbmem_init(void)
{
	int ret;

	if (!proc_create_seq("fb", 0, NULL, &proc_fb_seq_ops))
		return -ENOMEM;

	ret = register_chrdev(FB_MAJOR, "fb", &fb_fops);
	if (ret) {
		printk("unable to get major %d for fb devs\n", FB_MAJOR);
		goto err_chrdev;
	}

	fb_class = class_create("graphics");
	if (IS_ERR(fb_class)) {
		ret = PTR_ERR(fb_class);
		pr_warn("Unable to create fb class; errno = %d\n", ret);
		fb_class = NULL;
		goto err_class;
	}

	fb_console_init();

	return 0;

err_class:
	unregister_chrdev(FB_MAJOR, "fb");
err_chrdev:
	remove_proc_entry("fb", NULL);
	return ret;
}

#ifdef MODULE
module_init(fbmem_init);
static void __exit
fbmem_exit(void)
{
	fb_console_exit();

	remove_proc_entry("fb", NULL);
	class_destroy(fb_class);
	unregister_chrdev(FB_MAJOR, "fb");
}

module_exit(fbmem_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Framebuffer base");
#else
subsys_initcall(fbmem_init);
#endif

int fb_new_modelist(struct fb_info *info)
{
	struct fb_var_screeninfo var = info->var;
	struct list_head *pos, *n;
	struct fb_modelist *modelist;
	struct fb_videomode *m, mode;
	int err;

	list_for_each_safe(pos, n, &info->modelist) {
		modelist = list_entry(pos, struct fb_modelist, list);
		m = &modelist->mode;
		fb_videomode_to_var(&var, m);
		var.activate = FB_ACTIVATE_TEST;
		err = fb_set_var(info, &var);
		fb_var_to_videomode(&mode, &var);
		if (err || !fb_mode_is_equal(m, &mode)) {
			list_del(pos);
			kfree(pos);
		}
	}

	if (list_empty(&info->modelist))
		return 1;

	fbcon_new_modelist(info);

	return 0;
}

#if defined(CONFIG_VIDEO_NOMODESET)
bool fb_modesetting_disabled(const char *drvname)
{
	bool fwonly = video_firmware_drivers_only();

	if (fwonly)
		pr_warn("Driver %s not loading because of nomodeset parameter\n",
			drvname);

	return fwonly;
}
EXPORT_SYMBOL(fb_modesetting_disabled);
#endif

MODULE_LICENSE("GPL");
