/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */

#ifndef __QPLIB_TLV_H__
#define __QPLIB_TLV_H__

struct roce_tlv {
	struct tlv tlv;
	u8 total_size; // in units of 16 byte chunks
	u8 unused[7];  // for 16 byte alignment
};

#define CHUNK_SIZE 16
#define CHUNKS(x) (((x) + CHUNK_SIZE - 1) / CHUNK_SIZE)

static inline  void __roce_1st_tlv_prep(struct roce_tlv *rtlv, u8 tot_chunks,
					u16 content_bytes, u8 flags)
{
	rtlv->tlv.cmd_discr = cpu_to_le16(CMD_DISCR_TLV_ENCAP);
	rtlv->tlv.tlv_type = cpu_to_le16(TLV_TYPE_ROCE_SP_COMMAND);
	rtlv->tlv.length = cpu_to_le16(content_bytes);
	rtlv->tlv.flags = TLV_FLAGS_REQUIRED;
	rtlv->tlv.flags |= flags ? TLV_FLAGS_MORE : 0;
	rtlv->total_size = (tot_chunks);
}

static inline void __roce_ext_tlv_prep(struct roce_tlv *rtlv, u16 tlv_type,
				       u16 content_bytes, u8 more, u8 flags)
{
	rtlv->tlv.cmd_discr = cpu_to_le16(CMD_DISCR_TLV_ENCAP);
	rtlv->tlv.tlv_type = cpu_to_le16(tlv_type);
	rtlv->tlv.length = cpu_to_le16(content_bytes);
	rtlv->tlv.flags |= more ? TLV_FLAGS_MORE : 0;
	rtlv->tlv.flags |= flags ? TLV_FLAGS_REQUIRED : 0;
}

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

struct bnxt_qplib_tlv_modify_cc_req {
	struct roce_tlv				tlv_hdr;
	struct cmdq_modify_roce_cc		base_req;
	__le64					tlvpad;
	struct cmdq_modify_roce_cc_gen1_tlv	ext_req;
};

struct bnxt_qplib_tlv_query_rcc_sb {
	struct roce_tlv					tlv_hdr;
	struct creq_query_roce_cc_resp_sb		base_sb;
	struct creq_query_roce_cc_gen1_resp_sb_tlv	gen1_sb;
};
#endif /* __QPLIB_TLV_H__ */
