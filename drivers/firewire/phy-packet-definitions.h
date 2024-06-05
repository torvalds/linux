// SPDX-License-Identifier: GPL-2.0-or-later
//
// phy-packet-definitions.h - The definitions of phy packet for IEEE 1394.
//
// Copyright (c) 2024 Takashi Sakamoto

#ifndef _FIREWIRE_PHY_PACKET_DEFINITIONS_H
#define _FIREWIRE_PHY_PACKET_DEFINITIONS_H

#define SELF_ID_EXTENDED_MASK				0x00800000
#define SELF_ID_EXTENDED_SHIFT				23
#define SELF_ID_MORE_PACKETS_MASK			0x00000001
#define SELF_ID_MORE_PACKETS_SHIFT			0

#define SELF_ID_EXTENDED_SEQUENCE_MASK			0x00700000
#define SELF_ID_EXTENDED_SEQUENCE_SHIFT			20

#define SELF_ID_PORT_STATUS_MASK			0x3

#define SELF_ID_SEQUENCE_MAXIMUM_QUADLET_COUNT		4

static inline bool phy_packet_self_id_get_extended(u32 quadlet)
{
	return (quadlet & SELF_ID_EXTENDED_MASK) >> SELF_ID_EXTENDED_SHIFT;
}

static inline bool phy_packet_self_id_get_more_packets(u32 quadlet)
{
	return (quadlet & SELF_ID_MORE_PACKETS_MASK) >> SELF_ID_MORE_PACKETS_SHIFT;
}

static inline unsigned int phy_packet_self_id_extended_get_sequence(u32 quadlet)
{
	return (quadlet & SELF_ID_EXTENDED_SEQUENCE_MASK) >> SELF_ID_EXTENDED_SEQUENCE_SHIFT;
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
