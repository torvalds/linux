/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/changebit.S
 *
 *  Copyright (C) 1995-1996 Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "bitops.h"
                .text

bitop	_change_bit, eor
