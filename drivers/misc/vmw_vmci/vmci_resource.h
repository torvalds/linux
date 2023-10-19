/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 */

#ifndef _VMCI_RESOURCE_H_
#define _VMCI_RESOURCE_H_

#include <linux/vmw_vmci_defs.h>
#include <linux/types.h>

#include "vmci_context.h"


enum vmci_resource_type {
	VMCI_RESOURCE_TYPE_ANY,
	VMCI_RESOURCE_TYPE_API,
	VMCI_RESOURCE_TYPE_GROUP,
	VMCI_RESOURCE_TYPE_DATAGRAM,
	VMCI_RESOURCE_TYPE_DOORBELL,
	VMCI_RESOURCE_TYPE_QPAIR_GUEST,
	VMCI_RESOURCE_TYPE_QPAIR_HOST
};

struct vmci_resource {
	struct vmci_handle handle;
	enum vmci_resource_type type;
	struct hlist_node node;
	struct kref kref;
	struct completion done;
};


int vmci_resource_add(struct vmci_resource *resource,
		      enum vmci_resource_type resource_type,
		      struct vmci_handle handle);

void vmci_resource_remove(struct vmci_resource *resource);

struct vmci_resource *
vmci_resource_by_handle(struct vmci_handle resource_handle,
			enum vmci_resource_type resource_type);

struct vmci_resource *vmci_resource_get(struct vmci_resource *resource);
int vmci_resource_put(struct vmci_resource *resource);

struct vmci_handle vmci_resource_handle(struct vmci_resource *resource);

#endif /* _VMCI_RESOURCE_H_ */
