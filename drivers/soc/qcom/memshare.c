// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/net.h>
#include <linux/soc/qcom/qmi.h>

#include "memshare_qmi_msg.h"

#define MEMSHARE_MAX_CLIENTS 10

struct memshare_client {
	u32 id;
	u32 proc_id;
	u32 qrtr_node;
	u32 size;
	phys_addr_t phy_addr;
};

struct memshare {
	struct device *dev;
	struct qmi_handle qmi;
	unsigned int client_cnt;
	struct memshare_client *legacy_client;
	struct memshare_client *clients[MEMSHARE_MAX_CLIENTS];
};

static struct memshare_client *memshare_get_client(struct memshare *share, u32 id, u32 proc_id)
{
	int i;

	for (i = 0; i < share->client_cnt; i++)
		if (share->clients[i]->id == id && share->clients[i]->proc_id == proc_id)
			return share->clients[i];

	return NULL;
}

static void memshare_alloc_req_handler(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
				       struct qmi_txn *txn, const void *data)
{
	struct memshare *share = container_of(qmi, struct memshare, qmi);
	const struct mem_alloc_req_msg_v01 *req = data;
	struct mem_alloc_resp_msg_v01 resp = { .resp = QMI_RESULT_FAILURE_V01 };
	struct memshare_client *client = share->legacy_client;
	int ret;

	dev_dbg(share->dev,
		"alloc_req: num_bytes=%d, block_alignment_valid=%d, block_alignment=%d, node=%d\n",
		req->num_bytes,
		req->block_alignment_valid,
		req->block_alignment,
		sq->sq_node
	);

	if (!client) {
		dev_err(share->dev, "Unknown request from legacy client (size=%d, node=%d)\n",
			req->num_bytes, sq->sq_node);
		goto send_response;
	}

	if (sq->sq_node != client->qrtr_node) {
		dev_err(share->dev, "Request from node %d but %d expected\n",
			sq->sq_node, client->qrtr_node);
		goto send_response;
	}

	if (client->size && client->size != req->num_bytes) {
		dev_err(share->dev, "Got a request with wrong size (size=%d)\n",
			req->num_bytes);
		goto send_response;
	}

	if (req->block_alignment_valid)
		if (client->phy_addr % MEM_BLOCK_ALIGN_TO_BYTES(req->block_alignment) != 0)
			dev_warn(share->dev, "Memory region is not aligned by %d bytes\n",
				 MEM_BLOCK_ALIGN_TO_BYTES(req->block_alignment));

	if (!client->phy_addr) {
		dev_info(share->dev,
			 "Client sent a request but no memory is configured (size=%d, node=%d)\n",
			 req->num_bytes, sq->sq_node);
		goto send_response;
	}

	resp.resp = QMI_RESULT_SUCCESS_V01;
	resp.handle_valid = true;
	resp.handle = client->phy_addr;
	resp.num_bytes_valid = true;
	resp.num_bytes = client->size;

send_response:
	ret = qmi_send_response(qmi, sq, txn, MEM_ALLOC_MSG_V01, MEM_MAX_MSG_LEN_V01,
				mem_alloc_resp_msg_data_v01_ei, &resp);
	if (ret < 0)
		dev_err(share->dev, "Failed to send response: %d\n", ret);
}

static void memshare_free_req_handler(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
				      struct qmi_txn *txn, const void *data)
{
	struct memshare *share = container_of(qmi, struct memshare, qmi);
	const struct mem_free_req_msg_v01 *req = data;
	struct mem_free_resp_msg_v01 resp = { .resp = QMI_RESULT_FAILURE_V01 };
	struct memshare_client *client = share->legacy_client;
	int ret;

	dev_dbg(share->dev, "free_req: node=%d\n", sq->sq_node);

	if (!client) {
		dev_err(share->dev, "Unknown request from legacy client\n");
		goto send_response;
	}

	if (sq->sq_node != client->qrtr_node) {
		dev_err(share->dev, "Request from node %d but %d expected\n",
			sq->sq_node, client->qrtr_node);
		goto send_response;
	}

	if (client->phy_addr != req->handle) {
		dev_err(share->dev, "Got a request with wrong address\n");
		goto send_response;
	}

