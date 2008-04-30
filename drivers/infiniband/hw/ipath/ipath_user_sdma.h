/*
 * Copyright (c) 2007, 2008 QLogic Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/device.h>

struct ipath_user_sdma_queue;

struct ipath_user_sdma_queue *
ipath_user_sdma_queue_create(struct device *dev, int unit, int port, int sport);
void ipath_user_sdma_queue_destroy(struct ipath_user_sdma_queue *pq);

int ipath_user_sdma_writev(struct ipath_devdata *dd,
			   struct ipath_user_sdma_queue *pq,
			   const struct iovec *iov,
			   unsigned long dim);

int ipath_user_sdma_make_progress(struct ipath_devdata *dd,
				  struct ipath_user_sdma_queue *pq);

int ipath_user_sdma_pkt_sent(const struct ipath_user_sdma_queue *pq,
			     u32 counter);
void ipath_user_sdma_queue_drain(struct ipath_devdata *dd,
				 struct ipath_user_sdma_queue *pq);

u32 ipath_user_sdma_complete_counter(const struct ipath_user_sdma_queue *pq);
u32 ipath_user_sdma_inflight_counter(struct ipath_user_sdma_queue *pq);
