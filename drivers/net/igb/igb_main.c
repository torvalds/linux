/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/netdevice.h>
#include <linux/ipv6.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#ifdef CONFIG_DCA
#include <linux/dca.h>
#endif
#include "igb.h"

#define DRV_VERSION "1.2.45-k2"
char igb_driver_name[] = "igb";
char igb_driver_version[] = DRV_VERSION;
static const char igb_driver_string[] =
				"Intel(R) Gigabit Ethernet Network Driver";
static const char igb_copyright[] = "Copyright (c) 2008 Intel Corporation.";

static const struct e1000_info *igb_info_tbl[] = {
	[board_82575] = &e1000_82575_info,
};

static struct pci_device_id igb_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_FIBER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82575EB_COPPER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82575EB_FIBER_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82575GB_QUAD_COPPER), board_82575 },
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, igb_pci_tbl);

void igb_reset(struct igb_adapter *);
static int igb_setup_all_tx_resources(struct igb_adapter *);
static int igb_setup_all_rx_resources(struct igb_adapter *);
static void igb_free_all_tx_resources(struct igb_adapter *);
static void igb_free_all_rx_resources(struct igb_adapter *);
static void igb_free_tx_resources(struct igb_ring *);
static void igb_free_rx_resources(struct igb_ring *);
void igb_update_stats(struct igb_adapter *);
static int igb_probe(struct pci_dev *, const struct pci_device_id *);
static void __devexit igb_remove(struct pci_dev *pdev);
static int igb_sw_init(struct igb_adapter *);
static int igb_open(struct net_device *);
static int igb_close(struct net_device *);
static void igb_configure_tx(struct igb_adapter *);
static void igb_configure_rx(struct igb_adapter *);
static void igb_setup_rctl(struct igb_adapter *);
static void igb_clean_all_tx_rings(struct igb_adapter *);
static void igb_clean_all_rx_rings(struct igb_adapter *);
static void igb_clean_tx_ring(struct igb_ring *);
static void igb_clean_rx_ring(struct igb_ring *);
static void igb_set_multi(struct net_device *);
static void igb_update_phy_info(unsigned long);
static void igb_watchdog(unsigned long);
static void igb_watchdog_task(struct work_struct *);
static int igb_xmit_frame_ring_adv(struct sk_buff *, struct net_device *,
				  struct igb_ring *);
static int igb_xmit_frame_adv(struct sk_buff *skb, struct net_device *);
static struct net_device_stats *igb_get_stats(struct net_device *);
static int igb_change_mtu(struct net_device *, int);
static int igb_set_mac(struct net_device *, void *);
static irqreturn_t igb_intr(int irq, void *);
static irqreturn_t igb_intr_msi(int irq, void *);
static irqreturn_t igb_msix_other(int irq, void *);
static irqreturn_t igb_msix_rx(int irq, void *);
static irqreturn_t igb_msix_tx(int irq, void *);
static int igb_clean_rx_ring_msix(struct napi_struct *, int);
#ifdef CONFIG_DCA
static void igb_update_rx_dca(struct igb_ring *);
static void igb_update_tx_dca(struct igb_ring *);
static void igb_setup_dca(struct igb_adapter *);
#endif /* CONFIG_DCA */
static bool igb_clean_tx_irq(struct igb_ring *);
static int igb_poll(struct napi_struct *, int);
static bool igb_clean_rx_irq_adv(struct igb_ring *, int *, int);
static void igb_alloc_rx_buffers_adv(struct igb_ring *, int);
#ifdef CONFIG_IGB_LRO
static int igb_get_skb_hdr(struct sk_buff *skb, void **, void **, u64 *, void *);
#endif
static int igb_ioctl(struct net_device *, struct ifreq *, int cmd);
static void igb_tx_timeout(struct net_device *);
static void igb_reset_task(struct work_struct *);
static void igb_vlan_rx_register(struct net_device *, struct vlan_group *);
static void igb_vlan_rx_add_vid(struct net_device *, u16);
static void igb_vlan_rx_kill_vid(struct net_device *, u16);
static void igb_restore_vlan(struct igb_adapter *);

static int igb_suspend(struct pci_dev *, pm_message_t);
#ifdef CONFIG_PM
static int igb_resume(struct pci_dev *);
#endif
static void igb_shutdown(struct pci_dev *);
#ifdef CONFIG_DCA
static int igb_notify_dca(struct notifier_block *, unsigned long, void *);
static struct notifier_block dca_notifier = {
	.notifier_call	= igb_notify_dca,
	.next		= NULL,
	.priority	= 0
};
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
/* for netdump / net console */
static void igb_netpoll(struct net_device *);
#endif

static pci_ers_result_t igb_io_error_detected(struct pci_dev *,
		     pci_channel_state_t);
static pci_ers_result_t igb_io_slot_reset(struct pci_dev *);
static void igb_io_resume(struct pci_dev *);

static struct pci_error_handlers igb_err_handler = {
	.error_detected = igb_io_error_detected,
	.slot_reset = igb_io_slot_reset,
	.resume = igb_io_resume,
};


static struct pci_driver igb_driver = {
	.name     = igb_driver_name,
	.id_table = igb_pci_tbl,
	.probe    = igb_probe,
	.remove   = __devexit_p(igb_remove),
#ifdef CONFIG_PM
	/* Power Managment Hooks */
	.suspend  = igb_suspend,
	.resume   = igb_resume,
#endif
	.shutdown = igb_shutdown,
	.err_handler = &igb_err_handler
};

static int global_quad_port_a; /* global quad port a indication */

MODULE_AUTHOR("Intel Corporation, <e1000-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Intel(R) Gigabit Ethernet Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

#ifdef DEBUG
/**
 * igb_get_hw_dev_name - return device name string
 * used by hardware layer to print debugging information
 **/
char *igb_get_hw_dev_name(struct e1000_hw *hw)
{
	struct igb_adapter *adapter = hw->back;
	return adapter->netdev->name;
}
#endif

/**
 * igb_init_module - Driver Registration Routine
 *
 * igb_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init igb_init_module(void)
{
	int ret;
	printk(KERN_INFO "%s - version %s\n",
	       igb_driver_string, igb_driver_version);

	printk(KERN_INFO "%s\n", igb_copyright);

	global_quad_port_a = 0;

	ret = pci_register_driver(&igb_driver);
#ifdef CONFIG_DCA
	dca_register_notify(&dca_notifier);
#endif
	return ret;
}

module_init(igb_init_module);

/**
 * igb_exit_module - Driver Exit Cleanup Routine
 *
 * igb_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit igb_exit_module(void)
{
#ifdef CONFIG_DCA
	dca_unregister_notify(&dca_notifier);
#endif
	pci_unregister_driver(&igb_driver);
}

module_exit(igb_exit_module);

/**
 * igb_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 * We allocate one ring per queue at run-time since we don't know the
 * number of queues at compile-time.
 **/
static int igb_alloc_queues(struct igb_adapter *adapter)
{
	int i;

	adapter->tx_ring = kcalloc(adapter->num_tx_queues,
				   sizeof(struct igb_ring), GFP_KERNEL);
	if (!adapter->tx_ring)
		return -ENOMEM;

	adapter->rx_ring = kcalloc(adapter->num_rx_queues,
				   sizeof(struct igb_ring), GFP_KERNEL);
	if (!adapter->rx_ring) {
		kfree(adapter->tx_ring);
		return -ENOMEM;
	}

	adapter->rx_ring->buddy = adapter->tx_ring;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igb_ring *ring = &(adapter->tx_ring[i]);
		ring->adapter = adapter;
		ring->queue_index = i;
	}
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct igb_ring *ring = &(adapter->rx_ring[i]);
		ring->adapter = adapter;
		ring->queue_index = i;
		ring->itr_register = E1000_ITR;

		/* set a default napi handler for each rx_ring */
		netif_napi_add(adapter->netdev, &ring->napi, igb_poll, 64);
	}
	return 0;
}

static void igb_free_queues(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		netif_napi_del(&adapter->rx_ring[i].napi);

	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);
}

#define IGB_N0_QUEUE -1
static void igb_assign_vector(struct igb_adapter *adapter, int rx_queue,
			      int tx_queue, int msix_vector)
{
	u32 msixbm = 0;
	struct e1000_hw *hw = &adapter->hw;
	u32 ivar, index;

	switch (hw->mac.type) {
	case e1000_82575:
		/* The 82575 assigns vectors using a bitmask, which matches the
		   bitmask for the EICR/EIMS/EIMC registers.  To assign one
		   or more queues to a vector, we write the appropriate bits
		   into the MSIXBM register for that vector. */
		if (rx_queue > IGB_N0_QUEUE) {
			msixbm = E1000_EICR_RX_QUEUE0 << rx_queue;
			adapter->rx_ring[rx_queue].eims_value = msixbm;
		}
		if (tx_queue > IGB_N0_QUEUE) {
			msixbm |= E1000_EICR_TX_QUEUE0 << tx_queue;
			adapter->tx_ring[tx_queue].eims_value =
				  E1000_EICR_TX_QUEUE0 << tx_queue;
		}
		array_wr32(E1000_MSIXBM(0), msix_vector, msixbm);
		break;
	case e1000_82576:
		/* The 82576 uses a table-based method for assigning vectors.
		   Each queue has a single entry in the table to which we write
		   a vector number along with a "valid" bit.  Sadly, the layout
		   of the table is somewhat counterintuitive. */
		if (rx_queue > IGB_N0_QUEUE) {
			index = (rx_queue & 0x7);
			ivar = array_rd32(E1000_IVAR0, index);
			if (rx_queue < 8) {
				/* vector goes into low byte of register */
				ivar = ivar & 0xFFFFFF00;
				ivar |= msix_vector | E1000_IVAR_VALID;
			} else {
				/* vector goes into third byte of register */
				ivar = ivar & 0xFF00FFFF;
				ivar |= (msix_vector | E1000_IVAR_VALID) << 16;
			}
			adapter->rx_ring[rx_queue].eims_value= 1 << msix_vector;
			array_wr32(E1000_IVAR0, index, ivar);
		}
		if (tx_queue > IGB_N0_QUEUE) {
			index = (tx_queue & 0x7);
			ivar = array_rd32(E1000_IVAR0, index);
			if (tx_queue < 8) {
				/* vector goes into second byte of register */
				ivar = ivar & 0xFFFF00FF;
				ivar |= (msix_vector | E1000_IVAR_VALID) << 8;
			} else {
				/* vector goes into high byte of register */
				ivar = ivar & 0x00FFFFFF;
				ivar |= (msix_vector | E1000_IVAR_VALID) << 24;
			}
			adapter->tx_ring[tx_queue].eims_value= 1 << msix_vector;
			array_wr32(E1000_IVAR0, index, ivar);
		}
		break;
	default:
		BUG();
		break;
	}
}

/**
 * igb_configure_msix - Configure MSI-X hardware
 *
 * igb_configure_msix sets up the hardware to properly
 * generate MSI-X interrupts.
 **/
static void igb_configure_msix(struct igb_adapter *adapter)
{
	u32 tmp;
	int i, vector = 0;
	struct e1000_hw *hw = &adapter->hw;

	adapter->eims_enable_mask = 0;
	if (hw->mac.type == e1000_82576)
		/* Turn on MSI-X capability first, or our settings
		 * won't stick.  And it will take days to debug. */
		wr32(E1000_GPIE, E1000_GPIE_MSIX_MODE |
				   E1000_GPIE_PBA | E1000_GPIE_EIAME | 
 				   E1000_GPIE_NSICR);

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igb_ring *tx_ring = &adapter->tx_ring[i];
		igb_assign_vector(adapter, IGB_N0_QUEUE, i, vector++);
		adapter->eims_enable_mask |= tx_ring->eims_value;
		if (tx_ring->itr_val)
			writel(tx_ring->itr_val,
			       hw->hw_addr + tx_ring->itr_register);
		else
			writel(1, hw->hw_addr + tx_ring->itr_register);
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct igb_ring *rx_ring = &adapter->rx_ring[i];
		rx_ring->buddy = NULL;
		igb_assign_vector(adapter, i, IGB_N0_QUEUE, vector++);
		adapter->eims_enable_mask |= rx_ring->eims_value;
		if (rx_ring->itr_val)
			writel(rx_ring->itr_val,
			       hw->hw_addr + rx_ring->itr_register);
		else
			writel(1, hw->hw_addr + rx_ring->itr_register);
	}


	/* set vector for other causes, i.e. link changes */
	switch (hw->mac.type) {
	case e1000_82575:
		array_wr32(E1000_MSIXBM(0), vector++,
				      E1000_EIMS_OTHER);

		tmp = rd32(E1000_CTRL_EXT);
		/* enable MSI-X PBA support*/
		tmp |= E1000_CTRL_EXT_PBA_CLR;

		/* Auto-Mask interrupts upon ICR read. */
		tmp |= E1000_CTRL_EXT_EIAME;
		tmp |= E1000_CTRL_EXT_IRCA;

		wr32(E1000_CTRL_EXT, tmp);
		adapter->eims_enable_mask |= E1000_EIMS_OTHER;
		adapter->eims_other = E1000_EIMS_OTHER;

		break;

	case e1000_82576:
		tmp = (vector++ | E1000_IVAR_VALID) << 8;
		wr32(E1000_IVAR_MISC, tmp);

		adapter->eims_enable_mask = (1 << (vector)) - 1;
		adapter->eims_other = 1 << (vector - 1);
		break;
	default:
		/* do nothing, since nothing else supports MSI-X */
		break;
	} /* switch (hw->mac.type) */
	wrfl();
}

/**
 * igb_request_msix - Initialize MSI-X interrupts
 *
 * igb_request_msix allocates MSI-X vectors and requests interrupts from the
 * kernel.
 **/
static int igb_request_msix(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i, err = 0, vector = 0;

	vector = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igb_ring *ring = &(adapter->tx_ring[i]);
		sprintf(ring->name, "%s-tx%d", netdev->name, i);
		err = request_irq(adapter->msix_entries[vector].vector,
				  &igb_msix_tx, 0, ring->name,
				  &(adapter->tx_ring[i]));
		if (err)
			goto out;
		ring->itr_register = E1000_EITR(0) + (vector << 2);
		ring->itr_val = 976; /* ~4000 ints/sec */
		vector++;
	}
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct igb_ring *ring = &(adapter->rx_ring[i]);
		if (strlen(netdev->name) < (IFNAMSIZ - 5))
			sprintf(ring->name, "%s-rx%d", netdev->name, i);
		else
			memcpy(ring->name, netdev->name, IFNAMSIZ);
		err = request_irq(adapter->msix_entries[vector].vector,
				  &igb_msix_rx, 0, ring->name,
				  &(adapter->rx_ring[i]));
		if (err)
			goto out;
		ring->itr_register = E1000_EITR(0) + (vector << 2);
		ring->itr_val = adapter->itr;
		/* overwrite the poll routine for MSIX, we've already done
		 * netif_napi_add */
		ring->napi.poll = &igb_clean_rx_ring_msix;
		vector++;
	}

	err = request_irq(adapter->msix_entries[vector].vector,
			  &igb_msix_other, 0, netdev->name, netdev);
	if (err)
		goto out;

	igb_configure_msix(adapter);
	return 0;
out:
	return err;
}

static void igb_reset_interrupt_capability(struct igb_adapter *adapter)
{
	if (adapter->msix_entries) {
		pci_disable_msix(adapter->pdev);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
	} else if (adapter->flags & IGB_FLAG_HAS_MSI)
		pci_disable_msi(adapter->pdev);
	return;
}


/**
 * igb_set_interrupt_capability - set MSI or MSI-X if supported
 *
 * Attempt to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static void igb_set_interrupt_capability(struct igb_adapter *adapter)
{
	int err;
	int numvecs, i;

	numvecs = adapter->num_tx_queues + adapter->num_rx_queues + 1;
	adapter->msix_entries = kcalloc(numvecs, sizeof(struct msix_entry),
					GFP_KERNEL);
	if (!adapter->msix_entries)
		goto msi_only;

	for (i = 0; i < numvecs; i++)
		adapter->msix_entries[i].entry = i;

	err = pci_enable_msix(adapter->pdev,
			      adapter->msix_entries,
			      numvecs);
	if (err == 0)
		goto out;

	igb_reset_interrupt_capability(adapter);

	/* If we can't do MSI-X, try MSI */
msi_only:
	adapter->num_rx_queues = 1;
	adapter->num_tx_queues = 1;
	if (!pci_enable_msi(adapter->pdev))
		adapter->flags |= IGB_FLAG_HAS_MSI;
out:
	/* Notify the stack of the (possibly) reduced Tx Queue count. */
	adapter->netdev->real_num_tx_queues = adapter->num_tx_queues;
	return;
}

