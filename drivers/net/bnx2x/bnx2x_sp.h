/* bnx2x_sp.h: Broadcom Everest network driver.
 *
 * Copyright 2011 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Vladislav Zolotarov
 *
 */
#ifndef BNX2X_SP_VERBS
#define BNX2X_SP_VERBS

struct bnx2x;
struct eth_context;

/* Bits representing general command's configuration */
enum {
	RAMROD_TX,
	RAMROD_RX,
	/* Wait until all pending commands complete */
	RAMROD_COMP_WAIT,
	/* Don't send a ramrod, only update a registry */
	RAMROD_DRV_CLR_ONLY,
	/* Configure HW according to the current object state */
	RAMROD_RESTORE,
	 /* Execute the next command now */
	RAMROD_EXEC,
	/*
	 * Don't add a new command and continue execution of posponed
	 * commands. If not set a new command will be added to the
	 * pending commands list.
	 */
	RAMROD_CONT,
};

typedef enum {
	BNX2X_OBJ_TYPE_RX,
	BNX2X_OBJ_TYPE_TX,
	BNX2X_OBJ_TYPE_RX_TX,
} bnx2x_obj_type;

/* Filtering states */
enum {
	BNX2X_FILTER_MAC_PENDING,
	BNX2X_FILTER_VLAN_PENDING,
	BNX2X_FILTER_VLAN_MAC_PENDING,
	BNX2X_FILTER_RX_MODE_PENDING,
	BNX2X_FILTER_RX_MODE_SCHED,
	BNX2X_FILTER_ISCSI_ETH_START_SCHED,
	BNX2X_FILTER_ISCSI_ETH_STOP_SCHED,
	BNX2X_FILTER_FCOE_ETH_START_SCHED,
	BNX2X_FILTER_FCOE_ETH_STOP_SCHED,
	BNX2X_FILTER_MCAST_PENDING,
	BNX2X_FILTER_MCAST_SCHED,
	BNX2X_FILTER_RSS_CONF_PENDING,
};

struct bnx2x_raw_obj {
	u8		func_id;

	/* Queue params */
	u8		cl_id;
	u32		cid;

	/* Ramrod data buffer params */
	void		*rdata;
	dma_addr_t	rdata_mapping;

	/* Ramrod state params */
	int		state;   /* "ramrod is pending" state bit */
	unsigned long	*pstate; /* pointer to state buffer */

	bnx2x_obj_type	obj_type;

	int (*wait_comp)(struct bnx2x *bp,
			 struct bnx2x_raw_obj *o);

	bool (*check_pending)(struct bnx2x_raw_obj *o);
	void (*clear_pending)(struct bnx2x_raw_obj *o);
	void (*set_pending)(struct bnx2x_raw_obj *o);
};

/************************* VLAN-MAC commands related parameters ***************/
struct bnx2x_mac_ramrod_data {
	u8 mac[ETH_ALEN];
};

struct bnx2x_vlan_ramrod_data {
	u16 vlan;
};

struct bnx2x_vlan_mac_ramrod_data {
	u8 mac[ETH_ALEN];
	u16 vlan;
};

union bnx2x_classification_ramrod_data {
	struct bnx2x_mac_ramrod_data mac;
	struct bnx2x_vlan_ramrod_data vlan;
	struct bnx2x_vlan_mac_ramrod_data vlan_mac;
};

/* VLAN_MAC commands */
enum bnx2x_vlan_mac_cmd {
	BNX2X_VLAN_MAC_ADD,
	BNX2X_VLAN_MAC_DEL,
	BNX2X_VLAN_MAC_MOVE,
};

struct bnx2x_vlan_mac_data {
	/* Requested command: BNX2X_VLAN_MAC_XX */
	enum bnx2x_vlan_mac_cmd cmd;
	/*
	 * used to contain the data related vlan_mac_flags bits from
	 * ramrod parameters.
	 */
	unsigned long vlan_mac_flags;

	/* Needed for MOVE command */
	struct bnx2x_vlan_mac_obj *target_obj;

	union bnx2x_classification_ramrod_data u;
};

/*************************** Exe Queue obj ************************************/
union bnx2x_exe_queue_cmd_data {
	struct bnx2x_vlan_mac_data vlan_mac;

	struct {
		/* TODO */
	} mcast;
};

struct bnx2x_exeq_elem {
	struct list_head		link;

	/* Length of this element in the exe_chunk. */
	int				cmd_len;

	union bnx2x_exe_queue_cmd_data	cmd_data;
};

union bnx2x_qable_obj;

union bnx2x_exeq_comp_elem {
	union event_ring_elem *elem;
};

struct bnx2x_exe_queue_obj;

typedef int (*exe_q_validate)(struct bnx2x *bp,
			      union bnx2x_qable_obj *o,
			      struct bnx2x_exeq_elem *elem);

/**
 * @return positive is entry was optimized, 0 - if not, negative
 *         in case of an error.
 */
typedef int (*exe_q_optimize)(struct bnx2x *bp,
			      union bnx2x_qable_obj *o,
			      struct bnx2x_exeq_elem *elem);
typedef int (*exe_q_execute)(struct bnx2x *bp,
			     union bnx2x_qable_obj *o,
			     struct list_head *exe_chunk,
			     unsigned long *ramrod_flags);
typedef struct bnx2x_exeq_elem *
			(*exe_q_get)(struct bnx2x_exe_queue_obj *o,
				     struct bnx2x_exeq_elem *elem);

struct bnx2x_exe_queue_obj {
	/*
	 * Commands pending for an execution.
	 */
	struct list_head	exe_queue;

