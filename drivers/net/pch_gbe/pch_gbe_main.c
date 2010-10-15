/*
 * Copyright (C) 1999 - 2010 Intel Corporation.
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
 *
 * This code was derived from the Intel e1000e Linux driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#include "pch_gbe.h"
#include "pch_gbe_api.h"

#define DRV_VERSION     "1.00"
const char pch_driver_version[] = DRV_VERSION;

#define PCI_DEVICE_ID_INTEL_IOH1_GBE	0x8802		/* Pci device ID */
#define PCH_GBE_MAR_ENTRIES		16
#define PCH_GBE_SHORT_PKT		64
#define DSC_INIT16			0xC000
#define PCH_GBE_DMA_ALIGN		0
#define PCH_GBE_WATCHDOG_PERIOD		(1 * HZ)	/* watchdog time */
#define PCH_GBE_COPYBREAK_DEFAULT	256
#define PCH_GBE_PCI_BAR			1

#define PCH_GBE_TX_WEIGHT         64
#define PCH_GBE_RX_WEIGHT         64
#define PCH_GBE_RX_BUFFER_WRITE   16

/* Initialize the wake-on-LAN settings */
#define PCH_GBE_WL_INIT_SETTING    (PCH_GBE_WLC_MP)

#define PCH_GBE_MAC_RGMII_CTRL_SETTING ( \
	PCH_GBE_CHIP_TYPE_INTERNAL | \
	PCH_GBE_RGMII_MODE_RGMII   | \
	PCH_GBE_CRS_SEL              \
	)

/* Ethertype field values */
#define PCH_GBE_MAX_JUMBO_FRAME_SIZE    10318
#define PCH_GBE_FRAME_SIZE_2048         2048
#define PCH_GBE_FRAME_SIZE_4096         4096
#define PCH_GBE_FRAME_SIZE_8192         8192

#define PCH_GBE_GET_DESC(R, i, type)    (&(((struct type *)((R).desc))[i]))
#define PCH_GBE_RX_DESC(R, i)           PCH_GBE_GET_DESC(R, i, pch_gbe_rx_desc)
#define PCH_GBE_TX_DESC(R, i)           PCH_GBE_GET_DESC(R, i, pch_gbe_tx_desc)
#define PCH_GBE_DESC_UNUSED(R) \
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

/* Pause packet value */
#define	PCH_GBE_PAUSE_PKT1_VALUE    0x00C28001
#define	PCH_GBE_PAUSE_PKT2_VALUE    0x00000100
#define	PCH_GBE_PAUSE_PKT4_VALUE    0x01000888
#define	PCH_GBE_PAUSE_PKT5_VALUE    0x0000FFFF

#define PCH_GBE_ETH_ALEN            6

/* This defines the bits that are set in the Interrupt Mask
 * Set/Read Register.  Each bit is documented below:
 *   o RXT0   = Receiver Timer Interrupt (ring 0)
 *   o TXDW   = Transmit Descriptor Written Back
 *   o RXDMT0 = Receive Descriptor Minimum Threshold hit (ring 0)
 *   o RXSEQ  = Receive Sequence Error
 *   o LSC    = Link Status Change
 */
#define PCH_GBE_INT_ENABLE_MASK ( \
	PCH_GBE_INT_RX_DMA_CMPLT |    \
	PCH_GBE_INT_RX_DSC_EMP   |    \
	PCH_GBE_INT_WOL_DET      |    \
	PCH_GBE_INT_TX_CMPLT          \
	)


static unsigned int copybreak __read_mostly = PCH_GBE_COPYBREAK_DEFAULT;

static int pch_gbe_mdio_read(struct net_device *netdev, int addr, int reg);
static void pch_gbe_mdio_write(struct net_device *netdev, int addr, int reg,
			       int data);
/**
 * pch_gbe_mac_read_mac_addr - Read MAC address
 * @hw:	            Pointer to the HW structure
 * Returns
 *	0:			Successful.
 */
s32 pch_gbe_mac_read_mac_addr(struct pch_gbe_hw *hw)
{
	u32  adr1a, adr1b;

	adr1a = ioread32(&hw->reg->mac_adr[0].high);
	adr1b = ioread32(&hw->reg->mac_adr[0].low);

	hw->mac.addr[0] = (u8)(adr1a & 0xFF);
	hw->mac.addr[1] = (u8)((adr1a >> 8) & 0xFF);
	hw->mac.addr[2] = (u8)((adr1a >> 16) & 0xFF);
	hw->mac.addr[3] = (u8)((adr1a >> 24) & 0xFF);
	hw->mac.addr[4] = (u8)(adr1b & 0xFF);
	hw->mac.addr[5] = (u8)((adr1b >> 8) & 0xFF);

	pr_debug("hw->mac.addr : %pM\n", hw->mac.addr);
	return 0;
}

/**
 * pch_gbe_wait_clr_bit - Wait to clear a bit
 * @reg:	Pointer of register
 * @busy:	Busy bit
 */
static void pch_gbe_wait_clr_bit(void *reg, u32 bit)
{
	u32 tmp;
	/* wait busy */
	tmp = 1000;
	while ((ioread32(reg) & bit) && --tmp)
		cpu_relax();
	if (!tmp)
		pr_err("Error: busy bit is not cleared\n");
}
/**
 * pch_gbe_mac_mar_set - Set MAC address register
 * @hw:	    Pointer to the HW structure
 * @addr:   Pointer to the MAC address
 * @index:  MAC address array register
 */
static void pch_gbe_mac_mar_set(struct pch_gbe_hw *hw, u8 * addr, u32 index)
{
	u32 mar_low, mar_high, adrmask;

	pr_debug("index : 0x%x\n", index);

	/*
	 * HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	mar_high = ((u32) addr[0] | ((u32) addr[1] << 8) |
		   ((u32) addr[2] << 16) | ((u32) addr[3] << 24));
	mar_low = ((u32) addr[4] | ((u32) addr[5] << 8));
	/* Stop the MAC Address of index. */
	adrmask = ioread32(&hw->reg->ADDR_MASK);
	iowrite32((adrmask | (0x0001 << index)), &hw->reg->ADDR_MASK);
	/* wait busy */
	pch_gbe_wait_clr_bit(&hw->reg->ADDR_MASK, PCH_GBE_BUSY);
	/* Set the MAC address to the MAC address 1A/1B register */
	iowrite32(mar_high, &hw->reg->mac_adr[index].high);
	iowrite32(mar_low, &hw->reg->mac_adr[index].low);
	/* Start the MAC address of index */
	iowrite32((adrmask & ~(0x0001 << index)), &hw->reg->ADDR_MASK);
	/* wait busy */
	pch_gbe_wait_clr_bit(&hw->reg->ADDR_MASK, PCH_GBE_BUSY);
}

/**
 * pch_gbe_mac_reset_hw - Reset hardware
 * @hw:	Pointer to the HW structure
 */
static void pch_gbe_mac_reset_hw(struct pch_gbe_hw *hw)
{
	/* Read the MAC address. and store to the private data */
	pch_gbe_mac_read_mac_addr(hw);
	iowrite32(PCH_GBE_ALL_RST, &hw->reg->RESET);
#ifdef PCH_GBE_MAC_IFOP_RGMII
	iowrite32(PCH_GBE_MODE_GMII_ETHER, &hw->reg->MODE);
#endif
	pch_gbe_wait_clr_bit(&hw->reg->RESET, PCH_GBE_ALL_RST);
	/* Setup the receive address */
	pch_gbe_mac_mar_set(hw, hw->mac.addr, 0);
	return;
}

/**
 * pch_gbe_mac_init_rx_addrs - Initialize receive address's
 * @hw:	Pointer to the HW structure
 * @mar_count: Receive address registers
 */
static void pch_gbe_mac_init_rx_addrs(struct pch_gbe_hw *hw, u16 mar_count)
{
	u32 i;

	/* Setup the receive address */
	pch_gbe_mac_mar_set(hw, hw->mac.addr, 0);

	/* Zero out the other receive addresses */
	for (i = 1; i < mar_count; i++) {
		iowrite32(0, &hw->reg->mac_adr[i].high);
		iowrite32(0, &hw->reg->mac_adr[i].low);
	}
	iowrite32(0xFFFE, &hw->reg->ADDR_MASK);
	/* wait busy */
	pch_gbe_wait_clr_bit(&hw->reg->ADDR_MASK, PCH_GBE_BUSY);
}


/**
 * pch_gbe_mac_mc_addr_list_update - Update Multicast addresses
 * @hw:	            Pointer to the HW structure
 * @mc_addr_list:   Array of multicast addresses to program
 * @mc_addr_count:  Number of multicast addresses to program
 * @mar_used_count: The first MAC Address register free to program
 * @mar_total_num:  Total number of supported MAC Address Registers
 */
static void pch_gbe_mac_mc_addr_list_update(struct pch_gbe_hw *hw,
					    u8 *mc_addr_list, u32 mc_addr_count,
					    u32 mar_used_count, u32 mar_total_num)
{
	u32 i, adrmask;

	/* Load the first set of multicast addresses into the exact
	 * filters (RAR).  If there are not enough to fill the RAR
	 * array, clear the filters.
	 */
	for (i = mar_used_count; i < mar_total_num; i++) {
		if (mc_addr_count) {
			pch_gbe_mac_mar_set(hw, mc_addr_list, i);
			mc_addr_count--;
			mc_addr_list += PCH_GBE_ETH_ALEN;
		} else {
			/* Clear MAC address mask */
			adrmask = ioread32(&hw->reg->ADDR_MASK);
			iowrite32((adrmask | (0x0001 << i)),
					&hw->reg->ADDR_MASK);
			/* wait busy */
			pch_gbe_wait_clr_bit(&hw->reg->ADDR_MASK, PCH_GBE_BUSY);
			/* Clear MAC address */
			iowrite32(0, &hw->reg->mac_adr[i].high);
			iowrite32(0, &hw->reg->mac_adr[i].low);
		}
	}
}

/**
 * pch_gbe_mac_force_mac_fc - Force the MAC's flow control settings
 * @hw:	            Pointer to the HW structure
 * Returns
 *	0:			Successful.
 *	Negative value:		Failed.
 */
s32 pch_gbe_mac_force_mac_fc(struct pch_gbe_hw *hw)
{
	struct pch_gbe_mac_info *mac = &hw->mac;
	u32 rx_fctrl;

	pr_debug("mac->fc = %u\n", mac->fc);

	rx_fctrl = ioread32(&hw->reg->RX_FCTRL);

	switch (mac->fc) {
	case PCH_GBE_FC_NONE:
		rx_fctrl &= ~PCH_GBE_FL_CTRL_EN;
		mac->tx_fc_enable = false;
		break;
	case PCH_GBE_FC_RX_PAUSE:
		rx_fctrl |= PCH_GBE_FL_CTRL_EN;
		mac->tx_fc_enable = false;
		break;
	case PCH_GBE_FC_TX_PAUSE:
		rx_fctrl &= ~PCH_GBE_FL_CTRL_EN;
		mac->tx_fc_enable = true;
		break;
	case PCH_GBE_FC_FULL:
		rx_fctrl |= PCH_GBE_FL_CTRL_EN;
		mac->tx_fc_enable = true;
		break;
	default:
		pr_err("Flow control param set incorrectly\n");
		return -EINVAL;
	}
	if (mac->link_duplex == DUPLEX_HALF)
		rx_fctrl &= ~PCH_GBE_FL_CTRL_EN;
	iowrite32(rx_fctrl, &hw->reg->RX_FCTRL);
	pr_debug("RX_FCTRL reg : 0x%08x  mac->tx_fc_enable : %d\n",
		 ioread32(&hw->reg->RX_FCTRL), mac->tx_fc_enable);
	return 0;
}

