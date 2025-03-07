// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/once.h>
#include <linux/random.h>
#include <linux/string.h>
#include <uapi/linux/if_ether.h>

#include "fbnic_tlv.h"

/**
 * fbnic_tlv_msg_alloc - Allocate page and initialize FW message header
 * @msg_id: Identifier for new message we are starting
 *
 * Return: pointer to start of message, or NULL on failure.
 *
 * Allocates a page and initializes message header at start of page.
 * Initial message size is 1 DWORD which is just the header.
 **/
struct fbnic_tlv_msg *fbnic_tlv_msg_alloc(u16 msg_id)
{
	struct fbnic_tlv_hdr hdr = { 0 };
	struct fbnic_tlv_msg *msg;

	msg = (struct fbnic_tlv_msg *)__get_free_page(GFP_KERNEL);
	if (!msg)
		return NULL;

	/* Start with zero filled header and then back fill with data */
	hdr.type = msg_id;
	hdr.is_msg = 1;
	hdr.len = cpu_to_le16(1);

	/* Copy header into start of message */
	msg->hdr = hdr;

	return msg;
}

/**
 * fbnic_tlv_attr_put_flag - Add flag value to message
 * @msg: Message header we are adding flag attribute to
 * @attr_id: ID of flag attribute we are adding to message
 *
 * Return: -ENOSPC if there is no room for the attribute. Otherwise 0.
 *
 * Adds a 1 DWORD flag attribute to the message. The presence of this
 * attribute can be used as a boolean value indicating true, otherwise the
 * value is considered false.
 **/
int fbnic_tlv_attr_put_flag(struct fbnic_tlv_msg *msg, const u16 attr_id)
{
	int attr_max_len = PAGE_SIZE - offset_in_page(msg) - sizeof(*msg);
	struct fbnic_tlv_hdr hdr = { 0 };
	struct fbnic_tlv_msg *attr;

	attr_max_len -= le16_to_cpu(msg->hdr.len) * sizeof(u32);
	if (attr_max_len < sizeof(*attr))
		return -ENOSPC;

	/* Get header pointer and bump attr to start of data */
	attr = &msg[le16_to_cpu(msg->hdr.len)];

	/* Record attribute type and size */
	hdr.type = attr_id;
	hdr.len = cpu_to_le16(sizeof(hdr));

	attr->hdr = hdr;
	le16_add_cpu(&msg->hdr.len,
		     FBNIC_TLV_MSG_SIZE(le16_to_cpu(hdr.len)));

	return 0;
}

/**
 * fbnic_tlv_attr_put_value - Add data to message
 * @msg: Message header we are adding flag attribute to
 * @attr_id: ID of flag attribute we are adding to message
 * @value: Pointer to data to be stored
 * @len: Size of data to be stored.
 *
 * Return: -ENOSPC if there is no room for the attribute. Otherwise 0.
 *
 * Adds header and copies data pointed to by value into the message. The
 * result is rounded up to the nearest DWORD for sizing so that the
 * headers remain aligned.
 *
 * The assumption is that the value field is in a format where byte
 * ordering can be guaranteed such as a byte array or a little endian
 * format.
 **/
int fbnic_tlv_attr_put_value(struct fbnic_tlv_msg *msg, const u16 attr_id,
			     const void *value, const int len)
{
	int attr_max_len = PAGE_SIZE - offset_in_page(msg) - sizeof(*msg);
	struct fbnic_tlv_hdr hdr = { 0 };
	struct fbnic_tlv_msg *attr;

	attr_max_len -= le16_to_cpu(msg->hdr.len) * sizeof(u32);
	if (attr_max_len < sizeof(*attr) + len)
		return -ENOSPC;

	/* Get header pointer and bump attr to start of data */
	attr = &msg[le16_to_cpu(msg->hdr.len)];

	/* Record attribute type and size */
	hdr.type = attr_id;
	hdr.len = cpu_to_le16(sizeof(hdr) + len);

	/* Zero pad end of region to be written if we aren't aligned */
	if (len % sizeof(hdr))
		attr->value[len / sizeof(hdr)] = 0;

	/* Copy data over */
	memcpy(attr->value, value, len);

	attr->hdr = hdr;
	le16_add_cpu(&msg->hdr.len,
		     FBNIC_TLV_MSG_SIZE(le16_to_cpu(hdr.len)));

	return 0;
}

/**
 * __fbnic_tlv_attr_put_int - Add integer to message
 * @msg: Message header we are adding flag attribute to
 * @attr_id: ID of flag attribute we are adding to message
 * @value: Data to be stored
 * @len: Size of data to be stored, either 4 or 8 bytes.
 *
 * Return: -ENOSPC if there is no room for the attribute. Otherwise 0.
 *
 * Adds header and copies data pointed to by value into the message. Will
 * format the data as little endian.
 **/
