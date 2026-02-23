/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_HAL_WIFI7_H
#define ATH12K_HAL_WIFI7_H

#include "../core.h"
#include "../hal.h"
#include "hal_desc.h"
#include "hal_tx.h"
#include "hal_rx.h"
#include "hal_rx_desc.h"

/* calculate the register address from bar0 of shadow register x */
#define HAL_SHADOW_BASE_ADDR			0x000008fc
#define HAL_SHADOW_NUM_REGS			40
#define HAL_HP_OFFSET_IN_REG_START		1
#define HAL_OFFSET_FROM_HP_TO_TP		4

#define HAL_SHADOW_REG(x) (HAL_SHADOW_BASE_ADDR + (4 * (x)))
#define HAL_REO_QDESC_MAX_PEERID		8191

/* WCSS Relative address */
#define HAL_SEQ_WCSS_CMEM_OFFSET		0x00100000
#define HAL_SEQ_WCSS_UMAC_OFFSET		0x00a00000
#define HAL_SEQ_WCSS_UMAC_REO_REG		0x00a38000
#define HAL_SEQ_WCSS_UMAC_TCL_REG		0x00a44000
#define HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(hal) \
	((hal)->regs->umac_ce0_src_reg_base)
#define HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal) \
	((hal)->regs->umac_ce0_dest_reg_base)
#define HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(hal) \
	((hal)->regs->umac_ce1_src_reg_base)
#define HAL_SEQ_WCSS_UMAC_CE1_DST_REG(hal) \
	((hal)->regs->umac_ce1_dest_reg_base)
#define HAL_SEQ_WCSS_UMAC_WBM_REG		0x00a34000

#define HAL_CE_WFSS_CE_REG_BASE			0x01b80000

#define HAL_TCL_SW_CONFIG_BANK_ADDR		0x00a4408c

/* SW2TCL(x) R0 ring configuration address */
#define HAL_TCL1_RING_CMN_CTRL_REG		0x00000020
#define HAL_TCL1_RING_DSCP_TID_MAP		0x00000240

#define HAL_TCL1_RING_BASE_LSB(hal) \
	((hal)->regs->tcl1_ring_base_lsb)
#define HAL_TCL1_RING_BASE_MSB(hal) \
	((hal)->regs->tcl1_ring_base_msb)
#define HAL_TCL1_RING_ID(hal)		((hal)->regs->tcl1_ring_id)
#define HAL_TCL1_RING_MISC(hal) \
	((hal)->regs->tcl1_ring_misc)
#define HAL_TCL1_RING_TP_ADDR_LSB(hal) \
	((hal)->regs->tcl1_ring_tp_addr_lsb)
#define HAL_TCL1_RING_TP_ADDR_MSB(hal) \
	((hal)->regs->tcl1_ring_tp_addr_msb)
#define HAL_TCL1_RING_CONSUMER_INT_SETUP_IX0(hal) \
	((hal)->regs->tcl1_ring_consumer_int_setup_ix0)
#define HAL_TCL1_RING_CONSUMER_INT_SETUP_IX1(hal) \
	((hal)->regs->tcl1_ring_consumer_int_setup_ix1)
#define HAL_TCL1_RING_MSI1_BASE_LSB(hal) \
	((hal)->regs->tcl1_ring_msi1_base_lsb)
#define HAL_TCL1_RING_MSI1_BASE_MSB(hal) \
	((hal)->regs->tcl1_ring_msi1_base_msb)
#define HAL_TCL1_RING_MSI1_DATA(hal) \
	((hal)->regs->tcl1_ring_msi1_data)
#define HAL_TCL2_RING_BASE_LSB(hal) \
	((hal)->regs->tcl2_ring_base_lsb)
#define HAL_TCL_RING_BASE_LSB(hal) \
	((hal)->regs->tcl_ring_base_lsb)

