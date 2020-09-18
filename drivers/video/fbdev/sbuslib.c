// SPDX-License-Identifier: GPL-2.0
/* sbuslib.c: Helper library for SBUS framebuffer drivers.
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/of_device.h>

#include <asm/fbio.h>

#include "sbuslib.h"

void sbusfb_fill_var(struct fb_var_screeninfo *var, struct device_node *dp,
		     int bpp)
{
	memset(var, 0, sizeof(*var));

	var->xres = of_getintprop_default(dp, "width", 1152);
	var->yres = of_getintprop_default(dp, "height", 900);
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->bits_per_pixel = bpp;
}

EXPORT_SYMBOL(sbusfb_fill_var);

static unsigned long sbusfb_mmapsize(long size, unsigned long fbsize)
{
	if (size == SBUS_MMAP_EMPTY) return 0;
	if (size >= 0) return size;
	return fbsize * (-size);
}

int sbusfb_mmap_helper(struct sbus_mmap_map *map,
		       unsigned long physbase,
		       unsigned long fbsize,
		       unsigned long iospace,
		       struct vm_area_struct *vma)
{
	unsigned int size, page, r, map_size;
	unsigned long map_offset = 0;
	unsigned long off;
	int i;
                                        
	if (!(vma->vm_flags & (VM_SHARED | VM_MAYSHARE)))
		return -EINVAL;

	size = vma->vm_end - vma->vm_start;
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	off = vma->vm_pgoff << PAGE_SHIFT;

	/* VM_IO | VM_DONTEXPAND | VM_DONTDUMP are set by remap_pfn_range() */

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		map_size = 0;
		for (i = 0; map[i].size; i++)
			if (map[i].voff == off+page) {
				map_size = sbusfb_mmapsize(map[i].size, fbsize);
#ifdef __sparc_v9__
#define POFF_MASK	(PAGE_MASK|0x1UL)
#else
#define POFF_MASK	(PAGE_MASK)
#endif				
				map_offset = (physbase + map[i].poff) & POFF_MASK;
				break;
			}
		if (!map_size) {
			page += PAGE_SIZE;
			continue;
		}
		if (page + map_size > size)
			map_size = size - page;
		r = io_remap_pfn_range(vma,
					vma->vm_start + page,
					MK_IOSPACE_PFN(iospace,
						map_offset >> PAGE_SHIFT),
					map_size,
					vma->vm_page_prot);
		if (r)
			return -EAGAIN;
		page += map_size;
	}

	return 0;
}
EXPORT_SYMBOL(sbusfb_mmap_helper);

int sbusfb_ioctl_helper(unsigned long cmd, unsigned long arg,
			struct fb_info *info,
			int type, int fb_depth, unsigned long fb_size)
{
	switch(cmd) {
	case FBIOGTYPE: {
		struct fbtype __user *f = (struct fbtype __user *) arg;

		if (put_user(type, &f->fb_type) ||
		    put_user(info->var.yres, &f->fb_height) ||
		    put_user(info->var.xres, &f->fb_width) ||
		    put_user(fb_depth, &f->fb_depth) ||
		    put_user(0, &f->fb_cmsize) ||
		    put_user(fb_size, &f->fb_cmsize))
			return -EFAULT;
		return 0;
	}
	case FBIOPUTCMAP_SPARC: {
		struct fbcmap __user *c = (struct fbcmap __user *) arg;
		struct fb_cmap cmap;
		u16 red, green, blue;
		u8 red8, green8, blue8;
		unsigned char __user *ured;
		unsigned char __user *ugreen;
		unsigned char __user *ublue;
		unsigned int index, count, i;

		if (get_user(index, &c->index) ||
		    get_user(count, &c->count) ||
		    get_user(ured, &c->red) ||
		    get_user(ugreen, &c->green) ||
		    get_user(ublue, &c->blue))
			return -EFAULT;

		cmap.len = 1;
		cmap.red = &red;
		cmap.green = &green;
		cmap.blue = &blue;
		cmap.transp = NULL;
		for (i = 0; i < count; i++) {
			int err;

			if (get_user(red8, &ured[i]) ||
			    get_user(green8, &ugreen[i]) ||
			    get_user(blue8, &ublue[i]))
				return -EFAULT;

			red = red8 << 8;
			green = green8 << 8;
			blue = blue8 << 8;

			cmap.start = index + i;
			err = fb_set_cmap(&cmap, info);
			if (err)
				return err;
		}
		return 0;
	}
	case FBIOGETCMAP_SPARC: {
		struct fbcmap __user *c = (struct fbcmap __user *) arg;
		unsigned char __user *ured;
		unsigned char __user *ugreen;
		unsigned char __user *ublue;
		struct fb_cmap *cmap = &info->cmap;
		unsigned int index, count, i;
		u8 red, green, blue;

		if (get_user(index, &c->index) ||
		    get_user(count, &c->count) ||
		    get_user(ured, &c->red) ||
		    get_user(ugreen, &c->green) ||
		    get_user(ublue, &c->blue))
			return -EFAULT;

		if (index > cmap->len || count > cmap->len - index)
			return -EINVAL;

		for (i = 0; i < count; i++) {
			red = cmap->red[index + i] >> 8;
			green = cmap->green[index + i] >> 8;
			blue = cmap->blue[index + i] >> 8;
			if (put_user(red, &ured[i]) ||
			    put_user(green, &ugreen[i]) ||
			    put_user(blue, &ublue[i]))
				return -EFAULT;
		}
		return 0;
	}
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(sbusfb_ioctl_helper);

#ifdef CONFIG_COMPAT
static int fbiogetputcmap(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct fbcmap32 __user *argp = (void __user *)arg;
	struct fbcmap __user *p = compat_alloc_user_space(sizeof(*p));
	u32 addr;
	int ret;

	ret = copy_in_user(p, argp, 2 * sizeof(int));
	ret |= get_user(addr, &argp->red);
	ret |= put_user(compat_ptr(addr), &p->red);
	ret |= get_user(addr, &argp->green);
	ret |= put_user(compat_ptr(addr), &p->green);
	ret |= get_user(addr, &argp->blue);
	ret |= put_user(compat_ptr(addr), &p->blue);
	if (ret)
		return -EFAULT;
	return info->fbops->fb_ioctl(info,
			(cmd == FBIOPUTCMAP32) ?
			FBIOPUTCMAP_SPARC : FBIOGETCMAP_SPARC,
			(unsigned long)p);
}

int sbusfb_compat_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FBIOGTYPE:
	case FBIOSATTR:
	case FBIOGATTR:
	case FBIOSVIDEO:
	case FBIOGVIDEO:
	case FBIOSCURSOR32:
	case FBIOGCURSOR32:	/* This is not implemented yet.
				   Later it should be converted... */
	case FBIOSCURPOS:
	case FBIOGCURPOS:
	case FBIOGCURMAX:
		return info->fbops->fb_ioctl(info, cmd, arg);
	case FBIOPUTCMAP32:
		return fbiogetputcmap(info, cmd, arg);
	case FBIOGETCMAP32:
		return fbiogetputcmap(info, cmd, arg);
	default:
		return -ENOIOCTLCMD;
	}
}
EXPORT_SYMBOL(sbusfb_compat_ioctl);
#endif
