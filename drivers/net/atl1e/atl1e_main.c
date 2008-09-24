/*
 * Copyright(c) 2007 Atheros Corporation. All rights reserved.
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "atl1e.h"

#define DRV_VERSION "1.0.0.7-NAPI"

char atl1e_driver_name[] = "ATL1E";
char atl1e_driver_version[] = DRV_VERSION;
#define PCI_DEVICE_ID_ATTANSIC_L1E      0x1026
/*
 * atl1e_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static struct pci_device_id atl1e_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATTANSIC_L1E)},
	/* required last entry */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, atl1e_pci_tbl);

MODULE_AUTHOR("Atheros Corporation, <xiong.huang@atheros.com>, Jie Yang <jie.yang@atheros.com>");
MODULE_DESCRIPTION("Atheros 1000M Ethernet Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static void atl1e_setup_mac_ctrl(struct atl1e_adapter *adapter);

static const u16
atl1e_rx_page_vld_regs[AT_MAX_RECEIVE_QUEUE][AT_PAGE_NUM_PER_QUEUE] =
{
	{REG_HOST_RXF0_PAGE0_VLD, REG_HOST_RXF0_PAGE1_VLD},
	{REG_HOST_RXF1_PAGE0_VLD, REG_HOST_RXF1_PAGE1_VLD},
	{REG_HOST_RXF2_PAGE0_VLD, REG_HOST_RXF2_PAGE1_VLD},
	{REG_HOST_RXF3_PAGE0_VLD, REG_HOST_RXF3_PAGE1_VLD}
};

static const u16 atl1e_rx_page_hi_addr_regs[AT_MAX_RECEIVE_QUEUE] =
{
	REG_RXF0_BASE_ADDR_HI,
	REG_RXF1_BASE_ADDR_HI,
	REG_RXF2_BASE_ADDR_HI,
	REG_RXF3_BASE_ADDR_HI
};

static const u16
atl1e_rx_page_lo_addr_regs[AT_MAX_RECEIVE_QUEUE][AT_PAGE_NUM_PER_QUEUE] =
{
	{REG_HOST_RXF0_PAGE0_LO, REG_HOST_RXF0_PAGE1_LO},
	{REG_HOST_RXF1_PAGE0_LO, REG_HOST_RXF1_PAGE1_LO},
	{REG_HOST_RXF2_PAGE0_LO, REG_HOST_RXF2_PAGE1_LO},
	{REG_HOST_RXF3_PAGE0_LO, REG_HOST_RXF3_PAGE1_LO}
};

static const u16
atl1e_rx_page_write_offset_regs[AT_MAX_RECEIVE_QUEUE][AT_PAGE_NUM_PER_QUEUE] =
{
	{REG_HOST_RXF0_MB0_LO,  REG_HOST_RXF0_MB1_LO},
	{REG_HOST_RXF1_MB0_LO,  REG_HOST_RXF1_MB1_LO},
	{REG_HOST_RXF2_MB0_LO,  REG_HOST_RXF2_MB1_LO},
	{REG_HOST_RXF3_MB0_LO,  REG_HOST_RXF3_MB1_LO}
};

static const u16 atl1e_pay_load_size[] = {
	128, 256, 512, 1024, 2048, 4096,
};

/*
 * atl1e_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 */
static inline void atl1e_irq_enable(struct atl1e_adapter *adapter)
{
	if (likely(atomic_dec_and_test(&adapter->irq_sem))) {
		AT_WRITE_REG(&adapter->hw, REG_ISR, 0);
		AT_WRITE_REG(&adapter->hw, REG_IMR, IMR_NORMAL_MASK);
		AT_WRITE_FLUSH(&adapter->hw);
	}
}

/*
 * atl1e_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 */
static inline void atl1e_irq_disable(struct atl1e_adapter *adapter)
{
	atomic_inc(&adapter->irq_sem);
	AT_WRITE_REG(&adapter->hw, REG_IMR, 0);
	AT_WRITE_FLUSH(&adapter->hw);
	synchronize_irq(adapter->pdev->irq);
}

/*
 * atl1e_irq_reset - reset interrupt confiure on the NIC
 * @adapter: board private structure
 */
static inline void atl1e_irq_reset(struct atl1e_adapter *adapter)
{
	atomic_set(&adapter->irq_sem, 0);
	AT_WRITE_REG(&adapter->hw, REG_ISR, 0);
	AT_WRITE_REG(&adapter->hw, REG_IMR, 0);
	AT_WRITE_FLUSH(&adapter->hw);
}

/*
 * atl1e_phy_config - Timer Call-back
 * @data: pointer to netdev cast into an unsigned long
 */
static void atl1e_phy_config(unsigned long data)
{
	struct atl1e_adapter *adapter = (struct atl1e_adapter *) data;
	struct atl1e_hw *hw = &adapter->hw;
	unsigned long flags;

	spin_lock_irqsave(&adapter->mdio_lock, flags);
	atl1e_restart_autoneg(hw);
	spin_unlock_irqrestore(&adapter->mdio_lock, flags);
}

void atl1e_reinit_locked(struct atl1e_adapter *adapter)
{

	WARN_ON(in_interrupt());
	while (test_and_set_bit(__AT_RESETTING, &adapter->flags))
		msleep(1);
	atl1e_down(adapter);
	atl1e_up(adapter);
	clear_bit(__AT_RESETTING, &adapter->flags);
}

static void atl1e_reset_task(struct work_struct *work)
{
	struct atl1e_adapter *adapter;
	adapter = container_of(work, struct atl1e_adapter, reset_task);

	atl1e_reinit_locked(adapter);
}

static int atl1e_check_link(struct atl1e_adapter *adapter)
{
	struct atl1e_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev    *pdev   = adapter->pdev;
	int err = 0;
	u16 speed, duplex, phy_data;

	/* MII_BMSR must read twise */
	atl1e_read_phy_reg(hw, MII_BMSR, &phy_data);
	atl1e_read_phy_reg(hw, MII_BMSR, &phy_data);
	if ((phy_data & BMSR_LSTATUS) == 0) {
		/* link down */
		if (netif_carrier_ok(netdev)) { /* old link state: Up */
			u32 value;
			/* disable rx */
			value = AT_READ_REG(hw, REG_MAC_CTRL);
			value &= ~MAC_CTRL_RX_EN;
			AT_WRITE_REG(hw, REG_MAC_CTRL, value);
			adapter->link_speed = SPEED_0;
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
	} else {
		/* Link Up */
		err = atl1e_get_speed_and_duplex(hw, &speed, &duplex);
		if (unlikely(err))
			return err;

		/* link result is our setting */
		if (adapter->link_speed != speed ||
		    adapter->link_duplex != duplex) {
			adapter->link_speed  = speed;
			adapter->link_duplex = duplex;
			atl1e_setup_mac_ctrl(adapter);
			dev_info(&pdev->dev,
				"%s: %s NIC Link is Up<%d Mbps %s>\n",
				atl1e_driver_name, netdev->name,
				adapter->link_speed,
				adapter->link_duplex == FULL_DUPLEX ?
				"Full Duplex" : "Half Duplex");
		}

		if (!netif_carrier_ok(netdev)) {
			/* Link down -> Up */
			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
		}
	}
	return 0;
}

/*
 * atl1e_link_chg_task - deal with link change event Out of interrupt context
 * @netdev: network interface device structure
 */
static void atl1e_link_chg_task(struct work_struct *work)
{
	struct atl1e_adapter *adapter;
	unsigned long flags;

	adapter = container_of(work, struct atl1e_adapter, link_chg_task);
	spin_lock_irqsave(&adapter->mdio_lock, flags);
	atl1e_check_link(adapter);
	spin_unlock_irqrestore(&adapter->mdio_lock, flags);
}

static void atl1e_link_chg_event(struct atl1e_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev    *pdev   = adapter->pdev;
	u16 phy_data = 0;
	u16 link_up = 0;

	spin_lock(&adapter->mdio_lock);
	atl1e_read_phy_reg(&adapter->hw, MII_BMSR, &phy_data);
	atl1e_read_phy_reg(&adapter->hw, MII_BMSR, &phy_data);
	spin_unlock(&adapter->mdio_lock);
	link_up = phy_data & BMSR_LSTATUS;
	/* notify upper layer link down ASAP */
	if (!link_up) {
		if (netif_carrier_ok(netdev)) {
			/* old link state: Up */
			dev_info(&pdev->dev, "%s: %s NIC Link is Down\n",
					atl1e_driver_name, netdev->name);
			adapter->link_speed = SPEED_0;
			netif_stop_queue(netdev);
		}
	}
	schedule_work(&adapter->link_chg_task);
}

static void atl1e_del_timer(struct atl1e_adapter *adapter)
{
	del_timer_sync(&adapter->phy_config_timer);
}

static void atl1e_cancel_work(struct atl1e_adapter *adapter)
{
	cancel_work_sync(&adapter->reset_task);
	cancel_work_sync(&adapter->link_chg_task);
}

/*
 * atl1e_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 */
static void atl1e_tx_timeout(struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	schedule_work(&adapter->reset_task);
}

/*
 * atl1e_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 */
static void atl1e_set_multi(struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;
	struct dev_mc_list *mc_ptr;
	u32 mac_ctrl_data = 0;
	u32 hash_value;

	/* Check for Promiscuous and All Multicast modes */
	mac_ctrl_data = AT_READ_REG(hw, REG_MAC_CTRL);

	if (netdev->flags & IFF_PROMISC) {
		mac_ctrl_data |= MAC_CTRL_PROMIS_EN;
	} else if (netdev->flags & IFF_ALLMULTI) {
		mac_ctrl_data |= MAC_CTRL_MC_ALL_EN;
		mac_ctrl_data &= ~MAC_CTRL_PROMIS_EN;
	} else {
		mac_ctrl_data &= ~(MAC_CTRL_PROMIS_EN | MAC_CTRL_MC_ALL_EN);
	}

	AT_WRITE_REG(hw, REG_MAC_CTRL, mac_ctrl_data);

	/* clear the old settings from the multicast hash table */
	AT_WRITE_REG(hw, REG_RX_HASH_TABLE, 0);
	AT_WRITE_REG_ARRAY(hw, REG_RX_HASH_TABLE, 1, 0);

	/* comoute mc addresses' hash value ,and put it into hash table */
	for (mc_ptr = netdev->mc_list; mc_ptr; mc_ptr = mc_ptr->next) {
		hash_value = atl1e_hash_mc_addr(hw, mc_ptr->dmi_addr);
		atl1e_hash_set(hw, hash_value);
	}
}

static void atl1e_vlan_rx_register(struct net_device *netdev,
				   struct vlan_group *grp)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	u32 mac_ctrl_data = 0;

	dev_dbg(&pdev->dev, "atl1e_vlan_rx_register\n");

	atl1e_irq_disable(adapter);

	adapter->vlgrp = grp;
	mac_ctrl_data = AT_READ_REG(&adapter->hw, REG_MAC_CTRL);

	if (grp) {
		/* enable VLAN tag insert/strip */
		mac_ctrl_data |= MAC_CTRL_RMV_VLAN;
	} else {
		/* disable VLAN tag insert/strip */
		mac_ctrl_data &= ~MAC_CTRL_RMV_VLAN;
	}

	AT_WRITE_REG(&adapter->hw, REG_MAC_CTRL, mac_ctrl_data);
	atl1e_irq_enable(adapter);
}

static void atl1e_restore_vlan(struct atl1e_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	dev_dbg(&pdev->dev, "atl1e_restore_vlan !");
	atl1e_vlan_rx_register(adapter->netdev, adapter->vlgrp);
}
/*
 * atl1e_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 */
static int atl1e_set_mac_addr(struct net_device *netdev, void *p)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (netif_running(netdev))
		return -EBUSY;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac_addr, addr->sa_data, netdev->addr_len);

	atl1e_hw_set_mac_addr(&adapter->hw);

	return 0;
}

