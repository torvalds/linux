/*
 * arch/arm/mach-sun6i/clock/ccu_sysfs.h
 * (c) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * James Deng <csjamesdeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __CCU_SYSFS_H
#define __CCU_SYSFS_H

#define CCU_ASSERT_GOTO(x, errline, pos) if (!(x)) {errline = __LINE__; goto pos;}

#endif
