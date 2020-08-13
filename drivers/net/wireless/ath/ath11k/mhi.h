/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */
#ifndef _ATH11K_MHI_H
#define _ATH11K_MHI_H

#include "pci.h"

enum ath11k_mhi_state {
	ATH11K_MHI_INIT,
	ATH11K_MHI_DEINIT,
	ATH11K_MHI_POWER_ON,
	ATH11K_MHI_POWER_OFF,
	ATH11K_MHI_FORCE_POWER_OFF,
	ATH11K_MHI_SUSPEND,
	ATH11K_MHI_RESUME,
	ATH11K_MHI_TRIGGER_RDDM,
	ATH11K_MHI_RDDM,
	ATH11K_MHI_RDDM_DONE,
};

int ath11k_mhi_start(struct ath11k_pci *ar_pci);
void ath11k_mhi_stop(struct ath11k_pci *ar_pci);
int ath11k_mhi_register(struct ath11k_pci *ar_pci);
void ath11k_mhi_unregister(struct ath11k_pci *ar_pci);

#endif
