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
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/vt.h>
#include <linux/init.h>
#include <linux/linux_logo.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/kmod.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/efi.h>
#include <linux/fb.h>

#include <asm/fb.h>


    /*
     *  Frame buffer device initialization and setup routines
     */

#define FBPIXMAPSIZE	(1024 * 8)

struct fb_info *registered_fb[FB_MAX] __read_mostly;
int num_registered_fb __read_mostly;

int lock_fb_info(struct fb_info *info)
{
	mutex_lock(&info->lock);
	if (!info->fbops) {
		mutex_unlock(&info->lock);
		return 0;
	}
	return 1;
}
EXPORT_SYMBOL(lock_fb_info);

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
				for (k = 7; k >= 0; k--) {
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
		tmp = image->width;
		image->width = image->height;
		image->height = tmp;
		tmp = image->dy;
		image->dy = image->dx;
		image->dx = info->var.xres - image->width - tmp;
	} else if (rotate == FB_ROTATE_CCW) {
		fb_rotate_logo_ccw(image->data, dst, image->width,
				   image->height);
		tmp = image->width;
		image->width = image->height;
		image->height = tmp;
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

	if (rotate == FB_ROTATE_UR) {
		for (x = 0;
		     x < num && image->dx + image->width <= info->var.xres;
		     x++) {
			info->fbops->fb_imageblit(info, image);
			image->dx += image->width + 8;
		}
	} else if (rotate == FB_ROTATE_UD) {
		for (x = 0; x < num && image->dx >= 0; x++) {
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
		for (x = 0; x < num && image->dy >= 0; x++) {
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
	    info->flags & FBINFO_MODULE)
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
		logo_new = kmalloc(logo->width * logo->height, GFP_KERNEL);
		if (logo_new == NULL) {
			kfree(palette);
			if (saved_pseudo_palette)
				info->pseudo_palette = saved_pseudo_palette;
			return 0;
		}
		image.data = logo_new;
		fb_set_logo(info, logo, logo_new, fb_logo.depth);
	}

	image.dx = 0;
	image.dy = y;
	image.width = logo->width;
	image.height = logo->height;

	if (rotate) {
		logo_rotate = kmalloc(logo->width *
				      logo->height, GFP_KERNEL);
		if (logo_rotate)
			fb_rotate_logo(info, logo_rotate, &image, rotate);
	}

	fb_do_show_logo(info, &image, rotate, n);

	kfree(palette);
	if (saved_pseudo_palette != NULL)
		info->pseudo_palette = saved_pseudo_palette;
	kfree(logo_new);
	kfree(logo_rotate);
	return logo->height;
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
		y += fb_show_logo_line(info, rotate,
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

	memset(&fb_logo, 0, sizeof(struct logo_data));

	if (info->flags & FBINFO_MISC_TILEBLITTING ||
	    info->flags & FBINFO_MODULE)
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

	return fb_prepare_extra_logos(info, fb_logo.logo->height, yres);
}

int fb_show_logo(struct fb_info *info, int rotate)
{
	int y;

	y = fb_show_logo_line(info, rotate, fb_logo.logo, 0,
			      num_online_cpus());
	y = fb_show_extra_logos(info, y, rotate);

	return y;
}
#else
int fb_prepare_logo(struct fb_info *info, int rotate) { return 0; }
int fb_show_logo(struct fb_info *info, int rotate) { return 0; }
#endif /* CONFIG_LOGO */

static void *fb_seq_start(struct seq_file *m, loff_t *pos)
{
	return (*pos < FB_MAX) ? pos : NULL;
}

static void *fb_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return (*pos < FB_MAX) ? pos : NULL;
}

static void fb_seq_stop(struct seq_file *m, void *v)
{
}

static int fb_seq_show(struct seq_file *m, void *v)
{
	int i = *(loff_t *)v;
	struct fb_info *fi = registered_fb[i];

	if (fi)
		seq_printf(m, "%d %s\n", fi->node, fi->fix.id);
	return 0;
}

static const struct seq_operations proc_fb_seq_ops = {
	.start	= fb_seq_start,
	.next	= fb_seq_next,
	.stop	= fb_seq_stop,
	.show	= fb_seq_show,
};

static int proc_fb_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proc_fb_seq_ops);
}

