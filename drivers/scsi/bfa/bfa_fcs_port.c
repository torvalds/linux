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
 *  bfa_fcs_pport.c BFA FCS PPORT ( physical port)
 */

#include <fcs/bfa_fcs.h>
#include <bfa_svc.h>
#include <fcs/bfa_fcs_fabric.h>
#include "fcs_trcmod.h"
#include "fcs.h"
#include "fcs_fabric.h"
#include "fcs_port.h"

BFA_TRC_FILE(FCS, PPORT);

static void
bfa_fcs_pport_event_handler(void *cbarg, bfa_pport_event_t event)
{
	struct bfa_fcs_s      *fcs = cbarg;

	bfa_trc(fcs, event);

	switch (event) {
	case BFA_PPORT_LINKUP:
		bfa_fcs_fabric_link_up(&fcs->fabric);
		break;

	case BFA_PPORT_LINKDOWN:
		bfa_fcs_fabric_link_down(&fcs->fabric);
		break;

	case BFA_PPORT_TRUNK_LINKDOWN:
		bfa_assert(0);
		break;

	default:
		bfa_assert(0);
	}
}

void
bfa_fcs_pport_attach(struct bfa_fcs_s *fcs)
{
	bfa_pport_event_register(fcs->bfa, bfa_fcs_pport_event_handler, fcs);
}
