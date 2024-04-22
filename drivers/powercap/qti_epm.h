/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_EPM_H__
#define __QCOM_EPM_H__

#include <linux/interrupt.h>
#include <linux/ipc_logging.h>
#include <linux/powercap.h>
#include <linux/thermal.h>

struct epm_priv;
struct epm_device;

#define IPC_LOGPAGES 10
#define EPM_DBG(epm, msg, args...) do {				\
		dev_dbg(epm->dev, "%s:" msg, __func__, args);	\
		if ((epm) && (epm)->ipc_log) {			\
			ipc_log_string((epm)->ipc_log,		\
			"[%s] "msg"\n",				\
			 current->comm, args);			\
		}						\
	} while (0)


#define EPM_REG_NAME_LENGTH 32
#define EPM_POWER_CH_MAX  48
#define EPM_TZ_CH_MAX  8
#define EPM_MAX_DATA_MAX 10

/*
 * Different epm modes of operation
 */
enum epm_mode {
	EPM_ACAT_MODE,
	EPM_RCM_MODE,

	/* Keep last */
	EPM_MODE_MAX
};

/*
 * Different epm sdam IDs to use as an index into an array
 */
enum epm_sdam_id {
	CONFIG_SDAM,
	DATA_AVG_SDAM,
	DATA_1_SDAM,
	DATA_2_SDAM,
	DATA_3_SDAM,
	DATA_4_SDAM,
	DATA_5_SDAM,
	DATA_6_SDAM,
	DATA_7_SDAM,
	DATA_8_SDAM,
	DATA_9_SDAM,
	DATA_10_SDAM,
	DATA_11_SDAM,

	/* Keep last */
	MAX_EPM_SDAM
};

/*
 * Data sdam field IDs to use as an index into an array
 */
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
	DATA_SDAM_DIE_TEMP_SID8 = DATA_SDAM_DIE_TEMP_SID1 + EPM_TZ_CH_MAX - 1,
	DATA_SDAM_POWER_LSB_CH1,
	DATA_SDAM_POWER_MSB_CH1,
	DATA_SDAM_POWER_LSB_CH48 = DATA_SDAM_POWER_LSB_CH1 +  2 * (EPM_POWER_CH_MAX - 1),
	DATA_SDAM_POWER_MSB_CH48,

	/* Keep last */
	MAX_SDAM_DATA
};

/*
 * config sdam field IDs to use as an index into an array
 */
enum config_sdam_field_ids {
	CONFIG_SDAM_EPM_MODE,
	CONFIG_SDAM_EPM_STATUS,
	CONFIG_SDAM_MAX_DATA,
	CONFIG_SDAM_MEAS_CFG,
	CONFIG_SDAM_LAST_FULL_SDAM,
	CONFIG_SDAM_CONFIG_1,
	CONFIG_SDAM_PID_1,
	CONFIG_SDAM_CONFIG_48 = CONFIG_SDAM_CONFIG_1 + 2 * (EPM_POWER_CH_MAX - 1),

	/* Keep last */
	MAX_CONFIG_SDAM_DATA
};

/*
 * ACAT mode different epm data types
 */
enum epm_data_type {
	EPM_1S_DATA,
	EPM_10S_AVG_DATA,

	/* Keep last */
	EPM_TYPE_MAX
};

/**
 * struct epm_sdam - EPM sdam data structure
 * @id:		EPM sdam id type
 * @nvmem:	Pointer to nvmem device
 * @lock:	lock to protect multiple read concurrently
 * @last_data:	last full read data copy for current sdam
 */
struct epm_sdam {
	enum epm_sdam_id	id;
	struct nvmem_device	*nvmem;
	struct mutex		lock;
	uint8_t			last_data[MAX_CONFIG_SDAM_DATA];
};

/**
 * struct epm_device_pz_data - EPM device powercap zone data for each data type
 * @pz:		Pointer to powercap zone device
 * @type:	Type of epm acat data types, 1S data to 10S average data
 * @epm_dev:	Pointer to regulator epm channel device
 */
struct epm_device_pz_data {
	struct powercap_zone pz;
	enum epm_data_type type;
	struct epm_device *epm_dev;
};

/**
 * struct epm_device -  Each regulator channel device data
 * @epm_node:	epm device list head member to traverse all devices
 * @priv:	epm hardware instance that this channel is connected to
 * @epm_pz:	array of powercap zone data types for different data retrieval
 * @name:	name of the regulator which is used to identify channel
 * @enabled:	epm channel is enabled or not
 * @sid:	epm channel SID
 * @pid:	epm channel PID
 * @gang_num:	epm channel gang_num
 * @data_offset: epm channel power data offset from DATA sdam base
 * @last_data:	epm channel last 1S data
 * @last_avg_data: epm channel last 10S average data
 * @time_stamp:	 timestamp of last 1S data collected from epm channel
 * @avg_time_stamp: timestamp of last 10S average data collected from epm channel
 * @lock:	lock to protect multiple client read concurrently
 */
