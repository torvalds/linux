/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_PARAM_H
#define _ASM_MICROBLAZE_PARAM_H

#ifdef __KERNEL__
#define HZ		CONFIG_HZ	/* internal kernel timer frequency */
#define USER_HZ		100		/* for user interfaces in "ticks" */
#define CLOCKS_PER_SEC	(USER_HZ)	/* frequency at which times() counts */
#endif /* __KERNEL__ */

#ifndef HZ
#define HZ 100
#endif

#define EXEC_PAGESIZE	4096

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif /* _ASM_MICROBLAZE_PARAM_H */
