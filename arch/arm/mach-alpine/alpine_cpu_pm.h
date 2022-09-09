/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Low-level power-management support for Alpine platform.
 *
 * Copyright (C) 2015 Annapurna Labs Ltd.
 */

#ifndef __ALPINE_CPU_PM_H__
#define __ALPINE_CPU_PM_H__

/* Alpine CPU Power Management Services Initialization */
void alpine_cpu_pm_init(void);

/* Wake-up a CPU */
int alpine_cpu_wakeup(unsigned int phys_cpu, uint32_t phys_resume_addr);

#endif /* __ALPINE_CPU_PM_H__ */
