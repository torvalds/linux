/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#if !defined(_KBASE_TL_SERIALIZE_H)
#define _KBASE_TL_SERIALIZE_H

#include <mali_kbase.h>

#include <linux/timer.h>

/* The number of nanoseconds in a second. */
#define NSECS_IN_SEC       1000000000ull /* ns */

/**
 * kbasep_serialize_bytes - serialize bytes to the message buffer
 *
 * @buffer:    Message buffer
 * @pos:       Message buffer offset
 * @bytes:     Bytes to serialize
 * @len:       Length of bytes array
 *
 * Serialize bytes as if using memcpy().
 *
 * Return: updated position in the buffer
 */
static inline size_t kbasep_serialize_bytes(
		char       *buffer,
		size_t     pos,
		const void *bytes,
		size_t     len)
{
	KBASE_DEBUG_ASSERT(buffer);
	KBASE_DEBUG_ASSERT(bytes);

	memcpy(&buffer[pos], bytes, len);

	return pos + len;
}

/**
 * kbasep_serialize_string - serialize string to the message buffer
 *
 * @buffer:         Message buffer
 * @pos:            Message buffer offset
 * @string:         String to serialize
 * @max_write_size: Number of bytes that can be stored in buffer
 *
 * String is serialized as 4 bytes for string size,
 * then string content and then null terminator.
 *
 * Return: updated position in the buffer
 */
static inline size_t kbasep_serialize_string(
		char       *buffer,
		size_t     pos,
		const char *string,
		size_t     max_write_size)
{
	u32 string_len;

	KBASE_DEBUG_ASSERT(buffer);
	KBASE_DEBUG_ASSERT(string);
	/* Timeline string consists of at least string length and nul
	 * terminator.
	 */
	KBASE_DEBUG_ASSERT(max_write_size >= sizeof(string_len) + sizeof(char));
	max_write_size -= sizeof(string_len);

	string_len = strscpy(
			&buffer[pos + sizeof(string_len)],
			string,
			max_write_size);
	string_len += sizeof(char);

	/* Make sure that the source string fit into the buffer. */
	KBASE_DEBUG_ASSERT(string_len <= max_write_size);

	/* Update string length. */
	memcpy(&buffer[pos], &string_len, sizeof(string_len));

	return pos + sizeof(string_len) + string_len;
}

/**
 * kbasep_serialize_timestamp - serialize timestamp to the message buffer
 *
 * @buffer: Message buffer
 * @pos:    Message buffer offset
 *
 * Get current timestamp using kbasep_get_timestamp()
 * and serialize it as 64 bit unsigned integer.
 *
 * Return: updated position in the buffer
 */
static inline size_t kbasep_serialize_timestamp(void *buffer, size_t pos)
{
	u64             timestamp;

	timestamp = ktime_get_raw_ns();

	return kbasep_serialize_bytes(
			buffer, pos,
			&timestamp, sizeof(timestamp));
}
#endif /* _KBASE_TL_SERIALIZE_H */
