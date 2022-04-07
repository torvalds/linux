/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _UFSHCD_CRYPTO_QTI_H
#define _UFSHCD_CRYPTO_QTI_H

#include "ufshcd.h"
#include "ufshcd-crypto.h"

#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO_QTI)
int ufshcd_qti_hba_init_crypto_capabilities(struct ufs_hba *hba);
#else /* CONFIG_SCSI_UFS_CRYPTO_QTI */
static inline int ufshcd_qti_hba_init_crypto_capabilities(
		struct ufs_hba *hba)
{
	return 0;
}
#endif /* CONFIG_SCSI_UFS_CRYPTO_QTI */

#endif /* _UFSHCD_CRYPTO_QTI_H */
