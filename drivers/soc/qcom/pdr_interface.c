// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 The Linux Foundation. All rights reserved.
 */

#include <linux/cleanup.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "pdr_internal.h"

struct pdr_service {
	char service_name[SERVREG_NAME_LENGTH + 1];
	char service_path[SERVREG_NAME_LENGTH + 1];

	struct sockaddr_qrtr addr;

	unsigned int instance;
	unsigned int service;
	u8 service_data_valid;
	u32 service_data;
	int state;

	bool need_notifier_register;
	bool need_notifier_remove;
	bool need_locator_lookup;
	bool service_connected;

	struct list_head node;
};

struct pdr_handle {
	struct qmi_handle locator_hdl;
	struct qmi_handle notifier_hdl;

	struct sockaddr_qrtr locator_addr;

	struct list_head lookups;
	struct list_head indack_list;

	/* control access to pdr lookup/indack lists */
	struct mutex list_lock;

	/* serialize pd status invocation */
	struct mutex status_lock;

	/* control access to the locator state */
	struct mutex lock;

	bool locator_init_complete;

	struct work_struct locator_work;
	struct work_struct notifier_work;
	struct work_struct indack_work;

	struct workqueue_struct *notifier_wq;
	struct workqueue_struct *indack_wq;

	void (*status)(int state, char *service_path, void *priv);
	void *priv;
};

struct pdr_list_node {
	enum servreg_service_state curr_state;
	u16 transaction_id;
	struct pdr_service *pds;
	struct list_head node;
};

static int pdr_locator_new_server(struct qmi_handle *qmi,
				  struct qmi_service *svc)
{
	struct pdr_handle *pdr = container_of(qmi, struct pdr_handle,
					      locator_hdl);
	struct pdr_service *pds;

	mutex_lock(&pdr->lock);
	/* Create a local client port for QMI communication */
	pdr->locator_addr.sq_family = AF_QIPCRTR;
	pdr->locator_addr.sq_node = svc->node;
	pdr->locator_addr.sq_port = svc->port;

	pdr->locator_init_complete = true;
	mutex_unlock(&pdr->lock);

	/* Service pending lookup requests */
	mutex_lock(&pdr->list_lock);
	list_for_each_entry(pds, &pdr->lookups, node) {
		if (pds->need_locator_lookup)
			schedule_work(&pdr->locator_work);
	}
	mutex_unlock(&pdr->list_lock);

	return 0;
}

static void pdr_locator_del_server(struct qmi_handle *qmi,
				   struct qmi_service *svc)
{
	struct pdr_handle *pdr = container_of(qmi, struct pdr_handle,
					      locator_hdl);

	mutex_lock(&pdr->lock);
	pdr->locator_init_complete = false;

	pdr->locator_addr.sq_node = 0;
	pdr->locator_addr.sq_port = 0;
	mutex_unlock(&pdr->lock);
}

static const struct qmi_ops pdr_locator_ops = {
	.new_server = pdr_locator_new_server,
	.del_server = pdr_locator_del_server,
};

static int pdr_register_listener(struct pdr_handle *pdr,
				 struct pdr_service *pds,
				 bool enable)
{
	struct servreg_register_listener_resp resp;
	struct servreg_register_listener_req req;
	struct qmi_txn txn;
	int ret;

	ret = qmi_txn_init(&pdr->notifier_hdl, &txn,
			   servreg_register_listener_resp_ei,
			   &resp);
	if (ret < 0)
		return ret;

	req.enable = enable;
	strscpy(req.service_path, pds->service_path, sizeof(req.service_path));

	ret = qmi_send_request(&pdr->notifier_hdl, &pds->addr,
			       &txn, SERVREG_REGISTER_LISTENER_REQ,
			       SERVREG_REGISTER_LISTENER_REQ_LEN,
			       servreg_register_listener_req_ei,
			       &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0) {
		pr_err("PDR: %s register listener txn wait failed: %d\n",
		       pds->service_path, ret);
		return ret;
	}

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		pr_err("PDR: %s register listener failed: 0x%x\n",
		       pds->service_path, resp.resp.error);
		return -EREMOTEIO;
	}

	pds->state = resp.curr_state;

	return 0;
}

