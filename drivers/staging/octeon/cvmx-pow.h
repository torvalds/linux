/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/**
 * Interface to the hardware Packet Order / Work unit.
 *
 * New, starting with SDK 1.7.0, cvmx-pow supports a number of
 * extended consistency checks. The define
 * CVMX_ENABLE_POW_CHECKS controls the runtime insertion of POW
 * internal state checks to find common programming errors. If
 * CVMX_ENABLE_POW_CHECKS is not defined, checks are by default
 * enabled. For example, cvmx-pow will check for the following
 * program errors or POW state inconsistency.
 * - Requesting a POW operation with an active tag switch in
 *   progress.
 * - Waiting for a tag switch to complete for an excessively
 *   long period. This is normally a sign of an error in locking
 *   causing deadlock.
 * - Illegal tag switches from NULL_NULL.
 * - Illegal tag switches from NULL.
 * - Illegal deschedule request.
 * - WQE pointer not matching the one attached to the core by
 *   the POW.
 *
 */

#ifndef __CVMX_POW_H__
#define __CVMX_POW_H__

#include <asm/octeon/cvmx-pow-defs.h>

#include "cvmx-scratch.h"
#include "cvmx-wqe.h"

/* Default to having all POW constancy checks turned on */
#ifndef CVMX_ENABLE_POW_CHECKS
#define CVMX_ENABLE_POW_CHECKS 1
#endif

enum cvmx_pow_tag_type {
	/* Tag ordering is maintained */
	CVMX_POW_TAG_TYPE_ORDERED   = 0L,
	/* Tag ordering is maintained, and at most one PP has the tag */
	CVMX_POW_TAG_TYPE_ATOMIC    = 1L,
	/*
	 * The work queue entry from the order - NEVER tag switch from
	 * NULL to NULL
	 */
	CVMX_POW_TAG_TYPE_NULL      = 2L,
	/* A tag switch to NULL, and there is no space reserved in POW
	 * - NEVER tag switch to NULL_NULL
	 * - NEVER tag switch from NULL_NULL
	 * - NULL_NULL is entered at the beginning of time and on a deschedule.
	 * - NULL_NULL can be exited by a new work request. A NULL_SWITCH
	 * load can also switch the state to NULL
	 */
	CVMX_POW_TAG_TYPE_NULL_NULL = 3L
};

/**
 * Wait flag values for pow functions.
 */
typedef enum {
	CVMX_POW_WAIT = 1,
	CVMX_POW_NO_WAIT = 0,
} cvmx_pow_wait_t;

/**
 *  POW tag operations.  These are used in the data stored to the POW.
 */
typedef enum {
	/*
	 * switch the tag (only) for this PP
	 * - the previous tag should be non-NULL in this case
	 * - tag switch response required
	 * - fields used: op, type, tag
	 */
	CVMX_POW_TAG_OP_SWTAG = 0L,
	/*
	 * switch the tag for this PP, with full information
	 * - this should be used when the previous tag is NULL
	 * - tag switch response required
	 * - fields used: address, op, grp, type, tag
	 */
	CVMX_POW_TAG_OP_SWTAG_FULL = 1L,
	/*
	 * switch the tag (and/or group) for this PP and de-schedule
	 * - OK to keep the tag the same and only change the group
	 * - fields used: op, no_sched, grp, type, tag
	 */
	CVMX_POW_TAG_OP_SWTAG_DESCH = 2L,
	/*
	 * just de-schedule
	 * - fields used: op, no_sched
	 */
	CVMX_POW_TAG_OP_DESCH = 3L,
	/*
	 * create an entirely new work queue entry
	 * - fields used: address, op, qos, grp, type, tag
	 */
	CVMX_POW_TAG_OP_ADDWQ = 4L,
	/*
	 * just update the work queue pointer and grp for this PP
	 * - fields used: address, op, grp
	 */
	CVMX_POW_TAG_OP_UPDATE_WQP_GRP = 5L,
	/*
	 * set the no_sched bit on the de-schedule list
	 *
	 * - does nothing if the selected entry is not on the
	 *   de-schedule list
	 *
	 * - does nothing if the stored work queue pointer does not
	 *   match the address field
	 *
	 * - fields used: address, index, op
	 *
	 *  Before issuing a *_NSCHED operation, SW must guarantee
	 *  that all prior deschedules and set/clr NSCHED operations
	 *  are complete and all prior switches are complete. The
	 *  hardware provides the opsdone bit and swdone bit for SW
	 *  polling. After issuing a *_NSCHED operation, SW must
	 *  guarantee that the set/clr NSCHED is complete before any
	 *  subsequent operations.
	 */
	CVMX_POW_TAG_OP_SET_NSCHED = 6L,
	/*
	 * clears the no_sched bit on the de-schedule list
	 *
	 * - does nothing if the selected entry is not on the
	 *   de-schedule list
	 *
	 * - does nothing if the stored work queue pointer does not
	 *   match the address field
	 *
	 * - fields used: address, index, op
	 *
	 * Before issuing a *_NSCHED operation, SW must guarantee that
	 * all prior deschedules and set/clr NSCHED operations are
	 * complete and all prior switches are complete. The hardware
	 * provides the opsdone bit and swdone bit for SW
	 * polling. After issuing a *_NSCHED operation, SW must
	 * guarantee that the set/clr NSCHED is complete before any
	 * subsequent operations.
	 */
	CVMX_POW_TAG_OP_CLR_NSCHED = 7L,
	/* do nothing */
	CVMX_POW_TAG_OP_NOP = 15L
} cvmx_pow_tag_op_t;

/**
 * This structure defines the store data on a store to POW
 */
typedef union {
	uint64_t u64;
	struct {
		/*
		 * Don't reschedule this entry. no_sched is used for
		 * CVMX_POW_TAG_OP_SWTAG_DESCH and
		 * CVMX_POW_TAG_OP_DESCH
		 */
		uint64_t no_sched:1;
		uint64_t unused:2;
		/* Tontains index of entry for a CVMX_POW_TAG_OP_*_NSCHED */
		uint64_t index:13;
		/* The operation to perform */
		cvmx_pow_tag_op_t op:4;
		uint64_t unused2:2;
		/*
		 * The QOS level for the packet. qos is only used for
		 * CVMX_POW_TAG_OP_ADDWQ
		 */
		uint64_t qos:3;
		/*
		 * The group that the work queue entry will be
		 * scheduled to grp is used for CVMX_POW_TAG_OP_ADDWQ,
		 * CVMX_POW_TAG_OP_SWTAG_FULL,
		 * CVMX_POW_TAG_OP_SWTAG_DESCH, and
		 * CVMX_POW_TAG_OP_UPDATE_WQP_GRP
		 */
		uint64_t grp:4;
		/*
		 * The type of the tag. type is used for everything
		 * except CVMX_POW_TAG_OP_DESCH,
		 * CVMX_POW_TAG_OP_UPDATE_WQP_GRP, and
		 * CVMX_POW_TAG_OP_*_NSCHED
		 */
		uint64_t type:3;
		/*
		 * The actual tag. tag is used for everything except
		 * CVMX_POW_TAG_OP_DESCH,
		 * CVMX_POW_TAG_OP_UPDATE_WQP_GRP, and
		 * CVMX_POW_TAG_OP_*_NSCHED
		 */
		uint64_t tag:32;
	} s;
} cvmx_pow_tag_req_t;

/**
 * This structure describes the address to load stuff from POW
 */
