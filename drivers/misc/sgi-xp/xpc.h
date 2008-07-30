/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) structures and macros.
 */

#ifndef _DRIVERS_MISC_SGIXP_XPC_H
#define _DRIVERS_MISC_SGIXP_XPC_H

#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/addrs.h>
#include <asm/sn/mspec.h>
#include <asm/sn/shub_mmr.h>
#include "xp.h"

/*
 * XPC Version numbers consist of a major and minor number. XPC can always
 * talk to versions with same major #, and never talk to versions with a
 * different major #.
 */
#define _XPC_VERSION(_maj, _min)	(((_maj) << 4) | ((_min) & 0xf))
#define XPC_VERSION_MAJOR(_v)		((_v) >> 4)
#define XPC_VERSION_MINOR(_v)		((_v) & 0xf)

/*
 * The next macros define word or bit representations for given
 * C-brick nasid in either the SAL provided bit array representing
 * nasids in the partition/machine or the AMO_t array used for
 * inter-partition initiation communications.
 *
 * For SN2 machines, C-Bricks are alway even numbered NASIDs.  As
 * such, some space will be saved by insisting that nasid information
 * passed from SAL always be packed for C-Bricks and the
 * cross-partition interrupts use the same packing scheme.
 */
#define XPC_NASID_W_INDEX(_n)	(((_n) / 64) / 2)
#define XPC_NASID_B_INDEX(_n)	(((_n) / 2) & (64 - 1))
#define XPC_NASID_IN_ARRAY(_n, _p) ((_p)[XPC_NASID_W_INDEX(_n)] & \
				    (1UL << XPC_NASID_B_INDEX(_n)))
#define XPC_NASID_FROM_W_B(_w, _b) (((_w) * 64 + (_b)) * 2)

#define XPC_HB_DEFAULT_INTERVAL		5	/* incr HB every x secs */
#define XPC_HB_CHECK_DEFAULT_INTERVAL	20	/* check HB every x secs */

/* define the process name of HB checker and the CPU it is pinned to */
#define XPC_HB_CHECK_THREAD_NAME	"xpc_hb"
#define XPC_HB_CHECK_CPU		0

/* define the process name of the discovery thread */
#define XPC_DISCOVERY_THREAD_NAME	"xpc_discovery"

/*
 * the reserved page
 *
 *   SAL reserves one page of memory per partition for XPC. Though a full page
 *   in length (16384 bytes), its starting address is not page aligned, but it
 *   is cacheline aligned. The reserved page consists of the following:
 *
 *   reserved page header
 *
 *     The first two 64-byte cachelines of the reserved page contain the
 *     header (struct xpc_rsvd_page). Before SAL initialization has completed,
 *     SAL has set up the following fields of the reserved page header:
 *     SAL_signature, SAL_version, SAL_partid, and SAL_nasids_size. The
 *     other fields are set up by XPC. (xpc_rsvd_page points to the local
 *     partition's reserved page.)
 *
 *   part_nasids mask
 *   mach_nasids mask
 *
 *     SAL also sets up two bitmaps (or masks), one that reflects the actual
 *     nasids in this partition (part_nasids), and the other that reflects
 *     the actual nasids in the entire machine (mach_nasids). We're only
 *     interested in the even numbered nasids (which contain the processors
 *     and/or memory), so we only need half as many bits to represent the
 *     nasids. The part_nasids mask is located starting at the first cacheline
 *     following the reserved page header. The mach_nasids mask follows right
 *     after the part_nasids mask. The size in bytes of each mask is reflected
 *     by the reserved page header field 'SAL_nasids_size'. (Local partition's
 *     mask pointers are xpc_part_nasids and xpc_mach_nasids.)
 *
 *   vars	(ia64-sn2 only)
 *   vars part	(ia64-sn2 only)
 *
 *     Immediately following the mach_nasids mask are the XPC variables
 *     required by other partitions. First are those that are generic to all
 *     partitions (vars), followed on the next available cacheline by those
 *     which are partition specific (vars part). These are setup by XPC.
 *     (Local partition's vars pointers are xpc_vars and xpc_vars_part.)
 *
 * Note: Until 'stamp' is set non-zero, the partition XPC code has not been
 *       initialized.
 */
