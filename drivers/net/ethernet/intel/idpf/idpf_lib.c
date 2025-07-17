// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_virtchnl.h"
#include "idpf_ptp.h"

static const struct net_device_ops idpf_netdev_ops;

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
	kfree(free_irq(adapter->msix_entries[0].vector, adapter));
	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);
}

/**
 * idpf_intr_rel - Release interrupt capabilities and free memory
 * @adapter: adapter to disable interrupts on
 */
void idpf_intr_rel(struct idpf_adapter *adapter)
{
	if (!adapter->msix_entries)
		return;

	idpf_mb_intr_rel_irq(adapter);
	pci_free_irq_vectors(adapter->pdev);
	idpf_send_dealloc_vectors_msg(adapter);
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
	int irq_num, mb_vidx = 0, err;
	char *name;

	irq_num = adapter->msix_entries[mb_vidx].vector;
	name = kasprintf(GFP_KERNEL, "%s-%s-%d",
			 dev_driver_string(&adapter->pdev->dev),
			 "Mailbox", mb_vidx);
	err = request_irq(irq_num, adapter->irq_mb_handler, 0, name, adapter);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"IRQ request for mailbox failed, error: %d\n", err);

		return err;
	}

	set_bit(IDPF_MB_INTR_MODE, adapter->flags);

	return 0;
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
 * idpf_vector_lifo_push - push MSIX vector index onto stack
 * @adapter: private data struct
 * @vec_idx: vector index to store
 */
static int idpf_vector_lifo_push(struct idpf_adapter *adapter, u16 vec_idx)
{
	struct idpf_vector_lifo *stack = &adapter->vector_stack;

	lockdep_assert_held(&adapter->vector_lock);

	if (stack->top == stack->base) {
		dev_err(&adapter->pdev->dev, "Exceeded the vector stack limit: %d\n",
			stack->top);
		return -EINVAL;
	}

	stack->vec_idx[--stack->top] = vec_idx;

	return 0;
}

/**
 * idpf_vector_lifo_pop - pop MSIX vector index from stack
 * @adapter: private data struct
 */
static int idpf_vector_lifo_pop(struct idpf_adapter *adapter)
{
	struct idpf_vector_lifo *stack = &adapter->vector_stack;

	lockdep_assert_held(&adapter->vector_lock);

	if (stack->top == stack->size) {
		dev_err(&adapter->pdev->dev, "No interrupt vectors are available to distribute!\n");

		return -EINVAL;
	}

	return stack->vec_idx[stack->top++];
}

/**
 * idpf_vector_stash - Store the vector indexes onto the stack
 * @adapter: private data struct
 * @q_vector_idxs: vector index array
 * @vec_info: info related to the number of vectors
 *
 * This function is a no-op if there are no vectors indexes to be stashed
 */
static void idpf_vector_stash(struct idpf_adapter *adapter, u16 *q_vector_idxs,
			      struct idpf_vector_info *vec_info)
{
	int i, base = 0;
	u16 vec_idx;

	lockdep_assert_held(&adapter->vector_lock);

	if (!vec_info->num_curr_vecs)
		return;

	/* For default vports, no need to stash vector allocated from the
	 * default pool onto the stack
	 */
	if (vec_info->default_vport)
		base = IDPF_MIN_Q_VEC;

	for (i = vec_info->num_curr_vecs - 1; i >= base ; i--) {
		vec_idx = q_vector_idxs[i];
		idpf_vector_lifo_push(adapter, vec_idx);
		adapter->num_avail_msix++;
	}
}

/**
 * idpf_req_rel_vector_indexes - Request or release MSIX vector indexes
 * @adapter: driver specific private structure
 * @q_vector_idxs: vector index array
 * @vec_info: info related to the number of vectors
 *
 * This is the core function to distribute the MSIX vectors acquired from the
 * OS. It expects the caller to pass the number of vectors required and
 * also previously allocated. First, it stashes previously allocated vector
 * indexes on to the stack and then figures out if it can allocate requested
 * vectors. It can wait on acquiring the mutex lock. If the caller passes 0 as
 * requested vectors, then this function just stashes the already allocated
 * vectors and returns 0.
 *
 * Returns actual number of vectors allocated on success, error value on failure
 * If 0 is returned, implies the stack has no vectors to allocate which is also
 * a failure case for the caller
 */
int idpf_req_rel_vector_indexes(struct idpf_adapter *adapter,
				u16 *q_vector_idxs,
				struct idpf_vector_info *vec_info)
{
	u16 num_req_vecs, num_alloc_vecs = 0, max_vecs;
	struct idpf_vector_lifo *stack;
	int i, j, vecid;

	mutex_lock(&adapter->vector_lock);
	stack = &adapter->vector_stack;
	num_req_vecs = vec_info->num_req_vecs;

	/* Stash interrupt vector indexes onto the stack if required */
	idpf_vector_stash(adapter, q_vector_idxs, vec_info);

	if (!num_req_vecs)
		goto rel_lock;

	if (vec_info->default_vport) {
		/* As IDPF_MIN_Q_VEC per default vport is put aside in the
		 * default pool of the stack, use them for default vports
		 */
		j = vec_info->index * IDPF_MIN_Q_VEC + IDPF_MBX_Q_VEC;
		for (i = 0; i < IDPF_MIN_Q_VEC; i++) {
			q_vector_idxs[num_alloc_vecs++] = stack->vec_idx[j++];
			num_req_vecs--;
		}
	}

	/* Find if stack has enough vector to allocate */
	max_vecs = min(adapter->num_avail_msix, num_req_vecs);

	for (j = 0; j < max_vecs; j++) {
		vecid = idpf_vector_lifo_pop(adapter);
		q_vector_idxs[num_alloc_vecs++] = vecid;
	}
	adapter->num_avail_msix -= max_vecs;

rel_lock:
	mutex_unlock(&adapter->vector_lock);

	return num_alloc_vecs;
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

	adapter->mb_vector.v_idx = le16_to_cpu(adapter->caps.mailbox_vector_id);

	vecids = kcalloc(total_vecs, sizeof(u16), GFP_KERNEL);
	if (!vecids) {
		err = -ENOMEM;
		goto free_msix;
	}

