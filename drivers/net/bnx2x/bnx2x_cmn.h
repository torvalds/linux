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

extern int num_queues;

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
 * @param is_serdes
 *
 * @return 0 - link is UP
 */
u8 bnx2x_link_test(struct bnx2x *bp, u8 is_serdes);

/**
 * Handles link status change
 *
 * @param bp
 */
void bnx2x__link_status_update(struct bnx2x *bp);

/**
 * Report link status to upper layer
 *
 * @param bp
 *
 * @return int
 */
void bnx2x_link_report(struct bnx2x *bp);

/**
 * calculates MF speed according to current linespeed and MF
 * configuration
 *
 * @param bp
 *
 * @return u16
 */
u16 bnx2x_get_mf_speed(struct bnx2x *bp);

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
 * Loads device firmware
 *
 * @param bp
 *
 * @return int
 */
int bnx2x_init_firmware(struct bnx2x *bp);

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
 * Setup eth Client.
 *
 * @param bp
 * @param fp
 * @param is_leading
 *
 * @return int
 */
int bnx2x_setup_client(struct bnx2x *bp, struct bnx2x_fastpath *fp,
		       int is_leading);

/**
 * Set number of queues according to mode
 *
 * @param bp
 *
 */
void bnx2x_set_num_queues(struct bnx2x *bp);

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
 * netdev->dev_addr.
 *
 * @param bp driver handle
 * @param set
 */
void bnx2x_set_eth_mac(struct bnx2x *bp, int set);

#ifdef BCM_CNIC
/**
 * Set/Clear FIP MAC(s) at the next enties in the CAM after the ETH
 * MAC(s). This function will wait until the ramdord completion
 * returns.
 *
 * @param bp driver handle
 * @param set set or clear the CAM entry
 *
 * @return 0 if cussess, -ENODEV if ramrod doesn't return.
 */
int bnx2x_set_fip_eth_mac_addr(struct bnx2x *bp, int set);

/**
 * Set/Clear ALL_ENODE mcast MAC.
 *
 * @param bp
 * @param set
 *
 * @return int
 */
int bnx2x_set_all_enode_macs(struct bnx2x *bp, int set);
#endif

/**
 * Set MAC filtering configurations.
 *
 * @remarks called with netif_tx_lock from dev_mcast.c
 *
 * @param dev net_device
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
 * @param event bnx2x_stats_event
 */
void bnx2x_stats_handle(struct bnx2x *bp, enum bnx2x_stats_event event);

/**
 * Handle ramrods completion
 *
 * @param fp fastpath handle for the event
 * @param rr_cqe eth_rx_cqe
 */
void bnx2x_sp_event(struct bnx2x_fastpath *fp, union eth_rx_cqe *rr_cqe);

/**
 * Init/halt function before/after sending
 * CLIENT_SETUP/CFC_DEL for the first/last client.
 *
 * @param bp
 *
 * @return int
 */
int bnx2x_func_start(struct bnx2x *bp);

/**
 * Prepare ILT configurations according to current driver
 * parameters.
 *
 * @param bp
 */
void bnx2x_ilt_set_info(struct bnx2x *bp);

/**
 * Inintialize dcbx protocol
 *
 * @param bp
 */
void bnx2x_dcbx_init(struct bnx2x *bp);

/**
 * Set power state to the requested value. Currently only D0 and
 * D3hot are supported.
 *
 * @param bp
 * @param state D0 or D3hot
 *
 * @return int
 */
int bnx2x_set_power_state(struct bnx2x *bp, pci_power_t state);

/**
 * Updates MAX part of MF configuration in HW
 * (if required)
 *
 * @param bp
 * @param value
 */
void bnx2x_update_max_mf_config(struct bnx2x *bp, u32 value);

/* dev_close main block */
int bnx2x_nic_unload(struct bnx2x *bp, int unload_mode);

/* dev_open main block */
int bnx2x_nic_load(struct bnx2x *bp, int load_mode);

/* hard_xmit callback */
netdev_tx_t bnx2x_start_xmit(struct sk_buff *skb, struct net_device *dev);

/* select_queue callback */
u16 bnx2x_select_queue(struct net_device *dev, struct sk_buff *skb);

int bnx2x_change_mac_addr(struct net_device *dev, void *p);

/* NAPI poll Rx part */
int bnx2x_rx_int(struct bnx2x_fastpath *fp, int budget);

