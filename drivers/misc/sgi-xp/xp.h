/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2008 Silicon Graphics, Inc. All rights reserved.
 */

/*
 * External Cross Partition (XP) structures and defines.
 */

#ifndef _DRIVERS_MISC_SGIXP_XP_H
#define _DRIVERS_MISC_SGIXP_XP_H

#include <linux/mutex.h>

#ifdef CONFIG_IA64
#include <asm/system.h>
#include <asm/sn/arch.h>	/* defines is_shub1() and is_shub2() */
#define is_shub()	ia64_platform_is("sn2")
#define is_uv()		ia64_platform_is("uv")
#endif
#ifdef CONFIG_X86_64
#include <asm/genapic.h>
#define is_uv()		is_uv_system()
#endif

#ifndef is_shub1
#define is_shub1()	0
#endif

#ifndef is_shub2
#define is_shub2()	0
#endif

#ifndef is_shub
#define is_shub()	0
#endif

#ifndef is_uv
#define is_uv()		0
#endif

#ifdef USE_DBUG_ON
#define DBUG_ON(condition)	BUG_ON(condition)
#else
#define DBUG_ON(condition)
#endif

/*
 * Define the maximum number of partitions the system can possibly support.
 * It is based on the maximum number of hardware partitionable regions. The
 * term 'region' in this context refers to the minimum number of nodes that
 * can comprise an access protection grouping. The access protection is in
 * regards to memory, IPI and IOI.
 *
 * The maximum number of hardware partitionable regions is equal to the
 * maximum number of nodes in the entire system divided by the minimum number
 * of nodes that comprise an access protection grouping.
 */
#define XP_MAX_NPARTITIONS_SN2	64
#define XP_MAX_NPARTITIONS_UV	256

/*
 * XPC establishes channel connections between the local partition and any
 * other partition that is currently up. Over these channels, kernel-level
 * `users' can communicate with their counterparts on the other partitions.
 *
 * If the need for additional channels arises, one can simply increase
 * XPC_MAX_NCHANNELS accordingly. If the day should come where that number
 * exceeds the absolute MAXIMUM number of channels possible (eight), then one
 * will need to make changes to the XPC code to accommodate for this.
 *
 * The absolute maximum number of channels possible is limited to eight for
 * performance reasons on sn2 hardware. The internal cross partition structures
 * require sixteen bytes per channel, and eight allows all of this
 * interface-shared info to fit in one 128-byte cacheline.
 */
#define XPC_MEM_CHANNEL		0	/* memory channel number */
#define	XPC_NET_CHANNEL		1	/* network channel number */

#define XPC_MAX_NCHANNELS	2	/* max #of channels allowed */

#if XPC_MAX_NCHANNELS > 8
#error	XPC_MAX_NCHANNELS exceeds absolute MAXIMUM possible.
#endif

/*
 * Define macro, XPC_MSG_SIZE(), is provided for the user
 * that wants to fit as many msg entries as possible in a given memory size
 * (e.g. a memory page).
 */
#define XPC_MSG_MAX_SIZE	128
#define XPC_MSG_HDR_MAX_SIZE	16
#define XPC_MSG_PAYLOAD_MAX_SIZE (XPC_MSG_MAX_SIZE - XPC_MSG_HDR_MAX_SIZE)

#define XPC_MSG_SIZE(_payload_size) \
				ALIGN(XPC_MSG_HDR_MAX_SIZE + (_payload_size), \
				      is_uv() ? 64 : 128)


/*
 * Define the return values and values passed to user's callout functions.
 * (It is important to add new value codes at the end just preceding
 * xpUnknownReason, which must have the highest numerical value.)
 */
enum xp_retval {
	xpSuccess = 0,

	xpNotConnected,		/*  1: channel is not connected */
	xpConnected,		/*  2: channel connected (opened) */
	xpRETIRED1,		/*  3: (formerly xpDisconnected) */

	xpMsgReceived,		/*  4: message received */
	xpMsgDelivered,		/*  5: message delivered and acknowledged */

	xpRETIRED2,		/*  6: (formerly xpTransferFailed) */

	xpNoWait,		/*  7: operation would require wait */
	xpRetry,		/*  8: retry operation */
	xpTimeout,		/*  9: timeout in xpc_allocate_msg_wait() */
	xpInterrupted,		/* 10: interrupted wait */

	xpUnequalMsgSizes,	/* 11: message size disparity between sides */
	xpInvalidAddress,	/* 12: invalid address */