	num_vec_ids = idpf_get_vec_ids(adapter, vecids, total_vecs,
				       &adapter->req_vec_chunks->vchunks);
	if (num_vec_ids < v_actual) {
		err = -EINVAL;
		goto free_vecids;
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
 * __idpf_del_mac_filter - Delete a MAC filter from the filter list
 * @vport_config: Vport config structure
 * @macaddr: The MAC address
 *
 * Returns 0 on success, error value on failure
 **/
static int __idpf_del_mac_filter(struct idpf_vport_config *vport_config,
				 const u8 *macaddr)
{
	struct idpf_mac_filter *f;

	spin_lock_bh(&vport_config->mac_filter_list_lock);
	f = idpf_find_mac_filter(vport_config, macaddr);
	if (f) {
		list_del(&f->list);
		kfree(f);
	}
	spin_unlock_bh(&vport_config->mac_filter_list_lock);

	return 0;
}

/**
 * idpf_del_mac_filter - Delete a MAC filter from the filter list
 * @vport: Main vport structure
 * @np: Netdev private structure
 * @macaddr: The MAC address
 * @async: Don't wait for return message
 *
 * Removes filter from list and if interface is up, tells hardware about the
 * removed filter.
 **/
static int idpf_del_mac_filter(struct idpf_vport *vport,
			       struct idpf_netdev_priv *np,
			       const u8 *macaddr, bool async)
{
	struct idpf_vport_config *vport_config;
	struct idpf_mac_filter *f;

	vport_config = np->adapter->vport_config[np->vport_idx];

	spin_lock_bh(&vport_config->mac_filter_list_lock);
	f = idpf_find_mac_filter(vport_config, macaddr);
	if (f) {
		f->remove = true;
	} else {
		spin_unlock_bh(&vport_config->mac_filter_list_lock);

		return -EINVAL;
	}
	spin_unlock_bh(&vport_config->mac_filter_list_lock);

	if (np->state == __IDPF_VPORT_UP) {
		int err;

		err = idpf_add_del_mac_filters(vport, np, false, async);
		if (err)
			return err;
	}

	return  __idpf_del_mac_filter(vport_config, macaddr);
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
 * idpf_del_all_mac_filters - Delete all MAC filters in list
 * @vport: main vport struct
 *
 * Takes mac_filter_list_lock spinlock.  Deletes all filters
 */
static void idpf_del_all_mac_filters(struct idpf_vport *vport)
{
	struct idpf_vport_config *vport_config;
	struct idpf_mac_filter *f, *ftmp;

	vport_config = vport->adapter->vport_config[vport->idx];
	spin_lock_bh(&vport_config->mac_filter_list_lock);

	list_for_each_entry_safe(f, ftmp, &vport_config->user_config.mac_filter_list,
				 list) {
		list_del(&f->list);
		kfree(f);
	}

	spin_unlock_bh(&vport_config->mac_filter_list_lock);
}

/**
 * idpf_restore_mac_filters - Re-add all MAC filters in list
 * @vport: main vport struct
 *
 * Takes mac_filter_list_lock spinlock.  Sets add field to true for filters to
 * resync filters back to HW.
 */
static void idpf_restore_mac_filters(struct idpf_vport *vport)
{
	struct idpf_vport_config *vport_config;
	struct idpf_mac_filter *f;

	vport_config = vport->adapter->vport_config[vport->idx];
	spin_lock_bh(&vport_config->mac_filter_list_lock);

	list_for_each_entry(f, &vport_config->user_config.mac_filter_list, list)
		f->add = true;

	spin_unlock_bh(&vport_config->mac_filter_list_lock);

	idpf_add_del_mac_filters(vport, netdev_priv(vport->netdev),
				 true, false);
}

/**
 * idpf_remove_mac_filters - Remove all MAC filters in list
 * @vport: main vport struct
 *
 * Takes mac_filter_list_lock spinlock. Sets remove field to true for filters
 * to remove filters in HW.
 */
static void idpf_remove_mac_filters(struct idpf_vport *vport)
{
	struct idpf_vport_config *vport_config;
	struct idpf_mac_filter *f;

	vport_config = vport->adapter->vport_config[vport->idx];
	spin_lock_bh(&vport_config->mac_filter_list_lock);

	list_for_each_entry(f, &vport_config->user_config.mac_filter_list, list)
		f->remove = true;

	spin_unlock_bh(&vport_config->mac_filter_list_lock);

	idpf_add_del_mac_filters(vport, netdev_priv(vport->netdev),
				 false, false);
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
	netdev_features_t other_offloads = 0;
	netdev_features_t csum_offloads = 0;
	netdev_features_t tso_offloads = 0;
	netdev_features_t dflt_features;
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
		np->max_tx_hdr_size = idpf_get_max_tx_hdr_size(adapter);
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
	np->max_tx_hdr_size = idpf_get_max_tx_hdr_size(adapter);

	spin_lock_init(&np->stats_lock);

	err = idpf_init_mac_addr(vport, netdev);
	if (err) {
		free_netdev(vport->netdev);
		vport->netdev = NULL;

		return err;
	}

	/* assign netdev_ops */
	netdev->netdev_ops = &idpf_netdev_ops;

	/* setup watchdog timeout value to be 5 second */
	netdev->watchdog_timeo = 5 * HZ;

	netdev->dev_port = idx;

	/* configure default MTU size */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = vport->max_mtu;

	dflt_features = NETIF_F_SG	|
			NETIF_F_HIGHDMA;

	if (idpf_is_cap_ena_all(adapter, IDPF_RSS_CAPS, IDPF_CAP_RSS))
		dflt_features |= NETIF_F_RXHASH;
	if (idpf_is_cap_ena_all(adapter, IDPF_CSUM_CAPS, IDPF_CAP_TX_CSUM_L4V4))
		csum_offloads |= NETIF_F_IP_CSUM;
	if (idpf_is_cap_ena_all(adapter, IDPF_CSUM_CAPS, IDPF_CAP_TX_CSUM_L4V6))
		csum_offloads |= NETIF_F_IPV6_CSUM;
	if (idpf_is_cap_ena(adapter, IDPF_CSUM_CAPS, IDPF_CAP_RX_CSUM))
		csum_offloads |= NETIF_F_RXCSUM;
	if (idpf_is_cap_ena_all(adapter, IDPF_CSUM_CAPS, IDPF_CAP_TX_SCTP_CSUM))
		csum_offloads |= NETIF_F_SCTP_CRC;

	if (idpf_is_cap_ena(adapter, IDPF_SEG_CAPS, VIRTCHNL2_CAP_SEG_IPV4_TCP))
		tso_offloads |= NETIF_F_TSO;
	if (idpf_is_cap_ena(adapter, IDPF_SEG_CAPS, VIRTCHNL2_CAP_SEG_IPV6_TCP))
		tso_offloads |= NETIF_F_TSO6;
	if (idpf_is_cap_ena_all(adapter, IDPF_SEG_CAPS,
				VIRTCHNL2_CAP_SEG_IPV4_UDP |
				VIRTCHNL2_CAP_SEG_IPV6_UDP))
		tso_offloads |= NETIF_F_GSO_UDP_L4;
	if (idpf_is_cap_ena_all(adapter, IDPF_RSC_CAPS, IDPF_CAP_RSC))
		other_offloads |= NETIF_F_GRO_HW;
	if (idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_LOOPBACK))
		other_offloads |= NETIF_F_LOOPBACK;

	netdev->features |= dflt_features | csum_offloads | tso_offloads;
	netdev->hw_features |=  netdev->features | other_offloads;
	netdev->vlan_features |= netdev->features | other_offloads;
	netdev->hw_enc_features |= dflt_features | other_offloads;
	idpf_set_ethtool_ops(netdev);
	netif_set_affinity_auto(netdev);
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
 * idpf_remove_features - Turn off feature configs
 * @vport: virtual port structure
 */
static void idpf_remove_features(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;

	if (idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_MACFILTER))
		idpf_remove_mac_filters(vport);
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
	netif_tx_disable(vport->netdev);

	idpf_send_disable_vport_msg(vport);
	idpf_send_disable_queues_msg(vport);
	idpf_send_map_unmap_queue_vector_msg(vport, false);
	/* Normally we ask for queues in create_vport, but if the number of
	 * initially requested queues have changed, for example via ethtool
	 * set channels, we do delete queues and then add the queues back
	 * instead of deleting and reallocating the vport.
	 */
	if (test_and_clear_bit(IDPF_VPORT_DEL_QUEUES, vport->flags))
		idpf_send_delete_queues_msg(vport);

	idpf_remove_features(vport);

	vport->link_up = false;
	idpf_vport_intr_deinit(vport);
	idpf_vport_queues_rel(vport);
	idpf_vport_intr_rel(vport);
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
	u16 idx = vport->idx;

	kfree(vport->rx_ptype_lkup);
	vport->rx_ptype_lkup = NULL;

	if (test_and_clear_bit(IDPF_VPORT_REG_NETDEV,
			       adapter->vport_config[idx]->flags)) {
		unregister_netdev(vport->netdev);
		free_netdev(vport->netdev);
	}
	vport->netdev = NULL;

	adapter->netdevs[idx] = NULL;
}

/**
 * idpf_vport_rel - Delete a vport and free its resources
 * @vport: the vport being removed
 */
static void idpf_vport_rel(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_vport_config *vport_config;
	struct idpf_vector_info vec_info;
	struct idpf_rss_data *rss_data;
	struct idpf_vport_max_q max_q;
	u16 idx = vport->idx;

	vport_config = adapter->vport_config[vport->idx];
	idpf_deinit_rss(vport);
	rss_data = &vport_config->user_config.rss_data;
	kfree(rss_data->rss_key);
	rss_data->rss_key = NULL;

	idpf_send_destroy_vport_msg(vport);

	/* Release all max queues allocated to the adapter's pool */
	max_q.max_rxq = vport_config->max_q.max_rxq;
	max_q.max_txq = vport_config->max_q.max_txq;
	max_q.max_bufq = vport_config->max_q.max_bufq;
	max_q.max_complq = vport_config->max_q.max_complq;
	idpf_vport_dealloc_max_qs(adapter, &max_q);

	/* Release all the allocated vectors on the stack */
	vec_info.num_req_vecs = 0;
	vec_info.num_curr_vecs = vport->num_q_vectors;
	vec_info.default_vport = vport->default_vport;

	idpf_req_rel_vector_indexes(adapter, vport->q_vector_idxs, &vec_info);

	kfree(vport->q_vector_idxs);
	vport->q_vector_idxs = NULL;

	kfree(adapter->vport_params_recvd[idx]);
	adapter->vport_params_recvd[idx] = NULL;
	kfree(adapter->vport_params_reqd[idx]);
	adapter->vport_params_reqd[idx] = NULL;
	if (adapter->vport_config[idx]) {
		kfree(adapter->vport_config[idx]->req_qs_chunks);
		adapter->vport_config[idx]->req_qs_chunks = NULL;
	}
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
	idpf_vport_stop(vport);

	if (!test_bit(IDPF_HR_RESET_IN_PROG, adapter->flags))
		idpf_decfg_netdev(vport);
	if (test_bit(IDPF_REMOVE_IN_PROG, adapter->flags))
		idpf_del_all_mac_filters(vport);

	if (adapter->netdevs[i]) {
		struct idpf_netdev_priv *np = netdev_priv(adapter->netdevs[i]);

		np->vport = NULL;
	}

	idpf_vport_rel(vport);

	adapter->vports[i] = NULL;
	adapter->next_vport = idpf_get_free_slot(adapter);
}

/**
 * idpf_is_hsplit_supported - check whether the header split is supported
 * @vport: virtual port to check the capability for
 *
 * Return: true if it's supported by the HW/FW, false if not.
 */
static bool idpf_is_hsplit_supported(const struct idpf_vport *vport)
{
	return idpf_is_queue_model_split(vport->rxq_model) &&
	       idpf_is_cap_ena_all(vport->adapter, IDPF_HSPLIT_CAPS,
				   IDPF_CAP_HSPLIT);
}

/**
 * idpf_vport_get_hsplit - get the current header split feature state
 * @vport: virtual port to query the state for
 *
 * Return: ``ETHTOOL_TCP_DATA_SPLIT_UNKNOWN`` if not supported,
 *         ``ETHTOOL_TCP_DATA_SPLIT_DISABLED`` if disabled,
 *         ``ETHTOOL_TCP_DATA_SPLIT_ENABLED`` if active.
 */
u8 idpf_vport_get_hsplit(const struct idpf_vport *vport)
{
	const struct idpf_vport_user_config_data *config;

	if (!idpf_is_hsplit_supported(vport))
		return ETHTOOL_TCP_DATA_SPLIT_UNKNOWN;

	config = &vport->adapter->vport_config[vport->idx]->user_config;

	return test_bit(__IDPF_USER_FLAG_HSPLIT, config->user_flags) ?
	       ETHTOOL_TCP_DATA_SPLIT_ENABLED :
	       ETHTOOL_TCP_DATA_SPLIT_DISABLED;
}

/**
 * idpf_vport_set_hsplit - enable or disable header split on a given vport
 * @vport: virtual port to configure
 * @val: Ethtool flag controlling the header split state
 *
 * Return: true on success, false if not supported by the HW.
 */
bool idpf_vport_set_hsplit(const struct idpf_vport *vport, u8 val)
{
	struct idpf_vport_user_config_data *config;

	if (!idpf_is_hsplit_supported(vport))
		return val == ETHTOOL_TCP_DATA_SPLIT_UNKNOWN;

	config = &vport->adapter->vport_config[vport->idx]->user_config;

	switch (val) {
	case ETHTOOL_TCP_DATA_SPLIT_UNKNOWN:
		/* Default is to enable */
	case ETHTOOL_TCP_DATA_SPLIT_ENABLED:
		__set_bit(__IDPF_USER_FLAG_HSPLIT, config->user_flags);
		return true;
	case ETHTOOL_TCP_DATA_SPLIT_DISABLED:
		__clear_bit(__IDPF_USER_FLAG_HSPLIT, config->user_flags);
		return true;
	default:
		return false;
	}
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
	u16 num_max_q;

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
	vport->compln_clean_budget = IDPF_TX_COMPLQ_CLEAN_BUDGET;
	vport->default_vport = adapter->num_alloc_vports <
			       idpf_get_default_vports(adapter);

	num_max_q = max(max_q->max_txq, max_q->max_rxq);
	vport->q_vector_idxs = kcalloc(num_max_q, sizeof(u16), GFP_KERNEL);
	if (!vport->q_vector_idxs)
		goto free_vport;

	idpf_vport_init(vport, max_q);

	/* This alloc is done separate from the LUT because it's not strictly
	 * dependent on how many queues we have. If we change number of queues
	 * and soft reset we'll need a new LUT but the key can remain the same
	 * for as long as the vport exists.
	 */
	rss_data = &adapter->vport_config[idx]->user_config.rss_data;
	rss_data->rss_key = kzalloc(rss_data->rss_key_size, GFP_KERNEL);
	if (!rss_data->rss_key)
		goto free_vector_idxs;

	/* Initialize default rss key */
	netdev_rss_key_fill((void *)rss_data->rss_key, rss_data->rss_key_size);

	/* fill vport slot in the adapter struct */
	adapter->vports[idx] = vport;
	adapter->vport_ids[idx] = idpf_get_vport_id(vport);

	adapter->num_alloc_vports++;
	/* prepare adapter->next_vport for next use */
	adapter->next_vport = idpf_get_free_slot(adapter);

	return vport;

free_vector_idxs:
	kfree(vport->q_vector_idxs);
free_vport:
	kfree(vport);

	return NULL;
}

/**
 * idpf_get_stats64 - get statistics for network device structure
 * @netdev: network interface device structure
 * @stats: main device statistics structure
 */
static void idpf_get_stats64(struct net_device *netdev,
			     struct rtnl_link_stats64 *stats)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);

