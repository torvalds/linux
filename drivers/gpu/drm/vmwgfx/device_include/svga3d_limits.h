/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2012-2021 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * svga3d_limits.h --
 *
 *    SVGA 3d hardware limits
 */



#ifndef _SVGA3D_LIMITS_H_
#define _SVGA3D_LIMITS_H_

#define SVGA3D_HB_MAX_CONTEXT_IDS 256
#define SVGA3D_HB_MAX_SURFACE_IDS (32 * 1024)

#define SVGA3D_DX_MAX_RENDER_TARGETS 8
#define SVGA3D_DX11_MAX_UAVIEWS 8
#define SVGA3D_DX11_1_MAX_UAVIEWS 64
#define SVGA3D_MAX_UAVIEWS (SVGA3D_DX11_1_MAX_UAVIEWS)
#define SVGA3D_DX11_MAX_SIMULTANEOUS_RTUAV (SVGA3D_DX11_MAX_UAVIEWS)
#define SVGA3D_DX11_1_MAX_SIMULTANEOUS_RTUAV (SVGA3D_DX11_1_MAX_UAVIEWS)
#define SVGA3D_MAX_SIMULTANEOUS_RTUAV (SVGA3D_MAX_UAVIEWS)

#define SVGA3D_HB_MAX_SURFACE_SIZE MBYTES_2_BYTES(128)

#define SVGA3D_MAX_SHADERIDS 5000

#define SVGA3D_MAX_SIMULTANEOUS_SHADERS 20000

#define SVGA3D_NUM_TEXTURE_UNITS 32
#define SVGA3D_NUM_LIGHTS 8

#define SVGA3D_MAX_VIDEOPROCESSOR_SAMPLERS 32

#define SVGA3D_MAX_SHADER_MEMORY_BYTES (8 * 1024 * 1024)
#define SVGA3D_MAX_SHADER_MEMORY                                               \
	(SVGA3D_MAX_SHADER_MEMORY_BYTES / sizeof(uint32))

#define SVGA3D_MAX_SHADER_THREAD_GROUPS 65535

#define SVGA3D_MAX_CLIP_PLANES 6

#define SVGA3D_MAX_TEXTURE_COORDS 8

#define SVGA3D_MAX_SURFACE_FACES 6

#define SVGA3D_SM4_MAX_SURFACE_ARRAYSIZE 512
#define SVGA3D_SM5_MAX_SURFACE_ARRAYSIZE 2048
#define SVGA3D_MAX_SURFACE_ARRAYSIZE SVGA3D_SM5_MAX_SURFACE_ARRAYSIZE

#define SVGA3D_MAX_VERTEX_ARRAYS 32

#define SVGA3D_MAX_DRAW_PRIMITIVE_RANGES 32

#define SVGA3D_MAX_SAMPLES 8

#define SVGA3D_MIN_SBX_DATA_SIZE (GBYTES_2_BYTES(1))
#define SVGA3D_MAX_SBX_DATA_SIZE (GBYTES_2_BYTES(4))

#define SVGA3D_MIN_SBX_DATA_SIZE_DVM (MBYTES_2_BYTES(900))
#define SVGA3D_MAX_SBX_DATA_SIZE_DVM (MBYTES_2_BYTES(910))
#endif
