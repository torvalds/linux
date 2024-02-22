// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/rpmsg/qcom_glink.h>
#include <linux/qcom_scm.h>
#include "msm_memshare.h"
#include "heap_mem_ext_v01.h"

#include <soc/qcom/secure_buffer.h>
#include <trace/events/rproc_qcom.h>

/* Macros */
static unsigned long(attrs);

static struct qmi_handle *mem_share_svc_handle;
static uint64_t bootup_request;

/* Memshare Driver Structure */
struct memshare_driver {
	struct device *dev;
	struct mutex mem_share;
	struct mutex mem_free;
	struct work_struct memshare_init_work;
};

struct memshare_child {
	struct device *dev;
	int client_id;
	struct qcom_glink_mem_entry *mem_entry;
};

static struct memshare_driver *memsh_drv;
static struct memshare_child *memsh_child[MAX_CLIENTS];
static struct mem_blocks memblock[MAX_CLIENTS];
static uint32_t num_clients;

static inline bool is_shared_mapping(struct mem_blocks *mb)
{
	if (!mb)
		return false;

	return mb->hyp_map_info.num_vmids > 1;
}

static int check_client(int client_id, int proc, int request)
{
	int i = 0;
	int found = DHMS_MEM_CLIENT_INVALID;

	for (i = 0; i < num_clients; i++) {
		if (memblock[i].client_id == client_id &&
				memblock[i].peripheral == proc) {
			found = i;
			break;
		}
	}
	if ((found == DHMS_MEM_CLIENT_INVALID) && !request) {
		dev_dbg(memsh_drv->dev,
			"memshare: No registered client for the client_id: %d, adding a new client\n",
			client_id);
		/* Add a new client */
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (memblock[i].client_id == DHMS_MEM_CLIENT_INVALID) {
				memblock[i].client_id = client_id;
				memblock[i].allotted = 0;
				memblock[i].guarantee = 0;
				memblock[i].peripheral = proc;
				found = i;
				break;
			}
		}
	}

	return found;
}

static void free_client(int id)
{
	memblock[id].phy_addr = 0;
	memblock[id].virtual_addr = 0;
	memblock[id].allotted = 0;
	memblock[id].guarantee = 0;
	memblock[id].sequence_id = -1;
	memblock[id].memory_type = MEMORY_CMA;
}

static void fill_alloc_response(struct mem_alloc_generic_resp_msg_v01 *resp,
						int id, int *flag)
{
	resp->sequence_id_valid = 1;
	resp->sequence_id = memblock[id].sequence_id;
	resp->dhms_mem_alloc_addr_info_valid = 1;
	resp->dhms_mem_alloc_addr_info_len = 1;
	resp->dhms_mem_alloc_addr_info[0].phy_addr = memblock[id].phy_addr;
	resp->dhms_mem_alloc_addr_info[0].num_bytes = memblock[id].size;
	if (!*flag) {
		resp->resp.result = QMI_RESULT_SUCCESS_V01;
		resp->resp.error = QMI_ERR_NONE_V01;
	} else {
		resp->resp.result = QMI_RESULT_FAILURE_V01;
		resp->resp.error = QMI_ERR_NO_MEMORY_V01;
	}
}

static void initialize_client(void)
{
	int i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		memblock[i].allotted = 0;
		memblock[i].size = 0;
		memblock[i].guarantee = 0;
		memblock[i].phy_addr = 0;
		memblock[i].virtual_addr = 0;
		memblock[i].client_id = DHMS_MEM_CLIENT_INVALID;
		memblock[i].peripheral = -1;
		memblock[i].sequence_id = -1;
		memblock[i].memory_type = MEMORY_CMA;
		memblock[i].free_memory = 0;
		memblock[i].hyp_mapping = 0;
	}
}

