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
 *  fcs_ms.h FCS ms interfaces
 */
#ifndef __FCS_MS_H__
#define __FCS_MS_H__

/* MS FCS routines */
void bfa_fcs_port_ms_init(struct bfa_fcs_port_s *port);
void bfa_fcs_port_ms_offline(struct bfa_fcs_port_s *port);
void bfa_fcs_port_ms_online(struct bfa_fcs_port_s *port);
void bfa_fcs_port_ms_fabric_rscn(struct bfa_fcs_port_s *port);

/* FDMI FCS routines */
void bfa_fcs_port_fdmi_init(struct bfa_fcs_port_ms_s *ms);
void bfa_fcs_port_fdmi_offline(struct bfa_fcs_port_ms_s *ms);
void bfa_fcs_port_fdmi_online(struct bfa_fcs_port_ms_s *ms);

#endif
