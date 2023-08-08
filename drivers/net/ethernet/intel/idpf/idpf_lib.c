// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"

static const struct net_device_ops idpf_netdev_ops_splitq;
static const struct net_device_ops idpf_netdev_ops_singleq;

const char * const idpf_vport_vc_state_str[] = {
	IDPF_FOREACH_VPORT_VC_STATE(IDPF_GEN_STRING)
};

/**
 * idpf_init_vector_stack - Fill the MSIX vector stack with vector index
 * @adapter: private data struct
 *
 * Return 0 on success, error on failure
 */
static int idpf_init_vector_stack(struct idpf_adapter *adapter)
{
	struct idpf_vector_lifo *stack;
	u16 min_vec;
	u32 i;

	mutex_lock(&adapter->vector_lock);
	min_vec = adapter->num_msix_entries - adapter->num_avail_msix;
	stack = &adapter->vector_stack;
	stack->size = adapter->num_msix_entries;
	/* set the base and top to point at start of the 'free pool' to
	 * distribute the unused vectors on-demand basis
	 */
	stack->base = min_vec;
	stack->top = min_vec;

	stack->vec_idx = kcalloc(stack->size, sizeof(u16), GFP_KERNEL);
	if (!stack->vec_idx) {
		mutex_unlock(&adapter->vector_lock);

		return -ENOMEM;
	}

	for (i = 0; i < stack->size; i++)
		stack->vec_idx[i] = i;

	mutex_unlock(&adapter->vector_lock);

	return 0;
}

/**
 * idpf_deinit_vector_stack - zero out the MSIX vector stack
 * @adapter: private data struct
 */
static void idpf_deinit_vector_stack(struct idpf_adapter *adapter)
{
	struct idpf_vector_lifo *stack;

	mutex_lock(&adapter->vector_lock);
	stack = &adapter->vector_stack;
	kfree(stack->vec_idx);
	stack->vec_idx = NULL;
	mutex_unlock(&adapter->vector_lock);
}

/**
 * idpf_mb_intr_rel_irq - Free the IRQ association with the OS
 * @adapter: adapter structure
 *
 * This will also disable interrupt mode and queue up mailbox task. Mailbox
 * task will reschedule itself if not in interrupt mode.
 */
static void idpf_mb_intr_rel_irq(struct idpf_adapter *adapter)
{
	clear_bit(IDPF_MB_INTR_MODE, adapter->flags);
	free_irq(adapter->msix_entries[0].vector, adapter);
	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);
}

/**
 * idpf_intr_rel - Release interrupt capabilities and free memory
 * @adapter: adapter to disable interrupts on
 */
void idpf_intr_rel(struct idpf_adapter *adapter)
{
	int err;

	if (!adapter->msix_entries)
		return;

	idpf_mb_intr_rel_irq(adapter);
	pci_free_irq_vectors(adapter->pdev);

	err = idpf_send_dealloc_vectors_msg(adapter);
	if (err)
		dev_err(&adapter->pdev->dev,
			"Failed to deallocate vectors: %d\n", err);

	idpf_deinit_vector_stack(adapter);
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
}

/**
 * idpf_mb_intr_clean - Interrupt handler for the mailbox
 * @irq: interrupt number
 * @data: pointer to the adapter structure
 */
static irqreturn_t idpf_mb_intr_clean(int __always_unused irq, void *data)
{
	struct idpf_adapter *adapter = (struct idpf_adapter *)data;

	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);

	return IRQ_HANDLED;
}

/**
 * idpf_mb_irq_enable - Enable MSIX interrupt for the mailbox
 * @adapter: adapter to get the hardware address for register write
 */
static void idpf_mb_irq_enable(struct idpf_adapter *adapter)
{
	struct idpf_intr_reg *intr = &adapter->mb_vector.intr_reg;
	u32 val;

	val = intr->dyn_ctl_intena_m | intr->dyn_ctl_itridx_m;
	writel(val, intr->dyn_ctl);
	writel(intr->icr_ena_ctlq_m, intr->icr_ena);
}

/**
 * idpf_mb_intr_req_irq - Request irq for the mailbox interrupt
 * @adapter: adapter structure to pass to the mailbox irq handler
 */
static int idpf_mb_intr_req_irq(struct idpf_adapter *adapter)
{
	struct idpf_q_vector *mb_vector = &adapter->mb_vector;
	int irq_num, mb_vidx = 0, err;

	irq_num = adapter->msix_entries[mb_vidx].vector;
	mb_vector->name = kasprintf(GFP_KERNEL, "%s-%s-%d",
				    dev_driver_string(&adapter->pdev->dev),
				    "Mailbox", mb_vidx);
	err = request_irq(irq_num, adapter->irq_mb_handler, 0,
			  mb_vector->name, adapter);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"IRQ request for mailbox failed, error: %d\n", err);

		return err;
	}

	set_bit(IDPF_MB_INTR_MODE, adapter->flags);

	return 0;
}

/**
 * idpf_set_mb_vec_id - Set vector index for mailbox
 * @adapter: adapter structure to access the vector chunks
 *
 * The first vector id in the requested vector chunks from the CP is for
 * the mailbox
 */
