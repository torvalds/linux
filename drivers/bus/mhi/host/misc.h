/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef _MHI_CORE_MISC_H_
#define _MHI_CORE_MISC_H_

#include <linux/mhi_misc.h>
#include <linux/msm_pcie.h>

#define MHI_FORCE_WAKE_DELAY_US (100)
#define MHI_IPC_LOG_PAGES (100)
#define MAX_RDDM_TABLE_SIZE (8)
#define MHI_REG_SIZE (SZ_4K)

#define REG_WRITE_QUEUE_LEN 512

/* MHI misc capability registers */
#define MISC_OFFSET (0x24)
#define MISC_CAP_MASK (0xFFFFFFFF)
#define MISC_CAP_SHIFT (0)

#define CAP_CAPID_MASK (0xFF000000)
#define CAP_CAPID_SHIFT (24)
#define CAP_NEXT_CAP_MASK (0x00FFF000)
#define CAP_NEXT_CAP_SHIFT (12)

/* MHI Bandwidth scaling offsets */
#define BW_SCALE_CFG_OFFSET (0x04)
#define BW_SCALE_CFG_CHAN_DB_ID_MASK (0xFE000000)
#define BW_SCALE_CFG_CHAN_DB_ID_SHIFT (25)
#define BW_SCALE_CFG_ENABLED_MASK (0x01000000)
#define BW_SCALE_CFG_ENABLED_SHIFT (24)
#define BW_SCALE_CFG_ER_ID_MASK (0x00F80000)
#define BW_SCALE_CFG_ER_ID_SHIFT (19)

#define BW_SCALE_CAP_ID (3)
#define MHI_TRE_GET_EV_BW_REQ_SEQ(tre) (((tre)->dword[0] >> 8) & 0xFF)
#define MHI_BW_SCALE_CHAN_DB		126

#define MHI_BW_SCALE_SETUP(er_index) (((MHI_BW_SCALE_CHAN_DB << \
	BW_SCALE_CFG_CHAN_DB_ID_SHIFT) & BW_SCALE_CFG_CHAN_DB_ID_MASK) | \
	((1 << BW_SCALE_CFG_ENABLED_SHIFT) & BW_SCALE_CFG_ENABLED_MASK) | \
	(((er_index) << BW_SCALE_CFG_ER_ID_SHIFT) & BW_SCALE_CFG_ER_ID_MASK))

#define MHI_BW_SCALE_RESULT(status, seq) (((status) & 0xF) << 8 | \
					((seq) & 0xFF))

enum mhi_bw_scale_req_status {
	MHI_BW_SCALE_SUCCESS = 0x0,
	MHI_BW_SCALE_INVALID = 0x1,
	MHI_BW_SCALE_NACK    = 0xF,
};

/* subsystem failure reason cfg command */
#define MHI_TRE_CMD_SFR_CFG_PTR(ptr) (ptr)
#define MHI_TRE_CMD_SFR_CFG_DWORD0(len) (len)
#define MHI_TRE_CMD_SFR_CFG_DWORD1 (MHI_CMD_SFR_CFG << 16)

/* MHI Timesync offsets */
#define TIMESYNC_CFG_OFFSET (0x04)
#define TIMESYNC_CFG_ENABLED_MASK (0x80000000)
#define TIMESYNC_CFG_ENABLED_SHIFT (31)
#define TIMESYNC_CFG_CHAN_DB_ID_MASK (0x0000FF00)
#define TIMESYNC_CFG_CHAN_DB_ID_SHIFT (8)
#define TIMESYNC_CFG_ER_ID_MASK (0x000000FF)
#define TIMESYNC_CFG_ER_ID_SHIFT (0)

#define TIMESYNC_TIME_LOW_OFFSET (0x8)
#define TIMESYNC_TIME_HIGH_OFFSET (0xC)

#define MHI_TIMESYNC_CHAN_DB (125)
#define TIMESYNC_CAP_ID (2)

#define MHI_TIMESYNC_DB_SETUP(er_index) ((MHI_TIMESYNC_CHAN_DB << \
	TIMESYNC_CFG_CHAN_DB_ID_SHIFT) & TIMESYNC_CFG_CHAN_DB_ID_MASK | \
	(1 << TIMESYNC_CFG_ENABLED_SHIFT) & TIMESYNC_CFG_ENABLED_MASK | \
	((er_index) << TIMESYNC_CFG_ER_ID_SHIFT) & TIMESYNC_CFG_ER_ID_MASK)

/* MHI WLAN specific offsets */
#define MHI_HOST_NOTIFY_CFG_OFFSET (0x04)
#define MHI_HOST_NOTIFY_ENABLE (1)
#define MHI_HOST_NOTIFY_DB (124)
#define MHI_HOST_NOTIFY_CAP_ID (6)
#define MHI_HOST_NOTIFY_CFG_ENABLED_MASK (0x80000000)
#define MHI_HOST_NOTIFY_CFG_ENABLED_SHIFT (31)
#define MHI_HOST_NOTIFY_CFG_CHAN_DB_ID_MASK (0x000000FF)
#define MHI_HOST_NOTIFY_CFG_CHAN_DB_ID_SHIFT (0)
#define MHI_HOST_NOTIFY_ENABLE_SETUP ((MHI_HOST_NOTIFY_ENABLE << \
					MHI_HOST_NOTIFY_CFG_ENABLED_SHIFT) & \
					(MHI_HOST_NOTIFY_CFG_ENABLED_MASK))
