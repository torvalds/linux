/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 * Copyright 2020 NXP
 */
#ifndef __FSL_DPNI_H
#define __FSL_DPNI_H

#include "dpkg.h"

struct fsl_mc_io;

/**
 * Data Path Network Interface API
 * Contains initialization APIs and runtime control APIs for DPNI
 */

/** General DPNI macros */

/**
 * Maximum number of traffic classes
 */
#define DPNI_MAX_TC				8
/**
 * Maximum number of buffer pools per DPNI
 */
#define DPNI_MAX_DPBP				8

/**
 * All traffic classes considered; see dpni_set_queue()
 */
#define DPNI_ALL_TCS				(u8)(-1)
/**
 * All flows within traffic class considered; see dpni_set_queue()
 */
#define DPNI_ALL_TC_FLOWS			(u16)(-1)
/**
 * Generate new flow ID; see dpni_set_queue()
 */
#define DPNI_NEW_FLOW_ID			(u16)(-1)

/**
 * Tx traffic is always released to a buffer pool on transmit, there are no
 * resources allocated to have the frames confirmed back to the source after
 * transmission.
 */
#define DPNI_OPT_TX_FRM_RELEASE			0x000001
/**
 * Disables support for MAC address filtering for addresses other than primary
 * MAC address. This affects both unicast and multicast. Promiscuous mode can
 * still be enabled/disabled for both unicast and multicast. If promiscuous mode
 * is disabled, only traffic matching the primary MAC address will be accepted.
 */
#define DPNI_OPT_NO_MAC_FILTER			0x000002
/**
 * Allocate policers for this DPNI. They can be used to rate-limit traffic per
 * traffic class (TC) basis.
 */
#define DPNI_OPT_HAS_POLICING			0x000004
/**
 * Congestion can be managed in several ways, allowing the buffer pool to
 * deplete on ingress, taildrop on each queue or use congestion groups for sets
 * of queues. If set, it configures a single congestion groups across all TCs.
 * If reset, a congestion group is allocated for each TC. Only relevant if the
 * DPNI has multiple traffic classes.
 */
#define DPNI_OPT_SHARED_CONGESTION		0x000008
/**
 * Enables TCAM for Flow Steering and QoS look-ups. If not specified, all
 * look-ups are exact match. Note that TCAM is not available on LS1088 and its
 * variants. Setting this bit on these SoCs will trigger an error.
 */
#define DPNI_OPT_HAS_KEY_MASKING		0x000010
/**
 * Disables the flow steering table.
 */
#define DPNI_OPT_NO_FS				0x000020
/**
 * Flow steering table is shared between all traffic classes
 */
#define DPNI_OPT_SHARED_FS			0x001000

int dpni_open(struct fsl_mc_io	*mc_io,
	      u32		cmd_flags,
	      int		dpni_id,
	      u16		*token);

int dpni_close(struct fsl_mc_io	*mc_io,
	       u32		cmd_flags,
	       u16		token);

/**
 * struct dpni_pools_cfg - Structure representing buffer pools configuration
 * @num_dpbp: Number of DPBPs
 * @pools: Array of buffer pools parameters; The number of valid entries
 *	must match 'num_dpbp' value
 * @pools.dpbp_id: DPBP object ID
 * @pools.buffer_size: Buffer size
 * @pools.backup_pool: Backup pool
 */
struct dpni_pools_cfg {
	u8		num_dpbp;
	struct {
		int	dpbp_id;
		u16	buffer_size;
		int	backup_pool;
	} pools[DPNI_MAX_DPBP];
};

int dpni_set_pools(struct fsl_mc_io		*mc_io,
		   u32				cmd_flags,
		   u16				token,
		   const struct dpni_pools_cfg	*cfg);

int dpni_enable(struct fsl_mc_io	*mc_io,
		u32			cmd_flags,
		u16			token);

int dpni_disable(struct fsl_mc_io	*mc_io,
		 u32			cmd_flags,
		 u16			token);

int dpni_is_enabled(struct fsl_mc_io	*mc_io,
		    u32			cmd_flags,
		    u16			token,
		    int			*en);

int dpni_reset(struct fsl_mc_io	*mc_io,
	       u32		cmd_flags,
	       u16		token);

/**
 * DPNI IRQ Index and Events
 */

/**
 * IRQ index
 */
#define DPNI_IRQ_INDEX				0
/**
 * IRQ events:
 *       indicates a change in link state
 *       indicates a change in endpoint
 */
#define DPNI_IRQ_EVENT_LINK_CHANGED		0x00000001
#define DPNI_IRQ_EVENT_ENDPOINT_CHANGED		0x00000002

int dpni_set_irq_enable(struct fsl_mc_io	*mc_io,
			u32			cmd_flags,
			u16			token,
			u8			irq_index,
			u8			en);

int dpni_get_irq_enable(struct fsl_mc_io	*mc_io,
			u32			cmd_flags,
			u16			token,
			u8			irq_index,
			u8			*en);

int dpni_set_irq_mask(struct fsl_mc_io	*mc_io,
		      u32		cmd_flags,
		      u16		token,
		      u8		irq_index,
		      u32		mask);

