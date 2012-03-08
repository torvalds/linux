/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for S3C2410 machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_S3C2410_COMMON_H
#define __ARCH_ARM_MACH_S3C2410_COMMON_H

#ifdef CONFIG_CPU_S3C2410
void s3c2410_restart(char mode, const char *cmd);
#endif

#ifdef CONFIG_CPU_S3C2440
void s3c2440_restart(char mode, const char *cmd);
#endif

#endif /* __ARCH_ARM_MACH_S3C2410_COMMON_H */
