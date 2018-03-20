// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* Intel(R) Ethernet Connection E800 Series Linux Driver */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ice.h"

#define DRV_VERSION	"ice-0.0.1-k"
#define DRV_SUMMARY	"Intel(R) Ethernet Connection E800 Series Linux Driver"
static const char ice_drv_ver[] = DRV_VERSION;
static const char ice_driver_string[] = DRV_SUMMARY;
static const char ice_copyright[] = "Copyright (c) 2018, Intel Corporation.";

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static int debug = -1;
module_param(debug, int, 0644);
#ifndef CONFIG_DYNAMIC_DEBUG
MODULE_PARM_DESC(debug, "netif level (0=none,...,16=all), hw debug_mask (0x8XXXXXXX)");
#else
MODULE_PARM_DESC(debug, "netif level (0=none,...,16=all)");
#endif /* !CONFIG_DYNAMIC_DEBUG */

static struct workqueue_struct *ice_wq;

static int ice_vsi_release(struct ice_vsi *vsi);

/**
 * ice_get_free_slot - get the next non-NULL location index in array
 * @array: array to search
 * @size: size of the array
 * @curr: last known occupied index to be used as a search hint
 *
 * void * is being used to keep the functionality generic. This lets us use this
 * function on any array of pointers.
 */
static int ice_get_free_slot(void *array, int size, int curr)
{
	int **tmp_array = (int **)array;
	int next;

	if (curr < (size - 1) && !tmp_array[curr + 1]) {
		next = curr + 1;
	} else {
		int i = 0;

		while ((i < size) && (tmp_array[i]))
			i++;
		if (i == size)
			next = ICE_NO_VSI;
		else
			next = i;
	}
	return next;
}

/**
 * ice_search_res - Search the tracker for a block of resources
 * @res: pointer to the resource
 * @needed: size of the block needed
 * @id: identifier to track owner
 * Returns the base item index of the block, or -ENOMEM for error
 */
static int ice_search_res(struct ice_res_tracker *res, u16 needed, u16 id)
{
	int start = res->search_hint;
	int end = start;

	id |= ICE_RES_VALID_BIT;

	do {
		/* skip already allocated entries */
		if (res->list[end++] & ICE_RES_VALID_BIT) {
			start = end;
			if ((start + needed) > res->num_entries)
				break;
		}

		if (end == (start + needed)) {
			int i = start;

			/* there was enough, so assign it to the requestor */
			while (i != end)
				res->list[i++] = id;

			if (end == res->num_entries)
				end = 0;

			res->search_hint = end;
			return start;
		}
	} while (1);

	return -ENOMEM;
}

/**
 * ice_get_res - get a block of resources
 * @pf: board private structure
 * @res: pointer to the resource
 * @needed: size of the block needed
 * @id: identifier to track owner
 *
 * Returns the base item index of the block, or -ENOMEM for error
 * The search_hint trick and lack of advanced fit-finding only works
 * because we're highly likely to have all the same sized requests.
 * Linear search time and any fragmentation should be minimal.
 */
static int
ice_get_res(struct ice_pf *pf, struct ice_res_tracker *res, u16 needed, u16 id)
{
	int ret;

	if (!res || !pf)
		return -EINVAL;

	if (!needed || needed > res->num_entries || id >= ICE_RES_VALID_BIT) {
		dev_err(&pf->pdev->dev,
			"param err: needed=%d, num_entries = %d id=0x%04x\n",
			needed, res->num_entries, id);
		return -EINVAL;
	}

	/* search based on search_hint */
	ret = ice_search_res(res, needed, id);

	if (ret < 0) {
		/* previous search failed. Reset search hint and try again */
		res->search_hint = 0;
		ret = ice_search_res(res, needed, id);
	}

	return ret;
}

/**
 * ice_free_res - free a block of resources
 * @res: pointer to the resource
 * @index: starting index previously returned by ice_get_res
 * @id: identifier to track owner
 * Returns number of resources freed
 */
static int ice_free_res(struct ice_res_tracker *res, u16 index, u16 id)
{
	int count = 0;
	int i;

	if (!res || index >= res->num_entries)
		return -EINVAL;

	id |= ICE_RES_VALID_BIT;
	for (i = index; i < res->num_entries && res->list[i] == id; i++) {
		res->list[i] = 0;
		count++;
	}

	return count;
}

/**
 * __ice_clean_ctrlq - helper function to clean controlq rings
 * @pf: ptr to struct ice_pf
 * @q_type: specific Control queue type
 */
static int __ice_clean_ctrlq(struct ice_pf *pf, enum ice_ctl_q q_type)
{
	struct ice_rq_event_info event;
	struct ice_hw *hw = &pf->hw;
	struct ice_ctl_q_info *cq;
	u16 pending, i = 0;
	const char *qtype;
	u32 oldval, val;

	switch (q_type) {
	case ICE_CTL_Q_ADMIN:
		cq = &hw->adminq;
		qtype = "Admin";
		break;
	default:
		dev_warn(&pf->pdev->dev, "Unknown control queue type 0x%x\n",
			 q_type);
		return 0;
	}

	/* check for error indications - PF_xx_AxQLEN register layout for
	 * FW/MBX/SB are identical so just use defines for PF_FW_AxQLEN.
	 */
	val = rd32(hw, cq->rq.len);
	if (val & (PF_FW_ARQLEN_ARQVFE_M | PF_FW_ARQLEN_ARQOVFL_M |
		   PF_FW_ARQLEN_ARQCRIT_M)) {
		oldval = val;
		if (val & PF_FW_ARQLEN_ARQVFE_M)
			dev_dbg(&pf->pdev->dev,
				"%s Receive Queue VF Error detected\n", qtype);
		if (val & PF_FW_ARQLEN_ARQOVFL_M) {
			dev_dbg(&pf->pdev->dev,
				"%s Receive Queue Overflow Error detected\n",
				qtype);
		}
		if (val & PF_FW_ARQLEN_ARQCRIT_M)
			dev_dbg(&pf->pdev->dev,
				"%s Receive Queue Critical Error detected\n",
				qtype);
		val &= ~(PF_FW_ARQLEN_ARQVFE_M | PF_FW_ARQLEN_ARQOVFL_M |
			 PF_FW_ARQLEN_ARQCRIT_M);
		if (oldval != val)
			wr32(hw, cq->rq.len, val);
	}

	val = rd32(hw, cq->sq.len);
	if (val & (PF_FW_ATQLEN_ATQVFE_M | PF_FW_ATQLEN_ATQOVFL_M |
		   PF_FW_ATQLEN_ATQCRIT_M)) {
		oldval = val;
		if (val & PF_FW_ATQLEN_ATQVFE_M)
			dev_dbg(&pf->pdev->dev,
				"%s Send Queue VF Error detected\n", qtype);
		if (val & PF_FW_ATQLEN_ATQOVFL_M) {
			dev_dbg(&pf->pdev->dev,
				"%s Send Queue Overflow Error detected\n",
				qtype);
		}
		if (val & PF_FW_ATQLEN_ATQCRIT_M)
			dev_dbg(&pf->pdev->dev,
				"%s Send Queue Critical Error detected\n",
				qtype);
		val &= ~(PF_FW_ATQLEN_ATQVFE_M | PF_FW_ATQLEN_ATQOVFL_M |
			 PF_FW_ATQLEN_ATQCRIT_M);
		if (oldval != val)
			wr32(hw, cq->sq.len, val);
	}

	event.buf_len = cq->rq_buf_size;
	event.msg_buf = devm_kzalloc(&pf->pdev->dev, event.buf_len,
				     GFP_KERNEL);
	if (!event.msg_buf)
		return 0;

	do {
		enum ice_status ret;

		ret = ice_clean_rq_elem(hw, cq, &event, &pending);
		if (ret == ICE_ERR_AQ_NO_WORK)
			break;
		if (ret) {
			dev_err(&pf->pdev->dev,
				"%s Receive Queue event error %d\n", qtype,
				ret);
			break;
		}
	} while (pending && (i++ < ICE_DFLT_IRQ_WORK));

	devm_kfree(&pf->pdev->dev, event.msg_buf);

	return pending && (i == ICE_DFLT_IRQ_WORK);
}

