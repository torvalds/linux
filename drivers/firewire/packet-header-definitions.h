// SPDX-License-Identifier: GPL-2.0-or-later
//
// packet-header-definitions.h - The definitions of header fields for IEEE 1394 packet.
//
// Copyright (c) 2024 Takashi Sakamoto

#ifndef _FIREWIRE_PACKET_HEADER_DEFINITIONS_H
#define _FIREWIRE_PACKET_HEADER_DEFINITIONS_H

#define ASYNC_HEADER_QUADLET_COUNT		4

#define ASYNC_HEADER_Q0_DESTINATION_SHIFT	16
#define ASYNC_HEADER_Q0_DESTINATION_MASK	0xffff0000
#define ASYNC_HEADER_Q0_TLABEL_SHIFT		10
#define ASYNC_HEADER_Q0_TLABEL_MASK		0x0000fc00
#define ASYNC_HEADER_Q0_RETRY_SHIFT		8
#define ASYNC_HEADER_Q0_RETRY_MASK		0x00000300
#define ASYNC_HEADER_Q0_TCODE_SHIFT		4
#define ASYNC_HEADER_Q0_TCODE_MASK		0x000000f0
#define ASYNC_HEADER_Q0_PRIORITY_SHIFT		0
#define ASYNC_HEADER_Q0_PRIORITY_MASK		0x0000000f
#define ASYNC_HEADER_Q1_SOURCE_SHIFT		16
#define ASYNC_HEADER_Q1_SOURCE_MASK		0xffff0000
#define ASYNC_HEADER_Q1_RCODE_SHIFT		12
#define ASYNC_HEADER_Q1_RCODE_MASK		0x0000f000
#define ASYNC_HEADER_Q1_RCODE_SHIFT		12
#define ASYNC_HEADER_Q1_RCODE_MASK		0x0000f000
#define ASYNC_HEADER_Q1_OFFSET_HIGH_SHIFT	0
#define ASYNC_HEADER_Q1_OFFSET_HIGH_MASK	0x0000ffff
#define ASYNC_HEADER_Q3_DATA_LENGTH_SHIFT	16
#define ASYNC_HEADER_Q3_DATA_LENGTH_MASK	0xffff0000
#define ASYNC_HEADER_Q3_EXTENDED_TCODE_SHIFT	0
#define ASYNC_HEADER_Q3_EXTENDED_TCODE_MASK	0x0000ffff

static inline unsigned int async_header_get_destination(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[0] & ASYNC_HEADER_Q0_DESTINATION_MASK) >> ASYNC_HEADER_Q0_DESTINATION_SHIFT;
}

static inline unsigned int async_header_get_tlabel(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[0] & ASYNC_HEADER_Q0_TLABEL_MASK) >> ASYNC_HEADER_Q0_TLABEL_SHIFT;
}

static inline unsigned int async_header_get_retry(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[0] & ASYNC_HEADER_Q0_RETRY_MASK) >> ASYNC_HEADER_Q0_RETRY_SHIFT;
}

static inline unsigned int async_header_get_tcode(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[0] & ASYNC_HEADER_Q0_TCODE_MASK) >> ASYNC_HEADER_Q0_TCODE_SHIFT;
}

static inline unsigned int async_header_get_priority(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[0] & ASYNC_HEADER_Q0_PRIORITY_MASK) >> ASYNC_HEADER_Q0_PRIORITY_SHIFT;
}

static inline unsigned int async_header_get_source(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[1] & ASYNC_HEADER_Q1_SOURCE_MASK) >> ASYNC_HEADER_Q1_SOURCE_SHIFT;
}

static inline unsigned int async_header_get_rcode(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[1] & ASYNC_HEADER_Q1_RCODE_MASK) >> ASYNC_HEADER_Q1_RCODE_SHIFT;
}

static inline u64 async_header_get_offset(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	u32 hi = (header[1] & ASYNC_HEADER_Q1_OFFSET_HIGH_MASK) >> ASYNC_HEADER_Q1_OFFSET_HIGH_SHIFT;
	return (((u64)hi) << 32) | ((u64)header[2]);
}

