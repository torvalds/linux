/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File hw_atl_llh.c: Definitions of bitfield and register access functions for
 * Atlantic registers.
 */

#include "hw_atl_llh.h"
#include "hw_atl_llh_internal.h"
#include "../aq_hw_utils.h"

/* global */
void reg_glb_cpu_sem_set(struct aq_hw_s *aq_hw, u32 glb_cpu_sem, u32 semaphore)
{
	aq_hw_write_reg(aq_hw, glb_cpu_sem_adr(semaphore), glb_cpu_sem);
}

u32 reg_glb_cpu_sem_get(struct aq_hw_s *aq_hw, u32 semaphore)
{
	return aq_hw_read_reg(aq_hw, glb_cpu_sem_adr(semaphore));
}

void glb_glb_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 glb_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, glb_reg_res_dis_adr,
			    glb_reg_res_dis_msk,
			    glb_reg_res_dis_shift,
			    glb_reg_res_dis);
}

void glb_soft_res_set(struct aq_hw_s *aq_hw, u32 soft_res)
{
	aq_hw_write_reg_bit(aq_hw, glb_soft_res_adr, glb_soft_res_msk,
			    glb_soft_res_shift, soft_res);
}

u32 glb_soft_res_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, glb_soft_res_adr,
				  glb_soft_res_msk,
				  glb_soft_res_shift);
}

u32 reg_rx_dma_stat_counter7get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, rx_dma_stat_counter7_adr);
}

u32 reg_glb_mif_id_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, glb_mif_id_adr);
}

/* stats */
u32 rpb_rx_dma_drop_pkt_cnt_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, rpb_rx_dma_drop_pkt_cnt_adr);
}

u32 stats_rx_dma_good_octet_counterlsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, stats_rx_dma_good_octet_counterlsw__adr);
}

u32 stats_rx_dma_good_pkt_counterlsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, stats_rx_dma_good_pkt_counterlsw__adr);
}

u32 stats_tx_dma_good_octet_counterlsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, stats_tx_dma_good_octet_counterlsw__adr);
}

u32 stats_tx_dma_good_pkt_counterlsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, stats_tx_dma_good_pkt_counterlsw__adr);
}

u32 stats_rx_dma_good_octet_countermsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, stats_rx_dma_good_octet_countermsw__adr);
}

u32 stats_rx_dma_good_pkt_countermsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, stats_rx_dma_good_pkt_countermsw__adr);
}

u32 stats_tx_dma_good_octet_countermsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, stats_tx_dma_good_octet_countermsw__adr);
}

u32 stats_tx_dma_good_pkt_countermsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, stats_tx_dma_good_pkt_countermsw__adr);
}

/* interrupt */
void itr_irq_auto_masklsw_set(struct aq_hw_s *aq_hw, u32 irq_auto_masklsw)
{
	aq_hw_write_reg(aq_hw, itr_iamrlsw_adr, irq_auto_masklsw);
}

