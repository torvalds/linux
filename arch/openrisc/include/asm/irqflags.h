/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */

#ifndef ___ASM_OPENRISC_IRQFLAGS_H
#define ___ASM_OPENRISC_IRQFLAGS_H

#include <asm/spr_defs.h>

#define ARCH_IRQ_DISABLED        0x00
#define ARCH_IRQ_ENABLED         (SPR_SR_IEE|SPR_SR_TEE)

#include <asm-generic/irqflags.h>

#endif /* ___ASM_OPENRISC_IRQFLAGS_H */
