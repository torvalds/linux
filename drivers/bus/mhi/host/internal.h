/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 */

#ifndef _MHI_INT_H
#define _MHI_INT_H

#include "../common.h"

extern struct bus_type mhi_bus_type;

/* Host request register */
#define MHI_SOC_RESET_REQ_OFFSET			0xb0
#define MHI_SOC_RESET_REQ				BIT(0)

#define SOC_HW_VERSION_OFFS				0x224
#define SOC_HW_VERSION_FAM_NUM_BMSK			GENMASK(31, 28)
#define SOC_HW_VERSION_DEV_NUM_BMSK			GENMASK(27, 16)
#define SOC_HW_VERSION_MAJOR_VER_BMSK			GENMASK(15, 8)
#define SOC_HW_VERSION_MINOR_VER_BMSK			GENMASK(7, 0)

struct mhi_ctxt {
	struct mhi_event_ctxt *er_ctxt;
	struct mhi_chan_ctxt *chan_ctxt;
	struct mhi_cmd_ctxt *cmd_ctxt;
	dma_addr_t er_ctxt_addr;
	dma_addr_t chan_ctxt_addr;
	dma_addr_t cmd_ctxt_addr;
};

struct bhi_vec_entry {
	u64 dma_addr;
	u64 size;
};

enum mhi_ch_state_type {
	MHI_CH_STATE_TYPE_RESET,
	MHI_CH_STATE_TYPE_STOP,
	MHI_CH_STATE_TYPE_START,
	MHI_CH_STATE_TYPE_MAX,
};

extern const char * const mhi_ch_state_type_str[MHI_CH_STATE_TYPE_MAX];
#define TO_CH_STATE_TYPE_STR(state) (((state) >= MHI_CH_STATE_TYPE_MAX) ? \
				     "INVALID_STATE" : \
				     mhi_ch_state_type_str[(state)])

#define MHI_INVALID_BRSTMODE(mode) (mode != MHI_DB_BRST_DISABLE && \
				    mode != MHI_DB_BRST_ENABLE)

extern const char * const mhi_ee_str[MHI_EE_MAX];
#define TO_MHI_EXEC_STR(ee) (((ee) >= MHI_EE_MAX) ? \
			     "INVALID_EE" : mhi_ee_str[ee])

#define MHI_IN_PBL(ee) (ee == MHI_EE_PBL || ee == MHI_EE_PTHRU || \
			ee == MHI_EE_EDL)
#define MHI_POWER_UP_CAPABLE(ee) (MHI_IN_PBL(ee) || ee == MHI_EE_AMSS)
#define MHI_FW_LOAD_CAPABLE(ee) (ee == MHI_EE_PBL || ee == MHI_EE_EDL)
#define MHI_IN_MISSION_MODE(ee) (ee == MHI_EE_AMSS || ee == MHI_EE_WFW || \
				 ee == MHI_EE_FP)

enum dev_st_transition {
	DEV_ST_TRANSITION_PBL,
	DEV_ST_TRANSITION_READY,
	DEV_ST_TRANSITION_SBL,
	DEV_ST_TRANSITION_MISSION_MODE,
	DEV_ST_TRANSITION_FP,
	DEV_ST_TRANSITION_SYS_ERR,
	DEV_ST_TRANSITION_DISABLE,
	DEV_ST_TRANSITION_MAX,
};

extern const char * const dev_state_tran_str[DEV_ST_TRANSITION_MAX];
#define TO_DEV_STATE_TRANS_STR(state) (((state) >= DEV_ST_TRANSITION_MAX) ? \
				"INVALID_STATE" : dev_state_tran_str[state])

/* internal power states */
enum mhi_pm_state {
	MHI_PM_STATE_DISABLE,
	MHI_PM_STATE_POR,
	MHI_PM_STATE_M0,
	MHI_PM_STATE_M2,
	MHI_PM_STATE_M3_ENTER,
	MHI_PM_STATE_M3,
	MHI_PM_STATE_M3_EXIT,
	MHI_PM_STATE_FW_DL_ERR,
	MHI_PM_STATE_SYS_ERR_DETECT,
	MHI_PM_STATE_SYS_ERR_PROCESS,
	MHI_PM_STATE_SHUTDOWN_PROCESS,
	MHI_PM_STATE_LD_ERR_FATAL_DETECT,
	MHI_PM_STATE_MAX
};