void itr_irq_map_en_rx_set(struct aq_hw_s *aq_hw, u32 irq_map_en_rx, u32 rx)
{
/* register address for bitfield imr_rx{r}_en */
	static u32 itr_imr_rxren_adr[32] = {
			0x00002100U, 0x00002100U, 0x00002104U, 0x00002104U,
			0x00002108U, 0x00002108U, 0x0000210cU, 0x0000210cU,
			0x00002110U, 0x00002110U, 0x00002114U, 0x00002114U,
			0x00002118U, 0x00002118U, 0x0000211cU, 0x0000211cU,
			0x00002120U, 0x00002120U, 0x00002124U, 0x00002124U,
			0x00002128U, 0x00002128U, 0x0000212cU, 0x0000212cU,
			0x00002130U, 0x00002130U, 0x00002134U, 0x00002134U,
			0x00002138U, 0x00002138U, 0x0000213cU, 0x0000213cU
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

void itr_irq_map_en_tx_set(struct aq_hw_s *aq_hw, u32 irq_map_en_tx, u32 tx)
{
/* register address for bitfield imr_tx{t}_en */
	static u32 itr_imr_txten_adr[32] = {
			0x00002100U, 0x00002100U, 0x00002104U, 0x00002104U,
			0x00002108U, 0x00002108U, 0x0000210cU, 0x0000210cU,
			0x00002110U, 0x00002110U, 0x00002114U, 0x00002114U,
			0x00002118U, 0x00002118U, 0x0000211cU, 0x0000211cU,
			0x00002120U, 0x00002120U, 0x00002124U, 0x00002124U,
			0x00002128U, 0x00002128U, 0x0000212cU, 0x0000212cU,
			0x00002130U, 0x00002130U, 0x00002134U, 0x00002134U,
			0x00002138U, 0x00002138U, 0x0000213cU, 0x0000213cU
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

void itr_irq_map_rx_set(struct aq_hw_s *aq_hw, u32 irq_map_rx, u32 rx)
{
/* register address for bitfield imr_rx{r}[4:0] */
	static u32 itr_imr_rxr_adr[32] = {
			0x00002100U, 0x00002100U, 0x00002104U, 0x00002104U,
			0x00002108U, 0x00002108U, 0x0000210cU, 0x0000210cU,
			0x00002110U, 0x00002110U, 0x00002114U, 0x00002114U,
			0x00002118U, 0x00002118U, 0x0000211cU, 0x0000211cU,
			0x00002120U, 0x00002120U, 0x00002124U, 0x00002124U,
			0x00002128U, 0x00002128U, 0x0000212cU, 0x0000212cU,
			0x00002130U, 0x00002130U, 0x00002134U, 0x00002134U,
			0x00002138U, 0x00002138U, 0x0000213cU, 0x0000213cU
		};

/* bitmask for bitfield imr_rx{r}[4:0] */
	static u32 itr_imr_rxr_msk[32] = {
			0x00001f00U, 0x0000001fU, 0x00001f00U, 0x0000001fU,
			0x00001f00U, 0x0000001fU, 0x00001f00U, 0x0000001fU,
			0x00001f00U, 0x0000001fU, 0x00001f00U, 0x0000001fU,
			0x00001f00U, 0x0000001fU, 0x00001f00U, 0x0000001fU,
			0x00001f00U, 0x0000001fU, 0x00001f00U, 0x0000001fU,
			0x00001f00U, 0x0000001fU, 0x00001f00U, 0x0000001fU,
			0x00001f00U, 0x0000001fU, 0x00001f00U, 0x0000001fU,
			0x00001f00U, 0x0000001fU, 0x00001f00U, 0x0000001fU
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

void itr_irq_map_tx_set(struct aq_hw_s *aq_hw, u32 irq_map_tx, u32 tx)
{
/* register address for bitfield imr_tx{t}[4:0] */
	static u32 itr_imr_txt_adr[32] = {
			0x00002100U, 0x00002100U, 0x00002104U, 0x00002104U,
			0x00002108U, 0x00002108U, 0x0000210cU, 0x0000210cU,
			0x00002110U, 0x00002110U, 0x00002114U, 0x00002114U,
			0x00002118U, 0x00002118U, 0x0000211cU, 0x0000211cU,
			0x00002120U, 0x00002120U, 0x00002124U, 0x00002124U,
			0x00002128U, 0x00002128U, 0x0000212cU, 0x0000212cU,
			0x00002130U, 0x00002130U, 0x00002134U, 0x00002134U,
			0x00002138U, 0x00002138U, 0x0000213cU, 0x0000213cU
		};

/* bitmask for bitfield imr_tx{t}[4:0] */
	static u32 itr_imr_txt_msk[32] = {
			0x1f000000U, 0x001f0000U, 0x1f000000U, 0x001f0000U,
			0x1f000000U, 0x001f0000U, 0x1f000000U, 0x001f0000U,
			0x1f000000U, 0x001f0000U, 0x1f000000U, 0x001f0000U,
			0x1f000000U, 0x001f0000U, 0x1f000000U, 0x001f0000U,
			0x1f000000U, 0x001f0000U, 0x1f000000U, 0x001f0000U,
			0x1f000000U, 0x001f0000U, 0x1f000000U, 0x001f0000U,
			0x1f000000U, 0x001f0000U, 0x1f000000U, 0x001f0000U,
			0x1f000000U, 0x001f0000U, 0x1f000000U, 0x001f0000U
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

void itr_irq_msk_clearlsw_set(struct aq_hw_s *aq_hw, u32 irq_msk_clearlsw)
{
	aq_hw_write_reg(aq_hw, itr_imcrlsw_adr, irq_msk_clearlsw);
}

void itr_irq_msk_setlsw_set(struct aq_hw_s *aq_hw, u32 irq_msk_setlsw)
{
	aq_hw_write_reg(aq_hw, itr_imsrlsw_adr, irq_msk_setlsw);
}

void itr_irq_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 irq_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, itr_reg_res_dsbl_adr,
			    itr_reg_res_dsbl_msk,
			    itr_reg_res_dsbl_shift, irq_reg_res_dis);
}

void itr_irq_status_clearlsw_set(struct aq_hw_s *aq_hw,
				 u32 irq_status_clearlsw)
{
	aq_hw_write_reg(aq_hw, itr_iscrlsw_adr, irq_status_clearlsw);
}

u32 itr_irq_statuslsw_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, itr_isrlsw_adr);
}

u32 itr_res_irq_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, itr_res_adr, itr_res_msk,
				  itr_res_shift);
}

void itr_res_irq_set(struct aq_hw_s *aq_hw, u32 res_irq)
{
	aq_hw_write_reg_bit(aq_hw, itr_res_adr, itr_res_msk,
			    itr_res_shift, res_irq);
}

/* rdm */
void rdm_cpu_id_set(struct aq_hw_s *aq_hw, u32 cpuid, u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, rdm_dcadcpuid_adr(dca),
			    rdm_dcadcpuid_msk,
			    rdm_dcadcpuid_shift, cpuid);
}

void rdm_rx_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_dca_en)
{
	aq_hw_write_reg_bit(aq_hw, rdm_dca_en_adr, rdm_dca_en_msk,
			    rdm_dca_en_shift, rx_dca_en);
}

void rdm_rx_dca_mode_set(struct aq_hw_s *aq_hw, u32 rx_dca_mode)
{
	aq_hw_write_reg_bit(aq_hw, rdm_dca_mode_adr, rdm_dca_mode_msk,
			    rdm_dca_mode_shift, rx_dca_mode);
}

void rdm_rx_desc_data_buff_size_set(struct aq_hw_s *aq_hw,
				    u32 rx_desc_data_buff_size, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, rdm_descddata_size_adr(descriptor),
			    rdm_descddata_size_msk,
			    rdm_descddata_size_shift,
			    rx_desc_data_buff_size);
}

void rdm_rx_desc_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_desc_dca_en, u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, rdm_dcaddesc_en_adr(dca),
			    rdm_dcaddesc_en_msk,
			    rdm_dcaddesc_en_shift,
			    rx_desc_dca_en);
}

void rdm_rx_desc_en_set(struct aq_hw_s *aq_hw, u32 rx_desc_en, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, rdm_descden_adr(descriptor),
			    rdm_descden_msk,
			    rdm_descden_shift,
			    rx_desc_en);
}

void rdm_rx_desc_head_buff_size_set(struct aq_hw_s *aq_hw,
				    u32 rx_desc_head_buff_size, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, rdm_descdhdr_size_adr(descriptor),
			    rdm_descdhdr_size_msk,
			    rdm_descdhdr_size_shift,
			    rx_desc_head_buff_size);
}

void rdm_rx_desc_head_splitting_set(struct aq_hw_s *aq_hw,
				    u32 rx_desc_head_splitting, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, rdm_descdhdr_split_adr(descriptor),
			    rdm_descdhdr_split_msk,
			    rdm_descdhdr_split_shift,
			    rx_desc_head_splitting);
}

u32 rdm_rx_desc_head_ptr_get(struct aq_hw_s *aq_hw, u32 descriptor)
{
	return aq_hw_read_reg_bit(aq_hw, rdm_descdhd_adr(descriptor),
				  rdm_descdhd_msk, rdm_descdhd_shift);
}

void rdm_rx_desc_len_set(struct aq_hw_s *aq_hw, u32 rx_desc_len, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, rdm_descdlen_adr(descriptor),
			    rdm_descdlen_msk, rdm_descdlen_shift,
			    rx_desc_len);
}

void rdm_rx_desc_res_set(struct aq_hw_s *aq_hw, u32 rx_desc_res, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, rdm_descdreset_adr(descriptor),
			    rdm_descdreset_msk, rdm_descdreset_shift,
			    rx_desc_res);
}

void rdm_rx_desc_wr_wb_irq_en_set(struct aq_hw_s *aq_hw,
				  u32 rx_desc_wr_wb_irq_en)
{
	aq_hw_write_reg_bit(aq_hw, rdm_int_desc_wrb_en_adr,
			    rdm_int_desc_wrb_en_msk,
			    rdm_int_desc_wrb_en_shift,
			    rx_desc_wr_wb_irq_en);
}

