/*******************************************************************************
  This is the driver for the GMAC on-chip Ethernet controller for ST SoCs.
  DWC Ether MAC 10/100/1000 Universal version 3.41a  has been used for
  developing this code.

  Copyright (C) 2007-2009  STMicroelectronics Ltd

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

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/netdevice.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/phy.h>

#include "stmmac.h"
#include "gmac.h"

#undef GMAC_DEBUG
/*#define GMAC_DEBUG*/
#undef FRAME_FILTER_DEBUG
/*#define FRAME_FILTER_DEBUG*/
#ifdef GMAC_DEBUG
#define DBG(fmt, args...)  printk(fmt, ## args)
#else
#define DBG(fmt, args...)  do { } while (0)
#endif

static void gmac_dump_regs(unsigned long ioaddr)
{
	int i;
	pr_info("\t----------------------------------------------\n"
	       "\t  GMAC registers (base addr = 0x%8x)\n"
	       "\t----------------------------------------------\n",
	       (unsigned int)ioaddr);

	for (i = 0; i < 55; i++) {
		int offset = i * 4;
		pr_info("\tReg No. %d (offset 0x%x): 0x%08x\n", i,
		       offset, readl(ioaddr + offset));
	}
	return;
}

static int gmac_dma_init(unsigned long ioaddr, int pbl, u32 dma_tx, u32 dma_rx)
{
	u32 value = readl(ioaddr + DMA_BUS_MODE);
	/* DMA SW reset */
	value |= DMA_BUS_MODE_SFT_RESET;
	writel(value, ioaddr + DMA_BUS_MODE);
	do {} while ((readl(ioaddr + DMA_BUS_MODE) & DMA_BUS_MODE_SFT_RESET));

	value = /* DMA_BUS_MODE_FB | */ DMA_BUS_MODE_4PBL |
	    ((pbl << DMA_BUS_MODE_PBL_SHIFT) |
	     (pbl << DMA_BUS_MODE_RPBL_SHIFT));

#ifdef CONFIG_STMMAC_DA
	value |= DMA_BUS_MODE_DA;	/* Rx has priority over tx */
#endif
	writel(value, ioaddr + DMA_BUS_MODE);

	/* Mask interrupts by writing to CSR7 */
	writel(DMA_INTR_DEFAULT_MASK, ioaddr + DMA_INTR_ENA);

	/* The base address of the RX/TX descriptor lists must be written into
	 * DMA CSR3 and CSR4, respectively. */
	writel(dma_tx, ioaddr + DMA_TX_BASE_ADDR);
	writel(dma_rx, ioaddr + DMA_RCV_BASE_ADDR);

	return 0;
}

/* Transmit FIFO flush operation */
static void gmac_flush_tx_fifo(unsigned long ioaddr)
{
	u32 csr6 = readl(ioaddr + DMA_CONTROL);
	writel((csr6 | DMA_CONTROL_FTF), ioaddr + DMA_CONTROL);

	do {} while ((readl(ioaddr + DMA_CONTROL) & DMA_CONTROL_FTF));
}

static void gmac_dma_operation_mode(unsigned long ioaddr, int txmode,
				    int rxmode)
{
	u32 csr6 = readl(ioaddr + DMA_CONTROL);

	if (txmode == SF_DMA_MODE) {
		DBG(KERN_DEBUG "GMAC: enabling TX store and forward mode\n");
		/* Transmit COE type 2 cannot be done in cut-through mode. */
		csr6 |= DMA_CONTROL_TSF;
		/* Operating on second frame increase the performance
		 * especially when transmit store-and-forward is used.*/
		csr6 |= DMA_CONTROL_OSF;
	} else {
		DBG(KERN_DEBUG "GMAC: disabling TX store and forward mode"
			      " (threshold = %d)\n", txmode);
		csr6 &= ~DMA_CONTROL_TSF;
		csr6 &= DMA_CONTROL_TC_TX_MASK;
		/* Set the transmit threashold */
		if (txmode <= 32)
			csr6 |= DMA_CONTROL_TTC_32;
		else if (txmode <= 64)
			csr6 |= DMA_CONTROL_TTC_64;
		else if (txmode <= 128)
			csr6 |= DMA_CONTROL_TTC_128;
		else if (txmode <= 192)
			csr6 |= DMA_CONTROL_TTC_192;
		else
			csr6 |= DMA_CONTROL_TTC_256;
	}

	if (rxmode == SF_DMA_MODE) {
		DBG(KERN_DEBUG "GMAC: enabling RX store and forward mode\n");
		csr6 |= DMA_CONTROL_RSF;
	} else {
		DBG(KERN_DEBUG "GMAC: disabling RX store and forward mode"
			      " (threshold = %d)\n", rxmode);
		csr6 &= ~DMA_CONTROL_RSF;
		csr6 &= DMA_CONTROL_TC_RX_MASK;
		if (rxmode <= 32)
			csr6 |= DMA_CONTROL_RTC_32;
		else if (rxmode <= 64)
			csr6 |= DMA_CONTROL_RTC_64;
		else if (rxmode <= 96)
			csr6 |= DMA_CONTROL_RTC_96;
		else
			csr6 |= DMA_CONTROL_RTC_128;
	}

	writel(csr6, ioaddr + DMA_CONTROL);
	return;
}

