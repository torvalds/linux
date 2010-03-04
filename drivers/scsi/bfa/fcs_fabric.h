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
 *  fcs_lport.h FCS logical port interfaces
 */

#ifndef __FCS_FABRIC_H__
#define __FCS_FABRIC_H__

#include <fcs/bfa_fcs.h>
#include <fcs/bfa_fcs_vport.h>
#include <fcs/bfa_fcs_lport.h>

/*
* fcs friend functions: only between fcs modules
 */
void		bfa_fcs_fabric_attach(struct bfa_fcs_s *fcs);
void            bfa_fcs_fabric_modinit(struct bfa_fcs_s *fcs);
void            bfa_fcs_fabric_modexit(struct bfa_fcs_s *fcs);
void            bfa_fcs_fabric_modsusp(struct bfa_fcs_s *fcs);
void            bfa_fcs_fabric_link_up(struct bfa_fcs_fabric_s *fabric);
void            bfa_fcs_fabric_link_down(struct bfa_fcs_fabric_s *fabric);
void            bfa_fcs_fabric_addvport(struct bfa_fcs_fabric_s *fabric,
					struct bfa_fcs_vport_s *vport);
void            bfa_fcs_fabric_delvport(struct bfa_fcs_fabric_s *fabric,
					struct bfa_fcs_vport_s *vport);
int             bfa_fcs_fabric_is_online(struct bfa_fcs_fabric_s *fabric);
struct bfa_fcs_vport_s *bfa_fcs_fabric_vport_lookup(
			struct bfa_fcs_fabric_s *fabric, wwn_t pwwn);
void            bfa_fcs_fabric_modstart(struct bfa_fcs_s *fcs);
void            bfa_fcs_fabric_uf_recv(struct bfa_fcs_fabric_s *fabric,
			struct fchs_s *fchs, u16 len);
u16        bfa_fcs_fabric_vport_count(struct bfa_fcs_fabric_s *fabric);
bfa_boolean_t   bfa_fcs_fabric_is_loopback(struct bfa_fcs_fabric_s *fabric);
enum bfa_pport_type bfa_fcs_fabric_port_type(struct bfa_fcs_fabric_s *fabric);
void     	bfa_fcs_fabric_psymb_init(struct bfa_fcs_fabric_s *fabric);
void bfa_fcs_fabric_port_delete_comp(struct bfa_fcs_fabric_s *fabric);

bfa_status_t bfa_fcs_fabric_addvf(struct bfa_fcs_fabric_s *vf,
			struct bfa_fcs_s *fcs, struct bfa_port_cfg_s *port_cfg,
			struct bfad_vf_s *vf_drv);
void bfa_fcs_auth_finished(struct bfa_fcs_fabric_s *fabric,
			enum auth_status status);

void bfa_fcs_fabric_set_fabric_name(struct bfa_fcs_fabric_s *fabric,
			wwn_t fabric_name);
#endif /* __FCS_FABRIC_H__ */