int __fbnic_tlv_attr_put_int(struct fbnic_tlv_msg *msg, const u16 attr_id,
			     s64 value, const int len)
{
	__le64 le64_value = cpu_to_le64(value);

	return fbnic_tlv_attr_put_value(msg, attr_id, &le64_value, len);
}

/**
 * fbnic_tlv_attr_put_mac_addr - Add mac_addr to message
 * @msg: Message header we are adding flag attribute to
 * @attr_id: ID of flag attribute we are adding to message
 * @mac_addr: Byte pointer to MAC address to be stored
 *
 * Return: -ENOSPC if there is no room for the attribute. Otherwise 0.
 *
 * Adds header and copies data pointed to by mac_addr into the message. Will
 * copy the address raw so it will be in big endian with start of MAC
 * address at start of attribute.
 **/
int fbnic_tlv_attr_put_mac_addr(struct fbnic_tlv_msg *msg, const u16 attr_id,
				const u8 *mac_addr)
{
	return fbnic_tlv_attr_put_value(msg, attr_id, mac_addr, ETH_ALEN);
}

/**
 * fbnic_tlv_attr_put_string - Add string to message
 * @msg: Message header we are adding flag attribute to
 * @attr_id: ID of flag attribute we are adding to message
 * @string: Byte pointer to null terminated string to be stored
 *
 * Return: -ENOSPC if there is no room for the attribute. Otherwise 0.
 *
 * Adds header and copies data pointed to by string into the message. Will
 * copy the address raw so it will be in byte order.
 **/
int fbnic_tlv_attr_put_string(struct fbnic_tlv_msg *msg, u16 attr_id,
			      const char *string)
{
	int attr_max_len = PAGE_SIZE - sizeof(*msg);
	int str_len = 1;

	/* The max length will be message minus existing message and new
	 * attribute header. Since the message is measured in DWORDs we have
	 * to multiply the size by 4.
	 *
	 * The string length doesn't include the \0 so we have to add one to
	 * the final value, so start with that as our initial value.
	 *
	 * We will verify if the string will fit in fbnic_tlv_attr_put_value()
	 */
	attr_max_len -= le16_to_cpu(msg->hdr.len) * sizeof(u32);
	str_len += strnlen(string, attr_max_len);

	return fbnic_tlv_attr_put_value(msg, attr_id, string, str_len);
}

/**
 * fbnic_tlv_attr_get_unsigned - Retrieve unsigned value from result
 * @attr: Attribute to retrieve data from
 * @def: The default value if attr is NULL
 *
 * Return: unsigned 64b value containing integer value
 **/
u64 fbnic_tlv_attr_get_unsigned(struct fbnic_tlv_msg *attr, u64 def)
{
	__le64 le64_value = 0;

	if (!attr)
		return def;

	memcpy(&le64_value, &attr->value[0],
	       le16_to_cpu(attr->hdr.len) - sizeof(*attr));

	return le64_to_cpu(le64_value);
}

/**
 * fbnic_tlv_attr_get_signed - Retrieve signed value from result
 * @attr: Attribute to retrieve data from
 * @def: The default value if attr is NULL
 *
 * Return: signed 64b value containing integer value
 **/
s64 fbnic_tlv_attr_get_signed(struct fbnic_tlv_msg *attr, s64 def)
{
	__le64 le64_value = 0;
	int shift;
	s64 value;

	if (!attr)
		return def;

	shift = (8 + sizeof(*attr) - le16_to_cpu(attr->hdr.len)) * 8;

	/* Copy the value and adjust for byte ordering */
	memcpy(&le64_value, &attr->value[0],
	       le16_to_cpu(attr->hdr.len) - sizeof(*attr));
	value = le64_to_cpu(le64_value);

	/* Sign extend the return value by using a pair of shifts */
	return (value << shift) >> shift;
}

/**
 * fbnic_tlv_attr_get_string - Retrieve string value from result
 * @attr: Attribute to retrieve data from
 * @dst: Pointer to an allocated string to store the data
 * @dstsize: The maximum size which can be in dst
 *
 * Return: the size of the string read from firmware or negative error.
 **/
ssize_t fbnic_tlv_attr_get_string(struct fbnic_tlv_msg *attr, char *dst,
				  size_t dstsize)
{
	size_t srclen, len;
	ssize_t ret;

	if (!attr)
		return -EINVAL;

	if (dstsize == 0)
		return -E2BIG;

	srclen = le16_to_cpu(attr->hdr.len) - sizeof(*attr);
	if (srclen > 0 && ((char *)attr->value)[srclen - 1] == '\0')
		srclen--;

	if (srclen >= dstsize) {
		len = dstsize - 1;
		ret = -E2BIG;
	} else {
		len = srclen;
		ret = len;
	}

	memcpy(dst, &attr->value, len);
	/* Zero pad end of dst. */
	memset(dst + len, 0, dstsize - len);

	return ret;
}

