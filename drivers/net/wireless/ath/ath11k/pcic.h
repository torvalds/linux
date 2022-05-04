/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ATH11K_PCI_CMN_H
#define _ATH11K_PCI_CMN_H

#include "core.h"

#define ATH11K_PCI_IRQ_CE0_OFFSET	3
#define ATH11K_PCI_IRQ_DP_OFFSET	14

#define ATH11K_PCI_WINDOW_ENABLE_BIT		0x40000000
#define ATH11K_PCI_WINDOW_REG_ADDRESS		0x310c
#define ATH11K_PCI_WINDOW_VALUE_MASK		GENMASK(24, 19)
#define ATH11K_PCI_WINDOW_START			0x80000
#define ATH11K_PCI_WINDOW_RANGE_MASK		GENMASK(18, 0)

/* BAR0 + 4k is always accessible, and no
 * need to force wakeup.
 * 4K - 32 = 0xFE0
 */
#define ATH11K_PCI_ACCESS_ALWAYS_OFF 0xFE0

int ath11k_pcic_get_user_msi_assignment(struct ath11k_base *ab, char *user_name,
					int *num_vectors, u32 *user_base_data,
					u32 *base_vector);
void ath11k_pcic_write32(struct ath11k_base *ab, u32 offset, u32 value);
u32 ath11k_pcic_read32(struct ath11k_base *ab, u32 offset);
void ath11k_pcic_get_msi_address(struct ath11k_base *ab, u32 *msi_addr_lo,
				 u32 *msi_addr_hi);
void ath11k_pcic_get_ce_msi_idx(struct ath11k_base *ab, u32 ce_id, u32 *msi_idx);
void ath11k_pcic_free_irq(struct ath11k_base *ab);
int ath11k_pcic_config_irq(struct ath11k_base *ab);
void ath11k_pcic_ext_irq_enable(struct ath11k_base *ab);
void ath11k_pcic_ext_irq_disable(struct ath11k_base *ab);
void ath11k_pcic_stop(struct ath11k_base *ab);
int ath11k_pcic_start(struct ath11k_base *ab);
int ath11k_pcic_map_service_to_pipe(struct ath11k_base *ab, u16 service_id,
				    u8 *ul_pipe, u8 *dl_pipe);
void ath11k_pcic_ce_irqs_enable(struct ath11k_base *ab);
void ath11k_pcic_ce_irq_disable_sync(struct ath11k_base *ab);
int ath11k_pcic_init_msi_config(struct ath11k_base *ab);
#endif