/**
 * pch_gbe_mac_set_wol_event - Set wake-on-lan event
 * @hw:     Pointer to the HW structure
 * @wu_evt: Wake up event
 */
static void pch_gbe_mac_set_wol_event(struct pch_gbe_hw *hw, u32 wu_evt)
{
	u32 addr_mask;

	pr_debug("wu_evt : 0x%08x  ADDR_MASK reg : 0x%08x\n",
		 wu_evt, ioread32(&hw->reg->ADDR_MASK));

	if (wu_evt) {
		/* Set Wake-On-Lan address mask */
		addr_mask = ioread32(&hw->reg->ADDR_MASK);
		iowrite32(addr_mask, &hw->reg->WOL_ADDR_MASK);
		/* wait busy */
		pch_gbe_wait_clr_bit(&hw->reg->WOL_ADDR_MASK, PCH_GBE_WLA_BUSY);
		iowrite32(0, &hw->reg->WOL_ST);
		iowrite32((wu_evt | PCH_GBE_WLC_WOL_MODE), &hw->reg->WOL_CTRL);
		iowrite32(0x02, &hw->reg->TCPIP_ACC);
		iowrite32(PCH_GBE_INT_ENABLE_MASK, &hw->reg->INT_EN);
	} else {
		iowrite32(0, &hw->reg->WOL_CTRL);
		iowrite32(0, &hw->reg->WOL_ST);
	}
	return;
}

/**
 * pch_gbe_mac_ctrl_miim - Control MIIM interface
 * @hw:   Pointer to the HW structure
 * @addr: Address of PHY
 * @dir:  Operetion. (Write or Read)
 * @reg:  Access register of PHY
 * @data: Write data.
 *
 * Returns: Read date.
 */
u16 pch_gbe_mac_ctrl_miim(struct pch_gbe_hw *hw, u32 addr, u32 dir, u32 reg,
			u16 data)
{
	u32 data_out = 0;
	unsigned int i;
	unsigned long flags;

	spin_lock_irqsave(&hw->miim_lock, flags);

	for (i = 100; i; --i) {
		if ((ioread32(&hw->reg->MIIM) & PCH_GBE_MIIM_OPER_READY))
			break;
		udelay(20);
	}
	if (i == 0) {
		pr_err("pch-gbe.miim won't go Ready\n");
		spin_unlock_irqrestore(&hw->miim_lock, flags);
		return 0;	/* No way to indicate timeout error */
	}
	iowrite32(((reg << PCH_GBE_MIIM_REG_ADDR_SHIFT) |
		  (addr << PCH_GBE_MIIM_PHY_ADDR_SHIFT) |
		  dir | data), &hw->reg->MIIM);
	for (i = 0; i < 100; i++) {
		udelay(20);
		data_out = ioread32(&hw->reg->MIIM);
		if ((data_out & PCH_GBE_MIIM_OPER_READY))
			break;
	}
	spin_unlock_irqrestore(&hw->miim_lock, flags);

	pr_debug("PHY %s: reg=%d, data=0x%04X\n",
		 dir == PCH_GBE_MIIM_OPER_READ ? "READ" : "WRITE", reg,
		 dir == PCH_GBE_MIIM_OPER_READ ? data_out : data);
	return (u16) data_out;
}

/**
 * pch_gbe_mac_set_pause_packet - Set pause packet
 * @hw:   Pointer to the HW structure
 */
static void pch_gbe_mac_set_pause_packet(struct pch_gbe_hw *hw)
{
	unsigned long tmp2, tmp3;

	/* Set Pause packet */
	tmp2 = hw->mac.addr[1];
	tmp2 = (tmp2 << 8) | hw->mac.addr[0];
	tmp2 = PCH_GBE_PAUSE_PKT2_VALUE | (tmp2 << 16);

	tmp3 = hw->mac.addr[5];
	tmp3 = (tmp3 << 8) | hw->mac.addr[4];
	tmp3 = (tmp3 << 8) | hw->mac.addr[3];
	tmp3 = (tmp3 << 8) | hw->mac.addr[2];

	iowrite32(PCH_GBE_PAUSE_PKT1_VALUE, &hw->reg->PAUSE_PKT1);
	iowrite32(tmp2, &hw->reg->PAUSE_PKT2);
	iowrite32(tmp3, &hw->reg->PAUSE_PKT3);
	iowrite32(PCH_GBE_PAUSE_PKT4_VALUE, &hw->reg->PAUSE_PKT4);
	iowrite32(PCH_GBE_PAUSE_PKT5_VALUE, &hw->reg->PAUSE_PKT5);

	/* Transmit Pause Packet */
	iowrite32(PCH_GBE_PS_PKT_RQ, &hw->reg->PAUSE_REQ);

	pr_debug("PAUSE_PKT1-5 reg : 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		 ioread32(&hw->reg->PAUSE_PKT1), ioread32(&hw->reg->PAUSE_PKT2),
		 ioread32(&hw->reg->PAUSE_PKT3), ioread32(&hw->reg->PAUSE_PKT4),
		 ioread32(&hw->reg->PAUSE_PKT5));

	return;
}


/**
 * pch_gbe_alloc_queues - Allocate memory for all rings
 * @adapter:  Board private structure to initialize
 * Returns
 *	0:	Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_alloc_queues(struct pch_gbe_adapter *adapter)
{
	int size;

	size = (int)sizeof(struct pch_gbe_tx_ring);
	adapter->tx_ring = kzalloc(size, GFP_KERNEL);
	if (!adapter->tx_ring)
		return -ENOMEM;
	size = (int)sizeof(struct pch_gbe_rx_ring);
	adapter->rx_ring = kzalloc(size, GFP_KERNEL);
	if (!adapter->rx_ring) {
		kfree(adapter->tx_ring);
		return -ENOMEM;
	}
	return 0;
}

/**
 * pch_gbe_init_stats - Initialize status
 * @adapter:  Board private structure to initialize
 */
static void pch_gbe_init_stats(struct pch_gbe_adapter *adapter)
{
	memset(&adapter->stats, 0, sizeof(adapter->stats));
	return;
}

/**
 * pch_gbe_init_phy - Initialize PHY
 * @adapter:  Board private structure to initialize
 * Returns
 *	0:	Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_init_phy(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u32 addr;
	u16 bmcr, stat;

	/* Discover phy addr by searching addrs in order {1,0,2,..., 31} */
	for (addr = 0; addr < PCH_GBE_PHY_REGS_LEN; addr++) {
		adapter->mii.phy_id = (addr == 0) ? 1 : (addr == 1) ? 0 : addr;
		bmcr = pch_gbe_mdio_read(netdev, adapter->mii.phy_id, MII_BMCR);
		stat = pch_gbe_mdio_read(netdev, adapter->mii.phy_id, MII_BMSR);
		stat = pch_gbe_mdio_read(netdev, adapter->mii.phy_id, MII_BMSR);
		if (!((bmcr == 0xFFFF) || ((stat == 0) && (bmcr == 0))))
			break;
	}
	adapter->hw.phy.addr = adapter->mii.phy_id;
	pr_debug("phy_addr = %d\n", adapter->mii.phy_id);
	if (addr == 32)
		return -EAGAIN;
	/* Selected the phy and isolate the rest */
	for (addr = 0; addr < PCH_GBE_PHY_REGS_LEN; addr++) {
		if (addr != adapter->mii.phy_id) {
			pch_gbe_mdio_write(netdev, addr, MII_BMCR,
					   BMCR_ISOLATE);
		} else {
			bmcr = pch_gbe_mdio_read(netdev, addr, MII_BMCR);
			pch_gbe_mdio_write(netdev, addr, MII_BMCR,
					   bmcr & ~BMCR_ISOLATE);
		}
	}

	/* MII setup */
	adapter->mii.phy_id_mask = 0x1F;
	adapter->mii.reg_num_mask = 0x1F;
	adapter->mii.dev = adapter->netdev;
	adapter->mii.mdio_read = pch_gbe_mdio_read;
	adapter->mii.mdio_write = pch_gbe_mdio_write;
	adapter->mii.supports_gmii = mii_check_gmii_support(&adapter->mii);
	return 0;
}

/**
 * pch_gbe_mdio_read - The read function for mii
 * @netdev: Network interface device structure
 * @addr:   Phy ID
 * @reg:    Access location
 * Returns
 *	0:	Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_mdio_read(struct net_device *netdev, int addr, int reg)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	return pch_gbe_mac_ctrl_miim(hw, addr, PCH_GBE_HAL_MIIM_READ, reg,
				     (u16) 0);
}

/**
 * pch_gbe_mdio_write - The write function for mii
 * @netdev: Network interface device structure
 * @addr:   Phy ID (not used)
 * @reg:    Access location
 * @data:   Write data
 */
static void pch_gbe_mdio_write(struct net_device *netdev,
			       int addr, int reg, int data)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	pch_gbe_mac_ctrl_miim(hw, addr, PCH_GBE_HAL_MIIM_WRITE, reg, data);
}

/**
 * pch_gbe_reset_task - Reset processing at the time of transmission timeout
 * @work:  Pointer of board private structure
 */
static void pch_gbe_reset_task(struct work_struct *work)
{
	struct pch_gbe_adapter *adapter;
	adapter = container_of(work, struct pch_gbe_adapter, reset_task);

	pch_gbe_reinit_locked(adapter);
}

/**
 * pch_gbe_reinit_locked- Re-initialization
 * @adapter:  Board private structure
 */
void pch_gbe_reinit_locked(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	rtnl_lock();
	if (netif_running(netdev)) {
		pch_gbe_down(adapter);
		pch_gbe_up(adapter);
	}
	rtnl_unlock();
}

/**
 * pch_gbe_reset - Reset GbE
 * @adapter:  Board private structure
 */
void pch_gbe_reset(struct pch_gbe_adapter *adapter)
{
	pch_gbe_mac_reset_hw(&adapter->hw);
	/* Setup the receive address. */
	pch_gbe_mac_init_rx_addrs(&adapter->hw, PCH_GBE_MAR_ENTRIES);
	if (pch_gbe_hal_init_hw(&adapter->hw))
		pr_err("Hardware Error\n");
}

/**
 * pch_gbe_free_irq - Free an interrupt
 * @adapter:  Board private structure
 */
