/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */

#ifndef ADF_HEARTBEAT_H_
#define ADF_HEARTBEAT_H_

#include <linux/types.h>

struct adf_accel_dev;
struct dentry;

#define ADF_CFG_HB_TIMER_MIN_MS 200
#define ADF_CFG_HB_TIMER_DEFAULT_MS 500
#define ADF_CFG_HB_COUNT_THRESHOLD 3

#define ADF_CFG_HB_RESET_MS 5000

enum adf_device_heartbeat_status {
	HB_DEV_UNRESPONSIVE = 0,
	HB_DEV_ALIVE,
	HB_DEV_UNSUPPORTED,
};

/* Heartbeat counter pair */
struct hb_cnt_pair {
	__u16 resp_heartbeat_cnt;
	__u16 req_heartbeat_cnt;
};

struct adf_heartbeat {
	unsigned int hb_sent_counter;
	unsigned int hb_failed_counter;
	unsigned int hb_timer;
	u64 last_hb_check_time;
	u64 last_hb_reset_time;
	bool ctrs_cnt_checked;
	struct hb_dma_addr {
		dma_addr_t phy_addr;
		void *virt_addr;
	} dma;
	struct {
		struct dentry *base_dir;
		struct dentry *status;
		struct dentry *cfg;
		struct dentry *sent;
		struct dentry *failed;
#ifdef CONFIG_CRYPTO_DEV_QAT_ERROR_INJECTION
		struct dentry *inject_error;
#endif
	} dbgfs;
};

#ifdef CONFIG_DEBUG_FS
int adf_heartbeat_init(struct adf_accel_dev *accel_dev);
int adf_heartbeat_start(struct adf_accel_dev *accel_dev);
void adf_heartbeat_shutdown(struct adf_accel_dev *accel_dev);

int adf_heartbeat_ms_to_ticks(struct adf_accel_dev *accel_dev, unsigned int time_ms,
			      uint32_t *value);
int adf_heartbeat_save_cfg_param(struct adf_accel_dev *accel_dev,
				 unsigned int timer_ms);
void adf_heartbeat_status(struct adf_accel_dev *accel_dev,
			  enum adf_device_heartbeat_status *hb_status);
void adf_heartbeat_check_ctrs(struct adf_accel_dev *accel_dev);

#ifdef CONFIG_CRYPTO_DEV_QAT_ERROR_INJECTION
int adf_heartbeat_inject_error(struct adf_accel_dev *accel_dev);
#else
static inline int adf_heartbeat_inject_error(struct adf_accel_dev *accel_dev)
{
	return -EPERM;
}
#endif

#else
static inline int adf_heartbeat_init(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static inline int adf_heartbeat_start(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static inline void adf_heartbeat_shutdown(struct adf_accel_dev *accel_dev)
{
}

static inline int adf_heartbeat_save_cfg_param(struct adf_accel_dev *accel_dev,
					       unsigned int timer_ms)
{
	return 0;
}

static inline void adf_heartbeat_check_ctrs(struct adf_accel_dev *accel_dev)
{
}
#endif
#endif /* ADF_HEARTBEAT_H_ */
