/*
 * Linux network driver for Brocade Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */

#include "cna.h"

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>

#include "bna.h"

#include "bnad.h"

#define BNAD_NUM_TXF_COUNTERS 12
#define BNAD_NUM_RXF_COUNTERS 10
#define BNAD_NUM_CQ_COUNTERS 3
#define BNAD_NUM_RXQ_COUNTERS 6
#define BNAD_NUM_TXQ_COUNTERS 5

#define BNAD_ETHTOOL_STATS_NUM						\
	(sizeof(struct rtnl_link_stats64) / sizeof(u64) +	\
	sizeof(struct bnad_drv_stats) / sizeof(u64) +		\
	offsetof(struct bfi_ll_stats, rxf_stats[0]) / sizeof(u64))

static char *bnad_net_stats_strings[BNAD_ETHTOOL_STATS_NUM] = {
	"rx_packets",
	"tx_packets",
	"rx_bytes",
	"tx_bytes",
	"rx_errors",
	"tx_errors",
	"rx_dropped",
	"tx_dropped",
	"multicast",
	"collisions",

	"rx_length_errors",
	"rx_over_errors",
	"rx_crc_errors",
	"rx_frame_errors",
	"rx_fifo_errors",
	"rx_missed_errors",

	"tx_aborted_errors",
	"tx_carrier_errors",
	"tx_fifo_errors",
	"tx_heartbeat_errors",
	"tx_window_errors",

	"rx_compressed",
	"tx_compressed",

	"netif_queue_stop",
	"netif_queue_wakeup",
	"netif_queue_stopped",
	"tso4",
	"tso6",
	"tso_err",
	"tcpcsum_offload",
	"udpcsum_offload",
	"csum_help",
	"csum_help_err",
	"hw_stats_updates",
	"netif_rx_schedule",
	"netif_rx_complete",
	"netif_rx_dropped",

	"link_toggle",
	"cee_up",

	"rxp_info_alloc_failed",
	"mbox_intr_disabled",
	"mbox_intr_enabled",
	"tx_unmap_q_alloc_failed",
	"rx_unmap_q_alloc_failed",
	"rxbuf_alloc_failed",

	"mac_frame_64",
	"mac_frame_65_127",
	"mac_frame_128_255",
	"mac_frame_256_511",
	"mac_frame_512_1023",
	"mac_frame_1024_1518",
	"mac_frame_1518_1522",
	"mac_rx_bytes",
	"mac_rx_packets",
	"mac_rx_fcs_error",
	"mac_rx_multicast",
	"mac_rx_broadcast",
	"mac_rx_control_frames",
	"mac_rx_pause",
	"mac_rx_unknown_opcode",
	"mac_rx_alignment_error",
	"mac_rx_frame_length_error",
	"mac_rx_code_error",
	"mac_rx_carrier_sense_error",
	"mac_rx_undersize",
	"mac_rx_oversize",
	"mac_rx_fragments",
	"mac_rx_jabber",
	"mac_rx_drop",

	"mac_tx_bytes",
	"mac_tx_packets",
	"mac_tx_multicast",
	"mac_tx_broadcast",
	"mac_tx_pause",
	"mac_tx_deferral",
	"mac_tx_excessive_deferral",
	"mac_tx_single_collision",
	"mac_tx_muliple_collision",
	"mac_tx_late_collision",
	"mac_tx_excessive_collision",
	"mac_tx_total_collision",
	"mac_tx_pause_honored",
	"mac_tx_drop",
	"mac_tx_jabber",
	"mac_tx_fcs_error",
	"mac_tx_control_frame",
	"mac_tx_oversize",
	"mac_tx_undersize",
	"mac_tx_fragments",

	"bpc_tx_pause_0",
	"bpc_tx_pause_1",
	"bpc_tx_pause_2",
	"bpc_tx_pause_3",
	"bpc_tx_pause_4",
	"bpc_tx_pause_5",
	"bpc_tx_pause_6",
	"bpc_tx_pause_7",
	"bpc_tx_zero_pause_0",
	"bpc_tx_zero_pause_1",
	"bpc_tx_zero_pause_2",
	"bpc_tx_zero_pause_3",
	"bpc_tx_zero_pause_4",
	"bpc_tx_zero_pause_5",
	"bpc_tx_zero_pause_6",
	"bpc_tx_zero_pause_7",
	"bpc_tx_first_pause_0",
	"bpc_tx_first_pause_1",
	"bpc_tx_first_pause_2",
	"bpc_tx_first_pause_3",
	"bpc_tx_first_pause_4",
	"bpc_tx_first_pause_5",
	"bpc_tx_first_pause_6",
	"bpc_tx_first_pause_7",

	"bpc_rx_pause_0",
	"bpc_rx_pause_1",
	"bpc_rx_pause_2",
	"bpc_rx_pause_3",
	"bpc_rx_pause_4",
	"bpc_rx_pause_5",
	"bpc_rx_pause_6",
	"bpc_rx_pause_7",
	"bpc_rx_zero_pause_0",
	"bpc_rx_zero_pause_1",
	"bpc_rx_zero_pause_2",
	"bpc_rx_zero_pause_3",
	"bpc_rx_zero_pause_4",
	"bpc_rx_zero_pause_5",
	"bpc_rx_zero_pause_6",
	"bpc_rx_zero_pause_7",
	"bpc_rx_first_pause_0",
	"bpc_rx_first_pause_1",
	"bpc_rx_first_pause_2",
	"bpc_rx_first_pause_3",
	"bpc_rx_first_pause_4",
	"bpc_rx_first_pause_5",
	"bpc_rx_first_pause_6",
	"bpc_rx_first_pause_7",

	"rad_rx_frames",
	"rad_rx_octets",
	"rad_rx_vlan_frames",
	"rad_rx_ucast",
	"rad_rx_ucast_octets",
	"rad_rx_ucast_vlan",
	"rad_rx_mcast",
	"rad_rx_mcast_octets",
	"rad_rx_mcast_vlan",
	"rad_rx_bcast",
	"rad_rx_bcast_octets",
	"rad_rx_bcast_vlan",
	"rad_rx_drops",

	"fc_rx_ucast_octets",
	"fc_rx_ucast",
	"fc_rx_ucast_vlan",
	"fc_rx_mcast_octets",
	"fc_rx_mcast",
	"fc_rx_mcast_vlan",
	"fc_rx_bcast_octets",
	"fc_rx_bcast",
	"fc_rx_bcast_vlan",

	"fc_tx_ucast_octets",
	"fc_tx_ucast",
	"fc_tx_ucast_vlan",
	"fc_tx_mcast_octets",
	"fc_tx_mcast",
	"fc_tx_mcast_vlan",
	"fc_tx_bcast_octets",
	"fc_tx_bcast",
	"fc_tx_bcast_vlan",
	"fc_tx_parity_errors",
	"fc_tx_timeout",
	"fc_tx_fid_parity_errors",
};

