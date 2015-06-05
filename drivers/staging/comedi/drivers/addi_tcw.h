#ifndef _ADDI_TCW_H
#define _ADDI_TCW_H

/*
 * Following are the generic definitions for the ADDI-DATA timer/counter/
 * watchdog (TCW) registers and bits. Some of the registers are not used
 * depending on the use of the TCW.
 */

#define ADDI_TCW_VAL_REG		0x00

#define ADDI_TCW_SYNC_REG		0x00
#define ADDI_TCW_SYNC_CTR_TRIG		(1 << 8)
#define ADDI_TCW_SYNC_CTR_DIS		(1 << 7)
#define ADDI_TCW_SYNC_CTR_ENA		(1 << 6)
#define ADDI_TCW_SYNC_TIMER_TRIG	(1 << 5)
#define ADDI_TCW_SYNC_TIMER_DIS		(1 << 4)
#define ADDI_TCW_SYNC_TIMER_ENA		(1 << 3)
#define ADDI_TCW_SYNC_WDOG_TRIG		(1 << 2)
#define ADDI_TCW_SYNC_WDOG_DIS		(1 << 1)
#define ADDI_TCW_SYNC_WDOG_ENA		(1 << 0)

#define ADDI_TCW_RELOAD_REG		0x04

#define ADDI_TCW_TIMEBASE_REG		0x08

#define ADDI_TCW_CTRL_REG		0x0c
#define ADDI_TCW_CTRL_EXT_CLK_STATUS	(1 << 21)
#define ADDI_TCW_CTRL_CASCADE		(1 << 20)
#define ADDI_TCW_CTRL_CNTR_ENA		(1 << 19)
#define ADDI_TCW_CTRL_CNT_UP		(1 << 18)
#define ADDI_TCW_CTRL_EXT_CLK(x)	((x) << 16)
#define ADDI_TCW_CTRL_OUT(x)		((x) << 11)
#define ADDI_TCW_CTRL_GATE		(1 << 10)
#define ADDI_TCW_CTRL_TRIG		(1 << 9)
#define ADDI_TCW_CTRL_EXT_GATE(x)	((x) << 7)
#define ADDI_TCW_CTRL_EXT_TRIG(x)	((x) << 5)
#define ADDI_TCW_CTRL_TIMER_ENA		(1 << 4)
#define ADDI_TCW_CTRL_RESET_ENA		(1 << 3)
#define ADDI_TCW_CTRL_WARN_ENA		(1 << 2)
#define ADDI_TCW_CTRL_IRQ_ENA		(1 << 1)
#define ADDI_TCW_CTRL_ENA		(1 << 0)

#define ADDI_TCW_STATUS_REG		0x10
#define ADDI_TCW_STATUS_SOFT_CLR	(1 << 3)
#define ADDI_TCW_STATUS_SOFT_TRIG	(1 << 1)
#define ADDI_TCW_STATUS_OVERFLOW	(1 << 0)

#define ADDI_TCW_IRQ_REG		0x14
#define ADDI_TCW_IRQ			(1 << 0)

#define ADDI_TCW_WARN_TIMEVAL_REG	0x18

#define ADDI_TCW_WARN_TIMEBASE_REG	0x1c

#endif