static void pch_gbe_free_irq(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	free_irq(adapter->pdev->irq, netdev);
	if (adapter->have_msi) {
		pci_disable_msi(adapter->pdev);
		pr_debug("call pci_disable_msi\n");
	}
}

/**
 * pch_gbe_irq_disable - Mask off interrupt generation on the NIC
 * @adapter:  Board private structure
 */
static void pch_gbe_irq_disable(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;

	atomic_inc(&adapter->irq_sem);
	iowrite32(0, &hw->reg->INT_EN);
	ioread32(&hw->reg->INT_ST);
	synchronize_irq(adapter->pdev->irq);

	pr_debug("INT_EN reg : 0x%08x\n", ioread32(&hw->reg->INT_EN));
}

/**
 * pch_gbe_irq_enable - Enable default interrupt generation settings
 * @adapter:  Board private structure
 */
static void pch_gbe_irq_enable(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;

	if (likely(atomic_dec_and_test(&adapter->irq_sem)))
		iowrite32(PCH_GBE_INT_ENABLE_MASK, &hw->reg->INT_EN);
	ioread32(&hw->reg->INT_ST);
	pr_debug("INT_EN reg : 0x%08x\n", ioread32(&hw->reg->INT_EN));
}



/**
 * pch_gbe_setup_tctl - configure the Transmit control registers
 * @adapter:  Board private structure
 */
static void pch_gbe_setup_tctl(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 tx_mode, tcpip;

	tx_mode = PCH_GBE_TM_LONG_PKT |
		PCH_GBE_TM_ST_AND_FD |
		PCH_GBE_TM_SHORT_PKT |
		PCH_GBE_TM_TH_TX_STRT_8 |
		PCH_GBE_TM_TH_ALM_EMP_4 | PCH_GBE_TM_TH_ALM_FULL_8;

	iowrite32(tx_mode, &hw->reg->TX_MODE);

	tcpip = ioread32(&hw->reg->TCPIP_ACC);
	tcpip |= PCH_GBE_TX_TCPIPACC_EN;
	iowrite32(tcpip, &hw->reg->TCPIP_ACC);
	return;
}

/**
 * pch_gbe_configure_tx - Configure Transmit Unit after Reset
 * @adapter:  Board private structure
 */
static void pch_gbe_configure_tx(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 tdba, tdlen, dctrl;

	pr_debug("dma addr = 0x%08llx  size = 0x%08x\n",
		 (unsigned long long)adapter->tx_ring->dma,
		 adapter->tx_ring->size);

	/* Setup the HW Tx Head and Tail descriptor pointers */
	tdba = adapter->tx_ring->dma;
	tdlen = adapter->tx_ring->size - 0x10;
	iowrite32(tdba, &hw->reg->TX_DSC_BASE);
	iowrite32(tdlen, &hw->reg->TX_DSC_SIZE);
	iowrite32(tdba, &hw->reg->TX_DSC_SW_P);

	/* Enables Transmission DMA */
	dctrl = ioread32(&hw->reg->DMA_CTRL);
	dctrl |= PCH_GBE_TX_DMA_EN;
	iowrite32(dctrl, &hw->reg->DMA_CTRL);
}

/**
 * pch_gbe_setup_rctl - Configure the receive control registers
 * @adapter:  Board private structure
 */
static void pch_gbe_setup_rctl(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 rx_mode, tcpip;

	rx_mode = PCH_GBE_ADD_FIL_EN | PCH_GBE_MLT_FIL_EN |
	PCH_GBE_RH_ALM_EMP_4 | PCH_GBE_RH_ALM_FULL_4 | PCH_GBE_RH_RD_TRG_8;

	iowrite32(rx_mode, &hw->reg->RX_MODE);

	tcpip = ioread32(&hw->reg->TCPIP_ACC);

	if (adapter->rx_csum) {
		tcpip &= ~PCH_GBE_RX_TCPIPACC_OFF;
		tcpip |= PCH_GBE_RX_TCPIPACC_EN;
	} else {
		tcpip |= PCH_GBE_RX_TCPIPACC_OFF;
		tcpip &= ~PCH_GBE_RX_TCPIPACC_EN;
	}
	iowrite32(tcpip, &hw->reg->TCPIP_ACC);
	return;
}

/**
 * pch_gbe_configure_rx - Configure Receive Unit after Reset
 * @adapter:  Board private structure
 */
static void pch_gbe_configure_rx(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 rdba, rdlen, rctl, rxdma;

	pr_debug("dma adr = 0x%08llx  size = 0x%08x\n",
		 (unsigned long long)adapter->rx_ring->dma,
		 adapter->rx_ring->size);

	pch_gbe_mac_force_mac_fc(hw);

	/* Disables Receive MAC */
	rctl = ioread32(&hw->reg->MAC_RX_EN);
	iowrite32((rctl & ~PCH_GBE_MRE_MAC_RX_EN), &hw->reg->MAC_RX_EN);

	/* Disables Receive DMA */
	rxdma = ioread32(&hw->reg->DMA_CTRL);
	rxdma &= ~PCH_GBE_RX_DMA_EN;
	iowrite32(rxdma, &hw->reg->DMA_CTRL);

	pr_debug("MAC_RX_EN reg = 0x%08x  DMA_CTRL reg = 0x%08x\n",
		 ioread32(&hw->reg->MAC_RX_EN),
		 ioread32(&hw->reg->DMA_CTRL));

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring */
	rdba = adapter->rx_ring->dma;
	rdlen = adapter->rx_ring->size - 0x10;
	iowrite32(rdba, &hw->reg->RX_DSC_BASE);
	iowrite32(rdlen, &hw->reg->RX_DSC_SIZE);
	iowrite32((rdba + rdlen), &hw->reg->RX_DSC_SW_P);

	/* Enables Receive DMA */
	rxdma = ioread32(&hw->reg->DMA_CTRL);
	rxdma |= PCH_GBE_RX_DMA_EN;
	iowrite32(rxdma, &hw->reg->DMA_CTRL);
	/* Enables Receive */
	iowrite32(PCH_GBE_MRE_MAC_RX_EN, &hw->reg->MAC_RX_EN);
}

/**
 * pch_gbe_unmap_and_free_tx_resource - Unmap and free tx socket buffer
 * @adapter:     Board private structure
 * @buffer_info: Buffer information structure
 */
static void pch_gbe_unmap_and_free_tx_resource(
	struct pch_gbe_adapter *adapter, struct pch_gbe_buffer *buffer_info)
{
	if (buffer_info->mapped) {
		dma_unmap_single(&adapter->pdev->dev, buffer_info->dma,
				 buffer_info->length, DMA_TO_DEVICE);
		buffer_info->mapped = false;
	}
	if (buffer_info->skb) {
		dev_kfree_skb_any(buffer_info->skb);
		buffer_info->skb = NULL;
	}
}

/**
 * pch_gbe_unmap_and_free_rx_resource - Unmap and free rx socket buffer
 * @adapter:      Board private structure
 * @buffer_info:  Buffer information structure
 */
static void pch_gbe_unmap_and_free_rx_resource(
					struct pch_gbe_adapter *adapter,
					struct pch_gbe_buffer *buffer_info)
{
	if (buffer_info->mapped) {
		dma_unmap_single(&adapter->pdev->dev, buffer_info->dma,
				 buffer_info->length, DMA_FROM_DEVICE);
		buffer_info->mapped = false;
	}
	if (buffer_info->skb) {
		dev_kfree_skb_any(buffer_info->skb);
		buffer_info->skb = NULL;
	}
}

/**
 * pch_gbe_clean_tx_ring - Free Tx Buffers
 * @adapter:  Board private structure
 * @tx_ring:  Ring to be cleaned
 */
static void pch_gbe_clean_tx_ring(struct pch_gbe_adapter *adapter,
				   struct pch_gbe_tx_ring *tx_ring)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pch_gbe_buffer *buffer_info;
	unsigned long size;
	unsigned int i;

	/* Free all the Tx ring sk_buffs */
	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->buffer_info[i];
		pch_gbe_unmap_and_free_tx_resource(adapter, buffer_info);
	}
	pr_debug("call pch_gbe_unmap_and_free_tx_resource() %d count\n", i);

	size = (unsigned long)sizeof(struct pch_gbe_buffer) * tx_ring->count;
	memset(tx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);
	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	iowrite32(tx_ring->dma, &hw->reg->TX_DSC_HW_P);
	iowrite32((tx_ring->size - 0x10), &hw->reg->TX_DSC_SIZE);
}

/**
 * pch_gbe_clean_rx_ring - Free Rx Buffers
 * @adapter:  Board private structure
 * @rx_ring:  Ring to free buffers from
 */
static void
pch_gbe_clean_rx_ring(struct pch_gbe_adapter *adapter,
		      struct pch_gbe_rx_ring *rx_ring)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pch_gbe_buffer *buffer_info;
	unsigned long size;
	unsigned int i;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		pch_gbe_unmap_and_free_rx_resource(adapter, buffer_info);
	}
	pr_debug("call pch_gbe_unmap_and_free_rx_resource() %d count\n", i);
	size = (unsigned long)sizeof(struct pch_gbe_buffer) * rx_ring->count;
	memset(rx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
	iowrite32(rx_ring->dma, &hw->reg->RX_DSC_HW_P);
	iowrite32((rx_ring->size - 0x10), &hw->reg->RX_DSC_SIZE);
}

static void pch_gbe_set_rgmii_ctrl(struct pch_gbe_adapter *adapter, u16 speed,
				    u16 duplex)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	unsigned long rgmii = 0;

	/* Set the RGMII control. */
#ifdef PCH_GBE_MAC_IFOP_RGMII
	switch (speed) {
	case SPEED_10:
		rgmii = (PCH_GBE_RGMII_RATE_2_5M |
			 PCH_GBE_MAC_RGMII_CTRL_SETTING);
		break;
	case SPEED_100:
		rgmii = (PCH_GBE_RGMII_RATE_25M |
			 PCH_GBE_MAC_RGMII_CTRL_SETTING);
		break;
	case SPEED_1000:
		rgmii = (PCH_GBE_RGMII_RATE_125M |
			 PCH_GBE_MAC_RGMII_CTRL_SETTING);
		break;
	}
	iowrite32(rgmii, &hw->reg->RGMII_CTRL);
#else	/* GMII */
	rgmii = 0;
	iowrite32(rgmii, &hw->reg->RGMII_CTRL);
#endif
}
static void pch_gbe_set_mode(struct pch_gbe_adapter *adapter, u16 speed,
			      u16 duplex)
{
	struct net_device *netdev = adapter->netdev;
	struct pch_gbe_hw *hw = &adapter->hw;
	unsigned long mode = 0;

	/* Set the communication mode */
	switch (speed) {
	case SPEED_10:
		mode = PCH_GBE_MODE_MII_ETHER;
		netdev->tx_queue_len = 10;
		break;
	case SPEED_100:
		mode = PCH_GBE_MODE_MII_ETHER;
		netdev->tx_queue_len = 100;
		break;
	case SPEED_1000:
		mode = PCH_GBE_MODE_GMII_ETHER;
		break;
	}
	if (duplex == DUPLEX_FULL)
		mode |= PCH_GBE_MODE_FULL_DUPLEX;
	else
		mode |= PCH_GBE_MODE_HALF_DUPLEX;
	iowrite32(mode, &hw->reg->MODE);
}

