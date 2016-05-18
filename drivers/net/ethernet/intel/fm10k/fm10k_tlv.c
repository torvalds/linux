/* Intel(R) Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include "fm10k_tlv.h"

/**
 *  fm10k_tlv_msg_init - Initialize message block for TLV data storage
 *  @msg: Pointer to message block
 *  @msg_id: Message ID indicating message type
 *
 *  This function return success if provided with a valid message pointer
 **/
s32 fm10k_tlv_msg_init(u32 *msg, u16 msg_id)
{
	/* verify pointer is not NULL */
	if (!msg)
		return FM10K_ERR_PARAM;

	*msg = (FM10K_TLV_FLAGS_MSG << FM10K_TLV_FLAGS_SHIFT) | msg_id;

	return 0;
}

/**
 *  fm10k_tlv_attr_put_null_string - Place null terminated string on message
 *  @msg: Pointer to message block
 *  @attr_id: Attribute ID
 *  @string: Pointer to string to be stored in attribute
 *
 *  This function will reorder a string to be CPU endian and store it in
 *  the attribute buffer.  It will return success if provided with a valid
 *  pointers.
 **/
static s32 fm10k_tlv_attr_put_null_string(u32 *msg, u16 attr_id,
					  const unsigned char *string)
{
	u32 attr_data = 0, len = 0;
	u32 *attr;

	/* verify pointers are not NULL */
	if (!string || !msg)
		return FM10K_ERR_PARAM;

	attr = &msg[FM10K_TLV_DWORD_LEN(*msg)];

	/* copy string into local variable and then write to msg */
	do {
		/* write data to message */
		if (len && !(len % 4)) {
			attr[len / 4] = attr_data;
			attr_data = 0;
		}

		/* record character to offset location */
		attr_data |= (u32)(*string) << (8 * (len % 4));
		len++;

		/* test for NULL and then increment */
	} while (*(string++));

	/* write last piece of data to message */
	attr[(len + 3) / 4] = attr_data;

	/* record attribute header, update message length */
	len <<= FM10K_TLV_LEN_SHIFT;
	attr[0] = len | attr_id;

	/* add header length to length */
	len += FM10K_TLV_HDR_LEN << FM10K_TLV_LEN_SHIFT;
	*msg += FM10K_TLV_LEN_ALIGN(len);

	return 0;
}

/**
 *  fm10k_tlv_attr_get_null_string - Get null terminated string from attribute
 *  @attr: Pointer to attribute
 *  @string: Pointer to location of destination string
 *
 *  This function pulls the string back out of the attribute and will place
 *  it in the array pointed by by string.  It will return success if provided
 *  with a valid pointers.
 **/
static s32 fm10k_tlv_attr_get_null_string(u32 *attr, unsigned char *string)
{
	u32 len;

	/* verify pointers are not NULL */
	if (!string || !attr)
		return FM10K_ERR_PARAM;

	len = *attr >> FM10K_TLV_LEN_SHIFT;
	attr++;

	while (len--)
		string[len] = (u8)(attr[len / 4] >> (8 * (len % 4)));

	return 0;
}

/**
 *  fm10k_tlv_attr_put_mac_vlan - Store MAC/VLAN attribute in message
 *  @msg: Pointer to message block
 *  @attr_id: Attribute ID
 *  @mac_addr: MAC address to be stored
 *
 *  This function will reorder a MAC address to be CPU endian and store it
 *  in the attribute buffer.  It will return success if provided with a
 *  valid pointers.
 **/
s32 fm10k_tlv_attr_put_mac_vlan(u32 *msg, u16 attr_id,
				const u8 *mac_addr, u16 vlan)
{
	u32 len = ETH_ALEN << FM10K_TLV_LEN_SHIFT;
	u32 *attr;

	/* verify pointers are not NULL */
	if (!msg || !mac_addr)
		return FM10K_ERR_PARAM;

	attr = &msg[FM10K_TLV_DWORD_LEN(*msg)];

	/* record attribute header, update message length */
	attr[0] = len | attr_id;

	/* copy value into local variable and then write to msg */
	attr[1] = le32_to_cpu(*(const __le32 *)&mac_addr[0]);
	attr[2] = le16_to_cpu(*(const __le16 *)&mac_addr[4]);
	attr[2] |= (u32)vlan << 16;

	/* add header length to length */
	len += FM10K_TLV_HDR_LEN << FM10K_TLV_LEN_SHIFT;
	*msg += FM10K_TLV_LEN_ALIGN(len);

	return 0;
}

