// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2015 - 2021 Intel Corporation */
#include "main.h"

/**
 * irdma_arp_table -manage arp table
 * @rf: RDMA PCI function
 * @ip_addr: ip address for device
 * @ipv4: IPv4 flag
 * @mac_addr: mac address ptr
 * @action: modify, delete or add
 */
int irdma_arp_table(struct irdma_pci_f *rf, u32 *ip_addr, bool ipv4,
		    const u8 *mac_addr, u32 action)
{
	unsigned long flags;
	int arp_index;
	u32 ip[4] = {};

	if (ipv4)
		ip[0] = *ip_addr;
	else
		memcpy(ip, ip_addr, sizeof(ip));

	spin_lock_irqsave(&rf->arp_lock, flags);
	for (arp_index = 0; (u32)arp_index < rf->arp_table_size; arp_index++) {
		if (!memcmp(rf->arp_table[arp_index].ip_addr, ip, sizeof(ip)))
			break;
	}

	switch (action) {
	case IRDMA_ARP_ADD:
		if (arp_index != rf->arp_table_size) {
			arp_index = -1;
			break;
		}

		arp_index = 0;
		if (irdma_alloc_rsrc(rf, rf->allocated_arps, rf->arp_table_size,
				     (u32 *)&arp_index, &rf->next_arp_index)) {
			arp_index = -1;
			break;
		}

		memcpy(rf->arp_table[arp_index].ip_addr, ip,
		       sizeof(rf->arp_table[arp_index].ip_addr));
		ether_addr_copy(rf->arp_table[arp_index].mac_addr, mac_addr);
		break;
	case IRDMA_ARP_RESOLVE:
		if (arp_index == rf->arp_table_size)
			arp_index = -1;
		break;
	case IRDMA_ARP_DELETE:
		if (arp_index == rf->arp_table_size) {
			arp_index = -1;
			break;
		}

		memset(rf->arp_table[arp_index].ip_addr, 0,
		       sizeof(rf->arp_table[arp_index].ip_addr));
		eth_zero_addr(rf->arp_table[arp_index].mac_addr);
		irdma_free_rsrc(rf, rf->allocated_arps, arp_index);
		break;
	default:
		arp_index = -1;
		break;
	}

	spin_unlock_irqrestore(&rf->arp_lock, flags);
	return arp_index;
}

/**
 * irdma_add_arp - add a new arp entry if needed
 * @rf: RDMA function
 * @ip: IP address
 * @ipv4: IPv4 flag
 * @mac: MAC address
 */
int irdma_add_arp(struct irdma_pci_f *rf, u32 *ip, bool ipv4, const u8 *mac)
{
	int arpidx;

	arpidx = irdma_arp_table(rf, &ip[0], ipv4, NULL, IRDMA_ARP_RESOLVE);
	if (arpidx >= 0) {
		if (ether_addr_equal(rf->arp_table[arpidx].mac_addr, mac))
			return arpidx;

		irdma_manage_arp_cache(rf, rf->arp_table[arpidx].mac_addr, ip,
				       ipv4, IRDMA_ARP_DELETE);
	}

	irdma_manage_arp_cache(rf, mac, ip, ipv4, IRDMA_ARP_ADD);

	return irdma_arp_table(rf, ip, ipv4, NULL, IRDMA_ARP_RESOLVE);
}

/**
 * wr32 - write 32 bits to hw register
 * @hw: hardware information including registers
 * @reg: register offset
 * @val: value to write to register
 */
inline void wr32(struct irdma_hw *hw, u32 reg, u32 val)
{
	writel(val, hw->hw_addr + reg);
}

/**
 * rd32 - read a 32 bit hw register
 * @hw: hardware information including registers
 * @reg: register offset
 *
 * Return value of register content
 */
inline u32 rd32(struct irdma_hw *hw, u32 reg)
{
	return readl(hw->hw_addr + reg);
}

/**
 * rd64 - read a 64 bit hw register
 * @hw: hardware information including registers
 * @reg: register offset
 *
 * Return value of register content
 */
inline u64 rd64(struct irdma_hw *hw, u32 reg)
{
	return readq(hw->hw_addr + reg);
}

static void irdma_gid_change_event(struct ib_device *ibdev)
{
	struct ib_event ib_event;

	ib_event.event = IB_EVENT_GID_CHANGE;
	ib_event.device = ibdev;
	ib_event.element.port_num = 1;
	ib_dispatch_event(&ib_event);
}

/**
 * irdma_inetaddr_event - system notifier for ipv4 addr events
 * @notifier: not used
 * @event: event for notifier
 * @ptr: if address
 */
int irdma_inetaddr_event(struct notifier_block *notifier, unsigned long event,
			 void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	struct net_device *real_dev, *netdev = ifa->ifa_dev->dev;
	struct irdma_device *iwdev;
	struct ib_device *ibdev;
	u32 local_ipaddr;

	real_dev = rdma_vlan_dev_real_dev(netdev);
	if (!real_dev)
		real_dev = netdev;

	ibdev = ib_device_get_by_netdev(real_dev, RDMA_DRIVER_IRDMA);
	if (!ibdev)
		return NOTIFY_DONE;

	iwdev = to_iwdev(ibdev);
	local_ipaddr = ntohl(ifa->ifa_address);
	ibdev_dbg(&iwdev->ibdev,
		  "DEV: netdev %p event %lu local_ip=%pI4 MAC=%pM\n", real_dev,
		  event, &local_ipaddr, real_dev->dev_addr);
	switch (event) {
	case NETDEV_DOWN:
		irdma_manage_arp_cache(iwdev->rf, real_dev->dev_addr,
				       &local_ipaddr, true, IRDMA_ARP_DELETE);
		irdma_if_notify(iwdev, real_dev, &local_ipaddr, true, false);
		irdma_gid_change_event(&iwdev->ibdev);
		break;
	case NETDEV_UP:
	case NETDEV_CHANGEADDR:
		irdma_add_arp(iwdev->rf, &local_ipaddr, true, real_dev->dev_addr);
		irdma_if_notify(iwdev, real_dev, &local_ipaddr, true, true);
		irdma_gid_change_event(&iwdev->ibdev);
		break;
	default:
		break;
	}

	ib_device_put(ibdev);

	return NOTIFY_DONE;
}

/**
 * irdma_inet6addr_event - system notifier for ipv6 addr events
 * @notifier: not used
 * @event: event for notifier
 * @ptr: if address
 */
int irdma_inet6addr_event(struct notifier_block *notifier, unsigned long event,
			  void *ptr)
{
	struct inet6_ifaddr *ifa = ptr;
	struct net_device *real_dev, *netdev = ifa->idev->dev;
	struct irdma_device *iwdev;
	struct ib_device *ibdev;
	u32 local_ipaddr6[4];

	real_dev = rdma_vlan_dev_real_dev(netdev);
	if (!real_dev)
		real_dev = netdev;

	ibdev = ib_device_get_by_netdev(real_dev, RDMA_DRIVER_IRDMA);
	if (!ibdev)
		return NOTIFY_DONE;

	iwdev = to_iwdev(ibdev);
	irdma_copy_ip_ntohl(local_ipaddr6, ifa->addr.in6_u.u6_addr32);
	ibdev_dbg(&iwdev->ibdev,
		  "DEV: netdev %p event %lu local_ip=%pI6 MAC=%pM\n", real_dev,
		  event, local_ipaddr6, real_dev->dev_addr);
	switch (event) {
	case NETDEV_DOWN:
		irdma_manage_arp_cache(iwdev->rf, real_dev->dev_addr,
				       local_ipaddr6, false, IRDMA_ARP_DELETE);
		irdma_if_notify(iwdev, real_dev, local_ipaddr6, false, false);
		irdma_gid_change_event(&iwdev->ibdev);
		break;
	case NETDEV_UP:
	case NETDEV_CHANGEADDR:
		irdma_add_arp(iwdev->rf, local_ipaddr6, false,
			      real_dev->dev_addr);
		irdma_if_notify(iwdev, real_dev, local_ipaddr6, false, true);
		irdma_gid_change_event(&iwdev->ibdev);
		break;
	default:
		break;
	}

	ib_device_put(ibdev);

	return NOTIFY_DONE;
}

/**
 * irdma_net_event - system notifier for net events
 * @notifier: not used
 * @event: event for notifier
 * @ptr: neighbor
 */
int irdma_net_event(struct notifier_block *notifier, unsigned long event,
		    void *ptr)
{
	struct neighbour *neigh = ptr;
	struct net_device *real_dev, *netdev = (struct net_device *)neigh->dev;
	struct irdma_device *iwdev;
	struct ib_device *ibdev;
	__be32 *p;
	u32 local_ipaddr[4] = {};
	bool ipv4 = true;

	real_dev = rdma_vlan_dev_real_dev(netdev);
	if (!real_dev)
		real_dev = netdev;

	ibdev = ib_device_get_by_netdev(real_dev, RDMA_DRIVER_IRDMA);
	if (!ibdev)
		return NOTIFY_DONE;

	iwdev = to_iwdev(ibdev);

	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		p = (__be32 *)neigh->primary_key;
		if (neigh->tbl->family == AF_INET6) {
			ipv4 = false;
			irdma_copy_ip_ntohl(local_ipaddr, p);
		} else {
			local_ipaddr[0] = ntohl(*p);
		}

		ibdev_dbg(&iwdev->ibdev,
			  "DEV: netdev %p state %d local_ip=%pI4 MAC=%pM\n",
			  iwdev->netdev, neigh->nud_state, local_ipaddr,
			  neigh->ha);

		if (neigh->nud_state & NUD_VALID)
			irdma_add_arp(iwdev->rf, local_ipaddr, ipv4, neigh->ha);

		else
			irdma_manage_arp_cache(iwdev->rf, neigh->ha,
					       local_ipaddr, ipv4,
					       IRDMA_ARP_DELETE);
		break;
	default:
		break;
	}

	ib_device_put(ibdev);

	return NOTIFY_DONE;
}

/**
 * irdma_netdevice_event - system notifier for netdev events
 * @notifier: not used
 * @event: event for notifier
 * @ptr: netdev
 */
int irdma_netdevice_event(struct notifier_block *notifier, unsigned long event,
			  void *ptr)
{
	struct irdma_device *iwdev;
	struct ib_device *ibdev;
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);

	ibdev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_IRDMA);
	if (!ibdev)
		return NOTIFY_DONE;

	iwdev = to_iwdev(ibdev);
	iwdev->iw_status = 1;
	switch (event) {
	case NETDEV_DOWN:
		iwdev->iw_status = 0;
		fallthrough;
	case NETDEV_UP:
		irdma_port_ibevent(iwdev);
		break;
	default:
		break;
	}
	ib_device_put(ibdev);

	return NOTIFY_DONE;
}

/**
 * irdma_add_ipv6_addr - add ipv6 address to the hw arp table
 * @iwdev: irdma device
 */
