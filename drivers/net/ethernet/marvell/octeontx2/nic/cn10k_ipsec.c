// SPDX-License-Identifier: GPL-2.0
/* Marvell IPSEC offload driver
 *
 * Copyright (C) 2024 Marvell.
 */

#include <net/xfrm.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>
#include <crypto/aead.h>
#include <crypto/gcm.h>

#include "otx2_common.h"
#include "otx2_struct.h"
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

static void cn10k_cpt_inst_flush(struct otx2_nic *pf, struct cpt_inst_s *inst,
				 u64 size)
{
	struct otx2_lmt_info *lmt_info;
	u64 val = 0, tar_addr = 0;

	lmt_info = per_cpu_ptr(pf->hw.lmt_info, smp_processor_id());
	/* FIXME: val[0:10] LMT_ID.
	 * [12:15] no of LMTST - 1 in the burst.
	 * [19:63] data size of each LMTST in the burst except first.
	 */
	val = (lmt_info->lmt_id & 0x7FF);
	/* Target address for LMTST flush tells HW how many 128bit
	 * words are present.
	 * tar_addr[6:4] size of first LMTST - 1 in units of 128b.
	 */
	tar_addr |= pf->ipsec.io_addr | (((size / 16) - 1) & 0x7) << 4;
	dma_wmb();
	memcpy((u64 *)lmt_info->lmt_addr, inst, size);
	cn10k_lmt_flush(val, tar_addr);
}

static int cn10k_wait_for_cpt_respose(struct otx2_nic *pf,
				      struct cpt_res_s *res)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	u64 *completion_ptr = (u64 *)res;

	do {
		if (time_after(jiffies, timeout)) {
			netdev_err(pf->netdev, "CPT response timeout\n");
			return -EBUSY;
		}
	} while ((READ_ONCE(*completion_ptr) & CN10K_CPT_COMP_E_MASK) ==
		 CN10K_CPT_COMP_E_NOTDONE);

	if (!(res->compcode == CN10K_CPT_COMP_E_GOOD ||
	      res->compcode == CN10K_CPT_COMP_E_WARN) || res->uc_compcode) {
		netdev_err(pf->netdev, "compcode=%x doneint=%x\n",
			   res->compcode, res->doneint);
		netdev_err(pf->netdev, "uc_compcode=%x uc_info=%llx esn=%llx\n",
			   res->uc_compcode, (u64)res->uc_info, res->esn);
	}
	return 0;
}

static int cn10k_outb_write_sa(struct otx2_nic *pf, struct qmem *sa_info)
{
	dma_addr_t res_iova, dptr_iova, sa_iova;
	struct cn10k_tx_sa_s *sa_dptr;
	struct cpt_inst_s inst = {};
	struct cpt_res_s *res;
	u32 sa_size, off;
	u64 *sptr, *dptr;
	u64 reg_val;
	int ret;

	sa_iova = sa_info->iova;
	if (!sa_iova)
		return -EINVAL;

	res = dma_alloc_coherent(pf->dev, sizeof(struct cpt_res_s),
				 &res_iova, GFP_ATOMIC);
	if (!res)
		return -ENOMEM;

	sa_size = sizeof(struct cn10k_tx_sa_s);
	sa_dptr = dma_alloc_coherent(pf->dev, sa_size, &dptr_iova, GFP_ATOMIC);
	if (!sa_dptr) {
		dma_free_coherent(pf->dev, sizeof(struct cpt_res_s), res,
				  res_iova);
		return -ENOMEM;
	}

	sptr = (__force u64 *)sa_info->base;
	dptr =  (__force u64 *)sa_dptr;
	for (off = 0; off < (sa_size / 8); off++)
		*(dptr + off) = (__force u64)cpu_to_be64(*(sptr + off));

	res->compcode = CN10K_CPT_COMP_E_NOTDONE;
	inst.res_addr = res_iova;
	inst.dptr = (u64)dptr_iova;
	inst.param2 = sa_size >> 3;
	inst.dlen = sa_size;
	inst.opcode_major = CN10K_IPSEC_MAJOR_OP_WRITE_SA;
	inst.opcode_minor = CN10K_IPSEC_MINOR_OP_WRITE_SA;
	inst.cptr = sa_iova;
	inst.ctx_val = 1;
	inst.egrp = CN10K_DEF_CPT_IPSEC_EGRP;

	/* Check if CPT-LF available */
	if (!cn10k_cpt_device_set_inuse(pf)) {
		ret = -ENODEV;
		goto free_mem;
	}

	cn10k_cpt_inst_flush(pf, &inst, sizeof(struct cpt_inst_s));
	dma_wmb();
	ret = cn10k_wait_for_cpt_respose(pf, res);
	if (ret)
		goto set_available;

	/* Trigger CTX flush to write dirty data back to DRAM */
	reg_val = FIELD_PREP(CPT_LF_CTX_FLUSH_CPTR, sa_iova >> 7);
	otx2_write64(pf, CN10K_CPT_LF_CTX_FLUSH, reg_val);

set_available:
	cn10k_cpt_device_set_available(pf);
free_mem:
	dma_free_coherent(pf->dev, sa_size, sa_dptr, dptr_iova);
	dma_free_coherent(pf->dev, sizeof(struct cpt_res_s), res, res_iova);
	return ret;
}

