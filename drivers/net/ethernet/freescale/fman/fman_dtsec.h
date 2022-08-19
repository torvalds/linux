/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#ifndef __DTSEC_H
#define __DTSEC_H

#include "fman_mac.h"

struct fman_mac *dtsec_config(struct fman_mac_params *params);
int dtsec_set_promiscuous(struct fman_mac *dtsec, bool new_val);
int dtsec_modify_mac_address(struct fman_mac *dtsec, const enet_addr_t *enet_addr);
int dtsec_adjust_link(struct fman_mac *dtsec,
		      u16 speed);
int dtsec_restart_autoneg(struct fman_mac *dtsec);
int dtsec_cfg_max_frame_len(struct fman_mac *dtsec, u16 new_val);
int dtsec_cfg_pad_and_crc(struct fman_mac *dtsec, bool new_val);
int dtsec_enable(struct fman_mac *dtsec);
int dtsec_disable(struct fman_mac *dtsec);
int dtsec_init(struct fman_mac *dtsec);
int dtsec_free(struct fman_mac *dtsec);
int dtsec_accept_rx_pause_frames(struct fman_mac *dtsec, bool en);
int dtsec_set_tx_pause_frames(struct fman_mac *dtsec, u8 priority,
			      u16 pause_time, u16 thresh_time);
int dtsec_set_exception(struct fman_mac *dtsec,
			enum fman_mac_exceptions exception, bool enable);
int dtsec_add_hash_mac_address(struct fman_mac *dtsec, enet_addr_t *eth_addr);
int dtsec_del_hash_mac_address(struct fman_mac *dtsec, enet_addr_t *eth_addr);
int dtsec_get_version(struct fman_mac *dtsec, u32 *mac_version);
int dtsec_set_allmulti(struct fman_mac *dtsec, bool enable);
int dtsec_set_tstamp(struct fman_mac *dtsec, bool enable);

#endif /* __DTSEC_H */
