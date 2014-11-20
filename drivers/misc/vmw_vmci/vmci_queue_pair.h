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

#ifndef _VMCI_QUEUE_PAIR_H_
#define _VMCI_QUEUE_PAIR_H_

#include <linux/vmw_vmci_defs.h>
#include <linux/types.h>

#include "vmci_context.h"

/* Callback needed for correctly waiting on events. */
typedef int (*vmci_event_release_cb) (void *client_data);

/* Guest device port I/O. */
struct ppn_set {
	u64 num_produce_pages;
	u64 num_consume_pages;
	u32 *produce_ppns;
	u32 *consume_ppns;
	bool initialized;
};

/* VMCIqueue_pairAllocInfo */
struct vmci_qp_alloc_info {
	struct vmci_handle handle;
	u32 peer;
	u32 flags;
	u64 produce_size;
	u64 consume_size;
	u64 ppn_va;	/* Start VA of queue pair PPNs. */
	u64 num_ppns;
	s32 result;
	u32 version;
};

/* VMCIqueue_pairSetVAInfo */
struct vmci_qp_set_va_info {
	struct vmci_handle handle;
	u64 va;		/* Start VA of queue pair PPNs. */
	u64 num_ppns;
	u32 version;
	s32 result;
};

/*
 * For backwards compatibility, here is a version of the
 * VMCIqueue_pairPageFileInfo before host support end-points was added.
 * Note that the current version of that structure requires VMX to
 * pass down the VA of the mapped file.  Before host support was added
 * there was nothing of the sort.  So, when the driver sees the ioctl
 * with a parameter that is the sizeof
 * VMCIqueue_pairPageFileInfo_NoHostQP then it can infer that the version
 * of VMX running can't attach to host end points because it doesn't
 * provide the VA of the mapped files.
 *
 * The Linux driver doesn't get an indication of the size of the
 * structure passed down from user space.  So, to fix a long standing
 * but unfiled bug, the _pad field has been renamed to version.
 * Existing versions of VMX always initialize the PageFileInfo
 * structure so that _pad, er, version is set to 0.
 *
 * A version value of 1 indicates that the size of the structure has
 * been increased to include two UVA's: produce_uva and consume_uva.
 * These UVA's are of the mmap()'d queue contents backing files.
 *
 * In addition, if when VMX is sending down the
 * VMCIqueue_pairPageFileInfo structure it gets an error then it will
 * try again with the _NoHostQP version of the file to see if an older
 * VMCI kernel module is running.
 */

/* VMCIqueue_pairPageFileInfo */
struct vmci_qp_page_file_info {
	struct vmci_handle handle;
	u64 produce_page_file;	  /* User VA. */
	u64 consume_page_file;	  /* User VA. */
	u64 produce_page_file_size;  /* Size of the file name array. */
	u64 consume_page_file_size;  /* Size of the file name array. */
	s32 result;
	u32 version;	/* Was _pad. */
	u64 produce_va;	/* User VA of the mapped file. */
	u64 consume_va;	/* User VA of the mapped file. */
};

/* vmci queuepair detach info */
struct vmci_qp_dtch_info {
	struct vmci_handle handle;
	s32 result;
	u32 _pad;
};

/*
 * struct vmci_qp_page_store describes how the memory of a given queue pair
 * is backed. When the queue pair is between the host and a guest, the
 * page store consists of references to the guest pages. On vmkernel,
 * this is a list of PPNs, and on hosted, it is a user VA where the
 * queue pair is mapped into the VMX address space.
 */
struct vmci_qp_page_store {
	/* Reference to pages backing the queue pair. */
	u64 pages;
	/* Length of pageList/virtual addres range (in pages). */
	u32 len;
};

/*
 * This data type contains the information about a queue.
 * There are two queues (hence, queue pairs) per transaction model between a
 * pair of end points, A & B.  One queue is used by end point A to transmit
 * commands and responses to B.  The other queue is used by B to transmit
 * commands and responses.
 *
 * struct vmci_queue_kern_if is a per-OS defined Queue structure.  It contains
 * either a direct pointer to the linear address of the buffer contents or a
 * pointer to structures which help the OS locate those data pages.  See
 * vmciKernelIf.c for each platform for its definition.
 */
struct vmci_queue {
	struct vmci_queue_header *q_header;
	struct vmci_queue_header *saved_header;
	struct vmci_queue_kern_if *kernel_if;
};

/*
 * Utility function that checks whether the fields of the page
 * store contain valid values.
 * Result:
 * true if the page store is wellformed. false otherwise.
 */
static inline bool
VMCI_QP_PAGESTORE_IS_WELLFORMED(struct vmci_qp_page_store *page_store)
{
	return page_store->len >= 2;
}

void vmci_qp_broker_exit(void);
int vmci_qp_broker_alloc(struct vmci_handle handle, u32 peer,
			 u32 flags, u32 priv_flags,
			 u64 produce_size, u64 consume_size,
			 struct vmci_qp_page_store *page_store,
			 struct vmci_ctx *context);
int vmci_qp_broker_set_page_store(struct vmci_handle handle,
				  u64 produce_uva, u64 consume_uva,
				  struct vmci_ctx *context);
int vmci_qp_broker_detach(struct vmci_handle handle, struct vmci_ctx *context);

void vmci_qp_guest_endpoints_exit(void);

int vmci_qp_alloc(struct vmci_handle *handle,
		  struct vmci_queue **produce_q, u64 produce_size,
		  struct vmci_queue **consume_q, u64 consume_size,
		  u32 peer, u32 flags, u32 priv_flags,
		  bool guest_endpoint, vmci_event_release_cb wakeup_cb,
		  void *client_data);
int vmci_qp_broker_map(struct vmci_handle handle,
		       struct vmci_ctx *context, u64 guest_mem);
int vmci_qp_broker_unmap(struct vmci_handle handle,
			 struct vmci_ctx *context, u32 gid);

#endif /* _VMCI_QUEUE_PAIR_H_ */
