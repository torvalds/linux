/* bnx2x_cmn.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */
#ifndef BNX2X_CMN_H
#define BNX2X_CMN_H

#include <linux/types.h>
#include <linux/netdevice.h>


#include "bnx2x.h"


/*********************** Interfaces ****************************
 *  Functions that need to be implemented by each driver version
 */

/**
 * Initialize link parameters structure variables.
 *
 * @param bp
 * @param load_mode
 *
 * @return u8
 */
u8 bnx2x_initial_phy_init(struct bnx2x *bp, int load_mode);

/**
 * Configure hw according to link parameters structure.
 *
 * @param bp
 */
void bnx2x_link_set(struct bnx2x *bp);

/**
 * Query link status
 *
 * @param bp
 *
 * @return 0 - link is UP
 */
u8 bnx2x_link_test(struct bnx2x *bp);

/**
 * Handles link status change
 *
 * @param bp
 */
void bnx2x__link_status_update(struct bnx2x *bp);

/**
 * MSI-X slowpath interrupt handler
 *
 * @param irq
 * @param dev_instance
 *
 * @return irqreturn_t
 */
irqreturn_t bnx2x_msix_sp_int(int irq, void *dev_instance);

/**
 * non MSI-X interrupt handler
 *
 * @param irq
 * @param dev_instance
 *
 * @return irqreturn_t
 */
irqreturn_t bnx2x_interrupt(int irq, void *dev_instance);
#ifdef BCM_CNIC

/**
 * Send command to cnic driver
 *
 * @param bp
 * @param cmd
 */
int bnx2x_cnic_notify(struct bnx2x *bp, int cmd);

/**
 * Provides cnic information for proper interrupt handling
 *
 * @param bp
 */
void bnx2x_setup_cnic_irq_info(struct bnx2x *bp);
#endif

/**
 * Enable HW interrupts.
 *
 * @param bp
 */
void bnx2x_int_enable(struct bnx2x *bp);

/**
 * Disable interrupts. This function ensures that there are no
 * ISRs or SP DPCs (sp_task) are running after it returns.
 *
 * @param bp
 * @param disable_hw if true, disable HW interrupts.
 */
void bnx2x_int_disable_sync(struct bnx2x *bp, int disable_hw);

/**
 * Init HW blocks according to current initialization stage:
 * COMMON, PORT or FUNCTION.
 *
 * @param bp
 * @param load_code: COMMON, PORT or FUNCTION
 *
 * @return int
 */
int bnx2x_init_hw(struct bnx2x *bp, u32 load_code);

/**
 * Init driver internals:
 *  - rings
 *  - status blocks
 *  - etc.
 *
 * @param bp
 * @param load_code COMMON, PORT or FUNCTION
 */
void bnx2x_nic_init(struct bnx2x *bp, u32 load_code);

/**
 * Allocate driver's memory.
 *
 * @param bp
 *
 * @return int
 */
int bnx2x_alloc_mem(struct bnx2x *bp);

/**
 * Release driver's memory.
 *
 * @param bp
 */
void bnx2x_free_mem(struct bnx2x *bp);

/**
 * Bring up a leading (the first) eth Client.
 *
 * @param bp
 *
 * @return int
 */
int bnx2x_setup_leading(struct bnx2x *bp);

/**
 * Setup non-leading eth Client.
 *
 * @param bp
 * @param fp
 *
 * @return int
 */
int bnx2x_setup_multi(struct bnx2x *bp, int index);

/**
 * Set number of quueus according to mode and number of available
 * msi-x vectors
 *
 * @param bp
 *
 */
void bnx2x_set_num_queues_msix(struct bnx2x *bp);

/**
 * Cleanup chip internals:
 * - Cleanup MAC configuration.
 * - Close clients.
 * - etc.
 *
 * @param bp
 * @param unload_mode
 */
void bnx2x_chip_cleanup(struct bnx2x *bp, int unload_mode);

/**
 * Acquire HW lock.
 *
 * @param bp
 * @param resource Resource bit which was locked
 *
 * @return int
 */
int bnx2x_acquire_hw_lock(struct bnx2x *bp, u32 resource);

