/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _ATH11K_MHI_H
#define _ATH11K_MHI_H

#include "pci.h"

#define PCIE_TXVECDB				0x360
#define PCIE_TXVECSTATUS			0x368
#define PCIE_RXVECDB				0x394
#define PCIE_RXVECSTATUS			0x39C

#define MHISTATUS				0x48
#define MHICTRL					0x38
#define MHICTRL_RESET_MASK			0x2

int ath11k_mhi_start(struct ath11k_pci *ar_pci);
void ath11k_mhi_stop(struct ath11k_pci *ar_pci);
int ath11k_mhi_register(struct ath11k_pci *ar_pci);
void ath11k_mhi_unregister(struct ath11k_pci *ar_pci);
void ath11k_mhi_set_mhictrl_reset(struct ath11k_base *ab);
void ath11k_mhi_clear_vector(struct ath11k_base *ab);

int ath11k_mhi_suspend(struct ath11k_pci *ar_pci);
int ath11k_mhi_resume(struct ath11k_pci *ar_pci);

#endif
