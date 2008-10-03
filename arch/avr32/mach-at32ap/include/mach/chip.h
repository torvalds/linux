/*
 * AVR32 chip-specific definitions
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_ARCH_CHIP_H__
#define __ASM_AVR32_ARCH_CHIP_H__

#if defined(CONFIG_CPU_AT32AP700X)
# include <mach/at32ap700x.h>
#else
# error Unknown chip type selected
#endif

#endif /* __ASM_AVR32_ARCH_CHIP_H__ */
