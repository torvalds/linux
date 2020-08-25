/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2018 NXP
 */
#ifndef _DPSECI_H_
#define _DPSECI_H_

/*
 * Data Path SEC Interface API
 * Contains initialization APIs and runtime control APIs for DPSECI
 */

struct fsl_mc_io;

/**
 * General DPSECI macros
 */

/**
 * Maximum number of Tx/Rx queues per DPSECI object
 */
#define DPSECI_MAX_QUEUE_NUM		16

/**
 * All queues considered; see dpseci_set_rx_queue()
 */
#define DPSECI_ALL_QUEUES	(u8)(-1)

int dpseci_open(struct fsl_mc_io *mc_io, u32 cmd_flags, int dpseci_id,
		u16 *token);

int dpseci_close(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token);

/**
 * Enable the Congestion Group support
 */
#define DPSECI_OPT_HAS_CG		0x000020

/**
 * struct dpseci_cfg - Structure representing DPSECI configuration
 * @options: Any combination of the following flags:
 *		DPSECI_OPT_HAS_CG
 * @num_tx_queues: num of queues towards the SEC
 * @num_rx_queues: num of queues back from the SEC
 * @priorities: Priorities for the SEC hardware processing;
 *		each place in the array is the priority of the tx queue
 *		towards the SEC;
 *		valid priorities are configured with values 1-8;
 */
struct dpseci_cfg {
	u32 options;
	u8 num_tx_queues;
	u8 num_rx_queues;
	u8 priorities[DPSECI_MAX_QUEUE_NUM];
};

int dpseci_enable(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token);

int dpseci_disable(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token);

int dpseci_reset(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token);

int dpseci_is_enabled(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
		      int *en);

/**
 * struct dpseci_attr - Structure representing DPSECI attributes
 * @id: DPSECI object ID
 * @num_tx_queues: number of queues towards the SEC
 * @num_rx_queues: number of queues back from the SEC
 * @options: any combination of the following flags:
 *		DPSECI_OPT_HAS_CG
 */
struct dpseci_attr {
	int id;
	u8 num_tx_queues;
	u8 num_rx_queues;
	u32 options;
};

int dpseci_get_attributes(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			  struct dpseci_attr *attr);

/**
 * enum dpseci_dest - DPSECI destination types
 * @DPSECI_DEST_NONE: Unassigned destination; The queue is set in parked mode
 *	and does not generate FQDAN notifications; user is expected to dequeue
 *	from the queue based on polling or other user-defined method
 * @DPSECI_DEST_DPIO: The queue is set in schedule mode and generates FQDAN
 *	notifications to the specified DPIO; user is expected to dequeue from
 *	the queue only after notification is received
 * @DPSECI_DEST_DPCON: The queue is set in schedule mode and does not generate
 *	FQDAN notifications, but is connected to the specified DPCON object;
 *	user is expected to dequeue from the DPCON channel
 */
enum dpseci_dest {
	DPSECI_DEST_NONE = 0,
	DPSECI_DEST_DPIO,
	DPSECI_DEST_DPCON
};

/**
 * struct dpseci_dest_cfg - Structure representing DPSECI destination parameters
 * @dest_type: Destination type
 * @dest_id: Either DPIO ID or DPCON ID, depending on the destination type
 * @priority: Priority selection within the DPIO or DPCON channel; valid values
 *	are 0-1 or 0-7, depending on the number of priorities in that channel;
 *	not relevant for 'DPSECI_DEST_NONE' option
 */
struct dpseci_dest_cfg {
	enum dpseci_dest dest_type;
	int dest_id;
	u8 priority;
};

/**
 * DPSECI queue modification options
 */

/**
 * Select to modify the user's context associated with the queue
 */
#define DPSECI_QUEUE_OPT_USER_CTX		0x00000001

/**
 * Select to modify the queue's destination
 */
#define DPSECI_QUEUE_OPT_DEST			0x00000002

/**
 * Select to modify the queue's order preservation
 */
#define DPSECI_QUEUE_OPT_ORDER_PRESERVATION	0x00000004

