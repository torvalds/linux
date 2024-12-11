/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef _IA_CSS_RMGR_H
#define _IA_CSS_RMGR_H

#include <ia_css_err.h>

#ifndef __INLINE_RMGR__
#define STORAGE_CLASS_RMGR_H extern
#define STORAGE_CLASS_RMGR_C
#else				/* __INLINE_RMGR__ */
#define STORAGE_CLASS_RMGR_H static inline
#define STORAGE_CLASS_RMGR_C static inline
#endif				/* __INLINE_RMGR__ */

/**
 * @brief Initialize resource manager (host/common)
 */
int ia_css_rmgr_init(void);

/**
 * @brief Uninitialize resource manager (host/common)
 */
void ia_css_rmgr_uninit(void);

/*****************************************************************
 * Interface definition - resource type (host/common)
 *****************************************************************
 *
 * struct ia_css_rmgr_<type>_pool;
 * struct ia_css_rmgr_<type>_handle;
 *
 * STORAGE_CLASS_RMGR_H void ia_css_rmgr_init_<type>(
 *	struct ia_css_rmgr_<type>_pool *pool);
 *
 * STORAGE_CLASS_RMGR_H void ia_css_rmgr_uninit_<type>(
 *	struct ia_css_rmgr_<type>_pool *pool);
 *
 * STORAGE_CLASS_RMGR_H void ia_css_rmgr_acq_<type>(
 *	struct ia_css_rmgr_<type>_pool *pool,
 *	struct ia_css_rmgr_<type>_handle **handle);
 *
 * STORAGE_CLASS_RMGR_H void ia_css_rmgr_rel_<type>(
 *	struct ia_css_rmgr_<type>_pool *pool,
 *	struct ia_css_rmgr_<type>_handle **handle);
 *
 *****************************************************************
 * Interface definition - refcounting (host/common)
 *****************************************************************
 *
 * void ia_css_rmgr_refcount_retain_<type>(
 *	struct ia_css_rmgr_<type>_handle **handle);
 *
 * void ia_css_rmgr_refcount_release_<type>(
 *	struct ia_css_rmgr_<type>_handle **handle);
 */

#include "ia_css_rmgr_vbuf.h"

#endif	/* _IA_CSS_RMGR_H */
