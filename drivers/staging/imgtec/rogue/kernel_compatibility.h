/*************************************************************************/ /*!
@File           services/server/env/linux/kernel_compatibility.h
@Title          Kernel versions compatibility macros
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Per-version macros to allow code to seamlessly use older kernel
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef __KERNEL_COMPATIBILITY_H__
#define __KERNEL_COMPATIBILITY_H__

#include <linux/version.h>

/*
 * Stop supporting an old kernel? Remove the top block.
 * New incompatible kernel?       Append a new block at the bottom.
 *
 * Please write you version test as `VERSION < X.Y`, and use the earliest
 * possible version :)
 */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0))

/* Linux 3.7 split VM_RESERVED into VM_DONTDUMP and VM_DONTEXPAND */
#define VM_DONTDUMP VM_RESERVED

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)) */

/*
 * Note: this fix had to be written backwards because get_unused_fd_flags
 * was already defined but not exported on kernels < 3.7
 *
 * When removing support for kernels < 3.7, this block should be removed
 * and all `get_unused_fd()` should be manually replaced with
 * `get_unused_fd_flags(0)`
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))

/* Linux 3.19 removed get_unused_fd() */
/* get_unused_fd_flags  was introduced in 3.7 */
#define get_unused_fd() get_unused_fd_flags(0)

#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))

/* Linux 3.12 introduced a new shrinker API */
#define SHRINK_STOP (~0UL)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)) && defined(CONFIG_ARM)

/* Linux 3.13 renamed ioremap_cached to ioremap_cache */
#define ioremap_cache(cookie,size) ioremap_cached(cookie,size)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)) && defined(CONFIG_ARM) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0))

/* Linux 3.17 changed the 3rd argument from a `struct page ***pages` to
 * `struct page **pages` */
#define map_vm_area(area, prot, pages) map_vm_area(area, prot, &pages)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0))

/*
 * Linux 4.7 removed this function but its replacement was available since 3.19.
 */
#define drm_crtc_send_vblank_event(crtc, e) drm_send_vblank_event((crtc)->dev, drm_crtc_index(crtc), e)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))

/* Linux 4.4 renamed GFP_WAIT to GFP_RECLAIM */
#define __GFP_RECLAIM __GFP_WAIT

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)) && !defined(CHROMIUMOS_WORKAROUNDS_KERNEL318)

/* Linux 4.5 added a new printf-style parameter for debug messages */

#define drm_encoder_init(dev, encoder, funcs, encoder_type, name, ...) \
        drm_encoder_init(dev, encoder, funcs, encoder_type)

#define drm_universal_plane_init(dev, plane, possible_crtcs, funcs, formats, format_count, type, name, ...) \
        drm_universal_plane_init(dev, plane, possible_crtcs, funcs, formats, format_count, type)

#define drm_crtc_init_with_planes(dev, crtc, primary, cursor, funcs, name, ...) \
        drm_crtc_init_with_planes(dev, crtc, primary, cursor, funcs)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)) && !defined(CHROMIUMOS_WORKAROUNDS_KERNEL318)

/*
 * Linux 4.6 removed the start and end arguments as it now always maps
 * the entire DMA-BUF.
 * Additionally, dma_buf_end_cpu_access() now returns an int error.
 */
#define dma_buf_begin_cpu_access(DMABUF, DIRECTION) dma_buf_begin_cpu_access(DMABUF, 0, DMABUF->size, DIRECTION)
#define dma_buf_end_cpu_access(DMABUF, DIRECTION) ({ dma_buf_end_cpu_access(DMABUF, 0, DMABUF->size, DIRECTION); 0; })

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)) && !defined(CHROMIUMOS_WORKAROUNDS_KERNEL318) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0))

/* Linux 4.7 removed the first arguments as it's never been used */
#define drm_gem_object_lookup(filp, handle) drm_gem_object_lookup((filp)->minor->dev, filp, handle)

/* Linux 4.7 replaced nla_put_u64 with nla_put_u64_64bit */
#define nla_put_u64_64bit(skb, attrtype, value, padattr) nla_put_u64(skb, attrtype, value)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))

/* Linux 4.9 change the second argument to a drm_file pointer */
#define drm_vma_node_is_allowed(node, file_priv) drm_vma_node_is_allowed(node, (file_priv)->filp)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)) */

#if !defined(CHROMIUMOS_WORKAROUNDS_KERNEL318)
#define dev_pm_opp_of_add_table of_init_opp_table
#define dev_pm_opp_of_remove_table of_free_opp_table
#endif


#endif /* __KERNEL_COMPATIBILITY_H__ */
/*****************************************************************************
 End of file (kernel_compatibility.h)
*****************************************************************************/
