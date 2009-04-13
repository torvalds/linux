/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2009 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) structures and macros.
 */

#ifndef _DRIVERS_MISC_SGIXP_XPC_H
#define _DRIVERS_MISC_SGIXP_XPC_H

#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include "xp.h"

/*
 * XPC Version numbers consist of a major and minor number. XPC can always
 * talk to versions with same major #, and never talk to versions with a
 * different major #.
 */
#define _XPC_VERSION(_maj, _min)	(((_maj) << 4) | ((_min) & 0xf))
#define XPC_VERSION_MAJOR(_v)		((_v) >> 4)
#define XPC_VERSION_MINOR(_v)		((_v) & 0xf)

/* define frequency of the heartbeat and frequency how often it's checked */
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
 *     nasids. When mapping nasid to bit in a mask (or bit to nasid) be sure
 *     to either divide or multiply by 2. The part_nasids mask is located
 *     starting at the first cacheline following the reserved page header. The
 *     mach_nasids mask follows right after the part_nasids mask. The size in
 *     bytes of each mask is reflected by the reserved page header field
 *     'SAL_nasids_size'. (Local partition's mask pointers are xpc_part_nasids
 *     and xpc_mach_nasids.)
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
 * Note: Until 'ts_jiffies' is set non-zero, the partition XPC code has not been
 *       initialized.
 */
struct xpc_rsvd_page {
	u64 SAL_signature;	/* SAL: unique signature */
	u64 SAL_version;	/* SAL: version */
	short SAL_partid;	/* SAL: partition ID */
	short max_npartitions;	/* value of XPC_MAX_PARTITIONS */
	u8 version;
	u8 pad1[3];		/* align to next u64 in 1st 64-byte cacheline */
	unsigned long ts_jiffies; /* timestamp when rsvd pg was setup by XPC */
	union {
		struct {
			unsigned long vars_pa;	/* phys addr */
		} sn2;
		struct {
			unsigned long heartbeat_gpa; /* phys addr */
			unsigned long activate_gru_mq_desc_gpa; /* phys addr */
		} uv;
	} sn;
	u64 pad2[9];		/* align to last u64 in 2nd 64-byte cacheline */
	u64 SAL_nasids_size;	/* SAL: size of each nasid mask in bytes */
};

#define XPC_RP_VERSION _XPC_VERSION(3, 0) /* version 3.0 of the reserved page */

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
	unsigned long vars_part_pa;
	unsigned long amos_page_pa;/* paddr of page of amos from MSPEC driver */
	struct amo *amos_page;	/* vaddr of page of amos from MSPEC driver */
};

#define XPC_V_VERSION _XPC_VERSION(3, 1)    /* version 3.1 of the cross vars */

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

	unsigned long openclose_args_pa; /* phys addr of open and close args */
	unsigned long GPs_pa;	/* physical address of Get/Put values */

	unsigned long chctl_amo_pa; /* physical address of chctl flags' amo */

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
#define XPC_VP_MAGIC1_SN2 0x0053524156435058L /* 'XPCVARS\0'L (little endian) */
#define XPC_VP_MAGIC2_SN2 0x0073726176435058L /* 'XPCvars\0'L (little endian) */

/* the reserved page sizes and offsets */

#define XPC_RP_HEADER_SIZE	L1_CACHE_ALIGN(sizeof(struct xpc_rsvd_page))
#define XPC_RP_VARS_SIZE	L1_CACHE_ALIGN(sizeof(struct xpc_vars_sn2))

#define XPC_RP_PART_NASIDS(_rp) ((unsigned long *)((u8 *)(_rp) + \
				 XPC_RP_HEADER_SIZE))
#define XPC_RP_MACH_NASIDS(_rp) (XPC_RP_PART_NASIDS(_rp) + \
				 xpc_nasid_mask_nlongs)
#define XPC_RP_VARS(_rp)	((struct xpc_vars_sn2 *) \
				 (XPC_RP_MACH_NASIDS(_rp) + \
				  xpc_nasid_mask_nlongs))


/*
 * The following structure describes the partition's heartbeat info which
 * will be periodically read by other partitions to determine whether this
 * XPC is still 'alive'.
 */
