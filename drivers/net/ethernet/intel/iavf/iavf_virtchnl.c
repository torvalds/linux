// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include "iavf.h"
#include "iavf_prototype.h"
#include "iavf_client.h"

/* busy wait delay in msec */
#define IAVF_BUSY_WAIT_DELAY 10
#define IAVF_BUSY_WAIT_COUNT 50

/**
 * iavf_send_pf_msg
 * @adapter: adapter structure
 * @op: virtual channel opcode
 * @msg: pointer to message buffer
 * @len: message length
 *
 * Send message to PF and print status if failure.
 **/
static int iavf_send_pf_msg(struct iavf_adapter *adapter,
			    enum virtchnl_ops op, u8 *msg, u16 len)
{
	struct iavf_hw *hw = &adapter->hw;
	enum iavf_status err;

	if (adapter->flags & IAVF_FLAG_PF_COMMS_FAILED)
		return 0; /* nothing to see here, move along */

	err = iavf_aq_send_msg_to_pf(hw, op, 0, msg, len, NULL);
	if (err)
		dev_dbg(&adapter->pdev->dev, "Unable to send opcode %d to PF, err %s, aq_err %s\n",
			op, iavf_stat_str(hw, err),
			iavf_aq_str(hw, hw->aq.asq_last_status));
	return err;
}

/**
 * iavf_send_api_ver
 * @adapter: adapter structure
 *
 * Send API version admin queue message to the PF. The reply is not checked
 * in this function. Returns 0 if the message was successfully
 * sent, or one of the IAVF_ADMIN_QUEUE_ERROR_ statuses if not.
 **/
int iavf_send_api_ver(struct iavf_adapter *adapter)
{
	struct virtchnl_version_info vvi;

	vvi.major = VIRTCHNL_VERSION_MAJOR;
	vvi.minor = VIRTCHNL_VERSION_MINOR;

	return iavf_send_pf_msg(adapter, VIRTCHNL_OP_VERSION, (u8 *)&vvi,
				sizeof(vvi));
}

/**
 * iavf_verify_api_ver
 * @adapter: adapter structure
 *
 * Compare API versions with the PF. Must be called after admin queue is
 * initialized. Returns 0 if API versions match, -EIO if they do not,
 * IAVF_ERR_ADMIN_QUEUE_NO_WORK if the admin queue is empty, and any errors
 * from the firmware are propagated.
 **/
int iavf_verify_api_ver(struct iavf_adapter *adapter)
{
	struct virtchnl_version_info *pf_vvi;
	struct iavf_hw *hw = &adapter->hw;
	struct iavf_arq_event_info event;
	enum virtchnl_ops op;
	enum iavf_status err;

	event.buf_len = IAVF_MAX_AQ_BUF_SIZE;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf) {
		err = -ENOMEM;
		goto out;
	}

	while (1) {
		err = iavf_clean_arq_element(hw, &event, NULL);
		/* When the AQ is empty, iavf_clean_arq_element will return
		 * nonzero and this loop will terminate.
		 */
		if (err)
			goto out_alloc;
		op =
		    (enum virtchnl_ops)le32_to_cpu(event.desc.cookie_high);
		if (op == VIRTCHNL_OP_VERSION)
			break;
	}


	err = (enum iavf_status)le32_to_cpu(event.desc.cookie_low);
	if (err)
		goto out_alloc;

	if (op != VIRTCHNL_OP_VERSION) {
		dev_info(&adapter->pdev->dev, "Invalid reply type %d from PF\n",
			op);
		err = -EIO;
		goto out_alloc;
	}

	pf_vvi = (struct virtchnl_version_info *)event.msg_buf;
	adapter->pf_version = *pf_vvi;

	if ((pf_vvi->major > VIRTCHNL_VERSION_MAJOR) ||
	    ((pf_vvi->major == VIRTCHNL_VERSION_MAJOR) &&
	     (pf_vvi->minor > VIRTCHNL_VERSION_MINOR)))
		err = -EIO;

out_alloc:
	kfree(event.msg_buf);
out:
	return err;
}

/**
 * iavf_send_vf_config_msg
 * @adapter: adapter structure
 *
 * Send VF configuration request admin queue message to the PF. The reply
 * is not checked in this function. Returns 0 if the message was
 * successfully sent, or one of the IAVF_ADMIN_QUEUE_ERROR_ statuses if not.
 **/
int iavf_send_vf_config_msg(struct iavf_adapter *adapter)
{
	u32 caps;

	caps = VIRTCHNL_VF_OFFLOAD_L2 |
	       VIRTCHNL_VF_OFFLOAD_RSS_PF |
	       VIRTCHNL_VF_OFFLOAD_RSS_AQ |
	       VIRTCHNL_VF_OFFLOAD_RSS_REG |
	       VIRTCHNL_VF_OFFLOAD_VLAN |
	       VIRTCHNL_VF_OFFLOAD_WB_ON_ITR |
	       VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2 |
	       VIRTCHNL_VF_OFFLOAD_ENCAP |
	       VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM |
	       VIRTCHNL_VF_OFFLOAD_REQ_QUEUES |
	       VIRTCHNL_VF_OFFLOAD_ADQ |
	       VIRTCHNL_VF_OFFLOAD_USO |
	       VIRTCHNL_VF_OFFLOAD_FDIR_PF |
	       VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF |
	       VIRTCHNL_VF_CAP_ADV_LINK_SPEED;

	adapter->current_op = VIRTCHNL_OP_GET_VF_RESOURCES;
	adapter->aq_required &= ~IAVF_FLAG_AQ_GET_CONFIG;
	if (PF_IS_V11(adapter))
		return iavf_send_pf_msg(adapter, VIRTCHNL_OP_GET_VF_RESOURCES,
					(u8 *)&caps, sizeof(caps));
	else
		return iavf_send_pf_msg(adapter, VIRTCHNL_OP_GET_VF_RESOURCES,
					NULL, 0);
}

/**
 * iavf_validate_num_queues
 * @adapter: adapter structure
 *
 * Validate that the number of queues the PF has sent in
 * VIRTCHNL_OP_GET_VF_RESOURCES is not larger than the VF can handle.
 **/
static void iavf_validate_num_queues(struct iavf_adapter *adapter)
{
	if (adapter->vf_res->num_queue_pairs > IAVF_MAX_REQ_QUEUES) {
		struct virtchnl_vsi_resource *vsi_res;
		int i;

		dev_info(&adapter->pdev->dev, "Received %d queues, but can only have a max of %d\n",
			 adapter->vf_res->num_queue_pairs,
			 IAVF_MAX_REQ_QUEUES);
		dev_info(&adapter->pdev->dev, "Fixing by reducing queues to %d\n",
			 IAVF_MAX_REQ_QUEUES);
		adapter->vf_res->num_queue_pairs = IAVF_MAX_REQ_QUEUES;
		for (i = 0; i < adapter->vf_res->num_vsis; i++) {
			vsi_res = &adapter->vf_res->vsi_res[i];
			vsi_res->num_queue_pairs = IAVF_MAX_REQ_QUEUES;
		}
	}
}

/**
 * iavf_get_vf_config
 * @adapter: private adapter structure
 *
 * Get VF configuration from PF and populate hw structure. Must be called after
 * admin queue is initialized. Busy waits until response is received from PF,
 * with maximum timeout. Response from PF is returned in the buffer for further
 * processing by the caller.
 **/