struct xpc_rsvd_page {
	u64 SAL_signature;	/* SAL: unique signature */
	u64 SAL_version;	/* SAL: version */
	short SAL_partid;	/* SAL: partition ID */
	short max_npartitions;	/* value of XPC_MAX_PARTITIONS */
	u8 version;
	u8 pad1[3];		/* align to next u64 in 1st 64-byte cacheline */
	union {
		u64 vars_pa;	/* physical address of struct xpc_vars */
		u64 activate_mq_gpa;	/* global phys address of activate_mq */
	} sn;
	unsigned long stamp;	/* time when reserved page was setup by XPC */
	u64 pad2[10];		/* align to last u64 in 2nd 64-byte cacheline */
	u64 SAL_nasids_size;	/* SAL: size of each nasid mask in bytes */
};

#define XPC_RP_VERSION _XPC_VERSION(2, 0) /* version 2.0 of the reserved page */

/*
 * Define the structures by which XPC variables can be exported to other
 * partitions. (There are two: struct xpc_vars and struct xpc_vars_part)
 */

/*
 * The following structure describes the partition generic variables
 * needed by other partitions in order to properly initialize.
 *
 * struct xpc_vars version number also applies to struct xpc_vars_part.
 * Changes to either structure and/or related functionality should be
 * reflected by incrementing either the major or minor version numbers
 * of struct xpc_vars.
 */
struct xpc_vars_sn2 {
	u8 version;
	u64 heartbeat;
	DECLARE_BITMAP(heartbeating_to_mask, XP_MAX_NPARTITIONS_SN2);
	u64 heartbeat_offline;	/* if 0, heartbeat should be changing */
	int activate_IRQ_nasid;
	int activate_IRQ_phys_cpuid;
	u64 vars_part_pa;
	u64 amos_page_pa;	/* paddr of page of AMOs from MSPEC driver */
	AMO_t *amos_page;	/* vaddr of page of AMOs from MSPEC driver */
};

#define XPC_V_VERSION _XPC_VERSION(3, 1)    /* version 3.1 of the cross vars */

/*
 * The following pertains to ia64-sn2 only.
 *
 * Memory for XPC's AMO variables is allocated by the MSPEC driver. These
 * pages are located in the lowest granule. The lowest granule uses 4k pages
 * for cached references and an alternate TLB handler to never provide a
 * cacheable mapping for the entire region. This will prevent speculative
 * reading of cached copies of our lines from being issued which will cause
 * a PI FSB Protocol error to be generated by the SHUB. For XPC, we need 64
 * AMO variables (based on XP_MAX_NPARTITIONS_SN2) to identify the senders of
 * NOTIFY IRQs, 128 AMO variables (based on XP_NASID_MASK_WORDS) to identify
 * the senders of ACTIVATE IRQs, 1 AMO variable to identify which remote
 * partitions (i.e., XPCs) consider themselves currently engaged with the
 * local XPC and 1 AMO variable to request partition deactivation.
 */
#define XPC_NOTIFY_IRQ_AMOS	0
#define XPC_ACTIVATE_IRQ_AMOS	(XPC_NOTIFY_IRQ_AMOS + XP_MAX_NPARTITIONS_SN2)
#define XPC_ENGAGED_PARTITIONS_AMO (XPC_ACTIVATE_IRQ_AMOS + XP_NASID_MASK_WORDS)
#define XPC_DEACTIVATE_REQUEST_AMO  (XPC_ENGAGED_PARTITIONS_AMO + 1)

/*
 * The following structure describes the per partition specific variables.
 *
 * An array of these structures, one per partition, will be defined. As a
 * partition becomes active XPC will copy the array entry corresponding to
 * itself from that partition. It is desirable that the size of this structure
 * evenly divides into a 128-byte cacheline, such that none of the entries in
 * this array crosses a 128-byte cacheline boundary. As it is now, each entry
 * occupies 64-bytes.
 */
struct xpc_vars_part_sn2 {
	u64 magic;

	u64 openclose_args_pa;	/* physical address of open and close args */
	u64 GPs_pa;		/* physical address of Get/Put values */

	u64 chctl_amo_pa;	/* physical address of chctl flags' AMO_t */

	int notify_IRQ_nasid;	/* nasid of where to send notify IRQs */
	int notify_IRQ_phys_cpuid;	/* CPUID of where to send notify IRQs */

	u8 nchannels;		/* #of defined channels supported */

	u8 reserved[23];	/* pad to a full 64 bytes */
};

/*
 * The vars_part MAGIC numbers play a part in the first contact protocol.
 *
 * MAGIC1 indicates that the per partition specific variables for a remote
 * partition have been initialized by this partition.
 *
 * MAGIC2 indicates that this partition has pulled the remote partititions
 * per partition variables that pertain to this partition.
 */
#define XPC_VP_MAGIC1	0x0053524156435058L   /* 'XPCVARS\0'L (little endian) */
#define XPC_VP_MAGIC2	0x0073726176435058L   /* 'XPCvars\0'L (little endian) */

