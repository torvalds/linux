/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SPL2SW_MAC_H__
#define __SPL2SW_MAC_H__

void spl2sw_mac_hw_stop(struct spl2sw_common *comm);
void spl2sw_mac_hw_start(struct spl2sw_common *comm);
int  spl2sw_mac_addr_add(struct spl2sw_mac *mac);
int  spl2sw_mac_addr_del(struct spl2sw_mac *mac);
void spl2sw_mac_hw_init(struct spl2sw_common *comm);
void spl2sw_mac_rx_mode_set(struct spl2sw_mac *mac);
void spl2sw_mac_init(struct spl2sw_common *comm);
void spl2sw_mac_soft_reset(struct spl2sw_common *comm);

#endif
