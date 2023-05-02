// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "thermal_mitigation_device_service_v01.h"

#define QMI_CDEV_DRIVER		"qmi-cooling-device"
#define QMI_TMD_RESP_TOUT	msecs_to_jiffies(100)

struct qmi_cooling_device {
	struct device_node		*np;
	char				cdev_name[QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01];
	char				qmi_name[QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01];
	bool                            connection_active;
	struct list_head		qmi_node;
	struct thermal_cooling_device	*cdev;
	unsigned int			mtgn_state;
	unsigned int			max_level;
	struct qmi_tmd_instance		*tmd;
};

struct qmi_tmd_instance {
	struct device			*dev;
	struct qmi_handle		handle;
	struct mutex			mutex;
	uint32_t			inst_id;
	struct list_head		tmd_cdev_list;
	struct work_struct		svc_arrive_work;
};

static struct qmi_tmd_instance *tmd_instances;
static int tmd_inst_cnt;

static char  device_clients[][QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01] = {
	{"pa"},
	{"pa_fr1"},
	{"cx_vdd_limit"},
	{"modem"},
	{"modem_current"},
	{"modem_skin"},
	{"modem_bw"},
	{"modem_bw_backoff"},
	{"vbatt_low"},
	{"charge_state"},
	{"mmw0"},
	{"mmw1"},
	{"mmw2"},
	{"mmw3"},
	{"mmw_skin0"},
	{"mmw_skin1"},
	{"mmw_skin2"},
	{"mmw_skin3"},
	{"wlan"},
	{"wlan_bw"},
	{"mmw_skin0_dsc"},
	{"mmw_skin1_dsc"},
	{"mmw_skin2_dsc"},
	{"mmw_skin3_dsc"},
	{"modem_skin_lte_dsc"},
	{"modem_skin_nr_dsc"},
	{"pa_dsc"},
	{"pa_fr1_dsc"},
	{"cdsp_sw"},
	{"cdsp_sw_hvx"},
	{"cdsp_sw_hmx"},
	{"cdsp_hw"},
	{"cpuv_restriction_cold"},
	{"cpr_cold"},
	{"modem_lte_dsc"},
	{"modem_nr_dsc"},
	{"modem_nr_scg_dsc"},
	{"sdr0_lte_dsc"},
	{"sdr1_lte_dsc"},
	{"sdr0_nr_dsc"},
	{"sdr1_nr_dsc"},
	{"sdr0_nr_scg_dsc"},
	{"sdr1_nr_scg_dsc"},
	{"pa_lte_sdr0_dsc"},
	{"pa_lte_sdr1_dsc"},
	{"pa_nr_sdr0_dsc"},
	{"pa_nr_sdr1_dsc"},
	{"pa_nr_sdr0_scg_dsc"},
	{"pa_nr_sdr1_scg_dsc"},
	{"mmw0_dsc"},
	{"mmw1_dsc"},
	{"mmw2_dsc"},
	{"mmw3_dsc"},
	{"mmw_ific_dsc"},
	{"modem_lte_sub1_dsc"},
	{"modem_nr_sub1_dsc"},
	{"modem_nr_scg_sub1_dsc"},
	{"sdr0_lte_sub1_dsc"},
	{"sdr1_lte_sub1_dsc"},
	{"sdr0_nr_sub1_dsc"},
	{"sdr1_nr_sub1_dsc"},
	{"pa_lte_sdr0_sub1_dsc"},
	{"pa_lte_sdr1_sub1_dsc"},
	{"pa_nr_sdr0_sub1_dsc"},
	{"pa_nr_sdr1_sub1_dsc"},
	{"pa_nr_sdr0_scg_sub1_dsc"},
	{"pa_nr_sdr1_scg_sub1_dsc"},
	{"mmw0_sub1_dsc"},
	{"mmw1_sub1_dsc"},
	{"mmw2_sub1_dsc"},
	{"mmw3_sub1_dsc"},
	{"mmw_ific_sub1_dsc"},
	{"bcl"},
};

static int qmi_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct qmi_cooling_device *qmi_cdev = cdev->devdata;

	if (!qmi_cdev)
		return -EINVAL;

	*state = qmi_cdev->max_level;

	return 0;
}