typedef union {
	uint64_t u64;

    /**
     * Address for new work request loads (did<2:0> == 0)
     */
	struct {
		/* Mips64 address region. Should be CVMX_IO_SEG */
		uint64_t mem_region:2;
		/* Must be zero */
		uint64_t reserved_49_61:13;
		/* Must be one */
		uint64_t is_io:1;
		/* the ID of POW -- did<2:0> == 0 in this case */
		uint64_t did:8;
		/* Must be zero */
		uint64_t reserved_4_39:36;
		/*
		 * If set, don't return load response until work is
		 * available.
		 */
		uint64_t wait:1;
		/* Must be zero */
		uint64_t reserved_0_2:3;
	} swork;

    /**
     * Address for loads to get POW internal status
     */
	struct {
		/* Mips64 address region. Should be CVMX_IO_SEG */
		uint64_t mem_region:2;
		/* Must be zero */
		uint64_t reserved_49_61:13;
		/* Must be one */
		uint64_t is_io:1;
		/* the ID of POW -- did<2:0> == 1 in this case */
		uint64_t did:8;
		/* Must be zero */
		uint64_t reserved_10_39:30;
		/* The core id to get status for */
		uint64_t coreid:4;
		/*
		 * If set and get_cur is set, return reverse tag-list
		 * pointer rather than forward tag-list pointer.
		 */
		uint64_t get_rev:1;
		/*
		 * If set, return current status rather than pending
		 * status.
		 */
		uint64_t get_cur:1;
		/*
		 * If set, get the work-queue pointer rather than
		 * tag/type.
		 */
		uint64_t get_wqp:1;
		/* Must be zero */
		uint64_t reserved_0_2:3;
	} sstatus;

    /**
     * Address for memory loads to get POW internal state
     */
	struct {
		/* Mips64 address region. Should be CVMX_IO_SEG */
		uint64_t mem_region:2;
		/* Must be zero */
		uint64_t reserved_49_61:13;
		/* Must be one */
		uint64_t is_io:1;
		/* the ID of POW -- did<2:0> == 2 in this case */
		uint64_t did:8;
		/* Must be zero */
		uint64_t reserved_16_39:24;
		/* POW memory index */
		uint64_t index:11;
		/*
		 * If set, return deschedule information rather than
		 * the standard response for work-queue index (invalid
		 * if the work-queue entry is not on the deschedule
		 * list).
		 */
		uint64_t get_des:1;
		/*
		 * If set, get the work-queue pointer rather than
		 * tag/type (no effect when get_des set).
		 */
		uint64_t get_wqp:1;
		/* Must be zero */
		uint64_t reserved_0_2:3;
	} smemload;

    /**
     * Address for index/pointer loads
     */
	struct {
		/* Mips64 address region. Should be CVMX_IO_SEG */
		uint64_t mem_region:2;
		/* Must be zero */
		uint64_t reserved_49_61:13;
		/* Must be one */
		uint64_t is_io:1;
		/* the ID of POW -- did<2:0> == 3 in this case */
		uint64_t did:8;
		/* Must be zero */
		uint64_t reserved_9_39:31;
		/*
		 * when {get_rmt ==0 AND get_des_get_tail == 0}, this
		 * field selects one of eight POW internal-input
		 * queues (0-7), one per QOS level; values 8-15 are
		 * illegal in this case; when {get_rmt ==0 AND
		 * get_des_get_tail == 1}, this field selects one of
		 * 16 deschedule lists (per group); when get_rmt ==1,
		 * this field selects one of 16 memory-input queue
		 * lists.  The two memory-input queue lists associated
		 * with each QOS level are:
		 *
		 * - qosgrp = 0, qosgrp = 8:      QOS0
		 * - qosgrp = 1, qosgrp = 9:      QOS1
		 * - qosgrp = 2, qosgrp = 10:     QOS2
		 * - qosgrp = 3, qosgrp = 11:     QOS3
		 * - qosgrp = 4, qosgrp = 12:     QOS4
		 * - qosgrp = 5, qosgrp = 13:     QOS5
		 * - qosgrp = 6, qosgrp = 14:     QOS6
		 * - qosgrp = 7, qosgrp = 15:     QOS7
		 */
		uint64_t qosgrp:4;
		/*
		 * If set and get_rmt is clear, return deschedule list
		 * indexes rather than indexes for the specified qos
		 * level; if set and get_rmt is set, return the tail
		 * pointer rather than the head pointer for the
		 * specified qos level.
		 */
		uint64_t get_des_get_tail:1;
		/*
		 * If set, return remote pointers rather than the
		 * local indexes for the specified qos level.
		 */
		uint64_t get_rmt:1;
		/* Must be zero */
		uint64_t reserved_0_2:3;
	} sindexload;

    /**
     * address for NULL_RD request (did<2:0> == 4) when this is read,
     * HW attempts to change the state to NULL if it is NULL_NULL (the
     * hardware cannot switch from NULL_NULL to NULL if a POW entry is
     * not available - software may need to recover by finishing
     * another piece of work before a POW entry can ever become
     * available.)
     */
	struct {
		/* Mips64 address region. Should be CVMX_IO_SEG */
		uint64_t mem_region:2;
		/* Must be zero */
		uint64_t reserved_49_61:13;
		/* Must be one */
		uint64_t is_io:1;
		/* the ID of POW -- did<2:0> == 4 in this case */
		uint64_t did:8;
		/* Must be zero */
		uint64_t reserved_0_39:40;
	} snull_rd;
} cvmx_pow_load_addr_t;

/**
 * This structure defines the response to a load/SENDSINGLE to POW
 * (except CSR reads)
 */
