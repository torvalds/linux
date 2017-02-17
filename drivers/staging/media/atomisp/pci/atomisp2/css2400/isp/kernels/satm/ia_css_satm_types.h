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

#ifndef __IA_CSS_SATM_TYPES_H
#define __IA_CSS_SATM_TYPES_H

/**
 * \brief SATM Parameters
 * \detail Currently SATM paramters are used only for testing purposes
 */
struct ia_css_satm_params {
	int test_satm; /**< Test parameter */
};

/**
 * \brief SATM public paramterers.
 * \details Struct with all paramters for SATM that can be seet from
 * the CSS API. Currenly, only test paramters are defined.
 */
struct ia_css_satm_config {
	struct ia_css_satm_params params; /**< SATM paramaters */
};

#endif /* __IA_CSS_SATM_TYPES_H */
