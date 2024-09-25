/* SPDX-License-Identifier: GPL-2.0-only */
//Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

#ifndef __MHI_H
#define __MHI_H

#include <linux/msm_ep_pcie.h>
#include <linux/ipc_logging.h>
#include <linux/msm_mhi_dev.h>
#include <linux/mhi_dma.h>


/**
 * MHI control data structures alloted by the host, including
 * channel context array, event context array, command context and rings.
 */

/* Channel context state */
enum mhi_dev_ch_ctx_state {
	MHI_DEV_CH_STATE_DISABLED,
	MHI_DEV_CH_STATE_ENABLED,
	MHI_DEV_CH_STATE_RUNNING,
	MHI_DEV_CH_STATE_SUSPENDED,
	MHI_DEV_CH_STATE_STOP,
	MHI_DEV_CH_STATE_ERROR,
	MHI_DEV_CH_STATE_RESERVED,
	MHI_DEV_CH_STATE_32BIT = 0x7FFFFFFF
};

/* Channel type */
enum mhi_dev_ch_ctx_type {
	MHI_DEV_CH_TYPE_NONE,
	MHI_DEV_CH_TYPE_OUTBOUND_CHANNEL,
	MHI_DEV_CH_TYPE_INBOUND_CHANNEL,
	MHI_DEV_CH_RESERVED
};

/* Channel context type */
struct mhi_dev_ch_ctx {
	enum mhi_dev_ch_ctx_state	ch_state;
	enum mhi_dev_ch_ctx_type	ch_type;
	uint32_t			err_indx;
	uint64_t			rbase;
	uint64_t			rlen;
	uint64_t			rp;
	uint64_t			wp;
} __packed;

enum mhi_dev_ring_element_type_id {
	MHI_DEV_RING_EL_INVALID = 0,
	MHI_DEV_RING_EL_NOOP = 1,
	MHI_DEV_RING_EL_TRANSFER = 2,
	MHI_DEV_RING_EL_RESET = 16,
	MHI_DEV_RING_EL_STOP = 17,
	MHI_DEV_RING_EL_START = 18,
	MHI_DEV_RING_EL_MHI_STATE_CHG = 32,
	MHI_DEV_RING_EL_CMD_COMPLETION_EVT = 33,
	MHI_DEV_RING_EL_TRANSFER_COMPLETION_EVENT = 34,
	MHI_DEV_RING_EL_EE_STATE_CHANGE_NOTIFY = 64,
	MHI_DEV_RING_EL_CH_STATE_CHANGE_NOTIFY = 65,
	MHI_DEV_RING_EL_UNDEF
};

enum mhi_dev_ring_state {
	RING_STATE_UINT = 0,
	RING_STATE_IDLE,
	RING_STATE_PENDING,
};

enum mhi_dev_ring_type {
	RING_TYPE_CMD = 0,
	RING_TYPE_ER,
	RING_TYPE_CH,
	RING_TYPE_INVAL
};

/* Event context interrupt moderation */
enum mhi_dev_evt_ctx_int_mod_timer {
	MHI_DEV_EVT_INT_MODERATION_DISABLED
};

/* Event ring type */
enum mhi_dev_evt_ctx_event_ring_type {
	MHI_DEV_EVT_TYPE_DEFAULT,
	MHI_DEV_EVT_TYPE_VALID,
	MHI_DEV_EVT_RESERVED
};

/* Event ring context type */
struct mhi_dev_ev_ctx {
	uint32_t				res1:16;
	enum mhi_dev_evt_ctx_int_mod_timer	intmodt:16;
	enum mhi_dev_evt_ctx_event_ring_type	ertype;
	uint32_t				msivec;
	uint64_t				rbase;
	uint64_t				rlen;
	uint64_t				rp;
	uint64_t				wp;
} __packed;

/* Command context */
struct mhi_dev_cmd_ctx {
	uint32_t				res1;
	uint32_t				res2;
	uint32_t				res3;
	uint64_t				rbase;
	uint64_t				rlen;
	uint64_t				rp;
	uint64_t				wp;
} __packed;

/* generic context */
struct mhi_dev_gen_ctx {
	uint32_t				res1;
	uint32_t				res2;
	uint32_t				res3;
	uint64_t				rbase;
	uint64_t				rlen;
	uint64_t				rp;
	uint64_t				wp;
} __packed;

/* Transfer ring element */
struct mhi_dev_transfer_ring_element {
	uint64_t				data_buf_ptr;
	uint32_t				len:16;
	uint32_t				res1:16;
	uint32_t				chain:1;
	uint32_t				res2:7;
	uint32_t				ieob:1;
	uint32_t				ieot:1;
	uint32_t				bei:1;
	uint32_t				res3:5;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				res4:8;
} __packed;

/* Command ring element */
/* Command ring no-op command */
struct mhi_dev_cmd_ring_op {
	uint64_t				res1;
	uint32_t				res2;
	uint32_t				res3:16;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				chid:8;
} __packed;

/* Command ring reset channel command */
struct mhi_dev_cmd_ring_reset_channel_cmd {
	uint64_t				res1;
	uint32_t				res2;
	uint32_t				res3:16;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				chid:8;
} __packed;

/* Command ring stop channel command */
struct mhi_dev_cmd_ring_stop_channel_cmd {
	uint64_t				res1;
	uint32_t				res2;
	uint32_t				res3:16;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				chid:8;
} __packed;

/* Command ring start channel command */
struct mhi_dev_cmd_ring_start_channel_cmd {
	uint64_t				res1;
	uint32_t				seqnum;
	uint32_t				reliable:1;
	uint32_t				res2:15;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				chid:8;
} __packed;