static int modem_notifier_cb(struct notifier_block *this, unsigned long code,
					void *_cmd)
{
	u64 source_vmids = 0;
	int i, j, ret, size = 0;
	struct qcom_scm_vmperm dest_vmids[] = {{QCOM_SCM_VMID_HLOS},
					       {PERM_READ|PERM_WRITE|PERM_EXEC}};
	struct memshare_child *client_node = NULL;

	mutex_lock(&memsh_drv->mem_share);

	switch (code) {

	case QCOM_SSR_BEFORE_SHUTDOWN:
		trace_rproc_qcom_event("modem", "QCOM_SSR_BEFORE_SHUTDOWN", "modem_notifier-enter");
		bootup_request++;
		dev_info(memsh_drv->dev,
		"memshare: QCOM_SSR_BEFORE_SHUTDOWN: bootup_request:%d\n",
		bootup_request);
		for (i = 0; i < MAX_CLIENTS; i++)
			memblock[i].alloc_request = 0;
		break;

	case QCOM_SSR_AFTER_SHUTDOWN:
		trace_rproc_qcom_event("modem", "QCOM_SSR_AFTER_SHUTDOWN", "modem_notifier-enter");
		break;

	case QCOM_SSR_BEFORE_POWERUP:
		trace_rproc_qcom_event("modem", "QCOM_SSR_BEFORE_POWERUP", "modem_notifier-enter");
		break;

	case QCOM_SSR_AFTER_POWERUP:
		trace_rproc_qcom_event("modem", "QCOM_SSR_AFTER_POWERUP", "modem_notifier-enter");
		dev_info(memsh_drv->dev, "memshare: QCOM_SSR_AFTER_POWERUP: Modem has booted up\n");
		for (i = 0; i < MAX_CLIENTS; i++) {
			client_node = memsh_child[i];
			size = memblock[i].size;
			if (memblock[i].free_memory > 0 &&
					bootup_request >= 2) {
				memblock[i].free_memory -= 1;
				dev_dbg(memsh_drv->dev, "memshare: free_memory count: %d for client id: %d\n",
					memblock[i].free_memory,
					memblock[i].client_id);
			}

			if (memblock[i].free_memory == 0 &&
				memblock[i].peripheral ==
				DHMS_MEM_PROC_MPSS_V01 &&
				!memblock[i].guarantee &&
				!memblock[i].client_request &&
				memblock[i].allotted &&
				!memblock[i].alloc_request) {
				dev_info(memsh_drv->dev,
					"memshare: hypervisor unmapping for allocated memory with client id: %d\n",
					memblock[i].client_id);
				if (memblock[i].hyp_mapping) {
					struct memshare_hyp_mapping *source;

					source = &memblock[i].hyp_map_info;
					for (j = 0; j < source->num_vmids; j++)
						source_vmids |= BIT(source->vmids[j]);

					ret = qcom_scm_assign_mem(
							memblock[i].phy_addr,
							memblock[i].size,
							&source_vmids,
							dest_vmids, 1);
					if (ret &&
						memblock[i].hyp_mapping == 1) {
						/*
						 * This is an error case as hyp
						 * mapping was successful
						 * earlier but during unmap
						 * it lead to failure.
						 */
						dev_err(memsh_drv->dev,
							"memshare: failed to hypervisor unmap the memory region for client id: %d\n",
							memblock[i].client_id);
					} else {
						memblock[i].hyp_mapping = 0;
					}
				}
				if (memblock[i].guard_band) {
				/*
				 *	Check if the client required guard band
				 *	support so the memory region of client's
				 *	size + guard bytes of 4K can be freed.
				 */
					size += MEMSHARE_GUARD_BYTES;
				}
				dma_free_attrs(client_node->dev,
					size, memblock[i].virtual_addr,
					memblock[i].phy_addr,
					attrs);
				free_client(i);
			}
		}
		bootup_request++;
		break;

	default:
		break;
	}
	mutex_unlock(&memsh_drv->mem_share);
	dev_info(memsh_drv->dev,
	"memshare: notifier_cb processed for code: %d\n", code);

	trace_rproc_qcom_event("modem", "modem_notifier", "exit");
	return NOTIFY_DONE;
}

static struct notifier_block nb = {
	.notifier_call = modem_notifier_cb,
};

static void shared_hyp_mapping(int index)
{
	u64 source_vmlist[] = {BIT(QCOM_SCM_VMID_HLOS)};
	struct memshare_hyp_mapping *dest;
	struct qcom_scm_vmperm *newvm;
	struct mem_blocks *mb;
	int ret, j;

	if (index >= MAX_CLIENTS) {
		dev_err(memsh_drv->dev,
			"memshare: hypervisor mapping failure for invalid client\n");
		return;
	}
	mb = &memblock[index];
	dest = &mb->hyp_map_info;

	newvm = kcalloc(dest->num_vmids, sizeof(struct qcom_scm_vmperm), GFP_KERNEL);
	if (!newvm)
		return;

	for (j = 0; j < dest->num_vmids; j++) {
		newvm[j].vmid = dest->vmids[j];
		newvm[j].perm = dest->perms[j];
	}
	ret = qcom_scm_assign_mem(mb->phy_addr, mb->size, source_vmlist,
			      newvm, dest->num_vmids);
	kfree(newvm);
	if (ret != 0) {
		dev_err(memsh_drv->dev, "memshare: qcom_scm_assign_mem failed size: %u, err: %d\n",
				mb->size, ret);
		return;
	}
	mb->hyp_mapping = 1;
}