/**
 * ice_clean_adminq_subtask - clean the AdminQ rings
 * @pf: board private structure
 */
static void ice_clean_adminq_subtask(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	u32 val;

	if (!test_bit(__ICE_ADMINQ_EVENT_PENDING, pf->state))
		return;

	if (__ice_clean_ctrlq(pf, ICE_CTL_Q_ADMIN))
		return;

	clear_bit(__ICE_ADMINQ_EVENT_PENDING, pf->state);

	/* re-enable Admin queue interrupt causes */
	val = rd32(hw, PFINT_FW_CTL);
	wr32(hw, PFINT_FW_CTL, (val | PFINT_FW_CTL_CAUSE_ENA_M));

	ice_flush(hw);
}

/**
 * ice_service_task_schedule - schedule the service task to wake up
 * @pf: board private structure
 *
 * If not already scheduled, this puts the task into the work queue.
 */
static void ice_service_task_schedule(struct ice_pf *pf)
{
	if (!test_bit(__ICE_DOWN, pf->state) &&
	    !test_and_set_bit(__ICE_SERVICE_SCHED, pf->state))
		queue_work(ice_wq, &pf->serv_task);
}

/**
 * ice_service_task_complete - finish up the service task
 * @pf: board private structure
 */
static void ice_service_task_complete(struct ice_pf *pf)
{
	WARN_ON(!test_bit(__ICE_SERVICE_SCHED, pf->state));

	/* force memory (pf->state) to sync before next service task */
	smp_mb__before_atomic();
	clear_bit(__ICE_SERVICE_SCHED, pf->state);
}

/**
 * ice_service_timer - timer callback to schedule service task
 * @t: pointer to timer_list
 */
static void ice_service_timer(struct timer_list *t)
{
	struct ice_pf *pf = from_timer(pf, t, serv_tmr);

	mod_timer(&pf->serv_tmr, round_jiffies(pf->serv_tmr_period + jiffies));
	ice_service_task_schedule(pf);
}

/**
 * ice_service_task - manage and run subtasks
 * @work: pointer to work_struct contained by the PF struct
 */
static void ice_service_task(struct work_struct *work)
{
	struct ice_pf *pf = container_of(work, struct ice_pf, serv_task);
	unsigned long start_time = jiffies;

	/* subtasks */
	ice_clean_adminq_subtask(pf);

	/* Clear __ICE_SERVICE_SCHED flag to allow scheduling next event */
	ice_service_task_complete(pf);

	/* If the tasks have taken longer than one service timer period
	 * or there is more work to be done, reset the service timer to
	 * schedule the service task now.
	 */
	if (time_after(jiffies, (start_time + pf->serv_tmr_period)) ||
	    test_bit(__ICE_ADMINQ_EVENT_PENDING, pf->state))
		mod_timer(&pf->serv_tmr, jiffies);
}

/**
 * ice_set_ctrlq_len - helper function to set controlq length
 * @hw: pointer to the hw instance
 */
static void ice_set_ctrlq_len(struct ice_hw *hw)
{
	hw->adminq.num_rq_entries = ICE_AQ_LEN;
	hw->adminq.num_sq_entries = ICE_AQ_LEN;
	hw->adminq.rq_buf_size = ICE_AQ_MAX_BUF_LEN;
	hw->adminq.sq_buf_size = ICE_AQ_MAX_BUF_LEN;
}

/**
 * ice_vsi_delete - delete a VSI from the switch
 * @vsi: pointer to VSI being removed
 */
static void ice_vsi_delete(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_vsi_ctx ctxt;
	enum ice_status status;

	ctxt.vsi_num = vsi->vsi_num;

	memcpy(&ctxt.info, &vsi->info, sizeof(struct ice_aqc_vsi_props));

	status = ice_aq_free_vsi(&pf->hw, &ctxt, false, NULL);
	if (status)
		dev_err(&pf->pdev->dev, "Failed to delete VSI %i in FW\n",
			vsi->vsi_num);
}

/**
 * ice_vsi_setup_q_map - Setup a VSI queue map
 * @vsi: the VSI being configured
 * @ctxt: VSI context structure
 */
static void ice_vsi_setup_q_map(struct ice_vsi *vsi, struct ice_vsi_ctx *ctxt)
{
	u16 offset = 0, qmap = 0, pow = 0, qcount;
	u16 qcount_tx = vsi->alloc_txq;
	u16 qcount_rx = vsi->alloc_rxq;
	bool ena_tc0 = false;
	int i;

	/* at least TC0 should be enabled by default */
	if (vsi->tc_cfg.numtc) {
		if (!(vsi->tc_cfg.ena_tc & BIT(0)))
			ena_tc0 =  true;
	} else {
		ena_tc0 =  true;
	}

	if (ena_tc0) {
		vsi->tc_cfg.numtc++;
		vsi->tc_cfg.ena_tc |= 1;
	}

	qcount = qcount_rx / vsi->tc_cfg.numtc;

	/* find higher power-of-2 of qcount */
	pow = ilog2(qcount);

	if (!is_power_of_2(qcount))
		pow++;

	/* TC mapping is a function of the number of Rx queues assigned to the
	 * VSI for each traffic class and the offset of these queues.
	 * The first 10 bits are for queue offset for TC0, next 4 bits for no:of
	 * queues allocated to TC0. No:of queues is a power-of-2.
	 *
	 * If TC is not enabled, the queue offset is set to 0, and allocate one
	 * queue, this way, traffic for the given TC will be sent to the default
	 * queue.
	 *
	 * Setup number and offset of Rx queues for all TCs for the VSI
	 */
	for (i = 0; i < ICE_MAX_TRAFFIC_CLASS; i++) {
		if (!(vsi->tc_cfg.ena_tc & BIT(i))) {
			/* TC is not enabled */
			vsi->tc_cfg.tc_info[i].qoffset = 0;
			vsi->tc_cfg.tc_info[i].qcount = 1;
			ctxt->info.tc_mapping[i] = 0;
			continue;
		}

		/* TC is enabled */
		vsi->tc_cfg.tc_info[i].qoffset = offset;
		vsi->tc_cfg.tc_info[i].qcount = qcount;

		qmap = ((offset << ICE_AQ_VSI_TC_Q_OFFSET_S) &
			ICE_AQ_VSI_TC_Q_OFFSET_M) |
			((pow << ICE_AQ_VSI_TC_Q_NUM_S) &
			 ICE_AQ_VSI_TC_Q_NUM_M);
		offset += qcount;
		ctxt->info.tc_mapping[i] = cpu_to_le16(qmap);
	}

	vsi->num_txq = qcount_tx;
	vsi->num_rxq = offset;

	/* Rx queue mapping */
	ctxt->info.mapping_flags |= cpu_to_le16(ICE_AQ_VSI_Q_MAP_CONTIG);
	/* q_mapping buffer holds the info for the first queue allocated for
	 * this VSI in the PF space and also the number of queues associated
	 * with this VSI.
	 */
	ctxt->info.q_mapping[0] = cpu_to_le16(vsi->rxq_map[0]);
	ctxt->info.q_mapping[1] = cpu_to_le16(vsi->num_rxq);
}

/**
 * ice_set_dflt_vsi_ctx - Set default VSI context before adding a VSI
 * @ctxt: the VSI context being set
 *
 * This initializes a default VSI context for all sections except the Queues.
 */
