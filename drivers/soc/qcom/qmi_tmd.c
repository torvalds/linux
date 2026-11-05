// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025, Linaro Limited
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * QMI Thermal Mitigation Device (TMD).
 * Provides cooling device support for remote subsystems
 * running the TMD service via QMI.
 */
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/soc/qcom/qmi_tmd.h>
#include <linux/thermal.h>

#define QMI_TMD_SERVICE_VERS_V01 0x01

#define QMI_TMD_SET_LEVEL_REQ 0x0021
#define QMI_TMD_GET_DEV_LIST_REQ 0x0020

#define QMI_TMD_DEV_ID_LEN_MAX 32
#define QMI_TMD_DEV_LIST_MAX 32
#define QMI_TMD_RESP_TIMEOUT	msecs_to_jiffies(100)
#define TMD_GET_LEVEL_REQ_MAX_LEN 36
#define TMD_SET_LEVEL_REQ_MAX_LEN 40

#define TMD_GET_DEV_LIST_REQ_MAX_LEN 0
#define TMD_GET_DEV_LIST_RESP_MAX_LEN 1099

struct tmd_dev_id {
	char mitigation_dev_id[QMI_TMD_DEV_ID_LEN_MAX + 1];
};

static const struct qmi_elem_info tmd_dev_id_ei[] = {
	{
		.data_type = QMI_STRING,
		.elem_len = QMI_TMD_DEV_ID_LEN_MAX + 1,
		.elem_size = sizeof(char),
		.array_type = NO_ARRAY,
		.tlv_type = 0,
		.offset = offsetof(struct tmd_dev_id,
				   mitigation_dev_id),
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_dev_list {
	struct tmd_dev_id mitigation_dev_id;
	u8 max_mitigation_level;
};

static const struct qmi_elem_info tmd_dev_list_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct tmd_dev_id),
		.array_type = NO_ARRAY,
		.tlv_type = 0,
		.offset = offsetof(struct tmd_dev_list,
				   mitigation_dev_id),
		.ei_array = tmd_dev_id_ei,
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0,
		.offset = offsetof(struct tmd_dev_list,
				   max_mitigation_level),
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_get_dev_list_req {
	char placeholder;
};

static const struct qmi_elem_info tmd_get_dev_list_req_ei[] = {
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_get_dev_list_resp {
	struct qmi_response_type_v01 resp;
	u8 mitigation_device_list_valid;
	u32 mitigation_device_list_len;
	struct tmd_dev_list
		mitigation_device_list[QMI_TMD_DEV_LIST_MAX];
};

static const struct qmi_elem_info tmd_get_dev_list_resp_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct tmd_get_dev_list_resp,
				   resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct tmd_get_dev_list_resp,
				   mitigation_device_list_valid),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct tmd_get_dev_list_resp,
				   mitigation_device_list_len),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = QMI_TMD_DEV_LIST_MAX,
		.elem_size = sizeof(struct tmd_dev_list),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct tmd_get_dev_list_resp,
				   mitigation_device_list),
		.ei_array = tmd_dev_list_ei,
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_set_level_req {
	struct tmd_dev_id mitigation_dev_id;
	u8 mitigation_level;
};

static const struct qmi_elem_info tmd_set_level_req_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct tmd_dev_id),
		.array_type = NO_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct tmd_set_level_req,
				   mitigation_dev_id),
		.ei_array = tmd_dev_id_ei,
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(uint8_t),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct tmd_set_level_req,
				   mitigation_level),
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct tmd_set_level_resp {
	struct qmi_response_type_v01 resp;
};

static const struct qmi_elem_info tmd_set_level_resp_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct tmd_set_level_resp, resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

/**
 * struct qmi_tmd - A TMD cooling device
 * @name:	The name of this tmd shared by the remote subsystem
 * @cdev:	Thermal cooling device handle
 * @cur_state:	The current mitigation state
 * @max_state:	The maximum state
 * @qmi_tmd_cli:	Parent QMI TMD client
 */
struct qmi_tmd {
	const char *name;
	struct thermal_cooling_device *cdev;
	unsigned int cur_state;
	unsigned int max_state;
	struct qmi_tmd_client *qmi_tmd_cli;
};

