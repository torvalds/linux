// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017, Linaro Ltd.
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <trace/events/rproc_qcom.h>

#include "qcom_common.h"

#define SYSMON_NOTIF_TIMEOUT CONFIG_RPROC_SYSMON_NOTIF_TIMEOUT
#define SYSMON_SHUTDOWN_NOTIF_TIMEOUT CONFIG_RPROC_SYSMON_SHUTDOWN_NOTIF_TIMEOUT

#define SYSMON_SUBDEV_NAME "sysmon"

static const char * const notif_timeout_msg = "sysmon msg from %s to %s for %s taking too long";
static const char * const shutdown_timeout_msg = "sysmon_send_shutdown to %s taking too long";

static BLOCKING_NOTIFIER_HEAD(sysmon_notifiers);

struct qcom_sysmon;

struct notif_timeout_data {
	struct qcom_sysmon *dest;
	struct timer_list timer;
};

struct qcom_sysmon {
	struct rproc_subdev subdev;
	struct rproc *rproc;

	int state;
	struct mutex state_lock;

	struct list_head node;

	const char *name;

	int shutdown_irq;
	int ssctl_version;
	int ssctl_instance;

	struct notif_timeout_data timeout_data;
	struct notifier_block nb;

	struct device *dev;
	uint32_t transaction_id;

	struct rpmsg_endpoint *ept;
	struct completion comp;
	struct completion ind_comp;
	struct completion shutdown_comp;
	struct completion ssctl_comp;
	struct mutex lock;

	bool ssr_ack;
	bool shutdown_acked;

	struct qmi_handle qmi;
	struct sockaddr_qrtr ssctl;
};

static DEFINE_MUTEX(sysmon_lock);
static LIST_HEAD(sysmon_list);

uint32_t qcom_sysmon_get_txn_id(struct qcom_sysmon *sysmon)
{
	return sysmon->transaction_id;
}
EXPORT_SYMBOL(qcom_sysmon_get_txn_id);

/**
 * sysmon_send_event() - send notification of other remote's SSR event
 * @sysmon:	sysmon context
 * @event:	sysmon event context
 */
static void sysmon_send_event(struct qcom_sysmon *sysmon,
			      const struct qcom_sysmon *source)
{
	char req[50];
	int len;
	int ret;

	len = scnprintf(req, sizeof(req), "ssr:%s:%s", source->name,
		       subdevice_state_string[source->state]);
	if (len >= sizeof(req))
		return;

	mutex_lock(&sysmon->lock);
	reinit_completion(&sysmon->comp);
	sysmon->ssr_ack = false;

	ret = rpmsg_send(sysmon->ept, req, len);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed to send sysmon event\n");
		goto out_unlock;
	}

	ret = wait_for_completion_timeout(&sysmon->comp,
					  msecs_to_jiffies(5000));
	if (!ret) {
		dev_err(sysmon->dev, "timeout waiting for sysmon ack\n");
		goto out_unlock;
	}

	if (!sysmon->ssr_ack)
		dev_err(sysmon->dev, "unexpected response to sysmon event\n");

out_unlock:
	mutex_unlock(&sysmon->lock);
}

/**
 * sysmon_request_shutdown() - request graceful shutdown of remote
 * @sysmon:	sysmon context
 *
 * Return: boolean indicator of the remote processor acking the request
 */
static bool sysmon_request_shutdown(struct qcom_sysmon *sysmon)
{
	char *req = "ssr:shutdown";
	bool acked = false;
	int ret;

	mutex_lock(&sysmon->lock);
	reinit_completion(&sysmon->comp);
	sysmon->ssr_ack = false;

	ret = rpmsg_send(sysmon->ept, req, strlen(req) + 1);
	if (ret < 0) {
		dev_err(sysmon->dev, "send sysmon shutdown request failed\n");
		goto out_unlock;
	}

	ret = wait_for_completion_timeout(&sysmon->comp,
					  msecs_to_jiffies(5000));
	if (!ret) {
		dev_err(sysmon->dev, "timeout waiting for sysmon ack\n");
		goto out_unlock;
	}

	if (!sysmon->ssr_ack)
		dev_err(sysmon->dev,
			"unexpected response to sysmon shutdown request\n");
	else
		acked = true;

out_unlock:
	mutex_unlock(&sysmon->lock);

	return acked;
}

