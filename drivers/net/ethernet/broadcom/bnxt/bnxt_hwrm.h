/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2020 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_HWRM_H
#define BNXT_HWRM_H

#include "bnxt_hsi.h"

enum bnxt_hwrm_ctx_flags {
	/* Update the HWRM_API_FLAGS right below for any new non-internal bit added here */
	BNXT_HWRM_INTERNAL_CTX_OWNED	= BIT(0), /* caller owns the context */
	BNXT_HWRM_INTERNAL_RESP_DIRTY	= BIT(1), /* response contains data */
	BNXT_HWRM_CTX_SILENT		= BIT(2), /* squelch firmware errors */
	BNXT_HWRM_FULL_WAIT		= BIT(3), /* wait for full timeout of HWRM command */
};

#define HWRM_API_FLAGS (BNXT_HWRM_CTX_SILENT | BNXT_HWRM_FULL_WAIT)

struct bnxt_hwrm_ctx {
	u64 sentinel;
	dma_addr_t dma_handle;
	struct output *resp;
	struct input *req;
	dma_addr_t slice_handle;
	void *slice_addr;
	u32 slice_size;
	u32 req_len;
	enum bnxt_hwrm_ctx_flags flags;
	unsigned int timeout;
	u32 allocated;
	gfp_t gfp;
};

#define BNXT_HWRM_MAX_REQ_LEN		(bp->hwrm_max_req_len)
#define BNXT_HWRM_SHORT_REQ_LEN		sizeof(struct hwrm_short_input)
#define HWRM_CMD_MAX_TIMEOUT		40000
#define SHORT_HWRM_CMD_TIMEOUT		20
#define HWRM_CMD_TIMEOUT		(bp->hwrm_cmd_timeout)
#define HWRM_RESET_TIMEOUT		((HWRM_CMD_TIMEOUT) * 4)
#define HWRM_COREDUMP_TIMEOUT		((HWRM_CMD_TIMEOUT) * 12)
#define BNXT_HWRM_TARGET		0xffff
#define BNXT_HWRM_NO_CMPL_RING		-1
#define BNXT_HWRM_REQ_MAX_SIZE		128
#define BNXT_HWRM_DMA_SIZE		(2 * PAGE_SIZE) /* space for req+resp */
#define BNXT_HWRM_RESP_RESERVED		PAGE_SIZE
#define BNXT_HWRM_RESP_OFFSET		(BNXT_HWRM_DMA_SIZE -		\
					 BNXT_HWRM_RESP_RESERVED)
#define BNXT_HWRM_CTX_OFFSET		(BNXT_HWRM_RESP_OFFSET -	\
					 sizeof(struct bnxt_hwrm_ctx))
#define BNXT_HWRM_DMA_ALIGN		16
#define BNXT_HWRM_SENTINEL		0xb6e1f68a12e9a7eb /* arbitrary value */
#define BNXT_HWRM_REQS_PER_PAGE		(BNXT_PAGE_SIZE /	\
					 BNXT_HWRM_REQ_MAX_SIZE)
#define HWRM_SHORT_MIN_TIMEOUT		3
#define HWRM_SHORT_MAX_TIMEOUT		10
#define HWRM_SHORT_TIMEOUT_COUNTER	5

#define HWRM_MIN_TIMEOUT		25
#define HWRM_MAX_TIMEOUT		40

#define HWRM_WAIT_MUST_ABORT(bp, ctx)					\
	(le16_to_cpu((ctx)->req->req_type) != HWRM_VER_GET &&		\
	 !bnxt_is_fw_healthy(bp))

static inline unsigned int hwrm_total_timeout(unsigned int n)
{
	return n <= HWRM_SHORT_TIMEOUT_COUNTER ? n * HWRM_SHORT_MIN_TIMEOUT :
		HWRM_SHORT_TIMEOUT_COUNTER * HWRM_SHORT_MIN_TIMEOUT +
		(n - HWRM_SHORT_TIMEOUT_COUNTER) * HWRM_MIN_TIMEOUT;
}


