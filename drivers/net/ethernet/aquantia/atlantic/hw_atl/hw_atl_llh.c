// SPDX-License-Identifier: GPL-2.0-only
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 */

/* File hw_atl_llh.c: Definitions of bitfield and register access functions for
 * Atlantic registers.
 */

#include "hw_atl_llh.h"
#include "hw_atl_llh_internal.h"
#include "../aq_hw_utils.h"

/* global */
void hw_atl_reg_glb_cpu_sem_set(struct aq_hw_s *aq_hw, u32 glb_cpu_sem,
				u32 semaphore)
{
	aq_hw_write_reg(aq_hw, HW_ATL_GLB_CPU_SEM_ADR(semaphore), glb_cpu_sem);
}

u32 hw_atl_reg_glb_cpu_sem_get(struct aq_hw_s *aq_hw, u32 semaphore)
{
	return aq_hw_read_reg(aq_hw, HW_ATL_GLB_CPU_SEM_ADR(semaphore));
}

void hw_atl_glb_glb_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 glb_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_GLB_REG_RES_DIS_ADR,
			    HW_ATL_GLB_REG_RES_DIS_MSK,
			    HW_ATL_GLB_REG_RES_DIS_SHIFT,
			    glb_reg_res_dis);
}

void hw_atl_glb_soft_res_set(struct aq_hw_s *aq_hw, u32 soft_res)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_GLB_SOFT_RES_ADR,
			    HW_ATL_GLB_SOFT_RES_MSK,
			    HW_ATL_GLB_SOFT_RES_SHIFT, soft_res);
}

u32 hw_atl_glb_soft_res_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL_GLB_SOFT_RES_ADR,
				  HW_ATL_GLB_SOFT_RES_MSK,
				  HW_ATL_GLB_SOFT_RES_SHIFT);
}

u32 hw_atl_reg_glb_mif_id_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, HW_ATL_GLB_MIF_ID_ADR);
}

/* stats */
u32 hw_atl_rpb_rx_dma_drop_pkt_cnt_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, HW_ATL_RPB_RX_DMA_DROP_PKT_CNT_ADR);
}

u64 hw_atl_stats_rx_dma_good_octet_counter_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg64(aq_hw, HW_ATL_STATS_RX_DMA_GOOD_OCTET_COUNTERLSW);
}

u64 hw_atl_stats_rx_dma_good_pkt_counter_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg64(aq_hw, HW_ATL_STATS_RX_DMA_GOOD_PKT_COUNTERLSW);
}

u64 hw_atl_stats_tx_dma_good_octet_counter_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg64(aq_hw, HW_ATL_STATS_TX_DMA_GOOD_OCTET_COUNTERLSW);
}

u64 hw_atl_stats_tx_dma_good_pkt_counter_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg64(aq_hw, HW_ATL_STATS_TX_DMA_GOOD_PKT_COUNTERLSW);
}

/* interrupt */
void hw_atl_itr_irq_auto_masklsw_set(struct aq_hw_s *aq_hw,
				     u32 irq_auto_masklsw)
{
	aq_hw_write_reg(aq_hw, HW_ATL_ITR_IAMRLSW_ADR, irq_auto_masklsw);
}

void hw_atl_itr_irq_map_en_rx_set(struct aq_hw_s *aq_hw, u32 irq_map_en_rx,
				  u32 rx)
{
/* register address for bitfield imr_rx{r}_en */
	static u32 itr_imr_rxren_adr[32] = {
			0x00002100U, 0x00002100U, 0x00002104U, 0x00002104U,
			0x00002108U, 0x00002108U, 0x0000210CU, 0x0000210CU,
			0x00002110U, 0x00002110U, 0x00002114U, 0x00002114U,
			0x00002118U, 0x00002118U, 0x0000211CU, 0x0000211CU,
			0x00002120U, 0x00002120U, 0x00002124U, 0x00002124U,
			0x00002128U, 0x00002128U, 0x0000212CU, 0x0000212CU,
			0x00002130U, 0x00002130U, 0x00002134U, 0x00002134U,
			0x00002138U, 0x00002138U, 0x0000213CU, 0x0000213CU
		};

/* bitmask for bitfield imr_rx{r}_en */
	static u32 itr_imr_rxren_msk[32] = {
			0x00008000U, 0x00000080U, 0x00008000U, 0x00000080U,
			0x00008000U, 0x00000080U, 0x00008000U, 0x00000080U,
			0x00008000U, 0x00000080U, 0x00008000U, 0x00000080U,
			0x00008000U, 0x00000080U, 0x00008000U, 0x00000080U,
			0x00008000U, 0x00000080U, 0x00008000U, 0x00000080U,
			0x00008000U, 0x00000080U, 0x00008000U, 0x00000080U,
			0x00008000U, 0x00000080U, 0x00008000U, 0x00000080U,
			0x00008000U, 0x00000080U, 0x00008000U, 0x00000080U
		};

/* lower bit position of bitfield imr_rx{r}_en */
	static u32 itr_imr_rxren_shift[32] = {
			15U, 7U, 15U, 7U, 15U, 7U, 15U, 7U,
			15U, 7U, 15U, 7U, 15U, 7U, 15U, 7U,
			15U, 7U, 15U, 7U, 15U, 7U, 15U, 7U,
			15U, 7U, 15U, 7U, 15U, 7U, 15U, 7U
		};

	aq_hw_write_reg_bit(aq_hw, itr_imr_rxren_adr[rx],
			    itr_imr_rxren_msk[rx],
			    itr_imr_rxren_shift[rx],
			    irq_map_en_rx);
}

void hw_atl_itr_irq_map_en_tx_set(struct aq_hw_s *aq_hw, u32 irq_map_en_tx,
				  u32 tx)
{
/* register address for bitfield imr_tx{t}_en */
	static u32 itr_imr_txten_adr[32] = {
			0x00002100U, 0x00002100U, 0x00002104U, 0x00002104U,
			0x00002108U, 0x00002108U, 0x0000210CU, 0x0000210CU,
			0x00002110U, 0x00002110U, 0x00002114U, 0x00002114U,
			0x00002118U, 0x00002118U, 0x0000211CU, 0x0000211CU,
			0x00002120U, 0x00002120U, 0x00002124U, 0x00002124U,
			0x00002128U, 0x00002128U, 0x0000212CU, 0x0000212CU,
			0x00002130U, 0x00002130U, 0x00002134U, 0x00002134U,
			0x00002138U, 0x00002138U, 0x0000213CU, 0x0000213CU
		};

/* bitmask for bitfield imr_tx{t}_en */
	static u32 itr_imr_txten_msk[32] = {
			0x80000000U, 0x00800000U, 0x80000000U, 0x00800000U,
			0x80000000U, 0x00800000U, 0x80000000U, 0x00800000U,
			0x80000000U, 0x00800000U, 0x80000000U, 0x00800000U,
			0x80000000U, 0x00800000U, 0x80000000U, 0x00800000U,
			0x80000000U, 0x00800000U, 0x80000000U, 0x00800000U,
			0x80000000U, 0x00800000U, 0x80000000U, 0x00800000U,
			0x80000000U, 0x00800000U, 0x80000000U, 0x00800000U,
			0x80000000U, 0x00800000U, 0x80000000U, 0x00800000U
		};

/* lower bit position of bitfield imr_tx{t}_en */
	static u32 itr_imr_txten_shift[32] = {
			31U, 23U, 31U, 23U, 31U, 23U, 31U, 23U,
			31U, 23U, 31U, 23U, 31U, 23U, 31U, 23U,
			31U, 23U, 31U, 23U, 31U, 23U, 31U, 23U,
			31U, 23U, 31U, 23U, 31U, 23U, 31U, 23U
		};

	aq_hw_write_reg_bit(aq_hw, itr_imr_txten_adr[tx],
			    itr_imr_txten_msk[tx],
			    itr_imr_txten_shift[tx],
			    irq_map_en_tx);
}