static int cn10k_ipsec_get_hw_ctx_offset(void)
{
	/* Offset on Hardware-context offset in word */
	return (offsetof(struct cn10k_tx_sa_s, hw_ctx) / sizeof(u64)) & 0x7F;
}

static int cn10k_ipsec_get_ctx_push_size(void)
{
	/* Context push size is round up and in multiple of 8 Byte */
	return (roundup(offsetof(struct cn10k_tx_sa_s, hw_ctx), 8) / 8) & 0x7F;
}

static int cn10k_ipsec_get_aes_key_len(int key_len)
{
	/* key_len is aes key length in bytes */
	switch (key_len) {
	case 16:
		return CN10K_IPSEC_SA_AES_KEY_LEN_128;
	case 24:
		return CN10K_IPSEC_SA_AES_KEY_LEN_192;
	default:
		return CN10K_IPSEC_SA_AES_KEY_LEN_256;
	}
}

static void cn10k_outb_prepare_sa(struct xfrm_state *x,
				  struct cn10k_tx_sa_s *sa_entry)
{
	int key_len = (x->aead->alg_key_len + 7) / 8;
	struct net_device *netdev = x->xso.dev;
	u8 *key = x->aead->alg_key;
	struct otx2_nic *pf;
	u32 *tmp_salt;
	u64 *tmp_key;
	int idx;

	memset(sa_entry, 0, sizeof(struct cn10k_tx_sa_s));

	/* context size, 128 Byte aligned up */
	pf = netdev_priv(netdev);
	sa_entry->ctx_size = (pf->ipsec.sa_size / OTX2_ALIGN)  & 0xF;
	sa_entry->hw_ctx_off = cn10k_ipsec_get_hw_ctx_offset();
	sa_entry->ctx_push_size = cn10k_ipsec_get_ctx_push_size();

	/* Ucode to skip two words of CPT_CTX_HW_S */
	sa_entry->ctx_hdr_size = 1;

	/* Allow Atomic operation (AOP) */
	sa_entry->aop_valid = 1;

	/* Outbound, ESP TRANSPORT/TUNNEL Mode, AES-GCM with */
	sa_entry->sa_dir = CN10K_IPSEC_SA_DIR_OUTB;
	sa_entry->ipsec_protocol = CN10K_IPSEC_SA_IPSEC_PROTO_ESP;
	sa_entry->enc_type = CN10K_IPSEC_SA_ENCAP_TYPE_AES_GCM;
	sa_entry->iv_src = CN10K_IPSEC_SA_IV_SRC_PACKET;
	if (x->props.mode == XFRM_MODE_TUNNEL)
		sa_entry->ipsec_mode = CN10K_IPSEC_SA_IPSEC_MODE_TUNNEL;
	else
		sa_entry->ipsec_mode = CN10K_IPSEC_SA_IPSEC_MODE_TRANSPORT;

	/* Last 4 bytes are salt */
	key_len -= 4;
	sa_entry->aes_key_len = cn10k_ipsec_get_aes_key_len(key_len);
	memcpy(sa_entry->cipher_key, key, key_len);
	tmp_key = (u64 *)sa_entry->cipher_key;

