/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

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
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "e1000.h"

char e1000_driver_name[] = "e1000";
static char e1000_driver_string[] = "Intel(R) PRO/1000 Network Driver";
#ifndef CONFIG_E1000_NAPI
#define DRIVERNAPI
#else
#define DRIVERNAPI "-NAPI"
#endif
#define DRV_VERSION "7.2.9-k4"DRIVERNAPI
char e1000_driver_version[] = DRV_VERSION;
static char e1000_copyright[] = "Copyright (c) 1999-2006 Intel Corporation.";

/* e1000_pci_tbl - PCI Device ID Table
 *
 * Last entry must be all 0s
 *
 * Macro expands to...
 *   {PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}
 */
static struct pci_device_id e1000_pci_tbl[] = {
	INTEL_E1000_ETHERNET_DEVICE(0x1000),
	INTEL_E1000_ETHERNET_DEVICE(0x1001),
	INTEL_E1000_ETHERNET_DEVICE(0x1004),
	INTEL_E1000_ETHERNET_DEVICE(0x1008),
	INTEL_E1000_ETHERNET_DEVICE(0x1009),
	INTEL_E1000_ETHERNET_DEVICE(0x100C),
	INTEL_E1000_ETHERNET_DEVICE(0x100D),
	INTEL_E1000_ETHERNET_DEVICE(0x100E),
	INTEL_E1000_ETHERNET_DEVICE(0x100F),
	INTEL_E1000_ETHERNET_DEVICE(0x1010),
	INTEL_E1000_ETHERNET_DEVICE(0x1011),
	INTEL_E1000_ETHERNET_DEVICE(0x1012),
	INTEL_E1000_ETHERNET_DEVICE(0x1013),
	INTEL_E1000_ETHERNET_DEVICE(0x1014),
	INTEL_E1000_ETHERNET_DEVICE(0x1015),
	INTEL_E1000_ETHERNET_DEVICE(0x1016),
	INTEL_E1000_ETHERNET_DEVICE(0x1017),
	INTEL_E1000_ETHERNET_DEVICE(0x1018),
	INTEL_E1000_ETHERNET_DEVICE(0x1019),
	INTEL_E1000_ETHERNET_DEVICE(0x101A),
	INTEL_E1000_ETHERNET_DEVICE(0x101D),
	INTEL_E1000_ETHERNET_DEVICE(0x101E),
	INTEL_E1000_ETHERNET_DEVICE(0x1026),
	INTEL_E1000_ETHERNET_DEVICE(0x1027),
	INTEL_E1000_ETHERNET_DEVICE(0x1028),
	INTEL_E1000_ETHERNET_DEVICE(0x1049),
	INTEL_E1000_ETHERNET_DEVICE(0x104A),
	INTEL_E1000_ETHERNET_DEVICE(0x104B),
	INTEL_E1000_ETHERNET_DEVICE(0x104C),
	INTEL_E1000_ETHERNET_DEVICE(0x104D),
	INTEL_E1000_ETHERNET_DEVICE(0x105E),
	INTEL_E1000_ETHERNET_DEVICE(0x105F),
	INTEL_E1000_ETHERNET_DEVICE(0x1060),
	INTEL_E1000_ETHERNET_DEVICE(0x1075),
	INTEL_E1000_ETHERNET_DEVICE(0x1076),
	INTEL_E1000_ETHERNET_DEVICE(0x1077),
	INTEL_E1000_ETHERNET_DEVICE(0x1078),
	INTEL_E1000_ETHERNET_DEVICE(0x1079),
	INTEL_E1000_ETHERNET_DEVICE(0x107A),
	INTEL_E1000_ETHERNET_DEVICE(0x107B),
	INTEL_E1000_ETHERNET_DEVICE(0x107C),
	INTEL_E1000_ETHERNET_DEVICE(0x107D),
	INTEL_E1000_ETHERNET_DEVICE(0x107E),
	INTEL_E1000_ETHERNET_DEVICE(0x107F),
	INTEL_E1000_ETHERNET_DEVICE(0x108A),
	INTEL_E1000_ETHERNET_DEVICE(0x108B),
	INTEL_E1000_ETHERNET_DEVICE(0x108C),
	INTEL_E1000_ETHERNET_DEVICE(0x1096),
	INTEL_E1000_ETHERNET_DEVICE(0x1098),
	INTEL_E1000_ETHERNET_DEVICE(0x1099),
	INTEL_E1000_ETHERNET_DEVICE(0x109A),
	INTEL_E1000_ETHERNET_DEVICE(0x10A4),
	INTEL_E1000_ETHERNET_DEVICE(0x10B5),
	INTEL_E1000_ETHERNET_DEVICE(0x10B9),
	INTEL_E1000_ETHERNET_DEVICE(0x10BA),
	INTEL_E1000_ETHERNET_DEVICE(0x10BB),
	/* required last entry */
	{0,}
};

MODULE_DEVICE_TABLE(pci, e1000_pci_tbl);

int e1000_up(struct e1000_adapter *adapter);
void e1000_down(struct e1000_adapter *adapter);
void e1000_reinit_locked(struct e1000_adapter *adapter);
void e1000_reset(struct e1000_adapter *adapter);
int e1000_set_spd_dplx(struct e1000_adapter *adapter, uint16_t spddplx);
int e1000_setup_all_tx_resources(struct e1000_adapter *adapter);
int e1000_setup_all_rx_resources(struct e1000_adapter *adapter);
void e1000_free_all_tx_resources(struct e1000_adapter *adapter);
void e1000_free_all_rx_resources(struct e1000_adapter *adapter);
static int e1000_setup_tx_resources(struct e1000_adapter *adapter,
                             struct e1000_tx_ring *txdr);
static int e1000_setup_rx_resources(struct e1000_adapter *adapter,
                             struct e1000_rx_ring *rxdr);
static void e1000_free_tx_resources(struct e1000_adapter *adapter,
                             struct e1000_tx_ring *tx_ring);
static void e1000_free_rx_resources(struct e1000_adapter *adapter,
                             struct e1000_rx_ring *rx_ring);
void e1000_update_stats(struct e1000_adapter *adapter);

static int e1000_init_module(void);
static void e1000_exit_module(void);
static int e1000_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void __devexit e1000_remove(struct pci_dev *pdev);
static int e1000_alloc_queues(struct e1000_adapter *adapter);
static int e1000_sw_init(struct e1000_adapter *adapter);
static int e1000_open(struct net_device *netdev);
static int e1000_close(struct net_device *netdev);
static void e1000_configure_tx(struct e1000_adapter *adapter);
static void e1000_configure_rx(struct e1000_adapter *adapter);
static void e1000_setup_rctl(struct e1000_adapter *adapter);
static void e1000_clean_all_tx_rings(struct e1000_adapter *adapter);
static void e1000_clean_all_rx_rings(struct e1000_adapter *adapter);
static void e1000_clean_tx_ring(struct e1000_adapter *adapter,
                                struct e1000_tx_ring *tx_ring);
static void e1000_clean_rx_ring(struct e1000_adapter *adapter,
                                struct e1000_rx_ring *rx_ring);
static void e1000_set_multi(struct net_device *netdev);
static void e1000_update_phy_info(unsigned long data);
static void e1000_watchdog(unsigned long data);
static void e1000_82547_tx_fifo_stall(unsigned long data);
static int e1000_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
static struct net_device_stats * e1000_get_stats(struct net_device *netdev);
static int e1000_change_mtu(struct net_device *netdev, int new_mtu);
static int e1000_set_mac(struct net_device *netdev, void *p);
static irqreturn_t e1000_intr(int irq, void *data);
static boolean_t e1000_clean_tx_irq(struct e1000_adapter *adapter,
                                    struct e1000_tx_ring *tx_ring);
#ifdef CONFIG_E1000_NAPI
static int e1000_clean(struct net_device *poll_dev, int *budget);
static boolean_t e1000_clean_rx_irq(struct e1000_adapter *adapter,
                                    struct e1000_rx_ring *rx_ring,
                                    int *work_done, int work_to_do);
static boolean_t e1000_clean_rx_irq_ps(struct e1000_adapter *adapter,
                                       struct e1000_rx_ring *rx_ring,
                                       int *work_done, int work_to_do);
#else
static boolean_t e1000_clean_rx_irq(struct e1000_adapter *adapter,
                                    struct e1000_rx_ring *rx_ring);
static boolean_t e1000_clean_rx_irq_ps(struct e1000_adapter *adapter,
                                       struct e1000_rx_ring *rx_ring);
#endif
static void e1000_alloc_rx_buffers(struct e1000_adapter *adapter,
                                   struct e1000_rx_ring *rx_ring,
				   int cleaned_count);
static void e1000_alloc_rx_buffers_ps(struct e1000_adapter *adapter,
                                      struct e1000_rx_ring *rx_ring,
				      int cleaned_count);
static int e1000_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
static int e1000_mii_ioctl(struct net_device *netdev, struct ifreq *ifr,
			   int cmd);
void e1000_set_ethtool_ops(struct net_device *netdev);
static void e1000_enter_82542_rst(struct e1000_adapter *adapter);
static void e1000_leave_82542_rst(struct e1000_adapter *adapter);
static void e1000_tx_timeout(struct net_device *dev);
static void e1000_reset_task(struct net_device *dev);
static void e1000_smartspeed(struct e1000_adapter *adapter);
static int e1000_82547_fifo_workaround(struct e1000_adapter *adapter,
                                       struct sk_buff *skb);

static void e1000_vlan_rx_register(struct net_device *netdev, struct vlan_group *grp);
static void e1000_vlan_rx_add_vid(struct net_device *netdev, uint16_t vid);
static void e1000_vlan_rx_kill_vid(struct net_device *netdev, uint16_t vid);
static void e1000_restore_vlan(struct e1000_adapter *adapter);

static int e1000_suspend(struct pci_dev *pdev, pm_message_t state);
#ifdef CONFIG_PM
static int e1000_resume(struct pci_dev *pdev);
#endif
static void e1000_shutdown(struct pci_dev *pdev);

#ifdef CONFIG_NET_POLL_CONTROLLER
/* for netdump / net console */
static void e1000_netpoll (struct net_device *netdev);
#endif

extern void e1000_check_options(struct e1000_adapter *adapter);

static pci_ers_result_t e1000_io_error_detected(struct pci_dev *pdev,
                     pci_channel_state_t state);
static pci_ers_result_t e1000_io_slot_reset(struct pci_dev *pdev);
static void e1000_io_resume(struct pci_dev *pdev);

static struct pci_error_handlers e1000_err_handler = {
	.error_detected = e1000_io_error_detected,
	.slot_reset = e1000_io_slot_reset,
	.resume = e1000_io_resume,
};

static struct pci_driver e1000_driver = {
	.name     = e1000_driver_name,
	.id_table = e1000_pci_tbl,
	.probe    = e1000_probe,
	.remove   = __devexit_p(e1000_remove),
#ifdef CONFIG_PM
	/* Power Managment Hooks */
	.suspend  = e1000_suspend,
	.resume   = e1000_resume,
#endif
	.shutdown = e1000_shutdown,
	.err_handler = &e1000_err_handler
};

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) PRO/1000 Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static int debug = NETIF_MSG_DRV | NETIF_MSG_PROBE;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

/**
 * e1000_init_module - Driver Registration Routine
 *
 * e1000_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/

static int __init
e1000_init_module(void)
{
	int ret;
	printk(KERN_INFO "%s - version %s\n",
	       e1000_driver_string, e1000_driver_version);

	printk(KERN_INFO "%s\n", e1000_copyright);

	ret = pci_register_driver(&e1000_driver);

	return ret;
}

module_init(e1000_init_module);

/**
 * e1000_exit_module - Driver Exit Cleanup Routine
 *
 * e1000_exit_module is called just before the driver is removed
 * from memory.
 **/

static void __exit
e1000_exit_module(void)
{
	pci_unregister_driver(&e1000_driver);
}

module_exit(e1000_exit_module);

static int e1000_request_irq(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int flags, err = 0;

	flags = IRQF_SHARED;
#ifdef CONFIG_PCI_MSI
	if (adapter->hw.mac_type > e1000_82547_rev_2) {
		adapter->have_msi = TRUE;
		if ((err = pci_enable_msi(adapter->pdev))) {
			DPRINTK(PROBE, ERR,
			 "Unable to allocate MSI interrupt Error: %d\n", err);
			adapter->have_msi = FALSE;
		}
	}
	if (adapter->have_msi)
		flags &= ~IRQF_SHARED;
#endif
	if ((err = request_irq(adapter->pdev->irq, &e1000_intr, flags,
	                       netdev->name, netdev)))
		DPRINTK(PROBE, ERR,
		        "Unable to allocate interrupt Error: %d\n", err);

	return err;
}

static void e1000_free_irq(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	free_irq(adapter->pdev->irq, netdev);

#ifdef CONFIG_PCI_MSI
	if (adapter->have_msi)
		pci_disable_msi(adapter->pdev);
#endif
}

/**
 * e1000_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/

static void
e1000_irq_disable(struct e1000_adapter *adapter)
{
	atomic_inc(&adapter->irq_sem);
	E1000_WRITE_REG(&adapter->hw, IMC, ~0);
	E1000_WRITE_FLUSH(&adapter->hw);
	synchronize_irq(adapter->pdev->irq);
}

/**
 * e1000_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/

static void
e1000_irq_enable(struct e1000_adapter *adapter)
{
	if (likely(atomic_dec_and_test(&adapter->irq_sem))) {
		E1000_WRITE_REG(&adapter->hw, IMS, IMS_ENABLE_MASK);
		E1000_WRITE_FLUSH(&adapter->hw);
	}
}

static void
e1000_update_mng_vlan(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	uint16_t vid = adapter->hw.mng_cookie.vlan_id;
	uint16_t old_vid = adapter->mng_vlan_id;
	if (adapter->vlgrp) {
		if (!adapter->vlgrp->vlan_devices[vid]) {
			if (adapter->hw.mng_cookie.status &
				E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT) {
				e1000_vlan_rx_add_vid(netdev, vid);
				adapter->mng_vlan_id = vid;
			} else
				adapter->mng_vlan_id = E1000_MNG_VLAN_NONE;

			if ((old_vid != (uint16_t)E1000_MNG_VLAN_NONE) &&
					(vid != old_vid) &&
					!adapter->vlgrp->vlan_devices[old_vid])
				e1000_vlan_rx_kill_vid(netdev, old_vid);
		} else
			adapter->mng_vlan_id = vid;
	}
}

/**
 * e1000_release_hw_control - release control of the h/w to f/w
 * @adapter: address of board private structure
 *
 * e1000_release_hw_control resets {CTRL_EXT|FWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that the
 * driver is no longer loaded. For AMT version (only with 82573) i
 * of the f/w this means that the network i/f is closed.
 *
 **/

static void
e1000_release_hw_control(struct e1000_adapter *adapter)
{
	uint32_t ctrl_ext;
	uint32_t swsm;
	uint32_t extcnf;

	/* Let firmware taken over control of h/w */
	switch (adapter->hw.mac_type) {
	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
		ctrl_ext = E1000_READ_REG(&adapter->hw, CTRL_EXT);
		E1000_WRITE_REG(&adapter->hw, CTRL_EXT,
				ctrl_ext & ~E1000_CTRL_EXT_DRV_LOAD);
		break;
	case e1000_82573:
		swsm = E1000_READ_REG(&adapter->hw, SWSM);
		E1000_WRITE_REG(&adapter->hw, SWSM,
				swsm & ~E1000_SWSM_DRV_LOAD);
	case e1000_ich8lan:
		extcnf = E1000_READ_REG(&adapter->hw, CTRL_EXT);
		E1000_WRITE_REG(&adapter->hw, CTRL_EXT,
				extcnf & ~E1000_CTRL_EXT_DRV_LOAD);
		break;
	default:
		break;
	}
}

/**
 * e1000_get_hw_control - get control of the h/w from f/w
 * @adapter: address of board private structure
 *
 * e1000_get_hw_control sets {CTRL_EXT|FWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that
 * the driver is loaded. For AMT version (only with 82573)
 * of the f/w this means that the network i/f is open.
 *
 **/

static void
e1000_get_hw_control(struct e1000_adapter *adapter)
{
	uint32_t ctrl_ext;
	uint32_t swsm;
	uint32_t extcnf;

	/* Let firmware know the driver has taken over */
	switch (adapter->hw.mac_type) {
	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
		ctrl_ext = E1000_READ_REG(&adapter->hw, CTRL_EXT);
		E1000_WRITE_REG(&adapter->hw, CTRL_EXT,
				ctrl_ext | E1000_CTRL_EXT_DRV_LOAD);
		break;
	case e1000_82573:
		swsm = E1000_READ_REG(&adapter->hw, SWSM);
		E1000_WRITE_REG(&adapter->hw, SWSM,
				swsm | E1000_SWSM_DRV_LOAD);
		break;
	case e1000_ich8lan:
		extcnf = E1000_READ_REG(&adapter->hw, EXTCNF_CTRL);
		E1000_WRITE_REG(&adapter->hw, EXTCNF_CTRL,
				extcnf | E1000_EXTCNF_CTRL_SWFLAG);
		break;
	default:
		break;
	}
}

int
e1000_up(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;

	/* hardware has been reset, we need to reload some things */

	e1000_set_multi(netdev);

	e1000_restore_vlan(adapter);

	e1000_configure_tx(adapter);
	e1000_setup_rctl(adapter);
	e1000_configure_rx(adapter);
	/* call E1000_DESC_UNUSED which always leaves
	 * at least 1 descriptor unused to make sure
	 * next_to_use != next_to_clean */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct e1000_rx_ring *ring = &adapter->rx_ring[i];
		adapter->alloc_rx_buf(adapter, ring,
		                      E1000_DESC_UNUSED(ring));
	}

	adapter->tx_queue_len = netdev->tx_queue_len;

#ifdef CONFIG_E1000_NAPI
	netif_poll_enable(netdev);
#endif
	e1000_irq_enable(adapter);

	clear_bit(__E1000_DOWN, &adapter->flags);

	mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);
	return 0;
}

/**
 * e1000_power_up_phy - restore link in case the phy was powered down
 * @adapter: address of board private structure
 *
 * The phy may be powered down to save power and turn off link when the
 * driver is unloaded and wake on lan is not enabled (among others)
 * *** this routine MUST be followed by a call to e1000_reset ***
 *
 **/

void e1000_power_up_phy(struct e1000_adapter *adapter)
{
	uint16_t mii_reg = 0;

	/* Just clear the power down bit to wake the phy back up */
	if (adapter->hw.media_type == e1000_media_type_copper) {
		/* according to the manual, the phy will retain its
		 * settings across a power-down/up cycle */
		e1000_read_phy_reg(&adapter->hw, PHY_CTRL, &mii_reg);
		mii_reg &= ~MII_CR_POWER_DOWN;
		e1000_write_phy_reg(&adapter->hw, PHY_CTRL, mii_reg);
	}
}

static void e1000_power_down_phy(struct e1000_adapter *adapter)
{
	/* Power down the PHY so no link is implied when interface is down *
	 * The PHY cannot be powered down if any of the following is TRUE *
	 * (a) WoL is enabled
	 * (b) AMT is active
	 * (c) SoL/IDER session is active */
	if (!adapter->wol && adapter->hw.mac_type >= e1000_82540 &&
	   adapter->hw.media_type == e1000_media_type_copper) {
		uint16_t mii_reg = 0;

		switch (adapter->hw.mac_type) {
		case e1000_82540:
		case e1000_82545:
		case e1000_82545_rev_3:
		case e1000_82546:
		case e1000_82546_rev_3:
		case e1000_82541:
		case e1000_82541_rev_2:
		case e1000_82547:
		case e1000_82547_rev_2:
			if (E1000_READ_REG(&adapter->hw, MANC) &
			    E1000_MANC_SMBUS_EN)
				goto out;
			break;
		case e1000_82571:
		case e1000_82572:
		case e1000_82573:
		case e1000_80003es2lan:
		case e1000_ich8lan:
			if (e1000_check_mng_mode(&adapter->hw) ||
			    e1000_check_phy_reset_block(&adapter->hw))
				goto out;
			break;
		default:
			goto out;
		}
		e1000_read_phy_reg(&adapter->hw, PHY_CTRL, &mii_reg);
		mii_reg |= MII_CR_POWER_DOWN;
		e1000_write_phy_reg(&adapter->hw, PHY_CTRL, mii_reg);
		mdelay(1);
	}
out:
	return;
}

void
e1000_down(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer */
	set_bit(__E1000_DOWN, &adapter->flags);

	e1000_irq_disable(adapter);

	del_timer_sync(&adapter->tx_fifo_stall_timer);
	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

#ifdef CONFIG_E1000_NAPI
	netif_poll_disable(netdev);
#endif
	netdev->tx_queue_len = adapter->tx_queue_len;
	adapter->link_speed = 0;
	adapter->link_duplex = 0;
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	e1000_reset(adapter);
	e1000_clean_all_tx_rings(adapter);
	e1000_clean_all_rx_rings(adapter);
}