/**
 * Release HW lock.
 *
 * @param bp driver handle
 * @param resource Resource bit which was locked
 *
 * @return int
 */
int bnx2x_release_hw_lock(struct bnx2x *bp, u32 resource);

/**
 * Configure eth MAC address in the HW according to the value in
 * netdev->dev_addr for 57711
 *
 * @param bp driver handle
 * @param set
 */
void bnx2x_set_eth_mac_addr_e1h(struct bnx2x *bp, int set);

/**
 * Configure eth MAC address in the HW according to the value in
 * netdev->dev_addr for 57710
 *
 * @param bp driver handle
 * @param set
 */
void bnx2x_set_eth_mac_addr_e1(struct bnx2x *bp, int set);

#ifdef BCM_CNIC
/**
 * Set iSCSI MAC(s) at the next enties in the CAM after the ETH
 * MAC(s). The function will wait until the ramrod completion
 * returns.
 *
 * @param bp driver handle
 * @param set set or clear the CAM entry
 *
 * @return 0 if cussess, -ENODEV if ramrod doesn't return.
 */
int bnx2x_set_iscsi_eth_mac_addr(struct bnx2x *bp, int set);
#endif

/**
 * Initialize status block in FW and HW
 *
 * @param bp driver handle
 * @param sb host_status_block
 * @param dma_addr_t mapping
 * @param int sb_id
 */
void bnx2x_init_sb(struct bnx2x *bp, struct host_status_block *sb,
			  dma_addr_t mapping, int sb_id);

/**
 * Reconfigure FW/HW according to dev->flags rx mode
 *
 * @param dev net_device
 *
 */
void bnx2x_set_rx_mode(struct net_device *dev);

/**
 * Configure MAC filtering rules in a FW.
 *
 * @param bp driver handle
 */
void bnx2x_set_storm_rx_mode(struct bnx2x *bp);

/* Parity errors related */
void bnx2x_inc_load_cnt(struct bnx2x *bp);
u32 bnx2x_dec_load_cnt(struct bnx2x *bp);
bool bnx2x_chk_parity_attn(struct bnx2x *bp);
bool bnx2x_reset_is_done(struct bnx2x *bp);
void bnx2x_disable_close_the_gate(struct bnx2x *bp);

/**
 * Perform statistics handling according to event
 *
 * @param bp driver handle
 * @param even tbnx2x_stats_event
 */
void bnx2x_stats_handle(struct bnx2x *bp, enum bnx2x_stats_event event);

/**
 * Configures FW with client paramteres (like HW VLAN removal)
 * for each active client.
 *
 * @param bp
 */
void bnx2x_set_client_config(struct bnx2x *bp);

/**
 * Handle sp events
 *
 * @param fp fastpath handle for the event
 * @param rr_cqe eth_rx_cqe
 */
void bnx2x_sp_event(struct bnx2x_fastpath *fp,  union eth_rx_cqe *rr_cqe);


static inline void bnx2x_update_fpsb_idx(struct bnx2x_fastpath *fp)
{
	struct host_status_block *fpsb = fp->status_blk;

	barrier(); /* status block is written to by the chip */
	fp->fp_c_idx = fpsb->c_status_block.status_block_index;
	fp->fp_u_idx = fpsb->u_status_block.status_block_index;
}

static inline void bnx2x_update_rx_prod(struct bnx2x *bp,
					struct bnx2x_fastpath *fp,
					u16 bd_prod, u16 rx_comp_prod,
					u16 rx_sge_prod)
{
	struct ustorm_eth_rx_producers rx_prods = {0};
	int i;

	/* Update producers */
	rx_prods.bd_prod = bd_prod;
	rx_prods.cqe_prod = rx_comp_prod;
	rx_prods.sge_prod = rx_sge_prod;

	/*
	 * Make sure that the BD and SGE data is updated before updating the
	 * producers since FW might read the BD/SGE right after the producer
	 * is updated.
	 * This is only applicable for weak-ordered memory model archs such
	 * as IA-64. The following barrier is also mandatory since FW will
	 * assumes BDs must have buffers.
	 */
	wmb();

	for (i = 0; i < sizeof(struct ustorm_eth_rx_producers)/4; i++)
		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_RX_PRODS_OFFSET(BP_PORT(bp), fp->cl_id) + i*4,
		       ((u32 *)&rx_prods)[i]);

	mmiowb(); /* keep prod updates ordered */

	DP(NETIF_MSG_RX_STATUS,
	   "queue[%d]:  wrote  bd_prod %u  cqe_prod %u  sge_prod %u\n",
	   fp->index, bd_prod, rx_comp_prod, rx_sge_prod);
}



