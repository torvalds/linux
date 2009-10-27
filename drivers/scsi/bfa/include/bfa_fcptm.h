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

#ifndef __BFA_FCPTM_H__
#define __BFA_FCPTM_H__

#include <bfa.h>
#include <bfa_svc.h>
#include <bfi/bfi_fcptm.h>

/*
 * forward declarations
 */
struct bfa_tin_s;
struct bfa_iotm_s;
struct bfa_tsktm_s;

/*
 * bfa fcptm module API functions
 */
void bfa_fcptm_path_tov_set(struct bfa_s *bfa, u16 path_tov);
u16 bfa_fcptm_path_tov_get(struct bfa_s *bfa);
void bfa_fcptm_qdepth_set(struct bfa_s *bfa, u16 q_depth);
u16 bfa_fcptm_qdepth_get(struct bfa_s *bfa);

/*
 * bfa tin API functions
 */
void bfa_tin_get_stats(struct bfa_tin_s *tin, struct bfa_tin_stats_s *stats);
void bfa_tin_clear_stats(struct bfa_tin_s *tin);

#endif /* __BFA_FCPTM_H__ */