int iavf_get_vf_config(struct iavf_adapter *adapter)
{
	struct iavf_hw *hw = &adapter->hw;
	struct iavf_arq_event_info event;
	enum virtchnl_ops op;
	enum iavf_status err;
	u16 len;

	len =  sizeof(struct virtchnl_vf_resource) +
		IAVF_MAX_VF_VSI * sizeof(struct virtchnl_vsi_resource);
	event.buf_len = len;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf) {
		err = -ENOMEM;
		goto out;
	}

	while (1) {
		/* When the AQ is empty, iavf_clean_arq_element will return
		 * nonzero and this loop will terminate.
		 */
		err = iavf_clean_arq_element(hw, &event, NULL);
		if (err)
			goto out_alloc;
		op =
		    (enum virtchnl_ops)le32_to_cpu(event.desc.cookie_high);
		if (op == VIRTCHNL_OP_GET_VF_RESOURCES)
			break;
	}

	err = (enum iavf_status)le32_to_cpu(event.desc.cookie_low);
	memcpy(adapter->vf_res, event.msg_buf, min(event.msg_len, len));

	/* some PFs send more queues than we should have so validate that
	 * we aren't getting too many queues
	 */
	if (!err)
		iavf_validate_num_queues(adapter);
	iavf_vf_parse_hw_config(hw, adapter->vf_res);
out_alloc:
	kfree(event.msg_buf);
out:
	return err;
}

/**
 * iavf_configure_queues
 * @adapter: adapter structure
 *
 * Request that the PF set up our (previously allocated) queues.
 **/
void iavf_configure_queues(struct iavf_adapter *adapter)
{
	struct virtchnl_vsi_queue_config_info *vqci;
	struct virtchnl_queue_pair_info *vqpi;
	int pairs = adapter->num_active_queues;
	int i, max_frame = IAVF_MAX_RXBUFFER;
	size_t len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot configure queues, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_CONFIG_VSI_QUEUES;
	len = struct_size(vqci, qpair, pairs);
	vqci = kzalloc(len, GFP_KERNEL);
	if (!vqci)
		return;

	/* Limit maximum frame size when jumbo frames is not enabled */
	if (!(adapter->flags & IAVF_FLAG_LEGACY_RX) &&
	    (adapter->netdev->mtu <= ETH_DATA_LEN))
		max_frame = IAVF_RXBUFFER_1536 - NET_IP_ALIGN;

	vqci->vsi_id = adapter->vsi_res->vsi_id;
	vqci->num_queue_pairs = pairs;
	vqpi = vqci->qpair;
	/* Size check is not needed here - HW max is 16 queue pairs, and we
	 * can fit info for 31 of them into the AQ buffer before it overflows.
	 */
	for (i = 0; i < pairs; i++) {
		vqpi->txq.vsi_id = vqci->vsi_id;
		vqpi->txq.queue_id = i;
		vqpi->txq.ring_len = adapter->tx_rings[i].count;
		vqpi->txq.dma_ring_addr = adapter->tx_rings[i].dma;
		vqpi->rxq.vsi_id = vqci->vsi_id;
		vqpi->rxq.queue_id = i;
		vqpi->rxq.ring_len = adapter->rx_rings[i].count;
		vqpi->rxq.dma_ring_addr = adapter->rx_rings[i].dma;
		vqpi->rxq.max_pkt_size = max_frame;
		vqpi->rxq.databuffer_size =
			ALIGN(adapter->rx_rings[i].rx_buf_len,
			      BIT_ULL(IAVF_RXQ_CTX_DBUFF_SHIFT));
		vqpi++;
	}

	adapter->aq_required &= ~IAVF_FLAG_AQ_CONFIGURE_QUEUES;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
			 (u8 *)vqci, len);
	kfree(vqci);
}

/**
 * iavf_enable_queues
 * @adapter: adapter structure
 *
 * Request that the PF enable all of our queues.
 **/
void iavf_enable_queues(struct iavf_adapter *adapter)
{
	struct virtchnl_queue_select vqs;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot enable queues, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ENABLE_QUEUES;
	vqs.vsi_id = adapter->vsi_res->vsi_id;
	vqs.tx_queues = BIT(adapter->num_active_queues) - 1;
	vqs.rx_queues = vqs.tx_queues;
	adapter->aq_required &= ~IAVF_FLAG_AQ_ENABLE_QUEUES;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ENABLE_QUEUES,
			 (u8 *)&vqs, sizeof(vqs));
}

/**
 * iavf_disable_queues
 * @adapter: adapter structure
 *
 * Request that the PF disable all of our queues.
 **/
void iavf_disable_queues(struct iavf_adapter *adapter)
{
	struct virtchnl_queue_select vqs;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot disable queues, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DISABLE_QUEUES;
	vqs.vsi_id = adapter->vsi_res->vsi_id;
	vqs.tx_queues = BIT(adapter->num_active_queues) - 1;
	vqs.rx_queues = vqs.tx_queues;
	adapter->aq_required &= ~IAVF_FLAG_AQ_DISABLE_QUEUES;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DISABLE_QUEUES,
			 (u8 *)&vqs, sizeof(vqs));
}

/**
 * iavf_map_queues
 * @adapter: adapter structure
 *
 * Request that the PF map queues to interrupt vectors. Misc causes, including
 * admin queue, are always mapped to vector 0.
 **/
void iavf_map_queues(struct iavf_adapter *adapter)
{
	struct virtchnl_irq_map_info *vimi;
	struct virtchnl_vector_map *vecmap;
	struct iavf_q_vector *q_vector;
	int v_idx, q_vectors;
	size_t len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot map queues to vectors, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_CONFIG_IRQ_MAP;

	q_vectors = adapter->num_msix_vectors - NONQ_VECS;

	len = struct_size(vimi, vecmap, adapter->num_msix_vectors);
	vimi = kzalloc(len, GFP_KERNEL);
	if (!vimi)
		return;

	vimi->num_vectors = adapter->num_msix_vectors;
	/* Queue vectors first */
	for (v_idx = 0; v_idx < q_vectors; v_idx++) {
		q_vector = &adapter->q_vectors[v_idx];
		vecmap = &vimi->vecmap[v_idx];

		vecmap->vsi_id = adapter->vsi_res->vsi_id;
		vecmap->vector_id = v_idx + NONQ_VECS;
		vecmap->txq_map = q_vector->ring_mask;
		vecmap->rxq_map = q_vector->ring_mask;
		vecmap->rxitr_idx = IAVF_RX_ITR;
		vecmap->txitr_idx = IAVF_TX_ITR;
	}
	/* Misc vector last - this is only for AdminQ messages */
	vecmap = &vimi->vecmap[v_idx];
	vecmap->vsi_id = adapter->vsi_res->vsi_id;
	vecmap->vector_id = 0;
	vecmap->txq_map = 0;
	vecmap->rxq_map = 0;

	adapter->aq_required &= ~IAVF_FLAG_AQ_MAP_VECTORS;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_IRQ_MAP,
			 (u8 *)vimi, len);
	kfree(vimi);
}

/**
 * iavf_add_ether_addrs
 * @adapter: adapter structure
 *
 * Request that the PF add one or more addresses to our filters.
 **/
void iavf_add_ether_addrs(struct iavf_adapter *adapter)
{
	struct virtchnl_ether_addr_list *veal;
	struct iavf_mac_filter *f;
	int i = 0, count = 0;
	bool more = false;
	size_t len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot add filters, command %d pending\n",
			adapter->current_op);
		return;
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		if (f->add)
			count++;
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_MAC_FILTER;
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ADD_ETH_ADDR;

	len = struct_size(veal, list, count);
	if (len > IAVF_MAX_AQ_BUF_SIZE) {
		dev_warn(&adapter->pdev->dev, "Too many add MAC changes in one request\n");
		count = (IAVF_MAX_AQ_BUF_SIZE -
			 sizeof(struct virtchnl_ether_addr_list)) /
			sizeof(struct virtchnl_ether_addr);
		len = struct_size(veal, list, count);
		more = true;
	}

	veal = kzalloc(len, GFP_ATOMIC);
	if (!veal) {
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}

	veal->vsi_id = adapter->vsi_res->vsi_id;
	veal->num_elements = count;
	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		if (f->add) {
			ether_addr_copy(veal->list[i].addr, f->macaddr);
			i++;
			f->add = false;
			if (i == count)
				break;
		}
	}
	if (!more)
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_MAC_FILTER;

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_ETH_ADDR, (u8 *)veal, len);
	kfree(veal);
}