void rdm_rx_head_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_head_dca_en, u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, rdm_dcadhdr_en_adr(dca),
			    rdm_dcadhdr_en_msk,
			    rdm_dcadhdr_en_shift,
			    rx_head_dca_en);
}

void rdm_rx_pld_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_pld_dca_en, u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, rdm_dcadpay_en_adr(dca),
			    rdm_dcadpay_en_msk, rdm_dcadpay_en_shift,
			    rx_pld_dca_en);
}

void rdm_rdm_intr_moder_en_set(struct aq_hw_s *aq_hw, u32 rdm_intr_moder_en)
{
	aq_hw_write_reg_bit(aq_hw, rdm_int_rim_en_adr,
			    rdm_int_rim_en_msk,
			    rdm_int_rim_en_shift,
			    rdm_intr_moder_en);
}

/* reg */
void reg_gen_irq_map_set(struct aq_hw_s *aq_hw, u32 gen_intr_map, u32 regidx)
{
	aq_hw_write_reg(aq_hw, gen_intr_map_adr(regidx), gen_intr_map);
}

u32 reg_gen_irq_status_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, gen_intr_stat_adr);
}

void reg_irq_glb_ctl_set(struct aq_hw_s *aq_hw, u32 intr_glb_ctl)
{
	aq_hw_write_reg(aq_hw, intr_glb_ctl_adr, intr_glb_ctl);
}

void reg_irq_thr_set(struct aq_hw_s *aq_hw, u32 intr_thr, u32 throttle)
{
	aq_hw_write_reg(aq_hw, intr_thr_adr(throttle), intr_thr);
}

void reg_rx_dma_desc_base_addresslswset(struct aq_hw_s *aq_hw,
					u32 rx_dma_desc_base_addrlsw,
					u32 descriptor)
{
	aq_hw_write_reg(aq_hw, rx_dma_desc_base_addrlsw_adr(descriptor),
			rx_dma_desc_base_addrlsw);
}

void reg_rx_dma_desc_base_addressmswset(struct aq_hw_s *aq_hw,
					u32 rx_dma_desc_base_addrmsw,
					u32 descriptor)
{
	aq_hw_write_reg(aq_hw, rx_dma_desc_base_addrmsw_adr(descriptor),
			rx_dma_desc_base_addrmsw);
}

u32 reg_rx_dma_desc_status_get(struct aq_hw_s *aq_hw, u32 descriptor)
{
	return aq_hw_read_reg(aq_hw, rx_dma_desc_stat_adr(descriptor));
}

void reg_rx_dma_desc_tail_ptr_set(struct aq_hw_s *aq_hw,
				  u32 rx_dma_desc_tail_ptr, u32 descriptor)
{
	aq_hw_write_reg(aq_hw, rx_dma_desc_tail_ptr_adr(descriptor),
			rx_dma_desc_tail_ptr);
}

void reg_rx_flr_mcst_flr_msk_set(struct aq_hw_s *aq_hw, u32 rx_flr_mcst_flr_msk)
{
	aq_hw_write_reg(aq_hw, rx_flr_mcst_flr_msk_adr, rx_flr_mcst_flr_msk);
}

void reg_rx_flr_mcst_flr_set(struct aq_hw_s *aq_hw, u32 rx_flr_mcst_flr,
			     u32 filter)
{
	aq_hw_write_reg(aq_hw, rx_flr_mcst_flr_adr(filter), rx_flr_mcst_flr);
}

void reg_rx_flr_rss_control1set(struct aq_hw_s *aq_hw, u32 rx_flr_rss_control1)
{
	aq_hw_write_reg(aq_hw, rx_flr_rss_control1_adr, rx_flr_rss_control1);
}

void reg_rx_flr_control2_set(struct aq_hw_s *aq_hw, u32 rx_filter_control2)
{
	aq_hw_write_reg(aq_hw, rx_flr_control2_adr, rx_filter_control2);
}

void reg_rx_intr_moder_ctrl_set(struct aq_hw_s *aq_hw,
				u32 rx_intr_moderation_ctl,
				u32 queue)
{
	aq_hw_write_reg(aq_hw, rx_intr_moderation_ctl_adr(queue),
			rx_intr_moderation_ctl);
}

void reg_tx_dma_debug_ctl_set(struct aq_hw_s *aq_hw, u32 tx_dma_debug_ctl)
{
	aq_hw_write_reg(aq_hw, tx_dma_debug_ctl_adr, tx_dma_debug_ctl);
}

void reg_tx_dma_desc_base_addresslswset(struct aq_hw_s *aq_hw,
					u32 tx_dma_desc_base_addrlsw,
					u32 descriptor)
{
	aq_hw_write_reg(aq_hw, tx_dma_desc_base_addrlsw_adr(descriptor),
			tx_dma_desc_base_addrlsw);
}

void reg_tx_dma_desc_base_addressmswset(struct aq_hw_s *aq_hw,
					u32 tx_dma_desc_base_addrmsw,
					u32 descriptor)
{
	aq_hw_write_reg(aq_hw, tx_dma_desc_base_addrmsw_adr(descriptor),
			tx_dma_desc_base_addrmsw);
}

void reg_tx_dma_desc_tail_ptr_set(struct aq_hw_s *aq_hw,
				  u32 tx_dma_desc_tail_ptr, u32 descriptor)
{
	aq_hw_write_reg(aq_hw, tx_dma_desc_tail_ptr_adr(descriptor),
			tx_dma_desc_tail_ptr);
}

void reg_tx_intr_moder_ctrl_set(struct aq_hw_s *aq_hw,
				u32 tx_intr_moderation_ctl,
				u32 queue)
{
	aq_hw_write_reg(aq_hw, tx_intr_moderation_ctl_adr(queue),
			tx_intr_moderation_ctl);
}

/* RPB: rx packet buffer */
void rpb_dma_sys_lbk_set(struct aq_hw_s *aq_hw, u32 dma_sys_lbk)
{
	aq_hw_write_reg_bit(aq_hw, rpb_dma_sys_lbk_adr,
			    rpb_dma_sys_lbk_msk,
			    rpb_dma_sys_lbk_shift, dma_sys_lbk);
}

void rpb_rpf_rx_traf_class_mode_set(struct aq_hw_s *aq_hw,
				    u32 rx_traf_class_mode)
{
	aq_hw_write_reg_bit(aq_hw, rpb_rpf_rx_tc_mode_adr,
			    rpb_rpf_rx_tc_mode_msk,
			    rpb_rpf_rx_tc_mode_shift,
			    rx_traf_class_mode);
}

