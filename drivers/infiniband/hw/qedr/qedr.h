/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#ifndef __QEDR_H__
#define __QEDR_H__

#include <linux/pci.h>
#include <rdma/ib_addr.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qede_roce.h>

#define QEDR_MODULE_VERSION	"8.10.10.0"
#define QEDR_NODE_DESC "QLogic 579xx RoCE HCA"
#define DP_NAME(dev) ((dev)->ibdev.name)

#define DP_DEBUG(dev, module, fmt, ...)					\
	pr_debug("(%s) " module ": " fmt,				\
		 DP_NAME(dev) ? DP_NAME(dev) : "", ## __VA_ARGS__)

#define QEDR_MSG_INIT "INIT"

struct qedr_dev {
	struct ib_device	ibdev;
	struct qed_dev		*cdev;
	struct pci_dev		*pdev;
	struct net_device	*ndev;

	enum ib_atomic_cap	atomic_cap;

	u32			dp_module;
	u8			dp_level;
};
#endif