int dpni_get_irq_mask(struct fsl_mc_io	*mc_io,
		      u32		cmd_flags,
		      u16		token,
		      u8		irq_index,
		      u32		*mask);

int dpni_get_irq_status(struct fsl_mc_io	*mc_io,
			u32			cmd_flags,
			u16			token,
			u8			irq_index,
			u32			*status);

int dpni_clear_irq_status(struct fsl_mc_io	*mc_io,
			  u32			cmd_flags,
			  u16			token,
			  u8			irq_index,
			  u32			status);

/**
 * struct dpni_attr - Structure representing DPNI attributes
 * @options: Any combination of the following options:
 *		DPNI_OPT_TX_FRM_RELEASE
 *		DPNI_OPT_NO_MAC_FILTER
 *		DPNI_OPT_HAS_POLICING
 *		DPNI_OPT_SHARED_CONGESTION
 *		DPNI_OPT_HAS_KEY_MASKING
 *		DPNI_OPT_NO_FS
 * @num_queues: Number of Tx and Rx queues used for traffic distribution.
 * @num_tcs: Number of traffic classes (TCs), reserved for the DPNI.
 * @mac_filter_entries: Number of entries in the MAC address filtering table.
 * @vlan_filter_entries: Number of entries in the VLAN address filtering table.
 * @qos_entries: Number of entries in the QoS classification table.
 * @fs_entries: Number of entries in the flow steering table.
 * @qos_key_size: Size, in bytes, of the QoS look-up key. Defining a key larger
 *		than this when adding QoS entries will result in an error.
 * @fs_key_size: Size, in bytes, of the flow steering look-up key. Defining a
 *		key larger than this when composing the hash + FS key will
 *		result in an error.
 * @wriop_version: Version of WRIOP HW block. The 3 version values are stored
 *		on 6, 5, 5 bits respectively.
 */
struct dpni_attr {
	u32 options;
	u8 num_queues;
	u8 num_tcs;
	u8 mac_filter_entries;
	u8 vlan_filter_entries;
	u8 qos_entries;
	u16 fs_entries;
	u8 qos_key_size;
	u8 fs_key_size;
	u16 wriop_version;
};

int dpni_get_attributes(struct fsl_mc_io	*mc_io,
			u32			cmd_flags,
			u16			token,
			struct dpni_attr	*attr);

/**
 * DPNI errors
 */

/**
 * Extract out of frame header error
 */
#define DPNI_ERROR_EOFHE	0x00020000
/**
 * Frame length error
 */
#define DPNI_ERROR_FLE		0x00002000
/**
 * Frame physical error
 */
#define DPNI_ERROR_FPE		0x00001000
/**
 * Parsing header error
 */
#define DPNI_ERROR_PHE		0x00000020
/**
 * Parser L3 checksum error
 */
#define DPNI_ERROR_L3CE		0x00000004
/**
 * Parser L3 checksum error
 */
#define DPNI_ERROR_L4CE		0x00000001

/**
 * enum dpni_error_action - Defines DPNI behavior for errors
 * @DPNI_ERROR_ACTION_DISCARD: Discard the frame
 * @DPNI_ERROR_ACTION_CONTINUE: Continue with the normal flow
 * @DPNI_ERROR_ACTION_SEND_TO_ERROR_QUEUE: Send the frame to the error queue
 */
enum dpni_error_action {
	DPNI_ERROR_ACTION_DISCARD = 0,
	DPNI_ERROR_ACTION_CONTINUE = 1,
	DPNI_ERROR_ACTION_SEND_TO_ERROR_QUEUE = 2
};

/**
 * struct dpni_error_cfg - Structure representing DPNI errors treatment
 * @errors: Errors mask; use 'DPNI_ERROR__<X>
 * @error_action: The desired action for the errors mask
 * @set_frame_annotation: Set to '1' to mark the errors in frame annotation
 *		status (FAS); relevant only for the non-discard action
 */
struct dpni_error_cfg {
	u32			errors;
	enum dpni_error_action	error_action;
	int			set_frame_annotation;
};

int dpni_set_errors_behavior(struct fsl_mc_io		*mc_io,
			     u32			cmd_flags,
			     u16			token,
			     struct dpni_error_cfg	*cfg);

/**
 * DPNI buffer layout modification options
 */

/**
 * Select to modify the time-stamp setting
 */
#define DPNI_BUF_LAYOUT_OPT_TIMESTAMP		0x00000001
/**
 * Select to modify the parser-result setting; not applicable for Tx
 */
#define DPNI_BUF_LAYOUT_OPT_PARSER_RESULT	0x00000002
/**
 * Select to modify the frame-status setting
 */
#define DPNI_BUF_LAYOUT_OPT_FRAME_STATUS	0x00000004
/**
 * Select to modify the private-data-size setting
 */
#define DPNI_BUF_LAYOUT_OPT_PRIVATE_DATA_SIZE	0x00000008
/**
 * Select to modify the data-alignment setting
 */