	spin_lock_bh(&np->stats_lock);
	*stats = np->netstats;
	spin_unlock_bh(&np->stats_lock);
}

/**
 * idpf_statistics_task - Delayed task to get statistics over mailbox
 * @work: work_struct handle to our data
 */
void idpf_statistics_task(struct work_struct *work)
{
	struct idpf_adapter *adapter;
	int i;

	adapter = container_of(work, struct idpf_adapter, stats_task.work);

	for (i = 0; i < adapter->max_vports; i++) {
		struct idpf_vport *vport = adapter->vports[i];

		if (vport && !test_bit(IDPF_HR_RESET_IN_PROG, adapter->flags))
			idpf_send_get_stats_msg(vport);
	}

	queue_delayed_work(adapter->stats_wq, &adapter->stats_task,
			   msecs_to_jiffies(10000));
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

	idpf_recv_mb_msg(adapter);
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
 * idpf_restore_features - Restore feature configs
 * @vport: virtual port structure
 */
static void idpf_restore_features(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;

	if (idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_MACFILTER))
		idpf_restore_mac_filters(vport);
}

/**
 * idpf_set_real_num_queues - set number of queues for netdev
 * @vport: virtual port structure
 *
 * Returns 0 on success, negative on failure.
 */
static int idpf_set_real_num_queues(struct idpf_vport *vport)
{
	int err;

	err = netif_set_real_num_rx_queues(vport->netdev, vport->num_rxq);
	if (err)
		return err;

	return netif_set_real_num_tx_queues(vport->netdev, vport->num_txq);
}

