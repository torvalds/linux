#ifndef _SPARC_VADDRS_H
#define _SPARC_VADDRS_H

#include <asm/head.h>

/*
 * asm/vaddrs.h:  Here we define the virtual addresses at
 *                      which important things will be mapped.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2000 Anton Blanchard (anton@samba.org)
 */

#define SRMMU_MAXMEM		0x0c000000

#define SRMMU_NOCACHE_VADDR	(KERNBASE + SRMMU_MAXMEM)
				/* = 0x0fc000000 */
/* XXX Empiricals - this needs to go away - KMW */
#define SRMMU_MIN_NOCACHE_PAGES (550)
#define SRMMU_MAX_NOCACHE_PAGES	(1280)

/* The following constant is used in mm/srmmu.c::srmmu_nocache_calcsize()
 * to determine the amount of memory that will be reserved as nocache:
 *
 * 256 pages will be taken as nocache per each
 * SRMMU_NOCACHE_ALCRATIO MB of system memory.
 *
 * limits enforced:	nocache minimum = 256 pages
 *			nocache maximum = 1280 pages
 */
#define SRMMU_NOCACHE_ALCRATIO	64	/* 256 pages per 64MB of system RAM */

#define SUN4M_IOBASE_VADDR	0xfd000000 /* Base for mapping pages */
#define IOBASE_VADDR		0xfe000000
#define IOBASE_END		0xfe600000

#define KADB_DEBUGGER_BEGVM	0xffc00000 /* Where kern debugger is in virt-mem */
#define KADB_DEBUGGER_ENDVM	0xffd00000
#define DEBUG_FIRSTVADDR	KADB_DEBUGGER_BEGVM
#define DEBUG_LASTVADDR		KADB_DEBUGGER_ENDVM

#define LINUX_OPPROM_BEGVM	0xffd00000
#define LINUX_OPPROM_ENDVM	0xfff00000

#define DVMA_VADDR		0xfff00000 /* Base area of the DVMA on suns */
#define DVMA_END		0xfffc0000

#endif /* !(_SPARC_VADDRS_H) */