static int
bnad_get_settings(struct net_device *netdev, struct ethtool_cmd *cmd)
{
	cmd->supported = SUPPORTED_10000baseT_Full;
	cmd->advertising = ADVERTISED_10000baseT_Full;
	cmd->autoneg = AUTONEG_DISABLE;
	cmd->supported |= SUPPORTED_FIBRE;
	cmd->advertising |= ADVERTISED_FIBRE;
	cmd->port = PORT_FIBRE;
	cmd->phy_address = 0;

	if (netif_carrier_ok(netdev)) {
		cmd->speed = SPEED_10000;
		cmd->duplex = DUPLEX_FULL;
	} else {
		cmd->speed = -1;
		cmd->duplex = -1;
	}
	cmd->transceiver = XCVR_EXTERNAL;
	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;

	return 0;
}

static int
bnad_set_settings(struct net_device *netdev, struct ethtool_cmd *cmd)
{
	/* 10G full duplex setting supported only */
	if (cmd->autoneg == AUTONEG_ENABLE)
		return -EOPNOTSUPP; else {
		if ((cmd->speed == SPEED_10000) && (cmd->duplex == DUPLEX_FULL))
			return 0;
	}

	return -EOPNOTSUPP;
}

static void
bnad_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct bnad *bnad = netdev_priv(netdev);
	struct bfa_ioc_attr *ioc_attr;
	unsigned long flags;

	strcpy(drvinfo->driver, BNAD_NAME);
	strcpy(drvinfo->version, BNAD_VERSION);

	ioc_attr = kzalloc(sizeof(*ioc_attr), GFP_KERNEL);
	if (ioc_attr) {
		memset(ioc_attr, 0, sizeof(*ioc_attr));
		spin_lock_irqsave(&bnad->bna_lock, flags);
		bfa_nw_ioc_get_attr(&bnad->bna.device.ioc, ioc_attr);
		spin_unlock_irqrestore(&bnad->bna_lock, flags);

		strncpy(drvinfo->fw_version, ioc_attr->adapter_attr.fw_ver,
			sizeof(drvinfo->fw_version) - 1);
		kfree(ioc_attr);
	}

	strncpy(drvinfo->bus_info, pci_name(bnad->pcidev), ETHTOOL_BUSINFO_LEN);
}

