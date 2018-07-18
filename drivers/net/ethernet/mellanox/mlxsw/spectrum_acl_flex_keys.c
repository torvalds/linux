/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_acl_flex_keys.c
 * Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017-2018 Jiri Pirko <jiri@mellanox.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include "spectrum.h"
#include "item.h"
#include "core_acl_flex_keys.h"

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l2_dmac[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DMAC_32_47, 0x00, 2),
	MLXSW_AFK_ELEMENT_INST_BUF(DMAC_0_31, 0x02, 4),
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x08, 13, 3),
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x08, 0, 12),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l2_smac[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_32_47, 0x00, 2),
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_0_31, 0x02, 4),
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x08, 13, 3),
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x08, 0, 12),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l2_smac_ex[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_32_47, 0x02, 2),
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_0_31, 0x04, 4),
	MLXSW_AFK_ELEMENT_INST_U32(ETHERTYPE, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_sip[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_0_31, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x08, 0, 8),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_dip[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_0_31, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x08, 0, 8),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_0_31, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_U32(IP_ECN, 0x04, 4, 2),
	MLXSW_AFK_ELEMENT_INST_U32(IP_TTL_, 0x04, 24, 8),
	MLXSW_AFK_ELEMENT_INST_U32(IP_DSCP, 0x08, 0, 6),
	MLXSW_AFK_ELEMENT_INST_U32(TCP_FLAGS, 0x08, 8, 9), /* TCP_CONTROL+TCP_ECN */
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_ex[] = {
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x00, 0, 12),
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x08, 29, 3),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_L4_PORT, 0x08, 0, 16),
	MLXSW_AFK_ELEMENT_INST_U32(DST_L4_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_dip[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_32_63, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_0_31, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_ex1[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_96_127, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_64_95, 0x04, 4),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x08, 0, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_sip[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_32_63, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_0_31, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_sip_ex[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_96_127, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_64_95, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_packet_type[] = {
	MLXSW_AFK_ELEMENT_INST_U32(ETHERTYPE, 0x00, 0, 16),
};

static const struct mlxsw_afk_block mlxsw_sp1_afk_blocks[] = {
	MLXSW_AFK_BLOCK(0x10, mlxsw_sp_afk_element_info_l2_dmac),
	MLXSW_AFK_BLOCK(0x11, mlxsw_sp_afk_element_info_l2_smac),
	MLXSW_AFK_BLOCK(0x12, mlxsw_sp_afk_element_info_l2_smac_ex),
	MLXSW_AFK_BLOCK(0x30, mlxsw_sp_afk_element_info_ipv4_sip),
	MLXSW_AFK_BLOCK(0x31, mlxsw_sp_afk_element_info_ipv4_dip),
	MLXSW_AFK_BLOCK(0x32, mlxsw_sp_afk_element_info_ipv4),
	MLXSW_AFK_BLOCK(0x33, mlxsw_sp_afk_element_info_ipv4_ex),
	MLXSW_AFK_BLOCK(0x60, mlxsw_sp_afk_element_info_ipv6_dip),
	MLXSW_AFK_BLOCK(0x65, mlxsw_sp_afk_element_info_ipv6_ex1),
	MLXSW_AFK_BLOCK(0x62, mlxsw_sp_afk_element_info_ipv6_sip),
	MLXSW_AFK_BLOCK(0x63, mlxsw_sp_afk_element_info_ipv6_sip_ex),
	MLXSW_AFK_BLOCK(0xB0, mlxsw_sp_afk_element_info_packet_type),
};

static void mlxsw_sp1_afk_encode_u32(const struct mlxsw_item *storage_item,
				     const struct mlxsw_item *output_item,
				     char *storage, char *output_indexed)
{
	u32 value;

	value = __mlxsw_item_get32(storage, storage_item, 0);
	__mlxsw_item_set32(output_indexed, output_item, 0, value);
}

static void mlxsw_sp1_afk_encode_buf(const struct mlxsw_item *storage_item,
				     const struct mlxsw_item *output_item,
				     char *storage, char *output_indexed)
{
	char *storage_data = __mlxsw_item_data(storage, storage_item, 0);
	char *output_data = __mlxsw_item_data(output_indexed, output_item, 0);
	size_t len = output_item->size.bytes;

	memcpy(output_data, storage_data, len);
}

#define MLXSW_SP1_AFK_KEY_BLOCK_SIZE 16

static void
mlxsw_sp1_afk_encode_one(const struct mlxsw_afk_element_inst *elinst,
			 int block_index, char *storage, char *output)
{
	unsigned int offset = block_index * MLXSW_SP1_AFK_KEY_BLOCK_SIZE;
	char *output_indexed = output + offset;
	const struct mlxsw_item *storage_item = &elinst->info->item;
	const struct mlxsw_item *output_item = &elinst->item;

	if (elinst->type == MLXSW_AFK_ELEMENT_TYPE_U32)
		mlxsw_sp1_afk_encode_u32(storage_item, output_item,
					 storage, output_indexed);
	else if (elinst->type == MLXSW_AFK_ELEMENT_TYPE_BUF)
		mlxsw_sp1_afk_encode_buf(storage_item, output_item,
					 storage, output_indexed);
}

const struct mlxsw_afk_ops mlxsw_sp1_afk_ops = {
	.blocks		= mlxsw_sp1_afk_blocks,
	.blocks_count	= ARRAY_SIZE(mlxsw_sp1_afk_blocks),
	.encode_one	= mlxsw_sp1_afk_encode_one,
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_0[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DMAC_0_31, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_1[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_0_31, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_2[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_32_47, 0x04, 2),
	MLXSW_AFK_ELEMENT_INST_BUF(DMAC_32_47, 0x06, 2),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_3[] = {
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x00, 0, 3),
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x04, 16, 12),
	MLXSW_AFK_ELEMENT_INST_BUF(DMAC_32_47, 0x06, 2),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_4[] = {
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x00, 0, 3),
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x04, 16, 12),
	MLXSW_AFK_ELEMENT_INST_U32(ETHERTYPE, 0x04, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_5[] = {
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x04, 16, 12),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x04, 0, 8), /* RX_ACL_SYSTEM_PORT */
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_0[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_0_31, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_1[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_0_31, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_2[] = {
	MLXSW_AFK_ELEMENT_INST_U32(IP_DSCP, 0x04, 0, 6),
	MLXSW_AFK_ELEMENT_INST_U32(IP_ECN, 0x04, 6, 2),
	MLXSW_AFK_ELEMENT_INST_U32(IP_TTL_, 0x04, 8, 8),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x04, 16, 8),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_0[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_32_63, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_1[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_64_95, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_2[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_96_127, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_3[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_32_63, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_4[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_64_95, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_5[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_96_127, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l4_0[] = {
	MLXSW_AFK_ELEMENT_INST_U32(SRC_L4_PORT, 0x04, 16, 16),
	MLXSW_AFK_ELEMENT_INST_U32(DST_L4_PORT, 0x04, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l4_2[] = {
	MLXSW_AFK_ELEMENT_INST_U32(TCP_FLAGS, 0x04, 16, 9), /* TCP_CONTROL + TCP_ECN */
};

static const struct mlxsw_afk_block mlxsw_sp2_afk_blocks[] = {
	MLXSW_AFK_BLOCK(0x10, mlxsw_sp_afk_element_info_mac_0),
	MLXSW_AFK_BLOCK(0x11, mlxsw_sp_afk_element_info_mac_1),
	MLXSW_AFK_BLOCK(0x12, mlxsw_sp_afk_element_info_mac_2),
	MLXSW_AFK_BLOCK(0x13, mlxsw_sp_afk_element_info_mac_3),
	MLXSW_AFK_BLOCK(0x14, mlxsw_sp_afk_element_info_mac_4),
	MLXSW_AFK_BLOCK(0x15, mlxsw_sp_afk_element_info_mac_5),
	MLXSW_AFK_BLOCK(0x38, mlxsw_sp_afk_element_info_ipv4_0),
	MLXSW_AFK_BLOCK(0x39, mlxsw_sp_afk_element_info_ipv4_1),
	MLXSW_AFK_BLOCK(0x3A, mlxsw_sp_afk_element_info_ipv4_2),
	MLXSW_AFK_BLOCK(0x40, mlxsw_sp_afk_element_info_ipv6_0),
	MLXSW_AFK_BLOCK(0x41, mlxsw_sp_afk_element_info_ipv6_1),
	MLXSW_AFK_BLOCK(0x42, mlxsw_sp_afk_element_info_ipv6_2),
	MLXSW_AFK_BLOCK(0x43, mlxsw_sp_afk_element_info_ipv6_3),
	MLXSW_AFK_BLOCK(0x44, mlxsw_sp_afk_element_info_ipv6_4),
	MLXSW_AFK_BLOCK(0x45, mlxsw_sp_afk_element_info_ipv6_5),
	MLXSW_AFK_BLOCK(0x90, mlxsw_sp_afk_element_info_l4_0),
	MLXSW_AFK_BLOCK(0x92, mlxsw_sp_afk_element_info_l4_2),
};

static void
mlxsw_sp2_afk_encode_one(const struct mlxsw_afk_element_inst *elinst,
			 int block_index, char *storage, char *output)
{
}

const struct mlxsw_afk_ops mlxsw_sp2_afk_ops = {
	.blocks		= mlxsw_sp2_afk_blocks,
	.blocks_count	= ARRAY_SIZE(mlxsw_sp2_afk_blocks),
	.encode_one	= mlxsw_sp2_afk_encode_one,
};