static int sysmon_callback(struct rpmsg_device *rpdev, void *data, int count,
			   void *priv, u32 addr)
{
	struct qcom_sysmon *sysmon = priv;
	const char *ssr_ack = "ssr:ack";
	const int ssr_ack_len = strlen(ssr_ack) + 1;

	if (!sysmon)
		return -EINVAL;

	if (count >= ssr_ack_len && !memcmp(data, ssr_ack, ssr_ack_len))
		sysmon->ssr_ack = true;

	complete(&sysmon->comp);

	return 0;
}

#define SSCTL_SHUTDOWN_REQ		0x21
#define SSCTL_SHUTDOWN_READY_IND	0x21
#define SSCTL_SUBSYS_EVENT_REQ		0x23
#define SSCTL_SUBSYS_EVENT_WITH_TID_REQ		0x25

#define SSCTL_MAX_MSG_LEN		7

#define SSCTL_SUBSYS_NAME_LENGTH	15

enum {
	SSCTL_SSR_EVENT_FORCED,
	SSCTL_SSR_EVENT_GRACEFUL,
};

struct ssctl_shutdown_resp {
	struct qmi_response_type_v01 resp;
};

static struct qmi_elem_info ssctl_shutdown_resp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ssctl_shutdown_resp, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{}
};

struct ssctl_subsys_event_with_tid_req {
	u8 subsys_name_len;
	char subsys_name[SSCTL_SUBSYS_NAME_LENGTH];
	u32 event;
	u8 evt_driven_valid;
	u32 evt_driven;
	u8 transaction_id_valid;
	uint32_t transaction_id;
};

static struct qmi_elem_info ssctl_subsys_event_with_tid_req_ei[] = {
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(uint8_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct ssctl_subsys_event_with_tid_req,
					   subsys_name_len),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= SSCTL_SUBSYS_NAME_LENGTH,
		.elem_size	= sizeof(char),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct ssctl_subsys_event_with_tid_req,
					   subsys_name),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(uint32_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ssctl_subsys_event_with_tid_req,
					   event),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(uint8_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x03,
		.offset		= offsetof(struct ssctl_subsys_event_with_tid_req,
					   transaction_id_valid),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(uint32_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x03,
		.offset		= offsetof(struct ssctl_subsys_event_with_tid_req,
					   transaction_id),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(uint8_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ssctl_subsys_event_with_tid_req,
					   evt_driven_valid),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(uint32_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ssctl_subsys_event_with_tid_req,
					   evt_driven),
		.ei_array	= NULL,
	},
	{}
};

struct ssctl_subsys_event_with_tid_resp {
	struct qmi_response_type_v01 resp;
};

static struct qmi_elem_info ssctl_subsys_event_with_tid_resp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ssctl_subsys_event_with_tid_resp,
					   resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{}
};


static struct qmi_elem_info ssctl_shutdown_ind_ei[] = {
	{}
};

static void sysmon_ind_cb(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			  struct qmi_txn *txn, const void *data)
{
	struct qcom_sysmon *sysmon = container_of(qmi, struct qcom_sysmon, qmi);

	complete(&sysmon->ind_comp);
}

static struct qmi_msg_handler qmi_indication_handler[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = SSCTL_SHUTDOWN_READY_IND,
		.ei = ssctl_shutdown_ind_ei,
		.decoded_size = 0,
		.fn = sysmon_ind_cb
	},
	{}
};

static bool ssctl_request_shutdown_wait(struct qcom_sysmon *sysmon)
{
	int ret;

	ret = wait_for_completion_timeout(&sysmon->shutdown_comp, 10 * HZ);
	if (ret)
		return true;

	ret = try_wait_for_completion(&sysmon->ind_comp);
	if (ret)
		return true;

	dev_err(sysmon->dev, "timeout waiting for shutdown ack\n");
	return false;
}

