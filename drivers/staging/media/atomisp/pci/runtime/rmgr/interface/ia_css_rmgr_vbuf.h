/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef _IA_CSS_RMGR_VBUF_H
#define _IA_CSS_RMGR_VBUF_H

#include "ia_css_rmgr.h"
#include <type_support.h>
#include <ia_css_types.h>
#include <system_local.h>

/**
 * @brief Data structure for the resource handle (host, vbuf)
 */
struct ia_css_rmgr_vbuf_handle {
	ia_css_ptr vptr;
	u8 count;
	u32 size;
};

/**
 * @brief Data structure for the resource pool (host, vbuf)
 */
struct ia_css_rmgr_vbuf_pool {
	u8 copy_on_write;
	u8 recycle;
	u32 size;
	u32 index;
	struct ia_css_rmgr_vbuf_handle **handles;
};

/**
 * @brief VBUF resource pools
 */
extern struct ia_css_rmgr_vbuf_pool *vbuf_ref;
extern struct ia_css_rmgr_vbuf_pool *vbuf_write;
extern struct ia_css_rmgr_vbuf_pool *hmm_buffer_pool;

/**
 * @brief Initialize the resource pool (host, vbuf)
 *
 * @param pool	The pointer to the pool
 */
STORAGE_CLASS_RMGR_H int ia_css_rmgr_init_vbuf(
    struct ia_css_rmgr_vbuf_pool *pool);

/**
 * @brief Uninitialize the resource pool (host, vbuf)
 *
 * @param pool	The pointer to the pool
 */
STORAGE_CLASS_RMGR_H void ia_css_rmgr_uninit_vbuf(
    struct ia_css_rmgr_vbuf_pool *pool);

/**
 * @brief Acquire a handle from the pool (host, vbuf)
 *
 * @param pool		The pointer to the pool
 * @param handle	The pointer to the handle
 */
STORAGE_CLASS_RMGR_H void ia_css_rmgr_acq_vbuf(
    struct ia_css_rmgr_vbuf_pool *pool,
    struct ia_css_rmgr_vbuf_handle **handle);

/**
 * @brief Release a handle to the pool (host, vbuf)
 *
 * @param pool		The pointer to the pool
 * @param handle	The pointer to the handle
 */
STORAGE_CLASS_RMGR_H void ia_css_rmgr_rel_vbuf(
    struct ia_css_rmgr_vbuf_pool *pool,
    struct ia_css_rmgr_vbuf_handle **handle);

/**
 * @brief Retain the reference count for a handle (host, vbuf)
 *
 * @param handle	The pointer to the handle
 */
void ia_css_rmgr_refcount_retain_vbuf(struct ia_css_rmgr_vbuf_handle **handle);

/**
 * @brief Release the reference count for a handle (host, vbuf)
 *
 * @param handle	The pointer to the handle
 */
void ia_css_rmgr_refcount_release_vbuf(struct ia_css_rmgr_vbuf_handle **handle);

#endif	/* _IA_CSS_RMGR_VBUF_H */