	/*
	 * Commands pending for an completion.
	 */
	struct list_head	pending_comp;

	spinlock_t		lock;

	/* Maximum length of commands' list for one execution */
	int			exe_chunk_len;

	union bnx2x_qable_obj	*owner;

	/****** Virtual functions ******/
	/**
	 * Called before commands execution for commands that are really
	 * going to be executed (after 'optimize').
	 *
	 * Must run under exe_queue->lock
	 */
	exe_q_validate		validate;


	/**
	 * This will try to cancel the current pending commands list
	 * considering the new command.
	 *
	 * Must run under exe_queue->lock
	 */
	exe_q_optimize		optimize;

	/**
	 * Run the next commands chunk (owner specific).
	 */
	exe_q_execute		execute;

	/**
	 * Return the exe_queue element containing the specific command
	 * if any. Otherwise return NULL.
	 */
	exe_q_get		get;
};
/***************** Classification verbs: Set/Del MAC/VLAN/VLAN-MAC ************/
/*
 * Element in the VLAN_MAC registry list having all currenty configured
 * rules.
 */
struct bnx2x_vlan_mac_registry_elem {
	struct list_head	link;

	/*
	 * Used to store the cam offset used for the mac/vlan/vlan-mac.
	 * Relevant for 57710 and 57711 only. VLANs and MACs share the
	 * same CAM for these chips.
	 */
	int			cam_offset;

	/* Needed for DEL and RESTORE flows */
	unsigned long		vlan_mac_flags;

	union bnx2x_classification_ramrod_data u;
};

/* Bits representing VLAN_MAC commands specific flags */
enum {
	BNX2X_UC_LIST_MAC,
	BNX2X_ETH_MAC,
	BNX2X_ISCSI_ETH_MAC,
	BNX2X_NETQ_ETH_MAC,
	BNX2X_DONT_CONSUME_CAM_CREDIT,
	BNX2X_DONT_CONSUME_CAM_CREDIT_DEST,
};

struct bnx2x_vlan_mac_ramrod_params {
	/* Object to run the command from */
	struct bnx2x_vlan_mac_obj *vlan_mac_obj;

	/* General command flags: COMP_WAIT, etc. */
	unsigned long ramrod_flags;

	/* Command specific configuration request */
	struct bnx2x_vlan_mac_data user_req;
};

struct bnx2x_vlan_mac_obj {
	struct bnx2x_raw_obj raw;

	/* Bookkeeping list: will prevent the addition of already existing
	 * entries.
	 */
	struct list_head		head;

	/* TODO: Add it's initialization in the init functions */
	struct bnx2x_exe_queue_obj	exe_queue;

	/* MACs credit pool */
	struct bnx2x_credit_pool_obj	*macs_pool;

	/* VLANs credit pool */
	struct bnx2x_credit_pool_obj	*vlans_pool;

	/* RAMROD command to be used */
	int				ramrod_cmd;

	/**
	 * Checks if ADD-ramrod with the given params may be performed.
	 *
	 * @return zero if the element may be added
	 */

	int (*check_add)(struct bnx2x_vlan_mac_obj *o,
			 union bnx2x_classification_ramrod_data *data);

	/**
	 * Checks if DEL-ramrod with the given params may be performed.
	 *
	 * @return true if the element may be deleted
	 */
	struct bnx2x_vlan_mac_registry_elem *
		(*check_del)(struct bnx2x_vlan_mac_obj *o,
			     union bnx2x_classification_ramrod_data *data);

	/**
	 * Checks if DEL-ramrod with the given params may be performed.
	 *
	 * @return true if the element may be deleted
	 */
	bool (*check_move)(struct bnx2x_vlan_mac_obj *src_o,
			   struct bnx2x_vlan_mac_obj *dst_o,
			   union bnx2x_classification_ramrod_data *data);

	/**
	 *  Update the relevant credit object(s) (consume/return
	 *  correspondingly).
	 */
	bool (*get_credit)(struct bnx2x_vlan_mac_obj *o);
	bool (*put_credit)(struct bnx2x_vlan_mac_obj *o);
	bool (*get_cam_offset)(struct bnx2x_vlan_mac_obj *o, int *offset);
	bool (*put_cam_offset)(struct bnx2x_vlan_mac_obj *o, int offset);

	/**
	 * Configures one rule in the ramrod data buffer.
	 */
	void (*set_one_rule)(struct bnx2x *bp,
			     struct bnx2x_vlan_mac_obj *o,
			     struct bnx2x_exeq_elem *elem, int rule_idx,
			     int cam_offset);

	/**
	*  Delete all configured elements having the given
	*  vlan_mac_flags specification. Assumes no pending for
	*  execution commands. Will schedule all all currently
	*  configured MACs/VLANs/VLAN-MACs matching the vlan_mac_flags
	*  specification for deletion and will use the given
	*  ramrod_flags for the last DEL operation.
	 *
	 * @param bp
	 * @param o
	 * @param ramrod_flags RAMROD_XX flags
	 *
	 * @return 0 if the last operation has completed successfully
	 *         and there are no more elements left, positive value
	 *         if there are pending for completion commands,
	 *         negative value in case of failure.
	 */
	int (*delete_all)(struct bnx2x *bp,
			  struct bnx2x_vlan_mac_obj *o,
			  unsigned long *vlan_mac_flags,
			  unsigned long *ramrod_flags);

