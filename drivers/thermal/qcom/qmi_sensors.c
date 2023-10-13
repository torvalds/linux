// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/net.h>
#include <linux/kernel.h>
#include <linux/suspend.h>

#include "thermal_sensor_service_v01.h"
#include "qmi_sensors.h"
#include "thermal_zone_internal.h"

#define QMI_SENS_DRIVER		"qmi-therm-sensors"
#define QMI_TS_RESP_TOUT	msecs_to_jiffies(100)
#define QMI_FL_SIGN		0x80000000
#define QMI_FL_EXP		0x7f800000
#define QMI_FL_MANTISSA		0x007fffff
#define QMI_FL_NORM		0x00800000
#define QMI_FL_SIGN_BIT		31
#define QMI_MANTISSA_MSB	23

struct qmi_sensor {
	struct device			*dev;
	char				qmi_name[QMI_CLIENT_NAME_LENGTH];
	bool                            connection_active;
	struct list_head		ts_node;
	struct thermal_zone_device	*tz_dev;
	int32_t				last_reading;
	int32_t				high_thresh;
	int32_t				low_thresh;
	struct qmi_ts_instance		*ts;
	enum qmi_ts_sensor		sens_type;
	struct work_struct		therm_notify_work;
};

struct qmi_ts_instance {
	struct device			*dev;
	struct qmi_handle		handle;
	struct mutex			mutex;
	uint32_t			inst_id;
	struct list_head		ts_sensor_list;
	struct work_struct		svc_arrive_work;
};

static struct qmi_ts_instance *ts_instances;
static int ts_inst_cnt;
static atomic_t in_suspend;

static int32_t encode_qmi(int32_t val)
{
	uint32_t shift = 0, local_val = 0;
	unsigned long temp_val = 0;

	if (val == INT_MAX || val == INT_MIN)
		return 0;

	temp_val = val = val / 1000;
	if (val < 0) {
		temp_val *= -1;
		local_val |= 1 << QMI_FL_SIGN_BIT;
	}
	shift = find_last_bit(&temp_val, sizeof(temp_val) * 8);
	local_val |= ((shift + 127) << QMI_MANTISSA_MSB);
	temp_val &= ~(1 << shift);

	local_val |= temp_val << (QMI_MANTISSA_MSB - shift);
	pr_debug("inp:%d shift:%d out:%x temp_val:%x\n",
			val, shift, local_val, temp_val);

	return local_val;
}

static int32_t decode_qmi(int32_t float32)
{
	int fraction, shift, mantissa, sign, exp, zeropre;

	mantissa = float32 & GENMASK(22, 0);
	sign = (float32 & BIT(31)) ? -1 : 1;
	exp = (float32 & ~BIT(31)) >> 23;

	if (!exp && !mantissa)
		return 0;

	exp -= 127;
	if (exp < 0) {
		exp = -exp;
		zeropre = (((BIT(23) + mantissa) * 100) >> 23) >> exp;
		return zeropre >= 50 ? sign : 0;
	}

	shift = 23 - exp;
	float32 = BIT(exp) + (mantissa >> shift);
	fraction = mantissa & GENMASK(shift - 1, 0);

	return (((fraction * 100) >> shift) >= 50) ? sign * (float32 + 1) : sign * float32;
}