static int
get_regs(struct bnad *bnad, u32 * regs)
{
	int num = 0, i;
	u32 reg_addr;
	unsigned long flags;

#define BNAD_GET_REG(addr) 					\
do {								\
	if (regs)						\
		regs[num++] = readl(bnad->bar0 + (addr));	\
	else							\
		num++;						\
} while (0)

	spin_lock_irqsave(&bnad->bna_lock, flags);

	/* DMA Block Internal Registers */
	BNAD_GET_REG(DMA_CTRL_REG0);
	BNAD_GET_REG(DMA_CTRL_REG1);
	BNAD_GET_REG(DMA_ERR_INT_STATUS);
	BNAD_GET_REG(DMA_ERR_INT_ENABLE);
	BNAD_GET_REG(DMA_ERR_INT_STATUS_SET);

	/* APP Block Register Address Offset from BAR0 */
	BNAD_GET_REG(HOSTFN0_INT_STATUS);
	BNAD_GET_REG(HOSTFN0_INT_MASK);
	BNAD_GET_REG(HOST_PAGE_NUM_FN0);
	BNAD_GET_REG(HOST_MSIX_ERR_INDEX_FN0);
	BNAD_GET_REG(FN0_PCIE_ERR_REG);
	BNAD_GET_REG(FN0_ERR_TYPE_STATUS_REG);
	BNAD_GET_REG(FN0_ERR_TYPE_MSK_STATUS_REG);

	BNAD_GET_REG(HOSTFN1_INT_STATUS);
	BNAD_GET_REG(HOSTFN1_INT_MASK);
	BNAD_GET_REG(HOST_PAGE_NUM_FN1);
	BNAD_GET_REG(HOST_MSIX_ERR_INDEX_FN1);
	BNAD_GET_REG(FN1_PCIE_ERR_REG);
	BNAD_GET_REG(FN1_ERR_TYPE_STATUS_REG);
	BNAD_GET_REG(FN1_ERR_TYPE_MSK_STATUS_REG);

	BNAD_GET_REG(PCIE_MISC_REG);

	BNAD_GET_REG(HOST_SEM0_INFO_REG);
	BNAD_GET_REG(HOST_SEM1_INFO_REG);
	BNAD_GET_REG(HOST_SEM2_INFO_REG);
	BNAD_GET_REG(HOST_SEM3_INFO_REG);

	BNAD_GET_REG(TEMPSENSE_CNTL_REG);
	BNAD_GET_REG(TEMPSENSE_STAT_REG);

	BNAD_GET_REG(APP_LOCAL_ERR_STAT);
	BNAD_GET_REG(APP_LOCAL_ERR_MSK);

	BNAD_GET_REG(PCIE_LNK_ERR_STAT);
	BNAD_GET_REG(PCIE_LNK_ERR_MSK);

	BNAD_GET_REG(FCOE_FIP_ETH_TYPE);
	BNAD_GET_REG(RESV_ETH_TYPE);

	BNAD_GET_REG(HOSTFN2_INT_STATUS);
	BNAD_GET_REG(HOSTFN2_INT_MASK);
	BNAD_GET_REG(HOST_PAGE_NUM_FN2);
	BNAD_GET_REG(HOST_MSIX_ERR_INDEX_FN2);
	BNAD_GET_REG(FN2_PCIE_ERR_REG);
	BNAD_GET_REG(FN2_ERR_TYPE_STATUS_REG);
	BNAD_GET_REG(FN2_ERR_TYPE_MSK_STATUS_REG);

	BNAD_GET_REG(HOSTFN3_INT_STATUS);
	BNAD_GET_REG(HOSTFN3_INT_MASK);
	BNAD_GET_REG(HOST_PAGE_NUM_FN3);
	BNAD_GET_REG(HOST_MSIX_ERR_INDEX_FN3);
	BNAD_GET_REG(FN3_PCIE_ERR_REG);
	BNAD_GET_REG(FN3_ERR_TYPE_STATUS_REG);
	BNAD_GET_REG(FN3_ERR_TYPE_MSK_STATUS_REG);

	/* Host Command Status Registers */
	reg_addr = HOST_CMDSTS0_CLR_REG;
	for (i = 0; i < 16; i++) {
		BNAD_GET_REG(reg_addr);
		BNAD_GET_REG(reg_addr + 4);
		BNAD_GET_REG(reg_addr + 8);
		reg_addr += 0x10;
	}

	/* Function ID register */
	BNAD_GET_REG(FNC_ID_REG);

	/* Function personality register */
	BNAD_GET_REG(FNC_PERS_REG);

	/* Operation mode register */
	BNAD_GET_REG(OP_MODE);

	/* LPU0 Registers */
	BNAD_GET_REG(LPU0_MBOX_CTL_REG);
	BNAD_GET_REG(LPU0_MBOX_CMD_REG);
	BNAD_GET_REG(LPU0_MBOX_LINK_0REG);
	BNAD_GET_REG(LPU1_MBOX_LINK_0REG);
	BNAD_GET_REG(LPU0_MBOX_STATUS_0REG);
	BNAD_GET_REG(LPU1_MBOX_STATUS_0REG);
	BNAD_GET_REG(LPU0_ERR_STATUS_REG);
	BNAD_GET_REG(LPU0_ERR_SET_REG);

	/* LPU1 Registers */
	BNAD_GET_REG(LPU1_MBOX_CTL_REG);
	BNAD_GET_REG(LPU1_MBOX_CMD_REG);
	BNAD_GET_REG(LPU0_MBOX_LINK_1REG);
	BNAD_GET_REG(LPU1_MBOX_LINK_1REG);
	BNAD_GET_REG(LPU0_MBOX_STATUS_1REG);
	BNAD_GET_REG(LPU1_MBOX_STATUS_1REG);
	BNAD_GET_REG(LPU1_ERR_STATUS_REG);
	BNAD_GET_REG(LPU1_ERR_SET_REG);

	/* PSS Registers */
	BNAD_GET_REG(PSS_CTL_REG);
	BNAD_GET_REG(PSS_ERR_STATUS_REG);
	BNAD_GET_REG(ERR_STATUS_SET);
	BNAD_GET_REG(PSS_RAM_ERR_STATUS_REG);

	/* Catapult CPQ Registers */
	BNAD_GET_REG(HOSTFN0_LPU0_MBOX0_CMD_STAT);
	BNAD_GET_REG(HOSTFN0_LPU1_MBOX0_CMD_STAT);
	BNAD_GET_REG(LPU0_HOSTFN0_MBOX0_CMD_STAT);
	BNAD_GET_REG(LPU1_HOSTFN0_MBOX0_CMD_STAT);

	BNAD_GET_REG(HOSTFN0_LPU0_MBOX1_CMD_STAT);
	BNAD_GET_REG(HOSTFN0_LPU1_MBOX1_CMD_STAT);
	BNAD_GET_REG(LPU0_HOSTFN0_MBOX1_CMD_STAT);
	BNAD_GET_REG(LPU1_HOSTFN0_MBOX1_CMD_STAT);

	BNAD_GET_REG(HOSTFN1_LPU0_MBOX0_CMD_STAT);
	BNAD_GET_REG(HOSTFN1_LPU1_MBOX0_CMD_STAT);
	BNAD_GET_REG(LPU0_HOSTFN1_MBOX0_CMD_STAT);
	BNAD_GET_REG(LPU1_HOSTFN1_MBOX0_CMD_STAT);

	BNAD_GET_REG(HOSTFN1_LPU0_MBOX1_CMD_STAT);
	BNAD_GET_REG(HOSTFN1_LPU1_MBOX1_CMD_STAT);
	BNAD_GET_REG(LPU0_HOSTFN1_MBOX1_CMD_STAT);
	BNAD_GET_REG(LPU1_HOSTFN1_MBOX1_CMD_STAT);

	BNAD_GET_REG(HOSTFN2_LPU0_MBOX0_CMD_STAT);
	BNAD_GET_REG(HOSTFN2_LPU1_MBOX0_CMD_STAT);
	BNAD_GET_REG(LPU0_HOSTFN2_MBOX0_CMD_STAT);
	BNAD_GET_REG(LPU1_HOSTFN2_MBOX0_CMD_STAT);

	BNAD_GET_REG(HOSTFN2_LPU0_MBOX1_CMD_STAT);
	BNAD_GET_REG(HOSTFN2_LPU1_MBOX1_CMD_STAT);
	BNAD_GET_REG(LPU0_HOSTFN2_MBOX1_CMD_STAT);
	BNAD_GET_REG(LPU1_HOSTFN2_MBOX1_CMD_STAT);

	BNAD_GET_REG(HOSTFN3_LPU0_MBOX0_CMD_STAT);
	BNAD_GET_REG(HOSTFN3_LPU1_MBOX0_CMD_STAT);
	BNAD_GET_REG(LPU0_HOSTFN3_MBOX0_CMD_STAT);
	BNAD_GET_REG(LPU1_HOSTFN3_MBOX0_CMD_STAT);

	BNAD_GET_REG(HOSTFN3_LPU0_MBOX1_CMD_STAT);
	BNAD_GET_REG(HOSTFN3_LPU1_MBOX1_CMD_STAT);
	BNAD_GET_REG(LPU0_HOSTFN3_MBOX1_CMD_STAT);
	BNAD_GET_REG(LPU1_HOSTFN3_MBOX1_CMD_STAT);

	/* Host Function Force Parity Error Registers */
	BNAD_GET_REG(HOSTFN0_LPU_FORCE_PERR);
	BNAD_GET_REG(HOSTFN1_LPU_FORCE_PERR);
	BNAD_GET_REG(HOSTFN2_LPU_FORCE_PERR);
	BNAD_GET_REG(HOSTFN3_LPU_FORCE_PERR);

	/* LL Port[0|1] Halt Mask Registers */
	BNAD_GET_REG(LL_HALT_MSK_P0);
	BNAD_GET_REG(LL_HALT_MSK_P1);

	/* LL Port[0|1] Error Mask Registers */
	BNAD_GET_REG(LL_ERR_MSK_P0);
	BNAD_GET_REG(LL_ERR_MSK_P1);

	/* EMC FLI Registers */
	BNAD_GET_REG(FLI_CMD_REG);
	BNAD_GET_REG(FLI_ADDR_REG);
	BNAD_GET_REG(FLI_CTL_REG);
	BNAD_GET_REG(FLI_WRDATA_REG);
	BNAD_GET_REG(FLI_RDDATA_REG);
	BNAD_GET_REG(FLI_DEV_STATUS_REG);
	BNAD_GET_REG(FLI_SIG_WD_REG);

	BNAD_GET_REG(FLI_DEV_VENDOR_REG);
	BNAD_GET_REG(FLI_ERR_STATUS_REG);

	/* RxAdm 0 Registers */
	BNAD_GET_REG(RAD0_CTL_REG);
	BNAD_GET_REG(RAD0_PE_PARM_REG);
	BNAD_GET_REG(RAD0_BCN_REG);
	BNAD_GET_REG(RAD0_DEFAULT_REG);
	BNAD_GET_REG(RAD0_PROMISC_REG);
	BNAD_GET_REG(RAD0_BCNQ_REG);
	BNAD_GET_REG(RAD0_DEFAULTQ_REG);

	BNAD_GET_REG(RAD0_ERR_STS);
	BNAD_GET_REG(RAD0_SET_ERR_STS);
	BNAD_GET_REG(RAD0_ERR_INT_EN);
	BNAD_GET_REG(RAD0_FIRST_ERR);
	BNAD_GET_REG(RAD0_FORCE_ERR);

	BNAD_GET_REG(RAD0_MAC_MAN_1H);
	BNAD_GET_REG(RAD0_MAC_MAN_1L);
	BNAD_GET_REG(RAD0_MAC_MAN_2H);
	BNAD_GET_REG(RAD0_MAC_MAN_2L);
	BNAD_GET_REG(RAD0_MAC_MAN_3H);
	BNAD_GET_REG(RAD0_MAC_MAN_3L);
	BNAD_GET_REG(RAD0_MAC_MAN_4H);
	BNAD_GET_REG(RAD0_MAC_MAN_4L);

	BNAD_GET_REG(RAD0_LAST4_IP);

	/* RxAdm 1 Registers */
	BNAD_GET_REG(RAD1_CTL_REG);
	BNAD_GET_REG(RAD1_PE_PARM_REG);
	BNAD_GET_REG(RAD1_BCN_REG);
	BNAD_GET_REG(RAD1_DEFAULT_REG);
	BNAD_GET_REG(RAD1_PROMISC_REG);
	BNAD_GET_REG(RAD1_BCNQ_REG);
	BNAD_GET_REG(RAD1_DEFAULTQ_REG);

	BNAD_GET_REG(RAD1_ERR_STS);
	BNAD_GET_REG(RAD1_SET_ERR_STS);
	BNAD_GET_REG(RAD1_ERR_INT_EN);

	/* TxA0 Registers */
	BNAD_GET_REG(TXA0_CTRL_REG);
	/* TxA0 TSO Sequence # Registers (RO) */
	for (i = 0; i < 8; i++) {
		BNAD_GET_REG(TXA0_TSO_TCP_SEQ_REG(i));
		BNAD_GET_REG(TXA0_TSO_IP_INFO_REG(i));
	}

	/* TxA1 Registers */
	BNAD_GET_REG(TXA1_CTRL_REG);
	/* TxA1 TSO Sequence # Registers (RO) */
	for (i = 0; i < 8; i++) {
		BNAD_GET_REG(TXA1_TSO_TCP_SEQ_REG(i));
		BNAD_GET_REG(TXA1_TSO_IP_INFO_REG(i));
	}

	/* RxA Registers */
	BNAD_GET_REG(RXA0_CTL_REG);
	BNAD_GET_REG(RXA1_CTL_REG);

	/* PLB0 Registers */
	BNAD_GET_REG(PLB0_ECM_TIMER_REG);
	BNAD_GET_REG(PLB0_RL_CTL);
	for (i = 0; i < 8; i++)
		BNAD_GET_REG(PLB0_RL_MAX_BC(i));
	BNAD_GET_REG(PLB0_RL_TU_PRIO);
	for (i = 0; i < 8; i++)
		BNAD_GET_REG(PLB0_RL_BYTE_CNT(i));
	BNAD_GET_REG(PLB0_RL_MIN_REG);
	BNAD_GET_REG(PLB0_RL_MAX_REG);
	BNAD_GET_REG(PLB0_EMS_ADD_REG);

	/* PLB1 Registers */
	BNAD_GET_REG(PLB1_ECM_TIMER_REG);
	BNAD_GET_REG(PLB1_RL_CTL);
	for (i = 0; i < 8; i++)
		BNAD_GET_REG(PLB1_RL_MAX_BC(i));
	BNAD_GET_REG(PLB1_RL_TU_PRIO);
	for (i = 0; i < 8; i++)
		BNAD_GET_REG(PLB1_RL_BYTE_CNT(i));
	BNAD_GET_REG(PLB1_RL_MIN_REG);
	BNAD_GET_REG(PLB1_RL_MAX_REG);
	BNAD_GET_REG(PLB1_EMS_ADD_REG);

	/* HQM Control Register */
	BNAD_GET_REG(HQM0_CTL_REG);
	BNAD_GET_REG(HQM0_RXQ_STOP_SEM);
	BNAD_GET_REG(HQM0_TXQ_STOP_SEM);
	BNAD_GET_REG(HQM1_CTL_REG);
	BNAD_GET_REG(HQM1_RXQ_STOP_SEM);
	BNAD_GET_REG(HQM1_TXQ_STOP_SEM);

	/* LUT Registers */
	BNAD_GET_REG(LUT0_ERR_STS);
	BNAD_GET_REG(LUT0_SET_ERR_STS);
	BNAD_GET_REG(LUT1_ERR_STS);
	BNAD_GET_REG(LUT1_SET_ERR_STS);

	/* TRC Registers */
	BNAD_GET_REG(TRC_CTL_REG);
	BNAD_GET_REG(TRC_MODS_REG);
	BNAD_GET_REG(TRC_TRGC_REG);
	BNAD_GET_REG(TRC_CNT1_REG);
	BNAD_GET_REG(TRC_CNT2_REG);
	BNAD_GET_REG(TRC_NXTS_REG);
	BNAD_GET_REG(TRC_DIRR_REG);
	for (i = 0; i < 10; i++)
		BNAD_GET_REG(TRC_TRGM_REG(i));
	for (i = 0; i < 10; i++)
		BNAD_GET_REG(TRC_NXTM_REG(i));
	for (i = 0; i < 10; i++)
		BNAD_GET_REG(TRC_STRM_REG(i));

	spin_unlock_irqrestore(&bnad->bna_lock, flags);
#undef BNAD_GET_REG
	return num;
}
static int
bnad_get_regs_len(struct net_device *netdev)
{
	int ret = get_regs(netdev_priv(netdev), NULL) * sizeof(u32);
	return ret;
}