	xpNoMemory,		/* 13: no memory available for XPC structures */
	xpLackOfResources,	/* 14: insufficient resources for operation */
	xpUnregistered,		/* 15: channel is not registered */
	xpAlreadyRegistered,	/* 16: channel is already registered */

	xpPartitionDown,	/* 17: remote partition is down */
	xpNotLoaded,		/* 18: XPC module is not loaded */
	xpUnloading,		/* 19: this side is unloading XPC module */

	xpBadMagic,		/* 20: XPC MAGIC string not found */

	xpReactivating,		/* 21: remote partition was reactivated */

	xpUnregistering,	/* 22: this side is unregistering channel */
	xpOtherUnregistering,	/* 23: other side is unregistering channel */

	xpCloneKThread,		/* 24: cloning kernel thread */
	xpCloneKThreadFailed,	/* 25: cloning kernel thread failed */

	xpNoHeartbeat,		/* 26: remote partition has no heartbeat */

	xpPioReadError,		/* 27: PIO read error */
	xpPhysAddrRegFailed,	/* 28: registration of phys addr range failed */

	xpRETIRED3,		/* 29: (formerly xpBteDirectoryError) */
	xpRETIRED4,		/* 30: (formerly xpBtePoisonError) */
	xpRETIRED5,		/* 31: (formerly xpBteWriteError) */
	xpRETIRED6,		/* 32: (formerly xpBteAccessError) */
	xpRETIRED7,		/* 33: (formerly xpBtePWriteError) */
	xpRETIRED8,		/* 34: (formerly xpBtePReadError) */
	xpRETIRED9,		/* 35: (formerly xpBteTimeOutError) */
	xpRETIRED10,		/* 36: (formerly xpBteXtalkError) */
	xpRETIRED11,		/* 37: (formerly xpBteNotAvailable) */
	xpRETIRED12,		/* 38: (formerly xpBteUnmappedError) */

	xpBadVersion,		/* 39: bad version number */
	xpVarsNotSet,		/* 40: the XPC variables are not set up */
	xpNoRsvdPageAddr,	/* 41: unable to get rsvd page's phys addr */
	xpInvalidPartid,	/* 42: invalid partition ID */
	xpLocalPartid,		/* 43: local partition ID */

	xpOtherGoingDown,	/* 44: other side going down, reason unknown */
	xpSystemGoingDown,	/* 45: system is going down, reason unknown */
	xpSystemHalt,		/* 46: system is being halted */
	xpSystemReboot,		/* 47: system is being rebooted */
	xpSystemPoweroff,	/* 48: system is being powered off */

	xpDisconnecting,	/* 49: channel disconnecting (closing) */

	xpOpenCloseError,	/* 50: channel open/close protocol error */

	xpDisconnected,		/* 51: channel disconnected (closed) */

	xpBteCopyError,		/* 52: bte_copy() returned error */
	xpSalError,		/* 53: sn SAL error */
	xpRsvdPageNotSet,	/* 54: the reserved page is not set up */
	xpPayloadTooBig,	/* 55: payload too large for message slot */

	xpUnsupported,		/* 56: unsupported functionality or resource */
	xpNeedMoreInfo,		/* 57: more info is needed by SAL */

	xpGruCopyError,		/* 58: gru_copy_gru() returned error */
	xpGruSendMqError,	/* 59: gru send message queue related error */

	xpBadChannelNumber,	/* 60: invalid channel number */
	xpBadMsgType,		/* 60: invalid message type */

	xpUnknownReason		/* 61: unknown reason - must be last in enum */
};

/*
 * Define the callout function type used by XPC to update the user on
 * connection activity and state changes via the user function registered
 * by xpc_connect().
 *
 * Arguments:
 *
 *	reason - reason code.
 *	partid - partition ID associated with condition.
 *	ch_number - channel # associated with condition.
 *	data - pointer to optional data.
 *	key - pointer to optional user-defined value provided as the "key"
 *	      argument to xpc_connect().
 *
 * A reason code of xpConnected indicates that a connection has been
 * established to the specified partition on the specified channel. The data
 * argument indicates the max number of entries allowed in the message queue.
 *
 * A reason code of xpMsgReceived indicates that a XPC message arrived from
 * the specified partition on the specified channel. The data argument
 * specifies the address of the message's payload. The user must call
 * xpc_received() when finished with the payload.
 *
 * All other reason codes indicate failure. The data argmument is NULL.
 * When a failure reason code is received, one can assume that the channel
 * is not connected.
 */
typedef void (*xpc_channel_func) (enum xp_retval reason, short partid,
				  int ch_number, void *data, void *key);

