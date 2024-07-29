/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2024, Jiaxun Yang <jiaxun.yang@flygoat.com>
 *  Loongson EFI firmware sleeper routine
 */

#include <asm/asm.h>
#include <asm/pm.h>

#include <kernel-entry-init.h>

LEAF(loongson_lefi_sleep)
	SUSPEND_SAVE
	move	t9, a0
	PTR_LA	a0, wake
	move	a1, sp
	jalr    t9
wake:
	smp_slave_setup
	RESUME_RESTORE_REGS_RETURN
END(loongson_lefi_sleep)
