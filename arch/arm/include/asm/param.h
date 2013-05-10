/*
 *  arch/arm/include/asm/param.h
 *
 *  Copyright (C) 1995-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PARAM_H
#define __ASM_PARAM_H

#ifdef __KERNEL__
# define HZ		CONFIG_HZ	/* Internal kernel timer frequency */
# define USER_HZ	100		/* User interfaces are in "ticks" */
# define CLOCKS_PER_SEC	(USER_HZ)	/* like times() */
#else
# define HZ		100
#endif

#define EXEC_PAGESIZE	4096

#ifndef NOGROUP
#define NOGROUP         (-1)
#endif

/* max length of hostname */
#define MAXHOSTNAMELEN  64

#endif

