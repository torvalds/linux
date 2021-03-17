/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MVME147HW_H_
#define _MVME147HW_H_

#include <asm/irq.h>

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
} MK48T02;

#define RTC_WRITE	0x80
#define RTC_READ	0x40
#define RTC_STOP	0x20

#define m147_rtc ((MK48T02 * volatile)0xfffe07f8)


struct pcc_regs {
   volatile u_long	dma_tadr;
   volatile u_long	dma_dadr;
   volatile u_long	dma_bcr;
   volatile u_long	dma_hr;
   volatile u_short	t1_preload;
   volatile u_short	t1_count;
   volatile u_short	t2_preload;
   volatile u_short	t2_count;
   volatile u_char	t1_int_cntrl;
   volatile u_char	t1_cntrl;
   volatile u_char	t2_int_cntrl;
   volatile u_char	t2_cntrl;
   volatile u_char	ac_fail;
   volatile u_char	watchdog;
   volatile u_char	lpt_intr;
   volatile u_char	lpt_cntrl;
   volatile u_char	dma_intr;
   volatile u_char	dma_cntrl;
   volatile u_char	bus_error;
   volatile u_char	dma_status;
   volatile u_char	abort;
   volatile u_char	ta_fnctl;
   volatile u_char	serial_cntrl;
   volatile u_char	general_cntrl;
   volatile u_char	lan_cntrl;
   volatile u_char	general_status;
   volatile u_char	scsi_interrupt;
   volatile u_char	slave;
   volatile u_char	soft1_cntrl;
   volatile u_char	int_base;
   volatile u_char	soft2_cntrl;
   volatile u_char	revision_level;
   volatile u_char	lpt_data;
   volatile u_char	lpt_status;
   };

#define m147_pcc ((struct pcc_regs * volatile)0xfffe1000)


#define PCC_INT_ENAB		0x08

#define PCC_TIMER_INT_CLR	0x80
#define PCC_TIMER_CLR_OVF	0x04

#define PCC_LEVEL_ABORT		0x07
#define PCC_LEVEL_SERIAL	0x04
#define PCC_LEVEL_ETH		0x04
#define PCC_LEVEL_TIMER1	0x04
#define PCC_LEVEL_SCSI_PORT	0x04
#define PCC_LEVEL_SCSI_DMA	0x04

#define PCC_IRQ_AC_FAIL		(IRQ_USER+0)
#define PCC_IRQ_BERR		(IRQ_USER+1)
#define PCC_IRQ_ABORT		(IRQ_USER+2)
/* #define PCC_IRQ_SERIAL	(IRQ_USER+3) */
#define PCC_IRQ_PRINTER		(IRQ_USER+7)
#define PCC_IRQ_TIMER1		(IRQ_USER+8)
#define PCC_IRQ_TIMER2		(IRQ_USER+9)
#define PCC_IRQ_SOFTWARE1	(IRQ_USER+10)
#define PCC_IRQ_SOFTWARE2	(IRQ_USER+11)


#define M147_SCC_A_ADDR		0xfffe3002
#define M147_SCC_B_ADDR		0xfffe3000
#define M147_SCC_PCLK		5000000

#define MVME147_IRQ_SCSI_PORT	(IRQ_USER+0x45)
#define MVME147_IRQ_SCSI_DMA	(IRQ_USER+0x46)

/* SCC interrupts, for MVME147 */

#define MVME147_IRQ_TYPE_PRIO	0
#define MVME147_IRQ_SCC_BASE		(IRQ_USER+32)
#define MVME147_IRQ_SCCB_TX		(IRQ_USER+32)
#define MVME147_IRQ_SCCB_STAT		(IRQ_USER+34)
#define MVME147_IRQ_SCCB_RX		(IRQ_USER+36)
#define MVME147_IRQ_SCCB_SPCOND		(IRQ_USER+38)
#define MVME147_IRQ_SCCA_TX		(IRQ_USER+40)
#define MVME147_IRQ_SCCA_STAT		(IRQ_USER+42)
#define MVME147_IRQ_SCCA_RX		(IRQ_USER+44)
#define MVME147_IRQ_SCCA_SPCOND		(IRQ_USER+46)

#define MVME147_LANCE_BASE	0xfffe1800
#define MVME147_LANCE_IRQ	(IRQ_USER+4)

#define ETHERNET_ADDRESS 0xfffe0778

#endif
