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

#ifndef __MEMAC_H
#define __MEMAC_H

#include "fman_mac.h"

#include <linux/netdevice.h>

struct fman_mac *memac_config(struct fman_mac_params *params);
int memac_set_promiscuous(struct fman_mac *memac, bool new_val);
int memac_modify_mac_address(struct fman_mac *memac, enet_addr_t *enet_addr);
int memac_adjust_link(struct fman_mac *memac, u16 speed);
int memac_cfg_max_frame_len(struct fman_mac *memac, u16 new_val);
int memac_cfg_reset_on_init(struct fman_mac *memac, bool enable);
int memac_cfg_fixed_link(struct fman_mac *memac,
			 struct fixed_phy_status *fixed_link);
int memac_enable(struct fman_mac *memac, enum comm_mode mode);
int memac_disable(struct fman_mac *memac, enum comm_mode mode);
int memac_init(struct fman_mac *memac);
int memac_free(struct fman_mac *memac);
int memac_accept_rx_pause_frames(struct fman_mac *memac, bool en);
int memac_set_tx_pause_frames(struct fman_mac *memac, u8 priority,
			      u16 pause_time, u16 thresh_time);
int memac_set_exception(struct fman_mac *memac,
			enum fman_mac_exceptions exception, bool enable);
int memac_add_hash_mac_address(struct fman_mac *memac, enet_addr_t *eth_addr);
int memac_del_hash_mac_address(struct fman_mac *memac, enet_addr_t *eth_addr);

#endif /* __MEMAC_H */