static void
bnad_get_regs(struct net_device *netdev, struct ethtool_regs *regs, void *buf)
{
	memset(buf, 0, bnad_get_regs_len(netdev));
	get_regs(netdev_priv(netdev), buf);
}

static void
bnad_get_wol(struct net_device *netdev, struct ethtool_wolinfo *wolinfo)
{
	wolinfo->supported = 0;
	wolinfo->wolopts = 0;
}

static int
bnad_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *coalesce)
{
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;

	/* Lock rqd. to access bnad->bna_lock */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	coalesce->use_adaptive_rx_coalesce =
		(bnad->cfg_flags & BNAD_CF_DIM_ENABLED) ? true : false;
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	coalesce->rx_coalesce_usecs = bnad->rx_coalescing_timeo *
					BFI_COALESCING_TIMER_UNIT;
	coalesce->tx_coalesce_usecs = bnad->tx_coalescing_timeo *
					BFI_COALESCING_TIMER_UNIT;
	coalesce->tx_max_coalesced_frames = BFI_TX_INTERPKT_COUNT;

	return 0;
}

static int
bnad_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *coalesce)
{
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;
	int dim_timer_del = 0;

	if (coalesce->rx_coalesce_usecs == 0 ||
	    coalesce->rx_coalesce_usecs >
	    BFI_MAX_COALESCING_TIMEO * BFI_COALESCING_TIMER_UNIT)
		return -EINVAL;

	if (coalesce->tx_coalesce_usecs == 0 ||
	    coalesce->tx_coalesce_usecs >
	    BFI_MAX_COALESCING_TIMEO * BFI_COALESCING_TIMER_UNIT)
		return -EINVAL;

	mutex_lock(&bnad->conf_mutex);
	/*
	 * Do not need to store rx_coalesce_usecs here
	 * Every time DIM is disabled, we can get it from the
	 * stack.
	 */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (coalesce->use_adaptive_rx_coalesce) {
		if (!(bnad->cfg_flags & BNAD_CF_DIM_ENABLED)) {
			bnad->cfg_flags |= BNAD_CF_DIM_ENABLED;
			bnad_dim_timer_start(bnad);
		}
	} else {
		if (bnad->cfg_flags & BNAD_CF_DIM_ENABLED) {
			bnad->cfg_flags &= ~BNAD_CF_DIM_ENABLED;
			dim_timer_del = bnad_dim_timer_running(bnad);
			if (dim_timer_del) {
				clear_bit(BNAD_RF_DIM_TIMER_RUNNING,
							&bnad->run_flags);
				spin_unlock_irqrestore(&bnad->bna_lock, flags);
				del_timer_sync(&bnad->dim_timer);
				spin_lock_irqsave(&bnad->bna_lock, flags);
			}
			bnad_rx_coalescing_timeo_set(bnad);
		}
	}
	if (bnad->tx_coalescing_timeo != coalesce->tx_coalesce_usecs /
					BFI_COALESCING_TIMER_UNIT) {
		bnad->tx_coalescing_timeo = coalesce->tx_coalesce_usecs /
						BFI_COALESCING_TIMER_UNIT;
		bnad_tx_coalescing_timeo_set(bnad);
	}

	if (bnad->rx_coalescing_timeo != coalesce->rx_coalesce_usecs /
					BFI_COALESCING_TIMER_UNIT) {
		bnad->rx_coalescing_timeo = coalesce->rx_coalesce_usecs /
						BFI_COALESCING_TIMER_UNIT;

		if (!(bnad->cfg_flags & BNAD_CF_DIM_ENABLED))
			bnad_rx_coalescing_timeo_set(bnad);

	}

	/* Add Tx Inter-pkt DMA count?  */

	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	mutex_unlock(&bnad->conf_mutex);
	return 0;
}

