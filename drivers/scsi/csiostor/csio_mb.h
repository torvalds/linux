/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __CSIO_MB_H__
#define __CSIO_MB_H__

#include <linux/timer.h>
#include <linux/completion.h>

#include "t4fw_api.h"
#include "t4fw_api_stor.h"
#include "csio_defs.h"

#define CSIO_STATS_OFFSET (2)
#define CSIO_NUM_STATS_PER_MB (6)

struct fw_fcoe_port_cmd_params {
	uint8_t		portid;
	uint8_t		idx;
	uint8_t		nstats;
};

#define CSIO_DUMP_MB(__hw, __num, __mb)					\
	csio_dbg(__hw, "\t%llx %llx %llx %llx %llx %llx %llx %llx\n",	\
		(unsigned long long)csio_rd_reg64(__hw, __mb),		\
		(unsigned long long)csio_rd_reg64(__hw, __mb + 8),	\
		(unsigned long long)csio_rd_reg64(__hw, __mb + 16),	\
		(unsigned long long)csio_rd_reg64(__hw, __mb + 24),	\
		(unsigned long long)csio_rd_reg64(__hw, __mb + 32),	\
		(unsigned long long)csio_rd_reg64(__hw, __mb + 40),	\
		(unsigned long long)csio_rd_reg64(__hw, __mb + 48),	\
		(unsigned long long)csio_rd_reg64(__hw, __mb + 56))

#define CSIO_MB_MAX_REGS	8
#define CSIO_MAX_MB_SIZE	64
#define CSIO_MB_POLL_FREQ	5		/*  5 ms */
#define CSIO_MB_DEFAULT_TMO	FW_CMD_MAX_TIMEOUT

/* Device master in HELLO command */
enum csio_dev_master { CSIO_MASTER_CANT, CSIO_MASTER_MAY, CSIO_MASTER_MUST };

enum csio_mb_owner { CSIO_MBOWNER_NONE, CSIO_MBOWNER_FW, CSIO_MBOWNER_PL };

enum csio_dev_state {
	CSIO_DEV_STATE_UNINIT,
	CSIO_DEV_STATE_INIT,
	CSIO_DEV_STATE_ERR
};

#define FW_PARAM_DEV(param) \
	(FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) | \
	 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_##param))

#define FW_PARAM_PFVF(param) \
	(FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_PFVF) | \
	 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_PFVF_##param)|  \
	 FW_PARAMS_PARAM_Y_V(0) | \
	 FW_PARAMS_PARAM_Z_V(0))

#define CSIO_INIT_MBP(__mbp, __cp,  __tmo, __priv, __fn, __clear)	\
do {									\
	if (__clear)							\
		memset((__cp), 0,					\
			    CSIO_MB_MAX_REGS * sizeof(__be64));		\
	INIT_LIST_HEAD(&(__mbp)->list);					\
	(__mbp)->tmo		= (__tmo);				\
	(__mbp)->priv		= (void *)(__priv);			\
	(__mbp)->mb_cbfn	= (__fn);				\
	(__mbp)->mb_size	= sizeof(*(__cp));			\
} while (0)

struct csio_mbm_stats {
	uint32_t	n_req;		/* number of mbox req */
	uint32_t	n_rsp;		/* number of mbox rsp */
	uint32_t	n_activeq;	/* number of mbox req active Q */
	uint32_t	n_cbfnq;	/* number of mbox req cbfn Q */
	uint32_t	n_tmo;		/* number of mbox timeout */
	uint32_t	n_cancel;	/* number of mbox cancel */
	uint32_t	n_err;		/* number of mbox error */
};

/* Driver version of Mailbox */
struct csio_mb {
	struct list_head	list;			/* for req/resp */
							/* queue in driver */
	__be64			mb[CSIO_MB_MAX_REGS];	/* MB in HW format */
	int			mb_size;		/* Size of this
							 * mailbox.
							 */
	uint32_t		tmo;			/* Timeout */
	struct completion	cmplobj;		/* MB Completion
							 * object
							 */
	void			(*mb_cbfn) (struct csio_hw *, struct csio_mb *);
							/* Callback fn */
	void			*priv;			/* Owner private ptr */
};

struct csio_mbm {
	uint32_t		a_mbox;			/* Async mbox num */
	uint32_t		intr_idx;		/* Interrupt index */
	struct timer_list	timer;			/* Mbox timer */
	struct csio_hw		*hw;			/* Hardware pointer */
	struct list_head	req_q;			/* Mbox request queue */
	struct list_head	cbfn_q;			/* Mbox completion q */
	struct csio_mb		*mcurrent;		/* Current mailbox */
	uint32_t		req_q_cnt;		/* Outstanding mbox
							 * cmds
							 */
	struct csio_mbm_stats	stats;			/* Statistics */
};

#define csio_set_mb_intr_idx(_m, _i)	((_m)->intr_idx = (_i))
#define csio_get_mb_intr_idx(_m)	((_m)->intr_idx)

struct csio_iq_params;
struct csio_eq_params;

enum fw_retval csio_mb_fw_retval(struct csio_mb *);

/* MB helpers */
void csio_mb_hello(struct csio_hw *, struct csio_mb *, uint32_t,
		   uint32_t, uint32_t, enum csio_dev_master,
		   void (*)(struct csio_hw *, struct csio_mb *));

void csio_mb_process_hello_rsp(struct csio_hw *, struct csio_mb *,
			       enum fw_retval *, enum csio_dev_state *,
			       uint8_t *);