static void idpf_set_mb_vec_id(struct idpf_adapter *adapter)
{
	if (adapter->req_vec_chunks)
		adapter->mb_vector.v_idx =
			le16_to_cpu(adapter->caps.mailbox_vector_id);
	else
		adapter->mb_vector.v_idx = 0;
}

/**
 * idpf_mb_intr_init - Initialize the mailbox interrupt
 * @adapter: adapter structure to store the mailbox vector
 */
static int idpf_mb_intr_init(struct idpf_adapter *adapter)
{
	adapter->dev_ops.reg_ops.mb_intr_reg_init(adapter);
	adapter->irq_mb_handler = idpf_mb_intr_clean;

	return idpf_mb_intr_req_irq(adapter);
}

/**
 * idpf_intr_req - Request interrupt capabilities
 * @adapter: adapter to enable interrupts on
 *
 * Returns 0 on success, negative on failure
 */
int idpf_intr_req(struct idpf_adapter *adapter)
{
	u16 default_vports = idpf_get_default_vports(adapter);
	int num_q_vecs, total_vecs, num_vec_ids;
	int min_vectors, v_actual, err;
	unsigned int vector;
	u16 *vecids;

	total_vecs = idpf_get_reserved_vecs(adapter);
	num_q_vecs = total_vecs - IDPF_MBX_Q_VEC;

	err = idpf_send_alloc_vectors_msg(adapter, num_q_vecs);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to allocate %d vectors: %d\n", num_q_vecs, err);

		return -EAGAIN;
	}

	min_vectors = IDPF_MBX_Q_VEC + IDPF_MIN_Q_VEC * default_vports;
	v_actual = pci_alloc_irq_vectors(adapter->pdev, min_vectors,
					 total_vecs, PCI_IRQ_MSIX);
	if (v_actual < min_vectors) {
		dev_err(&adapter->pdev->dev, "Failed to allocate MSIX vectors: %d\n",
			v_actual);
		err = -EAGAIN;
		goto send_dealloc_vecs;
	}

	adapter->msix_entries = kcalloc(v_actual, sizeof(struct msix_entry),
					GFP_KERNEL);

	if (!adapter->msix_entries) {
		err = -ENOMEM;
		goto free_irq;
	}

	idpf_set_mb_vec_id(adapter);

	vecids = kcalloc(total_vecs, sizeof(u16), GFP_KERNEL);
	if (!vecids) {
		err = -ENOMEM;
		goto free_msix;
	}

	if (adapter->req_vec_chunks) {
		struct virtchnl2_vector_chunks *vchunks;
		struct virtchnl2_alloc_vectors *ac;

		ac = adapter->req_vec_chunks;
		vchunks = &ac->vchunks;

		num_vec_ids = idpf_get_vec_ids(adapter, vecids, total_vecs,
					       vchunks);
		if (num_vec_ids < v_actual) {
			err = -EINVAL;
			goto free_vecids;
		}
	} else {
		int i;

		for (i = 0; i < v_actual; i++)
			vecids[i] = i;
	}

	for (vector = 0; vector < v_actual; vector++) {
		adapter->msix_entries[vector].entry = vecids[vector];
		adapter->msix_entries[vector].vector =
			pci_irq_vector(adapter->pdev, vector);
	}

	adapter->num_req_msix = total_vecs;
	adapter->num_msix_entries = v_actual;
	/* 'num_avail_msix' is used to distribute excess vectors to the vports
	 * after considering the minimum vectors required per each default
	 * vport
	 */
	adapter->num_avail_msix = v_actual - min_vectors;

	/* Fill MSIX vector lifo stack with vector indexes */
	err = idpf_init_vector_stack(adapter);
	if (err)
		goto free_vecids;

	err = idpf_mb_intr_init(adapter);
	if (err)
		goto deinit_vec_stack;
	idpf_mb_irq_enable(adapter);
	kfree(vecids);

	return 0;

deinit_vec_stack:
	idpf_deinit_vector_stack(adapter);
free_vecids:
	kfree(vecids);
free_msix:
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
free_irq:
	pci_free_irq_vectors(adapter->pdev);
send_dealloc_vecs:
	idpf_send_dealloc_vectors_msg(adapter);

	return err;
}

/**
 * idpf_find_mac_filter - Search filter list for specific mac filter
 * @vconfig: Vport config structure
 * @macaddr: The MAC address
 *
 * Returns ptr to the filter object or NULL. Must be called while holding the
 * mac_filter_list_lock.
 **/
static struct idpf_mac_filter *idpf_find_mac_filter(struct idpf_vport_config *vconfig,
						    const u8 *macaddr)
{
	struct idpf_mac_filter *f;

	if (!macaddr)
		return NULL;

	list_for_each_entry(f, &vconfig->user_config.mac_filter_list, list) {
		if (ether_addr_equal(macaddr, f->macaddr))
			return f;
	}

	return NULL;
}

/**
 * __idpf_add_mac_filter - Add mac filter helper function
 * @vport_config: Vport config structure
 * @macaddr: Address to add
 *
 * Takes mac_filter_list_lock spinlock to add new filter to list.
 */
