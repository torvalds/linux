/*
 *  linux/drivers/video/fbcmap.c -- Colormap handling for frame buffer devices
 *
 *	Created 15 Jun 1997 by Geert Uytterhoeven
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/string.h>
#include <linux/module.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "console/fbcondecor.h"

static u16 red2[] __read_mostly = {
    0x0000, 0xaaaa
};
static u16 green2[] __read_mostly = {
    0x0000, 0xaaaa
};
static u16 blue2[] __read_mostly = {
    0x0000, 0xaaaa
};

static u16 red4[] __read_mostly = {
    0x0000, 0xaaaa, 0x5555, 0xffff
};
static u16 green4[] __read_mostly = {
    0x0000, 0xaaaa, 0x5555, 0xffff
};
static u16 blue4[] __read_mostly = {
    0x0000, 0xaaaa, 0x5555, 0xffff
};

static u16 red8[] __read_mostly = {
    0x0000, 0x0000, 0x0000, 0x0000, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa
};
static u16 green8[] __read_mostly = {
    0x0000, 0x0000, 0xaaaa, 0xaaaa, 0x0000, 0x0000, 0x5555, 0xaaaa
};
static u16 blue8[] __read_mostly = {
    0x0000, 0xaaaa, 0x0000, 0xaaaa, 0x0000, 0xaaaa, 0x0000, 0xaaaa
};

static u16 red16[] __read_mostly = {
    0x0000, 0x0000, 0x0000, 0x0000, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa,
    0x5555, 0x5555, 0x5555, 0x5555, 0xffff, 0xffff, 0xffff, 0xffff
};
static u16 green16[] __read_mostly = {
    0x0000, 0x0000, 0xaaaa, 0xaaaa, 0x0000, 0x0000, 0x5555, 0xaaaa,
    0x5555, 0x5555, 0xffff, 0xffff, 0x5555, 0x5555, 0xffff, 0xffff
};
static u16 blue16[] __read_mostly = {
    0x0000, 0xaaaa, 0x0000, 0xaaaa, 0x0000, 0xaaaa, 0x0000, 0xaaaa,
    0x5555, 0xffff, 0x5555, 0xffff, 0x5555, 0xffff, 0x5555, 0xffff
};

static const struct fb_cmap default_2_colors = {
    .len=2, .red=red2, .green=green2, .blue=blue2
};
static const struct fb_cmap default_8_colors = {
    .len=8, .red=red8, .green=green8, .blue=blue8
};
static const struct fb_cmap default_4_colors = {
    .len=4, .red=red4, .green=green4, .blue=blue4
};
static const struct fb_cmap default_16_colors = {
    .len=16, .red=red16, .green=green16, .blue=blue16
};



/**
 *	fb_alloc_cmap - allocate a colormap
 *	@cmap: frame buffer colormap structure
 *	@len: length of @cmap
 *	@transp: boolean, 1 if there is transparency, 0 otherwise
 *	@flags: flags for kmalloc memory allocation
 *
 *	Allocates memory for a colormap @cmap.  @len is the
 *	number of entries in the palette.
 *
 *	Returns negative errno on error, or zero on success.
 *
 */

int fb_alloc_cmap_gfp(struct fb_cmap *cmap, int len, int transp, gfp_t flags)
{
	int size = len * sizeof(u16);
	int ret = -ENOMEM;

	if (cmap->len != len) {
		fb_dealloc_cmap(cmap);
		if (!len)
			return 0;

		cmap->red = kmalloc(size, flags);
		if (!cmap->red)
			goto fail;
		cmap->green = kmalloc(size, flags);
		if (!cmap->green)
			goto fail;
		cmap->blue = kmalloc(size, flags);
		if (!cmap->blue)
			goto fail;
		if (transp) {
			cmap->transp = kmalloc(size, flags);
			if (!cmap->transp)
				goto fail;
		} else {
			cmap->transp = NULL;
		}
	}
	cmap->start = 0;
	cmap->len = len;
	ret = fb_copy_cmap(fb_default_cmap(len), cmap);
	if (ret)
		goto fail;
	return 0;

fail:
	fb_dealloc_cmap(cmap);
	return ret;
}

int fb_alloc_cmap(struct fb_cmap *cmap, int len, int transp)
{
	return fb_alloc_cmap_gfp(cmap, len, transp, GFP_ATOMIC);
}

/**
 *      fb_dealloc_cmap - deallocate a colormap
 *      @cmap: frame buffer colormap structure
 *
 *      Deallocates a colormap that was previously allocated with
 *      fb_alloc_cmap().
 *
 */

void fb_dealloc_cmap(struct fb_cmap *cmap)
{
	kfree(cmap->red);
	kfree(cmap->green);
	kfree(cmap->blue);
	kfree(cmap->transp);

	cmap->red = cmap->green = cmap->blue = cmap->transp = NULL;
	cmap->len = 0;
}

/**
 *	fb_copy_cmap - copy a colormap
 *	@from: frame buffer colormap structure
 *	@to: frame buffer colormap structure
 *
 *	Copy contents of colormap from @from to @to.
 */

int fb_copy_cmap(const struct fb_cmap *from, struct fb_cmap *to)
{
	int tooff = 0, fromoff = 0;
	int size;

	if (to->start > from->start)
		fromoff = to->start - from->start;
	else
		tooff = from->start - to->start;
	size = to->len - tooff;
	if (size > (int) (from->len - fromoff))
		size = from->len - fromoff;
	if (size <= 0)
		return -EINVAL;
	size *= sizeof(u16);

	memcpy(to->red+tooff, from->red+fromoff, size);
	memcpy(to->green+tooff, from->green+fromoff, size);
	memcpy(to->blue+tooff, from->blue+fromoff, size);
	if (from->transp && to->transp)
		memcpy(to->transp+tooff, from->transp+fromoff, size);
	return 0;
}

