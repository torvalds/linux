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

#include <cs/bfa_debug.h>
#include <bfa_os_inc.h>
#include <cs/bfa_q.h>
#include <log/bfa_log_hal.h>

/**
 *  cs_debug_api
 */


void
bfa_panic(int line, char *file, char *panicstr)
{
	bfa_log(NULL, BFA_LOG_HAL_ASSERT, file, line, panicstr);
	bfa_os_panic();
}

void
bfa_sm_panic(struct bfa_log_mod_s *logm, int line, char *file, int event)
{
	bfa_log(logm, BFA_LOG_HAL_SM_ASSERT, file, line, event);
	bfa_os_panic();
}

int
bfa_q_is_on_q_func(struct list_head *q, struct list_head *qe)
{
	struct list_head        *tqe;

	tqe = bfa_q_next(q);
	while (tqe != q) {
		if (tqe == qe)
			return (1);
		tqe = bfa_q_next(tqe);
		if (tqe == NULL)
			break;
	}
	return (0);
}


