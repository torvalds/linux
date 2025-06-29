// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/uaccess.h>

ssize_t fb_io_read(struct fb_info *info, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	u8 *buffer, *dst;
	u8 __iomem *src;
	int c, cnt = 0, err = 0;
	unsigned long total_size, trailing;

	if (info->flags & FBINFO_VIRTFB)
		fb_warn_once(info, "Framebuffer is not in I/O address space.");

	if (!info->screen_base)
		return -ENODEV;

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

	src = (u8 __iomem *) (info->screen_base + p);

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	while (count) {
		c  = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		dst = buffer;
		fb_memcpy_fromio(dst, src, c);
		dst += c;
		src += c;

		trailing = copy_to_user(buf, buffer, c);
		if (trailing == c) {
			err = -EFAULT;
			break;
		}
		c -= trailing;

		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return cnt ? cnt : err;
}
EXPORT_SYMBOL(fb_io_read);

ssize_t fb_io_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	u8 *buffer, *src;
	u8 __iomem *dst;
	int c, cnt = 0, err = 0;
	unsigned long total_size, trailing;

	if (info->flags & FBINFO_VIRTFB)
		fb_warn_once(info, "Framebuffer is not in I/O address space.");

	if (!info->screen_base)
		return -ENODEV;

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

	dst = (u8 __iomem *) (info->screen_base + p);

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	while (count) {
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		src = buffer;

		trailing = copy_from_user(src, buf, c);
		if (trailing == c) {
			err = -EFAULT;
			break;
		}
		c -= trailing;

		fb_memcpy_toio(dst, src, c);
		dst += c;
		src += c;
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (cnt) ? cnt : err;
}
EXPORT_SYMBOL(fb_io_write);

int fb_io_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start = info->fix.smem_start;
	u32 len = info->fix.smem_len;
	unsigned long mmio_pgoff = PAGE_ALIGN((start & ~PAGE_MASK) + len) >> PAGE_SHIFT;

	if (info->flags & FBINFO_VIRTFB)
		fb_warn_once(info, "Framebuffer is not in I/O address space.");

	/*
	 * This can be either the framebuffer mapping, or if pgoff points
	 * past it, the mmio mapping.
	 */
	if (vma->vm_pgoff >= mmio_pgoff) {
		if (info->var.accel_flags)
			return -EINVAL;

		vma->vm_pgoff -= mmio_pgoff;
		start = info->fix.mmio_start;
		len = info->fix.mmio_len;
	}

	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	vma->vm_page_prot = pgprot_framebuffer(vma->vm_page_prot, vma->vm_start,
					       vma->vm_end, start);

	return vm_iomap_memory(vma, start, len);
}
EXPORT_SYMBOL(fb_io_mmap);

MODULE_DESCRIPTION("Fbdev helpers for framebuffers in I/O memory");
MODULE_LICENSE("GPL");
