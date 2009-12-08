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
#ifndef __FCS_FCPIM_H__
#define __FCS_FCPIM_H__

#include <defs/bfa_defs_port.h>
#include <fcs/bfa_fcs_lport.h>
#include <fcs/bfa_fcs_rport.h>

/*
 * Following routines are from FCPIM and will be called by rport.
 */
struct bfa_fcs_itnim_s *bfa_fcs_itnim_create(struct bfa_fcs_rport_s *rport);
void bfa_fcs_itnim_delete(struct bfa_fcs_itnim_s *itnim);
void bfa_fcs_itnim_rport_offline(struct bfa_fcs_itnim_s *itnim);
void bfa_fcs_itnim_rport_online(struct bfa_fcs_itnim_s *itnim);
bfa_status_t bfa_fcs_itnim_get_online_state(struct bfa_fcs_itnim_s *itnim);

void bfa_fcs_itnim_is_initiator(struct bfa_fcs_itnim_s *itnim);
void bfa_fcs_itnim_pause(struct bfa_fcs_itnim_s *itnim);
void bfa_fcs_itnim_resume(struct bfa_fcs_itnim_s *itnim);

/*
 * Modudle init/cleanup routines.
 */
void bfa_fcs_fcpim_modinit(struct bfa_fcs_s *fcs);
void bfa_fcs_fcpim_modexit(struct bfa_fcs_s *fcs);
void bfa_fcs_fcpim_uf_recv(struct bfa_fcs_itnim_s *itnim, struct fchs_s *fchs,
			u16 len);
#endif /* __FCS_FCPIM_H__ */