/**
 * iavf_del_ether_addrs
 * @adapter: adapter structure
 *
 * Request that the PF remove one or more addresses from our filters.
 **/
void iavf_del_ether_addrs(struct iavf_adapter *adapter)
{
	struct virtchnl_ether_addr_list *veal;
	struct iavf_mac_filter *f, *ftmp;
	int i = 0, count = 0;
	bool more = false;
	size_t len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot remove filters, command %d pending\n",
			adapter->current_op);
		return;
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		if (f->remove)
			count++;
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_MAC_FILTER;
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DEL_ETH_ADDR;

	len = struct_size(veal, list, count);
	if (len > IAVF_MAX_AQ_BUF_SIZE) {
		dev_warn(&adapter->pdev->dev, "Too many delete MAC changes in one request\n");
		count = (IAVF_MAX_AQ_BUF_SIZE -
			 sizeof(struct virtchnl_ether_addr_list)) /
			sizeof(struct virtchnl_ether_addr);
		len = struct_size(veal, list, count);
		more = true;
	}
	veal = kzalloc(len, GFP_ATOMIC);
	if (!veal) {
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}

	veal->vsi_id = adapter->vsi_res->vsi_id;
	veal->num_elements = count;
	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list, list) {
		if (f->remove) {
			ether_addr_copy(veal->list[i].addr, f->macaddr);
			i++;
			list_del(&f->list);
			kfree(f);
			if (i == count)
				break;
		}
	}
	if (!more)
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_MAC_FILTER;

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_ETH_ADDR, (u8 *)veal, len);
	kfree(veal);
}

/**
 * iavf_mac_add_ok
 * @adapter: adapter structure
 *
 * Submit list of filters based on PF response.
 **/
static void iavf_mac_add_ok(struct iavf_adapter *adapter)
{
	struct iavf_mac_filter *f, *ftmp;

	spin_lock_bh(&adapter->mac_vlan_list_lock);
	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list, list) {
		f->is_new_mac = false;
	}
	spin_unlock_bh(&adapter->mac_vlan_list_lock);
}

/**
 * iavf_mac_add_reject
 * @adapter: adapter structure
 *
 * Remove filters from list based on PF response.
 **/
static void iavf_mac_add_reject(struct iavf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct iavf_mac_filter *f, *ftmp;

	spin_lock_bh(&adapter->mac_vlan_list_lock);
	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list, list) {
		if (f->remove && ether_addr_equal(f->macaddr, netdev->dev_addr))
			f->remove = false;

		if (f->is_new_mac) {
			list_del(&f->list);
			kfree(f);
		}
	}
	spin_unlock_bh(&adapter->mac_vlan_list_lock);
}

/**
 * iavf_add_vlans
 * @adapter: adapter structure
 *
 * Request that the PF add one or more VLAN filters to our VSI.
 **/
void iavf_add_vlans(struct iavf_adapter *adapter)
{
	struct virtchnl_vlan_filter_list *vvfl;
	int len, i = 0, count = 0;
	struct iavf_vlan_filter *f;
	bool more = false;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot add VLANs, command %d pending\n",
			adapter->current_op);
		return;
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry(f, &adapter->vlan_filter_list, list) {
		if (f->add)
			count++;
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_VLAN_FILTER;
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ADD_VLAN;

	len = sizeof(struct virtchnl_vlan_filter_list) +
	      (count * sizeof(u16));
	if (len > IAVF_MAX_AQ_BUF_SIZE) {
		dev_warn(&adapter->pdev->dev, "Too many add VLAN changes in one request\n");
		count = (IAVF_MAX_AQ_BUF_SIZE -
			 sizeof(struct virtchnl_vlan_filter_list)) /
			sizeof(u16);
		len = sizeof(struct virtchnl_vlan_filter_list) +
		      (count * sizeof(u16));
		more = true;
	}
	vvfl = kzalloc(len, GFP_ATOMIC);
	if (!vvfl) {
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}

	vvfl->vsi_id = adapter->vsi_res->vsi_id;
	vvfl->num_elements = count;
	list_for_each_entry(f, &adapter->vlan_filter_list, list) {
		if (f->add) {
			vvfl->vlan_id[i] = f->vlan;
			i++;
			f->add = false;
			if (i == count)
				break;
		}
	}
	if (!more)
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_VLAN_FILTER;

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_VLAN, (u8 *)vvfl, len);
	kfree(vvfl);
}

/**
 * iavf_del_vlans
 * @adapter: adapter structure
 *
 * Request that the PF remove one or more VLAN filters from our VSI.
 **/
void iavf_del_vlans(struct iavf_adapter *adapter)
{
	struct virtchnl_vlan_filter_list *vvfl;
	struct iavf_vlan_filter *f, *ftmp;
	int len, i = 0, count = 0;
	bool more = false;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot remove VLANs, command %d pending\n",
			adapter->current_op);
		return;
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry(f, &adapter->vlan_filter_list, list) {
		if (f->remove)
			count++;
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_VLAN_FILTER;
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DEL_VLAN;

	len = sizeof(struct virtchnl_vlan_filter_list) +
	      (count * sizeof(u16));
	if (len > IAVF_MAX_AQ_BUF_SIZE) {
		dev_warn(&adapter->pdev->dev, "Too many delete VLAN changes in one request\n");
		count = (IAVF_MAX_AQ_BUF_SIZE -
			 sizeof(struct virtchnl_vlan_filter_list)) /
			sizeof(u16);
		len = sizeof(struct virtchnl_vlan_filter_list) +
		      (count * sizeof(u16));
		more = true;
	}
	vvfl = kzalloc(len, GFP_ATOMIC);
	if (!vvfl) {
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}

	vvfl->vsi_id = adapter->vsi_res->vsi_id;
	vvfl->num_elements = count;
	list_for_each_entry_safe(f, ftmp, &adapter->vlan_filter_list, list) {
		if (f->remove) {
			vvfl->vlan_id[i] = f->vlan;
			i++;
			list_del(&f->list);
			kfree(f);
			if (i == count)
				break;
		}
	}
	if (!more)
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_VLAN_FILTER;

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_VLAN, (u8 *)vvfl, len);
	kfree(vvfl);
}

/**
 * iavf_set_promiscuous
 * @adapter: adapter structure
 * @flags: bitmask to control unicast/multicast promiscuous.
 *
 * Request that the PF enable promiscuous mode for our VSI.
 **/
void iavf_set_promiscuous(struct iavf_adapter *adapter, int flags)
{
	struct virtchnl_promisc_info vpi;
	int promisc_all;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot set promiscuous mode, command %d pending\n",
			adapter->current_op);
		return;
	}

	promisc_all = FLAG_VF_UNICAST_PROMISC |
		      FLAG_VF_MULTICAST_PROMISC;
	if ((flags & promisc_all) == promisc_all) {
		adapter->flags |= IAVF_FLAG_PROMISC_ON;
		adapter->aq_required &= ~IAVF_FLAG_AQ_REQUEST_PROMISC;
		dev_info(&adapter->pdev->dev, "Entering promiscuous mode\n");
	}

	if (flags & FLAG_VF_MULTICAST_PROMISC) {
		adapter->flags |= IAVF_FLAG_ALLMULTI_ON;
		adapter->aq_required &= ~IAVF_FLAG_AQ_REQUEST_ALLMULTI;
		dev_info(&adapter->pdev->dev, "Entering multicast promiscuous mode\n");
	}

	if (!flags) {
		adapter->flags &= ~(IAVF_FLAG_PROMISC_ON |
				    IAVF_FLAG_ALLMULTI_ON);
		adapter->aq_required &= ~(IAVF_FLAG_AQ_RELEASE_PROMISC |
					  IAVF_FLAG_AQ_RELEASE_ALLMULTI);
		dev_info(&adapter->pdev->dev, "Leaving promiscuous mode\n");
	}

	adapter->current_op = VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE;
	vpi.vsi_id = adapter->vsi_res->vsi_id;
	vpi.flags = flags;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
			 (u8 *)&vpi, sizeof(vpi));
}

