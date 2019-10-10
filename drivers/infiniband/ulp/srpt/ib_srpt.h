/*
 * Copyright (c) 2006 - 2009 Mellanox Technology Inc.  All rights reserved.
 * Copyright (C) 2009 - 2010 Bart Van Assche <bvanassche@acm.org>.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef IB_SRPT_H
#define IB_SRPT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/wait.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_sa.h>
#include <rdma/ib_cm.h>
#include <rdma/rdma_cm.h>
#include <rdma/rw.h>

#include <scsi/srp.h>

#include "ib_dm_mad.h"

/*
 * The prefix the ServiceName field must start with in the device management
 * ServiceEntries attribute pair. See also the SRP specification.
 */
#define SRP_SERVICE_NAME_PREFIX		"SRP.T10:"

struct srpt_nexus;

enum {
	/*
	 * SRP IOControllerProfile attributes for SRP target ports that have
	 * not been defined in <scsi/srp.h>. Source: section B.7, table B.7
	 * in the SRP specification.
	 */
	SRP_PROTOCOL = 0x0108,
	SRP_PROTOCOL_VERSION = 0x0001,
	SRP_IO_SUBCLASS = 0x609e,
	SRP_SEND_TO_IOC = 0x01,
	SRP_SEND_FROM_IOC = 0x02,
	SRP_RDMA_READ_FROM_IOC = 0x08,
	SRP_RDMA_WRITE_FROM_IOC = 0x20,

	/*
	 * srp_login_cmd.req_flags bitmasks. See also table 9 in the SRP
	 * specification.
	 */
	SRP_MTCH_ACTION = 0x03, /* MULTI-CHANNEL ACTION */
	SRP_LOSOLNT = 0x10, /* logout solicited notification */
	SRP_CRSOLNT = 0x20, /* credit request solicited notification */
	SRP_AESOLNT = 0x40, /* asynchronous event solicited notification */

	/*
	 * srp_cmd.sol_nt / srp_tsk_mgmt.sol_not bitmasks. See also tables
	 * 18 and 20 in the SRP specification.
	 */
	SRP_SCSOLNT = 0x02, /* SCSOLNT = successful solicited notification */
	SRP_UCSOLNT = 0x04, /* UCSOLNT = unsuccessful solicited notification */

	/*
	 * srp_rsp.sol_not / srp_t_logout.sol_not bitmasks. See also tables
	 * 16 and 22 in the SRP specification.
	 */
	SRP_SOLNT = 0x01, /* SOLNT = solicited notification */

	/* See also table 24 in the SRP specification. */
	SRP_TSK_MGMT_SUCCESS = 0x00,
	SRP_TSK_MGMT_FUNC_NOT_SUPP = 0x04,
	SRP_TSK_MGMT_FAILED = 0x05,

	/* See also table 21 in the SRP specification. */
	SRP_CMD_SIMPLE_Q = 0x0,
	SRP_CMD_HEAD_OF_Q = 0x1,
	SRP_CMD_ORDERED_Q = 0x2,
	SRP_CMD_ACA = 0x4,

	SRPT_DEF_SG_TABLESIZE = 128,
	/*
	 * An experimentally determined value that avoids that QP creation
	 * fails due to "swiotlb buffer is full" on systems using the swiotlb.
	 */
	SRPT_MAX_SG_PER_WQE = 16,

	MIN_SRPT_SQ_SIZE = 16,
	DEF_SRPT_SQ_SIZE = 4096,
	MAX_SRPT_RQ_SIZE = 128,
	MIN_SRPT_SRQ_SIZE = 4,
	DEFAULT_SRPT_SRQ_SIZE = 4095,
	MAX_SRPT_SRQ_SIZE = 65535,
	MAX_SRPT_RDMA_SIZE = 1U << 24,
	MAX_SRPT_RSP_SIZE = 1024,

	SRP_MAX_ADD_CDB_LEN = 16,
	SRP_MAX_IMM_DATA_OFFSET = 80,
	SRP_MAX_IMM_DATA = 8 * 1024,
	MIN_MAX_REQ_SIZE = 996,
	DEFAULT_MAX_REQ_SIZE_1 = sizeof(struct srp_cmd)/*48*/ +
				 SRP_MAX_ADD_CDB_LEN +
				 sizeof(struct srp_indirect_buf)/*20*/ +
				 128 * sizeof(struct srp_direct_buf)/*16*/,
	DEFAULT_MAX_REQ_SIZE_2 = SRP_MAX_IMM_DATA_OFFSET +
				 sizeof(struct srp_imm_buf) + SRP_MAX_IMM_DATA,
	DEFAULT_MAX_REQ_SIZE = DEFAULT_MAX_REQ_SIZE_1 > DEFAULT_MAX_REQ_SIZE_2 ?
			       DEFAULT_MAX_REQ_SIZE_1 : DEFAULT_MAX_REQ_SIZE_2,