/**
 * struct qmi_tmd_client - QMI TMD client state
 * @dev:		Device associated with this instance
 * @handle:		QMI connection handle
 * @mutex:		Serializes QMI request/response sequences (qmi_txn_init,
 *			qmi_send_request) during DSP subsystem restart and
 *			protects @connection_active and @exiting
 * @connection_active:	Whether or not we're connected to the QMI TMD service
 * @exiting:		Whether or not teardown has started
 * @svc_arrive_work:	Work item for initialising when the TMD service starts
 * @num_tmds:		Number of tmds described in the device tree
 * @tmds:		An array of tmd structures
 */
struct qmi_tmd_client {
	struct device *dev;
	struct qmi_handle handle;
	/* protects QMI transactions, connection_active and exiting */
	struct mutex mutex;
	bool connection_active;
	bool exiting;
	struct work_struct svc_arrive_work;
	int num_tmds;
	struct qmi_tmd tmds[] __counted_by(num_tmds);
};

/* Notify the remote subsystem of the requested cooling state */
static int qmi_tmd_send_state_request(struct qmi_tmd *tmd, int state)
{
	struct tmd_set_level_resp resp = { 0 };
	struct tmd_set_level_req req = { 0 };
	struct qmi_tmd_client *qmi_tmd_cli = tmd->qmi_tmd_cli;
	struct qmi_txn txn;
	int ret = 0;

	if (!qmi_tmd_cli->connection_active)
		return -ENOTCONN;

	strscpy(req.mitigation_dev_id.mitigation_dev_id, tmd->name,
		QMI_TMD_DEV_ID_LEN_MAX + 1);
	req.mitigation_level = state;

	ret = qmi_txn_init(&qmi_tmd_cli->handle, &txn,
			   tmd_set_level_resp_ei, &resp);
	if (ret < 0) {
		dev_err(qmi_tmd_cli->dev, "qmi set state %d txn init failed for %s ret %d\n",
			state, tmd->name, ret);
		return ret;
	}

	ret = qmi_send_request(&qmi_tmd_cli->handle, NULL, &txn,
			       QMI_TMD_SET_LEVEL_REQ,
			       TMD_SET_LEVEL_REQ_MAX_LEN,
			       tmd_set_level_req_ei, &req);
	if (ret < 0) {
		dev_err(qmi_tmd_cli->dev, "qmi set state %d txn send failed for %s ret %d\n",
			state, tmd->name, ret);
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, QMI_TMD_RESP_TIMEOUT);
	if (ret < 0) {
		dev_err(qmi_tmd_cli->dev, "qmi set state %d txn wait failed for %s ret %d\n",
			state, tmd->name, ret);
		return ret;
	}

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		dev_err(qmi_tmd_cli->dev,
			"qmi set state %d failed for %s result %#x error %#x\n",
			state, tmd->name,
			resp.resp.result, resp.resp.error);
		return -EREMOTEIO;
	}

	dev_dbg(qmi_tmd_cli->dev, "Requested state %d/%d for %s\n", state,
		tmd->max_state, tmd->name);

	tmd->cur_state = state;

	return 0;
}

static int qmi_tmd_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct qmi_tmd *tmd = cdev->devdata;

	*state = tmd->max_state;

	return 0;
}

static int qmi_tmd_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct qmi_tmd *tmd = cdev->devdata;

	/* cur_state is protected by thermal core's cdev->lock */
	*state = tmd->cur_state;

	return 0;
}

static int qmi_tmd_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct qmi_tmd *tmd = cdev->devdata;

	if (state > tmd->max_state)
		return -EINVAL;

	/* cur_state is protected by thermal core's cdev->lock */
	if (tmd->cur_state == state)
		return 0;

	guard(mutex)(&tmd->qmi_tmd_cli->mutex);

	return qmi_tmd_send_state_request(tmd, state);
}

static const struct thermal_cooling_device_ops qmi_tmd_cooling_ops = {
	.get_max_state = qmi_tmd_get_max_state,
	.get_cur_state = qmi_tmd_get_cur_state,
	.set_cur_state = qmi_tmd_set_cur_state,
};