typedef union {
	uint64_t u64;

    /**
     * Response to new work request loads
     */
	struct {
		/*
		 * Set when no new work queue entry was returned.  *
		 * If there was de-scheduled work, the HW will
		 * definitely return it. When this bit is set, it
		 * could mean either mean:
		 *
		 * - There was no work, or
		 *
		 * - There was no work that the HW could find. This
		 *   case can happen, regardless of the wait bit value
		 *   in the original request, when there is work in
		 *   the IQ's that is too deep down the list.
		 */
		uint64_t no_work:1;
		/* Must be zero */
		uint64_t reserved_40_62:23;
		/* 36 in O1 -- the work queue pointer */
		uint64_t addr:40;
	} s_work;

    /**
     * Result for a POW Status Load (when get_cur==0 and get_wqp==0)
     */
	struct {
		uint64_t reserved_62_63:2;
		/* Set when there is a pending non-NULL SWTAG or
		 * SWTAG_FULL, and the POW entry has not left the list
		 * for the original tag. */
		uint64_t pend_switch:1;
		/* Set when SWTAG_FULL and pend_switch is set. */
		uint64_t pend_switch_full:1;
		/*
		 * Set when there is a pending NULL SWTAG, or an
		 * implicit switch to NULL.
		 */
		uint64_t pend_switch_null:1;
		/* Set when there is a pending DESCHED or SWTAG_DESCHED. */
		uint64_t pend_desched:1;
		/*
		 * Set when there is a pending SWTAG_DESCHED and
		 * pend_desched is set.
		 */
		uint64_t pend_desched_switch:1;
		/* Set when nosched is desired and pend_desched is set. */
		uint64_t pend_nosched:1;
		/* Set when there is a pending GET_WORK. */
		uint64_t pend_new_work:1;
		/*
		 * When pend_new_work is set, this bit indicates that
		 * the wait bit was set.
		 */
		uint64_t pend_new_work_wait:1;
		/* Set when there is a pending NULL_RD. */
		uint64_t pend_null_rd:1;
		/* Set when there is a pending CLR_NSCHED. */
		uint64_t pend_nosched_clr:1;
		uint64_t reserved_51:1;
		/* This is the index when pend_nosched_clr is set. */
		uint64_t pend_index:11;
		/*
		 * This is the new_grp when (pend_desched AND
		 * pend_desched_switch) is set.
		 */
		uint64_t pend_grp:4;
		uint64_t reserved_34_35:2;
		/*
		 * This is the tag type when pend_switch or
		 * (pend_desched AND pend_desched_switch) are set.
		 */
		uint64_t pend_type:2;
		/*
		 * - this is the tag when pend_switch or (pend_desched
		 *    AND pend_desched_switch) are set.
		 */
		uint64_t pend_tag:32;
	} s_sstatus0;

    /**
     * Result for a POW Status Load (when get_cur==0 and get_wqp==1)
     */
	struct {
		uint64_t reserved_62_63:2;
		/*
		 * Set when there is a pending non-NULL SWTAG or
		 * SWTAG_FULL, and the POW entry has not left the list
		 * for the original tag.
		 */
		uint64_t pend_switch:1;
		/* Set when SWTAG_FULL and pend_switch is set. */
		uint64_t pend_switch_full:1;
		/*
		 * Set when there is a pending NULL SWTAG, or an
		 * implicit switch to NULL.
		 */
		uint64_t pend_switch_null:1;
		/*
		 * Set when there is a pending DESCHED or
		 * SWTAG_DESCHED.
		 */
		uint64_t pend_desched:1;
		/*
		 * Set when there is a pending SWTAG_DESCHED and
		 * pend_desched is set.
		 */
		uint64_t pend_desched_switch:1;
		/* Set when nosched is desired and pend_desched is set. */
		uint64_t pend_nosched:1;
		/* Set when there is a pending GET_WORK. */
		uint64_t pend_new_work:1;
		/*
		 * When pend_new_work is set, this bit indicates that
		 * the wait bit was set.
		 */
		uint64_t pend_new_work_wait:1;
		/* Set when there is a pending NULL_RD. */
		uint64_t pend_null_rd:1;
		/* Set when there is a pending CLR_NSCHED. */
		uint64_t pend_nosched_clr:1;
		uint64_t reserved_51:1;
		/* This is the index when pend_nosched_clr is set. */
		uint64_t pend_index:11;
		/*
		 * This is the new_grp when (pend_desched AND
		 * pend_desched_switch) is set.
		 */
		uint64_t pend_grp:4;
		/* This is the wqp when pend_nosched_clr is set. */
		uint64_t pend_wqp:36;
	} s_sstatus1;

    /**
     * Result for a POW Status Load (when get_cur==1, get_wqp==0, and
     * get_rev==0)
     */
	struct {
		uint64_t reserved_62_63:2;
		/*
		 * Points to the next POW entry in the tag list when
		 * tail == 0 (and tag_type is not NULL or NULL_NULL).
		 */
		uint64_t link_index:11;
		/* The POW entry attached to the core. */
		uint64_t index:11;
		/*
		 * The group attached to the core (updated when new
		 * tag list entered on SWTAG_FULL).
		 */
		uint64_t grp:4;
		/*
		 * Set when this POW entry is at the head of its tag
		 * list (also set when in the NULL or NULL_NULL
		 * state).
		 */
		uint64_t head:1;
		/*
		 * Set when this POW entry is at the tail of its tag
		 * list (also set when in the NULL or NULL_NULL
		 * state).
		 */
		uint64_t tail:1;
		/*
		 * The tag type attached to the core (updated when new
		 * tag list entered on SWTAG, SWTAG_FULL, or
		 * SWTAG_DESCHED).
		 */
		uint64_t tag_type:2;
		/*
		 * The tag attached to the core (updated when new tag
		 * list entered on SWTAG, SWTAG_FULL, or
		 * SWTAG_DESCHED).
		 */
		uint64_t tag:32;
	} s_sstatus2;

    /**
     * Result for a POW Status Load (when get_cur==1, get_wqp==0, and get_rev==1)
     */
	struct {
		uint64_t reserved_62_63:2;
		/*
		 * Points to the prior POW entry in the tag list when
		 * head == 0 (and tag_type is not NULL or
		 * NULL_NULL). This field is unpredictable when the
		 * core's state is NULL or NULL_NULL.
		 */
		uint64_t revlink_index:11;
		/* The POW entry attached to the core. */
		uint64_t index:11;
		/*
		 * The group attached to the core (updated when new
		 * tag list entered on SWTAG_FULL).
		 */
		uint64_t grp:4;
		/* Set when this POW entry is at the head of its tag
		 * list (also set when in the NULL or NULL_NULL
		 * state).
		 */
		uint64_t head:1;
		/*
		 * Set when this POW entry is at the tail of its tag
		 * list (also set when in the NULL or NULL_NULL
		 * state).
		 */
		uint64_t tail:1;
		/*
		 * The tag type attached to the core (updated when new
		 * tag list entered on SWTAG, SWTAG_FULL, or
		 * SWTAG_DESCHED).
		 */
		uint64_t tag_type:2;
		/*
		 * The tag attached to the core (updated when new tag
		 * list entered on SWTAG, SWTAG_FULL, or
		 * SWTAG_DESCHED).
		 */
		uint64_t tag:32;
	} s_sstatus3;

    /**
     * Result for a POW Status Load (when get_cur==1, get_wqp==1, and
     * get_rev==0)
     */
	struct {
		uint64_t reserved_62_63:2;
		/*
		 * Points to the next POW entry in the tag list when
		 * tail == 0 (and tag_type is not NULL or NULL_NULL).
		 */
		uint64_t link_index:11;
		/* The POW entry attached to the core. */
		uint64_t index:11;
		/*
		 * The group attached to the core (updated when new
		 * tag list entered on SWTAG_FULL).
		 */
		uint64_t grp:4;
		/*
		 * The wqp attached to the core (updated when new tag
		 * list entered on SWTAG_FULL).
		 */
		uint64_t wqp:36;
	} s_sstatus4;

    /**
     * Result for a POW Status Load (when get_cur==1, get_wqp==1, and
     * get_rev==1)
     */
	struct {
		uint64_t reserved_62_63:2;
		/*
		 * Points to the prior POW entry in the tag list when
		 * head == 0 (and tag_type is not NULL or
		 * NULL_NULL). This field is unpredictable when the
		 * core's state is NULL or NULL_NULL.
		 */
		uint64_t revlink_index:11;
		/* The POW entry attached to the core. */
		uint64_t index:11;
		/*
		 * The group attached to the core (updated when new
		 * tag list entered on SWTAG_FULL).
		 */
		uint64_t grp:4;
		/*
		 * The wqp attached to the core (updated when new tag
		 * list entered on SWTAG_FULL).
		 */
		uint64_t wqp:36;
	} s_sstatus5;

    /**
     * Result For POW Memory Load (get_des == 0 and get_wqp == 0)
     */
	struct {
		uint64_t reserved_51_63:13;
		/*
		 * The next entry in the input, free, descheduled_head
		 * list (unpredictable if entry is the tail of the
		 * list).
		 */
		uint64_t next_index:11;
		/* The group of the POW entry. */
		uint64_t grp:4;
		uint64_t reserved_35:1;
		/*
		 * Set when this POW entry is at the tail of its tag
		 * list (also set when in the NULL or NULL_NULL
		 * state).
		 */
		uint64_t tail:1;
		/* The tag type of the POW entry. */
		uint64_t tag_type:2;
		/* The tag of the POW entry. */
		uint64_t tag:32;
	} s_smemload0;

    /**
     * Result For POW Memory Load (get_des == 0 and get_wqp == 1)
     */
	struct {
		uint64_t reserved_51_63:13;
		/*
		 * The next entry in the input, free, descheduled_head
		 * list (unpredictable if entry is the tail of the
		 * list).
		 */
		uint64_t next_index:11;
		/* The group of the POW entry. */
		uint64_t grp:4;
		/* The WQP held in the POW entry. */
		uint64_t wqp:36;
	} s_smemload1;

    /**
     * Result For POW Memory Load (get_des == 1)
     */
	struct {
		uint64_t reserved_51_63:13;
		/*
		 * The next entry in the tag list connected to the
		 * descheduled head.
		 */
		uint64_t fwd_index:11;
		/* The group of the POW entry. */
		uint64_t grp:4;
		/* The nosched bit for the POW entry. */
		uint64_t nosched:1;
		/* There is a pending tag switch */
		uint64_t pend_switch:1;
		/*
		 * The next tag type for the new tag list when
		 * pend_switch is set.
		 */
		uint64_t pend_type:2;
		/*
		 * The next tag for the new tag list when pend_switch
		 * is set.
		 */
		uint64_t pend_tag:32;
	} s_smemload2;

    /**
     * Result For POW Index/Pointer Load (get_rmt == 0/get_des_get_tail == 0)
     */
	struct {
		uint64_t reserved_52_63:12;
		/*
		 * set when there is one or more POW entries on the
		 * free list.
		 */
		uint64_t free_val:1;
		/*
		 * set when there is exactly one POW entry on the free
		 * list.
		 */
		uint64_t free_one:1;
		uint64_t reserved_49:1;
		/*
		 * when free_val is set, indicates the first entry on
		 * the free list.
		 */
		uint64_t free_head:11;
		uint64_t reserved_37:1;
		/*
		 * when free_val is set, indicates the last entry on
		 * the free list.
		 */
		uint64_t free_tail:11;
		/*
		 * set when there is one or more POW entries on the
		 * input Q list selected by qosgrp.
		 */
		uint64_t loc_val:1;
		/*
		 * set when there is exactly one POW entry on the
		 * input Q list selected by qosgrp.
		 */
		uint64_t loc_one:1;
		uint64_t reserved_23:1;
		/*
		 * when loc_val is set, indicates the first entry on
		 * the input Q list selected by qosgrp.
		 */
		uint64_t loc_head:11;
		uint64_t reserved_11:1;
		/*
		 * when loc_val is set, indicates the last entry on
		 * the input Q list selected by qosgrp.
		 */
		uint64_t loc_tail:11;
	} sindexload0;

    /**
     * Result For POW Index/Pointer Load (get_rmt == 0/get_des_get_tail == 1)
     */
	struct {
		uint64_t reserved_52_63:12;
		/*
		 * set when there is one or more POW entries on the
		 * nosched list.
		 */
		uint64_t nosched_val:1;
		/*
		 * set when there is exactly one POW entry on the
		 * nosched list.
		 */
		uint64_t nosched_one:1;
		uint64_t reserved_49:1;
		/*
		 * when nosched_val is set, indicates the first entry
		 * on the nosched list.
		 */
		uint64_t nosched_head:11;
		uint64_t reserved_37:1;
		/*
		 * when nosched_val is set, indicates the last entry
		 * on the nosched list.
		 */
		uint64_t nosched_tail:11;
		/*
		 * set when there is one or more descheduled heads on
		 * the descheduled list selected by qosgrp.
		 */
		uint64_t des_val:1;
		/*
		 * set when there is exactly one descheduled head on
		 * the descheduled list selected by qosgrp.
		 */
		uint64_t des_one:1;
		uint64_t reserved_23:1;
		/*
		 * when des_val is set, indicates the first
		 * descheduled head on the descheduled list selected
		 * by qosgrp.
		 */
		uint64_t des_head:11;
		uint64_t reserved_11:1;
		/*
		 * when des_val is set, indicates the last descheduled
		 * head on the descheduled list selected by qosgrp.
		 */
		uint64_t des_tail:11;
	} sindexload1;

    /**
     * Result For POW Index/Pointer Load (get_rmt == 1/get_des_get_tail == 0)
     */
	struct {
		uint64_t reserved_39_63:25;
		/*
		 * Set when this DRAM list is the current head
		 * (i.e. is the next to be reloaded when the POW
		 * hardware reloads a POW entry from DRAM). The POW
		 * hardware alternates between the two DRAM lists
		 * associated with a QOS level when it reloads work
		 * from DRAM into the POW unit.
		 */
		uint64_t rmt_is_head:1;
		/*
		 * Set when the DRAM portion of the input Q list
		 * selected by qosgrp contains one or more pieces of
		 * work.
		 */
		uint64_t rmt_val:1;
		/*
		 * Set when the DRAM portion of the input Q list
		 * selected by qosgrp contains exactly one piece of
		 * work.
		 */
		uint64_t rmt_one:1;
		/*
		 * When rmt_val is set, indicates the first piece of
		 * work on the DRAM input Q list selected by
		 * qosgrp.
		 */
		uint64_t rmt_head:36;
	} sindexload2;

    /**
     * Result For POW Index/Pointer Load (get_rmt ==
     * 1/get_des_get_tail == 1)
     */
	struct {
		uint64_t reserved_39_63:25;
		/*
		 * set when this DRAM list is the current head
		 * (i.e. is the next to be reloaded when the POW
		 * hardware reloads a POW entry from DRAM). The POW
		 * hardware alternates between the two DRAM lists
		 * associated with a QOS level when it reloads work
		 * from DRAM into the POW unit.
		 */
		uint64_t rmt_is_head:1;
		/*
		 * set when the DRAM portion of the input Q list
		 * selected by qosgrp contains one or more pieces of
		 * work.
		 */
		uint64_t rmt_val:1;
		/*
		 * set when the DRAM portion of the input Q list
		 * selected by qosgrp contains exactly one piece of
		 * work.
		 */
		uint64_t rmt_one:1;
		/*
		 * when rmt_val is set, indicates the last piece of
		 * work on the DRAM input Q list selected by
		 * qosgrp.
		 */
		uint64_t rmt_tail:36;
	} sindexload3;

    /**
     * Response to NULL_RD request loads
     */
	struct {
		uint64_t unused:62;
		/* of type cvmx_pow_tag_type_t. state is one of the
		 * following:
		 *
		 * - CVMX_POW_TAG_TYPE_ORDERED
		 * - CVMX_POW_TAG_TYPE_ATOMIC
		 * - CVMX_POW_TAG_TYPE_NULL
		 * - CVMX_POW_TAG_TYPE_NULL_NULL
		 */
		uint64_t state:2;
	} s_null_rd;

} cvmx_pow_tag_load_resp_t;