/**
 *  fm10k_tlv_attr_get_mac_vlan - Get MAC/VLAN stored in attribute
 *  @attr: Pointer to attribute
 *  @attr_id: Attribute ID
 *  @mac_addr: location of buffer to store MAC address
 *
 *  This function pulls the MAC address back out of the attribute and will
 *  place it in the array pointed by by mac_addr.  It will return success
 *  if provided with a valid pointers.
 **/
s32 fm10k_tlv_attr_get_mac_vlan(u32 *attr, u8 *mac_addr, u16 *vlan)
{
	/* verify pointers are not NULL */
	if (!mac_addr || !attr)
		return FM10K_ERR_PARAM;

	*(__le32 *)&mac_addr[0] = cpu_to_le32(attr[1]);
	*(__le16 *)&mac_addr[4] = cpu_to_le16((u16)(attr[2]));
	*vlan = (u16)(attr[2] >> 16);

	return 0;
}

/**
 *  fm10k_tlv_attr_put_bool - Add header indicating value "true"
 *  @msg: Pointer to message block
 *  @attr_id: Attribute ID
 *
 *  This function will simply add an attribute header, the fact
 *  that the header is here means the attribute value is true, else
 *  it is false.  The function will return success if provided with a
 *  valid pointers.
 **/
s32 fm10k_tlv_attr_put_bool(u32 *msg, u16 attr_id)
{
	/* verify pointers are not NULL */
	if (!msg)
		return FM10K_ERR_PARAM;

	/* record attribute header */
	msg[FM10K_TLV_DWORD_LEN(*msg)] = attr_id;

	/* add header length to length */
	*msg += FM10K_TLV_HDR_LEN << FM10K_TLV_LEN_SHIFT;

	return 0;
}

/**
 *  fm10k_tlv_attr_put_value - Store integer value attribute in message
 *  @msg: Pointer to message block
 *  @attr_id: Attribute ID
 *  @value: Value to be written
 *  @len: Size of value
 *
 *  This function will place an integer value of up to 8 bytes in size
 *  in a message attribute.  The function will return success provided
 *  that msg is a valid pointer, and len is 1, 2, 4, or 8.
 **/
s32 fm10k_tlv_attr_put_value(u32 *msg, u16 attr_id, s64 value, u32 len)
{
	u32 *attr;

	/* verify non-null msg and len is 1, 2, 4, or 8 */
	if (!msg || !len || len > 8 || (len & (len - 1)))
		return FM10K_ERR_PARAM;

	attr = &msg[FM10K_TLV_DWORD_LEN(*msg)];

	if (len < 4) {
		attr[1] = (u32)value & (BIT(8 * len) - 1);
	} else {
		attr[1] = (u32)value;
		if (len > 4)
			attr[2] = (u32)(value >> 32);
	}

	/* record attribute header, update message length */
	len <<= FM10K_TLV_LEN_SHIFT;
	attr[0] = len | attr_id;

	/* add header length to length */
	len += FM10K_TLV_HDR_LEN << FM10K_TLV_LEN_SHIFT;
	*msg += FM10K_TLV_LEN_ALIGN(len);

	return 0;
}

/**
 *  fm10k_tlv_attr_get_value - Get integer value stored in attribute
 *  @attr: Pointer to attribute
 *  @value: Pointer to destination buffer
 *  @len: Size of value
 *
 *  This function will place an integer value of up to 8 bytes in size
 *  in the offset pointed to by value.  The function will return success
 *  provided that pointers are valid and the len value matches the
 *  attribute length.
 **/
s32 fm10k_tlv_attr_get_value(u32 *attr, void *value, u32 len)
{
	/* verify pointers are not NULL */
	if (!attr || !value)
		return FM10K_ERR_PARAM;

	if ((*attr >> FM10K_TLV_LEN_SHIFT) != len)
		return FM10K_ERR_PARAM;

	if (len == 8)
		*(u64 *)value = ((u64)attr[2] << 32) | attr[1];
	else if (len == 4)
		*(u32 *)value = attr[1];
	else if (len == 2)
		*(u16 *)value = (u16)attr[1];
	else
		*(u8 *)value = (u8)attr[1];

	return 0;
}

