/*
 * Copyright (C) 2010 - Maxim Levitsky
 * driver for Ricoh memstick readers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef R592_H

#include <linux/memstick.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/ctype.h>

/* write to this reg (number,len) triggers TPC execution */
#define R592_TPC_EXEC			0x00
#define R592_TPC_EXEC_LEN_SHIFT		16		/* Bits 16..25 are TPC len */
#define R592_TPC_EXEC_BIG_FIFO		(1 << 26)	/* If bit 26 is set, large fifo is used (reg 48) */
#define R592_TPC_EXEC_TPC_SHIFT		28		/* Bits 28..31 are the TPC number */


/* Window for small TPC fifo (big endian)*/
/* reads and writes always are done in  8 byte chunks */
/* Not used in driver, because large fifo does better job */
#define R592_SFIFO			0x08


/* Status register (ms int, small fifo, IO)*/
#define R592_STATUS			0x10
							/* Parallel INT bits */
#define R592_STATUS_P_CMDNACK		(1 << 16)	/* INT reg: NACK (parallel mode) */
#define R592_STATUS_P_BREQ		(1 << 17)	/* INT reg: card ready (parallel mode)*/
#define R592_STATUS_P_INTERR		(1 << 18)	/* INT reg: int error (parallel mode)*/
#define R592_STATUS_P_CED		(1 << 19)	/* INT reg: command done (parallel mode) */

							/* Fifo status */
#define R592_STATUS_SFIFO_FULL		(1 << 20)	/* Small Fifo almost full (last chunk is written) */
#define R592_STATUS_SFIFO_EMPTY		(1 << 21)	/* Small Fifo empty */

							/* Error detection via CRC */
#define R592_STATUS_SEND_ERR		(1 << 24)	/* Send failed */
#define R592_STATUS_RECV_ERR		(1 << 25)	/* Receive failed */

							/* Card state */
#define R592_STATUS_RDY			(1 << 28)	/* RDY signal received */
#define R592_STATUS_CED			(1 << 29)	/* INT: Command done (serial mode)*/
#define R592_STATUS_SFIFO_INPUT		(1 << 30)	/* Small fifo received data*/

#define R592_SFIFO_SIZE			32		/* total size of small fifo is 32 bytes */
#define R592_SFIFO_PACKET		8		/* packet size of small fifo */

/* IO control */
#define R592_IO				0x18
#define	R592_IO_16			(1 << 16)	/* Set by default, can be cleared */
#define	R592_IO_18			(1 << 18)	/* Set by default, can be cleared */
#define	R592_IO_SERIAL1			(1 << 20)	/* Set by default, can be cleared, (cleared on parallel) */
#define	R592_IO_22			(1 << 22)	/* Set by default, can be cleared */
#define R592_IO_DIRECTION		(1 << 24)	/* TPC direction (1 write 0 read) */
#define	R592_IO_26			(1 << 26)	/* Set by default, can be cleared */
#define	R592_IO_SERIAL2			(1 << 30)	/* Set by default, can be cleared (cleared on parallel), serial doesn't work if unset */
#define R592_IO_RESET			(1 << 31)	/* Reset, sets defaults*/


/* Turns hardware on/off */
#define R592_POWER			0x20		/* bits 0-7 writeable */
#define R592_POWER_0			(1 << 0)	/* set on start, cleared on stop - must be set*/
#define R592_POWER_1			(1 << 1)	/* set on start, cleared on stop - must be set*/
#define R592_POWER_3			(1 << 3)	/* must be clear */
#define R592_POWER_20			(1 << 5)	/* set before switch to parallel */

/* IO mode*/
#define R592_IO_MODE			0x24
#define R592_IO_MODE_SERIAL		1
#define R592_IO_MODE_PARALLEL		3


