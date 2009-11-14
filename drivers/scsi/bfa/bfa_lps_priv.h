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

#ifndef __BFA_LPS_PRIV_H__
#define __BFA_LPS_PRIV_H__

#include <bfa_svc.h>

struct bfa_lps_mod_s {
	struct list_head		lps_free_q;
	struct list_head		lps_active_q;
	struct bfa_lps_s	*lps_arr;
	int			num_lps;
};

#define BFA_LPS_MOD(__bfa)		(&(__bfa)->modules.lps_mod)
#define BFA_LPS_FROM_TAG(__mod, __tag)	(&(__mod)->lps_arr[__tag])

/*
 * external functions
 */
void	bfa_lps_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);

#endif /* __BFA_LPS_PRIV_H__ */
