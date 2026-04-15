// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include "otx2_common.h"
#include "otx2_reg.h"
#include "otx2_struct.h"
#include "cn10k.h"

/* CN20K mbox AF => PFx irq handler */
irqreturn_t cn20k_pfaf_mbox_intr_handler(int irq, void *pf_irq)
{
	struct otx2_nic *pf = pf_irq;
	struct mbox *mw = &pf->mbox;
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *mbox;
	struct mbox_hdr *hdr;
	u64 pf_trig_val;

	pf_trig_val = otx2_read64(pf, RVU_PF_INT) & 0x3ULL;

	/* Clear the IRQ */
	otx2_write64(pf, RVU_PF_INT, pf_trig_val);

	if (pf_trig_val & BIT_ULL(0)) {
		mbox = &mw->mbox_up;
		mdev = &mbox->dev[0];
		otx2_sync_mbox_bbuf(mbox, 0);

		hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
		if (hdr->num_msgs)
			queue_work(pf->mbox_wq, &mw->mbox_up_wrk);

		trace_otx2_msg_interrupt(pf->pdev, "UP message from AF to PF",
					 BIT_ULL(0));
	}

	if (pf_trig_val & BIT_ULL(1)) {
		mbox = &mw->mbox;
		mdev = &mbox->dev[0];
		otx2_sync_mbox_bbuf(mbox, 0);

		hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
		if (hdr->num_msgs)
			queue_work(pf->mbox_wq, &mw->mbox_wrk);
		trace_otx2_msg_interrupt(pf->pdev, "DOWN reply from AF to PF",
					 BIT_ULL(1));
	}

	return IRQ_HANDLED;
}

irqreturn_t cn20k_vfaf_mbox_intr_handler(int irq, void *vf_irq)
{
	struct otx2_nic *vf = vf_irq;
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *mbox;
	struct mbox_hdr *hdr;
	u64 vf_trig_val;

	vf_trig_val = otx2_read64(vf, RVU_VF_INT) & 0x3ULL;
	/* Clear the IRQ */
	otx2_write64(vf, RVU_VF_INT, vf_trig_val);

	/* Read latest mbox data */
	smp_rmb();

	if (vf_trig_val & BIT_ULL(1)) {
		/* Check for PF => VF response messages */
		mbox = &vf->mbox.mbox;
		mdev = &mbox->dev[0];
		otx2_sync_mbox_bbuf(mbox, 0);

		hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
		if (hdr->num_msgs)
			queue_work(vf->mbox_wq, &vf->mbox.mbox_wrk);

		trace_otx2_msg_interrupt(mbox->pdev, "DOWN reply from PF0 to VF",
					 BIT_ULL(1));
	}

	if (vf_trig_val & BIT_ULL(0)) {
		/* Check for PF => VF notification messages */
		mbox = &vf->mbox.mbox_up;
		mdev = &mbox->dev[0];
		otx2_sync_mbox_bbuf(mbox, 0);

		hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
		if (hdr->num_msgs)
			queue_work(vf->mbox_wq, &vf->mbox.mbox_up_wrk);

		trace_otx2_msg_interrupt(mbox->pdev, "UP message from PF0 to VF",
					 BIT_ULL(0));
	}

	return IRQ_HANDLED;
}

