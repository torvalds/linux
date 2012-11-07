/* bnx2x_cmn.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2012 Broadcom Corporation
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
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>


#include "bnx2x.h"

/* This is used as a replacement for an MCP if it's not present */
extern int load_count[2][3]; /* per-path: 0-common, 1-port0, 2-port1 */

extern int num_queues;
extern int int_mode;

/************************ Macros ********************************/
#define BNX2X_PCI_FREE(x, y, size) \
	do { \
		if (x) { \
			dma_free_coherent(&bp->pdev->dev, size, (void *)x, y); \
			x = NULL; \
			y = 0; \
		} \
	} while (0)

#define BNX2X_FREE(x) \
	do { \
		if (x) { \
			kfree((void *)x); \
			x = NULL; \
		} \
	} while (0)

#define BNX2X_PCI_ALLOC(x, y, size) \
	do { \
		x = dma_alloc_coherent(&bp->pdev->dev, size, y, GFP_KERNEL); \
		if (x == NULL) \
			goto alloc_mem_err; \
		memset((void *)x, 0, size); \
	} while (0)

#define BNX2X_ALLOC(x, size) \
	do { \
		x = kzalloc(size, GFP_KERNEL); \
		if (x == NULL) \
			goto alloc_mem_err; \
	} while (0)

/*********************** Interfaces ****************************
 *  Functions that need to be implemented by each driver version
 */
/* Init */

/**
 * bnx2x_send_unload_req - request unload mode from the MCP.
 *
 * @bp:			driver handle
 * @unload_mode:	requested function's unload mode
 *
 * Return unload mode returned by the MCP: COMMON, PORT or FUNC.
 */
u32 bnx2x_send_unload_req(struct bnx2x *bp, int unload_mode);

/**
 * bnx2x_send_unload_done - send UNLOAD_DONE command to the MCP.
 *
 * @bp:		driver handle
 * @keep_link:		true iff link should be kept up
 */
void bnx2x_send_unload_done(struct bnx2x *bp, bool keep_link);

/**
 * bnx2x_config_rss_pf - configure RSS parameters in a PF.
 *
 * @bp:			driver handle
 * @rss_obj:		RSS object to use
 * @ind_table:		indirection table to configure
 * @config_hash:	re-configure RSS hash keys configuration
 */
int bnx2x_config_rss_pf(struct bnx2x *bp, struct bnx2x_rss_config_obj *rss_obj,
			bool config_hash);

/**
 * bnx2x__init_func_obj - init function object
 *
 * @bp:			driver handle
 *
 * Initializes the Function Object with the appropriate
 * parameters which include a function slow path driver
 * interface.
 */
void bnx2x__init_func_obj(struct bnx2x *bp);

/**
 * bnx2x_setup_queue - setup eth queue.
 *
 * @bp:		driver handle
 * @fp:		pointer to the fastpath structure
 * @leading:	boolean
 *
 */
int bnx2x_setup_queue(struct bnx2x *bp, struct bnx2x_fastpath *fp,
		       bool leading);

/**
 * bnx2x_setup_leading - bring up a leading eth queue.
 *
 * @bp:		driver handle
 */
int bnx2x_setup_leading(struct bnx2x *bp);

/**
 * bnx2x_fw_command - send the MCP a request
 *
 * @bp:		driver handle
 * @command:	request
 * @param:	request's parameter
 *
 * block until there is a reply
 */
u32 bnx2x_fw_command(struct bnx2x *bp, u32 command, u32 param);

/**
 * bnx2x_initial_phy_init - initialize link parameters structure variables.
 *
 * @bp:		driver handle
 * @load_mode:	current mode
 */
u8 bnx2x_initial_phy_init(struct bnx2x *bp, int load_mode);

/**
 * bnx2x_link_set - configure hw according to link parameters structure.
 *
 * @bp:		driver handle
 */
void bnx2x_link_set(struct bnx2x *bp);

/**
 * bnx2x_force_link_reset - Forces link reset, and put the PHY
 * in reset as well.
 *
 * @bp:		driver handle
 */
void bnx2x_force_link_reset(struct bnx2x *bp);

/**
 * bnx2x_link_test - query link status.
 *
 * @bp:		driver handle
 * @is_serdes:	bool
 *
 * Returns 0 if link is UP.
 */
u8 bnx2x_link_test(struct bnx2x *bp, u8 is_serdes);

/**
 * bnx2x_drv_pulse - write driver pulse to shmem
 *
 * @bp:		driver handle
 *
 * writes the value in bp->fw_drv_pulse_wr_seq to drv_pulse mbox
 * in the shmem.
 */
void bnx2x_drv_pulse(struct bnx2x *bp);

/**
 * bnx2x_igu_ack_sb - update IGU with current SB value
 *
 * @bp:		driver handle
 * @igu_sb_id:	SB id
 * @segment:	SB segment
 * @index:	SB index
 * @op:		SB operation
 * @update:	is HW update required
 */
void bnx2x_igu_ack_sb(struct bnx2x *bp, u8 igu_sb_id, u8 segment,
		      u16 index, u8 op, u8 update);

/* Disable transactions from chip to host */
void bnx2x_pf_disable(struct bnx2x *bp);

/**
 * bnx2x__link_status_update - handles link status change.
 *
 * @bp:		driver handle
 */