void hw_atl_itr_irq_map_rx_set(struct aq_hw_s *aq_hw, u32 irq_map_rx, u32 rx)
{
/* register address for bitfield imr_rx{r}[4:0] */
	static u32 itr_imr_rxr_adr[32] = {
			0x00002100U, 0x00002100U, 0x00002104U, 0x00002104U,
			0x00002108U, 0x00002108U, 0x0000210CU, 0x0000210CU,
			0x00002110U, 0x00002110U, 0x00002114U, 0x00002114U,
			0x00002118U, 0x00002118U, 0x0000211CU, 0x0000211CU,
			0x00002120U, 0x00002120U, 0x00002124U, 0x00002124U,
			0x00002128U, 0x00002128U, 0x0000212CU, 0x0000212CU,
			0x00002130U, 0x00002130U, 0x00002134U, 0x00002134U,
			0x00002138U, 0x00002138U, 0x0000213CU, 0x0000213CU
		};

/* bitmask for bitfield imr_rx{r}[4:0] */
	static u32 itr_imr_rxr_msk[32] = {
			0x00001f00U, 0x0000001FU, 0x00001F00U, 0x0000001FU,
			0x00001f00U, 0x0000001FU, 0x00001F00U, 0x0000001FU,
			0x00001f00U, 0x0000001FU, 0x00001F00U, 0x0000001FU,
			0x00001f00U, 0x0000001FU, 0x00001F00U, 0x0000001FU,
			0x00001f00U, 0x0000001FU, 0x00001F00U, 0x0000001FU,
			0x00001f00U, 0x0000001FU, 0x00001F00U, 0x0000001FU,
			0x00001f00U, 0x0000001FU, 0x00001F00U, 0x0000001FU,
			0x00001f00U, 0x0000001FU, 0x00001F00U, 0x0000001FU
		};

/* lower bit position of bitfield imr_rx{r}[4:0] */
	static u32 itr_imr_rxr_shift[32] = {
			8U, 0U, 8U, 0U, 8U, 0U, 8U, 0U,
			8U, 0U, 8U, 0U, 8U, 0U, 8U, 0U,
			8U, 0U, 8U, 0U, 8U, 0U, 8U, 0U,
			8U, 0U, 8U, 0U, 8U, 0U, 8U, 0U
		};

	aq_hw_write_reg_bit(aq_hw, itr_imr_rxr_adr[rx],
			    itr_imr_rxr_msk[rx],
			    itr_imr_rxr_shift[rx],
			    irq_map_rx);
}

void hw_atl_itr_irq_map_tx_set(struct aq_hw_s *aq_hw, u32 irq_map_tx, u32 tx)
{
/* register address for bitfield imr_tx{t}[4:0] */
	static u32 itr_imr_txt_adr[32] = {
			0x00002100U, 0x00002100U, 0x00002104U, 0x00002104U,
			0x00002108U, 0x00002108U, 0x0000210CU, 0x0000210CU,
			0x00002110U, 0x00002110U, 0x00002114U, 0x00002114U,
			0x00002118U, 0x00002118U, 0x0000211CU, 0x0000211CU,
			0x00002120U, 0x00002120U, 0x00002124U, 0x00002124U,
			0x00002128U, 0x00002128U, 0x0000212CU, 0x0000212CU,
			0x00002130U, 0x00002130U, 0x00002134U, 0x00002134U,
			0x00002138U, 0x00002138U, 0x0000213CU, 0x0000213CU
		};

/* bitmask for bitfield imr_tx{t}[4:0] */
	static u32 itr_imr_txt_msk[32] = {
			0x1f000000U, 0x001F0000U, 0x1F000000U, 0x001F0000U,
			0x1f000000U, 0x001F0000U, 0x1F000000U, 0x001F0000U,
			0x1f000000U, 0x001F0000U, 0x1F000000U, 0x001F0000U,
			0x1f000000U, 0x001F0000U, 0x1F000000U, 0x001F0000U,
			0x1f000000U, 0x001F0000U, 0x1F000000U, 0x001F0000U,
			0x1f000000U, 0x001F0000U, 0x1F000000U, 0x001F0000U,
			0x1f000000U, 0x001F0000U, 0x1F000000U, 0x001F0000U,
			0x1f000000U, 0x001F0000U, 0x1F000000U, 0x001F0000U
		};

/* lower bit position of bitfield imr_tx{t}[4:0] */
	static u32 itr_imr_txt_shift[32] = {
			24U, 16U, 24U, 16U, 24U, 16U, 24U, 16U,
			24U, 16U, 24U, 16U, 24U, 16U, 24U, 16U,
			24U, 16U, 24U, 16U, 24U, 16U, 24U, 16U,
			24U, 16U, 24U, 16U, 24U, 16U, 24U, 16U
		};

	aq_hw_write_reg_bit(aq_hw, itr_imr_txt_adr[tx],
			    itr_imr_txt_msk[tx],
			    itr_imr_txt_shift[tx],
			    irq_map_tx);
}

void hw_atl_itr_irq_msk_clearlsw_set(struct aq_hw_s *aq_hw,
				     u32 irq_msk_clearlsw)
{
	aq_hw_write_reg(aq_hw, HW_ATL_ITR_IMCRLSW_ADR, irq_msk_clearlsw);
}

void hw_atl_itr_irq_msk_setlsw_set(struct aq_hw_s *aq_hw, u32 irq_msk_setlsw)
{
	aq_hw_write_reg(aq_hw, HW_ATL_ITR_IMSRLSW_ADR, irq_msk_setlsw);
}

void hw_atl_itr_irq_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 irq_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_ITR_REG_RES_DSBL_ADR,
			    HW_ATL_ITR_REG_RES_DSBL_MSK,
			    HW_ATL_ITR_REG_RES_DSBL_SHIFT, irq_reg_res_dis);
}

void hw_atl_itr_irq_status_clearlsw_set(struct aq_hw_s *aq_hw,
					u32 irq_status_clearlsw)
{
	aq_hw_write_reg(aq_hw, HW_ATL_ITR_ISCRLSW_ADR, irq_status_clearlsw);
}

u32 hw_atl_itr_irq_statuslsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, HW_ATL_ITR_ISRLSW_ADR);
}

u32 hw_atl_itr_res_irq_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL_ITR_RES_ADR, HW_ATL_ITR_RES_MSK,
				  HW_ATL_ITR_RES_SHIFT);
}

void hw_atl_itr_res_irq_set(struct aq_hw_s *aq_hw, u32 res_irq)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_ITR_RES_ADR, HW_ATL_ITR_RES_MSK,
			    HW_ATL_ITR_RES_SHIFT, res_irq);
}

/* set RSC interrupt */
void hw_atl_itr_rsc_en_set(struct aq_hw_s *aq_hw, u32 enable)
{
	aq_hw_write_reg(aq_hw, HW_ATL_ITR_RSC_EN_ADR, enable);
}

/* set RSC delay */
void hw_atl_itr_rsc_delay_set(struct aq_hw_s *aq_hw, u32 delay)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_ITR_RSC_DELAY_ADR,
			    HW_ATL_ITR_RSC_DELAY_MSK,
			    HW_ATL_ITR_RSC_DELAY_SHIFT,
			    delay);
}

/* rdm */
void hw_atl_rdm_cpu_id_set(struct aq_hw_s *aq_hw, u32 cpuid, u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DCADCPUID_ADR(dca),
			    HW_ATL_RDM_DCADCPUID_MSK,
			    HW_ATL_RDM_DCADCPUID_SHIFT, cpuid);
}

void hw_atl_rdm_rx_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_dca_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DCA_EN_ADR, HW_ATL_RDM_DCA_EN_MSK,
			    HW_ATL_RDM_DCA_EN_SHIFT, rx_dca_en);
}

void hw_atl_rdm_rx_dca_mode_set(struct aq_hw_s *aq_hw, u32 rx_dca_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DCA_MODE_ADR,
			    HW_ATL_RDM_DCA_MODE_MSK,
			    HW_ATL_RDM_DCA_MODE_SHIFT, rx_dca_mode);
}

void hw_atl_rdm_rx_desc_data_buff_size_set(struct aq_hw_s *aq_hw,
					   u32 rx_desc_data_buff_size,
					   u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DESCDDATA_SIZE_ADR(descriptor),
			    HW_ATL_RDM_DESCDDATA_SIZE_MSK,
			    HW_ATL_RDM_DESCDDATA_SIZE_SHIFT,
			    rx_desc_data_buff_size);
}

void hw_atl_rdm_rx_desc_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_desc_dca_en,
				   u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DCADDESC_EN_ADR(dca),
			    HW_ATL_RDM_DCADDESC_EN_MSK,
			    HW_ATL_RDM_DCADDESC_EN_SHIFT,
			    rx_desc_dca_en);
}

void hw_atl_rdm_rx_desc_en_set(struct aq_hw_s *aq_hw, u32 rx_desc_en,
			       u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DESCDEN_ADR(descriptor),
			    HW_ATL_RDM_DESCDEN_MSK,
			    HW_ATL_RDM_DESCDEN_SHIFT,
			    rx_desc_en);
}

void hw_atl_rdm_rx_desc_head_buff_size_set(struct aq_hw_s *aq_hw,
					   u32 rx_desc_head_buff_size,
					   u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DESCDHDR_SIZE_ADR(descriptor),
			    HW_ATL_RDM_DESCDHDR_SIZE_MSK,
			    HW_ATL_RDM_DESCDHDR_SIZE_SHIFT,
			    rx_desc_head_buff_size);
}

void hw_atl_rdm_rx_desc_head_splitting_set(struct aq_hw_s *aq_hw,
					   u32 rx_desc_head_splitting,
					   u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DESCDHDR_SPLIT_ADR(descriptor),
			    HW_ATL_RDM_DESCDHDR_SPLIT_MSK,
			    HW_ATL_RDM_DESCDHDR_SPLIT_SHIFT,
			    rx_desc_head_splitting);
}

