// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

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
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l2_smac[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_32_47, 0x00, 2),
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_0_31, 0x02, 4),
	MLXSW_AFK_ELEMENT_INST_U32(PCP, 0x08, 13, 3),
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x08, 0, 12),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_l2_smac_ex[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_32_47, 0x02, 2),
	MLXSW_AFK_ELEMENT_INST_BUF(SMAC_0_31, 0x04, 4),
	MLXSW_AFK_ELEMENT_INST_U32(ETHERTYPE, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_sip[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(SRC_IP_0_31, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_U32(L4_PORT_RANGE, 0x04, 16, 16),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x08, 0, 8),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 16),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_dip[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_0_31, 0x00, 4),
	MLXSW_AFK_ELEMENT_INST_U32(L4_PORT_RANGE, 0x04, 16, 16),
	MLXSW_AFK_ELEMENT_INST_U32(IP_PROTO, 0x08, 0, 8),
	MLXSW_AFK_ELEMENT_INST_U32(SRC_SYS_PORT, 0x0C, 0, 16),
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

#define MLXSW_SP1_AFK_KEY_BLOCK_SIZE 16

static void mlxsw_sp1_afk_encode_block(char *output, int block_index,
				       char *block)
{
	unsigned int offset = block_index * MLXSW_SP1_AFK_KEY_BLOCK_SIZE;
	char *output_indexed = output + offset;

	memcpy(output_indexed, block, MLXSW_SP1_AFK_KEY_BLOCK_SIZE);
}

static void mlxsw_sp1_afk_clear_block(char *output, int block_index)
{
	unsigned int offset = block_index * MLXSW_SP1_AFK_KEY_BLOCK_SIZE;
	char *output_indexed = output + offset;

	memset(output_indexed, 0, MLXSW_SP1_AFK_KEY_BLOCK_SIZE);
}

const struct mlxsw_afk_ops mlxsw_sp1_afk_ops = {
	.blocks		= mlxsw_sp1_afk_blocks,
	.blocks_count	= ARRAY_SIZE(mlxsw_sp1_afk_blocks),
	.encode_block	= mlxsw_sp1_afk_encode_block,
	.clear_block	= mlxsw_sp1_afk_clear_block,
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_0[] = {
	MLXSW_AFK_ELEMENT_INST_U32(FDB_MISS, 0x00, 3, 1),
	MLXSW_AFK_ELEMENT_INST_BUF(DMAC_0_31, 0x04, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_1[] = {
	MLXSW_AFK_ELEMENT_INST_U32(FDB_MISS, 0x00, 3, 1),
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
	MLXSW_AFK_ELEMENT_INST_EXT_U32(SRC_SYS_PORT, 0x04, 0, 8, -1, true), /* RX_ACL_SYSTEM_PORT */
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

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_4[] = {
	MLXSW_AFK_ELEMENT_INST_U32(VIRT_ROUTER_LSB, 0x04, 24, 8),
	MLXSW_AFK_ELEMENT_INST_EXT_U32(VIRT_ROUTER_MSB, 0x00, 0, 3, 0, true),
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
	MLXSW_AFK_ELEMENT_INST_U32(L4_PORT_RANGE, 0x04, 0, 16),
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
	MLXSW_AFK_BLOCK(0x3C, mlxsw_sp_afk_element_info_ipv4_4),
	MLXSW_AFK_BLOCK(0x40, mlxsw_sp_afk_element_info_ipv6_0),
	MLXSW_AFK_BLOCK(0x41, mlxsw_sp_afk_element_info_ipv6_1),
	MLXSW_AFK_BLOCK(0x42, mlxsw_sp_afk_element_info_ipv6_2),
	MLXSW_AFK_BLOCK(0x43, mlxsw_sp_afk_element_info_ipv6_3),
	MLXSW_AFK_BLOCK(0x44, mlxsw_sp_afk_element_info_ipv6_4),
	MLXSW_AFK_BLOCK(0x45, mlxsw_sp_afk_element_info_ipv6_5),
	MLXSW_AFK_BLOCK(0x90, mlxsw_sp_afk_element_info_l4_0),
	MLXSW_AFK_BLOCK(0x92, mlxsw_sp_afk_element_info_l4_2),
};

#define MLXSW_SP2_AFK_BITS_PER_BLOCK 36

/* A block in Spectrum-2 is of the following form:
 *
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |35|34|33|32|
 * +-----------------------------------------------------------------------------------------------+
 * |31|30|29|28|27|26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 */
MLXSW_ITEM64(sp2_afk, block, value, 0x00, 0, MLXSW_SP2_AFK_BITS_PER_BLOCK);

/* The key / mask block layout in Spectrum-2 is of the following form:
 *
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |                block11_high                   |
 * +-----------------------------------------------------------------------------------------------+
 * |                    block11_low                               |         block10_high           |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * ...
 */

struct mlxsw_sp2_afk_block_layout {
	unsigned short offset;
	struct mlxsw_item item;
};

#define MLXSW_SP2_AFK_BLOCK_LAYOUT(_block, _offset, _shift)			\
	{									\
		.offset = _offset,						\
		{								\
			.shift = _shift,					\
			.size = {.bits = MLXSW_SP2_AFK_BITS_PER_BLOCK},		\
			.name = #_block,					\
		}								\
	}									\

static const struct mlxsw_sp2_afk_block_layout mlxsw_sp2_afk_blocks_layout[] = {
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block0, 0x30, 0),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block1, 0x2C, 4),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block2, 0x28, 8),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block3, 0x24, 12),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block4, 0x20, 16),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block5, 0x1C, 20),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block6, 0x18, 24),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block7, 0x14, 28),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block8, 0x0C, 0),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block9, 0x08, 4),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block10, 0x04, 8),
	MLXSW_SP2_AFK_BLOCK_LAYOUT(block11, 0x00, 12),
};