#define DPNI_BUF_LAYOUT_OPT_DATA_ALIGN		0x00000010
/**
 * Select to modify the data-head-room setting
 */
#define DPNI_BUF_LAYOUT_OPT_DATA_HEAD_ROOM	0x00000020
/**
 * Select to modify the data-tail-room setting
 */
#define DPNI_BUF_LAYOUT_OPT_DATA_TAIL_ROOM	0x00000040

/**
 * struct dpni_buffer_layout - Structure representing DPNI buffer layout
 * @options: Flags representing the suggested modifications to the buffer
 *		layout; Use any combination of 'DPNI_BUF_LAYOUT_OPT_<X>' flags
 * @pass_timestamp: Pass timestamp value
 * @pass_parser_result: Pass parser results
 * @pass_frame_status: Pass frame status
 * @private_data_size: Size kept for private data (in bytes)
 * @data_align: Data alignment
 * @data_head_room: Data head room
 * @data_tail_room: Data tail room
 */
struct dpni_buffer_layout {
	u32	options;
	int	pass_timestamp;
	int	pass_parser_result;
	int	pass_frame_status;
	u16	private_data_size;
	u16	data_align;
	u16	data_head_room;
	u16	data_tail_room;
};

/**
 * enum dpni_queue_type - Identifies a type of queue targeted by the command
 * @DPNI_QUEUE_RX: Rx queue
 * @DPNI_QUEUE_TX: Tx queue
 * @DPNI_QUEUE_TX_CONFIRM: Tx confirmation queue
 * @DPNI_QUEUE_RX_ERR: Rx error queue
 */enum dpni_queue_type {
	DPNI_QUEUE_RX,
	DPNI_QUEUE_TX,
	DPNI_QUEUE_TX_CONFIRM,
	DPNI_QUEUE_RX_ERR,
};

int dpni_get_buffer_layout(struct fsl_mc_io		*mc_io,
			   u32				cmd_flags,
			   u16				token,
			   enum dpni_queue_type		qtype,
			   struct dpni_buffer_layout	*layout);

int dpni_set_buffer_layout(struct fsl_mc_io		   *mc_io,
			   u32				   cmd_flags,
			   u16				   token,
			   enum dpni_queue_type		   qtype,
			   const struct dpni_buffer_layout *layout);

/**
 * enum dpni_offload - Identifies a type of offload targeted by the command
 * @DPNI_OFF_RX_L3_CSUM: Rx L3 checksum validation
 * @DPNI_OFF_RX_L4_CSUM: Rx L4 checksum validation
 * @DPNI_OFF_TX_L3_CSUM: Tx L3 checksum generation
 * @DPNI_OFF_TX_L4_CSUM: Tx L4 checksum generation
 */
enum dpni_offload {
	DPNI_OFF_RX_L3_CSUM,
	DPNI_OFF_RX_L4_CSUM,
	DPNI_OFF_TX_L3_CSUM,
	DPNI_OFF_TX_L4_CSUM,
};

int dpni_set_offload(struct fsl_mc_io	*mc_io,
		     u32		cmd_flags,
		     u16		token,
		     enum dpni_offload	type,
		     u32		config);

int dpni_get_offload(struct fsl_mc_io	*mc_io,
		     u32		cmd_flags,
		     u16		token,
		     enum dpni_offload	type,
		     u32		*config);

int dpni_get_qdid(struct fsl_mc_io	*mc_io,
		  u32			cmd_flags,
		  u16			token,
		  enum dpni_queue_type	qtype,
		  u16			*qdid);

int dpni_get_tx_data_offset(struct fsl_mc_io	*mc_io,
			    u32			cmd_flags,
			    u16			token,
			    u16			*data_offset);

#define DPNI_STATISTICS_CNT		7

/**
 * union dpni_statistics - Union describing the DPNI statistics
 * @page_0: Page_0 statistics structure
 * @page_0.ingress_all_frames: Ingress frame count
 * @page_0.ingress_all_bytes: Ingress byte count
 * @page_0.ingress_multicast_frames: Ingress multicast frame count
 * @page_0.ingress_multicast_bytes: Ingress multicast byte count
 * @page_0.ingress_broadcast_frames: Ingress broadcast frame count
 * @page_0.ingress_broadcast_bytes: Ingress broadcast byte count
 * @page_1: Page_1 statistics structure
 * @page_1.egress_all_frames: Egress frame count
 * @page_1.egress_all_bytes: Egress byte count
 * @page_1.egress_multicast_frames: Egress multicast frame count
 * @page_1.egress_multicast_bytes: Egress multicast byte count
 * @page_1.egress_broadcast_frames: Egress broadcast frame count
 * @page_1.egress_broadcast_bytes: Egress broadcast byte count
 * @page_2: Page_2 statistics structure
 * @page_2.ingress_filtered_frames: Ingress filtered frame count
 * @page_2.ingress_discarded_frames: Ingress discarded frame count
 * @page_2.ingress_nobuffer_discards: Ingress discarded frame count due to
 *	lack of buffers
 * @page_2.egress_discarded_frames: Egress discarded frame count
 * @page_2.egress_confirmed_frames: Egress confirmed frame count
 * @page3: Page_3 statistics structure
 * @page_3.egress_dequeue_bytes: Cumulative count of the number of bytes
 *	dequeued from egress FQs
 * @page_3.egress_dequeue_frames: Cumulative count of the number of frames
 *	dequeued from egress FQs
 * @page_3.egress_reject_bytes: Cumulative count of the number of bytes in
 *	egress frames whose enqueue was rejected
 * @page_3.egress_reject_frames: Cumulative count of the number of egress
 *	frames whose enqueue was rejected
 * @page_4: Page_4 statistics structure: congestion points
 * @page_4.cgr_reject_frames: number of rejected frames due to congestion point
 * @page_4.cgr_reject_bytes: number of rejected bytes due to congestion point
 * @page_5: Page_5 statistics structure: policer
 * @page_5.policer_cnt_red: NUmber of red colored frames
 * @page_5.policer_cnt_yellow: number of yellow colored frames
 * @page_5.policer_cnt_green: number of green colored frames
 * @page_5.policer_cnt_re_red: number of recolored red frames
 * @page_5.policer_cnt_re_yellow: number of recolored yellow frames
 * @page_6: Page_6 statistics structure
 * @page_6.tx_pending_frames: total number of frames pending in egress FQs
 * @raw: raw statistics structure, used to index counters
 */
