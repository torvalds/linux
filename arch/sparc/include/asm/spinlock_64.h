/* SPDX-License-Identifier: GPL-2.0 */
/* spinlock.h: 64-bit Sparc spinlock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SPINLOCK_H
#define __SPARC64_SPINLOCK_H

#ifndef __ASSEMBLY__

#include <asm/processor.h>
#include <asm/barrier.h>
#include <asm/qrwlock.h>
#include <asm/qspinlock.h>

#define arch_read_lock_flags(p, f) arch_read_lock(p)
#define arch_write_lock_flags(p, f) arch_write_lock(p)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SPINLOCK_H) */
