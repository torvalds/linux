/*
 * vlock.h - simple voting lock implementation
 *
 * Created by:  Dave Martin, 2012-08-16
 * Copyright:   (C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __VLOCK_H
#define __VLOCK_H

#include <asm/bL_entry.h>

#define VLOCK_OWNER_OFFSET      0
#define VLOCK_VOTING_OFFSET     4
#define VLOCK_VOTING_SIZE       ((BL_CPUS_PER_CLUSTER + 3) / 4 * 4)
#define VLOCK_SIZE              (VLOCK_VOTING_OFFSET + VLOCK_VOTING_SIZE)
#define VLOCK_OWNER_NONE        0

#ifndef __ASSEMBLY__

struct vlock {
	char data[VLOCK_SIZE];
};

int vlock_trylock(struct vlock *lock, unsigned int owner);
void vlock_unlock(struct vlock *lock);

#endif /* __ASSEMBLY__ */
#endif /* ! __VLOCK_H */
