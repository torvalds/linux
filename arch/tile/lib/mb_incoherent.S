/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Assembly code for invoking the HV's fence_incoherent syscall.
 */

#include <linux/linkage.h>
#include <hv/syscall_public.h>
#include <arch/abi.h>
#include <arch/chip.h>

#if !CHIP_HAS_MF_WAITS_FOR_VICTIMS()

/*
 * Invoke the hypervisor's fence_incoherent syscall, which guarantees
 * that all victims for cachelines homed on this tile have reached memory.
 */
STD_ENTRY(__mb_incoherent)
	moveli TREG_SYSCALL_NR_NAME, HV_SYS_fence_incoherent
	swint2
	jrp lr
	STD_ENDPROC(__mb_incoherent)

#endif
