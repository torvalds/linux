/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include "i40e.h"

/***********************misc routines*****************************/

/**
 * i40e_vc_isvalid_vsi_id
 * @vf: pointer to the vf info
 * @vsi_id: vf relative vsi id
 *
 * check for the valid vsi id
 **/
static inline bool i40e_vc_isvalid_vsi_id(struct i40e_vf *vf, u8 vsi_id)
{
	struct i40e_pf *pf = vf->pf;

	return pf->vsi[vsi_id]->vf_id == vf->vf_id;
}

/**
 * i40e_vc_isvalid_queue_id
 * @vf: pointer to the vf info
 * @vsi_id: vsi id
 * @qid: vsi relative queue id
 *
 * check for the valid queue id
 **/
static inline bool i40e_vc_isvalid_queue_id(struct i40e_vf *vf, u8 vsi_id,
					    u8 qid)
{
	struct i40e_pf *pf = vf->pf;

	return qid < pf->vsi[vsi_id]->num_queue_pairs;
}

/**
 * i40e_vc_isvalid_vector_id
 * @vf: pointer to the vf info
 * @vector_id: vf relative vector id
 *
 * check for the valid vector id
 **/
static inline bool i40e_vc_isvalid_vector_id(struct i40e_vf *vf, u8 vector_id)
{
	struct i40e_pf *pf = vf->pf;

	return vector_id < pf->hw.func_caps.num_msix_vectors_vf;
}

/***********************vf resource mgmt routines*****************/

/**
 * i40e_vc_get_pf_queue_id
 * @vf: pointer to the vf info
 * @vsi_idx: index of VSI in PF struct
 * @vsi_queue_id: vsi relative queue id
 *
 * return pf relative queue id
 **/
static u16 i40e_vc_get_pf_queue_id(struct i40e_vf *vf, u8 vsi_idx,
				   u8 vsi_queue_id)
{
	struct i40e_pf *pf = vf->pf;
	struct i40e_vsi *vsi = pf->vsi[vsi_idx];
	u16 pf_queue_id = I40E_QUEUE_END_OF_LIST;

	if (le16_to_cpu(vsi->info.mapping_flags) &
	    I40E_AQ_VSI_QUE_MAP_NONCONTIG)
		pf_queue_id =
			le16_to_cpu(vsi->info.queue_mapping[vsi_queue_id]);
	else
		pf_queue_id = le16_to_cpu(vsi->info.queue_mapping[0]) +
			      vsi_queue_id;

	return pf_queue_id;
}

/**
 * i40e_ctrl_vsi_tx_queue
 * @vf: pointer to the vf info
 * @vsi_idx: index of VSI in PF struct
 * @vsi_queue_id: vsi relative queue index
 * @ctrl: control flags
 *
 * enable/disable/enable check/disable check
 **/
static int i40e_ctrl_vsi_tx_queue(struct i40e_vf *vf, u16 vsi_idx,
				  u16 vsi_queue_id,
				  enum i40e_queue_ctrl ctrl)
{
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	bool writeback = false;
	u16 pf_queue_id;
	int ret = 0;
	u32 reg;

	pf_queue_id = i40e_vc_get_pf_queue_id(vf, vsi_idx, vsi_queue_id);
	reg = rd32(hw, I40E_QTX_ENA(pf_queue_id));

	switch (ctrl) {
	case I40E_QUEUE_CTRL_ENABLE:
		reg |= I40E_QTX_ENA_QENA_REQ_MASK;
		writeback = true;
		break;
	case I40E_QUEUE_CTRL_ENABLECHECK:
		ret = (reg & I40E_QTX_ENA_QENA_STAT_MASK) ? 0 : -EPERM;
		break;
	case I40E_QUEUE_CTRL_DISABLE:
		reg &= ~I40E_QTX_ENA_QENA_REQ_MASK;
		writeback = true;
		break;
	case I40E_QUEUE_CTRL_DISABLECHECK:
		ret = (reg & I40E_QTX_ENA_QENA_STAT_MASK) ? -EPERM : 0;
		break;
	case I40E_QUEUE_CTRL_FASTDISABLE:
		reg |= I40E_QTX_ENA_FAST_QDIS_MASK;
		writeback = true;
		break;
	case I40E_QUEUE_CTRL_FASTDISABLECHECK:
		ret = (reg & I40E_QTX_ENA_QENA_STAT_MASK) ? -EPERM : 0;
		if (!ret) {
			reg &= ~I40E_QTX_ENA_FAST_QDIS_MASK;
			writeback = true;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (writeback) {
		wr32(hw, I40E_QTX_ENA(pf_queue_id), reg);
		i40e_flush(hw);
	}

	return ret;
}

/**
 * i40e_ctrl_vsi_rx_queue
 * @vf: pointer to the vf info
 * @vsi_idx: index of VSI in PF struct
 * @vsi_queue_id: vsi relative queue index
 * @ctrl: control flags
 *
 * enable/disable/enable check/disable check
 **/
static int i40e_ctrl_vsi_rx_queue(struct i40e_vf *vf, u16 vsi_idx,
				  u16 vsi_queue_id,
				  enum i40e_queue_ctrl ctrl)
{
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	bool writeback = false;
	u16 pf_queue_id;
	int ret = 0;
	u32 reg;

	pf_queue_id = i40e_vc_get_pf_queue_id(vf, vsi_idx, vsi_queue_id);
	reg = rd32(hw, I40E_QRX_ENA(pf_queue_id));

	switch (ctrl) {
	case I40E_QUEUE_CTRL_ENABLE:
		reg |= I40E_QRX_ENA_QENA_REQ_MASK;
		writeback = true;
		break;
	case I40E_QUEUE_CTRL_ENABLECHECK:
		ret = (reg & I40E_QRX_ENA_QENA_STAT_MASK) ? 0 : -EPERM;
		break;
	case I40E_QUEUE_CTRL_DISABLE:
		reg &= ~I40E_QRX_ENA_QENA_REQ_MASK;
		writeback = true;
		break;
	case I40E_QUEUE_CTRL_DISABLECHECK:
		ret = (reg & I40E_QRX_ENA_QENA_STAT_MASK) ? -EPERM : 0;
		break;
	case I40E_QUEUE_CTRL_FASTDISABLE:
		reg |= I40E_QRX_ENA_FAST_QDIS_MASK;
		writeback = true;
		break;
	case I40E_QUEUE_CTRL_FASTDISABLECHECK:
		ret = (reg & I40E_QRX_ENA_QENA_STAT_MASK) ? -EPERM : 0;
		if (!ret) {
			reg &= ~I40E_QRX_ENA_FAST_QDIS_MASK;
			writeback = true;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (writeback) {
		wr32(hw, I40E_QRX_ENA(pf_queue_id), reg);
		i40e_flush(hw);
	}

	return ret;
}

/**
 * i40e_config_irq_link_list
 * @vf: pointer to the vf info
 * @vsi_idx: index of VSI in PF struct
 * @vecmap: irq map info
 *
 * configure irq link list from the map
 **/
static void i40e_config_irq_link_list(struct i40e_vf *vf, u16 vsi_idx,
				      struct i40e_virtchnl_vector_map *vecmap)
{
	unsigned long linklistmap = 0, tempmap;
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	u16 vsi_queue_id, pf_queue_id;
	enum i40e_queue_type qtype;
	u16 next_q, vector_id;
	u32 reg, reg_idx;
	u16 itr_idx = 0;

	vector_id = vecmap->vector_id;
	/* setup the head */
	if (0 == vector_id)
		reg_idx = I40E_VPINT_LNKLST0(vf->vf_id);
	else
		reg_idx = I40E_VPINT_LNKLSTN(
			    ((pf->hw.func_caps.num_msix_vectors_vf - 1)
					      * vf->vf_id) + (vector_id - 1));

	if (vecmap->rxq_map == 0 && vecmap->txq_map == 0) {
		/* Special case - No queues mapped on this vector */
		wr32(hw, reg_idx, I40E_VPINT_LNKLST0_FIRSTQ_INDX_MASK);
		goto irq_list_done;
	}
	tempmap = vecmap->rxq_map;
	vsi_queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (vsi_queue_id < I40E_MAX_VSI_QP) {
		linklistmap |= (1 <<
				(I40E_VIRTCHNL_SUPPORTED_QTYPES *
				 vsi_queue_id));
		vsi_queue_id =
		    find_next_bit(&tempmap, I40E_MAX_VSI_QP, vsi_queue_id + 1);
	}

	tempmap = vecmap->txq_map;
	vsi_queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (vsi_queue_id < I40E_MAX_VSI_QP) {
		linklistmap |= (1 <<
				(I40E_VIRTCHNL_SUPPORTED_QTYPES * vsi_queue_id
				 + 1));
		vsi_queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					     vsi_queue_id + 1);
	}

	next_q = find_first_bit(&linklistmap,
				(I40E_MAX_VSI_QP *
				 I40E_VIRTCHNL_SUPPORTED_QTYPES));
	vsi_queue_id = next_q/I40E_VIRTCHNL_SUPPORTED_QTYPES;
	qtype = next_q%I40E_VIRTCHNL_SUPPORTED_QTYPES;
	pf_queue_id = i40e_vc_get_pf_queue_id(vf, vsi_idx, vsi_queue_id);
	reg = ((qtype << I40E_VPINT_LNKLSTN_FIRSTQ_TYPE_SHIFT) | pf_queue_id);

	wr32(hw, reg_idx, reg);

	while (next_q < (I40E_MAX_VSI_QP * I40E_VIRTCHNL_SUPPORTED_QTYPES)) {
		switch (qtype) {
		case I40E_QUEUE_TYPE_RX:
			reg_idx = I40E_QINT_RQCTL(pf_queue_id);
			itr_idx = vecmap->rxitr_idx;
			break;
		case I40E_QUEUE_TYPE_TX:
			reg_idx = I40E_QINT_TQCTL(pf_queue_id);
			itr_idx = vecmap->txitr_idx;
			break;
		default:
			break;
		}

		next_q = find_next_bit(&linklistmap,
				       (I40E_MAX_VSI_QP *
					I40E_VIRTCHNL_SUPPORTED_QTYPES),
				       next_q + 1);
		if (next_q < (I40E_MAX_VSI_QP * I40E_VIRTCHNL_SUPPORTED_QTYPES)) {
			vsi_queue_id = next_q / I40E_VIRTCHNL_SUPPORTED_QTYPES;
			qtype = next_q % I40E_VIRTCHNL_SUPPORTED_QTYPES;
			pf_queue_id = i40e_vc_get_pf_queue_id(vf, vsi_idx,
							      vsi_queue_id);
		} else {
			pf_queue_id = I40E_QUEUE_END_OF_LIST;
			qtype = 0;
		}

		/* format for the RQCTL & TQCTL regs is same */
		reg = (vector_id) |
		    (qtype << I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT) |
		    (pf_queue_id << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT) |
		    (1 << I40E_QINT_RQCTL_CAUSE_ENA_SHIFT) |
		    (itr_idx << I40E_QINT_RQCTL_ITR_INDX_SHIFT);
		wr32(hw, reg_idx, reg);
	}

irq_list_done:
	i40e_flush(hw);
}

/**
 * i40e_config_vsi_tx_queue
 * @vf: pointer to the vf info
 * @vsi_idx: index of VSI in PF struct
 * @vsi_queue_id: vsi relative queue index
 * @info: config. info
 *
 * configure tx queue
 **/
static int i40e_config_vsi_tx_queue(struct i40e_vf *vf, u16 vsi_idx,
				    u16 vsi_queue_id,
				    struct i40e_virtchnl_txq_info *info)
{
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_hmc_obj_txq tx_ctx;
	u16 pf_queue_id;
	u32 qtx_ctl;
	int ret = 0;

	pf_queue_id = i40e_vc_get_pf_queue_id(vf, vsi_idx, vsi_queue_id);

	/* clear the context structure first */
	memset(&tx_ctx, 0, sizeof(struct i40e_hmc_obj_txq));

	/* only set the required fields */
	tx_ctx.base = info->dma_ring_addr / 128;
	tx_ctx.qlen = info->ring_len;
	tx_ctx.rdylist = le16_to_cpu(pf->vsi[vsi_idx]->info.qs_handle[0]);
	tx_ctx.rdylist_act = 0;

	/* clear the context in the HMC */
	ret = i40e_clear_lan_tx_queue_context(hw, pf_queue_id);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"Failed to clear VF LAN Tx queue context %d, error: %d\n",
			pf_queue_id, ret);
		ret = -ENOENT;
		goto error_context;
	}

	/* set the context in the HMC */
	ret = i40e_set_lan_tx_queue_context(hw, pf_queue_id, &tx_ctx);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"Failed to set VF LAN Tx queue context %d error: %d\n",
			pf_queue_id, ret);
		ret = -ENOENT;
		goto error_context;
	}

	/* associate this queue with the PCI VF function */
	qtx_ctl = I40E_QTX_CTL_VF_QUEUE;
	qtx_ctl |= ((hw->hmc.hmc_fn_id << I40E_QTX_CTL_PF_INDX_SHIFT)
		    & I40E_QTX_CTL_PF_INDX_MASK);
	qtx_ctl |= (((vf->vf_id + hw->func_caps.vf_base_id)
		     << I40E_QTX_CTL_VFVM_INDX_SHIFT)
		    & I40E_QTX_CTL_VFVM_INDX_MASK);
	wr32(hw, I40E_QTX_CTL(pf_queue_id), qtx_ctl);
	i40e_flush(hw);

