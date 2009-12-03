/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
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
#ifndef __BFA_UF_PRIV_H__
#define __BFA_UF_PRIV_H__

#include <cs/bfa_sm.h>
#include <bfa_svc.h>
#include <bfi/bfi_uf.h>

#define BFA_UF_MIN	(4)

struct bfa_uf_mod_s {
	struct bfa_s *bfa;		/*  back pointer to BFA */
	struct bfa_uf_s *uf_list;	/*  array of UFs */
	u16	num_ufs;	/*  num unsolicited rx frames */
	struct list_head 	uf_free_q;	/*  free UFs */
	struct list_head 	uf_posted_q;	/*  UFs posted to IOC */
	struct bfa_uf_buf_s *uf_pbs_kva;	/*  list UF bufs request pld */
	u64	uf_pbs_pa;	/*  phy addr for UF bufs */
	struct bfi_uf_buf_post_s *uf_buf_posts;
					/*  pre-built UF post msgs */
	bfa_cb_uf_recv_t ufrecv;	/*  uf recv handler function */
	void		*cbarg;		/*  uf receive handler arg */
};

#define BFA_UF_MOD(__bfa)	(&(__bfa)->modules.uf_mod)

#define ufm_pbs_pa(_ufmod, _uftag)	\
	((_ufmod)->uf_pbs_pa + sizeof(struct bfa_uf_buf_s) * (_uftag))

void	bfa_uf_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);

#endif /* __BFA_UF_PRIV_H__ */