static void pdr_notifier_work(struct work_struct *work)
{
	struct pdr_handle *pdr = container_of(work, struct pdr_handle,
					      notifier_work);
	struct pdr_service *pds;
	int ret;

	mutex_lock(&pdr->list_lock);
	list_for_each_entry(pds, &pdr->lookups, node) {
		if (pds->service_connected) {
			if (!pds->need_notifier_register)
				continue;

			pds->need_notifier_register = false;
			ret = pdr_register_listener(pdr, pds, true);
			if (ret < 0)
				pds->state = SERVREG_SERVICE_STATE_DOWN;
		} else {
			if (!pds->need_notifier_remove)
				continue;

			pds->need_notifier_remove = false;
			pds->state = SERVREG_SERVICE_STATE_DOWN;
		}

		mutex_lock(&pdr->status_lock);
		pdr->status(pds->state, pds->service_path, pdr->priv);
		mutex_unlock(&pdr->status_lock);
	}
	mutex_unlock(&pdr->list_lock);
}

static int pdr_notifier_new_server(struct qmi_handle *qmi,
				   struct qmi_service *svc)
{
	struct pdr_handle *pdr = container_of(qmi, struct pdr_handle,
					      notifier_hdl);
	struct pdr_service *pds;

	mutex_lock(&pdr->list_lock);
	list_for_each_entry(pds, &pdr->lookups, node) {
		if (pds->service == svc->service &&
		    pds->instance == svc->instance) {
			pds->service_connected = true;
			pds->need_notifier_register = true;
			pds->addr.sq_family = AF_QIPCRTR;
			pds->addr.sq_node = svc->node;
			pds->addr.sq_port = svc->port;
			queue_work(pdr->notifier_wq, &pdr->notifier_work);
		}
	}
	mutex_unlock(&pdr->list_lock);

	return 0;
}

static void pdr_notifier_del_server(struct qmi_handle *qmi,
				    struct qmi_service *svc)
{
	struct pdr_handle *pdr = container_of(qmi, struct pdr_handle,
					      notifier_hdl);
	struct pdr_service *pds;

	mutex_lock(&pdr->list_lock);
	list_for_each_entry(pds, &pdr->lookups, node) {
		if (pds->service == svc->service &&
		    pds->instance == svc->instance) {
			pds->service_connected = false;
			pds->need_notifier_remove = true;
			pds->addr.sq_node = 0;
			pds->addr.sq_port = 0;
			queue_work(pdr->notifier_wq, &pdr->notifier_work);
		}
	}
	mutex_unlock(&pdr->list_lock);
}

static const struct qmi_ops pdr_notifier_ops = {
	.new_server = pdr_notifier_new_server,
	.del_server = pdr_notifier_del_server,
};

static int pdr_send_indack_msg(struct pdr_handle *pdr, struct pdr_service *pds,
			       u16 tid)
{
	struct servreg_set_ack_resp resp;
	struct servreg_set_ack_req req;
	struct qmi_txn txn;
	int ret;

	ret = qmi_txn_init(&pdr->notifier_hdl, &txn, servreg_set_ack_resp_ei,
			   &resp);
	if (ret < 0)
		return ret;

	req.transaction_id = tid;
	strscpy(req.service_path, pds->service_path, sizeof(req.service_path));

	ret = qmi_send_request(&pdr->notifier_hdl, &pds->addr,
			       &txn, SERVREG_SET_ACK_REQ,
			       SERVREG_SET_ACK_REQ_LEN,
			       servreg_set_ack_req_ei,
			       &req);

	/* Skip waiting for response */
	qmi_txn_cancel(&txn);
	return ret;
}

static void pdr_indack_work(struct work_struct *work)
{
	struct pdr_handle *pdr = container_of(work, struct pdr_handle,
					      indack_work);
	struct pdr_list_node *ind, *tmp;
	struct pdr_service *pds;

	list_for_each_entry_safe(ind, tmp, &pdr->indack_list, node) {
		pds = ind->pds;

		mutex_lock(&pdr->status_lock);
		pds->state = ind->curr_state;
		pdr->status(pds->state, pds->service_path, pdr->priv);
		mutex_unlock(&pdr->status_lock);

		/* Ack the indication after clients release the PD resources */
		pdr_send_indack_msg(pdr, pds, ind->transaction_id);

		mutex_lock(&pdr->list_lock);
		list_del(&ind->node);
		mutex_unlock(&pdr->list_lock);

		kfree(ind);
	}
}

static void pdr_indication_cb(struct qmi_handle *qmi,
			      struct sockaddr_qrtr *sq,
			      struct qmi_txn *txn, const void *data)
{
	struct pdr_handle *pdr = container_of(qmi, struct pdr_handle,
					      notifier_hdl);
	const struct servreg_state_updated_ind *ind_msg = data;
	struct pdr_list_node *ind;
	struct pdr_service *pds = NULL, *iter;

	if (!ind_msg || !ind_msg->service_path[0] ||
	    strlen(ind_msg->service_path) > SERVREG_NAME_LENGTH)
		return;

