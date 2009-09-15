/*
 *
 * ipac.h	Defines for the Infineon (former Siemens) ISDN
 *		chip series
 *
 * Author       Karsten Keil <keil@isdn4linux.de>
 *
 * Copyright 2009  by Karsten Keil <keil@isdn4linux.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "iohelper.h"

struct isac_hw {
	struct dchannel		dch;
	u32			type;
	u32			off;		/* offset to isac regs */
	char			*name;
	spinlock_t		*hwlock;	/* lock HW acccess */
	read_reg_func		*read_reg;
	write_reg_func		*write_reg;
	fifo_func		*read_fifo;
	fifo_func		*write_fifo;
	int			(*monitor)(void *, u32, u8 *, int);
	void			(*release)(struct isac_hw *);
	int			(*init)(struct isac_hw *);
	int			(*ctrl)(struct isac_hw *, u32, u_long);
	int			(*open)(struct isac_hw *, struct channel_req *);
	u8			*mon_tx;
	u8			*mon_rx;
	int			mon_txp;
	int			mon_txc;
	int			mon_rxp;
	struct arcofi_msg	*arcofi_list;
	struct timer_list	arcofitimer;
	wait_queue_head_t	arcofi_wait;
	u8			arcofi_bc;
	u8			arcofi_state;
	u8			mocr;
	u8			adf2;
	u8			state;
};

struct ipac_hw;

struct hscx_hw {
	struct bchannel		bch;
	struct ipac_hw		*ip;
	u8			fifo_size;
	u8			off;	/* offset to ICA or ICB */
	u8			slot;
	char			log[64];
};

struct ipac_hw {
	struct isac_hw		isac;
	struct hscx_hw		hscx[2];
	char			*name;
	void			*hw;
	spinlock_t		*hwlock;	/* lock HW acccess */
	struct module		*owner;
	u32			type;
	read_reg_func		*read_reg;
	write_reg_func		*write_reg;
	fifo_func		*read_fifo;
	fifo_func		*write_fifo;
	void			(*release)(struct ipac_hw *);
	int			(*init)(struct ipac_hw *);
	int			(*ctrl)(struct ipac_hw *, u32, u_long);
	u8			conf;
};

#define IPAC_TYPE_ISAC		0x0010
#define IPAC_TYPE_IPAC		0x0020
#define IPAC_TYPE_ISACX		0x0040
#define IPAC_TYPE_IPACX		0x0080
#define IPAC_TYPE_HSCX		0x0100

#define ISAC_USE_ARCOFI		0x1000

/* Monitor functions */
#define MONITOR_RX_0		0x1000
#define MONITOR_RX_1		0x1001
#define MONITOR_TX_0		0x2000
#define MONITOR_TX_1		0x2001

/* All registers original Siemens Spec  */
/* IPAC/ISAC registers */
#define ISAC_MASK		0x20
#define ISAC_ISTA		0x20
#define ISAC_STAR		0x21
#define ISAC_CMDR		0x21
#define ISAC_EXIR		0x24
#define ISAC_ADF2		0x39
#define ISAC_SPCR		0x30
#define ISAC_ADF1		0x38
#define ISAC_CIR0		0x31
#define ISAC_CIX0		0x31
#define ISAC_CIR1		0x33
#define ISAC_CIX1		0x33
#define ISAC_STCR		0x37
#define ISAC_MODE		0x22
#define ISAC_RSTA		0x27
#define ISAC_RBCL		0x25
#define ISAC_RBCH		0x2A
#define ISAC_TIMR		0x23
#define ISAC_SQXR		0x3b
#define ISAC_SQRR		0x3b
#define ISAC_MOSR		0x3a
#define ISAC_MOCR		0x3a
#define ISAC_MOR0		0x32
#define ISAC_MOX0		0x32
#define ISAC_MOR1		0x34
#define ISAC_MOX1		0x34

#define ISAC_RBCH_XAC		0x80

#define IPAC_D_TIN2		0x01

