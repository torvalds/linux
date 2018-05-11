/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _FM10K_TLV_H_
#define _FM10K_TLV_H_

/* forward declaration */
struct fm10k_msg_data;

#include "fm10k_type.h"

/* Message / Argument header format
 *    3			  2		      1			  0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |	     Length	   | Flags |	      Type / ID		   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * The message header format described here is used for messages that are
 * passed between the PF and the VF.  To allow for messages larger then
 * mailbox size we will provide a message with the above header and it
 * will be segmented and transported to the mailbox to the other side where
 * it is reassembled.  It contains the following fields:
 * Length: Length of the message in bytes excluding the message header
 * Flags: TBD
 * Type/ID: These will be the message/argument types we pass
 */
/* message data header */
#define FM10K_TLV_ID_SHIFT		0
#define FM10K_TLV_ID_SIZE		16
#define FM10K_TLV_ID_MASK		((1u << FM10K_TLV_ID_SIZE) - 1)
#define FM10K_TLV_FLAGS_SHIFT		16
#define FM10K_TLV_FLAGS_MSG		0x1
#define FM10K_TLV_FLAGS_SIZE		4
#define FM10K_TLV_LEN_SHIFT		20
#define FM10K_TLV_LEN_SIZE		12

#define FM10K_TLV_HDR_LEN		4ul
#define FM10K_TLV_LEN_ALIGN_MASK \
	((FM10K_TLV_HDR_LEN - 1) << FM10K_TLV_LEN_SHIFT)
#define FM10K_TLV_LEN_ALIGN(tlv) \
	(((tlv) + FM10K_TLV_LEN_ALIGN_MASK) & ~FM10K_TLV_LEN_ALIGN_MASK)
#define FM10K_TLV_DWORD_LEN(tlv) \
	((u16)((FM10K_TLV_LEN_ALIGN(tlv)) >> (FM10K_TLV_LEN_SHIFT + 2)) + 1)

#define FM10K_TLV_RESULTS_MAX		32

enum fm10k_tlv_type {
	FM10K_TLV_NULL_STRING,
	FM10K_TLV_MAC_ADDR,
	FM10K_TLV_BOOL,
	FM10K_TLV_UNSIGNED,
	FM10K_TLV_SIGNED,
	FM10K_TLV_LE_STRUCT,
	FM10K_TLV_NESTED,
	FM10K_TLV_MAX_TYPE
};

#define FM10K_TLV_ERROR (~0u)

struct fm10k_tlv_attr {
	unsigned int		id;
	enum fm10k_tlv_type	type;
	u16			len;
};

#define FM10K_TLV_ATTR_NULL_STRING(id, len) { id, FM10K_TLV_NULL_STRING, len }
#define FM10K_TLV_ATTR_MAC_ADDR(id)	    { id, FM10K_TLV_MAC_ADDR, 6 }
#define FM10K_TLV_ATTR_BOOL(id)		    { id, FM10K_TLV_BOOL, 0 }
#define FM10K_TLV_ATTR_U8(id)		    { id, FM10K_TLV_UNSIGNED, 1 }
#define FM10K_TLV_ATTR_U16(id)		    { id, FM10K_TLV_UNSIGNED, 2 }
#define FM10K_TLV_ATTR_U32(id)		    { id, FM10K_TLV_UNSIGNED, 4 }
#define FM10K_TLV_ATTR_U64(id)		    { id, FM10K_TLV_UNSIGNED, 8 }
#define FM10K_TLV_ATTR_S8(id)		    { id, FM10K_TLV_SIGNED, 1 }
#define FM10K_TLV_ATTR_S16(id)		    { id, FM10K_TLV_SIGNED, 2 }
#define FM10K_TLV_ATTR_S32(id)		    { id, FM10K_TLV_SIGNED, 4 }
#define FM10K_TLV_ATTR_S64(id)		    { id, FM10K_TLV_SIGNED, 8 }
#define FM10K_TLV_ATTR_LE_STRUCT(id, len)   { id, FM10K_TLV_LE_STRUCT, len }
#define FM10K_TLV_ATTR_NESTED(id)	    { id, FM10K_TLV_NESTED }
#define FM10K_TLV_ATTR_LAST		    { FM10K_TLV_ERROR }