	resp.resp = QMI_RESULT_SUCCESS_V01;

send_response:
	ret = qmi_send_response(qmi, sq, txn, MEM_FREE_MSG_V01, MEM_MAX_MSG_LEN_V01,
				mem_free_resp_msg_data_v01_ei, &resp);
	if (ret < 0)
		dev_err(share->dev, "Failed to send response: %d\n", ret);
}

static void memshare_alloc_generic_req_handler(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
					       struct qmi_txn *txn, const void *data)
{
	struct memshare *share = container_of(qmi, struct memshare, qmi);
	const struct mem_alloc_generic_req_msg_v01 *req = data;
	struct mem_alloc_generic_resp_msg_v01 *resp;
	struct memshare_client *client;
	int ret;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp)
		return;

	resp->resp.result = QMI_RESULT_FAILURE_V01;
	resp->resp.error = QMI_ERR_INTERNAL_V01;

	dev_dbg(share->dev,
		"alloc_generic_req: num_bytes=%d, client_id=%d, proc_id=%d, sequence_id=%d, "
		"alloc_contiguous_valid=%d, alloc_contiguous=%d, block_alignment_valid=%d, "
		"block_alignment=%d, node=%d\n",
		req->num_bytes,
		req->client_id,
		req->proc_id,
		req->sequence_id,
		req->alloc_contiguous_valid,
		req->alloc_contiguous,
		req->block_alignment_valid,
		req->block_alignment,
		sq->sq_node
	);

	client = memshare_get_client(share, req->client_id, req->proc_id);
	if (!client) {
		dev_err(share->dev,
			"Got a request from unknown client (id=%d, proc=%d, size=%d, node=%d)\n",
			req->client_id, req->proc_id, req->num_bytes, sq->sq_node);
		goto send_response;
	}

	if (sq->sq_node != client->qrtr_node) {
		dev_err(share->dev, "Request from node %d but %d expected\n",
			sq->sq_node, client->qrtr_node);
		goto send_response;
	}

	if (!client->phy_addr) {
		dev_info(share->dev,
			 "Client sent a request but no memory is configured "
			 "(id=%d, proc=%d, size=%d, node=%d)\n",
			 req->client_id, req->proc_id, req->num_bytes, sq->sq_node);
		goto send_response;
	}

	if (client->size != req->num_bytes) {
		dev_err(share->dev,
			"Got a request with wrong size (id=%d, proc=%d, size=%d)\n",
			req->client_id, req->proc_id, req->num_bytes);
		goto send_response;
	}

	if (req->block_alignment_valid)
		if (client->phy_addr % MEM_BLOCK_ALIGN_TO_BYTES(req->block_alignment) != 0)
			dev_warn(share->dev, "Memory region is not aligned by %d bytes\n",
				 MEM_BLOCK_ALIGN_TO_BYTES(req->block_alignment));

	resp->resp.result = QMI_RESULT_SUCCESS_V01;
	resp->resp.error = QMI_ERR_NONE_V01;
	resp->sequence_id_valid = true;
	resp->sequence_id = req->sequence_id;
	resp->dhms_mem_alloc_addr_info_valid = true;
	resp->dhms_mem_alloc_addr_info_len = 1;
	resp->dhms_mem_alloc_addr_info[0].phy_addr = client->phy_addr;
	resp->dhms_mem_alloc_addr_info[0].num_bytes = client->size;

send_response:
	ret = qmi_send_response(qmi, sq, txn, MEM_ALLOC_GENERIC_MSG_V01, MEM_MAX_MSG_LEN_V01,
				mem_alloc_generic_resp_msg_data_v01_ei, resp);
	if (ret < 0)
		dev_err(share->dev, "Failed to send response: %d\n", ret);

	kfree(resp);
}

static void memshare_free_generic_req_handler(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
					      struct qmi_txn *txn, const void *data)
{
	struct memshare *share = container_of(qmi, struct memshare, qmi);
	const struct mem_free_generic_req_msg_v01 *req = data;
	struct mem_free_generic_resp_msg_v01 resp = {
		.resp.result = QMI_RESULT_FAILURE_V01,
		.resp.error = QMI_ERR_INTERNAL_V01,
	};
	struct memshare_client *client;
	int ret;

