/*
 * arch/arm/mach-ns9xxx/include/mach/module.h
 *
 * Copyright (C) 2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_MODULE_H
#define __ASM_ARCH_MODULE_H

#include <asm/mach-types.h>

#define module_is_cc7ucamry()	(0			\
		|| machine_is_cc7ucamry()		\
		)

#define module_is_cc9c()	(0			\
		)

#define module_is_cc9p9210()	(0			\
		|| machine_is_cc9p9210()		\
		|| machine_is_cc9p9210js()		\
		)

#define module_is_cc9p9215()	(0			\
		|| machine_is_cc9p9215()		\
		|| machine_is_cc9p9215js()		\
		)

#define module_is_cc9p9360()	(0			\
		|| machine_is_cc9p9360dev()		\
		|| machine_is_cc9p9360js()		\
		)

#define module_is_cc9p9750()	(0			\
		|| machine_is_a9m9750()			\
		|| machine_is_cc9p9750js()		\
		|| machine_is_cc9p9750val()		\
		)

#define module_is_ccw9c()	(0			\
		)

#define module_is_inc20otter()	(0			\
		|| machine_is_inc20otter()		\
		)

#define module_is_otter()	(0			\
		|| machine_is_otter()			\
		)

#endif /* ifndef __ASM_ARCH_MODULE_H */