int fb_cmap_to_user(const struct fb_cmap *from, struct fb_cmap_user *to)
{
	int tooff = 0, fromoff = 0;
	int size;

	if (to->start > from->start)
		fromoff = to->start - from->start;
	else
		tooff = from->start - to->start;
	size = to->len - tooff;
	if (size > (int) (from->len - fromoff))
		size = from->len - fromoff;
	if (size <= 0)
		return -EINVAL;
	size *= sizeof(u16);

	if (copy_to_user(to->red+tooff, from->red+fromoff, size))
		return -EFAULT;
	if (copy_to_user(to->green+tooff, from->green+fromoff, size))
		return -EFAULT;
	if (copy_to_user(to->blue+tooff, from->blue+fromoff, size))
		return -EFAULT;
	if (from->transp && to->transp)
		if (copy_to_user(to->transp+tooff, from->transp+fromoff, size))
			return -EFAULT;
	return 0;
}

/**
 *	fb_set_cmap - set the colormap
 *	@cmap: frame buffer colormap structure
 *	@info: frame buffer info structure
 *
 *	Sets the colormap @cmap for a screen of device @info.
 *
 *	Returns negative errno on error, or zero on success.
 *
 */

int fb_set_cmap(struct fb_cmap *cmap, struct fb_info *info)
{
	int i, start, rc = 0;
	u16 *red, *green, *blue, *transp;
	u_int hred, hgreen, hblue, htransp = 0xffff;

	red = cmap->red;
	green = cmap->green;
	blue = cmap->blue;
	transp = cmap->transp;
	start = cmap->start;

	if (start < 0 || (!info->fbops->fb_setcolreg &&
			  !info->fbops->fb_setcmap))
		return -EINVAL;
	if (info->fbops->fb_setcmap) {
		rc = info->fbops->fb_setcmap(cmap, info);
	} else {
		for (i = 0; i < cmap->len; i++) {
			hred = *red++;
			hgreen = *green++;
			hblue = *blue++;
			if (transp)
				htransp = *transp++;
			if (info->fbops->fb_setcolreg(start++,
						      hred, hgreen, hblue, 
						      htransp, info))
				break;
		}
	}
	if (rc == 0) {
		fb_copy_cmap(cmap, &info->cmap);
		if (fbcon_decor_active(info, vc_cons[fg_console].d) &&
		    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
			fbcon_decor_fix_pseudo_pal(info, vc_cons[fg_console].d);
	}
	return rc;
}

int fb_set_user_cmap(struct fb_cmap_user *cmap, struct fb_info *info)
{
	int rc, size = cmap->len * sizeof(u16);
	struct fb_cmap umap;

	if (size < 0 || size < cmap->len)
		return -E2BIG;

	memset(&umap, 0, sizeof(struct fb_cmap));
	rc = fb_alloc_cmap_gfp(&umap, cmap->len, cmap->transp != NULL,
				GFP_KERNEL);
	if (rc)
		return rc;
	if (copy_from_user(umap.red, cmap->red, size) ||
	    copy_from_user(umap.green, cmap->green, size) ||
	    copy_from_user(umap.blue, cmap->blue, size) ||
	    (cmap->transp && copy_from_user(umap.transp, cmap->transp, size))) {
		rc = -EFAULT;
		goto out;
	}
	umap.start = cmap->start;
	if (!lock_fb_info(info)) {
		rc = -ENODEV;
		goto out;
	}
	if (cmap->start < 0 || (!info->fbops->fb_setcolreg &&
				!info->fbops->fb_setcmap)) {
		rc = -EINVAL;
		goto out1;
	}
	rc = fb_set_cmap(&umap, info);
out1:
	unlock_fb_info(info);
out:
	fb_dealloc_cmap(&umap);
	return rc;
}

/**
 *	fb_default_cmap - get default colormap
 *	@len: size of palette for a depth
 *
 *	Gets the default colormap for a specific screen depth.  @len
 *	is the size of the palette for a particular screen depth.
 *
 *	Returns pointer to a frame buffer colormap structure.
 *
 */

const struct fb_cmap *fb_default_cmap(int len)
{
    if (len <= 2)
	return &default_2_colors;
    if (len <= 4)
	return &default_4_colors;
    if (len <= 8)
	return &default_8_colors;
    return &default_16_colors;
}


/**
 *	fb_invert_cmaps - invert all defaults colormaps
 *
 *	Invert all default colormaps.
 *
 */

void fb_invert_cmaps(void)
{
    u_int i;

    for (i = 0; i < ARRAY_SIZE(red2); i++) {
	red2[i] = ~red2[i];
	green2[i] = ~green2[i];
	blue2[i] = ~blue2[i];
    }
    for (i = 0; i < ARRAY_SIZE(red4); i++) {
	red4[i] = ~red4[i];
	green4[i] = ~green4[i];
	blue4[i] = ~blue4[i];
    }
    for (i = 0; i < ARRAY_SIZE(red8); i++) {
	red8[i] = ~red8[i];
	green8[i] = ~green8[i];
	blue8[i] = ~blue8[i];
    }
    for (i = 0; i < ARRAY_SIZE(red16); i++) {
	red16[i] = ~red16[i];
	green16[i] = ~green16[i];
	blue16[i] = ~blue16[i];
    }
}


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(fb_alloc_cmap);
EXPORT_SYMBOL(fb_dealloc_cmap);
EXPORT_SYMBOL(fb_copy_cmap);
EXPORT_SYMBOL(fb_set_cmap);
EXPORT_SYMBOL(fb_default_cmap);
EXPORT_SYMBOL(fb_invert_cmaps);