/**
 * igb_request_irq - initialize interrupts
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static int igb_request_irq(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	int err = 0;

	if (adapter->msix_entries) {
		err = igb_request_msix(adapter);
		if (!err)
			goto request_done;
		/* fall back to MSI */
		igb_reset_interrupt_capability(adapter);
		if (!pci_enable_msi(adapter->pdev))
			adapter->flags |= IGB_FLAG_HAS_MSI;
		igb_free_all_tx_resources(adapter);
		igb_free_all_rx_resources(adapter);
		adapter->num_rx_queues = 1;
		igb_alloc_queues(adapter);
	} else {
		switch (hw->mac.type) {
		case e1000_82575:
			wr32(E1000_MSIXBM(0),
			     (E1000_EICR_RX_QUEUE0 | E1000_EIMS_OTHER));
			break;
		case e1000_82576:
			wr32(E1000_IVAR0, E1000_IVAR_VALID);
			break;
		default:
			break;
		}
	}

	if (adapter->flags & IGB_FLAG_HAS_MSI) {
		err = request_irq(adapter->pdev->irq, &igb_intr_msi, 0,
				  netdev->name, netdev);
		if (!err)
			goto request_done;
		/* fall back to legacy interrupts */
		igb_reset_interrupt_capability(adapter);
		adapter->flags &= ~IGB_FLAG_HAS_MSI;
	}

	err = request_irq(adapter->pdev->irq, &igb_intr, IRQF_SHARED,
			  netdev->name, netdev);

	if (err)
		dev_err(&adapter->pdev->dev, "Error %d getting interrupt\n",
			err);

request_done:
	return err;
}

static void igb_free_irq(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (adapter->msix_entries) {
		int vector = 0, i;

		for (i = 0; i < adapter->num_tx_queues; i++)
			free_irq(adapter->msix_entries[vector++].vector,
				&(adapter->tx_ring[i]));
		for (i = 0; i < adapter->num_rx_queues; i++)
			free_irq(adapter->msix_entries[vector++].vector,
				&(adapter->rx_ring[i]));

		free_irq(adapter->msix_entries[vector++].vector, netdev);
		return;
	}

	free_irq(adapter->pdev->irq, netdev);
}

/**
 * igb_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/
static void igb_irq_disable(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	if (adapter->msix_entries) {
		wr32(E1000_EIAM, 0);
		wr32(E1000_EIMC, ~0);
		wr32(E1000_EIAC, 0);
	}

	wr32(E1000_IAM, 0);
	wr32(E1000_IMC, ~0);
	wrfl();
	synchronize_irq(adapter->pdev->irq);
}

/**
 * igb_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/
static void igb_irq_enable(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	if (adapter->msix_entries) {
		wr32(E1000_EIAC, adapter->eims_enable_mask);
		wr32(E1000_EIAM, adapter->eims_enable_mask);
		wr32(E1000_EIMS, adapter->eims_enable_mask);
		wr32(E1000_IMS, E1000_IMS_LSC);
	} else {
		wr32(E1000_IMS, IMS_ENABLE_MASK);
		wr32(E1000_IAM, IMS_ENABLE_MASK);
	}
}

static void igb_update_mng_vlan(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u16 vid = adapter->hw.mng_cookie.vlan_id;
	u16 old_vid = adapter->mng_vlan_id;
	if (adapter->vlgrp) {
		if (!vlan_group_get_device(adapter->vlgrp, vid)) {
			if (adapter->hw.mng_cookie.status &
				E1000_MNG_DHCP_COOKIE_STATUS_VLAN) {
				igb_vlan_rx_add_vid(netdev, vid);
				adapter->mng_vlan_id = vid;
			} else
				adapter->mng_vlan_id = IGB_MNG_VLAN_NONE;

			if ((old_vid != (u16)IGB_MNG_VLAN_NONE) &&
					(vid != old_vid) &&
			    !vlan_group_get_device(adapter->vlgrp, old_vid))
				igb_vlan_rx_kill_vid(netdev, old_vid);
		} else
			adapter->mng_vlan_id = vid;
	}
}

/**
 * igb_release_hw_control - release control of the h/w to f/w
 * @adapter: address of board private structure
 *
 * igb_release_hw_control resets CTRL_EXT:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that the
 * driver is no longer loaded.
 *
 **/
static void igb_release_hw_control(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_ext;

	/* Let firmware take over control of h/w */
	ctrl_ext = rd32(E1000_CTRL_EXT);
	wr32(E1000_CTRL_EXT,
			ctrl_ext & ~E1000_CTRL_EXT_DRV_LOAD);
}


/**
 * igb_get_hw_control - get control of the h/w from f/w
 * @adapter: address of board private structure
 *
 * igb_get_hw_control sets CTRL_EXT:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that
 * the driver is loaded.
 *
 **/
static void igb_get_hw_control(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_ext;

	/* Let firmware know the driver has taken over */
	ctrl_ext = rd32(E1000_CTRL_EXT);
	wr32(E1000_CTRL_EXT,
			ctrl_ext | E1000_CTRL_EXT_DRV_LOAD);
}

/**
 * igb_configure - configure the hardware for RX and TX
 * @adapter: private board structure
 **/
static void igb_configure(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;

	igb_get_hw_control(adapter);
	igb_set_multi(netdev);

	igb_restore_vlan(adapter);

	igb_configure_tx(adapter);
	igb_setup_rctl(adapter);
	igb_configure_rx(adapter);

	igb_rx_fifo_flush_82575(&adapter->hw);

	/* call IGB_DESC_UNUSED which always leaves
	 * at least 1 descriptor unused to make sure
	 * next_to_use != next_to_clean */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct igb_ring *ring = &adapter->rx_ring[i];
		igb_alloc_rx_buffers_adv(ring, IGB_DESC_UNUSED(ring));
	}


	adapter->tx_queue_len = netdev->tx_queue_len;
}


/**
 * igb_up - Open the interface and prepare it to handle traffic
 * @adapter: board private structure
 **/

int igb_up(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	int i;

	/* hardware has been reset, we need to reload some things */
	igb_configure(adapter);

	clear_bit(__IGB_DOWN, &adapter->state);

	for (i = 0; i < adapter->num_rx_queues; i++)
		napi_enable(&adapter->rx_ring[i].napi);
	if (adapter->msix_entries)
		igb_configure_msix(adapter);

	/* Clear any pending interrupts. */
	rd32(E1000_ICR);
	igb_irq_enable(adapter);

	/* Fire a link change interrupt to start the watchdog. */
	wr32(E1000_ICS, E1000_ICS_LSC);
	return 0;
}

void igb_down(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 tctl, rctl;
	int i;

	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer */
	set_bit(__IGB_DOWN, &adapter->state);

	/* disable receives in the hardware */
	rctl = rd32(E1000_RCTL);
	wr32(E1000_RCTL, rctl & ~E1000_RCTL_EN);
	/* flush and sleep below */

	netif_tx_stop_all_queues(netdev);

	/* disable transmits in the hardware */
	tctl = rd32(E1000_TCTL);
	tctl &= ~E1000_TCTL_EN;
	wr32(E1000_TCTL, tctl);
	/* flush both disables and wait for them to finish */
	wrfl();
	msleep(10);

	for (i = 0; i < adapter->num_rx_queues; i++)
		napi_disable(&adapter->rx_ring[i].napi);

	igb_irq_disable(adapter);

	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

	netdev->tx_queue_len = adapter->tx_queue_len;
	netif_carrier_off(netdev);
	adapter->link_speed = 0;
	adapter->link_duplex = 0;

	if (!pci_channel_offline(adapter->pdev))
		igb_reset(adapter);
	igb_clean_all_tx_rings(adapter);
	igb_clean_all_rx_rings(adapter);
}

void igb_reinit_locked(struct igb_adapter *adapter)
{
	WARN_ON(in_interrupt());
	while (test_and_set_bit(__IGB_RESETTING, &adapter->state))
		msleep(1);
	igb_down(adapter);
	igb_up(adapter);
	clear_bit(__IGB_RESETTING, &adapter->state);
}

void igb_reset(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_fc_info *fc = &hw->fc;
	u32 pba = 0, tx_space, min_tx_space, min_rx_space;
	u16 hwm;

	/* Repartition Pba for greater than 9k mtu
	 * To take effect CTRL.RST is required.
	 */
	if (mac->type != e1000_82576) {
	pba = E1000_PBA_34K;
	}
	else {
		pba = E1000_PBA_64K;
	}

	if ((adapter->max_frame_size > ETH_FRAME_LEN + ETH_FCS_LEN) &&
	    (mac->type < e1000_82576)) {
		/* adjust PBA for jumbo frames */
		wr32(E1000_PBA, pba);

		/* To maintain wire speed transmits, the Tx FIFO should be
		 * large enough to accommodate two full transmit packets,
		 * rounded up to the next 1KB and expressed in KB.  Likewise,
		 * the Rx FIFO should be large enough to accommodate at least
		 * one full receive packet and is similarly rounded up and
		 * expressed in KB. */
		pba = rd32(E1000_PBA);
		/* upper 16 bits has Tx packet buffer allocation size in KB */
		tx_space = pba >> 16;
		/* lower 16 bits has Rx packet buffer allocation size in KB */
		pba &= 0xffff;
		/* the tx fifo also stores 16 bytes of information about the tx
		 * but don't include ethernet FCS because hardware appends it */
		min_tx_space = (adapter->max_frame_size +
				sizeof(struct e1000_tx_desc) -
				ETH_FCS_LEN) * 2;
		min_tx_space = ALIGN(min_tx_space, 1024);
		min_tx_space >>= 10;
		/* software strips receive CRC, so leave room for it */
		min_rx_space = adapter->max_frame_size;
		min_rx_space = ALIGN(min_rx_space, 1024);
		min_rx_space >>= 10;

		/* If current Tx allocation is less than the min Tx FIFO size,
		 * and the min Tx FIFO size is less than the current Rx FIFO
		 * allocation, take space away from current Rx allocation */
		if (tx_space < min_tx_space &&
		    ((min_tx_space - tx_space) < pba)) {
			pba = pba - (min_tx_space - tx_space);

			/* if short on rx space, rx wins and must trump tx
			 * adjustment */
			if (pba < min_rx_space)
				pba = min_rx_space;
		}
		wr32(E1000_PBA, pba);
	}

	/* flow control settings */
	/* The high water mark must be low enough to fit one full frame
	 * (or the size used for early receive) above it in the Rx FIFO.
	 * Set it to the lower of:
	 * - 90% of the Rx FIFO size, or
	 * - the full Rx FIFO size minus one full frame */
	hwm = min(((pba << 10) * 9 / 10),
			((pba << 10) - 2 * adapter->max_frame_size));

	if (mac->type < e1000_82576) {
		fc->high_water = hwm & 0xFFF8;	/* 8-byte granularity */
		fc->low_water = fc->high_water - 8;
	} else {
		fc->high_water = hwm & 0xFFF0;	/* 16-byte granularity */
		fc->low_water = fc->high_water - 16;
	}
	fc->pause_time = 0xFFFF;
	fc->send_xon = 1;
	fc->type = fc->original_type;

	/* Allow time for pending master requests to run */
	adapter->hw.mac.ops.reset_hw(&adapter->hw);
	wr32(E1000_WUC, 0);

	if (adapter->hw.mac.ops.init_hw(&adapter->hw))
		dev_err(&adapter->pdev->dev, "Hardware Error\n");

	igb_update_mng_vlan(adapter);

	/* Enable h/w to recognize an 802.1Q VLAN Ethernet packet */
	wr32(E1000_VET, ETHERNET_IEEE_VLAN_TYPE);

	igb_reset_adaptive(&adapter->hw);
	if (adapter->hw.phy.ops.get_phy_info)
		adapter->hw.phy.ops.get_phy_info(&adapter->hw);
}

/**
 * igb_is_need_ioport - determine if an adapter needs ioport resources or not
 * @pdev: PCI device information struct
 *
 * Returns true if an adapter needs ioport resources
 **/
static int igb_is_need_ioport(struct pci_dev *pdev)
{
	switch (pdev->device) {
	/* Currently there are no adapters that need ioport resources */
	default:
		return false;
	}
}

/**
 * igb_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in igb_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * igb_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int __devinit igb_probe(struct pci_dev *pdev,
			       const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct igb_adapter *adapter;
	struct e1000_hw *hw;
	const struct e1000_info *ei = igb_info_tbl[ent->driver_data];
	unsigned long mmio_start, mmio_len;
	int i, err, pci_using_dac;
	u16 eeprom_data = 0;
	u16 eeprom_apme_mask = IGB_EEPROM_APME;
	u32 part_num;
	int bars, need_ioport;

	/* do not allocate ioport bars when not needed */
	need_ioport = igb_is_need_ioport(pdev);
	if (need_ioport) {
		bars = pci_select_bars(pdev, IORESOURCE_MEM | IORESOURCE_IO);
		err = pci_enable_device(pdev);
	} else {
		bars = pci_select_bars(pdev, IORESOURCE_MEM);
		err = pci_enable_device_mem(pdev);
	}
	if (err)
		return err;

	pci_using_dac = 0;
	err = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
	if (!err) {
		err = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
		if (!err)
			pci_using_dac = 1;
	} else {
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (err) {
			err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
			if (err) {
				dev_err(&pdev->dev, "No usable DMA "
					"configuration, aborting\n");
				goto err_dma;
			}
		}
	}

	err = pci_request_selected_regions(pdev, bars, igb_driver_name);
	if (err)
		goto err_pci_reg;

	pci_set_master(pdev);
	pci_save_state(pdev);

	err = -ENOMEM;
	netdev = alloc_etherdev_mq(sizeof(struct igb_adapter), IGB_MAX_TX_QUEUES);
	if (!netdev)
		goto err_alloc_etherdev;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	hw = &adapter->hw;
	hw->back = adapter;
	adapter->msg_enable = NETIF_MSG_DRV | NETIF_MSG_PROBE;
	adapter->bars = bars;
	adapter->need_ioport = need_ioport;

	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);

	err = -EIO;
	adapter->hw.hw_addr = ioremap(mmio_start, mmio_len);
	if (!adapter->hw.hw_addr)
		goto err_ioremap;

	netdev->open = &igb_open;
	netdev->stop = &igb_close;
	netdev->get_stats = &igb_get_stats;
	netdev->set_multicast_list = &igb_set_multi;
	netdev->set_mac_address = &igb_set_mac;
	netdev->change_mtu = &igb_change_mtu;
	netdev->do_ioctl = &igb_ioctl;
	igb_set_ethtool_ops(netdev);
	netdev->tx_timeout = &igb_tx_timeout;
	netdev->watchdog_timeo = 5 * HZ;
	netdev->vlan_rx_register = igb_vlan_rx_register;
	netdev->vlan_rx_add_vid = igb_vlan_rx_add_vid;
	netdev->vlan_rx_kill_vid = igb_vlan_rx_kill_vid;
#ifdef CONFIG_NET_POLL_CONTROLLER
	netdev->poll_controller = igb_netpoll;
#endif
	netdev->hard_start_xmit = &igb_xmit_frame_adv;

	strncpy(netdev->name, pci_name(pdev), sizeof(netdev->name) - 1);

	netdev->mem_start = mmio_start;
	netdev->mem_end = mmio_start + mmio_len;

	/* PCI config space info */
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->revision_id = pdev->revision;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;

	/* setup the private structure */
	hw->back = adapter;
	/* Copy the default MAC, PHY and NVM function pointers */
	memcpy(&hw->mac.ops, ei->mac_ops, sizeof(hw->mac.ops));
	memcpy(&hw->phy.ops, ei->phy_ops, sizeof(hw->phy.ops));
	memcpy(&hw->nvm.ops, ei->nvm_ops, sizeof(hw->nvm.ops));
	/* Initialize skew-specific constants */
	err = ei->get_invariants(hw);
	if (err)
		goto err_hw_init;

	err = igb_sw_init(adapter);
	if (err)
		goto err_sw_init;

	igb_get_bus_info_pcie(hw);

	/* set flags */
	switch (hw->mac.type) {
	case e1000_82576:
	case e1000_82575:
		adapter->flags |= IGB_FLAG_HAS_DCA;
		adapter->flags |= IGB_FLAG_NEED_CTX_IDX;
		break;
	default:
		break;
	}

	hw->phy.autoneg_wait_to_complete = false;
	hw->mac.adaptive_ifs = true;

	/* Copper options */
	if (hw->phy.media_type == e1000_media_type_copper) {
		hw->phy.mdix = AUTO_ALL_MODES;
		hw->phy.disable_polarity_correction = false;
		hw->phy.ms_type = e1000_ms_hw_default;
	}

	if (igb_check_reset_block(hw))
		dev_info(&pdev->dev,
			"PHY reset is blocked due to SOL/IDER session.\n");

	netdev->features = NETIF_F_SG |
			   NETIF_F_HW_CSUM |
			   NETIF_F_HW_VLAN_TX |
			   NETIF_F_HW_VLAN_RX |
			   NETIF_F_HW_VLAN_FILTER;

	netdev->features |= NETIF_F_TSO;
	netdev->features |= NETIF_F_TSO6;

#ifdef CONFIG_IGB_LRO
	netdev->features |= NETIF_F_LRO;