/* the reserved page sizes and offsets */

#define XPC_RP_HEADER_SIZE	L1_CACHE_ALIGN(sizeof(struct xpc_rsvd_page))
#define XPC_RP_VARS_SIZE	L1_CACHE_ALIGN(sizeof(struct xpc_vars_sn2))

#define XPC_RP_PART_NASIDS(_rp) ((u64 *)((u8 *)(_rp) + XPC_RP_HEADER_SIZE))
#define XPC_RP_MACH_NASIDS(_rp) (XPC_RP_PART_NASIDS(_rp) + xp_nasid_mask_words)
#define XPC_RP_VARS(_rp)	((struct xpc_vars_sn2 *)(XPC_RP_MACH_NASIDS(_rp) + \
				    xp_nasid_mask_words))

/*
 * Functions registered by add_timer() or called by kernel_thread() only
 * allow for a single 64-bit argument. The following macros can be used to
 * pack and unpack two (32-bit, 16-bit or 8-bit) arguments into or out from
 * the passed argument.
 */
#define XPC_PACK_ARGS(_arg1, _arg2) \
			((((u64) _arg1) & 0xffffffff) | \
			((((u64) _arg2) & 0xffffffff) << 32))

#define XPC_UNPACK_ARG1(_args)	(((u64) _args) & 0xffffffff)
#define XPC_UNPACK_ARG2(_args)	((((u64) _args) >> 32) & 0xffffffff)

/*
 * Define a Get/Put value pair (pointers) used with a message queue.
 */
struct xpc_gp {
	s64 get;		/* Get value */
	s64 put;		/* Put value */
};

#define XPC_GP_SIZE \
		L1_CACHE_ALIGN(sizeof(struct xpc_gp) * XPC_MAX_NCHANNELS)

/*
 * Define a structure that contains arguments associated with opening and
 * closing a channel.
 */
struct xpc_openclose_args {
	u16 reason;		/* reason why channel is closing */
	u16 msg_size;		/* sizeof each message entry */
	u16 remote_nentries;	/* #of message entries in remote msg queue */
	u16 local_nentries;	/* #of message entries in local msg queue */
	u64 local_msgqueue_pa;	/* physical address of local message queue */
};

#define XPC_OPENCLOSE_ARGS_SIZE \
	      L1_CACHE_ALIGN(sizeof(struct xpc_openclose_args) * \
	      XPC_MAX_NCHANNELS)

/* struct xpc_msg flags */

#define	XPC_M_DONE		0x01	/* msg has been received/consumed */
#define	XPC_M_READY		0x02	/* msg is ready to be sent */
#define	XPC_M_INTERRUPT		0x04	/* send interrupt when msg consumed */

#define XPC_MSG_ADDRESS(_payload) \
		((struct xpc_msg *)((u8 *)(_payload) - XPC_MSG_PAYLOAD_OFFSET))

/*
 * Defines notify entry.
 *
 * This is used to notify a message's sender that their message was received
 * and consumed by the intended recipient.
 */
struct xpc_notify {
	u8 type;		/* type of notification */

	/* the following two fields are only used if type == XPC_N_CALL */
	xpc_notify_func func;	/* user's notify function */
	void *key;		/* pointer to user's key */
};

/* struct xpc_notify type of notification */

#define	XPC_N_CALL		0x01	/* notify function provided by user */

