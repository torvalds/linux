/*******************************************************************************
 * Copyright © 2012, Shuge
 *		Author: shuge  <shugeLinux@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 ********************************************************************************/

#include <linux/io.h>
#include <linux/bitrev.h>
#include <linux/crc32.h>

#include "gmac_ethtool.h"
#include "sunxi_gmac.h"
#include "gmac_desc.h"

#undef GMAC_BASE_DEBUG
#ifdef GMAC_BASE_DEBUG
#define BASE_DBG(fmt, args...)  printk(fmt, ## args)
#else
#define BASE_DBG(fmt, args...)  do { } while (0)
#endif

/*
 *
 * Sun6i platform gmac dma operations
 *
 */
int gdma_init(void __iomem *ioaddr, int pbl, u32 dma_tx,
			      u32 dma_rx)
{
	u32 value = readl(ioaddr + GDMA_BUS_MODE);
	int limit;

	/* DMA SW reset */
	value |= SOFT_RESET;
	writel(value, ioaddr + GDMA_BUS_MODE);
	limit = 10;
	while (limit--) {
		if (!(readl(ioaddr + GDMA_BUS_MODE) & SOFT_RESET))
			break;
		mdelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	value = BUS_MODE_FIXBUST | BUS_MODE_4PBL |
	    ((pbl << BUS_MODE_PBL_SHIFT) |
	     (pbl << BUS_MODE_RPBL_SHIFT));

#ifdef CONFIG_GMAC_DA
	value |= BUS_MODE_DA;	/* Rx has priority over tx */
#endif
	writel(value, ioaddr + GDMA_BUS_MODE);

	/* Mask interrupts by writing to CSR7 */
	writel(GDMA_DEF_INTR, ioaddr + GDMA_INTR_ENA);

	/* Write the base address of Rx/Tx descriptor lists into registers */
	writel(dma_tx, ioaddr + GDMA_XMT_LIST);
	writel(dma_rx, ioaddr + GDMA_RCV_LIST);

	return 0;
}

void dma_oper_mode(void __iomem *ioaddr, int txmode,
				    int rxmode)
{
	u32 op_val = readl(ioaddr + GDMA_OP_MODE);

	if (txmode == SF_DMA_MODE) {
		pr_debug(KERN_DEBUG "GMAC: enable TX store and forward mode\n");
		/* Transmit COE type 2 cannot be done in cut-through mode. */
		op_val |= OP_MODE_TSF;
		/* Operating on second frame increase the performance
		 * especially when transmit store-and-forward is used.*/
		op_val |= OP_MODE_OSF;
	} else {
		pr_debug(KERN_DEBUG "GMAC: disabling TX store and forward mode"
			      " (threshold = %d)\n", txmode);
		op_val &= ~OP_MODE_TSF;
		op_val &= OP_MODE_TC_TX_MASK;
		/* Set the transmit threshold */
		if (txmode <= 32)
			op_val |= OP_MODE_TTC_32;
		else if (txmode <= 64)
			op_val |= OP_MODE_TTC_64;
		else if (txmode <= 128)
			op_val |= OP_MODE_TTC_128;
		else if (txmode <= 192)
			op_val |= OP_MODE_TTC_192;
		else
			op_val |= OP_MODE_TTC_256;
	}

	if (rxmode == SF_DMA_MODE) {
		pr_debug(KERN_DEBUG "GMAC: enable RX store and forward mode\n");
		op_val |= OP_MODE_RSF;
	} else {
		pr_debug(KERN_DEBUG "GMAC: disabling RX store and forward mode"
			      " (threshold = %d)\n", rxmode);
		op_val &= ~OP_MODE_RSF;
		op_val &= OP_MODE_TC_RX_MASK;
		if (rxmode <= 32)
			op_val |= OP_MODE_RTC_32;
		else if (rxmode <= 64)
			op_val |= OP_MODE_RTC_64;
		else if (rxmode <= 96)
			op_val |= OP_MODE_RTC_96;
		else
			op_val |= OP_MODE_RTC_128;
	}

	writel(op_val, ioaddr + GDMA_OP_MODE);
}

#ifdef DMA_DEBUG
static void show_tx_process_state(unsigned int status)
{
	unsigned int state;
	state = (status & STATUS_TS_MASK) >> STATUS_TS_SHIFT;

	switch (state) {
	case 0:
		printk("- TX (Stopped): Reset or Stop command\n");
		break;
	case 1:
		printk("- TX (Running):Fetching the Tx desc\n");
		break;
	case 2:
		printk("- TX (Running): Waiting for end of tx\n");
		break;
	case 3:
		printk("- TX (Running): Reading the data "
		       "and queuing the data into the Tx buf\n");
		break;
	case 6:
		printk("- TX (Suspended): Tx Buff Underflow "
		       "or an unavailable Transmit descriptor\n");
		break;
	case 7:
		printk("- TX (Running): Closing Tx descriptor\n");
		break;
	default:
		break;
	}
}

static void show_rx_process_state(unsigned int status)
{
	unsigned int state;
	state = (status & STATUS_RS_MASK) >> STATUS_RS_SHIFT;

	switch (state) {
	case 0:
		printk("- RX (Stopped): Reset or Stop command\n");
		break;
	case 1:
		printk("- RX (Running): Fetching the Rx desc\n");
		break;
	case 2:
		printk("- RX (Running):Checking for end of pkt\n");
		break;
	case 3:
		printk("- RX (Running): Waiting for Rx pkt\n");
		break;
	case 4:
		printk("- RX (Suspended): Unavailable Rx buf\n");
		break;
	case 5:
		printk("- RX (Running): Closing Rx descriptor\n");
		break;
	case 6:
		printk("- RX(Running): Flushing the current frame"
		       " from the Rx buf\n");
		break;
	case 7:
		printk("- RX (Running): Queuing the Rx frame"
		       " from the Rx buf into memory\n");
		break;
	default:
		break;
	}
}
#endif

int dma_interrupt(void __iomem *ioaddr, struct gmac_extra_stats *x)
{
	int ret = 0;
	/* read the status register (CSR5) */
	u32 intr_status = readl(ioaddr + GDMA_STATUS);

	BASE_DBG(KERN_INFO "%s: [CSR5: 0x%08x]\n", __func__, intr_status);
#ifdef DWMAC_DMA_DEBUG
	/* It displays the DMA process states (CSR5 register) */
	show_tx_process_state(intr_status);
	show_rx_process_state(intr_status);
#endif
	/* ABNORMAL interrupts */
	if (unlikely(intr_status & GDMA_STAT_AIS)) {
		BASE_DBG(KERN_INFO "CSR5[15] DMA ABNORMAL IRQ: ");
		if (unlikely(intr_status & GDMA_STAT_UNF)) {
			BASE_DBG(KERN_INFO "transmit underflow\n");
			ret = tx_hard_error_bump_tc;
			x->tx_undeflow_irq++;
		}
		if (unlikely(intr_status & GDMA_STAT_TJT)) {
			BASE_DBG(KERN_INFO "transmit jabber\n");
			x->tx_jabber_irq++;
		}
		if (unlikely(intr_status & GDMA_STAT_OVF)) {
			BASE_DBG(KERN_INFO "recv overflow\n");
			x->rx_overflow_irq++;
		}
		if (unlikely(intr_status & GDMA_STAT_RU)) {
			BASE_DBG(KERN_INFO "receive buffer unavailable\n");
			x->rx_buf_unav_irq++;
		}
		if (unlikely(intr_status & GDMA_STAT_RPS)) {
			BASE_DBG(KERN_INFO "receive process stopped\n");
			x->rx_process_stopped_irq++;
		}
		if (unlikely(intr_status & GDMA_STAT_RWT)) {
			BASE_DBG(KERN_INFO "receive watchdog\n");
			x->rx_watchdog_irq++;
		}
		if (unlikely(intr_status & GDMA_STAT_ETI)) {
			BASE_DBG(KERN_INFO "transmit early interrupt\n");
			x->tx_early_irq++;
		}
		if (unlikely(intr_status & GDMA_STAT_TPS)) {
			BASE_DBG(KERN_INFO "transmit process stopped\n");
			x->tx_process_stopped_irq++;
			ret = tx_hard_error;
		}
		if (unlikely(intr_status & GDMA_STAT_FBI)) {
			BASE_DBG(KERN_INFO "fatal bus error\n");
			x->fatal_bus_error_irq++;
			ret = tx_hard_error;
		}
	}
	/* TX/RX NORMAL interrupts */
	if (intr_status & GDMA_STAT_NIS) {
		x->normal_irq_n++;
		if (likely((intr_status & GDMA_STAT_RI) ||
			 (intr_status & (GDMA_STAT_TI))))
				ret = handle_tx_rx;
	}
	/* Optional hardware blocks, interrupts should be disabled */
	if (unlikely(intr_status & (GDMA_STAT_GLI)))
		pr_info("%s: unexpected status %08x\n", __func__, intr_status);
	/* Clear the interrupt by writing a logic 1 to the CSR5[15-0] */
	writel((intr_status & 0x1ffff), ioaddr + GDMA_STATUS);

	BASE_DBG(KERN_INFO "\n\n");
	return ret;
}

void dma_flush_tx_fifo(void __iomem *ioaddr)
{
	u32 csr6 = readl(ioaddr + GDMA_OP_MODE);
	writel((csr6 | OP_MODE_FTF), ioaddr + GDMA_OP_MODE);

	while ((readl(ioaddr + GDMA_OP_MODE) & OP_MODE_FTF));
}

void dma_dump_regs(void __iomem *ioaddr)
{
	int i;
	pr_info(" DMA registers\n");
	for (i = 0; i < 22; i++) {
		if ((i < 9) || (i > 17)) {
			int offset = i * 4;
			pr_err("\t Reg No. %d (offset 0x%x): 0x%08x\n", i,
			       (GDMA_BUS_MODE + offset),
			       readl(ioaddr + GDMA_BUS_MODE + offset));
		}
	}
}

/*
 *
 * Sun6i platform gmac core operations
 *
 */
void core_init(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + GMAC_CONTROL);
	value |= GMAC_CORE_INIT;
	writel(value, ioaddr + GMAC_CONTROL);

	/* Mask GMAC interrupts */
	writel(0x207, ioaddr + GMAC_INT_MASK);

#ifdef GMAC_VLAN_TAG_USED
	/* Tag detection without filtering */
	writel(0x0, ioaddr + GMAC_VLAN_TAG);
#endif

}

