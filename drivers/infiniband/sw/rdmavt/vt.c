/*
 * Copyright(c) 2015 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include "vt.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("RDMA Verbs Transport Library");

static int rvt_init(void)
{
	/* Do any work needed prior to drivers calling for registration*/
	return 0;
}
module_init(rvt_init);

static void rvt_cleanup(void)
{
}
module_exit(rvt_cleanup);

int rvt_register_device(struct rvt_dev_info *rdi)
{
	if (!rdi)
		return -EINVAL;

	/*
	 * Drivers have the option to override anything in the ibdev that they
	 * want to specifically handle. VT needs to check for things it supports
	 * and if the driver wants to handle that functionality let it. We may
	 * come up with a better mechanism that simplifies the code at some
	 * point.
	 */

	/* DMA Operations */
	rdi->ibdev.dma_ops =
		rdi->ibdev.dma_ops ? : &rvt_default_dma_mapping_ops;

	/* Protection Domain */
	rdi->ibdev.alloc_pd =
		rdi->ibdev.alloc_pd ? : rvt_alloc_pd;
	rdi->ibdev.dealloc_pd =
		rdi->ibdev.dealloc_pd ? : rvt_dealloc_pd;

	spin_lock_init(&rdi->n_pds_lock);
	rdi->n_pds_allocated = 0;

	/* We are now good to announce we exist */
	return ib_register_device(&rdi->ibdev, rdi->port_callback);
}
EXPORT_SYMBOL(rvt_register_device);

void rvt_unregister_device(struct rvt_dev_info *rdi)
{
	if (!rdi)
		return;

	ib_unregister_device(&rdi->ibdev);
}
EXPORT_SYMBOL(rvt_unregister_device);
