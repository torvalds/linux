/*
 * Copyright 2004 The Unichrome Project. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE UNICHROME PROJECT, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Thomas Hellström 2004.
 */

#ifndef _VIA_VERIFIER_H_
#define _VIA_VERIFIER_H_

typedef enum{
	no_sequence = 0, 
	z_address,
	dest_address,
	tex_address
}drm_via_sequence_t;



typedef struct{
	unsigned texture;
	uint32_t z_addr; 
	uint32_t d_addr; 
        uint32_t t_addr[2][10];
	uint32_t pitch[2][10];
	uint32_t height[2][10];
	uint32_t tex_level_lo[2]; 
	uint32_t tex_level_hi[2];
	uint32_t tex_palette_size[2];
	drm_via_sequence_t unfinished;
	int agp_texture;
	int multitex;
	drm_device_t *dev;
	drm_map_t *map_cache;
	uint32_t vertex_count;
	int agp;
	const uint32_t *buf_start;
} drm_via_state_t;

extern int via_verify_command_stream(const uint32_t * buf, unsigned int size, 
				     drm_device_t *dev, int agp);

#endif