	mutex_lock(&pdr->list_lock);
	list_for_each_entry(iter, &pdr->lookups, node) {
		if (strcmp(iter->service_path, ind_msg->service_path))
			continue;

		pds = iter;
		break;
	}
	mutex_unlock(&pdr->list_lock);

	if (!pds)
		return;

	pr_info("PDR: Indication received from %s, state: 0x%x, trans-id: %d\n",
		ind_msg->service_path, ind_msg->curr_state,
		ind_msg->transaction_id);

	ind = kzalloc(sizeof(*ind), GFP_KERNEL);
	if (!ind)
		return;

	ind->transaction_id = ind_msg->transaction_id;
	ind->curr_state = ind_msg->curr_state;
	ind->pds = pds;

	mutex_lock(&pdr->list_lock);
	list_add_tail(&ind->node, &pdr->indack_list);
	mutex_unlock(&pdr->list_lock);

	queue_work(pdr->indack_wq, &pdr->indack_work);
}

static const struct qmi_msg_handler qmi_indication_handler[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = SERVREG_STATE_UPDATED_IND_ID,
		.ei = servreg_state_updated_ind_ei,
		.decoded_size = sizeof(struct servreg_state_updated_ind),
		.fn = pdr_indication_cb,
	},
	{}
};

static int pdr_get_domain_list(struct servreg_get_domain_list_req *req,
			       struct servreg_get_domain_list_resp *resp,
			       struct pdr_handle *pdr)
{
	struct qmi_txn txn;
	int ret;

	ret = qmi_txn_init(&pdr->locator_hdl, &txn,
			   servreg_get_domain_list_resp_ei, resp);
	if (ret < 0)
		return ret;

	mutex_lock(&pdr->lock);
	ret = qmi_send_request(&pdr->locator_hdl,
			       &pdr->locator_addr,
			       &txn, SERVREG_GET_DOMAIN_LIST_REQ,
			       SERVREG_GET_DOMAIN_LIST_REQ_MAX_LEN,
			       servreg_get_domain_list_req_ei,
			       req);
	mutex_unlock(&pdr->lock);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0) {
		pr_err("PDR: %s get domain list txn wait failed: %d\n",
		       req->service_name, ret);
		return ret;
	}

	if (resp->resp.result != QMI_RESULT_SUCCESS_V01) {
		pr_err("PDR: %s get domain list failed: 0x%x\n",
		       req->service_name, resp->resp.error);
		return -EREMOTEIO;
	}

	return 0;
}