/**
 * This structure describes the address used for stores to the POW.
 *  The store address is meaningful on stores to the POW.  The
 *  hardware assumes that an aligned 64-bit store was used for all
 *  these stores.  Note the assumption that the work queue entry is
 *  aligned on an 8-byte boundary (since the low-order 3 address bits
 *  must be zero).  Note that not all fields are used by all
 *  operations.
 *
 *  NOTE: The following is the behavior of the pending switch bit at the PP
 *       for POW stores (i.e. when did<7:3> == 0xc)
 *     - did<2:0> == 0      => pending switch bit is set
 *     - did<2:0> == 1      => no affect on the pending switch bit
 *     - did<2:0> == 3      => pending switch bit is cleared
 *     - did<2:0> == 7      => no affect on the pending switch bit
 *     - did<2:0> == others => must not be used
 *     - No other loads/stores have an affect on the pending switch bit
 *     - The switch bus from POW can clear the pending switch bit
 *
 *  NOTE: did<2:0> == 2 is used by the HW for a special single-cycle
 *  ADDWQ command that only contains the pointer). SW must never use
 *  did<2:0> == 2.
 */
typedef union {
    /**
     * Unsigned 64 bit integer representation of store address
     */
	uint64_t u64;

	struct {
		/* Memory region.  Should be CVMX_IO_SEG in most cases */
		uint64_t mem_reg:2;
		uint64_t reserved_49_61:13;	/* Must be zero */
		uint64_t is_io:1;	/* Must be one */
		/* Device ID of POW.  Note that different sub-dids are used. */
		uint64_t did:8;
		uint64_t reserved_36_39:4;	/* Must be zero */
		/* Address field. addr<2:0> must be zero */
		uint64_t addr:36;
	} stag;
} cvmx_pow_tag_store_addr_t;

/**
 * decode of the store data when an IOBDMA SENDSINGLE is sent to POW
 */
typedef union {
	uint64_t u64;

	struct {
		/*
		 * the (64-bit word) location in scratchpad to write
		 * to (if len != 0)
		 */
		uint64_t scraddr:8;
		/* the number of words in the response (0 => no response) */
		uint64_t len:8;
		/* the ID of the device on the non-coherent bus */
		uint64_t did:8;
		uint64_t unused:36;
		/* if set, don't return load response until work is available */
		uint64_t wait:1;
		uint64_t unused2:3;
	} s;

} cvmx_pow_iobdma_store_t;

/* CSR typedefs have been moved to cvmx-csr-*.h */

/**
 * Get the POW tag for this core. This returns the current
 * tag type, tag, group, and POW entry index associated with
 * this core. Index is only valid if the tag type isn't NULL_NULL.
 * If a tag switch is pending this routine returns the tag before
 * the tag switch, not after.
 *
 * Returns Current tag
 */
static inline cvmx_pow_tag_req_t cvmx_pow_get_current_tag(void)
{
	cvmx_pow_load_addr_t load_addr;
	cvmx_pow_tag_load_resp_t load_resp;
	cvmx_pow_tag_req_t result;

	load_addr.u64 = 0;
	load_addr.sstatus.mem_region = CVMX_IO_SEG;
	load_addr.sstatus.is_io = 1;
	load_addr.sstatus.did = CVMX_OCT_DID_TAG_TAG1;
	load_addr.sstatus.coreid = cvmx_get_core_num();
	load_addr.sstatus.get_cur = 1;
	load_resp.u64 = cvmx_read_csr(load_addr.u64);
	result.u64 = 0;
	result.s.grp = load_resp.s_sstatus2.grp;
	result.s.index = load_resp.s_sstatus2.index;
	result.s.type = load_resp.s_sstatus2.tag_type;
	result.s.tag = load_resp.s_sstatus2.tag;
	return result;
}

/**
 * Get the POW WQE for this core. This returns the work queue
 * entry currently associated with this core.
 *
 * Returns WQE pointer
 */
static inline cvmx_wqe_t *cvmx_pow_get_current_wqp(void)
{
	cvmx_pow_load_addr_t load_addr;
	cvmx_pow_tag_load_resp_t load_resp;

	load_addr.u64 = 0;
	load_addr.sstatus.mem_region = CVMX_IO_SEG;
	load_addr.sstatus.is_io = 1;
	load_addr.sstatus.did = CVMX_OCT_DID_TAG_TAG1;
	load_addr.sstatus.coreid = cvmx_get_core_num();
	load_addr.sstatus.get_cur = 1;
	load_addr.sstatus.get_wqp = 1;
	load_resp.u64 = cvmx_read_csr(load_addr.u64);
	return (cvmx_wqe_t *) cvmx_phys_to_ptr(load_resp.s_sstatus4.wqp);
}

