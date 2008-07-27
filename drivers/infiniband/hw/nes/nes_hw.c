/*
 * Copyright (c) 2006 - 2008 NetEffect, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_vlan.h>
#include <linux/inet_lro.h>

#include "nes.h"

static unsigned int nes_lro_max_aggr = NES_LRO_MAX_AGGR;
module_param(nes_lro_max_aggr, uint, 0444);
MODULE_PARM_DESC(nes_lro_max_aggr, "NIC LRO max packet aggregation");

static u32 crit_err_count;
u32 int_mod_timer_init;
u32 int_mod_cq_depth_256;
u32 int_mod_cq_depth_128;
u32 int_mod_cq_depth_32;
u32 int_mod_cq_depth_24;
u32 int_mod_cq_depth_16;
u32 int_mod_cq_depth_4;
u32 int_mod_cq_depth_1;

#include "nes_cm.h"

static void nes_cqp_ce_handler(struct nes_device *nesdev, struct nes_hw_cq *cq);
static void nes_init_csr_ne020(struct nes_device *nesdev, u8 hw_rev, u8 port_count);
static int nes_init_serdes(struct nes_device *nesdev, u8 hw_rev, u8 port_count,
			   u8 OneG_Mode);
static void nes_nic_napi_ce_handler(struct nes_device *nesdev, struct nes_hw_nic_cq *cq);
static void nes_process_aeq(struct nes_device *nesdev, struct nes_hw_aeq *aeq);
static void nes_process_ceq(struct nes_device *nesdev, struct nes_hw_ceq *ceq);
static void nes_process_iwarp_aeqe(struct nes_device *nesdev,
				   struct nes_hw_aeqe *aeqe);
static void nes_process_mac_intr(struct nes_device *nesdev, u32 mac_number);
static unsigned int nes_reset_adapter_ne020(struct nes_device *nesdev, u8 *OneG_Mode);

#ifdef CONFIG_INFINIBAND_NES_DEBUG
static unsigned char *nes_iwarp_state_str[] = {
	"Non-Existant",
	"Idle",
	"RTS",
	"Closing",
	"RSVD1",
	"Terminate",
	"Error",
	"RSVD2",
};

static unsigned char *nes_tcp_state_str[] = {
	"Non-Existant",
	"Closed",
	"Listen",
	"SYN Sent",
	"SYN Rcvd",
	"Established",
	"Close Wait",
	"FIN Wait 1",
	"Closing",
	"Last Ack",
	"FIN Wait 2",
	"Time Wait",
	"RSVD1",
	"RSVD2",
	"RSVD3",
	"RSVD4",
};
#endif


/**
 * nes_nic_init_timer_defaults
 */
void  nes_nic_init_timer_defaults(struct nes_device *nesdev, u8 jumbomode)
{
	unsigned long flags;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_tune_timer *shared_timer = &nesadapter->tune_timer;

	spin_lock_irqsave(&nesadapter->periodic_timer_lock, flags);

	shared_timer->timer_in_use_min = NES_NIC_FAST_TIMER_LOW;
	shared_timer->timer_in_use_max = NES_NIC_FAST_TIMER_HIGH;
	if (jumbomode) {
		shared_timer->threshold_low    = DEFAULT_JUMBO_NES_QL_LOW;
		shared_timer->threshold_target = DEFAULT_JUMBO_NES_QL_TARGET;
		shared_timer->threshold_high   = DEFAULT_JUMBO_NES_QL_HIGH;
	} else {
		shared_timer->threshold_low    = DEFAULT_NES_QL_LOW;
		shared_timer->threshold_target = DEFAULT_NES_QL_TARGET;
		shared_timer->threshold_high   = DEFAULT_NES_QL_HIGH;
	}

	/* todo use netdev->mtu to set thresholds */
	spin_unlock_irqrestore(&nesadapter->periodic_timer_lock, flags);
}


/**
 * nes_nic_init_timer
 */
static void  nes_nic_init_timer(struct nes_device *nesdev)
{
	unsigned long flags;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_tune_timer *shared_timer = &nesadapter->tune_timer;

	spin_lock_irqsave(&nesadapter->periodic_timer_lock, flags);

	if (shared_timer->timer_in_use_old == 0) {
		nesdev->deepcq_count = 0;
		shared_timer->timer_direction_upward = 0;
		shared_timer->timer_direction_downward = 0;
		shared_timer->timer_in_use = NES_NIC_FAST_TIMER;
		shared_timer->timer_in_use_old = 0;

	}
	if (shared_timer->timer_in_use != shared_timer->timer_in_use_old) {
		shared_timer->timer_in_use_old = shared_timer->timer_in_use;
		nes_write32(nesdev->regs+NES_PERIODIC_CONTROL,
			0x80000000 | ((u32)(shared_timer->timer_in_use*8)));
	}
	/* todo use netdev->mtu to set thresholds */
	spin_unlock_irqrestore(&nesadapter->periodic_timer_lock, flags);
}


/**
 * nes_nic_tune_timer
 */
static void nes_nic_tune_timer(struct nes_device *nesdev)
{
	unsigned long flags;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_tune_timer *shared_timer = &nesadapter->tune_timer;
	u16 cq_count = nesdev->currcq_count;

	spin_lock_irqsave(&nesadapter->periodic_timer_lock, flags);

	if (shared_timer->cq_count_old <= cq_count)
		shared_timer->cq_direction_downward = 0;
	else
		shared_timer->cq_direction_downward++;
	shared_timer->cq_count_old = cq_count;
	if (shared_timer->cq_direction_downward > NES_NIC_CQ_DOWNWARD_TREND) {
		if (cq_count <= shared_timer->threshold_low &&
		    shared_timer->threshold_low > 4) {
			shared_timer->threshold_low = shared_timer->threshold_low/2;
			shared_timer->cq_direction_downward=0;
			nesdev->currcq_count = 0;
			spin_unlock_irqrestore(&nesadapter->periodic_timer_lock, flags);
			return;
		}
	}

	if (cq_count > 1) {
		nesdev->deepcq_count += cq_count;
		if (cq_count <= shared_timer->threshold_low) {       /* increase timer gently */
			shared_timer->timer_direction_upward++;
			shared_timer->timer_direction_downward = 0;
		} else if (cq_count <= shared_timer->threshold_target) { /* balanced */
			shared_timer->timer_direction_upward = 0;
			shared_timer->timer_direction_downward = 0;
		} else if (cq_count <= shared_timer->threshold_high) {  /* decrease timer gently */
			shared_timer->timer_direction_downward++;
			shared_timer->timer_direction_upward = 0;
		} else if (cq_count <= (shared_timer->threshold_high) * 2) {
			shared_timer->timer_in_use -= 2;
			shared_timer->timer_direction_upward = 0;
			shared_timer->timer_direction_downward++;
		} else {
			shared_timer->timer_in_use -= 4;
			shared_timer->timer_direction_upward = 0;
			shared_timer->timer_direction_downward++;
		}

		if (shared_timer->timer_direction_upward > 3 ) {  /* using history */
			shared_timer->timer_in_use += 3;
			shared_timer->timer_direction_upward = 0;
			shared_timer->timer_direction_downward = 0;
		}
		if (shared_timer->timer_direction_downward > 5) { /* using history */
			shared_timer->timer_in_use -= 4 ;
			shared_timer->timer_direction_downward = 0;
			shared_timer->timer_direction_upward = 0;
		}
	}

	/* boundary checking */
	if (shared_timer->timer_in_use > NES_NIC_FAST_TIMER_HIGH)
		shared_timer->timer_in_use = NES_NIC_FAST_TIMER_HIGH;
	else if (shared_timer->timer_in_use < NES_NIC_FAST_TIMER_LOW) {
		shared_timer->timer_in_use = NES_NIC_FAST_TIMER_LOW;
	}

	nesdev->currcq_count = 0;

	spin_unlock_irqrestore(&nesadapter->periodic_timer_lock, flags);
}


/**
 * nes_init_adapter - initialize adapter
 */
struct nes_adapter *nes_init_adapter(struct nes_device *nesdev, u8 hw_rev) {
	struct nes_adapter *nesadapter = NULL;
	unsigned long num_pds;
	u32 u32temp;
	u32 port_count;
	u16 max_rq_wrs;
	u16 max_sq_wrs;
	u32 max_mr;
	u32 max_256pbl;
	u32 max_4kpbl;
	u32 max_qp;
	u32 max_irrq;
	u32 max_cq;
	u32 hte_index_mask;
	u32 adapter_size;
	u32 arp_table_size;
	u16 vendor_id;
	u8  OneG_Mode;
	u8  func_index;

	/* search the list of existing adapters */
	list_for_each_entry(nesadapter, &nes_adapter_list, list) {
		nes_debug(NES_DBG_INIT, "Searching Adapter list for PCI devfn = 0x%X,"
				" adapter PCI slot/bus = %u/%u, pci devices PCI slot/bus = %u/%u, .\n",
				nesdev->pcidev->devfn,
				PCI_SLOT(nesadapter->devfn),
				nesadapter->bus_number,
				PCI_SLOT(nesdev->pcidev->devfn),
				nesdev->pcidev->bus->number );
		if ((PCI_SLOT(nesadapter->devfn) == PCI_SLOT(nesdev->pcidev->devfn)) &&
				(nesadapter->bus_number == nesdev->pcidev->bus->number)) {
			nesadapter->ref_count++;
			return nesadapter;
		}
	}

	/* no adapter found */
	num_pds = pci_resource_len(nesdev->pcidev, BAR_1) >> PAGE_SHIFT;
	if ((hw_rev != NE020_REV) && (hw_rev != NE020_REV1)) {
		nes_debug(NES_DBG_INIT, "NE020 driver detected unknown hardware revision 0x%x\n",
				hw_rev);
		return NULL;
	}

	nes_debug(NES_DBG_INIT, "Determine Soft Reset, QP_control=0x%x, CPU0=0x%x, CPU1=0x%x, CPU2=0x%x\n",
			nes_read_indexed(nesdev, NES_IDX_QP_CONTROL + PCI_FUNC(nesdev->pcidev->devfn) * 8),
			nes_read_indexed(nesdev, NES_IDX_INT_CPU_STATUS),
			nes_read_indexed(nesdev, NES_IDX_INT_CPU_STATUS + 4),
			nes_read_indexed(nesdev, NES_IDX_INT_CPU_STATUS + 8));

	nes_debug(NES_DBG_INIT, "Reset and init NE020\n");


	if ((port_count = nes_reset_adapter_ne020(nesdev, &OneG_Mode)) == 0)
		return NULL;
	if (nes_init_serdes(nesdev, hw_rev, port_count, OneG_Mode))
		return NULL;
	nes_init_csr_ne020(nesdev, hw_rev, port_count);

	max_qp = nes_read_indexed(nesdev, NES_IDX_QP_CTX_SIZE);
	nes_debug(NES_DBG_INIT, "QP_CTX_SIZE=%u\n", max_qp);

	u32temp = nes_read_indexed(nesdev, NES_IDX_QUAD_HASH_TABLE_SIZE);
	if (max_qp > ((u32)1 << (u32temp & 0x001f))) {
		nes_debug(NES_DBG_INIT, "Reducing Max QPs to %u due to hash table size = 0x%08X\n",
				max_qp, u32temp);
		max_qp = (u32)1 << (u32temp & 0x001f);
	}

	hte_index_mask = ((u32)1 << ((u32temp & 0x001f)+1))-1;
	nes_debug(NES_DBG_INIT, "Max QP = %u, hte_index_mask = 0x%08X.\n",
			max_qp, hte_index_mask);

	u32temp = nes_read_indexed(nesdev, NES_IDX_IRRQ_COUNT);

	max_irrq = 1 << (u32temp & 0x001f);

	if (max_qp > max_irrq) {
		max_qp = max_irrq;
		nes_debug(NES_DBG_INIT, "Reducing Max QPs to %u due to Available Q1s.\n",
				max_qp);
	}

	/* there should be no reason to allocate more pds than qps */
	if (num_pds > max_qp)
		num_pds = max_qp;

	u32temp = nes_read_indexed(nesdev, NES_IDX_MRT_SIZE);
	max_mr = (u32)8192 << (u32temp & 0x7);

	u32temp = nes_read_indexed(nesdev, NES_IDX_PBL_REGION_SIZE);
	max_256pbl = (u32)1 << (u32temp & 0x0000001f);
	max_4kpbl = (u32)1 << ((u32temp >> 16) & 0x0000001f);
	max_cq = nes_read_indexed(nesdev, NES_IDX_CQ_CTX_SIZE);

	u32temp = nes_read_indexed(nesdev, NES_IDX_ARP_CACHE_SIZE);
	arp_table_size = 1 << u32temp;

	adapter_size = (sizeof(struct nes_adapter) +
			(sizeof(unsigned long)-1)) & (~(sizeof(unsigned long)-1));
	adapter_size += sizeof(unsigned long) * BITS_TO_LONGS(max_qp);
	adapter_size += sizeof(unsigned long) * BITS_TO_LONGS(max_mr);
	adapter_size += sizeof(unsigned long) * BITS_TO_LONGS(max_cq);
	adapter_size += sizeof(unsigned long) * BITS_TO_LONGS(num_pds);
	adapter_size += sizeof(unsigned long) * BITS_TO_LONGS(arp_table_size);
	adapter_size += sizeof(struct nes_qp **) * max_qp;

	/* allocate a new adapter struct */
	nesadapter = kzalloc(adapter_size, GFP_KERNEL);
	if (nesadapter == NULL) {
		return NULL;
	}

	nes_debug(NES_DBG_INIT, "Allocating new nesadapter @ %p, size = %u (actual size = %u).\n",
			nesadapter, (u32)sizeof(struct nes_adapter), adapter_size);

	/* populate the new nesadapter */
	nesadapter->devfn = nesdev->pcidev->devfn;
	nesadapter->bus_number = nesdev->pcidev->bus->number;
	nesadapter->ref_count = 1;
	nesadapter->timer_int_req = 0xffff0000;
	nesadapter->OneG_Mode = OneG_Mode;
	nesadapter->doorbell_start = nesdev->doorbell_region;

	/* nesadapter->tick_delta = clk_divisor; */
	nesadapter->hw_rev = hw_rev;
	nesadapter->port_count = port_count;

	nesadapter->max_qp = max_qp;
	nesadapter->hte_index_mask = hte_index_mask;
	nesadapter->max_irrq = max_irrq;
	nesadapter->max_mr = max_mr;
	nesadapter->max_256pbl = max_256pbl - 1;
	nesadapter->max_4kpbl = max_4kpbl - 1;
	nesadapter->max_cq = max_cq;
	nesadapter->free_256pbl = max_256pbl - 1;
	nesadapter->free_4kpbl = max_4kpbl - 1;
	nesadapter->max_pd = num_pds;
	nesadapter->arp_table_size = arp_table_size;

	nesadapter->et_pkt_rate_low = NES_TIMER_ENABLE_LIMIT;
	if (nes_drv_opt & NES_DRV_OPT_DISABLE_INT_MOD) {
		nesadapter->et_use_adaptive_rx_coalesce = 0;
		nesadapter->timer_int_limit = NES_TIMER_INT_LIMIT;
		nesadapter->et_rx_coalesce_usecs_irq = interrupt_mod_interval;
	} else {
		nesadapter->et_use_adaptive_rx_coalesce = 1;
		nesadapter->timer_int_limit = NES_TIMER_INT_LIMIT_DYNAMIC;
		nesadapter->et_rx_coalesce_usecs_irq = 0;
		printk(PFX "%s: Using Adaptive Interrupt Moderation\n", __func__);
	}
	/* Setup and enable the periodic timer */
	if (nesadapter->et_rx_coalesce_usecs_irq)
		nes_write32(nesdev->regs+NES_PERIODIC_CONTROL, 0x80000000 |
				((u32)(nesadapter->et_rx_coalesce_usecs_irq * 8)));
	else
		nes_write32(nesdev->regs+NES_PERIODIC_CONTROL, 0x00000000);

	nesadapter->base_pd = 1;

	nesadapter->device_cap_flags =
		IB_DEVICE_LOCAL_DMA_LKEY | IB_DEVICE_MEM_WINDOW;

	nesadapter->allocated_qps = (unsigned long *)&(((unsigned char *)nesadapter)
			[(sizeof(struct nes_adapter)+(sizeof(unsigned long)-1))&(~(sizeof(unsigned long)-1))]);
	nesadapter->allocated_cqs = &nesadapter->allocated_qps[BITS_TO_LONGS(max_qp)];
	nesadapter->allocated_mrs = &nesadapter->allocated_cqs[BITS_TO_LONGS(max_cq)];
	nesadapter->allocated_pds = &nesadapter->allocated_mrs[BITS_TO_LONGS(max_mr)];
	nesadapter->allocated_arps = &nesadapter->allocated_pds[BITS_TO_LONGS(num_pds)];
	nesadapter->qp_table = (struct nes_qp **)(&nesadapter->allocated_arps[BITS_TO_LONGS(arp_table_size)]);


	/* mark the usual suspect QPs and CQs as in use */
	for (u32temp = 0; u32temp < NES_FIRST_QPN; u32temp++) {
		set_bit(u32temp, nesadapter->allocated_qps);
		set_bit(u32temp, nesadapter->allocated_cqs);
	}

	for (u32temp = 0; u32temp < 20; u32temp++)
		set_bit(u32temp, nesadapter->allocated_pds);
	u32temp = nes_read_indexed(nesdev, NES_IDX_QP_MAX_CFG_SIZES);

	max_rq_wrs = ((u32temp >> 8) & 3);
	switch (max_rq_wrs) {
		case 0:
			max_rq_wrs = 4;
			break;
		case 1:
			max_rq_wrs = 16;
			break;
		case 2:
			max_rq_wrs = 32;
			break;
		case 3:
			max_rq_wrs = 512;
			break;
	}

	max_sq_wrs = (u32temp & 3);
	switch (max_sq_wrs) {
		case 0:
			max_sq_wrs = 4;
			break;
		case 1:
			max_sq_wrs = 16;
			break;
		case 2:
			max_sq_wrs = 32;
			break;
		case 3:
			max_sq_wrs = 512;
			break;
	}
	nesadapter->max_qp_wr = min(max_rq_wrs, max_sq_wrs);
	nesadapter->max_irrq_wr = (u32temp >> 16) & 3;

