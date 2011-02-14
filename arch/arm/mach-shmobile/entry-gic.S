/*
 * ARM Interrupt demux handler using GIC
 *
 * Copyright (C) 2010 Magnus Damm
 * Copyright (C) 2011 Paul Mundt
 * Copyright (C) 2010 - 2011 Renesas Solutions Corp.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <asm/assembler.h>
#include <asm/entry-macro-multi.S>
#include <asm/hardware/gic.h>
#include <asm/hardware/entry-macro-gic.S>

	arch_irq_handler shmobile_handle_irq_gic
