// SPDX-License-Identifier: GPL-2.0
/* Marvell IPSEC offload driver
 *
 * Copyright (C) 2024 Marvell.
 */

#include <net/xfrm.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>

#include "otx2_common.h"
#include "cn10k_ipsec.h"

static bool is_dev_support_ipsec_offload(struct pci_dev *pdev)
{
	return is_dev_cn10ka_b0(pdev) || is_dev_cn10kb(pdev);
}

static bool cn10k_cpt_device_set_inuse(struct otx2_nic *pf)
{
	enum cn10k_cpt_hw_state_e state;

	while (true) {
		state = atomic_cmpxchg(&pf->ipsec.cpt_state,
				       CN10K_CPT_HW_AVAILABLE,
				       CN10K_CPT_HW_IN_USE);
		if (state == CN10K_CPT_HW_AVAILABLE)
			return true;
		if (state == CN10K_CPT_HW_UNAVAILABLE)
			return false;

		mdelay(1);
	}
}

static void cn10k_cpt_device_set_available(struct otx2_nic *pf)
{
	atomic_set(&pf->ipsec.cpt_state, CN10K_CPT_HW_AVAILABLE);
}

static void  cn10k_cpt_device_set_unavailable(struct otx2_nic *pf)
{
	atomic_set(&pf->ipsec.cpt_state, CN10K_CPT_HW_UNAVAILABLE);
}

static int cn10k_outb_cptlf_attach(struct otx2_nic *pf)
{
	struct rsrc_attach *attach;
	int ret = -ENOMEM;

	mutex_lock(&pf->mbox.lock);
	/* Get memory to put this msg */
	attach = otx2_mbox_alloc_msg_attach_resources(&pf->mbox);
	if (!attach)
		goto unlock;

	attach->cptlfs = true;
	attach->modify = true;

	/* Send attach request to AF */
	ret = otx2_sync_mbox_msg(&pf->mbox);

unlock:
	mutex_unlock(&pf->mbox.lock);
	return ret;
}

static int cn10k_outb_cptlf_detach(struct otx2_nic *pf)
{
	struct rsrc_detach *detach;
	int ret = -ENOMEM;

	mutex_lock(&pf->mbox.lock);
	detach = otx2_mbox_alloc_msg_detach_resources(&pf->mbox);
	if (!detach)
		goto unlock;

	detach->partial = true;
	detach->cptlfs = true;

	/* Send detach request to AF */
	ret = otx2_sync_mbox_msg(&pf->mbox);

unlock:
	mutex_unlock(&pf->mbox.lock);
	return ret;
}

static int cn10k_outb_cptlf_alloc(struct otx2_nic *pf)
{
	struct cpt_lf_alloc_req_msg *req;
	int ret = -ENOMEM;

	mutex_lock(&pf->mbox.lock);
	req = otx2_mbox_alloc_msg_cpt_lf_alloc(&pf->mbox);
	if (!req)
		goto unlock;

	/* PF function */
	req->nix_pf_func = pf->pcifunc;
	/* Enable SE-IE Engine Group */
	req->eng_grpmsk = 1 << CN10K_DEF_CPT_IPSEC_EGRP;

	ret = otx2_sync_mbox_msg(&pf->mbox);

unlock:
	mutex_unlock(&pf->mbox.lock);
	return ret;
}

static void cn10k_outb_cptlf_free(struct otx2_nic *pf)
{
	mutex_lock(&pf->mbox.lock);
	otx2_mbox_alloc_msg_cpt_lf_free(&pf->mbox);
	otx2_sync_mbox_msg(&pf->mbox);
	mutex_unlock(&pf->mbox.lock);
}

static int cn10k_outb_cptlf_config(struct otx2_nic *pf)
{
	struct cpt_inline_ipsec_cfg_msg *req;
	int ret = -ENOMEM;

	mutex_lock(&pf->mbox.lock);
	req = otx2_mbox_alloc_msg_cpt_inline_ipsec_cfg(&pf->mbox);
	if (!req)
		goto unlock;

	req->dir = CPT_INLINE_OUTBOUND;
	req->enable = 1;
	req->nix_pf_func = pf->pcifunc;
	ret = otx2_sync_mbox_msg(&pf->mbox);
unlock:
	mutex_unlock(&pf->mbox.lock);
	return ret;
}

static void cn10k_outb_cptlf_iq_enable(struct otx2_nic *pf)
{
	u64 reg_val;

	/* Set Execution Enable of instruction queue */
	reg_val = otx2_read64(pf, CN10K_CPT_LF_INPROG);
	reg_val |= BIT_ULL(16);
	otx2_write64(pf, CN10K_CPT_LF_INPROG, reg_val);

	/* Set iqueue's enqueuing */
	reg_val = otx2_read64(pf, CN10K_CPT_LF_CTL);
	reg_val |= BIT_ULL(0);
	otx2_write64(pf, CN10K_CPT_LF_CTL, reg_val);
}