static int qmi_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct qmi_cooling_device *qmi_cdev = cdev->devdata;

	if (!qmi_cdev)
		return -EINVAL;

	*state = qmi_cdev->mtgn_state;

	return 0;
}

static int qmi_tmd_send_state_request(struct qmi_cooling_device *qmi_cdev,
				uint8_t state)
{
	int ret = 0;
	struct tmd_set_mitigation_level_req_msg_v01 req;
	struct tmd_set_mitigation_level_resp_msg_v01 tmd_resp;
	struct qmi_tmd_instance *tmd = qmi_cdev->tmd;
	struct qmi_txn txn;

	memset(&req, 0, sizeof(req));
	memset(&tmd_resp, 0, sizeof(tmd_resp));

	strscpy(req.mitigation_dev_id.mitigation_dev_id, qmi_cdev->qmi_name,
		QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01);
	req.mitigation_level = state;

	mutex_lock(&tmd->mutex);

	ret = qmi_txn_init(&tmd->handle, &txn,
		tmd_set_mitigation_level_resp_msg_v01_ei, &tmd_resp);
	if (ret < 0) {
		pr_err("qmi set state:%d txn init failed for %s ret:%d\n",
			state, qmi_cdev->cdev_name, ret);
		goto qmi_send_exit;
	}

	ret = qmi_send_request(&tmd->handle, NULL, &txn,
			QMI_TMD_SET_MITIGATION_LEVEL_REQ_V01,
			TMD_SET_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN,
			tmd_set_mitigation_level_req_msg_v01_ei, &req);
	if (ret < 0) {
		pr_err("qmi set state:%d txn send failed for %s ret:%d\n",
			state, qmi_cdev->cdev_name, ret);
		qmi_txn_cancel(&txn);
		goto qmi_send_exit;
	}

	ret = qmi_txn_wait(&txn, QMI_TMD_RESP_TOUT);
	if (ret < 0) {
		pr_err("qmi set state:%d txn wait failed for %s ret:%d\n",
			state, qmi_cdev->cdev_name, ret);
		goto qmi_send_exit;
	}
	if (tmd_resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ret = tmd_resp.resp.result;
		pr_err("qmi set state:%d NOT success for %s ret:%d\n",
			state, qmi_cdev->cdev_name, ret);
		goto qmi_send_exit;
	}
	ret = 0;
	pr_debug("Requested qmi state:%d for %s\n", state, qmi_cdev->cdev_name);

qmi_send_exit:
	mutex_unlock(&tmd->mutex);
	return ret;
}

static int qmi_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct qmi_cooling_device *qmi_cdev = cdev->devdata;
	int ret = 0;

	if (!qmi_cdev)
		return -EINVAL;

	if (state > qmi_cdev->max_level)
		return -EINVAL;

	if (qmi_cdev->mtgn_state == state)
		return 0;

	/* save it and return if server exit */
	if (!qmi_cdev->connection_active) {
		qmi_cdev->mtgn_state = state;
		pr_debug("Pending request:%ld for %s\n", state,
				qmi_cdev->cdev_name);
		return 0;
	}

	/* It is best effort to save state even if QMI fail */
	ret = qmi_tmd_send_state_request(qmi_cdev, (uint8_t)state);

	qmi_cdev->mtgn_state = state;

	return ret;
}

static struct thermal_cooling_device_ops qmi_device_ops = {
	.get_max_state = qmi_get_max_state,
	.get_cur_state = qmi_get_cur_state,
	.set_cur_state = qmi_set_cur_state,
};

static int qmi_register_cooling_device(struct qmi_cooling_device *qmi_cdev)
{
	qmi_cdev->cdev = thermal_of_cooling_device_register(
					qmi_cdev->np,
					qmi_cdev->cdev_name,
					qmi_cdev,
					&qmi_device_ops);
	if (IS_ERR(qmi_cdev->cdev)) {
		pr_err("Cooling register failed for %s, ret:%ld\n",
			qmi_cdev->cdev_name, PTR_ERR(qmi_cdev->cdev));
		return PTR_ERR(qmi_cdev->cdev);
	}
	pr_debug("Cooling register success for %s\n", qmi_cdev->cdev_name);

	return 0;
}