union dpni_statistics {
	struct {
		u64 ingress_all_frames;
		u64 ingress_all_bytes;
		u64 ingress_multicast_frames;
		u64 ingress_multicast_bytes;
		u64 ingress_broadcast_frames;
		u64 ingress_broadcast_bytes;
	} page_0;
	struct {
		u64 egress_all_frames;
		u64 egress_all_bytes;
		u64 egress_multicast_frames;
		u64 egress_multicast_bytes;
		u64 egress_broadcast_frames;
		u64 egress_broadcast_bytes;
	} page_1;
	struct {
		u64 ingress_filtered_frames;
		u64 ingress_discarded_frames;
		u64 ingress_nobuffer_discards;
		u64 egress_discarded_frames;
		u64 egress_confirmed_frames;
	} page_2;
	struct {
		u64 egress_dequeue_bytes;
		u64 egress_dequeue_frames;
		u64 egress_reject_bytes;
		u64 egress_reject_frames;
	} page_3;
	struct {
		u64 cgr_reject_frames;
		u64 cgr_reject_bytes;
	} page_4;
	struct {
		u64 policer_cnt_red;
		u64 policer_cnt_yellow;
		u64 policer_cnt_green;
		u64 policer_cnt_re_red;
		u64 policer_cnt_re_yellow;
	} page_5;
	struct {
		u64 tx_pending_frames;
	} page_6;
	struct {
		u64 counter[DPNI_STATISTICS_CNT];
	} raw;
};

int dpni_get_statistics(struct fsl_mc_io	*mc_io,
			u32			cmd_flags,
			u16			token,
			u8			page,
			union dpni_statistics	*stat);

/**
 * Enable auto-negotiation
 */
#define DPNI_LINK_OPT_AUTONEG		0x0000000000000001ULL
/**
 * Enable half-duplex mode
 */
#define DPNI_LINK_OPT_HALF_DUPLEX	0x0000000000000002ULL
/**
 * Enable pause frames
 */
#define DPNI_LINK_OPT_PAUSE		0x0000000000000004ULL
/**
 * Enable a-symmetric pause frames
 */
#define DPNI_LINK_OPT_ASYM_PAUSE	0x0000000000000008ULL

/**
 * Enable priority flow control pause frames
 */
#define DPNI_LINK_OPT_PFC_PAUSE		0x0000000000000010ULL

/**
 * struct - Structure representing DPNI link configuration
 * @rate: Rate
 * @options: Mask of available options; use 'DPNI_LINK_OPT_<X>' values
 */
struct dpni_link_cfg {
	u32 rate;
	u64 options;
};

int dpni_set_link_cfg(struct fsl_mc_io			*mc_io,
		      u32				cmd_flags,
		      u16				token,
		      const struct dpni_link_cfg	*cfg);

int dpni_get_link_cfg(struct fsl_mc_io			*mc_io,
		      u32				cmd_flags,
		      u16				token,
		      struct dpni_link_cfg		*cfg);

/**
 * struct dpni_link_state - Structure representing DPNI link state
 * @rate: Rate
 * @options: Mask of available options; use 'DPNI_LINK_OPT_<X>' values
 * @up: Link state; '0' for down, '1' for up
 */
struct dpni_link_state {
	u32	rate;
	u64	options;
	int	up;
};

int dpni_get_link_state(struct fsl_mc_io	*mc_io,
			u32			cmd_flags,
			u16			token,
			struct dpni_link_state	*state);

int dpni_set_max_frame_length(struct fsl_mc_io	*mc_io,
			      u32		cmd_flags,
			      u16		token,
			      u16		max_frame_length);

int dpni_get_max_frame_length(struct fsl_mc_io	*mc_io,
			      u32		cmd_flags,
			      u16		token,
			      u16		*max_frame_length);