#define MHI_HOST_NOTIFY_DB_SETUP ((MHI_HOST_NOTIFY_DB << \
					MHI_HOST_NOTIFY_CFG_CHAN_DB_ID_SHIFT) & \
					(MHI_HOST_NOTIFY_CFG_CHAN_DB_ID_MASK))
#define MHI_HOST_NOTIFY_CFG_SETUP ((MHI_HOST_NOTIFY_ENABLE_SETUP) | \
					(MHI_HOST_NOTIFY_DB_SETUP))

#define MHI_VERB(dev, fmt, ...) do { \
	struct mhi_private *mhi_priv = \
		dev_get_drvdata(&mhi_cntrl->mhi_dev->dev); \
	dev_dbg(dev, "[D][%s] " fmt, __func__, ##__VA_ARGS__); \
	if (mhi_priv && mhi_priv->log_lvl <= MHI_MSG_LVL_VERBOSE) \
		ipc_log_string(mhi_priv->log_buf, "[D][%s] " fmt, __func__, \
			       ##__VA_ARGS__); \
} while (0)

#define MHI_LOG(dev, fmt, ...) do {	\
	struct mhi_private *mhi_priv = \
		dev_get_drvdata(&mhi_cntrl->mhi_dev->dev); \
	dev_dbg(dev, "[I][%s] " fmt, __func__, ##__VA_ARGS__); \
	if (mhi_priv && mhi_priv->log_lvl <= MHI_MSG_LVL_INFO) \
		ipc_log_string(mhi_priv->log_buf, "[I][%s] " fmt, __func__, \
			       ##__VA_ARGS__); \
} while (0)

#define MHI_ERR(dev, fmt, ...) do {	\
	struct mhi_private *mhi_priv = \
		dev_get_drvdata(&mhi_cntrl->mhi_dev->dev); \
	dev_err(dev, "[E][%s] " fmt, __func__, ##__VA_ARGS__); \
	if (mhi_priv && mhi_priv->log_lvl <= MHI_MSG_LVL_ERROR) \
		ipc_log_string(mhi_priv->log_buf, "[E][%s] " fmt, __func__, \
			       ##__VA_ARGS__); \
} while (0)

#define MHI_CRITICAL(dev, fmt, ...) do { \
	struct mhi_private *mhi_priv = \
		dev_get_drvdata(&mhi_cntrl->mhi_dev->dev); \
	dev_crit(dev, "[C][%s] " fmt, __func__, ##__VA_ARGS__); \
	if (mhi_priv && mhi_priv->log_lvl <= MHI_MSG_LVL_CRITICAL) \
		ipc_log_string(mhi_priv->log_buf, "[C][%s] " fmt, __func__, \
			       ##__VA_ARGS__); \
} while (0)

/**
 * struct rddm_table_info - rddm table info
 * @base_address - Start offset of the file
 * @actual_phys_address - phys addr offset of file
 * @size - size of file
 * @description - file description
 * @file_name - name of file
 */
struct rddm_table_info {
	u64 base_address;
	u64 actual_phys_address;
	u64 size;
	char description[20];
	char file_name[20];
};

/**
 * struct rddm_header - rddm header
 * @version - header ver
 * @header_size - size of header
 * @rddm_table_info - array of rddm table info
 */
struct rddm_header {
	u32 version;
	u32 header_size;
	struct rddm_table_info table_info[MAX_RDDM_TABLE_SIZE];
};

/**
 * struct file_info - keeping track of file info while traversing the rddm
 * table header
 * @file_offset - current file offset
 * @seg_idx - mhi buf seg array index
 * @rem_seg_len - remaining length of the segment containing current file
 */
struct file_info {
	u8 *file_offset;
	u32 file_size;
	u32 seg_idx;
	u32 rem_seg_len;
};

/**
 * struct reg_write_info - offload reg write info
 * @reg_addr - register address
 * @val - value to be written to register
 * @chan - channel number
 * @valid - entry is valid or not
 */
struct reg_write_info {
	void __iomem *reg_addr;
	u32 val;
	bool valid;
};

/**
 * struct mhi_private - For private variables of an MHI controller
 */
struct mhi_private {
	struct list_head node;
	struct mhi_controller *mhi_cntrl;
	enum MHI_DEBUG_LEVEL log_lvl;
	void *log_buf;
	u32 saved_pm_state;
	enum mhi_state saved_dev_state;
	u32 m2_timeout_ms;
	void *priv_data;
	void __iomem *bw_scale_db;
	int (*bw_scale)(struct mhi_controller *mhi_cntrl,
			struct mhi_link_info *link_info);
	phys_addr_t base_addr;
	u32 numeric_id;
	u32 bw_response;
	struct mhi_sfr_info *sfr_info;
	struct mhi_timesync *timesync;

	/* reg write offload */
	struct workqueue_struct *offload_wq;
	struct work_struct reg_write_work;
	struct reg_write_info *reg_write_q;
	atomic_t write_idx;
	u32 read_idx;
};

/**
 * struct mhi_bus - For MHI controller debug
 */
struct mhi_bus {
	struct list_head controller_list;
	struct mutex lock;
};

/**
 * struct mhi_sfr_info - For receiving MHI subsystem failure reason
 */
struct mhi_sfr_info {
	void *buf_addr;
	dma_addr_t dma_addr;
	size_t len;
	char *str;
	unsigned int ccs;
	struct completion completion;
};

/**
 * struct mhi_timesync - For enabling use of MHI time synchronization feature
 */
struct mhi_timesync {
	u64 (*time_get)(struct mhi_controller *mhi_cntrl);
	int (*lpm_disable)(struct mhi_controller *mhi_cntrl);
	int (*lpm_enable)(struct mhi_controller *mhi_cntrl);
	void __iomem *time_reg;
	void __iomem *time_db;
	u32 int_sequence;
	u64 local_time;
	u64 remote_time;
	bool db_pending;
	struct completion completion;
	spinlock_t lock; /* list protection */
	struct list_head head;
	struct mutex mutex;
};

/**
 * struct tsync_node - Stores requests when using the timesync doorbell method
 */
struct tsync_node {
	struct list_head node;
	u32 sequence;
	u64 remote_time;
	struct mhi_device *mhi_dev;
	void (*cb_func)(struct mhi_device *mhi_dev, u32 sequence,
			u64 local_time, u64 remote_time);
};

#ifdef CONFIG_MHI_BUS_MISC
void mhi_misc_init(void);
void mhi_misc_exit(void);
int mhi_misc_init_mmio(struct mhi_controller *mhi_cntrl);
int mhi_misc_register_controller(struct mhi_controller *mhi_cntrl);
void mhi_misc_unregister_controller(struct mhi_controller *mhi_cntrl);
int mhi_misc_sysfs_create(struct mhi_controller *mhi_cntrl);
void mhi_misc_sysfs_destroy(struct mhi_controller *mhi_cntrl);
int mhi_process_misc_bw_ev_ring(struct mhi_controller *mhi_cntrl,
				struct mhi_event *mhi_event, u32 event_quota);
int mhi_process_misc_tsync_ev_ring(struct mhi_controller *mhi_cntrl,
				   struct mhi_event *mhi_event, u32 event_quota);
void mhi_misc_mission_mode(struct mhi_controller *mhi_cntrl);
void mhi_misc_dbs_pending(struct mhi_controller *mhi_cntrl);
void mhi_misc_disable(struct mhi_controller *mhi_cntrl);
void mhi_misc_cmd_configure(struct mhi_controller *mhi_cntrl,
			    unsigned int type, u64 *ptr, u32 *dword0,
			    u32 *dword1);
void mhi_misc_cmd_completion(struct mhi_controller *mhi_cntrl,
			     unsigned int type, unsigned int ccs);
void mhi_write_offload_wakedb(struct mhi_controller *mhi_cntrl, int db_val);
void mhi_reset_reg_write_q(struct mhi_controller *mhi_cntrl);
void mhi_force_reg_write(struct mhi_controller *mhi_cntrl);
#else
static inline void mhi_misc_init(void)
{
}

static inline void mhi_misc_exit(void)
{
}

static inline int mhi_misc_init_mmio(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline int mhi_misc_register_controller(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline void mhi_misc_unregister_controller(struct mhi_controller
						  *mhi_cntrl)
{
}

static inline int mhi_misc_sysfs_create(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline void mhi_misc_sysfs_destroy(struct mhi_controller *mhi_cntrl)
{
}

static inline int mhi_process_misc_bw_ev_ring(struct mhi_controller *mhi_cntrl,
				struct mhi_event *mhi_event, u32 event_quota)
{
	return 0;
}

static inline int mhi_process_misc_tsync_ev_ring
				(struct mhi_controller *mhi_cntrl,
				 struct mhi_event *mhi_event, u32 event_quota)
{
	return 0;
}

static inline void mhi_misc_mission_mode(struct mhi_controller *mhi_cntrl)
{
}

static inline void mhi_special_dbs_pending(struct mhi_controller *mhi_cntrl)
{
}

static inline void mhi_misc_disable(struct mhi_controller *mhi_cntrl)
{
}

static inline void mhi_misc_cmd_configure(struct mhi_controller *mhi_cntrl,
					  unsigned int type, u64 *ptr,
					  u32 *dword0, u32 *dword1)
{
}

static inline void mhi_misc_cmd_completion(struct mhi_controller *mhi_cntrl,
					   unsigned int type, unsigned int ccs)
{
}

static inline void mhi_write_offload_wakedb(struct mhi_controller *mhi_cntrl,
					    int db_val)
{
}

void mhi_reset_reg_write_q(struct mhi_controller *mhi_cntrl)
{
}

void mhi_force_reg_write(struct mhi_controller *mhi_cntrl)
{
}
#endif

#endif /* _MHI_CORE_MISC_H_ */
