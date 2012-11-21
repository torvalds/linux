/*
 * include/asm-xtensa/sockios.h
 *
 * Socket-level I/O control calls.  Copied from MIPS.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 * Copyright (C) 2001 Tensilica Inc.
 */

#ifndef _XTENSA_SOCKIOS_H
#define _XTENSA_SOCKIOS_H

#include <asm/ioctl.h>

/* Socket-level I/O control calls. */

#define FIOGETOWN	_IOR('f', 123, int)
#define FIOSETOWN 	_IOW('f', 124, int)

#define SIOCATMARK	_IOR('s', 7, int)
#define SIOCSPGRP	_IOW('s', 8, pid_t)
#define SIOCGPGRP	_IOR('s', 9, pid_t)

#define SIOCGSTAMP	0x8906		/* Get stamp (timeval) */
#define SIOCGSTAMPNS	0x8907		/* Get stamp (timespec) */

#endif	/* _XTENSA_SOCKIOS_H */
