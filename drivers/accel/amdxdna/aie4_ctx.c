// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>
#include <linux/types.h>

#include "aie.h"
#include "aie4_host_queue.h"
#include "aie4_msg_priv.h"
#include "aie4_pci.h"
#include "amdxdna_ctx.h"
#include "amdxdna_gem.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_mailbox_helper.h"
#include "amdxdna_pci_drv.h"

static irqreturn_t cert_comp_isr(int irq, void *p)
{
	struct cert_comp *cert_comp = p;

	wake_up_all(&cert_comp->waitq);
	return IRQ_HANDLED;
}

static struct cert_comp *aie4_lookup_cert_comp(struct amdxdna_dev_hdl *ndev, u32 msix_idx)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	struct cert_comp *cert_comp;
	int ret;

	guard(mutex)(&ndev->cert_comp_lock);

	cert_comp = xa_load(&ndev->cert_comp_xa, msix_idx);
	if (cert_comp) {
		kref_get(&cert_comp->kref);
		return cert_comp;
	}

	cert_comp = kzalloc_obj(*cert_comp);
	if (!cert_comp)
		return NULL;

	cert_comp->ndev = ndev;
	cert_comp->msix_idx = msix_idx;
	init_waitqueue_head(&cert_comp->waitq);
	kref_init(&cert_comp->kref);

	ret = pci_irq_vector(pdev, cert_comp->msix_idx);
	if (ret < 0) {
		XDNA_ERR(xdna, "MSI-X idx %u is invalid, ret:%d", msix_idx, ret);
		goto free_cert_comp;
	}
	cert_comp->irq = ret;

	ret = request_irq(cert_comp->irq, cert_comp_isr, 0, "xdna_hsa", cert_comp);
	if (ret) {
		XDNA_ERR(xdna, "request irq %d failed %d", cert_comp->irq, ret);
		goto free_cert_comp;
	}

	ret = xa_err(xa_store(&ndev->cert_comp_xa, msix_idx, cert_comp, GFP_KERNEL));
	if (ret) {
		XDNA_ERR(xdna, "store cert_comp for msix idx %d failed %d", msix_idx, ret);
		goto free_irq;
	}

	return cert_comp;

free_irq:
	free_irq(cert_comp->irq, cert_comp);
free_cert_comp:
	kfree(cert_comp);
	return NULL;
}

static void cert_comp_release(struct kref *kref)
{
	struct cert_comp *cert_comp = container_of(kref, struct cert_comp, kref);
	struct amdxdna_dev_hdl *ndev = cert_comp->ndev;

	drm_WARN_ON(&ndev->aie.xdna->ddev, !mutex_is_locked(&ndev->cert_comp_lock));

	xa_erase(&ndev->cert_comp_xa, cert_comp->msix_idx);
	free_irq(cert_comp->irq, cert_comp);
	kfree(cert_comp);
}

static void aie4_put_cert_comp(struct cert_comp *cert_comp)
{
	struct amdxdna_dev_hdl *ndev;

	ndev = cert_comp->ndev;
	guard(mutex)(&ndev->cert_comp_lock);
	kref_put(&cert_comp->kref, cert_comp_release);
}

static int aie4_msg_destroy_context(struct amdxdna_dev_hdl *ndev, u32 hw_context_id)
{
	DECLARE_AIE_MSG(aie4_msg_destroy_hw_context, AIE4_MSG_OP_DESTROY_HW_CONTEXT);

	req.hw_context_id = hw_context_id;
	return aie_send_mgmt_msg_wait(&ndev->aie, &msg);
}

static int aie4_hwctx_create(struct amdxdna_hwctx *hwctx)
{
	DECLARE_AIE_MSG(aie4_msg_create_hw_context, AIE4_MSG_OP_CREATE_HW_CONTEXT);
	struct amdxdna_client *client = hwctx->client;
	struct amdxdna_hwctx_priv *priv = hwctx->priv;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	int ret;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	if (!ndev->partition_id || !hwctx->num_tiles) {
		XDNA_ERR(xdna, "invalid request partition_id %d, num_tiles %d",
			 ndev->partition_id, hwctx->num_tiles);
		return -EINVAL;
	}

	req.partition_id = ndev->partition_id;
	req.request_num_tiles = hwctx->num_tiles;
	req.pasid = FIELD_PREP(AIE4_MSG_PASID, client->pasid) |
		FIELD_PREP(AIE4_MSG_PASID_VLD, 1);
	req.priority_band = hwctx->qos.priority;

	req.hsa_addr_high = upper_32_bits(amdxdna_gem_dev_addr(priv->umq_bo));
	req.hsa_addr_low = lower_32_bits(amdxdna_gem_dev_addr(priv->umq_bo));

	XDNA_DBG(xdna, "pasid 0x%x, num_tiles %d, hsa[0x%x 0x%x]",
		 req.pasid, req.request_num_tiles, req.hsa_addr_high, req.hsa_addr_low);

	ret = aie_send_mgmt_msg_wait(&ndev->aie, &msg);
	if (ret) {
		XDNA_ERR(xdna, "create ctx failed: %d", ret);
		return ret;
	}

	XDNA_DBG(xdna, "resp msix: %d, ctx id: %d, doorbell: %d",
		 resp.job_complete_msix_idx,
		 resp.hw_context_id,
		 resp.doorbell_offset);

	/* setup interrupt completion per msix index */
	priv->cert_comp = aie4_lookup_cert_comp(ndev, resp.job_complete_msix_idx);
	if (!priv->cert_comp) {
		aie4_msg_destroy_context(ndev, resp.hw_context_id);
		return -EINVAL;
	}

	priv->hw_ctx_id = resp.hw_context_id;
	hwctx->doorbell_offset = resp.doorbell_offset;

	return 0;
}