/**
 * pch_gbe_watchdog - Watchdog process
 * @data:  Board private structure
 */
static void pch_gbe_watchdog(unsigned long data)
{
	struct pch_gbe_adapter *adapter = (struct pch_gbe_adapter *)data;
	struct net_device *netdev = adapter->netdev;
	struct pch_gbe_hw *hw = &adapter->hw;
	struct ethtool_cmd cmd;

	pr_debug("right now = %ld\n", jiffies);

	pch_gbe_update_stats(adapter);
	if ((mii_link_ok(&adapter->mii)) && (!netif_carrier_ok(netdev))) {
		netdev->tx_queue_len = adapter->tx_queue_len;
		/* mii library handles link maintenance tasks */
		if (mii_ethtool_gset(&adapter->mii, &cmd)) {
			pr_err("ethtool get setting Error\n");
			mod_timer(&adapter->watchdog_timer,
				  round_jiffies(jiffies +
						PCH_GBE_WATCHDOG_PERIOD));
			return;
		}
		hw->mac.link_speed = cmd.speed;
		hw->mac.link_duplex = cmd.duplex;
		/* Set the RGMII control. */
		pch_gbe_set_rgmii_ctrl(adapter, hw->mac.link_speed,
						hw->mac.link_duplex);
		/* Set the communication mode */
		pch_gbe_set_mode(adapter, hw->mac.link_speed,
				 hw->mac.link_duplex);
		netdev_dbg(netdev,
			   "Link is Up %d Mbps %s-Duplex\n",
			   cmd.speed,
			   cmd.duplex == DUPLEX_FULL ? "Full" : "Half");
		netif_carrier_on(netdev);
		netif_wake_queue(netdev);
	} else if ((!mii_link_ok(&adapter->mii)) &&
		   (netif_carrier_ok(netdev))) {
		netdev_dbg(netdev, "NIC Link is Down\n");
		hw->mac.link_speed = SPEED_10;
		hw->mac.link_duplex = DUPLEX_HALF;
		netif_carrier_off(netdev);
		netif_stop_queue(netdev);
	}
	mod_timer(&adapter->watchdog_timer,
		  round_jiffies(jiffies + PCH_GBE_WATCHDOG_PERIOD));
}

/**
 * pch_gbe_tx_queue - Carry out queuing of the transmission data
 * @adapter:  Board private structure
 * @tx_ring:  Tx descriptor ring structure
 * @skb:      Sockt buffer structure
 */
static void pch_gbe_tx_queue(struct pch_gbe_adapter *adapter,
			      struct pch_gbe_tx_ring *tx_ring,
			      struct sk_buff *skb)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pch_gbe_tx_desc *tx_desc;
	struct pch_gbe_buffer *buffer_info;
	struct sk_buff *tmp_skb;
	unsigned int frame_ctrl;
	unsigned int ring_num;
	unsigned long flags;

	/*-- Set frame control --*/
	frame_ctrl = 0;
	if (unlikely(skb->len < PCH_GBE_SHORT_PKT))
		frame_ctrl |= PCH_GBE_TXD_CTRL_APAD;
	if (unlikely(!adapter->tx_csum))
		frame_ctrl |= PCH_GBE_TXD_CTRL_TCPIP_ACC_OFF;

	/* Performs checksum processing */
	/*
	 * It is because the hardware accelerator does not support a checksum,
	 * when the received data size is less than 64 bytes.
	 */
	if ((skb->len < PCH_GBE_SHORT_PKT) && (adapter->tx_csum)) {
		frame_ctrl |= PCH_GBE_TXD_CTRL_APAD |
			      PCH_GBE_TXD_CTRL_TCPIP_ACC_OFF;
		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *iph = ip_hdr(skb);
			unsigned int offset;
			iph->check = 0;
			iph->check = ip_fast_csum((u8 *) iph, iph->ihl);
			offset = skb_transport_offset(skb);
			if (iph->protocol == IPPROTO_TCP) {
				skb->csum = 0;
				tcp_hdr(skb)->check = 0;
				skb->csum = skb_checksum(skb, offset,
							 skb->len - offset, 0);
				tcp_hdr(skb)->check =
					csum_tcpudp_magic(iph->saddr,
							  iph->daddr,
							  skb->len - offset,
							  IPPROTO_TCP,
							  skb->csum);
			} else if (iph->protocol == IPPROTO_UDP) {
				skb->csum = 0;
				udp_hdr(skb)->check = 0;
				skb->csum =
					skb_checksum(skb, offset,
						     skb->len - offset, 0);
				udp_hdr(skb)->check =
					csum_tcpudp_magic(iph->saddr,
							  iph->daddr,
							  skb->len - offset,
							  IPPROTO_UDP,
							  skb->csum);
			}
		}
	}
	spin_lock_irqsave(&tx_ring->tx_lock, flags);
	ring_num = tx_ring->next_to_use;
	if (unlikely((ring_num + 1) == tx_ring->count))
		tx_ring->next_to_use = 0;
	else
		tx_ring->next_to_use = ring_num + 1;

	spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
	buffer_info = &tx_ring->buffer_info[ring_num];
	tmp_skb = buffer_info->skb;

	/* [Header:14][payload] ---> [Header:14][paddong:2][payload]    */
	memcpy(tmp_skb->data, skb->data, ETH_HLEN);
	tmp_skb->data[ETH_HLEN] = 0x00;
	tmp_skb->data[ETH_HLEN + 1] = 0x00;
	tmp_skb->len = skb->len;
	memcpy(&tmp_skb->data[ETH_HLEN + 2], &skb->data[ETH_HLEN],
	       (skb->len - ETH_HLEN));
	/*-- Set Buffer infomation --*/
	buffer_info->length = tmp_skb->len;
	buffer_info->dma = dma_map_single(&adapter->pdev->dev, tmp_skb->data,
					  buffer_info->length,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(&adapter->pdev->dev, buffer_info->dma)) {
		pr_err("TX DMA map failed\n");
		buffer_info->dma = 0;
		buffer_info->time_stamp = 0;
		tx_ring->next_to_use = ring_num;
		return;
	}
	buffer_info->mapped = true;
	buffer_info->time_stamp = jiffies;

	/*-- Set Tx descriptor --*/
	tx_desc = PCH_GBE_TX_DESC(*tx_ring, ring_num);
	tx_desc->buffer_addr = (buffer_info->dma);
	tx_desc->length = (tmp_skb->len);
	tx_desc->tx_words_eob = ((tmp_skb->len + 3));
	tx_desc->tx_frame_ctrl = (frame_ctrl);
	tx_desc->gbec_status = (DSC_INIT16);

	if (unlikely(++ring_num == tx_ring->count))
		ring_num = 0;

	/* Update software pointer of TX descriptor */
	iowrite32(tx_ring->dma +
		  (int)sizeof(struct pch_gbe_tx_desc) * ring_num,
		  &hw->reg->TX_DSC_SW_P);
	dev_kfree_skb_any(skb);
}

/**
 * pch_gbe_update_stats - Update the board statistics counters
 * @adapter:  Board private structure
 */
void pch_gbe_update_stats(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_hw_stats *stats = &adapter->stats;
	unsigned long flags;

	/*
	 * Prevent stats update while adapter is being reset, or if the pci
	 * connection is down.
	 */
	if ((pdev->error_state) && (pdev->error_state != pci_channel_io_normal))
		return;

	spin_lock_irqsave(&adapter->stats_lock, flags);

	/* Update device status "adapter->stats" */
	stats->rx_errors = stats->rx_crc_errors + stats->rx_frame_errors;
	stats->tx_errors = stats->tx_length_errors +
	    stats->tx_aborted_errors +
	    stats->tx_carrier_errors + stats->tx_timeout_count;

	/* Update network device status "adapter->net_stats" */
	netdev->stats.rx_packets = stats->rx_packets;
	netdev->stats.rx_bytes = stats->rx_bytes;
	netdev->stats.rx_dropped = stats->rx_dropped;
	netdev->stats.tx_packets = stats->tx_packets;
	netdev->stats.tx_bytes = stats->tx_bytes;
	netdev->stats.tx_dropped = stats->tx_dropped;
	/* Fill out the OS statistics structure */
	netdev->stats.multicast = stats->multicast;
	netdev->stats.collisions = stats->collisions;
	/* Rx Errors */
	netdev->stats.rx_errors = stats->rx_errors;
	netdev->stats.rx_crc_errors = stats->rx_crc_errors;
	netdev->stats.rx_frame_errors = stats->rx_frame_errors;
	/* Tx Errors */
	netdev->stats.tx_errors = stats->tx_errors;
	netdev->stats.tx_aborted_errors = stats->tx_aborted_errors;
	netdev->stats.tx_carrier_errors = stats->tx_carrier_errors;

	spin_unlock_irqrestore(&adapter->stats_lock, flags);
}

/**
 * pch_gbe_intr - Interrupt Handler
 * @irq:   Interrupt number
 * @data:  Pointer to a network interface device structure
 * Returns
 *	- IRQ_HANDLED:	Our interrupt
 *	- IRQ_NONE:	Not our interrupt
 */
static irqreturn_t pch_gbe_intr(int irq, void *data)
{
	struct net_device *netdev = data;
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 int_st;
	u32 int_en;

	/* Check request status */
	int_st = ioread32(&hw->reg->INT_ST);
	int_st = int_st & ioread32(&hw->reg->INT_EN);
	/* When request status is no interruption factor */
	if (unlikely(!int_st))
		return IRQ_NONE;	/* Not our interrupt. End processing. */
	pr_debug("%s occur int_st = 0x%08x\n", __func__, int_st);
	if (int_st & PCH_GBE_INT_RX_FRAME_ERR)
		adapter->stats.intr_rx_frame_err_count++;
	if (int_st & PCH_GBE_INT_RX_FIFO_ERR)
		adapter->stats.intr_rx_fifo_err_count++;
	if (int_st & PCH_GBE_INT_RX_DMA_ERR)
		adapter->stats.intr_rx_dma_err_count++;
	if (int_st & PCH_GBE_INT_TX_FIFO_ERR)
		adapter->stats.intr_tx_fifo_err_count++;
	if (int_st & PCH_GBE_INT_TX_DMA_ERR)
		adapter->stats.intr_tx_dma_err_count++;
	if (int_st & PCH_GBE_INT_TCPIP_ERR)
		adapter->stats.intr_tcpip_err_count++;
	/* When Rx descriptor is empty  */
	if ((int_st & PCH_GBE_INT_RX_DSC_EMP)) {
		adapter->stats.intr_rx_dsc_empty_count++;
		pr_err("Rx descriptor is empty\n");
		int_en = ioread32(&hw->reg->INT_EN);
		iowrite32((int_en & ~PCH_GBE_INT_RX_DSC_EMP), &hw->reg->INT_EN);
		if (hw->mac.tx_fc_enable) {
			/* Set Pause packet */
			pch_gbe_mac_set_pause_packet(hw);
		}
		if ((int_en & (PCH_GBE_INT_RX_DMA_CMPLT | PCH_GBE_INT_TX_CMPLT))
		    == 0) {
			return IRQ_HANDLED;
		}
	}

	/* When request status is Receive interruption */
	if ((int_st & (PCH_GBE_INT_RX_DMA_CMPLT | PCH_GBE_INT_TX_CMPLT))) {
		if (likely(napi_schedule_prep(&adapter->napi))) {
			/* Enable only Rx Descriptor empty */
			atomic_inc(&adapter->irq_sem);
			int_en = ioread32(&hw->reg->INT_EN);
			int_en &=
			    ~(PCH_GBE_INT_RX_DMA_CMPLT | PCH_GBE_INT_TX_CMPLT);
			iowrite32(int_en, &hw->reg->INT_EN);
			/* Start polling for NAPI */
			__napi_schedule(&adapter->napi);
		}
	}
	pr_debug("return = 0x%08x  INT_EN reg = 0x%08x\n",
		 IRQ_HANDLED, ioread32(&hw->reg->INT_EN));
	return IRQ_HANDLED;
}