enum mhi_dev_cmd_completion_code {
	MHI_CMD_COMPL_CODE_INVALID = 0,
	MHI_CMD_COMPL_CODE_SUCCESS = 1,
	MHI_CMD_COMPL_CODE_EOT = 2,
	MHI_CMD_COMPL_CODE_OVERFLOW = 3,
	MHI_CMD_COMPL_CODE_EOB = 4,
	MHI_CMD_COMPL_CODE_UNDEFINED = 16,
	MHI_CMD_COMPL_CODE_RING_EL = 17,
	MHI_CMD_COMPL_CODE_RES
};

/* Event ring elements */
/* Transfer completion event */
struct mhi_dev_event_ring_transfer_completion {
	uint64_t				ptr;
	uint32_t				len:16;
	uint32_t				res1:8;
	enum mhi_dev_cmd_completion_code	code:8;
	uint32_t				res2:16;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				chid:8;
} __packed;

/* Command completion event */
struct mhi_dev_event_ring_cmd_completion {
	uint64_t				ptr;
	uint32_t				res1:24;
	enum mhi_dev_cmd_completion_code	code:8;
	uint32_t				res2:16;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				res3:8;
} __packed;

enum mhi_dev_state {
	MHI_DEV_RESET_STATE = 0,
	MHI_DEV_READY_STATE,
	MHI_DEV_M0_STATE,
	MHI_DEV_M1_STATE,
	MHI_DEV_M2_STATE,
	MHI_DEV_M3_STATE,
	MHI_DEV_MAX_STATE,
	MHI_DEV_SYSERR_STATE = 0xff
};

/* MHI state change event */
struct mhi_dev_event_ring_state_change {
	uint64_t				ptr;
	uint32_t				res1:24;
	enum mhi_dev_state			mhistate:8;
	uint32_t				res2:16;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				res3:8;
} __packed;

enum mhi_dev_execenv {
	MHI_DEV_SBL_EE = 1,
	MHI_DEV_AMSS_EE = 2,
	MHI_DEV_UNRESERVED
};

enum mhi_dev_channel_id {
	IPA_CLIENT_MHI_PROD = 100,
	IPA_CLIENT_MHI_CONS = 101,
	IPA_CLIENT_MHI_DPL_CONS = 102,
	IPA_CLIENT_MHI2_PROD = 105,
	IPA_CLIENT_MHI2_CONS = 106,
};

/* EE state change event */
struct mhi_dev_event_ring_ee_state_change {
	uint64_t				ptr;
	uint32_t				res1:24;
	enum mhi_dev_execenv			execenv:8;
	uint32_t				res2:16;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				res3:8;
} __packed;

/* Generic cmd to parse common details like type and channel id */
struct mhi_dev_ring_generic {
	uint64_t				ptr;
	uint32_t				res1:24;
	enum mhi_dev_state			mhistate:8;
	uint32_t				res2:16;
	enum mhi_dev_ring_element_type_id	type:8;
	uint32_t				chid:8;
} __packed;

struct mhi_config {
	uint32_t	mhi_reg_len;
	uint32_t	version;
	uint32_t	event_rings;
	uint32_t	hw_event_rings;
	uint32_t	channels;
	uint32_t	chdb_offset;
	uint32_t	erdb_offset;
};

#define NUM_CHANNELS			128
#define HW_CHANNEL_BASE			100
#define NUM_HW_CHANNELS			28
#define HW_CHANNEL_END			127
#define MHI_ENV_VALUE			2
#define MHI_MASK_ROWS_CH_EV_DB		4
#define TRB_MAX_DATA_SIZE		8192
#define TRB_MAX_DATA_SIZE_16K		16384
#define MHI_CTRL_STATE			100
#define MHI_MAX_NUM_INSTANCES		17 /* 1PF and 16 VFs */
#define MHI_DEFAULT_ERROR_LOG_ID	255
#define MHI_DEV_WAKE_DB_CHAN		127
#define PCIE_EP_TIMER_US		10000000

/* maximum transfer completion events buffer */
#define NUM_TR_EVENTS_DEFAULT			128
#define NUM_CMD_EVENTS_DEFAULT			20


/* Set flush threshold to 80% of event buf size */
#define MHI_CMPL_EVT_FLUSH_THRSHLD(n) ((n * 8) / 10)

/* Possible ring element types */
union mhi_dev_ring_element_type {
	struct mhi_dev_cmd_ring_op			cmd_no_op;
	struct mhi_dev_cmd_ring_reset_channel_cmd	cmd_reset;
	struct mhi_dev_cmd_ring_stop_channel_cmd	cmd_stop;
	struct mhi_dev_cmd_ring_start_channel_cmd	cmd_start;
	struct mhi_dev_transfer_ring_element		tre;
	struct mhi_dev_event_ring_transfer_completion	evt_tr_comp;
	struct mhi_dev_event_ring_cmd_completion	evt_cmd_comp;
	struct mhi_dev_event_ring_state_change		evt_state_change;
	struct mhi_dev_event_ring_ee_state_change	evt_ee_state;
	struct mhi_dev_ring_generic			generic;
};

/* Transfer ring element type */
union mhi_dev_ring_ctx {
	struct mhi_dev_cmd_ctx		cmd;
	struct mhi_dev_ev_ctx		ev;
	struct mhi_dev_ch_ctx		ch;
	struct mhi_dev_gen_ctx		generic;
};