/* IPAC/HSCX */
#define IPAC_ISTAB		0x20	/* RD	*/
#define IPAC_MASKB		0x20	/* WR	*/
#define IPAC_STARB		0x21	/* RD	*/
#define IPAC_CMDRB		0x21	/* WR	*/
#define IPAC_MODEB		0x22	/* R/W	*/
#define IPAC_EXIRB		0x24	/* RD	*/
#define IPAC_RBCLB		0x25	/* RD	*/
#define IPAC_RAH1		0x26	/* WR	*/
#define IPAC_RAH2		0x27	/* WR	*/
#define IPAC_RSTAB		0x27	/* RD	*/
#define IPAC_RAL1		0x28	/* R/W	*/
#define IPAC_RAL2		0x29	/* WR	*/
#define IPAC_RHCRB		0x29	/* RD	*/
#define IPAC_XBCL		0x2A	/* WR	*/
#define IPAC_CCR2		0x2C	/* R/W	*/
#define IPAC_RBCHB		0x2D	/* RD	*/
#define IPAC_XBCH		0x2D	/* WR	*/
#define HSCX_VSTR		0x2E	/* RD	*/
#define IPAC_RLCR		0x2E	/* WR	*/
#define IPAC_CCR1		0x2F	/* R/W	*/
#define IPAC_TSAX		0x30	/* WR	*/
#define IPAC_TSAR		0x31	/* WR	*/
#define IPAC_XCCR		0x32	/* WR	*/
#define IPAC_RCCR		0x33	/* WR	*/

/* IPAC_ISTAB/IPAC_MASKB bits */
#define IPAC_B_XPR		0x10
#define IPAC_B_RPF		0x40
#define IPAC_B_RME		0x80
#define IPAC_B_ON		0x2F

/* IPAC_EXIRB bits */
#define IPAC_B_RFS		0x04
#define IPAC_B_RFO		0x10
#define IPAC_B_XDU		0x40
#define IPAC_B_XMR		0x80

/* IPAC special registers */
#define IPAC_CONF		0xC0	/* R/W	*/
#define IPAC_ISTA		0xC1	/* RD	*/
#define IPAC_MASK		0xC1	/* WR	*/
#define IPAC_ID			0xC2	/* RD	*/
#define IPAC_ACFG		0xC3	/* R/W	*/
#define IPAC_AOE		0xC4	/* R/W	*/
#define IPAC_ARX		0xC5	/* RD	*/
#define IPAC_ATX		0xC5	/* WR	*/
#define IPAC_PITA1		0xC6	/* R/W	*/
#define IPAC_PITA2		0xC7	/* R/W	*/
#define IPAC_POTA1		0xC8	/* R/W	*/
#define IPAC_POTA2		0xC9	/* R/W	*/
#define IPAC_PCFG		0xCA	/* R/W	*/
#define IPAC_SCFG		0xCB	/* R/W	*/
#define IPAC_TIMR2		0xCC	/* R/W	*/

/* IPAC_ISTA/_MASK bits */
#define IPAC__EXB		0x01
#define IPAC__ICB		0x02
#define IPAC__EXA		0x04
#define IPAC__ICA		0x08
#define IPAC__EXD		0x10
#define IPAC__ICD		0x20
#define IPAC__INT0		0x40
#define IPAC__INT1		0x80
#define IPAC__ON		0xC0

/* HSCX ISTA/MASK bits */
#define HSCX__EXB		0x01
#define HSCX__EXA		0x02
#define HSCX__ICA		0x04

/* ISAC/ISACX/IPAC/IPACX L1 commands */
#define ISAC_CMD_TIM		0x0
#define ISAC_CMD_RS		0x1
#define ISAC_CMD_SCZ		0x4
#define ISAC_CMD_SSZ		0x2
#define ISAC_CMD_AR8		0x8
#define ISAC_CMD_AR10		0x9
#define ISAC_CMD_ARL		0xA
#define ISAC_CMD_DUI		0xF

/* ISAC/ISACX/IPAC/IPACX L1 indications */
#define ISAC_IND_RS		0x1
#define ISAC_IND_PU		0x7
#define ISAC_IND_DR		0x0
#define ISAC_IND_SD		0x2
#define ISAC_IND_DIS		0x3
#define ISAC_IND_EI		0x6
#define ISAC_IND_RSY		0x4
#define ISAC_IND_ARD		0x8
#define ISAC_IND_TI		0xA
#define ISAC_IND_ATI		0xB
#define ISAC_IND_AI8		0xC
#define ISAC_IND_AI10		0xD
#define ISAC_IND_DID		0xF

