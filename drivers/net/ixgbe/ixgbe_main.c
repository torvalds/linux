/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2007 Intel Corporation.

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

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/ipv6.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>

#include "ixgbe.h"
#include "ixgbe_common.h"

char ixgbe_driver_name[] = "ixgbe";
static const char ixgbe_driver_string[] =
	"Intel(R) 10 Gigabit PCI Express Network Driver";

#define DRV_VERSION "1.1.18"
const char ixgbe_driver_version[] = DRV_VERSION;
static const char ixgbe_copyright[] =
	 "Copyright (c) 1999-2007 Intel Corporation.";

static const struct ixgbe_info *ixgbe_info_tbl[] = {
	[board_82598AF]			= &ixgbe_82598AF_info,
	[board_82598EB]			= &ixgbe_82598EB_info,
	[board_82598AT]			= &ixgbe_82598AT_info,
};

/* ixgbe_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static struct pci_device_id ixgbe_pci_tbl[] = {
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598AF_DUAL_PORT),
	 board_82598AF },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598AF_SINGLE_PORT),
	 board_82598AF },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598AT_DUAL_PORT),
	 board_82598AT },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598EB_CX4),
	 board_82598EB },

	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, ixgbe_pci_tbl);

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) 10 Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

#define DEFAULT_DEBUG_LEVEL_SHIFT 3


#ifdef DEBUG
/**
 * ixgbe_get_hw_dev_name - return device name string
 * used by hardware layer to print debugging information
 **/
char *ixgbe_get_hw_dev_name(struct ixgbe_hw *hw)
{
	struct ixgbe_adapter *adapter = hw->back;
	struct net_device *netdev = adapter->netdev;
	return netdev->name;
}
#endif

static void ixgbe_set_ivar(struct ixgbe_adapter *adapter, u16 int_alloc_entry,
			   u8 msix_vector)
{
	u32 ivar, index;

	msix_vector |= IXGBE_IVAR_ALLOC_VAL;
	index = (int_alloc_entry >> 2) & 0x1F;
	ivar = IXGBE_READ_REG(&adapter->hw, IXGBE_IVAR(index));
	ivar &= ~(0xFF << (8 * (int_alloc_entry & 0x3)));
	ivar |= (msix_vector << (8 * (int_alloc_entry & 0x3)));
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_IVAR(index), ivar);
}

static void ixgbe_unmap_and_free_tx_resource(struct ixgbe_adapter *adapter,
					     struct ixgbe_tx_buffer
					     *tx_buffer_info)
{
	if (tx_buffer_info->dma) {
		pci_unmap_page(adapter->pdev,
			       tx_buffer_info->dma,
			       tx_buffer_info->length, PCI_DMA_TODEVICE);
		tx_buffer_info->dma = 0;
	}
	if (tx_buffer_info->skb) {
		dev_kfree_skb_any(tx_buffer_info->skb);
		tx_buffer_info->skb = NULL;
	}
	/* tx_buffer_info must be completely set up in the transmit path */
}

static inline bool ixgbe_check_tx_hang(struct ixgbe_adapter *adapter,
				       struct ixgbe_ring *tx_ring,
				       unsigned int eop,
				       union ixgbe_adv_tx_desc *eop_desc)
{
	/* Detect a transmit hang in hardware, this serializes the
	 * check with the clearing of time_stamp and movement of i */
	adapter->detect_tx_hung = false;
	if (tx_ring->tx_buffer_info[eop].dma &&
	    time_after(jiffies, tx_ring->tx_buffer_info[eop].time_stamp + HZ) &&
	    !(IXGBE_READ_REG(&adapter->hw, IXGBE_TFCS) & IXGBE_TFCS_TXOFF)) {
		/* detected Tx unit hang */
		DPRINTK(DRV, ERR, "Detected Tx Unit Hang\n"
			"  TDH                  <%x>\n"
			"  TDT                  <%x>\n"
			"  next_to_use          <%x>\n"
			"  next_to_clean        <%x>\n"
			"tx_buffer_info[next_to_clean]\n"
			"  time_stamp           <%lx>\n"
			"  next_to_watch        <%x>\n"
			"  jiffies              <%lx>\n"
			"  next_to_watch.status <%x>\n",
			readl(adapter->hw.hw_addr + tx_ring->head),
			readl(adapter->hw.hw_addr + tx_ring->tail),
			tx_ring->next_to_use,
			tx_ring->next_to_clean,
			tx_ring->tx_buffer_info[eop].time_stamp,
			eop, jiffies, eop_desc->wb.status);
		return true;
	}

	return false;
}

/**
 * ixgbe_clean_tx_irq - Reclaim resources after transmit completes
 * @adapter: board private structure
 **/
static bool ixgbe_clean_tx_irq(struct ixgbe_adapter *adapter,
				    struct ixgbe_ring *tx_ring)
{
	struct net_device *netdev = adapter->netdev;
	union ixgbe_adv_tx_desc *tx_desc, *eop_desc;
	struct ixgbe_tx_buffer *tx_buffer_info;
	unsigned int i, eop;
	bool cleaned = false;
	int count = 0;

	i = tx_ring->next_to_clean;
	eop = tx_ring->tx_buffer_info[i].next_to_watch;
	eop_desc = IXGBE_TX_DESC_ADV(*tx_ring, eop);
	while (eop_desc->wb.status & cpu_to_le32(IXGBE_TXD_STAT_DD)) {
		for (cleaned = false; !cleaned;) {
			tx_desc = IXGBE_TX_DESC_ADV(*tx_ring, i);
			tx_buffer_info = &tx_ring->tx_buffer_info[i];
			cleaned = (i == eop);

			tx_ring->stats.bytes += tx_buffer_info->length;
			ixgbe_unmap_and_free_tx_resource(adapter,
							 tx_buffer_info);
			tx_desc->wb.status = 0;

			i++;
			if (i == tx_ring->count)
				i = 0;
		}

		tx_ring->stats.packets++;

		eop = tx_ring->tx_buffer_info[i].next_to_watch;
		eop_desc = IXGBE_TX_DESC_ADV(*tx_ring, eop);

		/* weight of a sort for tx, avoid endless transmit cleanup */
		if (count++ >= tx_ring->work_limit)
			break;
	}

	tx_ring->next_to_clean = i;

#define TX_WAKE_THRESHOLD 32
	spin_lock(&tx_ring->tx_lock);

	if (cleaned && netif_carrier_ok(netdev) &&
	    (IXGBE_DESC_UNUSED(tx_ring) >= TX_WAKE_THRESHOLD) &&
	    !test_bit(__IXGBE_DOWN, &adapter->state))
		netif_wake_queue(netdev);

	spin_unlock(&tx_ring->tx_lock);

	if (adapter->detect_tx_hung)
		if (ixgbe_check_tx_hang(adapter, tx_ring, eop, eop_desc))
			netif_stop_queue(netdev);

	if (count >= tx_ring->work_limit)
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS, tx_ring->eims_value);

	return cleaned;
}

/**
 * ixgbe_receive_skb - Send a completed packet up the stack
 * @adapter: board private structure
 * @skb: packet to send up
 * @is_vlan: packet has a VLAN tag
 * @tag: VLAN tag from descriptor
 **/
static void ixgbe_receive_skb(struct ixgbe_adapter *adapter,
			      struct sk_buff *skb, bool is_vlan,
			      u16 tag)
{
	if (!(adapter->flags & IXGBE_FLAG_IN_NETPOLL)) {
		if (adapter->vlgrp && is_vlan)
			vlan_hwaccel_receive_skb(skb, adapter->vlgrp, tag);
		else
			netif_receive_skb(skb);
	} else {

		if (adapter->vlgrp && is_vlan)
			vlan_hwaccel_rx(skb, adapter->vlgrp, tag);
		else
			netif_rx(skb);
	}
}

static inline void ixgbe_rx_checksum(struct ixgbe_adapter *adapter,
					 u32 status_err,
					 struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_NONE;

	/* Ignore Checksum bit is set */
	if ((status_err & IXGBE_RXD_STAT_IXSM) ||
		     !(adapter->flags & IXGBE_FLAG_RX_CSUM_ENABLED))
		return;
	/* TCP/UDP checksum error bit is set */
	if (status_err & (IXGBE_RXDADV_ERR_TCPE | IXGBE_RXDADV_ERR_IPE)) {
		/* let the stack verify checksum errors */
		adapter->hw_csum_rx_error++;
		return;
	}
	/* It must be a TCP or UDP packet with a valid checksum */
	if (status_err & (IXGBE_RXD_STAT_L4CS | IXGBE_RXD_STAT_UDPCS))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	adapter->hw_csum_rx_good++;
}

/**
 * ixgbe_alloc_rx_buffers - Replace used receive buffers; packet split
 * @adapter: address of board private structure
 **/
static void ixgbe_alloc_rx_buffers(struct ixgbe_adapter *adapter,
				       struct ixgbe_ring *rx_ring,
				       int cleaned_count)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union ixgbe_adv_rx_desc *rx_desc;
	struct ixgbe_rx_buffer *rx_buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int bufsz = adapter->rx_buf_len + NET_IP_ALIGN;

	i = rx_ring->next_to_use;
	rx_buffer_info = &rx_ring->rx_buffer_info[i];

	while (cleaned_count--) {
		rx_desc = IXGBE_RX_DESC_ADV(*rx_ring, i);

		if (!rx_buffer_info->page &&
				(adapter->flags & IXGBE_FLAG_RX_PS_ENABLED)) {
			rx_buffer_info->page = alloc_page(GFP_ATOMIC);
			if (!rx_buffer_info->page) {
				adapter->alloc_rx_page_failed++;
				goto no_buffers;
			}
			rx_buffer_info->page_dma =
			    pci_map_page(pdev, rx_buffer_info->page,
					 0, PAGE_SIZE, PCI_DMA_FROMDEVICE);
		}

		if (!rx_buffer_info->skb) {
			skb = netdev_alloc_skb(netdev, bufsz);

			if (!skb) {
				adapter->alloc_rx_buff_failed++;
				goto no_buffers;
			}

			/*
			 * Make buffer alignment 2 beyond a 16 byte boundary
			 * this will result in a 16 byte aligned IP header after
			 * the 14 byte MAC header is removed
			 */
			skb_reserve(skb, NET_IP_ALIGN);

			rx_buffer_info->skb = skb;
			rx_buffer_info->dma = pci_map_single(pdev, skb->data,
							  bufsz,
							  PCI_DMA_FROMDEVICE);
		}
		/* Refresh the desc even if buffer_addrs didn't change because
		 * each write-back erases this info. */
		if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
			rx_desc->read.pkt_addr =
			    cpu_to_le64(rx_buffer_info->page_dma);
			rx_desc->read.hdr_addr =
					cpu_to_le64(rx_buffer_info->dma);
		} else {
			rx_desc->read.pkt_addr =
					cpu_to_le64(rx_buffer_info->dma);
		}

		i++;
		if (i == rx_ring->count)
			i = 0;
		rx_buffer_info = &rx_ring->rx_buffer_info[i];
	}