static int qmi_tmd_register(struct qmi_tmd_client *qmi_tmd_cli,
			    const char *label, u8 max_state)
{
	struct device *dev = qmi_tmd_cli->dev;
	struct qmi_tmd *tmd;
	int index;

	for (index = 0; index < qmi_tmd_cli->num_tmds; index++) {
		tmd = &qmi_tmd_cli->tmds[index];

		if (!strncasecmp(tmd->name, label,
				 QMI_TMD_DEV_ID_LEN_MAX + 1))
			goto found;
	}

	dev_dbg(qmi_tmd_cli->dev,
		"TMD '%s' available in firmware but not specified in DT\n",
		label);
	return 0;

found:
	tmd->max_state = max_state;

	/*
	 * If the cooling device already exists then the QMI service went away and
	 * came back. So just make sure the current cooling device state is
	 * reflected on the remote side and then return.
	 */
	if (tmd->cdev) {
		guard(mutex)(&qmi_tmd_cli->mutex);

		return qmi_tmd_send_state_request(tmd, tmd->cur_state);
	}

	tmd->cdev = thermal_of_cooling_device_register(dev->of_node, index,
						       label, tmd, &qmi_tmd_cooling_ops);
	if (IS_ERR(tmd->cdev)) {
		int ret = PTR_ERR(tmd->cdev);

		tmd->cdev = NULL;
		return ret;
	}

	return 0;
}

static void qmi_tmd_unregister(struct qmi_tmd_client *qmi_tmd_cli)
{
	struct qmi_tmd *tmd;
	int index;

	for (index = 0; index < qmi_tmd_cli->num_tmds; index++) {
		tmd = &qmi_tmd_cli->tmds[index];

		if (!tmd->cdev)
			continue;

		thermal_cooling_device_unregister(tmd->cdev);
		tmd->cdev = NULL;
	}
}

static void qmi_tmd_svc_arrive(struct work_struct *work)
{
	struct qmi_tmd_client *qmi_tmd_cli =
		container_of(work, struct qmi_tmd_client, svc_arrive_work);

	struct tmd_get_dev_list_req req = { 0 };
	struct tmd_get_dev_list_resp *resp __free(kfree) = NULL;
	int ret, i;
	struct qmi_txn txn;

	resp = kzalloc_obj(*resp, GFP_KERNEL);
	if (!resp) {
		ret = -ENOMEM;
		goto out;
	}

	scoped_guard(mutex, &qmi_tmd_cli->mutex) {
		if (qmi_tmd_cli->exiting)
			return;

		ret = qmi_txn_init(&qmi_tmd_cli->handle, &txn,
				   tmd_get_dev_list_resp_ei, resp);
		if (ret < 0)
			goto out;

		ret = qmi_send_request(&qmi_tmd_cli->handle, NULL, &txn,
				       QMI_TMD_GET_DEV_LIST_REQ,
				       TMD_GET_DEV_LIST_REQ_MAX_LEN,
				       tmd_get_dev_list_req_ei, &req);
		if (ret < 0) {
			qmi_txn_cancel(&txn);
			goto out;
		}

		ret = qmi_txn_wait(&txn, QMI_TMD_RESP_TIMEOUT);
		if (ret < 0)
			goto out;

		if (resp->resp.result != QMI_RESULT_SUCCESS_V01) {
			ret = -EPROTO;
			goto out;
		}

		qmi_tmd_cli->connection_active = true;
	}

	for (i = 0; i < resp->mitigation_device_list_len; i++) {
		struct tmd_dev_list *device =
			&resp->mitigation_device_list[i];

		ret = qmi_tmd_register(qmi_tmd_cli,
				       device->mitigation_dev_id.mitigation_dev_id,
				       device->max_mitigation_level);
		if (ret)
			break;
	}

out:
	if (ret)
		dev_err(qmi_tmd_cli->dev, "Failed to initialize TMD service: %d\n", ret);
}

static void qmi_tmd_del_server(struct qmi_handle *qmi, struct qmi_service *service)
{
	struct qmi_tmd_client *qmi_tmd_cli =
		container_of(qmi, struct qmi_tmd_client, handle);

	scoped_guard(mutex, &qmi_tmd_cli->mutex) {
		qmi_tmd_cli->connection_active = false;
	}
}