/* MHI host Control and data address region */
struct mhi_host_addr {
	uint32_t	ctrl_base_lsb;
	uint32_t	ctrl_base_msb;
	uint32_t	ctrl_limit_lsb;
	uint32_t	ctrl_limit_msb;
	uint32_t	data_base_lsb;
	uint32_t	data_base_msb;
	uint32_t	data_limit_lsb;
	uint32_t	data_limit_msb;
};

/* MHI physical and virtual address region */
struct mhi_meminfo {
	struct device	*dev;
	uintptr_t	pa_aligned;
	uintptr_t	pa_unaligned;
	uintptr_t	va_aligned;
	uintptr_t	va_unaligned;
	uintptr_t	size;
};

struct mhi_addr {
	uint64_t	host_pa;
	uint64_t	device_pa;
	uint64_t	device_va;
	size_t		size;
	dma_addr_t	phy_addr;
	void		*virt_addr;
};

struct mhi_interrupt_state {
	uint32_t	mask;
	uint32_t	status;
};

enum mhi_dev_channel_state {
	MHI_DEV_CH_UNINT,
	MHI_DEV_CH_STARTED,
	MHI_DEV_CH_PENDING_START,
	MHI_DEV_CH_PENDING_STOP,
	MHI_DEV_CH_STOPPED,
	MHI_DEV_CH_CLOSED,
};

enum mhi_dev_ch_operation {
	MHI_DEV_OPEN_CH,
	MHI_DEV_CLOSE_CH,
	MHI_DEV_READ_CH,
	MHI_DEV_READ_WR,
	MHI_DEV_POLL,
};

enum mhi_dev_tr_compl_evt_type {
	SEND_EVENT_BUFFER,
	SEND_EVENT_RD_OFFSET,
	SEND_MSI,
	SEND_CMD_CMP,
};

enum mhi_dev_transfer_type {
	MHI_DEV_DMA_SYNC,
	MHI_DEV_DMA_ASYNC,
};

struct msi_buf_cb_data {
	u32 *buf;
	dma_addr_t dma_addr;
};

struct mhi_dev_channel;

struct mhi_dev_ring {
	struct list_head			list;
	struct mhi_dev				*mhi_dev;

	uint32_t				id;
	size_t				rd_offset;
	size_t				wr_offset;
	size_t				ring_size;

	enum mhi_dev_ring_type			type;
	enum mhi_dev_ring_state			state;
	/*
	 * Lock to prevent race in updating event ring
	 * which is shared by multiple channels
	 */
	struct mutex	event_lock;
	/* device virtual address location of the cached host ring ctx data */
	union mhi_dev_ring_element_type		*ring_cache;
	/* Physical address of the cached ring copy on the device side */
	dma_addr_t				ring_cache_dma_handle;
	/* Device VA of read pointer array (used only for event rings) */
	uint64_t			*evt_rp_cache;
	/* PA of the read pointer array (used only for event rings) */
	dma_addr_t				evt_rp_cache_dma_handle;
	/* Device VA of msi buffer (used only for event rings)  */
	uint32_t			*msi_buf;
	/* PA of msi buf (used only for event rings) */
	dma_addr_t				msi_buf_dma_handle;
	/* Physical address of the host where we will write/read to/from */
	struct mhi_addr				ring_shadow;
	/* Ring type - cmd, event, transfer ring and its rp/wp... */
	union mhi_dev_ring_ctx			*ring_ctx;
	/* ring_ctx_shadow -> tracking ring_ctx in the host */
	union mhi_dev_ring_ctx			*ring_ctx_shadow;
	struct msi_buf_cb_data		msi_buffer;
	int (*ring_cb)(struct mhi_dev *dev,
			union mhi_dev_ring_element_type *el,
			void *ctx);
};

static inline void mhi_dev_ring_inc_index(struct mhi_dev_ring *ring,
						size_t rd_offset)
{
	ring->rd_offset++;
	if (ring->rd_offset == ring->ring_size)
		ring->rd_offset = 0;
}

/* trace information planned to use for read/write */
#define TRACE_DATA_MAX				128
#define MHI_DEV_DATA_MAX			512

#define MHI_DEV_MMIO_RANGE			0xb80
#define MHI_DEV_MMIO_OFFSET			0x100

struct ring_cache_req {
	struct completion	*done;
	void			*context;
};

struct event_req {
	union mhi_dev_ring_element_type *tr_events;
	/*
	 * Start index of the completion event buffer segment
	 * to be flushed to host
	 */
	u32			start;
	u32			num_events;
	dma_addr_t		dma;
	u32			dma_len;
	void			*context;
	enum mhi_dev_tr_compl_evt_type event_type;
	struct list_head	list;
	u32			flush_num;
	bool			snd_cmpl;
	bool		is_cmd_cpl;
	bool		is_stale;
};

struct mhi_cmd_cmpl_ctx {
	/* Indices for completion event buffer */
	uint32_t			cmd_buf_rp;
	uint32_t			cmd_buf_wp;
	uint32_t			cmd_buf_size;
	bool				mem_allocated;
	struct list_head	cmd_req_buffers;
	struct event_req		*ereqs;
	union mhi_dev_ring_element_type *cmd_events;
};

struct mhi_dev_channel {
	struct list_head		list;
	struct list_head		clients;
	/* synchronization for changing channel state,
	 * adding/removing clients, mhi_dev callbacks, etc
	 */
	struct mhi_dev_ring		*ring;