/*
 * Define the callout function type used by XPC to notify the user of
 * messages received and delivered via the user function registered by
 * xpc_send_notify().
 *
 * Arguments:
 *
 *	reason - reason code.
 *	partid - partition ID associated with condition.
 *	ch_number - channel # associated with condition.
 *	key - pointer to optional user-defined value provided as the "key"
 *	      argument to xpc_send_notify().
 *
 * A reason code of xpMsgDelivered indicates that the message was delivered
 * to the intended recipient and that they have acknowledged its receipt by
 * calling xpc_received().
 *
 * All other reason codes indicate failure.
 *
 * NOTE: The user defined function must be callable by an interrupt handler
 *       and thus cannot block.
 */
typedef void (*xpc_notify_func) (enum xp_retval reason, short partid,
				 int ch_number, void *key);

/*
 * The following is a registration entry. There is a global array of these,
 * one per channel. It is used to record the connection registration made
 * by the users of XPC. As long as a registration entry exists, for any
 * partition that comes up, XPC will attempt to establish a connection on
 * that channel. Notification that a connection has been made will occur via
 * the xpc_channel_func function.
 *
 * The 'func' field points to the function to call when aynchronous
 * notification is required for such events as: a connection established/lost,
 * or an incoming message received, or an error condition encountered. A
 * non-NULL 'func' field indicates that there is an active registration for
 * the channel.
 */
struct xpc_registration {
	struct mutex mutex;
	xpc_channel_func func;	/* function to call */
	void *key;		/* pointer to user's key */
	u16 nentries;		/* #of msg entries in local msg queue */
	u16 entry_size;		/* message queue's message entry size */
	u32 assigned_limit;	/* limit on #of assigned kthreads */
	u32 idle_limit;		/* limit on #of idle kthreads */
} ____cacheline_aligned;

#define XPC_CHANNEL_REGISTERED(_c)	(xpc_registrations[_c].func != NULL)

/* the following are valid xpc_send() or xpc_send_notify() flags */
#define XPC_WAIT	0	/* wait flag */
#define XPC_NOWAIT	1	/* no wait flag */

struct xpc_interface {
	void (*connect) (int);
	void (*disconnect) (int);
	enum xp_retval (*send) (short, int, u32, void *, u16);
	enum xp_retval (*send_notify) (short, int, u32, void *, u16,
					xpc_notify_func, void *);
	void (*received) (short, int, void *);
	enum xp_retval (*partid_to_nasids) (short, void *);
};

extern struct xpc_interface xpc_interface;

extern void xpc_set_interface(void (*)(int),
			      void (*)(int),
			      enum xp_retval (*)(short, int, u32, void *, u16),
			      enum xp_retval (*)(short, int, u32, void *, u16,
						 xpc_notify_func, void *),
			      void (*)(short, int, void *),
			      enum xp_retval (*)(short, void *));
extern void xpc_clear_interface(void);

extern enum xp_retval xpc_connect(int, xpc_channel_func, void *, u16,
				   u16, u32, u32);
extern void xpc_disconnect(int);

static inline enum xp_retval
xpc_send(short partid, int ch_number, u32 flags, void *payload,
	 u16 payload_size)
{
	return xpc_interface.send(partid, ch_number, flags, payload,
				  payload_size);
}

static inline enum xp_retval
xpc_send_notify(short partid, int ch_number, u32 flags, void *payload,
		u16 payload_size, xpc_notify_func func, void *key)
{
	return xpc_interface.send_notify(partid, ch_number, flags, payload,
					 payload_size, func, key);
}

static inline void
xpc_received(short partid, int ch_number, void *payload)
{
	return xpc_interface.received(partid, ch_number, payload);
}

static inline enum xp_retval
xpc_partid_to_nasids(short partid, void *nasids)
{
	return xpc_interface.partid_to_nasids(partid, nasids);
}

extern short xp_max_npartitions;
extern short xp_partition_id;
extern u8 xp_region_size;

extern unsigned long (*xp_pa) (void *);
extern enum xp_retval (*xp_remote_memcpy) (unsigned long, const unsigned long,
		       size_t);
extern int (*xp_cpu_to_nasid) (int);

extern u64 xp_nofault_PIOR_target;
extern int xp_nofault_PIOR(void *);
extern int xp_error_PIOR(void);

extern struct device *xp;
extern enum xp_retval xp_init_sn2(void);
extern enum xp_retval xp_init_uv(void);
extern void xp_exit_sn2(void);
extern void xp_exit_uv(void);

#endif /* _DRIVERS_MISC_SGIXP_XP_H */