/*
 * Define the structure that manages all the stuff required by a channel. In
 * particular, they are used to manage the messages sent across the channel.
 *
 * This structure is private to a partition, and is NOT shared across the
 * partition boundary.
 *
 * There is an array of these structures for each remote partition. It is
 * allocated at the time a partition becomes active. The array contains one
 * of these structures for each potential channel connection to that partition.
 *
>>> sn2 only!!!
 * Each of these structures manages two message queues (circular buffers).
 * They are allocated at the time a channel connection is made. One of
 * these message queues (local_msgqueue) holds the locally created messages
 * that are destined for the remote partition. The other of these message
 * queues (remote_msgqueue) is a locally cached copy of the remote partition's
 * own local_msgqueue.
 *
 * The following is a description of the Get/Put pointers used to manage these
 * two message queues. Consider the local_msgqueue to be on one partition
 * and the remote_msgqueue to be its cached copy on another partition. A
 * description of what each of the lettered areas contains is included.
 *
 *
 *                     local_msgqueue      remote_msgqueue
 *
 *                        |/////////|      |/////////|
 *    w_remote_GP.get --> +---------+      |/////////|
 *                        |    F    |      |/////////|
 *     remote_GP.get  --> +---------+      +---------+ <-- local_GP->get
 *                        |         |      |         |
 *                        |         |      |    E    |
 *                        |         |      |         |
 *                        |         |      +---------+ <-- w_local_GP.get
 *                        |    B    |      |/////////|
 *                        |         |      |////D////|
 *                        |         |      |/////////|
 *                        |         |      +---------+ <-- w_remote_GP.put
 *                        |         |      |////C////|
 *      local_GP->put --> +---------+      +---------+ <-- remote_GP.put
 *                        |         |      |/////////|
 *                        |    A    |      |/////////|
 *                        |         |      |/////////|
 *     w_local_GP.put --> +---------+      |/////////|
 *                        |/////////|      |/////////|
 *
 *
 *	    ( remote_GP.[get|put] are cached copies of the remote
 *	      partition's local_GP->[get|put], and thus their values can
 *	      lag behind their counterparts on the remote partition. )
 *
 *
 *  A - Messages that have been allocated, but have not yet been sent to the
 *	remote partition.
 *
 *  B - Messages that have been sent, but have not yet been acknowledged by the
 *      remote partition as having been received.
 *
 *  C - Area that needs to be prepared for the copying of sent messages, by
 *	the clearing of the message flags of any previously received messages.
 *
 *  D - Area into which sent messages are to be copied from the remote
 *	partition's local_msgqueue and then delivered to their intended
 *	recipients. [ To allow for a multi-message copy, another pointer
 *	(next_msg_to_pull) has been added to keep track of the next message
 *	number needing to be copied (pulled). It chases after w_remote_GP.put.
 *	Any messages lying between w_local_GP.get and next_msg_to_pull have
 *	been copied and are ready to be delivered. ]
 *
 *  E - Messages that have been copied and delivered, but have not yet been
 *	acknowledged by the recipient as having been received.
 *
 *  F - Messages that have been acknowledged, but XPC has not yet notified the
 *	sender that the message was received by its intended recipient.
 *	This is also an area that needs to be prepared for the allocating of
 *	new messages, by the clearing of the message flags of the acknowledged
 *	messages.
 */
struct xpc_channel_sn2 {

	/* various flavors of local and remote Get/Put values */

	struct xpc_gp *local_GP;	/* local Get/Put values */
	struct xpc_gp remote_GP;	/* remote Get/Put values */
	struct xpc_gp w_local_GP;	/* working local Get/Put values */
	struct xpc_gp w_remote_GP;	/* working remote Get/Put values */
	s64 next_msg_to_pull;	/* Put value of next msg to pull */

	struct mutex msg_to_pull_mutex;	/* next msg to pull serialization */
};

struct xpc_channel_uv {
	/* >>> code is coming */
};

struct xpc_channel {
	short partid;		/* ID of remote partition connected */
	spinlock_t lock;	/* lock for updating this structure */
	u32 flags;		/* general flags */

	enum xp_retval reason;	/* reason why channel is disconnect'g */
	int reason_line;	/* line# disconnect initiated from */

	u16 number;		/* channel # */

	u16 msg_size;		/* sizeof each msg entry */
	u16 local_nentries;	/* #of msg entries in local msg queue */
	u16 remote_nentries;	/* #of msg entries in remote msg queue */

	void *local_msgqueue_base;	/* base address of kmalloc'd space */
	struct xpc_msg *local_msgqueue;	/* local message queue */
	void *remote_msgqueue_base;	/* base address of kmalloc'd space */
	struct xpc_msg *remote_msgqueue; /* cached copy of remote partition's */
					 /* local message queue */
	u64 remote_msgqueue_pa;	/* phys addr of remote partition's */
				/* local message queue */

	atomic_t references;	/* #of external references to queues */

	atomic_t n_on_msg_allocate_wq;	/* #on msg allocation wait queue */
	wait_queue_head_t msg_allocate_wq;	/* msg allocation wait queue */

	u8 delayed_chctl_flags;	/* chctl flags received, but delayed */
				/* action until channel disconnected */

	/* queue of msg senders who want to be notified when msg received */

	atomic_t n_to_notify;	/* #of msg senders to notify */
	struct xpc_notify *notify_queue;    /* notify queue for messages sent */

	xpc_channel_func func;	/* user's channel function */
	void *key;		/* pointer to user's key */

	struct completion wdisconnect_wait;    /* wait for channel disconnect */

	struct xpc_openclose_args *local_openclose_args; /* args passed on */
					     /* opening or closing of channel */

	/* kthread management related fields */

