/*
 * arch/arm/mach-ns9xxx/include/mach/board.h
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_BOARD_H
#define __ASM_ARCH_BOARD_H

#include <asm/mach-types.h>

#define board_is_a9m9750dev()	(0			\
		|| machine_is_cc9p9750dev()		\
		)

#define board_is_a9mvali()	(0			\
		|| machine_is_cc9p9750val()		\
		)

#define board_is_jscc9p9210()	(0			\
		|| machine_is_cc9p9210js()		\
		)

#define board_is_jscc9p9215()	(0			\
		|| machine_is_cc9p9215js()		\
		)

#define board_is_jscc9p9360()	(0			\
		|| machine_is_cc9p9360js()		\
		)

#define board_is_uncbas()	(0			\
		|| machine_is_cc7ucamry()		\
		)

#endif /* ifndef __ASM_ARCH_BOARD_H */