void rpb_rx_buff_en_set(struct aq_hw_s *aq_hw, u32 rx_buff_en)
{
	aq_hw_write_reg_bit(aq_hw, rpb_rx_buf_en_adr, rpb_rx_buf_en_msk,
			    rpb_rx_buf_en_shift, rx_buff_en);
}

void rpb_rx_buff_hi_threshold_per_tc_set(struct aq_hw_s *aq_hw,
					 u32 rx_buff_hi_threshold_per_tc,
					 u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, rpb_rxbhi_thresh_adr(buffer),
			    rpb_rxbhi_thresh_msk, rpb_rxbhi_thresh_shift,
			    rx_buff_hi_threshold_per_tc);
}

void rpb_rx_buff_lo_threshold_per_tc_set(struct aq_hw_s *aq_hw,
					 u32 rx_buff_lo_threshold_per_tc,
					 u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, rpb_rxblo_thresh_adr(buffer),
			    rpb_rxblo_thresh_msk,
			    rpb_rxblo_thresh_shift,
			    rx_buff_lo_threshold_per_tc);
}

void rpb_rx_flow_ctl_mode_set(struct aq_hw_s *aq_hw, u32 rx_flow_ctl_mode)
{
	aq_hw_write_reg_bit(aq_hw, rpb_rx_fc_mode_adr,
			    rpb_rx_fc_mode_msk,
			    rpb_rx_fc_mode_shift, rx_flow_ctl_mode);
}

void rpb_rx_pkt_buff_size_per_tc_set(struct aq_hw_s *aq_hw,
				     u32 rx_pkt_buff_size_per_tc, u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, rpb_rxbbuf_size_adr(buffer),
			    rpb_rxbbuf_size_msk, rpb_rxbbuf_size_shift,
			    rx_pkt_buff_size_per_tc);
}

void rpb_rx_xoff_en_per_tc_set(struct aq_hw_s *aq_hw, u32 rx_xoff_en_per_tc,
			       u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, rpb_rxbxoff_en_adr(buffer),
			    rpb_rxbxoff_en_msk, rpb_rxbxoff_en_shift,
			    rx_xoff_en_per_tc);
}

/* rpf */

void rpfl2broadcast_count_threshold_set(struct aq_hw_s *aq_hw,
					u32 l2broadcast_count_threshold)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2bc_thresh_adr,
			    rpfl2bc_thresh_msk,
			    rpfl2bc_thresh_shift,
			    l2broadcast_count_threshold);
}

void rpfl2broadcast_en_set(struct aq_hw_s *aq_hw, u32 l2broadcast_en)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2bc_en_adr, rpfl2bc_en_msk,
			    rpfl2bc_en_shift, l2broadcast_en);
}

void rpfl2broadcast_flr_act_set(struct aq_hw_s *aq_hw, u32 l2broadcast_flr_act)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2bc_act_adr, rpfl2bc_act_msk,
			    rpfl2bc_act_shift, l2broadcast_flr_act);
}

void rpfl2multicast_flr_en_set(struct aq_hw_s *aq_hw, u32 l2multicast_flr_en,
			       u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2mc_enf_adr(filter),
			    rpfl2mc_enf_msk,
			    rpfl2mc_enf_shift, l2multicast_flr_en);
}

void rpfl2promiscuous_mode_en_set(struct aq_hw_s *aq_hw,
				  u32 l2promiscuous_mode_en)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2promis_mode_adr,
			    rpfl2promis_mode_msk,
			    rpfl2promis_mode_shift,
			    l2promiscuous_mode_en);
}

void rpfl2unicast_flr_act_set(struct aq_hw_s *aq_hw, u32 l2unicast_flr_act,
			      u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2uc_actf_adr(filter),
			    rpfl2uc_actf_msk, rpfl2uc_actf_shift,
			    l2unicast_flr_act);
}

void rpfl2_uc_flr_en_set(struct aq_hw_s *aq_hw, u32 l2unicast_flr_en,
			 u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2uc_enf_adr(filter),
			    rpfl2uc_enf_msk,
			    rpfl2uc_enf_shift, l2unicast_flr_en);
}

void rpfl2unicast_dest_addresslsw_set(struct aq_hw_s *aq_hw,
				      u32 l2unicast_dest_addresslsw,
				      u32 filter)
{
	aq_hw_write_reg(aq_hw, rpfl2uc_daflsw_adr(filter),
			l2unicast_dest_addresslsw);
}

void rpfl2unicast_dest_addressmsw_set(struct aq_hw_s *aq_hw,
				      u32 l2unicast_dest_addressmsw,
				      u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2uc_dafmsw_adr(filter),
			    rpfl2uc_dafmsw_msk, rpfl2uc_dafmsw_shift,
			    l2unicast_dest_addressmsw);
}

void rpfl2_accept_all_mc_packets_set(struct aq_hw_s *aq_hw,
				     u32 l2_accept_all_mc_packets)
{
	aq_hw_write_reg_bit(aq_hw, rpfl2mc_accept_all_adr,
			    rpfl2mc_accept_all_msk,
			    rpfl2mc_accept_all_shift,
			    l2_accept_all_mc_packets);
}

void rpf_rpb_user_priority_tc_map_set(struct aq_hw_s *aq_hw,
				      u32 user_priority_tc_map, u32 tc)
{
/* register address for bitfield rx_tc_up{t}[2:0] */
	static u32 rpf_rpb_rx_tc_upt_adr[8] = {
			0x000054c4U, 0x000054c4U, 0x000054c4U, 0x000054c4U,
			0x000054c4U, 0x000054c4U, 0x000054c4U, 0x000054c4U
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

void rpf_rss_key_addr_set(struct aq_hw_s *aq_hw, u32 rss_key_addr)
{
	aq_hw_write_reg_bit(aq_hw, rpf_rss_key_addr_adr,
			    rpf_rss_key_addr_msk,
			    rpf_rss_key_addr_shift,
			    rss_key_addr);
}

void rpf_rss_key_wr_data_set(struct aq_hw_s *aq_hw, u32 rss_key_wr_data)
{
	aq_hw_write_reg(aq_hw, rpf_rss_key_wr_data_adr,
			rss_key_wr_data);
}

u32 rpf_rss_key_wr_en_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, rpf_rss_key_wr_eni_adr,
				  rpf_rss_key_wr_eni_msk,
				  rpf_rss_key_wr_eni_shift);
}