/**
 * ssctl_request_shutdown() - request shutdown via SSCTL QMI service
 * @sysmon:	sysmon context
 *
 * Return: boolean indicator of the remote processor acking the request
 */
static bool ssctl_request_shutdown(struct qcom_sysmon *sysmon)
{
	struct ssctl_shutdown_resp resp;
	struct qmi_txn txn;
	bool acked = false;
	int ret;

	if (sysmon->ssctl_instance == -EINVAL)
		return false;

	reinit_completion(&sysmon->ind_comp);
	reinit_completion(&sysmon->shutdown_comp);
	ret = qmi_txn_init(&sysmon->qmi, &txn, ssctl_shutdown_resp_ei, &resp);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed to allocate QMI txn\n");
		return false;
	}

	ret = qmi_send_request(&sysmon->qmi, &sysmon->ssctl, &txn,
			       SSCTL_SHUTDOWN_REQ, 0, NULL, NULL);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed to send shutdown request\n");
		qmi_txn_cancel(&txn);
		return false;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed receiving QMI response\n");
	} else if (resp.resp.result) {
		dev_err(sysmon->dev, "shutdown request failed\n");
	} else {
		dev_dbg(sysmon->dev, "shutdown request completed\n");
		acked = true;
	}

	if (sysmon->shutdown_irq > 0)
		return ssctl_request_shutdown_wait(sysmon);

	return acked;
}

/**
 * ssctl_send_event() - send notification of other remote's SSR event
 * @sysmon:	sysmon context
 * @event:	sysmon event context
 */
static int ssctl_send_event(struct qcom_sysmon *sysmon,
			     const struct qcom_sysmon *source, bool is_tid_valid)
{
	struct ssctl_subsys_event_with_tid_resp resp;
	struct ssctl_subsys_event_with_tid_req req;
	struct qmi_txn txn;
	int ret, ssctl_event;

	if (sysmon->ssctl_instance == -EINVAL)
		return -EINVAL;

	memset(&resp, 0, sizeof(resp));
	ret = qmi_txn_init(&sysmon->qmi, &txn, ssctl_subsys_event_with_tid_resp_ei, &resp);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed to allocate QMI txn\n");
		return ret;
	}

	memset(&req, 0, sizeof(req));
	strscpy(req.subsys_name, source->name, sizeof(req.subsys_name));
	req.subsys_name_len = strlen(req.subsys_name);
	req.event = source->state;
	req.evt_driven_valid = true;
	req.evt_driven = SSCTL_SSR_EVENT_FORCED;
	req.transaction_id_valid = is_tid_valid ? false : true;
	req.transaction_id = sysmon->transaction_id;
	ssctl_event = is_tid_valid ? SSCTL_SUBSYS_EVENT_REQ : SSCTL_SUBSYS_EVENT_WITH_TID_REQ;

	ret = qmi_send_request(&sysmon->qmi, &sysmon->ssctl, &txn,
			       ssctl_event, 40, ssctl_subsys_event_with_tid_req_ei, &req);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed to send shutdown request\n");
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed receiving QMI response\n");
		return ret;
	}

	if (resp.resp.result) {
		dev_err(sysmon->dev, "failed to receive %s ssr %s event. response result: %d error: %d\n",
			source->name, subdevice_state_string[source->state],
			resp.resp.result, resp.resp.error);
		return resp.resp.result;
	}

	dev_dbg(sysmon->dev, "ssr event send completed\n");

	return 0;
}

/**
 * ssctl_new_server() - QMI callback indicating a new service
 * @qmi:	QMI handle
 * @svc:	service information
 *
 * Return: 0 if we're interested in this service, -EINVAL otherwise.
 */