	/**
	 * Reconfigures the next MAC/VLAN/VLAN-MAC element from the previously
	 * configured elements list.
	 *
	 * @param bp
	 * @param p Command parameters (RAMROD_COMP_WAIT bit in
	 *          ramrod_flags is only taken into an account)
	 * @param ppos a pointer to the cooky that should be given back in the
	 *        next call to make function handle the next element. If
	 *        *ppos is set to NULL it will restart the iterator.
	 *        If returned *ppos == NULL this means that the last
	 *        element has been handled.
	 *
	 * @return int
	 */
	int (*restore)(struct bnx2x *bp,
		       struct bnx2x_vlan_mac_ramrod_params *p,
		       struct bnx2x_vlan_mac_registry_elem **ppos);

	/**
	 * Should be called on a completion arival.
	 *
	 * @param bp
	 * @param o
	 * @param cqe Completion element we are handling
	 * @param ramrod_flags if RAMROD_CONT is set the next bulk of
	 *		       pending commands will be executed.
	 *		       RAMROD_DRV_CLR_ONLY and RAMROD_RESTORE
	 *		       may also be set if needed.
	 *
	 * @return 0 if there are neither pending nor waiting for
	 *         completion commands. Positive value if there are
	 *         pending for execution or for completion commands.
	 *         Negative value in case of an error (including an
	 *         error in the cqe).
	 */
	int (*complete)(struct bnx2x *bp, struct bnx2x_vlan_mac_obj *o,
			union event_ring_elem *cqe,
			unsigned long *ramrod_flags);

	/**
	 * Wait for completion of all commands. Don't schedule new ones,
	 * just wait. It assumes that the completion code will schedule
	 * for new commands.
	 */
	int (*wait)(struct bnx2x *bp, struct bnx2x_vlan_mac_obj *o);
};

/** RX_MODE verbs:DROP_ALL/ACCEPT_ALL/ACCEPT_ALL_MULTI/ACCEPT_ALL_VLAN/NORMAL */

/* RX_MODE ramrod spesial flags: set in rx_mode_flags field in
 * a bnx2x_rx_mode_ramrod_params.
 */
enum {
	BNX2X_RX_MODE_FCOE_ETH,
	BNX2X_RX_MODE_ISCSI_ETH,
};

enum {
	BNX2X_ACCEPT_UNICAST,
	BNX2X_ACCEPT_MULTICAST,
	BNX2X_ACCEPT_ALL_UNICAST,
	BNX2X_ACCEPT_ALL_MULTICAST,
	BNX2X_ACCEPT_BROADCAST,
	BNX2X_ACCEPT_UNMATCHED,
	BNX2X_ACCEPT_ANY_VLAN
};

struct bnx2x_rx_mode_ramrod_params {
	struct bnx2x_rx_mode_obj *rx_mode_obj;
	unsigned long *pstate;
	int state;
	u8 cl_id;
	u32 cid;
	u8 func_id;
	unsigned long ramrod_flags;
	unsigned long rx_mode_flags;

	/*
	 * rdata is either a pointer to eth_filter_rules_ramrod_data(e2) or to
	 * a tstorm_eth_mac_filter_config (e1x).
	 */
	void *rdata;
	dma_addr_t rdata_mapping;

	/* Rx mode settings */
	unsigned long rx_accept_flags;

	/* internal switching settings */
	unsigned long tx_accept_flags;
};

struct bnx2x_rx_mode_obj {
	int (*config_rx_mode)(struct bnx2x *bp,
			      struct bnx2x_rx_mode_ramrod_params *p);

	int (*wait_comp)(struct bnx2x *bp,
			 struct bnx2x_rx_mode_ramrod_params *p);
};

/********************** Set multicast group ***********************************/

struct bnx2x_mcast_list_elem {
	struct list_head link;
	u8 *mac;
};

union bnx2x_mcast_config_data {
	u8 *mac;
	u8 bin; /* used in a RESTORE flow */
};

struct bnx2x_mcast_ramrod_params {
	struct bnx2x_mcast_obj *mcast_obj;

	/* Relevant options are RAMROD_COMP_WAIT and RAMROD_DRV_CLR_ONLY */
	unsigned long ramrod_flags;

	struct list_head mcast_list; /* list of struct bnx2x_mcast_list_elem */
	/** TODO:
	 *      - rename it to macs_num.
	 *      - Add a new command type for handling pending commands
	 *        (remove "zero semantics").
	 *
	 *  Length of mcast_list. If zero and ADD_CONT command - post
	 *  pending commands.
	 */
	int mcast_list_len;
};

enum {
	BNX2X_MCAST_CMD_ADD,
	BNX2X_MCAST_CMD_CONT,
	BNX2X_MCAST_CMD_DEL,
	BNX2X_MCAST_CMD_RESTORE,
};

struct bnx2x_mcast_obj {
	struct bnx2x_raw_obj raw;

	union {
		struct {
		#define BNX2X_MCAST_BINS_NUM	256
		#define BNX2X_MCAST_VEC_SZ	(BNX2X_MCAST_BINS_NUM / 64)
			u64 vec[BNX2X_MCAST_VEC_SZ];

			/** Number of BINs to clear. Should be updated
			 *  immediately when a command arrives in order to
			 *  properly create DEL commands.
			 */
			int num_bins_set;
		} aprox_match;

		struct {
			struct list_head macs;
			int num_macs_set;
		} exact_match;
	} registry;

	/* Pending commands */
	struct list_head pending_cmds_head;

	/* A state that is set in raw.pstate, when there are pending commands */
	int sched_state;

	/* Maximal number of mcast MACs configured in one command */
	int max_cmd_len;