/**
 *  fm10k_tlv_attr_put_le_struct - Store little endian structure in message
 *  @msg: Pointer to message block
 *  @attr_id: Attribute ID
 *  @le_struct: Pointer to structure to be written
 *  @len: Size of le_struct
 *
 *  This function will place a little endian structure value in a message
 *  attribute.  The function will return success provided that all pointers
 *  are valid and length is a non-zero multiple of 4.
 **/
s32 fm10k_tlv_attr_put_le_struct(u32 *msg, u16 attr_id,
				 const void *le_struct, u32 len)
{
	const __le32 *le32_ptr = (const __le32 *)le_struct;
	u32 *attr;
	u32 i;

	/* verify non-null msg and len is in 32 bit words */
	if (!msg || !len || (len % 4))
		return FM10K_ERR_PARAM;

	attr = &msg[FM10K_TLV_DWORD_LEN(*msg)];

	/* copy le32 structure into host byte order at 32b boundaries */
	for (i = 0; i < (len / 4); i++)
		attr[i + 1] = le32_to_cpu(le32_ptr[i]);

	/* record attribute header, update message length */
	len <<= FM10K_TLV_LEN_SHIFT;
	attr[0] = len | attr_id;

	/* add header length to length */
	len += FM10K_TLV_HDR_LEN << FM10K_TLV_LEN_SHIFT;
	*msg += FM10K_TLV_LEN_ALIGN(len);

	return 0;
}

/**
 *  fm10k_tlv_attr_get_le_struct - Get little endian struct form attribute
 *  @attr: Pointer to attribute
 *  @le_struct: Pointer to structure to be written
 *  @len: Size of structure
 *
 *  This function will place a little endian structure in the buffer
 *  pointed to by le_struct.  The function will return success
 *  provided that pointers are valid and the len value matches the
 *  attribute length.
 **/
s32 fm10k_tlv_attr_get_le_struct(u32 *attr, void *le_struct, u32 len)
{
	__le32 *le32_ptr = (__le32 *)le_struct;
	u32 i;

	/* verify pointers are not NULL */
	if (!le_struct || !attr)
		return FM10K_ERR_PARAM;

	if ((*attr >> FM10K_TLV_LEN_SHIFT) != len)
		return FM10K_ERR_PARAM;

	attr++;

	for (i = 0; len; i++, len -= 4)
		le32_ptr[i] = cpu_to_le32(attr[i]);

	return 0;
}

/**
 *  fm10k_tlv_attr_nest_start - Start a set of nested attributes
 *  @msg: Pointer to message block
 *  @attr_id: Attribute ID
 *
 *  This function will mark off a new nested region for encapsulating
 *  a given set of attributes.  The idea is if you wish to place a secondary
 *  structure within the message this mechanism allows for that.  The
 *  function will return NULL on failure, and a pointer to the start
 *  of the nested attributes on success.
 **/
static u32 *fm10k_tlv_attr_nest_start(u32 *msg, u16 attr_id)
{
	u32 *attr;

	/* verify pointer is not NULL */
	if (!msg)
		return NULL;

	attr = &msg[FM10K_TLV_DWORD_LEN(*msg)];

	attr[0] = attr_id;

	/* return pointer to nest header */
	return attr;
}

/**
 *  fm10k_tlv_attr_nest_stop - Stop a set of nested attributes
 *  @msg: Pointer to message block
 *
 *  This function closes off an existing set of nested attributes.  The
 *  message pointer should be pointing to the parent of the nest.  So in
 *  the case of a nest within the nest this would be the outer nest pointer.
 *  This function will return success provided all pointers are valid.
 **/
static s32 fm10k_tlv_attr_nest_stop(u32 *msg)
{
	u32 *attr;
	u32 len;

	/* verify pointer is not NULL */
	if (!msg)
		return FM10K_ERR_PARAM;

	/* locate the nested header and retrieve its length */
	attr = &msg[FM10K_TLV_DWORD_LEN(*msg)];
	len = (attr[0] >> FM10K_TLV_LEN_SHIFT) << FM10K_TLV_LEN_SHIFT;

	/* only include nest if data was added to it */
	if (len) {
		len += FM10K_TLV_HDR_LEN << FM10K_TLV_LEN_SHIFT;
		*msg += len;
	}

	return 0;
}

