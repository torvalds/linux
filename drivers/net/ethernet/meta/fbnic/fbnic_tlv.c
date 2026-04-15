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

#define FBNIC_TLV_TEST_STRING_LEN	32

struct fbnic_tlv_test {
	u64	test_u64;
	s64	test_s64;
	u32	test_u32;
	s32	test_s32;
	u16	test_u16;
	s16	test_s16;
	u8	test_mac[ETH_ALEN];
	u8	test_mac_array[4][ETH_ALEN];
	u8	test_true;
	u8	test_false;
	char	test_string[FBNIC_TLV_TEST_STRING_LEN];
};

static struct fbnic_tlv_test test_struct;

const struct fbnic_tlv_index fbnic_tlv_test_index[] = {
	FBNIC_TLV_ATTR_U64(FBNIC_TLV_TEST_MSG_U64),
	FBNIC_TLV_ATTR_S64(FBNIC_TLV_TEST_MSG_S64),
	FBNIC_TLV_ATTR_U32(FBNIC_TLV_TEST_MSG_U32),
	FBNIC_TLV_ATTR_S32(FBNIC_TLV_TEST_MSG_S32),
	FBNIC_TLV_ATTR_U32(FBNIC_TLV_TEST_MSG_U16),
	FBNIC_TLV_ATTR_S32(FBNIC_TLV_TEST_MSG_S16),
	FBNIC_TLV_ATTR_MAC_ADDR(FBNIC_TLV_TEST_MSG_MAC_ADDR),
	FBNIC_TLV_ATTR_FLAG(FBNIC_TLV_TEST_MSG_FLAG_TRUE),
	FBNIC_TLV_ATTR_FLAG(FBNIC_TLV_TEST_MSG_FLAG_FALSE),
	FBNIC_TLV_ATTR_STRING(FBNIC_TLV_TEST_MSG_STRING,
			      FBNIC_TLV_TEST_STRING_LEN),
	FBNIC_TLV_ATTR_ARRAY(FBNIC_TLV_TEST_MSG_ARRAY),
	FBNIC_TLV_ATTR_NESTED(FBNIC_TLV_TEST_MSG_NESTED),
	FBNIC_TLV_ATTR_LAST
};

static void fbnic_tlv_test_struct_init(void)
{
	int i = FBNIC_TLV_TEST_STRING_LEN - 1;

	/* Populate the struct with random data */
	get_random_once(&test_struct,
			offsetof(struct fbnic_tlv_test, test_string) + i);

	/* Force true/false to their expected values */
	test_struct.test_false = false;
	test_struct.test_true = true;

	/* Convert test_string to a true ASCII string */
	test_struct.test_string[i] = '\0';
	while (i--) {
		/* Force characters into displayable range */
		if (test_struct.test_string[i] < 64 ||
		    test_struct.test_string[i] >= 96) {
			test_struct.test_string[i] %= 32;
			test_struct.test_string[i] += 64;
		}
	}
}

static int fbnic_tlv_test_attr_data(struct fbnic_tlv_msg *msg)
{
	struct fbnic_tlv_msg *array;
	int err, i;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_TLV_TEST_MSG_U64,
				     test_struct.test_u64);
	if (err)
		return err;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_TLV_TEST_MSG_S64,
				     test_struct.test_s64);
	if (err)
		return err;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_TLV_TEST_MSG_U32,
				     test_struct.test_u32);
	if (err)
		return err;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_TLV_TEST_MSG_S32,
				     test_struct.test_s32);
	if (err)
		return err;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_TLV_TEST_MSG_U16,
				     test_struct.test_u16);
	if (err)
		return err;

	err = fbnic_tlv_attr_put_int(msg, FBNIC_TLV_TEST_MSG_S16,
				     test_struct.test_s16);
	if (err)
		return err;

	err = fbnic_tlv_attr_put_value(msg, FBNIC_TLV_TEST_MSG_MAC_ADDR,
				       test_struct.test_mac, ETH_ALEN);
	if (err)
		return err;

	/* Start MAC address array */
	array = fbnic_tlv_attr_nest_start(msg, FBNIC_TLV_TEST_MSG_ARRAY);
	if (!array)
		return -ENOSPC;

	for (i = 0; i < 4; i++) {
		err = fbnic_tlv_attr_put_value(array,
					       FBNIC_TLV_TEST_MSG_MAC_ADDR,
					       test_struct.test_mac_array[i],
					       ETH_ALEN);
		if (err)
			return err;
	}

	/* Close array */
	fbnic_tlv_attr_nest_stop(msg);

	err = fbnic_tlv_attr_put_flag(msg, FBNIC_TLV_TEST_MSG_FLAG_TRUE);
	if (err)
		return err;

	return fbnic_tlv_attr_put_string(msg, FBNIC_TLV_TEST_MSG_STRING,
					 test_struct.test_string);
}

/**
 * fbnic_tlv_test_create - Allocate a test message and fill it w/ data
 * @fbd: FBNIC device structure
 *
 * Return: NULL on failure to allocate or pointer to new TLV test message.
 **/
struct fbnic_tlv_msg *fbnic_tlv_test_create(struct fbnic_dev *fbd)
{
	struct fbnic_tlv_msg *msg, *nest;
	int err;

	msg = fbnic_tlv_msg_alloc(FBNIC_TLV_MSG_ID_TEST);
	if (!msg)
		return NULL;

	/* Randomize struct data */
	fbnic_tlv_test_struct_init();