	MIN_MAX_RSP_SIZE = sizeof(struct srp_rsp)/*36*/ + 4,
	DEFAULT_MAX_RSP_SIZE = 256, /* leaves 220 bytes for sense data */

	DEFAULT_MAX_RDMA_SIZE = 65536,
};

/**
 * enum srpt_command_state - SCSI command state managed by SRPT
 * @SRPT_STATE_NEW:           New command arrived and is being processed.
 * @SRPT_STATE_NEED_DATA:     Processing a write or bidir command and waiting
 *                            for data arrival.
 * @SRPT_STATE_DATA_IN:       Data for the write or bidir command arrived and is
 *                            being processed.
 * @SRPT_STATE_CMD_RSP_SENT:  SRP_RSP for SRP_CMD has been sent.
 * @SRPT_STATE_MGMT:          Processing a SCSI task management command.
 * @SRPT_STATE_MGMT_RSP_SENT: SRP_RSP for SRP_TSK_MGMT has been sent.
 * @SRPT_STATE_DONE:          Command processing finished successfully, command
 *                            processing has been aborted or command processing
 *                            failed.
 */
enum srpt_command_state {
	SRPT_STATE_NEW		 = 0,
	SRPT_STATE_NEED_DATA	 = 1,
	SRPT_STATE_DATA_IN	 = 2,
	SRPT_STATE_CMD_RSP_SENT	 = 3,
	SRPT_STATE_MGMT		 = 4,
	SRPT_STATE_MGMT_RSP_SENT = 5,
	SRPT_STATE_DONE		 = 6,
};

/**
 * struct srpt_ioctx - shared SRPT I/O context information
 * @cqe:   Completion queue element.
 * @buf:   Pointer to the buffer.
 * @dma:   DMA address of the buffer.
 * @offset: Offset of the first byte in @buf and @dma that is actually used.
 * @index: Index of the I/O context in its ioctx_ring array.
 */
struct srpt_ioctx {
	struct ib_cqe		cqe;
	void			*buf;
	dma_addr_t		dma;
	uint32_t		offset;
	uint32_t		index;
};

/**
 * struct srpt_recv_ioctx - SRPT receive I/O context
 * @ioctx:     See above.
 * @wait_list: Node for insertion in srpt_rdma_ch.cmd_wait_list.
 * @byte_len:  Number of bytes in @ioctx.buf.
 */
struct srpt_recv_ioctx {
	struct srpt_ioctx	ioctx;
	struct list_head	wait_list;
	int			byte_len;
};

struct srpt_rw_ctx {
	struct rdma_rw_ctx	rw;
	struct scatterlist	*sg;
	unsigned int		nents;
};

/**
 * struct srpt_send_ioctx - SRPT send I/O context
 * @ioctx:       See above.
 * @ch:          Channel pointer.
 * @recv_ioctx:  Receive I/O context associated with this send I/O context.
 *		 Only used for processing immediate data.
 * @s_rw_ctx:    @rw_ctxs points here if only a single rw_ctx is needed.
 * @rw_ctxs:     RDMA read/write contexts.
 * @imm_sg:      Scatterlist for immediate data.
 * @rdma_cqe:    RDMA completion queue element.
 * @state:       I/O context state.
 * @cmd:         Target core command data structure.
 * @sense_data:  SCSI sense data.
 * @n_rdma:      Number of work requests needed to transfer this ioctx.
 * @n_rw_ctx:    Size of rw_ctxs array.
 * @queue_status_only: Send a SCSI status back to the initiator but no data.
 * @sense_data:  Sense data to be sent to the initiator.
 */
struct srpt_send_ioctx {
	struct srpt_ioctx	ioctx;
	struct srpt_rdma_ch	*ch;
	struct srpt_recv_ioctx	*recv_ioctx;

	struct srpt_rw_ctx	s_rw_ctx;
	struct srpt_rw_ctx	*rw_ctxs;

	struct scatterlist	imm_sg;