static inline void bnx2x_ack_sb(struct bnx2x *bp, u8 sb_id,
				u8 storm, u16 index, u8 op, u8 update)
{
	u32 hc_addr = (HC_REG_COMMAND_REG + BP_PORT(bp)*32 +
		       COMMAND_REG_INT_ACK);
	struct igu_ack_register igu_ack;

	igu_ack.status_block_index = index;
	igu_ack.sb_id_and_flags =
			((sb_id << IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT) |
			 (storm << IGU_ACK_REGISTER_STORM_ID_SHIFT) |
			 (update << IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT) |
			 (op << IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT));

	DP(BNX2X_MSG_OFF, "write 0x%08x to HC addr 0x%x\n",
	   (*(u32 *)&igu_ack), hc_addr);
	REG_WR(bp, hc_addr, (*(u32 *)&igu_ack));

	/* Make sure that ACK is written */
	mmiowb();
	barrier();
}
static inline u16 bnx2x_ack_int(struct bnx2x *bp)
{
	u32 hc_addr = (HC_REG_COMMAND_REG + BP_PORT(bp)*32 +
		       COMMAND_REG_SIMD_MASK);
	u32 result = REG_RD(bp, hc_addr);

	DP(BNX2X_MSG_OFF, "read 0x%08x from HC addr 0x%x\n",
	   result, hc_addr);

	return result;
}

/*
 * fast path service functions
 */

static inline int bnx2x_has_tx_work_unload(struct bnx2x_fastpath *fp)
{
	/* Tell compiler that consumer and producer can change */
	barrier();
	return (fp->tx_pkt_prod != fp->tx_pkt_cons);
}

static inline u16 bnx2x_tx_avail(struct bnx2x_fastpath *fp)
{
	s16 used;
	u16 prod;
	u16 cons;

	prod = fp->tx_bd_prod;
	cons = fp->tx_bd_cons;

	/* NUM_TX_RINGS = number of "next-page" entries
	   It will be used as a threshold */
	used = SUB_S16(prod, cons) + (s16)NUM_TX_RINGS;

#ifdef BNX2X_STOP_ON_ERROR
	WARN_ON(used < 0);
	WARN_ON(used > fp->bp->tx_ring_size);
	WARN_ON((fp->bp->tx_ring_size - used) > MAX_TX_AVAIL);
#endif

	return (s16)(fp->bp->tx_ring_size) - used;
}

static inline int bnx2x_has_tx_work(struct bnx2x_fastpath *fp)
{
	u16 hw_cons;

	/* Tell compiler that status block fields can change */
	barrier();
	hw_cons = le16_to_cpu(*fp->tx_cons_sb);
	return hw_cons != fp->tx_pkt_cons;
}

static inline void bnx2x_free_rx_sge(struct bnx2x *bp,
				     struct bnx2x_fastpath *fp, u16 index)
{
	struct sw_rx_page *sw_buf = &fp->rx_page_ring[index];
	struct page *page = sw_buf->page;
	struct eth_rx_sge *sge = &fp->rx_sge_ring[index];

	/* Skip "next page" elements */
	if (!page)
		return;

	dma_unmap_page(&bp->pdev->dev, dma_unmap_addr(sw_buf, mapping),
		       SGE_PAGE_SIZE*PAGES_PER_SGE, PCI_DMA_FROMDEVICE);
	__free_pages(page, PAGES_PER_SGE_SHIFT);

	sw_buf->page = NULL;
	sge->addr_hi = 0;
	sge->addr_lo = 0;
}

static inline void bnx2x_free_rx_sge_range(struct bnx2x *bp,
					   struct bnx2x_fastpath *fp, int last)
{
	int i;

	for (i = 0; i < last; i++)
		bnx2x_free_rx_sge(bp, fp, i);
}