	enum mhi_dev_channel_state	state;
	uint32_t			ch_id;
	enum mhi_dev_ch_ctx_type	ch_type;
	struct mutex			ch_lock;
	/* client which the current inbound/outbound message is for */
	struct mhi_dev_client		*active_client;
	/* Pointer to completion event buffer */
	union mhi_dev_ring_element_type *tr_events;
	/* Indices for completion event buffer */
	uint32_t			evt_buf_rp;
	uint32_t			evt_buf_wp;
	uint32_t			evt_buf_size;
	/*
	 * Pointer to a block of event request structs used to temporarily
	 * store completion events and data before sending them to host
	 */
	struct event_req		*ereqs;
	/* Linked list head for event request structs */
	struct list_head		event_req_buffers;
	uint32_t				evt_req_size;
	/* Linked list head for event request structs to be flushed */
	struct list_head		flush_event_req_buffers;
	/* Pointer to the currently used event request struct */
	struct event_req		*curr_ereq;
	/* current TRE being processed */
	uint64_t			tre_loc;
	/* current TRE size */
	uint32_t			tre_size;
	/* tre bytes left to read/write */
	uint32_t			tre_bytes_left;
	/* td size being read/written from/to so far */
	uint32_t			td_size;
	uint32_t			pend_wr_count;
	uint32_t			msi_cnt;
	uint32_t			flush_req_cnt;
	uint32_t			snd_cmpl_cnt;
	uint32_t			pend_flush_cnt;
	bool				skip_td;
	bool				db_pending;
	bool				reset_pending;
};

/* Structure device for mhi dev */
struct mhi_dev {
	/*MHI device details*/
	struct mhi_dma_function_params mhi_dma_fun_params;

	/* MHI MMIO related members */
	phys_addr_t			mmio_base_pa_addr;
	void				*mmio_base_addr;
	phys_addr_t			mhi_dma_uc_mbox_crdb;
	phys_addr_t			mhi_dma_uc_mbox_erdb;

	uint32_t			*mmio_backup;
	struct mhi_config		cfg;
	u32				msi_data;
	u32				msi_lower;
	spinlock_t			msi_lock;

	spinlock_t			lock;
	/* Host control base information */
	struct mhi_host_addr		host_addr;
	struct mhi_addr			ctrl_base;
	struct mhi_addr			data_base;
	struct mhi_addr			ch_ctx_shadow;
	struct mhi_dev_ch_ctx		*ch_ctx_cache;
	dma_addr_t			ch_ctx_cache_dma_handle;
	struct mhi_addr			ev_ctx_shadow;
	struct mhi_dev_ch_ctx		*ev_ctx_cache;
	dma_addr_t			ev_ctx_cache_dma_handle;

	struct mhi_addr			cmd_ctx_shadow;
	struct mhi_dev_ch_ctx		*cmd_ctx_cache;
	dma_addr_t			cmd_ctx_cache_dma_handle;
	struct mhi_dev_ring		**ring;
	int				mhi_irq;
	struct mhi_dev_channel		**ch;
	struct mhi_cmd_cmpl_ctx			*cmd_ctx;

	int				ctrl_int;
	int				cmd_int;
	/* CHDB and EVDB device interrupt state */
	struct mhi_interrupt_state	chdb[4];
	struct mhi_interrupt_state	evdb[4];

	/* Scheduler work */
	struct work_struct		chdb_ctrl_work;

	struct mutex			mhi_lock;
	struct mutex			mhi_event_lock;

	/* process a ring element */
	struct workqueue_struct		*pending_ring_wq;
	struct work_struct		pending_work;

	struct list_head		event_ring_list;
	struct list_head		process_ring_list;

	size_t			cmd_ring_idx;
	size_t			ev_ring_start;
	size_t			ch_ring_start;

	/* MHI DMA Handles */
	u32				dma_clnt_hndl[NUM_HW_CHANNELS];
	struct workqueue_struct		*ring_init_wq;
	struct work_struct		ring_init_cb_work;
	struct work_struct		re_init;

	atomic_t			write_active;
	atomic_t			is_suspended;
	atomic_t			mhi_dev_wake;
	atomic_t			re_init_done;
	struct mutex			mhi_write_test;
	u64				device_local_pa_base;
	u32				mhi_ep_msi_num;
	u32				mhi_version;
	u32				mhi_chan_hw_base;
	u32				mhi_num_ipc_pages_dev_fac;
	/* Physical scratch buffer for writing control data to the host */
	bool				mhi_dma_ready;

	/* Use  PCI eDMA for data transfer */
	bool				use_edma;

	/* Use  MHI DMA for Software channel data transfer */
	bool				use_mhi_dma;

	/* Denotes if the MHI instance is physcial or virtual */
	bool				is_mhi_pf;

	bool				is_flashless;

	bool				mhi_has_smmu;

	/* iATU is required to map control and data region */
	bool				config_iatu;

	/* Indicates if mhi init is done */
	bool				init_done;

	/* MHI state info */
	enum mhi_ctrl_info		ctrl_info;

	/*Register for interrupt*/
	bool				mhi_int;
	bool				mhi_int_en;

	/* Enable M2 autonomous mode from MHI */
	bool				enable_m2;

	/* Status of device wake doorbell */
	bool				wake_db_status;

	/* Dont timeout waiting for M0 */
	bool				no_m0_timeout;

	bool				stop_polling_m0;
	bool				msi_disable;

	/* Registered client callback list */
	struct list_head		client_cb_list;

	/* EP PCIe registration */
	struct workqueue_struct		*pcie_event_wq;
	struct work_struct		pcie_event;

