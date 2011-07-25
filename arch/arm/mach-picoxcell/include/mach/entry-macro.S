/*
 * entry-macro.S
 *
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * Low-level IRQ helper macros for picoXcell platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/map.h>

#define VA_VIC0		IO_ADDRESS(PICOXCELL_VIC0_BASE)
#define VA_VIC1		IO_ADDRESS(PICOXCELL_VIC1_BASE)

#include <asm/entry-macro-vic2.S>