/**
 *  fm10k_tlv_attr_validate - Validate attribute metadata
 *  @attr: Pointer to attribute
 *  @tlv_attr: Type and length info for attribute
 *
 *  This function does some basic validation of the input TLV.  It
 *  verifies the length, and in the case of null terminated strings
 *  it verifies that the last byte is null.  The function will
 *  return FM10K_ERR_PARAM if any attribute is malformed, otherwise
 *  it returns 0.
 **/
static s32 fm10k_tlv_attr_validate(u32 *attr,
				   const struct fm10k_tlv_attr *tlv_attr)
{
	u32 attr_id = *attr & FM10K_TLV_ID_MASK;
	u16 len = *attr >> FM10K_TLV_LEN_SHIFT;

	/* verify this is an attribute and not a message */
	if (*attr & (FM10K_TLV_FLAGS_MSG << FM10K_TLV_FLAGS_SHIFT))
		return FM10K_ERR_PARAM;

	/* search through the list of attributes to find a matching ID */
	while (tlv_attr->id < attr_id)
		tlv_attr++;

	/* if didn't find a match then we should exit */
	if (tlv_attr->id != attr_id)
		return FM10K_NOT_IMPLEMENTED;

	/* move to start of attribute data */
	attr++;

	switch (tlv_attr->type) {
	case FM10K_TLV_NULL_STRING:
		if (!len ||
		    (attr[(len - 1) / 4] & (0xFF << (8 * ((len - 1) % 4)))))
			return FM10K_ERR_PARAM;
		if (len > tlv_attr->len)
			return FM10K_ERR_PARAM;
		break;
	case FM10K_TLV_MAC_ADDR:
		if (len != ETH_ALEN)
			return FM10K_ERR_PARAM;
		break;
	case FM10K_TLV_BOOL:
		if (len)
			return FM10K_ERR_PARAM;
		break;
	case FM10K_TLV_UNSIGNED:
	case FM10K_TLV_SIGNED:
		if (len != tlv_attr->len)
			return FM10K_ERR_PARAM;
		break;
	case FM10K_TLV_LE_STRUCT:
		/* struct must be 4 byte aligned */
		if ((len % 4) || len != tlv_attr->len)
			return FM10K_ERR_PARAM;
		break;
	case FM10K_TLV_NESTED:
		/* nested attributes must be 4 byte aligned */
		if (len % 4)
			return FM10K_ERR_PARAM;
		break;
	default:
		/* attribute id is mapped to bad value */
		return FM10K_ERR_PARAM;
	}

	return 0;
}

/**
 *  fm10k_tlv_attr_parse - Parses stream of attribute data
 *  @attr: Pointer to attribute list
 *  @results: Pointer array to store pointers to attributes
 *  @tlv_attr: Type and length info for attributes
 *
 *  This function validates a stream of attributes and parses them
 *  up into an array of pointers stored in results.  The function will
 *  return FM10K_ERR_PARAM on any input or message error,
 *  FM10K_NOT_IMPLEMENTED for any attribute that is outside of the array
 *  and 0 on success. Any attributes not found in tlv_attr will be silently
 *  ignored.
 **/
static s32 fm10k_tlv_attr_parse(u32 *attr, u32 **results,
				const struct fm10k_tlv_attr *tlv_attr)
{
	u32 i, attr_id, offset = 0;
	s32 err = 0;
	u16 len;

	/* verify pointers are not NULL */
	if (!attr || !results)
		return FM10K_ERR_PARAM;

	/* initialize results to NULL */
	for (i = 0; i < FM10K_TLV_RESULTS_MAX; i++)
		results[i] = NULL;

	/* pull length from the message header */
	len = *attr >> FM10K_TLV_LEN_SHIFT;

	/* no attributes to parse if there is no length */
	if (!len)
		return 0;

	/* no attributes to parse, just raw data, message becomes attribute */
	if (!tlv_attr) {
		results[0] = attr;
		return 0;
	}

	/* move to start of attribute data */
	attr++;

	/* run through list parsing all attributes */
	while (offset < len) {
		attr_id = *attr & FM10K_TLV_ID_MASK;

		if (attr_id >= FM10K_TLV_RESULTS_MAX)
			return FM10K_NOT_IMPLEMENTED;

		err = fm10k_tlv_attr_validate(attr, tlv_attr);
		if (err == FM10K_NOT_IMPLEMENTED)
			; /* silently ignore non-implemented attributes */
		else if (err)
			return err;
		else
			results[attr_id] = attr;

		/* update offset */
		offset += FM10K_TLV_DWORD_LEN(*attr) * 4;

		/* move to next attribute */
		attr = &attr[FM10K_TLV_DWORD_LEN(*attr)];
	}

	/* we should find ourselves at the end of the list */
	if (offset != len)
		return FM10K_ERR_PARAM;

	return 0;
}