error_context:
	return ret;
}

/**
 * i40e_config_vsi_rx_queue
 * @vf: pointer to the vf info
 * @vsi_idx: index of VSI in PF struct
 * @vsi_queue_id: vsi relative queue index
 * @info: config. info
 *
 * configure rx queue
 **/
static int i40e_config_vsi_rx_queue(struct i40e_vf *vf, u16 vsi_idx,
				    u16 vsi_queue_id,
				    struct i40e_virtchnl_rxq_info *info)
{
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_hmc_obj_rxq rx_ctx;
	u16 pf_queue_id;
	int ret = 0;

	pf_queue_id = i40e_vc_get_pf_queue_id(vf, vsi_idx, vsi_queue_id);

	/* clear the context structure first */
	memset(&rx_ctx, 0, sizeof(struct i40e_hmc_obj_rxq));

	/* only set the required fields */
	rx_ctx.base = info->dma_ring_addr / 128;
	rx_ctx.qlen = info->ring_len;

	if (info->splithdr_enabled) {
		rx_ctx.hsplit_0 = I40E_RX_SPLIT_L2      |
				  I40E_RX_SPLIT_IP      |
				  I40E_RX_SPLIT_TCP_UDP |
				  I40E_RX_SPLIT_SCTP;
		/* header length validation */
		if (info->hdr_size > ((2 * 1024) - 64)) {
			ret = -EINVAL;
			goto error_param;
		}
		rx_ctx.hbuff = info->hdr_size >> I40E_RXQ_CTX_HBUFF_SHIFT;

		/* set splitalways mode 10b */
		rx_ctx.dtype = 0x2;
	}

	/* databuffer length validation */
	if (info->databuffer_size > ((16 * 1024) - 128)) {
		ret = -EINVAL;
		goto error_param;
	}
	rx_ctx.dbuff = info->databuffer_size >> I40E_RXQ_CTX_DBUFF_SHIFT;

	/* max pkt. length validation */
	if (info->max_pkt_size >= (16 * 1024) || info->max_pkt_size < 64) {
		ret = -EINVAL;
		goto error_param;
	}
	rx_ctx.rxmax = info->max_pkt_size;

	/* enable 32bytes desc always */
	rx_ctx.dsize = 1;

	/* default values */
	rx_ctx.tphrdesc_ena = 1;
	rx_ctx.tphwdesc_ena = 1;
	rx_ctx.tphdata_ena = 1;
	rx_ctx.tphhead_ena = 1;
	rx_ctx.lrxqthresh = 2;
	rx_ctx.crcstrip = 1;

	/* clear the context in the HMC */
	ret = i40e_clear_lan_rx_queue_context(hw, pf_queue_id);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"Failed to clear VF LAN Rx queue context %d, error: %d\n",
			pf_queue_id, ret);
		ret = -ENOENT;
		goto error_param;
	}

	/* set the context in the HMC */
	ret = i40e_set_lan_rx_queue_context(hw, pf_queue_id, &rx_ctx);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"Failed to set VF LAN Rx queue context %d error: %d\n",
			pf_queue_id, ret);
		ret = -ENOENT;
		goto error_param;
	}

error_param:
	return ret;
}

/**
 * i40e_alloc_vsi_res
 * @vf: pointer to the vf info
 * @type: type of VSI to allocate
 *
 * alloc vf vsi context & resources
 **/
static int i40e_alloc_vsi_res(struct i40e_vf *vf, enum i40e_vsi_type type)
{
	struct i40e_mac_filter *f = NULL;
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vsi *vsi;
	int ret = 0;

	vsi = i40e_vsi_setup(pf, type, pf->vsi[pf->lan_vsi]->seid, vf->vf_id);

	if (!vsi) {
		dev_err(&pf->pdev->dev,
			"add vsi failed for vf %d, aq_err %d\n",
			vf->vf_id, pf->hw.aq.asq_last_status);
		ret = -ENOENT;
		goto error_alloc_vsi_res;
	}
	if (type == I40E_VSI_SRIOV) {
		vf->lan_vsi_index = vsi->idx;
		vf->lan_vsi_id = vsi->id;
		dev_info(&pf->pdev->dev,
			 "LAN VSI index %d, VSI id %d\n",
			 vsi->idx, vsi->id);
		f = i40e_add_filter(vsi, vf->default_lan_addr.addr,
				    0, true, false);
	}
	if (!f) {
		dev_err(&pf->pdev->dev, "Unable to add ucast filter\n");
		ret = -ENOMEM;
		goto error_alloc_vsi_res;
	}

	/* program mac filter */
	ret = i40e_sync_vsi_filters(vsi);
	if (ret) {
		dev_err(&pf->pdev->dev, "Unable to program ucast filters\n");
		goto error_alloc_vsi_res;
	}

	/* accept bcast pkts. by default */
	ret = i40e_aq_set_vsi_broadcast(hw, vsi->seid, true, NULL);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"set vsi bcast failed for vf %d, vsi %d, aq_err %d\n",
			vf->vf_id, vsi->idx, pf->hw.aq.asq_last_status);
		ret = -EINVAL;
	}