	/* Total number of currently pending MACs to configure: both
	 * in the pending commands list and in the current command.
	 */
	int total_pending_num;

	u8 engine_id;

	/**
	 * @param cmd command to execute (BNX2X_MCAST_CMD_X, see above)
	 */
	int (*config_mcast)(struct bnx2x *bp,
				struct bnx2x_mcast_ramrod_params *p, int cmd);

	/**
	 * Fills the ramrod data during the RESTORE flow.
	 *
	 * @param bp
	 * @param o
	 * @param start_idx Registry index to start from
	 * @param rdata_idx Index in the ramrod data to start from
	 *
	 * @return -1 if we handled the whole registry or index of the last
	 *         handled registry element.
	 */
	int (*hdl_restore)(struct bnx2x *bp, struct bnx2x_mcast_obj *o,
			   int start_bin, int *rdata_idx);

	int (*enqueue_cmd)(struct bnx2x *bp, struct bnx2x_mcast_obj *o,
			   struct bnx2x_mcast_ramrod_params *p, int cmd);

	void (*set_one_rule)(struct bnx2x *bp,
			     struct bnx2x_mcast_obj *o, int idx,
			     union bnx2x_mcast_config_data *cfg_data, int cmd);

	/** Checks if there are more mcast MACs to be set or a previous
	 *  command is still pending.
	 */
	bool (*check_pending)(struct bnx2x_mcast_obj *o);

	/**
	 * Set/Clear/Check SCHEDULED state of the object
	 */
	void (*set_sched)(struct bnx2x_mcast_obj *o);
	void (*clear_sched)(struct bnx2x_mcast_obj *o);
	bool (*check_sched)(struct bnx2x_mcast_obj *o);

	/* Wait until all pending commands complete */
	int (*wait_comp)(struct bnx2x *bp, struct bnx2x_mcast_obj *o);

	/**
	 * Handle the internal object counters needed for proper
	 * commands handling. Checks that the provided parameters are
	 * feasible.
	 */
	int (*validate)(struct bnx2x *bp,
			struct bnx2x_mcast_ramrod_params *p, int cmd);

	/**
	 * Restore the values of internal counters in case of a failure.
	 */
	void (*revert)(struct bnx2x *bp,
		       struct bnx2x_mcast_ramrod_params *p,
		       int old_num_bins);

	int (*get_registry_size)(struct bnx2x_mcast_obj *o);
	void (*set_registry_size)(struct bnx2x_mcast_obj *o, int n);
};

/*************************** Credit handling **********************************/
struct bnx2x_credit_pool_obj {

	/* Current amount of credit in the pool */
	atomic_t	credit;

	/* Maximum allowed credit. put() will check against it. */
	int		pool_sz;

	/*
	 *  Allocate a pool table statically.
	 *
	 *  Currently the mamimum allowed size is MAX_MAC_CREDIT_E2(272)
	 *
	 *  The set bit in the table will mean that the entry is available.
	 */
#define BNX2X_POOL_VEC_SIZE	(MAX_MAC_CREDIT_E2 / 64)
	u64		pool_mirror[BNX2X_POOL_VEC_SIZE];

	/* Base pool offset (initialized differently */
	int		base_pool_offset;

	/**
	 * Get the next free pool entry.
	 *
	 * @return true if there was a free entry in the pool
	 */
	bool (*get_entry)(struct bnx2x_credit_pool_obj *o, int *entry);

	/**
	 * Return the entry back to the pool.
	 *
	 * @return true if entry is legal and has been successfully
	 *         returned to the pool.
	 */
	bool (*put_entry)(struct bnx2x_credit_pool_obj *o, int entry);

	/**
	 * Get the requested amount of credit from the pool.
	 *
	 * @param cnt Amount of requested credit
	 * @return true if the operation is successful
	 */
	bool (*get)(struct bnx2x_credit_pool_obj *o, int cnt);

	/**
	 * Returns the credit to the pool.
	 *
	 * @param cnt Amount of credit to return
	 * @return true if the operation is successful
	 */
	bool (*put)(struct bnx2x_credit_pool_obj *o, int cnt);

	/**
	 * Reads the current amount of credit.
	 */
	int (*check)(struct bnx2x_credit_pool_obj *o);
};

/*************************** RSS configuration ********************************/
enum {
	/* RSS_MODE bits are mutually exclusive */
	BNX2X_RSS_MODE_DISABLED,
	BNX2X_RSS_MODE_REGULAR,
	BNX2X_RSS_MODE_VLAN_PRI,
	BNX2X_RSS_MODE_E1HOV_PRI,
	BNX2X_RSS_MODE_IP_DSCP,

	BNX2X_RSS_SET_SRCH, /* Setup searcher, E1x specific flag */

	BNX2X_RSS_IPV4,
	BNX2X_RSS_IPV4_TCP,
	BNX2X_RSS_IPV6,
	BNX2X_RSS_IPV6_TCP,
};

struct bnx2x_config_rss_params {
	struct bnx2x_rss_config_obj *rss_obj;

	/* may have RAMROD_COMP_WAIT set only */
	unsigned long	ramrod_flags;

	/* BNX2X_RSS_X bits */
	unsigned long	rss_flags;

	/* Number hash bits to take into an account */
	u8		rss_result_mask;

	/* Indirection table */
	u8		ind_table[T_ETH_INDIRECTION_TABLE_SIZE];

	/* RSS hash values */
	u32		rss_key[10];

	/* valid only iff BNX2X_RSS_UPDATE_TOE is set */
	u16		toe_rss_bitmap;
};

