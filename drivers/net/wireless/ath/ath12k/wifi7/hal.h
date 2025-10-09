/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_HAL_WIFI7_H
#define ATH12K_HAL_WIFI7_H

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
#endif