	nesadapter->max_sge = 4;
	nesadapter->max_cqe = 32767;

	if (nes_read_eeprom_values(nesdev, nesadapter)) {
		printk(KERN_ERR PFX "Unable to read EEPROM data.\n");
		kfree(nesadapter);
		return NULL;
	}

	u32temp = nes_read_indexed(nesdev, NES_IDX_TCP_TIMER_CONFIG);
	nes_write_indexed(nesdev, NES_IDX_TCP_TIMER_CONFIG,
			(u32temp & 0xff000000) | (nesadapter->tcp_timer_core_clk_divisor & 0x00ffffff));

	/* setup port configuration */
	if (nesadapter->port_count == 1) {
		u32temp = 0x00000000;
		if (nes_drv_opt & NES_DRV_OPT_DUAL_LOGICAL_PORT)
			nes_write_indexed(nesdev, NES_IDX_TX_POOL_SIZE, 0x00000002);
		else
			nes_write_indexed(nesdev, NES_IDX_TX_POOL_SIZE, 0x00000003);
	} else {
		if (nesadapter->port_count == 2)
			u32temp = 0x00000044;
		else
			u32temp = 0x000000e4;
		nes_write_indexed(nesdev, NES_IDX_TX_POOL_SIZE, 0x00000003);
	}

	nes_write_indexed(nesdev, NES_IDX_NIC_LOGPORT_TO_PHYPORT, u32temp);
	nes_debug(NES_DBG_INIT, "Probe time, LOG2PHY=%u\n",
			nes_read_indexed(nesdev, NES_IDX_NIC_LOGPORT_TO_PHYPORT));

	spin_lock_init(&nesadapter->resource_lock);
	spin_lock_init(&nesadapter->phy_lock);
	spin_lock_init(&nesadapter->pbl_lock);
	spin_lock_init(&nesadapter->periodic_timer_lock);

	INIT_LIST_HEAD(&nesadapter->nesvnic_list[0]);
	INIT_LIST_HEAD(&nesadapter->nesvnic_list[1]);
	INIT_LIST_HEAD(&nesadapter->nesvnic_list[2]);
	INIT_LIST_HEAD(&nesadapter->nesvnic_list[3]);

	if ((!nesadapter->OneG_Mode) && (nesadapter->port_count == 2)) {
		u32 pcs_control_status0, pcs_control_status1;
		u32 reset_value;
		u32 i = 0;
		u32 int_cnt = 0;
		u32 ext_cnt = 0;
		unsigned long flags;
		u32 j = 0;

		pcs_control_status0 = nes_read_indexed(nesdev,
			NES_IDX_PHY_PCS_CONTROL_STATUS0);
		pcs_control_status1 = nes_read_indexed(nesdev,
			NES_IDX_PHY_PCS_CONTROL_STATUS0 + 0x200);

		for (i = 0; i < NES_MAX_LINK_CHECK; i++) {
			pcs_control_status0 = nes_read_indexed(nesdev,
					NES_IDX_PHY_PCS_CONTROL_STATUS0);
			pcs_control_status1 = nes_read_indexed(nesdev,
					NES_IDX_PHY_PCS_CONTROL_STATUS0 + 0x200);
			if ((0x0F000100 == (pcs_control_status0 & 0x0F000100))
			    || (0x0F000100 == (pcs_control_status1 & 0x0F000100)))
				int_cnt++;
			msleep(1);
		}
		if (int_cnt > 1) {
			spin_lock_irqsave(&nesadapter->phy_lock, flags);
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1, 0x0000F088);
			mh_detected++;
			reset_value = nes_read32(nesdev->regs+NES_SOFTWARE_RESET);
			reset_value |= 0x0000003d;
			nes_write32(nesdev->regs+NES_SOFTWARE_RESET, reset_value);

			while (((nes_read32(nesdev->regs+NES_SOFTWARE_RESET)
				& 0x00000040) != 0x00000040) && (j++ < 5000));
			spin_unlock_irqrestore(&nesadapter->phy_lock, flags);

			pcs_control_status0 = nes_read_indexed(nesdev,
					NES_IDX_PHY_PCS_CONTROL_STATUS0);
			pcs_control_status1 = nes_read_indexed(nesdev,
					NES_IDX_PHY_PCS_CONTROL_STATUS0 + 0x200);

			for (i = 0; i < NES_MAX_LINK_CHECK; i++) {
				pcs_control_status0 = nes_read_indexed(nesdev,
					NES_IDX_PHY_PCS_CONTROL_STATUS0);
				pcs_control_status1 = nes_read_indexed(nesdev,
					NES_IDX_PHY_PCS_CONTROL_STATUS0 + 0x200);
				if ((0x0F000100 == (pcs_control_status0 & 0x0F000100))
					|| (0x0F000100 == (pcs_control_status1 & 0x0F000100))) {
					if (++ext_cnt > int_cnt) {
						spin_lock_irqsave(&nesadapter->phy_lock, flags);
						nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1,
								0x0000F0C8);
						mh_detected++;
						reset_value = nes_read32(nesdev->regs+NES_SOFTWARE_RESET);
						reset_value |= 0x0000003d;
						nes_write32(nesdev->regs+NES_SOFTWARE_RESET, reset_value);

						while (((nes_read32(nesdev->regs+NES_SOFTWARE_RESET)
							& 0x00000040) != 0x00000040) && (j++ < 5000));
						spin_unlock_irqrestore(&nesadapter->phy_lock, flags);
						break;
					}
				}
				msleep(1);
			}
		}
	}

	if (nesadapter->hw_rev == NE020_REV) {
		init_timer(&nesadapter->mh_timer);
		nesadapter->mh_timer.function = nes_mh_fix;
		nesadapter->mh_timer.expires = jiffies + (HZ/5);  /* 1 second */
		nesadapter->mh_timer.data = (unsigned long)nesdev;
		add_timer(&nesadapter->mh_timer);
	} else {
		nes_write32(nesdev->regs+NES_INTF_INT_STAT, 0x0f000000);
	}

	init_timer(&nesadapter->lc_timer);
	nesadapter->lc_timer.function = nes_clc;
	nesadapter->lc_timer.expires = jiffies + 3600 * HZ;  /* 1 hour */
	nesadapter->lc_timer.data = (unsigned long)nesdev;
	add_timer(&nesadapter->lc_timer);

	list_add_tail(&nesadapter->list, &nes_adapter_list);

	for (func_index = 0; func_index < 8; func_index++) {
		pci_bus_read_config_word(nesdev->pcidev->bus,
					PCI_DEVFN(PCI_SLOT(nesdev->pcidev->devfn),
					func_index), 0, &vendor_id);
		if (vendor_id == 0xffff)
			break;
	}
	nes_debug(NES_DBG_INIT, "%s %d functions found for %s.\n", __func__,
		func_index, pci_name(nesdev->pcidev));
	nesadapter->adapter_fcn_count = func_index;

	return nesadapter;
}


/**
 * nes_reset_adapter_ne020
 */
static unsigned int nes_reset_adapter_ne020(struct nes_device *nesdev, u8 *OneG_Mode)
{
	u32 port_count;
	u32 u32temp;
	u32 i;

	u32temp = nes_read32(nesdev->regs+NES_SOFTWARE_RESET);
	port_count = ((u32temp & 0x00000300) >> 8) + 1;
	/* TODO: assuming that both SERDES are set the same for now */
	*OneG_Mode = (u32temp & 0x00003c00) ? 0 : 1;
	nes_debug(NES_DBG_INIT, "Initial Software Reset = 0x%08X, port_count=%u\n",
			u32temp, port_count);
	if (*OneG_Mode)
		nes_debug(NES_DBG_INIT, "Running in 1G mode.\n");
	u32temp &= 0xff00ffc0;
	switch (port_count) {
		case 1:
			u32temp |= 0x00ee0000;
			break;
		case 2:
			u32temp |= 0x00cc0000;
			break;
		case 4:
			u32temp |= 0x00000000;
			break;
		default:
			return 0;
			break;
	}

	/* check and do full reset if needed */
	if (nes_read_indexed(nesdev, NES_IDX_QP_CONTROL+(PCI_FUNC(nesdev->pcidev->devfn)*8))) {
		nes_debug(NES_DBG_INIT, "Issuing Full Soft reset = 0x%08X\n", u32temp | 0xd);
		nes_write32(nesdev->regs+NES_SOFTWARE_RESET, u32temp | 0xd);

		i = 0;
		while (((nes_read32(nesdev->regs+NES_SOFTWARE_RESET) & 0x00000040) == 0) && i++ < 10000)
			mdelay(1);
		if (i >= 10000) {
			nes_debug(NES_DBG_INIT, "Did not see full soft reset done.\n");
			return 0;
		}

		i = 0;
		while ((nes_read_indexed(nesdev, NES_IDX_INT_CPU_STATUS) != 0x80) && i++ < 10000)
			mdelay(1);
		if (i >= 10000) {
			printk(KERN_ERR PFX "Internal CPU not ready, status = %02X\n",
			       nes_read_indexed(nesdev, NES_IDX_INT_CPU_STATUS));
			return 0;
		}
	}

	/* port reset */
	switch (port_count) {
		case 1:
			u32temp |= 0x00ee0010;
			break;
		case 2:
			u32temp |= 0x00cc0030;
			break;
		case 4:
			u32temp |= 0x00000030;
			break;
	}

	nes_debug(NES_DBG_INIT, "Issuing Port Soft reset = 0x%08X\n", u32temp | 0xd);
	nes_write32(nesdev->regs+NES_SOFTWARE_RESET, u32temp | 0xd);

	i = 0;
	while (((nes_read32(nesdev->regs+NES_SOFTWARE_RESET) & 0x00000040) == 0) && i++ < 10000)
		mdelay(1);
	if (i >= 10000) {
		nes_debug(NES_DBG_INIT, "Did not see port soft reset done.\n");
		return 0;
	}

	/* serdes 0 */
	i = 0;
	while (((u32temp = (nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_STATUS0)
			& 0x0000000f)) != 0x0000000f) && i++ < 5000)
		mdelay(1);
	if (i >= 5000) {
		nes_debug(NES_DBG_INIT, "Serdes 0 not ready, status=%x\n", u32temp);
		return 0;
	}

	/* serdes 1 */
	if (port_count > 1) {
		i = 0;
		while (((u32temp = (nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_STATUS1)
				& 0x0000000f)) != 0x0000000f) && i++ < 5000)
			mdelay(1);
		if (i >= 5000) {
			nes_debug(NES_DBG_INIT, "Serdes 1 not ready, status=%x\n", u32temp);
			return 0;
		}
	}

	return port_count;
}


/**
 * nes_init_serdes
 */
static int nes_init_serdes(struct nes_device *nesdev, u8 hw_rev, u8 port_count,
			   u8 OneG_Mode)
{
	int i;
	u32 u32temp;

	if (hw_rev != NE020_REV) {
		/* init serdes 0 */

		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_CDR_CONTROL0, 0x000000FF);
		if (!OneG_Mode)
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_TX_HIGHZ_LANE_MODE0, 0x11110000);
		if (port_count > 1) {
			/* init serdes 1 */
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_CDR_CONTROL1, 0x000000FF);
			if (!OneG_Mode)
				nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_TX_HIGHZ_LANE_MODE1, 0x11110000);
			}
	} else {
		/* init serdes 0 */
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL0, 0x00000008);
		i = 0;
		while (((u32temp = (nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_STATUS0)
				& 0x0000000f)) != 0x0000000f) && i++ < 5000)
			mdelay(1);
		if (i >= 5000) {
			nes_debug(NES_DBG_PHY, "Init: serdes 0 not ready, status=%x\n", u32temp);
			return 1;
		}
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_TX_EMP0, 0x000bdef7);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_TX_DRIVE0, 0x9ce73000);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_MODE0, 0x0ff00000);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_SIGDET0, 0x00000000);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_BYPASS0, 0x00000000);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_LOOPBACK_CONTROL0, 0x00000000);
		if (OneG_Mode)
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_EQ_CONTROL0, 0xf0182222);
		else
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_EQ_CONTROL0, 0xf0042222);

		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_CDR_CONTROL0, 0x000000ff);
		if (port_count > 1) {
			/* init serdes 1 */
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1, 0x00000048);
			i = 0;
			while (((u32temp = (nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_STATUS1)
				& 0x0000000f)) != 0x0000000f) && (i++ < 5000))
				mdelay(1);
			if (i >= 5000) {
				printk("%s: Init: serdes 1 not ready, status=%x\n", __func__, u32temp);
				/* return 1; */
			}
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_TX_EMP1, 0x000bdef7);
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_TX_DRIVE1, 0x9ce73000);
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_MODE1, 0x0ff00000);
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_SIGDET1, 0x00000000);
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_BYPASS1, 0x00000000);
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_LOOPBACK_CONTROL1, 0x00000000);
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_EQ_CONTROL1, 0xf0002222);
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_CDR_CONTROL1, 0x000000ff);
		}
	}
	return 0;
}


/**
 * nes_init_csr_ne020
 * Initialize registers for ne020 hardware
 */
static void nes_init_csr_ne020(struct nes_device *nesdev, u8 hw_rev, u8 port_count)
{
	u32 u32temp;

	nes_debug(NES_DBG_INIT, "port_count=%d\n", port_count);

	nes_write_indexed(nesdev, 0x000001E4, 0x00000007);
	/* nes_write_indexed(nesdev, 0x000001E8, 0x000208C4); */
	nes_write_indexed(nesdev, 0x000001E8, 0x00020874);
	nes_write_indexed(nesdev, 0x000001D8, 0x00048002);
	/* nes_write_indexed(nesdev, 0x000001D8, 0x0004B002); */
	nes_write_indexed(nesdev, 0x000001FC, 0x00050005);
	nes_write_indexed(nesdev, 0x00000600, 0x55555555);
	nes_write_indexed(nesdev, 0x00000604, 0x55555555);

	/* TODO: move these MAC register settings to NIC bringup */
	nes_write_indexed(nesdev, 0x00002000, 0x00000001);
	nes_write_indexed(nesdev, 0x00002004, 0x00000001);
	nes_write_indexed(nesdev, 0x00002008, 0x0000FFFF);
	nes_write_indexed(nesdev, 0x0000200C, 0x00000001);
	nes_write_indexed(nesdev, 0x00002010, 0x000003c1);
	nes_write_indexed(nesdev, 0x0000201C, 0x75345678);
	if (port_count > 1) {
		nes_write_indexed(nesdev, 0x00002200, 0x00000001);
		nes_write_indexed(nesdev, 0x00002204, 0x00000001);
		nes_write_indexed(nesdev, 0x00002208, 0x0000FFFF);
		nes_write_indexed(nesdev, 0x0000220C, 0x00000001);
		nes_write_indexed(nesdev, 0x00002210, 0x000003c1);
		nes_write_indexed(nesdev, 0x0000221C, 0x75345678);
		nes_write_indexed(nesdev, 0x00000908, 0x20000001);
	}
	if (port_count > 2) {
		nes_write_indexed(nesdev, 0x00002400, 0x00000001);
		nes_write_indexed(nesdev, 0x00002404, 0x00000001);
		nes_write_indexed(nesdev, 0x00002408, 0x0000FFFF);
		nes_write_indexed(nesdev, 0x0000240C, 0x00000001);
		nes_write_indexed(nesdev, 0x00002410, 0x000003c1);
		nes_write_indexed(nesdev, 0x0000241C, 0x75345678);
		nes_write_indexed(nesdev, 0x00000910, 0x20000001);

		nes_write_indexed(nesdev, 0x00002600, 0x00000001);
		nes_write_indexed(nesdev, 0x00002604, 0x00000001);
		nes_write_indexed(nesdev, 0x00002608, 0x0000FFFF);
		nes_write_indexed(nesdev, 0x0000260C, 0x00000001);
		nes_write_indexed(nesdev, 0x00002610, 0x000003c1);
		nes_write_indexed(nesdev, 0x0000261C, 0x75345678);
		nes_write_indexed(nesdev, 0x00000918, 0x20000001);
	}

	nes_write_indexed(nesdev, 0x00005000, 0x00018000);
	/* nes_write_indexed(nesdev, 0x00005000, 0x00010000); */
	nes_write_indexed(nesdev, 0x00005004, 0x00020001);
	nes_write_indexed(nesdev, 0x00005008, 0x1F1F1F1F);
	nes_write_indexed(nesdev, 0x00005010, 0x1F1F1F1F);
	nes_write_indexed(nesdev, 0x00005018, 0x1F1F1F1F);
	nes_write_indexed(nesdev, 0x00005020, 0x1F1F1F1F);
	nes_write_indexed(nesdev, 0x00006090, 0xFFFFFFFF);

	/* TODO: move this to code, get from EEPROM */
	nes_write_indexed(nesdev, 0x00000900, 0x20000001);
	nes_write_indexed(nesdev, 0x000060C0, 0x0000028e);
	nes_write_indexed(nesdev, 0x000060C8, 0x00000020);

	nes_write_indexed(nesdev, 0x000001EC, 0x7b2625a0);
	/* nes_write_indexed(nesdev, 0x000001EC, 0x5f2625a0); */

	if (hw_rev != NE020_REV) {
		u32temp = nes_read_indexed(nesdev, 0x000008e8);
		u32temp |= 0x80000000;
		nes_write_indexed(nesdev, 0x000008e8, u32temp);
		u32temp = nes_read_indexed(nesdev, 0x000021f8);
		u32temp &= 0x7fffffff;
		u32temp |= 0x7fff0010;
		nes_write_indexed(nesdev, 0x000021f8, u32temp);
	}
}


/**
 * nes_destroy_adapter - destroy the adapter structure
 */
