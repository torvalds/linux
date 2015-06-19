/*
 * Low-level power-management support for Alpine platform.
 *
 * Copyright (C) 2015 Annapurna Labs Ltd.
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
 */

#ifndef __ALPINE_CPU_PM_H__
#define __ALPINE_CPU_PM_H__

/* Alpine CPU Power Management Services Initialization */
void alpine_cpu_pm_init(void);

/* Wake-up a CPU */
int alpine_cpu_wakeup(unsigned int phys_cpu, uint32_t phys_resume_addr);

#endif /* __ALPINE_CPU_PM_H__ */