static const struct file_operations fb_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_fb_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static ssize_t
fb_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_path.dentry->d_inode;
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];
	u32 *buffer, *dst;
	u32 __iomem *src;
	int c, i, cnt = 0, err = 0;
	unsigned long total_size;

	if (!info || ! info->screen_base)
		return -ENODEV;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	if (info->fbops->fb_read)
		return info->fbops->fb_read(info, buf, count, ppos);
	
	total_size = info->screen_size;

	if (total_size == 0)
		total_size = info->fix.smem_len;

	if (p >= total_size)
		return 0;

	if (count >= total_size)
		count = total_size;

	if (count + p > total_size)
		count = total_size - p;

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
			 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	src = (u32 __iomem *) (info->screen_base + p);

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	while (count) {
		c  = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		dst = buffer;
		for (i = c >> 2; i--; )
			*dst++ = fb_readl(src++);
		if (c & 3) {
			u8 *dst8 = (u8 *) dst;
			u8 __iomem *src8 = (u8 __iomem *) src;

			for (i = c & 3; i--;)
				*dst8++ = fb_readb(src8++);

			src = (u32 __iomem *) src8;
		}

		if (copy_to_user(buf, buffer, c)) {
			err = -EFAULT;
			break;
		}
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (err) ? err : cnt;
}

static ssize_t
fb_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_path.dentry->d_inode;
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];
	u32 *buffer, *src;
	u32 __iomem *dst;
	int c, i, cnt = 0, err = 0;
	unsigned long total_size;

	if (!info || !info->screen_base)
		return -ENODEV;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	if (info->fbops->fb_write)
		return info->fbops->fb_write(info, buf, count, ppos);
	
	total_size = info->screen_size;

	if (total_size == 0)
		total_size = info->fix.smem_len;

	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
			 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	dst = (u32 __iomem *) (info->screen_base + p);

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	while (count) {
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		src = buffer;

		if (copy_from_user(src, buf, c)) {
			err = -EFAULT;
			break;
		}

		for (i = c >> 2; i--; )
			fb_writel(*src++, dst++);

		if (c & 3) {
			u8 *src8 = (u8 *) src;
			u8 __iomem *dst8 = (u8 __iomem *) dst;

			for (i = c & 3; i--; )
				fb_writeb(*src8++, dst8++);

			dst = (u32 __iomem *) dst8;
		}

		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (cnt) ? cnt : err;
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
	    var->yoffset + yres > info->var.yres_virtual ||
	    var->xoffset + info->var.xres > info->var.xres_virtual)
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