static void cn10k_outb_cptlf_iq_disable(struct otx2_nic *pf)
{
	u32 inflight, grb_cnt, gwb_cnt;
	u32 nq_ptr, dq_ptr;
	int timeout = 20;
	u64 reg_val;
	int cnt;

	/* Disable instructions enqueuing */
	otx2_write64(pf, CN10K_CPT_LF_CTL, 0ull);

	/* Wait for instruction queue to become empty.
	 * CPT_LF_INPROG.INFLIGHT count is zero
	 */
	do {
		reg_val = otx2_read64(pf, CN10K_CPT_LF_INPROG);
		inflight = FIELD_GET(CPT_LF_INPROG_INFLIGHT, reg_val);
		if (!inflight)
			break;

		usleep_range(10000, 20000);
		if (timeout-- < 0) {
			netdev_err(pf->netdev, "Timeout to cleanup CPT IQ\n");
			break;
		}
	} while (1);

	/* Disable executions in the LF's queue,
	 * the queue should be empty at this point
	 */
	reg_val &= ~BIT_ULL(16);
	otx2_write64(pf, CN10K_CPT_LF_INPROG, reg_val);

	/* Wait for instruction queue to become empty */
	cnt = 0;
	do {
		reg_val = otx2_read64(pf, CN10K_CPT_LF_INPROG);
		if (reg_val & BIT_ULL(31))
			cnt = 0;
		else
			cnt++;
		reg_val = otx2_read64(pf, CN10K_CPT_LF_Q_GRP_PTR);
		nq_ptr = FIELD_GET(CPT_LF_Q_GRP_PTR_DQ_PTR, reg_val);
		dq_ptr = FIELD_GET(CPT_LF_Q_GRP_PTR_DQ_PTR, reg_val);
	} while ((cnt < 10) && (nq_ptr != dq_ptr));

	cnt = 0;
	do {
		reg_val = otx2_read64(pf, CN10K_CPT_LF_INPROG);
		inflight = FIELD_GET(CPT_LF_INPROG_INFLIGHT, reg_val);
		grb_cnt = FIELD_GET(CPT_LF_INPROG_GRB_CNT, reg_val);
		gwb_cnt = FIELD_GET(CPT_LF_INPROG_GWB_CNT, reg_val);
		if (inflight == 0 && gwb_cnt < 40 &&
		    (grb_cnt == 0 || grb_cnt == 40))
			cnt++;
		else
			cnt = 0;
	} while (cnt < 10);
}

/* Allocate memory for CPT outbound Instruction queue.
 * Instruction queue memory format is:
 *      -----------------------------
 *     | Instruction Group memory    |
 *     |  (CPT_LF_Q_SIZE[SIZE_DIV40] |
 *     |   x 16 Bytes)               |
 *     |                             |
 *      ----------------------------- <-- CPT_LF_Q_BASE[ADDR]
 *     | Flow Control (128 Bytes)    |
 *     |                             |
 *      -----------------------------
 *     |  Instruction Memory         |
 *     |  (CPT_LF_Q_SIZE[SIZE_DIV40] |
 *     |   × 40 × 64 bytes)          |
 *     |                             |
 *      -----------------------------
 */
static int cn10k_outb_cptlf_iq_alloc(struct otx2_nic *pf)
{
	struct cn10k_cpt_inst_queue *iq = &pf->ipsec.iq;

	iq->size = CN10K_CPT_INST_QLEN_BYTES + CN10K_CPT_Q_FC_LEN +
		    CN10K_CPT_INST_GRP_QLEN_BYTES + OTX2_ALIGN;

	iq->real_vaddr = dma_alloc_coherent(pf->dev, iq->size,
					    &iq->real_dma_addr, GFP_KERNEL);
	if (!iq->real_vaddr)
		return -ENOMEM;

	/* iq->vaddr/dma_addr points to Flow Control location */
	iq->vaddr = iq->real_vaddr + CN10K_CPT_INST_GRP_QLEN_BYTES;
	iq->dma_addr = iq->real_dma_addr + CN10K_CPT_INST_GRP_QLEN_BYTES;

	/* Align pointers */
	iq->vaddr = PTR_ALIGN(iq->vaddr, OTX2_ALIGN);
	iq->dma_addr = PTR_ALIGN(iq->dma_addr, OTX2_ALIGN);
	return 0;
}

static void cn10k_outb_cptlf_iq_free(struct otx2_nic *pf)
{
	struct cn10k_cpt_inst_queue *iq = &pf->ipsec.iq;

	if (iq->real_vaddr)
		dma_free_coherent(pf->dev, iq->size, iq->real_vaddr,
				  iq->real_dma_addr);

	iq->real_vaddr = NULL;
	iq->vaddr = NULL;
}