/*
 * atl1e_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 */
static int atl1e_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	int old_mtu   = netdev->mtu;
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

	if ((max_frame < ETH_ZLEN + ETH_FCS_LEN) ||
			(max_frame > MAX_JUMBO_FRAME_SIZE)) {
		dev_warn(&adapter->pdev->dev, "invalid MTU setting\n");
		return -EINVAL;
	}
	/* set MTU */
	if (old_mtu != new_mtu && netif_running(netdev)) {
		while (test_and_set_bit(__AT_RESETTING, &adapter->flags))
			msleep(1);
		netdev->mtu = new_mtu;
		adapter->hw.max_frame_size = new_mtu;
		adapter->hw.rx_jumbo_th = (max_frame + 7) >> 3;
		atl1e_down(adapter);
		atl1e_up(adapter);
		clear_bit(__AT_RESETTING, &adapter->flags);
	}
	return 0;
}

/*
 *  caller should hold mdio_lock
 */
static int atl1e_mdio_read(struct net_device *netdev, int phy_id, int reg_num)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	u16 result;

	atl1e_read_phy_reg(&adapter->hw, reg_num & MDIO_REG_ADDR_MASK, &result);
	return result;
}

static void atl1e_mdio_write(struct net_device *netdev, int phy_id,
			     int reg_num, int val)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);

	atl1e_write_phy_reg(&adapter->hw, reg_num & MDIO_REG_ADDR_MASK, val);
}

/*
 * atl1e_mii_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 */
static int atl1e_mii_ioctl(struct net_device *netdev,
			   struct ifreq *ifr, int cmd)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	struct mii_ioctl_data *data = if_mii(ifr);
	unsigned long flags;
	int retval = 0;

	if (!netif_running(netdev))
		return -EINVAL;

	spin_lock_irqsave(&adapter->mdio_lock, flags);
	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = 0;
		break;

	case SIOCGMIIREG:
		if (!capable(CAP_NET_ADMIN)) {
			retval = -EPERM;
			goto out;
		}
		if (atl1e_read_phy_reg(&adapter->hw, data->reg_num & 0x1F,
				    &data->val_out)) {
			retval = -EIO;
			goto out;
		}
		break;

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN)) {
			retval = -EPERM;
			goto out;
		}
		if (data->reg_num & ~(0x1F)) {
			retval = -EFAULT;
			goto out;
		}

		dev_dbg(&pdev->dev, "<atl1e_mii_ioctl> write %x %x",
				data->reg_num, data->val_in);
		if (atl1e_write_phy_reg(&adapter->hw,
				     data->reg_num, data->val_in)) {
			retval = -EIO;
			goto out;
		}
		break;

	default:
		retval = -EOPNOTSUPP;
		break;
	}
out:
	spin_unlock_irqrestore(&adapter->mdio_lock, flags);
	return retval;

}

/*
 * atl1e_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 */
static int atl1e_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return atl1e_mii_ioctl(netdev, ifr, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

static void atl1e_setup_pcicmd(struct pci_dev *pdev)
{
	u16 cmd;

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd &= ~(PCI_COMMAND_INTX_DISABLE | PCI_COMMAND_IO);
	cmd |=  (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	pci_write_config_word(pdev, PCI_COMMAND, cmd);

	/*
	 * some motherboards BIOS(PXE/EFI) driver may set PME
	 * while they transfer control to OS (Windows/Linux)
	 * so we should clear this bit before NIC work normally
	 */
	pci_write_config_dword(pdev, REG_PM_CTRLSTAT, 0);
	msleep(1);
}

/*
 * atl1e_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 */
static int __devinit atl1e_alloc_queues(struct atl1e_adapter *adapter)
{
	return 0;
}

/*
 * atl1e_sw_init - Initialize general software structures (struct atl1e_adapter)
 * @adapter: board private structure to initialize
 *
 * atl1e_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 */
static int __devinit atl1e_sw_init(struct atl1e_adapter *adapter)
{
	struct atl1e_hw *hw   = &adapter->hw;
	struct pci_dev	*pdev = adapter->pdev;
	u32 phy_status_data = 0;

	adapter->wol = 0;
	adapter->link_speed = SPEED_0;   /* hardware init */
	adapter->link_duplex = FULL_DUPLEX;
	adapter->num_rx_queues = 1;

	/* PCI config space info */
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_id = pdev->subsystem_device;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);
	pci_read_config_word(pdev, PCI_COMMAND, &hw->pci_cmd_word);

	phy_status_data = AT_READ_REG(hw, REG_PHY_STATUS);
	/* nic type */
	if (hw->revision_id >= 0xF0) {
		hw->nic_type = athr_l2e_revB;
	} else {
		if (phy_status_data & PHY_STATUS_100M)
			hw->nic_type = athr_l1e;
		else
			hw->nic_type = athr_l2e_revA;
	}

	phy_status_data = AT_READ_REG(hw, REG_PHY_STATUS);

	if (phy_status_data & PHY_STATUS_EMI_CA)
		hw->emi_ca = true;
	else
		hw->emi_ca = false;

	hw->phy_configured = false;
	hw->preamble_len = 7;
	hw->max_frame_size = adapter->netdev->mtu;
	hw->rx_jumbo_th = (hw->max_frame_size + ETH_HLEN +
				VLAN_HLEN + ETH_FCS_LEN + 7) >> 3;

	hw->rrs_type = atl1e_rrs_disable;
	hw->indirect_tab = 0;
	hw->base_cpu = 0;

	/* need confirm */

	hw->ict = 50000;                 /* 100ms */
	hw->smb_timer = 200000;          /* 200ms  */
	hw->tpd_burst = 5;
	hw->rrd_thresh = 1;
	hw->tpd_thresh = adapter->tx_ring.count / 2;
	hw->rx_count_down = 4;  /* 2us resolution */
	hw->tx_count_down = hw->imt * 4 / 3;
	hw->dmar_block = atl1e_dma_req_1024;
	hw->dmaw_block = atl1e_dma_req_1024;
	hw->dmar_dly_cnt = 15;
	hw->dmaw_dly_cnt = 4;

	if (atl1e_alloc_queues(adapter)) {
		dev_err(&pdev->dev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	atomic_set(&adapter->irq_sem, 1);
	spin_lock_init(&adapter->mdio_lock);
	spin_lock_init(&adapter->tx_lock);

	set_bit(__AT_DOWN, &adapter->flags);

	return 0;
}

/*
 * atl1e_clean_tx_ring - Free Tx-skb
 * @adapter: board private structure
 */
static void atl1e_clean_tx_ring(struct atl1e_adapter *adapter)
{
	struct atl1e_tx_ring *tx_ring = (struct atl1e_tx_ring *)
				&adapter->tx_ring;
	struct atl1e_tx_buffer *tx_buffer = NULL;
	struct pci_dev *pdev = adapter->pdev;
	u16 index, ring_count;

	if (tx_ring->desc == NULL || tx_ring->tx_buffer == NULL)
		return;

	ring_count = tx_ring->count;
	/* first unmmap dma */
	for (index = 0; index < ring_count; index++) {
		tx_buffer = &tx_ring->tx_buffer[index];
		if (tx_buffer->dma) {
			pci_unmap_page(pdev, tx_buffer->dma,
					tx_buffer->length, PCI_DMA_TODEVICE);
			tx_buffer->dma = 0;
		}
	}
	/* second free skb */
	for (index = 0; index < ring_count; index++) {
		tx_buffer = &tx_ring->tx_buffer[index];
		if (tx_buffer->skb) {
			dev_kfree_skb_any(tx_buffer->skb);
			tx_buffer->skb = NULL;
		}
	}
	/* Zero out Tx-buffers */
	memset(tx_ring->desc, 0, sizeof(struct atl1e_tpd_desc) *
				ring_count);
	memset(tx_ring->tx_buffer, 0, sizeof(struct atl1e_tx_buffer) *
				ring_count);
}

/*
 * atl1e_clean_rx_ring - Free rx-reservation skbs
 * @adapter: board private structure
 */
static void atl1e_clean_rx_ring(struct atl1e_adapter *adapter)
{
	struct atl1e_rx_ring *rx_ring =
		(struct atl1e_rx_ring *)&adapter->rx_ring;
	struct atl1e_rx_page_desc *rx_page_desc = rx_ring->rx_page_desc;
	u16 i, j;


	if (adapter->ring_vir_addr == NULL)
		return;
	/* Zero out the descriptor ring */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		for (j = 0; j < AT_PAGE_NUM_PER_QUEUE; j++) {
			if (rx_page_desc[i].rx_page[j].addr != NULL) {
				memset(rx_page_desc[i].rx_page[j].addr, 0,
						rx_ring->real_page_size);
			}
		}
	}
}

static void atl1e_cal_ring_size(struct atl1e_adapter *adapter, u32 *ring_size)
{
	*ring_size = ((u32)(adapter->tx_ring.count *
		     sizeof(struct atl1e_tpd_desc) + 7
			/* tx ring, qword align */
		     + adapter->rx_ring.real_page_size * AT_PAGE_NUM_PER_QUEUE *
			adapter->num_rx_queues + 31
			/* rx ring,  32 bytes align */
		     + (1 + AT_PAGE_NUM_PER_QUEUE * adapter->num_rx_queues) *
			sizeof(u32) + 3));
			/* tx, rx cmd, dword align   */
}

static void atl1e_init_ring_resources(struct atl1e_adapter *adapter)
{
	struct atl1e_tx_ring *tx_ring = NULL;
	struct atl1e_rx_ring *rx_ring = NULL;

	tx_ring = &adapter->tx_ring;
	rx_ring = &adapter->rx_ring;

	rx_ring->real_page_size = adapter->rx_ring.page_size
				 + adapter->hw.max_frame_size
				 + ETH_HLEN + VLAN_HLEN
				 + ETH_FCS_LEN;
	rx_ring->real_page_size = roundup(rx_ring->real_page_size, 32);
	atl1e_cal_ring_size(adapter, &adapter->ring_size);

	adapter->ring_vir_addr = NULL;
	adapter->rx_ring.desc = NULL;
	rwlock_init(&adapter->tx_ring.tx_lock);

	return;
}

/*
 * Read / Write Ptr Initialize:
 */
static void atl1e_init_ring_ptrs(struct atl1e_adapter *adapter)
{
	struct atl1e_tx_ring *tx_ring = NULL;
	struct atl1e_rx_ring *rx_ring = NULL;
	struct atl1e_rx_page_desc *rx_page_desc = NULL;
	int i, j;

	tx_ring = &adapter->tx_ring;
	rx_ring = &adapter->rx_ring;
	rx_page_desc = rx_ring->rx_page_desc;

	tx_ring->next_to_use = 0;
	atomic_set(&tx_ring->next_to_clean, 0);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		rx_page_desc[i].rx_using  = 0;
		rx_page_desc[i].rx_nxseq = 0;
		for (j = 0; j < AT_PAGE_NUM_PER_QUEUE; j++) {
			*rx_page_desc[i].rx_page[j].write_offset_addr = 0;
			rx_page_desc[i].rx_page[j].read_offset = 0;
		}
	}
}

/*
 * atl1e_free_ring_resources - Free Tx / RX descriptor Resources
 * @adapter: board private structure
 *
 * Free all transmit software resources
 */
static void atl1e_free_ring_resources(struct atl1e_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	atl1e_clean_tx_ring(adapter);
	atl1e_clean_rx_ring(adapter);

	if (adapter->ring_vir_addr) {
		pci_free_consistent(pdev, adapter->ring_size,
				adapter->ring_vir_addr, adapter->ring_dma);
		adapter->ring_vir_addr = NULL;
	}

	if (adapter->tx_ring.tx_buffer) {
		kfree(adapter->tx_ring.tx_buffer);
		adapter->tx_ring.tx_buffer = NULL;
	}
}

/*
 * atl1e_setup_mem_resources - allocate Tx / RX descriptor resources
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 */
static int atl1e_setup_ring_resources(struct atl1e_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct atl1e_tx_ring *tx_ring;
	struct atl1e_rx_ring *rx_ring;
	struct atl1e_rx_page_desc  *rx_page_desc;
	int size, i, j;
	u32 offset = 0;
	int err = 0;

	if (adapter->ring_vir_addr != NULL)
		return 0; /* alloced already */

	tx_ring = &adapter->tx_ring;
	rx_ring = &adapter->rx_ring;

	/* real ring DMA buffer */

	size = adapter->ring_size;
	adapter->ring_vir_addr = pci_alloc_consistent(pdev,
			adapter->ring_size, &adapter->ring_dma);

	if (adapter->ring_vir_addr == NULL) {
		dev_err(&pdev->dev, "pci_alloc_consistent failed, "
				    "size = D%d", size);
		return -ENOMEM;
	}

	memset(adapter->ring_vir_addr, 0, adapter->ring_size);

	rx_page_desc = rx_ring->rx_page_desc;

	/* Init TPD Ring */
	tx_ring->dma = roundup(adapter->ring_dma, 8);
	offset = tx_ring->dma - adapter->ring_dma;
	tx_ring->desc = (struct atl1e_tpd_desc *)
			(adapter->ring_vir_addr + offset);
	size = sizeof(struct atl1e_tx_buffer) * (tx_ring->count);
	tx_ring->tx_buffer = kzalloc(size, GFP_KERNEL);
	if (tx_ring->tx_buffer == NULL) {
		dev_err(&pdev->dev, "kzalloc failed , size = D%d", size);
		err = -ENOMEM;
		goto failed;
	}

	/* Init RXF-Pages */
	offset += (sizeof(struct atl1e_tpd_desc) * tx_ring->count);
	offset = roundup(offset, 32);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		for (j = 0; j < AT_PAGE_NUM_PER_QUEUE; j++) {
			rx_page_desc[i].rx_page[j].dma =
				adapter->ring_dma + offset;
			rx_page_desc[i].rx_page[j].addr =
				adapter->ring_vir_addr + offset;
			offset += rx_ring->real_page_size;
		}
	}

	/* Init CMB dma address */
	tx_ring->cmb_dma = adapter->ring_dma + offset;
	tx_ring->cmb     = (u32 *)(adapter->ring_vir_addr + offset);
	offset += sizeof(u32);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		for (j = 0; j < AT_PAGE_NUM_PER_QUEUE; j++) {
			rx_page_desc[i].rx_page[j].write_offset_dma =
				adapter->ring_dma + offset;
			rx_page_desc[i].rx_page[j].write_offset_addr =
				adapter->ring_vir_addr + offset;
			offset += sizeof(u32);
		}
	}

	if (unlikely(offset > adapter->ring_size)) {
		dev_err(&pdev->dev, "offset(%d) > ring size(%d) !!\n",
				offset, adapter->ring_size);
		err = -1;
		goto failed;
	}

	return 0;
