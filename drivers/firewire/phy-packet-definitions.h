// SPDX-License-Identifier: GPL-2.0-or-later
//
// phy-packet-definitions.h - The definitions of phy packet for IEEE 1394.
//
// Copyright (c) 2024 Takashi Sakamoto

#ifndef _FIREWIRE_PHY_PACKET_DEFINITIONS_H
#define _FIREWIRE_PHY_PACKET_DEFINITIONS_H

#define PACKET_IDENTIFIER_MASK				0xc0000000
#define PACKET_IDENTIFIER_SHIFT				30

static inline unsigned int phy_packet_get_packet_identifier(u32 quadlet)
{
	return (quadlet & PACKET_IDENTIFIER_MASK) >> PACKET_IDENTIFIER_SHIFT;
}

static inline void phy_packet_set_packet_identifier(u32 *quadlet, unsigned int packet_identifier)
{
	*quadlet &= ~PACKET_IDENTIFIER_MASK;
	*quadlet |= (packet_identifier << PACKET_IDENTIFIER_SHIFT) & PACKET_IDENTIFIER_MASK;
}

#define PHY_PACKET_PACKET_IDENTIFIER_PHY_CONFIG		0

#define PHY_CONFIG_ROOT_ID_MASK				0x3f000000
#define PHY_CONFIG_ROOT_ID_SHIFT			24
#define PHY_CONFIG_FORCE_ROOT_NODE_MASK			0x00800000
#define PHY_CONFIG_FORCE_ROOT_NODE_SHIFT		23
#define PHY_CONFIG_GAP_COUNT_OPTIMIZATION_MASK		0x00400000
#define PHY_CONFIG_GAP_COUNT_OPTIMIZATION_SHIFT		22
#define PHY_CONFIG_GAP_COUNT_MASK			0x003f0000
#define PHY_CONFIG_GAP_COUNT_SHIFT			16

static inline unsigned int phy_packet_phy_config_get_root_id(u32 quadlet)
{
	return (quadlet & PHY_CONFIG_ROOT_ID_MASK) >> PHY_CONFIG_ROOT_ID_SHIFT;
}

static inline void phy_packet_phy_config_set_root_id(u32 *quadlet, unsigned int root_id)
{
	*quadlet &= ~PHY_CONFIG_ROOT_ID_MASK;
	*quadlet |= (root_id << PHY_CONFIG_ROOT_ID_SHIFT) & PHY_CONFIG_ROOT_ID_MASK;
}

static inline bool phy_packet_phy_config_get_force_root_node(u32 quadlet)
{
	return (quadlet & PHY_CONFIG_FORCE_ROOT_NODE_MASK) >> PHY_CONFIG_FORCE_ROOT_NODE_SHIFT;
}

static inline void phy_packet_phy_config_set_force_root_node(u32 *quadlet, bool has_force_root_node)
{
	*quadlet &= ~PHY_CONFIG_FORCE_ROOT_NODE_MASK;
	*quadlet |= (has_force_root_node << PHY_CONFIG_FORCE_ROOT_NODE_SHIFT) & PHY_CONFIG_FORCE_ROOT_NODE_MASK;
}

static inline bool phy_packet_phy_config_get_gap_count_optimization(u32 quadlet)
{
	return (quadlet & PHY_CONFIG_GAP_COUNT_OPTIMIZATION_MASK) >> PHY_CONFIG_GAP_COUNT_OPTIMIZATION_SHIFT;
}

static inline void phy_packet_phy_config_set_gap_count_optimization(u32 *quadlet, bool has_gap_count_optimization)
{
	*quadlet &= ~PHY_CONFIG_GAP_COUNT_OPTIMIZATION_MASK;
	*quadlet |= (has_gap_count_optimization << PHY_CONFIG_GAP_COUNT_OPTIMIZATION_SHIFT) & PHY_CONFIG_GAP_COUNT_OPTIMIZATION_MASK;
}

static inline unsigned int phy_packet_phy_config_get_gap_count(u32 quadlet)
{
	return (quadlet & PHY_CONFIG_GAP_COUNT_MASK) >> PHY_CONFIG_GAP_COUNT_SHIFT;
}

static inline void phy_packet_phy_config_set_gap_count(u32 *quadlet, unsigned int gap_count)
{
	*quadlet &= ~PHY_CONFIG_GAP_COUNT_MASK;
	*quadlet |= (gap_count << PHY_CONFIG_GAP_COUNT_SHIFT) & PHY_CONFIG_GAP_COUNT_MASK;
}

#define PHY_PACKET_PACKET_IDENTIFIER_SELF_ID		2

#define SELF_ID_PHY_ID_MASK				0x3f000000
#define SELF_ID_PHY_ID_SHIFT				24
#define SELF_ID_EXTENDED_MASK				0x00800000
#define SELF_ID_EXTENDED_SHIFT				23
#define SELF_ID_MORE_PACKETS_MASK			0x00000001
#define SELF_ID_MORE_PACKETS_SHIFT			0

