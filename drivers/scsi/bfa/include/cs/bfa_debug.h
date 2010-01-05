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
 *  bfa_debug.h BFA debug interfaces
 */

#ifndef __BFA_DEBUG_H__
#define __BFA_DEBUG_H__

#define bfa_assert(__cond)	do {					\
	if (!(__cond)) 							\
		bfa_panic(__LINE__, __FILE__, #__cond);      \
} while (0)

#define bfa_sm_fault(__mod, __event)	do {				\
	bfa_sm_panic((__mod)->logm, __LINE__, __FILE__, __event);      \
} while (0)

#ifndef BFA_PERF_BUILD
#define bfa_assert_fp(__cond)	bfa_assert(__cond)
#else
#define bfa_assert_fp(__cond)
#endif

struct bfa_log_mod_s;
void bfa_panic(int line, char *file, char *panicstr);
void bfa_sm_panic(struct bfa_log_mod_s *logm, int line, char *file, int event);

#endif /* __BFA_DEBUG_H__ */
