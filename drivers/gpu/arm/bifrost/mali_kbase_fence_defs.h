/*
 *
 * (C) COPYRIGHT 2010-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifndef _KBASE_FENCE_DEFS_H_
#define _KBASE_FENCE_DEFS_H_

/*
 * There was a big rename in the 4.10 kernel (fence* -> dma_fence*)
 * This file hides the compatibility issues with this for the rest the driver
 */

#if defined(CONFIG_MALI_BIFROST_DMA_FENCE) || defined(CONFIG_SYNC_FILE)

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))

#include <linux/fence.h>

#define dma_fence_context_alloc(a) fence_context_alloc(a)
#define dma_fence_init(a, b, c, d, e) fence_init(a, b, c, d, e)
#define dma_fence_get(a) fence_get(a)
#define dma_fence_put(a) fence_put(a)
#define dma_fence_signal(a) fence_signal(a)
#define dma_fence_is_signaled(a) fence_is_signaled(a)
#define dma_fence_add_callback(a, b, c) fence_add_callback(a, b, c)
#define dma_fence_remove_callback(a, b) fence_remove_callback(a, b)

#else

#include <linux/dma-fence.h>

#endif /* < 4.10.0 */

#endif /* CONFIG_MALI_BIFROST_DMA_FENCE || CONFIG_SYNC_FILE */

#endif /* _KBASE_FENCE_DEFS_H_ */