static inline int bnx2x_alloc_rx_sge(struct bnx2x *bp,
				     struct bnx2x_fastpath *fp, u16 index)
{
	struct page *page = alloc_pages(GFP_ATOMIC, PAGES_PER_SGE_SHIFT);
	struct sw_rx_page *sw_buf = &fp->rx_page_ring[index];
	struct eth_rx_sge *sge = &fp->rx_sge_ring[index];
	dma_addr_t mapping;

	if (unlikely(page == NULL))
		return -ENOMEM;

	mapping = dma_map_page(&bp->pdev->dev, page, 0,
			       SGE_PAGE_SIZE*PAGES_PER_SGE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(&bp->pdev->dev, mapping))) {
		__free_pages(page, PAGES_PER_SGE_SHIFT);
		return -ENOMEM;
	}

	sw_buf->page = page;
	dma_unmap_addr_set(sw_buf, mapping, mapping);

	sge->addr_hi = cpu_to_le32(U64_HI(mapping));
	sge->addr_lo = cpu_to_le32(U64_LO(mapping));

	return 0;
}
static inline int bnx2x_alloc_rx_skb(struct bnx2x *bp,
				     struct bnx2x_fastpath *fp, u16 index)
{
	struct sk_buff *skb;
	struct sw_rx_bd *rx_buf = &fp->rx_buf_ring[index];
	struct eth_rx_bd *rx_bd = &fp->rx_desc_ring[index];
	dma_addr_t mapping;

	skb = netdev_alloc_skb(bp->dev, bp->rx_buf_size);
	if (unlikely(skb == NULL))
		return -ENOMEM;

	mapping = dma_map_single(&bp->pdev->dev, skb->data, bp->rx_buf_size,
				 DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(&bp->pdev->dev, mapping))) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	rx_buf->skb = skb;
	dma_unmap_addr_set(rx_buf, mapping, mapping);

	rx_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
	rx_bd->addr_lo = cpu_to_le32(U64_LO(mapping));

	return 0;
}

/* note that we are not allocating a new skb,
 * we are just moving one from cons to prod
 * we are not creating a new mapping,
 * so there is no need to check for dma_mapping_error().
 */
static inline void bnx2x_reuse_rx_skb(struct bnx2x_fastpath *fp,
			       struct sk_buff *skb, u16 cons, u16 prod)
{
	struct bnx2x *bp = fp->bp;
	struct sw_rx_bd *cons_rx_buf = &fp->rx_buf_ring[cons];
	struct sw_rx_bd *prod_rx_buf = &fp->rx_buf_ring[prod];
	struct eth_rx_bd *cons_bd = &fp->rx_desc_ring[cons];
	struct eth_rx_bd *prod_bd = &fp->rx_desc_ring[prod];

	dma_sync_single_for_device(&bp->pdev->dev,
				   dma_unmap_addr(cons_rx_buf, mapping),
				   RX_COPY_THRESH, DMA_FROM_DEVICE);

	prod_rx_buf->skb = cons_rx_buf->skb;
	dma_unmap_addr_set(prod_rx_buf, mapping,
			   dma_unmap_addr(cons_rx_buf, mapping));
	*prod_bd = *cons_bd;
}

static inline void bnx2x_clear_sge_mask_next_elems(struct bnx2x_fastpath *fp)
{
	int i, j;

	for (i = 1; i <= NUM_RX_SGE_PAGES; i++) {
		int idx = RX_SGE_CNT * i - 1;

		for (j = 0; j < 2; j++) {
			SGE_MASK_CLEAR_BIT(fp, idx);
			idx--;
		}
	}
}

static inline void bnx2x_init_sge_ring_bit_mask(struct bnx2x_fastpath *fp)
{
	/* Set the mask to all 1-s: it's faster to compare to 0 than to 0xf-s */
	memset(fp->sge_mask, 0xff,
	       (NUM_RX_SGE >> RX_SGE_MASK_ELEM_SHIFT)*sizeof(u64));

	/* Clear the two last indices in the page to 1:
	   these are the indices that correspond to the "next" element,
	   hence will never be indicated and should be removed from
	   the calculations. */
	bnx2x_clear_sge_mask_next_elems(fp);
}
static inline void bnx2x_free_tpa_pool(struct bnx2x *bp,
				       struct bnx2x_fastpath *fp, int last)
{
	int i;