/**
 * pch_gbe_alloc_rx_buffers - Replace used receive buffers; legacy & extended
 * @adapter:       Board private structure
 * @rx_ring:       Rx descriptor ring
 * @cleaned_count: Cleaned count
 */
static void
pch_gbe_alloc_rx_buffers(struct pch_gbe_adapter *adapter,
			 struct pch_gbe_rx_ring *rx_ring, int cleaned_count)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pch_gbe_rx_desc *rx_desc;
	struct pch_gbe_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int bufsz;

	bufsz = adapter->rx_buffer_len + PCH_GBE_DMA_ALIGN;
	i = rx_ring->next_to_use;

	while ((cleaned_count--)) {
		buffer_info = &rx_ring->buffer_info[i];
		skb = buffer_info->skb;
		if (skb) {
			skb_trim(skb, 0);
		} else {
			skb = netdev_alloc_skb(netdev, bufsz);
			if (unlikely(!skb)) {
				/* Better luck next round */
				adapter->stats.rx_alloc_buff_failed++;
				break;
			}
			/* 64byte align */
			skb_reserve(skb, PCH_GBE_DMA_ALIGN);

			buffer_info->skb = skb;
			buffer_info->length = adapter->rx_buffer_len;
		}
		buffer_info->dma = dma_map_single(&pdev->dev,
						  skb->data,
						  buffer_info->length,
						  DMA_FROM_DEVICE);
		if (dma_mapping_error(&adapter->pdev->dev, buffer_info->dma)) {
			dev_kfree_skb(skb);
			buffer_info->skb = NULL;
			buffer_info->dma = 0;
			adapter->stats.rx_alloc_buff_failed++;
			break; /* while !buffer_info->skb */
		}
		buffer_info->mapped = true;
		rx_desc = PCH_GBE_RX_DESC(*rx_ring, i);
		rx_desc->buffer_addr = (buffer_info->dma);
		rx_desc->gbec_status = DSC_INIT16;

		pr_debug("i = %d  buffer_info->dma = 0x08%llx  buffer_info->length = 0x%x\n",
			 i, (unsigned long long)buffer_info->dma,
			 buffer_info->length);

		if (unlikely(++i == rx_ring->count))
			i = 0;
	}
	if (likely(rx_ring->next_to_use != i)) {
		rx_ring->next_to_use = i;
		if (unlikely(i-- == 0))
			i = (rx_ring->count - 1);
		iowrite32(rx_ring->dma +
			  (int)sizeof(struct pch_gbe_rx_desc) * i,
			  &hw->reg->RX_DSC_SW_P);
	}
	return;
}

/**
 * pch_gbe_alloc_tx_buffers - Allocate transmit buffers
 * @adapter:   Board private structure
 * @tx_ring:   Tx descriptor ring
 */
static void pch_gbe_alloc_tx_buffers(struct pch_gbe_adapter *adapter,
					struct pch_gbe_tx_ring *tx_ring)
{
	struct pch_gbe_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int bufsz;
	struct pch_gbe_tx_desc *tx_desc;

	bufsz =
	    adapter->hw.mac.max_frame_size + PCH_GBE_DMA_ALIGN + NET_IP_ALIGN;

	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->buffer_info[i];
		skb = netdev_alloc_skb(adapter->netdev, bufsz);
		skb_reserve(skb, PCH_GBE_DMA_ALIGN);
		buffer_info->skb = skb;
		tx_desc = PCH_GBE_TX_DESC(*tx_ring, i);
		tx_desc->gbec_status = (DSC_INIT16);
	}
	return;
}

/**
 * pch_gbe_clean_tx - Reclaim resources after transmit completes
 * @adapter:   Board private structure
 * @tx_ring:   Tx descriptor ring
 * Returns
 *	true:  Cleaned the descriptor
 *	false: Not cleaned the descriptor
 */
static bool
pch_gbe_clean_tx(struct pch_gbe_adapter *adapter,
		 struct pch_gbe_tx_ring *tx_ring)
{
	struct pch_gbe_tx_desc *tx_desc;
	struct pch_gbe_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int cleaned_count = 0;
	bool cleaned = false;

	pr_debug("next_to_clean : %d\n", tx_ring->next_to_clean);

	i = tx_ring->next_to_clean;
	tx_desc = PCH_GBE_TX_DESC(*tx_ring, i);
	pr_debug("gbec_status:0x%04x  dma_status:0x%04x\n",
		 tx_desc->gbec_status, tx_desc->dma_status);

	while ((tx_desc->gbec_status & DSC_INIT16) == 0x0000) {
		pr_debug("gbec_status:0x%04x\n", tx_desc->gbec_status);
		cleaned = true;
		buffer_info = &tx_ring->buffer_info[i];
		skb = buffer_info->skb;

		if ((tx_desc->gbec_status & PCH_GBE_TXD_GMAC_STAT_ABT)) {
			adapter->stats.tx_aborted_errors++;
			pr_err("Transfer Abort Error\n");
		} else if ((tx_desc->gbec_status & PCH_GBE_TXD_GMAC_STAT_CRSER)
			  ) {
			adapter->stats.tx_carrier_errors++;
			pr_err("Transfer Carrier Sense Error\n");
		} else if ((tx_desc->gbec_status & PCH_GBE_TXD_GMAC_STAT_EXCOL)
			  ) {
			adapter->stats.tx_aborted_errors++;
			pr_err("Transfer Collision Abort Error\n");
		} else if ((tx_desc->gbec_status &
			    (PCH_GBE_TXD_GMAC_STAT_SNGCOL |
			     PCH_GBE_TXD_GMAC_STAT_MLTCOL))) {
			adapter->stats.collisions++;
			adapter->stats.tx_packets++;
			adapter->stats.tx_bytes += skb->len;
			pr_debug("Transfer Collision\n");
		} else if ((tx_desc->gbec_status & PCH_GBE_TXD_GMAC_STAT_CMPLT)
			  ) {
			adapter->stats.tx_packets++;
			adapter->stats.tx_bytes += skb->len;
		}
		if (buffer_info->mapped) {
			pr_debug("unmap buffer_info->dma : %d\n", i);
			dma_unmap_single(&adapter->pdev->dev, buffer_info->dma,
					 buffer_info->length, DMA_TO_DEVICE);
			buffer_info->mapped = false;
		}
		if (buffer_info->skb) {
			pr_debug("trim buffer_info->skb : %d\n", i);
			skb_trim(buffer_info->skb, 0);
		}
		tx_desc->gbec_status = DSC_INIT16;
		if (unlikely(++i == tx_ring->count))
			i = 0;
		tx_desc = PCH_GBE_TX_DESC(*tx_ring, i);

		/* weight of a sort for tx, to avoid endless transmit cleanup */
		if (cleaned_count++ == PCH_GBE_TX_WEIGHT)
			break;
	}
	pr_debug("called pch_gbe_unmap_and_free_tx_resource() %d count\n",
		 cleaned_count);
	/* Recover from running out of Tx resources in xmit_frame */
	if (unlikely(cleaned && (netif_queue_stopped(adapter->netdev)))) {
		netif_wake_queue(adapter->netdev);
		adapter->stats.tx_restart_count++;
		pr_debug("Tx wake queue\n");
	}
	spin_lock(&adapter->tx_queue_lock);
	tx_ring->next_to_clean = i;
	spin_unlock(&adapter->tx_queue_lock);
	pr_debug("next_to_clean : %d\n", tx_ring->next_to_clean);
	return cleaned;
}

/**
 * pch_gbe_clean_rx - Send received data up the network stack; legacy
 * @adapter:     Board private structure
 * @rx_ring:     Rx descriptor ring
 * @work_done:   Completed count
 * @work_to_do:  Request count
 * Returns
 *	true:  Cleaned the descriptor
 *	false: Not cleaned the descriptor
 */