static void irdma_add_ipv6_addr(struct irdma_device *iwdev)
{
	struct net_device *ip_dev;
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifp, *tmp;
	u32 local_ipaddr6[4];

	rcu_read_lock();
	for_each_netdev_rcu (&init_net, ip_dev) {
		if (((rdma_vlan_dev_vlan_id(ip_dev) < 0xFFFF &&
		      rdma_vlan_dev_real_dev(ip_dev) == iwdev->netdev) ||
		      ip_dev == iwdev->netdev) &&
		      (READ_ONCE(ip_dev->flags) & IFF_UP)) {
			idev = __in6_dev_get(ip_dev);
			if (!idev) {
				ibdev_err(&iwdev->ibdev, "ipv6 inet device not found\n");
				break;
			}
			list_for_each_entry_safe (ifp, tmp, &idev->addr_list,
						  if_list) {
				ibdev_dbg(&iwdev->ibdev,
					  "INIT: IP=%pI6, vlan_id=%d, MAC=%pM\n",
					  &ifp->addr,
					  rdma_vlan_dev_vlan_id(ip_dev),
					  ip_dev->dev_addr);

				irdma_copy_ip_ntohl(local_ipaddr6,
						    ifp->addr.in6_u.u6_addr32);
				irdma_manage_arp_cache(iwdev->rf,
						       ip_dev->dev_addr,
						       local_ipaddr6, false,
						       IRDMA_ARP_ADD);
			}
		}
	}
	rcu_read_unlock();
}

/**
 * irdma_add_ipv4_addr - add ipv4 address to the hw arp table
 * @iwdev: irdma device
 */
static void irdma_add_ipv4_addr(struct irdma_device *iwdev)
{
	struct net_device *dev;
	struct in_device *idev;
	u32 ip_addr;

	rcu_read_lock();
	for_each_netdev_rcu (&init_net, dev) {
		if (((rdma_vlan_dev_vlan_id(dev) < 0xFFFF &&
		      rdma_vlan_dev_real_dev(dev) == iwdev->netdev) ||
		      dev == iwdev->netdev) && (READ_ONCE(dev->flags) & IFF_UP)) {
			const struct in_ifaddr *ifa;

			idev = __in_dev_get_rcu(dev);
			if (!idev)
				continue;

			in_dev_for_each_ifa_rcu(ifa, idev) {
				ibdev_dbg(&iwdev->ibdev, "CM: IP=%pI4, vlan_id=%d, MAC=%pM\n",
					  &ifa->ifa_address, rdma_vlan_dev_vlan_id(dev),
					  dev->dev_addr);

				ip_addr = ntohl(ifa->ifa_address);
				irdma_manage_arp_cache(iwdev->rf, dev->dev_addr,
						       &ip_addr, true,
						       IRDMA_ARP_ADD);
			}
		}
	}
	rcu_read_unlock();
}

/**
 * irdma_add_ip - add ip addresses
 * @iwdev: irdma device
 *
 * Add ipv4/ipv6 addresses to the arp cache
 */
void irdma_add_ip(struct irdma_device *iwdev)
{
	irdma_add_ipv4_addr(iwdev);
	irdma_add_ipv6_addr(iwdev);
}

/**
 * irdma_alloc_and_get_cqp_request - get cqp struct
 * @cqp: device cqp ptr
 * @wait: cqp to be used in wait mode
 */
struct irdma_cqp_request *irdma_alloc_and_get_cqp_request(struct irdma_cqp *cqp,
							  bool wait)
{
	struct irdma_cqp_request *cqp_request = NULL;
	unsigned long flags;

	spin_lock_irqsave(&cqp->req_lock, flags);
	if (!list_empty(&cqp->cqp_avail_reqs)) {
		cqp_request = list_first_entry(&cqp->cqp_avail_reqs,
					       struct irdma_cqp_request, list);
		list_del_init(&cqp_request->list);
	}
	spin_unlock_irqrestore(&cqp->req_lock, flags);
	if (!cqp_request) {
		cqp_request = kzalloc(sizeof(*cqp_request), GFP_ATOMIC);
		if (cqp_request) {
			cqp_request->dynamic = true;
			if (wait)
				init_waitqueue_head(&cqp_request->waitq);
		}
	}
	if (!cqp_request) {
		ibdev_dbg(to_ibdev(cqp->sc_cqp.dev), "ERR: CQP Request Fail: No Memory");
		return NULL;
	}

	cqp_request->waiting = wait;
	refcount_set(&cqp_request->refcnt, 1);
	memset(&cqp_request->compl_info, 0, sizeof(cqp_request->compl_info));

	return cqp_request;
}

/**
 * irdma_get_cqp_request - increase refcount for cqp_request
 * @cqp_request: pointer to cqp_request instance
 */
static inline void irdma_get_cqp_request(struct irdma_cqp_request *cqp_request)
{
	refcount_inc(&cqp_request->refcnt);
}

/**
 * irdma_free_cqp_request - free cqp request
 * @cqp: cqp ptr
 * @cqp_request: to be put back in cqp list
 */
void irdma_free_cqp_request(struct irdma_cqp *cqp,
			    struct irdma_cqp_request *cqp_request)
{
	unsigned long flags;

	if (cqp_request->dynamic) {
		kfree(cqp_request);
	} else {
		cqp_request->request_done = false;
		cqp_request->callback_fcn = NULL;
		cqp_request->waiting = false;

		spin_lock_irqsave(&cqp->req_lock, flags);
		list_add_tail(&cqp_request->list, &cqp->cqp_avail_reqs);
		spin_unlock_irqrestore(&cqp->req_lock, flags);
	}
	wake_up(&cqp->remove_wq);
}

/**
 * irdma_put_cqp_request - dec ref count and free if 0
 * @cqp: cqp ptr
 * @cqp_request: to be put back in cqp list
 */
void irdma_put_cqp_request(struct irdma_cqp *cqp,
			   struct irdma_cqp_request *cqp_request)
{
	if (refcount_dec_and_test(&cqp_request->refcnt))
		irdma_free_cqp_request(cqp, cqp_request);
}

/**
 * irdma_free_pending_cqp_request -free pending cqp request objs
 * @cqp: cqp ptr
 * @cqp_request: to be put back in cqp list
 */
static void
irdma_free_pending_cqp_request(struct irdma_cqp *cqp,
			       struct irdma_cqp_request *cqp_request)
{
	if (cqp_request->waiting) {
		cqp_request->compl_info.error = true;
		cqp_request->request_done = true;
		wake_up(&cqp_request->waitq);
	}
	wait_event_timeout(cqp->remove_wq,
			   refcount_read(&cqp_request->refcnt) == 1, 1000);
	irdma_put_cqp_request(cqp, cqp_request);
}

/**
 * irdma_cleanup_pending_cqp_op - clean-up cqp with no
 * completions
 * @rf: RDMA PCI function
 */
void irdma_cleanup_pending_cqp_op(struct irdma_pci_f *rf)
{
	struct irdma_sc_dev *dev = &rf->sc_dev;
	struct irdma_cqp *cqp = &rf->cqp;
	struct irdma_cqp_request *cqp_request = NULL;
	struct cqp_cmds_info *pcmdinfo = NULL;
	u32 i, pending_work, wqe_idx;

	pending_work = IRDMA_RING_USED_QUANTA(cqp->sc_cqp.sq_ring);
	wqe_idx = IRDMA_RING_CURRENT_TAIL(cqp->sc_cqp.sq_ring);
	for (i = 0; i < pending_work; i++) {
		cqp_request = (struct irdma_cqp_request *)(unsigned long)
				      cqp->scratch_array[wqe_idx];
		if (cqp_request)
			irdma_free_pending_cqp_request(cqp, cqp_request);
		wqe_idx = (wqe_idx + 1) % IRDMA_RING_SIZE(cqp->sc_cqp.sq_ring);
	}

	while (!list_empty(&dev->cqp_cmd_head)) {
		pcmdinfo = irdma_remove_cqp_head(dev);
		cqp_request =
			container_of(pcmdinfo, struct irdma_cqp_request, info);
		if (cqp_request)
			irdma_free_pending_cqp_request(cqp, cqp_request);
	}
}

/**
 * irdma_wait_event - wait for completion
 * @rf: RDMA PCI function
 * @cqp_request: cqp request to wait
 */
static int irdma_wait_event(struct irdma_pci_f *rf,
			    struct irdma_cqp_request *cqp_request)
{
	struct irdma_cqp_timeout cqp_timeout = {};
	bool cqp_error = false;
	int err_code = 0;

	cqp_timeout.compl_cqp_cmds = rf->sc_dev.cqp_cmd_stats[IRDMA_OP_CMPL_CMDS];
	do {
		irdma_cqp_ce_handler(rf, &rf->ccq.sc_cq);
		if (wait_event_timeout(cqp_request->waitq,
				       cqp_request->request_done,
				       msecs_to_jiffies(CQP_COMPL_WAIT_TIME_MS)))
			break;

		irdma_check_cqp_progress(&cqp_timeout, &rf->sc_dev);

		if (cqp_timeout.count < CQP_TIMEOUT_THRESHOLD)
			continue;

		if (!rf->reset) {
			rf->reset = true;
			rf->gen_ops.request_reset(rf);
		}
		return -ETIMEDOUT;
	} while (1);

	cqp_error = cqp_request->compl_info.error;
	if (cqp_error) {
		err_code = -EIO;
		if (cqp_request->compl_info.maj_err_code == 0xFFFF &&
		    cqp_request->compl_info.min_err_code == 0x8029) {
			if (!rf->reset) {
				rf->reset = true;
				rf->gen_ops.request_reset(rf);
			}
		}
	}

	return err_code;
}

