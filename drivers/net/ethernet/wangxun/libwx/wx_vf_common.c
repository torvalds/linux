// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_mbx.h"
#include "wx_lib.h"
#include "wx_vf.h"
#include "wx_vf_lib.h"
#include "wx_vf_common.h"

int wxvf_suspend(struct device *dev_d)
{
	struct pci_dev *pdev = to_pci_dev(dev_d);
	struct wx *wx = pci_get_drvdata(pdev);

	netif_device_detach(wx->netdev);
	wx_clear_interrupt_scheme(wx);
	pci_disable_device(pdev);

	return 0;
}
EXPORT_SYMBOL(wxvf_suspend);

void wxvf_shutdown(struct pci_dev *pdev)
{
	wxvf_suspend(&pdev->dev);
}
EXPORT_SYMBOL(wxvf_shutdown);

int wxvf_resume(struct device *dev_d)
{
	struct pci_dev *pdev = to_pci_dev(dev_d);
	struct wx *wx = pci_get_drvdata(pdev);

	pci_set_master(pdev);
	wx_init_interrupt_scheme(wx);
	netif_device_attach(wx->netdev);

	return 0;
}
EXPORT_SYMBOL(wxvf_resume);

void wxvf_remove(struct pci_dev *pdev)
{
	struct wx *wx = pci_get_drvdata(pdev);
	struct net_device *netdev;

	cancel_work_sync(&wx->service_task);
	netdev = wx->netdev;
	unregister_netdev(netdev);
	kfree(wx->vfinfo);
	kfree(wx->rss_key);
	kfree(wx->mac_table);
	wx_clear_interrupt_scheme(wx);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
	pci_disable_device(pdev);
}
EXPORT_SYMBOL(wxvf_remove);

static irqreturn_t wx_msix_misc_vf(int __always_unused irq, void *data)
{
	struct wx *wx = data;

	set_bit(WX_FLAG_NEED_UPDATE_LINK, wx->flags);
	/* Clear the interrupt */
	if (netif_running(wx->netdev))
		wr32(wx, WX_VXIMC, wx->eims_other);

	return IRQ_HANDLED;
}

int wx_request_msix_irqs_vf(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	int vector, err;

	for (vector = 0; vector < wx->num_q_vectors; vector++) {
		struct wx_q_vector *q_vector = wx->q_vector[vector];
		struct msix_entry *entry = &wx->msix_q_entries[vector];

		if (q_vector->tx.ring && q_vector->rx.ring)
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-TxRx-%d", netdev->name, entry->entry);
		else
			/* skip this unused q_vector */
			continue;

		err = request_irq(entry->vector, wx_msix_clean_rings, 0,
				  q_vector->name, q_vector);
		if (err) {
			wx_err(wx, "request_irq failed for MSIX interrupt %s Error: %d\n",
			       q_vector->name, err);
			goto free_queue_irqs;
		}
	}

	err = request_threaded_irq(wx->msix_entry->vector, wx_msix_misc_vf,
				   NULL, IRQF_ONESHOT, netdev->name, wx);
	if (err) {
		wx_err(wx, "request_irq for msix_other failed: %d\n", err);
		goto free_queue_irqs;
	}

	return 0;

free_queue_irqs:
	while (vector) {
		vector--;
		free_irq(wx->msix_q_entries[vector].vector,
			 wx->q_vector[vector]);
	}
	wx_reset_interrupt_capability(wx);
	return err;
}
EXPORT_SYMBOL(wx_request_msix_irqs_vf);

void wx_negotiate_api_vf(struct wx *wx)
{
	int api[] = {
		     wx_mbox_api_13,
		     wx_mbox_api_null};
	int err = 0, idx = 0;

	spin_lock_bh(&wx->mbx.mbx_lock);
	while (api[idx] != wx_mbox_api_null) {
		err = wx_negotiate_api_version(wx, api[idx]);
		if (!err)
			break;
		idx++;
	}
	spin_unlock_bh(&wx->mbx.mbx_lock);
}
EXPORT_SYMBOL(wx_negotiate_api_vf);

