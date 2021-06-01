/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#if !defined(__EFCT_XPORT_H__)
#define __EFCT_XPORT_H__

enum efct_xport_ctrl {
	EFCT_XPORT_PORT_ONLINE = 1,
	EFCT_XPORT_PORT_OFFLINE,
	EFCT_XPORT_SHUTDOWN,
	EFCT_XPORT_POST_NODE_EVENT,
	EFCT_XPORT_WWNN_SET,
	EFCT_XPORT_WWPN_SET,
};

enum efct_xport_status {
	EFCT_XPORT_PORT_STATUS,
	EFCT_XPORT_CONFIG_PORT_STATUS,
	EFCT_XPORT_LINK_SPEED,
	EFCT_XPORT_IS_SUPPORTED_LINK_SPEED,
	EFCT_XPORT_LINK_STATISTICS,
	EFCT_XPORT_LINK_STAT_RESET,
	EFCT_XPORT_IS_QUIESCED
};

struct efct_xport_link_stats {
	bool		rec;
	bool		gec;
	bool		w02of;
	bool		w03of;
	bool		w04of;
	bool		w05of;
	bool		w06of;
	bool		w07of;
	bool		w08of;
	bool		w09of;
	bool		w10of;
	bool		w11of;
	bool		w12of;
	bool		w13of;
	bool		w14of;
	bool		w15of;
	bool		w16of;
	bool		w17of;
	bool		w18of;
	bool		w19of;
	bool		w20of;
	bool		w21of;
	bool		clrc;
	bool		clof1;
	u32		link_failure_error_count;
	u32		loss_of_sync_error_count;
	u32		loss_of_signal_error_count;
	u32		primitive_sequence_error_count;
	u32		invalid_transmission_word_error_count;
	u32		crc_error_count;
	u32		primitive_sequence_event_timeout_count;
	u32		elastic_buffer_overrun_error_count;
	u32		arbitration_fc_al_timeout_count;
	u32		advertised_receive_bufftor_to_buffer_credit;
	u32		current_receive_buffer_to_buffer_credit;
	u32		advertised_transmit_buffer_to_buffer_credit;
	u32		current_transmit_buffer_to_buffer_credit;
	u32		received_eofa_count;
	u32		received_eofdti_count;
	u32		received_eofni_count;
	u32		received_soff_count;
	u32		received_dropped_no_aer_count;
	u32		received_dropped_no_available_rpi_resources_count;
	u32		received_dropped_no_available_xri_resources_count;
};

struct efct_xport_host_stats {
	bool		cc;
	u32		transmit_kbyte_count;
	u32		receive_kbyte_count;
	u32		transmit_frame_count;
	u32		receive_frame_count;
	u32		transmit_sequence_count;
	u32		receive_sequence_count;
	u32		total_exchanges_originator;
	u32		total_exchanges_responder;
	u32		receive_p_bsy_count;
	u32		receive_f_bsy_count;
	u32		dropped_frames_due_to_no_rq_buffer_count;
	u32		empty_rq_timeout_count;
	u32		dropped_frames_due_to_no_xri_count;
	u32		empty_xri_pool_count;
};

struct efct_xport_host_statistics {
	struct completion		done;
	struct efct_xport_link_stats	link_stats;
	struct efct_xport_host_stats	host_stats;
};

union efct_xport_stats_u {
	u32	value;
	struct efct_xport_host_statistics stats;
};

struct efct_xport_fcp_stats {
	u64		input_bytes;
	u64		output_bytes;
	u64		input_requests;
	u64		output_requests;
	u64		control_requests;
};

struct efct_xport {
	struct efct		*efct;
	/* wwpn requested by user for primary nport */
	u64			req_wwpn;
	/* wwnn requested by user for primary nport */
	u64			req_wwnn;

	/* Nodes */
	/* number of allocated nodes */
	u32			nodes_count;
	/* used to track how often IO pool is empty */
	atomic_t		io_alloc_failed_count;
	/* array of pointers to nodes */
	struct efc_node		**nodes;

	/* Io pool and counts */
	/* pointer to IO pool */
	struct efct_io_pool	*io_pool;
	/* lock for io_pending_list */
	spinlock_t		io_pending_lock;
	/* list of IOs waiting for HW resources
	 *  lock: xport->io_pending_lock
	 *  link: efct_io_s->io_pending_link
	 */
	struct list_head	io_pending_list;
	/* count of totals IOS allocated */
	atomic_t		io_total_alloc;
	/* count of totals IOS free'd */
	atomic_t		io_total_free;
	/* count of totals IOS that were pended */
	atomic_t		io_total_pending;
	/* count of active IOS */
	atomic_t		io_active_count;
	/* count of pending IOS */
	atomic_t		io_pending_count;
	/* non-zero if efct_scsi_check_pending is executing */
	atomic_t		io_pending_recursing;

	/* Port */
	/* requested link state */
	u32			configured_link_state;

	/* Timer for Statistics */
	struct timer_list	stats_timer;
	union efct_xport_stats_u fc_xport_stats;
	struct efct_xport_fcp_stats fcp_stats;
};

struct efct_rport_data {
	struct efc_node		*node;
};

struct efct_xport *
efct_xport_alloc(struct efct *efct);
int
efct_xport_attach(struct efct_xport *xport);
int
efct_xport_initialize(struct efct_xport *xport);
void
efct_xport_detach(struct efct_xport *xport);
int
efct_xport_control(struct efct_xport *xport, enum efct_xport_ctrl cmd, ...);
int
efct_xport_status(struct efct_xport *xport, enum efct_xport_status cmd,
		  union efct_xport_stats_u *result);
void
efct_xport_free(struct efct_xport *xport);

struct scsi_transport_template *efct_attach_fc_transport(void);
struct scsi_transport_template *efct_attach_vport_fc_transport(void);
void
efct_release_fc_transport(struct scsi_transport_template *transport_template);

#endif /* __EFCT_XPORT_H__ */
