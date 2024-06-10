/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH11K_HAL_H
#define ATH11K_HAL_H

#include "hal_desc.h"
#include "rx_desc.h"

struct ath11k_base;

#define HAL_LINK_DESC_SIZE			(32 << 2)
#define HAL_LINK_DESC_ALIGN			128
#define HAL_NUM_MPDUS_PER_LINK_DESC		6
#define HAL_NUM_TX_MSDUS_PER_LINK_DESC		7
#define HAL_NUM_RX_MSDUS_PER_LINK_DESC		6
#define HAL_NUM_MPDU_LINKS_PER_QUEUE_DESC	12
#define HAL_MAX_AVAIL_BLK_RES			3

#define HAL_RING_BASE_ALIGN	8

#define HAL_WBM_IDLE_SCATTER_BUF_SIZE_MAX	32704
/* TODO: Check with hw team on the supported scatter buf size */
#define HAL_WBM_IDLE_SCATTER_NEXT_PTR_SIZE	8
#define HAL_WBM_IDLE_SCATTER_BUF_SIZE (HAL_WBM_IDLE_SCATTER_BUF_SIZE_MAX - \
				       HAL_WBM_IDLE_SCATTER_NEXT_PTR_SIZE)

#define HAL_DSCP_TID_MAP_TBL_NUM_ENTRIES_MAX	48
#define HAL_DSCP_TID_TBL_SIZE			24

/* calculate the register address from bar0 of shadow register x */
#define HAL_SHADOW_BASE_ADDR(ab)		ab->hw_params.regs->hal_shadow_base_addr
#define HAL_SHADOW_NUM_REGS			36
#define HAL_HP_OFFSET_IN_REG_START		1
#define HAL_OFFSET_FROM_HP_TO_TP		4

#define HAL_SHADOW_REG(ab, x) (HAL_SHADOW_BASE_ADDR(ab) + (4 * (x)))

/* WCSS Relative address */
#define HAL_SEQ_WCSS_UMAC_OFFSET		0x00a00000
#define HAL_SEQ_WCSS_UMAC_REO_REG		0x00a38000
#define HAL_SEQ_WCSS_UMAC_TCL_REG		0x00a44000
#define HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(x) \
		(ab->hw_params.regs->hal_seq_wcss_umac_ce0_src_reg)
#define HAL_SEQ_WCSS_UMAC_CE0_DST_REG(x) \
		(ab->hw_params.regs->hal_seq_wcss_umac_ce0_dst_reg)
#define HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(x) \
		(ab->hw_params.regs->hal_seq_wcss_umac_ce1_src_reg)
#define HAL_SEQ_WCSS_UMAC_CE1_DST_REG(x) \
		(ab->hw_params.regs->hal_seq_wcss_umac_ce1_dst_reg)
#define HAL_SEQ_WCSS_UMAC_WBM_REG		0x00a34000

#define HAL_CE_WFSS_CE_REG_BASE			0x01b80000
#define HAL_WLAON_REG_BASE			0x01f80000

/* SW2TCL(x) R0 ring configuration address */
#define HAL_TCL1_RING_CMN_CTRL_REG		0x00000014
#define HAL_TCL1_RING_DSCP_TID_MAP		0x0000002c
#define HAL_TCL1_RING_BASE_LSB(ab)		ab->hw_params.regs->hal_tcl1_ring_base_lsb
#define HAL_TCL1_RING_BASE_MSB(ab)		ab->hw_params.regs->hal_tcl1_ring_base_msb
#define HAL_TCL1_RING_ID(ab)			ab->hw_params.regs->hal_tcl1_ring_id
#define HAL_TCL1_RING_MISC(ab)			ab->hw_params.regs->hal_tcl1_ring_misc
#define HAL_TCL1_RING_TP_ADDR_LSB(ab) \
	ab->hw_params.regs->hal_tcl1_ring_tp_addr_lsb
#define HAL_TCL1_RING_TP_ADDR_MSB(ab) \
	ab->hw_params.regs->hal_tcl1_ring_tp_addr_msb
#define HAL_TCL1_RING_CONSUMER_INT_SETUP_IX0(ab) \
	ab->hw_params.regs->hal_tcl1_ring_consumer_int_setup_ix0
#define HAL_TCL1_RING_CONSUMER_INT_SETUP_IX1(ab) \
	ab->hw_params.regs->hal_tcl1_ring_consumer_int_setup_ix1
#define HAL_TCL1_RING_MSI1_BASE_LSB(ab) \
	ab->hw_params.regs->hal_tcl1_ring_msi1_base_lsb
#define HAL_TCL1_RING_MSI1_BASE_MSB(ab) \
	ab->hw_params.regs->hal_tcl1_ring_msi1_base_msb
#define HAL_TCL1_RING_MSI1_DATA(ab) \
	ab->hw_params.regs->hal_tcl1_ring_msi1_data
#define HAL_TCL2_RING_BASE_LSB(ab)		ab->hw_params.regs->hal_tcl2_ring_base_lsb
#define HAL_TCL_RING_BASE_LSB(ab)		ab->hw_params.regs->hal_tcl_ring_base_lsb