void cn20k_enable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	/* Clear PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INTX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INTX(1), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INTX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INTX(1), ~0ull);

	/* Enable PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INT_ENA_W1SX(0), INTR_MASK(numvfs));
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INT_ENA_W1SX(0), INTR_MASK(numvfs));
	if (numvfs > 64) {
		numvfs -= 64;
		otx2_write64(pf, RVU_MBOX_PF_VFPF_INT_ENA_W1SX(1),
			     INTR_MASK(numvfs));
		otx2_write64(pf, RVU_MBOX_PF_VFPF1_INT_ENA_W1SX(1),
			     INTR_MASK(numvfs));
	}
}

void cn20k_disable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	int vector, intr_vec, vec = 0;

	/* Disable PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INT_ENA_W1CX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INT_ENA_W1CX(1), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INT_ENA_W1CX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INT_ENA_W1CX(1), ~0ull);

	otx2_write64(pf, RVU_MBOX_PF_VFPF_INTX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INTX(0), ~0ull);

	if (numvfs > 64) {
		otx2_write64(pf, RVU_MBOX_PF_VFPF_INTX(1), ~0ull);
		otx2_write64(pf, RVU_MBOX_PF_VFPF1_INTX(1), ~0ull);
	}

	for (intr_vec = RVU_MBOX_PF_INT_VEC_VFPF_MBOX0; intr_vec <=
			RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1; intr_vec++, vec++) {
		vector = pci_irq_vector(pf->pdev, intr_vec);
		free_irq(vector, pf->hw.pfvf_irq_devid[vec]);
	}
}

irqreturn_t cn20k_pfvf_mbox_intr_handler(int irq, void *pf_irq)
{
	struct pf_irq_data *irq_data = pf_irq;
	struct otx2_nic *pf = irq_data->pf;
	struct mbox *mbox;
	u64 intr;

	/* Sync with mbox memory region */
	rmb();

	/* Clear interrupts */
	intr = otx2_read64(pf, irq_data->intr_status);
	otx2_write64(pf, irq_data->intr_status, intr);
	mbox = pf->mbox_pfvf;

	if (intr)
		trace_otx2_msg_interrupt(pf->pdev, "VF(s) to PF", intr);

	irq_data->pf_queue_work_hdlr(mbox, pf->mbox_pfvf_wq, irq_data->start,
				     irq_data->mdevs, intr);

	return IRQ_HANDLED;
}

