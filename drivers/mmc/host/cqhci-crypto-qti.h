/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UFSHCD_CRYPTO_QTI_H
#define _UFSHCD_CRYPTO_QTI_H

#include "cqhci-crypto.h"

#if IS_ENABLED(CONFIG_MMC_CRYPTO_QTI)
int cqhci_qti_crypto_init(struct cqhci_host *cq_host);
#else
int cqhci_qti_crypto_init(struct cqhci_host *cq_host)
{
	return 0;
}
#endif /* CONFIG_MMC_CRYPTO_QTI) */
#endif /* _UFSHCD_ICE_QTI_H */
