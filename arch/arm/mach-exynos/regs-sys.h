/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * EXYNOS - system register definition
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_SYS_H
#define __ASM_ARCH_REGS_SYS_H __FILE__

#include <mach/map.h>

#define S5P_SYSREG(x)                          (S3C_VA_SYS + (x))

/* For EXYNOS5 */
#define EXYNOS5_SYS_I2C_CFG                    S5P_SYSREG(0x0234)

#endif /* __ASM_ARCH_REGS_SYS_H */