static int pdr_locate_service(struct pdr_handle *pdr, struct pdr_service *pds)
{
	struct servreg_get_domain_list_req req;
	struct servreg_location_entry *entry;
	int domains_read = 0;
	int ret, i;

	struct servreg_get_domain_list_resp *resp __free(kfree) = kzalloc(sizeof(*resp),
									  GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	/* Prepare req message */
	strscpy(req.service_name, pds->service_name, sizeof(req.service_name));
	req.domain_offset_valid = true;
	req.domain_offset = 0;

	do {
		req.domain_offset = domains_read;
		ret = pdr_get_domain_list(&req, resp, pdr);
		if (ret < 0)
			return ret;

		for (i = 0; i < resp->domain_list_len; i++) {
			entry = &resp->domain_list[i];

			if (strnlen(entry->name, sizeof(entry->name)) == sizeof(entry->name))
				continue;

			if (!strcmp(entry->name, pds->service_path)) {
				pds->service_data_valid = entry->service_data_valid;
				pds->service_data = entry->service_data;
				pds->instance = entry->instance;
				return 0;
			}
		}

		/* Update ret to indicate that the service is not yet found */
		ret = -ENXIO;

		/* Always read total_domains from the response msg */
		if (resp->domain_list_len > resp->total_domains)
			resp->domain_list_len = resp->total_domains;

		domains_read += resp->domain_list_len;
	} while (domains_read < resp->total_domains);

	return ret;
}

static void pdr_notify_lookup_failure(struct pdr_handle *pdr,
				      struct pdr_service *pds,
				      int err)
{
	pr_err("PDR: service lookup for %s failed: %d\n",
	       pds->service_name, err);

	if (err == -ENXIO)
		return;

	list_del(&pds->node);
	pds->state = SERVREG_LOCATOR_ERR;
	mutex_lock(&pdr->status_lock);
	pdr->status(pds->state, pds->service_path, pdr->priv);
	mutex_unlock(&pdr->status_lock);
	kfree(pds);
}

static void pdr_locator_work(struct work_struct *work)
{
	struct pdr_handle *pdr = container_of(work, struct pdr_handle,
					      locator_work);
	struct pdr_service *pds, *tmp;
	int ret = 0;

	/* Bail out early if the SERVREG LOCATOR QMI service is not up */
	mutex_lock(&pdr->lock);
	if (!pdr->locator_init_complete) {
		mutex_unlock(&pdr->lock);
		pr_debug("PDR: SERVICE LOCATOR service not available\n");
		return;
	}
	mutex_unlock(&pdr->lock);

	mutex_lock(&pdr->list_lock);
	list_for_each_entry_safe(pds, tmp, &pdr->lookups, node) {
		if (!pds->need_locator_lookup)
			continue;

		ret = pdr_locate_service(pdr, pds);
		if (ret < 0) {
			pdr_notify_lookup_failure(pdr, pds, ret);
			continue;
		}

		ret = qmi_add_lookup(&pdr->notifier_hdl, pds->service, 1,
				     pds->instance);
		if (ret < 0) {
			pdr_notify_lookup_failure(pdr, pds, ret);
			continue;
		}

		pds->need_locator_lookup = false;
	}
	mutex_unlock(&pdr->list_lock);
}

/**
 * pdr_add_lookup() - register a tracking request for a PD
 * @pdr:		PDR client handle
 * @service_name:	service name of the tracking request
 * @service_path:	service path of the tracking request
 *
 * Registering a pdr lookup allows for tracking the life cycle of the PD.
 *
 * Return: pdr_service object on success, ERR_PTR on failure. -EALREADY is
 * returned if a lookup is already in progress for the given service path.
 */
struct pdr_service *pdr_add_lookup(struct pdr_handle *pdr,
				   const char *service_name,
				   const char *service_path)
{
	struct pdr_service *tmp;

	if (IS_ERR_OR_NULL(pdr))
		return ERR_PTR(-EINVAL);

	if (!service_name || strlen(service_name) > SERVREG_NAME_LENGTH ||
	    !service_path || strlen(service_path) > SERVREG_NAME_LENGTH)
		return ERR_PTR(-EINVAL);

	struct pdr_service *pds __free(kfree) = kzalloc(sizeof(*pds), GFP_KERNEL);
	if (!pds)
		return ERR_PTR(-ENOMEM);

	pds->service = SERVREG_NOTIFIER_SERVICE;
	strscpy(pds->service_name, service_name, sizeof(pds->service_name));
	strscpy(pds->service_path, service_path, sizeof(pds->service_path));
	pds->need_locator_lookup = true;

	mutex_lock(&pdr->list_lock);
	list_for_each_entry(tmp, &pdr->lookups, node) {
		if (strcmp(tmp->service_path, service_path))
			continue;

		mutex_unlock(&pdr->list_lock);
		return ERR_PTR(-EALREADY);
	}

	list_add(&pds->node, &pdr->lookups);
	mutex_unlock(&pdr->list_lock);

	schedule_work(&pdr->locator_work);

	return_ptr(pds);
}
EXPORT_SYMBOL_GPL(pdr_add_lookup);

/**
 * pdr_restart_pd() - restart PD
 * @pdr:	PDR client handle
 * @pds:	PD service handle
 *
 * Restarts the PD tracked by the PDR client handle for a given service path.
 *
 * Return: 0 on success, negative errno on failure.
 */
int pdr_restart_pd(struct pdr_handle *pdr, struct pdr_service *pds)
{
	struct servreg_restart_pd_resp resp;
	struct servreg_restart_pd_req req = { 0 };
	struct sockaddr_qrtr addr;
	struct pdr_service *tmp;
	struct qmi_txn txn;
	int ret;

	if (IS_ERR_OR_NULL(pdr) || IS_ERR_OR_NULL(pds))
		return -EINVAL;

	mutex_lock(&pdr->list_lock);
	list_for_each_entry(tmp, &pdr->lookups, node) {
		if (tmp != pds)
			continue;

		if (!pds->service_connected)
			break;

		/* Prepare req message */
		strscpy(req.service_path, pds->service_path, sizeof(req.service_path));
		addr = pds->addr;
		break;
	}
	mutex_unlock(&pdr->list_lock);

	if (!req.service_path[0])
		return -EINVAL;

	ret = qmi_txn_init(&pdr->notifier_hdl, &txn,
			   servreg_restart_pd_resp_ei,
			   &resp);
	if (ret < 0)
		return ret;

	ret = qmi_send_request(&pdr->notifier_hdl, &addr,
			       &txn, SERVREG_RESTART_PD_REQ,
			       SERVREG_RESTART_PD_REQ_MAX_LEN,
			       servreg_restart_pd_req_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0) {
		pr_err("PDR: %s PD restart txn wait failed: %d\n",
		       req.service_path, ret);
		return ret;
	}

	/* Check response if PDR is disabled */
	if (resp.resp.result == QMI_RESULT_FAILURE_V01 &&
	    resp.resp.error == QMI_ERR_DISABLED_V01) {
		pr_err("PDR: %s PD restart is disabled: 0x%x\n",
		       req.service_path, resp.resp.error);
		return -EOPNOTSUPP;
	}

	/* Check the response for other error case*/
	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		pr_err("PDR: %s request for PD restart failed: 0x%x\n",
		       req.service_path, resp.resp.error);
		return -EREMOTEIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pdr_restart_pd);

/**
 * pdr_handle_alloc() - initialize the PDR client handle
 * @status:	function to be called on PD state change
 * @priv:	handle for client's use
 *
 * Initializes the PDR client handle to allow for tracking/restart of PDs.
 *
 * Return: pdr_handle object on success, ERR_PTR on failure.
 */
struct pdr_handle *pdr_handle_alloc(void (*status)(int state,
						   char *service_path,
						   void *priv), void *priv)
{
	int ret;

	if (!status)
		return ERR_PTR(-EINVAL);

	struct pdr_handle *pdr __free(kfree) = kzalloc(sizeof(*pdr), GFP_KERNEL);
	if (!pdr)
		return ERR_PTR(-ENOMEM);

	pdr->status = status;
	pdr->priv = priv;

	mutex_init(&pdr->status_lock);
	mutex_init(&pdr->list_lock);
	mutex_init(&pdr->lock);

	INIT_LIST_HEAD(&pdr->lookups);
	INIT_LIST_HEAD(&pdr->indack_list);

	INIT_WORK(&pdr->locator_work, pdr_locator_work);
	INIT_WORK(&pdr->notifier_work, pdr_notifier_work);
	INIT_WORK(&pdr->indack_work, pdr_indack_work);

	pdr->notifier_wq = create_singlethread_workqueue("pdr_notifier_wq");
	if (!pdr->notifier_wq)
		return ERR_PTR(-ENOMEM);

	pdr->indack_wq = alloc_ordered_workqueue("pdr_indack_wq", WQ_HIGHPRI);
	if (!pdr->indack_wq) {
		ret = -ENOMEM;
		goto destroy_notifier;
	}

	ret = qmi_handle_init(&pdr->locator_hdl,
			      SERVREG_GET_DOMAIN_LIST_RESP_MAX_LEN,
			      &pdr_locator_ops, NULL);
	if (ret < 0)
		goto destroy_indack;

	ret = qmi_add_lookup(&pdr->locator_hdl, SERVREG_LOCATOR_SERVICE, 1, 1);
	if (ret < 0)
		goto release_qmi_handle;

	ret = qmi_handle_init(&pdr->notifier_hdl,
			      SERVREG_STATE_UPDATED_IND_MAX_LEN,
			      &pdr_notifier_ops,
			      qmi_indication_handler);
	if (ret < 0)
		goto release_qmi_handle;

	return_ptr(pdr);

release_qmi_handle:
	qmi_handle_release(&pdr->locator_hdl);
destroy_indack:
	destroy_workqueue(pdr->indack_wq);
destroy_notifier:
	destroy_workqueue(pdr->notifier_wq);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(pdr_handle_alloc);

/**
 * pdr_handle_release() - release the PDR client handle
 * @pdr:	PDR client handle
 *
 * Cleans up pending tracking requests and releases the underlying qmi handles.
 */
void pdr_handle_release(struct pdr_handle *pdr)
{
	struct pdr_service *pds, *tmp;

	if (IS_ERR_OR_NULL(pdr))
		return;

	mutex_lock(&pdr->list_lock);
	list_for_each_entry_safe(pds, tmp, &pdr->lookups, node) {
		list_del(&pds->node);
		kfree(pds);
	}
	mutex_unlock(&pdr->list_lock);

	cancel_work_sync(&pdr->locator_work);
	cancel_work_sync(&pdr->notifier_work);
	cancel_work_sync(&pdr->indack_work);

	destroy_workqueue(pdr->notifier_wq);
	destroy_workqueue(pdr->indack_wq);

	qmi_handle_release(&pdr->locator_hdl);
	qmi_handle_release(&pdr->notifier_hdl);

	kfree(pdr);
}
EXPORT_SYMBOL_GPL(pdr_handle_release);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm Protection Domain Restart helpers");
