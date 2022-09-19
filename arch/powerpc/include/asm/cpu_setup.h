/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 IBM Corporation
 */

#ifndef _ASM_POWERPC_CPU_SETUP_H
#define _ASM_POWERPC_CPU_SETUP_H
void __setup_cpu_power7(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_power8(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_power9(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_power10(unsigned long offset, struct cpu_spec *spec);
void __restore_cpu_power7(void);
void __restore_cpu_power8(void);
void __restore_cpu_power9(void);
void __restore_cpu_power10(void);

void __setup_cpu_e500v1(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_e500v2(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_e500mc(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_440ep(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_440epx(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_440gx(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_440grx(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_440spe(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_440x5(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_460ex(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_460gt(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_460sx(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_apm821xx(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_603(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_604(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_750(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_750cx(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_750fx(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_7400(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_7410(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_745x(unsigned long offset, struct cpu_spec *spec);

void __setup_cpu_ppc970(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_ppc970MP(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_pa6t(unsigned long offset, struct cpu_spec *spec);
void __restore_cpu_pa6t(void);
void __restore_cpu_ppc970(void);

void __setup_cpu_e5500(unsigned long offset, struct cpu_spec *spec);
void __setup_cpu_e6500(unsigned long offset, struct cpu_spec *spec);
void __restore_cpu_e5500(void);
void __restore_cpu_e6500(void);
#endif /* _ASM_POWERPC_CPU_SETUP_H */
