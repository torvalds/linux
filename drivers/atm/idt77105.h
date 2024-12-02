/* SPDX-License-Identifier: GPL-2.0 */
/* drivers/atm/idt77105.h - IDT77105 (PHY) declarations */
 
/* Written 1999 by Greg Banks, NEC Australia <gnb@linuxfan.com>. Based on suni.h */
 

#ifndef DRIVER_ATM_IDT77105_H
#define DRIVER_ATM_IDT77105_H

#include <linux/atmdev.h>
#include <linux/atmioc.h>


/* IDT77105 registers */

#define IDT77105_MCR		0x0	/* Master Control Register */
#define IDT77105_ISTAT	        0x1	/* Interrupt Status */
#define IDT77105_DIAG   	0x2	/* Diagnostic Control */
#define IDT77105_LEDHEC		0x3	/* LED Driver & HEC Status/Control */
#define IDT77105_CTRLO		0x4	/* Low Byte Counter Register */
#define IDT77105_CTRHI		0x5	/* High Byte Counter Register */
#define IDT77105_CTRSEL		0x6	/* Counter Register Read Select */

/* IDT77105 register values */

/* MCR */
#define IDT77105_MCR_UPLO	0x80	/* R/W, User Prog'le Output Latch */
#define IDT77105_MCR_DREC	0x40	/* R/W, Discard Receive Error Cells */
#define IDT77105_MCR_ECEIO	0x20	/* R/W, Enable Cell Error Interrupts
                                         * Only */
#define IDT77105_MCR_TDPC	0x10	/* R/W, Transmit Data Parity Check */
#define IDT77105_MCR_DRIC	0x08	/* R/W, Discard Received Idle Cells */
#define IDT77105_MCR_HALTTX	0x04	/* R/W, Halt Tx */
#define IDT77105_MCR_UMODE	0x02	/* R/W, Utopia (cell/byte) Mode */
#define IDT77105_MCR_EIP	0x01	/* R/W, Enable Interrupt Pin */

/* ISTAT */
#define IDT77105_ISTAT_GOODSIG	0x40	/* R, Good Signal Bit */
#define IDT77105_ISTAT_HECERR	0x20	/* sticky, HEC Error*/
#define IDT77105_ISTAT_SCR	0x10	/* sticky, Short Cell Received */
#define IDT77105_ISTAT_TPE	0x08	/* sticky, Transmit Parity Error */
#define IDT77105_ISTAT_RSCC	0x04	/* sticky, Rx Signal Condition Change */
#define IDT77105_ISTAT_RSE	0x02	/* sticky, Rx Symbol Error */
#define IDT77105_ISTAT_RFO	0x01	/* sticky, Rx FIFO Overrun */

/* DIAG */
#define IDT77105_DIAG_FTD	0x80	/* R/W, Force TxClav deassert */
#define IDT77105_DIAG_ROS	0x40	/* R/W, RxClav operation select */
#define IDT77105_DIAG_MPCS	0x20	/* R/W, Multi-PHY config'n select */
#define IDT77105_DIAG_RFLUSH	0x10	/* R/W, clear receive FIFO */
#define IDT77105_DIAG_ITPE	0x08	/* R/W, Insert Tx payload error */
#define IDT77105_DIAG_ITHE	0x04	/* R/W, Insert Tx HEC error */
#define IDT77105_DIAG_UMODE	0x02	/* R/W, Utopia (cell/byte) Mode */
#define IDT77105_DIAG_LCMASK	0x03	/* R/W, Loopback Control */

#define IDT77105_DIAG_LC_NORMAL         0x00	/* Receive from network */
#define IDT77105_DIAG_LC_PHY_LOOPBACK	0x02
#define IDT77105_DIAG_LC_LINE_LOOPBACK	0x03

/* LEDHEC */
#define IDT77105_LEDHEC_DRHC	0x40	/* R/W, Disable Rx HEC check */
#define IDT77105_LEDHEC_DTHC	0x20	/* R/W, Disable Tx HEC calculation */
#define IDT77105_LEDHEC_RPWMASK	0x18	/* R/W, RxRef pulse width select */
#define IDT77105_LEDHEC_TFS	0x04	/* R, Tx FIFO Status (1=empty) */
#define IDT77105_LEDHEC_TLS	0x02	/* R, Tx LED Status (1=lit) */
#define IDT77105_LEDHEC_RLS	0x01	/* R, Rx LED Status (1=lit) */

#define IDT77105_LEDHEC_RPW_1	0x00	/* RxRef active for 1 RxClk cycle */
#define IDT77105_LEDHEC_RPW_2	0x08	/* RxRef active for 2 RxClk cycle */
#define IDT77105_LEDHEC_RPW_4	0x10	/* RxRef active for 4 RxClk cycle */
#define IDT77105_LEDHEC_RPW_8	0x18	/* RxRef active for 8 RxClk cycle */

/* CTRSEL */
#define IDT77105_CTRSEL_SEC	0x08	/* W, Symbol Error Counter */
#define IDT77105_CTRSEL_TCC	0x04	/* W, Tx Cell Counter */
#define IDT77105_CTRSEL_RCC	0x02	/* W, Rx Cell Counter */
#define IDT77105_CTRSEL_RHEC	0x01	/* W, Rx HEC Error Counter */

#ifdef __KERNEL__
int idt77105_init(struct atm_dev *dev);
#endif

/*
 * Tunable parameters
 */
 
/* Time between samples of the hardware cell counters. Should be <= 1 sec */
#define IDT77105_STATS_TIMER_PERIOD     (HZ) 
/* Time between checks to see if the signal has been found again */
#define IDT77105_RESTART_TIMER_PERIOD   (5 * HZ)

#endif