static int qmi_sensor_pm_notify(struct notifier_block *nb,
				unsigned long mode, void *_unused)
{
	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		atomic_set(&in_suspend, 1);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		atomic_set(&in_suspend, 0);
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block qmi_sensor_pm_nb = {
	.notifier_call = qmi_sensor_pm_notify,
};

static void qmi_ts_thresh_notify(struct work_struct *work)
{
	struct qmi_sensor *qmi_sens = container_of(work,
						struct qmi_sensor,
						therm_notify_work);

	thermal_zone_device_update(qmi_sens->tz_dev, THERMAL_TRIP_VIOLATED);
};

static void qmi_ts_update_temperature(struct qmi_ts_instance *ts,
		const struct ts_temp_report_ind_msg_v01 *ind_msg,
		uint8_t notify)
{
	struct qmi_sensor *qmi_sens;

	list_for_each_entry(qmi_sens, &ts->ts_sensor_list,
					ts_node) {
		if ((strncasecmp(qmi_sens->qmi_name,
			ind_msg->sensor_id.sensor_id,
			QMI_TS_SENSOR_ID_LENGTH_MAX_V01)))
			continue;

		qmi_sens->last_reading =
			decode_qmi(ind_msg->temp) * 1000;
		pr_debug("sensor:%s temperature:%d\n",
				qmi_sens->qmi_name, qmi_sens->last_reading);
		if (!qmi_sens->tz_dev)
			return;
		if (notify &&
			((qmi_sens->high_thresh != INT_MAX &&
			qmi_sens->last_reading >= qmi_sens->high_thresh) ||
			(qmi_sens->low_thresh != (-INT_MAX) &&
			 qmi_sens->last_reading <= qmi_sens->low_thresh))) {
			pr_debug("Sensor:%s Notify. temp:%d\n",
					ind_msg->sensor_id.sensor_id,
					qmi_sens->last_reading);
			queue_work(system_highpri_wq,
					&qmi_sens->therm_notify_work);
		}
		return;
	}
}

void qmi_ts_ind_cb(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
				   struct qmi_txn *txn, const void *decoded)
{
	const struct ts_temp_report_ind_msg_v01 *ind_msg = decoded;
	uint8_t notify = 0;
	struct qmi_ts_instance *ts = container_of(qmi, struct qmi_ts_instance,
							handle);

	if (!txn) {
		pr_err("Invalid transaction\n");
		return;
	}

	if ((ind_msg->report_type != QMI_TS_TEMP_REPORT_CURRENT_TEMP_V01) ||
		ind_msg->seq_num_valid)
		notify = 1;

	if (ind_msg->temp_valid)
		qmi_ts_update_temperature(ts, ind_msg, notify);
	else
		pr_err("Error invalid temperature field.\n");
}

static int qmi_ts_request(struct qmi_sensor *qmi_sens,
				bool send_current_temp_report)
{
	int ret = 0;
	struct ts_register_notification_temp_resp_msg_v01 resp;
	struct ts_register_notification_temp_req_msg_v01 req;
	struct qmi_ts_instance *ts = qmi_sens->ts;
	struct qmi_txn txn;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	strscpy(req.sensor_id.sensor_id, qmi_sens->qmi_name,
		QMI_TS_SENSOR_ID_LENGTH_MAX_V01);
	req.seq_num = 0;
	if (send_current_temp_report) {
		req.send_current_temp_report = 1;
		req.seq_num_valid = true;
	} else {
		req.seq_num_valid = false;
		req.temp_threshold_high_valid =
			qmi_sens->high_thresh != INT_MAX;
		req.temp_threshold_high =
			encode_qmi(qmi_sens->high_thresh);
		req.temp_threshold_low_valid =
			qmi_sens->low_thresh != (-INT_MAX);
		req.temp_threshold_low =
			encode_qmi(qmi_sens->low_thresh);

		pr_debug("Sensor:%s set high_trip:%d, low_trip:%d, high_valid:%d, low_valid:%d\n",
			qmi_sens->qmi_name,
			qmi_sens->high_thresh,
			qmi_sens->low_thresh,
			req.temp_threshold_high_valid,
			req.temp_threshold_low_valid);
	}

	mutex_lock(&ts->mutex);

	ret = qmi_txn_init(&ts->handle, &txn,
		ts_register_notification_temp_resp_msg_v01_ei, &resp);
	if (ret < 0) {
		pr_err("qmi txn init failed for %s ret:%d\n",
			qmi_sens->qmi_name, ret);
		goto qmi_send_exit;
	}

	ret = qmi_send_request(&ts->handle, NULL, &txn,
			QMI_TS_REGISTER_NOTIFICATION_TEMP_REQ_V01,
			TS_REGISTER_NOTIFICATION_TEMP_REQ_MSG_V01_MAX_MSG_LEN,
			ts_register_notification_temp_req_msg_v01_ei, &req);
	if (ret < 0) {
		pr_err("qmi txn send failed for %s ret:%d\n",
			qmi_sens->qmi_name, ret);
		qmi_txn_cancel(&txn);
		goto qmi_send_exit;
	}

	ret = qmi_txn_wait(&txn, QMI_TS_RESP_TOUT);
	if (ret < 0) {
		pr_err("qmi txn wait failed for %s ret:%d\n",
			qmi_sens->qmi_name, ret);
		goto qmi_send_exit;
	}
	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ret = resp.resp.result;
		pr_err("qmi NOT success for %s ret:%d\n",
			qmi_sens->qmi_name, ret);
		goto qmi_send_exit;
	}
	ret = 0;

qmi_send_exit:
	mutex_unlock(&ts->mutex);
	return ret;
}

