/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 */

#ifndef _VMCI_DRIVER_H_
#define _VMCI_DRIVER_H_

#include <linux/vmw_vmci_defs.h>
#include <linux/wait.h>

#include "vmci_queue_pair.h"
#include "vmci_context.h"

enum vmci_obj_type {
	VMCIOBJ_VMX_VM = 10,
	VMCIOBJ_CONTEXT,
	VMCIOBJ_SOCKET,
	VMCIOBJ_NOT_SET,
};

/* For storing VMCI structures in file handles. */
struct vmci_obj {
	void *ptr;
	enum vmci_obj_type type;
};

/*
 * Needed by other components of this module.  It's okay to have one global
 * instance of this because there can only ever be one VMCI device.  Our
 * virtual hardware enforces this.
 */
extern struct pci_dev *vmci_pdev;

u32 vmci_get_context_id(void);
int vmci_send_datagram(struct vmci_datagram *dg);

int vmci_host_init(void);
void vmci_host_exit(void);
bool vmci_host_code_active(void);

int vmci_guest_init(void);
void vmci_guest_exit(void);
bool vmci_guest_code_active(void);
u32 vmci_get_vm_context_id(void);

bool vmci_use_ppn64(void);

#endif /* _VMCI_DRIVER_H_ */