error_alloc_vsi_res:
	return ret;
}

/**
 * i40e_reset_vf
 * @vf: pointer to the vf structure
 * @flr: VFLR was issued or not
 *
 * reset the vf
 **/
int i40e_reset_vf(struct i40e_vf *vf, bool flr)
{
	int ret = -ENOENT;
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	u32 reg, reg_idx, msix_vf;
	bool rsd = false;
	u16 pf_queue_id;
	int i, j;

	/* warn the VF */
	wr32(hw, I40E_VFGEN_RSTAT1(vf->vf_id), I40E_VFR_INPROGRESS);

	clear_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states);

	/* PF triggers VFR only when VF requests, in case of
	 * VFLR, HW triggers VFR
	 */
	if (!flr) {
		/* reset vf using VPGEN_VFRTRIG reg */
		reg = I40E_VPGEN_VFRTRIG_VFSWR_MASK;
		wr32(hw, I40E_VPGEN_VFRTRIG(vf->vf_id), reg);
		i40e_flush(hw);
	}

	/* poll VPGEN_VFRSTAT reg to make sure
	 * that reset is complete
	 */
	for (i = 0; i < 4; i++) {
		/* vf reset requires driver to first reset the
		 * vf & than poll the status register to make sure
		 * that the requested op was completed
		 * successfully
		 */
		udelay(10);
		reg = rd32(hw, I40E_VPGEN_VFRSTAT(vf->vf_id));
		if (reg & I40E_VPGEN_VFRSTAT_VFRD_MASK) {
			rsd = true;
			break;
		}
	}

	if (!rsd)
		dev_err(&pf->pdev->dev, "VF reset check timeout %d\n",
			vf->vf_id);

	/* fast disable qps */
	for (j = 0; j < pf->vsi[vf->lan_vsi_index]->num_queue_pairs; j++) {
		ret = i40e_ctrl_vsi_tx_queue(vf, vf->lan_vsi_index, j,
					     I40E_QUEUE_CTRL_FASTDISABLE);
		ret = i40e_ctrl_vsi_rx_queue(vf, vf->lan_vsi_index, j,
					     I40E_QUEUE_CTRL_FASTDISABLE);
	}

	/* Queue enable/disable requires driver to
	 * first reset the vf & than poll the status register
	 * to make sure that the requested op was completed
	 * successfully
	 */
	udelay(10);
	for (j = 0; j < pf->vsi[vf->lan_vsi_index]->num_queue_pairs; j++) {
		ret = i40e_ctrl_vsi_tx_queue(vf, vf->lan_vsi_index, j,
					     I40E_QUEUE_CTRL_FASTDISABLECHECK);
		if (ret)
			dev_info(&pf->pdev->dev,
				 "Queue control check failed on Tx queue %d of VSI %d VF %d\n",
				 vf->lan_vsi_index, j, vf->vf_id);
		ret = i40e_ctrl_vsi_rx_queue(vf, vf->lan_vsi_index, j,
					     I40E_QUEUE_CTRL_FASTDISABLECHECK);
		if (ret)
			dev_info(&pf->pdev->dev,
				 "Queue control check failed on Rx queue %d of VSI %d VF %d\n",
				 vf->lan_vsi_index, j, vf->vf_id);
	}

	/* clear the irq settings */
	msix_vf = pf->hw.func_caps.num_msix_vectors_vf;
	for (i = 0; i < msix_vf; i++) {
		/* format is same for both registers */
		if (0 == i)
			reg_idx = I40E_VPINT_LNKLST0(vf->vf_id);
		else
			reg_idx = I40E_VPINT_LNKLSTN(((msix_vf - 1) *
						      (vf->vf_id))
						     + (i - 1));
		reg = (I40E_VPINT_LNKLSTN_FIRSTQ_TYPE_MASK |
		       I40E_VPINT_LNKLSTN_FIRSTQ_INDX_MASK);
		wr32(hw, reg_idx, reg);
		i40e_flush(hw);
	}
	/* disable interrupts so the VF starts in a known state */
	for (i = 0; i < msix_vf; i++) {
		/* format is same for both registers */
		if (0 == i)
			reg_idx = I40E_VFINT_DYN_CTL0(vf->vf_id);
		else
			reg_idx = I40E_VFINT_DYN_CTLN(((msix_vf - 1) *
						      (vf->vf_id))
						     + (i - 1));
		wr32(hw, reg_idx, I40E_VFINT_DYN_CTLN_CLEARPBA_MASK);
		i40e_flush(hw);
	}

	/* set the defaults for the rqctl & tqctl registers */
	reg = (I40E_QINT_RQCTL_NEXTQ_INDX_MASK | I40E_QINT_RQCTL_ITR_INDX_MASK |
	       I40E_QINT_RQCTL_NEXTQ_TYPE_MASK);
	for (j = 0; j < pf->vsi[vf->lan_vsi_index]->num_queue_pairs; j++) {
		pf_queue_id = i40e_vc_get_pf_queue_id(vf, vf->lan_vsi_index, j);
		wr32(hw, I40E_QINT_RQCTL(pf_queue_id), reg);
		wr32(hw, I40E_QINT_TQCTL(pf_queue_id), reg);
	}

	/* clear the reset bit in the VPGEN_VFRTRIG reg */
	reg = rd32(hw, I40E_VPGEN_VFRTRIG(vf->vf_id));
	reg &= ~I40E_VPGEN_VFRTRIG_VFSWR_MASK;
	wr32(hw, I40E_VPGEN_VFRTRIG(vf->vf_id), reg);
	/* tell the VF the reset is done */
	wr32(hw, I40E_VFGEN_RSTAT1(vf->vf_id), I40E_VFR_COMPLETED);
	i40e_flush(hw);

	return ret;
}

/**
 * i40e_enable_vf_mappings
 * @vf: pointer to the vf info
 *
 * enable vf mappings
 **/
static void i40e_enable_vf_mappings(struct i40e_vf *vf)
{
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	u32 reg, total_queue_pairs = 0;
	int j;

	/* Tell the hardware we're using noncontiguous mapping. HW requires
	 * that VF queues be mapped using this method, even when they are
	 * contiguous in real life
	 */
	wr32(hw, I40E_VSILAN_QBASE(vf->lan_vsi_id),
	     I40E_VSILAN_QBASE_VSIQTABLE_ENA_MASK);

	/* enable VF vplan_qtable mappings */
	reg = I40E_VPLAN_MAPENA_TXRX_ENA_MASK;
	wr32(hw, I40E_VPLAN_MAPENA(vf->vf_id), reg);

	/* map PF queues to VF queues */
	for (j = 0; j < pf->vsi[vf->lan_vsi_index]->num_queue_pairs; j++) {
		u16 qid = i40e_vc_get_pf_queue_id(vf, vf->lan_vsi_index, j);
		reg = (qid & I40E_VPLAN_QTABLE_QINDEX_MASK);
		wr32(hw, I40E_VPLAN_QTABLE(total_queue_pairs, vf->vf_id), reg);
		total_queue_pairs++;
	}

	/* map PF queues to VSI */
	for (j = 0; j < 7; j++) {
		if (j * 2 >= pf->vsi[vf->lan_vsi_index]->num_queue_pairs) {
			reg = 0x07FF07FF;	/* unused */
		} else {
			u16 qid = i40e_vc_get_pf_queue_id(vf, vf->lan_vsi_index,
							  j * 2);
			reg = qid;
			qid = i40e_vc_get_pf_queue_id(vf, vf->lan_vsi_index,
						      (j * 2) + 1);
			reg |= qid << 16;
		}
		wr32(hw, I40E_VSILAN_QTABLE(j, vf->lan_vsi_id), reg);
	}

	i40e_flush(hw);
}

/**
 * i40e_disable_vf_mappings
 * @vf: pointer to the vf info
 *
 * disable vf mappings
 **/
static void i40e_disable_vf_mappings(struct i40e_vf *vf)
{
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	int i;

	/* disable qp mappings */
	wr32(hw, I40E_VPLAN_MAPENA(vf->vf_id), 0);
	for (i = 0; i < I40E_MAX_VSI_QP; i++)
		wr32(hw, I40E_VPLAN_QTABLE(i, vf->vf_id),
		     I40E_QUEUE_END_OF_LIST);
	i40e_flush(hw);
}

/**
 * i40e_free_vf_res
 * @vf: pointer to the vf info
 *
 * free vf resources
 **/
static void i40e_free_vf_res(struct i40e_vf *vf)
{
	struct i40e_pf *pf = vf->pf;

	/* free vsi & disconnect it from the parent uplink */
	if (vf->lan_vsi_index) {
		i40e_vsi_release(pf->vsi[vf->lan_vsi_index]);
		vf->lan_vsi_index = 0;
		vf->lan_vsi_id = 0;
	}
	/* reset some of the state varibles keeping
	 * track of the resources
	 */
	vf->num_queue_pairs = 0;
	vf->vf_states = 0;
}

/**
 * i40e_alloc_vf_res
 * @vf: pointer to the vf info
 *
 * allocate vf resources
 **/