	dev_dbg(share->dev,
		"free_generic_req: dhms_mem_alloc_addr_info_len=%d, client_id_valid=%d, "
		"client_id=%d, proc_id_valid=%d, proc_id=%d, node=%d\n",
		req->dhms_mem_alloc_addr_info_len,
		req->client_id_valid,
		req->client_id,
		req->proc_id_valid,
		req->proc_id,
		sq->sq_node
	);

	if (req->dhms_mem_alloc_addr_info_len != 1) {
		dev_err(share->dev, "addr_info_len = %d is unexpected\n",
			req->dhms_mem_alloc_addr_info_len);
		goto send_response;
	}

	if (!req->client_id_valid || !req->proc_id_valid) {
		dev_err(share->dev, "Got a request from unknown client\n");
		goto send_response;
	}

	client = memshare_get_client(share, req->client_id, req->proc_id);
	if (!client) {
		dev_err(share->dev, "Got a request from unknown client (id=%d, proc=%d)\n",
			req->client_id, req->proc_id);
		goto send_response;
	}

	if (req->dhms_mem_alloc_addr_info[0].phy_addr != client->phy_addr) {
		dev_err(share->dev, "Client sent invalid handle\n");
		goto send_response;
	}

	if (sq->sq_node != client->qrtr_node) {
		dev_err(share->dev, "Request from node %d but %d expected\n",
			sq->sq_node, client->qrtr_node);
		goto send_response;
	}

	resp.resp.result = QMI_RESULT_SUCCESS_V01;
	resp.resp.error = QMI_ERR_NONE_V01;

send_response:
	ret = qmi_send_response(qmi, sq, txn, MEM_FREE_GENERIC_MSG_V01, MEM_MAX_MSG_LEN_V01,
				mem_free_generic_resp_msg_data_v01_ei, &resp);
	if (ret < 0)
		dev_err(share->dev, "Failed to send response: %d\n", ret);
}

static void memshare_query_size_req_handler(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
					    struct qmi_txn *txn, const void *data)
{
	struct memshare *share = container_of(qmi, struct memshare, qmi);
	const struct mem_query_size_req_msg_v01 *req = data;
	struct mem_query_size_rsp_msg_v01 resp = {
		.resp.result = QMI_RESULT_FAILURE_V01,
		.resp.error = QMI_ERR_INTERNAL_V01,
	};
	struct memshare_client *client;
	int ret;

	dev_dbg(share->dev,
		"query_size_req: client_id=%d, proc_id_valid=%d, proc_id=%d, node=%d\n",
		req->client_id,
		req->proc_id_valid,
		req->proc_id,
		sq->sq_node
	);

	client = memshare_get_client(share, req->client_id, req->proc_id);
	if (!client) {
		dev_err(share->dev, "Got a request from unknown client (id=%d, proc=%d)\n",
			req->client_id, req->proc_id);
		goto send_response;
	}

	if (sq->sq_node != client->qrtr_node) {
		dev_err(share->dev, "Request from node %d but %d expected\n",
			sq->sq_node, client->qrtr_node);
		goto send_response;
	}

	if (!client->phy_addr)
		goto send_response;

	resp.resp.result = QMI_RESULT_SUCCESS_V01;
	resp.resp.error = QMI_ERR_NONE_V01;
	resp.size_valid = true;
	resp.size = client->size;

send_response:
	ret = qmi_send_response(qmi, sq, txn, MEM_QUERY_SIZE_MSG_V01, MEM_MAX_MSG_LEN_V01,
				mem_query_size_resp_msg_data_v01_ei, &resp);
	if (ret < 0)
		dev_err(share->dev, "Failed to send response: %d\n", ret);
}