#ifndef CVMX_MF_CHORD
#define CVMX_MF_CHORD(dest)         CVMX_RDHWR(dest, 30)
#endif

/**
 * Print a warning if a tag switch is pending for this core
 *
 * @function: Function name checking for a pending tag switch
 */
static inline void __cvmx_pow_warn_if_pending_switch(const char *function)
{
	uint64_t switch_complete;
	CVMX_MF_CHORD(switch_complete);
	if (!switch_complete)
		pr_warning("%s called with tag switch in progress\n", function);
}

/**
 * Waits for a tag switch to complete by polling the completion bit.
 * Note that switches to NULL complete immediately and do not need
 * to be waited for.
 */
static inline void cvmx_pow_tag_sw_wait(void)
{
	const uint64_t MAX_CYCLES = 1ull << 31;
	uint64_t switch_complete;
	uint64_t start_cycle = cvmx_get_cycle();
	while (1) {
		CVMX_MF_CHORD(switch_complete);
		if (unlikely(switch_complete))
			break;
		if (unlikely(cvmx_get_cycle() > start_cycle + MAX_CYCLES)) {
			pr_warning("Tag switch is taking a long time, "
				   "possible deadlock\n");
			start_cycle = -MAX_CYCLES - 1;
		}
	}
}

/**
 * Synchronous work request.  Requests work from the POW.
 * This function does NOT wait for previous tag switches to complete,
 * so the caller must ensure that there is not a pending tag switch.
 *
 * @wait:   When set, call stalls until work becomes avaiable, or times out.
 *               If not set, returns immediately.
 *
 * Returns Returns the WQE pointer from POW. Returns NULL if no work
 * was available.
 */
static inline cvmx_wqe_t *cvmx_pow_work_request_sync_nocheck(cvmx_pow_wait_t
							     wait)
{
	cvmx_pow_load_addr_t ptr;
	cvmx_pow_tag_load_resp_t result;

	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	ptr.u64 = 0;
	ptr.swork.mem_region = CVMX_IO_SEG;
	ptr.swork.is_io = 1;
	ptr.swork.did = CVMX_OCT_DID_TAG_SWTAG;
	ptr.swork.wait = wait;

	result.u64 = cvmx_read_csr(ptr.u64);

	if (result.s_work.no_work)
		return NULL;
	else
		return (cvmx_wqe_t *) cvmx_phys_to_ptr(result.s_work.addr);
}

/**
 * Synchronous work request.  Requests work from the POW.
 * This function waits for any previous tag switch to complete before
 * requesting the new work.
 *
 * @wait:   When set, call stalls until work becomes avaiable, or times out.
 *               If not set, returns immediately.
 *
 * Returns Returns the WQE pointer from POW. Returns NULL if no work
 * was available.
 */
static inline cvmx_wqe_t *cvmx_pow_work_request_sync(cvmx_pow_wait_t wait)
{
	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	/* Must not have a switch pending when requesting work */
	cvmx_pow_tag_sw_wait();
	return cvmx_pow_work_request_sync_nocheck(wait);

}

/**
 * Synchronous null_rd request.  Requests a switch out of NULL_NULL POW state.
 * This function waits for any previous tag switch to complete before
 * requesting the null_rd.
 *
 * Returns Returns the POW state of type cvmx_pow_tag_type_t.
 */
static inline enum cvmx_pow_tag_type cvmx_pow_work_request_null_rd(void)
{
	cvmx_pow_load_addr_t ptr;
	cvmx_pow_tag_load_resp_t result;

	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	/* Must not have a switch pending when requesting work */
	cvmx_pow_tag_sw_wait();

	ptr.u64 = 0;
	ptr.snull_rd.mem_region = CVMX_IO_SEG;
	ptr.snull_rd.is_io = 1;
	ptr.snull_rd.did = CVMX_OCT_DID_TAG_NULL_RD;

	result.u64 = cvmx_read_csr(ptr.u64);

	return (enum cvmx_pow_tag_type) result.s_null_rd.state;
}

/**
 * Asynchronous work request.  Work is requested from the POW unit,
 * and should later be checked with function
 * cvmx_pow_work_response_async.  This function does NOT wait for
 * previous tag switches to complete, so the caller must ensure that
 * there is not a pending tag switch.
 *
 * @scr_addr: Scratch memory address that response will be returned
 *            to, which is either a valid WQE, or a response with the
 *            invalid bit set.  Byte address, must be 8 byte aligned.
 *
 * @wait: 1 to cause response to wait for work to become available (or
 *        timeout), 0 to cause response to return immediately
 */
static inline void cvmx_pow_work_request_async_nocheck(int scr_addr,
						       cvmx_pow_wait_t wait)
{
	cvmx_pow_iobdma_store_t data;

	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	/* scr_addr must be 8 byte aligned */
	data.s.scraddr = scr_addr >> 3;
	data.s.len = 1;
	data.s.did = CVMX_OCT_DID_TAG_SWTAG;
	data.s.wait = wait;
	cvmx_send_single(data.u64);
}

/**
 * Asynchronous work request.  Work is requested from the POW unit,
 * and should later be checked with function
 * cvmx_pow_work_response_async.  This function waits for any previous
 * tag switch to complete before requesting the new work.
 *
 * @scr_addr: Scratch memory address that response will be returned
 *            to, which is either a valid WQE, or a response with the
 *            invalid bit set.  Byte address, must be 8 byte aligned.
 *
 * @wait: 1 to cause response to wait for work to become available (or
 *                  timeout), 0 to cause response to return immediately
 */
static inline void cvmx_pow_work_request_async(int scr_addr,
					       cvmx_pow_wait_t wait)
{
	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	/* Must not have a switch pending when requesting work */
	cvmx_pow_tag_sw_wait();
	cvmx_pow_work_request_async_nocheck(scr_addr, wait);
}

/**
 * Gets result of asynchronous work request.  Performs a IOBDMA sync
 * to wait for the response.
 *
 * @scr_addr: Scratch memory address to get result from Byte address,
 *            must be 8 byte aligned.
 *
 * Returns Returns the WQE from the scratch register, or NULL if no
 * work was available.
 */
static inline cvmx_wqe_t *cvmx_pow_work_response_async(int scr_addr)
{
	cvmx_pow_tag_load_resp_t result;

	CVMX_SYNCIOBDMA;
	result.u64 = cvmx_scratch_read64(scr_addr);

	if (result.s_work.no_work)
		return NULL;
	else
		return (cvmx_wqe_t *) cvmx_phys_to_ptr(result.s_work.addr);
}

/**
 * Checks if a work queue entry pointer returned by a work
 * request is valid.  It may be invalid due to no work
 * being available or due to a timeout.
 *
 * @wqe_ptr: pointer to a work queue entry returned by the POW
 *
 * Returns 0 if pointer is valid
 *         1 if invalid (no work was returned)
 */
static inline uint64_t cvmx_pow_work_invalid(cvmx_wqe_t *wqe_ptr)
{
	return wqe_ptr == NULL;
}

/**
 * Starts a tag switch to the provided tag value and tag type.
 * Completion for the tag switch must be checked for separately.  This
 * function does NOT update the work queue entry in dram to match tag
 * value and type, so the application must keep track of these if they
 * are important to the application.  This tag switch command must not
 * be used for switches to NULL, as the tag switch pending bit will be
 * set by the switch request, but never cleared by the hardware.
 *
 * NOTE: This should not be used when switching from a NULL tag.  Use
 * cvmx_pow_tag_sw_full() instead.
 *
 * This function does no checks, so the caller must ensure that any
 * previous tag switch has completed.
 *
 * @tag:      new tag value
 * @tag_type: new tag type (ordered or atomic)
 */
static inline void cvmx_pow_tag_sw_nocheck(uint32_t tag,
					   enum cvmx_pow_tag_type tag_type)
{
	cvmx_addr_t ptr;
	cvmx_pow_tag_req_t tag_req;