	/* Add first level of data to message */
	err = fbnic_tlv_test_attr_data(msg);
	if (err)
		goto free_message;

	/* Start second level nested */
	nest = fbnic_tlv_attr_nest_start(msg, FBNIC_TLV_TEST_MSG_NESTED);
	if (!nest)
		goto free_message;

	/* Add nested data */
	err = fbnic_tlv_test_attr_data(nest);
	if (err)
		goto free_message;

	/* Close nest and report full message */
	fbnic_tlv_attr_nest_stop(msg);

	return msg;
free_message:
	free_page((unsigned long)msg);
	return NULL;
}

void fbnic_tlv_attr_addr_copy(u8 *dest, struct fbnic_tlv_msg *src)
{
	u8 *mac_addr;

	mac_addr = fbnic_tlv_attr_get_value_ptr(src);
	memcpy(dest, mac_addr, ETH_ALEN);
}

/**
 * fbnic_tlv_parser_test_attr - Function loading test attributes into structure
 * @str: Test structure to load
 * @results: Pointer to results array
 *
 * Copies attributes into structure. Any attribute that doesn't exist in the
 * results array is not populated.
 **/
static void fbnic_tlv_parser_test_attr(struct fbnic_tlv_test *str,
				       struct fbnic_tlv_msg **results)
{
	struct fbnic_tlv_msg *array_results[4];
	struct fbnic_tlv_msg *attr;
	char *string = NULL;
	int i, err;

	str->test_u64 = fta_get_uint(results, FBNIC_TLV_TEST_MSG_U64);
	str->test_u32 = fta_get_uint(results, FBNIC_TLV_TEST_MSG_U32);
	str->test_u16 = fta_get_uint(results, FBNIC_TLV_TEST_MSG_U16);

	str->test_s64 = fta_get_sint(results, FBNIC_TLV_TEST_MSG_S64);
	str->test_s32 = fta_get_sint(results, FBNIC_TLV_TEST_MSG_S32);
	str->test_s16 = fta_get_sint(results, FBNIC_TLV_TEST_MSG_S16);

	attr = results[FBNIC_TLV_TEST_MSG_MAC_ADDR];
	if (attr)
		fbnic_tlv_attr_addr_copy(str->test_mac, attr);

	attr = results[FBNIC_TLV_TEST_MSG_ARRAY];
	if (attr) {
		int len = le16_to_cpu(attr->hdr.len) / sizeof(u32) - 1;

		err = fbnic_tlv_attr_parse_array(&attr[1], len,
						 array_results,
						 fbnic_tlv_test_index,
						 FBNIC_TLV_TEST_MSG_MAC_ADDR,
						 4);
		if (!err) {
			for (i = 0; i < 4 && array_results[i]; i++)
				fbnic_tlv_attr_addr_copy(str->test_mac_array[i],
							 array_results[i]);
		}
	}

	str->test_true = !!results[FBNIC_TLV_TEST_MSG_FLAG_TRUE];
	str->test_false = !!results[FBNIC_TLV_TEST_MSG_FLAG_FALSE];

	attr = results[FBNIC_TLV_TEST_MSG_STRING];
	if (attr) {
		string = fbnic_tlv_attr_get_value_ptr(attr);
		strscpy(str->test_string, string, FBNIC_TLV_TEST_STRING_LEN);
	}
}

static void fbnic_tlv_test_dump(struct fbnic_tlv_test *value, char *prefix)
{
	print_hex_dump(KERN_INFO, prefix, DUMP_PREFIX_OFFSET, 16, 1,
		       value, sizeof(*value), true);
}

/**
 * fbnic_tlv_parser_test - Function for parsing and testing test message
 * @opaque: Unused value
 * @results: Results of parser output
 *
 * Return: negative value on error, or 0 on success.
 *
 * Parses attributes to structures and compares the structure to the
 * expected test value that should have been used to populate the message.
 *
 * Used to verify message generation and parser are working correctly.
 **/
int fbnic_tlv_parser_test(void *opaque, struct fbnic_tlv_msg **results)
{
	struct fbnic_tlv_msg *nest_results[FBNIC_TLV_RESULTS_MAX] = { 0 };
	struct fbnic_tlv_test result_struct;
	struct fbnic_tlv_msg *attr;
	int err;

	memset(&result_struct, 0, sizeof(result_struct));
	fbnic_tlv_parser_test_attr(&result_struct, results);

	if (memcmp(&test_struct, &result_struct, sizeof(test_struct))) {
		fbnic_tlv_test_dump(&result_struct, "fbnic: found - ");
		fbnic_tlv_test_dump(&test_struct, "fbnic: expected - ");
		return -EINVAL;
	}

	attr = results[FBNIC_TLV_TEST_MSG_NESTED];
	if (!attr)
		return -EINVAL;

	err = fbnic_tlv_attr_parse(&attr[1],
				   le16_to_cpu(attr->hdr.len) / sizeof(u32) - 1,
				   nest_results, fbnic_tlv_test_index);
	if (err)
		return err;

	memset(&result_struct, 0, sizeof(result_struct));
	fbnic_tlv_parser_test_attr(&result_struct, nest_results);

	if (memcmp(&test_struct, &result_struct, sizeof(test_struct))) {
		fbnic_tlv_test_dump(&result_struct, "fbnic: found - ");
		fbnic_tlv_test_dump(&test_struct, "fbnic: expected - ");
		return -EINVAL;
	}

	return 0;
}
