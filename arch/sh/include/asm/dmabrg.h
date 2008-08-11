/*
 * SH7760 DMABRG (USB/Audio) support
 */

#ifndef _DMABRG_H_
#define _DMABRG_H_

/* IRQ sources */
#define DMABRGIRQ_USBDMA	0
#define DMABRGIRQ_USBDMAERR	1
#define DMABRGIRQ_A0TXF		2
#define DMABRGIRQ_A0TXH		3
#define DMABRGIRQ_A0RXF		4
#define DMABRGIRQ_A0RXH		5
#define DMABRGIRQ_A1TXF		6
#define DMABRGIRQ_A1TXH		7
#define DMABRGIRQ_A1RXF		8
#define DMABRGIRQ_A1RXH		9

extern int dmabrg_request_irq(unsigned int, void(*)(void *), void *);
extern void dmabrg_free_irq(unsigned int);

#endif
