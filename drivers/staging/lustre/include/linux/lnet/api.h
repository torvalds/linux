// SPDX-License-Identifier: GPL-2.0
/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011 - 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Seagate, Inc.
 */

#ifndef __LNET_API_H__
#define __LNET_API_H__

/** \defgroup lnet LNet
 *
 * The Lustre Networking subsystem.
 *
 * LNet is an asynchronous message-passing API, which provides an unreliable
 * connectionless service that can't guarantee any order. It supports OFA IB,
 * TCP/IP, and Cray Interconnects, and routes between heterogeneous networks.
 *
 * @{
 */

#include <uapi/linux/lnet/lnet-types.h>

/** \defgroup lnet_init_fini Initialization and cleanup
 * The LNet must be properly initialized before any LNet calls can be made.
 * @{
 */
int LNetNIInit(lnet_pid_t requested_pid);
int LNetNIFini(void);
/** @} lnet_init_fini */

/** \defgroup lnet_addr LNet addressing and basic types
 *
 * Addressing scheme and basic data types of LNet.
 *
 * The LNet API is memory-oriented, so LNet must be able to address not only
 * end-points but also memory region within a process address space.
 * An ::lnet_nid_t addresses an end-point. An ::lnet_pid_t identifies a process
 * in a node. A portal represents an opening in the address space of a
 * process. Match bits is criteria to identify a region of memory inside a
 * portal, and offset specifies an offset within the memory region.
 *
 * LNet creates a table of portals for each process during initialization.
 * This table has MAX_PORTALS entries and its size can't be dynamically
 * changed. A portal stays empty until the owning process starts to add
 * memory regions to it. A portal is sometimes called an index because
 * it's an entry in the portals table of a process.
 *
 * \see LNetMEAttach
 * @{
 */
int LNetGetId(unsigned int index, struct lnet_process_id *id);
int LNetDist(lnet_nid_t nid, lnet_nid_t *srcnid, __u32 *order);

/** @} lnet_addr */

/** \defgroup lnet_me Match entries
 *
 * A match entry (abbreviated as ME) describes a set of criteria to accept
 * incoming requests.
 *
 * A portal is essentially a match list plus a set of attributes. A match
 * list is a chain of MEs. Each ME includes a pointer to a memory descriptor
 * and a set of match criteria. The match criteria can be used to reject
 * incoming requests based on process ID or the match bits provided in the
 * request. MEs can be dynamically inserted into a match list by LNetMEAttach()
 * and LNetMEInsert(), and removed from its list by LNetMEUnlink().
 * @{
 */
int LNetMEAttach(unsigned int      portal,
		 struct lnet_process_id match_id_in,
		 __u64		   match_bits_in,
		 __u64		   ignore_bits_in,
		 enum lnet_unlink unlink_in,
		 enum lnet_ins_pos pos_in,
		 struct lnet_handle_me *handle_out);

int LNetMEInsert(struct lnet_handle_me current_in,
		 struct lnet_process_id match_id_in,
		 __u64		   match_bits_in,
		 __u64		   ignore_bits_in,
		 enum lnet_unlink unlink_in,
		 enum lnet_ins_pos position_in,
		 struct lnet_handle_me *handle_out);

int LNetMEUnlink(struct lnet_handle_me current_in);
/** @} lnet_me */

/** \defgroup lnet_md Memory descriptors
 *
 * A memory descriptor contains information about a region of a user's
 * memory (either in kernel or user space) and optionally points to an
 * event queue where information about the operations performed on the
 * memory descriptor are recorded. Memory descriptor is abbreviated as
 * MD and can be used interchangeably with the memory region it describes.
 *
 * The LNet API provides two operations to create MDs: LNetMDAttach()
 * and LNetMDBind(); one operation to unlink and release the resources
 * associated with a MD: LNetMDUnlink().
 * @{
 */
int LNetMDAttach(struct lnet_handle_me current_in,
		 struct lnet_md md_in,
		 enum lnet_unlink unlink_in,
		 struct lnet_handle_md *md_handle_out);

int LNetMDBind(struct lnet_md md_in,
	       enum lnet_unlink unlink_in,
	       struct lnet_handle_md *md_handle_out);

int LNetMDUnlink(struct lnet_handle_md md_in);
/** @} lnet_md */

/** \defgroup lnet_eq Events and event queues
 *
 * Event queues (abbreviated as EQ) are used to log operations performed on
 * local MDs. In particular, they signal the completion of a data transmission
 * into or out of a MD. They can also be used to hold acknowledgments for
 * completed PUT operations and indicate when a MD has been unlinked. Multiple
 * MDs can share a single EQ. An EQ may have an optional event handler
 * associated with it. If an event handler exists, it will be run for each
 * event that is deposited into the EQ.
 *
 * In addition to the lnet_handle_eq, the LNet API defines two types
 * associated with events: The ::lnet_event_kind defines the kinds of events
 * that can be stored in an EQ. The lnet_event defines a structure that
 * holds the information about with an event.
 *
 * There are five functions for dealing with EQs: LNetEQAlloc() is used to
 * create an EQ and allocate the resources needed, while LNetEQFree()
 * releases these resources and free the EQ. LNetEQGet() retrieves the next
 * event from an EQ, and LNetEQWait() can be used to block a process until
 * an EQ has at least one event. LNetEQPoll() can be used to test or wait
 * on multiple EQs.
 * @{
 */
int LNetEQAlloc(unsigned int       count_in,
		lnet_eq_handler_t  handler,
		struct lnet_handle_eq *handle_out);

int LNetEQFree(struct lnet_handle_eq eventq_in);

int LNetEQPoll(struct lnet_handle_eq *eventqs_in,
	       int		 neq_in,
	       int		 timeout_ms,
	       struct lnet_event *event_out,
	       int		*which_eq_out);
/** @} lnet_eq */

/** \defgroup lnet_data Data movement operations
 *
 * The LNet API provides two data movement operations: LNetPut()
 * and LNetGet().
 * @{
 */
int LNetPut(lnet_nid_t	      self,
	    struct lnet_handle_md md_in,
	    enum lnet_ack_req ack_req_in,
	    struct lnet_process_id target_in,
	    unsigned int      portal_in,
	    __u64	      match_bits_in,
	    unsigned int      offset_in,
	    __u64	      hdr_data_in);

int LNetGet(lnet_nid_t	      self,
	    struct lnet_handle_md md_in,
	    struct lnet_process_id target_in,
	    unsigned int      portal_in,
	    __u64	      match_bits_in,
	    unsigned int      offset_in);
/** @} lnet_data */

/** \defgroup lnet_misc Miscellaneous operations.
 * Miscellaneous operations.
 * @{
 */
int LNetSetLazyPortal(int portal);
int LNetClearLazyPortal(int portal);
int LNetCtl(unsigned int cmd, void *arg);
void LNetDebugPeer(struct lnet_process_id id);

/** @} lnet_misc */

/** @} lnet */
#endif
