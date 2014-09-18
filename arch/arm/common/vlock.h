/*
 * vlock.h - simple voting lock implementation
 *
 * Created by:	Dave Martin, 2012-08-16
 * Copyright:	(C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __VLOCK_H
#define __VLOCK_H

#include <asm/mcpm.h>

/* Offsets and sizes are rounded to a word (4 bytes) */
#define VLOCK_OWNER_OFFSET	0
#define VLOCK_VOTING_OFFSET	4
#define VLOCK_VOTING_SIZE	((MAX_CPUS_PER_CLUSTER + 3) / 4 * 4)
#define VLOCK_SIZE		(VLOCK_VOTING_OFFSET + VLOCK_VOTING_SIZE)
#define VLOCK_OWNER_NONE	0

#endif /* ! __VLOCK_H */