static const char *const irdma_cqp_cmd_names[IRDMA_MAX_CQP_OPS] = {
	[IRDMA_OP_CEQ_DESTROY] = "Destroy CEQ Cmd",
	[IRDMA_OP_AEQ_DESTROY] = "Destroy AEQ Cmd",
	[IRDMA_OP_DELETE_ARP_CACHE_ENTRY] = "Delete ARP Cache Cmd",
	[IRDMA_OP_MANAGE_APBVT_ENTRY] = "Manage APBV Table Entry Cmd",
	[IRDMA_OP_CEQ_CREATE] = "CEQ Create Cmd",
	[IRDMA_OP_AEQ_CREATE] = "AEQ Destroy Cmd",
	[IRDMA_OP_MANAGE_QHASH_TABLE_ENTRY] = "Manage Quad Hash Table Entry Cmd",
	[IRDMA_OP_QP_MODIFY] = "Modify QP Cmd",
	[IRDMA_OP_QP_UPLOAD_CONTEXT] = "Upload Context Cmd",
	[IRDMA_OP_CQ_CREATE] = "Create CQ Cmd",
	[IRDMA_OP_CQ_DESTROY] = "Destroy CQ Cmd",
	[IRDMA_OP_QP_CREATE] = "Create QP Cmd",
	[IRDMA_OP_QP_DESTROY] = "Destroy QP Cmd",
	[IRDMA_OP_ALLOC_STAG] = "Allocate STag Cmd",
	[IRDMA_OP_MR_REG_NON_SHARED] = "Register Non-Shared MR Cmd",
	[IRDMA_OP_DEALLOC_STAG] = "Deallocate STag Cmd",
	[IRDMA_OP_MW_ALLOC] = "Allocate Memory Window Cmd",
	[IRDMA_OP_QP_FLUSH_WQES] = "Flush QP Cmd",
	[IRDMA_OP_ADD_ARP_CACHE_ENTRY] = "Add ARP Cache Cmd",
	[IRDMA_OP_MANAGE_PUSH_PAGE] = "Manage Push Page Cmd",
	[IRDMA_OP_UPDATE_PE_SDS] = "Update PE SDs Cmd",
	[IRDMA_OP_MANAGE_HMC_PM_FUNC_TABLE] = "Manage HMC PM Function Table Cmd",
	[IRDMA_OP_SUSPEND] = "Suspend QP Cmd",
	[IRDMA_OP_RESUME] = "Resume QP Cmd",
	[IRDMA_OP_MANAGE_VF_PBLE_BP] = "Manage VF PBLE Backing Pages Cmd",
	[IRDMA_OP_QUERY_FPM_VAL] = "Query FPM Values Cmd",
	[IRDMA_OP_COMMIT_FPM_VAL] = "Commit FPM Values Cmd",
	[IRDMA_OP_AH_CREATE] = "Create Address Handle Cmd",
	[IRDMA_OP_AH_MODIFY] = "Modify Address Handle Cmd",
	[IRDMA_OP_AH_DESTROY] = "Destroy Address Handle Cmd",
	[IRDMA_OP_MC_CREATE] = "Create Multicast Group Cmd",
	[IRDMA_OP_MC_DESTROY] = "Destroy Multicast Group Cmd",
	[IRDMA_OP_MC_MODIFY] = "Modify Multicast Group Cmd",
	[IRDMA_OP_STATS_ALLOCATE] = "Add Statistics Instance Cmd",
	[IRDMA_OP_STATS_FREE] = "Free Statistics Instance Cmd",
	[IRDMA_OP_STATS_GATHER] = "Gather Statistics Cmd",
	[IRDMA_OP_WS_ADD_NODE] = "Add Work Scheduler Node Cmd",
	[IRDMA_OP_WS_MODIFY_NODE] = "Modify Work Scheduler Node Cmd",
	[IRDMA_OP_WS_DELETE_NODE] = "Delete Work Scheduler Node Cmd",
	[IRDMA_OP_SET_UP_MAP] = "Set UP-UP Mapping Cmd",
	[IRDMA_OP_GEN_AE] = "Generate AE Cmd",
	[IRDMA_OP_QUERY_RDMA_FEATURES] = "RDMA Get Features Cmd",
	[IRDMA_OP_ALLOC_LOCAL_MAC_ENTRY] = "Allocate Local MAC Entry Cmd",
	[IRDMA_OP_ADD_LOCAL_MAC_ENTRY] = "Add Local MAC Entry Cmd",
	[IRDMA_OP_DELETE_LOCAL_MAC_ENTRY] = "Delete Local MAC Entry Cmd",
	[IRDMA_OP_CQ_MODIFY] = "CQ Modify Cmd",
};

static const struct irdma_cqp_err_info irdma_noncrit_err_list[] = {
	{0xffff, 0x8006, "Flush No Wqe Pending"},
	{0xffff, 0x8007, "Modify QP Bad Close"},
	{0xffff, 0x8009, "LLP Closed"},
	{0xffff, 0x800a, "Reset Not Sent"}
};

/**
 * irdma_cqp_crit_err - check if CQP error is critical
 * @dev: pointer to dev structure
 * @cqp_cmd: code for last CQP operation
 * @maj_err_code: major error code
 * @min_err_code: minot error code
 */
bool irdma_cqp_crit_err(struct irdma_sc_dev *dev, u8 cqp_cmd,
			u16 maj_err_code, u16 min_err_code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(irdma_noncrit_err_list); ++i) {
		if (maj_err_code == irdma_noncrit_err_list[i].maj &&
		    min_err_code == irdma_noncrit_err_list[i].min) {
			ibdev_dbg(to_ibdev(dev),
				  "CQP: [%s Error][%s] maj=0x%x min=0x%x\n",
				  irdma_noncrit_err_list[i].desc,
				  irdma_cqp_cmd_names[cqp_cmd], maj_err_code,
				  min_err_code);
			return false;
		}
	}
	return true;
}

/**
 * irdma_handle_cqp_op - process cqp command
 * @rf: RDMA PCI function
 * @cqp_request: cqp request to process
 */
int irdma_handle_cqp_op(struct irdma_pci_f *rf,
			struct irdma_cqp_request *cqp_request)
{
	struct irdma_sc_dev *dev = &rf->sc_dev;
	struct cqp_cmds_info *info = &cqp_request->info;
	int status;
	bool put_cqp_request = true;

	if (rf->reset)
		return -EBUSY;

	irdma_get_cqp_request(cqp_request);
	status = irdma_process_cqp_cmd(dev, info);
	if (status)
		goto err;

	if (cqp_request->waiting) {
		put_cqp_request = false;
		status = irdma_wait_event(rf, cqp_request);
		if (status)
			goto err;
	}

	return 0;

err:
	if (irdma_cqp_crit_err(dev, info->cqp_cmd,
			       cqp_request->compl_info.maj_err_code,
			       cqp_request->compl_info.min_err_code))
		ibdev_err(&rf->iwdev->ibdev,
			  "[%s Error][op_code=%d] status=%d waiting=%d completion_err=%d maj=0x%x min=0x%x\n",
			  irdma_cqp_cmd_names[info->cqp_cmd], info->cqp_cmd, status, cqp_request->waiting,
			  cqp_request->compl_info.error, cqp_request->compl_info.maj_err_code,
			  cqp_request->compl_info.min_err_code);

	if (put_cqp_request)
		irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

void irdma_qp_add_ref(struct ib_qp *ibqp)
{
	struct irdma_qp *iwqp = (struct irdma_qp *)ibqp;

	refcount_inc(&iwqp->refcnt);
}

void irdma_qp_rem_ref(struct ib_qp *ibqp)
{
	struct irdma_qp *iwqp = to_iwqp(ibqp);
	struct irdma_device *iwdev = iwqp->iwdev;
	u32 qp_num;
	unsigned long flags;

	spin_lock_irqsave(&iwdev->rf->qptable_lock, flags);
	if (!refcount_dec_and_test(&iwqp->refcnt)) {
		spin_unlock_irqrestore(&iwdev->rf->qptable_lock, flags);
		return;
	}

	qp_num = iwqp->ibqp.qp_num;
	iwdev->rf->qp_table[qp_num] = NULL;
	spin_unlock_irqrestore(&iwdev->rf->qptable_lock, flags);
	complete(&iwqp->free_qp);
}

struct ib_device *to_ibdev(struct irdma_sc_dev *dev)
{
	return &(container_of(dev, struct irdma_pci_f, sc_dev))->iwdev->ibdev;
}

/**
 * irdma_get_qp - get qp address
 * @device: iwarp device
 * @qpn: qp number
 */
struct ib_qp *irdma_get_qp(struct ib_device *device, int qpn)
{
	struct irdma_device *iwdev = to_iwdev(device);

	if (qpn < IW_FIRST_QPN || qpn >= iwdev->rf->max_qp)
		return NULL;

