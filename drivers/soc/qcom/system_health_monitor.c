// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, 2016 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/ipc_logging.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/uaccess.h>

#include <linux/soc/qcom/qmi.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_rproc.h>

#include "system_health_monitor_v01.h"

#define MODULE_NAME "system_health_monitor"

#define SUBSYS_NAME_LEN 256
#define SSRESTART_STRLEN 256

enum {
	SHM_INFO_FLAG = 0x1,
	SHM_DEBUG_FLAG = 0x2,
};
static int shm_debug_mask = SHM_INFO_FLAG;
module_param_named(debug_mask, shm_debug_mask,
		   int, 0664);
static int shm_default_timeout_ms = 5000;
module_param_named(default_timeout_ms, shm_default_timeout_ms,
		   int, 0664);

#define DEFAULT_SHM_RATELIMIT_INTERVAL (HZ / 5)
#define DEFAULT_SHM_RATELIMIT_BURST 2

#define SHM_ILCTXT_NUM_PAGES 2
static void *shm_ilctxt;
#define SHM_INFO_LOG(x...) do { \
	if ((shm_debug_mask & SHM_INFO_FLAG)) \
		ipc_log_string(shm_ilctxt, x); \
} while (0)

#define SHM_DEBUG(x...) do { \
	if ((shm_debug_mask & SHM_DEBUG_FLAG)) \
		ipc_log_string(shm_ilctxt, x); \
} while (0)

#define SHM_ERR(x...) do { \
	ipc_log_string(shm_ilctxt, x); \
	pr_err(x); \
} while (0)

struct class *system_health_monitor_classp;
static dev_t system_health_monitor_dev;
static struct cdev system_health_monitor_cdev;
static struct device *system_health_monitor_devp;

#define SYSTEM_HEALTH_MONITOR_IOCTL_MAGIC (0xC3)

#define CHECK_SYSTEM_HEALTH_IOCTL \
	_IOR(SYSTEM_HEALTH_MONITOR_IOCTL_MAGIC, 0, unsigned int)

static struct workqueue_struct *shm_svc_workqueue;
static struct qmi_handle *shm_svc_handle;

enum {
	SHM_STATE_DEFAULT = 0,
	SHM_STATE_CHECKING,
};

struct restart_work {
	struct delayed_work dwork;
	struct hma_info *hmap;
	bool connected;
	u32 sq_node;
	u32 sq_port;
};

/**
 * struct hma_info - Information about a Health Monitor Agent(HMA)
 * @list:		List to chain up the hma to the hma_list.
 * @subsys_name:	Name of the remote subsystem that hosts this HMA.
 * @ssrestart_string:	Remote subsystem restart string.
 * @connected:		Connect state of the HMA.
 * @sq:			Destination sockaddr of QMI client.
 * @timeout:		Timeout as registered by the HMA.
 * @check_state:	The state of shm check.
 * @is_in_reset:	Flag to identify if the remote subsystem is in reset.
 * @restart_nb:		Notifier block to receive subsystem restart events.
 * @restart_nb_h:	Handle to subsystem restart notifier block.
 * @rs:			Rate-limit the health check.
 * @rproc:		Remoteproc phandler to the subsystem.
 * @rwp:		The work_struct to handle shm check fail.
 */
struct hma_info {
	struct list_head list;
	char subsys_name[SUBSYS_NAME_LEN];
	char ssrestart_string[SSRESTART_STRLEN];
	bool connected;
	struct sockaddr_qrtr sq;
	uint32_t timeout;
	atomic_t check_state;
	atomic_t is_in_reset;
	struct notifier_block restart_nb;
	void *restart_nb_h;
	struct ratelimit_state rs;
	struct rproc *rproc;
	struct restart_work rwp;
};

static void shm_svc_restart_worker(struct work_struct *work);

static DEFINE_MUTEX(hma_info_list_lock);
static LIST_HEAD(hma_info_list);

