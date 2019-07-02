/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 */

#ifndef _VMCI_ROUTE_H_
#define _VMCI_ROUTE_H_

#include <linux/vmw_vmci_defs.h>

enum vmci_route {
	VMCI_ROUTE_NONE,
	VMCI_ROUTE_AS_HOST,
	VMCI_ROUTE_AS_GUEST,
};

int vmci_route(struct vmci_handle *src, const struct vmci_handle *dst,
	       bool from_guest, enum vmci_route *route);

#endif /* _VMCI_ROUTE_H_ */