	struct mhi_dev_ctx		*mhi_hw_ctx;
	struct mhi_sm_dev		*mhi_sm_ctx;
	/* MHI VF number */
	uint32_t			vf_id;

	bool				no_path_from_ipa_to_pcie;

	int (*device_to_host)(uint64_t dst_pa, void *src, uint32_t len,
				struct mhi_dev *mhi, struct mhi_req *req);

	int (*host_to_device)(void *device, uint64_t src_pa, uint32_t len,
				struct mhi_dev *mhi, struct mhi_req *mreq);

	void (*write_to_host)(struct mhi_dev *mhi,
			struct mhi_addr *mhi_transfer, struct event_req *ereq,
			enum mhi_dev_transfer_type type);

	void (*read_from_host)(struct mhi_dev *mhi,
				struct mhi_addr *mhi_transfer);
};

/* Structure device for mhi dev */
struct mhi_dev_ctx {
	struct platform_device		*pdev;
	struct device			*dev;

	struct ep_pcie_register_event	event_reg;
	u32				ifc_id;
	struct ep_pcie_hw		*phandle;

	struct mhi_dev			*mhi_dev[MHI_MAX_NUM_INSTANCES];

	/* Tx, Rx DMA channels */
	struct dma_chan			*tx_dma_chan;
	struct dma_chan			*rx_dma_chan;

	struct ep_pcie_notify		*notify;
	struct mhi_dma_ops		mhi_dma_fun_ops;
	struct ep_pcie_cap		ep_cap;
};

enum mhi_id {
	MHI_DEV_PHY_FUN,
	MHI_DEV_VIRT_0,
	MHI_DEV_VIRT_1,
};

enum mhi_msg_level {
	MHI_MSG_VERBOSE = 0x0,
	MHI_MSG_INFO = 0x1,
	MHI_MSG_DBG = 0x2,
	MHI_MSG_WARNING = 0x3,
	MHI_MSG_ERROR = 0x4,
	MHI_MSG_CRITICAL = 0x5,
	MHI_MSG_reserved = 0x80000000
};


/* Structure for mhi device operations */
struct mhi_dev_ops {
	int	(*register_state_cb)(void (*mhi_state_cb)
			(struct mhi_dev_client_cb_data *cb_data),
			void *data, enum mhi_client_channel channel, uint32_t vf_id);
	int	(*ctrl_state_info)(uint32_t vf_id, uint32_t idx, uint32_t *info);
	int	(*open_channel)(uint32_t vf_id, uint32_t chan_id,
			struct mhi_dev_client **handle,
			void (*mhi_dev_client_cb_reason)
				(struct mhi_dev_client_cb_reason *cb));
	void	(*close_channel)(struct mhi_dev_client *handle);
	int	(*write_channel)(struct mhi_req *mreq);
	int	(*read_channel)(struct mhi_req *mreq);
	int	(*is_channel_empty)(struct mhi_dev_client *handle);
};

extern uint32_t bhi_imgtxdb;
extern enum mhi_msg_level mhi_msg_lvl;
extern enum mhi_msg_level mhi_ipc_msg_lvl;
extern enum mhi_msg_level mhi_ipc_err_msg_lvl;
extern void *mhi_ipc_err_log;
extern void *mhi_ipc_vf_log[MHI_MAX_NUM_INSTANCES];
extern void *mhi_ipc_default_err_log;