static void handle_alloc_generic_req(struct qmi_handle *handle,
	struct sockaddr_qrtr *sq, struct qmi_txn *txn, const void *decoded_msg)
{
	struct mem_alloc_generic_req_msg_v01 *alloc_req;
	struct mem_alloc_generic_resp_msg_v01 *alloc_resp;
	struct memshare_child *client_node = NULL;
	int rc, resp = 0, i;
	int index = DHMS_MEM_CLIENT_INVALID;
	uint32_t size = 0;

	mutex_lock(&memsh_drv->mem_share);
	alloc_req = (struct mem_alloc_generic_req_msg_v01 *)decoded_msg;
	dev_info(memsh_drv->dev,
		"memshare_alloc: memory alloc request received for client id: %d, proc_id: %d, request size: %d\n",
		alloc_req->client_id, alloc_req->proc_id, alloc_req->num_bytes);
	alloc_resp = kzalloc(sizeof(*alloc_resp),
					GFP_KERNEL);
	if (!alloc_resp) {
		mutex_unlock(&memsh_drv->mem_share);
		return;
	}
	alloc_resp->resp.result = QMI_RESULT_FAILURE_V01;
	alloc_resp->resp.error = QMI_ERR_NO_MEMORY_V01;
	index = check_client(alloc_req->client_id, alloc_req->proc_id,
								CHECK);

	if (index >= MAX_CLIENTS) {
		dev_err(memsh_drv->dev,
			"memshare_alloc: client not found for index: %d, requested client: %d, proc_id: %d\n",
			index, alloc_req->client_id, alloc_req->proc_id);
		kfree(alloc_resp);
		alloc_resp = NULL;
		mutex_unlock(&memsh_drv->mem_share);
		return;
	}

	for (i = 0; i < num_clients; i++) {
		if (memsh_child[i]->client_id == alloc_req->client_id) {
			client_node = memsh_child[i];
			dev_info(memsh_drv->dev,
				"memshare_alloc: found client with client_id: %d, index: %d\n",
				alloc_req->client_id, index);
			break;
		}
	}

	if (!client_node) {
		dev_err(memsh_drv->dev,
			"memshare_alloc: No valid client node found\n");
		kfree(alloc_resp);
		alloc_resp = NULL;
		mutex_unlock(&memsh_drv->mem_share);
		return;
	}

	if (!memblock[index].allotted && alloc_req->num_bytes > 0) {

		if (alloc_req->num_bytes > memblock[index].init_size)
			alloc_req->num_bytes = memblock[index].init_size;

		if (memblock[index].guard_band)
			size = alloc_req->num_bytes + MEMSHARE_GUARD_BYTES;
		else
			size = alloc_req->num_bytes;
		rc = memshare_alloc(client_node->dev, size,
					&memblock[index]);
		if (rc) {
			dev_err(memsh_drv->dev,
				"memshare_alloc: unable to allocate memory of size: %d for requested client\n",
				size);
			resp = 1;
		}
		if (!resp) {
			memblock[index].free_memory += 1;
			memblock[index].allotted = 1;
			memblock[index].size = alloc_req->num_bytes;
			memblock[index].peripheral = alloc_req->proc_id;
		}
	}

	if (is_shared_mapping(&memblock[index])) {
		struct mem_blocks *mb = &memblock[index];

		client_node->mem_entry = qcom_glink_mem_entry_init(client_node->dev,
				mb->virtual_addr, mb->phy_addr, mb->size, mb->phy_addr);
	}
	dev_dbg(memsh_drv->dev,
		"memshare_alloc: free memory count for client id: %d = %d\n",
		memblock[index].client_id, memblock[index].free_memory);

	memblock[index].sequence_id = alloc_req->sequence_id;
	memblock[index].alloc_request = 1;