/* IRQ,card detection,large fifo (first word irq status, second enable) */
/* IRQs are ACKed by clearing the bits */
#define R592_REG_MSC			0x28
#define R592_REG_MSC_PRSNT		(1 << 1)	/* card present (only status)*/
#define R592_REG_MSC_IRQ_INSERT		(1 << 8)	/* detect insert / card insered */
#define R592_REG_MSC_IRQ_REMOVE		(1 << 9)	/* detect removal / card removed */
#define R592_REG_MSC_FIFO_EMPTY		(1 << 10)	/* fifo is empty */
#define R592_REG_MSC_FIFO_DMA_DONE	(1 << 11)	/* dma enable / dma done */

#define R592_REG_MSC_FIFO_USER_ORN	(1 << 12)	/* set if software reads empty fifo (if R592_REG_MSC_FIFO_EMPTY is set) */
#define R592_REG_MSC_FIFO_MISMATH	(1 << 13)	/* set if amount of data in fifo doesn't match amount in TPC */
#define R592_REG_MSC_FIFO_DMA_ERR	(1 << 14)	/* IO failure */
#define R592_REG_MSC_LED		(1 << 15)	/* clear to turn led off (only status)*/

#define DMA_IRQ_ACK_MASK \
	(R592_REG_MSC_FIFO_DMA_DONE | R592_REG_MSC_FIFO_DMA_ERR)

#define DMA_IRQ_EN_MASK (DMA_IRQ_ACK_MASK << 16)

#define IRQ_ALL_ACK_MASK 0x00007F00
#define IRQ_ALL_EN_MASK (IRQ_ALL_ACK_MASK << 16)

/* DMA address for large FIFO read/writes*/
#define R592_FIFO_DMA			0x2C

/* PIO access to large FIFO (512 bytes) (big endian)*/
#define R592_FIFO_PIO			0x30
#define R592_LFIFO_SIZE			512		/* large fifo size */


/* large FIFO DMA settings */
#define R592_FIFO_DMA_SETTINGS		0x34
#define R592_FIFO_DMA_SETTINGS_EN	(1 << 0)	/* DMA enabled */
#define R592_FIFO_DMA_SETTINGS_DIR	(1 << 1)	/* Dma direction (1 read, 0 write) */
#define R592_FIFO_DMA_SETTINGS_CAP	(1 << 24)	/* Dma is aviable */

/* Maybe just an delay */
/* Bits 17..19 are just number */
/* bit 16 is set, then bit 20 is waited */
/* time to wait is about 50 spins * 2 ^ (bits 17..19) */
/* seems to be possible just to ignore */
/* Probably debug register */
#define R592_REG38			0x38
#define R592_REG38_CHANGE		(1 << 16)	/* Start bit */
#define R592_REG38_DONE			(1 << 20)	/* HW set this after the delay */
#define R592_REG38_SHIFT		17

/* Debug register, written (0xABCDEF00) when error happens - not used*/
#define R592_REG_3C			0x3C

struct r592_device {
	struct pci_dev *pci_dev;
	struct memstick_host	*host;		/* host backpointer */
	struct memstick_request *req;		/* current request */

	/* Registers, IRQ */
	void __iomem *mmio;
	int irq;
	spinlock_t irq_lock;
	spinlock_t io_thread_lock;
	struct timer_list detect_timer;

	struct task_struct *io_thread;
	bool parallel_mode;

	DECLARE_KFIFO(pio_fifo, u8, sizeof(u32));

	/* DMA area */
	int dma_capable;
	int dma_error;
	struct completion dma_done;
	void *dummy_dma_page;
	dma_addr_t dummy_dma_page_physical_address;

};

#define DRV_NAME "r592"


#define message(format, ...) \
	printk(KERN_INFO DRV_NAME ": " format "\n", ## __VA_ARGS__)

#define __dbg(level, format, ...) \
	do { \
		if (debug >= level) \
			printk(KERN_DEBUG DRV_NAME \
				": " format "\n", ## __VA_ARGS__); \
	} while (0)


#define dbg(format, ...)		__dbg(1, format, ## __VA_ARGS__)
#define dbg_verbose(format, ...)	__dbg(2, format, ## __VA_ARGS__)
#define dbg_reg(format, ...)		__dbg(3, format, ## __VA_ARGS__)

#endif
