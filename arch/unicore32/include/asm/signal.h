/*
 * linux/arch/unicore32/include/asm/signal.h
 *
 * Code specific to UniCore ISA
 *
 * Copyright (C) 2014 GUAN Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE32_ASM_SIGNAL_H__
#define __UNICORE32_ASM_SIGNAL_H__

#ifdef CONFIG_UNICORE32_OLDABI
#define SA_RESTORER	0x04000000
#endif

#include <asm-generic/signal.h>

#endif /* __UNICORE32_ASM_SIGNAL_H__ */