	for (idx = 0; idx < key_len / 8; idx++)
		tmp_key[idx] = (__force u64)cpu_to_be64(tmp_key[idx]);

	memcpy(&sa_entry->iv_gcm_salt, key + key_len, 4);
	tmp_salt = (u32 *)&sa_entry->iv_gcm_salt;
	*tmp_salt = (__force u32)cpu_to_be32(*tmp_salt);

	/* Write SA context data to memory before enabling */
	wmb();

	/* Enable SA */
	sa_entry->sa_valid = 1;
}

static int cn10k_ipsec_validate_state(struct xfrm_state *x,
				      struct netlink_ext_ack *extack)
{
	if (x->props.aalgo != SADB_AALG_NONE) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload authenticated xfrm states");
		return -EINVAL;
	}
	if (x->props.ealgo != SADB_X_EALG_AES_GCM_ICV16) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only AES-GCM-ICV16 xfrm state may be offloaded");
		return -EINVAL;
	}
	if (x->props.calgo != SADB_X_CALG_NONE) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload compressed xfrm states");
		return -EINVAL;
	}
	if (x->props.flags & XFRM_STATE_ESN) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload ESN xfrm states");
		return -EINVAL;
	}
	if (x->props.family != AF_INET && x->props.family != AF_INET6) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only IPv4/v6 xfrm states may be offloaded");
		return -EINVAL;
	}
	if (x->xso.type != XFRM_DEV_OFFLOAD_CRYPTO) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload other than crypto-mode");
		return -EINVAL;
	}
	if (x->props.mode != XFRM_MODE_TRANSPORT &&
	    x->props.mode != XFRM_MODE_TUNNEL) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only tunnel/transport xfrm states may be offloaded");
		return -EINVAL;
	}
	if (x->id.proto != IPPROTO_ESP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only ESP xfrm state may be offloaded");
		return -EINVAL;
	}
	if (x->encap) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Encapsulated xfrm state may not be offloaded");
		return -EINVAL;
	}
	if (!x->aead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload xfrm states without aead");
		return -EINVAL;
	}

	if (x->aead->alg_icv_len != 128) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload xfrm states with AEAD ICV length other than 128bit");
		return -EINVAL;
	}
	if (x->aead->alg_key_len != 128 + 32 &&
	    x->aead->alg_key_len != 192 + 32 &&
	    x->aead->alg_key_len != 256 + 32) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload xfrm states with AEAD key length other than 128/192/256bit");
		return -EINVAL;
	}
	if (x->tfcpad) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload xfrm states with tfc padding");
		return -EINVAL;
	}
	if (!x->geniv) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload xfrm states without geniv");
		return -EINVAL;
	}
	if (strcmp(x->geniv, "seqiv")) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload xfrm states with geniv other than seqiv");
		return -EINVAL;
	}
	return 0;
}

static int cn10k_ipsec_inb_add_state(struct xfrm_state *x,
				     struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG_MOD(extack, "xfrm inbound offload not supported");
	return -EOPNOTSUPP;
}

static int cn10k_ipsec_outb_add_state(struct net_device *dev,
				      struct xfrm_state *x,
				      struct netlink_ext_ack *extack)
{
	struct cn10k_tx_sa_s *sa_entry;
	struct qmem *sa_info;
	struct otx2_nic *pf;
	int err;

	err = cn10k_ipsec_validate_state(x, extack);
	if (err)
		return err;

	pf = netdev_priv(dev);

	err = qmem_alloc(pf->dev, &sa_info, pf->ipsec.sa_size, OTX2_ALIGN);
	if (err)
		return err;

	sa_entry = (struct cn10k_tx_sa_s *)sa_info->base;
	cn10k_outb_prepare_sa(x, sa_entry);

	err = cn10k_outb_write_sa(pf, sa_info);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Error writing outbound SA");
		qmem_free(pf->dev, sa_info);
		return err;
	}

	x->xso.offload_handle = (unsigned long)sa_info;
	/* Enable static branch when first SA setup */
	if (!pf->ipsec.outb_sa_count)
		static_branch_enable(&cn10k_ipsec_sa_enabled);
	pf->ipsec.outb_sa_count++;
	return 0;
}