/* Not yet implemented --- no RMON module */
static void gmac_dma_diagnostic_fr(void *data, struct stmmac_extra_stats *x,
				   unsigned long ioaddr)
{
	return;
}

static void gmac_dump_dma_regs(unsigned long ioaddr)
{
	int i;
	pr_info(" DMA registers\n");
	for (i = 0; i < 22; i++) {
		if ((i < 9) || (i > 17)) {
			int offset = i * 4;
			pr_err("\t Reg No. %d (offset 0x%x): 0x%08x\n", i,
			       (DMA_BUS_MODE + offset),
			       readl(ioaddr + DMA_BUS_MODE + offset));
		}
	}
	return;
}

static int gmac_get_tx_frame_status(void *data, struct stmmac_extra_stats *x,
				    struct dma_desc *p, unsigned long ioaddr)
{
	int ret = 0;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->des01.etx.error_summary)) {
		DBG(KERN_ERR "GMAC TX error... 0x%08x\n", p->des01.etx);
		if (unlikely(p->des01.etx.jabber_timeout)) {
			DBG(KERN_ERR "\tjabber_timeout error\n");
			x->tx_jabber++;
		}

		if (unlikely(p->des01.etx.frame_flushed)) {
			DBG(KERN_ERR "\tframe_flushed error\n");
			x->tx_frame_flushed++;
			gmac_flush_tx_fifo(ioaddr);
		}

		if (unlikely(p->des01.etx.loss_carrier)) {
			DBG(KERN_ERR "\tloss_carrier error\n");
			x->tx_losscarrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely(p->des01.etx.no_carrier)) {
			DBG(KERN_ERR "\tno_carrier error\n");
			x->tx_carrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely(p->des01.etx.late_collision)) {
			DBG(KERN_ERR "\tlate_collision error\n");
			stats->collisions += p->des01.etx.collision_count;
		}
		if (unlikely(p->des01.etx.excessive_collisions)) {
			DBG(KERN_ERR "\texcessive_collisions\n");
			stats->collisions += p->des01.etx.collision_count;
		}
		if (unlikely(p->des01.etx.excessive_deferral)) {
			DBG(KERN_INFO "\texcessive tx_deferral\n");
			x->tx_deferred++;
		}

		if (unlikely(p->des01.etx.underflow_error)) {
			DBG(KERN_ERR "\tunderflow error\n");
			gmac_flush_tx_fifo(ioaddr);
			x->tx_underflow++;
		}

		if (unlikely(p->des01.etx.ip_header_error)) {
			DBG(KERN_ERR "\tTX IP header csum error\n");
			x->tx_ip_header_error++;
		}

		if (unlikely(p->des01.etx.payload_error)) {
			DBG(KERN_ERR "\tAddr/Payload csum error\n");
			x->tx_payload_error++;
			gmac_flush_tx_fifo(ioaddr);
		}

		ret = -1;
	}

	if (unlikely(p->des01.etx.deferred)) {
		DBG(KERN_INFO "GMAC TX status: tx deferred\n");
		x->tx_deferred++;
	}
#ifdef STMMAC_VLAN_TAG_USED
	if (p->des01.etx.vlan_frame) {
		DBG(KERN_INFO "GMAC TX status: VLAN frame\n");
		x->tx_vlan++;
	}
#endif

	return ret;
}

static int gmac_get_tx_len(struct dma_desc *p)
{
	return p->des01.etx.buffer1_size;
}