static void
bnad_get_ringparam(struct net_device *netdev,
		   struct ethtool_ringparam *ringparam)
{
	struct bnad *bnad = netdev_priv(netdev);

	ringparam->rx_max_pending = BNAD_MAX_Q_DEPTH / bnad_rxqs_per_cq;
	ringparam->rx_mini_max_pending = 0;
	ringparam->rx_jumbo_max_pending = 0;
	ringparam->tx_max_pending = BNAD_MAX_Q_DEPTH;

	ringparam->rx_pending = bnad->rxq_depth;
	ringparam->rx_mini_max_pending = 0;
	ringparam->rx_jumbo_max_pending = 0;
	ringparam->tx_pending = bnad->txq_depth;
}

static int
bnad_set_ringparam(struct net_device *netdev,
		   struct ethtool_ringparam *ringparam)
{
	int i, current_err, err = 0;
	struct bnad *bnad = netdev_priv(netdev);

	mutex_lock(&bnad->conf_mutex);
	if (ringparam->rx_pending == bnad->rxq_depth &&
	    ringparam->tx_pending == bnad->txq_depth) {
		mutex_unlock(&bnad->conf_mutex);
		return 0;
	}

	if (ringparam->rx_pending < BNAD_MIN_Q_DEPTH ||
	    ringparam->rx_pending > BNAD_MAX_Q_DEPTH / bnad_rxqs_per_cq ||
	    !BNA_POWER_OF_2(ringparam->rx_pending)) {
		mutex_unlock(&bnad->conf_mutex);
		return -EINVAL;
	}
	if (ringparam->tx_pending < BNAD_MIN_Q_DEPTH ||
	    ringparam->tx_pending > BNAD_MAX_Q_DEPTH ||
	    !BNA_POWER_OF_2(ringparam->tx_pending)) {
		mutex_unlock(&bnad->conf_mutex);
		return -EINVAL;
	}

