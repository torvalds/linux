/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_FIRMWARE_H
#define __IA_CSS_FIRMWARE_H

/* @file
 * This file contains firmware loading/unloading support functionality
 */

#include <linux/device.h>
#include "ia_css_err.h"
#include "ia_css_env.h"

/* CSS firmware package structure.
 */
struct ia_css_fw {
	void	    *data;  /** pointer to the firmware data */
	unsigned int bytes; /** length in bytes of firmware data */
};

/* @brief Loads the firmware
 * @param[in]	env		Environment, provides functions to access the
 *				environment in which the CSS code runs. This is
 *				used for host side memory access and message
 *				printing.
 * @param[in]	fw		Firmware package containing the firmware for all
 *				predefined ISP binaries.
 * @return			Returns -EINVAL in case of any
 *				errors and 0 otherwise.
 *
 * This function interprets the firmware package. All
 * contents of this firmware package are copied into local data structures, so
 * the fw pointer could be freed after this function completes.
 *
 * Rationale for this function is that it can be called before ia_css_init, and thus
 * speeds up ia_css_init (ia_css_init is called each time a stream is created but the
 * firmware only needs to be loaded once).
 */
int
ia_css_load_firmware(struct device *dev, const struct ia_css_env *env,
		     const struct ia_css_fw  *fw);

/* @brief Unloads the firmware
 * @return	None
 *
 * This function unloads the firmware loaded by ia_css_load_firmware.
 * It is pointless to call this function if no firmware is loaded,
 * but it won't harm. Use this to deallocate all memory associated with the firmware.
 */
void
ia_css_unload_firmware(void);

#endif /* __IA_CSS_FIRMWARE_H */