	return &iwdev->rf->qp_table[qpn]->ibqp;
}

/**
 * irdma_remove_cqp_head - return head entry and remove
 * @dev: device
 */
void *irdma_remove_cqp_head(struct irdma_sc_dev *dev)
{
	struct list_head *entry;
	struct list_head *list = &dev->cqp_cmd_head;

	if (list_empty(list))
		return NULL;

	entry = list->next;
	list_del(entry);

	return entry;
}

/**
 * irdma_cqp_sds_cmd - create cqp command for sd
 * @dev: hardware control device structure
 * @sdinfo: information for sd cqp
 *
 */
int irdma_cqp_sds_cmd(struct irdma_sc_dev *dev,
		      struct irdma_update_sds_info *sdinfo)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_pci_f *rf = dev_to_rf(dev);
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	memcpy(&cqp_info->in.u.update_pe_sds.info, sdinfo,
	       sizeof(cqp_info->in.u.update_pe_sds.info));
	cqp_info->cqp_cmd = IRDMA_OP_UPDATE_PE_SDS;
	cqp_info->post_sq = 1;
	cqp_info->in.u.update_pe_sds.dev = dev;
	cqp_info->in.u.update_pe_sds.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_cqp_qp_suspend_resume - cqp command for suspend/resume
 * @qp: hardware control qp
 * @op: suspend or resume
 */
int irdma_cqp_qp_suspend_resume(struct irdma_sc_qp *qp, u8 op)
{
	struct irdma_sc_dev *dev = qp->dev;
	struct irdma_cqp_request *cqp_request;
	struct irdma_sc_cqp *cqp = dev->cqp;
	struct cqp_cmds_info *cqp_info;
	struct irdma_pci_f *rf = dev_to_rf(dev);
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, false);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = op;
	cqp_info->in.u.suspend_resume.cqp = cqp;
	cqp_info->in.u.suspend_resume.qp = qp;
	cqp_info->in.u.suspend_resume.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_term_modify_qp - modify qp for term message
 * @qp: hardware control qp
 * @next_state: qp's next state
 * @term: terminate code
 * @term_len: length
 */
void irdma_term_modify_qp(struct irdma_sc_qp *qp, u8 next_state, u8 term,
			  u8 term_len)
{
	struct irdma_qp *iwqp;

	iwqp = qp->qp_uk.back_qp;
	irdma_next_iw_state(iwqp, next_state, 0, term, term_len);
};

/**
 * irdma_terminate_done - after terminate is completed
 * @qp: hardware control qp
 * @timeout_occurred: indicates if terminate timer expired
 */
void irdma_terminate_done(struct irdma_sc_qp *qp, int timeout_occurred)
{
	struct irdma_qp *iwqp;
	u8 hte = 0;
	bool first_time;
	unsigned long flags;

	iwqp = qp->qp_uk.back_qp;
	spin_lock_irqsave(&iwqp->lock, flags);
	if (iwqp->hte_added) {
		iwqp->hte_added = 0;
		hte = 1;
	}
	first_time = !(qp->term_flags & IRDMA_TERM_DONE);
	qp->term_flags |= IRDMA_TERM_DONE;
	spin_unlock_irqrestore(&iwqp->lock, flags);
	if (first_time) {
		if (!timeout_occurred)
			irdma_terminate_del_timer(qp);

		irdma_next_iw_state(iwqp, IRDMA_QP_STATE_ERROR, hte, 0, 0);
		irdma_cm_disconn(iwqp);
	}
}

static void irdma_terminate_timeout(struct timer_list *t)
{
	struct irdma_qp *iwqp = from_timer(iwqp, t, terminate_timer);
	struct irdma_sc_qp *qp = &iwqp->sc_qp;

	irdma_terminate_done(qp, 1);
	irdma_qp_rem_ref(&iwqp->ibqp);
}

/**
 * irdma_terminate_start_timer - start terminate timeout
 * @qp: hardware control qp
 */
void irdma_terminate_start_timer(struct irdma_sc_qp *qp)
{
	struct irdma_qp *iwqp;

	iwqp = qp->qp_uk.back_qp;
	irdma_qp_add_ref(&iwqp->ibqp);
	timer_setup(&iwqp->terminate_timer, irdma_terminate_timeout, 0);
	iwqp->terminate_timer.expires = jiffies + HZ;

	add_timer(&iwqp->terminate_timer);
}

/**
 * irdma_terminate_del_timer - delete terminate timeout
 * @qp: hardware control qp
 */
void irdma_terminate_del_timer(struct irdma_sc_qp *qp)
{
	struct irdma_qp *iwqp;
	int ret;

	iwqp = qp->qp_uk.back_qp;
	ret = del_timer(&iwqp->terminate_timer);
	if (ret)
		irdma_qp_rem_ref(&iwqp->ibqp);
}

/**
 * irdma_cqp_query_fpm_val_cmd - send cqp command for fpm
 * @dev: function device struct
 * @val_mem: buffer for fpm
 * @hmc_fn_id: function id for fpm
 */
int irdma_cqp_query_fpm_val_cmd(struct irdma_sc_dev *dev,
				struct irdma_dma_mem *val_mem, u8 hmc_fn_id)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_pci_f *rf = dev_to_rf(dev);
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	cqp_request->param = NULL;
	cqp_info->in.u.query_fpm_val.cqp = dev->cqp;
	cqp_info->in.u.query_fpm_val.fpm_val_pa = val_mem->pa;
	cqp_info->in.u.query_fpm_val.fpm_val_va = val_mem->va;
	cqp_info->in.u.query_fpm_val.hmc_fn_id = hmc_fn_id;
	cqp_info->cqp_cmd = IRDMA_OP_QUERY_FPM_VAL;
	cqp_info->post_sq = 1;
	cqp_info->in.u.query_fpm_val.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_cqp_commit_fpm_val_cmd - commit fpm values in hw
 * @dev: hardware control device structure
 * @val_mem: buffer with fpm values
 * @hmc_fn_id: function id for fpm
 */
int irdma_cqp_commit_fpm_val_cmd(struct irdma_sc_dev *dev,
				 struct irdma_dma_mem *val_mem, u8 hmc_fn_id)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_pci_f *rf = dev_to_rf(dev);
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	cqp_request->param = NULL;
	cqp_info->in.u.commit_fpm_val.cqp = dev->cqp;
	cqp_info->in.u.commit_fpm_val.fpm_val_pa = val_mem->pa;
	cqp_info->in.u.commit_fpm_val.fpm_val_va = val_mem->va;
	cqp_info->in.u.commit_fpm_val.hmc_fn_id = hmc_fn_id;
	cqp_info->cqp_cmd = IRDMA_OP_COMMIT_FPM_VAL;
	cqp_info->post_sq = 1;
	cqp_info->in.u.commit_fpm_val.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_cqp_cq_create_cmd - create a cq for the cqp
 * @dev: device pointer
 * @cq: pointer to created cq
 */
int irdma_cqp_cq_create_cmd(struct irdma_sc_dev *dev, struct irdma_sc_cq *cq)
{
	struct irdma_pci_f *rf = dev_to_rf(dev);
	struct irdma_cqp *iwcqp = &rf->cqp;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(iwcqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = IRDMA_OP_CQ_CREATE;
	cqp_info->post_sq = 1;
	cqp_info->in.u.cq_create.cq = cq;
	cqp_info->in.u.cq_create.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(iwcqp, cqp_request);

	return status;
}

/**
 * irdma_cqp_qp_create_cmd - create a qp for the cqp
 * @dev: device pointer
 * @qp: pointer to created qp
 */
int irdma_cqp_qp_create_cmd(struct irdma_sc_dev *dev, struct irdma_sc_qp *qp)
{
	struct irdma_pci_f *rf = dev_to_rf(dev);
	struct irdma_cqp *iwcqp = &rf->cqp;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_create_qp_info *qp_info;
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(iwcqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	qp_info = &cqp_request->info.in.u.qp_create.info;
	memset(qp_info, 0, sizeof(*qp_info));
	qp_info->cq_num_valid = true;
	qp_info->next_iwarp_state = IRDMA_QP_STATE_RTS;
	cqp_info->cqp_cmd = IRDMA_OP_QP_CREATE;
	cqp_info->post_sq = 1;
	cqp_info->in.u.qp_create.qp = qp;
	cqp_info->in.u.qp_create.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(iwcqp, cqp_request);

	return status;
}

/**
 * irdma_dealloc_push_page - free a push page for qp
 * @rf: RDMA PCI function
 * @qp: hardware control qp
 */
static void irdma_dealloc_push_page(struct irdma_pci_f *rf,
				    struct irdma_sc_qp *qp)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;

	if (qp->push_idx == IRDMA_INVALID_PUSH_PAGE_INDEX)
		return;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, false);
	if (!cqp_request)
		return;

	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = IRDMA_OP_MANAGE_PUSH_PAGE;
	cqp_info->post_sq = 1;
	cqp_info->in.u.manage_push_page.info.push_idx = qp->push_idx;
	cqp_info->in.u.manage_push_page.info.qs_handle = qp->qs_handle;
	cqp_info->in.u.manage_push_page.info.free_page = 1;
	cqp_info->in.u.manage_push_page.info.push_page_type = 0;
	cqp_info->in.u.manage_push_page.cqp = &rf->cqp.sc_cqp;
	cqp_info->in.u.manage_push_page.scratch = (uintptr_t)cqp_request;
	status = irdma_handle_cqp_op(rf, cqp_request);
	if (!status)
		qp->push_idx = IRDMA_INVALID_PUSH_PAGE_INDEX;
	irdma_put_cqp_request(&rf->cqp, cqp_request);
}

/**
 * irdma_free_qp_rsrc - free up memory resources for qp
 * @iwqp: qp ptr (user or kernel)
 */
void irdma_free_qp_rsrc(struct irdma_qp *iwqp)
{
	struct irdma_device *iwdev = iwqp->iwdev;
	struct irdma_pci_f *rf = iwdev->rf;
	u32 qp_num = iwqp->ibqp.qp_num;

	irdma_ieq_cleanup_qp(iwdev->vsi.ieq, &iwqp->sc_qp);
	irdma_dealloc_push_page(rf, &iwqp->sc_qp);
	if (iwqp->sc_qp.vsi) {
		irdma_qp_rem_qos(&iwqp->sc_qp);
		iwqp->sc_qp.dev->ws_remove(iwqp->sc_qp.vsi,
					   iwqp->sc_qp.user_pri);
	}

	if (qp_num > 2)
		irdma_free_rsrc(rf, rf->allocated_qps, qp_num);
	dma_free_coherent(rf->sc_dev.hw->device, iwqp->q2_ctx_mem.size,
			  iwqp->q2_ctx_mem.va, iwqp->q2_ctx_mem.pa);
	iwqp->q2_ctx_mem.va = NULL;
	dma_free_coherent(rf->sc_dev.hw->device, iwqp->kqp.dma_mem.size,
			  iwqp->kqp.dma_mem.va, iwqp->kqp.dma_mem.pa);
	iwqp->kqp.dma_mem.va = NULL;
	kfree(iwqp->kqp.sq_wrid_mem);
	kfree(iwqp->kqp.rq_wrid_mem);
}

/**
 * irdma_cq_wq_destroy - send cq destroy cqp
 * @rf: RDMA PCI function
 * @cq: hardware control cq
 */
void irdma_cq_wq_destroy(struct irdma_pci_f *rf, struct irdma_sc_cq *cq)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request)
		return;

	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = IRDMA_OP_CQ_DESTROY;
	cqp_info->post_sq = 1;
	cqp_info->in.u.cq_destroy.cq = cq;
	cqp_info->in.u.cq_destroy.scratch = (uintptr_t)cqp_request;

	irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);
}

/**
 * irdma_hw_modify_qp_callback - handle state for modifyQPs that don't wait
 * @cqp_request: modify QP completion
 */
static void irdma_hw_modify_qp_callback(struct irdma_cqp_request *cqp_request)
{
	struct cqp_cmds_info *cqp_info;
	struct irdma_qp *iwqp;

	cqp_info = &cqp_request->info;
	iwqp = cqp_info->in.u.qp_modify.qp->qp_uk.back_qp;
	atomic_dec(&iwqp->hw_mod_qp_pend);
	wake_up(&iwqp->mod_qp_waitq);
}

/**
 * irdma_hw_modify_qp - setup cqp for modify qp
 * @iwdev: RDMA device
 * @iwqp: qp ptr (user or kernel)
 * @info: info for modify qp
 * @wait: flag to wait or not for modify qp completion
 */
int irdma_hw_modify_qp(struct irdma_device *iwdev, struct irdma_qp *iwqp,
		       struct irdma_modify_qp_info *info, bool wait)
{
	int status;
	struct irdma_pci_f *rf = iwdev->rf;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_modify_qp_info *m_info;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, wait);
	if (!cqp_request)
		return -ENOMEM;

	if (!wait) {
		cqp_request->callback_fcn = irdma_hw_modify_qp_callback;
		atomic_inc(&iwqp->hw_mod_qp_pend);
	}
	cqp_info = &cqp_request->info;
	m_info = &cqp_info->in.u.qp_modify.info;
	memcpy(m_info, info, sizeof(*m_info));
	cqp_info->cqp_cmd = IRDMA_OP_QP_MODIFY;
	cqp_info->post_sq = 1;
	cqp_info->in.u.qp_modify.qp = &iwqp->sc_qp;
	cqp_info->in.u.qp_modify.scratch = (uintptr_t)cqp_request;
	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);
	if (status) {
		if (rdma_protocol_roce(&iwdev->ibdev, 1))
			return status;

		switch (m_info->next_iwarp_state) {
			struct irdma_gen_ae_info ae_info;

		case IRDMA_QP_STATE_RTS:
		case IRDMA_QP_STATE_IDLE:
		case IRDMA_QP_STATE_TERMINATE:
		case IRDMA_QP_STATE_CLOSING:
			if (info->curr_iwarp_state == IRDMA_QP_STATE_IDLE)
				irdma_send_reset(iwqp->cm_node);
			else
				iwqp->sc_qp.term_flags = IRDMA_TERM_DONE;
			if (!wait) {
				ae_info.ae_code = IRDMA_AE_BAD_CLOSE;
				ae_info.ae_src = 0;
				irdma_gen_ae(rf, &iwqp->sc_qp, &ae_info, false);
			} else {
				cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp,
									      wait);
				if (!cqp_request)
					return -ENOMEM;

				cqp_info = &cqp_request->info;
				m_info = &cqp_info->in.u.qp_modify.info;
				memcpy(m_info, info, sizeof(*m_info));
				cqp_info->cqp_cmd = IRDMA_OP_QP_MODIFY;
				cqp_info->post_sq = 1;
				cqp_info->in.u.qp_modify.qp = &iwqp->sc_qp;
				cqp_info->in.u.qp_modify.scratch = (uintptr_t)cqp_request;
				m_info->next_iwarp_state = IRDMA_QP_STATE_ERROR;
				m_info->reset_tcp_conn = true;
				irdma_handle_cqp_op(rf, cqp_request);
				irdma_put_cqp_request(&rf->cqp, cqp_request);
			}
			break;
		case IRDMA_QP_STATE_ERROR:
		default:
			break;
		}
	}

	return status;
}