static int __idpf_add_mac_filter(struct idpf_vport_config *vport_config,
				 const u8 *macaddr)
{
	struct idpf_mac_filter *f;

	spin_lock_bh(&vport_config->mac_filter_list_lock);

	f = idpf_find_mac_filter(vport_config, macaddr);
	if (f) {
		f->remove = false;
		spin_unlock_bh(&vport_config->mac_filter_list_lock);

		return 0;
	}

	f = kzalloc(sizeof(*f), GFP_ATOMIC);
	if (!f) {
		spin_unlock_bh(&vport_config->mac_filter_list_lock);

		return -ENOMEM;
	}

	ether_addr_copy(f->macaddr, macaddr);
	list_add_tail(&f->list, &vport_config->user_config.mac_filter_list);
	f->add = true;

	spin_unlock_bh(&vport_config->mac_filter_list_lock);

	return 0;
}

/**
 * idpf_add_mac_filter - Add a mac filter to the filter list
 * @vport: Main vport structure
 * @np: Netdev private structure
 * @macaddr: The MAC address
 * @async: Don't wait for return message
 *
 * Returns 0 on success or error on failure. If interface is up, we'll also
 * send the virtchnl message to tell hardware about the filter.
 **/
static int idpf_add_mac_filter(struct idpf_vport *vport,
			       struct idpf_netdev_priv *np,
			       const u8 *macaddr, bool async)
{
	struct idpf_vport_config *vport_config;
	int err;

	vport_config = np->adapter->vport_config[np->vport_idx];
	err = __idpf_add_mac_filter(vport_config, macaddr);
	if (err)
		return err;

	if (np->state == __IDPF_VPORT_UP)
		err = idpf_add_del_mac_filters(vport, np, true, async);

	return err;
}

/**
 * idpf_deinit_mac_addr - deinitialize mac address for vport
 * @vport: main vport structure
 */
static void idpf_deinit_mac_addr(struct idpf_vport *vport)
{
	struct idpf_vport_config *vport_config;
	struct idpf_mac_filter *f;

	vport_config = vport->adapter->vport_config[vport->idx];

	spin_lock_bh(&vport_config->mac_filter_list_lock);

	f = idpf_find_mac_filter(vport_config, vport->default_mac_addr);
	if (f) {
		list_del(&f->list);
		kfree(f);
	}

	spin_unlock_bh(&vport_config->mac_filter_list_lock);
}

/**
 * idpf_init_mac_addr - initialize mac address for vport
 * @vport: main vport structure
 * @netdev: pointer to netdev struct associated with this vport
 */
static int idpf_init_mac_addr(struct idpf_vport *vport,
			      struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_adapter *adapter = vport->adapter;
	int err;

	if (is_valid_ether_addr(vport->default_mac_addr)) {
		eth_hw_addr_set(netdev, vport->default_mac_addr);
		ether_addr_copy(netdev->perm_addr, vport->default_mac_addr);

		return idpf_add_mac_filter(vport, np, vport->default_mac_addr,
					   false);
	}

	if (!idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS,
			     VIRTCHNL2_CAP_MACFILTER)) {
		dev_err(&adapter->pdev->dev,
			"MAC address is not provided and capability is not set\n");

		return -EINVAL;
	}

	eth_hw_addr_random(netdev);
	err = idpf_add_mac_filter(vport, np, netdev->dev_addr, false);
	if (err)
		return err;

	dev_info(&adapter->pdev->dev, "Invalid MAC address %pM, using random %pM\n",
		 vport->default_mac_addr, netdev->dev_addr);
	ether_addr_copy(vport->default_mac_addr, netdev->dev_addr);

	return 0;
}

/**
 * idpf_cfg_netdev - Allocate, configure and register a netdev
 * @vport: main vport structure
 *
 * Returns 0 on success, negative value on failure.
 */
