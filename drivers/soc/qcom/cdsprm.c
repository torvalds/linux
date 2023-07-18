// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */


/*
 * This driver uses rpmsg to communicate with CDSP and receive requests
 * for CPU L3 frequency and QoS along with Cx Limit management and
 * thermal cooling handling.
 */

#define pr_fmt(fmt) "cdsprm: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/pm_qos.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rpmsg.h>
#include <linux/thermal.h>
#include <linux/debugfs.h>
#include <asm/arch_timer.h>
#include <linux/soc/qcom/cdsprm.h>
#include <linux/soc/qcom/cdsprm_cxlimit.h>
#include <linux/soc/qcom/cdsprm_dcvs_clients_votes.h>

#define SYSMON_CDSP_FEATURE_L3_RX				1
#define SYSMON_CDSP_FEATURE_RM_RX				2
#define SYSMON_CDSP_FEATURE_COMPUTE_PRIO_TX		3
#define SYSMON_CDSP_FEATURE_NPU_LIMIT_TX		4
#define SYSMON_CDSP_FEATURE_NPU_LIMIT_RX		5
#define SYSMON_CDSP_FEATURE_NPU_ACTIVITY_TX		6
#define SYSMON_CDSP_FEATURE_NPU_ACTIVITY_RX		7
#define SYSMON_CDSP_FEATURE_NPU_CORNER_TX		8
#define SYSMON_CDSP_FEATURE_NPU_CORNER_RX		9
#define SYSMON_CDSP_FEATURE_THERMAL_LIMIT_TX	10
#define SYSMON_CDSP_FEATURE_CAMERA_ACTIVITY_TX	11
#define SYSMON_CDSP_FEATURE_VERSION_RX			12
#define SYSMON_CDSP_FEATURE_VTCM_CONFIG			13
#define SYSMON_CDSP_VTCM_SEND_TX_STATUS			14
#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
#define SYSMON_CDSP_VTCM_SEND_TEST_TX_STATUS	15
#endif
#define SYSMON_CDSP_FEATURE_CRM_PDKILL			16
#define SYSMON_CDSP_CRM_PDKILL_RX_STATUS		17
#define SYSMON_CDSP_FEATURE_GET_DCVS_CLIENTS	20
#define SYSMON_CDSP_DCVS_CLIENTS_RX_STATUS		21


#define SYSMON_CDSP_QOS_FLAG_IGNORE				0
#define SYSMON_CDSP_QOS_FLAG_ENABLE				1
#define SYSMON_CDSP_QOS_FLAG_DISABLE			2
#define QOS_LATENCY_DISABLE_VALUE				-1
#define SYS_CLK_TICKS_PER_MS					19200
#define CDSPRM_MSG_QUEUE_DEPTH					50
#define CDSP_THERMAL_MAX_STATE					10
#define HVX_THERMAL_MAX_STATE					10
#define NUM_VTCM_PARTITIONS_MAX					16
#define NUM_VTCM_APPID_MAPS_MAX					32
#define SYSMON_CDSP_GLINK_VERSION				0x2
#define WAIT_FOR_DCVS_CLIENTDATA_TIMEOUT		msecs_to_jiffies(100)
#define DCVS_CLIENTS_VOTES_SLEEP_LATENCY_THRESHOLD_US	5000

enum {
	VTCM_PARTITION_REMOVE = 0,
	VTCM_PARTITION_SET = 1,
	VTCM_PARTITION_TEST = 2,
};

struct vtcm_partition_info_t {
	unsigned char partition_id;
	unsigned int size;
	unsigned int flags;
};

struct vtcm_appid_map_t {
	unsigned char app_id;
	unsigned char partition_id;
};

struct vtcm_partition_interface {
	unsigned int command;
	unsigned char num_partitions;
	unsigned char num_app_id_maps;
	struct vtcm_partition_info_t partitions[NUM_VTCM_PARTITIONS_MAX];
	struct vtcm_appid_map_t app2partition[NUM_VTCM_APPID_MAPS_MAX];
};

struct sysmon_l3_msg {
	unsigned int l3_clock_khz;
};

struct sysmon_rm_msg {
	unsigned int b_qos_flag;
	unsigned int timetick_low;
	unsigned int timetick_high;
};

struct sysmon_npu_limit_msg {
	unsigned int corner;
};

struct sysmon_npu_limit_ack {
	unsigned int corner;
};

struct sysmon_compute_prio_msg {
	unsigned int priority_idx;
};

struct sysmon_npu_activity_msg {
	unsigned int b_enabled;
};

struct sysmon_npu_corner_msg {
	unsigned int corner;
};

struct sysmon_thermal_msg {
	unsigned short hvx_level;
	unsigned short cdsp_level;
};

struct sysmon_camera_msg {
	unsigned int b_enabled;
};

struct sysmon_version_msg {
	unsigned int id;
};

struct sysmon_msg {
	unsigned int feature_id;
	union {
		struct sysmon_l3_msg l3_struct;
		struct sysmon_rm_msg rm_struct;
		struct sysmon_npu_limit_msg npu_limit;
		struct sysmon_npu_activity_msg npu_activity;
		struct sysmon_npu_corner_msg npu_corner;
		struct sysmon_version_msg version;
		unsigned int vtcm_rx_status;
#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
		unsigned int vtcm_test_rx_status;
#endif
		unsigned int crm_pdkill_status;
	} fs;
	unsigned int size;
};

struct sysmon_msg_v2 {
	unsigned int feature_id;
	unsigned int size;
	unsigned int version;
	unsigned int reserved1;
	unsigned int reserved2;
	union {
		struct sysmon_dcvs_clients dcvs_clients_votes;
	} fs;
};

struct crm_pdkill {
	unsigned int b_enable;
};

struct sysmon_msg_tx {
	unsigned int feature_id;
	union {
		struct sysmon_npu_limit_ack npu_limit_ack;
		struct sysmon_compute_prio_msg compute_prio;
		struct sysmon_npu_activity_msg npu_activity;
		struct sysmon_thermal_msg thermal;
		struct sysmon_npu_corner_msg npu_corner;
		struct sysmon_camera_msg camera;
	} fs;
	unsigned int size;
};

struct sysmon_msg_tx_v2 {
	unsigned int size;
	unsigned int cdsp_ver_info;
	unsigned int feature_id;
	union {
		struct vtcm_partition_interface vtcm_partition;
		struct crm_pdkill pdkill;
	} fs;
};

enum delay_state {
	CDSP_DELAY_THREAD_NOT_STARTED = 0,
	CDSP_DELAY_THREAD_STARTED = 1,
	CDSP_DELAY_THREAD_BEFORE_SLEEP = 2,
	CDSP_DELAY_THREAD_AFTER_SLEEP = 3,
	CDSP_DELAY_THREAD_EXITING = 4,
};

struct cdsprm_request {
	struct list_head node;
	struct sysmon_msg msg;
	bool busy;
};

struct cdsprm {
	unsigned int			cdsp_version;
	unsigned int			event;
	unsigned int			b_vtcm_partitioning;
#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
	unsigned int			b_vtcm_test_partitioning;
#endif
	unsigned int			b_vtcm_partition_en;
	unsigned int			b_resmgr_pdkill;
	unsigned int			b_resmgr_pdkill_override;
	unsigned int            b_resmgr_pdkill_status;
	struct completion		msg_avail;
	struct cdsprm_request		msg_queue[CDSPRM_MSG_QUEUE_DEPTH];
	struct vtcm_partition_interface		vtcm_partition;
	unsigned int			msg_queue_idx;
	struct task_struct		*cdsprm_wq_task;
	struct workqueue_struct		*delay_work_queue;
	struct work_struct		cdsprm_delay_work;
	struct mutex			rm_lock;
	spinlock_t			l3_lock;
	spinlock_t			list_lock;
	struct mutex			rpmsg_lock;
	struct rpmsg_device		*rpmsgdev;
	enum delay_state		dt_state;
	unsigned long long		timestamp;
	struct pm_qos_request		pm_qos_req;
	unsigned int			qos_latency_us;
	unsigned int			qos_max_ms;
	unsigned int			compute_prio_idx;
	struct mutex			npu_activity_lock;
	bool				b_cx_limit_en;
	unsigned int			b_npu_enabled;
	unsigned int			b_camera_enabled;
	unsigned int			b_npu_activity_waiting;
	unsigned int			b_npu_corner_waiting;
	struct completion		npu_activity_complete;
	struct completion		npu_corner_complete;
	unsigned int			npu_enable_cnt;
	enum cdsprm_npu_corner		npu_corner;
	enum cdsprm_npu_corner		allowed_npu_corner;
	enum cdsprm_npu_corner		npu_corner_limit;
	struct mutex			thermal_lock;
	unsigned int			thermal_cdsp_level;
	unsigned int			thermal_hvx_level;
	struct thermal_cooling_device	*cdsp_tcdev;
	struct thermal_cooling_device	*hvx_tcdev;
	bool				qos_request;
	bool				b_rpmsg_register;
	bool				b_qosinitdone;
	bool				b_applyingNpuLimit;
	bool				b_silver_en;
	int					latency_request;
	int					b_cdsprm_devinit;
	int					b_hvxrm_devinit;
	struct dentry			*debugfs_dir;
#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
	struct dentry			*debugfs_dir_debug;
	struct dentry			*debugfs_file_vtcm_test;
#endif
	struct dentry			*debugfs_file_priority;
	struct dentry			*debugfs_file_vtcm;
	struct dentry			*debugfs_resmgr_pdkill_override;
	struct dentry			*debugfs_resmgr_pdkill_state;
	struct dentry			*debugfs_dcvs_clients_info;
	int (*set_l3_freq)(unsigned int freq_khz);
	int (*set_l3_freq_cached)(unsigned int freq_khz);
	int (*set_corner_limit)(enum cdsprm_npu_corner);
	int (*set_corner_limit_cached)(enum cdsprm_npu_corner);
	u32 *coreno;
	u32 corecount;
	struct dev_pm_qos_request *dev_pm_qos_req;
	struct sysmon_dcvs_clients dcvs_clients_votes;
	struct completion dcvs_clients_votes_complete;
};

static struct cdsprm gcdsprm;
static LIST_HEAD(cdsprm_list);
static DECLARE_WAIT_QUEUE_HEAD(cdsprm_wq);

/**
 * cdsprm_register_cdspl3gov() - Register a method to set L3 clock
 *                               frequency
 * @arg: cdsprm_l3 structure with set L3 clock frequency method
 *
 * Note: To be called from cdspl3 governor only. Called when the governor is
 *       started.
 */