void
e1000_reinit_locked(struct e1000_adapter *adapter)
{
	WARN_ON(in_interrupt());
	while (test_and_set_bit(__E1000_RESETTING, &adapter->flags))
		msleep(1);
	e1000_down(adapter);
	e1000_up(adapter);
	clear_bit(__E1000_RESETTING, &adapter->flags);
}

void
e1000_reset(struct e1000_adapter *adapter)
{
	uint32_t pba, manc;
	uint16_t fc_high_water_mark = E1000_FC_HIGH_DIFF;

	/* Repartition Pba for greater than 9k mtu
	 * To take effect CTRL.RST is required.
	 */

	switch (adapter->hw.mac_type) {
	case e1000_82547:
	case e1000_82547_rev_2:
		pba = E1000_PBA_30K;
		break;
	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
		pba = E1000_PBA_38K;
		break;
	case e1000_82573:
		pba = E1000_PBA_12K;
		break;
	case e1000_ich8lan:
		pba = E1000_PBA_8K;
		break;
	default:
		pba = E1000_PBA_48K;
		break;
	}

	if ((adapter->hw.mac_type != e1000_82573) &&
	   (adapter->netdev->mtu > E1000_RXBUFFER_8192))
		pba -= 8; /* allocate more FIFO for Tx */


	if (adapter->hw.mac_type == e1000_82547) {
		adapter->tx_fifo_head = 0;
		adapter->tx_head_addr = pba << E1000_TX_HEAD_ADDR_SHIFT;
		adapter->tx_fifo_size =
			(E1000_PBA_40K - pba) << E1000_PBA_BYTES_SHIFT;
		atomic_set(&adapter->tx_fifo_stall, 0);
	}

	E1000_WRITE_REG(&adapter->hw, PBA, pba);

	/* flow control settings */
	/* Set the FC high water mark to 90% of the FIFO size.
	 * Required to clear last 3 LSB */
	fc_high_water_mark = ((pba * 9216)/10) & 0xFFF8;
	/* We can't use 90% on small FIFOs because the remainder
	 * would be less than 1 full frame.  In this case, we size
	 * it to allow at least a full frame above the high water
	 *  mark. */
	if (pba < E1000_PBA_16K)
		fc_high_water_mark = (pba * 1024) - 1600;

	adapter->hw.fc_high_water = fc_high_water_mark;
	adapter->hw.fc_low_water = fc_high_water_mark - 8;
	if (adapter->hw.mac_type == e1000_80003es2lan)
		adapter->hw.fc_pause_time = 0xFFFF;
	else
		adapter->hw.fc_pause_time = E1000_FC_PAUSE_TIME;
	adapter->hw.fc_send_xon = 1;
	adapter->hw.fc = adapter->hw.original_fc;

	/* Allow time for pending master requests to run */
	e1000_reset_hw(&adapter->hw);
	if (adapter->hw.mac_type >= e1000_82544)
		E1000_WRITE_REG(&adapter->hw, WUC, 0);

	if (e1000_init_hw(&adapter->hw))
		DPRINTK(PROBE, ERR, "Hardware Error\n");
	e1000_update_mng_vlan(adapter);
	/* Enable h/w to recognize an 802.1Q VLAN Ethernet packet */
	E1000_WRITE_REG(&adapter->hw, VET, ETHERNET_IEEE_VLAN_TYPE);

	e1000_reset_adaptive(&adapter->hw);
	e1000_phy_get_info(&adapter->hw, &adapter->phy_info);

	if (!adapter->smart_power_down &&
	    (adapter->hw.mac_type == e1000_82571 ||
	     adapter->hw.mac_type == e1000_82572)) {
		uint16_t phy_data = 0;
		/* speed up time to link by disabling smart power down, ignore
		 * the return value of this function because there is nothing
		 * different we would do if it failed */
		e1000_read_phy_reg(&adapter->hw, IGP02E1000_PHY_POWER_MGMT,
		                   &phy_data);
		phy_data &= ~IGP02E1000_PM_SPD;
		e1000_write_phy_reg(&adapter->hw, IGP02E1000_PHY_POWER_MGMT,
		                    phy_data);
	}

	if ((adapter->en_mng_pt) &&
	    (adapter->hw.mac_type >= e1000_82540) &&
	    (adapter->hw.mac_type < e1000_82571) &&
	    (adapter->hw.media_type == e1000_media_type_copper)) {
		manc = E1000_READ_REG(&adapter->hw, MANC);
		manc |= (E1000_MANC_ARP_EN | E1000_MANC_EN_MNG2HOST);
		E1000_WRITE_REG(&adapter->hw, MANC, manc);
	}
}

/**
 * e1000_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in e1000_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * e1000_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/

static int __devinit
e1000_probe(struct pci_dev *pdev,
            const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct e1000_adapter *adapter;
	unsigned long mmio_start, mmio_len;
	unsigned long flash_start, flash_len;

	static int cards_found = 0;
	static int global_quad_port_a = 0; /* global ksp3 port a indication */
	int i, err, pci_using_dac;
	uint16_t eeprom_data = 0;
	uint16_t eeprom_apme_mask = E1000_EEPROM_APME;
	if ((err = pci_enable_device(pdev)))
		return err;

	if (!(err = pci_set_dma_mask(pdev, DMA_64BIT_MASK)) &&
	    !(err = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK))) {
		pci_using_dac = 1;
	} else {
		if ((err = pci_set_dma_mask(pdev, DMA_32BIT_MASK)) &&
		    (err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK))) {
			E1000_ERR("No usable DMA configuration, aborting\n");
			goto err_dma;
		}
		pci_using_dac = 0;
	}

	if ((err = pci_request_regions(pdev, e1000_driver_name)))
		goto err_pci_reg;

	pci_set_master(pdev);

	err = -ENOMEM;
	netdev = alloc_etherdev(sizeof(struct e1000_adapter));
	if (!netdev)
		goto err_alloc_etherdev;

	SET_MODULE_OWNER(netdev);
	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->hw.back = adapter;
	adapter->msg_enable = (1 << debug) - 1;

	mmio_start = pci_resource_start(pdev, BAR_0);
	mmio_len = pci_resource_len(pdev, BAR_0);

	err = -EIO;
	adapter->hw.hw_addr = ioremap(mmio_start, mmio_len);
	if (!adapter->hw.hw_addr)
		goto err_ioremap;

	for (i = BAR_1; i <= BAR_5; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
			adapter->hw.io_base = pci_resource_start(pdev, i);
			break;
		}
	}

	netdev->open = &e1000_open;
	netdev->stop = &e1000_close;
	netdev->hard_start_xmit = &e1000_xmit_frame;
	netdev->get_stats = &e1000_get_stats;
	netdev->set_multicast_list = &e1000_set_multi;
	netdev->set_mac_address = &e1000_set_mac;
	netdev->change_mtu = &e1000_change_mtu;
	netdev->do_ioctl = &e1000_ioctl;
	e1000_set_ethtool_ops(netdev);
	netdev->tx_timeout = &e1000_tx_timeout;
	netdev->watchdog_timeo = 5 * HZ;
#ifdef CONFIG_E1000_NAPI
	netdev->poll = &e1000_clean;
	netdev->weight = 64;
#endif
	netdev->vlan_rx_register = e1000_vlan_rx_register;
	netdev->vlan_rx_add_vid = e1000_vlan_rx_add_vid;
	netdev->vlan_rx_kill_vid = e1000_vlan_rx_kill_vid;
#ifdef CONFIG_NET_POLL_CONTROLLER
	netdev->poll_controller = e1000_netpoll;
#endif
	strncpy(netdev->name, pci_name(pdev), sizeof(netdev->name) - 1);

	netdev->mem_start = mmio_start;
	netdev->mem_end = mmio_start + mmio_len;
	netdev->base_addr = adapter->hw.io_base;

	adapter->bd_number = cards_found;

	/* setup the private structure */

	if ((err = e1000_sw_init(adapter)))
		goto err_sw_init;

	err = -EIO;
	/* Flash BAR mapping must happen after e1000_sw_init
	 * because it depends on mac_type */
	if ((adapter->hw.mac_type == e1000_ich8lan) &&
	   (pci_resource_flags(pdev, 1) & IORESOURCE_MEM)) {
		flash_start = pci_resource_start(pdev, 1);
		flash_len = pci_resource_len(pdev, 1);
		adapter->hw.flash_address = ioremap(flash_start, flash_len);
		if (!adapter->hw.flash_address)
			goto err_flashmap;
	}

	if (e1000_check_phy_reset_block(&adapter->hw))
		DPRINTK(PROBE, INFO, "PHY reset is blocked due to SOL/IDER session.\n");

	if (adapter->hw.mac_type >= e1000_82543) {
		netdev->features = NETIF_F_SG |
				   NETIF_F_HW_CSUM |
				   NETIF_F_HW_VLAN_TX |
				   NETIF_F_HW_VLAN_RX |
				   NETIF_F_HW_VLAN_FILTER;
		if (adapter->hw.mac_type == e1000_ich8lan)
			netdev->features &= ~NETIF_F_HW_VLAN_FILTER;
	}

#ifdef NETIF_F_TSO
	if ((adapter->hw.mac_type >= e1000_82544) &&
	   (adapter->hw.mac_type != e1000_82547))
		netdev->features |= NETIF_F_TSO;

#ifdef NETIF_F_TSO6
	if (adapter->hw.mac_type > e1000_82547_rev_2)
		netdev->features |= NETIF_F_TSO6;
#endif
#endif
	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	netdev->features |= NETIF_F_LLTX;

	adapter->en_mng_pt = e1000_enable_mng_pass_thru(&adapter->hw);

	/* initialize eeprom parameters */

	if (e1000_init_eeprom_params(&adapter->hw)) {
		E1000_ERR("EEPROM initialization failed\n");
		goto err_eeprom;
	}

	/* before reading the EEPROM, reset the controller to
	 * put the device in a known good starting state */

	e1000_reset_hw(&adapter->hw);

	/* make sure the EEPROM is good */

	if (e1000_validate_eeprom_checksum(&adapter->hw) < 0) {
		DPRINTK(PROBE, ERR, "The EEPROM Checksum Is Not Valid\n");
		goto err_eeprom;
	}

	/* copy the MAC address out of the EEPROM */

	if (e1000_read_mac_addr(&adapter->hw))
		DPRINTK(PROBE, ERR, "EEPROM Read Error\n");
	memcpy(netdev->dev_addr, adapter->hw.mac_addr, netdev->addr_len);
	memcpy(netdev->perm_addr, adapter->hw.mac_addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->perm_addr)) {
		DPRINTK(PROBE, ERR, "Invalid MAC Address\n");
		goto err_eeprom;
	}

	e1000_get_bus_info(&adapter->hw);

	init_timer(&adapter->tx_fifo_stall_timer);
	adapter->tx_fifo_stall_timer.function = &e1000_82547_tx_fifo_stall;
	adapter->tx_fifo_stall_timer.data = (unsigned long) adapter;

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &e1000_watchdog;
	adapter->watchdog_timer.data = (unsigned long) adapter;

	init_timer(&adapter->phy_info_timer);
	adapter->phy_info_timer.function = &e1000_update_phy_info;
	adapter->phy_info_timer.data = (unsigned long) adapter;

	INIT_WORK(&adapter->reset_task,
		(void (*)(void *))e1000_reset_task, netdev);

	e1000_check_options(adapter);

	/* Initial Wake on LAN setting
	 * If APM wake is enabled in the EEPROM,
	 * enable the ACPI Magic Packet filter
	 */

	switch (adapter->hw.mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
		break;
	case e1000_82544:
		e1000_read_eeprom(&adapter->hw,
			EEPROM_INIT_CONTROL2_REG, 1, &eeprom_data);
		eeprom_apme_mask = E1000_EEPROM_82544_APM;
		break;
	case e1000_ich8lan:
		e1000_read_eeprom(&adapter->hw,
			EEPROM_INIT_CONTROL1_REG, 1, &eeprom_data);
		eeprom_apme_mask = E1000_EEPROM_ICH8_APME;
		break;
	case e1000_82546:
	case e1000_82546_rev_3:
	case e1000_82571:
	case e1000_80003es2lan:
		if (E1000_READ_REG(&adapter->hw, STATUS) & E1000_STATUS_FUNC_1){
			e1000_read_eeprom(&adapter->hw,
				EEPROM_INIT_CONTROL3_PORT_B, 1, &eeprom_data);
			break;
		}
		/* Fall Through */
	default:
		e1000_read_eeprom(&adapter->hw,
			EEPROM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);
		break;
	}
	if (eeprom_data & eeprom_apme_mask)
		adapter->eeprom_wol |= E1000_WUFC_MAG;

	/* now that we have the eeprom settings, apply the special cases
	 * where the eeprom may be wrong or the board simply won't support
	 * wake on lan on a particular port */
	switch (pdev->device) {
	case E1000_DEV_ID_82546GB_PCIE:
		adapter->eeprom_wol = 0;
		break;
	case E1000_DEV_ID_82546EB_FIBER:
	case E1000_DEV_ID_82546GB_FIBER:
	case E1000_DEV_ID_82571EB_FIBER:
		/* Wake events only supported on port A for dual fiber
		 * regardless of eeprom setting */
		if (E1000_READ_REG(&adapter->hw, STATUS) & E1000_STATUS_FUNC_1)
			adapter->eeprom_wol = 0;
		break;
	case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
	case E1000_DEV_ID_82571EB_QUAD_COPPER:
		/* if quad port adapter, disable WoL on all but port A */
		if (global_quad_port_a != 0)
			adapter->eeprom_wol = 0;
		else
			adapter->quad_port_a = 1;
		/* Reset for multiple quad port adapters */
		if (++global_quad_port_a == 4)
			global_quad_port_a = 0;
		break;
	}

	/* initialize the wol settings based on the eeprom settings */
	adapter->wol = adapter->eeprom_wol;

	/* print bus type/speed/width info */
	{
	struct e1000_hw *hw = &adapter->hw;
	DPRINTK(PROBE, INFO, "(PCI%s:%s:%s) ",
		((hw->bus_type == e1000_bus_type_pcix) ? "-X" :
		 (hw->bus_type == e1000_bus_type_pci_express ? " Express":"")),
		((hw->bus_speed == e1000_bus_speed_2500) ? "2.5Gb/s" :
		 (hw->bus_speed == e1000_bus_speed_133) ? "133MHz" :
		 (hw->bus_speed == e1000_bus_speed_120) ? "120MHz" :
		 (hw->bus_speed == e1000_bus_speed_100) ? "100MHz" :
		 (hw->bus_speed == e1000_bus_speed_66) ? "66MHz" : "33MHz"),
		((hw->bus_width == e1000_bus_width_64) ? "64-bit" :
		 (hw->bus_width == e1000_bus_width_pciex_4) ? "Width x4" :
		 (hw->bus_width == e1000_bus_width_pciex_1) ? "Width x1" :
		 "32-bit"));
	}

	for (i = 0; i < 6; i++)
		printk("%2.2x%c", netdev->dev_addr[i], i == 5 ? '\n' : ':');

	/* reset the hardware with the new settings */
	e1000_reset(adapter);

	/* If the controller is 82573 and f/w is AMT, do not set
	 * DRV_LOAD until the interface is up.  For all other cases,
	 * let the f/w know that the h/w is now under the control
	 * of the driver. */
	if (adapter->hw.mac_type != e1000_82573 ||
	    !e1000_check_mng_mode(&adapter->hw))
		e1000_get_hw_control(adapter);

	strcpy(netdev->name, "eth%d");
	if ((err = register_netdev(netdev)))
		goto err_register;

	/* tell the stack to leave us alone until e1000_open() is called */
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	DPRINTK(PROBE, INFO, "Intel(R) PRO/1000 Network Connection\n");

	cards_found++;
	return 0;

err_register:
	e1000_release_hw_control(adapter);
err_eeprom:
	if (!e1000_check_phy_reset_block(&adapter->hw))
		e1000_phy_hw_reset(&adapter->hw);

	if (adapter->hw.flash_address)
		iounmap(adapter->hw.flash_address);
err_flashmap:
#ifdef CONFIG_E1000_NAPI
	for (i = 0; i < adapter->num_rx_queues; i++)
		dev_put(&adapter->polling_netdev[i]);
#endif

	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);
#ifdef CONFIG_E1000_NAPI
	kfree(adapter->polling_netdev);
#endif
err_sw_init:
	iounmap(adapter->hw.hw_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_regions(pdev);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * e1000_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * e1000_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/

static void __devexit
e1000_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	uint32_t manc;
#ifdef CONFIG_E1000_NAPI
	int i;
#endif

	flush_scheduled_work();

	if (adapter->hw.mac_type >= e1000_82540 &&
	    adapter->hw.mac_type < e1000_82571 &&
	    adapter->hw.media_type == e1000_media_type_copper) {
		manc = E1000_READ_REG(&adapter->hw, MANC);
		if (manc & E1000_MANC_SMBUS_EN) {
			manc |= E1000_MANC_ARP_EN;
			E1000_WRITE_REG(&adapter->hw, MANC, manc);
		}
	}

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant. */
	e1000_release_hw_control(adapter);

	unregister_netdev(netdev);
#ifdef CONFIG_E1000_NAPI
	for (i = 0; i < adapter->num_rx_queues; i++)
		dev_put(&adapter->polling_netdev[i]);
#endif

	if (!e1000_check_phy_reset_block(&adapter->hw))
		e1000_phy_hw_reset(&adapter->hw);

	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);
#ifdef CONFIG_E1000_NAPI
	kfree(adapter->polling_netdev);
#endif

	iounmap(adapter->hw.hw_addr);
	if (adapter->hw.flash_address)
		iounmap(adapter->hw.flash_address);
	pci_release_regions(pdev);

	free_netdev(netdev);

	pci_disable_device(pdev);
}

/**
 * e1000_sw_init - Initialize general software structures (struct e1000_adapter)
 * @adapter: board private structure to initialize
 *
 * e1000_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/

static int __devinit
e1000_sw_init(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
#ifdef CONFIG_E1000_NAPI
	int i;
#endif

	/* PCI config space info */

	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_id = pdev->subsystem_device;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);

	pci_read_config_word(pdev, PCI_COMMAND, &hw->pci_cmd_word);

	adapter->rx_buffer_len = MAXIMUM_ETHERNET_VLAN_SIZE;
	adapter->rx_ps_bsize0 = E1000_RXBUFFER_128;
	hw->max_frame_size = netdev->mtu +
			     ENET_HEADER_SIZE + ETHERNET_FCS_SIZE;
	hw->min_frame_size = MINIMUM_ETHERNET_FRAME_SIZE;

	/* identify the MAC */

	if (e1000_set_mac_type(hw)) {
		DPRINTK(PROBE, ERR, "Unknown MAC Type\n");
		return -EIO;
	}

	switch (hw->mac_type) {
	default:
		break;
	case e1000_82541:
	case e1000_82547:
	case e1000_82541_rev_2:
	case e1000_82547_rev_2:
		hw->phy_init_script = 1;
		break;
	}

	e1000_set_media_type(hw);

	hw->wait_autoneg_complete = FALSE;
	hw->tbi_compatibility_en = TRUE;
	hw->adaptive_ifs = TRUE;

	/* Copper options */

	if (hw->media_type == e1000_media_type_copper) {
		hw->mdix = AUTO_ALL_MODES;
		hw->disable_polarity_correction = FALSE;
		hw->master_slave = E1000_MASTER_SLAVE;
	}

	adapter->num_tx_queues = 1;
	adapter->num_rx_queues = 1;

	if (e1000_alloc_queues(adapter)) {
		DPRINTK(PROBE, ERR, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

#ifdef CONFIG_E1000_NAPI
	for (i = 0; i < adapter->num_rx_queues; i++) {
		adapter->polling_netdev[i].priv = adapter;
		adapter->polling_netdev[i].poll = &e1000_clean;
		adapter->polling_netdev[i].weight = 64;
		dev_hold(&adapter->polling_netdev[i]);
		set_bit(__LINK_STATE_START, &adapter->polling_netdev[i].state);
	}
	spin_lock_init(&adapter->tx_queue_lock);
#endif

	atomic_set(&adapter->irq_sem, 1);
	spin_lock_init(&adapter->stats_lock);

	set_bit(__E1000_DOWN, &adapter->flags);

	return 0;
}

/**
 * e1000_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 * We allocate one ring per queue at run-time since we don't know the
 * number of queues at compile-time.  The polling_netdev array is
 * intended for Multiqueue, but should work fine with a single queue.
 **/

static int __devinit
e1000_alloc_queues(struct e1000_adapter *adapter)
{
	int size;

	size = sizeof(struct e1000_tx_ring) * adapter->num_tx_queues;
	adapter->tx_ring = kmalloc(size, GFP_KERNEL);
	if (!adapter->tx_ring)
		return -ENOMEM;
	memset(adapter->tx_ring, 0, size);

	size = sizeof(struct e1000_rx_ring) * adapter->num_rx_queues;
	adapter->rx_ring = kmalloc(size, GFP_KERNEL);
	if (!adapter->rx_ring) {
		kfree(adapter->tx_ring);
		return -ENOMEM;
	}
	memset(adapter->rx_ring, 0, size);

#ifdef CONFIG_E1000_NAPI
	size = sizeof(struct net_device) * adapter->num_rx_queues;
	adapter->polling_netdev = kmalloc(size, GFP_KERNEL);
	if (!adapter->polling_netdev) {
		kfree(adapter->tx_ring);
		kfree(adapter->rx_ring);
		return -ENOMEM;
	}
	memset(adapter->polling_netdev, 0, size);
#endif

	return E1000_SUCCESS;
}

/**
 * e1000_open - Called when a network interface is made active
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

static int
e1000_open(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	int err;

	/* disallow open during test */
	if (test_bit(__E1000_TESTING, &adapter->flags))
		return -EBUSY;

	/* allocate transmit descriptors */
	if ((err = e1000_setup_all_tx_resources(adapter)))
		goto err_setup_tx;

	/* allocate receive descriptors */
	if ((err = e1000_setup_all_rx_resources(adapter)))
		goto err_setup_rx;

	err = e1000_request_irq(adapter);
	if (err)
		goto err_req_irq;

	e1000_power_up_phy(adapter);

	if ((err = e1000_up(adapter)))
		goto err_up;
	adapter->mng_vlan_id = E1000_MNG_VLAN_NONE;
	if ((adapter->hw.mng_cookie.status &
			  E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT)) {
		e1000_update_mng_vlan(adapter);
	}

	/* If AMT is enabled, let the firmware know that the network
	 * interface is now open */
	if (adapter->hw.mac_type == e1000_82573 &&
	    e1000_check_mng_mode(&adapter->hw))
		e1000_get_hw_control(adapter);

	return E1000_SUCCESS;

err_up:
	e1000_power_down_phy(adapter);
	e1000_free_irq(adapter);
err_req_irq:
	e1000_free_all_rx_resources(adapter);
err_setup_rx:
	e1000_free_all_tx_resources(adapter);
err_setup_tx:
	e1000_reset(adapter);

	return err;
}

