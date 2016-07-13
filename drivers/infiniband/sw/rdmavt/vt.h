#ifndef DEF_RDMAVT_H
#define DEF_RDMAVT_H

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

#include <rdma/rdma_vt.h>
#include <linux/pci.h>
#include "dma.h"
#include "pd.h"
#include "qp.h"
#include "ah.h"
#include "mr.h"
#include "srq.h"
#include "mcast.h"
#include "mmap.h"
#include "cq.h"
#include "mad.h"
#include "mmap.h"

#define rvt_pr_info(rdi, fmt, ...) \
	__rvt_pr_info(rdi->driver_f.get_pci_dev(rdi), \
		      rdi->driver_f.get_card_name(rdi), \
		      fmt, \
		      ##__VA_ARGS__)

#define rvt_pr_warn(rdi, fmt, ...) \
	__rvt_pr_warn(rdi->driver_f.get_pci_dev(rdi), \
		      rdi->driver_f.get_card_name(rdi), \
		      fmt, \
		      ##__VA_ARGS__)

#define rvt_pr_err(rdi, fmt, ...) \
	__rvt_pr_err(rdi->driver_f.get_pci_dev(rdi), \
		     rdi->driver_f.get_card_name(rdi), \
		     fmt, \
		     ##__VA_ARGS__)

#define __rvt_pr_info(pdev, name, fmt, ...) \
	dev_info(&pdev->dev, "%s: " fmt, name, ##__VA_ARGS__)

#define __rvt_pr_warn(pdev, name, fmt, ...) \
	dev_warn(&pdev->dev, "%s: " fmt, name, ##__VA_ARGS__)

#define __rvt_pr_err(pdev, name, fmt, ...) \
	dev_err(&pdev->dev, "%s: " fmt, name, ##__VA_ARGS__)

static inline int ibport_num_to_idx(struct ib_device *ibdev, u8 port_num)
{
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	int port_index;

	port_index = port_num - 1; /* IB ports start at 1 our arrays at 0 */
	if ((port_index < 0) || (port_index >= rdi->dparms.nports))
		return -EINVAL;

	return port_index;
}

#endif          /* DEF_RDMAVT_H */
