/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_TLV_H_
#define _FBNIC_TLV_H_

#include <asm/byteorder.h>
#include <linux/bits.h>
#include <linux/const.h>
#include <linux/types.h>

#define FBNIC_TLV_MSG_ALIGN(len)	ALIGN(len, sizeof(u32))
#define FBNIC_TLV_MSG_SIZE(len)		\
		(FBNIC_TLV_MSG_ALIGN(len) / sizeof(u32))

/* TLV Header Format
 *    3			  2		      1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |		Length		   |M|I|RSV|	   Type / ID	   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * The TLV header format described above will be used for transferring
 * messages between the host and the firmware. To ensure byte ordering
 * we have defined all fields as being little endian.
 * Type/ID: Identifier for message and/or attribute
 * RSV: Reserved field for future use, likely as additional flags
 * I: cannot_ignore flag, identifies if unrecognized attribute can be ignored
 * M: is_msg, indicates that this is the start of a new message
 * Length: Total length of message in dwords including header
 *		or
 *	   Total length of attribute in bytes including header
 */
struct fbnic_tlv_hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u16 type		: 12; /* 0 .. 11  Type / ID */
	u16 rsvd		: 2;  /* 12 .. 13 Reserved for future use */
	u16 cannot_ignore	: 1;  /* 14	  Attribute can be ignored */
	u16 is_msg		: 1;  /* 15	  Header belongs to message */
#elif defined(__BIG_ENDIAN_BITFIELD)
	u16 is_msg		: 1;  /* 15	  Header belongs to message */
	u16 cannot_ignore	: 1;  /* 14	  Attribute can be ignored */
	u16 rsvd		: 2;  /* 13 .. 12 Reserved for future use */
	u16 type		: 12; /* 11 .. 0  Type / ID */
#else
#error "Missing defines from byteorder.h"
#endif
	__le16 len;		/* 16 .. 32	length including TLV header */
};

#define FBNIC_TLV_RESULTS_MAX		32

struct fbnic_tlv_msg {
	struct fbnic_tlv_hdr	hdr;
	__le32			value[];
};

#define FBNIC_TLV_MSG_ID_UNKNOWN		USHRT_MAX

enum fbnic_tlv_type {
	FBNIC_TLV_STRING,
	FBNIC_TLV_FLAG,
	FBNIC_TLV_UNSIGNED,
	FBNIC_TLV_SIGNED,
	FBNIC_TLV_BINARY,
	FBNIC_TLV_NESTED,
	FBNIC_TLV_ARRAY,
	__FBNIC_TLV_MAX_TYPE
};

/* TLV Index
 * Defines the relationship between the attribute IDs and their types.
 * For each entry in the index there will be a size and type associated
 * with it so that we can use this to parse the data and verify it matches
 * the expected layout.
 */
struct fbnic_tlv_index {
	u16			id;
	u16			len;
	enum fbnic_tlv_type	type;
};

#define TLV_MAX_DATA			(PAGE_SIZE - 512)
#define FBNIC_TLV_ATTR_ID_UNKNOWN	USHRT_MAX
#define FBNIC_TLV_ATTR_STRING(id, len)	{ id, len, FBNIC_TLV_STRING }
#define FBNIC_TLV_ATTR_FLAG(id)		{ id, 0, FBNIC_TLV_FLAG }
#define FBNIC_TLV_ATTR_U32(id)		{ id, sizeof(u32), FBNIC_TLV_UNSIGNED }
#define FBNIC_TLV_ATTR_U64(id)		{ id, sizeof(u64), FBNIC_TLV_UNSIGNED }
#define FBNIC_TLV_ATTR_S32(id)		{ id, sizeof(s32), FBNIC_TLV_SIGNED }
#define FBNIC_TLV_ATTR_S64(id)		{ id, sizeof(s64), FBNIC_TLV_SIGNED }
#define FBNIC_TLV_ATTR_MAC_ADDR(id)	{ id, ETH_ALEN, FBNIC_TLV_BINARY }
#define FBNIC_TLV_ATTR_NESTED(id)	{ id, 0, FBNIC_TLV_NESTED }
#define FBNIC_TLV_ATTR_ARRAY(id)	{ id, 0, FBNIC_TLV_ARRAY }
#define FBNIC_TLV_ATTR_RAW_DATA(id)	{ id, TLV_MAX_DATA, FBNIC_TLV_BINARY }
#define FBNIC_TLV_ATTR_LAST		{ FBNIC_TLV_ATTR_ID_UNKNOWN, 0, 0 }