static int fb_check_caps(struct fb_info *info, struct fb_var_screeninfo *var,
			 u32 activate)
{
	struct fb_event event;
	struct fb_blit_caps caps, fbcaps;
	int err = 0;

	memset(&caps, 0, sizeof(caps));
	memset(&fbcaps, 0, sizeof(fbcaps));
	caps.flags = (activate & FB_ACTIVATE_ALL) ? 1 : 0;
	event.info = info;
	event.data = &caps;
	fb_notifier_call_chain(FB_EVENT_GET_REQ, &event);
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
	int flags = info->flags;
	int ret = 0;

	if (var->activate & FB_ACTIVATE_INV_MODE) {
		struct fb_videomode mode1, mode2;

		fb_var_to_videomode(&mode1, var);
		fb_var_to_videomode(&mode2, &info->var);
		/* make sure we don't delete the videomode of current var */
		ret = fb_mode_is_equal(&mode1, &mode2);

		if (!ret) {
		    struct fb_event event;

		    event.info = info;
		    event.data = &mode1;
		    ret = fb_notifier_call_chain(FB_EVENT_MODE_DELETE, &event);
		}

		if (!ret)
		    fb_delete_videomode(&mode1, &info->modelist);


		ret = (ret) ? -EINVAL : 0;
		goto done;
	}

	if ((var->activate & FB_ACTIVATE_FORCE) ||
	    memcmp(&info->var, var, sizeof(struct fb_var_screeninfo))) {
		u32 activate = var->activate;

		if (!info->fbops->fb_check_var) {
			*var = info->var;
			goto done;
		}

		ret = info->fbops->fb_check_var(var, info);

		if (ret)
			goto done;

		if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
			struct fb_videomode mode;

			if (info->fbops->fb_get_caps) {
				ret = fb_check_caps(info, var, activate);

				if (ret)
					goto done;
			}

			info->var = *var;

			if (info->fbops->fb_set_par)
				info->fbops->fb_set_par(info);

			fb_pan_display(info, &info->var);
			fb_set_cmap(&info->cmap, info);
			fb_var_to_videomode(&mode, &info->var);

			if (info->modelist.prev && info->modelist.next &&
			    !list_empty(&info->modelist))
				ret = fb_add_videomode(&mode, &info->modelist);

			if (!ret && (flags & FBINFO_MISC_USEREVENT)) {
				struct fb_event event;
				int evnt = (activate & FB_ACTIVATE_ALL) ?
					FB_EVENT_MODE_CHANGE_ALL :
					FB_EVENT_MODE_CHANGE;

				info->flags &= ~FBINFO_MISC_USEREVENT;
				event.info = info;
				event.data = &mode;
				fb_notifier_call_chain(evnt, &event);
			}
		}
	}

 done:
	return ret;
}

int
fb_blank(struct fb_info *info, int blank)
{	
 	int ret = -EINVAL;

 	if (blank > FB_BLANK_POWERDOWN)
 		blank = FB_BLANK_POWERDOWN;

	if (info->fbops->fb_blank)
 		ret = info->fbops->fb_blank(blank, info);

 	if (!ret) {
		struct fb_event event;

		event.info = info;
		event.data = &blank;
		fb_notifier_call_chain(FB_EVENT_BLANK, &event);
	}

 	return ret;
}