/**
 * restart_notifier_cb() - Callback to handle SSR events
 * @this:	Reference to the notifier block.
 * @code:	Type of SSR event.
 * @data:	Data that needs to be handled as part of SSR event.
 *
 * This function is used to identify if a subsystem which hosts an HMA
 * is already in reset, so that a duplicate subsystem restart is not
 * triggered.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int restart_notifier_cb(struct notifier_block *this,
			       unsigned long code, void *data)
{
	struct hma_info *tmp_hma_info =
		container_of(this, struct hma_info, restart_nb);
	struct restart_work *rwp;

	if (code == QCOM_SSR_BEFORE_SHUTDOWN) {
		atomic_set(&tmp_hma_info->is_in_reset, 1);
		tmp_hma_info->connected = false;
		rwp = &tmp_hma_info->rwp;
		cancel_delayed_work(&rwp->dwork);
		SHM_INFO_LOG("%s: %s going to shutdown\n",
			 __func__, tmp_hma_info->ssrestart_string);
	} else if (code == QCOM_SSR_AFTER_POWERUP) {
		atomic_set(&tmp_hma_info->is_in_reset, 0);
		SHM_INFO_LOG("%s: %s powered up\n",
			 __func__, tmp_hma_info->ssrestart_string);
	}
	return 0;
}

/**
 * shm_svc_restart_worker() - Worker to restart a subsystem
 * @work:	Reference to the work item being handled.
 *
 * This function restarts the subsystem which hosts an HMA. This function
 * checks the following before triggering a restart:
 * 1) Health check report is not received.
 * 2) The subsystem has not undergone a reset.
 * 3) The subsystem is not undergoing a reset.
 */
static void shm_svc_restart_worker(struct work_struct *work)
{
	int rc;
	struct delayed_work *dwork = to_delayed_work(work);
	struct restart_work *rwp =
		container_of(dwork, struct restart_work, dwork);
	struct hma_info *tmp_hma_info = rwp->hmap;

	if (atomic_read(&tmp_hma_info->check_state) != SHM_STATE_CHECKING) {
		SHM_INFO_LOG("%s: %s restart worker unexpected\n",
			 __func__, tmp_hma_info->subsys_name);
		return;
	}

	if (!tmp_hma_info->connected || (rwp->sq_node != tmp_hma_info->sq.sq_node ||
	    rwp->sq_port != tmp_hma_info->sq.sq_port)) {
		SHM_INFO_LOG(
			"%s: Connection to %s is reset. No further action\n",
			 __func__, tmp_hma_info->subsys_name);
		return;
	}

	if (atomic_read(&tmp_hma_info->is_in_reset)) {
		SHM_INFO_LOG(
			"%s: %s is going thru restart. No further action\n",
			 __func__, tmp_hma_info->subsys_name);
		return;
	}

	if (tmp_hma_info->rproc->recovery_disabled)
		panic("%s: HMA in %s failed to respond in time, panic!\n", __func__,
		      tmp_hma_info->subsys_name);

	SHM_ERR("%s: HMA in %s failed to respond in time. Restarting %s...\n",
		__func__, tmp_hma_info->subsys_name,
		tmp_hma_info->ssrestart_string);

	if (tmp_hma_info->rproc->state == RPROC_RUNNING ||
		    tmp_hma_info->rproc->state == RPROC_ATTACHED) {
		rproc_shutdown(tmp_hma_info->rproc);
		rc = rproc_boot(tmp_hma_info->rproc);
		if (rc < 0)
			SHM_ERR("%s: Error %d restarting %s\n",
				__func__, rc, tmp_hma_info->ssrestart_string);
	}
}

