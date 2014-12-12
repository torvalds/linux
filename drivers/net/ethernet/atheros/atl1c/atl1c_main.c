/*
 * Copyright(c) 2008 - 2009 Atheros Corporation. All rights reserved.
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

#include "atl1c.h"

#define ATL1C_DRV_VERSION "1.0.1.1-NAPI"
char atl1c_driver_name[] = "atl1c";
char atl1c_driver_version[] = ATL1C_DRV_VERSION;

/*
 * atl1c_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id atl1c_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATTANSIC_L1C)},
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATTANSIC_L2C)},
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATHEROS_L2C_B)},
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATHEROS_L2C_B2)},
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATHEROS_L1D)},
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATHEROS_L1D_2_0)},
	/* required last entry */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, atl1c_pci_tbl);

MODULE_AUTHOR("Jie Yang");
MODULE_AUTHOR("Qualcomm Atheros Inc., <nic-devel@qualcomm.com>");
MODULE_DESCRIPTION("Qualcomm Atheros 100/1000M Ethernet Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(ATL1C_DRV_VERSION);

static int atl1c_stop_mac(struct atl1c_hw *hw);
static void atl1c_disable_l0s_l1(struct atl1c_hw *hw);
static void atl1c_set_aspm(struct atl1c_hw *hw, u16 link_speed);
static void atl1c_start_mac(struct atl1c_adapter *adapter);
static void atl1c_clean_rx_irq(struct atl1c_adapter *adapter,
		   int *work_done, int work_to_do);
static int atl1c_up(struct atl1c_adapter *adapter);
static void atl1c_down(struct atl1c_adapter *adapter);
static int atl1c_reset_mac(struct atl1c_hw *hw);
static void atl1c_reset_dma_ring(struct atl1c_adapter *adapter);
static int atl1c_configure(struct atl1c_adapter *adapter);
static int atl1c_alloc_rx_buffer(struct atl1c_adapter *adapter);

static const u16 atl1c_pay_load_size[] = {
	128, 256, 512, 1024, 2048, 4096,
};


static const u32 atl1c_default_msg = NETIF_MSG_DRV | NETIF_MSG_PROBE |
	NETIF_MSG_LINK | NETIF_MSG_TIMER | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP;
static void atl1c_pcie_patch(struct atl1c_hw *hw)
{
	u32 mst_data, data;

	/* pclk sel could switch to 25M */
	AT_READ_REG(hw, REG_MASTER_CTRL, &mst_data);
	mst_data &= ~MASTER_CTRL_CLK_SEL_DIS;
	AT_WRITE_REG(hw, REG_MASTER_CTRL, mst_data);

	/* WoL/PCIE related settings */
	if (hw->nic_type == athr_l1c || hw->nic_type == athr_l2c) {
		AT_READ_REG(hw, REG_PCIE_PHYMISC, &data);
		data |= PCIE_PHYMISC_FORCE_RCV_DET;
		AT_WRITE_REG(hw, REG_PCIE_PHYMISC, data);
	} else { /* new dev set bit5 of MASTER */
		if (!(mst_data & MASTER_CTRL_WAKEN_25M))
			AT_WRITE_REG(hw, REG_MASTER_CTRL,
				mst_data | MASTER_CTRL_WAKEN_25M);
	}
	/* aspm/PCIE setting only for l2cb 1.0 */
	if (hw->nic_type == athr_l2c_b && hw->revision_id == L2CB_V10) {
		AT_READ_REG(hw, REG_PCIE_PHYMISC2, &data);
		data = FIELD_SETX(data, PCIE_PHYMISC2_CDR_BW,
			L2CB1_PCIE_PHYMISC2_CDR_BW);
		data = FIELD_SETX(data, PCIE_PHYMISC2_L0S_TH,
			L2CB1_PCIE_PHYMISC2_L0S_TH);
		AT_WRITE_REG(hw, REG_PCIE_PHYMISC2, data);
		/* extend L1 sync timer */
		AT_READ_REG(hw, REG_LINK_CTRL, &data);
		data |= LINK_CTRL_EXT_SYNC;
		AT_WRITE_REG(hw, REG_LINK_CTRL, data);
	}
	/* l2cb 1.x & l1d 1.x */
	if (hw->nic_type == athr_l2c_b || hw->nic_type == athr_l1d) {
		AT_READ_REG(hw, REG_PM_CTRL, &data);
		data |= PM_CTRL_L0S_BUFSRX_EN;
		AT_WRITE_REG(hw, REG_PM_CTRL, data);
		/* clear vendor msg */
		AT_READ_REG(hw, REG_DMA_DBG, &data);
		AT_WRITE_REG(hw, REG_DMA_DBG, data & ~DMA_DBG_VENDOR_MSG);
	}
}

/* FIXME: no need any more ? */
/*
 * atl1c_init_pcie - init PCIE module
 */
static void atl1c_reset_pcie(struct atl1c_hw *hw, u32 flag)
{
	u32 data;
	u32 pci_cmd;
	struct pci_dev *pdev = hw->adapter->pdev;
	int pos;

	AT_READ_REG(hw, PCI_COMMAND, &pci_cmd);
	pci_cmd &= ~PCI_COMMAND_INTX_DISABLE;
	pci_cmd |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
		PCI_COMMAND_IO);
	AT_WRITE_REG(hw, PCI_COMMAND, pci_cmd);

	/*
	 * Clear any PowerSaveing Settings
	 */
	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);
	/* wol sts read-clear */
	AT_READ_REG(hw, REG_WOL_CTRL, &data);
	AT_WRITE_REG(hw, REG_WOL_CTRL, 0);

	/*
	 * Mask some pcie error bits
	 */
	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ERR);
	if (pos) {
		pci_read_config_dword(pdev, pos + PCI_ERR_UNCOR_SEVER, &data);
		data &= ~(PCI_ERR_UNC_DLP | PCI_ERR_UNC_FCP);
		pci_write_config_dword(pdev, pos + PCI_ERR_UNCOR_SEVER, data);
	}
	/* clear error status */
	pcie_capability_write_word(pdev, PCI_EXP_DEVSTA,
			PCI_EXP_DEVSTA_NFED |
			PCI_EXP_DEVSTA_FED |
			PCI_EXP_DEVSTA_CED |
			PCI_EXP_DEVSTA_URD);

	AT_READ_REG(hw, REG_LTSSM_ID_CTRL, &data);
	data &= ~LTSSM_ID_EN_WRO;
	AT_WRITE_REG(hw, REG_LTSSM_ID_CTRL, data);

	atl1c_pcie_patch(hw);
	if (flag & ATL1C_PCIE_L0S_L1_DISABLE)
		atl1c_disable_l0s_l1(hw);

	msleep(5);
}

/**
 * atl1c_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 */
static inline void atl1c_irq_enable(struct atl1c_adapter *adapter)
{
	if (likely(atomic_dec_and_test(&adapter->irq_sem))) {
		AT_WRITE_REG(&adapter->hw, REG_ISR, 0x7FFFFFFF);
		AT_WRITE_REG(&adapter->hw, REG_IMR, adapter->hw.intr_mask);
		AT_WRITE_FLUSH(&adapter->hw);
	}
}

/**
 * atl1c_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 */
static inline void atl1c_irq_disable(struct atl1c_adapter *adapter)
{
	atomic_inc(&adapter->irq_sem);
	AT_WRITE_REG(&adapter->hw, REG_IMR, 0);
	AT_WRITE_REG(&adapter->hw, REG_ISR, ISR_DIS_INT);
	AT_WRITE_FLUSH(&adapter->hw);
	synchronize_irq(adapter->pdev->irq);
}

/**
 * atl1c_irq_reset - reset interrupt confiure on the NIC
 * @adapter: board private structure
 */
static inline void atl1c_irq_reset(struct atl1c_adapter *adapter)
{
	atomic_set(&adapter->irq_sem, 1);
	atl1c_irq_enable(adapter);
}

/*
 * atl1c_wait_until_idle - wait up to AT_HW_MAX_IDLE_DELAY reads
 * of the idle status register until the device is actually idle
 */
static u32 atl1c_wait_until_idle(struct atl1c_hw *hw, u32 modu_ctrl)
{
	int timeout;
	u32 data;

	for (timeout = 0; timeout < AT_HW_MAX_IDLE_DELAY; timeout++) {
		AT_READ_REG(hw, REG_IDLE_STATUS, &data);
		if ((data & modu_ctrl) == 0)
			return 0;
		msleep(1);
	}
	return data;
}

/**
 * atl1c_phy_config - Timer Call-back
 * @data: pointer to netdev cast into an unsigned long
 */
static void atl1c_phy_config(unsigned long data)
{
	struct atl1c_adapter *adapter = (struct atl1c_adapter *) data;
	struct atl1c_hw *hw = &adapter->hw;
	unsigned long flags;

	spin_lock_irqsave(&adapter->mdio_lock, flags);
	atl1c_restart_autoneg(hw);
	spin_unlock_irqrestore(&adapter->mdio_lock, flags);
}

void atl1c_reinit_locked(struct atl1c_adapter *adapter)
{
	WARN_ON(in_interrupt());
	atl1c_down(adapter);
	atl1c_up(adapter);
	clear_bit(__AT_RESETTING, &adapter->flags);
}

static void atl1c_check_link_status(struct atl1c_adapter *adapter)
{
	struct atl1c_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev    *pdev   = adapter->pdev;
	int err;
	unsigned long flags;
	u16 speed, duplex, phy_data;

	spin_lock_irqsave(&adapter->mdio_lock, flags);
	/* MII_BMSR must read twise */
	atl1c_read_phy_reg(hw, MII_BMSR, &phy_data);
	atl1c_read_phy_reg(hw, MII_BMSR, &phy_data);
	spin_unlock_irqrestore(&adapter->mdio_lock, flags);

	if ((phy_data & BMSR_LSTATUS) == 0) {
		/* link down */
		netif_carrier_off(netdev);
		hw->hibernate = true;
		if (atl1c_reset_mac(hw) != 0)
			if (netif_msg_hw(adapter))
				dev_warn(&pdev->dev, "reset mac failed\n");
		atl1c_set_aspm(hw, SPEED_0);
		atl1c_post_phy_linkchg(hw, SPEED_0);
		atl1c_reset_dma_ring(adapter);
		atl1c_configure(adapter);
	} else {
		/* Link Up */
		hw->hibernate = false;
		spin_lock_irqsave(&adapter->mdio_lock, flags);
		err = atl1c_get_speed_and_duplex(hw, &speed, &duplex);
		spin_unlock_irqrestore(&adapter->mdio_lock, flags);
		if (unlikely(err))
			return;
		/* link result is our setting */
		if (adapter->link_speed != speed ||
		    adapter->link_duplex != duplex) {
			adapter->link_speed  = speed;
			adapter->link_duplex = duplex;
			atl1c_set_aspm(hw, speed);
			atl1c_post_phy_linkchg(hw, speed);
			atl1c_start_mac(adapter);
			if (netif_msg_link(adapter))
				dev_info(&pdev->dev,
					"%s: %s NIC Link is Up<%d Mbps %s>\n",
					atl1c_driver_name, netdev->name,
					adapter->link_speed,
					adapter->link_duplex == FULL_DUPLEX ?
					"Full Duplex" : "Half Duplex");
		}
		if (!netif_carrier_ok(netdev))
			netif_carrier_on(netdev);
	}
}

static void atl1c_link_chg_event(struct atl1c_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev    *pdev   = adapter->pdev;
	u16 phy_data;
	u16 link_up;

	spin_lock(&adapter->mdio_lock);
	atl1c_read_phy_reg(&adapter->hw, MII_BMSR, &phy_data);
	atl1c_read_phy_reg(&adapter->hw, MII_BMSR, &phy_data);
	spin_unlock(&adapter->mdio_lock);
	link_up = phy_data & BMSR_LSTATUS;
	/* notify upper layer link down ASAP */
	if (!link_up) {
		if (netif_carrier_ok(netdev)) {
			/* old link state: Up */
			netif_carrier_off(netdev);
			if (netif_msg_link(adapter))
				dev_info(&pdev->dev,
					"%s: %s NIC Link is Down\n",
					atl1c_driver_name, netdev->name);
			adapter->link_speed = SPEED_0;
		}
	}

	set_bit(ATL1C_WORK_EVENT_LINK_CHANGE, &adapter->work_event);
	schedule_work(&adapter->common_task);
}

