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
 * This file contains dummy FCPTM routines to aid in Initiator Mode only
 * compilation of OS driver.
 *
 */

#include "bfa_os_inc.h"
#include "fcs_rport.h"
#include "fcs_fcptm.h"
#include "fcs/bfa_fcs_rport.h"

struct bfa_fcs_tin_s *
bfa_fcs_tin_create(struct bfa_fcs_rport_s *rport)
{
	return NULL;
}

void
bfa_fcs_tin_delete(struct bfa_fcs_tin_s *tin)
{
}

void
bfa_fcs_tin_rport_offline(struct bfa_fcs_tin_s *tin)
{
}

void
bfa_fcs_tin_rport_online(struct bfa_fcs_tin_s *tin)
{
}

void
bfa_fcs_tin_rx_prli(struct bfa_fcs_tin_s *tin, struct fchs_s *fchs, u16 len)
{
}

void
bfa_fcs_fcptm_uf_recv(struct bfa_fcs_tin_s *tin, struct fchs_s *fchs, u16 len)
{
}

void
bfa_fcs_tin_pause(struct bfa_fcs_tin_s *tin)
{
}

void
bfa_fcs_tin_resume(struct bfa_fcs_tin_s *tin)
{
}