static int idpf_cfg_netdev(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_vport_config *vport_config;
	netdev_features_t dflt_features;
	netdev_features_t offloads = 0;
	struct idpf_netdev_priv *np;
	struct net_device *netdev;
	u16 idx = vport->idx;
	int err;

	vport_config = adapter->vport_config[idx];

	/* It's possible we already have a netdev allocated and registered for
	 * this vport
	 */
	if (test_bit(IDPF_VPORT_REG_NETDEV, vport_config->flags)) {
		netdev = adapter->netdevs[idx];
		np = netdev_priv(netdev);
		np->vport = vport;
		np->vport_idx = vport->idx;
		np->vport_id = vport->vport_id;
		vport->netdev = netdev;

		return idpf_init_mac_addr(vport, netdev);
	}

	netdev = alloc_etherdev_mqs(sizeof(struct idpf_netdev_priv),
				    vport_config->max_q.max_txq,
				    vport_config->max_q.max_rxq);
	if (!netdev)
		return -ENOMEM;

	vport->netdev = netdev;
	np = netdev_priv(netdev);
	np->vport = vport;
	np->adapter = adapter;
	np->vport_idx = vport->idx;
	np->vport_id = vport->vport_id;

	err = idpf_init_mac_addr(vport, netdev);
	if (err) {
		free_netdev(vport->netdev);
		vport->netdev = NULL;

		return err;
	}

	/* assign netdev_ops */
	if (idpf_is_queue_model_split(vport->txq_model))
		netdev->netdev_ops = &idpf_netdev_ops_splitq;
	else
		netdev->netdev_ops = &idpf_netdev_ops_singleq;

	/* setup watchdog timeout value to be 5 second */
	netdev->watchdog_timeo = 5 * HZ;

	/* configure default MTU size */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = vport->max_mtu;

	dflt_features = NETIF_F_SG	|
			NETIF_F_HIGHDMA;

	if (idpf_is_cap_ena_all(adapter, IDPF_RSS_CAPS, IDPF_CAP_RSS))
		dflt_features |= NETIF_F_RXHASH;
	if (idpf_is_cap_ena_all(adapter, IDPF_CSUM_CAPS, IDPF_CAP_RX_CSUM_L4V4))
		dflt_features |= NETIF_F_IP_CSUM;
	if (idpf_is_cap_ena_all(adapter, IDPF_CSUM_CAPS, IDPF_CAP_RX_CSUM_L4V6))
		dflt_features |= NETIF_F_IPV6_CSUM;
	if (idpf_is_cap_ena(adapter, IDPF_CSUM_CAPS, IDPF_CAP_RX_CSUM))
		dflt_features |= NETIF_F_RXCSUM;
	if (idpf_is_cap_ena_all(adapter, IDPF_CSUM_CAPS, IDPF_CAP_SCTP_CSUM))
		dflt_features |= NETIF_F_SCTP_CRC;

	if (idpf_is_cap_ena(adapter, IDPF_SEG_CAPS, VIRTCHNL2_CAP_SEG_IPV4_TCP))
		dflt_features |= NETIF_F_TSO;
	if (idpf_is_cap_ena(adapter, IDPF_SEG_CAPS, VIRTCHNL2_CAP_SEG_IPV6_TCP))
		dflt_features |= NETIF_F_TSO6;
	if (idpf_is_cap_ena_all(adapter, IDPF_SEG_CAPS,
				VIRTCHNL2_CAP_SEG_IPV4_UDP |
				VIRTCHNL2_CAP_SEG_IPV6_UDP))
		dflt_features |= NETIF_F_GSO_UDP_L4;
	if (idpf_is_cap_ena_all(adapter, IDPF_RSC_CAPS, IDPF_CAP_RSC))
		offloads |= NETIF_F_GRO_HW;
	/* advertise to stack only if offloads for encapsulated packets is
	 * supported
	 */
	if (idpf_is_cap_ena(vport->adapter, IDPF_SEG_CAPS,
			    VIRTCHNL2_CAP_SEG_TX_SINGLE_TUNNEL)) {
		offloads |= NETIF_F_GSO_UDP_TUNNEL	|
			    NETIF_F_GSO_GRE		|
			    NETIF_F_GSO_GRE_CSUM	|
			    NETIF_F_GSO_PARTIAL		|
			    NETIF_F_GSO_UDP_TUNNEL_CSUM	|
			    NETIF_F_GSO_IPXIP4		|
			    NETIF_F_GSO_IPXIP6		|
			    0;

		if (!idpf_is_cap_ena_all(vport->adapter, IDPF_CSUM_CAPS,
					 IDPF_CAP_TUNNEL_TX_CSUM))
			netdev->gso_partial_features |=
				NETIF_F_GSO_UDP_TUNNEL_CSUM;

		netdev->gso_partial_features |= NETIF_F_GSO_GRE_CSUM;
		offloads |= NETIF_F_TSO_MANGLEID;
	}
	if (idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_LOOPBACK))
		offloads |= NETIF_F_LOOPBACK;

	netdev->features |= dflt_features;
	netdev->hw_features |= dflt_features | offloads;
	netdev->hw_enc_features |= dflt_features | offloads;
	SET_NETDEV_DEV(netdev, &adapter->pdev->dev);

	/* carrier off on init to avoid Tx hangs */
	netif_carrier_off(netdev);

	/* make sure transmit queues start off as stopped */
	netif_tx_stop_all_queues(netdev);

	/* The vport can be arbitrarily released so we need to also track
	 * netdevs in the adapter struct
	 */
	adapter->netdevs[idx] = netdev;

	return 0;
}

/**
 * idpf_get_free_slot - get the next non-NULL location index in array
 * @adapter: adapter in which to look for a free vport slot
 */
static int idpf_get_free_slot(struct idpf_adapter *adapter)
{
	unsigned int i;

	for (i = 0; i < adapter->max_vports; i++) {
		if (!adapter->vports[i])
			return i;
	}

	return IDPF_NO_FREE_SLOT;
}

/**
 * idpf_vport_stop - Disable a vport
 * @vport: vport to disable
 */
static void idpf_vport_stop(struct idpf_vport *vport)
{
	struct idpf_netdev_priv *np = netdev_priv(vport->netdev);

	if (np->state <= __IDPF_VPORT_DOWN)
		return;

	netif_carrier_off(vport->netdev);

	idpf_vport_intr_rel(vport);
	idpf_vport_queues_rel(vport);
	np->state = __IDPF_VPORT_DOWN;
}