static int i40e_alloc_vf_res(struct i40e_vf *vf)
{
	struct i40e_pf *pf = vf->pf;
	int total_queue_pairs = 0;
	int ret;

	/* allocate hw vsi context & associated resources */
	ret = i40e_alloc_vsi_res(vf, I40E_VSI_SRIOV);
	if (ret)
		goto error_alloc;
	total_queue_pairs += pf->vsi[vf->lan_vsi_index]->num_queue_pairs;
	set_bit(I40E_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps);

	/* store the total qps number for the runtime
	 * vf req validation
	 */
	vf->num_queue_pairs = total_queue_pairs;

	/* vf is now completely initialized */
	set_bit(I40E_VF_STAT_INIT, &vf->vf_states);

error_alloc:
	if (ret)
		i40e_free_vf_res(vf);

	return ret;
}

/**
 * i40e_vfs_are_assigned
 * @pf: pointer to the pf structure
 *
 * Determine if any VFs are assigned to VMs
 **/
static bool i40e_vfs_are_assigned(struct i40e_pf *pf)
{
	struct pci_dev *pdev = pf->pdev;
	struct pci_dev *vfdev;

	/* loop through all the VFs to see if we own any that are assigned */
	vfdev = pci_get_device(PCI_VENDOR_ID_INTEL, I40E_VF_DEVICE_ID , NULL);
	while (vfdev) {
		/* if we don't own it we don't care */
		if (vfdev->is_virtfn && pci_physfn(vfdev) == pdev) {
			/* if it is assigned we cannot release it */
			if (vfdev->dev_flags & PCI_DEV_FLAGS_ASSIGNED)
				return true;
		}

		vfdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				       I40E_VF_DEVICE_ID,
				       vfdev);
	}

	return false;
}

/**
 * i40e_free_vfs
 * @pf: pointer to the pf structure
 *
 * free vf resources
 **/
void i40e_free_vfs(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	int i;

	if (!pf->vf)
		return;

	/* Disable interrupt 0 so we don't try to handle the VFLR. */
	wr32(hw, I40E_PFINT_DYN_CTL0, 0);
	i40e_flush(hw);

	/* free up vf resources */
	for (i = 0; i < pf->num_alloc_vfs; i++) {
		if (test_bit(I40E_VF_STAT_INIT, &pf->vf[i].vf_states))
			i40e_free_vf_res(&pf->vf[i]);
		/* disable qp mappings */
		i40e_disable_vf_mappings(&pf->vf[i]);
	}

	kfree(pf->vf);
	pf->vf = NULL;
	pf->num_alloc_vfs = 0;

	if (!i40e_vfs_are_assigned(pf))
		pci_disable_sriov(pf->pdev);
	else
		dev_warn(&pf->pdev->dev,
			 "unable to disable SR-IOV because VFs are assigned.\n");

	/* Re-enable interrupt 0. */
	wr32(hw, I40E_PFINT_DYN_CTL0,
	     I40E_PFINT_DYN_CTL0_INTENA_MASK |
	     I40E_PFINT_DYN_CTL0_CLEARPBA_MASK |
	     (I40E_ITR_NONE << I40E_PFINT_DYN_CTL0_ITR_INDX_SHIFT));
	i40e_flush(hw);
}

#ifdef CONFIG_PCI_IOV
/**
 * i40e_alloc_vfs
 * @pf: pointer to the pf structure
 * @num_alloc_vfs: number of vfs to allocate
 *
 * allocate vf resources
 **/
static int i40e_alloc_vfs(struct i40e_pf *pf, u16 num_alloc_vfs)
{
	struct i40e_vf *vfs;
	int i, ret = 0;

	ret = pci_enable_sriov(pf->pdev, num_alloc_vfs);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"pci_enable_sriov failed with error %d!\n", ret);
		pf->num_alloc_vfs = 0;
		goto err_iov;
	}

	/* allocate memory */
	vfs = kzalloc(num_alloc_vfs * sizeof(struct i40e_vf), GFP_KERNEL);
	if (!vfs) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* apply default profile */
	for (i = 0; i < num_alloc_vfs; i++) {
		vfs[i].pf = pf;
		vfs[i].parent_type = I40E_SWITCH_ELEMENT_TYPE_VEB;
		vfs[i].vf_id = i;

		/* assign default capabilities */
		set_bit(I40E_VIRTCHNL_VF_CAP_L2, &vfs[i].vf_caps);

		ret = i40e_alloc_vf_res(&vfs[i]);
		i40e_reset_vf(&vfs[i], true);
		if (ret)
			break;

		/* enable vf vplan_qtable mappings */
		i40e_enable_vf_mappings(&vfs[i]);
	}
	pf->vf = vfs;
	pf->num_alloc_vfs = num_alloc_vfs;

err_alloc:
	if (ret)
		i40e_free_vfs(pf);
err_iov:
	return ret;
}

#endif
/**
 * i40e_pci_sriov_enable
 * @pdev: pointer to a pci_dev structure
 * @num_vfs: number of vfs to allocate
 *
 * Enable or change the number of VFs
 **/
static int i40e_pci_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	int pre_existing_vfs = pci_num_vf(pdev);
	int err = 0;

	dev_info(&pdev->dev, "Allocating %d VFs.\n", num_vfs);
	if (pre_existing_vfs && pre_existing_vfs != num_vfs)
		i40e_free_vfs(pf);
	else if (pre_existing_vfs && pre_existing_vfs == num_vfs)
		goto out;

	if (num_vfs > pf->num_req_vfs) {
		err = -EPERM;
		goto err_out;
	}

	err = i40e_alloc_vfs(pf, num_vfs);
	if (err) {
		dev_warn(&pdev->dev, "Failed to enable SR-IOV: %d\n", err);
		goto err_out;
	}

out:
	return num_vfs;

err_out:
	return err;
#endif
	return 0;
}

/**
 * i40e_pci_sriov_configure
 * @pdev: pointer to a pci_dev structure
 * @num_vfs: number of vfs to allocate
 *
 * Enable or change the number of VFs. Called when the user updates the number
 * of VFs in sysfs.
 **/
int i40e_pci_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);

	if (num_vfs)
		return i40e_pci_sriov_enable(pdev, num_vfs);

	i40e_free_vfs(pf);
	return 0;
}

/***********************virtual channel routines******************/

/**
 * i40e_vc_send_msg_to_vf
 * @vf: pointer to the vf info
 * @v_opcode: virtual channel opcode
 * @v_retval: virtual channel return value
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * send msg to vf
 **/
static int i40e_vc_send_msg_to_vf(struct i40e_vf *vf, u32 v_opcode,
				  u32 v_retval, u8 *msg, u16 msglen)
{
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	i40e_status aq_ret;

	/* single place to detect unsuccessful return values */
	if (v_retval) {
		vf->num_invalid_msgs++;
		dev_err(&pf->pdev->dev, "Failed opcode %d Error: %d\n",
			v_opcode, v_retval);
		if (vf->num_invalid_msgs >
		    I40E_DEFAULT_NUM_INVALID_MSGS_ALLOWED) {
			dev_err(&pf->pdev->dev,
				"Number of invalid messages exceeded for VF %d\n",
				vf->vf_id);
			dev_err(&pf->pdev->dev, "Use PF Control I/F to enable the VF\n");
			set_bit(I40E_VF_STAT_DISABLED, &vf->vf_states);
		}
	} else {
		vf->num_valid_msgs++;
	}

	aq_ret = i40e_aq_send_msg_to_vf(hw, vf->vf_id, v_opcode, v_retval,
				     msg, msglen, NULL);
	if (aq_ret) {
		dev_err(&pf->pdev->dev,
			"Unable to send the message to VF %d aq_err %d\n",
			vf->vf_id, pf->hw.aq.asq_last_status);
		return -EIO;
	}

	return 0;
}

/**
 * i40e_vc_send_resp_to_vf
 * @vf: pointer to the vf info
 * @opcode: operation code
 * @retval: return value
 *
 * send resp msg to vf
 **/
static int i40e_vc_send_resp_to_vf(struct i40e_vf *vf,
				   enum i40e_virtchnl_ops opcode,
				   i40e_status retval)
{
	return i40e_vc_send_msg_to_vf(vf, opcode, retval, NULL, 0);
}

/**
 * i40e_vc_get_version_msg
 * @vf: pointer to the vf info
 *
 * called from the vf to request the API version used by the PF
 **/
static int i40e_vc_get_version_msg(struct i40e_vf *vf)
{
	struct i40e_virtchnl_version_info info = {
		I40E_VIRTCHNL_VERSION_MAJOR, I40E_VIRTCHNL_VERSION_MINOR
	};

	return i40e_vc_send_msg_to_vf(vf, I40E_VIRTCHNL_OP_VERSION,
				      I40E_SUCCESS, (u8 *)&info,
				      sizeof(struct
					     i40e_virtchnl_version_info));
}

/**
 * i40e_vc_get_vf_resources_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf to request its resources
 **/