/**
 * shm_send_health_check_ind() - Initiate a subsystem health check
 * @tmp_hma_info:	Info about an HMA which resides in a subsystem.
 *
 * This functi on initiates a health check of a subsytem, which hosts the
 * HMA, by sending a health check QMI indication message.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int shm_send_health_check_ind(struct hma_info *tmp_hma_info)
{
	int rc;
	struct restart_work *rwp = &tmp_hma_info->rwp;

	if (!tmp_hma_info->connected || atomic_read(&tmp_hma_info->is_in_reset))
		return 0;

	/* Rate limit the health check as configured by the subsystem */
	if (!__ratelimit(&tmp_hma_info->rs))
		return 0;

	if (atomic_cmpxchg(&tmp_hma_info->check_state,
		SHM_STATE_DEFAULT, SHM_STATE_CHECKING)) {
		SHM_ERR("%s: Already checking\n", __func__);
		return 0;
	}

	INIT_DELAYED_WORK(&rwp->dwork, shm_svc_restart_worker);
	rwp->hmap = tmp_hma_info;
	rwp->connected = tmp_hma_info->connected;
	rwp->sq_node = tmp_hma_info->sq.sq_node;
	rwp->sq_port = tmp_hma_info->sq.sq_port;

	rc = qmi_send_indication(shm_svc_handle, &tmp_hma_info->sq,
				 QMI_HEALTH_MON_HEALTH_CHECK_IND_V01,
				 HMON_HEALTH_CHECK_IND_MSG_V01_MAX_MSG_LEN,
				 hmon_health_check_ind_msg_v01_ei, NULL);
	if (rc < 0) {
		SHM_ERR("%s: Send Error %d to %s\n",
			__func__, rc, tmp_hma_info->subsys_name);
		atomic_set(&tmp_hma_info->check_state, SHM_STATE_DEFAULT);
		return rc;
	}

	queue_delayed_work(shm_svc_workqueue, &rwp->dwork,
			   msecs_to_jiffies(tmp_hma_info->timeout));
	return 0;
}

