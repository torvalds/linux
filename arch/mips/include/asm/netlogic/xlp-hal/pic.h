/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NLM_HAL_PIC_H
#define _NLM_HAL_PIC_H

/* PIC Specific registers */
#define PIC_CTRL                0x00

/* PIC control register defines */
#define PIC_CTRL_ITV		32 /* interrupt timeout value */
#define PIC_CTRL_ICI		19 /* ICI interrupt timeout enable */
#define PIC_CTRL_ITE		18 /* interrupt timeout enable */
#define PIC_CTRL_STE		10 /* system timer interrupt enable */
#define PIC_CTRL_WWR1		8  /* watchdog 1 wraparound count for reset */
#define PIC_CTRL_WWR0		6  /* watchdog 0 wraparound count for reset */
#define PIC_CTRL_WWN1		4  /* watchdog 1 wraparound count for NMI */
#define PIC_CTRL_WWN0		2  /* watchdog 0 wraparound count for NMI */
#define PIC_CTRL_WTE		0  /* watchdog timer enable */

/* PIC Status register defines */
#define PIC_ICI_STATUS		33 /* ICI interrupt timeout status */
#define PIC_ITE_STATUS		32 /* interrupt timeout status */
#define PIC_STS_STATUS		4  /* System timer interrupt status */
#define PIC_WNS_STATUS		2  /* NMI status for watchdog timers */
#define PIC_WIS_STATUS		0  /* Interrupt status for watchdog timers */

/* PIC IPI control register offsets */
#define PIC_IPICTRL_NMI		32
#define PIC_IPICTRL_RIV		20 /* received interrupt vector */
#define PIC_IPICTRL_IDB		16 /* interrupt destination base */
#define PIC_IPICTRL_DTE		 0 /* interrupt destination thread enables */

/* PIC IRT register offsets */
#define PIC_IRT_ENABLE		31
#define PIC_IRT_NMI		29
#define PIC_IRT_SCH		28 /* Scheduling scheme */
#define PIC_IRT_RVEC		20 /* Interrupt receive vectors */
#define PIC_IRT_DT		19 /* Destination type */
#define PIC_IRT_DB		16 /* Destination base */
#define PIC_IRT_DTE		0  /* Destination thread enables */

#define PIC_BYTESWAP            0x02
#define PIC_STATUS              0x04
#define PIC_INTR_TIMEOUT	0x06
#define PIC_ICI0_INTR_TIMEOUT	0x08
#define PIC_ICI1_INTR_TIMEOUT	0x0a
#define PIC_ICI2_INTR_TIMEOUT	0x0c
#define PIC_IPI_CTL		0x0e
#define PIC_INT_ACK             0x10
#define PIC_INT_PENDING0        0x12
#define PIC_INT_PENDING1        0x14
#define PIC_INT_PENDING2        0x16

#define PIC_WDOG0_MAXVAL        0x18
#define PIC_WDOG0_COUNT         0x1a
#define PIC_WDOG0_ENABLE0       0x1c
#define PIC_WDOG0_ENABLE1       0x1e
#define PIC_WDOG0_BEATCMD       0x20
#define PIC_WDOG0_BEAT0         0x22
#define PIC_WDOG0_BEAT1         0x24

#define PIC_WDOG1_MAXVAL        0x26
#define PIC_WDOG1_COUNT         0x28
#define PIC_WDOG1_ENABLE0       0x2a
#define PIC_WDOG1_ENABLE1       0x2c
#define PIC_WDOG1_BEATCMD       0x2e
#define PIC_WDOG1_BEAT0         0x30
#define PIC_WDOG1_BEAT1         0x32

#define PIC_WDOG_MAXVAL(i)      (PIC_WDOG0_MAXVAL + ((i) ? 7 : 0))
#define PIC_WDOG_COUNT(i)       (PIC_WDOG0_COUNT + ((i) ? 7 : 0))
#define PIC_WDOG_ENABLE0(i)     (PIC_WDOG0_ENABLE0 + ((i) ? 7 : 0))
#define PIC_WDOG_ENABLE1(i)     (PIC_WDOG0_ENABLE1 + ((i) ? 7 : 0))
#define PIC_WDOG_BEATCMD(i)     (PIC_WDOG0_BEATCMD + ((i) ? 7 : 0))
#define PIC_WDOG_BEAT0(i)       (PIC_WDOG0_BEAT0 + ((i) ? 7 : 0))
#define PIC_WDOG_BEAT1(i)       (PIC_WDOG0_BEAT1 + ((i) ? 7 : 0))