struct xpc_heartbeat_uv {
	unsigned long value;
	unsigned long offline;	/* if 0, heartbeat should be changing */
};

/*
 * Info pertinent to a GRU message queue using a watch list for irq generation.
 */
struct xpc_gru_mq_uv {
	void *address;		/* address of GRU message queue */
	unsigned int order;	/* size of GRU message queue as a power of 2 */
	int irq;		/* irq raised when message is received in mq */
	int mmr_blade;		/* blade where watchlist was allocated from */
	unsigned long mmr_offset; /* offset of irq mmr located on mmr_blade */
	unsigned long mmr_value; /* value of irq mmr located on mmr_blade */
	int watchlist_num;	/* number of watchlist allocatd by BIOS */
	void *gru_mq_desc;	/* opaque structure used by the GRU driver */
};

/*
 * The activate_mq is used to send/receive GRU messages that affect XPC's
 * partition active state and channel state. This is uv only.
 */
struct xpc_activate_mq_msghdr_uv {
	unsigned int gru_msg_hdr; /* FOR GRU INTERNAL USE ONLY */
	short partid;		/* sender's partid */
	u8 act_state;		/* sender's act_state at time msg sent */
	u8 type;		/* message's type */
	unsigned long rp_ts_jiffies; /* timestamp of sender's rp setup by XPC */
};

/* activate_mq defined message types */
#define XPC_ACTIVATE_MQ_MSG_SYNC_ACT_STATE_UV		0

#define XPC_ACTIVATE_MQ_MSG_ACTIVATE_REQ_UV		1
#define XPC_ACTIVATE_MQ_MSG_DEACTIVATE_REQ_UV		2

#define XPC_ACTIVATE_MQ_MSG_CHCTL_CLOSEREQUEST_UV	3
#define XPC_ACTIVATE_MQ_MSG_CHCTL_CLOSEREPLY_UV		4
#define XPC_ACTIVATE_MQ_MSG_CHCTL_OPENREQUEST_UV	5
#define XPC_ACTIVATE_MQ_MSG_CHCTL_OPENREPLY_UV		6
#define XPC_ACTIVATE_MQ_MSG_CHCTL_OPENCOMPLETE_UV	7

#define XPC_ACTIVATE_MQ_MSG_MARK_ENGAGED_UV		8
#define XPC_ACTIVATE_MQ_MSG_MARK_DISENGAGED_UV		9

struct xpc_activate_mq_msg_uv {
	struct xpc_activate_mq_msghdr_uv hdr;
};

struct xpc_activate_mq_msg_activate_req_uv {
	struct xpc_activate_mq_msghdr_uv hdr;
	unsigned long rp_gpa;
	unsigned long heartbeat_gpa;
	unsigned long activate_gru_mq_desc_gpa;
};

struct xpc_activate_mq_msg_deactivate_req_uv {
	struct xpc_activate_mq_msghdr_uv hdr;
	enum xp_retval reason;
};

struct xpc_activate_mq_msg_chctl_closerequest_uv {
	struct xpc_activate_mq_msghdr_uv hdr;
	short ch_number;
	enum xp_retval reason;
};

struct xpc_activate_mq_msg_chctl_closereply_uv {
	struct xpc_activate_mq_msghdr_uv hdr;
	short ch_number;
};

struct xpc_activate_mq_msg_chctl_openrequest_uv {
	struct xpc_activate_mq_msghdr_uv hdr;
	short ch_number;
	short entry_size;	/* size of notify_mq's GRU messages */
	short local_nentries;	/* ??? Is this needed? What is? */
};

struct xpc_activate_mq_msg_chctl_openreply_uv {
	struct xpc_activate_mq_msghdr_uv hdr;
	short ch_number;
	short remote_nentries;	/* ??? Is this needed? What is? */
	short local_nentries;	/* ??? Is this needed? What is? */
	unsigned long notify_gru_mq_desc_gpa;
};

struct xpc_activate_mq_msg_chctl_opencomplete_uv {
	struct xpc_activate_mq_msghdr_uv hdr;
	short ch_number;
};

