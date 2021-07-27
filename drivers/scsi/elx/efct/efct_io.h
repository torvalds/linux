/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#if !defined(__EFCT_IO_H__)
#define __EFCT_IO_H__

#include "efct_lio.h"

#define EFCT_LOG_ENABLE_IO_ERRORS(efct)		\
		(((efct) != NULL) ? (((efct)->logmask & (1U << 6)) != 0) : 0)

#define io_error_log(io, fmt, ...)  \
	do { \
		if (EFCT_LOG_ENABLE_IO_ERRORS(io->efct)) \
			efc_log_warn(io->efct, fmt, ##__VA_ARGS__); \
	} while (0)

#define SCSI_CMD_BUF_LENGTH	48
#define SCSI_RSP_BUF_LENGTH	(FCP_RESP_WITH_EXT + SCSI_SENSE_BUFFERSIZE)
#define EFCT_NUM_SCSI_IOS	8192

enum efct_io_type {
	EFCT_IO_TYPE_IO = 0,
	EFCT_IO_TYPE_ELS,
	EFCT_IO_TYPE_CT,
	EFCT_IO_TYPE_CT_RESP,
	EFCT_IO_TYPE_BLS_RESP,
	EFCT_IO_TYPE_ABORT,

	EFCT_IO_TYPE_MAX,
};

enum efct_els_state {
	EFCT_ELS_REQUEST = 0,
	EFCT_ELS_REQUEST_DELAYED,
	EFCT_ELS_REQUEST_DELAY_ABORT,
	EFCT_ELS_REQ_ABORT,
	EFCT_ELS_REQ_ABORTED,
	EFCT_ELS_ABORT_IO_COMPL,
};

/**
 * Scsi target IO object
 * @efct:		pointer back to efct
 * @instance_index:	unique instance index value
 * @io:			IO display name
 * @node:		pointer to node
 * @list_entry:		io list entry
 * @io_pending_link:	io pending list entry
 * @ref:		reference counter
 * @release:		release callback function
 * @init_task_tag:	initiator task tag (OX_ID) for back-end and SCSI logging
 * @tgt_task_tag:	target task tag (RX_ID) for back-end and SCSI logging
 * @hw_tag:		HW layer unique IO id
 * @tag:		unique IO identifier
 * @sgl:		SGL
 * @sgl_allocated:	Number of allocated SGEs
 * @sgl_count:		Number of SGEs in this SGL
 * @tgt_io:		backend target private IO data
 * @exp_xfer_len:	expected data transfer length, based on FC header
 * @hw_priv:		Declarations private to HW/SLI
 * @io_type:		indicates what this struct efct_io structure is used for
 * @hio:		hw io object
 * @transferred:	Number of bytes transferred
 * @auto_resp:		set if auto_trsp was set
 * @low_latency:	set if low latency request
 * @wq_steering:	selected WQ steering request
 * @wq_class:		selected WQ class if steering is class
 * @xfer_req:		transfer size for current request
 * @scsi_tgt_cb:	target callback function
 * @scsi_tgt_cb_arg:	target callback function argument
 * @abort_cb:		abort callback function
 * @abort_cb_arg:	abort callback function argument
 * @bls_cb:		BLS callback function
 * @bls_cb_arg:		BLS callback function argument
 * @tmf_cmd:		TMF command being processed
 * @abort_rx_id:	rx_id from the ABTS that initiated the command abort
 * @cmd_tgt:		True if this is a Target command
 * @send_abts:		when aborting, indicates ABTS is to be sent
 * @cmd_ini:		True if this is an Initiator command
 * @seq_init:		True if local node has sequence initiative
 * @iparam:		iparams for hw io send call
 * @hio_type:		HW IO type
 * @wire_len:		wire length
 * @hw_cb:		saved HW callback
 * @io_to_abort:	for abort handling, pointer to IO to abort
 * @rspbuf:		SCSI Response buffer
 * @timeout:		Timeout value in seconds for this IO
 * @cs_ctl:		CS_CTL priority for this IO
 * @io_free:		Is io object in freelist
 * @app_id:		application id
 */
struct efct_io {
	struct efct		*efct;
	u32			instance_index;
	const char		*display_name;
	struct efct_node	*node;

	struct list_head	list_entry;
	struct list_head	io_pending_link;
	struct kref		ref;
	void (*release)(struct kref *arg);
	u32			init_task_tag;
	u32			tgt_task_tag;
	u32			hw_tag;
	u32			tag;
	struct efct_scsi_sgl	*sgl;
	u32			sgl_allocated;
	u32			sgl_count;
	struct efct_scsi_tgt_io tgt_io;
	u32			exp_xfer_len;

	void			*hw_priv;

	enum efct_io_type	io_type;
	struct efct_hw_io	*hio;
	size_t			transferred;

	bool			auto_resp;
	bool			low_latency;
	u8			wq_steering;
	u8			wq_class;
	u64			xfer_req;
	efct_scsi_io_cb_t	scsi_tgt_cb;
	void			*scsi_tgt_cb_arg;
	efct_scsi_io_cb_t	abort_cb;
	void			*abort_cb_arg;
	efct_scsi_io_cb_t	bls_cb;
	void			*bls_cb_arg;
	enum efct_scsi_tmf_cmd	tmf_cmd;
	u16			abort_rx_id;

	bool			cmd_tgt;
	bool			send_abts;
	bool			cmd_ini;
	bool			seq_init;
	union efct_hw_io_param_u iparam;
	enum efct_hw_io_type	hio_type;
	u64			wire_len;
	void			*hw_cb;

	struct efct_io		*io_to_abort;

	struct efc_dma		rspbuf;
	u32			timeout;
	u8			cs_ctl;
	u8			io_free;
	u32			app_id;
};

struct efct_io_cb_arg {
	int status;
	int ext_status;
	void *app;
};

struct efct_io_pool *
efct_io_pool_create(struct efct *efct, u32 num_sgl);
int
efct_io_pool_free(struct efct_io_pool *io_pool);
u32
efct_io_pool_allocated(struct efct_io_pool *io_pool);

struct efct_io *
efct_io_pool_io_alloc(struct efct_io_pool *io_pool);
void
efct_io_pool_io_free(struct efct_io_pool *io_pool, struct efct_io *io);
struct efct_io *
efct_io_find_tgt_io(struct efct *efct, struct efct_node *node,
		    u16 ox_id, u16 rx_id);
#endif /* __EFCT_IO_H__ */