failed:
	if (adapter->ring_vir_addr != NULL) {
		pci_free_consistent(pdev, adapter->ring_size,
				adapter->ring_vir_addr, adapter->ring_dma);
		adapter->ring_vir_addr = NULL;
	}
	return err;
}

static inline void atl1e_configure_des_ring(const struct atl1e_adapter *adapter)
{

	struct atl1e_hw *hw = (struct atl1e_hw *)&adapter->hw;
	struct atl1e_rx_ring *rx_ring =
			(struct atl1e_rx_ring *)&adapter->rx_ring;
	struct atl1e_tx_ring *tx_ring =
			(struct atl1e_tx_ring *)&adapter->tx_ring;
	struct atl1e_rx_page_desc *rx_page_desc = NULL;
	int i, j;

	AT_WRITE_REG(hw, REG_DESC_BASE_ADDR_HI,
			(u32)((adapter->ring_dma & AT_DMA_HI_ADDR_MASK) >> 32));
	AT_WRITE_REG(hw, REG_TPD_BASE_ADDR_LO,
			(u32)((tx_ring->dma) & AT_DMA_LO_ADDR_MASK));
	AT_WRITE_REG(hw, REG_TPD_RING_SIZE, (u16)(tx_ring->count));
	AT_WRITE_REG(hw, REG_HOST_TX_CMB_LO,
			(u32)((tx_ring->cmb_dma) & AT_DMA_LO_ADDR_MASK));

	rx_page_desc = rx_ring->rx_page_desc;
	/* RXF Page Physical address / Page Length */
	for (i = 0; i < AT_MAX_RECEIVE_QUEUE; i++) {
		AT_WRITE_REG(hw, atl1e_rx_page_hi_addr_regs[i],
				 (u32)((adapter->ring_dma &
				 AT_DMA_HI_ADDR_MASK) >> 32));
		for (j = 0; j < AT_PAGE_NUM_PER_QUEUE; j++) {
			u32 page_phy_addr;
			u32 offset_phy_addr;

			page_phy_addr = rx_page_desc[i].rx_page[j].dma;
			offset_phy_addr =
				   rx_page_desc[i].rx_page[j].write_offset_dma;

			AT_WRITE_REG(hw, atl1e_rx_page_lo_addr_regs[i][j],
					page_phy_addr & AT_DMA_LO_ADDR_MASK);
			AT_WRITE_REG(hw, atl1e_rx_page_write_offset_regs[i][j],
					offset_phy_addr & AT_DMA_LO_ADDR_MASK);
			AT_WRITE_REGB(hw, atl1e_rx_page_vld_regs[i][j], 1);
		}
	}
	/* Page Length */
	AT_WRITE_REG(hw, REG_HOST_RXFPAGE_SIZE, rx_ring->page_size);
	/* Load all of base address above */
	AT_WRITE_REG(hw, REG_LOAD_PTR, 1);

	return;
}

static inline void atl1e_configure_tx(struct atl1e_adapter *adapter)
{
	struct atl1e_hw *hw = (struct atl1e_hw *)&adapter->hw;
	u32 dev_ctrl_data = 0;
	u32 max_pay_load = 0;
	u32 jumbo_thresh = 0;
	u32 extra_size = 0;     /* Jumbo frame threshold in QWORD unit */

	/* configure TXQ param */
	if (hw->nic_type != athr_l2e_revB) {
		extra_size = ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN;
		if (hw->max_frame_size <= 1500) {
			jumbo_thresh = hw->max_frame_size + extra_size;
		} else if (hw->max_frame_size < 6*1024) {
			jumbo_thresh =
				(hw->max_frame_size + extra_size) * 2 / 3;
		} else {
			jumbo_thresh = (hw->max_frame_size + extra_size) / 2;
		}
		AT_WRITE_REG(hw, REG_TX_EARLY_TH, (jumbo_thresh + 7) >> 3);
	}

	dev_ctrl_data = AT_READ_REG(hw, REG_DEVICE_CTRL);

	max_pay_load  = ((dev_ctrl_data >> DEVICE_CTRL_MAX_PAYLOAD_SHIFT)) &
			DEVICE_CTRL_MAX_PAYLOAD_MASK;

	hw->dmaw_block = min(max_pay_load, hw->dmaw_block);

	max_pay_load  = ((dev_ctrl_data >> DEVICE_CTRL_MAX_RREQ_SZ_SHIFT)) &
			DEVICE_CTRL_MAX_RREQ_SZ_MASK;
	hw->dmar_block = min(max_pay_load, hw->dmar_block);

	if (hw->nic_type != athr_l2e_revB)
		AT_WRITE_REGW(hw, REG_TXQ_CTRL + 2,
			      atl1e_pay_load_size[hw->dmar_block]);
	/* enable TXQ */
	AT_WRITE_REGW(hw, REG_TXQ_CTRL,
			(((u16)hw->tpd_burst & TXQ_CTRL_NUM_TPD_BURST_MASK)
			 << TXQ_CTRL_NUM_TPD_BURST_SHIFT)
			| TXQ_CTRL_ENH_MODE | TXQ_CTRL_EN);
	return;
}