static void ice_set_dflt_vsi_ctx(struct ice_vsi_ctx *ctxt)
{
	u32 table = 0;

	memset(&ctxt->info, 0, sizeof(ctxt->info));
	/* VSI's should be allocated from shared pool */
	ctxt->alloc_from_pool = true;
	/* Src pruning enabled by default */
	ctxt->info.sw_flags = ICE_AQ_VSI_SW_FLAG_SRC_PRUNE;
	/* Traffic from VSI can be sent to LAN */
	ctxt->info.sw_flags2 = ICE_AQ_VSI_SW_FLAG_LAN_ENA;
	/* Allow all packets untagged/tagged */
	ctxt->info.port_vlan_flags = ((ICE_AQ_VSI_PVLAN_MODE_ALL &
				       ICE_AQ_VSI_PVLAN_MODE_M) >>
				      ICE_AQ_VSI_PVLAN_MODE_S);
	/* Show VLAN/UP from packets in Rx descriptors */
	ctxt->info.port_vlan_flags |= ((ICE_AQ_VSI_PVLAN_EMOD_STR_BOTH &
					ICE_AQ_VSI_PVLAN_EMOD_M) >>
				       ICE_AQ_VSI_PVLAN_EMOD_S);
	/* Have 1:1 UP mapping for both ingress/egress tables */
	table |= ICE_UP_TABLE_TRANSLATE(0, 0);
	table |= ICE_UP_TABLE_TRANSLATE(1, 1);
	table |= ICE_UP_TABLE_TRANSLATE(2, 2);
	table |= ICE_UP_TABLE_TRANSLATE(3, 3);
	table |= ICE_UP_TABLE_TRANSLATE(4, 4);
	table |= ICE_UP_TABLE_TRANSLATE(5, 5);
	table |= ICE_UP_TABLE_TRANSLATE(6, 6);
	table |= ICE_UP_TABLE_TRANSLATE(7, 7);
	ctxt->info.ingress_table = cpu_to_le32(table);
	ctxt->info.egress_table = cpu_to_le32(table);
	/* Have 1:1 UP mapping for outer to inner UP table */
	ctxt->info.outer_up_table = cpu_to_le32(table);
	/* No Outer tag support outer_tag_flags remains to zero */
}

/**
 * ice_vsi_add - Create a new VSI or fetch preallocated VSI
 * @vsi: the VSI being configured
 *
 * This initializes a VSI context depending on the VSI type to be added and
 * passes it down to the add_vsi aq command to create a new VSI.
 */
static int ice_vsi_add(struct ice_vsi *vsi)
{
	struct ice_vsi_ctx ctxt = { 0 };
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	int ret = 0;

	switch (vsi->type) {
	case ICE_VSI_PF:
		ctxt.flags = ICE_AQ_VSI_TYPE_PF;
		break;
	default:
		return -ENODEV;
	}

	ice_set_dflt_vsi_ctx(&ctxt);
	/* if the switch is in VEB mode, allow VSI loopback */
	if (vsi->vsw->bridge_mode == BRIDGE_MODE_VEB)
		ctxt.info.sw_flags |= ICE_AQ_VSI_SW_FLAG_ALLOW_LB;

	ctxt.info.sw_id = vsi->port_info->sw_id;
	ice_vsi_setup_q_map(vsi, &ctxt);

	ret = ice_aq_add_vsi(hw, &ctxt, NULL);
	if (ret) {
		dev_err(&vsi->back->pdev->dev,
			"Add VSI AQ call failed, err %d\n", ret);
		return -EIO;
	}
	vsi->info = ctxt.info;
	vsi->vsi_num = ctxt.vsi_num;

	return ret;
}

/**
 * ice_vsi_clear_rings - Deallocates the Tx and Rx rings for VSI
 * @vsi: the VSI having rings deallocated
 */
static void ice_vsi_clear_rings(struct ice_vsi *vsi)
{
	int i;

	if (vsi->tx_rings) {
		for (i = 0; i < vsi->alloc_txq; i++) {
			if (vsi->tx_rings[i]) {
				kfree_rcu(vsi->tx_rings[i], rcu);
				vsi->tx_rings[i] = NULL;
			}
		}
	}
	if (vsi->rx_rings) {
		for (i = 0; i < vsi->alloc_rxq; i++) {
			if (vsi->rx_rings[i]) {
				kfree_rcu(vsi->rx_rings[i], rcu);
				vsi->rx_rings[i] = NULL;
			}
		}
	}
}

/**
 * ice_vsi_alloc_rings - Allocates Tx and Rx rings for the VSI
 * @vsi: VSI which is having rings allocated
 */
static int ice_vsi_alloc_rings(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int i;

	/* Allocate tx_rings */
	for (i = 0; i < vsi->alloc_txq; i++) {
		struct ice_ring *ring;

		/* allocate with kzalloc(), free with kfree_rcu() */
		ring = kzalloc(sizeof(*ring), GFP_KERNEL);

		if (!ring)
			goto err_out;

		ring->q_index = i;
		ring->reg_idx = vsi->txq_map[i];
		ring->ring_active = false;
		ring->vsi = vsi;
		ring->netdev = vsi->netdev;
		ring->dev = &pf->pdev->dev;
		ring->count = vsi->num_desc;

		vsi->tx_rings[i] = ring;
	}

	/* Allocate rx_rings */
	for (i = 0; i < vsi->alloc_rxq; i++) {
		struct ice_ring *ring;

		/* allocate with kzalloc(), free with kfree_rcu() */
		ring = kzalloc(sizeof(*ring), GFP_KERNEL);
		if (!ring)
			goto err_out;

		ring->q_index = i;
		ring->reg_idx = vsi->rxq_map[i];
		ring->ring_active = false;
		ring->vsi = vsi;
		ring->netdev = vsi->netdev;
		ring->dev = &pf->pdev->dev;
		ring->count = vsi->num_desc;
		vsi->rx_rings[i] = ring;
	}

	return 0;

err_out:
	ice_vsi_clear_rings(vsi);
	return -ENOMEM;
}

/**
 * ice_ena_misc_vector - enable the non-queue interrupts
 * @pf: board private structure
 */
static void ice_ena_misc_vector(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	u32 val;

	/* clear things first */
	wr32(hw, PFINT_OICR_ENA, 0);	/* disable all */
	rd32(hw, PFINT_OICR);		/* read to clear */

	val = (PFINT_OICR_HLP_RDY_M |
	       PFINT_OICR_CPM_RDY_M |
	       PFINT_OICR_ECC_ERR_M |
	       PFINT_OICR_MAL_DETECT_M |
	       PFINT_OICR_GRST_M |
	       PFINT_OICR_PCI_EXCEPTION_M |
	       PFINT_OICR_GPIO_M |
	       PFINT_OICR_STORM_DETECT_M |
	       PFINT_OICR_HMC_ERR_M);

	wr32(hw, PFINT_OICR_ENA, val);

	/* SW_ITR_IDX = 0, but don't change INTENA */
	wr32(hw, GLINT_DYN_CTL(pf->oicr_idx),
	     GLINT_DYN_CTL_SW_ITR_INDX_M | GLINT_DYN_CTL_INTENA_MSK_M);
}

/**
 * ice_misc_intr - misc interrupt handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t ice_misc_intr(int __always_unused irq, void *data)
{
	struct ice_pf *pf = (struct ice_pf *)data;
	struct ice_hw *hw = &pf->hw;
	irqreturn_t ret = IRQ_NONE;
	u32 oicr, ena_mask;

	set_bit(__ICE_ADMINQ_EVENT_PENDING, pf->state);

	oicr = rd32(hw, PFINT_OICR);
	ena_mask = rd32(hw, PFINT_OICR_ENA);

	if (!(oicr & PFINT_OICR_INTEVENT_M))
		goto ena_intr;

	if (oicr & PFINT_OICR_HMC_ERR_M) {
		ena_mask &= ~PFINT_OICR_HMC_ERR_M;
		dev_dbg(&pf->pdev->dev,
			"HMC Error interrupt - info 0x%x, data 0x%x\n",
			rd32(hw, PFHMC_ERRORINFO),
			rd32(hw, PFHMC_ERRORDATA));
	}

	/* Report and mask off any remaining unexpected interrupts */
	oicr &= ena_mask;
	if (oicr) {
		dev_dbg(&pf->pdev->dev, "unhandled interrupt oicr=0x%08x\n",
			oicr);
		/* If a critical error is pending there is no choice but to
		 * reset the device.
		 */
		if (oicr & (PFINT_OICR_PE_CRITERR_M |
			    PFINT_OICR_PCI_EXCEPTION_M |
			    PFINT_OICR_ECC_ERR_M))
			set_bit(__ICE_PFR_REQ, pf->state);

		ena_mask &= ~oicr;
	}
	ret = IRQ_HANDLED;

ena_intr:
	/* re-enable interrupt causes that are not handled during this pass */
	wr32(hw, PFINT_OICR_ENA, ena_mask);
	if (!test_bit(__ICE_DOWN, pf->state)) {
		ice_service_task_schedule(pf);
		ice_irq_dynamic_ena(hw);
	}

	return ret;
}