/**
 * irdma_cqp_cq_destroy_cmd - destroy the cqp cq
 * @dev: device pointer
 * @cq: pointer to cq
 */
void irdma_cqp_cq_destroy_cmd(struct irdma_sc_dev *dev, struct irdma_sc_cq *cq)
{
	struct irdma_pci_f *rf = dev_to_rf(dev);

	irdma_cq_wq_destroy(rf, cq);
}

/**
 * irdma_cqp_qp_destroy_cmd - destroy the cqp
 * @dev: device pointer
 * @qp: pointer to qp
 */
int irdma_cqp_qp_destroy_cmd(struct irdma_sc_dev *dev, struct irdma_sc_qp *qp)
{
	struct irdma_pci_f *rf = dev_to_rf(dev);
	struct irdma_cqp *iwcqp = &rf->cqp;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(iwcqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	memset(cqp_info, 0, sizeof(*cqp_info));
	cqp_info->cqp_cmd = IRDMA_OP_QP_DESTROY;
	cqp_info->post_sq = 1;
	cqp_info->in.u.qp_destroy.qp = qp;
	cqp_info->in.u.qp_destroy.scratch = (uintptr_t)cqp_request;
	cqp_info->in.u.qp_destroy.remove_hash_idx = true;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_ieq_mpa_crc_ae - generate AE for crc error
 * @dev: hardware control device structure
 * @qp: hardware control qp
 */
void irdma_ieq_mpa_crc_ae(struct irdma_sc_dev *dev, struct irdma_sc_qp *qp)
{
	struct irdma_gen_ae_info info = {};
	struct irdma_pci_f *rf = dev_to_rf(dev);

	ibdev_dbg(&rf->iwdev->ibdev, "AEQ: Generate MPA CRC AE\n");
	info.ae_code = IRDMA_AE_LLP_RECEIVED_MPA_CRC_ERROR;
	info.ae_src = IRDMA_AE_SOURCE_RQ;
	irdma_gen_ae(rf, qp, &info, false);
}

/**
 * irdma_init_hash_desc - initialize hash for crc calculation
 * @desc: cryption type
 */
int irdma_init_hash_desc(struct shash_desc **desc)
{
	struct crypto_shash *tfm;
	struct shash_desc *tdesc;

	tfm = crypto_alloc_shash("crc32c", 0, 0);
	if (IS_ERR(tfm))
		return -EINVAL;

	tdesc = kzalloc(sizeof(*tdesc) + crypto_shash_descsize(tfm),
			GFP_KERNEL);
	if (!tdesc) {
		crypto_free_shash(tfm);
		return -EINVAL;
	}

	tdesc->tfm = tfm;
	*desc = tdesc;

	return 0;
}

/**
 * irdma_free_hash_desc - free hash desc
 * @desc: to be freed
 */
void irdma_free_hash_desc(struct shash_desc *desc)
{
	if (desc) {
		crypto_free_shash(desc->tfm);
		kfree(desc);
	}
}

/**
 * irdma_ieq_check_mpacrc - check if mpa crc is OK
 * @desc: desc for hash
 * @addr: address of buffer for crc
 * @len: length of buffer
 * @val: value to be compared
 */
int irdma_ieq_check_mpacrc(struct shash_desc *desc, void *addr, u32 len,
			   u32 val)
{
	u32 crc = 0;
	int ret;
	int ret_code = 0;

	crypto_shash_init(desc);
	ret = crypto_shash_update(desc, addr, len);
	if (!ret)
		crypto_shash_final(desc, (u8 *)&crc);
	if (crc != val)
		ret_code = -EINVAL;

	return ret_code;
}

/**
 * irdma_ieq_get_qp - get qp based on quad in puda buffer
 * @dev: hardware control device structure
 * @buf: receive puda buffer on exception q
 */
struct irdma_sc_qp *irdma_ieq_get_qp(struct irdma_sc_dev *dev,
				     struct irdma_puda_buf *buf)
{
	struct irdma_qp *iwqp;
	struct irdma_cm_node *cm_node;
	struct irdma_device *iwdev = buf->vsi->back_vsi;
	u32 loc_addr[4] = {};
	u32 rem_addr[4] = {};
	u16 loc_port, rem_port;
	struct ipv6hdr *ip6h;
	struct iphdr *iph = (struct iphdr *)buf->iph;
	struct tcphdr *tcph = (struct tcphdr *)buf->tcph;

	if (iph->version == 4) {
		loc_addr[0] = ntohl(iph->daddr);
		rem_addr[0] = ntohl(iph->saddr);
	} else {
		ip6h = (struct ipv6hdr *)buf->iph;
		irdma_copy_ip_ntohl(loc_addr, ip6h->daddr.in6_u.u6_addr32);
		irdma_copy_ip_ntohl(rem_addr, ip6h->saddr.in6_u.u6_addr32);
	}
	loc_port = ntohs(tcph->dest);
	rem_port = ntohs(tcph->source);
	cm_node = irdma_find_node(&iwdev->cm_core, rem_port, rem_addr, loc_port,
				  loc_addr, buf->vlan_valid ? buf->vlan_id : 0xFFFF);
	if (!cm_node)
		return NULL;

	iwqp = cm_node->iwqp;
	irdma_rem_ref_cm_node(cm_node);

	return &iwqp->sc_qp;
}

/**
 * irdma_send_ieq_ack - ACKs for duplicate or OOO partials FPDUs
 * @qp: qp ptr
 */
void irdma_send_ieq_ack(struct irdma_sc_qp *qp)
{
	struct irdma_cm_node *cm_node = ((struct irdma_qp *)qp->qp_uk.back_qp)->cm_node;
	struct irdma_puda_buf *buf = qp->pfpdu.lastrcv_buf;
	struct tcphdr *tcph = (struct tcphdr *)buf->tcph;

	cm_node->tcp_cntxt.rcv_nxt = qp->pfpdu.nextseqnum;
	cm_node->tcp_cntxt.loc_seq_num = ntohl(tcph->ack_seq);

	irdma_send_ack(cm_node);
}

/**
 * irdma_puda_ieq_get_ah_info - get AH info from IEQ buffer
 * @qp: qp pointer
 * @ah_info: AH info pointer
 */
void irdma_puda_ieq_get_ah_info(struct irdma_sc_qp *qp,
				struct irdma_ah_info *ah_info)
{
	struct irdma_puda_buf *buf = qp->pfpdu.ah_buf;
	struct iphdr *iph;
	struct ipv6hdr *ip6h;

	memset(ah_info, 0, sizeof(*ah_info));
	ah_info->do_lpbk = true;
	ah_info->vlan_tag = buf->vlan_id;
	ah_info->insert_vlan_tag = buf->vlan_valid;
	ah_info->ipv4_valid = buf->ipv4;
	ah_info->vsi = qp->vsi;

	if (buf->smac_valid)
		ether_addr_copy(ah_info->mac_addr, buf->smac);

	if (buf->ipv4) {
		ah_info->ipv4_valid = true;
		iph = (struct iphdr *)buf->iph;
		ah_info->hop_ttl = iph->ttl;
		ah_info->tc_tos = iph->tos;
		ah_info->dest_ip_addr[0] = ntohl(iph->daddr);
		ah_info->src_ip_addr[0] = ntohl(iph->saddr);
	} else {
		ip6h = (struct ipv6hdr *)buf->iph;
		ah_info->hop_ttl = ip6h->hop_limit;
		ah_info->tc_tos = ip6h->priority;
		irdma_copy_ip_ntohl(ah_info->dest_ip_addr,
				    ip6h->daddr.in6_u.u6_addr32);
		irdma_copy_ip_ntohl(ah_info->src_ip_addr,
				    ip6h->saddr.in6_u.u6_addr32);
	}

	ah_info->dst_arpindex = irdma_arp_table(dev_to_rf(qp->dev),
						ah_info->dest_ip_addr,
						ah_info->ipv4_valid,
						NULL, IRDMA_ARP_RESOLVE);
}

/**
 * irdma_gen1_ieq_update_tcpip_info - update tcpip in the buffer
 * @buf: puda to update
 * @len: length of buffer
 * @seqnum: seq number for tcp
 */
static void irdma_gen1_ieq_update_tcpip_info(struct irdma_puda_buf *buf,
					     u16 len, u32 seqnum)
{
	struct tcphdr *tcph;
	struct iphdr *iph;
	u16 iphlen;
	u16 pktsize;
	u8 *addr = buf->mem.va;

	iphlen = (buf->ipv4) ? 20 : 40;
	iph = (struct iphdr *)(addr + buf->maclen);
	tcph = (struct tcphdr *)(addr + buf->maclen + iphlen);
	pktsize = len + buf->tcphlen + iphlen;
	iph->tot_len = htons(pktsize);
	tcph->seq = htonl(seqnum);
}

/**
 * irdma_ieq_update_tcpip_info - update tcpip in the buffer
 * @buf: puda to update
 * @len: length of buffer
 * @seqnum: seq number for tcp
 */
void irdma_ieq_update_tcpip_info(struct irdma_puda_buf *buf, u16 len,
				 u32 seqnum)
{
	struct tcphdr *tcph;
	u8 *addr;

	if (buf->vsi->dev->hw_attrs.uk_attrs.hw_rev == IRDMA_GEN_1)
		return irdma_gen1_ieq_update_tcpip_info(buf, len, seqnum);

	addr = buf->mem.va;
	tcph = (struct tcphdr *)addr;
	tcph->seq = htonl(seqnum);
}

/**
 * irdma_gen1_puda_get_tcpip_info - get tcpip info from puda
 * buffer
 * @info: to get information
 * @buf: puda buffer
 */
static int irdma_gen1_puda_get_tcpip_info(struct irdma_puda_cmpl_info *info,
					  struct irdma_puda_buf *buf)
{
	struct iphdr *iph;
	struct ipv6hdr *ip6h;
	struct tcphdr *tcph;
	u16 iphlen;
	u16 pkt_len;
	u8 *mem = buf->mem.va;
	struct ethhdr *ethh = buf->mem.va;

	if (ethh->h_proto == htons(0x8100)) {
		info->vlan_valid = true;
		buf->vlan_id = ntohs(((struct vlan_ethhdr *)ethh)->h_vlan_TCI) &
			       VLAN_VID_MASK;
	}

	buf->maclen = (info->vlan_valid) ? 18 : 14;
	iphlen = (info->l3proto) ? 40 : 20;
	buf->ipv4 = (info->l3proto) ? false : true;
	buf->iph = mem + buf->maclen;
	iph = (struct iphdr *)buf->iph;
	buf->tcph = buf->iph + iphlen;
	tcph = (struct tcphdr *)buf->tcph;

	if (buf->ipv4) {
		pkt_len = ntohs(iph->tot_len);
	} else {
		ip6h = (struct ipv6hdr *)buf->iph;
		pkt_len = ntohs(ip6h->payload_len) + iphlen;
	}

	buf->totallen = pkt_len + buf->maclen;

	if (info->payload_len < buf->totallen) {
		ibdev_dbg(to_ibdev(buf->vsi->dev),
			  "ERR: payload_len = 0x%x totallen expected0x%x\n",
			  info->payload_len, buf->totallen);
		return -EINVAL;
	}

	buf->tcphlen = tcph->doff << 2;
	buf->datalen = pkt_len - iphlen - buf->tcphlen;
	buf->data = buf->datalen ? buf->tcph + buf->tcphlen : NULL;
	buf->hdrlen = buf->maclen + iphlen + buf->tcphlen;
	buf->seqnum = ntohl(tcph->seq);

	return 0;
}

/**
 * irdma_puda_get_tcpip_info - get tcpip info from puda buffer
 * @info: to get information
 * @buf: puda buffer
 */
int irdma_puda_get_tcpip_info(struct irdma_puda_cmpl_info *info,
			      struct irdma_puda_buf *buf)
{
	struct tcphdr *tcph;
	u32 pkt_len;
	u8 *mem;

	if (buf->vsi->dev->hw_attrs.uk_attrs.hw_rev == IRDMA_GEN_1)
		return irdma_gen1_puda_get_tcpip_info(info, buf);

	mem = buf->mem.va;
	buf->vlan_valid = info->vlan_valid;
	if (info->vlan_valid)
		buf->vlan_id = info->vlan;

	buf->ipv4 = info->ipv4;
	if (buf->ipv4)
		buf->iph = mem + IRDMA_IPV4_PAD;
	else
		buf->iph = mem;

	buf->tcph = mem + IRDMA_TCP_OFFSET;
	tcph = (struct tcphdr *)buf->tcph;
	pkt_len = info->payload_len;
	buf->totallen = pkt_len;
	buf->tcphlen = tcph->doff << 2;
	buf->datalen = pkt_len - IRDMA_TCP_OFFSET - buf->tcphlen;
	buf->data = buf->datalen ? buf->tcph + buf->tcphlen : NULL;
	buf->hdrlen = IRDMA_TCP_OFFSET + buf->tcphlen;
	buf->seqnum = ntohl(tcph->seq);

	if (info->smac_valid) {
		ether_addr_copy(buf->smac, info->smac);
		buf->smac_valid = true;
	}

	return 0;
}

/**
 * irdma_hw_stats_timeout - Stats timer-handler which updates all HW stats
 * @t: timer_list pointer
 */
static void irdma_hw_stats_timeout(struct timer_list *t)
{
	struct irdma_vsi_pestat *pf_devstat =
		from_timer(pf_devstat, t, stats_timer);
	struct irdma_sc_vsi *sc_vsi = pf_devstat->vsi;

	if (sc_vsi->dev->hw_attrs.uk_attrs.hw_rev == IRDMA_GEN_1)
		irdma_cqp_gather_stats_gen1(sc_vsi->dev, sc_vsi->pestat);
	else
		irdma_cqp_gather_stats_cmd(sc_vsi->dev, sc_vsi->pestat, false);

	mod_timer(&pf_devstat->stats_timer,
		  jiffies + msecs_to_jiffies(STATS_TIMER_DELAY));
}

/**
 * irdma_hw_stats_start_timer - Start periodic stats timer
 * @vsi: vsi structure pointer
 */
void irdma_hw_stats_start_timer(struct irdma_sc_vsi *vsi)
{
	struct irdma_vsi_pestat *devstat = vsi->pestat;

	timer_setup(&devstat->stats_timer, irdma_hw_stats_timeout, 0);
	mod_timer(&devstat->stats_timer,
		  jiffies + msecs_to_jiffies(STATS_TIMER_DELAY));
}

/**
 * irdma_hw_stats_stop_timer - Delete periodic stats timer
 * @vsi: pointer to vsi structure
 */
void irdma_hw_stats_stop_timer(struct irdma_sc_vsi *vsi)
{
	struct irdma_vsi_pestat *devstat = vsi->pestat;

	del_timer_sync(&devstat->stats_timer);
}

/**
 * irdma_process_stats - Checking for wrap and update stats
 * @pestat: stats structure pointer
 */
static inline void irdma_process_stats(struct irdma_vsi_pestat *pestat)
{
	sc_vsi_update_stats(pestat->vsi);
}

/**
 * irdma_cqp_gather_stats_gen1 - Gather stats
 * @dev: pointer to device structure
 * @pestat: statistics structure
 */
void irdma_cqp_gather_stats_gen1(struct irdma_sc_dev *dev,
				 struct irdma_vsi_pestat *pestat)
{
	struct irdma_gather_stats *gather_stats =
		pestat->gather_info.gather_stats_va;
	u32 stats_inst_offset_32;
	u32 stats_inst_offset_64;

	stats_inst_offset_32 = (pestat->gather_info.use_stats_inst) ?
				       pestat->gather_info.stats_inst_index :
				       pestat->hw->hmc.hmc_fn_id;
	stats_inst_offset_32 *= 4;
	stats_inst_offset_64 = stats_inst_offset_32 * 2;

	gather_stats->rxvlanerr =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_RXVLANERR]
		     + stats_inst_offset_32);
	gather_stats->ip4rxdiscard =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_IP4RXDISCARD]
		     + stats_inst_offset_32);
	gather_stats->ip4rxtrunc =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_IP4RXTRUNC]
		     + stats_inst_offset_32);
	gather_stats->ip4txnoroute =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_IP4TXNOROUTE]
		     + stats_inst_offset_32);
	gather_stats->ip6rxdiscard =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_IP6RXDISCARD]
		     + stats_inst_offset_32);
	gather_stats->ip6rxtrunc =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_IP6RXTRUNC]
		     + stats_inst_offset_32);
	gather_stats->ip6txnoroute =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_IP6TXNOROUTE]
		     + stats_inst_offset_32);
	gather_stats->tcprtxseg =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_TCPRTXSEG]
		     + stats_inst_offset_32);
	gather_stats->tcprxopterr =
		rd32(dev->hw,
		     dev->hw_stats_regs_32[IRDMA_HW_STAT_INDEX_TCPRXOPTERR]
		     + stats_inst_offset_32);

	gather_stats->ip4rxocts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP4RXOCTS]
		     + stats_inst_offset_64);
	gather_stats->ip4rxpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP4RXPKTS]
		     + stats_inst_offset_64);
	gather_stats->ip4txfrag =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP4RXFRAGS]
		     + stats_inst_offset_64);
	gather_stats->ip4rxmcpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP4RXMCPKTS]
		     + stats_inst_offset_64);
	gather_stats->ip4txocts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP4TXOCTS]
		     + stats_inst_offset_64);
	gather_stats->ip4txpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP4TXPKTS]
		     + stats_inst_offset_64);
	gather_stats->ip4txfrag =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP4TXFRAGS]
		     + stats_inst_offset_64);
	gather_stats->ip4txmcpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP4TXMCPKTS]
		     + stats_inst_offset_64);
	gather_stats->ip6rxocts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP6RXOCTS]
		     + stats_inst_offset_64);
	gather_stats->ip6rxpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP6RXPKTS]
		     + stats_inst_offset_64);
	gather_stats->ip6txfrags =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP6RXFRAGS]
		     + stats_inst_offset_64);
	gather_stats->ip6rxmcpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP6RXMCPKTS]
		     + stats_inst_offset_64);
	gather_stats->ip6txocts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP6TXOCTS]
		     + stats_inst_offset_64);
	gather_stats->ip6txpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP6TXPKTS]
		     + stats_inst_offset_64);
	gather_stats->ip6txfrags =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP6TXFRAGS]
		     + stats_inst_offset_64);
	gather_stats->ip6txmcpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_IP6TXMCPKTS]
		     + stats_inst_offset_64);
	gather_stats->tcprxsegs =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_TCPRXSEGS]
		     + stats_inst_offset_64);
	gather_stats->tcptxsegs =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_TCPTXSEG]
		     + stats_inst_offset_64);
	gather_stats->rdmarxrds =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_RDMARXRDS]
		     + stats_inst_offset_64);
	gather_stats->rdmarxsnds =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_RDMARXSNDS]
		     + stats_inst_offset_64);
	gather_stats->rdmarxwrs =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_RDMARXWRS]
		     + stats_inst_offset_64);
	gather_stats->rdmatxrds =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_RDMATXRDS]
		     + stats_inst_offset_64);
	gather_stats->rdmatxsnds =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_RDMATXSNDS]
		     + stats_inst_offset_64);
	gather_stats->rdmatxwrs =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_RDMATXWRS]
		     + stats_inst_offset_64);
	gather_stats->rdmavbn =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_RDMAVBND]
		     + stats_inst_offset_64);
	gather_stats->rdmavinv =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_RDMAVINV]
		     + stats_inst_offset_64);
	gather_stats->udprxpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_UDPRXPKTS]
		     + stats_inst_offset_64);
	gather_stats->udptxpkts =
		rd64(dev->hw,
		     dev->hw_stats_regs_64[IRDMA_HW_STAT_INDEX_UDPTXPKTS]
		     + stats_inst_offset_64);

	irdma_process_stats(pestat);
}