static int i40e_vc_get_vf_resources_msg(struct i40e_vf *vf)
{
	struct i40e_virtchnl_vf_resource *vfres = NULL;
	struct i40e_pf *pf = vf->pf;
	i40e_status aq_ret = 0;
	struct i40e_vsi *vsi;
	int i = 0, len = 0;
	int num_vsis = 1;
	int ret;

	if (!test_bit(I40E_VF_STAT_INIT, &vf->vf_states)) {
		aq_ret = I40E_ERR_PARAM;
		goto err;
	}

	len = (sizeof(struct i40e_virtchnl_vf_resource) +
	       sizeof(struct i40e_virtchnl_vsi_resource) * num_vsis);

	vfres = kzalloc(len, GFP_KERNEL);
	if (!vfres) {
		aq_ret = I40E_ERR_NO_MEMORY;
		len = 0;
		goto err;
	}

	vfres->vf_offload_flags = I40E_VIRTCHNL_VF_OFFLOAD_L2;
	vsi = pf->vsi[vf->lan_vsi_index];
	if (!vsi->info.pvid)
		vfres->vf_offload_flags |= I40E_VIRTCHNL_VF_OFFLOAD_VLAN;

	vfres->num_vsis = num_vsis;
	vfres->num_queue_pairs = vf->num_queue_pairs;
	vfres->max_vectors = pf->hw.func_caps.num_msix_vectors_vf;
	if (vf->lan_vsi_index) {
		vfres->vsi_res[i].vsi_id = vf->lan_vsi_index;
		vfres->vsi_res[i].vsi_type = I40E_VSI_SRIOV;
		vfres->vsi_res[i].num_queue_pairs =
		    pf->vsi[vf->lan_vsi_index]->num_queue_pairs;
		memcpy(vfres->vsi_res[i].default_mac_addr,
		       vf->default_lan_addr.addr, ETH_ALEN);
		i++;
	}
	set_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states);

err:
	/* send the response back to the vf */
	ret = i40e_vc_send_msg_to_vf(vf, I40E_VIRTCHNL_OP_GET_VF_RESOURCES,
				     aq_ret, (u8 *)vfres, len);

	kfree(vfres);
	return ret;
}

/**
 * i40e_vc_reset_vf_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf to reset itself,
 * unlike other virtchnl messages, pf driver
 * doesn't send the response back to the vf
 **/
static int i40e_vc_reset_vf_msg(struct i40e_vf *vf)
{
	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states))
		return -ENOENT;

	return i40e_reset_vf(vf, false);
}

/**
 * i40e_vc_config_promiscuous_mode_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf to configure the promiscuous mode of
 * vf vsis
 **/
static int i40e_vc_config_promiscuous_mode_msg(struct i40e_vf *vf,
					       u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_promisc_info *info =
	    (struct i40e_virtchnl_promisc_info *)msg;
	struct i40e_pf *pf = vf->pf;
	struct i40e_hw *hw = &pf->hw;
	bool allmulti = false;
	bool promisc = false;
	i40e_status aq_ret;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states) ||
	    !test_bit(I40E_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps) ||
	    !i40e_vc_isvalid_vsi_id(vf, info->vsi_id) ||
	    (pf->vsi[info->vsi_id]->type != I40E_VSI_FCOE)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	if (info->flags & I40E_FLAG_VF_UNICAST_PROMISC)
		promisc = true;
	aq_ret = i40e_aq_set_vsi_unicast_promiscuous(hw, info->vsi_id,
						     promisc, NULL);
	if (aq_ret)
		goto error_param;

	if (info->flags & I40E_FLAG_VF_MULTICAST_PROMISC)
		allmulti = true;
	aq_ret = i40e_aq_set_vsi_multicast_promiscuous(hw, info->vsi_id,
						       allmulti, NULL);

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf,
				       I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
				       aq_ret);
}

/**
 * i40e_vc_config_queues_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf to configure the rx/tx
 * queues
 **/
static int i40e_vc_config_queues_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_vsi_queue_config_info *qci =
	    (struct i40e_virtchnl_vsi_queue_config_info *)msg;
	struct i40e_virtchnl_queue_pair_info *qpi;
	u16 vsi_id, vsi_queue_id;
	i40e_status aq_ret = 0;
	int i;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	vsi_id = qci->vsi_id;
	if (!i40e_vc_isvalid_vsi_id(vf, vsi_id)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}
	for (i = 0; i < qci->num_queue_pairs; i++) {
		qpi = &qci->qpair[i];
		vsi_queue_id = qpi->txq.queue_id;
		if ((qpi->txq.vsi_id != vsi_id) ||
		    (qpi->rxq.vsi_id != vsi_id) ||
		    (qpi->rxq.queue_id != vsi_queue_id) ||
		    !i40e_vc_isvalid_queue_id(vf, vsi_id, vsi_queue_id)) {
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}

		if (i40e_config_vsi_rx_queue(vf, vsi_id, vsi_queue_id,
					     &qpi->rxq) ||
		    i40e_config_vsi_tx_queue(vf, vsi_id, vsi_queue_id,
					     &qpi->txq)) {
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}
	}

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES,
				       aq_ret);
}

/**
 * i40e_vc_config_irq_map_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf to configure the irq to
 * queue map
 **/
static int i40e_vc_config_irq_map_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_irq_map_info *irqmap_info =
	    (struct i40e_virtchnl_irq_map_info *)msg;
	struct i40e_virtchnl_vector_map *map;
	u16 vsi_id, vsi_queue_id, vector_id;
	i40e_status aq_ret = 0;
	unsigned long tempmap;
	int i;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < irqmap_info->num_vectors; i++) {
		map = &irqmap_info->vecmap[i];

		vector_id = map->vector_id;
		vsi_id = map->vsi_id;
		/* validate msg params */
		if (!i40e_vc_isvalid_vector_id(vf, vector_id) ||
		    !i40e_vc_isvalid_vsi_id(vf, vsi_id)) {
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}

		/* lookout for the invalid queue index */
		tempmap = map->rxq_map;
		vsi_queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
		while (vsi_queue_id < I40E_MAX_VSI_QP) {
			if (!i40e_vc_isvalid_queue_id(vf, vsi_id,
						      vsi_queue_id)) {
				aq_ret = I40E_ERR_PARAM;
				goto error_param;
			}
			vsi_queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
						     vsi_queue_id + 1);
		}

		tempmap = map->txq_map;
		vsi_queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
		while (vsi_queue_id < I40E_MAX_VSI_QP) {
			if (!i40e_vc_isvalid_queue_id(vf, vsi_id,
						      vsi_queue_id)) {
				aq_ret = I40E_ERR_PARAM;
				goto error_param;
			}
			vsi_queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
						     vsi_queue_id + 1);
		}

		i40e_config_irq_link_list(vf, vsi_id, map);
	}
error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP,
				       aq_ret);
}

/**
 * i40e_vc_enable_queues_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf to enable all or specific queue(s)
 **/
static int i40e_vc_enable_queues_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_queue_select *vqs =
	    (struct i40e_virtchnl_queue_select *)msg;
	struct i40e_pf *pf = vf->pf;
	u16 vsi_id = vqs->vsi_id;
	i40e_status aq_ret = 0;
	unsigned long tempmap;
	u16 queue_id;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	if (!i40e_vc_isvalid_vsi_id(vf, vsi_id)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	if ((0 == vqs->rx_queues) && (0 == vqs->tx_queues)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	tempmap = vqs->rx_queues;
	queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (queue_id < I40E_MAX_VSI_QP) {
		if (!i40e_vc_isvalid_queue_id(vf, vsi_id, queue_id)) {
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}
		i40e_ctrl_vsi_rx_queue(vf, vsi_id, queue_id,
				       I40E_QUEUE_CTRL_ENABLE);

		queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					 queue_id + 1);
	}

	tempmap = vqs->tx_queues;
	queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (queue_id < I40E_MAX_VSI_QP) {
		if (!i40e_vc_isvalid_queue_id(vf, vsi_id, queue_id)) {
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}
		i40e_ctrl_vsi_tx_queue(vf, vsi_id, queue_id,
				       I40E_QUEUE_CTRL_ENABLE);

		queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					 queue_id + 1);
	}

	/* Poll the status register to make sure that the
	 * requested op was completed successfully
	 */
	udelay(10);

	tempmap = vqs->rx_queues;
	queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (queue_id < I40E_MAX_VSI_QP) {
		if (i40e_ctrl_vsi_rx_queue(vf, vsi_id, queue_id,
					   I40E_QUEUE_CTRL_ENABLECHECK)) {
			dev_err(&pf->pdev->dev,
				"Queue control check failed on RX queue %d of VSI %d VF %d\n",
				queue_id, vsi_id, vf->vf_id);
		}
		queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					 queue_id + 1);
	}

	tempmap = vqs->tx_queues;
	queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (queue_id < I40E_MAX_VSI_QP) {
		if (i40e_ctrl_vsi_tx_queue(vf, vsi_id, queue_id,
					   I40E_QUEUE_CTRL_ENABLECHECK)) {
			dev_err(&pf->pdev->dev,
				"Queue control check failed on TX queue %d of VSI %d VF %d\n",
				queue_id, vsi_id, vf->vf_id);
		}
		queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					 queue_id + 1);
	}

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_ENABLE_QUEUES,
				       aq_ret);
}

/**
 * i40e_vc_disable_queues_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf to disable all or specific
 * queue(s)
 **/
