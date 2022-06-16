// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/anon_inodes.h>
#include <linux/gunyah/gh_msgq.h>
#include <linux/kthread.h>
#include <linux/memory_hotplug.h>
#include <linux/module.h>
#include <linux/qcom_dma_heap.h>

#include "../../../../drivers/dma-buf/heaps/qcom_sg_ops.h"
#include "mem-buf-gh.h"
#include "mem-buf-msgq.h"
#include "mem-buf-ids.h"
#include "trace-mem-buf.h"

#define MEM_BUF_MHP_ALIGNMENT (1UL << SUBSECTION_SHIFT)
#define MEM_BUF_TIMEOUT_MS 3500
#define to_rmt_msg(_work) container_of(_work, struct mem_buf_rmt_msg, work)

/* Maintains a list of memory buffers requested from other VMs */
static DEFINE_MUTEX(mem_buf_list_lock);
static LIST_HEAD(mem_buf_list);

/* Data structures for tracking message queue usage. */
static struct workqueue_struct *mem_buf_wq;
static void *mem_buf_msgq_hdl;

/* Maintains a list of memory buffers lent out to other VMs */
static DEFINE_MUTEX(mem_buf_xfer_mem_list_lock);
static LIST_HEAD(mem_buf_xfer_mem_list);

/**
 * struct mem_buf_rmt_msg: Represents a message sent from a remote VM
 * @msg: A pointer to the message buffer
 * @msg_size: The size of the message
 * @work: work structure for dispatching the message processing to a worker
 * thread, so as to not block the message queue receiving thread.
 */
struct mem_buf_rmt_msg {
	void *msg;
	size_t msg_size;
	struct work_struct work;
};

/**
 * struct mem_buf_xfer_mem: Represents a memory buffer lent out or transferred
 * to another VM.
 * @size: The size of the memory buffer
 * @mem_type: The type of memory that was allocated and transferred
 * @mem_type_data: Data associated with the type of memory
 * @mem_sgt: An SG-Table representing the memory transferred
 * @secure_alloc: Denotes if the memory was assigned to the targeted VMs as part
 * of the allocation step
 * @hdl: The memparcel handle associated with the memory
 * @trans_type: The type of memory transfer associated with the memory (donation,
 * share, lend).
 * @entry: List entry for maintaining a list of memory buffers that are lent
 * out.
 * @nr_acl_entries: The number of VMIDs and permissions associated with the
 * memory
 * @dst_vmids: The VMIDs that have access to the memory
 * @dst_perms: The access permissions for the VMIDs that can access the memory
 * @txn_id: The transaction ID that corresponds to this memory buffer
 */
struct mem_buf_xfer_mem {
	size_t size;
	enum mem_buf_mem_type mem_type;
	void *mem_type_data;
	struct sg_table *mem_sgt;
	bool secure_alloc;
	u32 trans_type;
	gh_memparcel_handle_t hdl;
	struct list_head entry;
	u32 nr_acl_entries;
	int *dst_vmids;
	int *dst_perms;
	u32 txn_id;
};

/**
 * struct mem_buf_desc - Internal data structure, which contains information
 * about a particular memory buffer.
 * @size: The size of the memory buffer
 * @acl_desc: A GH ACL descriptor that describes the VMIDs that have access to
 * the memory, as well as the permissions each VMID has.
 * @sgl_desc: An GH SG-List descriptor that describes the IPAs of the memory
 * associated with the memory buffer that was allocated from another VM.
 * @memparcel_hdl: The handle associated with the memparcel that represents the
 * memory buffer.
 * @trans_type: The type of memory transfer associated with the memory (donation,
 * share, lend).
 * @src_mem_type: The type of memory that was allocated on the remote VM
 * @src_data: Memory type specific data used by the remote VM when performing
 * the allocation.
 * @dst_mem_type: The memory type of the memory buffer on the native VM
 * @dst_data: Memory type specific data used by the native VM when adding the
 * memory to the system.
 * @filp: Pointer to the file structure for the membuf
 * @entry: List head for maintaing a list of memory buffers that have been
 * provided by remote VMs.
 * @txn: The transaction associated with a memory buffer
 */
struct mem_buf_desc {
	size_t size;
	struct gh_acl_desc *acl_desc;
	struct gh_sgl_desc *sgl_desc;
	gh_memparcel_handle_t memparcel_hdl;
	u32 trans_type;
	enum mem_buf_mem_type src_mem_type;
	void *src_data;
	enum mem_buf_mem_type dst_mem_type;
	void *dst_data;
	struct file *filp;
	struct list_head entry;
	void *txn;
};

struct mem_buf_xfer_dmaheap_mem {
	char name[MEM_BUF_MAX_DMAHEAP_NAME_LEN];
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attachment;
};

/* Functions invoked when treating allocation requests from other VMs. */
static int mem_buf_rmt_alloc_dmaheap_mem(struct mem_buf_xfer_mem *xfer_mem)
{
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attachment;
	struct sg_table *mem_sgt;
	struct mem_buf_xfer_dmaheap_mem *dmaheap_mem_data = xfer_mem->mem_type_data;
	int flags = O_RDWR | O_CLOEXEC;
	struct dma_heap *heap;
	char *name = dmaheap_mem_data->name;

	pr_debug("%s: Starting DMAHEAP allocation\n", __func__);
	heap = dma_heap_find(name);
	if (!heap) {
		pr_err("%s no such heap %s\n", __func__, name);
		return -EINVAL;
	}

	dmabuf = dma_heap_buffer_alloc(heap, xfer_mem->size, flags, 0);
	if (IS_ERR(dmabuf)) {
		pr_err("%s dmaheap_alloc failure sz: 0x%x heap: %s flags: 0x%x rc: %d\n",
		       __func__, xfer_mem->size, name, flags,
		       PTR_ERR(dmabuf));
		return PTR_ERR(dmabuf);
	}

	attachment = dma_buf_attach(dmabuf, mem_buf_dev);
	if (IS_ERR(attachment)) {
		pr_err("%s dma_buf_attach failure rc: %d\n",  __func__,
		       PTR_ERR(attachment));
		dma_buf_put(dmabuf);
		return PTR_ERR(attachment);
	}

	mem_sgt = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR(mem_sgt)) {
		pr_err("%s dma_buf_map_attachment failure rc: %d\n", __func__,
		       PTR_ERR(mem_sgt));
		dma_buf_detach(dmabuf, attachment);
		dma_buf_put(dmabuf);
		return PTR_ERR(mem_sgt);
	}

	dmaheap_mem_data->dmabuf = dmabuf;
	dmaheap_mem_data->attachment = attachment;
	xfer_mem->mem_sgt = mem_sgt;
	xfer_mem->secure_alloc = false;

	pr_debug("%s: DMAHEAP allocation complete\n", __func__);
	return 0;
}

