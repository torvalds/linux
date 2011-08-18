/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __SYS_SIGCONTEXT_I386_H
#define __SYS_SIGCONTEXT_I386_H

#include <generated/user_constants.h>

#define SC_OFFSET(sc, field) \
	*((unsigned long *) &(((char *) (sc))[HOST_##field]))

#define SC_TRAPNO(sc) SC_OFFSET(sc, SC_TRAPNO)
#define SC_ERR(sc) SC_OFFSET(sc, SC_ERR)
#define SC_CR2(sc) SC_OFFSET(sc, SC_CR2)

#define GET_FAULTINFO_FROM_SC(fi, sc) \
	{ \
		(fi).cr2 = SC_CR2(sc); \
		(fi).error_code = SC_ERR(sc); \
		(fi).trap_no = SC_TRAPNO(sc); \
	}

#define GET_FAULTINFO_FROM_MC(fi, mc) \
	{ \
		(fi).cr2 = (mc)->cr2; \
		(fi).error_code = (mc)->gregs[REG_ERR]; \
		(fi).trap_no = (mc)->gregs[REG_TRAPNO]; \
	}

#endif