static long do_fb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	struct fb_ops *fb;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct fb_con2fbmap con2fb;
	struct fb_cmap cmap_from;
	struct fb_cmap_user cmap;
	struct fb_event event;
	void __user *argp = (void __user *)arg;
	long ret = 0;

	switch (cmd) {
	case FBIOGET_VSCREENINFO:
		if (!lock_fb_info(info))
			return -ENODEV;
		var = info->var;
		unlock_fb_info(info);

		ret = copy_to_user(argp, &var, sizeof(var)) ? -EFAULT : 0;
		break;
	case FBIOPUT_VSCREENINFO:
		if (copy_from_user(&var, argp, sizeof(var)))
			return -EFAULT;
		if (!lock_fb_info(info))
			return -ENODEV;
		acquire_console_sem();
		info->flags |= FBINFO_MISC_USEREVENT;
		ret = fb_set_var(info, &var);
		info->flags &= ~FBINFO_MISC_USEREVENT;
		release_console_sem();
		unlock_fb_info(info);
		if (!ret && copy_to_user(argp, &var, sizeof(var)))
			ret = -EFAULT;
		break;
	case FBIOGET_FSCREENINFO:
		if (!lock_fb_info(info))
			return -ENODEV;
		fix = info->fix;
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
		if (!lock_fb_info(info))
			return -ENODEV;
		cmap_from = info->cmap;
		unlock_fb_info(info);
		ret = fb_cmap_to_user(&cmap_from, &cmap);
		break;
	case FBIOPAN_DISPLAY:
		if (copy_from_user(&var, argp, sizeof(var)))
			return -EFAULT;
		if (!lock_fb_info(info))
			return -ENODEV;
		acquire_console_sem();
		ret = fb_pan_display(info, &var);
		release_console_sem();
		unlock_fb_info(info);
		if (ret == 0 && copy_to_user(argp, &var, sizeof(var)))
			return -EFAULT;
		break;
	case FBIO_CURSOR:
		ret = -EINVAL;
		break;
	case FBIOGET_CON2FBMAP:
		if (copy_from_user(&con2fb, argp, sizeof(con2fb)))
			return -EFAULT;
		if (con2fb.console < 1 || con2fb.console > MAX_NR_CONSOLES)
			return -EINVAL;
		con2fb.framebuffer = -1;
		event.data = &con2fb;
		event.info = info;
		fb_notifier_call_chain(FB_EVENT_GET_CONSOLE_MAP, &event);
		ret = copy_to_user(argp, &con2fb, sizeof(con2fb)) ? -EFAULT : 0;
		break;
	case FBIOPUT_CON2FBMAP:
		if (copy_from_user(&con2fb, argp, sizeof(con2fb)))
			return -EFAULT;
		if (con2fb.console < 1 || con2fb.console > MAX_NR_CONSOLES)
			return -EINVAL;
		if (con2fb.framebuffer < 0 || con2fb.framebuffer >= FB_MAX)
			return -EINVAL;
		if (!registered_fb[con2fb.framebuffer])
			request_module("fb%d", con2fb.framebuffer);
		if (!registered_fb[con2fb.framebuffer]) {
			ret = -EINVAL;
			break;
		}
		event.data = &con2fb;
		event.info = info;
		ret = fb_notifier_call_chain(FB_EVENT_SET_CONSOLE_MAP, &event);
		break;
	case FBIOBLANK:
		if (!lock_fb_info(info))
			return -ENODEV;
		acquire_console_sem();
		info->flags |= FBINFO_MISC_USEREVENT;
		ret = fb_blank(info, arg);
		info->flags &= ~FBINFO_MISC_USEREVENT;
		release_console_sem();
		unlock_fb_info(info);
		break;
	default:
		if (!lock_fb_info(info))
			return -ENODEV;
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
	struct inode *inode = file->f_path.dentry->d_inode;
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];

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
	struct fb_cmap_user __user *cmap;
	struct fb_cmap32 __user *cmap32;
	__u32 data;
	int err;

	cmap = compat_alloc_user_space(sizeof(*cmap));
	cmap32 = compat_ptr(arg);

	if (copy_in_user(&cmap->start, &cmap32->start, 2 * sizeof(__u32)))
		return -EFAULT;

	if (get_user(data, &cmap32->red) ||
	    put_user(compat_ptr(data), &cmap->red) ||
	    get_user(data, &cmap32->green) ||
	    put_user(compat_ptr(data), &cmap->green) ||
	    get_user(data, &cmap32->blue) ||
	    put_user(compat_ptr(data), &cmap->blue) ||
	    get_user(data, &cmap32->transp) ||
	    put_user(compat_ptr(data), &cmap->transp))
		return -EFAULT;

	err = do_fb_ioctl(info, cmd, (unsigned long) cmap);

	if (!err) {
		if (copy_in_user(&cmap32->start,
				 &cmap->start,
				 2 * sizeof(__u32)))
			err = -EFAULT;
	}
	return err;
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

	return err;
}

static int fb_get_fscreeninfo(struct fb_info *info, unsigned int cmd,
			      unsigned long arg)
{
	mm_segment_t old_fs;
	struct fb_fix_screeninfo fix;
	struct fb_fix_screeninfo32 __user *fix32;
	int err;

	fix32 = compat_ptr(arg);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = do_fb_ioctl(info, cmd, (unsigned long) &fix);
	set_fs(old_fs);

	if (!err)
		err = do_fscreeninfo_to_user(&fix, fix32);

	return err;
}