static int gmac_coe_rdes0(int ipc_err, int type, int payload_err)
{
	int ret = good_frame;
	u32 status = (type << 2 | ipc_err << 1 | payload_err) & 0x7;

	/* bits 5 7 0 | Frame status
	 * ----------------------------------------------------------
	 *      0 0 0 | IEEE 802.3 Type frame (lenght < 1536 octects)
	 *      1 0 0 | IPv4/6 No CSUM errorS.
	 *      1 0 1 | IPv4/6 CSUM PAYLOAD error
	 *      1 1 0 | IPv4/6 CSUM IP HR error
	 *      1 1 1 | IPv4/6 IP PAYLOAD AND HEADER errorS
	 *      0 0 1 | IPv4/6 unsupported IP PAYLOAD
	 *      0 1 1 | COE bypassed.. no IPv4/6 frame
	 *      0 1 0 | Reserved.
	 */
	if (status == 0x0) {
		DBG(KERN_INFO "RX Des0 status: IEEE 802.3 Type frame.\n");
		ret = good_frame;
	} else if (status == 0x4) {
		DBG(KERN_INFO "RX Des0 status: IPv4/6 No CSUM errorS.\n");
		ret = good_frame;
	} else if (status == 0x5) {
		DBG(KERN_ERR "RX Des0 status: IPv4/6 Payload Error.\n");
		ret = csum_none;
	} else if (status == 0x6) {
		DBG(KERN_ERR "RX Des0 status: IPv4/6 Header Error.\n");
		ret = csum_none;
	} else if (status == 0x7) {
		DBG(KERN_ERR
		    "RX Des0 status: IPv4/6 Header and Payload Error.\n");
		ret = csum_none;
	} else if (status == 0x1) {
		DBG(KERN_ERR
		    "RX Des0 status: IPv4/6 unsupported IP PAYLOAD.\n");
		ret = discard_frame;
	} else if (status == 0x3) {
		DBG(KERN_ERR "RX Des0 status: No IPv4, IPv6 frame.\n");
		ret = discard_frame;
	}
	return ret;
}

static int gmac_get_rx_frame_status(void *data, struct stmmac_extra_stats *x,
				    struct dma_desc *p)
{
	int ret = good_frame;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->des01.erx.error_summary)) {
		DBG(KERN_ERR "GMAC RX Error Summary... 0x%08x\n", p->des01.erx);
		if (unlikely(p->des01.erx.descriptor_error)) {
			DBG(KERN_ERR "\tdescriptor error\n");
			x->rx_desc++;
			stats->rx_length_errors++;
		}
		if (unlikely(p->des01.erx.overflow_error)) {
			DBG(KERN_ERR "\toverflow error\n");
			x->rx_gmac_overflow++;
		}

		if (unlikely(p->des01.erx.ipc_csum_error))
			DBG(KERN_ERR "\tIPC Csum Error/Giant frame\n");

		if (unlikely(p->des01.erx.late_collision)) {
			DBG(KERN_ERR "\tlate_collision error\n");
			stats->collisions++;
			stats->collisions++;
		}
		if (unlikely(p->des01.erx.receive_watchdog)) {
			DBG(KERN_ERR "\treceive_watchdog error\n");
			x->rx_watchdog++;
		}
		if (unlikely(p->des01.erx.error_gmii)) {
			DBG(KERN_ERR "\tReceive Error\n");
			x->rx_mii++;
		}
		if (unlikely(p->des01.erx.crc_error)) {
			DBG(KERN_ERR "\tCRC error\n");
			x->rx_crc++;
			stats->rx_crc_errors++;
		}
		ret = discard_frame;
	}

	/* After a payload csum error, the ES bit is set.
	 * It doesn't match with the information reported into the databook.
	 * At any rate, we need to understand if the CSUM hw computation is ok
	 * and report this info to the upper layers. */
	ret = gmac_coe_rdes0(p->des01.erx.ipc_csum_error,
		p->des01.erx.frame_type, p->des01.erx.payload_csum_error);

	if (unlikely(p->des01.erx.dribbling)) {
		DBG(KERN_ERR "GMAC RX: dribbling error\n");
		ret = discard_frame;
	}
	if (unlikely(p->des01.erx.sa_filter_fail)) {
		DBG(KERN_ERR "GMAC RX : Source Address filter fail\n");
		x->sa_rx_filter_fail++;
		ret = discard_frame;
	}
	if (unlikely(p->des01.erx.da_filter_fail)) {
		DBG(KERN_ERR "GMAC RX : Destination Address filter fail\n");
		x->da_rx_filter_fail++;
		ret = discard_frame;
	}
	if (unlikely(p->des01.erx.length_error)) {
		DBG(KERN_ERR "GMAC RX: length_error error\n");
		x->rx_lenght++;
		ret = discard_frame;
	}