struct bnx2x_rss_config_obj {
	struct bnx2x_raw_obj	raw;

	/* RSS engine to use */
	u8			engine_id;

	/* Last configured indirection table */
	u8			ind_table[T_ETH_INDIRECTION_TABLE_SIZE];

	int (*config_rss)(struct bnx2x *bp,
			  struct bnx2x_config_rss_params *p);
};

/*********************** Queue state update ***********************************/

/* UPDATE command options */
enum {
	BNX2X_Q_UPDATE_IN_VLAN_REM,
	BNX2X_Q_UPDATE_IN_VLAN_REM_CHNG,
	BNX2X_Q_UPDATE_OUT_VLAN_REM,
	BNX2X_Q_UPDATE_OUT_VLAN_REM_CHNG,
	BNX2X_Q_UPDATE_ANTI_SPOOF,
	BNX2X_Q_UPDATE_ANTI_SPOOF_CHNG,
	BNX2X_Q_UPDATE_ACTIVATE,
	BNX2X_Q_UPDATE_ACTIVATE_CHNG,
	BNX2X_Q_UPDATE_DEF_VLAN_EN,
	BNX2X_Q_UPDATE_DEF_VLAN_EN_CHNG,
	BNX2X_Q_UPDATE_SILENT_VLAN_REM_CHNG,
	BNX2X_Q_UPDATE_SILENT_VLAN_REM
};

/* Allowed Queue states */
enum bnx2x_q_state {
	BNX2X_Q_STATE_RESET,
	BNX2X_Q_STATE_INITIALIZED,
	BNX2X_Q_STATE_ACTIVE,
	BNX2X_Q_STATE_MULTI_COS,
	BNX2X_Q_STATE_MCOS_TERMINATED,
	BNX2X_Q_STATE_INACTIVE,
	BNX2X_Q_STATE_STOPPED,
	BNX2X_Q_STATE_TERMINATED,
	BNX2X_Q_STATE_FLRED,
	BNX2X_Q_STATE_MAX,
};

/* Allowed commands */
enum bnx2x_queue_cmd {
	BNX2X_Q_CMD_INIT,
	BNX2X_Q_CMD_SETUP,
	BNX2X_Q_CMD_SETUP_TX_ONLY,
	BNX2X_Q_CMD_DEACTIVATE,
	BNX2X_Q_CMD_ACTIVATE,
	BNX2X_Q_CMD_UPDATE,
	BNX2X_Q_CMD_UPDATE_TPA,
	BNX2X_Q_CMD_HALT,
	BNX2X_Q_CMD_CFC_DEL,
	BNX2X_Q_CMD_TERMINATE,
	BNX2X_Q_CMD_EMPTY,
	BNX2X_Q_CMD_MAX,
};

/* queue SETUP + INIT flags */
enum {
	BNX2X_Q_FLG_TPA,
	BNX2X_Q_FLG_TPA_IPV6,
	BNX2X_Q_FLG_STATS,
	BNX2X_Q_FLG_ZERO_STATS,
	BNX2X_Q_FLG_ACTIVE,
	BNX2X_Q_FLG_OV,
	BNX2X_Q_FLG_VLAN,
	BNX2X_Q_FLG_COS,
	BNX2X_Q_FLG_HC,
	BNX2X_Q_FLG_HC_EN,
	BNX2X_Q_FLG_DHC,
	BNX2X_Q_FLG_FCOE,
	BNX2X_Q_FLG_LEADING_RSS,
	BNX2X_Q_FLG_MCAST,
	BNX2X_Q_FLG_DEF_VLAN,
	BNX2X_Q_FLG_TX_SWITCH,
	BNX2X_Q_FLG_TX_SEC,
	BNX2X_Q_FLG_ANTI_SPOOF,
	BNX2X_Q_FLG_SILENT_VLAN_REM
};

/* Queue type options: queue type may be a compination of below. */
enum bnx2x_q_type {
	/** TODO: Consider moving both these flags into the init()
	 *        ramrod params.
	 */
	BNX2X_Q_TYPE_HAS_RX,
	BNX2X_Q_TYPE_HAS_TX,
};

#define BNX2X_PRIMARY_CID_INDEX			0
#define BNX2X_MULTI_TX_COS_E1X			1
#define BNX2X_MULTI_TX_COS_E2_E3A0		2
#define BNX2X_MULTI_TX_COS_E3B0			3
#define BNX2X_MULTI_TX_COS			BNX2X_MULTI_TX_COS_E3B0


struct bnx2x_queue_init_params {
	struct {
		unsigned long	flags;
		u16		hc_rate;
		u8		fw_sb_id;
		u8		sb_cq_index;
	} tx;

	struct {
		unsigned long	flags;
		u16		hc_rate;
		u8		fw_sb_id;
		u8		sb_cq_index;
	} rx;

	/* CID context in the host memory */
	struct eth_context *cxts[BNX2X_MULTI_TX_COS];

	/* maximum number of cos supported by hardware */
	u8 max_cos;
};

struct bnx2x_queue_terminate_params {
	/* index within the tx_only cids of this queue object */
	u8 cid_index;
};

struct bnx2x_queue_cfc_del_params {
	/* index within the tx_only cids of this queue object */
	u8 cid_index;
};

struct bnx2x_queue_update_params {
	unsigned long	update_flags; /* BNX2X_Q_UPDATE_XX bits */
	u16		def_vlan;
	u16		silent_removal_value;
	u16		silent_removal_mask;
/* index within the tx_only cids of this queue object */
	u8		cid_index;
};