#define HWRM_VALID_BIT_DELAY_USEC	150

#define BNXT_HWRM_CHNL_CHIMP	0
#define BNXT_HWRM_CHNL_KONG	1

static inline bool bnxt_cfa_hwrm_message(u16 req_type)
{
	switch (req_type) {
	case HWRM_CFA_ENCAP_RECORD_ALLOC:
	case HWRM_CFA_ENCAP_RECORD_FREE:
	case HWRM_CFA_DECAP_FILTER_ALLOC:
	case HWRM_CFA_DECAP_FILTER_FREE:
	case HWRM_CFA_EM_FLOW_ALLOC:
	case HWRM_CFA_EM_FLOW_FREE:
	case HWRM_CFA_EM_FLOW_CFG:
	case HWRM_CFA_FLOW_ALLOC:
	case HWRM_CFA_FLOW_FREE:
	case HWRM_CFA_FLOW_INFO:
	case HWRM_CFA_FLOW_FLUSH:
	case HWRM_CFA_FLOW_STATS:
	case HWRM_CFA_METER_PROFILE_ALLOC:
	case HWRM_CFA_METER_PROFILE_FREE:
	case HWRM_CFA_METER_PROFILE_CFG:
	case HWRM_CFA_METER_INSTANCE_ALLOC:
	case HWRM_CFA_METER_INSTANCE_FREE:
		return true;
	default:
		return false;
	}
}

static inline bool bnxt_kong_hwrm_message(struct bnxt *bp, struct input *req)
{
	return (bp->fw_cap & BNXT_FW_CAP_KONG_MB_CHNL &&
		(bnxt_cfa_hwrm_message(le16_to_cpu(req->req_type)) ||
		 le16_to_cpu(req->target_id) == HWRM_TARGET_ID_KONG));
}

static inline void *bnxt_get_hwrm_resp_addr(struct bnxt *bp, void *req)
{
	return bp->hwrm_cmd_resp_addr;
}

static inline u16 bnxt_get_hwrm_seq_id(struct bnxt *bp, u16 dst)
{
	u16 seq_id;

	if (dst == BNXT_HWRM_CHNL_CHIMP)
		seq_id = bp->hwrm_cmd_seq++;
	else
		seq_id = bp->hwrm_cmd_kong_seq++;
	return seq_id;
}

void bnxt_hwrm_cmd_hdr_init(struct bnxt *, void *, u16, u16, u16);
int _hwrm_send_message(struct bnxt *bp, void *msg, u32 len, int timeout);
int _hwrm_send_message_silent(struct bnxt *bp, void *msg, u32 len, int timeout);
int hwrm_send_message(struct bnxt *bp, void *msg, u32 len, int timeout);
int hwrm_send_message_silent(struct bnxt *bp, void *msg, u32 len, int timeout);
int __hwrm_req_init(struct bnxt *bp, void **req, u16 req_type, u32 req_len);
#define hwrm_req_init(bp, req, req_type) \
	__hwrm_req_init((bp), (void **)&(req), (req_type), sizeof(*(req)))
void *hwrm_req_hold(struct bnxt *bp, void *req);
void hwrm_req_drop(struct bnxt *bp, void *req);
void hwrm_req_flags(struct bnxt *bp, void *req, enum bnxt_hwrm_ctx_flags flags);
void hwrm_req_timeout(struct bnxt *bp, void *req, unsigned int timeout);
int hwrm_req_send(struct bnxt *bp, void *req);
int hwrm_req_send_silent(struct bnxt *bp, void *req);
int hwrm_req_replace(struct bnxt *bp, void *req, void *new_req, u32 len);
void hwrm_req_alloc_flags(struct bnxt *bp, void *req, gfp_t flags);
void *hwrm_req_dma_slice(struct bnxt *bp, void *req, u32 size, dma_addr_t *dma);
#endif
