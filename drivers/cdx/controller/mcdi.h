/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2008-2013 Solarflare Communications Inc.
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef CDX_MCDI_H
#define CDX_MCDI_H

#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/rpmsg.h>

#include "bitfield.h"
#include "mc_cdx_pcol.h"

#ifdef DEBUG
#define CDX_WARN_ON_ONCE_PARANOID(x) WARN_ON_ONCE(x)
#define CDX_WARN_ON_PARANOID(x) WARN_ON(x)
#else
#define CDX_WARN_ON_ONCE_PARANOID(x) do {} while (0)
#define CDX_WARN_ON_PARANOID(x) do {} while (0)
#endif

/**
 * enum cdx_mcdi_mode - MCDI transaction mode
 * @MCDI_MODE_EVENTS: wait for an mcdi response callback.
 * @MCDI_MODE_FAIL: we think MCDI is dead, so fail-fast all calls
 */
enum cdx_mcdi_mode {
	MCDI_MODE_EVENTS,
	MCDI_MODE_FAIL,
};

#define MCDI_RPC_TIMEOUT	(10 * HZ)
#define MCDI_RPC_LONG_TIMEOU	(60 * HZ)
#define MCDI_RPC_POST_RST_TIME	(10 * HZ)

#define MCDI_BUF_LEN (8 + MCDI_CTL_SDU_LEN_MAX)

/**
 * enum cdx_mcdi_cmd_state - State for an individual MCDI command
 * @MCDI_STATE_QUEUED: Command not started and is waiting to run.
 * @MCDI_STATE_RETRY: Command was submitted and MC rejected with no resources,
 *	as MC have too many outstanding commands. Command will be retried once
 *	another command returns.
 * @MCDI_STATE_RUNNING: Command was accepted and is running.
 * @MCDI_STATE_RUNNING_CANCELLED: Command is running but the issuer cancelled
 *	the command.
 * @MCDI_STATE_FINISHED: Processing of this command has completed.
 */

enum cdx_mcdi_cmd_state {
	MCDI_STATE_QUEUED,
	MCDI_STATE_RETRY,
	MCDI_STATE_RUNNING,
	MCDI_STATE_RUNNING_CANCELLED,
	MCDI_STATE_FINISHED,
};

/**
 * struct cdx_mcdi - CDX MCDI Firmware interface, to interact
 *	with CDX controller.
 * @mcdi: MCDI interface
 * @mcdi_ops: MCDI operations
 * @r5_rproc : R5 Remoteproc device handle
 * @rpdev: RPMsg device
 * @ept: RPMsg endpoint
 * @work: Post probe work
 */
struct cdx_mcdi {
	/* MCDI interface */
	struct cdx_mcdi_data *mcdi;
	const struct cdx_mcdi_ops *mcdi_ops;

	struct rproc *r5_rproc;
	struct rpmsg_device *rpdev;
	struct rpmsg_endpoint *ept;
	struct work_struct work;
};

struct cdx_mcdi_ops {
	void (*mcdi_request)(struct cdx_mcdi *cdx,
			     const struct cdx_dword *hdr, size_t hdr_len,
			     const struct cdx_dword *sdu, size_t sdu_len);
	unsigned int (*mcdi_rpc_timeout)(struct cdx_mcdi *cdx, unsigned int cmd);
};

typedef void cdx_mcdi_async_completer(struct cdx_mcdi *cdx,
				      unsigned long cookie, int rc,
				      struct cdx_dword *outbuf,
				      size_t outlen_actual);

/**
 * struct cdx_mcdi_cmd - An outstanding MCDI command
 * @ref: Reference count. There will be one reference if the command is
 *	in the mcdi_iface cmd_list, another if it's on a cleanup list,
 *	and a third if it's queued in the work queue.
 * @list: The data for this entry in mcdi->cmd_list
 * @cleanup_list: The data for this entry in a cleanup list
 * @work: The work item for this command, queued in mcdi->workqueue
 * @mcdi: The mcdi_iface for this command
 * @state: The state of this command
 * @inlen: inbuf length
 * @inbuf: Input buffer
 * @quiet: Whether to silence errors
 * @reboot_seen: Whether a reboot has been seen during this command,
 *	to prevent duplicates
 * @seq: Sequence number
 * @started: Jiffies this command was started at
 * @cookie: Context for completion function
 * @completer: Completion function
 * @handle: Command handle
 * @cmd: Command number
 * @rc: Return code
 * @outlen: Length of output buffer
 * @outbuf: Output buffer
 */
struct cdx_mcdi_cmd {
	struct kref ref;
	struct list_head list;
	struct list_head cleanup_list;
	struct work_struct work;
	struct cdx_mcdi_iface *mcdi;
	enum cdx_mcdi_cmd_state state;
	size_t inlen;
	const struct cdx_dword *inbuf;
	bool quiet;
	bool reboot_seen;
	u8 seq;
	unsigned long started;
	unsigned long cookie;
	cdx_mcdi_async_completer *completer;
	unsigned int handle;
	unsigned int cmd;
	int rc;
	size_t outlen;
	struct cdx_dword *outbuf;
	/* followed by inbuf data if necessary */
};