	atomic_t kthreads_assigned;	/* #of kthreads assigned to channel */
	u32 kthreads_assigned_limit;	/* limit on #of kthreads assigned */
	atomic_t kthreads_idle;	/* #of kthreads idle waiting for work */
	u32 kthreads_idle_limit;	/* limit on #of kthreads idle */
	atomic_t kthreads_active;	/* #of kthreads actively working */

	wait_queue_head_t idle_wq;	/* idle kthread wait queue */

	union {
		struct xpc_channel_sn2 sn2;
		struct xpc_channel_uv uv;
	} sn;

} ____cacheline_aligned;

/* struct xpc_channel flags */

#define	XPC_C_WASCONNECTED	0x00000001	/* channel was connected */

#define	XPC_C_ROPENREPLY	0x00000002	/* remote open channel reply */
#define	XPC_C_OPENREPLY		0x00000004	/* local open channel reply */
#define	XPC_C_ROPENREQUEST	0x00000008     /* remote open channel request */
#define	XPC_C_OPENREQUEST	0x00000010	/* local open channel request */

#define	XPC_C_SETUP		0x00000020 /* channel's msgqueues are alloc'd */
#define	XPC_C_CONNECTEDCALLOUT	0x00000040     /* connected callout initiated */
#define	XPC_C_CONNECTEDCALLOUT_MADE \
				0x00000080     /* connected callout completed */
#define	XPC_C_CONNECTED		0x00000100	/* local channel is connected */
#define	XPC_C_CONNECTING	0x00000200	/* channel is being connected */

#define	XPC_C_RCLOSEREPLY	0x00000400	/* remote close channel reply */
#define	XPC_C_CLOSEREPLY	0x00000800	/* local close channel reply */
#define	XPC_C_RCLOSEREQUEST	0x00001000    /* remote close channel request */
#define	XPC_C_CLOSEREQUEST	0x00002000     /* local close channel request */

#define	XPC_C_DISCONNECTED	0x00004000	/* channel is disconnected */
#define	XPC_C_DISCONNECTING	0x00008000   /* channel is being disconnected */
#define	XPC_C_DISCONNECTINGCALLOUT \
				0x00010000 /* disconnecting callout initiated */
#define	XPC_C_DISCONNECTINGCALLOUT_MADE \
				0x00020000 /* disconnecting callout completed */
#define	XPC_C_WDISCONNECT	0x00040000  /* waiting for channel disconnect */

/*
 * The channel control flags (chctl) union consists of a 64-bit variable which
 * is divided up into eight bytes, ordered from right to left. Byte zero
 * pertains to channel 0, byte one to channel 1, and so on. Each channel's byte
 * can have one or more of the chctl flags set in it.
 */

union xpc_channel_ctl_flags {
	u64 all_flags;
	u8 flags[XPC_MAX_NCHANNELS];
};

/* chctl flags */
#define	XPC_CHCTL_CLOSEREQUEST	0x01
#define	XPC_CHCTL_CLOSEREPLY	0x02
#define	XPC_CHCTL_OPENREQUEST	0x04
#define	XPC_CHCTL_OPENREPLY	0x08
#define	XPC_CHCTL_MSGREQUEST	0x10

#define XPC_OPENCLOSE_CHCTL_FLAGS \
			(XPC_CHCTL_CLOSEREQUEST | XPC_CHCTL_CLOSEREPLY | \
			 XPC_CHCTL_OPENREQUEST | XPC_CHCTL_OPENREPLY)
#define XPC_MSG_CHCTL_FLAGS	XPC_CHCTL_MSGREQUEST

static inline int
xpc_any_openclose_chctl_flags_set(union xpc_channel_ctl_flags *chctl)
{
	int ch_number;

	for (ch_number = 0; ch_number < XPC_MAX_NCHANNELS; ch_number++) {
		if (chctl->flags[ch_number] & XPC_OPENCLOSE_CHCTL_FLAGS)
			return 1;
	}
	return 0;
}

static inline int
xpc_any_msg_chctl_flags_set(union xpc_channel_ctl_flags *chctl)
{
	int ch_number;

	for (ch_number = 0; ch_number < XPC_MAX_NCHANNELS; ch_number++) {
		if (chctl->flags[ch_number] & XPC_MSG_CHCTL_FLAGS)
			return 1;
	}
	return 0;
}

/*
 * Manages channels on a partition basis. There is one of these structures
 * for each partition (a partition will never utilize the structure that
 * represents itself).
 */

struct xpc_partition_sn2 {
	u64 remote_amos_page_pa;	/* phys addr of partition's amos page */
	int activate_IRQ_nasid;	/* active partition's act/deact nasid */
	int activate_IRQ_phys_cpuid;	/* active part's act/deact phys cpuid */