void cdsprm_register_cdspl3gov(struct cdsprm_l3 *arg)
{
	unsigned long flags;

	if (!arg)
		return;

	spin_lock_irqsave(&gcdsprm.l3_lock, flags);
	gcdsprm.set_l3_freq = arg->set_l3_freq;
	spin_unlock_irqrestore(&gcdsprm.l3_lock, flags);
}
EXPORT_SYMBOL(cdsprm_register_cdspl3gov);

int cdsprm_cxlimit_npu_limit_register(
	const struct cdsprm_npu_limit_cbs *npu_limit_cb)
{
	if (!npu_limit_cb)
		return -EINVAL;

	gcdsprm.set_corner_limit = npu_limit_cb->set_corner_limit;

	return 0;
}
EXPORT_SYMBOL(cdsprm_cxlimit_npu_limit_register);

int cdsprm_cxlimit_npu_limit_deregister(void)
{
	if (!gcdsprm.set_corner_limit)
		return -EINVAL;

	gcdsprm.set_corner_limit = NULL;

	return 0;
}
EXPORT_SYMBOL(cdsprm_cxlimit_npu_limit_deregister);

int cdsprm_compute_core_set_priority(unsigned int priority_idx)
{
	struct sysmon_msg_tx rpmsg_msg_tx;

	gcdsprm.compute_prio_idx = priority_idx;

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version) {
		rpmsg_msg_tx.feature_id =
			SYSMON_CDSP_FEATURE_COMPUTE_PRIO_TX;
		rpmsg_msg_tx.fs.compute_prio.priority_idx =
				priority_idx;
		rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
		rpmsg_send(gcdsprm.rpmsgdev->ept,
			&rpmsg_msg_tx,
			sizeof(rpmsg_msg_tx));
		pr_debug("Compute core priority set to %d\n",
			priority_idx);
	}

	return 0;
}
EXPORT_SYMBOL(cdsprm_compute_core_set_priority);

#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
int cdsprm_compute_vtcm_test(unsigned int vtcm_test_data)
{
	int result = -EINVAL;
	struct sysmon_msg_tx_v2 rpmsg_v2;

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version > 1) {
		rpmsg_v2.cdsp_ver_info = SYSMON_CDSP_GLINK_VERSION;
		rpmsg_v2.feature_id = SYSMON_CDSP_FEATURE_VTCM_CONFIG;
		rpmsg_v2.fs.vtcm_partition.command = VTCM_PARTITION_TEST;
		rpmsg_v2.size = sizeof(rpmsg_v2);
		result = rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_v2,
				sizeof(rpmsg_v2));
		gcdsprm.b_vtcm_test_partitioning =
		rpmsg_v2.fs.vtcm_partition.command;
		if (result)
			pr_info("VTCM partition test failed\n");
		else {
			pr_info("VTCM partition test command set to %d\n",
				rpmsg_v2.fs.vtcm_partition.command);
		}
	}

	return result;

}
#endif

int cdsprm_compute_vtcm_set_partition_map(unsigned int b_vtcm_partitioning)
{
	int i, j, result = -EINVAL;
	struct sysmon_msg_tx_v2 rpmsg_v2;

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version > 1) {

		rpmsg_v2.cdsp_ver_info = SYSMON_CDSP_GLINK_VERSION;
		rpmsg_v2.feature_id = SYSMON_CDSP_FEATURE_VTCM_CONFIG;
		rpmsg_v2.fs.vtcm_partition.command = b_vtcm_partitioning;
		rpmsg_v2.fs.vtcm_partition.num_partitions =
				gcdsprm.vtcm_partition.num_partitions;
		rpmsg_v2.fs.vtcm_partition.num_app_id_maps =
				gcdsprm.vtcm_partition.num_app_id_maps;

		for (i = 0; i < gcdsprm.vtcm_partition.num_partitions; i++) {
			rpmsg_v2.fs.vtcm_partition.partitions[i].partition_id
			= gcdsprm.vtcm_partition.partitions[i].partition_id;
			rpmsg_v2.fs.vtcm_partition.partitions[i].size =
			gcdsprm.vtcm_partition.partitions[i].size;
			rpmsg_v2.fs.vtcm_partition.partitions[i].flags =
			gcdsprm.vtcm_partition.partitions[i].flags;
		}

		for (j = 0; j < gcdsprm.vtcm_partition.num_app_id_maps; j++) {
			rpmsg_v2.fs.vtcm_partition.app2partition[j].app_id
			= gcdsprm.vtcm_partition.app2partition[j].app_id;
			rpmsg_v2.fs.vtcm_partition.app2partition[j].partition_id
			= gcdsprm.vtcm_partition.app2partition[j].partition_id;
		}

		rpmsg_v2.size = sizeof(rpmsg_v2);
		result = rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_v2,
				sizeof(rpmsg_v2));
		gcdsprm.b_vtcm_partitioning =
				rpmsg_v2.fs.vtcm_partition.command;

		if (result)
			pr_info("VTCM partition and map failed\n");
		else {
			pr_info("VTCM partition and map info set to %d\n",
					gcdsprm.b_vtcm_partitioning);
		}
	}

	return result;
}
EXPORT_SYMBOL(cdsprm_compute_vtcm_set_partition_map);

int cdsprm_resmgr_pdkill_config(unsigned int b_enable)
{
	int result = -EINVAL;
	struct sysmon_msg_tx_v2 rpmsg_v2;

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version > 2) {
		rpmsg_v2.cdsp_ver_info = SYSMON_CDSP_GLINK_VERSION;
		rpmsg_v2.feature_id = SYSMON_CDSP_FEATURE_CRM_PDKILL;
		rpmsg_v2.fs.pdkill.b_enable = b_enable;
		rpmsg_v2.size = sizeof(rpmsg_v2);
		result = rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_v2,
				sizeof(rpmsg_v2));

		if (result)
			pr_info("Sending resmgr pdkill config failed: %d\n", result);
		else {
			gcdsprm.b_resmgr_pdkill_override = b_enable;
			pr_info("resmgr pdkill config set to %d\n", b_enable);
		}
	}

	return result;
}

int cdsprm_cxlimit_npu_activity_notify(unsigned int b_enabled)
{
	int result = -EINVAL;
	struct sysmon_msg_tx rpmsg_msg_tx;

	if (!gcdsprm.b_cx_limit_en)
		return result;

	mutex_lock(&gcdsprm.npu_activity_lock);
	if (b_enabled)
		gcdsprm.npu_enable_cnt++;
	else if (gcdsprm.npu_enable_cnt)
		gcdsprm.npu_enable_cnt--;

	if ((gcdsprm.npu_enable_cnt &&
		gcdsprm.b_npu_enabled) ||
		(!gcdsprm.npu_enable_cnt &&
			!gcdsprm.b_npu_enabled)) {
		mutex_unlock(&gcdsprm.npu_activity_lock);
		return 0;
	}

	gcdsprm.b_npu_enabled = b_enabled;

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version) {
		if (gcdsprm.b_npu_enabled)
			gcdsprm.b_npu_activity_waiting++;
		rpmsg_msg_tx.feature_id =
			SYSMON_CDSP_FEATURE_NPU_ACTIVITY_TX;
		rpmsg_msg_tx.fs.npu_activity.b_enabled =
			gcdsprm.b_npu_enabled;
		rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
		result = rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_msg_tx,
				sizeof(rpmsg_msg_tx));
		if (gcdsprm.b_npu_enabled && result)
			gcdsprm.b_npu_activity_waiting--;
	}

	if (gcdsprm.b_npu_enabled && !result) {
		mutex_unlock(&gcdsprm.npu_activity_lock);
		wait_for_completion(&gcdsprm.npu_activity_complete);
		mutex_lock(&gcdsprm.npu_activity_lock);
		gcdsprm.b_npu_activity_waiting--;
	}

	mutex_unlock(&gcdsprm.npu_activity_lock);

	return result;
}
EXPORT_SYMBOL(cdsprm_cxlimit_npu_activity_notify);

enum cdsprm_npu_corner cdsprm_cxlimit_npu_corner_notify(
				enum cdsprm_npu_corner corner)
{
	int result = -EINVAL;
	enum cdsprm_npu_corner past_npu_corner;
	enum cdsprm_npu_corner return_npu_corner = corner;
	struct sysmon_msg_tx rpmsg_msg_tx;

	if (gcdsprm.b_applyingNpuLimit || !gcdsprm.b_cx_limit_en)
		return corner;

	mutex_lock(&gcdsprm.npu_activity_lock);
	past_npu_corner = gcdsprm.npu_corner;
	gcdsprm.npu_corner = corner;

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version) {
		if ((gcdsprm.npu_corner > past_npu_corner) ||
			!gcdsprm.npu_corner)
			gcdsprm.b_npu_corner_waiting++;
		rpmsg_msg_tx.feature_id =
			SYSMON_CDSP_FEATURE_NPU_CORNER_TX;
		rpmsg_msg_tx.fs.npu_corner.corner =
			(unsigned int)gcdsprm.npu_corner;
		rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
		result = rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_msg_tx,
				sizeof(rpmsg_msg_tx));
		if (((gcdsprm.npu_corner > past_npu_corner) ||
			!gcdsprm.npu_corner) && result)
			gcdsprm.b_npu_corner_waiting--;
	}

	if (((gcdsprm.npu_corner > past_npu_corner) ||
		!gcdsprm.npu_corner) && !result) {
		mutex_unlock(&gcdsprm.npu_activity_lock);
		wait_for_completion(&gcdsprm.npu_corner_complete);
		mutex_lock(&gcdsprm.npu_activity_lock);
		if (gcdsprm.allowed_npu_corner) {
			return_npu_corner = gcdsprm.allowed_npu_corner;
			gcdsprm.npu_corner = gcdsprm.allowed_npu_corner;
		}
		gcdsprm.b_npu_corner_waiting--;
	}

	mutex_unlock(&gcdsprm.npu_activity_lock);

	return return_npu_corner;
}
EXPORT_SYMBOL(cdsprm_cxlimit_npu_corner_notify);

int cdsprm_cxlimit_camera_activity_notify(unsigned int b_enabled)
{
	struct sysmon_msg_tx rpmsg_msg_tx;

	if (!gcdsprm.b_cx_limit_en)
		return -EINVAL;

	gcdsprm.b_camera_enabled = b_enabled;

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version) {
		rpmsg_msg_tx.feature_id =
			SYSMON_CDSP_FEATURE_CAMERA_ACTIVITY_TX;
		rpmsg_msg_tx.fs.camera.b_enabled =
				b_enabled;
		rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
		rpmsg_send(gcdsprm.rpmsgdev->ept,
			&rpmsg_msg_tx,
			sizeof(rpmsg_msg_tx));
	}

	return 0;
}
EXPORT_SYMBOL(cdsprm_cxlimit_camera_activity_notify);

