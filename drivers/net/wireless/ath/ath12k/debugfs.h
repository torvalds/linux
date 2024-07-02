/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ATH12K_DEBUGFS_H_
#define _ATH12K_DEBUGFS_H_

#ifdef CONFIG_ATH12K_DEBUGFS
void ath12k_debugfs_soc_create(struct ath12k_base *ab);
void ath12k_debugfs_soc_destroy(struct ath12k_base *ab);
void ath12k_debugfs_register(struct ath12k *ar);

#else
static inline void ath12k_debugfs_soc_create(struct ath12k_base *ab)
{
}

static inline void ath12k_debugfs_soc_destroy(struct ath12k_base *ab)
{
}

static inline void ath12k_debugfs_register(struct ath12k *ar)
{
}

#endif /* CONFIG_ATH12K_DEBUGFS */

#endif /* _ATH12K_DEBUGFS_H_ */
