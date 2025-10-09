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
#endif
