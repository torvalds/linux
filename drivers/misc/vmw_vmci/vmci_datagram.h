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

#ifndef _VMCI_DATAGRAM_H_
#define _VMCI_DATAGRAM_H_

#include <linux/types.h>
#include <linux/list.h>

#include "vmci_context.h"

#define VMCI_MAX_DELAYED_DG_HOST_QUEUE_SIZE 256

/*
 * The struct vmci_datagram_queue_entry is a queue header for the in-kernel VMCI
 * datagram queues. It is allocated in non-paged memory, as the
 * content is accessed while holding a spinlock. The pending datagram
 * itself may be allocated from paged memory. We shadow the size of
 * the datagram in the non-paged queue entry as this size is used
 * while holding the same spinlock as above.
 */
struct vmci_datagram_queue_entry {
	struct list_head list_item;	/* For queuing. */
	size_t dg_size;		/* Size of datagram. */
	struct vmci_datagram *dg;	/* Pending datagram. */
};

/* VMCIDatagramSendRecvInfo */
struct vmci_datagram_snd_rcv_info {
	u64 addr;
	u32 len;
	s32 result;
};

/* Datagram API for non-public use. */
int vmci_datagram_dispatch(u32 context_id, struct vmci_datagram *dg,
			   bool from_guest);
int vmci_datagram_invoke_guest_handler(struct vmci_datagram *dg);

#endif /* _VMCI_DATAGRAM_H_ */