int cn20k_register_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	struct otx2_hw *hw = &pf->hw;
	struct pf_irq_data *irq_data;
	int intr_vec, ret, vec = 0;
	char *irq_name;

	/* irq data for 4 PF intr vectors */
	irq_data = devm_kcalloc(pf->dev, 4,
				sizeof(struct pf_irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	for (intr_vec = RVU_MBOX_PF_INT_VEC_VFPF_MBOX0; intr_vec <=
			RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1; intr_vec++, vec++) {
		switch (intr_vec) {
		case RVU_MBOX_PF_INT_VEC_VFPF_MBOX0:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF_INTX(0);
			irq_data[vec].start = 0;
			irq_data[vec].mdevs = 64;
			break;
		case RVU_MBOX_PF_INT_VEC_VFPF_MBOX1:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF_INTX(1);
			irq_data[vec].start = 64;
			irq_data[vec].mdevs = 96;
			break;
		case RVU_MBOX_PF_INT_VEC_VFPF1_MBOX0:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF1_INTX(0);
			irq_data[vec].start = 0;
			irq_data[vec].mdevs = 64;
			break;
		case RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF1_INTX(1);
			irq_data[vec].start = 64;
			irq_data[vec].mdevs = 96;
			break;
		}
		irq_data[vec].pf_queue_work_hdlr = otx2_queue_vf_work;
		irq_data[vec].vec_num = intr_vec;
		irq_data[vec].pf = pf;

		/* Register mailbox interrupt handler */
		irq_name = &hw->irq_name[intr_vec * NAME_SIZE];
		if (pf->pcifunc)
			snprintf(irq_name, NAME_SIZE,
				 "RVUPF%d_VF%d Mbox%d", rvu_get_pf(pf->pdev,
				 pf->pcifunc), vec / 2, vec % 2);
		else
			snprintf(irq_name, NAME_SIZE, "RVUPF_VF%d Mbox%d",
				 vec / 2, vec % 2);

		hw->pfvf_irq_devid[vec] = &irq_data[vec];
		ret = request_irq(pci_irq_vector(pf->pdev, intr_vec),
				  pf->hw_ops->pfvf_mbox_intr_handler, 0,
				  irq_name,
				  &irq_data[vec]);
		if (ret) {
			dev_err(pf->dev,
				"RVUPF: IRQ registration failed for PFVF mbox0 irq\n");
			return ret;
		}
	}

	cn20k_enable_pfvf_mbox_intr(pf, numvfs);

	return 0;
}

#define RQ_BP_LVL_AURA   (255 - ((85 * 256) / 100)) /* BP when 85% is full */

static u8 cn20k_aura_bpid_idx(struct otx2_nic *pfvf, int aura_id)
{
#ifdef CONFIG_DCB
	return pfvf->queue_to_pfc_map[aura_id];
#else
	return 0;
#endif
}

static int cn20k_tc_get_entry_index(struct otx2_flow_config *flow_cfg,
				    struct otx2_tc_flow *node)
{
	struct otx2_tc_flow *tmp;
	int index = 0;

	list_for_each_entry(tmp, &flow_cfg->flow_list_tc, list) {
		if (tmp == node)
			return index;

		index++;
	}

	return -1;
}

int cn20k_tc_free_mcam_entry(struct otx2_nic *nic, u16 entry)
{
	struct npc_mcam_free_entry_req *req;
	int err;

	mutex_lock(&nic->mbox.lock);
	req = otx2_mbox_alloc_msg_npc_mcam_free_entry(&nic->mbox);
	if (!req) {
		mutex_unlock(&nic->mbox.lock);
		return -ENOMEM;
	}

	req->entry = entry;
	/* Send message to AF to free MCAM entries */
	err = otx2_sync_mbox_msg(&nic->mbox);
	if (err) {
		mutex_unlock(&nic->mbox.lock);
		return err;
	}

	mutex_unlock(&nic->mbox.lock);

	return 0;
}

static bool cn20k_tc_check_entry_shiftable(struct otx2_nic *nic,
					   struct otx2_flow_config *flow_cfg,
					   struct otx2_tc_flow *node, int index,
					   bool error)
{
	struct otx2_tc_flow *first, *tmp, *n;
	u32 prio = 0;
	int i = 0;
	u8 type;

	first = list_first_entry(&flow_cfg->flow_list_tc, struct otx2_tc_flow,
				 list);
	type = first->kw_type;

	/* Check all the nodes from start to given index (including index) has
	 * same type i.e, either X2 or X4
	 */
	list_for_each_entry_safe(tmp, n, &flow_cfg->flow_list_tc, list) {
		if (i > index)
			break;

		if (type != tmp->kw_type) {
			/* List has both X2 and X4 entries so entries cannot be
			 * shifted to save MCAM space.
			 */
			if (error)
				dev_err(nic->dev, "Rule %d cannot be shifted to %d\n",
					tmp->prio, prio);
			return false;
		}

		type = tmp->kw_type;
		prio = tmp->prio;
		i++;
	}

	return true;
}

void cn20k_tc_update_mcam_table_del_req(struct otx2_nic *nic,
					struct otx2_flow_config *flow_cfg,
					struct otx2_tc_flow *node)
{
	struct otx2_tc_flow *first, *tmp, *n;
	int i = 0, index;
	u16 cntr_val = 0;
	u16 entry;

	index = cn20k_tc_get_entry_index(flow_cfg, node);
	if (index < 0) {
		netdev_dbg(nic->netdev, "Could not find node\n");
		return;
	}

	first = list_first_entry(&flow_cfg->flow_list_tc, struct otx2_tc_flow,
				 list);
	entry = first->entry;

	/* If entries cannot be shifted then delete given entry
	 * and free it to AF too.
	 */
	if (!cn20k_tc_check_entry_shiftable(nic, flow_cfg, node,
					    index, false)) {
		list_del(&node->list);
		entry = node->entry;
		goto free_mcam_entry;
	}

	/* Find and delete the entry from the list and re-install
	 * all the entries from beginning to the index of the
	 * deleted entry to higher mcam indexes.
	 */
	list_for_each_entry_safe(tmp, n, &flow_cfg->flow_list_tc, list) {
		if (node == tmp) {
			list_del(&tmp->list);
			break;
		}

		otx2_del_mcam_flow_entry(nic, tmp->entry, &cntr_val);
		tmp->entry = (list_next_entry(tmp, list))->entry;
		tmp->req.entry = tmp->entry;
		tmp->req.cntr_val = cntr_val;
	}

	list_for_each_entry_safe(tmp, n, &flow_cfg->flow_list_tc, list) {
		if (i == index)
			break;

		otx2_add_mcam_flow_entry(nic, &tmp->req);
		i++;
	}

free_mcam_entry:
	if (cn20k_tc_free_mcam_entry(nic, entry))
		netdev_err(nic->netdev, "Freeing entry %d to AF failed\n",
			   entry);
}

int cn20k_tc_update_mcam_table_add_req(struct otx2_nic *nic,
				       struct otx2_flow_config *flow_cfg,
				       struct otx2_tc_flow *node)
{
	struct otx2_tc_flow *tmp;
	u16 cntr_val = 0;
	int list_idx, i;
	int entry, prev;

	/* Find the index of the entry(list_idx) whose priority
	 * is greater than the new entry and re-install all
	 * the entries from beginning to list_idx to higher
	 * mcam indexes.
	 */
	list_idx = otx2_tc_add_to_flow_list(flow_cfg, node);
	entry = node->entry;
	if (!cn20k_tc_check_entry_shiftable(nic, flow_cfg, node,
					    list_idx, true))
		/* Due to mix of X2 and X4, entries cannot be shifted.
		 * In this case free the entry allocated for this rule.
		 */
		return -EINVAL;

	for (i = 0; i < list_idx; i++) {
		tmp = otx2_tc_get_entry_by_index(flow_cfg, i);
		if (!tmp)
			return -ENOMEM;

		otx2_del_mcam_flow_entry(nic, tmp->entry, &cntr_val);
		prev = tmp->entry;
		tmp->entry = entry;
		tmp->req.entry = tmp->entry;
		tmp->req.cntr_val = cntr_val;
		otx2_add_mcam_flow_entry(nic, &tmp->req);
		entry = prev;
	}

	return entry;
}

#define MAX_TC_HW_PRIORITY		125
#define MAX_TC_VF_PRIORITY		126
#define MAX_TC_PF_PRIORITY		127

static int __cn20k_tc_alloc_entry(struct otx2_nic *nic,
				  struct npc_install_flow_req *flow_req,
				  u16 *entry, u8 *type,
				  u32 tc_priority, bool hw_priority)
{
	struct otx2_flow_config *flow_cfg = nic->flow_cfg;
	struct npc_install_flow_req *req;
	struct npc_install_flow_rsp *rsp;
	struct otx2_tc_flow *tmp;
	int ret = 0;

	req = otx2_mbox_alloc_msg_npc_install_flow(&nic->mbox);
	if (!req)
		return -ENOMEM;

	memcpy(&flow_req->hdr, &req->hdr, sizeof(struct mbox_msghdr));
	memcpy(req, flow_req, sizeof(struct npc_install_flow_req));
	req->alloc_entry = 1;

	/* Allocate very least priority for first rule */
	if (hw_priority || list_empty(&flow_cfg->flow_list_tc)) {
		req->ref_prio = NPC_MCAM_LEAST_PRIO;
	} else {
		req->ref_prio = NPC_MCAM_HIGHER_PRIO;
		tmp = list_first_entry(&flow_cfg->flow_list_tc,
				       struct otx2_tc_flow, list);
		req->ref_entry = tmp->entry;
	}

	ret = otx2_sync_mbox_msg(&nic->mbox);
	if (ret)
		return ret;

	rsp = (struct npc_install_flow_rsp *)otx2_mbox_get_rsp(&nic->mbox.mbox,
							       0, &req->hdr);
	if (IS_ERR(rsp))
		return -EFAULT;

	if (entry)
		*entry = rsp->entry;
	if (type)
		*type = rsp->kw_type;

	return ret;
}

int cn20k_tc_alloc_entry(struct otx2_nic *nic,
			 struct flow_cls_offload *tc_flow_cmd,
			 struct otx2_tc_flow *new_node,
			 struct npc_install_flow_req *flow_req)
{
	bool hw_priority = false;
	u16 entry_from_af;
	u8 entry_type;
	int ret;

	if (is_otx2_vf(nic->pcifunc))
		flow_req->hw_prio = MAX_TC_VF_PRIORITY;
	else
		flow_req->hw_prio = MAX_TC_PF_PRIORITY;

	if (new_node->prio <= MAX_TC_HW_PRIORITY) {
		flow_req->hw_prio = new_node->prio;
		hw_priority = true;
	}

	mutex_lock(&nic->mbox.lock);

	ret = __cn20k_tc_alloc_entry(nic, flow_req, &entry_from_af, &entry_type,
				     new_node->prio, hw_priority);
	if (ret) {
		mutex_unlock(&nic->mbox.lock);
		return ret;
	}

	new_node->kw_type = entry_type;
	new_node->entry = entry_from_af;

	mutex_unlock(&nic->mbox.lock);

	return 0;
}

static int cn20k_aura_aq_init(struct otx2_nic *pfvf, int aura_id,
			      int pool_id, int numptrs)
{
	struct npa_cn20k_aq_enq_req *aq;
	struct otx2_pool *pool;
	u8 bpid_idx;
	int err;

	pool = &pfvf->qset.pool[pool_id];

	/* Allocate memory for HW to update Aura count.
	 * Alloc one cache line, so that it fits all FC_STYPE modes.
	 */
	if (!pool->fc_addr) {
		err = qmem_alloc(pfvf->dev, &pool->fc_addr, 1, OTX2_ALIGN);
		if (err)
			return err;
	}

	/* Initialize this aura's context via AF */
	aq = otx2_mbox_alloc_msg_npa_cn20k_aq_enq(&pfvf->mbox);
	if (!aq) {
		/* Shared mbox memory buffer is full, flush it and retry */
		err = otx2_sync_mbox_msg(&pfvf->mbox);
		if (err)
			return err;
		aq = otx2_mbox_alloc_msg_npa_cn20k_aq_enq(&pfvf->mbox);
		if (!aq)
			return -ENOMEM;
	}

	aq->aura_id = aura_id;

	/* Will be filled by AF with correct pool context address */
	aq->aura.pool_addr = pool_id;
	aq->aura.pool_caching = 1;
	aq->aura.shift = ilog2(numptrs) - 8;
	aq->aura.count = numptrs;
	aq->aura.limit = numptrs;
	aq->aura.avg_level = 255;
	aq->aura.ena = 1;
	aq->aura.fc_ena = 1;
	aq->aura.fc_addr = pool->fc_addr->iova;
	aq->aura.fc_hyst_bits = 0; /* Store count on all updates */

	/* Enable backpressure for RQ aura */
	if (aura_id < pfvf->hw.rqpool_cnt && !is_otx2_lbkvf(pfvf->pdev)) {
		aq->aura.bp_ena = 0;
		/* If NIX1 LF is attached then specify NIX1_RX.
		 *
		 * Below NPA_AURA_S[BP_ENA] is set according to the
		 * NPA_BPINTF_E enumeration given as:
		 * 0x0 + a*0x1 where 'a' is 0 for NIX0_RX and 1 for NIX1_RX so
		 * NIX0_RX is 0x0 + 0*0x1 = 0
		 * NIX1_RX is 0x0 + 1*0x1 = 1
		 * But in HRM it is given that
		 * "NPA_AURA_S[BP_ENA](w1[33:32]) - Enable aura backpressure to
		 * NIX-RX based on [BP] level. One bit per NIX-RX; index
		 * enumerated by NPA_BPINTF_E."
		 */
		if (pfvf->nix_blkaddr == BLKADDR_NIX1)
			aq->aura.bp_ena = 1;

		bpid_idx = cn20k_aura_bpid_idx(pfvf, aura_id);
		aq->aura.bpid = pfvf->bpid[bpid_idx];

		/* Set backpressure level for RQ's Aura */
		aq->aura.bp = RQ_BP_LVL_AURA;
	}

	/* Fill AQ info */
	aq->ctype = NPA_AQ_CTYPE_AURA;
	aq->op = NPA_AQ_INSTOP_INIT;

	return 0;
}

static int cn20k_pool_aq_init(struct otx2_nic *pfvf, u16 pool_id,
			      int stack_pages, int numptrs, int buf_size,
			      int type)
{
	struct page_pool_params pp_params = { 0 };
	struct npa_cn20k_aq_enq_req *aq;
	struct otx2_pool *pool;
	int err, sz;

	pool = &pfvf->qset.pool[pool_id];
	/* Alloc memory for stack which is used to store buffer pointers */
	err = qmem_alloc(pfvf->dev, &pool->stack,
			 stack_pages, pfvf->hw.stack_pg_bytes);
	if (err)
		return err;

	pool->rbsize = buf_size;

	/* Initialize this pool's context via AF */
	aq = otx2_mbox_alloc_msg_npa_cn20k_aq_enq(&pfvf->mbox);
	if (!aq) {
		/* Shared mbox memory buffer is full, flush it and retry */
		err = otx2_sync_mbox_msg(&pfvf->mbox);
		if (err) {
			qmem_free(pfvf->dev, pool->stack);
			return err;
		}
		aq = otx2_mbox_alloc_msg_npa_cn20k_aq_enq(&pfvf->mbox);
		if (!aq) {
			qmem_free(pfvf->dev, pool->stack);
			return -ENOMEM;
		}
	}

	aq->aura_id = pool_id;
	aq->pool.stack_base = pool->stack->iova;
	aq->pool.stack_caching = 1;
	aq->pool.ena = 1;
	aq->pool.buf_size = buf_size / 128;
	aq->pool.stack_max_pages = stack_pages;
	aq->pool.shift = ilog2(numptrs) - 8;
	aq->pool.ptr_start = 0;
	aq->pool.ptr_end = ~0ULL;

	/* Fill AQ info */
	aq->ctype = NPA_AQ_CTYPE_POOL;
	aq->op = NPA_AQ_INSTOP_INIT;

	if (type != AURA_NIX_RQ) {
		pool->page_pool = NULL;
		return 0;
	}

	sz = ALIGN(ALIGN(SKB_DATA_ALIGN(buf_size), OTX2_ALIGN), PAGE_SIZE);
	pp_params.order = get_order(sz);
	pp_params.flags = PP_FLAG_DMA_MAP;
	pp_params.pool_size = min(OTX2_PAGE_POOL_SZ, numptrs);
	pp_params.nid = NUMA_NO_NODE;
	pp_params.dev = pfvf->dev;
	pp_params.dma_dir = DMA_FROM_DEVICE;
	pool->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(pool->page_pool)) {
		netdev_err(pfvf->netdev, "Creation of page pool failed\n");
		return PTR_ERR(pool->page_pool);
	}

	return 0;
}