/**
 * struct cdx_mcdi_iface - MCDI protocol context
 * @cdx: The associated NIC
 * @iface_lock: Serialise access to this structure
 * @outstanding_cleanups: Count of cleanups
 * @cmd_list: List of outstanding and running commands
 * @workqueue: Workqueue used for delayed processing
 * @cmd_complete_wq: Waitqueue for command completion
 * @db_held_by: Command the MC doorbell is in use by
 * @seq_held_by: Command each sequence number is in use by
 * @prev_handle: The last used command handle
 * @mode: Poll for mcdi completion, or wait for an mcdi_event
 * @prev_seq: The last used sequence number
 * @new_epoch: Indicates start of day or start of MC reboot recovery
 */
struct cdx_mcdi_iface {
	struct cdx_mcdi *cdx;
	/* Serialise access */
	struct mutex iface_lock;
	unsigned int outstanding_cleanups;
	struct list_head cmd_list;
	struct workqueue_struct *workqueue;
	wait_queue_head_t cmd_complete_wq;
	struct cdx_mcdi_cmd *db_held_by;
	struct cdx_mcdi_cmd *seq_held_by[16];
	unsigned int prev_handle;
	enum cdx_mcdi_mode mode;
	u8 prev_seq;
	bool new_epoch;
};

/**
 * struct cdx_mcdi_data - extra state for NICs that implement MCDI
 * @iface: Interface/protocol state
 * @fn_flags: Flags for this function, as returned by %MC_CMD_DRV_ATTACH.
 */
struct cdx_mcdi_data {
	struct cdx_mcdi_iface iface;
	u32 fn_flags;
};

static inline struct cdx_mcdi_iface *cdx_mcdi_if(struct cdx_mcdi *cdx)
{
	return cdx->mcdi ? &cdx->mcdi->iface : NULL;
}

int cdx_mcdi_init(struct cdx_mcdi *cdx);
void cdx_mcdi_finish(struct cdx_mcdi *cdx);

void cdx_mcdi_process_cmd(struct cdx_mcdi *cdx, struct cdx_dword *outbuf, int len);
int cdx_mcdi_rpc(struct cdx_mcdi *cdx, unsigned int cmd,
		 const struct cdx_dword *inbuf, size_t inlen,
		 struct cdx_dword *outbuf, size_t outlen, size_t *outlen_actual);
int cdx_mcdi_rpc_async(struct cdx_mcdi *cdx, unsigned int cmd,
		       const struct cdx_dword *inbuf, size_t inlen,
		       cdx_mcdi_async_completer *complete,
		       unsigned long cookie);
int cdx_mcdi_wait_for_quiescence(struct cdx_mcdi *cdx,
				 unsigned int timeout_jiffies);

/*
 * We expect that 16- and 32-bit fields in MCDI requests and responses
 * are appropriately aligned, but 64-bit fields are only
 * 32-bit-aligned.
 */
#define MCDI_DECLARE_BUF(_name, _len) struct cdx_dword _name[DIV_ROUND_UP(_len, 4)] = {{0}}
#define _MCDI_PTR(_buf, _offset)					\
	((u8 *)(_buf) + (_offset))
#define MCDI_PTR(_buf, _field)						\
	_MCDI_PTR(_buf, MC_CMD_ ## _field ## _OFST)
#define _MCDI_CHECK_ALIGN(_ofst, _align)				\
	((void)BUILD_BUG_ON_ZERO((_ofst) & ((_align) - 1)),		\
	 (_ofst))
#define _MCDI_DWORD(_buf, _field)					\
	((_buf) + (_MCDI_CHECK_ALIGN(MC_CMD_ ## _field ## _OFST, 4) >> 2))

#define MCDI_BYTE(_buf, _field)						\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 1),	\
	 *MCDI_PTR(_buf, _field))
#define MCDI_WORD(_buf, _field)						\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 2),	\
	 le16_to_cpu(*(__force const __le16 *)MCDI_PTR(_buf, _field)))
#define MCDI_SET_DWORD(_buf, _field, _value)				\
	CDX_POPULATE_DWORD_1(*_MCDI_DWORD(_buf, _field), CDX_DWORD, _value)
#define MCDI_DWORD(_buf, _field)					\
	CDX_DWORD_FIELD(*_MCDI_DWORD(_buf, _field), CDX_DWORD)
#define MCDI_POPULATE_DWORD_1(_buf, _field, _name1, _value1)		\
	CDX_POPULATE_DWORD_1(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1)
#define MCDI_SET_QWORD(_buf, _field, _value)				\
	do {								\
		CDX_POPULATE_DWORD_1(_MCDI_DWORD(_buf, _field)[0],	\
				     CDX_DWORD, (u32)(_value));	\
		CDX_POPULATE_DWORD_1(_MCDI_DWORD(_buf, _field)[1],	\
				     CDX_DWORD, (u64)(_value) >> 32);	\
	} while (0)
#define MCDI_QWORD(_buf, _field)					\
	(CDX_DWORD_FIELD(_MCDI_DWORD(_buf, _field)[0], CDX_DWORD) |	\
	(u64)CDX_DWORD_FIELD(_MCDI_DWORD(_buf, _field)[1], CDX_DWORD) << 32)

#endif /* CDX_MCDI_H */