static int cdsprm_thermal_cdsp_clk_limit(unsigned int level)
{
	int result = -EINVAL;
	struct sysmon_msg_tx rpmsg_msg_tx;

	mutex_lock(&gcdsprm.thermal_lock);

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version) {
		rpmsg_msg_tx.feature_id =
			SYSMON_CDSP_FEATURE_THERMAL_LIMIT_TX;
		rpmsg_msg_tx.fs.thermal.hvx_level =
			gcdsprm.thermal_hvx_level;
		rpmsg_msg_tx.fs.thermal.cdsp_level = level;
		rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
		result = rpmsg_send(gcdsprm.rpmsgdev->ept,
					&rpmsg_msg_tx,
					sizeof(rpmsg_msg_tx));
	}

	if (result == 0)
		gcdsprm.thermal_cdsp_level = level;

	mutex_unlock(&gcdsprm.thermal_lock);

	return result;
}

static int cdsprm_thermal_hvx_instruction_limit(unsigned int level)
{
	int result = -EINVAL;
	struct sysmon_msg_tx rpmsg_msg_tx;

	mutex_lock(&gcdsprm.thermal_lock);

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version) {
		rpmsg_msg_tx.feature_id =
			SYSMON_CDSP_FEATURE_THERMAL_LIMIT_TX;
		rpmsg_msg_tx.fs.thermal.hvx_level = level;
		rpmsg_msg_tx.fs.thermal.cdsp_level =
				gcdsprm.thermal_cdsp_level;
		rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
		result = rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_msg_tx,
				sizeof(rpmsg_msg_tx));
	}

	if (result == 0)
		gcdsprm.thermal_hvx_level = level;

	mutex_unlock(&gcdsprm.thermal_lock);

	return result;
}

/**
 * cdsprm_unregister_cdspl3gov() - Unregister the method to set L3 clock
 *                                 frequency
 *
 * Note: To be called from cdspl3 governor only. Called when the governor is
 *       stopped
 */
void cdsprm_unregister_cdspl3gov(void)
{
	unsigned long flags;

	spin_lock_irqsave(&gcdsprm.l3_lock, flags);
	gcdsprm.set_l3_freq = NULL;
	spin_unlock_irqrestore(&gcdsprm.l3_lock, flags);
}
EXPORT_SYMBOL(cdsprm_unregister_cdspl3gov);

static void qos_cores_init(struct device *dev)
{
	int i, err = 0;
	u32 *cpucores = NULL;

	of_find_property(dev->of_node,
					"qcom,qos-cores", &gcdsprm.corecount);

	if (gcdsprm.corecount) {
		gcdsprm.corecount /= sizeof(u32);

		cpucores = kcalloc(gcdsprm.corecount,
					sizeof(u32), GFP_KERNEL);

		if (cpucores == NULL) {
			dev_err(dev,
					"kcalloc failed for cpucores\n");
			gcdsprm.b_silver_en = false;
		} else {
			for (i = 0; i < gcdsprm.corecount; i++) {
				err = of_property_read_u32_index(dev->of_node,
							"qcom,qos-cores", i, &cpucores[i]);
				if (err) {
					dev_err(dev,
						"%s: failed to read QOS coree for core:%d\n",
							__func__, i);
					gcdsprm.b_silver_en = false;
				}
			}

			gcdsprm.coreno = cpucores;

			gcdsprm.dev_pm_qos_req = kcalloc(gcdsprm.corecount,
					sizeof(struct dev_pm_qos_request), GFP_KERNEL);

			if (gcdsprm.dev_pm_qos_req == NULL) {
				dev_err(dev,
						"kcalloc failed for dev_pm_qos_req\n");
				gcdsprm.b_silver_en = false;
			}
		}
	}
}

static void set_qos_latency(int latency)
{
	int err = 0;
	u32 ii = 0;
	int cpu;

	if (gcdsprm.b_silver_en) {

		for (ii = 0; ii < gcdsprm.corecount; ii++) {
			cpu = gcdsprm.coreno[ii];

			if (!gcdsprm.qos_request) {
				err = dev_pm_qos_add_request(
						get_cpu_device(cpu),
						&gcdsprm.dev_pm_qos_req[ii],
						DEV_PM_QOS_RESUME_LATENCY,
						latency);
			} else {
				err = dev_pm_qos_update_request(
						&gcdsprm.dev_pm_qos_req[ii],
						latency);
			}

			if (err < 0) {
				pr_err("%s: %s: PM voting cpu:%d fail,err %d,QoS update %d\n",
					current->comm, __func__, cpu,
					err, gcdsprm.qos_request);
				break;
			}
		}

		if (err >= 0)
			gcdsprm.qos_request = true;
	} else {
		if (!gcdsprm.qos_request) {
			cpu_latency_qos_add_request(&gcdsprm.pm_qos_req, latency);
			gcdsprm.qos_request = true;
		} else {
			cpu_latency_qos_update_request(&gcdsprm.pm_qos_req,
				latency);
		}
	}
}

static void process_rm_request(struct sysmon_msg *msg)
{
	struct sysmon_rm_msg *rm_msg;

	if (!msg)
		return;

	if (msg->feature_id == SYSMON_CDSP_FEATURE_RM_RX) {
		mutex_lock(&gcdsprm.rm_lock);
		rm_msg = &msg->fs.rm_struct;
		if (rm_msg->b_qos_flag ==
			SYSMON_CDSP_QOS_FLAG_ENABLE) {
			if (gcdsprm.latency_request !=
					gcdsprm.qos_latency_us) {
				set_qos_latency(gcdsprm.qos_latency_us);
				gcdsprm.latency_request =
					gcdsprm.qos_latency_us;
				pr_debug("Set qos latency to %d\n",
						gcdsprm.latency_request);
			}
			gcdsprm.timestamp = ((rm_msg->timetick_low) |
			 ((unsigned long long)rm_msg->timetick_high << 32));
			if (gcdsprm.dt_state >= CDSP_DELAY_THREAD_AFTER_SLEEP) {
				flush_workqueue(gcdsprm.delay_work_queue);
				if (gcdsprm.dt_state ==
						CDSP_DELAY_THREAD_EXITING) {
					gcdsprm.dt_state =
						CDSP_DELAY_THREAD_STARTED;
					queue_work(gcdsprm.delay_work_queue,
					  &gcdsprm.cdsprm_delay_work);
				}
			} else if (gcdsprm.dt_state ==
						CDSP_DELAY_THREAD_NOT_STARTED) {
				gcdsprm.dt_state = CDSP_DELAY_THREAD_STARTED;
				queue_work(gcdsprm.delay_work_queue,
					&gcdsprm.cdsprm_delay_work);
			}
		} else if ((rm_msg->b_qos_flag ==
					SYSMON_CDSP_QOS_FLAG_DISABLE) &&
				(gcdsprm.latency_request !=
					PM_QOS_RESUME_LATENCY_DEFAULT_VALUE)) {
			set_qos_latency(PM_QOS_RESUME_LATENCY_DEFAULT_VALUE);
			gcdsprm.latency_request = PM_QOS_RESUME_LATENCY_DEFAULT_VALUE;
			pr_debug("Set qos latency to %d\n",
					gcdsprm.latency_request);
		}
		mutex_unlock(&gcdsprm.rm_lock);
	} else {
		pr_err("Received incorrect msg on rm queue: %d\n",
				msg->feature_id);
	}
}

static void process_delayed_rm_request(struct work_struct *work)
{
	unsigned long long timestamp, curr_timestamp;
	unsigned int time_ms = 0;

	mutex_lock(&gcdsprm.rm_lock);

	timestamp = gcdsprm.timestamp;
	curr_timestamp = __arch_counter_get_cntvct();

	while ((gcdsprm.latency_request ==
					gcdsprm.qos_latency_us) &&
			(curr_timestamp < timestamp)) {
		if ((timestamp - curr_timestamp) <
		(gcdsprm.qos_max_ms * SYS_CLK_TICKS_PER_MS))
			time_ms = (timestamp - curr_timestamp) /
						SYS_CLK_TICKS_PER_MS;
		else
			break;
		gcdsprm.dt_state = CDSP_DELAY_THREAD_BEFORE_SLEEP;

		mutex_unlock(&gcdsprm.rm_lock);
		usleep_range(time_ms * 1000, (time_ms + 2) * 1000);
		mutex_lock(&gcdsprm.rm_lock);

		gcdsprm.dt_state = CDSP_DELAY_THREAD_AFTER_SLEEP;
		timestamp = gcdsprm.timestamp;
		curr_timestamp = __arch_counter_get_cntvct();
	}

	set_qos_latency(PM_QOS_RESUME_LATENCY_DEFAULT_VALUE);
	gcdsprm.latency_request = PM_QOS_RESUME_LATENCY_DEFAULT_VALUE;
	pr_debug("Set qos latency to %d\n", gcdsprm.latency_request);
	gcdsprm.dt_state = CDSP_DELAY_THREAD_EXITING;

	mutex_unlock(&gcdsprm.rm_lock);
}

static void cdsprm_rpmsg_send_details(void)
{
	struct sysmon_msg_tx rpmsg_msg_tx;

	if (!gcdsprm.cdsp_version)
		return;

	if (gcdsprm.cdsp_version > 1 && gcdsprm.b_vtcm_partition_en)
		cdsprm_compute_vtcm_set_partition_map(VTCM_PARTITION_SET);

	if (gcdsprm.b_cx_limit_en) {
		reinit_completion(&gcdsprm.npu_activity_complete);
		reinit_completion(&gcdsprm.npu_corner_complete);

		if (gcdsprm.npu_corner) {
			rpmsg_msg_tx.feature_id =
				SYSMON_CDSP_FEATURE_NPU_CORNER_TX;
			rpmsg_msg_tx.fs.npu_corner.corner =
				(unsigned int)gcdsprm.npu_corner;
			rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
			rpmsg_send(gcdsprm.rpmsgdev->ept,
					&rpmsg_msg_tx,
					sizeof(rpmsg_msg_tx));
		}

		if (gcdsprm.b_npu_enabled) {
			rpmsg_msg_tx.feature_id =
				SYSMON_CDSP_FEATURE_NPU_ACTIVITY_TX;
			rpmsg_msg_tx.fs.npu_activity.b_enabled =
				gcdsprm.b_npu_enabled;
			rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
			rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_msg_tx,
				sizeof(rpmsg_msg_tx));
		}

		cdsprm_compute_core_set_priority(gcdsprm.compute_prio_idx);

		if (gcdsprm.b_camera_enabled) {
			rpmsg_msg_tx.feature_id =
				SYSMON_CDSP_FEATURE_CAMERA_ACTIVITY_TX;
			rpmsg_msg_tx.fs.camera.b_enabled =
					gcdsprm.b_camera_enabled;
			rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
			rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_msg_tx,
				sizeof(rpmsg_msg_tx));
		}
	}

	if (gcdsprm.thermal_cdsp_level) {
		cdsprm_thermal_cdsp_clk_limit(
			gcdsprm.thermal_cdsp_level);
	} else if (gcdsprm.thermal_hvx_level) {
		cdsprm_thermal_hvx_instruction_limit(
			gcdsprm.thermal_hvx_level);
	}

	cdsprm_resmgr_pdkill_config(gcdsprm.b_resmgr_pdkill_override);
}