no_buffers:
	if (rx_ring->next_to_use != i) {
		rx_ring->next_to_use = i;
		if (i-- == 0)
			i = (rx_ring->count - 1);

		/*
		 * Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		writel(i, adapter->hw.hw_addr + rx_ring->tail);
	}
}

static bool ixgbe_clean_rx_irq(struct ixgbe_adapter *adapter,
			       struct ixgbe_ring *rx_ring,
			       int *work_done, int work_to_do)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union ixgbe_adv_rx_desc *rx_desc, *next_rxd;
	struct ixgbe_rx_buffer *rx_buffer_info, *next_buffer;
	struct sk_buff *skb;
	unsigned int i;
	u32 upper_len, len, staterr;
	u16 hdr_info, vlan_tag;
	bool is_vlan, cleaned = false;
	int cleaned_count = 0;

	i = rx_ring->next_to_clean;
	upper_len = 0;
	rx_desc = IXGBE_RX_DESC_ADV(*rx_ring, i);
	staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	rx_buffer_info = &rx_ring->rx_buffer_info[i];
	is_vlan = (staterr & IXGBE_RXD_STAT_VP);
	vlan_tag = le16_to_cpu(rx_desc->wb.upper.vlan);

	while (staterr & IXGBE_RXD_STAT_DD) {
		if (*work_done >= work_to_do)
			break;
		(*work_done)++;

		if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
			hdr_info =
			    le16_to_cpu(rx_desc->wb.lower.lo_dword.hdr_info);
			len =
			    ((hdr_info & IXGBE_RXDADV_HDRBUFLEN_MASK) >>
			     IXGBE_RXDADV_HDRBUFLEN_SHIFT);
			if (hdr_info & IXGBE_RXDADV_SPH)
				adapter->rx_hdr_split++;
			if (len > IXGBE_RX_HDR_SIZE)
				len = IXGBE_RX_HDR_SIZE;
			upper_len = le16_to_cpu(rx_desc->wb.upper.length);
		} else
			len = le16_to_cpu(rx_desc->wb.upper.length);

		cleaned = true;
		skb = rx_buffer_info->skb;
		prefetch(skb->data - NET_IP_ALIGN);
		rx_buffer_info->skb = NULL;

		if (len && !skb_shinfo(skb)->nr_frags) {
			pci_unmap_single(pdev, rx_buffer_info->dma,
					 adapter->rx_buf_len + NET_IP_ALIGN,
					 PCI_DMA_FROMDEVICE);
			skb_put(skb, len);
		}

		if (upper_len) {
			pci_unmap_page(pdev, rx_buffer_info->page_dma,
				       PAGE_SIZE, PCI_DMA_FROMDEVICE);
			rx_buffer_info->page_dma = 0;
			skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
					   rx_buffer_info->page, 0, upper_len);
			rx_buffer_info->page = NULL;

			skb->len += upper_len;
			skb->data_len += upper_len;
			skb->truesize += upper_len;
		}

		i++;
		if (i == rx_ring->count)
			i = 0;
		next_buffer = &rx_ring->rx_buffer_info[i];

		next_rxd = IXGBE_RX_DESC_ADV(*rx_ring, i);
		prefetch(next_rxd);

		cleaned_count++;
		if (staterr & IXGBE_RXD_STAT_EOP) {
			rx_ring->stats.packets++;
			rx_ring->stats.bytes += skb->len;
		} else {
			rx_buffer_info->skb = next_buffer->skb;
			rx_buffer_info->dma = next_buffer->dma;
			next_buffer->skb = skb;
			adapter->non_eop_descs++;
			goto next_desc;
		}

		if (staterr & IXGBE_RXDADV_ERR_FRAME_ERR_MASK) {
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		ixgbe_rx_checksum(adapter, staterr, skb);
		skb->protocol = eth_type_trans(skb, netdev);
		ixgbe_receive_skb(adapter, skb, is_vlan, vlan_tag);
		netdev->last_rx = jiffies;

next_desc:
		rx_desc->wb.upper.status_error = 0;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= IXGBE_RX_BUFFER_WRITE) {
			ixgbe_alloc_rx_buffers(adapter, rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		rx_buffer_info = next_buffer;

		staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
		is_vlan = (staterr & IXGBE_RXD_STAT_VP);
		vlan_tag = le16_to_cpu(rx_desc->wb.upper.vlan);
	}

	rx_ring->next_to_clean = i;
	cleaned_count = IXGBE_DESC_UNUSED(rx_ring);

	if (cleaned_count)
		ixgbe_alloc_rx_buffers(adapter, rx_ring, cleaned_count);

	return cleaned;
}

#define IXGBE_MAX_INTR 10
/**
 * ixgbe_configure_msix - Configure MSI-X hardware
 * @adapter: board private structure
 *
 * ixgbe_configure_msix sets up the hardware to properly generate MSI-X
 * interrupts.
 **/
static void ixgbe_configure_msix(struct ixgbe_adapter *adapter)
{
	int i, vector = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		ixgbe_set_ivar(adapter, IXGBE_IVAR_TX_QUEUE(i),
			       IXGBE_MSIX_VECTOR(vector));
		writel(EITR_INTS_PER_SEC_TO_REG(adapter->tx_eitr),
		       adapter->hw.hw_addr + adapter->tx_ring[i].itr_register);
		vector++;
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		ixgbe_set_ivar(adapter, IXGBE_IVAR_RX_QUEUE(i),
			       IXGBE_MSIX_VECTOR(vector));
		writel(EITR_INTS_PER_SEC_TO_REG(adapter->rx_eitr),
		       adapter->hw.hw_addr + adapter->rx_ring[i].itr_register);
		vector++;
	}

	vector = adapter->num_tx_queues + adapter->num_rx_queues;
	ixgbe_set_ivar(adapter, IXGBE_IVAR_OTHER_CAUSES_INDEX,
		       IXGBE_MSIX_VECTOR(vector));
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EITR(vector), 1950);
}

static irqreturn_t ixgbe_msix_lsc(int irq, void *data)
{
	struct net_device *netdev = data;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 eicr = IXGBE_READ_REG(hw, IXGBE_EICR);

	if (eicr & IXGBE_EICR_LSC) {
		adapter->lsc_int++;
		if (!test_bit(__IXGBE_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies);
	}
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS, IXGBE_EIMS_OTHER);

	return IRQ_HANDLED;
}

static irqreturn_t ixgbe_msix_clean_tx(int irq, void *data)
{
	struct ixgbe_ring *txr = data;
	struct ixgbe_adapter *adapter = txr->adapter;

	ixgbe_clean_tx_irq(adapter, txr);

	return IRQ_HANDLED;
}

static irqreturn_t ixgbe_msix_clean_rx(int irq, void *data)
{
	struct ixgbe_ring *rxr = data;
	struct ixgbe_adapter *adapter = rxr->adapter;

	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, rxr->eims_value);
	netif_rx_schedule(adapter->netdev, &adapter->napi);
	return IRQ_HANDLED;
}

static int ixgbe_clean_rxonly(struct napi_struct *napi, int budget)
{
	struct ixgbe_adapter *adapter = container_of(napi,
					struct ixgbe_adapter, napi);
	struct net_device *netdev = adapter->netdev;
	int work_done = 0;
	struct ixgbe_ring *rxr = adapter->rx_ring;

	/* Keep link state information with original netdev */
	if (!netif_carrier_ok(netdev))
		goto quit_polling;

	ixgbe_clean_rx_irq(adapter, rxr, &work_done, budget);

	/* If no Tx and not enough Rx work done, exit the polling mode */
	if ((work_done < budget) || !netif_running(netdev)) {
quit_polling:
		netif_rx_complete(netdev, napi);
		if (!test_bit(__IXGBE_DOWN, &adapter->state))
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS,
					rxr->eims_value);
	}

	return work_done;
}

/**
 * ixgbe_setup_msix - Initialize MSI-X interrupts
 *
 * ixgbe_setup_msix allocates MSI-X vectors and requests
 * interrutps from the kernel.
 **/
static int ixgbe_setup_msix(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i, int_vector = 0, err = 0;
	int max_msix_count;

	/* +1 for the LSC interrupt */
	max_msix_count = adapter->num_rx_queues + adapter->num_tx_queues + 1;
	adapter->msix_entries = kcalloc(max_msix_count,
					sizeof(struct msix_entry), GFP_KERNEL);
	if (!adapter->msix_entries)
		return -ENOMEM;

	for (i = 0; i < max_msix_count; i++)
		adapter->msix_entries[i].entry = i;

	err = pci_enable_msix(adapter->pdev, adapter->msix_entries,
			      max_msix_count);
	if (err)
		goto out;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		sprintf(adapter->tx_ring[i].name, "%s-tx%d", netdev->name, i);
		err = request_irq(adapter->msix_entries[int_vector].vector,
				  &ixgbe_msix_clean_tx,
				  0,
				  adapter->tx_ring[i].name,
				  &(adapter->tx_ring[i]));
		if (err) {
			DPRINTK(PROBE, ERR,
				"request_irq failed for MSIX interrupt "
				"Error: %d\n", err);
			goto release_irqs;
		}
		adapter->tx_ring[i].eims_value =
		    (1 << IXGBE_MSIX_VECTOR(int_vector));
		adapter->tx_ring[i].itr_register = IXGBE_EITR(int_vector);
		int_vector++;
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		if (strlen(netdev->name) < (IFNAMSIZ - 5))
			sprintf(adapter->rx_ring[i].name,
				"%s-rx%d", netdev->name, i);
		else
			memcpy(adapter->rx_ring[i].name,
			       netdev->name, IFNAMSIZ);
		err = request_irq(adapter->msix_entries[int_vector].vector,
				  &ixgbe_msix_clean_rx, 0,
				  adapter->rx_ring[i].name,
				  &(adapter->rx_ring[i]));
		if (err) {
			DPRINTK(PROBE, ERR,
				"request_irq failed for MSIX interrupt "
				"Error: %d\n", err);
			goto release_irqs;
		}

		adapter->rx_ring[i].eims_value =
		    (1 << IXGBE_MSIX_VECTOR(int_vector));
		adapter->rx_ring[i].itr_register = IXGBE_EITR(int_vector);
		int_vector++;
	}

	sprintf(adapter->lsc_name, "%s-lsc", netdev->name);
	err = request_irq(adapter->msix_entries[int_vector].vector,
			  &ixgbe_msix_lsc, 0, adapter->lsc_name, netdev);
	if (err) {
		DPRINTK(PROBE, ERR,
			"request_irq for msix_lsc failed: %d\n", err);
		goto release_irqs;
	}

	/* FIXME: implement netif_napi_remove() instead */
	adapter->napi.poll = ixgbe_clean_rxonly;
	adapter->flags |= IXGBE_FLAG_MSIX_ENABLED;
	return 0;