#define HAL_TCL1_RING_MSI1_BASE_LSB_OFFSET(ab)				\
	(HAL_TCL1_RING_MSI1_BASE_LSB(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_MSI1_BASE_MSB_OFFSET(ab)				\
	(HAL_TCL1_RING_MSI1_BASE_MSB(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_MSI1_DATA_OFFSET(ab)				\
	(HAL_TCL1_RING_MSI1_DATA(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_BASE_MSB_OFFSET(ab)				\
	(HAL_TCL1_RING_BASE_MSB(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_ID_OFFSET(ab)				\
	(HAL_TCL1_RING_ID(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_CONSR_INT_SETUP_IX0_OFFSET(ab)			\
	(HAL_TCL1_RING_CONSUMER_INT_SETUP_IX0(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_CONSR_INT_SETUP_IX1_OFFSET(ab) \
		(HAL_TCL1_RING_CONSUMER_INT_SETUP_IX1(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_TP_ADDR_LSB_OFFSET(ab) \
		(HAL_TCL1_RING_TP_ADDR_LSB(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_TP_ADDR_MSB_OFFSET(ab) \
		(HAL_TCL1_RING_TP_ADDR_MSB(ab) - HAL_TCL1_RING_BASE_LSB(ab))
#define HAL_TCL1_RING_MISC_OFFSET(ab) \
		(HAL_TCL1_RING_MISC(ab) - HAL_TCL1_RING_BASE_LSB(ab))

/* SW2TCL(x) R2 ring pointers (head/tail) address */
#define HAL_TCL1_RING_HP			0x00002000
#define HAL_TCL1_RING_TP			0x00002004
#define HAL_TCL2_RING_HP			0x00002008
#define HAL_TCL_RING_HP				0x00002018

#define HAL_TCL1_RING_TP_OFFSET \
		(HAL_TCL1_RING_TP - HAL_TCL1_RING_HP)

/* TCL STATUS ring address */
#define HAL_TCL_STATUS_RING_BASE_LSB(ab) \
	ab->hw_params.regs->hal_tcl_status_ring_base_lsb
#define HAL_TCL_STATUS_RING_HP			0x00002030

/* REO2SW(x) R0 ring configuration address */
#define HAL_REO1_GEN_ENABLE			0x00000000
#define HAL_REO1_DEST_RING_CTRL_IX_0		0x00000004
#define HAL_REO1_DEST_RING_CTRL_IX_1		0x00000008
#define HAL_REO1_DEST_RING_CTRL_IX_2		0x0000000c
#define HAL_REO1_DEST_RING_CTRL_IX_3		0x00000010
#define HAL_REO1_MISC_CTL(ab)			ab->hw_params.regs->hal_reo1_misc_ctl
#define HAL_REO1_RING_BASE_LSB(ab)		ab->hw_params.regs->hal_reo1_ring_base_lsb
#define HAL_REO1_RING_BASE_MSB(ab)		ab->hw_params.regs->hal_reo1_ring_base_msb
#define HAL_REO1_RING_ID(ab)			ab->hw_params.regs->hal_reo1_ring_id
#define HAL_REO1_RING_MISC(ab)			ab->hw_params.regs->hal_reo1_ring_misc
#define HAL_REO1_RING_HP_ADDR_LSB(ab) \
	ab->hw_params.regs->hal_reo1_ring_hp_addr_lsb
#define HAL_REO1_RING_HP_ADDR_MSB(ab) \
	ab->hw_params.regs->hal_reo1_ring_hp_addr_msb
#define HAL_REO1_RING_PRODUCER_INT_SETUP(ab) \
	ab->hw_params.regs->hal_reo1_ring_producer_int_setup
#define HAL_REO1_RING_MSI1_BASE_LSB(ab) \
	ab->hw_params.regs->hal_reo1_ring_msi1_base_lsb
#define HAL_REO1_RING_MSI1_BASE_MSB(ab) \
	ab->hw_params.regs->hal_reo1_ring_msi1_base_msb
#define HAL_REO1_RING_MSI1_DATA(ab) \
	ab->hw_params.regs->hal_reo1_ring_msi1_data
#define HAL_REO2_RING_BASE_LSB(ab)		ab->hw_params.regs->hal_reo2_ring_base_lsb
#define HAL_REO1_AGING_THRESH_IX_0(ab) \
	ab->hw_params.regs->hal_reo1_aging_thresh_ix_0
#define HAL_REO1_AGING_THRESH_IX_1(ab) \
	ab->hw_params.regs->hal_reo1_aging_thresh_ix_1
#define HAL_REO1_AGING_THRESH_IX_2(ab) \
	ab->hw_params.regs->hal_reo1_aging_thresh_ix_2
#define HAL_REO1_AGING_THRESH_IX_3(ab) \
	ab->hw_params.regs->hal_reo1_aging_thresh_ix_3

#define HAL_REO1_RING_MSI1_BASE_LSB_OFFSET(ab) \
		(HAL_REO1_RING_MSI1_BASE_LSB(ab) - HAL_REO1_RING_BASE_LSB(ab))
#define HAL_REO1_RING_MSI1_BASE_MSB_OFFSET(ab) \
		(HAL_REO1_RING_MSI1_BASE_MSB(ab) - HAL_REO1_RING_BASE_LSB(ab))
#define HAL_REO1_RING_MSI1_DATA_OFFSET(ab) \
		(HAL_REO1_RING_MSI1_DATA(ab) - HAL_REO1_RING_BASE_LSB(ab))
#define HAL_REO1_RING_BASE_MSB_OFFSET(ab) \
		(HAL_REO1_RING_BASE_MSB(ab) - HAL_REO1_RING_BASE_LSB(ab))
#define HAL_REO1_RING_ID_OFFSET(ab) (HAL_REO1_RING_ID(ab) - HAL_REO1_RING_BASE_LSB(ab))
#define HAL_REO1_RING_PRODUCER_INT_SETUP_OFFSET(ab) \
		(HAL_REO1_RING_PRODUCER_INT_SETUP(ab) - HAL_REO1_RING_BASE_LSB(ab))
#define HAL_REO1_RING_HP_ADDR_LSB_OFFSET(ab) \
		(HAL_REO1_RING_HP_ADDR_LSB(ab) - HAL_REO1_RING_BASE_LSB(ab))
#define HAL_REO1_RING_HP_ADDR_MSB_OFFSET(ab) \
		(HAL_REO1_RING_HP_ADDR_MSB(ab) - HAL_REO1_RING_BASE_LSB(ab))
#define HAL_REO1_RING_MISC_OFFSET(ab) \
	(HAL_REO1_RING_MISC(ab) - HAL_REO1_RING_BASE_LSB(ab))

/* REO2SW(x) R2 ring pointers (head/tail) address */
#define HAL_REO1_RING_HP(ab)			ab->hw_params.regs->hal_reo1_ring_hp
#define HAL_REO1_RING_TP(ab)			ab->hw_params.regs->hal_reo1_ring_tp
#define HAL_REO2_RING_HP(ab)			ab->hw_params.regs->hal_reo2_ring_hp

#define HAL_REO1_RING_TP_OFFSET(ab)	(HAL_REO1_RING_TP(ab) - HAL_REO1_RING_HP(ab))

/* REO2TCL R0 ring configuration address */
#define HAL_REO_TCL_RING_BASE_LSB(ab) \
	ab->hw_params.regs->hal_reo_tcl_ring_base_lsb

/* REO2TCL R2 ring pointer (head/tail) address */
#define HAL_REO_TCL_RING_HP(ab)			ab->hw_params.regs->hal_reo_tcl_ring_hp

/* REO CMD R0 address */
#define HAL_REO_CMD_RING_BASE_LSB(ab) \
	ab->hw_params.regs->hal_reo_cmd_ring_base_lsb

/* REO CMD R2 address */
#define HAL_REO_CMD_HP(ab)			ab->hw_params.regs->hal_reo_cmd_ring_hp

/* SW2REO R0 address */
#define HAL_SW2REO_RING_BASE_LSB(ab) \
	ab->hw_params.regs->hal_sw2reo_ring_base_lsb

/* SW2REO R2 address */
#define HAL_SW2REO_RING_HP(ab)			ab->hw_params.regs->hal_sw2reo_ring_hp

/* CE ring R0 address */
#define HAL_CE_DST_RING_BASE_LSB		0x00000000
#define HAL_CE_DST_STATUS_RING_BASE_LSB		0x00000058
#define HAL_CE_DST_RING_CTRL			0x000000b0

/* CE ring R2 address */
#define HAL_CE_DST_RING_HP			0x00000400
#define HAL_CE_DST_STATUS_RING_HP		0x00000408

/* REO status address */
#define HAL_REO_STATUS_RING_BASE_LSB(ab) \
	ab->hw_params.regs->hal_reo_status_ring_base_lsb
#define HAL_REO_STATUS_HP(ab)			ab->hw_params.regs->hal_reo_status_hp

/* WBM Idle R0 address */
#define HAL_WBM_IDLE_LINK_RING_BASE_LSB(x) \
		(ab->hw_params.regs->hal_wbm_idle_link_ring_base_lsb)
#define HAL_WBM_IDLE_LINK_RING_MISC_ADDR(x) \
		(ab->hw_params.regs->hal_wbm_idle_link_ring_misc)
#define HAL_WBM_R0_IDLE_LIST_CONTROL_ADDR	0x00000048
#define HAL_WBM_R0_IDLE_LIST_SIZE_ADDR		0x0000004c
#define HAL_WBM_SCATTERED_RING_BASE_LSB		0x00000058
#define HAL_WBM_SCATTERED_RING_BASE_MSB		0x0000005c
#define HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX0 0x00000068
#define HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX1 0x0000006c
#define HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX0 0x00000078
#define HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX1 0x0000007c
#define HAL_WBM_SCATTERED_DESC_PTR_HP_ADDR	 0x00000084

/* WBM Idle R2 address */
#define HAL_WBM_IDLE_LINK_RING_HP		0x000030b0

/* SW2WBM R0 release address */
#define HAL_WBM_RELEASE_RING_BASE_LSB(x) \
		(ab->hw_params.regs->hal_wbm_release_ring_base_lsb)

/* SW2WBM R2 release address */
#define HAL_WBM_RELEASE_RING_HP			0x00003018

/* WBM2SW R0 release address */
#define HAL_WBM0_RELEASE_RING_BASE_LSB(x) \
		(ab->hw_params.regs->hal_wbm0_release_ring_base_lsb)
#define HAL_WBM1_RELEASE_RING_BASE_LSB(x) \
		(ab->hw_params.regs->hal_wbm1_release_ring_base_lsb)

/* WBM2SW R2 release address */
#define HAL_WBM0_RELEASE_RING_HP		0x000030c0
#define HAL_WBM1_RELEASE_RING_HP		0x000030c8

/* TCL ring field mask and offset */
#define HAL_TCL1_RING_BASE_MSB_RING_SIZE		GENMASK(27, 8)
#define HAL_TCL1_RING_BASE_MSB_RING_BASE_ADDR_MSB	GENMASK(7, 0)
#define HAL_TCL1_RING_ID_ENTRY_SIZE			GENMASK(7, 0)
#define HAL_TCL1_RING_MISC_MSI_LOOPCNT_DISABLE		BIT(1)
#define HAL_TCL1_RING_MISC_MSI_SWAP			BIT(3)
#define HAL_TCL1_RING_MISC_HOST_FW_SWAP			BIT(4)
#define HAL_TCL1_RING_MISC_DATA_TLV_SWAP		BIT(5)
#define HAL_TCL1_RING_MISC_SRNG_ENABLE			BIT(6)
#define HAL_TCL1_RING_CONSR_INT_SETUP_IX0_INTR_TMR_THOLD   GENMASK(31, 16)
#define HAL_TCL1_RING_CONSR_INT_SETUP_IX0_BATCH_COUNTER_THOLD GENMASK(14, 0)
#define HAL_TCL1_RING_CONSR_INT_SETUP_IX1_LOW_THOLD	GENMASK(15, 0)
#define HAL_TCL1_RING_MSI1_BASE_MSB_MSI1_ENABLE		BIT(8)
#define HAL_TCL1_RING_MSI1_BASE_MSB_ADDR		GENMASK(7, 0)
#define HAL_TCL1_RING_CMN_CTRL_DSCP_TID_MAP_PROG_EN	BIT(17)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP		GENMASK(31, 0)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP0		GENMASK(2, 0)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP1		GENMASK(5, 3)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP2		GENMASK(8, 6)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP3		GENMASK(11, 9)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP4		GENMASK(14, 12)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP5		GENMASK(17, 15)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP6		GENMASK(20, 18)
#define HAL_TCL1_RING_FIELD_DSCP_TID_MAP7		GENMASK(23, 21)

/* REO ring field mask and offset */
#define HAL_REO1_RING_BASE_MSB_RING_SIZE		GENMASK(27, 8)
#define HAL_REO1_RING_BASE_MSB_RING_BASE_ADDR_MSB	GENMASK(7, 0)
#define HAL_REO1_RING_ID_RING_ID			GENMASK(15, 8)
#define HAL_REO1_RING_ID_ENTRY_SIZE			GENMASK(7, 0)
#define HAL_REO1_RING_MISC_MSI_SWAP			BIT(3)
#define HAL_REO1_RING_MISC_HOST_FW_SWAP			BIT(4)
#define HAL_REO1_RING_MISC_DATA_TLV_SWAP		BIT(5)
#define HAL_REO1_RING_MISC_SRNG_ENABLE			BIT(6)
#define HAL_REO1_RING_PRDR_INT_SETUP_INTR_TMR_THOLD	GENMASK(31, 16)
#define HAL_REO1_RING_PRDR_INT_SETUP_BATCH_COUNTER_THOLD GENMASK(14, 0)
#define HAL_REO1_RING_MSI1_BASE_MSB_MSI1_ENABLE		BIT(8)
#define HAL_REO1_RING_MSI1_BASE_MSB_ADDR		GENMASK(7, 0)
#define HAL_REO1_GEN_ENABLE_FRAG_DST_RING		GENMASK(25, 23)
#define HAL_REO1_GEN_ENABLE_AGING_LIST_ENABLE		BIT(2)
#define HAL_REO1_GEN_ENABLE_AGING_FLUSH_ENABLE		BIT(3)
#define HAL_REO1_MISC_CTL_FRAGMENT_DST_RING		GENMASK(20, 17)

/* CE ring bit field mask and shift */
#define HAL_CE_DST_R0_DEST_CTRL_MAX_LEN			GENMASK(15, 0)

#define HAL_ADDR_LSB_REG_MASK				0xffffffff

#define HAL_ADDR_MSB_REG_SHIFT				32

/* WBM ring bit field mask and shift */
#define HAL_WBM_LINK_DESC_IDLE_LIST_MODE		BIT(1)
#define HAL_WBM_SCATTER_BUFFER_SIZE			GENMASK(10, 2)
#define HAL_WBM_SCATTER_RING_SIZE_OF_IDLE_LINK_DESC_LIST GENMASK(31, 16)
#define HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32	GENMASK(7, 0)
#define HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_MATCH_TAG	GENMASK(31, 8)

#define HAL_WBM_SCATTERED_DESC_HEAD_P_OFFSET_IX1	GENMASK(20, 8)
#define HAL_WBM_SCATTERED_DESC_TAIL_P_OFFSET_IX1	GENMASK(20, 8)

#define BASE_ADDR_MATCH_TAG_VAL 0x5

#define HAL_REO_REO2SW1_RING_BASE_MSB_RING_SIZE		0x000fffff
#define HAL_REO_REO2TCL_RING_BASE_MSB_RING_SIZE		0x000fffff
#define HAL_REO_SW2REO_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_REO_CMD_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_REO_STATUS_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_SW2TCL1_RING_BASE_MSB_RING_SIZE		0x000fffff
#define HAL_SW2TCL1_CMD_RING_BASE_MSB_RING_SIZE		0x000fffff
#define HAL_TCL_STATUS_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_CE_SRC_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_CE_DST_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_CE_DST_STATUS_RING_BASE_MSB_RING_SIZE	0x0000ffff
#define HAL_WBM_IDLE_LINK_RING_BASE_MSB_RING_SIZE	0x0000ffff
#define HAL_SW2WBM_RELEASE_RING_BASE_MSB_RING_SIZE	0x0000ffff
#define HAL_WBM2SW_RELEASE_RING_BASE_MSB_RING_SIZE	0x000fffff
#define HAL_RXDMA_RING_MAX_SIZE				0x0000ffff

/* IPQ5018 ce registers */
#define HAL_IPQ5018_CE_WFSS_REG_BASE		0x08400000
#define HAL_IPQ5018_CE_SIZE			0x200000

/* Add any other errors here and return them in
 * ath11k_hal_rx_desc_get_err().
 */

enum hal_srng_ring_id {
	HAL_SRNG_RING_ID_REO2SW1 = 0,
	HAL_SRNG_RING_ID_REO2SW2,
	HAL_SRNG_RING_ID_REO2SW3,
	HAL_SRNG_RING_ID_REO2SW4,
	HAL_SRNG_RING_ID_REO2TCL,
	HAL_SRNG_RING_ID_SW2REO,

	HAL_SRNG_RING_ID_REO_CMD = 8,
	HAL_SRNG_RING_ID_REO_STATUS,

	HAL_SRNG_RING_ID_SW2TCL1 = 16,
	HAL_SRNG_RING_ID_SW2TCL2,
	HAL_SRNG_RING_ID_SW2TCL3,
	HAL_SRNG_RING_ID_SW2TCL4,

	HAL_SRNG_RING_ID_SW2TCL_CMD = 24,
	HAL_SRNG_RING_ID_TCL_STATUS,

	HAL_SRNG_RING_ID_CE0_SRC = 32,
	HAL_SRNG_RING_ID_CE1_SRC,
	HAL_SRNG_RING_ID_CE2_SRC,
	HAL_SRNG_RING_ID_CE3_SRC,
	HAL_SRNG_RING_ID_CE4_SRC,
	HAL_SRNG_RING_ID_CE5_SRC,
	HAL_SRNG_RING_ID_CE6_SRC,
	HAL_SRNG_RING_ID_CE7_SRC,
	HAL_SRNG_RING_ID_CE8_SRC,
	HAL_SRNG_RING_ID_CE9_SRC,
	HAL_SRNG_RING_ID_CE10_SRC,
	HAL_SRNG_RING_ID_CE11_SRC,

	HAL_SRNG_RING_ID_CE0_DST = 56,
	HAL_SRNG_RING_ID_CE1_DST,
	HAL_SRNG_RING_ID_CE2_DST,
	HAL_SRNG_RING_ID_CE3_DST,
	HAL_SRNG_RING_ID_CE4_DST,
	HAL_SRNG_RING_ID_CE5_DST,
	HAL_SRNG_RING_ID_CE6_DST,
	HAL_SRNG_RING_ID_CE7_DST,
	HAL_SRNG_RING_ID_CE8_DST,
	HAL_SRNG_RING_ID_CE9_DST,
	HAL_SRNG_RING_ID_CE10_DST,
	HAL_SRNG_RING_ID_CE11_DST,

	HAL_SRNG_RING_ID_CE0_DST_STATUS = 80,
	HAL_SRNG_RING_ID_CE1_DST_STATUS,
	HAL_SRNG_RING_ID_CE2_DST_STATUS,
	HAL_SRNG_RING_ID_CE3_DST_STATUS,
	HAL_SRNG_RING_ID_CE4_DST_STATUS,
	HAL_SRNG_RING_ID_CE5_DST_STATUS,
	HAL_SRNG_RING_ID_CE6_DST_STATUS,
	HAL_SRNG_RING_ID_CE7_DST_STATUS,
	HAL_SRNG_RING_ID_CE8_DST_STATUS,
	HAL_SRNG_RING_ID_CE9_DST_STATUS,
	HAL_SRNG_RING_ID_CE10_DST_STATUS,
	HAL_SRNG_RING_ID_CE11_DST_STATUS,

	HAL_SRNG_RING_ID_WBM_IDLE_LINK = 104,
	HAL_SRNG_RING_ID_WBM_SW_RELEASE,
	HAL_SRNG_RING_ID_WBM2SW0_RELEASE,
	HAL_SRNG_RING_ID_WBM2SW1_RELEASE,
	HAL_SRNG_RING_ID_WBM2SW2_RELEASE,
	HAL_SRNG_RING_ID_WBM2SW3_RELEASE,
	HAL_SRNG_RING_ID_WBM2SW4_RELEASE,

	HAL_SRNG_RING_ID_UMAC_ID_END = 127,
	HAL_SRNG_RING_ID_LMAC1_ID_START,

	HAL_SRNG_RING_ID_WMAC1_SW2RXDMA0_BUF = HAL_SRNG_RING_ID_LMAC1_ID_START,
	HAL_SRNG_RING_ID_WMAC1_SW2RXDMA1_BUF,
	HAL_SRNG_RING_ID_WMAC1_SW2RXDMA2_BUF,
	HAL_SRNG_RING_ID_WMAC1_SW2RXDMA0_STATBUF,
	HAL_SRNG_RING_ID_WMAC1_SW2RXDMA1_STATBUF,
	HAL_SRNG_RING_ID_WMAC1_RXDMA2SW0,
	HAL_SRNG_RING_ID_WMAC1_RXDMA2SW1,
	HAL_SRNG_RING_ID_WMAC1_SW2RXDMA1_DESC,
	HAL_SRNG_RING_ID_RXDMA_DIR_BUF,

	HAL_SRNG_RING_ID_LMAC1_ID_END = 143
};

/* SRNG registers are split into two groups R0 and R2 */
#define HAL_SRNG_REG_GRP_R0	0
#define HAL_SRNG_REG_GRP_R2	1
#define HAL_SRNG_NUM_REG_GRP    2

#define HAL_SRNG_NUM_LMACS      3
#define HAL_SRNG_REO_EXCEPTION  HAL_SRNG_RING_ID_REO2SW1
#define HAL_SRNG_RINGS_PER_LMAC (HAL_SRNG_RING_ID_LMAC1_ID_END - \
				 HAL_SRNG_RING_ID_LMAC1_ID_START)
#define HAL_SRNG_NUM_LMAC_RINGS (HAL_SRNG_NUM_LMACS * HAL_SRNG_RINGS_PER_LMAC)
#define HAL_SRNG_RING_ID_MAX    (HAL_SRNG_RING_ID_UMAC_ID_END + \
				 HAL_SRNG_NUM_LMAC_RINGS)

enum hal_ring_type {
	HAL_REO_DST,
	HAL_REO_EXCEPTION,
	HAL_REO_REINJECT,
	HAL_REO_CMD,
	HAL_REO_STATUS,
	HAL_TCL_DATA,
	HAL_TCL_CMD,
	HAL_TCL_STATUS,
	HAL_CE_SRC,
	HAL_CE_DST,
	HAL_CE_DST_STATUS,
	HAL_WBM_IDLE_LINK,
	HAL_SW2WBM_RELEASE,
	HAL_WBM2SW_RELEASE,
	HAL_RXDMA_BUF,
	HAL_RXDMA_DST,
	HAL_RXDMA_MONITOR_BUF,
	HAL_RXDMA_MONITOR_STATUS,
	HAL_RXDMA_MONITOR_DST,
	HAL_RXDMA_MONITOR_DESC,
	HAL_RXDMA_DIR_BUF,
	HAL_MAX_RING_TYPES,
};

#define HAL_RX_MAX_BA_WINDOW	256

#define HAL_DEFAULT_REO_TIMEOUT_USEC		(40 * 1000)

/**
 * enum hal_reo_cmd_type: Enum for REO command type
 * @HAL_REO_CMD_GET_QUEUE_STATS: Get REO queue status/stats
 * @HAL_REO_CMD_FLUSH_QUEUE: Flush all frames in REO queue
 * @HAL_REO_CMD_FLUSH_CACHE: Flush descriptor entries in the cache
 * @HAL_REO_CMD_UNBLOCK_CACHE: Unblock a descriptor's address that was blocked
 *      earlier with a 'REO_FLUSH_CACHE' command
 * @HAL_REO_CMD_FLUSH_TIMEOUT_LIST: Flush buffers/descriptors from timeout list
 * @HAL_REO_CMD_UPDATE_RX_QUEUE: Update REO queue settings
 */
enum hal_reo_cmd_type {
	HAL_REO_CMD_GET_QUEUE_STATS     = 0,
	HAL_REO_CMD_FLUSH_QUEUE         = 1,
	HAL_REO_CMD_FLUSH_CACHE         = 2,
	HAL_REO_CMD_UNBLOCK_CACHE       = 3,
	HAL_REO_CMD_FLUSH_TIMEOUT_LIST  = 4,
	HAL_REO_CMD_UPDATE_RX_QUEUE     = 5,
};

/**
 * enum hal_reo_cmd_status: Enum for execution status of REO command
 * @HAL_REO_CMD_SUCCESS: Command has successfully executed
 * @HAL_REO_CMD_BLOCKED: Command could not be executed as the queue
 *			 or cache was blocked
 * @HAL_REO_CMD_FAILED: Command execution failed, could be due to
 *			invalid queue desc
 * @HAL_REO_CMD_RESOURCE_BLOCKED:
 * @HAL_REO_CMD_DRAIN:
 */
enum hal_reo_cmd_status {
	HAL_REO_CMD_SUCCESS		= 0,
	HAL_REO_CMD_BLOCKED		= 1,
	HAL_REO_CMD_FAILED		= 2,
	HAL_REO_CMD_RESOURCE_BLOCKED	= 3,
	HAL_REO_CMD_DRAIN		= 0xff,
};

struct hal_wbm_idle_scatter_list {
	dma_addr_t paddr;
	struct hal_wbm_link_desc *vaddr;
};

struct hal_srng_params {
	dma_addr_t ring_base_paddr;
	u32 *ring_base_vaddr;
	int num_entries;
	u32 intr_batch_cntr_thres_entries;
	u32 intr_timer_thres_us;
	u32 flags;
	u32 max_buffer_len;
	u32 low_threshold;
	dma_addr_t msi_addr;
	u32 msi_data;

	/* Add more params as needed */
};

enum hal_srng_dir {
	HAL_SRNG_DIR_SRC,
	HAL_SRNG_DIR_DST
};

/* srng flags */
#define HAL_SRNG_FLAGS_MSI_SWAP			0x00000008
#define HAL_SRNG_FLAGS_RING_PTR_SWAP		0x00000010
#define HAL_SRNG_FLAGS_DATA_TLV_SWAP		0x00000020
#define HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN	0x00010000
#define HAL_SRNG_FLAGS_MSI_INTR			0x00020000
#define HAL_SRNG_FLAGS_CACHED                   0x20000000
#define HAL_SRNG_FLAGS_LMAC_RING		0x80000000
#define HAL_SRNG_FLAGS_REMAP_CE_RING        0x10000000

#define HAL_SRNG_TLV_HDR_TAG		GENMASK(9, 1)
#define HAL_SRNG_TLV_HDR_LEN		GENMASK(25, 10)

/* Common SRNG ring structure for source and destination rings */
struct hal_srng {
	/* Unique SRNG ring ID */
	u8 ring_id;

	/* Ring initialization done */
	u8 initialized;

	/* Interrupt/MSI value assigned to this ring */
	int irq;

	/* Physical base address of the ring */
	dma_addr_t ring_base_paddr;

	/* Virtual base address of the ring */
	u32 *ring_base_vaddr;

	/* Number of entries in ring */
	u32 num_entries;

	/* Ring size */
	u32 ring_size;

	/* Ring size mask */
	u32 ring_size_mask;

	/* Size of ring entry */
	u32 entry_size;

	/* Interrupt timer threshold - in micro seconds */
	u32 intr_timer_thres_us;

	/* Interrupt batch counter threshold - in number of ring entries */
	u32 intr_batch_cntr_thres_entries;

	/* MSI Address */
	dma_addr_t msi_addr;

	/* MSI data */
	u32 msi_data;

	/* Misc flags */
	u32 flags;

	/* Lock for serializing ring index updates */
	spinlock_t lock;

	/* Start offset of SRNG register groups for this ring
	 * TBD: See if this is required - register address can be derived
	 * from ring ID
	 */
	u32 hwreg_base[HAL_SRNG_NUM_REG_GRP];

	u64 timestamp;

	/* Source or Destination ring */
	enum hal_srng_dir ring_dir;

	union {
		struct {
			/* SW tail pointer */
			u32 tp;

			/* Shadow head pointer location to be updated by HW */
			volatile u32 *hp_addr;

			/* Cached head pointer */
			u32 cached_hp;

			/* Tail pointer location to be updated by SW - This
			 * will be a register address and need not be
			 * accessed through SW structure
			 */
			u32 *tp_addr;

			/* Current SW loop cnt */
			u32 loop_cnt;

			/* max transfer size */
			u16 max_buffer_length;

			/* head pointer at access end */
			u32 last_hp;
		} dst_ring;

		struct {
			/* SW head pointer */
			u32 hp;

			/* SW reap head pointer */
			u32 reap_hp;

			/* Shadow tail pointer location to be updated by HW */
			u32 *tp_addr;

			/* Cached tail pointer */
			u32 cached_tp;

			/* Head pointer location to be updated by SW - This
			 * will be a register address and need not be accessed
			 * through SW structure
			 */
			u32 *hp_addr;

			/* Low threshold - in number of ring entries */
			u32 low_threshold;

			/* tail pointer at access end */
			u32 last_tp;
		} src_ring;
	} u;
};

/* Interrupt mitigation - Batch threshold in terms of number of frames */
#define HAL_SRNG_INT_BATCH_THRESHOLD_TX 256
#define HAL_SRNG_INT_BATCH_THRESHOLD_RX 128
#define HAL_SRNG_INT_BATCH_THRESHOLD_OTHER 1

/* Interrupt mitigation - timer threshold in us */
#define HAL_SRNG_INT_TIMER_THRESHOLD_TX 1000
#define HAL_SRNG_INT_TIMER_THRESHOLD_RX 500
#define HAL_SRNG_INT_TIMER_THRESHOLD_OTHER 256

/* HW SRNG configuration table */
struct hal_srng_config {
	int start_ring_id;
	u16 max_rings;
	u16 entry_size;
	u32 reg_start[HAL_SRNG_NUM_REG_GRP];
	u16 reg_size[HAL_SRNG_NUM_REG_GRP];
	u8 lmac_ring;
	enum hal_srng_dir ring_dir;
	u32 max_size;
};

/**
 * enum hal_rx_buf_return_buf_manager - manager for returned rx buffers
 *
 * @HAL_RX_BUF_RBM_WBM_IDLE_BUF_LIST: Buffer returned to WBM idle buffer list
 * @HAL_RX_BUF_RBM_WBM_IDLE_DESC_LIST: Descriptor returned to WBM idle
 *	descriptor list.
 * @HAL_RX_BUF_RBM_FW_BM: Buffer returned to FW
 * @HAL_RX_BUF_RBM_SW0_BM: For Tx completion -- returned to host
 * @HAL_RX_BUF_RBM_SW1_BM: For Tx completion -- returned to host
 * @HAL_RX_BUF_RBM_SW2_BM: For Tx completion -- returned to host
 * @HAL_RX_BUF_RBM_SW3_BM: For Rx release -- returned to host
 * @HAL_RX_BUF_RBM_SW4_BM: For Tx completion -- returned to host
 */

enum hal_rx_buf_return_buf_manager {
	HAL_RX_BUF_RBM_WBM_IDLE_BUF_LIST,
	HAL_RX_BUF_RBM_WBM_IDLE_DESC_LIST,
	HAL_RX_BUF_RBM_FW_BM,
	HAL_RX_BUF_RBM_SW0_BM,
	HAL_RX_BUF_RBM_SW1_BM,
	HAL_RX_BUF_RBM_SW2_BM,
	HAL_RX_BUF_RBM_SW3_BM,
	HAL_RX_BUF_RBM_SW4_BM,
};

#define HAL_SRNG_DESC_LOOP_CNT		0xf0000000

#define HAL_REO_CMD_FLG_NEED_STATUS		BIT(0)
#define HAL_REO_CMD_FLG_STATS_CLEAR		BIT(1)
#define HAL_REO_CMD_FLG_FLUSH_BLOCK_LATER	BIT(2)
#define HAL_REO_CMD_FLG_FLUSH_RELEASE_BLOCKING	BIT(3)
#define HAL_REO_CMD_FLG_FLUSH_NO_INVAL		BIT(4)
#define HAL_REO_CMD_FLG_FLUSH_FWD_ALL_MPDUS	BIT(5)
#define HAL_REO_CMD_FLG_FLUSH_ALL		BIT(6)
#define HAL_REO_CMD_FLG_UNBLK_RESOURCE		BIT(7)
#define HAL_REO_CMD_FLG_UNBLK_CACHE		BIT(8)

/* Should be matching with HAL_REO_UPD_RX_QUEUE_INFO0_UPD_* feilds */
#define HAL_REO_CMD_UPD0_RX_QUEUE_NUM		BIT(8)
#define HAL_REO_CMD_UPD0_VLD			BIT(9)
#define HAL_REO_CMD_UPD0_ALDC			BIT(10)
#define HAL_REO_CMD_UPD0_DIS_DUP_DETECTION	BIT(11)
#define HAL_REO_CMD_UPD0_SOFT_REORDER_EN	BIT(12)
#define HAL_REO_CMD_UPD0_AC			BIT(13)
#define HAL_REO_CMD_UPD0_BAR			BIT(14)
#define HAL_REO_CMD_UPD0_RETRY			BIT(15)
#define HAL_REO_CMD_UPD0_CHECK_2K_MODE		BIT(16)
#define HAL_REO_CMD_UPD0_OOR_MODE		BIT(17)
#define HAL_REO_CMD_UPD0_BA_WINDOW_SIZE		BIT(18)
#define HAL_REO_CMD_UPD0_PN_CHECK		BIT(19)
#define HAL_REO_CMD_UPD0_EVEN_PN		BIT(20)
#define HAL_REO_CMD_UPD0_UNEVEN_PN		BIT(21)
#define HAL_REO_CMD_UPD0_PN_HANDLE_ENABLE	BIT(22)
#define HAL_REO_CMD_UPD0_PN_SIZE		BIT(23)
#define HAL_REO_CMD_UPD0_IGNORE_AMPDU_FLG	BIT(24)
#define HAL_REO_CMD_UPD0_SVLD			BIT(25)
#define HAL_REO_CMD_UPD0_SSN			BIT(26)
#define HAL_REO_CMD_UPD0_SEQ_2K_ERR		BIT(27)
#define HAL_REO_CMD_UPD0_PN_ERR			BIT(28)
#define HAL_REO_CMD_UPD0_PN_VALID		BIT(29)
#define HAL_REO_CMD_UPD0_PN			BIT(30)

/* Should be matching with HAL_REO_UPD_RX_QUEUE_INFO1_* feilds */
#define HAL_REO_CMD_UPD1_VLD			BIT(16)
#define HAL_REO_CMD_UPD1_ALDC			GENMASK(18, 17)
#define HAL_REO_CMD_UPD1_DIS_DUP_DETECTION	BIT(19)
#define HAL_REO_CMD_UPD1_SOFT_REORDER_EN	BIT(20)
#define HAL_REO_CMD_UPD1_AC			GENMASK(22, 21)
#define HAL_REO_CMD_UPD1_BAR			BIT(23)
#define HAL_REO_CMD_UPD1_RETRY			BIT(24)
#define HAL_REO_CMD_UPD1_CHECK_2K_MODE		BIT(25)
#define HAL_REO_CMD_UPD1_OOR_MODE		BIT(26)
#define HAL_REO_CMD_UPD1_PN_CHECK		BIT(27)
#define HAL_REO_CMD_UPD1_EVEN_PN		BIT(28)
#define HAL_REO_CMD_UPD1_UNEVEN_PN		BIT(29)
#define HAL_REO_CMD_UPD1_PN_HANDLE_ENABLE	BIT(30)
#define HAL_REO_CMD_UPD1_IGNORE_AMPDU_FLG	BIT(31)

/* Should be matching with HAL_REO_UPD_RX_QUEUE_INFO2_* feilds */
#define HAL_REO_CMD_UPD2_SVLD			BIT(10)
#define HAL_REO_CMD_UPD2_SSN			GENMASK(22, 11)
#define HAL_REO_CMD_UPD2_SEQ_2K_ERR		BIT(23)
#define HAL_REO_CMD_UPD2_PN_ERR			BIT(24)

#define HAL_REO_DEST_RING_CTRL_HASH_RING_MAP	GENMASK(31, 8)

struct ath11k_hal_reo_cmd {
	u32 addr_lo;
	u32 flag;
	u32 upd0;
	u32 upd1;
	u32 upd2;
	u32 pn[4];
	u16 rx_queue_num;
	u16 min_rel;
	u16 min_fwd;
	u8 addr_hi;
	u8 ac_list;
	u8 blocking_idx;
	u16 ba_window_size;
	u8 pn_size;
};

enum hal_pn_type {
	HAL_PN_TYPE_NONE,
	HAL_PN_TYPE_WPA,
	HAL_PN_TYPE_WAPI_EVEN,
	HAL_PN_TYPE_WAPI_UNEVEN,
};

enum hal_ce_desc {
	HAL_CE_DESC_SRC,
	HAL_CE_DESC_DST,
	HAL_CE_DESC_DST_STATUS,
};

#define HAL_HASH_ROUTING_RING_TCL 0
#define HAL_HASH_ROUTING_RING_SW1 1
#define HAL_HASH_ROUTING_RING_SW2 2
#define HAL_HASH_ROUTING_RING_SW3 3
#define HAL_HASH_ROUTING_RING_SW4 4
#define HAL_HASH_ROUTING_RING_REL 5
#define HAL_HASH_ROUTING_RING_FW  6

struct hal_reo_status_header {
	u16 cmd_num;
	enum hal_reo_cmd_status cmd_status;
	u16 cmd_exe_time;
	u32 timestamp;
};

struct hal_reo_status_queue_stats {
	u16 ssn;
	u16 curr_idx;
	u32 pn[4];
	u32 last_rx_queue_ts;
	u32 last_rx_dequeue_ts;
	u32 rx_bitmap[8]; /* Bitmap from 0-255 */
	u32 curr_mpdu_cnt;
	u32 curr_msdu_cnt;
	u16 fwd_due_to_bar_cnt;
	u16 dup_cnt;
	u32 frames_in_order_cnt;
	u32 num_mpdu_processed_cnt;
	u32 num_msdu_processed_cnt;
	u32 total_num_processed_byte_cnt;
	u32 late_rx_mpdu_cnt;
	u32 reorder_hole_cnt;
	u8 timeout_cnt;
	u8 bar_rx_cnt;
	u8 num_window_2k_jump_cnt;
};

struct hal_reo_status_flush_queue {
	bool err_detected;
};

enum hal_reo_status_flush_cache_err_code {
	HAL_REO_STATUS_FLUSH_CACHE_ERR_CODE_SUCCESS,
	HAL_REO_STATUS_FLUSH_CACHE_ERR_CODE_IN_USE,
	HAL_REO_STATUS_FLUSH_CACHE_ERR_CODE_NOT_FOUND,
};

struct hal_reo_status_flush_cache {
	bool err_detected;
	enum hal_reo_status_flush_cache_err_code err_code;
	bool cache_controller_flush_status_hit;
	u8 cache_controller_flush_status_desc_type;
	u8 cache_controller_flush_status_client_id;
	u8 cache_controller_flush_status_err;
	u8 cache_controller_flush_status_cnt;
};

enum hal_reo_status_unblock_cache_type {
	HAL_REO_STATUS_UNBLOCK_BLOCKING_RESOURCE,
	HAL_REO_STATUS_UNBLOCK_ENTIRE_CACHE_USAGE,
};

struct hal_reo_status_unblock_cache {
	bool err_detected;
	enum hal_reo_status_unblock_cache_type unblock_type;
};

struct hal_reo_status_flush_timeout_list {
	bool err_detected;
	bool list_empty;
	u16 release_desc_cnt;
	u16 fwd_buf_cnt;
};

enum hal_reo_threshold_idx {
	HAL_REO_THRESHOLD_IDX_DESC_COUNTER0,
	HAL_REO_THRESHOLD_IDX_DESC_COUNTER1,
	HAL_REO_THRESHOLD_IDX_DESC_COUNTER2,
	HAL_REO_THRESHOLD_IDX_DESC_COUNTER_SUM,
};

struct hal_reo_status_desc_thresh_reached {
	enum hal_reo_threshold_idx threshold_idx;
	u32 link_desc_counter0;
	u32 link_desc_counter1;
	u32 link_desc_counter2;
	u32 link_desc_counter_sum;
};

struct hal_reo_status {
	struct hal_reo_status_header uniform_hdr;
	u8 loop_cnt;
	union {
		struct hal_reo_status_queue_stats queue_stats;
		struct hal_reo_status_flush_queue flush_queue;
		struct hal_reo_status_flush_cache flush_cache;
		struct hal_reo_status_unblock_cache unblock_cache;
		struct hal_reo_status_flush_timeout_list timeout_list;
		struct hal_reo_status_desc_thresh_reached desc_thresh_reached;
	} u;
};

/* HAL context to be used to access SRNG APIs (currently used by data path
 * and transport (CE) modules)
 */
struct ath11k_hal {
	/* HAL internal state for all SRNG rings.
	 */
	struct hal_srng srng_list[HAL_SRNG_RING_ID_MAX];

	/* SRNG configuration table */
	struct hal_srng_config *srng_config;

	/* Remote pointer memory for HW/FW updates */
	struct {
		u32 *vaddr;
		dma_addr_t paddr;
	} rdp;

	/* Shared memory for ring pointer updates from host to FW */
	struct {
		u32 *vaddr;
		dma_addr_t paddr;
	} wrp;

	/* Available REO blocking resources bitmap */
	u8 avail_blk_resource;

	u8 current_blk_index;

	/* shadow register configuration */
	u32 shadow_reg_addr[HAL_SHADOW_NUM_REGS];
	int num_shadow_reg_configured;

	struct lock_class_key srng_key[HAL_SRNG_RING_ID_MAX];
};

u32 ath11k_hal_reo_qdesc_size(u32 ba_window_size, u8 tid);
void ath11k_hal_reo_qdesc_setup(void *vaddr, int tid, u32 ba_window_size,
				u32 start_seq, enum hal_pn_type type);
void ath11k_hal_reo_init_cmd_ring(struct ath11k_base *ab,
				  struct hal_srng *srng);
void ath11k_hal_setup_link_idle_list(struct ath11k_base *ab,
				     struct hal_wbm_idle_scatter_list *sbuf,
				     u32 nsbufs, u32 tot_link_desc,
				     u32 end_offset);

dma_addr_t ath11k_hal_srng_get_tp_addr(struct ath11k_base *ab,
				       struct hal_srng *srng);
dma_addr_t ath11k_hal_srng_get_hp_addr(struct ath11k_base *ab,
				       struct hal_srng *srng);
void ath11k_hal_set_link_desc_addr(struct hal_wbm_link_desc *desc, u32 cookie,
				   dma_addr_t paddr);
u32 ath11k_hal_ce_get_desc_size(enum hal_ce_desc type);
void ath11k_hal_ce_src_set_desc(void *buf, dma_addr_t paddr, u32 len, u32 id,
				u8 byte_swap_data);
void ath11k_hal_ce_dst_set_desc(void *buf, dma_addr_t paddr);
u32 ath11k_hal_ce_dst_status_get_length(void *buf);
int ath11k_hal_srng_get_entrysize(struct ath11k_base *ab, u32 ring_type);
int ath11k_hal_srng_get_max_entries(struct ath11k_base *ab, u32 ring_type);
void ath11k_hal_srng_get_params(struct ath11k_base *ab, struct hal_srng *srng,
				struct hal_srng_params *params);
u32 *ath11k_hal_srng_dst_get_next_entry(struct ath11k_base *ab,
					struct hal_srng *srng);
u32 *ath11k_hal_srng_dst_peek(struct ath11k_base *ab, struct hal_srng *srng);
int ath11k_hal_srng_dst_num_free(struct ath11k_base *ab, struct hal_srng *srng,
				 bool sync_hw_ptr);
u32 *ath11k_hal_srng_src_peek(struct ath11k_base *ab, struct hal_srng *srng);
u32 *ath11k_hal_srng_src_get_next_reaped(struct ath11k_base *ab,
					 struct hal_srng *srng);
u32 *ath11k_hal_srng_src_reap_next(struct ath11k_base *ab,
				   struct hal_srng *srng);
u32 *ath11k_hal_srng_src_get_next_entry(struct ath11k_base *ab,
					struct hal_srng *srng);
int ath11k_hal_srng_src_num_free(struct ath11k_base *ab, struct hal_srng *srng,
				 bool sync_hw_ptr);
void ath11k_hal_srng_access_begin(struct ath11k_base *ab,
				  struct hal_srng *srng);
void ath11k_hal_srng_access_end(struct ath11k_base *ab, struct hal_srng *srng);
int ath11k_hal_srng_setup(struct ath11k_base *ab, enum hal_ring_type type,
			  int ring_num, int mac_id,
			  struct hal_srng_params *params);
int ath11k_hal_srng_init(struct ath11k_base *ath11k);
void ath11k_hal_srng_deinit(struct ath11k_base *ath11k);
void ath11k_hal_dump_srng_stats(struct ath11k_base *ab);
void ath11k_hal_srng_get_shadow_config(struct ath11k_base *ab,
				       u32 **cfg, u32 *len);
int ath11k_hal_srng_update_shadow_config(struct ath11k_base *ab,
					 enum hal_ring_type ring_type,
					int ring_num);
void ath11k_hal_srng_shadow_config(struct ath11k_base *ab);
void ath11k_hal_srng_shadow_update_hp_tp(struct ath11k_base *ab,
					 struct hal_srng *srng);
#endif