static int i40e_vc_disable_queues_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_queue_select *vqs =
	    (struct i40e_virtchnl_queue_select *)msg;
	struct i40e_pf *pf = vf->pf;
	u16 vsi_id = vqs->vsi_id;
	i40e_status aq_ret = 0;
	unsigned long tempmap;
	u16 queue_id;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	if (!i40e_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	if ((0 == vqs->rx_queues) && (0 == vqs->tx_queues)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	tempmap = vqs->rx_queues;
	queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (queue_id < I40E_MAX_VSI_QP) {
		if (!i40e_vc_isvalid_queue_id(vf, vsi_id, queue_id)) {
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}
		i40e_ctrl_vsi_rx_queue(vf, vsi_id, queue_id,
				       I40E_QUEUE_CTRL_DISABLE);

		queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					 queue_id + 1);
	}

	tempmap = vqs->tx_queues;
	queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (queue_id < I40E_MAX_VSI_QP) {
		if (!i40e_vc_isvalid_queue_id(vf, vsi_id, queue_id)) {
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}
		i40e_ctrl_vsi_tx_queue(vf, vsi_id, queue_id,
				       I40E_QUEUE_CTRL_DISABLE);

		queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					 queue_id + 1);
	}

	/* Poll the status register to make sure that the
	 * requested op was completed successfully
	 */
	udelay(10);

	tempmap = vqs->rx_queues;
	queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (queue_id < I40E_MAX_VSI_QP) {
		if (i40e_ctrl_vsi_rx_queue(vf, vsi_id, queue_id,
					   I40E_QUEUE_CTRL_DISABLECHECK)) {
			dev_err(&pf->pdev->dev,
				"Queue control check failed on RX queue %d of VSI %d VF %d\n",
				queue_id, vsi_id, vf->vf_id);
		}
		queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					 queue_id + 1);
	}

	tempmap = vqs->tx_queues;
	queue_id = find_first_bit(&tempmap, I40E_MAX_VSI_QP);
	while (queue_id < I40E_MAX_VSI_QP) {
		if (i40e_ctrl_vsi_tx_queue(vf, vsi_id, queue_id,
					   I40E_QUEUE_CTRL_DISABLECHECK)) {
			dev_err(&pf->pdev->dev,
				"Queue control check failed on TX queue %d of VSI %d VF %d\n",
				queue_id, vsi_id, vf->vf_id);
		}
		queue_id = find_next_bit(&tempmap, I40E_MAX_VSI_QP,
					 queue_id + 1);
	}

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_DISABLE_QUEUES,
				       aq_ret);
}

/**
 * i40e_vc_get_stats_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf to get vsi stats
 **/
static int i40e_vc_get_stats_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_queue_select *vqs =
	    (struct i40e_virtchnl_queue_select *)msg;
	struct i40e_pf *pf = vf->pf;
	struct i40e_eth_stats stats;
	i40e_status aq_ret = 0;
	struct i40e_vsi *vsi;

	memset(&stats, 0, sizeof(struct i40e_eth_stats));

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	if (!i40e_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vqs->vsi_id];
	if (!vsi) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}
	i40e_update_eth_stats(vsi);
	memcpy(&stats, &vsi->eth_stats, sizeof(struct i40e_eth_stats));

error_param:
	/* send the response back to the vf */
	return i40e_vc_send_msg_to_vf(vf, I40E_VIRTCHNL_OP_GET_STATS, aq_ret,
				      (u8 *)&stats, sizeof(stats));
}

/**
 * i40e_vc_add_mac_addr_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * add guest mac address filter
 **/
static int i40e_vc_add_mac_addr_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_ether_addr_list *al =
	    (struct i40e_virtchnl_ether_addr_list *)msg;
	struct i40e_pf *pf = vf->pf;
	struct i40e_vsi *vsi = NULL;
	u16 vsi_id = al->vsi_id;
	i40e_status aq_ret = 0;
	int i;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states) ||
	    !test_bit(I40E_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps) ||
	    !i40e_vc_isvalid_vsi_id(vf, vsi_id)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < al->num_elements; i++) {
		if (is_broadcast_ether_addr(al->list[i].addr) ||
		    is_zero_ether_addr(al->list[i].addr)) {
			dev_err(&pf->pdev->dev, "invalid VF MAC addr %pMAC\n",
				al->list[i].addr);
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}
	}
	vsi = pf->vsi[vsi_id];

	/* add new addresses to the list */
	for (i = 0; i < al->num_elements; i++) {
		struct i40e_mac_filter *f;

		f = i40e_find_mac(vsi, al->list[i].addr, true, false);
		if (f) {
			if (i40e_is_vsi_in_vlan(vsi))
				f = i40e_put_mac_in_vlan(vsi, al->list[i].addr,
							 true, false);
			else
				f = i40e_add_filter(vsi, al->list[i].addr, -1,
						    true, false);
		}

		if (!f) {
			dev_err(&pf->pdev->dev,
				"Unable to add VF MAC filter\n");
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}
	}

	/* program the updated filter list */
	if (i40e_sync_vsi_filters(vsi))
		dev_err(&pf->pdev->dev, "Unable to program VF MAC filters\n");

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS,
				       aq_ret);
}

/**
 * i40e_vc_del_mac_addr_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * remove guest mac address filter
 **/
static int i40e_vc_del_mac_addr_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_ether_addr_list *al =
	    (struct i40e_virtchnl_ether_addr_list *)msg;
	struct i40e_pf *pf = vf->pf;
	struct i40e_vsi *vsi = NULL;
	u16 vsi_id = al->vsi_id;
	i40e_status aq_ret = 0;
	int i;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states) ||
	    !test_bit(I40E_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps) ||
	    !i40e_vc_isvalid_vsi_id(vf, vsi_id)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}
	vsi = pf->vsi[vsi_id];

	/* delete addresses from the list */
	for (i = 0; i < al->num_elements; i++)
		i40e_del_filter(vsi, al->list[i].addr,
				I40E_VLAN_ANY, true, false);

	/* program the updated filter list */
	if (i40e_sync_vsi_filters(vsi))
		dev_err(&pf->pdev->dev, "Unable to program VF MAC filters\n");

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS,
				       aq_ret);
}

/**
 * i40e_vc_add_vlan_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * program guest vlan id
 **/
static int i40e_vc_add_vlan_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_vlan_filter_list *vfl =
	    (struct i40e_virtchnl_vlan_filter_list *)msg;
	struct i40e_pf *pf = vf->pf;
	struct i40e_vsi *vsi = NULL;
	u16 vsi_id = vfl->vsi_id;
	i40e_status aq_ret = 0;
	int i;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states) ||
	    !test_bit(I40E_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps) ||
	    !i40e_vc_isvalid_vsi_id(vf, vsi_id)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < vfl->num_elements; i++) {
		if (vfl->vlan_id[i] > I40E_MAX_VLANID) {
			aq_ret = I40E_ERR_PARAM;
			dev_err(&pf->pdev->dev,
				"invalid VF VLAN id %d\n", vfl->vlan_id[i]);
			goto error_param;
		}
	}
	vsi = pf->vsi[vsi_id];
	if (vsi->info.pvid) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	i40e_vlan_stripping_enable(vsi);
	for (i = 0; i < vfl->num_elements; i++) {
		/* add new VLAN filter */
		int ret = i40e_vsi_add_vlan(vsi, vfl->vlan_id[i]);
		if (ret)
			dev_err(&pf->pdev->dev,
				"Unable to add VF vlan filter %d, error %d\n",
				vfl->vlan_id[i], ret);
	}

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_ADD_VLAN, aq_ret);
}

/**
 * i40e_vc_remove_vlan_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * remove programmed guest vlan id
 **/
static int i40e_vc_remove_vlan_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	struct i40e_virtchnl_vlan_filter_list *vfl =
	    (struct i40e_virtchnl_vlan_filter_list *)msg;
	struct i40e_pf *pf = vf->pf;
	struct i40e_vsi *vsi = NULL;
	u16 vsi_id = vfl->vsi_id;
	i40e_status aq_ret = 0;
	int i;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states) ||
	    !test_bit(I40E_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps) ||
	    !i40e_vc_isvalid_vsi_id(vf, vsi_id)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < vfl->num_elements; i++) {
		if (vfl->vlan_id[i] > I40E_MAX_VLANID) {
			aq_ret = I40E_ERR_PARAM;
			goto error_param;
		}
	}

	vsi = pf->vsi[vsi_id];
	if (vsi->info.pvid) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < vfl->num_elements; i++) {
		int ret = i40e_vsi_kill_vlan(vsi, vfl->vlan_id[i]);
		if (ret)
			dev_err(&pf->pdev->dev,
				"Unable to delete VF vlan filter %d, error %d\n",
				vfl->vlan_id[i], ret);
	}

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_DEL_VLAN, aq_ret);
}

/**
 * i40e_vc_fcoe_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * called from the vf for the fcoe msgs
 **/
static int i40e_vc_fcoe_msg(struct i40e_vf *vf, u8 *msg, u16 msglen)
{
	i40e_status aq_ret = 0;

	if (!test_bit(I40E_VF_STAT_ACTIVE, &vf->vf_states) ||
	    !test_bit(I40E_VF_STAT_FCOEENA, &vf->vf_states)) {
		aq_ret = I40E_ERR_PARAM;
		goto error_param;
	}
	aq_ret = I40E_ERR_NOT_IMPLEMENTED;

error_param:
	/* send the response to the vf */
	return i40e_vc_send_resp_to_vf(vf, I40E_VIRTCHNL_OP_FCOE, aq_ret);
}