void rpf_rss_key_wr_en_set(struct aq_hw_s *aq_hw, u32 rss_key_wr_en)
{
	aq_hw_write_reg_bit(aq_hw, rpf_rss_key_wr_eni_adr,
			    rpf_rss_key_wr_eni_msk,
			    rpf_rss_key_wr_eni_shift,
			    rss_key_wr_en);
}

void rpf_rss_redir_tbl_addr_set(struct aq_hw_s *aq_hw, u32 rss_redir_tbl_addr)
{
	aq_hw_write_reg_bit(aq_hw, rpf_rss_redir_addr_adr,
			    rpf_rss_redir_addr_msk,
			    rpf_rss_redir_addr_shift, rss_redir_tbl_addr);
}

void rpf_rss_redir_tbl_wr_data_set(struct aq_hw_s *aq_hw,
				   u32 rss_redir_tbl_wr_data)
{
	aq_hw_write_reg_bit(aq_hw, rpf_rss_redir_wr_data_adr,
			    rpf_rss_redir_wr_data_msk,
			    rpf_rss_redir_wr_data_shift,
			    rss_redir_tbl_wr_data);
}

u32 rpf_rss_redir_wr_en_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, rpf_rss_redir_wr_eni_adr,
				  rpf_rss_redir_wr_eni_msk,
				  rpf_rss_redir_wr_eni_shift);
}

void rpf_rss_redir_wr_en_set(struct aq_hw_s *aq_hw, u32 rss_redir_wr_en)
{
	aq_hw_write_reg_bit(aq_hw, rpf_rss_redir_wr_eni_adr,
			    rpf_rss_redir_wr_eni_msk,
			    rpf_rss_redir_wr_eni_shift, rss_redir_wr_en);
}

void rpf_tpo_to_rpf_sys_lbk_set(struct aq_hw_s *aq_hw, u32 tpo_to_rpf_sys_lbk)
{
	aq_hw_write_reg_bit(aq_hw, rpf_tpo_rpf_sys_lbk_adr,
			    rpf_tpo_rpf_sys_lbk_msk,
			    rpf_tpo_rpf_sys_lbk_shift,
			    tpo_to_rpf_sys_lbk);
}

void rpf_vlan_inner_etht_set(struct aq_hw_s *aq_hw, u32 vlan_inner_etht)
{
	aq_hw_write_reg_bit(aq_hw, rpf_vl_inner_tpid_adr,
			    rpf_vl_inner_tpid_msk,
			    rpf_vl_inner_tpid_shift,
			    vlan_inner_etht);
}

void rpf_vlan_outer_etht_set(struct aq_hw_s *aq_hw, u32 vlan_outer_etht)
{
	aq_hw_write_reg_bit(aq_hw, rpf_vl_outer_tpid_adr,
			    rpf_vl_outer_tpid_msk,
			    rpf_vl_outer_tpid_shift,
			    vlan_outer_etht);
}

void rpf_vlan_prom_mode_en_set(struct aq_hw_s *aq_hw, u32 vlan_prom_mode_en)
{
	aq_hw_write_reg_bit(aq_hw, rpf_vl_promis_mode_adr,
			    rpf_vl_promis_mode_msk,
			    rpf_vl_promis_mode_shift,
			    vlan_prom_mode_en);
}

void rpf_vlan_accept_untagged_packets_set(struct aq_hw_s *aq_hw,
					  u32 vlan_accept_untagged_packets)
{
	aq_hw_write_reg_bit(aq_hw, rpf_vl_accept_untagged_mode_adr,
			    rpf_vl_accept_untagged_mode_msk,
			    rpf_vl_accept_untagged_mode_shift,
			    vlan_accept_untagged_packets);
}

void rpf_vlan_untagged_act_set(struct aq_hw_s *aq_hw, u32 vlan_untagged_act)
{
	aq_hw_write_reg_bit(aq_hw, rpf_vl_untagged_act_adr,
			    rpf_vl_untagged_act_msk,
			    rpf_vl_untagged_act_shift,
			    vlan_untagged_act);
}

void rpf_vlan_flr_en_set(struct aq_hw_s *aq_hw, u32 vlan_flr_en, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_vl_en_f_adr(filter),
			    rpf_vl_en_f_msk,
			    rpf_vl_en_f_shift,
			    vlan_flr_en);
}

void rpf_vlan_flr_act_set(struct aq_hw_s *aq_hw, u32 vlan_flr_act, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_vl_act_f_adr(filter),
			    rpf_vl_act_f_msk,
			    rpf_vl_act_f_shift,
			    vlan_flr_act);
}

void rpf_vlan_id_flr_set(struct aq_hw_s *aq_hw, u32 vlan_id_flr, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_vl_id_f_adr(filter),
			    rpf_vl_id_f_msk,
			    rpf_vl_id_f_shift,
			    vlan_id_flr);
}

void rpf_etht_flr_en_set(struct aq_hw_s *aq_hw, u32 etht_flr_en, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_et_enf_adr(filter),
			    rpf_et_enf_msk,
			    rpf_et_enf_shift, etht_flr_en);
}

void rpf_etht_user_priority_en_set(struct aq_hw_s *aq_hw,
				   u32 etht_user_priority_en, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_et_upfen_adr(filter),
			    rpf_et_upfen_msk, rpf_et_upfen_shift,
			    etht_user_priority_en);
}

void rpf_etht_rx_queue_en_set(struct aq_hw_s *aq_hw, u32 etht_rx_queue_en,
			      u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_et_rxqfen_adr(filter),
			    rpf_et_rxqfen_msk, rpf_et_rxqfen_shift,
			    etht_rx_queue_en);
}

void rpf_etht_user_priority_set(struct aq_hw_s *aq_hw, u32 etht_user_priority,
				u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_et_upf_adr(filter),
			    rpf_et_upf_msk,
			    rpf_et_upf_shift, etht_user_priority);
}

void rpf_etht_rx_queue_set(struct aq_hw_s *aq_hw, u32 etht_rx_queue,
			   u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_et_rxqf_adr(filter),
			    rpf_et_rxqf_msk,
			    rpf_et_rxqf_shift, etht_rx_queue);
}

void rpf_etht_mgt_queue_set(struct aq_hw_s *aq_hw, u32 etht_mgt_queue,
			    u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_et_mng_rxqf_adr(filter),
			    rpf_et_mng_rxqf_msk, rpf_et_mng_rxqf_shift,
			    etht_mgt_queue);
}