static int mem_buf_rmt_alloc_mem(struct mem_buf_xfer_mem *xfer_mem)
{
	int ret = -EINVAL;

	if (xfer_mem->mem_type == MEM_BUF_DMAHEAP_MEM_TYPE)
		ret = mem_buf_rmt_alloc_dmaheap_mem(xfer_mem);

	return ret;
}

static void mem_buf_rmt_free_dmaheap_mem(struct mem_buf_xfer_mem *xfer_mem)
{
	struct mem_buf_xfer_dmaheap_mem *dmaheap_mem_data = xfer_mem->mem_type_data;
	struct dma_buf *dmabuf = dmaheap_mem_data->dmabuf;
	struct dma_buf_attachment *attachment = dmaheap_mem_data->attachment;
	struct sg_table *mem_sgt = xfer_mem->mem_sgt;

	pr_debug("%s: Freeing DMAHEAP memory\n", __func__);
	dma_buf_unmap_attachment(attachment, mem_sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(dmabuf, attachment);
	dma_buf_put(dmaheap_mem_data->dmabuf);
	pr_debug("%s: DMAHEAP memory freed\n", __func__);
}

static void mem_buf_rmt_free_mem(struct mem_buf_xfer_mem *xfer_mem)
{
	if (xfer_mem->mem_type == MEM_BUF_DMAHEAP_MEM_TYPE)
		mem_buf_rmt_free_dmaheap_mem(xfer_mem);
}

static
struct mem_buf_xfer_dmaheap_mem *mem_buf_alloc_dmaheap_xfer_mem_type_data(
								void *rmt_data)
{
	struct mem_buf_xfer_dmaheap_mem *dmaheap_mem_data;

	dmaheap_mem_data = kzalloc(sizeof(*dmaheap_mem_data), GFP_KERNEL);
	if (!dmaheap_mem_data)
		return ERR_PTR(-ENOMEM);

	strscpy(dmaheap_mem_data->name, (char *)rmt_data,
		MEM_BUF_MAX_DMAHEAP_NAME_LEN);
	pr_debug("%s: DMAHEAP source heap: %s\n", __func__,
		dmaheap_mem_data->name);
	return dmaheap_mem_data;
}

static void *mem_buf_alloc_xfer_mem_type_data(enum mem_buf_mem_type type,
					      void *rmt_data)
{
	void *data = ERR_PTR(-EINVAL);

	if (type == MEM_BUF_DMAHEAP_MEM_TYPE)
		data = mem_buf_alloc_dmaheap_xfer_mem_type_data(rmt_data);

	return data;
}

static
void mem_buf_free_dmaheap_xfer_mem_type_data(struct mem_buf_xfer_dmaheap_mem *mem)
{
	kfree(mem);
}

static void mem_buf_free_xfer_mem_type_data(enum mem_buf_mem_type type,
					    void *data)
{
	if (type == MEM_BUF_DMAHEAP_MEM_TYPE)
		mem_buf_free_dmaheap_xfer_mem_type_data(data);
}

static
struct mem_buf_xfer_mem *mem_buf_prep_xfer_mem(void *req_msg)
{
	int ret;
	struct mem_buf_xfer_mem *xfer_mem;
	u32 nr_acl_entries;
	void *arb_payload;
	enum mem_buf_mem_type mem_type;
	void *mem_type_data;

	nr_acl_entries = get_alloc_req_nr_acl_entries(req_msg);
	if (nr_acl_entries != 1)
		return ERR_PTR(-EINVAL);

	arb_payload = get_alloc_req_arb_payload(req_msg);
	if (!arb_payload)
		return ERR_PTR(-EINVAL);

	mem_type = get_alloc_req_src_mem_type(req_msg);

	xfer_mem = kzalloc(sizeof(*xfer_mem), GFP_KERNEL);
	if (!xfer_mem)
		return ERR_PTR(-ENOMEM);

	xfer_mem->txn_id = get_alloc_req_txn_id(req_msg);
	xfer_mem->size = get_alloc_req_size(req_msg);
	xfer_mem->mem_type = mem_type;
	xfer_mem->nr_acl_entries = nr_acl_entries;
	ret = mem_buf_gh_acl_desc_to_vmid_perm_list(get_alloc_req_gh_acl_desc(req_msg),
						    &xfer_mem->dst_vmids,
						    &xfer_mem->dst_perms);
	if (ret) {
		pr_err("%s failed to create VMID and permissions list: %d\n",
		       __func__, ret);
		kfree(xfer_mem);
		return ERR_PTR(ret);
	}
	mem_type_data = mem_buf_alloc_xfer_mem_type_data(mem_type, arb_payload);
	if (IS_ERR(mem_type_data)) {
		pr_err("%s: failed to allocate mem type specific data: %d\n",
		       __func__, PTR_ERR(mem_type_data));
		kfree(xfer_mem->dst_vmids);
		kfree(xfer_mem->dst_perms);
		kfree(xfer_mem);
		return ERR_CAST(mem_type_data);
	}
	xfer_mem->mem_type_data = mem_type_data;
	INIT_LIST_HEAD(&xfer_mem->entry);
	return xfer_mem;
}

static void mem_buf_free_xfer_mem(struct mem_buf_xfer_mem *xfer_mem)
{
	mem_buf_free_xfer_mem_type_data(xfer_mem->mem_type,
					xfer_mem->mem_type_data);
	kfree(xfer_mem->dst_vmids);
	kfree(xfer_mem->dst_perms);
	kfree(xfer_mem);
}

/*
 * @owner_vmid: Owner of the memparcel handle which has @vmids and @perms
 */
static int __maybe_unused mem_buf_get_mem_xfer_type(int *vmids, int *perms,
						    unsigned int nr_acl_entries, int owner_vmid)
{
	u32 i;

	for (i = 0; i < nr_acl_entries; i++)
		if (vmids[i] == owner_vmid &&
		    perms[i] != 0)
			return GH_RM_TRANS_TYPE_SHARE;

	return GH_RM_TRANS_TYPE_LEND;
}

/*
 * @owner_vmid: Owner of the memparcel handle which has @acl_desc
 */
static int mem_buf_get_mem_xfer_type_gh(struct gh_acl_desc *acl_desc, int owner_vmid)
{
	u32 i, nr_acl_entries = acl_desc->n_acl_entries;

	for (i = 0; i < nr_acl_entries; i++)
		if (acl_desc->acl_entries[i].vmid == owner_vmid &&
		    acl_desc->acl_entries[i].perms != 0)
			return GH_RM_TRANS_TYPE_SHARE;

	return GH_RM_TRANS_TYPE_LEND;
}

static struct mem_buf_xfer_mem *mem_buf_process_alloc_req(void *req)
{
	int ret;
	u32 xfer_type;
	struct mem_buf_xfer_mem *xfer_mem;
	struct mem_buf_lend_kernel_arg arg = {0};

	xfer_mem = mem_buf_prep_xfer_mem(req);
	if (IS_ERR(xfer_mem))
		return xfer_mem;

	ret = mem_buf_rmt_alloc_mem(xfer_mem);
	if (ret < 0)
		goto err_rmt_alloc;

	if (!xfer_mem->secure_alloc) {
		xfer_type = get_alloc_req_xfer_type(req);

		arg.nr_acl_entries = xfer_mem->nr_acl_entries;
		arg.vmids = xfer_mem->dst_vmids;
		arg.perms = xfer_mem->dst_perms;
		ret = mem_buf_assign_mem(xfer_type, xfer_mem->mem_sgt, &arg);
		if (ret < 0)
			goto err_assign_mem;

		xfer_mem->hdl = arg.memparcel_hdl;
		xfer_mem->trans_type = xfer_type;
	}

	mutex_lock(&mem_buf_xfer_mem_list_lock);
	list_add(&xfer_mem->entry, &mem_buf_xfer_mem_list);
	mutex_unlock(&mem_buf_xfer_mem_list_lock);

	return xfer_mem;

err_assign_mem:
	if (ret != -EADDRNOTAVAIL)
		mem_buf_rmt_free_mem(xfer_mem);
err_rmt_alloc:
	mem_buf_free_xfer_mem(xfer_mem);
	return ERR_PTR(ret);
}

static void mem_buf_cleanup_alloc_req(struct mem_buf_xfer_mem *xfer_mem,
				      gh_memparcel_handle_t memparcel_hdl)
{
	int ret;

	if (!xfer_mem->secure_alloc) {
		if (memparcel_hdl == xfer_mem->hdl) {
			ret = mem_buf_unassign_mem(xfer_mem->mem_sgt,
						   xfer_mem->dst_vmids,
						   xfer_mem->nr_acl_entries,
						   xfer_mem->hdl);
			if (ret < 0)
				return;
		} else {
			struct gh_sgl_desc *sgl_desc;
			struct gh_acl_desc *acl_desc;
			size_t size;

			size = struct_size(acl_desc, acl_entries, 1);
			acl_desc = kzalloc(size, GFP_KERNEL);
			if (!acl_desc)
				return;

			acl_desc->n_acl_entries = 1;
			acl_desc->acl_entries[0].vmid = VMID_HLOS;
			acl_desc->acl_entries[0].perms = GH_RM_ACL_X | GH_RM_ACL_W | GH_RM_ACL_R;

			sgl_desc = mem_buf_map_mem_s2(GH_RM_TRANS_TYPE_DONATE, &memparcel_hdl,
						      acl_desc, VMID_TUIVM);
			if (IS_ERR(sgl_desc)) {
				kfree(acl_desc);
				return;
			}
			kvfree(sgl_desc);
			kfree(acl_desc);
		}
	}
	mem_buf_rmt_free_mem(xfer_mem);
	mem_buf_free_xfer_mem(xfer_mem);
}

static void mem_buf_alloc_req_work(struct work_struct *work)
{
	struct mem_buf_rmt_msg *rmt_msg = to_rmt_msg(work);
	void *req_msg = rmt_msg->msg;
	void *resp_msg;
	struct mem_buf_xfer_mem *xfer_mem;
	gh_memparcel_handle_t hdl = 0;
	int ret;

	trace_receive_alloc_req(req_msg);
	xfer_mem = mem_buf_process_alloc_req(req_msg);
	if (IS_ERR(xfer_mem)) {
		ret = PTR_ERR(xfer_mem);
		pr_err("%s: failed to process rmt memory alloc request: %d\n",
		       __func__, ret);
		xfer_mem = NULL;
	} else {
		ret = 0;
		hdl = xfer_mem->hdl;
	}

	resp_msg = mem_buf_construct_alloc_resp(req_msg, ret, hdl);
	kfree(rmt_msg->msg);
	kfree(rmt_msg);
	if (IS_ERR(resp_msg))
		goto out_err;

	trace_send_alloc_resp_msg(resp_msg);
	ret = mem_buf_msgq_send(mem_buf_msgq_hdl, resp_msg);
	/*
	 * Free the buffer regardless of the return value as the hypervisor
	 * would have consumed the data in the case of a success.
	 */
	kfree(resp_msg);
	if (ret < 0) {
		pr_err("%s: failed to send memory allocation response rc: %d\n",
		       __func__, ret);
		goto out_err;
	}
	pr_debug("%s: Allocation response sent\n", __func__);
	return;

out_err:
	if (xfer_mem) {
		mutex_lock(&mem_buf_xfer_mem_list_lock);
		list_del(&xfer_mem->entry);
		mutex_unlock(&mem_buf_xfer_mem_list_lock);
		mem_buf_cleanup_alloc_req(xfer_mem, xfer_mem->hdl);
	}
}

static void mem_buf_relinquish_work(struct work_struct *work)
{
	struct mem_buf_xfer_mem *xfer_mem_iter, *tmp, *xfer_mem = NULL;
	struct mem_buf_rmt_msg *rmt_msg = to_rmt_msg(work);
	struct mem_buf_alloc_relinquish *relinquish_msg = rmt_msg->msg;
	u32 relinquish_txn_id = get_relinquish_req_txn_id(relinquish_msg);

	trace_receive_relinquish_msg(relinquish_msg);
	mutex_lock(&mem_buf_xfer_mem_list_lock);
	list_for_each_entry_safe(xfer_mem_iter, tmp, &mem_buf_xfer_mem_list,
				 entry)
		if (xfer_mem_iter->txn_id == relinquish_txn_id) {
			xfer_mem = xfer_mem_iter;
			list_del(&xfer_mem->entry);
			break;
		}
	mutex_unlock(&mem_buf_xfer_mem_list_lock);

	if (xfer_mem)
		mem_buf_cleanup_alloc_req(xfer_mem, relinquish_msg->hdl);
	else
		pr_err("%s: transferred memory with txn_id 0x%x not found\n",
		       __func__, relinquish_txn_id);

	kfree(rmt_msg->msg);
	kfree(rmt_msg);
}

static int mem_buf_alloc_resp_hdlr(void *hdlr_data, void *msg_buf, size_t size, void *out_buf)
{
	struct mem_buf_alloc_resp *alloc_resp = msg_buf;
	struct mem_buf_desc *membuf = out_buf;
	int ret;

	trace_receive_alloc_resp_msg(alloc_resp);
	if (!(mem_buf_capability & MEM_BUF_CAP_CONSUMER)) {
		kfree(msg_buf);
		return -EPERM;
	}

	ret = get_alloc_resp_retval(alloc_resp);
	if (ret < 0) {
		pr_err("%s remote allocation failed rc: %d\n", __func__, ret);
	} else {
		membuf->memparcel_hdl = get_alloc_resp_hdl(alloc_resp);
	}

	kfree(msg_buf);
	return ret;
}

/* Functions invoked when treating allocation requests to other VMs. */
static void mem_buf_alloc_req_hdlr(void *hdlr_data, void *buf, size_t size)
{
	struct mem_buf_rmt_msg *rmt_msg;

	if (!(mem_buf_capability & MEM_BUF_CAP_SUPPLIER)) {
		kfree(buf);
		return;
	}

	rmt_msg = kmalloc(sizeof(*rmt_msg), GFP_KERNEL);
	if (!rmt_msg) {
		kfree(buf);
		return;
	}

	rmt_msg->msg = buf;
	rmt_msg->msg_size = size;
	INIT_WORK(&rmt_msg->work, mem_buf_alloc_req_work);
	queue_work(mem_buf_wq, &rmt_msg->work);
}

static void mem_buf_relinquish_hdlr(void *hdlr_data, void *buf, size_t size)
{
	struct mem_buf_rmt_msg *rmt_msg;

	if (!(mem_buf_capability & MEM_BUF_CAP_SUPPLIER)) {
		kfree(buf);
		return;
	}

	rmt_msg = kmalloc(sizeof(*rmt_msg), GFP_KERNEL);
	if (!rmt_msg) {
		kfree(buf);
		return;
	}

	rmt_msg->msg = buf;
	rmt_msg->msg_size = size;
	INIT_WORK(&rmt_msg->work, mem_buf_relinquish_work);
	queue_work(mem_buf_wq, &rmt_msg->work);
}

static int mem_buf_request_mem(struct mem_buf_desc *membuf)
{
	void *alloc_req_msg;
	int ret;

	alloc_req_msg = mem_buf_construct_alloc_req(membuf->txn, membuf->size, membuf->acl_desc,
						    membuf->src_mem_type, membuf->src_data,
						    membuf->trans_type);
	if (IS_ERR(alloc_req_msg)) {
		ret = PTR_ERR(alloc_req_msg);
		goto out;
	}

	ret = mem_buf_msgq_send(mem_buf_msgq_hdl, alloc_req_msg);

	/*
	 * Free the buffer regardless of the return value as the hypervisor
	 * would have consumed the data in the case of a success.
	 */
	kfree(alloc_req_msg);

	if (ret < 0)
		goto out;

	ret = mem_buf_txn_wait(membuf->txn);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static void __mem_buf_relinquish_mem(u32 txn_id, u32 memparcel_hdl)
{
	void *relinquish_msg;
	int ret;

	relinquish_msg = mem_buf_construct_relinquish_msg(txn_id, memparcel_hdl);
	if (IS_ERR(relinquish_msg))
		return;

	trace_send_relinquish_msg(relinquish_msg);
	ret = mem_buf_msgq_send(mem_buf_msgq_hdl, relinquish_msg);

	/*
	 * Free the buffer regardless of the return value as the hypervisor
	 * would have consumed the data in the case of a success.
	 */
	kfree(relinquish_msg);

	if (ret < 0)
		pr_err("%s failed to send memory relinquish message rc: %d\n",
		       __func__, ret);
	else
		pr_debug("%s: allocation relinquish message sent\n", __func__);
}

/*
 * Check if membuf already has a valid handle. If it doesn't, then create one.
 */
static void mem_buf_relinquish_mem(struct mem_buf_desc *membuf)
{
	int ret;
	int vmids[] = {VMID_HLOS};
	int perms[] = {PERM_READ | PERM_WRITE | PERM_EXEC};
	struct sg_table *sgt;
	struct mem_buf_lend_kernel_arg arg;
	u32 txn_id = mem_buf_retrieve_txn_id(membuf->txn);

	if (membuf->memparcel_hdl != MEM_BUF_MEMPARCEL_INVALID) {
		if (membuf->trans_type != GH_RM_TRANS_TYPE_DONATE) {
			ret = mem_buf_unmap_mem_s2(membuf->memparcel_hdl);
			if (ret)
				return;
		}

		return __mem_buf_relinquish_mem(txn_id, membuf->memparcel_hdl);
	}

	sgt = dup_gh_sgl_desc_to_sgt(membuf->sgl_desc);
	if (IS_ERR(sgt))
		return;

	arg.nr_acl_entries = 1;
	arg.vmids = vmids;
	arg.perms = perms;
	arg.flags = GH_RM_MEM_DONATE_SANITIZE;
	arg.label = 0;

	ret = mem_buf_assign_mem(GH_RM_TRANS_TYPE_DONATE, sgt, &arg);
	if (ret)
		goto err_free_sgt;

	membuf->memparcel_hdl = arg.memparcel_hdl;
	__mem_buf_relinquish_mem(txn_id, membuf->memparcel_hdl);
err_free_sgt:
	sg_free_table(sgt);
	kfree(sgt);
}

static void mem_buf_relinquish_memparcel_hdl(void *hdlr_data, u32 txn_id, gh_memparcel_handle_t hdl)
{
	__mem_buf_relinquish_mem(txn_id, hdl);
}

static int get_mem_buf(void *membuf_desc);
static int mem_buf_add_dmaheap_mem(struct mem_buf_desc *membuf, struct sg_table *sgt,
				   void *dst_data)
{
	char *heap_name = dst_data;

	return carveout_heap_add_memory(heap_name, sgt, (void *)membuf,
					get_mem_buf, mem_buf_put);
}

static int mem_buf_add_mem_type(struct mem_buf_desc *membuf, enum mem_buf_mem_type type,
				void *dst_data, struct sg_table *sgt)
{
	if (type == MEM_BUF_DMAHEAP_MEM_TYPE)
		return mem_buf_add_dmaheap_mem(membuf, sgt, dst_data);

	return -EINVAL;
}

static int mem_buf_add_mem(struct mem_buf_desc *membuf)
{
	int i, ret, nr_sgl_entries;
	struct sg_table sgt;
	struct scatterlist *sgl;
	u64 base, size;

	pr_debug("%s: adding memory to destination\n", __func__);
	nr_sgl_entries = membuf->sgl_desc->n_sgl_entries;
	ret = sg_alloc_table(&sgt, nr_sgl_entries, GFP_KERNEL);
	if (ret)
		return ret;

	for_each_sg(sgt.sgl, sgl, nr_sgl_entries, i) {
		base = membuf->sgl_desc->sgl_entries[i].ipa_base;
		size = membuf->sgl_desc->sgl_entries[i].size;
		sg_set_page(sgl, phys_to_page(base), size, 0);
	}

	ret = mem_buf_add_mem_type(membuf, membuf->dst_mem_type, membuf->dst_data,
				   &sgt);
	if (ret)
		pr_err("%s failed to add memory to destination rc: %d\n",
		       __func__, ret);
	else
		pr_debug("%s: memory added to destination\n", __func__);

	sg_free_table(&sgt);
	return ret;
}

static int mem_buf_remove_dmaheap_mem(struct sg_table *sgt, void *dst_data)
{
	char *heap_name = dst_data;

	return carveout_heap_remove_memory(heap_name, sgt);
}

static int mem_buf_remove_mem_type(enum mem_buf_mem_type type, void *dst_data,
				   struct sg_table *sgt)
{
	if (type == MEM_BUF_DMAHEAP_MEM_TYPE)
		return mem_buf_remove_dmaheap_mem(sgt, dst_data);

	return -EINVAL;
}

static int mem_buf_remove_mem(struct mem_buf_desc *membuf)
{
	int i, ret, nr_sgl_entries;
	struct sg_table sgt;
	struct scatterlist *sgl;
	u64 base, size;

	pr_debug("%s: removing memory from destination\n", __func__);
	nr_sgl_entries = membuf->sgl_desc->n_sgl_entries;
	ret = sg_alloc_table(&sgt, nr_sgl_entries, GFP_KERNEL);
	if (ret)
		return ret;

	for_each_sg(sgt.sgl, sgl, nr_sgl_entries, i) {
		base = membuf->sgl_desc->sgl_entries[i].ipa_base;
		size = membuf->sgl_desc->sgl_entries[i].size;
		sg_set_page(sgl, phys_to_page(base), size, 0);
	}

	ret = mem_buf_remove_mem_type(membuf->dst_mem_type, membuf->dst_data,
				      &sgt);
	if (ret)
		pr_err("%s failed to remove memory from destination rc: %d\n",
		       __func__, ret);
	else
		pr_debug("%s: memory removed from destination\n", __func__);

	sg_free_table(&sgt);
	return ret;
}

static void *mem_buf_retrieve_dmaheap_mem_type_data_user(
				struct mem_buf_dmaheap_data __user *udata)
{
	char *buf;
	int ret;
	struct mem_buf_dmaheap_data data;

	ret = copy_struct_from_user(&data, sizeof(data),
				    udata,
				    sizeof(data));
	if (ret)
		return ERR_PTR(-EINVAL);

	buf = kcalloc(MEM_BUF_MAX_DMAHEAP_NAME_LEN, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	ret = strncpy_from_user(buf, (const void __user *)data.heap_name,
			MEM_BUF_MAX_DMAHEAP_NAME_LEN);
	if (ret < 0 || ret == MEM_BUF_MAX_DMAHEAP_NAME_LEN) {
		kfree(buf);
		return ERR_PTR(-EINVAL);
	}
	return buf;
}

static void *mem_buf_retrieve_mem_type_data_user(enum mem_buf_mem_type mem_type,
						 void __user *mem_type_data)
{
	void *data = ERR_PTR(-EINVAL);

	if (mem_type == MEM_BUF_DMAHEAP_MEM_TYPE)
		data = mem_buf_retrieve_dmaheap_mem_type_data_user(mem_type_data);

	return data;
}

static void *mem_buf_retrieve_dmaheap_mem_type_data(char *dmaheap_name)
{
	return kstrdup(dmaheap_name, GFP_KERNEL);
}

static void *mem_buf_retrieve_mem_type_data(enum mem_buf_mem_type mem_type,
					    void *mem_type_data)
{
	void *data = ERR_PTR(-EINVAL);

	if (mem_type == MEM_BUF_DMAHEAP_MEM_TYPE)
		data = mem_buf_retrieve_dmaheap_mem_type_data(mem_type_data);

	return data;
}

static void mem_buf_free_dmaheap_mem_type_data(char *dmaheap_name)
{
	kfree(dmaheap_name);
}

static void mem_buf_free_mem_type_data(enum mem_buf_mem_type mem_type,
				       void *mem_type_data)
{
	if (mem_type == MEM_BUF_DMAHEAP_MEM_TYPE)
		mem_buf_free_dmaheap_mem_type_data(mem_type_data);
}

static int mem_buf_buffer_release(struct inode *inode, struct file *filp)
{
	struct mem_buf_desc *membuf = filp->private_data;
	int ret;

	mutex_lock(&mem_buf_list_lock);
	list_del(&membuf->entry);
	mutex_unlock(&mem_buf_list_lock);

	pr_debug("%s: Destroyng carveout memory\n", __func__);
	ret = mem_buf_remove_mem(membuf);
	if (ret < 0)
		goto out_free_mem;

	ret = mem_buf_unmap_mem_s1(membuf->sgl_desc);
	if (ret < 0)
		goto out_free_mem;

	mem_buf_relinquish_mem(membuf);

out_free_mem:
	mem_buf_destroy_txn(mem_buf_msgq_hdl, membuf->txn);
	mem_buf_free_mem_type_data(membuf->dst_mem_type, membuf->dst_data);
	mem_buf_free_mem_type_data(membuf->src_mem_type, membuf->src_data);
	kvfree(membuf->sgl_desc);
	kfree(membuf->acl_desc);
	kfree(membuf);
	return ret;
}

static const struct file_operations mem_buf_fops = {
	.release = mem_buf_buffer_release,
};

static bool is_valid_mem_type(enum mem_buf_mem_type mem_type)
{
	return mem_type == MEM_BUF_DMAHEAP_MEM_TYPE;
}

static void *mem_buf_alloc(struct mem_buf_allocation_data *alloc_data)
{
	int ret;
	struct file *filp;
	struct mem_buf_desc *membuf;
	struct gh_sgl_desc *sgl_desc;
	int perms = PERM_READ | PERM_WRITE | PERM_EXEC;

	if (!(mem_buf_capability & MEM_BUF_CAP_CONSUMER))
		return ERR_PTR(-EOPNOTSUPP);

	if (!alloc_data || !alloc_data->size || alloc_data->nr_acl_entries != 1 ||
	    !alloc_data->vmids || !alloc_data->perms ||
	    !is_valid_mem_type(alloc_data->src_mem_type) ||
	    !is_valid_mem_type(alloc_data->dst_mem_type))
		return ERR_PTR(-EINVAL);

	membuf = kzalloc(sizeof(*membuf), GFP_KERNEL);
	if (!membuf)
		return ERR_PTR(-ENOMEM);

	pr_debug("%s: mem buf alloc begin\n", __func__);
	membuf->size = ALIGN(alloc_data->size, MEM_BUF_MHP_ALIGNMENT);
	membuf->acl_desc = mem_buf_vmid_perm_list_to_gh_acl(
				alloc_data->vmids, &perms,
				alloc_data->nr_acl_entries);
	if (IS_ERR(membuf->acl_desc)) {
		ret = PTR_ERR(membuf->acl_desc);
		goto err_alloc_acl_list;
	}
	membuf->trans_type = alloc_data->trans_type;
	membuf->src_mem_type = alloc_data->src_mem_type;
	membuf->dst_mem_type = alloc_data->dst_mem_type;

	membuf->src_data =
		mem_buf_retrieve_mem_type_data(alloc_data->src_mem_type,
					       alloc_data->src_data);
	if (IS_ERR(membuf->src_data)) {
		ret = PTR_ERR(membuf->src_data);
		goto err_alloc_src_data;
	}

	membuf->dst_data =
		mem_buf_retrieve_mem_type_data(alloc_data->dst_mem_type,
					       alloc_data->dst_data);
	if (IS_ERR(membuf->dst_data)) {
		ret = PTR_ERR(membuf->dst_data);
		goto err_alloc_dst_data;
	}

	membuf->txn = mem_buf_init_txn(mem_buf_msgq_hdl, membuf);
	if (IS_ERR(membuf->txn)) {
		ret = PTR_ERR(membuf->txn);
		goto err_init_txn;
	}

	trace_mem_buf_alloc_info(membuf->size, membuf->src_mem_type,
				 membuf->dst_mem_type, membuf->acl_desc);
	ret = mem_buf_request_mem(membuf);
	if (ret)
		goto err_mem_req;

	sgl_desc = mem_buf_map_mem_s2(membuf->trans_type, &membuf->memparcel_hdl,
				      membuf->acl_desc, VMID_HLOS);
	if (IS_ERR(sgl_desc))
		goto err_map_mem_s2;
	membuf->sgl_desc = sgl_desc;

	ret = mem_buf_map_mem_s1(membuf->sgl_desc);
	if (ret)
		goto err_map_mem_s1;

	filp = anon_inode_getfile("membuf", &mem_buf_fops, membuf, O_RDWR);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto err_get_file;
	}
	membuf->filp = filp;

	mutex_lock(&mem_buf_list_lock);
	list_add_tail(&membuf->entry, &mem_buf_list);
	mutex_unlock(&mem_buf_list_lock);

	ret = mem_buf_add_mem(membuf);
	if (ret)
		goto err_add_mem;

	pr_debug("%s: mem buf alloc success\n", __func__);
	return membuf;

err_add_mem:
	fput(filp);
	return ERR_PTR(ret);
err_get_file:
	if (mem_buf_unmap_mem_s1(membuf->sgl_desc) < 0) {
		kvfree(membuf->sgl_desc);
		goto err_mem_req;
	}
err_map_mem_s1:
err_map_mem_s2:
	mem_buf_relinquish_mem(membuf);
	kvfree(membuf->sgl_desc);
err_mem_req:
	mem_buf_destroy_txn(mem_buf_msgq_hdl, membuf->txn);
err_init_txn:
	mem_buf_free_mem_type_data(membuf->dst_mem_type, membuf->dst_data);
err_alloc_dst_data:
	mem_buf_free_mem_type_data(membuf->src_mem_type, membuf->src_data);
err_alloc_src_data:
	kfree(membuf->acl_desc);
err_alloc_acl_list:
	kfree(membuf);
	return ERR_PTR(ret);
}

static int _mem_buf_get_fd(struct file *filp)
{
	int fd;

	if (!filp)
		return -EINVAL;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	fd_install(fd, filp);
	return fd;
}

int mem_buf_get_fd(void *membuf_desc)
{
	struct mem_buf_desc *membuf = membuf_desc;

	if (!membuf_desc)
		return -EINVAL;

	return _mem_buf_get_fd(membuf->filp);
}
EXPORT_SYMBOL(mem_buf_get_fd);

static void _mem_buf_put(struct file *filp)
{
	fput(filp);
}

void mem_buf_put(void *membuf_desc)
{
	struct mem_buf_desc *membuf = membuf_desc;

	if (membuf && membuf->filp)
		_mem_buf_put(membuf->filp);
}
EXPORT_SYMBOL(mem_buf_put);

static bool is_mem_buf_file(struct file *filp)
{
	return filp->f_op == &mem_buf_fops;
}

void *mem_buf_get(int fd)
{
	struct file *filp;

	filp = fget(fd);

	if (!filp)
		return ERR_PTR(-EBADF);

	if (!is_mem_buf_file(filp)) {
		fput(filp);
		return ERR_PTR(-EINVAL);
	}

	return filp->private_data;
}
EXPORT_SYMBOL(mem_buf_get);

static int get_mem_buf(void *membuf_desc)
{
	struct mem_buf_desc *membuf = membuf_desc;

	/*
	 * get_file_rcu used for atomic_long_inc_unless_zero.
	 * It returns 0 if file count is already zero.
	 */
	if (!get_file_rcu(membuf->filp))
		return -EINVAL;

	return 0;
}

static void mem_buf_retrieve_release(struct qcom_sg_buffer *buffer)
{
	sg_free_table(&buffer->sg_table);
	kfree(buffer);
}

struct dma_buf *mem_buf_retrieve(struct mem_buf_retrieve_kernel_arg *arg)
{
	int ret, op;
	struct qcom_sg_buffer *buffer;
	struct gh_acl_desc *acl_desc;
	struct gh_sgl_desc *sgl_desc;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	struct sg_table *sgt;

	if (arg->fd_flags & ~MEM_BUF_VALID_FD_FLAGS)
		return ERR_PTR(-EINVAL);

	if (!arg->nr_acl_entries || !arg->vmids || !arg->perms)
		return ERR_PTR(-EINVAL);

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	acl_desc = mem_buf_vmid_perm_list_to_gh_acl(arg->vmids, arg->perms,
				arg->nr_acl_entries);
	if (IS_ERR(acl_desc)) {
		ret = PTR_ERR(acl_desc);
		goto err_gh_acl;
	}

	op = mem_buf_get_mem_xfer_type_gh(acl_desc, arg->sender_vmid);
	sgl_desc = mem_buf_map_mem_s2(op, &arg->memparcel_hdl, acl_desc, arg->sender_vmid);
	if (IS_ERR(sgl_desc)) {
		ret = PTR_ERR(sgl_desc);
		goto err_map_s2;
	}

	ret = mem_buf_map_mem_s1(sgl_desc);
	if (ret < 0)
		goto err_map_mem_s1;

	sgt = dup_gh_sgl_desc_to_sgt(sgl_desc);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_dup_sgt;
	}
	buffer->sg_table = *sgt;
	kfree(sgt);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = NULL;
	buffer->len = mem_buf_get_sgl_buf_size(sgl_desc);
	buffer->uncached = false;
	buffer->free = mem_buf_retrieve_release;
	buffer->vmperm = mem_buf_vmperm_alloc_accept(&buffer->sg_table,
						     arg->memparcel_hdl);

	exp_info.size = buffer->len;
	exp_info.flags = arg->fd_flags;
	exp_info.priv = buffer;

	dmabuf = mem_buf_dma_buf_export(&exp_info, &qcom_sg_buf_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err_export_dma_buf;
	}

	/* sgt & qcom_sg_buffer will be freed by mem_buf_retrieve_release */
	kvfree(sgl_desc);
	kfree(acl_desc);
	return dmabuf;

err_export_dma_buf:
	sg_free_table(&buffer->sg_table);
err_dup_sgt:
	mem_buf_unmap_mem_s1(sgl_desc);
err_map_mem_s1:
	kvfree(sgl_desc);
	mem_buf_unmap_mem_s2(arg->memparcel_hdl);
err_map_s2:
	kfree(acl_desc);
err_gh_acl:
	kfree(buffer);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(mem_buf_retrieve);

static int mem_buf_prep_alloc_data(struct mem_buf_allocation_data *alloc_data,
				struct mem_buf_alloc_ioctl_arg *allocation_args)
{
	unsigned int nr_acl_entries = allocation_args->nr_acl_entries;
	int ret;

	alloc_data->size = allocation_args->size;
	alloc_data->nr_acl_entries = nr_acl_entries;

	ret = mem_buf_acl_to_vmid_perms_list(nr_acl_entries,
				(const void __user *)allocation_args->acl_list,
				&alloc_data->vmids, &alloc_data->perms);
	if (ret)
		goto err_acl;

	/* alloc_data->trans_type set later according to src&dest_mem_type */
	alloc_data->src_mem_type = allocation_args->src_mem_type;
	alloc_data->dst_mem_type = allocation_args->dst_mem_type;

	alloc_data->src_data =
		mem_buf_retrieve_mem_type_data_user(
						allocation_args->src_mem_type,
				(void __user *)allocation_args->src_data);
	if (IS_ERR(alloc_data->src_data)) {
		ret = PTR_ERR(alloc_data->src_data);
		goto err_alloc_src_data;
	}

	alloc_data->dst_data =
		mem_buf_retrieve_mem_type_data_user(
						allocation_args->dst_mem_type,
				(void __user *)allocation_args->dst_data);
	if (IS_ERR(alloc_data->dst_data)) {
		ret = PTR_ERR(alloc_data->dst_data);
		goto err_alloc_dst_data;
	}

	return 0;

err_alloc_dst_data:
	mem_buf_free_mem_type_data(alloc_data->src_mem_type,
				   alloc_data->src_data);
err_alloc_src_data:
	kfree(alloc_data->vmids);
	kfree(alloc_data->perms);
err_acl:
	return ret;
}

static void mem_buf_free_alloc_data(struct mem_buf_allocation_data *alloc_data)
{
	mem_buf_free_mem_type_data(alloc_data->dst_mem_type,
				   alloc_data->dst_data);
	mem_buf_free_mem_type_data(alloc_data->src_mem_type,
				   alloc_data->src_data);
	kfree(alloc_data->vmids);
	kfree(alloc_data->perms);
}

int mem_buf_alloc_fd(struct mem_buf_alloc_ioctl_arg *allocation_args)
{
	struct mem_buf_desc *membuf;
	struct mem_buf_allocation_data alloc_data;
	int ret;

	if (!allocation_args->size || !allocation_args->nr_acl_entries ||
	    !allocation_args->acl_list ||
	    (allocation_args->nr_acl_entries > MEM_BUF_MAX_NR_ACL_ENTS) ||
	    !is_valid_mem_type(allocation_args->src_mem_type) ||
	    !is_valid_mem_type(allocation_args->dst_mem_type) ||
	    allocation_args->reserved0 || allocation_args->reserved1 ||
	    allocation_args->reserved2)
		return -EINVAL;

	ret = mem_buf_prep_alloc_data(&alloc_data, allocation_args);
	if (ret < 0)
		return ret;

	/* Only donate is supported currently */
	alloc_data.trans_type = GH_RM_TRANS_TYPE_DONATE;
	membuf = mem_buf_alloc(&alloc_data);
	if (IS_ERR(membuf)) {
		ret = PTR_ERR(membuf);
		goto out;
	}

	ret = mem_buf_get_fd(membuf);
	if (ret < 0)
		mem_buf_put(membuf);

out:
	mem_buf_free_alloc_data(&alloc_data);
	return ret;
}

int mem_buf_retrieve_user(struct mem_buf_retrieve_ioctl_arg *uarg)
{
	int ret, fd;
	int *vmids, *perms;
	struct dma_buf *dmabuf;
	struct mem_buf_retrieve_kernel_arg karg = {0};

	if (!uarg->nr_acl_entries || !uarg->acl_list ||
	    uarg->nr_acl_entries > MEM_BUF_MAX_NR_ACL_ENTS ||
	    uarg->reserved0 || uarg->reserved1 ||
	    uarg->reserved2 ||
	    uarg->fd_flags & ~MEM_BUF_VALID_FD_FLAGS)
		return -EINVAL;

	ret = mem_buf_acl_to_vmid_perms_list(uarg->nr_acl_entries,
			(void *)uarg->acl_list, &vmids, &perms);
	if (ret)
		return ret;

	karg.sender_vmid = mem_buf_fd_to_vmid(uarg->sender_vm_fd);
	if (karg.sender_vmid < 0) {
		pr_err_ratelimited("%s: Invalid sender_vmid %d\n", __func__, uarg->sender_vm_fd);
		goto err_sender_vmid;
	}

	karg.nr_acl_entries = uarg->nr_acl_entries;
	karg.vmids = vmids;
	karg.perms = perms;
	karg.memparcel_hdl = uarg->memparcel_hdl;
	karg.fd_flags = uarg->fd_flags;
	dmabuf = mem_buf_retrieve(&karg);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err_retrieve;
	}

	fd = dma_buf_fd(dmabuf, karg.fd_flags);
	if (fd < 0) {
		ret = fd;
		goto err_fd;
	}

	uarg->dma_buf_import_fd = fd;
	kfree(vmids);
	kfree(perms);
	return 0;
err_fd:
	dma_buf_put(dmabuf);
err_sender_vmid:
err_retrieve:
	kfree(vmids);
	kfree(perms);
	return ret;
}

static const struct mem_buf_msgq_ops msgq_ops = {
	.alloc_req_hdlr = mem_buf_alloc_req_hdlr,
	.alloc_resp_hdlr = mem_buf_alloc_resp_hdlr,
	.relinquish_hdlr = mem_buf_relinquish_hdlr,
	.relinquish_memparcel_hdl = mem_buf_relinquish_memparcel_hdl,
};

int mem_buf_msgq_alloc(struct device *dev)
{
	struct mem_buf_msgq_hdlr_info info = {
		.msgq_ops = &msgq_ops,
	};
	int ret;

	/* No msgq if neither a consumer nor a supplier */
	if (!(mem_buf_capability & MEM_BUF_CAP_DUAL))
		return 0;

	mem_buf_wq = alloc_workqueue("mem_buf_wq", WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (!mem_buf_wq) {
		dev_err(dev, "Unable to initialize workqueue\n");
		return -EINVAL;
	}

	mem_buf_msgq_hdl = mem_buf_msgq_register("trusted_vm", &info);
	if (IS_ERR(mem_buf_msgq_hdl)) {
		ret = PTR_ERR(mem_buf_msgq_hdl);
		dev_err(dev, "Unable to register for mem-buf message queue\n");
		goto err_msgq_register;
	}

	return 0;

err_msgq_register:
	destroy_workqueue(mem_buf_wq);
	mem_buf_wq = NULL;
	return ret;
}

void mem_buf_msgq_free(struct device *dev)
{
	if (!(mem_buf_capability & MEM_BUF_CAP_DUAL))
		return;

	mutex_lock(&mem_buf_list_lock);
	if (!list_empty(&mem_buf_list))
		dev_err(mem_buf_dev,
			"Removing mem-buf driver while there are membufs\n");
	mutex_unlock(&mem_buf_list_lock);

	mutex_lock(&mem_buf_xfer_mem_list_lock);
	if (!list_empty(&mem_buf_xfer_mem_list))
		dev_err(mem_buf_dev,
			"Removing mem-buf driver while memory is still lent\n");
	mutex_unlock(&mem_buf_xfer_mem_list_lock);
	mem_buf_msgq_unregister(mem_buf_msgq_hdl);
	mem_buf_msgq_hdl = NULL;
	destroy_workqueue(mem_buf_wq);
	mem_buf_wq = NULL;
}