	fill_alloc_response(alloc_resp, index, &resp);
	/*
	 * Perform the Hypervisor mapping in order to avoid XPU viloation
	 * to the allocated region for Modem Clients
	 */
	if (!memblock[index].hyp_mapping &&
		memblock[index].allotted)
		shared_hyp_mapping(index);
	mutex_unlock(&memsh_drv->mem_share);
	dev_info(memsh_drv->dev,
		"memshare_alloc: client_id: %d, alloc_resp.num_bytes: %d, alloc_resp.resp.result: %lx\n",
		alloc_req->client_id,
		alloc_resp->dhms_mem_alloc_addr_info[0].num_bytes,
		(unsigned long)alloc_resp->resp.result);

	rc = qmi_send_response(mem_share_svc_handle, sq, txn,
			  MEM_ALLOC_GENERIC_RESP_MSG_V01,
			  sizeof(struct mem_alloc_generic_resp_msg_v01),
			  mem_alloc_generic_resp_msg_data_v01_ei, alloc_resp);
	if (rc < 0)
		dev_err(memsh_drv->dev,
		"memshare_alloc: Error sending the alloc response: %d\n",
		rc);

	kfree(alloc_resp);
	alloc_resp = NULL;
}

static void handle_free_generic_req(struct qmi_handle *handle,
	struct sockaddr_qrtr *sq, struct qmi_txn *txn, const void *decoded_msg)
{
	u64 source_vmids = 0;
	struct mem_free_generic_req_msg_v01 *free_req;
	struct mem_free_generic_resp_msg_v01 free_resp;
	struct memshare_child *client_node = NULL;
	int rc, flag = 0, ret = 0, size = 0, i, j;
	int index = DHMS_MEM_CLIENT_INVALID;
	struct qcom_scm_vmperm dest_vmids[] = {{QCOM_SCM_VMID_HLOS},
					       {PERM_READ|PERM_WRITE|PERM_EXEC}};

	mutex_lock(&memsh_drv->mem_free);
	free_req = (struct mem_free_generic_req_msg_v01 *)decoded_msg;
	memset(&free_resp, 0, sizeof(free_resp));
	free_resp.resp.error = QMI_ERR_INTERNAL_V01;
	free_resp.resp.result = QMI_RESULT_FAILURE_V01;
	dev_info(memsh_drv->dev,
		"memshare_free: handling memory free request with client id: %d, proc_id: %d\n",
		free_req->client_id, free_req->proc_id);

	index = check_client(free_req->client_id, free_req->proc_id, FREE);

	if (index >= MAX_CLIENTS) {
		dev_err(memsh_drv->dev, "memshare_free: invalid client request to free memory\n");
		flag = 1;
	}

	for (i = 0; i < num_clients; i++) {
		if (memsh_child[i]->client_id == free_req->client_id) {
			client_node = memsh_child[i];
			dev_info(memsh_drv->dev,
				"memshare_free: found client with client_id: %d, index: %d\n",
				free_req->client_id, index);
			break;
		}
	}

	if (!client_node) {
		dev_err(memsh_drv->dev,
			"memshare_free: No valid client node found\n");
		mutex_unlock(&memsh_drv->mem_free);
		return;
	}

	if (client_node->mem_entry) {
		qcom_glink_mem_entry_free(client_node->mem_entry);
		client_node->mem_entry = NULL;
	}

	if (!flag && !memblock[index].guarantee &&
				!memblock[index].client_request &&
				memblock[index].allotted) {
		struct memshare_hyp_mapping *source;

		dev_dbg(memsh_drv->dev,
			"memshare_free: hypervisor unmapping for free_req->client_id: %d - size: %d\n",
			free_req->client_id, memblock[index].size);

		source = &memblock[index].hyp_map_info;
		for (j = 0; j < source->num_vmids; j++)
			source_vmids |= BIT(source->vmids[j]);

		ret = qcom_scm_assign_mem(memblock[index].phy_addr, memblock[index].size,
				      &source_vmids,
				      dest_vmids, 1);
		if (ret && memblock[index].hyp_mapping == 1) {
		/*
		 * This is an error case as hyp mapping was successful
		 * earlier but during unmap it lead to failure.
		 */
			dev_err(memsh_drv->dev,
				"memshare_free: failed to unmap the region for client id:%d\n",
				index);
		}
		size = memblock[index].size;
		if (memblock[index].guard_band) {
		/*
		 *	Check if the client required guard band support so
		 *	the memory region of client's size + guard
		 *	bytes of 4K can be freed
		 */
			size += MEMSHARE_GUARD_BYTES;
		}
		dma_free_attrs(client_node->dev, size,
			memblock[index].virtual_addr,
			memblock[index].phy_addr,
			attrs);
		free_client(index);
	} else {
		dev_err(memsh_drv->dev,
			"memshare_free: cannot free the memory for a guaranteed client (client index: %d)\n",
			index);
	}

