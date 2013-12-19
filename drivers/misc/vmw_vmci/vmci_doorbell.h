/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#ifndef VMCI_DOORBELL_H
#define VMCI_DOORBELL_H

#include <linux/vmw_vmci_defs.h>
#include <linux/types.h>

#include "vmci_driver.h"

/*
 * VMCINotifyResourceInfo: Used to create and destroy doorbells, and
 * generate a notification for a doorbell or queue pair.
 */
struct vmci_dbell_notify_resource_info {
	struct vmci_handle handle;
	u16 resource;
	u16 action;
	s32 result;
};

/*
 * Structure used for checkpointing the doorbell mappings. It is
 * written to the checkpoint as is, so changing this structure will
 * break checkpoint compatibility.
 */
struct dbell_cpt_state {
	struct vmci_handle handle;
	u64 bitmap_idx;
};

int vmci_dbell_host_context_notify(u32 src_cid, struct vmci_handle handle);
int vmci_dbell_get_priv_flags(struct vmci_handle handle, u32 *priv_flags);

bool vmci_dbell_register_notification_bitmap(u32 bitmap_ppn);
void vmci_dbell_scan_notification_entries(u8 *bitmap);

#endif /* VMCI_DOORBELL_H */