void nes_destroy_adapter(struct nes_adapter *nesadapter)
{
	struct nes_adapter *tmp_adapter;

	list_for_each_entry(tmp_adapter, &nes_adapter_list, list) {
		nes_debug(NES_DBG_SHUTDOWN, "Nes Adapter list entry = 0x%p.\n",
				tmp_adapter);
	}

	nesadapter->ref_count--;
	if (!nesadapter->ref_count) {
		if (nesadapter->hw_rev == NE020_REV) {
			del_timer(&nesadapter->mh_timer);
		}
		del_timer(&nesadapter->lc_timer);

		list_del(&nesadapter->list);
		kfree(nesadapter);
	}
}


/**
 * nes_init_cqp
 */
int nes_init_cqp(struct nes_device *nesdev)
{
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_cqp_qp_context *cqp_qp_context;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_hw_ceq *ceq;
	struct nes_hw_ceq *nic_ceq;
	struct nes_hw_aeq *aeq;
	void *vmem;
	dma_addr_t pmem;
	u32 count=0;
	u32 cqp_head;
	u64 u64temp;
	u32 u32temp;

	/* allocate CQP memory */
	/* Need to add max_cq to the aeq size once cq overflow checking is added back */
	/* SQ is 512 byte aligned, others are 256 byte aligned */
	nesdev->cqp_mem_size = 512 +
			(sizeof(struct nes_hw_cqp_wqe) * NES_CQP_SQ_SIZE) +
			(sizeof(struct nes_hw_cqe) * NES_CCQ_SIZE) +
			max(((u32)sizeof(struct nes_hw_ceqe) * NES_CCEQ_SIZE), (u32)256) +
			max(((u32)sizeof(struct nes_hw_ceqe) * NES_NIC_CEQ_SIZE), (u32)256) +
			(sizeof(struct nes_hw_aeqe) * nesadapter->max_qp) +
			sizeof(struct nes_hw_cqp_qp_context);

	nesdev->cqp_vbase = pci_alloc_consistent(nesdev->pcidev, nesdev->cqp_mem_size,
			&nesdev->cqp_pbase);
	if (!nesdev->cqp_vbase) {
		nes_debug(NES_DBG_INIT, "Unable to allocate memory for host descriptor rings\n");
		return -ENOMEM;
	}
	memset(nesdev->cqp_vbase, 0, nesdev->cqp_mem_size);

	/* Allocate a twice the number of CQP requests as the SQ size */
	nesdev->nes_cqp_requests = kzalloc(sizeof(struct nes_cqp_request) *
			2 * NES_CQP_SQ_SIZE, GFP_KERNEL);
	if (nesdev->nes_cqp_requests == NULL) {
		nes_debug(NES_DBG_INIT, "Unable to allocate memory CQP request entries.\n");
		pci_free_consistent(nesdev->pcidev, nesdev->cqp_mem_size, nesdev->cqp.sq_vbase,
				nesdev->cqp.sq_pbase);
		return -ENOMEM;
	}

	nes_debug(NES_DBG_INIT, "Allocated CQP structures at %p (phys = %016lX), size = %u.\n",
			nesdev->cqp_vbase, (unsigned long)nesdev->cqp_pbase, nesdev->cqp_mem_size);

	spin_lock_init(&nesdev->cqp.lock);
	init_waitqueue_head(&nesdev->cqp.waitq);

	/* Setup Various Structures */
	vmem = (void *)(((unsigned long)nesdev->cqp_vbase + (512 - 1)) &
			~(unsigned long)(512 - 1));
	pmem = (dma_addr_t)(((unsigned long long)nesdev->cqp_pbase + (512 - 1)) &
			~(unsigned long long)(512 - 1));

	nesdev->cqp.sq_vbase = vmem;
	nesdev->cqp.sq_pbase = pmem;
	nesdev->cqp.sq_size = NES_CQP_SQ_SIZE;
	nesdev->cqp.sq_head = 0;
	nesdev->cqp.sq_tail = 0;
	nesdev->cqp.qp_id = PCI_FUNC(nesdev->pcidev->devfn);

	vmem += (sizeof(struct nes_hw_cqp_wqe) * nesdev->cqp.sq_size);
	pmem += (sizeof(struct nes_hw_cqp_wqe) * nesdev->cqp.sq_size);

	nesdev->ccq.cq_vbase = vmem;
	nesdev->ccq.cq_pbase = pmem;
	nesdev->ccq.cq_size = NES_CCQ_SIZE;
	nesdev->ccq.cq_head = 0;
	nesdev->ccq.ce_handler = nes_cqp_ce_handler;
	nesdev->ccq.cq_number = PCI_FUNC(nesdev->pcidev->devfn);

	vmem += (sizeof(struct nes_hw_cqe) * nesdev->ccq.cq_size);
	pmem += (sizeof(struct nes_hw_cqe) * nesdev->ccq.cq_size);

	nesdev->ceq_index = PCI_FUNC(nesdev->pcidev->devfn);
	ceq = &nesadapter->ceq[nesdev->ceq_index];
	ceq->ceq_vbase = vmem;
	ceq->ceq_pbase = pmem;
	ceq->ceq_size = NES_CCEQ_SIZE;
	ceq->ceq_head = 0;

	vmem += max(((u32)sizeof(struct nes_hw_ceqe) * ceq->ceq_size), (u32)256);
	pmem += max(((u32)sizeof(struct nes_hw_ceqe) * ceq->ceq_size), (u32)256);

	nesdev->nic_ceq_index = PCI_FUNC(nesdev->pcidev->devfn) + 8;
	nic_ceq = &nesadapter->ceq[nesdev->nic_ceq_index];
	nic_ceq->ceq_vbase = vmem;
	nic_ceq->ceq_pbase = pmem;
	nic_ceq->ceq_size = NES_NIC_CEQ_SIZE;
	nic_ceq->ceq_head = 0;

	vmem += max(((u32)sizeof(struct nes_hw_ceqe) * nic_ceq->ceq_size), (u32)256);
	pmem += max(((u32)sizeof(struct nes_hw_ceqe) * nic_ceq->ceq_size), (u32)256);

	aeq = &nesadapter->aeq[PCI_FUNC(nesdev->pcidev->devfn)];
	aeq->aeq_vbase = vmem;
	aeq->aeq_pbase = pmem;
	aeq->aeq_size = nesadapter->max_qp;
	aeq->aeq_head = 0;

	/* Setup QP Context */
	vmem += (sizeof(struct nes_hw_aeqe) * aeq->aeq_size);
	pmem += (sizeof(struct nes_hw_aeqe) * aeq->aeq_size);

	cqp_qp_context = vmem;
	cqp_qp_context->context_words[0] =
			cpu_to_le32((PCI_FUNC(nesdev->pcidev->devfn) << 12) + (2 << 10));
	cqp_qp_context->context_words[1] = 0;
	cqp_qp_context->context_words[2] = cpu_to_le32((u32)nesdev->cqp.sq_pbase);
	cqp_qp_context->context_words[3] = cpu_to_le32(((u64)nesdev->cqp.sq_pbase) >> 32);


	/* Write the address to Create CQP */
	if ((sizeof(dma_addr_t) > 4)) {
		nes_write_indexed(nesdev,
				NES_IDX_CREATE_CQP_HIGH + (PCI_FUNC(nesdev->pcidev->devfn) * 8),
				((u64)pmem) >> 32);
	} else {
		nes_write_indexed(nesdev,
				NES_IDX_CREATE_CQP_HIGH + (PCI_FUNC(nesdev->pcidev->devfn) * 8), 0);
	}
	nes_write_indexed(nesdev,
			NES_IDX_CREATE_CQP_LOW + (PCI_FUNC(nesdev->pcidev->devfn) * 8),
			(u32)pmem);

	INIT_LIST_HEAD(&nesdev->cqp_avail_reqs);
	INIT_LIST_HEAD(&nesdev->cqp_pending_reqs);

	for (count = 0; count < 2*NES_CQP_SQ_SIZE; count++) {
		init_waitqueue_head(&nesdev->nes_cqp_requests[count].waitq);
		list_add_tail(&nesdev->nes_cqp_requests[count].list, &nesdev->cqp_avail_reqs);
	}

	/* Write Create CCQ WQE */
	cqp_head = nesdev->cqp.sq_head++;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
			(NES_CQP_CREATE_CQ | NES_CQP_CQ_CEQ_VALID |
			NES_CQP_CQ_CHK_OVERFLOW | ((u32)nesdev->ccq.cq_size << 16)));
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX,
			    (nesdev->ccq.cq_number |
			     ((u32)nesdev->ceq_index << 16)));
	u64temp = (u64)nesdev->ccq.cq_pbase;
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_CQ_WQE_PBL_LOW_IDX, u64temp);
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX] = 0;
	u64temp = (unsigned long)&nesdev->ccq;
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_LOW_IDX] =
			cpu_to_le32((u32)(u64temp >> 1));
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX] =
			cpu_to_le32(((u32)((u64temp) >> 33)) & 0x7FFFFFFF);
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_DOORBELL_INDEX_HIGH_IDX] = 0;

	/* Write Create CEQ WQE */
	cqp_head = nesdev->cqp.sq_head++;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
			    (NES_CQP_CREATE_CEQ + ((u32)nesdev->ceq_index << 8)));
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_CEQ_WQE_ELEMENT_COUNT_IDX, ceq->ceq_size);
	u64temp = (u64)ceq->ceq_pbase;
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_CQ_WQE_PBL_LOW_IDX, u64temp);

	/* Write Create AEQ WQE */
	cqp_head = nesdev->cqp.sq_head++;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
			(NES_CQP_CREATE_AEQ + ((u32)PCI_FUNC(nesdev->pcidev->devfn) << 8)));
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_AEQ_WQE_ELEMENT_COUNT_IDX, aeq->aeq_size);
	u64temp = (u64)aeq->aeq_pbase;
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_CQ_WQE_PBL_LOW_IDX, u64temp);

	/* Write Create NIC CEQ WQE */
	cqp_head = nesdev->cqp.sq_head++;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
			(NES_CQP_CREATE_CEQ + ((u32)nesdev->nic_ceq_index << 8)));
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_CEQ_WQE_ELEMENT_COUNT_IDX, nic_ceq->ceq_size);
	u64temp = (u64)nic_ceq->ceq_pbase;
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_CQ_WQE_PBL_LOW_IDX, u64temp);

	/* Poll until CCQP done */
	count = 0;
	do {
		if (count++ > 1000) {
			printk(KERN_ERR PFX "Error creating CQP\n");
			pci_free_consistent(nesdev->pcidev, nesdev->cqp_mem_size,
					nesdev->cqp_vbase, nesdev->cqp_pbase);
			return -1;
		}
		udelay(10);
	} while (!(nes_read_indexed(nesdev,
			NES_IDX_QP_CONTROL + (PCI_FUNC(nesdev->pcidev->devfn) * 8)) & (1 << 8)));

	nes_debug(NES_DBG_INIT, "CQP Status = 0x%08X\n", nes_read_indexed(nesdev,
			NES_IDX_QP_CONTROL+(PCI_FUNC(nesdev->pcidev->devfn)*8)));

	u32temp = 0x04800000;
	nes_write32(nesdev->regs+NES_WQE_ALLOC, u32temp | nesdev->cqp.qp_id);

	/* wait for the CCQ, CEQ, and AEQ to get created */
	count = 0;
	do {
		if (count++ > 1000) {
			printk(KERN_ERR PFX "Error creating CCQ, CEQ, and AEQ\n");
			pci_free_consistent(nesdev->pcidev, nesdev->cqp_mem_size,
					nesdev->cqp_vbase, nesdev->cqp_pbase);
			return -1;
		}
		udelay(10);
	} while (((nes_read_indexed(nesdev,
			NES_IDX_QP_CONTROL+(PCI_FUNC(nesdev->pcidev->devfn)*8)) & (15<<8)) != (15<<8)));

	/* dump the QP status value */
	nes_debug(NES_DBG_INIT, "QP Status = 0x%08X\n", nes_read_indexed(nesdev,
			NES_IDX_QP_CONTROL+(PCI_FUNC(nesdev->pcidev->devfn)*8)));

	nesdev->cqp.sq_tail++;

	return 0;
}


/**
 * nes_destroy_cqp
 */
int nes_destroy_cqp(struct nes_device *nesdev)
{
	struct nes_hw_cqp_wqe *cqp_wqe;
	u32 count = 0;
	u32 cqp_head;
	unsigned long flags;

	do {
		if (count++ > 1000)
			break;
		udelay(10);
	} while (!(nesdev->cqp.sq_head == nesdev->cqp.sq_tail));

	/* Reset CCQ */
	nes_write32(nesdev->regs+NES_CQE_ALLOC, NES_CQE_ALLOC_RESET |
			nesdev->ccq.cq_number);

	/* Disable device interrupts */
	nes_write32(nesdev->regs+NES_INT_MASK, 0x7fffffff);

	spin_lock_irqsave(&nesdev->cqp.lock, flags);

	/* Destroy the AEQ */
	cqp_head = nesdev->cqp.sq_head++;
	nesdev->cqp.sq_head &= nesdev->cqp.sq_size-1;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(NES_CQP_DESTROY_AEQ |
			((u32)PCI_FUNC(nesdev->pcidev->devfn) << 8));
	cqp_wqe->wqe_words[NES_CQP_WQE_COMP_CTX_HIGH_IDX] = 0;

	/* Destroy the NIC CEQ */
	cqp_head = nesdev->cqp.sq_head++;
	nesdev->cqp.sq_head &= nesdev->cqp.sq_size-1;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(NES_CQP_DESTROY_CEQ |
			((u32)nesdev->nic_ceq_index << 8));

	/* Destroy the CEQ */
	cqp_head = nesdev->cqp.sq_head++;
	nesdev->cqp.sq_head &= nesdev->cqp.sq_size-1;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(NES_CQP_DESTROY_CEQ |
			(nesdev->ceq_index << 8));

	/* Destroy the CCQ */
	cqp_head = nesdev->cqp.sq_head++;
	nesdev->cqp.sq_head &= nesdev->cqp.sq_size-1;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(NES_CQP_DESTROY_CQ);
	cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX] = cpu_to_le32(nesdev->ccq.cq_number |
			((u32)nesdev->ceq_index << 16));

	/* Destroy CQP */
	cqp_head = nesdev->cqp.sq_head++;
	nesdev->cqp.sq_head &= nesdev->cqp.sq_size-1;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(NES_CQP_DESTROY_QP |
			NES_CQP_QP_TYPE_CQP);
	cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX] = cpu_to_le32(nesdev->cqp.qp_id);

	barrier();
	/* Ring doorbell (5 WQEs) */
	nes_write32(nesdev->regs+NES_WQE_ALLOC, 0x05800000 | nesdev->cqp.qp_id);

	spin_unlock_irqrestore(&nesdev->cqp.lock, flags);

	/* wait for the CCQ, CEQ, and AEQ to get destroyed */
	count = 0;
	do {
		if (count++ > 1000) {
			printk(KERN_ERR PFX "Function%d: Error destroying CCQ, CEQ, and AEQ\n",
					PCI_FUNC(nesdev->pcidev->devfn));
			break;
		}
		udelay(10);
	} while (((nes_read_indexed(nesdev,
			NES_IDX_QP_CONTROL + (PCI_FUNC(nesdev->pcidev->devfn)*8)) & (15 << 8)) != 0));

	/* dump the QP status value */
	nes_debug(NES_DBG_SHUTDOWN, "Function%d: QP Status = 0x%08X\n",
			PCI_FUNC(nesdev->pcidev->devfn),
			nes_read_indexed(nesdev,
			NES_IDX_QP_CONTROL+(PCI_FUNC(nesdev->pcidev->devfn)*8)));

	kfree(nesdev->nes_cqp_requests);

	/* Free the control structures */
	pci_free_consistent(nesdev->pcidev, nesdev->cqp_mem_size, nesdev->cqp.sq_vbase,
			nesdev->cqp.sq_pbase);

	return 0;
}


/**
 * nes_init_phy
 */