#endif

	netdev->vlan_features |= NETIF_F_TSO;
	netdev->vlan_features |= NETIF_F_TSO6;
	netdev->vlan_features |= NETIF_F_HW_CSUM;
	netdev->vlan_features |= NETIF_F_SG;

	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	netdev->features |= NETIF_F_LLTX;
	adapter->en_mng_pt = igb_enable_mng_pass_thru(&adapter->hw);

	/* before reading the NVM, reset the controller to put the device in a
	 * known good starting state */
	hw->mac.ops.reset_hw(hw);

	/* make sure the NVM is good */
	if (igb_validate_nvm_checksum(hw) < 0) {
		dev_err(&pdev->dev, "The NVM Checksum Is Not Valid\n");
		err = -EIO;
		goto err_eeprom;
	}

	/* copy the MAC address out of the NVM */
	if (hw->mac.ops.read_mac_addr(hw))
		dev_err(&pdev->dev, "NVM Read Error\n");

	memcpy(netdev->dev_addr, hw->mac.addr, netdev->addr_len);
	memcpy(netdev->perm_addr, hw->mac.addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->perm_addr)) {
		dev_err(&pdev->dev, "Invalid MAC Address\n");
		err = -EIO;
		goto err_eeprom;
	}

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &igb_watchdog;
	adapter->watchdog_timer.data = (unsigned long) adapter;

	init_timer(&adapter->phy_info_timer);
	adapter->phy_info_timer.function = &igb_update_phy_info;
	adapter->phy_info_timer.data = (unsigned long) adapter;

	INIT_WORK(&adapter->reset_task, igb_reset_task);
	INIT_WORK(&adapter->watchdog_task, igb_watchdog_task);

	/* Initialize link & ring properties that are user-changeable */
	adapter->tx_ring->count = 256;
	for (i = 0; i < adapter->num_tx_queues; i++)
		adapter->tx_ring[i].count = adapter->tx_ring->count;
	adapter->rx_ring->count = 256;
	for (i = 0; i < adapter->num_rx_queues; i++)
		adapter->rx_ring[i].count = adapter->rx_ring->count;

	adapter->fc_autoneg = true;
	hw->mac.autoneg = true;
	hw->phy.autoneg_advertised = 0x2f;

	hw->fc.original_type = e1000_fc_default;
	hw->fc.type = e1000_fc_default;

	adapter->itr_setting = 3;
	adapter->itr = IGB_START_ITR;

	igb_validate_mdi_setting(hw);

	adapter->rx_csum = 1;

	/* Initial Wake on LAN setting If APM wake is enabled in the EEPROM,
	 * enable the ACPI Magic Packet filter
	 */

	if (hw->bus.func == 0 ||
	    hw->device_id == E1000_DEV_ID_82575EB_COPPER)
		hw->nvm.ops.read_nvm(hw, NVM_INIT_CONTROL3_PORT_A, 1,
				     &eeprom_data);

	if (eeprom_data & eeprom_apme_mask)
		adapter->eeprom_wol |= E1000_WUFC_MAG;

	/* now that we have the eeprom settings, apply the special cases where
	 * the eeprom may be wrong or the board simply won't support wake on
	 * lan on a particular port */
	switch (pdev->device) {
	case E1000_DEV_ID_82575GB_QUAD_COPPER:
		adapter->eeprom_wol = 0;
		break;
	case E1000_DEV_ID_82575EB_FIBER_SERDES:
	case E1000_DEV_ID_82576_FIBER:
	case E1000_DEV_ID_82576_SERDES:
		/* Wake events only supported on port A for dual fiber
		 * regardless of eeprom setting */
		if (rd32(E1000_STATUS) & E1000_STATUS_FUNC_1)
			adapter->eeprom_wol = 0;
		break;
	}

	/* initialize the wol settings based on the eeprom settings */
	adapter->wol = adapter->eeprom_wol;

	/* reset the hardware with the new settings */
	igb_reset(adapter);

	/* let the f/w know that the h/w is now under the control of the
	 * driver. */
	igb_get_hw_control(adapter);

	/* tell the stack to leave us alone until igb_open() is called */
	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err)
		goto err_register;

#ifdef CONFIG_DCA
	if ((adapter->flags & IGB_FLAG_HAS_DCA) &&
	    (dca_add_requester(&pdev->dev) == 0)) {
		adapter->flags |= IGB_FLAG_DCA_ENABLED;
		dev_info(&pdev->dev, "DCA enabled\n");
		/* Always use CB2 mode, difference is masked
		 * in the CB driver. */
		wr32(E1000_DCA_CTRL, 2);
		igb_setup_dca(adapter);
	}
#endif

	dev_info(&pdev->dev, "Intel(R) Gigabit Ethernet Network Connection\n");
	/* print bus type/speed/width info */
	dev_info(&pdev->dev,
		 "%s: (PCIe:%s:%s) %02x:%02x:%02x:%02x:%02x:%02x\n",
		 netdev->name,
		 ((hw->bus.speed == e1000_bus_speed_2500)
		  ? "2.5Gb/s" : "unknown"),
		 ((hw->bus.width == e1000_bus_width_pcie_x4)
		  ? "Width x4" : (hw->bus.width == e1000_bus_width_pcie_x1)
		  ? "Width x1" : "unknown"),
		 netdev->dev_addr[0], netdev->dev_addr[1], netdev->dev_addr[2],
		 netdev->dev_addr[3], netdev->dev_addr[4], netdev->dev_addr[5]);

	igb_read_part_num(hw, &part_num);
	dev_info(&pdev->dev, "%s: PBA No: %06x-%03x\n", netdev->name,
		(part_num >> 8), (part_num & 0xff));

	dev_info(&pdev->dev,
		"Using %s interrupts. %d rx queue(s), %d tx queue(s)\n",
		adapter->msix_entries ? "MSI-X" :
		(adapter->flags & IGB_FLAG_HAS_MSI) ? "MSI" : "legacy",
		adapter->num_rx_queues, adapter->num_tx_queues);

	return 0;

err_register:
	igb_release_hw_control(adapter);
err_eeprom:
	if (!igb_check_reset_block(hw))
		hw->phy.ops.reset_phy(hw);

	if (hw->flash_address)
		iounmap(hw->flash_address);

	igb_remove_device(hw);
	igb_free_queues(adapter);
err_sw_init:
err_hw_init:
	iounmap(hw->hw_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_selected_regions(pdev, bars);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * igb_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * igb_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void __devexit igb_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);
#ifdef CONFIG_DCA
	struct e1000_hw *hw = &adapter->hw;
#endif

	/* flush_scheduled work may reschedule our watchdog task, so
	 * explicitly disable watchdog tasks from being rescheduled  */
	set_bit(__IGB_DOWN, &adapter->state);
	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

	flush_scheduled_work();

#ifdef CONFIG_DCA
	if (adapter->flags & IGB_FLAG_DCA_ENABLED) {
		dev_info(&pdev->dev, "DCA disabled\n");
		dca_remove_requester(&pdev->dev);
		adapter->flags &= ~IGB_FLAG_DCA_ENABLED;
		wr32(E1000_DCA_CTRL, 1);
	}
#endif

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant. */
	igb_release_hw_control(adapter);

	unregister_netdev(netdev);

	if (adapter->hw.phy.ops.reset_phy &&
	    !igb_check_reset_block(&adapter->hw))
		adapter->hw.phy.ops.reset_phy(&adapter->hw);

	igb_remove_device(&adapter->hw);
	igb_reset_interrupt_capability(adapter);

	igb_free_queues(adapter);

	iounmap(adapter->hw.hw_addr);
	if (adapter->hw.flash_address)
		iounmap(adapter->hw.flash_address);
	pci_release_selected_regions(pdev, adapter->bars);

	free_netdev(netdev);

	pci_disable_device(pdev);
}

/**
 * igb_sw_init - Initialize general software structures (struct igb_adapter)
 * @adapter: board private structure to initialize
 *
 * igb_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int __devinit igb_sw_init(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;

	pci_read_config_word(pdev, PCI_COMMAND, &hw->bus.pci_cmd_word);

	adapter->rx_buffer_len = MAXIMUM_ETHERNET_VLAN_SIZE;
	adapter->rx_ps_hdr_size = 0; /* disable packet split */
	adapter->max_frame_size = netdev->mtu + ETH_HLEN + ETH_FCS_LEN;
	adapter->min_frame_size = ETH_ZLEN + ETH_FCS_LEN;

	/* Number of supported queues. */
	/* Having more queues than CPUs doesn't make sense. */
	adapter->num_rx_queues = min((u32)IGB_MAX_RX_QUEUES, (u32)num_online_cpus());
	adapter->num_tx_queues = min(IGB_MAX_TX_QUEUES, num_online_cpus());

	/* This call may decrease the number of queues depending on
	 * interrupt mode. */
	igb_set_interrupt_capability(adapter);

	if (igb_alloc_queues(adapter)) {
		dev_err(&pdev->dev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	/* Explicitly disable IRQ since the NIC can be in any state. */
	igb_irq_disable(adapter);

	set_bit(__IGB_DOWN, &adapter->state);
	return 0;
}

/**
 * igb_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/
static int igb_open(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int err;
	int i;

	/* disallow open during test */
	if (test_bit(__IGB_TESTING, &adapter->state))
		return -EBUSY;

	/* allocate transmit descriptors */
	err = igb_setup_all_tx_resources(adapter);
	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = igb_setup_all_rx_resources(adapter);
	if (err)
		goto err_setup_rx;

	/* e1000_power_up_phy(adapter); */

	adapter->mng_vlan_id = IGB_MNG_VLAN_NONE;
	if ((adapter->hw.mng_cookie.status &
	     E1000_MNG_DHCP_COOKIE_STATUS_VLAN))
		igb_update_mng_vlan(adapter);

	/* before we allocate an interrupt, we must be ready to handle it.
	 * Setting DEBUG_SHIRQ in the kernel makes it fire an interrupt
	 * as soon as we call pci_request_irq, so we have to setup our
	 * clean_rx handler before we do so.  */
	igb_configure(adapter);

	err = igb_request_irq(adapter);
	if (err)
		goto err_req_irq;

	/* From here on the code is the same as igb_up() */
	clear_bit(__IGB_DOWN, &adapter->state);

	for (i = 0; i < adapter->num_rx_queues; i++)
		napi_enable(&adapter->rx_ring[i].napi);

	/* Clear any pending interrupts. */
	rd32(E1000_ICR);

	igb_irq_enable(adapter);

	netif_tx_start_all_queues(netdev);

	/* Fire a link status change interrupt to start the watchdog. */
	wr32(E1000_ICS, E1000_ICS_LSC);

	return 0;

err_req_irq:
	igb_release_hw_control(adapter);
	/* e1000_power_down_phy(adapter); */
	igb_free_all_rx_resources(adapter);
err_setup_rx:
	igb_free_all_tx_resources(adapter);
err_setup_tx:
	igb_reset(adapter);

	return err;
}

/**
 * igb_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the driver's control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
static int igb_close(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	WARN_ON(test_bit(__IGB_RESETTING, &adapter->state));
	igb_down(adapter);

	igb_free_irq(adapter);

	igb_free_all_tx_resources(adapter);
	igb_free_all_rx_resources(adapter);

	/* kill manageability vlan ID if supported, but not if a vlan with
	 * the same ID is registered on the host OS (let 8021q kill it) */
	if ((adapter->hw.mng_cookie.status &
			  E1000_MNG_DHCP_COOKIE_STATUS_VLAN) &&
	     !(adapter->vlgrp &&
	       vlan_group_get_device(adapter->vlgrp, adapter->mng_vlan_id)))
		igb_vlan_rx_kill_vid(netdev, adapter->mng_vlan_id);

	return 0;
}

/**
 * igb_setup_tx_resources - allocate Tx resources (Descriptors)
 * @adapter: board private structure
 * @tx_ring: tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/

int igb_setup_tx_resources(struct igb_adapter *adapter,
			   struct igb_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;
	int size;

	size = sizeof(struct igb_buffer) * tx_ring->count;
	tx_ring->buffer_info = vmalloc(size);
	if (!tx_ring->buffer_info)
		goto err;
	memset(tx_ring->buffer_info, 0, size);

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(struct e1000_tx_desc)
			+ sizeof(u32);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	tx_ring->desc = pci_alloc_consistent(pdev, tx_ring->size,
					     &tx_ring->dma);

	if (!tx_ring->desc)
		goto err;

	tx_ring->adapter = adapter;
	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	return 0;

err:
	vfree(tx_ring->buffer_info);
	dev_err(&adapter->pdev->dev,
		"Unable to allocate memory for the transmit descriptor ring\n");
	return -ENOMEM;
}

/**
 * igb_setup_all_tx_resources - wrapper to allocate Tx resources
 *				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/
static int igb_setup_all_tx_resources(struct igb_adapter *adapter)
{
	int i, err = 0;
	int r_idx;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		err = igb_setup_tx_resources(adapter, &adapter->tx_ring[i]);
		if (err) {
			dev_err(&adapter->pdev->dev,
				"Allocation for Tx Queue %u failed\n", i);
			for (i--; i >= 0; i--)
				igb_free_tx_resources(&adapter->tx_ring[i]);
			break;
		}
	}

	for (i = 0; i < IGB_MAX_TX_QUEUES; i++) {
		r_idx = i % adapter->num_tx_queues;
		adapter->multi_tx_table[i] = &adapter->tx_ring[r_idx];
	}	
	return err;
}

/**
 * igb_configure_tx - Configure transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
static void igb_configure_tx(struct igb_adapter *adapter)
{
	u64 tdba, tdwba;
	struct e1000_hw *hw = &adapter->hw;
	u32 tctl;
	u32 txdctl, txctrl;
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igb_ring *ring = &(adapter->tx_ring[i]);

		wr32(E1000_TDLEN(i),
				ring->count * sizeof(struct e1000_tx_desc));
		tdba = ring->dma;
		wr32(E1000_TDBAL(i),
				tdba & 0x00000000ffffffffULL);
		wr32(E1000_TDBAH(i), tdba >> 32);

		tdwba = ring->dma + ring->count * sizeof(struct e1000_tx_desc);
		tdwba |= 1; /* enable head wb */
		wr32(E1000_TDWBAL(i),
				tdwba & 0x00000000ffffffffULL);
		wr32(E1000_TDWBAH(i), tdwba >> 32);

		ring->head = E1000_TDH(i);
		ring->tail = E1000_TDT(i);
		writel(0, hw->hw_addr + ring->tail);
		writel(0, hw->hw_addr + ring->head);
		txdctl = rd32(E1000_TXDCTL(i));
		txdctl |= E1000_TXDCTL_QUEUE_ENABLE;
		wr32(E1000_TXDCTL(i), txdctl);

		/* Turn off Relaxed Ordering on head write-backs.  The
		 * writebacks MUST be delivered in order or it will
		 * completely screw up our bookeeping.
		 */
		txctrl = rd32(E1000_DCA_TXCTRL(i));
		txctrl &= ~E1000_DCA_TXCTRL_TX_WB_RO_EN;
		wr32(E1000_DCA_TXCTRL(i), txctrl);
	}



	/* Use the default values for the Tx Inter Packet Gap (IPG) timer */

	/* Program the Transmit Control Register */

	tctl = rd32(E1000_TCTL);
	tctl &= ~E1000_TCTL_CT;
	tctl |= E1000_TCTL_PSP | E1000_TCTL_RTLC |
		(E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

	igb_config_collision_dist(hw);

	/* Setup Transmit Descriptor Settings for eop descriptor */
	adapter->txd_cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;

	/* Enable transmits */
	tctl |= E1000_TCTL_EN;

	wr32(E1000_TCTL, tctl);
}

/**
 * igb_setup_rx_resources - allocate Rx resources (Descriptors)
 * @adapter: board private structure
 * @rx_ring:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/

int igb_setup_rx_resources(struct igb_adapter *adapter,
			   struct igb_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;
	int size, desc_len;

#ifdef CONFIG_IGB_LRO
	size = sizeof(struct net_lro_desc) * MAX_LRO_DESCRIPTORS;
	rx_ring->lro_mgr.lro_arr = vmalloc(size);
	if (!rx_ring->lro_mgr.lro_arr)
		goto err;
	memset(rx_ring->lro_mgr.lro_arr, 0, size);
#endif

	size = sizeof(struct igb_buffer) * rx_ring->count;
	rx_ring->buffer_info = vmalloc(size);
	if (!rx_ring->buffer_info)
		goto err;
	memset(rx_ring->buffer_info, 0, size);

	desc_len = sizeof(union e1000_adv_rx_desc);

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * desc_len;
	rx_ring->size = ALIGN(rx_ring->size, 4096);

	rx_ring->desc = pci_alloc_consistent(pdev, rx_ring->size,
					     &rx_ring->dma);

	if (!rx_ring->desc)
		goto err;

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	rx_ring->adapter = adapter;

	return 0;

err:
#ifdef CONFIG_IGB_LRO
	vfree(rx_ring->lro_mgr.lro_arr);
	rx_ring->lro_mgr.lro_arr = NULL;
#endif
	vfree(rx_ring->buffer_info);
	dev_err(&adapter->pdev->dev, "Unable to allocate memory for "
		"the receive descriptor ring\n");
	return -ENOMEM;
}

/**
 * igb_setup_all_rx_resources - wrapper to allocate Rx resources
 *				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/
static int igb_setup_all_rx_resources(struct igb_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		err = igb_setup_rx_resources(adapter, &adapter->rx_ring[i]);
		if (err) {
			dev_err(&adapter->pdev->dev,
				"Allocation for Rx Queue %u failed\n", i);
			for (i--; i >= 0; i--)
				igb_free_rx_resources(&adapter->rx_ring[i]);
			break;
		}
	}

	return err;
}

/**
 * igb_setup_rctl - configure the receive control registers
 * @adapter: Board private structure
 **/