int dpni_set_multicast_promisc(struct fsl_mc_io *mc_io,
			       u32		cmd_flags,
			       u16		token,
			       int		en);

int dpni_get_multicast_promisc(struct fsl_mc_io *mc_io,
			       u32		cmd_flags,
			       u16		token,
			       int		*en);

int dpni_set_unicast_promisc(struct fsl_mc_io	*mc_io,
			     u32		cmd_flags,
			     u16		token,
			     int		en);

int dpni_get_unicast_promisc(struct fsl_mc_io	*mc_io,
			     u32		cmd_flags,
			     u16		token,
			     int		*en);

int dpni_set_primary_mac_addr(struct fsl_mc_io *mc_io,
			      u32		cmd_flags,
			      u16		token,
			      const u8		mac_addr[6]);

int dpni_get_primary_mac_addr(struct fsl_mc_io	*mc_io,
			      u32		cmd_flags,
			      u16		token,
			      u8		mac_addr[6]);

int dpni_get_port_mac_addr(struct fsl_mc_io	*mc_io,
			   u32			cm_flags,
			   u16			token,
			   u8			mac_addr[6]);

int dpni_add_mac_addr(struct fsl_mc_io	*mc_io,
		      u32		cmd_flags,
		      u16		token,
		      const u8		mac_addr[6]);

int dpni_remove_mac_addr(struct fsl_mc_io	*mc_io,
			 u32			cmd_flags,
			 u16			token,
			 const u8		mac_addr[6]);

int dpni_clear_mac_filters(struct fsl_mc_io	*mc_io,
			   u32			cmd_flags,
			   u16			token,
			   int			unicast,
			   int			multicast);

/**
 * enum dpni_dist_mode - DPNI distribution mode
 * @DPNI_DIST_MODE_NONE: No distribution
 * @DPNI_DIST_MODE_HASH: Use hash distribution; only relevant if
 *		the 'DPNI_OPT_DIST_HASH' option was set at DPNI creation
 * @DPNI_DIST_MODE_FS:  Use explicit flow steering; only relevant if
 *	 the 'DPNI_OPT_DIST_FS' option was set at DPNI creation
 */
enum dpni_dist_mode {
	DPNI_DIST_MODE_NONE = 0,
	DPNI_DIST_MODE_HASH = 1,
	DPNI_DIST_MODE_FS = 2
};

/**
 * enum dpni_fs_miss_action -   DPNI Flow Steering miss action
 * @DPNI_FS_MISS_DROP: In case of no-match, drop the frame
 * @DPNI_FS_MISS_EXPLICIT_FLOWID: In case of no-match, use explicit flow-id
 * @DPNI_FS_MISS_HASH: In case of no-match, distribute using hash
 */
enum dpni_fs_miss_action {
	DPNI_FS_MISS_DROP = 0,
	DPNI_FS_MISS_EXPLICIT_FLOWID = 1,
	DPNI_FS_MISS_HASH = 2
};

/**
 * struct dpni_fs_tbl_cfg - Flow Steering table configuration
 * @miss_action: Miss action selection
 * @default_flow_id: Used when 'miss_action = DPNI_FS_MISS_EXPLICIT_FLOWID'
 */
struct dpni_fs_tbl_cfg {
	enum dpni_fs_miss_action	miss_action;
	u16				default_flow_id;
};

int dpni_prepare_key_cfg(const struct dpkg_profile_cfg *cfg,
			 u8 *key_cfg_buf);

/**
 * struct dpni_rx_tc_dist_cfg - Rx traffic class distribution configuration
 * @dist_size: Set the distribution size;
 *	supported values: 1,2,3,4,6,7,8,12,14,16,24,28,32,48,56,64,96,
 *	112,128,192,224,256,384,448,512,768,896,1024
 * @dist_mode: Distribution mode
 * @key_cfg_iova: I/O virtual address of 256 bytes DMA-able memory filled with
 *		the extractions to be used for the distribution key by calling
 *		dpni_prepare_key_cfg() relevant only when
 *		'dist_mode != DPNI_DIST_MODE_NONE', otherwise it can be '0'
 * @fs_cfg: Flow Steering table configuration; only relevant if
 *		'dist_mode = DPNI_DIST_MODE_FS'
 */
struct dpni_rx_tc_dist_cfg {
	u16			dist_size;
	enum dpni_dist_mode	dist_mode;
	u64			key_cfg_iova;
	struct dpni_fs_tbl_cfg	fs_cfg;
};

int dpni_set_rx_tc_dist(struct fsl_mc_io			*mc_io,
			u32					cmd_flags,
			u16					token,
			u8					tc_id,
			const struct dpni_rx_tc_dist_cfg	*cfg);

/**
 * When used for fs_miss_flow_id in function dpni_set_rx_dist,
 * will signal to dpni to drop all unclassified frames
 */
#define DPNI_FS_MISS_DROP		((uint16_t)-1)