static long fb_compat_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	long ret = -ENOIOCTLCMD;

	switch(cmd) {
	case FBIOGET_VSCREENINFO:
	case FBIOPUT_VSCREENINFO:
	case FBIOPAN_DISPLAY:
	case FBIOGET_CON2FBMAP:
	case FBIOPUT_CON2FBMAP:
		arg = (unsigned long) compat_ptr(arg);
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
__acquires(&info->lock)
__releases(&info->lock)
{
	int fbidx = iminor(file->f_path.dentry->d_inode);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	unsigned long off;
	unsigned long start;
	u32 len;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	off = vma->vm_pgoff << PAGE_SHIFT;
	if (!fb)
		return -ENODEV;
	if (fb->fb_mmap) {
		int res;
		mutex_lock(&info->lock);
		res = fb->fb_mmap(info, vma);
		mutex_unlock(&info->lock);
		return res;
	}

	mutex_lock(&info->lock);

	/* frame buffer memory */
	start = info->fix.smem_start;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);
	if (off >= len) {
		/* memory mapped io */
		off -= len;
		if (info->var.accel_flags) {
			mutex_unlock(&info->lock);
			return -EINVAL;
		}
		start = info->fix.mmio_start;
		len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.mmio_len);
	}
	mutex_unlock(&info->lock);
	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;
	/* This is an IO map - tell maydump to skip this VMA */
	vma->vm_flags |= VM_IO | VM_RESERVED;
	fb_pgprotect(file, vma, off);
	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static int
fb_open(struct inode *inode, struct file *file)
__acquires(&info->lock)
__releases(&info->lock)
{
	int fbidx = iminor(inode);
	struct fb_info *info;
	int res = 0;

	if (fbidx >= FB_MAX)
		return -ENODEV;
	info = registered_fb[fbidx];
	if (!info)
		request_module("fb%d", fbidx);
	info = registered_fb[fbidx];
	if (!info)
		return -ENODEV;
	mutex_lock(&info->lock);
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
	mutex_unlock(&info->lock);
	return res;
}

static int 
fb_release(struct inode *inode, struct file *file)
__acquires(&info->lock)
__releases(&info->lock)
{
	struct fb_info * const info = file->private_data;

	mutex_lock(&info->lock);
	if (info->fbops->fb_release)
		info->fbops->fb_release(info,1);
	module_put(info->fbops->owner);
	mutex_unlock(&info->lock);
	return 0;
}

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
#ifdef HAVE_ARCH_FB_UNMAPPED_AREA
	.get_unmapped_area = get_fb_unmapped_area,
#endif
#ifdef CONFIG_FB_DEFERRED_IO
	.fsync =	fb_deferred_io_fsync,
#endif
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
	int i;
	struct fb_event event;
	struct fb_videomode mode;

	if (num_registered_fb == FB_MAX)
		return -ENXIO;

	if (fb_check_foreignness(fb_info))
		return -ENOSYS;

	num_registered_fb++;
	for (i = 0 ; i < FB_MAX; i++)
		if (!registered_fb[i])
			break;
	fb_info->node = i;
	mutex_init(&fb_info->lock);

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

	fb_var_to_videomode(&mode, &fb_info->var);
	fb_add_videomode(&mode, &fb_info->modelist);
	registered_fb[i] = fb_info;

	event.info = fb_info;
	fb_notifier_call_chain(FB_EVENT_FB_REGISTERED, &event);
	return 0;
}


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

int
unregister_framebuffer(struct fb_info *fb_info)
{
	struct fb_event event;
	int i, ret = 0;

	i = fb_info->node;
	if (!registered_fb[i]) {
		ret = -EINVAL;
		goto done;
	}

	event.info = fb_info;
	ret = fb_notifier_call_chain(FB_EVENT_FB_UNBIND, &event);

	if (ret) {
		ret = -EINVAL;
		goto done;
	}

	if (fb_info->pixmap.addr &&
	    (fb_info->pixmap.flags & FB_PIXMAP_DEFAULT))
		kfree(fb_info->pixmap.addr);
	fb_destroy_modelist(&fb_info->modelist);
	registered_fb[i]=NULL;
	num_registered_fb--;
	fb_cleanup_device(fb_info);
	device_destroy(fb_class, MKDEV(FB_MAJOR, i));
	event.info = fb_info;
	fb_notifier_call_chain(FB_EVENT_FB_UNREGISTERED, &event);
done:
	return ret;
}

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
	struct fb_event event;

	event.info = info;
	if (state) {
		fb_notifier_call_chain(FB_EVENT_SUSPEND, &event);
		info->state = FBINFO_STATE_SUSPENDED;
	} else {
		info->state = FBINFO_STATE_RUNNING;
		fb_notifier_call_chain(FB_EVENT_RESUME, &event);
	}
}

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
	proc_create("fb", 0, NULL, &fb_proc_fops);

	if (register_chrdev(FB_MAJOR,"fb",&fb_fops))
		printk("unable to get major %d for fb devs\n", FB_MAJOR);

	fb_class = class_create(THIS_MODULE, "graphics");
	if (IS_ERR(fb_class)) {
		printk(KERN_WARNING "Unable to create fb class; errno = %ld\n", PTR_ERR(fb_class));
		fb_class = NULL;
	}
	return 0;
}