/**
 * idpf_up_complete - Complete interface up sequence
 * @vport: virtual port structure
 *
 * Returns 0 on success, negative on failure.
 */
static int idpf_up_complete(struct idpf_vport *vport)
{
	struct idpf_netdev_priv *np = netdev_priv(vport->netdev);

	if (vport->link_up && !netif_carrier_ok(vport->netdev)) {
		netif_carrier_on(vport->netdev);
		netif_tx_start_all_queues(vport->netdev);
	}

	np->state = __IDPF_VPORT_UP;

	return 0;
}

/**
 * idpf_rx_init_buf_tail - Write initial buffer ring tail value
 * @vport: virtual port struct
 */
static void idpf_rx_init_buf_tail(struct idpf_vport *vport)
{
	int i, j;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct idpf_rxq_group *grp = &vport->rxq_grps[i];

		if (idpf_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				const struct idpf_buf_queue *q =
					&grp->splitq.bufq_sets[j].bufq;

				writel(q->next_to_alloc, q->tail);
			}
		} else {
			for (j = 0; j < grp->singleq.num_rxq; j++) {
				const struct idpf_rx_queue *q =
					grp->singleq.rxqs[j];

				writel(q->next_to_alloc, q->tail);
			}
		}
	}
}

/**
 * idpf_vport_open - Bring up a vport
 * @vport: vport to bring up
 */