/**
 * ice_vsi_map_rings_to_vectors - Map VSI rings to interrupt vectors
 * @vsi: the VSI being configured
 *
 * This function maps descriptor rings to the queue-specific vectors allotted
 * through the MSI-X enabling code. On a constrained vector budget, we map Tx
 * and Rx rings to the vector as "efficiently" as possible.
 */
static void ice_vsi_map_rings_to_vectors(struct ice_vsi *vsi)
{
	int q_vectors = vsi->num_q_vectors;
	int tx_rings_rem, rx_rings_rem;
	int v_id;

	/* initially assigning remaining rings count to VSIs num queue value */
	tx_rings_rem = vsi->num_txq;
	rx_rings_rem = vsi->num_rxq;

	for (v_id = 0; v_id < q_vectors; v_id++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[v_id];
		int tx_rings_per_v, rx_rings_per_v, q_id, q_base;

		/* Tx rings mapping to vector */
		tx_rings_per_v = DIV_ROUND_UP(tx_rings_rem, q_vectors - v_id);
		q_vector->num_ring_tx = tx_rings_per_v;
		q_vector->tx.ring = NULL;
		q_base = vsi->num_txq - tx_rings_rem;

		for (q_id = q_base; q_id < (q_base + tx_rings_per_v); q_id++) {
			struct ice_ring *tx_ring = vsi->tx_rings[q_id];

			tx_ring->q_vector = q_vector;
			tx_ring->next = q_vector->tx.ring;
			q_vector->tx.ring = tx_ring;
		}
		tx_rings_rem -= tx_rings_per_v;

		/* Rx rings mapping to vector */
		rx_rings_per_v = DIV_ROUND_UP(rx_rings_rem, q_vectors - v_id);
		q_vector->num_ring_rx = rx_rings_per_v;
		q_vector->rx.ring = NULL;
		q_base = vsi->num_rxq - rx_rings_rem;

		for (q_id = q_base; q_id < (q_base + rx_rings_per_v); q_id++) {
			struct ice_ring *rx_ring = vsi->rx_rings[q_id];

			rx_ring->q_vector = q_vector;
			rx_ring->next = q_vector->rx.ring;
			q_vector->rx.ring = rx_ring;
		}
		rx_rings_rem -= rx_rings_per_v;
	}
}

/**
 * ice_vsi_set_num_qs - Set num queues, descriptors and vectors for a VSI
 * @vsi: the VSI being configured
 *
 * Return 0 on success and a negative value on error
 */
static void ice_vsi_set_num_qs(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;

	switch (vsi->type) {
	case ICE_VSI_PF:
		vsi->alloc_txq = pf->num_lan_tx;
		vsi->alloc_rxq = pf->num_lan_rx;
		vsi->num_desc = ALIGN(ICE_DFLT_NUM_DESC, ICE_REQ_DESC_MULTIPLE);
		vsi->num_q_vectors = max_t(int, pf->num_lan_rx, pf->num_lan_tx);
		break;
	default:
		dev_warn(&vsi->back->pdev->dev, "Unknown VSI type %d\n",
			 vsi->type);
		break;
	}
}

/**
 * ice_vsi_alloc_arrays - Allocate queue and vector pointer arrays for the vsi
 * @vsi: VSI pointer
 * @alloc_qvectors: a bool to specify if q_vectors need to be allocated.
 *
 * On error: returns error code (negative)
 * On success: returns 0
 */
static int ice_vsi_alloc_arrays(struct ice_vsi *vsi, bool alloc_qvectors)
{
	struct ice_pf *pf = vsi->back;

	/* allocate memory for both Tx and Rx ring pointers */
	vsi->tx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_txq,
				     sizeof(struct ice_ring *), GFP_KERNEL);
	if (!vsi->tx_rings)
		goto err_txrings;

	vsi->rx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_rxq,
				     sizeof(struct ice_ring *), GFP_KERNEL);
	if (!vsi->rx_rings)
		goto err_rxrings;

	if (alloc_qvectors) {
		/* allocate memory for q_vector pointers */
		vsi->q_vectors = devm_kcalloc(&pf->pdev->dev,
					      vsi->num_q_vectors,
					      sizeof(struct ice_q_vector *),
					      GFP_KERNEL);
		if (!vsi->q_vectors)
			goto err_vectors;
	}

	return 0;

err_vectors:
	devm_kfree(&pf->pdev->dev, vsi->rx_rings);
err_rxrings:
	devm_kfree(&pf->pdev->dev, vsi->tx_rings);
err_txrings:
	return -ENOMEM;
}

/**
 * ice_vsi_alloc - Allocates the next available struct vsi in the PF
 * @pf: board private structure
 * @type: type of VSI
 *
 * returns a pointer to a VSI on success, NULL on failure.
 */
static struct ice_vsi *ice_vsi_alloc(struct ice_pf *pf, enum ice_vsi_type type)
{
	struct ice_vsi *vsi = NULL;

	/* Need to protect the allocation of the VSIs at the PF level */
	mutex_lock(&pf->sw_mutex);

	/* If we have already allocated our maximum number of VSIs,
	 * pf->next_vsi will be ICE_NO_VSI. If not, pf->next_vsi index
	 * is available to be populated
	 */
	if (pf->next_vsi == ICE_NO_VSI) {
		dev_dbg(&pf->pdev->dev, "out of VSI slots!\n");
		goto unlock_pf;
	}

	vsi = devm_kzalloc(&pf->pdev->dev, sizeof(*vsi), GFP_KERNEL);
	if (!vsi)
		goto unlock_pf;

	vsi->type = type;
	vsi->back = pf;
	set_bit(__ICE_DOWN, vsi->state);
	vsi->idx = pf->next_vsi;
	vsi->work_lmt = ICE_DFLT_IRQ_WORK;

	ice_vsi_set_num_qs(vsi);

	switch (vsi->type) {
	case ICE_VSI_PF:
		if (ice_vsi_alloc_arrays(vsi, true))
			goto err_rings;

		break;
	default:
		dev_warn(&pf->pdev->dev, "Unknown VSI type %d\n", vsi->type);
		goto unlock_pf;
	}

	/* fill VSI slot in the PF struct */
	pf->vsi[pf->next_vsi] = vsi;

	/* prepare pf->next_vsi for next use */
	pf->next_vsi = ice_get_free_slot(pf->vsi, pf->num_alloc_vsi,
					 pf->next_vsi);
	goto unlock_pf;

err_rings:
	devm_kfree(&pf->pdev->dev, vsi);
	vsi = NULL;
unlock_pf:
	mutex_unlock(&pf->sw_mutex);
	return vsi;
}

/**
 * ice_free_irq_msix_misc - Unroll misc vector setup
 * @pf: board private structure
 */
static void ice_free_irq_msix_misc(struct ice_pf *pf)
{
	/* disable OICR interrupt */
	wr32(&pf->hw, PFINT_OICR_ENA, 0);
	ice_flush(&pf->hw);

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags) && pf->msix_entries) {
		synchronize_irq(pf->msix_entries[pf->oicr_idx].vector);
		devm_free_irq(&pf->pdev->dev,
			      pf->msix_entries[pf->oicr_idx].vector, pf);
	}

	ice_free_res(pf->irq_tracker, pf->oicr_idx, ICE_RES_MISC_VEC_ID);
}

/**
 * ice_req_irq_msix_misc - Setup the misc vector to handle non queue events
 * @pf: board private structure
 *
 * This sets up the handler for MSIX 0, which is used to manage the
 * non-queue interrupts, e.g. AdminQ and errors.  This is not used
 * when in MSI or Legacy interrupt mode.
 */
