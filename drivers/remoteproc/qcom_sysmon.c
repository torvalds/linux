// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017, Linaro Ltd.
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
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/rpmsg.h>

#include "qcom_common.h"

static BLOCKING_NOTIFIER_HEAD(sysmon_notifiers);

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

	struct notifier_block nb;

	struct device *dev;

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

enum {
	SSCTL_SSR_EVENT_BEFORE_POWERUP,
	SSCTL_SSR_EVENT_AFTER_POWERUP,
	SSCTL_SSR_EVENT_BEFORE_SHUTDOWN,
	SSCTL_SSR_EVENT_AFTER_SHUTDOWN,
};

static const char * const sysmon_state_string[] = {
	[SSCTL_SSR_EVENT_BEFORE_POWERUP]	= "before_powerup",
	[SSCTL_SSR_EVENT_AFTER_POWERUP]		= "after_powerup",
	[SSCTL_SSR_EVENT_BEFORE_SHUTDOWN]	= "before_shutdown",
	[SSCTL_SSR_EVENT_AFTER_SHUTDOWN]	= "after_shutdown",
};

struct sysmon_event {
	const char *subsys_name;
	u32 ssr_event;
};

static DEFINE_MUTEX(sysmon_lock);
static LIST_HEAD(sysmon_list);

/**
 * sysmon_send_event() - send notification of other remote's SSR event
 * @sysmon:	sysmon context
 * @event:	sysmon event context
 */
static void sysmon_send_event(struct qcom_sysmon *sysmon,
			      const struct sysmon_event *event)
{
	char req[50];
	int len;
	int ret;

	len = snprintf(req, sizeof(req), "ssr:%s:%s", event->subsys_name,
		       sysmon_state_string[event->ssr_event]);
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

#define SSCTL_MAX_MSG_LEN		7

#define SSCTL_SUBSYS_NAME_LENGTH	15

enum {
	SSCTL_SSR_EVENT_FORCED,
	SSCTL_SSR_EVENT_GRACEFUL,
};

struct ssctl_shutdown_resp {
	struct qmi_response_type_v01 resp;
};

static const struct qmi_elem_info ssctl_shutdown_resp_ei[] = {
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

struct ssctl_subsys_event_req {
	u8 subsys_name_len;
	char subsys_name[SSCTL_SUBSYS_NAME_LENGTH];
	u32 event;
	u8 evt_driven_valid;
	u32 evt_driven;
};

static const struct qmi_elem_info ssctl_subsys_event_req_ei[] = {
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(uint8_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct ssctl_subsys_event_req,
					   subsys_name_len),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= SSCTL_SUBSYS_NAME_LENGTH,
		.elem_size	= sizeof(char),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct ssctl_subsys_event_req,
					   subsys_name),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(uint32_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ssctl_subsys_event_req,
					   event),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(uint8_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ssctl_subsys_event_req,
					   evt_driven_valid),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(uint32_t),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ssctl_subsys_event_req,
					   evt_driven),
		.ei_array	= NULL,
	},
	{}
};

struct ssctl_subsys_event_resp {
	struct qmi_response_type_v01 resp;
};

static const struct qmi_elem_info ssctl_subsys_event_resp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ssctl_subsys_event_resp,
					   resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{}
};

static const struct qmi_elem_info ssctl_shutdown_ind_ei[] = {
	{}
};

static void sysmon_ind_cb(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			  struct qmi_txn *txn, const void *data)
{
	struct qcom_sysmon *sysmon = container_of(qmi, struct qcom_sysmon, qmi);

	complete(&sysmon->ind_comp);
}

