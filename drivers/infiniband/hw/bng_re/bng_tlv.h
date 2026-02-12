/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */

#ifndef __BNG_TLV_H__
#define __BNG_TLV_H__

#include "bng_roce_hsi.h"

struct roce_tlv {
	struct tlv tlv;
	u8 total_size; // in units of 16 byte chunks
	u8 unused[7];  // for 16 byte alignment
};

/*
 * TLV size in units of 16 byte chunks
 */
#define TLV_SIZE ((sizeof(struct roce_tlv) + 15) / 16)
/*
 * TLV length in bytes
 */
#define TLV_BYTES (TLV_SIZE * 16)

#define HAS_TLV_HEADER(msg) (le16_to_cpu(((struct tlv *)(msg))->cmd_discr) == CMD_DISCR_TLV_ENCAP)
#define GET_TLV_DATA(tlv)   ((void *)&((uint8_t *)(tlv))[TLV_BYTES])

static inline u8 __get_cmdq_base_opcode(struct cmdq_base *req, u32 size)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		return ((struct cmdq_base *)GET_TLV_DATA(req))->opcode;
	else
		return req->opcode;
}

static inline void __set_cmdq_base_opcode(struct cmdq_base *req,
					  u32 size, u8 val)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		((struct cmdq_base *)GET_TLV_DATA(req))->opcode = val;
	else
		req->opcode = val;
}

static inline __le16 __get_cmdq_base_cookie(struct cmdq_base *req, u32 size)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		return ((struct cmdq_base *)GET_TLV_DATA(req))->cookie;
	else
		return req->cookie;
}

static inline void __set_cmdq_base_cookie(struct cmdq_base *req,
					  u32 size, __le16 val)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		((struct cmdq_base *)GET_TLV_DATA(req))->cookie = val;
	else
		req->cookie = val;
}

static inline __le64 __get_cmdq_base_resp_addr(struct cmdq_base *req, u32 size)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		return ((struct cmdq_base *)GET_TLV_DATA(req))->resp_addr;
	else
		return req->resp_addr;
}

static inline void __set_cmdq_base_resp_addr(struct cmdq_base *req,
					     u32 size, __le64 val)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		((struct cmdq_base *)GET_TLV_DATA(req))->resp_addr = val;
	else
		req->resp_addr = val;
}

static inline u8 __get_cmdq_base_resp_size(struct cmdq_base *req, u32 size)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		return ((struct cmdq_base *)GET_TLV_DATA(req))->resp_size;
	else
		return req->resp_size;
}

static inline void __set_cmdq_base_resp_size(struct cmdq_base *req,
					     u32 size, u8 val)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		((struct cmdq_base *)GET_TLV_DATA(req))->resp_size = val;
	else
		req->resp_size = val;
}

static inline u8 __get_cmdq_base_cmd_size(struct cmdq_base *req, u32 size)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		return ((struct roce_tlv *)(req))->total_size;
	else
		return req->cmd_size;
}

static inline void __set_cmdq_base_cmd_size(struct cmdq_base *req,
					    u32 size, u8 val)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		((struct cmdq_base *)GET_TLV_DATA(req))->cmd_size = val;
	else
		req->cmd_size = val;
}

static inline __le16 __get_cmdq_base_flags(struct cmdq_base *req, u32 size)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		return ((struct cmdq_base *)GET_TLV_DATA(req))->flags;
	else
		return req->flags;
}

static inline void __set_cmdq_base_flags(struct cmdq_base *req,
					 u32 size, __le16 val)
{
	if (HAS_TLV_HEADER(req) && size > TLV_BYTES)
		((struct cmdq_base *)GET_TLV_DATA(req))->flags = val;
	else
		req->flags = val;
}

#endif /* __BNG_TLV_H__ */