	struct ib_cqe		rdma_cqe;
	enum srpt_command_state	state;
	struct se_cmd		cmd;
	u8			n_rdma;
	u8			n_rw_ctx;
	bool			queue_status_only;
	u8			sense_data[TRANSPORT_SENSE_BUFFER];
};

/**
 * enum rdma_ch_state - SRP channel state
 * @CH_CONNECTING:    QP is in RTR state; waiting for RTU.
 * @CH_LIVE:	      QP is in RTS state.
 * @CH_DISCONNECTING: DREQ has been sent and waiting for DREP or DREQ has
 *                    been received.
 * @CH_DRAINING:      DREP has been received or waiting for DREP timed out
 *                    and last work request has been queued.
 * @CH_DISCONNECTED:  Last completion has been received.
 */
enum rdma_ch_state {
	CH_CONNECTING,
	CH_LIVE,
	CH_DISCONNECTING,
	CH_DRAINING,
	CH_DISCONNECTED,
};

/**
 * struct srpt_rdma_ch - RDMA channel
 * @nexus:         I_T nexus this channel is associated with.
 * @qp:            IB queue pair used for communicating over this channel.
 * @ib_cm:	   See below.
 * @ib_cm.cm_id:   IB CM ID associated with the channel.
 * @rdma_cm:	   See below.
 * @rdma_cm.cm_id: RDMA CM ID associated with the channel.
 * @cq:            IB completion queue for this channel.
 * @zw_cqe:	   Zero-length write CQE.
 * @rcu:           RCU head.
 * @kref:	   kref for this channel.
 * @rq_size:       IB receive queue size.
 * @max_rsp_size:  Maximum size of an RSP response message in bytes.
 * @sq_wr_avail:   number of work requests available in the send queue.
 * @sport:         pointer to the information of the HCA port used by this
 *                 channel.
 * @max_ti_iu_len: maximum target-to-initiator information unit length.
 * @req_lim:       request limit: maximum number of requests that may be sent
 *                 by the initiator without having received a response.
 * @req_lim_delta: Number of credits not yet sent back to the initiator.
 * @imm_data_offset: Offset from start of SRP_CMD for immediate data.
 * @spinlock:      Protects free_list and state.
 * @state:         channel state. See also enum rdma_ch_state.
 * @using_rdma_cm: Whether the RDMA/CM or IB/CM is used for this channel.
 * @processing_wait_list: Whether or not cmd_wait_list is being processed.
 * @rsp_buf_cache: kmem_cache for @ioctx_ring.
 * @ioctx_ring:    Send ring.
 * @req_buf_cache: kmem_cache for @ioctx_recv_ring.
 * @ioctx_recv_ring: Receive I/O context ring.
 * @list:          Node in srpt_nexus.ch_list.
 * @cmd_wait_list: List of SCSI commands that arrived before the RTU event. This
 *                 list contains struct srpt_ioctx elements and is protected
 *                 against concurrent modification by the cm_id spinlock.
 * @pkey:          P_Key of the IB partition for this SRP channel.
 * @sess:          Session information associated with this SRP channel.
 * @sess_name:     Session name.
 * @release_work:  Allows scheduling of srpt_release_channel().
 */
struct srpt_rdma_ch {
	struct srpt_nexus	*nexus;
	struct ib_qp		*qp;
	union {
		struct {
			struct ib_cm_id		*cm_id;
		} ib_cm;
		struct {
			struct rdma_cm_id	*cm_id;
		} rdma_cm;
	};
	struct ib_cq		*cq;
	struct ib_cqe		zw_cqe;
	struct rcu_head		rcu;
	struct kref		kref;
	int			rq_size;
	u32			max_rsp_size;
	atomic_t		sq_wr_avail;
	struct srpt_port	*sport;
	int			max_ti_iu_len;
	atomic_t		req_lim;
	atomic_t		req_lim_delta;
	u16			imm_data_offset;
	spinlock_t		spinlock;
	enum rdma_ch_state	state;
	struct kmem_cache	*rsp_buf_cache;
	struct srpt_send_ioctx	**ioctx_ring;
	struct kmem_cache	*req_buf_cache;
	struct srpt_recv_ioctx	**ioctx_recv_ring;
	struct list_head	list;
	struct list_head	cmd_wait_list;
	uint16_t		pkey;
	bool			using_rdma_cm;
	bool			processing_wait_list;
	struct se_session	*sess;
	u8			sess_name[40];
	struct work_struct	release_work;
};