static void atl1c_common_task(struct work_struct *work)
{
	struct atl1c_adapter *adapter;
	struct net_device *netdev;

	adapter = container_of(work, struct atl1c_adapter, common_task);
	netdev = adapter->netdev;

	if (test_bit(__AT_DOWN, &adapter->flags))
		return;

	if (test_and_clear_bit(ATL1C_WORK_EVENT_RESET, &adapter->work_event)) {
		netif_device_detach(netdev);
		atl1c_down(adapter);
		atl1c_up(adapter);
		netif_device_attach(netdev);
	}

	if (test_and_clear_bit(ATL1C_WORK_EVENT_LINK_CHANGE,
		&adapter->work_event)) {
		atl1c_irq_disable(adapter);
		atl1c_check_link_status(adapter);
		atl1c_irq_enable(adapter);
	}
}


static void atl1c_del_timer(struct atl1c_adapter *adapter)
{
	del_timer_sync(&adapter->phy_config_timer);
}


/**
 * atl1c_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 */
static void atl1c_tx_timeout(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	set_bit(ATL1C_WORK_EVENT_RESET, &adapter->work_event);
	schedule_work(&adapter->common_task);
}

/**
 * atl1c_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 */
static void atl1c_set_multi(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct atl1c_hw *hw = &adapter->hw;
	struct netdev_hw_addr *ha;
	u32 mac_ctrl_data;
	u32 hash_value;

	/* Check for Promiscuous and All Multicast modes */
	AT_READ_REG(hw, REG_MAC_CTRL, &mac_ctrl_data);

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
	netdev_for_each_mc_addr(ha, netdev) {
		hash_value = atl1c_hash_mc_addr(hw, ha->addr);
		atl1c_hash_set(hw, hash_value);
	}
}

static void __atl1c_vlan_mode(netdev_features_t features, u32 *mac_ctrl_data)
{
	if (features & NETIF_F_HW_VLAN_CTAG_RX) {
		/* enable VLAN tag insert/strip */
		*mac_ctrl_data |= MAC_CTRL_RMV_VLAN;
	} else {
		/* disable VLAN tag insert/strip */
		*mac_ctrl_data &= ~MAC_CTRL_RMV_VLAN;
	}
}

static void atl1c_vlan_mode(struct net_device *netdev,
	netdev_features_t features)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	u32 mac_ctrl_data = 0;

	if (netif_msg_pktdata(adapter))
		dev_dbg(&pdev->dev, "atl1c_vlan_mode\n");

	atl1c_irq_disable(adapter);
	AT_READ_REG(&adapter->hw, REG_MAC_CTRL, &mac_ctrl_data);
	__atl1c_vlan_mode(features, &mac_ctrl_data);
	AT_WRITE_REG(&adapter->hw, REG_MAC_CTRL, mac_ctrl_data);
	atl1c_irq_enable(adapter);
}

static void atl1c_restore_vlan(struct atl1c_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	if (netif_msg_pktdata(adapter))
		dev_dbg(&pdev->dev, "atl1c_restore_vlan\n");
	atl1c_vlan_mode(adapter->netdev, adapter->netdev->features);
}

/**
 * atl1c_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 */
static int atl1c_set_mac_addr(struct net_device *netdev, void *p)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (netif_running(netdev))
		return -EBUSY;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac_addr, addr->sa_data, netdev->addr_len);

	atl1c_hw_set_mac_addr(&adapter->hw, adapter->hw.mac_addr);

	return 0;
}

static void atl1c_set_rxbufsize(struct atl1c_adapter *adapter,
				struct net_device *dev)
{
	unsigned int head_size;
	int mtu = dev->mtu;

	adapter->rx_buffer_len = mtu > AT_RX_BUF_SIZE ?
		roundup(mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN, 8) : AT_RX_BUF_SIZE;

	head_size = SKB_DATA_ALIGN(adapter->rx_buffer_len + NET_SKB_PAD) +
		    SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	adapter->rx_frag_size = roundup_pow_of_two(head_size);
}

static netdev_features_t atl1c_fix_features(struct net_device *netdev,
	netdev_features_t features)
{
	/*
	 * Since there is no support for separate rx/tx vlan accel
	 * enable/disable make sure tx flag is always in same state as rx.
	 */
	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		features |= NETIF_F_HW_VLAN_CTAG_TX;
	else
		features &= ~NETIF_F_HW_VLAN_CTAG_TX;

	if (netdev->mtu > MAX_TSO_FRAME_SIZE)
		features &= ~(NETIF_F_TSO | NETIF_F_TSO6);

	return features;
}

static int atl1c_set_features(struct net_device *netdev,
	netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;

	if (changed & NETIF_F_HW_VLAN_CTAG_RX)
		atl1c_vlan_mode(netdev, features);

	return 0;
}

/**
 * atl1c_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 */
static int atl1c_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct atl1c_hw *hw = &adapter->hw;
	int old_mtu   = netdev->mtu;
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

	/* Fast Ethernet controller doesn't support jumbo packet */
	if (((hw->nic_type == athr_l2c ||
	      hw->nic_type == athr_l2c_b ||
	      hw->nic_type == athr_l2c_b2) && new_mtu > ETH_DATA_LEN) ||
	      max_frame < ETH_ZLEN + ETH_FCS_LEN ||
	      max_frame > MAX_JUMBO_FRAME_SIZE) {
		if (netif_msg_link(adapter))
			dev_warn(&adapter->pdev->dev, "invalid MTU setting\n");
		return -EINVAL;
	}
	/* set MTU */
	if (old_mtu != new_mtu && netif_running(netdev)) {
		while (test_and_set_bit(__AT_RESETTING, &adapter->flags))
			msleep(1);
		netdev->mtu = new_mtu;
		adapter->hw.max_frame_size = new_mtu;
		atl1c_set_rxbufsize(adapter, netdev);
		atl1c_down(adapter);
		netdev_update_features(netdev);
		atl1c_up(adapter);
		clear_bit(__AT_RESETTING, &adapter->flags);
	}
	return 0;
}

/*
 *  caller should hold mdio_lock
 */
static int atl1c_mdio_read(struct net_device *netdev, int phy_id, int reg_num)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	u16 result;

	atl1c_read_phy_reg(&adapter->hw, reg_num, &result);
	return result;
}

static void atl1c_mdio_write(struct net_device *netdev, int phy_id,
			     int reg_num, int val)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	atl1c_write_phy_reg(&adapter->hw, reg_num, val);
}

