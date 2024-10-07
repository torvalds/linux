/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_QPT_H__
#define __QCOM_QPT_H__

#include <linux/interrupt.h>
#include <linux/ipc_logging.h>
#include <linux/powercap.h>

struct qpt_priv;
struct qpt_device;

#define IPC_LOGPAGES 10
#define QPT_DBG(qpt, msg, args...) do {				\
		dev_dbg(qpt->dev, "%s:" msg, __func__, args);	\
		if ((qpt) && (qpt)->ipc_log) {			\
			ipc_log_string((qpt)->ipc_log,		\
			"[%s] "msg"\n",				\
			 current->comm, args);			\
		}						\
	} while (0)

#define QPT_REG_NAME_LENGTH 32
#define QPT_POWER_CH_MAX  48
#define QPT_TZ_CH_MAX  8
#define QPT_MAX_DATA_MAX 10

/* Different qpt modes of operation */
enum qpt_mode {
	QPT_ACAT_MODE,
	QPT_RCM_MODE,
	QPT_MODE_MAX
};

/* Different qpt sdam IDs to use as an index into an array */
enum qpt_sdam_id {
	CONFIG_SDAM,
	DATA_AVG_SDAM,
	MAX_QPT_SDAM
};

/* Data sdam field IDs to use as an index into an array */
enum data_sdam_field_ids {
	DATA_SDAM_SEQ_START,
	DATA_SDAM_SEQ_END,
	DATA_SDAM_NUM_RECORDS,
	DATA_SDAM_RTC0,
	DATA_SDAM_RTC1,
	DATA_SDAM_RTC2,
	DATA_SDAM_RTC3,
	DATA_SDAM_VPH_LSB,
	DATA_SDAM_VPH_MSB,
	DATA_SDAM_DIE_TEMP_SID1,
	DATA_SDAM_DIE_TEMP_SID8 = DATA_SDAM_DIE_TEMP_SID1 + QPT_TZ_CH_MAX - 1,
	DATA_SDAM_POWER_LSB_CH1,
	DATA_SDAM_POWER_MSB_CH1,
	DATA_SDAM_POWER_LSB_CH48 = DATA_SDAM_POWER_LSB_CH1 +  2 * (QPT_POWER_CH_MAX - 1),
	DATA_SDAM_POWER_MSB_CH48,
	MAX_SDAM_DATA
};

/* config sdam field IDs to use as an index into an array */
enum config_sdam_field_ids {
	CONFIG_SDAM_QPT_MODE,
	CONFIG_SDAM_QPT_STATUS,
	CONFIG_SDAM_MAX_DATA,
	CONFIG_SDAM_MEAS_CFG,
	CONFIG_SDAM_LAST_FULL_SDAM,
	CONFIG_SDAM_CONFIG_1,
	CONFIG_SDAM_PID_1,
	CONFIG_SDAM_CONFIG_48 = CONFIG_SDAM_CONFIG_1 + 2 * (QPT_POWER_CH_MAX - 1),
	MAX_CONFIG_SDAM_DATA
};

/**
 * struct qpt_sdam - QPT sdam data structure
 * @id:		QPT sdam id type
 * @nvmem:	Pointer to nvmem device
 * @lock:	lock to protect multiple read concurrently
 * @last_data:	last full read data copy for current sdam
 */
struct qpt_sdam {
	enum qpt_sdam_id	id;
	struct nvmem_device	*nvmem;
	struct mutex		lock;
	uint8_t			last_data[MAX_CONFIG_SDAM_DATA];
};

/**
 * struct qpt_device -  Each regulator channel device data
 * @qpt_node:	qpt device list head member to traverse all devices
 * @priv:	qpt hardware instance that this channel is connected to
 * @pz:	array of powercap zone data types for different data retrieval
 * @name:	name of the regulator which is used to identify channel
 * @enabled:	qpt channel is enabled or not
 * @sid:	qpt channel SID
 * @pid:	qpt channel PID
 * @gang_num:	qpt channel gang_num
 * @data_offset:	qpt channel power data offset from DATA sdam base
 * @last_data:	qpt channel last 1S data
 * @last_data_uw:	qpt channel last 10S average data
 * @lock:	lock to protect multiple client read concurrently
 */