int nes_init_phy(struct nes_device *nesdev)
{
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 counter = 0;
	u32 sds_common_control0;
	u32 mac_index = nesdev->mac_index;
	u32 tx_config = 0;
	u16 phy_data;
	u32 temp_phy_data = 0;
	u32 temp_phy_data2 = 0;
	u32 i = 0;

	if ((nesadapter->OneG_Mode) &&
	    (nesadapter->phy_type[mac_index] != NES_PHY_TYPE_PUMA_1G)) {
		nes_debug(NES_DBG_PHY, "1G PHY, mac_index = %d.\n", mac_index);
		if (nesadapter->phy_type[mac_index] == NES_PHY_TYPE_1G) {
			printk(PFX "%s: Programming mdc config for 1G\n", __func__);
			tx_config = nes_read_indexed(nesdev, NES_IDX_MAC_TX_CONFIG);
			tx_config |= 0x04;
			nes_write_indexed(nesdev, NES_IDX_MAC_TX_CONFIG, tx_config);
		}

		nes_read_1G_phy_reg(nesdev, 1, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 1 phy address %u = 0x%X.\n",
				nesadapter->phy_index[mac_index], phy_data);
		nes_write_1G_phy_reg(nesdev, 23, nesadapter->phy_index[mac_index], 0xb000);

		/* Reset the PHY */
		nes_write_1G_phy_reg(nesdev, 0, nesadapter->phy_index[mac_index], 0x8000);
		udelay(100);
		counter = 0;
		do {
			nes_read_1G_phy_reg(nesdev, 0, nesadapter->phy_index[mac_index], &phy_data);
			nes_debug(NES_DBG_PHY, "Phy data from register 0 = 0x%X.\n", phy_data);
			if (counter++ > 100) break;
		} while (phy_data & 0x8000);

		/* Setting no phy loopback */
		phy_data &= 0xbfff;
		phy_data |= 0x1140;
		nes_write_1G_phy_reg(nesdev, 0, nesadapter->phy_index[mac_index],  phy_data);
		nes_read_1G_phy_reg(nesdev, 0, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0 = 0x%X.\n", phy_data);

		nes_read_1G_phy_reg(nesdev, 0x17, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0x17 = 0x%X.\n", phy_data);

		nes_read_1G_phy_reg(nesdev, 0x1e, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0x1e = 0x%X.\n", phy_data);

		/* Setting the interrupt mask */
		nes_read_1G_phy_reg(nesdev, 0x19, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0x19 = 0x%X.\n", phy_data);
		nes_write_1G_phy_reg(nesdev, 0x19, nesadapter->phy_index[mac_index], 0xffee);

		nes_read_1G_phy_reg(nesdev, 0x19, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0x19 = 0x%X.\n", phy_data);

		/* turning on flow control */
		nes_read_1G_phy_reg(nesdev, 4, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0x4 = 0x%X.\n", phy_data);
		nes_write_1G_phy_reg(nesdev, 4, nesadapter->phy_index[mac_index],
				(phy_data & ~(0x03E0)) | 0xc00);
		/* nes_write_1G_phy_reg(nesdev, 4, nesadapter->phy_index[mac_index],
				phy_data | 0xc00); */
		nes_read_1G_phy_reg(nesdev, 4, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0x4 = 0x%X.\n", phy_data);

		nes_read_1G_phy_reg(nesdev, 9, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0x9 = 0x%X.\n", phy_data);
		/* Clear Half duplex */
		nes_write_1G_phy_reg(nesdev, 9, nesadapter->phy_index[mac_index],
				phy_data & ~(0x0100));
		nes_read_1G_phy_reg(nesdev, 9, nesadapter->phy_index[mac_index], &phy_data);
		nes_debug(NES_DBG_PHY, "Phy data from register 0x9 = 0x%X.\n", phy_data);

		nes_read_1G_phy_reg(nesdev, 0, nesadapter->phy_index[mac_index], &phy_data);
		nes_write_1G_phy_reg(nesdev, 0, nesadapter->phy_index[mac_index], phy_data | 0x0300);
	} else {
		if ((nesadapter->phy_type[mac_index] == NES_PHY_TYPE_IRIS) ||
		    (nesadapter->phy_type[mac_index] == NES_PHY_TYPE_ARGUS)) {
			/* setup 10G MDIO operation */
			tx_config = nes_read_indexed(nesdev, NES_IDX_MAC_TX_CONFIG);
			tx_config |= 0x14;
			nes_write_indexed(nesdev, NES_IDX_MAC_TX_CONFIG, tx_config);
		}
		if ((nesadapter->phy_type[mac_index] == NES_PHY_TYPE_ARGUS)) {
			nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x3, 0xd7ee);

			temp_phy_data = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
			mdelay(10);
			nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x3, 0xd7ee);
			temp_phy_data2 = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);

			/*
			 * if firmware is already running (like from a
			 * driver un-load/load, don't do anything.
			 */
			if (temp_phy_data == temp_phy_data2) {
				/* configure QT2505 AMCC PHY */
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0x0000, 0x8000);
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xc300, 0x0000);
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xc302, 0x0044);
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xc318, 0x0052);
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xc319, 0x0008);
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xc31a, 0x0098);
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x3, 0x0026, 0x0E00);
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x3, 0x0027, 0x0000);
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x3, 0x0028, 0xA528);

				/*
				 * remove micro from reset; chip boots from ROM,
				 * uploads EEPROM f/w image, uC executes f/w
				 */
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xc300, 0x0002);

				/*
				 * wait for heart beat to start to
				 * know loading is done
				 */
				counter = 0;
				do {
					nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x3, 0xd7ee);
					temp_phy_data = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
					if (counter++ > 1000) {
						nes_debug(NES_DBG_PHY, "AMCC PHY- breaking from heartbeat check <this is bad!!!> \n");
						break;
					}
					mdelay(100);
					nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x3, 0xd7ee);
					temp_phy_data2 = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
				} while ((temp_phy_data2 == temp_phy_data));

				/*
				 * wait for tracking to start to know
				 * f/w is good to go
				 */
				counter = 0;
				do {
					nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x3, 0xd7fd);
					temp_phy_data = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
					if (counter++ > 1000) {
						nes_debug(NES_DBG_PHY, "AMCC PHY- breaking from status check <this is bad!!!> \n");
						break;
					}
					mdelay(1000);
					/*
					 * nes_debug(NES_DBG_PHY, "AMCC PHY- phy_status not ready yet = 0x%02X\n",
					 *			temp_phy_data);
					 */
				} while (((temp_phy_data & 0xff) != 0x50) && ((temp_phy_data & 0xff) != 0x70));

				/* set LOS Control invert RXLOSB_I_PADINV */
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xd003, 0x0000);
				/* set LOS Control to mask of RXLOSB_I */
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xc314, 0x0042);
				/* set LED1 to input mode (LED1 and LED2 share same LED) */
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xd006, 0x0007);
				/* set LED2 to RX link_status and activity */
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xd007, 0x000A);
				/* set LED3 to RX link_status */
				nes_write_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 0x1, 0xd008, 0x0009);

				/*
				 * reset the res-calibration on t2
				 * serdes; ensures it is stable after
				 * the amcc phy is stable
				 */

				sds_common_control0  = nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL0);
				sds_common_control0 |= 0x1;
				nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL0, sds_common_control0);

				/* release the res-calibration reset */
				sds_common_control0 &= 0xfffffffe;
				nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL0, sds_common_control0);

				i = 0;
				while (((nes_read32(nesdev->regs + NES_SOFTWARE_RESET) & 0x00000040) != 0x00000040)
						&& (i++ < 5000)) {
					/* mdelay(1); */
				}

				/*
				 * wait for link train done before moving on,
				 * or will get an interupt storm
				 */
				counter = 0;
				do {
					temp_phy_data = nes_read_indexed(nesdev, NES_IDX_PHY_PCS_CONTROL_STATUS0 +
								(0x200 * (nesdev->mac_index & 1)));
					if (counter++ > 1000) {
						nes_debug(NES_DBG_PHY, "AMCC PHY- breaking from link train wait <this is bad, link didnt train!!!>\n");
						break;
					}
					mdelay(1);
				} while (((temp_phy_data & 0x0f1f0000) != 0x0f0f0000));
			}
		}
	}
	return 0;
}


/**
 * nes_replenish_nic_rq
 */
static void nes_replenish_nic_rq(struct nes_vnic *nesvnic)
{
	unsigned long flags;
	dma_addr_t bus_address;
	struct sk_buff *skb;
	struct nes_hw_nic_rq_wqe *nic_rqe;
	struct nes_hw_nic *nesnic;
	struct nes_device *nesdev;
	u32 rx_wqes_posted = 0;

	nesnic = &nesvnic->nic;
	nesdev = nesvnic->nesdev;
	spin_lock_irqsave(&nesnic->rq_lock, flags);
	if (nesnic->replenishing_rq !=0) {
		if (((nesnic->rq_size-1) == atomic_read(&nesvnic->rx_skbs_needed)) &&
				(atomic_read(&nesvnic->rx_skb_timer_running) == 0)) {
			atomic_set(&nesvnic->rx_skb_timer_running, 1);
			spin_unlock_irqrestore(&nesnic->rq_lock, flags);
			nesvnic->rq_wqes_timer.expires = jiffies + (HZ/2);	/* 1/2 second */
			add_timer(&nesvnic->rq_wqes_timer);
		} else
		spin_unlock_irqrestore(&nesnic->rq_lock, flags);
		return;
	}
	nesnic->replenishing_rq = 1;
	spin_unlock_irqrestore(&nesnic->rq_lock, flags);
	do {
		skb = dev_alloc_skb(nesvnic->max_frame_size);
		if (skb) {
			skb->dev = nesvnic->netdev;

			bus_address = pci_map_single(nesdev->pcidev,
					skb->data, nesvnic->max_frame_size, PCI_DMA_FROMDEVICE);

			nic_rqe = &nesnic->rq_vbase[nesvnic->nic.rq_head];
			nic_rqe->wqe_words[NES_NIC_RQ_WQE_LENGTH_1_0_IDX] =
					cpu_to_le32(nesvnic->max_frame_size);
			nic_rqe->wqe_words[NES_NIC_RQ_WQE_LENGTH_3_2_IDX] = 0;
			nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_LOW_IDX] =
					cpu_to_le32((u32)bus_address);
			nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_HIGH_IDX] =
					cpu_to_le32((u32)((u64)bus_address >> 32));
			nesnic->rx_skb[nesnic->rq_head] = skb;
			nesnic->rq_head++;
			nesnic->rq_head &= nesnic->rq_size - 1;
			atomic_dec(&nesvnic->rx_skbs_needed);
			barrier();
			if (++rx_wqes_posted == 255) {
				nes_write32(nesdev->regs+NES_WQE_ALLOC, (rx_wqes_posted << 24) | nesnic->qp_id);
				rx_wqes_posted = 0;
			}
		} else {
			spin_lock_irqsave(&nesnic->rq_lock, flags);
			if (((nesnic->rq_size-1) == atomic_read(&nesvnic->rx_skbs_needed)) &&
					(atomic_read(&nesvnic->rx_skb_timer_running) == 0)) {
				atomic_set(&nesvnic->rx_skb_timer_running, 1);
				spin_unlock_irqrestore(&nesnic->rq_lock, flags);
				nesvnic->rq_wqes_timer.expires = jiffies + (HZ/2);	/* 1/2 second */
				add_timer(&nesvnic->rq_wqes_timer);
			} else
				spin_unlock_irqrestore(&nesnic->rq_lock, flags);
			break;
		}
	} while (atomic_read(&nesvnic->rx_skbs_needed));
	barrier();
	if (rx_wqes_posted)
		nes_write32(nesdev->regs+NES_WQE_ALLOC, (rx_wqes_posted << 24) | nesnic->qp_id);
	nesnic->replenishing_rq = 0;
}


/**
 * nes_rq_wqes_timeout
 */
static void nes_rq_wqes_timeout(unsigned long parm)
{
	struct nes_vnic *nesvnic = (struct nes_vnic *)parm;
	printk("%s: Timer fired.\n", __func__);
	atomic_set(&nesvnic->rx_skb_timer_running, 0);
	if (atomic_read(&nesvnic->rx_skbs_needed))
		nes_replenish_nic_rq(nesvnic);
}


static int nes_lro_get_skb_hdr(struct sk_buff *skb, void **iphdr,
			       void **tcph, u64 *hdr_flags, void *priv)
{
	unsigned int ip_len;
	struct iphdr *iph;
	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	if (iph->protocol != IPPROTO_TCP)
		return -1;
	ip_len = ip_hdrlen(skb);
	skb_set_transport_header(skb, ip_len);
	*tcph = tcp_hdr(skb);

	*hdr_flags = LRO_IPV4 | LRO_TCP;
	*iphdr = iph;
	return 0;
}


/**
 * nes_init_nic_qp
 */
int nes_init_nic_qp(struct nes_device *nesdev, struct net_device *netdev)
{
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_hw_nic_sq_wqe *nic_sqe;
	struct nes_hw_nic_qp_context *nic_context;
	struct sk_buff *skb;
	struct nes_hw_nic_rq_wqe *nic_rqe;
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	unsigned long flags;
	void *vmem;
	dma_addr_t pmem;
	u64 u64temp;
	int ret;
	u32 cqp_head;
	u32 counter;
	u32 wqe_count;
	u8 jumbomode=0;

	/* Allocate fragment, SQ, RQ, and CQ; Reuse CEQ based on the PCI function */
	nesvnic->nic_mem_size = 256 +
			(NES_NIC_WQ_SIZE * sizeof(struct nes_first_frag)) +
			(NES_NIC_WQ_SIZE * sizeof(struct nes_hw_nic_sq_wqe)) +
			(NES_NIC_WQ_SIZE * sizeof(struct nes_hw_nic_rq_wqe)) +
			(NES_NIC_WQ_SIZE * 2 * sizeof(struct nes_hw_nic_cqe)) +
			sizeof(struct nes_hw_nic_qp_context);

	nesvnic->nic_vbase = pci_alloc_consistent(nesdev->pcidev, nesvnic->nic_mem_size,
			&nesvnic->nic_pbase);
	if (!nesvnic->nic_vbase) {
		nes_debug(NES_DBG_INIT, "Unable to allocate memory for NIC host descriptor rings\n");
		return -ENOMEM;
	}
	memset(nesvnic->nic_vbase, 0, nesvnic->nic_mem_size);
	nes_debug(NES_DBG_INIT, "Allocated NIC QP structures at %p (phys = %016lX), size = %u.\n",
			nesvnic->nic_vbase, (unsigned long)nesvnic->nic_pbase, nesvnic->nic_mem_size);

	vmem = (void *)(((unsigned long)nesvnic->nic_vbase + (256 - 1)) &
			~(unsigned long)(256 - 1));
	pmem = (dma_addr_t)(((unsigned long long)nesvnic->nic_pbase + (256 - 1)) &
			~(unsigned long long)(256 - 1));

	/* Setup the first Fragment buffers */
	nesvnic->nic.first_frag_vbase = vmem;

	for (counter = 0; counter < NES_NIC_WQ_SIZE; counter++) {
		nesvnic->nic.frag_paddr[counter] = pmem;
		pmem += sizeof(struct nes_first_frag);
	}

	/* setup the SQ */
	vmem += (NES_NIC_WQ_SIZE * sizeof(struct nes_first_frag));

	nesvnic->nic.sq_vbase = (void *)vmem;
	nesvnic->nic.sq_pbase = pmem;
	nesvnic->nic.sq_head = 0;
	nesvnic->nic.sq_tail = 0;
	nesvnic->nic.sq_size = NES_NIC_WQ_SIZE;
	for (counter = 0; counter < NES_NIC_WQ_SIZE; counter++) {
		nic_sqe = &nesvnic->nic.sq_vbase[counter];
		nic_sqe->wqe_words[NES_NIC_SQ_WQE_MISC_IDX] =
				cpu_to_le32(NES_NIC_SQ_WQE_DISABLE_CHKSUM |
				NES_NIC_SQ_WQE_COMPLETION);
		nic_sqe->wqe_words[NES_NIC_SQ_WQE_LENGTH_0_TAG_IDX] =
				cpu_to_le32((u32)NES_FIRST_FRAG_SIZE << 16);
		nic_sqe->wqe_words[NES_NIC_SQ_WQE_FRAG0_LOW_IDX] =
				cpu_to_le32((u32)nesvnic->nic.frag_paddr[counter]);
		nic_sqe->wqe_words[NES_NIC_SQ_WQE_FRAG0_HIGH_IDX] =
				cpu_to_le32((u32)((u64)nesvnic->nic.frag_paddr[counter] >> 32));
	}

	nesvnic->get_cqp_request = nes_get_cqp_request;
	nesvnic->post_cqp_request = nes_post_cqp_request;
	nesvnic->mcrq_mcast_filter = NULL;

	spin_lock_init(&nesvnic->nic.sq_lock);
	spin_lock_init(&nesvnic->nic.rq_lock);

	/* setup the RQ */
	vmem += (NES_NIC_WQ_SIZE * sizeof(struct nes_hw_nic_sq_wqe));
	pmem += (NES_NIC_WQ_SIZE * sizeof(struct nes_hw_nic_sq_wqe));


	nesvnic->nic.rq_vbase = vmem;
	nesvnic->nic.rq_pbase = pmem;
	nesvnic->nic.rq_head = 0;
	nesvnic->nic.rq_tail = 0;
	nesvnic->nic.rq_size = NES_NIC_WQ_SIZE;

	/* setup the CQ */
	vmem += (NES_NIC_WQ_SIZE * sizeof(struct nes_hw_nic_rq_wqe));
	pmem += (NES_NIC_WQ_SIZE * sizeof(struct nes_hw_nic_rq_wqe));

	if (nesdev->nesadapter->netdev_count > 2)
		nesvnic->mcrq_qp_id = nesvnic->nic_index + 32;
	else
		nesvnic->mcrq_qp_id = nesvnic->nic.qp_id + 4;

	nesvnic->nic_cq.cq_vbase = vmem;
	nesvnic->nic_cq.cq_pbase = pmem;
	nesvnic->nic_cq.cq_head = 0;
	nesvnic->nic_cq.cq_size = NES_NIC_WQ_SIZE * 2;

	nesvnic->nic_cq.ce_handler = nes_nic_napi_ce_handler;

	/* Send CreateCQ request to CQP */
	spin_lock_irqsave(&nesdev->cqp.lock, flags);
	cqp_head = nesdev->cqp.sq_head;

	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);

	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(
			NES_CQP_CREATE_CQ | NES_CQP_CQ_CEQ_VALID |
			((u32)nesvnic->nic_cq.cq_size << 16));
	cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX] = cpu_to_le32(
			nesvnic->nic_cq.cq_number | ((u32)nesdev->nic_ceq_index << 16));
	u64temp = (u64)nesvnic->nic_cq.cq_pbase;
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_CQ_WQE_PBL_LOW_IDX, u64temp);
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX] =  0;
	u64temp = (unsigned long)&nesvnic->nic_cq;
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_LOW_IDX] =  cpu_to_le32((u32)(u64temp >> 1));
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX] =
			cpu_to_le32(((u32)((u64temp) >> 33)) & 0x7FFFFFFF);
	cqp_wqe->wqe_words[NES_CQP_CQ_WQE_DOORBELL_INDEX_HIGH_IDX] = 0;
	if (++cqp_head >= nesdev->cqp.sq_size)
		cqp_head = 0;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);

	/* Send CreateQP request to CQP */
	nic_context = (void *)(&nesvnic->nic_cq.cq_vbase[nesvnic->nic_cq.cq_size]);
	nic_context->context_words[NES_NIC_CTX_MISC_IDX] =
			cpu_to_le32((u32)NES_NIC_CTX_SIZE |
			((u32)PCI_FUNC(nesdev->pcidev->devfn) << 12));
	nes_debug(NES_DBG_INIT, "RX_WINDOW_BUFFER_PAGE_TABLE_SIZE = 0x%08X, RX_WINDOW_BUFFER_SIZE = 0x%08X\n",
			nes_read_indexed(nesdev, NES_IDX_RX_WINDOW_BUFFER_PAGE_TABLE_SIZE),
			nes_read_indexed(nesdev, NES_IDX_RX_WINDOW_BUFFER_SIZE));
	if (nes_read_indexed(nesdev, NES_IDX_RX_WINDOW_BUFFER_SIZE) != 0) {
		nic_context->context_words[NES_NIC_CTX_MISC_IDX] |= cpu_to_le32(NES_NIC_BACK_STORE);
	}

	u64temp = (u64)nesvnic->nic.sq_pbase;
	nic_context->context_words[NES_NIC_CTX_SQ_LOW_IDX]  = cpu_to_le32((u32)u64temp);
	nic_context->context_words[NES_NIC_CTX_SQ_HIGH_IDX] = cpu_to_le32((u32)(u64temp >> 32));
	u64temp = (u64)nesvnic->nic.rq_pbase;
	nic_context->context_words[NES_NIC_CTX_RQ_LOW_IDX]  = cpu_to_le32((u32)u64temp);
	nic_context->context_words[NES_NIC_CTX_RQ_HIGH_IDX] = cpu_to_le32((u32)(u64temp >> 32));

	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(NES_CQP_CREATE_QP |
			NES_CQP_QP_TYPE_NIC);
	cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX] = cpu_to_le32(nesvnic->nic.qp_id);
	u64temp = (u64)nesvnic->nic_cq.cq_pbase +
			(nesvnic->nic_cq.cq_size * sizeof(struct nes_hw_nic_cqe));
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_QP_WQE_CONTEXT_LOW_IDX, u64temp);

	if (++cqp_head >= nesdev->cqp.sq_size)
		cqp_head = 0;
	nesdev->cqp.sq_head = cqp_head;

	barrier();

	/* Ring doorbell (2 WQEs) */
	nes_write32(nesdev->regs+NES_WQE_ALLOC, 0x02800000 | nesdev->cqp.qp_id);

	spin_unlock_irqrestore(&nesdev->cqp.lock, flags);
	nes_debug(NES_DBG_INIT, "Waiting for create NIC QP%u to complete.\n",
			nesvnic->nic.qp_id);

	ret = wait_event_timeout(nesdev->cqp.waitq, (nesdev->cqp.sq_tail == cqp_head),
			NES_EVENT_TIMEOUT);
	nes_debug(NES_DBG_INIT, "Create NIC QP%u completed, wait_event_timeout ret = %u.\n",
			nesvnic->nic.qp_id, ret);
	if (!ret) {
		nes_debug(NES_DBG_INIT, "NIC QP%u create timeout expired\n", nesvnic->nic.qp_id);
		pci_free_consistent(nesdev->pcidev, nesvnic->nic_mem_size, nesvnic->nic_vbase,
				nesvnic->nic_pbase);
		return -EIO;
	}

	/* Populate the RQ */
	for (counter = 0; counter < (NES_NIC_WQ_SIZE - 1); counter++) {
		skb = dev_alloc_skb(nesvnic->max_frame_size);
		if (!skb) {
			nes_debug(NES_DBG_INIT, "%s: out of memory for receive skb\n", netdev->name);

			nes_destroy_nic_qp(nesvnic);
			return -ENOMEM;
		}

		skb->dev = netdev;

		pmem = pci_map_single(nesdev->pcidev, skb->data,
				nesvnic->max_frame_size, PCI_DMA_FROMDEVICE);

		nic_rqe = &nesvnic->nic.rq_vbase[counter];
		nic_rqe->wqe_words[NES_NIC_RQ_WQE_LENGTH_1_0_IDX] = cpu_to_le32(nesvnic->max_frame_size);
		nic_rqe->wqe_words[NES_NIC_RQ_WQE_LENGTH_3_2_IDX] = 0;
		nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_LOW_IDX]  = cpu_to_le32((u32)pmem);
		nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_HIGH_IDX] = cpu_to_le32((u32)((u64)pmem >> 32));
		nesvnic->nic.rx_skb[counter] = skb;
	}

	wqe_count = NES_NIC_WQ_SIZE - 1;
	nesvnic->nic.rq_head = wqe_count;
	barrier();
	do {
		counter = min(wqe_count, ((u32)255));
		wqe_count -= counter;
		nes_write32(nesdev->regs+NES_WQE_ALLOC, (counter << 24) | nesvnic->nic.qp_id);
	} while (wqe_count);
	init_timer(&nesvnic->rq_wqes_timer);
	nesvnic->rq_wqes_timer.function = nes_rq_wqes_timeout;
	nesvnic->rq_wqes_timer.data = (unsigned long)nesvnic;
	nes_debug(NES_DBG_INIT, "NAPI support Enabled\n");
	if (nesdev->nesadapter->et_use_adaptive_rx_coalesce)
	{
		nes_nic_init_timer(nesdev);
		if (netdev->mtu > 1500)
			jumbomode = 1;
		nes_nic_init_timer_defaults(nesdev, jumbomode);
	}
	nesvnic->lro_mgr.max_aggr       = nes_lro_max_aggr;
	nesvnic->lro_mgr.max_desc       = NES_MAX_LRO_DESCRIPTORS;
	nesvnic->lro_mgr.lro_arr        = nesvnic->lro_desc;
	nesvnic->lro_mgr.get_skb_header = nes_lro_get_skb_hdr;
	nesvnic->lro_mgr.features       = LRO_F_NAPI | LRO_F_EXTRACT_VLAN_ID;
	nesvnic->lro_mgr.dev            = netdev;
	nesvnic->lro_mgr.ip_summed      = CHECKSUM_UNNECESSARY;
	nesvnic->lro_mgr.ip_summed_aggr = CHECKSUM_UNNECESSARY;
	return 0;
}