	if (CVMX_ENABLE_POW_CHECKS) {
		cvmx_pow_tag_req_t current_tag;
		__cvmx_pow_warn_if_pending_switch(__func__);
		current_tag = cvmx_pow_get_current_tag();
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL_NULL)
			pr_warning("%s called with NULL_NULL tag\n",
				   __func__);
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL)
			pr_warning("%s called with NULL tag\n", __func__);
		if ((current_tag.s.type == tag_type)
		   && (current_tag.s.tag == tag))
			pr_warning("%s called to perform a tag switch to the "
				   "same tag\n",
			     __func__);
		if (tag_type == CVMX_POW_TAG_TYPE_NULL)
			pr_warning("%s called to perform a tag switch to "
				   "NULL. Use cvmx_pow_tag_sw_null() instead\n",
			     __func__);
	}

	/*
	 * Note that WQE in DRAM is not updated here, as the POW does
	 * not read from DRAM once the WQE is in flight.  See hardware
	 * manual for complete details.  It is the application's
	 * responsibility to keep track of the current tag value if
	 * that is important.
	 */

	tag_req.u64 = 0;
	tag_req.s.op = CVMX_POW_TAG_OP_SWTAG;
	tag_req.s.tag = tag;
	tag_req.s.type = tag_type;

	ptr.u64 = 0;
	ptr.sio.mem_region = CVMX_IO_SEG;
	ptr.sio.is_io = 1;
	ptr.sio.did = CVMX_OCT_DID_TAG_SWTAG;

	/* once this store arrives at POW, it will attempt the switch
	   software must wait for the switch to complete separately */
	cvmx_write_io(ptr.u64, tag_req.u64);
}

/**
 * Starts a tag switch to the provided tag value and tag type.
 * Completion for the tag switch must be checked for separately.  This
 * function does NOT update the work queue entry in dram to match tag
 * value and type, so the application must keep track of these if they
 * are important to the application.  This tag switch command must not
 * be used for switches to NULL, as the tag switch pending bit will be
 * set by the switch request, but never cleared by the hardware.
 *
 * NOTE: This should not be used when switching from a NULL tag.  Use
 * cvmx_pow_tag_sw_full() instead.
 *
 * This function waits for any previous tag switch to complete, and also
 * displays an error on tag switches to NULL.
 *
 * @tag:      new tag value
 * @tag_type: new tag type (ordered or atomic)
 */
static inline void cvmx_pow_tag_sw(uint32_t tag,
				   enum cvmx_pow_tag_type tag_type)
{
	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	/*
	 * Note that WQE in DRAM is not updated here, as the POW does
	 * not read from DRAM once the WQE is in flight.  See hardware
	 * manual for complete details.  It is the application's
	 * responsibility to keep track of the current tag value if
	 * that is important.
	 */

	/*
	 * Ensure that there is not a pending tag switch, as a tag
	 * switch cannot be started if a previous switch is still
	 * pending.
	 */
	cvmx_pow_tag_sw_wait();
	cvmx_pow_tag_sw_nocheck(tag, tag_type);
}

/**
 * Starts a tag switch to the provided tag value and tag type.
 * Completion for the tag switch must be checked for separately.  This
 * function does NOT update the work queue entry in dram to match tag
 * value and type, so the application must keep track of these if they
 * are important to the application.  This tag switch command must not
 * be used for switches to NULL, as the tag switch pending bit will be
 * set by the switch request, but never cleared by the hardware.
 *
 * This function must be used for tag switches from NULL.
 *
 * This function does no checks, so the caller must ensure that any
 * previous tag switch has completed.
 *
 * @wqp:      pointer to work queue entry to submit.  This entry is
 *            updated to match the other parameters
 * @tag:      tag value to be assigned to work queue entry
 * @tag_type: type of tag
 * @group:    group value for the work queue entry.
 */
static inline void cvmx_pow_tag_sw_full_nocheck(cvmx_wqe_t *wqp, uint32_t tag,
						enum cvmx_pow_tag_type tag_type,
						uint64_t group)
{
	cvmx_addr_t ptr;
	cvmx_pow_tag_req_t tag_req;

	if (CVMX_ENABLE_POW_CHECKS) {
		cvmx_pow_tag_req_t current_tag;
		__cvmx_pow_warn_if_pending_switch(__func__);
		current_tag = cvmx_pow_get_current_tag();
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL_NULL)
			pr_warning("%s called with NULL_NULL tag\n",
				   __func__);
		if ((current_tag.s.type == tag_type)
		   && (current_tag.s.tag == tag))
			pr_warning("%s called to perform a tag switch to "
				   "the same tag\n",
			     __func__);
		if (tag_type == CVMX_POW_TAG_TYPE_NULL)
			pr_warning("%s called to perform a tag switch to "
				   "NULL. Use cvmx_pow_tag_sw_null() instead\n",
			     __func__);
		if (wqp != cvmx_phys_to_ptr(0x80))
			if (wqp != cvmx_pow_get_current_wqp())
				pr_warning("%s passed WQE(%p) doesn't match "
					   "the address in the POW(%p)\n",
				     __func__, wqp,
				     cvmx_pow_get_current_wqp());
	}

	/*
	 * Note that WQE in DRAM is not updated here, as the POW does
	 * not read from DRAM once the WQE is in flight.  See hardware
	 * manual for complete details.  It is the application's
	 * responsibility to keep track of the current tag value if
	 * that is important.
	 */

	tag_req.u64 = 0;
	tag_req.s.op = CVMX_POW_TAG_OP_SWTAG_FULL;
	tag_req.s.tag = tag;
	tag_req.s.type = tag_type;
	tag_req.s.grp = group;

	ptr.u64 = 0;
	ptr.sio.mem_region = CVMX_IO_SEG;
	ptr.sio.is_io = 1;
	ptr.sio.did = CVMX_OCT_DID_TAG_SWTAG;
	ptr.sio.offset = CAST64(wqp);

	/*
	 * once this store arrives at POW, it will attempt the switch
	 * software must wait for the switch to complete separately.
	 */
	cvmx_write_io(ptr.u64, tag_req.u64);
}

/**
 * Starts a tag switch to the provided tag value and tag type.
 * Completion for the tag switch must be checked for separately.  This
 * function does NOT update the work queue entry in dram to match tag
 * value and type, so the application must keep track of these if they
 * are important to the application.  This tag switch command must not
 * be used for switches to NULL, as the tag switch pending bit will be
 * set by the switch request, but never cleared by the hardware.
 *
 * This function must be used for tag switches from NULL.
 *
 * This function waits for any pending tag switches to complete
 * before requesting the tag switch.
 *
 * @wqp:      pointer to work queue entry to submit.  This entry is updated
 *            to match the other parameters
 * @tag:      tag value to be assigned to work queue entry
 * @tag_type: type of tag
 * @group:      group value for the work queue entry.
 */
static inline void cvmx_pow_tag_sw_full(cvmx_wqe_t *wqp, uint32_t tag,
					enum cvmx_pow_tag_type tag_type,
					uint64_t group)
{
	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	/*
	 * Ensure that there is not a pending tag switch, as a tag
	 * switch cannot be started if a previous switch is still
	 * pending.
	 */
	cvmx_pow_tag_sw_wait();
	cvmx_pow_tag_sw_full_nocheck(wqp, tag, tag_type, group);
}

/**
 * Switch to a NULL tag, which ends any ordering or
 * synchronization provided by the POW for the current
 * work queue entry.  This operation completes immediately,
 * so completion should not be waited for.
 * This function does NOT wait for previous tag switches to complete,
 * so the caller must ensure that any previous tag switches have completed.
 */
static inline void cvmx_pow_tag_sw_null_nocheck(void)
{
	cvmx_addr_t ptr;
	cvmx_pow_tag_req_t tag_req;

	if (CVMX_ENABLE_POW_CHECKS) {
		cvmx_pow_tag_req_t current_tag;
		__cvmx_pow_warn_if_pending_switch(__func__);
		current_tag = cvmx_pow_get_current_tag();
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL_NULL)
			pr_warning("%s called with NULL_NULL tag\n",
				   __func__);
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL)
			pr_warning("%s called when we already have a "
				   "NULL tag\n",
			     __func__);
	}

	tag_req.u64 = 0;
	tag_req.s.op = CVMX_POW_TAG_OP_SWTAG;
	tag_req.s.type = CVMX_POW_TAG_TYPE_NULL;

	ptr.u64 = 0;
	ptr.sio.mem_region = CVMX_IO_SEG;
	ptr.sio.is_io = 1;
	ptr.sio.did = CVMX_OCT_DID_TAG_TAG1;

	cvmx_write_io(ptr.u64, tag_req.u64);

	/* switch to NULL completes immediately */
}