static int ice_req_irq_msix_misc(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	int oicr_idx, err = 0;
	u8 itr_gran;
	u32 val;

	if (!pf->int_name[0])
		snprintf(pf->int_name, sizeof(pf->int_name) - 1, "%s-%s:misc",
			 dev_driver_string(&pf->pdev->dev),
			 dev_name(&pf->pdev->dev));

	/* reserve one vector in irq_tracker for misc interrupts */
	oicr_idx = ice_get_res(pf, pf->irq_tracker, 1, ICE_RES_MISC_VEC_ID);
	if (oicr_idx < 0)
		return oicr_idx;

	pf->oicr_idx = oicr_idx;

	err = devm_request_irq(&pf->pdev->dev,
			       pf->msix_entries[pf->oicr_idx].vector,
			       ice_misc_intr, 0, pf->int_name, pf);
	if (err) {
		dev_err(&pf->pdev->dev,
			"devm_request_irq for %s failed: %d\n",
			pf->int_name, err);
		ice_free_res(pf->irq_tracker, 1, ICE_RES_MISC_VEC_ID);
		return err;
	}

	ice_ena_misc_vector(pf);

	val = (pf->oicr_idx & PFINT_OICR_CTL_MSIX_INDX_M) |
	      (ICE_RX_ITR & PFINT_OICR_CTL_ITR_INDX_M) |
	      PFINT_OICR_CTL_CAUSE_ENA_M;
	wr32(hw, PFINT_OICR_CTL, val);

	/* This enables Admin queue Interrupt causes */
	val = (pf->oicr_idx & PFINT_FW_CTL_MSIX_INDX_M) |
	      (ICE_RX_ITR & PFINT_FW_CTL_ITR_INDX_M) |
	      PFINT_FW_CTL_CAUSE_ENA_M;
	wr32(hw, PFINT_FW_CTL, val);

	itr_gran = hw->itr_gran_200;

	wr32(hw, GLINT_ITR(ICE_RX_ITR, pf->oicr_idx),
	     ITR_TO_REG(ICE_ITR_8K, itr_gran));

	ice_flush(hw);
	ice_irq_dynamic_ena(hw);

	return 0;
}

/**
 * ice_vsi_get_qs_contig - Assign a contiguous chunk of queues to VSI
 * @vsi: the VSI getting queues
 *
 * Return 0 on success and a negative value on error
 */
static int ice_vsi_get_qs_contig(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int offset, ret = 0;

	mutex_lock(&pf->avail_q_mutex);
	/* look for contiguous block of queues for tx */
	offset = bitmap_find_next_zero_area(pf->avail_txqs, ICE_MAX_TXQS,
					    0, vsi->alloc_txq, 0);
	if (offset < ICE_MAX_TXQS) {
		int i;

		bitmap_set(pf->avail_txqs, offset, vsi->alloc_txq);
		for (i = 0; i < vsi->alloc_txq; i++)
			vsi->txq_map[i] = i + offset;
	} else {
		ret = -ENOMEM;
		vsi->tx_mapping_mode = ICE_VSI_MAP_SCATTER;
	}

	/* look for contiguous block of queues for rx */
	offset = bitmap_find_next_zero_area(pf->avail_rxqs, ICE_MAX_RXQS,
					    0, vsi->alloc_rxq, 0);
	if (offset < ICE_MAX_RXQS) {
		int i;

		bitmap_set(pf->avail_rxqs, offset, vsi->alloc_rxq);
		for (i = 0; i < vsi->alloc_rxq; i++)
			vsi->rxq_map[i] = i + offset;
	} else {
		ret = -ENOMEM;
		vsi->rx_mapping_mode = ICE_VSI_MAP_SCATTER;
	}
	mutex_unlock(&pf->avail_q_mutex);

	return ret;
}

/**
 * ice_vsi_get_qs_scatter - Assign a scattered queues to VSI
 * @vsi: the VSI getting queues
 *
 * Return 0 on success and a negative value on error
 */
static int ice_vsi_get_qs_scatter(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int i, index = 0;

	mutex_lock(&pf->avail_q_mutex);

	if (vsi->tx_mapping_mode == ICE_VSI_MAP_SCATTER) {
		for (i = 0; i < vsi->alloc_txq; i++) {
			index = find_next_zero_bit(pf->avail_txqs,
						   ICE_MAX_TXQS, index);
			if (index < ICE_MAX_TXQS) {
				set_bit(index, pf->avail_txqs);
				vsi->txq_map[i] = index;
			} else {
				goto err_scatter_tx;
			}
		}
	}

	if (vsi->rx_mapping_mode == ICE_VSI_MAP_SCATTER) {
		for (i = 0; i < vsi->alloc_rxq; i++) {
			index = find_next_zero_bit(pf->avail_rxqs,
						   ICE_MAX_RXQS, index);
			if (index < ICE_MAX_RXQS) {
				set_bit(index, pf->avail_rxqs);
				vsi->rxq_map[i] = index;
			} else {
				goto err_scatter_rx;
			}
		}
	}

	mutex_unlock(&pf->avail_q_mutex);
	return 0;

err_scatter_rx:
	/* unflag any queues we have grabbed (i is failed position) */
	for (index = 0; index < i; index++) {
		clear_bit(vsi->rxq_map[index], pf->avail_rxqs);
		vsi->rxq_map[index] = 0;
	}
	i = vsi->alloc_txq;
err_scatter_tx:
	/* i is either position of failed attempt or vsi->alloc_txq */
	for (index = 0; index < i; index++) {
		clear_bit(vsi->txq_map[index], pf->avail_txqs);
		vsi->txq_map[index] = 0;
	}

	mutex_unlock(&pf->avail_q_mutex);
	return -ENOMEM;
}

/**
 * ice_vsi_get_qs - Assign queues from PF to VSI
 * @vsi: the VSI to assign queues to
 *
 * Returns 0 on success and a negative value on error
 */
static int ice_vsi_get_qs(struct ice_vsi *vsi)
{
	int ret = 0;

	vsi->tx_mapping_mode = ICE_VSI_MAP_CONTIG;
	vsi->rx_mapping_mode = ICE_VSI_MAP_CONTIG;

	/* NOTE: ice_vsi_get_qs_contig() will set the rx/tx mapping
	 * modes individually to scatter if assigning contiguous queues
	 * to rx or tx fails
	 */
	ret = ice_vsi_get_qs_contig(vsi);
	if (ret < 0) {
		if (vsi->tx_mapping_mode == ICE_VSI_MAP_SCATTER)
			vsi->alloc_txq = max_t(u16, vsi->alloc_txq,
					       ICE_MAX_SCATTER_TXQS);
		if (vsi->rx_mapping_mode == ICE_VSI_MAP_SCATTER)
			vsi->alloc_rxq = max_t(u16, vsi->alloc_rxq,
					       ICE_MAX_SCATTER_RXQS);
		ret = ice_vsi_get_qs_scatter(vsi);
	}

	return ret;
}

/**
 * ice_vsi_put_qs - Release queues from VSI to PF
 * @vsi: the VSI thats going to release queues
 */
static void ice_vsi_put_qs(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int i;

	mutex_lock(&pf->avail_q_mutex);

	for (i = 0; i < vsi->alloc_txq; i++) {
		clear_bit(vsi->txq_map[i], pf->avail_txqs);
		vsi->txq_map[i] = ICE_INVAL_Q_INDEX;
	}

	for (i = 0; i < vsi->alloc_rxq; i++) {
		clear_bit(vsi->rxq_map[i], pf->avail_rxqs);
		vsi->rxq_map[i] = ICE_INVAL_Q_INDEX;
	}

	mutex_unlock(&pf->avail_q_mutex);
}

/**
 * ice_free_q_vector - Free memory allocated for a specific interrupt vector
 * @vsi: VSI having the memory freed
 * @v_idx: index of the vector to be freed
 */
static void ice_free_q_vector(struct ice_vsi *vsi, int v_idx)
{
	struct ice_q_vector *q_vector;
	struct ice_ring *ring;

	if (!vsi->q_vectors[v_idx]) {
		dev_dbg(&vsi->back->pdev->dev, "Queue vector at index %d not found\n",
			v_idx);
		return;
	}
	q_vector = vsi->q_vectors[v_idx];

	ice_for_each_ring(ring, q_vector->tx)
		ring->q_vector = NULL;
	ice_for_each_ring(ring, q_vector->rx)
		ring->q_vector = NULL;

	/* only VSI with an associated netdev is set up with NAPI */
	if (vsi->netdev)
		netif_napi_del(&q_vector->napi);

	devm_kfree(&vsi->back->pdev->dev, q_vector);
	vsi->q_vectors[v_idx] = NULL;
}

/**
 * ice_vsi_free_q_vectors - Free memory allocated for interrupt vectors
 * @vsi: the VSI having memory freed
 */