static struct cdsprm_request *get_next_request(void)
{
	struct cdsprm_request *req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gcdsprm.list_lock, flags);
	req = list_first_entry_or_null(&cdsprm_list,
				struct cdsprm_request, node);
	spin_unlock_irqrestore(&gcdsprm.list_lock,
					flags);

	return req;
}

static int process_cdsp_request_thread(void *data)
{
	struct cdsprm_request *req = NULL;
	struct sysmon_msg *msg = NULL;
	unsigned int l3_clock_khz;
	unsigned long flags;
	int result = 0;
	struct sysmon_msg_tx rpmsg_msg_tx;

	while (!kthread_should_stop()) {
		result = wait_event_interruptible(cdsprm_wq,
				(kthread_should_stop() ? true :
					(NULL != (req = get_next_request()))));

		if (kthread_should_stop())
			break;

		if (result)
			continue;

		if (!req)
			break;

		msg = &req->msg;

		if (msg && (msg->feature_id == SYSMON_CDSP_FEATURE_RM_RX) &&
			gcdsprm.b_qosinitdone) {
			process_rm_request(msg);
		} else if (msg && (msg->feature_id ==
			SYSMON_CDSP_FEATURE_L3_RX)) {
			l3_clock_khz = msg->fs.l3_struct.l3_clock_khz;

			spin_lock_irqsave(&gcdsprm.l3_lock, flags);
			gcdsprm.set_l3_freq_cached = gcdsprm.set_l3_freq;
			spin_unlock_irqrestore(&gcdsprm.l3_lock, flags);

			if (gcdsprm.set_l3_freq_cached) {
				gcdsprm.set_l3_freq_cached(l3_clock_khz);
				pr_debug("Set L3 clock %d done\n",
					l3_clock_khz);
			}
		} else if (msg && (msg->feature_id ==
				SYSMON_CDSP_FEATURE_NPU_LIMIT_RX)) {
			mutex_lock(&gcdsprm.npu_activity_lock);

			gcdsprm.set_corner_limit_cached =
						gcdsprm.set_corner_limit;

			if (gcdsprm.set_corner_limit_cached) {
				gcdsprm.npu_corner_limit =
					msg->fs.npu_limit.corner;
				gcdsprm.b_applyingNpuLimit = true;
				result = gcdsprm.set_corner_limit_cached(
						gcdsprm.npu_corner_limit);
				gcdsprm.b_applyingNpuLimit = false;
				pr_debug("Set NPU limit to %d\n",
					msg->fs.npu_limit.corner);
			} else {
				result = -ENOMSG;
				pr_debug("NPU limit not registered\n");
			}

			mutex_unlock(&gcdsprm.npu_activity_lock);
			/*
			 * Send Limit ack back to DSP
			 */
			rpmsg_msg_tx.feature_id =
				SYSMON_CDSP_FEATURE_NPU_LIMIT_TX;

			if (result == 0) {
				rpmsg_msg_tx.fs.npu_limit_ack.corner =
					msg->fs.npu_limit.corner;
			} else {
				rpmsg_msg_tx.fs.npu_limit_ack.corner =
						CDSPRM_NPU_CLK_OFF;
			}

			rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
			result = rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_msg_tx,
				sizeof(rpmsg_msg_tx));

			if (result)
				pr_err("rpmsg send failed %d\n", result);
			else
				pr_debug("NPU limit ack sent\n");
		} else if (msg && (msg->feature_id ==
				SYSMON_CDSP_FEATURE_VERSION_RX)) {
			cdsprm_rpmsg_send_details();
			pr_debug("Sent preserved data to DSP\n");
		}

		spin_lock_irqsave(&gcdsprm.list_lock, flags);
		list_del(&req->node);
		req->busy = false;
		spin_unlock_irqrestore(&gcdsprm.list_lock, flags);
	}

	return 0;
}

static int cdsprm_rpmsg_probe(struct rpmsg_device *dev)
{
	/* Populate child nodes as platform devices */
	of_platform_populate(dev->dev.of_node, NULL, NULL, &dev->dev);
	gcdsprm.rpmsgdev = dev;
	dev_dbg(&dev->dev, "rpmsg probe called for cdsp\n");

	return 0;
}

static void cdsprm_rpmsg_remove(struct rpmsg_device *dev)
{
	gcdsprm.rpmsgdev = NULL;
	gcdsprm.cdsp_version = 0;

	if (gcdsprm.b_cx_limit_en) {
		mutex_lock(&gcdsprm.npu_activity_lock);
		complete_all(&gcdsprm.npu_activity_complete);
		complete_all(&gcdsprm.npu_corner_complete);
		mutex_unlock(&gcdsprm.npu_activity_lock);

		gcdsprm.set_corner_limit_cached = gcdsprm.set_corner_limit;

		if ((gcdsprm.npu_corner_limit < CDSPRM_NPU_TURBO_L1) &&
			gcdsprm.set_corner_limit_cached)
			gcdsprm.set_corner_limit_cached(CDSPRM_NPU_TURBO_L1);
	}
}

static int cdsprm_rpmsg_callback(struct rpmsg_device *dev, void *data,
		int len, void *priv, u32 addr)
{
	struct sysmon_msg *msg = (struct sysmon_msg *)data;
	struct sysmon_msg_v2 *msg_v2 = (struct sysmon_msg_v2 *)data;
	bool b_valid = false;
	struct cdsprm_request *req;
	unsigned long flags;

	if (!data || (len < sizeof(*msg)) ||
	    ((len > sizeof(*msg) && msg_v2->size != len))) {
		dev_err(&dev->dev,
			"Invalid message in rpmsg callback, length: %d, expected: >= %lu\n",
			len, sizeof(*msg));
		return -EINVAL;
	}

	if ((msg->feature_id == SYSMON_CDSP_FEATURE_RM_RX) &&
			gcdsprm.b_qosinitdone) {
		dev_dbg(&dev->dev, "Processing RM request\n");
		b_valid = true;
	} else if (msg->feature_id == SYSMON_CDSP_FEATURE_L3_RX) {
		dev_dbg(&dev->dev, "Processing L3 request\n");
		spin_lock_irqsave(&gcdsprm.l3_lock, flags);
		gcdsprm.set_l3_freq_cached = gcdsprm.set_l3_freq;
		spin_unlock_irqrestore(&gcdsprm.l3_lock, flags);
		if (gcdsprm.set_l3_freq_cached)
			b_valid = true;
	} else if ((msg->feature_id == SYSMON_CDSP_FEATURE_NPU_CORNER_RX) &&
			(gcdsprm.b_cx_limit_en)) {
		gcdsprm.allowed_npu_corner = msg->fs.npu_corner.corner;
		dev_dbg(&dev->dev,
			"Processing NPU corner request ack for %d\n",
			gcdsprm.allowed_npu_corner);
		if (gcdsprm.b_npu_corner_waiting)
			complete(&gcdsprm.npu_corner_complete);
	} else if ((msg->feature_id == SYSMON_CDSP_FEATURE_NPU_LIMIT_RX) &&
			(gcdsprm.b_cx_limit_en)) {
		dev_dbg(&dev->dev, "Processing NPU limit request for %d\n",
			msg->fs.npu_limit.corner);
		b_valid = true;
	} else if ((msg->feature_id == SYSMON_CDSP_FEATURE_NPU_ACTIVITY_RX) &&
			(gcdsprm.b_cx_limit_en)) {
		dev_dbg(&dev->dev, "Processing NPU activity request ack\n");
		if (gcdsprm.b_npu_activity_waiting)
			complete(&gcdsprm.npu_activity_complete);
	} else if (msg->feature_id == SYSMON_CDSP_FEATURE_VERSION_RX) {
		gcdsprm.cdsp_version = msg->fs.version.id;
		b_valid = true;
		dev_dbg(&dev->dev, "Received CDSP version 0x%x\n",
			gcdsprm.cdsp_version);
	} else if (msg->feature_id == SYSMON_CDSP_VTCM_SEND_TX_STATUS) {
		gcdsprm.b_vtcm_partitioning = msg->fs.vtcm_rx_status;
		if (gcdsprm.b_vtcm_partitioning == 0)
			gcdsprm.b_vtcm_partitioning = VTCM_PARTITION_SET;
		else
			gcdsprm.b_vtcm_partitioning = VTCM_PARTITION_REMOVE;
		dev_dbg(&dev->dev, "Received CDSP VTCM Tx status %d\n",
			gcdsprm.b_vtcm_partitioning);
	}

#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
	else if (msg->feature_id == SYSMON_CDSP_VTCM_SEND_TEST_TX_STATUS) {
		gcdsprm.b_vtcm_test_partitioning = msg->fs.vtcm_test_rx_status;
		if (gcdsprm.b_vtcm_test_partitioning == 0)
			gcdsprm.b_vtcm_test_partitioning = VTCM_PARTITION_TEST;
		else
			gcdsprm.b_vtcm_test_partitioning =
				VTCM_PARTITION_REMOVE;
		dev_dbg(&dev->dev, "Received CDSP VTCM test enablement status %d\n",
			gcdsprm.b_vtcm_test_partitioning);
	}
#endif

	else if (msg->feature_id == SYSMON_CDSP_CRM_PDKILL_RX_STATUS) {
		if (!msg->fs.crm_pdkill_status)
			gcdsprm.b_resmgr_pdkill_status = gcdsprm.b_resmgr_pdkill_override;
		dev_dbg(&dev->dev,
				"Received ack for resmgr pdkill config (%d) with status (%d)\n",
				gcdsprm.b_resmgr_pdkill_override, msg->fs.crm_pdkill_status);
	} else if (msg->feature_id == SYSMON_CDSP_DCVS_CLIENTS_RX_STATUS) {
		gcdsprm.dcvs_clients_votes.status = 0;
		gcdsprm.dcvs_clients_votes = msg_v2->fs.dcvs_clients_votes;
		complete(&gcdsprm.dcvs_clients_votes_complete);
		dev_dbg(&dev->dev, "Received DCVS clients information");
	} else {
		dev_err(&dev->dev, "Received incorrect msg feature %d\n",
		msg->feature_id);
	}

	if (b_valid) {
		spin_lock_irqsave(&gcdsprm.list_lock, flags);

		if (!gcdsprm.msg_queue[gcdsprm.msg_queue_idx].busy) {
			req = &gcdsprm.msg_queue[gcdsprm.msg_queue_idx];
			req->busy = true;
			req->msg = *msg;
			if (gcdsprm.msg_queue_idx <
					(CDSPRM_MSG_QUEUE_DEPTH - 1))
				gcdsprm.msg_queue_idx++;
			else
				gcdsprm.msg_queue_idx = 0;
		} else {
			spin_unlock_irqrestore(&gcdsprm.list_lock, flags);
			dev_dbg(&dev->dev,
				"Unable to queue cdsp request, no memory\n");
			return -ENOMEM;
		}

		list_add_tail(&req->node, &cdsprm_list);
		spin_unlock_irqrestore(&gcdsprm.list_lock, flags);
		wake_up_interruptible(&cdsprm_wq);
	}

	return 0;
}