static int qmi_sensor_read(struct thermal_zone_device *tz, int *temp)
{
	struct qmi_sensor *qmi_sens = (struct qmi_sensor *)tz->devdata;

	if (qmi_sens->connection_active && !atomic_read(&in_suspend))
		qmi_ts_request(qmi_sens, true);
	*temp = qmi_sens->last_reading;

	return 0;
}

static int qmi_sensor_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct qmi_sensor *qmi_sens = (struct qmi_sensor *)tz->devdata;
	int ret = 0;

	if (qmi_sens->high_thresh == high &&
		qmi_sens->low_thresh == low)
		return ret;
	qmi_sens->high_thresh = high;
	qmi_sens->low_thresh = low;
	if (!qmi_sens->connection_active)
		return ret;
	ret = qmi_ts_request(qmi_sens, false);
	if (ret)
		pr_err("Sensor:%s set high trip:%d low trip:%d error%d\n",
				qmi_sens->qmi_name,
				qmi_sens->high_thresh,
				qmi_sens->low_thresh,
				ret);

	return ret;
}

static struct thermal_zone_device_ops qmi_sensor_ops = {
	.get_temp = qmi_sensor_read,
	.set_trips = qmi_sensor_set_trips,
};

static struct qmi_msg_handler handlers[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_TS_TEMP_REPORT_IND_V01,
		.ei = ts_temp_report_ind_msg_v01_ei,
		.decoded_size = sizeof(struct ts_temp_report_ind_msg_v01),
		.fn = qmi_ts_ind_cb
	},
	{}
};

static int qmi_register_sensor_device(struct qmi_sensor *qmi_sens)
{
	int ret = 0;

	qmi_sens->tz_dev = devm_thermal_of_zone_register(
				qmi_sens->dev,
				qmi_sens->sens_type + qmi_sens->ts->inst_id,
				qmi_sens, &qmi_sensor_ops);
	if (IS_ERR(qmi_sens->tz_dev)) {
		ret = PTR_ERR(qmi_sens->tz_dev);
		if (ret != -ENODEV)
			pr_err("sensor register failed for %s, ret:%d\n",
				qmi_sens->qmi_name, ret);
		qmi_sens->tz_dev = NULL;
		return ret;
	}
	qti_update_tz_ops(qmi_sens->tz_dev, true);

	pr_debug("Sensor register success for %s\n", qmi_sens->qmi_name);

	return 0;
}

