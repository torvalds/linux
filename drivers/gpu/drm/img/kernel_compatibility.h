/*************************************************************************/ /*!
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

/* Linux 3.6 introduced seq_vprintf(). Earlier versions don't have this
 * so we work around the limitation by vsnprintf() + seq_puts().
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
#define seq_vprintf(seq_file, fmt, args) \
do { \
	char aszBuffer[512]; /* maximum message buffer size */ \
	vsnprintf(aszBuffer, sizeof(aszBuffer), fmt, args); \
	seq_puts(seq_file, aszBuffer); \
} while (0)
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)) */

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
/* get_unused_fd_flags was introduced in 3.7 */
#define get_unused_fd() get_unused_fd_flags(0)

#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))

/*
 * Headers shouldn't normally be included by this file but this is a special
 * case as it's not obvious from the name that devfreq_add_device needs this
 * include.
 */
#include <linux/string.h>

#define devfreq_add_device(dev, profile, name, data) \
	({ \
		struct devfreq *__devfreq; \
		if (name && !strcmp(name, "simple_ondemand")) \
			__devfreq = devfreq_add_device(dev, profile, \
							   &devfreq_simple_ondemand, data); \
		else \
			__devfreq = ERR_PTR(-EINVAL); \
		__devfreq; \
	})

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))

#define DRIVER_RENDER 0
#define DRM_RENDER_ALLOW 0

/* Linux 3.12 introduced a new shrinker API */
#define SHRINK_STOP (~0UL)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))

#define dev_pm_opp_get_opp_count(dev) opp_get_opp_count(dev)
#define dev_pm_opp_get_freq(opp) opp_get_freq(opp)
#define dev_pm_opp_get_voltage(opp) opp_get_voltage(opp)
#define dev_pm_opp_add(dev, freq, u_volt) opp_add(dev, freq, u_volt)
#define dev_pm_opp_find_freq_ceil(dev, freq) opp_find_freq_ceil(dev, freq)

#if defined(CONFIG_ARM)
/* Linux 3.13 renamed ioremap_cached to ioremap_cache */
#define ioremap_cache(cookie, size) ioremap_cached(cookie, size)
#endif /* defined(CONFIG_ARM) */

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))

/* Linux 3.14 introduced a new set of sized min and max defines */
#ifndef U32_MAX
#define U32_MAX ((u32)UINT_MAX)
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)) */


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

/* seq_has_overflowed() was introduced in 3.19 but the structure elements
 * have been available since 2.x
 */
#include <linux/seq_file.h>
static inline bool seq_has_overflowed(struct seq_file *m)
{
	return m->count == m->size;
}

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))

#define debugfs_create_file_size(name, mode, parent, data, fops, file_size) \
	({ \
		struct dentry *de; \
		de = debugfs_create_file(name, mode, parent, data, fops); \
		if (de) \
			de->d_inode->i_size = file_size; \
		de; \
	})

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0))
#define drm_fb_helper_unregister_fbi(fb_helper) \
	({ \
		if ((fb_helper) && (fb_helper)->fbdev) \
			unregister_framebuffer((fb_helper)->fbdev); \
	})
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))

/* Linux 4.4 renamed GFP_WAIT to GFP_RECLAIM */
#define __GFP_RECLAIM __GFP_WAIT

#if !defined(CHROMIUMOS_KERNEL) || (LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0))
#define dev_pm_opp_of_add_table(dev) of_init_opp_table(dev)
#define dev_pm_opp_of_remove_table(dev) of_free_opp_table(dev)
#else
#define sync_fence_create(data_name, sync_pt) sync_fence_create(data_name, &(sync_pt)->base)
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)) && \
	(!defined(CHROMIUMOS_KERNEL) || (LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)))

/* Linux 4.5 added a new printf-style parameter for debug messages */

#define drm_encoder_init(dev, encoder, funcs, encoder_type, name, ...) \
	drm_encoder_init(dev, encoder, funcs, encoder_type)

#define drm_universal_plane_init(dev, plane, possible_crtcs, funcs, formats, format_count, format_modifiers, type, name, ...) \
	({ (void) format_modifiers; drm_universal_plane_init(dev, plane, possible_crtcs, funcs, formats, format_count, type); })

#define drm_crtc_init_with_planes(dev, crtc, primary, cursor, funcs, name, ...) \
	drm_crtc_init_with_planes(dev, crtc, primary, cursor, funcs)

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))