static int atl1c_mii_ioctl(struct net_device *netdev,
			   struct ifreq *ifr, int cmd)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
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
		if (atl1c_read_phy_reg(&adapter->hw, data->reg_num & 0x1F,
				    &data->val_out)) {
			retval = -EIO;
			goto out;
		}
		break;

	case SIOCSMIIREG:
		if (data->reg_num & ~(0x1F)) {
			retval = -EFAULT;
			goto out;
		}

		dev_dbg(&pdev->dev, "<atl1c_mii_ioctl> write %x %x",
				data->reg_num, data->val_in);
		if (atl1c_write_phy_reg(&adapter->hw,
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

static int atl1c_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return atl1c_mii_ioctl(netdev, ifr, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * atl1c_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 */
static int atl1c_alloc_queues(struct atl1c_adapter *adapter)
{
	return 0;
}

static void atl1c_set_mac_type(struct atl1c_hw *hw)
{
	switch (hw->device_id) {
	case PCI_DEVICE_ID_ATTANSIC_L2C:
		hw->nic_type = athr_l2c;
		break;
	case PCI_DEVICE_ID_ATTANSIC_L1C:
		hw->nic_type = athr_l1c;
		break;
	case PCI_DEVICE_ID_ATHEROS_L2C_B:
		hw->nic_type = athr_l2c_b;
		break;
	case PCI_DEVICE_ID_ATHEROS_L2C_B2:
		hw->nic_type = athr_l2c_b2;
		break;
	case PCI_DEVICE_ID_ATHEROS_L1D:
		hw->nic_type = athr_l1d;
		break;
	case PCI_DEVICE_ID_ATHEROS_L1D_2_0:
		hw->nic_type = athr_l1d_2;
		break;
	default:
		break;
	}
}

static int atl1c_setup_mac_funcs(struct atl1c_hw *hw)
{
	u32 link_ctrl_data;

	atl1c_set_mac_type(hw);
	AT_READ_REG(hw, REG_LINK_CTRL, &link_ctrl_data);

	hw->ctrl_flags = ATL1C_INTR_MODRT_ENABLE  |
			 ATL1C_TXQ_MODE_ENHANCE;
	hw->ctrl_flags |= ATL1C_ASPM_L0S_SUPPORT |
			  ATL1C_ASPM_L1_SUPPORT;
	hw->ctrl_flags |= ATL1C_ASPM_CTRL_MON;

	if (hw->nic_type == athr_l1c ||
	    hw->nic_type == athr_l1d ||
	    hw->nic_type == athr_l1d_2)
		hw->link_cap_flags |= ATL1C_LINK_CAP_1000M;
	return 0;
}

struct atl1c_platform_patch {
	u16 pci_did;
	u8  pci_revid;
	u16 subsystem_vid;
	u16 subsystem_did;
	u32 patch_flag;
#define ATL1C_LINK_PATCH	0x1
};
static const struct atl1c_platform_patch plats[] = {
{0x2060, 0xC1, 0x1019, 0x8152, 0x1},
{0x2060, 0xC1, 0x1019, 0x2060, 0x1},
{0x2060, 0xC1, 0x1019, 0xE000, 0x1},
{0x2062, 0xC0, 0x1019, 0x8152, 0x1},
{0x2062, 0xC0, 0x1019, 0x2062, 0x1},
{0x2062, 0xC0, 0x1458, 0xE000, 0x1},
{0x2062, 0xC1, 0x1019, 0x8152, 0x1},
{0x2062, 0xC1, 0x1019, 0x2062, 0x1},
{0x2062, 0xC1, 0x1458, 0xE000, 0x1},
{0x2062, 0xC1, 0x1565, 0x2802, 0x1},
{0x2062, 0xC1, 0x1565, 0x2801, 0x1},
{0x1073, 0xC0, 0x1019, 0x8151, 0x1},
{0x1073, 0xC0, 0x1019, 0x1073, 0x1},
{0x1073, 0xC0, 0x1458, 0xE000, 0x1},
{0x1083, 0xC0, 0x1458, 0xE000, 0x1},
{0x1083, 0xC0, 0x1019, 0x8151, 0x1},
{0x1083, 0xC0, 0x1019, 0x1083, 0x1},
{0x1083, 0xC0, 0x1462, 0x7680, 0x1},
{0x1083, 0xC0, 0x1565, 0x2803, 0x1},
{0},
};

static void atl1c_patch_assign(struct atl1c_hw *hw)
{
	struct pci_dev	*pdev = hw->adapter->pdev;
	u32 misc_ctrl;
	int i = 0;

	hw->msi_lnkpatch = false;

	while (plats[i].pci_did != 0) {
		if (plats[i].pci_did == hw->device_id &&
		    plats[i].pci_revid == hw->revision_id &&
		    plats[i].subsystem_vid == hw->subsystem_vendor_id &&
		    plats[i].subsystem_did == hw->subsystem_id) {
			if (plats[i].patch_flag & ATL1C_LINK_PATCH)
				hw->msi_lnkpatch = true;
		}
		i++;
	}

	if (hw->device_id == PCI_DEVICE_ID_ATHEROS_L2C_B2 &&
	    hw->revision_id == L2CB_V21) {
		/* config acess mode */
		pci_write_config_dword(pdev, REG_PCIE_IND_ACC_ADDR,
				       REG_PCIE_DEV_MISC_CTRL);
		pci_read_config_dword(pdev, REG_PCIE_IND_ACC_DATA, &misc_ctrl);
		misc_ctrl &= ~0x100;
		pci_write_config_dword(pdev, REG_PCIE_IND_ACC_ADDR,
				       REG_PCIE_DEV_MISC_CTRL);
		pci_write_config_dword(pdev, REG_PCIE_IND_ACC_DATA, misc_ctrl);
	}
}
/**
 * atl1c_sw_init - Initialize general software structures (struct atl1c_adapter)
 * @adapter: board private structure to initialize
 *
 * atl1c_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 */
static int atl1c_sw_init(struct atl1c_adapter *adapter)
{
	struct atl1c_hw *hw   = &adapter->hw;
	struct pci_dev	*pdev = adapter->pdev;
	u32 revision;


	adapter->wol = 0;
	device_set_wakeup_enable(&pdev->dev, false);
	adapter->link_speed = SPEED_0;
	adapter->link_duplex = FULL_DUPLEX;
	adapter->tpd_ring[0].count = 1024;
	adapter->rfd_ring.count = 512;

	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_id = pdev->subsystem_device;
	pci_read_config_dword(pdev, PCI_CLASS_REVISION, &revision);
	hw->revision_id = revision & 0xFF;
	/* before link up, we assume hibernate is true */
	hw->hibernate = true;
	hw->media_type = MEDIA_TYPE_AUTO_SENSOR;
	if (atl1c_setup_mac_funcs(hw) != 0) {
		dev_err(&pdev->dev, "set mac function pointers failed\n");
		return -1;
	}
	atl1c_patch_assign(hw);

	hw->intr_mask = IMR_NORMAL_MASK;
	hw->phy_configured = false;
	hw->preamble_len = 7;
	hw->max_frame_size = adapter->netdev->mtu;
	hw->autoneg_advertised = ADVERTISED_Autoneg;
	hw->indirect_tab = 0xE4E4E4E4;
	hw->base_cpu = 0;

	hw->ict = 50000;		/* 100ms */
	hw->smb_timer = 200000;	  	/* 400ms */
	hw->rx_imt = 200;
	hw->tx_imt = 1000;

	hw->tpd_burst = 5;
	hw->rfd_burst = 8;
	hw->dma_order = atl1c_dma_ord_out;
	hw->dmar_block = atl1c_dma_req_1024;

	if (atl1c_alloc_queues(adapter)) {
		dev_err(&pdev->dev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}
	/* TODO */
	atl1c_set_rxbufsize(adapter, adapter->netdev);
	atomic_set(&adapter->irq_sem, 1);
	spin_lock_init(&adapter->mdio_lock);
	spin_lock_init(&adapter->tx_lock);
	set_bit(__AT_DOWN, &adapter->flags);

	return 0;
}

static inline void atl1c_clean_buffer(struct pci_dev *pdev,
				struct atl1c_buffer *buffer_info)
{
	u16 pci_driection;
	if (buffer_info->flags & ATL1C_BUFFER_FREE)
		return;
	if (buffer_info->dma) {
		if (buffer_info->flags & ATL1C_PCIMAP_FROMDEVICE)
			pci_driection = PCI_DMA_FROMDEVICE;
		else
			pci_driection = PCI_DMA_TODEVICE;

		if (buffer_info->flags & ATL1C_PCIMAP_SINGLE)
			pci_unmap_single(pdev, buffer_info->dma,
					buffer_info->length, pci_driection);
		else if (buffer_info->flags & ATL1C_PCIMAP_PAGE)
			pci_unmap_page(pdev, buffer_info->dma,
					buffer_info->length, pci_driection);
	}
	if (buffer_info->skb)
		dev_consume_skb_any(buffer_info->skb);
	buffer_info->dma = 0;
	buffer_info->skb = NULL;
	ATL1C_SET_BUFFER_STATE(buffer_info, ATL1C_BUFFER_FREE);
}
/**
 * atl1c_clean_tx_ring - Free Tx-skb
 * @adapter: board private structure
 */
static void atl1c_clean_tx_ring(struct atl1c_adapter *adapter,
				enum atl1c_trans_queue type)
{
	struct atl1c_tpd_ring *tpd_ring = &adapter->tpd_ring[type];
	struct atl1c_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	u16 index, ring_count;

	ring_count = tpd_ring->count;
	for (index = 0; index < ring_count; index++) {
		buffer_info = &tpd_ring->buffer_info[index];
		atl1c_clean_buffer(pdev, buffer_info);
	}

	/* Zero out Tx-buffers */
	memset(tpd_ring->desc, 0, sizeof(struct atl1c_tpd_desc) *
		ring_count);
	atomic_set(&tpd_ring->next_to_clean, 0);
	tpd_ring->next_to_use = 0;
}

/**
 * atl1c_clean_rx_ring - Free rx-reservation skbs
 * @adapter: board private structure
 */
static void atl1c_clean_rx_ring(struct atl1c_adapter *adapter)
{
	struct atl1c_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1c_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1c_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	int j;

	for (j = 0; j < rfd_ring->count; j++) {
		buffer_info = &rfd_ring->buffer_info[j];
		atl1c_clean_buffer(pdev, buffer_info);
	}
	/* zero out the descriptor ring */
	memset(rfd_ring->desc, 0, rfd_ring->size);
	rfd_ring->next_to_clean = 0;
	rfd_ring->next_to_use = 0;
	rrd_ring->next_to_use = 0;
	rrd_ring->next_to_clean = 0;
}

/*
 * Read / Write Ptr Initialize:
 */
static void atl1c_init_ring_ptrs(struct atl1c_adapter *adapter)
{
	struct atl1c_tpd_ring *tpd_ring = adapter->tpd_ring;
	struct atl1c_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1c_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1c_buffer *buffer_info;
	int i, j;

	for (i = 0; i < AT_MAX_TRANSMIT_QUEUE; i++) {
		tpd_ring[i].next_to_use = 0;
		atomic_set(&tpd_ring[i].next_to_clean, 0);
		buffer_info = tpd_ring[i].buffer_info;
		for (j = 0; j < tpd_ring->count; j++)
			ATL1C_SET_BUFFER_STATE(&buffer_info[i],
					ATL1C_BUFFER_FREE);
	}
	rfd_ring->next_to_use = 0;
	rfd_ring->next_to_clean = 0;
	rrd_ring->next_to_use = 0;
	rrd_ring->next_to_clean = 0;
	for (j = 0; j < rfd_ring->count; j++) {
		buffer_info = &rfd_ring->buffer_info[j];
		ATL1C_SET_BUFFER_STATE(buffer_info, ATL1C_BUFFER_FREE);
	}
}

/**
 * atl1c_free_ring_resources - Free Tx / RX descriptor Resources
 * @adapter: board private structure
 *
 * Free all transmit software resources
 */
static void atl1c_free_ring_resources(struct atl1c_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	pci_free_consistent(pdev, adapter->ring_header.size,
					adapter->ring_header.desc,
					adapter->ring_header.dma);
	adapter->ring_header.desc = NULL;

	/* Note: just free tdp_ring.buffer_info,
	*  it contain rfd_ring.buffer_info, do not double free */
	if (adapter->tpd_ring[0].buffer_info) {
		kfree(adapter->tpd_ring[0].buffer_info);
		adapter->tpd_ring[0].buffer_info = NULL;
	}
	if (adapter->rx_page) {
		put_page(adapter->rx_page);
		adapter->rx_page = NULL;
	}
}

/**
 * atl1c_setup_mem_resources - allocate Tx / RX descriptor resources
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 */
static int atl1c_setup_ring_resources(struct atl1c_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct atl1c_tpd_ring *tpd_ring = adapter->tpd_ring;
	struct atl1c_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1c_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1c_ring_header *ring_header = &adapter->ring_header;
	int size;
	int i;
	int count = 0;
	int rx_desc_count = 0;
	u32 offset = 0;

	rrd_ring->count = rfd_ring->count;
	for (i = 1; i < AT_MAX_TRANSMIT_QUEUE; i++)
		tpd_ring[i].count = tpd_ring[0].count;

	/* 2 tpd queue, one high priority queue,
	 * another normal priority queue */
	size = sizeof(struct atl1c_buffer) * (tpd_ring->count * 2 +
		rfd_ring->count);
	tpd_ring->buffer_info = kzalloc(size, GFP_KERNEL);
	if (unlikely(!tpd_ring->buffer_info))
		goto err_nomem;

	for (i = 0; i < AT_MAX_TRANSMIT_QUEUE; i++) {
		tpd_ring[i].buffer_info =
			(tpd_ring->buffer_info + count);
		count += tpd_ring[i].count;
	}

	rfd_ring->buffer_info =
		(tpd_ring->buffer_info + count);
	count += rfd_ring->count;
	rx_desc_count += rfd_ring->count;

	/*
	 * real ring DMA buffer
	 * each ring/block may need up to 8 bytes for alignment, hence the
	 * additional bytes tacked onto the end.
	 */
	ring_header->size = size =
		sizeof(struct atl1c_tpd_desc) * tpd_ring->count * 2 +
		sizeof(struct atl1c_rx_free_desc) * rx_desc_count +
		sizeof(struct atl1c_recv_ret_status) * rx_desc_count +
		8 * 4;

	ring_header->desc = pci_alloc_consistent(pdev, ring_header->size,
				&ring_header->dma);
	if (unlikely(!ring_header->desc)) {
		dev_err(&pdev->dev, "pci_alloc_consistend failed\n");
		goto err_nomem;
	}
	memset(ring_header->desc, 0, ring_header->size);
	/* init TPD ring */

	tpd_ring[0].dma = roundup(ring_header->dma, 8);
	offset = tpd_ring[0].dma - ring_header->dma;
	for (i = 0; i < AT_MAX_TRANSMIT_QUEUE; i++) {
		tpd_ring[i].dma = ring_header->dma + offset;
		tpd_ring[i].desc = (u8 *) ring_header->desc + offset;
		tpd_ring[i].size =
			sizeof(struct atl1c_tpd_desc) * tpd_ring[i].count;
		offset += roundup(tpd_ring[i].size, 8);
	}
	/* init RFD ring */
	rfd_ring->dma = ring_header->dma + offset;
	rfd_ring->desc = (u8 *) ring_header->desc + offset;
	rfd_ring->size = sizeof(struct atl1c_rx_free_desc) * rfd_ring->count;
	offset += roundup(rfd_ring->size, 8);

	/* init RRD ring */
	rrd_ring->dma = ring_header->dma + offset;
	rrd_ring->desc = (u8 *) ring_header->desc + offset;
	rrd_ring->size = sizeof(struct atl1c_recv_ret_status) *
		rrd_ring->count;
	offset += roundup(rrd_ring->size, 8);

	return 0;

err_nomem:
	kfree(tpd_ring->buffer_info);
	return -ENOMEM;
}

static void atl1c_configure_des_ring(struct atl1c_adapter *adapter)
{
	struct atl1c_hw *hw = &adapter->hw;
	struct atl1c_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1c_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1c_tpd_ring *tpd_ring = (struct atl1c_tpd_ring *)
				adapter->tpd_ring;

	/* TPD */
	AT_WRITE_REG(hw, REG_TX_BASE_ADDR_HI,
			(u32)((tpd_ring[atl1c_trans_normal].dma &
				AT_DMA_HI_ADDR_MASK) >> 32));
	/* just enable normal priority TX queue */
	AT_WRITE_REG(hw, REG_TPD_PRI0_ADDR_LO,
			(u32)(tpd_ring[atl1c_trans_normal].dma &
				AT_DMA_LO_ADDR_MASK));
	AT_WRITE_REG(hw, REG_TPD_PRI1_ADDR_LO,
			(u32)(tpd_ring[atl1c_trans_high].dma &
				AT_DMA_LO_ADDR_MASK));
	AT_WRITE_REG(hw, REG_TPD_RING_SIZE,
			(u32)(tpd_ring[0].count & TPD_RING_SIZE_MASK));


	/* RFD */
	AT_WRITE_REG(hw, REG_RX_BASE_ADDR_HI,
			(u32)((rfd_ring->dma & AT_DMA_HI_ADDR_MASK) >> 32));
	AT_WRITE_REG(hw, REG_RFD0_HEAD_ADDR_LO,
			(u32)(rfd_ring->dma & AT_DMA_LO_ADDR_MASK));

	AT_WRITE_REG(hw, REG_RFD_RING_SIZE,
			rfd_ring->count & RFD_RING_SIZE_MASK);
	AT_WRITE_REG(hw, REG_RX_BUF_SIZE,
			adapter->rx_buffer_len & RX_BUF_SIZE_MASK);

	/* RRD */
	AT_WRITE_REG(hw, REG_RRD0_HEAD_ADDR_LO,
			(u32)(rrd_ring->dma & AT_DMA_LO_ADDR_MASK));
	AT_WRITE_REG(hw, REG_RRD_RING_SIZE,
			(rrd_ring->count & RRD_RING_SIZE_MASK));

	if (hw->nic_type == athr_l2c_b) {
		AT_WRITE_REG(hw, REG_SRAM_RXF_LEN, 0x02a0L);
		AT_WRITE_REG(hw, REG_SRAM_TXF_LEN, 0x0100L);
		AT_WRITE_REG(hw, REG_SRAM_RXF_ADDR, 0x029f0000L);
		AT_WRITE_REG(hw, REG_SRAM_RFD0_INFO, 0x02bf02a0L);
		AT_WRITE_REG(hw, REG_SRAM_TXF_ADDR, 0x03bf02c0L);
		AT_WRITE_REG(hw, REG_SRAM_TRD_ADDR, 0x03df03c0L);
		AT_WRITE_REG(hw, REG_TXF_WATER_MARK, 0);	/* TX watermark, to enter l1 state.*/
		AT_WRITE_REG(hw, REG_RXD_DMA_CTRL, 0);		/* RXD threshold.*/
	}
	/* Load all of base address above */
	AT_WRITE_REG(hw, REG_LOAD_PTR, 1);
}

static void atl1c_configure_tx(struct atl1c_adapter *adapter)
{
	struct atl1c_hw *hw = &adapter->hw;
	int max_pay_load;
	u16 tx_offload_thresh;
	u32 txq_ctrl_data;

	tx_offload_thresh = MAX_TSO_FRAME_SIZE;
	AT_WRITE_REG(hw, REG_TX_TSO_OFFLOAD_THRESH,
		(tx_offload_thresh >> 3) & TX_TSO_OFFLOAD_THRESH_MASK);
	max_pay_load = pcie_get_readrq(adapter->pdev) >> 8;
	hw->dmar_block = min_t(u32, max_pay_load, hw->dmar_block);
	/*
	 * if BIOS had changed the dam-read-max-length to an invalid value,
	 * restore it to default value
	 */
	if (hw->dmar_block < DEVICE_CTRL_MAXRRS_MIN) {
		pcie_set_readrq(adapter->pdev, 128 << DEVICE_CTRL_MAXRRS_MIN);
		hw->dmar_block = DEVICE_CTRL_MAXRRS_MIN;
	}
	txq_ctrl_data =
		hw->nic_type == athr_l2c_b || hw->nic_type == athr_l2c_b2 ?
		L2CB_TXQ_CFGV : L1C_TXQ_CFGV;

	AT_WRITE_REG(hw, REG_TXQ_CTRL, txq_ctrl_data);
}

static void atl1c_configure_rx(struct atl1c_adapter *adapter)
{
	struct atl1c_hw *hw = &adapter->hw;
	u32 rxq_ctrl_data;

	rxq_ctrl_data = (hw->rfd_burst & RXQ_RFD_BURST_NUM_MASK) <<
			RXQ_RFD_BURST_NUM_SHIFT;

	if (hw->ctrl_flags & ATL1C_RX_IPV6_CHKSUM)
		rxq_ctrl_data |= IPV6_CHKSUM_CTRL_EN;

	/* aspm for gigabit */
	if (hw->nic_type != athr_l1d_2 && (hw->device_id & 1) != 0)
		rxq_ctrl_data = FIELD_SETX(rxq_ctrl_data, ASPM_THRUPUT_LIMIT,
			ASPM_THRUPUT_LIMIT_100M);

	AT_WRITE_REG(hw, REG_RXQ_CTRL, rxq_ctrl_data);
}

static void atl1c_configure_dma(struct atl1c_adapter *adapter)
{
	struct atl1c_hw *hw = &adapter->hw;
	u32 dma_ctrl_data;

	dma_ctrl_data = FIELDX(DMA_CTRL_RORDER_MODE, DMA_CTRL_RORDER_MODE_OUT) |
		DMA_CTRL_RREQ_PRI_DATA |
		FIELDX(DMA_CTRL_RREQ_BLEN, hw->dmar_block) |
		FIELDX(DMA_CTRL_WDLY_CNT, DMA_CTRL_WDLY_CNT_DEF) |
		FIELDX(DMA_CTRL_RDLY_CNT, DMA_CTRL_RDLY_CNT_DEF);

	AT_WRITE_REG(hw, REG_DMA_CTRL, dma_ctrl_data);
}

/*
 * Stop the mac, transmit and receive units
 * hw - Struct containing variables accessed by shared code
 * return : 0  or  idle status (if error)
 */
static int atl1c_stop_mac(struct atl1c_hw *hw)
{
	u32 data;

	AT_READ_REG(hw, REG_RXQ_CTRL, &data);
	data &= ~RXQ_CTRL_EN;
	AT_WRITE_REG(hw, REG_RXQ_CTRL, data);

	AT_READ_REG(hw, REG_TXQ_CTRL, &data);
	data &= ~TXQ_CTRL_EN;
	AT_WRITE_REG(hw, REG_TXQ_CTRL, data);

	atl1c_wait_until_idle(hw, IDLE_STATUS_RXQ_BUSY | IDLE_STATUS_TXQ_BUSY);

	AT_READ_REG(hw, REG_MAC_CTRL, &data);
	data &= ~(MAC_CTRL_TX_EN | MAC_CTRL_RX_EN);
	AT_WRITE_REG(hw, REG_MAC_CTRL, data);

	return (int)atl1c_wait_until_idle(hw,
		IDLE_STATUS_TXMAC_BUSY | IDLE_STATUS_RXMAC_BUSY);
}

static void atl1c_start_mac(struct atl1c_adapter *adapter)
{
	struct atl1c_hw *hw = &adapter->hw;
	u32 mac, txq, rxq;

	hw->mac_duplex = adapter->link_duplex == FULL_DUPLEX ? true : false;
	hw->mac_speed = adapter->link_speed == SPEED_1000 ?
		atl1c_mac_speed_1000 : atl1c_mac_speed_10_100;

	AT_READ_REG(hw, REG_TXQ_CTRL, &txq);
	AT_READ_REG(hw, REG_RXQ_CTRL, &rxq);
	AT_READ_REG(hw, REG_MAC_CTRL, &mac);

	txq |= TXQ_CTRL_EN;
	rxq |= RXQ_CTRL_EN;
	mac |= MAC_CTRL_TX_EN | MAC_CTRL_TX_FLOW |
	       MAC_CTRL_RX_EN | MAC_CTRL_RX_FLOW |
	       MAC_CTRL_ADD_CRC | MAC_CTRL_PAD |
	       MAC_CTRL_BC_EN | MAC_CTRL_SINGLE_PAUSE_EN |
	       MAC_CTRL_HASH_ALG_CRC32;
	if (hw->mac_duplex)
		mac |= MAC_CTRL_DUPLX;
	else
		mac &= ~MAC_CTRL_DUPLX;
	mac = FIELD_SETX(mac, MAC_CTRL_SPEED, hw->mac_speed);
	mac = FIELD_SETX(mac, MAC_CTRL_PRMLEN, hw->preamble_len);

	AT_WRITE_REG(hw, REG_TXQ_CTRL, txq);
	AT_WRITE_REG(hw, REG_RXQ_CTRL, rxq);
	AT_WRITE_REG(hw, REG_MAC_CTRL, mac);
}

/*
 * Reset the transmit and receive units; mask and clear all interrupts.
 * hw - Struct containing variables accessed by shared code
 * return : 0  or  idle status (if error)
 */
static int atl1c_reset_mac(struct atl1c_hw *hw)
{
	struct atl1c_adapter *adapter = hw->adapter;
	struct pci_dev *pdev = adapter->pdev;
	u32 ctrl_data = 0;

	atl1c_stop_mac(hw);
	/*
	 * Issue Soft Reset to the MAC.  This will reset the chip's
	 * transmit, receive, DMA.  It will not effect
	 * the current PCI configuration.  The global reset bit is self-
	 * clearing, and should clear within a microsecond.
	 */
	AT_READ_REG(hw, REG_MASTER_CTRL, &ctrl_data);
	ctrl_data |= MASTER_CTRL_OOB_DIS;
	AT_WRITE_REG(hw, REG_MASTER_CTRL, ctrl_data | MASTER_CTRL_SOFT_RST);

	AT_WRITE_FLUSH(hw);
	msleep(10);
	/* Wait at least 10ms for All module to be Idle */

	if (atl1c_wait_until_idle(hw, IDLE_STATUS_MASK)) {
		dev_err(&pdev->dev,
			"MAC state machine can't be idle since"
			" disabled for 10ms second\n");
		return -1;
	}
	AT_WRITE_REG(hw, REG_MASTER_CTRL, ctrl_data);

	/* driver control speed/duplex */
	AT_READ_REG(hw, REG_MAC_CTRL, &ctrl_data);
	AT_WRITE_REG(hw, REG_MAC_CTRL, ctrl_data | MAC_CTRL_SPEED_MODE_SW);

	/* clk switch setting */
	AT_READ_REG(hw, REG_SERDES, &ctrl_data);
	switch (hw->nic_type) {
	case athr_l2c_b:
		ctrl_data &= ~(SERDES_PHY_CLK_SLOWDOWN |
				SERDES_MAC_CLK_SLOWDOWN);
		AT_WRITE_REG(hw, REG_SERDES, ctrl_data);
		break;
	case athr_l2c_b2:
	case athr_l1d_2:
		ctrl_data |= SERDES_PHY_CLK_SLOWDOWN | SERDES_MAC_CLK_SLOWDOWN;
		AT_WRITE_REG(hw, REG_SERDES, ctrl_data);
		break;
	default:
		break;
	}

	return 0;
}

static void atl1c_disable_l0s_l1(struct atl1c_hw *hw)
{
	u16 ctrl_flags = hw->ctrl_flags;

	hw->ctrl_flags &= ~(ATL1C_ASPM_L0S_SUPPORT | ATL1C_ASPM_L1_SUPPORT);
	atl1c_set_aspm(hw, SPEED_0);
	hw->ctrl_flags = ctrl_flags;
}

/*
 * Set ASPM state.
 * Enable/disable L0s/L1 depend on link state.
 */
static void atl1c_set_aspm(struct atl1c_hw *hw, u16 link_speed)
{
	u32 pm_ctrl_data;
	u32 link_l1_timer;

	AT_READ_REG(hw, REG_PM_CTRL, &pm_ctrl_data);
	pm_ctrl_data &= ~(PM_CTRL_ASPM_L1_EN |
			  PM_CTRL_ASPM_L0S_EN |
			  PM_CTRL_MAC_ASPM_CHK);
	/* L1 timer */
	if (hw->nic_type == athr_l2c_b2 || hw->nic_type == athr_l1d_2) {
		pm_ctrl_data &= ~PMCTRL_TXL1_AFTER_L0S;
		link_l1_timer =
			link_speed == SPEED_1000 || link_speed == SPEED_100 ?
			L1D_PMCTRL_L1_ENTRY_TM_16US : 1;
		pm_ctrl_data = FIELD_SETX(pm_ctrl_data,
			L1D_PMCTRL_L1_ENTRY_TM, link_l1_timer);
	} else {
		link_l1_timer = hw->nic_type == athr_l2c_b ?
			L2CB1_PM_CTRL_L1_ENTRY_TM : L1C_PM_CTRL_L1_ENTRY_TM;
		if (link_speed != SPEED_1000 && link_speed != SPEED_100)
			link_l1_timer = 1;
		pm_ctrl_data = FIELD_SETX(pm_ctrl_data,
			PM_CTRL_L1_ENTRY_TIMER, link_l1_timer);
	}

	/* L0S/L1 enable */
	if ((hw->ctrl_flags & ATL1C_ASPM_L0S_SUPPORT) && link_speed != SPEED_0)
		pm_ctrl_data |= PM_CTRL_ASPM_L0S_EN | PM_CTRL_MAC_ASPM_CHK;
	if (hw->ctrl_flags & ATL1C_ASPM_L1_SUPPORT)
		pm_ctrl_data |= PM_CTRL_ASPM_L1_EN | PM_CTRL_MAC_ASPM_CHK;

	/* l2cb & l1d & l2cb2 & l1d2 */
	if (hw->nic_type == athr_l2c_b || hw->nic_type == athr_l1d ||
	    hw->nic_type == athr_l2c_b2 || hw->nic_type == athr_l1d_2) {
		pm_ctrl_data = FIELD_SETX(pm_ctrl_data,
			PM_CTRL_PM_REQ_TIMER, PM_CTRL_PM_REQ_TO_DEF);
		pm_ctrl_data |= PM_CTRL_RCVR_WT_TIMER |
				PM_CTRL_SERDES_PD_EX_L1 |
				PM_CTRL_CLK_SWH_L1;
		pm_ctrl_data &= ~(PM_CTRL_SERDES_L1_EN |
				  PM_CTRL_SERDES_PLL_L1_EN |
				  PM_CTRL_SERDES_BUFS_RX_L1_EN |
				  PM_CTRL_SA_DLY_EN |
				  PM_CTRL_HOTRST);
		/* disable l0s if link down or l2cb */
		if (link_speed == SPEED_0 || hw->nic_type == athr_l2c_b)
			pm_ctrl_data &= ~PM_CTRL_ASPM_L0S_EN;
	} else { /* l1c */
		pm_ctrl_data =
			FIELD_SETX(pm_ctrl_data, PM_CTRL_L1_ENTRY_TIMER, 0);
		if (link_speed != SPEED_0) {
			pm_ctrl_data |= PM_CTRL_SERDES_L1_EN |
					PM_CTRL_SERDES_PLL_L1_EN |
					PM_CTRL_SERDES_BUFS_RX_L1_EN;
			pm_ctrl_data &= ~(PM_CTRL_SERDES_PD_EX_L1 |
					  PM_CTRL_CLK_SWH_L1 |
					  PM_CTRL_ASPM_L0S_EN |
					  PM_CTRL_ASPM_L1_EN);
		} else { /* link down */
			pm_ctrl_data |= PM_CTRL_CLK_SWH_L1;
			pm_ctrl_data &= ~(PM_CTRL_SERDES_L1_EN |
					  PM_CTRL_SERDES_PLL_L1_EN |
					  PM_CTRL_SERDES_BUFS_RX_L1_EN |
					  PM_CTRL_ASPM_L0S_EN);
		}
	}
	AT_WRITE_REG(hw, REG_PM_CTRL, pm_ctrl_data);

	return;
}

/**
 * atl1c_configure - Configure Transmit&Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx /Rx unit of the MAC after a reset.
 */
static int atl1c_configure_mac(struct atl1c_adapter *adapter)
{
	struct atl1c_hw *hw = &adapter->hw;
	u32 master_ctrl_data = 0;
	u32 intr_modrt_data;
	u32 data;

	AT_READ_REG(hw, REG_MASTER_CTRL, &master_ctrl_data);
	master_ctrl_data &= ~(MASTER_CTRL_TX_ITIMER_EN |
			      MASTER_CTRL_RX_ITIMER_EN |
			      MASTER_CTRL_INT_RDCLR);
	/* clear interrupt status */
	AT_WRITE_REG(hw, REG_ISR, 0xFFFFFFFF);
	/*  Clear any WOL status */
	AT_WRITE_REG(hw, REG_WOL_CTRL, 0);
	/* set Interrupt Clear Timer
	 * HW will enable self to assert interrupt event to system after
	 * waiting x-time for software to notify it accept interrupt.
	 */

	data = CLK_GATING_EN_ALL;
	if (hw->ctrl_flags & ATL1C_CLK_GATING_EN) {
		if (hw->nic_type == athr_l2c_b)
			data &= ~CLK_GATING_RXMAC_EN;
	} else
		data = 0;
	AT_WRITE_REG(hw, REG_CLK_GATING_CTRL, data);

	AT_WRITE_REG(hw, REG_INT_RETRIG_TIMER,
		hw->ict & INT_RETRIG_TIMER_MASK);

	atl1c_configure_des_ring(adapter);

	if (hw->ctrl_flags & ATL1C_INTR_MODRT_ENABLE) {
		intr_modrt_data = (hw->tx_imt & IRQ_MODRT_TIMER_MASK) <<
					IRQ_MODRT_TX_TIMER_SHIFT;
		intr_modrt_data |= (hw->rx_imt & IRQ_MODRT_TIMER_MASK) <<
					IRQ_MODRT_RX_TIMER_SHIFT;
		AT_WRITE_REG(hw, REG_IRQ_MODRT_TIMER_INIT, intr_modrt_data);
		master_ctrl_data |=
			MASTER_CTRL_TX_ITIMER_EN | MASTER_CTRL_RX_ITIMER_EN;
	}

	if (hw->ctrl_flags & ATL1C_INTR_CLEAR_ON_READ)
		master_ctrl_data |= MASTER_CTRL_INT_RDCLR;

	master_ctrl_data |= MASTER_CTRL_SA_TIMER_EN;
	AT_WRITE_REG(hw, REG_MASTER_CTRL, master_ctrl_data);

	AT_WRITE_REG(hw, REG_SMB_STAT_TIMER,
		hw->smb_timer & SMB_STAT_TIMER_MASK);

	/* set MTU */
	AT_WRITE_REG(hw, REG_MTU, hw->max_frame_size + ETH_HLEN +
			VLAN_HLEN + ETH_FCS_LEN);

	atl1c_configure_tx(adapter);
	atl1c_configure_rx(adapter);
	atl1c_configure_dma(adapter);

	return 0;
}

static int atl1c_configure(struct atl1c_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int num;

	atl1c_init_ring_ptrs(adapter);
	atl1c_set_multi(netdev);
	atl1c_restore_vlan(adapter);

	num = atl1c_alloc_rx_buffer(adapter);
	if (unlikely(num == 0))
		return -ENOMEM;

	if (atl1c_configure_mac(adapter))
		return -EIO;

	return 0;
}

static void atl1c_update_hw_stats(struct atl1c_adapter *adapter)
{
	u16 hw_reg_addr = 0;
	unsigned long *stats_item = NULL;
	u32 data;

	/* update rx status */
	hw_reg_addr = REG_MAC_RX_STATUS_BIN;
	stats_item  = &adapter->hw_stats.rx_ok;
	while (hw_reg_addr <= REG_MAC_RX_STATUS_END) {
		AT_READ_REG(&adapter->hw, hw_reg_addr, &data);
		*stats_item += data;
		stats_item++;
		hw_reg_addr += 4;
	}
/* update tx status */
	hw_reg_addr = REG_MAC_TX_STATUS_BIN;
	stats_item  = &adapter->hw_stats.tx_ok;
	while (hw_reg_addr <= REG_MAC_TX_STATUS_END) {
		AT_READ_REG(&adapter->hw, hw_reg_addr, &data);
		*stats_item += data;
		stats_item++;
		hw_reg_addr += 4;
	}
}

/**
 * atl1c_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 */
static struct net_device_stats *atl1c_get_stats(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct atl1c_hw_stats  *hw_stats = &adapter->hw_stats;
	struct net_device_stats *net_stats = &netdev->stats;

	atl1c_update_hw_stats(adapter);
	net_stats->rx_bytes   = hw_stats->rx_byte_cnt;
	net_stats->tx_bytes   = hw_stats->tx_byte_cnt;
	net_stats->multicast  = hw_stats->rx_mcast;
	net_stats->collisions = hw_stats->tx_1_col +
				hw_stats->tx_2_col +
				hw_stats->tx_late_col +
				hw_stats->tx_abort_col;

	net_stats->rx_errors  = hw_stats->rx_frag +
				hw_stats->rx_fcs_err +
				hw_stats->rx_len_err +
				hw_stats->rx_sz_ov +
				hw_stats->rx_rrd_ov +
				hw_stats->rx_align_err +
				hw_stats->rx_rxf_ov;

	net_stats->rx_fifo_errors   = hw_stats->rx_rxf_ov;
	net_stats->rx_length_errors = hw_stats->rx_len_err;
	net_stats->rx_crc_errors    = hw_stats->rx_fcs_err;
	net_stats->rx_frame_errors  = hw_stats->rx_align_err;
	net_stats->rx_dropped       = hw_stats->rx_rrd_ov;

	net_stats->tx_errors = hw_stats->tx_late_col +
			       hw_stats->tx_abort_col +
			       hw_stats->tx_underrun +
			       hw_stats->tx_trunc;

	net_stats->tx_fifo_errors    = hw_stats->tx_underrun;
	net_stats->tx_aborted_errors = hw_stats->tx_abort_col;
	net_stats->tx_window_errors  = hw_stats->tx_late_col;

	net_stats->rx_packets = hw_stats->rx_ok + net_stats->rx_errors;
	net_stats->tx_packets = hw_stats->tx_ok + net_stats->tx_errors;

	return net_stats;
}

static inline void atl1c_clear_phy_int(struct atl1c_adapter *adapter)
{
	u16 phy_data;

	spin_lock(&adapter->mdio_lock);
	atl1c_read_phy_reg(&adapter->hw, MII_ISR, &phy_data);
	spin_unlock(&adapter->mdio_lock);
}

static bool atl1c_clean_tx_irq(struct atl1c_adapter *adapter,
				enum atl1c_trans_queue type)
{
	struct atl1c_tpd_ring *tpd_ring = &adapter->tpd_ring[type];
	struct atl1c_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	u16 next_to_clean = atomic_read(&tpd_ring->next_to_clean);
	u16 hw_next_to_clean;
	u16 reg;

	reg = type == atl1c_trans_high ? REG_TPD_PRI1_CIDX : REG_TPD_PRI0_CIDX;

	AT_READ_REGW(&adapter->hw, reg, &hw_next_to_clean);

	while (next_to_clean != hw_next_to_clean) {
		buffer_info = &tpd_ring->buffer_info[next_to_clean];
		atl1c_clean_buffer(pdev, buffer_info);
		if (++next_to_clean == tpd_ring->count)
			next_to_clean = 0;
		atomic_set(&tpd_ring->next_to_clean, next_to_clean);
	}

	if (netif_queue_stopped(adapter->netdev) &&
			netif_carrier_ok(adapter->netdev)) {
		netif_wake_queue(adapter->netdev);
	}

	return true;
}

/**
 * atl1c_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 */
static irqreturn_t atl1c_intr(int irq, void *data)
{
	struct net_device *netdev  = data;
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	struct atl1c_hw *hw = &adapter->hw;
	int max_ints = AT_MAX_INT_WORK;
	int handled = IRQ_NONE;
	u32 status;
	u32 reg_data;

	do {
		AT_READ_REG(hw, REG_ISR, &reg_data);
		status = reg_data & hw->intr_mask;

		if (status == 0 || (status & ISR_DIS_INT) != 0) {
			if (max_ints != AT_MAX_INT_WORK)
				handled = IRQ_HANDLED;
			break;
		}
		/* link event */
		if (status & ISR_GPHY)
			atl1c_clear_phy_int(adapter);
		/* Ack ISR */
		AT_WRITE_REG(hw, REG_ISR, status | ISR_DIS_INT);
		if (status & ISR_RX_PKT) {
			if (likely(napi_schedule_prep(&adapter->napi))) {
				hw->intr_mask &= ~ISR_RX_PKT;
				AT_WRITE_REG(hw, REG_IMR, hw->intr_mask);
				__napi_schedule(&adapter->napi);
			}
		}
		if (status & ISR_TX_PKT)
			atl1c_clean_tx_irq(adapter, atl1c_trans_normal);

		handled = IRQ_HANDLED;
		/* check if PCIE PHY Link down */
		if (status & ISR_ERROR) {
			if (netif_msg_hw(adapter))
				dev_err(&pdev->dev,
					"atl1c hardware error (status = 0x%x)\n",
					status & ISR_ERROR);
			/* reset MAC */
			set_bit(ATL1C_WORK_EVENT_RESET, &adapter->work_event);
			schedule_work(&adapter->common_task);
			return IRQ_HANDLED;
		}

		if (status & ISR_OVER)
			if (netif_msg_intr(adapter))
				dev_warn(&pdev->dev,
					"TX/RX overflow (status = 0x%x)\n",
					status & ISR_OVER);

		/* link event */
		if (status & (ISR_GPHY | ISR_MANUAL)) {
			netdev->stats.tx_carrier_errors++;
			atl1c_link_chg_event(adapter);
			break;
		}

	} while (--max_ints > 0);
	/* re-enable Interrupt*/
	AT_WRITE_REG(&adapter->hw, REG_ISR, 0);
	return handled;
}

static inline void atl1c_rx_checksum(struct atl1c_adapter *adapter,
		  struct sk_buff *skb, struct atl1c_recv_ret_status *prrs)
{
	/*
	 * The pid field in RRS in not correct sometimes, so we
	 * cannot figure out if the packet is fragmented or not,
	 * so we tell the KERNEL CHECKSUM_NONE
	 */
	skb_checksum_none_assert(skb);
}

static struct sk_buff *atl1c_alloc_skb(struct atl1c_adapter *adapter)
{
	struct sk_buff *skb;
	struct page *page;

	if (adapter->rx_frag_size > PAGE_SIZE)
		return netdev_alloc_skb(adapter->netdev,
					adapter->rx_buffer_len);

	page = adapter->rx_page;
	if (!page) {
		adapter->rx_page = page = alloc_page(GFP_ATOMIC);
		if (unlikely(!page))
			return NULL;
		adapter->rx_page_offset = 0;
	}

	skb = build_skb(page_address(page) + adapter->rx_page_offset,
			adapter->rx_frag_size);
	if (likely(skb)) {
		adapter->rx_page_offset += adapter->rx_frag_size;
		if (adapter->rx_page_offset >= PAGE_SIZE)
			adapter->rx_page = NULL;
		else
			get_page(page);
	}
	return skb;
}

static int atl1c_alloc_rx_buffer(struct atl1c_adapter *adapter)
{
	struct atl1c_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct pci_dev *pdev = adapter->pdev;
	struct atl1c_buffer *buffer_info, *next_info;
	struct sk_buff *skb;
	void *vir_addr = NULL;
	u16 num_alloc = 0;
	u16 rfd_next_to_use, next_next;
	struct atl1c_rx_free_desc *rfd_desc;
	dma_addr_t mapping;

	next_next = rfd_next_to_use = rfd_ring->next_to_use;
	if (++next_next == rfd_ring->count)
		next_next = 0;
	buffer_info = &rfd_ring->buffer_info[rfd_next_to_use];
	next_info = &rfd_ring->buffer_info[next_next];

	while (next_info->flags & ATL1C_BUFFER_FREE) {
		rfd_desc = ATL1C_RFD_DESC(rfd_ring, rfd_next_to_use);

		skb = atl1c_alloc_skb(adapter);
		if (unlikely(!skb)) {
			if (netif_msg_rx_err(adapter))
				dev_warn(&pdev->dev, "alloc rx buffer failed\n");
			break;
		}

		/*
		 * Make buffer alignment 2 beyond a 16 byte boundary
		 * this will result in a 16 byte aligned IP header after
		 * the 14 byte MAC header is removed
		 */
		vir_addr = skb->data;
		ATL1C_SET_BUFFER_STATE(buffer_info, ATL1C_BUFFER_BUSY);
		buffer_info->skb = skb;
		buffer_info->length = adapter->rx_buffer_len;
		mapping = pci_map_single(pdev, vir_addr,
						buffer_info->length,
						PCI_DMA_FROMDEVICE);
		if (unlikely(pci_dma_mapping_error(pdev, mapping))) {
			dev_kfree_skb(skb);
			buffer_info->skb = NULL;
			buffer_info->length = 0;
			ATL1C_SET_BUFFER_STATE(buffer_info, ATL1C_BUFFER_FREE);
			netif_warn(adapter, rx_err, adapter->netdev, "RX pci_map_single failed");
			break;
		}
		buffer_info->dma = mapping;
		ATL1C_SET_PCIMAP_TYPE(buffer_info, ATL1C_PCIMAP_SINGLE,
			ATL1C_PCIMAP_FROMDEVICE);
		rfd_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
		rfd_next_to_use = next_next;
		if (++next_next == rfd_ring->count)
			next_next = 0;
		buffer_info = &rfd_ring->buffer_info[rfd_next_to_use];
		next_info = &rfd_ring->buffer_info[next_next];
		num_alloc++;
	}

	if (num_alloc) {
		/* TODO: update mailbox here */
		wmb();
		rfd_ring->next_to_use = rfd_next_to_use;
		AT_WRITE_REG(&adapter->hw, REG_MB_RFD0_PROD_IDX,
			rfd_ring->next_to_use & MB_RFDX_PROD_IDX_MASK);
	}

	return num_alloc;
}

static void atl1c_clean_rrd(struct atl1c_rrd_ring *rrd_ring,
			struct	atl1c_recv_ret_status *rrs, u16 num)
{
	u16 i;
	/* the relationship between rrd and rfd is one map one */
	for (i = 0; i < num; i++, rrs = ATL1C_RRD_DESC(rrd_ring,
					rrd_ring->next_to_clean)) {
		rrs->word3 &= ~RRS_RXD_UPDATED;
		if (++rrd_ring->next_to_clean == rrd_ring->count)
			rrd_ring->next_to_clean = 0;
	}
}

static void atl1c_clean_rfd(struct atl1c_rfd_ring *rfd_ring,
	struct atl1c_recv_ret_status *rrs, u16 num)
{
	u16 i;
	u16 rfd_index;
	struct atl1c_buffer *buffer_info = rfd_ring->buffer_info;

	rfd_index = (rrs->word0 >> RRS_RX_RFD_INDEX_SHIFT) &
			RRS_RX_RFD_INDEX_MASK;
	for (i = 0; i < num; i++) {
		buffer_info[rfd_index].skb = NULL;
		ATL1C_SET_BUFFER_STATE(&buffer_info[rfd_index],
					ATL1C_BUFFER_FREE);
		if (++rfd_index == rfd_ring->count)
			rfd_index = 0;
	}
	rfd_ring->next_to_clean = rfd_index;
}

static void atl1c_clean_rx_irq(struct atl1c_adapter *adapter,
		   int *work_done, int work_to_do)
{
	u16 rfd_num, rfd_index;
	u16 count = 0;
	u16 length;
	struct pci_dev *pdev = adapter->pdev;
	struct net_device *netdev  = adapter->netdev;
	struct atl1c_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1c_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct sk_buff *skb;
	struct atl1c_recv_ret_status *rrs;
	struct atl1c_buffer *buffer_info;

	while (1) {
		if (*work_done >= work_to_do)
			break;
		rrs = ATL1C_RRD_DESC(rrd_ring, rrd_ring->next_to_clean);
		if (likely(RRS_RXD_IS_VALID(rrs->word3))) {
			rfd_num = (rrs->word0 >> RRS_RX_RFD_CNT_SHIFT) &
				RRS_RX_RFD_CNT_MASK;
			if (unlikely(rfd_num != 1))
				/* TODO support mul rfd*/
				if (netif_msg_rx_err(adapter))
					dev_warn(&pdev->dev,
						"Multi rfd not support yet!\n");
			goto rrs_checked;
		} else {
			break;
		}
rrs_checked:
		atl1c_clean_rrd(rrd_ring, rrs, rfd_num);
		if (rrs->word3 & (RRS_RX_ERR_SUM | RRS_802_3_LEN_ERR)) {
			atl1c_clean_rfd(rfd_ring, rrs, rfd_num);
				if (netif_msg_rx_err(adapter))
					dev_warn(&pdev->dev,
						"wrong packet! rrs word3 is %x\n",
						rrs->word3);
			continue;
		}

		length = le16_to_cpu((rrs->word3 >> RRS_PKT_SIZE_SHIFT) &
				RRS_PKT_SIZE_MASK);
		/* Good Receive */
		if (likely(rfd_num == 1)) {
			rfd_index = (rrs->word0 >> RRS_RX_RFD_INDEX_SHIFT) &
					RRS_RX_RFD_INDEX_MASK;
			buffer_info = &rfd_ring->buffer_info[rfd_index];
			pci_unmap_single(pdev, buffer_info->dma,
				buffer_info->length, PCI_DMA_FROMDEVICE);
			skb = buffer_info->skb;
		} else {
			/* TODO */
			if (netif_msg_rx_err(adapter))
				dev_warn(&pdev->dev,
					"Multi rfd not support yet!\n");
			break;
		}
		atl1c_clean_rfd(rfd_ring, rrs, rfd_num);
		skb_put(skb, length - ETH_FCS_LEN);
		skb->protocol = eth_type_trans(skb, netdev);
		atl1c_rx_checksum(adapter, skb, rrs);
		if (rrs->word3 & RRS_VLAN_INS) {
			u16 vlan;

			AT_TAG_TO_VLAN(rrs->vlan_tag, vlan);
			vlan = le16_to_cpu(vlan);
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan);
		}
		netif_receive_skb(skb);

		(*work_done)++;
		count++;
	}
	if (count)
		atl1c_alloc_rx_buffer(adapter);
}

/**
 * atl1c_clean - NAPI Rx polling callback
 */
static int atl1c_clean(struct napi_struct *napi, int budget)
{
	struct atl1c_adapter *adapter =
			container_of(napi, struct atl1c_adapter, napi);
	int work_done = 0;

	/* Keep link state information with original netdev */
	if (!netif_carrier_ok(adapter->netdev))
		goto quit_polling;
	/* just enable one RXQ */
	atl1c_clean_rx_irq(adapter, &work_done, budget);

	if (work_done < budget) {
quit_polling:
		napi_complete(napi);
		adapter->hw.intr_mask |= ISR_RX_PKT;
		AT_WRITE_REG(&adapter->hw, REG_IMR, adapter->hw.intr_mask);
	}
	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER

/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void atl1c_netpoll(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	disable_irq(adapter->pdev->irq);
	atl1c_intr(adapter->pdev->irq, netdev);
	enable_irq(adapter->pdev->irq);
}
#endif

static inline u16 atl1c_tpd_avail(struct atl1c_adapter *adapter, enum atl1c_trans_queue type)
{
	struct atl1c_tpd_ring *tpd_ring = &adapter->tpd_ring[type];
	u16 next_to_use = 0;
	u16 next_to_clean = 0;

	next_to_clean = atomic_read(&tpd_ring->next_to_clean);
	next_to_use   = tpd_ring->next_to_use;

	return (u16)(next_to_clean > next_to_use) ?
		(next_to_clean - next_to_use - 1) :
		(tpd_ring->count + next_to_clean - next_to_use - 1);
}

/*
 * get next usable tpd
 * Note: should call atl1c_tdp_avail to make sure
 * there is enough tpd to use
 */
static struct atl1c_tpd_desc *atl1c_get_tpd(struct atl1c_adapter *adapter,
	enum atl1c_trans_queue type)
{
	struct atl1c_tpd_ring *tpd_ring = &adapter->tpd_ring[type];
	struct atl1c_tpd_desc *tpd_desc;
	u16 next_to_use = 0;

	next_to_use = tpd_ring->next_to_use;
	if (++tpd_ring->next_to_use == tpd_ring->count)
		tpd_ring->next_to_use = 0;
	tpd_desc = ATL1C_TPD_DESC(tpd_ring, next_to_use);
	memset(tpd_desc, 0, sizeof(struct atl1c_tpd_desc));
	return	tpd_desc;
}

static struct atl1c_buffer *
atl1c_get_tx_buffer(struct atl1c_adapter *adapter, struct atl1c_tpd_desc *tpd)
{
	struct atl1c_tpd_ring *tpd_ring = adapter->tpd_ring;

	return &tpd_ring->buffer_info[tpd -
			(struct atl1c_tpd_desc *)tpd_ring->desc];
}

/* Calculate the transmit packet descript needed*/
static u16 atl1c_cal_tpd_req(const struct sk_buff *skb)
{
	u16 tpd_req;
	u16 proto_hdr_len = 0;

	tpd_req = skb_shinfo(skb)->nr_frags + 1;

	if (skb_is_gso(skb)) {
		proto_hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		if (proto_hdr_len < skb_headlen(skb))
			tpd_req++;
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			tpd_req++;
	}
	return tpd_req;
}

static int atl1c_tso_csum(struct atl1c_adapter *adapter,
			  struct sk_buff *skb,
			  struct atl1c_tpd_desc **tpd,
			  enum atl1c_trans_queue type)
{
	struct pci_dev *pdev = adapter->pdev;
	unsigned short offload_type;
	u8 hdr_len;
	u32 real_len;

	if (skb_is_gso(skb)) {
		int err;

		err = skb_cow_head(skb, 0);
		if (err < 0)
			return err;

		offload_type = skb_shinfo(skb)->gso_type;

		if (offload_type & SKB_GSO_TCPV4) {
			real_len = (((unsigned char *)ip_hdr(skb) - skb->data)
					+ ntohs(ip_hdr(skb)->tot_len));

			if (real_len < skb->len)
				pskb_trim(skb, real_len);

			hdr_len = (skb_transport_offset(skb) + tcp_hdrlen(skb));
			if (unlikely(skb->len == hdr_len)) {
				/* only xsum need */
				if (netif_msg_tx_queued(adapter))
					dev_warn(&pdev->dev,
						"IPV4 tso with zero data??\n");
				goto check_sum;
			} else {
				ip_hdr(skb)->check = 0;
				tcp_hdr(skb)->check = ~csum_tcpudp_magic(
							ip_hdr(skb)->saddr,
							ip_hdr(skb)->daddr,
							0, IPPROTO_TCP, 0);
				(*tpd)->word1 |= 1 << TPD_IPV4_PACKET_SHIFT;
			}
		}

		if (offload_type & SKB_GSO_TCPV6) {
			struct atl1c_tpd_ext_desc *etpd =
				*(struct atl1c_tpd_ext_desc **)(tpd);

			memset(etpd, 0, sizeof(struct atl1c_tpd_ext_desc));
			*tpd = atl1c_get_tpd(adapter, type);
			ipv6_hdr(skb)->payload_len = 0;
			/* check payload == 0 byte ? */
			hdr_len = (skb_transport_offset(skb) + tcp_hdrlen(skb));
			if (unlikely(skb->len == hdr_len)) {
				/* only xsum need */
				if (netif_msg_tx_queued(adapter))
					dev_warn(&pdev->dev,
						"IPV6 tso with zero data??\n");
				goto check_sum;
			} else
				tcp_hdr(skb)->check = ~csum_ipv6_magic(
						&ipv6_hdr(skb)->saddr,
						&ipv6_hdr(skb)->daddr,
						0, IPPROTO_TCP, 0);
			etpd->word1 |= 1 << TPD_LSO_EN_SHIFT;
			etpd->word1 |= 1 << TPD_LSO_VER_SHIFT;
			etpd->pkt_len = cpu_to_le32(skb->len);
			(*tpd)->word1 |= 1 << TPD_LSO_VER_SHIFT;
		}

		(*tpd)->word1 |= 1 << TPD_LSO_EN_SHIFT;
		(*tpd)->word1 |= (skb_transport_offset(skb) & TPD_TCPHDR_OFFSET_MASK) <<
				TPD_TCPHDR_OFFSET_SHIFT;
		(*tpd)->word1 |= (skb_shinfo(skb)->gso_size & TPD_MSS_MASK) <<
				TPD_MSS_SHIFT;
		return 0;
	}

check_sum:
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		u8 css, cso;
		cso = skb_checksum_start_offset(skb);

		if (unlikely(cso & 0x1)) {
			if (netif_msg_tx_err(adapter))
				dev_err(&adapter->pdev->dev,
					"payload offset should not an event number\n");
			return -1;
		} else {
			css = cso + skb->csum_offset;

			(*tpd)->word1 |= ((cso >> 1) & TPD_PLOADOFFSET_MASK) <<
					TPD_PLOADOFFSET_SHIFT;
			(*tpd)->word1 |= ((css >> 1) & TPD_CCSUM_OFFSET_MASK) <<
					TPD_CCSUM_OFFSET_SHIFT;
			(*tpd)->word1 |= 1 << TPD_CCSUM_EN_SHIFT;
		}
	}
	return 0;
}

static void atl1c_tx_rollback(struct atl1c_adapter *adpt,
			      struct atl1c_tpd_desc *first_tpd,
			      enum atl1c_trans_queue type)
{
	struct atl1c_tpd_ring *tpd_ring = &adpt->tpd_ring[type];
	struct atl1c_buffer *buffer_info;
	struct atl1c_tpd_desc *tpd;
	u16 first_index, index;

	first_index = first_tpd - (struct atl1c_tpd_desc *)tpd_ring->desc;
	index = first_index;
	while (index != tpd_ring->next_to_use) {
		tpd = ATL1C_TPD_DESC(tpd_ring, index);
		buffer_info = &tpd_ring->buffer_info[index];
		atl1c_clean_buffer(adpt->pdev, buffer_info);
		memset(tpd, 0, sizeof(struct atl1c_tpd_desc));
		if (++index == tpd_ring->count)
			index = 0;
	}
	tpd_ring->next_to_use = first_index;
}

static int atl1c_tx_map(struct atl1c_adapter *adapter,
		      struct sk_buff *skb, struct atl1c_tpd_desc *tpd,
			enum atl1c_trans_queue type)
{
	struct atl1c_tpd_desc *use_tpd = NULL;
	struct atl1c_buffer *buffer_info = NULL;
	u16 buf_len = skb_headlen(skb);
	u16 map_len = 0;
	u16 mapped_len = 0;
	u16 hdr_len = 0;
	u16 nr_frags;
	u16 f;
	int tso;

	nr_frags = skb_shinfo(skb)->nr_frags;
	tso = (tpd->word1 >> TPD_LSO_EN_SHIFT) & TPD_LSO_EN_MASK;
	if (tso) {
		/* TSO */
		map_len = hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		use_tpd = tpd;

		buffer_info = atl1c_get_tx_buffer(adapter, use_tpd);
		buffer_info->length = map_len;
		buffer_info->dma = pci_map_single(adapter->pdev,
					skb->data, hdr_len, PCI_DMA_TODEVICE);
		if (unlikely(pci_dma_mapping_error(adapter->pdev,
						   buffer_info->dma)))
			goto err_dma;
		ATL1C_SET_BUFFER_STATE(buffer_info, ATL1C_BUFFER_BUSY);
		ATL1C_SET_PCIMAP_TYPE(buffer_info, ATL1C_PCIMAP_SINGLE,
			ATL1C_PCIMAP_TODEVICE);
		mapped_len += map_len;
		use_tpd->buffer_addr = cpu_to_le64(buffer_info->dma);
		use_tpd->buffer_len = cpu_to_le16(buffer_info->length);
	}

	if (mapped_len < buf_len) {
		/* mapped_len == 0, means we should use the first tpd,
		   which is given by caller  */
		if (mapped_len == 0)
			use_tpd = tpd;
		else {
			use_tpd = atl1c_get_tpd(adapter, type);
			memcpy(use_tpd, tpd, sizeof(struct atl1c_tpd_desc));
		}
		buffer_info = atl1c_get_tx_buffer(adapter, use_tpd);
		buffer_info->length = buf_len - mapped_len;
		buffer_info->dma =
			pci_map_single(adapter->pdev, skb->data + mapped_len,
					buffer_info->length, PCI_DMA_TODEVICE);
		if (unlikely(pci_dma_mapping_error(adapter->pdev,
						   buffer_info->dma)))
			goto err_dma;

		ATL1C_SET_BUFFER_STATE(buffer_info, ATL1C_BUFFER_BUSY);
		ATL1C_SET_PCIMAP_TYPE(buffer_info, ATL1C_PCIMAP_SINGLE,
			ATL1C_PCIMAP_TODEVICE);
		use_tpd->buffer_addr = cpu_to_le64(buffer_info->dma);
		use_tpd->buffer_len  = cpu_to_le16(buffer_info->length);
	}

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];

		use_tpd = atl1c_get_tpd(adapter, type);
		memcpy(use_tpd, tpd, sizeof(struct atl1c_tpd_desc));

		buffer_info = atl1c_get_tx_buffer(adapter, use_tpd);
		buffer_info->length = skb_frag_size(frag);
		buffer_info->dma = skb_frag_dma_map(&adapter->pdev->dev,
						    frag, 0,
						    buffer_info->length,
						    DMA_TO_DEVICE);
		if (dma_mapping_error(&adapter->pdev->dev, buffer_info->dma))
			goto err_dma;

		ATL1C_SET_BUFFER_STATE(buffer_info, ATL1C_BUFFER_BUSY);
		ATL1C_SET_PCIMAP_TYPE(buffer_info, ATL1C_PCIMAP_PAGE,
			ATL1C_PCIMAP_TODEVICE);
		use_tpd->buffer_addr = cpu_to_le64(buffer_info->dma);
		use_tpd->buffer_len  = cpu_to_le16(buffer_info->length);
	}

	/* The last tpd */
	use_tpd->word1 |= 1 << TPD_EOP_SHIFT;
	/* The last buffer info contain the skb address,
	   so it will be free after unmap */
	buffer_info->skb = skb;

	return 0;

err_dma:
	buffer_info->dma = 0;
	buffer_info->length = 0;
	return -1;
}

static void atl1c_tx_queue(struct atl1c_adapter *adapter, struct sk_buff *skb,
			   struct atl1c_tpd_desc *tpd, enum atl1c_trans_queue type)
{
	struct atl1c_tpd_ring *tpd_ring = &adapter->tpd_ring[type];
	u16 reg;

	reg = type == atl1c_trans_high ? REG_TPD_PRI1_PIDX : REG_TPD_PRI0_PIDX;
	AT_WRITE_REGW(&adapter->hw, reg, tpd_ring->next_to_use);
}

static netdev_tx_t atl1c_xmit_frame(struct sk_buff *skb,
					  struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;
	u16 tpd_req = 1;
	struct atl1c_tpd_desc *tpd;
	enum atl1c_trans_queue type = atl1c_trans_normal;

	if (test_bit(__AT_DOWN, &adapter->flags)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	tpd_req = atl1c_cal_tpd_req(skb);
	if (!spin_trylock_irqsave(&adapter->tx_lock, flags)) {
		if (netif_msg_pktdata(adapter))
			dev_info(&adapter->pdev->dev, "tx locked\n");
		return NETDEV_TX_LOCKED;
	}

	if (atl1c_tpd_avail(adapter, type) < tpd_req) {
		/* no enough descriptor, just stop queue */
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&adapter->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	tpd = atl1c_get_tpd(adapter, type);

	/* do TSO and check sum */
	if (atl1c_tso_csum(adapter, skb, &tpd, type) != 0) {
		spin_unlock_irqrestore(&adapter->tx_lock, flags);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (unlikely(vlan_tx_tag_present(skb))) {
		u16 vlan = vlan_tx_tag_get(skb);
		__le16 tag;

		vlan = cpu_to_le16(vlan);
		AT_VLAN_TO_TAG(vlan, tag);
		tpd->word1 |= 1 << TPD_INS_VTAG_SHIFT;
		tpd->vlan_tag = tag;
	}

	if (skb_network_offset(skb) != ETH_HLEN)
		tpd->word1 |= 1 << TPD_ETH_TYPE_SHIFT; /* Ethernet frame */

	if (atl1c_tx_map(adapter, skb, tpd, type) < 0) {
		netif_info(adapter, tx_done, adapter->netdev,
			   "tx-skb droppted due to dma error\n");
		/* roll back tpd/buffer */
		atl1c_tx_rollback(adapter, tpd, type);
		spin_unlock_irqrestore(&adapter->tx_lock, flags);
		dev_kfree_skb_any(skb);
	} else {
		atl1c_tx_queue(adapter, skb, tpd, type);
		spin_unlock_irqrestore(&adapter->tx_lock, flags);
	}

	return NETDEV_TX_OK;
}

static void atl1c_free_irq(struct atl1c_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	free_irq(adapter->pdev->irq, netdev);

	if (adapter->have_msi)
		pci_disable_msi(adapter->pdev);
}

static int atl1c_request_irq(struct atl1c_adapter *adapter)
{
	struct pci_dev    *pdev   = adapter->pdev;
	struct net_device *netdev = adapter->netdev;
	int flags = 0;
	int err = 0;

	adapter->have_msi = true;
	err = pci_enable_msi(adapter->pdev);
	if (err) {
		if (netif_msg_ifup(adapter))
			dev_err(&pdev->dev,
				"Unable to allocate MSI interrupt Error: %d\n",
				err);
		adapter->have_msi = false;
	}

	if (!adapter->have_msi)
		flags |= IRQF_SHARED;
	err = request_irq(adapter->pdev->irq, atl1c_intr, flags,
			netdev->name, netdev);
	if (err) {
		if (netif_msg_ifup(adapter))
			dev_err(&pdev->dev,
				"Unable to allocate interrupt Error: %d\n",
				err);
		if (adapter->have_msi)
			pci_disable_msi(adapter->pdev);
		return err;
	}
	if (netif_msg_ifup(adapter))
		dev_dbg(&pdev->dev, "atl1c_request_irq OK\n");
	return err;
}


static void atl1c_reset_dma_ring(struct atl1c_adapter *adapter)
{
	/* release tx-pending skbs and reset tx/rx ring index */
	atl1c_clean_tx_ring(adapter, atl1c_trans_normal);
	atl1c_clean_tx_ring(adapter, atl1c_trans_high);
	atl1c_clean_rx_ring(adapter);
}

static int atl1c_up(struct atl1c_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	netif_carrier_off(netdev);

	err = atl1c_configure(adapter);
	if (unlikely(err))
		goto err_up;

	err = atl1c_request_irq(adapter);
	if (unlikely(err))
		goto err_up;

	atl1c_check_link_status(adapter);
	clear_bit(__AT_DOWN, &adapter->flags);
	napi_enable(&adapter->napi);
	atl1c_irq_enable(adapter);
	netif_start_queue(netdev);
	return err;

err_up:
	atl1c_clean_rx_ring(adapter);
	return err;
}

static void atl1c_down(struct atl1c_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	atl1c_del_timer(adapter);
	adapter->work_event = 0; /* clear all event */
	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer */
	set_bit(__AT_DOWN, &adapter->flags);
	netif_carrier_off(netdev);
	napi_disable(&adapter->napi);
	atl1c_irq_disable(adapter);
	atl1c_free_irq(adapter);
	/* disable ASPM if device inactive */
	atl1c_disable_l0s_l1(&adapter->hw);
	/* reset MAC to disable all RX/TX */
	atl1c_reset_mac(&adapter->hw);
	msleep(1);

	adapter->link_speed = SPEED_0;
	adapter->link_duplex = -1;
	atl1c_reset_dma_ring(adapter);
}

/**
 * atl1c_open - Called when a network interface is made active
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
static int atl1c_open(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	int err;

	/* disallow open during test */
	if (test_bit(__AT_TESTING, &adapter->flags))
		return -EBUSY;

	/* allocate rx/tx dma buffer & descriptors */
	err = atl1c_setup_ring_resources(adapter);
	if (unlikely(err))
		return err;

	err = atl1c_up(adapter);
	if (unlikely(err))
		goto err_up;

	return 0;

err_up:
	atl1c_free_irq(adapter);
	atl1c_free_ring_resources(adapter);
	atl1c_reset_mac(&adapter->hw);
	return err;
}

/**
 * atl1c_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 */
static int atl1c_close(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	WARN_ON(test_bit(__AT_RESETTING, &adapter->flags));
	set_bit(__AT_DOWN, &adapter->flags);
	cancel_work_sync(&adapter->common_task);
	atl1c_down(adapter);
	atl1c_free_ring_resources(adapter);
	return 0;
}

static int atl1c_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct atl1c_hw *hw = &adapter->hw;
	u32 wufc = adapter->wol;

	atl1c_disable_l0s_l1(hw);
	if (netif_running(netdev)) {
		WARN_ON(test_bit(__AT_RESETTING, &adapter->flags));
		atl1c_down(adapter);
	}
	netif_device_detach(netdev);

	if (wufc)
		if (atl1c_phy_to_ps_link(hw) != 0)
			dev_dbg(&pdev->dev, "phy power saving failed");

	atl1c_power_saving(hw, wufc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int atl1c_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	AT_WRITE_REG(&adapter->hw, REG_WOL_CTRL, 0);
	atl1c_reset_pcie(&adapter->hw, ATL1C_PCIE_L0S_L1_DISABLE);

	atl1c_phy_reset(&adapter->hw);
	atl1c_reset_mac(&adapter->hw);
	atl1c_phy_init(&adapter->hw);

#if 0
	AT_READ_REG(&adapter->hw, REG_PM_CTRLSTAT, &pm_data);
	pm_data &= ~PM_CTRLSTAT_PME_EN;
	AT_WRITE_REG(&adapter->hw, REG_PM_CTRLSTAT, pm_data);
#endif

	netif_device_attach(netdev);
	if (netif_running(netdev))
		atl1c_up(adapter);

	return 0;
}
#endif

static void atl1c_shutdown(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	atl1c_suspend(&pdev->dev);
	pci_wake_from_d3(pdev, adapter->wol);
	pci_set_power_state(pdev, PCI_D3hot);
}

static const struct net_device_ops atl1c_netdev_ops = {
	.ndo_open		= atl1c_open,
	.ndo_stop		= atl1c_close,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_start_xmit		= atl1c_xmit_frame,
	.ndo_set_mac_address	= atl1c_set_mac_addr,
	.ndo_set_rx_mode	= atl1c_set_multi,
	.ndo_change_mtu		= atl1c_change_mtu,
	.ndo_fix_features	= atl1c_fix_features,
	.ndo_set_features	= atl1c_set_features,
	.ndo_do_ioctl		= atl1c_ioctl,
	.ndo_tx_timeout		= atl1c_tx_timeout,
	.ndo_get_stats		= atl1c_get_stats,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= atl1c_netpoll,
#endif
};

static int atl1c_init_netdev(struct net_device *netdev, struct pci_dev *pdev)
{
	SET_NETDEV_DEV(netdev, &pdev->dev);
	pci_set_drvdata(pdev, netdev);

	netdev->netdev_ops = &atl1c_netdev_ops;
	netdev->watchdog_timeo = AT_TX_WATCHDOG;
	atl1c_set_ethtool_ops(netdev);

	/* TODO: add when ready */
	netdev->hw_features =	NETIF_F_SG		|
				NETIF_F_HW_CSUM		|
				NETIF_F_HW_VLAN_CTAG_RX	|
				NETIF_F_TSO		|
				NETIF_F_TSO6;
	netdev->features =	netdev->hw_features	|
				NETIF_F_HW_VLAN_CTAG_TX;
	return 0;
}

/**
 * atl1c_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in atl1c_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * atl1c_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 */
static int atl1c_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct atl1c_adapter *adapter;
	static int cards_found;

	int err = 0;

	/* enable device (incl. PCI PM wakeup and hotplug setup) */
	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		return err;
	}

	/*
	 * The atl1c chip can DMA to 64-bit addresses, but it uses a single
	 * shared register for the high 32 bits, so only a single, aligned,
	 * 4 GB physical address range can be used at a time.
	 *
	 * Supporting 64-bit DMA on this hardware is more trouble than it's
	 * worth.  It is far easier to limit to 32-bit DMA than update
	 * various kernel subsystems to support the mechanics required by a
	 * fixed-high-32-bit system.
	 */
	if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) != 0) ||
	    (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)) != 0)) {
		dev_err(&pdev->dev, "No usable DMA configuration,aborting\n");
		goto err_dma;
	}

	err = pci_request_regions(pdev, atl1c_driver_name);
	if (err) {
		dev_err(&pdev->dev, "cannot obtain PCI resources\n");
		goto err_pci_reg;
	}

	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct atl1c_adapter));
	if (netdev == NULL) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	err = atl1c_init_netdev(netdev, pdev);
	if (err) {
		dev_err(&pdev->dev, "init netdevice failed\n");
		goto err_init_netdev;
	}
	adapter = netdev_priv(netdev);
	adapter->bd_number = cards_found;
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->hw.adapter = adapter;
	adapter->msg_enable = netif_msg_init(-1, atl1c_default_msg);
	adapter->hw.hw_addr = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	if (!adapter->hw.hw_addr) {
		err = -EIO;
		dev_err(&pdev->dev, "cannot map device registers\n");
		goto err_ioremap;
	}

	/* init mii data */
	adapter->mii.dev = netdev;
	adapter->mii.mdio_read  = atl1c_mdio_read;
	adapter->mii.mdio_write = atl1c_mdio_write;
	adapter->mii.phy_id_mask = 0x1f;
	adapter->mii.reg_num_mask = MDIO_CTRL_REG_MASK;
	netif_napi_add(netdev, &adapter->napi, atl1c_clean, 64);
	setup_timer(&adapter->phy_config_timer, atl1c_phy_config,
			(unsigned long)adapter);
	/* setup the private structure */
	err = atl1c_sw_init(adapter);
	if (err) {
		dev_err(&pdev->dev, "net device private data init failed\n");
		goto err_sw_init;
	}
	atl1c_reset_pcie(&adapter->hw, ATL1C_PCIE_L0S_L1_DISABLE);

	/* Init GPHY as early as possible due to power saving issue  */
	atl1c_phy_reset(&adapter->hw);

	err = atl1c_reset_mac(&adapter->hw);
	if (err) {
		err = -EIO;
		goto err_reset;
	}

	/* reset the controller to
	 * put the device in a known good starting state */
	err = atl1c_phy_init(&adapter->hw);
	if (err) {
		err = -EIO;
		goto err_reset;
	}
	if (atl1c_read_mac_addr(&adapter->hw)) {
		/* got a random MAC address, set NET_ADDR_RANDOM to netdev */
		netdev->addr_assign_type = NET_ADDR_RANDOM;
	}
	memcpy(netdev->dev_addr, adapter->hw.mac_addr, netdev->addr_len);
	if (netif_msg_probe(adapter))
		dev_dbg(&pdev->dev, "mac address : %pM\n",
			adapter->hw.mac_addr);

	atl1c_hw_set_mac_addr(&adapter->hw, adapter->hw.mac_addr);
	INIT_WORK(&adapter->common_task, atl1c_common_task);
	adapter->work_event = 0;
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "register netdevice failed\n");
		goto err_register;
	}

	if (netif_msg_probe(adapter))
		dev_info(&pdev->dev, "version %s\n", ATL1C_DRV_VERSION);
	cards_found++;
	return 0;