/* NAPI poll Tx part */
int bnx2x_tx_int(struct bnx2x_fastpath *fp);

/* suspend/resume callbacks */
int bnx2x_suspend(struct pci_dev *pdev, pm_message_t state);
int bnx2x_resume(struct pci_dev *pdev);

/* Release IRQ vectors */
void bnx2x_free_irq(struct bnx2x *bp);

void bnx2x_init_rx_rings(struct bnx2x *bp);
void bnx2x_free_skbs(struct bnx2x *bp);
void bnx2x_netif_stop(struct bnx2x *bp, int disable_hw);
void bnx2x_netif_start(struct bnx2x *bp);

/**
 * Fill msix_table, request vectors, update num_queues according
 * to number of available vectors
 *
 * @param bp
 *
 * @return int
 */
int bnx2x_enable_msix(struct bnx2x *bp);

/**
 * Request msi mode from OS, updated internals accordingly
 *
 * @param bp
 *
 * @return int
 */
int bnx2x_enable_msi(struct bnx2x *bp);

/**
 * NAPI callback
 *
 * @param napi
 * @param budget
 *
 * @return int
 */
int bnx2x_poll(struct napi_struct *napi, int budget);

/**
 * Allocate/release memories outsize main driver structure
 *
 * @param bp
 *
 * @return int
 */
int __devinit bnx2x_alloc_mem_bp(struct bnx2x *bp);
void bnx2x_free_mem_bp(struct bnx2x *bp);

/**
 * Change mtu netdev callback
 *
 * @param dev
 * @param new_mtu
 *
 * @return int
 */
int bnx2x_change_mtu(struct net_device *dev, int new_mtu);

/**
 * tx timeout netdev callback
 *
 * @param dev
 * @param new_mtu
 *
 * @return int
 */
void bnx2x_tx_timeout(struct net_device *dev);

#ifdef BCM_VLAN
/**
 * vlan rx register netdev callback
 *
 * @param dev
 * @param new_mtu
 *
 * @return int
 */
void bnx2x_vlan_rx_register(struct net_device *dev,
				   struct vlan_group *vlgrp);

#endif

static inline void bnx2x_update_fpsb_idx(struct bnx2x_fastpath *fp)
{
	barrier(); /* status block is written to by the chip */
	fp->fp_hc_idx = fp->sb_running_index[SM_RX_ID];
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
		REG_WR(bp,
		       BAR_USTRORM_INTMEM + fp->ustorm_rx_prods_offset + i*4,
		       ((u32 *)&rx_prods)[i]);

	mmiowb(); /* keep prod updates ordered */

	DP(NETIF_MSG_RX_STATUS,
	   "queue[%d]:  wrote  bd_prod %u  cqe_prod %u  sge_prod %u\n",
	   fp->index, bd_prod, rx_comp_prod, rx_sge_prod);
}

static inline void bnx2x_igu_ack_sb_gen(struct bnx2x *bp, u8 igu_sb_id,
					u8 segment, u16 index, u8 op,
					u8 update, u32 igu_addr)
{
	struct igu_regular cmd_data = {0};

	cmd_data.sb_id_and_flags =
			((index << IGU_REGULAR_SB_INDEX_SHIFT) |
			 (segment << IGU_REGULAR_SEGMENT_ACCESS_SHIFT) |
			 (update << IGU_REGULAR_BUPDATE_SHIFT) |
			 (op << IGU_REGULAR_ENABLE_INT_SHIFT));

	DP(NETIF_MSG_HW, "write 0x%08x to IGU addr 0x%x\n",
	   cmd_data.sb_id_and_flags, igu_addr);
	REG_WR(bp, igu_addr, cmd_data.sb_id_and_flags);

	/* Make sure that ACK is written */
	mmiowb();
	barrier();
}