	if (ringparam->rx_pending != bnad->rxq_depth) {
		bnad->rxq_depth = ringparam->rx_pending;
		for (i = 0; i < bnad->num_rx; i++) {
			if (!bnad->rx_info[i].rx)
				continue;
			bnad_cleanup_rx(bnad, i);
			current_err = bnad_setup_rx(bnad, i);
			if (current_err && !err)
				err = current_err;
		}
	}
	if (ringparam->tx_pending != bnad->txq_depth) {
		bnad->txq_depth = ringparam->tx_pending;
		for (i = 0; i < bnad->num_tx; i++) {
			if (!bnad->tx_info[i].tx)
				continue;
			bnad_cleanup_tx(bnad, i);
			current_err = bnad_setup_tx(bnad, i);
			if (current_err && !err)
				err = current_err;
		}
	}

	mutex_unlock(&bnad->conf_mutex);
	return err;
}

static void
bnad_get_pauseparam(struct net_device *netdev,
		    struct ethtool_pauseparam *pauseparam)
{
	struct bnad *bnad = netdev_priv(netdev);

	pauseparam->autoneg = 0;
	pauseparam->rx_pause = bnad->bna.port.pause_config.rx_pause;
	pauseparam->tx_pause = bnad->bna.port.pause_config.tx_pause;
}

static int
bnad_set_pauseparam(struct net_device *netdev,
		    struct ethtool_pauseparam *pauseparam)
{
	struct bnad *bnad = netdev_priv(netdev);
	struct bna_pause_config pause_config;
	unsigned long flags;

	if (pauseparam->autoneg == AUTONEG_ENABLE)
		return -EINVAL;

	mutex_lock(&bnad->conf_mutex);
	if (pauseparam->rx_pause != bnad->bna.port.pause_config.rx_pause ||
	    pauseparam->tx_pause != bnad->bna.port.pause_config.tx_pause) {
		pause_config.rx_pause = pauseparam->rx_pause;
		pause_config.tx_pause = pauseparam->tx_pause;
		spin_lock_irqsave(&bnad->bna_lock, flags);
		bna_port_pause_config(&bnad->bna.port, &pause_config, NULL);
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
	}
	mutex_unlock(&bnad->conf_mutex);
	return 0;
}

static u32
bnad_get_rx_csum(struct net_device *netdev)
{
	u32 rx_csum;
	struct bnad *bnad = netdev_priv(netdev);

	rx_csum = bnad->rx_csum;
	return rx_csum;
}

static int
bnad_set_rx_csum(struct net_device *netdev, u32 rx_csum)
{
	struct bnad *bnad = netdev_priv(netdev);

	mutex_lock(&bnad->conf_mutex);
	bnad->rx_csum = rx_csum;
	mutex_unlock(&bnad->conf_mutex);
	return 0;
}

static int
bnad_set_tx_csum(struct net_device *netdev, u32 tx_csum)
{
	struct bnad *bnad = netdev_priv(netdev);

	mutex_lock(&bnad->conf_mutex);
	if (tx_csum) {
		netdev->features |= NETIF_F_IP_CSUM;
		netdev->features |= NETIF_F_IPV6_CSUM;
	} else {
		netdev->features &= ~NETIF_F_IP_CSUM;
		netdev->features &= ~NETIF_F_IPV6_CSUM;
	}
	mutex_unlock(&bnad->conf_mutex);
	return 0;
}

static int
bnad_set_tso(struct net_device *netdev, u32 tso)
{
	struct bnad *bnad = netdev_priv(netdev);

	mutex_lock(&bnad->conf_mutex);
	if (tso) {
		netdev->features |= NETIF_F_TSO;
		netdev->features |= NETIF_F_TSO6;
	} else {
		netdev->features &= ~NETIF_F_TSO;
		netdev->features &= ~NETIF_F_TSO6;
	}
	mutex_unlock(&bnad->conf_mutex);
	return 0;
}