/**
 *  fm10k_tlv_msg_parse - Parses message header and calls function handler
 *  @hw: Pointer to hardware structure
 *  @msg: Pointer to message
 *  @mbx: Pointer to mailbox information structure
 *  @func: Function array containing list of message handling functions
 *
 *  This function should be the first function called upon receiving a
 *  message.  The handler will identify the message type and call the correct
 *  handler for the given message.  It will return the value from the function
 *  call on a recognized message type, otherwise it will return
 *  FM10K_NOT_IMPLEMENTED on an unrecognized type.
 **/
s32 fm10k_tlv_msg_parse(struct fm10k_hw *hw, u32 *msg,
			struct fm10k_mbx_info *mbx,
			const struct fm10k_msg_data *data)
{
	u32 *results[FM10K_TLV_RESULTS_MAX];
	u32 msg_id;
	s32 err;

	/* verify pointer is not NULL */
	if (!msg || !data)
		return FM10K_ERR_PARAM;

	/* verify this is a message and not an attribute */
	if (!(*msg & (FM10K_TLV_FLAGS_MSG << FM10K_TLV_FLAGS_SHIFT)))
		return FM10K_ERR_PARAM;

	/* grab message ID */
	msg_id = *msg & FM10K_TLV_ID_MASK;

	while (data->id < msg_id)
		data++;

	/* if we didn't find it then pass it up as an error */
	if (data->id != msg_id) {
		while (data->id != FM10K_TLV_ERROR)
			data++;
	}

	/* parse the attributes into the results list */
	err = fm10k_tlv_attr_parse(msg, results, data->attr);
	if (err < 0)
		return err;

	return data->func(hw, results, mbx);
}

/**
 *  fm10k_tlv_msg_error - Default handler for unrecognized TLV message IDs
 *  @hw: Pointer to hardware structure
 *  @results: Pointer array to message, results[0] is pointer to message
 *  @mbx: Unused mailbox pointer
 *
 *  This function is a default handler for unrecognized messages.  At a
 *  a minimum it just indicates that the message requested was
 *  unimplemented.
 **/
s32 fm10k_tlv_msg_error(struct fm10k_hw *hw, u32 **results,
			struct fm10k_mbx_info *mbx)
{
	return FM10K_NOT_IMPLEMENTED;
}

static const unsigned char test_str[] =	"fm10k";
static const unsigned char test_mac[ETH_ALEN] = { 0x12, 0x34, 0x56,
						  0x78, 0x9a, 0xbc };
static const u16 test_vlan = 0x0FED;
static const u64 test_u64 = 0xfedcba9876543210ull;
static const u32 test_u32 = 0x87654321;
static const u16 test_u16 = 0x8765;
static const u8  test_u8  = 0x87;
static const s64 test_s64 = -0x123456789abcdef0ll;
static const s32 test_s32 = -0x1235678;
static const s16 test_s16 = -0x1234;
static const s8  test_s8  = -0x12;
static const __le32 test_le[2] = { cpu_to_le32(0x12345678),
				   cpu_to_le32(0x9abcdef0)};

/* The message below is meant to be used as a test message to demonstrate
 * how to use the TLV interface and to test the types.  Normally this code
 * be compiled out by stripping the code wrapped in FM10K_TLV_TEST_MSG
 */