/**
 * irdma_process_cqp_stats - Checking for wrap and update stats
 * @cqp_request: cqp_request structure pointer
 */
static void irdma_process_cqp_stats(struct irdma_cqp_request *cqp_request)
{
	struct irdma_vsi_pestat *pestat = cqp_request->param;

	irdma_process_stats(pestat);
}

/**
 * irdma_cqp_gather_stats_cmd - Gather stats
 * @dev: pointer to device structure
 * @pestat: pointer to stats info
 * @wait: flag to wait or not wait for stats
 */
int irdma_cqp_gather_stats_cmd(struct irdma_sc_dev *dev,
			       struct irdma_vsi_pestat *pestat, bool wait)

{
	struct irdma_pci_f *rf = dev_to_rf(dev);
	struct irdma_cqp *iwcqp = &rf->cqp;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(iwcqp, wait);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	memset(cqp_info, 0, sizeof(*cqp_info));
	cqp_info->cqp_cmd = IRDMA_OP_STATS_GATHER;
	cqp_info->post_sq = 1;
	cqp_info->in.u.stats_gather.info = pestat->gather_info;
	cqp_info->in.u.stats_gather.scratch = (uintptr_t)cqp_request;
	cqp_info->in.u.stats_gather.cqp = &rf->cqp.sc_cqp;
	cqp_request->param = pestat;
	if (!wait)
		cqp_request->callback_fcn = irdma_process_cqp_stats;
	status = irdma_handle_cqp_op(rf, cqp_request);
	if (wait)
		irdma_process_stats(pestat);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_cqp_stats_inst_cmd - Allocate/free stats instance
 * @vsi: pointer to vsi structure
 * @cmd: command to allocate or free
 * @stats_info: pointer to allocate stats info
 */
int irdma_cqp_stats_inst_cmd(struct irdma_sc_vsi *vsi, u8 cmd,
			     struct irdma_stats_inst_info *stats_info)
{
	struct irdma_pci_f *rf = dev_to_rf(vsi->dev);
	struct irdma_cqp *iwcqp = &rf->cqp;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;
	bool wait = false;

