// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/export.h>
#include <linux/fb.h>
#include <linux/mutex.h>
#include <linux/slab.h>

/**
 * framebuffer_alloc - creates a new frame buffer info structure
 *
 * @size: size of driver private data, can be zero
 * @dev: pointer to the device for this fb, this can be NULL
 *
 * Creates a new frame buffer info structure. Also reserves @size bytes
 * for driver private data (info->par). info->par (if any) will be
 * aligned to sizeof(long). The new instances of struct fb_info and
 * the driver private data are both cleared to zero.
 *
 * Returns the new structure, or NULL if an error occurred.
 *
 */
struct fb_info *framebuffer_alloc(size_t size, struct device *dev)
{
#define BYTES_PER_LONG (BITS_PER_LONG/8)
#define PADDING (BYTES_PER_LONG - (sizeof(struct fb_info) % BYTES_PER_LONG))
	int fb_info_size = sizeof(struct fb_info);
	struct fb_info *info;
	char *p;

	if (size)
		fb_info_size += PADDING;

	p = kzalloc(fb_info_size + size, GFP_KERNEL);

	if (!p)
		return NULL;

	info = (struct fb_info *) p;

	if (size)
		info->par = p + fb_info_size;

	info->device = dev;
	info->fbcon_rotate_hint = -1;
	info->blank = FB_BLANK_UNBLANK;

#if IS_ENABLED(CONFIG_FB_BACKLIGHT)
	mutex_init(&info->bl_curve_mutex);
#endif

	return info;
#undef PADDING
#undef BYTES_PER_LONG
}
EXPORT_SYMBOL(framebuffer_alloc);

/**
 * framebuffer_release - marks the structure available for freeing
 *
 * @info: frame buffer info structure
 *
 * Drop the reference count of the device embedded in the
 * framebuffer info structure.
 *
 */
void framebuffer_release(struct fb_info *info)
{
	if (!info)
		return;

	if (WARN_ON(refcount_read(&info->count)))
		return;

#if IS_ENABLED(CONFIG_FB_BACKLIGHT)
	mutex_destroy(&info->bl_curve_mutex);
#endif

	kfree(info);
}
EXPORT_SYMBOL(framebuffer_release);
