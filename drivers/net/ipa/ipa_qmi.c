// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/qrtr.h>
#include <linux/soc/qcom/qmi.h>

#include "ipa.h"
#include "ipa_endpoint.h"
#include "ipa_mem.h"
#include "ipa_table.h"
#include "ipa_modem.h"
#include "ipa_qmi_msg.h"

/**
 * DOC: AP/Modem QMI Handshake
 *
 * The AP and modem perform a "handshake" at initialization time to ensure
 * both sides know when everything is ready to begin operating.  The AP
 * driver (this code) uses two QMI handles (endpoints) for this; a client
 * using a service on the modem, and server to service modem requests (and
 * to supply an indication message from the AP).  Once the handshake is
 * complete, the AP and modem may begin IPA operation.  This occurs
 * only when the AP IPA driver, modem IPA driver, and IPA microcontroller
 * are ready.
 *
 * The QMI service on the modem expects to receive an INIT_DRIVER request from
 * the AP, which contains parameters used by the modem during initialization.
 * The AP sends this request as soon as it is knows the modem side service
 * is available.  The modem responds to this request, and if this response
 * contains a success result, the AP knows the modem IPA driver is ready.
 *
 * The modem is responsible for loading firmware on the IPA microcontroller.
 * This occurs only during the initial modem boot.  The modem sends a
 * separate DRIVER_INIT_COMPLETE request to the AP to report that the
 * microcontroller is ready.  The AP may assume the microcontroller is
 * ready and remain so (even if the modem reboots) once it has received
 * and responded to this request.
 *
 * There is one final exchange involved in the handshake.  It is required
 * on the initial modem boot, but optional (but in practice does occur) on
 * subsequent boots.  The modem expects to receive a final INIT_COMPLETE
 * indication message from the AP when it is about to begin its normal
 * operation.  The AP will only send this message after it has received
 * and responded to an INDICATION_REGISTER request from the modem.
 *
 * So in summary:
 * - Whenever the AP learns the modem has booted and its IPA QMI service
 *   is available, it sends an INIT_DRIVER request to the modem.  The
 *   modem supplies a success response when it is ready to operate.
 * - On the initial boot, the modem sets up the IPA microcontroller, and
 *   sends a DRIVER_INIT_COMPLETE request to the AP when this is done.
 * - When the modem is ready to receive an INIT_COMPLETE indication from
 *   the AP, it sends an INDICATION_REGISTER request to the AP.
 * - On the initial modem boot, everything is ready when:
 *	- AP has received a success response from its INIT_DRIVER request
 *	- AP has responded to a DRIVER_INIT_COMPLETE request
 *	- AP has responded to an INDICATION_REGISTER request from the modem
 *	- AP has sent an INIT_COMPLETE indication to the modem
 * - On subsequent modem boots, everything is ready when:
 *	- AP has received a success response from its INIT_DRIVER request
 *	- AP has responded to a DRIVER_INIT_COMPLETE request
 * - The INDICATION_REGISTER request and INIT_COMPLETE indication are
 *   optional for non-initial modem boots, and have no bearing on the
 *   determination of when things are "ready"
 */

#define IPA_HOST_SERVICE_SVC_ID		0x31
#define IPA_HOST_SVC_VERS		1
#define IPA_HOST_SERVICE_INS_ID		1

#define IPA_MODEM_SERVICE_SVC_ID	0x31
#define IPA_MODEM_SERVICE_INS_ID	2
#define IPA_MODEM_SVC_VERS		1

#define QMI_INIT_DRIVER_TIMEOUT		60000	/* A minute in milliseconds */

/* Send an INIT_COMPLETE indication message to the modem */
static void ipa_server_init_complete(struct ipa_qmi *ipa_qmi)
{
	struct ipa *ipa = container_of(ipa_qmi, struct ipa, qmi);
	struct qmi_handle *qmi = &ipa_qmi->server_handle;
	struct sockaddr_qrtr *sq = &ipa_qmi->modem_sq;
	struct ipa_init_complete_ind ind = { };
	int ret;

	ind.status.result = QMI_RESULT_SUCCESS_V01;
	ind.status.error = QMI_ERR_NONE_V01;

	ret = qmi_send_indication(qmi, sq, IPA_QMI_INIT_COMPLETE,
				   IPA_QMI_INIT_COMPLETE_IND_SZ,
				   ipa_init_complete_ind_ei, &ind);
	if (ret)
		dev_err(&ipa->pdev->dev,
			"error %d sending init complete indication\n", ret);
	else
		ipa_qmi->indication_sent = true;
}

