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

#ifndef USNIC_FWD_H_
#define USNIC_FWD_H_

#include <linux/if.h>
#include <linux/pci.h>
#include <linux/spinlock.h>

#include "usnic_abi.h"
#include "vnic_devcmd.h"

struct usnic_fwd_dev {
	struct pci_dev			*pdev;
	struct net_device		*netdev;
	spinlock_t			lock;
};

struct usnic_fwd_filter {
	enum usnic_transport_type	transport;
	u16				port_num;
};

struct usnic_fwd_filter_hndl {
	enum filter_type		type;
	u32				id;
	u32				vnic_idx;
	struct usnic_fwd_dev		*ufdev;
	struct list_head		link;
	struct usnic_fwd_filter		*filter;
};

struct usnic_fwd_dev *usnic_fwd_dev_alloc(struct pci_dev *pdev);
void usnic_fwd_dev_free(struct usnic_fwd_dev *ufdev);
int usnic_fwd_add_usnic_filter(struct usnic_fwd_dev *ufdev, int vnic_idx,
				int rq_idx, struct usnic_fwd_filter *filter,
				struct usnic_fwd_filter_hndl **filter_hndl);
int usnic_fwd_del_filter(struct usnic_fwd_filter_hndl *filter_hndl);
int usnic_fwd_enable_rq(struct usnic_fwd_dev *ufdev, int vnic_idx, int rq_idx);
int usnic_fwd_disable_rq(struct usnic_fwd_dev *ufdev, int vnic_idx, int rq_idx);

#endif /* !USNIC_FWD_H_ */