static void ice_vsi_free_q_vectors(struct ice_vsi *vsi)
{
	int v_idx;

	for (v_idx = 0; v_idx < vsi->num_q_vectors; v_idx++)
		ice_free_q_vector(vsi, v_idx);
}

/**
 * ice_cfg_netdev - Setup the netdev flags
 * @vsi: the VSI being configured
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_cfg_netdev(struct ice_vsi *vsi)
{
	struct ice_netdev_priv *np;
	struct net_device *netdev;
	u8 mac_addr[ETH_ALEN];

	netdev = alloc_etherdev_mqs(sizeof(struct ice_netdev_priv),
				    vsi->alloc_txq, vsi->alloc_rxq);
	if (!netdev)
		return -ENOMEM;

	vsi->netdev = netdev;
	np = netdev_priv(netdev);
	np->vsi = vsi;

	/* set features that user can change */
	netdev->hw_features = NETIF_F_SG	|
			      NETIF_F_HIGHDMA	|
			      NETIF_F_RXHASH;

	/* enable features */
	netdev->features |= netdev->hw_features;

	if (vsi->type == ICE_VSI_PF) {
		SET_NETDEV_DEV(netdev, &vsi->back->pdev->dev);
		ether_addr_copy(mac_addr, vsi->port_info->mac.perm_addr);

		ether_addr_copy(netdev->dev_addr, mac_addr);
		ether_addr_copy(netdev->perm_addr, mac_addr);
	}

	netdev->priv_flags |= IFF_UNICAST_FLT;

	/* setup watchdog timeout value to be 5 second */
	netdev->watchdog_timeo = 5 * HZ;

	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = ICE_MAX_MTU;

	return 0;
}

/**
 * ice_vsi_free_arrays - clean up vsi resources
 * @vsi: pointer to VSI being cleared
 * @free_qvectors: bool to specify if q_vectors should be deallocated
 */
static void ice_vsi_free_arrays(struct ice_vsi *vsi, bool free_qvectors)
{
	struct ice_pf *pf = vsi->back;

	/* free the ring and vector containers */
	if (free_qvectors && vsi->q_vectors) {
		devm_kfree(&pf->pdev->dev, vsi->q_vectors);
		vsi->q_vectors = NULL;
	}
	if (vsi->tx_rings) {
		devm_kfree(&pf->pdev->dev, vsi->tx_rings);
		vsi->tx_rings = NULL;
	}
	if (vsi->rx_rings) {
		devm_kfree(&pf->pdev->dev, vsi->rx_rings);
		vsi->rx_rings = NULL;
	}
}

/**
 * ice_vsi_clear - clean up and deallocate the provided vsi
 * @vsi: pointer to VSI being cleared
 *
 * This deallocates the vsi's queue resources, removes it from the PF's
 * VSI array if necessary, and deallocates the VSI
 *
 * Returns 0 on success, negative on failure
 */
static int ice_vsi_clear(struct ice_vsi *vsi)
{
	struct ice_pf *pf = NULL;

	if (!vsi)
		return 0;

	if (!vsi->back)
		return -EINVAL;

	pf = vsi->back;

	if (!pf->vsi[vsi->idx] || pf->vsi[vsi->idx] != vsi) {
		dev_dbg(&pf->pdev->dev, "vsi does not exist at pf->vsi[%d]\n",
			vsi->idx);
		return -EINVAL;
	}

	mutex_lock(&pf->sw_mutex);
	/* updates the PF for this cleared vsi */

	pf->vsi[vsi->idx] = NULL;
	if (vsi->idx < pf->next_vsi)
		pf->next_vsi = vsi->idx;

	ice_vsi_free_arrays(vsi, true);
	mutex_unlock(&pf->sw_mutex);
	devm_kfree(&pf->pdev->dev, vsi);

	return 0;
}