static int verify_sensor_and_register(struct qmi_ts_instance *ts)
{
	struct ts_get_sensor_list_req_msg_v01 req;
	struct ts_get_sensor_list_resp_msg_v01 *ts_resp;
	int ret = 0, i;
	struct qmi_txn txn;

	memset(&req, 0, sizeof(req));
	/* size of ts_resp is very high, use heap memory rather than stack */
	ts_resp = kzalloc(sizeof(*ts_resp), GFP_KERNEL);
	if (!ts_resp)
		return -ENOMEM;

	mutex_lock(&ts->mutex);
	ret = qmi_txn_init(&ts->handle, &txn,
		ts_get_sensor_list_resp_msg_v01_ei, ts_resp);
	if (ret < 0) {
		pr_err("Transaction Init error for inst_id:0x%x ret:%d\n",
			ts->inst_id, ret);
		goto reg_exit;
	}

	ret = qmi_send_request(&ts->handle, NULL, &txn,
			QMI_TS_GET_SENSOR_LIST_REQ_V01,
			TS_GET_SENSOR_LIST_REQ_MSG_V01_MAX_MSG_LEN,
			ts_get_sensor_list_req_msg_v01_ei,
			&req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		goto reg_exit;
	}

	ret = qmi_txn_wait(&txn, QMI_TS_RESP_TOUT);
	if (ret < 0) {
		pr_err("Transaction wait error for inst_id:0x%x ret:%d\n",
			ts->inst_id, ret);
		goto reg_exit;
	}
	if (ts_resp->resp.result != QMI_RESULT_SUCCESS_V01) {
		ret = ts_resp->resp.result;
		pr_err("Get sensor list NOT success for inst_id:0x%x ret:%d\n",
			ts->inst_id, ret);
		goto reg_exit;
	}
	mutex_unlock(&ts->mutex);

	for (i = 0; i < ts_resp->sensor_list_len; i++) {
		struct qmi_sensor *qmi_sens = NULL;

		list_for_each_entry(qmi_sens, &ts->ts_sensor_list,
					ts_node) {
			if ((strncasecmp(qmi_sens->qmi_name,
				ts_resp->sensor_list[i].sensor_id,
				QMI_TS_SENSOR_ID_LENGTH_MAX_V01)))
				continue;

			qmi_sens->connection_active = true;
			/*
			 * Send a temperature request notification.
			 */
			qmi_ts_request(qmi_sens, true);
			if (!qmi_sens->tz_dev)
				ret = qmi_register_sensor_device(qmi_sens);
			break;
		}
	}

	/* Check and get sensor list extended */
	for (i = 0; ts_resp->sensor_list_ext01_valid &&
		 (i < ts_resp->sensor_list_ext01_len); i++) {
		struct qmi_sensor *qmi_sens = NULL;

		list_for_each_entry(qmi_sens, &ts->ts_sensor_list,
					ts_node) {
			if ((strncasecmp(qmi_sens->qmi_name,
				ts_resp->sensor_list_ext01[i].sensor_id,
				QMI_TS_SENSOR_ID_LENGTH_MAX_V01)))
				continue;

			qmi_sens->connection_active = true;
			/*
			 * Send a temperature request notification.
			 */
			qmi_ts_request(qmi_sens, true);
			if (!qmi_sens->tz_dev)
				ret = qmi_register_sensor_device(qmi_sens);
			break;
		}
	}

	kfree(ts_resp);
	return ret;

reg_exit:
	mutex_unlock(&ts->mutex);
	kfree(ts_resp);

	return ret;
}

static void qmi_ts_svc_arrive(struct work_struct *work)
{
	struct qmi_ts_instance *ts = container_of(work,
						struct qmi_ts_instance,
						svc_arrive_work);

	verify_sensor_and_register(ts);
}

static void thermal_qmi_net_reset(struct qmi_handle *qmi)
{
	struct qmi_ts_instance *ts = container_of(qmi,
						struct qmi_ts_instance,
						handle);
	struct qmi_sensor *qmi_sens = NULL;
	int ret;

	pr_debug("reset QMI server\n");
	list_for_each_entry(qmi_sens, &ts->ts_sensor_list,
					ts_node) {
		if (!qmi_sens->connection_active)
			continue;
		qmi_ts_request(qmi_sens, true);
		ret = qmi_ts_request(qmi_sens, false);
		if (ret)
			pr_err("Sensor:%s set high trip:%d low trip:%d err%d\n",
				qmi_sens->tz_dev->type,
				qmi_sens->high_thresh,
				qmi_sens->low_thresh,
				ret);
	}
}