#define MHI_PM_DISABLE					BIT(0)
#define MHI_PM_POR					BIT(1)
#define MHI_PM_M0					BIT(2)
#define MHI_PM_M2					BIT(3)
#define MHI_PM_M3_ENTER					BIT(4)
#define MHI_PM_M3					BIT(5)
#define MHI_PM_M3_EXIT					BIT(6)
/* firmware download failure state */
#define MHI_PM_FW_DL_ERR				BIT(7)
#define MHI_PM_SYS_ERR_DETECT				BIT(8)
#define MHI_PM_SYS_ERR_PROCESS				BIT(9)
#define MHI_PM_SHUTDOWN_PROCESS				BIT(10)
/* link not accessible */
#define MHI_PM_LD_ERR_FATAL_DETECT			BIT(11)

#define MHI_REG_ACCESS_VALID(pm_state)			((pm_state & (MHI_PM_POR | MHI_PM_M0 | \
						MHI_PM_M2 | MHI_PM_M3_ENTER | MHI_PM_M3_EXIT | \
						MHI_PM_SYS_ERR_DETECT | MHI_PM_SYS_ERR_PROCESS | \
						MHI_PM_SHUTDOWN_PROCESS | MHI_PM_FW_DL_ERR)))
#define MHI_PM_IN_ERROR_STATE(pm_state)			(pm_state >= MHI_PM_FW_DL_ERR)
#define MHI_PM_IN_FATAL_STATE(pm_state)			(pm_state == MHI_PM_LD_ERR_FATAL_DETECT)
#define MHI_DB_ACCESS_VALID(mhi_cntrl)			(mhi_cntrl->pm_state & mhi_cntrl->db_access)
#define MHI_WAKE_DB_CLEAR_VALID(pm_state)		(pm_state & (MHI_PM_M0 | \
							MHI_PM_M2 | MHI_PM_M3_EXIT))
#define MHI_WAKE_DB_SET_VALID(pm_state)			(pm_state & MHI_PM_M2)
#define MHI_WAKE_DB_FORCE_SET_VALID(pm_state)		MHI_WAKE_DB_CLEAR_VALID(pm_state)
#define MHI_EVENT_ACCESS_INVALID(pm_state)		(pm_state == MHI_PM_DISABLE || \
							MHI_PM_IN_ERROR_STATE(pm_state))
#define MHI_PM_IN_SUSPEND_STATE(pm_state)		(pm_state & \
							(MHI_PM_M3_ENTER | MHI_PM_M3))

#define NR_OF_CMD_RINGS					1
#define CMD_EL_PER_RING					128
#define PRIMARY_CMD_RING				0
#define MHI_DEV_WAKE_DB					127
#define MHI_MAX_MTU					0xffff
#define MHI_RANDOM_U32_NONZERO(bmsk)			(get_random_u32_inclusive(1, bmsk))

enum mhi_er_type {
	MHI_ER_TYPE_INVALID = 0x0,
	MHI_ER_TYPE_VALID = 0x1,
};

struct db_cfg {
	bool reset_req;
	bool db_mode;
	u32 pollcfg;
	enum mhi_db_brst_mode brstmode;
	dma_addr_t db_val;
	void (*process_db)(struct mhi_controller *mhi_cntrl,
			   struct db_cfg *db_cfg, void __iomem *io_addr,
			   dma_addr_t db_val);
};

struct mhi_pm_transitions {
	enum mhi_pm_state from_state;
	u32 to_states;
};

struct state_transition {
	struct list_head node;
	enum dev_st_transition state;
};

struct mhi_ring {
	dma_addr_t dma_handle;
	dma_addr_t iommu_base;
	__le64 *ctxt_wp; /* point to ctxt wp */
	void *pre_aligned;
	void *base;
	void *rp;
	void *wp;
	size_t el_size;
	size_t len;
	size_t elements;
	size_t alloc_size;
	void __iomem *db_addr;
};

struct mhi_cmd {
	struct mhi_ring ring;
	spinlock_t lock;
};

struct mhi_buf_info {
	void *v_addr;
	void *bb_addr;
	void *wp;
	void *cb_buf;
	dma_addr_t p_addr;
	size_t len;
	enum dma_data_direction dir;
	bool used; /* Indicates whether the buffer is used or not */
	bool pre_mapped; /* Already pre-mapped by client */
};

