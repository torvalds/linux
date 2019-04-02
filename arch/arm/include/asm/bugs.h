/*
 *  arch/arm/include/asm/s.h
 *
 *  Copyright (C) 1995-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_S_H
#define __ASM_S_H

extern void check_writebuffer_s(void);

#ifdef CONFIG_MMU
extern void check_s(void);
extern void check_other_s(void);
#else
#define check_s() do { } while (0)
#define check_other_s() do { } while (0)
#endif

#endif
