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

#include <bfa.h>
#include <bfa_svc.h>
#include "fcs_lport.h"
#include "fcs_rport.h"
#include "lport_priv.h"

/**
 *  fab.c port fab implementation.
 */

/**
 *  bfa_fcs_port_fab_public port fab public functions
 */

/**
 *   Called by port to initialize fabric services of the base port.
 */
void
bfa_fcs_port_fab_init(struct bfa_fcs_port_s *port)
{
	bfa_fcs_port_ns_init(port);
	bfa_fcs_port_scn_init(port);
	bfa_fcs_port_ms_init(port);
}

/**
 *   Called by port to notify transition to online state.
 */
void
bfa_fcs_port_fab_online(struct bfa_fcs_port_s *port)
{
	bfa_fcs_port_ns_online(port);
	bfa_fcs_port_scn_online(port);
}

/**
 *   Called by port to notify transition to offline state.
 */
void
bfa_fcs_port_fab_offline(struct bfa_fcs_port_s *port)
{
	bfa_fcs_port_ns_offline(port);
	bfa_fcs_port_scn_offline(port);
	bfa_fcs_port_ms_offline(port);
}