/*
 * Functions registered by add_timer() or called by kernel_thread() only
 * allow for a single 64-bit argument. The following macros can be used to
 * pack and unpack two (32-bit, 16-bit or 8-bit) arguments into or out from
 * the passed argument.
 */
#define XPC_PACK_ARGS(_arg1, _arg2) \
			((((u64)_arg1) & 0xffffffff) | \
			((((u64)_arg2) & 0xffffffff) << 32))

#define XPC_UNPACK_ARG1(_args)	(((u64)_args) & 0xffffffff)
#define XPC_UNPACK_ARG2(_args)	((((u64)_args) >> 32) & 0xffffffff)

/*
 * Define a Get/Put value pair (pointers) used with a message queue.
 */
struct xpc_gp_sn2 {
	s64 get;		/* Get value */
	s64 put;		/* Put value */
};

#define XPC_GP_SIZE \
		L1_CACHE_ALIGN(sizeof(struct xpc_gp_sn2) * XPC_MAX_NCHANNELS)

/*
 * Define a structure that contains arguments associated with opening and
 * closing a channel.
 */
struct xpc_openclose_args {
	u16 reason;		/* reason why channel is closing */
	u16 entry_size;		/* sizeof each message entry */
	u16 remote_nentries;	/* #of message entries in remote msg queue */
	u16 local_nentries;	/* #of message entries in local msg queue */
	unsigned long local_msgqueue_pa; /* phys addr of local message queue */
};

#define XPC_OPENCLOSE_ARGS_SIZE \
	      L1_CACHE_ALIGN(sizeof(struct xpc_openclose_args) * \
	      XPC_MAX_NCHANNELS)


/*
 * Structures to define a fifo singly-linked list.
 */

struct xpc_fifo_entry_uv {
	struct xpc_fifo_entry_uv *next;
};

struct xpc_fifo_head_uv {
	struct xpc_fifo_entry_uv *first;
	struct xpc_fifo_entry_uv *last;
	spinlock_t lock;
	int n_entries;
};

/*
 * Define a sn2 styled message.
 *
 * A user-defined message resides in the payload area. The max size of the
 * payload is defined by the user via xpc_connect().
 *
 * The size of a message entry (within a message queue) must be a 128-byte
 * cacheline sized multiple in order to facilitate the BTE transfer of messages
 * from one message queue to another.
 */
struct xpc_msg_sn2 {
	u8 flags;		/* FOR XPC INTERNAL USE ONLY */
	u8 reserved[7];		/* FOR XPC INTERNAL USE ONLY */
	s64 number;		/* FOR XPC INTERNAL USE ONLY */

	u64 payload;		/* user defined portion of message */
};

/* struct xpc_msg_sn2 flags */

#define	XPC_M_SN2_DONE		0x01	/* msg has been received/consumed */
#define	XPC_M_SN2_READY		0x02	/* msg is ready to be sent */
#define	XPC_M_SN2_INTERRUPT	0x04	/* send interrupt when msg consumed */

/*
 * The format of a uv XPC notify_mq GRU message is as follows:
 *
 * A user-defined message resides in the payload area. The max size of the
 * payload is defined by the user via xpc_connect().
 *
 * The size of a message (payload and header) sent via the GRU must be either 1
 * or 2 GRU_CACHE_LINE_BYTES in length.
 */

struct xpc_notify_mq_msghdr_uv {
	union {
		unsigned int gru_msg_hdr;	/* FOR GRU INTERNAL USE ONLY */
		struct xpc_fifo_entry_uv next;	/* FOR XPC INTERNAL USE ONLY */
	} u;
	short partid;		/* FOR XPC INTERNAL USE ONLY */
	u8 ch_number;		/* FOR XPC INTERNAL USE ONLY */
	u8 size;		/* FOR XPC INTERNAL USE ONLY */
	unsigned int msg_slot_number;	/* FOR XPC INTERNAL USE ONLY */
};

struct xpc_notify_mq_msg_uv {
	struct xpc_notify_mq_msghdr_uv hdr;
	unsigned long payload;
};