struct epm_device {
	struct list_head		epm_node;
	struct epm_priv			*priv;
	struct epm_device_pz_data	epm_pz[EPM_TYPE_MAX];
	char				name[EPM_REG_NAME_LENGTH];
	bool				enabled;
	uint8_t				sid;
	uint8_t				pid;
	uint8_t				gang_num;
	uint8_t                         data_offset;
	uint16_t			last_data[EPM_MAX_DATA_MAX];
	uint16_t			last_avg_data;
	u64				last_data_uw[EPM_MAX_DATA_MAX];
	u64				last_avg_data_uw;
	u64				time_stamp;
	u64				avg_time_stamp;
	struct mutex			lock;
};

/**
 * struct epm_tz_device -  EPM each temeprature device data
 * @offset:	epm tz channel offset from first temperature data
 * @last_temp:	epm thermal zone last read data
 * @time_stamp:	timestamp of last temperature data collected for given tz
 * @priv:	epm hardware instance that this channel is connected to
 * @tz:		array of powercap zone data types for different data retrieval
 * @lock:	lock to protect multiple client read concurrently
 */
struct epm_tz_device {
	uint8_t                         offset;
	int				last_temp;
	u64				time_stamp;
	struct epm_priv			*priv;
	struct thermal_zone_device	*tz;
	struct mutex			lock;
};

/**
 * struct epm_priv - Structure for EPM hardware private data
 * @dev:		Pointer for EPM device
 * @mode:		enum to give current mode of operation
 * @sdam:		Pointer for array of EPM sdams
 * @pct:		pointer to powercap control type
 * @irq:		epm sdam pbs irq number
 * @num_sdams:		Number of SDAMs used for EPM from DT
 * @num_reg:		Number of regulator based on config sdam
 * @max_data:		EPM hardware max_data configuration
 * @data_1s_base_pid:	PID info of 1st 1S sdam of EPM
 * @last_sdam_pid:	PID info of latest 1S data sdam
 * @reg_ppid_map:	array of regulator/rail PPID from devicetree
 * @dt_reg_cnt:		Number of regulator count in devicetree
 * @last_ch_offset:	Last enabled data channel offset
 * @initialized:	EPM hardware initialization is done if it is true
 * @g_enabled:		The epm global enable bit status
 * @ops:		EPM hardware supported ops
 * @config_sdam_data:	Config sdam data dump collected at init
 * @ipc_log:		Handle to ipc_logging
 * @all_1s_read_ts:	Timestamp collected just after epm irq 1S data update
 * @all_avg_read_ts:	Timestamp collected just after epm irq 10S avg data update
 * @epm_dev_head:	List head for all epm channel devices
 * @epm_tz:		Array of list of pmic die temperature devices
 * @sec_read_lock:	lock to protect 1s data update and client request
 * @avg_read_lock:	lock to protect avg data update and client request
 * @avg_data_work:	Workqueue to check avg data if counters are not matching state
 */
struct epm_priv {
	struct device		*dev;
	enum epm_mode           mode;
	struct epm_sdam		*sdam;
	struct powercap_control_type *pct;
	int			irq;
	u32			num_sdams;
	u32			num_reg;
	u8			max_data;
	u16			data_1s_base_pid;
	u16			last_sdam_pid;
	u16			reg_ppid_map[EPM_POWER_CH_MAX];
	u8			dt_reg_cnt;
	u8			dt_tz_cnt;
	u8			last_ch_offset;
	bool			initialized;
	bool			g_enabled;
	struct epm_ops		*ops;
	uint8_t                 *config_sdam_data;
	void			*ipc_log;
	u64			all_1s_read_ts;
	u64			all_avg_read_ts;
	u64			all_tz_read_ts;
	struct list_head	epm_dev_head;
	struct epm_tz_device	*epm_tz;
	struct mutex		sec_read_lock;
	struct mutex		avg_read_lock;
	struct delayed_work	avg_data_work;
};

/**
 * struct epm_ops - Structure for EPM hardware supported ops
 * @init:		EPM hardware init function
 * @get_mode:		Function to get current EPM operation mode
 * @get_power:		Function to get power for EPM channel in us for a given type
 * @get_max_power:	Function to get max power which EPM channel can deliver
 * @get_temp:		Function to get current temperature of a pmic die
 * @get_hw_mon_data:	Function to hardware monitor data for RCM mode
 * @release:		Function to clear all EPM data on exit
 * @suspend:		Function to execute EPM during suspend callback if any
 * @resume:		Function to restore EPM durng resume callback if any
 */
struct epm_ops {
	int (*init)(struct epm_priv *priv);
	enum epm_mode (*get_mode)(struct epm_priv *epm);
	int (*get_power)(struct epm_device *epm_dev, enum epm_data_type type, u64 *power);
	int (*get_max_power)(const struct epm_device *epm_dev, u64 *max_power);
	int (*get_temp)(struct epm_tz_device *epm_tz, int *temp);
	int (*get_hw_mon_data)(struct epm_device *epm, int *data);
	void (*release)(struct epm_priv *epm);
	int (*suspend)(struct epm_priv *epm);
	int (*resume)(struct epm_priv *epm);
};

extern struct epm_ops epm_hw_ops;

#endif /* __QCOM_EPM_H__ */
