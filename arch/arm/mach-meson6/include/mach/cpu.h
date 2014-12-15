/*
 * cpu.h
 *
 *  Created on: May 24, 2012
 *      Author: jerry.yu
 */

#ifndef __MACH_MESON6_CPU_H_
#define __MACH_MESON6_CPU_H_

#include <plat/cpu.h>

extern int (*get_cpu_temperature_celius)(void);
int get_cpu_temperature(void);

#define MESON_CPU_TYPE	MESON_CPU_TYPE_MESON6

#endif /* __MACH_MESON6_CPU_H_ */