static int cdsp_get_max_state(struct thermal_cooling_device *cdev,
				unsigned long *state)
{
	*state = CDSP_THERMAL_MAX_STATE;

	return 0;
}

static int cdsp_get_cur_state(struct thermal_cooling_device *cdev,
				unsigned long *state)
{
	*state = gcdsprm.thermal_cdsp_level;

	return 0;
}

static int cdsp_set_cur_state(struct thermal_cooling_device *cdev,
				unsigned long state)
{
	if (gcdsprm.thermal_cdsp_level == state)
		return 0;

	cdsprm_thermal_cdsp_clk_limit(state);

	return 0;
}

static const struct thermal_cooling_device_ops cdsp_cooling_ops = {
	.get_max_state = cdsp_get_max_state,
	.get_cur_state = cdsp_get_cur_state,
	.set_cur_state = cdsp_set_cur_state,
};

static int hvx_get_max_state(struct thermal_cooling_device *cdev,
				unsigned long *state)
{
	*state = HVX_THERMAL_MAX_STATE;

	return 0;
}

static int hvx_get_cur_state(struct thermal_cooling_device *cdev,
				unsigned long *state)
{
	*state = gcdsprm.thermal_hvx_level;

	return 0;
}

static int hvx_set_cur_state(struct thermal_cooling_device *cdev,
				unsigned long state)
{
	if (gcdsprm.thermal_hvx_level == state)
		return 0;

	cdsprm_thermal_hvx_instruction_limit(state);

	return 0;
}

static int cdsprm_vtcm_partition_state_read(void *data, u64 *val)
{
	*val = gcdsprm.b_vtcm_partitioning;

	return 0;
}

static int cdsprm_vtcm_partition_state_write(void *data, u64 val)
{

	cdsprm_compute_vtcm_set_partition_map((unsigned int)val);

	if (gcdsprm.b_vtcm_partitioning != val)

		return -EINVAL;

	return 0;
}

static int cdsprm_compute_prio_read(void *data, u64 *val)
{
	*val = gcdsprm.compute_prio_idx;

	return 0;
}

static int cdsprm_compute_prio_write(void *data, u64 val)
{
	cdsprm_compute_core_set_priority((unsigned int)val);

	return 0;
}

#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
static int cdsprm_vtcm_test_state_read(void *data, u64 *val)
{
	*val = gcdsprm.b_vtcm_test_partitioning;

	return 0;
}

static int cdsprm_vtcm_test_state_write(void *data, u64 val)
{
	cdsprm_compute_vtcm_test((unsigned int)val);

	if (gcdsprm.b_vtcm_test_partitioning != VTCM_PARTITION_TEST)
		return -EINVAL;

	return 0;

}
#endif

DEFINE_DEBUGFS_ATTRIBUTE(cdsprm_debugfs_fops,
			cdsprm_compute_prio_read,
			cdsprm_compute_prio_write,
			"%llu\n");

DEFINE_DEBUGFS_ATTRIBUTE(cdsprmvtcm_debugfs_fops,
			cdsprm_vtcm_partition_state_read,
			cdsprm_vtcm_partition_state_write,
			"%llu\n");

#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
DEFINE_DEBUGFS_ATTRIBUTE(cdsprmvtcm_test_debugfs_fops,
			cdsprm_vtcm_test_state_read,
			cdsprm_vtcm_test_state_write,
			"%llu\n");
#endif

static int cdsprm_resmgr_pdkill_override_read(void *data, u64 *val)
{
	*val = gcdsprm.b_resmgr_pdkill_status;

	return 0;
}

static int cdsprm_resmgr_pdkill_override_write(void *data, u64 val)
{
	int result = cdsprm_resmgr_pdkill_config((unsigned int)val);

	pr_info("resmgr pdkill override %d sent to NSP, returned %d\n", val, result);

	return result;
}

DEFINE_DEBUGFS_ATTRIBUTE(cdsprm_resmgr_pdkill_debugfs_fops,
			cdsprm_resmgr_pdkill_override_read,
			cdsprm_resmgr_pdkill_override_write,
			"%llu\n");

static char *dcvs_vcorner(unsigned int i)
{
	switch (i) {
	case 0:
		return "DISABLE";
	case 1:
		return "SVS2";
	case 2:
		return "SVS";
	case 3:
		return "SVS_PLUS";
	case 4:
		return "NOM";
	case 5:
		return "NOM_PLUS";
	case 6:
		return "TURBO";
	case 7:
		return "TURBO_PLUS";
	case 8:
		return "TURBO_L2";
	case 9:
		return "TURBO_L3";
	case 10 ... 255:
		return "MAX";
	default:
		return "INVALID";
	}
}

char *dcvs_vcorner_ext(unsigned int i)
{
	switch (i) {
	case 0:
		return "DISABLE";
	case 1 ... 0x100:
		return "MIN";
	case 0x101 ... 0x134:
		return "LOW_SVS_D2";
	case 0x135 ... 0x138:
		return "LOW_SVS_D1";
	case 0x139 ... 0x140:
		return "LOW_SVS";
	case 0x141 ... 0x180:
		return "SVS";
	case 0x181 ... 0x1c0:
		return "SVS_L1";
	case 0x1C1 ... 0x200:
		return "NOM";
	case 0x201 ... 0x240:
		return "NOM_L1";
	case 0x241 ... 0x280:
		return "TUR";
	case 0x281 ... 0x2A0:
		return "TUR_L1";
	case 0x2A1 ... 0x2B0:
		return "TUR_L2";
	case 0x2B1 ... 0x2C0:
		return "TUR_L3";
	case 0x2C1 ... 0xFFFF:
		return "MAX";
	default:
		return "INVALID";
	}
}

static char *dcvs_policy(unsigned int i)
{
	switch (i) {
	case 1:
		return "ADSP_DCVS_POLICY_BALANCED";
	case 2:
		return "ADSP_DCVS_POLICY_POWER_SAVER";
	case 3:
		return "ADSP_DCVS_POLICY_POWER_SAVER_AGGRESSIVE";
	case 4:
		return "ADSP_DCVS_POLICY_PERFORMANCE";
	case 5:
		return "ADSP_DCVS_POLICY_DUTY_CYCLE";
	default:
		return "INVALID_DCVS_POLICY";
	}
}

static char *dcvs_client_policy(unsigned int i)
{
	switch (i) {
	case 1:
		return "ADSP_DCVS_ADJUST_UP_DOWN";
	case 2:
		return "DSP_DCVS_ADJUST_ONLY_UP";
	case 4:
		return "ADSP_DCVS_POWER_SAVER_MODE";
	case 8:
		return "ADSP_DCVS_POWER_SAVER_AGGRESSIVE_MODE";
	case 16:
		return "ADSP_DCVS_PERFORMANCE_MODE";
	case 32:
		return "ADSP_DCVS_DUTY_CYCLE_MODE";
	default:
		return "INVALID_DCVS_POLICY";
	}
}

enum {
	LPM_ENABLED = 0,
	LPM_DISABLED,
	STANDALONE_APCR_ENABLED,
	RPM_ASSISTED_APCR_ENABLED,
};

static char *lpm_state(unsigned int lpmState)
{
	switch (lpmState) {
	case LPM_DISABLED:
		return "ALL_LPM_DISABLED";
	case STANDALONE_APCR_ENABLED:
		return "STANDALONE_APCR_ENABLED";
	case RPM_ASSISTED_APCR_ENABLED:
		return "RPM_ASSISTED_APCR_ENABLED";
	default:
		return "UNKNOWN";
	}
}

/** Input: bitwise-OR of values from #MmpmClientClassType */
static void print_client_classes(unsigned int adsppmClientClass)
{
	char buf[120];
	int bitMask = 1;

	strscpy(buf, "Client Class: ", sizeof(buf));
	for (int i = 0; i < 5; i++) {
		if ((bitMask & adsppmClientClass) != 0) {
			switch (i) {
			case 0:
				strlcat(buf, "AUDIO ", sizeof(buf));
				break;
			case 1:
				strlcat(buf, "VOICE ", sizeof(buf));
				break;
			case 2:
				strlcat(buf, "COMPUTE ", sizeof(buf));
				break;
			case 3:
				strlcat(buf, "STREAMING_1_HVX ", sizeof(buf));
				break;
			case 4:
				strlcat(buf, "STREAMING_2_HVX ", sizeof(buf));
				break;
			default:
				strlcat(buf, "UNKNOWN ", sizeof(buf));
				break;
			}
		}

		bitMask = bitMask << 1;
	}

	if (!adsppmClientClass)
		strlcat(buf, "None", sizeof(buf));

	pr_info("%s\n", buf);
}