/**
 * nes_destroy_nic_qp
 */
void nes_destroy_nic_qp(struct nes_vnic *nesvnic)
{
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_hw_nic_rq_wqe *nic_rqe;
	u64 wqe_frag;
	u32 cqp_head;
	unsigned long flags;
	int ret;

	/* Free remaining NIC receive buffers */
	while (nesvnic->nic.rq_head != nesvnic->nic.rq_tail) {
		nic_rqe   = &nesvnic->nic.rq_vbase[nesvnic->nic.rq_tail];
		wqe_frag  = (u64)le32_to_cpu(nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_LOW_IDX]);
		wqe_frag |= ((u64)le32_to_cpu(nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_HIGH_IDX])) << 32;
		pci_unmap_single(nesdev->pcidev, (dma_addr_t)wqe_frag,
				nesvnic->max_frame_size, PCI_DMA_FROMDEVICE);
		dev_kfree_skb(nesvnic->nic.rx_skb[nesvnic->nic.rq_tail++]);
		nesvnic->nic.rq_tail &= (nesvnic->nic.rq_size - 1);
	}

	spin_lock_irqsave(&nesdev->cqp.lock, flags);

	/* Destroy NIC QP */
	cqp_head = nesdev->cqp.sq_head;
	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);

	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
		(NES_CQP_DESTROY_QP | NES_CQP_QP_TYPE_NIC));
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX,
		nesvnic->nic.qp_id);

	if (++cqp_head >= nesdev->cqp.sq_size)
		cqp_head = 0;

	cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];

	/* Destroy NIC CQ */
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
		(NES_CQP_DESTROY_CQ | ((u32)nesvnic->nic_cq.cq_size << 16)));
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX,
		(nesvnic->nic_cq.cq_number | ((u32)nesdev->nic_ceq_index << 16)));

	if (++cqp_head >= nesdev->cqp.sq_size)
		cqp_head = 0;

	nesdev->cqp.sq_head = cqp_head;
	barrier();

	/* Ring doorbell (2 WQEs) */
	nes_write32(nesdev->regs+NES_WQE_ALLOC, 0x02800000 | nesdev->cqp.qp_id);

	spin_unlock_irqrestore(&nesdev->cqp.lock, flags);
	nes_debug(NES_DBG_SHUTDOWN, "Waiting for CQP, cqp_head=%u, cqp.sq_head=%u,"
			" cqp.sq_tail=%u, cqp.sq_size=%u\n",
			cqp_head, nesdev->cqp.sq_head,
			nesdev->cqp.sq_tail, nesdev->cqp.sq_size);

	ret = wait_event_timeout(nesdev->cqp.waitq, (nesdev->cqp.sq_tail == cqp_head),
			NES_EVENT_TIMEOUT);

	nes_debug(NES_DBG_SHUTDOWN, "Destroy NIC QP returned, wait_event_timeout ret = %u, cqp_head=%u,"
			" cqp.sq_head=%u, cqp.sq_tail=%u\n",
			ret, cqp_head, nesdev->cqp.sq_head, nesdev->cqp.sq_tail);
	if (!ret) {
		nes_debug(NES_DBG_SHUTDOWN, "NIC QP%u destroy timeout expired\n",
				nesvnic->nic.qp_id);
	}

	pci_free_consistent(nesdev->pcidev, nesvnic->nic_mem_size, nesvnic->nic_vbase,
			nesvnic->nic_pbase);
}

/**
 * nes_napi_isr
 */
int nes_napi_isr(struct nes_device *nesdev)
{
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 int_stat;

	if (nesdev->napi_isr_ran) {
		/* interrupt status has already been read in ISR */
		int_stat = nesdev->int_stat;
	} else {
		int_stat = nes_read32(nesdev->regs + NES_INT_STAT);
		nesdev->int_stat = int_stat;
		nesdev->napi_isr_ran = 1;
	}

	int_stat &= nesdev->int_req;
	/* iff NIC, process here, else wait for DPC */
	if ((int_stat) && ((int_stat & 0x0000ff00) == int_stat)) {
		nesdev->napi_isr_ran = 0;
		nes_write32(nesdev->regs + NES_INT_STAT,
			(int_stat &
			~(NES_INT_INTF | NES_INT_TIMER | NES_INT_MAC0 | NES_INT_MAC1 | NES_INT_MAC2 | NES_INT_MAC3)));

		/* Process the CEQs */
		nes_process_ceq(nesdev, &nesdev->nesadapter->ceq[nesdev->nic_ceq_index]);

		if (unlikely((((nesadapter->et_rx_coalesce_usecs_irq) &&
					(!nesadapter->et_use_adaptive_rx_coalesce)) ||
					((nesadapter->et_use_adaptive_rx_coalesce) &&
					 (nesdev->deepcq_count > nesadapter->et_pkt_rate_low))))) {
			if ((nesdev->int_req & NES_INT_TIMER) == 0) {
				/* Enable Periodic timer interrupts */
				nesdev->int_req |= NES_INT_TIMER;
				/* ack any pending periodic timer interrupts so we don't get an immediate interrupt */
				/* TODO: need to also ack other unused periodic timer values, get from nesadapter */
				nes_write32(nesdev->regs+NES_TIMER_STAT,
						nesdev->timer_int_req  | ~(nesdev->nesadapter->timer_int_req));
				nes_write32(nesdev->regs+NES_INTF_INT_MASK,
						~(nesdev->intf_int_req | NES_INTF_PERIODIC_TIMER));
			}

			if (unlikely(nesadapter->et_use_adaptive_rx_coalesce))
			{
				nes_nic_init_timer(nesdev);
			}
			/* Enable interrupts, except CEQs */
			nes_write32(nesdev->regs+NES_INT_MASK, 0x0000ffff | (~nesdev->int_req));
		} else {
			/* Enable interrupts, make sure timer is off */
			nesdev->int_req &= ~NES_INT_TIMER;
			nes_write32(nesdev->regs+NES_INTF_INT_MASK, ~(nesdev->intf_int_req));
			nes_write32(nesdev->regs+NES_INT_MASK, ~nesdev->int_req);
		}
		nesdev->deepcq_count = 0;
		return 1;
	} else {
		return 0;
	}
}


/**
 * nes_dpc
 */
void nes_dpc(unsigned long param)
{
	struct nes_device *nesdev = (struct nes_device *)param;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 counter;
	u32 loop_counter = 0;
	u32 int_status_bit;
	u32 int_stat;
	u32 timer_stat;
	u32 temp_int_stat;
	u32 intf_int_stat;
	u32 debug_error;
	u32 processed_intf_int = 0;
	u16 processed_timer_int = 0;
	u16 completion_ints = 0;
	u16 timer_ints = 0;

	/* nes_debug(NES_DBG_ISR, "\n"); */

	do {
		timer_stat = 0;
		if (nesdev->napi_isr_ran) {
			nesdev->napi_isr_ran = 0;
			int_stat = nesdev->int_stat;
		} else
			int_stat = nes_read32(nesdev->regs+NES_INT_STAT);
		if (processed_intf_int != 0)
			int_stat &= nesdev->int_req & ~NES_INT_INTF;
		else
			int_stat &= nesdev->int_req;
		if (processed_timer_int == 0) {
			processed_timer_int = 1;
			if (int_stat & NES_INT_TIMER) {
				timer_stat = nes_read32(nesdev->regs + NES_TIMER_STAT);
				if ((timer_stat & nesdev->timer_int_req) == 0) {
					int_stat &= ~NES_INT_TIMER;
				}
			}
		} else {
			int_stat &= ~NES_INT_TIMER;
		}

		if (int_stat) {
			if (int_stat & ~(NES_INT_INTF | NES_INT_TIMER | NES_INT_MAC0|
					NES_INT_MAC1|NES_INT_MAC2 | NES_INT_MAC3)) {
				/* Ack the interrupts */
				nes_write32(nesdev->regs+NES_INT_STAT,
					(int_stat & ~(NES_INT_INTF | NES_INT_TIMER | NES_INT_MAC0|
					NES_INT_MAC1 | NES_INT_MAC2 | NES_INT_MAC3)));
			}

			temp_int_stat = int_stat;
			for (counter = 0, int_status_bit = 1; counter < 16; counter++) {
				if (int_stat & int_status_bit) {
					nes_process_ceq(nesdev, &nesadapter->ceq[counter]);
					temp_int_stat &= ~int_status_bit;
					completion_ints = 1;
				}
				if (!(temp_int_stat & 0x0000ffff))
					break;
				int_status_bit <<= 1;
			}

			/* Process the AEQ for this pci function */
			int_status_bit = 1 << (16 + PCI_FUNC(nesdev->pcidev->devfn));
			if (int_stat & int_status_bit) {
				nes_process_aeq(nesdev, &nesadapter->aeq[PCI_FUNC(nesdev->pcidev->devfn)]);
			}

			/* Process the MAC interrupt for this pci function */
			int_status_bit = 1 << (24 + nesdev->mac_index);
			if (int_stat & int_status_bit) {
				nes_process_mac_intr(nesdev, nesdev->mac_index);
			}

			if (int_stat & NES_INT_TIMER) {
				if (timer_stat & nesdev->timer_int_req) {
					nes_write32(nesdev->regs + NES_TIMER_STAT,
							(timer_stat & nesdev->timer_int_req) |
							~(nesdev->nesadapter->timer_int_req));
					timer_ints = 1;
				}
			}

			if (int_stat & NES_INT_INTF) {
				processed_intf_int = 1;
				intf_int_stat = nes_read32(nesdev->regs+NES_INTF_INT_STAT);
				intf_int_stat &= nesdev->intf_int_req;
				if (NES_INTF_INT_CRITERR & intf_int_stat) {
					debug_error = nes_read_indexed(nesdev, NES_IDX_DEBUG_ERROR_CONTROL_STATUS);
					printk(KERN_ERR PFX "Critical Error reported by device!!! 0x%02X\n",
							(u16)debug_error);
					nes_write_indexed(nesdev, NES_IDX_DEBUG_ERROR_CONTROL_STATUS,
							0x01010000 | (debug_error & 0x0000ffff));
					/* BUG(); */
					if (crit_err_count++ > 10)
						nes_write_indexed(nesdev, NES_IDX_DEBUG_ERROR_MASKS1, 1 << 0x17);
				}
				if (NES_INTF_INT_PCIERR & intf_int_stat) {
					printk(KERN_ERR PFX "PCI Error reported by device!!!\n");
					BUG();
				}
				if (NES_INTF_INT_AEQ_OFLOW & intf_int_stat) {
					printk(KERN_ERR PFX "AEQ Overflow reported by device!!!\n");
					BUG();
				}
				nes_write32(nesdev->regs+NES_INTF_INT_STAT, intf_int_stat);
			}

			if (int_stat & NES_INT_TSW) {
			}
		}
		/* Don't use the interface interrupt bit stay in loop */
		int_stat &= ~NES_INT_INTF | NES_INT_TIMER | NES_INT_MAC0 |
				NES_INT_MAC1 | NES_INT_MAC2 | NES_INT_MAC3;
	} while ((int_stat != 0) && (loop_counter++ < MAX_DPC_ITERATIONS));

	if (timer_ints == 1) {
		if ((nesadapter->et_rx_coalesce_usecs_irq) || (nesadapter->et_use_adaptive_rx_coalesce)) {
			if (completion_ints == 0) {
				nesdev->timer_only_int_count++;
				if (nesdev->timer_only_int_count>=nesadapter->timer_int_limit) {
					nesdev->timer_only_int_count = 0;
					nesdev->int_req &= ~NES_INT_TIMER;
					nes_write32(nesdev->regs + NES_INTF_INT_MASK, ~(nesdev->intf_int_req));
					nes_write32(nesdev->regs + NES_INT_MASK, ~nesdev->int_req);
				} else {
					nes_write32(nesdev->regs+NES_INT_MASK, 0x0000ffff | (~nesdev->int_req));
				}
			} else {
				if (unlikely(nesadapter->et_use_adaptive_rx_coalesce))
				{
					nes_nic_init_timer(nesdev);
				}
				nesdev->timer_only_int_count = 0;
				nes_write32(nesdev->regs+NES_INT_MASK, 0x0000ffff | (~nesdev->int_req));
			}
		} else {
			nesdev->timer_only_int_count = 0;
			nesdev->int_req &= ~NES_INT_TIMER;
			nes_write32(nesdev->regs+NES_INTF_INT_MASK, ~(nesdev->intf_int_req));
			nes_write32(nesdev->regs+NES_TIMER_STAT,
					nesdev->timer_int_req | ~(nesdev->nesadapter->timer_int_req));
			nes_write32(nesdev->regs+NES_INT_MASK, ~nesdev->int_req);
		}
	} else {
		if ( (completion_ints == 1) &&
			 (((nesadapter->et_rx_coalesce_usecs_irq) &&
			   (!nesadapter->et_use_adaptive_rx_coalesce)) ||
			  ((nesdev->deepcq_count > nesadapter->et_pkt_rate_low) &&
			   (nesadapter->et_use_adaptive_rx_coalesce) )) ) {
			/* nes_debug(NES_DBG_ISR, "Enabling periodic timer interrupt.\n" ); */
			nesdev->timer_only_int_count = 0;
			nesdev->int_req |= NES_INT_TIMER;
			nes_write32(nesdev->regs+NES_TIMER_STAT,
					nesdev->timer_int_req | ~(nesdev->nesadapter->timer_int_req));
			nes_write32(nesdev->regs+NES_INTF_INT_MASK,
					~(nesdev->intf_int_req | NES_INTF_PERIODIC_TIMER));
			nes_write32(nesdev->regs+NES_INT_MASK, 0x0000ffff | (~nesdev->int_req));
		} else {
			nes_write32(nesdev->regs+NES_INT_MASK, ~nesdev->int_req);
		}
	}
	nesdev->deepcq_count = 0;
}


