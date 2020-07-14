/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef _UFSHCD_CRYPTO_H
#define _UFSHCD_CRYPTO_H

#ifdef CONFIG_SCSI_UFS_CRYPTO
#include "ufshcd.h"
#include "ufshci.h"

static inline void ufshcd_prepare_lrbp_crypto(struct ufs_hba *hba,
					      struct scsi_cmnd *cmd,
					      struct ufshcd_lrb *lrbp)
{
	struct request *rq = cmd->request;

	if (rq->crypt_keyslot) {
		lrbp->crypto_key_slot = blk_ksm_get_slot_idx(rq->crypt_keyslot);
		lrbp->data_unit_num = rq->crypt_ctx->bc_dun[0];
	} else {
		lrbp->crypto_key_slot = -1;
	}
}

bool ufshcd_crypto_enable(struct ufs_hba *hba);

int ufshcd_hba_init_crypto(struct ufs_hba *hba);

void ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
					    struct request_queue *q);

void ufshcd_crypto_destroy_keyslot_manager(struct ufs_hba *hba);

#else /* CONFIG_SCSI_UFS_CRYPTO */

static inline void ufshcd_prepare_lrbp_crypto(struct ufs_hba *hba,
					      struct scsi_cmnd *cmd,
					      struct ufshcd_lrb *lrbp) { }

static inline bool ufshcd_crypto_enable(struct ufs_hba *hba)
{
	return false;
}

static inline int ufshcd_hba_init_crypto(struct ufs_hba *hba)
{
	return 0;
}

static inline void ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
						struct request_queue *q) { }

static inline void ufshcd_crypto_destroy_keyslot_manager(struct ufs_hba *hba)
{ }

#endif /* CONFIG_SCSI_UFS_CRYPTO */

#endif /* _UFSHCD_CRYPTO_H */