	u64 remote_vars_pa;	/* phys addr of partition's vars */
	u64 remote_vars_part_pa;	/* phys addr of partition's vars part */
	u8 remote_vars_version;	/* version# of partition's vars */

	void *local_GPs_base;	/* base address of kmalloc'd space */
	struct xpc_gp *local_GPs;	/* local Get/Put values */
	void *remote_GPs_base;	/* base address of kmalloc'd space */
	struct xpc_gp *remote_GPs;	/* copy of remote partition's local */
					/* Get/Put values */
	u64 remote_GPs_pa;	/* phys address of remote partition's local */
				/* Get/Put values */

	u64 remote_openclose_args_pa;	/* phys addr of remote's args */

	int notify_IRQ_nasid;	/* nasid of where to send notify IRQs */
	int notify_IRQ_phys_cpuid;	/* CPUID of where to send notify IRQs */
	char notify_IRQ_owner[8];	/* notify IRQ's owner's name */

	AMO_t *remote_chctl_amo_va; /* address of remote chctl flags' AMO_t */
	AMO_t *local_chctl_amo_va;	/* address of chctl flags' AMO_t */

	struct timer_list dropped_notify_IRQ_timer;	/* dropped IRQ timer */
};

struct xpc_partition_uv {
	/* >>> code is coming */
};

struct xpc_partition {

	/* XPC HB infrastructure */

	u8 remote_rp_version;	/* version# of partition's rsvd pg */
	unsigned long remote_rp_stamp; /* time when rsvd pg was initialized */
	u64 remote_rp_pa;	/* phys addr of partition's rsvd pg */
	u64 last_heartbeat;	/* HB at last read */
	u32 activate_IRQ_rcvd;	/* IRQs since activation */
	spinlock_t act_lock;	/* protect updating of act_state */
	u8 act_state;		/* from XPC HB viewpoint */
	enum xp_retval reason;	/* reason partition is deactivating */
	int reason_line;	/* line# deactivation initiated from */

	unsigned long disengage_timeout;	/* timeout in jiffies */
	struct timer_list disengage_timer;

	/* XPC infrastructure referencing and teardown control */

	u8 setup_state;		/* infrastructure setup state */
	wait_queue_head_t teardown_wq;	/* kthread waiting to teardown infra */
	atomic_t references;	/* #of references to infrastructure */

	u8 nchannels;		/* #of defined channels supported */
	atomic_t nchannels_active;  /* #of channels that are not DISCONNECTED */
	atomic_t nchannels_engaged;  /* #of channels engaged with remote part */
	struct xpc_channel *channels;	/* array of channel structures */

	/* fields used for managing channel avialability and activity */

	union xpc_channel_ctl_flags chctl; /* chctl flags yet to be processed */
	spinlock_t chctl_lock;	/* chctl flags lock */

	void *local_openclose_args_base;   /* base address of kmalloc'd space */
	struct xpc_openclose_args *local_openclose_args;      /* local's args */
	void *remote_openclose_args_base;  /* base address of kmalloc'd space */
	struct xpc_openclose_args *remote_openclose_args; /* copy of remote's */
							  /* args */

	/* channel manager related fields */

	atomic_t channel_mgr_requests;	/* #of requests to activate chan mgr */
	wait_queue_head_t channel_mgr_wq;	/* channel mgr's wait queue */

	union {
		struct xpc_partition_sn2 sn2;
		struct xpc_partition_uv uv;
	} sn;

} ____cacheline_aligned;

/* struct xpc_partition act_state values (for XPC HB) */

#define	XPC_P_INACTIVE		0x00	/* partition is not active */
#define XPC_P_ACTIVATION_REQ	0x01	/* created thread to activate */
#define XPC_P_ACTIVATING	0x02	/* activation thread started */
#define XPC_P_ACTIVE		0x03	/* xpc_partition_up() was called */
#define XPC_P_DEACTIVATING	0x04	/* partition deactivation initiated */

#define XPC_DEACTIVATE_PARTITION(_p, _reason) \
			xpc_deactivate_partition(__LINE__, (_p), (_reason))

/* struct xpc_partition setup_state values */

#define XPC_P_UNSET		0x00	/* infrastructure was never setup */
#define XPC_P_SETUP		0x01	/* infrastructure is setup */
#define XPC_P_WTEARDOWN		0x02	/* waiting to teardown infrastructure */
#define XPC_P_TORNDOWN		0x03	/* infrastructure is torndown */

/*
 * struct xpc_partition_sn2's dropped notify IRQ timer is set to wait the
 * following interval #of seconds before checking for dropped notify IRQs.
 * These can occur whenever an IRQ's associated amo write doesn't complete
 * until after the IRQ was received.
 */