/**
 * ice_vsi_alloc_q_vector - Allocate memory for a single interrupt vector
 * @vsi: the VSI being configured
 * @v_idx: index of the vector in the vsi struct
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 */
static int ice_vsi_alloc_q_vector(struct ice_vsi *vsi, int v_idx)
{
	struct ice_pf *pf = vsi->back;
	struct ice_q_vector *q_vector;

	/* allocate q_vector */
	q_vector = devm_kzalloc(&pf->pdev->dev, sizeof(*q_vector), GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	q_vector->vsi = vsi;
	q_vector->v_idx = v_idx;
	/* only set affinity_mask if the CPU is online */
	if (cpu_online(v_idx))
		cpumask_set_cpu(v_idx, &q_vector->affinity_mask);

	/* tie q_vector and vsi together */
	vsi->q_vectors[v_idx] = q_vector;

	return 0;
}

/**
 * ice_vsi_alloc_q_vectors - Allocate memory for interrupt vectors
 * @vsi: the VSI being configured
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 */
static int ice_vsi_alloc_q_vectors(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int v_idx = 0, num_q_vectors;
	int err;

	if (vsi->q_vectors[0]) {
		dev_dbg(&pf->pdev->dev, "VSI %d has existing q_vectors\n",
			vsi->vsi_num);
		return -EEXIST;
	}

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		num_q_vectors = vsi->num_q_vectors;
	} else {
		err = -EINVAL;
		goto err_out;
	}

	for (v_idx = 0; v_idx < num_q_vectors; v_idx++) {
		err = ice_vsi_alloc_q_vector(vsi, v_idx);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	while (v_idx--)
		ice_free_q_vector(vsi, v_idx);

	dev_err(&pf->pdev->dev,
		"Failed to allocate %d q_vector for VSI %d, ret=%d\n",
		vsi->num_q_vectors, vsi->vsi_num, err);
	vsi->num_q_vectors = 0;
	return err;
}

/**
 * ice_vsi_setup_vector_base - Set up the base vector for the given VSI
 * @vsi: ptr to the VSI
 *
 * This should only be called after ice_vsi_alloc() which allocates the
 * corresponding SW VSI structure and initializes num_queue_pairs for the
 * newly allocated VSI.
 *
 * Returns 0 on success or negative on failure
 */
static int ice_vsi_setup_vector_base(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int num_q_vectors = 0;

	if (vsi->base_vector) {
		dev_dbg(&pf->pdev->dev, "VSI %d has non-zero base vector %d\n",
			vsi->vsi_num, vsi->base_vector);
		return -EEXIST;
	}

	if (!test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
		return -ENOENT;

	switch (vsi->type) {
	case ICE_VSI_PF:
		num_q_vectors = vsi->num_q_vectors;
		break;
	default:
		dev_warn(&vsi->back->pdev->dev, "Unknown VSI type %d\n",
			 vsi->type);
		break;
	}

	if (num_q_vectors)
		vsi->base_vector = ice_get_res(pf, pf->irq_tracker,
					       num_q_vectors, vsi->idx);

	if (vsi->base_vector < 0) {
		dev_err(&pf->pdev->dev,
			"Failed to get tracking for %d vectors for VSI %d, err=%d\n",
			num_q_vectors, vsi->vsi_num, vsi->base_vector);
		return -ENOENT;
	}

	return 0;
}

/**
 * ice_vsi_setup - Set up a VSI by a given type
 * @pf: board private structure
 * @type: VSI type
 * @pi: pointer to the port_info instance
 *
 * This allocates the sw VSI structure and its queue resources.
 *
 * Returns pointer to the successfully allocated and configure VSI sw struct on
 * success, otherwise returns NULL on failure.
 */
static struct ice_vsi *
ice_vsi_setup(struct ice_pf *pf, enum ice_vsi_type type,
	      struct ice_port_info *pi)
{
	struct device *dev = &pf->pdev->dev;
	struct ice_vsi_ctx ctxt = { 0 };
	struct ice_vsi *vsi;
	int ret;

	vsi = ice_vsi_alloc(pf, type);
	if (!vsi) {
		dev_err(dev, "could not allocate VSI\n");
		return NULL;
	}

	vsi->port_info = pi;
	vsi->vsw = pf->first_sw;

	if (ice_vsi_get_qs(vsi)) {
		dev_err(dev, "Failed to allocate queues. vsi->idx = %d\n",
			vsi->idx);
		goto err_get_qs;
	}

	/* create the VSI */
	ret = ice_vsi_add(vsi);
	if (ret)
		goto err_vsi;

	ctxt.vsi_num = vsi->vsi_num;

	switch (vsi->type) {
	case ICE_VSI_PF:
		ret = ice_cfg_netdev(vsi);
		if (ret)
			goto err_cfg_netdev;

		ret = register_netdev(vsi->netdev);
		if (ret)
			goto err_register_netdev;

		netif_carrier_off(vsi->netdev);

		/* make sure transmit queues start off as stopped */
		netif_tx_stop_all_queues(vsi->netdev);
		ret = ice_vsi_alloc_q_vectors(vsi);
		if (ret)
			goto err_msix;

		ret = ice_vsi_setup_vector_base(vsi);
		if (ret)
			goto err_rings;

		ret = ice_vsi_alloc_rings(vsi);
		if (ret)
			goto err_rings;

		ice_vsi_map_rings_to_vectors(vsi);

		break;
	default:
		/* if vsi type is not recognized, clean up the resources and
		 * exit
		 */
		goto err_rings;
	}
	return vsi;

err_rings:
	ice_vsi_free_q_vectors(vsi);
err_msix:
	if (vsi->netdev && vsi->netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(vsi->netdev);
err_register_netdev:
	if (vsi->netdev) {
		free_netdev(vsi->netdev);
		vsi->netdev = NULL;
	}
err_cfg_netdev:
	ret = ice_aq_free_vsi(&pf->hw, &ctxt, false, NULL);
	if (ret)
		dev_err(&vsi->back->pdev->dev,
			"Free VSI AQ call failed, err %d\n", ret);
err_vsi:
	ice_vsi_put_qs(vsi);
err_get_qs:
	pf->q_left_tx += vsi->alloc_txq;
	pf->q_left_rx += vsi->alloc_rxq;
	ice_vsi_clear(vsi);

	return NULL;
}

/**
 * ice_setup_pf_sw - Setup the HW switch on startup or after reset
 * @pf: board private structure
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_setup_pf_sw(struct ice_pf *pf)
{
	struct ice_vsi *vsi;
	int status = 0;

	vsi = ice_vsi_setup(pf, ICE_VSI_PF, pf->hw.port_info);
	if (!vsi) {
		status = -ENOMEM;
		goto error_exit;
	}

error_exit:
	if (vsi) {
		ice_vsi_free_q_vectors(vsi);
		if (vsi->netdev && vsi->netdev->reg_state == NETREG_REGISTERED)
			unregister_netdev(vsi->netdev);
		if (vsi->netdev) {
			free_netdev(vsi->netdev);
			vsi->netdev = NULL;
		}
		ice_vsi_delete(vsi);
		ice_vsi_put_qs(vsi);
		pf->q_left_tx += vsi->alloc_txq;
		pf->q_left_rx += vsi->alloc_rxq;
		ice_vsi_clear(vsi);
	}
	return status;
}

/**
 * ice_determine_q_usage - Calculate queue distribution
 * @pf: board private structure
 *
 * Return -ENOMEM if we don't get enough queues for all ports
 */
static void ice_determine_q_usage(struct ice_pf *pf)
{
	u16 q_left_tx, q_left_rx;

	q_left_tx = pf->hw.func_caps.common_cap.num_txq;
	q_left_rx = pf->hw.func_caps.common_cap.num_rxq;

	/* initial support for only 1 tx and 1 rx queue */
	pf->num_lan_tx = 1;
	pf->num_lan_rx = 1;

	pf->q_left_tx = q_left_tx - pf->num_lan_tx;
	pf->q_left_rx = q_left_rx - pf->num_lan_rx;
}

/**
 * ice_deinit_pf - Unrolls initialziations done by ice_init_pf
 * @pf: board private structure to initialize
 */
static void ice_deinit_pf(struct ice_pf *pf)
{
	if (pf->serv_tmr.function)
		del_timer_sync(&pf->serv_tmr);
	if (pf->serv_task.func)
		cancel_work_sync(&pf->serv_task);
	mutex_destroy(&pf->sw_mutex);
	mutex_destroy(&pf->avail_q_mutex);
}

/**
 * ice_init_pf - Initialize general software structures (struct ice_pf)
 * @pf: board private structure to initialize
 */
static void ice_init_pf(struct ice_pf *pf)
{
	bitmap_zero(pf->flags, ICE_PF_FLAGS_NBITS);
	set_bit(ICE_FLAG_MSIX_ENA, pf->flags);

	mutex_init(&pf->sw_mutex);
	mutex_init(&pf->avail_q_mutex);

	/* Clear avail_[t|r]x_qs bitmaps (set all to avail) */
	mutex_lock(&pf->avail_q_mutex);
	bitmap_zero(pf->avail_txqs, ICE_MAX_TXQS);
	bitmap_zero(pf->avail_rxqs, ICE_MAX_RXQS);
	mutex_unlock(&pf->avail_q_mutex);

	/* setup service timer and periodic service task */
	timer_setup(&pf->serv_tmr, ice_service_timer, 0);
	pf->serv_tmr_period = HZ;
	INIT_WORK(&pf->serv_task, ice_service_task);
	clear_bit(__ICE_SERVICE_SCHED, pf->state);
}

/**
 * ice_ena_msix_range - Request a range of MSIX vectors from the OS
 * @pf: board private structure
 *
 * compute the number of MSIX vectors required (v_budget) and request from
 * the OS. Return the number of vectors reserved or negative on failure
 */
static int ice_ena_msix_range(struct ice_pf *pf)
{
	int v_left, v_actual, v_budget = 0;
	int needed, err, i;

	v_left = pf->hw.func_caps.common_cap.num_msix_vectors;

	/* reserve one vector for miscellaneous handler */
	needed = 1;
	v_budget += needed;
	v_left -= needed;

	/* reserve vectors for LAN traffic */
	pf->num_lan_msix = min_t(int, num_online_cpus(), v_left);
	v_budget += pf->num_lan_msix;

	pf->msix_entries = devm_kcalloc(&pf->pdev->dev, v_budget,
					sizeof(struct msix_entry), GFP_KERNEL);

	if (!pf->msix_entries) {
		err = -ENOMEM;
		goto exit_err;
	}

	for (i = 0; i < v_budget; i++)
		pf->msix_entries[i].entry = i;

	/* actually reserve the vectors */
	v_actual = pci_enable_msix_range(pf->pdev, pf->msix_entries,
					 ICE_MIN_MSIX, v_budget);

	if (v_actual < 0) {
		dev_err(&pf->pdev->dev, "unable to reserve MSI-X vectors\n");
		err = v_actual;
		goto msix_err;
	}

	if (v_actual < v_budget) {
		dev_warn(&pf->pdev->dev,
			 "not enough vectors. requested = %d, obtained = %d\n",
			 v_budget, v_actual);
		if (v_actual >= (pf->num_lan_msix + 1)) {
			pf->num_avail_msix = v_actual - (pf->num_lan_msix + 1);
		} else if (v_actual >= 2) {
			pf->num_lan_msix = 1;
			pf->num_avail_msix = v_actual - 2;
		} else {
			pci_disable_msix(pf->pdev);
			err = -ERANGE;
			goto msix_err;
		}
	}

	return v_actual;

msix_err:
	devm_kfree(&pf->pdev->dev, pf->msix_entries);
	goto exit_err;

exit_err:
	pf->num_lan_msix = 0;
	clear_bit(ICE_FLAG_MSIX_ENA, pf->flags);
	return err;
}

/**
 * ice_dis_msix - Disable MSI-X interrupt setup in OS
 * @pf: board private structure
 */
static void ice_dis_msix(struct ice_pf *pf)
{
	pci_disable_msix(pf->pdev);
	devm_kfree(&pf->pdev->dev, pf->msix_entries);
	pf->msix_entries = NULL;
	clear_bit(ICE_FLAG_MSIX_ENA, pf->flags);
}

/**
 * ice_init_interrupt_scheme - Determine proper interrupt scheme
 * @pf: board private structure to initialize
 */
static int ice_init_interrupt_scheme(struct ice_pf *pf)
{
	int vectors = 0;
	ssize_t size;

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
		vectors = ice_ena_msix_range(pf);
	else
		return -ENODEV;

	if (vectors < 0)
		return vectors;

	/* set up vector assignment tracking */
	size = sizeof(struct ice_res_tracker) + (sizeof(u16) * vectors);

	pf->irq_tracker = devm_kzalloc(&pf->pdev->dev, size, GFP_KERNEL);
	if (!pf->irq_tracker) {
		ice_dis_msix(pf);
		return -ENOMEM;
	}

	pf->irq_tracker->num_entries = vectors;

	return 0;
}

/**
 * ice_clear_interrupt_scheme - Undo things done by ice_init_interrupt_scheme
 * @pf: board private structure
 */
static void ice_clear_interrupt_scheme(struct ice_pf *pf)
{
	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
		ice_dis_msix(pf);

	devm_kfree(&pf->pdev->dev, pf->irq_tracker);
	pf->irq_tracker = NULL;
}

/**
 * ice_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in ice_pci_tbl
 *
 * Returns 0 on success, negative on failure
 */
static int ice_probe(struct pci_dev *pdev,
		     const struct pci_device_id __always_unused *ent)
{
	struct ice_pf *pf;
	struct ice_hw *hw;
	int err;

	/* this driver uses devres, see Documentation/driver-model/devres.txt */
	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, BIT(ICE_BAR0), pci_name(pdev));
	if (err) {
		dev_err(&pdev->dev, "I/O map error %d\n", err);
		return err;
	}

	pf = devm_kzalloc(&pdev->dev, sizeof(*pf), GFP_KERNEL);
	if (!pf)
		return -ENOMEM;

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "DMA configuration failed: 0x%x\n", err);
		return err;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	pf->pdev = pdev;
	pci_set_drvdata(pdev, pf);
	set_bit(__ICE_DOWN, pf->state);

	hw = &pf->hw;
	hw->hw_addr = pcim_iomap_table(pdev)[ICE_BAR0];
	hw->back = pf;
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;
	hw->bus.device = PCI_SLOT(pdev->devfn);
	hw->bus.func = PCI_FUNC(pdev->devfn);
	ice_set_ctrlq_len(hw);

	pf->msg_enable = netif_msg_init(debug, ICE_DFLT_NETIF_M);

#ifndef CONFIG_DYNAMIC_DEBUG
	if (debug < -1)
		hw->debug_mask = debug;
#endif

	err = ice_init_hw(hw);
	if (err) {
		dev_err(&pdev->dev, "ice_init_hw failed: %d\n", err);
		err = -EIO;
		goto err_exit_unroll;
	}

	dev_info(&pdev->dev, "firmware %d.%d.%05d api %d.%d\n",
		 hw->fw_maj_ver, hw->fw_min_ver, hw->fw_build,
		 hw->api_maj_ver, hw->api_min_ver);

	ice_init_pf(pf);

	ice_determine_q_usage(pf);

	pf->num_alloc_vsi = min_t(u16, ICE_MAX_VSI_ALLOC,
				  hw->func_caps.guaranteed_num_vsi);
	if (!pf->num_alloc_vsi) {
		err = -EIO;
		goto err_init_pf_unroll;
	}

	pf->vsi = devm_kcalloc(&pdev->dev, pf->num_alloc_vsi,
			       sizeof(struct ice_vsi *), GFP_KERNEL);
	if (!pf->vsi) {
		err = -ENOMEM;
		goto err_init_pf_unroll;
	}

	err = ice_init_interrupt_scheme(pf);
	if (err) {
		dev_err(&pdev->dev,
			"ice_init_interrupt_scheme failed: %d\n", err);
		err = -EIO;
		goto err_init_interrupt_unroll;
	}

	/* In case of MSIX we are going to setup the misc vector right here
	 * to handle admin queue events etc. In case of legacy and MSI
	 * the misc functionality and queue processing is combined in
	 * the same vector and that gets setup at open.
	 */
	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		err = ice_req_irq_msix_misc(pf);
		if (err) {
			dev_err(&pdev->dev,
				"setup of misc vector failed: %d\n", err);
			goto err_init_interrupt_unroll;
		}
	}

	/* create switch struct for the switch element created by FW on boot */
	pf->first_sw = devm_kzalloc(&pdev->dev, sizeof(struct ice_sw),
				    GFP_KERNEL);
	if (!pf->first_sw) {
		err = -ENOMEM;
		goto err_msix_misc_unroll;
	}

	pf->first_sw->bridge_mode = BRIDGE_MODE_VEB;
	pf->first_sw->pf = pf;

	/* record the sw_id available for later use */
	pf->first_sw->sw_id = hw->port_info->sw_id;

	err = ice_setup_pf_sw(pf);
	if (err) {
		dev_err(&pdev->dev,
			"probe failed due to setup pf switch:%d\n", err);
		goto err_alloc_sw_unroll;
	}
	return 0;

err_alloc_sw_unroll:
	set_bit(__ICE_DOWN, pf->state);
	devm_kfree(&pf->pdev->dev, pf->first_sw);
err_msix_misc_unroll:
	ice_free_irq_msix_misc(pf);
err_init_interrupt_unroll:
	ice_clear_interrupt_scheme(pf);
	devm_kfree(&pdev->dev, pf->vsi);
err_init_pf_unroll:
	ice_deinit_pf(pf);
	ice_deinit_hw(hw);
err_exit_unroll:
	pci_disable_pcie_error_reporting(pdev);
	return err;
}