void bnx2x__link_status_update(struct bnx2x *bp);

/**
 * bnx2x_link_report - report link status to upper layer.
 *
 * @bp:		driver handle
 */
void bnx2x_link_report(struct bnx2x *bp);

/* None-atomic version of bnx2x_link_report() */
void __bnx2x_link_report(struct bnx2x *bp);

/**
 * bnx2x_get_mf_speed - calculate MF speed.
 *
 * @bp:		driver handle
 *
 * Takes into account current linespeed and MF configuration.
 */
u16 bnx2x_get_mf_speed(struct bnx2x *bp);

/**
 * bnx2x_msix_sp_int - MSI-X slowpath interrupt handler
 *
 * @irq:		irq number
 * @dev_instance:	private instance
 */
irqreturn_t bnx2x_msix_sp_int(int irq, void *dev_instance);

/**
 * bnx2x_interrupt - non MSI-X interrupt handler
 *
 * @irq:		irq number
 * @dev_instance:	private instance
 */
irqreturn_t bnx2x_interrupt(int irq, void *dev_instance);

/**
 * bnx2x_cnic_notify - send command to cnic driver
 *
 * @bp:		driver handle
 * @cmd:	command
 */
int bnx2x_cnic_notify(struct bnx2x *bp, int cmd);

/**
 * bnx2x_setup_cnic_irq_info - provides cnic with IRQ information
 *
 * @bp:		driver handle
 */
void bnx2x_setup_cnic_irq_info(struct bnx2x *bp);

/**
 * bnx2x_setup_cnic_info - provides cnic with updated info
 *
 * @bp:		driver handle
 */
void bnx2x_setup_cnic_info(struct bnx2x *bp);

/**
 * bnx2x_int_enable - enable HW interrupts.
 *
 * @bp:		driver handle
 */
void bnx2x_int_enable(struct bnx2x *bp);

/**
 * bnx2x_int_disable_sync - disable interrupts.
 *
 * @bp:		driver handle
 * @disable_hw:	true, disable HW interrupts.
 *
 * This function ensures that there are no
 * ISRs or SP DPCs (sp_task) are running after it returns.
 */
void bnx2x_int_disable_sync(struct bnx2x *bp, int disable_hw);

/**
 * bnx2x_nic_init_cnic - init driver internals for cnic.
 *
 * @bp:		driver handle
 * @load_code:	COMMON, PORT or FUNCTION
 *
 * Initializes:
 *  - rings
 *  - status blocks
 *  - etc.
 */
void bnx2x_nic_init_cnic(struct bnx2x *bp);

/**
 * bnx2x_nic_init - init driver internals.
 *
 * @bp:		driver handle
 *
 * Initializes:
 *  - rings
 *  - status blocks
 *  - etc.
 */
void bnx2x_nic_init(struct bnx2x *bp, u32 load_code);
/**
 * bnx2x_alloc_mem_cnic - allocate driver's memory for cnic.
 *
 * @bp:		driver handle
 */
int bnx2x_alloc_mem_cnic(struct bnx2x *bp);
/**
 * bnx2x_alloc_mem - allocate driver's memory.
 *
 * @bp:		driver handle
 */
int bnx2x_alloc_mem(struct bnx2x *bp);

/**
 * bnx2x_free_mem_cnic - release driver's memory for cnic.
 *
 * @bp:		driver handle
 */
void bnx2x_free_mem_cnic(struct bnx2x *bp);
/**
 * bnx2x_free_mem - release driver's memory.
 *
 * @bp:		driver handle
 */
void bnx2x_free_mem(struct bnx2x *bp);

/**
 * bnx2x_set_num_queues - set number of queues according to mode.
 *
 * @bp:		driver handle
 */
void bnx2x_set_num_queues(struct bnx2x *bp);

/**
 * bnx2x_chip_cleanup - cleanup chip internals.
 *
 * @bp:			driver handle
 * @unload_mode:	COMMON, PORT, FUNCTION
 * @keep_link:		true iff link should be kept up.
 *
 * - Cleanup MAC configuration.
 * - Closes clients.
 * - etc.
 */
void bnx2x_chip_cleanup(struct bnx2x *bp, int unload_mode, bool keep_link);

/**
 * bnx2x_acquire_hw_lock - acquire HW lock.
 *
 * @bp:		driver handle
 * @resource:	resource bit which was locked
 */
int bnx2x_acquire_hw_lock(struct bnx2x *bp, u32 resource);

/**
 * bnx2x_release_hw_lock - release HW lock.
 *
 * @bp:		driver handle
 * @resource:	resource bit which was locked
 */
int bnx2x_release_hw_lock(struct bnx2x *bp, u32 resource);

/**
 * bnx2x_release_leader_lock - release recovery leader lock
 *
 * @bp:		driver handle
 */
int bnx2x_release_leader_lock(struct bnx2x *bp);

/**
 * bnx2x_set_eth_mac - configure eth MAC address in the HW
 *
 * @bp:		driver handle
 * @set:	set or clear
 *
 * Configures according to the value in netdev->dev_addr.
 */
int bnx2x_set_eth_mac(struct bnx2x *bp, bool set);