#define XPC_DROPPED_NOTIFY_IRQ_WAIT_INTERVAL	(0.25 * HZ)

/* number of seconds to wait for other partitions to disengage */
#define XPC_DISENGAGE_DEFAULT_TIMELIMIT		90

/* interval in seconds to print 'waiting deactivation' messages */
#define XPC_DEACTIVATE_PRINTMSG_INTERVAL	10

#define XPC_PARTID(_p)	((short)((_p) - &xpc_partitions[0]))

/* found in xp_main.c */
extern struct xpc_registration xpc_registrations[];

/* found in xpc_main.c */
extern struct device *xpc_part;
extern struct device *xpc_chan;
extern int xpc_disengage_timelimit;
extern int xpc_disengage_timedout;
extern atomic_t xpc_activate_IRQ_rcvd;
extern wait_queue_head_t xpc_activate_IRQ_wq;
extern void *xpc_heartbeating_to_mask;
extern void xpc_activate_partition(struct xpc_partition *);
extern void xpc_activate_kthreads(struct xpc_channel *, int);
extern void xpc_create_kthreads(struct xpc_channel *, int, int);
extern void xpc_disconnect_wait(int);
extern enum xp_retval (*xpc_rsvd_page_init) (struct xpc_rsvd_page *);
extern void (*xpc_heartbeat_init) (void);
extern void (*xpc_heartbeat_exit) (void);
extern void (*xpc_increment_heartbeat) (void);
extern void (*xpc_offline_heartbeat) (void);
extern void (*xpc_online_heartbeat) (void);
extern void (*xpc_check_remote_hb) (void);
extern enum xp_retval (*xpc_make_first_contact) (struct xpc_partition *);
extern u64 (*xpc_get_chctl_all_flags) (struct xpc_partition *);
extern void (*xpc_notify_senders_of_disconnect) (struct xpc_channel *);
extern void (*xpc_process_msg_chctl_flags) (struct xpc_partition *, int);
extern int (*xpc_n_of_deliverable_msgs) (struct xpc_channel *);
extern struct xpc_msg *(*xpc_get_deliverable_msg) (struct xpc_channel *);
extern void (*xpc_request_partition_activation) (struct xpc_rsvd_page *, u64,
						 int);
extern void (*xpc_request_partition_reactivation) (struct xpc_partition *);
extern void (*xpc_request_partition_deactivation) (struct xpc_partition *);
extern void (*xpc_cancel_partition_deactivation_request) (
							struct xpc_partition *);
extern void (*xpc_process_activate_IRQ_rcvd) (int);
extern enum xp_retval (*xpc_setup_infrastructure) (struct xpc_partition *);
extern void (*xpc_teardown_infrastructure) (struct xpc_partition *);

extern void (*xpc_indicate_partition_engaged) (struct xpc_partition *);
extern int (*xpc_partition_engaged) (short);
extern int (*xpc_any_partition_engaged) (void);
extern void (*xpc_indicate_partition_disengaged) (struct xpc_partition *);
extern void (*xpc_assume_partition_disengaged) (short);

extern void (*xpc_send_chctl_closerequest) (struct xpc_channel *,
					    unsigned long *);
extern void (*xpc_send_chctl_closereply) (struct xpc_channel *,
					  unsigned long *);
extern void (*xpc_send_chctl_openrequest) (struct xpc_channel *,
					   unsigned long *);
extern void (*xpc_send_chctl_openreply) (struct xpc_channel *, unsigned long *);

extern enum xp_retval (*xpc_send_msg) (struct xpc_channel *, u32, void *, u16,
				       u8, xpc_notify_func, void *);
extern void (*xpc_received_msg) (struct xpc_channel *, struct xpc_msg *);

/* found in xpc_sn2.c */
extern int xpc_init_sn2(void);
extern void xpc_exit_sn2(void);

/* found in xpc_uv.c */
extern void xpc_init_uv(void);
extern void xpc_exit_uv(void);

/* found in xpc_partition.c */
extern int xpc_exiting;
extern int xp_nasid_mask_words;
extern struct xpc_rsvd_page *xpc_rsvd_page;
extern u64 *xpc_mach_nasids;
extern struct xpc_partition *xpc_partitions;
extern char *xpc_remote_copy_buffer;
extern void *xpc_remote_copy_buffer_base;
extern void *xpc_kmalloc_cacheline_aligned(size_t, gfp_t, void **);
extern struct xpc_rsvd_page *xpc_setup_rsvd_page(void);
extern int xpc_identify_activate_IRQ_sender(void);
extern int xpc_partition_disengaged(struct xpc_partition *);
extern enum xp_retval xpc_mark_partition_active(struct xpc_partition *);
extern void xpc_mark_partition_inactive(struct xpc_partition *);
extern void xpc_discovery(void);
extern enum xp_retval xpc_get_remote_rp(int, u64 *, struct xpc_rsvd_page *,
					u64 *);
