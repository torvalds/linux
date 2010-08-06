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
 *  fcs_rport.h FCS rport interfaces and defines
 */

#ifndef __FCS_RPORT_H__
#define __FCS_RPORT_H__

#include <fcs/bfa_fcs_rport.h>

#define BFA_FCS_RPORT_MAX_RETRIES               (5)

void bfa_fcs_rport_uf_recv(struct bfa_fcs_rport_s *rport, struct fchs_s *fchs,
			u16 len);
void bfa_fcs_rport_scn(struct bfa_fcs_rport_s *rport);

struct bfa_fcs_rport_s *bfa_fcs_rport_create(struct bfa_fcs_port_s *port,
			u32 pid);
void bfa_fcs_rport_delete(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_online(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_offline(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_start(struct bfa_fcs_port_s *port, struct fchs_s *rx_fchs,
			struct fc_logi_s *plogi_rsp);
void bfa_fcs_rport_plogi_create(struct bfa_fcs_port_s *port,
			struct fchs_s *rx_fchs,
			struct fc_logi_s *plogi);
void bfa_fcs_rport_plogi(struct bfa_fcs_rport_s *rport, struct fchs_s *fchs,
			struct fc_logi_s *plogi);
void bfa_fcs_rport_logo_imp(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_prlo(struct bfa_fcs_rport_s *rport, uint16_t ox_id);
void bfa_fcs_rport_itnim_ack(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_itntm_ack(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_tin_ack(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_fcptm_offline_done(struct bfa_fcs_rport_s *rport);
int  bfa_fcs_rport_get_state(struct bfa_fcs_rport_s *rport);
struct bfa_fcs_rport_s *bfa_fcs_rport_create_by_wwn(struct bfa_fcs_port_s *port,
			wwn_t wwn);


/* Rport Features */
void  bfa_fcs_rpf_init(struct bfa_fcs_rport_s *rport);
void  bfa_fcs_rpf_rport_online(struct bfa_fcs_rport_s *rport);
void  bfa_fcs_rpf_rport_offline(struct bfa_fcs_rport_s *rport);

#endif /* __FCS_RPORT_H__ */