/**
 * kern_check_system_health() - Check the system health
 *
 * This function is used by the kernel drivers to initiate the
 * system health check. This function in turn triggers SHM to send
 * QMI message to all the HMAs connected to it.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int kern_check_system_health(void)
{
	int rc;
	int final_rc = 0;
	struct hma_info *tmp_hma_info;

	mutex_lock(&hma_info_list_lock);
	list_for_each_entry(tmp_hma_info, &hma_info_list, list) {
		rc = shm_send_health_check_ind(tmp_hma_info);
		if (rc < 0) {
			SHM_ERR("%s by %s failed for %s - rc %d\n", __func__,
				current->comm, tmp_hma_info->subsys_name, rc);
			final_rc = rc;
		}
	}
	mutex_unlock(&hma_info_list_lock);
	return final_rc;
}
EXPORT_SYMBOL(kern_check_system_health);


static void shm_reg_req_handler(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
				struct qmi_txn *txn, const void *decoded)
{
	struct hmon_register_req_msg_v01 *req;
	struct hmon_register_resp_msg_v01 *resp;
	struct hma_info *tmp_hma_info;
	bool hma_info_found = false;
	int rc;

	SHM_INFO_LOG("%s, from [0x%x,0x%x]\n", __func__, sq->sq_node, sq->sq_port);
	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp) {
		SHM_ERR("%s: malloc for resp failed\n", __func__);
		return;
	}

	req = (struct hmon_register_req_msg_v01 *)decoded;
	if (!req->name_valid) {
		SHM_ERR("%s: host name invalid\n", __func__);
		goto send_reg_resp;
	}

	SHM_INFO_LOG("recv reg list: name_valid: %d, name: %s\n", req->name_valid, req->name);
	mutex_lock(&hma_info_list_lock);
	list_for_each_entry(tmp_hma_info, &hma_info_list, list) {
		if (!strcmp(tmp_hma_info->subsys_name, req->name) &&
		    !atomic_read(&tmp_hma_info->is_in_reset)) {
			if (tmp_hma_info->connected)
				SHM_ERR("%s: Duplicate HMA from %s-cur[0x%x:0x%x] new[0x%x:0x%x]\n",
					__func__, req->name, tmp_hma_info->sq.sq_node,
					tmp_hma_info->sq.sq_port, sq->sq_node, sq->sq_port);

			tmp_hma_info->connected = true;
			atomic_set(&tmp_hma_info->check_state, SHM_STATE_DEFAULT);
			memcpy(&tmp_hma_info->sq, sq, sizeof(*sq));
			if (req->timeout_valid)
				tmp_hma_info->timeout = req->timeout + shm_default_timeout_ms;
			else
				tmp_hma_info->timeout = shm_default_timeout_ms;
			ratelimit_state_init(&tmp_hma_info->rs,
					     DEFAULT_SHM_RATELIMIT_INTERVAL,
					     DEFAULT_SHM_RATELIMIT_BURST);
			SHM_INFO_LOG("%s: from %s timeout_ms %d [0x%x:0x%x]\n",
				     __func__, req->name, tmp_hma_info->timeout,
				     sq->sq_node, sq->sq_port);
			hma_info_found = true;
		}
	}
	mutex_unlock(&hma_info_list_lock);

send_reg_resp:
	if (hma_info_found) {
		memset(resp, 0, sizeof(*resp));
	} else {
		resp->resp.result = QMI_RESULT_FAILURE_V01;
		resp->resp.error = QMI_ERR_INVALID_ID_V01;
	}
	rc = qmi_send_response(shm_svc_handle, sq, txn,
			       QMI_HEALTH_MON_REG_RESP_V01,
			       HMON_REGISTER_RESP_MSG_V01_MAX_MSG_LEN,
			       hmon_register_resp_msg_v01_ei, resp);
	if (rc < 0)
		SHM_ERR("%s: send_resp failed to %s - rc %d\n",
			__func__, req->name, rc);
	else
		SHM_INFO_LOG("%s: send resp: %d\n", __func__, rc);
	kfree(resp);
}

static void shm_chk_comp_req_handler(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
				     struct qmi_txn *txn, const void *decoded)
{
	struct hmon_health_check_complete_req_msg_v01 *req;
	struct hmon_health_check_complete_resp_msg_v01 *resp;
	struct hma_info *tmp_hma_info;
	bool hma_info_found = false;
	int rc;
	struct restart_work *rwp;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp) {
		SHM_ERR("%s: malloc for resp failed\n", __func__);
		return;
	}

	req = (struct hmon_health_check_complete_req_msg_v01 *)decoded;
	if (!req->result_valid) {
		SHM_ERR("%s: Invalid result\n", __func__);
		goto send_resp;
	}

	mutex_lock(&hma_info_list_lock);
	list_for_each_entry(tmp_hma_info, &hma_info_list, list) {
		if (!tmp_hma_info->connected ||
		    tmp_hma_info->sq.sq_node != sq->sq_node ||
		    tmp_hma_info->sq.sq_port != sq->sq_port)
			continue;
		rwp = &tmp_hma_info->rwp;
		hma_info_found = true;
		if (req->result == HEALTH_MONITOR_CHECK_SUCCESS_V01) {
			if (atomic_cmpxchg(&tmp_hma_info->check_state,
				SHM_STATE_CHECKING, SHM_STATE_DEFAULT)) {
				SHM_INFO_LOG("%s: %s Health Check Success\n",
					__func__, tmp_hma_info->subsys_name);
				cancel_delayed_work_sync(&rwp->dwork);
			} else {
				SHM_INFO_LOG("%s: %s Health Check Unexpected.\n",
					 __func__, tmp_hma_info->subsys_name);
			}
		} else {
			SHM_INFO_LOG("%s: %s Health Check Failure\n",
				 __func__, tmp_hma_info->subsys_name);
		}
	}
	mutex_unlock(&hma_info_list_lock);

send_resp:
	if (hma_info_found) {
		memset(resp, 0, sizeof(*resp));
	} else {
		resp->resp.result = QMI_RESULT_FAILURE_V01;
		resp->resp.error = QMI_ERR_INVALID_ID_V01;
	}
	rc = qmi_send_response(shm_svc_handle, sq, txn,
			       QMI_HEALTH_MON_HEALTH_CHECK_COMPLETE_RESP_V01,
			       HMON_HEALTH_CHECK_COMPLETE_RESP_MSG_V01_MAX_MSG_LEN,
			       hmon_health_check_complete_resp_msg_v01_ei, resp);
	if (rc < 0)
		SHM_ERR("%s: send_resp failed - rc %d\n",
			__func__, rc);
	kfree(resp);
}

static void shm_svc_disconnect_cb(struct qmi_handle *qmi,
				  unsigned int node, unsigned int port)
{
	struct hma_info *tmp_hma_info;

	return;

	mutex_lock(&hma_info_list_lock);
	list_for_each_entry(tmp_hma_info, &hma_info_list, list) {
		if (tmp_hma_info->connected && tmp_hma_info->sq.sq_node == node &&
		    tmp_hma_info->sq.sq_port == port) {
			SHM_INFO_LOG("%s: connection[0x%x:0x%x] to HMA in %s exited\n",
				 __func__, node, port,
				 tmp_hma_info->subsys_name);
			tmp_hma_info->connected = false;
			break;
		}
	}
	mutex_unlock(&hma_info_list_lock);
}

static struct qmi_ops server_ops = {
	.del_client = shm_svc_disconnect_cb,
};

static struct qmi_msg_handler qmi_shm_handlers[] = {
	{
		.type = QMI_REQUEST,
		.msg_id = QMI_HEALTH_MON_REG_REQ_V01,
		.ei = hmon_register_req_msg_v01_ei,
		.decoded_size =
			sizeof(struct hmon_register_req_msg_v01),
		.fn = shm_reg_req_handler,
	},
	{
		.type = QMI_REQUEST,
		.msg_id = QMI_HEALTH_MON_HEALTH_CHECK_COMPLETE_REQ_V01,
		.ei = hmon_health_check_complete_req_msg_v01_ei,
		.decoded_size =
			sizeof(struct hmon_health_check_complete_req_msg_v01),
		.fn = shm_chk_comp_req_handler,
	},
	{}
};

static int system_health_monitor_open(struct inode *inode, struct file *file)
{
	SHM_DEBUG("%s by %s\n", __func__, current->comm);
	return 0;
}

static int system_health_monitor_release(struct inode *inode,
					  struct file *file)
{
	SHM_DEBUG("%s by %s\n", __func__, current->comm);
	return 0;
}

static ssize_t system_health_monitor_write(struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
	SHM_ERR("%s by %s\n", __func__, current->comm);
	return -EOPNOTSUPP;
}

static ssize_t system_health_monitor_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	SHM_ERR("%s by %s\n", __func__, current->comm);
	return -EOPNOTSUPP;
}

static long system_health_monitor_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	int rc = 0;

	switch (cmd) {
	case CHECK_SYSTEM_HEALTH_IOCTL:
		SHM_INFO_LOG("%s by %s\n", __func__, current->comm);
		rc = kern_check_system_health();
		break;
	default:
		SHM_ERR("%s: Invalid cmd %d by %s\n",
			__func__, cmd, current->comm);
		rc = -EINVAL;
	}
	return rc;
}

static const struct file_operations system_health_monitor_fops = {
	.owner = THIS_MODULE,
	.open = system_health_monitor_open,
	.release = system_health_monitor_release,
	.read = system_health_monitor_read,
	.write = system_health_monitor_write,
	.unlocked_ioctl = system_health_monitor_ioctl,
	.compat_ioctl = system_health_monitor_ioctl,
};

/**
 * start_system_health_monitor_service() - Start the SHM QMI service
 *
 * This function registers the SHM QMI service, if it is not already
 * registered.
 */