struct rxq_pause_params {
	u16		bd_th_lo;
	u16		bd_th_hi;
	u16		rcq_th_lo;
	u16		rcq_th_hi;
	u16		sge_th_lo; /* valid iff BNX2X_Q_FLG_TPA */
	u16		sge_th_hi; /* valid iff BNX2X_Q_FLG_TPA */
	u16		pri_map;
};

/* general */
struct bnx2x_general_setup_params {
	/* valid iff BNX2X_Q_FLG_STATS */
	u8		stat_id;

	u8		spcl_id;
	u16		mtu;
	u8		cos;
};

struct bnx2x_rxq_setup_params {
	/* dma */
	dma_addr_t	dscr_map;
	dma_addr_t	sge_map;
	dma_addr_t	rcq_map;
	dma_addr_t	rcq_np_map;

	u16		drop_flags;
	u16		buf_sz;
	u8		fw_sb_id;
	u8		cl_qzone_id;

	/* valid iff BNX2X_Q_FLG_TPA */
	u16		tpa_agg_sz;
	u16		sge_buf_sz;
	u8		max_sges_pkt;
	u8		max_tpa_queues;
	u8		rss_engine_id;

	u8		cache_line_log;

	u8		sb_cq_index;

	/* valid iff BXN2X_Q_FLG_SILENT_VLAN_REM */
	u16 silent_removal_value;
	u16 silent_removal_mask;
};

struct bnx2x_txq_setup_params {
	/* dma */
	dma_addr_t	dscr_map;

	u8		fw_sb_id;
	u8		sb_cq_index;
	u8		cos;		/* valid iff BNX2X_Q_FLG_COS */
	u16		traffic_type;
	/* equals to the leading rss client id, used for TX classification*/
	u8		tss_leading_cl_id;

	/* valid iff BNX2X_Q_FLG_DEF_VLAN */
	u16		default_vlan;
};

struct bnx2x_queue_setup_params {
	struct bnx2x_general_setup_params gen_params;
	struct bnx2x_txq_setup_params txq_params;
	struct bnx2x_rxq_setup_params rxq_params;
	struct rxq_pause_params pause_params;
	unsigned long flags;
};

struct bnx2x_queue_setup_tx_only_params {
	struct bnx2x_general_setup_params	gen_params;
	struct bnx2x_txq_setup_params		txq_params;
	unsigned long				flags;
	/* index within the tx_only cids of this queue object */
	u8					cid_index;
};

struct bnx2x_queue_state_params {
	struct bnx2x_queue_sp_obj *q_obj;

	/* Current command */
	enum bnx2x_queue_cmd cmd;

	/* may have RAMROD_COMP_WAIT set only */
	unsigned long ramrod_flags;

	/* Params according to the current command */
	union {
		struct bnx2x_queue_update_params	update;
		struct bnx2x_queue_setup_params		setup;
		struct bnx2x_queue_init_params		init;
		struct bnx2x_queue_setup_tx_only_params	tx_only;
		struct bnx2x_queue_terminate_params	terminate;
		struct bnx2x_queue_cfc_del_params	cfc_del;
	} params;
};

struct bnx2x_queue_sp_obj {
	u32		cids[BNX2X_MULTI_TX_COS];
	u8		cl_id;
	u8		func_id;

	/*
	 * number of traffic classes supported by queue.
	 * The primary connection of the queue suppotrs the first traffic
	 * class. Any further traffic class is suppoted by a tx-only
	 * connection.
	 *
	 * Therefore max_cos is also a number of valid entries in the cids
	 * array.
	 */
	u8 max_cos;
	u8 num_tx_only, next_tx_only;

	enum bnx2x_q_state state, next_state;

	/* bits from enum bnx2x_q_type */
	unsigned long	type;

	/* BNX2X_Q_CMD_XX bits. This object implements "one
	 * pending" paradigm but for debug and tracing purposes it's
	 * more convinient to have different bits for different
	 * commands.
	 */
	unsigned long	pending;

	/* Buffer to use as a ramrod data and its mapping */
	void		*rdata;
	dma_addr_t	rdata_mapping;

	/**
	 * Performs one state change according to the given parameters.
	 *
	 * @return 0 in case of success and negative value otherwise.
	 */
	int (*send_cmd)(struct bnx2x *bp,
			struct bnx2x_queue_state_params *params);

	/**
	 * Sets the pending bit according to the requested transition.
	 */
	int (*set_pending)(struct bnx2x_queue_sp_obj *o,
			   struct bnx2x_queue_state_params *params);

	/**
	 * Checks that the requested state transition is legal.
	 */
	int (*check_transition)(struct bnx2x *bp,
				struct bnx2x_queue_sp_obj *o,
				struct bnx2x_queue_state_params *params);

	/**
	 * Completes the pending command.
	 */
	int (*complete_cmd)(struct bnx2x *bp,
			    struct bnx2x_queue_sp_obj *o,
			    enum bnx2x_queue_cmd);

	int (*wait_comp)(struct bnx2x *bp,
			 struct bnx2x_queue_sp_obj *o,
			 enum bnx2x_queue_cmd cmd);
};

/********************** Function state update *********************************/
/* Allowed Function states */
enum bnx2x_func_state {
	BNX2X_F_STATE_RESET,
	BNX2X_F_STATE_INITIALIZED,
	BNX2X_F_STATE_STARTED,
	BNX2X_F_STATE_TX_STOPPED,
	BNX2X_F_STATE_MAX,
};

