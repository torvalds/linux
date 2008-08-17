#ifndef __UM_ATOMIC_H
#define __UM_ATOMIC_H

/* The i386 atomic.h calls printk, but doesn't include kernel.h, so we
 * include it here.
 */
#include "linux/kernel.h"

#include "asm/arch/atomic.h"

#endif