#ifdef STMMAC_VLAN_TAG_USED
	if (p->des01.erx.vlan_tag) {
		DBG(KERN_INFO "GMAC RX: VLAN frame tagged\n");
		x->rx_vlan++;
	}
#endif
	return ret;
}

static void gmac_irq_status(unsigned long ioaddr)
{
	u32 intr_status = readl(ioaddr + GMAC_INT_STATUS);

	/* Not used events (e.g. MMC interrupts) are not handled. */
	if ((intr_status & mmc_tx_irq))
		DBG(KERN_DEBUG "GMAC: MMC tx interrupt: 0x%08x\n",
		    readl(ioaddr + GMAC_MMC_TX_INTR));
	if (unlikely(intr_status & mmc_rx_irq))
		DBG(KERN_DEBUG "GMAC: MMC rx interrupt: 0x%08x\n",
		    readl(ioaddr + GMAC_MMC_RX_INTR));
	if (unlikely(intr_status & mmc_rx_csum_offload_irq))
		DBG(KERN_DEBUG "GMAC: MMC rx csum offload: 0x%08x\n",
		    readl(ioaddr + GMAC_MMC_RX_CSUM_OFFLOAD));
	if (unlikely(intr_status & pmt_irq)) {
		DBG(KERN_DEBUG "GMAC: received Magic frame\n");
		/* clear the PMT bits 5 and 6 by reading the PMT
		 * status register. */
		readl(ioaddr + GMAC_PMT);
	}

	return;
}

static void gmac_core_init(unsigned long ioaddr)
{
	u32 value = readl(ioaddr + GMAC_CONTROL);
	value |= GMAC_CORE_INIT;
	writel(value, ioaddr + GMAC_CONTROL);

	/* STBus Bridge Configuration */
	/*writel(0xc5608, ioaddr + 0x00007000);*/

	/* Freeze MMC counters */
	writel(0x8, ioaddr + GMAC_MMC_CTRL);
	/* Mask GMAC interrupts */
	writel(0x207, ioaddr + GMAC_INT_MASK);

#ifdef STMMAC_VLAN_TAG_USED
	/* Tag detection without filtering */
	writel(0x0, ioaddr + GMAC_VLAN_TAG);
#endif
	return;
}

static void gmac_set_umac_addr(unsigned long ioaddr, unsigned char *addr,
				unsigned int reg_n)
{
	stmmac_set_mac_addr(ioaddr, addr, GMAC_ADDR_HIGH(reg_n),
				GMAC_ADDR_LOW(reg_n));
}

static void gmac_get_umac_addr(unsigned long ioaddr, unsigned char *addr,
				unsigned int reg_n)
{
	stmmac_get_mac_addr(ioaddr, addr, GMAC_ADDR_HIGH(reg_n),
				GMAC_ADDR_LOW(reg_n));
}

