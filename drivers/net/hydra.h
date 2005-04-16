/*	$Linux: hydra.h,v 1.0 1994/10/26 02:03:47 cgd Exp $	*/

/*
 * Copyright (c) 1994 Timo Rossi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by  Timo Rossi
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The Hydra Systems card uses the National Semiconductor
 * 8390 NIC (Network Interface Controller) chip, located
 * at card base address + 0xffe1. NIC registers are accessible
 * only at odd byte addresses, so the register offsets must
 * be multiplied by two.
 *
 * Card address PROM is located at card base + 0xffc0 (even byte addresses)
 *
 * RAM starts at the card base address, and is 16K or 64K.
 * The current Amiga NetBSD hydra driver is hardwired for 16K.
 * It seems that the RAM should be accessed as words or longwords only.
 *
 */

/* adapted for Linux by Topi Kanerva 03/29/95
   with original author's permission          */

#define HYDRA_NIC_BASE 0xffe1

/* Page0 registers */

#define NIC_CR     0       /* Command register   */
#define NIC_PSTART (1*2)   /* Page start (write) */
#define NIC_PSTOP  (2*2)   /* Page stop (write)  */
#define NIC_BNDRY  (3*2)   /* Boundary pointer   */
#define NIC_TSR    (4*2)   /* Transmit status (read) */
#define NIC_TPSR   (4*2)   /* Transmit page start (write) */
#define NIC_NCR    (5*2)   /* Number of collisions, read  */
#define NIC_TBCR0  (5*2)   /* Transmit byte count low (write)  */
#define NIC_FIFO   (6*2)   /* FIFO reg. (read)   */
#define NIC_TBCR1  (6*2)   /* Transmit byte count high (write) */
#define NIC_ISR    (7*2)   /* Interrupt status register */
#define NIC_RBCR0  (0xa*2) /* Remote byte count low (write)  */
#define NIC_RBCR1  (0xb*2) /* Remote byte count high (write) */
#define NIC_RSR    (0xc*2) /* Receive status (read)  */
#define NIC_RCR    (0xc*2) /* Receive config (write) */
#define NIC_CNTR0  (0xd*2) /* Frame alignment error count (read) */
#define NIC_TCR    (0xd*2) /* Transmit config (write)  */
#define NIC_CNTR1  (0xe*2) /* CRC error counter (read) */
#define NIC_DCR    (0xe*2) /* Data config (write) */
#define NIC_CNTR2  (0xf*2) /* missed packet counter (read) */
#define NIC_IMR    (0xf*2) /* Interrupt mask reg. (write)  */

/* Page1 registers */

#define NIC_PAR0   (1*2)   /* Physical address */
#define NIC_PAR1   (2*2)
#define NIC_PAR2   (3*2)
#define NIC_PAR3   (4*2)
#define NIC_PAR4   (5*2)
#define NIC_PAR5   (6*2)
#define NIC_CURR   (7*2)   /* Current RX ring-buffer page */
#define NIC_MAR0   (8*2)   /* Multicast address */
#define NIC_MAR1   (9*2)
#define NIC_MAR2   (0xa*2)
#define NIC_MAR3   (0xb*2)
#define NIC_MAR4   (0xc*2)
#define NIC_MAR5   (0xd*2)
#define NIC_MAR6   (0xe*2)
#define NIC_MAR7   (0xf*2)

/* Command register definitions */

#define CR_STOP   0x01 /* Stop -- software reset command */
#define CR_START  0x02 /* Start */
#define CR_TXP   0x04 /* Transmit packet */

#define CR_RD0    0x08 /* Remote DMA cmd */
#define CR_RD1    0x10
#define CR_RD2    0x20

#define CR_NODMA  CR_RD2

#define CR_PS0    0x40 /* Page select */
#define CR_PS1    0x80

#define CR_PAGE0  0
#define CR_PAGE1  CR_PS0
#define CR_PAGE2  CR_PS1

/* Interrupt status reg. definitions */

#define ISR_PRX   0x01 /* Packet received without errors */
#define ISR_PTX   0x02 /* Packet transmitted without errors */
#define ISR_RXE   0x04 /* Receive error  */
#define ISR_TXE   0x08 /* Transmit error */
#define ISR_OVW   0x10 /* Ring buffer overrun */
#define ISR_CNT   0x20 /* Counter overflow    */
#define ISR_RDC   0x40 /* Remote DMA compile */
#define ISR_RST   0x80 /* Reset status      */

/* Data config reg. definitions */

#define DCR_WTS   0x01 /* Word transfer select  */
#define DCR_BOS   0x02 /* Byte order select     */
#define DCR_LAS   0x04 /* Long address select   */
#define DCR_LS    0x08 /* Loopback select       */
#define DCR_AR    0x10 /* Auto-init remote      */
#define DCR_FT0   0x20 /* FIFO threshold select */
#define DCR_FT1   0x40

/* Transmit config reg. definitions */

#define TCR_CRC  0x01 /* Inhibit CRC */
#define TCR_LB0  0x02 /* Loopback control */
#define TCR_LB1  0x04
#define TCR_ATD  0x08 /* Auto transmit disable */
#define TCR_OFST 0x10 /* Collision offset enable */

/* Transmit status reg. definitions */

#define TSR_PTX  0x01 /* Packet transmitted */
#define TSR_COL  0x04 /* Transmit collided */
#define TSR_ABT  0x08 /* Transmit aborted */
#define TSR_CRS  0x10 /* Carrier sense lost */
#define TSR_FU   0x20 /* FIFO underrun */
#define TSR_CDH  0x40 /* CD Heartbeat */
#define TSR_OWC  0x80 /* Out of Window Collision */

/* Receiver config register definitions */

#define RCR_SEP  0x01 /* Save errored packets */
#define RCR_AR   0x02 /* Accept runt packets */
#define RCR_AB   0x04 /* Accept broadcast */
#define RCR_AM   0x08 /* Accept multicast */
#define RCR_PRO  0x10 /* Promiscuous mode */
#define RCR_MON  0x20 /* Monitor mode */

/* Receiver status register definitions */

#define RSR_PRX  0x01 /* Packet received without error */
#define RSR_CRC  0x02 /* CRC error */
#define RSR_FAE  0x04 /* Frame alignment error */
#define RSR_FO   0x08 /* FIFO overrun */
#define RSR_MPA  0x10 /* Missed packet */
#define RSR_PHY  0x20 /* Physical address */
#define RSR_DIS  0x40 /* Received disabled */
#define RSR_DFR  0x80 /* Deferring (jabber) */

/* Hydra System card address PROM offset */

#define HYDRA_ADDRPROM 0xffc0


