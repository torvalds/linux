/* SPDX-License-Identifier: MIT */
#ifndef __NV50_TILE_H__
#define __NV50_TILE_H__

#include <linux/types.h>
#include <linux/math.h>

/*
 * Tiling parameters for NV50+.
 * GOB = Group of bytes, the main unit for tiling blocks.
 * Tiling blocks are a power of 2 number of GOB.
 * All GOBs and blocks have the same width: 64 bytes (so 16 pixels in 32bits).
 * tile_mode is the log2 of the number of GOB per block.
 */

#define NV_TILE_GOB_HEIGHT_TESLA 4	/* 4 x 64 bytes = 256 bytes for a GOB on Tesla*/
#define NV_TILE_GOB_HEIGHT 8		/* 8 x 64 bytes = 512 bytes for a GOB on Fermi and later */
#define NV_TILE_GOB_WIDTH_BYTES 64

/* Number of blocks to cover the width of the framebuffer */
static inline u32 nouveau_get_width_in_blocks(u32 stride)
{
	return DIV_ROUND_UP(stride, NV_TILE_GOB_WIDTH_BYTES);
}

/* Return the height in pixel of one GOB */
static inline u32 nouveau_get_gob_height(u16 family)
{
	if (family == NV_DEVICE_INFO_V0_TESLA)
		return NV_TILE_GOB_HEIGHT_TESLA;
	else
		return NV_TILE_GOB_HEIGHT;
}

/* Number of blocks to cover the heigth of the framebuffer */
static inline u32 nouveau_get_height_in_blocks(u32 height, u32 gobs_in_block, u16 family)
{
	return DIV_ROUND_UP(height, nouveau_get_gob_height(family) * gobs_in_block);
}

/* Return the GOB size in bytes */
static inline u32 nouveau_get_gob_size(u16 family)
{
	return nouveau_get_gob_height(family) * NV_TILE_GOB_WIDTH_BYTES;
}

/* Return the number of GOB in a block */
static inline int nouveau_get_gobs_in_block(u32 tile_mode, u16 chipset)
{
	if (chipset >= 0xc0)
		return 1 << (tile_mode >> 4);
	return 1 << tile_mode;
}

/* Return true if tile_mode is invalid */
static inline bool nouveau_check_tile_mode(u32 tile_mode, u16 chipset)
{
	if (chipset >= 0xc0)
		return (tile_mode & 0xfffff0f);
	return (tile_mode & 0xfffffff0);
}

#endif