/* Allowed Function commands */
enum bnx2x_func_cmd {
	BNX2X_F_CMD_HW_INIT,
	BNX2X_F_CMD_START,
	BNX2X_F_CMD_STOP,
	BNX2X_F_CMD_HW_RESET,
	BNX2X_F_CMD_TX_STOP,
	BNX2X_F_CMD_TX_START,
	BNX2X_F_CMD_MAX,
};

struct bnx2x_func_hw_init_params {
	/* A load phase returned by MCP.
	 *
	 * May be:
	 *		FW_MSG_CODE_DRV_LOAD_COMMON_CHIP
	 *		FW_MSG_CODE_DRV_LOAD_COMMON
	 *		FW_MSG_CODE_DRV_LOAD_PORT
	 *		FW_MSG_CODE_DRV_LOAD_FUNCTION
	 */
	u32 load_phase;
};

struct bnx2x_func_hw_reset_params {
	/* A load phase returned by MCP.
	 *
	 * May be:
	 *		FW_MSG_CODE_DRV_LOAD_COMMON_CHIP
	 *		FW_MSG_CODE_DRV_LOAD_COMMON
	 *		FW_MSG_CODE_DRV_LOAD_PORT
	 *		FW_MSG_CODE_DRV_LOAD_FUNCTION
	 */
	u32 reset_phase;
};

struct bnx2x_func_start_params {
	/* Multi Function mode:
	 *	- Single Function
	 *	- Switch Dependent
	 *	- Switch Independent
	 */
	u16 mf_mode;

	/* Switch Dependent mode outer VLAN tag */
	u16 sd_vlan_tag;

	/* Function cos mode */
	u8 network_cos_mode;
};

struct bnx2x_func_tx_start_params {
	struct priority_cos traffic_type_to_priority_cos[MAX_TRAFFIC_TYPES];
	u8 dcb_enabled;
	u8 dcb_version;
	u8 dont_add_pri_0_en;
};

struct bnx2x_func_state_params {
	struct bnx2x_func_sp_obj *f_obj;

	/* Current command */
	enum bnx2x_func_cmd cmd;

	/* may have RAMROD_COMP_WAIT set only */
	unsigned long	ramrod_flags;

	/* Params according to the current command */
	union {
		struct bnx2x_func_hw_init_params hw_init;
		struct bnx2x_func_hw_reset_params hw_reset;
		struct bnx2x_func_start_params start;
		struct bnx2x_func_tx_start_params tx_start;
	} params;
};

struct bnx2x_func_sp_drv_ops {
	/* Init tool + runtime initialization:
	 *      - Common Chip
	 *      - Common (per Path)
	 *      - Port
	 *      - Function phases
	 */
	int (*init_hw_cmn_chip)(struct bnx2x *bp);
	int (*init_hw_cmn)(struct bnx2x *bp);
	int (*init_hw_port)(struct bnx2x *bp);
	int (*init_hw_func)(struct bnx2x *bp);

	/* Reset Function HW: Common, Port, Function phases. */
	void (*reset_hw_cmn)(struct bnx2x *bp);
	void (*reset_hw_port)(struct bnx2x *bp);
	void (*reset_hw_func)(struct bnx2x *bp);

	/* Init/Free GUNZIP resources */
	int (*gunzip_init)(struct bnx2x *bp);
	void (*gunzip_end)(struct bnx2x *bp);

	/* Prepare/Release FW resources */
	int (*init_fw)(struct bnx2x *bp);
	void (*release_fw)(struct bnx2x *bp);
};

struct bnx2x_func_sp_obj {
	enum bnx2x_func_state	state, next_state;

	/* BNX2X_FUNC_CMD_XX bits. This object implements "one
	 * pending" paradigm but for debug and tracing purposes it's
	 * more convinient to have different bits for different
	 * commands.
	 */
	unsigned long		pending;

	/* Buffer to use as a ramrod data and its mapping */
	void			*rdata;
	dma_addr_t		rdata_mapping;

	/* this mutex validates that when pending flag is taken, the next
	 * ramrod to be sent will be the one set the pending bit
	 */
	struct mutex		one_pending_mutex;

	/* Driver interface */
	struct bnx2x_func_sp_drv_ops	*drv;

	/**
	 * Performs one state change according to the given parameters.
	 *
	 * @return 0 in case of success and negative value otherwise.
	 */
	int (*send_cmd)(struct bnx2x *bp,
			struct bnx2x_func_state_params *params);

	/**
	 * Checks that the requested state transition is legal.
	 */
	int (*check_transition)(struct bnx2x *bp,
				struct bnx2x_func_sp_obj *o,
				struct bnx2x_func_state_params *params);

	/**
	 * Completes the pending command.
	 */
	int (*complete_cmd)(struct bnx2x *bp,
			    struct bnx2x_func_sp_obj *o,
			    enum bnx2x_func_cmd cmd);

	int (*wait_comp)(struct bnx2x *bp, struct bnx2x_func_sp_obj *o,
			 enum bnx2x_func_cmd cmd);
};

/********************** Interfaces ********************************************/
/* Queueable objects set */
union bnx2x_qable_obj {
	struct bnx2x_vlan_mac_obj vlan_mac;
};
/************** Function state update *********/
void bnx2x_init_func_obj(struct bnx2x *bp,
			 struct bnx2x_func_sp_obj *obj,
			 void *rdata, dma_addr_t rdata_mapping,
			 struct bnx2x_func_sp_drv_ops *drv_iface);

int bnx2x_func_state_change(struct bnx2x *bp,
			    struct bnx2x_func_state_params *params);

