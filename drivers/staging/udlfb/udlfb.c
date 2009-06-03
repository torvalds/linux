/*****************************************************************************
 *                          DLFB Kernel Driver                               *
 *                            Version 0.2 (udlfb)                            *
 *             (C) 2009 Roberto De Ioris <roberto@unbit.it>                  *
 *                                                                           *
 *     This file is licensed under the GPLv2. See COPYING in the package.    *
 * Based on the amazing work of Florian Echtler and libdlo 0.1               *
 *                                                                           *
 *                                                                           *
 * 31.05.09 release 0.2                                                      *
 * 22.05.09 First public (ugly) release                                      *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/mutex.h>

#include "udlfb.h"

#define DRIVER_VERSION "DLFB 0.2"

/* memory functions taken from vfb */

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size);	/* Clear the ram out, no junk to the user */
	adr = (unsigned long)mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long)mem;
	while ((long)size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

static int dlfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
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
};

/*
static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x17e9, 0x023d) },
	{ }
};
*/

static struct usb_device_id id_table[] = {
	{.idVendor = 0x17e9, .match_flags = USB_DEVICE_ID_MATCH_VENDOR,},
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver dlfb_driver;

static int
image_blit(struct dlfb_data *dev_info, int x, int y, int width, int height,
	   char *data)
{

	int i, j, base;
	int rem = width;
	int ret;

	int diff;

	char *bufptr;

	if (x + width > dev_info->info->var.xres)
		return -EINVAL;

	if (y + height > dev_info->info->var.yres)
		return -EINVAL;

	mutex_lock(&dev_info->bulk_mutex);

	base = dev_info->base16 + (dev_info->info->var.xres * 2 * y) + (x * 2);

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

			if (rem > 255) {

				diff = 0;
				for (j = 0; j < 510; j++) {
					if (dev_info->
					    backing_buffer[base + j] !=
					    data[j]) {
						diff = 1;
						break;
					}
				}

				if (diff == 1) {
					*bufptr++ = 0xAF;
					*bufptr++ = 0x68;

					*bufptr++ = (char)(base >> 16);
					*bufptr++ = (char)(base >> 8);
					*bufptr++ = (char)(base);
					*bufptr++ = 255;
					/* PUT COMPRESSION HERE */
					for (j = 0; j < 510; j += 2) {
						bufptr[j] = data[j + 1];
						bufptr[j + 1] = data[j];
					}
					bufptr += 510;
				}

				rem -= 255;
				base += 510;
				data += 510;
			} else {

				diff = 0;

				for (j = 0; j < rem * 2; j++) {
					if (dev_info->
					    backing_buffer[base + j] !=
					    data[j]) {
						diff = 1;
						break;
					}
				}

				if (diff == 1) {

					*bufptr++ = 0xAF;
					*bufptr++ = 0x68;

					*bufptr++ = (char)(base >> 16);
					*bufptr++ = (char)(base >> 8);
					*bufptr++ = (char)(base);
					*bufptr++ = rem;
					/* PUT COMPRESSION HERE */
					for (j = 0; j < rem * 2; j += 2) {
						bufptr[j] = data[j + 1];
						bufptr[j + 1] = data[j];
					}
					bufptr += rem * 2;

				}

				base += rem * 2;
				data += rem * 2;
				rem = 0;
			}

		}

		memcpy(dev_info->backing_buffer + base - (width * 2),
		       data - (width * 2), width * 2);