/**
 * Switch to a NULL tag, which ends any ordering or
 * synchronization provided by the POW for the current
 * work queue entry.  This operation completes immediately,
 * so completion should not be waited for.
 * This function waits for any pending tag switches to complete
 * before requesting the switch to NULL.
 */
static inline void cvmx_pow_tag_sw_null(void)
{
	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	/*
	 * Ensure that there is not a pending tag switch, as a tag
	 * switch cannot be started if a previous switch is still
	 * pending.
	 */
	cvmx_pow_tag_sw_wait();
	cvmx_pow_tag_sw_null_nocheck();

	/* switch to NULL completes immediately */
}

/**
 * Submits work to an input queue.  This function updates the work
 * queue entry in DRAM to match the arguments given.  Note that the
 * tag provided is for the work queue entry submitted, and is
 * unrelated to the tag that the core currently holds.
 *
 * @wqp:      pointer to work queue entry to submit.  This entry is
 *            updated to match the other parameters
 * @tag:      tag value to be assigned to work queue entry
 * @tag_type: type of tag
 * @qos:      Input queue to add to.
 * @grp:      group value for the work queue entry.
 */
static inline void cvmx_pow_work_submit(cvmx_wqe_t *wqp, uint32_t tag,
					enum cvmx_pow_tag_type tag_type,
					uint64_t qos, uint64_t grp)
{
	cvmx_addr_t ptr;
	cvmx_pow_tag_req_t tag_req;

	wqp->qos = qos;
	wqp->tag = tag;
	wqp->tag_type = tag_type;
	wqp->grp = grp;

	tag_req.u64 = 0;
	tag_req.s.op = CVMX_POW_TAG_OP_ADDWQ;
	tag_req.s.type = tag_type;
	tag_req.s.tag = tag;
	tag_req.s.qos = qos;
	tag_req.s.grp = grp;

	ptr.u64 = 0;
	ptr.sio.mem_region = CVMX_IO_SEG;
	ptr.sio.is_io = 1;
	ptr.sio.did = CVMX_OCT_DID_TAG_TAG1;
	ptr.sio.offset = cvmx_ptr_to_phys(wqp);

	/*
	 * SYNC write to memory before the work submit.  This is
	 * necessary as POW may read values from DRAM at this time.
	 */
	CVMX_SYNCWS;
	cvmx_write_io(ptr.u64, tag_req.u64);
}

/**
 * This function sets the group mask for a core.  The group mask
 * indicates which groups each core will accept work from. There are
 * 16 groups.
 *
 * @core_num:   core to apply mask to
 * @mask:   Group mask. There are 16 groups, so only bits 0-15 are valid,
 *               representing groups 0-15.
 *               Each 1 bit in the mask enables the core to accept work from
 *               the corresponding group.
 */
static inline void cvmx_pow_set_group_mask(uint64_t core_num, uint64_t mask)
{
	union cvmx_pow_pp_grp_mskx grp_msk;

	grp_msk.u64 = cvmx_read_csr(CVMX_POW_PP_GRP_MSKX(core_num));
	grp_msk.s.grp_msk = mask;
	cvmx_write_csr(CVMX_POW_PP_GRP_MSKX(core_num), grp_msk.u64);
}

/**
 * This function sets POW static priorities for a core. Each input queue has
 * an associated priority value.
 *
 * @core_num:   core to apply priorities to
 * @priority:   Vector of 8 priorities, one per POW Input Queue (0-7).
 *                   Highest priority is 0 and lowest is 7. A priority value
 *                   of 0xF instructs POW to skip the Input Queue when
 *                   scheduling to this specific core.
 *                   NOTE: priorities should not have gaps in values, meaning
 *                         {0,1,1,1,1,1,1,1} is a valid configuration while
 *                         {0,2,2,2,2,2,2,2} is not.
 */
static inline void cvmx_pow_set_priority(uint64_t core_num,
					 const uint8_t priority[])
{
	/* POW priorities are supported on CN5xxx and later */
	if (!OCTEON_IS_MODEL(OCTEON_CN3XXX)) {
		union cvmx_pow_pp_grp_mskx grp_msk;

		grp_msk.u64 = cvmx_read_csr(CVMX_POW_PP_GRP_MSKX(core_num));
		grp_msk.s.qos0_pri = priority[0];
		grp_msk.s.qos1_pri = priority[1];
		grp_msk.s.qos2_pri = priority[2];
		grp_msk.s.qos3_pri = priority[3];
		grp_msk.s.qos4_pri = priority[4];
		grp_msk.s.qos5_pri = priority[5];
		grp_msk.s.qos6_pri = priority[6];
		grp_msk.s.qos7_pri = priority[7];

		/* Detect gaps between priorities and flag error */
		{
			int i;
			uint32_t prio_mask = 0;

			for (i = 0; i < 8; i++)
				if (priority[i] != 0xF)
					prio_mask |= 1 << priority[i];

			if (prio_mask ^ ((1 << cvmx_pop(prio_mask)) - 1)) {
				pr_err("POW static priorities should be "
				       "contiguous (0x%llx)\n",
				     (unsigned long long)prio_mask);
				return;
			}
		}

		cvmx_write_csr(CVMX_POW_PP_GRP_MSKX(core_num), grp_msk.u64);
	}
}

/**
 * Performs a tag switch and then an immediate deschedule. This completes
 * immediately, so completion must not be waited for.  This function does NOT
 * update the wqe in DRAM to match arguments.
 *
 * This function does NOT wait for any prior tag switches to complete, so the
 * calling code must do this.
 *
 * Note the following CAVEAT of the Octeon HW behavior when
 * re-scheduling DE-SCHEDULEd items whose (next) state is
 * ORDERED:
 *   - If there are no switches pending at the time that the
 *     HW executes the de-schedule, the HW will only re-schedule
 *     the head of the FIFO associated with the given tag. This
 *     means that in many respects, the HW treats this ORDERED
 *     tag as an ATOMIC tag. Note that in the SWTAG_DESCH
 *     case (to an ORDERED tag), the HW will do the switch
 *     before the deschedule whenever it is possible to do
 *     the switch immediately, so it may often look like
 *     this case.
 *   - If there is a pending switch to ORDERED at the time
 *     the HW executes the de-schedule, the HW will perform
 *     the switch at the time it re-schedules, and will be
 *     able to reschedule any/all of the entries with the
 *     same tag.
 * Due to this behavior, the RECOMMENDATION to software is
 * that they have a (next) state of ATOMIC when they
 * DE-SCHEDULE. If an ORDERED tag is what was really desired,
 * SW can choose to immediately switch to an ORDERED tag
 * after the work (that has an ATOMIC tag) is re-scheduled.
 * Note that since there are never any tag switches pending
 * when the HW re-schedules, this switch can be IMMEDIATE upon
 * the reception of the pointer during the re-schedule.
 *
 * @tag:      New tag value
 * @tag_type: New tag type
 * @group:    New group value
 * @no_sched: Control whether this work queue entry will be rescheduled.
 *                 - 1 : don't schedule this work
 *                 - 0 : allow this work to be scheduled.
 */
static inline void cvmx_pow_tag_sw_desched_nocheck(
	uint32_t tag,
	enum cvmx_pow_tag_type tag_type,
	uint64_t group,
	uint64_t no_sched)
{
	cvmx_addr_t ptr;
	cvmx_pow_tag_req_t tag_req;

	if (CVMX_ENABLE_POW_CHECKS) {
		cvmx_pow_tag_req_t current_tag;
		__cvmx_pow_warn_if_pending_switch(__func__);
		current_tag = cvmx_pow_get_current_tag();
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL_NULL)
			pr_warning("%s called with NULL_NULL tag\n",
				   __func__);
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL)
			pr_warning("%s called with NULL tag. Deschedule not "
				   "allowed from NULL state\n",
			     __func__);
		if ((current_tag.s.type != CVMX_POW_TAG_TYPE_ATOMIC)
			&& (tag_type != CVMX_POW_TAG_TYPE_ATOMIC))
			pr_warning("%s called where neither the before or "
				   "after tag is ATOMIC\n",
			     __func__);
	}

	tag_req.u64 = 0;
	tag_req.s.op = CVMX_POW_TAG_OP_SWTAG_DESCH;
	tag_req.s.tag = tag;
	tag_req.s.type = tag_type;
	tag_req.s.grp = group;
	tag_req.s.no_sched = no_sched;

	ptr.u64 = 0;
	ptr.sio.mem_region = CVMX_IO_SEG;
	ptr.sio.is_io = 1;
	ptr.sio.did = CVMX_OCT_DID_TAG_TAG3;
	/*
	 * since TAG3 is used, this store will clear the local pending
	 * switch bit.
	 */
	cvmx_write_io(ptr.u64, tag_req.u64);
}

