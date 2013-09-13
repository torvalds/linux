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

#ifndef __VMCI_EVENT_H__
#define __VMCI_EVENT_H__

#include <linux/vmw_vmci_api.h>

int vmci_event_init(void);
void vmci_event_exit(void);
int vmci_event_dispatch(struct vmci_datagram *msg);

#endif /*__VMCI_EVENT_H__ */
