/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_ETHTOOL_H
#define BNXT_ETHTOOL_H

extern const struct ethtool_ops bnxt_ethtool_ops;

u32 _bnxt_fw_to_ethtool_adv_spds(u16, u8);
u32 bnxt_fw_to_ethtool_speed(u16);
u16 bnxt_get_fw_auto_link_speeds(u32);

#endif