static void
bnad_get_strings(struct net_device *netdev, u32 stringset, u8 * string)
{
	struct bnad *bnad = netdev_priv(netdev);
	int i, j, q_num;
	u64 bmap;

	mutex_lock(&bnad->conf_mutex);

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < BNAD_ETHTOOL_STATS_NUM; i++) {
			BUG_ON(!(strlen(bnad_net_stats_strings[i]) <
				   ETH_GSTRING_LEN));
			memcpy(string, bnad_net_stats_strings[i],
			       ETH_GSTRING_LEN);
			string += ETH_GSTRING_LEN;
		}
		bmap = (u64)bnad->bna.tx_mod.txf_bmap[0] |
			((u64)bnad->bna.tx_mod.txf_bmap[1] << 32);
		for (i = 0; bmap && (i < BFI_LL_TXF_ID_MAX); i++) {
			if (bmap & 1) {
				sprintf(string, "txf%d_ucast_octets", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_ucast", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_ucast_vlan", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_mcast_octets", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_mcast", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_mcast_vlan", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_bcast_octets", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_bcast", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_bcast_vlan", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_errors", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_filter_vlan", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txf%d_filter_mac_sa", i);
				string += ETH_GSTRING_LEN;
			}
			bmap >>= 1;
		}

		bmap = (u64)bnad->bna.rx_mod.rxf_bmap[0] |
			((u64)bnad->bna.rx_mod.rxf_bmap[1] << 32);
		for (i = 0; bmap && (i < BFI_LL_RXF_ID_MAX); i++) {
			if (bmap & 1) {
				sprintf(string, "rxf%d_ucast_octets", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_ucast", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_ucast_vlan", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_mcast_octets", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_mcast", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_mcast_vlan", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_bcast_octets", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_bcast", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_bcast_vlan", i);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxf%d_frame_drops", i);
				string += ETH_GSTRING_LEN;
			}
			bmap >>= 1;
		}

		q_num = 0;
		for (i = 0; i < bnad->num_rx; i++) {
			if (!bnad->rx_info[i].rx)
				continue;
			for (j = 0; j < bnad->num_rxp_per_rx; j++) {
				sprintf(string, "cq%d_producer_index", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "cq%d_consumer_index", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "cq%d_hw_producer_index",
					q_num);
				string += ETH_GSTRING_LEN;
				q_num++;
			}
		}

		q_num = 0;
		for (i = 0; i < bnad->num_rx; i++) {
			if (!bnad->rx_info[i].rx)
				continue;
			for (j = 0; j < bnad->num_rxp_per_rx; j++) {
				sprintf(string, "rxq%d_packets", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxq%d_bytes", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxq%d_packets_with_error",
								q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxq%d_allocbuf_failed", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxq%d_producer_index", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "rxq%d_consumer_index", q_num);
				string += ETH_GSTRING_LEN;
				q_num++;
				if (bnad->rx_info[i].rx_ctrl[j].ccb &&
					bnad->rx_info[i].rx_ctrl[j].ccb->
					rcb[1] &&
					bnad->rx_info[i].rx_ctrl[j].ccb->
					rcb[1]->rxq) {
					sprintf(string, "rxq%d_packets", q_num);
					string += ETH_GSTRING_LEN;
					sprintf(string, "rxq%d_bytes", q_num);
					string += ETH_GSTRING_LEN;
					sprintf(string,
					"rxq%d_packets_with_error", q_num);
					string += ETH_GSTRING_LEN;
					sprintf(string, "rxq%d_allocbuf_failed",
								q_num);
					string += ETH_GSTRING_LEN;
					sprintf(string, "rxq%d_producer_index",
								q_num);
					string += ETH_GSTRING_LEN;
					sprintf(string, "rxq%d_consumer_index",
								q_num);
					string += ETH_GSTRING_LEN;
					q_num++;
				}
			}
		}

		q_num = 0;
		for (i = 0; i < bnad->num_tx; i++) {
			if (!bnad->tx_info[i].tx)
				continue;
			for (j = 0; j < bnad->num_txq_per_tx; j++) {
				sprintf(string, "txq%d_packets", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txq%d_bytes", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txq%d_producer_index", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txq%d_consumer_index", q_num);
				string += ETH_GSTRING_LEN;
				sprintf(string, "txq%d_hw_consumer_index",
									q_num);
				string += ETH_GSTRING_LEN;
				q_num++;
			}
		}

		break;

	default:
		break;
	}

	mutex_unlock(&bnad->conf_mutex);
}

static int
bnad_get_stats_count_locked(struct net_device *netdev)
{
	struct bnad *bnad = netdev_priv(netdev);
	int i, j, count, rxf_active_num = 0, txf_active_num = 0;
	u64 bmap;

	bmap = (u64)bnad->bna.tx_mod.txf_bmap[0] |
			((u64)bnad->bna.tx_mod.txf_bmap[1] << 32);
	for (i = 0; bmap && (i < BFI_LL_TXF_ID_MAX); i++) {
		if (bmap & 1)
			txf_active_num++;
		bmap >>= 1;
	}
	bmap = (u64)bnad->bna.rx_mod.rxf_bmap[0] |
			((u64)bnad->bna.rx_mod.rxf_bmap[1] << 32);
	for (i = 0; bmap && (i < BFI_LL_RXF_ID_MAX); i++) {
		if (bmap & 1)
			rxf_active_num++;
		bmap >>= 1;
	}
	count = BNAD_ETHTOOL_STATS_NUM +
		txf_active_num * BNAD_NUM_TXF_COUNTERS +
		rxf_active_num * BNAD_NUM_RXF_COUNTERS;

	for (i = 0; i < bnad->num_rx; i++) {
		if (!bnad->rx_info[i].rx)
			continue;
		count += bnad->num_rxp_per_rx * BNAD_NUM_CQ_COUNTERS;
		count += bnad->num_rxp_per_rx * BNAD_NUM_RXQ_COUNTERS;
		for (j = 0; j < bnad->num_rxp_per_rx; j++)
			if (bnad->rx_info[i].rx_ctrl[j].ccb &&
				bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1] &&
				bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1]->rxq)
				count +=  BNAD_NUM_RXQ_COUNTERS;
	}

	for (i = 0; i < bnad->num_tx; i++) {
		if (!bnad->tx_info[i].tx)
			continue;
		count += bnad->num_txq_per_tx * BNAD_NUM_TXQ_COUNTERS;
	}
	return count;
}