#define PIC_TIMER0_MAXVAL    0x34
#define PIC_TIMER1_MAXVAL    0x36
#define PIC_TIMER2_MAXVAL    0x38
#define PIC_TIMER3_MAXVAL    0x3a
#define PIC_TIMER4_MAXVAL    0x3c
#define PIC_TIMER5_MAXVAL    0x3e
#define PIC_TIMER6_MAXVAL    0x40
#define PIC_TIMER7_MAXVAL    0x42
#define PIC_TIMER_MAXVAL(i)  (PIC_TIMER0_MAXVAL + ((i) * 2))

#define PIC_TIMER0_COUNT     0x44
#define PIC_TIMER1_COUNT     0x46
#define PIC_TIMER2_COUNT     0x48
#define PIC_TIMER3_COUNT     0x4a
#define PIC_TIMER4_COUNT     0x4c
#define PIC_TIMER5_COUNT     0x4e
#define PIC_TIMER6_COUNT     0x50
#define PIC_TIMER7_COUNT     0x52
#define PIC_TIMER_COUNT(i)   (PIC_TIMER0_COUNT + ((i) * 2))

#define PIC_ITE0_N0_N1          0x54
#define PIC_ITE1_N0_N1          0x58
#define PIC_ITE2_N0_N1          0x5c
#define PIC_ITE3_N0_N1          0x60
#define PIC_ITE4_N0_N1          0x64
#define PIC_ITE5_N0_N1          0x68
#define PIC_ITE6_N0_N1          0x6c
#define PIC_ITE7_N0_N1          0x70
#define PIC_ITE_N0_N1(i)        (PIC_ITE0_N0_N1 + ((i) * 4))

#define PIC_ITE0_N2_N3          0x56
#define PIC_ITE1_N2_N3          0x5a
#define PIC_ITE2_N2_N3          0x5e
#define PIC_ITE3_N2_N3          0x62
#define PIC_ITE4_N2_N3          0x66
#define PIC_ITE5_N2_N3          0x6a
#define PIC_ITE6_N2_N3          0x6e
#define PIC_ITE7_N2_N3          0x72
#define PIC_ITE_N2_N3(i)        (PIC_ITE0_N2_N3 + ((i) * 4))

#define PIC_IRT0                0x74
#define PIC_IRT(i)              (PIC_IRT0 + ((i) * 2))

#define TIMER_CYCLES_MAXVAL	0xffffffffffffffffULL

/*
 *    IRT Map
 */
#define PIC_NUM_IRTS		160

#define PIC_IRT_WD_0_INDEX	0
#define PIC_IRT_WD_1_INDEX	1
#define PIC_IRT_WD_NMI_0_INDEX	2
#define PIC_IRT_WD_NMI_1_INDEX	3
#define PIC_IRT_TIMER_0_INDEX	4
#define PIC_IRT_TIMER_1_INDEX	5
#define PIC_IRT_TIMER_2_INDEX	6
#define PIC_IRT_TIMER_3_INDEX	7
#define PIC_IRT_TIMER_4_INDEX	8
#define PIC_IRT_TIMER_5_INDEX	9
#define PIC_IRT_TIMER_6_INDEX	10
#define PIC_IRT_TIMER_7_INDEX	11
#define PIC_IRT_CLOCK_INDEX	PIC_IRT_TIMER_7_INDEX
#define PIC_IRT_TIMER_INDEX(num)	((num) + PIC_IRT_TIMER_0_INDEX)


