/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

#include "rv1106_pm.h"

#define RV1106_GPIO0_INT_ST		0xff380050
#define RV1106_PMUGRF_OS_REG10		0xff020228
#define RV1106_PMUGRF_SOC_CON4		0xff020010
#define RV1106_CRU_GLB_SRST_FST		0xff3b0c08

#define RV1106_CRU_GLB_RST_CON_ADDR	0xff3b0c10
#define CRU_FST_RST_PMU_VAL		0x000c000c

#if RV1106_SLEEP_DEBUG
/********************* console used for sleep.S ******************************/
#define UART_REG_DLL	(0x00)
#define UART_REG_DLH	(0x04)
#define UART_REG_IER	(0x04)
#define UART_REG_FCR	(0x08)
#define UART_REG_LCR	(0x0c)
#define UART_REG_MCR	(0x10)

#define UARTLCR_DLAB	(1 << 7)
#define UARTFCR_DMAEN	(1 << 3)
#define UARTFCR_FIFOEN	(1 << 0)

#define CONSOLE_UART_BASE	0xff4c0000
#define CONSOLE_CLKRATE 	24000000
#define CONSOLE_BAUDRATE	115200

#define GPIO1_B_IOMUX		0xff538008
#define GRF_GPIO1D_VAL		0xff002200

.macro early_console_init
	ldr r0, =GPIO1_B_IOMUX
	ldr r1, =GRF_GPIO1D_VAL
	str r1, [r0]

	ldr	r0, =CONSOLE_UART_BASE
	ldr	r1, =CONSOLE_CLKRATE
	ldr	r2, =CONSOLE_BAUDRATE
	/* Program the baudrate */
	/* Divisor =  Uart clock / (16 * baudrate) */
	mov	r1, #0xd
	mov	r2, #0x0
	ldr	r3, [r0, #UART_REG_LCR]
	orr	r3, r3, #UARTLCR_DLAB
	str	r3, [r0, #UART_REG_LCR] /* enable DLL, DLH programming */
	str	r1, [r0, #UART_REG_DLL] /* program DLL */
	str	r2, [r0, #UART_REG_DLH] /* program DLH */
	mov	r2, #~UARTLCR_DLAB
	and	r3, r3, r2
	str	r3, [r0, #UART_REG_LCR] /* disable DLL, DLH programming */

	/* 8n1 */
	mov	r3, #3
	str	r3, [r0, #UART_REG_LCR]
	/* no interrupt */
	mov	r3, #0
	str	r3, [r0, #UART_REG_IER]
	/* enable fifo, DMA */
	mov	r3, #(UARTFCR_FIFOEN | UARTFCR_DMAEN)
	str	r3, [r0, #UART_REG_FCR]
	/* DTR + RTS */
	mov	r3, #3
	str	r3, [r0, #UART_REG_MCR]
	mov	r0, #1
	dsb	sy
.endm

.macro early_console_putc ch
	ldr	r0, =CONSOLE_UART_BASE
	mov	r1, #\ch
	str	r1, [r0]
.endm
/********************* console used for sleep.S ******************************/
#endif

.align	2
.arm


ENTRY(rockchip_slp_cpu_resume)
#if RV1106_SLEEP_DEBUG
	early_console_init
	/* print 'A' */
	early_console_putc 0x41
#endif

	setmode	PSR_I_BIT | PSR_F_BIT | SVC_MODE, r1  @ set svc, irqs off

#if RV1106_WAKEUP_TO_SYSTEM_RESET
	/* save gpio wakeup src */
	ldr	r0, =RV1106_PMUGRF_OS_REG10
	ldr	r1, =RV1106_GPIO0_INT_ST
	ldr	r1, [r1]
	str	r1, [r0]

	/* enable first reset trigger pmu reset */
	ldr	r0, =RV1106_CRU_GLB_RST_CON_ADDR
	ldr	r1, =CRU_FST_RST_PMU_VAL
	str	r1, [r0]

	/* clear pmu reset hold */
	ldr	r0, =RV1106_PMUGRF_SOC_CON4
	ldr	r1, =0xffff0000
	str	r1, [r0]
	add	r0, r0, #4
	str	r1, [r0]

	/* first reset */
	ldr	r0, =RV1106_CRU_GLB_SRST_FST
	mov	r1, #0xfdb9
	str	r1, [r0]
	b	.
#endif

	ldr	r3, rkpm_bootdata_l2ctlr_f
	cmp	r3, #0
	beq	sp_set
	ldr	r3, rkpm_bootdata_l2ctlr
	mcr	p15, 1, r3, c9, c0, 2
sp_set:
	ldr	sp, rkpm_bootdata_cpusp

	ldr	r0, rkpm_ddr_data
	ldr	r1, rkpm_ddr_func
	cmp	r1, #0
	beq	boot
	blx	r1

boot:
	ldr	r1, rkpm_bootdata_cpu_code
	bx	r1
ENDPROC(rockchip_slp_cpu_resume)

/* Parameters filled in by the kernel */

/* Flag for whether to restore L2CTLR on resume */
	.global rkpm_bootdata_l2ctlr_f
rkpm_bootdata_l2ctlr_f:
	.long 0

/* Saved L2CTLR to restore on resume */
	.global rkpm_bootdata_l2ctlr
rkpm_bootdata_l2ctlr:
	.long 0

/* CPU resume SP addr */
	.globl rkpm_bootdata_cpusp
rkpm_bootdata_cpusp:
	.long 0

/* CPU resume function (physical address) */
	.globl rkpm_bootdata_cpu_code
rkpm_bootdata_cpu_code:
	.long 0

/* ddr resume data */
	.globl rkpm_ddr_data
rkpm_ddr_data:
	.long 0

/* ddr resume function (physical address) */
	.globl rkpm_ddr_func
rkpm_ddr_func:
	.long 0

ENTRY(rv1106_bootram_sz)
        .word   . - rockchip_slp_cpu_resume