u32 hw_atl_rdm_rx_desc_head_ptr_get(struct aq_hw_s *aq_hw, u32 descriptor)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL_RDM_DESCDHD_ADR(descriptor),
				  HW_ATL_RDM_DESCDHD_MSK,
				  HW_ATL_RDM_DESCDHD_SHIFT);
}

void hw_atl_rdm_rx_desc_len_set(struct aq_hw_s *aq_hw, u32 rx_desc_len,
				u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DESCDLEN_ADR(descriptor),
			    HW_ATL_RDM_DESCDLEN_MSK, HW_ATL_RDM_DESCDLEN_SHIFT,
			    rx_desc_len);
}

void hw_atl_rdm_rx_desc_res_set(struct aq_hw_s *aq_hw, u32 rx_desc_res,
				u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DESCDRESET_ADR(descriptor),
			    HW_ATL_RDM_DESCDRESET_MSK,
			    HW_ATL_RDM_DESCDRESET_SHIFT,
			    rx_desc_res);
}

void hw_atl_rdm_rx_desc_wr_wb_irq_en_set(struct aq_hw_s *aq_hw,
					 u32 rx_desc_wr_wb_irq_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_INT_DESC_WRB_EN_ADR,
			    HW_ATL_RDM_INT_DESC_WRB_EN_MSK,
			    HW_ATL_RDM_INT_DESC_WRB_EN_SHIFT,
			    rx_desc_wr_wb_irq_en);
}

void hw_atl_rdm_rx_head_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_head_dca_en,
				   u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DCADHDR_EN_ADR(dca),
			    HW_ATL_RDM_DCADHDR_EN_MSK,
			    HW_ATL_RDM_DCADHDR_EN_SHIFT,
			    rx_head_dca_en);
}

void hw_atl_rdm_rx_pld_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_pld_dca_en,
				  u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_DCADPAY_EN_ADR(dca),
			    HW_ATL_RDM_DCADPAY_EN_MSK,
			    HW_ATL_RDM_DCADPAY_EN_SHIFT,
			    rx_pld_dca_en);
}

void hw_atl_rdm_rdm_intr_moder_en_set(struct aq_hw_s *aq_hw,
				      u32 rdm_intr_moder_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_INT_RIM_EN_ADR,
			    HW_ATL_RDM_INT_RIM_EN_MSK,
			    HW_ATL_RDM_INT_RIM_EN_SHIFT,
			    rdm_intr_moder_en);
}

/* reg */
void hw_atl_reg_gen_irq_map_set(struct aq_hw_s *aq_hw, u32 gen_intr_map,
				u32 regidx)
{
	aq_hw_write_reg(aq_hw, HW_ATL_GEN_INTR_MAP_ADR(regidx), gen_intr_map);
}

u32 hw_atl_reg_gen_irq_status_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, HW_ATL_GEN_INTR_STAT_ADR);
}

void hw_atl_reg_irq_glb_ctl_set(struct aq_hw_s *aq_hw, u32 intr_glb_ctl)
{
	aq_hw_write_reg(aq_hw, HW_ATL_INTR_GLB_CTL_ADR, intr_glb_ctl);
}

void hw_atl_reg_irq_thr_set(struct aq_hw_s *aq_hw, u32 intr_thr, u32 throttle)
{
	aq_hw_write_reg(aq_hw, HW_ATL_INTR_THR_ADR(throttle), intr_thr);
}

void hw_atl_reg_rx_dma_desc_base_addresslswset(struct aq_hw_s *aq_hw,
					       u32 rx_dma_desc_base_addrlsw,
					       u32 descriptor)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RX_DMA_DESC_BASE_ADDRLSW_ADR(descriptor),
			rx_dma_desc_base_addrlsw);
}

void hw_atl_reg_rx_dma_desc_base_addressmswset(struct aq_hw_s *aq_hw,
					       u32 rx_dma_desc_base_addrmsw,
					       u32 descriptor)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RX_DMA_DESC_BASE_ADDRMSW_ADR(descriptor),
			rx_dma_desc_base_addrmsw);
}

u32 hw_atl_reg_rx_dma_desc_status_get(struct aq_hw_s *aq_hw, u32 descriptor)
{
	return aq_hw_read_reg(aq_hw, HW_ATL_RX_DMA_DESC_STAT_ADR(descriptor));
}

void hw_atl_reg_rx_dma_desc_tail_ptr_set(struct aq_hw_s *aq_hw,
					 u32 rx_dma_desc_tail_ptr,
					 u32 descriptor)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RX_DMA_DESC_TAIL_PTR_ADR(descriptor),
			rx_dma_desc_tail_ptr);
}

void hw_atl_reg_rx_flr_mcst_flr_msk_set(struct aq_hw_s *aq_hw,
					u32 rx_flr_mcst_flr_msk)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RX_FLR_MCST_FLR_MSK_ADR,
			rx_flr_mcst_flr_msk);
}

void hw_atl_reg_rx_flr_mcst_flr_set(struct aq_hw_s *aq_hw, u32 rx_flr_mcst_flr,
				    u32 filter)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RX_FLR_MCST_FLR_ADR(filter),
			rx_flr_mcst_flr);
}

void hw_atl_reg_rx_flr_rss_control1set(struct aq_hw_s *aq_hw,
				       u32 rx_flr_rss_control1)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RX_FLR_RSS_CONTROL1_ADR,
			rx_flr_rss_control1);
}

void hw_atl_reg_rx_flr_control2_set(struct aq_hw_s *aq_hw,
				    u32 rx_filter_control2)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RX_FLR_CONTROL2_ADR, rx_filter_control2);
}

void hw_atl_reg_rx_intr_moder_ctrl_set(struct aq_hw_s *aq_hw,
				       u32 rx_intr_moderation_ctl,
				       u32 queue)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RX_INTR_MODERATION_CTL_ADR(queue),
			rx_intr_moderation_ctl);
}

void hw_atl_reg_tx_dma_debug_ctl_set(struct aq_hw_s *aq_hw,
				     u32 tx_dma_debug_ctl)
{
	aq_hw_write_reg(aq_hw, HW_ATL_TX_DMA_DEBUG_CTL_ADR, tx_dma_debug_ctl);
}

void hw_atl_reg_tx_dma_desc_base_addresslswset(struct aq_hw_s *aq_hw,
					       u32 tx_dma_desc_base_addrlsw,
					       u32 descriptor)
{
	aq_hw_write_reg(aq_hw, HW_ATL_TX_DMA_DESC_BASE_ADDRLSW_ADR(descriptor),
			tx_dma_desc_base_addrlsw);
}

void hw_atl_reg_tx_dma_desc_base_addressmswset(struct aq_hw_s *aq_hw,
					       u32 tx_dma_desc_base_addrmsw,
					       u32 descriptor)
{
	aq_hw_write_reg(aq_hw, HW_ATL_TX_DMA_DESC_BASE_ADDRMSW_ADR(descriptor),
			tx_dma_desc_base_addrmsw);
}

void hw_atl_reg_tx_dma_desc_tail_ptr_set(struct aq_hw_s *aq_hw,
					 u32 tx_dma_desc_tail_ptr,
					 u32 descriptor)
{
	aq_hw_write_reg(aq_hw, HW_ATL_TX_DMA_DESC_TAIL_PTR_ADR(descriptor),
			tx_dma_desc_tail_ptr);
}

void hw_atl_reg_tx_intr_moder_ctrl_set(struct aq_hw_s *aq_hw,
				       u32 tx_intr_moderation_ctl,
				       u32 queue)
{
	aq_hw_write_reg(aq_hw, HW_ATL_TX_INTR_MODERATION_CTL_ADR(queue),
			tx_intr_moderation_ctl);
}

/* RPB: rx packet buffer */
void hw_atl_rpb_dma_sys_lbk_set(struct aq_hw_s *aq_hw, u32 dma_sys_lbk)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPB_DMA_SYS_LBK_ADR,
			    HW_ATL_RPB_DMA_SYS_LBK_MSK,
			    HW_ATL_RPB_DMA_SYS_LBK_SHIFT, dma_sys_lbk);
}

void hw_atl_rpb_rpf_rx_traf_class_mode_set(struct aq_hw_s *aq_hw,
					   u32 rx_traf_class_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPB_RPF_RX_TC_MODE_ADR,
			    HW_ATL_RPB_RPF_RX_TC_MODE_MSK,
			    HW_ATL_RPB_RPF_RX_TC_MODE_SHIFT,
			    rx_traf_class_mode);
}

void hw_atl_rpb_rx_buff_en_set(struct aq_hw_s *aq_hw, u32 rx_buff_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPB_RX_BUF_EN_ADR,
			    HW_ATL_RPB_RX_BUF_EN_MSK,
			    HW_ATL_RPB_RX_BUF_EN_SHIFT, rx_buff_en);
}

void hw_atl_rpb_rx_buff_hi_threshold_per_tc_set(struct aq_hw_s *aq_hw,
						u32 rx_buff_hi_threshold_per_tc,
						u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPB_RXBHI_THRESH_ADR(buffer),
			    HW_ATL_RPB_RXBHI_THRESH_MSK,
			    HW_ATL_RPB_RXBHI_THRESH_SHIFT,
			    rx_buff_hi_threshold_per_tc);
}

