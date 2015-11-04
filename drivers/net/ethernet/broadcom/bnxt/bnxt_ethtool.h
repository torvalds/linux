/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_ETHTOOL_H
#define BNXT_ETHTOOL_H

extern const struct ethtool_ops bnxt_ethtool_ops;

u32 bnxt_fw_to_ethtool_speed(u16);

#endif
