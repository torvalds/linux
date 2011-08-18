/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __SYSDEP_X86_64_SIGCONTEXT_H
#define __SYSDEP_X86_64_SIGCONTEXT_H

#include <generated/user_constants.h>

#define SC_OFFSET(sc, field) \
	 *((unsigned long *) &(((char *) (sc))[HOST_##field]))
#define SC_CR2(sc) SC_OFFSET(sc, SC_CR2)
#define SC_ERR(sc) SC_OFFSET(sc, SC_ERR)
#define SC_TRAPNO(sc) SC_OFFSET(sc, SC_TRAPNO)

#define GET_FAULTINFO_FROM_SC(fi, sc) \
	{ \
		(fi).cr2 = SC_CR2(sc); \
		(fi).error_code = SC_ERR(sc); \
		(fi).trap_no = SC_TRAPNO(sc); \
	}

#define GET_FAULTINFO_FROM_MC(fi, mc) \
	{ \
		(fi).cr2 = (mc)->gregs[REG_CR2]; \
		(fi).error_code = (mc)->gregs[REG_ERR]; \
		(fi).trap_no = (mc)->gregs[REG_TRAPNO]; \
	}

#endif