	if (flag) {
		free_resp.resp.result = QMI_RESULT_FAILURE_V01;
		free_resp.resp.error = QMI_ERR_INVALID_ID_V01;
	} else {
		free_resp.resp.result = QMI_RESULT_SUCCESS_V01;
		free_resp.resp.error = QMI_ERR_NONE_V01;
	}

	mutex_unlock(&memsh_drv->mem_free);
	rc = qmi_send_response(mem_share_svc_handle, sq, txn,
			  MEM_FREE_GENERIC_RESP_MSG_V01,
			  MEM_FREE_REQ_MAX_MSG_LEN_V01,
			  mem_free_generic_resp_msg_data_v01_ei, &free_resp);
	if (rc < 0)
		dev_err(memsh_drv->dev,
		"memshare_free: error sending the free response: %d\n", rc);
}

static void handle_query_size_req(struct qmi_handle *handle,
	struct sockaddr_qrtr *sq, struct qmi_txn *txn, const void *decoded_msg)
{
	int rc, index = DHMS_MEM_CLIENT_INVALID;
	struct mem_query_size_req_msg_v01 *query_req;
	struct mem_query_size_rsp_msg_v01 *query_resp;

	mutex_lock(&memsh_drv->mem_share);
	query_req = (struct mem_query_size_req_msg_v01 *)decoded_msg;
	query_resp = kzalloc(sizeof(*query_resp),
					GFP_KERNEL);
	if (!query_resp) {
		mutex_unlock(&memsh_drv->mem_share);
		return;
	}
	dev_dbg(memsh_drv->dev,
		"memshare_query: query on availalbe memory size for client id: %d, proc_id: %d\n",
		query_req->client_id, query_req->proc_id);
	index = check_client(query_req->client_id, query_req->proc_id,
								CHECK);

	if (index >= MAX_CLIENTS) {
		dev_err(memsh_drv->dev,
			"memshare_query: client not found, requested client: %d, proc_id: %d\n",
			query_req->client_id, query_req->proc_id);
		kfree(query_resp);
		query_resp = NULL;
		mutex_unlock(&memsh_drv->mem_share);
		return;
	}

	if (memblock[index].init_size) {
		query_resp->size_valid = 1;
		query_resp->size = memblock[index].init_size;
	} else {
		query_resp->size_valid = 1;
		query_resp->size = 0;
	}
	query_resp->resp.result = QMI_RESULT_SUCCESS_V01;
	query_resp->resp.error = QMI_ERR_NONE_V01;
	mutex_unlock(&memsh_drv->mem_share);

	dev_info(memsh_drv->dev,
		"memshare_query: client_id : %d, query_resp.size :%d, query_resp.resp.result :%lx\n",
		query_req->client_id, query_resp->size,
		(unsigned long)query_resp->resp.result);
	rc = qmi_send_response(mem_share_svc_handle, sq, txn,
			  MEM_QUERY_SIZE_RESP_MSG_V01,
			  MEM_QUERY_MAX_MSG_LEN_V01,
			  mem_query_size_resp_msg_data_v01_ei, query_resp);
	if (rc < 0)
		dev_err(memsh_drv->dev,
		"memshare_query: Error sending the query response: %d\n", rc);

	kfree(query_resp);
	query_resp = NULL;
}

static void mem_share_svc_disconnect_cb(struct qmi_handle *qmi,
				  unsigned int node, unsigned int port)
{
}

static struct qmi_ops server_ops = {
	.del_client = mem_share_svc_disconnect_cb,
};

static struct qmi_msg_handler qmi_memshare_handlers[] = {
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_ALLOC_GENERIC_REQ_MSG_V01,
		.ei = mem_alloc_generic_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_alloc_generic_req_msg_v01),
		.fn = handle_alloc_generic_req,
	},
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_FREE_GENERIC_REQ_MSG_V01,
		.ei = mem_free_generic_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_free_generic_req_msg_v01),
		.fn = handle_free_generic_req,
	},
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_QUERY_SIZE_REQ_MSG_V01,
		.ei = mem_query_size_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_query_size_req_msg_v01),
		.fn = handle_query_size_req,
	},
	{}
};

