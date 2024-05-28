/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_NUMERIC_H
#define UDS_NUMERIC_H

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/types.h>

/*
 * These utilities encode or decode a number from an offset in a larger data buffer and then
 * advance the offset pointer to the next field in the buffer.
 */

static inline void decode_s64_le(const u8 *buffer, size_t *offset, s64 *decoded)
{
	*decoded = get_unaligned_le64(buffer + *offset);
	*offset += sizeof(s64);
}

static inline void encode_s64_le(u8 *data, size_t *offset, s64 to_encode)
{
	put_unaligned_le64(to_encode, data + *offset);
	*offset += sizeof(s64);
}

static inline void decode_u64_le(const u8 *buffer, size_t *offset, u64 *decoded)
{
	*decoded = get_unaligned_le64(buffer + *offset);
	*offset += sizeof(u64);
}

static inline void encode_u64_le(u8 *data, size_t *offset, u64 to_encode)
{
	put_unaligned_le64(to_encode, data + *offset);
	*offset += sizeof(u64);
}

static inline void decode_s32_le(const u8 *buffer, size_t *offset, s32 *decoded)
{
	*decoded = get_unaligned_le32(buffer + *offset);
	*offset += sizeof(s32);
}

static inline void encode_s32_le(u8 *data, size_t *offset, s32 to_encode)
{
	put_unaligned_le32(to_encode, data + *offset);
	*offset += sizeof(s32);
}

static inline void decode_u32_le(const u8 *buffer, size_t *offset, u32 *decoded)
{
	*decoded = get_unaligned_le32(buffer + *offset);
	*offset += sizeof(u32);
}

static inline void encode_u32_le(u8 *data, size_t *offset, u32 to_encode)
{
	put_unaligned_le32(to_encode, data + *offset);
	*offset += sizeof(u32);
}

static inline void decode_u16_le(const u8 *buffer, size_t *offset, u16 *decoded)
{
	*decoded = get_unaligned_le16(buffer + *offset);
	*offset += sizeof(u16);
}

static inline void encode_u16_le(u8 *data, size_t *offset, u16 to_encode)
{
	put_unaligned_le16(to_encode, data + *offset);
	*offset += sizeof(u16);
}

#endif /* UDS_NUMERIC_H */