static void __mlxsw_sp2_afk_block_value_set(char *output, int block_index,
					    u64 block_value)
{
	const struct mlxsw_sp2_afk_block_layout *block_layout;

	if (WARN_ON(block_index < 0 ||
		    block_index >= ARRAY_SIZE(mlxsw_sp2_afk_blocks_layout)))
		return;

	block_layout = &mlxsw_sp2_afk_blocks_layout[block_index];
	__mlxsw_item_set64(output + block_layout->offset,
			   &block_layout->item, 0, block_value);
}

static void mlxsw_sp2_afk_encode_block(char *output, int block_index,
				       char *block)
{
	u64 block_value = mlxsw_sp2_afk_block_value_get(block);

	__mlxsw_sp2_afk_block_value_set(output, block_index, block_value);
}

static void mlxsw_sp2_afk_clear_block(char *output, int block_index)
{
	__mlxsw_sp2_afk_block_value_set(output, block_index, 0);
}

const struct mlxsw_afk_ops mlxsw_sp2_afk_ops = {
	.blocks		= mlxsw_sp2_afk_blocks,
	.blocks_count	= ARRAY_SIZE(mlxsw_sp2_afk_blocks),
	.encode_block	= mlxsw_sp2_afk_encode_block,
	.clear_block	= mlxsw_sp2_afk_clear_block,
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_mac_5b[] = {
	MLXSW_AFK_ELEMENT_INST_U32(VID, 0x04, 18, 12),
	MLXSW_AFK_ELEMENT_INST_EXT_U32(SRC_SYS_PORT, 0x04, 0, 9, -1, true), /* RX_ACL_SYSTEM_PORT */
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv4_4b[] = {
	MLXSW_AFK_ELEMENT_INST_U32(VIRT_ROUTER_LSB, 0x04, 13, 8),
	MLXSW_AFK_ELEMENT_INST_U32(VIRT_ROUTER_MSB, 0x04, 21, 4),
};

static struct mlxsw_afk_element_inst mlxsw_sp_afk_element_info_ipv6_2b[] = {
	MLXSW_AFK_ELEMENT_INST_BUF(DST_IP_96_127, 0x04, 4),
};

static const struct mlxsw_afk_block mlxsw_sp4_afk_blocks[] = {
	MLXSW_AFK_BLOCK(0x10, mlxsw_sp_afk_element_info_mac_0),
	MLXSW_AFK_BLOCK(0x11, mlxsw_sp_afk_element_info_mac_1),
	MLXSW_AFK_BLOCK(0x12, mlxsw_sp_afk_element_info_mac_2),
	MLXSW_AFK_BLOCK(0x13, mlxsw_sp_afk_element_info_mac_3),
	MLXSW_AFK_BLOCK(0x14, mlxsw_sp_afk_element_info_mac_4),
	MLXSW_AFK_BLOCK(0x1A, mlxsw_sp_afk_element_info_mac_5b),
	MLXSW_AFK_BLOCK(0x38, mlxsw_sp_afk_element_info_ipv4_0),
	MLXSW_AFK_BLOCK(0x39, mlxsw_sp_afk_element_info_ipv4_1),
	MLXSW_AFK_BLOCK(0x3A, mlxsw_sp_afk_element_info_ipv4_2),
	MLXSW_AFK_BLOCK(0x35, mlxsw_sp_afk_element_info_ipv4_4b),
	MLXSW_AFK_BLOCK(0x40, mlxsw_sp_afk_element_info_ipv6_0),
	MLXSW_AFK_BLOCK(0x41, mlxsw_sp_afk_element_info_ipv6_1),
	MLXSW_AFK_BLOCK(0x47, mlxsw_sp_afk_element_info_ipv6_2b),
	MLXSW_AFK_BLOCK(0x43, mlxsw_sp_afk_element_info_ipv6_3),
	MLXSW_AFK_BLOCK(0x44, mlxsw_sp_afk_element_info_ipv6_4),
	MLXSW_AFK_BLOCK(0x45, mlxsw_sp_afk_element_info_ipv6_5),
	MLXSW_AFK_BLOCK(0x90, mlxsw_sp_afk_element_info_l4_0),
	MLXSW_AFK_BLOCK(0x92, mlxsw_sp_afk_element_info_l4_2),
};

const struct mlxsw_afk_ops mlxsw_sp4_afk_ops = {
	.blocks		= mlxsw_sp4_afk_blocks,
	.blocks_count	= ARRAY_SIZE(mlxsw_sp4_afk_blocks),
	.encode_block	= mlxsw_sp2_afk_encode_block,
	.clear_block	= mlxsw_sp2_afk_clear_block,
};