/**
 * iavf_request_stats
 * @adapter: adapter structure
 *
 * Request VSI statistics from PF.
 **/
void iavf_request_stats(struct iavf_adapter *adapter)
{
	struct virtchnl_queue_select vqs;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* no error message, this isn't crucial */
		return;
	}

	adapter->aq_required &= ~IAVF_FLAG_AQ_REQUEST_STATS;
	adapter->current_op = VIRTCHNL_OP_GET_STATS;
	vqs.vsi_id = adapter->vsi_res->vsi_id;
	/* queue maps are ignored for this message - only the vsi is used */
	if (iavf_send_pf_msg(adapter, VIRTCHNL_OP_GET_STATS, (u8 *)&vqs,
			     sizeof(vqs)))
		/* if the request failed, don't lock out others */
		adapter->current_op = VIRTCHNL_OP_UNKNOWN;
}

/**
 * iavf_get_hena
 * @adapter: adapter structure
 *
 * Request hash enable capabilities from PF
 **/
void iavf_get_hena(struct iavf_adapter *adapter)
{
	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot get RSS hash capabilities, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_GET_RSS_HENA_CAPS;
	adapter->aq_required &= ~IAVF_FLAG_AQ_GET_HENA;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_GET_RSS_HENA_CAPS, NULL, 0);
}

/**
 * iavf_set_hena
 * @adapter: adapter structure
 *
 * Request the PF to set our RSS hash capabilities
 **/
void iavf_set_hena(struct iavf_adapter *adapter)
{
	struct virtchnl_rss_hena vrh;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot set RSS hash enable, command %d pending\n",
			adapter->current_op);
		return;
	}
	vrh.hena = adapter->hena;
	adapter->current_op = VIRTCHNL_OP_SET_RSS_HENA;
	adapter->aq_required &= ~IAVF_FLAG_AQ_SET_HENA;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_SET_RSS_HENA, (u8 *)&vrh,
			 sizeof(vrh));
}

/**
 * iavf_set_rss_key
 * @adapter: adapter structure
 *
 * Request the PF to set our RSS hash key
 **/
void iavf_set_rss_key(struct iavf_adapter *adapter)
{
	struct virtchnl_rss_key *vrk;
	int len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot set RSS key, command %d pending\n",
			adapter->current_op);
		return;
	}
	len = sizeof(struct virtchnl_rss_key) +
	      (adapter->rss_key_size * sizeof(u8)) - 1;
	vrk = kzalloc(len, GFP_KERNEL);
	if (!vrk)
		return;
	vrk->vsi_id = adapter->vsi.id;
	vrk->key_len = adapter->rss_key_size;
	memcpy(vrk->key, adapter->rss_key, adapter->rss_key_size);

	adapter->current_op = VIRTCHNL_OP_CONFIG_RSS_KEY;
	adapter->aq_required &= ~IAVF_FLAG_AQ_SET_RSS_KEY;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_RSS_KEY, (u8 *)vrk, len);
	kfree(vrk);
}

/**
 * iavf_set_rss_lut
 * @adapter: adapter structure
 *
 * Request the PF to set our RSS lookup table
 **/
void iavf_set_rss_lut(struct iavf_adapter *adapter)
{
	struct virtchnl_rss_lut *vrl;
	int len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot set RSS LUT, command %d pending\n",
			adapter->current_op);
		return;
	}
	len = sizeof(struct virtchnl_rss_lut) +
	      (adapter->rss_lut_size * sizeof(u8)) - 1;
	vrl = kzalloc(len, GFP_KERNEL);
	if (!vrl)
		return;
	vrl->vsi_id = adapter->vsi.id;
	vrl->lut_entries = adapter->rss_lut_size;
	memcpy(vrl->lut, adapter->rss_lut, adapter->rss_lut_size);
	adapter->current_op = VIRTCHNL_OP_CONFIG_RSS_LUT;
	adapter->aq_required &= ~IAVF_FLAG_AQ_SET_RSS_LUT;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_RSS_LUT, (u8 *)vrl, len);
	kfree(vrl);
}

/**
 * iavf_enable_vlan_stripping
 * @adapter: adapter structure
 *
 * Request VLAN header stripping to be enabled
 **/
void iavf_enable_vlan_stripping(struct iavf_adapter *adapter)
{
	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot enable stripping, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ENABLE_VLAN_STRIPPING;
	adapter->aq_required &= ~IAVF_FLAG_AQ_ENABLE_VLAN_STRIPPING;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ENABLE_VLAN_STRIPPING, NULL, 0);
}

/**
 * iavf_disable_vlan_stripping
 * @adapter: adapter structure
 *
 * Request VLAN header stripping to be disabled
 **/
void iavf_disable_vlan_stripping(struct iavf_adapter *adapter)
{
	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot disable stripping, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DISABLE_VLAN_STRIPPING;
	adapter->aq_required &= ~IAVF_FLAG_AQ_DISABLE_VLAN_STRIPPING;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DISABLE_VLAN_STRIPPING, NULL, 0);
}

#define IAVF_MAX_SPEED_STRLEN	13

/**
 * iavf_print_link_message - print link up or down
 * @adapter: adapter structure
 *
 * Log a message telling the world of our wonderous link status
 */
static void iavf_print_link_message(struct iavf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int link_speed_mbps;
	char *speed;

	if (!adapter->link_up) {
		netdev_info(netdev, "NIC Link is Down\n");
		return;
	}

	speed = kzalloc(IAVF_MAX_SPEED_STRLEN, GFP_KERNEL);
	if (!speed)
		return;

	if (ADV_LINK_SUPPORT(adapter)) {
		link_speed_mbps = adapter->link_speed_mbps;
		goto print_link_msg;
	}

	switch (adapter->link_speed) {
	case VIRTCHNL_LINK_SPEED_40GB:
		link_speed_mbps = SPEED_40000;
		break;
	case VIRTCHNL_LINK_SPEED_25GB:
		link_speed_mbps = SPEED_25000;
		break;
	case VIRTCHNL_LINK_SPEED_20GB:
		link_speed_mbps = SPEED_20000;
		break;
	case VIRTCHNL_LINK_SPEED_10GB:
		link_speed_mbps = SPEED_10000;
		break;
	case VIRTCHNL_LINK_SPEED_5GB:
		link_speed_mbps = SPEED_5000;
		break;
	case VIRTCHNL_LINK_SPEED_2_5GB:
		link_speed_mbps = SPEED_2500;
		break;
	case VIRTCHNL_LINK_SPEED_1GB:
		link_speed_mbps = SPEED_1000;
		break;
	case VIRTCHNL_LINK_SPEED_100MB:
		link_speed_mbps = SPEED_100;
		break;
	default:
		link_speed_mbps = SPEED_UNKNOWN;
		break;
	}

print_link_msg:
	if (link_speed_mbps > SPEED_1000) {
		if (link_speed_mbps == SPEED_2500)
			snprintf(speed, IAVF_MAX_SPEED_STRLEN, "2.5 Gbps");
		else
			/* convert to Gbps inline */
			snprintf(speed, IAVF_MAX_SPEED_STRLEN, "%d %s",
				 link_speed_mbps / 1000, "Gbps");
	} else if (link_speed_mbps == SPEED_UNKNOWN) {
		snprintf(speed, IAVF_MAX_SPEED_STRLEN, "%s", "Unknown Mbps");
	} else {
		snprintf(speed, IAVF_MAX_SPEED_STRLEN, "%u %s",
			 link_speed_mbps, "Mbps");
	}

	netdev_info(netdev, "NIC Link is Up Speed is %s Full Duplex\n", speed);
	kfree(speed);
}