static void igb_setup_rctl(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;
	u32 srrctl = 0;
	int i;

	rctl = rd32(E1000_RCTL);

	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);

	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM |
		E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
		(adapter->hw.mac.mc_filter_type << E1000_RCTL_MO_SHIFT);

	/*
	 * enable stripping of CRC. It's unlikely this will break BMC
	 * redirection as it did with e1000. Newer features require
	 * that the HW strips the CRC.
	*/
	rctl |= E1000_RCTL_SECRC;

	rctl &= ~E1000_RCTL_SBP;

	if (adapter->netdev->mtu <= ETH_DATA_LEN)
		rctl &= ~E1000_RCTL_LPE;
	else
		rctl |= E1000_RCTL_LPE;
	if (adapter->rx_buffer_len <= IGB_RXBUFFER_2048) {
		/* Setup buffer sizes */
		rctl &= ~E1000_RCTL_SZ_4096;
		rctl |= E1000_RCTL_BSEX;
		switch (adapter->rx_buffer_len) {
		case IGB_RXBUFFER_256:
			rctl |= E1000_RCTL_SZ_256;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case IGB_RXBUFFER_512:
			rctl |= E1000_RCTL_SZ_512;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case IGB_RXBUFFER_1024:
			rctl |= E1000_RCTL_SZ_1024;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case IGB_RXBUFFER_2048:
		default:
			rctl |= E1000_RCTL_SZ_2048;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		}
	} else {
		rctl &= ~E1000_RCTL_BSEX;
		srrctl = adapter->rx_buffer_len >> E1000_SRRCTL_BSIZEPKT_SHIFT;
	}

	/* 82575 and greater support packet-split where the protocol
	 * header is placed in skb->data and the packet data is
	 * placed in pages hanging off of skb_shinfo(skb)->nr_frags.
	 * In the case of a non-split, skb->data is linearly filled,
	 * followed by the page buffers.  Therefore, skb->data is
	 * sized to hold the largest protocol header.
	 */
	/* allocations using alloc_page take too long for regular MTU
	 * so only enable packet split for jumbo frames */
	if (rctl & E1000_RCTL_LPE) {
		adapter->rx_ps_hdr_size = IGB_RXBUFFER_128;
		srrctl |= adapter->rx_ps_hdr_size <<
			 E1000_SRRCTL_BSIZEHDRSIZE_SHIFT;
		srrctl |= E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS;
	} else {
		adapter->rx_ps_hdr_size = 0;
		srrctl |= E1000_SRRCTL_DESCTYPE_ADV_ONEBUF;
	}

	for (i = 0; i < adapter->num_rx_queues; i++)
		wr32(E1000_SRRCTL(i), srrctl);

	wr32(E1000_RCTL, rctl);
}

/**
 * igb_configure_rx - Configure receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void igb_configure_rx(struct igb_adapter *adapter)
{
	u64 rdba;
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl, rxcsum;
	u32 rxdctl;
	int i;

	/* disable receives while setting up the descriptors */
	rctl = rd32(E1000_RCTL);
	wr32(E1000_RCTL, rctl & ~E1000_RCTL_EN);
	wrfl();
	mdelay(10);

	if (adapter->itr_setting > 3)
		wr32(E1000_ITR, adapter->itr);

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct igb_ring *ring = &(adapter->rx_ring[i]);
		rdba = ring->dma;
		wr32(E1000_RDBAL(i),
				rdba & 0x00000000ffffffffULL);
		wr32(E1000_RDBAH(i), rdba >> 32);
		wr32(E1000_RDLEN(i),
			       ring->count * sizeof(union e1000_adv_rx_desc));

		ring->head = E1000_RDH(i);
		ring->tail = E1000_RDT(i);
		writel(0, hw->hw_addr + ring->tail);
		writel(0, hw->hw_addr + ring->head);

		rxdctl = rd32(E1000_RXDCTL(i));
		rxdctl |= E1000_RXDCTL_QUEUE_ENABLE;
		rxdctl &= 0xFFF00000;
		rxdctl |= IGB_RX_PTHRESH;
		rxdctl |= IGB_RX_HTHRESH << 8;
		rxdctl |= IGB_RX_WTHRESH << 16;
		wr32(E1000_RXDCTL(i), rxdctl);
#ifdef CONFIG_IGB_LRO
		/* Intitial LRO Settings */
		ring->lro_mgr.max_aggr = MAX_LRO_AGGR;
		ring->lro_mgr.max_desc = MAX_LRO_DESCRIPTORS;
		ring->lro_mgr.get_skb_header = igb_get_skb_hdr;
		ring->lro_mgr.features = LRO_F_NAPI | LRO_F_EXTRACT_VLAN_ID;
		ring->lro_mgr.dev = adapter->netdev;
		ring->lro_mgr.ip_summed = CHECKSUM_UNNECESSARY;
		ring->lro_mgr.ip_summed_aggr = CHECKSUM_UNNECESSARY;
#endif
	}

	if (adapter->num_rx_queues > 1) {
		u32 random[10];
		u32 mrqc;
		u32 j, shift;
		union e1000_reta {
			u32 dword;
			u8  bytes[4];
		} reta;

		get_random_bytes(&random[0], 40);

		if (hw->mac.type >= e1000_82576)
			shift = 0;
		else
			shift = 6;
		for (j = 0; j < (32 * 4); j++) {
			reta.bytes[j & 3] =
				(j % adapter->num_rx_queues) << shift;
			if ((j & 3) == 3)
				writel(reta.dword,
				       hw->hw_addr + E1000_RETA(0) + (j & ~3));
		}
		mrqc = E1000_MRQC_ENABLE_RSS_4Q;

		/* Fill out hash function seeds */
		for (j = 0; j < 10; j++)
			array_wr32(E1000_RSSRK(0), j, random[j]);

		mrqc |= (E1000_MRQC_RSS_FIELD_IPV4 |
			 E1000_MRQC_RSS_FIELD_IPV4_TCP);
		mrqc |= (E1000_MRQC_RSS_FIELD_IPV6 |
			 E1000_MRQC_RSS_FIELD_IPV6_TCP);
		mrqc |= (E1000_MRQC_RSS_FIELD_IPV4_UDP |
			 E1000_MRQC_RSS_FIELD_IPV6_UDP);
		mrqc |= (E1000_MRQC_RSS_FIELD_IPV6_UDP_EX |
			 E1000_MRQC_RSS_FIELD_IPV6_TCP_EX);


		wr32(E1000_MRQC, mrqc);

		/* Multiqueue and raw packet checksumming are mutually
		 * exclusive.  Note that this not the same as TCP/IP
		 * checksumming, which works fine. */
		rxcsum = rd32(E1000_RXCSUM);
		rxcsum |= E1000_RXCSUM_PCSD;
		wr32(E1000_RXCSUM, rxcsum);
	} else {
		/* Enable Receive Checksum Offload for TCP and UDP */
		rxcsum = rd32(E1000_RXCSUM);
		if (adapter->rx_csum) {
			rxcsum |= E1000_RXCSUM_TUOFL;

			/* Enable IPv4 payload checksum for UDP fragments
			 * Must be used in conjunction with packet-split. */
			if (adapter->rx_ps_hdr_size)
				rxcsum |= E1000_RXCSUM_IPPCSE;
		} else {
			rxcsum &= ~E1000_RXCSUM_TUOFL;
			/* don't need to clear IPPCSE as it defaults to 0 */
		}
		wr32(E1000_RXCSUM, rxcsum);
	}

	if (adapter->vlgrp)
		wr32(E1000_RLPML,
				adapter->max_frame_size + VLAN_TAG_SIZE);
	else
		wr32(E1000_RLPML, adapter->max_frame_size);

	/* Enable Receives */
	wr32(E1000_RCTL, rctl);
}

/**
 * igb_free_tx_resources - Free Tx Resources per Queue
 * @adapter: board private structure
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
static void igb_free_tx_resources(struct igb_ring *tx_ring)
{
	struct pci_dev *pdev = tx_ring->adapter->pdev;

	igb_clean_tx_ring(tx_ring);

	vfree(tx_ring->buffer_info);
	tx_ring->buffer_info = NULL;

	pci_free_consistent(pdev, tx_ring->size, tx_ring->desc, tx_ring->dma);

	tx_ring->desc = NULL;
}

/**
 * igb_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/
static void igb_free_all_tx_resources(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		igb_free_tx_resources(&adapter->tx_ring[i]);
}

static void igb_unmap_and_free_tx_resource(struct igb_adapter *adapter,
					   struct igb_buffer *buffer_info)
{
	if (buffer_info->dma) {
		pci_unmap_page(adapter->pdev,
				buffer_info->dma,
				buffer_info->length,
				PCI_DMA_TODEVICE);
		buffer_info->dma = 0;
	}
	if (buffer_info->skb) {
		dev_kfree_skb_any(buffer_info->skb);
		buffer_info->skb = NULL;
	}
	buffer_info->time_stamp = 0;
	/* buffer_info must be completely set up in the transmit path */
}

/**
 * igb_clean_tx_ring - Free Tx Buffers
 * @adapter: board private structure
 * @tx_ring: ring to be cleaned
 **/
static void igb_clean_tx_ring(struct igb_ring *tx_ring)
{
	struct igb_adapter *adapter = tx_ring->adapter;
	struct igb_buffer *buffer_info;
	unsigned long size;
	unsigned int i;

	if (!tx_ring->buffer_info)
		return;
	/* Free all the Tx ring sk_buffs */

	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->buffer_info[i];
		igb_unmap_and_free_tx_resource(adapter, buffer_info);
	}

	size = sizeof(struct igb_buffer) * tx_ring->count;
	memset(tx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */

	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	writel(0, adapter->hw.hw_addr + tx_ring->head);
	writel(0, adapter->hw.hw_addr + tx_ring->tail);
}

/**
 * igb_clean_all_tx_rings - Free Tx Buffers for all queues
 * @adapter: board private structure
 **/
static void igb_clean_all_tx_rings(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		igb_clean_tx_ring(&adapter->tx_ring[i]);
}

/**
 * igb_free_rx_resources - Free Rx Resources
 * @adapter: board private structure
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
static void igb_free_rx_resources(struct igb_ring *rx_ring)
{
	struct pci_dev *pdev = rx_ring->adapter->pdev;

	igb_clean_rx_ring(rx_ring);

	vfree(rx_ring->buffer_info);
	rx_ring->buffer_info = NULL;

#ifdef CONFIG_IGB_LRO
	vfree(rx_ring->lro_mgr.lro_arr);
	rx_ring->lro_mgr.lro_arr = NULL;
#endif 

	pci_free_consistent(pdev, rx_ring->size, rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * igb_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/
static void igb_free_all_rx_resources(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		igb_free_rx_resources(&adapter->rx_ring[i]);
}

/**
 * igb_clean_rx_ring - Free Rx Buffers per Queue
 * @adapter: board private structure
 * @rx_ring: ring to free buffers from
 **/
static void igb_clean_rx_ring(struct igb_ring *rx_ring)
{
	struct igb_adapter *adapter = rx_ring->adapter;
	struct igb_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i;

	if (!rx_ring->buffer_info)
		return;
	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		if (buffer_info->dma) {
			if (adapter->rx_ps_hdr_size)
				pci_unmap_single(pdev, buffer_info->dma,
						 adapter->rx_ps_hdr_size,
						 PCI_DMA_FROMDEVICE);
			else
				pci_unmap_single(pdev, buffer_info->dma,
						 adapter->rx_buffer_len,
						 PCI_DMA_FROMDEVICE);
			buffer_info->dma = 0;
		}

		if (buffer_info->skb) {
			dev_kfree_skb(buffer_info->skb);
			buffer_info->skb = NULL;
		}
		if (buffer_info->page) {
			if (buffer_info->page_dma)
				pci_unmap_page(pdev, buffer_info->page_dma,
					       PAGE_SIZE / 2,
					       PCI_DMA_FROMDEVICE);
			put_page(buffer_info->page);
			buffer_info->page = NULL;
			buffer_info->page_dma = 0;
			buffer_info->page_offset = 0;
		}
	}

	size = sizeof(struct igb_buffer) * rx_ring->count;
	memset(rx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	writel(0, adapter->hw.hw_addr + rx_ring->head);
	writel(0, adapter->hw.hw_addr + rx_ring->tail);
}

/**
 * igb_clean_all_rx_rings - Free Rx Buffers for all queues
 * @adapter: board private structure
 **/
static void igb_clean_all_rx_rings(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		igb_clean_rx_ring(&adapter->rx_ring[i]);
}

/**
 * igb_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int igb_set_mac(struct net_device *netdev, void *p)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac.addr, addr->sa_data, netdev->addr_len);

	adapter->hw.mac.ops.rar_set(&adapter->hw, adapter->hw.mac.addr, 0);

	return 0;
}

/**
 * igb_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 **/
static void igb_set_multi(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_mac_info *mac = &hw->mac;
	struct dev_mc_list *mc_ptr;
	u8  *mta_list;
	u32 rctl;
	int i;

	/* Check for Promiscuous and All Multicast modes */

	rctl = rd32(E1000_RCTL);

	if (netdev->flags & IFF_PROMISC) {
		rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
		rctl &= ~E1000_RCTL_VFE;
	} else {
		if (netdev->flags & IFF_ALLMULTI) {
			rctl |= E1000_RCTL_MPE;
			rctl &= ~E1000_RCTL_UPE;
		} else
			rctl &= ~(E1000_RCTL_UPE | E1000_RCTL_MPE);
		rctl |= E1000_RCTL_VFE;
	}
	wr32(E1000_RCTL, rctl);

	if (!netdev->mc_count) {
		/* nothing to program, so clear mc list */
		igb_update_mc_addr_list_82575(hw, NULL, 0, 1,
					  mac->rar_entry_count);
		return;
	}

	mta_list = kzalloc(netdev->mc_count * 6, GFP_ATOMIC);
	if (!mta_list)
		return;

	/* The shared function expects a packed array of only addresses. */
	mc_ptr = netdev->mc_list;

	for (i = 0; i < netdev->mc_count; i++) {
		if (!mc_ptr)
			break;
		memcpy(mta_list + (i*ETH_ALEN), mc_ptr->dmi_addr, ETH_ALEN);
		mc_ptr = mc_ptr->next;
	}
	igb_update_mc_addr_list_82575(hw, mta_list, i, 1,
	                              mac->rar_entry_count);
	kfree(mta_list);
}

/* Need to wait a few seconds after link up to get diagnostic information from
 * the phy */
static void igb_update_phy_info(unsigned long data)
{
	struct igb_adapter *adapter = (struct igb_adapter *) data;
	if (adapter->hw.phy.ops.get_phy_info)
		adapter->hw.phy.ops.get_phy_info(&adapter->hw);
}

/**
 * igb_watchdog - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void igb_watchdog(unsigned long data)
{
	struct igb_adapter *adapter = (struct igb_adapter *)data;
	/* Do the rest outside of interrupt context */
	schedule_work(&adapter->watchdog_task);
}

