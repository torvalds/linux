/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/arm/mach-at91/generic.h
 *
 *  Copyright (C) 2005 David Brownell
 */

#ifndef _AT91_GENERIC_H
#define _AT91_GENERIC_H

#ifdef CONFIG_PM
extern void __init at91rm9200_pm_init(void);
extern void __init at91sam9_pm_init(void);
extern void __init sam9x60_pm_init(void);
extern void __init sam9x7_pm_init(void);
extern void __init sama5_pm_init(void);
extern void __init sama5d2_pm_init(void);
extern void __init sama7_pm_init(void);
#else
static inline void __init at91rm9200_pm_init(void) { }
static inline void __init at91sam9_pm_init(void) { }
static inline void __init sam9x60_pm_init(void) { }
static inline void __init sam9x7_pm_init(void) { }
static inline void __init sama5_pm_init(void) { }
static inline void __init sama5d2_pm_init(void) { }
static inline void __init sama7_pm_init(void) { }
#endif

#endif /* _AT91_GENERIC_H */