/**
 * struct dpni_rx_dist_cfg - Rx distribution configuration
 * @dist_size:	distribution size
 * @key_cfg_iova: I/O virtual address of 256 bytes DMA-able memory filled with
 *		the extractions to be used for the distribution key by calling
 *		dpni_prepare_key_cfg(); relevant only when enable!=0 otherwise
 *		it can be '0'
 * @enable: enable/disable the distribution.
 * @tc: TC id for which distribution is set
 * @fs_miss_flow_id: when packet misses all rules from flow steering table and
 *		hash is disabled it will be put into this queue id; use
 *		DPNI_FS_MISS_DROP to drop frames. The value of this field is
 *		used only when flow steering distribution is enabled and hash
 *		distribution is disabled
 */
struct dpni_rx_dist_cfg {
	u16 dist_size;
	u64 key_cfg_iova;
	u8 enable;
	u8 tc;
	u16 fs_miss_flow_id;
};

int dpni_set_rx_fs_dist(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			const struct dpni_rx_dist_cfg *cfg);

int dpni_set_rx_hash_dist(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  const struct dpni_rx_dist_cfg *cfg);

/**
 * struct dpni_qos_tbl_cfg - Structure representing QOS table configuration
 * @key_cfg_iova: I/O virtual address of 256 bytes DMA-able memory filled with
 *		key extractions to be used as the QoS criteria by calling
 *		dpkg_prepare_key_cfg()
 * @discard_on_miss: Set to '1' to discard frames in case of no match (miss);
 *		'0' to use the 'default_tc' in such cases
 * @default_tc: Used in case of no-match and 'discard_on_miss'= 0
 */
struct dpni_qos_tbl_cfg {
	u64 key_cfg_iova;
	int discard_on_miss;
	u8 default_tc;
};

int dpni_set_qos_table(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       const struct dpni_qos_tbl_cfg *cfg);

/**
 * enum dpni_dest - DPNI destination types
 * @DPNI_DEST_NONE: Unassigned destination; The queue is set in parked mode and
 *		does not generate FQDAN notifications; user is expected to
 *		dequeue from the queue based on polling or other user-defined
 *		method
 * @DPNI_DEST_DPIO: The queue is set in schedule mode and generates FQDAN
 *		notifications to the specified DPIO; user is expected to dequeue
 *		from the queue only after notification is received
 * @DPNI_DEST_DPCON: The queue is set in schedule mode and does not generate
 *		FQDAN notifications, but is connected to the specified DPCON
 *		object; user is expected to dequeue from the DPCON channel
 */
enum dpni_dest {
	DPNI_DEST_NONE = 0,
	DPNI_DEST_DPIO = 1,
	DPNI_DEST_DPCON = 2
};

/**
 * struct dpni_queue - Queue structure
 * @destination - Destination structure
 * @destination.id: ID of the destination, only relevant if DEST_TYPE is > 0.
 *	Identifies either a DPIO or a DPCON object.
 *	Not relevant for Tx queues.
 * @destination.type:	May be one of the following:
 *	0 - No destination, queue can be manually
 *		queried, but will not push traffic or
 *		notifications to a DPIO;
 *	1 - The destination is a DPIO. When traffic
 *		becomes available in the queue a FQDAN
 *		(FQ data available notification) will be
 *		generated to selected DPIO;
 *	2 - The destination is a DPCON. The queue is
 *		associated with a DPCON object for the
 *		purpose of scheduling between multiple
 *		queues. The DPCON may be independently
 *		configured to generate notifications.
 *		Not relevant for Tx queues.
 * @destination.hold_active: Hold active, maintains a queue scheduled for longer
 *	in a DPIO during dequeue to reduce spread of traffic.
 *	Only relevant if queues are
 *	not affined to a single DPIO.
 * @user_context: User data, presented to the user along with any frames
 *	from this queue. Not relevant for Tx queues.
 * @flc: FD FLow Context structure
 * @flc.value: Default FLC value for traffic dequeued from
 *      this queue.  Please check description of FD
 *      structure for more information.
 *      Note that FLC values set using dpni_add_fs_entry,
 *      if any, take precedence over values per queue.
 * @flc.stash_control: Boolean, indicates whether the 6 lowest
 *      - significant bits are used for stash control.
 *      significant bits are used for stash control.  If set, the 6
 *      least significant bits in value are interpreted as follows:
 *      - bits 0-1: indicates the number of 64 byte units of context
 *      that are stashed.  FLC value is interpreted as a memory address
 *      in this case, excluding the 6 LS bits.
 *      - bits 2-3: indicates the number of 64 byte units of frame
 *      annotation to be stashed.  Annotation is placed at FD[ADDR].
 *      - bits 4-5: indicates the number of 64 byte units of frame
 *      data to be stashed.  Frame data is placed at FD[ADDR] +
 *      FD[OFFSET].
 *      For more details check the Frame Descriptor section in the
 *      hardware documentation.
 */
struct dpni_queue {
	struct {
		u16 id;
		enum dpni_dest type;
		char hold_active;
		u8 priority;
	} destination;
	u64 user_context;
	struct {
		u64 value;
		char stash_control;
	} flc;
};