/**
 * idpf_stop - Disables a network interface
 * @netdev: network interface device structure
 *
 * The stop entry point is called when an interface is de-activated by the OS,
 * and the netdevice enters the DOWN state.  The hardware is still under the
 * driver's control, but the netdev interface is disabled.
 *
 * Returns success only - not allowed to fail
 */
static int idpf_stop(struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport *vport;

	if (test_bit(IDPF_REMOVE_IN_PROG, np->adapter->flags))
		return 0;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	idpf_vport_stop(vport);

	idpf_vport_ctrl_unlock(netdev);

	return 0;
}

/**
 * idpf_decfg_netdev - Unregister the netdev
 * @vport: vport for which netdev to be unregistered
 */
static void idpf_decfg_netdev(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;

	unregister_netdev(vport->netdev);
	free_netdev(vport->netdev);
	vport->netdev = NULL;

	adapter->netdevs[vport->idx] = NULL;
}

/**
 * idpf_vport_rel - Delete a vport and free its resources
 * @vport: the vport being removed
 */
static void idpf_vport_rel(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_vport_config *vport_config;
	struct idpf_rss_data *rss_data;
	struct idpf_vport_max_q max_q;
	u16 idx = vport->idx;
	int i;

	vport_config = adapter->vport_config[vport->idx];
	idpf_deinit_rss(vport);
	rss_data = &vport_config->user_config.rss_data;
	kfree(rss_data->rss_key);
	rss_data->rss_key = NULL;

	idpf_send_destroy_vport_msg(vport);

	/* Set all bits as we dont know on which vc_state the vport vhnl_wq
	 * is waiting on and wakeup the virtchnl workqueue even if it is
	 * waiting for the response as we are going down
	 */
	for (i = 0; i < IDPF_VC_NBITS; i++)
		set_bit(i, vport->vc_state);
	wake_up(&vport->vchnl_wq);

	mutex_destroy(&vport->vc_buf_lock);

	/* Clear all the bits */
	for (i = 0; i < IDPF_VC_NBITS; i++)
		clear_bit(i, vport->vc_state);

	/* Release all max queues allocated to the adapter's pool */
	max_q.max_rxq = vport_config->max_q.max_rxq;
	max_q.max_txq = vport_config->max_q.max_txq;
	max_q.max_bufq = vport_config->max_q.max_bufq;
	max_q.max_complq = vport_config->max_q.max_complq;
	idpf_vport_dealloc_max_qs(adapter, &max_q);

	kfree(adapter->vport_params_recvd[idx]);
	adapter->vport_params_recvd[idx] = NULL;
	kfree(adapter->vport_params_reqd[idx]);
	adapter->vport_params_reqd[idx] = NULL;
	kfree(vport);
	adapter->num_alloc_vports--;
}

/**
 * idpf_vport_dealloc - cleanup and release a given vport
 * @vport: pointer to idpf vport structure
 *
 * returns nothing
 */
static void idpf_vport_dealloc(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	unsigned int i = vport->idx;

	idpf_deinit_mac_addr(vport);

	if (!test_bit(IDPF_HR_RESET_IN_PROG, adapter->flags))
		idpf_decfg_netdev(vport);

	if (adapter->netdevs[i]) {
		struct idpf_netdev_priv *np = netdev_priv(adapter->netdevs[i]);

		np->vport = NULL;
	}

	idpf_vport_rel(vport);

	adapter->vports[i] = NULL;
	adapter->next_vport = idpf_get_free_slot(adapter);
}

/**
 * idpf_vport_alloc - Allocates the next available struct vport in the adapter
 * @adapter: board private structure
 * @max_q: vport max queue info
 *
 * returns a pointer to a vport on success, NULL on failure.
 */
static struct idpf_vport *idpf_vport_alloc(struct idpf_adapter *adapter,
					   struct idpf_vport_max_q *max_q)
{
	struct idpf_rss_data *rss_data;
	u16 idx = adapter->next_vport;
	struct idpf_vport *vport;

	if (idx == IDPF_NO_FREE_SLOT)
		return NULL;

	vport = kzalloc(sizeof(*vport), GFP_KERNEL);
	if (!vport)
		return vport;

	if (!adapter->vport_config[idx]) {
		struct idpf_vport_config *vport_config;

		vport_config = kzalloc(sizeof(*vport_config), GFP_KERNEL);
		if (!vport_config) {
			kfree(vport);

			return NULL;
		}

		adapter->vport_config[idx] = vport_config;
	}

	vport->idx = idx;
	vport->adapter = adapter;
	vport->default_vport = adapter->num_alloc_vports <
			       idpf_get_default_vports(adapter);

	idpf_vport_init(vport, max_q);

	/* This alloc is done separate from the LUT because it's not strictly
	 * dependent on how many queues we have. If we change number of queues
	 * and soft reset we'll need a new LUT but the key can remain the same
	 * for as long as the vport exists.
	 */
	rss_data = &adapter->vport_config[idx]->user_config.rss_data;
	rss_data->rss_key = kzalloc(rss_data->rss_key_size, GFP_KERNEL);
	if (!rss_data->rss_key) {
		kfree(vport);

		return NULL;
	}
	/* Initialize default rss key */
	netdev_rss_key_fill((void *)rss_data->rss_key, rss_data->rss_key_size);

