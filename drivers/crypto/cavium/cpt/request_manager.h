/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Cavium, Inc.
 */

#ifndef __REQUEST_MANAGER_H
#define __REQUEST_MANAGER_H

#include "cpt_common.h"

#define TIME_IN_RESET_COUNT  5
#define COMPLETION_CODE_SIZE 8
#define COMPLETION_CODE_INIT 0
#define PENDING_THOLD  100
#define MAX_SG_IN_CNT 12
#define MAX_SG_OUT_CNT 13
#define SG_LIST_HDR_SIZE  8
#define MAX_BUF_CNT	16

union ctrl_info {
	u32 flags;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u32 reserved0:26;
		u32 grp:3; /* Group bits */
		u32 dma_mode:2; /* DMA mode */
		u32 se_req:1;/* To SE core */
#else
		u32 se_req:1; /* To SE core */
		u32 dma_mode:2; /* DMA mode */
		u32 grp:3; /* Group bits */
		u32 reserved0:26;
#endif
	} s;
};

union opcode_info {
	u16 flags;
	struct {
		u8 major;
		u8 minor;
	} s;
};

struct cptvf_request {
	union opcode_info opcode;
	u16 param1;
	u16 param2;
	u16 dlen;
};

struct buf_ptr {
	u8 *vptr;
	dma_addr_t dma_addr;
	u16 size;
};

struct cpt_request_info {
	u8 incnt; /* Number of input buffers */
	u8 outcnt; /* Number of output buffers */
	u16 rlen; /* Output length */
	union ctrl_info ctrl; /* User control information */
	struct cptvf_request req; /* Request Information (Core specific) */

	bool may_sleep;

	struct buf_ptr in[MAX_BUF_CNT];
	struct buf_ptr out[MAX_BUF_CNT];

	void (*callback)(int, void *); /* Kernel ASYNC request callabck */
	void *callback_arg; /* Kernel ASYNC request callabck arg */
};

struct sglist_component {
	union {
		u64 len;
		struct {
			u16 len0;
			u16 len1;
			u16 len2;
			u16 len3;
		} s;
	} u;
	u64 ptr0;
	u64 ptr1;
	u64 ptr2;
	u64 ptr3;
};

struct cpt_info_buffer {
	struct cpt_vf *cptvf;
	unsigned long time_in;
	u8 extra_time;

	struct cpt_request_info *req;
	dma_addr_t dptr_baddr;
	u32 dlen;
	dma_addr_t rptr_baddr;
	dma_addr_t comp_baddr;
	u8 *in_buffer;
	u8 *out_buffer;
	u8 *gather_components;
	u8 *scatter_components;

	struct pending_entry *pentry;
	volatile u64 *completion_addr;
	volatile u64 *alternate_caddr;
};

/*
 * CPT_INST_S software command definitions
 * Words EI (0-3)
 */
union vq_cmd_word0 {
	u64 u64;
	struct {
		u16 opcode;
		u16 param1;
		u16 param2;
		u16 dlen;
	} s;
};

union vq_cmd_word3 {
	u64 u64;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 grp:3;
		u64 cptr:61;
#else
		u64 cptr:61;
		u64 grp:3;
#endif
	} s;
};

struct cpt_vq_command {
	union vq_cmd_word0 cmd;
	u64 dptr;
	u64 rptr;
	union vq_cmd_word3 cptr;
};

void vq_post_process(struct cpt_vf *cptvf, u32 qno);
int process_request(struct cpt_vf *cptvf, struct cpt_request_info *req);
#endif /* __REQUEST_MANAGER_H */