/* the new ISACX / IPACX */
/* D-channel registers   */
#define ISACX_RFIFOD		0x00	/* RD	*/
#define ISACX_XFIFOD		0x00	/* WR	*/
#define ISACX_ISTAD		0x20	/* RD	*/
#define ISACX_MASKD		0x20	/* WR	*/
#define ISACX_STARD		0x21	/* RD	*/
#define ISACX_CMDRD		0x21	/* WR	*/
#define ISACX_MODED		0x22	/* R/W	*/
#define ISACX_EXMD1		0x23	/* R/W	*/
#define ISACX_TIMR1		0x24	/* R/W	*/
#define ISACX_SAP1		0x25	/* WR	*/
#define ISACX_SAP2		0x26	/* WR	*/
#define ISACX_RBCLD		0x26	/* RD	*/
#define ISACX_RBCHD		0x27	/* RD	*/
#define ISACX_TEI1		0x27	/* WR	*/
#define ISACX_TEI2		0x28	/* WR	*/
#define ISACX_RSTAD		0x28	/* RD	*/
#define ISACX_TMD		0x29	/* R/W	*/
#define ISACX_CIR0		0x2E	/* RD	*/
#define ISACX_CIX0		0x2E	/* WR	*/
#define ISACX_CIR1		0x2F	/* RD	*/
#define ISACX_CIX1		0x2F	/* WR	*/

/* Transceiver registers  */
#define ISACX_TR_CONF0		0x30	/* R/W	*/
#define ISACX_TR_CONF1		0x31	/* R/W	*/
#define ISACX_TR_CONF2		0x32	/* R/W	*/
#define ISACX_TR_STA		0x33	/* RD	*/
#define ISACX_TR_CMD		0x34	/* R/W	*/
#define ISACX_SQRR1		0x35	/* RD	*/
#define ISACX_SQXR1		0x35	/* WR	*/
#define ISACX_SQRR2		0x36	/* RD	*/
#define ISACX_SQXR2		0x36	/* WR	*/
#define ISACX_SQRR3		0x37	/* RD	*/
#define ISACX_SQXR3		0x37	/* WR	*/
#define ISACX_ISTATR		0x38	/* RD	*/
#define ISACX_MASKTR		0x39	/* R/W	*/
#define ISACX_TR_MODE		0x3A	/* R/W	*/
#define ISACX_ACFG1		0x3C	/* R/W	*/
#define ISACX_ACFG2		0x3D	/* R/W	*/
#define ISACX_AOE		0x3E	/* R/W	*/
#define ISACX_ARX		0x3F	/* RD	*/
#define ISACX_ATX		0x3F	/* WR	*/

/* IOM: Timeslot, DPS, CDA  */
#define ISACX_CDA10		0x40	/* R/W	*/
#define ISACX_CDA11		0x41	/* R/W	*/
#define ISACX_CDA20		0x42	/* R/W	*/
#define ISACX_CDA21		0x43	/* R/W	*/
#define ISACX_CDA_TSDP10	0x44	/* R/W	*/
#define ISACX_CDA_TSDP11	0x45	/* R/W	*/
#define ISACX_CDA_TSDP20	0x46	/* R/W	*/
#define ISACX_CDA_TSDP21	0x47	/* R/W	*/
#define ISACX_BCHA_TSDP_BC1	0x48	/* R/W	*/
#define ISACX_BCHA_TSDP_BC2	0x49	/* R/W	*/
#define ISACX_BCHB_TSDP_BC1	0x4A	/* R/W	*/
#define ISACX_BCHB_TSDP_BC2	0x4B	/* R/W	*/
#define ISACX_TR_TSDP_BC1	0x4C	/* R/W	*/
#define ISACX_TR_TSDP_BC2	0x4D	/* R/W	*/
#define ISACX_CDA1_CR		0x4E	/* R/W	*/
#define ISACX_CDA2_CR		0x4F	/* R/W	*/

/* IOM: Contol, Sync transfer, Monitor    */
#define ISACX_TR_CR		0x50	/* R/W	*/
#define ISACX_TRC_CR		0x50	/* R/W	*/
#define ISACX_BCHA_CR		0x51	/* R/W	*/
#define ISACX_BCHB_CR		0x52	/* R/W	*/
#define ISACX_DCI_CR		0x53	/* R/W	*/
#define ISACX_DCIC_CR		0x53	/* R/W	*/
#define ISACX_MON_CR		0x54	/* R/W	*/
#define ISACX_SDS1_CR		0x55	/* R/W	*/
#define ISACX_SDS2_CR		0x56	/* R/W	*/
#define ISACX_IOM_CR		0x57	/* R/W	*/
#define ISACX_STI		0x58	/* RD	*/
#define ISACX_ASTI		0x58	/* WR	*/
#define ISACX_MSTI		0x59	/* R/W	*/
#define ISACX_SDS_CONF		0x5A	/* R/W	*/
#define ISACX_MCDA		0x5B	/* RD	*/
#define ISACX_MOR		0x5C	/* RD	*/
#define ISACX_MOX		0x5C	/* WR	*/
#define ISACX_MOSR		0x5D	/* RD	*/
#define ISACX_MOCR		0x5E	/* R/W	*/
#define ISACX_MSTA		0x5F	/* RD	*/
#define ISACX_MCONF		0x5F	/* WR	*/