struct mhi_event {
	struct mhi_controller *mhi_cntrl;
	struct mhi_chan *mhi_chan; /* dedicated to channel */
	u32 er_index;
	u32 intmod;
	u32 irq;
	int chan; /* this event ring is dedicated to a channel (optional) */
	u32 priority;
	enum mhi_er_data_type data_type;
	struct mhi_ring ring;
	struct db_cfg db_cfg;
	struct tasklet_struct task;
	spinlock_t lock;
	int (*process_event)(struct mhi_controller *mhi_cntrl,
			     struct mhi_event *mhi_event,
			     u32 event_quota);
	bool hw_ring;
	bool cl_manage;
	bool offload_ev; /* managed by a device driver */
};

struct mhi_chan {
	const char *name;
	/*
	 * Important: When consuming, increment tre_ring first and when
	 * releasing, decrement buf_ring first. If tre_ring has space, buf_ring
	 * is guranteed to have space so we do not need to check both rings.
	 */
	struct mhi_ring buf_ring;
	struct mhi_ring tre_ring;
	u32 chan;
	u32 er_index;
	u32 intmod;
	enum mhi_ch_type type;
	enum dma_data_direction dir;
	struct db_cfg db_cfg;
	enum mhi_ch_ee_mask ee_mask;
	enum mhi_ch_state ch_state;
	enum mhi_ev_ccs ccs;
	struct mhi_device *mhi_dev;
	void (*xfer_cb)(struct mhi_device *mhi_dev, struct mhi_result *result);
	struct mutex mutex;
	struct completion completion;
	rwlock_t lock;
	struct list_head node;
	bool lpm_notify;
	bool configured;
	bool offload_ch;
	bool pre_alloc;
	bool wake_capable;
};

/* Default MHI timeout */
#define MHI_TIMEOUT_MS (1000)

/* debugfs related functions */
#ifdef CONFIG_MHI_BUS_DEBUG
void mhi_create_debugfs(struct mhi_controller *mhi_cntrl);
void mhi_destroy_debugfs(struct mhi_controller *mhi_cntrl);
void mhi_debugfs_init(void);
void mhi_debugfs_exit(void);
#else
static inline void mhi_create_debugfs(struct mhi_controller *mhi_cntrl)
{
}

static inline void mhi_destroy_debugfs(struct mhi_controller *mhi_cntrl)
{
}

static inline void mhi_debugfs_init(void)
{
}

static inline void mhi_debugfs_exit(void)
{
}
#endif

struct mhi_device *mhi_alloc_device(struct mhi_controller *mhi_cntrl);

int mhi_destroy_device(struct device *dev, void *data);
void mhi_create_devices(struct mhi_controller *mhi_cntrl);

int mhi_alloc_bhie_table(struct mhi_controller *mhi_cntrl,
			 struct image_info **image_info, size_t alloc_size);
void mhi_free_bhie_table(struct mhi_controller *mhi_cntrl,
			 struct image_info *image_info);

/* Power management APIs */
enum mhi_pm_state __must_check mhi_tryset_pm_state(
					struct mhi_controller *mhi_cntrl,
					enum mhi_pm_state state);
const char *to_mhi_pm_state_str(u32 state);
int mhi_queue_state_transition(struct mhi_controller *mhi_cntrl,
			       enum dev_st_transition state);
void mhi_pm_st_worker(struct work_struct *work);
void mhi_pm_sys_err_handler(struct mhi_controller *mhi_cntrl);
int mhi_ready_state_transition(struct mhi_controller *mhi_cntrl);
int mhi_pm_m0_transition(struct mhi_controller *mhi_cntrl);
void mhi_pm_m1_transition(struct mhi_controller *mhi_cntrl);
int mhi_pm_m3_transition(struct mhi_controller *mhi_cntrl);
int __mhi_device_get_sync(struct mhi_controller *mhi_cntrl);
int mhi_send_cmd(struct mhi_controller *mhi_cntrl, struct mhi_chan *mhi_chan,
		 enum mhi_cmd_type cmd);
int mhi_download_amss_image(struct mhi_controller *mhi_cntrl);
static inline bool mhi_is_active(struct mhi_controller *mhi_cntrl)
{
	return (mhi_cntrl->dev_state >= MHI_STATE_M0 &&
		mhi_cntrl->dev_state <= MHI_STATE_M3_FAST);
}

static inline void mhi_trigger_resume(struct mhi_controller *mhi_cntrl)
{
	pm_wakeup_event(&mhi_cntrl->mhi_dev->dev, 0);
	mhi_cntrl->runtime_get(mhi_cntrl);
	mhi_cntrl->runtime_put(mhi_cntrl);
}

