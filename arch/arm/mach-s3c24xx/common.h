/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for S3C24XX SoCs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_S3C24XX_COMMON_H
#define __ARCH_ARM_MACH_S3C24XX_COMMON_H __FILE__

void s3c2410_restart(char mode, const char *cmd);
void s3c244x_restart(char mode, const char *cmd);

extern struct syscore_ops s3c24xx_irq_syscore_ops;

#endif /* __ARCH_ARM_MACH_S3C24XX_COMMON_H */