static inline u32 async_header_get_quadlet_data(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return header[3];
}

static inline unsigned int async_header_get_data_length(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[3] & ASYNC_HEADER_Q3_DATA_LENGTH_MASK) >> ASYNC_HEADER_Q3_DATA_LENGTH_SHIFT;
}

static inline unsigned int async_header_get_extended_tcode(const u32 header[ASYNC_HEADER_QUADLET_COUNT])
{
	return (header[3] & ASYNC_HEADER_Q3_EXTENDED_TCODE_MASK) >> ASYNC_HEADER_Q3_EXTENDED_TCODE_SHIFT;
}

static inline void async_header_set_destination(u32 header[ASYNC_HEADER_QUADLET_COUNT],
						unsigned int destination)
{
	header[0] &= ~ASYNC_HEADER_Q0_DESTINATION_MASK;
	header[0] |= (((u32)destination) << ASYNC_HEADER_Q0_DESTINATION_SHIFT) & ASYNC_HEADER_Q0_DESTINATION_MASK;
}

static inline void async_header_set_tlabel(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					   unsigned int tlabel)
{
	header[0] &= ~ASYNC_HEADER_Q0_TLABEL_MASK;
	header[0] |= (((u32)tlabel) << ASYNC_HEADER_Q0_TLABEL_SHIFT) & ASYNC_HEADER_Q0_TLABEL_MASK;
}

static inline void async_header_set_retry(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					  unsigned int retry)
{
	header[0] &= ~ASYNC_HEADER_Q0_RETRY_MASK;
	header[0] |= (((u32)retry) << ASYNC_HEADER_Q0_RETRY_SHIFT) & ASYNC_HEADER_Q0_RETRY_MASK;
}

static inline void async_header_set_tcode(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					  unsigned int tcode)
{
	header[0] &= ~ASYNC_HEADER_Q0_TCODE_MASK;
	header[0] |= (((u32)tcode) << ASYNC_HEADER_Q0_TCODE_SHIFT) & ASYNC_HEADER_Q0_TCODE_MASK;
}

static inline void async_header_set_priority(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					     unsigned int priority)
{
	header[0] &= ~ASYNC_HEADER_Q0_PRIORITY_MASK;
	header[0] |= (((u32)priority) << ASYNC_HEADER_Q0_PRIORITY_SHIFT) & ASYNC_HEADER_Q0_PRIORITY_MASK;
}


static inline void async_header_set_source(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					   unsigned int source)
{
	header[1] &= ~ASYNC_HEADER_Q1_SOURCE_MASK;
	header[1] |= (((u32)source) << ASYNC_HEADER_Q1_SOURCE_SHIFT) & ASYNC_HEADER_Q1_SOURCE_MASK;
}

static inline void async_header_set_rcode(u32 header[ASYNC_HEADER_QUADLET_COUNT],
					  unsigned int rcode)
{
	header[1] &= ~ASYNC_HEADER_Q1_RCODE_MASK;
	header[1] |= (((u32)rcode) << ASYNC_HEADER_Q1_RCODE_SHIFT) & ASYNC_HEADER_Q1_RCODE_MASK;
}

static inline void async_header_set_offset(u32 header[ASYNC_HEADER_QUADLET_COUNT], u64 offset)
{
	u32 hi = (u32)(offset >> 32);
	header[1] &= ~ASYNC_HEADER_Q1_OFFSET_HIGH_MASK;
	header[1] |= (hi << ASYNC_HEADER_Q1_OFFSET_HIGH_SHIFT) & ASYNC_HEADER_Q1_OFFSET_HIGH_MASK;
	header[2] = (u32)(offset & 0x00000000ffffffff);
}

static inline void async_header_set_quadlet_data(u32 header[ASYNC_HEADER_QUADLET_COUNT], u32 quadlet_data)
{
	header[3] = quadlet_data;
}

