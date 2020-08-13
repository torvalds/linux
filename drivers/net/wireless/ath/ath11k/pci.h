/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 */

#include "core.h"

struct ath11k_pci {
	struct pci_dev *pdev;
	struct ath11k_base *ab;
	u16 dev_id;
};

static inline struct ath11k_pci *ath11k_pci_priv(struct ath11k_base *ab)
{
	return (struct ath11k_pci *)ab->drv_priv;
}