static int ssctl_new_server(struct qmi_handle *qmi, struct qmi_service *svc)
{
	struct qcom_sysmon *sysmon = container_of(qmi, struct qcom_sysmon, qmi);

	switch (svc->version) {
	case 1:
		if (svc->instance != 0)
			return -EINVAL;
		if (strcmp(sysmon->name, "modem"))
			return -EINVAL;
		break;
	case 2:
		if (svc->instance != sysmon->ssctl_instance)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	sysmon->ssctl_version = svc->version;

	sysmon->ssctl.sq_family = AF_QIPCRTR;
	sysmon->ssctl.sq_node = svc->node;
	sysmon->ssctl.sq_port = svc->port;

	svc->priv = sysmon;

	complete(&sysmon->ssctl_comp);

	return 0;
}

/**
 * ssctl_del_server() - QMI callback indicating that @svc is removed
 * @qmi:	QMI handle
 * @svc:	service information
 */
static void ssctl_del_server(struct qmi_handle *qmi, struct qmi_service *svc)
{
	struct qcom_sysmon *sysmon = svc->priv;

	sysmon->ssctl_version = 0;
}

static const struct qmi_ops ssctl_ops = {
	.new_server = ssctl_new_server,
	.del_server = ssctl_del_server,
};

static void sysmon_notif_timeout_handler(struct timer_list *t)
{
	struct notif_timeout_data *td = from_timer(td, t, timer);
	struct qcom_sysmon *sysmon = container_of(td, struct qcom_sysmon, timeout_data);

	if (IS_ENABLED(CONFIG_QCOM_PANIC_ON_NOTIF_TIMEOUT) &&
	    system_state != SYSTEM_RESTART &&
	    system_state != SYSTEM_POWER_OFF &&
	    system_state != SYSTEM_HALT &&
	    !qcom_device_shutdown_in_progress)
		panic(notif_timeout_msg, sysmon->name, td->dest->name,
		      subdevice_state_string[sysmon->state]);
	else
		WARN(1, notif_timeout_msg, sysmon->name, td->dest->name,
		     subdevice_state_string[sysmon->state]);
}

static void sysmon_shutdown_notif_timeout_handler(struct timer_list *t)
{
	struct notif_timeout_data *td = from_timer(td, t, timer);
	struct qcom_sysmon *sysmon = container_of(td, struct qcom_sysmon, timeout_data);

	WARN(1, shutdown_timeout_msg, sysmon->name);
}

static inline void send_event(struct qcom_sysmon *sysmon, struct qcom_sysmon *source)
{
	unsigned long timeout;
	int ret;

	source->timeout_data.timer.function = sysmon_notif_timeout_handler;
	source->timeout_data.dest = sysmon;
	timeout = jiffies + msecs_to_jiffies(SYSMON_NOTIF_TIMEOUT);
	mod_timer(&source->timeout_data.timer, timeout);

	/* Only SSCTL version 2 supports SSR events */
	if (sysmon->ssctl_version == 2) {
		ret = ssctl_send_event(sysmon, source, false);
		if (ret == QMI_RESULT_FAILURE_V01) {
			/* Retry with older ssctl event */
			dev_dbg(sysmon->dev, "Retrying with no trascation id request\n");
			ret = ssctl_send_event(sysmon, source, true);
		}
		/* if ret !=1 we don't retry */
		if (ret)
			pr_err("Failed to send event\n");
	}
	else if (sysmon->ept)
		sysmon_send_event(sysmon, source);

	del_timer_sync(&source->timeout_data.timer);
}

static int sysmon_prepare(struct rproc_subdev *subdev)
{
	struct qcom_sysmon *sysmon = container_of(subdev, struct qcom_sysmon,
						  subdev);

	trace_rproc_qcom_event(dev_name(sysmon->rproc->dev.parent), SYSMON_SUBDEV_NAME, "prepare");

	mutex_lock(&sysmon->state_lock);
	sysmon->state = QCOM_SSR_BEFORE_POWERUP;
	blocking_notifier_call_chain(&sysmon_notifiers, 0, (void *)sysmon);
	mutex_unlock(&sysmon->state_lock);

	return 0;
}

/**
 * sysmon_start() - start callback for the sysmon remoteproc subdevice
 * @subdev:	instance of the sysmon subdevice
 *
 * Inform all the listners of sysmon notifications that the rproc associated
 * to @subdev has booted up. The rproc that booted up also needs to know
 * which rprocs are already up and running, so send start notifications
 * on behalf of all the online rprocs.
 */
static int sysmon_start(struct rproc_subdev *subdev)
{
	struct qcom_sysmon *sysmon = container_of(subdev, struct qcom_sysmon,
						  subdev);
	struct qcom_sysmon *target;

	trace_rproc_qcom_event(dev_name(sysmon->rproc->dev.parent), SYSMON_SUBDEV_NAME, "start");

	reinit_completion(&sysmon->ssctl_comp);
	mutex_lock(&sysmon->state_lock);
	sysmon->state = QCOM_SSR_AFTER_POWERUP;
	blocking_notifier_call_chain(&sysmon_notifiers, 0, (void *)sysmon);
	mutex_unlock(&sysmon->state_lock);

	mutex_lock(&sysmon_lock);
	list_for_each_entry(target, &sysmon_list, node) {
		mutex_lock(&target->state_lock);

		if (target == sysmon || target->state != QCOM_SSR_AFTER_POWERUP) {
			mutex_unlock(&target->state_lock);
			continue;
		}

		send_event(sysmon, target);
		mutex_unlock(&target->state_lock);
	}
	mutex_unlock(&sysmon_lock);

	return 0;
}

static void sysmon_stop(struct rproc_subdev *subdev, bool crashed)
{
	unsigned long timeout;
	struct qcom_sysmon *sysmon = container_of(subdev, struct qcom_sysmon, subdev);

	trace_rproc_qcom_event(dev_name(sysmon->rproc->dev.parent), SYSMON_SUBDEV_NAME,
			       crashed ? "crash stop" : "stop");

	sysmon->shutdown_acked = false;

	mutex_lock(&sysmon->state_lock);
	sysmon->state = QCOM_SSR_BEFORE_SHUTDOWN;

	sysmon->transaction_id++;
	dev_info(sysmon->dev, "Incrementing tid for %s to %d\n", sysmon->name,
		 sysmon->transaction_id);

	blocking_notifier_call_chain(&sysmon_notifiers, 0, (void *)sysmon);
	mutex_unlock(&sysmon->state_lock);

	/* Don't request graceful shutdown if we've crashed */
	if (crashed)
		return;

	sysmon->timeout_data.timer.function = sysmon_shutdown_notif_timeout_handler;
	timeout = jiffies + msecs_to_jiffies(SYSMON_SHUTDOWN_NOTIF_TIMEOUT);

	if (sysmon->ssctl_instance) {
		if (!wait_for_completion_timeout(&sysmon->ssctl_comp, HZ / 2))
			dev_err(sysmon->dev, "timeout waiting for ssctl service\n");
	}

	mod_timer(&sysmon->timeout_data.timer, timeout);
	if (sysmon->ssctl_version)
		sysmon->shutdown_acked = ssctl_request_shutdown(sysmon);
	else if (sysmon->ept)
		sysmon->shutdown_acked = sysmon_request_shutdown(sysmon);

	del_timer_sync(&sysmon->timeout_data.timer);
}

static void sysmon_unprepare(struct rproc_subdev *subdev)
{
	struct qcom_sysmon *sysmon = container_of(subdev, struct qcom_sysmon,
						  subdev);

	trace_rproc_qcom_event(dev_name(sysmon->rproc->dev.parent), SYSMON_SUBDEV_NAME,
			       "unprepare");

	mutex_lock(&sysmon->state_lock);
	sysmon->state = QCOM_SSR_AFTER_SHUTDOWN;
	blocking_notifier_call_chain(&sysmon_notifiers, 0, (void *)sysmon);
	mutex_unlock(&sysmon->state_lock);
}

/**
 * sysmon_notify() - notify sysmon target of another's SSR
 * @nb:		notifier_block associated with sysmon instance
 * @event:	unused
 * @data:	SSR identifier of the remote that is going down
 */
static int sysmon_notify(struct notifier_block *nb, unsigned long event,
			 void *data)
{
	struct qcom_sysmon *sysmon = container_of(nb, struct qcom_sysmon, nb);
	struct qcom_sysmon *source = data;

	/* Skip non-running rprocs and the originating instance */
	if (sysmon->state != QCOM_SSR_AFTER_POWERUP ||
	    !strcmp(source->name, sysmon->name)) {
		dev_dbg(sysmon->dev, "not notifying %s\n", sysmon->name);
		return NOTIFY_DONE;
	}

	send_event(sysmon, source);

	return NOTIFY_DONE;
}

static irqreturn_t sysmon_shutdown_interrupt(int irq, void *data)
{
	struct qcom_sysmon *sysmon = data;

	complete(&sysmon->shutdown_comp);

	return IRQ_HANDLED;
}

#define QMI_SSCTL_GET_FAILURE_REASON_REQ	0x0022
#define QMI_SSCTL_EMPTY_MSG_LENGTH		0
#define QMI_SSCTL_ERROR_MSG_LENGTH		90
#define QMI_EOTI_DATA_TYPE	\
{				\
	.data_type = QMI_EOTI,	\
	.elem_len  = 0,		\
	.elem_size = 0,		\
	.array_type  = NO_ARRAY,\
	.tlv_type  = 0x00,	\
	.offset    = 0,		\
	.ei_array  = NULL,	\
},

struct qmi_ssctl_get_failure_reason_resp_msg {
	struct qmi_response_type_v01 resp;
	uint8_t error_message_valid;
	uint32_t error_message_len;
	char error_message[QMI_SSCTL_ERROR_MSG_LENGTH];
};

static struct qmi_elem_info qmi_ssctl_get_failure_reason_req_msg_ei[] = {
	QMI_EOTI_DATA_TYPE
};

static struct qmi_elem_info qmi_ssctl_get_failure_reason_resp_msg_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len  = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x02,
		.offset    = offsetof(
			struct qmi_ssctl_get_failure_reason_resp_msg,
							resp),
		.ei_array  = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len  = 1,
		.elem_size = sizeof(uint8_t),
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x10,
		.offset    = offsetof(
			struct qmi_ssctl_get_failure_reason_resp_msg,
						error_message_valid),
		.ei_array  = NULL,
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len  = 1,
		.elem_size = sizeof(uint8_t),
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x10,
		.offset    = offsetof(
			struct qmi_ssctl_get_failure_reason_resp_msg,
						error_message_len),
		.ei_array  = NULL,
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len  = QMI_SSCTL_ERROR_MSG_LENGTH,
		.elem_size = sizeof(char),
		.array_type  = VAR_LEN_ARRAY,
		.tlv_type  = 0x10,
		.offset    = offsetof(
			struct qmi_ssctl_get_failure_reason_resp_msg,
						error_message),
		.ei_array  = NULL,
	},
	QMI_EOTI_DATA_TYPE
};