static void igb_watchdog_task(struct work_struct *work)
{
	struct igb_adapter *adapter = container_of(work,
					struct igb_adapter, watchdog_task);
	struct e1000_hw *hw = &adapter->hw;

	struct net_device *netdev = adapter->netdev;
	struct igb_ring *tx_ring = adapter->tx_ring;
	struct e1000_mac_info *mac = &adapter->hw.mac;
	u32 link;
	u32 eics = 0;
	s32 ret_val;
	int i;

	if ((netif_carrier_ok(netdev)) &&
	    (rd32(E1000_STATUS) & E1000_STATUS_LU))
		goto link_up;

	ret_val = hw->mac.ops.check_for_link(&adapter->hw);
	if ((ret_val == E1000_ERR_PHY) &&
	    (hw->phy.type == e1000_phy_igp_3) &&
	    (rd32(E1000_CTRL) &
	     E1000_PHY_CTRL_GBE_DISABLE))
		dev_info(&adapter->pdev->dev,
			 "Gigabit has been disabled, downgrading speed\n");

	if ((hw->phy.media_type == e1000_media_type_internal_serdes) &&
	    !(rd32(E1000_TXCW) & E1000_TXCW_ANE))
		link = mac->serdes_has_link;
	else
		link = rd32(E1000_STATUS) &
				      E1000_STATUS_LU;

	if (link) {
		if (!netif_carrier_ok(netdev)) {
			u32 ctrl;
			hw->mac.ops.get_speed_and_duplex(&adapter->hw,
						   &adapter->link_speed,
						   &adapter->link_duplex);

			ctrl = rd32(E1000_CTRL);
			dev_info(&adapter->pdev->dev,
				 "NIC Link is Up %d Mbps %s, "
				 "Flow Control: %s\n",
				 adapter->link_speed,
				 adapter->link_duplex == FULL_DUPLEX ?
				 "Full Duplex" : "Half Duplex",
				 ((ctrl & E1000_CTRL_TFCE) && (ctrl &
				 E1000_CTRL_RFCE)) ? "RX/TX" : ((ctrl &
				 E1000_CTRL_RFCE) ? "RX" : ((ctrl &
				 E1000_CTRL_TFCE) ? "TX" : "None")));

			/* tweak tx_queue_len according to speed/duplex and
			 * adjust the timeout factor */
			netdev->tx_queue_len = adapter->tx_queue_len;
			adapter->tx_timeout_factor = 1;
			switch (adapter->link_speed) {
			case SPEED_10:
				netdev->tx_queue_len = 10;
				adapter->tx_timeout_factor = 14;
				break;
			case SPEED_100:
				netdev->tx_queue_len = 100;
				/* maybe add some timeout factor ? */
				break;
			}

			netif_carrier_on(netdev);
			netif_tx_wake_all_queues(netdev);

			if (!test_bit(__IGB_DOWN, &adapter->state))
				mod_timer(&adapter->phy_info_timer,
					  round_jiffies(jiffies + 2 * HZ));
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;
			dev_info(&adapter->pdev->dev, "NIC Link is Down\n");
			netif_carrier_off(netdev);
			netif_tx_stop_all_queues(netdev);
			if (!test_bit(__IGB_DOWN, &adapter->state))
				mod_timer(&adapter->phy_info_timer,
					  round_jiffies(jiffies + 2 * HZ));
		}
	}

link_up:
	igb_update_stats(adapter);

	mac->tx_packet_delta = adapter->stats.tpt - adapter->tpt_old;
	adapter->tpt_old = adapter->stats.tpt;
	mac->collision_delta = adapter->stats.colc - adapter->colc_old;
	adapter->colc_old = adapter->stats.colc;

	adapter->gorc = adapter->stats.gorc - adapter->gorc_old;
	adapter->gorc_old = adapter->stats.gorc;
	adapter->gotc = adapter->stats.gotc - adapter->gotc_old;
	adapter->gotc_old = adapter->stats.gotc;

	igb_update_adaptive(&adapter->hw);

	if (!netif_carrier_ok(netdev)) {
		if (IGB_DESC_UNUSED(tx_ring) + 1 < tx_ring->count) {
			/* We've lost link, so the controller stops DMA,
			 * but we've got queued Tx work that's never going
			 * to get done, so reset controller to flush Tx.
			 * (Do the reset outside of interrupt context). */
			adapter->tx_timeout_count++;
			schedule_work(&adapter->reset_task);
		}
	}

	/* Cause software interrupt to ensure rx ring is cleaned */
	if (adapter->msix_entries) {
		for (i = 0; i < adapter->num_rx_queues; i++)
			eics |= adapter->rx_ring[i].eims_value;
		wr32(E1000_EICS, eics);
	} else {
		wr32(E1000_ICS, E1000_ICS_RXDMT0);
	}

	/* Force detection of hung controller every watchdog period */
	tx_ring->detect_tx_hung = true;

	/* Reset the timer */
	if (!test_bit(__IGB_DOWN, &adapter->state))
		mod_timer(&adapter->watchdog_timer,
			  round_jiffies(jiffies + 2 * HZ));
}

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};


/**
 * igb_update_ring_itr - update the dynamic ITR value based on packet size
 *
 *      Stores a new ITR value based on strictly on packet size.  This
 *      algorithm is less sophisticated than that used in igb_update_itr,
 *      due to the difficulty of synchronizing statistics across multiple
 *      receive rings.  The divisors and thresholds used by this fuction
 *      were determined based on theoretical maximum wire speed and testing
 *      data, in order to minimize response time while increasing bulk
 *      throughput.
 *      This functionality is controlled by the InterruptThrottleRate module
 *      parameter (see igb_param.c)
 *      NOTE:  This function is called only when operating in a multiqueue
 *             receive environment.
 * @rx_ring: pointer to ring
 **/
static void igb_update_ring_itr(struct igb_ring *rx_ring)
{
	int new_val = rx_ring->itr_val;
	int avg_wire_size = 0;
	struct igb_adapter *adapter = rx_ring->adapter;

	if (!rx_ring->total_packets)
		goto clear_counts; /* no packets, so don't do anything */

	/* For non-gigabit speeds, just fix the interrupt rate at 4000
	 * ints/sec - ITR timer value of 120 ticks.
	 */
	if (adapter->link_speed != SPEED_1000) {
		new_val = 120;
		goto set_itr_val;
	}
	avg_wire_size = rx_ring->total_bytes / rx_ring->total_packets;

	/* Add 24 bytes to size to account for CRC, preamble, and gap */
	avg_wire_size += 24;

	/* Don't starve jumbo frames */
	avg_wire_size = min(avg_wire_size, 3000);

	/* Give a little boost to mid-size frames */
	if ((avg_wire_size > 300) && (avg_wire_size < 1200))
		new_val = avg_wire_size / 3;
	else
		new_val = avg_wire_size / 2;

set_itr_val:
	if (new_val != rx_ring->itr_val) {
		rx_ring->itr_val = new_val;
		rx_ring->set_itr = 1;
	}
clear_counts:
	rx_ring->total_bytes = 0;
	rx_ring->total_packets = 0;
}

/**
 * igb_update_itr - update the dynamic ITR value based on statistics
 *      Stores a new ITR value based on packets and byte
 *      counts during the last interrupt.  The advantage of per interrupt
 *      computation is faster updates and more accurate ITR for the current
 *      traffic pattern.  Constants in this function were computed
 *      based on theoretical maximum wire speed and thresholds were set based
 *      on testing data as well as attempting to minimize response time
 *      while increasing bulk throughput.
 *      this functionality is controlled by the InterruptThrottleRate module
 *      parameter (see igb_param.c)
 *      NOTE:  These calculations are only valid when operating in a single-
 *             queue environment.
 * @adapter: pointer to adapter
 * @itr_setting: current adapter->itr
 * @packets: the number of packets during this measurement interval
 * @bytes: the number of bytes during this measurement interval
 **/
static unsigned int igb_update_itr(struct igb_adapter *adapter, u16 itr_setting,
				   int packets, int bytes)
{
	unsigned int retval = itr_setting;

	if (packets == 0)
		goto update_itr_done;

	switch (itr_setting) {
	case lowest_latency:
		/* handle TSO and jumbo frames */
		if (bytes/packets > 8000)
			retval = bulk_latency;
		else if ((packets < 5) && (bytes > 512))
			retval = low_latency;
		break;
	case low_latency:  /* 50 usec aka 20000 ints/s */
		if (bytes > 10000) {
			/* this if handles the TSO accounting */
			if (bytes/packets > 8000) {
				retval = bulk_latency;
			} else if ((packets < 10) || ((bytes/packets) > 1200)) {
				retval = bulk_latency;
			} else if ((packets > 35)) {
				retval = lowest_latency;
			}
		} else if (bytes/packets > 2000) {
			retval = bulk_latency;
		} else if (packets <= 2 && bytes < 512) {
			retval = lowest_latency;
		}
		break;
	case bulk_latency: /* 250 usec aka 4000 ints/s */
		if (bytes > 25000) {
			if (packets > 35)
				retval = low_latency;
		} else if (bytes < 6000) {
			retval = low_latency;
		}
		break;
	}

update_itr_done:
	return retval;
}

static void igb_set_itr(struct igb_adapter *adapter)
{
	u16 current_itr;
	u32 new_itr = adapter->itr;

	/* for non-gigabit speeds, just fix the interrupt rate at 4000 */
	if (adapter->link_speed != SPEED_1000) {
		current_itr = 0;
		new_itr = 4000;
		goto set_itr_now;
	}

	adapter->rx_itr = igb_update_itr(adapter,
				    adapter->rx_itr,
				    adapter->rx_ring->total_packets,
				    adapter->rx_ring->total_bytes);

	if (adapter->rx_ring->buddy) {
		adapter->tx_itr = igb_update_itr(adapter,
					    adapter->tx_itr,
					    adapter->tx_ring->total_packets,
					    adapter->tx_ring->total_bytes);

		current_itr = max(adapter->rx_itr, adapter->tx_itr);
	} else {
		current_itr = adapter->rx_itr;
	}

	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (adapter->itr_setting == 3 &&
	    current_itr == lowest_latency)
		current_itr = low_latency;

	switch (current_itr) {
	/* counts and packets in update_itr are dependent on these numbers */
	case lowest_latency:
		new_itr = 70000;
		break;
	case low_latency:
		new_itr = 20000; /* aka hwitr = ~200 */
		break;
	case bulk_latency:
		new_itr = 4000;
		break;
	default:
		break;
	}

set_itr_now:
	adapter->rx_ring->total_bytes = 0;
	adapter->rx_ring->total_packets = 0;
	if (adapter->rx_ring->buddy) {
		adapter->rx_ring->buddy->total_bytes = 0;
		adapter->rx_ring->buddy->total_packets = 0;
	}

	if (new_itr != adapter->itr) {
		/* this attempts to bias the interrupt rate towards Bulk
		 * by adding intermediate steps when interrupt rate is
		 * increasing */
		new_itr = new_itr > adapter->itr ?
			     min(adapter->itr + (new_itr >> 2), new_itr) :
			     new_itr;
		/* Don't write the value here; it resets the adapter's
		 * internal timer, and causes us to delay far longer than
		 * we should between interrupts.  Instead, we write the ITR
		 * value at the beginning of the next interrupt so the timing
		 * ends up being correct.
		 */
		adapter->itr = new_itr;
		adapter->rx_ring->itr_val = 1000000000 / (new_itr * 256);
		adapter->rx_ring->set_itr = 1;
	}

	return;
}


#define IGB_TX_FLAGS_CSUM		0x00000001
#define IGB_TX_FLAGS_VLAN		0x00000002
#define IGB_TX_FLAGS_TSO		0x00000004
#define IGB_TX_FLAGS_IPV4		0x00000008
#define IGB_TX_FLAGS_VLAN_MASK	0xffff0000
#define IGB_TX_FLAGS_VLAN_SHIFT	16

static inline int igb_tso_adv(struct igb_adapter *adapter,
			      struct igb_ring *tx_ring,
			      struct sk_buff *skb, u32 tx_flags, u8 *hdr_len)
{
	struct e1000_adv_tx_context_desc *context_desc;
	unsigned int i;
	int err;
	struct igb_buffer *buffer_info;
	u32 info = 0, tu_cmd = 0;
	u32 mss_l4len_idx, l4len;
	*hdr_len = 0;

	if (skb_header_cloned(skb)) {
		err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
		if (err)
			return err;
	}

	l4len = tcp_hdrlen(skb);
	*hdr_len += l4len;

	if (skb->protocol == htons(ETH_P_IP)) {
		struct iphdr *iph = ip_hdr(skb);
		iph->tot_len = 0;
		iph->check = 0;
		tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
							 iph->daddr, 0,
							 IPPROTO_TCP,
							 0);
	} else if (skb_shinfo(skb)->gso_type == SKB_GSO_TCPV6) {
		ipv6_hdr(skb)->payload_len = 0;
		tcp_hdr(skb)->check = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						       &ipv6_hdr(skb)->daddr,
						       0, IPPROTO_TCP, 0);
	}

	i = tx_ring->next_to_use;

	buffer_info = &tx_ring->buffer_info[i];
	context_desc = E1000_TX_CTXTDESC_ADV(*tx_ring, i);
	/* VLAN MACLEN IPLEN */
	if (tx_flags & IGB_TX_FLAGS_VLAN)
		info |= (tx_flags & IGB_TX_FLAGS_VLAN_MASK);
	info |= (skb_network_offset(skb) << E1000_ADVTXD_MACLEN_SHIFT);
	*hdr_len += skb_network_offset(skb);
	info |= skb_network_header_len(skb);
	*hdr_len += skb_network_header_len(skb);
	context_desc->vlan_macip_lens = cpu_to_le32(info);

	/* ADV DTYP TUCMD MKRLOC/ISCSIHEDLEN */
	tu_cmd |= (E1000_TXD_CMD_DEXT | E1000_ADVTXD_DTYP_CTXT);

	if (skb->protocol == htons(ETH_P_IP))
		tu_cmd |= E1000_ADVTXD_TUCMD_IPV4;
	tu_cmd |= E1000_ADVTXD_TUCMD_L4T_TCP;

	context_desc->type_tucmd_mlhl = cpu_to_le32(tu_cmd);

	/* MSS L4LEN IDX */
	mss_l4len_idx = (skb_shinfo(skb)->gso_size << E1000_ADVTXD_MSS_SHIFT);
	mss_l4len_idx |= (l4len << E1000_ADVTXD_L4LEN_SHIFT);

	/* Context index must be unique per ring. */
	if (adapter->flags & IGB_FLAG_NEED_CTX_IDX)
		mss_l4len_idx |= tx_ring->queue_index << 4;

	context_desc->mss_l4len_idx = cpu_to_le32(mss_l4len_idx);
	context_desc->seqnum_seed = 0;

	buffer_info->time_stamp = jiffies;
	buffer_info->dma = 0;
	i++;
	if (i == tx_ring->count)
		i = 0;

	tx_ring->next_to_use = i;

	return true;
}

static inline bool igb_tx_csum_adv(struct igb_adapter *adapter,
					struct igb_ring *tx_ring,
					struct sk_buff *skb, u32 tx_flags)
{
	struct e1000_adv_tx_context_desc *context_desc;
	unsigned int i;
	struct igb_buffer *buffer_info;
	u32 info = 0, tu_cmd = 0;

	if ((skb->ip_summed == CHECKSUM_PARTIAL) ||
	    (tx_flags & IGB_TX_FLAGS_VLAN)) {
		i = tx_ring->next_to_use;
		buffer_info = &tx_ring->buffer_info[i];
		context_desc = E1000_TX_CTXTDESC_ADV(*tx_ring, i);

		if (tx_flags & IGB_TX_FLAGS_VLAN)
			info |= (tx_flags & IGB_TX_FLAGS_VLAN_MASK);
		info |= (skb_network_offset(skb) << E1000_ADVTXD_MACLEN_SHIFT);
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			info |= skb_network_header_len(skb);

		context_desc->vlan_macip_lens = cpu_to_le32(info);

		tu_cmd |= (E1000_TXD_CMD_DEXT | E1000_ADVTXD_DTYP_CTXT);

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			switch (skb->protocol) {
			case __constant_htons(ETH_P_IP):
				tu_cmd |= E1000_ADVTXD_TUCMD_IPV4;
				if (ip_hdr(skb)->protocol == IPPROTO_TCP)
					tu_cmd |= E1000_ADVTXD_TUCMD_L4T_TCP;
				break;
			case __constant_htons(ETH_P_IPV6):
				/* XXX what about other V6 headers?? */
				if (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP)
					tu_cmd |= E1000_ADVTXD_TUCMD_L4T_TCP;
				break;
			default:
				if (unlikely(net_ratelimit()))
					dev_warn(&adapter->pdev->dev,
					    "partial checksum but proto=%x!\n",
					    skb->protocol);
				break;
			}
		}

		context_desc->type_tucmd_mlhl = cpu_to_le32(tu_cmd);
		context_desc->seqnum_seed = 0;
		if (adapter->flags & IGB_FLAG_NEED_CTX_IDX)
			context_desc->mss_l4len_idx =
				cpu_to_le32(tx_ring->queue_index << 4);

		buffer_info->time_stamp = jiffies;
		buffer_info->dma = 0;

		i++;
		if (i == tx_ring->count)
			i = 0;
		tx_ring->next_to_use = i;

		return true;
	}


	return false;
}

#define IGB_MAX_TXD_PWR	16
#define IGB_MAX_DATA_PER_TXD	(1<<IGB_MAX_TXD_PWR)

static inline int igb_tx_map_adv(struct igb_adapter *adapter,
				 struct igb_ring *tx_ring,
				 struct sk_buff *skb)
{
	struct igb_buffer *buffer_info;
	unsigned int len = skb_headlen(skb);
	unsigned int count = 0, i;
	unsigned int f;

	i = tx_ring->next_to_use;

	buffer_info = &tx_ring->buffer_info[i];
	BUG_ON(len >= IGB_MAX_DATA_PER_TXD);
	buffer_info->length = len;
	/* set time_stamp *before* dma to help avoid a possible race */
	buffer_info->time_stamp = jiffies;
	buffer_info->dma = pci_map_single(adapter->pdev, skb->data, len,
					  PCI_DMA_TODEVICE);
	count++;
	i++;
	if (i == tx_ring->count)
		i = 0;

	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];
		len = frag->size;

		buffer_info = &tx_ring->buffer_info[i];
		BUG_ON(len >= IGB_MAX_DATA_PER_TXD);
		buffer_info->length = len;
		buffer_info->time_stamp = jiffies;
		buffer_info->dma = pci_map_page(adapter->pdev,
						frag->page,
						frag->page_offset,
						len,
						PCI_DMA_TODEVICE);

		count++;
		i++;
		if (i == tx_ring->count)
			i = 0;
	}

	i = (i == 0) ? tx_ring->count - 1 : i - 1;
	tx_ring->buffer_info[i].skb = skb;

	return count;
}