void hw_atl_rpb_rx_buff_lo_threshold_per_tc_set(struct aq_hw_s *aq_hw,
						u32 rx_buff_lo_threshold_per_tc,
						u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPB_RXBLO_THRESH_ADR(buffer),
			    HW_ATL_RPB_RXBLO_THRESH_MSK,
			    HW_ATL_RPB_RXBLO_THRESH_SHIFT,
			    rx_buff_lo_threshold_per_tc);
}

void hw_atl_rpb_rx_flow_ctl_mode_set(struct aq_hw_s *aq_hw, u32 rx_flow_ctl_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPB_RX_FC_MODE_ADR,
			    HW_ATL_RPB_RX_FC_MODE_MSK,
			    HW_ATL_RPB_RX_FC_MODE_SHIFT, rx_flow_ctl_mode);
}

void hw_atl_rdm_rx_dma_desc_cache_init_set(struct aq_hw_s *aq_hw, u32 init)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RDM_RX_DMA_DESC_CACHE_INIT_ADR,
			    HW_ATL_RDM_RX_DMA_DESC_CACHE_INIT_MSK,
			    HW_ATL_RDM_RX_DMA_DESC_CACHE_INIT_SHIFT,
			    init);
}

void hw_atl_rpb_rx_pkt_buff_size_per_tc_set(struct aq_hw_s *aq_hw,
					    u32 rx_pkt_buff_size_per_tc, u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPB_RXBBUF_SIZE_ADR(buffer),
			    HW_ATL_RPB_RXBBUF_SIZE_MSK,
			    HW_ATL_RPB_RXBBUF_SIZE_SHIFT,
			    rx_pkt_buff_size_per_tc);
}

void hw_atl_rpb_rx_xoff_en_per_tc_set(struct aq_hw_s *aq_hw, u32 rx_xoff_en_per_tc,
				      u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPB_RXBXOFF_EN_ADR(buffer),
			    HW_ATL_RPB_RXBXOFF_EN_MSK,
			    HW_ATL_RPB_RXBXOFF_EN_SHIFT,
			    rx_xoff_en_per_tc);
}

/* rpf */

void hw_atl_rpfl2broadcast_count_threshold_set(struct aq_hw_s *aq_hw,
					       u32 l2broadcast_count_threshold)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2BC_THRESH_ADR,
			    HW_ATL_RPFL2BC_THRESH_MSK,
			    HW_ATL_RPFL2BC_THRESH_SHIFT,
			    l2broadcast_count_threshold);
}

void hw_atl_rpfl2broadcast_en_set(struct aq_hw_s *aq_hw, u32 l2broadcast_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2BC_EN_ADR, HW_ATL_RPFL2BC_EN_MSK,
			    HW_ATL_RPFL2BC_EN_SHIFT, l2broadcast_en);
}

void hw_atl_rpfl2broadcast_flr_act_set(struct aq_hw_s *aq_hw,
				       u32 l2broadcast_flr_act)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2BC_ACT_ADR,
			    HW_ATL_RPFL2BC_ACT_MSK,
			    HW_ATL_RPFL2BC_ACT_SHIFT, l2broadcast_flr_act);
}

void hw_atl_rpfl2multicast_flr_en_set(struct aq_hw_s *aq_hw,
				      u32 l2multicast_flr_en,
				      u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2MC_ENF_ADR(filter),
			    HW_ATL_RPFL2MC_ENF_MSK,
			    HW_ATL_RPFL2MC_ENF_SHIFT, l2multicast_flr_en);
}

void hw_atl_rpfl2promiscuous_mode_en_set(struct aq_hw_s *aq_hw,
					 u32 l2promiscuous_mode_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2PROMIS_MODE_ADR,
			    HW_ATL_RPFL2PROMIS_MODE_MSK,
			    HW_ATL_RPFL2PROMIS_MODE_SHIFT,
			    l2promiscuous_mode_en);
}

void hw_atl_rpfl2unicast_flr_act_set(struct aq_hw_s *aq_hw,
				     u32 l2unicast_flr_act,
				     u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2UC_ACTF_ADR(filter),
			    HW_ATL_RPFL2UC_ACTF_MSK, HW_ATL_RPFL2UC_ACTF_SHIFT,
			    l2unicast_flr_act);
}

void hw_atl_rpfl2_uc_flr_en_set(struct aq_hw_s *aq_hw, u32 l2unicast_flr_en,
				u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2UC_ENF_ADR(filter),
			    HW_ATL_RPFL2UC_ENF_MSK,
			    HW_ATL_RPFL2UC_ENF_SHIFT, l2unicast_flr_en);
}

void hw_atl_rpfl2unicast_dest_addresslsw_set(struct aq_hw_s *aq_hw,
					     u32 l2unicast_dest_addresslsw,
					     u32 filter)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPFL2UC_DAFLSW_ADR(filter),
			l2unicast_dest_addresslsw);
}

void hw_atl_rpfl2unicast_dest_addressmsw_set(struct aq_hw_s *aq_hw,
					     u32 l2unicast_dest_addressmsw,
					     u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2UC_DAFMSW_ADR(filter),
			    HW_ATL_RPFL2UC_DAFMSW_MSK,
			    HW_ATL_RPFL2UC_DAFMSW_SHIFT,
			    l2unicast_dest_addressmsw);
}

void hw_atl_rpfl2_accept_all_mc_packets_set(struct aq_hw_s *aq_hw,
					    u32 l2_accept_all_mc_packets)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPFL2MC_ACCEPT_ALL_ADR,
			    HW_ATL_RPFL2MC_ACCEPT_ALL_MSK,
			    HW_ATL_RPFL2MC_ACCEPT_ALL_SHIFT,
			    l2_accept_all_mc_packets);
}

void hw_atl_rpf_rpb_user_priority_tc_map_set(struct aq_hw_s *aq_hw,
					     u32 user_priority_tc_map, u32 tc)
{
/* register address for bitfield rx_tc_up{t}[2:0] */
	static u32 rpf_rpb_rx_tc_upt_adr[8] = {
			0x000054c4U, 0x000054C4U, 0x000054C4U, 0x000054C4U,
			0x000054c4U, 0x000054C4U, 0x000054C4U, 0x000054C4U
		};

/* bitmask for bitfield rx_tc_up{t}[2:0] */
	static u32 rpf_rpb_rx_tc_upt_msk[8] = {
			0x00000007U, 0x00000070U, 0x00000700U, 0x00007000U,
			0x00070000U, 0x00700000U, 0x07000000U, 0x70000000U
		};

/* lower bit position of bitfield rx_tc_up{t}[2:0] */
	static u32 rpf_rpb_rx_tc_upt_shft[8] = {
			0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U
		};

	aq_hw_write_reg_bit(aq_hw, rpf_rpb_rx_tc_upt_adr[tc],
			    rpf_rpb_rx_tc_upt_msk[tc],
			    rpf_rpb_rx_tc_upt_shft[tc],
			    user_priority_tc_map);
}

void hw_atl_rpf_rss_key_addr_set(struct aq_hw_s *aq_hw, u32 rss_key_addr)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_RSS_KEY_ADDR_ADR,
			    HW_ATL_RPF_RSS_KEY_ADDR_MSK,
			    HW_ATL_RPF_RSS_KEY_ADDR_SHIFT,
			    rss_key_addr);
}

void hw_atl_rpf_rss_key_wr_data_set(struct aq_hw_s *aq_hw, u32 rss_key_wr_data)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPF_RSS_KEY_WR_DATA_ADR,
			rss_key_wr_data);
}

u32 hw_atl_rpf_rss_key_wr_en_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL_RPF_RSS_KEY_WR_ENI_ADR,
				  HW_ATL_RPF_RSS_KEY_WR_ENI_MSK,
				  HW_ATL_RPF_RSS_KEY_WR_ENI_SHIFT);
}

void hw_atl_rpf_rss_key_wr_en_set(struct aq_hw_s *aq_hw, u32 rss_key_wr_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_RSS_KEY_WR_ENI_ADR,
			    HW_ATL_RPF_RSS_KEY_WR_ENI_MSK,
			    HW_ATL_RPF_RSS_KEY_WR_ENI_SHIFT,
			    rss_key_wr_en);
}

void hw_atl_rpf_rss_redir_tbl_addr_set(struct aq_hw_s *aq_hw,
				       u32 rss_redir_tbl_addr)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_RSS_REDIR_ADDR_ADR,
			    HW_ATL_RPF_RSS_REDIR_ADDR_MSK,
			    HW_ATL_RPF_RSS_REDIR_ADDR_SHIFT,
			    rss_redir_tbl_addr);
}

void hw_atl_rpf_rss_redir_tbl_wr_data_set(struct aq_hw_s *aq_hw,
					  u32 rss_redir_tbl_wr_data)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_RSS_REDIR_WR_DATA_ADR,
			    HW_ATL_RPF_RSS_REDIR_WR_DATA_MSK,
			    HW_ATL_RPF_RSS_REDIR_WR_DATA_SHIFT,
			    rss_redir_tbl_wr_data);
}