/**
 * qcom_sysmon_get_reason() - Retrieve failure reason from a subsystem.
 * @dest_desc:	Subsystem descriptor of the subsystem to query
 * @buf:	Caller-allocated buffer for the returned NUL-terminated reason
 * @len:	Length of @buf
 *
 * Reverts to using legacy sysmon API (sysmon_get_reason_no_qmi()) if client
 * handle is not set.
 *
 * Returns 0 for success, -EINVAL for an invalid destination, -ENODEV if
 * the SMD transport channel is not open, -ETIMEDOUT if the destination
 * subsystem does not respond, and -EPROTO if the destination subsystem
 * responds with something unexpected.
 *
 */
int qcom_sysmon_get_reason(struct qcom_sysmon *sysmon, char *buf, size_t len)
{
	char req = 0;
	struct qmi_ssctl_get_failure_reason_resp_msg resp;
	struct qmi_txn txn;
	const char *dest_ss;
	int ret;

	if (sysmon == NULL || buf == NULL || len == 0)
		return -EINVAL;

	dest_ss = sysmon->name;

	ret = qmi_txn_init(&sysmon->qmi, &txn, qmi_ssctl_get_failure_reason_resp_msg_ei,
			   &resp);
	if (ret < 0) {
		pr_err("SYSMON QMI tx init failed to dest %s, ret - %d\n", dest_ss, ret);
		goto out;
	}

	ret = qmi_send_request(&sysmon->qmi, &sysmon->ssctl, &txn,
			       QMI_SSCTL_GET_FAILURE_REASON_REQ,
			       QMI_SSCTL_EMPTY_MSG_LENGTH,
			       qmi_ssctl_get_failure_reason_req_msg_ei,
			       &req);
	if (ret < 0) {
		pr_err("SYSMON QMI send req failed to dest %s, ret - %d\n", dest_ss, ret);
		qmi_txn_cancel(&txn);
		goto out;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0) {
		pr_err("SYSMON QMI qmi txn wait failed to dest %s, ret - %d\n", dest_ss, ret);
		goto out;
	} else if (resp.resp.result) {
		dev_err(sysmon->dev, "failed to receive req. response result: %d\n",
			resp.resp.result);
		goto out;
	}
	strscpy(buf, resp.error_message, len);
out:
	return ret;
}
EXPORT_SYMBOL(qcom_sysmon_get_reason);

