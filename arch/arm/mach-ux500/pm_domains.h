/*
 * Copyright (C) 2014 Linaro Ltd.
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __MACH_UX500_PM_DOMAINS_H
#define __MACH_UX500_PM_DOMAINS_H

#ifdef CONFIG_PM_GENERIC_DOMAINS
extern int __init ux500_pm_domains_init(void);
#else
static inline int ux500_pm_domains_init(void) { return 0; }
#endif

#endif