static int
bnad_per_q_stats_fill(struct bnad *bnad, u64 *buf, int bi)
{
	int i, j;
	struct bna_rcb *rcb = NULL;
	struct bna_tcb *tcb = NULL;

	for (i = 0; i < bnad->num_rx; i++) {
		if (!bnad->rx_info[i].rx)
			continue;
		for (j = 0; j < bnad->num_rxp_per_rx; j++)
			if (bnad->rx_info[i].rx_ctrl[j].ccb &&
				bnad->rx_info[i].rx_ctrl[j].ccb->rcb[0] &&
				bnad->rx_info[i].rx_ctrl[j].ccb->rcb[0]->rxq) {
				buf[bi++] = bnad->rx_info[i].rx_ctrl[j].
						ccb->producer_index;
				buf[bi++] = 0; /* ccb->consumer_index */
				buf[bi++] = *(bnad->rx_info[i].rx_ctrl[j].
						ccb->hw_producer_index);
			}
	}
	for (i = 0; i < bnad->num_rx; i++) {
		if (!bnad->rx_info[i].rx)
			continue;
		for (j = 0; j < bnad->num_rxp_per_rx; j++)
			if (bnad->rx_info[i].rx_ctrl[j].ccb) {
				if (bnad->rx_info[i].rx_ctrl[j].ccb->rcb[0] &&
					bnad->rx_info[i].rx_ctrl[j].ccb->
					rcb[0]->rxq) {
					rcb = bnad->rx_info[i].rx_ctrl[j].
							ccb->rcb[0];
					buf[bi++] = rcb->rxq->rx_packets;
					buf[bi++] = rcb->rxq->rx_bytes;
					buf[bi++] = rcb->rxq->
							rx_packets_with_error;
					buf[bi++] = rcb->rxq->
							rxbuf_alloc_failed;
					buf[bi++] = rcb->producer_index;
					buf[bi++] = rcb->consumer_index;
				}
				if (bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1] &&
					bnad->rx_info[i].rx_ctrl[j].ccb->
					rcb[1]->rxq) {
					rcb = bnad->rx_info[i].rx_ctrl[j].
								ccb->rcb[1];
					buf[bi++] = rcb->rxq->rx_packets;
					buf[bi++] = rcb->rxq->rx_bytes;
					buf[bi++] = rcb->rxq->
							rx_packets_with_error;
					buf[bi++] = rcb->rxq->
							rxbuf_alloc_failed;
					buf[bi++] = rcb->producer_index;
					buf[bi++] = rcb->consumer_index;
				}
			}
	}

	for (i = 0; i < bnad->num_tx; i++) {
		if (!bnad->tx_info[i].tx)
			continue;
		for (j = 0; j < bnad->num_txq_per_tx; j++)
			if (bnad->tx_info[i].tcb[j] &&
				bnad->tx_info[i].tcb[j]->txq) {
				tcb = bnad->tx_info[i].tcb[j];
				buf[bi++] = tcb->txq->tx_packets;
				buf[bi++] = tcb->txq->tx_bytes;
				buf[bi++] = tcb->producer_index;
				buf[bi++] = tcb->consumer_index;
				buf[bi++] = *(tcb->hw_consumer_index);
			}
	}

	return bi;
}

static void
bnad_get_ethtool_stats(struct net_device *netdev, struct ethtool_stats *stats,
		       u64 *buf)
{
	struct bnad *bnad = netdev_priv(netdev);
	int i, j, bi;
	unsigned long flags;
	struct rtnl_link_stats64 *net_stats64;
	u64 *stats64;
	u64 bmap;

	mutex_lock(&bnad->conf_mutex);
	if (bnad_get_stats_count_locked(netdev) != stats->n_stats) {
		mutex_unlock(&bnad->conf_mutex);
		return;
	}

	/*
	 * Used bna_lock to sync reads from bna_stats, which is written
	 * under the same lock
	 */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bi = 0;
	memset(buf, 0, stats->n_stats * sizeof(u64));

	net_stats64 = (struct rtnl_link_stats64 *)buf;
	bnad_netdev_qstats_fill(bnad, net_stats64);
	bnad_netdev_hwstats_fill(bnad, net_stats64);

	bi = sizeof(*net_stats64) / sizeof(u64);

	/* Get netif_queue_stopped from stack */
	bnad->stats.drv_stats.netif_queue_stopped = netif_queue_stopped(netdev);

	/* Fill driver stats into ethtool buffers */
	stats64 = (u64 *)&bnad->stats.drv_stats;
	for (i = 0; i < sizeof(struct bnad_drv_stats) / sizeof(u64); i++)
		buf[bi++] = stats64[i];

	/* Fill hardware stats excluding the rxf/txf into ethtool bufs */
	stats64 = (u64 *) bnad->stats.bna_stats->hw_stats;
	for (i = 0;
	     i < offsetof(struct bfi_ll_stats, rxf_stats[0]) / sizeof(u64);
	     i++)
		buf[bi++] = stats64[i];

	/* Fill txf stats into ethtool buffers */
	bmap = (u64)bnad->bna.tx_mod.txf_bmap[0] |
			((u64)bnad->bna.tx_mod.txf_bmap[1] << 32);
	for (i = 0; bmap && (i < BFI_LL_TXF_ID_MAX); i++) {
		if (bmap & 1) {
			stats64 = (u64 *)&bnad->stats.bna_stats->
						hw_stats->txf_stats[i];
			for (j = 0; j < sizeof(struct bfi_ll_stats_txf) /
					sizeof(u64); j++)
				buf[bi++] = stats64[j];
		}
		bmap >>= 1;
	}

	/*  Fill rxf stats into ethtool buffers */
	bmap = (u64)bnad->bna.rx_mod.rxf_bmap[0] |
			((u64)bnad->bna.rx_mod.rxf_bmap[1] << 32);
	for (i = 0; bmap && (i < BFI_LL_RXF_ID_MAX); i++) {
		if (bmap & 1) {
			stats64 = (u64 *)&bnad->stats.bna_stats->
						hw_stats->rxf_stats[i];
			for (j = 0; j < sizeof(struct bfi_ll_stats_rxf) /
					sizeof(u64); j++)
				buf[bi++] = stats64[j];
		}
		bmap >>= 1;
	}

	/* Fill per Q stats into ethtool buffers */
	bi = bnad_per_q_stats_fill(bnad, buf, bi);

	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	mutex_unlock(&bnad->conf_mutex);
}

static int
bnad_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return bnad_get_stats_count_locked(netdev);
	default:
		return -EOPNOTSUPP;
	}
}

static struct ethtool_ops bnad_ethtool_ops = {
	.get_settings = bnad_get_settings,
	.set_settings = bnad_set_settings,
	.get_drvinfo = bnad_get_drvinfo,
	.get_regs_len = bnad_get_regs_len,
	.get_regs = bnad_get_regs,
	.get_wol = bnad_get_wol,
	.get_link = ethtool_op_get_link,
	.get_coalesce = bnad_get_coalesce,
	.set_coalesce = bnad_set_coalesce,
	.get_ringparam = bnad_get_ringparam,
	.set_ringparam = bnad_set_ringparam,
	.get_pauseparam = bnad_get_pauseparam,
	.set_pauseparam = bnad_set_pauseparam,
	.get_rx_csum = bnad_get_rx_csum,
	.set_rx_csum = bnad_set_rx_csum,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = bnad_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = bnad_set_tso,
	.get_strings = bnad_get_strings,
	.get_ethtool_stats = bnad_get_ethtool_stats,
	.get_sset_count = bnad_get_sset_count
};

void
bnad_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &bnad_ethtool_ops);
}