/**
 * nes_process_ceq
 */
static void nes_process_ceq(struct nes_device *nesdev, struct nes_hw_ceq *ceq)
{
	u64 u64temp;
	struct nes_hw_cq *cq;
	u32 head;
	u32 ceq_size;

	/* nes_debug(NES_DBG_CQ, "\n"); */
	head = ceq->ceq_head;
	ceq_size = ceq->ceq_size;

	do {
		if (le32_to_cpu(ceq->ceq_vbase[head].ceqe_words[NES_CEQE_CQ_CTX_HIGH_IDX]) &
				NES_CEQE_VALID) {
			u64temp = (((u64)(le32_to_cpu(ceq->ceq_vbase[head].ceqe_words[NES_CEQE_CQ_CTX_HIGH_IDX]))) << 32) |
						((u64)(le32_to_cpu(ceq->ceq_vbase[head].ceqe_words[NES_CEQE_CQ_CTX_LOW_IDX])));
			u64temp <<= 1;
			cq = *((struct nes_hw_cq **)&u64temp);
			/* nes_debug(NES_DBG_CQ, "pCQ = %p\n", cq); */
			barrier();
			ceq->ceq_vbase[head].ceqe_words[NES_CEQE_CQ_CTX_HIGH_IDX] = 0;

			/* call the event handler */
			cq->ce_handler(nesdev, cq);

			if (++head >= ceq_size)
				head = 0;
		} else {
			break;
		}

	} while (1);

	ceq->ceq_head = head;
}


/**
 * nes_process_aeq
 */
static void nes_process_aeq(struct nes_device *nesdev, struct nes_hw_aeq *aeq)
{
	/* u64 u64temp; */
	u32 head;
	u32 aeq_size;
	u32 aeqe_misc;
	u32 aeqe_cq_id;
	struct nes_hw_aeqe volatile *aeqe;

	head = aeq->aeq_head;
	aeq_size = aeq->aeq_size;

	do {
		aeqe = &aeq->aeq_vbase[head];
		if ((le32_to_cpu(aeqe->aeqe_words[NES_AEQE_MISC_IDX]) & NES_AEQE_VALID) == 0)
			break;
		aeqe_misc  = le32_to_cpu(aeqe->aeqe_words[NES_AEQE_MISC_IDX]);
		aeqe_cq_id = le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_QP_CQ_ID_IDX]);
		if (aeqe_misc & (NES_AEQE_QP|NES_AEQE_CQ)) {
			if (aeqe_cq_id >= NES_FIRST_QPN) {
				/* dealing with an accelerated QP related AE */
				/*
				 * u64temp = (((u64)(le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_CTXT_HIGH_IDX]))) << 32) |
				 *	     ((u64)(le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_CTXT_LOW_IDX])));
				 */
				nes_process_iwarp_aeqe(nesdev, (struct nes_hw_aeqe *)aeqe);
			} else {
				/* TODO: dealing with a CQP related AE */
				nes_debug(NES_DBG_AEQ, "Processing CQP related AE, misc = 0x%04X\n",
						(u16)(aeqe_misc >> 16));
			}
		}

		aeqe->aeqe_words[NES_AEQE_MISC_IDX] = 0;

		if (++head >= aeq_size)
			head = 0;
	}
	while (1);
	aeq->aeq_head = head;
}

static void nes_reset_link(struct nes_device *nesdev, u32 mac_index)
{
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 reset_value;
	u32 i=0;
	u32 u32temp;

	if (nesadapter->hw_rev == NE020_REV) {
		return;
	}
	mh_detected++;

	reset_value = nes_read32(nesdev->regs+NES_SOFTWARE_RESET);

	if ((mac_index == 0) || ((mac_index == 1) && (nesadapter->OneG_Mode)))
		reset_value |= 0x0000001d;
	else
		reset_value |= 0x0000002d;

	if (4 <= (nesadapter->link_interrupt_count[mac_index] / ((u16)NES_MAX_LINK_INTERRUPTS))) {
		if ((!nesadapter->OneG_Mode) && (nesadapter->port_count == 2)) {
			nesadapter->link_interrupt_count[0] = 0;
			nesadapter->link_interrupt_count[1] = 0;
			u32temp = nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1);
			if (0x00000040 & u32temp)
				nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1, 0x0000F088);
			else
				nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1, 0x0000F0C8);

			reset_value |= 0x0000003d;
		}
		nesadapter->link_interrupt_count[mac_index] = 0;
	}

	nes_write32(nesdev->regs+NES_SOFTWARE_RESET, reset_value);

	while (((nes_read32(nesdev->regs+NES_SOFTWARE_RESET)
			& 0x00000040) != 0x00000040) && (i++ < 5000));

	if (0x0000003d == (reset_value & 0x0000003d)) {
		u32 pcs_control_status0, pcs_control_status1;

		for (i = 0; i < 10; i++) {
			pcs_control_status0 = nes_read_indexed(nesdev, NES_IDX_PHY_PCS_CONTROL_STATUS0);
			pcs_control_status1 = nes_read_indexed(nesdev, NES_IDX_PHY_PCS_CONTROL_STATUS0 + 0x200);
			if (((0x0F000000 == (pcs_control_status0 & 0x0F000000))
			     && (pcs_control_status0 & 0x00100000))
			    || ((0x0F000000 == (pcs_control_status1 & 0x0F000000))
				&& (pcs_control_status1 & 0x00100000)))
				continue;
			else
				break;
		}
		if (10 == i) {
			u32temp = nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1);
			if (0x00000040 & u32temp)
				nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1, 0x0000F088);
			else
				nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL1, 0x0000F0C8);

			nes_write32(nesdev->regs+NES_SOFTWARE_RESET, reset_value);

			while (((nes_read32(nesdev->regs + NES_SOFTWARE_RESET)
				 & 0x00000040) != 0x00000040) && (i++ < 5000));
		}
	}
}

/**
 * nes_process_mac_intr
 */
static void nes_process_mac_intr(struct nes_device *nesdev, u32 mac_number)
{
	unsigned long flags;
	u32 pcs_control_status;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_vnic *nesvnic;
	u32 mac_status;
	u32 mac_index = nesdev->mac_index;
	u32 u32temp;
	u16 phy_data;
	u16 temp_phy_data;
	u32 pcs_val  = 0x0f0f0000;
	u32 pcs_mask = 0x0f1f0000;

	spin_lock_irqsave(&nesadapter->phy_lock, flags);
	if (nesadapter->mac_sw_state[mac_number] != NES_MAC_SW_IDLE) {
		spin_unlock_irqrestore(&nesadapter->phy_lock, flags);
		return;
	}
	nesadapter->mac_sw_state[mac_number] = NES_MAC_SW_INTERRUPT;
	spin_unlock_irqrestore(&nesadapter->phy_lock, flags);

	/* ack the MAC interrupt */
	mac_status = nes_read_indexed(nesdev, NES_IDX_MAC_INT_STATUS + (mac_index * 0x200));
	/* Clear the interrupt */
	nes_write_indexed(nesdev, NES_IDX_MAC_INT_STATUS + (mac_index * 0x200), mac_status);

	nes_debug(NES_DBG_PHY, "MAC%u interrupt status = 0x%X.\n", mac_number, mac_status);

	if (mac_status & (NES_MAC_INT_LINK_STAT_CHG | NES_MAC_INT_XGMII_EXT)) {
		nesdev->link_status_interrupts++;
		if (0 == (++nesadapter->link_interrupt_count[mac_index] % ((u16)NES_MAX_LINK_INTERRUPTS))) {
			spin_lock_irqsave(&nesadapter->phy_lock, flags);
			nes_reset_link(nesdev, mac_index);
			spin_unlock_irqrestore(&nesadapter->phy_lock, flags);
		}
		/* read the PHY interrupt status register */
		if (nesadapter->OneG_Mode) {
			do {
				nes_read_1G_phy_reg(nesdev, 0x1a,
						nesadapter->phy_index[mac_index], &phy_data);
				nes_debug(NES_DBG_PHY, "Phy%d data from register 0x1a = 0x%X.\n",
						nesadapter->phy_index[mac_index], phy_data);
			} while (phy_data&0x8000);

			temp_phy_data = 0;
			do {
				nes_read_1G_phy_reg(nesdev, 0x11,
						nesadapter->phy_index[mac_index], &phy_data);
				nes_debug(NES_DBG_PHY, "Phy%d data from register 0x11 = 0x%X.\n",
						nesadapter->phy_index[mac_index], phy_data);
				if (temp_phy_data == phy_data)
					break;
				temp_phy_data = phy_data;
			} while (1);

			nes_read_1G_phy_reg(nesdev, 0x1e,
					nesadapter->phy_index[mac_index], &phy_data);
			nes_debug(NES_DBG_PHY, "Phy%d data from register 0x1e = 0x%X.\n",
					nesadapter->phy_index[mac_index], phy_data);

			nes_read_1G_phy_reg(nesdev, 1,
					nesadapter->phy_index[mac_index], &phy_data);
			nes_debug(NES_DBG_PHY, "1G phy%u data from register 1 = 0x%X\n",
					nesadapter->phy_index[mac_index], phy_data);

			if (temp_phy_data & 0x1000) {
				nes_debug(NES_DBG_PHY, "The Link is up according to the PHY\n");
				phy_data = 4;
			} else {
				nes_debug(NES_DBG_PHY, "The Link is down according to the PHY\n");
			}
		}
		nes_debug(NES_DBG_PHY, "Eth SERDES Common Status: 0=0x%08X, 1=0x%08X\n",
				nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_STATUS0),
				nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_STATUS0+0x200));

		if (nesadapter->phy_type[mac_index] == NES_PHY_TYPE_PUMA_1G) {
			switch (mac_index) {
			case 1:
			case 3:
				pcs_control_status = nes_read_indexed(nesdev,
						NES_IDX_PHY_PCS_CONTROL_STATUS0 + 0x200);
				break;
			default:
				pcs_control_status = nes_read_indexed(nesdev,
						NES_IDX_PHY_PCS_CONTROL_STATUS0);
				break;
			}
		} else {
			pcs_control_status = nes_read_indexed(nesdev,
					NES_IDX_PHY_PCS_CONTROL_STATUS0 + ((mac_index & 1) * 0x200));
			pcs_control_status = nes_read_indexed(nesdev,
					NES_IDX_PHY_PCS_CONTROL_STATUS0 + ((mac_index & 1) * 0x200));
		}

		nes_debug(NES_DBG_PHY, "PCS PHY Control/Status%u: 0x%08X\n",
				mac_index, pcs_control_status);
		if ((nesadapter->OneG_Mode) &&
				(nesadapter->phy_type[mac_index] != NES_PHY_TYPE_PUMA_1G)) {
			u32temp = 0x01010000;
			if (nesadapter->port_count > 2) {
				u32temp |= 0x02020000;
			}
			if ((pcs_control_status & u32temp)!= u32temp) {
				phy_data = 0;
				nes_debug(NES_DBG_PHY, "PCS says the link is down\n");
			}
		} else {
			switch (nesadapter->phy_type[mac_index]) {
			case NES_PHY_TYPE_IRIS:
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 1, 1);
				temp_phy_data = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
				u32temp = 20;
				do {
					nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 1, 1);
					phy_data = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
					if ((phy_data == temp_phy_data) || (!(--u32temp)))
						break;
					temp_phy_data = phy_data;
				} while (1);
				nes_debug(NES_DBG_PHY, "%s: Phy data = 0x%04X, link was %s.\n",
					__func__, phy_data, nesadapter->mac_link_down[mac_index] ? "DOWN" : "UP");
				break;

			case NES_PHY_TYPE_ARGUS:
				/* clear the alarms */
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 4, 0x0008);
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 4, 0xc001);
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 4, 0xc002);
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 4, 0xc005);
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 4, 0xc006);
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 1, 0x9003);
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 1, 0x9004);
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 1, 0x9005);
				/* check link status */
				nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 1, 1);
				temp_phy_data = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
				u32temp = 100;
				do {
					nes_read_10G_phy_reg(nesdev, nesadapter->phy_index[mac_index], 1, 1);

					phy_data = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
					if ((phy_data == temp_phy_data) || (!(--u32temp)))
						break;
					temp_phy_data = phy_data;
				} while (1);
				nes_debug(NES_DBG_PHY, "%s: Phy data = 0x%04X, link was %s.\n",
					__func__, phy_data, nesadapter->mac_link_down ? "DOWN" : "UP");
				break;

			case NES_PHY_TYPE_PUMA_1G:
				if (mac_index < 2)
					pcs_val = pcs_mask = 0x01010000;
				else
					pcs_val = pcs_mask = 0x02020000;
				/* fall through */
			default:
				phy_data = (pcs_val == (pcs_control_status & pcs_mask)) ? 0x4 : 0x0;
				break;
			}
		}

		if (phy_data & 0x0004) {
			nesadapter->mac_link_down[mac_index] = 0;
			list_for_each_entry(nesvnic, &nesadapter->nesvnic_list[mac_index], list) {
				nes_debug(NES_DBG_PHY, "The Link is UP!!.  linkup was %d\n",
						nesvnic->linkup);
				if (nesvnic->linkup == 0) {
					printk(PFX "The Link is now up for port %s, netdev %p.\n",
							nesvnic->netdev->name, nesvnic->netdev);
					if (netif_queue_stopped(nesvnic->netdev))
						netif_start_queue(nesvnic->netdev);
					nesvnic->linkup = 1;
					netif_carrier_on(nesvnic->netdev);
				}
			}
		} else {
			nesadapter->mac_link_down[mac_index] = 1;
			list_for_each_entry(nesvnic, &nesadapter->nesvnic_list[mac_index], list) {
				nes_debug(NES_DBG_PHY, "The Link is Down!!. linkup was %d\n",
						nesvnic->linkup);
				if (nesvnic->linkup == 1) {
					printk(PFX "The Link is now down for port %s, netdev %p.\n",
							nesvnic->netdev->name, nesvnic->netdev);
					if (!(netif_queue_stopped(nesvnic->netdev)))
						netif_stop_queue(nesvnic->netdev);
					nesvnic->linkup = 0;
					netif_carrier_off(nesvnic->netdev);
				}
			}
		}
	}

	nesadapter->mac_sw_state[mac_number] = NES_MAC_SW_IDLE;
}



static void nes_nic_napi_ce_handler(struct nes_device *nesdev, struct nes_hw_nic_cq *cq)
{
	struct nes_vnic *nesvnic = container_of(cq, struct nes_vnic, nic_cq);

	netif_rx_schedule(nesdev->netdev[nesvnic->netdev_index], &nesvnic->napi);
}


/* The MAX_RQES_TO_PROCESS defines how many max read requests to complete before
* getting out of nic_ce_handler
*/
#define	MAX_RQES_TO_PROCESS	384

/**
 * nes_nic_ce_handler
 */