	if (cmd == IRDMA_OP_STATS_ALLOCATE)
		wait = true;
	cqp_request = irdma_alloc_and_get_cqp_request(iwcqp, wait);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	memset(cqp_info, 0, sizeof(*cqp_info));
	cqp_info->cqp_cmd = cmd;
	cqp_info->post_sq = 1;
	cqp_info->in.u.stats_manage.info = *stats_info;
	cqp_info->in.u.stats_manage.scratch = (uintptr_t)cqp_request;
	cqp_info->in.u.stats_manage.cqp = &rf->cqp.sc_cqp;
	status = irdma_handle_cqp_op(rf, cqp_request);
	if (wait)
		stats_info->stats_idx = cqp_request->compl_info.op_ret_val;
	irdma_put_cqp_request(iwcqp, cqp_request);

	return status;
}

/**
 * irdma_cqp_ceq_cmd - Create/Destroy CEQ's after CEQ 0
 * @dev: pointer to device info
 * @sc_ceq: pointer to ceq structure
 * @op: Create or Destroy
 */
int irdma_cqp_ceq_cmd(struct irdma_sc_dev *dev, struct irdma_sc_ceq *sc_ceq,
		      u8 op)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_pci_f *rf = dev_to_rf(dev);
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	cqp_info->post_sq = 1;
	cqp_info->cqp_cmd = op;
	cqp_info->in.u.ceq_create.ceq = sc_ceq;
	cqp_info->in.u.ceq_create.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_cqp_aeq_cmd - Create/Destroy AEQ
 * @dev: pointer to device info
 * @sc_aeq: pointer to aeq structure
 * @op: Create or Destroy
 */
int irdma_cqp_aeq_cmd(struct irdma_sc_dev *dev, struct irdma_sc_aeq *sc_aeq,
		      u8 op)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	struct irdma_pci_f *rf = dev_to_rf(dev);
	int status;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, true);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	cqp_info->post_sq = 1;
	cqp_info->cqp_cmd = op;
	cqp_info->in.u.aeq_create.aeq = sc_aeq;
	cqp_info->in.u.aeq_create.scratch = (uintptr_t)cqp_request;

	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_cqp_ws_node_cmd - Add/modify/delete ws node
 * @dev: pointer to device structure
 * @cmd: Add, modify or delete
 * @node_info: pointer to ws node info
 */
int irdma_cqp_ws_node_cmd(struct irdma_sc_dev *dev, u8 cmd,
			  struct irdma_ws_node_info *node_info)
{
	struct irdma_pci_f *rf = dev_to_rf(dev);
	struct irdma_cqp *iwcqp = &rf->cqp;
	struct irdma_sc_cqp *cqp = &iwcqp->sc_cqp;
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;
	bool poll;

	if (!rf->sc_dev.ceq_valid)
		poll = true;
	else
		poll = false;

	cqp_request = irdma_alloc_and_get_cqp_request(iwcqp, !poll);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	memset(cqp_info, 0, sizeof(*cqp_info));
	cqp_info->cqp_cmd = cmd;
	cqp_info->post_sq = 1;
	cqp_info->in.u.ws_node.info = *node_info;
	cqp_info->in.u.ws_node.cqp = cqp;
	cqp_info->in.u.ws_node.scratch = (uintptr_t)cqp_request;
	status = irdma_handle_cqp_op(rf, cqp_request);
	if (status)
		goto exit;

	if (poll) {
		struct irdma_ccq_cqe_info compl_info;

		status = irdma_sc_poll_for_cqp_op_done(cqp, IRDMA_CQP_OP_WORK_SCHED_NODE,
						       &compl_info);
		node_info->qs_handle = compl_info.op_ret_val;
		ibdev_dbg(&rf->iwdev->ibdev, "DCB: opcode=%d, compl_info.retval=%d\n",
			  compl_info.op_code, compl_info.op_ret_val);
	} else {
		node_info->qs_handle = cqp_request->compl_info.op_ret_val;
	}

exit:
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	return status;
}

/**
 * irdma_ah_cqp_op - perform an AH cqp operation
 * @rf: RDMA PCI function
 * @sc_ah: address handle
 * @cmd: AH operation
 * @wait: wait if true
 * @callback_fcn: Callback function on CQP op completion
 * @cb_param: parameter for callback function
 *
 * returns errno
 */
int irdma_ah_cqp_op(struct irdma_pci_f *rf, struct irdma_sc_ah *sc_ah, u8 cmd,
		    bool wait,
		    void (*callback_fcn)(struct irdma_cqp_request *),
		    void *cb_param)
{
	struct irdma_cqp_request *cqp_request;
	struct cqp_cmds_info *cqp_info;
	int status;

	if (cmd != IRDMA_OP_AH_CREATE && cmd != IRDMA_OP_AH_DESTROY)
		return -EINVAL;

	cqp_request = irdma_alloc_and_get_cqp_request(&rf->cqp, wait);
	if (!cqp_request)
		return -ENOMEM;

	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = cmd;
	cqp_info->post_sq = 1;
	if (cmd == IRDMA_OP_AH_CREATE) {
		cqp_info->in.u.ah_create.info = sc_ah->ah_info;
		cqp_info->in.u.ah_create.scratch = (uintptr_t)cqp_request;
		cqp_info->in.u.ah_create.cqp = &rf->cqp.sc_cqp;
	} else if (cmd == IRDMA_OP_AH_DESTROY) {
		cqp_info->in.u.ah_destroy.info = sc_ah->ah_info;
		cqp_info->in.u.ah_destroy.scratch = (uintptr_t)cqp_request;
		cqp_info->in.u.ah_destroy.cqp = &rf->cqp.sc_cqp;
	}

	if (!wait) {
		cqp_request->callback_fcn = callback_fcn;
		cqp_request->param = cb_param;
	}
	status = irdma_handle_cqp_op(rf, cqp_request);
	irdma_put_cqp_request(&rf->cqp, cqp_request);

	if (status)
		return -ENOMEM;

	if (wait)
		sc_ah->ah_info.ah_valid = (cmd == IRDMA_OP_AH_CREATE);

	return 0;
}

/**
 * irdma_ieq_ah_cb - callback after creation of AH for IEQ
 * @cqp_request: pointer to cqp_request of create AH
 */
static void irdma_ieq_ah_cb(struct irdma_cqp_request *cqp_request)
{
	struct irdma_sc_qp *qp = cqp_request->param;
	struct irdma_sc_ah *sc_ah = qp->pfpdu.ah;
	unsigned long flags;

	spin_lock_irqsave(&qp->pfpdu.lock, flags);
	if (!cqp_request->compl_info.op_ret_val) {
		sc_ah->ah_info.ah_valid = true;
		irdma_ieq_process_fpdus(qp, qp->vsi->ieq);
	} else {
		sc_ah->ah_info.ah_valid = false;
		irdma_ieq_cleanup_qp(qp->vsi->ieq, qp);
	}
	spin_unlock_irqrestore(&qp->pfpdu.lock, flags);
}

/**
 * irdma_ilq_ah_cb - callback after creation of AH for ILQ
 * @cqp_request: pointer to cqp_request of create AH
 */
static void irdma_ilq_ah_cb(struct irdma_cqp_request *cqp_request)
{
	struct irdma_cm_node *cm_node = cqp_request->param;
	struct irdma_sc_ah *sc_ah = cm_node->ah;

	sc_ah->ah_info.ah_valid = !cqp_request->compl_info.op_ret_val;
	irdma_add_conn_est_qh(cm_node);
}

/**
 * irdma_puda_create_ah - create AH for ILQ/IEQ qp's
 * @dev: device pointer
 * @ah_info: Address handle info
 * @wait: When true will wait for operation to complete
 * @type: ILQ/IEQ
 * @cb_param: Callback param when not waiting
 * @ah_ret: Returned pointer to address handle if created
 *
 */
int irdma_puda_create_ah(struct irdma_sc_dev *dev,
			 struct irdma_ah_info *ah_info, bool wait,
			 enum puda_rsrc_type type, void *cb_param,
			 struct irdma_sc_ah **ah_ret)
{
	struct irdma_sc_ah *ah;
	struct irdma_pci_f *rf = dev_to_rf(dev);
	int err;

	ah = kzalloc(sizeof(*ah), GFP_ATOMIC);
	*ah_ret = ah;
	if (!ah)
		return -ENOMEM;

	err = irdma_alloc_rsrc(rf, rf->allocated_ahs, rf->max_ah,
			       &ah_info->ah_idx, &rf->next_ah);
	if (err)
		goto err_free;

	ah->dev = dev;
	ah->ah_info = *ah_info;

	if (type == IRDMA_PUDA_RSRC_TYPE_ILQ)
		err = irdma_ah_cqp_op(rf, ah, IRDMA_OP_AH_CREATE, wait,
				      irdma_ilq_ah_cb, cb_param);
	else
		err = irdma_ah_cqp_op(rf, ah, IRDMA_OP_AH_CREATE, wait,
				      irdma_ieq_ah_cb, cb_param);

	if (err)
		goto error;
	return 0;

error:
	irdma_free_rsrc(rf, rf->allocated_ahs, ah->ah_info.ah_idx);
err_free:
	kfree(ah);
	*ah_ret = NULL;
	return -ENOMEM;
}