release_irqs:
	int_vector--;
	for (; int_vector >= adapter->num_tx_queues; int_vector--)
		free_irq(adapter->msix_entries[int_vector].vector,
			 &(adapter->rx_ring[int_vector -
					    adapter->num_tx_queues]));

	for (; int_vector >= 0; int_vector--)
		free_irq(adapter->msix_entries[int_vector].vector,
			 &(adapter->tx_ring[int_vector]));
out:
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
	adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
	return err;
}

/**
 * ixgbe_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 * @pt_regs: CPU registers structure
 **/
static irqreturn_t ixgbe_intr(int irq, void *data)
{
	struct net_device *netdev = data;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 eicr;

	eicr = IXGBE_READ_REG(hw, IXGBE_EICR);

	if (!eicr)
		return IRQ_NONE;	/* Not our interrupt */

	if (eicr & IXGBE_EICR_LSC) {
		adapter->lsc_int++;
		if (!test_bit(__IXGBE_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies);
	}
	if (netif_rx_schedule_prep(netdev, &adapter->napi)) {
		/* Disable interrupts and register for poll. The flush of the
		 * posted write is intentionally left out. */
		atomic_inc(&adapter->irq_sem);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, ~0);
		__netif_rx_schedule(netdev, &adapter->napi);
	}

	return IRQ_HANDLED;
}

/**
 * ixgbe_request_irq - initialize interrupts
 * @adapter: board private structure
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static int ixgbe_request_irq(struct ixgbe_adapter *adapter, u32 *num_rx_queues)
{
	struct net_device *netdev = adapter->netdev;
	int flags, err;
	irqreturn_t(*handler) (int, void *) = &ixgbe_intr;

	flags = IRQF_SHARED;

	err = ixgbe_setup_msix(adapter);
	if (!err)
		goto request_done;

	/*
	 * if we can't do MSI-X, fall through and try MSI
	 * No need to reallocate memory since we're decreasing the number of
	 * queues. We just won't use the other ones, also it is freed correctly
	 * on ixgbe_remove.
	 */
	*num_rx_queues = 1;

	/* do MSI */
	err = pci_enable_msi(adapter->pdev);
	if (!err) {
		adapter->flags |= IXGBE_FLAG_MSI_ENABLED;
		flags &= ~IRQF_SHARED;
		handler = &ixgbe_intr;
	}

	err = request_irq(adapter->pdev->irq, handler, flags,
			  netdev->name, netdev);
	if (err)
		DPRINTK(PROBE, ERR, "request_irq failed, Error %d\n", err);

request_done:
	return err;
}

static void ixgbe_free_irq(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		int i;

		for (i = 0; i < adapter->num_tx_queues; i++)
			free_irq(adapter->msix_entries[i].vector,
				 &(adapter->tx_ring[i]));
		for (i = 0; i < adapter->num_rx_queues; i++)
			free_irq(adapter->msix_entries[i +
						adapter->num_tx_queues].vector,
				&(adapter->rx_ring[i]));
		i = adapter->num_rx_queues + adapter->num_tx_queues;
		free_irq(adapter->msix_entries[i].vector, netdev);
		pci_disable_msix(adapter->pdev);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
		adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
		return;
	}

	free_irq(adapter->pdev->irq, netdev);
	if (adapter->flags & IXGBE_FLAG_MSI_ENABLED) {
		pci_disable_msi(adapter->pdev);
		adapter->flags &= ~IXGBE_FLAG_MSI_ENABLED;
	}
}

/**
 * ixgbe_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/
static inline void ixgbe_irq_disable(struct ixgbe_adapter *adapter)
{
	atomic_inc(&adapter->irq_sem);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, ~0);
	IXGBE_WRITE_FLUSH(&adapter->hw);
	synchronize_irq(adapter->pdev->irq);
}

/**
 * ixgbe_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/
static inline void ixgbe_irq_enable(struct ixgbe_adapter *adapter)
{
	if (atomic_dec_and_test(&adapter->irq_sem)) {
		if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIAC,
					(IXGBE_EIMS_ENABLE_MASK &
					 ~(IXGBE_EIMS_OTHER | IXGBE_EIMS_LSC)));
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS,
				IXGBE_EIMS_ENABLE_MASK);
		IXGBE_WRITE_FLUSH(&adapter->hw);
	}
}

/**
 * ixgbe_configure_msi_and_legacy - Initialize PIN (INTA...) and MSI interrupts
 *
 **/
static void ixgbe_configure_msi_and_legacy(struct ixgbe_adapter *adapter)
{
	int i;
	struct ixgbe_hw *hw = &adapter->hw;

	if (adapter->rx_eitr)
		IXGBE_WRITE_REG(hw, IXGBE_EITR(0),
				EITR_INTS_PER_SEC_TO_REG(adapter->rx_eitr));

	/* for re-triggering the interrupt in non-NAPI mode */
	adapter->rx_ring[0].eims_value = (1 << IXGBE_MSIX_VECTOR(0));
	adapter->tx_ring[0].eims_value = (1 << IXGBE_MSIX_VECTOR(0));

	ixgbe_set_ivar(adapter, IXGBE_IVAR_RX_QUEUE(0), 0);
	for (i = 0; i < adapter->num_tx_queues; i++)
		ixgbe_set_ivar(adapter, IXGBE_IVAR_TX_QUEUE(i), i);
}

/**
 * ixgbe_configure_tx - Configure 8254x Transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
static void ixgbe_configure_tx(struct ixgbe_adapter *adapter)
{
	u64 tdba;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 i, tdlen;

	/* Setup the HW Tx Head and Tail descriptor pointers */
	for (i = 0; i < adapter->num_tx_queues; i++) {
		tdba = adapter->tx_ring[i].dma;
		tdlen = adapter->tx_ring[i].count *
		    sizeof(union ixgbe_adv_tx_desc);
		IXGBE_WRITE_REG(hw, IXGBE_TDBAL(i), (tdba & DMA_32BIT_MASK));
		IXGBE_WRITE_REG(hw, IXGBE_TDBAH(i), (tdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_TDLEN(i), tdlen);
		IXGBE_WRITE_REG(hw, IXGBE_TDH(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_TDT(i), 0);
		adapter->tx_ring[i].head = IXGBE_TDH(i);
		adapter->tx_ring[i].tail = IXGBE_TDT(i);
	}

	IXGBE_WRITE_REG(hw, IXGBE_TIPG, IXGBE_TIPG_FIBER_DEFAULT);
}

#define PAGE_USE_COUNT(S) (((S) >> PAGE_SHIFT) + \
			(((S) & (PAGE_SIZE - 1)) ? 1 : 0))

#define IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT			2
/**
 * ixgbe_configure_rx - Configure 8254x Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void ixgbe_configure_rx(struct ixgbe_adapter *adapter)
{
	u64 rdba;
	struct ixgbe_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	int max_frame = netdev->mtu + ETH_HLEN + ETH_FCS_LEN;
	u32 rdlen, rxctrl, rxcsum;
	u32 random[10];
	u32 reta, mrqc;
	int i;
	u32 fctrl, hlreg0;
	u32 srrctl;
	u32 pages;

	/* Decide whether to use packet split mode or not */
	if (netdev->mtu > ETH_DATA_LEN)
		adapter->flags |= IXGBE_FLAG_RX_PS_ENABLED;
	else
		adapter->flags &= ~IXGBE_FLAG_RX_PS_ENABLED;

	/* Set the RX buffer length according to the mode */
	if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
		adapter->rx_buf_len = IXGBE_RX_HDR_SIZE;
	} else {
		if (netdev->mtu <= ETH_DATA_LEN)
			adapter->rx_buf_len = MAXIMUM_ETHERNET_VLAN_SIZE;
		else
			adapter->rx_buf_len = ALIGN(max_frame, 1024);
	}

	fctrl = IXGBE_READ_REG(&adapter->hw, IXGBE_FCTRL);
	fctrl |= IXGBE_FCTRL_BAM;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_FCTRL, fctrl);

	hlreg0 = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	if (adapter->netdev->mtu <= ETH_DATA_LEN)
		hlreg0 &= ~IXGBE_HLREG0_JUMBOEN;
	else
		hlreg0 |= IXGBE_HLREG0_JUMBOEN;
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg0);

	pages = PAGE_USE_COUNT(adapter->netdev->mtu);

	srrctl = IXGBE_READ_REG(&adapter->hw, IXGBE_SRRCTL(0));
	srrctl &= ~IXGBE_SRRCTL_BSIZEHDR_MASK;
	srrctl &= ~IXGBE_SRRCTL_BSIZEPKT_MASK;

	if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
		srrctl |= PAGE_SIZE >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;
		srrctl |= IXGBE_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS;
		srrctl |= ((IXGBE_RX_HDR_SIZE <<
			    IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT) &
			   IXGBE_SRRCTL_BSIZEHDR_MASK);
	} else {
		srrctl |= IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;

		if (adapter->rx_buf_len == MAXIMUM_ETHERNET_VLAN_SIZE)
			srrctl |=
			     IXGBE_RXBUFFER_2048 >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;
		else
			srrctl |=
			     adapter->rx_buf_len >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;
	}
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_SRRCTL(0), srrctl);

	rdlen = adapter->rx_ring[0].count * sizeof(union ixgbe_adv_rx_desc);
	/* disable receives while setting up the descriptors */
	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl & ~IXGBE_RXCTRL_RXEN);

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		rdba = adapter->rx_ring[i].dma;
		IXGBE_WRITE_REG(hw, IXGBE_RDBAL(i), (rdba & DMA_32BIT_MASK));
		IXGBE_WRITE_REG(hw, IXGBE_RDBAH(i), (rdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_RDLEN(i), rdlen);
		IXGBE_WRITE_REG(hw, IXGBE_RDH(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_RDT(i), 0);
		adapter->rx_ring[i].head = IXGBE_RDH(i);
		adapter->rx_ring[i].tail = IXGBE_RDT(i);
	}

	if (adapter->num_rx_queues > 1) {
		/* Random 40bytes used as random key in RSS hash function */
		get_random_bytes(&random[0], 40);

		switch (adapter->num_rx_queues) {
		case 8:
		case 4:
			/* Bits [3:0] in each byte refers the Rx queue no */
			reta = 0x00010203;
			break;
		case 2:
			reta = 0x00010001;
			break;
		default:
			reta = 0x00000000;
			break;
		}

		/* Fill out redirection table */
		for (i = 0; i < 32; i++) {
			IXGBE_WRITE_REG_ARRAY(hw, IXGBE_RETA(0), i, reta);
			if (adapter->num_rx_queues > 4) {
				i++;
				IXGBE_WRITE_REG_ARRAY(hw, IXGBE_RETA(0), i,
						      0x04050607);
			}
		}

		/* Fill out hash function seeds */
		for (i = 0; i < 10; i++)
			IXGBE_WRITE_REG_ARRAY(hw, IXGBE_RSSRK(0), i, random[i]);

		mrqc = IXGBE_MRQC_RSSEN
		    /* Perform hash on these packet types */
		    | IXGBE_MRQC_RSS_FIELD_IPV4
		    | IXGBE_MRQC_RSS_FIELD_IPV4_TCP
		    | IXGBE_MRQC_RSS_FIELD_IPV4_UDP
		    | IXGBE_MRQC_RSS_FIELD_IPV6_EX_TCP
		    | IXGBE_MRQC_RSS_FIELD_IPV6_EX
		    | IXGBE_MRQC_RSS_FIELD_IPV6
		    | IXGBE_MRQC_RSS_FIELD_IPV6_TCP
		    | IXGBE_MRQC_RSS_FIELD_IPV6_UDP
		    | IXGBE_MRQC_RSS_FIELD_IPV6_EX_UDP;
		IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);

		/* Multiqueue and packet checksumming are mutually exclusive. */
		rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
		rxcsum |= IXGBE_RXCSUM_PCSD;
		IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);
	} else {
		/* Enable Receive Checksum Offload for TCP and UDP */
		rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
		if (adapter->flags & IXGBE_FLAG_RX_CSUM_ENABLED) {
			/* Enable IPv4 payload checksum for UDP fragments
			 * Must be used in conjunction with packet-split. */
			rxcsum |= IXGBE_RXCSUM_IPPCSE;
		} else {
			/* don't need to clear IPPCSE as it defaults to 0 */
		}
		IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);
	}
	/* Enable Receives */
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl);
	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
}

