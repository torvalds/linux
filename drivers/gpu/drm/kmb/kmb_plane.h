/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright © 2018-2020 Intel Corporation
 */

#ifndef __KMB_PLANE_H__
#define __KMB_PLANE_H__

#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>

#define LCD_INT_VL0_ERR ((LAYER0_DMA_FIFO_UNDERFLOW) | \
			(LAYER0_DMA_FIFO_OVERFLOW) | \
			(LAYER0_DMA_CB_FIFO_OVERFLOW) | \
			(LAYER0_DMA_CB_FIFO_UNDERFLOW) | \
			(LAYER0_DMA_CR_FIFO_OVERFLOW) | \
			(LAYER0_DMA_CR_FIFO_UNDERFLOW))

#define LCD_INT_VL1_ERR ((LAYER1_DMA_FIFO_UNDERFLOW) | \
			(LAYER1_DMA_FIFO_OVERFLOW) | \
			(LAYER1_DMA_CB_FIFO_OVERFLOW) | \
			(LAYER1_DMA_CB_FIFO_UNDERFLOW) | \
			(LAYER1_DMA_CR_FIFO_OVERFLOW) | \
			(LAYER1_DMA_CR_FIFO_UNDERFLOW))

#define LCD_INT_GL0_ERR (LAYER2_DMA_FIFO_OVERFLOW | LAYER2_DMA_FIFO_UNDERFLOW)
#define LCD_INT_GL1_ERR (LAYER3_DMA_FIFO_OVERFLOW | LAYER3_DMA_FIFO_UNDERFLOW)
#define LCD_INT_VL0 (LAYER0_DMA_DONE | LAYER0_DMA_IDLE | LCD_INT_VL0_ERR)
#define LCD_INT_VL1 (LAYER1_DMA_DONE | LAYER1_DMA_IDLE | LCD_INT_VL1_ERR)
#define LCD_INT_GL0 (LAYER2_DMA_DONE | LAYER2_DMA_IDLE | LCD_INT_GL0_ERR)
#define LCD_INT_GL1 (LAYER3_DMA_DONE | LAYER3_DMA_IDLE | LCD_INT_GL1_ERR)
#define LCD_INT_DMA_ERR (LCD_INT_VL0_ERR | LCD_INT_VL1_ERR \
		| LCD_INT_GL0_ERR | LCD_INT_GL1_ERR)

#define POSSIBLE_CRTCS 1
#define to_kmb_plane(x) container_of(x, struct kmb_plane, base_plane)

enum layer_id {
	LAYER_0,
	LAYER_1,
	LAYER_2,
	LAYER_3,
	/* KMB_MAX_PLANES */
};

#define KMB_MAX_PLANES 1

enum sub_plane_id {
	Y_PLANE,
	U_PLANE,
	V_PLANE,
	MAX_SUB_PLANES,
};

struct kmb_plane {
	struct drm_plane base_plane;
	unsigned char id;
};

struct layer_status {
	bool disable;
	u32 ctrl;
};

struct kmb_plane *kmb_plane_init(struct drm_device *drm);
void kmb_plane_destroy(struct drm_plane *plane);
#endif /* __KMB_PLANE_H__ */