#define HAL_TCL1_RING_MSI1_BASE_LSB_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_MSI1_BASE_LSB(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_MSI1_BASE_MSB_OFFSET(hal)	({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_MSI1_BASE_MSB(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_MSI1_DATA_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_MSI1_DATA(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_BASE_MSB_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_BASE_MSB(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_ID_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_ID(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_CONSR_INT_SETUP_IX0_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_CONSUMER_INT_SETUP_IX0(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_CONSR_INT_SETUP_IX1_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_CONSUMER_INT_SETUP_IX1(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_TP_ADDR_LSB_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_TP_ADDR_LSB(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_TP_ADDR_MSB_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_TP_ADDR_MSB(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })
#define HAL_TCL1_RING_MISC_OFFSET(hal) ({ typeof(hal) _hal = (hal); \
	(HAL_TCL1_RING_MISC(_hal) - HAL_TCL1_RING_BASE_LSB(_hal)); })

/* SW2TCL(x) R2 ring pointers (head/tail) address */
#define HAL_TCL1_RING_HP			0x00002000
#define HAL_TCL1_RING_TP			0x00002004
#define HAL_TCL2_RING_HP			0x00002008
#define HAL_TCL_RING_HP				0x00002028

#define HAL_TCL1_RING_TP_OFFSET \
		(HAL_TCL1_RING_TP - HAL_TCL1_RING_HP)

/* TCL STATUS ring address */
#define HAL_TCL_STATUS_RING_BASE_LSB(hal) \
	((hal)->regs->tcl_status_ring_base_lsb)
#define HAL_TCL_STATUS_RING_HP			0x00002048

/* PPE2TCL1 Ring address */
#define HAL_TCL_PPE2TCL1_RING_BASE_LSB		0x00000c48
#define HAL_TCL_PPE2TCL1_RING_HP		0x00002038

/* WBM PPE Release Ring address */
#define HAL_WBM_PPE_RELEASE_RING_BASE_LSB(hal) \
	((hal)->regs->ppe_rel_ring_base)
#define HAL_WBM_PPE_RELEASE_RING_HP		0x00003020

/* REO2SW(x) R0 ring configuration address */
#define HAL_REO1_GEN_ENABLE			0x00000000
#define HAL_REO1_MISC_CTRL_ADDR(hal) \
	((hal)->regs->reo1_misc_ctrl_addr)
#define HAL_REO1_DEST_RING_CTRL_IX_0		0x00000004
#define HAL_REO1_DEST_RING_CTRL_IX_1		0x00000008
#define HAL_REO1_DEST_RING_CTRL_IX_2		0x0000000c
#define HAL_REO1_DEST_RING_CTRL_IX_3		0x00000010
#define HAL_REO1_QDESC_ADDR(hal)		((hal)->regs->reo1_qdesc_addr)
#define HAL_REO1_QDESC_MAX_PEERID(hal)	((hal)->regs->reo1_qdesc_max_peerid)
#define HAL_REO1_SW_COOKIE_CFG0(hal)	((hal)->regs->reo1_sw_cookie_cfg0)
#define HAL_REO1_SW_COOKIE_CFG1(hal)	((hal)->regs->reo1_sw_cookie_cfg1)
#define HAL_REO1_QDESC_LUT_BASE0(hal)	((hal)->regs->reo1_qdesc_lut_base0)
#define HAL_REO1_QDESC_LUT_BASE1(hal)	((hal)->regs->reo1_qdesc_lut_base1)
#define HAL_REO1_RING_BASE_LSB(hal)	((hal)->regs->reo1_ring_base_lsb)
#define HAL_REO1_RING_BASE_MSB(hal)	((hal)->regs->reo1_ring_base_msb)
#define HAL_REO1_RING_ID(hal)		((hal)->regs->reo1_ring_id)
#define HAL_REO1_RING_MISC(hal)		((hal)->regs->reo1_ring_misc)
#define HAL_REO1_RING_HP_ADDR_LSB(hal)	((hal)->regs->reo1_ring_hp_addr_lsb)
#define HAL_REO1_RING_HP_ADDR_MSB(hal)	((hal)->regs->reo1_ring_hp_addr_msb)
#define HAL_REO1_RING_PRODUCER_INT_SETUP(hal) \
	((hal)->regs->reo1_ring_producer_int_setup)
