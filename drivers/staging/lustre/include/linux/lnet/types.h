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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LNET_TYPES_H__
#define __LNET_TYPES_H__

/** \addtogroup lnet
 * @{ */

#include <linux/libcfs/libcfs.h>

/** \addtogroup lnet_addr
 * @{ */

/** Portal reserved for LNet's own use.
 * \see lustre/include/lustre/lustre_idl.h for Lustre portal assignments.
 */
#define LNET_RESERVED_PORTAL      0

/**
 * Address of an end-point in an LNet network.
 *
 * A node can have multiple end-points and hence multiple addresses.
 * An LNet network can be a simple network (e.g. tcp0) or a network of
 * LNet networks connected by LNet routers. Therefore an end-point address
 * has two parts: network ID, and address within a network.
 *
 * \see LNET_NIDNET, LNET_NIDADDR, and LNET_MKNID.
 */
typedef __u64 lnet_nid_t;
/**
 * ID of a process in a node. Shortened as PID to distinguish from
 * lnet_process_id_t, the global process ID.
 */
typedef __u32 lnet_pid_t;

/** wildcard NID that matches any end-point address */
#define LNET_NID_ANY      ((lnet_nid_t) -1)
/** wildcard PID that matches any lnet_pid_t */
#define LNET_PID_ANY      ((lnet_pid_t) -1)

#define LNET_PID_RESERVED 0xf0000000 /* reserved bits in PID */
#define LNET_PID_USERFLAG 0x80000000 /* set in userspace peers */

#define LNET_TIME_FOREVER    (-1)

/**
 * Objects maintained by the LNet are accessed through handles. Handle types
 * have names of the form lnet_handle_xx_t, where xx is one of the two letter
 * object type codes ('eq' for event queue, 'md' for memory descriptor, and
 * 'me' for match entry).
 * Each type of object is given a unique handle type to enhance type checking.
 * The type lnet_handle_any_t can be used when a generic handle is needed.
 * Every handle value can be converted into a value of type lnet_handle_any_t
 * without loss of information.
 */
typedef struct {
	__u64	 cookie;
} lnet_handle_any_t;

typedef lnet_handle_any_t lnet_handle_eq_t;
typedef lnet_handle_any_t lnet_handle_md_t;
typedef lnet_handle_any_t lnet_handle_me_t;

#define LNET_WIRE_HANDLE_COOKIE_NONE   (-1)

/**
 * Invalidate handle \a h.
 */
static inline void LNetInvalidateHandle(lnet_handle_any_t *h)
{
	h->cookie = LNET_WIRE_HANDLE_COOKIE_NONE;
}

/**
 * Compare handles \a h1 and \a h2.
 *
 * \return 1 if handles are equal, 0 if otherwise.
 */
static inline int LNetHandleIsEqual(lnet_handle_any_t h1, lnet_handle_any_t h2)
{
	return h1.cookie == h2.cookie;
}

/**
 * Check whether handle \a h is invalid.
 *
 * \return 1 if handle is invalid, 0 if valid.
 */
static inline int LNetHandleIsInvalid(lnet_handle_any_t h)
{
	return LNET_WIRE_HANDLE_COOKIE_NONE == h.cookie;
}

/**
 * Global process ID.
 */
typedef struct {
	/** node id */
	lnet_nid_t nid;
	/** process id */
	lnet_pid_t pid;
} lnet_process_id_t;
/** @} lnet_addr */

/** \addtogroup lnet_me
 * @{ */

/**
 * Specifies whether the match entry or memory descriptor should be unlinked
 * automatically (LNET_UNLINK) or not (LNET_RETAIN).
 */
typedef enum {
	LNET_RETAIN = 0,
	LNET_UNLINK
} lnet_unlink_t;

/**
 * Values of the type lnet_ins_pos_t are used to control where a new match
 * entry is inserted. The value LNET_INS_BEFORE is used to insert the new
 * entry before the current entry or before the head of the list. The value
 * LNET_INS_AFTER is used to insert the new entry after the current entry
 * or after the last item in the list.
 */
typedef enum {
	/** insert ME before current position or head of the list */
	LNET_INS_BEFORE,
	/** insert ME after current position or tail of the list */
	LNET_INS_AFTER,
	/** attach ME at tail of local CPU partition ME list */
	LNET_INS_LOCAL
} lnet_ins_pos_t;

