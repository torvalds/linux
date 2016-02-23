/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_BARRIER_H
#define __ASM_AVR32_BARRIER_H

/*
 * Weirdest thing ever.. no full barrier, but it has a write barrier!
 */
#define wmb()	asm volatile("sync 0" : : : "memory")

#ifdef CONFIG_SMP
# error "The AVR32 port does not support SMP"
#endif

#include <asm-generic/barrier.h>

#endif /* __ASM_AVR32_BARRIER_H */