extern void xpc_deactivate_partition(const int, struct xpc_partition *,
				     enum xp_retval);
extern enum xp_retval xpc_initiate_partid_to_nasids(short, void *);

/* found in xpc_channel.c */
extern void *xpc_kzalloc_cacheline_aligned(size_t, gfp_t, void **);
extern void xpc_initiate_connect(int);
extern void xpc_initiate_disconnect(int);
extern enum xp_retval xpc_allocate_msg_wait(struct xpc_channel *);
extern enum xp_retval xpc_initiate_send(short, int, u32, void *, u16);
extern enum xp_retval xpc_initiate_send_notify(short, int, u32, void *, u16,
					       xpc_notify_func, void *);
extern void xpc_initiate_received(short, int, void *);
extern void xpc_process_sent_chctl_flags(struct xpc_partition *);
extern void xpc_connected_callout(struct xpc_channel *);
extern void xpc_deliver_msg(struct xpc_channel *);
extern void xpc_disconnect_channel(const int, struct xpc_channel *,
				   enum xp_retval, unsigned long *);
extern void xpc_disconnect_callout(struct xpc_channel *, enum xp_retval);
extern void xpc_partition_going_down(struct xpc_partition *, enum xp_retval);

static inline int
xpc_hb_allowed(short partid, void *heartbeating_to_mask)
{
	return test_bit(partid, heartbeating_to_mask);
}

static inline int
xpc_any_hbs_allowed(void)
{
	DBUG_ON(xpc_heartbeating_to_mask == NULL);
	return !bitmap_empty(xpc_heartbeating_to_mask, xp_max_npartitions);
}

static inline void
xpc_allow_hb(short partid)
{
	DBUG_ON(xpc_heartbeating_to_mask == NULL);
	set_bit(partid, xpc_heartbeating_to_mask);
}

static inline void
xpc_disallow_hb(short partid)
{
	DBUG_ON(xpc_heartbeating_to_mask == NULL);
	clear_bit(partid, xpc_heartbeating_to_mask);
}

static inline void
xpc_disallow_all_hbs(void)
{
	DBUG_ON(xpc_heartbeating_to_mask == NULL);
	bitmap_zero(xpc_heartbeating_to_mask, xp_max_npartitions);
}

static inline void
xpc_wakeup_channel_mgr(struct xpc_partition *part)
{
	if (atomic_inc_return(&part->channel_mgr_requests) == 1)
		wake_up(&part->channel_mgr_wq);
}

/*
 * These next two inlines are used to keep us from tearing down a channel's
 * msg queues while a thread may be referencing them.
 */
static inline void
xpc_msgqueue_ref(struct xpc_channel *ch)
{
	atomic_inc(&ch->references);
}

static inline void
xpc_msgqueue_deref(struct xpc_channel *ch)
{
	s32 refs = atomic_dec_return(&ch->references);

	DBUG_ON(refs < 0);
	if (refs == 0)
		xpc_wakeup_channel_mgr(&xpc_partitions[ch->partid]);
}

#define XPC_DISCONNECT_CHANNEL(_ch, _reason, _irqflgs) \
		xpc_disconnect_channel(__LINE__, _ch, _reason, _irqflgs)

/*
 * These two inlines are used to keep us from tearing down a partition's
 * setup infrastructure while a thread may be referencing it.
 */
static inline void
xpc_part_deref(struct xpc_partition *part)
{
	s32 refs = atomic_dec_return(&part->references);

	DBUG_ON(refs < 0);
	if (refs == 0 && part->setup_state == XPC_P_WTEARDOWN)
		wake_up(&part->teardown_wq);
}

static inline int
xpc_part_ref(struct xpc_partition *part)
{
	int setup;

	atomic_inc(&part->references);
	setup = (part->setup_state == XPC_P_SETUP);
	if (!setup)
		xpc_part_deref(part);

	return setup;
}

/*
 * The following macro is to be used for the setting of the reason and
 * reason_line fields in both the struct xpc_channel and struct xpc_partition
 * structures.
 */
#define XPC_SET_REASON(_p, _reason, _line) \
	{ \
		(_p)->reason = _reason; \
		(_p)->reason_line = _line; \
	}

#endif /* _DRIVERS_MISC_SGIXP_XPC_H */