static inline void atl1e_configure_rx(struct atl1e_adapter *adapter)
{
	struct atl1e_hw *hw = (struct atl1e_hw *)&adapter->hw;
	u32 rxf_len  = 0;
	u32 rxf_low  = 0;
	u32 rxf_high = 0;
	u32 rxf_thresh_data = 0;
	u32 rxq_ctrl_data = 0;

	if (hw->nic_type != athr_l2e_revB) {
		AT_WRITE_REGW(hw, REG_RXQ_JMBOSZ_RRDTIM,
			      (u16)((hw->rx_jumbo_th & RXQ_JMBOSZ_TH_MASK) <<
			      RXQ_JMBOSZ_TH_SHIFT |
			      (1 & RXQ_JMBO_LKAH_MASK) <<
			      RXQ_JMBO_LKAH_SHIFT));

		rxf_len  = AT_READ_REG(hw, REG_SRAM_RXF_LEN);
		rxf_high = rxf_len * 4 / 5;
		rxf_low  = rxf_len / 5;
		rxf_thresh_data = ((rxf_high  & RXQ_RXF_PAUSE_TH_HI_MASK)
				  << RXQ_RXF_PAUSE_TH_HI_SHIFT) |
				  ((rxf_low & RXQ_RXF_PAUSE_TH_LO_MASK)
				  << RXQ_RXF_PAUSE_TH_LO_SHIFT);

		AT_WRITE_REG(hw, REG_RXQ_RXF_PAUSE_THRESH, rxf_thresh_data);
	}

	/* RRS */
	AT_WRITE_REG(hw, REG_IDT_TABLE, hw->indirect_tab);
	AT_WRITE_REG(hw, REG_BASE_CPU_NUMBER, hw->base_cpu);

	if (hw->rrs_type & atl1e_rrs_ipv4)
		rxq_ctrl_data |= RXQ_CTRL_HASH_TYPE_IPV4;

	if (hw->rrs_type & atl1e_rrs_ipv4_tcp)
		rxq_ctrl_data |= RXQ_CTRL_HASH_TYPE_IPV4_TCP;

	if (hw->rrs_type & atl1e_rrs_ipv6)
		rxq_ctrl_data |= RXQ_CTRL_HASH_TYPE_IPV6;

	if (hw->rrs_type & atl1e_rrs_ipv6_tcp)
		rxq_ctrl_data |= RXQ_CTRL_HASH_TYPE_IPV6_TCP;

	if (hw->rrs_type != atl1e_rrs_disable)
		rxq_ctrl_data |=
			(RXQ_CTRL_HASH_ENABLE | RXQ_CTRL_RSS_MODE_MQUESINT);

	rxq_ctrl_data |= RXQ_CTRL_IPV6_XSUM_VERIFY_EN | RXQ_CTRL_PBA_ALIGN_32 |
			 RXQ_CTRL_CUT_THRU_EN | RXQ_CTRL_EN;

	AT_WRITE_REG(hw, REG_RXQ_CTRL, rxq_ctrl_data);
	return;
}

static inline void atl1e_configure_dma(struct atl1e_adapter *adapter)
{
	struct atl1e_hw *hw = &adapter->hw;
	u32 dma_ctrl_data = 0;

	dma_ctrl_data = DMA_CTRL_RXCMB_EN;
	dma_ctrl_data |= (((u32)hw->dmar_block) & DMA_CTRL_DMAR_BURST_LEN_MASK)
		<< DMA_CTRL_DMAR_BURST_LEN_SHIFT;
	dma_ctrl_data |= (((u32)hw->dmaw_block) & DMA_CTRL_DMAW_BURST_LEN_MASK)
		<< DMA_CTRL_DMAW_BURST_LEN_SHIFT;
	dma_ctrl_data |= DMA_CTRL_DMAR_REQ_PRI | DMA_CTRL_DMAR_OUT_ORDER;
	dma_ctrl_data |= (((u32)hw->dmar_dly_cnt) & DMA_CTRL_DMAR_DLY_CNT_MASK)
		<< DMA_CTRL_DMAR_DLY_CNT_SHIFT;
	dma_ctrl_data |= (((u32)hw->dmaw_dly_cnt) & DMA_CTRL_DMAW_DLY_CNT_MASK)
		<< DMA_CTRL_DMAW_DLY_CNT_SHIFT;

	AT_WRITE_REG(hw, REG_DMA_CTRL, dma_ctrl_data);
	return;
}

static void atl1e_setup_mac_ctrl(struct atl1e_adapter *adapter)
{
	u32 value;
	struct atl1e_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;

	/* Config MAC CTRL Register */
	value = MAC_CTRL_TX_EN |
		MAC_CTRL_RX_EN ;

	if (FULL_DUPLEX == adapter->link_duplex)
		value |= MAC_CTRL_DUPLX;

	value |= ((u32)((SPEED_1000 == adapter->link_speed) ?
			  MAC_CTRL_SPEED_1000 : MAC_CTRL_SPEED_10_100) <<
			  MAC_CTRL_SPEED_SHIFT);
	value |= (MAC_CTRL_TX_FLOW | MAC_CTRL_RX_FLOW);

	value |= (MAC_CTRL_ADD_CRC | MAC_CTRL_PAD);
	value |= (((u32)adapter->hw.preamble_len &
		  MAC_CTRL_PRMLEN_MASK) << MAC_CTRL_PRMLEN_SHIFT);

	if (adapter->vlgrp)
		value |= MAC_CTRL_RMV_VLAN;

	value |= MAC_CTRL_BC_EN;
	if (netdev->flags & IFF_PROMISC)
		value |= MAC_CTRL_PROMIS_EN;
	if (netdev->flags & IFF_ALLMULTI)
		value |= MAC_CTRL_MC_ALL_EN;

	AT_WRITE_REG(hw, REG_MAC_CTRL, value);
}

/*
 * atl1e_configure - Configure Transmit&Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx /Rx unit of the MAC after a reset.
 */
static int atl1e_configure(struct atl1e_adapter *adapter)
{
	struct atl1e_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;

	u32 intr_status_data = 0;

	/* clear interrupt status */
	AT_WRITE_REG(hw, REG_ISR, ~0);

	/* 1. set MAC Address */
	atl1e_hw_set_mac_addr(hw);

	/* 2. Init the Multicast HASH table done by set_muti */

	/* 3. Clear any WOL status */
	AT_WRITE_REG(hw, REG_WOL_CTRL, 0);

	/* 4. Descripter Ring BaseMem/Length/Read ptr/Write ptr
	 *    TPD Ring/SMB/RXF0 Page CMBs, they use the same
	 *    High 32bits memory */
	atl1e_configure_des_ring(adapter);

	/* 5. set Interrupt Moderator Timer */
	AT_WRITE_REGW(hw, REG_IRQ_MODU_TIMER_INIT, hw->imt);
	AT_WRITE_REGW(hw, REG_IRQ_MODU_TIMER2_INIT, hw->imt);
	AT_WRITE_REG(hw, REG_MASTER_CTRL, MASTER_CTRL_LED_MODE |
			MASTER_CTRL_ITIMER_EN | MASTER_CTRL_ITIMER2_EN);

	/* 6. rx/tx threshold to trig interrupt */
	AT_WRITE_REGW(hw, REG_TRIG_RRD_THRESH, hw->rrd_thresh);
	AT_WRITE_REGW(hw, REG_TRIG_TPD_THRESH, hw->tpd_thresh);
	AT_WRITE_REGW(hw, REG_TRIG_RXTIMER, hw->rx_count_down);
	AT_WRITE_REGW(hw, REG_TRIG_TXTIMER, hw->tx_count_down);

	/* 7. set Interrupt Clear Timer */
	AT_WRITE_REGW(hw, REG_CMBDISDMA_TIMER, hw->ict);

	/* 8. set MTU */
	AT_WRITE_REG(hw, REG_MTU, hw->max_frame_size + ETH_HLEN +
			VLAN_HLEN + ETH_FCS_LEN);

	/* 9. config TXQ early tx threshold */
	atl1e_configure_tx(adapter);

	/* 10. config RXQ */
	atl1e_configure_rx(adapter);

	/* 11. config  DMA Engine */
	atl1e_configure_dma(adapter);

	/* 12. smb timer to trig interrupt */
	AT_WRITE_REG(hw, REG_SMB_STAT_TIMER, hw->smb_timer);

	intr_status_data = AT_READ_REG(hw, REG_ISR);
	if (unlikely((intr_status_data & ISR_PHY_LINKDOWN) != 0)) {
		dev_err(&pdev->dev, "atl1e_configure failed,"
				"PCIE phy link down\n");
		return -1;
	}

	AT_WRITE_REG(hw, REG_ISR, 0x7fffffff);
	return 0;
}

/*
 * atl1e_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 */
static struct net_device_stats *atl1e_get_stats(struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw_stats  *hw_stats = &adapter->hw_stats;
	struct net_device_stats *net_stats = &adapter->net_stats;

	net_stats->rx_packets = hw_stats->rx_ok;
	net_stats->tx_packets = hw_stats->tx_ok;
	net_stats->rx_bytes   = hw_stats->rx_byte_cnt;
	net_stats->tx_bytes   = hw_stats->tx_byte_cnt;
	net_stats->multicast  = hw_stats->rx_mcast;
	net_stats->collisions = hw_stats->tx_1_col +
				hw_stats->tx_2_col * 2 +
				hw_stats->tx_late_col + hw_stats->tx_abort_col;

	net_stats->rx_errors  = hw_stats->rx_frag + hw_stats->rx_fcs_err +
				hw_stats->rx_len_err + hw_stats->rx_sz_ov +
				hw_stats->rx_rrd_ov + hw_stats->rx_align_err;
	net_stats->rx_fifo_errors   = hw_stats->rx_rxf_ov;
	net_stats->rx_length_errors = hw_stats->rx_len_err;
	net_stats->rx_crc_errors    = hw_stats->rx_fcs_err;
	net_stats->rx_frame_errors  = hw_stats->rx_align_err;
	net_stats->rx_over_errors   = hw_stats->rx_rrd_ov + hw_stats->rx_rxf_ov;

	net_stats->rx_missed_errors = hw_stats->rx_rrd_ov + hw_stats->rx_rxf_ov;

	net_stats->tx_errors = hw_stats->tx_late_col + hw_stats->tx_abort_col +
			       hw_stats->tx_underrun + hw_stats->tx_trunc;
	net_stats->tx_fifo_errors    = hw_stats->tx_underrun;
	net_stats->tx_aborted_errors = hw_stats->tx_abort_col;
	net_stats->tx_window_errors  = hw_stats->tx_late_col;

	return &adapter->net_stats;
}

static void atl1e_update_hw_stats(struct atl1e_adapter *adapter)
{
	u16 hw_reg_addr = 0;
	unsigned long *stats_item = NULL;

	/* update rx status */
	hw_reg_addr = REG_MAC_RX_STATUS_BIN;
	stats_item  = &adapter->hw_stats.rx_ok;
	while (hw_reg_addr <= REG_MAC_RX_STATUS_END) {
		*stats_item += AT_READ_REG(&adapter->hw, hw_reg_addr);
		stats_item++;
		hw_reg_addr += 4;
	}
	/* update tx status */
	hw_reg_addr = REG_MAC_TX_STATUS_BIN;
	stats_item  = &adapter->hw_stats.tx_ok;
	while (hw_reg_addr <= REG_MAC_TX_STATUS_END) {
		*stats_item += AT_READ_REG(&adapter->hw, hw_reg_addr);
		stats_item++;
		hw_reg_addr += 4;
	}
}

static inline void atl1e_clear_phy_int(struct atl1e_adapter *adapter)
{
	u16 phy_data;

	spin_lock(&adapter->mdio_lock);
	atl1e_read_phy_reg(&adapter->hw, MII_INT_STATUS, &phy_data);
	spin_unlock(&adapter->mdio_lock);
}

