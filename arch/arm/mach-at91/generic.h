/*
 * linux/arch/arm/mach-at91/generic.h
 *
 *  Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _AT91_GENERIC_H
#define _AT91_GENERIC_H

#ifdef CONFIG_PM
extern void __init at91rm9200_pm_init(void);
extern void __init at91sam9_pm_init(void);
extern void __init sama5_pm_init(void);
extern void __init sama5d2_pm_init(void);
#else
static inline void __init at91rm9200_pm_init(void) { }
static inline void __init at91sam9_pm_init(void) { }
static inline void __init sama5_pm_init(void) { }
static inline void __init sama5d2_pm_init(void) { }
#endif

#endif /* _AT91_GENERIC_H */