static int cn10k_ipsec_add_state(struct net_device *dev,
				 struct xfrm_state *x,
				 struct netlink_ext_ack *extack)
{
	if (x->xso.dir == XFRM_DEV_OFFLOAD_IN)
		return cn10k_ipsec_inb_add_state(x, extack);
	else
		return cn10k_ipsec_outb_add_state(dev, x, extack);
}

static void cn10k_ipsec_del_state(struct net_device *dev, struct xfrm_state *x)
{
	struct cn10k_tx_sa_s *sa_entry;
	struct qmem *sa_info;
	struct otx2_nic *pf;
	int err;

	if (x->xso.dir == XFRM_DEV_OFFLOAD_IN)
		return;

	pf = netdev_priv(dev);

	sa_info = (struct qmem *)x->xso.offload_handle;
	sa_entry = (struct cn10k_tx_sa_s *)sa_info->base;
	memset(sa_entry, 0, sizeof(struct cn10k_tx_sa_s));
	/* Disable SA in CPT h/w */
	sa_entry->ctx_push_size = cn10k_ipsec_get_ctx_push_size();
	sa_entry->ctx_size = (pf->ipsec.sa_size / OTX2_ALIGN)  & 0xF;
	sa_entry->aop_valid = 1;

	err = cn10k_outb_write_sa(pf, sa_info);
	if (err)
		netdev_err(dev, "Error (%d) deleting SA\n", err);

	x->xso.offload_handle = 0;
	qmem_free(pf->dev, sa_info);

	/* If no more SA's then update netdev feature for potential change
	 * in NETIF_F_HW_ESP.
	 */
	if (!--pf->ipsec.outb_sa_count)
		queue_work(pf->ipsec.sa_workq, &pf->ipsec.sa_work);
}

static const struct xfrmdev_ops cn10k_ipsec_xfrmdev_ops = {
	.xdo_dev_state_add	= cn10k_ipsec_add_state,
	.xdo_dev_state_delete	= cn10k_ipsec_del_state,
};

static void cn10k_ipsec_sa_wq_handler(struct work_struct *work)
{
	struct cn10k_ipsec *ipsec = container_of(work, struct cn10k_ipsec,
						 sa_work);
	struct otx2_nic *pf = container_of(ipsec, struct otx2_nic, ipsec);

	/* Disable static branch when no more SA enabled */
	static_branch_disable(&cn10k_ipsec_sa_enabled);
	rtnl_lock();
	netdev_update_features(pf->netdev);
	rtnl_unlock();
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

	/* Don't do CPT cleanup if SA installed */
	if (pf->ipsec.outb_sa_count) {
		netdev_err(pf->netdev, "SA installed on this device\n");
		return -EBUSY;
	}

	return cn10k_outb_cpt_clean(pf);
}

int cn10k_ipsec_init(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	u32 sa_size;

	if (!is_dev_support_ipsec_offload(pf->pdev))
		return 0;

	/* Each SA entry size is 128 Byte round up in size */
	sa_size = sizeof(struct cn10k_tx_sa_s) % OTX2_ALIGN ?
			 (sizeof(struct cn10k_tx_sa_s) / OTX2_ALIGN + 1) *
			 OTX2_ALIGN : sizeof(struct cn10k_tx_sa_s);
	pf->ipsec.sa_size = sa_size;

	INIT_WORK(&pf->ipsec.sa_work, cn10k_ipsec_sa_wq_handler);
	pf->ipsec.sa_workq = alloc_workqueue("cn10k_ipsec_sa_workq",
					     WQ_PERCPU, 0);
	if (!pf->ipsec.sa_workq) {
		netdev_err(pf->netdev, "SA alloc workqueue failed\n");
		return -ENOMEM;
	}

	/* Set xfrm device ops */
	netdev->xfrmdev_ops = &cn10k_ipsec_xfrmdev_ops;
	netdev->hw_features |= NETIF_F_HW_ESP;
	netdev->hw_enc_features |= NETIF_F_HW_ESP;

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

	if (pf->ipsec.sa_workq) {
		destroy_workqueue(pf->ipsec.sa_workq);
		pf->ipsec.sa_workq = NULL;
	}

	cn10k_outb_cpt_clean(pf);
}
EXPORT_SYMBOL(cn10k_ipsec_clean);