	/* fill vport slot in the adapter struct */
	adapter->vports[idx] = vport;
	adapter->vport_ids[idx] = idpf_get_vport_id(vport);

	adapter->num_alloc_vports++;
	/* prepare adapter->next_vport for next use */
	adapter->next_vport = idpf_get_free_slot(adapter);

	return vport;
}

/**
 * idpf_mbx_task - Delayed task to handle mailbox responses
 * @work: work_struct handle
 */
void idpf_mbx_task(struct work_struct *work)
{
	struct idpf_adapter *adapter;

	adapter = container_of(work, struct idpf_adapter, mbx_task.work);

	if (test_bit(IDPF_MB_INTR_MODE, adapter->flags))
		idpf_mb_irq_enable(adapter);
	else
		queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task,
				   msecs_to_jiffies(300));

	idpf_recv_mb_msg(adapter, VIRTCHNL2_OP_UNKNOWN, NULL, 0);
}

/**
 * idpf_service_task - Delayed task for handling mailbox responses
 * @work: work_struct handle to our data
 *
 */
void idpf_service_task(struct work_struct *work)
{
	struct idpf_adapter *adapter;

	adapter = container_of(work, struct idpf_adapter, serv_task.work);

	if (idpf_is_reset_detected(adapter) &&
	    !idpf_is_reset_in_prog(adapter) &&
	    !test_bit(IDPF_REMOVE_IN_PROG, adapter->flags)) {
		dev_info(&adapter->pdev->dev, "HW reset detected\n");
		set_bit(IDPF_HR_FUNC_RESET, adapter->flags);
		queue_delayed_work(adapter->vc_event_wq,
				   &adapter->vc_event_task,
				   msecs_to_jiffies(10));
	}

	queue_delayed_work(adapter->serv_wq, &adapter->serv_task,
			   msecs_to_jiffies(300));
}

/**
 * idpf_vport_open - Bring up a vport
 * @vport: vport to bring up
 * @alloc_res: allocate queue resources
 */
static int idpf_vport_open(struct idpf_vport *vport, bool alloc_res)
{
	struct idpf_netdev_priv *np = netdev_priv(vport->netdev);
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_vport_config *vport_config;
	int err;

	if (np->state != __IDPF_VPORT_DOWN)
		return -EBUSY;

	/* we do not allow interface up just yet */
	netif_carrier_off(vport->netdev);

	if (alloc_res) {
		err = idpf_vport_queues_alloc(vport);
		if (err)
			return err;
	}

	err = idpf_vport_intr_alloc(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to allocate interrupts for vport %u: %d\n",
			vport->vport_id, err);
		goto queues_rel;
	}

	err = idpf_vport_queue_ids_init(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize queue ids for vport %u: %d\n",
			vport->vport_id, err);
		goto intr_rel;
	}

	err = idpf_rx_bufs_init_all(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize RX buffers for vport %u: %d\n",
			vport->vport_id, err);
		goto intr_rel;
	}

	err = idpf_queue_reg_init(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize queue registers for vport %u: %d\n",
			vport->vport_id, err);
		goto intr_rel;
	}

	err = idpf_send_config_queues_msg(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to configure queues for vport %u, %d\n",
			vport->vport_id, err);
		goto intr_rel;
	}

	vport_config = adapter->vport_config[vport->idx];
	if (vport_config->user_config.rss_data.rss_lut)
		err = idpf_config_rss(vport);
	else
		err = idpf_init_rss(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize RSS for vport %u: %d\n",
			vport->vport_id, err);
		goto intr_rel;
	}

	return 0;

intr_rel:
	idpf_vport_intr_rel(vport);
queues_rel:
	idpf_vport_queues_rel(vport);

	return err;
}

/**
 * idpf_init_task - Delayed initialization task
 * @work: work_struct handle to our data
 *
 * Init task finishes up pending work started in probe. Due to the asynchronous
 * nature in which the device communicates with hardware, we may have to wait
 * several milliseconds to get a response.  Instead of busy polling in probe,
 * pulling it out into a delayed work task prevents us from bogging down the
 * whole system waiting for a response from hardware.
 */