/**
 * iavf_get_vpe_link_status
 * @adapter: adapter structure
 * @vpe: virtchnl_pf_event structure
 *
 * Helper function for determining the link status
 **/
static bool
iavf_get_vpe_link_status(struct iavf_adapter *adapter,
			 struct virtchnl_pf_event *vpe)
{
	if (ADV_LINK_SUPPORT(adapter))
		return vpe->event_data.link_event_adv.link_status;
	else
		return vpe->event_data.link_event.link_status;
}

/**
 * iavf_set_adapter_link_speed_from_vpe
 * @adapter: adapter structure for which we are setting the link speed
 * @vpe: virtchnl_pf_event structure that contains the link speed we are setting
 *
 * Helper function for setting iavf_adapter link speed
 **/
static void
iavf_set_adapter_link_speed_from_vpe(struct iavf_adapter *adapter,
				     struct virtchnl_pf_event *vpe)
{
	if (ADV_LINK_SUPPORT(adapter))
		adapter->link_speed_mbps =
			vpe->event_data.link_event_adv.link_speed;
	else
		adapter->link_speed = vpe->event_data.link_event.link_speed;
}

/**
 * iavf_enable_channels
 * @adapter: adapter structure
 *
 * Request that the PF enable channels as specified by
 * the user via tc tool.
 **/
void iavf_enable_channels(struct iavf_adapter *adapter)
{
	struct virtchnl_tc_info *vti = NULL;
	size_t len;
	int i;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot configure mqprio, command %d pending\n",
			adapter->current_op);
		return;
	}

	len = struct_size(vti, list, adapter->num_tc - 1);
	vti = kzalloc(len, GFP_KERNEL);
	if (!vti)
		return;
	vti->num_tc = adapter->num_tc;
	for (i = 0; i < vti->num_tc; i++) {
		vti->list[i].count = adapter->ch_config.ch_info[i].count;
		vti->list[i].offset = adapter->ch_config.ch_info[i].offset;
		vti->list[i].pad = 0;
		vti->list[i].max_tx_rate =
				adapter->ch_config.ch_info[i].max_tx_rate;
	}

	adapter->ch_config.state = __IAVF_TC_RUNNING;
	adapter->flags |= IAVF_FLAG_REINIT_ITR_NEEDED;
	adapter->current_op = VIRTCHNL_OP_ENABLE_CHANNELS;
	adapter->aq_required &= ~IAVF_FLAG_AQ_ENABLE_CHANNELS;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ENABLE_CHANNELS, (u8 *)vti, len);
	kfree(vti);
}

/**
 * iavf_disable_channels
 * @adapter: adapter structure
 *
 * Request that the PF disable channels that are configured
 **/
void iavf_disable_channels(struct iavf_adapter *adapter)
{
	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot configure mqprio, command %d pending\n",
			adapter->current_op);
		return;
	}

	adapter->ch_config.state = __IAVF_TC_INVALID;
	adapter->flags |= IAVF_FLAG_REINIT_ITR_NEEDED;
	adapter->current_op = VIRTCHNL_OP_DISABLE_CHANNELS;
	adapter->aq_required &= ~IAVF_FLAG_AQ_DISABLE_CHANNELS;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DISABLE_CHANNELS, NULL, 0);
}

/**
 * iavf_print_cloud_filter
 * @adapter: adapter structure
 * @f: cloud filter to print
 *
 * Print the cloud filter
 **/
static void iavf_print_cloud_filter(struct iavf_adapter *adapter,
				    struct virtchnl_filter *f)
{
	switch (f->flow_type) {
	case VIRTCHNL_TCP_V4_FLOW:
		dev_info(&adapter->pdev->dev, "dst_mac: %pM src_mac: %pM vlan_id: %hu dst_ip: %pI4 src_ip %pI4 dst_port %hu src_port %hu\n",
			 &f->data.tcp_spec.dst_mac,
			 &f->data.tcp_spec.src_mac,
			 ntohs(f->data.tcp_spec.vlan_id),
			 &f->data.tcp_spec.dst_ip[0],
			 &f->data.tcp_spec.src_ip[0],
			 ntohs(f->data.tcp_spec.dst_port),
			 ntohs(f->data.tcp_spec.src_port));
		break;
	case VIRTCHNL_TCP_V6_FLOW:
		dev_info(&adapter->pdev->dev, "dst_mac: %pM src_mac: %pM vlan_id: %hu dst_ip: %pI6 src_ip %pI6 dst_port %hu src_port %hu\n",
			 &f->data.tcp_spec.dst_mac,
			 &f->data.tcp_spec.src_mac,
			 ntohs(f->data.tcp_spec.vlan_id),
			 &f->data.tcp_spec.dst_ip,
			 &f->data.tcp_spec.src_ip,
			 ntohs(f->data.tcp_spec.dst_port),
			 ntohs(f->data.tcp_spec.src_port));
		break;
	}
}

/**
 * iavf_add_cloud_filter
 * @adapter: adapter structure
 *
 * Request that the PF add cloud filters as specified
 * by the user via tc tool.
 **/
void iavf_add_cloud_filter(struct iavf_adapter *adapter)
{
	struct iavf_cloud_filter *cf;
	struct virtchnl_filter *f;
	int len = 0, count = 0;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot add cloud filter, command %d pending\n",
			adapter->current_op);
		return;
	}
	list_for_each_entry(cf, &adapter->cloud_filter_list, list) {
		if (cf->add) {
			count++;
			break;
		}
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_CLOUD_FILTER;
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ADD_CLOUD_FILTER;

	len = sizeof(struct virtchnl_filter);
	f = kzalloc(len, GFP_KERNEL);
	if (!f)
		return;

	list_for_each_entry(cf, &adapter->cloud_filter_list, list) {
		if (cf->add) {
			memcpy(f, &cf->f, sizeof(struct virtchnl_filter));
			cf->add = false;
			cf->state = __IAVF_CF_ADD_PENDING;
			iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_CLOUD_FILTER,
					 (u8 *)f, len);
		}
	}
	kfree(f);
}

/**
 * iavf_del_cloud_filter
 * @adapter: adapter structure
 *
 * Request that the PF delete cloud filters as specified
 * by the user via tc tool.
 **/
void iavf_del_cloud_filter(struct iavf_adapter *adapter)
{
	struct iavf_cloud_filter *cf, *cftmp;
	struct virtchnl_filter *f;
	int len = 0, count = 0;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot remove cloud filter, command %d pending\n",
			adapter->current_op);
		return;
	}
	list_for_each_entry(cf, &adapter->cloud_filter_list, list) {
		if (cf->del) {
			count++;
			break;
		}
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_CLOUD_FILTER;
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DEL_CLOUD_FILTER;

	len = sizeof(struct virtchnl_filter);
	f = kzalloc(len, GFP_KERNEL);
	if (!f)
		return;

	list_for_each_entry_safe(cf, cftmp, &adapter->cloud_filter_list, list) {
		if (cf->del) {
			memcpy(f, &cf->f, sizeof(struct virtchnl_filter));
			cf->del = false;
			cf->state = __IAVF_CF_DEL_PENDING;
			iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_CLOUD_FILTER,
					 (u8 *)f, len);
		}
	}
	kfree(f);
}

/**
 * iavf_add_fdir_filter
 * @adapter: the VF adapter structure
 *
 * Request that the PF add Flow Director filters as specified
 * by the user via ethtool.
 **/