void nes_nic_ce_handler(struct nes_device *nesdev, struct nes_hw_nic_cq *cq)
{
	u64 u64temp;
	dma_addr_t bus_address;
	struct nes_hw_nic *nesnic;
	struct nes_vnic *nesvnic = container_of(cq, struct nes_vnic, nic_cq);
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_hw_nic_rq_wqe *nic_rqe;
	struct nes_hw_nic_sq_wqe *nic_sqe;
	struct sk_buff *skb;
	struct sk_buff *rx_skb;
	__le16 *wqe_fragment_length;
	u32 head;
	u32 cq_size;
	u32 rx_pkt_size;
	u32 cqe_count=0;
	u32 cqe_errv;
	u32 cqe_misc;
	u16 wqe_fragment_index = 1;	/* first fragment (0) is used by copy buffer */
	u16 vlan_tag;
	u16 pkt_type;
	u16 rqes_processed = 0;
	u8 sq_cqes = 0;
	u8 nes_use_lro = 0;

	head = cq->cq_head;
	cq_size = cq->cq_size;
	cq->cqes_pending = 1;
	if (nesvnic->netdev->features & NETIF_F_LRO)
		nes_use_lro = 1;
	do {
		if (le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_NIC_CQE_MISC_IDX]) &
				NES_NIC_CQE_VALID) {
			nesnic = &nesvnic->nic;
			cqe_misc = le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_NIC_CQE_MISC_IDX]);
			if (cqe_misc & NES_NIC_CQE_SQ) {
				sq_cqes++;
				wqe_fragment_index = 1;
				nic_sqe = &nesnic->sq_vbase[nesnic->sq_tail];
				skb = nesnic->tx_skb[nesnic->sq_tail];
				wqe_fragment_length = (__le16 *)&nic_sqe->wqe_words[NES_NIC_SQ_WQE_LENGTH_0_TAG_IDX];
				/* bump past the vlan tag */
				wqe_fragment_length++;
				if (le16_to_cpu(wqe_fragment_length[wqe_fragment_index]) != 0) {
					u64temp = (u64) le32_to_cpu(nic_sqe->wqe_words[NES_NIC_SQ_WQE_FRAG0_LOW_IDX +
							wqe_fragment_index * 2]);
					u64temp += ((u64)le32_to_cpu(nic_sqe->wqe_words[NES_NIC_SQ_WQE_FRAG0_HIGH_IDX +
							wqe_fragment_index * 2])) << 32;
					bus_address = (dma_addr_t)u64temp;
					if (test_and_clear_bit(nesnic->sq_tail, nesnic->first_frag_overflow)) {
						pci_unmap_single(nesdev->pcidev,
								bus_address,
								le16_to_cpu(wqe_fragment_length[wqe_fragment_index++]),
								PCI_DMA_TODEVICE);
					}
					for (; wqe_fragment_index < 5; wqe_fragment_index++) {
						if (wqe_fragment_length[wqe_fragment_index]) {
							u64temp = le32_to_cpu(nic_sqe->wqe_words[NES_NIC_SQ_WQE_FRAG0_LOW_IDX +
										wqe_fragment_index * 2]);
							u64temp += ((u64)le32_to_cpu(nic_sqe->wqe_words[NES_NIC_SQ_WQE_FRAG0_HIGH_IDX
										+ wqe_fragment_index * 2])) <<32;
							bus_address = (dma_addr_t)u64temp;
							pci_unmap_page(nesdev->pcidev,
									bus_address,
									le16_to_cpu(wqe_fragment_length[wqe_fragment_index]),
									PCI_DMA_TODEVICE);
						} else
							break;
					}
					if (skb)
						dev_kfree_skb_any(skb);
				}
				nesnic->sq_tail++;
				nesnic->sq_tail &= nesnic->sq_size-1;
				if (sq_cqes > 128) {
					barrier();
				/* restart the queue if it had been stopped */
				if (netif_queue_stopped(nesvnic->netdev))
					netif_wake_queue(nesvnic->netdev);
					sq_cqes = 0;
				}
			} else {
				rqes_processed ++;

				cq->rx_cqes_completed++;
				cq->rx_pkts_indicated++;
				rx_pkt_size = cqe_misc & 0x0000ffff;
				nic_rqe = &nesnic->rq_vbase[nesnic->rq_tail];
				/* Get the skb */
				rx_skb = nesnic->rx_skb[nesnic->rq_tail];
				nic_rqe = &nesnic->rq_vbase[nesvnic->nic.rq_tail];
				bus_address = (dma_addr_t)le32_to_cpu(nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_LOW_IDX]);
				bus_address += ((u64)le32_to_cpu(nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_HIGH_IDX])) << 32;
				pci_unmap_single(nesdev->pcidev, bus_address,
						nesvnic->max_frame_size, PCI_DMA_FROMDEVICE);
				/* rx_skb->tail = rx_skb->data + rx_pkt_size; */
				/* rx_skb->len = rx_pkt_size; */
				rx_skb->len = 0;  /* TODO: see if this is necessary */
				skb_put(rx_skb, rx_pkt_size);
				rx_skb->protocol = eth_type_trans(rx_skb, nesvnic->netdev);
				nesnic->rq_tail++;
				nesnic->rq_tail &= nesnic->rq_size - 1;

				atomic_inc(&nesvnic->rx_skbs_needed);
				if (atomic_read(&nesvnic->rx_skbs_needed) > (nesvnic->nic.rq_size>>1)) {
					nes_write32(nesdev->regs+NES_CQE_ALLOC,
							cq->cq_number | (cqe_count << 16));
					/* nesadapter->tune_timer.cq_count += cqe_count; */
					nesdev->currcq_count += cqe_count;
					cqe_count = 0;
					nes_replenish_nic_rq(nesvnic);
				}
				pkt_type = (u16)(le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_NIC_CQE_TAG_PKT_TYPE_IDX]));
				cqe_errv = (cqe_misc & NES_NIC_CQE_ERRV_MASK) >> NES_NIC_CQE_ERRV_SHIFT;
				rx_skb->ip_summed = CHECKSUM_NONE;

				if ((NES_PKT_TYPE_TCPV4_BITS == (pkt_type & NES_PKT_TYPE_TCPV4_MASK)) ||
						(NES_PKT_TYPE_UDPV4_BITS == (pkt_type & NES_PKT_TYPE_UDPV4_MASK))) {
					if ((cqe_errv &
							(NES_NIC_ERRV_BITS_IPV4_CSUM_ERR | NES_NIC_ERRV_BITS_TCPUDP_CSUM_ERR |
							NES_NIC_ERRV_BITS_IPH_ERR | NES_NIC_ERRV_BITS_WQE_OVERRUN)) == 0) {
						if (nesvnic->rx_checksum_disabled == 0) {
							rx_skb->ip_summed = CHECKSUM_UNNECESSARY;
						}
					} else
						nes_debug(NES_DBG_CQ, "%s: unsuccessfully checksummed TCP or UDP packet."
								" errv = 0x%X, pkt_type = 0x%X.\n",
								nesvnic->netdev->name, cqe_errv, pkt_type);

				} else if ((pkt_type & NES_PKT_TYPE_IPV4_MASK) == NES_PKT_TYPE_IPV4_BITS) {
					if ((cqe_errv &
							(NES_NIC_ERRV_BITS_IPV4_CSUM_ERR | NES_NIC_ERRV_BITS_IPH_ERR |
							NES_NIC_ERRV_BITS_WQE_OVERRUN)) == 0) {
						if (nesvnic->rx_checksum_disabled == 0) {
							rx_skb->ip_summed = CHECKSUM_UNNECESSARY;
							/* nes_debug(NES_DBG_CQ, "%s: Reporting successfully checksummed IPv4 packet.\n",
								  nesvnic->netdev->name); */
						}
					} else
						nes_debug(NES_DBG_CQ, "%s: unsuccessfully checksummed TCP or UDP packet."
								" errv = 0x%X, pkt_type = 0x%X.\n",
								nesvnic->netdev->name, cqe_errv, pkt_type);
					}
				/* nes_debug(NES_DBG_CQ, "pkt_type=%x, APBVT_MASK=%x\n",
							pkt_type, (pkt_type & NES_PKT_TYPE_APBVT_MASK)); */

				if ((pkt_type & NES_PKT_TYPE_APBVT_MASK) == NES_PKT_TYPE_APBVT_BITS) {
					nes_cm_recv(rx_skb, nesvnic->netdev);
				} else {
					if ((cqe_misc & NES_NIC_CQE_TAG_VALID) && (nesvnic->vlan_grp != NULL)) {
						vlan_tag = (u16)(le32_to_cpu(
								cq->cq_vbase[head].cqe_words[NES_NIC_CQE_TAG_PKT_TYPE_IDX])
								>> 16);
						nes_debug(NES_DBG_CQ, "%s: Reporting stripped VLAN packet. Tag = 0x%04X\n",
								nesvnic->netdev->name, vlan_tag);
						if (nes_use_lro)
							lro_vlan_hwaccel_receive_skb(&nesvnic->lro_mgr, rx_skb,
									nesvnic->vlan_grp, vlan_tag, NULL);
						else
							nes_vlan_rx(rx_skb, nesvnic->vlan_grp, vlan_tag);
					} else {
						if (nes_use_lro)
							lro_receive_skb(&nesvnic->lro_mgr, rx_skb, NULL);
						else
							nes_netif_rx(rx_skb);
					}
				}

				nesvnic->netdev->last_rx = jiffies;
				/* nesvnic->netstats.rx_packets++; */
				/* nesvnic->netstats.rx_bytes += rx_pkt_size; */
			}

			cq->cq_vbase[head].cqe_words[NES_NIC_CQE_MISC_IDX] = 0;
			/* Accounting... */
			cqe_count++;
			if (++head >= cq_size)
				head = 0;
			if (cqe_count == 255) {
				/* Replenish Nic CQ */
				nes_write32(nesdev->regs+NES_CQE_ALLOC,
						cq->cq_number | (cqe_count << 16));
				/* nesdev->nesadapter->tune_timer.cq_count += cqe_count; */
				nesdev->currcq_count += cqe_count;
				cqe_count = 0;
			}

			if (cq->rx_cqes_completed >= nesvnic->budget)
				break;
		} else {
			cq->cqes_pending = 0;
			break;
		}

	} while (1);

	if (nes_use_lro)
		lro_flush_all(&nesvnic->lro_mgr);
	if (sq_cqes) {
		barrier();
		/* restart the queue if it had been stopped */
		if (netif_queue_stopped(nesvnic->netdev))
			netif_wake_queue(nesvnic->netdev);
	}
	cq->cq_head = head;
	/* nes_debug(NES_DBG_CQ, "CQ%u Processed = %u cqes, new head = %u.\n",
			cq->cq_number, cqe_count, cq->cq_head); */
	cq->cqe_allocs_pending = cqe_count;
	if (unlikely(nesadapter->et_use_adaptive_rx_coalesce))
	{
		/* nesdev->nesadapter->tune_timer.cq_count += cqe_count; */
		nesdev->currcq_count += cqe_count;
		nes_nic_tune_timer(nesdev);
	}
	if (atomic_read(&nesvnic->rx_skbs_needed))
		nes_replenish_nic_rq(nesvnic);
}


/**
 * nes_cqp_ce_handler
 */
static void nes_cqp_ce_handler(struct nes_device *nesdev, struct nes_hw_cq *cq)
{
	u64 u64temp;
	unsigned long flags;
	struct nes_hw_cqp *cqp = NULL;
	struct nes_cqp_request *cqp_request;
	struct nes_hw_cqp_wqe *cqp_wqe;
	u32 head;
	u32 cq_size;
	u32 cqe_count=0;
	u32 error_code;
	/* u32 counter; */

	head = cq->cq_head;
	cq_size = cq->cq_size;

	do {
		/* process the CQE */
		/* nes_debug(NES_DBG_CQP, "head=%u cqe_words=%08X\n", head,
			  le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_CQE_OPCODE_IDX])); */

		if (le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_CQE_OPCODE_IDX]) & NES_CQE_VALID) {
			u64temp = (((u64)(le32_to_cpu(cq->cq_vbase[head].
					cqe_words[NES_CQE_COMP_COMP_CTX_HIGH_IDX]))) << 32) |
					((u64)(le32_to_cpu(cq->cq_vbase[head].
					cqe_words[NES_CQE_COMP_COMP_CTX_LOW_IDX])));
			cqp = *((struct nes_hw_cqp **)&u64temp);

			error_code = le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_CQE_ERROR_CODE_IDX]);
			if (error_code) {
				nes_debug(NES_DBG_CQP, "Bad Completion code for opcode 0x%02X from CQP,"
						" Major/Minor codes = 0x%04X:%04X.\n",
						le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_CQE_OPCODE_IDX])&0x3f,
						(u16)(error_code >> 16),
						(u16)error_code);
				nes_debug(NES_DBG_CQP, "cqp: qp_id=%u, sq_head=%u, sq_tail=%u\n",
						cqp->qp_id, cqp->sq_head, cqp->sq_tail);
			}

			u64temp = (((u64)(le32_to_cpu(nesdev->cqp.sq_vbase[cqp->sq_tail].
					wqe_words[NES_CQP_WQE_COMP_SCRATCH_HIGH_IDX]))) << 32) |
					((u64)(le32_to_cpu(nesdev->cqp.sq_vbase[cqp->sq_tail].
					wqe_words[NES_CQP_WQE_COMP_SCRATCH_LOW_IDX])));
			cqp_request = *((struct nes_cqp_request **)&u64temp);
			if (cqp_request) {
				if (cqp_request->waiting) {
					/* nes_debug(NES_DBG_CQP, "%s: Waking up requestor\n"); */
					cqp_request->major_code = (u16)(error_code >> 16);
					cqp_request->minor_code = (u16)error_code;
					barrier();
					cqp_request->request_done = 1;
					wake_up(&cqp_request->waitq);
					nes_put_cqp_request(nesdev, cqp_request);
				} else {
					if (cqp_request->callback)
						cqp_request->cqp_callback(nesdev, cqp_request);
					nes_free_cqp_request(nesdev, cqp_request);
				}
			} else {
				wake_up(&nesdev->cqp.waitq);
			}

			cq->cq_vbase[head].cqe_words[NES_CQE_OPCODE_IDX] = 0;
			nes_write32(nesdev->regs + NES_CQE_ALLOC, cq->cq_number | (1 << 16));
			if (++cqp->sq_tail >= cqp->sq_size)
				cqp->sq_tail = 0;

			/* Accounting... */
			cqe_count++;
			if (++head >= cq_size)
				head = 0;
		} else {
			break;
		}
	} while (1);
	cq->cq_head = head;

	spin_lock_irqsave(&nesdev->cqp.lock, flags);
	while ((!list_empty(&nesdev->cqp_pending_reqs)) &&
			((((nesdev->cqp.sq_tail+nesdev->cqp.sq_size)-nesdev->cqp.sq_head) &
			(nesdev->cqp.sq_size - 1)) != 1)) {
		cqp_request = list_entry(nesdev->cqp_pending_reqs.next,
				struct nes_cqp_request, list);
		list_del_init(&cqp_request->list);
		head = nesdev->cqp.sq_head++;
		nesdev->cqp.sq_head &= nesdev->cqp.sq_size-1;
		cqp_wqe = &nesdev->cqp.sq_vbase[head];
		memcpy(cqp_wqe, &cqp_request->cqp_wqe, sizeof(*cqp_wqe));
		barrier();
		cqp_wqe->wqe_words[NES_CQP_WQE_COMP_SCRATCH_LOW_IDX] =
			cpu_to_le32((u32)((unsigned long)cqp_request));
		cqp_wqe->wqe_words[NES_CQP_WQE_COMP_SCRATCH_HIGH_IDX] =
			cpu_to_le32((u32)(upper_32_bits((unsigned long)cqp_request)));
		nes_debug(NES_DBG_CQP, "CQP request %p (opcode 0x%02X) put on CQPs SQ wqe%u.\n",
				cqp_request, le32_to_cpu(cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX])&0x3f, head);
		/* Ring doorbell (1 WQEs) */
		barrier();
		nes_write32(nesdev->regs+NES_WQE_ALLOC, 0x01800000 | nesdev->cqp.qp_id);
	}
	spin_unlock_irqrestore(&nesdev->cqp.lock, flags);

	/* Arm the CCQ */
	nes_write32(nesdev->regs+NES_CQE_ALLOC, NES_CQE_ALLOC_NOTIFY_NEXT |
			cq->cq_number);
	nes_read32(nesdev->regs+NES_CQE_ALLOC);
}


/**
 * nes_process_iwarp_aeqe
 */
static void nes_process_iwarp_aeqe(struct nes_device *nesdev,
				   struct nes_hw_aeqe *aeqe)
{
	u64 context;
	u64 aeqe_context = 0;
	unsigned long flags;
	struct nes_qp *nesqp;
	int resource_allocated;
	/* struct iw_cm_id *cm_id; */
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct ib_event ibevent;
	/* struct iw_cm_event cm_event; */
	u32 aeq_info;
	u32 next_iwarp_state = 0;
	u16 async_event_id;
	u8 tcp_state;
	u8 iwarp_state;

