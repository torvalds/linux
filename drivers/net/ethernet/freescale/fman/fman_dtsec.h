/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DTSEC_H
#define __DTSEC_H

#include "fman_mac.h"

struct fman_mac *dtsec_config(struct fman_mac_params *params);
int dtsec_set_promiscuous(struct fman_mac *dtsec, bool new_val);
int dtsec_modify_mac_address(struct fman_mac *dtsec, enet_addr_t *enet_addr);
int dtsec_adjust_link(struct fman_mac *dtsec,
		      u16 speed);
int dtsec_restart_autoneg(struct fman_mac *dtsec);
int dtsec_cfg_max_frame_len(struct fman_mac *dtsec, u16 new_val);
int dtsec_cfg_pad_and_crc(struct fman_mac *dtsec, bool new_val);
int dtsec_enable(struct fman_mac *dtsec, enum comm_mode mode);
int dtsec_disable(struct fman_mac *dtsec, enum comm_mode mode);
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

#endif /* __DTSEC_H */