/**
 * struct dpni_queue_id - Queue identification, used for enqueue commands
 *			or queue control
 * @fqid: FQID used for enqueueing to and/or configuration of this specific FQ
 * @qdbin: Queueing bin, used to enqueue using QDID, DQBIN, QPRI. Only relevant
 *		for Tx queues.
 */
struct dpni_queue_id {
	u32 fqid;
	u16 qdbin;
};

/**
 * Set User Context
 */
#define DPNI_QUEUE_OPT_USER_CTX		0x00000001
#define DPNI_QUEUE_OPT_DEST		0x00000002
#define DPNI_QUEUE_OPT_FLC		0x00000004
#define DPNI_QUEUE_OPT_HOLD_ACTIVE	0x00000008

int dpni_set_queue(struct fsl_mc_io	*mc_io,
		   u32			cmd_flags,
		   u16			token,
		   enum dpni_queue_type	qtype,
		   u8			tc,
		   u8			index,
		   u8			options,
		   const struct dpni_queue *queue);

int dpni_get_queue(struct fsl_mc_io	*mc_io,
		   u32			cmd_flags,
		   u16			token,
		   enum dpni_queue_type	qtype,
		   u8			tc,
		   u8			index,
		   struct dpni_queue	*queue,
		   struct dpni_queue_id	*qid);

/**
 * enum dpni_congestion_unit - DPNI congestion units
 * @DPNI_CONGESTION_UNIT_BYTES: bytes units
 * @DPNI_CONGESTION_UNIT_FRAMES: frames units
 */
enum dpni_congestion_unit {
	DPNI_CONGESTION_UNIT_BYTES = 0,
	DPNI_CONGESTION_UNIT_FRAMES
};

/**
 * enum dpni_congestion_point - Structure representing congestion point
 * @DPNI_CP_QUEUE: Set taildrop per queue, identified by QUEUE_TYPE, TC and
 *		QUEUE_INDEX
 * @DPNI_CP_GROUP: Set taildrop per queue group. Depending on options used to
 *		define the DPNI this can be either per TC (default) or per
 *		interface (DPNI_OPT_SHARED_CONGESTION set at DPNI create).
 *		QUEUE_INDEX is ignored if this type is used.
 */
enum dpni_congestion_point {
	DPNI_CP_QUEUE,
	DPNI_CP_GROUP,
};

/**
 * struct dpni_dest_cfg - Structure representing DPNI destination parameters
 * @dest_type:	Destination type
 * @dest_id:	Either DPIO ID or DPCON ID, depending on the destination type
 * @priority:	Priority selection within the DPIO or DPCON channel; valid
 *		values are 0-1 or 0-7, depending on the number of priorities
 *		in that channel; not relevant for 'DPNI_DEST_NONE' option
 */
struct dpni_dest_cfg {
	enum dpni_dest dest_type;
	int dest_id;
	u8 priority;
};

/* DPNI congestion options */

/**
 * This congestion will trigger flow control or priority flow control.
 * This will have effect only if flow control is enabled with
 * dpni_set_link_cfg().
 */
#define DPNI_CONG_OPT_FLOW_CONTROL		0x00000040

/**
 * struct dpni_congestion_notification_cfg - congestion notification
 *					configuration
 * @units: Units type
 * @threshold_entry: Above this threshold we enter a congestion state.
 *		set it to '0' to disable it
 * @threshold_exit: Below this threshold we exit the congestion state.
 * @message_ctx: The context that will be part of the CSCN message
 * @message_iova: I/O virtual address (must be in DMA-able memory),
 *		must be 16B aligned; valid only if 'DPNI_CONG_OPT_WRITE_MEM_<X>'
 *		is contained in 'options'
 * @dest_cfg: CSCN can be send to either DPIO or DPCON WQ channel
 * @notification_mode: Mask of available options; use 'DPNI_CONG_OPT_<X>' values
 */

struct dpni_congestion_notification_cfg {
	enum dpni_congestion_unit units;
	u32 threshold_entry;
	u32 threshold_exit;
	u64 message_ctx;
	u64 message_iova;
	struct dpni_dest_cfg dest_cfg;
	u16 notification_mode;
};

int dpni_set_congestion_notification(
			struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			enum dpni_queue_type qtype,
			u8 tc_id,
			const struct dpni_congestion_notification_cfg *cfg);

/**
 * struct dpni_taildrop - Structure representing the taildrop
 * @enable:	Indicates whether the taildrop is active or not.
 * @units:	Indicates the unit of THRESHOLD. Queue taildrop only supports
 *		byte units, this field is ignored and assumed = 0 if
 *		CONGESTION_POINT is 0.
 * @threshold:	Threshold value, in units identified by UNITS field. Value 0
 *		cannot be used as a valid taildrop threshold, THRESHOLD must
 *		be > 0 if the taildrop is enabled.
 */
struct dpni_taildrop {
	char enable;
	enum dpni_congestion_unit units;
	u32 threshold;
};

int dpni_set_taildrop(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      enum dpni_congestion_point cg_point,
		      enum dpni_queue_type q_type,
		      u8 tc,
		      u8 q_index,
		      struct dpni_taildrop *taildrop);

