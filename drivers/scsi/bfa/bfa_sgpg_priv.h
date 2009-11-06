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

/**
 *  hal_sgpg.h BFA SG page module
 */

#ifndef __BFA_SGPG_PRIV_H__
#define __BFA_SGPG_PRIV_H__

#include <cs/bfa_q.h>

#define BFA_SGPG_MIN	(16)

/**
 * Alignment macro for SG page allocation
 */
#define BFA_SGPG_ROUNDUP(_l) (((_l) + (sizeof(struct bfi_sgpg_s) - 1)) \
			& ~(sizeof(struct bfi_sgpg_s) - 1))

struct bfa_sgpg_wqe_s {
	struct list_head qe;	/*  queue sg page element	*/
	int	nsgpg;		/*  pages to be allocated	*/
	int	nsgpg_total;	/*  total pages required	*/
	void	(*cbfn) (void *cbarg);
				/*  callback function		*/
	void	*cbarg;		/*  callback arg		*/
	struct list_head sgpg_q;	/*  queue of alloced sgpgs	*/
};

struct bfa_sgpg_s {
	struct list_head 	qe;	/*  queue sg page element	*/
	struct bfi_sgpg_s *sgpg; /*  va of SG page		*/
	union bfi_addr_u sgpg_pa;/*  pa of SG page		*/
};

/**
 * Given number of SG elements, BFA_SGPG_NPAGE() returns the number of
 * SG pages required.
 */
#define BFA_SGPG_NPAGE(_nsges)  (((_nsges) / BFI_SGPG_DATA_SGES) + 1)

struct bfa_sgpg_mod_s {
	struct bfa_s *bfa;
	int		num_sgpgs;	/*  number of SG pages		*/
	int		free_sgpgs;	/*  number of free SG pages	*/
	struct bfa_sgpg_s *hsgpg_arr;	/*  BFA SG page array	*/
	struct bfi_sgpg_s *sgpg_arr;	/*  actual SG page array	*/
	u64	sgpg_arr_pa;	/*  SG page array DMA addr	*/
	struct list_head sgpg_q;	/*  queue of free SG pages	*/
	struct list_head sgpg_wait_q; /*  wait queue for SG pages	*/
};
#define BFA_SGPG_MOD(__bfa)	(&(__bfa)->modules.sgpg_mod)

bfa_status_t	bfa_sgpg_malloc(struct bfa_s *bfa, struct list_head *sgpg_q,
								int nsgpgs);
void		bfa_sgpg_mfree(struct bfa_s *bfa, struct list_head *sgpg_q,
								int nsgpgs);
void		bfa_sgpg_winit(struct bfa_sgpg_wqe_s *wqe,
				   void (*cbfn) (void *cbarg), void *cbarg);
void		bfa_sgpg_wait(struct bfa_s *bfa, struct bfa_sgpg_wqe_s *wqe,
								int nsgpgs);
void		bfa_sgpg_wcancel(struct bfa_s *bfa, struct bfa_sgpg_wqe_s *wqe);

#endif /* __BFA_SGPG_PRIV_H__ */