/**
 * irdma_puda_free_ah - free a puda address handle
 * @dev: device pointer
 * @ah: The address handle to free
 */
void irdma_puda_free_ah(struct irdma_sc_dev *dev, struct irdma_sc_ah *ah)
{
	struct irdma_pci_f *rf = dev_to_rf(dev);

	if (!ah)
		return;

	if (ah->ah_info.ah_valid) {
		irdma_ah_cqp_op(rf, ah, IRDMA_OP_AH_DESTROY, false, NULL, NULL);
		irdma_free_rsrc(rf, rf->allocated_ahs, ah->ah_info.ah_idx);
	}

	kfree(ah);
}

/**
 * irdma_gsi_ud_qp_ah_cb - callback after creation of AH for GSI/ID QP
 * @cqp_request: pointer to cqp_request of create AH
 */
void irdma_gsi_ud_qp_ah_cb(struct irdma_cqp_request *cqp_request)
{
	struct irdma_sc_ah *sc_ah = cqp_request->param;

	if (!cqp_request->compl_info.op_ret_val)
		sc_ah->ah_info.ah_valid = true;
	else
		sc_ah->ah_info.ah_valid = false;
}

/**
 * irdma_prm_add_pble_mem - add moemory to pble resources
 * @pprm: pble resource manager
 * @pchunk: chunk of memory to add
 */
int irdma_prm_add_pble_mem(struct irdma_pble_prm *pprm,
			   struct irdma_chunk *pchunk)
{
	u64 sizeofbitmap;

	if (pchunk->size & 0xfff)
		return -EINVAL;

	sizeofbitmap = (u64)pchunk->size >> pprm->pble_shift;

	pchunk->bitmapbuf = bitmap_zalloc(sizeofbitmap, GFP_KERNEL);
	if (!pchunk->bitmapbuf)
		return -ENOMEM;

	pchunk->sizeofbitmap = sizeofbitmap;
	/* each pble is 8 bytes hence shift by 3 */
	pprm->total_pble_alloc += pchunk->size >> 3;
	pprm->free_pble_cnt += pchunk->size >> 3;

	return 0;
}

/**
 * irdma_prm_get_pbles - get pble's from prm
 * @pprm: pble resource manager
 * @chunkinfo: nformation about chunk where pble's were acquired
 * @mem_size: size of pble memory needed
 * @vaddr: returns virtual address of pble memory
 * @fpm_addr: returns fpm address of pble memory
 */
int irdma_prm_get_pbles(struct irdma_pble_prm *pprm,
			struct irdma_pble_chunkinfo *chunkinfo, u64 mem_size,
			u64 **vaddr, u64 *fpm_addr)
{
	u64 bits_needed;
	u64 bit_idx = PBLE_INVALID_IDX;
	struct irdma_chunk *pchunk = NULL;
	struct list_head *chunk_entry = pprm->clist.next;
	u32 offset;
	unsigned long flags;
	*vaddr = NULL;
	*fpm_addr = 0;

	bits_needed = DIV_ROUND_UP_ULL(mem_size, BIT_ULL(pprm->pble_shift));

	spin_lock_irqsave(&pprm->prm_lock, flags);
	while (chunk_entry != &pprm->clist) {
		pchunk = (struct irdma_chunk *)chunk_entry;
		bit_idx = bitmap_find_next_zero_area(pchunk->bitmapbuf,
						     pchunk->sizeofbitmap, 0,
						     bits_needed, 0);
		if (bit_idx < pchunk->sizeofbitmap)
			break;

		/* list.next used macro */
		chunk_entry = pchunk->list.next;
	}

	if (!pchunk || bit_idx >= pchunk->sizeofbitmap) {
		spin_unlock_irqrestore(&pprm->prm_lock, flags);
		return -ENOMEM;
	}

	bitmap_set(pchunk->bitmapbuf, bit_idx, bits_needed);
	offset = bit_idx << pprm->pble_shift;
	*vaddr = pchunk->vaddr + offset;
	*fpm_addr = pchunk->fpm_addr + offset;

	chunkinfo->pchunk = pchunk;
	chunkinfo->bit_idx = bit_idx;
	chunkinfo->bits_used = bits_needed;
	/* 3 is sizeof pble divide */
	pprm->free_pble_cnt -= chunkinfo->bits_used << (pprm->pble_shift - 3);
	spin_unlock_irqrestore(&pprm->prm_lock, flags);

	return 0;
}

/**
 * irdma_prm_return_pbles - return pbles back to prm
 * @pprm: pble resource manager
 * @chunkinfo: chunk where pble's were acquired and to be freed
 */
void irdma_prm_return_pbles(struct irdma_pble_prm *pprm,
			    struct irdma_pble_chunkinfo *chunkinfo)
{
	unsigned long flags;

	spin_lock_irqsave(&pprm->prm_lock, flags);
	pprm->free_pble_cnt += chunkinfo->bits_used << (pprm->pble_shift - 3);
	bitmap_clear(chunkinfo->pchunk->bitmapbuf, chunkinfo->bit_idx,
		     chunkinfo->bits_used);
	spin_unlock_irqrestore(&pprm->prm_lock, flags);
}

int irdma_map_vm_page_list(struct irdma_hw *hw, void *va, dma_addr_t *pg_dma,
			   u32 pg_cnt)
{
	struct page *vm_page;
	int i;
	u8 *addr;

	addr = (u8 *)(uintptr_t)va;
	for (i = 0; i < pg_cnt; i++) {
		vm_page = vmalloc_to_page(addr);
		if (!vm_page)
			goto err;

		pg_dma[i] = dma_map_page(hw->device, vm_page, 0, PAGE_SIZE,
					 DMA_BIDIRECTIONAL);
		if (dma_mapping_error(hw->device, pg_dma[i]))
			goto err;

		addr += PAGE_SIZE;
	}

	return 0;

err:
	irdma_unmap_vm_page_list(hw, pg_dma, i);
	return -ENOMEM;
}

void irdma_unmap_vm_page_list(struct irdma_hw *hw, dma_addr_t *pg_dma, u32 pg_cnt)
{
	int i;

	for (i = 0; i < pg_cnt; i++)
		dma_unmap_page(hw->device, pg_dma[i], PAGE_SIZE, DMA_BIDIRECTIONAL);
}

/**
 * irdma_pble_free_paged_mem - free virtual paged memory
 * @chunk: chunk to free with paged memory
 */
void irdma_pble_free_paged_mem(struct irdma_chunk *chunk)
{
	if (!chunk->pg_cnt)
		goto done;

	irdma_unmap_vm_page_list(chunk->dev->hw, chunk->dmainfo.dmaaddrs,
				 chunk->pg_cnt);

done:
	kfree(chunk->dmainfo.dmaaddrs);
	chunk->dmainfo.dmaaddrs = NULL;
	vfree(chunk->vaddr);
	chunk->vaddr = NULL;
	chunk->type = 0;
}

/**
 * irdma_pble_get_paged_mem -allocate paged memory for pbles
 * @chunk: chunk to add for paged memory
 * @pg_cnt: number of pages needed
 */
int irdma_pble_get_paged_mem(struct irdma_chunk *chunk, u32 pg_cnt)
{
	u32 size;
	void *va;

	chunk->dmainfo.dmaaddrs = kzalloc(pg_cnt << 3, GFP_KERNEL);
	if (!chunk->dmainfo.dmaaddrs)
		return -ENOMEM;

	size = PAGE_SIZE * pg_cnt;
	va = vmalloc(size);
	if (!va)
		goto err;

	if (irdma_map_vm_page_list(chunk->dev->hw, va, chunk->dmainfo.dmaaddrs,
				   pg_cnt)) {
		vfree(va);
		goto err;
	}
	chunk->vaddr = va;
	chunk->size = size;
	chunk->pg_cnt = pg_cnt;
	chunk->type = PBLE_SD_PAGED;

	return 0;
err:
	kfree(chunk->dmainfo.dmaaddrs);
	chunk->dmainfo.dmaaddrs = NULL;

	return -ENOMEM;
}

/**
 * irdma_alloc_ws_node_id - Allocate a tx scheduler node ID
 * @dev: device pointer
 */
u16 irdma_alloc_ws_node_id(struct irdma_sc_dev *dev)
{
	struct irdma_pci_f *rf = dev_to_rf(dev);
	u32 next = 1;
	u32 node_id;

	if (irdma_alloc_rsrc(rf, rf->allocated_ws_nodes, rf->max_ws_node_id,
			     &node_id, &next))
		return IRDMA_WS_NODE_INVALID;

	return (u16)node_id;
}

/**
 * irdma_free_ws_node_id - Free a tx scheduler node ID
 * @dev: device pointer
 * @node_id: Work scheduler node ID
 */
void irdma_free_ws_node_id(struct irdma_sc_dev *dev, u16 node_id)
{
	struct irdma_pci_f *rf = dev_to_rf(dev);

	irdma_free_rsrc(rf, rf->allocated_ws_nodes, (u32)node_id);
}

/**
 * irdma_modify_qp_to_err - Modify a QP to error
 * @sc_qp: qp structure
 */
void irdma_modify_qp_to_err(struct irdma_sc_qp *sc_qp)
{
	struct irdma_qp *qp = sc_qp->qp_uk.back_qp;
	struct ib_qp_attr attr;

	if (qp->iwdev->rf->reset)
		return;
	attr.qp_state = IB_QPS_ERR;

	if (rdma_protocol_roce(qp->ibqp.device, 1))
		irdma_modify_qp_roce(&qp->ibqp, &attr, IB_QP_STATE, NULL);
	else
		irdma_modify_qp(&qp->ibqp, &attr, IB_QP_STATE, NULL);
}

void irdma_ib_qp_event(struct irdma_qp *iwqp, enum irdma_qp_event_type event)
{
	struct ib_event ibevent;

	if (!iwqp->ibqp.event_handler)
		return;

	switch (event) {
	case IRDMA_QP_EVENT_CATASTROPHIC:
		ibevent.event = IB_EVENT_QP_FATAL;
		break;
	case IRDMA_QP_EVENT_ACCESS_ERR:
		ibevent.event = IB_EVENT_QP_ACCESS_ERR;
		break;
	}
	ibevent.device = iwqp->ibqp.device;
	ibevent.element.qp = &iwqp->ibqp;
	iwqp->ibqp.event_handler(&ibevent, iwqp->ibqp.qp_context);
}

bool irdma_cq_empty(struct irdma_cq *iwcq)
{
	struct irdma_cq_uk *ukcq;
	u64 qword3;
	__le64 *cqe;
	u8 polarity;

	ukcq  = &iwcq->sc_cq.cq_uk;
	cqe = IRDMA_GET_CURRENT_CQ_ELEM(ukcq);
	get_64bit_val(cqe, 24, &qword3);
	polarity = (u8)FIELD_GET(IRDMA_CQ_VALID, qword3);

	return polarity != ukcq->polarity;
}