int core_en_rx_coe(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + GMAC_CONTROL);

	value |= GMAC_CTL_IPC;
	writel(value, ioaddr + GMAC_CONTROL);

	value = readl(ioaddr + GMAC_CONTROL);

	return !!(value & GMAC_CTL_IPC);
}

void core_dump_regs(void __iomem *ioaddr)
{
	int i;
	pr_info("\tDWMAC1000 regs (base addr = 0x%p)\n", ioaddr);

	for (i = 0; i < 55; i++) {
		int offset = i * 4;
		pr_info("\tReg No. %d (offset 0x%x): 0x%08x\n", i,
			offset, readl(ioaddr + offset));
	}
}

void core_set_filter(struct net_device *dev)
{
	void __iomem *ioaddr = (void __iomem *) dev->base_addr;
	unsigned int value = 0;

	pr_debug(KERN_INFO "%s: # mcasts %d, # unicast %d\n",
		 __func__, netdev_mc_count(dev), netdev_uc_count(dev));

	if (dev->flags & IFF_PROMISC)
		value = GMAC_FRAME_FILTER_PR;
	else if ((netdev_mc_count(dev) > HASH_TABLE_SIZE)
		   || (dev->flags & IFF_ALLMULTI)) {
		value = GMAC_FRAME_FILTER_PM;	/* pass all multi */
		writel(0xffffffff, ioaddr + GMAC_HASH_HIGH);
		writel(0xffffffff, ioaddr + GMAC_HASH_LOW);
	} else if (!netdev_mc_empty(dev)) {
		u32 mc_filter[2];
		struct netdev_hw_addr *ha;

		/* Hash filter for multicast */
		value = GMAC_FRAME_FILTER_HMC;

		memset(mc_filter, 0, sizeof(mc_filter));
		netdev_for_each_mc_addr(ha, dev) {
			/* The upper 6 bits of the calculated CRC are used to
			   index the contens of the hash table */
			int bit_nr =
			    bitrev32(~crc32_le(~0, ha->addr, 6)) >> 26;
			/* The most significant bit determines the register to
			 * use (H/L) while the other 5 bits determine the bit
			 * within the register. */
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
		}
		writel(mc_filter[0], ioaddr + GMAC_HASH_LOW);
		writel(mc_filter[1], ioaddr + GMAC_HASH_HIGH);
	}

	/* Handle multiple unicast addresses (perfect filtering)*/
	if (netdev_uc_count(dev) > GMAC_MAX_UNICAST_ADDRESSES)
		/* Switch to promiscuous mode is more than 8 addrs
		   are required */
		value |= GMAC_FRAME_FILTER_PR;
	else {
		int reg = 1;
		struct netdev_hw_addr *ha;

		netdev_for_each_uc_addr(ha, dev) {
			gmac_set_umac_addr(ioaddr, ha->addr, reg);
			reg++;
		}
	}

#ifdef FRAME_FILTER_DEBUG
	/* Enable Receive all mode (to debug filtering_fail errors) */
	value |= GMAC_FRAME_FILTER_RA;
#endif
	writel(value, ioaddr + GMAC_FRAME_FILTER);

	pr_debug(KERN_INFO "\tFrame Filter reg: 0x%08x\n\tHash regs: "
	    "HI 0x%08x, LO 0x%08x\n", readl(ioaddr + GMAC_FRAME_FILTER),
	    readl(ioaddr + GMAC_HASH_HIGH), readl(ioaddr + GMAC_HASH_LOW));
}