int dpni_get_taildrop(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      enum dpni_congestion_point cg_point,
		      enum dpni_queue_type q_type,
		      u8 tc,
		      u8 q_index,
		      struct dpni_taildrop *taildrop);

/**
 * struct dpni_rule_cfg - Rule configuration for table lookup
 * @key_iova: I/O virtual address of the key (must be in DMA-able memory)
 * @mask_iova: I/O virtual address of the mask (must be in DMA-able memory)
 * @key_size: key and mask size (in bytes)
 */
struct dpni_rule_cfg {
	u64	key_iova;
	u64	mask_iova;
	u8	key_size;
};

/**
 * Discard matching traffic. If set, this takes precedence over any other
 * configuration and matching traffic is always discarded.
 */
 #define DPNI_FS_OPT_DISCARD            0x1

/**
 * Set FLC value. If set, flc member of struct dpni_fs_action_cfg is used to
 * override the FLC value set per queue.
 * For more details check the Frame Descriptor section in the hardware
 * documentation.
 */
#define DPNI_FS_OPT_SET_FLC            0x2

/**
 * Indicates whether the 6 lowest significant bits of FLC are used for stash
 * control. If set, the 6 least significant bits in value are interpreted as
 * follows:
 *     - bits 0-1: indicates the number of 64 byte units of context that are
 *     stashed. FLC value is interpreted as a memory address in this case,
 *     excluding the 6 LS bits.
 *     - bits 2-3: indicates the number of 64 byte units of frame annotation
 *     to be stashed. Annotation is placed at FD[ADDR].
 *     - bits 4-5: indicates the number of 64 byte units of frame data to be
 *     stashed. Frame data is placed at FD[ADDR] + FD[OFFSET].
 * This flag is ignored if DPNI_FS_OPT_SET_FLC is not specified.
 */
#define DPNI_FS_OPT_SET_STASH_CONTROL  0x4

/**
 * struct dpni_fs_action_cfg - Action configuration for table look-up
 * @flc:	FLC value for traffic matching this rule. Please check the
 *		Frame Descriptor section in the hardware documentation for
 *		more information.
 * @flow_id:	Identifies the Rx queue used for matching traffic. Supported
 *		values are in range 0 to num_queue-1.
 * @options:	Any combination of DPNI_FS_OPT_ values.
 */
struct dpni_fs_action_cfg {
	u64 flc;
	u16 flow_id;
	u16 options;
};

int dpni_add_fs_entry(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 tc_id,
		      u16 index,
		      const struct dpni_rule_cfg *cfg,
		      const struct dpni_fs_action_cfg *action);

int dpni_remove_fs_entry(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u8 tc_id,
			 const struct dpni_rule_cfg *cfg);

int dpni_add_qos_entry(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       const struct dpni_rule_cfg *cfg,
		       u8 tc_id,
		       u16 index);

int dpni_remove_qos_entry(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  const struct dpni_rule_cfg *cfg);

int dpni_clear_qos_table(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token);

int dpni_get_api_version(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 *major_ver,
			 u16 *minor_ver);
/**
 * struct dpni_tx_shaping - Structure representing DPNI tx shaping configuration
 * @rate_limit:		Rate in Mbps
 * @max_burst_size:	Burst size in bytes (up to 64KB)
 */
struct dpni_tx_shaping_cfg {
	u32 rate_limit;
	u16 max_burst_size;
};

int dpni_set_tx_shaping(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			const struct dpni_tx_shaping_cfg *tx_cr_shaper,
			const struct dpni_tx_shaping_cfg *tx_er_shaper,
			int coupled);

/**
 * struct dpni_single_step_cfg - configure single step PTP (IEEE 1588)
 * @en:		enable single step PTP. When enabled the PTPv1 functionality
 *		will not work. If the field is zero, offset and ch_update
 *		parameters will be ignored
 * @offset:	start offset from the beginning of the frame where
 *		timestamp field is found. The offset must respect all MAC
 *		headers, VLAN tags and other protocol headers
 * @ch_update:	when set UDP checksum will be updated inside packet
 * @peer_delay:	For peer-to-peer transparent clocks add this value to the
 *		correction field in addition to the transient time update.
 *		The value expresses nanoseconds.
 */
struct dpni_single_step_cfg {
	u8	en;
	u8	ch_update;
	u16	offset;
	u32	peer_delay;
};

int dpni_set_single_step_cfg(struct fsl_mc_io *mc_io,
			     u32 cmd_flags,
			     u16 token,
			     struct dpni_single_step_cfg *ptp_cfg);

int dpni_get_single_step_cfg(struct fsl_mc_io *mc_io,
			     u32 cmd_flags,
			     u16 token,
			     struct dpni_single_step_cfg *ptp_cfg);

int dpni_enable_vlan_filter(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			    u32 en);

int dpni_add_vlan_id(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
		     u16 vlan_id, u8 flags, u8 tc_id, u8 flow_id);

int dpni_remove_vlan_id(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			u16 vlan_id);

#endif /* __FSL_DPNI_H */
