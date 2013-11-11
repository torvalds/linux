/*
 * Linux network driver for Brocade Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * Copyright (c) 2005-2011 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */

#ifndef __BFA_MSGQ_H__
#define __BFA_MSGQ_H__

#include "bfa_defs.h"
#include "bfi.h"
#include "bfa_ioc.h"
#include "bfa_cs.h"

#define BFA_MSGQ_FREE_CNT(_q)						\
	(((_q)->consumer_index - (_q)->producer_index - 1) & ((_q)->depth - 1))

#define BFA_MSGQ_INDX_ADD(_q_indx, _qe_num, _q_depth)			\
	((_q_indx) = (((_q_indx) + (_qe_num)) & ((_q_depth) - 1)))

#define BFA_MSGQ_CMDQ_NUM_ENTRY		128
#define BFA_MSGQ_CMDQ_SIZE						\
	(BFI_MSGQ_CMD_ENTRY_SIZE * BFA_MSGQ_CMDQ_NUM_ENTRY)

#define BFA_MSGQ_RSPQ_NUM_ENTRY		128
#define BFA_MSGQ_RSPQ_SIZE						\
	(BFI_MSGQ_RSP_ENTRY_SIZE * BFA_MSGQ_RSPQ_NUM_ENTRY)

#define bfa_msgq_cmd_set(_cmd, _cbfn, _cbarg, _msg_size, _msg_hdr)	\
do {									\
	(_cmd)->cbfn = (_cbfn);						\
	(_cmd)->cbarg = (_cbarg);					\
	(_cmd)->msg_size = (_msg_size);					\
	(_cmd)->msg_hdr = (_msg_hdr);					\
} while (0)

struct bfa_msgq;

typedef void (*bfa_msgq_cmdcbfn_t)(void *cbarg, enum bfa_status status);

struct bfa_msgq_cmd_entry {
	struct list_head				qe;
	bfa_msgq_cmdcbfn_t		cbfn;
	void				*cbarg;
	size_t				msg_size;
	struct bfi_msgq_mhdr *msg_hdr;
};

enum bfa_msgq_cmdq_flags {
	BFA_MSGQ_CMDQ_F_DB_UPDATE	= 1,
};

struct bfa_msgq_cmdq {
	bfa_fsm_t			fsm;
	enum bfa_msgq_cmdq_flags flags;

	u16			producer_index;
	u16			consumer_index;
	u16			depth; /* FW Q depth is 16 bits */
	struct bfa_dma addr;
	struct bfa_mbox_cmd dbell_mb;

	u16			token;
	int				offset;
	int				bytes_to_copy;
	struct bfa_mbox_cmd copy_mb;

	struct list_head		pending_q; /* pending command queue */

	struct bfa_msgq *msgq;
};

enum bfa_msgq_rspq_flags {
	BFA_MSGQ_RSPQ_F_DB_UPDATE	= 1,
};

typedef void (*bfa_msgq_mcfunc_t)(void *cbarg, struct bfi_msgq_mhdr *mhdr);

struct bfa_msgq_rspq {
	bfa_fsm_t			fsm;
	enum bfa_msgq_rspq_flags flags;

	u16			producer_index;
	u16			consumer_index;
	u16			depth; /* FW Q depth is 16 bits */
	struct bfa_dma addr;
	struct bfa_mbox_cmd dbell_mb;

	int				nmclass;
	struct {
		bfa_msgq_mcfunc_t	cbfn;
		void			*cbarg;
	} rsphdlr[BFI_MC_MAX];

	struct bfa_msgq *msgq;
};

struct bfa_msgq {
	struct bfa_msgq_cmdq cmdq;
	struct bfa_msgq_rspq rspq;

	struct bfa_wc			init_wc;
	struct bfa_mbox_cmd init_mb;

	struct bfa_ioc_notify ioc_notify;
	struct bfa_ioc *ioc;
};

u32 bfa_msgq_meminfo(void);
void bfa_msgq_memclaim(struct bfa_msgq *msgq, u8 *kva, u64 pa);
void bfa_msgq_attach(struct bfa_msgq *msgq, struct bfa_ioc *ioc);
void bfa_msgq_regisr(struct bfa_msgq *msgq, enum bfi_mclass mc,
		     bfa_msgq_mcfunc_t cbfn, void *cbarg);
void bfa_msgq_cmd_post(struct bfa_msgq *msgq,
		       struct bfa_msgq_cmd_entry *cmd);
void bfa_msgq_rsp_copy(struct bfa_msgq *msgq, u8 *buf, size_t buf_len);

#endif