#define drm_universal_plane_init(dev, plane, possible_crtcs, funcs, formats, format_count, format_modifiers, type, name, ...) \
	({ (void) format_modifiers; drm_universal_plane_init(dev, plane, possible_crtcs, funcs, formats, format_count, type, name, ##__VA_ARGS__); })

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0))

/*
 * Linux 4.6 removed the first two parameters, the "struct task_struct" type
 * pointer "current" is defined in asm/current.h, which makes it pointless
 * to pass it on every function call.
*/
#define get_user_pages(start, nr_pages, gup_flags, pages, vmas) \
	get_user_pages(current, current->mm, start, nr_pages, gup_flags & FOLL_WRITE, gup_flags & FOLL_FORCE, pages, vmas)

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))

/* Linux 4.9 replaced the write/force parameters with "gup_flags" */
#define get_user_pages(start, nr_pages, gup_flags, pages, vmas) \
	get_user_pages(start, nr_pages, gup_flags & FOLL_WRITE, gup_flags & FOLL_FORCE, pages, vmas)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)) && \
	(!defined(CHROMIUMOS_KERNEL) || (LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)))

/*
 * Linux 4.6 removed the start and end arguments as it now always maps
 * the entire DMA-BUF.
 * Additionally, dma_buf_end_cpu_access() now returns an int error.
 */
#define dma_buf_begin_cpu_access(DMABUF, DIRECTION) dma_buf_begin_cpu_access(DMABUF, 0, DMABUF->size, DIRECTION)
#define dma_buf_end_cpu_access(DMABUF, DIRECTION) ({ dma_buf_end_cpu_access(DMABUF, 0, DMABUF->size, DIRECTION); 0; })

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)) && \
		  (!defined(CHROMIUMOS_KERNEL) || (LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0))) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0))

/* Linux 4.7 removed the first arguments as it's never been used */
#define drm_gem_object_lookup(filp, handle) drm_gem_object_lookup((filp)->minor->dev, filp, handle)

/* Linux 4.7 replaced nla_put_u64 with nla_put_u64_64bit */
#define nla_put_u64_64bit(skb, attrtype, value, padattr) nla_put_u64(skb, attrtype, value)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))

/* Linux 4.9 changed the second argument to a drm_file pointer */
#define drm_vma_node_is_allowed(node, file_priv) drm_vma_node_is_allowed(node, (file_priv)->filp)
#define drm_vma_node_allow(node, file_priv) drm_vma_node_allow(node, (file_priv)->filp)
#define drm_vma_node_revoke(node, file_priv) drm_vma_node_revoke(node, (file_priv)->filp)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
#define refcount_read(r) atomic_read(r)
#define drm_mm_insert_node(mm, node, size) drm_mm_insert_node(mm, node, size, 0, DRM_MM_SEARCH_DEFAULT)

#define drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd) drm_helper_mode_fill_fb_struct(fb, mode_cmd)

/*
 * In Linux Kernels >= 4.12 for x86 another level of page tables has been
 * added. The added level (p4d) sits between pgd and pud, so when it
 * doesn`t exist, pud_offset function takes pgd as a parameter instead
 * of p4d.
 */
#define p4d_t pgd_t
#define p4d_offset(pgd, address) (pgd)
#define p4d_none(p4d) (0)
#define p4d_bad(p4d) (0)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))

#define drm_mode_object_get(obj)          drm_mode_object_reference(obj)
#define drm_mode_object_put(obj)          drm_mode_object_unreference(obj)
#define drm_connector_get(obj)            drm_connector_reference(obj)
#define drm_connector_put(obj)            drm_connector_unreference(obj)
#define drm_framebuffer_get(obj)          drm_framebuffer_reference(obj)
#define drm_framebuffer_put(obj)          drm_framebuffer_unreference(obj)
#define drm_gem_object_get(obj)           drm_gem_object_reference(obj)
#define drm_gem_object_put_locked(obj)    drm_gem_object_unreference(obj)
#define __drm_gem_object_put(obj)         __drm_gem_object_unreference(obj)
#define drm_property_blob_get(obj)        drm_property_reference_blob(obj)
#define drm_property_blob_put(obj)        drm_property_unreference_blob(obj)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))

#define drm_dev_put(dev) drm_dev_unref(dev)

#define drm_mode_object_find(dev, file_priv, id, type) drm_mode_object_find(dev, id, type)
#define drm_encoder_find(dev, file_priv, id) drm_encoder_find(dev, id)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0))

