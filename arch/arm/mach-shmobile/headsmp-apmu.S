/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SMP support for APMU based systems with Cortex A7/A15
 *
 * Copyright (C) 2014  Renesas Electronics Corporation
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

ENTRY(shmobile_boot_apmu)
	bl	secure_cntvoff_init
	b	secondary_startup
ENDPROC(shmobile_boot_apmu)