static int start_system_health_monitor_service(void)
{
	int rc;

	shm_svc_workqueue = create_singlethread_workqueue("shm_svc");
	if (!shm_svc_workqueue) {
		SHM_ERR("%s: Error creating workqueue\n", __func__);
		return -EFAULT;
	}

	shm_svc_handle = kzalloc(sizeof(struct qmi_handle), GFP_KERNEL);
	if (!shm_svc_handle) {
		SHM_ERR("%s: Error kzalloc for qmi handle\n", __func__);
		destroy_workqueue(shm_svc_workqueue);
		return -ENOMEM;
	}

	rc = qmi_handle_init(shm_svc_handle, 512/*sizeof(struct qmi_elem_info)*/,
			     &server_ops, qmi_shm_handlers);
	if (rc < 0) {
		SHM_ERR("%s: Create qmi handle failed: %d\n", __func__, rc);
		kfree(shm_svc_handle);
		destroy_workqueue(shm_svc_workqueue);
		return rc;
	}

	rc = qmi_add_server(shm_svc_handle, HMON_SERVICE_ID_V01,
			    HMON_SERVICE_VERS_V01, 0);
	if (rc < 0) {
		SHM_ERR("%s: qmi add server failed: %d\n", __func__, rc);
		qmi_handle_release(shm_svc_handle);
		kfree(shm_svc_handle);
		destroy_workqueue(shm_svc_workqueue);
		return rc;
	}

	SHM_INFO_LOG("%s: Successfully\n", __func__);

	return 0;
}