void csio_mb_bye(struct csio_hw *, struct csio_mb *, uint32_t,
		 void (*)(struct csio_hw *, struct csio_mb *));

void csio_mb_reset(struct csio_hw *, struct csio_mb *, uint32_t, int, int,
		   void (*)(struct csio_hw *, struct csio_mb *));

void csio_mb_params(struct csio_hw *, struct csio_mb *, uint32_t, unsigned int,
		    unsigned int, unsigned int, const u32 *, u32 *, bool,
		    void (*)(struct csio_hw *, struct csio_mb *));

void csio_mb_process_read_params_rsp(struct csio_hw *, struct csio_mb *,
				enum fw_retval *, unsigned int , u32 *);

void csio_mb_ldst(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo,
		  int reg);

void csio_mb_caps_config(struct csio_hw *, struct csio_mb *, uint32_t,
			    bool, bool, bool, bool,
			    void (*)(struct csio_hw *, struct csio_mb *));

void csio_mb_port(struct csio_hw *, struct csio_mb *, uint32_t,
		  uint8_t, bool, uint32_t, uint16_t,
		  void (*) (struct csio_hw *, struct csio_mb *));

void csio_mb_process_read_port_rsp(struct csio_hw *, struct csio_mb *,
				   enum fw_retval *, uint16_t,
				   uint32_t *, uint32_t *);

void csio_mb_initialize(struct csio_hw *, struct csio_mb *, uint32_t,
			void (*)(struct csio_hw *, struct csio_mb *));

void csio_mb_iq_alloc_write(struct csio_hw *, struct csio_mb *, void *,
			uint32_t, struct csio_iq_params *,
			void (*) (struct csio_hw *, struct csio_mb *));

void csio_mb_iq_alloc_write_rsp(struct csio_hw *, struct csio_mb *,
				enum fw_retval *, struct csio_iq_params *);

void csio_mb_iq_free(struct csio_hw *, struct csio_mb *, void *,
		     uint32_t, struct csio_iq_params *,
		     void (*) (struct csio_hw *, struct csio_mb *));

void csio_mb_eq_ofld_alloc_write(struct csio_hw *, struct csio_mb *, void *,
				 uint32_t, struct csio_eq_params *,
				 void (*) (struct csio_hw *, struct csio_mb *));

void csio_mb_eq_ofld_alloc_write_rsp(struct csio_hw *, struct csio_mb *,
				     enum fw_retval *, struct csio_eq_params *);

void csio_mb_eq_ofld_free(struct csio_hw *, struct csio_mb *, void *,
			  uint32_t , struct csio_eq_params *,
			  void (*) (struct csio_hw *, struct csio_mb *));

void csio_fcoe_read_res_info_init_mb(struct csio_hw *, struct csio_mb *,
			uint32_t,
			void (*) (struct csio_hw *, struct csio_mb *));

void csio_write_fcoe_link_cond_init_mb(struct csio_lnode *, struct csio_mb *,
			uint32_t, uint8_t, uint32_t, uint8_t, bool, uint32_t,
			void (*) (struct csio_hw *, struct csio_mb *));

void csio_fcoe_vnp_alloc_init_mb(struct csio_lnode *, struct csio_mb *,
			uint32_t, uint32_t , uint32_t , uint16_t,
			uint8_t [8], uint8_t [8],
			void (*) (struct csio_hw *, struct csio_mb *));

void csio_fcoe_vnp_read_init_mb(struct csio_lnode *, struct csio_mb *,
			uint32_t, uint32_t , uint32_t ,
			void (*) (struct csio_hw *, struct csio_mb *));

void csio_fcoe_vnp_free_init_mb(struct csio_lnode *, struct csio_mb *,
			uint32_t , uint32_t, uint32_t ,
			void (*) (struct csio_hw *, struct csio_mb *));

void csio_fcoe_read_fcf_init_mb(struct csio_lnode *, struct csio_mb *,
			uint32_t, uint32_t, uint32_t,
			void (*cbfn) (struct csio_hw *, struct csio_mb *));

void csio_fcoe_read_portparams_init_mb(struct csio_hw *hw,
			struct csio_mb *mbp, uint32_t mb_tmo,
			struct fw_fcoe_port_cmd_params *portparams,
			void (*cbfn)(struct csio_hw *, struct csio_mb *));

void csio_mb_process_portparams_rsp(struct csio_hw *hw, struct csio_mb *mbp,
				enum fw_retval *retval,
				struct fw_fcoe_port_cmd_params *portparams,
				struct fw_fcoe_port_stats *portstats);

/* MB module functions */
int csio_mbm_init(struct csio_mbm *, struct csio_hw *,
			    void (*)(struct timer_list *));
void csio_mbm_exit(struct csio_mbm *);
void csio_mb_intr_enable(struct csio_hw *);
void csio_mb_intr_disable(struct csio_hw *);

int csio_mb_issue(struct csio_hw *, struct csio_mb *);
void csio_mb_completions(struct csio_hw *, struct list_head *);
int csio_mb_fwevt_handler(struct csio_hw *, __be64 *);
int csio_mb_isr_handler(struct csio_hw *);
struct csio_mb *csio_mb_tmo_handler(struct csio_hw *);
void csio_mb_cancel_all(struct csio_hw *, struct list_head *);

#endif /* ifndef __CSIO_MB_H__ */
