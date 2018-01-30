/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common defines for v7m cpus
 */
#define V7M_SCS_ICTR			IOMEM(0xe000e004)
#define V7M_SCS_ICTR_INTLINESNUM_MASK		0x0000000f

#define BASEADDR_V7M_SCB		IOMEM(0xe000ed00)

#define V7M_SCB_CPUID			0x00

#define V7M_SCB_ICSR			0x04
#define V7M_SCB_ICSR_PENDSVSET			(1 << 28)
#define V7M_SCB_ICSR_PENDSVCLR			(1 << 27)
#define V7M_SCB_ICSR_RETTOBASE			(1 << 11)

#define V7M_SCB_VTOR			0x08

#define V7M_SCB_AIRCR			0x0c
#define V7M_SCB_AIRCR_VECTKEY			(0x05fa << 16)
#define V7M_SCB_AIRCR_SYSRESETREQ		(1 << 2)

#define V7M_SCB_SCR			0x10
#define V7M_SCB_SCR_SLEEPDEEP			(1 << 2)

#define V7M_SCB_CCR			0x14
#define V7M_SCB_CCR_STKALIGN			(1 << 9)
#define V7M_SCB_CCR_DC				(1 << 16)
#define V7M_SCB_CCR_IC				(1 << 17)
#define V7M_SCB_CCR_BP				(1 << 18)

#define V7M_SCB_SHPR2			0x1c
#define V7M_SCB_SHPR3			0x20

#define V7M_SCB_SHCSR			0x24
#define V7M_SCB_SHCSR_USGFAULTENA		(1 << 18)
#define V7M_SCB_SHCSR_BUSFAULTENA		(1 << 17)
#define V7M_SCB_SHCSR_MEMFAULTENA		(1 << 16)

#define V7M_xPSR_FRAMEPTRALIGN			0x00000200
#define V7M_xPSR_EXCEPTIONNO			0x000001ff

/*
 * When branching to an address that has bits [31:28] == 0xf an exception return
 * occurs. Bits [27:5] are reserved (SBOP). If the processor implements the FP
 * extension Bit [4] defines if the exception frame has space allocated for FP
 * state information, SBOP otherwise. Bit [3] defines the mode that is returned
 * to (0 -> handler mode; 1 -> thread mode). Bit [2] defines which sp is used
 * (0 -> msp; 1 -> psp). Bits [1:0] are fixed to 0b01.
 */
#define EXC_RET_STACK_MASK			0x00000004
#define EXC_RET_THREADMODE_PROCESSSTACK		0xfffffffd

/* Cache related definitions */

#define	V7M_SCB_CLIDR		0x78	/* Cache Level ID register */
#define	V7M_SCB_CTR		0x7c	/* Cache Type register */
#define	V7M_SCB_CCSIDR		0x80	/* Cache size ID register */
#define	V7M_SCB_CSSELR		0x84	/* Cache size selection register */

/* Memory-mapped MPU registers for M-class */
#define MPU_TYPE		0x90
#define MPU_CTRL		0x94
#define MPU_CTRL_ENABLE		1
#define MPU_CTRL_PRIVDEFENA	(1 << 2)

#define MPU_RNR			0x98
#define MPU_RBAR		0x9c
#define MPU_RASR		0xa0

/* Cache opeartions */
#define	V7M_SCB_ICIALLU		0x250	/* I-cache invalidate all to PoU */
#define	V7M_SCB_ICIMVAU		0x258	/* I-cache invalidate by MVA to PoU */
#define	V7M_SCB_DCIMVAC		0x25c	/* D-cache invalidate by MVA to PoC */
#define	V7M_SCB_DCISW		0x260	/* D-cache invalidate by set-way */
#define	V7M_SCB_DCCMVAU		0x264	/* D-cache clean by MVA to PoU */
#define	V7M_SCB_DCCMVAC		0x268	/* D-cache clean by MVA to PoC */
#define	V7M_SCB_DCCSW		0x26c	/* D-cache clean by set-way */
#define	V7M_SCB_DCCIMVAC	0x270	/* D-cache clean and invalidate by MVA to PoC */
#define	V7M_SCB_DCCISW		0x274	/* D-cache clean and invalidate by set-way */
#define	V7M_SCB_BPIALL		0x278	/* D-cache clean and invalidate by set-way */

#ifndef __ASSEMBLY__

enum reboot_mode;

void armv7m_restart(enum reboot_mode mode, const char *cmd);

#endif /* __ASSEMBLY__ */