/* 11 and 12 */
#define PIC_NUM_MSG_Q_IRTS	32
#define PIC_IRT_MSG_Q0_INDEX	12
#define PIC_IRT_MSG_Q_INDEX(qid)	((qid) + PIC_IRT_MSG_Q0_INDEX)
/* 12 to 43 */
#define PIC_IRT_MSG_0_INDEX	44
#define PIC_IRT_MSG_1_INDEX	45
/* 44 and 45 */
#define PIC_NUM_PCIE_MSIX_IRTS	32
#define PIC_IRT_PCIE_MSIX_0_INDEX	46
#define PIC_IRT_PCIE_MSIX_INDEX(num)	((num) + PIC_IRT_PCIE_MSIX_0_INDEX)
/* 46 to 77 */
#define PIC_NUM_PCIE_LINK_IRTS		4
#define PIC_IRT_PCIE_LINK_0_INDEX	78
#define PIC_IRT_PCIE_LINK_1_INDEX	79
#define PIC_IRT_PCIE_LINK_2_INDEX	80
#define PIC_IRT_PCIE_LINK_3_INDEX	81
#define PIC_IRT_PCIE_LINK_INDEX(num)	((num) + PIC_IRT_PCIE_LINK_0_INDEX)
/* 78 to 81 */
#define PIC_NUM_NA_IRTS			32
/* 82 to 113 */
#define PIC_IRT_NA_0_INDEX		82
#define PIC_IRT_NA_INDEX(num)		((num) + PIC_IRT_NA_0_INDEX)
#define PIC_IRT_POE_INDEX		114

#define PIC_NUM_USB_IRTS		6
#define PIC_IRT_USB_0_INDEX		115
#define PIC_IRT_EHCI_0_INDEX		115
#define PIC_IRT_OHCI_0_INDEX		116
#define PIC_IRT_OHCI_1_INDEX		117
#define PIC_IRT_EHCI_1_INDEX		118
#define PIC_IRT_OHCI_2_INDEX		119
#define PIC_IRT_OHCI_3_INDEX		120
#define PIC_IRT_USB_INDEX(num)		((num) + PIC_IRT_USB_0_INDEX)
/* 115 to 120 */
#define PIC_IRT_GDX_INDEX		121
#define PIC_IRT_SEC_INDEX		122
#define PIC_IRT_RSA_INDEX		123

#define PIC_NUM_COMP_IRTS		4
#define PIC_IRT_COMP_0_INDEX		124
#define PIC_IRT_COMP_INDEX(num)		((num) + PIC_IRT_COMP_0_INDEX)
/* 124 to 127 */
#define PIC_IRT_GBU_INDEX		128
#define PIC_IRT_ICC_0_INDEX		129 /* ICC - Inter Chip Coherency */
#define PIC_IRT_ICC_1_INDEX		130
#define PIC_IRT_ICC_2_INDEX		131
#define PIC_IRT_CAM_INDEX		132
#define PIC_IRT_UART_0_INDEX		133
#define PIC_IRT_UART_1_INDEX		134
#define PIC_IRT_I2C_0_INDEX		135
#define PIC_IRT_I2C_1_INDEX		136
#define PIC_IRT_SYS_0_INDEX		137
#define PIC_IRT_SYS_1_INDEX		138
#define PIC_IRT_JTAG_INDEX		139
#define PIC_IRT_PIC_INDEX		140
#define PIC_IRT_NBU_INDEX		141
#define PIC_IRT_TCU_INDEX		142
#define PIC_IRT_GCU_INDEX		143 /* GBC - Global Coherency */
#define PIC_IRT_DMC_0_INDEX		144
#define PIC_IRT_DMC_1_INDEX		145

#define PIC_NUM_GPIO_IRTS		4
#define PIC_IRT_GPIO_0_INDEX		146
#define PIC_IRT_GPIO_INDEX(num)		((num) + PIC_IRT_GPIO_0_INDEX)

/* 146 to 149 */
#define PIC_IRT_NOR_INDEX		150
#define PIC_IRT_NAND_INDEX		151
#define PIC_IRT_SPI_INDEX		152
#define PIC_IRT_MMC_INDEX		153

#define PIC_CLOCK_TIMER			7
#define PIC_IRQ_BASE			8

#if !defined(LOCORE) && !defined(__ASSEMBLY__)

#define PIC_IRT_FIRST_IRQ		(PIC_IRQ_BASE)
#define PIC_IRT_LAST_IRQ		63
#define PIC_IRQ_IS_IRT(irq)		((irq) >= PIC_IRT_FIRST_IRQ)

/*
 *   Misc
 */
#define PIC_IRT_VALID			1
#define PIC_LOCAL_SCHEDULING		1
#define PIC_GLOBAL_SCHEDULING		0

#define nlm_read_pic_reg(b, r)	nlm_read_reg64(b, r)
#define nlm_write_pic_reg(b, r, v) nlm_write_reg64(b, r, v)
#define nlm_get_pic_pcibase(node) nlm_pcicfg_base(XLP_IO_PIC_OFFSET(node))
#define nlm_get_pic_regbase(node) (nlm_get_pic_pcibase(node) + XLP_IO_PCI_HDRSZ)