static void thermal_qmi_del_server(struct qmi_handle *qmi,
				    struct qmi_service *service)
{
	struct qmi_ts_instance *ts = container_of(qmi,
						struct qmi_ts_instance,
						handle);
	struct qmi_sensor *qmi_sens = NULL;

	pr_debug("QMI server deleted\n");
	list_for_each_entry(qmi_sens, &ts->ts_sensor_list, ts_node)
		qmi_sens->connection_active = false;
}

static int thermal_qmi_new_server(struct qmi_handle *qmi,
				    struct qmi_service *service)
{
	struct qmi_ts_instance *ts = container_of(qmi,
						struct qmi_ts_instance,
						handle);
	struct sockaddr_qrtr sq = {AF_QIPCRTR, service->node, service->port};

	mutex_lock(&ts->mutex);
	kernel_connect(qmi->sock, (struct sockaddr *)&sq, sizeof(sq), 0);
	mutex_unlock(&ts->mutex);
	queue_work(system_highpri_wq, &ts->svc_arrive_work);

	return 0;
}

static struct qmi_ops thermal_qmi_event_ops = {
	.new_server = thermal_qmi_new_server,
	.del_server = thermal_qmi_del_server,
	.net_reset = thermal_qmi_net_reset,
};

static void qmi_ts_cleanup(void)
{
	struct qmi_ts_instance *ts;
	struct qmi_sensor *qmi_sens, *c_next;
	int idx = 0;

	for (; idx < ts_inst_cnt; idx++) {
		ts = &ts_instances[idx];
		mutex_lock(&ts->mutex);
		list_for_each_entry_safe(qmi_sens, c_next,
			&ts->ts_sensor_list, ts_node) {
			qmi_sens->connection_active = false;
			if (qmi_sens->tz_dev) {
				qti_update_tz_ops(qmi_sens->tz_dev, false);
				qmi_sens->tz_dev = NULL;
			}
			list_del(&qmi_sens->ts_node);
		}
		qmi_handle_release(&ts->handle);

		mutex_unlock(&ts->mutex);
	}
	ts_inst_cnt = 0;
}