void iavf_add_fdir_filter(struct iavf_adapter *adapter)
{
	struct iavf_fdir_fltr *fdir;
	struct virtchnl_fdir_add *f;
	bool process_fltr = false;
	int len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot add Flow Director filter, command %d pending\n",
			adapter->current_op);
		return;
	}

	len = sizeof(struct virtchnl_fdir_add);
	f = kzalloc(len, GFP_KERNEL);
	if (!f)
		return;

	spin_lock_bh(&adapter->fdir_fltr_lock);
	list_for_each_entry(fdir, &adapter->fdir_list_head, list) {
		if (fdir->state == IAVF_FDIR_FLTR_ADD_REQUEST) {
			process_fltr = true;
			fdir->state = IAVF_FDIR_FLTR_ADD_PENDING;
			memcpy(f, &fdir->vc_add_msg, len);
			break;
		}
	}
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	if (!process_fltr) {
		/* prevent iavf_add_fdir_filter() from being called when there
		 * are no filters to add
		 */
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_FDIR_FILTER;
		kfree(f);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ADD_FDIR_FILTER;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_FDIR_FILTER, (u8 *)f, len);
	kfree(f);
}

/**
 * iavf_del_fdir_filter
 * @adapter: the VF adapter structure
 *
 * Request that the PF delete Flow Director filters as specified
 * by the user via ethtool.
 **/
void iavf_del_fdir_filter(struct iavf_adapter *adapter)
{
	struct iavf_fdir_fltr *fdir;
	struct virtchnl_fdir_del f;
	bool process_fltr = false;
	int len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot remove Flow Director filter, command %d pending\n",
			adapter->current_op);
		return;
	}

	len = sizeof(struct virtchnl_fdir_del);

	spin_lock_bh(&adapter->fdir_fltr_lock);
	list_for_each_entry(fdir, &adapter->fdir_list_head, list) {
		if (fdir->state == IAVF_FDIR_FLTR_DEL_REQUEST) {
			process_fltr = true;
			memset(&f, 0, len);
			f.vsi_id = fdir->vc_add_msg.vsi_id;
			f.flow_id = fdir->flow_id;
			fdir->state = IAVF_FDIR_FLTR_DEL_PENDING;
			break;
		}
	}
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	if (!process_fltr) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_FDIR_FILTER;
		return;
	}

	adapter->current_op = VIRTCHNL_OP_DEL_FDIR_FILTER;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_FDIR_FILTER, (u8 *)&f, len);
}

/**
 * iavf_add_adv_rss_cfg
 * @adapter: the VF adapter structure
 *
 * Request that the PF add RSS configuration as specified
 * by the user via ethtool.
 **/
void iavf_add_adv_rss_cfg(struct iavf_adapter *adapter)
{
	struct virtchnl_rss_cfg *rss_cfg;
	struct iavf_adv_rss *rss;
	bool process_rss = false;
	int len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot add RSS configuration, command %d pending\n",
			adapter->current_op);
		return;
	}

	len = sizeof(struct virtchnl_rss_cfg);
	rss_cfg = kzalloc(len, GFP_KERNEL);
	if (!rss_cfg)
		return;

	spin_lock_bh(&adapter->adv_rss_lock);
	list_for_each_entry(rss, &adapter->adv_rss_list_head, list) {
		if (rss->state == IAVF_ADV_RSS_ADD_REQUEST) {
			process_rss = true;
			rss->state = IAVF_ADV_RSS_ADD_PENDING;
			memcpy(rss_cfg, &rss->cfg_msg, len);
			iavf_print_adv_rss_cfg(adapter, rss,
					       "Input set change for",
					       "is pending");
			break;
		}
	}
	spin_unlock_bh(&adapter->adv_rss_lock);

	if (process_rss) {
		adapter->current_op = VIRTCHNL_OP_ADD_RSS_CFG;
		iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_RSS_CFG,
				 (u8 *)rss_cfg, len);
	} else {
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_ADV_RSS_CFG;
	}

	kfree(rss_cfg);
}

/**
 * iavf_del_adv_rss_cfg
 * @adapter: the VF adapter structure
 *
 * Request that the PF delete RSS configuration as specified
 * by the user via ethtool.
 **/
void iavf_del_adv_rss_cfg(struct iavf_adapter *adapter)
{
	struct virtchnl_rss_cfg *rss_cfg;
	struct iavf_adv_rss *rss;
	bool process_rss = false;
	int len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot remove RSS configuration, command %d pending\n",
			adapter->current_op);
		return;
	}

	len = sizeof(struct virtchnl_rss_cfg);
	rss_cfg = kzalloc(len, GFP_KERNEL);
	if (!rss_cfg)
		return;

	spin_lock_bh(&adapter->adv_rss_lock);
	list_for_each_entry(rss, &adapter->adv_rss_list_head, list) {
		if (rss->state == IAVF_ADV_RSS_DEL_REQUEST) {
			process_rss = true;
			rss->state = IAVF_ADV_RSS_DEL_PENDING;
			memcpy(rss_cfg, &rss->cfg_msg, len);
			break;
		}
	}
	spin_unlock_bh(&adapter->adv_rss_lock);

	if (process_rss) {
		adapter->current_op = VIRTCHNL_OP_DEL_RSS_CFG;
		iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_RSS_CFG,
				 (u8 *)rss_cfg, len);
	} else {
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_ADV_RSS_CFG;
	}

	kfree(rss_cfg);
}

/**
 * iavf_request_reset
 * @adapter: adapter structure
 *
 * Request that the PF reset this VF. No response is expected.
 **/
void iavf_request_reset(struct iavf_adapter *adapter)
{
	/* Don't check CURRENT_OP - this is always higher priority */
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_RESET_VF, NULL, 0);
	adapter->current_op = VIRTCHNL_OP_UNKNOWN;
}

/**
 * iavf_virtchnl_completion
 * @adapter: adapter structure
 * @v_opcode: opcode sent by PF
 * @v_retval: retval sent by PF
 * @msg: message sent by PF
 * @msglen: message length
 *
 * Asynchronous completion function for admin queue messages. Rather than busy
 * wait, we fire off our requests and assume that no errors will be returned.
 * This function handles the reply messages.
 **/
void iavf_virtchnl_completion(struct iavf_adapter *adapter,
			      enum virtchnl_ops v_opcode,
			      enum iavf_status v_retval, u8 *msg, u16 msglen)
{
	struct net_device *netdev = adapter->netdev;

