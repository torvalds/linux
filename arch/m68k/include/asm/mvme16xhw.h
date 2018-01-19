/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_MVME16xHW_H_
#define _M68K_MVME16xHW_H_

#include <asm/irq.h>


typedef struct {
	u_char	ack_icr,
		flt_icr,
		sel_icr,
		pe_icr,
		bsy_icr,
		spare1,
		isr,
		cr,
		spare2,
		spare3,
		spare4,
		data;
} MVMElp, *MVMElpPtr;

#define MVME_LPR_BASE	0xfff42030

#define mvmelp   ((*(volatile MVMElpPtr)(MVME_LPR_BASE)))

typedef struct {
	unsigned char
		ctrl,
		bcd_sec,
		bcd_min,
		bcd_hr,
		bcd_dow,
		bcd_dom,
		bcd_mth,
		bcd_year;
} MK48T08_t, *MK48T08ptr_t;

#define RTC_WRITE	0x80
#define RTC_READ	0x40
#define RTC_STOP	0x20

#define MVME_RTC_BASE	0xfffc1ff8

#define MVME_I596_BASE	0xfff46000

#define MVME_SCC_A_ADDR	0xfff45005
#define MVME_SCC_B_ADDR	0xfff45001
#define MVME_SCC_PCLK	10000000

#define MVME162_IRQ_TYPE_PRIO	0

#define MVME167_IRQ_PRN		(IRQ_USER+20)
#define MVME16x_IRQ_I596	(IRQ_USER+23)
#define MVME16x_IRQ_SCSI	(IRQ_USER+21)
#define MVME16x_IRQ_FLY		(IRQ_USER+63)
#define MVME167_IRQ_SER_ERR	(IRQ_USER+28)
#define MVME167_IRQ_SER_MODEM	(IRQ_USER+29)
#define MVME167_IRQ_SER_TX	(IRQ_USER+30)
#define MVME167_IRQ_SER_RX	(IRQ_USER+31)
#define MVME16x_IRQ_TIMER	(IRQ_USER+25)
#define MVME167_IRQ_ABORT	(IRQ_USER+46)
#define MVME162_IRQ_ABORT	(IRQ_USER+30)

/* SCC interrupts, for MVME162 */
#define MVME162_IRQ_SCC_BASE		(IRQ_USER+0)
#define MVME162_IRQ_SCCB_TX		(IRQ_USER+0)
#define MVME162_IRQ_SCCB_STAT		(IRQ_USER+2)
#define MVME162_IRQ_SCCB_RX		(IRQ_USER+4)
#define MVME162_IRQ_SCCB_SPCOND		(IRQ_USER+6)
#define MVME162_IRQ_SCCA_TX		(IRQ_USER+8)
#define MVME162_IRQ_SCCA_STAT		(IRQ_USER+10)
#define MVME162_IRQ_SCCA_RX		(IRQ_USER+12)
#define MVME162_IRQ_SCCA_SPCOND		(IRQ_USER+14)

/* MVME162 version register */

#define MVME162_VERSION_REG	0xfff4202e

extern unsigned short mvme16x_config;

/* Lower 8 bits must match the revision register in the MC2 chip */

#define MVME16x_CONFIG_SPEED_32		0x0001
#define MVME16x_CONFIG_NO_VMECHIP2	0x0002
#define MVME16x_CONFIG_NO_SCSICHIP	0x0004
#define MVME16x_CONFIG_NO_ETHERNET	0x0008
#define MVME16x_CONFIG_GOT_FPU		0x0010

#define MVME16x_CONFIG_GOT_LP		0x0100
#define MVME16x_CONFIG_GOT_CD2401	0x0200
#define MVME16x_CONFIG_GOT_SCCA		0x0400
#define MVME16x_CONFIG_GOT_SCCB		0x0800

#endif