enum bnx2x_func_state bnx2x_func_get_state(struct bnx2x *bp,
					   struct bnx2x_func_sp_obj *o);
/******************* Queue State **************/
void bnx2x_init_queue_obj(struct bnx2x *bp,
			  struct bnx2x_queue_sp_obj *obj, u8 cl_id, u32 *cids,
			  u8 cid_cnt, u8 func_id, void *rdata,
			  dma_addr_t rdata_mapping, unsigned long type);

int bnx2x_queue_state_change(struct bnx2x *bp,
			     struct bnx2x_queue_state_params *params);

/********************* VLAN-MAC ****************/
void bnx2x_init_mac_obj(struct bnx2x *bp,
			struct bnx2x_vlan_mac_obj *mac_obj,
			u8 cl_id, u32 cid, u8 func_id, void *rdata,
			dma_addr_t rdata_mapping, int state,
			unsigned long *pstate, bnx2x_obj_type type,
			struct bnx2x_credit_pool_obj *macs_pool);

void bnx2x_init_vlan_obj(struct bnx2x *bp,
			 struct bnx2x_vlan_mac_obj *vlan_obj,
			 u8 cl_id, u32 cid, u8 func_id, void *rdata,
			 dma_addr_t rdata_mapping, int state,
			 unsigned long *pstate, bnx2x_obj_type type,
			 struct bnx2x_credit_pool_obj *vlans_pool);

void bnx2x_init_vlan_mac_obj(struct bnx2x *bp,
			     struct bnx2x_vlan_mac_obj *vlan_mac_obj,
			     u8 cl_id, u32 cid, u8 func_id, void *rdata,
			     dma_addr_t rdata_mapping, int state,
			     unsigned long *pstate, bnx2x_obj_type type,
			     struct bnx2x_credit_pool_obj *macs_pool,
			     struct bnx2x_credit_pool_obj *vlans_pool);

int bnx2x_config_vlan_mac(struct bnx2x *bp,
			  struct bnx2x_vlan_mac_ramrod_params *p);

int bnx2x_vlan_mac_move(struct bnx2x *bp,
			struct bnx2x_vlan_mac_ramrod_params *p,
			struct bnx2x_vlan_mac_obj *dest_o);

/********************* RX MODE ****************/

void bnx2x_init_rx_mode_obj(struct bnx2x *bp,
			    struct bnx2x_rx_mode_obj *o);

/**
 * Send and RX_MODE ramrod according to the provided parameters.
 *
 * @param bp
 * @param p Command parameters
 *
 * @return 0 - if operation was successfull and there is no pending completions,
 *         positive number - if there are pending completions,
 *         negative - if there were errors
 */
int bnx2x_config_rx_mode(struct bnx2x *bp,
			 struct bnx2x_rx_mode_ramrod_params *p);

/****************** MULTICASTS ****************/

void bnx2x_init_mcast_obj(struct bnx2x *bp,
			  struct bnx2x_mcast_obj *mcast_obj,
			  u8 mcast_cl_id, u32 mcast_cid, u8 func_id,
			  u8 engine_id, void *rdata, dma_addr_t rdata_mapping,
			  int state, unsigned long *pstate,
			  bnx2x_obj_type type);

/**
 * Configure multicast MACs list. May configure a new list
 * provided in p->mcast_list (BNX2X_MCAST_CMD_ADD), clean up
 * (BNX2X_MCAST_CMD_DEL) or restore (BNX2X_MCAST_CMD_RESTORE) a current
 * configuration, continue to execute the pending commands
 * (BNX2X_MCAST_CMD_CONT).
 *
 * If previous command is still pending or if number of MACs to
 * configure is more that maximum number of MACs in one command,
 * the current command will be enqueued to the tail of the
 * pending commands list.
 *
 * @param bp
 * @param p
 * @param command to execute: BNX2X_MCAST_CMD_X
 *
 * @return 0 is operation was sucessfull and there are no pending completions,
 *         negative if there were errors, positive if there are pending
 *         completions.
 */
int bnx2x_config_mcast(struct bnx2x *bp,
		       struct bnx2x_mcast_ramrod_params *p, int cmd);

/****************** CREDIT POOL ****************/
void bnx2x_init_mac_credit_pool(struct bnx2x *bp,
				struct bnx2x_credit_pool_obj *p, u8 func_id,
				u8 func_num);
void bnx2x_init_vlan_credit_pool(struct bnx2x *bp,
				 struct bnx2x_credit_pool_obj *p, u8 func_id,
				 u8 func_num);


/****************** RSS CONFIGURATION ****************/
void bnx2x_init_rss_config_obj(struct bnx2x *bp,
			       struct bnx2x_rss_config_obj *rss_obj,
			       u8 cl_id, u32 cid, u8 func_id, u8 engine_id,
			       void *rdata, dma_addr_t rdata_mapping,
			       int state, unsigned long *pstate,
			       bnx2x_obj_type type);

/**
 * Updates RSS configuration according to provided parameters.
 *
 * @param bp
 * @param p
 *
 * @return 0 in case of success
 */
int bnx2x_config_rss(struct bnx2x *bp,
		     struct bnx2x_config_rss_params *p);

/**
 * Return the current ind_table configuration.
 *
 * @param bp
 * @param ind_table buffer to fill with the current indirection
 *                  table content. Should be at least
 *                  T_ETH_INDIRECTION_TABLE_SIZE bytes long.
 */
void bnx2x_get_rss_ind_table(struct bnx2x_rss_config_obj *rss_obj,
			     u8 *ind_table);

#endif /* BNX2X_SP_VERBS */
