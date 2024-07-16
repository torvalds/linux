/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/mach-sti/smp.h
 *
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 *		http://www.st.com
 */

#ifndef __MACH_STI_SMP_H
#define __MACH_STI_SMP_H

extern const struct smp_operations sti_smp_ops;

void sti_secondary_startup(void);

#endif