static int verify_devices_and_register(struct qmi_tmd_instance *tmd)
{
	struct tmd_get_mitigation_device_list_req_msg_v01 req;
	struct tmd_get_mitigation_device_list_resp_msg_v01 *tmd_resp;
	int ret = 0, i;
	struct qmi_txn txn;

	memset(&req, 0, sizeof(req));
	/* size of tmd_resp is very high, use heap memory rather than stack */
	tmd_resp = kzalloc(sizeof(*tmd_resp), GFP_KERNEL);
	if (!tmd_resp)
		return -ENOMEM;

	mutex_lock(&tmd->mutex);
	ret = qmi_txn_init(&tmd->handle, &txn,
		tmd_get_mitigation_device_list_resp_msg_v01_ei, tmd_resp);
	if (ret < 0) {
		pr_err("Transaction Init error for inst_id:0x%x ret:%d\n",
			tmd->inst_id, ret);
		goto reg_exit;
	}

	ret = qmi_send_request(&tmd->handle, NULL, &txn,
			QMI_TMD_GET_MITIGATION_DEVICE_LIST_REQ_V01,
			TMD_GET_MITIGATION_DEVICE_LIST_REQ_MSG_V01_MAX_MSG_LEN,
			tmd_get_mitigation_device_list_req_msg_v01_ei,
			&req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		goto reg_exit;
	}

	ret = qmi_txn_wait(&txn, QMI_TMD_RESP_TOUT);
	if (ret < 0) {
		pr_err("Transaction wait error for inst_id:0x%x ret:%d\n",
			tmd->inst_id, ret);
		goto reg_exit;
	}
	if (tmd_resp->resp.result != QMI_RESULT_SUCCESS_V01) {
		ret = tmd_resp->resp.result;
		pr_err("Get device list NOT success for inst_id:0x%x ret:%d\n",
			tmd->inst_id, ret);
		goto reg_exit;
	}
	mutex_unlock(&tmd->mutex);

	for (i = 0; i < tmd_resp->mitigation_device_list_len; i++) {
		struct qmi_cooling_device *qmi_cdev = NULL;

		list_for_each_entry(qmi_cdev, &tmd->tmd_cdev_list,
					qmi_node) {
			struct tmd_mitigation_dev_list_type_v01 *device =
				&tmd_resp->mitigation_device_list[i];

			if ((strncasecmp(qmi_cdev->qmi_name,
				device->mitigation_dev_id.mitigation_dev_id,
				QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01)))
				continue;

			qmi_cdev->connection_active = true;
			qmi_cdev->max_level = device->max_mitigation_level;
			/*
			 * It is better to set current state
			 * initially or during restart
			 */
			qmi_tmd_send_state_request(qmi_cdev,
							qmi_cdev->mtgn_state);
			if (!qmi_cdev->cdev)
				ret = qmi_register_cooling_device(qmi_cdev);
			break;
		}
	}

	for (i = 0; tmd_resp->mitigation_device_list_ext01_valid &&
		i < tmd_resp->mitigation_device_list_ext01_len; i++) {
		struct qmi_cooling_device *qmi_cdev = NULL;

		list_for_each_entry(qmi_cdev, &tmd->tmd_cdev_list,
					qmi_node) {
			struct tmd_mitigation_dev_list_type_v01 *device =
				&tmd_resp->mitigation_device_list_ext01[i];

			if ((strncasecmp(qmi_cdev->qmi_name,
				device->mitigation_dev_id.mitigation_dev_id,
				QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01)))
				continue;

			qmi_cdev->connection_active = true;
			qmi_cdev->max_level = device->max_mitigation_level;
			/*
			 * It is better to set current state
			 * initially or during restart
			 */
			qmi_tmd_send_state_request(qmi_cdev,
						qmi_cdev->mtgn_state);
			if (!qmi_cdev->cdev)
				ret = qmi_register_cooling_device(qmi_cdev);
			break;
		}
	}

	kfree(tmd_resp);
	return ret;

reg_exit:
	mutex_unlock(&tmd->mutex);
	kfree(tmd_resp);

	return ret;
}