/* Interrupt and general registers */
#define ISACX_ISTA		0x60	/* RD	*/
#define ISACX_MASK		0x60	/* WR	*/
#define ISACX_AUXI		0x61	/* RD	*/
#define ISACX_AUXM		0x61	/* WR	*/
#define ISACX_MODE1		0x62	/* R/W	*/
#define ISACX_MODE2		0x63	/* R/W	*/
#define ISACX_ID		0x64	/* RD	*/
#define ISACX_SRES		0x64	/* WR	*/
#define ISACX_TIMR2		0x65	/* R/W	*/

/* Register Bits */
/* ISACX/IPACX _ISTAD (R) and _MASKD (W) */
#define ISACX_D_XDU		0x04
#define ISACX_D_XMR		0x08
#define ISACX_D_XPR		0x10
#define ISACX_D_RFO		0x20
#define ISACX_D_RPF		0x40
#define ISACX_D_RME		0x80

/* ISACX/IPACX _ISTA (R) and _MASK (W) */
#define ISACX__ICD		0x01
#define ISACX__MOS		0x02
#define ISACX__TRAN		0x04
#define ISACX__AUX		0x08
#define ISACX__CIC		0x10
#define ISACX__ST		0x20
#define IPACX__ICB		0x40
#define IPACX__ICA		0x80
#define IPACX__ON		0x2C

/* ISACX/IPACX _CMDRD (W) */
#define ISACX_CMDRD_XRES	0x01
#define ISACX_CMDRD_XME		0x02
#define ISACX_CMDRD_XTF		0x08
#define ISACX_CMDRD_STI		0x10
#define ISACX_CMDRD_RRES	0x40
#define ISACX_CMDRD_RMC		0x80

/* ISACX/IPACX _RSTAD (R) */
#define ISACX_RSTAD_TA		0x01
#define ISACX_RSTAD_CR		0x02
#define ISACX_RSTAD_SA0		0x04
#define ISACX_RSTAD_SA1		0x08
#define ISACX_RSTAD_RAB		0x10
#define ISACX_RSTAD_CRC		0x20
#define ISACX_RSTAD_RDO		0x40
#define ISACX_RSTAD_VFR		0x80

/* ISACX/IPACX _CIR0 (R) */
#define ISACX_CIR0_BAS		0x01
#define ISACX_CIR0_SG		0x08
#define ISACX_CIR0_CIC1		0x08
#define ISACX_CIR0_CIC0		0x08

/* B-channel registers */
#define IPACX_OFF_ICA		0x70
#define IPACX_OFF_ICB		0x80

/* ICA: IPACX_OFF_ICA + Reg ICB: IPACX_OFF_ICB + Reg */

#define IPACX_ISTAB		0x00    /* RD	*/
#define IPACX_MASKB		0x00	/* WR	*/
#define IPACX_STARB		0x01	/* RD	*/
#define IPACX_CMDRB		0x01	/* WR	*/
#define IPACX_MODEB		0x02	/* R/W	*/
#define IPACX_EXMB		0x03	/* R/W	*/
#define IPACX_RAH1		0x05	/* WR	*/
#define IPACX_RAH2		0x06	/* WR	*/
#define IPACX_RBCLB		0x06	/* RD	*/
#define IPACX_RBCHB		0x07	/* RD	*/
#define IPACX_RAL1		0x07	/* WR	*/
#define IPACX_RAL2		0x08	/* WR	*/
#define IPACX_RSTAB		0x08	/* RD	*/
#define IPACX_TMB		0x09	/* R/W	*/
#define IPACX_RFIFOB		0x0A	/* RD	*/
#define IPACX_XFIFOB		0x0A	/* WR	*/

/* IPACX_ISTAB / IPACX_MASKB bits */
#define IPACX_B_XDU		0x04
#define IPACX_B_XPR		0x10
#define IPACX_B_RFO		0x20
#define IPACX_B_RPF		0x40
#define IPACX_B_RME		0x80

#define IPACX_B_ON		0x0B

extern int mISDNisac_init(struct isac_hw *, void *);
extern irqreturn_t mISDNisac_irq(struct isac_hw *, u8);
extern u32 mISDNipac_init(struct ipac_hw *, void *);
extern irqreturn_t mISDNipac_irq(struct ipac_hw *, int);