static void ixgbe_vlan_rx_register(struct net_device *netdev,
				   struct vlan_group *grp)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u32 ctrl;

	ixgbe_irq_disable(adapter);
	adapter->vlgrp = grp;

	if (grp) {
		/* enable VLAN tag insert/strip */
		ctrl = IXGBE_READ_REG(&adapter->hw, IXGBE_VLNCTRL);
		ctrl |= IXGBE_VLNCTRL_VME | IXGBE_VLNCTRL_VFE;
		ctrl &= ~IXGBE_VLNCTRL_CFIEN;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_VLNCTRL, ctrl);
	}

	ixgbe_irq_enable(adapter);
}

static void ixgbe_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	/* add VID to filter table */
	ixgbe_set_vfta(&adapter->hw, vid, 0, true);
}

static void ixgbe_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	ixgbe_irq_disable(adapter);
	vlan_group_set_device(adapter->vlgrp, vid, NULL);
	ixgbe_irq_enable(adapter);

	/* remove VID from filter table */
	ixgbe_set_vfta(&adapter->hw, vid, 0, false);
}

static void ixgbe_restore_vlan(struct ixgbe_adapter *adapter)
{
	ixgbe_vlan_rx_register(adapter->netdev, adapter->vlgrp);

	if (adapter->vlgrp) {
		u16 vid;
		for (vid = 0; vid < VLAN_GROUP_ARRAY_LEN; vid++) {
			if (!vlan_group_get_device(adapter->vlgrp, vid))
				continue;
			ixgbe_vlan_rx_add_vid(adapter->netdev, vid);
		}
	}
}

/**
 * ixgbe_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 **/
static void ixgbe_set_multi(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	struct dev_mc_list *mc_ptr;
	u8 *mta_list;
	u32 fctrl;
	int i;

	/* Check for Promiscuous and All Multicast modes */

	fctrl = IXGBE_READ_REG(hw, IXGBE_FCTRL);

	if (netdev->flags & IFF_PROMISC) {
		fctrl |= (IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
	} else if (netdev->flags & IFF_ALLMULTI) {
		fctrl |= IXGBE_FCTRL_MPE;
		fctrl &= ~IXGBE_FCTRL_UPE;
	} else {
		fctrl &= ~(IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
	}

	IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl);

	if (netdev->mc_count) {
		mta_list = kcalloc(netdev->mc_count, ETH_ALEN, GFP_ATOMIC);
		if (!mta_list)
			return;

		/* Shared function expects packed array of only addresses. */
		mc_ptr = netdev->mc_list;

		for (i = 0; i < netdev->mc_count; i++) {
			if (!mc_ptr)
				break;
			memcpy(mta_list + (i * ETH_ALEN), mc_ptr->dmi_addr,
			       ETH_ALEN);
			mc_ptr = mc_ptr->next;
		}

		ixgbe_update_mc_addr_list(hw, mta_list, i, 0);
		kfree(mta_list);
	} else {
		ixgbe_update_mc_addr_list(hw, NULL, 0, 0);
	}

}

static void ixgbe_configure(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;

	ixgbe_set_multi(netdev);

	ixgbe_restore_vlan(adapter);

	ixgbe_configure_tx(adapter);
	ixgbe_configure_rx(adapter);
	for (i = 0; i < adapter->num_rx_queues; i++)
		ixgbe_alloc_rx_buffers(adapter, &adapter->rx_ring[i],
					   (adapter->rx_ring[i].count - 1));
}

static int ixgbe_up_complete(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;
	u32 gpie = 0;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 txdctl, rxdctl, mhadd;
	int max_frame = netdev->mtu + ETH_HLEN + ETH_FCS_LEN;

	if (adapter->flags & (IXGBE_FLAG_MSIX_ENABLED |
			      IXGBE_FLAG_MSI_ENABLED)) {
		if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
			gpie = (IXGBE_GPIE_MSIX_MODE | IXGBE_GPIE_EIAME |
				IXGBE_GPIE_PBA_SUPPORT | IXGBE_GPIE_OCD);
		} else {
			/* MSI only */
			gpie = (IXGBE_GPIE_EIAME |
				IXGBE_GPIE_PBA_SUPPORT);
		}
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_GPIE, gpie);
		gpie = IXGBE_READ_REG(&adapter->hw, IXGBE_GPIE);
	}

	mhadd = IXGBE_READ_REG(hw, IXGBE_MHADD);

	if (max_frame != (mhadd >> IXGBE_MHADD_MFS_SHIFT)) {
		mhadd &= ~IXGBE_MHADD_MFS_MASK;
		mhadd |= max_frame << IXGBE_MHADD_MFS_SHIFT;

		IXGBE_WRITE_REG(hw, IXGBE_MHADD, mhadd);
	}

	for (i = 0; i < adapter->num_tx_queues; i++) {
		txdctl = IXGBE_READ_REG(&adapter->hw, IXGBE_TXDCTL(i));
		txdctl |= IXGBE_TXDCTL_ENABLE;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_TXDCTL(i), txdctl);
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		rxdctl = IXGBE_READ_REG(&adapter->hw, IXGBE_RXDCTL(i));
		rxdctl |= IXGBE_RXDCTL_ENABLE;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_RXDCTL(i), rxdctl);
	}
	/* enable all receives */
	rxdctl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	rxdctl |= (IXGBE_RXCTRL_DMBYPS | IXGBE_RXCTRL_RXEN);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxdctl);

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		ixgbe_configure_msix(adapter);
	else
		ixgbe_configure_msi_and_legacy(adapter);

	clear_bit(__IXGBE_DOWN, &adapter->state);
	napi_enable(&adapter->napi);
	ixgbe_irq_enable(adapter);

	/* bring the link up in the watchdog, this could race with our first
	 * link up interrupt but shouldn't be a problem */
	mod_timer(&adapter->watchdog_timer, jiffies);
	return 0;
}

int ixgbe_up(struct ixgbe_adapter *adapter)
{
	/* hardware has been reset, we need to reload some things */
	ixgbe_configure(adapter);

	return ixgbe_up_complete(adapter);
}

void ixgbe_reset(struct ixgbe_adapter *adapter)
{
	if (ixgbe_init_hw(&adapter->hw))
		DPRINTK(PROBE, ERR, "Hardware Error\n");

	/* reprogram the RAR[0] in case user changed it. */
	ixgbe_set_rar(&adapter->hw, 0, adapter->hw.mac.addr, 0, IXGBE_RAH_AV);

}

#ifdef CONFIG_PM
static int ixgbe_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u32 err, num_rx_queues = adapter->num_rx_queues;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "ixgbe: Cannot enable PCI device from " \
				"suspend\n");
		return err;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	if (netif_running(netdev)) {
		err = ixgbe_request_irq(adapter, &num_rx_queues);
		if (err)
			return err;
	}

	ixgbe_reset(adapter);

	if (netif_running(netdev))
		ixgbe_up(adapter);

	netif_device_attach(netdev);

	return 0;
}
#endif

/**
 * ixgbe_clean_rx_ring - Free Rx Buffers per Queue
 * @adapter: board private structure
 * @rx_ring: ring to free buffers from
 **/
