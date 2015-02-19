/*
 * Keystone Secure APIs
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 * 	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

/**
 * u32 keystone_cpu_smc(u32 command, u32 cpu, u32 addr)
 *
 * Low level CPU monitor API
 * @command:	Monitor command.
 * @cpu:	CPU Number
 * @addr:	Kernel jump address for boot CPU
 *
 * Return: Non zero value on failure
 */
ENTRY(keystone_cpu_smc)
	stmfd   sp!, {r4-r11, lr}
	smc	#0
	ldmfd   sp!, {r4-r11, pc}
ENDPROC(keystone_cpu_smc)
