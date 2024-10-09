/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _EMAC_PTP_H_
#define _EMAC_PTP_H_

int emac_ptp_init(struct net_device *netdev);
void emac_ptp_remove(struct net_device *netdev);
int emac_ptp_config(struct emac_hw *hw);
int emac_ptp_stop(struct emac_hw *hw);
int emac_ptp_set_linkspeed(struct emac_hw *hw, u32 speed);
int emac_tstamp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
void emac_ptp_intr(struct emac_hw *hw);

#endif /* _EMAC_PTP_H_ */