static int cn20k_sq_aq_init(void *dev, u16 qidx, u8 chan_offset, u16 sqb_aura)
{
	struct nix_cn20k_aq_enq_req *aq;
	struct otx2_nic *pfvf = dev;

	/* Get memory to put this msg */
	aq = otx2_mbox_alloc_msg_nix_cn20k_aq_enq(&pfvf->mbox);
	if (!aq)
		return -ENOMEM;

	aq->sq.cq = pfvf->hw.rx_queues + qidx;
	aq->sq.max_sqe_size = NIX_MAXSQESZ_W16; /* 128 byte */
	aq->sq.cq_ena = 1;
	aq->sq.ena = 1;
	aq->sq.smq = otx2_get_smq_idx(pfvf, qidx);
	aq->sq.smq_rr_weight = mtu_to_dwrr_weight(pfvf, pfvf->tx_max_pktlen);
	aq->sq.default_chan = pfvf->hw.tx_chan_base + chan_offset;
	aq->sq.sqe_stype = NIX_STYPE_STF; /* Cache SQB */
	aq->sq.sqb_aura = sqb_aura;
	aq->sq.sq_int_ena = NIX_SQINT_BITS;
	aq->sq.qint_idx = 0;
	/* Due pipelining impact minimum 2000 unused SQ CQE's
	 * need to maintain to avoid CQ overflow.
	 */
	aq->sq.cq_limit = (SEND_CQ_SKID * 256) / (pfvf->qset.sqe_cnt);

	/* Fill AQ info */
	aq->qidx = qidx;
	aq->ctype = NIX_AQ_CTYPE_SQ;
	aq->op = NIX_AQ_INSTOP_INIT;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

static struct dev_hw_ops cn20k_hw_ops = {
	.pfaf_mbox_intr_handler = cn20k_pfaf_mbox_intr_handler,
	.vfaf_mbox_intr_handler = cn20k_vfaf_mbox_intr_handler,
	.pfvf_mbox_intr_handler = cn20k_pfvf_mbox_intr_handler,
	.sq_aq_init = cn20k_sq_aq_init,
	.sqe_flush = cn10k_sqe_flush,
	.aura_freeptr = cn10k_aura_freeptr,
	.refill_pool_ptrs = cn10k_refill_pool_ptrs,
	.aura_aq_init = cn20k_aura_aq_init,
	.pool_aq_init = cn20k_pool_aq_init,
};

void cn20k_init(struct otx2_nic *pfvf)
{
	pfvf->hw_ops = &cn20k_hw_ops;
}
EXPORT_SYMBOL(cn20k_init);