static inline void igb_tx_queue_adv(struct igb_adapter *adapter,
				    struct igb_ring *tx_ring,
				    int tx_flags, int count, u32 paylen,
				    u8 hdr_len)
{
	union e1000_adv_tx_desc *tx_desc = NULL;
	struct igb_buffer *buffer_info;
	u32 olinfo_status = 0, cmd_type_len;
	unsigned int i;

	cmd_type_len = (E1000_ADVTXD_DTYP_DATA | E1000_ADVTXD_DCMD_IFCS |
			E1000_ADVTXD_DCMD_DEXT);

	if (tx_flags & IGB_TX_FLAGS_VLAN)
		cmd_type_len |= E1000_ADVTXD_DCMD_VLE;

	if (tx_flags & IGB_TX_FLAGS_TSO) {
		cmd_type_len |= E1000_ADVTXD_DCMD_TSE;

		/* insert tcp checksum */
		olinfo_status |= E1000_TXD_POPTS_TXSM << 8;

		/* insert ip checksum */
		if (tx_flags & IGB_TX_FLAGS_IPV4)
			olinfo_status |= E1000_TXD_POPTS_IXSM << 8;

	} else if (tx_flags & IGB_TX_FLAGS_CSUM) {
		olinfo_status |= E1000_TXD_POPTS_TXSM << 8;
	}

	if ((adapter->flags & IGB_FLAG_NEED_CTX_IDX) &&
	    (tx_flags & (IGB_TX_FLAGS_CSUM | IGB_TX_FLAGS_TSO |
			 IGB_TX_FLAGS_VLAN)))
		olinfo_status |= tx_ring->queue_index << 4;

	olinfo_status |= ((paylen - hdr_len) << E1000_ADVTXD_PAYLEN_SHIFT);

	i = tx_ring->next_to_use;
	while (count--) {
		buffer_info = &tx_ring->buffer_info[i];
		tx_desc = E1000_TX_DESC_ADV(*tx_ring, i);
		tx_desc->read.buffer_addr = cpu_to_le64(buffer_info->dma);
		tx_desc->read.cmd_type_len =
			cpu_to_le32(cmd_type_len | buffer_info->length);
		tx_desc->read.olinfo_status = cpu_to_le32(olinfo_status);
		i++;
		if (i == tx_ring->count)
			i = 0;
	}

	tx_desc->read.cmd_type_len |= cpu_to_le32(adapter->txd_cmd);
	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64). */
	wmb();

	tx_ring->next_to_use = i;
	writel(i, adapter->hw.hw_addr + tx_ring->tail);
	/* we need this if more than one processor can write to our tail
	 * at a time, it syncronizes IO on IA64/Altix systems */
	mmiowb();
}

static int __igb_maybe_stop_tx(struct net_device *netdev,
			       struct igb_ring *tx_ring, int size)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	netif_stop_subqueue(netdev, tx_ring->queue_index);

	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it. */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available. */
	if (IGB_DESC_UNUSED(tx_ring) < size)
		return -EBUSY;

	/* A reprieve! */
	netif_wake_subqueue(netdev, tx_ring->queue_index);
	++adapter->restart_queue;
	return 0;
}

static int igb_maybe_stop_tx(struct net_device *netdev,
			     struct igb_ring *tx_ring, int size)
{
	if (IGB_DESC_UNUSED(tx_ring) >= size)
		return 0;
	return __igb_maybe_stop_tx(netdev, tx_ring, size);
}

#define TXD_USE_COUNT(S) (((S) >> (IGB_MAX_TXD_PWR)) + 1)

static int igb_xmit_frame_ring_adv(struct sk_buff *skb,
				   struct net_device *netdev,
				   struct igb_ring *tx_ring)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	unsigned int tx_flags = 0;
	unsigned int len;
	u8 hdr_len = 0;
	int tso = 0;

	len = skb_headlen(skb);

	if (test_bit(__IGB_DOWN, &adapter->state)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (skb->len <= 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* need: 1 descriptor per page,
	 *       + 2 desc gap to keep tail from touching head,
	 *       + 1 desc for skb->data,
	 *       + 1 desc for context descriptor,
	 * otherwise try next time */
	if (igb_maybe_stop_tx(netdev, tx_ring, skb_shinfo(skb)->nr_frags + 4)) {
		/* this is a hard error */
		return NETDEV_TX_BUSY;
	}
	skb_orphan(skb);

	if (adapter->vlgrp && vlan_tx_tag_present(skb)) {
		tx_flags |= IGB_TX_FLAGS_VLAN;
		tx_flags |= (vlan_tx_tag_get(skb) << IGB_TX_FLAGS_VLAN_SHIFT);
	}

	if (skb->protocol == htons(ETH_P_IP))
		tx_flags |= IGB_TX_FLAGS_IPV4;

	tso = skb_is_gso(skb) ? igb_tso_adv(adapter, tx_ring, skb, tx_flags,
					      &hdr_len) : 0;

	if (tso < 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (tso)
		tx_flags |= IGB_TX_FLAGS_TSO;
	else if (igb_tx_csum_adv(adapter, tx_ring, skb, tx_flags))
			if (skb->ip_summed == CHECKSUM_PARTIAL)
				tx_flags |= IGB_TX_FLAGS_CSUM;

	igb_tx_queue_adv(adapter, tx_ring, tx_flags,
			 igb_tx_map_adv(adapter, tx_ring, skb),
			 skb->len, hdr_len);

	netdev->trans_start = jiffies;

	/* Make sure there is space in the ring for the next send. */
	igb_maybe_stop_tx(netdev, tx_ring, MAX_SKB_FRAGS + 4);

	return NETDEV_TX_OK;
}

static int igb_xmit_frame_adv(struct sk_buff *skb, struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct igb_ring *tx_ring;

	int r_idx = 0;
	r_idx = skb->queue_mapping & (IGB_MAX_TX_QUEUES - 1);
	tx_ring = adapter->multi_tx_table[r_idx];

	/* This goes back to the question of how to logically map a tx queue
	 * to a flow.  Right now, performance is impacted slightly negatively
	 * if using multiple tx queues.  If the stack breaks away from a
	 * single qdisc implementation, we can look at this again. */
	return (igb_xmit_frame_ring_adv(skb, netdev, tx_ring));
}

/**
 * igb_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/
static void igb_tx_timeout(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	/* Do the reset outside of interrupt context */
	adapter->tx_timeout_count++;
	schedule_work(&adapter->reset_task);
	wr32(E1000_EICS, adapter->eims_enable_mask &
		~(E1000_EIMS_TCP_TIMER | E1000_EIMS_OTHER));
}

static void igb_reset_task(struct work_struct *work)
{
	struct igb_adapter *adapter;
	adapter = container_of(work, struct igb_adapter, reset_task);

	igb_reinit_locked(adapter);
}

/**
 * igb_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/
static struct net_device_stats *
igb_get_stats(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	/* only return the current stats */
	return &adapter->net_stats;
}

/**
 * igb_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int igb_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN;

	if ((max_frame < ETH_ZLEN + ETH_FCS_LEN) ||
	    (max_frame > MAX_JUMBO_FRAME_SIZE)) {
		dev_err(&adapter->pdev->dev, "Invalid MTU setting\n");
		return -EINVAL;
	}

#define MAX_STD_JUMBO_FRAME_SIZE 9234
	if (max_frame > MAX_STD_JUMBO_FRAME_SIZE) {
		dev_err(&adapter->pdev->dev, "MTU > 9216 not supported.\n");
		return -EINVAL;
	}

	while (test_and_set_bit(__IGB_RESETTING, &adapter->state))
		msleep(1);
	/* igb_down has a dependency on max_frame_size */
	adapter->max_frame_size = max_frame;
	if (netif_running(netdev))
		igb_down(adapter);

	/* NOTE: netdev_alloc_skb reserves 16 bytes, and typically NET_IP_ALIGN
	 * means we reserve 2 more, this pushes us to allocate from the next
	 * larger slab size.
	 * i.e. RXBUFFER_2048 --> size-4096 slab
	 */

	if (max_frame <= IGB_RXBUFFER_256)
		adapter->rx_buffer_len = IGB_RXBUFFER_256;
	else if (max_frame <= IGB_RXBUFFER_512)
		adapter->rx_buffer_len = IGB_RXBUFFER_512;
	else if (max_frame <= IGB_RXBUFFER_1024)
		adapter->rx_buffer_len = IGB_RXBUFFER_1024;
	else if (max_frame <= IGB_RXBUFFER_2048)
		adapter->rx_buffer_len = IGB_RXBUFFER_2048;
	else
#if (PAGE_SIZE / 2) > IGB_RXBUFFER_16384
		adapter->rx_buffer_len = IGB_RXBUFFER_16384;
#else
		adapter->rx_buffer_len = PAGE_SIZE / 2;
#endif
	/* adjust allocation if LPE protects us, and we aren't using SBP */
	if ((max_frame == ETH_FRAME_LEN + ETH_FCS_LEN) ||
	     (max_frame == MAXIMUM_ETHERNET_VLAN_SIZE))
		adapter->rx_buffer_len = MAXIMUM_ETHERNET_VLAN_SIZE;

	dev_info(&adapter->pdev->dev, "changing MTU from %d to %d\n",
		 netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;

	if (netif_running(netdev))
		igb_up(adapter);
	else
		igb_reset(adapter);

	clear_bit(__IGB_RESETTING, &adapter->state);

	return 0;
}

/**
 * igb_update_stats - Update the board statistics counters
 * @adapter: board private structure
 **/

void igb_update_stats(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	u16 phy_tmp;

#define PHY_IDLE_ERROR_COUNT_MASK 0x00FF

	/*
	 * Prevent stats update while adapter is being reset, or if the pci
	 * connection is down.
	 */
	if (adapter->link_speed == 0)
		return;
	if (pci_channel_offline(pdev))
		return;

	adapter->stats.crcerrs += rd32(E1000_CRCERRS);
	adapter->stats.gprc += rd32(E1000_GPRC);
	adapter->stats.gorc += rd32(E1000_GORCL);
	rd32(E1000_GORCH); /* clear GORCL */
	adapter->stats.bprc += rd32(E1000_BPRC);
	adapter->stats.mprc += rd32(E1000_MPRC);
	adapter->stats.roc += rd32(E1000_ROC);

	adapter->stats.prc64 += rd32(E1000_PRC64);
	adapter->stats.prc127 += rd32(E1000_PRC127);
	adapter->stats.prc255 += rd32(E1000_PRC255);
	adapter->stats.prc511 += rd32(E1000_PRC511);
	adapter->stats.prc1023 += rd32(E1000_PRC1023);
	adapter->stats.prc1522 += rd32(E1000_PRC1522);
	adapter->stats.symerrs += rd32(E1000_SYMERRS);
	adapter->stats.sec += rd32(E1000_SEC);

	adapter->stats.mpc += rd32(E1000_MPC);
	adapter->stats.scc += rd32(E1000_SCC);
	adapter->stats.ecol += rd32(E1000_ECOL);
	adapter->stats.mcc += rd32(E1000_MCC);
	adapter->stats.latecol += rd32(E1000_LATECOL);
	adapter->stats.dc += rd32(E1000_DC);
	adapter->stats.rlec += rd32(E1000_RLEC);
	adapter->stats.xonrxc += rd32(E1000_XONRXC);
	adapter->stats.xontxc += rd32(E1000_XONTXC);
	adapter->stats.xoffrxc += rd32(E1000_XOFFRXC);
	adapter->stats.xofftxc += rd32(E1000_XOFFTXC);
	adapter->stats.fcruc += rd32(E1000_FCRUC);
	adapter->stats.gptc += rd32(E1000_GPTC);
	adapter->stats.gotc += rd32(E1000_GOTCL);
	rd32(E1000_GOTCH); /* clear GOTCL */
	adapter->stats.rnbc += rd32(E1000_RNBC);
	adapter->stats.ruc += rd32(E1000_RUC);
	adapter->stats.rfc += rd32(E1000_RFC);
	adapter->stats.rjc += rd32(E1000_RJC);
	adapter->stats.tor += rd32(E1000_TORH);
	adapter->stats.tot += rd32(E1000_TOTH);
	adapter->stats.tpr += rd32(E1000_TPR);

	adapter->stats.ptc64 += rd32(E1000_PTC64);
	adapter->stats.ptc127 += rd32(E1000_PTC127);
	adapter->stats.ptc255 += rd32(E1000_PTC255);
	adapter->stats.ptc511 += rd32(E1000_PTC511);
	adapter->stats.ptc1023 += rd32(E1000_PTC1023);
	adapter->stats.ptc1522 += rd32(E1000_PTC1522);

	adapter->stats.mptc += rd32(E1000_MPTC);
	adapter->stats.bptc += rd32(E1000_BPTC);

	/* used for adaptive IFS */

	hw->mac.tx_packet_delta = rd32(E1000_TPT);
	adapter->stats.tpt += hw->mac.tx_packet_delta;
	hw->mac.collision_delta = rd32(E1000_COLC);
	adapter->stats.colc += hw->mac.collision_delta;

	adapter->stats.algnerrc += rd32(E1000_ALGNERRC);
	adapter->stats.rxerrc += rd32(E1000_RXERRC);
	adapter->stats.tncrs += rd32(E1000_TNCRS);
	adapter->stats.tsctc += rd32(E1000_TSCTC);
	adapter->stats.tsctfc += rd32(E1000_TSCTFC);

	adapter->stats.iac += rd32(E1000_IAC);
	adapter->stats.icrxoc += rd32(E1000_ICRXOC);
	adapter->stats.icrxptc += rd32(E1000_ICRXPTC);
	adapter->stats.icrxatc += rd32(E1000_ICRXATC);
	adapter->stats.ictxptc += rd32(E1000_ICTXPTC);
	adapter->stats.ictxatc += rd32(E1000_ICTXATC);
	adapter->stats.ictxqec += rd32(E1000_ICTXQEC);
	adapter->stats.ictxqmtc += rd32(E1000_ICTXQMTC);
	adapter->stats.icrxdmtc += rd32(E1000_ICRXDMTC);

	/* Fill out the OS statistics structure */
	adapter->net_stats.multicast = adapter->stats.mprc;
	adapter->net_stats.collisions = adapter->stats.colc;

	/* Rx Errors */

	/* RLEC on some newer hardware can be incorrect so build
	* our own version based on RUC and ROC */
	adapter->net_stats.rx_errors = adapter->stats.rxerrc +
		adapter->stats.crcerrs + adapter->stats.algnerrc +
		adapter->stats.ruc + adapter->stats.roc +
		adapter->stats.cexterr;
	adapter->net_stats.rx_length_errors = adapter->stats.ruc +
					      adapter->stats.roc;
	adapter->net_stats.rx_crc_errors = adapter->stats.crcerrs;
	adapter->net_stats.rx_frame_errors = adapter->stats.algnerrc;
	adapter->net_stats.rx_missed_errors = adapter->stats.mpc;

	/* Tx Errors */
	adapter->net_stats.tx_errors = adapter->stats.ecol +
				       adapter->stats.latecol;
	adapter->net_stats.tx_aborted_errors = adapter->stats.ecol;
	adapter->net_stats.tx_window_errors = adapter->stats.latecol;
	adapter->net_stats.tx_carrier_errors = adapter->stats.tncrs;

	/* Tx Dropped needs to be maintained elsewhere */

	/* Phy Stats */
	if (hw->phy.media_type == e1000_media_type_copper) {
		if ((adapter->link_speed == SPEED_1000) &&
		   (!hw->phy.ops.read_phy_reg(hw, PHY_1000T_STATUS,
					      &phy_tmp))) {
			phy_tmp &= PHY_IDLE_ERROR_COUNT_MASK;
			adapter->phy_stats.idle_errors += phy_tmp;
		}
	}

	/* Management Stats */
	adapter->stats.mgptc += rd32(E1000_MGTPTC);
	adapter->stats.mgprc += rd32(E1000_MGTPRC);
	adapter->stats.mgpdc += rd32(E1000_MGTPDC);
}


static irqreturn_t igb_msix_other(int irq, void *data)
{
	struct net_device *netdev = data;
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 icr = rd32(E1000_ICR);

	/* reading ICR causes bit 31 of EICR to be cleared */
	if (!(icr & E1000_ICR_LSC))
		goto no_link_interrupt;
	hw->mac.get_link_status = 1;
	/* guard against interrupt when we're going down */
	if (!test_bit(__IGB_DOWN, &adapter->state))
		mod_timer(&adapter->watchdog_timer, jiffies + 1);
	
no_link_interrupt:
	wr32(E1000_IMS, E1000_IMS_LSC);
	wr32(E1000_EIMS, adapter->eims_other);

	return IRQ_HANDLED;
}

static irqreturn_t igb_msix_tx(int irq, void *data)
{
	struct igb_ring *tx_ring = data;
	struct igb_adapter *adapter = tx_ring->adapter;
	struct e1000_hw *hw = &adapter->hw;

#ifdef CONFIG_DCA
	if (adapter->flags & IGB_FLAG_DCA_ENABLED)
		igb_update_tx_dca(tx_ring);
#endif
	tx_ring->total_bytes = 0;
	tx_ring->total_packets = 0;

	/* auto mask will automatically reenable the interrupt when we write
	 * EICS */
	if (!igb_clean_tx_irq(tx_ring))
		/* Ring was not completely cleaned, so fire another interrupt */
		wr32(E1000_EICS, tx_ring->eims_value);
	else
		wr32(E1000_EIMS, tx_ring->eims_value);

	return IRQ_HANDLED;
}