/*
 * Define sn2's notify entry.
 *
 * This is used to notify a message's sender that their message was received
 * and consumed by the intended recipient.
 */
struct xpc_notify_sn2 {
	u8 type;		/* type of notification */

	/* the following two fields are only used if type == XPC_N_CALL */
	xpc_notify_func func;	/* user's notify function */
	void *key;		/* pointer to user's key */
};

/* struct xpc_notify_sn2 type of notification */

#define	XPC_N_CALL	0x01	/* notify function provided by user */

/*
 * Define uv's version of the notify entry. It additionally is used to allocate
 * a msg slot on the remote partition into which is copied a sent message.
 */
struct xpc_send_msg_slot_uv {
	struct xpc_fifo_entry_uv next;
	unsigned int msg_slot_number;
	xpc_notify_func func;	/* user's notify function */
	void *key;		/* pointer to user's key */
};

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
 */

/*
 * The following is sn2 only.
 *
 * Each channel structure manages two message queues (circular buffers).
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
	struct xpc_openclose_args *local_openclose_args; /* args passed on */
					     /* opening or closing of channel */

	void *local_msgqueue_base;	/* base address of kmalloc'd space */
	struct xpc_msg_sn2 *local_msgqueue;	/* local message queue */
	void *remote_msgqueue_base;	/* base address of kmalloc'd space */
	struct xpc_msg_sn2 *remote_msgqueue; /* cached copy of remote */
					   /* partition's local message queue */
	unsigned long remote_msgqueue_pa; /* phys addr of remote partition's */
					  /* local message queue */

	struct xpc_notify_sn2 *notify_queue;/* notify queue for messages sent */

	/* various flavors of local and remote Get/Put values */

	struct xpc_gp_sn2 *local_GP;	/* local Get/Put values */
	struct xpc_gp_sn2 remote_GP;	/* remote Get/Put values */
	struct xpc_gp_sn2 w_local_GP;	/* working local Get/Put values */
	struct xpc_gp_sn2 w_remote_GP;	/* working remote Get/Put values */
	s64 next_msg_to_pull;	/* Put value of next msg to pull */

	struct mutex msg_to_pull_mutex;	/* next msg to pull serialization */
};

struct xpc_channel_uv {
	void *cached_notify_gru_mq_desc; /* remote partition's notify mq's */
					 /* gru mq descriptor */

	struct xpc_send_msg_slot_uv *send_msg_slots;
	void *recv_msg_slots;	/* each slot will hold a xpc_notify_mq_msg_uv */
				/* structure plus the user's payload */

	struct xpc_fifo_head_uv msg_slot_free_list;
	struct xpc_fifo_head_uv recv_msg_list;	/* deliverable payloads */
};

struct xpc_channel {
	short partid;		/* ID of remote partition connected */
	spinlock_t lock;	/* lock for updating this structure */
	unsigned int flags;	/* general flags */

	enum xp_retval reason;	/* reason why channel is disconnect'g */
	int reason_line;	/* line# disconnect initiated from */

	u16 number;		/* channel # */

	u16 entry_size;		/* sizeof each msg entry */
	u16 local_nentries;	/* #of msg entries in local msg queue */
	u16 remote_nentries;	/* #of msg entries in remote msg queue */

	atomic_t references;	/* #of external references to queues */

	atomic_t n_on_msg_allocate_wq;	/* #on msg allocation wait queue */
	wait_queue_head_t msg_allocate_wq;	/* msg allocation wait queue */

	u8 delayed_chctl_flags;	/* chctl flags received, but delayed */
				/* action until channel disconnected */

	atomic_t n_to_notify;	/* #of msg senders to notify */

	xpc_channel_func func;	/* user's channel function */
	void *key;		/* pointer to user's key */