/* If requested (and not already sent) send the INIT_COMPLETE indication */
static void ipa_qmi_indication(struct ipa_qmi *ipa_qmi)
{
	if (!ipa_qmi->indication_requested)
		return;

	if (ipa_qmi->indication_sent)
		return;

	ipa_server_init_complete(ipa_qmi);
}

/* Determine whether everything is ready to start normal operation.
 * We know everything (else) is ready when we know the IPA driver on
 * the modem is ready, and the microcontroller is ready.
 *
 * When the modem boots (or reboots), the handshake sequence starts
 * with the AP sending the modem an INIT_DRIVER request.  Within
 * that request, the uc_loaded flag will be zero (false) for an
 * initial boot, non-zero (true) for a subsequent (SSR) boot.
 */
static void ipa_qmi_ready(struct ipa_qmi *ipa_qmi)
{
	struct ipa *ipa = container_of(ipa_qmi, struct ipa, qmi);
	int ret;

	/* We aren't ready until the modem and microcontroller are */
	if (!ipa_qmi->modem_ready || !ipa_qmi->uc_ready)
		return;

	/* Send the indication message if it was requested */
	ipa_qmi_indication(ipa_qmi);

	/* The initial boot requires us to send the indication. */
	if (ipa_qmi->initial_boot) {
		if (!ipa_qmi->indication_sent)
			return;

		/* The initial modem boot completed successfully */
		ipa_qmi->initial_boot = false;
	}

	/* We're ready.  Start up normal operation */
	ipa = container_of(ipa_qmi, struct ipa, qmi);
	ret = ipa_modem_start(ipa);
	if (ret)
		dev_err(&ipa->pdev->dev, "error %d starting modem\n", ret);
}

/* All QMI clients from the modem node are gone (modem shut down or crashed). */
static void ipa_server_bye(struct qmi_handle *qmi, unsigned int node)
{
	struct ipa_qmi *ipa_qmi;

	ipa_qmi = container_of(qmi, struct ipa_qmi, server_handle);

	/* The modem client and server go away at the same time */
	memset(&ipa_qmi->modem_sq, 0, sizeof(ipa_qmi->modem_sq));

	/* initial_boot doesn't change when modem reboots */
	/* uc_ready doesn't change when modem reboots */
	ipa_qmi->modem_ready = false;
	ipa_qmi->indication_requested = false;
	ipa_qmi->indication_sent = false;
}

static const struct qmi_ops ipa_server_ops = {
	.bye		= ipa_server_bye,
};

/* Callback function to handle an INDICATION_REGISTER request message from the
 * modem.  This informs the AP that the modem is now ready to receive the
 * INIT_COMPLETE indication message.
 */
static void ipa_server_indication_register(struct qmi_handle *qmi,
					   struct sockaddr_qrtr *sq,
					   struct qmi_txn *txn,
					   const void *decoded)
{
	struct ipa_indication_register_rsp rsp = { };
	struct ipa_qmi *ipa_qmi;
	struct ipa *ipa;
	int ret;

	ipa_qmi = container_of(qmi, struct ipa_qmi, server_handle);
	ipa = container_of(ipa_qmi, struct ipa, qmi);

	rsp.rsp.result = QMI_RESULT_SUCCESS_V01;
	rsp.rsp.error = QMI_ERR_NONE_V01;

	ret = qmi_send_response(qmi, sq, txn, IPA_QMI_INDICATION_REGISTER,
				IPA_QMI_INDICATION_REGISTER_RSP_SZ,
				ipa_indication_register_rsp_ei, &rsp);
	if (!ret) {
		ipa_qmi->indication_requested = true;
		ipa_qmi_ready(ipa_qmi);		/* We might be ready now */
	} else {
		dev_err(&ipa->pdev->dev,
			"error %d sending register indication response\n", ret);
	}
}

