/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */
#ifndef ATH11K_AHB_H
#define ATH11K_AHB_H

#include "core.h"

#define ATH11K_AHB_RECOVERY_TIMEOUT (3 * HZ)
struct ath11k_base;

static inline u32 ath11k_ahb_read32(struct ath11k_base *ab, u32 offset)
{
	return ioread32(ab->mem + offset);
}

static inline void ath11k_ahb_write32(struct ath11k_base *ab, u32 offset, u32 value)
{
	iowrite32(value, ab->mem + offset);
}

void ath11k_ahb_ext_irq_enable(struct ath11k_base *ab);
void ath11k_ahb_ext_irq_disable(struct ath11k_base *ab);
int ath11k_ahb_start(struct ath11k_base *ab);
void ath11k_ahb_stop(struct ath11k_base *ab);
int ath11k_ahb_power_up(struct ath11k_base *ab);
void ath11k_ahb_power_down(struct ath11k_base *ab);
int ath11k_ahb_map_service_to_pipe(struct ath11k_base *ab, u16 service_id,
				   u8 *ul_pipe, u8 *dl_pipe);

int ath11k_ahb_init(void);
void ath11k_ahb_exit(void);

#endif