static bool
pch_gbe_clean_rx(struct pch_gbe_adapter *adapter,
		 struct pch_gbe_rx_ring *rx_ring,
		 int *work_done, int work_to_do)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_buffer *buffer_info;
	struct pch_gbe_rx_desc *rx_desc;
	u32 length;
	unsigned char tmp_packet[ETH_HLEN];
	unsigned int i;
	unsigned int cleaned_count = 0;
	bool cleaned = false;
	struct sk_buff *skb;
	u8 dma_status;
	u16 gbec_status;
	u32 tcp_ip_status;
	u8 skb_copy_flag = 0;
	u8 skb_padding_flag = 0;

	i = rx_ring->next_to_clean;

	while (*work_done < work_to_do) {
		/* Check Rx descriptor status */
		rx_desc = PCH_GBE_RX_DESC(*rx_ring, i);
		if (rx_desc->gbec_status == DSC_INIT16)
			break;
		cleaned = true;
		cleaned_count++;

		dma_status = rx_desc->dma_status;
		gbec_status = rx_desc->gbec_status;
		tcp_ip_status = rx_desc->tcp_ip_status;
		rx_desc->gbec_status = DSC_INIT16;
		buffer_info = &rx_ring->buffer_info[i];
		skb = buffer_info->skb;

		/* unmap dma */
		dma_unmap_single(&pdev->dev, buffer_info->dma,
				   buffer_info->length, DMA_FROM_DEVICE);
		buffer_info->mapped = false;
		/* Prefetch the packet */
		prefetch(skb->data);

		pr_debug("RxDecNo = 0x%04x  Status[DMA:0x%02x GBE:0x%04x "
			 "TCP:0x%08x]  BufInf = 0x%p\n",
			 i, dma_status, gbec_status, tcp_ip_status,
			 buffer_info);
		/* Error check */
		if (unlikely(gbec_status & PCH_GBE_RXD_GMAC_STAT_NOTOCTAL)) {
			adapter->stats.rx_frame_errors++;
			pr_err("Receive Not Octal Error\n");
		} else if (unlikely(gbec_status &
				PCH_GBE_RXD_GMAC_STAT_NBLERR)) {
			adapter->stats.rx_frame_errors++;
			pr_err("Receive Nibble Error\n");
		} else if (unlikely(gbec_status &
				PCH_GBE_RXD_GMAC_STAT_CRCERR)) {
			adapter->stats.rx_crc_errors++;
			pr_err("Receive CRC Error\n");
		} else {
			/* get receive length */
			/* length convert[-3], padding[-2] */
			length = (rx_desc->rx_words_eob) - 3 - 2;

			/* Decide the data conversion method */
			if (!adapter->rx_csum) {
				/* [Header:14][payload] */
				skb_padding_flag = 0;
				skb_copy_flag = 1;
			} else {
				/* [Header:14][padding:2][payload] */
				skb_padding_flag = 1;
				if (length < copybreak)
					skb_copy_flag = 1;
				else
					skb_copy_flag = 0;
			}

			/* Data conversion */
			if (skb_copy_flag) {	/* recycle  skb */
				struct sk_buff *new_skb;
				new_skb =
				    netdev_alloc_skb(netdev,
						     length + NET_IP_ALIGN);
				if (new_skb) {
					if (!skb_padding_flag) {
						skb_reserve(new_skb,
								NET_IP_ALIGN);
					}
					memcpy(new_skb->data, skb->data,
						length);
					/* save the skb
					 * in buffer_info as good */
					skb = new_skb;
				} else if (!skb_padding_flag) {
					/* dorrop error */
					pr_err("New skb allocation Error\n");
					goto dorrop;
				}
			} else {
				buffer_info->skb = NULL;
			}
			if (skb_padding_flag) {
				memcpy(&tmp_packet[0], &skb->data[0], ETH_HLEN);
				memcpy(&skb->data[NET_IP_ALIGN], &tmp_packet[0],
					ETH_HLEN);
				skb_reserve(skb, NET_IP_ALIGN);

			}

			/* update status of driver */
			adapter->stats.rx_bytes += length;
			adapter->stats.rx_packets++;
			if ((gbec_status & PCH_GBE_RXD_GMAC_STAT_MARMLT))
				adapter->stats.multicast++;
			/* Write meta date of skb */
			skb_put(skb, length);
			skb->protocol = eth_type_trans(skb, netdev);
			if ((tcp_ip_status & PCH_GBE_RXD_ACC_STAT_TCPIPOK) ==
			    PCH_GBE_RXD_ACC_STAT_TCPIPOK) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				skb->ip_summed = CHECKSUM_NONE;
			}
			napi_gro_receive(&adapter->napi, skb);
			(*work_done)++;
			pr_debug("Receive skb->ip_summed: %d length: %d\n",
				 skb->ip_summed, length);
		}
dorrop:
		/* return some buffers to hardware, one at a time is too slow */
		if (unlikely(cleaned_count >= PCH_GBE_RX_BUFFER_WRITE)) {
			pch_gbe_alloc_rx_buffers(adapter, rx_ring,
						 cleaned_count);
			cleaned_count = 0;
		}
		if (++i == rx_ring->count)
			i = 0;
	}
	rx_ring->next_to_clean = i;
	if (cleaned_count)
		pch_gbe_alloc_rx_buffers(adapter, rx_ring, cleaned_count);
	return cleaned;
}

/**
 * pch_gbe_setup_tx_resources - Allocate Tx resources (Descriptors)
 * @adapter:  Board private structure
 * @tx_ring:  Tx descriptor ring (for a specific queue) to setup
 * Returns
 *	0:		Successfully
 *	Negative value:	Failed
 */
int pch_gbe_setup_tx_resources(struct pch_gbe_adapter *adapter,
				struct pch_gbe_tx_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_tx_desc *tx_desc;
	int size;
	int desNo;

	size = (int)sizeof(struct pch_gbe_buffer) * tx_ring->count;
	tx_ring->buffer_info = vmalloc(size);
	if (!tx_ring->buffer_info) {
		pr_err("Unable to allocate memory for the buffer infomation\n");
		return -ENOMEM;
	}
	memset(tx_ring->buffer_info, 0, size);

	tx_ring->size = tx_ring->count * (int)sizeof(struct pch_gbe_tx_desc);

	tx_ring->desc = dma_alloc_coherent(&pdev->dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->desc) {
		vfree(tx_ring->buffer_info);
		pr_err("Unable to allocate memory for the transmit descriptor ring\n");
		return -ENOMEM;
	}
	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	spin_lock_init(&tx_ring->tx_lock);

	for (desNo = 0; desNo < tx_ring->count; desNo++) {
		tx_desc = PCH_GBE_TX_DESC(*tx_ring, desNo);
		tx_desc->gbec_status = DSC_INIT16;
	}
	pr_debug("tx_ring->desc = 0x%p  tx_ring->dma = 0x%08llx\n"
		 "next_to_clean = 0x%08x  next_to_use = 0x%08x\n",
		 tx_ring->desc, (unsigned long long)tx_ring->dma,
		 tx_ring->next_to_clean, tx_ring->next_to_use);
	return 0;
}

/**
 * pch_gbe_setup_rx_resources - Allocate Rx resources (Descriptors)
 * @adapter:  Board private structure
 * @rx_ring:  Rx descriptor ring (for a specific queue) to setup
 * Returns
 *	0:		Successfully
 *	Negative value:	Failed
 */
int pch_gbe_setup_rx_resources(struct pch_gbe_adapter *adapter,
				struct pch_gbe_rx_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_rx_desc *rx_desc;
	int size;
	int desNo;

	size = (int)sizeof(struct pch_gbe_buffer) * rx_ring->count;
	rx_ring->buffer_info = vmalloc(size);
	if (!rx_ring->buffer_info) {
		pr_err("Unable to allocate memory for the receive descriptor ring\n");
		return -ENOMEM;
	}
	memset(rx_ring->buffer_info, 0, size);
	rx_ring->size = rx_ring->count * (int)sizeof(struct pch_gbe_rx_desc);
	rx_ring->desc =	dma_alloc_coherent(&pdev->dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);

	if (!rx_ring->desc) {
		pr_err("Unable to allocate memory for the receive descriptor ring\n");
		vfree(rx_ring->buffer_info);
		return -ENOMEM;
	}
	memset(rx_ring->desc, 0, rx_ring->size);
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
	for (desNo = 0; desNo < rx_ring->count; desNo++) {
		rx_desc = PCH_GBE_RX_DESC(*rx_ring, desNo);
		rx_desc->gbec_status = DSC_INIT16;
	}
	pr_debug("rx_ring->desc = 0x%p  rx_ring->dma = 0x%08llx "
		 "next_to_clean = 0x%08x  next_to_use = 0x%08x\n",
		 rx_ring->desc, (unsigned long long)rx_ring->dma,
		 rx_ring->next_to_clean, rx_ring->next_to_use);
	return 0;
}

/**
 * pch_gbe_free_tx_resources - Free Tx Resources
 * @adapter:  Board private structure
 * @tx_ring:  Tx descriptor ring for a specific queue
 */
void pch_gbe_free_tx_resources(struct pch_gbe_adapter *adapter,
				struct pch_gbe_tx_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	pch_gbe_clean_tx_ring(adapter, tx_ring);
	vfree(tx_ring->buffer_info);
	tx_ring->buffer_info = NULL;
	pci_free_consistent(pdev, tx_ring->size, tx_ring->desc, tx_ring->dma);
	tx_ring->desc = NULL;
}

/**
 * pch_gbe_free_rx_resources - Free Rx Resources
 * @adapter:  Board private structure
 * @rx_ring:  Ring to clean the resources from
 */
void pch_gbe_free_rx_resources(struct pch_gbe_adapter *adapter,
				struct pch_gbe_rx_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	pch_gbe_clean_rx_ring(adapter, rx_ring);
	vfree(rx_ring->buffer_info);
	rx_ring->buffer_info = NULL;
	pci_free_consistent(pdev, rx_ring->size, rx_ring->desc, rx_ring->dma);
	rx_ring->desc = NULL;
}

/**
 * pch_gbe_request_irq - Allocate an interrupt line
 * @adapter:  Board private structure
 * Returns
 *	0:		Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_request_irq(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;
	int flags;

	flags = IRQF_SHARED;
	adapter->have_msi = false;
	err = pci_enable_msi(adapter->pdev);
	pr_debug("call pci_enable_msi\n");
	if (err) {
		pr_debug("call pci_enable_msi - Error: %d\n", err);
	} else {
		flags = 0;
		adapter->have_msi = true;
	}
	err = request_irq(adapter->pdev->irq, &pch_gbe_intr,
			  flags, netdev->name, netdev);
	if (err)
		pr_err("Unable to allocate interrupt Error: %d\n", err);
	pr_debug("adapter->have_msi : %d  flags : 0x%04x  return : 0x%04x\n",
		 adapter->have_msi, flags, err);
	return err;
}


static void pch_gbe_set_multi(struct net_device *netdev);
/**
 * pch_gbe_up - Up GbE network device
 * @adapter:  Board private structure
 * Returns
 *	0:		Successfully
 *	Negative value:	Failed
 */
int pch_gbe_up(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pch_gbe_tx_ring *tx_ring = adapter->tx_ring;
	struct pch_gbe_rx_ring *rx_ring = adapter->rx_ring;
	int err;

	/* hardware has been reset, we need to reload some things */
	pch_gbe_set_multi(netdev);

	pch_gbe_setup_tctl(adapter);
	pch_gbe_configure_tx(adapter);
	pch_gbe_setup_rctl(adapter);
	pch_gbe_configure_rx(adapter);

	err = pch_gbe_request_irq(adapter);
	if (err) {
		pr_err("Error: can't bring device up\n");
		return err;
	}
	pch_gbe_alloc_tx_buffers(adapter, tx_ring);
	pch_gbe_alloc_rx_buffers(adapter, rx_ring, rx_ring->count);
	adapter->tx_queue_len = netdev->tx_queue_len;

	mod_timer(&adapter->watchdog_timer, jiffies);

	napi_enable(&adapter->napi);
	pch_gbe_irq_enable(adapter);
	netif_start_queue(adapter->netdev);

	return 0;
}

/**
 * pch_gbe_down - Down GbE network device
 * @adapter:  Board private structure
 */
void pch_gbe_down(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer */
	napi_disable(&adapter->napi);
	atomic_set(&adapter->irq_sem, 0);

	pch_gbe_irq_disable(adapter);
	pch_gbe_free_irq(adapter);

	del_timer_sync(&adapter->watchdog_timer);

	netdev->tx_queue_len = adapter->tx_queue_len;
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	pch_gbe_reset(adapter);
	pch_gbe_clean_tx_ring(adapter, adapter->tx_ring);
	pch_gbe_clean_rx_ring(adapter, adapter->rx_ring);
}