static int of_get_qmi_ts_platform_data(struct device *dev)
{
	int ret = 0, i = 0, idx = 0;
	struct device_node *np = dev->of_node;
	struct device_node *subsys_np = NULL;
	struct qmi_ts_instance *ts;
	struct qmi_sensor *qmi_sens;
	int sens_name_max = 0, sens_idx = 0, subsys_cnt = 0;

	subsys_cnt = of_get_available_child_count(np);
	if (!subsys_cnt) {
		dev_err(dev, "No child node to process\n");
		return -EFAULT;
	}

	ts = devm_kcalloc(dev, subsys_cnt, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	for_each_available_child_of_node(np, subsys_np) {
		if (idx >= subsys_cnt)
			break;

		ret = of_property_read_u32(subsys_np, "qcom,instance-id",
					&ts[idx].inst_id);
		if (ret) {
			dev_err(dev, "error reading qcom,insance-id. ret:%d\n",
					ret);
			goto data_fetch_err;
		}

		ts[idx].dev = dev;
		mutex_init(&ts[idx].mutex);
		INIT_LIST_HEAD(&ts[idx].ts_sensor_list);
		INIT_WORK(&ts[idx].svc_arrive_work, qmi_ts_svc_arrive);

		sens_name_max = of_property_count_strings(subsys_np,
					"qcom,qmi-sensor-names");
		if (sens_name_max <= 0) {
			dev_err(dev, "Invalid or no sensor. err:%d\n",
					sens_name_max);
			ret = -EINVAL;
			goto data_fetch_err;
		}

		for (sens_idx = 0; sens_idx < sens_name_max; sens_idx++) {
			const char *qmi_name;

			qmi_sens = devm_kzalloc(dev, sizeof(*qmi_sens),
							GFP_KERNEL);
			if (!qmi_sens) {
				ret = -ENOMEM;
				goto data_fetch_err;
			}

			of_property_read_string_index(subsys_np,
					"qcom,qmi-sensor-names", sens_idx,
					&qmi_name);
			strscpy(qmi_sens->qmi_name, qmi_name,
						QMI_CLIENT_NAME_LENGTH);
			/* Check for supported qmi sensors */
			for (i = 0; i < QMI_TS_MAX_NR; i++) {
				if (!strcmp(sensor_clients[i],
							qmi_sens->qmi_name))
					break;
			}

			if (i >= QMI_TS_MAX_NR) {
				dev_err(dev, "Unknown sensor:%s\n",
					qmi_sens->qmi_name);
				ret = -EINVAL;
				goto data_fetch_err;
			}
			dev_dbg(dev, "QMI sensor:%s available\n", qmi_name);
			qmi_sens->sens_type = i;
			qmi_sens->ts = &ts[idx];
			qmi_sens->dev = dev;
			qmi_sens->last_reading = 0;
			qmi_sens->high_thresh = INT_MAX;
			qmi_sens->low_thresh = -INT_MAX;
			INIT_WORK(&qmi_sens->therm_notify_work,
					qmi_ts_thresh_notify);
			list_add(&qmi_sens->ts_node, &ts[idx].ts_sensor_list);
		}
		idx++;
	}
	ts_instances = ts;
	ts_inst_cnt = subsys_cnt;

	return 0;
data_fetch_err:
	of_node_put(subsys_np);
	return ret;
}

static int qmi_sens_device_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0, idx = 0;
	struct qmi_ts_instance *ts;

	ret = of_get_qmi_ts_platform_data(dev);
	if (ret)
		goto probe_err;

	if (!ts_instances || !ts_inst_cnt) {
		dev_err(dev, "Empty ts instances\n");
		return -EINVAL;
	}

	for (; idx < ts_inst_cnt; idx++) {
		ts = &ts_instances[idx];
		if (list_empty(&ts->ts_sensor_list)) {
			ret = -ENODEV;
			goto probe_err;
		}
		ret = qmi_handle_init(&ts->handle,
			TS_GET_SENSOR_LIST_RESP_MSG_V01_MAX_MSG_LEN,
			&thermal_qmi_event_ops, handlers);
		if (ret < 0) {
			dev_err(dev, "QMI[0x%x] handle init failed. err:%d\n",
					ts->inst_id, ret);
			ts_inst_cnt = idx;
			ret = -EPROBE_DEFER;
			goto probe_err;
		}
		ret = qmi_add_lookup(&ts->handle, TS_SERVICE_ID_V01,
				TS_SERVICE_VERS_V01, ts->inst_id);
		if (ret < 0) {
			dev_err(dev, "QMI register failed for 0x%x, ret:%d\n",
				ts->inst_id, ret);
			ret = -EPROBE_DEFER;
			goto probe_err;
		}
	}
	atomic_set(&in_suspend, 0);
	register_pm_notifier(&qmi_sensor_pm_nb);
	return 0;

probe_err:
	qmi_ts_cleanup();
	return ret;
}

static int qmi_sens_device_remove(struct platform_device *pdev)
{
	qmi_ts_cleanup();
	unregister_pm_notifier(&qmi_sensor_pm_nb);

	return 0;
}

static const struct of_device_id qmi_sens_device_match[] = {
	{.compatible = "qcom,qmi-sensors"},
	{}
};

static struct platform_driver qmi_sens_device_driver = {
	.probe          = qmi_sens_device_probe,
	.remove         = qmi_sens_device_remove,
	.driver         = {
		.name   = QMI_SENS_DRIVER,
		.of_match_table = qmi_sens_device_match,
	},
};

module_platform_driver(qmi_sens_device_driver);
MODULE_LICENSE("GPL");