/** @} lnet_me */

/** \addtogroup lnet_md
 * @{ */

/**
 * Defines the visible parts of a memory descriptor. Values of this type
 * are used to initialize memory descriptors.
 */
typedef struct {
	/**
	 * Specify the memory region associated with the memory descriptor.
	 * If the options field has:
	 * - LNET_MD_KIOV bit set: The start field points to the starting
	 * address of an array of lnet_kiov_t and the length field specifies
	 * the number of entries in the array. The length can't be bigger
	 * than LNET_MAX_IOV. The lnet_kiov_t is used to describe page-based
	 * fragments that are not necessarily mapped in virtual memory.
	 * - LNET_MD_IOVEC bit set: The start field points to the starting
	 * address of an array of struct iovec and the length field specifies
	 * the number of entries in the array. The length can't be bigger
	 * than LNET_MAX_IOV. The struct iovec is used to describe fragments
	 * that have virtual addresses.
	 * - Otherwise: The memory region is contiguous. The start field
	 * specifies the starting address for the memory region and the
	 * length field specifies its length.
	 *
	 * When the memory region is fragmented, all fragments but the first
	 * one must start on page boundary, and all but the last must end on
	 * page boundary.
	 */
	void	    *start;
	unsigned int     length;
	/**
	 * Specifies the maximum number of operations that can be performed
	 * on the memory descriptor. An operation is any action that could
	 * possibly generate an event. In the usual case, the threshold value
	 * is decremented for each operation on the MD. When the threshold
	 * drops to zero, the MD becomes inactive and does not respond to
	 * operations. A threshold value of LNET_MD_THRESH_INF indicates that
	 * there is no bound on the number of operations that may be applied
	 * to a MD.
	 */
	int	      threshold;
	/**
	 * Specifies the largest incoming request that the memory descriptor
	 * should respond to. When the unused portion of a MD (length -
	 * local offset) falls below this value, the MD becomes inactive and
	 * does not respond to further operations. This value is only used
	 * if the LNET_MD_MAX_SIZE option is set.
	 */
	int	      max_size;
	/**
	 * Specifies the behavior of the memory descriptor. A bitwise OR
	 * of the following values can be used:
	 * - LNET_MD_OP_PUT: The LNet PUT operation is allowed on this MD.
	 * - LNET_MD_OP_GET: The LNet GET operation is allowed on this MD.
	 * - LNET_MD_MANAGE_REMOTE: The offset used in accessing the memory
	 *   region is provided by the incoming request. By default, the
	 *   offset is maintained locally. When maintained locally, the
	 *   offset is incremented by the length of the request so that
	 *   the next operation (PUT or GET) will access the next part of
	 *   the memory region. Note that only one offset variable exists
	 *   per memory descriptor. If both PUT and GET operations are
	 *   performed on a memory descriptor, the offset is updated each time.
	 * - LNET_MD_TRUNCATE: The length provided in the incoming request can
	 *   be reduced to match the memory available in the region (determined
	 *   by subtracting the offset from the length of the memory region).
	 *   By default, if the length in the incoming operation is greater
	 *   than the amount of memory available, the operation is rejected.
	 * - LNET_MD_ACK_DISABLE: An acknowledgment should not be sent for
	 *   incoming PUT operations, even if requested. By default,
	 *   acknowledgments are sent for PUT operations that request an
	 *   acknowledgment. Acknowledgments are never sent for GET operations.
	 *   The data sent in the REPLY serves as an implicit acknowledgment.
	 * - LNET_MD_KIOV: The start and length fields specify an array of
	 *   lnet_kiov_t.
	 * - LNET_MD_IOVEC: The start and length fields specify an array of
	 *   struct iovec.
	 * - LNET_MD_MAX_SIZE: The max_size field is valid.
	 *
	 * Note:
	 * - LNET_MD_KIOV or LNET_MD_IOVEC allows for a scatter/gather
	 *   capability for memory descriptors. They can't be both set.
	 * - When LNET_MD_MAX_SIZE is set, the total length of the memory
	 *   region (i.e. sum of all fragment lengths) must not be less than
	 *   \a max_size.
	 */
	unsigned int     options;
	/**
	 * A user-specified value that is associated with the memory
	 * descriptor. The value does not need to be a pointer, but must fit
	 * in the space used by a pointer. This value is recorded in events
	 * associated with operations on this MD.
	 */
	void	    *user_ptr;
	/**
	 * A handle for the event queue used to log the operations performed on
	 * the memory region. If this argument is a NULL handle (i.e. nullified
	 * by LNetInvalidateHandle()), operations performed on this memory
	 * descriptor are not logged.
	 */
	lnet_handle_eq_t eq_handle;
} lnet_md_t;