/**
 * pch_gbe_sw_init - Initialize general software structures (struct pch_gbe_adapter)
 * @adapter:  Board private structure to initialize
 * Returns
 *	0:		Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_sw_init(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;

	adapter->rx_buffer_len = PCH_GBE_FRAME_SIZE_2048;
	hw->mac.max_frame_size = netdev->mtu + ETH_HLEN + ETH_FCS_LEN;
	hw->mac.min_frame_size = ETH_ZLEN + ETH_FCS_LEN;

	/* Initialize the hardware-specific values */
	if (pch_gbe_hal_setup_init_funcs(hw)) {
		pr_err("Hardware Initialization Failure\n");
		return -EIO;
	}
	if (pch_gbe_alloc_queues(adapter)) {
		pr_err("Unable to allocate memory for queues\n");
		return -ENOMEM;
	}
	spin_lock_init(&adapter->hw.miim_lock);
	spin_lock_init(&adapter->tx_queue_lock);
	spin_lock_init(&adapter->stats_lock);
	spin_lock_init(&adapter->ethtool_lock);
	atomic_set(&adapter->irq_sem, 0);
	pch_gbe_irq_disable(adapter);

	pch_gbe_init_stats(adapter);

	pr_debug("rx_buffer_len : %d  mac.min_frame_size : %d  mac.max_frame_size : %d\n",
		 (u32) adapter->rx_buffer_len,
		 hw->mac.min_frame_size, hw->mac.max_frame_size);
	return 0;
}

/**
 * pch_gbe_open - Called when a network interface is made active
 * @netdev:	Network interface device structure
 * Returns
 *	0:		Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_open(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	int err;

	/* allocate transmit descriptors */
	err = pch_gbe_setup_tx_resources(adapter, adapter->tx_ring);
	if (err)
		goto err_setup_tx;
	/* allocate receive descriptors */
	err = pch_gbe_setup_rx_resources(adapter, adapter->rx_ring);
	if (err)
		goto err_setup_rx;
	pch_gbe_hal_power_up_phy(hw);
	err = pch_gbe_up(adapter);
	if (err)
		goto err_up;
	pr_debug("Success End\n");
	return 0;

err_up:
	if (!adapter->wake_up_evt)
		pch_gbe_hal_power_down_phy(hw);
	pch_gbe_free_rx_resources(adapter, adapter->rx_ring);
err_setup_rx:
	pch_gbe_free_tx_resources(adapter, adapter->tx_ring);
err_setup_tx:
	pch_gbe_reset(adapter);
	pr_err("Error End\n");
	return err;
}

/**
 * pch_gbe_stop - Disables a network interface
 * @netdev:  Network interface device structure
 * Returns
 *	0: Successfully
 */
static int pch_gbe_stop(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	pch_gbe_down(adapter);
	if (!adapter->wake_up_evt)
		pch_gbe_hal_power_down_phy(hw);
	pch_gbe_free_tx_resources(adapter, adapter->tx_ring);
	pch_gbe_free_rx_resources(adapter, adapter->rx_ring);
	return 0;
}

/**
 * pch_gbe_xmit_frame - Packet transmitting start
 * @skb:     Socket buffer structure
 * @netdev:  Network interface device structure
 * Returns
 *	- NETDEV_TX_OK:   Normal end
 *	- NETDEV_TX_BUSY: Error end
 */
static int pch_gbe_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_tx_ring *tx_ring = adapter->tx_ring;
	unsigned long flags;

	if (unlikely(skb->len > (adapter->hw.mac.max_frame_size - 4))) {
		pr_err("Transfer length Error: skb len: %d > max: %d\n",
		       skb->len, adapter->hw.mac.max_frame_size);
		dev_kfree_skb_any(skb);
		adapter->stats.tx_length_errors++;
		return NETDEV_TX_OK;
	}
	if (!spin_trylock_irqsave(&tx_ring->tx_lock, flags)) {
		/* Collision - tell upper layer to requeue */
		return NETDEV_TX_LOCKED;
	}
	if (unlikely(!PCH_GBE_DESC_UNUSED(tx_ring))) {
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
		pr_debug("Return : BUSY  next_to use : 0x%08x  next_to clean : 0x%08x\n",
			 tx_ring->next_to_use, tx_ring->next_to_clean);
		return NETDEV_TX_BUSY;
	}
	spin_unlock_irqrestore(&tx_ring->tx_lock, flags);

	/* CRC,ITAG no support */
	pch_gbe_tx_queue(adapter, tx_ring, skb);
	return NETDEV_TX_OK;
}

/**
 * pch_gbe_get_stats - Get System Network Statistics
 * @netdev:  Network interface device structure
 * Returns:  The current stats
 */
static struct net_device_stats *pch_gbe_get_stats(struct net_device *netdev)
{
	/* only return the current stats */
	return &netdev->stats;
}

/**
 * pch_gbe_set_multi - Multicast and Promiscuous mode set
 * @netdev:   Network interface device structure
 */
static void pch_gbe_set_multi(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	struct netdev_hw_addr *ha;
	u8 *mta_list;
	u32 rctl;
	int i;
	int mc_count;

	pr_debug("netdev->flags : 0x%08x\n", netdev->flags);

	/* Check for Promiscuous and All Multicast modes */
	rctl = ioread32(&hw->reg->RX_MODE);
	mc_count = netdev_mc_count(netdev);
	if ((netdev->flags & IFF_PROMISC)) {
		rctl &= ~PCH_GBE_ADD_FIL_EN;
		rctl &= ~PCH_GBE_MLT_FIL_EN;
	} else if ((netdev->flags & IFF_ALLMULTI)) {
		/* all the multicasting receive permissions */
		rctl |= PCH_GBE_ADD_FIL_EN;
		rctl &= ~PCH_GBE_MLT_FIL_EN;
	} else {
		if (mc_count >= PCH_GBE_MAR_ENTRIES) {
			/* all the multicasting receive permissions */
			rctl |= PCH_GBE_ADD_FIL_EN;
			rctl &= ~PCH_GBE_MLT_FIL_EN;
		} else {
			rctl |= (PCH_GBE_ADD_FIL_EN | PCH_GBE_MLT_FIL_EN);
		}
	}
	iowrite32(rctl, &hw->reg->RX_MODE);

	if (mc_count >= PCH_GBE_MAR_ENTRIES)
		return;
	mta_list = kmalloc(mc_count * ETH_ALEN, GFP_ATOMIC);
	if (!mta_list)
		return;

	/* The shared function expects a packed array of only addresses. */
	i = 0;
	netdev_for_each_mc_addr(ha, netdev) {
		if (i == mc_count)
			break;
		memcpy(mta_list + (i++ * ETH_ALEN), &ha->addr, ETH_ALEN);
	}
	pch_gbe_mac_mc_addr_list_update(hw, mta_list, i, 1,
					PCH_GBE_MAR_ENTRIES);
	kfree(mta_list);

	pr_debug("RX_MODE reg(check bit31,30 ADD,MLT) : 0x%08x  netdev->mc_count : 0x%08x\n",
		 ioread32(&hw->reg->RX_MODE), mc_count);
}

/**
 * pch_gbe_set_mac - Change the Ethernet Address of the NIC
 * @netdev: Network interface device structure
 * @addr:   Pointer to an address structure
 * Returns
 *	0:		Successfully
 *	-EADDRNOTAVAIL:	Failed
 */
static int pch_gbe_set_mac(struct net_device *netdev, void *addr)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *skaddr = addr;
	int ret_val;

	if (!is_valid_ether_addr(skaddr->sa_data)) {
		ret_val = -EADDRNOTAVAIL;
	} else {
		memcpy(netdev->dev_addr, skaddr->sa_data, netdev->addr_len);
		memcpy(adapter->hw.mac.addr, skaddr->sa_data, netdev->addr_len);
		pch_gbe_mac_mar_set(&adapter->hw, adapter->hw.mac.addr, 0);
		ret_val = 0;
	}
	pr_debug("ret_val : 0x%08x\n", ret_val);
	pr_debug("dev_addr : %pM\n", netdev->dev_addr);
	pr_debug("mac_addr : %pM\n", adapter->hw.mac.addr);
	pr_debug("MAC_ADR1AB reg : 0x%08x 0x%08x\n",
		 ioread32(&adapter->hw.reg->mac_adr[0].high),
		 ioread32(&adapter->hw.reg->mac_adr[0].low));
	return ret_val;
}

/**
 * pch_gbe_change_mtu - Change the Maximum Transfer Unit
 * @netdev:   Network interface device structure
 * @new_mtu:  New value for maximum frame size
 * Returns
 *	0:		Successfully
 *	-EINVAL:	Failed
 */
static int pch_gbe_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	int max_frame;

	max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN;
	if ((max_frame < ETH_ZLEN + ETH_FCS_LEN) ||
		(max_frame > PCH_GBE_MAX_JUMBO_FRAME_SIZE)) {
		pr_err("Invalid MTU setting\n");
		return -EINVAL;
	}
	if (max_frame <= PCH_GBE_FRAME_SIZE_2048)
		adapter->rx_buffer_len = PCH_GBE_FRAME_SIZE_2048;
	else if (max_frame <= PCH_GBE_FRAME_SIZE_4096)
		adapter->rx_buffer_len = PCH_GBE_FRAME_SIZE_4096;
	else if (max_frame <= PCH_GBE_FRAME_SIZE_8192)
		adapter->rx_buffer_len = PCH_GBE_FRAME_SIZE_8192;
	else
		adapter->rx_buffer_len = PCH_GBE_MAX_JUMBO_FRAME_SIZE;
	netdev->mtu = new_mtu;
	adapter->hw.mac.max_frame_size = max_frame;

	if (netif_running(netdev))
		pch_gbe_reinit_locked(adapter);
	else
		pch_gbe_reset(adapter);

	pr_debug("max_frame : %d  rx_buffer_len : %d  mtu : %d  max_frame_size : %d\n",
		 max_frame, (u32) adapter->rx_buffer_len, netdev->mtu,
		 adapter->hw.mac.max_frame_size);
	return 0;
}

/**
 * pch_gbe_ioctl - Controls register through a MII interface
 * @netdev:   Network interface device structure
 * @ifr:      Pointer to ifr structure
 * @cmd:      Control command
 * Returns
 *	0:	Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	pr_debug("cmd : 0x%04x\n", cmd);

	return generic_mii_ioctl(&adapter->mii, if_mii(ifr), cmd, NULL);
}

/**
 * pch_gbe_tx_timeout - Respond to a Tx Hang
 * @netdev:   Network interface device structure
 */
static void pch_gbe_tx_timeout(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	adapter->stats.tx_timeout_count++;
	schedule_work(&adapter->reset_task);
}

/**
 * pch_gbe_napi_poll - NAPI receive and transfer polling callback
 * @napi:    Pointer of polling device struct
 * @budget:  The maximum number of a packet
 * Returns
 *	false:  Exit the polling mode
 *	true:   Continue the polling mode
 */
