/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2012, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _DMA_BUF_LOCK_H
#define _DMA_BUF_LOCK_H

typedef enum dma_buf_lock_exclusive
{
	DMA_BUF_LOCK_NONEXCLUSIVE = 0,
	DMA_BUF_LOCK_EXCLUSIVE = -1
} dma_buf_lock_exclusive;

typedef struct dma_buf_lock_k_request
{
	int count;
	int *list_of_dma_buf_fds;
	int timeout;
	dma_buf_lock_exclusive exclusive;
} dma_buf_lock_k_request;

#define DMA_BUF_LOCK_IOC_MAGIC '~'

#define DMA_BUF_LOCK_FUNC_LOCK_ASYNC       _IOW(DMA_BUF_LOCK_IOC_MAGIC, 11, dma_buf_lock_k_request)

#define DMA_BUF_LOCK_IOC_MINNR 11
#define DMA_BUF_LOCK_IOC_MAXNR 11

#endif /* _DMA_BUF_LOCK_H */
