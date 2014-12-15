/*
 * mach/smp.h
 *
 */

#ifndef __MACH_MESON8_SMP_H_
#define __MACH_MESON8_SMP_H_

#include <linux/smp.h>
#include <asm/smp.h>

extern struct smp_operations meson_smp_ops;

typedef int (*exl_call_func_t)(void *p_arg);
extern int try_exclu_cpu_exe(exl_call_func_t func, void * p_arg);
extern int pm_notifier_call_chain(unsigned long val);


#endif /* __MACH_MESON8_SMP_H_ */