static bool atl1e_clean_tx_irq(struct atl1e_adapter *adapter)
{
	struct atl1e_tx_ring *tx_ring = (struct atl1e_tx_ring *)
					&adapter->tx_ring;
	struct atl1e_tx_buffer *tx_buffer = NULL;
	u16 hw_next_to_clean = AT_READ_REGW(&adapter->hw, REG_TPD_CONS_IDX);
	u16 next_to_clean = atomic_read(&tx_ring->next_to_clean);

	while (next_to_clean != hw_next_to_clean) {
		tx_buffer = &tx_ring->tx_buffer[next_to_clean];
		if (tx_buffer->dma) {
			pci_unmap_page(adapter->pdev, tx_buffer->dma,
					tx_buffer->length, PCI_DMA_TODEVICE);
			tx_buffer->dma = 0;
		}

		if (tx_buffer->skb) {
			dev_kfree_skb_irq(tx_buffer->skb);
			tx_buffer->skb = NULL;
		}

		if (++next_to_clean == tx_ring->count)
			next_to_clean = 0;
	}

	atomic_set(&tx_ring->next_to_clean, next_to_clean);

	if (netif_queue_stopped(adapter->netdev) &&
			netif_carrier_ok(adapter->netdev)) {
		netif_wake_queue(adapter->netdev);
	}

	return true;
}

/*
 * atl1e_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 * @pt_regs: CPU registers structure
 */
static irqreturn_t atl1e_intr(int irq, void *data)
{
	struct net_device *netdev  = data;
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	struct atl1e_hw *hw = &adapter->hw;
	int max_ints = AT_MAX_INT_WORK;
	int handled = IRQ_NONE;
	u32 status;

	do {
		status = AT_READ_REG(hw, REG_ISR);
		if ((status & IMR_NORMAL_MASK) == 0 ||
				(status & ISR_DIS_INT) != 0) {
			if (max_ints != AT_MAX_INT_WORK)
				handled = IRQ_HANDLED;
			break;
		}
		/* link event */
		if (status & ISR_GPHY)
			atl1e_clear_phy_int(adapter);
		/* Ack ISR */
		AT_WRITE_REG(hw, REG_ISR, status | ISR_DIS_INT);

		handled = IRQ_HANDLED;
		/* check if PCIE PHY Link down */
		if (status & ISR_PHY_LINKDOWN) {
			dev_err(&pdev->dev,
				"pcie phy linkdown %x\n", status);
			if (netif_running(adapter->netdev)) {
				/* reset MAC */
				atl1e_irq_reset(adapter);
				schedule_work(&adapter->reset_task);
				break;
			}
		}

		/* check if DMA read/write error */
		if (status & (ISR_DMAR_TO_RST | ISR_DMAW_TO_RST)) {
			dev_err(&pdev->dev,
				"PCIE DMA RW error (status = 0x%x)\n",
				status);
			atl1e_irq_reset(adapter);
			schedule_work(&adapter->reset_task);
			break;
		}

		if (status & ISR_SMB)
			atl1e_update_hw_stats(adapter);

		/* link event */
		if (status & (ISR_GPHY | ISR_MANUAL)) {
			adapter->net_stats.tx_carrier_errors++;
			atl1e_link_chg_event(adapter);
			break;
		}

		/* transmit event */
		if (status & ISR_TX_EVENT)
			atl1e_clean_tx_irq(adapter);

		if (status & ISR_RX_EVENT) {
			/*
			 * disable rx interrupts, without
			 * the synchronize_irq bit
			 */
			AT_WRITE_REG(hw, REG_IMR,
				     IMR_NORMAL_MASK & ~ISR_RX_EVENT);
			AT_WRITE_FLUSH(hw);
			if (likely(netif_rx_schedule_prep(netdev,
				   &adapter->napi)))
				__netif_rx_schedule(netdev, &adapter->napi);
		}
	} while (--max_ints > 0);
	/* re-enable Interrupt*/
	AT_WRITE_REG(&adapter->hw, REG_ISR, 0);

	return handled;
}

static inline void atl1e_rx_checksum(struct atl1e_adapter *adapter,
		  struct sk_buff *skb, struct atl1e_recv_ret_status *prrs)
{
	u8 *packet = (u8 *)(prrs + 1);
	struct iphdr *iph;
	u16 head_len = ETH_HLEN;
	u16 pkt_flags;
	u16 err_flags;

	skb->ip_summed = CHECKSUM_NONE;
	pkt_flags = prrs->pkt_flag;
	err_flags = prrs->err_flag;
	if (((pkt_flags & RRS_IS_IPV4) || (pkt_flags & RRS_IS_IPV6)) &&
		((pkt_flags & RRS_IS_TCP) || (pkt_flags & RRS_IS_UDP))) {
		if (pkt_flags & RRS_IS_IPV4) {
			if (pkt_flags & RRS_IS_802_3)
				head_len += 8;
			iph = (struct iphdr *) (packet + head_len);
			if (iph->frag_off != 0 && !(pkt_flags & RRS_IS_IP_DF))
				goto hw_xsum;
		}
		if (!(err_flags & (RRS_ERR_IP_CSUM | RRS_ERR_L4_CSUM))) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			return;
		}
	}

hw_xsum :
	return;
}

static struct atl1e_rx_page *atl1e_get_rx_page(struct atl1e_adapter *adapter,
					       u8 que)
{
	struct atl1e_rx_page_desc *rx_page_desc =
		(struct atl1e_rx_page_desc *) adapter->rx_ring.rx_page_desc;
	u8 rx_using = rx_page_desc[que].rx_using;

	return (struct atl1e_rx_page *)&(rx_page_desc[que].rx_page[rx_using]);
}

static void atl1e_clean_rx_irq(struct atl1e_adapter *adapter, u8 que,
		   int *work_done, int work_to_do)
{
	struct pci_dev *pdev = adapter->pdev;
	struct net_device *netdev  = adapter->netdev;
	struct atl1e_rx_ring *rx_ring = (struct atl1e_rx_ring *)
					 &adapter->rx_ring;
	struct atl1e_rx_page_desc *rx_page_desc =
		(struct atl1e_rx_page_desc *) rx_ring->rx_page_desc;
	struct sk_buff *skb = NULL;
	struct atl1e_rx_page *rx_page = atl1e_get_rx_page(adapter, que);
	u32 packet_size, write_offset;
	struct atl1e_recv_ret_status *prrs;

	write_offset = *(rx_page->write_offset_addr);
	if (likely(rx_page->read_offset < write_offset)) {
		do {
			if (*work_done >= work_to_do)
				break;
			(*work_done)++;
			/* get new packet's  rrs */
			prrs = (struct atl1e_recv_ret_status *) (rx_page->addr +
						 rx_page->read_offset);
			/* check sequence number */
			if (prrs->seq_num != rx_page_desc[que].rx_nxseq) {
				dev_err(&pdev->dev,
					"rx sequence number"
					" error (rx=%d) (expect=%d)\n",
					prrs->seq_num,
					rx_page_desc[que].rx_nxseq);
				rx_page_desc[que].rx_nxseq++;
				/* just for debug use */
				AT_WRITE_REG(&adapter->hw, REG_DEBUG_DATA0,
					     (((u32)prrs->seq_num) << 16) |
					     rx_page_desc[que].rx_nxseq);
				goto fatal_err;
			}
			rx_page_desc[que].rx_nxseq++;

			/* error packet */
			if (prrs->pkt_flag & RRS_IS_ERR_FRAME) {
				if (prrs->err_flag & (RRS_ERR_BAD_CRC |
					RRS_ERR_DRIBBLE | RRS_ERR_CODE |
					RRS_ERR_TRUNC)) {
				/* hardware error, discard this packet*/
					dev_err(&pdev->dev,
						"rx packet desc error %x\n",
						*((u32 *)prrs + 1));
					goto skip_pkt;
				}
			}

			packet_size = ((prrs->word1 >> RRS_PKT_SIZE_SHIFT) &
					RRS_PKT_SIZE_MASK) - 4; /* CRC */
			skb = netdev_alloc_skb(netdev,
					       packet_size + NET_IP_ALIGN);
			if (skb == NULL) {
				dev_warn(&pdev->dev, "%s: Memory squeeze,"
					"deferring packet.\n", netdev->name);
				goto skip_pkt;
			}
			skb_reserve(skb, NET_IP_ALIGN);
			skb->dev = netdev;
			memcpy(skb->data, (u8 *)(prrs + 1), packet_size);
			skb_put(skb, packet_size);
			skb->protocol = eth_type_trans(skb, netdev);
			atl1e_rx_checksum(adapter, skb, prrs);

			if (unlikely(adapter->vlgrp &&
				(prrs->pkt_flag & RRS_IS_VLAN_TAG))) {
				u16 vlan_tag = (prrs->vtag >> 4) |
					       ((prrs->vtag & 7) << 13) |
					       ((prrs->vtag & 8) << 9);
				dev_dbg(&pdev->dev,
					"RXD VLAN TAG<RRD>=0x%04x\n",
					prrs->vtag);
				vlan_hwaccel_receive_skb(skb, adapter->vlgrp,
							 vlan_tag);
			} else {
				netif_receive_skb(skb);
			}

			netdev->last_rx = jiffies;
skip_pkt:
	/* skip current packet whether it's ok or not. */
			rx_page->read_offset +=
				(((u32)((prrs->word1 >> RRS_PKT_SIZE_SHIFT) &
				RRS_PKT_SIZE_MASK) +
				sizeof(struct atl1e_recv_ret_status) + 31) &
						0xFFFFFFE0);

			if (rx_page->read_offset >= rx_ring->page_size) {
				/* mark this page clean */
				u16 reg_addr;
				u8  rx_using;

				rx_page->read_offset =
					*(rx_page->write_offset_addr) = 0;
				rx_using = rx_page_desc[que].rx_using;
				reg_addr =
					atl1e_rx_page_vld_regs[que][rx_using];
				AT_WRITE_REGB(&adapter->hw, reg_addr, 1);
				rx_page_desc[que].rx_using ^= 1;
				rx_page = atl1e_get_rx_page(adapter, que);
			}
			write_offset = *(rx_page->write_offset_addr);
		} while (rx_page->read_offset < write_offset);
	}

	return;

fatal_err:
	if (!test_bit(__AT_DOWN, &adapter->flags))
		schedule_work(&adapter->reset_task);
}

/*
 * atl1e_clean - NAPI Rx polling callback
 * @adapter: board private structure
 */