u32 hw_atl_rpf_rss_redir_wr_en_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL_RPF_RSS_REDIR_WR_ENI_ADR,
				  HW_ATL_RPF_RSS_REDIR_WR_ENI_MSK,
				  HW_ATL_RPF_RSS_REDIR_WR_ENI_SHIFT);
}

void hw_atl_rpf_rss_redir_wr_en_set(struct aq_hw_s *aq_hw, u32 rss_redir_wr_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_RSS_REDIR_WR_ENI_ADR,
			    HW_ATL_RPF_RSS_REDIR_WR_ENI_MSK,
			    HW_ATL_RPF_RSS_REDIR_WR_ENI_SHIFT, rss_redir_wr_en);
}

void hw_atl_rpf_tpo_to_rpf_sys_lbk_set(struct aq_hw_s *aq_hw,
				       u32 tpo_to_rpf_sys_lbk)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_TPO_RPF_SYS_LBK_ADR,
			    HW_ATL_RPF_TPO_RPF_SYS_LBK_MSK,
			    HW_ATL_RPF_TPO_RPF_SYS_LBK_SHIFT,
			    tpo_to_rpf_sys_lbk);
}

void hw_atl_rpf_vlan_inner_etht_set(struct aq_hw_s *aq_hw, u32 vlan_inner_etht)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_INNER_TPID_ADR,
			    HW_ATL_RPF_VL_INNER_TPID_MSK,
			    HW_ATL_RPF_VL_INNER_TPID_SHIFT,
			    vlan_inner_etht);
}

void hw_atl_rpf_vlan_outer_etht_set(struct aq_hw_s *aq_hw, u32 vlan_outer_etht)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_OUTER_TPID_ADR,
			    HW_ATL_RPF_VL_OUTER_TPID_MSK,
			    HW_ATL_RPF_VL_OUTER_TPID_SHIFT,
			    vlan_outer_etht);
}

void hw_atl_rpf_vlan_prom_mode_en_set(struct aq_hw_s *aq_hw,
				      u32 vlan_prom_mode_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_PROMIS_MODE_ADR,
			    HW_ATL_RPF_VL_PROMIS_MODE_MSK,
			    HW_ATL_RPF_VL_PROMIS_MODE_SHIFT,
			    vlan_prom_mode_en);
}

void hw_atl_rpf_vlan_accept_untagged_packets_set(struct aq_hw_s *aq_hw,
						 u32 vlan_acc_untagged_packets)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_ACCEPT_UNTAGGED_MODE_ADR,
			    HW_ATL_RPF_VL_ACCEPT_UNTAGGED_MODE_MSK,
			    HW_ATL_RPF_VL_ACCEPT_UNTAGGED_MODE_SHIFT,
			    vlan_acc_untagged_packets);
}

void hw_atl_rpf_vlan_untagged_act_set(struct aq_hw_s *aq_hw,
				      u32 vlan_untagged_act)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_UNTAGGED_ACT_ADR,
			    HW_ATL_RPF_VL_UNTAGGED_ACT_MSK,
			    HW_ATL_RPF_VL_UNTAGGED_ACT_SHIFT,
			    vlan_untagged_act);
}

void hw_atl_rpf_vlan_flr_en_set(struct aq_hw_s *aq_hw, u32 vlan_flr_en,
				u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_EN_F_ADR(filter),
			    HW_ATL_RPF_VL_EN_F_MSK,
			    HW_ATL_RPF_VL_EN_F_SHIFT,
			    vlan_flr_en);
}

void hw_atl_rpf_vlan_flr_act_set(struct aq_hw_s *aq_hw, u32 vlan_flr_act,
				 u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_ACT_F_ADR(filter),
			    HW_ATL_RPF_VL_ACT_F_MSK,
			    HW_ATL_RPF_VL_ACT_F_SHIFT,
			    vlan_flr_act);
}

void hw_atl_rpf_vlan_id_flr_set(struct aq_hw_s *aq_hw, u32 vlan_id_flr,
				u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_ID_F_ADR(filter),
			    HW_ATL_RPF_VL_ID_F_MSK,
			    HW_ATL_RPF_VL_ID_F_SHIFT,
			    vlan_id_flr);
}

void hw_atl_rpf_vlan_rxq_en_flr_set(struct aq_hw_s *aq_hw, u32 vlan_rxq_en,
				    u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_RXQ_EN_F_ADR(filter),
			    HW_ATL_RPF_VL_RXQ_EN_F_MSK,
			    HW_ATL_RPF_VL_RXQ_EN_F_SHIFT,
			    vlan_rxq_en);
}

void hw_atl_rpf_vlan_rxq_flr_set(struct aq_hw_s *aq_hw, u32 vlan_rxq,
				 u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_VL_RXQ_F_ADR(filter),
			    HW_ATL_RPF_VL_RXQ_F_MSK,
			    HW_ATL_RPF_VL_RXQ_F_SHIFT,
			    vlan_rxq);
};

void hw_atl_rpf_etht_flr_en_set(struct aq_hw_s *aq_hw, u32 etht_flr_en,
				u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_ET_ENF_ADR(filter),
			    HW_ATL_RPF_ET_ENF_MSK,
			    HW_ATL_RPF_ET_ENF_SHIFT, etht_flr_en);
}

void hw_atl_rpf_etht_user_priority_en_set(struct aq_hw_s *aq_hw,
					  u32 etht_user_priority_en, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_ET_UPFEN_ADR(filter),
			    HW_ATL_RPF_ET_UPFEN_MSK, HW_ATL_RPF_ET_UPFEN_SHIFT,
			    etht_user_priority_en);
}

void hw_atl_rpf_etht_rx_queue_en_set(struct aq_hw_s *aq_hw,
				     u32 etht_rx_queue_en,
				     u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_ET_RXQFEN_ADR(filter),
			    HW_ATL_RPF_ET_RXQFEN_MSK,
			    HW_ATL_RPF_ET_RXQFEN_SHIFT,
			    etht_rx_queue_en);
}

void hw_atl_rpf_etht_user_priority_set(struct aq_hw_s *aq_hw,
				       u32 etht_user_priority,
				       u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_ET_UPF_ADR(filter),
			    HW_ATL_RPF_ET_UPF_MSK,
			    HW_ATL_RPF_ET_UPF_SHIFT, etht_user_priority);
}

void hw_atl_rpf_etht_rx_queue_set(struct aq_hw_s *aq_hw, u32 etht_rx_queue,
				  u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_ET_RXQF_ADR(filter),
			    HW_ATL_RPF_ET_RXQF_MSK,
			    HW_ATL_RPF_ET_RXQF_SHIFT, etht_rx_queue);
}

void hw_atl_rpf_etht_mgt_queue_set(struct aq_hw_s *aq_hw, u32 etht_mgt_queue,
				   u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_ET_MNG_RXQF_ADR(filter),
			    HW_ATL_RPF_ET_MNG_RXQF_MSK,
			    HW_ATL_RPF_ET_MNG_RXQF_SHIFT,
			    etht_mgt_queue);
}

void hw_atl_rpf_etht_flr_act_set(struct aq_hw_s *aq_hw, u32 etht_flr_act,
				 u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_ET_ACTF_ADR(filter),
			    HW_ATL_RPF_ET_ACTF_MSK,
			    HW_ATL_RPF_ET_ACTF_SHIFT, etht_flr_act);
}

void hw_atl_rpf_etht_flr_set(struct aq_hw_s *aq_hw, u32 etht_flr, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_ET_VALF_ADR(filter),
			    HW_ATL_RPF_ET_VALF_MSK,
			    HW_ATL_RPF_ET_VALF_SHIFT, etht_flr);
}

void hw_atl_rpf_l4_spd_set(struct aq_hw_s *aq_hw, u32 val, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_L4_SPD_ADR(filter),
			    HW_ATL_RPF_L4_SPD_MSK,
			    HW_ATL_RPF_L4_SPD_SHIFT, val);
}

void hw_atl_rpf_l4_dpd_set(struct aq_hw_s *aq_hw, u32 val, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPF_L4_DPD_ADR(filter),
			    HW_ATL_RPF_L4_DPD_MSK,
			    HW_ATL_RPF_L4_DPD_SHIFT, val);
}

/* RPO: rx packet offload */
void hw_atl_rpo_ipv4header_crc_offload_en_set(struct aq_hw_s *aq_hw,
					      u32 ipv4header_crc_offload_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_IPV4CHK_EN_ADR,
			    HW_ATL_RPO_IPV4CHK_EN_MSK,
			    HW_ATL_RPO_IPV4CHK_EN_SHIFT,
			    ipv4header_crc_offload_en);
}

void hw_atl_rpo_rx_desc_vlan_stripping_set(struct aq_hw_s *aq_hw,
					   u32 rx_desc_vlan_stripping,
					   u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_DESCDVL_STRIP_ADR(descriptor),
			    HW_ATL_RPO_DESCDVL_STRIP_MSK,
			    HW_ATL_RPO_DESCDVL_STRIP_SHIFT,
			    rx_desc_vlan_stripping);
}

