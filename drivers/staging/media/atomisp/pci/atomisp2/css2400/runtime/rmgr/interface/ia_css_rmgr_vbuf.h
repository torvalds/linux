#ifndef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#ifndef _IA_CSS_RMGR_VBUF_H
#define _IA_CSS_RMGR_VBUF_H

#include "ia_css_rmgr.h"
#include <type_support.h>
#include <system_types.h>

/**
 * @brief Data structure for the resource handle (host, vbuf)
 */
struct ia_css_rmgr_vbuf_handle {
	hrt_vaddress vptr;
	uint8_t count;
	uint32_t size;
};

/**
 * @brief Data structure for the resource pool (host, vbuf)
 */
struct ia_css_rmgr_vbuf_pool {
	uint8_t copy_on_write;
	uint8_t recycle;
	uint32_t size;
	uint32_t index;
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
STORAGE_CLASS_RMGR_H enum ia_css_err ia_css_rmgr_init_vbuf(
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