static inline void bnx2x_igu_clear_sb_gen(struct bnx2x *bp,
					  u8 idu_sb_id, bool is_Pf)
{
	u32 data, ctl, cnt = 100;
	u32 igu_addr_data = IGU_REG_COMMAND_REG_32LSB_DATA;
	u32 igu_addr_ctl = IGU_REG_COMMAND_REG_CTRL;
	u32 igu_addr_ack = IGU_REG_CSTORM_TYPE_0_SB_CLEANUP + (idu_sb_id/32)*4;
	u32 sb_bit =  1 << (idu_sb_id%32);
	u32 func_encode = BP_FUNC(bp) |
			((is_Pf == true ? 1 : 0) << IGU_FID_ENCODE_IS_PF_SHIFT);
	u32 addr_encode = IGU_CMD_E2_PROD_UPD_BASE + idu_sb_id;

	/* Not supported in BC mode */
	if (CHIP_INT_MODE_IS_BC(bp))
		return;

	data = (IGU_USE_REGISTER_cstorm_type_0_sb_cleanup
			<< IGU_REGULAR_CLEANUP_TYPE_SHIFT)	|
		IGU_REGULAR_CLEANUP_SET				|
		IGU_REGULAR_BCLEANUP;

	ctl = addr_encode << IGU_CTRL_REG_ADDRESS_SHIFT		|
	      func_encode << IGU_CTRL_REG_FID_SHIFT		|
	      IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT;

	DP(NETIF_MSG_HW, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
			 data, igu_addr_data);
	REG_WR(bp, igu_addr_data, data);
	mmiowb();
	barrier();
	DP(NETIF_MSG_HW, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
			  ctl, igu_addr_ctl);
	REG_WR(bp, igu_addr_ctl, ctl);
	mmiowb();
	barrier();

	/* wait for clean up to finish */
	while (!(REG_RD(bp, igu_addr_ack) & sb_bit) && --cnt)
		msleep(20);


	if (!(REG_RD(bp, igu_addr_ack) & sb_bit)) {
		DP(NETIF_MSG_HW, "Unable to finish IGU cleanup: "
			  "idu_sb_id %d offset %d bit %d (cnt %d)\n",
			  idu_sb_id, idu_sb_id/32, idu_sb_id%32, cnt);
	}
}