/**
 * qcom_add_sysmon_subdev() - create a sysmon subdev for the given remoteproc
 * @rproc:	rproc context to associate the subdev with
 * @name:	name of this subdev, to use in SSR
 * @ssctl_instance: instance id of the ssctl QMI service
 *
 * Return: A new qcom_sysmon object, or NULL on failure
 */
struct qcom_sysmon *qcom_add_sysmon_subdev(struct rproc *rproc,
					   const char *name,
					   int ssctl_instance)
{
	struct qcom_sysmon *sysmon;
	int ret;

	sysmon = kzalloc(sizeof(*sysmon), GFP_KERNEL);
	if (!sysmon)
		return ERR_PTR(-ENOMEM);

	sysmon->dev = rproc->dev.parent;
	sysmon->rproc = rproc;

	sysmon->name = name;
	sysmon->ssctl_instance = ssctl_instance;

	init_completion(&sysmon->comp);
	init_completion(&sysmon->ind_comp);
	init_completion(&sysmon->shutdown_comp);
	init_completion(&sysmon->ssctl_comp);
	timer_setup(&sysmon->timeout_data.timer, sysmon_notif_timeout_handler, 0);
	mutex_init(&sysmon->lock);
	mutex_init(&sysmon->state_lock);

	if (sysmon->ssctl_instance == -EINVAL)
		goto add_subdev_callbacks;

	sysmon->shutdown_irq = of_irq_get_byname(sysmon->dev->of_node,
						 "shutdown-ack");
	if (sysmon->shutdown_irq < 0) {
		if (sysmon->shutdown_irq != -ENODATA) {
			dev_err(sysmon->dev,
				"failed to retrieve shutdown-ack IRQ\n");
			ret = sysmon->shutdown_irq;
			kfree(sysmon);
			return ERR_PTR(ret);
		}
	} else {
		ret = devm_request_threaded_irq(sysmon->dev,
						sysmon->shutdown_irq,
						NULL, sysmon_shutdown_interrupt,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						"q6v5 shutdown-ack", sysmon);
		if (ret) {
			dev_err(sysmon->dev,
				"failed to acquire shutdown-ack IRQ\n");
			kfree(sysmon);
			return ERR_PTR(ret);
		}
	}

	ret = qmi_handle_init(&sysmon->qmi, SSCTL_MAX_MSG_LEN, &ssctl_ops,
			      qmi_indication_handler);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed to initialize qmi handle\n");
		kfree(sysmon);
		return ERR_PTR(ret);
	}

	qmi_add_lookup(&sysmon->qmi, 43, 0, 0);