/**
 * parse_devicetree() - Parse the device tree for HMA information
 * @node:	Pointer to the device tree node.
 * @hma:	HMA information which needs to be extracted.
 *
 * This function parses the device tree, extracts the HMA information.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int parse_devicetree(struct device_node *node,
			    struct hma_info *hma)
{
	char *key;
	const char *subsys_name;
	const char *ssrestart_string;
	struct property *prop;
	int size;

	key = "qcom,subsys-name";
	subsys_name = of_get_property(node, key, NULL);
	if (!subsys_name)
		goto error;
	strscpy(hma->subsys_name, subsys_name, SUBSYS_NAME_LEN);

	key = "qcom,ssrestart-string";
	ssrestart_string = of_get_property(node, key, NULL);
	if (!ssrestart_string)
		goto error;
	strscpy(hma->ssrestart_string, ssrestart_string, SSRESTART_STRLEN);

	key = "qcom,rproc_phandle";
	prop = of_find_property(node, key, &size);
	if (!prop)
		goto error;

	hma->rproc = rproc_get_by_phandle(be32_to_cpup(prop->value));
	if (!hma->rproc) {
		SHM_ERR("%s: [%s]rproc phandle not ready\n",
			hma->subsys_name, __func__);
		return -EPROBE_DEFER;
	}

	return 0;
error:
	SHM_ERR("%s: missing key: %s\n", __func__, key);
	return -ENODEV;
}

/**
 * system_health_monitor_probe() - Probe function to construct HMA info
 * @pdev:	Platform device pointing to a device tree node.
 *
 * This function extracts the HMA information from the device tree, constructs
 * it and adds it to the global list.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int system_health_monitor_probe(struct platform_device *pdev)
{
	int rc;
	struct hma_info *hma, *tmp_hma;
	struct device_node *node;

	mutex_lock(&hma_info_list_lock);
	for_each_child_of_node(pdev->dev.of_node, node) {
		hma = kzalloc(sizeof(*hma), GFP_KERNEL);
		if (!hma) {
			SHM_ERR("%s: Error allocation hma_info\n", __func__);
			rc = -ENOMEM;
			goto probe_err;
		}

		rc = parse_devicetree(node, hma);
		if (rc) {
			SHM_ERR("%s Failed to parse Device Tree\n", __func__);
			kfree(hma);
			goto probe_err;
		}

		hma->restart_nb.notifier_call = restart_notifier_cb;
		hma->restart_nb_h = qcom_register_ssr_notifier(
				hma->ssrestart_string, &hma->restart_nb);
		if (IS_ERR_OR_NULL(hma->restart_nb_h)) {
			rc = -EFAULT;
			SHM_ERR("%s: Error registering restart notif for %s\n",
				__func__, hma->ssrestart_string);
			kfree(hma);
			goto probe_err;
		}

		list_add_tail(&hma->list, &hma_info_list);
		SHM_INFO_LOG("%s: Added HMA info for %s\n",
			 __func__, hma->subsys_name);
	}

	rc = start_system_health_monitor_service();
	if (rc) {
		SHM_ERR("%s Failed to start service %d\n", __func__, rc);
		goto probe_err;
	}
	mutex_unlock(&hma_info_list_lock);
	return 0;
probe_err:
	list_for_each_entry_safe(hma, tmp_hma, &hma_info_list, list) {
		list_del(&hma->list);
		qcom_unregister_ssr_notifier(hma->restart_nb_h,
					     &hma->restart_nb);
		kfree(hma);
	}
	mutex_unlock(&hma_info_list_lock);
	return rc;
}

static const struct of_device_id system_health_monitor_match_table[] = {
	{ .compatible = "qcom,system-health-monitor" },
	{},
};

static struct platform_driver system_health_monitor_driver = {
	.probe = system_health_monitor_probe,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = system_health_monitor_match_table,
	},
};

/**
 * system_health_monitor_init() - Initialize the system health monitor module
 *
 * This functions registers a platform driver to probe for and extract the HMA
 * information. This function registers the character device interface to the
 * user-space.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
static int __init system_health_monitor_init(void)
{
	int rc;

	shm_ilctxt = ipc_log_context_create(SHM_ILCTXT_NUM_PAGES, "shm", 0);
	if (!shm_ilctxt) {
		SHM_ERR("%s: Unable to create SHM logging context\n", __func__);
		shm_debug_mask = 0;
	}

	rc = platform_driver_register(&system_health_monitor_driver);
	if (rc) {
		SHM_ERR("%s: system_health_monitor_driver register failed %d\n",
			__func__, rc);
		return rc;
	}

	rc = alloc_chrdev_region(&system_health_monitor_dev,
				 0, 1, "system_health_monitor");
	if (rc < 0) {
		SHM_ERR("%s: alloc_chrdev_region() failed %d\n", __func__, rc);
		return rc;
	}

	system_health_monitor_classp = class_create(THIS_MODULE,
						"system_health_monitor");
	if (IS_ERR_OR_NULL(system_health_monitor_classp)) {
		SHM_ERR("%s: class_create() failed\n", __func__);
		rc = -ENOMEM;
		goto init_error1;
	}

	cdev_init(&system_health_monitor_cdev, &system_health_monitor_fops);
	system_health_monitor_cdev.owner = THIS_MODULE;
	rc = cdev_add(&system_health_monitor_cdev,
		      system_health_monitor_dev, 1);
	if (rc < 0) {
		SHM_ERR("%s: cdev_add() failed - rc %d\n",
			__func__, rc);
		goto init_error2;
	}

	system_health_monitor_devp = device_create(system_health_monitor_classp,
					NULL, system_health_monitor_dev, NULL,
					"system_health_monitor");
	if (IS_ERR_OR_NULL(system_health_monitor_devp)) {
		SHM_ERR("%s: device_create() failed - rc %d\n",
			__func__, rc);
		rc = PTR_ERR(system_health_monitor_devp);
		goto init_error3;
	}
	SHM_INFO_LOG("%s: Complete\n", __func__);
	return 0;
init_error3:
	cdev_del(&system_health_monitor_cdev);
init_error2:
	class_destroy(system_health_monitor_classp);
init_error1:
	unregister_chrdev_region(MAJOR(system_health_monitor_dev), 1);
	return rc;
}
module_init(system_health_monitor_init);

MODULE_DESCRIPTION("System Health Monitor");
MODULE_LICENSE("GPL");
