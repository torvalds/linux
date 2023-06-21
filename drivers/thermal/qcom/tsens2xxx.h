/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_TSENS_H__
#define __QCOM_TSENS_H__

#include <linux/kernel.h>
#include <linux/thermal.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/ipc_logging.h>

#define DEBUG_SIZE					10
#define TSENS_MAX_SENSORS			16
#define TSENS_NUM_SENSORS_8937		11
#define TSENS_NUM_SENSORS_405		10
#define TSENS_CONTROLLER_ID(n)			(n)
#define TSENS_CTRL_ADDR(n)			(n)
#define TSENS_TM_SN_STATUS(n)			((n) + 0xa0)

#define ONE_PT_CALIB		0x1
#define ONE_PT_CALIB2		0x2
#define TWO_PT_CALIB		0x3

#define SLOPE_FACTOR		1000
#define SLOPE_DEFAULT		3200

#define IPC_LOGPAGES 10
#define MIN_TEMP_DEF_OFFSET		0xFF

#define TSENS_DRIVER_NAME                       "msm-tsens"

enum tsens_trip_type {
	TSENS_TRIP_CONFIGURABLE_HI = 4,
	TSENS_TRIP_CONFIGURABLE_LOW
};

enum tsens_dbg_type {
	TSENS_DBG_POLL,
	TSENS_DBG_LOG_TEMP_READS,
	TSENS_DBG_LOG_INTERRUPT_TIMESTAMP,
	TSENS_DBG_LOG_BUS_ID_DATA,
	TSENS_DBG_MTC_DATA,
	TSENS_DBG_LOG_MAX
};

#define tsens_sec_to_msec_value		1000

struct tsens_device;

#ifdef CONFIG_DEBUG_FS
#define TSENS_IPC(idx, dev, msg, args...) do { \
		if (dev) { \
			if ((idx == 0) && (dev)->ipc_log0) \
				ipc_log_string((dev)->ipc_log0, \
					"%s: " msg, __func__, args); \
			else if ((idx == 1) && (dev)->ipc_log1) \
				ipc_log_string((dev)->ipc_log1, \
					"%s: " msg, __func__, args); \
			else if ((idx == 2) && (dev)->ipc_log2) \
				ipc_log_string((dev)->ipc_log2, \
					"%s: " msg, __func__, args); \
			else \
				pr_debug("tsens: invalid logging index\n"); \
		} \
	} while (0)
#define TSENS_DUMP(dev, msg, args...) do {				\
		TSENS_IPC(2, dev, msg, args); \
		pr_info(msg, ##args);	\
	} while (0)
#define TSENS_ERR(dev, msg, args...) do {				\
		pr_err(msg, ##args);	\
		TSENS_IPC(1, dev, msg, args); \
	} while (0)
#define TSENS_INFO(dev, msg, args...) do {				\
		pr_info(msg, ##args);	\
		TSENS_IPC(1, dev, msg, args); \
	} while (0)
#define TSENS_DBG(dev, msg, args...) do {				\
		pr_debug(msg, ##args);	\
		if (dev) { \
			TSENS_IPC(0, dev, msg, args); \
		}	\
	} while (0)
#define TSENS_DBG1(dev, msg, args...) do {				\
		pr_debug(msg, ##args);	\
		if (dev) { \
			TSENS_IPC(1, dev, msg, args); \
		}	\
	} while (0)
#else
#define	TSENS_DBG1(dev, msg, x...)		pr_debug(msg, ##x)
#define	TSENS_DBG(dev, msg, x...)		pr_debug(msg, ##x)
#define	TSENS_INFO(dev, msg, x...)		pr_info(msg, ##x)
#define	TSENS_ERR(dev, msg, x...)		pr_err(msg, ##x)
#define	TSENS_DUMP(dev, msg, x...)		pr_info(msg, ##x)
#endif

#if IS_ENABLED(CONFIG_THERMAL_TSENS_LEGACY)
int tsens2xxx_dbg(struct tsens_device *data, u32 id, u32 dbg_type, int *temp);
#else
static inline int tsens2xxx_dbg(struct tsens_device *data, u32 id,
						u32 dbg_type, int *temp)
{ return -ENXIO; }
#endif