/**
 * fbnic_tlv_attr_nest_start - Add nested attribute header to message
 * @msg: Message header we are adding flag attribute to
 * @attr_id: ID of flag attribute we are adding to message
 *
 * Return: NULL if there is no room for the attribute. Otherwise a pointer
 * to the new attribute header.
 *
 * New header length is stored initially in DWORDs.
 **/
struct fbnic_tlv_msg *fbnic_tlv_attr_nest_start(struct fbnic_tlv_msg *msg,
						u16 attr_id)
{
	int attr_max_len = PAGE_SIZE - offset_in_page(msg) - sizeof(*msg);
	struct fbnic_tlv_msg *attr = &msg[le16_to_cpu(msg->hdr.len)];
	struct fbnic_tlv_hdr hdr = { 0 };

	/* Make sure we have space for at least the nest header plus one more */
	attr_max_len -= le16_to_cpu(msg->hdr.len) * sizeof(u32);
	if (attr_max_len < sizeof(*attr) * 2)
		return NULL;

	/* Record attribute type and size */
	hdr.type = attr_id;

	/* Add current message length to account for consumption within the
	 * page and leave it as a multiple of DWORDs, we will shift to
	 * bytes when we close it out.
	 */
	hdr.len = cpu_to_le16(1);

	attr->hdr = hdr;

	return attr;
}

/**
 * fbnic_tlv_attr_nest_stop - Close out nested attribute and add it to message
 * @msg: Message header we are adding flag attribute to
 *
 * Closes out nested attribute, adds length to message, and then bumps
 * length from DWORDs to bytes to match other attributes.
 **/
void fbnic_tlv_attr_nest_stop(struct fbnic_tlv_msg *msg)
{
	struct fbnic_tlv_msg *attr = &msg[le16_to_cpu(msg->hdr.len)];
	u16 len = le16_to_cpu(attr->hdr.len);

	/* Add attribute to message if there is more than just a header */
	if (len <= 1)
		return;

	le16_add_cpu(&msg->hdr.len, len);

	/* Convert from DWORDs to bytes */
	attr->hdr.len = cpu_to_le16(len * sizeof(u32));
}

