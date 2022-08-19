/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#ifndef __MEMAC_H
#define __MEMAC_H

#include "fman_mac.h"

#include <linux/netdevice.h>
#include <linux/phy_fixed.h>

struct fman_mac *memac_config(struct fman_mac_params *params);
int memac_set_promiscuous(struct fman_mac *memac, bool new_val);
int memac_modify_mac_address(struct fman_mac *memac, const enet_addr_t *enet_addr);
int memac_adjust_link(struct fman_mac *memac, u16 speed);
int memac_cfg_max_frame_len(struct fman_mac *memac, u16 new_val);
int memac_cfg_reset_on_init(struct fman_mac *memac, bool enable);
int memac_cfg_fixed_link(struct fman_mac *memac,
			 struct fixed_phy_status *fixed_link);
int memac_enable(struct fman_mac *memac);
int memac_disable(struct fman_mac *memac);
int memac_init(struct fman_mac *memac);
int memac_free(struct fman_mac *memac);
int memac_accept_rx_pause_frames(struct fman_mac *memac, bool en);
int memac_set_tx_pause_frames(struct fman_mac *memac, u8 priority,
			      u16 pause_time, u16 thresh_time);
int memac_set_exception(struct fman_mac *memac,
			enum fman_mac_exceptions exception, bool enable);
int memac_add_hash_mac_address(struct fman_mac *memac, enet_addr_t *eth_addr);
int memac_del_hash_mac_address(struct fman_mac *memac, enet_addr_t *eth_addr);
int memac_set_allmulti(struct fman_mac *memac, bool enable);
int memac_set_tstamp(struct fman_mac *memac, bool enable);

#endif /* __MEMAC_H */