static inline void async_header_set_data_length(u32 header[ASYNC_HEADER_QUADLET_COUNT],
						unsigned int data_length)
{
	header[3] &= ~ASYNC_HEADER_Q3_DATA_LENGTH_MASK;
	header[3] |= (((u32)data_length) << ASYNC_HEADER_Q3_DATA_LENGTH_SHIFT) & ASYNC_HEADER_Q3_DATA_LENGTH_MASK;
}

static inline void async_header_set_extended_tcode(u32 header[ASYNC_HEADER_QUADLET_COUNT],
						   unsigned int extended_tcode)
{
	header[3] &= ~ASYNC_HEADER_Q3_EXTENDED_TCODE_MASK;
	header[3] |= (((u32)extended_tcode) << ASYNC_HEADER_Q3_EXTENDED_TCODE_SHIFT) & ASYNC_HEADER_Q3_EXTENDED_TCODE_MASK;
}

#define ISOC_HEADER_DATA_LENGTH_SHIFT		16
#define ISOC_HEADER_DATA_LENGTH_MASK		0xffff0000
#define ISOC_HEADER_TAG_SHIFT			14
#define ISOC_HEADER_TAG_MASK			0x0000c000
#define ISOC_HEADER_CHANNEL_SHIFT		8
#define ISOC_HEADER_CHANNEL_MASK		0x00003f00
#define ISOC_HEADER_TCODE_SHIFT			4
#define ISOC_HEADER_TCODE_MASK			0x000000f0
#define ISOC_HEADER_SY_SHIFT			0
#define ISOC_HEADER_SY_MASK			0x0000000f

static inline unsigned int isoc_header_get_data_length(u32 header)
{
	return (header & ISOC_HEADER_DATA_LENGTH_MASK) >> ISOC_HEADER_DATA_LENGTH_SHIFT;
}

static inline unsigned int isoc_header_get_tag(u32 header)
{
	return (header & ISOC_HEADER_TAG_MASK) >> ISOC_HEADER_TAG_SHIFT;
}

static inline unsigned int isoc_header_get_channel(u32 header)
{
	return (header & ISOC_HEADER_CHANNEL_MASK) >> ISOC_HEADER_CHANNEL_SHIFT;
}

static inline unsigned int isoc_header_get_tcode(u32 header)
{
	return (header & ISOC_HEADER_TCODE_MASK) >> ISOC_HEADER_TCODE_SHIFT;
}

static inline unsigned int isoc_header_get_sy(u32 header)
{
	return (header & ISOC_HEADER_SY_MASK) >> ISOC_HEADER_SY_SHIFT;
}

static inline void isoc_header_set_data_length(u32 *header, unsigned int data_length)
{
	*header &= ~ISOC_HEADER_DATA_LENGTH_MASK;
	*header |= (((u32)data_length) << ISOC_HEADER_DATA_LENGTH_SHIFT) & ISOC_HEADER_DATA_LENGTH_MASK;
}

static inline void isoc_header_set_tag(u32 *header, unsigned int tag)
{
	*header &= ~ISOC_HEADER_TAG_MASK;
	*header |= (((u32)tag) << ISOC_HEADER_TAG_SHIFT) & ISOC_HEADER_TAG_MASK;
}

static inline void isoc_header_set_channel(u32 *header, unsigned int channel)
{
	*header &= ~ISOC_HEADER_CHANNEL_MASK;
	*header |= (((u32)channel) << ISOC_HEADER_CHANNEL_SHIFT) & ISOC_HEADER_CHANNEL_MASK;
}

static inline void isoc_header_set_tcode(u32 *header, unsigned int tcode)
{
	*header &= ~ISOC_HEADER_TCODE_MASK;
	*header |= (((u32)tcode) << ISOC_HEADER_TCODE_SHIFT) & ISOC_HEADER_TCODE_MASK;
}

static inline void isoc_header_set_sy(u32 *header, unsigned int sy)
{
	*header &= ~ISOC_HEADER_SY_MASK;
	*header |= (((u32)sy) << ISOC_HEADER_SY_SHIFT) & ISOC_HEADER_SY_MASK;
}

#endif // _FIREWIRE_PACKET_HEADER_DEFINITIONS_H