#define mhi_log(vf_id, _msg_lvl, _msg, ...) do { \
	if (_msg_lvl >= mhi_msg_lvl) { \
		pr_err_ratelimited("[0x%x %s] "_msg, bhi_imgtxdb, \
				__func__, ##__VA_ARGS__); \
	} \
	if (vf_id < MHI_MAX_NUM_INSTANCES && mhi_ipc_vf_log[vf_id] &&    \
			(_msg_lvl >= mhi_ipc_msg_lvl)) { \
		ipc_log_string(mhi_ipc_vf_log[vf_id],                     \
		"[0x%x %s] " _msg, bhi_imgtxdb, __func__, ##__VA_ARGS__); \
	} \
	if (vf_id == MHI_DEFAULT_ERROR_LOG_ID && mhi_ipc_default_err_log &&       \
			(_msg_lvl >= mhi_ipc_err_msg_lvl)) { \
		ipc_log_string(mhi_ipc_default_err_log,                     \
		"[0x%x %s] " _msg, bhi_imgtxdb, __func__, ##__VA_ARGS__); \
	} \
	else if (mhi_ipc_err_log && (_msg_lvl >= mhi_ipc_err_msg_lvl)) { \
		if (vf_id == 0) {				\
			ipc_log_string(mhi_ipc_err_log,			\
			"[0x%x %s] PF = %x  " _msg, bhi_imgtxdb, __func__, vf_id, ##__VA_ARGS__); \
		} \
		if (vf_id != 0) { \
			ipc_log_string(mhi_ipc_err_log,                 \
			"[0x%x %s] VF = %x  " _msg, bhi_imgtxdb, __func__, vf_id, ##__VA_ARGS__); \
		} \
	} \
} while (0)

/* Use ID 0 for legacy /dev/mhi_ctrl. Channel 0 used for internal only */
#define MHI_DEV_UEVENT_CTRL	0

#define MHI_USE_DMA(mhi) (mhi->use_mhi_dma || mhi->use_edma)

struct mhi_dev_uevent_info {
	enum mhi_client_channel	channel;
	enum mhi_ctrl_info	ctrl_info;
};

struct mhi_dev_iov {
	void		*addr;
	uint32_t	buf_size;
};


struct mhi_dev_trace {
	unsigned int timestamp;
	uint32_t data[TRACE_DATA_MAX];
};

/* MHI Ring related functions */

/**
 * mhi_ring_init() - Initializes the Ring id to the default un-initialized
 *		state. Once a start command is received, the respective ring
 *		is then prepared by fetching the context and updating the
 *		offset.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 * @type:	Command/Event or Channel transfer ring.
 * @id:		Index to the ring id. For command its usually 1, Event rings
 *		may vary from 1 to 128. Channels vary from 1 to 256.
 */
void mhi_ring_init(struct mhi_dev_ring *ring,
			enum mhi_dev_ring_type type, int id);

/**
 * mhi_ring_start() - Fetches the respective transfer ring's context from
 *		the host and updates the write offset.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 * @ctx:	Transfer ring of type mhi_dev_ring_ctx.
 * @dev:	MHI device structure.
 */
int mhi_ring_start(struct mhi_dev_ring *ring,
			union mhi_dev_ring_ctx *ctx, struct mhi_dev *mhi);

/**
 * mhi_dev_cache_ring() - Cache the data for the corresponding ring locally.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 * @wr_offset:	Cache the TRE's up to the write offset value.
 */
int mhi_dev_cache_ring(struct mhi_dev_ring *ring, size_t wr_offset);

/**
 * mhi_dev_update_wr_offset() - Check for any updates in the write offset.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 */
int mhi_dev_update_wr_offset(struct mhi_dev_ring *ring);

/**
 * mhi_dev_process_ring() - Update the Write pointer, fetch the ring elements
 *			    and invoke the clients callback.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 */
int mhi_dev_process_ring(struct mhi_dev_ring *ring);

/**
 * mhi_dev_process_ring_element() - Fetch the ring elements and invoke the
 *			    clients callback.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 * @offset:	Offset index into the respective ring's cache element.
 */
int mhi_dev_process_ring_element(struct mhi_dev_ring *ring, size_t offset);

/**
 * mhi_dev_add_element() - Copy the element to the respective transfer rings
 *			read pointer and increment the index.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 * @element:	Transfer ring element to be copied to the host memory.
 */
int mhi_dev_add_element(struct mhi_dev_ring *ring,
				union mhi_dev_ring_element_type *element,
				struct event_req *ereq, int evt_offset);

/*
 * mhi_ring_set_cb () - Call back function of the ring.
 *
 * @ring:	Ring for the respective context - Channel/Event/Command.
 * @ring_cb:	callback function.
 */
void mhi_ring_set_cb(struct mhi_dev_ring *ring,
			int (*ring_cb)(struct mhi_dev *dev,
			union mhi_dev_ring_element_type *el, void *ctx));

/**
 * mhi_ring_set_state() - Sets internal state of the ring for tracking whether
 *		a ring is being processed, idle or uninitialized.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 * @state:	state of type mhi_dev_ring_state.
 */
void mhi_ring_set_state(struct mhi_dev_ring *ring,
			enum mhi_dev_ring_state state);

/**
 * mhi_ring_get_state() - Obtains the internal state of the ring.
 * @ring:	Ring for the respective context - Channel/Event/Command.
 */
enum mhi_dev_ring_state mhi_ring_get_state(struct mhi_dev_ring *ring);

/* MMIO related functions */

/**
 * mhi_dev_mmio_read() - Generic MHI MMIO register read API.
 * @dev:	MHI device structure.
 * @offset:	MHI address offset from base.
 * @reg_val:	Pointer the register value is stored to.
 */
int mhi_dev_mmio_read(struct mhi_dev *dev, uint32_t offset,
			uint32_t *reg_value);

/**
 * mhi_dev_mmio_read() - Generic MHI MMIO register write API.
 * @dev:	MHI device structure.
 * @offset:	MHI address offset from base.
 * @val:	Value to be written to the register offset.
 */
int mhi_dev_mmio_write(struct mhi_dev *dev, uint32_t offset,
				uint32_t val);

/**
 * mhi_dev_mmio_masked_write() - Generic MHI MMIO register write masked API.
 * @dev:	MHI device structure.
 * @offset:	MHI address offset from base.
 * @mask:	Register field mask.
 * @shift:	Register field mask shift value.
 * @val:	Value to be written to the register offset.
 */
int mhi_dev_mmio_masked_write(struct mhi_dev *dev, uint32_t offset,
						uint32_t mask, uint32_t shift,
						uint32_t val);
/**
 * mhi_dev_mmio_masked_read() - Generic MHI MMIO register read masked API.
 * @dev:	MHI device structure.
 * @offset:	MHI address offset from base.
 * @mask:	Register field mask.
 * @shift:	Register field mask shift value.
 * @reg_val:	Pointer the register value is stored to.
 */
int mhi_dev_mmio_masked_read(struct mhi_dev *dev, uint32_t offset,
						uint32_t mask, uint32_t shift,
						uint32_t *reg_val);
/**
 * mhi_dev_mmio_enable_ctrl_interrupt() - Enable Control interrupt.
 * @dev:	MHI device structure.
 */

int mhi_dev_mmio_enable_ctrl_interrupt(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_disable_ctrl_interrupt() - Disable Control interrupt.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_disable_ctrl_interrupt(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_read_ctrl_status_interrupt() - Read Control interrupt status.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_read_ctrl_status_interrupt(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_enable_cmdb_interrupt() - Enable Command doorbell interrupt.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_enable_cmdb_interrupt(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_disable_cmdb_interrupt() - Disable Command doorbell interrupt.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_disable_cmdb_interrupt(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_read_cmdb_interrupt() - Read Command doorbell status.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_read_cmdb_status_interrupt(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_enable_chdb_a7() - Enable Channel doorbell for a given
 *		channel id.
 * @dev:	MHI device structure.
 * @chdb_id:	Channel id number.
 */
int mhi_dev_mmio_enable_chdb_a7(struct mhi_dev *dev, uint32_t chdb_id);
/**
 * mhi_dev_mmio_disable_chdb_a7() - Disable Channel doorbell for a given
 *		channel id.
 * @dev:	MHI device structure.
 * @chdb_id:	Channel id number.
 */
int mhi_dev_mmio_disable_chdb_a7(struct mhi_dev *dev, uint32_t chdb_id);

/**
 * mhi_dev_mmio_enable_erdb_a7() - Enable Event ring doorbell for a given
 *		event ring id.
 * @dev:	MHI device structure.
 * @erdb_id:	Event ring id number.
 */
int mhi_dev_mmio_enable_erdb_a7(struct mhi_dev *dev, uint32_t erdb_id);

/**
 * mhi_dev_mmio_disable_erdb_a7() - Disable Event ring doorbell for a given
 *		event ring id.
 * @dev:	MHI device structure.
 * @erdb_id:	Event ring id number.
 */
int mhi_dev_mmio_disable_erdb_a7(struct mhi_dev *dev, uint32_t erdb_id);

/**
 * mhi_dev_mmio_enable_chdb_interrupts() - Enable all Channel doorbell
 *		interrupts.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_enable_chdb_interrupts(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_mask_chdb_interrupts() - Mask all Channel doorbell
 *		interrupts.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_mask_chdb_interrupts(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_read_chdb_interrupts() - Read all Channel doorbell
 *		interrupts.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_read_chdb_status_interrupts(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_enable_erdb_interrupts() - Enable all Event doorbell
 *		interrupts.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_enable_erdb_interrupts(struct mhi_dev *dev);

/**
 *mhi_dev_mmio_mask_erdb_interrupts() - Mask all Event doorbell
 *		interrupts.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_mask_erdb_interrupts(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_read_erdb_interrupts() - Read all Event doorbell
 *		interrupts.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_read_erdb_status_interrupts(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_mask_interrupts() - Mask all MHI interrupts.
 * @dev:	MHI device structure.
 */
void mhi_dev_mmio_mask_interrupts(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_clear_interrupts() - Clear all doorbell interrupts.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_clear_interrupts(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_get_chc_base() - Fetch the Channel ring context base address.
 @dev:	MHI device structure.
 */
int mhi_dev_mmio_get_chc_base(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_get_erc_base() - Fetch the Event ring context base address.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_get_erc_base(struct mhi_dev *dev);

/**
 * mhi_dev_get_crc_base() - Fetch the Command ring context base address.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_get_crc_base(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_get_ch_db() - Fetch the Write offset of the Channel ring ID.
 * @dev:	MHI device structure.
 * @wr_offset:	Pointer of the write offset to be written to.
 */
int mhi_dev_mmio_get_ch_db(struct mhi_dev_ring *ring, uint64_t *wr_offset);

/**
 * mhi_dev_get_erc_base() - Fetch the Write offset of the Event ring ID.
 * @dev:	MHI device structure.
 * @wr_offset:	Pointer of the write offset to be written to.
 */
int mhi_dev_mmio_get_erc_db(struct mhi_dev_ring *ring, uint64_t *wr_offset);

/**
 * mhi_dev_get_cmd_base() - Fetch the Write offset of the Command ring ID.
 * @dev:	MHI device structure.
 * @wr_offset:	Pointer of the write offset to be written to.
 */
int mhi_dev_mmio_get_cmd_db(struct mhi_dev_ring *ring, uint64_t *wr_offset);

/**
 * mhi_dev_mmio_set_env() - Write the Execution Enviornment.
 * @dev:	MHI device structure.
 * @value:	Value of the EXEC EVN.
 */
int mhi_dev_mmio_set_env(struct mhi_dev *dev, uint32_t value);

/**
 * mhi_dev_mmio_clear_reset() - Clear the reset bit
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_clear_reset(struct mhi_dev *dev);

/**
 * mhi_dev_mmio_reset() - Reset the MMIO done as part of initialization.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_reset(struct mhi_dev *dev);

/**
 * mhi_dev_get_mhi_addr() - Fetches the Data and Control region from the Host.
 * @dev:	MHI device structure.
 */
int mhi_dev_get_mhi_addr(struct mhi_dev *dev);

/**
 * mhi_dev_get_mhi_state() - Fetches the MHI state such as M0/M1/M2/M3.
 * @dev:	MHI device structure.
 * @state:	Pointer of type mhi_dev_state
 * @mhi_reset:	MHI device reset from host.
 */
int mhi_dev_mmio_get_mhi_state(struct mhi_dev *dev, enum mhi_dev_state *state,
						u32 *mhi_reset);

/**
 * mhi_dev_mmio_init() - Initializes the MMIO and reads the Number of event
 *		rings, support number of channels, and offsets to the Channel
 *		and Event doorbell from the host.
 * @dev:	MHI device structure.
 */
int mhi_dev_mmio_init(struct mhi_dev *dev);

/**
 * mhi_dev_update_ner() - Update the number of event rings (NER) programmed by
 *		the host.
 * @dev:	MHI device structure.
 */
int mhi_dev_update_ner(struct mhi_dev *dev);

/**
 * mhi_dev_restore_mmio() - Restores the MMIO when MHI device comes out of M3.
 * @dev:	MHI device structure.
 */
int mhi_dev_restore_mmio(struct mhi_dev *dev);

/**
 * mhi_dev_backup_mmio() - Backup MMIO before a MHI transition to M3.
 * @dev:	MHI device structure.
 */
int mhi_dev_backup_mmio(struct mhi_dev *dev);

/**
 * mhi_dev_dump_mmio() - Memory dump of the MMIO region for debug.
 * @dev:	MHI device structure.
 */
int mhi_dev_dump_mmio(struct mhi_dev *dev);

/**
 * mhi_dev_config_outbound_iatu() - Configure Outbound Address translation
 *		unit between device and host to map the Data and Control
 *		information.
 * @dev:	MHI device structure.
 */
int mhi_dev_config_outbound_iatu(struct mhi_dev *mhi);

/**
 * mhi_dev_send_state_change_event() - Send state change event to the host
 *		such as M0/M1/M2/M3.
 * @dev:	MHI device structure.
 * @state:	MHI state of type mhi_dev_state
 */
int mhi_dev_send_state_change_event(struct mhi_dev *mhi,
					enum mhi_dev_state state);
/**
 * mhi_dev_send_ee_event() - Send Execution enviornment state change
 *		event to the host.
 * @dev:	MHI device structure.
 * @state:	MHI state of type mhi_dev_execenv
 */
int mhi_dev_send_ee_event(struct mhi_dev *mhi,
					enum mhi_dev_execenv exec_env);
/**
 * mhi_dev_syserr() - System error when unexpected events are received.
 * @dev:	MHI device structure.
 */
int mhi_dev_syserr(struct mhi_dev *mhi);

/**
 * mhi_dev_suspend() - MHI device suspend to stop channel processing at the
 *		Transfer ring boundary, update the channel state to suspended.
 * @dev:	MHI device structure.
 */
int mhi_dev_suspend(struct mhi_dev *mhi);

/**
 * mhi_channel_error_notif() - Send state change event to the host for channel e
				error notification
 * @dev:	MHI device structure.
 */
int mhi_channel_error_notif(struct mhi_dev *mhi);

/**
 * mhi_dev_resume() - MHI device resume to update the channel state to running.
 * @dev:	MHI device structure.
 */
int mhi_dev_resume(struct mhi_dev *mhi);

/**
 * mhi_dev_trigger_hw_acc_wakeup() - Notify State machine there is HW
 *		accelerated data to be send and prevent MHI suspend.
 * @dev:	MHI device structure.
 */
int mhi_dev_trigger_hw_acc_wakeup(struct mhi_dev *mhi);

/**
 * mhi_pcie_config_db_routing() - Configure Doorbell for Event and Channel
 *		context with MHI DMA when performing a MHI resume.
 * @dev:	MHI device structure.
 */
int mhi_pcie_config_db_routing(struct mhi_dev *mhi);

/**
 * mhi_uci_init() - Initializes the User control interface (UCI) which
 *		exposes device nodes for the supported MHI software
 *		channels.
 */
int mhi_uci_init(void);

void mhi_dev_notify_a7_event(struct mhi_dev *mhi);

void uci_ctrl_update(struct mhi_dev_client_cb_reason *reason);
/**
 * mhi_uci_chan_state_notify_all - Notifies channel state updates for
 *				all clients who have uevents enabled.
 */
void mhi_uci_chan_state_notify_all(struct mhi_dev *mhi,
		enum mhi_ctrl_info ch_state);
/**
 * mhi_uci_chan_state_notify - Notifies channel state update to the client
 *				if uevents are enabled.
 */
void mhi_uci_chan_state_notify(struct mhi_dev *mhi,
		enum mhi_client_channel ch_id, enum mhi_ctrl_info ch_state);

/**
 * mhi_dev_configure_inactivity_timer() - Configure inactive timer.
 * @mhi:        MHI dev structure
 * @enable:     Flag to enable or disable timer
 */
int mhi_dev_configure_inactivity_timer(struct mhi_dev *mhi, bool enable);

void mhi_dev_pm_relax(struct mhi_dev *mhi_ctx);
void mhi_dev_resume_init_with_link_up(struct ep_pcie_notify *notify);

int  mhi_edma_release(void);

int  mhi_edma_status(void);

int mhi_edma_init(struct device *dev);
void free_coherent(struct mhi_dev *mhi, size_t size, void *virt,
		   dma_addr_t phys);
void *alloc_coherent(struct mhi_dev *mhi, size_t size, dma_addr_t *phys,
		     gfp_t gfp);
/**
 * mhi_dev_net_interface_init() - Initializes the mhi device network interface
 *		which exposes the virtual network interface (mhi_dev_net0).
 *		data packets will transfer between MHI host interface (mhi_swip)
 *		and mhi_dev_net interface using software path.
 * @dev_ops	MHI dev function pointers
 * @vf_id       MHI instance (physical or virtual) id.
 * @num_vfs     Total number of vutual MHI instances supported on this target.
 */
#if IS_ENABLED(CONFIG_MSM_MHI_NET_DEV)
int mhi_dev_net_interface_init(struct mhi_dev_ops *dev_ops, u32 vf_id, u32 num_vfs);
#else
static inline int mhi_dev_net_interface_init(struct mhi_dev_ops *dev_ops, u32 vf_id, u32 num_vfs)
{
	return -EINVAL;
}
#endif
#endif /* _MHI_H */