void hw_atl_rpo_outer_vlan_tag_mode_set(void *context,
					u32 outervlantagmode)
{
	aq_hw_write_reg_bit(context, HW_ATL_RPO_OUTER_VL_INS_MODE_ADR,
			    HW_ATL_RPO_OUTER_VL_INS_MODE_MSK,
			    HW_ATL_RPO_OUTER_VL_INS_MODE_SHIFT,
			    outervlantagmode);
}

u32 hw_atl_rpo_outer_vlan_tag_mode_get(void *context)
{
	return aq_hw_read_reg_bit(context, HW_ATL_RPO_OUTER_VL_INS_MODE_ADR,
				  HW_ATL_RPO_OUTER_VL_INS_MODE_MSK,
				  HW_ATL_RPO_OUTER_VL_INS_MODE_SHIFT);
}

void hw_atl_rpo_tcp_udp_crc_offload_en_set(struct aq_hw_s *aq_hw,
					   u32 tcp_udp_crc_offload_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPOL4CHK_EN_ADR,
			    HW_ATL_RPOL4CHK_EN_MSK,
			    HW_ATL_RPOL4CHK_EN_SHIFT, tcp_udp_crc_offload_en);
}

void hw_atl_rpo_lro_en_set(struct aq_hw_s *aq_hw, u32 lro_en)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPO_LRO_EN_ADR, lro_en);
}

void hw_atl_rpo_lro_patch_optimization_en_set(struct aq_hw_s *aq_hw,
					      u32 lro_patch_optimization_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_LRO_PTOPT_EN_ADR,
			    HW_ATL_RPO_LRO_PTOPT_EN_MSK,
			    HW_ATL_RPO_LRO_PTOPT_EN_SHIFT,
			    lro_patch_optimization_en);
}

void hw_atl_rpo_lro_qsessions_lim_set(struct aq_hw_s *aq_hw,
				      u32 lro_qsessions_lim)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_LRO_QSES_LMT_ADR,
			    HW_ATL_RPO_LRO_QSES_LMT_MSK,
			    HW_ATL_RPO_LRO_QSES_LMT_SHIFT,
			    lro_qsessions_lim);
}

void hw_atl_rpo_lro_total_desc_lim_set(struct aq_hw_s *aq_hw,
				       u32 lro_total_desc_lim)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_LRO_TOT_DSC_LMT_ADR,
			    HW_ATL_RPO_LRO_TOT_DSC_LMT_MSK,
			    HW_ATL_RPO_LRO_TOT_DSC_LMT_SHIFT,
			    lro_total_desc_lim);
}

void hw_atl_rpo_lro_min_pay_of_first_pkt_set(struct aq_hw_s *aq_hw,
					     u32 lro_min_pld_of_first_pkt)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_LRO_PKT_MIN_ADR,
			    HW_ATL_RPO_LRO_PKT_MIN_MSK,
			    HW_ATL_RPO_LRO_PKT_MIN_SHIFT,
			    lro_min_pld_of_first_pkt);
}

void hw_atl_rpo_lro_pkt_lim_set(struct aq_hw_s *aq_hw, u32 lro_pkt_lim)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPO_LRO_RSC_MAX_ADR, lro_pkt_lim);
}

void hw_atl_rpo_lro_max_num_of_descriptors_set(struct aq_hw_s *aq_hw,
					       u32 lro_max_number_of_descriptors,
					       u32 lro)
{
/* Register address for bitfield lro{L}_des_max[1:0] */
	static u32 rpo_lro_ldes_max_adr[32] = {
			0x000055A0U, 0x000055A0U, 0x000055A0U, 0x000055A0U,
			0x000055A0U, 0x000055A0U, 0x000055A0U, 0x000055A0U,
			0x000055A4U, 0x000055A4U, 0x000055A4U, 0x000055A4U,
			0x000055A4U, 0x000055A4U, 0x000055A4U, 0x000055A4U,
			0x000055A8U, 0x000055A8U, 0x000055A8U, 0x000055A8U,
			0x000055A8U, 0x000055A8U, 0x000055A8U, 0x000055A8U,
			0x000055ACU, 0x000055ACU, 0x000055ACU, 0x000055ACU,
			0x000055ACU, 0x000055ACU, 0x000055ACU, 0x000055ACU
		};

/* Bitmask for bitfield lro{L}_des_max[1:0] */
	static u32 rpo_lro_ldes_max_msk[32] = {
			0x00000003U, 0x00000030U, 0x00000300U, 0x00003000U,
			0x00030000U, 0x00300000U, 0x03000000U, 0x30000000U,
			0x00000003U, 0x00000030U, 0x00000300U, 0x00003000U,
			0x00030000U, 0x00300000U, 0x03000000U, 0x30000000U,
			0x00000003U, 0x00000030U, 0x00000300U, 0x00003000U,
			0x00030000U, 0x00300000U, 0x03000000U, 0x30000000U,
			0x00000003U, 0x00000030U, 0x00000300U, 0x00003000U,
			0x00030000U, 0x00300000U, 0x03000000U, 0x30000000U
		};

/* Lower bit position of bitfield lro{L}_des_max[1:0] */
	static u32 rpo_lro_ldes_max_shift[32] = {
			0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U,
			0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U,
			0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U,
			0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U
		};

	aq_hw_write_reg_bit(aq_hw, rpo_lro_ldes_max_adr[lro],
			    rpo_lro_ldes_max_msk[lro],
			    rpo_lro_ldes_max_shift[lro],
			    lro_max_number_of_descriptors);
}

void hw_atl_rpo_lro_time_base_divider_set(struct aq_hw_s *aq_hw,
					  u32 lro_time_base_divider)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_LRO_TB_DIV_ADR,
			    HW_ATL_RPO_LRO_TB_DIV_MSK,
			    HW_ATL_RPO_LRO_TB_DIV_SHIFT,
			    lro_time_base_divider);
}

void hw_atl_rpo_lro_inactive_interval_set(struct aq_hw_s *aq_hw,
					  u32 lro_inactive_interval)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_LRO_INA_IVAL_ADR,
			    HW_ATL_RPO_LRO_INA_IVAL_MSK,
			    HW_ATL_RPO_LRO_INA_IVAL_SHIFT,
			    lro_inactive_interval);
}

void hw_atl_rpo_lro_max_coalescing_interval_set(struct aq_hw_s *aq_hw,
						u32 lro_max_coal_interval)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RPO_LRO_MAX_IVAL_ADR,
			    HW_ATL_RPO_LRO_MAX_IVAL_MSK,
			    HW_ATL_RPO_LRO_MAX_IVAL_SHIFT,
			    lro_max_coal_interval);
}

/* rx */
void hw_atl_rx_rx_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 rx_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_RX_REG_RES_DSBL_ADR,
			    HW_ATL_RX_REG_RES_DSBL_MSK,
			    HW_ATL_RX_REG_RES_DSBL_SHIFT,
			    rx_reg_res_dis);
}

/* tdm */
void hw_atl_tdm_cpu_id_set(struct aq_hw_s *aq_hw, u32 cpuid, u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_DCADCPUID_ADR(dca),
			    HW_ATL_TDM_DCADCPUID_MSK,
			    HW_ATL_TDM_DCADCPUID_SHIFT, cpuid);
}

void hw_atl_tdm_large_send_offload_en_set(struct aq_hw_s *aq_hw,
					  u32 large_send_offload_en)
{
	aq_hw_write_reg(aq_hw, HW_ATL_TDM_LSO_EN_ADR, large_send_offload_en);
}

void hw_atl_tdm_tx_dca_en_set(struct aq_hw_s *aq_hw, u32 tx_dca_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_DCA_EN_ADR, HW_ATL_TDM_DCA_EN_MSK,
			    HW_ATL_TDM_DCA_EN_SHIFT, tx_dca_en);
}

void hw_atl_tdm_tx_dca_mode_set(struct aq_hw_s *aq_hw, u32 tx_dca_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_DCA_MODE_ADR,
			    HW_ATL_TDM_DCA_MODE_MSK,
			    HW_ATL_TDM_DCA_MODE_SHIFT, tx_dca_mode);
}

void hw_atl_tdm_tx_desc_dca_en_set(struct aq_hw_s *aq_hw, u32 tx_desc_dca_en,
				   u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_DCADDESC_EN_ADR(dca),
			    HW_ATL_TDM_DCADDESC_EN_MSK,
			    HW_ATL_TDM_DCADDESC_EN_SHIFT,
			    tx_desc_dca_en);
}

void hw_atl_tdm_tx_desc_en_set(struct aq_hw_s *aq_hw, u32 tx_desc_en,
			       u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_DESCDEN_ADR(descriptor),
			    HW_ATL_TDM_DESCDEN_MSK,
			    HW_ATL_TDM_DESCDEN_SHIFT,
			    tx_desc_en);
}

