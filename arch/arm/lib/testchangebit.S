/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/testchangebit.S
 *
 *  Copyright (C) 1995-1996 Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "bitops.h"
                .text

testop	_test_and_change_bit, eor, str
