/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_acl_flex_keys.h
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_SPECTRUM_ACL_FLEX_KEYS_H
#define _MLXSW_SPECTRUM_ACL_FLEX_KEYS_H

#include "core_acl_flex_keys.h"

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l2_dmac[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DMAC, 0x00, 6),
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x08, 13, 3),
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x08, 0, 12),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l2_smac[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC, 0x00, 6),
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x08, 13, 3),
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x08, 0, 12),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l2_smac_ex[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC, 0x02, 6),
	MLXSW_AFK_ELEMENT_INST_U32(ETHERTYPE, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_sip[] = {
	MLXSW_AFK_ELEMENT_INST_U32(SRC_IP4, 0x00, 0, 32),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x08, 0, 8),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_dip[] = {
	MLXSW_AFK_ELEMENT_INST_U32(DST_IP4, 0x00, 0, 32),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x08, 0, 8),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_ex[] = {
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x00, 0, 12),
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x08, 29, 3),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_L4_PORT, 0x08, 0, 16),
	MLXSW_AFK_ELEMENT_INST_U32(DST_L4_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_dip[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP6_LO, 0x00, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_ex1[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP6_HI, 0x00, 8),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x08, 0, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_sip[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP6_LO, 0x00, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_sip_ex[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP6_HI, 0x00, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_packet_type[] = {
	MLXSW_AFK_ELEMENT_INST_U32(ETHERTYPE, 0x00, 0, 16),
};

static const struct mlxsw_afk_block mlxsw_sp_afk_blocks[] = {
	MLXSW_AFK_BLOCK(0x10, mlxsw_sp_afk_element_info_l2_dmac),
	MLXSW_AFK_BLOCK(0x11, mlxsw_sp_afk_element_info_l2_smac),
	MLXSW_AFK_BLOCK(0x12, mlxsw_sp_afk_element_info_l2_smac_ex),
	MLXSW_AFK_BLOCK(0x30, mlxsw_sp_afk_element_info_ipv4_sip),
	MLXSW_AFK_BLOCK(0x31, mlxsw_sp_afk_element_info_ipv4_dip),
	MLXSW_AFK_BLOCK(0x33, mlxsw_sp_afk_element_info_ipv4_ex),
	MLXSW_AFK_BLOCK(0x60, mlxsw_sp_afk_element_info_ipv6_dip),
	MLXSW_AFK_BLOCK(0x65, mlxsw_sp_afk_element_info_ipv6_ex1),
	MLXSW_AFK_BLOCK(0x62, mlxsw_sp_afk_element_info_ipv6_sip),
	MLXSW_AFK_BLOCK(0x63, mlxsw_sp_afk_element_info_ipv6_sip_ex),
	MLXSW_AFK_BLOCK(0xB0, mlxsw_sp_afk_element_info_packet_type),
};

#define MLXSW_SP_AFK_BLOCKS_COUNT ARRAY_SIZE(mlxsw_sp_afk_blocks)

#endif