u32 hw_atl_tdm_tx_desc_head_ptr_get(struct aq_hw_s *aq_hw, u32 descriptor)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL_TDM_DESCDHD_ADR(descriptor),
				  HW_ATL_TDM_DESCDHD_MSK,
				  HW_ATL_TDM_DESCDHD_SHIFT);
}

void hw_atl_tdm_tx_desc_len_set(struct aq_hw_s *aq_hw, u32 tx_desc_len,
				u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_DESCDLEN_ADR(descriptor),
			    HW_ATL_TDM_DESCDLEN_MSK,
			    HW_ATL_TDM_DESCDLEN_SHIFT,
			    tx_desc_len);
}

void hw_atl_tdm_tx_desc_wr_wb_irq_en_set(struct aq_hw_s *aq_hw,
					 u32 tx_desc_wr_wb_irq_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_INT_DESC_WRB_EN_ADR,
			    HW_ATL_TDM_INT_DESC_WRB_EN_MSK,
			    HW_ATL_TDM_INT_DESC_WRB_EN_SHIFT,
			    tx_desc_wr_wb_irq_en);
}

void hw_atl_tdm_tx_desc_wr_wb_threshold_set(struct aq_hw_s *aq_hw,
					    u32 tx_desc_wr_wb_threshold,
					    u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_DESCDWRB_THRESH_ADR(descriptor),
			    HW_ATL_TDM_DESCDWRB_THRESH_MSK,
			    HW_ATL_TDM_DESCDWRB_THRESH_SHIFT,
			    tx_desc_wr_wb_threshold);
}

void hw_atl_tdm_tdm_intr_moder_en_set(struct aq_hw_s *aq_hw,
				      u32 tdm_irq_moderation_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TDM_INT_MOD_EN_ADR,
			    HW_ATL_TDM_INT_MOD_EN_MSK,
			    HW_ATL_TDM_INT_MOD_EN_SHIFT,
			    tdm_irq_moderation_en);
}

/* thm */
void hw_atl_thm_lso_tcp_flag_of_first_pkt_set(struct aq_hw_s *aq_hw,
					      u32 lso_tcp_flag_of_first_pkt)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_THM_LSO_TCP_FLAG_FIRST_ADR,
			    HW_ATL_THM_LSO_TCP_FLAG_FIRST_MSK,
			    HW_ATL_THM_LSO_TCP_FLAG_FIRST_SHIFT,
			    lso_tcp_flag_of_first_pkt);
}

void hw_atl_thm_lso_tcp_flag_of_last_pkt_set(struct aq_hw_s *aq_hw,
					     u32 lso_tcp_flag_of_last_pkt)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_THM_LSO_TCP_FLAG_LAST_ADR,
			    HW_ATL_THM_LSO_TCP_FLAG_LAST_MSK,
			    HW_ATL_THM_LSO_TCP_FLAG_LAST_SHIFT,
			    lso_tcp_flag_of_last_pkt);
}

void hw_atl_thm_lso_tcp_flag_of_middle_pkt_set(struct aq_hw_s *aq_hw,
					       u32 lso_tcp_flag_of_middle_pkt)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_THM_LSO_TCP_FLAG_MID_ADR,
			    HW_ATL_THM_LSO_TCP_FLAG_MID_MSK,
			    HW_ATL_THM_LSO_TCP_FLAG_MID_SHIFT,
			    lso_tcp_flag_of_middle_pkt);
}

/* TPB: tx packet buffer */
void hw_atl_tpb_tx_buff_en_set(struct aq_hw_s *aq_hw, u32 tx_buff_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPB_TX_BUF_EN_ADR,
			    HW_ATL_TPB_TX_BUF_EN_MSK,
			    HW_ATL_TPB_TX_BUF_EN_SHIFT, tx_buff_en);
}

void hw_atl_rpb_tps_tx_tc_mode_set(struct aq_hw_s *aq_hw,
				   u32 tx_traf_class_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPB_TX_TC_MODE_ADDR,
			HW_ATL_TPB_TX_TC_MODE_MSK,
			HW_ATL_TPB_TX_TC_MODE_SHIFT,
			tx_traf_class_mode);
}

void hw_atl_tpb_tx_buff_hi_threshold_per_tc_set(struct aq_hw_s *aq_hw,
						u32 tx_buff_hi_threshold_per_tc,
					 u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPB_TXBHI_THRESH_ADR(buffer),
			    HW_ATL_TPB_TXBHI_THRESH_MSK,
			    HW_ATL_TPB_TXBHI_THRESH_SHIFT,
			    tx_buff_hi_threshold_per_tc);
}

void hw_atl_tpb_tx_buff_lo_threshold_per_tc_set(struct aq_hw_s *aq_hw,
						u32 tx_buff_lo_threshold_per_tc,
					 u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPB_TXBLO_THRESH_ADR(buffer),
			    HW_ATL_TPB_TXBLO_THRESH_MSK,
			    HW_ATL_TPB_TXBLO_THRESH_SHIFT,
			    tx_buff_lo_threshold_per_tc);
}

void hw_atl_tpb_tx_dma_sys_lbk_en_set(struct aq_hw_s *aq_hw, u32 tx_dma_sys_lbk_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPB_DMA_SYS_LBK_ADR,
			    HW_ATL_TPB_DMA_SYS_LBK_MSK,
			    HW_ATL_TPB_DMA_SYS_LBK_SHIFT,
			    tx_dma_sys_lbk_en);
}

void hw_atl_tpb_tx_pkt_buff_size_per_tc_set(struct aq_hw_s *aq_hw,
					    u32 tx_pkt_buff_size_per_tc, u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPB_TXBBUF_SIZE_ADR(buffer),
			    HW_ATL_TPB_TXBBUF_SIZE_MSK,
			    HW_ATL_TPB_TXBBUF_SIZE_SHIFT,
			    tx_pkt_buff_size_per_tc);
}

void hw_atl_tpb_tx_path_scp_ins_en_set(struct aq_hw_s *aq_hw, u32 tx_path_scp_ins_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPB_TX_SCP_INS_EN_ADR,
			    HW_ATL_TPB_TX_SCP_INS_EN_MSK,
			    HW_ATL_TPB_TX_SCP_INS_EN_SHIFT,
			    tx_path_scp_ins_en);
}

/* TPO: tx packet offload */
void hw_atl_tpo_ipv4header_crc_offload_en_set(struct aq_hw_s *aq_hw,
					      u32 ipv4header_crc_offload_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPO_IPV4CHK_EN_ADR,
			    HW_ATL_TPO_IPV4CHK_EN_MSK,
			    HW_ATL_TPO_IPV4CHK_EN_SHIFT,
			    ipv4header_crc_offload_en);
}

void hw_atl_tpo_tcp_udp_crc_offload_en_set(struct aq_hw_s *aq_hw,
					   u32 tcp_udp_crc_offload_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPOL4CHK_EN_ADR,
			    HW_ATL_TPOL4CHK_EN_MSK,
			    HW_ATL_TPOL4CHK_EN_SHIFT,
			    tcp_udp_crc_offload_en);
}

void hw_atl_tpo_tx_pkt_sys_lbk_en_set(struct aq_hw_s *aq_hw,
				      u32 tx_pkt_sys_lbk_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPO_PKT_SYS_LBK_ADR,
			    HW_ATL_TPO_PKT_SYS_LBK_MSK,
			    HW_ATL_TPO_PKT_SYS_LBK_SHIFT,
			    tx_pkt_sys_lbk_en);
}

/* TPS: tx packet scheduler */
void hw_atl_tps_tx_pkt_shed_data_arb_mode_set(struct aq_hw_s *aq_hw,
					      u32 tx_pkt_shed_data_arb_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DATA_TC_ARB_MODE_ADR,
			    HW_ATL_TPS_DATA_TC_ARB_MODE_MSK,
			    HW_ATL_TPS_DATA_TC_ARB_MODE_SHIFT,
			    tx_pkt_shed_data_arb_mode);
}

void hw_atl_tps_tx_pkt_shed_desc_rate_curr_time_res_set(struct aq_hw_s *aq_hw,
							u32 curr_time_res)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DESC_RATE_TA_RST_ADR,
			    HW_ATL_TPS_DESC_RATE_TA_RST_MSK,
			    HW_ATL_TPS_DESC_RATE_TA_RST_SHIFT,
			    curr_time_res);
}

void hw_atl_tps_tx_pkt_shed_desc_rate_lim_set(struct aq_hw_s *aq_hw,
					      u32 tx_pkt_shed_desc_rate_lim)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DESC_RATE_LIM_ADR,
			    HW_ATL_TPS_DESC_RATE_LIM_MSK,
			    HW_ATL_TPS_DESC_RATE_LIM_SHIFT,
			    tx_pkt_shed_desc_rate_lim);
}

void hw_atl_tps_tx_pkt_shed_desc_tc_arb_mode_set(struct aq_hw_s *aq_hw,
						 u32 arb_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DESC_TC_ARB_MODE_ADR,
			    HW_ATL_TPS_DESC_TC_ARB_MODE_MSK,
			    HW_ATL_TPS_DESC_TC_ARB_MODE_SHIFT,
			    arb_mode);
}

