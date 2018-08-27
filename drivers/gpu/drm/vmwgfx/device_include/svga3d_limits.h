/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 * Copyright 2007-2015 VMware, Inc.
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
 **********************************************************/

/*
 * svga3d_limits.h --
 *
 *       SVGA 3d hardware limits
 */

#ifndef _SVGA3D_LIMITS_H_
#define _SVGA3D_LIMITS_H_

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE

#include "includeCheck.h"

#define SVGA3D_NUM_CLIPPLANES                   6
#define SVGA3D_MAX_RENDER_TARGETS               8
#define SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS  (SVGA3D_MAX_RENDER_TARGETS)
#define SVGA3D_MAX_UAVIEWS                      8
#define SVGA3D_MAX_CONTEXT_IDS                  256
#define SVGA3D_MAX_SURFACE_IDS                  (32 * 1024)

/*
 * Maximum ID a shader can be assigned on a given context.
 */
#define SVGA3D_MAX_SHADERIDS                    5000
/*
 * Maximum number of shaders of a given type that can be defined
 * (including all contexts).
 */
#define SVGA3D_MAX_SIMULTANEOUS_SHADERS         20000

#define SVGA3D_NUM_TEXTURE_UNITS                32
#define SVGA3D_NUM_LIGHTS                       8

/*
 * Maximum size in dwords of shader text the SVGA device will allow.
 * Currently 8 MB.
 */
#define SVGA3D_MAX_SHADER_MEMORY_BYTES (8 * 1024 * 1024)
#define SVGA3D_MAX_SHADER_MEMORY  (SVGA3D_MAX_SHADER_MEMORY_BYTES / \
                                   sizeof(uint32))

#define SVGA3D_MAX_CLIP_PLANES    6

/*
 * This is the limit to the number of fixed-function texture
 * transforms and texture coordinates we can support. It does *not*
 * correspond to the number of texture image units (samplers) we
 * support!
 */
#define SVGA3D_MAX_TEXTURE_COORDS 8

/*
 * Number of faces in a cubemap.
 */
#define SVGA3D_MAX_SURFACE_FACES 6

/*
 * Maximum number of array indexes in a GB surface (with DX enabled).
 */
#define SVGA3D_MAX_SURFACE_ARRAYSIZE 512

/*
 * The maximum number of vertex arrays we're guaranteed to support in
 * SVGA_3D_CMD_DRAWPRIMITIVES.
 */
#define SVGA3D_MAX_VERTEX_ARRAYS   32

/*
 * The maximum number of primitive ranges we're guaranteed to support
 * in SVGA_3D_CMD_DRAWPRIMITIVES.
 */
#define SVGA3D_MAX_DRAW_PRIMITIVE_RANGES 32

#endif /* _SVGA3D_LIMITS_H_ */