#ifdef MODULE
module_init(fbmem_init);
static void __exit
fbmem_exit(void)
{
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
	struct fb_event event;
	struct fb_var_screeninfo var = info->var;
	struct list_head *pos, *n;
	struct fb_modelist *modelist;
	struct fb_videomode *m, mode;
	int err = 1;

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

	err = 1;

	if (!list_empty(&info->modelist)) {
		event.info = info;
		err = fb_notifier_call_chain(FB_EVENT_NEW_MODELIST, &event);
	}

	return err;
}

static char *video_options[FB_MAX] __read_mostly;
static int ofonly __read_mostly;

/**
 * fb_get_options - get kernel boot parameters
 * @name:   framebuffer name as it would appear in
 *          the boot parameter line
 *          (video=<name>:<options>)
 * @option: the option will be stored here
 *
 * NOTE: Needed to maintain backwards compatibility
 */
int fb_get_options(char *name, char **option)
{
	char *opt, *options = NULL;
	int opt_len, retval = 0;
	int name_len = strlen(name), i;

	if (name_len && ofonly && strncmp(name, "offb", 4))
		retval = 1;

	if (name_len && !retval) {
		for (i = 0; i < FB_MAX; i++) {
			if (video_options[i] == NULL)
				continue;
			opt_len = strlen(video_options[i]);
			if (!opt_len)
				continue;
			opt = video_options[i];
			if (!strncmp(name, opt, name_len) &&
			    opt[name_len] == ':')
				options = opt + name_len + 1;
		}
	}
	if (options && !strncmp(options, "off", 3))
		retval = 1;

	if (option)
		*option = options;

	return retval;
}

#ifndef MODULE
/**
 *	video_setup - process command line options
 *	@options: string of options
 *
 *	Process command line options for frame buffer subsystem.
 *
 *	NOTE: This function is a __setup and __init function.
 *            It only stores the options.  Drivers have to call
 *            fb_get_options() as necessary.
 *
 *	Returns zero.
 *
 */
static int __init video_setup(char *options)
{
	int i, global = 0;

	if (!options || !*options)
 		global = 1;

 	if (!global && !strncmp(options, "ofonly", 6)) {
 		ofonly = 1;
 		global = 1;
 	}

 	if (!global && !strstr(options, "fb:")) {
 		fb_mode_option = options;
 		global = 1;
 	}

 	if (!global) {
 		for (i = 0; i < FB_MAX; i++) {
 			if (video_options[i] == NULL) {
 				video_options[i] = options;
 				break;
 			}

		}
	}

	return 1;
}
__setup("video=", video_setup);
#endif

    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(register_framebuffer);
EXPORT_SYMBOL(unregister_framebuffer);
EXPORT_SYMBOL(num_registered_fb);
EXPORT_SYMBOL(registered_fb);
EXPORT_SYMBOL(fb_show_logo);
EXPORT_SYMBOL(fb_set_var);
EXPORT_SYMBOL(fb_blank);
EXPORT_SYMBOL(fb_pan_display);
EXPORT_SYMBOL(fb_get_buffer_offset);
EXPORT_SYMBOL(fb_set_suspend);
EXPORT_SYMBOL(fb_get_options);

MODULE_LICENSE("GPL");
