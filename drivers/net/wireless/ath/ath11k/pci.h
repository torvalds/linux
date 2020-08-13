/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 */

#include "core.h"

struct ath11k_msi_user {
	char *name;
	int num_vectors;
	u32 base_vector;
};

struct ath11k_msi_config {
	int total_vectors;
	int total_users;
	struct ath11k_msi_user *users;
};

struct ath11k_pci {
	struct pci_dev *pdev;
	struct ath11k_base *ab;
	u16 dev_id;
	u32 msi_ep_base_data;
};

static inline struct ath11k_pci *ath11k_pci_priv(struct ath11k_base *ab)
{
	return (struct ath11k_pci *)ab->drv_priv;
}
