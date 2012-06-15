/* $Id: hfc_2bs0.h,v 1.5.2.2 2004/01/12 22:52:26 keil Exp $
 *
 * specific defines for CCD's HFC 2BS0
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define HFC_CTMT	0xe0
#define HFC_CIRM	0xc0
#define HFC_CIP		0x80
#define HFC_Z1		0x00
#define HFC_Z2		0x08
#define HFC_Z_LOW	0x00
#define HFC_Z_HIGH	0x04
#define HFC_F1_INC	0x28
#define HFC_FIFO_IN	0x2c
#define HFC_F1		0x30
#define HFC_F2		0x34
#define HFC_F2_INC	0x38
#define HFC_FIFO_OUT	0x3c
#define HFC_B1          0x00
#define HFC_B2		0x02
#define HFC_REC		0x01
#define HFC_SEND	0x00
#define HFC_CHANNEL(ch) (ch ? HFC_B2 : HFC_B1)

#define HFC_STATUS	0
#define HFC_DATA	1
#define HFC_DATA_NODEB	2

/* Status (READ) */
#define HFC_BUSY	0x01
#define HFC_TIMINT	0x02
#define HFC_EXTINT	0x04

/* CTMT (Write) */
#define HFC_CLTIMER 0x10
#define HFC_TIM50MS 0x08
#define HFC_TIMIRQE 0x04
#define HFC_TRANSB2 0x02
#define HFC_TRANSB1 0x01

/* CIRM (Write) */
#define HFC_RESET	0x08
#define HFC_MEM8K	0x10
#define HFC_INTA	0x01
#define HFC_INTB	0x02
#define HFC_INTC	0x03
#define HFC_INTD	0x04
#define HFC_INTE	0x05
#define HFC_INTF	0x06

extern void main_irq_hfc(struct BCState *bcs);
extern void inithfc(struct IsdnCardState *cs);
extern void releasehfc(struct IsdnCardState *cs);
