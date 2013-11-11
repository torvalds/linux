/* $Id: netjet.h,v 2.8.2.2 2004/01/12 22:52:28 keil Exp $
 *
 * NETjet common header file
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *              by Matt Henderson,
 *                 Traverse Technologies P/L www.traverse.com.au
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define byteout(addr, val) outb(val, addr)
#define bytein(addr) inb(addr)

#define NETJET_CTRL	0x00
#define NETJET_DMACTRL	0x01
#define NETJET_AUXCTRL	0x02
#define NETJET_AUXDATA	0x03
#define NETJET_IRQMASK0 0x04
#define NETJET_IRQMASK1 0x05
#define NETJET_IRQSTAT0 0x06
#define NETJET_IRQSTAT1 0x07
#define NETJET_DMA_READ_START	0x08
#define NETJET_DMA_READ_IRQ	0x0c
#define NETJET_DMA_READ_END	0x10
#define NETJET_DMA_READ_ADR	0x14
#define NETJET_DMA_WRITE_START	0x18
#define NETJET_DMA_WRITE_IRQ	0x1c
#define NETJET_DMA_WRITE_END	0x20
#define NETJET_DMA_WRITE_ADR	0x24
#define NETJET_PULSE_CNT	0x28

#define NETJET_ISAC_OFF	0xc0
#define NETJET_ISACIRQ	0x10
#define NETJET_IRQM0_READ	0x0c
#define NETJET_IRQM0_READ_1	0x04
#define NETJET_IRQM0_READ_2	0x08
#define NETJET_IRQM0_WRITE	0x03
#define NETJET_IRQM0_WRITE_1	0x01
#define NETJET_IRQM0_WRITE_2	0x02

#define NETJET_DMA_TXSIZE 512
#define NETJET_DMA_RXSIZE 128

#define HDLC_ZERO_SEARCH 0
#define HDLC_FLAG_SEARCH 1
#define HDLC_FLAG_FOUND  2
#define HDLC_FRAME_FOUND 3
#define HDLC_NULL 4
#define HDLC_PART 5
#define HDLC_FULL 6

#define HDLC_FLAG_VALUE	0x7e

u_char NETjet_ReadIC(struct IsdnCardState *cs, u_char offset);
void NETjet_WriteIC(struct IsdnCardState *cs, u_char offset, u_char value);
void NETjet_ReadICfifo(struct IsdnCardState *cs, u_char *data, int size);
void NETjet_WriteICfifo(struct IsdnCardState *cs, u_char *data, int size);

void read_tiger(struct IsdnCardState *cs);
void write_tiger(struct IsdnCardState *cs);

void netjet_fill_dma(struct BCState *bcs);
void netjet_interrupt(int intno, void *dev_id);
void inittiger(struct IsdnCardState *cs);
void release_io_netjet(struct IsdnCardState *cs);