/* Respond to a DRIVER_INIT_COMPLETE request message from the modem. */
static void ipa_server_driver_init_complete(struct qmi_handle *qmi,
					    struct sockaddr_qrtr *sq,
					    struct qmi_txn *txn,
					    const void *decoded)
{
	struct ipa_driver_init_complete_rsp rsp = { };
	struct ipa_qmi *ipa_qmi;
	struct ipa *ipa;
	int ret;

	ipa_qmi = container_of(qmi, struct ipa_qmi, server_handle);
	ipa = container_of(ipa_qmi, struct ipa, qmi);

	rsp.rsp.result = QMI_RESULT_SUCCESS_V01;
	rsp.rsp.error = QMI_ERR_NONE_V01;

	ret = qmi_send_response(qmi, sq, txn, IPA_QMI_DRIVER_INIT_COMPLETE,
				IPA_QMI_DRIVER_INIT_COMPLETE_RSP_SZ,
				ipa_driver_init_complete_rsp_ei, &rsp);
	if (!ret) {
		ipa_qmi->uc_ready = true;
		ipa_qmi_ready(ipa_qmi);		/* We might be ready now */
	} else {
		dev_err(&ipa->pdev->dev,
			"error %d sending init complete response\n", ret);
	}
}

/* The server handles two request message types sent by the modem. */
static const struct qmi_msg_handler ipa_server_msg_handlers[] = {
	{
		.type		= QMI_REQUEST,
		.msg_id		= IPA_QMI_INDICATION_REGISTER,
		.ei		= ipa_indication_register_req_ei,
		.decoded_size	= IPA_QMI_INDICATION_REGISTER_REQ_SZ,
		.fn		= ipa_server_indication_register,
	},
	{
		.type		= QMI_REQUEST,
		.msg_id		= IPA_QMI_DRIVER_INIT_COMPLETE,
		.ei		= ipa_driver_init_complete_req_ei,
		.decoded_size	= IPA_QMI_DRIVER_INIT_COMPLETE_REQ_SZ,
		.fn		= ipa_server_driver_init_complete,
	},
	{ },
};

/* Handle an INIT_DRIVER response message from the modem. */
static void ipa_client_init_driver(struct qmi_handle *qmi,
				   struct sockaddr_qrtr *sq,
				   struct qmi_txn *txn, const void *decoded)
{
	txn->result = 0;	/* IPA_QMI_INIT_DRIVER request was successful */
	complete(&txn->completion);
}

/* The client handles one response message type sent by the modem. */
static const struct qmi_msg_handler ipa_client_msg_handlers[] = {
	{
		.type		= QMI_RESPONSE,
		.msg_id		= IPA_QMI_INIT_DRIVER,
		.ei		= ipa_init_modem_driver_rsp_ei,
		.decoded_size	= IPA_QMI_INIT_DRIVER_RSP_SZ,
		.fn		= ipa_client_init_driver,
	},
	{ },
};

/* Return a pointer to an init modem driver request structure, which contains
 * configuration parameters for the modem.  The modem may be started multiple
 * times, but generally these parameters don't change so we can reuse the
 * request structure once it's initialized.  The only exception is the
 * skip_uc_load field, which will be set only after the microcontroller has
 * reported it has completed its initialization.
 */
