// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013 Matrox Graphics
 *
 * Author: Christopher Harvey <charvey@matrox.com>
 */

#include <linux/pci.h>

#include "mgag200_drv.h"

static bool warn_transparent = true;
static bool warn_palette = true;

static int mgag200_cursor_update(struct mga_device *mdev, void *dst, void *src,
				 unsigned int width, unsigned int height)
{
	struct drm_device *dev = mdev->dev;
	unsigned int i, row, col;
	uint32_t colour_set[16];
	uint32_t *next_space = &colour_set[0];
	uint32_t *palette_iter;
	uint32_t this_colour;
	bool found = false;
	int colour_count = 0;
	u8 reg_index;
	u8 this_row[48];

	memset(&colour_set[0], 0, sizeof(uint32_t)*16);
	/* width*height*4 = 16384 */
	for (i = 0; i < 16384; i += 4) {
		this_colour = ioread32(src + i);
		/* No transparency */
		if (this_colour>>24 != 0xff &&
			this_colour>>24 != 0x0) {
			if (warn_transparent) {
				dev_info(&dev->pdev->dev, "Video card doesn't support cursors with partial transparency.\n");
				dev_info(&dev->pdev->dev, "Not enabling hardware cursor.\n");
				warn_transparent = false; /* Only tell the user once. */
			}
			return -EINVAL;
		}
		/* Don't need to store transparent pixels as colours */
		if (this_colour>>24 == 0x0)
			continue;
		found = false;
		for (palette_iter = &colour_set[0]; palette_iter != next_space; palette_iter++) {
			if (*palette_iter == this_colour) {
				found = true;
				break;
			}
		}
		if (found)
			continue;
		/* We only support 4bit paletted cursors */
		if (colour_count >= 16) {
			if (warn_palette) {
				dev_info(&dev->pdev->dev, "Video card only supports cursors with up to 16 colours.\n");
				dev_info(&dev->pdev->dev, "Not enabling hardware cursor.\n");
				warn_palette = false; /* Only tell the user once. */
			}
			return -EINVAL;
		}
		*next_space = this_colour;
		next_space++;
		colour_count++;
	}

	/* Program colours from cursor icon into palette */
	for (i = 0; i < colour_count; i++) {
		if (i <= 2)
			reg_index = 0x8 + i*0x4;
		else
			reg_index = 0x60 + i*0x3;
		WREG_DAC(reg_index, colour_set[i] & 0xff);
		WREG_DAC(reg_index+1, colour_set[i]>>8 & 0xff);
		WREG_DAC(reg_index+2, colour_set[i]>>16 & 0xff);
		BUG_ON((colour_set[i]>>24 & 0xff) != 0xff);
	}

	/* now write colour indices into hardware cursor buffer */
	for (row = 0; row < 64; row++) {
		memset(&this_row[0], 0, 48);
		for (col = 0; col < 64; col++) {
			this_colour = ioread32(src + 4*(col + 64*row));
			/* write transparent pixels */
			if (this_colour>>24 == 0x0) {
				this_row[47 - col/8] |= 0x80>>(col%8);
				continue;
			}

			/* write colour index here */
			for (i = 0; i < colour_count; i++) {
				if (colour_set[i] == this_colour) {
					if (col % 2)
						this_row[col/2] |= i<<4;
					else
						this_row[col/2] |= i;
					break;
				}
			}
		}
		memcpy_toio(dst + row*48, &this_row[0], 48);
	}

	return 0;
}

static void mgag200_cursor_set_base(struct mga_device *mdev, u64 address)
{
	u8 addrl = (address >> 10) & 0xff;
	u8 addrh = (address >> 18) & 0x3f;

	/* Program gpu address of cursor buffer */
	WREG_DAC(MGA1064_CURSOR_BASE_ADR_LOW, addrl);
	WREG_DAC(MGA1064_CURSOR_BASE_ADR_HI, addrh);
}

static int mgag200_show_cursor(struct mga_device *mdev, void *src,
			       unsigned int width, unsigned int height)
{
	struct drm_device *dev = mdev->dev;
	struct drm_gem_vram_object *gbo;
	void *dst;
	s64 off;
	int ret;

	gbo = mdev->cursor.gbo[mdev->cursor.next_index];
	if (!gbo) {
		WREG8(MGA_CURPOSXL, 0);
		WREG8(MGA_CURPOSXH, 0);
		return -ENOTSUPP; /* Didn't allocate space for cursors */
	}
	dst = drm_gem_vram_vmap(gbo);
	if (IS_ERR(dst)) {
		ret = PTR_ERR(dst);
		dev_err(&dev->pdev->dev,
			"failed to map cursor updates: %d\n", ret);
		return ret;
	}
	off = drm_gem_vram_offset(gbo);
	if (off < 0) {
		ret = (int)off;
		dev_err(&dev->pdev->dev,
			"failed to get cursor scanout address: %d\n", ret);
		goto err_drm_gem_vram_vunmap;
	}

	ret = mgag200_cursor_update(mdev, dst, src, width, height);
	if (ret)
		goto err_drm_gem_vram_vunmap;
	mgag200_cursor_set_base(mdev, off);

	/* Adjust cursor control register to turn on the cursor */
	WREG_DAC(MGA1064_CURSOR_CTL, 4); /* 16-colour palletized cursor mode */

	drm_gem_vram_vunmap(gbo, dst);

	++mdev->cursor.next_index;
	mdev->cursor.next_index %= ARRAY_SIZE(mdev->cursor.gbo);

	return 0;

err_drm_gem_vram_vunmap:
	drm_gem_vram_vunmap(gbo, dst);
	return ret;
}