static inline void bnx2x_hc_ack_sb(struct bnx2x *bp, u8 sb_id,
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

static inline void bnx2x_igu_ack_sb(struct bnx2x *bp, u8 igu_sb_id, u8 segment,
		      u16 index, u8 op, u8 update)
{
	u32 igu_addr = BAR_IGU_INTMEM + (IGU_CMD_INT_ACK_BASE + igu_sb_id)*8;

	bnx2x_igu_ack_sb_gen(bp, igu_sb_id, segment, index, op, update,
			     igu_addr);
}

static inline void bnx2x_ack_sb(struct bnx2x *bp, u8 igu_sb_id, u8 storm,
				u16 index, u8 op, u8 update)
{
	if (bp->common.int_block == INT_BLOCK_HC)
		bnx2x_hc_ack_sb(bp, igu_sb_id, storm, index, op, update);
	else {
		u8 segment;

		if (CHIP_INT_MODE_IS_BC(bp))
			segment = storm;
		else if (igu_sb_id != bp->igu_dsb_id)
			segment = IGU_SEG_ACCESS_DEF;
		else if (storm == ATTENTION_ID)
			segment = IGU_SEG_ACCESS_ATTN;
		else
			segment = IGU_SEG_ACCESS_DEF;
		bnx2x_igu_ack_sb(bp, igu_sb_id, segment, index, op, update);
	}
}

static inline u16 bnx2x_hc_ack_int(struct bnx2x *bp)
{
	u32 hc_addr = (HC_REG_COMMAND_REG + BP_PORT(bp)*32 +
		       COMMAND_REG_SIMD_MASK);
	u32 result = REG_RD(bp, hc_addr);

	DP(BNX2X_MSG_OFF, "read 0x%08x from HC addr 0x%x\n",
	   result, hc_addr);

	barrier();
	return result;
}

static inline u16 bnx2x_igu_ack_int(struct bnx2x *bp)
{
	u32 igu_addr = (BAR_IGU_INTMEM + IGU_REG_SISR_MDPC_WMASK_LSB_UPPER*8);
	u32 result = REG_RD(bp, igu_addr);

	DP(NETIF_MSG_HW, "read 0x%08x from IGU addr 0x%x\n",
	   result, igu_addr);

	barrier();
	return result;
}

static inline u16 bnx2x_ack_int(struct bnx2x *bp)
{
	barrier();
	if (bp->common.int_block == INT_BLOCK_HC)
		return bnx2x_hc_ack_int(bp);
	else
		return bnx2x_igu_ack_int(bp);
}

static inline int bnx2x_has_tx_work_unload(struct bnx2x_fastpath *fp)
{
	/* Tell compiler that consumer and producer can change */
	barrier();
	return fp->tx_pkt_prod != fp->tx_pkt_cons;
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

/**
 * disables tx from stack point of view
 *
 * @param bp
 */
static inline void bnx2x_tx_disable(struct bnx2x *bp)
{
	netif_tx_disable(bp->dev);
	netif_carrier_off(bp->dev);
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
		       SGE_PAGE_SIZE*PAGES_PER_SGE, DMA_FROM_DEVICE);
	__free_pages(page, PAGES_PER_SGE_SHIFT);

	sw_buf->page = NULL;
	sge->addr_hi = 0;
	sge->addr_lo = 0;
}

static inline void bnx2x_add_all_napi(struct bnx2x *bp)
{
	int i;

	/* Add NAPI objects */
	for_each_napi_queue(bp, i)
		netif_napi_add(bp->dev, &bnx2x_fp(bp, i, napi),
			       bnx2x_poll, BNX2X_NAPI_WEIGHT);
}

static inline void bnx2x_del_all_napi(struct bnx2x *bp)
{
	int i;

	for_each_napi_queue(bp, i)
		netif_napi_del(&bnx2x_fp(bp, i, napi));
}

static inline void bnx2x_disable_msi(struct bnx2x *bp)
{
	if (bp->flags & USING_MSIX_FLAG) {
		pci_disable_msix(bp->pdev);
		bp->flags &= ~USING_MSIX_FLAG;
	} else if (bp->flags & USING_MSI_FLAG) {
		pci_disable_msi(bp->pdev);
		bp->flags &= ~USING_MSI_FLAG;
	}
}

static inline int bnx2x_calc_num_queues(struct bnx2x *bp)
{
	return  num_queues ?
		 min_t(int, num_queues, BNX2X_MAX_QUEUES(bp)) :
		 min_t(int, num_online_cpus(), BNX2X_MAX_QUEUES(bp));
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
				      u16 cons, u16 prod)
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

static inline void bnx2x_free_rx_sge_range(struct bnx2x *bp,
					   struct bnx2x_fastpath *fp, int last)
{
	int i;

	for (i = 0; i < last; i++)
		bnx2x_free_rx_sge(bp, fp, i);
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


static inline void bnx2x_init_tx_rings(struct bnx2x *bp)
{
	int i, j;

	for_each_tx_queue(bp, j) {
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

		SET_FLAG(fp->tx_db.data.header.header, DOORBELL_HDR_DB_TYPE, 1);
		fp->tx_db.data.zero_fill1 = 0;
		fp->tx_db.data.prod = 0;

		fp->tx_pkt_prod = 0;
		fp->tx_pkt_cons = 0;
		fp->tx_bd_prod = 0;
		fp->tx_bd_cons = 0;
		fp->tx_pkt = 0;
	}
}

static inline void bnx2x_set_next_page_rx_bd(struct bnx2x_fastpath *fp)
{
	int i;

	for (i = 1; i <= NUM_RX_RINGS; i++) {
		struct eth_rx_bd *rx_bd;

		rx_bd = &fp->rx_desc_ring[RX_DESC_CNT * i - 2];
		rx_bd->addr_hi =
			cpu_to_le32(U64_HI(fp->rx_desc_mapping +
				    BCM_PAGE_SIZE*(i % NUM_RX_RINGS)));
		rx_bd->addr_lo =
			cpu_to_le32(U64_LO(fp->rx_desc_mapping +
				    BCM_PAGE_SIZE*(i % NUM_RX_RINGS)));
	}
}

static inline void bnx2x_set_next_page_sgl(struct bnx2x_fastpath *fp)
{
	int i;

	for (i = 1; i <= NUM_RX_SGE_PAGES; i++) {
		struct eth_rx_sge *sge;

		sge = &fp->rx_sge_ring[RX_SGE_CNT * i - 2];
		sge->addr_hi =
			cpu_to_le32(U64_HI(fp->rx_sge_mapping +
			BCM_PAGE_SIZE*(i % NUM_RX_SGE_PAGES)));

		sge->addr_lo =
			cpu_to_le32(U64_LO(fp->rx_sge_mapping +
			BCM_PAGE_SIZE*(i % NUM_RX_SGE_PAGES)));
	}
}

static inline void bnx2x_set_next_page_rx_cq(struct bnx2x_fastpath *fp)
{
	int i;
	for (i = 1; i <= NUM_RCQ_RINGS; i++) {
		struct eth_rx_cqe_next_page *nextpg;

		nextpg = (struct eth_rx_cqe_next_page *)
			&fp->rx_comp_ring[RCQ_DESC_CNT * i - 1];
		nextpg->addr_hi =
			cpu_to_le32(U64_HI(fp->rx_comp_mapping +
				   BCM_PAGE_SIZE*(i % NUM_RCQ_RINGS)));
		nextpg->addr_lo =
			cpu_to_le32(U64_LO(fp->rx_comp_mapping +
				   BCM_PAGE_SIZE*(i % NUM_RCQ_RINGS)));
	}
}

#ifdef BCM_CNIC
static inline void bnx2x_init_fcoe_fp(struct bnx2x *bp)
{
	bnx2x_fcoe(bp, cl_id) = BNX2X_FCOE_ETH_CL_ID +
		BP_E1HVN(bp) * NONE_ETH_CONTEXT_USE;
	bnx2x_fcoe(bp, cid) = BNX2X_FCOE_ETH_CID;
	bnx2x_fcoe(bp, fw_sb_id) = DEF_SB_ID;
	bnx2x_fcoe(bp, igu_sb_id) = bp->igu_dsb_id;
	bnx2x_fcoe(bp, bp) = bp;
	bnx2x_fcoe(bp, state) = BNX2X_FP_STATE_CLOSED;
	bnx2x_fcoe(bp, index) = FCOE_IDX;
	bnx2x_fcoe(bp, rx_cons_sb) = BNX2X_FCOE_L2_RX_INDEX;
	bnx2x_fcoe(bp, tx_cons_sb) = BNX2X_FCOE_L2_TX_INDEX;
	/* qZone id equals to FW (per path) client id */
	bnx2x_fcoe(bp, cl_qzone_id) = bnx2x_fcoe(bp, cl_id) +
		BP_PORT(bp)*(CHIP_IS_E2(bp) ? ETH_MAX_RX_CLIENTS_E2 :
				ETH_MAX_RX_CLIENTS_E1H);
	/* init shortcut */
	bnx2x_fcoe(bp, ustorm_rx_prods_offset) = CHIP_IS_E2(bp) ?
	    USTORM_RX_PRODS_E2_OFFSET(bnx2x_fcoe(bp, cl_qzone_id)) :
	    USTORM_RX_PRODS_E1X_OFFSET(BP_PORT(bp), bnx2x_fcoe_fp(bp)->cl_id);

}
#endif

static inline void __storm_memset_struct(struct bnx2x *bp,
					 u32 addr, size_t size, u32 *data)
{
	int i;
	for (i = 0; i < size/4; i++)
		REG_WR(bp, addr + (i * 4), data[i]);
}

static inline void storm_memset_mac_filters(struct bnx2x *bp,
			struct tstorm_eth_mac_filter_config *mac_filters,
			u16 abs_fid)
{
	size_t size = sizeof(struct tstorm_eth_mac_filter_config);

	u32 addr = BAR_TSTRORM_INTMEM +
			TSTORM_MAC_FILTER_CONFIG_OFFSET(abs_fid);

	__storm_memset_struct(bp, addr, size, (u32 *)mac_filters);
}

static inline void storm_memset_cmng(struct bnx2x *bp,
				struct cmng_struct_per_port *cmng,
				u8 port)
{
	size_t size = sizeof(struct cmng_struct_per_port);

	u32 addr = BAR_XSTRORM_INTMEM +
			XSTORM_CMNG_PER_PORT_VARS_OFFSET(port);

	__storm_memset_struct(bp, addr, size, (u32 *)cmng);
}

/* HW Lock for shared dual port PHYs */
void bnx2x_acquire_phy_lock(struct bnx2x *bp);
void bnx2x_release_phy_lock(struct bnx2x *bp);

/**
 * Extracts MAX BW part from MF configuration.
 *
 * @param bp
 * @param mf_cfg
 *
 * @return u16
 */
static inline u16 bnx2x_extract_max_cfg(struct bnx2x *bp, u32 mf_cfg)
{
	u16 max_cfg = (mf_cfg & FUNC_MF_CFG_MAX_BW_MASK) >>
			      FUNC_MF_CFG_MAX_BW_SHIFT;
	if (!max_cfg) {
		BNX2X_ERR("Illegal configuration detected for Max BW - "
			  "using 100 instead\n");
		max_cfg = 100;
	}
	return max_cfg;
}

#endif /* BNX2X_CMN_H */