/* IRT and h/w interrupt routines */
static inline int
nlm_pic_read_irt(uint64_t base, int irt_index)
{
	return nlm_read_pic_reg(base, PIC_IRT(irt_index));
}

static inline void
nlm_set_irt_to_cpu(uint64_t base, int irt, int cpu)
{
	uint64_t val;

	val = nlm_read_pic_reg(base, PIC_IRT(irt));
	/* clear cpuset and mask */
	val &= ~((0x7ull << 16) | 0xffff);
	/* set DB, cpuset and cpumask */
	val |= (1 << 19) | ((cpu >> 4) << 16) | (1 << (cpu & 0xf));
	nlm_write_pic_reg(base, PIC_IRT(irt), val);
}

static inline void
nlm_pic_write_irt(uint64_t base, int irt_num, int en, int nmi,
	int sch, int vec, int dt, int db, int dte)
{
	uint64_t val;

	val = (((uint64_t)en & 0x1) << 31) | ((nmi & 0x1) << 29) |
			((sch & 0x1) << 28) | ((vec & 0x3f) << 20) |
			((dt & 0x1) << 19) | ((db & 0x7) << 16) |
			(dte & 0xffff);

	nlm_write_pic_reg(base, PIC_IRT(irt_num), val);
}

static inline void
nlm_pic_write_irt_direct(uint64_t base, int irt_num, int en, int nmi,
	int sch, int vec, int cpu)
{
	nlm_pic_write_irt(base, irt_num, en, nmi, sch, vec, 1,
		(cpu >> 4),		/* thread group */
		1 << (cpu & 0xf));	/* thread mask */
}

static inline uint64_t
nlm_pic_read_timer(uint64_t base, int timer)
{
	return nlm_read_pic_reg(base, PIC_TIMER_COUNT(timer));
}

static inline void
nlm_pic_write_timer(uint64_t base, int timer, uint64_t value)
{
	nlm_write_pic_reg(base, PIC_TIMER_COUNT(timer), value);
}

static inline void
nlm_pic_set_timer(uint64_t base, int timer, uint64_t value, int irq, int cpu)
{
	uint64_t pic_ctrl = nlm_read_pic_reg(base, PIC_CTRL);
	int en;

	en = (irq > 0);
	nlm_write_pic_reg(base, PIC_TIMER_MAXVAL(timer), value);
	nlm_pic_write_irt_direct(base, PIC_IRT_TIMER_INDEX(timer),
		en, 0, 0, irq, cpu);

	/* enable the timer */
	pic_ctrl |= (1 << (PIC_CTRL_STE + timer));
	nlm_write_pic_reg(base, PIC_CTRL, pic_ctrl);
}

static inline void
nlm_pic_enable_irt(uint64_t base, int irt)
{
	uint64_t reg;

	reg = nlm_read_pic_reg(base, PIC_IRT(irt));
	nlm_write_pic_reg(base, PIC_IRT(irt), reg | (1u << 31));
}

static inline void
nlm_pic_disable_irt(uint64_t base, int irt)
{
	uint64_t reg;

	reg = nlm_read_pic_reg(base, PIC_IRT(irt));
	nlm_write_pic_reg(base, PIC_IRT(irt), reg & ~((uint64_t)1 << 31));
}

static inline void
nlm_pic_send_ipi(uint64_t base, int hwt, int irq, int nmi)
{
	uint64_t ipi;

	ipi = (nmi << 31) | (irq << 20);
	ipi |= ((hwt >> 4) << 16) | (1 << (hwt & 0xf)); /* cpuset and mask */
	nlm_write_pic_reg(base, PIC_IPI_CTL, ipi);
}

static inline void
nlm_pic_ack(uint64_t base, int irt_num)
{
	nlm_write_pic_reg(base, PIC_INT_ACK, irt_num);

	/* Ack the Status register for Watchdog & System timers */
	if (irt_num < 12)
		nlm_write_pic_reg(base, PIC_STATUS, (1 << irt_num));
}

static inline void
nlm_pic_init_irt(uint64_t base, int irt, int irq, int hwt)
{
	nlm_pic_write_irt_direct(base, irt, 0, 0, 0, irq, hwt);
}

int nlm_irq_to_irt(int irq);

#endif /* __ASSEMBLY__ */
#endif /* _NLM_HAL_PIC_H */