#define HAL_REO1_RING_MSI1_BASE_LSB(hal)	\
	((hal)->regs->reo1_ring_msi1_base_lsb)
#define HAL_REO1_RING_MSI1_BASE_MSB(hal)	\
	((hal)->regs->reo1_ring_msi1_base_msb)
#define HAL_REO1_RING_MSI1_DATA(hal)	((hal)->regs->reo1_ring_msi1_data)
#define HAL_REO2_RING_BASE_LSB(hal)	((hal)->regs->reo2_ring_base)
#define HAL_REO1_AGING_THRESH_IX_0(hal)	((hal)->regs->reo1_aging_thres_ix0)
#define HAL_REO1_AGING_THRESH_IX_1(hal)	((hal)->regs->reo1_aging_thres_ix1)
#define HAL_REO1_AGING_THRESH_IX_2(hal)	((hal)->regs->reo1_aging_thres_ix2)
#define HAL_REO1_AGING_THRESH_IX_3(hal)	((hal)->regs->reo1_aging_thres_ix3)

/* REO2SW(x) R2 ring pointers (head/tail) address */
#define HAL_REO1_RING_HP			0x00003048
#define HAL_REO1_RING_TP			0x0000304c
#define HAL_REO2_RING_HP			0x00003050

#define HAL_REO1_RING_TP_OFFSET			(HAL_REO1_RING_TP - HAL_REO1_RING_HP)

/* REO2SW0 ring configuration address */
#define HAL_REO_SW0_RING_BASE_LSB(hal) \
	((hal)->regs->reo2_sw0_ring_base)

/* REO2SW0 R2 ring pointer (head/tail) address */
#define HAL_REO_SW0_RING_HP			0x00003088

/* REO CMD R0 address */
#define HAL_REO_CMD_RING_BASE_LSB(hal) \
	((hal)->regs->reo_cmd_ring_base)

/* REO CMD R2 address */
#define HAL_REO_CMD_HP				0x00003020

/* SW2REO R0 address */
#define	HAL_SW2REO_RING_BASE_LSB(hal) \
	((hal)->regs->sw2reo_ring_base)
#define HAL_SW2REO1_RING_BASE_LSB(hal) \
	((hal)->regs->sw2reo1_ring_base)

/* SW2REO R2 address */
#define HAL_SW2REO_RING_HP			0x00003028
#define HAL_SW2REO1_RING_HP			0x00003030

/* CE ring R0 address */
#define HAL_CE_SRC_RING_BASE_LSB                0x00000000
#define HAL_CE_DST_RING_BASE_LSB		0x00000000
#define HAL_CE_DST_STATUS_RING_BASE_LSB		0x00000058
#define HAL_CE_DST_RING_CTRL			0x000000b0

/* CE ring R2 address */
#define HAL_CE_DST_RING_HP			0x00000400
#define HAL_CE_DST_STATUS_RING_HP		0x00000408

/* REO status address */
#define HAL_REO_STATUS_RING_BASE_LSB(hal) \
	((hal)->regs->reo_status_ring_base)
#define HAL_REO_STATUS_HP			0x000030a8

/* WBM Idle R0 address */
#define HAL_WBM_IDLE_LINK_RING_BASE_LSB(hal) \
	((hal)->regs->wbm_idle_ring_base_lsb)
#define HAL_WBM_IDLE_LINK_RING_MISC_ADDR(hal) \
	((hal)->regs->wbm_idle_ring_misc_addr)
