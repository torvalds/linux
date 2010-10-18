/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * PRCMU f/w APIs
 */
#ifndef __MACH_PRCMU_H
#define __MACH_PRCMU_H

int prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size);
int prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size);

#endif /* __MACH_PRCMU_H */