static void ixgbe_clean_rx_ring(struct ixgbe_adapter *adapter,
				struct ixgbe_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i;

	/* Free all the Rx ring sk_buffs */

	for (i = 0; i < rx_ring->count; i++) {
		struct ixgbe_rx_buffer *rx_buffer_info;

		rx_buffer_info = &rx_ring->rx_buffer_info[i];
		if (rx_buffer_info->dma) {
			pci_unmap_single(pdev, rx_buffer_info->dma,
					 adapter->rx_buf_len,
					 PCI_DMA_FROMDEVICE);
			rx_buffer_info->dma = 0;
		}
		if (rx_buffer_info->skb) {
			dev_kfree_skb(rx_buffer_info->skb);
			rx_buffer_info->skb = NULL;
		}
		if (!rx_buffer_info->page)
			continue;
		pci_unmap_page(pdev, rx_buffer_info->page_dma, PAGE_SIZE,
			       PCI_DMA_FROMDEVICE);
		rx_buffer_info->page_dma = 0;

		put_page(rx_buffer_info->page);
		rx_buffer_info->page = NULL;
	}

	size = sizeof(struct ixgbe_rx_buffer) * rx_ring->count;
	memset(rx_ring->rx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	writel(0, adapter->hw.hw_addr + rx_ring->head);
	writel(0, adapter->hw.hw_addr + rx_ring->tail);
}

/**
 * ixgbe_clean_tx_ring - Free Tx Buffers
 * @adapter: board private structure
 * @tx_ring: ring to be cleaned
 **/
static void ixgbe_clean_tx_ring(struct ixgbe_adapter *adapter,
				struct ixgbe_ring *tx_ring)
{
	struct ixgbe_tx_buffer *tx_buffer_info;
	unsigned long size;
	unsigned int i;

	/* Free all the Tx ring sk_buffs */

	for (i = 0; i < tx_ring->count; i++) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		ixgbe_unmap_and_free_tx_resource(adapter, tx_buffer_info);
	}

	size = sizeof(struct ixgbe_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	writel(0, adapter->hw.hw_addr + tx_ring->head);
	writel(0, adapter->hw.hw_addr + tx_ring->tail);
}

/**
 * ixgbe_clean_all_tx_rings - Free Tx Buffers for all queues
 * @adapter: board private structure
 **/
static void ixgbe_clean_all_tx_rings(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		ixgbe_clean_tx_ring(adapter, &adapter->tx_ring[i]);
}

/**
 * ixgbe_clean_all_rx_rings - Free Rx Buffers for all queues
 * @adapter: board private structure
 **/
static void ixgbe_clean_all_rx_rings(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		ixgbe_clean_rx_ring(adapter, &adapter->rx_ring[i]);
}

void ixgbe_down(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u32 rxctrl;

	/* signal that we are down to the interrupt handler */
	set_bit(__IXGBE_DOWN, &adapter->state);

	/* disable receives */
	rxctrl = IXGBE_READ_REG(&adapter->hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RXCTRL,
			rxctrl & ~IXGBE_RXCTRL_RXEN);

	netif_tx_disable(netdev);

	/* disable transmits in the hardware */

	/* flush both disables */
	IXGBE_WRITE_FLUSH(&adapter->hw);
	msleep(10);

	ixgbe_irq_disable(adapter);

	napi_disable(&adapter->napi);
	del_timer_sync(&adapter->watchdog_timer);

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	ixgbe_reset(adapter);
	ixgbe_clean_all_tx_rings(adapter);
	ixgbe_clean_all_rx_rings(adapter);

}

static int ixgbe_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
#ifdef CONFIG_PM
	int retval = 0;
#endif

	netif_device_detach(netdev);

	if (netif_running(netdev)) {
		ixgbe_down(adapter);
		ixgbe_free_irq(adapter);
	}

#ifdef CONFIG_PM
	retval = pci_save_state(pdev);
	if (retval)
		return retval;
#endif

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	pci_disable_device(pdev);

	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static void ixgbe_shutdown(struct pci_dev *pdev)
{
	ixgbe_suspend(pdev, PMSG_SUSPEND);
}

/**
 * ixgbe_clean - NAPI Rx polling callback
 * @adapter: board private structure
 **/
static int ixgbe_clean(struct napi_struct *napi, int budget)
{
	struct ixgbe_adapter *adapter = container_of(napi,
					struct ixgbe_adapter, napi);
	struct net_device *netdev = adapter->netdev;
	int tx_cleaned = 0, work_done = 0;

	/* Keep link state information with original netdev */
	if (!netif_carrier_ok(adapter->netdev))
		goto quit_polling;

	/* In non-MSIX case, there is no multi-Tx/Rx queue */
	tx_cleaned = ixgbe_clean_tx_irq(adapter, adapter->tx_ring);
	ixgbe_clean_rx_irq(adapter, &adapter->rx_ring[0], &work_done,
			   budget);

	/* If no Tx and not enough Rx work done, exit the polling mode */
	if ((!tx_cleaned && (work_done < budget)) ||
	    !netif_running(adapter->netdev)) {
quit_polling:
		netif_rx_complete(netdev, napi);
		ixgbe_irq_enable(adapter);
	}

	return work_done;
}

/**
 * ixgbe_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/
static void ixgbe_tx_timeout(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	schedule_work(&adapter->reset_task);
}

static void ixgbe_reset_task(struct work_struct *work)
{
	struct ixgbe_adapter *adapter;
	adapter = container_of(work, struct ixgbe_adapter, reset_task);

	adapter->tx_timeout_count++;

	ixgbe_down(adapter);
	ixgbe_up(adapter);
}

/**
 * ixgbe_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 * We allocate one ring per queue at run-time since we don't know the
 * number of queues at compile-time.  The polling_netdev array is
 * intended for Multiqueue, but should work fine with a single queue.
 **/
static int __devinit ixgbe_alloc_queues(struct ixgbe_adapter *adapter)
{
	int i;

	adapter->tx_ring = kcalloc(adapter->num_tx_queues,
				   sizeof(struct ixgbe_ring), GFP_KERNEL);
	if (!adapter->tx_ring)
		return -ENOMEM;

	for (i = 0; i < adapter->num_tx_queues; i++)
		adapter->tx_ring[i].count = IXGBE_DEFAULT_TXD;

	adapter->rx_ring = kcalloc(adapter->num_rx_queues,
				   sizeof(struct ixgbe_ring), GFP_KERNEL);
	if (!adapter->rx_ring) {
		kfree(adapter->tx_ring);
		return -ENOMEM;
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		adapter->rx_ring[i].adapter = adapter;
		adapter->rx_ring[i].itr_register = IXGBE_EITR(i);
		adapter->rx_ring[i].count = IXGBE_DEFAULT_RXD;
	}

	return 0;
}

/**
 * ixgbe_sw_init - Initialize general software structures (struct ixgbe_adapter)
 * @adapter: board private structure to initialize
 *
 * ixgbe_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int __devinit ixgbe_sw_init(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;

	/* default flow control settings */
	hw->fc.original_type = ixgbe_fc_full;
	hw->fc.type = ixgbe_fc_full;

	hw->mac.link_mode_select = IXGBE_AUTOC_LMS_10G_LINK_NO_AN;
	if (hw->mac.ops.reset(hw)) {
		dev_err(&pdev->dev, "HW Init failed\n");
		return -EIO;
	}
	if (hw->phy.ops.setup_speed(hw, IXGBE_LINK_SPEED_10GB_FULL, true,
				   false)) {
		dev_err(&pdev->dev, "Link Speed setup failed\n");
		return -EIO;
	}

	/* initialize eeprom parameters */
	if (ixgbe_init_eeprom(hw)) {
		dev_err(&pdev->dev, "EEPROM initialization failed\n");
		return -EIO;
	}

	/* Set the default values */
	adapter->num_rx_queues = IXGBE_DEFAULT_RXQ;
	adapter->num_tx_queues = 1;
	adapter->flags |= IXGBE_FLAG_RX_CSUM_ENABLED;

	if (ixgbe_alloc_queues(adapter)) {
		dev_err(&pdev->dev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	atomic_set(&adapter->irq_sem, 1);
	set_bit(__IXGBE_DOWN, &adapter->state);

	return 0;
}

/**
 * ixgbe_setup_tx_resources - allocate Tx resources (Descriptors)
 * @adapter: board private structure
 * @txdr:    tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/
int ixgbe_setup_tx_resources(struct ixgbe_adapter *adapter,
			     struct ixgbe_ring *txdr)
{
	struct pci_dev *pdev = adapter->pdev;
	int size;

	size = sizeof(struct ixgbe_tx_buffer) * txdr->count;
	txdr->tx_buffer_info = vmalloc(size);
	if (!txdr->tx_buffer_info) {
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the transmit descriptor ring\n");
		return -ENOMEM;
	}
	memset(txdr->tx_buffer_info, 0, size);

	/* round up to nearest 4K */
	txdr->size = txdr->count * sizeof(union ixgbe_adv_tx_desc);
	txdr->size = ALIGN(txdr->size, 4096);

	txdr->desc = pci_alloc_consistent(pdev, txdr->size, &txdr->dma);
	if (!txdr->desc) {
		vfree(txdr->tx_buffer_info);
		DPRINTK(PROBE, ERR,
			"Memory allocation failed for the tx desc ring\n");
		return -ENOMEM;
	}

	txdr->adapter = adapter;
	txdr->next_to_use = 0;
	txdr->next_to_clean = 0;
	txdr->work_limit = txdr->count;
	spin_lock_init(&txdr->tx_lock);

	return 0;
}

/**
 * ixgbe_setup_rx_resources - allocate Rx resources (Descriptors)
 * @adapter: board private structure
 * @rxdr:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/
int ixgbe_setup_rx_resources(struct ixgbe_adapter *adapter,
			     struct ixgbe_ring *rxdr)
{
	struct pci_dev *pdev = adapter->pdev;
	int size, desc_len;

	size = sizeof(struct ixgbe_rx_buffer) * rxdr->count;
	rxdr->rx_buffer_info = vmalloc(size);
	if (!rxdr->rx_buffer_info) {
		DPRINTK(PROBE, ERR,
			"vmalloc allocation failed for the rx desc ring\n");
		return -ENOMEM;
	}
	memset(rxdr->rx_buffer_info, 0, size);

	desc_len = sizeof(union ixgbe_adv_rx_desc);

	/* Round up to nearest 4K */
	rxdr->size = rxdr->count * desc_len;
	rxdr->size = ALIGN(rxdr->size, 4096);

	rxdr->desc = pci_alloc_consistent(pdev, rxdr->size, &rxdr->dma);

	if (!rxdr->desc) {
		DPRINTK(PROBE, ERR,
			"Memory allocation failed for the rx desc ring\n");
		vfree(rxdr->rx_buffer_info);
		return -ENOMEM;
	}

	rxdr->next_to_clean = 0;
	rxdr->next_to_use = 0;
	rxdr->adapter = adapter;

	return 0;
}

/**
 * ixgbe_free_tx_resources - Free Tx Resources per Queue
 * @adapter: board private structure
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
static void ixgbe_free_tx_resources(struct ixgbe_adapter *adapter,
				    struct ixgbe_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	ixgbe_clean_tx_ring(adapter, tx_ring);

	vfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;

	pci_free_consistent(pdev, tx_ring->size, tx_ring->desc, tx_ring->dma);

	tx_ring->desc = NULL;
}

/**
 * ixgbe_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/
static void ixgbe_free_all_tx_resources(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		ixgbe_free_tx_resources(adapter, &adapter->tx_ring[i]);
}

/**
 * ixgbe_free_rx_resources - Free Rx Resources
 * @adapter: board private structure
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
static void ixgbe_free_rx_resources(struct ixgbe_adapter *adapter,
				    struct ixgbe_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	ixgbe_clean_rx_ring(adapter, rx_ring);

	vfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;

	pci_free_consistent(pdev, rx_ring->size, rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * ixgbe_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/
static void ixgbe_free_all_rx_resources(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		ixgbe_free_rx_resources(adapter, &adapter->rx_ring[i]);
}

/**
 * ixgbe_setup_all_tx_resources - wrapper to allocate Tx resources
 *				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int ixgbe_setup_all_tx_resources(struct ixgbe_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		err = ixgbe_setup_tx_resources(adapter, &adapter->tx_ring[i]);
		if (err) {
			DPRINTK(PROBE, ERR,
				"Allocation for Tx Queue %u failed\n", i);
			break;
		}
	}

	return err;
}

/**
 * ixgbe_setup_all_rx_resources - wrapper to allocate Rx resources
 *				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/

static int ixgbe_setup_all_rx_resources(struct ixgbe_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		err = ixgbe_setup_rx_resources(adapter, &adapter->rx_ring[i]);
		if (err) {
			DPRINTK(PROBE, ERR,
				"Allocation for Rx Queue %u failed\n", i);
			break;
		}
	}

	return err;
}

/**
 * ixgbe_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int ixgbe_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN;

	if ((max_frame < (ETH_ZLEN + ETH_FCS_LEN)) ||
	    (max_frame > IXGBE_MAX_JUMBO_FRAME_SIZE))
		return -EINVAL;

	netdev->mtu = new_mtu;

	if (netif_running(netdev)) {
		ixgbe_down(adapter);
		ixgbe_up(adapter);
	}

	return 0;
}

/**
 * ixgbe_open - Called when a network interface is made active
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
static int ixgbe_open(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int err;
	u32 ctrl_ext;
	u32 num_rx_queues = adapter->num_rx_queues;

	/* Let firmware know the driver has taken over */
	ctrl_ext = IXGBE_READ_REG(&adapter->hw, IXGBE_CTRL_EXT);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_CTRL_EXT,
			ctrl_ext | IXGBE_CTRL_EXT_DRV_LOAD);