static int atl1e_clean(struct napi_struct *napi, int budget)
{
	struct atl1e_adapter *adapter =
			container_of(napi, struct atl1e_adapter, napi);
	struct net_device *netdev  = adapter->netdev;
	struct pci_dev    *pdev    = adapter->pdev;
	u32 imr_data;
	int work_done = 0;

	/* Keep link state information with original netdev */
	if (!netif_carrier_ok(adapter->netdev))
		goto quit_polling;

	atl1e_clean_rx_irq(adapter, 0, &work_done, budget);

	/* If no Tx and not enough Rx work done, exit the polling mode */
	if (work_done < budget) {
quit_polling:
		netif_rx_complete(netdev, napi);
		imr_data = AT_READ_REG(&adapter->hw, REG_IMR);
		AT_WRITE_REG(&adapter->hw, REG_IMR, imr_data | ISR_RX_EVENT);
		/* test debug */
		if (test_bit(__AT_DOWN, &adapter->flags)) {
			atomic_dec(&adapter->irq_sem);
			dev_err(&pdev->dev,
				"atl1e_clean is called when AT_DOWN\n");
		}
		/* reenable RX intr */
		/*atl1e_irq_enable(adapter); */

	}
	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER

/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void atl1e_netpoll(struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);

	disable_irq(adapter->pdev->irq);
	atl1e_intr(adapter->pdev->irq, netdev);
	enable_irq(adapter->pdev->irq);
}
#endif

static inline u16 atl1e_tpd_avail(struct atl1e_adapter *adapter)
{
	struct atl1e_tx_ring *tx_ring = &adapter->tx_ring;
	u16 next_to_use = 0;
	u16 next_to_clean = 0;

	next_to_clean = atomic_read(&tx_ring->next_to_clean);
	next_to_use   = tx_ring->next_to_use;

	return (u16)(next_to_clean > next_to_use) ?
		(next_to_clean - next_to_use - 1) :
		(tx_ring->count + next_to_clean - next_to_use - 1);
}

/*
 * get next usable tpd
 * Note: should call atl1e_tdp_avail to make sure
 * there is enough tpd to use
 */
static struct atl1e_tpd_desc *atl1e_get_tpd(struct atl1e_adapter *adapter)
{
	struct atl1e_tx_ring *tx_ring = &adapter->tx_ring;
	u16 next_to_use = 0;

	next_to_use = tx_ring->next_to_use;
	if (++tx_ring->next_to_use == tx_ring->count)
		tx_ring->next_to_use = 0;

	memset(&tx_ring->desc[next_to_use], 0, sizeof(struct atl1e_tpd_desc));
	return (struct atl1e_tpd_desc *)&tx_ring->desc[next_to_use];
}

static struct atl1e_tx_buffer *
atl1e_get_tx_buffer(struct atl1e_adapter *adapter, struct atl1e_tpd_desc *tpd)
{
	struct atl1e_tx_ring *tx_ring = &adapter->tx_ring;

	return &tx_ring->tx_buffer[tpd - tx_ring->desc];
}

/* Calculate the transmit packet descript needed*/
static u16 atl1e_cal_tdp_req(const struct sk_buff *skb)
{
	int i = 0;
	u16 tpd_req = 1;
	u16 fg_size = 0;
	u16 proto_hdr_len = 0;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		fg_size = skb_shinfo(skb)->frags[i].size;
		tpd_req += ((fg_size + MAX_TX_BUF_LEN - 1) >> MAX_TX_BUF_SHIFT);
	}

	if (skb_is_gso(skb)) {
		if (skb->protocol == ntohs(ETH_P_IP) ||
		   (skb_shinfo(skb)->gso_type == SKB_GSO_TCPV6)) {
			proto_hdr_len = skb_transport_offset(skb) +
					tcp_hdrlen(skb);
			if (proto_hdr_len < skb_headlen(skb)) {
				tpd_req += ((skb_headlen(skb) - proto_hdr_len +
					   MAX_TX_BUF_LEN - 1) >>
					   MAX_TX_BUF_SHIFT);
			}
		}

	}
	return tpd_req;
}

static int atl1e_tso_csum(struct atl1e_adapter *adapter,
		       struct sk_buff *skb, struct atl1e_tpd_desc *tpd)
{
	struct pci_dev *pdev = adapter->pdev;
	u8 hdr_len;
	u32 real_len;
	unsigned short offload_type;
	int err;

	if (skb_is_gso(skb)) {
		if (skb_header_cloned(skb)) {
			err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (unlikely(err))
				return -1;
		}
		offload_type = skb_shinfo(skb)->gso_type;

		if (offload_type & SKB_GSO_TCPV4) {
			real_len = (((unsigned char *)ip_hdr(skb) - skb->data)
					+ ntohs(ip_hdr(skb)->tot_len));

			if (real_len < skb->len)
				pskb_trim(skb, real_len);

			hdr_len = (skb_transport_offset(skb) + tcp_hdrlen(skb));
			if (unlikely(skb->len == hdr_len)) {
				/* only xsum need */
				dev_warn(&pdev->dev,
				      "IPV4 tso with zero data??\n");
				goto check_sum;
			} else {
				ip_hdr(skb)->check = 0;
				ip_hdr(skb)->tot_len = 0;
				tcp_hdr(skb)->check = ~csum_tcpudp_magic(
							ip_hdr(skb)->saddr,
							ip_hdr(skb)->daddr,
							0, IPPROTO_TCP, 0);
				tpd->word3 |= (ip_hdr(skb)->ihl &
					TDP_V4_IPHL_MASK) <<
					TPD_V4_IPHL_SHIFT;
				tpd->word3 |= ((tcp_hdrlen(skb) >> 2) &
					TPD_TCPHDRLEN_MASK) <<
					TPD_TCPHDRLEN_SHIFT;
				tpd->word3 |= ((skb_shinfo(skb)->gso_size) &
					TPD_MSS_MASK) << TPD_MSS_SHIFT;
				tpd->word3 |= 1 << TPD_SEGMENT_EN_SHIFT;
			}
			return 0;
		}

		if (offload_type & SKB_GSO_TCPV6) {
			real_len = (((unsigned char *)ipv6_hdr(skb) - skb->data)
					+ ntohs(ipv6_hdr(skb)->payload_len));
			if (real_len < skb->len)
				pskb_trim(skb, real_len);

			/* check payload == 0 byte ? */
			hdr_len = (skb_transport_offset(skb) + tcp_hdrlen(skb));
			if (unlikely(skb->len == hdr_len)) {
				/* only xsum need */
				dev_warn(&pdev->dev,
					"IPV6 tso with zero data??\n");
				goto check_sum;
			} else {
				tcp_hdr(skb)->check = ~csum_ipv6_magic(
						&ipv6_hdr(skb)->saddr,
						&ipv6_hdr(skb)->daddr,
						0, IPPROTO_TCP, 0);
				tpd->word3 |= 1 << TPD_IP_VERSION_SHIFT;
				hdr_len >>= 1;
				tpd->word3 |= (hdr_len & TPD_V6_IPHLLO_MASK) <<
					TPD_V6_IPHLLO_SHIFT;
				tpd->word3 |= ((hdr_len >> 3) &
					TPD_V6_IPHLHI_MASK) <<
					TPD_V6_IPHLHI_SHIFT;
				tpd->word3 |= (tcp_hdrlen(skb) >> 2 &
					TPD_TCPHDRLEN_MASK) <<
					TPD_TCPHDRLEN_SHIFT;
				tpd->word3 |= ((skb_shinfo(skb)->gso_size) &
					TPD_MSS_MASK) << TPD_MSS_SHIFT;
					tpd->word3 |= 1 << TPD_SEGMENT_EN_SHIFT;
			}
		}
		return 0;
	}

check_sum:
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		u8 css, cso;

		cso = skb_transport_offset(skb);
		if (unlikely(cso & 0x1)) {
			dev_err(&adapter->pdev->dev,
			   "pay load offset should not ant event number\n");
			return -1;
		} else {
			css = cso + skb->csum_offset;
			tpd->word3 |= (cso & TPD_PLOADOFFSET_MASK) <<
					TPD_PLOADOFFSET_SHIFT;
			tpd->word3 |= (css & TPD_CCSUMOFFSET_MASK) <<
					TPD_CCSUMOFFSET_SHIFT;
			tpd->word3 |= 1 << TPD_CC_SEGMENT_EN_SHIFT;
		}
	}

	return 0;
}

static void atl1e_tx_map(struct atl1e_adapter *adapter,
		      struct sk_buff *skb, struct atl1e_tpd_desc *tpd)
{
	struct atl1e_tpd_desc *use_tpd = NULL;
	struct atl1e_tx_buffer *tx_buffer = NULL;
	u16 buf_len = skb->len - skb->data_len;
	u16 map_len = 0;
	u16 mapped_len = 0;
	u16 hdr_len = 0;
	u16 nr_frags;
	u16 f;
	int segment;

	nr_frags = skb_shinfo(skb)->nr_frags;
	segment = (tpd->word3 >> TPD_SEGMENT_EN_SHIFT) & TPD_SEGMENT_EN_MASK;
	if (segment) {
		/* TSO */
		map_len = hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		use_tpd = tpd;

		tx_buffer = atl1e_get_tx_buffer(adapter, use_tpd);
		tx_buffer->length = map_len;
		tx_buffer->dma = pci_map_single(adapter->pdev,
					skb->data, hdr_len, PCI_DMA_TODEVICE);
		mapped_len += map_len;
		use_tpd->buffer_addr = cpu_to_le64(tx_buffer->dma);
		use_tpd->word2 = (use_tpd->word2 & (~TPD_BUFLEN_MASK)) |
			((cpu_to_le32(tx_buffer->length) &
			TPD_BUFLEN_MASK) << TPD_BUFLEN_SHIFT);
	}

	while (mapped_len < buf_len) {
		/* mapped_len == 0, means we should use the first tpd,
		   which is given by caller  */
		if (mapped_len == 0) {
			use_tpd = tpd;
		} else {
			use_tpd = atl1e_get_tpd(adapter);
			memcpy(use_tpd, tpd, sizeof(struct atl1e_tpd_desc));
		}
		tx_buffer = atl1e_get_tx_buffer(adapter, use_tpd);
		tx_buffer->skb = NULL;

		tx_buffer->length = map_len =
			((buf_len - mapped_len) >= MAX_TX_BUF_LEN) ?
			MAX_TX_BUF_LEN : (buf_len - mapped_len);
		tx_buffer->dma =
			pci_map_single(adapter->pdev, skb->data + mapped_len,
					map_len, PCI_DMA_TODEVICE);
		mapped_len  += map_len;
		use_tpd->buffer_addr = cpu_to_le64(tx_buffer->dma);
		use_tpd->word2 = (use_tpd->word2 & (~TPD_BUFLEN_MASK)) |
			((cpu_to_le32(tx_buffer->length) &
			TPD_BUFLEN_MASK) << TPD_BUFLEN_SHIFT);
	}

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;
		u16 i;
		u16 seg_num;

		frag = &skb_shinfo(skb)->frags[f];
		buf_len = frag->size;