struct fbnic_tlv_parser {
	u16				id;
	const struct fbnic_tlv_index	*attr;
	int				(*func)(void *opaque,
						struct fbnic_tlv_msg **results);
};

#define FBNIC_TLV_PARSER(id, attr, func) { FBNIC_TLV_MSG_ID_##id, attr, func }

static inline void *
fbnic_tlv_attr_get_value_ptr(struct fbnic_tlv_msg *attr)
{
	return (void *)&attr->value[0];
}

static inline bool fbnic_tlv_attr_get_bool(struct fbnic_tlv_msg *attr)
{
	return !!attr;
}

u64 fbnic_tlv_attr_get_unsigned(struct fbnic_tlv_msg *attr, u64 def);
s64 fbnic_tlv_attr_get_signed(struct fbnic_tlv_msg *attr, s64 def);
ssize_t fbnic_tlv_attr_get_string(struct fbnic_tlv_msg *attr, char *dst,
				  size_t dstsize);
struct fbnic_tlv_msg *fbnic_tlv_msg_alloc(u16 msg_id);
int fbnic_tlv_attr_put_flag(struct fbnic_tlv_msg *msg, const u16 attr_id);
int fbnic_tlv_attr_put_value(struct fbnic_tlv_msg *msg, const u16 attr_id,
			     const void *value, const int len);
int __fbnic_tlv_attr_put_int(struct fbnic_tlv_msg *msg, const u16 attr_id,
			     s64 value, const int len);
#define fbnic_tlv_attr_put_int(msg, attr_id, value) \
	__fbnic_tlv_attr_put_int(msg, attr_id, value, \
				 FBNIC_TLV_MSG_ALIGN(sizeof(value)))
int fbnic_tlv_attr_put_mac_addr(struct fbnic_tlv_msg *msg, const u16 attr_id,
				const u8 *mac_addr);
int fbnic_tlv_attr_put_string(struct fbnic_tlv_msg *msg, u16 attr_id,
			      const char *string);
struct fbnic_tlv_msg *fbnic_tlv_attr_nest_start(struct fbnic_tlv_msg *msg,
						u16 attr_id);
void fbnic_tlv_attr_nest_stop(struct fbnic_tlv_msg *msg);
void fbnic_tlv_attr_addr_copy(u8 *dest, struct fbnic_tlv_msg *src);
int fbnic_tlv_attr_parse_array(struct fbnic_tlv_msg *attr, int len,
			       struct fbnic_tlv_msg **results,
			       const struct fbnic_tlv_index *tlv_index,
			       u16 tlv_attr_id, size_t array_len);
int fbnic_tlv_attr_parse(struct fbnic_tlv_msg *attr, int len,
			 struct fbnic_tlv_msg **results,
			 const struct fbnic_tlv_index *tlv_index);
int fbnic_tlv_msg_parse(void *opaque, struct fbnic_tlv_msg *msg,
			const struct fbnic_tlv_parser *parser);
int fbnic_tlv_parser_error(void *opaque, struct fbnic_tlv_msg **results);

#define fta_get_uint(_results, _id) \
	fbnic_tlv_attr_get_unsigned(_results[_id], 0)
#define fta_get_sint(_results, _id) \
	fbnic_tlv_attr_get_signed(_results[_id], 0)
#define fta_get_str(_results, _id, _dst, _dstsize) \
	fbnic_tlv_attr_get_string(_results[_id], _dst, _dstsize)

#define FBNIC_TLV_MSG_ERROR \
	FBNIC_TLV_PARSER(UNKNOWN, NULL, fbnic_tlv_parser_error)
#endif /* _FBNIC_TLV_H_ */
