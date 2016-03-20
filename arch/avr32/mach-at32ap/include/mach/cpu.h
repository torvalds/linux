/*
 * AVR32 CPU identification
 *
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_CPU_H
#define __ASM_ARCH_CPU_H

/*
 * Only AT32AP7000 is defined for now. We can identify the specific
 * chip at runtime, but I'm not sure if it's really worth it.
 */
#ifdef CONFIG_CPU_AT32AP700X
# define cpu_is_at32ap7000()	(1)
#else
# define cpu_is_at32ap7000()	(0)
#endif

#endif /* __ASM_ARCH_CPU_H */