static const struct qmi_msg_handler qmi_indication_handler[] = {
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
		dev_err(sysmon->dev, "timeout waiting for shutdown response\n");
	} else if (resp.resp.result) {
		dev_err(sysmon->dev, "shutdown request rejected\n");
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
static void ssctl_send_event(struct qcom_sysmon *sysmon,
			     const struct sysmon_event *event)
{
	struct ssctl_subsys_event_resp resp;
	struct ssctl_subsys_event_req req;
	struct qmi_txn txn;
	int ret;

	memset(&resp, 0, sizeof(resp));
	ret = qmi_txn_init(&sysmon->qmi, &txn, ssctl_subsys_event_resp_ei, &resp);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed to allocate QMI txn\n");
		return;
	}

	memset(&req, 0, sizeof(req));
	strlcpy(req.subsys_name, event->subsys_name, sizeof(req.subsys_name));
	req.subsys_name_len = strlen(req.subsys_name);
	req.event = event->ssr_event;
	req.evt_driven_valid = true;
	req.evt_driven = SSCTL_SSR_EVENT_FORCED;

	ret = qmi_send_request(&sysmon->qmi, &sysmon->ssctl, &txn,
			       SSCTL_SUBSYS_EVENT_REQ, 40,
			       ssctl_subsys_event_req_ei, &req);
	if (ret < 0) {
		dev_err(sysmon->dev, "failed to send subsystem event\n");
		qmi_txn_cancel(&txn);
		return;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0)
		dev_err(sysmon->dev, "timeout waiting for subsystem event response\n");
	else if (resp.resp.result)
		dev_err(sysmon->dev, "subsystem event rejected\n");
	else
		dev_dbg(sysmon->dev, "subsystem event accepted\n");
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

static int sysmon_prepare(struct rproc_subdev *subdev)
{
	struct qcom_sysmon *sysmon = container_of(subdev, struct qcom_sysmon,
						  subdev);
	struct sysmon_event event = {
		.subsys_name = sysmon->name,
		.ssr_event = SSCTL_SSR_EVENT_BEFORE_POWERUP
	};

	mutex_lock(&sysmon->state_lock);
	sysmon->state = SSCTL_SSR_EVENT_BEFORE_POWERUP;
	blocking_notifier_call_chain(&sysmon_notifiers, 0, (void *)&event);
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
	struct sysmon_event event = {
		.subsys_name = sysmon->name,
		.ssr_event = SSCTL_SSR_EVENT_AFTER_POWERUP
	};

	reinit_completion(&sysmon->ssctl_comp);
	mutex_lock(&sysmon->state_lock);
	sysmon->state = SSCTL_SSR_EVENT_AFTER_POWERUP;
	blocking_notifier_call_chain(&sysmon_notifiers, 0, (void *)&event);
	mutex_unlock(&sysmon->state_lock);

	mutex_lock(&sysmon_lock);
	list_for_each_entry(target, &sysmon_list, node) {
		mutex_lock(&target->state_lock);
		if (target == sysmon || target->state != SSCTL_SSR_EVENT_AFTER_POWERUP) {
			mutex_unlock(&target->state_lock);
			continue;
		}

		event.subsys_name = target->name;
		event.ssr_event = target->state;

		if (sysmon->ssctl_version == 2)
			ssctl_send_event(sysmon, &event);
		else if (sysmon->ept)
			sysmon_send_event(sysmon, &event);
		mutex_unlock(&target->state_lock);
	}
	mutex_unlock(&sysmon_lock);

	return 0;
}

static void sysmon_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct qcom_sysmon *sysmon = container_of(subdev, struct qcom_sysmon, subdev);
	struct sysmon_event event = {
		.subsys_name = sysmon->name,
		.ssr_event = SSCTL_SSR_EVENT_BEFORE_SHUTDOWN
	};

	sysmon->shutdown_acked = false;

	mutex_lock(&sysmon->state_lock);
	sysmon->state = SSCTL_SSR_EVENT_BEFORE_SHUTDOWN;
	blocking_notifier_call_chain(&sysmon_notifiers, 0, (void *)&event);
	mutex_unlock(&sysmon->state_lock);

	/* Don't request graceful shutdown if we've crashed */
	if (crashed)
		return;

	if (sysmon->ssctl_instance) {
		if (!wait_for_completion_timeout(&sysmon->ssctl_comp, HZ / 2))
			dev_err(sysmon->dev, "timeout waiting for ssctl service\n");
	}

	if (sysmon->ssctl_version)
		sysmon->shutdown_acked = ssctl_request_shutdown(sysmon);
	else if (sysmon->ept)
		sysmon->shutdown_acked = sysmon_request_shutdown(sysmon);
}

static void sysmon_unprepare(struct rproc_subdev *subdev)
{
	struct qcom_sysmon *sysmon = container_of(subdev, struct qcom_sysmon,
						  subdev);
	struct sysmon_event event = {
		.subsys_name = sysmon->name,
		.ssr_event = SSCTL_SSR_EVENT_AFTER_SHUTDOWN
	};

	mutex_lock(&sysmon->state_lock);
	sysmon->state = SSCTL_SSR_EVENT_AFTER_SHUTDOWN;
	blocking_notifier_call_chain(&sysmon_notifiers, 0, (void *)&event);
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
	struct sysmon_event *sysmon_event = data;

	/* Skip non-running rprocs and the originating instance */
	if (sysmon->state != SSCTL_SSR_EVENT_AFTER_POWERUP ||
	    !strcmp(sysmon_event->subsys_name, sysmon->name)) {
		dev_dbg(sysmon->dev, "not notifying %s\n", sysmon->name);
		return NOTIFY_DONE;
	}

	/* Only SSCTL version 2 supports SSR events */
	if (sysmon->ssctl_version == 2)
		ssctl_send_event(sysmon, sysmon_event);
	else if (sysmon->ept)
		sysmon_send_event(sysmon, sysmon_event);

	return NOTIFY_DONE;
}

static irqreturn_t sysmon_shutdown_interrupt(int irq, void *data)
{
	struct qcom_sysmon *sysmon = data;

	complete(&sysmon->shutdown_comp);

	return IRQ_HANDLED;
}

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
	mutex_init(&sysmon->lock);
	mutex_init(&sysmon->state_lock);

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