struct qpt_device {
	struct list_head		qpt_node;
	struct qpt_priv			*priv;
	struct powercap_zone		pz;
	char				name[QPT_REG_NAME_LENGTH];
	bool				enabled;
	uint8_t				sid;
	uint8_t				pid;
	uint8_t				gang_num;
	uint8_t				data_offset;
	uint16_t			last_data;
	u64				last_data_uw;
	struct mutex			lock;
};

/**
 * struct qpt_priv - Structure for QPT hardware private data
 * @dev:		Pointer for QPT device
 * @mode:		enum to give current mode of operation
 * @sdam:		Pointer for array of QPT sdams
 * @pct:		pointer to powercap control type
 * @irq:		qpt sdam pbs irq number
 * @num_sdams:		Number of SDAMs used for QPT from DT
 * @num_reg:		Number of regulator based on config sdam
 * @max_data:		QPT hardware max_data configuration
 * @reg_ppid_map:	array of regulator/rail PPID from devicetree
 * @dt_reg_cnt:		Number of regulator count in devicetree
 * @last_ch_offset:	Last enabled data channel offset
 * @initialized:	QPT hardware initialization is done if it is true
 * @irq_enabled:	The qpt irq enable/disable status
 * @in_suspend:		The QPT driver suspend status
 * @ops:		QPT hardware supported ops
 * @config_sdam_data:	Config sdam data dump collected at init
 * @ipc_log:		Handle to ipc_logging
 * @hw_read_ts:		Timestamp collected just after qpt irq data update
 * @rtc_ts:		RTC Timestamp collected just after qpt irq data update
 * @qpt_dev_head:	List head for all qpt channel devices
 * @hw_read_lock:	lock to protect avg data update and client request
 * @genpd_nb:		Genpd notifier for apps idle notification
 */
struct qpt_priv {
	struct device		*dev;
	enum qpt_mode		mode;
	struct qpt_sdam		*sdam;
	struct powercap_control_type		*pct;
	int			irq;
	u32			num_sdams;
	u32			num_reg;
	u8			max_data;
	u16			reg_ppid_map[QPT_POWER_CH_MAX];
	u8			dt_reg_cnt;
	u8			last_ch_offset;
	bool			initialized;
	bool			irq_enabled;
	atomic_t		in_suspend;
	struct qpt_ops		*ops;
	uint8_t			*config_sdam_data;
	void			*ipc_log;
	u64			hw_read_ts;
	u64			rtc_ts;
	struct list_head	qpt_dev_head;
	struct mutex		hw_read_lock;
	struct notifier_block	genpd_nb;
};

/**
 * struct qpt_ops - Structure for QPT hardware supported ops
 * @init:		QPT hardware init function
 * @get_mode:		Function to get current QPT operation mode
 * @get_power:		Function to get power for QPT channel in us for a given type
 * @get_max_power:	Function to get max power which QPT channel can deliver
 * @release:		Function to clear all QPT data on exit
 * @suspend:		Function to execute QPT during suspend callback if any
 * @resume:		Function to restore QPT durng resume callback if any
 */
struct qpt_ops {
	int (*init)(struct qpt_priv *priv);
	void (*get_power)(struct qpt_device *qpt_dev, u64 *power);
	int (*get_max_power)(const struct qpt_device *qpt_dev, u64 *max_power);
	void (*release)(struct qpt_priv *qpt);
	int (*suspend)(struct qpt_priv *qpt);
	int (*resume)(struct qpt_priv *qpt);
};

extern struct qpt_ops qpt_hw_ops;
extern void qpt_sysfs_notify(struct qpt_priv *qpt);

#endif /* __QCOM_QPT_H__ */