void idpf_init_task(struct work_struct *work)
{
	struct idpf_vport_config *vport_config;
	struct idpf_vport_max_q max_q;
	struct idpf_adapter *adapter;
	struct idpf_netdev_priv *np;
	struct idpf_vport *vport;
	u16 num_default_vports;
	struct pci_dev *pdev;
	bool default_vport;
	int index, err;

	adapter = container_of(work, struct idpf_adapter, init_task.work);

	num_default_vports = idpf_get_default_vports(adapter);
	if (adapter->num_alloc_vports < num_default_vports)
		default_vport = true;
	else
		default_vport = false;

	err = idpf_vport_alloc_max_qs(adapter, &max_q);
	if (err)
		goto unwind_vports;

	err = idpf_send_create_vport_msg(adapter, &max_q);
	if (err) {
		idpf_vport_dealloc_max_qs(adapter, &max_q);
		goto unwind_vports;
	}

	pdev = adapter->pdev;
	vport = idpf_vport_alloc(adapter, &max_q);
	if (!vport) {
		err = -EFAULT;
		dev_err(&pdev->dev, "failed to allocate vport: %d\n",
			err);
		idpf_vport_dealloc_max_qs(adapter, &max_q);
		goto unwind_vports;
	}

	index = vport->idx;
	vport_config = adapter->vport_config[index];

	init_waitqueue_head(&vport->vchnl_wq);

	mutex_init(&vport->vc_buf_lock);
	spin_lock_init(&vport_config->mac_filter_list_lock);

	INIT_LIST_HEAD(&vport_config->user_config.mac_filter_list);

	err = idpf_check_supported_desc_ids(vport);
	if (err) {
		dev_err(&pdev->dev, "failed to get required descriptor ids\n");
		goto cfg_netdev_err;
	}

	if (idpf_cfg_netdev(vport))
		goto cfg_netdev_err;

	err = idpf_send_get_rx_ptype_msg(vport);
	if (err)
		goto handle_err;

	/* Once state is put into DOWN, driver is ready for dev_open */
	np = netdev_priv(vport->netdev);
	np->state = __IDPF_VPORT_DOWN;
	if (test_and_clear_bit(IDPF_VPORT_UP_REQUESTED, vport_config->flags))
		idpf_vport_open(vport, true);

	/* Spawn and return 'idpf_init_task' work queue until all the
	 * default vports are created
	 */
	if (adapter->num_alloc_vports < num_default_vports) {
		queue_delayed_work(adapter->init_wq, &adapter->init_task,
				   msecs_to_jiffies(5 * (adapter->pdev->devfn & 0x07)));

		return;
	}

	for (index = 0; index < adapter->max_vports; index++) {
		if (adapter->netdevs[index] &&
		    !test_bit(IDPF_VPORT_REG_NETDEV,
			      adapter->vport_config[index]->flags)) {
			register_netdev(adapter->netdevs[index]);
			set_bit(IDPF_VPORT_REG_NETDEV,
				adapter->vport_config[index]->flags);
		}
	}

	/* As all the required vports are created, clear the reset flag
	 * unconditionally here in case we were in reset and the link was down.
	 */
	clear_bit(IDPF_HR_RESET_IN_PROG, adapter->flags);

	return;

handle_err:
	idpf_decfg_netdev(vport);
cfg_netdev_err:
	idpf_vport_rel(vport);
	adapter->vports[index] = NULL;
unwind_vports:
	if (default_vport) {
		for (index = 0; index < adapter->max_vports; index++) {
			if (adapter->vports[index])
				idpf_vport_dealloc(adapter->vports[index]);
		}
	}
	clear_bit(IDPF_HR_RESET_IN_PROG, adapter->flags);
}

/**
 * idpf_deinit_task - Device deinit routine
 * @adapter: Driver specific private structure
 *
 * Extended remove logic which will be used for
 * hard reset as well
 */
void idpf_deinit_task(struct idpf_adapter *adapter)
{
	unsigned int i;

	/* Wait until the init_task is done else this thread might release
	 * the resources first and the other thread might end up in a bad state
	 */
	cancel_delayed_work_sync(&adapter->init_task);

	if (!adapter->vports)
		return;

	for (i = 0; i < adapter->max_vports; i++) {
		if (adapter->vports[i])
			idpf_vport_dealloc(adapter->vports[i]);
	}
}

/**
 * idpf_check_reset_complete - check that reset is complete
 * @hw: pointer to hw struct
 * @reset_reg: struct with reset registers
 *
 * Returns 0 if device is ready to use, or -EBUSY if it's in reset.
 **/
static int idpf_check_reset_complete(struct idpf_hw *hw,
				     struct idpf_reset_reg *reset_reg)
{
	struct idpf_adapter *adapter = hw->back;
	int i;

	for (i = 0; i < 2000; i++) {
		u32 reg_val = readl(reset_reg->rstat);

		/* 0xFFFFFFFF might be read if other side hasn't cleared the
		 * register for us yet and 0xFFFFFFFF is not a valid value for
		 * the register, so treat that as invalid.
		 */
		if (reg_val != 0xFFFFFFFF && (reg_val & reset_reg->rstat_m))
			return 0;

		usleep_range(5000, 10000);
	}

	dev_warn(&adapter->pdev->dev, "Device reset timeout!\n");
	/* Clear the reset flag unconditionally here since the reset
	 * technically isn't in progress anymore from the driver's perspective
	 */
	clear_bit(IDPF_HR_RESET_IN_PROG, adapter->flags);

	return -EBUSY;
}

/**
 * idpf_set_vport_state - Set the vport state to be after the reset
 * @adapter: Driver specific private structure
 */
static void idpf_set_vport_state(struct idpf_adapter *adapter)
{
	u16 i;

	for (i = 0; i < adapter->max_vports; i++) {
		struct idpf_netdev_priv *np;

		if (!adapter->netdevs[i])
			continue;

		np = netdev_priv(adapter->netdevs[i]);
		if (np->state == __IDPF_VPORT_UP)
			set_bit(IDPF_VPORT_UP_REQUESTED,
				adapter->vport_config[i]->flags);
	}
}

/**
 * idpf_init_hard_reset - Initiate a hardware reset
 * @adapter: Driver specific private structure
 *
 * Deallocate the vports and all the resources associated with them and
 * reallocate. Also reinitialize the mailbox. Return 0 on success,
 * negative on failure.
 */