static struct qmi_msg_handler memshare_handlers[] = {
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_ALLOC_MSG_V01,
		.ei = mem_alloc_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_alloc_req_msg_v01),
		.fn = memshare_alloc_req_handler,
	},
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_FREE_MSG_V01,
		.ei = mem_free_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_free_req_msg_v01),
		.fn = memshare_free_req_handler,
	},
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_ALLOC_GENERIC_MSG_V01,
		.ei = mem_alloc_generic_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_alloc_generic_req_msg_v01),
		.fn = memshare_alloc_generic_req_handler,
	},
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_FREE_GENERIC_MSG_V01,
		.ei = mem_free_generic_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_free_generic_req_msg_v01),
		.fn = memshare_free_generic_req_handler,
	},
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_QUERY_SIZE_MSG_V01,
		.ei = mem_query_size_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_query_size_req_msg_v01),
		.fn = memshare_query_size_req_handler,
	},
	{ /* sentinel */ }
};

static int memshare_probe_dt(struct memshare *share)
{
	struct device_node *np = share->dev->of_node;
	struct device_node *proc_node = NULL, *client_node = NULL, *mem_node = NULL;
	struct reserved_mem *rmem;
	int ret = 0;
	u32 proc_id, qrtr_node;
	struct memshare_client *client;
	phandle legacy_client = 0;

	ret = of_property_read_u32(np, "qcom,legacy-client", &legacy_client);
	if (ret && ret != -EINVAL)
		return ret;

	for_each_available_child_of_node(np, proc_node) {
		ret = of_property_read_u32(proc_node, "reg", &proc_id);
		if (ret)
			goto error;

		ret = of_property_read_u32(proc_node, "qcom,qrtr-node", &qrtr_node);
		if (ret)
			goto error;

		for_each_available_child_of_node(proc_node, client_node) {
			if (share->client_cnt >= MEMSHARE_MAX_CLIENTS) {
				ret = -EINVAL;
				goto error;
			}

			client = devm_kzalloc(share->dev, sizeof(*client), GFP_KERNEL);
			if (!client) {
				ret = -ENOMEM;
				goto error;
			}

			ret = of_property_read_u32(client_node, "reg", &client->id);
			if (ret)
				goto error;

			client->proc_id = proc_id;
			client->qrtr_node = qrtr_node;

			mem_node = of_parse_phandle(client_node, "memory-region", 0);
			if (mem_node) {
				rmem = of_reserved_mem_lookup(mem_node);
				of_node_put(mem_node);
				if (!rmem) {
					dev_err(share->dev, "unable to resolve memory-region\n");
					ret = -EINVAL;
					goto error;
				}

				client->phy_addr = rmem->base;
				client->size = rmem->size;
			}

			if (client_node->phandle == legacy_client)
				share->legacy_client = client;

			share->clients[share->client_cnt] = client;
			share->client_cnt++;
		}
	}

	return 0;

error:
	of_node_put(client_node);
	of_node_put(proc_node);
	return ret;
}

static int memshare_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct memshare *share;
	int ret;

	share = devm_kzalloc(&pdev->dev, sizeof(*share), GFP_KERNEL);
	if (!share)
		return -ENOMEM;

	share->dev = dev;
	dev_set_drvdata(&pdev->dev, share);

	ret = qmi_handle_init(&share->qmi, MEM_MAX_MSG_LEN_V01, NULL, memshare_handlers);
	if (ret < 0)
		return ret;

	ret = memshare_probe_dt(share);
	if (ret < 0)
		goto error;

	ret = qmi_add_server(&share->qmi, MEM_SERVICE_SVC_ID,
			     MEM_SERVICE_VER, MEM_SERVICE_INS_ID);
	if (ret < 0)
		goto error;

	return 0;

error:
	qmi_handle_release(&share->qmi);
	return ret;
}

static void memshare_remove(struct platform_device *pdev)
{
	struct memshare *share = dev_get_drvdata(&pdev->dev);

	qmi_handle_release(&share->qmi);

}

static const struct of_device_id memshare_of_match[] = {
	{ .compatible = "qcom,memshare", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, memshare_of_match);

static struct platform_driver memshare_driver = {
	.probe = memshare_probe,
	.remove = memshare_remove,
	.driver = {
		.name = "qcom-memshare",
		.of_match_table = of_match_ptr(memshare_of_match),
	},
};
module_platform_driver(memshare_driver);

MODULE_DESCRIPTION("Qualcomm Memory Share Service");
MODULE_AUTHOR("Nikita Travkin <nikitos.tr@gmail.com>");
MODULE_LICENSE("GPL v2");