/*
 * Hide the cursor off screen. We can't disable the cursor hardware because
 * it takes too long to re-activate and causes momentary corruption.
 */
static void mgag200_hide_cursor(struct mga_device *mdev)
{
	WREG8(MGA_CURPOSXL, 0);
	WREG8(MGA_CURPOSXH, 0);
}

static void mgag200_move_cursor(struct mga_device *mdev, int x, int y)
{
	if (WARN_ON(x <= 0))
		return;
	if (WARN_ON(y <= 0))
		return;
	if (WARN_ON(x & ~0xffff))
		return;
	if (WARN_ON(y & ~0xffff))
		return;

	WREG8(MGA_CURPOSXL, x & 0xff);
	WREG8(MGA_CURPOSXH, (x>>8) & 0xff);

	WREG8(MGA_CURPOSYL, y & 0xff);
	WREG8(MGA_CURPOSYH, (y>>8) & 0xff);
}

int mgag200_cursor_init(struct mga_device *mdev)
{
	struct drm_device *dev = mdev->dev;
	size_t ncursors = ARRAY_SIZE(mdev->cursor.gbo);
	size_t size;
	int ret;
	size_t i;
	struct drm_gem_vram_object *gbo;

	size = roundup(64 * 48, PAGE_SIZE);
	if (size * ncursors > mdev->vram_fb_available)
		return -ENOMEM;

	for (i = 0; i < ncursors; ++i) {
		gbo = drm_gem_vram_create(dev, size, 0);
		if (IS_ERR(gbo)) {
			ret = PTR_ERR(gbo);
			goto err_drm_gem_vram_put;
		}
		ret = drm_gem_vram_pin(gbo, DRM_GEM_VRAM_PL_FLAG_VRAM |
					    DRM_GEM_VRAM_PL_FLAG_TOPDOWN);
		if (ret) {
			drm_gem_vram_put(gbo);
			goto err_drm_gem_vram_put;
		}

		mdev->cursor.gbo[i] = gbo;
	}

	/*
	 * At the high end of video memory, we reserve space for
	 * buffer objects. The cursor plane uses this memory to store
	 * a double-buffered image of the current cursor. Hence, it's
	 * not available for framebuffers.
	 */
	mdev->vram_fb_available -= ncursors * size;

	return 0;

err_drm_gem_vram_put:
	while (i) {
		--i;
		gbo = mdev->cursor.gbo[i];
		drm_gem_vram_unpin(gbo);
		drm_gem_vram_put(gbo);
		mdev->cursor.gbo[i] = NULL;
	}
	return ret;
}

void mgag200_cursor_fini(struct mga_device *mdev)
{
	size_t i;
	struct drm_gem_vram_object *gbo;

	for (i = 0; i < ARRAY_SIZE(mdev->cursor.gbo); ++i) {
		gbo = mdev->cursor.gbo[i];
		drm_gem_vram_unpin(gbo);
		drm_gem_vram_put(gbo);
	}
}

int mgag200_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
			    uint32_t handle, uint32_t width, uint32_t height)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct drm_gem_object *obj;
	struct drm_gem_vram_object *gbo = NULL;
	int ret;
	u8 *src;

	if (!handle || !file_priv) {
		mgag200_hide_cursor(mdev);
		return 0;
	}

	if (width != 64 || height != 64) {
		WREG8(MGA_CURPOSXL, 0);
		WREG8(MGA_CURPOSXH, 0);
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj)
		return -ENOENT;
	gbo = drm_gem_vram_of_gem(obj);
	src = drm_gem_vram_vmap(gbo);
	if (IS_ERR(src)) {
		ret = PTR_ERR(src);
		dev_err(&dev->pdev->dev,
			"failed to map user buffer updates\n");
		goto err_drm_gem_object_put_unlocked;
	}

	ret = mgag200_show_cursor(mdev, src, width, height);
	if (ret)
		goto err_drm_gem_vram_vunmap;

	/* Now update internal buffer pointers */
	drm_gem_vram_vunmap(gbo, src);
	drm_gem_object_put_unlocked(obj);

	return 0;
err_drm_gem_vram_vunmap:
	drm_gem_vram_vunmap(gbo, src);
err_drm_gem_object_put_unlocked:
	drm_gem_object_put_unlocked(obj);
	return ret;
}

int mgag200_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct mga_device *mdev = to_mga_device(crtc->dev);

	/* Our origin is at (64,64) */
	x += 64;
	y += 64;

	mgag200_move_cursor(mdev, x, y);

	return 0;
}