static void igb_write_itr(struct igb_ring *ring)
{
	struct e1000_hw *hw = &ring->adapter->hw;
	if ((ring->adapter->itr_setting & 3) && ring->set_itr) {
		switch (hw->mac.type) {
		case e1000_82576:
			wr32(ring->itr_register,
			     ring->itr_val |
			     0x80000000);
			break;
		default:
			wr32(ring->itr_register,
			     ring->itr_val |
			     (ring->itr_val << 16));
			break;
		}
		ring->set_itr = 0;
	}
}

static irqreturn_t igb_msix_rx(int irq, void *data)
{
	struct igb_ring *rx_ring = data;
	struct igb_adapter *adapter = rx_ring->adapter;

	/* Write the ITR value calculated at the end of the
	 * previous interrupt.
	 */

	igb_write_itr(rx_ring);

	if (netif_rx_schedule_prep(adapter->netdev, &rx_ring->napi))
		__netif_rx_schedule(adapter->netdev, &rx_ring->napi);

#ifdef CONFIG_DCA
	if (adapter->flags & IGB_FLAG_DCA_ENABLED)
		igb_update_rx_dca(rx_ring);
#endif
		return IRQ_HANDLED;
}

#ifdef CONFIG_DCA
static void igb_update_rx_dca(struct igb_ring *rx_ring)
{
	u32 dca_rxctrl;
	struct igb_adapter *adapter = rx_ring->adapter;
	struct e1000_hw *hw = &adapter->hw;
	int cpu = get_cpu();
	int q = rx_ring - adapter->rx_ring;

	if (rx_ring->cpu != cpu) {
		dca_rxctrl = rd32(E1000_DCA_RXCTRL(q));
		if (hw->mac.type == e1000_82576) {
			dca_rxctrl &= ~E1000_DCA_RXCTRL_CPUID_MASK_82576;
			dca_rxctrl |= dca_get_tag(cpu) <<
			              E1000_DCA_RXCTRL_CPUID_SHIFT;
		} else {
			dca_rxctrl &= ~E1000_DCA_RXCTRL_CPUID_MASK;
			dca_rxctrl |= dca_get_tag(cpu);
		}
		dca_rxctrl |= E1000_DCA_RXCTRL_DESC_DCA_EN;
		dca_rxctrl |= E1000_DCA_RXCTRL_HEAD_DCA_EN;
		dca_rxctrl |= E1000_DCA_RXCTRL_DATA_DCA_EN;
		wr32(E1000_DCA_RXCTRL(q), dca_rxctrl);
		rx_ring->cpu = cpu;
	}
	put_cpu();
}

static void igb_update_tx_dca(struct igb_ring *tx_ring)
{
	u32 dca_txctrl;
	struct igb_adapter *adapter = tx_ring->adapter;
	struct e1000_hw *hw = &adapter->hw;
	int cpu = get_cpu();
	int q = tx_ring - adapter->tx_ring;

	if (tx_ring->cpu != cpu) {
		dca_txctrl = rd32(E1000_DCA_TXCTRL(q));
		if (hw->mac.type == e1000_82576) {
			dca_txctrl &= ~E1000_DCA_TXCTRL_CPUID_MASK_82576;
			dca_txctrl |= dca_get_tag(cpu) <<
			              E1000_DCA_TXCTRL_CPUID_SHIFT;
		} else {
			dca_txctrl &= ~E1000_DCA_TXCTRL_CPUID_MASK;
			dca_txctrl |= dca_get_tag(cpu);
		}
		dca_txctrl |= E1000_DCA_TXCTRL_DESC_DCA_EN;
		wr32(E1000_DCA_TXCTRL(q), dca_txctrl);
		tx_ring->cpu = cpu;
	}
	put_cpu();
}

static void igb_setup_dca(struct igb_adapter *adapter)
{
	int i;

	if (!(adapter->flags & IGB_FLAG_DCA_ENABLED))
		return;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		adapter->tx_ring[i].cpu = -1;
		igb_update_tx_dca(&adapter->tx_ring[i]);
	}
	for (i = 0; i < adapter->num_rx_queues; i++) {
		adapter->rx_ring[i].cpu = -1;
		igb_update_rx_dca(&adapter->rx_ring[i]);
	}
}

static int __igb_notify_dca(struct device *dev, void *data)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	unsigned long event = *(unsigned long *)data;

	if (!(adapter->flags & IGB_FLAG_HAS_DCA))
		goto out;

	switch (event) {
	case DCA_PROVIDER_ADD:
		/* if already enabled, don't do it again */
		if (adapter->flags & IGB_FLAG_DCA_ENABLED)
			break;
		adapter->flags |= IGB_FLAG_DCA_ENABLED;
		/* Always use CB2 mode, difference is masked
		 * in the CB driver. */
		wr32(E1000_DCA_CTRL, 2);
		if (dca_add_requester(dev) == 0) {
			dev_info(&adapter->pdev->dev, "DCA enabled\n");
			igb_setup_dca(adapter);
			break;
		}
		/* Fall Through since DCA is disabled. */
	case DCA_PROVIDER_REMOVE:
		if (adapter->flags & IGB_FLAG_DCA_ENABLED) {
			/* without this a class_device is left
 			 * hanging around in the sysfs model */
			dca_remove_requester(dev);
			dev_info(&adapter->pdev->dev, "DCA disabled\n");
			adapter->flags &= ~IGB_FLAG_DCA_ENABLED;
			wr32(E1000_DCA_CTRL, 1);
		}
		break;
	}
out:
	return 0;
}

static int igb_notify_dca(struct notifier_block *nb, unsigned long event,
                          void *p)
{
	int ret_val;

	ret_val = driver_for_each_device(&igb_driver.driver, NULL, &event,
	                                 __igb_notify_dca);

	return ret_val ? NOTIFY_BAD : NOTIFY_DONE;
}
#endif /* CONFIG_DCA */