add_subdev_callbacks:
	sysmon->subdev.prepare = sysmon_prepare;
	sysmon->subdev.start = sysmon_start;
	sysmon->subdev.stop = sysmon_stop;
	sysmon->subdev.unprepare = sysmon_unprepare;

	rproc_add_subdev(rproc, &sysmon->subdev);

	sysmon->nb.notifier_call = sysmon_notify;
	blocking_notifier_chain_register(&sysmon_notifiers, &sysmon->nb);

	mutex_lock(&sysmon_lock);
	list_add(&sysmon->node, &sysmon_list);
	mutex_unlock(&sysmon_lock);

	return sysmon;
}
EXPORT_SYMBOL_GPL(qcom_add_sysmon_subdev);

/**
 * qcom_remove_sysmon_subdev() - release a qcom_sysmon
 * @sysmon:	sysmon context, as retrieved by qcom_add_sysmon_subdev()
 */
void qcom_remove_sysmon_subdev(struct qcom_sysmon *sysmon)
{
	if (!sysmon)
		return;

	mutex_lock(&sysmon_lock);
	list_del(&sysmon->node);
	mutex_unlock(&sysmon_lock);

	blocking_notifier_chain_unregister(&sysmon_notifiers, &sysmon->nb);

	rproc_remove_subdev(sysmon->rproc, &sysmon->subdev);

	if (sysmon->ssctl_instance != -EINVAL)
		qmi_handle_release(&sysmon->qmi);

	kfree(sysmon);
}
EXPORT_SYMBOL_GPL(qcom_remove_sysmon_subdev);