void rpf_etht_flr_act_set(struct aq_hw_s *aq_hw, u32 etht_flr_act, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_et_actf_adr(filter),
			    rpf_et_actf_msk,
			    rpf_et_actf_shift, etht_flr_act);
}

void rpf_etht_flr_set(struct aq_hw_s *aq_hw, u32 etht_flr, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, rpf_et_valf_adr(filter),
			    rpf_et_valf_msk,
			    rpf_et_valf_shift, etht_flr);
}

/* RPO: rx packet offload */
void rpo_ipv4header_crc_offload_en_set(struct aq_hw_s *aq_hw,
				       u32 ipv4header_crc_offload_en)
{
	aq_hw_write_reg_bit(aq_hw, rpo_ipv4chk_en_adr,
			    rpo_ipv4chk_en_msk,
			    rpo_ipv4chk_en_shift,
			    ipv4header_crc_offload_en);
}

void rpo_rx_desc_vlan_stripping_set(struct aq_hw_s *aq_hw,
				    u32 rx_desc_vlan_stripping, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, rpo_descdvl_strip_adr(descriptor),
			    rpo_descdvl_strip_msk,
			    rpo_descdvl_strip_shift,
			    rx_desc_vlan_stripping);
}

void rpo_tcp_udp_crc_offload_en_set(struct aq_hw_s *aq_hw,
				    u32 tcp_udp_crc_offload_en)
{
	aq_hw_write_reg_bit(aq_hw, rpol4chk_en_adr, rpol4chk_en_msk,
			    rpol4chk_en_shift, tcp_udp_crc_offload_en);
}

void rpo_lro_en_set(struct aq_hw_s *aq_hw, u32 lro_en)
{
	aq_hw_write_reg(aq_hw, rpo_lro_en_adr, lro_en);
}

void rpo_lro_patch_optimization_en_set(struct aq_hw_s *aq_hw,
				       u32 lro_patch_optimization_en)
{
	aq_hw_write_reg_bit(aq_hw, rpo_lro_ptopt_en_adr,
			    rpo_lro_ptopt_en_msk,
			    rpo_lro_ptopt_en_shift,
			    lro_patch_optimization_en);
}

void rpo_lro_qsessions_lim_set(struct aq_hw_s *aq_hw,
			       u32 lro_qsessions_lim)
{
	aq_hw_write_reg_bit(aq_hw, rpo_lro_qses_lmt_adr,
			    rpo_lro_qses_lmt_msk,
			    rpo_lro_qses_lmt_shift,
			    lro_qsessions_lim);
}

void rpo_lro_total_desc_lim_set(struct aq_hw_s *aq_hw, u32 lro_total_desc_lim)
{
	aq_hw_write_reg_bit(aq_hw, rpo_lro_tot_dsc_lmt_adr,
			    rpo_lro_tot_dsc_lmt_msk,
			    rpo_lro_tot_dsc_lmt_shift,
			    lro_total_desc_lim);
}

void rpo_lro_min_pay_of_first_pkt_set(struct aq_hw_s *aq_hw,
				      u32 lro_min_pld_of_first_pkt)
{
	aq_hw_write_reg_bit(aq_hw, rpo_lro_pkt_min_adr,
			    rpo_lro_pkt_min_msk,
			    rpo_lro_pkt_min_shift,
			    lro_min_pld_of_first_pkt);
}

void rpo_lro_pkt_lim_set(struct aq_hw_s *aq_hw, u32 lro_pkt_lim)
{
	aq_hw_write_reg(aq_hw, rpo_lro_rsc_max_adr, lro_pkt_lim);
}

void rpo_lro_max_num_of_descriptors_set(struct aq_hw_s *aq_hw,
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

void rpo_lro_time_base_divider_set(struct aq_hw_s *aq_hw,
				   u32 lro_time_base_divider)
{
	aq_hw_write_reg_bit(aq_hw, rpo_lro_tb_div_adr,
			    rpo_lro_tb_div_msk,
			    rpo_lro_tb_div_shift,
			    lro_time_base_divider);
}

void rpo_lro_inactive_interval_set(struct aq_hw_s *aq_hw,
				   u32 lro_inactive_interval)
{
	aq_hw_write_reg_bit(aq_hw, rpo_lro_ina_ival_adr,
			    rpo_lro_ina_ival_msk,
			    rpo_lro_ina_ival_shift,
			    lro_inactive_interval);
}

void rpo_lro_max_coalescing_interval_set(struct aq_hw_s *aq_hw,
					 u32 lro_max_coalescing_interval)
{
	aq_hw_write_reg_bit(aq_hw, rpo_lro_max_ival_adr,
			    rpo_lro_max_ival_msk,
			    rpo_lro_max_ival_shift,
			    lro_max_coalescing_interval);
}

/* rx */
void rx_rx_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 rx_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, rx_reg_res_dsbl_adr,
			    rx_reg_res_dsbl_msk,
			    rx_reg_res_dsbl_shift,
			    rx_reg_res_dis);
}

/* tdm */
void tdm_cpu_id_set(struct aq_hw_s *aq_hw, u32 cpuid, u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, tdm_dcadcpuid_adr(dca),
			    tdm_dcadcpuid_msk,
			    tdm_dcadcpuid_shift, cpuid);
}

void tdm_large_send_offload_en_set(struct aq_hw_s *aq_hw,
				   u32 large_send_offload_en)
{
	aq_hw_write_reg(aq_hw, tdm_lso_en_adr, large_send_offload_en);
}

void tdm_tx_dca_en_set(struct aq_hw_s *aq_hw, u32 tx_dca_en)
{
	aq_hw_write_reg_bit(aq_hw, tdm_dca_en_adr, tdm_dca_en_msk,
			    tdm_dca_en_shift, tx_dca_en);
}

void tdm_tx_dca_mode_set(struct aq_hw_s *aq_hw, u32 tx_dca_mode)
{
	aq_hw_write_reg_bit(aq_hw, tdm_dca_mode_adr, tdm_dca_mode_msk,
			    tdm_dca_mode_shift, tx_dca_mode);
}

void tdm_tx_desc_dca_en_set(struct aq_hw_s *aq_hw, u32 tx_desc_dca_en, u32 dca)
{
	aq_hw_write_reg_bit(aq_hw, tdm_dcaddesc_en_adr(dca),
			    tdm_dcaddesc_en_msk, tdm_dcaddesc_en_shift,
			    tx_desc_dca_en);
}