static const struct ipa_init_modem_driver_req *
init_modem_driver_req(struct ipa_qmi *ipa_qmi)
{
	struct ipa *ipa = container_of(ipa_qmi, struct ipa, qmi);
	static struct ipa_init_modem_driver_req req;
	const struct ipa_mem *mem;

	/* The microcontroller is initialized on the first boot */
	req.skip_uc_load_valid = 1;
	req.skip_uc_load = ipa->uc_loaded ? 1 : 0;

	/* We only have to initialize most of it once */
	if (req.platform_type_valid)
		return &req;

	req.platform_type_valid = 1;
	req.platform_type = IPA_QMI_PLATFORM_TYPE_MSM_ANDROID;

	mem = &ipa->mem[IPA_MEM_MODEM_HEADER];
	if (mem->size) {
		req.hdr_tbl_info_valid = 1;
		req.hdr_tbl_info.start = ipa->mem_offset + mem->offset;
		req.hdr_tbl_info.end = req.hdr_tbl_info.start + mem->size - 1;
	}

	mem = &ipa->mem[IPA_MEM_V4_ROUTE];
	req.v4_route_tbl_info_valid = 1;
	req.v4_route_tbl_info.start = ipa->mem_offset + mem->offset;
	req.v4_route_tbl_info.count = mem->size / IPA_TABLE_ENTRY_SIZE;

	mem = &ipa->mem[IPA_MEM_V6_ROUTE];
	req.v6_route_tbl_info_valid = 1;
	req.v6_route_tbl_info.start = ipa->mem_offset + mem->offset;
	req.v6_route_tbl_info.count = mem->size / IPA_TABLE_ENTRY_SIZE;

	mem = &ipa->mem[IPA_MEM_V4_FILTER];
	req.v4_filter_tbl_start_valid = 1;
	req.v4_filter_tbl_start = ipa->mem_offset + mem->offset;

	mem = &ipa->mem[IPA_MEM_V6_FILTER];
	req.v6_filter_tbl_start_valid = 1;
	req.v6_filter_tbl_start = ipa->mem_offset + mem->offset;

	mem = &ipa->mem[IPA_MEM_MODEM];
	if (mem->size) {
		req.modem_mem_info_valid = 1;
		req.modem_mem_info.start = ipa->mem_offset + mem->offset;
		req.modem_mem_info.size = mem->size;
	}

	req.ctrl_comm_dest_end_pt_valid = 1;
	req.ctrl_comm_dest_end_pt =
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]->endpoint_id;

	/* skip_uc_load_valid and skip_uc_load are set above */

	mem = &ipa->mem[IPA_MEM_MODEM_PROC_CTX];
	if (mem->size) {
		req.hdr_proc_ctx_tbl_info_valid = 1;
		req.hdr_proc_ctx_tbl_info.start =
			ipa->mem_offset + mem->offset;
		req.hdr_proc_ctx_tbl_info.end =
			req.hdr_proc_ctx_tbl_info.start + mem->size - 1;
	}

	/* Nothing to report for the compression table (zip_tbl_info) */

	mem = &ipa->mem[IPA_MEM_V4_ROUTE_HASHED];
	if (mem->size) {
		req.v4_hash_route_tbl_info_valid = 1;
		req.v4_hash_route_tbl_info.start =
				ipa->mem_offset + mem->offset;
		req.v4_hash_route_tbl_info.count =
				mem->size / IPA_TABLE_ENTRY_SIZE;
	}

	mem = &ipa->mem[IPA_MEM_V6_ROUTE_HASHED];
	if (mem->size) {
		req.v6_hash_route_tbl_info_valid = 1;
		req.v6_hash_route_tbl_info.start =
			ipa->mem_offset + mem->offset;
		req.v6_hash_route_tbl_info.count =
			mem->size / IPA_TABLE_ENTRY_SIZE;
	}

	mem = &ipa->mem[IPA_MEM_V4_FILTER_HASHED];
	if (mem->size) {
		req.v4_hash_filter_tbl_start_valid = 1;
		req.v4_hash_filter_tbl_start = ipa->mem_offset + mem->offset;
	}

	mem = &ipa->mem[IPA_MEM_V6_FILTER_HASHED];
	if (mem->size) {
		req.v6_hash_filter_tbl_start_valid = 1;
		req.v6_hash_filter_tbl_start = ipa->mem_offset + mem->offset;
	}

	/* None of the stats fields are valid (IPA v4.0 and above) */

	if (ipa->version != IPA_VERSION_3_5_1) {
		mem = &ipa->mem[IPA_MEM_STATS_QUOTA];
		if (mem->size) {
			req.hw_stats_quota_base_addr_valid = 1;
			req.hw_stats_quota_base_addr =
				ipa->mem_offset + mem->offset;
			req.hw_stats_quota_size_valid = 1;
			req.hw_stats_quota_size = ipa->mem_offset + mem->size;
		}

		mem = &ipa->mem[IPA_MEM_STATS_DROP];
		if (mem->size) {
			req.hw_stats_drop_base_addr_valid = 1;
			req.hw_stats_drop_base_addr =
				ipa->mem_offset + mem->offset;
			req.hw_stats_drop_size_valid = 1;
			req.hw_stats_drop_size = ipa->mem_offset + mem->size;
		}
	}

	return &req;
}

/* Send an INIT_DRIVER request to the modem, and wait for it to complete. */
static void ipa_client_init_driver_work(struct work_struct *work)
{
	unsigned long timeout = msecs_to_jiffies(QMI_INIT_DRIVER_TIMEOUT);
	const struct ipa_init_modem_driver_req *req;
	struct ipa_qmi *ipa_qmi;
	struct qmi_handle *qmi;
	struct qmi_txn txn;
	struct device *dev;
	struct ipa *ipa;
	int ret;

	ipa_qmi = container_of(work, struct ipa_qmi, init_driver_work);
	qmi = &ipa_qmi->client_handle;

	ipa = container_of(ipa_qmi, struct ipa, qmi);
	dev = &ipa->pdev->dev;

	ret = qmi_txn_init(qmi, &txn, NULL, NULL);
	if (ret < 0) {
		dev_err(dev, "error %d preparing init driver request\n", ret);
		return;
	}

	/* Send the request, and if successful wait for its response */
	req = init_modem_driver_req(ipa_qmi);
	ret = qmi_send_request(qmi, &ipa_qmi->modem_sq, &txn,
			       IPA_QMI_INIT_DRIVER, IPA_QMI_INIT_DRIVER_REQ_SZ,
			       ipa_init_modem_driver_req_ei, req);
	if (ret)
		dev_err(dev, "error %d sending init driver request\n", ret);
	else if ((ret = qmi_txn_wait(&txn, timeout)))
		dev_err(dev, "error %d awaiting init driver response\n", ret);

	if (!ret) {
		ipa_qmi->modem_ready = true;
		ipa_qmi_ready(ipa_qmi);		/* We might be ready now */
	} else {
		/* If any error occurs we need to cancel the transaction */
		qmi_txn_cancel(&txn);
	}
}