static int idpf_vport_open(struct idpf_vport *vport)
{
	struct idpf_netdev_priv *np = netdev_priv(vport->netdev);
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_vport_config *vport_config;
	int err;

	if (np->state != __IDPF_VPORT_DOWN)
		return -EBUSY;

	/* we do not allow interface up just yet */
	netif_carrier_off(vport->netdev);

	err = idpf_vport_intr_alloc(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to allocate interrupts for vport %u: %d\n",
			vport->vport_id, err);
		return err;
	}

	err = idpf_vport_queues_alloc(vport);
	if (err)
		goto intr_rel;

	err = idpf_vport_queue_ids_init(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize queue ids for vport %u: %d\n",
			vport->vport_id, err);
		goto queues_rel;
	}

	err = idpf_vport_intr_init(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize interrupts for vport %u: %d\n",
			vport->vport_id, err);
		goto queues_rel;
	}

	err = idpf_rx_bufs_init_all(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize RX buffers for vport %u: %d\n",
			vport->vport_id, err);
		goto queues_rel;
	}

	err = idpf_queue_reg_init(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize queue registers for vport %u: %d\n",
			vport->vport_id, err);
		goto queues_rel;
	}

	idpf_rx_init_buf_tail(vport);
	idpf_vport_intr_ena(vport);

	err = idpf_send_config_queues_msg(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to configure queues for vport %u, %d\n",
			vport->vport_id, err);
		goto intr_deinit;
	}

	err = idpf_send_map_unmap_queue_vector_msg(vport, true);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to map queue vectors for vport %u: %d\n",
			vport->vport_id, err);
		goto intr_deinit;
	}

	err = idpf_send_enable_queues_msg(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to enable queues for vport %u: %d\n",
			vport->vport_id, err);
		goto unmap_queue_vectors;
	}

	err = idpf_send_enable_vport_msg(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to enable vport %u: %d\n",
			vport->vport_id, err);
		err = -EAGAIN;
		goto disable_queues;
	}

	idpf_restore_features(vport);

	vport_config = adapter->vport_config[vport->idx];
	if (vport_config->user_config.rss_data.rss_lut)
		err = idpf_config_rss(vport);
	else
		err = idpf_init_rss(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize RSS for vport %u: %d\n",
			vport->vport_id, err);
		goto disable_vport;
	}

	err = idpf_up_complete(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to complete interface up for vport %u: %d\n",
			vport->vport_id, err);
		goto deinit_rss;
	}

	return 0;

deinit_rss:
	idpf_deinit_rss(vport);
disable_vport:
	idpf_send_disable_vport_msg(vport);
disable_queues:
	idpf_send_disable_queues_msg(vport);
unmap_queue_vectors:
	idpf_send_map_unmap_queue_vector_msg(vport, false);
intr_deinit:
	idpf_vport_intr_deinit(vport);
queues_rel:
	idpf_vport_queues_rel(vport);
intr_rel:
	idpf_vport_intr_rel(vport);

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

	init_waitqueue_head(&vport->sw_marker_wq);

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
		idpf_vport_open(vport);

	/* Spawn and return 'idpf_init_task' work queue until all the
	 * default vports are created
	 */
	if (adapter->num_alloc_vports < num_default_vports) {
		queue_delayed_work(adapter->init_wq, &adapter->init_task,
				   msecs_to_jiffies(5 * (adapter->pdev->devfn & 0x07)));

		return;
	}

	for (index = 0; index < adapter->max_vports; index++) {
		struct net_device *netdev = adapter->netdevs[index];
		struct idpf_vport_config *vport_config;

		vport_config = adapter->vport_config[index];

		if (!netdev ||
		    test_bit(IDPF_VPORT_REG_NETDEV, vport_config->flags))
			continue;

		err = register_netdev(netdev);
		if (err) {
			dev_err(&pdev->dev, "failed to register netdev for vport %d: %pe\n",
				index, ERR_PTR(err));
			continue;
		}
		set_bit(IDPF_VPORT_REG_NETDEV, vport_config->flags);
	}

	/* As all the required vports are created, clear the reset flag
	 * unconditionally here in case we were in reset and the link was down.
	 */
	clear_bit(IDPF_HR_RESET_IN_PROG, adapter->flags);
	/* Start the statistics task now */
	queue_delayed_work(adapter->stats_wq, &adapter->stats_task,
			   msecs_to_jiffies(10 * (pdev->devfn & 0x07)));

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
 * idpf_sriov_ena - Enable or change number of VFs
 * @adapter: private data struct
 * @num_vfs: number of VFs to allocate
 */
static int idpf_sriov_ena(struct idpf_adapter *adapter, int num_vfs)
{
	struct device *dev = &adapter->pdev->dev;
	int err;

	err = idpf_send_set_sriov_vfs_msg(adapter, num_vfs);
	if (err) {
		dev_err(dev, "Failed to allocate VFs: %d\n", err);

		return err;
	}

	err = pci_enable_sriov(adapter->pdev, num_vfs);
	if (err) {
		idpf_send_set_sriov_vfs_msg(adapter, 0);
		dev_err(dev, "Failed to enable SR-IOV: %d\n", err);

		return err;
	}

	adapter->num_vfs = num_vfs;

	return num_vfs;
}

/**
 * idpf_sriov_configure - Configure the requested VFs
 * @pdev: pointer to a pci_dev structure
 * @num_vfs: number of vfs to allocate
 *
 * Enable or change the number of VFs. Called when the user updates the number
 * of VFs in sysfs.
 **/