void tdm_tx_desc_en_set(struct aq_hw_s *aq_hw, u32 tx_desc_en, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, tdm_descden_adr(descriptor),
			    tdm_descden_msk,
			    tdm_descden_shift,
			    tx_desc_en);
}

u32 tdm_tx_desc_head_ptr_get(struct aq_hw_s *aq_hw, u32 descriptor)
{
	return aq_hw_read_reg_bit(aq_hw, tdm_descdhd_adr(descriptor),
				  tdm_descdhd_msk, tdm_descdhd_shift);
}

void tdm_tx_desc_len_set(struct aq_hw_s *aq_hw, u32 tx_desc_len,
			 u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, tdm_descdlen_adr(descriptor),
			    tdm_descdlen_msk,
			    tdm_descdlen_shift,
			    tx_desc_len);
}

void tdm_tx_desc_wr_wb_irq_en_set(struct aq_hw_s *aq_hw,
				  u32 tx_desc_wr_wb_irq_en)
{
	aq_hw_write_reg_bit(aq_hw, tdm_int_desc_wrb_en_adr,
			    tdm_int_desc_wrb_en_msk,
			    tdm_int_desc_wrb_en_shift,
			    tx_desc_wr_wb_irq_en);
}

void tdm_tx_desc_wr_wb_threshold_set(struct aq_hw_s *aq_hw,
				     u32 tx_desc_wr_wb_threshold,
				     u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, tdm_descdwrb_thresh_adr(descriptor),
			    tdm_descdwrb_thresh_msk,
			    tdm_descdwrb_thresh_shift,
			    tx_desc_wr_wb_threshold);
}

void tdm_tdm_intr_moder_en_set(struct aq_hw_s *aq_hw,
			       u32 tdm_irq_moderation_en)
{
	aq_hw_write_reg_bit(aq_hw, tdm_int_mod_en_adr,
			    tdm_int_mod_en_msk,
			    tdm_int_mod_en_shift,
			    tdm_irq_moderation_en);
}

/* thm */
void thm_lso_tcp_flag_of_first_pkt_set(struct aq_hw_s *aq_hw,
				       u32 lso_tcp_flag_of_first_pkt)
{
	aq_hw_write_reg_bit(aq_hw, thm_lso_tcp_flag_first_adr,
			    thm_lso_tcp_flag_first_msk,
			    thm_lso_tcp_flag_first_shift,
			    lso_tcp_flag_of_first_pkt);
}

void thm_lso_tcp_flag_of_last_pkt_set(struct aq_hw_s *aq_hw,
				      u32 lso_tcp_flag_of_last_pkt)
{
	aq_hw_write_reg_bit(aq_hw, thm_lso_tcp_flag_last_adr,
			    thm_lso_tcp_flag_last_msk,
			    thm_lso_tcp_flag_last_shift,
			    lso_tcp_flag_of_last_pkt);
}

void thm_lso_tcp_flag_of_middle_pkt_set(struct aq_hw_s *aq_hw,
					u32 lso_tcp_flag_of_middle_pkt)
{
	aq_hw_write_reg_bit(aq_hw, thm_lso_tcp_flag_mid_adr,
			    thm_lso_tcp_flag_mid_msk,
			    thm_lso_tcp_flag_mid_shift,
			    lso_tcp_flag_of_middle_pkt);
}

/* TPB: tx packet buffer */
void tpb_tx_buff_en_set(struct aq_hw_s *aq_hw, u32 tx_buff_en)
{
	aq_hw_write_reg_bit(aq_hw, tpb_tx_buf_en_adr, tpb_tx_buf_en_msk,
			    tpb_tx_buf_en_shift, tx_buff_en);
}

void tpb_tx_buff_hi_threshold_per_tc_set(struct aq_hw_s *aq_hw,
					 u32 tx_buff_hi_threshold_per_tc,
					 u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, tpb_txbhi_thresh_adr(buffer),
			    tpb_txbhi_thresh_msk, tpb_txbhi_thresh_shift,
			    tx_buff_hi_threshold_per_tc);
}

void tpb_tx_buff_lo_threshold_per_tc_set(struct aq_hw_s *aq_hw,
					 u32 tx_buff_lo_threshold_per_tc,
					 u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, tpb_txblo_thresh_adr(buffer),
			    tpb_txblo_thresh_msk, tpb_txblo_thresh_shift,
			    tx_buff_lo_threshold_per_tc);
}

void tpb_tx_dma_sys_lbk_en_set(struct aq_hw_s *aq_hw, u32 tx_dma_sys_lbk_en)
{
	aq_hw_write_reg_bit(aq_hw, tpb_dma_sys_lbk_adr,
			    tpb_dma_sys_lbk_msk,
			    tpb_dma_sys_lbk_shift,
			    tx_dma_sys_lbk_en);
}

void tpb_tx_pkt_buff_size_per_tc_set(struct aq_hw_s *aq_hw,
				     u32 tx_pkt_buff_size_per_tc, u32 buffer)
{
	aq_hw_write_reg_bit(aq_hw, tpb_txbbuf_size_adr(buffer),
			    tpb_txbbuf_size_msk,
			    tpb_txbbuf_size_shift,
			    tx_pkt_buff_size_per_tc);
}

void tpb_tx_path_scp_ins_en_set(struct aq_hw_s *aq_hw, u32 tx_path_scp_ins_en)
{
	aq_hw_write_reg_bit(aq_hw, tpb_tx_scp_ins_en_adr,
			    tpb_tx_scp_ins_en_msk,
			    tpb_tx_scp_ins_en_shift,
			    tx_path_scp_ins_en);
}

/* TPO: tx packet offload */
void tpo_ipv4header_crc_offload_en_set(struct aq_hw_s *aq_hw,
				       u32 ipv4header_crc_offload_en)
{
	aq_hw_write_reg_bit(aq_hw, tpo_ipv4chk_en_adr,
			    tpo_ipv4chk_en_msk,
			    tpo_ipv4chk_en_shift,
			    ipv4header_crc_offload_en);
}

void tpo_tcp_udp_crc_offload_en_set(struct aq_hw_s *aq_hw,
				    u32 tcp_udp_crc_offload_en)
{
	aq_hw_write_reg_bit(aq_hw, tpol4chk_en_adr,
			    tpol4chk_en_msk,
			    tpol4chk_en_shift,
			    tcp_udp_crc_offload_en);
}

void tpo_tx_pkt_sys_lbk_en_set(struct aq_hw_s *aq_hw, u32 tx_pkt_sys_lbk_en)
{
	aq_hw_write_reg_bit(aq_hw, tpo_pkt_sys_lbk_adr,
			    tpo_pkt_sys_lbk_msk,
			    tpo_pkt_sys_lbk_shift,
			    tx_pkt_sys_lbk_en);
}