		seg_num = (buf_len + MAX_TX_BUF_LEN - 1) / MAX_TX_BUF_LEN;
		for (i = 0; i < seg_num; i++) {
			use_tpd = atl1e_get_tpd(adapter);
			memcpy(use_tpd, tpd, sizeof(struct atl1e_tpd_desc));

			tx_buffer = atl1e_get_tx_buffer(adapter, use_tpd);
			if (tx_buffer->skb)
				BUG();

			tx_buffer->skb = NULL;
			tx_buffer->length =
				(buf_len > MAX_TX_BUF_LEN) ?
				MAX_TX_BUF_LEN : buf_len;
			buf_len -= tx_buffer->length;

			tx_buffer->dma =
				pci_map_page(adapter->pdev, frag->page,
						frag->page_offset +
						(i * MAX_TX_BUF_LEN),
						tx_buffer->length,
						PCI_DMA_TODEVICE);
			use_tpd->buffer_addr = cpu_to_le64(tx_buffer->dma);
			use_tpd->word2 = (use_tpd->word2 & (~TPD_BUFLEN_MASK)) |
					((cpu_to_le32(tx_buffer->length) &
					TPD_BUFLEN_MASK) << TPD_BUFLEN_SHIFT);
		}
	}

	if ((tpd->word3 >> TPD_SEGMENT_EN_SHIFT) & TPD_SEGMENT_EN_MASK)
		/* note this one is a tcp header */
		tpd->word3 |= 1 << TPD_HDRFLAG_SHIFT;
	/* The last tpd */

	use_tpd->word3 |= 1 << TPD_EOP_SHIFT;
	/* The last buffer info contain the skb address,
	   so it will be free after unmap */
	tx_buffer->skb = skb;
}

static void atl1e_tx_queue(struct atl1e_adapter *adapter, u16 count,
			   struct atl1e_tpd_desc *tpd)
{
	struct atl1e_tx_ring *tx_ring = &adapter->tx_ring;
	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64). */
	wmb();
	AT_WRITE_REG(&adapter->hw, REG_MB_TPD_PROD_IDX, tx_ring->next_to_use);
}

static int atl1e_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;
	u16 tpd_req = 1;
	struct atl1e_tpd_desc *tpd;

	if (test_bit(__AT_DOWN, &adapter->flags)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (unlikely(skb->len <= 0)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	tpd_req = atl1e_cal_tdp_req(skb);
	if (!spin_trylock_irqsave(&adapter->tx_lock, flags))
		return NETDEV_TX_LOCKED;

	if (atl1e_tpd_avail(adapter) < tpd_req) {
		/* no enough descriptor, just stop queue */
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&adapter->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	tpd = atl1e_get_tpd(adapter);

	if (unlikely(adapter->vlgrp && vlan_tx_tag_present(skb))) {
		u16 vlan_tag = vlan_tx_tag_get(skb);
		u16 atl1e_vlan_tag;

		tpd->word3 |= 1 << TPD_INS_VL_TAG_SHIFT;
		AT_VLAN_TAG_TO_TPD_TAG(vlan_tag, atl1e_vlan_tag);
		tpd->word2 |= (atl1e_vlan_tag & TPD_VLANTAG_MASK) <<
				TPD_VLAN_SHIFT;
	}

	if (skb->protocol == ntohs(ETH_P_8021Q))
		tpd->word3 |= 1 << TPD_VL_TAGGED_SHIFT;

	if (skb_network_offset(skb) != ETH_HLEN)
		tpd->word3 |= 1 << TPD_ETHTYPE_SHIFT; /* 802.3 frame */

	/* do TSO and check sum */
	if (atl1e_tso_csum(adapter, skb, tpd) != 0) {
		spin_unlock_irqrestore(&adapter->tx_lock, flags);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	atl1e_tx_map(adapter, skb, tpd);
	atl1e_tx_queue(adapter, tpd_req, tpd);

	netdev->trans_start = jiffies;
	spin_unlock_irqrestore(&adapter->tx_lock, flags);
	return NETDEV_TX_OK;
}

static void atl1e_free_irq(struct atl1e_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	free_irq(adapter->pdev->irq, netdev);

	if (adapter->have_msi)
		pci_disable_msi(adapter->pdev);
}

static int atl1e_request_irq(struct atl1e_adapter *adapter)
{
	struct pci_dev    *pdev   = adapter->pdev;
	struct net_device *netdev = adapter->netdev;
	int flags = 0;
	int err = 0;

	adapter->have_msi = true;
	err = pci_enable_msi(adapter->pdev);
	if (err) {
		dev_dbg(&pdev->dev,
			"Unable to allocate MSI interrupt Error: %d\n", err);
		adapter->have_msi = false;
	} else
		netdev->irq = pdev->irq;


	if (!adapter->have_msi)
		flags |= IRQF_SHARED;
	err = request_irq(adapter->pdev->irq, &atl1e_intr, flags,
			netdev->name, netdev);
	if (err) {
		dev_dbg(&pdev->dev,
			"Unable to allocate interrupt Error: %d\n", err);
		if (adapter->have_msi)
			pci_disable_msi(adapter->pdev);
		return err;
	}
	dev_dbg(&pdev->dev, "atl1e_request_irq OK\n");
	return err;
}

int atl1e_up(struct atl1e_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err = 0;
	u32 val;

	/* hardware has been reset, we need to reload some things */
	err = atl1e_init_hw(&adapter->hw);
	if (err) {
		err = -EIO;
		return err;
	}
	atl1e_init_ring_ptrs(adapter);
	atl1e_set_multi(netdev);
	atl1e_restore_vlan(adapter);

	if (atl1e_configure(adapter)) {
		err = -EIO;
		goto err_up;
	}

	clear_bit(__AT_DOWN, &adapter->flags);
	napi_enable(&adapter->napi);
	atl1e_irq_enable(adapter);
	val = AT_READ_REG(&adapter->hw, REG_MASTER_CTRL);
	AT_WRITE_REG(&adapter->hw, REG_MASTER_CTRL,
		      val | MASTER_CTRL_MANUAL_INT);

err_up:
	return err;
}

void atl1e_down(struct atl1e_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer */
	set_bit(__AT_DOWN, &adapter->flags);

#ifdef NETIF_F_LLTX
	netif_stop_queue(netdev);
#else
	netif_tx_disable(netdev);
#endif

	/* reset MAC to disable all RX/TX */
	atl1e_reset_hw(&adapter->hw);
	msleep(1);

	napi_disable(&adapter->napi);
	atl1e_del_timer(adapter);
	atl1e_irq_disable(adapter);

	netif_carrier_off(netdev);
	adapter->link_speed = SPEED_0;
	adapter->link_duplex = -1;
	atl1e_clean_tx_ring(adapter);
	atl1e_clean_rx_ring(adapter);
}

/*
 * atl1e_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 */
static int atl1e_open(struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	int err;

	/* disallow open during test */
	if (test_bit(__AT_TESTING, &adapter->flags))
		return -EBUSY;

	/* allocate rx/tx dma buffer & descriptors */
	atl1e_init_ring_resources(adapter);
	err = atl1e_setup_ring_resources(adapter);
	if (unlikely(err))
		return err;

	err = atl1e_request_irq(adapter);
	if (unlikely(err))
		goto err_req_irq;

	err = atl1e_up(adapter);
	if (unlikely(err))
		goto err_up;

	return 0;

err_up:
	atl1e_free_irq(adapter);
err_req_irq:
	atl1e_free_ring_resources(adapter);
	atl1e_reset_hw(&adapter->hw);

	return err;
}

/*
 * atl1e_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 */
static int atl1e_close(struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);

	WARN_ON(test_bit(__AT_RESETTING, &adapter->flags));
	atl1e_down(adapter);
	atl1e_free_irq(adapter);
	atl1e_free_ring_resources(adapter);

	return 0;
}

static int atl1e_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;
	u32 ctrl = 0;
	u32 mac_ctrl_data = 0;
	u32 wol_ctrl_data = 0;
	u16 mii_advertise_data = 0;
	u16 mii_bmsr_data = 0;
	u16 mii_intr_status_data = 0;
	u32 wufc = adapter->wol;
	u32 i;
#ifdef CONFIG_PM
	int retval = 0;
#endif

	if (netif_running(netdev)) {
		WARN_ON(test_bit(__AT_RESETTING, &adapter->flags));
		atl1e_down(adapter);
	}
	netif_device_detach(netdev);

#ifdef CONFIG_PM
	retval = pci_save_state(pdev);
	if (retval)
		return retval;
#endif

	if (wufc) {
		/* get link status */
		atl1e_read_phy_reg(hw, MII_BMSR, (u16 *)&mii_bmsr_data);
		atl1e_read_phy_reg(hw, MII_BMSR, (u16 *)&mii_bmsr_data);

		mii_advertise_data = MII_AR_10T_HD_CAPS;

		if ((atl1e_write_phy_reg(hw, MII_AT001_CR, 0) != 0) ||
		    (atl1e_write_phy_reg(hw,
			   MII_ADVERTISE, mii_advertise_data) != 0) ||
		    (atl1e_phy_commit(hw)) != 0) {
			dev_dbg(&pdev->dev, "set phy register failed\n");
			goto wol_dis;
		}

		hw->phy_configured = false; /* re-init PHY when resume */

		/* turn on magic packet wol */
		if (wufc & AT_WUFC_MAG)
			wol_ctrl_data |= WOL_MAGIC_EN | WOL_MAGIC_PME_EN;

		if (wufc & AT_WUFC_LNKC) {
		/* if orignal link status is link, just wait for retrive link */
			if (mii_bmsr_data & BMSR_LSTATUS) {
				for (i = 0; i < AT_SUSPEND_LINK_TIMEOUT; i++) {
					msleep(100);
					atl1e_read_phy_reg(hw, MII_BMSR,
							(u16 *)&mii_bmsr_data);
					if (mii_bmsr_data & BMSR_LSTATUS)
						break;
				}

				if ((mii_bmsr_data & BMSR_LSTATUS) == 0)
					dev_dbg(&pdev->dev,
						"%s: Link may change"
						"when suspend\n",
						atl1e_driver_name);
			}
			wol_ctrl_data |=  WOL_LINK_CHG_EN | WOL_LINK_CHG_PME_EN;
			/* only link up can wake up */
			if (atl1e_write_phy_reg(hw, MII_INT_CTRL, 0x400) != 0) {
				dev_dbg(&pdev->dev, "%s: read write phy "
						  "register failed.\n",
						  atl1e_driver_name);
				goto wol_dis;
			}
		}
		/* clear phy interrupt */
		atl1e_read_phy_reg(hw, MII_INT_STATUS, &mii_intr_status_data);
		/* Config MAC Ctrl register */
		mac_ctrl_data = MAC_CTRL_RX_EN;
		/* set to 10/100M halt duplex */
		mac_ctrl_data |= MAC_CTRL_SPEED_10_100 << MAC_CTRL_SPEED_SHIFT;
		mac_ctrl_data |= (((u32)adapter->hw.preamble_len &
				 MAC_CTRL_PRMLEN_MASK) <<
				 MAC_CTRL_PRMLEN_SHIFT);

		if (adapter->vlgrp)
			mac_ctrl_data |= MAC_CTRL_RMV_VLAN;

		/* magic packet maybe Broadcast&multicast&Unicast frame */
		if (wufc & AT_WUFC_MAG)
			mac_ctrl_data |= MAC_CTRL_BC_EN;

		dev_dbg(&pdev->dev,
			"%s: suspend MAC=0x%x\n",
			atl1e_driver_name, mac_ctrl_data);

		AT_WRITE_REG(hw, REG_WOL_CTRL, wol_ctrl_data);
		AT_WRITE_REG(hw, REG_MAC_CTRL, mac_ctrl_data);
		/* pcie patch */
		ctrl = AT_READ_REG(hw, REG_PCIE_PHYMISC);
		ctrl |= PCIE_PHYMISC_FORCE_RCV_DET;
		AT_WRITE_REG(hw, REG_PCIE_PHYMISC, ctrl);
		pci_enable_wake(pdev, pci_choose_state(pdev, state), 1);
		goto suspend_exit;
	}
wol_dis:

	/* WOL disabled */
	AT_WRITE_REG(hw, REG_WOL_CTRL, 0);

	/* pcie patch */
	ctrl = AT_READ_REG(hw, REG_PCIE_PHYMISC);
	ctrl |= PCIE_PHYMISC_FORCE_RCV_DET;
	AT_WRITE_REG(hw, REG_PCIE_PHYMISC, ctrl);

	atl1e_force_ps(hw);
	hw->phy_configured = false; /* re-init PHY when resume */

	pci_enable_wake(pdev, pci_choose_state(pdev, state), 0);

suspend_exit:

	if (netif_running(netdev))
		atl1e_free_irq(adapter);

	pci_disable_device(pdev);

	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

#ifdef CONFIG_PM
static int atl1e_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "ATL1e: Cannot enable PCI"
				" device from suspend\n");
		return err;
	}

	pci_set_master(pdev);

	AT_READ_REG(&adapter->hw, REG_WOL_CTRL); /* clear WOL status */

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	AT_WRITE_REG(&adapter->hw, REG_WOL_CTRL, 0);

	if (netif_running(netdev)) {
		err = atl1e_request_irq(adapter);
		if (err)
			return err;
	}

	atl1e_reset_hw(&adapter->hw);

	if (netif_running(netdev))
		atl1e_up(adapter);

	netif_device_attach(netdev);

	return 0;
}
#endif