static u16 cn10k_ipsec_get_ip_data_len(struct xfrm_state *x,
				       struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h;
	struct iphdr *iph;
	u8 *src;

	src = (u8 *)skb->data + ETH_HLEN;

	if (x->props.family == AF_INET) {
		iph = (struct iphdr *)src;
		return ntohs(iph->tot_len);
	}

	ipv6h = (struct ipv6hdr *)src;
	return ntohs(ipv6h->payload_len) + sizeof(struct ipv6hdr);
}

/* Prepare CPT and NIX SQE scatter/gather subdescriptor structure.
 * SG of NIX and CPT are same in size.
 * Layout of a NIX SQE and CPT SG entry:
 *      -----------------------------
 *     |     CPT Scatter Gather      |
 *     |       (SQE SIZE)            |
 *     |                             |
 *      -----------------------------
 *     |       NIX SQE               |
 *     |       (SQE SIZE)            |
 *     |                             |
 *      -----------------------------
 */
bool otx2_sqe_add_sg_ipsec(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
			   struct sk_buff *skb, int num_segs, int *offset)
{
	struct cpt_sg_s *cpt_sg = NULL;
	struct nix_sqe_sg_s *sg = NULL;
	u64 dma_addr, *iova = NULL;
	u64 *cpt_iova = NULL;
	u16 *sg_lens = NULL;
	int seg, len;

	sq->sg[sq->head].num_segs = 0;
	cpt_sg = (struct cpt_sg_s *)(sq->sqe_base - sq->sqe_size);

	for (seg = 0; seg < num_segs; seg++) {
		if ((seg % MAX_SEGS_PER_SG) == 0) {
			sg = (struct nix_sqe_sg_s *)(sq->sqe_base + *offset);
			sg->ld_type = NIX_SEND_LDTYPE_LDD;
			sg->subdc = NIX_SUBDC_SG;
			sg->segs = 0;
			sg_lens = (void *)sg;
			iova = (void *)sg + sizeof(*sg);
			/* Next subdc always starts at a 16byte boundary.
			 * So if sg->segs is whether 2 or 3, offset += 16bytes.
			 */
			if ((num_segs - seg) >= (MAX_SEGS_PER_SG - 1))
				*offset += sizeof(*sg) + (3 * sizeof(u64));
			else
				*offset += sizeof(*sg) + sizeof(u64);

			cpt_sg += (seg / MAX_SEGS_PER_SG) * 4;
			cpt_iova = (void *)cpt_sg + sizeof(*cpt_sg);
		}
		dma_addr = otx2_dma_map_skb_frag(pfvf, skb, seg, &len);
		if (dma_mapping_error(pfvf->dev, dma_addr))
			return false;

		sg_lens[seg % MAX_SEGS_PER_SG] = len;
		sg->segs++;
		*iova++ = dma_addr;
		*cpt_iova++ = dma_addr;

		/* Save DMA mapping info for later unmapping */
		sq->sg[sq->head].dma_addr[seg] = dma_addr;
		sq->sg[sq->head].size[seg] = len;
		sq->sg[sq->head].num_segs++;

		*cpt_sg = *(struct cpt_sg_s *)sg;
		cpt_sg->rsvd_63_50 = 0;
	}

	sq->sg[sq->head].skb = (u64)skb;
	return true;
}

static u16 cn10k_ipsec_get_param1(u8 iv_offset)
{
	u16 param1_val;

	/* Set Crypto mode, disable L3/L4 checksum */
	param1_val = CN10K_IPSEC_INST_PARAM1_DIS_L4_CSUM |
		      CN10K_IPSEC_INST_PARAM1_DIS_L3_CSUM;
	param1_val |= (u16)iv_offset << CN10K_IPSEC_INST_PARAM1_IV_OFFSET_SHIFT;
	return param1_val;
}