struct fm10k_msg_data {
	unsigned int		    id;
	const struct fm10k_tlv_attr *attr;
	s32			    (*func)(struct fm10k_hw *, u32 **,
					    struct fm10k_mbx_info *);
};

#define FM10K_MSG_HANDLER(id, attr, func) { id, attr, func }

s32 fm10k_tlv_msg_init(u32 *, u16);
s32 fm10k_tlv_attr_put_mac_vlan(u32 *, u16, const u8 *, u16);
s32 fm10k_tlv_attr_get_mac_vlan(u32 *, u8 *, u16 *);
s32 fm10k_tlv_attr_put_bool(u32 *, u16);
s32 fm10k_tlv_attr_put_value(u32 *, u16, s64, u32);
#define fm10k_tlv_attr_put_u8(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 1)
#define fm10k_tlv_attr_put_u16(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 2)
#define fm10k_tlv_attr_put_u32(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 4)
#define fm10k_tlv_attr_put_u64(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 8)
#define fm10k_tlv_attr_put_s8(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 1)
#define fm10k_tlv_attr_put_s16(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 2)
#define fm10k_tlv_attr_put_s32(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 4)
#define fm10k_tlv_attr_put_s64(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 8)
s32 fm10k_tlv_attr_get_value(u32 *, void *, u32);
#define fm10k_tlv_attr_get_u8(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(u8))
#define fm10k_tlv_attr_get_u16(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(u16))
#define fm10k_tlv_attr_get_u32(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(u32))
#define fm10k_tlv_attr_get_u64(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(u64))
#define fm10k_tlv_attr_get_s8(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(s8))
#define fm10k_tlv_attr_get_s16(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(s16))
#define fm10k_tlv_attr_get_s32(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(s32))
#define fm10k_tlv_attr_get_s64(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(s64))
s32 fm10k_tlv_attr_put_le_struct(u32 *, u16, const void *, u32);
s32 fm10k_tlv_attr_get_le_struct(u32 *, void *, u32);
s32 fm10k_tlv_msg_parse(struct fm10k_hw *, u32 *, struct fm10k_mbx_info *,
			const struct fm10k_msg_data *);
s32 fm10k_tlv_msg_error(struct fm10k_hw *hw, u32 **results,
			struct fm10k_mbx_info *);

#define FM10K_TLV_MSG_ID_TEST	0

enum fm10k_tlv_test_attr_id {
	FM10K_TEST_MSG_UNSET,
	FM10K_TEST_MSG_STRING,
	FM10K_TEST_MSG_MAC_ADDR,
	FM10K_TEST_MSG_U8,
	FM10K_TEST_MSG_U16,
	FM10K_TEST_MSG_U32,
	FM10K_TEST_MSG_U64,
	FM10K_TEST_MSG_S8,
	FM10K_TEST_MSG_S16,
	FM10K_TEST_MSG_S32,
	FM10K_TEST_MSG_S64,
	FM10K_TEST_MSG_LE_STRUCT,
	FM10K_TEST_MSG_NESTED,
	FM10K_TEST_MSG_RESULT,
	FM10K_TEST_MSG_MAX
};

extern const struct fm10k_tlv_attr fm10k_tlv_msg_test_attr[];
void fm10k_tlv_msg_test_create(u32 *, u32);
s32 fm10k_tlv_msg_test(struct fm10k_hw *, u32 **, struct fm10k_mbx_info *);

#define FM10K_TLV_MSG_TEST_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_TLV_MSG_ID_TEST, fm10k_tlv_msg_test_attr, func)
#define FM10K_TLV_MSG_ERROR_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_TLV_ERROR, NULL, func)
#endif /* _FM10K_MSG_H_ */
