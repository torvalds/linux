/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2019, 2023 NXP */

#ifndef CAAM_DEBUGFS_H
#define CAAM_DEBUGFS_H

struct dentry;
struct caam_drv_private;
struct caam_perfmon;

#ifdef CONFIG_DEBUG_FS
void caam_debugfs_init(struct caam_drv_private *ctrlpriv,
		       struct caam_perfmon __force *perfmon, struct dentry *root);
#else
static inline void caam_debugfs_init(struct caam_drv_private *ctrlpriv,
				     struct caam_perfmon __force *perfmon,
				     struct dentry *root)
{}
#endif

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_CRYPTO_DEV_FSL_CAAM_CRYPTO_API_QI)
void caam_debugfs_qi_congested(void);
void caam_debugfs_qi_init(struct caam_drv_private *ctrlpriv);
#else
static inline void caam_debugfs_qi_congested(void) {}
static inline void caam_debugfs_qi_init(struct caam_drv_private *ctrlpriv) {}
#endif

#endif /* CAAM_DEBUGFS_H */
