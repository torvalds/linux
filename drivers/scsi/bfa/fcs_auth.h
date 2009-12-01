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
 *  fcs_uf.h FCS unsolicited frame receive
 */


#ifndef __FCS_AUTH_H__
#define __FCS_AUTH_H__

#include <fcs/bfa_fcs.h>
#include <fcs/bfa_fcs_vport.h>
#include <fcs/bfa_fcs_lport.h>

/*
 * fcs friend functions: only between fcs modules
 */
void bfa_fcs_auth_uf_recv(struct bfa_fcs_fabric_s *fabric, int len);
void bfa_fcs_auth_start(struct bfa_fcs_fabric_s *fabric);
void bfa_fcs_auth_stop(struct bfa_fcs_fabric_s *fabric);

#endif /* __FCS_UF_H__ */