/* TPS: tx packet scheduler */
void tps_tx_pkt_shed_data_arb_mode_set(struct aq_hw_s *aq_hw,
				       u32 tx_pkt_shed_data_arb_mode)
{
	aq_hw_write_reg_bit(aq_hw, tps_data_tc_arb_mode_adr,
			    tps_data_tc_arb_mode_msk,
			    tps_data_tc_arb_mode_shift,
			    tx_pkt_shed_data_arb_mode);
}

void tps_tx_pkt_shed_desc_rate_curr_time_res_set(struct aq_hw_s *aq_hw,
						 u32 curr_time_res)
{
	aq_hw_write_reg_bit(aq_hw, tps_desc_rate_ta_rst_adr,
			    tps_desc_rate_ta_rst_msk,
			    tps_desc_rate_ta_rst_shift,
			    curr_time_res);
}

void tps_tx_pkt_shed_desc_rate_lim_set(struct aq_hw_s *aq_hw,
				       u32 tx_pkt_shed_desc_rate_lim)
{
	aq_hw_write_reg_bit(aq_hw, tps_desc_rate_lim_adr,
			    tps_desc_rate_lim_msk,
			    tps_desc_rate_lim_shift,
			    tx_pkt_shed_desc_rate_lim);
}

void tps_tx_pkt_shed_desc_tc_arb_mode_set(struct aq_hw_s *aq_hw,
					  u32 tx_pkt_shed_desc_tc_arb_mode)
{
	aq_hw_write_reg_bit(aq_hw, tps_desc_tc_arb_mode_adr,
			    tps_desc_tc_arb_mode_msk,
			    tps_desc_tc_arb_mode_shift,
			    tx_pkt_shed_desc_tc_arb_mode);
}

void tps_tx_pkt_shed_desc_tc_max_credit_set(struct aq_hw_s *aq_hw,
					    u32 tx_pkt_shed_desc_tc_max_credit,
					    u32 tc)
{
	aq_hw_write_reg_bit(aq_hw, tps_desc_tctcredit_max_adr(tc),
			    tps_desc_tctcredit_max_msk,
			    tps_desc_tctcredit_max_shift,
			    tx_pkt_shed_desc_tc_max_credit);
}

void tps_tx_pkt_shed_desc_tc_weight_set(struct aq_hw_s *aq_hw,
					u32 tx_pkt_shed_desc_tc_weight, u32 tc)
{
	aq_hw_write_reg_bit(aq_hw, tps_desc_tctweight_adr(tc),
			    tps_desc_tctweight_msk,
			    tps_desc_tctweight_shift,
			    tx_pkt_shed_desc_tc_weight);
}

void tps_tx_pkt_shed_desc_vm_arb_mode_set(struct aq_hw_s *aq_hw,
					  u32 tx_pkt_shed_desc_vm_arb_mode)
{
	aq_hw_write_reg_bit(aq_hw, tps_desc_vm_arb_mode_adr,
			    tps_desc_vm_arb_mode_msk,
			    tps_desc_vm_arb_mode_shift,
			    tx_pkt_shed_desc_vm_arb_mode);
}

void tps_tx_pkt_shed_tc_data_max_credit_set(struct aq_hw_s *aq_hw,
					    u32 tx_pkt_shed_tc_data_max_credit,
					    u32 tc)
{
	aq_hw_write_reg_bit(aq_hw, tps_data_tctcredit_max_adr(tc),
			    tps_data_tctcredit_max_msk,
			    tps_data_tctcredit_max_shift,
			    tx_pkt_shed_tc_data_max_credit);
}

void tps_tx_pkt_shed_tc_data_weight_set(struct aq_hw_s *aq_hw,
					u32 tx_pkt_shed_tc_data_weight, u32 tc)
{
	aq_hw_write_reg_bit(aq_hw, tps_data_tctweight_adr(tc),
			    tps_data_tctweight_msk,
			    tps_data_tctweight_shift,
			    tx_pkt_shed_tc_data_weight);
}

/* tx */
void tx_tx_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 tx_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, tx_reg_res_dsbl_adr,
			    tx_reg_res_dsbl_msk,
			    tx_reg_res_dsbl_shift, tx_reg_res_dis);
}

/* msm */
u32 msm_reg_access_status_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, msm_reg_access_busy_adr,
				  msm_reg_access_busy_msk,
				  msm_reg_access_busy_shift);
}

void msm_reg_addr_for_indirect_addr_set(struct aq_hw_s *aq_hw,
					u32 reg_addr_for_indirect_addr)
{
	aq_hw_write_reg_bit(aq_hw, msm_reg_addr_adr,
			    msm_reg_addr_msk,
			    msm_reg_addr_shift,
			    reg_addr_for_indirect_addr);
}

void msm_reg_rd_strobe_set(struct aq_hw_s *aq_hw, u32 reg_rd_strobe)
{
	aq_hw_write_reg_bit(aq_hw, msm_reg_rd_strobe_adr,
			    msm_reg_rd_strobe_msk,
			    msm_reg_rd_strobe_shift,
			    reg_rd_strobe);
}

u32 msm_reg_rd_data_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, msm_reg_rd_data_adr);
}

void msm_reg_wr_data_set(struct aq_hw_s *aq_hw, u32 reg_wr_data)
{
	aq_hw_write_reg(aq_hw, msm_reg_wr_data_adr, reg_wr_data);
}

void msm_reg_wr_strobe_set(struct aq_hw_s *aq_hw, u32 reg_wr_strobe)
{
	aq_hw_write_reg_bit(aq_hw, msm_reg_wr_strobe_adr,
			    msm_reg_wr_strobe_msk,
			    msm_reg_wr_strobe_shift,
			    reg_wr_strobe);
}

/* pci */
void pci_pci_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 pci_reg_res_dis)
{
	aq_hw_write_reg_bit(aq_hw, pci_reg_res_dsbl_adr,
			    pci_reg_res_dsbl_msk,
			    pci_reg_res_dsbl_shift,
			    pci_reg_res_dis);
}

void reg_glb_cpu_scratch_scp_set(struct aq_hw_s *aq_hw, u32 glb_cpu_scratch_scp,
				 u32 scratch_scp)
{
	aq_hw_write_reg(aq_hw, glb_cpu_scratch_scp_adr(scratch_scp),
			glb_cpu_scratch_scp);
}