/* The modem server is now available.  We will send an INIT_DRIVER request
 * to the modem, but can't wait for it to complete in this callback thread.
 * Schedule a worker on the global workqueue to do that for us.
 */
static int
ipa_client_new_server(struct qmi_handle *qmi, struct qmi_service *svc)
{
	struct ipa_qmi *ipa_qmi;

	ipa_qmi = container_of(qmi, struct ipa_qmi, client_handle);

	ipa_qmi->modem_sq.sq_family = AF_QIPCRTR;
	ipa_qmi->modem_sq.sq_node = svc->node;
	ipa_qmi->modem_sq.sq_port = svc->port;

	schedule_work(&ipa_qmi->init_driver_work);

	return 0;
}

static const struct qmi_ops ipa_client_ops = {
	.new_server	= ipa_client_new_server,
};

/* This is called by ipa_setup().  We can be informed via remoteproc that
 * the modem has shut down, in which case this function will be called
 * again to prepare for it coming back up again.
 */
int ipa_qmi_setup(struct ipa *ipa)
{
	struct ipa_qmi *ipa_qmi = &ipa->qmi;
	int ret;

	ipa_qmi->initial_boot = true;

	/* The server handle is used to handle the DRIVER_INIT_COMPLETE
	 * request on the first modem boot.  It also receives the
	 * INDICATION_REGISTER request on the first boot and (optionally)
	 * subsequent boots.  The INIT_COMPLETE indication message is
	 * sent over the server handle if requested.
	 */
	ret = qmi_handle_init(&ipa_qmi->server_handle,
			      IPA_QMI_SERVER_MAX_RCV_SZ, &ipa_server_ops,
			      ipa_server_msg_handlers);
	if (ret)
		return ret;

	ret = qmi_add_server(&ipa_qmi->server_handle, IPA_HOST_SERVICE_SVC_ID,
			     IPA_HOST_SVC_VERS, IPA_HOST_SERVICE_INS_ID);
	if (ret)
		goto err_server_handle_release;

	/* The client handle is only used for sending an INIT_DRIVER request
	 * to the modem, and receiving its response message.
	 */
	ret = qmi_handle_init(&ipa_qmi->client_handle,
			      IPA_QMI_CLIENT_MAX_RCV_SZ, &ipa_client_ops,
			      ipa_client_msg_handlers);
	if (ret)
		goto err_server_handle_release;

	/* We need this ready before the service lookup is added */
	INIT_WORK(&ipa_qmi->init_driver_work, ipa_client_init_driver_work);

	ret = qmi_add_lookup(&ipa_qmi->client_handle, IPA_MODEM_SERVICE_SVC_ID,
			     IPA_MODEM_SVC_VERS, IPA_MODEM_SERVICE_INS_ID);
	if (ret)
		goto err_client_handle_release;

	return 0;

err_client_handle_release:
	/* Releasing the handle also removes registered lookups */
	qmi_handle_release(&ipa_qmi->client_handle);
	memset(&ipa_qmi->client_handle, 0, sizeof(ipa_qmi->client_handle));
err_server_handle_release:
	/* Releasing the handle also removes registered services */
	qmi_handle_release(&ipa_qmi->server_handle);
	memset(&ipa_qmi->server_handle, 0, sizeof(ipa_qmi->server_handle));

	return ret;
}

void ipa_qmi_teardown(struct ipa *ipa)
{
	cancel_work_sync(&ipa->qmi.init_driver_work);

	qmi_handle_release(&ipa->qmi.client_handle);
	memset(&ipa->qmi.client_handle, 0, sizeof(ipa->qmi.client_handle));

	qmi_handle_release(&ipa->qmi.server_handle);
	memset(&ipa->qmi.server_handle, 0, sizeof(ipa->qmi.server_handle));
}