int idpf_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct idpf_adapter *adapter = pci_get_drvdata(pdev);

	if (!idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_SRIOV)) {
		dev_info(&pdev->dev, "SR-IOV is not supported on this device\n");

		return -EOPNOTSUPP;
	}

	if (num_vfs)
		return idpf_sriov_ena(adapter, num_vfs);

	if (pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev, "Unable to free VFs because some are assigned to VMs\n");

		return -EBUSY;
	}

	pci_disable_sriov(adapter->pdev);
	idpf_send_set_sriov_vfs_msg(adapter, 0);
	adapter->num_vfs = 0;

	return 0;
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

	cancel_delayed_work_sync(&adapter->stats_task);

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

	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);

	/* Initialize the state machine, also allocate memory and request
	 * resources
	 */
	err = idpf_vc_core_init(adapter);
	if (err) {
		cancel_delayed_work_sync(&adapter->mbx_task);
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

	if (test_bit(IDPF_HR_FUNC_RESET, adapter->flags))
		goto func_reset;

	if (test_bit(IDPF_HR_DRV_LOAD, adapter->flags))
		goto drv_load;

	return;

func_reset:
	idpf_vc_xn_shutdown(adapter->vcxn_mngr);
drv_load:
	set_bit(IDPF_HR_RESET_IN_PROG, adapter->flags);
	idpf_init_hard_reset(adapter);
}

/**
 * idpf_initiate_soft_reset - Initiate a software reset
 * @vport: virtual port data struct
 * @reset_cause: reason for the soft reset
 *
 * Soft reset only reallocs vport queue resources. Returns 0 on success,
 * negative on failure.
 */
int idpf_initiate_soft_reset(struct idpf_vport *vport,
			     enum idpf_vport_reset_cause reset_cause)
{
	struct idpf_netdev_priv *np = netdev_priv(vport->netdev);
	enum idpf_vport_state current_state = np->state;
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_vport *new_vport;
	int err;

	/* If the system is low on memory, we can end up in bad state if we
	 * free all the memory for queue resources and try to allocate them
	 * again. Instead, we can pre-allocate the new resources before doing
	 * anything and bailing if the alloc fails.
	 *
	 * Make a clone of the existing vport to mimic its current
	 * configuration, then modify the new structure with any requested
	 * changes. Once the allocation of the new resources is done, stop the
	 * existing vport and copy the configuration to the main vport. If an
	 * error occurred, the existing vport will be untouched.
	 *
	 */
	new_vport = kzalloc(sizeof(*vport), GFP_KERNEL);
	if (!new_vport)
		return -ENOMEM;

	/* This purposely avoids copying the end of the struct because it
	 * contains wait_queues and mutexes and other stuff we don't want to
	 * mess with. Nothing below should use those variables from new_vport
	 * and should instead always refer to them in vport if they need to.
	 */
	memcpy(new_vport, vport, offsetof(struct idpf_vport, link_up));

	/* Adjust resource parameters prior to reallocating resources */
	switch (reset_cause) {
	case IDPF_SR_Q_CHANGE:
		err = idpf_vport_adjust_qs(new_vport);
		if (err)
			goto free_vport;
		break;
	case IDPF_SR_Q_DESC_CHANGE:
		/* Update queue parameters before allocating resources */
		idpf_vport_calc_num_q_desc(new_vport);
		break;
	case IDPF_SR_MTU_CHANGE:
	case IDPF_SR_RSC_CHANGE:
		break;
	default:
		dev_err(&adapter->pdev->dev, "Unhandled soft reset cause\n");
		err = -EINVAL;
		goto free_vport;
	}

	if (current_state <= __IDPF_VPORT_DOWN) {
		idpf_send_delete_queues_msg(vport);
	} else {
		set_bit(IDPF_VPORT_DEL_QUEUES, vport->flags);
		idpf_vport_stop(vport);
	}

	idpf_deinit_rss(vport);
	/* We're passing in vport here because we need its wait_queue
	 * to send a message and it should be getting all the vport
	 * config data out of the adapter but we need to be careful not
	 * to add code to add_queues to change the vport config within
	 * vport itself as it will be wiped with a memcpy later.
	 */
	err = idpf_send_add_queues_msg(vport, new_vport->num_txq,
				       new_vport->num_complq,
				       new_vport->num_rxq,
				       new_vport->num_bufq);
	if (err)
		goto err_reset;

	/* Same comment as above regarding avoiding copying the wait_queues and
	 * mutexes applies here. We do not want to mess with those if possible.
	 */
	memcpy(vport, new_vport, offsetof(struct idpf_vport, link_up));

	if (reset_cause == IDPF_SR_Q_CHANGE)
		idpf_vport_alloc_vec_indexes(vport);

	err = idpf_set_real_num_queues(vport);
	if (err)
		goto err_open;

	if (current_state == __IDPF_VPORT_UP)
		err = idpf_vport_open(vport);

	kfree(new_vport);

	return err;

err_reset:
	idpf_send_add_queues_msg(vport, vport->num_txq, vport->num_complq,
				 vport->num_rxq, vport->num_bufq);

err_open:
	if (current_state == __IDPF_VPORT_UP)
		idpf_vport_open(vport);

free_vport:
	kfree(new_vport);

	return err;
}

/**
 * idpf_addr_sync - Callback for dev_(mc|uc)_sync to add address
 * @netdev: the netdevice
 * @addr: address to add
 *
 * Called by __dev_(mc|uc)_sync when an address needs to be added. We call
 * __dev_(uc|mc)_sync from .set_rx_mode. Kernel takes addr_list_lock spinlock
 * meaning we cannot sleep in this context. Due to this, we have to add the
 * filter and send the virtchnl message asynchronously without waiting for the
 * response from the other side. We won't know whether or not the operation
 * actually succeeded until we get the message back.  Returns 0 on success,
 * negative on failure.
 */
static int idpf_addr_sync(struct net_device *netdev, const u8 *addr)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);

	return idpf_add_mac_filter(np->vport, np, addr, true);
}

/**
 * idpf_addr_unsync - Callback for dev_(mc|uc)_sync to remove address
 * @netdev: the netdevice
 * @addr: address to add
 *
 * Called by __dev_(mc|uc)_sync when an address needs to be added. We call
 * __dev_(uc|mc)_sync from .set_rx_mode. Kernel takes addr_list_lock spinlock
 * meaning we cannot sleep in this context. Due to this we have to delete the
 * filter and send the virtchnl message asynchronously without waiting for the
 * return from the other side.  We won't know whether or not the operation
 * actually succeeded until we get the message back. Returns 0 on success,
 * negative on failure.
 */