void hw_atl_tps_tx_pkt_shed_desc_tc_max_credit_set(struct aq_hw_s *aq_hw,
						   u32 max_credit,
						   u32 tc)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DESC_TCTCREDIT_MAX_ADR(tc),
			    HW_ATL_TPS_DESC_TCTCREDIT_MAX_MSK,
			    HW_ATL_TPS_DESC_TCTCREDIT_MAX_SHIFT,
			    max_credit);
}

void hw_atl_tps_tx_pkt_shed_desc_tc_weight_set(struct aq_hw_s *aq_hw,
					       u32 tx_pkt_shed_desc_tc_weight,
					       u32 tc)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DESC_TCTWEIGHT_ADR(tc),
			    HW_ATL_TPS_DESC_TCTWEIGHT_MSK,
			    HW_ATL_TPS_DESC_TCTWEIGHT_SHIFT,
			    tx_pkt_shed_desc_tc_weight);
}

void hw_atl_tps_tx_pkt_shed_desc_vm_arb_mode_set(struct aq_hw_s *aq_hw,
						 u32 arb_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DESC_VM_ARB_MODE_ADR,
			    HW_ATL_TPS_DESC_VM_ARB_MODE_MSK,
			    HW_ATL_TPS_DESC_VM_ARB_MODE_SHIFT,
			    arb_mode);
}

void hw_atl_tps_tx_pkt_shed_tc_data_max_credit_set(struct aq_hw_s *aq_hw,
						   u32 max_credit,
						   u32 tc)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DATA_TCTCREDIT_MAX_ADR(tc),
			    HW_ATL_TPS_DATA_TCTCREDIT_MAX_MSK,
			    HW_ATL_TPS_DATA_TCTCREDIT_MAX_SHIFT,
			    max_credit);
}

void hw_atl_tps_tx_pkt_shed_tc_data_weight_set(struct aq_hw_s *aq_hw,
					       u32 tx_pkt_shed_tc_data_weight,
					       u32 tc)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TPS_DATA_TCTWEIGHT_ADR(tc),
			    HW_ATL_TPS_DATA_TCTWEIGHT_MSK,
			    HW_ATL_TPS_DATA_TCTWEIGHT_SHIFT,
			    tx_pkt_shed_tc_data_weight);
}

/* tx */
void hw_atl_tx_tx_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 tx_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_TX_REG_RES_DSBL_ADR,
			    HW_ATL_TX_REG_RES_DSBL_MSK,
			    HW_ATL_TX_REG_RES_DSBL_SHIFT, tx_reg_res_dis);
}

/* msm */
u32 hw_atl_msm_reg_access_status_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL_MSM_REG_ACCESS_BUSY_ADR,
				  HW_ATL_MSM_REG_ACCESS_BUSY_MSK,
				  HW_ATL_MSM_REG_ACCESS_BUSY_SHIFT);
}

void hw_atl_msm_reg_addr_for_indirect_addr_set(struct aq_hw_s *aq_hw,
					       u32 reg_addr_for_indirect_addr)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_MSM_REG_ADDR_ADR,
			    HW_ATL_MSM_REG_ADDR_MSK,
			    HW_ATL_MSM_REG_ADDR_SHIFT,
			    reg_addr_for_indirect_addr);
}

void hw_atl_msm_reg_rd_strobe_set(struct aq_hw_s *aq_hw, u32 reg_rd_strobe)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_MSM_REG_RD_STROBE_ADR,
			    HW_ATL_MSM_REG_RD_STROBE_MSK,
			    HW_ATL_MSM_REG_RD_STROBE_SHIFT,
			    reg_rd_strobe);
}

u32 hw_atl_msm_reg_rd_data_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, HW_ATL_MSM_REG_RD_DATA_ADR);
}

void hw_atl_msm_reg_wr_data_set(struct aq_hw_s *aq_hw, u32 reg_wr_data)
{
	aq_hw_write_reg(aq_hw, HW_ATL_MSM_REG_WR_DATA_ADR, reg_wr_data);
}

void hw_atl_msm_reg_wr_strobe_set(struct aq_hw_s *aq_hw, u32 reg_wr_strobe)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_MSM_REG_WR_STROBE_ADR,
			    HW_ATL_MSM_REG_WR_STROBE_MSK,
			    HW_ATL_MSM_REG_WR_STROBE_SHIFT,
			    reg_wr_strobe);
}

/* pci */
void hw_atl_pci_pci_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 pci_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_PCI_REG_RES_DSBL_ADR,
			    HW_ATL_PCI_REG_RES_DSBL_MSK,
			    HW_ATL_PCI_REG_RES_DSBL_SHIFT,
			    pci_reg_res_dis);
}

void hw_atl_reg_glb_cpu_scratch_scp_set(struct aq_hw_s *aq_hw,
					u32 glb_cpu_scratch_scp,
					u32 scratch_scp)
{
	aq_hw_write_reg(aq_hw, HW_ATL_GLB_CPU_SCRATCH_SCP_ADR(scratch_scp),
			glb_cpu_scratch_scp);
}

void hw_atl_mcp_up_force_intr_set(struct aq_hw_s *aq_hw, u32 up_force_intr)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL_MCP_UP_FORCE_INTERRUPT_ADR,
			    HW_ATL_MCP_UP_FORCE_INTERRUPT_MSK,
			    HW_ATL_MCP_UP_FORCE_INTERRUPT_SHIFT,
			    up_force_intr);
}

void hw_atl_rpfl3l4_ipv4_dest_addr_clear(struct aq_hw_s *aq_hw, u8 location)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPF_L3_DSTA_ADR(location), 0U);
}

void hw_atl_rpfl3l4_ipv4_src_addr_clear(struct aq_hw_s *aq_hw, u8 location)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPF_L3_SRCA_ADR(location), 0U);
}

void hw_atl_rpfl3l4_cmd_clear(struct aq_hw_s *aq_hw, u8 location)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPF_L3_REG_CTRL_ADR(location), 0U);
}

void hw_atl_rpfl3l4_ipv6_dest_addr_clear(struct aq_hw_s *aq_hw, u8 location)
{
	int i;

	for (i = 0; i < 4; ++i)
		aq_hw_write_reg(aq_hw,
				HW_ATL_RPF_L3_DSTA_ADR(location + i),
				0U);
}

void hw_atl_rpfl3l4_ipv6_src_addr_clear(struct aq_hw_s *aq_hw, u8 location)
{
	int i;

	for (i = 0; i < 4; ++i)
		aq_hw_write_reg(aq_hw,
				HW_ATL_RPF_L3_SRCA_ADR(location + i),
				0U);
}

void hw_atl_rpfl3l4_ipv4_dest_addr_set(struct aq_hw_s *aq_hw, u8 location,
				       u32 ipv4_dest)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPF_L3_DSTA_ADR(location),
			ipv4_dest);
}

void hw_atl_rpfl3l4_ipv4_src_addr_set(struct aq_hw_s *aq_hw, u8 location,
				      u32 ipv4_src)
{
	aq_hw_write_reg(aq_hw,
			HW_ATL_RPF_L3_SRCA_ADR(location),
			ipv4_src);
}

void hw_atl_rpfl3l4_cmd_set(struct aq_hw_s *aq_hw, u8 location, u32 cmd)
{
	aq_hw_write_reg(aq_hw, HW_ATL_RPF_L3_REG_CTRL_ADR(location), cmd);
}

void hw_atl_rpfl3l4_ipv6_src_addr_set(struct aq_hw_s *aq_hw, u8 location,
				      u32 *ipv6_src)
{
	int i;

	for (i = 0; i < 4; ++i)
		aq_hw_write_reg(aq_hw,
				HW_ATL_RPF_L3_SRCA_ADR(location + i),
				ipv6_src[i]);
}

void hw_atl_rpfl3l4_ipv6_dest_addr_set(struct aq_hw_s *aq_hw, u8 location,
				       u32 *ipv6_dest)
{
	int i;

	for (i = 0; i < 4; ++i)
		aq_hw_write_reg(aq_hw,
				HW_ATL_RPF_L3_DSTA_ADR(location + i),
				ipv6_dest[i]);
}

u32 hw_atl_sem_ram_get(struct aq_hw_s *self)
{
	return hw_atl_reg_glb_cpu_sem_get(self, HW_ATL_FW_SM_RAM);
}

u32 hw_atl_scrpad_get(struct aq_hw_s *aq_hw, u32 scratch_scp)
{
	return aq_hw_read_reg(aq_hw,
			      HW_ATL_GLB_CPU_SCRATCH_SCP_ADR(scratch_scp));
}

u32 hw_atl_scrpad12_get(struct aq_hw_s *self)
{
	return  hw_atl_scrpad_get(self, 0xB);
}

u32 hw_atl_scrpad25_get(struct aq_hw_s *self)
{
	return hw_atl_scrpad_get(self, 0x18);
}