/**
 * e1000_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/

static int
e1000_close(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	WARN_ON(test_bit(__E1000_RESETTING, &adapter->flags));
	e1000_down(adapter);
	e1000_power_down_phy(adapter);
	e1000_free_irq(adapter);

	e1000_free_all_tx_resources(adapter);
	e1000_free_all_rx_resources(adapter);

	/* kill manageability vlan ID if supported, but not if a vlan with
	 * the same ID is registered on the host OS (let 8021q kill it) */
	if ((adapter->hw.mng_cookie.status &
			  E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT) &&
	     !(adapter->vlgrp &&
			  adapter->vlgrp->vlan_devices[adapter->mng_vlan_id])) {
		e1000_vlan_rx_kill_vid(netdev, adapter->mng_vlan_id);
	}

	/* If AMT is enabled, let the firmware know that the network
	 * interface is now closed */
	if (adapter->hw.mac_type == e1000_82573 &&
	    e1000_check_mng_mode(&adapter->hw))
		e1000_release_hw_control(adapter);

	return 0;
}

/**
 * e1000_check_64k_bound - check that memory doesn't cross 64kB boundary
 * @adapter: address of board private structure
 * @start: address of beginning of memory
 * @len: length of memory
 **/
static boolean_t
e1000_check_64k_bound(struct e1000_adapter *adapter,
		      void *start, unsigned long len)
{
	unsigned long begin = (unsigned long) start;
	unsigned long end = begin + len;

	/* First rev 82545 and 82546 need to not allow any memory
	 * write location to cross 64k boundary due to errata 23 */
	if (adapter->hw.mac_type == e1000_82545 ||
	    adapter->hw.mac_type == e1000_82546) {
		return ((begin ^ (end - 1)) >> 16) != 0 ? FALSE : TRUE;
	}

	return TRUE;
}

/**
 * e1000_setup_tx_resources - allocate Tx resources (Descriptors)
 * @adapter: board private structure
 * @txdr:    tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/

static int
e1000_setup_tx_resources(struct e1000_adapter *adapter,
                         struct e1000_tx_ring *txdr)
{
	struct pci_dev *pdev = adapter->pdev;
	int size;

	size = sizeof(struct e1000_buffer) * txdr->count;
	txdr->buffer_info = vmalloc(size);
	if (!txdr->buffer_info) {
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the transmit descriptor ring\n");
		return -ENOMEM;
	}
	memset(txdr->buffer_info, 0, size);

	/* round up to nearest 4K */

	txdr->size = txdr->count * sizeof(struct e1000_tx_desc);
	E1000_ROUNDUP(txdr->size, 4096);

	txdr->desc = pci_alloc_consistent(pdev, txdr->size, &txdr->dma);
	if (!txdr->desc) {
setup_tx_desc_die:
		vfree(txdr->buffer_info);
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the transmit descriptor ring\n");
		return -ENOMEM;
	}

	/* Fix for errata 23, can't cross 64kB boundary */
	if (!e1000_check_64k_bound(adapter, txdr->desc, txdr->size)) {
		void *olddesc = txdr->desc;
		dma_addr_t olddma = txdr->dma;
		DPRINTK(TX_ERR, ERR, "txdr align check failed: %u bytes "
				     "at %p\n", txdr->size, txdr->desc);
		/* Try again, without freeing the previous */
		txdr->desc = pci_alloc_consistent(pdev, txdr->size, &txdr->dma);
		/* Failed allocation, critical failure */
		if (!txdr->desc) {
			pci_free_consistent(pdev, txdr->size, olddesc, olddma);
			goto setup_tx_desc_die;
		}

		if (!e1000_check_64k_bound(adapter, txdr->desc, txdr->size)) {
			/* give up */
			pci_free_consistent(pdev, txdr->size, txdr->desc,
					    txdr->dma);
			pci_free_consistent(pdev, txdr->size, olddesc, olddma);
			DPRINTK(PROBE, ERR,
				"Unable to allocate aligned memory "
				"for the transmit descriptor ring\n");
			vfree(txdr->buffer_info);
			return -ENOMEM;
		} else {
			/* Free old allocation, new allocation was successful */
			pci_free_consistent(pdev, txdr->size, olddesc, olddma);
		}
	}
	memset(txdr->desc, 0, txdr->size);

	txdr->next_to_use = 0;
	txdr->next_to_clean = 0;
	spin_lock_init(&txdr->tx_lock);

	return 0;
}

/**
 * e1000_setup_all_tx_resources - wrapper to allocate Tx resources
 * 				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/

int
e1000_setup_all_tx_resources(struct e1000_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		err = e1000_setup_tx_resources(adapter, &adapter->tx_ring[i]);
		if (err) {
			DPRINTK(PROBE, ERR,
				"Allocation for Tx Queue %u failed\n", i);
			for (i-- ; i >= 0; i--)
				e1000_free_tx_resources(adapter,
							&adapter->tx_ring[i]);
			break;
		}
	}

	return err;
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/

static void
e1000_configure_tx(struct e1000_adapter *adapter)
{
	uint64_t tdba;
	struct e1000_hw *hw = &adapter->hw;
	uint32_t tdlen, tctl, tipg, tarc;
	uint32_t ipgr1, ipgr2;

	/* Setup the HW Tx Head and Tail descriptor pointers */

	switch (adapter->num_tx_queues) {
	case 1:
	default:
		tdba = adapter->tx_ring[0].dma;
		tdlen = adapter->tx_ring[0].count *
			sizeof(struct e1000_tx_desc);
		E1000_WRITE_REG(hw, TDLEN, tdlen);
		E1000_WRITE_REG(hw, TDBAH, (tdba >> 32));
		E1000_WRITE_REG(hw, TDBAL, (tdba & 0x00000000ffffffffULL));
		E1000_WRITE_REG(hw, TDT, 0);
		E1000_WRITE_REG(hw, TDH, 0);
		adapter->tx_ring[0].tdh = ((hw->mac_type >= e1000_82543) ? E1000_TDH : E1000_82542_TDH);
		adapter->tx_ring[0].tdt = ((hw->mac_type >= e1000_82543) ? E1000_TDT : E1000_82542_TDT);
		break;
	}

	/* Set the default values for the Tx Inter Packet Gap timer */

	if (hw->media_type == e1000_media_type_fiber ||
	    hw->media_type == e1000_media_type_internal_serdes)
		tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
	else
		tipg = DEFAULT_82543_TIPG_IPGT_COPPER;

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
		tipg = DEFAULT_82542_TIPG_IPGT;
		ipgr1 = DEFAULT_82542_TIPG_IPGR1;
		ipgr2 = DEFAULT_82542_TIPG_IPGR2;
		break;
	case e1000_80003es2lan:
		ipgr1 = DEFAULT_82543_TIPG_IPGR1;
		ipgr2 = DEFAULT_80003ES2LAN_TIPG_IPGR2;
		break;
	default:
		ipgr1 = DEFAULT_82543_TIPG_IPGR1;
		ipgr2 = DEFAULT_82543_TIPG_IPGR2;
		break;
	}
	tipg |= ipgr1 << E1000_TIPG_IPGR1_SHIFT;
	tipg |= ipgr2 << E1000_TIPG_IPGR2_SHIFT;
	E1000_WRITE_REG(hw, TIPG, tipg);

	/* Set the Tx Interrupt Delay register */

	E1000_WRITE_REG(hw, TIDV, adapter->tx_int_delay);
	if (hw->mac_type >= e1000_82540)
		E1000_WRITE_REG(hw, TADV, adapter->tx_abs_int_delay);

	/* Program the Transmit Control Register */

	tctl = E1000_READ_REG(hw, TCTL);
	tctl &= ~E1000_TCTL_CT;
	tctl |= E1000_TCTL_PSP | E1000_TCTL_RTLC |
		(E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

	if (hw->mac_type == e1000_82571 || hw->mac_type == e1000_82572) {
		tarc = E1000_READ_REG(hw, TARC0);
		/* set the speed mode bit, we'll clear it if we're not at
		 * gigabit link later */
		tarc |= (1 << 21);
		E1000_WRITE_REG(hw, TARC0, tarc);
	} else if (hw->mac_type == e1000_80003es2lan) {
		tarc = E1000_READ_REG(hw, TARC0);
		tarc |= 1;
		E1000_WRITE_REG(hw, TARC0, tarc);
		tarc = E1000_READ_REG(hw, TARC1);
		tarc |= 1;
		E1000_WRITE_REG(hw, TARC1, tarc);
	}

	e1000_config_collision_dist(hw);

	/* Setup Transmit Descriptor Settings for eop descriptor */
	adapter->txd_cmd = E1000_TXD_CMD_IDE | E1000_TXD_CMD_EOP |
		E1000_TXD_CMD_IFCS;

	if (hw->mac_type < e1000_82543)
		adapter->txd_cmd |= E1000_TXD_CMD_RPS;
	else
		adapter->txd_cmd |= E1000_TXD_CMD_RS;

	/* Cache if we're 82544 running in PCI-X because we'll
	 * need this to apply a workaround later in the send path. */
	if (hw->mac_type == e1000_82544 &&
	    hw->bus_type == e1000_bus_type_pcix)
		adapter->pcix_82544 = 1;

	E1000_WRITE_REG(hw, TCTL, tctl);

}

/**
 * e1000_setup_rx_resources - allocate Rx resources (Descriptors)
 * @adapter: board private structure
 * @rxdr:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/

static int
e1000_setup_rx_resources(struct e1000_adapter *adapter,
                         struct e1000_rx_ring *rxdr)
{
	struct pci_dev *pdev = adapter->pdev;
	int size, desc_len;

	size = sizeof(struct e1000_buffer) * rxdr->count;
	rxdr->buffer_info = vmalloc(size);
	if (!rxdr->buffer_info) {
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the receive descriptor ring\n");
		return -ENOMEM;
	}
	memset(rxdr->buffer_info, 0, size);

	size = sizeof(struct e1000_ps_page) * rxdr->count;
	rxdr->ps_page = kmalloc(size, GFP_KERNEL);
	if (!rxdr->ps_page) {
		vfree(rxdr->buffer_info);
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the receive descriptor ring\n");
		return -ENOMEM;
	}
	memset(rxdr->ps_page, 0, size);

	size = sizeof(struct e1000_ps_page_dma) * rxdr->count;
	rxdr->ps_page_dma = kmalloc(size, GFP_KERNEL);
	if (!rxdr->ps_page_dma) {
		vfree(rxdr->buffer_info);
		kfree(rxdr->ps_page);
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the receive descriptor ring\n");
		return -ENOMEM;
	}
	memset(rxdr->ps_page_dma, 0, size);

	if (adapter->hw.mac_type <= e1000_82547_rev_2)
		desc_len = sizeof(struct e1000_rx_desc);
	else
		desc_len = sizeof(union e1000_rx_desc_packet_split);

	/* Round up to nearest 4K */

	rxdr->size = rxdr->count * desc_len;
	E1000_ROUNDUP(rxdr->size, 4096);

	rxdr->desc = pci_alloc_consistent(pdev, rxdr->size, &rxdr->dma);

	if (!rxdr->desc) {
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the receive descriptor ring\n");
setup_rx_desc_die:
		vfree(rxdr->buffer_info);
		kfree(rxdr->ps_page);
		kfree(rxdr->ps_page_dma);
		return -ENOMEM;
	}

	/* Fix for errata 23, can't cross 64kB boundary */
	if (!e1000_check_64k_bound(adapter, rxdr->desc, rxdr->size)) {
		void *olddesc = rxdr->desc;
		dma_addr_t olddma = rxdr->dma;
		DPRINTK(RX_ERR, ERR, "rxdr align check failed: %u bytes "
				     "at %p\n", rxdr->size, rxdr->desc);
		/* Try again, without freeing the previous */
		rxdr->desc = pci_alloc_consistent(pdev, rxdr->size, &rxdr->dma);
		/* Failed allocation, critical failure */
		if (!rxdr->desc) {
			pci_free_consistent(pdev, rxdr->size, olddesc, olddma);
			DPRINTK(PROBE, ERR,
				"Unable to allocate memory "
				"for the receive descriptor ring\n");
			goto setup_rx_desc_die;
		}

		if (!e1000_check_64k_bound(adapter, rxdr->desc, rxdr->size)) {
			/* give up */
			pci_free_consistent(pdev, rxdr->size, rxdr->desc,
					    rxdr->dma);
			pci_free_consistent(pdev, rxdr->size, olddesc, olddma);
			DPRINTK(PROBE, ERR,
				"Unable to allocate aligned memory "
				"for the receive descriptor ring\n");
			goto setup_rx_desc_die;
		} else {
			/* Free old allocation, new allocation was successful */
			pci_free_consistent(pdev, rxdr->size, olddesc, olddma);
		}
	}
	memset(rxdr->desc, 0, rxdr->size);

	rxdr->next_to_clean = 0;
	rxdr->next_to_use = 0;

	return 0;
}

/**
 * e1000_setup_all_rx_resources - wrapper to allocate Rx resources
 * 				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/

int
e1000_setup_all_rx_resources(struct e1000_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		err = e1000_setup_rx_resources(adapter, &adapter->rx_ring[i]);
		if (err) {
			DPRINTK(PROBE, ERR,
				"Allocation for Rx Queue %u failed\n", i);
			for (i-- ; i >= 0; i--)
				e1000_free_rx_resources(adapter,
							&adapter->rx_ring[i]);
			break;
		}
	}

	return err;
}

/**
 * e1000_setup_rctl - configure the receive control registers
 * @adapter: Board private structure
 **/
#define PAGE_USE_COUNT(S) (((S) >> PAGE_SHIFT) + \
			(((S) & (PAGE_SIZE - 1)) ? 1 : 0))
static void
e1000_setup_rctl(struct e1000_adapter *adapter)
{
	uint32_t rctl, rfctl;
	uint32_t psrctl = 0;
#ifndef CONFIG_E1000_DISABLE_PACKET_SPLIT
	uint32_t pages = 0;
#endif

	rctl = E1000_READ_REG(&adapter->hw, RCTL);

	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);

	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM |
		E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
		(adapter->hw.mc_filter_type << E1000_RCTL_MO_SHIFT);

	if (adapter->hw.tbi_compatibility_on == 1)
		rctl |= E1000_RCTL_SBP;
	else
		rctl &= ~E1000_RCTL_SBP;

	if (adapter->netdev->mtu <= ETH_DATA_LEN)
		rctl &= ~E1000_RCTL_LPE;
	else
		rctl |= E1000_RCTL_LPE;

	/* Setup buffer sizes */
	rctl &= ~E1000_RCTL_SZ_4096;
	rctl |= E1000_RCTL_BSEX;
	switch (adapter->rx_buffer_len) {
		case E1000_RXBUFFER_256:
			rctl |= E1000_RCTL_SZ_256;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case E1000_RXBUFFER_512:
			rctl |= E1000_RCTL_SZ_512;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case E1000_RXBUFFER_1024:
			rctl |= E1000_RCTL_SZ_1024;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case E1000_RXBUFFER_2048:
		default:
			rctl |= E1000_RCTL_SZ_2048;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case E1000_RXBUFFER_4096:
			rctl |= E1000_RCTL_SZ_4096;
			break;
		case E1000_RXBUFFER_8192:
			rctl |= E1000_RCTL_SZ_8192;
			break;
		case E1000_RXBUFFER_16384:
			rctl |= E1000_RCTL_SZ_16384;
			break;
	}

#ifndef CONFIG_E1000_DISABLE_PACKET_SPLIT
	/* 82571 and greater support packet-split where the protocol
	 * header is placed in skb->data and the packet data is
	 * placed in pages hanging off of skb_shinfo(skb)->nr_frags.
	 * In the case of a non-split, skb->data is linearly filled,
	 * followed by the page buffers.  Therefore, skb->data is
	 * sized to hold the largest protocol header.
	 */
	/* allocations using alloc_page take too long for regular MTU
	 * so only enable packet split for jumbo frames */
	pages = PAGE_USE_COUNT(adapter->netdev->mtu);
	if ((adapter->hw.mac_type >= e1000_82571) && (pages <= 3) &&
	    PAGE_SIZE <= 16384 && (rctl & E1000_RCTL_LPE))
		adapter->rx_ps_pages = pages;
	else
		adapter->rx_ps_pages = 0;
#endif
	if (adapter->rx_ps_pages) {
		/* Configure extra packet-split registers */
		rfctl = E1000_READ_REG(&adapter->hw, RFCTL);
		rfctl |= E1000_RFCTL_EXTEN;
		/* disable packet split support for IPv6 extension headers,
		 * because some malformed IPv6 headers can hang the RX */
		rfctl |= (E1000_RFCTL_IPV6_EX_DIS |
		          E1000_RFCTL_NEW_IPV6_EXT_DIS);

		E1000_WRITE_REG(&adapter->hw, RFCTL, rfctl);

		rctl |= E1000_RCTL_DTYP_PS;

		psrctl |= adapter->rx_ps_bsize0 >>
			E1000_PSRCTL_BSIZE0_SHIFT;

		switch (adapter->rx_ps_pages) {
		case 3:
			psrctl |= PAGE_SIZE <<
				E1000_PSRCTL_BSIZE3_SHIFT;
		case 2:
			psrctl |= PAGE_SIZE <<
				E1000_PSRCTL_BSIZE2_SHIFT;
		case 1:
			psrctl |= PAGE_SIZE >>
				E1000_PSRCTL_BSIZE1_SHIFT;
			break;
		}

		E1000_WRITE_REG(&adapter->hw, PSRCTL, psrctl);
	}

	E1000_WRITE_REG(&adapter->hw, RCTL, rctl);
}

/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/

