/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com>
 * Author: Sundar Iyer <sundar.iyer@stericsson.com>
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com>
 *
 * License Terms: GNU General Public License v2
 *
 * PRCM Unit f/w API
 */
#ifndef __MACH_PRCMU_H
#define __MACH_PRCMU_H
#include <mach/prcmu-defs.h>

void __init prcmu_early_init(void);
int prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size);
int prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size);
int prcmu_set_ape_opp(enum prcmu_ape_opp opp);
int prcmu_set_cpu_opp(enum prcmu_cpu_opp opp);
int prcmu_set_ape_cpu_opps(enum prcmu_ape_opp ape_opp,
			   enum prcmu_cpu_opp cpu_opp);
int prcmu_get_ape_opp(void);
int prcmu_get_cpu_opp(void);
bool prcmu_has_arm_maxopp(void);

#endif /* __MACH_PRCMU_H */
