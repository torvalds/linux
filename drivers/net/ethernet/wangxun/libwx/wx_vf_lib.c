// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_hw.h"
#include "wx_lib.h"
#include "wx_vf.h"
#include "wx_vf_lib.h"

void wx_write_eitr_vf(struct wx_q_vector *q_vector)
{
	struct wx *wx = q_vector->wx;
	int v_idx = q_vector->v_idx;
	u32 itr_reg;

	itr_reg = q_vector->itr & WX_VXITR_MASK;

	/* set the WDIS bit to not clear the timer bits and cause an
	 * immediate assertion of the interrupt
	 */
	itr_reg |= WX_VXITR_CNT_WDIS;

	wr32(wx, WX_VXITR(v_idx), itr_reg);
}

static void wx_set_ivar_vf(struct wx *wx, s8 direction, u8 queue,
			   u8 msix_vector)
{
	u32 ivar, index;

	if (direction == -1) {
		/* other causes */
		msix_vector |= WX_PX_IVAR_ALLOC_VAL;
		ivar = rd32(wx, WX_VXIVAR_MISC);
		ivar &= ~0xFF;
		ivar |= msix_vector;
		wr32(wx, WX_VXIVAR_MISC, ivar);
	} else {
		/* tx or rx causes */
		msix_vector |= WX_PX_IVAR_ALLOC_VAL;
		index = ((16 * (queue & 1)) + (8 * direction));
		ivar = rd32(wx, WX_VXIVAR(queue >> 1));
		ivar &= ~(0xFF << index);
		ivar |= (msix_vector << index);
		wr32(wx, WX_VXIVAR(queue >> 1), ivar);
	}
}

void wx_configure_msix_vf(struct wx *wx)
{
	int v_idx;

	wx->eims_enable_mask = 0;
	for (v_idx = 0; v_idx < wx->num_q_vectors; v_idx++) {
		struct wx_q_vector *q_vector = wx->q_vector[v_idx];
		struct wx_ring *ring;

		wx_for_each_ring(ring, q_vector->rx)
			wx_set_ivar_vf(wx, 0, ring->reg_idx, v_idx);

		wx_for_each_ring(ring, q_vector->tx)
			wx_set_ivar_vf(wx, 1, ring->reg_idx, v_idx);

		/* add q_vector eims value to global eims_enable_mask */
		wx->eims_enable_mask |= BIT(v_idx);
		wx_write_eitr_vf(q_vector);
	}

	wx_set_ivar_vf(wx, -1, 1, v_idx);

	/* setup eims_other and add value to global eims_enable_mask */
	wx->eims_other = BIT(v_idx);
	wx->eims_enable_mask |= wx->eims_other;
}

int wx_write_uc_addr_list_vf(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);
	int count = 0;

	if (!netdev_uc_empty(netdev)) {
		struct netdev_hw_addr *ha;

		netdev_for_each_uc_addr(ha, netdev)
			wx_set_uc_addr_vf(wx, ++count, ha->addr);
	} else {
		/*
		 * If the list is empty then send message to PF driver to
		 * clear all macvlans on this VF.
		 */
		wx_set_uc_addr_vf(wx, 0, NULL);
	}

	return count;
}

/**
 * wx_configure_tx_ring_vf - Configure Tx ring after Reset
 * @wx: board private structure
 * @ring: structure containing ring specific data
 *
 * Configure the Tx descriptor ring after a reset.
 **/
static void wx_configure_tx_ring_vf(struct wx *wx, struct wx_ring *ring)
{
	u8 reg_idx = ring->reg_idx;
	u64 tdba = ring->dma;
	u32 txdctl = 0;
	int ret;

	/* disable queue to avoid issues while updating state */
	wr32(wx, WX_VXTXDCTL(reg_idx), WX_VXTXDCTL_FLUSH);
	wr32(wx, WX_VXTDBAL(reg_idx), tdba & DMA_BIT_MASK(32));
	wr32(wx, WX_VXTDBAH(reg_idx), tdba >> 32);

	/* enable relaxed ordering */
	pcie_capability_clear_and_set_word(wx->pdev, PCI_EXP_DEVCTL,
					   0, PCI_EXP_DEVCTL_RELAX_EN);

	/* reset head and tail pointers */
	wr32(wx, WX_VXTDH(reg_idx), 0);
	wr32(wx, WX_VXTDT(reg_idx), 0);
	ring->tail = wx->hw_addr + WX_VXTDT(reg_idx);

	/* reset ntu and ntc to place SW in sync with hardwdare */
	ring->next_to_clean = 0;
	ring->next_to_use = 0;

	txdctl |= WX_VXTXDCTL_BUFLEN(wx_buf_len(ring->count));
	txdctl |= WX_VXTXDCTL_ENABLE;

	/* reinitialize tx_buffer_info */
	memset(ring->tx_buffer_info, 0,
	       sizeof(struct wx_tx_buffer) * ring->count);

	wr32(wx, WX_VXTXDCTL(reg_idx), txdctl);
	/* poll to verify queue is enabled */
	ret = read_poll_timeout(rd32, txdctl, txdctl & WX_VXTXDCTL_ENABLE,
				1000, 10000, true, wx, WX_VXTXDCTL(reg_idx));
	if (ret == -ETIMEDOUT)
		wx_err(wx, "Could not enable Tx Queue %d\n", reg_idx);
}

/**
 * wx_configure_tx_vf - Configure Transmit Unit after Reset
 * @wx: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
void wx_configure_tx_vf(struct wx *wx)
{
	u32 i;

	/* Setup the HW Tx Head and Tail descriptor pointers */
	for (i = 0; i < wx->num_tx_queues; i++)
		wx_configure_tx_ring_vf(wx, wx->tx_ring[i]);
}