void wx_reset_vf(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	int ret = 0;

	ret = wx_reset_hw_vf(wx);
	if (!ret)
		wx_init_hw_vf(wx);
	wx_negotiate_api_vf(wx);
	if (is_valid_ether_addr(wx->mac.addr)) {
		eth_hw_addr_set(netdev, wx->mac.addr);
		ether_addr_copy(netdev->perm_addr, wx->mac.addr);
	}
}
EXPORT_SYMBOL(wx_reset_vf);

void wx_set_rx_mode_vf(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);
	unsigned int flags = netdev->flags;
	int xcast_mode;

	xcast_mode = (flags & IFF_ALLMULTI) ? WXVF_XCAST_MODE_ALLMULTI :
		     (flags & (IFF_BROADCAST | IFF_MULTICAST)) ?
		     WXVF_XCAST_MODE_MULTI : WXVF_XCAST_MODE_NONE;
	/* request the most inclusive mode we need */
	if (flags & IFF_PROMISC)
		xcast_mode = WXVF_XCAST_MODE_PROMISC;
	else if (flags & IFF_ALLMULTI)
		xcast_mode = WXVF_XCAST_MODE_ALLMULTI;
	else if (flags & (IFF_BROADCAST | IFF_MULTICAST))
		xcast_mode = WXVF_XCAST_MODE_MULTI;
	else
		xcast_mode = WXVF_XCAST_MODE_NONE;

	spin_lock_bh(&wx->mbx.mbx_lock);
	wx_update_xcast_mode_vf(wx, xcast_mode);
	wx_update_mc_addr_list_vf(wx, netdev);
	wx_write_uc_addr_list_vf(netdev);
	spin_unlock_bh(&wx->mbx.mbx_lock);
}
EXPORT_SYMBOL(wx_set_rx_mode_vf);

/**
 * wx_configure_rx_vf - Configure Receive Unit after Reset
 * @wx: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void wx_configure_rx_vf(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	int i, ret;

	wx_setup_psrtype_vf(wx);
	wx_setup_vfmrqc_vf(wx);

	spin_lock_bh(&wx->mbx.mbx_lock);
	ret = wx_rlpml_set_vf(wx,
			      netdev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN);
	spin_unlock_bh(&wx->mbx.mbx_lock);
	if (ret)
		wx_dbg(wx, "Failed to set MTU at %d\n", netdev->mtu);

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring
	 */
	for (i = 0; i < wx->num_rx_queues; i++) {
		struct wx_ring *rx_ring = wx->rx_ring[i];
#ifdef HAVE_SWIOTLB_SKIP_CPU_SYNC
		wx_set_rx_buffer_len_vf(wx, rx_ring);
#endif
		wx_configure_rx_ring_vf(wx, rx_ring);
	}
}

void wx_configure_vf(struct wx *wx)
{
	wx_set_rx_mode_vf(wx->netdev);
	wx_configure_tx_vf(wx);
	wx_configure_rx_vf(wx);
}
EXPORT_SYMBOL(wx_configure_vf);

int wx_set_mac_vf(struct net_device *netdev, void *p)
{
	struct wx *wx = netdev_priv(netdev);
	struct sockaddr *addr = p;
	int ret;

	ret = eth_prepare_mac_addr_change(netdev, addr);
	if (ret)
		return ret;

	spin_lock_bh(&wx->mbx.mbx_lock);
	ret = wx_set_rar_vf(wx, 1, (u8 *)addr->sa_data, 1);
	spin_unlock_bh(&wx->mbx.mbx_lock);

	if (ret)
		return -EPERM;

	memcpy(wx->mac.addr, addr->sa_data, netdev->addr_len);
	memcpy(wx->mac.perm_addr, addr->sa_data, netdev->addr_len);
	eth_hw_addr_set(netdev, addr->sa_data);

	return 0;
}
EXPORT_SYMBOL(wx_set_mac_vf);

void wxvf_watchdog_update_link(struct wx *wx)
{
	int err;

	if (!test_bit(WX_FLAG_NEED_UPDATE_LINK, wx->flags))
		return;

	spin_lock_bh(&wx->mbx.mbx_lock);
	err = wx_check_mac_link_vf(wx);
	spin_unlock_bh(&wx->mbx.mbx_lock);
	if (err) {
		wx->link = false;
		set_bit(WX_FLAG_NEED_DO_RESET, wx->flags);
	}
	clear_bit(WX_FLAG_NEED_UPDATE_LINK, wx->flags);
}
EXPORT_SYMBOL(wxvf_watchdog_update_link);