/**
 * bnx2x_set_rx_mode - set MAC filtering configurations.
 *
 * @dev:	netdevice
 *
 * called with netif_tx_lock from dev_mcast.c
 * If bp->state is OPEN, should be called with
 * netif_addr_lock_bh()
 */
void bnx2x_set_rx_mode(struct net_device *dev);

/**
 * bnx2x_set_storm_rx_mode - configure MAC filtering rules in a FW.
 *
 * @bp:		driver handle
 *
 * If bp->state is OPEN, should be called with
 * netif_addr_lock_bh().
 */
void bnx2x_set_storm_rx_mode(struct bnx2x *bp);

/**
 * bnx2x_set_q_rx_mode - configures rx_mode for a single queue.
 *
 * @bp:			driver handle
 * @cl_id:		client id
 * @rx_mode_flags:	rx mode configuration
 * @rx_accept_flags:	rx accept configuration
 * @tx_accept_flags:	tx accept configuration (tx switch)
 * @ramrod_flags:	ramrod configuration
 */
void bnx2x_set_q_rx_mode(struct bnx2x *bp, u8 cl_id,
			 unsigned long rx_mode_flags,
			 unsigned long rx_accept_flags,
			 unsigned long tx_accept_flags,
			 unsigned long ramrod_flags);

/* Parity errors related */
void bnx2x_set_pf_load(struct bnx2x *bp);
bool bnx2x_clear_pf_load(struct bnx2x *bp);
bool bnx2x_chk_parity_attn(struct bnx2x *bp, bool *global, bool print);
bool bnx2x_reset_is_done(struct bnx2x *bp, int engine);
void bnx2x_set_reset_in_progress(struct bnx2x *bp);
void bnx2x_set_reset_global(struct bnx2x *bp);
void bnx2x_disable_close_the_gate(struct bnx2x *bp);
int bnx2x_init_hw_func_cnic(struct bnx2x *bp);

/**
 * bnx2x_sp_event - handle ramrods completion.
 *
 * @fp:		fastpath handle for the event
 * @rr_cqe:	eth_rx_cqe
 */
void bnx2x_sp_event(struct bnx2x_fastpath *fp, union eth_rx_cqe *rr_cqe);

/**
 * bnx2x_ilt_set_info - prepare ILT configurations.
 *
 * @bp:		driver handle
 */
void bnx2x_ilt_set_info(struct bnx2x *bp);

/**
 * bnx2x_ilt_set_cnic_info - prepare ILT configurations for SRC
 * and TM.
 *
 * @bp:		driver handle
 */
void bnx2x_ilt_set_info_cnic(struct bnx2x *bp);

/**
 * bnx2x_dcbx_init - initialize dcbx protocol.
 *
 * @bp:		driver handle
 */
void bnx2x_dcbx_init(struct bnx2x *bp, bool update_shmem);

/**
 * bnx2x_set_power_state - set power state to the requested value.
 *
 * @bp:		driver handle
 * @state:	required state D0 or D3hot
 *
 * Currently only D0 and D3hot are supported.
 */
int bnx2x_set_power_state(struct bnx2x *bp, pci_power_t state);

/**
 * bnx2x_update_max_mf_config - update MAX part of MF configuration in HW.
 *
 * @bp:		driver handle
 * @value:	new value
 */
void bnx2x_update_max_mf_config(struct bnx2x *bp, u32 value);
/* Error handling */
void bnx2x_panic_dump(struct bnx2x *bp);

void bnx2x_fw_dump_lvl(struct bnx2x *bp, const char *lvl);

/* validate currect fw is loaded */
bool bnx2x_test_firmware_version(struct bnx2x *bp, bool is_err);

/* dev_close main block */
int bnx2x_nic_unload(struct bnx2x *bp, int unload_mode, bool keep_link);

/* dev_open main block */
int bnx2x_nic_load(struct bnx2x *bp, int load_mode);

/* hard_xmit callback */
netdev_tx_t bnx2x_start_xmit(struct sk_buff *skb, struct net_device *dev);

/* setup_tc callback */
int bnx2x_setup_tc(struct net_device *dev, u8 num_tc);

/* select_queue callback */
u16 bnx2x_select_queue(struct net_device *dev, struct sk_buff *skb);

/* reload helper */
int bnx2x_reload_if_running(struct net_device *dev);

int bnx2x_change_mac_addr(struct net_device *dev, void *p);

/* NAPI poll Rx part */
int bnx2x_rx_int(struct bnx2x_fastpath *fp, int budget);

void bnx2x_update_rx_prod(struct bnx2x *bp, struct bnx2x_fastpath *fp,
			u16 bd_prod, u16 rx_comp_prod, u16 rx_sge_prod);

/* NAPI poll Tx part */
int bnx2x_tx_int(struct bnx2x *bp, struct bnx2x_fp_txdata *txdata);

/* suspend/resume callbacks */
int bnx2x_suspend(struct pci_dev *pdev, pm_message_t state);
int bnx2x_resume(struct pci_dev *pdev);

/* Release IRQ vectors */
void bnx2x_free_irq(struct bnx2x *bp);

