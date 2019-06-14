// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013 Matrox Graphics
 *
 * Author: Christopher Harvey <charvey@matrox.com>
 */

#include <drm/drmP.h>
#include "mgag200_drv.h"

static bool warn_transparent = true;
static bool warn_palette = true;

/*
  Hide the cursor off screen. We can't disable the cursor hardware because it
  takes too long to re-activate and causes momentary corruption
*/
static void mga_hide_cursor(struct mga_device *mdev)
{
	WREG8(MGA_CURPOSXL, 0);
	WREG8(MGA_CURPOSXH, 0);
	if (mdev->cursor.pixels_current)
		drm_gem_vram_unpin(mdev->cursor.pixels_current);
	mdev->cursor.pixels_current = NULL;
}

int mga_crtc_cursor_set(struct drm_crtc *crtc,
			struct drm_file *file_priv,
			uint32_t handle,
			uint32_t width,
			uint32_t height)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = (struct mga_device *)dev->dev_private;
	struct drm_gem_vram_object *pixels_1 = mdev->cursor.pixels_1;
	struct drm_gem_vram_object *pixels_2 = mdev->cursor.pixels_2;
	struct drm_gem_vram_object *pixels_current = mdev->cursor.pixels_current;
	struct drm_gem_vram_object *pixels_next;
	struct drm_gem_object *obj;
	struct drm_gem_vram_object *gbo = NULL;
	int ret = 0;
	u8 *src, *dst;
	unsigned int i, row, col;
	uint32_t colour_set[16];
	uint32_t *next_space = &colour_set[0];
	uint32_t *palette_iter;
	uint32_t this_colour;
	bool found = false;
	int colour_count = 0;
	s64 gpu_addr;
	u64 dst_gpu;
	u8 reg_index;
	u8 this_row[48];

	if (!pixels_1 || !pixels_2) {
		WREG8(MGA_CURPOSXL, 0);
		WREG8(MGA_CURPOSXH, 0);
		return -ENOTSUPP; /* Didn't allocate space for cursors */
	}

	if (WARN_ON(pixels_current &&
		    pixels_1 != pixels_current &&
		    pixels_2 != pixels_current)) {
		return -ENOTSUPP; /* inconsistent state */
	}

	if (!handle || !file_priv) {
		mga_hide_cursor(mdev);
		return 0;
	}

	if (width != 64 || height != 64) {
		WREG8(MGA_CURPOSXL, 0);
		WREG8(MGA_CURPOSXH, 0);
		return -EINVAL;
	}

	if (pixels_current == pixels_1)
		pixels_next = pixels_2;
	else
		pixels_next = pixels_1;

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj)
		return -ENOENT;
	gbo = drm_gem_vram_of_gem(obj);
	ret = drm_gem_vram_pin(gbo, 0);
	if (ret) {
		dev_err(&dev->pdev->dev, "failed to lock user bo\n");
		goto err_drm_gem_object_put_unlocked;
	}
	src = drm_gem_vram_kmap(gbo, true, NULL);
	if (IS_ERR(src)) {
		ret = PTR_ERR(src);
		dev_err(&dev->pdev->dev,
			"failed to kmap user buffer updates\n");
		goto err_drm_gem_vram_unpin_src;
	}

	/* Pin and map up-coming buffer to write colour indices */
	ret = drm_gem_vram_pin(pixels_next, 0);
	if (ret) {
		dev_err(&dev->pdev->dev,
			"failed to pin cursor buffer: %d\n", ret);
		goto err_drm_gem_vram_kunmap_src;
	}
	dst = drm_gem_vram_kmap(pixels_next, true, NULL);
	if (IS_ERR(dst)) {
		ret = PTR_ERR(dst);
		dev_err(&dev->pdev->dev,
			"failed to kmap cursor updates: %d\n", ret);
		goto err_drm_gem_vram_unpin_dst;
	}
	gpu_addr = drm_gem_vram_offset(pixels_2);
	if (gpu_addr < 0) {
		ret = (int)gpu_addr;
		dev_err(&dev->pdev->dev,
			"failed to get cursor scanout address: %d\n", ret);
		goto err_drm_gem_vram_kunmap_dst;
	}
	dst_gpu = (u64)gpu_addr;

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
			ret = -EINVAL;
			goto err_drm_gem_vram_kunmap_dst;
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
			ret = -EINVAL;
			goto err_drm_gem_vram_kunmap_dst;
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

	/* Program gpu address of cursor buffer */
	WREG_DAC(MGA1064_CURSOR_BASE_ADR_LOW, (u8)((dst_gpu>>10) & 0xff));
	WREG_DAC(MGA1064_CURSOR_BASE_ADR_HI, (u8)((dst_gpu>>18) & 0x3f));

	/* Adjust cursor control register to turn on the cursor */
	WREG_DAC(MGA1064_CURSOR_CTL, 4); /* 16-colour palletized cursor mode */

	/* Now update internal buffer pointers */
	if (pixels_current)
		drm_gem_vram_unpin(pixels_current);
	mdev->cursor.pixels_current = pixels_next;

	drm_gem_vram_kunmap(pixels_next);
	drm_gem_vram_unpin(pixels_next);
	drm_gem_vram_kunmap(gbo);
	drm_gem_vram_unpin(gbo);
	drm_gem_object_put_unlocked(obj);

	return 0;

err_drm_gem_vram_kunmap_dst:
	drm_gem_vram_kunmap(pixels_next);
err_drm_gem_vram_unpin_dst:
	drm_gem_vram_unpin(pixels_next);
err_drm_gem_vram_kunmap_src:
	drm_gem_vram_kunmap(gbo);
err_drm_gem_vram_unpin_src:
	drm_gem_vram_unpin(gbo);
err_drm_gem_object_put_unlocked:
	drm_gem_object_put_unlocked(obj);
	return ret;
}

int mga_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct mga_device *mdev = (struct mga_device *)crtc->dev->dev_private;
	/* Our origin is at (64,64) */
	x += 64;
	y += 64;

	BUG_ON(x <= 0);
	BUG_ON(y <= 0);
	BUG_ON(x & ~0xffff);
	BUG_ON(y & ~0xffff);

	WREG8(MGA_CURPOSXL, x & 0xff);
	WREG8(MGA_CURPOSXH, (x>>8) & 0xff);

	WREG8(MGA_CURPOSYL, y & 0xff);
	WREG8(MGA_CURPOSYH, (y>>8) & 0xff);
	return 0;
}
