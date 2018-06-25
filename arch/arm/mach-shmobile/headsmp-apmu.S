/*
 * SMP support for APMU based systems with Cortex A7/A15
 *
 * Copyright (C) 2014  Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

#ifdef CONFIG_SMP
ENTRY(shmobile_boot_apmu)
	bl	secure_cntvoff_init
	b	secondary_startup
ENDPROC(shmobile_boot_apmu)
#endif
