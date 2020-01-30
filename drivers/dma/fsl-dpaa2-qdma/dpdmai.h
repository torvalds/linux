/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 NXP */

#ifndef __FSL_DPDMAI_H
#define __FSL_DPDMAI_H

/* DPDMAI Version */
#define DPDMAI_VER_MAJOR	2
#define DPDMAI_VER_MINOR	2

#define DPDMAI_CMD_BASE_VERSION	0
#define DPDMAI_CMD_ID_OFFSET	4

#define DPDMAI_CMDID_FORMAT(x)	(((x) << DPDMAI_CMD_ID_OFFSET) | \
				DPDMAI_CMD_BASE_VERSION)

/* Command IDs */
#define DPDMAI_CMDID_CLOSE		DPDMAI_CMDID_FORMAT(0x800)
#define DPDMAI_CMDID_OPEN               DPDMAI_CMDID_FORMAT(0x80E)
#define DPDMAI_CMDID_CREATE             DPDMAI_CMDID_FORMAT(0x90E)

#define DPDMAI_CMDID_ENABLE             DPDMAI_CMDID_FORMAT(0x002)
#define DPDMAI_CMDID_DISABLE            DPDMAI_CMDID_FORMAT(0x003)
#define DPDMAI_CMDID_GET_ATTR           DPDMAI_CMDID_FORMAT(0x004)
#define DPDMAI_CMDID_RESET              DPDMAI_CMDID_FORMAT(0x005)
#define DPDMAI_CMDID_IS_ENABLED         DPDMAI_CMDID_FORMAT(0x006)

#define DPDMAI_CMDID_SET_IRQ            DPDMAI_CMDID_FORMAT(0x010)
#define DPDMAI_CMDID_GET_IRQ            DPDMAI_CMDID_FORMAT(0x011)
#define DPDMAI_CMDID_SET_IRQ_ENABLE     DPDMAI_CMDID_FORMAT(0x012)
#define DPDMAI_CMDID_GET_IRQ_ENABLE     DPDMAI_CMDID_FORMAT(0x013)
#define DPDMAI_CMDID_SET_IRQ_MASK       DPDMAI_CMDID_FORMAT(0x014)
#define DPDMAI_CMDID_GET_IRQ_MASK       DPDMAI_CMDID_FORMAT(0x015)
#define DPDMAI_CMDID_GET_IRQ_STATUS     DPDMAI_CMDID_FORMAT(0x016)
#define DPDMAI_CMDID_CLEAR_IRQ_STATUS	DPDMAI_CMDID_FORMAT(0x017)

#define DPDMAI_CMDID_SET_RX_QUEUE	DPDMAI_CMDID_FORMAT(0x1A0)
#define DPDMAI_CMDID_GET_RX_QUEUE       DPDMAI_CMDID_FORMAT(0x1A1)
#define DPDMAI_CMDID_GET_TX_QUEUE       DPDMAI_CMDID_FORMAT(0x1A2)

#define MC_CMD_HDR_TOKEN_O 32  /* Token field offset */
#define MC_CMD_HDR_TOKEN_S 16  /* Token field size */

#define MAKE_UMASK64(_width) \
	((u64)((_width) < 64 ? ((u64)1 << (_width)) - 1 : (u64)-1))

/* Data Path DMA Interface API
 * Contains initialization APIs and runtime control APIs for DPDMAI
 */

/**
 * Maximum number of Tx/Rx priorities per DPDMAI object
 */
#define DPDMAI_PRIO_NUM		2

/* DPDMAI queue modification options */

/**
 * Select to modify the user's context associated with the queue
 */
#define DPDMAI_QUEUE_OPT_USER_CTX	0x1

/**
 * Select to modify the queue's destination
 */
#define DPDMAI_QUEUE_OPT_DEST		0x2

/**
 * struct dpdmai_cfg - Structure representing DPDMAI configuration
 * @priorities: Priorities for the DMA hardware processing; valid priorities are
 *	configured with values 1-8; the entry following last valid entry
 *	should be configured with 0
 */
struct dpdmai_cfg {
	u8 priorities[DPDMAI_PRIO_NUM];
};

