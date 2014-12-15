/*
 * Meson secure APIs file.
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *  Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Secure Definition
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * Author: Platform-BJ@amlogic.com
 *
 */

#include <linux/linkage.h>
#include <mach/meson-secure.h>

/*
 * This is common routine to manage secure monitor API
 * used to modify the PL310 secure registers.
 * 'r1' contains the value to be modified and 'r0' contains
 * the monitor API number. It uses few CPU registers
 * internally and hence they need be backed up including
 * link register "lr".
 * Function signature : void meson_smc1(u32 fn, u32 arg)
 */

ENTRY(meson_smc1)
    stmfd sp!, {r2-r12, lr}
    mov r2, r1
    mov r1, r0
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_smc1)

ENTRY(meson_smc_hal_api)
	stmfd sp!, {r3-r12, lr}	
	mov r2, r1
	mov r1, r0
	ldr r0, =CALL_TRUSTZONE_HAL_API
	dsb
	smc #0
	ldmfd sp!, {r3-r12, pc}
ENDPROC(meson_smc_hal_api)

ENTRY(meson_smc2)
    stmfd sp!, {r2-r12, lr}
    mov r1, r0
    ldr r0, =0x2
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_smc2)

ENTRY(meson_smc3)
    stmfd sp!, {r2-r12, lr}
    mov r2, r1
    mov r1, r0
    ldr r0, =0x3
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_smc3)

ENTRY(meson_read_corectrl)
    stmfd sp!, {r2-r12, lr}
    ldr r1, =TRUSTZONE_MON_CORE_RD_CTRL_INDEX
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_read_corectrl)

ENTRY(meson_modify_corectrl)
    stmfd sp!, {r2-r12, lr}
    mov r2, r0
    ldr r1, =TRUSTZONE_MON_CORE_WR_CTRL_INDEX
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_modify_corectrl)

ENTRY(meson_read_corestatus)
    stmfd sp!, {r2-r12, lr}
    ldr r1, =TRUSTZONE_MON_CORE_RD_STATUS0_INDEX
    add r1, r1, r0, lsl #1
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_read_corestatus)

ENTRY(meson_modify_corestatus)
    stmfd sp!, {r2-r12, lr}
    mov r2, r1
    ldr r1, =TRUSTZONE_MON_CORE_WR_STATUS0_INDEX
    add r1, r1, r0, lsl #1
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_modify_corestatus)

ENTRY(meson_auxcoreboot_addr)
    stmfd sp!, {r2-r12, lr}
    mov r3, r1
    mov r2, r0
    ldr r1, =TRUSTZONE_MON_CORE_BOOTADDR_INDEX
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_auxcoreboot_addr)

ENTRY(meson_suspend_firmware)
    stmfd sp!, {r2-r12, lr}
    mov r2, r0
    ldr r1, =TRUSTZONE_MON_SUSPNED_FIRMWARE
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_suspend_firmware)

ENTRY(meson_read_socrev1)
    stmfd sp!, {r2-r12, lr}    
    ldr r1, =TRUSTZONE_MON_CORE_RD_SOC_REV1
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_read_socrev1)

ENTRY(meson_read_socrev2)
    stmfd sp!, {r2-r12, lr}    
    ldr r1, =TRUSTZONE_MON_CORE_RD_SOC_REV2
    ldr r0, =CALL_TRUSTZONE_MON
    dsb
    smc #0
    ldmfd sp!, {r2-r12, pc}
ENDPROC(meson_read_socrev2)