static int
fbnic_tlv_attr_validate(struct fbnic_tlv_msg *attr,
			const struct fbnic_tlv_index *tlv_index)
{
	u16 len = le16_to_cpu(attr->hdr.len) - sizeof(*attr);
	u16 attr_id = attr->hdr.type;
	__le32 *value = &attr->value[0];

	if (attr->hdr.is_msg)
		return -EINVAL;

	if (attr_id >= FBNIC_TLV_RESULTS_MAX)
		return -EINVAL;

	while (tlv_index->id != attr_id) {
		if  (tlv_index->id == FBNIC_TLV_ATTR_ID_UNKNOWN) {
			if (attr->hdr.cannot_ignore)
				return -ENOENT;
			return le16_to_cpu(attr->hdr.len);
		}

		tlv_index++;
	}

	if (offset_in_page(attr) + len > PAGE_SIZE - sizeof(*attr))
		return -E2BIG;

	switch (tlv_index->type) {
	case FBNIC_TLV_STRING:
		if (!len || len > tlv_index->len)
			return -EINVAL;
		if (((char *)value)[len - 1])
			return -EINVAL;
		break;
	case FBNIC_TLV_FLAG:
		if (len)
			return -EINVAL;
		break;
	case FBNIC_TLV_UNSIGNED:
	case FBNIC_TLV_SIGNED:
		if (tlv_index->len > sizeof(__le64))
			return -EINVAL;
		fallthrough;
	case FBNIC_TLV_BINARY:
		if (!len || len > tlv_index->len)
			return -EINVAL;
		break;
	case FBNIC_TLV_NESTED:
	case FBNIC_TLV_ARRAY:
		if (len % 4)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * fbnic_tlv_attr_parse_array - Parse array of attributes into results array
 * @attr: Start of attributes in the message
 * @len: Length of attributes in the message
 * @results: Array of pointers to store the results of parsing
 * @tlv_index: List of TLV attributes to be parsed from message
 * @tlv_attr_id: Specific ID that is repeated in array
 * @array_len: Number of results to store in results array
 *
 * Return: zero on success, or negative value on error.
 *
 * Will take a list of attributes and a parser definition and will capture
 * the results in the results array to have the data extracted later.
 **/
int fbnic_tlv_attr_parse_array(struct fbnic_tlv_msg *attr, int len,
			       struct fbnic_tlv_msg **results,
			       const struct fbnic_tlv_index *tlv_index,
			       u16 tlv_attr_id, size_t array_len)
{
	int i = 0;

	/* Initialize results table to NULL. */
	memset(results, 0, array_len * sizeof(results[0]));

	/* Nothing to parse if header was only thing there */
	if (!len)
		return 0;

	/* Work through list of attributes, parsing them as necessary */
	while (len > 0) {
		u16 attr_id = attr->hdr.type;
		u16 attr_len;
		int err;

		if (tlv_attr_id != attr_id)
			return -EINVAL;

		/* Stop parsing on full error */
		err = fbnic_tlv_attr_validate(attr, tlv_index);
		if (err < 0)
			return err;

		if (i >= array_len)
			return -ENOSPC;

		results[i++] = attr;

		attr_len = FBNIC_TLV_MSG_SIZE(le16_to_cpu(attr->hdr.len));
		len -= attr_len;
		attr += attr_len;
	}

	return len == 0 ? 0 : -EINVAL;
}

/**
 * fbnic_tlv_attr_parse - Parse attributes into a list of attribute results
 * @attr: Start of attributes in the message
 * @len: Length of attributes in the message
 * @results: Array of pointers to store the results of parsing
 * @tlv_index: List of TLV attributes to be parsed from message
 *
 * Return: zero on success, or negative value on error.
 *
 * Will take a list of attributes and a parser definition and will capture
 * the results in the results array to have the data extracted later.
 **/
int fbnic_tlv_attr_parse(struct fbnic_tlv_msg *attr, int len,
			 struct fbnic_tlv_msg **results,
			 const struct fbnic_tlv_index *tlv_index)
{
	/* Initialize results table to NULL. */
	memset(results, 0, sizeof(results[0]) * FBNIC_TLV_RESULTS_MAX);

	/* Nothing to parse if header was only thing there */
	if (!len)
		return 0;

	/* Work through list of attributes, parsing them as necessary */
	while (len > 0) {
		int err = fbnic_tlv_attr_validate(attr, tlv_index);
		u16 attr_id = attr->hdr.type;
		u16 attr_len;

		/* Stop parsing on full error */
		if (err < 0)
			return err;

		/* Ignore results for unsupported values */
		if (!err) {
			/* Do not overwrite existing entries */
			if (results[attr_id])
				return -EADDRINUSE;

			results[attr_id] = attr;
		}

		attr_len = FBNIC_TLV_MSG_SIZE(le16_to_cpu(attr->hdr.len));
		len -= attr_len;
		attr += attr_len;
	}

	return len == 0 ? 0 : -EINVAL;
}

/**
 * fbnic_tlv_msg_parse - Parse message and process via predetermined functions
 * @opaque: Value passed to parser function to enable driver access
 * @msg: Message to be parsed.
 * @parser: TLV message parser definition.
 *
 * Return: zero on success, or negative value on error.
 *
 * Will take a message a number of message types via the attribute parsing
 * definitions and function provided for the parser array.
 **/
int fbnic_tlv_msg_parse(void *opaque, struct fbnic_tlv_msg *msg,
			const struct fbnic_tlv_parser *parser)
{
	struct fbnic_tlv_msg *results[FBNIC_TLV_RESULTS_MAX];
	u16 msg_id = msg->hdr.type;
	int err;

	if (!msg->hdr.is_msg)
		return -EINVAL;

	if (le16_to_cpu(msg->hdr.len) > PAGE_SIZE / sizeof(u32))
		return -E2BIG;

	while (parser->id != msg_id) {
		if (parser->id == FBNIC_TLV_MSG_ID_UNKNOWN)
			return -ENOENT;
		parser++;
	}

	err = fbnic_tlv_attr_parse(&msg[1], le16_to_cpu(msg->hdr.len) - 1,
				   results, parser->attr);
	if (err)
		return err;

	return parser->func(opaque, results);
}

/**
 * fbnic_tlv_parser_error - called if message doesn't match known type
 * @opaque: (unused)
 * @results: (unused)
 *
 * Return: -EBADMSG to indicate the message is an unsupported type
 **/
int fbnic_tlv_parser_error(void *opaque, struct fbnic_tlv_msg **results)
{
	return -EBADMSG;
}

void fbnic_tlv_attr_addr_copy(u8 *dest, struct fbnic_tlv_msg *src)
{
	u8 *mac_addr;

	mac_addr = fbnic_tlv_attr_get_value_ptr(src);
	memcpy(dest, mac_addr, ETH_ALEN);
}