try_intr_reinit:
	/* allocate transmit descriptors */
	err = ixgbe_setup_all_tx_resources(adapter);
	if (err)
		goto err_setup_tx;

	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED)) {
		num_rx_queues = 1;
		adapter->num_rx_queues = num_rx_queues;
	}

	/* allocate receive descriptors */
	err = ixgbe_setup_all_rx_resources(adapter);
	if (err)
		goto err_setup_rx;

	ixgbe_configure(adapter);

	err = ixgbe_request_irq(adapter, &num_rx_queues);
	if (err)
		goto err_req_irq;

	/* ixgbe_request might have reduced num_rx_queues */
	if (num_rx_queues < adapter->num_rx_queues) {
		/* We didn't get MSI-X, so we need to release everything,
		 * set our Rx queue count to num_rx_queues, and redo the
		 * whole init process.
		 */
		ixgbe_free_irq(adapter);
		if (adapter->flags & IXGBE_FLAG_MSI_ENABLED) {
			pci_disable_msi(adapter->pdev);
			adapter->flags &= ~IXGBE_FLAG_MSI_ENABLED;
		}
		ixgbe_free_all_rx_resources(adapter);
		ixgbe_free_all_tx_resources(adapter);
		adapter->num_rx_queues = num_rx_queues;

		/* Reset the hardware, and start over. */
		ixgbe_reset(adapter);

		goto try_intr_reinit;
	}

	err = ixgbe_up_complete(adapter);
	if (err)
		goto err_up;

	return 0;

err_up:
	ixgbe_free_irq(adapter);
err_req_irq:
	ixgbe_free_all_rx_resources(adapter);
err_setup_rx:
	ixgbe_free_all_tx_resources(adapter);
err_setup_tx:
	ixgbe_reset(adapter);

	return err;
}

/**
 * ixgbe_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
static int ixgbe_close(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u32 ctrl_ext;

	ixgbe_down(adapter);
	ixgbe_free_irq(adapter);

	ixgbe_free_all_tx_resources(adapter);
	ixgbe_free_all_rx_resources(adapter);

	ctrl_ext = IXGBE_READ_REG(&adapter->hw, IXGBE_CTRL_EXT);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_CTRL_EXT,
			ctrl_ext & ~IXGBE_CTRL_EXT_DRV_LOAD);

	return 0;
}

/**
 * ixgbe_update_stats - Update the board statistics counters.
 * @adapter: board private structure
 **/
void ixgbe_update_stats(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u64 good_rx, missed_rx, bprc;

	adapter->stats.crcerrs += IXGBE_READ_REG(hw, IXGBE_CRCERRS);
	good_rx = IXGBE_READ_REG(hw, IXGBE_GPRC);
	missed_rx = IXGBE_READ_REG(hw, IXGBE_MPC(0));
	missed_rx += IXGBE_READ_REG(hw, IXGBE_MPC(1));
	missed_rx += IXGBE_READ_REG(hw, IXGBE_MPC(2));
	missed_rx += IXGBE_READ_REG(hw, IXGBE_MPC(3));
	missed_rx += IXGBE_READ_REG(hw, IXGBE_MPC(4));
	missed_rx += IXGBE_READ_REG(hw, IXGBE_MPC(5));
	missed_rx += IXGBE_READ_REG(hw, IXGBE_MPC(6));
	missed_rx += IXGBE_READ_REG(hw, IXGBE_MPC(7));
	adapter->stats.gprc += (good_rx - missed_rx);

	adapter->stats.mpc[0] += missed_rx;
	adapter->stats.gorc += IXGBE_READ_REG(hw, IXGBE_GORCH);
	bprc = IXGBE_READ_REG(hw, IXGBE_BPRC);
	adapter->stats.bprc += bprc;
	adapter->stats.mprc += IXGBE_READ_REG(hw, IXGBE_MPRC);
	adapter->stats.mprc -= bprc;
	adapter->stats.roc += IXGBE_READ_REG(hw, IXGBE_ROC);
	adapter->stats.prc64 += IXGBE_READ_REG(hw, IXGBE_PRC64);
	adapter->stats.prc127 += IXGBE_READ_REG(hw, IXGBE_PRC127);
	adapter->stats.prc255 += IXGBE_READ_REG(hw, IXGBE_PRC255);
	adapter->stats.prc511 += IXGBE_READ_REG(hw, IXGBE_PRC511);
	adapter->stats.prc1023 += IXGBE_READ_REG(hw, IXGBE_PRC1023);
	adapter->stats.prc1522 += IXGBE_READ_REG(hw, IXGBE_PRC1522);

	adapter->stats.rlec += IXGBE_READ_REG(hw, IXGBE_RLEC);
	adapter->stats.lxonrxc += IXGBE_READ_REG(hw, IXGBE_LXONRXC);
	adapter->stats.lxontxc += IXGBE_READ_REG(hw, IXGBE_LXONTXC);
	adapter->stats.lxoffrxc += IXGBE_READ_REG(hw, IXGBE_LXOFFRXC);
	adapter->stats.lxofftxc += IXGBE_READ_REG(hw, IXGBE_LXOFFTXC);
	adapter->stats.ruc += IXGBE_READ_REG(hw, IXGBE_RUC);
	adapter->stats.gptc += IXGBE_READ_REG(hw, IXGBE_GPTC);
	adapter->stats.gotc += IXGBE_READ_REG(hw, IXGBE_GOTCH);
	adapter->stats.rnbc[0] += IXGBE_READ_REG(hw, IXGBE_RNBC(0));
	adapter->stats.ruc += IXGBE_READ_REG(hw, IXGBE_RUC);
	adapter->stats.rfc += IXGBE_READ_REG(hw, IXGBE_RFC);
	adapter->stats.rjc += IXGBE_READ_REG(hw, IXGBE_RJC);
	adapter->stats.tor += IXGBE_READ_REG(hw, IXGBE_TORH);
	adapter->stats.tpr += IXGBE_READ_REG(hw, IXGBE_TPR);
	adapter->stats.ptc64 += IXGBE_READ_REG(hw, IXGBE_PTC64);
	adapter->stats.ptc127 += IXGBE_READ_REG(hw, IXGBE_PTC127);
	adapter->stats.ptc255 += IXGBE_READ_REG(hw, IXGBE_PTC255);
	adapter->stats.ptc511 += IXGBE_READ_REG(hw, IXGBE_PTC511);
	adapter->stats.ptc1023 += IXGBE_READ_REG(hw, IXGBE_PTC1023);
	adapter->stats.ptc1522 += IXGBE_READ_REG(hw, IXGBE_PTC1522);
	adapter->stats.mptc += IXGBE_READ_REG(hw, IXGBE_MPTC);
	adapter->stats.bptc += IXGBE_READ_REG(hw, IXGBE_BPTC);

	/* Fill out the OS statistics structure */
	adapter->net_stats.rx_packets = adapter->stats.gprc;
	adapter->net_stats.tx_packets = adapter->stats.gptc;
	adapter->net_stats.rx_bytes = adapter->stats.gorc;
	adapter->net_stats.tx_bytes = adapter->stats.gotc;
	adapter->net_stats.multicast = adapter->stats.mprc;

	/* Rx Errors */
	adapter->net_stats.rx_errors = adapter->stats.crcerrs +
						adapter->stats.rlec;
	adapter->net_stats.rx_dropped = 0;
	adapter->net_stats.rx_length_errors = adapter->stats.rlec;
	adapter->net_stats.rx_crc_errors = adapter->stats.crcerrs;
	adapter->net_stats.rx_missed_errors = adapter->stats.mpc[0];

}

/**
 * ixgbe_watchdog - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void ixgbe_watchdog(unsigned long data)
{
	struct ixgbe_adapter *adapter = (struct ixgbe_adapter *)data;
	struct net_device *netdev = adapter->netdev;
	bool link_up;
	u32 link_speed = 0;

	adapter->hw.phy.ops.check(&adapter->hw, &(link_speed), &link_up);

	if (link_up) {
		if (!netif_carrier_ok(netdev)) {
			u32 frctl = IXGBE_READ_REG(&adapter->hw, IXGBE_FCTRL);
			u32 rmcs = IXGBE_READ_REG(&adapter->hw, IXGBE_RMCS);
#define FLOW_RX (frctl & IXGBE_FCTRL_RFCE)
#define FLOW_TX (rmcs & IXGBE_RMCS_TFCE_802_3X)
			DPRINTK(LINK, INFO, "NIC Link is Up %s, "
				"Flow Control: %s\n",
				(link_speed == IXGBE_LINK_SPEED_10GB_FULL ?
				 "10 Gbps" :
				 (link_speed == IXGBE_LINK_SPEED_1GB_FULL ?
				  "1 Gpbs" : "unknown speed")),
				((FLOW_RX && FLOW_TX) ? "RX/TX" :
				 (FLOW_RX ? "RX" :
				 (FLOW_TX ? "TX" : "None"))));

			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
		} else {
			/* Force detection of hung controller */
			adapter->detect_tx_hung = true;
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			DPRINTK(LINK, INFO, "NIC Link is Down\n");
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
	}

	ixgbe_update_stats(adapter);

	/* Reset the timer */
	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		mod_timer(&adapter->watchdog_timer,
			  round_jiffies(jiffies + 2 * HZ));
}

#define IXGBE_MAX_TXD_PWR	14
#define IXGBE_MAX_DATA_PER_TXD	(1 << IXGBE_MAX_TXD_PWR)

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S) (((S) >> IXGBE_MAX_TXD_PWR) + \
			 (((S) & (IXGBE_MAX_DATA_PER_TXD - 1)) ? 1 : 0))
#define DESC_NEEDED (TXD_USE_COUNT(IXGBE_MAX_DATA_PER_TXD) /* skb->data */ + \
	MAX_SKB_FRAGS * TXD_USE_COUNT(PAGE_SIZE) + 1)	/* for context */