/**
 * qcom_sysmon_shutdown_acked() - query the success of the last shutdown
 * @sysmon:	sysmon context
 *
 * When sysmon is used to request a graceful shutdown of the remote processor
 * this can be used by the remoteproc driver to query the success, in order to
 * know if it should fall back to other means of requesting a shutdown.
 *
 * Return: boolean indicator of the success of the last shutdown request
 */
bool qcom_sysmon_shutdown_acked(struct qcom_sysmon *sysmon)
{
	return sysmon && sysmon->shutdown_acked;
}
EXPORT_SYMBOL_GPL(qcom_sysmon_shutdown_acked);

/**
 * sysmon_probe() - probe sys_mon channel
 * @rpdev:	rpmsg device handle
 *
 * Find the sysmon context associated with the ancestor remoteproc and assign
 * this rpmsg device with said sysmon context.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int sysmon_probe(struct rpmsg_device *rpdev)
{
	struct qcom_sysmon *sysmon;
	struct rproc *rproc;

	rproc = rproc_get_by_child(&rpdev->dev);
	if (!rproc) {
		dev_err(&rpdev->dev, "sysmon device not child of rproc\n");
		return -EINVAL;
	}

	mutex_lock(&sysmon_lock);
	list_for_each_entry(sysmon, &sysmon_list, node) {
		if (sysmon->rproc == rproc)
			goto found;
	}
	mutex_unlock(&sysmon_lock);

	dev_err(&rpdev->dev, "no sysmon associated with parent rproc\n");

	return -EINVAL;

found:
	mutex_unlock(&sysmon_lock);

	rpdev->ept->priv = sysmon;
	sysmon->ept = rpdev->ept;

	return 0;
}

/**
 * sysmon_remove() - sys_mon channel remove handler
 * @rpdev:	rpmsg device handle
 *
 * Disassociate the rpmsg device with the sysmon instance.
 */
static void sysmon_remove(struct rpmsg_device *rpdev)
{
	struct qcom_sysmon *sysmon = rpdev->ept->priv;

	sysmon->ept = NULL;
}

static const struct rpmsg_device_id sysmon_match[] = {
	{ "sys_mon" },
	{}
};

static struct rpmsg_driver sysmon_driver = {
	.probe = sysmon_probe,
	.remove = sysmon_remove,
	.callback = sysmon_callback,
	.id_table = sysmon_match,
	.drv = {
		.name = "qcom_sysmon",
	},
};

module_rpmsg_driver(sysmon_driver);

MODULE_DESCRIPTION("Qualcomm sysmon driver");
MODULE_LICENSE("GPL v2");