struct tsens_dbg {
	u32				idx;
	unsigned long long		time_stmp[DEBUG_SIZE];
	unsigned long			temp[DEBUG_SIZE];
};

struct tsens_dbg_context {
	struct tsens_device		*tmdev;
	struct tsens_dbg		sensor_dbg_info[TSENS_MAX_SENSORS];
	int				tsens_critical_wd_cnt;
	u32				irq_idx;
	unsigned long long		irq_time_stmp[DEBUG_SIZE];
	struct delayed_work		tsens_critical_poll_test;
};

struct tsens_context {
	enum thermal_device_mode	high_th_state;
	enum thermal_device_mode	low_th_state;
	enum thermal_device_mode	crit_th_state;
	int				high_temp;
	int				low_temp;
	int				crit_temp;
	int				high_adc_code;
	int				low_adc_code;
};

struct tsens_sensor {
	struct tsens_device		*tmdev;
	struct thermal_zone_device	*tzd;
	u32				hw_id;
	u32				id;
	const char			*sensor_name;
	struct tsens_context		thr_state;
	int				offset;
	int				slope;
	int				cached_temp;
};

/**
 * struct tsens_ops - operations as supported by the tsens device
 * @init: Function to initialize the tsens device
 * @get_temp: Function which returns the temp in millidegC
 */
struct tsens_ops {
	int (*hw_init)(struct tsens_device *tmdev);
	int (*get_temp)(struct tsens_sensor *tm_sensor, int *temp);
	int (*set_trips)(struct tsens_sensor *tm_sensor, int low, int high);
	int (*interrupts_reg)(struct tsens_device *tmdev);
	int (*dbg)(struct tsens_device *tmdev, u32 id, u32 dbg_type,
								int *temp);
	int (*sensor_en)(struct tsens_device *tmdev, u32 sensor_id);
	int (*calibrate)(struct tsens_device *tmdev);
	int (*suspend)(struct tsens_device *tmdev);
	int (*resume)(struct tsens_device *tmdev);
};

struct tsens_irqs {
	const char			*name;
	irqreturn_t (*handler)(int irq, void *data);
};

/**
 * struct tsens_data - tsens instance specific data
 * @num_sensors: Max number of sensors supported by platform
 * @ops: operations the tsens instance supports
 * @hw_ids: Subset of sensors ids supported by platform, if not the first n
 */
struct tsens_data {
	const u32			num_sensors;
	const struct tsens_ops		*ops;
	unsigned int			*hw_ids;
	u32				temp_factor;
	bool				cycle_monitor;
	u32				cycle_compltn_monitor_mask;
	bool				wd_bark;
	u32				wd_bark_mask;
	bool				valid_status_check;
	u32				ver_major;
	u32				ver_minor;
};

struct tsens_device {
	struct device			*dev;
	struct platform_device		*pdev;
	struct list_head		list;
	struct regmap			*map;
	struct regmap_field		*status_field;
	void __iomem			*tsens_srot_addr;
	void __iomem			*tsens_tm_addr;
	void __iomem			*tsens_calib_addr;
	const struct tsens_ops		*ops;
	void					*ipc_log0;
	void					*ipc_log1;
	void					*ipc_log2;
	phys_addr_t				phys_addr_tm;
	struct tsens_dbg_context	tsens_dbg;
	spinlock_t			tsens_crit_lock;
	spinlock_t			tsens_upp_low_lock;
	const struct tsens_data		*ctrl_data;
	int				trdy_fail_ctr;
	struct tsens_sensor		zeroc;
	u8				zeroc_sensor_id;
	struct workqueue_struct		*tsens_reinit_work;
	struct work_struct		therm_fwk_notify;
	bool				tsens_reinit_wa;
	int                             tsens_reinit_cnt;
	struct tsens_sensor             sensor[0];
};

extern const struct tsens_data data_tsens2xxx, data_tsens23xx, data_tsens24xx,
		data_tsens26xx;
extern struct list_head tsens_device_list;

extern int tsens_2xxx_get_zeroc_status(
		struct tsens_sensor *sensor, int *status);

#endif /* __QCOM_TSENS_H__ */
