/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_DMA_BUF_H__
#define __MALI_DMA_BUF_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "mali_osk.h"

int mali_attach_dma_buf(struct mali_session_data *session, _mali_uk_attach_dma_buf_s __user *arg);
int mali_release_dma_buf(struct mali_session_data *session, _mali_uk_release_dma_buf_s __user *arg);
int mali_dma_buf_get_size(struct mali_session_data *session, _mali_uk_dma_buf_get_size_s __user *arg);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_KERNEL_LINUX_H__ */