static void wxvf_irq_enable(struct wx *wx)
{
	wr32(wx, WX_VXIMC, wx->eims_enable_mask);
}

static void wxvf_up_complete(struct wx *wx)
{
	/* Always set the carrier off */
	netif_carrier_off(wx->netdev);
	mod_timer(&wx->service_timer, jiffies + HZ);
	set_bit(WX_FLAG_NEED_UPDATE_LINK, wx->flags);

	wx_configure_msix_vf(wx);
	smp_mb__before_atomic();
	wx_napi_enable_all(wx);

	/* clear any pending interrupts, may auto mask */
	wr32(wx, WX_VXICR, U32_MAX);
	wxvf_irq_enable(wx);
	/* enable transmits */
	netif_tx_start_all_queues(wx->netdev);
}

int wxvf_open(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);
	int err;

	err = wx_setup_resources(wx);
	if (err)
		goto err_reset;
	wx_configure_vf(wx);

	err = wx_request_msix_irqs_vf(wx);
	if (err)
		goto err_free_resources;

	/* Notify the stack of the actual queue counts. */
	err = netif_set_real_num_tx_queues(netdev, wx->num_tx_queues);
	if (err)
		goto err_free_irq;

	err = netif_set_real_num_rx_queues(netdev, wx->num_rx_queues);
	if (err)
		goto err_free_irq;

	wxvf_up_complete(wx);

	return 0;
err_free_irq:
	wx_free_irq(wx);
err_free_resources:
	wx_free_resources(wx);
err_reset:
	wx_reset_vf(wx);
	return err;
}
EXPORT_SYMBOL(wxvf_open);

static void wxvf_down(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;

	timer_delete_sync(&wx->service_timer);
	netif_tx_stop_all_queues(netdev);
	netif_tx_disable(netdev);
	netif_carrier_off(netdev);
	wx_napi_disable_all(wx);
	wx_reset_vf(wx);

	wx_clean_all_tx_rings(wx);
	wx_clean_all_rx_rings(wx);
}

static void wxvf_reinit_locked(struct wx *wx)
{
	while (test_and_set_bit(WX_STATE_RESETTING, wx->state))
		usleep_range(1000, 2000);
	wxvf_down(wx);
	wx_free_irq(wx);
	wx_configure_vf(wx);
	wx_request_msix_irqs_vf(wx);
	wxvf_up_complete(wx);
	clear_bit(WX_STATE_RESETTING, wx->state);
}

static void wxvf_reset_subtask(struct wx *wx)
{
	if (!test_bit(WX_FLAG_NEED_DO_RESET, wx->flags))
		return;
	clear_bit(WX_FLAG_NEED_DO_RESET, wx->flags);

	rtnl_lock();
	if (test_bit(WX_STATE_RESETTING, wx->state) ||
	    !(netif_running(wx->netdev))) {
		rtnl_unlock();
		return;
	}
	wxvf_reinit_locked(wx);
	rtnl_unlock();
}

int wxvf_close(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);

	wxvf_down(wx);
	wx_free_irq(wx);
	wx_free_resources(wx);

	return 0;
}
EXPORT_SYMBOL(wxvf_close);

static void wxvf_link_config_subtask(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;

	wxvf_watchdog_update_link(wx);
	if (wx->link) {
		if (netif_carrier_ok(netdev))
			return;
		netif_carrier_on(netdev);
		netdev_info(netdev, "Link is Up - %s\n",
			    phy_speed_to_str(wx->speed));
	} else {
		if (!netif_carrier_ok(netdev))
			return;
		netif_carrier_off(netdev);
		netdev_info(netdev, "Link is Down\n");
	}
}

static void wxvf_service_task(struct work_struct *work)
{
	struct wx *wx = container_of(work, struct wx, service_task);

	wxvf_link_config_subtask(wx);
	wxvf_reset_subtask(wx);
	wx_service_event_complete(wx);
}

void wxvf_init_service(struct wx *wx)
{
	timer_setup(&wx->service_timer, wx_service_timer, 0);
	INIT_WORK(&wx->service_task, wxvf_service_task);
	clear_bit(WX_STATE_SERVICE_SCHED, wx->state);
}
EXPORT_SYMBOL(wxvf_init_service);