static void
e1000_configure_rx(struct e1000_adapter *adapter)
{
	uint64_t rdba;
	struct e1000_hw *hw = &adapter->hw;
	uint32_t rdlen, rctl, rxcsum, ctrl_ext;

	if (adapter->rx_ps_pages) {
		/* this is a 32 byte descriptor */
		rdlen = adapter->rx_ring[0].count *
			sizeof(union e1000_rx_desc_packet_split);
		adapter->clean_rx = e1000_clean_rx_irq_ps;
		adapter->alloc_rx_buf = e1000_alloc_rx_buffers_ps;
	} else {
		rdlen = adapter->rx_ring[0].count *
			sizeof(struct e1000_rx_desc);
		adapter->clean_rx = e1000_clean_rx_irq;
		adapter->alloc_rx_buf = e1000_alloc_rx_buffers;
	}

	/* disable receives while setting up the descriptors */
	rctl = E1000_READ_REG(hw, RCTL);
	E1000_WRITE_REG(hw, RCTL, rctl & ~E1000_RCTL_EN);

	/* set the Receive Delay Timer Register */
	E1000_WRITE_REG(hw, RDTR, adapter->rx_int_delay);

	if (hw->mac_type >= e1000_82540) {
		E1000_WRITE_REG(hw, RADV, adapter->rx_abs_int_delay);
		if (adapter->itr > 1)
			E1000_WRITE_REG(hw, ITR,
				1000000000 / (adapter->itr * 256));
	}

	if (hw->mac_type >= e1000_82571) {
		ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
		/* Reset delay timers after every interrupt */
		ctrl_ext |= E1000_CTRL_EXT_INT_TIMER_CLR;
#ifdef CONFIG_E1000_NAPI
		/* Auto-Mask interrupts upon ICR read. */
		ctrl_ext |= E1000_CTRL_EXT_IAME;
#endif
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
		E1000_WRITE_REG(hw, IAM, ~0);
		E1000_WRITE_FLUSH(hw);
	}

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring */
	switch (adapter->num_rx_queues) {
	case 1:
	default:
		rdba = adapter->rx_ring[0].dma;
		E1000_WRITE_REG(hw, RDLEN, rdlen);
		E1000_WRITE_REG(hw, RDBAH, (rdba >> 32));
		E1000_WRITE_REG(hw, RDBAL, (rdba & 0x00000000ffffffffULL));
		E1000_WRITE_REG(hw, RDT, 0);
		E1000_WRITE_REG(hw, RDH, 0);
		adapter->rx_ring[0].rdh = ((hw->mac_type >= e1000_82543) ? E1000_RDH : E1000_82542_RDH);
		adapter->rx_ring[0].rdt = ((hw->mac_type >= e1000_82543) ? E1000_RDT : E1000_82542_RDT);
		break;
	}

	/* Enable 82543 Receive Checksum Offload for TCP and UDP */
	if (hw->mac_type >= e1000_82543) {
		rxcsum = E1000_READ_REG(hw, RXCSUM);
		if (adapter->rx_csum == TRUE) {
			rxcsum |= E1000_RXCSUM_TUOFL;

			/* Enable 82571 IPv4 payload checksum for UDP fragments
			 * Must be used in conjunction with packet-split. */
			if ((hw->mac_type >= e1000_82571) &&
			    (adapter->rx_ps_pages)) {
				rxcsum |= E1000_RXCSUM_IPPCSE;
			}
		} else {
			rxcsum &= ~E1000_RXCSUM_TUOFL;
			/* don't need to clear IPPCSE as it defaults to 0 */
		}
		E1000_WRITE_REG(hw, RXCSUM, rxcsum);
	}

	/* Enable Receives */
	E1000_WRITE_REG(hw, RCTL, rctl);
}

/**
 * e1000_free_tx_resources - Free Tx Resources per Queue
 * @adapter: board private structure
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/

static void
e1000_free_tx_resources(struct e1000_adapter *adapter,
                        struct e1000_tx_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	e1000_clean_tx_ring(adapter, tx_ring);

	vfree(tx_ring->buffer_info);
	tx_ring->buffer_info = NULL;

	pci_free_consistent(pdev, tx_ring->size, tx_ring->desc, tx_ring->dma);

	tx_ring->desc = NULL;
}

/**
 * e1000_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/

void
e1000_free_all_tx_resources(struct e1000_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		e1000_free_tx_resources(adapter, &adapter->tx_ring[i]);
}

static void
e1000_unmap_and_free_tx_resource(struct e1000_adapter *adapter,
			struct e1000_buffer *buffer_info)
{
	if (buffer_info->dma) {
		pci_unmap_page(adapter->pdev,
				buffer_info->dma,
				buffer_info->length,
				PCI_DMA_TODEVICE);
	}
	if (buffer_info->skb)
		dev_kfree_skb_any(buffer_info->skb);
	memset(buffer_info, 0, sizeof(struct e1000_buffer));
}

/**
 * e1000_clean_tx_ring - Free Tx Buffers
 * @adapter: board private structure
 * @tx_ring: ring to be cleaned
 **/

static void
e1000_clean_tx_ring(struct e1000_adapter *adapter,
                    struct e1000_tx_ring *tx_ring)
{
	struct e1000_buffer *buffer_info;
	unsigned long size;
	unsigned int i;

	/* Free all the Tx ring sk_buffs */

	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->buffer_info[i];
		e1000_unmap_and_free_tx_resource(adapter, buffer_info);
	}

	size = sizeof(struct e1000_buffer) * tx_ring->count;
	memset(tx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */

	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	tx_ring->last_tx_tso = 0;

	writel(0, adapter->hw.hw_addr + tx_ring->tdh);
	writel(0, adapter->hw.hw_addr + tx_ring->tdt);
}

/**
 * e1000_clean_all_tx_rings - Free Tx Buffers for all queues
 * @adapter: board private structure
 **/

static void
e1000_clean_all_tx_rings(struct e1000_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		e1000_clean_tx_ring(adapter, &adapter->tx_ring[i]);
}

/**
 * e1000_free_rx_resources - Free Rx Resources
 * @adapter: board private structure
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/

static void
e1000_free_rx_resources(struct e1000_adapter *adapter,
                        struct e1000_rx_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	e1000_clean_rx_ring(adapter, rx_ring);

	vfree(rx_ring->buffer_info);
	rx_ring->buffer_info = NULL;
	kfree(rx_ring->ps_page);
	rx_ring->ps_page = NULL;
	kfree(rx_ring->ps_page_dma);
	rx_ring->ps_page_dma = NULL;

	pci_free_consistent(pdev, rx_ring->size, rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * e1000_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/

void
e1000_free_all_rx_resources(struct e1000_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		e1000_free_rx_resources(adapter, &adapter->rx_ring[i]);
}

/**
 * e1000_clean_rx_ring - Free Rx Buffers per Queue
 * @adapter: board private structure
 * @rx_ring: ring to free buffers from
 **/

static void
e1000_clean_rx_ring(struct e1000_adapter *adapter,
                    struct e1000_rx_ring *rx_ring)
{
	struct e1000_buffer *buffer_info;
	struct e1000_ps_page *ps_page;
	struct e1000_ps_page_dma *ps_page_dma;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i, j;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		if (buffer_info->skb) {
			pci_unmap_single(pdev,
					 buffer_info->dma,
					 buffer_info->length,
					 PCI_DMA_FROMDEVICE);

			dev_kfree_skb(buffer_info->skb);
			buffer_info->skb = NULL;
		}
		ps_page = &rx_ring->ps_page[i];
		ps_page_dma = &rx_ring->ps_page_dma[i];
		for (j = 0; j < adapter->rx_ps_pages; j++) {
			if (!ps_page->ps_page[j]) break;
			pci_unmap_page(pdev,
				       ps_page_dma->ps_page_dma[j],
				       PAGE_SIZE, PCI_DMA_FROMDEVICE);
			ps_page_dma->ps_page_dma[j] = 0;
			put_page(ps_page->ps_page[j]);
			ps_page->ps_page[j] = NULL;
		}
	}

	size = sizeof(struct e1000_buffer) * rx_ring->count;
	memset(rx_ring->buffer_info, 0, size);
	size = sizeof(struct e1000_ps_page) * rx_ring->count;
	memset(rx_ring->ps_page, 0, size);
	size = sizeof(struct e1000_ps_page_dma) * rx_ring->count;
	memset(rx_ring->ps_page_dma, 0, size);

	/* Zero out the descriptor ring */

	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	writel(0, adapter->hw.hw_addr + rx_ring->rdh);
	writel(0, adapter->hw.hw_addr + rx_ring->rdt);
}

/**
 * e1000_clean_all_rx_rings - Free Rx Buffers for all queues
 * @adapter: board private structure
 **/

static void
e1000_clean_all_rx_rings(struct e1000_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		e1000_clean_rx_ring(adapter, &adapter->rx_ring[i]);
}

/* The 82542 2.0 (revision 2) needs to have the receive unit in reset
 * and memory write and invalidate disabled for certain operations
 */
static void
e1000_enter_82542_rst(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	uint32_t rctl;

	e1000_pci_clear_mwi(&adapter->hw);

	rctl = E1000_READ_REG(&adapter->hw, RCTL);
	rctl |= E1000_RCTL_RST;
	E1000_WRITE_REG(&adapter->hw, RCTL, rctl);
	E1000_WRITE_FLUSH(&adapter->hw);
	mdelay(5);

	if (netif_running(netdev))
		e1000_clean_all_rx_rings(adapter);
}

static void
e1000_leave_82542_rst(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	uint32_t rctl;

	rctl = E1000_READ_REG(&adapter->hw, RCTL);
	rctl &= ~E1000_RCTL_RST;
	E1000_WRITE_REG(&adapter->hw, RCTL, rctl);
	E1000_WRITE_FLUSH(&adapter->hw);
	mdelay(5);

	if (adapter->hw.pci_cmd_word & PCI_COMMAND_INVALIDATE)
		e1000_pci_set_mwi(&adapter->hw);

	if (netif_running(netdev)) {
		/* No need to loop, because 82542 supports only 1 queue */
		struct e1000_rx_ring *ring = &adapter->rx_ring[0];
		e1000_configure_rx(adapter);
		adapter->alloc_rx_buf(adapter, ring, E1000_DESC_UNUSED(ring));
	}
}

/**
 * e1000_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/

static int
e1000_set_mac(struct net_device *netdev, void *p)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* 82542 2.0 needs to be in reset to write receive address registers */

	if (adapter->hw.mac_type == e1000_82542_rev2_0)
		e1000_enter_82542_rst(adapter);

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac_addr, addr->sa_data, netdev->addr_len);

	e1000_rar_set(&adapter->hw, adapter->hw.mac_addr, 0);

	/* With 82571 controllers, LAA may be overwritten (with the default)
	 * due to controller reset from the other port. */
	if (adapter->hw.mac_type == e1000_82571) {
		/* activate the work around */
		adapter->hw.laa_is_present = 1;

		/* Hold a copy of the LAA in RAR[14] This is done so that
		 * between the time RAR[0] gets clobbered  and the time it
		 * gets fixed (in e1000_watchdog), the actual LAA is in one
		 * of the RARs and no incoming packets directed to this port
		 * are dropped. Eventaully the LAA will be in RAR[0] and
		 * RAR[14] */
		e1000_rar_set(&adapter->hw, adapter->hw.mac_addr,
					E1000_RAR_ENTRIES - 1);
	}

	if (adapter->hw.mac_type == e1000_82542_rev2_0)
		e1000_leave_82542_rst(adapter);

	return 0;
}

/**
 * e1000_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 **/

static void
e1000_set_multi(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct dev_mc_list *mc_ptr;
	uint32_t rctl;
	uint32_t hash_value;
	int i, rar_entries = E1000_RAR_ENTRIES;
	int mta_reg_count = (hw->mac_type == e1000_ich8lan) ?
				E1000_NUM_MTA_REGISTERS_ICH8LAN :
				E1000_NUM_MTA_REGISTERS;

	if (adapter->hw.mac_type == e1000_ich8lan)
		rar_entries = E1000_RAR_ENTRIES_ICH8LAN;

	/* reserve RAR[14] for LAA over-write work-around */
	if (adapter->hw.mac_type == e1000_82571)
		rar_entries--;

	/* Check for Promiscuous and All Multicast modes */

	rctl = E1000_READ_REG(hw, RCTL);

	if (netdev->flags & IFF_PROMISC) {
		rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
	} else if (netdev->flags & IFF_ALLMULTI) {
		rctl |= E1000_RCTL_MPE;
		rctl &= ~E1000_RCTL_UPE;
	} else {
		rctl &= ~(E1000_RCTL_UPE | E1000_RCTL_MPE);
	}

	E1000_WRITE_REG(hw, RCTL, rctl);

	/* 82542 2.0 needs to be in reset to write receive address registers */

	if (hw->mac_type == e1000_82542_rev2_0)
		e1000_enter_82542_rst(adapter);

	/* load the first 14 multicast address into the exact filters 1-14
	 * RAR 0 is used for the station MAC adddress
	 * if there are not 14 addresses, go ahead and clear the filters
	 * -- with 82571 controllers only 0-13 entries are filled here
	 */
	mc_ptr = netdev->mc_list;

	for (i = 1; i < rar_entries; i++) {
		if (mc_ptr) {
			e1000_rar_set(hw, mc_ptr->dmi_addr, i);
			mc_ptr = mc_ptr->next;
		} else {
			E1000_WRITE_REG_ARRAY(hw, RA, i << 1, 0);
			E1000_WRITE_FLUSH(hw);
			E1000_WRITE_REG_ARRAY(hw, RA, (i << 1) + 1, 0);
			E1000_WRITE_FLUSH(hw);
		}
	}

	/* clear the old settings from the multicast hash table */

	for (i = 0; i < mta_reg_count; i++) {
		E1000_WRITE_REG_ARRAY(hw, MTA, i, 0);
		E1000_WRITE_FLUSH(hw);
	}

	/* load any remaining addresses into the hash table */

	for (; mc_ptr; mc_ptr = mc_ptr->next) {
		hash_value = e1000_hash_mc_addr(hw, mc_ptr->dmi_addr);
		e1000_mta_set(hw, hash_value);
	}

	if (hw->mac_type == e1000_82542_rev2_0)
		e1000_leave_82542_rst(adapter);
}

/* Need to wait a few seconds after link up to get diagnostic information from
 * the phy */

static void
e1000_update_phy_info(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *) data;
	e1000_phy_get_info(&adapter->hw, &adapter->phy_info);
}

/**
 * e1000_82547_tx_fifo_stall - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/

static void
e1000_82547_tx_fifo_stall(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *) data;
	struct net_device *netdev = adapter->netdev;
	uint32_t tctl;

	if (atomic_read(&adapter->tx_fifo_stall)) {
		if ((E1000_READ_REG(&adapter->hw, TDT) ==
		    E1000_READ_REG(&adapter->hw, TDH)) &&
		   (E1000_READ_REG(&adapter->hw, TDFT) ==
		    E1000_READ_REG(&adapter->hw, TDFH)) &&
		   (E1000_READ_REG(&adapter->hw, TDFTS) ==
		    E1000_READ_REG(&adapter->hw, TDFHS))) {
			tctl = E1000_READ_REG(&adapter->hw, TCTL);
			E1000_WRITE_REG(&adapter->hw, TCTL,
					tctl & ~E1000_TCTL_EN);
			E1000_WRITE_REG(&adapter->hw, TDFT,
					adapter->tx_head_addr);
			E1000_WRITE_REG(&adapter->hw, TDFH,
					adapter->tx_head_addr);
			E1000_WRITE_REG(&adapter->hw, TDFTS,
					adapter->tx_head_addr);
			E1000_WRITE_REG(&adapter->hw, TDFHS,
					adapter->tx_head_addr);
			E1000_WRITE_REG(&adapter->hw, TCTL, tctl);
			E1000_WRITE_FLUSH(&adapter->hw);

			adapter->tx_fifo_head = 0;
			atomic_set(&adapter->tx_fifo_stall, 0);
			netif_wake_queue(netdev);
		} else {
			mod_timer(&adapter->tx_fifo_stall_timer, jiffies + 1);
		}
	}
}

/**
 * e1000_watchdog - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void
e1000_watchdog(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *) data;
	struct net_device *netdev = adapter->netdev;
	struct e1000_tx_ring *txdr = adapter->tx_ring;
	uint32_t link, tctl;
	int32_t ret_val;

	ret_val = e1000_check_for_link(&adapter->hw);
	if ((ret_val == E1000_ERR_PHY) &&
	    (adapter->hw.phy_type == e1000_phy_igp_3) &&
	    (E1000_READ_REG(&adapter->hw, CTRL) & E1000_PHY_CTRL_GBE_DISABLE)) {
		/* See e1000_kumeran_lock_loss_workaround() */
		DPRINTK(LINK, INFO,
			"Gigabit has been disabled, downgrading speed\n");
	}

	if (adapter->hw.mac_type == e1000_82573) {
		e1000_enable_tx_pkt_filtering(&adapter->hw);
		if (adapter->mng_vlan_id != adapter->hw.mng_cookie.vlan_id)
			e1000_update_mng_vlan(adapter);
	}

	if ((adapter->hw.media_type == e1000_media_type_internal_serdes) &&
	   !(E1000_READ_REG(&adapter->hw, TXCW) & E1000_TXCW_ANE))
		link = !adapter->hw.serdes_link_down;
	else
		link = E1000_READ_REG(&adapter->hw, STATUS) & E1000_STATUS_LU;

	if (link) {
		if (!netif_carrier_ok(netdev)) {
			boolean_t txb2b = 1;
			e1000_get_speed_and_duplex(&adapter->hw,
			                           &adapter->link_speed,
			                           &adapter->link_duplex);

			DPRINTK(LINK, INFO, "NIC Link is Up %d Mbps %s\n",
			       adapter->link_speed,
			       adapter->link_duplex == FULL_DUPLEX ?
			       "Full Duplex" : "Half Duplex");

			/* tweak tx_queue_len according to speed/duplex
			 * and adjust the timeout factor */
			netdev->tx_queue_len = adapter->tx_queue_len;
			adapter->tx_timeout_factor = 1;
			switch (adapter->link_speed) {
			case SPEED_10:
				txb2b = 0;
				netdev->tx_queue_len = 10;
				adapter->tx_timeout_factor = 8;
				break;
			case SPEED_100:
				txb2b = 0;
				netdev->tx_queue_len = 100;
				/* maybe add some timeout factor ? */
				break;
			}

			if ((adapter->hw.mac_type == e1000_82571 ||
			     adapter->hw.mac_type == e1000_82572) &&
			    txb2b == 0) {
				uint32_t tarc0;
				tarc0 = E1000_READ_REG(&adapter->hw, TARC0);
				tarc0 &= ~(1 << 21);
				E1000_WRITE_REG(&adapter->hw, TARC0, tarc0);
			}

#ifdef NETIF_F_TSO
			/* disable TSO for pcie and 10/100 speeds, to avoid
			 * some hardware issues */
			if (!adapter->tso_force &&
			    adapter->hw.bus_type == e1000_bus_type_pci_express){
				switch (adapter->link_speed) {
				case SPEED_10:
				case SPEED_100:
					DPRINTK(PROBE,INFO,
				        "10/100 speed: disabling TSO\n");
					netdev->features &= ~NETIF_F_TSO;
#ifdef NETIF_F_TSO6
					netdev->features &= ~NETIF_F_TSO6;
#endif
					break;
				case SPEED_1000:
					netdev->features |= NETIF_F_TSO;
#ifdef NETIF_F_TSO6
					netdev->features |= NETIF_F_TSO6;
#endif
					break;
				default:
					/* oops */
					break;
				}
			}
#endif

			/* enable transmits in the hardware, need to do this
			 * after setting TARC0 */
			tctl = E1000_READ_REG(&adapter->hw, TCTL);
			tctl |= E1000_TCTL_EN;
			E1000_WRITE_REG(&adapter->hw, TCTL, tctl);

			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
			mod_timer(&adapter->phy_info_timer, jiffies + 2 * HZ);
			adapter->smartspeed = 0;
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;
			DPRINTK(LINK, INFO, "NIC Link is Down\n");
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
			mod_timer(&adapter->phy_info_timer, jiffies + 2 * HZ);

			/* 80003ES2LAN workaround--
			 * For packet buffer work-around on link down event;
			 * disable receives in the ISR and
			 * reset device here in the watchdog
			 */
			if (adapter->hw.mac_type == e1000_80003es2lan)
				/* reset device */
				schedule_work(&adapter->reset_task);
		}

		e1000_smartspeed(adapter);
	}

	e1000_update_stats(adapter);

	adapter->hw.tx_packet_delta = adapter->stats.tpt - adapter->tpt_old;
	adapter->tpt_old = adapter->stats.tpt;
	adapter->hw.collision_delta = adapter->stats.colc - adapter->colc_old;
	adapter->colc_old = adapter->stats.colc;

	adapter->gorcl = adapter->stats.gorcl - adapter->gorcl_old;
	adapter->gorcl_old = adapter->stats.gorcl;
	adapter->gotcl = adapter->stats.gotcl - adapter->gotcl_old;
	adapter->gotcl_old = adapter->stats.gotcl;

	e1000_update_adaptive(&adapter->hw);

	if (!netif_carrier_ok(netdev)) {
		if (E1000_DESC_UNUSED(txdr) + 1 < txdr->count) {
			/* We've lost link, so the controller stops DMA,
			 * but we've got queued Tx work that's never going
			 * to get done, so reset controller to flush Tx.
			 * (Do the reset outside of interrupt context). */
			adapter->tx_timeout_count++;
			schedule_work(&adapter->reset_task);
		}
	}

	/* Dynamic mode for Interrupt Throttle Rate (ITR) */
	if (adapter->hw.mac_type >= e1000_82540 && adapter->itr == 1) {
		/* Symmetric Tx/Rx gets a reduced ITR=2000; Total
		 * asymmetrical Tx or Rx gets ITR=8000; everyone
		 * else is between 2000-8000. */
		uint32_t goc = (adapter->gotcl + adapter->gorcl) / 10000;
		uint32_t dif = (adapter->gotcl > adapter->gorcl ?
			adapter->gotcl - adapter->gorcl :
			adapter->gorcl - adapter->gotcl) / 10000;
		uint32_t itr = goc > 0 ? (dif * 6000 / goc + 2000) : 8000;
		E1000_WRITE_REG(&adapter->hw, ITR, 1000000000 / (itr * 256));
	}

	/* Cause software interrupt to ensure rx ring is cleaned */
	E1000_WRITE_REG(&adapter->hw, ICS, E1000_ICS_RXDMT0);

	/* Force detection of hung controller every watchdog period */
	adapter->detect_tx_hung = TRUE;

	/* With 82571 controllers, LAA may be overwritten due to controller
	 * reset from the other port. Set the appropriate LAA in RAR[0] */
	if (adapter->hw.mac_type == e1000_82571 && adapter->hw.laa_is_present)
		e1000_rar_set(&adapter->hw, adapter->hw.mac_addr, 0);

	/* Reset the timer */
	mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);
}

