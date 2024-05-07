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
	jalr    a0
    smp_slave_setup
	RESUME_RESTORE_REGS_RETURN
END(loongson_lefi_sleep)
