/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for S5PC100 machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_S5PC100_COMMON_H
#define __ARCH_ARM_MACH_S5PC100_COMMON_H

void s5pc100_init_io(struct map_desc *mach_desc, int size);
void s5pc100_init_irq(void);

void s5pc100_register_clocks(void);
void s5pc100_setup_clocks(void);

void s5pc100_restart(char mode, const char *cmd);

extern  int s5pc100_init(void);
extern void s5pc100_map_io(void);
extern void s5pc100_init_clocks(int xtal);
extern void s5pc100_init_uarts(struct s3c2410_uartcfg *cfg, int no);

#endif /* __ARCH_ARM_MACH_S5PC100_COMMON_H */