#define E1000_TX_FLAGS_CSUM		0x00000001
#define E1000_TX_FLAGS_VLAN		0x00000002
#define E1000_TX_FLAGS_TSO		0x00000004
#define E1000_TX_FLAGS_IPV4		0x00000008
#define E1000_TX_FLAGS_VLAN_MASK	0xffff0000
#define E1000_TX_FLAGS_VLAN_SHIFT	16

static int
e1000_tso(struct e1000_adapter *adapter, struct e1000_tx_ring *tx_ring,
          struct sk_buff *skb)
{
#ifdef NETIF_F_TSO
	struct e1000_context_desc *context_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i;
	uint32_t cmd_length = 0;
	uint16_t ipcse = 0, tucse, mss;
	uint8_t ipcss, ipcso, tucss, tucso, hdr_len;
	int err;

	if (skb_is_gso(skb)) {
		if (skb_header_cloned(skb)) {
			err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (err)
				return err;
		}

		hdr_len = ((skb->h.raw - skb->data) + (skb->h.th->doff << 2));
		mss = skb_shinfo(skb)->gso_size;
		if (skb->protocol == htons(ETH_P_IP)) {
			skb->nh.iph->tot_len = 0;
			skb->nh.iph->check = 0;
			skb->h.th->check =
				~csum_tcpudp_magic(skb->nh.iph->saddr,
						   skb->nh.iph->daddr,
						   0,
						   IPPROTO_TCP,
						   0);
			cmd_length = E1000_TXD_CMD_IP;
			ipcse = skb->h.raw - skb->data - 1;
#ifdef NETIF_F_TSO6
		} else if (skb->protocol == htons(ETH_P_IPV6)) {
			skb->nh.ipv6h->payload_len = 0;
			skb->h.th->check =
				~csum_ipv6_magic(&skb->nh.ipv6h->saddr,
						 &skb->nh.ipv6h->daddr,
						 0,
						 IPPROTO_TCP,
						 0);
			ipcse = 0;
#endif
		}
		ipcss = skb->nh.raw - skb->data;
		ipcso = (void *)&(skb->nh.iph->check) - (void *)skb->data;
		tucss = skb->h.raw - skb->data;
		tucso = (void *)&(skb->h.th->check) - (void *)skb->data;
		tucse = 0;

		cmd_length |= (E1000_TXD_CMD_DEXT | E1000_TXD_CMD_TSE |
			       E1000_TXD_CMD_TCP | (skb->len - (hdr_len)));

		i = tx_ring->next_to_use;
		context_desc = E1000_CONTEXT_DESC(*tx_ring, i);
		buffer_info = &tx_ring->buffer_info[i];

		context_desc->lower_setup.ip_fields.ipcss  = ipcss;
		context_desc->lower_setup.ip_fields.ipcso  = ipcso;
		context_desc->lower_setup.ip_fields.ipcse  = cpu_to_le16(ipcse);
		context_desc->upper_setup.tcp_fields.tucss = tucss;
		context_desc->upper_setup.tcp_fields.tucso = tucso;
		context_desc->upper_setup.tcp_fields.tucse = cpu_to_le16(tucse);
		context_desc->tcp_seg_setup.fields.mss     = cpu_to_le16(mss);
		context_desc->tcp_seg_setup.fields.hdr_len = hdr_len;
		context_desc->cmd_and_length = cpu_to_le32(cmd_length);

		buffer_info->time_stamp = jiffies;

		if (++i == tx_ring->count) i = 0;
		tx_ring->next_to_use = i;

		return TRUE;
	}
#endif

	return FALSE;
}

static boolean_t
e1000_tx_csum(struct e1000_adapter *adapter, struct e1000_tx_ring *tx_ring,
              struct sk_buff *skb)
{
	struct e1000_context_desc *context_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i;
	uint8_t css;

	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		css = skb->h.raw - skb->data;

		i = tx_ring->next_to_use;
		buffer_info = &tx_ring->buffer_info[i];
		context_desc = E1000_CONTEXT_DESC(*tx_ring, i);

		context_desc->upper_setup.tcp_fields.tucss = css;
		context_desc->upper_setup.tcp_fields.tucso = css + skb->csum;
		context_desc->upper_setup.tcp_fields.tucse = 0;
		context_desc->tcp_seg_setup.data = 0;
		context_desc->cmd_and_length = cpu_to_le32(E1000_TXD_CMD_DEXT);

		buffer_info->time_stamp = jiffies;

		if (unlikely(++i == tx_ring->count)) i = 0;
		tx_ring->next_to_use = i;

		return TRUE;
	}

	return FALSE;
}

#define E1000_MAX_TXD_PWR	12
#define E1000_MAX_DATA_PER_TXD	(1<<E1000_MAX_TXD_PWR)

static int
e1000_tx_map(struct e1000_adapter *adapter, struct e1000_tx_ring *tx_ring,
             struct sk_buff *skb, unsigned int first, unsigned int max_per_txd,
             unsigned int nr_frags, unsigned int mss)
{
	struct e1000_buffer *buffer_info;
	unsigned int len = skb->len;
	unsigned int offset = 0, size, count = 0, i;
	unsigned int f;
	len -= skb->data_len;

	i = tx_ring->next_to_use;

	while (len) {
		buffer_info = &tx_ring->buffer_info[i];
		size = min(len, max_per_txd);
#ifdef NETIF_F_TSO
		/* Workaround for Controller erratum --
		 * descriptor for non-tso packet in a linear SKB that follows a
		 * tso gets written back prematurely before the data is fully
		 * DMA'd to the controller */
		if (!skb->data_len && tx_ring->last_tx_tso &&
		    !skb_is_gso(skb)) {
			tx_ring->last_tx_tso = 0;
			size -= 4;
		}

		/* Workaround for premature desc write-backs
		 * in TSO mode.  Append 4-byte sentinel desc */
		if (unlikely(mss && !nr_frags && size == len && size > 8))
			size -= 4;
#endif
		/* work-around for errata 10 and it applies
		 * to all controllers in PCI-X mode
		 * The fix is to make sure that the first descriptor of a
		 * packet is smaller than 2048 - 16 - 16 (or 2016) bytes
		 */
		if (unlikely((adapter->hw.bus_type == e1000_bus_type_pcix) &&
		                (size > 2015) && count == 0))
		        size = 2015;

		/* Workaround for potential 82544 hang in PCI-X.  Avoid
		 * terminating buffers within evenly-aligned dwords. */
		if (unlikely(adapter->pcix_82544 &&
		   !((unsigned long)(skb->data + offset + size - 1) & 4) &&
		   size > 4))
			size -= 4;

		buffer_info->length = size;
		buffer_info->dma =
			pci_map_single(adapter->pdev,
				skb->data + offset,
				size,
				PCI_DMA_TODEVICE);
		buffer_info->time_stamp = jiffies;

		len -= size;
		offset += size;
		count++;
		if (unlikely(++i == tx_ring->count)) i = 0;
	}

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];
		len = frag->size;
		offset = frag->page_offset;

		while (len) {
			buffer_info = &tx_ring->buffer_info[i];
			size = min(len, max_per_txd);
#ifdef NETIF_F_TSO
			/* Workaround for premature desc write-backs
			 * in TSO mode.  Append 4-byte sentinel desc */
			if (unlikely(mss && f == (nr_frags-1) && size == len && size > 8))
				size -= 4;
#endif
			/* Workaround for potential 82544 hang in PCI-X.
			 * Avoid terminating buffers within evenly-aligned
			 * dwords. */
			if (unlikely(adapter->pcix_82544 &&
			   !((unsigned long)(frag->page+offset+size-1) & 4) &&
			   size > 4))
				size -= 4;

			buffer_info->length = size;
			buffer_info->dma =
				pci_map_page(adapter->pdev,
					frag->page,
					offset,
					size,
					PCI_DMA_TODEVICE);
			buffer_info->time_stamp = jiffies;

			len -= size;
			offset += size;
			count++;
			if (unlikely(++i == tx_ring->count)) i = 0;
		}
	}

	i = (i == 0) ? tx_ring->count - 1 : i - 1;
	tx_ring->buffer_info[i].skb = skb;
	tx_ring->buffer_info[first].next_to_watch = i;

	return count;
}

static void
e1000_tx_queue(struct e1000_adapter *adapter, struct e1000_tx_ring *tx_ring,
               int tx_flags, int count)
{
	struct e1000_tx_desc *tx_desc = NULL;
	struct e1000_buffer *buffer_info;
	uint32_t txd_upper = 0, txd_lower = E1000_TXD_CMD_IFCS;
	unsigned int i;

	if (likely(tx_flags & E1000_TX_FLAGS_TSO)) {
		txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D |
		             E1000_TXD_CMD_TSE;
		txd_upper |= E1000_TXD_POPTS_TXSM << 8;

		if (likely(tx_flags & E1000_TX_FLAGS_IPV4))
			txd_upper |= E1000_TXD_POPTS_IXSM << 8;
	}

	if (likely(tx_flags & E1000_TX_FLAGS_CSUM)) {
		txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
		txd_upper |= E1000_TXD_POPTS_TXSM << 8;
	}

	if (unlikely(tx_flags & E1000_TX_FLAGS_VLAN)) {
		txd_lower |= E1000_TXD_CMD_VLE;
		txd_upper |= (tx_flags & E1000_TX_FLAGS_VLAN_MASK);
	}

	i = tx_ring->next_to_use;

	while (count--) {
		buffer_info = &tx_ring->buffer_info[i];
		tx_desc = E1000_TX_DESC(*tx_ring, i);
		tx_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
		tx_desc->lower.data =
			cpu_to_le32(txd_lower | buffer_info->length);
		tx_desc->upper.data = cpu_to_le32(txd_upper);
		if (unlikely(++i == tx_ring->count)) i = 0;
	}

	tx_desc->lower.data |= cpu_to_le32(adapter->txd_cmd);

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64). */
	wmb();

	tx_ring->next_to_use = i;
	writel(i, adapter->hw.hw_addr + tx_ring->tdt);
}

/**
 * 82547 workaround to avoid controller hang in half-duplex environment.
 * The workaround is to avoid queuing a large packet that would span
 * the internal Tx FIFO ring boundary by notifying the stack to resend
 * the packet at a later time.  This gives the Tx FIFO an opportunity to
 * flush all packets.  When that occurs, we reset the Tx FIFO pointers
 * to the beginning of the Tx FIFO.
 **/

#define E1000_FIFO_HDR			0x10
#define E1000_82547_PAD_LEN		0x3E0

static int
e1000_82547_fifo_workaround(struct e1000_adapter *adapter, struct sk_buff *skb)
{
	uint32_t fifo_space = adapter->tx_fifo_size - adapter->tx_fifo_head;
	uint32_t skb_fifo_len = skb->len + E1000_FIFO_HDR;

	E1000_ROUNDUP(skb_fifo_len, E1000_FIFO_HDR);

	if (adapter->link_duplex != HALF_DUPLEX)
		goto no_fifo_stall_required;

	if (atomic_read(&adapter->tx_fifo_stall))
		return 1;

	if (skb_fifo_len >= (E1000_82547_PAD_LEN + fifo_space)) {
		atomic_set(&adapter->tx_fifo_stall, 1);
		return 1;
	}

no_fifo_stall_required:
	adapter->tx_fifo_head += skb_fifo_len;
	if (adapter->tx_fifo_head >= adapter->tx_fifo_size)
		adapter->tx_fifo_head -= adapter->tx_fifo_size;
	return 0;
}

#define MINIMUM_DHCP_PACKET_SIZE 282
static int
e1000_transfer_dhcp_info(struct e1000_adapter *adapter, struct sk_buff *skb)
{
	struct e1000_hw *hw =  &adapter->hw;
	uint16_t length, offset;
	if (vlan_tx_tag_present(skb)) {
		if (!((vlan_tx_tag_get(skb) == adapter->hw.mng_cookie.vlan_id) &&
			( adapter->hw.mng_cookie.status &
			  E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT)) )
			return 0;
	}
	if (skb->len > MINIMUM_DHCP_PACKET_SIZE) {
		struct ethhdr *eth = (struct ethhdr *) skb->data;
		if ((htons(ETH_P_IP) == eth->h_proto)) {
			const struct iphdr *ip =
				(struct iphdr *)((uint8_t *)skb->data+14);
			if (IPPROTO_UDP == ip->protocol) {
				struct udphdr *udp =
					(struct udphdr *)((uint8_t *)ip +
						(ip->ihl << 2));
				if (ntohs(udp->dest) == 67) {
					offset = (uint8_t *)udp + 8 - skb->data;
					length = skb->len - offset;

					return e1000_mng_write_dhcp_info(hw,
							(uint8_t *)udp + 8,
							length);
				}
			}
		}
	}
	return 0;
}

static int __e1000_maybe_stop_tx(struct net_device *netdev, int size)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_tx_ring *tx_ring = adapter->tx_ring;

	netif_stop_queue(netdev);
	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it. */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available. */
	if (likely(E1000_DESC_UNUSED(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! */
	netif_start_queue(netdev);
	return 0;
}

static int e1000_maybe_stop_tx(struct net_device *netdev,
                               struct e1000_tx_ring *tx_ring, int size)
{
	if (likely(E1000_DESC_UNUSED(tx_ring) >= size))
		return 0;
	return __e1000_maybe_stop_tx(netdev, size);
}

#define TXD_USE_COUNT(S, X) (((S) >> (X)) + 1 )
static int
e1000_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_tx_ring *tx_ring;
	unsigned int first, max_per_txd = E1000_MAX_DATA_PER_TXD;
	unsigned int max_txd_pwr = E1000_MAX_TXD_PWR;
	unsigned int tx_flags = 0;
	unsigned int len = skb->len;
	unsigned long flags;
	unsigned int nr_frags = 0;
	unsigned int mss = 0;
	int count = 0;
	int tso;
	unsigned int f;
	len -= skb->data_len;

	/* This goes back to the question of how to logically map a tx queue
	 * to a flow.  Right now, performance is impacted slightly negatively
	 * if using multiple tx queues.  If the stack breaks away from a
	 * single qdisc implementation, we can look at this again. */
	tx_ring = adapter->tx_ring;

	if (unlikely(skb->len <= 0)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* 82571 and newer doesn't need the workaround that limited descriptor
	 * length to 4kB */
	if (adapter->hw.mac_type >= e1000_82571)
		max_per_txd = 8192;

#ifdef NETIF_F_TSO
	mss = skb_shinfo(skb)->gso_size;
	/* The controller does a simple calculation to
	 * make sure there is enough room in the FIFO before
	 * initiating the DMA for each buffer.  The calc is:
	 * 4 = ceil(buffer len/mss).  To make sure we don't
	 * overrun the FIFO, adjust the max buffer len if mss
	 * drops. */
	if (mss) {
		uint8_t hdr_len;
		max_per_txd = min(mss << 2, max_per_txd);
		max_txd_pwr = fls(max_per_txd) - 1;

		/* TSO Workaround for 82571/2/3 Controllers -- if skb->data
		* points to just header, pull a few bytes of payload from
		* frags into skb->data */
		hdr_len = ((skb->h.raw - skb->data) + (skb->h.th->doff << 2));
		if (skb->data_len && (hdr_len == (skb->len - skb->data_len))) {
			switch (adapter->hw.mac_type) {
				unsigned int pull_size;
			case e1000_82571:
			case e1000_82572:
			case e1000_82573:
			case e1000_ich8lan:
				pull_size = min((unsigned int)4, skb->data_len);
				if (!__pskb_pull_tail(skb, pull_size)) {
					DPRINTK(DRV, ERR,
						"__pskb_pull_tail failed.\n");
					dev_kfree_skb_any(skb);
					return NETDEV_TX_OK;
				}
				len = skb->len - skb->data_len;
				break;
			default:
				/* do nothing */
				break;
			}
		}
	}

	/* reserve a descriptor for the offload context */
	if ((mss) || (skb->ip_summed == CHECKSUM_PARTIAL))
		count++;
	count++;
#else
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		count++;
#endif

#ifdef NETIF_F_TSO
	/* Controller Erratum workaround */
	if (!skb->data_len && tx_ring->last_tx_tso && !skb_is_gso(skb))
		count++;
#endif

	count += TXD_USE_COUNT(len, max_txd_pwr);

	if (adapter->pcix_82544)
		count++;

	/* work-around for errata 10 and it applies to all controllers
	 * in PCI-X mode, so add one more descriptor to the count
	 */
	if (unlikely((adapter->hw.bus_type == e1000_bus_type_pcix) &&
			(len > 2015)))
		count++;

	nr_frags = skb_shinfo(skb)->nr_frags;
	for (f = 0; f < nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size,
				       max_txd_pwr);
	if (adapter->pcix_82544)
		count += nr_frags;


	if (adapter->hw.tx_pkt_filtering &&
	    (adapter->hw.mac_type == e1000_82573))
		e1000_transfer_dhcp_info(adapter, skb);

	local_irq_save(flags);
	if (!spin_trylock(&tx_ring->tx_lock)) {
		/* Collision - tell upper layer to requeue */
		local_irq_restore(flags);
		return NETDEV_TX_LOCKED;
	}

	/* need: count + 2 desc gap to keep tail from touching
	 * head, otherwise try next time */
	if (unlikely(e1000_maybe_stop_tx(netdev, tx_ring, count + 2))) {
		spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(adapter->hw.mac_type == e1000_82547)) {
		if (unlikely(e1000_82547_fifo_workaround(adapter, skb))) {
			netif_stop_queue(netdev);
			mod_timer(&adapter->tx_fifo_stall_timer, jiffies + 1);
			spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}
	}

	if (unlikely(adapter->vlgrp && vlan_tx_tag_present(skb))) {
		tx_flags |= E1000_TX_FLAGS_VLAN;
		tx_flags |= (vlan_tx_tag_get(skb) << E1000_TX_FLAGS_VLAN_SHIFT);
	}

	first = tx_ring->next_to_use;

	tso = e1000_tso(adapter, tx_ring, skb);
	if (tso < 0) {
		dev_kfree_skb_any(skb);
		spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
		return NETDEV_TX_OK;
	}

	if (likely(tso)) {
		tx_ring->last_tx_tso = 1;
		tx_flags |= E1000_TX_FLAGS_TSO;
	} else if (likely(e1000_tx_csum(adapter, tx_ring, skb)))
		tx_flags |= E1000_TX_FLAGS_CSUM;

	/* Old method was to assume IPv4 packet by default if TSO was enabled.
	 * 82571 hardware supports TSO capabilities for IPv6 as well...
	 * no longer assume, we must. */
	if (likely(skb->protocol == htons(ETH_P_IP)))
		tx_flags |= E1000_TX_FLAGS_IPV4;

	e1000_tx_queue(adapter, tx_ring, tx_flags,
	               e1000_tx_map(adapter, tx_ring, skb, first,
	                            max_per_txd, nr_frags, mss));

	netdev->trans_start = jiffies;

	/* Make sure there is space in the ring for the next send. */
	e1000_maybe_stop_tx(netdev, tx_ring, MAX_SKB_FRAGS + 2);

	spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
	return NETDEV_TX_OK;
}

/**
 * e1000_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/

static void
e1000_tx_timeout(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	adapter->tx_timeout_count++;
	schedule_work(&adapter->reset_task);
}

static void
e1000_reset_task(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	e1000_reinit_locked(adapter);
}

/**
 * e1000_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/

static struct net_device_stats *
e1000_get_stats(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	/* only return the current stats */
	return &adapter->net_stats;
}

/**
 * e1000_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/

static int
e1000_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	int max_frame = new_mtu + ENET_HEADER_SIZE + ETHERNET_FCS_SIZE;
	uint16_t eeprom_data = 0;

	if ((max_frame < MINIMUM_ETHERNET_FRAME_SIZE) ||
	    (max_frame > MAX_JUMBO_FRAME_SIZE)) {
		DPRINTK(PROBE, ERR, "Invalid MTU setting\n");
		return -EINVAL;
	}

	/* Adapter-specific max frame size limits. */
	switch (adapter->hw.mac_type) {
	case e1000_undefined ... e1000_82542_rev2_1:
	case e1000_ich8lan:
		if (max_frame > MAXIMUM_ETHERNET_FRAME_SIZE) {
			DPRINTK(PROBE, ERR, "Jumbo Frames not supported.\n");
			return -EINVAL;
		}
		break;
	case e1000_82573:
		/* Jumbo Frames not supported if:
		 * - this is not an 82573L device
		 * - ASPM is enabled in any way (0x1A bits 3:2) */
		e1000_read_eeprom(&adapter->hw, EEPROM_INIT_3GIO_3, 1,
		                  &eeprom_data);
		if ((adapter->hw.device_id != E1000_DEV_ID_82573L) ||
		    (eeprom_data & EEPROM_WORD1A_ASPM_MASK)) {
			if (max_frame > MAXIMUM_ETHERNET_FRAME_SIZE) {
				DPRINTK(PROBE, ERR,
			            	"Jumbo Frames not supported.\n");
				return -EINVAL;
			}
			break;
		}
		/* ERT will be enabled later to enable wire speed receives */

		/* fall through to get support */
	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
#define MAX_STD_JUMBO_FRAME_SIZE 9234
		if (max_frame > MAX_STD_JUMBO_FRAME_SIZE) {
			DPRINTK(PROBE, ERR, "MTU > 9216 not supported.\n");
			return -EINVAL;
		}
		break;
	default:
		/* Capable of supporting up to MAX_JUMBO_FRAME_SIZE limit. */
		break;
	}

	/* NOTE: netdev_alloc_skb reserves 16 bytes, and typically NET_IP_ALIGN
	 * means we reserve 2 more, this pushes us to allocate from the next
	 * larger slab size
	 * i.e. RXBUFFER_2048 --> size-4096 slab */

	if (max_frame <= E1000_RXBUFFER_256)
		adapter->rx_buffer_len = E1000_RXBUFFER_256;
	else if (max_frame <= E1000_RXBUFFER_512)
		adapter->rx_buffer_len = E1000_RXBUFFER_512;
	else if (max_frame <= E1000_RXBUFFER_1024)
		adapter->rx_buffer_len = E1000_RXBUFFER_1024;
	else if (max_frame <= E1000_RXBUFFER_2048)
		adapter->rx_buffer_len = E1000_RXBUFFER_2048;
	else if (max_frame <= E1000_RXBUFFER_4096)
		adapter->rx_buffer_len = E1000_RXBUFFER_4096;
	else if (max_frame <= E1000_RXBUFFER_8192)
		adapter->rx_buffer_len = E1000_RXBUFFER_8192;
	else if (max_frame <= E1000_RXBUFFER_16384)
		adapter->rx_buffer_len = E1000_RXBUFFER_16384;

	/* adjust allocation if LPE protects us, and we aren't using SBP */
	if (!adapter->hw.tbi_compatibility_on &&
	    ((max_frame == MAXIMUM_ETHERNET_FRAME_SIZE) ||
	     (max_frame == MAXIMUM_ETHERNET_VLAN_SIZE)))
		adapter->rx_buffer_len = MAXIMUM_ETHERNET_VLAN_SIZE;

	netdev->mtu = new_mtu;

	if (netif_running(netdev))
		e1000_reinit_locked(adapter);

	adapter->hw.max_frame_size = max_frame;

	return 0;
}