static void aie4_hwctx_destroy(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_client *client = hwctx->client;
	struct amdxdna_hwctx_priv *priv = hwctx->priv;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	aie4_msg_destroy_context(ndev, priv->hw_ctx_id);
	aie4_put_cert_comp(priv->cert_comp);
}

static void aie4_hwctx_umq_fini(struct amdxdna_hwctx *hwctx)
{
	if (hwctx->priv && hwctx->priv->umq_bo)
		amdxdna_gem_put_obj(hwctx->priv->umq_bo);
}

static int aie4_hwctx_umq_init(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_hwctx_priv *priv = hwctx->priv;
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	struct amdxdna_gem_obj *umq_bo;
	struct host_queue_header *qhdr;
	int ret;

	umq_bo = amdxdna_gem_get_obj(hwctx->client, hwctx->umq_bo_hdl, AMDXDNA_BO_SHARE);
	if (!umq_bo) {
		XDNA_ERR(xdna, "cannot find umq_bo handle %d", hwctx->umq_bo_hdl);
		return -ENOENT;
	}
	if (umq_bo->mem.size < sizeof(*qhdr)) {
		XDNA_ERR(xdna, "umq_bo size is too small");
		ret = -EINVAL;
		goto put_umq_bo;
	}

	/* get kva address for host queue read index and write index */
	qhdr = amdxdna_gem_vmap(umq_bo);
	if (!qhdr) {
		ret = -ENOMEM;
		goto put_umq_bo;
	}

	priv->umq_bo = umq_bo;
	priv->umq_read_index = &qhdr->read_index;
	priv->umq_write_index = &qhdr->write_index;

	return 0;

put_umq_bo:
	amdxdna_gem_put_obj(umq_bo);
	return ret;
}

int aie4_hwctx_init(struct amdxdna_hwctx *hwctx)
{
	struct amdxdna_client *client = hwctx->client;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_hwctx_priv *priv;
	int ret;

	priv = kzalloc_obj(*priv);
	if (!priv)
		return -ENOMEM;
	hwctx->priv = priv;

	ret = aie4_hwctx_umq_init(hwctx);
	if (ret)
		goto free_priv;

	ret = aie4_hwctx_create(hwctx);
	if (ret)
		goto umq_fini;

	XDNA_DBG(xdna, "hwctx %s init completed", hwctx->name);
	return 0;

umq_fini:
	aie4_hwctx_umq_fini(hwctx);
free_priv:
	kfree(priv);
	hwctx->priv = NULL;
	return ret;
}

void aie4_hwctx_fini(struct amdxdna_hwctx *hwctx)
{
	aie4_hwctx_destroy(hwctx);
	aie4_hwctx_umq_fini(hwctx);
	kfree(hwctx->priv);
}

static inline bool valid_queue_index(u64 read, u64 write, u32 capacity)
{
	return (write >= read) && ((write - read) <= capacity);
}

static u64 get_read_index(struct amdxdna_hwctx *hwctx)
{
	u64 wi = READ_ONCE(*hwctx->priv->umq_write_index);
	u64 ri = READ_ONCE(*hwctx->priv->umq_read_index);
	struct amdxdna_dev *xdna = hwctx->client->xdna;

	/*
	 * CERT cannot update read index as uint64 atomically. Driver may read
	 * half-updated read index when it has bits in high 32bit. In case read
	 * index is not valid, wait for some time and retry once. It should
	 * allow CERT to complete the read index update.
	 */
	if (!valid_queue_index(ri, wi, CTX_MAX_CMDS)) {
		XDNA_WARN(xdna, "Invalid index, ri %llu, wi %llu", ri, wi);
		usleep_range(100, 200);
		ri = READ_ONCE(*hwctx->priv->umq_read_index);
		if (!valid_queue_index(ri, wi, CTX_MAX_CMDS)) {
			XDNA_ERR(xdna, "Invalid index after retry, ri %llu, wi %llu", ri, wi);
			ri = 0;
		}
	}

	return ri;
}

static inline bool check_cmd_done(struct amdxdna_hwctx *hwctx, u64 seq)
{
	u64 read_idx = get_read_index(hwctx);

	return read_idx > seq;
}

int aie4_cmd_wait(struct amdxdna_hwctx *hwctx, u64 seq, u32 timeout)
{
	unsigned long wait_jifs = MAX_SCHEDULE_TIMEOUT;
	struct amdxdna_hwctx_priv *priv = hwctx->priv;
	struct cert_comp *cert_comp = priv->cert_comp;
	long ret;

	if (timeout)
		wait_jifs = msecs_to_jiffies(timeout);

	ret = wait_event_interruptible_timeout(cert_comp->waitq,
					       (check_cmd_done(hwctx, seq)),
					       wait_jifs);

	if (!ret)
		ret = -ETIME;

	return ret <= 0 ? ret : 0;
}

int aie4_hwctx_valid_doorbell(struct amdxdna_client *client, u32 vm_pgoff)
{
	struct amdxdna_hwctx *hwctx;
	unsigned long hwctx_id;
	int idx;

	idx = srcu_read_lock(&client->hwctx_srcu);
	amdxdna_for_each_hwctx(client, hwctx_id, hwctx) {
		if (vm_pgoff == (hwctx->doorbell_offset >> PAGE_SHIFT)) {
			srcu_read_unlock(&client->hwctx_srcu, idx);
			return 1;
		}
	}
	srcu_read_unlock(&client->hwctx_srcu, idx);

	return 0;
}