err_reset:
err_register:
err_sw_init:
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

/**
 * atl1c_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * atl1c_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 */
static void atl1c_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	unregister_netdev(netdev);
	/* restore permanent address */
	atl1c_hw_set_mac_addr(&adapter->hw, adapter->hw.perm_mac_addr);
	atl1c_phy_disable(&adapter->hw);

	iounmap(adapter->hw.hw_addr);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	free_netdev(netdev);
}

/**
 * atl1c_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t atl1c_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (netif_running(netdev))
		atl1c_down(adapter);

	pci_disable_device(pdev);

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * atl1c_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * resembles the first-half of the e1000_resume routine.
 */
static pci_ers_result_t atl1c_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	if (pci_enable_device(pdev)) {
		if (netif_msg_hw(adapter))
			dev_err(&pdev->dev,
				"Cannot re-enable PCI device after reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	atl1c_reset_mac(&adapter->hw);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * atl1c_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the atl1c_resume routine.
 */
static void atl1c_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev)) {
		if (atl1c_up(adapter)) {
			if (netif_msg_hw(adapter))
				dev_err(&pdev->dev,
					"Cannot bring device back up after reset\n");
			return;
		}
	}

	netif_device_attach(netdev);
}

static const struct pci_error_handlers atl1c_err_handler = {
	.error_detected = atl1c_io_error_detected,
	.slot_reset = atl1c_io_slot_reset,
	.resume = atl1c_io_resume,
};

static SIMPLE_DEV_PM_OPS(atl1c_pm_ops, atl1c_suspend, atl1c_resume);

static struct pci_driver atl1c_driver = {
	.name     = atl1c_driver_name,
	.id_table = atl1c_pci_tbl,
	.probe    = atl1c_probe,
	.remove   = atl1c_remove,
	.shutdown = atl1c_shutdown,
	.err_handler = &atl1c_err_handler,
	.driver.pm = &atl1c_pm_ops,
};

module_pci_driver(atl1c_driver);