/**
 * e1000_update_stats - Update the board statistics counters
 * @adapter: board private structure
 **/

void
e1000_update_stats(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long flags;
	uint16_t phy_tmp;

#define PHY_IDLE_ERROR_COUNT_MASK 0x00FF

	/*
	 * Prevent stats update while adapter is being reset, or if the pci
	 * connection is down.
	 */
	if (adapter->link_speed == 0)
		return;
	if (pdev->error_state && pdev->error_state != pci_channel_io_normal)
		return;

	spin_lock_irqsave(&adapter->stats_lock, flags);

	/* these counters are modified from e1000_adjust_tbi_stats,
	 * called from the interrupt context, so they must only
	 * be written while holding adapter->stats_lock
	 */

	adapter->stats.crcerrs += E1000_READ_REG(hw, CRCERRS);
	adapter->stats.gprc += E1000_READ_REG(hw, GPRC);
	adapter->stats.gorcl += E1000_READ_REG(hw, GORCL);
	adapter->stats.gorch += E1000_READ_REG(hw, GORCH);
	adapter->stats.bprc += E1000_READ_REG(hw, BPRC);
	adapter->stats.mprc += E1000_READ_REG(hw, MPRC);
	adapter->stats.roc += E1000_READ_REG(hw, ROC);

	if (adapter->hw.mac_type != e1000_ich8lan) {
		adapter->stats.prc64 += E1000_READ_REG(hw, PRC64);
		adapter->stats.prc127 += E1000_READ_REG(hw, PRC127);
		adapter->stats.prc255 += E1000_READ_REG(hw, PRC255);
		adapter->stats.prc511 += E1000_READ_REG(hw, PRC511);
		adapter->stats.prc1023 += E1000_READ_REG(hw, PRC1023);
		adapter->stats.prc1522 += E1000_READ_REG(hw, PRC1522);
	}

	adapter->stats.symerrs += E1000_READ_REG(hw, SYMERRS);
	adapter->stats.mpc += E1000_READ_REG(hw, MPC);
	adapter->stats.scc += E1000_READ_REG(hw, SCC);
	adapter->stats.ecol += E1000_READ_REG(hw, ECOL);
	adapter->stats.mcc += E1000_READ_REG(hw, MCC);
	adapter->stats.latecol += E1000_READ_REG(hw, LATECOL);
	adapter->stats.dc += E1000_READ_REG(hw, DC);
	adapter->stats.sec += E1000_READ_REG(hw, SEC);
	adapter->stats.rlec += E1000_READ_REG(hw, RLEC);
	adapter->stats.xonrxc += E1000_READ_REG(hw, XONRXC);
	adapter->stats.xontxc += E1000_READ_REG(hw, XONTXC);
	adapter->stats.xoffrxc += E1000_READ_REG(hw, XOFFRXC);
	adapter->stats.xofftxc += E1000_READ_REG(hw, XOFFTXC);
	adapter->stats.fcruc += E1000_READ_REG(hw, FCRUC);
	adapter->stats.gptc += E1000_READ_REG(hw, GPTC);
	adapter->stats.gotcl += E1000_READ_REG(hw, GOTCL);
	adapter->stats.gotch += E1000_READ_REG(hw, GOTCH);
	adapter->stats.rnbc += E1000_READ_REG(hw, RNBC);
	adapter->stats.ruc += E1000_READ_REG(hw, RUC);
	adapter->stats.rfc += E1000_READ_REG(hw, RFC);
	adapter->stats.rjc += E1000_READ_REG(hw, RJC);
	adapter->stats.torl += E1000_READ_REG(hw, TORL);
	adapter->stats.torh += E1000_READ_REG(hw, TORH);
	adapter->stats.totl += E1000_READ_REG(hw, TOTL);
	adapter->stats.toth += E1000_READ_REG(hw, TOTH);
	adapter->stats.tpr += E1000_READ_REG(hw, TPR);

	if (adapter->hw.mac_type != e1000_ich8lan) {
		adapter->stats.ptc64 += E1000_READ_REG(hw, PTC64);
		adapter->stats.ptc127 += E1000_READ_REG(hw, PTC127);
		adapter->stats.ptc255 += E1000_READ_REG(hw, PTC255);
		adapter->stats.ptc511 += E1000_READ_REG(hw, PTC511);
		adapter->stats.ptc1023 += E1000_READ_REG(hw, PTC1023);
		adapter->stats.ptc1522 += E1000_READ_REG(hw, PTC1522);
	}

	adapter->stats.mptc += E1000_READ_REG(hw, MPTC);
	adapter->stats.bptc += E1000_READ_REG(hw, BPTC);

	/* used for adaptive IFS */

	hw->tx_packet_delta = E1000_READ_REG(hw, TPT);
	adapter->stats.tpt += hw->tx_packet_delta;
	hw->collision_delta = E1000_READ_REG(hw, COLC);
	adapter->stats.colc += hw->collision_delta;

	if (hw->mac_type >= e1000_82543) {
		adapter->stats.algnerrc += E1000_READ_REG(hw, ALGNERRC);
		adapter->stats.rxerrc += E1000_READ_REG(hw, RXERRC);
		adapter->stats.tncrs += E1000_READ_REG(hw, TNCRS);
		adapter->stats.cexterr += E1000_READ_REG(hw, CEXTERR);
		adapter->stats.tsctc += E1000_READ_REG(hw, TSCTC);
		adapter->stats.tsctfc += E1000_READ_REG(hw, TSCTFC);
	}
	if (hw->mac_type > e1000_82547_rev_2) {
		adapter->stats.iac += E1000_READ_REG(hw, IAC);
		adapter->stats.icrxoc += E1000_READ_REG(hw, ICRXOC);

		if (adapter->hw.mac_type != e1000_ich8lan) {
			adapter->stats.icrxptc += E1000_READ_REG(hw, ICRXPTC);
			adapter->stats.icrxatc += E1000_READ_REG(hw, ICRXATC);
			adapter->stats.ictxptc += E1000_READ_REG(hw, ICTXPTC);
			adapter->stats.ictxatc += E1000_READ_REG(hw, ICTXATC);
			adapter->stats.ictxqec += E1000_READ_REG(hw, ICTXQEC);
			adapter->stats.ictxqmtc += E1000_READ_REG(hw, ICTXQMTC);
			adapter->stats.icrxdmtc += E1000_READ_REG(hw, ICRXDMTC);
		}
	}

	/* Fill out the OS statistics structure */
	adapter->net_stats.rx_packets = adapter->stats.gprc;
	adapter->net_stats.tx_packets = adapter->stats.gptc;
	adapter->net_stats.rx_bytes = adapter->stats.gorcl;
	adapter->net_stats.tx_bytes = adapter->stats.gotcl;
	adapter->net_stats.multicast = adapter->stats.mprc;
	adapter->net_stats.collisions = adapter->stats.colc;

	/* Rx Errors */

	/* RLEC on some newer hardware can be incorrect so build
	* our own version based on RUC and ROC */
	adapter->net_stats.rx_errors = adapter->stats.rxerrc +
		adapter->stats.crcerrs + adapter->stats.algnerrc +
		adapter->stats.ruc + adapter->stats.roc +
		adapter->stats.cexterr;
	adapter->stats.rlerrc = adapter->stats.ruc + adapter->stats.roc;
	adapter->net_stats.rx_length_errors = adapter->stats.rlerrc;
	adapter->net_stats.rx_crc_errors = adapter->stats.crcerrs;
	adapter->net_stats.rx_frame_errors = adapter->stats.algnerrc;
	adapter->net_stats.rx_missed_errors = adapter->stats.mpc;

	/* Tx Errors */
	adapter->stats.txerrc = adapter->stats.ecol + adapter->stats.latecol;
	adapter->net_stats.tx_errors = adapter->stats.txerrc;
	adapter->net_stats.tx_aborted_errors = adapter->stats.ecol;
	adapter->net_stats.tx_window_errors = adapter->stats.latecol;
	adapter->net_stats.tx_carrier_errors = adapter->stats.tncrs;

	/* Tx Dropped needs to be maintained elsewhere */

	/* Phy Stats */
	if (hw->media_type == e1000_media_type_copper) {
		if ((adapter->link_speed == SPEED_1000) &&
		   (!e1000_read_phy_reg(hw, PHY_1000T_STATUS, &phy_tmp))) {
			phy_tmp &= PHY_IDLE_ERROR_COUNT_MASK;
			adapter->phy_stats.idle_errors += phy_tmp;
		}

		if ((hw->mac_type <= e1000_82546) &&
		   (hw->phy_type == e1000_phy_m88) &&
		   !e1000_read_phy_reg(hw, M88E1000_RX_ERR_CNTR, &phy_tmp))
			adapter->phy_stats.receive_errors += phy_tmp;
	}

	spin_unlock_irqrestore(&adapter->stats_lock, flags);
}

/**
 * e1000_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/

static irqreturn_t
e1000_intr(int irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	uint32_t rctl, icr = E1000_READ_REG(hw, ICR);
#ifndef CONFIG_E1000_NAPI
	int i;
#else
	/* Interrupt Auto-Mask...upon reading ICR,
	 * interrupts are masked.  No need for the
	 * IMC write, but it does mean we should
	 * account for it ASAP. */
	if (likely(hw->mac_type >= e1000_82571))
		atomic_inc(&adapter->irq_sem);
#endif

	if (unlikely(!icr)) {
#ifdef CONFIG_E1000_NAPI
		if (hw->mac_type >= e1000_82571)
			e1000_irq_enable(adapter);
#endif
		return IRQ_NONE;  /* Not our interrupt */
	}

	if (unlikely(icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC))) {
		hw->get_link_status = 1;
		/* 80003ES2LAN workaround--
		 * For packet buffer work-around on link down event;
		 * disable receives here in the ISR and
		 * reset adapter in watchdog
		 */
		if (netif_carrier_ok(netdev) &&
		    (adapter->hw.mac_type == e1000_80003es2lan)) {
			/* disable receives */
			rctl = E1000_READ_REG(hw, RCTL);
			E1000_WRITE_REG(hw, RCTL, rctl & ~E1000_RCTL_EN);
		}
		/* guard against interrupt when we're going down */
		if (!test_bit(__E1000_DOWN, &adapter->flags))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

#ifdef CONFIG_E1000_NAPI
	if (unlikely(hw->mac_type < e1000_82571)) {
		atomic_inc(&adapter->irq_sem);
		E1000_WRITE_REG(hw, IMC, ~0);
		E1000_WRITE_FLUSH(hw);
	}
	if (likely(netif_rx_schedule_prep(netdev)))
		__netif_rx_schedule(netdev);
	else
		/* this really should not happen! if it does it is basically a
		 * bug, but not a hard error, so enable ints and continue */
		e1000_irq_enable(adapter);
#else
	/* Writing IMC and IMS is needed for 82547.
	 * Due to Hub Link bus being occupied, an interrupt
	 * de-assertion message is not able to be sent.
	 * When an interrupt assertion message is generated later,
	 * two messages are re-ordered and sent out.
	 * That causes APIC to think 82547 is in de-assertion
	 * state, while 82547 is in assertion state, resulting
	 * in dead lock. Writing IMC forces 82547 into
	 * de-assertion state.
	 */
	if (hw->mac_type == e1000_82547 || hw->mac_type == e1000_82547_rev_2) {
		atomic_inc(&adapter->irq_sem);
		E1000_WRITE_REG(hw, IMC, ~0);
	}

	for (i = 0; i < E1000_MAX_INTR; i++)
		if (unlikely(!adapter->clean_rx(adapter, adapter->rx_ring) &
		   !e1000_clean_tx_irq(adapter, adapter->tx_ring)))
			break;

	if (hw->mac_type == e1000_82547 || hw->mac_type == e1000_82547_rev_2)
		e1000_irq_enable(adapter);

#endif
	return IRQ_HANDLED;
}

#ifdef CONFIG_E1000_NAPI
/**
 * e1000_clean - NAPI Rx polling callback
 * @adapter: board private structure
 **/

static int
e1000_clean(struct net_device *poll_dev, int *budget)
{
	struct e1000_adapter *adapter;
	int work_to_do = min(*budget, poll_dev->quota);
	int tx_cleaned = 0, work_done = 0;

	/* Must NOT use netdev_priv macro here. */
	adapter = poll_dev->priv;

	/* Keep link state information with original netdev */
	if (!netif_carrier_ok(poll_dev))
		goto quit_polling;

	/* e1000_clean is called per-cpu.  This lock protects
	 * tx_ring[0] from being cleaned by multiple cpus
	 * simultaneously.  A failure obtaining the lock means
	 * tx_ring[0] is currently being cleaned anyway. */
	if (spin_trylock(&adapter->tx_queue_lock)) {
		tx_cleaned = e1000_clean_tx_irq(adapter,
		                                &adapter->tx_ring[0]);
		spin_unlock(&adapter->tx_queue_lock);
	}

	adapter->clean_rx(adapter, &adapter->rx_ring[0],
	                  &work_done, work_to_do);

	*budget -= work_done;
	poll_dev->quota -= work_done;

	/* If no Tx and not enough Rx work done, exit the polling mode */
	if ((!tx_cleaned && (work_done == 0)) ||
	   !netif_running(poll_dev)) {
quit_polling:
		netif_rx_complete(poll_dev);
		e1000_irq_enable(adapter);
		return 0;
	}

	return 1;
}

#endif
/**
 * e1000_clean_tx_irq - Reclaim resources after transmit completes
 * @adapter: board private structure
 **/

static boolean_t
e1000_clean_tx_irq(struct e1000_adapter *adapter,
                   struct e1000_tx_ring *tx_ring)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_tx_desc *tx_desc, *eop_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i, eop;
#ifdef CONFIG_E1000_NAPI
	unsigned int count = 0;
#endif
	boolean_t cleaned = FALSE;

	i = tx_ring->next_to_clean;
	eop = tx_ring->buffer_info[i].next_to_watch;
	eop_desc = E1000_TX_DESC(*tx_ring, eop);

	while (eop_desc->upper.data & cpu_to_le32(E1000_TXD_STAT_DD)) {
		for (cleaned = FALSE; !cleaned; ) {
			tx_desc = E1000_TX_DESC(*tx_ring, i);
			buffer_info = &tx_ring->buffer_info[i];
			cleaned = (i == eop);

			e1000_unmap_and_free_tx_resource(adapter, buffer_info);
			memset(tx_desc, 0, sizeof(struct e1000_tx_desc));

			if (unlikely(++i == tx_ring->count)) i = 0;
		}

		eop = tx_ring->buffer_info[i].next_to_watch;
		eop_desc = E1000_TX_DESC(*tx_ring, eop);
#ifdef CONFIG_E1000_NAPI
#define E1000_TX_WEIGHT 64
		/* weight of a sort for tx, to avoid endless transmit cleanup */
		if (count++ == E1000_TX_WEIGHT) break;
#endif
	}

	tx_ring->next_to_clean = i;

#define TX_WAKE_THRESHOLD 32
	if (unlikely(cleaned && netif_carrier_ok(netdev) &&
		     E1000_DESC_UNUSED(tx_ring) >= TX_WAKE_THRESHOLD)) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (netif_queue_stopped(netdev))
			netif_wake_queue(netdev);
	}

	if (adapter->detect_tx_hung) {
		/* Detect a transmit hang in hardware, this serializes the
		 * check with the clearing of time_stamp and movement of i */
		adapter->detect_tx_hung = FALSE;
		if (tx_ring->buffer_info[eop].dma &&
		    time_after(jiffies, tx_ring->buffer_info[eop].time_stamp +
		               (adapter->tx_timeout_factor * HZ))
		    && !(E1000_READ_REG(&adapter->hw, STATUS) &
		         E1000_STATUS_TXOFF)) {

			/* detected Tx unit hang */
			DPRINTK(DRV, ERR, "Detected Tx Unit Hang\n"
					"  Tx Queue             <%lu>\n"
					"  TDH                  <%x>\n"
					"  TDT                  <%x>\n"
					"  next_to_use          <%x>\n"
					"  next_to_clean        <%x>\n"
					"buffer_info[next_to_clean]\n"
					"  time_stamp           <%lx>\n"
					"  next_to_watch        <%x>\n"
					"  jiffies              <%lx>\n"
					"  next_to_watch.status <%x>\n",
				(unsigned long)((tx_ring - adapter->tx_ring) /
					sizeof(struct e1000_tx_ring)),
				readl(adapter->hw.hw_addr + tx_ring->tdh),
				readl(adapter->hw.hw_addr + tx_ring->tdt),
				tx_ring->next_to_use,
				tx_ring->next_to_clean,
				tx_ring->buffer_info[eop].time_stamp,
				eop,
				jiffies,
				eop_desc->upper.fields.status);
			netif_stop_queue(netdev);
		}
	}
	return cleaned;
}