static void qmi_tmd_svc_arrive(struct work_struct *work)
{
	struct qmi_tmd_instance *tmd = container_of(work,
						struct qmi_tmd_instance,
						svc_arrive_work);

	verify_devices_and_register(tmd);
}

static void thermal_qmi_net_reset(struct qmi_handle *qmi)
{
	struct qmi_tmd_instance *tmd = container_of(qmi,
						struct qmi_tmd_instance,
						handle);
	struct qmi_cooling_device *qmi_cdev = NULL;

	list_for_each_entry(qmi_cdev, &tmd->tmd_cdev_list,
					qmi_node) {
		if (qmi_cdev->connection_active)
			qmi_tmd_send_state_request(qmi_cdev,
							qmi_cdev->mtgn_state);
	}
}

static void thermal_qmi_del_server(struct qmi_handle *qmi,
				    struct qmi_service *service)
{
	struct qmi_tmd_instance *tmd = container_of(qmi,
						struct qmi_tmd_instance,
						handle);
	struct qmi_cooling_device *qmi_cdev = NULL;

	list_for_each_entry(qmi_cdev, &tmd->tmd_cdev_list, qmi_node)
		qmi_cdev->connection_active = false;
}

static int thermal_qmi_new_server(struct qmi_handle *qmi,
				    struct qmi_service *service)
{
	struct qmi_tmd_instance *tmd = container_of(qmi,
						struct qmi_tmd_instance,
						handle);
	struct sockaddr_qrtr sq = {AF_QIPCRTR, service->node, service->port};

	mutex_lock(&tmd->mutex);
	kernel_connect(qmi->sock, (struct sockaddr *)&sq, sizeof(sq), 0);
	mutex_unlock(&tmd->mutex);
	queue_work(system_highpri_wq, &tmd->svc_arrive_work);

	return 0;
}

static struct qmi_ops thermal_qmi_event_ops = {
	.new_server = thermal_qmi_new_server,
	.del_server = thermal_qmi_del_server,
	.net_reset = thermal_qmi_net_reset,
};

static void qmi_tmd_cleanup(void)
{
	int idx = 0;
	struct qmi_tmd_instance *tmd = tmd_instances;
	struct qmi_cooling_device *qmi_cdev, *c_next;

	for (; idx < tmd_inst_cnt; idx++) {
		mutex_lock(&tmd[idx].mutex);
		list_for_each_entry_safe(qmi_cdev, c_next,
				&tmd[idx].tmd_cdev_list, qmi_node) {
			qmi_cdev->connection_active = false;
			if (qmi_cdev->cdev)
				thermal_cooling_device_unregister(
					qmi_cdev->cdev);

			list_del(&qmi_cdev->qmi_node);
		}
		qmi_handle_release(&tmd[idx].handle);

		mutex_unlock(&tmd[idx].mutex);
	}
}