static void print_dcvs_clients_votes(void)
{
	struct sysmon_dcvs_client_info *dcvs_client_p = &gcdsprm.dcvs_clients_votes.table[0];
	struct sysmon_dcvs_client_agg_info *agg_info_p = &gcdsprm.dcvs_clients_votes.aggInfo;

	pr_info("Number of active clients %u\n", gcdsprm.dcvs_clients_votes.num_clients);

	if (gcdsprm.dcvs_clients_votes.num_clients) {
		pr_info("Aggregated States:\n");
		pr_info("    Core (Min, Max, Target) corner: (%s, %s, %s)\n",
				dcvs_vcorner(agg_info_p->core_min_corner),
				dcvs_vcorner(agg_info_p->core_max_corner),
				dcvs_vcorner(agg_info_p->core_target_corner));
		pr_info("    Core Perf Mode : %s\n",
				agg_info_p->core_perf_mode ? "LOW" : "HIGH");

		pr_info("    Bus (Min, Max, Target) corner: (%s, %s, %s)\n",
				dcvs_vcorner(agg_info_p->bus_min_corner),
				dcvs_vcorner(agg_info_p->bus_max_corner),
				dcvs_vcorner(agg_info_p->bus_target_corner));
		pr_info("    Bus Perf Mode : %s\n",
				agg_info_p->bus_perf_mode ? "LOW" : "HIGH");

		if (agg_info_p->dcvs_state)
			pr_info("    DCVS Participation: %s DCVS Policy : %s\n",
				agg_info_p->dcvs_state ? "ENABLED" : "DISABLED",
				dcvs_policy(agg_info_p->dcvs_policy));

		if (agg_info_p->core_clk_vote_mhz)
			pr_info("    Core Clk Vote : %u MHz\n",
				agg_info_p->core_clk_vote_mhz);

		if (agg_info_p->bus_clk_vote_mhz)
			pr_info("    Bus Clk Vote : %u MHz\n",
				agg_info_p->bus_clk_vote_mhz);

		pr_info("    HMX Power : %s\n",
				(agg_info_p->hmx_power) ? "ON" : "OFF");
		pr_info("    HMX (Min, Max, Target) Corner: (%s, %s, %s)\n",
				dcvs_vcorner_ext(agg_info_p->hmx_min_corner),
				dcvs_vcorner_ext(agg_info_p->hmx_max_corner),
				dcvs_vcorner_ext(agg_info_p->hmx_target_corner));

		pr_info("    HMX Perf Mode : %s\n",
				agg_info_p->hmx_perf_mode ? "LOW" : "HIGH");

		if (agg_info_p->hmx_freq_mhz)
			pr_info("    HMX Frequency : %u MHz\n",
					agg_info_p->hmx_freq_mhz);

		if (agg_info_p->hmx_floor_freq_mhz)
			pr_info("    HMX Floor Frequency : %u MHz\n",
					agg_info_p->hmx_floor_freq_mhz);

		if (agg_info_p->hmx_freq_from_q6_corner_mhz)
			pr_info("    HMX Frequency From Q6 : %u MHz\n",
					agg_info_p->hmx_freq_from_q6_corner_mhz);

		if (agg_info_p->hmx_clock_mhz)
			pr_info("    HMX Final Clock : %u MHz\n",
				agg_info_p->hmx_clock_mhz);

		pr_info("    CENG (Min, Max, Target) Corner: (%s, %s, %s)\n",
				dcvs_vcorner(agg_info_p->ceng_min_corner),
				dcvs_vcorner(agg_info_p->ceng_max_corner),
				dcvs_vcorner(agg_info_p->ceng_target_corner));

		pr_info("    CENG Perf Mode : %s\n",
				agg_info_p->ceng_perf_mode ? "LOW" : "HIGH");

		if (agg_info_p->ceng_ib)
			pr_info("    CENG Instantaneous  BW : %llu bytes per second\n",
					agg_info_p->ceng_ib);

		if (agg_info_p->ceng_ab)
			pr_info("    CENG Average BW : %u bytes per second\n",
					agg_info_p->ceng_ab);


		if (agg_info_p->ceng_clock_mhz)
			pr_info("    Q6->CENG Bus Final Clock : %u MHz\n",
				agg_info_p->ceng_clock_mhz);

		if (agg_info_p->sleep_latency)
			pr_info("    DCVS Aggregated Sleep Latency : %u us\n",
				agg_info_p->sleep_latency);

		if ((agg_info_p->sleep_latency_override))
			pr_info("    Minimum Sleep Latency (override from user config) : %u us\n",
				agg_info_p->sleep_latency_override);

		if (agg_info_p->final_sleep_latency_vote)
			pr_info("    Final Aggregated Sleep Latency : %u us\n",
				agg_info_p->final_sleep_latency_vote);

		if (agg_info_p->lpm_state)
			pr_info("    LPM state :%s\n",
				lpm_state(agg_info_p->lpm_state));

	}

	for (int i = 0; (i < gcdsprm.dcvs_clients_votes.num_clients) &&
					(i < DCVS_CLIENTS_TABLE_SIZE);
		 i++) {
		pr_info("Client ID : %u\n", dcvs_client_p[i].client_id);
		pr_info("    TID : %u\n", dcvs_client_p[i].tid);
		pr_info("    PID : %u\n", dcvs_client_p[i].pid);

		if (dcvs_client_p[i].libname[0])
			pr_info("    Lib Name : %s\n", dcvs_client_p[i].libname);

		if (dcvs_client_p[i].dcvs_enable)
			pr_info("    DCVS Participation: %s DCVS Policy: %s\n",
				dcvs_client_p[i].dcvs_enable ? "ENABLED" : "DISABLED",
				dcvs_client_policy(
					dcvs_client_p[i].dcvs_policy));

		if (dcvs_client_p[i].set_latency &&
		    dcvs_client_p[i].latency != 65535)
			pr_info("    Sleep Latency Vote : %u us\n", dcvs_client_p[i].latency);

		if (dcvs_client_p[i].client_class)
			print_client_classes(dcvs_client_p[i].client_class);

		if (dcvs_client_p[i].set_core_params) {
			pr_info("    Core (Min, Max, Target) Corner: (%s, %s, %s)\n",
				dcvs_vcorner(dcvs_client_p[i].core_min_corner),
				dcvs_vcorner(dcvs_client_p[i].core_max_corner),
				dcvs_vcorner(dcvs_client_p[i].core_target_corner));
			pr_info("    Core Perf Mode : %s\n",
					dcvs_client_p[i].core_perf_mode ? "LOW" : "HIGH");
		}

		if (dcvs_client_p[i].set_bus_params) {
			pr_info("    Bus (Min, Max, Target) Corner: (%s, %s, %s)\n",
				dcvs_vcorner(dcvs_client_p[i].bus_min_corner),
				dcvs_vcorner(dcvs_client_p[i].bus_max_corner),
				dcvs_vcorner(dcvs_client_p[i].bus_target_corner));
			pr_info("    Bus Perf Mode : %s\n",
					dcvs_client_p[i].bus_perf_mode ? "LOW" : "HIGH");
		}

		if (dcvs_client_p[i].mips_set_mips &&
		    (dcvs_client_p[i].mips_mipsPerThread ||
		     dcvs_client_p[i].mips_mipsTotal))
			pr_info("    MIPS Vote Per Thread: %u MIPS Vote Total: %u\n",
					dcvs_client_p[i].mips_mipsPerThread,
					dcvs_client_p[i].mips_mipsTotal);

		if (dcvs_client_p[i].mips_set_bus_bw &&
			dcvs_client_p[i].mips_bwBytePerSec)
			pr_info("    BW Vote : %llu bytes per second, BW Usage Percentage: %u\n",
					dcvs_client_p[i].mips_bwBytePerSec,
					dcvs_client_p[i].mips_busbwUsagePercentage);

		if (dcvs_client_p[i].mips_latency != 65535 &&
			dcvs_client_p[i].mips_set_latency)
			pr_info("    Legacy Sleep Latency Vote : %u us\n",
					dcvs_client_p[i].mips_latency);

		if (dcvs_client_p[i].sleep_disable &&
			dcvs_client_p[i].set_sleep_disable)
			pr_info("    LPM State: %s\n",
					lpm_state(dcvs_client_p[i].sleep_disable));

		pr_info("    HMX Power : %s\n",
				dcvs_client_p[i].hmx_params.power_up ? "ON" : "OFF");

		if (dcvs_client_p[i].hmx_params.set_clock) {
			pr_info("    HMX (Min, Max, Target) Corner: (%s, %s, %s)\n",
					dcvs_vcorner_ext(
						dcvs_client_p[i].hmx_params.min_corner),
					dcvs_vcorner_ext(
						dcvs_client_p[i].hmx_params.max_corner),
					dcvs_vcorner_ext(
						dcvs_client_p[i].hmx_params.target_corner));

			pr_info("    HMX Perf Mode : %s\n",
					dcvs_client_p[i].hmx_params.perf_mode ? "LOW" : "HIGH");

			pr_info("    HMX Default Frequency : %s\n",
					dcvs_client_p[i].hmx_params.pick_default
					? "TRUE"
					: "FALSE");

			if (dcvs_client_p[i].hmx_params.freq_mhz)
				pr_info("    HMX Frequency Requested : %u MHz\n",
						dcvs_client_p[i].hmx_params.freq_mhz);

			if (dcvs_client_p[i].hmx_params.floor_freq_mhz)
				pr_info("    HMX Floor Frequency Requested : %u MHz\n",
						dcvs_client_p[i].hmx_params.floor_freq_mhz);

			if (dcvs_client_p[i].hmx_params.freq_mhz_from_q6_corner)
				pr_info("    HMX Frequency From Q6 : %u MHz\n",
				dcvs_client_p[i].hmx_params.freq_mhz_from_q6_corner);
		}

		pr_info("    Q6->CENG (Min, Max, Target) Corner: (%s, %s, %s)\n",
				dcvs_vcorner(dcvs_client_p[i].ceng_params.min_corner),
				dcvs_vcorner(dcvs_client_p[i].ceng_params.max_corner),
				dcvs_vcorner(dcvs_client_p[i].ceng_params.target_corner));

		pr_info("    Q6->CENG Perf Mode : %s\n",
				dcvs_client_p[i].ceng_params.perf_mode ? "LOW" : "HIGH");

		if (dcvs_client_p[i].ceng_params.bwBytePerSec)
			pr_info("    Q6->CENG BW : %u bytes per second, Q6->CENG BW Usage Percentage : %u\n",
					dcvs_client_p[i].ceng_params.bwBytePerSec,
					dcvs_client_p[i].ceng_params.busbwUsagePercentage);

		pr_info("    Client Suspected to Prevent CDSP Sleep Issue: %s\n",
				(dcvs_client_p[i].set_latency &&
				 (dcvs_client_p[i].latency > 0) &&
				 (dcvs_client_p[i].latency <
				  DCVS_CLIENTS_VOTES_SLEEP_LATENCY_THRESHOLD_US) &&
				 dcvs_client_p[i].sleep_disable &&
				 ((dcvs_client_p[i].set_sleep_disable == STANDALONE_APCR_ENABLED) ||
				  (dcvs_client_p[i].set_sleep_disable == LPM_DISABLED)))
					? "YES"
					: "NO");

	}
}