static int ixgbe_tso(struct ixgbe_adapter *adapter,
			 struct ixgbe_ring *tx_ring, struct sk_buff *skb,
			 u32 tx_flags, u8 *hdr_len)
{
	struct ixgbe_adv_tx_context_desc *context_desc;
	unsigned int i;
	int err;
	struct ixgbe_tx_buffer *tx_buffer_info;
	u32 vlan_macip_lens = 0, type_tucmd_mlhl = 0;
	u32 mss_l4len_idx = 0, l4len;
	*hdr_len = 0;

	if (skb_is_gso(skb)) {
		if (skb_header_cloned(skb)) {
			err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (err)
				return err;
		}
		l4len = tcp_hdrlen(skb);
		*hdr_len += l4len;

		if (skb->protocol == ntohs(ETH_P_IP)) {
			struct iphdr *iph = ip_hdr(skb);
			iph->tot_len = 0;
			iph->check = 0;
			tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
								 iph->daddr, 0,
								 IPPROTO_TCP,
								 0);
			adapter->hw_tso_ctxt++;
		} else if (skb_shinfo(skb)->gso_type == SKB_GSO_TCPV6) {
			ipv6_hdr(skb)->payload_len = 0;
			tcp_hdr(skb)->check =
			    ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
					     &ipv6_hdr(skb)->daddr,
					     0, IPPROTO_TCP, 0);
			adapter->hw_tso6_ctxt++;
		}

		i = tx_ring->next_to_use;

		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		context_desc = IXGBE_TX_CTXTDESC_ADV(*tx_ring, i);

		/* VLAN MACLEN IPLEN */
		if (tx_flags & IXGBE_TX_FLAGS_VLAN)
			vlan_macip_lens |=
			    (tx_flags & IXGBE_TX_FLAGS_VLAN_MASK);
		vlan_macip_lens |= ((skb_network_offset(skb)) <<
				    IXGBE_ADVTXD_MACLEN_SHIFT);
		*hdr_len += skb_network_offset(skb);
		vlan_macip_lens |=
		    (skb_transport_header(skb) - skb_network_header(skb));
		*hdr_len +=
		    (skb_transport_header(skb) - skb_network_header(skb));
		context_desc->vlan_macip_lens = cpu_to_le32(vlan_macip_lens);
		context_desc->seqnum_seed = 0;

		/* ADV DTYP TUCMD MKRLOC/ISCSIHEDLEN */
		type_tucmd_mlhl |= (IXGBE_TXD_CMD_DEXT |
				    IXGBE_ADVTXD_DTYP_CTXT);

		if (skb->protocol == ntohs(ETH_P_IP))
			type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
		context_desc->type_tucmd_mlhl = cpu_to_le32(type_tucmd_mlhl);

		/* MSS L4LEN IDX */
		mss_l4len_idx |=
		    (skb_shinfo(skb)->gso_size << IXGBE_ADVTXD_MSS_SHIFT);
		mss_l4len_idx |= (l4len << IXGBE_ADVTXD_L4LEN_SHIFT);
		context_desc->mss_l4len_idx = cpu_to_le32(mss_l4len_idx);

		tx_buffer_info->time_stamp = jiffies;
		tx_buffer_info->next_to_watch = i;

		i++;
		if (i == tx_ring->count)
			i = 0;
		tx_ring->next_to_use = i;

		return true;
	}
	return false;
}

static bool ixgbe_tx_csum(struct ixgbe_adapter *adapter,
				   struct ixgbe_ring *tx_ring,
				   struct sk_buff *skb, u32 tx_flags)
{
	struct ixgbe_adv_tx_context_desc *context_desc;
	unsigned int i;
	struct ixgbe_tx_buffer *tx_buffer_info;
	u32 vlan_macip_lens = 0, type_tucmd_mlhl = 0;

	if (skb->ip_summed == CHECKSUM_PARTIAL ||
	    (tx_flags & IXGBE_TX_FLAGS_VLAN)) {
		i = tx_ring->next_to_use;
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		context_desc = IXGBE_TX_CTXTDESC_ADV(*tx_ring, i);

		if (tx_flags & IXGBE_TX_FLAGS_VLAN)
			vlan_macip_lens |=
			    (tx_flags & IXGBE_TX_FLAGS_VLAN_MASK);
		vlan_macip_lens |= (skb_network_offset(skb) <<
				    IXGBE_ADVTXD_MACLEN_SHIFT);
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			vlan_macip_lens |= (skb_transport_header(skb) -
					    skb_network_header(skb));

		context_desc->vlan_macip_lens = cpu_to_le32(vlan_macip_lens);
		context_desc->seqnum_seed = 0;

		type_tucmd_mlhl |= (IXGBE_TXD_CMD_DEXT |
				    IXGBE_ADVTXD_DTYP_CTXT);

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			if (skb->protocol == ntohs(ETH_P_IP))
				type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;

			if (skb->sk->sk_protocol == IPPROTO_TCP)
				type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
		}

		context_desc->type_tucmd_mlhl = cpu_to_le32(type_tucmd_mlhl);
		context_desc->mss_l4len_idx = 0;

		tx_buffer_info->time_stamp = jiffies;
		tx_buffer_info->next_to_watch = i;
		adapter->hw_csum_tx_good++;
		i++;
		if (i == tx_ring->count)
			i = 0;
		tx_ring->next_to_use = i;

		return true;
	}
	return false;
}

static int ixgbe_tx_map(struct ixgbe_adapter *adapter,
			struct ixgbe_ring *tx_ring,
			struct sk_buff *skb, unsigned int first)
{
	struct ixgbe_tx_buffer *tx_buffer_info;
	unsigned int len = skb->len;
	unsigned int offset = 0, size, count = 0, i;
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	unsigned int f;

	len -= skb->data_len;

	i = tx_ring->next_to_use;

	while (len) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		size = min(len, (uint)IXGBE_MAX_DATA_PER_TXD);

		tx_buffer_info->length = size;
		tx_buffer_info->dma = pci_map_single(adapter->pdev,
						  skb->data + offset,
						  size, PCI_DMA_TODEVICE);
		tx_buffer_info->time_stamp = jiffies;
		tx_buffer_info->next_to_watch = i;

		len -= size;
		offset += size;
		count++;
		i++;
		if (i == tx_ring->count)
			i = 0;
	}

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];
		len = frag->size;
		offset = frag->page_offset;

		while (len) {
			tx_buffer_info = &tx_ring->tx_buffer_info[i];
			size = min(len, (uint)IXGBE_MAX_DATA_PER_TXD);

			tx_buffer_info->length = size;
			tx_buffer_info->dma = pci_map_page(adapter->pdev,
							frag->page,
							offset,
							size, PCI_DMA_TODEVICE);
			tx_buffer_info->time_stamp = jiffies;
			tx_buffer_info->next_to_watch = i;

			len -= size;
			offset += size;
			count++;
			i++;
			if (i == tx_ring->count)
				i = 0;
		}
	}
	if (i == 0)
		i = tx_ring->count - 1;
	else
		i = i - 1;
	tx_ring->tx_buffer_info[i].skb = skb;
	tx_ring->tx_buffer_info[first].next_to_watch = i;

	return count;
}

static void ixgbe_tx_queue(struct ixgbe_adapter *adapter,
			       struct ixgbe_ring *tx_ring,
			       int tx_flags, int count, u32 paylen, u8 hdr_len)
{
	union ixgbe_adv_tx_desc *tx_desc = NULL;
	struct ixgbe_tx_buffer *tx_buffer_info;
	u32 olinfo_status = 0, cmd_type_len = 0;
	unsigned int i;
	u32 txd_cmd = IXGBE_TXD_CMD_EOP | IXGBE_TXD_CMD_RS | IXGBE_TXD_CMD_IFCS;

	cmd_type_len |= IXGBE_ADVTXD_DTYP_DATA;

	cmd_type_len |= IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT;

	if (tx_flags & IXGBE_TX_FLAGS_VLAN)
		cmd_type_len |= IXGBE_ADVTXD_DCMD_VLE;

	if (tx_flags & IXGBE_TX_FLAGS_TSO) {
		cmd_type_len |= IXGBE_ADVTXD_DCMD_TSE;

		olinfo_status |= IXGBE_TXD_POPTS_TXSM <<
						IXGBE_ADVTXD_POPTS_SHIFT;

		if (tx_flags & IXGBE_TX_FLAGS_IPV4)
			olinfo_status |= IXGBE_TXD_POPTS_IXSM <<
						IXGBE_ADVTXD_POPTS_SHIFT;

	} else if (tx_flags & IXGBE_TX_FLAGS_CSUM)
		olinfo_status |= IXGBE_TXD_POPTS_TXSM <<
						IXGBE_ADVTXD_POPTS_SHIFT;

	olinfo_status |= ((paylen - hdr_len) << IXGBE_ADVTXD_PAYLEN_SHIFT);

	i = tx_ring->next_to_use;
	while (count--) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		tx_desc = IXGBE_TX_DESC_ADV(*tx_ring, i);
		tx_desc->read.buffer_addr = cpu_to_le64(tx_buffer_info->dma);
		tx_desc->read.cmd_type_len =
			cpu_to_le32(cmd_type_len | tx_buffer_info->length);
		tx_desc->read.olinfo_status = cpu_to_le32(olinfo_status);

		i++;
		if (i == tx_ring->count)
			i = 0;
	}

	tx_desc->read.cmd_type_len |= cpu_to_le32(txd_cmd);

	/*
	 * Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	tx_ring->next_to_use = i;
	writel(i, adapter->hw.hw_addr + tx_ring->tail);
}

static int ixgbe_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_ring *tx_ring;
	unsigned int len = skb->len;
	unsigned int first;
	unsigned int tx_flags = 0;
	unsigned long flags = 0;
	u8 hdr_len;
	int tso;
	unsigned int mss = 0;
	int count = 0;
	unsigned int f;
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	len -= skb->data_len;

	tx_ring = adapter->tx_ring;

	if (skb->len <= 0) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	mss = skb_shinfo(skb)->gso_size;

	if (mss)
		count++;
	else if (skb->ip_summed == CHECKSUM_PARTIAL)
		count++;

	count += TXD_USE_COUNT(len);
	for (f = 0; f < nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size);

	spin_lock_irqsave(&tx_ring->tx_lock, flags);
	if (IXGBE_DESC_UNUSED(tx_ring) < (count + 2)) {
		adapter->tx_busy++;
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}
	spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
	if (adapter->vlgrp && vlan_tx_tag_present(skb)) {
		tx_flags |= IXGBE_TX_FLAGS_VLAN;
		tx_flags |= (vlan_tx_tag_get(skb) << IXGBE_TX_FLAGS_VLAN_SHIFT);
	}

	if (skb->protocol == ntohs(ETH_P_IP))
		tx_flags |= IXGBE_TX_FLAGS_IPV4;
	first = tx_ring->next_to_use;
	tso = ixgbe_tso(adapter, tx_ring, skb, tx_flags, &hdr_len);
	if (tso < 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (tso)
		tx_flags |= IXGBE_TX_FLAGS_TSO;
	else if (ixgbe_tx_csum(adapter, tx_ring, skb, tx_flags) &&
		 (skb->ip_summed == CHECKSUM_PARTIAL))
		tx_flags |= IXGBE_TX_FLAGS_CSUM;

	ixgbe_tx_queue(adapter, tx_ring, tx_flags,
			   ixgbe_tx_map(adapter, tx_ring, skb, first),
			   skb->len, hdr_len);

	netdev->trans_start = jiffies;

	spin_lock_irqsave(&tx_ring->tx_lock, flags);
	/* Make sure there is space in the ring for the next send. */
	if (IXGBE_DESC_UNUSED(tx_ring) < DESC_NEEDED)
		netif_stop_queue(netdev);
	spin_unlock_irqrestore(&tx_ring->tx_lock, flags);

	return NETDEV_TX_OK;
}