#define SELF_ID_ZERO_LINK_ACTIVE_MASK			0x00400000
#define SELF_ID_ZERO_LINK_ACTIVE_SHIFT			22
#define SELF_ID_ZERO_GAP_COUNT_MASK			0x003f0000
#define SELF_ID_ZERO_GAP_COUNT_SHIFT			16
#define SELF_ID_ZERO_SCODE_MASK				0x0000c000
#define SELF_ID_ZERO_SCODE_SHIFT			14
#define SELF_ID_ZERO_CONTENDER_MASK			0x00000800
#define SELF_ID_ZERO_CONTENDER_SHIFT			11
#define SELF_ID_ZERO_POWER_CLASS_MASK			0x00000700
#define SELF_ID_ZERO_POWER_CLASS_SHIFT			8
#define SELF_ID_ZERO_INITIATED_RESET_MASK		0x00000002
#define SELF_ID_ZERO_INITIATED_RESET_SHIFT		1

#define SELF_ID_EXTENDED_SEQUENCE_MASK			0x00700000
#define SELF_ID_EXTENDED_SEQUENCE_SHIFT			20

#define SELF_ID_PORT_STATUS_MASK			0x3

#define SELF_ID_SEQUENCE_MAXIMUM_QUADLET_COUNT		4

static inline unsigned int phy_packet_self_id_get_phy_id(u32 quadlet)
{
	return (quadlet & SELF_ID_PHY_ID_MASK)  >> SELF_ID_PHY_ID_SHIFT;
}

static inline void phy_packet_self_id_set_phy_id(u32 *quadlet, unsigned int phy_id)
{
	*quadlet &= ~SELF_ID_PHY_ID_MASK;
	*quadlet |= (phy_id << SELF_ID_PHY_ID_SHIFT) & SELF_ID_PHY_ID_MASK;
}

static inline bool phy_packet_self_id_get_extended(u32 quadlet)
{
	return (quadlet & SELF_ID_EXTENDED_MASK) >> SELF_ID_EXTENDED_SHIFT;
}

static inline void phy_packet_self_id_set_extended(u32 *quadlet, bool extended)
{
	*quadlet &= ~SELF_ID_EXTENDED_MASK;
	*quadlet |= (extended << SELF_ID_EXTENDED_SHIFT) & SELF_ID_EXTENDED_MASK;
}

static inline bool phy_packet_self_id_zero_get_link_active(u32 quadlet)
{
	return (quadlet & SELF_ID_ZERO_LINK_ACTIVE_MASK) >> SELF_ID_ZERO_LINK_ACTIVE_SHIFT;
}

static inline void phy_packet_self_id_zero_set_link_active(u32 *quadlet, bool is_active)
{
	*quadlet &= ~SELF_ID_ZERO_LINK_ACTIVE_MASK;
	*quadlet |= (is_active << SELF_ID_ZERO_LINK_ACTIVE_SHIFT) & SELF_ID_ZERO_LINK_ACTIVE_MASK;
}

static inline unsigned int phy_packet_self_id_zero_get_gap_count(u32 quadlet)
{
	return (quadlet & SELF_ID_ZERO_GAP_COUNT_MASK) >> SELF_ID_ZERO_GAP_COUNT_SHIFT;
}

static inline void phy_packet_self_id_zero_set_gap_count(u32 *quadlet, unsigned int gap_count)
{
	*quadlet &= ~SELF_ID_ZERO_GAP_COUNT_MASK;
	*quadlet |= (gap_count << SELF_ID_ZERO_GAP_COUNT_SHIFT) & SELF_ID_ZERO_GAP_COUNT_MASK;
}

static inline unsigned int phy_packet_self_id_zero_get_scode(u32 quadlet)
{
	return (quadlet & SELF_ID_ZERO_SCODE_MASK) >> SELF_ID_ZERO_SCODE_SHIFT;
}

static inline void phy_packet_self_id_zero_set_scode(u32 *quadlet, unsigned int speed)
{
	*quadlet &= ~SELF_ID_ZERO_SCODE_MASK;
	*quadlet |= (speed << SELF_ID_ZERO_SCODE_SHIFT) & SELF_ID_ZERO_SCODE_MASK;
}

static inline bool phy_packet_self_id_zero_get_contender(u32 quadlet)
{
	return (quadlet & SELF_ID_ZERO_CONTENDER_MASK) >> SELF_ID_ZERO_CONTENDER_SHIFT;
}

static inline void phy_packet_self_id_zero_set_contender(u32 *quadlet, bool is_contender)
{
	*quadlet &= ~SELF_ID_ZERO_CONTENDER_MASK;
	*quadlet |= (is_contender << SELF_ID_ZERO_CONTENDER_SHIFT) & SELF_ID_ZERO_CONTENDER_MASK;
}

static inline unsigned int phy_packet_self_id_zero_get_power_class(u32 quadlet)
{
	return (quadlet & SELF_ID_ZERO_POWER_CLASS_MASK) >> SELF_ID_ZERO_POWER_CLASS_SHIFT;
}

static inline void phy_packet_self_id_zero_set_power_class(u32 *quadlet, unsigned int power_class)
{
	*quadlet &= ~SELF_ID_ZERO_POWER_CLASS_MASK;
	*quadlet |= (power_class << SELF_ID_ZERO_POWER_CLASS_SHIFT) & SELF_ID_ZERO_POWER_CLASS_MASK;
}

