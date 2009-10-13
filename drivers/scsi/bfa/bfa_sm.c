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
 *  bfasm.c BFA State machine utility functions
 */

#include <cs/bfa_sm.h>

/**
 *  cs_sm_api
 */

int
bfa_sm_to_state(struct bfa_sm_table_s *smt, bfa_sm_t sm)
{
	int             i = 0;

	while (smt[i].sm && smt[i].sm != sm)
		i++;
	return smt[i].state;
}