static int idpf_addr_unsync(struct net_device *netdev, const u8 *addr)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);

	/* Under some circumstances, we might receive a request to delete
	 * our own device address from our uc list. Because we store the
	 * device address in the VSI's MAC filter list, we need to ignore
	 * such requests and not delete our device address from this list.
	 */
	if (ether_addr_equal(addr, netdev->dev_addr))
		return 0;

	idpf_del_mac_filter(np->vport, np, addr, true);

	return 0;
}

/**
 * idpf_set_rx_mode - NDO callback to set the netdev filters
 * @netdev: network interface device structure
 *
 * Stack takes addr_list_lock spinlock before calling our .set_rx_mode.  We
 * cannot sleep in this context.
 */
static void idpf_set_rx_mode(struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_user_config_data *config_data;
	struct idpf_adapter *adapter;
	bool changed = false;
	struct device *dev;
	int err;

	adapter = np->adapter;
	dev = &adapter->pdev->dev;

	if (idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_MACFILTER)) {
		__dev_uc_sync(netdev, idpf_addr_sync, idpf_addr_unsync);
		__dev_mc_sync(netdev, idpf_addr_sync, idpf_addr_unsync);
	}

	if (!idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_PROMISC))
		return;

	config_data = &adapter->vport_config[np->vport_idx]->user_config;
	/* IFF_PROMISC enables both unicast and multicast promiscuous,
	 * while IFF_ALLMULTI only enables multicast such that:
	 *
	 * promisc  + allmulti		= unicast | multicast
	 * promisc  + !allmulti		= unicast | multicast
	 * !promisc + allmulti		= multicast
	 */
	if ((netdev->flags & IFF_PROMISC) &&
	    !test_and_set_bit(__IDPF_PROMISC_UC, config_data->user_flags)) {
		changed = true;
		dev_info(&adapter->pdev->dev, "Entering promiscuous mode\n");
		if (!test_and_set_bit(__IDPF_PROMISC_MC, adapter->flags))
			dev_info(dev, "Entering multicast promiscuous mode\n");
	}

	if (!(netdev->flags & IFF_PROMISC) &&
	    test_and_clear_bit(__IDPF_PROMISC_UC, config_data->user_flags)) {
		changed = true;
		dev_info(dev, "Leaving promiscuous mode\n");
	}

	if (netdev->flags & IFF_ALLMULTI &&
	    !test_and_set_bit(__IDPF_PROMISC_MC, config_data->user_flags)) {
		changed = true;
		dev_info(dev, "Entering multicast promiscuous mode\n");
	}

	if (!(netdev->flags & (IFF_ALLMULTI | IFF_PROMISC)) &&
	    test_and_clear_bit(__IDPF_PROMISC_MC, config_data->user_flags)) {
		changed = true;
		dev_info(dev, "Leaving multicast promiscuous mode\n");
	}

	if (!changed)
		return;

	err = idpf_set_promiscuous(adapter, config_data, np->vport_id);
	if (err)
		dev_err(dev, "Failed to set promiscuous mode: %d\n", err);
}

/**
 * idpf_vport_manage_rss_lut - disable/enable RSS
 * @vport: the vport being changed
 *
 * In the event of disable request for RSS, this function will zero out RSS
 * LUT, while in the event of enable request for RSS, it will reconfigure RSS
 * LUT with the default LUT configuration.
 */
static int idpf_vport_manage_rss_lut(struct idpf_vport *vport)
{
	bool ena = idpf_is_feature_ena(vport, NETIF_F_RXHASH);
	struct idpf_rss_data *rss_data;
	u16 idx = vport->idx;
	int lut_size;

	rss_data = &vport->adapter->vport_config[idx]->user_config.rss_data;
	lut_size = rss_data->rss_lut_size * sizeof(u32);

	if (ena) {
		/* This will contain the default or user configured LUT */
		memcpy(rss_data->rss_lut, rss_data->cached_lut, lut_size);
	} else {
		/* Save a copy of the current LUT to be restored later if
		 * requested.
		 */
		memcpy(rss_data->cached_lut, rss_data->rss_lut, lut_size);

		/* Zero out the current LUT to disable */
		memset(rss_data->rss_lut, 0, lut_size);
	}

	return idpf_config_rss(vport);
}

/**
 * idpf_set_features - set the netdev feature flags
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 */
static int idpf_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct idpf_adapter *adapter;
	struct idpf_vport *vport;
	int err = 0;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	adapter = vport->adapter;

	if (idpf_is_reset_in_prog(adapter)) {
		dev_err(&adapter->pdev->dev, "Device is resetting, changing netdev features temporarily unavailable.\n");
		err = -EBUSY;
		goto unlock_mutex;
	}

	if (changed & NETIF_F_RXHASH) {
		netdev->features ^= NETIF_F_RXHASH;
		err = idpf_vport_manage_rss_lut(vport);
		if (err)
			goto unlock_mutex;
	}

	if (changed & NETIF_F_GRO_HW) {
		netdev->features ^= NETIF_F_GRO_HW;
		err = idpf_initiate_soft_reset(vport, IDPF_SR_RSC_CHANGE);
		if (err)
			goto unlock_mutex;
	}

	if (changed & NETIF_F_LOOPBACK) {
		netdev->features ^= NETIF_F_LOOPBACK;
		err = idpf_send_ena_dis_loopback_msg(vport);
	}

unlock_mutex:
	idpf_vport_ctrl_unlock(netdev);

	return err;
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

	err = idpf_set_real_num_queues(vport);
	if (err)
		goto unlock;

	err = idpf_vport_open(vport);

unlock:
	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_change_mtu - NDO callback to change the MTU
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct idpf_vport *vport;
	int err;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	WRITE_ONCE(netdev->mtu, new_mtu);

	err = idpf_initiate_soft_reset(vport, IDPF_SR_MTU_CHANGE);

	idpf_vport_ctrl_unlock(netdev);

	return err;
}