/**
 * i40e_vc_validate_vf_msg
 * @vf: pointer to the vf info
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 * @msghndl: msg handle
 *
 * validate msg
 **/
static int i40e_vc_validate_vf_msg(struct i40e_vf *vf, u32 v_opcode,
				   u32 v_retval, u8 *msg, u16 msglen)
{
	bool err_msg_format = false;
	int valid_len;

	/* Check if VF is disabled. */
	if (test_bit(I40E_VF_STAT_DISABLED, &vf->vf_states))
		return I40E_ERR_PARAM;

	/* Validate message length. */
	switch (v_opcode) {
	case I40E_VIRTCHNL_OP_VERSION:
		valid_len = sizeof(struct i40e_virtchnl_version_info);
		break;
	case I40E_VIRTCHNL_OP_RESET_VF:
	case I40E_VIRTCHNL_OP_GET_VF_RESOURCES:
		valid_len = 0;
		break;
	case I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE:
		valid_len = sizeof(struct i40e_virtchnl_txq_info);
		break;
	case I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE:
		valid_len = sizeof(struct i40e_virtchnl_rxq_info);
		break;
	case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES:
		valid_len = sizeof(struct i40e_virtchnl_vsi_queue_config_info);
		if (msglen >= valid_len) {
			struct i40e_virtchnl_vsi_queue_config_info *vqc =
			    (struct i40e_virtchnl_vsi_queue_config_info *)msg;
			valid_len += (vqc->num_queue_pairs *
				      sizeof(struct
					     i40e_virtchnl_queue_pair_info));
			if (vqc->num_queue_pairs == 0)
				err_msg_format = true;
		}
		break;
	case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP:
		valid_len = sizeof(struct i40e_virtchnl_irq_map_info);
		if (msglen >= valid_len) {
			struct i40e_virtchnl_irq_map_info *vimi =
			    (struct i40e_virtchnl_irq_map_info *)msg;
			valid_len += (vimi->num_vectors *
				      sizeof(struct i40e_virtchnl_vector_map));
			if (vimi->num_vectors == 0)
				err_msg_format = true;
		}
		break;
	case I40E_VIRTCHNL_OP_ENABLE_QUEUES:
	case I40E_VIRTCHNL_OP_DISABLE_QUEUES:
		valid_len = sizeof(struct i40e_virtchnl_queue_select);
		break;
	case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS:
	case I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS:
		valid_len = sizeof(struct i40e_virtchnl_ether_addr_list);
		if (msglen >= valid_len) {
			struct i40e_virtchnl_ether_addr_list *veal =
			    (struct i40e_virtchnl_ether_addr_list *)msg;
			valid_len += veal->num_elements *
			    sizeof(struct i40e_virtchnl_ether_addr);
			if (veal->num_elements == 0)
				err_msg_format = true;
		}
		break;
	case I40E_VIRTCHNL_OP_ADD_VLAN:
	case I40E_VIRTCHNL_OP_DEL_VLAN:
		valid_len = sizeof(struct i40e_virtchnl_vlan_filter_list);
		if (msglen >= valid_len) {
			struct i40e_virtchnl_vlan_filter_list *vfl =
			    (struct i40e_virtchnl_vlan_filter_list *)msg;
			valid_len += vfl->num_elements * sizeof(u16);
			if (vfl->num_elements == 0)
				err_msg_format = true;
		}
		break;
	case I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE:
		valid_len = sizeof(struct i40e_virtchnl_promisc_info);
		break;
	case I40E_VIRTCHNL_OP_GET_STATS:
		valid_len = sizeof(struct i40e_virtchnl_queue_select);
		break;
	/* These are always errors coming from the VF. */
	case I40E_VIRTCHNL_OP_EVENT:
	case I40E_VIRTCHNL_OP_UNKNOWN:
	default:
		return -EPERM;
		break;
	}
	/* few more checks */
	if ((valid_len != msglen) || (err_msg_format)) {
		i40e_vc_send_resp_to_vf(vf, v_opcode, I40E_ERR_PARAM);
		return -EINVAL;
	} else {
		return 0;
	}
}

/**
 * i40e_vc_process_vf_msg
 * @pf: pointer to the pf structure
 * @vf_id: source vf id
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 * @msghndl: msg handle
 *
 * called from the common aeq/arq handler to
 * process request from vf
 **/
int i40e_vc_process_vf_msg(struct i40e_pf *pf, u16 vf_id, u32 v_opcode,
			   u32 v_retval, u8 *msg, u16 msglen)
{
	struct i40e_vf *vf = &(pf->vf[vf_id]);
	struct i40e_hw *hw = &pf->hw;
	int ret;

	pf->vf_aq_requests++;
	/* perform basic checks on the msg */
	ret = i40e_vc_validate_vf_msg(vf, v_opcode, v_retval, msg, msglen);

	if (ret) {
		dev_err(&pf->pdev->dev, "invalid message from vf %d\n", vf_id);
		return ret;
	}
	wr32(hw, I40E_VFGEN_RSTAT1(vf_id), I40E_VFR_VFACTIVE);
	switch (v_opcode) {
	case I40E_VIRTCHNL_OP_VERSION:
		ret = i40e_vc_get_version_msg(vf);
		break;
	case I40E_VIRTCHNL_OP_GET_VF_RESOURCES:
		ret = i40e_vc_get_vf_resources_msg(vf);
		break;
	case I40E_VIRTCHNL_OP_RESET_VF:
		ret = i40e_vc_reset_vf_msg(vf);
		break;
	case I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE:
		ret = i40e_vc_config_promiscuous_mode_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES:
		ret = i40e_vc_config_queues_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP:
		ret = i40e_vc_config_irq_map_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_ENABLE_QUEUES:
		ret = i40e_vc_enable_queues_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_DISABLE_QUEUES:
		ret = i40e_vc_disable_queues_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS:
		ret = i40e_vc_add_mac_addr_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS:
		ret = i40e_vc_del_mac_addr_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_ADD_VLAN:
		ret = i40e_vc_add_vlan_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_DEL_VLAN:
		ret = i40e_vc_remove_vlan_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_GET_STATS:
		ret = i40e_vc_get_stats_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_FCOE:
		ret = i40e_vc_fcoe_msg(vf, msg, msglen);
		break;
	case I40E_VIRTCHNL_OP_UNKNOWN:
	default:
		dev_err(&pf->pdev->dev,
			"Unsupported opcode %d from vf %d\n", v_opcode, vf_id);
		ret = i40e_vc_send_resp_to_vf(vf, v_opcode,
					      I40E_ERR_NOT_IMPLEMENTED);
		break;
	}

	return ret;
}

/**
 * i40e_vc_process_vflr_event
 * @pf: pointer to the pf structure
 *
 * called from the vlfr irq handler to
 * free up vf resources and state variables
 **/
int i40e_vc_process_vflr_event(struct i40e_pf *pf)
{
	u32 reg, reg_idx, bit_idx, vf_id;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vf *vf;

	if (!test_bit(__I40E_VFLR_EVENT_PENDING, &pf->state))
		return 0;

	clear_bit(__I40E_VFLR_EVENT_PENDING, &pf->state);
	for (vf_id = 0; vf_id < pf->num_alloc_vfs; vf_id++) {
		reg_idx = (hw->func_caps.vf_base_id + vf_id) / 32;
		bit_idx = (hw->func_caps.vf_base_id + vf_id) % 32;
		/* read GLGEN_VFLRSTAT register to find out the flr vfs */
		vf = &pf->vf[vf_id];
		reg = rd32(hw, I40E_GLGEN_VFLRSTAT(reg_idx));
		if (reg & (1 << bit_idx)) {
			/* clear the bit in GLGEN_VFLRSTAT */
			wr32(hw, I40E_GLGEN_VFLRSTAT(reg_idx), (1 << bit_idx));

			if (i40e_reset_vf(vf, true))
				dev_err(&pf->pdev->dev,
					"Unable to reset the VF %d\n", vf_id);
			/* free up vf resources to destroy vsi state */
			i40e_free_vf_res(vf);

			/* allocate new vf resources with the default state */
			if (i40e_alloc_vf_res(vf))
				dev_err(&pf->pdev->dev,
					"Unable to allocate VF resources %d\n",
					vf_id);

			i40e_enable_vf_mappings(vf);
		}
	}

	/* re-enable vflr interrupt cause */
	reg = rd32(hw, I40E_PFINT_ICR0_ENA);
	reg |= I40E_PFINT_ICR0_ENA_VFLR_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);
	i40e_flush(hw);

	return 0;
}

/**
 * i40e_vc_vf_broadcast
 * @pf: pointer to the pf structure
 * @opcode: operation code
 * @retval: return value
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * send a message to all VFs on a given PF
 **/
static void i40e_vc_vf_broadcast(struct i40e_pf *pf,
				 enum i40e_virtchnl_ops v_opcode,
				 i40e_status v_retval, u8 *msg,
				 u16 msglen)
{
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vf *vf = pf->vf;
	int i;

	for (i = 0; i < pf->num_alloc_vfs; i++) {
		/* Ignore return value on purpose - a given VF may fail, but
		 * we need to keep going and send to all of them
		 */
		i40e_aq_send_msg_to_vf(hw, vf->vf_id, v_opcode, v_retval,
				       msg, msglen, NULL);
		vf++;
	}
}