	if (v_opcode == VIRTCHNL_OP_EVENT) {
		struct virtchnl_pf_event *vpe =
			(struct virtchnl_pf_event *)msg;
		bool link_up = iavf_get_vpe_link_status(adapter, vpe);

		switch (vpe->event) {
		case VIRTCHNL_EVENT_LINK_CHANGE:
			iavf_set_adapter_link_speed_from_vpe(adapter, vpe);

			/* we've already got the right link status, bail */
			if (adapter->link_up == link_up)
				break;

			if (link_up) {
				/* If we get link up message and start queues
				 * before our queues are configured it will
				 * trigger a TX hang. In that case, just ignore
				 * the link status message,we'll get another one
				 * after we enable queues and actually prepared
				 * to send traffic.
				 */
				if (adapter->state != __IAVF_RUNNING)
					break;

				/* For ADq enabled VF, we reconfigure VSIs and
				 * re-allocate queues. Hence wait till all
				 * queues are enabled.
				 */
				if (adapter->flags &
				    IAVF_FLAG_QUEUES_DISABLED)
					break;
			}

			adapter->link_up = link_up;
			if (link_up) {
				netif_tx_start_all_queues(netdev);
				netif_carrier_on(netdev);
			} else {
				netif_tx_stop_all_queues(netdev);
				netif_carrier_off(netdev);
			}
			iavf_print_link_message(adapter);
			break;
		case VIRTCHNL_EVENT_RESET_IMPENDING:
			dev_info(&adapter->pdev->dev, "Reset warning received from the PF\n");
			if (!(adapter->flags & IAVF_FLAG_RESET_PENDING)) {
				adapter->flags |= IAVF_FLAG_RESET_PENDING;
				dev_info(&adapter->pdev->dev, "Scheduling reset task\n");
				queue_work(iavf_wq, &adapter->reset_task);
			}
			break;
		default:
			dev_err(&adapter->pdev->dev, "Unknown event %d from PF\n",
				vpe->event);
			break;
		}
		return;
	}
	if (v_retval) {
		switch (v_opcode) {
		case VIRTCHNL_OP_ADD_VLAN:
			dev_err(&adapter->pdev->dev, "Failed to add VLAN filter, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			break;
		case VIRTCHNL_OP_ADD_ETH_ADDR:
			dev_err(&adapter->pdev->dev, "Failed to add MAC filter, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			iavf_mac_add_reject(adapter);
			/* restore administratively set MAC address */
			ether_addr_copy(adapter->hw.mac.addr, netdev->dev_addr);
			break;
		case VIRTCHNL_OP_DEL_VLAN:
			dev_err(&adapter->pdev->dev, "Failed to delete VLAN filter, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			break;
		case VIRTCHNL_OP_DEL_ETH_ADDR:
			dev_err(&adapter->pdev->dev, "Failed to delete MAC filter, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			break;
		case VIRTCHNL_OP_ENABLE_CHANNELS:
			dev_err(&adapter->pdev->dev, "Failed to configure queue channels, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			adapter->flags &= ~IAVF_FLAG_REINIT_ITR_NEEDED;
			adapter->ch_config.state = __IAVF_TC_INVALID;
			netdev_reset_tc(netdev);
			netif_tx_start_all_queues(netdev);
			break;
		case VIRTCHNL_OP_DISABLE_CHANNELS:
			dev_err(&adapter->pdev->dev, "Failed to disable queue channels, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			adapter->flags &= ~IAVF_FLAG_REINIT_ITR_NEEDED;
			adapter->ch_config.state = __IAVF_TC_RUNNING;
			netif_tx_start_all_queues(netdev);
			break;
		case VIRTCHNL_OP_ADD_CLOUD_FILTER: {
			struct iavf_cloud_filter *cf, *cftmp;

			list_for_each_entry_safe(cf, cftmp,
						 &adapter->cloud_filter_list,
						 list) {
				if (cf->state == __IAVF_CF_ADD_PENDING) {
					cf->state = __IAVF_CF_INVALID;
					dev_info(&adapter->pdev->dev, "Failed to add cloud filter, error %s\n",
						 iavf_stat_str(&adapter->hw,
							       v_retval));
					iavf_print_cloud_filter(adapter,
								&cf->f);
					list_del(&cf->list);
					kfree(cf);
					adapter->num_cloud_filters--;
				}
			}
			}
			break;
		case VIRTCHNL_OP_DEL_CLOUD_FILTER: {
			struct iavf_cloud_filter *cf;

			list_for_each_entry(cf, &adapter->cloud_filter_list,
					    list) {
				if (cf->state == __IAVF_CF_DEL_PENDING) {
					cf->state = __IAVF_CF_ACTIVE;
					dev_info(&adapter->pdev->dev, "Failed to del cloud filter, error %s\n",
						 iavf_stat_str(&adapter->hw,
							       v_retval));
					iavf_print_cloud_filter(adapter,
								&cf->f);
				}
			}
			}
			break;
		case VIRTCHNL_OP_ADD_FDIR_FILTER: {
			struct iavf_fdir_fltr *fdir, *fdir_tmp;

			spin_lock_bh(&adapter->fdir_fltr_lock);
			list_for_each_entry_safe(fdir, fdir_tmp,
						 &adapter->fdir_list_head,
						 list) {
				if (fdir->state == IAVF_FDIR_FLTR_ADD_PENDING) {
					dev_info(&adapter->pdev->dev, "Failed to add Flow Director filter, error %s\n",
						 iavf_stat_str(&adapter->hw,
							       v_retval));
					iavf_print_fdir_fltr(adapter, fdir);
					if (msglen)
						dev_err(&adapter->pdev->dev,
							"%s\n", msg);
					list_del(&fdir->list);
					kfree(fdir);
					adapter->fdir_active_fltr--;
				}
			}
			spin_unlock_bh(&adapter->fdir_fltr_lock);
			}
			break;
		case VIRTCHNL_OP_DEL_FDIR_FILTER: {
			struct iavf_fdir_fltr *fdir;

			spin_lock_bh(&adapter->fdir_fltr_lock);
			list_for_each_entry(fdir, &adapter->fdir_list_head,
					    list) {
				if (fdir->state == IAVF_FDIR_FLTR_DEL_PENDING) {
					fdir->state = IAVF_FDIR_FLTR_ACTIVE;
					dev_info(&adapter->pdev->dev, "Failed to del Flow Director filter, error %s\n",
						 iavf_stat_str(&adapter->hw,
							       v_retval));
					iavf_print_fdir_fltr(adapter, fdir);
				}
			}
			spin_unlock_bh(&adapter->fdir_fltr_lock);
			}
			break;
		case VIRTCHNL_OP_ADD_RSS_CFG: {
			struct iavf_adv_rss *rss, *rss_tmp;

			spin_lock_bh(&adapter->adv_rss_lock);
			list_for_each_entry_safe(rss, rss_tmp,
						 &adapter->adv_rss_list_head,
						 list) {
				if (rss->state == IAVF_ADV_RSS_ADD_PENDING) {
					iavf_print_adv_rss_cfg(adapter, rss,
							       "Failed to change the input set for",
							       NULL);
					list_del(&rss->list);
					kfree(rss);
				}
			}
			spin_unlock_bh(&adapter->adv_rss_lock);
			}
			break;
		case VIRTCHNL_OP_DEL_RSS_CFG: {
			struct iavf_adv_rss *rss;

			spin_lock_bh(&adapter->adv_rss_lock);
			list_for_each_entry(rss, &adapter->adv_rss_list_head,
					    list) {
				if (rss->state == IAVF_ADV_RSS_DEL_PENDING) {
					rss->state = IAVF_ADV_RSS_ACTIVE;
					dev_err(&adapter->pdev->dev, "Failed to delete RSS configuration, error %s\n",
						iavf_stat_str(&adapter->hw,
							      v_retval));
				}
			}
			spin_unlock_bh(&adapter->adv_rss_lock);
			}
			break;
		case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING:
		case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING:
			dev_warn(&adapter->pdev->dev, "Changing VLAN Stripping is not allowed when Port VLAN is configured\n");
			break;
		default:
			dev_err(&adapter->pdev->dev, "PF returned error %d (%s) to our request %d\n",
				v_retval, iavf_stat_str(&adapter->hw, v_retval),
				v_opcode);
		}
	}
	switch (v_opcode) {
	case VIRTCHNL_OP_ADD_ETH_ADDR:
		if (!v_retval)
			iavf_mac_add_ok(adapter);
		if (!ether_addr_equal(netdev->dev_addr, adapter->hw.mac.addr))
			ether_addr_copy(netdev->dev_addr, adapter->hw.mac.addr);
		break;
	case VIRTCHNL_OP_GET_STATS: {
		struct iavf_eth_stats *stats =
			(struct iavf_eth_stats *)msg;
		netdev->stats.rx_packets = stats->rx_unicast +
					   stats->rx_multicast +
					   stats->rx_broadcast;
		netdev->stats.tx_packets = stats->tx_unicast +
					   stats->tx_multicast +
					   stats->tx_broadcast;
		netdev->stats.rx_bytes = stats->rx_bytes;
		netdev->stats.tx_bytes = stats->tx_bytes;
		netdev->stats.tx_errors = stats->tx_errors;
		netdev->stats.rx_dropped = stats->rx_discards;
		netdev->stats.tx_dropped = stats->tx_discards;
		adapter->current_stats = *stats;
		}
		break;
	case VIRTCHNL_OP_GET_VF_RESOURCES: {
		u16 len = sizeof(struct virtchnl_vf_resource) +
			  IAVF_MAX_VF_VSI *
			  sizeof(struct virtchnl_vsi_resource);
		memcpy(adapter->vf_res, msg, min(msglen, len));
		iavf_validate_num_queues(adapter);
		iavf_vf_parse_hw_config(&adapter->hw, adapter->vf_res);
		if (is_zero_ether_addr(adapter->hw.mac.addr)) {
			/* restore current mac address */
			ether_addr_copy(adapter->hw.mac.addr, netdev->dev_addr);
		} else {
			/* refresh current mac address if changed */
			ether_addr_copy(netdev->dev_addr, adapter->hw.mac.addr);
			ether_addr_copy(netdev->perm_addr,
					adapter->hw.mac.addr);
		}
		spin_lock_bh(&adapter->mac_vlan_list_lock);
		iavf_add_filter(adapter, adapter->hw.mac.addr);
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		iavf_process_config(adapter);
		}
		break;
	case VIRTCHNL_OP_ENABLE_QUEUES:
		/* enable transmits */
		iavf_irq_enable(adapter, true);
		adapter->flags &= ~IAVF_FLAG_QUEUES_DISABLED;
		break;
	case VIRTCHNL_OP_DISABLE_QUEUES:
		iavf_free_all_tx_resources(adapter);
		iavf_free_all_rx_resources(adapter);
		if (adapter->state == __IAVF_DOWN_PENDING) {
			adapter->state = __IAVF_DOWN;
			wake_up(&adapter->down_waitqueue);
		}
		break;
	case VIRTCHNL_OP_VERSION:
	case VIRTCHNL_OP_CONFIG_IRQ_MAP:
		/* Don't display an error if we get these out of sequence.
		 * If the firmware needed to get kicked, we'll get these and
		 * it's no problem.
		 */
		if (v_opcode != adapter->current_op)
			return;
		break;
	case VIRTCHNL_OP_IWARP:
		/* Gobble zero-length replies from the PF. They indicate that
		 * a previous message was received OK, and the client doesn't
		 * care about that.
		 */
		if (msglen && CLIENT_ENABLED(adapter))
			iavf_notify_client_message(&adapter->vsi, msg, msglen);
		break;

	case VIRTCHNL_OP_CONFIG_IWARP_IRQ_MAP:
		adapter->client_pending &=
				~(BIT(VIRTCHNL_OP_CONFIG_IWARP_IRQ_MAP));
		break;
	case VIRTCHNL_OP_GET_RSS_HENA_CAPS: {
		struct virtchnl_rss_hena *vrh = (struct virtchnl_rss_hena *)msg;

		if (msglen == sizeof(*vrh))
			adapter->hena = vrh->hena;
		else
			dev_warn(&adapter->pdev->dev,
				 "Invalid message %d from PF\n", v_opcode);
		}
		break;
	case VIRTCHNL_OP_REQUEST_QUEUES: {
		struct virtchnl_vf_res_request *vfres =
			(struct virtchnl_vf_res_request *)msg;

		if (vfres->num_queue_pairs != adapter->num_req_queues) {
			dev_info(&adapter->pdev->dev,
				 "Requested %d queues, PF can support %d\n",
				 adapter->num_req_queues,
				 vfres->num_queue_pairs);
			adapter->num_req_queues = 0;
			adapter->flags &= ~IAVF_FLAG_REINIT_ITR_NEEDED;
		}
		}
		break;
	case VIRTCHNL_OP_ADD_CLOUD_FILTER: {
		struct iavf_cloud_filter *cf;

		list_for_each_entry(cf, &adapter->cloud_filter_list, list) {
			if (cf->state == __IAVF_CF_ADD_PENDING)
				cf->state = __IAVF_CF_ACTIVE;
		}
		}
		break;
	case VIRTCHNL_OP_DEL_CLOUD_FILTER: {
		struct iavf_cloud_filter *cf, *cftmp;

		list_for_each_entry_safe(cf, cftmp, &adapter->cloud_filter_list,
					 list) {
			if (cf->state == __IAVF_CF_DEL_PENDING) {
				cf->state = __IAVF_CF_INVALID;
				list_del(&cf->list);
				kfree(cf);
				adapter->num_cloud_filters--;
			}
		}
		}
		break;
	case VIRTCHNL_OP_ADD_FDIR_FILTER: {
		struct virtchnl_fdir_add *add_fltr = (struct virtchnl_fdir_add *)msg;
		struct iavf_fdir_fltr *fdir, *fdir_tmp;

		spin_lock_bh(&adapter->fdir_fltr_lock);
		list_for_each_entry_safe(fdir, fdir_tmp,
					 &adapter->fdir_list_head,
					 list) {
			if (fdir->state == IAVF_FDIR_FLTR_ADD_PENDING) {
				if (add_fltr->status == VIRTCHNL_FDIR_SUCCESS) {
					dev_info(&adapter->pdev->dev, "Flow Director filter with location %u is added\n",
						 fdir->loc);
					fdir->state = IAVF_FDIR_FLTR_ACTIVE;
					fdir->flow_id = add_fltr->flow_id;
				} else {
					dev_info(&adapter->pdev->dev, "Failed to add Flow Director filter with status: %d\n",
						 add_fltr->status);
					iavf_print_fdir_fltr(adapter, fdir);
					list_del(&fdir->list);
					kfree(fdir);
					adapter->fdir_active_fltr--;
				}
			}
		}
		spin_unlock_bh(&adapter->fdir_fltr_lock);
		}
		break;
	case VIRTCHNL_OP_DEL_FDIR_FILTER: {
		struct virtchnl_fdir_del *del_fltr = (struct virtchnl_fdir_del *)msg;
		struct iavf_fdir_fltr *fdir, *fdir_tmp;

		spin_lock_bh(&adapter->fdir_fltr_lock);
		list_for_each_entry_safe(fdir, fdir_tmp, &adapter->fdir_list_head,
					 list) {
			if (fdir->state == IAVF_FDIR_FLTR_DEL_PENDING) {
				if (del_fltr->status == VIRTCHNL_FDIR_SUCCESS) {
					dev_info(&adapter->pdev->dev, "Flow Director filter with location %u is deleted\n",
						 fdir->loc);
					list_del(&fdir->list);
					kfree(fdir);
					adapter->fdir_active_fltr--;
				} else {
					fdir->state = IAVF_FDIR_FLTR_ACTIVE;
					dev_info(&adapter->pdev->dev, "Failed to delete Flow Director filter with status: %d\n",
						 del_fltr->status);
					iavf_print_fdir_fltr(adapter, fdir);
				}
			}
		}
		spin_unlock_bh(&adapter->fdir_fltr_lock);
		}
		break;
	case VIRTCHNL_OP_ADD_RSS_CFG: {
		struct iavf_adv_rss *rss;

		spin_lock_bh(&adapter->adv_rss_lock);
		list_for_each_entry(rss, &adapter->adv_rss_list_head, list) {
			if (rss->state == IAVF_ADV_RSS_ADD_PENDING) {
				iavf_print_adv_rss_cfg(adapter, rss,
						       "Input set change for",
						       "successful");
				rss->state = IAVF_ADV_RSS_ACTIVE;
			}
		}
		spin_unlock_bh(&adapter->adv_rss_lock);
		}
		break;
	case VIRTCHNL_OP_DEL_RSS_CFG: {
		struct iavf_adv_rss *rss, *rss_tmp;

		spin_lock_bh(&adapter->adv_rss_lock);
		list_for_each_entry_safe(rss, rss_tmp,
					 &adapter->adv_rss_list_head, list) {
			if (rss->state == IAVF_ADV_RSS_DEL_PENDING) {
				list_del(&rss->list);
				kfree(rss);
			}
		}
		spin_unlock_bh(&adapter->adv_rss_lock);
		}
		break;
	default:
		if (adapter->current_op && (v_opcode != adapter->current_op))
			dev_warn(&adapter->pdev->dev, "Expected response %d from PF, received %d\n",
				 adapter->current_op, v_opcode);
		break;
	} /* switch v_opcode */
	adapter->current_op = VIRTCHNL_OP_UNKNOWN;
}