int memshare_alloc(struct device *dev,
		   unsigned int block_size,
		   struct mem_blocks *pblk)
{
	dev_dbg(memsh_drv->dev,
		"memshare: allocation request for size: %d", block_size);

	if (!pblk) {
		dev_err(memsh_drv->dev,
			"memshare: Failed memory block allocation\n");
		return -ENOMEM;
	}

	pblk->virtual_addr = dma_alloc_attrs(dev, block_size,
						&pblk->phy_addr, GFP_KERNEL,
						attrs);
	if (pblk->virtual_addr == NULL)
		return -ENOMEM;

	return 0;
}

static void memshare_init_worker(struct work_struct *work)
{
	int rc;

	mem_share_svc_handle = kzalloc(sizeof(struct qmi_handle),
					  GFP_KERNEL);
	if (!mem_share_svc_handle)
		return;

	rc = qmi_handle_init(mem_share_svc_handle,
		sizeof(struct qmi_elem_info),
		&server_ops, qmi_memshare_handlers);
	if (rc < 0) {
		dev_err(memsh_drv->dev,
			"memshare: Creating mem_share_svc qmi handle failed\n");
		kfree(mem_share_svc_handle);
		mem_share_svc_handle = NULL;
		return;
	}
	rc = qmi_add_server(mem_share_svc_handle, MEM_SHARE_SERVICE_SVC_ID,
		MEM_SHARE_SERVICE_VERS, MEM_SHARE_SERVICE_INS_ID);
	if (rc < 0) {
		dev_err(memsh_drv->dev,
			"memshare: Registering mem share svc failed %d\n", rc);
		if (mem_share_svc_handle) {
			qmi_handle_release(mem_share_svc_handle);
			kfree(mem_share_svc_handle);
			mem_share_svc_handle = NULL;
		}
		return;
	}
	dev_dbg(memsh_drv->dev, "memshare: memshare_init successful\n");
}

