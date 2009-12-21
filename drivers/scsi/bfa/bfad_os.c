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
 *  bfad_os.c Linux driver OS specific calls.
 */

#include "bfa_os_inc.h"
#include "bfad_drv.h"

void
bfa_os_gettimeofday(struct bfa_timeval_s *tv)
{
	struct timeval  tmp_tv;

	do_gettimeofday(&tmp_tv);
	tv->tv_sec = (u32) tmp_tv.tv_sec;
	tv->tv_usec = (u32) tmp_tv.tv_usec;
}

void
bfa_os_printf(struct bfa_log_mod_s *log_mod, u32 msg_id,
			const char *fmt, ...)
{
	va_list ap;
	#define BFA_STRING_256	256
	char tmp[BFA_STRING_256];

	va_start(ap, fmt);
	vsprintf(tmp, fmt, ap);
	va_end(ap);

	printk(tmp);
}


