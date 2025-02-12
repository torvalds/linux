/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef _EFX_REFLASH_H
#define _EFX_REFLASH_H

#include "net_driver.h"
#include <linux/firmware.h>

int efx_reflash_flash_firmware(struct efx_nic *efx, const struct firmware *fw,
			       struct netlink_ext_ack *extack);

#endif /* _EFX_REFLASH_H */