static int cdsprm_get_dcvs_clients_votes(void)
{
	int result = -EINVAL;
	struct sysmon_msg_tx_v2 rpmsg;

	if (gcdsprm.rpmsgdev && gcdsprm.cdsp_version >= 5) {
		rpmsg.cdsp_ver_info = SYSMON_CDSP_GLINK_VERSION;
		rpmsg.feature_id = SYSMON_CDSP_FEATURE_GET_DCVS_CLIENTS;
		rpmsg.size = sizeof(rpmsg);
		result = rpmsg_send(gcdsprm.rpmsgdev->ept, &rpmsg,
				    sizeof(rpmsg));
		if (result) {
			pr_err("cdsprm_sysmon_dcvs_clients_votes failed. Error : %u\n", result);
			result = DCVS_CLIENTS_VOTES_RPMSG_SEND_FAILED;
		}
	} else {
		result = DCVS_CLIENTS_VOTES_UNSUPPORTED_API;
		pr_err("cdsprm_sysmon_dcvs_clients_votes not supported. Error: %d\n", result);
	}

	return result;
}

int cdsprm_sysmon_dcvs_clients_votes(struct sysmon_dcvs_clients *dcvs_clients_info, bool enable_log)
{
	int result = -EINVAL;

	if (!dcvs_clients_info && !enable_log) {
		result = DCVS_CLIETNS_VOTES_INVALID_PARAMS;
		pr_err("Parameters validation failed.\n");
		return result;
	}

	result = cdsprm_get_dcvs_clients_votes();

	if (!result) {

		if (!wait_for_completion_timeout(
				&gcdsprm.dcvs_clients_votes_complete,
				WAIT_FOR_DCVS_CLIENTDATA_TIMEOUT)) {
			pr_err("timeout waiting for completion\n");
			result = DCVS_CLIENTS_VOTES_TIMEOUT;

			return result;
		}

		if (gcdsprm.dcvs_clients_votes.status == DCVS_CLIENTS_VOTES_FETCH_SUCCESS) {
			if (enable_log)
				print_dcvs_clients_votes();
		} else {
			pr_err("Failed to get DCVS clients information\n");
			result = DCVS_CLIENTS_VOTES_FAILED;
		}

		if (dcvs_clients_info)
			memcpy(dcvs_clients_info, &gcdsprm.dcvs_clients_votes,
				   sizeof(struct sysmon_dcvs_clients));

	}

	return result;
}
EXPORT_SYMBOL(cdsprm_sysmon_dcvs_clients_votes);

static int dcvs_clients_show(struct seq_file *s, void *d)
{
	int result = cdsprm_sysmon_dcvs_clients_votes(NULL, true);

	if (!result)
		seq_puts(s, "Check kernel log for cdsprm_sysmon_dcvs_clients_votes() output\n");
	else
		seq_printf(s, "cdsprm_sysmon_dcvs_clients_votes() failed. Error : %d\n", result);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dcvs_clients);