/**
 * struct dpdmai_attr - Structure representing DPDMAI attributes
 * @id: DPDMAI object ID
 * @version: DPDMAI version
 * @num_of_priorities: number of priorities
 */
struct dpdmai_attr {
	int	id;
	/**
	 * struct version - DPDMAI version
	 * @major: DPDMAI major version
	 * @minor: DPDMAI minor version
	 */
	struct {
		u16 major;
		u16 minor;
	} version;
	u8 num_of_priorities;
};

/**
 * enum dpdmai_dest - DPDMAI destination types
 * @DPDMAI_DEST_NONE: Unassigned destination; The queue is set in parked mode
 *	and does not generate FQDAN notifications; user is expected to dequeue
 *	from the queue based on polling or other user-defined method
 * @DPDMAI_DEST_DPIO: The queue is set in schedule mode and generates FQDAN
 *	notifications to the specified DPIO; user is expected to dequeue
 *	from the queue only after notification is received
 * @DPDMAI_DEST_DPCON: The queue is set in schedule mode and does not generate
 *	FQDAN notifications, but is connected to the specified DPCON object;
 *	user is expected to dequeue from the DPCON channel
 */
enum dpdmai_dest {
	DPDMAI_DEST_NONE = 0,
	DPDMAI_DEST_DPIO = 1,
	DPDMAI_DEST_DPCON = 2
};

/**
 * struct dpdmai_dest_cfg - Structure representing DPDMAI destination parameters
 * @dest_type: Destination type
 * @dest_id: Either DPIO ID or DPCON ID, depending on the destination type
 * @priority: Priority selection within the DPIO or DPCON channel; valid values
 *	are 0-1 or 0-7, depending on the number of priorities in that
 *	channel; not relevant for 'DPDMAI_DEST_NONE' option
 */
struct dpdmai_dest_cfg {
	enum dpdmai_dest dest_type;
	int dest_id;
	u8 priority;
};

/**
 * struct dpdmai_rx_queue_cfg - DPDMAI RX queue configuration
 * @options: Flags representing the suggested modifications to the queue;
 *	Use any combination of 'DPDMAI_QUEUE_OPT_<X>' flags
 * @user_ctx: User context value provided in the frame descriptor of each
 *	dequeued frame;
 *	valid only if 'DPDMAI_QUEUE_OPT_USER_CTX' is contained in 'options'
 * @dest_cfg: Queue destination parameters;
 *	valid only if 'DPDMAI_QUEUE_OPT_DEST' is contained in 'options'
 */
struct dpdmai_rx_queue_cfg {
	struct dpdmai_dest_cfg dest_cfg;
	u32 options;
	u64 user_ctx;

};

/**
 * struct dpdmai_rx_queue_attr - Structure representing attributes of Rx queues
 * @user_ctx:  User context value provided in the frame descriptor of each
 *	 dequeued frame
 * @dest_cfg: Queue destination configuration
 * @fqid: Virtual FQID value to be used for dequeue operations
 */
struct dpdmai_rx_queue_attr {
	struct dpdmai_dest_cfg	dest_cfg;
	u64 user_ctx;
	u32 fqid;
};

int dpdmai_open(struct fsl_mc_io *mc_io, u32 cmd_flags,
		int dpdmai_id, u16 *token);
int dpdmai_close(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token);
int dpdmai_create(struct fsl_mc_io *mc_io, u32 cmd_flags,
		  const struct dpdmai_cfg *cfg, u16 *token);
int dpdmai_enable(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token);
int dpdmai_disable(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token);
int dpdmai_reset(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token);
int dpdmai_get_attributes(struct fsl_mc_io *mc_io, u32 cmd_flags,
			  u16 token, struct dpdmai_attr	*attr);
int dpdmai_set_rx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			u8 priority, const struct dpdmai_rx_queue_cfg *cfg);
int dpdmai_get_rx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
			u8 priority, struct dpdmai_rx_queue_attr *attr);
int dpdmai_get_tx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags,
			u16 token, u8 priority, u32 *fqid);

#endif /* __FSL_DPDMAI_H */