/**
 * ice_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void ice_remove(struct pci_dev *pdev)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);
	int i = 0;
	int err;

	if (!pf)
		return;

	set_bit(__ICE_DOWN, pf->state);

	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (!pf->vsi[i])
			continue;

		err = ice_vsi_release(pf->vsi[i]);
		if (err)
			dev_dbg(&pf->pdev->dev, "Failed to release VSI index %d (err %d)\n",
				i, err);
	}

	ice_free_irq_msix_misc(pf);
	ice_clear_interrupt_scheme(pf);
	ice_deinit_pf(pf);
	ice_deinit_hw(&pf->hw);
	pci_disable_pcie_error_reporting(pdev);
}

/* ice_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id ice_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_BACKPLANE), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_QSFP), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_SFP), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_10G_BASE_T), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_SGMII), 0 },
	/* required last entry */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ice_pci_tbl);

static struct pci_driver ice_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ice_pci_tbl,
	.probe = ice_probe,
	.remove = ice_remove,
};

/**
 * ice_module_init - Driver registration routine
 *
 * ice_module_init is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init ice_module_init(void)
{
	int status;

	pr_info("%s - version %s\n", ice_driver_string, ice_drv_ver);
	pr_info("%s\n", ice_copyright);

	ice_wq = alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM, KBUILD_MODNAME);
	if (!ice_wq) {
		pr_err("Failed to create workqueue\n");
		return -ENOMEM;
	}

	status = pci_register_driver(&ice_driver);
	if (status) {
		pr_err("failed to register pci driver, err %d\n", status);
		destroy_workqueue(ice_wq);
	}

	return status;
}
module_init(ice_module_init);

/**
 * ice_module_exit - Driver exit cleanup routine
 *
 * ice_module_exit is called just before the driver is removed
 * from memory.
 */
static void __exit ice_module_exit(void)
{
	pci_unregister_driver(&ice_driver);
	destroy_workqueue(ice_wq);
	pr_info("module unloaded\n");
}
module_exit(ice_module_exit);

/**
 * ice_vsi_release - Delete a VSI and free its resources
 * @vsi: the VSI being removed
 *
 * Returns 0 on success or < 0 on error
 */
static int ice_vsi_release(struct ice_vsi *vsi)
{
	struct ice_pf *pf;

	if (!vsi->back)
		return -ENODEV;
	pf = vsi->back;

	if (vsi->netdev) {
		unregister_netdev(vsi->netdev);
		free_netdev(vsi->netdev);
		vsi->netdev = NULL;
	}

	/* reclaim interrupt vectors back to PF */
	ice_free_res(vsi->back->irq_tracker, vsi->base_vector, vsi->idx);
	pf->num_avail_msix += vsi->num_q_vectors;

	ice_vsi_delete(vsi);
	ice_vsi_free_q_vectors(vsi);
	ice_vsi_clear_rings(vsi);

	ice_vsi_put_qs(vsi);
	pf->q_left_tx += vsi->alloc_txq;
	pf->q_left_rx += vsi->alloc_rxq;

	ice_vsi_clear(vsi);

	return 0;
}