static int cdsprm_resmgr_pdkill_state_show(struct seq_file *s, void *d)
{
	if (gcdsprm.b_resmgr_pdkill_status == 0) {
		if (gcdsprm.b_resmgr_pdkill == 0)
			seq_puts(s, "\nresmgr pdkill feature is disabled\n");
		else
			seq_puts(s,
				"\nresmgr pdkill feature is disabled by debugfs override\n");
	} else {
		if (gcdsprm.b_resmgr_pdkill)
			seq_puts(s, "\nresmgr pdkill feature is enabled\n");
		else
			seq_puts(s,
				"\nresmgr pdkill feature is enabled by debugfs override\n");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(cdsprm_resmgr_pdkill_state);

static const struct thermal_cooling_device_ops hvx_cooling_ops = {
	.get_max_state = hvx_get_max_state,
	.get_cur_state = hvx_get_cur_state,
	.set_cur_state = hvx_set_cur_state,
};

static void pd_kill_rpmsg(struct device *dev)
{
	gcdsprm.b_resmgr_pdkill = of_property_read_bool(dev->of_node,
				"qcom,resmgr-pdkill-enable");
	gcdsprm.b_resmgr_pdkill_override = gcdsprm.b_resmgr_pdkill;

	if (gcdsprm.debugfs_dir) {
		gcdsprm.debugfs_resmgr_pdkill_override =
				debugfs_create_file("resmgr_pdkill_override_state",
				0644, gcdsprm.debugfs_dir, NULL,
				&cdsprm_resmgr_pdkill_debugfs_fops);

		if (!gcdsprm.debugfs_resmgr_pdkill_override)
			dev_err(dev, "Failed to create resmgr debugfs file\n");

		gcdsprm.debugfs_resmgr_pdkill_state =
				debugfs_create_file("resmgr_pdkill_state",
				0444, gcdsprm.debugfs_dir, NULL,
				&cdsprm_resmgr_pdkill_state_fops);

		if (!gcdsprm.debugfs_resmgr_pdkill_state)
			dev_err(dev, "Failed to create resmgr debugfs file\n");

		gcdsprm.debugfs_dcvs_clients_info =
				debugfs_create_file("dcvs_clients",
				0444, gcdsprm.debugfs_dir, NULL,
				&dcvs_clients_fops);

		if (!gcdsprm.debugfs_dcvs_clients_info)
			dev_err(dev, "Failed to create dcvs_clients_show file\n");

	}
}

static int cdsp_rm_driver_probe(struct platform_device *pdev)
{
	int n, m, i, ret;
	u32 p;
	struct device *dev = &pdev->dev;
	struct thermal_cooling_device *tcdev = 0;
	unsigned int cooling_cells = 0;

	gcdsprm.b_silver_en = of_property_read_bool(dev->of_node,
					"qcom,qos-cores");

	if (gcdsprm.b_silver_en)
		qos_cores_init(dev);

	if (of_property_read_u32(dev->of_node,
			"qcom,qos-latency-us", &gcdsprm.qos_latency_us)) {
		return -EINVAL;
	}

	if (of_property_read_u32(dev->of_node,
			"qcom,qos-maxhold-ms", &gcdsprm.qos_max_ms)) {
		return -EINVAL;
	}

	gcdsprm.debugfs_dir = debugfs_create_dir("compute", NULL);

	if (!gcdsprm.debugfs_dir) {
		dev_err(dev,
		"Failed to create debugfs directory for cdsprm\n");
	}

	gcdsprm.compute_prio_idx = CDSPRM_COMPUTE_AIX_OVER_HVX;
	of_property_read_u32(dev->of_node,
				"qcom,compute-priority-mode",
				&gcdsprm.compute_prio_idx);

	gcdsprm.b_cx_limit_en = of_property_read_bool(dev->of_node,
				"qcom,compute-cx-limit-en");

	if (gcdsprm.b_cx_limit_en) {
		if (gcdsprm.debugfs_dir) {
			gcdsprm.debugfs_file_priority =
						debugfs_create_file("priority",
						0644, gcdsprm.debugfs_dir,
						NULL, &cdsprm_debugfs_fops);
			if (!gcdsprm.debugfs_file_priority) {
				debugfs_remove_recursive(gcdsprm.debugfs_dir);
				dev_err(dev,
					"Failed to create debugfs file\n");
			}
		}
	}

	of_property_read_u32(dev->of_node,
				"#cooling-cells",
				&cooling_cells);

	if (cooling_cells && IS_ENABLED(CONFIG_THERMAL)) {
		tcdev = thermal_of_cooling_device_register(dev->of_node,
							"cdsp", NULL,
							&cdsp_cooling_ops);

		if (IS_ERR(tcdev)) {
			dev_err(dev,
				"CDSP thermal driver reg failed\n");
		}

		gcdsprm.cdsp_tcdev = tcdev;
	}

	gcdsprm.b_qosinitdone = true;

#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
	gcdsprm.debugfs_dir_debug
			 = debugfs_create_dir("vtcm_test", NULL);

		if (!gcdsprm.debugfs_dir_debug) {
			dev_err(dev,
			"Failed to find debugfs directory for cdsprm\n");
		} else {
			gcdsprm.debugfs_file_vtcm_test =
				debugfs_create_file("vtcm_partition_test",
				0644, gcdsprm.debugfs_dir_debug,
				NULL, &cdsprmvtcm_test_debugfs_fops);
		}
		if (!gcdsprm.debugfs_file_vtcm_test) {
			debugfs_remove_recursive(gcdsprm.debugfs_dir_debug);
					dev_err(dev,
					"Failed to create debugfs file\n");
		}
#endif

	gcdsprm.b_vtcm_partition_en = of_property_read_bool(dev->of_node,
				"qcom,vtcm-partition-info");

	if (gcdsprm.b_vtcm_partition_en) {
		n = of_property_count_u32_elems(dev->of_node,
			"qcom,vtcm-partition-info");

		gcdsprm.vtcm_partition.num_partitions = (n / 3);

		if ((n % 3) != 0) {
			dev_err(dev,
			"Maps are expected to have %d elements each entry\n",
			 n/3);
			return -EINVAL;
		}

		for (i = 0; i < gcdsprm.vtcm_partition.num_partitions; i++) {
			ret = of_property_read_u32_index(dev->of_node,
				"qcom,vtcm-partition-info", i * 3, &p);

			if (ret) {
				dev_err(dev,
				"Error reading partition element %d :%d\n",
				 i, p);
				return ret;
			}

			gcdsprm.vtcm_partition.partitions[i].partition_id
			 = (unsigned char)p;

			if (gcdsprm.vtcm_partition.partitions[i].partition_id
			 >= (n/3)) {
				dev_err(dev,
				"partition id is invalid: %d\n", n/3);
				return -EINVAL;
			}

			ret = of_property_read_u32_index(dev->of_node,
				"qcom,vtcm-partition-info", i * 3 + 1, &p);

			if (ret) {
				dev_err(dev,
						"Error reading partition element %d :%d\n"
						, i, p);
				return ret;
			}

			gcdsprm.vtcm_partition.partitions[i].size
			 = (unsigned int)p;

			ret = of_property_read_u32_index(dev->of_node,
				"qcom,vtcm-partition-info", i * 3 + 2, &p);

			if (ret) {
				dev_err(dev,
						"Error reading partition element %d :%d\n",
						 i, p);
				return ret;
			}

			gcdsprm.vtcm_partition.partitions[i].flags
			= (unsigned int)p;
		}

		m = of_property_count_u32_elems(dev->of_node,
			"qcom,vtcm-partition-map");

		gcdsprm.vtcm_partition.num_app_id_maps = (m / 2);

		if ((m % 2) != 0) {
			dev_err(dev,
			"Maps to have %d elements each entry\n", m / 2);
			return -EINVAL;
		}

		for (i = 0; i < gcdsprm.vtcm_partition.num_app_id_maps; i++) {
			ret = of_property_read_u32_index(dev->of_node,
				"qcom,vtcm-partition-map", i * 2, &p);

			if (ret) {
				dev_err(dev,
				"Error reading map element %d :%d\n", i, p);
				return ret;
			}

			gcdsprm.vtcm_partition.app2partition[i].app_id
			 = (unsigned char)p;

			ret = of_property_read_u32_index(dev->of_node,
				"qcom,vtcm-partition-map", i * 2 + 1, &p);

			if (ret) {
				dev_err(dev,
				"Error reading map element %d :%d\n", i, p);
				return ret;
			}

			gcdsprm.vtcm_partition.app2partition[i].partition_id
			 = (unsigned char)p;
		}

		if (!gcdsprm.debugfs_dir)
			gcdsprm.debugfs_dir
			 = debugfs_create_dir("compute", NULL);

		if (!gcdsprm.debugfs_dir)
			dev_err(dev,
			"Failed to find debugfs directory for cdsprm\n");
		else {
			gcdsprm.debugfs_file_vtcm =
				debugfs_create_file("vtcm_partition_state",
				0644, gcdsprm.debugfs_dir,
				NULL, &cdsprmvtcm_debugfs_fops);
		}

		if (!gcdsprm.debugfs_file_vtcm
			&& !gcdsprm.debugfs_file_priority) {
			debugfs_remove_recursive(gcdsprm.debugfs_dir);
					dev_err(dev,
					"Failed to create debugfs file\n");
		}
	}

	pd_kill_rpmsg(dev);

	dev_dbg(dev, "CDSP request manager driver probe called\n");

	return 0;
}

static int hvx_rm_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct thermal_cooling_device *tcdev = 0;
	unsigned int cooling_cells = 0;

	of_property_read_u32(dev->of_node,
				"#cooling-cells",
				&cooling_cells);

	if (cooling_cells && IS_ENABLED(CONFIG_THERMAL)) {
		tcdev = thermal_of_cooling_device_register(dev->of_node,
							"hvx", NULL,
							&hvx_cooling_ops);
		if (IS_ERR(tcdev)) {
			dev_err(dev,
				"HVX thermal driver reg failed\n");
		}
		gcdsprm.hvx_tcdev = tcdev;
	}

	dev_dbg(dev, "HVX request manager driver probe called\n");

	return 0;
}

static const struct rpmsg_device_id cdsprm_rpmsg_match[] = {
	{ "cdsprmglink-apps-dsp" },
	{ },
};

static const struct of_device_id cdsprm_rpmsg_of_match[] = {
	{ .compatible = "qcom,msm-cdsprm-rpmsg" },
	{ },
};
MODULE_DEVICE_TABLE(of, cdsprm_rpmsg_of_match);

static struct rpmsg_driver cdsprm_rpmsg_client = {
	.id_table = cdsprm_rpmsg_match,
	.probe = cdsprm_rpmsg_probe,
	.remove = cdsprm_rpmsg_remove,
	.callback = cdsprm_rpmsg_callback,
	.drv = {
		.name = "qcom,msm_cdsprm_rpmsg",
		.of_match_table = cdsprm_rpmsg_of_match,
	},
};

static const struct of_device_id cdsp_rm_match_table[] = {
	{ .compatible = "qcom,msm-cdsp-rm" },
	{ },
};

static struct platform_driver cdsp_rm = {
	.probe = cdsp_rm_driver_probe,
	.driver = {
		.name = "msm_cdsp_rm",
		.of_match_table = cdsp_rm_match_table,
	},
};

static const struct of_device_id hvx_rm_match_table[] = {
	{ .compatible = "qcom,msm-hvx-rm" },
	{ },
};

static struct platform_driver hvx_rm = {
	.probe = hvx_rm_driver_probe,
	.driver = {
		.name = "msm_hvx_rm",
		.of_match_table = hvx_rm_match_table,
	},
};

static int __init cdsprm_init(void)
{
	int err;

	mutex_init(&gcdsprm.rm_lock);
	mutex_init(&gcdsprm.rpmsg_lock);
	mutex_init(&gcdsprm.npu_activity_lock);
	mutex_init(&gcdsprm.thermal_lock);
	spin_lock_init(&gcdsprm.l3_lock);
	spin_lock_init(&gcdsprm.list_lock);
	init_completion(&gcdsprm.msg_avail);
	init_completion(&gcdsprm.npu_activity_complete);
	init_completion(&gcdsprm.npu_corner_complete);

	gcdsprm.cdsprm_wq_task = kthread_run(process_cdsp_request_thread,
					NULL, "cdsprm-wq");

	if (IS_ERR(gcdsprm.cdsprm_wq_task)) {
		pr_err("Failed to create kernel thread\n");
		return -ENOMEM;
	}

	gcdsprm.delay_work_queue =
			create_singlethread_workqueue("cdsprm-wq-delay");

	if (!gcdsprm.delay_work_queue) {
		err = -ENOMEM;
		pr_err("Failed to create rm delay work queue\n");
		goto err_wq;
	}

	INIT_WORK(&gcdsprm.cdsprm_delay_work, process_delayed_rm_request);
	err = platform_driver_register(&cdsp_rm);

	if (err) {
		pr_err("Failed to register cdsprm platform driver: %d\n",
				err);
		goto bail;
	}

	gcdsprm.b_cdsprm_devinit = true;

	init_completion(&gcdsprm.dcvs_clients_votes_complete);

	err = platform_driver_register(&hvx_rm);

	if (err) {
		pr_err("Failed to register hvxrm platform driver: %d\n",
				err);
		goto bail;
	}

	gcdsprm.b_hvxrm_devinit = true;

	err = register_rpmsg_driver(&cdsprm_rpmsg_client);

	if (err) {
		pr_err("Failed registering rpmsg driver with return %d\n",
				err);
		goto bail;
	}

	gcdsprm.b_rpmsg_register = true;

	pr_info("Init successful\n");

	return 0;
bail:
	destroy_workqueue(gcdsprm.delay_work_queue);
err_wq:
	kthread_stop(gcdsprm.cdsprm_wq_task);

	return err;
}

static void __exit cdsprm_exit(void)
{
	struct sysmon_msg_tx rpmsg_msg_tx;

	pr_info("Exit module called\n");
	if (gcdsprm.qos_request) {
		set_qos_latency(PM_QOS_RESUME_LATENCY_DEFAULT_VALUE);
		gcdsprm.latency_request = PM_QOS_RESUME_LATENCY_DEFAULT_VALUE;
	}

	if (gcdsprm.hvx_tcdev)
		thermal_cooling_device_unregister(gcdsprm.hvx_tcdev);

	if (gcdsprm.cdsp_tcdev)
		thermal_cooling_device_unregister(gcdsprm.cdsp_tcdev);

	if (gcdsprm.b_cx_limit_en) {
		mutex_lock(&gcdsprm.npu_activity_lock);
		complete_all(&gcdsprm.npu_activity_complete);
		complete_all(&gcdsprm.npu_corner_complete);
		mutex_unlock(&gcdsprm.npu_activity_lock);

		gcdsprm.set_corner_limit_cached = gcdsprm.set_corner_limit;

		if ((gcdsprm.npu_corner_limit < CDSPRM_NPU_TURBO_L1) &&
			gcdsprm.set_corner_limit_cached)
			gcdsprm.set_corner_limit_cached(CDSPRM_NPU_TURBO_L1);
	}

	if (gcdsprm.cdsp_version && gcdsprm.b_cx_limit_en) {

		if (gcdsprm.b_npu_enabled) {
			rpmsg_msg_tx.feature_id =
				SYSMON_CDSP_FEATURE_NPU_ACTIVITY_TX;
			rpmsg_msg_tx.fs.npu_activity.b_enabled = 0;
			rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
			rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_msg_tx,
				sizeof(rpmsg_msg_tx));
			gcdsprm.b_npu_enabled = 0;
		}

		if (gcdsprm.npu_corner) {
			rpmsg_msg_tx.feature_id =
				SYSMON_CDSP_FEATURE_NPU_CORNER_TX;
			rpmsg_msg_tx.fs.npu_corner.corner = 0;
			rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
			rpmsg_send(gcdsprm.rpmsgdev->ept,
					&rpmsg_msg_tx,
					sizeof(rpmsg_msg_tx));
			gcdsprm.npu_corner = 0;
		}

		if (gcdsprm.b_camera_enabled) {
			rpmsg_msg_tx.feature_id =
				SYSMON_CDSP_FEATURE_CAMERA_ACTIVITY_TX;
			rpmsg_msg_tx.fs.camera.b_enabled = 0;
			rpmsg_msg_tx.size = sizeof(rpmsg_msg_tx);
			rpmsg_send(gcdsprm.rpmsgdev->ept,
				&rpmsg_msg_tx,
				sizeof(rpmsg_msg_tx));
			gcdsprm.b_camera_enabled = 0;
		}
	}

	complete_all(&gcdsprm.dcvs_clients_votes_complete);

	if (gcdsprm.thermal_cdsp_level) {
		cdsprm_thermal_cdsp_clk_limit(0);
		gcdsprm.thermal_cdsp_level = 0;
	} else if (gcdsprm.thermal_hvx_level) {
		cdsprm_thermal_hvx_instruction_limit(0);
		gcdsprm.thermal_hvx_level = 0;
	}

	if (gcdsprm.b_rpmsg_register) {
		unregister_rpmsg_driver(&cdsprm_rpmsg_client);
		gcdsprm.rpmsgdev = NULL;
		gcdsprm.cdsp_version = 0;
	}

	if (gcdsprm.cdsprm_wq_task)
		kthread_stop(gcdsprm.cdsprm_wq_task);

	if (gcdsprm.delay_work_queue)
		destroy_workqueue(gcdsprm.delay_work_queue);

	if (gcdsprm.b_cdsprm_devinit)
		platform_driver_unregister(&cdsp_rm);

	if (gcdsprm.b_hvxrm_devinit)
		platform_driver_unregister(&hvx_rm);

	debugfs_remove_recursive(gcdsprm.debugfs_dir);

	gcdsprm.b_rpmsg_register = false;
	gcdsprm.b_hvxrm_devinit = false;
	gcdsprm.b_cdsprm_devinit = false;
	gcdsprm.debugfs_dir = NULL;
	gcdsprm.debugfs_file_priority = NULL;
	gcdsprm.debugfs_file_vtcm = NULL;
#if IS_ENABLED(CONFIG_CDSPRM_VTCM_DYNAMIC_DEBUG)
	gcdsprm.debugfs_file_vtcm_test = NULL;
#endif
	gcdsprm.debugfs_resmgr_pdkill_override = NULL;
	gcdsprm.debugfs_resmgr_pdkill_state = NULL;
	gcdsprm.hvx_tcdev = NULL;
	gcdsprm.cdsp_tcdev = NULL;

	pr_info("Exited\n");
}

module_init(cdsprm_init);
module_exit(cdsprm_exit);

MODULE_LICENSE("GPL");