static int idpf_init_hard_reset(struct idpf_adapter *adapter)
{
	struct idpf_reg_ops *reg_ops = &adapter->dev_ops.reg_ops;
	struct device *dev = &adapter->pdev->dev;
	struct net_device *netdev;
	int err;
	u16 i;

	mutex_lock(&adapter->vport_ctrl_lock);

	dev_info(dev, "Device HW Reset initiated\n");

	/* Avoid TX hangs on reset */
	for (i = 0; i < adapter->max_vports; i++) {
		netdev = adapter->netdevs[i];
		if (!netdev)
			continue;

		netif_carrier_off(netdev);
		netif_tx_disable(netdev);
	}

	/* Prepare for reset */
	if (test_and_clear_bit(IDPF_HR_DRV_LOAD, adapter->flags)) {
		reg_ops->trigger_reset(adapter, IDPF_HR_DRV_LOAD);
	} else if (test_and_clear_bit(IDPF_HR_FUNC_RESET, adapter->flags)) {
		bool is_reset = idpf_is_reset_detected(adapter);

		idpf_set_vport_state(adapter);
		idpf_vc_core_deinit(adapter);
		if (!is_reset)
			reg_ops->trigger_reset(adapter, IDPF_HR_FUNC_RESET);
		idpf_deinit_dflt_mbx(adapter);
	} else {
		dev_err(dev, "Unhandled hard reset cause\n");
		err = -EBADRQC;
		goto unlock_mutex;
	}

	/* Wait for reset to complete */
	err = idpf_check_reset_complete(&adapter->hw, &adapter->reset_reg);
	if (err) {
		dev_err(dev, "The driver was unable to contact the device's firmware. Check that the FW is running. Driver state= 0x%x\n",
			adapter->state);
		goto unlock_mutex;
	}

	/* Reset is complete and so start building the driver resources again */
	err = idpf_init_dflt_mbx(adapter);
	if (err) {
		dev_err(dev, "Failed to initialize default mailbox: %d\n", err);
		goto unlock_mutex;
	}

	/* Initialize the state machine, also allocate memory and request
	 * resources
	 */
	err = idpf_vc_core_init(adapter);
	if (err) {
		idpf_deinit_dflt_mbx(adapter);
		goto unlock_mutex;
	}

	/* Wait till all the vports are initialized to release the reset lock,
	 * else user space callbacks may access uninitialized vports
	 */
	while (test_bit(IDPF_HR_RESET_IN_PROG, adapter->flags))
		msleep(100);

unlock_mutex:
	mutex_unlock(&adapter->vport_ctrl_lock);

	return err;
}

/**
 * idpf_vc_event_task - Handle virtchannel event logic
 * @work: work queue struct
 */
void idpf_vc_event_task(struct work_struct *work)
{
	struct idpf_adapter *adapter;

	adapter = container_of(work, struct idpf_adapter, vc_event_task.work);

	if (test_bit(IDPF_REMOVE_IN_PROG, adapter->flags))
		return;

	if (test_bit(IDPF_HR_FUNC_RESET, adapter->flags) ||
	    test_bit(IDPF_HR_DRV_LOAD, adapter->flags)) {
		set_bit(IDPF_HR_RESET_IN_PROG, adapter->flags);
		idpf_init_hard_reset(adapter);
	}
}

/**
 * idpf_open - Called when a network interface becomes active
 * @netdev: network interface device structure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the netdev watchdog is enabled,
 * and the stack is notified that the interface is ready.
 *
 * Returns 0 on success, negative value on failure
 */
static int idpf_open(struct net_device *netdev)
{
	struct idpf_vport *vport;
	int err;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	err = idpf_vport_open(vport, true);

	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_alloc_dma_mem - Allocate dma memory
 * @hw: pointer to hw struct
 * @mem: pointer to dma_mem struct
 * @size: size of the memory to allocate
 */
void *idpf_alloc_dma_mem(struct idpf_hw *hw, struct idpf_dma_mem *mem, u64 size)
{
	struct idpf_adapter *adapter = hw->back;
	size_t sz = ALIGN(size, 4096);

	mem->va = dma_alloc_coherent(&adapter->pdev->dev, sz,
				     &mem->pa, GFP_KERNEL);
	mem->size = sz;

	return mem->va;
}

/**
 * idpf_free_dma_mem - Free the allocated dma memory
 * @hw: pointer to hw struct
 * @mem: pointer to dma_mem struct
 */
void idpf_free_dma_mem(struct idpf_hw *hw, struct idpf_dma_mem *mem)
{
	struct idpf_adapter *adapter = hw->back;

	dma_free_coherent(&adapter->pdev->dev, mem->size,
			  mem->va, mem->pa);
	mem->size = 0;
	mem->va = NULL;
	mem->pa = 0;
}

static const struct net_device_ops idpf_netdev_ops_splitq = {
	.ndo_open = idpf_open,
	.ndo_stop = idpf_stop,
};

static const struct net_device_ops idpf_netdev_ops_singleq = {
	.ndo_open = idpf_open,
	.ndo_stop = idpf_stop,
};