void bnx2x_free_fp_mem_cnic(struct bnx2x *bp);
void bnx2x_free_fp_mem(struct bnx2x *bp);
int bnx2x_alloc_fp_mem_cnic(struct bnx2x *bp);
int bnx2x_alloc_fp_mem(struct bnx2x *bp);
void bnx2x_init_rx_rings(struct bnx2x *bp);
void bnx2x_init_rx_rings_cnic(struct bnx2x *bp);
void bnx2x_free_skbs_cnic(struct bnx2x *bp);
void bnx2x_free_skbs(struct bnx2x *bp);
void bnx2x_netif_stop(struct bnx2x *bp, int disable_hw);
void bnx2x_netif_start(struct bnx2x *bp);
int bnx2x_load_cnic(struct bnx2x *bp);

/**
 * bnx2x_enable_msix - set msix configuration.
 *
 * @bp:		driver handle
 *
 * fills msix_table, requests vectors, updates num_queues
 * according to number of available vectors.
 */
int bnx2x_enable_msix(struct bnx2x *bp);

/**
 * bnx2x_enable_msi - request msi mode from OS, updated internals accordingly
 *
 * @bp:		driver handle
 */
int bnx2x_enable_msi(struct bnx2x *bp);

/**
 * bnx2x_poll - NAPI callback
 *
 * @napi:	napi structure
 * @budget:
 *
 */
int bnx2x_poll(struct napi_struct *napi, int budget);

/**
 * bnx2x_alloc_mem_bp - allocate memories outsize main driver structure
 *
 * @bp:		driver handle
 */
int __devinit bnx2x_alloc_mem_bp(struct bnx2x *bp);

/**
 * bnx2x_free_mem_bp - release memories outsize main driver structure
 *
 * @bp:		driver handle
 */
void bnx2x_free_mem_bp(struct bnx2x *bp);

/**
 * bnx2x_change_mtu - change mtu netdev callback
 *
 * @dev:	net device
 * @new_mtu:	requested mtu
 *
 */
int bnx2x_change_mtu(struct net_device *dev, int new_mtu);

#ifdef NETDEV_FCOE_WWNN
/**
 * bnx2x_fcoe_get_wwn - return the requested WWN value for this port
 *
 * @dev:	net_device
 * @wwn:	output buffer
 * @type:	WWN type: NETDEV_FCOE_WWNN (node) or NETDEV_FCOE_WWPN (port)
 *
 */
int bnx2x_fcoe_get_wwn(struct net_device *dev, u64 *wwn, int type);
#endif

netdev_features_t bnx2x_fix_features(struct net_device *dev,
				     netdev_features_t features);
int bnx2x_set_features(struct net_device *dev, netdev_features_t features);

/**
 * bnx2x_tx_timeout - tx timeout netdev callback
 *
 * @dev:	net device
 */
void bnx2x_tx_timeout(struct net_device *dev);

/*********************** Inlines **********************************/
/*********************** Fast path ********************************/
static inline void bnx2x_update_fpsb_idx(struct bnx2x_fastpath *fp)
{
	barrier(); /* status block is written to by the chip */
	fp->fp_hc_idx = fp->sb_running_index[SM_RX_ID];
}

static inline void bnx2x_update_rx_prod_gen(struct bnx2x *bp,
			struct bnx2x_fastpath *fp, u16 bd_prod,
			u16 rx_comp_prod, u16 rx_sge_prod, u32 start)
{
	struct ustorm_eth_rx_producers rx_prods = {0};
	u32 i;

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

	for (i = 0; i < sizeof(rx_prods)/4; i++)
		REG_WR(bp, start + i*4, ((u32 *)&rx_prods)[i]);

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

	DP(NETIF_MSG_INTR, "write 0x%08x to IGU addr 0x%x\n",
	   cmd_data.sb_id_and_flags, igu_addr);
	REG_WR(bp, igu_addr, cmd_data.sb_id_and_flags);

	/* Make sure that ACK is written */
	mmiowb();
	barrier();
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

	REG_WR(bp, hc_addr, (*(u32 *)&igu_ack));

	/* Make sure that ACK is written */
	mmiowb();
	barrier();
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

	barrier();
	return result;
}