static int cn10k_outb_cptlf_iq_init(struct otx2_nic *pf)
{
	u64 reg_val;
	int ret;

	/* Allocate Memory for CPT IQ */
	ret = cn10k_outb_cptlf_iq_alloc(pf);
	if (ret)
		return ret;

	/* Disable IQ */
	cn10k_outb_cptlf_iq_disable(pf);

	/* Set IQ base address */
	otx2_write64(pf, CN10K_CPT_LF_Q_BASE, pf->ipsec.iq.dma_addr);

	/* Set IQ size */
	reg_val = FIELD_PREP(CPT_LF_Q_SIZE_DIV40, CN10K_CPT_SIZE_DIV40 +
			     CN10K_CPT_EXTRA_SIZE_DIV40);
	otx2_write64(pf, CN10K_CPT_LF_Q_SIZE, reg_val);

	return 0;
}

static int cn10k_outb_cptlf_init(struct otx2_nic *pf)
{
	int ret;

	/* Initialize CPTLF Instruction Queue (IQ) */
	ret = cn10k_outb_cptlf_iq_init(pf);
	if (ret)
		return ret;

	/* Configure CPTLF for outbound ipsec offload */
	ret = cn10k_outb_cptlf_config(pf);
	if (ret)
		goto iq_clean;

	/* Enable CPTLF IQ */
	cn10k_outb_cptlf_iq_enable(pf);
	return 0;
iq_clean:
	cn10k_outb_cptlf_iq_free(pf);
	return ret;
}

static int cn10k_outb_cpt_init(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	int ret;

	/* Attach a CPT LF for outbound ipsec offload */
	ret = cn10k_outb_cptlf_attach(pf);
	if (ret)
		return ret;

	/* Allocate a CPT LF for outbound ipsec offload */
	ret = cn10k_outb_cptlf_alloc(pf);
	if (ret)
		goto detach;

	/* Initialize the CPTLF for outbound ipsec offload */
	ret = cn10k_outb_cptlf_init(pf);
	if (ret)
		goto lf_free;

	pf->ipsec.io_addr = (__force u64)otx2_get_regaddr(pf,
						CN10K_CPT_LF_NQX(0));

	/* Set ipsec offload enabled for this device */
	pf->flags |= OTX2_FLAG_IPSEC_OFFLOAD_ENABLED;

	cn10k_cpt_device_set_available(pf);
	return 0;

lf_free:
	cn10k_outb_cptlf_free(pf);
detach:
	cn10k_outb_cptlf_detach(pf);
	return ret;
}

static int cn10k_outb_cpt_clean(struct otx2_nic *pf)
{
	int ret;

	if (!cn10k_cpt_device_set_inuse(pf)) {
		netdev_err(pf->netdev, "CPT LF device unavailable\n");
		return -ENODEV;
	}

	/* Set ipsec offload disabled for this device */
	pf->flags &= ~OTX2_FLAG_IPSEC_OFFLOAD_ENABLED;

	/* Disable CPTLF Instruction Queue (IQ) */
	cn10k_outb_cptlf_iq_disable(pf);

	/* Set IQ base address and size to 0 */
	otx2_write64(pf, CN10K_CPT_LF_Q_BASE, 0);
	otx2_write64(pf, CN10K_CPT_LF_Q_SIZE, 0);

	/* Free CPTLF IQ */
	cn10k_outb_cptlf_iq_free(pf);

	/* Free and detach CPT LF */
	cn10k_outb_cptlf_free(pf);
	ret = cn10k_outb_cptlf_detach(pf);
	if (ret)
		netdev_err(pf->netdev, "Failed to detach CPT LF\n");

	cn10k_cpt_device_set_unavailable(pf);
	return ret;
}

int cn10k_ipsec_ethtool_init(struct net_device *netdev, bool enable)
{
	struct otx2_nic *pf = netdev_priv(netdev);

	/* IPsec offload supported on cn10k */
	if (!is_dev_support_ipsec_offload(pf->pdev))
		return -EOPNOTSUPP;

	/* Initialize CPT for outbound ipsec offload */
	if (enable)
		return cn10k_outb_cpt_init(netdev);

	return cn10k_outb_cpt_clean(pf);
}

int cn10k_ipsec_init(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);

	if (!is_dev_support_ipsec_offload(pf->pdev))
		return 0;

	cn10k_cpt_device_set_unavailable(pf);
	return 0;
}
EXPORT_SYMBOL(cn10k_ipsec_init);

void cn10k_ipsec_clean(struct otx2_nic *pf)
{
	if (!is_dev_support_ipsec_offload(pf->pdev))
		return;

	if (!(pf->flags & OTX2_FLAG_IPSEC_OFFLOAD_ENABLED))
		return;

	cn10k_outb_cpt_clean(pf);
}
EXPORT_SYMBOL(cn10k_ipsec_clean);