	nes_debug(NES_DBG_AEQ, "\n");
	aeq_info = le32_to_cpu(aeqe->aeqe_words[NES_AEQE_MISC_IDX]);
	if ((NES_AEQE_INBOUND_RDMA&aeq_info) || (!(NES_AEQE_QP&aeq_info))) {
		context  = le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_CTXT_LOW_IDX]);
		context += ((u64)le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_CTXT_HIGH_IDX])) << 32;
	} else {
		aeqe_context = le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_CTXT_LOW_IDX]);
		aeqe_context += ((u64)le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_CTXT_HIGH_IDX])) << 32;
		context = (unsigned long)nesadapter->qp_table[le32_to_cpu(
						aeqe->aeqe_words[NES_AEQE_COMP_QP_CQ_ID_IDX]) - NES_FIRST_QPN];
		BUG_ON(!context);
	}

	async_event_id = (u16)aeq_info;
	tcp_state = (aeq_info & NES_AEQE_TCP_STATE_MASK) >> NES_AEQE_TCP_STATE_SHIFT;
	iwarp_state = (aeq_info & NES_AEQE_IWARP_STATE_MASK) >> NES_AEQE_IWARP_STATE_SHIFT;
	nes_debug(NES_DBG_AEQ, "aeid = 0x%04X, qp-cq id = %d, aeqe = %p,"
			" Tcp state = %s, iWARP state = %s\n",
			async_event_id,
			le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_QP_CQ_ID_IDX]), aeqe,
			nes_tcp_state_str[tcp_state], nes_iwarp_state_str[iwarp_state]);

	switch (async_event_id) {
		case NES_AEQE_AEID_LLP_FIN_RECEIVED:
			nesqp = *((struct nes_qp **)&context);
			if (atomic_inc_return(&nesqp->close_timer_started) == 1) {
				nesqp->cm_id->add_ref(nesqp->cm_id);
				schedule_nes_timer(nesqp->cm_node, (struct sk_buff *)nesqp,
						NES_TIMER_TYPE_CLOSE, 1, 0);
				nes_debug(NES_DBG_AEQ, "QP%u Not decrementing QP refcount (%d),"
						" need ae to finish up, original_last_aeq = 0x%04X."
						" last_aeq = 0x%04X, scheduling timer. TCP state = %d\n",
						nesqp->hwqp.qp_id, atomic_read(&nesqp->refcount),
						async_event_id, nesqp->last_aeq, tcp_state);
			}
			if ((tcp_state != NES_AEQE_TCP_STATE_CLOSE_WAIT) ||
					(nesqp->ibqp_state != IB_QPS_RTS)) {
				/* FIN Received but tcp state or IB state moved on,
						should expect a	close complete */
				return;
			}
		case NES_AEQE_AEID_LLP_CLOSE_COMPLETE:
		case NES_AEQE_AEID_LLP_CONNECTION_RESET:
		case NES_AEQE_AEID_TERMINATE_SENT:
		case NES_AEQE_AEID_RDMAP_ROE_BAD_LLP_CLOSE:
		case NES_AEQE_AEID_RESET_SENT:
			nesqp = *((struct nes_qp **)&context);
			if (async_event_id == NES_AEQE_AEID_RESET_SENT) {
				tcp_state = NES_AEQE_TCP_STATE_CLOSED;
			}
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = iwarp_state;
			nesqp->hw_tcp_state = tcp_state;
			nesqp->last_aeq = async_event_id;

			if ((tcp_state == NES_AEQE_TCP_STATE_CLOSED) ||
					(tcp_state == NES_AEQE_TCP_STATE_TIME_WAIT)) {
				nesqp->hte_added = 0;
				spin_unlock_irqrestore(&nesqp->lock, flags);
				nes_debug(NES_DBG_AEQ, "issuing hw modifyqp for QP%u to remove hte\n",
						nesqp->hwqp.qp_id);
				nes_hw_modify_qp(nesdev, nesqp,
						NES_CQP_QP_IWARP_STATE_ERROR | NES_CQP_QP_DEL_HTE, 0);
				spin_lock_irqsave(&nesqp->lock, flags);
			}

			if ((nesqp->ibqp_state == IB_QPS_RTS) &&
					((tcp_state == NES_AEQE_TCP_STATE_CLOSE_WAIT) ||
					(async_event_id == NES_AEQE_AEID_LLP_CONNECTION_RESET))) {
				switch (nesqp->hw_iwarp_state) {
					case NES_AEQE_IWARP_STATE_RTS:
						next_iwarp_state = NES_CQP_QP_IWARP_STATE_CLOSING;
						nesqp->hw_iwarp_state = NES_AEQE_IWARP_STATE_CLOSING;
						break;
					case NES_AEQE_IWARP_STATE_TERMINATE:
						next_iwarp_state = NES_CQP_QP_IWARP_STATE_TERMINATE;
						nesqp->hw_iwarp_state = NES_AEQE_IWARP_STATE_TERMINATE;
						if (async_event_id == NES_AEQE_AEID_RDMAP_ROE_BAD_LLP_CLOSE) {
							next_iwarp_state |= 0x02000000;
							nesqp->hw_tcp_state = NES_AEQE_TCP_STATE_CLOSED;
						}
						break;
					default:
						next_iwarp_state = 0;
				}
				spin_unlock_irqrestore(&nesqp->lock, flags);
				if (next_iwarp_state) {
					nes_debug(NES_DBG_AEQ, "issuing hw modifyqp for QP%u. next state = 0x%08X,"
							" also added another reference\n",
							nesqp->hwqp.qp_id, next_iwarp_state);
					nes_hw_modify_qp(nesdev, nesqp, next_iwarp_state, 0);
				}
				nes_cm_disconn(nesqp);
			} else {
				if (async_event_id ==  NES_AEQE_AEID_LLP_FIN_RECEIVED) {
					/* FIN Received but ib state not RTS,
							close complete will be on its way */
					spin_unlock_irqrestore(&nesqp->lock, flags);
					return;
				}
				spin_unlock_irqrestore(&nesqp->lock, flags);
				if (async_event_id == NES_AEQE_AEID_RDMAP_ROE_BAD_LLP_CLOSE) {
					next_iwarp_state = NES_CQP_QP_IWARP_STATE_TERMINATE | 0x02000000;
					nesqp->hw_tcp_state = NES_AEQE_TCP_STATE_CLOSED;
					nes_debug(NES_DBG_AEQ, "issuing hw modifyqp for QP%u. next state = 0x%08X,"
							" also added another reference\n",
							nesqp->hwqp.qp_id, next_iwarp_state);
					nes_hw_modify_qp(nesdev, nesqp, next_iwarp_state, 0);
				}
				nes_cm_disconn(nesqp);
			}
			break;
		case NES_AEQE_AEID_LLP_TERMINATE_RECEIVED:
			nesqp = *((struct nes_qp **)&context);
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = iwarp_state;
			nesqp->hw_tcp_state = tcp_state;
			nesqp->last_aeq = async_event_id;
			spin_unlock_irqrestore(&nesqp->lock, flags);
			nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_LLP_TERMINATE_RECEIVED"
					" event on QP%u \n  Q2 Data:\n",
					nesqp->hwqp.qp_id);
			if (nesqp->ibqp.event_handler) {
				ibevent.device = nesqp->ibqp.device;
				ibevent.element.qp = &nesqp->ibqp;
				ibevent.event = IB_EVENT_QP_FATAL;
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
			}
			if ((tcp_state == NES_AEQE_TCP_STATE_CLOSE_WAIT) ||
					((nesqp->ibqp_state == IB_QPS_RTS)&&
					(async_event_id == NES_AEQE_AEID_LLP_CONNECTION_RESET))) {
				nes_cm_disconn(nesqp);
			} else {
				nesqp->in_disconnect = 0;
				wake_up(&nesqp->kick_waitq);
			}
			break;
		case NES_AEQE_AEID_LLP_TOO_MANY_RETRIES:
			nesqp = *((struct nes_qp **)&context);
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = NES_AEQE_IWARP_STATE_ERROR;
			nesqp->hw_tcp_state = NES_AEQE_TCP_STATE_CLOSED;
			nesqp->last_aeq = async_event_id;
			if (nesqp->cm_id) {
				nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_LLP_TOO_MANY_RETRIES"
						" event on QP%u, remote IP = 0x%08X \n",
						nesqp->hwqp.qp_id,
						ntohl(nesqp->cm_id->remote_addr.sin_addr.s_addr));
			} else {
				nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_LLP_TOO_MANY_RETRIES"
						" event on QP%u \n",
						nesqp->hwqp.qp_id);
			}
			spin_unlock_irqrestore(&nesqp->lock, flags);
			next_iwarp_state = NES_CQP_QP_IWARP_STATE_ERROR | NES_CQP_QP_RESET;
			nes_hw_modify_qp(nesdev, nesqp, next_iwarp_state, 0);
			if (nesqp->ibqp.event_handler) {
				ibevent.device = nesqp->ibqp.device;
				ibevent.element.qp = &nesqp->ibqp;
				ibevent.event = IB_EVENT_QP_FATAL;
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
			}
			break;
		case NES_AEQE_AEID_AMP_BAD_STAG_INDEX:
			if (NES_AEQE_INBOUND_RDMA&aeq_info) {
				nesqp = nesadapter->qp_table[le32_to_cpu(
						aeqe->aeqe_words[NES_AEQE_COMP_QP_CQ_ID_IDX])-NES_FIRST_QPN];
			} else {
				/* TODO: get the actual WQE and mask off wqe index */
				context &= ~((u64)511);
				nesqp = *((struct nes_qp **)&context);
			}
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = iwarp_state;
			nesqp->hw_tcp_state = tcp_state;
			nesqp->last_aeq = async_event_id;
			spin_unlock_irqrestore(&nesqp->lock, flags);
			nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_AMP_BAD_STAG_INDEX event on QP%u\n",
					nesqp->hwqp.qp_id);
			if (nesqp->ibqp.event_handler) {
				ibevent.device = nesqp->ibqp.device;
				ibevent.element.qp = &nesqp->ibqp;
				ibevent.event = IB_EVENT_QP_ACCESS_ERR;
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
			}
			break;
		case NES_AEQE_AEID_AMP_UNALLOCATED_STAG:
			nesqp = *((struct nes_qp **)&context);
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = iwarp_state;
			nesqp->hw_tcp_state = tcp_state;
			nesqp->last_aeq = async_event_id;
			spin_unlock_irqrestore(&nesqp->lock, flags);
			nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_AMP_UNALLOCATED_STAG event on QP%u\n",
					nesqp->hwqp.qp_id);
			if (nesqp->ibqp.event_handler) {
				ibevent.device = nesqp->ibqp.device;
				ibevent.element.qp = &nesqp->ibqp;
				ibevent.event = IB_EVENT_QP_ACCESS_ERR;
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
			}
			break;
		case NES_AEQE_AEID_PRIV_OPERATION_DENIED:
			nesqp = nesadapter->qp_table[le32_to_cpu(aeqe->aeqe_words
					[NES_AEQE_COMP_QP_CQ_ID_IDX])-NES_FIRST_QPN];
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = iwarp_state;
			nesqp->hw_tcp_state = tcp_state;
			nesqp->last_aeq = async_event_id;
			spin_unlock_irqrestore(&nesqp->lock, flags);
			nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_PRIV_OPERATION_DENIED event on QP%u,"
					" nesqp = %p, AE reported %p\n",
					nesqp->hwqp.qp_id, nesqp, *((struct nes_qp **)&context));
			if (nesqp->ibqp.event_handler) {
				ibevent.device = nesqp->ibqp.device;
				ibevent.element.qp = &nesqp->ibqp;
				ibevent.event = IB_EVENT_QP_ACCESS_ERR;
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
			}
			break;
		case NES_AEQE_AEID_CQ_OPERATION_ERROR:
			context <<= 1;
			nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_CQ_OPERATION_ERROR event on CQ%u, %p\n",
					le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_QP_CQ_ID_IDX]), (void *)(unsigned long)context);
			resource_allocated = nes_is_resource_allocated(nesadapter, nesadapter->allocated_cqs,
					le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_QP_CQ_ID_IDX]));
			if (resource_allocated) {
				printk(KERN_ERR PFX "%s: Processing an NES_AEQE_AEID_CQ_OPERATION_ERROR event on CQ%u\n",
						__func__, le32_to_cpu(aeqe->aeqe_words[NES_AEQE_COMP_QP_CQ_ID_IDX]));
			}
			break;
		case NES_AEQE_AEID_DDP_UBE_DDP_MESSAGE_TOO_LONG_FOR_AVAILABLE_BUFFER:
			nesqp = nesadapter->qp_table[le32_to_cpu(
					aeqe->aeqe_words[NES_AEQE_COMP_QP_CQ_ID_IDX])-NES_FIRST_QPN];
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = iwarp_state;
			nesqp->hw_tcp_state = tcp_state;
			nesqp->last_aeq = async_event_id;
			spin_unlock_irqrestore(&nesqp->lock, flags);
			nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_DDP_UBE_DDP_MESSAGE_TOO_LONG"
					"_FOR_AVAILABLE_BUFFER event on QP%u\n",
					nesqp->hwqp.qp_id);
			if (nesqp->ibqp.event_handler) {
				ibevent.device = nesqp->ibqp.device;
				ibevent.element.qp = &nesqp->ibqp;
				ibevent.event = IB_EVENT_QP_ACCESS_ERR;
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
			}
			/* tell cm to disconnect, cm will queue work to thread */
			nes_cm_disconn(nesqp);
			break;
		case NES_AEQE_AEID_DDP_UBE_INVALID_MSN_NO_BUFFER_AVAILABLE:
			nesqp = *((struct nes_qp **)&context);
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = iwarp_state;
			nesqp->hw_tcp_state = tcp_state;
			nesqp->last_aeq = async_event_id;
			spin_unlock_irqrestore(&nesqp->lock, flags);
			nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_DDP_UBE_INVALID_MSN"
					"_NO_BUFFER_AVAILABLE event on QP%u\n",
					nesqp->hwqp.qp_id);
			if (nesqp->ibqp.event_handler) {
				ibevent.device = nesqp->ibqp.device;
				ibevent.element.qp = &nesqp->ibqp;
				ibevent.event = IB_EVENT_QP_FATAL;
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
			}
			/* tell cm to disconnect, cm will queue work to thread */
			nes_cm_disconn(nesqp);
			break;
		case NES_AEQE_AEID_LLP_RECEIVED_MPA_CRC_ERROR:
			nesqp = *((struct nes_qp **)&context);
			spin_lock_irqsave(&nesqp->lock, flags);
			nesqp->hw_iwarp_state = iwarp_state;
			nesqp->hw_tcp_state = tcp_state;
			nesqp->last_aeq = async_event_id;
			spin_unlock_irqrestore(&nesqp->lock, flags);
			nes_debug(NES_DBG_AEQ, "Processing an NES_AEQE_AEID_LLP_RECEIVED_MPA_CRC_ERROR"
					" event on QP%u \n  Q2 Data:\n",
					nesqp->hwqp.qp_id);
			if (nesqp->ibqp.event_handler) {
				ibevent.device = nesqp->ibqp.device;
				ibevent.element.qp = &nesqp->ibqp;
				ibevent.event = IB_EVENT_QP_FATAL;
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
			}
			/* tell cm to disconnect, cm will queue work to thread */
			nes_cm_disconn(nesqp);
			break;
			/* TODO: additional AEs need to be here */
		default:
			nes_debug(NES_DBG_AEQ, "Processing an iWARP related AE for QP, misc = 0x%04X\n",
					async_event_id);
			break;
	}

}


/**
 * nes_iwarp_ce_handler
 */
void nes_iwarp_ce_handler(struct nes_device *nesdev, struct nes_hw_cq *hw_cq)
{
	struct nes_cq *nescq = container_of(hw_cq, struct nes_cq, hw_cq);

	/* nes_debug(NES_DBG_CQ, "Processing completion event for iWARP CQ%u.\n",
			nescq->hw_cq.cq_number); */
	nes_write32(nesdev->regs+NES_CQ_ACK, nescq->hw_cq.cq_number);

	if (nescq->ibcq.comp_handler)
		nescq->ibcq.comp_handler(&nescq->ibcq, nescq->ibcq.cq_context);

	return;
}


/**
 * nes_manage_apbvt()
 */
int nes_manage_apbvt(struct nes_vnic *nesvnic, u32 accel_local_port,
		u32 nic_index, u32 add_port)
{
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	int ret = 0;
	u16 major_code;

	/* Send manage APBVT request to CQP */
	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_QP, "Failed to get a cqp_request.\n");
		return -ENOMEM;
	}
	cqp_request->waiting = 1;
	cqp_wqe = &cqp_request->cqp_wqe;

	nes_debug(NES_DBG_QP, "%s APBV for local port=%u(0x%04x), nic_index=%u\n",
			(add_port == NES_MANAGE_APBVT_ADD) ? "ADD" : "DEL",
			accel_local_port, accel_local_port, nic_index);

	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX, (NES_CQP_MANAGE_APBVT |
			((add_port == NES_MANAGE_APBVT_ADD) ? NES_CQP_APBVT_ADD : 0)));
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX,
			((nic_index << NES_CQP_APBVT_NIC_SHIFT) | accel_local_port));

	nes_debug(NES_DBG_QP, "Waiting for CQP completion for APBVT.\n");

	atomic_set(&cqp_request->refcount, 2);
	nes_post_cqp_request(nesdev, cqp_request);

	if (add_port == NES_MANAGE_APBVT_ADD)
		ret = wait_event_timeout(cqp_request->waitq, (cqp_request->request_done != 0),
				NES_EVENT_TIMEOUT);
	nes_debug(NES_DBG_QP, "Completed, ret=%u,  CQP Major:Minor codes = 0x%04X:0x%04X\n",
			ret, cqp_request->major_code, cqp_request->minor_code);
	major_code = cqp_request->major_code;

	nes_put_cqp_request(nesdev, cqp_request);

	if (!ret)
		return -ETIME;
	else if (major_code)
		return -EIO;
	else
		return 0;
}


/**
 * nes_manage_arp_cache
 */
void nes_manage_arp_cache(struct net_device *netdev, unsigned char *mac_addr,
		u32 ip_addr, u32 action)
{
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_vnic *nesvnic = netdev_priv(netdev);
	struct nes_device *nesdev;
	struct nes_cqp_request *cqp_request;
	int arp_index;

	nesdev = nesvnic->nesdev;
	arp_index = nes_arp_table(nesdev, ip_addr, mac_addr, action);
	if (arp_index == -1) {
		return;
	}

	/* update the ARP entry */
	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_NETDEV, "Failed to get a cqp_request.\n");
		return;
	}
	cqp_request->waiting = 0;
	cqp_wqe = &cqp_request->cqp_wqe;
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);

	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(
			NES_CQP_MANAGE_ARP_CACHE | NES_CQP_ARP_PERM);
	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] |= cpu_to_le32(
			(u32)PCI_FUNC(nesdev->pcidev->devfn) << NES_CQP_ARP_AEQ_INDEX_SHIFT);
	cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX] = cpu_to_le32(arp_index);

	if (action == NES_ARP_ADD) {
		cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] |= cpu_to_le32(NES_CQP_ARP_VALID);
		cqp_wqe->wqe_words[NES_CQP_ARP_WQE_MAC_ADDR_LOW_IDX] = cpu_to_le32(
				(((u32)mac_addr[2]) << 24) | (((u32)mac_addr[3]) << 16) |
				(((u32)mac_addr[4]) << 8)  | (u32)mac_addr[5]);
		cqp_wqe->wqe_words[NES_CQP_ARP_WQE_MAC_HIGH_IDX] = cpu_to_le32(
				(((u32)mac_addr[0]) << 16) | (u32)mac_addr[1]);
	} else {
		cqp_wqe->wqe_words[NES_CQP_ARP_WQE_MAC_ADDR_LOW_IDX] = 0;
		cqp_wqe->wqe_words[NES_CQP_ARP_WQE_MAC_HIGH_IDX] = 0;
	}

	nes_debug(NES_DBG_NETDEV, "Not waiting for CQP, cqp.sq_head=%u, cqp.sq_tail=%u\n",
			nesdev->cqp.sq_head, nesdev->cqp.sq_tail);

	atomic_set(&cqp_request->refcount, 1);
	nes_post_cqp_request(nesdev, cqp_request);
}


/**
 * flush_wqes
 */
void flush_wqes(struct nes_device *nesdev, struct nes_qp *nesqp,
		u32 which_wq, u32 wait_completion)
{
	struct nes_cqp_request *cqp_request;
	struct nes_hw_cqp_wqe *cqp_wqe;
	int ret;

	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_QP, "Failed to get a cqp_request.\n");
		return;
	}
	if (wait_completion) {
		cqp_request->waiting = 1;
		atomic_set(&cqp_request->refcount, 2);
	} else {
		cqp_request->waiting = 0;
	}
	cqp_wqe = &cqp_request->cqp_wqe;
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);

	cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] =
			cpu_to_le32(NES_CQP_FLUSH_WQES | which_wq);
	cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX] = cpu_to_le32(nesqp->hwqp.qp_id);

	nes_post_cqp_request(nesdev, cqp_request);

	if (wait_completion) {
		/* Wait for CQP */
		ret = wait_event_timeout(cqp_request->waitq, (cqp_request->request_done != 0),
				NES_EVENT_TIMEOUT);
		nes_debug(NES_DBG_QP, "Flush SQ QP WQEs completed, ret=%u,"
				" CQP Major:Minor codes = 0x%04X:0x%04X\n",
				ret, cqp_request->major_code, cqp_request->minor_code);
		nes_put_cqp_request(nesdev, cqp_request);
	}
}