/* Max Transfer Unit (minimum supported everywhere).
 * CAVEAT EMPTOR, with multinet (i.e. routers forwarding between networks)
 * these limits are system wide and not interface-local. */
#define LNET_MTU_BITS	20
#define LNET_MTU	(1 << LNET_MTU_BITS)

/** limit on the number of fragments in discontiguous MDs */
#define LNET_MAX_IOV    256

/* Max payload size */
# define LNET_MAX_PAYLOAD	CONFIG_LNET_MAX_PAYLOAD
# if (LNET_MAX_PAYLOAD < LNET_MTU)
#  error "LNET_MAX_PAYLOAD too small - error in configure --with-max-payload-mb"
# else
#  if (LNET_MAX_PAYLOAD > (PAGE_SIZE * LNET_MAX_IOV))
/*  PAGE_SIZE is a constant: check with cpp! */
#   error "LNET_MAX_PAYLOAD too large - error in configure --with-max-payload-mb"
#  endif
# endif

/**
 * Options for the MD structure. See lnet_md_t::options.
 */
#define LNET_MD_OP_PUT	       (1 << 0)
/** See lnet_md_t::options. */
#define LNET_MD_OP_GET	       (1 << 1)
/** See lnet_md_t::options. */
#define LNET_MD_MANAGE_REMOTE	(1 << 2)
/* unused			    (1 << 3) */
/** See lnet_md_t::options. */
#define LNET_MD_TRUNCATE	     (1 << 4)
/** See lnet_md_t::options. */
#define LNET_MD_ACK_DISABLE	  (1 << 5)
/** See lnet_md_t::options. */
#define LNET_MD_IOVEC		(1 << 6)
/** See lnet_md_t::options. */
#define LNET_MD_MAX_SIZE	     (1 << 7)
/** See lnet_md_t::options. */
#define LNET_MD_KIOV		 (1 << 8)

/* For compatibility with Cray Portals */
#define LNET_MD_PHYS			 0

/** Infinite threshold on MD operations. See lnet_md_t::threshold */
#define LNET_MD_THRESH_INF       (-1)

/* NB lustre portals uses struct iovec internally! */
typedef struct iovec lnet_md_iovec_t;

/**
 * A page-based fragment of a MD.
 */
typedef struct {
	/** Pointer to the page where the fragment resides */
	struct page      *kiov_page;
	/** Length in bytes of the fragment */
	unsigned int     kiov_len;
	/**
	 * Starting offset of the fragment within the page. Note that the
	 * end of the fragment must not pass the end of the page; i.e.,
	 * kiov_len + kiov_offset <= PAGE_CACHE_SIZE.
	 */
	unsigned int     kiov_offset;
} lnet_kiov_t;
/** @} lnet_md */

/** \addtogroup lnet_eq
 * @{ */

/**
 * Six types of events can be logged in an event queue.
 */
typedef enum {
	/** An incoming GET operation has completed on the MD. */
	LNET_EVENT_GET		= 1,
	/**
	 * An incoming PUT operation has completed on the MD. The
	 * underlying layers will not alter the memory (on behalf of this
	 * operation) once this event has been logged.
	 */
	LNET_EVENT_PUT,
	/**
	 * A REPLY operation has completed. This event is logged after the
	 * data (if any) from the REPLY has been written into the MD.
	 */
	LNET_EVENT_REPLY,
	/** An acknowledgment has been received. */
	LNET_EVENT_ACK,
	/**
	 * An outgoing send (PUT or GET) operation has completed. This event
	 * is logged after the entire buffer has been sent and it is safe for
	 * the caller to reuse the buffer.
	 *
	 * Note:
	 * - The LNET_EVENT_SEND doesn't guarantee message delivery. It can
	 *   happen even when the message has not yet been put out on wire.
	 * - It's unsafe to assume that in an outgoing GET operation
	 *   the LNET_EVENT_SEND event would happen before the
	 *   LNET_EVENT_REPLY event. The same holds for LNET_EVENT_SEND and
	 *   LNET_EVENT_ACK events in an outgoing PUT operation.
	 */
	LNET_EVENT_SEND,
	/**
	 * A MD has been unlinked. Note that LNetMDUnlink() does not
	 * necessarily trigger an LNET_EVENT_UNLINK event.
	 * \see LNetMDUnlink
	 */
	LNET_EVENT_UNLINK,
} lnet_event_kind_t;

