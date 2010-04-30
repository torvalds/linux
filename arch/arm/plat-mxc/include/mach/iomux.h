/*
 * Copyright (C) 2010 Uwe Kleine-Koenig, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __MACH_IOMUX_H__
#define __MACH_IOMUX_H__

/* This file will go away, please include mach/iomux-mx... directly */

#ifdef CONFIG_ARCH_MX1
#include <mach/iomux-mx1.h>
#endif
#ifdef CONFIG_ARCH_MX2
#include <mach/iomux-mx2x.h>
#ifdef CONFIG_MACH_MX21
#include <mach/iomux-mx21.h>
#endif
#ifdef CONFIG_MACH_MX27
#include <mach/iomux-mx27.h>
#endif
#endif

#endif /* __MACH_IOMUX_H__ */