/**
 * ixgbe_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/
static struct net_device_stats *ixgbe_get_stats(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	/* only return the current stats */
	return &adapter->net_stats;
}

/**
 * ixgbe_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int ixgbe_set_mac(struct net_device *netdev, void *p)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac.addr, addr->sa_data, netdev->addr_len);

	ixgbe_set_rar(&adapter->hw, 0, adapter->hw.mac.addr, 0, IXGBE_RAH_AV);

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void ixgbe_netpoll(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	disable_irq(adapter->pdev->irq);
	adapter->flags |= IXGBE_FLAG_IN_NETPOLL;
	ixgbe_intr(adapter->pdev->irq, netdev);
	adapter->flags &= ~IXGBE_FLAG_IN_NETPOLL;
	enable_irq(adapter->pdev->irq);
}
#endif

/**
 * ixgbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in ixgbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * ixgbe_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int __devinit ixgbe_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct ixgbe_adapter *adapter = NULL;
	struct ixgbe_hw *hw;
	const struct ixgbe_info *ii = ixgbe_info_tbl[ent->driver_data];
	unsigned long mmio_start, mmio_len;
	static int cards_found;
	int i, err, pci_using_dac;
	u16 link_status, link_speed, link_width;
	u32 part_num;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK) &&
	    !pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK)) {
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
		pci_using_dac = 0;
	}

	err = pci_request_regions(pdev, ixgbe_driver_name);
	if (err) {
		dev_err(&pdev->dev, "pci_request_regions failed 0x%x\n", err);
		goto err_pci_reg;
	}

	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct ixgbe_adapter));
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);

	adapter->netdev = netdev;
	adapter->pdev = pdev;
	hw = &adapter->hw;
	hw->back = adapter;
	adapter->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;

	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);

	hw->hw_addr = ioremap(mmio_start, mmio_len);
	if (!hw->hw_addr) {
		err = -EIO;
		goto err_ioremap;
	}

	for (i = 1; i <= 5; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
	}

	netdev->open = &ixgbe_open;
	netdev->stop = &ixgbe_close;
	netdev->hard_start_xmit = &ixgbe_xmit_frame;
	netdev->get_stats = &ixgbe_get_stats;
	netdev->set_multicast_list = &ixgbe_set_multi;
	netdev->set_mac_address = &ixgbe_set_mac;
	netdev->change_mtu = &ixgbe_change_mtu;
	ixgbe_set_ethtool_ops(netdev);
	netdev->tx_timeout = &ixgbe_tx_timeout;
	netdev->watchdog_timeo = 5 * HZ;
	netif_napi_add(netdev, &adapter->napi, ixgbe_clean, 64);
	netdev->vlan_rx_register = ixgbe_vlan_rx_register;
	netdev->vlan_rx_add_vid = ixgbe_vlan_rx_add_vid;
	netdev->vlan_rx_kill_vid = ixgbe_vlan_rx_kill_vid;
#ifdef CONFIG_NET_POLL_CONTROLLER
	netdev->poll_controller = ixgbe_netpoll;
#endif
	strcpy(netdev->name, pci_name(pdev));

	netdev->mem_start = mmio_start;
	netdev->mem_end = mmio_start + mmio_len;

	adapter->bd_number = cards_found;

	/* PCI config space info */
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->revision_id = pdev->revision;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;

	/* Setup hw api */
	memcpy(&hw->mac.ops, ii->mac_ops, sizeof(hw->mac.ops));
	memcpy(&hw->phy.ops, ii->phy_ops, sizeof(hw->phy.ops));

	err = ii->get_invariants(hw);
	if (err)
		goto err_hw_init;

	/* setup the private structure */
	err = ixgbe_sw_init(adapter);
	if (err)
		goto err_sw_init;

	netdev->features = NETIF_F_SG |
			   NETIF_F_HW_CSUM |
			   NETIF_F_HW_VLAN_TX |
			   NETIF_F_HW_VLAN_RX |
			   NETIF_F_HW_VLAN_FILTER;

	netdev->features |= NETIF_F_TSO;

	netdev->features |= NETIF_F_TSO6;
	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;


	/* make sure the EEPROM is good */
	if (ixgbe_validate_eeprom_checksum(hw, NULL) < 0) {
		dev_err(&pdev->dev, "The EEPROM Checksum Is Not Valid\n");
		err = -EIO;
		goto err_eeprom;
	}

	memcpy(netdev->dev_addr, hw->mac.perm_addr, netdev->addr_len);
	memcpy(netdev->perm_addr, hw->mac.perm_addr, netdev->addr_len);

	if (ixgbe_validate_mac_addr(netdev->dev_addr)) {
		err = -EIO;
		goto err_eeprom;
	}

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &ixgbe_watchdog;
	adapter->watchdog_timer.data = (unsigned long)adapter;

	INIT_WORK(&adapter->reset_task, ixgbe_reset_task);

	/* initialize default flow control settings */
	hw->fc.original_type = ixgbe_fc_full;
	hw->fc.type = ixgbe_fc_full;
	hw->fc.high_water = IXGBE_DEFAULT_FCRTH;
	hw->fc.low_water = IXGBE_DEFAULT_FCRTL;
	hw->fc.pause_time = IXGBE_DEFAULT_FCPAUSE;

	/* Interrupt Throttle Rate */
	adapter->rx_eitr = (1000000 / IXGBE_DEFAULT_ITR_RX_USECS);
	adapter->tx_eitr = (1000000 / IXGBE_DEFAULT_ITR_TX_USECS);

	/* print bus type/speed/width info */
	pci_read_config_word(pdev, IXGBE_PCI_LINK_STATUS, &link_status);
	link_speed = link_status & IXGBE_PCI_LINK_SPEED;
	link_width = link_status & IXGBE_PCI_LINK_WIDTH;
	dev_info(&pdev->dev, "(PCI Express:%s:%s) "
		 "%02x:%02x:%02x:%02x:%02x:%02x\n",
		((link_speed == IXGBE_PCI_LINK_SPEED_5000) ? "5.0Gb/s" :
		 (link_speed == IXGBE_PCI_LINK_SPEED_2500) ? "2.5Gb/s" :
		 "Unknown"),
		((link_width == IXGBE_PCI_LINK_WIDTH_8) ? "Width x8" :
		 (link_width == IXGBE_PCI_LINK_WIDTH_4) ? "Width x4" :
		 (link_width == IXGBE_PCI_LINK_WIDTH_2) ? "Width x2" :
		 (link_width == IXGBE_PCI_LINK_WIDTH_1) ? "Width x1" :
		 "Unknown"),
		netdev->dev_addr[0], netdev->dev_addr[1], netdev->dev_addr[2],
		netdev->dev_addr[3], netdev->dev_addr[4], netdev->dev_addr[5]);
	ixgbe_read_part_num(hw, &part_num);
	dev_info(&pdev->dev, "MAC: %d, PHY: %d, PBA No: %06x-%03x\n",
		 hw->mac.type, hw->phy.type,
		 (part_num >> 8), (part_num & 0xff));

	/* reset the hardware with the new settings */
	ixgbe_start_hw(hw);

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err)
		goto err_register;


	dev_info(&pdev->dev, "Intel(R) 10 Gigabit Network Connection\n");
	cards_found++;
	return 0;

err_register:
err_hw_init:
err_sw_init:
err_eeprom:
	iounmap(hw->hw_addr);
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
 * ixgbe_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * ixgbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void __devexit ixgbe_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	set_bit(__IXGBE_DOWN, &adapter->state);
	del_timer_sync(&adapter->watchdog_timer);

	flush_scheduled_work();

	unregister_netdev(netdev);

	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);

	iounmap(adapter->hw.hw_addr);
	pci_release_regions(pdev);

	free_netdev(netdev);

	pci_disable_device(pdev);
}

/**
 * ixgbe_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t ixgbe_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev->priv;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		ixgbe_down(adapter);
	pci_disable_device(pdev);

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * ixgbe_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 */
static pci_ers_result_t ixgbe_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev->priv;

	if (pci_enable_device(pdev)) {
		DPRINTK(PROBE, ERR,
			"Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	ixgbe_reset(adapter);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * ixgbe_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation.
 */
static void ixgbe_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev->priv;

	if (netif_running(netdev)) {
		if (ixgbe_up(adapter)) {
			DPRINTK(PROBE, INFO, "ixgbe_up failed after reset\n");
			return;
		}
	}

	netif_device_attach(netdev);

}

static struct pci_error_handlers ixgbe_err_handler = {
	.error_detected = ixgbe_io_error_detected,
	.slot_reset = ixgbe_io_slot_reset,
	.resume = ixgbe_io_resume,
};

static struct pci_driver ixgbe_driver = {
	.name     = ixgbe_driver_name,
	.id_table = ixgbe_pci_tbl,
	.probe    = ixgbe_probe,
	.remove   = __devexit_p(ixgbe_remove),
#ifdef CONFIG_PM
	.suspend  = ixgbe_suspend,
	.resume   = ixgbe_resume,
#endif
	.shutdown = ixgbe_shutdown,
	.err_handler = &ixgbe_err_handler
};

/**
 * ixgbe_init_module - Driver Registration Routine
 *
 * ixgbe_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init ixgbe_init_module(void)
{
	int ret;
	printk(KERN_INFO "%s: %s - version %s\n", ixgbe_driver_name,
	       ixgbe_driver_string, ixgbe_driver_version);

	printk(KERN_INFO "%s: %s\n", ixgbe_driver_name, ixgbe_copyright);

	ret = pci_register_driver(&ixgbe_driver);
	return ret;
}
module_init(ixgbe_init_module);

/**
 * ixgbe_exit_module - Driver Exit Cleanup Routine
 *
 * ixgbe_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit ixgbe_exit_module(void)
{
	pci_unregister_driver(&ixgbe_driver);
}
module_exit(ixgbe_exit_module);

/* ixgbe_main.c */