const struct fm10k_tlv_attr fm10k_tlv_msg_test_attr[] = {
	FM10K_TLV_ATTR_NULL_STRING(FM10K_TEST_MSG_STRING, 80),
	FM10K_TLV_ATTR_MAC_ADDR(FM10K_TEST_MSG_MAC_ADDR),
	FM10K_TLV_ATTR_U8(FM10K_TEST_MSG_U8),
	FM10K_TLV_ATTR_U16(FM10K_TEST_MSG_U16),
	FM10K_TLV_ATTR_U32(FM10K_TEST_MSG_U32),
	FM10K_TLV_ATTR_U64(FM10K_TEST_MSG_U64),
	FM10K_TLV_ATTR_S8(FM10K_TEST_MSG_S8),
	FM10K_TLV_ATTR_S16(FM10K_TEST_MSG_S16),
	FM10K_TLV_ATTR_S32(FM10K_TEST_MSG_S32),
	FM10K_TLV_ATTR_S64(FM10K_TEST_MSG_S64),
	FM10K_TLV_ATTR_LE_STRUCT(FM10K_TEST_MSG_LE_STRUCT, 8),
	FM10K_TLV_ATTR_NESTED(FM10K_TEST_MSG_NESTED),
	FM10K_TLV_ATTR_S32(FM10K_TEST_MSG_RESULT),
	FM10K_TLV_ATTR_LAST
};

/**
 *  fm10k_tlv_msg_test_generate_data - Stuff message with data
 *  @msg: Pointer to message
 *  @attr_flags: List of flags indicating what attributes to add
 *
 *  This function is meant to load a message buffer with attribute data
 **/
static void fm10k_tlv_msg_test_generate_data(u32 *msg, u32 attr_flags)
{
	if (attr_flags & BIT(FM10K_TEST_MSG_STRING))
		fm10k_tlv_attr_put_null_string(msg, FM10K_TEST_MSG_STRING,
					       test_str);
	if (attr_flags & BIT(FM10K_TEST_MSG_MAC_ADDR))
		fm10k_tlv_attr_put_mac_vlan(msg, FM10K_TEST_MSG_MAC_ADDR,
					    test_mac, test_vlan);
	if (attr_flags & BIT(FM10K_TEST_MSG_U8))
		fm10k_tlv_attr_put_u8(msg, FM10K_TEST_MSG_U8,  test_u8);
	if (attr_flags & BIT(FM10K_TEST_MSG_U16))
		fm10k_tlv_attr_put_u16(msg, FM10K_TEST_MSG_U16, test_u16);
	if (attr_flags & BIT(FM10K_TEST_MSG_U32))
		fm10k_tlv_attr_put_u32(msg, FM10K_TEST_MSG_U32, test_u32);
	if (attr_flags & BIT(FM10K_TEST_MSG_U64))
		fm10k_tlv_attr_put_u64(msg, FM10K_TEST_MSG_U64, test_u64);
	if (attr_flags & BIT(FM10K_TEST_MSG_S8))
		fm10k_tlv_attr_put_s8(msg, FM10K_TEST_MSG_S8,  test_s8);
	if (attr_flags & BIT(FM10K_TEST_MSG_S16))
		fm10k_tlv_attr_put_s16(msg, FM10K_TEST_MSG_S16, test_s16);
	if (attr_flags & BIT(FM10K_TEST_MSG_S32))
		fm10k_tlv_attr_put_s32(msg, FM10K_TEST_MSG_S32, test_s32);
	if (attr_flags & BIT(FM10K_TEST_MSG_S64))
		fm10k_tlv_attr_put_s64(msg, FM10K_TEST_MSG_S64, test_s64);
	if (attr_flags & BIT(FM10K_TEST_MSG_LE_STRUCT))
		fm10k_tlv_attr_put_le_struct(msg, FM10K_TEST_MSG_LE_STRUCT,
					     test_le, 8);
}

/**
 *  fm10k_tlv_msg_test_create - Create a test message testing all attributes
 *  @msg: Pointer to message
 *  @attr_flags: List of flags indicating what attributes to add
 *
 *  This function is meant to load a message buffer with all attribute types
 *  including a nested attribute.
 **/
void fm10k_tlv_msg_test_create(u32 *msg, u32 attr_flags)
{
	u32 *nest = NULL;

	fm10k_tlv_msg_init(msg, FM10K_TLV_MSG_ID_TEST);

	fm10k_tlv_msg_test_generate_data(msg, attr_flags);

	/* check for nested attributes */
	attr_flags >>= FM10K_TEST_MSG_NESTED;

	if (attr_flags) {
		nest = fm10k_tlv_attr_nest_start(msg, FM10K_TEST_MSG_NESTED);

		fm10k_tlv_msg_test_generate_data(nest, attr_flags);

		fm10k_tlv_attr_nest_stop(msg);
	}
}