static void gmac_set_filter(struct net_device *dev)
{
	unsigned long ioaddr = dev->base_addr;
	unsigned int value = 0;

	DBG(KERN_INFO "%s: # mcasts %d, # unicast %d\n",
	    __func__, dev->mc_count, dev->uc_count);

	if (dev->flags & IFF_PROMISC)
		value = GMAC_FRAME_FILTER_PR;
	else if ((dev->mc_count > HASH_TABLE_SIZE)
		   || (dev->flags & IFF_ALLMULTI)) {
		value = GMAC_FRAME_FILTER_PM;	/* pass all multi */
		writel(0xffffffff, ioaddr + GMAC_HASH_HIGH);
		writel(0xffffffff, ioaddr + GMAC_HASH_LOW);
	} else if (dev->mc_count > 0) {
		int i;
		u32 mc_filter[2];
		struct dev_mc_list *mclist;

		/* Hash filter for multicast */
		value = GMAC_FRAME_FILTER_HMC;

		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list;
		     mclist && i < dev->mc_count; i++, mclist = mclist->next) {
			/* The upper 6 bits of the calculated CRC are used to
			   index the contens of the hash table */
			int bit_nr =
			    bitrev32(~crc32_le(~0, mclist->dmi_addr, 6)) >> 26;
			/* The most significant bit determines the register to
			 * use (H/L) while the other 5 bits determine the bit
			 * within the register. */
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
		}
		writel(mc_filter[0], ioaddr + GMAC_HASH_LOW);
		writel(mc_filter[1], ioaddr + GMAC_HASH_HIGH);
	}

	/* Handle multiple unicast addresses (perfect filtering)*/
	if (dev->uc_count > GMAC_MAX_UNICAST_ADDRESSES)
		/* Switch to promiscuous mode is more than 16 addrs
		   are required */
		value |= GMAC_FRAME_FILTER_PR;
	else {
		int i;
		struct dev_addr_list *uc_ptr = dev->uc_list;

			for (i = 0; i < dev->uc_count; i++) {
				gmac_set_umac_addr(ioaddr, uc_ptr->da_addr,
						i + 1);

				DBG(KERN_INFO "\t%d "
				"- Unicast addr %02x:%02x:%02x:%02x:%02x:"
				"%02x\n", i + 1,
				uc_ptr->da_addr[0], uc_ptr->da_addr[1],
				uc_ptr->da_addr[2], uc_ptr->da_addr[3],
				uc_ptr->da_addr[4], uc_ptr->da_addr[5]);
				uc_ptr = uc_ptr->next;
		}
	}

#ifdef FRAME_FILTER_DEBUG
	/* Enable Receive all mode (to debug filtering_fail errors) */
	value |= GMAC_FRAME_FILTER_RA;
#endif
	writel(value, ioaddr + GMAC_FRAME_FILTER);

	DBG(KERN_INFO "\tFrame Filter reg: 0x%08x\n\tHash regs: "
	    "HI 0x%08x, LO 0x%08x\n", readl(ioaddr + GMAC_FRAME_FILTER),
	    readl(ioaddr + GMAC_HASH_HIGH), readl(ioaddr + GMAC_HASH_LOW));

	return;
}

static void gmac_flow_ctrl(unsigned long ioaddr, unsigned int duplex,
			   unsigned int fc, unsigned int pause_time)
{
	unsigned int flow = 0;

	DBG(KERN_DEBUG "GMAC Flow-Control:\n");
	if (fc & FLOW_RX) {
		DBG(KERN_DEBUG "\tReceive Flow-Control ON\n");
		flow |= GMAC_FLOW_CTRL_RFE;
	}
	if (fc & FLOW_TX) {
		DBG(KERN_DEBUG "\tTransmit Flow-Control ON\n");
		flow |= GMAC_FLOW_CTRL_TFE;
	}

	if (duplex) {
		DBG(KERN_DEBUG "\tduplex mode: pause time: %d\n", pause_time);
		flow |= (pause_time << GMAC_FLOW_CTRL_PT_SHIFT);
	}

	writel(flow, ioaddr + GMAC_FLOW_CTRL);
	return;
}

static void gmac_pmt(unsigned long ioaddr, unsigned long mode)
{
	unsigned int pmt = 0;

	if (mode == WAKE_MAGIC) {
		DBG(KERN_DEBUG "GMAC: WOL Magic frame\n");
		pmt |= power_down | magic_pkt_en;
	} else if (mode == WAKE_UCAST) {
		DBG(KERN_DEBUG "GMAC: WOL on global unicast\n");
		pmt |= global_unicast;
	}

	writel(pmt, ioaddr + GMAC_PMT);
	return;
}

static void gmac_init_rx_desc(struct dma_desc *p, unsigned int ring_size,
				int disable_rx_ic)
{
	int i;
	for (i = 0; i < ring_size; i++) {
		p->des01.erx.own = 1;
		p->des01.erx.buffer1_size = BUF_SIZE_8KiB - 1;
		/* To support jumbo frames */
		p->des01.erx.buffer2_size = BUF_SIZE_8KiB - 1;
		if (i == ring_size - 1)
			p->des01.erx.end_ring = 1;
		if (disable_rx_ic)
			p->des01.erx.disable_ic = 1;
		p++;
	}
	return;
}

static void gmac_init_tx_desc(struct dma_desc *p, unsigned int ring_size)
{
	int i;

	for (i = 0; i < ring_size; i++) {
		p->des01.etx.own = 0;
		if (i == ring_size - 1)
			p->des01.etx.end_ring = 1;
		p++;
	}

	return;
}

