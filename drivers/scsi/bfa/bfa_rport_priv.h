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

#ifndef __BFA_RPORT_PRIV_H__
#define __BFA_RPORT_PRIV_H__

#include <bfa_svc.h>

#define BFA_RPORT_MIN	4

struct bfa_rport_mod_s {
	struct bfa_rport_s *rps_list;	/*  list of rports	*/
	struct list_head 	rp_free_q;	/*  free bfa_rports	*/
	struct list_head 	rp_active_q;	/*  free bfa_rports 	*/
	u16	num_rports;	/*  number of rports	*/
};

#define BFA_RPORT_MOD(__bfa)	(&(__bfa)->modules.rport_mod)

/**
 * Convert rport tag to RPORT
 */
#define BFA_RPORT_FROM_TAG(__bfa, _tag)				\
	(BFA_RPORT_MOD(__bfa)->rps_list +				\
	 ((_tag) & (BFA_RPORT_MOD(__bfa)->num_rports - 1)))

/*
 * external functions
 */
void	bfa_rport_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
#endif /* __BFA_RPORT_PRIV_H__ */