	struct completion wdisconnect_wait;    /* wait for channel disconnect */

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

#define XPC_C_ROPENCOMPLETE	0x00000002    /* remote open channel complete */
#define XPC_C_OPENCOMPLETE	0x00000004     /* local open channel complete */
#define	XPC_C_ROPENREPLY	0x00000008	/* remote open channel reply */
#define	XPC_C_OPENREPLY		0x00000010	/* local open channel reply */
#define	XPC_C_ROPENREQUEST	0x00000020     /* remote open channel request */
#define	XPC_C_OPENREQUEST	0x00000040	/* local open channel request */

#define	XPC_C_SETUP		0x00000080 /* channel's msgqueues are alloc'd */
#define	XPC_C_CONNECTEDCALLOUT	0x00000100     /* connected callout initiated */
#define	XPC_C_CONNECTEDCALLOUT_MADE \
				0x00000200     /* connected callout completed */
#define	XPC_C_CONNECTED		0x00000400	/* local channel is connected */
#define	XPC_C_CONNECTING	0x00000800	/* channel is being connected */

#define	XPC_C_RCLOSEREPLY	0x00001000	/* remote close channel reply */
#define	XPC_C_CLOSEREPLY	0x00002000	/* local close channel reply */
#define	XPC_C_RCLOSEREQUEST	0x00004000    /* remote close channel request */
#define	XPC_C_CLOSEREQUEST	0x00008000     /* local close channel request */

#define	XPC_C_DISCONNECTED	0x00010000	/* channel is disconnected */
#define	XPC_C_DISCONNECTING	0x00020000   /* channel is being disconnected */
#define	XPC_C_DISCONNECTINGCALLOUT \
				0x00040000 /* disconnecting callout initiated */
#define	XPC_C_DISCONNECTINGCALLOUT_MADE \
				0x00080000 /* disconnecting callout completed */
#define	XPC_C_WDISCONNECT	0x00100000  /* waiting for channel disconnect */

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
#define XPC_CHCTL_OPENCOMPLETE	0x10
#define	XPC_CHCTL_MSGREQUEST	0x20

#define XPC_OPENCLOSE_CHCTL_FLAGS \
			(XPC_CHCTL_CLOSEREQUEST | XPC_CHCTL_CLOSEREPLY | \
			 XPC_CHCTL_OPENREQUEST | XPC_CHCTL_OPENREPLY | \
			 XPC_CHCTL_OPENCOMPLETE)
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
 * Manage channels on a partition basis. There is one of these structures
 * for each partition (a partition will never utilize the structure that
 * represents itself).
 */

struct xpc_partition_sn2 {
	unsigned long remote_amos_page_pa; /* paddr of partition's amos page */
	int activate_IRQ_nasid;	/* active partition's act/deact nasid */
	int activate_IRQ_phys_cpuid;	/* active part's act/deact phys cpuid */

	unsigned long remote_vars_pa;	/* phys addr of partition's vars */
	unsigned long remote_vars_part_pa; /* paddr of partition's vars part */
	u8 remote_vars_version;	/* version# of partition's vars */

	void *local_GPs_base;	/* base address of kmalloc'd space */
	struct xpc_gp_sn2 *local_GPs;	/* local Get/Put values */
	void *remote_GPs_base;	/* base address of kmalloc'd space */
	struct xpc_gp_sn2 *remote_GPs;	/* copy of remote partition's local */
					/* Get/Put values */
	unsigned long remote_GPs_pa; /* phys addr of remote partition's local */
				     /* Get/Put values */

	void *local_openclose_args_base;   /* base address of kmalloc'd space */
	struct xpc_openclose_args *local_openclose_args;      /* local's args */
	unsigned long remote_openclose_args_pa;	/* phys addr of remote's args */

	int notify_IRQ_nasid;	/* nasid of where to send notify IRQs */
	int notify_IRQ_phys_cpuid;	/* CPUID of where to send notify IRQs */
	char notify_IRQ_owner[8];	/* notify IRQ's owner's name */

	struct amo *remote_chctl_amo_va; /* addr of remote chctl flags' amo */
	struct amo *local_chctl_amo_va;	/* address of chctl flags' amo */