/**
 * Performs a tag switch and then an immediate deschedule. This completes
 * immediately, so completion must not be waited for.  This function does NOT
 * update the wqe in DRAM to match arguments.
 *
 * This function waits for any prior tag switches to complete, so the
 * calling code may call this function with a pending tag switch.
 *
 * Note the following CAVEAT of the Octeon HW behavior when
 * re-scheduling DE-SCHEDULEd items whose (next) state is
 * ORDERED:
 *   - If there are no switches pending at the time that the
 *     HW executes the de-schedule, the HW will only re-schedule
 *     the head of the FIFO associated with the given tag. This
 *     means that in many respects, the HW treats this ORDERED
 *     tag as an ATOMIC tag. Note that in the SWTAG_DESCH
 *     case (to an ORDERED tag), the HW will do the switch
 *     before the deschedule whenever it is possible to do
 *     the switch immediately, so it may often look like
 *     this case.
 *   - If there is a pending switch to ORDERED at the time
 *     the HW executes the de-schedule, the HW will perform
 *     the switch at the time it re-schedules, and will be
 *     able to reschedule any/all of the entries with the
 *     same tag.
 * Due to this behavior, the RECOMMENDATION to software is
 * that they have a (next) state of ATOMIC when they
 * DE-SCHEDULE. If an ORDERED tag is what was really desired,
 * SW can choose to immediately switch to an ORDERED tag
 * after the work (that has an ATOMIC tag) is re-scheduled.
 * Note that since there are never any tag switches pending
 * when the HW re-schedules, this switch can be IMMEDIATE upon
 * the reception of the pointer during the re-schedule.
 *
 * @tag:      New tag value
 * @tag_type: New tag type
 * @group:    New group value
 * @no_sched: Control whether this work queue entry will be rescheduled.
 *                 - 1 : don't schedule this work
 *                 - 0 : allow this work to be scheduled.
 */
static inline void cvmx_pow_tag_sw_desched(uint32_t tag,
					   enum cvmx_pow_tag_type tag_type,
					   uint64_t group, uint64_t no_sched)
{
	if (CVMX_ENABLE_POW_CHECKS)
		__cvmx_pow_warn_if_pending_switch(__func__);

	/* Need to make sure any writes to the work queue entry are complete */
	CVMX_SYNCWS;
	/*
	 * Ensure that there is not a pending tag switch, as a tag
	 * switch cannot be started if a previous switch is still
	 * pending.
	 */
	cvmx_pow_tag_sw_wait();
	cvmx_pow_tag_sw_desched_nocheck(tag, tag_type, group, no_sched);
}

/**
 * Descchedules the current work queue entry.
 *
 * @no_sched: no schedule flag value to be set on the work queue
 *            entry.  If this is set the entry will not be
 *            rescheduled.
 */
static inline void cvmx_pow_desched(uint64_t no_sched)
{
	cvmx_addr_t ptr;
	cvmx_pow_tag_req_t tag_req;

	if (CVMX_ENABLE_POW_CHECKS) {
		cvmx_pow_tag_req_t current_tag;
		__cvmx_pow_warn_if_pending_switch(__func__);
		current_tag = cvmx_pow_get_current_tag();
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL_NULL)
			pr_warning("%s called with NULL_NULL tag\n",
				   __func__);
		if (current_tag.s.type == CVMX_POW_TAG_TYPE_NULL)
			pr_warning("%s called with NULL tag. Deschedule not "
				   "expected from NULL state\n",
			     __func__);
	}

	/* Need to make sure any writes to the work queue entry are complete */
	CVMX_SYNCWS;

	tag_req.u64 = 0;
	tag_req.s.op = CVMX_POW_TAG_OP_DESCH;
	tag_req.s.no_sched = no_sched;

	ptr.u64 = 0;
	ptr.sio.mem_region = CVMX_IO_SEG;
	ptr.sio.is_io = 1;
	ptr.sio.did = CVMX_OCT_DID_TAG_TAG3;
	/*
	 * since TAG3 is used, this store will clear the local pending
	 * switch bit.
	 */
	cvmx_write_io(ptr.u64, tag_req.u64);
}

/****************************************************
* Define usage of bits within the 32 bit tag values.
*****************************************************/

/*
 * Number of bits of the tag used by software.  The SW bits are always
 * a contiguous block of the high starting at bit 31.  The hardware
 * bits are always the low bits.  By default, the top 8 bits of the
 * tag are reserved for software, and the low 24 are set by the IPD
 * unit.
 */
#define CVMX_TAG_SW_BITS    (8)
#define CVMX_TAG_SW_SHIFT   (32 - CVMX_TAG_SW_BITS)

/* Below is the list of values for the top 8 bits of the tag. */
/*
 * Tag values with top byte of this value are reserved for internal
 * executive uses.
 */
#define CVMX_TAG_SW_BITS_INTERNAL  0x1
/* The executive divides the remaining 24 bits as follows:
 *  - the upper 8 bits (bits 23 - 16 of the tag) define a subgroup
 *
 *  - the lower 16 bits (bits 15 - 0 of the tag) define are the value
 *    with the subgroup
 *
 * Note that this section describes the format of tags generated by
 * software - refer to the hardware documentation for a description of
 * the tags values generated by the packet input hardware.  Subgroups
 * are defined here.
 */
/* Mask for the value portion of the tag */
#define CVMX_TAG_SUBGROUP_MASK  0xFFFF
#define CVMX_TAG_SUBGROUP_SHIFT 16
#define CVMX_TAG_SUBGROUP_PKO  0x1

/* End of executive tag subgroup definitions */

/*
 * The remaining values software bit values 0x2 - 0xff are available
 * for application use.
 */

/**
 * This function creates a 32 bit tag value from the two values provided.
 *
 * @sw_bits: The upper bits (number depends on configuration) are set
 *           to this value.  The remainder of bits are set by the
 *           hw_bits parameter.
 *
 * @hw_bits: The lower bits (number depends on configuration) are set
 *           to this value.  The remainder of bits are set by the
 *           sw_bits parameter.
 *
 * Returns 32 bit value of the combined hw and sw bits.
 */
static inline uint32_t cvmx_pow_tag_compose(uint64_t sw_bits, uint64_t hw_bits)
{
	return ((sw_bits & cvmx_build_mask(CVMX_TAG_SW_BITS)) <<
			CVMX_TAG_SW_SHIFT) |
		(hw_bits & cvmx_build_mask(32 - CVMX_TAG_SW_BITS));
}

/**
 * Extracts the bits allocated for software use from the tag
 *
 * @tag:    32 bit tag value
 *
 * Returns N bit software tag value, where N is configurable with the
 * CVMX_TAG_SW_BITS define
 */
static inline uint32_t cvmx_pow_tag_get_sw_bits(uint64_t tag)
{
	return (tag >> (32 - CVMX_TAG_SW_BITS)) &
		cvmx_build_mask(CVMX_TAG_SW_BITS);
}

/**
 *
 * Extracts the bits allocated for hardware use from the tag
 *
 * @tag:    32 bit tag value
 *
 * Returns (32 - N) bit software tag value, where N is configurable
 * with the CVMX_TAG_SW_BITS define
 */
static inline uint32_t cvmx_pow_tag_get_hw_bits(uint64_t tag)
{
	return tag & cvmx_build_mask(32 - CVMX_TAG_SW_BITS);
}

/**
 * Store the current POW internal state into the supplied
 * buffer. It is recommended that you pass a buffer of at least
 * 128KB. The format of the capture may change based on SDK
 * version and Octeon chip.
 *
 * @buffer: Buffer to store capture into
 * @buffer_size:
 *               The size of the supplied buffer
 *
 * Returns Zero on success, negative on failure
 */
extern int cvmx_pow_capture(void *buffer, int buffer_size);

/**
 * Dump a POW capture to the console in a human readable format.
 *
 * @buffer: POW capture from cvmx_pow_capture()
 * @buffer_size:
 *               Size of the buffer
 */
extern void cvmx_pow_display(void *buffer, int buffer_size);

/**
 * Return the number of POW entries supported by this chip
 *
 * Returns Number of POW entries
 */
extern int cvmx_pow_get_num_entries(void);

#endif /* __CVMX_POW_H__ */