		base += (dev_info->info->var.xres * 2) - (width * 2);
		data += (dev_info->info->var.xres * 2) - (width * 2);

	}

	if (bufptr > dev_info->buf)
		ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);

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

		for (j = 0; j < width * 2; j += 2) {
			dev_info->backing_buffer[base + j] = (char)(col >> 8);
			dev_info->backing_buffer[base + j + 1] = (char)(col);
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

		memcpy(dev_info->backing_buffer + base,
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

void dlfb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{

	struct dlfb_data *dev = info->par;

	copyarea(dev, area->dx, area->dy, area->sx, area->sy, area->width,
		 area->height);

	/* printk("COPY AREA %d %d %d %d %d %d !!!\n", area->dx, area->dy, area->sx, area->sy, area->width, area->height); */

}

void dlfb_imageblit(struct fb_info *info, const struct fb_image *image)
{

	int ret;
	struct dlfb_data *dev = info->par;
	/* printk("IMAGE BLIT (1) %d %d %d %d DEPTH %d {%p}!!!\n", image->dx, image->dy, image->width, image->height, image->depth, dev->udev); */
	cfb_imageblit(info, image);
	ret =
	    image_blit(dev, image->dx, image->dy, image->width, image->height,
		       info->screen_base);
	/* printk("IMAGE BLIT (2) %d %d %d %d DEPTH %d {%p} %d!!!\n", image->dx, image->dy, image->width, image->height, image->depth, dev->udev, ret); */
}

void dlfb_fillrect(struct fb_info *info, const struct fb_fillrect *region)
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

static int dlfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{

	struct dlfb_data *dev_info = info->par;
	struct dloarea *area;

	if (cmd == 0xAA) {

		area = (struct dloarea *)arg;

		if (area->x < 0)
			area->x = 0;

		if (area->x > info->var.xres)
			area->x = info->var.xres;

		if (area->y < 0)
			area->y = 0;

		if (area->y > info->var.yres)
			area->y = info->var.yres;

		image_blit(dev_info, area->x, area->y, area->w, area->h,
			   info->screen_base);
	}
	return 0;
}

/* taken from vesafb */

static int
dlfb_setcolreg(unsigned regno, unsigned red, unsigned green,
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

static int dlfb_release(struct fb_info *info, int user)
{
	struct dlfb_data *dev_info = info->par;
	image_blit(dev_info, 0, 0, info->var.xres, info->var.yres,
		   info->screen_base);
	return 0;
}

static int dlfb_blank(int blank_mode, struct fb_info *info)
{
	return 0;
}

static struct fb_ops dlfb_ops = {
	.fb_setcolreg = dlfb_setcolreg,
	.fb_fillrect = dlfb_fillrect,
	.fb_copyarea = dlfb_copyarea,
	.fb_imageblit = dlfb_imageblit,
	.fb_mmap = dlfb_mmap,
	.fb_ioctl = dlfb_ioctl,
	.fb_release = dlfb_release,
	.fb_blank = dlfb_blank,
};

static int
dlfb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct dlfb_data *dev_info;
	struct fb_info *info;
	int i;

	int ret;
	char rbuf[4];

	dev_info = kzalloc(sizeof(*dev_info), GFP_KERNEL);
	if (dev_info == NULL) {
		printk("cannot allocate dev_info structure.\n");
		return -ENOMEM;
	}

	mutex_init(&dev_info->bulk_mutex);

	dev_info->udev = usb_get_dev(interface_to_usbdev(interface));
	dev_info->interface = interface;

	printk("DisplayLink device attached\n");

	/* add framebuffer info to usb interface */
	usb_set_intfdata(interface, dev_info);

	dev_info->buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	/* usb_buffer_alloc(dev_info->udev, BUF_SIZE , GFP_KERNEL, &dev_info->tx_urb->transfer_dma); */

	if (dev_info->buf == NULL) {
		printk("unable to allocate memory for dlfb commands\n");
		goto out;
	}
	dev_info->bufend = dev_info->buf + BUF_SIZE;

	dev_info->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	usb_fill_bulk_urb(dev_info->tx_urb, dev_info->udev,
			  usb_sndbulkpipe(dev_info->udev, 1), dev_info->buf, 0,
			  dlfb_bulk_callback, dev_info);

	ret =
	    usb_control_msg(dev_info->udev, usb_rcvctrlpipe(dev_info->udev, 0),
			    (0x06), (0x80 | (0x02 << 5)), 0, 0, rbuf, 4, 0);
	printk("ret control msg 0: %d %x%x%x%x\n", ret, rbuf[0], rbuf[1],
	       rbuf[2], rbuf[3]);

	for (i = 0; i < 128; i++) {
		ret =
		    usb_control_msg(dev_info->udev,
				    usb_rcvctrlpipe(dev_info->udev, 0), (0x02),
				    (0x80 | (0x02 << 5)), i << 8, 0xA1, rbuf, 2,
				    0);
		/* printk("ret control msg edid %d: %d [%d]\n",i, ret, rbuf[1]); */
		dev_info->edid[i] = rbuf[1];
	}

	info = framebuffer_alloc(sizeof(u32) * 256, &dev_info->udev->dev);

	if (!info) {
		printk("non posso allocare il framebuffer displaylink");
		goto out;
	}

	fb_parse_edid(dev_info->edid, &info->var);

	printk("EDID XRES %d YRES %d\n", info->var.xres, info->var.yres);

	if (dlfb_set_video_mode(dev_info, info->var.xres, info->var.yres) != 0)
		goto out;

	printk("found valid mode...%d\n", info->var.pixclock);

	info->pseudo_palette = info->par;
	info->par = dev_info;

	dev_info->info = info;

	info->flags =
	    FBINFO_DEFAULT | FBINFO_READS_FAST | FBINFO_HWACCEL_IMAGEBLIT |
	    FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT;
	info->fbops = &dlfb_ops;
	info->screen_base = rvmalloc(dev_info->screen_size);

	if (info->screen_base == NULL) {
		printk
		    ("cannot allocate framebuffer virtual memory of %d bytes\n",
		     dev_info->screen_size);
		goto out0;
	}

	printk("screen base allocated !!!\n");

	dev_info->backing_buffer = kzalloc(dev_info->screen_size, GFP_KERNEL);

	if (!dev_info->backing_buffer)
		printk("non posso allocare il backing buffer\n");

	/* info->var = dev_info->si; */

	info->var.bits_per_pixel = 16;
	info->var.activate = FB_ACTIVATE_TEST;
	info->var.vmode = FB_VMODE_NONINTERLACED;

	info->var.red.offset = 11;
	info->var.red.length = 5;
	info->var.red.msb_right = 0;

	info->var.green.offset = 5;
	info->var.green.length = 6;
	info->var.green.msb_right = 0;

	info->var.blue.offset = 0;
	info->var.blue.length = 5;
	info->var.blue.msb_right = 0;

	/* info->var.pixclock =  (10000000 / FB_W * 1000 / FB_H)/2 ; */

	info->fix.smem_start = (unsigned long)info->screen_base;
	info->fix.smem_len = PAGE_ALIGN(dev_info->screen_size);
	memcpy(info->fix.id, "DisplayLink FB", 14);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.accel = info->flags;
	info->fix.line_length = dev_info->line_length;

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0)
		goto out1;

	printk("colormap allocated\n");
	if (register_framebuffer(info) < 0)
		goto out2;

	draw_rect(dev_info, 0, 0, dev_info->info->var.xres,
		  dev_info->info->var.yres, 0x30, 0xff, 0x30);

	return 0;

 out2:
	fb_dealloc_cmap(&info->cmap);
 out1:
	rvfree(info->screen_base, dev_info->screen_size);
 out0:
	framebuffer_release(info);
 out:
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev_info->udev);
	kfree(dev_info);
	return -ENOMEM;

}

static void dlfb_disconnect(struct usb_interface *interface)
{
	struct dlfb_data *dev_info = usb_get_intfdata(interface);

	mutex_unlock(&dev_info->bulk_mutex);

	usb_kill_urb(dev_info->tx_urb);
	usb_free_urb(dev_info->tx_urb);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev_info->udev);

	if (dev_info->info) {
		unregister_framebuffer(dev_info->info);
		fb_dealloc_cmap(&dev_info->info->cmap);
		rvfree(dev_info->info->screen_base, dev_info->screen_size);
		kfree(dev_info->backing_buffer);
		framebuffer_release(dev_info->info);

	}

	kfree(dev_info);

	printk("DisplayLink device disconnected\n");
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

	dlfb_init_modes();

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

MODULE_AUTHOR("Roberto De Ioris <roberto@unbit.it>");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
