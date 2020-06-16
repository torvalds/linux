/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef _UFSHCD_CRYPTO_H
#define _UFSHCD_CRYPTO_H

#ifdef CONFIG_SCSI_UFS_CRYPTO
#include "ufshcd.h"
#include "ufshci.h"

static inline void ufshcd_prepare_lrbp_crypto_spec(struct ufs_hba *hba,
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

bool ufshcd_crypto_enable_spec(struct ufs_hba *hba);

struct blk_ksm_ll_ops;
int ufshcd_hba_init_crypto_spec(struct ufs_hba *hba,
				const struct blk_ksm_ll_ops *ksm_ops);

void ufshcd_crypto_setup_rq_keyslot_manager_spec(struct ufs_hba *hba,
						 struct request_queue *q);

void ufshcd_crypto_destroy_keyslot_manager_spec(struct ufs_hba *hba);

/* Crypto Variant Ops Support */
bool ufshcd_crypto_enable(struct ufs_hba *hba);

int ufshcd_hba_init_crypto(struct ufs_hba *hba);

void ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
					    struct request_queue *q);

void ufshcd_crypto_destroy_keyslot_manager(struct ufs_hba *hba);

void ufshcd_prepare_lrbp_crypto(struct ufs_hba *hba,
				struct scsi_cmnd *cmd,
				struct ufshcd_lrb *lrbp);

int ufshcd_map_sg_crypto(struct ufs_hba *hba, struct ufshcd_lrb *lrbp);

int ufshcd_complete_lrbp_crypto(struct ufs_hba *hba,
				struct scsi_cmnd *cmd,
				struct ufshcd_lrb *lrbp);

void ufshcd_crypto_debug(struct ufs_hba *hba);

int ufshcd_crypto_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op);

int ufshcd_crypto_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op);

void ufshcd_crypto_set_vops(struct ufs_hba *hba,
			    struct ufs_hba_crypto_variant_ops *crypto_vops);

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

static inline int ufshcd_map_sg_crypto(struct ufs_hba *hba,
				       struct ufshcd_lrb *lrbp)
{
	return 0;
}

static inline int ufshcd_complete_lrbp_crypto(struct ufs_hba *hba,
					      struct scsi_cmnd *cmd,
					      struct ufshcd_lrb *lrbp)
{
	return 0;
}

static inline void ufshcd_crypto_debug(struct ufs_hba *hba) { }

static inline int ufshcd_crypto_suspend(struct ufs_hba *hba,
					enum ufs_pm_op pm_op)
{
	return 0;
}

static inline int ufshcd_crypto_resume(struct ufs_hba *hba,
					enum ufs_pm_op pm_op)
{
	return 0;
}

static inline void ufshcd_crypto_set_vops(struct ufs_hba *hba,
			struct ufs_hba_crypto_variant_ops *crypto_vops) { }

#endif /* CONFIG_SCSI_UFS_CRYPTO */

#endif /* _UFSHCD_CRYPTO_H */