#define LNET_SEQ_BASETYPE       long
typedef unsigned LNET_SEQ_BASETYPE lnet_seq_t;
#define LNET_SEQ_GT(a, b)	(((signed LNET_SEQ_BASETYPE)((a) - (b))) > 0)

/**
 * Information about an event on a MD.
 */
typedef struct {
	/** The identifier (nid, pid) of the target. */
	lnet_process_id_t   target;
	/** The identifier (nid, pid) of the initiator. */
	lnet_process_id_t   initiator;
	/**
	 * The NID of the immediate sender. If the request has been forwarded
	 * by routers, this is the NID of the last hop; otherwise it's the
	 * same as the initiator.
	 */
	lnet_nid_t	  sender;
	/** Indicates the type of the event. */
	lnet_event_kind_t   type;
	/** The portal table index specified in the request */
	unsigned int	pt_index;
	/** A copy of the match bits specified in the request. */
	__u64	       match_bits;
	/** The length (in bytes) specified in the request. */
	unsigned int	rlength;
	/**
	 * The length (in bytes) of the data that was manipulated by the
	 * operation. For truncated operations, the manipulated length will be
	 * the number of bytes specified by the MD (possibly with an offset,
	 * see lnet_md_t). For all other operations, the manipulated length
	 * will be the length of the requested operation, i.e. rlength.
	 */
	unsigned int	mlength;
	/**
	 * The handle to the MD associated with the event. The handle may be
	 * invalid if the MD has been unlinked.
	 */
	lnet_handle_md_t    md_handle;
	/**
	 * A snapshot of the state of the MD immediately after the event has
	 * been processed. In particular, the threshold field in md will
	 * reflect the value of the threshold after the operation occurred.
	 */
	lnet_md_t	   md;
	/**
	 * 64 bits of out-of-band user data. Only valid for LNET_EVENT_PUT.
	 * \see LNetPut
	 */
	__u64	       hdr_data;
	/**
	 * Indicates the completion status of the operation. It's 0 for
	 * successful operations, otherwise it's an error code.
	 */
	int		 status;
	/**
	 * Indicates whether the MD has been unlinked. Note that:
	 * - An event with unlinked set is the last event on the MD.
	 * - This field is also set for an explicit LNET_EVENT_UNLINK event.
	 * \see LNetMDUnlink
	 */
	int		 unlinked;
	/**
	 * The displacement (in bytes) into the memory region that the
	 * operation used. The offset can be determined by the operation for
	 * a remote managed MD or by the local MD.
	 * \see lnet_md_t::options
	 */
	unsigned int	offset;
	/**
	 * The sequence number for this event. Sequence numbers are unique
	 * to each event.
	 */
	volatile lnet_seq_t sequence;
} lnet_event_t;

/**
 * Event queue handler function type.
 *
 * The EQ handler runs for each event that is deposited into the EQ. The
 * handler is supplied with a pointer to the event that triggered the
 * handler invocation.
 *
 * The handler must not block, must be reentrant, and must not call any LNet
 * API functions. It should return as quickly as possible.
 */
typedef void (*lnet_eq_handler_t)(lnet_event_t *event);
#define LNET_EQ_HANDLER_NONE NULL
/** @} lnet_eq */

/** \addtogroup lnet_data
 * @{ */

/**
 * Specify whether an acknowledgment should be sent by target when the PUT
 * operation completes (i.e., when the data has been written to a MD of the
 * target process).
 *
 * \see lnet_md_t::options for the discussion on LNET_MD_ACK_DISABLE by which
 * acknowledgments can be disabled for a MD.
 */
typedef enum {
	/** Request an acknowledgment */
	LNET_ACK_REQ,
	/** Request that no acknowledgment should be generated. */
	LNET_NOACK_REQ
} lnet_ack_req_t;
/** @} lnet_data */

/** @} lnet */
#endif