/**
 * struct dpseci_rx_queue_cfg - DPSECI RX queue configuration
 * @options: Flags representing the suggested modifications to the queue;
 *	Use any combination of 'DPSECI_QUEUE_OPT_<X>' flags
 * @order_preservation_en: order preservation configuration for the rx queue
 * valid only if 'DPSECI_QUEUE_OPT_ORDER_PRESERVATION' is contained in 'options'
 * @user_ctx: User context value provided in the frame descriptor of each
 *	dequeued frame;	valid only if 'DPSECI_QUEUE_OPT_USER_CTX' is contained
 *	in 'options'
 * @dest_cfg: Queue destination parameters; valid only if
 *	'DPSECI_QUEUE_OPT_DEST' is contained in 'options'
 */
struct dpseci_rx_queue_cfg {
	u32 options;
	int order_preservation_en;
	u64 user_ctx;
	struct dpseci_dest_cfg dest_cfg;
};

int dpseci_set_rx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			u8 queue, const struct dpseci_rx_queue_cfg *cfg);

/**
 * struct dpseci_rx_queue_attr - Structure representing attributes of Rx queues
 * @user_ctx: User context value provided in the frame descriptor of each
 *	dequeued frame
 * @order_preservation_en: Status of the order preservation configuration on the
 *	queue
 * @dest_cfg: Queue destination configuration
 * @fqid: Virtual FQID value to be used for dequeue operations
 */
struct dpseci_rx_queue_attr {
	u64 user_ctx;
	int order_preservation_en;
	struct dpseci_dest_cfg dest_cfg;
	u32 fqid;
};

int dpseci_get_rx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			u8 queue, struct dpseci_rx_queue_attr *attr);

/**
 * struct dpseci_tx_queue_attr - Structure representing attributes of Tx queues
 * @fqid: Virtual FQID to be used for sending frames to SEC hardware
 * @priority: SEC hardware processing priority for the queue
 */
struct dpseci_tx_queue_attr {
	u32 fqid;
	u8 priority;
};

int dpseci_get_tx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			u8 queue, struct dpseci_tx_queue_attr *attr);

/**
 * struct dpseci_sec_attr - Structure representing attributes of the SEC
 *	hardware accelerator
 * @ip_id: ID for SEC
 * @major_rev: Major revision number for SEC
 * @minor_rev: Minor revision number for SEC
 * @era: SEC Era
 * @deco_num: The number of copies of the DECO that are implemented in this
 *	version of SEC
 * @zuc_auth_acc_num: The number of copies of ZUCA that are implemented in this
 *	version of SEC
 * @zuc_enc_acc_num: The number of copies of ZUCE that are implemented in this
 *	version of SEC
 * @snow_f8_acc_num: The number of copies of the SNOW-f8 module that are
 *	implemented in this version of SEC
 * @snow_f9_acc_num: The number of copies of the SNOW-f9 module that are
 *	implemented in this version of SEC
 * @crc_acc_num: The number of copies of the CRC module that are implemented in
 *	this version of SEC
 * @pk_acc_num:  The number of copies of the Public Key module that are
 *	implemented in this version of SEC
 * @kasumi_acc_num: The number of copies of the Kasumi module that are
 *	implemented in this version of SEC
 * @rng_acc_num: The number of copies of the Random Number Generator that are
 *	implemented in this version of SEC
 * @md_acc_num: The number of copies of the MDHA (Hashing module) that are
 *	implemented in this version of SEC
 * @arc4_acc_num: The number of copies of the ARC4 module that are implemented
 *	in this version of SEC
 * @des_acc_num: The number of copies of the DES module that are implemented in
 *	this version of SEC
 * @aes_acc_num: The number of copies of the AES module that are implemented in
 *	this version of SEC
 * @ccha_acc_num: The number of copies of the ChaCha20 module that are
 *	implemented in this version of SEC.
 * @ptha_acc_num: The number of copies of the Poly1305 module that are
 *	implemented in this version of SEC.
 **/