#define HAL_WBM_R0_IDLE_LIST_CONTROL_ADDR(hal) \
	((hal)->regs->wbm_r0_idle_list_cntl_addr)
#define HAL_WBM_R0_IDLE_LIST_SIZE_ADDR(hal) \
	((hal)->regs->wbm_r0_idle_list_size_addr)
#define HAL_WBM_SCATTERED_RING_BASE_LSB(hal) \
	((hal)->regs->wbm_scattered_ring_base_lsb)
#define HAL_WBM_SCATTERED_RING_BASE_MSB(hal) \
	((hal)->regs->wbm_scattered_ring_base_msb)
#define HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX0(hal) \
	((hal)->regs->wbm_scattered_desc_head_info_ix0)
#define HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX1(hal) \
	((hal)->regs->wbm_scattered_desc_head_info_ix1)
#define HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX0(hal) \
	((hal)->regs->wbm_scattered_desc_tail_info_ix0)
#define HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX1(hal) \
	((hal)->regs->wbm_scattered_desc_tail_info_ix1)
#define HAL_WBM_SCATTERED_DESC_PTR_HP_ADDR(hal) \
	((hal)->regs->wbm_scattered_desc_ptr_hp_addr)

/* WBM Idle R2 address */
#define HAL_WBM_IDLE_LINK_RING_HP		0x000030b8

/* SW2WBM R0 release address */
#define HAL_WBM_SW_RELEASE_RING_BASE_LSB(hal) \
	((hal)->regs->wbm_sw_release_ring_base_lsb)
#define HAL_WBM_SW1_RELEASE_RING_BASE_LSB(hal) \
	((hal)->regs->wbm_sw1_release_ring_base_lsb)

/* SW2WBM R2 release address */
#define HAL_WBM_SW_RELEASE_RING_HP		0x00003010
#define HAL_WBM_SW1_RELEASE_RING_HP		0x00003018

/* WBM2SW R0 release address */
#define HAL_WBM0_RELEASE_RING_BASE_LSB(hal) \
	((hal)->regs->wbm0_release_ring_base_lsb)

#define HAL_WBM1_RELEASE_RING_BASE_LSB(hal) \
	((hal)->regs->wbm1_release_ring_base_lsb)

/* WBM2SW R2 release address */
#define HAL_WBM0_RELEASE_RING_HP		0x000030c8
#define HAL_WBM1_RELEASE_RING_HP		0x000030d0

/* WBM cookie config address and mask */
#define HAL_WBM_SW_COOKIE_CFG0			0x00000040
#define HAL_WBM_SW_COOKIE_CFG1			0x00000044
#define HAL_WBM_SW_COOKIE_CFG2			0x00000090
#define HAL_WBM_SW_COOKIE_CONVERT_CFG		0x00000094

#define HAL_WBM_SW_COOKIE_CFG_CMEM_BASE_ADDR_MSB	GENMASK(7, 0)
#define HAL_WBM_SW_COOKIE_CFG_COOKIE_PPT_MSB		GENMASK(12, 8)
#define HAL_WBM_SW_COOKIE_CFG_COOKIE_SPT_MSB		GENMASK(17, 13)
#define HAL_WBM_SW_COOKIE_CFG_ALIGN			BIT(18)
#define HAL_WBM_SW_COOKIE_CFG_RELEASE_PATH_EN		BIT(0)
#define HAL_WBM_SW_COOKIE_CFG_ERR_PATH_EN		BIT(1)
#define HAL_WBM_SW_COOKIE_CFG_CONV_IND_EN		BIT(3)

#define HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW0_EN		BIT(1)
#define HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW1_EN		BIT(2)
#define HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW2_EN		BIT(3)
#define HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW3_EN		BIT(4)
#define HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW4_EN		BIT(5)
#define HAL_WBM_SW_COOKIE_CONV_CFG_GLOBAL_EN		BIT(8)