void core_flow_ctrl(void __iomem *ioaddr, unsigned int duplex,
			   unsigned int fc, unsigned int pause_time)
{
	unsigned int flow = 0;

	pr_debug(KERN_DEBUG "GMAC Flow-Control:\n");
	if (fc & FLOW_RX) {
		pr_debug(KERN_DEBUG "\tReceive Flow-Control ON\n");
		flow |= GMAC_FLOW_CTRL_RFE;
	}
	if (fc & FLOW_TX) {
		pr_debug(KERN_DEBUG "\tTransmit Flow-Control ON\n");
		flow |= GMAC_FLOW_CTRL_TFE;
	}

	if (duplex) {
		pr_debug(KERN_DEBUG "\tduplex mode: PAUSE %d\n", pause_time);
		flow |= (pause_time << GMAC_FLOW_CTRL_PT_SHIFT);
	}

	writel(flow, ioaddr + GMAC_FLOW_CTRL);
}

void core_irq_status(void __iomem *ioaddr)
{
	u32 intr_status = readl(ioaddr + GMAC_INT_STATUS);

	if (intr_status & RGMII_IRQ)
		readl(ioaddr + GMAC_RGMII_STATUS);
}

void gmac_set_umac_addr(void __iomem *ioaddr, unsigned char *addr, unsigned int reg_n)
{
	unsigned long data;

	data = (addr[5] << 8) | addr[4];
	writel(data, ioaddr + GMAC_ADDR_HI(reg_n));
	data = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(data, ioaddr + GMAC_ADDR_LO(reg_n));
}

/* Enable disable MAC RX/TX */
void gmac_set_tx_rx(void __iomem *ioaddr, bool enable)
{
	u32 value = readl(ioaddr + GMAC_CONTROL);

	if (enable)
		value |= GMAC_CTL_TE | GMAC_CTL_RE;
	else
		value &= ~(GMAC_CTL_TE | GMAC_CTL_RE);

	writel(value, ioaddr + GMAC_CONTROL);
}

void gmac_get_umac_addr(void __iomem *ioaddr, unsigned char *addr, unsigned int reg_n)
{
	unsigned int hi_addr, lo_addr;

	/* Read the MAC address from the hardware */
	hi_addr = readl(ioaddr + GMAC_ADDR_HI(reg_n));
	lo_addr = readl(ioaddr + GMAC_ADDR_LO(reg_n));

	/* Extract the MAC address from the high and low words */
	addr[0] = lo_addr & 0xff;
	addr[1] = (lo_addr >> 8) & 0xff;
	addr[2] = (lo_addr >> 16) & 0xff;
	addr[3] = (lo_addr >> 24) & 0xff;
	addr[4] = hi_addr & 0xff;
	addr[5] = (hi_addr >> 8) & 0xff;
}