static inline bool phy_packet_self_id_zero_get_initiated_reset(u32 quadlet)
{
	return (quadlet & SELF_ID_ZERO_INITIATED_RESET_MASK) >> SELF_ID_ZERO_INITIATED_RESET_SHIFT;
}

static inline void phy_packet_self_id_zero_set_initiated_reset(u32 *quadlet, bool is_initiated_reset)
{
	*quadlet &= ~SELF_ID_ZERO_INITIATED_RESET_MASK;
	*quadlet |= (is_initiated_reset << SELF_ID_ZERO_INITIATED_RESET_SHIFT) & SELF_ID_ZERO_INITIATED_RESET_MASK;
}

static inline bool phy_packet_self_id_get_more_packets(u32 quadlet)
{
	return (quadlet & SELF_ID_MORE_PACKETS_MASK) >> SELF_ID_MORE_PACKETS_SHIFT;
}

static inline void phy_packet_self_id_set_more_packets(u32 *quadlet, bool is_more_packets)
{
	*quadlet &= ~SELF_ID_MORE_PACKETS_MASK;
	*quadlet |= (is_more_packets << SELF_ID_MORE_PACKETS_SHIFT) & SELF_ID_MORE_PACKETS_MASK;
}

static inline unsigned int phy_packet_self_id_extended_get_sequence(u32 quadlet)
{
	return (quadlet & SELF_ID_EXTENDED_SEQUENCE_MASK) >> SELF_ID_EXTENDED_SEQUENCE_SHIFT;
}

static inline void phy_packet_self_id_extended_set_sequence(u32 *quadlet, unsigned int sequence)
{
	*quadlet &= ~SELF_ID_EXTENDED_SEQUENCE_MASK;
	*quadlet |= (sequence << SELF_ID_EXTENDED_SHIFT) & SELF_ID_EXTENDED_SEQUENCE_MASK;
}

struct self_id_sequence_enumerator {
	const u32 *cursor;
	unsigned int quadlet_count;
};

static inline const u32 *self_id_sequence_enumerator_next(
		struct self_id_sequence_enumerator *enumerator, unsigned int *quadlet_count)
{
	const u32 *self_id_sequence, *cursor;
	u32 quadlet;
	unsigned int count;
	unsigned int sequence;

	if (enumerator->cursor == NULL || enumerator->quadlet_count == 0)
		return ERR_PTR(-ENODATA);
	cursor = enumerator->cursor;
	count = 1;

	quadlet = *cursor;
	sequence = 0;
	while (phy_packet_self_id_get_more_packets(quadlet)) {
		if (count >= enumerator->quadlet_count ||
		    count >= SELF_ID_SEQUENCE_MAXIMUM_QUADLET_COUNT)
			return ERR_PTR(-EPROTO);
		++cursor;
		++count;
		quadlet = *cursor;

		if (!phy_packet_self_id_get_extended(quadlet) ||
		    sequence != phy_packet_self_id_extended_get_sequence(quadlet))
			return ERR_PTR(-EPROTO);
		++sequence;
	}

	*quadlet_count = count;
	self_id_sequence = enumerator->cursor;

	enumerator->cursor += count;
	enumerator->quadlet_count -= count;

	return self_id_sequence;
}

enum phy_packet_self_id_port_status {
	PHY_PACKET_SELF_ID_PORT_STATUS_NONE = 0,
	PHY_PACKET_SELF_ID_PORT_STATUS_NCONN = 1,
	PHY_PACKET_SELF_ID_PORT_STATUS_PARENT = 2,
	PHY_PACKET_SELF_ID_PORT_STATUS_CHILD = 3,
};

static inline unsigned int self_id_sequence_get_port_capacity(unsigned int quadlet_count)
{
	return quadlet_count * 8 - 5;
}

static inline enum phy_packet_self_id_port_status self_id_sequence_get_port_status(
		const u32 *self_id_sequence, unsigned int quadlet_count, unsigned int port_index)
{
	unsigned int index, shift;

	index = (port_index + 5) / 8;
	shift = 16 - ((port_index + 5) % 8) * 2;

	if (index < quadlet_count && index < SELF_ID_SEQUENCE_MAXIMUM_QUADLET_COUNT)
		return (self_id_sequence[index] >> shift) & SELF_ID_PORT_STATUS_MASK;

	return PHY_PACKET_SELF_ID_PORT_STATUS_NONE;
}

static inline void self_id_sequence_set_port_status(u32 *self_id_sequence, unsigned int quadlet_count,
						    unsigned int port_index,
						    enum phy_packet_self_id_port_status status)
{
	unsigned int index, shift;

	index = (port_index + 5) / 8;
	shift = 16 - ((port_index + 5) % 8) * 2;

	if (index < quadlet_count) {
		self_id_sequence[index] &= ~(SELF_ID_PORT_STATUS_MASK << shift);
		self_id_sequence[index] |= status << shift;
	}
}

#endif // _FIREWIRE_PHY_PACKET_DEFINITIONS_H