/* TCL ring field mask and offset */
#define HAL_TCL1_RING_BASE_MSB_RING_SIZE		GENMASK(27, 8)
#define HAL_TCL1_RING_BASE_MSB_RING_BASE_ADDR_MSB	GENMASK(7, 0)
#define HAL_TCL1_RING_ID_ENTRY_SIZE			GENMASK(7, 0)
#define HAL_TCL1_RING_MISC_MSI_RING_ID_DISABLE		BIT(0)
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
#define HAL_TCL1_RING_CMN_CTRL_DSCP_TID_MAP_PROG_EN	BIT(23)
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
#define HAL_REO1_MISC_CTL_FRAG_DST_RING			GENMASK(20, 17)
#define HAL_REO1_MISC_CTL_BAR_DST_RING			GENMASK(24, 21)
#define HAL_REO1_GEN_ENABLE_AGING_LIST_ENABLE		BIT(2)
#define HAL_REO1_GEN_ENABLE_AGING_FLUSH_ENABLE		BIT(3)
#define HAL_REO1_SW_COOKIE_CFG_CMEM_BASE_ADDR_MSB	GENMASK(7, 0)
#define HAL_REO1_SW_COOKIE_CFG_COOKIE_PPT_MSB		GENMASK(12, 8)
#define HAL_REO1_SW_COOKIE_CFG_COOKIE_SPT_MSB		GENMASK(17, 13)
#define HAL_REO1_SW_COOKIE_CFG_ALIGN			BIT(18)
#define HAL_REO1_SW_COOKIE_CFG_ENABLE			BIT(19)
#define HAL_REO1_SW_COOKIE_CFG_GLOBAL_ENABLE		BIT(20)
#define HAL_REO_QDESC_ADDR_READ_LUT_ENABLE		BIT(7)
#define HAL_REO_QDESC_ADDR_READ_CLEAR_QDESC_ARRAY	BIT(6)

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

#define HAL_WBM_IDLE_LINK_RING_MISC_SRNG_ENABLE		BIT(6)
#define HAL_WBM_IDLE_LINK_RING_MISC_RIND_ID_DISABLE	BIT(0)

#define BASE_ADDR_MATCH_TAG_VAL 0x5

#define HAL_REO_REO2SW1_RING_BASE_MSB_RING_SIZE		0x000fffff
#define HAL_REO_REO2SW0_RING_BASE_MSB_RING_SIZE		0x000fffff
#define HAL_REO_SW2REO_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_REO_CMD_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_REO_STATUS_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_SW2TCL1_RING_BASE_MSB_RING_SIZE		0x000fffff
#define HAL_SW2TCL1_CMD_RING_BASE_MSB_RING_SIZE		0x000fffff
#define HAL_TCL_STATUS_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_CE_SRC_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_CE_DST_RING_BASE_MSB_RING_SIZE		0x0000ffff
#define HAL_CE_DST_STATUS_RING_BASE_MSB_RING_SIZE	0x0000ffff
#define HAL_WBM_IDLE_LINK_RING_BASE_MSB_RING_SIZE	0x000fffff
#define HAL_SW2WBM_RELEASE_RING_BASE_MSB_RING_SIZE	0x0000ffff
#define HAL_WBM2SW_RELEASE_RING_BASE_MSB_RING_SIZE	0x000fffff
#define HAL_RXDMA_RING_MAX_SIZE				0x0000ffff
#define HAL_RXDMA_RING_MAX_SIZE_BE			0x000fffff
#define HAL_WBM2PPE_RELEASE_RING_BASE_MSB_RING_SIZE	0x000fffff

#define HAL_WBM2SW_REL_ERR_RING_NUM 3
/* Add any other errors here and return them in
 * ath12k_hal_rx_desc_get_err().
 */

#define HAL_IPQ5332_CE_WFSS_REG_BASE	0x740000
#define HAL_IPQ5332_CE_SIZE		0x100000