/**
 * idpf_features_check - Validate packet conforms to limits
 * @skb: skb buffer
 * @netdev: This port's netdev
 * @features: Offload features that the stack believes apply
 */
static netdev_features_t idpf_features_check(struct sk_buff *skb,
					     struct net_device *netdev,
					     netdev_features_t features)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	u16 max_tx_hdr_size = np->max_tx_hdr_size;
	size_t len;

	/* No point in doing any of this if neither checksum nor GSO are
	 * being requested for this frame.  We can rule out both by just
	 * checking for CHECKSUM_PARTIAL
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return features;

	/* We cannot support GSO if the MSS is going to be less than
	 * 88 bytes. If it is then we need to drop support for GSO.
	 */
	if (skb_is_gso(skb) &&
	    (skb_shinfo(skb)->gso_size < IDPF_TX_TSO_MIN_MSS))
		features &= ~NETIF_F_GSO_MASK;

	/* Ensure MACLEN is <= 126 bytes (63 words) and not an odd size */
	len = skb_network_offset(skb);
	if (unlikely(len & ~(126)))
		goto unsupported;

	len = skb_network_header_len(skb);
	if (unlikely(len > max_tx_hdr_size))
		goto unsupported;

	if (!skb->encapsulation)
		return features;

	/* L4TUNLEN can support 127 words */
	len = skb_inner_network_header(skb) - skb_transport_header(skb);
	if (unlikely(len & ~(127 * 2)))
		goto unsupported;

	/* IPLEN can support at most 127 dwords */
	len = skb_inner_network_header_len(skb);
	if (unlikely(len > max_tx_hdr_size))
		goto unsupported;

	/* No need to validate L4LEN as TCP is the only protocol with a
	 * a flexible value and we support all possible values supported
	 * by TCP, which is at most 15 dwords
	 */

	return features;

unsupported:
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

/**
 * idpf_set_mac - NDO callback to set port mac address
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int idpf_set_mac(struct net_device *netdev, void *p)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);
	struct idpf_vport_config *vport_config;
	struct sockaddr *addr = p;
	struct idpf_vport *vport;
	int err = 0;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	if (!idpf_is_cap_ena(vport->adapter, IDPF_OTHER_CAPS,
			     VIRTCHNL2_CAP_MACFILTER)) {
		dev_info(&vport->adapter->pdev->dev, "Setting MAC address is not supported\n");
		err = -EOPNOTSUPP;
		goto unlock_mutex;
	}

	if (!is_valid_ether_addr(addr->sa_data)) {
		dev_info(&vport->adapter->pdev->dev, "Invalid MAC address: %pM\n",
			 addr->sa_data);
		err = -EADDRNOTAVAIL;
		goto unlock_mutex;
	}

	if (ether_addr_equal(netdev->dev_addr, addr->sa_data))
		goto unlock_mutex;

	vport_config = vport->adapter->vport_config[vport->idx];
	err = idpf_add_mac_filter(vport, np, addr->sa_data, false);
	if (err) {
		__idpf_del_mac_filter(vport_config, addr->sa_data);
		goto unlock_mutex;
	}

	if (is_valid_ether_addr(vport->default_mac_addr))
		idpf_del_mac_filter(vport, np, vport->default_mac_addr, false);

	ether_addr_copy(vport->default_mac_addr, addr->sa_data);
	eth_hw_addr_set(netdev, addr->sa_data);

unlock_mutex:
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

	/* The control queue resources are freed under a spinlock, contiguous
	 * pages will avoid IOMMU remapping and the use vmap (and vunmap in
	 * dma_free_*() path.
	 */
	mem->va = dma_alloc_attrs(&adapter->pdev->dev, sz, &mem->pa,
				  GFP_KERNEL, DMA_ATTR_FORCE_CONTIGUOUS);
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

	dma_free_attrs(&adapter->pdev->dev, mem->size,
		       mem->va, mem->pa, DMA_ATTR_FORCE_CONTIGUOUS);
	mem->size = 0;
	mem->va = NULL;
	mem->pa = 0;
}

static int idpf_hwtstamp_set(struct net_device *netdev,
			     struct kernel_hwtstamp_config *config,
			     struct netlink_ext_ack *extack)
{
	struct idpf_vport *vport;
	int err;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	if (!vport->link_up) {
		idpf_vport_ctrl_unlock(netdev);
		return -EPERM;
	}

	if (!idpf_ptp_is_vport_tx_tstamp_ena(vport) &&
	    !idpf_ptp_is_vport_rx_tstamp_ena(vport)) {
		idpf_vport_ctrl_unlock(netdev);
		return -EOPNOTSUPP;
	}

	err = idpf_ptp_set_timestamp_mode(vport, config);

	idpf_vport_ctrl_unlock(netdev);

	return err;
}

static int idpf_hwtstamp_get(struct net_device *netdev,
			     struct kernel_hwtstamp_config *config)
{
	struct idpf_vport *vport;

	idpf_vport_ctrl_lock(netdev);
	vport = idpf_netdev_to_vport(netdev);

	if (!vport->link_up) {
		idpf_vport_ctrl_unlock(netdev);
		return -EPERM;
	}

	if (!idpf_ptp_is_vport_tx_tstamp_ena(vport) &&
	    !idpf_ptp_is_vport_rx_tstamp_ena(vport)) {
		idpf_vport_ctrl_unlock(netdev);
		return 0;
	}

	*config = vport->tstamp_config;

	idpf_vport_ctrl_unlock(netdev);

	return 0;
}

static const struct net_device_ops idpf_netdev_ops = {
	.ndo_open = idpf_open,
	.ndo_stop = idpf_stop,
	.ndo_start_xmit = idpf_tx_start,
	.ndo_features_check = idpf_features_check,
	.ndo_set_rx_mode = idpf_set_rx_mode,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = idpf_set_mac,
	.ndo_change_mtu = idpf_change_mtu,
	.ndo_get_stats64 = idpf_get_stats64,
	.ndo_set_features = idpf_set_features,
	.ndo_tx_timeout = idpf_tx_timeout,
	.ndo_hwtstamp_get = idpf_hwtstamp_get,
	.ndo_hwtstamp_set = idpf_hwtstamp_set,
};