static int memshare_child_probe(struct platform_device *pdev)
{
	int rc;
	uint32_t size, client_id;
	const char *name;
	struct memshare_child *drv;
	struct device_node *mem_node;

	drv = devm_kzalloc(&pdev->dev, sizeof(struct memshare_child),
							GFP_KERNEL);

	if (!drv)
		return -ENOMEM;

	drv->dev = &pdev->dev;
	platform_set_drvdata(pdev, drv);

	rc = of_property_read_u32(pdev->dev.of_node, "qcom,peripheral-size",
						&size);
	if (rc) {
		dev_err(drv->dev, "memshare: Error reading size of clients, rc: %d\n",
				rc);
		return rc;
	}

	rc = of_property_read_u32(pdev->dev.of_node, "qcom,client-id",
						&client_id);
	if (rc) {
		dev_err(memsh_drv->dev, "memshare: Error reading client id, rc: %d\n",
				rc);
		return rc;
	}

	memblock[num_clients].guarantee = of_property_read_bool(
							pdev->dev.of_node,
							"qcom,allocate-boot-time");

	memblock[num_clients].client_request = of_property_read_bool(
							pdev->dev.of_node,
							"qcom,allocate-on-request");

	memblock[num_clients].guard_band = of_property_read_bool(
							pdev->dev.of_node,
							"qcom,guard-band");

	/* If the shared property is set, allow access from both HLOS and peripheral */
	if (of_property_read_bool(pdev->dev.of_node, "qcom,shared")) {
		memblock[num_clients].hyp_map_info.num_vmids = 2;
		memblock[num_clients].hyp_map_info.vmids[0] = VMID_HLOS;
		memblock[num_clients].hyp_map_info.vmids[1] = VMID_MSS_MSA;
		memblock[num_clients].hyp_map_info.perms[0] = PERM_READ | PERM_WRITE;
		memblock[num_clients].hyp_map_info.perms[1] = PERM_READ | PERM_WRITE;

	} else {
		memblock[num_clients].hyp_map_info.num_vmids = 1;
		memblock[num_clients].hyp_map_info.vmids[0] = VMID_MSS_MSA;
		memblock[num_clients].hyp_map_info.perms[0] = PERM_READ | PERM_WRITE;
	}

	rc = of_property_read_string(pdev->dev.of_node, "label",
						&name);
	if (rc) {
		dev_err(memsh_drv->dev, "memshare: Error reading peripheral info for client, rc: %d\n",
					rc);
		return rc;
	}

	if (strcmp(name, "modem") == 0)
		memblock[num_clients].peripheral = DHMS_MEM_PROC_MPSS_V01;
	else if (strcmp(name, "adsp") == 0)
		memblock[num_clients].peripheral = DHMS_MEM_PROC_ADSP_V01;
	else if (strcmp(name, "wcnss") == 0)
		memblock[num_clients].peripheral = DHMS_MEM_PROC_WCNSS_V01;

	memblock[num_clients].init_size = size;
	memblock[num_clients].client_id = client_id;
	drv->client_id = client_id;

	mem_node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	of_node_put(mem_node);
	if (mem_node) {
		rc = of_reserved_mem_device_init(&pdev->dev);
		if (rc) {
			dev_err(&pdev->dev, "memshare: Failed to initialize memory region rc: %d\n",
				rc);
			return rc;
		}
		dev_info(&pdev->dev, "memshare: Memory allocation from shared DMA pool\n");
	} else {
		dev_info(&pdev->dev, "memshare: Continuing with allocation from CMA\n");
	}

	/*
	 * Memshare allocation for guaranteed clients
	 */
	if (memblock[num_clients].guarantee && size > 0) {
		if (memblock[num_clients].guard_band)
			size += MEMSHARE_GUARD_BYTES;
		rc = memshare_alloc(drv->dev,
				size,
				&memblock[num_clients]);
		if (rc) {
			dev_err(memsh_drv->dev,
				"memshare_child: Unable to allocate memory for guaranteed clients, rc: %d\n",
				rc);
			mem_node = of_parse_phandle(pdev->dev.of_node,
						    "memory-region", 0);
			of_node_put(mem_node);
			if (mem_node)
				of_reserved_mem_device_release(&pdev->dev);
			return rc;
		}
		memblock[num_clients].size = size;
		memblock[num_clients].allotted = 1;
		shared_hyp_mapping(num_clients);
	}

	memsh_child[num_clients] = drv;
	num_clients++;

	return 0;
}

static int memshare_probe(struct platform_device *pdev)
{
	int rc;
	struct memshare_driver *drv;
	struct device *dev = &pdev->dev;

	if (of_device_is_compatible(dev->of_node,
					"qcom,memshare-peripheral"))
		return memshare_child_probe(pdev);

	drv = devm_kzalloc(&pdev->dev, sizeof(struct memshare_driver),
							GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->dev = &pdev->dev;
	memsh_drv = drv;
	platform_set_drvdata(pdev, memsh_drv);

	/* Memory allocation has been done successfully */
	mutex_init(&drv->mem_free);
	mutex_init(&drv->mem_share);

	INIT_WORK(&drv->memshare_init_work, memshare_init_worker);
	schedule_work(&drv->memshare_init_work);

	initialize_client();
	num_clients = 0;

	rc = of_platform_populate(pdev->dev.of_node, NULL, NULL,
				&pdev->dev);

	if (rc) {
		dev_err(memsh_drv->dev,
			"memshare: error populating the devices\n");
		return rc;
	}

	qcom_register_ssr_notifier("modem", &nb);
	dev_dbg(memsh_drv->dev, "memshare: Memshare inited\n");

	return 0;
}

static int memshare_remove(struct platform_device *pdev)
{
	if (!memsh_drv)
		return 0;

	if (mem_share_svc_handle) {
		qmi_handle_release(mem_share_svc_handle);
		kfree(mem_share_svc_handle);
		mem_share_svc_handle = NULL;
	}
	return 0;
}

static const struct of_device_id memshare_match_table[] = {
	{ .compatible = "qcom,memshare", },
	{ .compatible = "qcom,memshare-peripheral", },
	{}
};

MODULE_DEVICE_TABLE(of, memshare_match_table);

static struct platform_driver memshare_pdriver = {
	.probe          = memshare_probe,
	.remove         = memshare_remove,
	.driver = {
		.name   = "memshare",
		.of_match_table = memshare_match_table,
	},
};

module_platform_driver(memshare_pdriver);

MODULE_DESCRIPTION("Mem Share QMI Service Driver");
MODULE_LICENSE("GPL");