/**
 *  fm10k_tlv_msg_test - Validate all results on test message receive
 *  @hw: Pointer to hardware structure
 *  @results: Pointer array to attributes in the message
 *  @mbx: Pointer to mailbox information structure
 *
 *  This function does a check to verify all attributes match what the test
 *  message placed in the message buffer.  It is the default handler
 *  for TLV test messages.
 **/
s32 fm10k_tlv_msg_test(struct fm10k_hw *hw, u32 **results,
		       struct fm10k_mbx_info *mbx)
{
	u32 *nest_results[FM10K_TLV_RESULTS_MAX];
	unsigned char result_str[80];
	unsigned char result_mac[ETH_ALEN];
	s32 err = 0;
	__le32 result_le[2];
	u16 result_vlan;
	u64 result_u64;
	u32 result_u32;
	u16 result_u16;
	u8  result_u8;
	s64 result_s64;
	s32 result_s32;
	s16 result_s16;
	s8  result_s8;
	u32 reply[3];

	/* retrieve results of a previous test */
	if (!!results[FM10K_TEST_MSG_RESULT])
		return fm10k_tlv_attr_get_s32(results[FM10K_TEST_MSG_RESULT],
					      &mbx->test_result);

parse_nested:
	if (!!results[FM10K_TEST_MSG_STRING]) {
		err = fm10k_tlv_attr_get_null_string(
					results[FM10K_TEST_MSG_STRING],
					result_str);
		if (!err && memcmp(test_str, result_str, sizeof(test_str)))
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_MAC_ADDR]) {
		err = fm10k_tlv_attr_get_mac_vlan(
					results[FM10K_TEST_MSG_MAC_ADDR],
					result_mac, &result_vlan);
		if (!err && !ether_addr_equal(test_mac, result_mac))
			err = FM10K_ERR_INVALID_VALUE;
		if (!err && test_vlan != result_vlan)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_U8]) {
		err = fm10k_tlv_attr_get_u8(results[FM10K_TEST_MSG_U8],
					    &result_u8);
		if (!err && test_u8 != result_u8)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_U16]) {
		err = fm10k_tlv_attr_get_u16(results[FM10K_TEST_MSG_U16],
					     &result_u16);
		if (!err && test_u16 != result_u16)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_U32]) {
		err = fm10k_tlv_attr_get_u32(results[FM10K_TEST_MSG_U32],
					     &result_u32);
		if (!err && test_u32 != result_u32)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_U64]) {
		err = fm10k_tlv_attr_get_u64(results[FM10K_TEST_MSG_U64],
					     &result_u64);
		if (!err && test_u64 != result_u64)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_S8]) {
		err = fm10k_tlv_attr_get_s8(results[FM10K_TEST_MSG_S8],
					    &result_s8);
		if (!err && test_s8 != result_s8)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_S16]) {
		err = fm10k_tlv_attr_get_s16(results[FM10K_TEST_MSG_S16],
					     &result_s16);
		if (!err && test_s16 != result_s16)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_S32]) {
		err = fm10k_tlv_attr_get_s32(results[FM10K_TEST_MSG_S32],
					     &result_s32);
		if (!err && test_s32 != result_s32)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_S64]) {
		err = fm10k_tlv_attr_get_s64(results[FM10K_TEST_MSG_S64],
					     &result_s64);
		if (!err && test_s64 != result_s64)
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}
	if (!!results[FM10K_TEST_MSG_LE_STRUCT]) {
		err = fm10k_tlv_attr_get_le_struct(
					results[FM10K_TEST_MSG_LE_STRUCT],
					result_le,
					sizeof(result_le));
		if (!err && memcmp(test_le, result_le, sizeof(test_le)))
			err = FM10K_ERR_INVALID_VALUE;
		if (err)
			goto report_result;
	}

	if (!!results[FM10K_TEST_MSG_NESTED]) {
		/* clear any pointers */
		memset(nest_results, 0, sizeof(nest_results));

		/* parse the nested attributes into the nest results list */
		err = fm10k_tlv_attr_parse(results[FM10K_TEST_MSG_NESTED],
					   nest_results,
					   fm10k_tlv_msg_test_attr);
		if (err)
			goto report_result;

		/* loop back through to the start */
		results = nest_results;
		goto parse_nested;
	}

report_result:
	/* generate reply with test result */
	fm10k_tlv_msg_init(reply, FM10K_TLV_MSG_ID_TEST);
	fm10k_tlv_attr_put_s32(reply, FM10K_TEST_MSG_RESULT, err);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, reply);
}
