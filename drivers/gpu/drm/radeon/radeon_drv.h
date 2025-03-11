/* radeon_drv.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * All rights reserved.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#ifndef __RADEON_DRV_H__
#define __RADEON_DRV_H__

#include <linux/firmware.h>
#include <linux/platform_device.h>

#include "radeon_family.h"

/* General customization:
 */

#define DRIVER_AUTHOR		"Gareth Hughes, Keith Whitwell, others."

#define DRIVER_NAME		"radeon"
#define DRIVER_DESC		"ATI Radeon"

/* Interface history:
 *
 * 1.1 - ??
 * 1.2 - Add vertex2 ioctl (keith)
 *     - Add stencil capability to clear ioctl (gareth, keith)
 *     - Increase MAX_TEXTURE_LEVELS (brian)
 * 1.3 - Add cmdbuf ioctl (keith)
 *     - Add support for new radeon packets (keith)
 *     - Add getparam ioctl (keith)
 *     - Add flip-buffers ioctl, deprecate fullscreen foo (keith).
 * 1.4 - Add scratch registers to get_param ioctl.
 * 1.5 - Add r200 packets to cmdbuf ioctl
 *     - Add r200 function to init ioctl
 *     - Add 'scalar2' instruction to cmdbuf
 * 1.6 - Add static GART memory manager
 *       Add irq handler (won't be turned on unless X server knows to)
 *       Add irq ioctls and irq_active getparam.
 *       Add wait command for cmdbuf ioctl
 *       Add GART offset query for getparam
 * 1.7 - Add support for cube map registers: R200_PP_CUBIC_FACES_[0..5]
 *       and R200_PP_CUBIC_OFFSET_F1_[0..5].
 *       Added packets R200_EMIT_PP_CUBIC_FACES_[0..5] and
 *       R200_EMIT_PP_CUBIC_OFFSETS_[0..5].  (brian)
 * 1.8 - Remove need to call cleanup ioctls on last client exit (keith)
 *       Add 'GET' queries for starting additional clients on different VT's.
 * 1.9 - Add DRM_IOCTL_RADEON_CP_RESUME ioctl.
 *       Add texture rectangle support for r100.
 * 1.10- Add SETPARAM ioctl; first parameter to set is FB_LOCATION, which
 *       clients use to tell the DRM where they think the framebuffer is
 *       located in the card's address space
 * 1.11- Add packet R200_EMIT_RB3D_BLENDCOLOR to support GL_EXT_blend_color
 *       and GL_EXT_blend_[func|equation]_separate on r200
 * 1.12- Add R300 CP microcode support - this just loads the CP on r300
 *       (No 3D support yet - just microcode loading).
 * 1.13- Add packet R200_EMIT_TCL_POINT_SPRITE_CNTL for ARB_point_parameters
 *     - Add hyperz support, add hyperz flags to clear ioctl.
 * 1.14- Add support for color tiling
 *     - Add R100/R200 surface allocation/free support
 * 1.15- Add support for texture micro tiling
 *     - Add support for r100 cube maps
 * 1.16- Add R200_EMIT_PP_TRI_PERF_CNTL packet to support brilinear
 *       texture filtering on r200
 * 1.17- Add initial support for R300 (3D).
 * 1.18- Add support for GL_ATI_fragment_shader, new packets
 *       R200_EMIT_PP_AFS_0/1, R200_EMIT_PP_TXCTLALL_0-5 (replaces
 *       R200_EMIT_PP_TXFILTER_0-5, 2 more regs) and R200_EMIT_ATF_TFACTOR
 *       (replaces R200_EMIT_TFACTOR_0 (8 consts instead of 6)
 * 1.19- Add support for gart table in FB memory and PCIE r300
 * 1.20- Add support for r300 texrect
 * 1.21- Add support for card type getparam
 * 1.22- Add support for texture cache flushes (R300_TX_CNTL)
 * 1.23- Add new radeon memory map work from benh
 * 1.24- Add general-purpose packet for manipulating scratch registers (r300)
 * 1.25- Add support for r200 vertex programs (R200_EMIT_VAP_PVS_CNTL,
 *       new packet type)
 * 1.26- Add support for variable size PCI(E) gart aperture
 * 1.27- Add support for IGP GART
 * 1.28- Add support for VBL on CRTC2
 * 1.29- R500 3D cmd buffer support
 * 1.30- Add support for occlusion queries
 * 1.31- Add support for num Z pipes from GET_PARAM
 * 1.32- fixes for rv740 setup
 * 1.33- Add r6xx/r7xx const buffer support
 * 1.34- fix evergreen/cayman GS register
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		34
#define DRIVER_PATCHLEVEL	0

long radeon_drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg);

int radeon_driver_load_kms(struct drm_device *dev, unsigned long flags);
void radeon_driver_unload_kms(struct drm_device *dev);
int radeon_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv);
void radeon_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv);

/* atpx handler */
#if defined(CONFIG_VGA_SWITCHEROO)
void radeon_register_atpx_handler(void);
void radeon_unregister_atpx_handler(void);
bool radeon_has_atpx_dgpu_power_cntl(void);
bool radeon_is_atpx_hybrid(void);
#else
static inline void radeon_register_atpx_handler(void) {}
static inline void radeon_unregister_atpx_handler(void) {}
static inline bool radeon_has_atpx_dgpu_power_cntl(void) { return false; }
static inline bool radeon_is_atpx_hybrid(void) { return false; }
#endif

#endif				/* __RADEON_DRV_H__ */
