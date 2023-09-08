// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Nuvoton Technology corporation.
// Copyright 2018 Google, Inc.

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

.arch armv7-a

/*
 * The boot ROM does not start secondary CPUs in SVC mode, so we need to do that
 * here.
 */
ENTRY(npcm7xx_secondary_startup)
	safe_svcmode_maskall r0

	b	secondary_startup
ENDPROC(npcm7xx_secondary_startup)
