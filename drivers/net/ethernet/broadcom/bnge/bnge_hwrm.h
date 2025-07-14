/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_HWRM_H_
#define _BNGE_HWRM_H_

#include <linux/bnxt/hsi.h>

enum bnge_hwrm_ctx_flags {
	BNGE_HWRM_INTERNAL_CTX_OWNED	= BIT(0),
	BNGE_HWRM_INTERNAL_RESP_DIRTY	= BIT(1),
	BNGE_HWRM_CTX_SILENT		= BIT(2),
	BNGE_HWRM_FULL_WAIT		= BIT(3),
};

#define BNGE_HWRM_API_FLAGS (BNGE_HWRM_CTX_SILENT | BNGE_HWRM_FULL_WAIT)

struct bnge_hwrm_ctx {
	u64 sentinel;
	dma_addr_t dma_handle;
	struct output *resp;
	struct input *req;
	dma_addr_t slice_handle;
	void *slice_addr;
	u32 slice_size;
	u32 req_len;
	enum bnge_hwrm_ctx_flags flags;
	unsigned int timeout;
	u32 allocated;
	gfp_t gfp;
};

enum bnge_hwrm_wait_state {
	BNGE_HWRM_PENDING,
	BNGE_HWRM_DEFERRED,
	BNGE_HWRM_COMPLETE,
	BNGE_HWRM_CANCELLED,
};

enum bnge_hwrm_chnl { BNGE_HWRM_CHNL_CHIMP, BNGE_HWRM_CHNL_KONG };

struct bnge_hwrm_wait_token {
	struct rcu_head rcu;
	struct hlist_node node;
	enum bnge_hwrm_wait_state state;
	enum bnge_hwrm_chnl dst;
	u16 seq_id;
};

#define BNGE_DFLT_HWRM_CMD_TIMEOUT		500

#define BNGE_GRCPF_REG_CHIMP_COMM		0x0
#define BNGE_GRCPF_REG_CHIMP_COMM_TRIGGER	0x100

#define BNGE_HWRM_MAX_REQ_LEN		(bd->hwrm_max_req_len)
#define BNGE_HWRM_SHORT_REQ_LEN		sizeof(struct hwrm_short_input)
#define BNGE_HWRM_CMD_MAX_TIMEOUT	40000U
#define BNGE_SHORT_HWRM_CMD_TIMEOUT	20
#define BNGE_HWRM_CMD_TIMEOUT		(bd->hwrm_cmd_timeout)
#define BNGE_HWRM_RESET_TIMEOUT		((BNGE_HWRM_CMD_TIMEOUT) * 4)
#define BNGE_HWRM_TARGET		0xffff
#define BNGE_HWRM_NO_CMPL_RING		-1
#define BNGE_HWRM_REQ_MAX_SIZE		128
#define BNGE_HWRM_DMA_SIZE		(2 * PAGE_SIZE) /* space for req+resp */
#define BNGE_HWRM_RESP_RESERVED		PAGE_SIZE
#define BNGE_HWRM_RESP_OFFSET		(BNGE_HWRM_DMA_SIZE -		\
					 BNGE_HWRM_RESP_RESERVED)
#define BNGE_HWRM_CTX_OFFSET		(BNGE_HWRM_RESP_OFFSET -	\
					 sizeof(struct bnge_hwrm_ctx))
#define BNGE_HWRM_DMA_ALIGN		16
#define BNGE_HWRM_SENTINEL		0xb6e1f68a12e9a7eb /* arbitrary value */
#define BNGE_HWRM_SHORT_MIN_TIMEOUT		3
#define BNGE_HWRM_SHORT_MAX_TIMEOUT		10
#define BNGE_HWRM_SHORT_TIMEOUT_COUNTER		5

#define BNGE_HWRM_MIN_TIMEOUT		25
#define BNGE_HWRM_MAX_TIMEOUT		40

static inline unsigned int bnge_hwrm_timeout(unsigned int n)
{
	return n <= BNGE_HWRM_SHORT_TIMEOUT_COUNTER ?
		n * BNGE_HWRM_SHORT_MIN_TIMEOUT :
		BNGE_HWRM_SHORT_TIMEOUT_COUNTER *
			BNGE_HWRM_SHORT_MIN_TIMEOUT +
			(n - BNGE_HWRM_SHORT_TIMEOUT_COUNTER) *
				BNGE_HWRM_MIN_TIMEOUT;
}

#define BNGE_HWRM_FIN_WAIT_USEC	50000

void bnge_cleanup_hwrm_resources(struct bnge_dev *bd);
int bnge_init_hwrm_resources(struct bnge_dev *bd);

int bnge_hwrm_req_create(struct bnge_dev *bd, void **req, u16 req_type,
			 u32 req_len);
#define bnge_hwrm_req_init(bd, req, req_type) \
	bnge_hwrm_req_create((bd), (void **)&(req), (req_type),	\
			     sizeof(*(req)))
void *bnge_hwrm_req_hold(struct bnge_dev *bd, void *req);
void bnge_hwrm_req_drop(struct bnge_dev *bd, void *req);
void bnge_hwrm_req_flags(struct bnge_dev *bd, void *req,
			 enum bnge_hwrm_ctx_flags flags);
void bnge_hwrm_req_timeout(struct bnge_dev *bd, void *req,
			   unsigned int timeout);
int bnge_hwrm_req_send(struct bnge_dev *bd, void *req);
int bnge_hwrm_req_send_silent(struct bnge_dev *bd, void *req);
void bnge_hwrm_req_alloc_flags(struct bnge_dev *bd, void *req, gfp_t flags);
void *bnge_hwrm_req_dma_slice(struct bnge_dev *bd, void *req, u32 size,
			      dma_addr_t *dma);
#endif /* _BNGE_HWRM_H_ */