static int of_get_qmi_tmd_platform_data(struct device *dev)
{
	int ret = 0, idx = 0, i = 0, subsys_cnt = 0;
	struct device_node *np = dev->of_node;
	struct device_node *subsys_np = NULL, *cdev_np = NULL;
	struct qmi_tmd_instance *tmd;
	struct qmi_cooling_device *qmi_cdev;

	subsys_cnt = of_get_available_child_count(np);
	if (!subsys_cnt) {
		dev_err(dev, "No child node to process\n");
		return -EFAULT;
	}

	tmd = devm_kcalloc(dev, subsys_cnt, sizeof(*tmd), GFP_KERNEL);
	if (!tmd)
		return -ENOMEM;

	for_each_available_child_of_node(np, subsys_np) {
		if (idx >= subsys_cnt) {
			of_node_put(subsys_np);
			break;
		}

		ret = of_property_read_u32(subsys_np, "qcom,instance-id",
				&tmd[idx].inst_id);
		if (ret) {
			dev_err(dev, "error reading qcom,insance-id. ret:%d\n",
				ret);
			goto data_subsys_error;
		}

		tmd[idx].dev = dev;
		mutex_init(&tmd[idx].mutex);
		INIT_LIST_HEAD(&tmd[idx].tmd_cdev_list);
		INIT_WORK(&tmd[idx].svc_arrive_work, qmi_tmd_svc_arrive);

		for_each_available_child_of_node(subsys_np, cdev_np) {
			const char *qmi_name;

			qmi_cdev = devm_kzalloc(dev, sizeof(*qmi_cdev),
					GFP_KERNEL);
			if (!qmi_cdev) {
				ret = -ENOMEM;
				goto data_error;
			}

			strscpy(qmi_cdev->cdev_name, cdev_np->name,
				QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01);

			if (!of_property_read_string(cdev_np,
					"qcom,qmi-dev-name",
					&qmi_name)) {
				strscpy(qmi_cdev->qmi_name, qmi_name,
				   QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01);
			} else {
				dev_err(dev, "Fail to parse dev name for %s\n",
					cdev_np->name);
				break;
			}
			/* Check for supported qmi dev*/
			for (i = 0; i < ARRAY_SIZE(device_clients); i++) {
				if (strcmp(device_clients[i],
					qmi_cdev->qmi_name) == 0)
					break;
			}

			if (i >= ARRAY_SIZE(device_clients)) {
				dev_err(dev, "Not supported dev name for %s\n",
					cdev_np->name);
				break;
			}
			qmi_cdev->tmd = &tmd[idx];
			qmi_cdev->np = cdev_np;
			qmi_cdev->mtgn_state = 0;
			list_add(&qmi_cdev->qmi_node, &tmd[idx].tmd_cdev_list);
		}
		idx++;
	}
	of_node_put(np);
	tmd_instances = tmd;
	tmd_inst_cnt = subsys_cnt;

	return 0;
data_error:
	of_node_put(cdev_np);
data_subsys_error:
	of_node_put(subsys_np);
	of_node_put(np);

	return ret;
}

static int qmi_device_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0, idx = 0;

	ret = of_get_qmi_tmd_platform_data(dev);
	if (ret)
		goto probe_err;

	if (!tmd_instances || !tmd_inst_cnt) {
		dev_err(dev, "Empty tmd instances\n");
		return -EINVAL;
	}

	for (; idx < tmd_inst_cnt; idx++) {
		struct qmi_tmd_instance *tmd = &tmd_instances[idx];

		if (list_empty(&tmd->tmd_cdev_list))
			continue;

		ret = qmi_handle_init(&tmd->handle,
			TMD_GET_MITIGATION_DEVICE_LIST_RESP_MSG_V01_MAX_MSG_LEN,
			&thermal_qmi_event_ops, NULL);
		if (ret < 0) {
			dev_err(dev, "QMI[0x%x] handle init failed. err:%d\n",
					tmd->inst_id, ret);
			tmd_inst_cnt = idx;
			goto probe_err;
		}
		ret = qmi_add_lookup(&tmd->handle, TMD_SERVICE_ID_V01,
					TMD_SERVICE_VERS_V01,
					tmd->inst_id);
		if (ret < 0) {
			dev_err(dev, "QMI register failed for 0x%x, ret:%d\n",
				tmd->inst_id, ret);
			goto probe_err;
		}
	}

	return 0;

probe_err:
	qmi_tmd_cleanup();
	return ret;
}

static int qmi_device_remove(struct platform_device *pdev)
{
	qmi_tmd_cleanup();

	return 0;
}

static const struct of_device_id qmi_device_match[] = {
	{.compatible = "qcom,qmi-cooling-devices"},
	{}
};

static struct platform_driver qmi_device_driver = {
	.probe          = qmi_device_probe,
	.remove         = qmi_device_remove,
	.driver         = {
		.name   = QMI_CDEV_DRIVER,
		.of_match_table = qmi_device_match,
	},
};

static int __init qmi_device_init(void)
{
	return platform_driver_register(&qmi_device_driver);
}
module_init(qmi_device_init);

static void __exit qmi_device_exit(void)
{
	platform_driver_unregister(&qmi_device_driver);
}
module_exit(qmi_device_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QTI QMI cooling device driver");
