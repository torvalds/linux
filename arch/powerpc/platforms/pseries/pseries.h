/*
 * Copyright 2006 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _PSERIES_PSERIES_H
#define _PSERIES_PSERIES_H

extern void __init fw_feature_init(const char *hypertas, unsigned long len);

struct pt_regs;

extern int pSeries_system_reset_exception(struct pt_regs *regs);
extern int pSeries_machine_check_exception(struct pt_regs *regs);

#ifdef CONFIG_SMP
extern void smp_init_pseries_mpic(void);
extern void smp_init_pseries_xics(void);
#else
static inline void smp_init_pseries_mpic(void) { };
static inline void smp_init_pseries_xics(void) { };
#endif

#ifdef CONFIG_KEXEC
extern void setup_kexec_cpu_down_xics(void);
extern void setup_kexec_cpu_down_mpic(void);
#else
static inline void setup_kexec_cpu_down_xics(void) { }
static inline void setup_kexec_cpu_down_mpic(void) { }
#endif

extern void pSeries_final_fixup(void);

/* Poweron flag used for enabling auto ups restart */
extern unsigned long rtas_poweron_auto;

#endif /* _PSERIES_PSERIES_H */