#define drm_atomic_helper_check_plane_state(plane_state, crtc_state, \
											min_scale, max_scale, \
											can_position, can_update_disabled) \
	({ \
		const struct drm_rect __clip = { \
			.x2 = crtc_state->crtc->mode.hdisplay, \
			.y2 = crtc_state->crtc->mode.vdisplay, \
		}; \
		int __ret = drm_plane_helper_check_state(plane_state, \
												 &__clip, \
												 min_scale, max_scale, \
												 can_position, \
												 can_update_disabled); \
		__ret; \
	})

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0))

#define drm_atomic_helper_check_plane_state(plane_state, crtc_state, \
											min_scale, max_scale, \
											can_position, can_update_disabled) \
	({ \
		const struct drm_rect __clip = { \
			.x2 = crtc_state->crtc->mode.hdisplay, \
			.y2 = crtc_state->crtc->mode.vdisplay, \
		}; \
		int __ret = drm_atomic_helper_check_plane_state(plane_state, \
														crtc_state, \
														&__clip, \
														min_scale, max_scale, \
														can_position, \
														can_update_disabled); \
		__ret; \
	})

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))

#define drm_connector_attach_encoder(connector, encoder) \
	drm_mode_connector_attach_encoder(connector, encoder)

#define drm_connector_update_edid_property(connector, edid) \
	drm_mode_connector_update_edid_property(connector, edid)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)) */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0))

/*
 * Work around architectures, e.g. MIPS, that define copy_from_user and
 * copy_to_user as macros that call access_ok, as this gets redefined below.
 * As of kernel 4.12, these functions are no longer defined per-architecture
 * so this work around isn't needed.
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
#if defined(copy_from_user)
 /*
  * NOTE: This function should not be called directly as it exists simply to
  * work around copy_from_user being defined as a macro that calls access_ok.
  */
static inline int
__pvr_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return copy_from_user(to, from, n);
}

#undef copy_from_user
#define copy_from_user(to, from, n) __copy_from_user(to, from, n)
#endif

#if defined(copy_to_user)
 /*
  * NOTE: This function should not be called directly as it exists simply to
  * work around copy_to_user being defined as a macro that calls access_ok.
  */
static inline int
__pvr_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return copy_to_user(to, from, n);
}

#undef copy_to_user
#define copy_to_user(to, from, n) __copy_to_user(to, from, n)
#endif
#endif

/*
 * Linux 5.0 dropped the type argument.
 *
 * This is unused in at least Linux 3.4 and above for all architectures other
 * than 'um' (User Mode Linux), which stopped using it in 4.2.
 */
#if defined(access_ok)
 /*
  * NOTE: This function should not be called directly as it exists simply to
  * work around access_ok being defined as a macro.
  */
static inline int
__pvr_access_ok_compat(int type, const void __user * addr, unsigned long size)
{
	return access_ok(type, addr, size);
}

#undef access_ok
#define access_ok(addr, size) __pvr_access_ok_compat(0, addr, size)
#else
#define access_ok(addr, size) access_ok(0, addr, size)
#endif

#endif

/*
 * Before v5.8, the "struct mm" has a semaphore named "mmap_sem" which is
 * renamed to "mmap_lock" in v5.8. Moreover, new APIs are provided to
 * access this lock starting from v5.8.
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))

#define mmap_write_lock(mm)   down_write(&mm->mmap_sem)
#define mmap_write_unlock(mm) up_write(&mm->mmap_sem)

#define mmap_read_lock(mm)    down_read(&mm->mmap_sem)
#define mmap_read_unlock(mm)  up_read(&mm->mmap_sem)

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
#define drm_gem_object_put(obj) drm_gem_object_unreference_unlocked(obj)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
#define drm_gem_object_put(obj) drm_gem_object_put_unlocked(obj)
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))

#define drm_prime_pages_to_sg(dev, pages, nr_pages) \
	drm_prime_pages_to_sg(pages, nr_pages)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0))

struct dma_buf_map {
	void *vaddr;
};

#define dma_buf_vmap(dmabuf, map) \
	({ \
		(map)->vaddr = dma_buf_vmap(dmabuf); \
		(map)->vaddr ? 0 : ((dmabuf) && (dmabuf)->ops->vmap) ? -ENOMEM : -EINVAL; \
	})

#define dma_buf_vunmap(dmabuf, map) \
	({ \
		dma_buf_vunmap(dmabuf, (map)->vaddr); \
		(map)->vaddr = NULL; \
	})

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0) */

#endif /* __KERNEL_COMPATIBILITY_H__ */