static void wx_configure_srrctl_vf(struct wx *wx, struct wx_ring *ring,
				   int index)
{
	u32 srrctl;

	srrctl = rd32m(wx, WX_VXRXDCTL(index),
		       (u32)~(WX_VXRXDCTL_HDRSZ_MASK | WX_VXRXDCTL_BUFSZ_MASK));
	srrctl |= WX_VXRXDCTL_DROP;
	srrctl |= WX_VXRXDCTL_HDRSZ(wx_hdr_sz(WX_RX_HDR_SIZE));
	srrctl |= WX_VXRXDCTL_BUFSZ(wx_buf_sz(WX_RX_BUF_SIZE));

	wr32(wx, WX_VXRXDCTL(index), srrctl);
}

void wx_setup_psrtype_vf(struct wx *wx)
{
	/* PSRTYPE must be initialized */
	u32 psrtype = WX_VXMRQC_PSR_L2HDR |
		      WX_VXMRQC_PSR_L3HDR |
		      WX_VXMRQC_PSR_L4HDR |
		      WX_VXMRQC_PSR_TUNHDR |
		      WX_VXMRQC_PSR_TUNMAC;

	wr32m(wx, WX_VXMRQC, WX_VXMRQC_PSR_MASK, WX_VXMRQC_PSR(psrtype));
}

void wx_setup_vfmrqc_vf(struct wx *wx)
{
	u16 rss_i = wx->num_rx_queues;
	u32 vfmrqc = 0, vfreta = 0;
	u8 i, j;

	/* Fill out hash function seeds */
	netdev_rss_key_fill(wx->rss_key, WX_RSS_KEY_SIZE);
	for (i = 0; i < WX_RSS_KEY_SIZE / 4; i++)
		wr32(wx, WX_VXRSSRK(i), wx->rss_key[i]);

	for (i = 0, j = 0; i < WX_MAX_RETA_ENTRIES; i++, j++) {
		if (j == rss_i)
			j = 0;

		wx->rss_indir_tbl[i] = j;

		vfreta |= j << (i & 0x3) * 8;
		if ((i & 3) == 3) {
			wr32(wx, WX_VXRETA(i >> 2), vfreta);
			vfreta = 0;
		}
	}

	/* Perform hash on these packet types */
	vfmrqc |= WX_VXMRQC_RSS_ALG_IPV4 |
		  WX_VXMRQC_RSS_ALG_IPV4_TCP |
		  WX_VXMRQC_RSS_ALG_IPV6 |
		  WX_VXMRQC_RSS_ALG_IPV6_TCP;

	vfmrqc |= WX_VXMRQC_RSS_EN;

	if (wx->num_rx_queues > 3)
		vfmrqc |= WX_VXMRQC_RSS_HASH(2);
	else if (wx->num_rx_queues > 1)
		vfmrqc |= WX_VXMRQC_RSS_HASH(1);
	wr32m(wx, WX_VXMRQC, WX_VXMRQC_RSS_MASK, WX_VXMRQC_RSS(vfmrqc));
}

void wx_configure_rx_ring_vf(struct wx *wx, struct wx_ring *ring)
{
	u8 reg_idx = ring->reg_idx;
	union wx_rx_desc *rx_desc;
	u64 rdba = ring->dma;
	u32 rxdctl;

	/* disable queue to avoid issues while updating state */
	rxdctl = rd32(wx, WX_VXRXDCTL(reg_idx));
	wx_disable_rx_queue(wx, ring);

	wr32(wx, WX_VXRDBAL(reg_idx), rdba & DMA_BIT_MASK(32));
	wr32(wx, WX_VXRDBAH(reg_idx), rdba >> 32);

	/* enable relaxed ordering */
	pcie_capability_clear_and_set_word(wx->pdev, PCI_EXP_DEVCTL,
					   0, PCI_EXP_DEVCTL_RELAX_EN);

	/* reset head and tail pointers */
	wr32(wx, WX_VXRDH(reg_idx), 0);
	wr32(wx, WX_VXRDT(reg_idx), 0);
	ring->tail = wx->hw_addr + WX_VXRDT(reg_idx);

	/* initialize rx_buffer_info */
	memset(ring->rx_buffer_info, 0,
	       sizeof(struct wx_rx_buffer) * ring->count);

	/* initialize Rx descriptor 0 */
	rx_desc = WX_RX_DESC(ring, 0);
	rx_desc->wb.upper.length = 0;

	/* reset ntu and ntc to place SW in sync with hardwdare */
	ring->next_to_clean = 0;
	ring->next_to_use = 0;
	ring->next_to_alloc = 0;

	wx_configure_srrctl_vf(wx, ring, reg_idx);

	/* allow any size packet since we can handle overflow */
	rxdctl &= ~WX_VXRXDCTL_BUFLEN_MASK;
	rxdctl |= WX_VXRXDCTL_BUFLEN(wx_buf_len(ring->count));
	rxdctl |= WX_VXRXDCTL_ENABLE | WX_VXRXDCTL_VLAN;

	/* enable RSC */
	rxdctl &= ~WX_VXRXDCTL_RSCMAX_MASK;
	rxdctl |= WX_VXRXDCTL_RSCMAX(0);
	rxdctl |= WX_VXRXDCTL_RSCEN;

	wr32(wx, WX_VXRXDCTL(reg_idx), rxdctl);

	/* pf/vf reuse */
	wx_enable_rx_queue(wx, ring);
	wx_alloc_rx_buffers(ring, wx_desc_unused(ring));
}