/**
 * igb_intr_msi - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t igb_intr_msi(int irq, void *data)
{
	struct net_device *netdev = data;
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	/* read ICR disables interrupts using IAM */
	u32 icr = rd32(E1000_ICR);

	igb_write_itr(adapter->rx_ring);

	if (icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		hw->mac.get_link_status = 1;
		if (!test_bit(__IGB_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

	netif_rx_schedule(netdev, &adapter->rx_ring[0].napi);

	return IRQ_HANDLED;
}

/**
 * igb_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t igb_intr(int irq, void *data)
{
	struct net_device *netdev = data;
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	/* Interrupt Auto-Mask...upon reading ICR, interrupts are masked.  No
	 * need for the IMC write */
	u32 icr = rd32(E1000_ICR);
	u32 eicr = 0;
	if (!icr)
		return IRQ_NONE;  /* Not our interrupt */

	igb_write_itr(adapter->rx_ring);

	/* IMS will not auto-mask if INT_ASSERTED is not set, and if it is
	 * not set, then the adapter didn't send an interrupt */
	if (!(icr & E1000_ICR_INT_ASSERTED))
		return IRQ_NONE;

	eicr = rd32(E1000_EICR);

	if (icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		hw->mac.get_link_status = 1;
		/* guard against interrupt when we're going down */
		if (!test_bit(__IGB_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

	netif_rx_schedule(netdev, &adapter->rx_ring[0].napi);

	return IRQ_HANDLED;
}

/**
 * igb_poll - NAPI Rx polling callback
 * @napi: napi polling structure
 * @budget: count of how many packets we should handle
 **/
static int igb_poll(struct napi_struct *napi, int budget)
{
	struct igb_ring *rx_ring = container_of(napi, struct igb_ring, napi);
	struct igb_adapter *adapter = rx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	int tx_clean_complete, work_done = 0;

	/* this poll routine only supports one tx and one rx queue */
#ifdef CONFIG_DCA
	if (adapter->flags & IGB_FLAG_DCA_ENABLED)
		igb_update_tx_dca(&adapter->tx_ring[0]);
#endif
	tx_clean_complete = igb_clean_tx_irq(&adapter->tx_ring[0]);

#ifdef CONFIG_DCA
	if (adapter->flags & IGB_FLAG_DCA_ENABLED)
		igb_update_rx_dca(&adapter->rx_ring[0]);
#endif
	igb_clean_rx_irq_adv(&adapter->rx_ring[0], &work_done, budget);

	/* If no Tx and not enough Rx work done, exit the polling mode */
	if ((tx_clean_complete && (work_done < budget)) ||
	    !netif_running(netdev)) {
		if (adapter->itr_setting & 3)
			igb_set_itr(adapter);
		netif_rx_complete(netdev, napi);
		if (!test_bit(__IGB_DOWN, &adapter->state))
			igb_irq_enable(adapter);
		return 0;
	}

	return 1;
}

static int igb_clean_rx_ring_msix(struct napi_struct *napi, int budget)
{
	struct igb_ring *rx_ring = container_of(napi, struct igb_ring, napi);
	struct igb_adapter *adapter = rx_ring->adapter;
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	int work_done = 0;

#ifdef CONFIG_DCA
	if (adapter->flags & IGB_FLAG_DCA_ENABLED)
		igb_update_rx_dca(rx_ring);
#endif
	igb_clean_rx_irq_adv(rx_ring, &work_done, budget);


	/* If not enough Rx work done, exit the polling mode */
	if ((work_done == 0) || !netif_running(netdev)) {
		netif_rx_complete(netdev, napi);

		if (adapter->itr_setting & 3) {
			if (adapter->num_rx_queues == 1)
				igb_set_itr(adapter);
			else
				igb_update_ring_itr(rx_ring);
		}

		if (!test_bit(__IGB_DOWN, &adapter->state))
			wr32(E1000_EIMS, rx_ring->eims_value);

		return 0;
	}

	return 1;
}

static inline u32 get_head(struct igb_ring *tx_ring)
{
	void *end = (struct e1000_tx_desc *)tx_ring->desc + tx_ring->count;
	return le32_to_cpu(*(volatile __le32 *)end);
}

/**
 * igb_clean_tx_irq - Reclaim resources after transmit completes
 * @adapter: board private structure
 * returns true if ring is completely cleaned
 **/
static bool igb_clean_tx_irq(struct igb_ring *tx_ring)
{
	struct igb_adapter *adapter = tx_ring->adapter;
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct e1000_tx_desc *tx_desc;
	struct igb_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	u32 head, oldhead;
	unsigned int count = 0;
	unsigned int total_bytes = 0, total_packets = 0;
	bool retval = true;

	rmb();
	head = get_head(tx_ring);
	i = tx_ring->next_to_clean;
	while (1) {
		while (i != head) {
			tx_desc = E1000_TX_DESC(*tx_ring, i);
			buffer_info = &tx_ring->buffer_info[i];
			skb = buffer_info->skb;

			if (skb) {
				unsigned int segs, bytecount;
				/* gso_segs is currently only valid for tcp */
				segs = skb_shinfo(skb)->gso_segs ?: 1;
				/* multiply data chunks by size of headers */
				bytecount = ((segs - 1) * skb_headlen(skb)) +
					    skb->len;
				total_packets += segs;
				total_bytes += bytecount;
			}

			igb_unmap_and_free_tx_resource(adapter, buffer_info);

			i++;
			if (i == tx_ring->count)
				i = 0;

			count++;
			if (count == IGB_MAX_TX_CLEAN) {
				retval = false;
				goto done_cleaning;
			}
		}
		oldhead = head;
		rmb();
		head = get_head(tx_ring);
		if (head == oldhead)
			goto done_cleaning;
	}  /* while (1) */

done_cleaning:
	tx_ring->next_to_clean = i;

	if (unlikely(count &&
		     netif_carrier_ok(netdev) &&
		     IGB_DESC_UNUSED(tx_ring) >= IGB_TX_QUEUE_WAKE)) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (__netif_subqueue_stopped(netdev, tx_ring->queue_index) &&
		    !(test_bit(__IGB_DOWN, &adapter->state))) {
			netif_wake_subqueue(netdev, tx_ring->queue_index);
			++adapter->restart_queue;
		}
	}

	if (tx_ring->detect_tx_hung) {
		/* Detect a transmit hang in hardware, this serializes the
		 * check with the clearing of time_stamp and movement of i */
		tx_ring->detect_tx_hung = false;
		if (tx_ring->buffer_info[i].time_stamp &&
		    time_after(jiffies, tx_ring->buffer_info[i].time_stamp +
			       (adapter->tx_timeout_factor * HZ))
		    && !(rd32(E1000_STATUS) &
			 E1000_STATUS_TXOFF)) {

			tx_desc = E1000_TX_DESC(*tx_ring, i);
			/* detected Tx unit hang */
			dev_err(&adapter->pdev->dev,
				"Detected Tx Unit Hang\n"
				"  Tx Queue             <%d>\n"
				"  TDH                  <%x>\n"
				"  TDT                  <%x>\n"
				"  next_to_use          <%x>\n"
				"  next_to_clean        <%x>\n"
				"  head (WB)            <%x>\n"
				"buffer_info[next_to_clean]\n"
				"  time_stamp           <%lx>\n"
				"  jiffies              <%lx>\n"
				"  desc.status          <%x>\n",
				tx_ring->queue_index,
				readl(adapter->hw.hw_addr + tx_ring->head),
				readl(adapter->hw.hw_addr + tx_ring->tail),
				tx_ring->next_to_use,
				tx_ring->next_to_clean,
				head,
				tx_ring->buffer_info[i].time_stamp,
				jiffies,
				tx_desc->upper.fields.status);
			netif_stop_subqueue(netdev, tx_ring->queue_index);
		}
	}
	tx_ring->total_bytes += total_bytes;
	tx_ring->total_packets += total_packets;
	tx_ring->tx_stats.bytes += total_bytes;
	tx_ring->tx_stats.packets += total_packets;
	adapter->net_stats.tx_bytes += total_bytes;
	adapter->net_stats.tx_packets += total_packets;
	return retval;
}

#ifdef CONFIG_IGB_LRO
 /**
 * igb_get_skb_hdr - helper function for LRO header processing
 * @skb: pointer to sk_buff to be added to LRO packet
 * @iphdr: pointer to ip header structure
 * @tcph: pointer to tcp header structure
 * @hdr_flags: pointer to header flags
 * @priv: pointer to the receive descriptor for the current sk_buff
 **/
static int igb_get_skb_hdr(struct sk_buff *skb, void **iphdr, void **tcph,
                           u64 *hdr_flags, void *priv)
{
	union e1000_adv_rx_desc *rx_desc = priv;
	u16 pkt_type = rx_desc->wb.lower.lo_dword.pkt_info &
	               (E1000_RXDADV_PKTTYPE_IPV4 | E1000_RXDADV_PKTTYPE_TCP);

	/* Verify that this is a valid IPv4 TCP packet */
	if (pkt_type != (E1000_RXDADV_PKTTYPE_IPV4 |
	                  E1000_RXDADV_PKTTYPE_TCP))
		return -1;

	/* Set network headers */
	skb_reset_network_header(skb);
	skb_set_transport_header(skb, ip_hdrlen(skb));
	*iphdr = ip_hdr(skb);
	*tcph = tcp_hdr(skb);
	*hdr_flags = LRO_IPV4 | LRO_TCP;

	return 0;

}
#endif /* CONFIG_IGB_LRO */

/**
 * igb_receive_skb - helper function to handle rx indications
 * @ring: pointer to receive ring receving this packet 
 * @status: descriptor status field as written by hardware
 * @vlan: descriptor vlan field as written by hardware (no le/be conversion)
 * @skb: pointer to sk_buff to be indicated to stack
 **/
static void igb_receive_skb(struct igb_ring *ring, u8 status,
                            union e1000_adv_rx_desc * rx_desc,
                            struct sk_buff *skb)
{
	struct igb_adapter * adapter = ring->adapter;
	bool vlan_extracted = (adapter->vlgrp && (status & E1000_RXD_STAT_VP));

#ifdef CONFIG_IGB_LRO
	if (adapter->netdev->features & NETIF_F_LRO &&
	    skb->ip_summed == CHECKSUM_UNNECESSARY) {
		if (vlan_extracted)
			lro_vlan_hwaccel_receive_skb(&ring->lro_mgr, skb,
			                   adapter->vlgrp,
			                   le16_to_cpu(rx_desc->wb.upper.vlan),
			                   rx_desc);
		else
			lro_receive_skb(&ring->lro_mgr,skb, rx_desc);
		ring->lro_used = 1;
	} else {
#endif
		if (vlan_extracted)
			vlan_hwaccel_receive_skb(skb, adapter->vlgrp,
			                  le16_to_cpu(rx_desc->wb.upper.vlan));
		else

			netif_receive_skb(skb);
#ifdef CONFIG_IGB_LRO
	}
#endif
}


static inline void igb_rx_checksum_adv(struct igb_adapter *adapter,
				       u32 status_err, struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_NONE;

	/* Ignore Checksum bit is set or checksum is disabled through ethtool */
	if ((status_err & E1000_RXD_STAT_IXSM) || !adapter->rx_csum)
		return;
	/* TCP/UDP checksum error bit is set */
	if (status_err &
	    (E1000_RXDEXT_STATERR_TCPE | E1000_RXDEXT_STATERR_IPE)) {
		/* let the stack verify checksum errors */
		adapter->hw_csum_err++;
		return;
	}
	/* It must be a TCP or UDP packet with a valid checksum */
	if (status_err & (E1000_RXD_STAT_TCPCS | E1000_RXD_STAT_UDPCS))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	adapter->hw_csum_good++;
}

static bool igb_clean_rx_irq_adv(struct igb_ring *rx_ring,
				 int *work_done, int budget)
{
	struct igb_adapter *adapter = rx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union e1000_adv_rx_desc *rx_desc , *next_rxd;
	struct igb_buffer *buffer_info , *next_buffer;
	struct sk_buff *skb;
	unsigned int i;
	u32 length, hlen, staterr;
	bool cleaned = false;
	int cleaned_count = 0;
	unsigned int total_bytes = 0, total_packets = 0;

	i = rx_ring->next_to_clean;
	rx_desc = E1000_RX_DESC_ADV(*rx_ring, i);
	staterr = le32_to_cpu(rx_desc->wb.upper.status_error);

	while (staterr & E1000_RXD_STAT_DD) {
		if (*work_done >= budget)
			break;
		(*work_done)++;
		buffer_info = &rx_ring->buffer_info[i];

		/* HW will not DMA in data larger than the given buffer, even
		 * if it parses the (NFS, of course) header to be larger.  In
		 * that case, it fills the header buffer and spills the rest
		 * into the page.
		 */
		hlen = (le16_to_cpu(rx_desc->wb.lower.lo_dword.hdr_info) &
		  E1000_RXDADV_HDRBUFLEN_MASK) >> E1000_RXDADV_HDRBUFLEN_SHIFT;
		if (hlen > adapter->rx_ps_hdr_size)
			hlen = adapter->rx_ps_hdr_size;

		length = le16_to_cpu(rx_desc->wb.upper.length);
		cleaned = true;
		cleaned_count++;

		skb = buffer_info->skb;
		prefetch(skb->data - NET_IP_ALIGN);
		buffer_info->skb = NULL;
		if (!adapter->rx_ps_hdr_size) {
			pci_unmap_single(pdev, buffer_info->dma,
					 adapter->rx_buffer_len +
					   NET_IP_ALIGN,
					 PCI_DMA_FROMDEVICE);
			skb_put(skb, length);
			goto send_up;
		}

		if (!skb_shinfo(skb)->nr_frags) {
			pci_unmap_single(pdev, buffer_info->dma,
					 adapter->rx_ps_hdr_size +
					   NET_IP_ALIGN,
					 PCI_DMA_FROMDEVICE);
			skb_put(skb, hlen);
		}

		if (length) {
			pci_unmap_page(pdev, buffer_info->page_dma,
				       PAGE_SIZE / 2, PCI_DMA_FROMDEVICE);
			buffer_info->page_dma = 0;

			skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags++,
						buffer_info->page,
						buffer_info->page_offset,
						length);

			if ((adapter->rx_buffer_len > (PAGE_SIZE / 2)) ||
			    (page_count(buffer_info->page) != 1))
				buffer_info->page = NULL;
			else
				get_page(buffer_info->page);

			skb->len += length;
			skb->data_len += length;

			skb->truesize += length;
		}
send_up:
		i++;
		if (i == rx_ring->count)
			i = 0;
		next_rxd = E1000_RX_DESC_ADV(*rx_ring, i);
		prefetch(next_rxd);
		next_buffer = &rx_ring->buffer_info[i];

		if (!(staterr & E1000_RXD_STAT_EOP)) {
			buffer_info->skb = xchg(&next_buffer->skb, skb);
			buffer_info->dma = xchg(&next_buffer->dma, 0);
			goto next_desc;
		}

		if (staterr & E1000_RXDEXT_ERR_FRAME_ERR_MASK) {
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		total_bytes += skb->len;
		total_packets++;

		igb_rx_checksum_adv(adapter, staterr, skb);

		skb->protocol = eth_type_trans(skb, netdev);

		igb_receive_skb(rx_ring, staterr, rx_desc, skb);

		netdev->last_rx = jiffies;

next_desc:
		rx_desc->wb.upper.status_error = 0;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= IGB_RX_BUFFER_WRITE) {
			igb_alloc_rx_buffers_adv(rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		buffer_info = next_buffer;

		staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	}

	rx_ring->next_to_clean = i;
	cleaned_count = IGB_DESC_UNUSED(rx_ring);

#ifdef CONFIG_IGB_LRO
	if (rx_ring->lro_used) {
		lro_flush_all(&rx_ring->lro_mgr);
		rx_ring->lro_used = 0;
	}
#endif

	if (cleaned_count)
		igb_alloc_rx_buffers_adv(rx_ring, cleaned_count);

	rx_ring->total_packets += total_packets;
	rx_ring->total_bytes += total_bytes;
	rx_ring->rx_stats.packets += total_packets;
	rx_ring->rx_stats.bytes += total_bytes;
	adapter->net_stats.rx_bytes += total_bytes;
	adapter->net_stats.rx_packets += total_packets;
	return cleaned;
}


/**
 * igb_alloc_rx_buffers_adv - Replace used receive buffers; packet split
 * @adapter: address of board private structure
 **/
static void igb_alloc_rx_buffers_adv(struct igb_ring *rx_ring,
				     int cleaned_count)
{
	struct igb_adapter *adapter = rx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union e1000_adv_rx_desc *rx_desc;
	struct igb_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;

	i = rx_ring->next_to_use;
	buffer_info = &rx_ring->buffer_info[i];

	while (cleaned_count--) {
		rx_desc = E1000_RX_DESC_ADV(*rx_ring, i);

		if (adapter->rx_ps_hdr_size && !buffer_info->page_dma) {
			if (!buffer_info->page) {
				buffer_info->page = alloc_page(GFP_ATOMIC);
				if (!buffer_info->page) {
					adapter->alloc_rx_buff_failed++;
					goto no_buffers;
				}
				buffer_info->page_offset = 0;
			} else {
				buffer_info->page_offset ^= PAGE_SIZE / 2;
			}
			buffer_info->page_dma =
				pci_map_page(pdev,
					     buffer_info->page,
					     buffer_info->page_offset,
					     PAGE_SIZE / 2,
					     PCI_DMA_FROMDEVICE);
		}

		if (!buffer_info->skb) {
			int bufsz;

			if (adapter->rx_ps_hdr_size)
				bufsz = adapter->rx_ps_hdr_size;
			else
				bufsz = adapter->rx_buffer_len;
			bufsz += NET_IP_ALIGN;
			skb = netdev_alloc_skb(netdev, bufsz);

			if (!skb) {
				adapter->alloc_rx_buff_failed++;
				goto no_buffers;
			}

			/* Make buffer alignment 2 beyond a 16 byte boundary
			 * this will result in a 16 byte aligned IP header after
			 * the 14 byte MAC header is removed
			 */
			skb_reserve(skb, NET_IP_ALIGN);

			buffer_info->skb = skb;
			buffer_info->dma = pci_map_single(pdev, skb->data,
							  bufsz,
							  PCI_DMA_FROMDEVICE);

		}
		/* Refresh the desc even if buffer_addrs didn't change because
		 * each write-back erases this info. */
		if (adapter->rx_ps_hdr_size) {
			rx_desc->read.pkt_addr =
			     cpu_to_le64(buffer_info->page_dma);
			rx_desc->read.hdr_addr = cpu_to_le64(buffer_info->dma);
		} else {
			rx_desc->read.pkt_addr =
			     cpu_to_le64(buffer_info->dma);
			rx_desc->read.hdr_addr = 0;
		}

		i++;
		if (i == rx_ring->count)
			i = 0;
		buffer_info = &rx_ring->buffer_info[i];
	}

no_buffers:
	if (rx_ring->next_to_use != i) {
		rx_ring->next_to_use = i;
		if (i == 0)
			i = (rx_ring->count - 1);
		else
			i--;

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64). */
		wmb();
		writel(i, adapter->hw.hw_addr + rx_ring->tail);
	}
}

/**
 * igb_mii_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 **/
static int igb_mii_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct mii_ioctl_data *data = if_mii(ifr);

	if (adapter->hw.phy.media_type != e1000_media_type_copper)
		return -EOPNOTSUPP;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = adapter->hw.phy.addr;
		break;
	case SIOCGMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (adapter->hw.phy.ops.read_phy_reg(&adapter->hw,
						     data->reg_num
						     & 0x1F, &data->val_out))
			return -EIO;
		break;
	case SIOCSMIIREG:
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/**
 * igb_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 **/
static int igb_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return igb_mii_ioctl(netdev, ifr, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

static void igb_vlan_rx_register(struct net_device *netdev,
				 struct vlan_group *grp)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl, rctl;

	igb_irq_disable(adapter);
	adapter->vlgrp = grp;

	if (grp) {
		/* enable VLAN tag insert/strip */
		ctrl = rd32(E1000_CTRL);
		ctrl |= E1000_CTRL_VME;
		wr32(E1000_CTRL, ctrl);

		/* enable VLAN receive filtering */
		rctl = rd32(E1000_RCTL);
		rctl &= ~E1000_RCTL_CFIEN;
		wr32(E1000_RCTL, rctl);
		igb_update_mng_vlan(adapter);
		wr32(E1000_RLPML,
				adapter->max_frame_size + VLAN_TAG_SIZE);
	} else {
		/* disable VLAN tag insert/strip */
		ctrl = rd32(E1000_CTRL);
		ctrl &= ~E1000_CTRL_VME;
		wr32(E1000_CTRL, ctrl);

		if (adapter->mng_vlan_id != (u16)IGB_MNG_VLAN_NONE) {
			igb_vlan_rx_kill_vid(netdev, adapter->mng_vlan_id);
			adapter->mng_vlan_id = IGB_MNG_VLAN_NONE;
		}
		wr32(E1000_RLPML,
				adapter->max_frame_size);
	}

	if (!test_bit(__IGB_DOWN, &adapter->state))
		igb_irq_enable(adapter);
}

static void igb_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 vfta, index;

	if ((adapter->hw.mng_cookie.status &
	     E1000_MNG_DHCP_COOKIE_STATUS_VLAN) &&
	    (vid == adapter->mng_vlan_id))
		return;
	/* add VID to filter table */
	index = (vid >> 5) & 0x7F;
	vfta = array_rd32(E1000_VFTA, index);
	vfta |= (1 << (vid & 0x1F));
	igb_write_vfta(&adapter->hw, index, vfta);
}

static void igb_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 vfta, index;

	igb_irq_disable(adapter);
	vlan_group_set_device(adapter->vlgrp, vid, NULL);

	if (!test_bit(__IGB_DOWN, &adapter->state))
		igb_irq_enable(adapter);

	if ((adapter->hw.mng_cookie.status &
	     E1000_MNG_DHCP_COOKIE_STATUS_VLAN) &&
	    (vid == adapter->mng_vlan_id)) {
		/* release control to f/w */
		igb_release_hw_control(adapter);
		return;
	}

	/* remove VID from filter table */
	index = (vid >> 5) & 0x7F;
	vfta = array_rd32(E1000_VFTA, index);
	vfta &= ~(1 << (vid & 0x1F));
	igb_write_vfta(&adapter->hw, index, vfta);
}

static void igb_restore_vlan(struct igb_adapter *adapter)
{
	igb_vlan_rx_register(adapter->netdev, adapter->vlgrp);

	if (adapter->vlgrp) {
		u16 vid;
		for (vid = 0; vid < VLAN_GROUP_ARRAY_LEN; vid++) {
			if (!vlan_group_get_device(adapter->vlgrp, vid))
				continue;
			igb_vlan_rx_add_vid(adapter->netdev, vid);
		}
	}
}

int igb_set_spd_dplx(struct igb_adapter *adapter, u16 spddplx)
{
	struct e1000_mac_info *mac = &adapter->hw.mac;

	mac->autoneg = 0;

	/* Fiber NICs only allow 1000 gbps Full duplex */
	if ((adapter->hw.phy.media_type == e1000_media_type_fiber) &&
		spddplx != (SPEED_1000 + DUPLEX_FULL)) {
		dev_err(&adapter->pdev->dev,
			"Unsupported Speed/Duplex configuration\n");
		return -EINVAL;
	}

	switch (spddplx) {
	case SPEED_10 + DUPLEX_HALF:
		mac->forced_speed_duplex = ADVERTISE_10_HALF;
		break;
	case SPEED_10 + DUPLEX_FULL:
		mac->forced_speed_duplex = ADVERTISE_10_FULL;
		break;
	case SPEED_100 + DUPLEX_HALF:
		mac->forced_speed_duplex = ADVERTISE_100_HALF;
		break;
	case SPEED_100 + DUPLEX_FULL:
		mac->forced_speed_duplex = ADVERTISE_100_FULL;
		break;
	case SPEED_1000 + DUPLEX_FULL:
		mac->autoneg = 1;
		adapter->hw.phy.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case SPEED_1000 + DUPLEX_HALF: /* not supported */
	default:
		dev_err(&adapter->pdev->dev,
			"Unsupported Speed/Duplex configuration\n");
		return -EINVAL;
	}
	return 0;
}


static int igb_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl, rctl, status;
	u32 wufc = adapter->wol;
#ifdef CONFIG_PM
	int retval = 0;
#endif

	netif_device_detach(netdev);

	if (netif_running(netdev))
		igb_close(netdev);

	igb_reset_interrupt_capability(adapter);

	igb_free_queues(adapter);

#ifdef CONFIG_PM
	retval = pci_save_state(pdev);
	if (retval)
		return retval;
#endif

	status = rd32(E1000_STATUS);
	if (status & E1000_STATUS_LU)
		wufc &= ~E1000_WUFC_LNKC;

	if (wufc) {
		igb_setup_rctl(adapter);
		igb_set_multi(netdev);

		/* turn on all-multi mode if wake on multicast is enabled */
		if (wufc & E1000_WUFC_MC) {
			rctl = rd32(E1000_RCTL);
			rctl |= E1000_RCTL_MPE;
			wr32(E1000_RCTL, rctl);
		}

		ctrl = rd32(E1000_CTRL);
		/* advertise wake from D3Cold */
		#define E1000_CTRL_ADVD3WUC 0x00100000
		/* phy power management enable */
		#define E1000_CTRL_EN_PHY_PWR_MGMT 0x00200000
		ctrl |= E1000_CTRL_ADVD3WUC;
		wr32(E1000_CTRL, ctrl);

		/* Allow time for pending master requests to run */
		igb_disable_pcie_master(&adapter->hw);

		wr32(E1000_WUC, E1000_WUC_PME_EN);
		wr32(E1000_WUFC, wufc);
	} else {
		wr32(E1000_WUC, 0);
		wr32(E1000_WUFC, 0);
	}

	/* make sure adapter isn't asleep if manageability/wol is enabled */
	if (wufc || adapter->en_mng_pt) {
		pci_enable_wake(pdev, PCI_D3hot, 1);
		pci_enable_wake(pdev, PCI_D3cold, 1);
	} else {
		igb_shutdown_fiber_serdes_link_82575(hw);
		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_enable_wake(pdev, PCI_D3cold, 0);
	}

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant. */
	igb_release_hw_control(adapter);

	pci_disable_device(pdev);

	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

#ifdef CONFIG_PM
static int igb_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if (adapter->need_ioport)
		err = pci_enable_device(pdev);
	else
		err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev,
			"igb: Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	igb_set_interrupt_capability(adapter);

	if (igb_alloc_queues(adapter)) {
		dev_err(&pdev->dev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	/* e1000_power_up_phy(adapter); */

	igb_reset(adapter);
	wr32(E1000_WUS, ~0);

	if (netif_running(netdev)) {
		err = igb_open(netdev);
		if (err)
			return err;
	}

	netif_device_attach(netdev);

	/* let the f/w know that the h/w is now under the control of the
	 * driver. */
	igb_get_hw_control(adapter);

	return 0;
}
#endif

static void igb_shutdown(struct pci_dev *pdev)
{
	igb_suspend(pdev, PMSG_SUSPEND);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void igb_netpoll(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	int i;
	int work_done = 0;

	igb_irq_disable(adapter);
	adapter->flags |= IGB_FLAG_IN_NETPOLL;

	for (i = 0; i < adapter->num_tx_queues; i++)
		igb_clean_tx_irq(&adapter->tx_ring[i]);

	for (i = 0; i < adapter->num_rx_queues; i++)
		igb_clean_rx_irq_adv(&adapter->rx_ring[i],
				     &work_done,
				     adapter->rx_ring[i].napi.weight);

	adapter->flags &= ~IGB_FLAG_IN_NETPOLL;
	igb_irq_enable(adapter);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

/**
 * igb_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t igb_io_error_detected(struct pci_dev *pdev,
					      pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (netif_running(netdev))
		igb_down(adapter);
	pci_disable_device(pdev);

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * igb_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * resembles the first-half of the igb_resume routine.
 */
static pci_ers_result_t igb_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int err;

	if (adapter->need_ioport)
		err = pci_enable_device(pdev);
	else
		err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);
	pci_restore_state(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	igb_reset(adapter);
	wr32(E1000_WUS, ~0);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * igb_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the igb_resume routine.
 */
static void igb_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev)) {
		if (igb_up(adapter)) {
			dev_err(&pdev->dev, "igb_up failed after reset\n");
			return;
		}
	}

	netif_device_attach(netdev);

	/* let the f/w know that the h/w is now under the control of the
	 * driver. */
	igb_get_hw_control(adapter);

}

/* igb_main.c */