struct dpseci_sec_attr {
	u16 ip_id;
	u8 major_rev;
	u8 minor_rev;
	u8 era;
	u8 deco_num;
	u8 zuc_auth_acc_num;
	u8 zuc_enc_acc_num;
	u8 snow_f8_acc_num;
	u8 snow_f9_acc_num;
	u8 crc_acc_num;
	u8 pk_acc_num;
	u8 kasumi_acc_num;
	u8 rng_acc_num;
	u8 md_acc_num;
	u8 arc4_acc_num;
	u8 des_acc_num;
	u8 aes_acc_num;
	u8 ccha_acc_num;
	u8 ptha_acc_num;
};

int dpseci_get_sec_attr(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			struct dpseci_sec_attr *attr);

int dpseci_get_api_version(struct fsl_mc_io *mc_io, u32 cmd_flags,
			   u16 *major_ver, u16 *minor_ver);

/**
 * enum dpseci_congestion_unit - DPSECI congestion units
 * @DPSECI_CONGESTION_UNIT_BYTES: bytes units
 * @DPSECI_CONGESTION_UNIT_FRAMES: frames units
 */
enum dpseci_congestion_unit {
	DPSECI_CONGESTION_UNIT_BYTES = 0,
	DPSECI_CONGESTION_UNIT_FRAMES
};

/**
 * CSCN message is written to message_iova once entering a
 * congestion state (see 'threshold_entry')
 */
#define DPSECI_CGN_MODE_WRITE_MEM_ON_ENTER		0x00000001

/**
 * CSCN message is written to message_iova once exiting a
 * congestion state (see 'threshold_exit')
 */
#define DPSECI_CGN_MODE_WRITE_MEM_ON_EXIT		0x00000002

/**
 * CSCN write will attempt to allocate into a cache (coherent write);
 * valid only if 'DPSECI_CGN_MODE_WRITE_MEM_<X>' is selected
 */
#define DPSECI_CGN_MODE_COHERENT_WRITE			0x00000004

/**
 * if 'dpseci_dest_cfg.dest_type != DPSECI_DEST_NONE' CSCN message is sent to
 * DPIO/DPCON's WQ channel once entering a congestion state
 * (see 'threshold_entry')
 */
#define DPSECI_CGN_MODE_NOTIFY_DEST_ON_ENTER		0x00000008

/**
 * if 'dpseci_dest_cfg.dest_type != DPSECI_DEST_NONE' CSCN message is sent to
 * DPIO/DPCON's WQ channel once exiting a congestion state
 * (see 'threshold_exit')
 */
#define DPSECI_CGN_MODE_NOTIFY_DEST_ON_EXIT		0x00000010

/**
 * if 'dpseci_dest_cfg.dest_type != DPSECI_DEST_NONE' when the CSCN is written
 * to the sw-portal's DQRR, the DQRI interrupt is asserted immediately
 * (if enabled)
 */
#define DPSECI_CGN_MODE_INTR_COALESCING_DISABLED	0x00000020

/**
 * struct dpseci_congestion_notification_cfg - congestion notification
 *	configuration
 * @units: units type
 * @threshold_entry: above this threshold we enter a congestion state.
 *	set it to '0' to disable it
 * @threshold_exit: below this threshold we exit the congestion state.
 * @message_ctx: The context that will be part of the CSCN message
 * @message_iova: I/O virtual address (must be in DMA-able memory),
 *	must be 16B aligned;
 * @dest_cfg: CSCN can be send to either DPIO or DPCON WQ channel
 * @notification_mode: Mask of available options; use 'DPSECI_CGN_MODE_<X>'
 *	values
 */
struct dpseci_congestion_notification_cfg {
	enum dpseci_congestion_unit units;
	u32 threshold_entry;
	u32 threshold_exit;
	u64 message_ctx;
	u64 message_iova;
	struct dpseci_dest_cfg dest_cfg;
	u16 notification_mode;
};

int dpseci_set_congestion_notification(struct fsl_mc_io *mc_io, u32 cmd_flags,
	u16 token, const struct dpseci_congestion_notification_cfg *cfg);

int dpseci_get_congestion_notification(struct fsl_mc_io *mc_io, u32 cmd_flags,
	u16 token, struct dpseci_congestion_notification_cfg *cfg);

#endif /* _DPSECI_H_ */