#define HAL_RX_MAX_BA_WINDOW	256

#define HAL_DEFAULT_BE_BK_VI_REO_TIMEOUT_USEC	(100 * 1000)
#define HAL_DEFAULT_VO_REO_TIMEOUT_USEC		(40 * 1000)

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
#define HAL_REO_CMD_FLG_FLUSH_QUEUE_1K_DESC	BIT(9)

/* Should be matching with HAL_REO_UPD_RX_QUEUE_INFO0_UPD_* fields */
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

/* Should be matching with HAL_REO_UPD_RX_QUEUE_INFO1_* fields */
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

/* Should be matching with HAL_REO_UPD_RX_QUEUE_INFO2_* fields */
#define HAL_REO_CMD_UPD2_SVLD			BIT(10)
#define HAL_REO_CMD_UPD2_SSN			GENMASK(22, 11)
#define HAL_REO_CMD_UPD2_SEQ_2K_ERR		BIT(23)
#define HAL_REO_CMD_UPD2_PN_ERR			BIT(24)

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

int ath12k_wifi7_hal_init(struct ath12k_base *ab);
void ath12k_wifi7_hal_ce_dst_setup(struct ath12k_base *ab,
				   struct hal_srng *srng, int ring_num);
void ath12k_wifi7_hal_srng_dst_hw_init(struct ath12k_base *ab,
				       struct hal_srng *srng);
void ath12k_wifi7_hal_srng_src_hw_init(struct ath12k_base *ab,
				       struct hal_srng *srng);
void ath12k_wifi7_hal_set_umac_srng_ptr_addr(struct ath12k_base *ab,
					     struct hal_srng *srng);
int ath12k_wifi7_hal_srng_update_shadow_config(struct ath12k_base *ab,
					       enum hal_ring_type ring_type,
					       int ring_num);
int ath12k_wifi7_hal_srng_get_ring_id(struct ath12k_hal *hal,
				      enum hal_ring_type type,
				      int ring_num, int mac_id);
u32 ath12k_wifi7_hal_ce_get_desc_size(enum hal_ce_desc type);
void ath12k_wifi7_hal_cc_config(struct ath12k_base *ab);
enum hal_rx_buf_return_buf_manager
ath12k_wifi7_hal_get_idle_link_rbm(struct ath12k_hal *hal, u8 device_id);
void ath12k_wifi7_hal_ce_src_set_desc(struct hal_ce_srng_src_desc *desc,
				      dma_addr_t paddr,
				      u32 len, u32 id, u8 byte_swap_data);
void ath12k_wifi7_hal_ce_dst_set_desc(struct hal_ce_srng_dest_desc *desc,
				      dma_addr_t paddr);
void
ath12k_wifi7_hal_set_link_desc_addr(struct hal_wbm_link_desc *desc,
				    u32 cookie, dma_addr_t paddr,
				    enum hal_rx_buf_return_buf_manager rbm);
u32
ath12k_wifi7_hal_ce_dst_status_get_length(struct hal_ce_srng_dst_status_desc *desc);
void
ath12k_wifi7_hal_setup_link_idle_list(struct ath12k_base *ab,
				      struct hal_wbm_idle_scatter_list *sbuf,
				      u32 nsbufs, u32 tot_link_desc,
				      u32 end_offset);
void ath12k_wifi7_hal_reoq_lut_addr_read_enable(struct ath12k_base *ab);
void ath12k_wifi7_hal_reoq_lut_set_max_peerid(struct ath12k_base *ab);
void ath12k_wifi7_hal_write_reoq_lut_addr(struct ath12k_base *ab,
					  dma_addr_t paddr);
void ath12k_wifi7_hal_write_ml_reoq_lut_addr(struct ath12k_base *ab,
					     dma_addr_t paddr);
u32 ath12k_wifi7_hal_reo_qdesc_size(u32 ba_window_size, u8 tid);
#endif
