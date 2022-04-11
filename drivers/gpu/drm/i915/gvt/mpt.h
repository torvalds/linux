/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eddie Dong <eddie.dong@intel.com>
 *    Dexuan Cui
 *    Jike Song <jike.song@intel.com>
 *
 * Contributors:
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#ifndef _GVT_MPT_H_
#define _GVT_MPT_H_

#include "gvt.h"

/**
 * DOC: Hypervisor Service APIs for GVT-g Core Logic
 *
 * This is the glue layer between specific hypervisor MPT modules and GVT-g core
 * logic. Each kind of hypervisor MPT module provides a collection of function
 * callbacks and will be attached to GVT host when the driver is loading.
 * GVT-g core logic will call these APIs to request specific services from
 * hypervisor.
 */

/**
 * intel_gvt_hypervisor_host_init - init GVT-g host side
 *
 * Returns:
 * Zero on success, negative error code if failed
 */
static inline int intel_gvt_hypervisor_host_init(struct device *dev, void *gvt)
{
	if (!intel_gvt_host.mpt->host_init)
		return -ENODEV;

	return intel_gvt_host.mpt->host_init(dev, gvt);
}

/**
 * intel_gvt_hypervisor_host_exit - exit GVT-g host side
 */
static inline void intel_gvt_hypervisor_host_exit(struct device *dev, void *gvt)
{
	/* optional to provide */
	if (!intel_gvt_host.mpt->host_exit)
		return;

	intel_gvt_host.mpt->host_exit(dev, gvt);
}

#endif /* _GVT_MPT_H_ */