bool cn10k_ipsec_transmit(struct otx2_nic *pf, struct netdev_queue *txq,
			  struct otx2_snd_queue *sq, struct sk_buff *skb,
			  int num_segs, int size)
{
	struct cpt_inst_s inst;
	struct cpt_res_s *res;
	struct xfrm_state *x;
	struct qmem *sa_info;
	dma_addr_t dptr_iova;
	struct sec_path *sp;
	u8 encap_offset;
	u8 auth_offset;
	u8 gthr_size;
	u8 iv_offset;
	u16 dlen;

	/* Check for IPSEC offload enabled */
	if (!(pf->flags & OTX2_FLAG_IPSEC_OFFLOAD_ENABLED))
		goto drop;

	sp = skb_sec_path(skb);
	if (unlikely(!sp->len))
		goto drop;

	x = xfrm_input_state(skb);
	if (unlikely(!x))
		goto drop;

	if (x->props.mode != XFRM_MODE_TRANSPORT &&
	    x->props.mode != XFRM_MODE_TUNNEL)
		goto drop;

	dlen = cn10k_ipsec_get_ip_data_len(x, skb);
	if (dlen == 0 && netif_msg_tx_err(pf)) {
		netdev_err(pf->netdev, "Invalid IP header, ip-length zero\n");
		goto drop;
	}

	/* Check for valid SA context */
	sa_info = (struct qmem *)x->xso.offload_handle;
	if (!sa_info)
		goto drop;

	memset(&inst, 0, sizeof(struct cpt_inst_s));

	/* Get authentication offset */
	if (x->props.family == AF_INET)
		auth_offset = sizeof(struct iphdr);
	else
		auth_offset = sizeof(struct ipv6hdr);

	/* IV offset is after ESP header */
	iv_offset = auth_offset + sizeof(struct ip_esp_hdr);
	/* Encap will start after IV */
	encap_offset = iv_offset + GCM_RFC4106_IV_SIZE;

	/* CPT Instruction word-1 */
	res = (struct cpt_res_s *)(sq->cpt_resp->base + (64 * sq->head));
	res->compcode = 0;
	inst.res_addr = sq->cpt_resp->iova + (64 * sq->head);

	/* CPT Instruction word-2 */
	inst.rvu_pf_func = pf->pcifunc;

	/* CPT Instruction word-3:
	 * Set QORD to force CPT_RES_S write completion
	 */
	inst.qord = 1;

	/* CPT Instruction word-4 */
	/* inst.dlen should not include ICV length */
	inst.dlen = dlen + ETH_HLEN - (x->aead->alg_icv_len / 8);
	inst.opcode_major = CN10K_IPSEC_MAJOR_OP_OUTB_IPSEC;
	inst.param1 = cn10k_ipsec_get_param1(iv_offset);

	inst.param2 = encap_offset <<
		       CN10K_IPSEC_INST_PARAM2_ENC_DATA_OFFSET_SHIFT;
	inst.param2 |= (u16)auth_offset <<
			CN10K_IPSEC_INST_PARAM2_AUTH_DATA_OFFSET_SHIFT;

	/* CPT Instruction word-5 */
	gthr_size = num_segs / MAX_SEGS_PER_SG;
	gthr_size = (num_segs % MAX_SEGS_PER_SG) ? gthr_size + 1 : gthr_size;

	gthr_size &= 0xF;
	dptr_iova = (sq->sqe_ring->iova + (sq->head * (sq->sqe_size * 2)));
	inst.dptr = dptr_iova | ((u64)gthr_size << 60);

	/* CPT Instruction word-6 */
	inst.rptr = inst.dptr;

	/* CPT Instruction word-7 */
	inst.cptr = sa_info->iova;
	inst.ctx_val = 1;
	inst.egrp = CN10K_DEF_CPT_IPSEC_EGRP;

	/* CPT Instruction word-0 */
	inst.nixtxl = (size / 16) - 1;
	inst.dat_offset = ETH_HLEN;
	inst.nixtx_offset = sq->sqe_size;

	netdev_tx_sent_queue(txq, skb->len);

	/* Finally Flush the CPT instruction */
	sq->head++;
	sq->head &= (sq->sqe_cnt - 1);
	cn10k_cpt_inst_flush(pf, &inst, sizeof(struct cpt_inst_s));
	return true;
drop:
	dev_kfree_skb_any(skb);
	return false;
}