	for (i = 0; i < last; i++) {
		struct sw_rx_bd *rx_buf = &(fp->tpa_pool[i]);
		struct sk_buff *skb = rx_buf->skb;

		if (skb == NULL) {
			DP(NETIF_MSG_IFDOWN, "tpa bin %d empty on free\n", i);
			continue;
		}

		if (fp->tpa_state[i] == BNX2X_TPA_START)
			dma_unmap_single(&bp->pdev->dev,
					 dma_unmap_addr(rx_buf, mapping),
					 bp->rx_buf_size, DMA_FROM_DEVICE);

		dev_kfree_skb(skb);
		rx_buf->skb = NULL;
	}
}


static inline void bnx2x_init_tx_ring(struct bnx2x *bp)
{
	int i, j;

	for_each_queue(bp, j) {
		struct bnx2x_fastpath *fp = &bp->fp[j];

		for (i = 1; i <= NUM_TX_RINGS; i++) {
			struct eth_tx_next_bd *tx_next_bd =
				&fp->tx_desc_ring[TX_DESC_CNT * i - 1].next_bd;

			tx_next_bd->addr_hi =
				cpu_to_le32(U64_HI(fp->tx_desc_mapping +
					    BCM_PAGE_SIZE*(i % NUM_TX_RINGS)));
			tx_next_bd->addr_lo =
				cpu_to_le32(U64_LO(fp->tx_desc_mapping +
					    BCM_PAGE_SIZE*(i % NUM_TX_RINGS)));
		}

		fp->tx_db.data.header.header = DOORBELL_HDR_DB_TYPE;
		fp->tx_db.data.zero_fill1 = 0;
		fp->tx_db.data.prod = 0;

		fp->tx_pkt_prod = 0;
		fp->tx_pkt_cons = 0;
		fp->tx_bd_prod = 0;
		fp->tx_bd_cons = 0;
		fp->tx_cons_sb = BNX2X_TX_SB_INDEX;
		fp->tx_pkt = 0;
	}
}
static inline int bnx2x_has_rx_work(struct bnx2x_fastpath *fp)
{
	u16 rx_cons_sb;

	/* Tell compiler that status block fields can change */
	barrier();
	rx_cons_sb = le16_to_cpu(*fp->rx_cons_sb);
	if ((rx_cons_sb & MAX_RCQ_DESC_CNT) == MAX_RCQ_DESC_CNT)
		rx_cons_sb++;
	return (fp->rx_comp_cons != rx_cons_sb);
}

/* HW Lock for shared dual port PHYs */
void bnx2x_acquire_phy_lock(struct bnx2x *bp);
void bnx2x_release_phy_lock(struct bnx2x *bp);

void bnx2x_link_report(struct bnx2x *bp);
int bnx2x_rx_int(struct bnx2x_fastpath *fp, int budget);
int bnx2x_tx_int(struct bnx2x_fastpath *fp);
void bnx2x_init_rx_rings(struct bnx2x *bp);
netdev_tx_t bnx2x_start_xmit(struct sk_buff *skb, struct net_device *dev);

int bnx2x_change_mac_addr(struct net_device *dev, void *p);
void bnx2x_tx_timeout(struct net_device *dev);
void bnx2x_vlan_rx_register(struct net_device *dev, struct vlan_group *vlgrp);
void bnx2x_netif_start(struct bnx2x *bp);
void bnx2x_netif_stop(struct bnx2x *bp, int disable_hw);
void bnx2x_free_irq(struct bnx2x *bp, bool disable_only);
int bnx2x_suspend(struct pci_dev *pdev, pm_message_t state);
int bnx2x_resume(struct pci_dev *pdev);
void bnx2x_free_skbs(struct bnx2x *bp);
int bnx2x_change_mtu(struct net_device *dev, int new_mtu);
int bnx2x_nic_unload(struct bnx2x *bp, int unload_mode);
int bnx2x_nic_load(struct bnx2x *bp, int load_mode);
int bnx2x_set_power_state(struct bnx2x *bp, pci_power_t state);

#endif /* BNX2X_CMN_H */