	struct timer_list dropped_notify_IRQ_timer;	/* dropped IRQ timer */
};

struct xpc_partition_uv {
	unsigned long heartbeat_gpa; /* phys addr of partition's heartbeat */
	struct xpc_heartbeat_uv cached_heartbeat; /* cached copy of */
						  /* partition's heartbeat */
	unsigned long activate_gru_mq_desc_gpa;	/* phys addr of parititon's */
						/* activate mq's gru mq */
						/* descriptor */
	void *cached_activate_gru_mq_desc; /* cached copy of partition's */
					   /* activate mq's gru mq descriptor */
	struct mutex cached_activate_gru_mq_desc_mutex;
	spinlock_t flags_lock;	/* protect updating of flags */
	unsigned int flags;	/* general flags */
	u8 remote_act_state;	/* remote partition's act_state */
	u8 act_state_req;	/* act_state request from remote partition */
	enum xp_retval reason;	/* reason for deactivate act_state request */
};

/* struct xpc_partition_uv flags */

#define XPC_P_CACHED_ACTIVATE_GRU_MQ_DESC_UV	0x00000001
#define XPC_P_ENGAGED_UV			0x00000002

/* struct xpc_partition_uv act_state change requests */

#define XPC_P_ASR_ACTIVATE_UV		0x01
#define XPC_P_ASR_REACTIVATE_UV		0x02
#define XPC_P_ASR_DEACTIVATE_UV		0x03

struct xpc_partition {

	/* XPC HB infrastructure */

