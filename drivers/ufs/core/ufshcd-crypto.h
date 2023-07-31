/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef _UFSHCD_CRYPTO_H
#define _UFSHCD_CRYPTO_H

#include <scsi/scsi_cmnd.h>
#include <ufs/ufshcd.h>
#include "ufshcd-priv.h"
#include <ufs/ufshci.h>

#ifdef CONFIG_SCSI_UFS_CRYPTO

static inline void ufshcd_prepare_lrbp_crypto(struct request *rq,
					      struct ufshcd_lrb *lrbp)
{
	if (!rq || !rq->crypt_keyslot) {
		lrbp->crypto_key_slot = -1;
		return;
	}

	lrbp->crypto_key_slot = blk_crypto_keyslot_index(rq->crypt_keyslot);
	lrbp->data_unit_num = rq->crypt_ctx->bc_dun[0];
}

static inline void
ufshcd_prepare_req_desc_hdr_crypto(struct ufshcd_lrb *lrbp,
				   struct request_desc_header *h)
{
	if (lrbp->crypto_key_slot < 0)
		return;
	h->enable_crypto = 1;
	h->cci = lrbp->crypto_key_slot;
	h->dunl = cpu_to_le32(lower_32_bits(lrbp->data_unit_num));
	h->dunu = cpu_to_le32(upper_32_bits(lrbp->data_unit_num));
}

bool ufshcd_crypto_enable(struct ufs_hba *hba);

int ufshcd_hba_init_crypto_capabilities(struct ufs_hba *hba);

void ufshcd_init_crypto(struct ufs_hba *hba);

void ufshcd_crypto_register(struct ufs_hba *hba, struct request_queue *q);

#else /* CONFIG_SCSI_UFS_CRYPTO */

static inline void ufshcd_prepare_lrbp_crypto(struct request *rq,
					      struct ufshcd_lrb *lrbp) { }

static inline void
ufshcd_prepare_req_desc_hdr_crypto(struct ufshcd_lrb *lrbp,
				   struct request_desc_header *h) { }

static inline bool ufshcd_crypto_enable(struct ufs_hba *hba)
{
	return false;
}

static inline int ufshcd_hba_init_crypto_capabilities(struct ufs_hba *hba)
{
	return 0;
}

static inline void ufshcd_init_crypto(struct ufs_hba *hba) { }

static inline void ufshcd_crypto_register(struct ufs_hba *hba,
					  struct request_queue *q) { }

#endif /* CONFIG_SCSI_UFS_CRYPTO */

#endif /* _UFSHCD_CRYPTO_H */
