/*
 * @File
 * @Title       PowerVR Linux fence compatibility header
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#if !defined(__PVR_LINUX_FENCE_H__)
#define __PVR_LINUX_FENCE_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)) && \
	!defined(CHROMIUMOS_KERNEL_HAS_DMA_FENCE)
#include <linux/fence.h>
#else
#include <linux/dma-fence.h>
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)) && \
	!defined(CHROMIUMOS_KERNEL_HAS_DMA_FENCE)
/* Structures */
#define	dma_fence fence
#define dma_fence_array fence_array
#define	dma_fence_cb fence_cb
#define	dma_fence_ops fence_ops

/* Defines and Enums */
#define DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT FENCE_FLAG_ENABLE_SIGNAL_BIT
#define DMA_FENCE_FLAG_SIGNALED_BIT FENCE_FLAG_SIGNALED_BIT
#define DMA_FENCE_FLAG_USER_BITS FENCE_FLAG_USER_BITS

#define DMA_FENCE_ERR FENCE_ERR
#define	DMA_FENCE_TRACE FENCE_TRACE
#define DMA_FENCE_WARN FENCE_WARN

/* Functions */
#define dma_fence_add_callback fence_add_callback
#define dma_fence_context_alloc fence_context_alloc
#define dma_fence_default_wait fence_default_wait
#define dma_fence_is_signaled fence_is_signaled
#define dma_fence_enable_sw_signaling fence_enable_sw_signaling
#define dma_fence_free fence_free
#define dma_fence_get fence_get
#define dma_fence_get_rcu fence_get_rcu
#define dma_fence_init fence_init
#define dma_fence_is_array fence_is_array
#define dma_fence_put fence_put
#define dma_fence_signal fence_signal
#define dma_fence_wait fence_wait
#define to_dma_fence_array to_fence_array

static inline signed long
dma_fence_wait_timeout(struct dma_fence *fence, bool intr, signed long timeout)
{
	signed long lret;

	lret = fence_wait_timeout(fence, intr, timeout);
	if (lret || timeout)
		return lret;

	return test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) ? 1 : 0;
}

#endif

#endif /* !defined(__PVR_LINUX_FENCE_H__) */
