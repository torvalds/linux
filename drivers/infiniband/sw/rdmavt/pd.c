/*
 * Copyright(c) 2016 Intel Corporation.
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

#include <linux/slab.h>
#include "pd.h"

/**
 * rvt_alloc_pd - allocate a protection domain
 * @ibdev: ib device
 * @context: optional user context
 * @udata: optional user data
 *
 * Allocate and keep track of a PD.
 *
 * Return: 0 on success
 */
struct ib_pd *rvt_alloc_pd(struct ib_device *ibdev,
			   struct ib_ucontext *context,
			   struct ib_udata *udata)
{
	struct rvt_dev_info *dev = ib_to_rvt(ibdev);
	struct rvt_pd *pd;
	struct ib_pd *ret;

	pd = kmalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}
	/*
	 * While we could continue allocating protecetion domains, being
	 * constrained only by system resources. The IBTA spec defines that
	 * there is a max_pd limit that can be set and we need to check for
	 * that.
	 */

	spin_lock(&dev->n_pds_lock);
	if (dev->n_pds_allocated == dev->dparms.props.max_pd) {
		spin_unlock(&dev->n_pds_lock);
		kfree(pd);
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	dev->n_pds_allocated++;
	spin_unlock(&dev->n_pds_lock);

	/* ib_alloc_pd() will initialize pd->ibpd. */
	pd->user = !!udata;

	ret = &pd->ibpd;

bail:
	return ret;
}

/**
 * rvt_dealloc_pd - Free PD
 * @ibpd: Free up PD
 *
 * Return: always 0
 */
int rvt_dealloc_pd(struct ib_pd *ibpd)
{
	struct rvt_pd *pd = ibpd_to_rvtpd(ibpd);
	struct rvt_dev_info *dev = ib_to_rvt(ibpd->device);

	spin_lock(&dev->n_pds_lock);
	dev->n_pds_allocated--;
	spin_unlock(&dev->n_pds_lock);

	kfree(pd);

	return 0;
}