/* Register access methods */
void mhi_db_brstmode(struct mhi_controller *mhi_cntrl, struct db_cfg *db_cfg,
		     void __iomem *db_addr, dma_addr_t db_val);
void mhi_db_brstmode_disable(struct mhi_controller *mhi_cntrl,
			     struct db_cfg *db_mode, void __iomem *db_addr,
			     dma_addr_t db_val);
int __must_check mhi_read_reg(struct mhi_controller *mhi_cntrl,
			      void __iomem *base, u32 offset, u32 *out);
int __must_check mhi_read_reg_field(struct mhi_controller *mhi_cntrl,
				    void __iomem *base, u32 offset, u32 mask,
				    u32 *out);
int __must_check mhi_poll_reg_field(struct mhi_controller *mhi_cntrl,
				    void __iomem *base, u32 offset, u32 mask,
				    u32 val, u32 delayus, u32 timeout_ms);
void mhi_write_reg(struct mhi_controller *mhi_cntrl, void __iomem *base,
		   u32 offset, u32 val);
int __must_check mhi_write_reg_field(struct mhi_controller *mhi_cntrl,
				     void __iomem *base, u32 offset, u32 mask,
				     u32 val);
void mhi_ring_er_db(struct mhi_event *mhi_event);
void mhi_write_db(struct mhi_controller *mhi_cntrl, void __iomem *db_addr,
		  dma_addr_t db_val);
void mhi_ring_cmd_db(struct mhi_controller *mhi_cntrl, struct mhi_cmd *mhi_cmd);
void mhi_ring_chan_db(struct mhi_controller *mhi_cntrl,
		      struct mhi_chan *mhi_chan);

/* Initialization methods */
int mhi_init_mmio(struct mhi_controller *mhi_cntrl);
int mhi_init_dev_ctxt(struct mhi_controller *mhi_cntrl);
void mhi_deinit_dev_ctxt(struct mhi_controller *mhi_cntrl);
int mhi_init_irq_setup(struct mhi_controller *mhi_cntrl);
void mhi_deinit_free_irq(struct mhi_controller *mhi_cntrl);
int mhi_rddm_prepare(struct mhi_controller *mhi_cntrl,
		      struct image_info *img_info);
void mhi_fw_load_handler(struct mhi_controller *mhi_cntrl);

/* Automatically allocate and queue inbound buffers */
#define MHI_CH_INBOUND_ALLOC_BUFS BIT(0)
int mhi_prepare_channel(struct mhi_controller *mhi_cntrl,
			struct mhi_chan *mhi_chan, unsigned int flags);

int mhi_init_chan_ctxt(struct mhi_controller *mhi_cntrl,
		       struct mhi_chan *mhi_chan);
void mhi_deinit_chan_ctxt(struct mhi_controller *mhi_cntrl,
			  struct mhi_chan *mhi_chan);
void mhi_reset_chan(struct mhi_controller *mhi_cntrl,
		    struct mhi_chan *mhi_chan);

/* Event processing methods */
void mhi_ctrl_ev_task(unsigned long data);
void mhi_ev_task(unsigned long data);
int mhi_process_data_event_ring(struct mhi_controller *mhi_cntrl,
				struct mhi_event *mhi_event, u32 event_quota);
int mhi_process_ctrl_ev_ring(struct mhi_controller *mhi_cntrl,
			     struct mhi_event *mhi_event, u32 event_quota);

/* ISR handlers */
irqreturn_t mhi_irq_handler(int irq_number, void *dev);
irqreturn_t mhi_intvec_threaded_handler(int irq_number, void *dev);
irqreturn_t mhi_intvec_handler(int irq_number, void *dev);

int mhi_gen_tre(struct mhi_controller *mhi_cntrl, struct mhi_chan *mhi_chan,
		struct mhi_buf_info *info, enum mhi_flags flags);
int mhi_map_single_no_bb(struct mhi_controller *mhi_cntrl,
			 struct mhi_buf_info *buf_info);
int mhi_map_single_use_bb(struct mhi_controller *mhi_cntrl,
			  struct mhi_buf_info *buf_info);
void mhi_unmap_single_no_bb(struct mhi_controller *mhi_cntrl,
			    struct mhi_buf_info *buf_info);
void mhi_unmap_single_use_bb(struct mhi_controller *mhi_cntrl,
			     struct mhi_buf_info *buf_info);

#endif /* _MHI_INT_H */
