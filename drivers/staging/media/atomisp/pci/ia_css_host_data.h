/* SPDX-License-Identifier: GPL-2.0 */
/* Release Version: irci_stable_candrpv_0415_20150521_0458 */
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

#ifndef __SH_CSS_HOST_DATA_H
#define __SH_CSS_HOST_DATA_H

#include <ia_css_types.h>	/* ia_css_pipe */

/**
 * @brief Allocate structure ia_css_host_data.
 *
 * @param[in]	size		Size of the requested host data
 *
 * @return
 *	- NULL, can't allocate requested size
 *	- pointer to structure, field address points to host data with size bytes
 */
struct ia_css_host_data *
ia_css_host_data_allocate(size_t size);

/**
 * @brief Free structure ia_css_host_data.
 *
 * @param[in]	me	Pointer to structure, if a NULL is passed functions
 *			returns without error. Otherwise a valid pointer to
 *			structure must be passed and a related memory
 *			is freed.
 *
 * @return
 */
void ia_css_host_data_free(struct ia_css_host_data *me);

#endif /* __SH_CSS_HOST_DATA_H */