	u8 remote_rp_version;	/* version# of partition's rsvd pg */
	unsigned long remote_rp_ts_jiffies; /* timestamp when rsvd pg setup */
	unsigned long remote_rp_pa;	/* phys addr of partition's rsvd pg */
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

#define	XPC_P_AS_INACTIVE	0x00	/* partition is not active */
#define XPC_P_AS_ACTIVATION_REQ	0x01	/* created thread to activate */
#define XPC_P_AS_ACTIVATING	0x02	/* activation thread started */
#define XPC_P_AS_ACTIVE		0x03	/* xpc_partition_up() was called */
#define XPC_P_AS_DEACTIVATING	0x04	/* partition deactivation initiated */

#define XPC_DEACTIVATE_PARTITION(_p, _reason) \
			xpc_deactivate_partition(__LINE__, (_p), (_reason))

/* struct xpc_partition setup_state values */

#define XPC_P_SS_UNSET		0x00	/* infrastructure was never setup */
#define XPC_P_SS_SETUP		0x01	/* infrastructure is setup */
#define XPC_P_SS_WTEARDOWN	0x02	/* waiting to teardown infrastructure */
#define XPC_P_SS_TORNDOWN	0x03	/* infrastructure is torndown */

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
extern int xpc_activate_IRQ_rcvd;
extern spinlock_t xpc_activate_IRQ_rcvd_lock;
extern wait_queue_head_t xpc_activate_IRQ_wq;
extern void *xpc_kzalloc_cacheline_aligned(size_t, gfp_t, void **);
extern void xpc_activate_partition(struct xpc_partition *);
extern void xpc_activate_kthreads(struct xpc_channel *, int);
extern void xpc_create_kthreads(struct xpc_channel *, int, int);
extern void xpc_disconnect_wait(int);
extern int (*xpc_setup_partitions_sn) (void);
extern void (*xpc_teardown_partitions_sn) (void);
extern enum xp_retval (*xpc_get_partition_rsvd_page_pa) (void *, u64 *,
							 unsigned long *,
							 size_t *);
extern int (*xpc_setup_rsvd_page_sn) (struct xpc_rsvd_page *);
extern void (*xpc_heartbeat_init) (void);
extern void (*xpc_heartbeat_exit) (void);
extern void (*xpc_increment_heartbeat) (void);
extern void (*xpc_offline_heartbeat) (void);
extern void (*xpc_online_heartbeat) (void);
extern enum xp_retval (*xpc_get_remote_heartbeat) (struct xpc_partition *);
extern void (*xpc_allow_hb) (short);
extern void (*xpc_disallow_hb) (short);
extern void (*xpc_disallow_all_hbs) (void);
extern enum xp_retval (*xpc_make_first_contact) (struct xpc_partition *);
extern u64 (*xpc_get_chctl_all_flags) (struct xpc_partition *);
extern enum xp_retval (*xpc_setup_msg_structures) (struct xpc_channel *);
extern void (*xpc_teardown_msg_structures) (struct xpc_channel *);
extern void (*xpc_notify_senders_of_disconnect) (struct xpc_channel *);
extern void (*xpc_process_msg_chctl_flags) (struct xpc_partition *, int);
extern int (*xpc_n_of_deliverable_payloads) (struct xpc_channel *);
extern void *(*xpc_get_deliverable_payload) (struct xpc_channel *);
extern void (*xpc_request_partition_activation) (struct xpc_rsvd_page *,
						 unsigned long, int);
extern void (*xpc_request_partition_reactivation) (struct xpc_partition *);
extern void (*xpc_request_partition_deactivation) (struct xpc_partition *);
extern void (*xpc_cancel_partition_deactivation_request) (
							struct xpc_partition *);
extern void (*xpc_process_activate_IRQ_rcvd) (void);
extern enum xp_retval (*xpc_setup_ch_structures_sn) (struct xpc_partition *);
extern void (*xpc_teardown_ch_structures_sn) (struct xpc_partition *);

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
extern void (*xpc_send_chctl_opencomplete) (struct xpc_channel *,
					    unsigned long *);

extern enum xp_retval (*xpc_save_remote_msgqueue_pa) (struct xpc_channel *,
						      unsigned long);

extern enum xp_retval (*xpc_send_payload) (struct xpc_channel *, u32, void *,
					   u16, u8, xpc_notify_func, void *);
extern void (*xpc_received_payload) (struct xpc_channel *, void *);

/* found in xpc_sn2.c */
extern int xpc_init_sn2(void);
extern void xpc_exit_sn2(void);

/* found in xpc_uv.c */
extern int xpc_init_uv(void);
extern void xpc_exit_uv(void);

/* found in xpc_partition.c */
extern int xpc_exiting;
extern int xpc_nasid_mask_nlongs;
extern struct xpc_rsvd_page *xpc_rsvd_page;
extern unsigned long *xpc_mach_nasids;
extern struct xpc_partition *xpc_partitions;
extern void *xpc_kmalloc_cacheline_aligned(size_t, gfp_t, void **);
extern int xpc_setup_rsvd_page(void);
extern void xpc_teardown_rsvd_page(void);
extern int xpc_identify_activate_IRQ_sender(void);
extern int xpc_partition_disengaged(struct xpc_partition *);
extern enum xp_retval xpc_mark_partition_active(struct xpc_partition *);
extern void xpc_mark_partition_inactive(struct xpc_partition *);
extern void xpc_discovery(void);
extern enum xp_retval xpc_get_remote_rp(int, unsigned long *,
					struct xpc_rsvd_page *,
					unsigned long *);
extern void xpc_deactivate_partition(const int, struct xpc_partition *,
				     enum xp_retval);
extern enum xp_retval xpc_initiate_partid_to_nasids(short, void *);

/* found in xpc_channel.c */
extern void xpc_initiate_connect(int);
extern void xpc_initiate_disconnect(int);
extern enum xp_retval xpc_allocate_msg_wait(struct xpc_channel *);
extern enum xp_retval xpc_initiate_send(short, int, u32, void *, u16);
extern enum xp_retval xpc_initiate_send_notify(short, int, u32, void *, u16,
					       xpc_notify_func, void *);
extern void xpc_initiate_received(short, int, void *);
extern void xpc_process_sent_chctl_flags(struct xpc_partition *);
extern void xpc_connected_callout(struct xpc_channel *);
extern void xpc_deliver_payload(struct xpc_channel *);
extern void xpc_disconnect_channel(const int, struct xpc_channel *,
				   enum xp_retval, unsigned long *);
extern void xpc_disconnect_callout(struct xpc_channel *, enum xp_retval);
extern void xpc_partition_going_down(struct xpc_partition *, enum xp_retval);

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
	if (refs == 0 && part->setup_state == XPC_P_SS_WTEARDOWN)
		wake_up(&part->teardown_wq);
}

static inline int
xpc_part_ref(struct xpc_partition *part)
{
	int setup;

	atomic_inc(&part->references);
	setup = (part->setup_state == XPC_P_SS_SETUP);
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