static int pch_gbe_napi_poll(struct napi_struct *napi, int budget)
{
	struct pch_gbe_adapter *adapter =
	    container_of(napi, struct pch_gbe_adapter, napi);
	struct net_device *netdev = adapter->netdev;
	int work_done = 0;
	bool poll_end_flag = false;
	bool cleaned = false;

	pr_debug("budget : %d\n", budget);

	/* Keep link state information with original netdev */
	if (!netif_carrier_ok(netdev)) {
		poll_end_flag = true;
	} else {
		cleaned = pch_gbe_clean_tx(adapter, adapter->tx_ring);
		pch_gbe_clean_rx(adapter, adapter->rx_ring, &work_done, budget);

		if (cleaned)
			work_done = budget;
		/* If no Tx and not enough Rx work done,
		 * exit the polling mode
		 */
		if ((work_done < budget) || !netif_running(netdev))
			poll_end_flag = true;
	}

	if (poll_end_flag) {
		napi_complete(napi);
		pch_gbe_irq_enable(adapter);
	}

	pr_debug("poll_end_flag : %d  work_done : %d  budget : %d\n",
		 poll_end_flag, work_done, budget);

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * pch_gbe_netpoll - Used by things like netconsole to send skbs
 * @netdev:  Network interface device structure
 */
static void pch_gbe_netpoll(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	disable_irq(adapter->pdev->irq);
	pch_gbe_intr(adapter->pdev->irq, netdev);
	enable_irq(adapter->pdev->irq);
}
#endif

static const struct net_device_ops pch_gbe_netdev_ops = {
	.ndo_open = pch_gbe_open,
	.ndo_stop = pch_gbe_stop,
	.ndo_start_xmit = pch_gbe_xmit_frame,
	.ndo_get_stats = pch_gbe_get_stats,
	.ndo_set_mac_address = pch_gbe_set_mac,
	.ndo_tx_timeout = pch_gbe_tx_timeout,
	.ndo_change_mtu = pch_gbe_change_mtu,
	.ndo_do_ioctl = pch_gbe_ioctl,
	.ndo_set_multicast_list = &pch_gbe_set_multi,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = pch_gbe_netpoll,
#endif
};

static pci_ers_result_t pch_gbe_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);
	if (netif_running(netdev))
		pch_gbe_down(adapter);
	pci_disable_device(pdev);
	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t pch_gbe_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	if (pci_enable_device(pdev)) {
		pr_err("Cannot re-enable PCI device after reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);
	pch_gbe_hal_power_up_phy(hw);
	pch_gbe_reset(adapter);
	/* Clear wake up status */
	pch_gbe_mac_set_wol_event(hw, 0);

	return PCI_ERS_RESULT_RECOVERED;
}

static void pch_gbe_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev)) {
		if (pch_gbe_up(adapter)) {
			pr_debug("can't bring device back up after reset\n");
			return;
		}
	}
	netif_device_attach(netdev);
}

static int __pch_gbe_suspend(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 wufc = adapter->wake_up_evt;
	int retval = 0;

	netif_device_detach(netdev);
	if (netif_running(netdev))
		pch_gbe_down(adapter);
	if (wufc) {
		pch_gbe_set_multi(netdev);
		pch_gbe_setup_rctl(adapter);
		pch_gbe_configure_rx(adapter);
		pch_gbe_set_rgmii_ctrl(adapter, hw->mac.link_speed,
					hw->mac.link_duplex);
		pch_gbe_set_mode(adapter, hw->mac.link_speed,
					hw->mac.link_duplex);
		pch_gbe_mac_set_wol_event(hw, wufc);
		pci_disable_device(pdev);
	} else {
		pch_gbe_hal_power_down_phy(hw);
		pch_gbe_mac_set_wol_event(hw, wufc);
		pci_disable_device(pdev);
	}
	return retval;
}

#ifdef CONFIG_PM
static int pch_gbe_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);

	return __pch_gbe_suspend(pdev);
}

static int pch_gbe_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 err;

	err = pci_enable_device(pdev);
	if (err) {
		pr_err("Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);
	pch_gbe_hal_power_up_phy(hw);
	pch_gbe_reset(adapter);
	/* Clear wake on lan control and status */
	pch_gbe_mac_set_wol_event(hw, 0);

	if (netif_running(netdev))
		pch_gbe_up(adapter);
	netif_device_attach(netdev);

	return 0;
}
#endif /* CONFIG_PM */

static void pch_gbe_shutdown(struct pci_dev *pdev)
{
	__pch_gbe_suspend(pdev);
	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, true);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

static void pch_gbe_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	flush_scheduled_work();
	unregister_netdev(netdev);

	pch_gbe_hal_phy_hw_reset(&adapter->hw);

	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);

	iounmap(adapter->hw.reg);
	pci_release_regions(pdev);
	free_netdev(netdev);
	pci_disable_device(pdev);
}

static int pch_gbe_probe(struct pci_dev *pdev,
			  const struct pci_device_id *pci_id)
{
	struct net_device *netdev;
	struct pch_gbe_adapter *adapter;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64))
		|| pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret) {
			ret = pci_set_consistent_dma_mask(pdev,
							  DMA_BIT_MASK(32));
			if (ret) {
				dev_err(&pdev->dev, "ERR: No usable DMA "
					"configuration, aborting\n");
				goto err_disable_device;
			}
		}
	}

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev,
			"ERR: Can't reserve PCI I/O and memory resources\n");
		goto err_disable_device;
	}
	pci_set_master(pdev);

	netdev = alloc_etherdev((int)sizeof(struct pch_gbe_adapter));
	if (!netdev) {
		ret = -ENOMEM;
		dev_err(&pdev->dev,
			"ERR: Can't allocate and set up an Ethernet device\n");
		goto err_release_pci;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->hw.back = adapter;
	adapter->hw.reg = pci_iomap(pdev, PCH_GBE_PCI_BAR, 0);
	if (!adapter->hw.reg) {
		ret = -EIO;
		dev_err(&pdev->dev, "Can't ioremap\n");
		goto err_free_netdev;
	}

	netdev->netdev_ops = &pch_gbe_netdev_ops;
	netdev->watchdog_timeo = PCH_GBE_WATCHDOG_PERIOD;
	netif_napi_add(netdev, &adapter->napi,
		       pch_gbe_napi_poll, PCH_GBE_RX_WEIGHT);
	netdev->features = NETIF_F_HW_CSUM | NETIF_F_GRO;
	pch_gbe_set_ethtool_ops(netdev);

	pch_gbe_mac_reset_hw(&adapter->hw);

	/* setup the private structure */
	ret = pch_gbe_sw_init(adapter);
	if (ret)
		goto err_iounmap;

	/* Initialize PHY */
	ret = pch_gbe_init_phy(adapter);
	if (ret) {
		dev_err(&pdev->dev, "PHY initialize error\n");
		goto err_free_adapter;
	}
	pch_gbe_hal_get_bus_info(&adapter->hw);

	/* Read the MAC address. and store to the private data */
	ret = pch_gbe_hal_read_mac_addr(&adapter->hw);
	if (ret) {
		dev_err(&pdev->dev, "MAC address Read Error\n");
		goto err_free_adapter;
	}

	memcpy(netdev->dev_addr, adapter->hw.mac.addr, netdev->addr_len);
	if (!is_valid_ether_addr(netdev->dev_addr)) {
		dev_err(&pdev->dev, "Invalid MAC Address\n");
		ret = -EIO;
		goto err_free_adapter;
	}
	setup_timer(&adapter->watchdog_timer, pch_gbe_watchdog,
		    (unsigned long)adapter);

	INIT_WORK(&adapter->reset_task, pch_gbe_reset_task);

	pch_gbe_check_options(adapter);

	if (adapter->tx_csum)
		netdev->features |= NETIF_F_HW_CSUM;
	else
		netdev->features &= ~NETIF_F_HW_CSUM;

	/* initialize the wol settings based on the eeprom settings */
	adapter->wake_up_evt = PCH_GBE_WL_INIT_SETTING;
	dev_info(&pdev->dev, "MAC address : %pM\n", netdev->dev_addr);

	/* reset the hardware with the new settings */
	pch_gbe_reset(adapter);

	ret = register_netdev(netdev);
	if (ret)
		goto err_free_adapter;
	/* tell the stack to leave us alone until pch_gbe_open() is called */
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	dev_dbg(&pdev->dev, "OKIsemi(R) PCH Network Connection\n");

	device_set_wakeup_enable(&pdev->dev, 1);
	return 0;

err_free_adapter:
	pch_gbe_hal_phy_hw_reset(&adapter->hw);
	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);
err_iounmap:
	iounmap(adapter->hw.reg);
err_free_netdev:
	free_netdev(netdev);
err_release_pci:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static DEFINE_PCI_DEVICE_TABLE(pch_gbe_pcidev_id) = {
	{.vendor = PCI_VENDOR_ID_INTEL,
	 .device = PCI_DEVICE_ID_INTEL_IOH1_GBE,
	 .subvendor = PCI_ANY_ID,
	 .subdevice = PCI_ANY_ID,
	 .class = (PCI_CLASS_NETWORK_ETHERNET << 8),
	 .class_mask = (0xFFFF00)
	 },
	/* required last entry */
	{0}
};

#ifdef CONFIG_PM
static const struct dev_pm_ops pch_gbe_pm_ops = {
	.suspend = pch_gbe_suspend,
	.resume = pch_gbe_resume,
	.freeze = pch_gbe_suspend,
	.thaw = pch_gbe_resume,
	.poweroff = pch_gbe_suspend,
	.restore = pch_gbe_resume,
};
#endif

static struct pci_error_handlers pch_gbe_err_handler = {
	.error_detected = pch_gbe_io_error_detected,
	.slot_reset = pch_gbe_io_slot_reset,
	.resume = pch_gbe_io_resume
};

static struct pci_driver pch_gbe_pcidev = {
	.name = KBUILD_MODNAME,
	.id_table = pch_gbe_pcidev_id,
	.probe = pch_gbe_probe,
	.remove = pch_gbe_remove,
#ifdef CONFIG_PM_OPS
	.driver.pm = &pch_gbe_pm_ops,
#endif
	.shutdown = pch_gbe_shutdown,
	.err_handler = &pch_gbe_err_handler
};


static int __init pch_gbe_init_module(void)
{
	int ret;

	ret = pci_register_driver(&pch_gbe_pcidev);
	if (copybreak != PCH_GBE_COPYBREAK_DEFAULT) {
		if (copybreak == 0) {
			pr_info("copybreak disabled\n");
		} else {
			pr_info("copybreak enabled for packets <= %u bytes\n",
				copybreak);
		}
	}
	return ret;
}

static void __exit pch_gbe_exit_module(void)
{
	pci_unregister_driver(&pch_gbe_pcidev);
}

module_init(pch_gbe_init_module);
module_exit(pch_gbe_exit_module);

MODULE_DESCRIPTION("OKI semiconductor PCH Gigabit ethernet Driver");
MODULE_AUTHOR("OKI semiconductor, <masa-korg@dsn.okisemi.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, pch_gbe_pcidev_id);

module_param(copybreak, uint, 0644);
MODULE_PARM_DESC(copybreak,
	"Maximum size of packet that is copied to a new buffer on receive");

/* pch_gbe_main.c */