/**
 * struct srpt_nexus - I_T nexus
 * @rcu:       RCU head for this data structure.
 * @entry:     srpt_port.nexus_list list node.
 * @ch_list:   struct srpt_rdma_ch list. Protected by srpt_port.mutex.
 * @i_port_id: 128-bit initiator port identifier copied from SRP_LOGIN_REQ.
 * @t_port_id: 128-bit target port identifier copied from SRP_LOGIN_REQ.
 */
struct srpt_nexus {
	struct rcu_head		rcu;
	struct list_head	entry;
	struct list_head	ch_list;
	u8			i_port_id[16];
	u8			t_port_id[16];
};

/**
 * struct srpt_port_attib - attributes for SRPT port
 * @srp_max_rdma_size: Maximum size of SRP RDMA transfers for new connections.
 * @srp_max_rsp_size: Maximum size of SRP response messages in bytes.
 * @srp_sq_size: Shared receive queue (SRQ) size.
 * @use_srq: Whether or not to use SRQ.
 */
struct srpt_port_attrib {
	u32			srp_max_rdma_size;
	u32			srp_max_rsp_size;
	u32			srp_sq_size;
	bool			use_srq;
};

/**
 * struct srpt_port - information associated by SRPT with a single IB port
 * @sdev:      backpointer to the HCA information.
 * @mad_agent: per-port management datagram processing information.
 * @enabled:   Whether or not this target port is enabled.
 * @port_guid: ASCII representation of Port GUID
 * @port_gid:  ASCII representation of Port GID
 * @port:      one-based port number.
 * @sm_lid:    cached value of the port's sm_lid.
 * @lid:       cached value of the port's lid.
 * @gid:       cached value of the port's gid.
 * @port_acl_lock spinlock for port_acl_list:
 * @work:      work structure for refreshing the aforementioned cached values.
 * @port_guid_tpg: TPG associated with target port GUID.
 * @port_guid_wwn: WWN associated with target port GUID.
 * @port_gid_tpg:  TPG associated with target port GID.
 * @port_gid_wwn:  WWN associated with target port GID.
 * @port_attrib:   Port attributes that can be accessed through configfs.
 * @ch_releaseQ:   Enables waiting for removal from nexus_list.
 * @mutex:	   Protects nexus_list.
 * @nexus_list:	   Nexus list. See also srpt_nexus.entry.
 */
struct srpt_port {
	struct srpt_device	*sdev;
	struct ib_mad_agent	*mad_agent;
	bool			enabled;
	u8			port_guid[24];
	u8			port_gid[64];
	u8			port;
	u32			sm_lid;
	u32			lid;
	union ib_gid		gid;
	struct work_struct	work;
	struct se_portal_group	port_guid_tpg;
	struct se_wwn		port_guid_wwn;
	struct se_portal_group	port_gid_tpg;
	struct se_wwn		port_gid_wwn;
	struct srpt_port_attrib port_attrib;
	wait_queue_head_t	ch_releaseQ;
	struct mutex		mutex;
	struct list_head	nexus_list;
};

/**
 * struct srpt_device - information associated by SRPT with a single HCA
 * @device:        Backpointer to the struct ib_device managed by the IB core.
 * @pd:            IB protection domain.
 * @lkey:          L_Key (local key) with write access to all local memory.
 * @srq:           Per-HCA SRQ (shared receive queue).
 * @cm_id:         Connection identifier.
 * @srq_size:      SRQ size.
 * @sdev_mutex:	   Serializes use_srq changes.
 * @use_srq:       Whether or not to use SRQ.
 * @req_buf_cache: kmem_cache for @ioctx_ring buffers.
 * @ioctx_ring:    Per-HCA SRQ.
 * @event_handler: Per-HCA asynchronous IB event handler.
 * @list:          Node in srpt_dev_list.
 * @port:          Information about the ports owned by this HCA.
 */
struct srpt_device {
	struct ib_device	*device;
	struct ib_pd		*pd;
	u32			lkey;
	struct ib_srq		*srq;
	struct ib_cm_id		*cm_id;
	int			srq_size;
	struct mutex		sdev_mutex;
	bool			use_srq;
	struct kmem_cache	*req_buf_cache;
	struct srpt_recv_ioctx	**ioctx_ring;
	struct ib_event_handler	event_handler;
	struct list_head	list;
	struct srpt_port        port[];
};

#endif				/* IB_SRPT_H */
