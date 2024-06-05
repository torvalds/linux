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

#endif // _FIREWIRE_PHY_PACKET_DEFINITIONS_H
