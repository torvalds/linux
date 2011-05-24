/*
 * Copyright 2010 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     Alex Deucher <alexander.deucher@amd.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>

/*
 * evergreen cards need to use the 3D engine to blit data which requires
 * quite a bit of hw state setup.  Rather than pull the whole 3D driver
 * (which normally generates the 3D state) into the DRM, we opt to use
 * statically generated state tables.  The regsiter state and shaders
 * were hand generated to support blitting functionality.  See the 3D
 * driver or documentation for descriptions of the registers and
 * shader instructions.
 */

const u32 cayman_default_state[] =
{
	/* XXX fill in additional blit state */

	0xc0026900,
	0x00000316,
	0x0000000e, /* VGT_VERTEX_REUSE_BLOCK_CNTL */
	0x00000010, /*  */

	0xc0026900,
	0x000000d9,
	0x00000000, /* CP_RINGID */
	0x00000000, /* CP_VMID */
};

const u32 cayman_default_size = ARRAY_SIZE(cayman_default_state);
