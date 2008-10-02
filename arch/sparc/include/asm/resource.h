/*
 * resource.h: Resource definitions.
 *
 * Copyright (C) 1995,1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_RESOURCE_H
#define _SPARC_RESOURCE_H

/*
 * These two resource limit IDs have a Sparc/Linux-specific ordering,
 * the rest comes from the generic header:
 */
#define RLIMIT_NOFILE		6	/* max number of open files */
#define RLIMIT_NPROC		7	/* max number of processes */

#if defined(__sparc__) && defined(__arch64__)
/* Use generic version */
#else
/*
 * SuS says limits have to be unsigned.
 * We make this unsigned, but keep the
 * old value for compatibility:
 */
#define RLIM_INFINITY		0x7fffffff
#endif

#include <asm-generic/resource.h>

#endif /* !(_SPARC_RESOURCE_H) */