static inline u16 bnx2x_igu_ack_int(struct bnx2x *bp)
{
	u32 igu_addr = (BAR_IGU_INTMEM + IGU_REG_SISR_MDPC_WMASK_LSB_UPPER*8);
	u32 result = REG_RD(bp, igu_addr);

	DP(NETIF_MSG_INTR, "read 0x%08x from IGU addr 0x%x\n",
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

static inline int bnx2x_has_tx_work_unload(struct bnx2x_fp_txdata *txdata)
{
	/* Tell compiler that consumer and producer can change */
	barrier();
	return txdata->tx_pkt_prod != txdata->tx_pkt_cons;
}

static inline u16 bnx2x_tx_avail(struct bnx2x *bp,
				 struct bnx2x_fp_txdata *txdata)
{
	s16 used;
	u16 prod;
	u16 cons;

	prod = txdata->tx_bd_prod;
	cons = txdata->tx_bd_cons;

	used = SUB_S16(prod, cons);

#ifdef BNX2X_STOP_ON_ERROR
	WARN_ON(used < 0);
	WARN_ON(used > txdata->tx_ring_size);
	WARN_ON((txdata->tx_ring_size - used) > MAX_TX_AVAIL);
#endif

	return (s16)(txdata->tx_ring_size) - used;
}

static inline int bnx2x_tx_queue_has_work(struct bnx2x_fp_txdata *txdata)
{
	u16 hw_cons;

	/* Tell compiler that status block fields can change */
	barrier();
	hw_cons = le16_to_cpu(*txdata->tx_cons_sb);
	return hw_cons != txdata->tx_pkt_cons;
}

static inline bool bnx2x_has_tx_work(struct bnx2x_fastpath *fp)
{
	u8 cos;
	for_each_cos_in_tx_queue(fp, cos)
		if (bnx2x_tx_queue_has_work(fp->txdata_ptr[cos]))
			return true;
	return false;
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
 * bnx2x_tx_disable - disables tx from stack point of view
 *
 * @bp:		driver handle
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

static inline void bnx2x_add_all_napi_cnic(struct bnx2x *bp)
{
	int i;

	/* Add NAPI objects */
	for_each_rx_queue_cnic(bp, i)
		netif_napi_add(bp->dev, &bnx2x_fp(bp, i, napi),
			       bnx2x_poll, BNX2X_NAPI_WEIGHT);
}

static inline void bnx2x_add_all_napi(struct bnx2x *bp)
{
	int i;

	/* Add NAPI objects */
	for_each_eth_queue(bp, i)
		netif_napi_add(bp->dev, &bnx2x_fp(bp, i, napi),
			       bnx2x_poll, BNX2X_NAPI_WEIGHT);
}

static inline void bnx2x_del_all_napi_cnic(struct bnx2x *bp)
{
	int i;

	for_each_rx_queue_cnic(bp, i)
		netif_napi_del(&bnx2x_fp(bp, i, napi));
}

static inline void bnx2x_del_all_napi(struct bnx2x *bp)
{
	int i;

	for_each_eth_queue(bp, i)
		netif_napi_del(&bnx2x_fp(bp, i, napi));
}

void bnx2x_set_int_mode(struct bnx2x *bp);

static inline void bnx2x_disable_msi(struct bnx2x *bp)
{
	if (bp->flags & USING_MSIX_FLAG) {
		pci_disable_msix(bp->pdev);
		bp->flags &= ~(USING_MSIX_FLAG | USING_SINGLE_MSIX_FLAG);
	} else if (bp->flags & USING_MSI_FLAG) {
		pci_disable_msi(bp->pdev);
		bp->flags &= ~USING_MSI_FLAG;
	}
}

static inline int bnx2x_calc_num_queues(struct bnx2x *bp)
{
	return  num_queues ?
		 min_t(int, num_queues, BNX2X_MAX_QUEUES(bp)) :
		 min_t(int, netif_get_num_default_rss_queues(),
		       BNX2X_MAX_QUEUES(bp));
}

static inline void bnx2x_clear_sge_mask_next_elems(struct bnx2x_fastpath *fp)
{
	int i, j;

	for (i = 1; i <= NUM_RX_SGE_PAGES; i++) {
		int idx = RX_SGE_CNT * i - 1;

		for (j = 0; j < 2; j++) {
			BIT_VEC64_CLEAR_BIT(fp->sge_mask, idx);
			idx--;
		}
	}
}

static inline void bnx2x_init_sge_ring_bit_mask(struct bnx2x_fastpath *fp)
{
	/* Set the mask to all 1-s: it's faster to compare to 0 than to 0xf-s */
	memset(fp->sge_mask, 0xff, sizeof(fp->sge_mask));

	/* Clear the two last indices in the page to 1:
	   these are the indices that correspond to the "next" element,
	   hence will never be indicated and should be removed from
	   the calculations. */
	bnx2x_clear_sge_mask_next_elems(fp);
}

/* note that we are not allocating a new buffer,
 * we are just moving one from cons to prod
 * we are not creating a new mapping,
 * so there is no need to check for dma_mapping_error().
 */
static inline void bnx2x_reuse_rx_data(struct bnx2x_fastpath *fp,
				      u16 cons, u16 prod)
{
	struct sw_rx_bd *cons_rx_buf = &fp->rx_buf_ring[cons];
	struct sw_rx_bd *prod_rx_buf = &fp->rx_buf_ring[prod];
	struct eth_rx_bd *cons_bd = &fp->rx_desc_ring[cons];
	struct eth_rx_bd *prod_bd = &fp->rx_desc_ring[prod];

	dma_unmap_addr_set(prod_rx_buf, mapping,
			   dma_unmap_addr(cons_rx_buf, mapping));
	prod_rx_buf->data = cons_rx_buf->data;
	*prod_bd = *cons_bd;
}

/************************* Init ******************************************/

/* returns func by VN for current port */
static inline int func_by_vn(struct bnx2x *bp, int vn)
{
	return 2 * vn + BP_PORT(bp);
}

static inline int bnx2x_config_rss_eth(struct bnx2x *bp, bool config_hash)
{
	return bnx2x_config_rss_pf(bp, &bp->rss_conf_obj, config_hash);
}

/**
 * bnx2x_func_start - init function
 *
 * @bp:		driver handle
 *
 * Must be called before sending CLIENT_SETUP for the first client.
 */
static inline int bnx2x_func_start(struct bnx2x *bp)
{
	struct bnx2x_func_state_params func_params = {NULL};
	struct bnx2x_func_start_params *start_params =
		&func_params.params.start;

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_START;

	/* Function parameters */
	start_params->mf_mode = bp->mf_mode;
	start_params->sd_vlan_tag = bp->mf_ov;

	if (CHIP_IS_E2(bp) || CHIP_IS_E3(bp))
		start_params->network_cos_mode = STATIC_COS;
	else /* CHIP_IS_E1X */
		start_params->network_cos_mode = FW_WRR;

	return bnx2x_func_state_change(bp, &func_params);
}


/**
 * bnx2x_set_fw_mac_addr - fill in a MAC address in FW format
 *
 * @fw_hi:	pointer to upper part
 * @fw_mid:	pointer to middle part
 * @fw_lo:	pointer to lower part
 * @mac:	pointer to MAC address
 */
static inline void bnx2x_set_fw_mac_addr(u16 *fw_hi, u16 *fw_mid, u16 *fw_lo,
					 u8 *mac)
{
	((u8 *)fw_hi)[0]  = mac[1];
	((u8 *)fw_hi)[1]  = mac[0];
	((u8 *)fw_mid)[0] = mac[3];
	((u8 *)fw_mid)[1] = mac[2];
	((u8 *)fw_lo)[0]  = mac[5];
	((u8 *)fw_lo)[1]  = mac[4];
}

static inline void bnx2x_free_rx_sge_range(struct bnx2x *bp,
					   struct bnx2x_fastpath *fp, int last)
{
	int i;

	if (fp->disable_tpa)
		return;

	for (i = 0; i < last; i++)
		bnx2x_free_rx_sge(bp, fp, i);
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

/* Statistics ID are global per chip/path, while Client IDs for E1x are per
 * port.
 */
static inline u8 bnx2x_stats_id(struct bnx2x_fastpath *fp)
{
	struct bnx2x *bp = fp->bp;
	if (!CHIP_IS_E1x(bp)) {
		/* there are special statistics counters for FCoE 136..140 */
		if (IS_FCOE_FP(fp))
			return bp->cnic_base_cl_id + (bp->pf_num >> 1);
		return fp->cl_id;
	}
	return fp->cl_id + BP_PORT(bp) * FP_SB_MAX_E1x;
}

static inline void bnx2x_init_vlan_mac_fp_objs(struct bnx2x_fastpath *fp,
					       bnx2x_obj_type obj_type)
{
	struct bnx2x *bp = fp->bp;

	/* Configure classification DBs */
	bnx2x_init_mac_obj(bp, &bnx2x_sp_obj(bp, fp).mac_obj, fp->cl_id,
			   fp->cid, BP_FUNC(bp), bnx2x_sp(bp, mac_rdata),
			   bnx2x_sp_mapping(bp, mac_rdata),
			   BNX2X_FILTER_MAC_PENDING,
			   &bp->sp_state, obj_type,
			   &bp->macs_pool);
}

/**
 * bnx2x_get_path_func_num - get number of active functions
 *
 * @bp:		driver handle
 *
 * Calculates the number of active (not hidden) functions on the
 * current path.
 */
static inline u8 bnx2x_get_path_func_num(struct bnx2x *bp)
{
	u8 func_num = 0, i;

	/* 57710 has only one function per-port */
	if (CHIP_IS_E1(bp))
		return 1;

	/* Calculate a number of functions enabled on the current
	 * PATH/PORT.
	 */
	if (CHIP_REV_IS_SLOW(bp)) {
		if (IS_MF(bp))
			func_num = 4;
		else
			func_num = 2;
	} else {
		for (i = 0; i < E1H_FUNC_MAX / 2; i++) {
			u32 func_config =
				MF_CFG_RD(bp,
					  func_mf_config[BP_PORT(bp) + 2 * i].
					  config);
			func_num +=
				((func_config & FUNC_MF_CFG_FUNC_HIDE) ? 0 : 1);
		}
	}

	WARN_ON(!func_num);

	return func_num;
}

static inline void bnx2x_init_bp_objs(struct bnx2x *bp)
{
	/* RX_MODE controlling object */
	bnx2x_init_rx_mode_obj(bp, &bp->rx_mode_obj);

	/* multicast configuration controlling object */
	bnx2x_init_mcast_obj(bp, &bp->mcast_obj, bp->fp->cl_id, bp->fp->cid,
			     BP_FUNC(bp), BP_FUNC(bp),
			     bnx2x_sp(bp, mcast_rdata),
			     bnx2x_sp_mapping(bp, mcast_rdata),
			     BNX2X_FILTER_MCAST_PENDING, &bp->sp_state,
			     BNX2X_OBJ_TYPE_RX);

	/* Setup CAM credit pools */
	bnx2x_init_mac_credit_pool(bp, &bp->macs_pool, BP_FUNC(bp),
				   bnx2x_get_path_func_num(bp));

	/* RSS configuration object */
	bnx2x_init_rss_config_obj(bp, &bp->rss_conf_obj, bp->fp->cl_id,
				  bp->fp->cid, BP_FUNC(bp), BP_FUNC(bp),
				  bnx2x_sp(bp, rss_rdata),
				  bnx2x_sp_mapping(bp, rss_rdata),
				  BNX2X_FILTER_RSS_CONF_PENDING, &bp->sp_state,
				  BNX2X_OBJ_TYPE_RX);
}

static inline u8 bnx2x_fp_qzone_id(struct bnx2x_fastpath *fp)
{
	if (CHIP_IS_E1x(fp->bp))
		return fp->cl_id + BP_PORT(fp->bp) * ETH_MAX_RX_CLIENTS_E1H;
	else
		return fp->cl_id;
}

static inline u32 bnx2x_rx_ustorm_prods_offset(struct bnx2x_fastpath *fp)
{
	struct bnx2x *bp = fp->bp;

	if (!CHIP_IS_E1x(bp))
		return USTORM_RX_PRODS_E2_OFFSET(fp->cl_qzone_id);
	else
		return USTORM_RX_PRODS_E1X_OFFSET(BP_PORT(bp), fp->cl_id);
}

static inline void bnx2x_init_txdata(struct bnx2x *bp,
				     struct bnx2x_fp_txdata *txdata, u32 cid,
				     int txq_index, __le16 *tx_cons_sb,
				     struct bnx2x_fastpath *fp)
{
	txdata->cid = cid;
	txdata->txq_index = txq_index;
	txdata->tx_cons_sb = tx_cons_sb;
	txdata->parent_fp = fp;
	txdata->tx_ring_size = IS_FCOE_FP(fp) ? MAX_TX_AVAIL : bp->tx_ring_size;

	DP(NETIF_MSG_IFUP, "created tx data cid %d, txq %d\n",
	   txdata->cid, txdata->txq_index);
}

static inline u8 bnx2x_cnic_eth_cl_id(struct bnx2x *bp, u8 cl_idx)
{
	return bp->cnic_base_cl_id + cl_idx +
		(bp->pf_num >> 1) * BNX2X_MAX_CNIC_ETH_CL_ID_IDX;
}

static inline u8 bnx2x_cnic_fw_sb_id(struct bnx2x *bp)
{

	/* the 'first' id is allocated for the cnic */
	return bp->base_fw_ndsb;
}

static inline u8 bnx2x_cnic_igu_sb_id(struct bnx2x *bp)
{
	return bp->igu_base_sb;
}


static inline void bnx2x_init_fcoe_fp(struct bnx2x *bp)
{
	struct bnx2x_fastpath *fp = bnx2x_fcoe_fp(bp);
	unsigned long q_type = 0;

	bnx2x_fcoe(bp, rx_queue) = BNX2X_NUM_ETH_QUEUES(bp);
	bnx2x_fcoe(bp, cl_id) = bnx2x_cnic_eth_cl_id(bp,
						     BNX2X_FCOE_ETH_CL_ID_IDX);
	bnx2x_fcoe(bp, cid) = BNX2X_FCOE_ETH_CID(bp);
	bnx2x_fcoe(bp, fw_sb_id) = DEF_SB_ID;
	bnx2x_fcoe(bp, igu_sb_id) = bp->igu_dsb_id;
	bnx2x_fcoe(bp, rx_cons_sb) = BNX2X_FCOE_L2_RX_INDEX;
	bnx2x_init_txdata(bp, bnx2x_fcoe(bp, txdata_ptr[0]),
			  fp->cid, FCOE_TXQ_IDX(bp), BNX2X_FCOE_L2_TX_INDEX,
			  fp);

	DP(NETIF_MSG_IFUP, "created fcoe tx data (fp index %d)\n", fp->index);

	/* qZone id equals to FW (per path) client id */
	bnx2x_fcoe(bp, cl_qzone_id) = bnx2x_fp_qzone_id(fp);
	/* init shortcut */
	bnx2x_fcoe(bp, ustorm_rx_prods_offset) =
		bnx2x_rx_ustorm_prods_offset(fp);

	/* Configure Queue State object */
	__set_bit(BNX2X_Q_TYPE_HAS_RX, &q_type);
	__set_bit(BNX2X_Q_TYPE_HAS_TX, &q_type);

	/* No multi-CoS for FCoE L2 client */
	BUG_ON(fp->max_cos != 1);

	bnx2x_init_queue_obj(bp, &bnx2x_sp_obj(bp, fp).q_obj, fp->cl_id,
			     &fp->cid, 1, BP_FUNC(bp), bnx2x_sp(bp, q_rdata),
			     bnx2x_sp_mapping(bp, q_rdata), q_type);

	DP(NETIF_MSG_IFUP,
	   "queue[%d]: bnx2x_init_sb(%p,%p) cl_id %d fw_sb %d igu_sb %d\n",
	   fp->index, bp, fp->status_blk.e2_sb, fp->cl_id, fp->fw_sb_id,
	   fp->igu_sb_id);
}

static inline int bnx2x_clean_tx_queue(struct bnx2x *bp,
				       struct bnx2x_fp_txdata *txdata)
{
	int cnt = 1000;

	while (bnx2x_has_tx_work_unload(txdata)) {
		if (!cnt) {
			BNX2X_ERR("timeout waiting for queue[%d]: txdata->tx_pkt_prod(%d) != txdata->tx_pkt_cons(%d)\n",
				  txdata->txq_index, txdata->tx_pkt_prod,
				  txdata->tx_pkt_cons);
#ifdef BNX2X_STOP_ON_ERROR
			bnx2x_panic();
			return -EBUSY;
#else
			break;
#endif
		}
		cnt--;
		usleep_range(1000, 1000);
	}

	return 0;
}

int bnx2x_get_link_cfg_idx(struct bnx2x *bp);

static inline void __storm_memset_struct(struct bnx2x *bp,
					 u32 addr, size_t size, u32 *data)
{
	int i;
	for (i = 0; i < size/4; i++)
		REG_WR(bp, addr + (i * 4), data[i]);
}

/**
 * bnx2x_wait_sp_comp - wait for the outstanding SP commands.
 *
 * @bp:		driver handle
 * @mask:	bits that need to be cleared
 */
static inline bool bnx2x_wait_sp_comp(struct bnx2x *bp, unsigned long mask)
{
	int tout = 5000; /* Wait for 5 secs tops */

	while (tout--) {
		smp_mb();
		netif_addr_lock_bh(bp->dev);
		if (!(bp->sp_state & mask)) {
			netif_addr_unlock_bh(bp->dev);
			return true;
		}
		netif_addr_unlock_bh(bp->dev);

		usleep_range(1000, 1000);
	}

	smp_mb();

	netif_addr_lock_bh(bp->dev);
	if (bp->sp_state & mask) {
		BNX2X_ERR("Filtering completion timed out. sp_state 0x%lx, mask 0x%lx\n",
			  bp->sp_state, mask);
		netif_addr_unlock_bh(bp->dev);
		return false;
	}
	netif_addr_unlock_bh(bp->dev);

	return true;
}

/**
 * bnx2x_set_ctx_validation - set CDU context validation values
 *
 * @bp:		driver handle
 * @cxt:	context of the connection on the host memory
 * @cid:	SW CID of the connection to be configured
 */
void bnx2x_set_ctx_validation(struct bnx2x *bp, struct eth_context *cxt,
			      u32 cid);

void bnx2x_update_coalesce_sb_index(struct bnx2x *bp, u8 fw_sb_id,
				    u8 sb_index, u8 disable, u16 usec);
void bnx2x_acquire_phy_lock(struct bnx2x *bp);
void bnx2x_release_phy_lock(struct bnx2x *bp);

/**
 * bnx2x_extract_max_cfg - extract MAX BW part from MF configuration.
 *
 * @bp:		driver handle
 * @mf_cfg:	MF configuration
 *
 */
static inline u16 bnx2x_extract_max_cfg(struct bnx2x *bp, u32 mf_cfg)
{
	u16 max_cfg = (mf_cfg & FUNC_MF_CFG_MAX_BW_MASK) >>
			      FUNC_MF_CFG_MAX_BW_SHIFT;
	if (!max_cfg) {
		DP(NETIF_MSG_IFUP | BNX2X_MSG_ETHTOOL,
		   "Max BW configured to 0 - using 100 instead\n");
		max_cfg = 100;
	}
	return max_cfg;
}

/* checks if HW supports GRO for given MTU */
static inline bool bnx2x_mtu_allows_gro(int mtu)
{
	/* gro frags per page */
	int fpp = SGE_PAGE_SIZE / (mtu - ETH_MAX_TPA_HEADER_SIZE);

	/*
	 * 1. number of frags should not grow above MAX_SKB_FRAGS
	 * 2. frag must fit the page
	 */
	return mtu <= SGE_PAGE_SIZE && (U_ETH_SGL_SIZE * fpp) <= MAX_SKB_FRAGS;
}

/**
 * bnx2x_get_iscsi_info - update iSCSI params according to licensing info.
 *
 * @bp:		driver handle
 *
 */
void bnx2x_get_iscsi_info(struct bnx2x *bp);

/**
 * bnx2x_link_sync_notify - send notification to other functions.
 *
 * @bp:		driver handle
 *
 */
static inline void bnx2x_link_sync_notify(struct bnx2x *bp)
{
	int func;
	int vn;

	/* Set the attention towards other drivers on the same port */
	for (vn = VN_0; vn < BP_MAX_VN_NUM(bp); vn++) {
		if (vn == BP_VN(bp))
			continue;

		func = func_by_vn(bp, vn);
		REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_0 +
		       (LINK_SYNC_ATTENTION_BIT_FUNC_0 + func)*4, 1);
	}
}

/**
 * bnx2x_update_drv_flags - update flags in shmem
 *
 * @bp:		driver handle
 * @flags:	flags to update
 * @set:	set or clear
 *
 */
static inline void bnx2x_update_drv_flags(struct bnx2x *bp, u32 flags, u32 set)
{
	if (SHMEM2_HAS(bp, drv_flags)) {
		u32 drv_flags;
		bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_DRV_FLAGS);
		drv_flags = SHMEM2_RD(bp, drv_flags);

		if (set)
			SET_FLAGS(drv_flags, flags);
		else
			RESET_FLAGS(drv_flags, flags);

		SHMEM2_WR(bp, drv_flags, drv_flags);
		DP(NETIF_MSG_IFUP, "drv_flags 0x%08x\n", drv_flags);
		bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_DRV_FLAGS);
	}
}

static inline bool bnx2x_is_valid_ether_addr(struct bnx2x *bp, u8 *addr)
{
	if (is_valid_ether_addr(addr) ||
	    (is_zero_ether_addr(addr) &&
	     (IS_MF_STORAGE_SD(bp) || IS_MF_FCOE_AFEX(bp))))
		return true;

	return false;
}

#endif /* BNX2X_CMN_H */
