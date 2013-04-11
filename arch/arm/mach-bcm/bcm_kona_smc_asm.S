/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/linkage.h>
#include "bcm_kona_smc.h"

/*
 * int bcm_kona_smc_asm(u32 service_id, u32 buffer_addr)
 */

ENTRY(bcm_kona_smc_asm)
	stmfd	sp!, {r4-r12, lr}
	mov	r4, r0		@ service_id
	mov	r5, #3		@ Keep IRQ and FIQ off in SM
	/*
	 * Since interrupts are disabled in the open mode, we must keep
	 * interrupts disabled in secure mode by setting R5=0x3. If interrupts
	 * are enabled in open mode, we can set R5=0x0 to allow interrupts in
	 * secure mode.  If we did this, the secure monitor would return back
	 * control to the open mode to handle the interrupt prior to completing
	 * the secure service. If this happened, R12 would not be
	 * SEC_EXIT_NORMAL and we would need to call SMC again after resetting
	 * R5 (it gets clobbered by the secure monitor) and setting R4 to
	 * SSAPI_RET_FROM_INT_SERV to indicate that we want the secure monitor
	 * to finish up the previous uncompleted secure service.
	 */
	mov	r6, r1		@ buffer_addr
	smc	#0
	/* Check r12 for SEC_EXIT_NORMAL here if interrupts are enabled */
	ldmfd	sp!, {r4-r12, pc}
ENDPROC(bcm_kona_smc_asm)