static int qmi_tmd_new_server(struct qmi_handle *qmi, struct qmi_service *service)
{
	struct sockaddr_qrtr sq = { AF_QIPCRTR, service->node, service->port };
	struct qmi_tmd_client *qmi_tmd_cli;
	int ret;

	qmi_tmd_cli = container_of(qmi, struct qmi_tmd_client, handle);

	scoped_guard(mutex, &qmi_tmd_cli->mutex) {
		if (qmi_tmd_cli->exiting)
			return 0;

		ret = kernel_connect(qmi->sock, (struct sockaddr_unsized *)&sq,
				     sizeof(sq), 0);
	}

	if (ret < 0) {
		dev_err(qmi_tmd_cli->dev, "QMI connect failed for node %u port %u: %d\n",
			service->node, service->port, ret);
		return ret;
	}

	queue_work(system_highpri_wq, &qmi_tmd_cli->svc_arrive_work);

	return 0;
}

static const struct qmi_ops qmi_tmd_ops = {
	.new_server = qmi_tmd_new_server,
	.del_server = qmi_tmd_del_server,
};

/**
 * qmi_tmd_init() - Initialize QMI TMD instance
 * @dev: Device pointer
 * @instance_id: QMI service instance ID for the remote subsystem
 * @tmd_names: Array of TMD names
 * @num_tmds: Number of TMD names
 *
 * Context: Must be called from probe context.
 *
 * Return: Pointer to qmi_tmd_client on success, ERR_PTR on failure
 */
struct qmi_tmd_client *qmi_tmd_init(struct device *dev,
				    unsigned int instance_id,
				    const char * const *tmd_names,
				    int num_tmds)
{
	struct qmi_tmd_client *qmi_tmd_cli;
	int ret, i;

	if (!dev || !tmd_names || num_tmds <= 0)
		return ERR_PTR(-EINVAL);

	qmi_tmd_cli = devm_kzalloc(dev, struct_size(qmi_tmd_cli, tmds, num_tmds), GFP_KERNEL);
	if (!qmi_tmd_cli)
		return ERR_PTR(-ENOMEM);

	qmi_tmd_cli->dev = dev;
	qmi_tmd_cli->num_tmds = num_tmds;
	mutex_init(&qmi_tmd_cli->mutex);
	INIT_WORK(&qmi_tmd_cli->svc_arrive_work, qmi_tmd_svc_arrive);

	for (i = 0; i < num_tmds; i++) {
		qmi_tmd_cli->tmds[i].name = tmd_names[i];
		qmi_tmd_cli->tmds[i].qmi_tmd_cli = qmi_tmd_cli;
	}

	ret = qmi_handle_init(&qmi_tmd_cli->handle,
			      TMD_GET_DEV_LIST_RESP_MAX_LEN,
			      &qmi_tmd_ops, NULL);
	if (ret < 0)
		return ERR_PTR(dev_err_probe(dev, ret, "QMI handle init failed\n"));

	ret = qmi_add_lookup(&qmi_tmd_cli->handle, QMI_SERVICE_ID_TMD,
			     QMI_TMD_SERVICE_VERS_V01, instance_id);
	if (ret < 0) {
		dev_err_probe(dev, ret, "QMI add lookup failed\n");
		goto err_release_handle;
	}

	return qmi_tmd_cli;

err_release_handle:
	qmi_handle_release(&qmi_tmd_cli->handle);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(qmi_tmd_init);

/**
 * qmi_tmd_exit() - Deinitialize QMI TMD instance
 * @qmi_tmd_cli: QMI TMD client to deinitialize
 */
void qmi_tmd_exit(struct qmi_tmd_client *qmi_tmd_cli)
{
	if (!qmi_tmd_cli)
		return;

	scoped_guard(mutex, &qmi_tmd_cli->mutex) {
		qmi_tmd_cli->exiting = true;
		qmi_tmd_cli->connection_active = false;
	}

	qmi_handle_release(&qmi_tmd_cli->handle);
	cancel_work_sync(&qmi_tmd_cli->svc_arrive_work);
	qmi_tmd_unregister(qmi_tmd_cli);
}
EXPORT_SYMBOL_GPL(qmi_tmd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm QMI Thermal Mitigation support");