/**
 * e1000_rx_checksum - Receive Checksum Offload for 82543
 * @adapter:     board private structure
 * @status_err:  receive descriptor status and error fields
 * @csum:        receive descriptor csum field
 * @sk_buff:     socket buffer with received data
 **/

static void
e1000_rx_checksum(struct e1000_adapter *adapter,
		  uint32_t status_err, uint32_t csum,
		  struct sk_buff *skb)
{
	uint16_t status = (uint16_t)status_err;
	uint8_t errors = (uint8_t)(status_err >> 24);
	skb->ip_summed = CHECKSUM_NONE;

	/* 82543 or newer only */
	if (unlikely(adapter->hw.mac_type < e1000_82543)) return;
	/* Ignore Checksum bit is set */
	if (unlikely(status & E1000_RXD_STAT_IXSM)) return;
	/* TCP/UDP checksum error bit is set */
	if (unlikely(errors & E1000_RXD_ERR_TCPE)) {
		/* let the stack verify checksum errors */
		adapter->hw_csum_err++;
		return;
	}
	/* TCP/UDP Checksum has not been calculated */
	if (adapter->hw.mac_type <= e1000_82547_rev_2) {
		if (!(status & E1000_RXD_STAT_TCPCS))
			return;
	} else {
		if (!(status & (E1000_RXD_STAT_TCPCS | E1000_RXD_STAT_UDPCS)))
			return;
	}
	/* It must be a TCP or UDP packet with a valid checksum */
	if (likely(status & E1000_RXD_STAT_TCPCS)) {
		/* TCP checksum is good */
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else if (adapter->hw.mac_type > e1000_82547_rev_2) {
		/* IP fragment with UDP payload */
		/* Hardware complements the payload checksum, so we undo it
		 * and then put the value in host order for further stack use.
		 */
		csum = ntohl(csum ^ 0xFFFF);
		skb->csum = csum;
		skb->ip_summed = CHECKSUM_COMPLETE;
	}
	adapter->hw_csum_good++;
}

/**
 * e1000_clean_rx_irq - Send received data up the network stack; legacy
 * @adapter: board private structure
 **/

static boolean_t
#ifdef CONFIG_E1000_NAPI
e1000_clean_rx_irq(struct e1000_adapter *adapter,
                   struct e1000_rx_ring *rx_ring,
                   int *work_done, int work_to_do)
#else
e1000_clean_rx_irq(struct e1000_adapter *adapter,
                   struct e1000_rx_ring *rx_ring)
#endif
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_rx_desc *rx_desc, *next_rxd;
	struct e1000_buffer *buffer_info, *next_buffer;
	unsigned long flags;
	uint32_t length;
	uint8_t last_byte;
	unsigned int i;
	int cleaned_count = 0;
	boolean_t cleaned = FALSE;

	i = rx_ring->next_to_clean;
	rx_desc = E1000_RX_DESC(*rx_ring, i);
	buffer_info = &rx_ring->buffer_info[i];

	while (rx_desc->status & E1000_RXD_STAT_DD) {
		struct sk_buff *skb;
		u8 status;

#ifdef CONFIG_E1000_NAPI
		if (*work_done >= work_to_do)
			break;
		(*work_done)++;
#endif
		status = rx_desc->status;
		skb = buffer_info->skb;
		buffer_info->skb = NULL;

		prefetch(skb->data - NET_IP_ALIGN);

		if (++i == rx_ring->count) i = 0;
		next_rxd = E1000_RX_DESC(*rx_ring, i);
		prefetch(next_rxd);

		next_buffer = &rx_ring->buffer_info[i];

		cleaned = TRUE;
		cleaned_count++;
		pci_unmap_single(pdev,
		                 buffer_info->dma,
		                 buffer_info->length,
		                 PCI_DMA_FROMDEVICE);

		length = le16_to_cpu(rx_desc->length);

		if (unlikely(!(status & E1000_RXD_STAT_EOP))) {
			/* All receives must fit into a single buffer */
			E1000_DBG("%s: Receive packet consumed multiple"
				  " buffers\n", netdev->name);
			/* recycle */
			buffer_info->skb = skb;
			goto next_desc;
		}

		if (unlikely(rx_desc->errors & E1000_RXD_ERR_FRAME_ERR_MASK)) {
			last_byte = *(skb->data + length - 1);
			if (TBI_ACCEPT(&adapter->hw, status,
			              rx_desc->errors, length, last_byte)) {
				spin_lock_irqsave(&adapter->stats_lock, flags);
				e1000_tbi_adjust_stats(&adapter->hw,
				                       &adapter->stats,
				                       length, skb->data);
				spin_unlock_irqrestore(&adapter->stats_lock,
				                       flags);
				length--;
			} else {
				/* recycle */
				buffer_info->skb = skb;
				goto next_desc;
			}
		}

		/* adjust length to remove Ethernet CRC, this must be
		 * done after the TBI_ACCEPT workaround above */
		length -= 4;

		/* code added for copybreak, this should improve
		 * performance for small packets with large amounts
		 * of reassembly being done in the stack */
#define E1000_CB_LENGTH 256
		if (length < E1000_CB_LENGTH) {
			struct sk_buff *new_skb =
			    netdev_alloc_skb(netdev, length + NET_IP_ALIGN);
			if (new_skb) {
				skb_reserve(new_skb, NET_IP_ALIGN);
				memcpy(new_skb->data - NET_IP_ALIGN,
				       skb->data - NET_IP_ALIGN,
				       length + NET_IP_ALIGN);
				/* save the skb in buffer_info as good */
				buffer_info->skb = skb;
				skb = new_skb;
				skb_put(skb, length);
			}
		} else
			skb_put(skb, length);

		/* end copybreak code */

		/* Receive Checksum Offload */
		e1000_rx_checksum(adapter,
				  (uint32_t)(status) |
				  ((uint32_t)(rx_desc->errors) << 24),
				  le16_to_cpu(rx_desc->csum), skb);

		skb->protocol = eth_type_trans(skb, netdev);
#ifdef CONFIG_E1000_NAPI
		if (unlikely(adapter->vlgrp &&
			    (status & E1000_RXD_STAT_VP))) {
			vlan_hwaccel_receive_skb(skb, adapter->vlgrp,
						 le16_to_cpu(rx_desc->special) &
						 E1000_RXD_SPC_VLAN_MASK);
		} else {
			netif_receive_skb(skb);
		}
#else /* CONFIG_E1000_NAPI */
		if (unlikely(adapter->vlgrp &&
			    (status & E1000_RXD_STAT_VP))) {
			vlan_hwaccel_rx(skb, adapter->vlgrp,
					le16_to_cpu(rx_desc->special) &
					E1000_RXD_SPC_VLAN_MASK);
		} else {
			netif_rx(skb);
		}
#endif /* CONFIG_E1000_NAPI */
		netdev->last_rx = jiffies;

next_desc:
		rx_desc->status = 0;

		/* return some buffers to hardware, one at a time is too slow */
		if (unlikely(cleaned_count >= E1000_RX_BUFFER_WRITE)) {
			adapter->alloc_rx_buf(adapter, rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		buffer_info = next_buffer;
	}
	rx_ring->next_to_clean = i;

	cleaned_count = E1000_DESC_UNUSED(rx_ring);
	if (cleaned_count)
		adapter->alloc_rx_buf(adapter, rx_ring, cleaned_count);

	return cleaned;
}

/**
 * e1000_clean_rx_irq_ps - Send received data up the network stack; packet split
 * @adapter: board private structure
 **/

static boolean_t
#ifdef CONFIG_E1000_NAPI
e1000_clean_rx_irq_ps(struct e1000_adapter *adapter,
                      struct e1000_rx_ring *rx_ring,
                      int *work_done, int work_to_do)
#else
e1000_clean_rx_irq_ps(struct e1000_adapter *adapter,
                      struct e1000_rx_ring *rx_ring)
#endif
{
	union e1000_rx_desc_packet_split *rx_desc, *next_rxd;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_buffer *buffer_info, *next_buffer;
	struct e1000_ps_page *ps_page;
	struct e1000_ps_page_dma *ps_page_dma;
	struct sk_buff *skb;
	unsigned int i, j;
	uint32_t length, staterr;
	int cleaned_count = 0;
	boolean_t cleaned = FALSE;

	i = rx_ring->next_to_clean;
	rx_desc = E1000_RX_DESC_PS(*rx_ring, i);
	staterr = le32_to_cpu(rx_desc->wb.middle.status_error);
	buffer_info = &rx_ring->buffer_info[i];

	while (staterr & E1000_RXD_STAT_DD) {
		ps_page = &rx_ring->ps_page[i];
		ps_page_dma = &rx_ring->ps_page_dma[i];
#ifdef CONFIG_E1000_NAPI
		if (unlikely(*work_done >= work_to_do))
			break;
		(*work_done)++;
#endif
		skb = buffer_info->skb;

		/* in the packet split case this is header only */
		prefetch(skb->data - NET_IP_ALIGN);

		if (++i == rx_ring->count) i = 0;
		next_rxd = E1000_RX_DESC_PS(*rx_ring, i);
		prefetch(next_rxd);

		next_buffer = &rx_ring->buffer_info[i];

		cleaned = TRUE;
		cleaned_count++;
		pci_unmap_single(pdev, buffer_info->dma,
				 buffer_info->length,
				 PCI_DMA_FROMDEVICE);

		if (unlikely(!(staterr & E1000_RXD_STAT_EOP))) {
			E1000_DBG("%s: Packet Split buffers didn't pick up"
				  " the full packet\n", netdev->name);
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		if (unlikely(staterr & E1000_RXDEXT_ERR_FRAME_ERR_MASK)) {
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		length = le16_to_cpu(rx_desc->wb.middle.length0);

		if (unlikely(!length)) {
			E1000_DBG("%s: Last part of the packet spanning"
				  " multiple descriptors\n", netdev->name);
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		/* Good Receive */
		skb_put(skb, length);

		{
		/* this looks ugly, but it seems compiler issues make it
		   more efficient than reusing j */
		int l1 = le16_to_cpu(rx_desc->wb.upper.length[0]);

		/* page alloc/put takes too long and effects small packet
		 * throughput, so unsplit small packets and save the alloc/put*/
		if (l1 && ((length + l1) <= adapter->rx_ps_bsize0)) {
			u8 *vaddr;
			/* there is no documentation about how to call
			 * kmap_atomic, so we can't hold the mapping
			 * very long */
			pci_dma_sync_single_for_cpu(pdev,
				ps_page_dma->ps_page_dma[0],
				PAGE_SIZE,
				PCI_DMA_FROMDEVICE);
			vaddr = kmap_atomic(ps_page->ps_page[0],
			                    KM_SKB_DATA_SOFTIRQ);
			memcpy(skb->tail, vaddr, l1);
			kunmap_atomic(vaddr, KM_SKB_DATA_SOFTIRQ);
			pci_dma_sync_single_for_device(pdev,
				ps_page_dma->ps_page_dma[0],
				PAGE_SIZE, PCI_DMA_FROMDEVICE);
			/* remove the CRC */
			l1 -= 4;
			skb_put(skb, l1);
			goto copydone;
		} /* if */
		}

		for (j = 0; j < adapter->rx_ps_pages; j++) {
			if (!(length= le16_to_cpu(rx_desc->wb.upper.length[j])))
				break;
			pci_unmap_page(pdev, ps_page_dma->ps_page_dma[j],
					PAGE_SIZE, PCI_DMA_FROMDEVICE);
			ps_page_dma->ps_page_dma[j] = 0;
			skb_fill_page_desc(skb, j, ps_page->ps_page[j], 0,
			                   length);
			ps_page->ps_page[j] = NULL;
			skb->len += length;
			skb->data_len += length;
			skb->truesize += length;
		}

		/* strip the ethernet crc, problem is we're using pages now so
		 * this whole operation can get a little cpu intensive */
		pskb_trim(skb, skb->len - 4);

copydone:
		e1000_rx_checksum(adapter, staterr,
				  le16_to_cpu(rx_desc->wb.lower.hi_dword.csum_ip.csum), skb);
		skb->protocol = eth_type_trans(skb, netdev);

		if (likely(rx_desc->wb.upper.header_status &
			   cpu_to_le16(E1000_RXDPS_HDRSTAT_HDRSP)))
			adapter->rx_hdr_split++;
#ifdef CONFIG_E1000_NAPI
		if (unlikely(adapter->vlgrp && (staterr & E1000_RXD_STAT_VP))) {
			vlan_hwaccel_receive_skb(skb, adapter->vlgrp,
				le16_to_cpu(rx_desc->wb.middle.vlan) &
				E1000_RXD_SPC_VLAN_MASK);
		} else {
			netif_receive_skb(skb);
		}
#else /* CONFIG_E1000_NAPI */
		if (unlikely(adapter->vlgrp && (staterr & E1000_RXD_STAT_VP))) {
			vlan_hwaccel_rx(skb, adapter->vlgrp,
				le16_to_cpu(rx_desc->wb.middle.vlan) &
				E1000_RXD_SPC_VLAN_MASK);
		} else {
			netif_rx(skb);
		}
#endif /* CONFIG_E1000_NAPI */
		netdev->last_rx = jiffies;

next_desc:
		rx_desc->wb.middle.status_error &= cpu_to_le32(~0xFF);
		buffer_info->skb = NULL;

		/* return some buffers to hardware, one at a time is too slow */
		if (unlikely(cleaned_count >= E1000_RX_BUFFER_WRITE)) {
			adapter->alloc_rx_buf(adapter, rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		buffer_info = next_buffer;

		staterr = le32_to_cpu(rx_desc->wb.middle.status_error);
	}
	rx_ring->next_to_clean = i;

	cleaned_count = E1000_DESC_UNUSED(rx_ring);
	if (cleaned_count)
		adapter->alloc_rx_buf(adapter, rx_ring, cleaned_count);

	return cleaned;
}

/**
 * e1000_alloc_rx_buffers - Replace used receive buffers; legacy & extended
 * @adapter: address of board private structure
 **/

static void
e1000_alloc_rx_buffers(struct e1000_adapter *adapter,
                       struct e1000_rx_ring *rx_ring,
		       int cleaned_count)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_rx_desc *rx_desc;
	struct e1000_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int bufsz = adapter->rx_buffer_len + NET_IP_ALIGN;

	i = rx_ring->next_to_use;
	buffer_info = &rx_ring->buffer_info[i];

	while (cleaned_count--) {
		skb = buffer_info->skb;
		if (skb) {
			skb_trim(skb, 0);
			goto map_skb;
		}

		skb = netdev_alloc_skb(netdev, bufsz);
		if (unlikely(!skb)) {
			/* Better luck next round */
			adapter->alloc_rx_buff_failed++;
			break;
		}

		/* Fix for errata 23, can't cross 64kB boundary */
		if (!e1000_check_64k_bound(adapter, skb->data, bufsz)) {
			struct sk_buff *oldskb = skb;
			DPRINTK(RX_ERR, ERR, "skb align check failed: %u bytes "
					     "at %p\n", bufsz, skb->data);
			/* Try again, without freeing the previous */
			skb = netdev_alloc_skb(netdev, bufsz);
			/* Failed allocation, critical failure */
			if (!skb) {
				dev_kfree_skb(oldskb);
				break;
			}

			if (!e1000_check_64k_bound(adapter, skb->data, bufsz)) {
				/* give up */
				dev_kfree_skb(skb);
				dev_kfree_skb(oldskb);
				break; /* while !buffer_info->skb */
			}

			/* Use new allocation */
			dev_kfree_skb(oldskb);
		}
		/* Make buffer alignment 2 beyond a 16 byte boundary
		 * this will result in a 16 byte aligned IP header after
		 * the 14 byte MAC header is removed
		 */
		skb_reserve(skb, NET_IP_ALIGN);

		buffer_info->skb = skb;
		buffer_info->length = adapter->rx_buffer_len;
map_skb:
		buffer_info->dma = pci_map_single(pdev,
						  skb->data,
						  adapter->rx_buffer_len,
						  PCI_DMA_FROMDEVICE);

		/* Fix for errata 23, can't cross 64kB boundary */
		if (!e1000_check_64k_bound(adapter,
					(void *)(unsigned long)buffer_info->dma,
					adapter->rx_buffer_len)) {
			DPRINTK(RX_ERR, ERR,
				"dma align check failed: %u bytes at %p\n",
				adapter->rx_buffer_len,
				(void *)(unsigned long)buffer_info->dma);
			dev_kfree_skb(skb);
			buffer_info->skb = NULL;

			pci_unmap_single(pdev, buffer_info->dma,
					 adapter->rx_buffer_len,
					 PCI_DMA_FROMDEVICE);

			break; /* while !buffer_info->skb */
		}
		rx_desc = E1000_RX_DESC(*rx_ring, i);
		rx_desc->buffer_addr = cpu_to_le64(buffer_info->dma);

		if (unlikely(++i == rx_ring->count))
			i = 0;
		buffer_info = &rx_ring->buffer_info[i];
	}

	if (likely(rx_ring->next_to_use != i)) {
		rx_ring->next_to_use = i;
		if (unlikely(i-- == 0))
			i = (rx_ring->count - 1);

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64). */
		wmb();
		writel(i, adapter->hw.hw_addr + rx_ring->rdt);
	}
}

/**
 * e1000_alloc_rx_buffers_ps - Replace used receive buffers; packet split
 * @adapter: address of board private structure
 **/

static void
e1000_alloc_rx_buffers_ps(struct e1000_adapter *adapter,
                          struct e1000_rx_ring *rx_ring,
			  int cleaned_count)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union e1000_rx_desc_packet_split *rx_desc;
	struct e1000_buffer *buffer_info;
	struct e1000_ps_page *ps_page;
	struct e1000_ps_page_dma *ps_page_dma;
	struct sk_buff *skb;
	unsigned int i, j;

	i = rx_ring->next_to_use;
	buffer_info = &rx_ring->buffer_info[i];
	ps_page = &rx_ring->ps_page[i];
	ps_page_dma = &rx_ring->ps_page_dma[i];

	while (cleaned_count--) {
		rx_desc = E1000_RX_DESC_PS(*rx_ring, i);

		for (j = 0; j < PS_PAGE_BUFFERS; j++) {
			if (j < adapter->rx_ps_pages) {
				if (likely(!ps_page->ps_page[j])) {
					ps_page->ps_page[j] =
						alloc_page(GFP_ATOMIC);
					if (unlikely(!ps_page->ps_page[j])) {
						adapter->alloc_rx_buff_failed++;
						goto no_buffers;
					}
					ps_page_dma->ps_page_dma[j] =
						pci_map_page(pdev,
							    ps_page->ps_page[j],
							    0, PAGE_SIZE,
							    PCI_DMA_FROMDEVICE);
				}
				/* Refresh the desc even if buffer_addrs didn't
				 * change because each write-back erases
				 * this info.
				 */
				rx_desc->read.buffer_addr[j+1] =
				     cpu_to_le64(ps_page_dma->ps_page_dma[j]);
			} else
				rx_desc->read.buffer_addr[j+1] = ~0;
		}

		skb = netdev_alloc_skb(netdev,
		                       adapter->rx_ps_bsize0 + NET_IP_ALIGN);

		if (unlikely(!skb)) {
			adapter->alloc_rx_buff_failed++;
			break;
		}

		/* Make buffer alignment 2 beyond a 16 byte boundary
		 * this will result in a 16 byte aligned IP header after
		 * the 14 byte MAC header is removed
		 */
		skb_reserve(skb, NET_IP_ALIGN);

		buffer_info->skb = skb;
		buffer_info->length = adapter->rx_ps_bsize0;
		buffer_info->dma = pci_map_single(pdev, skb->data,
						  adapter->rx_ps_bsize0,
						  PCI_DMA_FROMDEVICE);

		rx_desc->read.buffer_addr[0] = cpu_to_le64(buffer_info->dma);

		if (unlikely(++i == rx_ring->count)) i = 0;
		buffer_info = &rx_ring->buffer_info[i];
		ps_page = &rx_ring->ps_page[i];
		ps_page_dma = &rx_ring->ps_page_dma[i];
	}

no_buffers:
	if (likely(rx_ring->next_to_use != i)) {
		rx_ring->next_to_use = i;
		if (unlikely(i-- == 0)) i = (rx_ring->count - 1);

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64). */
		wmb();
		/* Hardware increments by 16 bytes, but packet split
		 * descriptors are 32 bytes...so we increment tail
		 * twice as much.
		 */
		writel(i<<1, adapter->hw.hw_addr + rx_ring->rdt);
	}
}

/**
 * e1000_smartspeed - Workaround for SmartSpeed on 82541 and 82547 controllers.
 * @adapter:
 **/

static void
e1000_smartspeed(struct e1000_adapter *adapter)
{
	uint16_t phy_status;
	uint16_t phy_ctrl;

	if ((adapter->hw.phy_type != e1000_phy_igp) || !adapter->hw.autoneg ||
	   !(adapter->hw.autoneg_advertised & ADVERTISE_1000_FULL))
		return;

	if (adapter->smartspeed == 0) {
		/* If Master/Slave config fault is asserted twice,
		 * we assume back-to-back */
		e1000_read_phy_reg(&adapter->hw, PHY_1000T_STATUS, &phy_status);
		if (!(phy_status & SR_1000T_MS_CONFIG_FAULT)) return;
		e1000_read_phy_reg(&adapter->hw, PHY_1000T_STATUS, &phy_status);
		if (!(phy_status & SR_1000T_MS_CONFIG_FAULT)) return;
		e1000_read_phy_reg(&adapter->hw, PHY_1000T_CTRL, &phy_ctrl);
		if (phy_ctrl & CR_1000T_MS_ENABLE) {
			phy_ctrl &= ~CR_1000T_MS_ENABLE;
			e1000_write_phy_reg(&adapter->hw, PHY_1000T_CTRL,
					    phy_ctrl);
			adapter->smartspeed++;
			if (!e1000_phy_setup_autoneg(&adapter->hw) &&
			   !e1000_read_phy_reg(&adapter->hw, PHY_CTRL,
				   	       &phy_ctrl)) {
				phy_ctrl |= (MII_CR_AUTO_NEG_EN |
					     MII_CR_RESTART_AUTO_NEG);
				e1000_write_phy_reg(&adapter->hw, PHY_CTRL,
						    phy_ctrl);
			}
		}
		return;
	} else if (adapter->smartspeed == E1000_SMARTSPEED_DOWNSHIFT) {
		/* If still no link, perhaps using 2/3 pair cable */
		e1000_read_phy_reg(&adapter->hw, PHY_1000T_CTRL, &phy_ctrl);
		phy_ctrl |= CR_1000T_MS_ENABLE;
		e1000_write_phy_reg(&adapter->hw, PHY_1000T_CTRL, phy_ctrl);
		if (!e1000_phy_setup_autoneg(&adapter->hw) &&
		   !e1000_read_phy_reg(&adapter->hw, PHY_CTRL, &phy_ctrl)) {
			phy_ctrl |= (MII_CR_AUTO_NEG_EN |
				     MII_CR_RESTART_AUTO_NEG);
			e1000_write_phy_reg(&adapter->hw, PHY_CTRL, phy_ctrl);
		}
	}
	/* Restart process after E1000_SMARTSPEED_MAX iterations */
	if (adapter->smartspeed++ == E1000_SMARTSPEED_MAX)
		adapter->smartspeed = 0;
}

/**
 * e1000_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 **/

static int
e1000_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return e1000_mii_ioctl(netdev, ifr, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * e1000_mii_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 **/

static int
e1000_mii_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct mii_ioctl_data *data = if_mii(ifr);
	int retval;
	uint16_t mii_reg;
	uint16_t spddplx;
	unsigned long flags;

	if (adapter->hw.media_type != e1000_media_type_copper)
		return -EOPNOTSUPP;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = adapter->hw.phy_addr;
		break;
	case SIOCGMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		spin_lock_irqsave(&adapter->stats_lock, flags);
		if (e1000_read_phy_reg(&adapter->hw, data->reg_num & 0x1F,
				   &data->val_out)) {
			spin_unlock_irqrestore(&adapter->stats_lock, flags);
			return -EIO;
		}
		spin_unlock_irqrestore(&adapter->stats_lock, flags);
		break;
	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (data->reg_num & ~(0x1F))
			return -EFAULT;
		mii_reg = data->val_in;
		spin_lock_irqsave(&adapter->stats_lock, flags);
		if (e1000_write_phy_reg(&adapter->hw, data->reg_num,
					mii_reg)) {
			spin_unlock_irqrestore(&adapter->stats_lock, flags);
			return -EIO;
		}
		if (adapter->hw.media_type == e1000_media_type_copper) {
			switch (data->reg_num) {
			case PHY_CTRL:
				if (mii_reg & MII_CR_POWER_DOWN)
					break;
				if (mii_reg & MII_CR_AUTO_NEG_EN) {
					adapter->hw.autoneg = 1;
					adapter->hw.autoneg_advertised = 0x2F;
				} else {
					if (mii_reg & 0x40)
						spddplx = SPEED_1000;
					else if (mii_reg & 0x2000)
						spddplx = SPEED_100;
					else
						spddplx = SPEED_10;
					spddplx += (mii_reg & 0x100)
						   ? DUPLEX_FULL :
						   DUPLEX_HALF;
					retval = e1000_set_spd_dplx(adapter,
								    spddplx);
					if (retval) {
						spin_unlock_irqrestore(
							&adapter->stats_lock,
							flags);
						return retval;
					}
				}
				if (netif_running(adapter->netdev))
					e1000_reinit_locked(adapter);
				else
					e1000_reset(adapter);
				break;
			case M88E1000_PHY_SPEC_CTRL:
			case M88E1000_EXT_PHY_SPEC_CTRL:
				if (e1000_phy_reset(&adapter->hw)) {
					spin_unlock_irqrestore(
						&adapter->stats_lock, flags);
					return -EIO;
				}
				break;
			}
		} else {
			switch (data->reg_num) {
			case PHY_CTRL:
				if (mii_reg & MII_CR_POWER_DOWN)
					break;
				if (netif_running(adapter->netdev))
					e1000_reinit_locked(adapter);
				else
					e1000_reset(adapter);
				break;
			}
		}
		spin_unlock_irqrestore(&adapter->stats_lock, flags);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return E1000_SUCCESS;
}

void
e1000_pci_set_mwi(struct e1000_hw *hw)
{
	struct e1000_adapter *adapter = hw->back;
	int ret_val = pci_set_mwi(adapter->pdev);

	if (ret_val)
		DPRINTK(PROBE, ERR, "Error in setting MWI\n");
}

void
e1000_pci_clear_mwi(struct e1000_hw *hw)
{
	struct e1000_adapter *adapter = hw->back;

	pci_clear_mwi(adapter->pdev);
}

void
e1000_read_pci_cfg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	struct e1000_adapter *adapter = hw->back;

	pci_read_config_word(adapter->pdev, reg, value);
}

void
e1000_write_pci_cfg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	struct e1000_adapter *adapter = hw->back;

	pci_write_config_word(adapter->pdev, reg, *value);
}

int32_t
e1000_read_pcie_cap_reg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
    struct e1000_adapter *adapter = hw->back;
    uint16_t cap_offset;