static void atl1e_shutdown(struct pci_dev *pdev)
{
	atl1e_suspend(pdev, PMSG_SUSPEND);
}

static int atl1e_init_netdev(struct net_device *netdev, struct pci_dev *pdev)
{
	SET_NETDEV_DEV(netdev, &pdev->dev);
	pci_set_drvdata(pdev, netdev);

	netdev->irq  = pdev->irq;
	netdev->open = &atl1e_open;
	netdev->stop = &atl1e_close;
	netdev->hard_start_xmit = &atl1e_xmit_frame;
	netdev->get_stats = &atl1e_get_stats;
	netdev->set_multicast_list = &atl1e_set_multi;
	netdev->set_mac_address = &atl1e_set_mac_addr;
	netdev->change_mtu = &atl1e_change_mtu;
	netdev->do_ioctl = &atl1e_ioctl;
	netdev->tx_timeout = &atl1e_tx_timeout;
	netdev->watchdog_timeo = AT_TX_WATCHDOG;
	netdev->vlan_rx_register = atl1e_vlan_rx_register;
#ifdef CONFIG_NET_POLL_CONTROLLER
	netdev->poll_controller = atl1e_netpoll;
#endif
	atl1e_set_ethtool_ops(netdev);

	netdev->features = NETIF_F_SG | NETIF_F_HW_CSUM |
		NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	netdev->features |= NETIF_F_LLTX;
	netdev->features |= NETIF_F_TSO;
	netdev->features |= NETIF_F_TSO6;

	return 0;
}

/*
 * atl1e_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in atl1e_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * atl1e_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 */
static int __devinit atl1e_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct atl1e_adapter *adapter = NULL;
	static int cards_found;

	int err = 0;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		return err;
	}

	/*
	 * The atl1e chip can DMA to 64-bit addresses, but it uses a single
	 * shared register for the high 32 bits, so only a single, aligned,
	 * 4 GB physical address range can be used at a time.
	 *
	 * Supporting 64-bit DMA on this hardware is more trouble than it's
	 * worth.  It is far easier to limit to 32-bit DMA than update
	 * various kernel subsystems to support the mechanics required by a
	 * fixed-high-32-bit system.
	 */
	if ((pci_set_dma_mask(pdev, DMA_32BIT_MASK) != 0) ||
	    (pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK) != 0)) {
		dev_err(&pdev->dev, "No usable DMA configuration,aborting\n");
		goto err_dma;
	}

	err = pci_request_regions(pdev, atl1e_driver_name);
	if (err) {
		dev_err(&pdev->dev, "cannot obtain PCI resources\n");
		goto err_pci_reg;
	}

	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct atl1e_adapter));
	if (netdev == NULL) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "etherdev alloc failed\n");
		goto err_alloc_etherdev;
	}

	err = atl1e_init_netdev(netdev, pdev);
	if (err) {
		dev_err(&pdev->dev, "init netdevice failed\n");
		goto err_init_netdev;
	}
	adapter = netdev_priv(netdev);
	adapter->bd_number = cards_found;
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->hw.adapter = adapter;
	adapter->hw.hw_addr = pci_iomap(pdev, BAR_0, 0);
	if (!adapter->hw.hw_addr) {
		err = -EIO;
		dev_err(&pdev->dev, "cannot map device registers\n");
		goto err_ioremap;
	}
	netdev->base_addr = (unsigned long)adapter->hw.hw_addr;

	/* init mii data */
	adapter->mii.dev = netdev;
	adapter->mii.mdio_read  = atl1e_mdio_read;
	adapter->mii.mdio_write = atl1e_mdio_write;
	adapter->mii.phy_id_mask = 0x1f;
	adapter->mii.reg_num_mask = MDIO_REG_ADDR_MASK;

	netif_napi_add(netdev, &adapter->napi, atl1e_clean, 64);

	init_timer(&adapter->phy_config_timer);
	adapter->phy_config_timer.function = &atl1e_phy_config;
	adapter->phy_config_timer.data = (unsigned long) adapter;

	/* get user settings */
	atl1e_check_options(adapter);
	/*
	 * Mark all PCI regions associated with PCI device
	 * pdev as being reserved by owner atl1e_driver_name
	 * Enables bus-mastering on the device and calls
	 * pcibios_set_master to do the needed arch specific settings
	 */
	atl1e_setup_pcicmd(pdev);
	/* setup the private structure */
	err = atl1e_sw_init(adapter);
	if (err) {
		dev_err(&pdev->dev, "net device private data init failed\n");
		goto err_sw_init;
	}

	/* Init GPHY as early as possible due to power saving issue  */
	atl1e_phy_init(&adapter->hw);
	/* reset the controller to
	 * put the device in a known good starting state */
	err = atl1e_reset_hw(&adapter->hw);
	if (err) {
		err = -EIO;
		goto err_reset;
	}

	if (atl1e_read_mac_addr(&adapter->hw) != 0) {
		err = -EIO;
		dev_err(&pdev->dev, "get mac address failed\n");
		goto err_eeprom;
	}

	memcpy(netdev->dev_addr, adapter->hw.mac_addr, netdev->addr_len);
	memcpy(netdev->perm_addr, adapter->hw.mac_addr, netdev->addr_len);
	dev_dbg(&pdev->dev, "mac address : %02x-%02x-%02x-%02x-%02x-%02x\n",
			adapter->hw.mac_addr[0], adapter->hw.mac_addr[1],
			adapter->hw.mac_addr[2], adapter->hw.mac_addr[3],
			adapter->hw.mac_addr[4], adapter->hw.mac_addr[5]);

	INIT_WORK(&adapter->reset_task, atl1e_reset_task);
	INIT_WORK(&adapter->link_chg_task, atl1e_link_chg_task);
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "register netdevice failed\n");
		goto err_register;
	}

	/* assume we have no link for now */
	netif_stop_queue(netdev);
	netif_carrier_off(netdev);

	cards_found++;

	return 0;

err_reset:
err_register:
err_sw_init:
err_eeprom:
	iounmap(adapter->hw.hw_addr);
err_init_netdev:
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_regions(pdev);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/*
 * atl1e_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * atl1e_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 */
static void __devexit atl1e_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1e_adapter *adapter = netdev_priv(netdev);

	/*
	 * flush_scheduled work may reschedule our watchdog task, so
	 * explicitly disable watchdog tasks from being rescheduled
	 */
	set_bit(__AT_DOWN, &adapter->flags);

	atl1e_del_timer(adapter);
	atl1e_cancel_work(adapter);

	unregister_netdev(netdev);
	atl1e_free_ring_resources(adapter);
	atl1e_force_ps(&adapter->hw);
	iounmap(adapter->hw.hw_addr);
	pci_release_regions(pdev);
	free_netdev(netdev);
	pci_disable_device(pdev);
}

/*
 * atl1e_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t
atl1e_io_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1e_adapter *adapter = netdev->priv;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		atl1e_down(adapter);

	pci_disable_device(pdev);

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/*
 * atl1e_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * resembles the first-half of the e1000_resume routine.
 */
static pci_ers_result_t atl1e_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1e_adapter *adapter = netdev->priv;

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev,
		       "ATL1e: Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	atl1e_reset_hw(&adapter->hw);

	return PCI_ERS_RESULT_RECOVERED;
}

/*
 * atl1e_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the atl1e_resume routine.
 */
static void atl1e_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1e_adapter *adapter = netdev->priv;

	if (netif_running(netdev)) {
		if (atl1e_up(adapter)) {
			dev_err(&pdev->dev,
			  "ATL1e: can't bring device back up after reset\n");
			return;
		}
	}

	netif_device_attach(netdev);
}

static struct pci_error_handlers atl1e_err_handler = {
	.error_detected = atl1e_io_error_detected,
	.slot_reset = atl1e_io_slot_reset,
	.resume = atl1e_io_resume,
};

static struct pci_driver atl1e_driver = {
	.name     = atl1e_driver_name,
	.id_table = atl1e_pci_tbl,
	.probe    = atl1e_probe,
	.remove   = __devexit_p(atl1e_remove),
	/* Power Managment Hooks */
#ifdef CONFIG_PM
	.suspend  = atl1e_suspend,
	.resume   = atl1e_resume,
#endif
	.shutdown = atl1e_shutdown,
	.err_handler = &atl1e_err_handler
};

/*
 * atl1e_init_module - Driver Registration Routine
 *
 * atl1e_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init atl1e_init_module(void)
{
	return pci_register_driver(&atl1e_driver);
}

/*
 * atl1e_exit_module - Driver Exit Cleanup Routine
 *
 * atl1e_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit atl1e_exit_module(void)
{
	pci_unregister_driver(&atl1e_driver);
}

module_init(atl1e_init_module);
module_exit(atl1e_exit_module);