static int gmac_get_tx_owner(struct dma_desc *p)
{
	return p->des01.etx.own;
}

static int gmac_get_rx_owner(struct dma_desc *p)
{
	return p->des01.erx.own;
}

static void gmac_set_tx_owner(struct dma_desc *p)
{
	p->des01.etx.own = 1;
}

static void gmac_set_rx_owner(struct dma_desc *p)
{
	p->des01.erx.own = 1;
}

static int gmac_get_tx_ls(struct dma_desc *p)
{
	return p->des01.etx.last_segment;
}

static void gmac_release_tx_desc(struct dma_desc *p)
{
	int ter = p->des01.etx.end_ring;

	memset(p, 0, sizeof(struct dma_desc));
	p->des01.etx.end_ring = ter;

	return;
}

static void gmac_prepare_tx_desc(struct dma_desc *p, int is_fs, int len,
				 int csum_flag)
{
	p->des01.etx.first_segment = is_fs;
	if (unlikely(len > BUF_SIZE_4KiB)) {
		p->des01.etx.buffer1_size = BUF_SIZE_4KiB;
		p->des01.etx.buffer2_size = len - BUF_SIZE_4KiB;
	} else {
		p->des01.etx.buffer1_size = len;
	}
	if (likely(csum_flag))
		p->des01.etx.checksum_insertion = cic_full;
}

static void gmac_clear_tx_ic(struct dma_desc *p)
{
	p->des01.etx.interrupt = 0;
}

static void gmac_close_tx_desc(struct dma_desc *p)
{
	p->des01.etx.last_segment = 1;
	p->des01.etx.interrupt = 1;
}

static int gmac_get_rx_frame_len(struct dma_desc *p)
{
	return p->des01.erx.frame_length;
}

struct stmmac_ops gmac_driver = {
	.core_init = gmac_core_init,
	.dump_mac_regs = gmac_dump_regs,
	.dma_init = gmac_dma_init,
	.dump_dma_regs = gmac_dump_dma_regs,
	.dma_mode = gmac_dma_operation_mode,
	.dma_diagnostic_fr = gmac_dma_diagnostic_fr,
	.tx_status = gmac_get_tx_frame_status,
	.rx_status = gmac_get_rx_frame_status,
	.get_tx_len = gmac_get_tx_len,
	.set_filter = gmac_set_filter,
	.flow_ctrl = gmac_flow_ctrl,
	.pmt = gmac_pmt,
	.init_rx_desc = gmac_init_rx_desc,
	.init_tx_desc = gmac_init_tx_desc,
	.get_tx_owner = gmac_get_tx_owner,
	.get_rx_owner = gmac_get_rx_owner,
	.release_tx_desc = gmac_release_tx_desc,
	.prepare_tx_desc = gmac_prepare_tx_desc,
	.clear_tx_ic = gmac_clear_tx_ic,
	.close_tx_desc = gmac_close_tx_desc,
	.get_tx_ls = gmac_get_tx_ls,
	.set_tx_owner = gmac_set_tx_owner,
	.set_rx_owner = gmac_set_rx_owner,
	.get_rx_frame_len = gmac_get_rx_frame_len,
	.host_irq_status = gmac_irq_status,
	.set_umac_addr = gmac_set_umac_addr,
	.get_umac_addr = gmac_get_umac_addr,
};

struct mac_device_info *gmac_setup(unsigned long ioaddr)
{
	struct mac_device_info *mac;
	u32 uid = readl(ioaddr + GMAC_VERSION);

	pr_info("\tGMAC - user ID: 0x%x, Synopsys ID: 0x%x\n",
	       ((uid & 0x0000ff00) >> 8), (uid & 0x000000ff));

	mac = kzalloc(sizeof(const struct mac_device_info), GFP_KERNEL);

	mac->ops = &gmac_driver;
	mac->hw.pmt = PMT_SUPPORTED;
	mac->hw.link.port = GMAC_CONTROL_PS;
	mac->hw.link.duplex = GMAC_CONTROL_DM;
	mac->hw.link.speed = GMAC_CONTROL_FES;
	mac->hw.mii.addr = GMAC_MII_ADDR;
	mac->hw.mii.data = GMAC_MII_DATA;

	return mac;
}
