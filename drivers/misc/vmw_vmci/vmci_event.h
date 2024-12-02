/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 */

#ifndef __VMCI_EVENT_H__
#define __VMCI_EVENT_H__

#include <linux/vmw_vmci_api.h>

int vmci_event_init(void);
void vmci_event_exit(void);
int vmci_event_dispatch(struct vmci_datagram *msg);

#endif /*__VMCI_EVENT_H__ */