    cap_offset = pci_find_capability(adapter->pdev, PCI_CAP_ID_EXP);
    if (!cap_offset)
        return -E1000_ERR_CONFIG;

    pci_read_config_word(adapter->pdev, cap_offset + reg, value);

    return E1000_SUCCESS;
}

void
e1000_io_write(struct e1000_hw *hw, unsigned long port, uint32_t value)
{
	outl(value, port);
}

static void
e1000_vlan_rx_register(struct net_device *netdev, struct vlan_group *grp)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	uint32_t ctrl, rctl;

	e1000_irq_disable(adapter);
	adapter->vlgrp = grp;

	if (grp) {
		/* enable VLAN tag insert/strip */
		ctrl = E1000_READ_REG(&adapter->hw, CTRL);
		ctrl |= E1000_CTRL_VME;
		E1000_WRITE_REG(&adapter->hw, CTRL, ctrl);

		if (adapter->hw.mac_type != e1000_ich8lan) {
			/* enable VLAN receive filtering */
			rctl = E1000_READ_REG(&adapter->hw, RCTL);
			rctl |= E1000_RCTL_VFE;
			rctl &= ~E1000_RCTL_CFIEN;
			E1000_WRITE_REG(&adapter->hw, RCTL, rctl);
			e1000_update_mng_vlan(adapter);
		}
	} else {
		/* disable VLAN tag insert/strip */
		ctrl = E1000_READ_REG(&adapter->hw, CTRL);
		ctrl &= ~E1000_CTRL_VME;
		E1000_WRITE_REG(&adapter->hw, CTRL, ctrl);

		if (adapter->hw.mac_type != e1000_ich8lan) {
			/* disable VLAN filtering */
			rctl = E1000_READ_REG(&adapter->hw, RCTL);
			rctl &= ~E1000_RCTL_VFE;
			E1000_WRITE_REG(&adapter->hw, RCTL, rctl);
			if (adapter->mng_vlan_id !=
			    (uint16_t)E1000_MNG_VLAN_NONE) {
				e1000_vlan_rx_kill_vid(netdev,
				                       adapter->mng_vlan_id);
				adapter->mng_vlan_id = E1000_MNG_VLAN_NONE;
			}
		}
	}

	e1000_irq_enable(adapter);
}

static void
e1000_vlan_rx_add_vid(struct net_device *netdev, uint16_t vid)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	uint32_t vfta, index;

	if ((adapter->hw.mng_cookie.status &
	     E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT) &&
	    (vid == adapter->mng_vlan_id))
		return;
	/* add VID to filter table */
	index = (vid >> 5) & 0x7F;
	vfta = E1000_READ_REG_ARRAY(&adapter->hw, VFTA, index);
	vfta |= (1 << (vid & 0x1F));
	e1000_write_vfta(&adapter->hw, index, vfta);
}

static void
e1000_vlan_rx_kill_vid(struct net_device *netdev, uint16_t vid)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	uint32_t vfta, index;

	e1000_irq_disable(adapter);

	if (adapter->vlgrp)
		adapter->vlgrp->vlan_devices[vid] = NULL;

	e1000_irq_enable(adapter);

	if ((adapter->hw.mng_cookie.status &
	     E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT) &&
	    (vid == adapter->mng_vlan_id)) {
		/* release control to f/w */
		e1000_release_hw_control(adapter);
		return;
	}

	/* remove VID from filter table */
	index = (vid >> 5) & 0x7F;
	vfta = E1000_READ_REG_ARRAY(&adapter->hw, VFTA, index);
	vfta &= ~(1 << (vid & 0x1F));
	e1000_write_vfta(&adapter->hw, index, vfta);
}

static void
e1000_restore_vlan(struct e1000_adapter *adapter)
{
	e1000_vlan_rx_register(adapter->netdev, adapter->vlgrp);

	if (adapter->vlgrp) {
		uint16_t vid;
		for (vid = 0; vid < VLAN_GROUP_ARRAY_LEN; vid++) {
			if (!adapter->vlgrp->vlan_devices[vid])
				continue;
			e1000_vlan_rx_add_vid(adapter->netdev, vid);
		}
	}
}

int
e1000_set_spd_dplx(struct e1000_adapter *adapter, uint16_t spddplx)
{
	adapter->hw.autoneg = 0;

	/* Fiber NICs only allow 1000 gbps Full duplex */
	if ((adapter->hw.media_type == e1000_media_type_fiber) &&
		spddplx != (SPEED_1000 + DUPLEX_FULL)) {
		DPRINTK(PROBE, ERR, "Unsupported Speed/Duplex configuration\n");
		return -EINVAL;
	}

	switch (spddplx) {
	case SPEED_10 + DUPLEX_HALF:
		adapter->hw.forced_speed_duplex = e1000_10_half;
		break;
	case SPEED_10 + DUPLEX_FULL:
		adapter->hw.forced_speed_duplex = e1000_10_full;
		break;
	case SPEED_100 + DUPLEX_HALF:
		adapter->hw.forced_speed_duplex = e1000_100_half;
		break;
	case SPEED_100 + DUPLEX_FULL:
		adapter->hw.forced_speed_duplex = e1000_100_full;
		break;
	case SPEED_1000 + DUPLEX_FULL:
		adapter->hw.autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case SPEED_1000 + DUPLEX_HALF: /* not supported */
	default:
		DPRINTK(PROBE, ERR, "Unsupported Speed/Duplex configuration\n");
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_PM
/* Save/restore 16 or 64 dwords of PCI config space depending on which
 * bus we're on (PCI(X) vs. PCI-E)
 */
#define PCIE_CONFIG_SPACE_LEN 256
#define PCI_CONFIG_SPACE_LEN 64
static int
e1000_pci_save_state(struct e1000_adapter *adapter)
{
	struct pci_dev *dev = adapter->pdev;
	int size;
	int i;

	if (adapter->hw.mac_type >= e1000_82571)
		size = PCIE_CONFIG_SPACE_LEN;
	else
		size = PCI_CONFIG_SPACE_LEN;

	WARN_ON(adapter->config_space != NULL);

	adapter->config_space = kmalloc(size, GFP_KERNEL);
	if (!adapter->config_space) {
		DPRINTK(PROBE, ERR, "unable to allocate %d bytes\n", size);
		return -ENOMEM;
	}
	for (i = 0; i < (size / 4); i++)
		pci_read_config_dword(dev, i * 4, &adapter->config_space[i]);
	return 0;
}

static void
e1000_pci_restore_state(struct e1000_adapter *adapter)
{
	struct pci_dev *dev = adapter->pdev;
	int size;
	int i;

	if (adapter->config_space == NULL)
		return;

	if (adapter->hw.mac_type >= e1000_82571)
		size = PCIE_CONFIG_SPACE_LEN;
	else
		size = PCI_CONFIG_SPACE_LEN;
	for (i = 0; i < (size / 4); i++)
		pci_write_config_dword(dev, i * 4, adapter->config_space[i]);
	kfree(adapter->config_space);
	adapter->config_space = NULL;
	return;
}
#endif /* CONFIG_PM */

static int
e1000_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	uint32_t ctrl, ctrl_ext, rctl, manc, status;
	uint32_t wufc = adapter->wol;
#ifdef CONFIG_PM
	int retval = 0;
#endif

	netif_device_detach(netdev);

	if (netif_running(netdev)) {
		WARN_ON(test_bit(__E1000_RESETTING, &adapter->flags));
		e1000_down(adapter);
	}

#ifdef CONFIG_PM
	/* Implement our own version of pci_save_state(pdev) because pci-
	 * express adapters have 256-byte config spaces. */
	retval = e1000_pci_save_state(adapter);
	if (retval)
		return retval;
#endif

	status = E1000_READ_REG(&adapter->hw, STATUS);
	if (status & E1000_STATUS_LU)
		wufc &= ~E1000_WUFC_LNKC;

	if (wufc) {
		e1000_setup_rctl(adapter);
		e1000_set_multi(netdev);

		/* turn on all-multi mode if wake on multicast is enabled */
		if (wufc & E1000_WUFC_MC) {
			rctl = E1000_READ_REG(&adapter->hw, RCTL);
			rctl |= E1000_RCTL_MPE;
			E1000_WRITE_REG(&adapter->hw, RCTL, rctl);
		}

		if (adapter->hw.mac_type >= e1000_82540) {
			ctrl = E1000_READ_REG(&adapter->hw, CTRL);
			/* advertise wake from D3Cold */
			#define E1000_CTRL_ADVD3WUC 0x00100000
			/* phy power management enable */
			#define E1000_CTRL_EN_PHY_PWR_MGMT 0x00200000
			ctrl |= E1000_CTRL_ADVD3WUC |
				E1000_CTRL_EN_PHY_PWR_MGMT;
			E1000_WRITE_REG(&adapter->hw, CTRL, ctrl);
		}

		if (adapter->hw.media_type == e1000_media_type_fiber ||
		   adapter->hw.media_type == e1000_media_type_internal_serdes) {
			/* keep the laser running in D3 */
			ctrl_ext = E1000_READ_REG(&adapter->hw, CTRL_EXT);
			ctrl_ext |= E1000_CTRL_EXT_SDP7_DATA;
			E1000_WRITE_REG(&adapter->hw, CTRL_EXT, ctrl_ext);
		}

		/* Allow time for pending master requests to run */
		e1000_disable_pciex_master(&adapter->hw);

		E1000_WRITE_REG(&adapter->hw, WUC, E1000_WUC_PME_EN);
		E1000_WRITE_REG(&adapter->hw, WUFC, wufc);
		pci_enable_wake(pdev, PCI_D3hot, 1);
		pci_enable_wake(pdev, PCI_D3cold, 1);
	} else {
		E1000_WRITE_REG(&adapter->hw, WUC, 0);
		E1000_WRITE_REG(&adapter->hw, WUFC, 0);
		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_enable_wake(pdev, PCI_D3cold, 0);
	}

	if (adapter->hw.mac_type >= e1000_82540 &&
	    adapter->hw.mac_type < e1000_82571 &&
	    adapter->hw.media_type == e1000_media_type_copper) {
		manc = E1000_READ_REG(&adapter->hw, MANC);
		if (manc & E1000_MANC_SMBUS_EN) {
			manc |= E1000_MANC_ARP_EN;
			E1000_WRITE_REG(&adapter->hw, MANC, manc);
			pci_enable_wake(pdev, PCI_D3hot, 1);
			pci_enable_wake(pdev, PCI_D3cold, 1);
		}
	}

	if (adapter->hw.phy_type == e1000_phy_igp_3)
		e1000_phy_powerdown_workaround(&adapter->hw);

	if (netif_running(netdev))
		e1000_free_irq(adapter);

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant. */
	e1000_release_hw_control(adapter);

	pci_disable_device(pdev);

	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

#ifdef CONFIG_PM
static int
e1000_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	uint32_t manc, err;

	pci_set_power_state(pdev, PCI_D0);
	e1000_pci_restore_state(adapter);
	if ((err = pci_enable_device(pdev))) {
		printk(KERN_ERR "e1000: Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	if (netif_running(netdev) && (err = e1000_request_irq(adapter)))
		return err;

	e1000_power_up_phy(adapter);
	e1000_reset(adapter);
	E1000_WRITE_REG(&adapter->hw, WUS, ~0);

	if (netif_running(netdev))
		e1000_up(adapter);

	netif_device_attach(netdev);

	if (adapter->hw.mac_type >= e1000_82540 &&
	    adapter->hw.mac_type < e1000_82571 &&
	    adapter->hw.media_type == e1000_media_type_copper) {
		manc = E1000_READ_REG(&adapter->hw, MANC);
		manc &= ~(E1000_MANC_ARP_EN);
		E1000_WRITE_REG(&adapter->hw, MANC, manc);
	}

	/* If the controller is 82573 and f/w is AMT, do not set
	 * DRV_LOAD until the interface is up.  For all other cases,
	 * let the f/w know that the h/w is now under the control
	 * of the driver. */
	if (adapter->hw.mac_type != e1000_82573 ||
	    !e1000_check_mng_mode(&adapter->hw))
		e1000_get_hw_control(adapter);

	return 0;
}
#endif

static void e1000_shutdown(struct pci_dev *pdev)
{
	e1000_suspend(pdev, PMSG_SUSPEND);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void
e1000_netpoll(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	disable_irq(adapter->pdev->irq);
	e1000_intr(adapter->pdev->irq, netdev);
	e1000_clean_tx_irq(adapter, adapter->tx_ring);
#ifndef CONFIG_E1000_NAPI
	adapter->clean_rx(adapter, adapter->rx_ring);
#endif
	enable_irq(adapter->pdev->irq);
}
#endif

/**
 * e1000_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci conneection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t e1000_io_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev->priv;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		e1000_down(adapter);
	pci_disable_device(pdev);

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * e1000_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * resembles the first-half of the e1000_resume routine.
 */
static pci_ers_result_t e1000_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev->priv;

	if (pci_enable_device(pdev)) {
		printk(KERN_ERR "e1000: Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	e1000_reset(adapter);
	E1000_WRITE_REG(&adapter->hw, WUS, ~0);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * e1000_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the e1000_resume routine.
 */
static void e1000_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev->priv;
	uint32_t manc, swsm;

	if (netif_running(netdev)) {
		if (e1000_up(adapter)) {
			printk("e1000: can't bring device back up after reset\n");
			return;
		}
	}

	netif_device_attach(netdev);

	if (adapter->hw.mac_type >= e1000_82540 &&
	    adapter->hw.mac_type < e1000_82571 &&
	    adapter->hw.media_type == e1000_media_type_copper) {
		manc = E1000_READ_REG(&adapter->hw, MANC);
		manc &= ~(E1000_MANC_ARP_EN);
		E1000_WRITE_REG(&adapter->hw, MANC, manc);
	}

	switch (adapter->hw.mac_type) {
	case e1000_82573:
		swsm = E1000_READ_REG(&adapter->hw, SWSM);
		E1000_WRITE_REG(&adapter->hw, SWSM,
				swsm | E1000_SWSM_DRV_LOAD);
		break;
	default:
		break;
	}

	if (netif_running(netdev))
		mod_timer(&adapter->watchdog_timer, jiffies);
}

/* e1000_main.c */
