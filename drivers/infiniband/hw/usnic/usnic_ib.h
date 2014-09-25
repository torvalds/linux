/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef USNIC_IB_H_
#define USNIC_IB_H_

#include <linux/iommu.h>
#include <linux/netdevice.h>

#include <rdma/ib_verbs.h>


#include "usnic.h"
#include "usnic_abi.h"
#include "usnic_vnic.h"

#define USNIC_IB_PORT_CNT		1
#define USNIC_IB_NUM_COMP_VECTORS	1

extern unsigned int usnic_ib_share_vf;

struct usnic_ib_ucontext {
	struct ib_ucontext		ibucontext;
	/* Protected by usnic_ib_dev->usdev_lock */
	struct list_head		qp_grp_list;
	struct list_head		link;
};

struct usnic_ib_pd {
	struct ib_pd			ibpd;
	struct usnic_uiom_pd		*umem_pd;
};

struct usnic_ib_mr {
	struct ib_mr			ibmr;
	struct usnic_uiom_reg		*umem;
};

struct usnic_ib_dev {
	struct ib_device		ib_dev;
	struct pci_dev			*pdev;
	struct net_device		*netdev;
	struct usnic_fwd_dev		*ufdev;
	struct list_head		ib_dev_link;
	struct list_head		vf_dev_list;
	struct list_head		ctx_list;
	struct mutex			usdev_lock;

	/* provisioning information */
	struct kref			vf_cnt;
	unsigned int			vf_res_cnt[USNIC_VNIC_RES_TYPE_MAX];

	/* sysfs vars for QPN reporting */
	struct kobject *qpn_kobj;
};

struct usnic_ib_vf {
	struct usnic_ib_dev		*pf;
	spinlock_t			lock;
	struct usnic_vnic		*vnic;
	unsigned int			qp_grp_ref_cnt;
	struct usnic_ib_pd		*pd;
	struct list_head		link;
};

static inline
struct usnic_ib_dev *to_usdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct usnic_ib_dev, ib_dev);
}

static inline
struct usnic_ib_ucontext *to_ucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct usnic_ib_ucontext, ibucontext);
}

static inline
struct usnic_ib_pd *to_upd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct usnic_ib_pd, ibpd);
}

static inline
struct usnic_ib_ucontext *to_uucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct usnic_ib_ucontext, ibucontext);
}

static inline
struct usnic_ib_mr *to_umr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct usnic_ib_mr, ibmr);
}
void usnic_ib_log_vf(struct usnic_ib_vf *vf);

#define UPDATE_PTR_LEFT(N, P, L)			\
do {							\
	L -= (N);					\
	P += (N);					\
} while (0)

#endif /* USNIC_IB_H_ */
