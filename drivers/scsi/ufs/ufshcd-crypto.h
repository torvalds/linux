/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef _UFSHCD_CRYPTO_H
#define _UFSHCD_CRYPTO_H

struct ufs_hba;

#ifdef CONFIG_SCSI_UFS_CRYPTO
#include <linux/keyslot-manager.h>

#include "ufshci.h"

#define NUM_KEYSLOTS(hba) (hba->crypto_capabilities.config_count + 1)

static inline bool ufshcd_keyslot_valid(struct ufs_hba *hba, unsigned int slot)
{
	/*
	 * The actual number of configurations supported is (CFGC+1), so slot
	 * numbers range from 0 to config_count inclusive.
	 */
	return slot < NUM_KEYSLOTS(hba);
}

static inline bool ufshcd_hba_is_crypto_supported(struct ufs_hba *hba)
{
	return hba->crypto_capabilities.reg_val != 0;
}

static inline bool ufshcd_is_crypto_enabled(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_CRYPTO;
}

void ufshcd_crypto_enable(struct ufs_hba *hba);

void ufshcd_crypto_disable(struct ufs_hba *hba);

int ufshcd_hba_init_crypto(struct ufs_hba *hba);

void ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
					    struct request_queue *q);

void ufshcd_crypto_destroy_rq_keyslot_manager(struct ufs_hba *hba,
					      struct request_queue *q);

#else /* CONFIG_SCSI_UFS_CRYPTO */

static inline bool ufshcd_keyslot_valid(struct ufs_hba *hba,
					unsigned int slot)
{
	return false;
}

static inline bool ufshcd_hba_is_crypto_supported(struct ufs_hba *hba)
{
	return false;
}

static inline bool ufshcd_is_crypto_enabled(struct ufs_hba *hba)
{
	return false;
}

static inline void ufshcd_crypto_enable(struct ufs_hba *hba) { }

static inline void ufshcd_crypto_disable(struct ufs_hba *hba) { }

static inline int ufshcd_hba_init_crypto(struct ufs_hba *hba)
{
	return 0;
}

static inline void ufshcd_crypto_setup_rq_keyslot_manager(
					struct ufs_hba *hba,
					struct request_queue *q) { }

static inline void ufshcd_crypto_destroy_rq_keyslot_manager(
				struct ufs_hba *hba,
				struct request_queue *q) { }

#endif /* CONFIG_SCSI_UFS_CRYPTO */

#endif /* _UFSHCD_CRYPTO_H */