/**
 * i40e_vc_notify_link_state
 * @pf: pointer to the pf structure
 *
 * send a link status message to all VFs on a given PF
 **/
void i40e_vc_notify_link_state(struct i40e_pf *pf)
{
	struct i40e_virtchnl_pf_event pfe;

	pfe.event = I40E_VIRTCHNL_EVENT_LINK_CHANGE;
	pfe.severity = I40E_PF_EVENT_SEVERITY_INFO;
	pfe.event_data.link_event.link_status =
	    pf->hw.phy.link_info.link_info & I40E_AQ_LINK_UP;
	pfe.event_data.link_event.link_speed = pf->hw.phy.link_info.link_speed;

	i40e_vc_vf_broadcast(pf, I40E_VIRTCHNL_OP_EVENT, I40E_SUCCESS,
			     (u8 *)&pfe, sizeof(struct i40e_virtchnl_pf_event));
}

/**
 * i40e_vc_notify_reset
 * @pf: pointer to the pf structure
 *
 * indicate a pending reset to all VFs on a given PF
 **/
void i40e_vc_notify_reset(struct i40e_pf *pf)
{
	struct i40e_virtchnl_pf_event pfe;

	pfe.event = I40E_VIRTCHNL_EVENT_RESET_IMPENDING;
	pfe.severity = I40E_PF_EVENT_SEVERITY_CERTAIN_DOOM;
	i40e_vc_vf_broadcast(pf, I40E_VIRTCHNL_OP_EVENT, I40E_SUCCESS,
			     (u8 *)&pfe, sizeof(struct i40e_virtchnl_pf_event));
}

/**
 * i40e_vc_notify_vf_reset
 * @vf: pointer to the vf structure
 *
 * indicate a pending reset to the given VF
 **/
void i40e_vc_notify_vf_reset(struct i40e_vf *vf)
{
	struct i40e_virtchnl_pf_event pfe;

	pfe.event = I40E_VIRTCHNL_EVENT_RESET_IMPENDING;
	pfe.severity = I40E_PF_EVENT_SEVERITY_CERTAIN_DOOM;
	i40e_aq_send_msg_to_vf(&vf->pf->hw, vf->vf_id, I40E_VIRTCHNL_OP_EVENT,
			       I40E_SUCCESS, (u8 *)&pfe,
			       sizeof(struct i40e_virtchnl_pf_event), NULL);
}

/**
 * i40e_ndo_set_vf_mac
 * @netdev: network interface device structure
 * @vf_id: vf identifier
 * @mac: mac address
 *
 * program vf mac address
 **/
int i40e_ndo_set_vf_mac(struct net_device *netdev, int vf_id, u8 *mac)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_mac_filter *f;
	struct i40e_vf *vf;
	int ret = 0;

	/* validate the request */
	if (vf_id >= pf->num_alloc_vfs) {
		dev_err(&pf->pdev->dev,
			"Invalid VF Identifier %d\n", vf_id);
		ret = -EINVAL;
		goto error_param;
	}

	vf = &(pf->vf[vf_id]);
	vsi = pf->vsi[vf->lan_vsi_index];
	if (!test_bit(I40E_VF_STAT_INIT, &vf->vf_states)) {
		dev_err(&pf->pdev->dev,
			"Uninitialized VF %d\n", vf_id);
		ret = -EINVAL;
		goto error_param;
	}

	if (!is_valid_ether_addr(mac)) {
		dev_err(&pf->pdev->dev,
			"Invalid VF ethernet address\n");
		ret = -EINVAL;
		goto error_param;
	}

	/* delete the temporary mac address */
	i40e_del_filter(vsi, vf->default_lan_addr.addr, 0, true, false);

	/* add the new mac address */
	f = i40e_add_filter(vsi, mac, 0, true, false);
	if (!f) {
		dev_err(&pf->pdev->dev,
			"Unable to add VF ucast filter\n");
		ret = -ENOMEM;
		goto error_param;
	}

	dev_info(&pf->pdev->dev, "Setting MAC %pM on VF %d\n", mac, vf_id);
	/* program mac filter */
	if (i40e_sync_vsi_filters(vsi)) {
		dev_err(&pf->pdev->dev, "Unable to program ucast filters\n");
		ret = -EIO;
		goto error_param;
	}
	memcpy(vf->default_lan_addr.addr, mac, ETH_ALEN);
	dev_info(&pf->pdev->dev, "Reload the VF driver to make this change effective.\n");
	ret = 0;

error_param:
	return ret;
}

/**
 * i40e_ndo_set_vf_port_vlan
 * @netdev: network interface device structure
 * @vf_id: vf identifier
 * @vlan_id: mac address
 * @qos: priority setting
 *
 * program vf vlan id and/or qos
 **/
int i40e_ndo_set_vf_port_vlan(struct net_device *netdev,
			      int vf_id, u16 vlan_id, u8 qos)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi;
	struct i40e_vf *vf;
	int ret = 0;

	/* validate the request */
	if (vf_id >= pf->num_alloc_vfs) {
		dev_err(&pf->pdev->dev, "Invalid VF Identifier %d\n", vf_id);
		ret = -EINVAL;
		goto error_pvid;
	}

	if ((vlan_id > I40E_MAX_VLANID) || (qos > 7)) {
		dev_err(&pf->pdev->dev, "Invalid VF Parameters\n");
		ret = -EINVAL;
		goto error_pvid;
	}

	vf = &(pf->vf[vf_id]);
	vsi = pf->vsi[vf->lan_vsi_index];
	if (!test_bit(I40E_VF_STAT_INIT, &vf->vf_states)) {
		dev_err(&pf->pdev->dev, "Uninitialized VF %d\n", vf_id);
		ret = -EINVAL;
		goto error_pvid;
	}

	if (vsi->info.pvid) {
		/* kill old VLAN */
		ret = i40e_vsi_kill_vlan(vsi, (le16_to_cpu(vsi->info.pvid) &
					       VLAN_VID_MASK));
		if (ret) {
			dev_info(&vsi->back->pdev->dev,
				 "remove VLAN failed, ret=%d, aq_err=%d\n",
				 ret, pf->hw.aq.asq_last_status);
		}
	}
	if (vlan_id || qos)
		ret = i40e_vsi_add_pvid(vsi,
				vlan_id | (qos << I40E_VLAN_PRIORITY_SHIFT));
	else
		i40e_vlan_stripping_disable(vsi);

	if (vlan_id) {
		dev_info(&pf->pdev->dev, "Setting VLAN %d, QOS 0x%x on VF %d\n",
			 vlan_id, qos, vf_id);

		/* add new VLAN filter */
		ret = i40e_vsi_add_vlan(vsi, vlan_id);
		if (ret) {
			dev_info(&vsi->back->pdev->dev,
				 "add VF VLAN failed, ret=%d aq_err=%d\n", ret,
				 vsi->back->hw.aq.asq_last_status);
			goto error_pvid;
		}
	}

	if (ret) {
		dev_err(&pf->pdev->dev, "Unable to update VF vsi context\n");
		goto error_pvid;
	}
	ret = 0;

error_pvid:
	return ret;
}

/**
 * i40e_ndo_set_vf_bw
 * @netdev: network interface device structure
 * @vf_id: vf identifier
 * @tx_rate: tx rate
 *
 * configure vf tx rate
 **/
int i40e_ndo_set_vf_bw(struct net_device *netdev, int vf_id, int tx_rate)
{
	return -EOPNOTSUPP;
}

/**
 * i40e_ndo_get_vf_config
 * @netdev: network interface device structure
 * @vf_id: vf identifier
 * @ivi: vf configuration structure
 *
 * return vf configuration
 **/
int i40e_ndo_get_vf_config(struct net_device *netdev,
			   int vf_id, struct ifla_vf_info *ivi)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_mac_filter *f, *ftmp;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_vf *vf;
	int ret = 0;

	/* validate the request */
	if (vf_id >= pf->num_alloc_vfs) {
		dev_err(&pf->pdev->dev, "Invalid VF Identifier %d\n", vf_id);
		ret = -EINVAL;
		goto error_param;
	}

	vf = &(pf->vf[vf_id]);
	/* first vsi is always the LAN vsi */
	vsi = pf->vsi[vf->lan_vsi_index];
	if (!test_bit(I40E_VF_STAT_INIT, &vf->vf_states)) {
		dev_err(&pf->pdev->dev, "Uninitialized VF %d\n", vf_id);
		ret = -EINVAL;
		goto error_param;
	}

	ivi->vf = vf_id;

	/* first entry of the list is the default ethernet address */
	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
		memcpy(&ivi->mac, f->macaddr, I40E_ETH_LENGTH_OF_ADDRESS);
		break;
	}

	ivi->tx_rate = 0;
	ivi->vlan = le16_to_cpu(vsi->info.pvid) & I40E_VLAN_MASK;
	ivi->qos = (le16_to_cpu(vsi->info.pvid) & I40E_PRIORITY_MASK) >>
		   I40E_VLAN_PRIORITY_SHIFT;
	ret = 0;

error_param:
	return ret;
}
