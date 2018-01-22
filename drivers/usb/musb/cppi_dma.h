/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2005-2006 by Texas Instruments */

#ifndef _CPPI_DMA_H_
#define _CPPI_DMA_H_

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/dmapool.h>
#include <linux/dmaengine.h>

#include "musb_core.h"
#include "musb_dma.h"

/* CPPI RX/TX state RAM */

struct cppi_tx_stateram {
	u32 tx_head;			/* "DMA packet" head descriptor */
	u32 tx_buf;
	u32 tx_current;			/* current descriptor */
	u32 tx_buf_current;
	u32 tx_info;			/* flags, remaining buflen */
	u32 tx_rem_len;
	u32 tx_dummy;			/* unused */
	u32 tx_complete;
};

struct cppi_rx_stateram {
	u32 rx_skipbytes;
	u32 rx_head;
	u32 rx_sop;			/* "DMA packet" head descriptor */
	u32 rx_current;			/* current descriptor */
	u32 rx_buf_current;
	u32 rx_len_len;
	u32 rx_cnt_cnt;
	u32 rx_complete;
};

/* hw_options bits in CPPI buffer descriptors */
#define CPPI_SOP_SET	((u32)(1 << 31))
#define CPPI_EOP_SET	((u32)(1 << 30))
#define CPPI_OWN_SET	((u32)(1 << 29))	/* owned by cppi */
#define CPPI_EOQ_MASK	((u32)(1 << 28))
#define CPPI_ZERO_SET	((u32)(1 << 23))	/* rx saw zlp; tx issues one */
#define CPPI_RXABT_MASK	((u32)(1 << 19))	/* need more rx buffers */

#define CPPI_RECV_PKTLEN_MASK 0xFFFF
#define CPPI_BUFFER_LEN_MASK 0xFFFF

#define CPPI_TEAR_READY ((u32)(1 << 31))

/* CPPI data structure definitions */

#define	CPPI_DESCRIPTOR_ALIGN	16	/* bytes; 5-dec docs say 4-byte align */

struct cppi_descriptor {
	/* hardware overlay */
	u32		hw_next;	/* next buffer descriptor Pointer */
	u32		hw_bufp;	/* i/o buffer pointer */
	u32		hw_off_len;	/* buffer_offset16, buffer_length16 */
	u32		hw_options;	/* flags:  SOP, EOP etc*/

	struct cppi_descriptor *next;
	dma_addr_t	dma;		/* address of this descriptor */
	u32		buflen;		/* for RX: original buffer length */
} __attribute__ ((aligned(CPPI_DESCRIPTOR_ALIGN)));


struct cppi;

/* CPPI  Channel Control structure */
struct cppi_channel {
	struct dma_channel	channel;

	/* back pointer to the DMA controller structure */
	struct cppi		*controller;

	/* which direction of which endpoint? */
	struct musb_hw_ep	*hw_ep;
	bool			transmit;
	u8			index;

	/* DMA modes:  RNDIS or "transparent" */
	u8			is_rndis;

	/* book keeping for current transfer request */
	dma_addr_t		buf_dma;
	u32			buf_len;
	u32			maxpacket;
	u32			offset;		/* dma requested */

	void __iomem		*state_ram;	/* CPPI state */

	struct cppi_descriptor	*freelist;

	/* BD management fields */
	struct cppi_descriptor	*head;
	struct cppi_descriptor	*tail;
	struct cppi_descriptor	*last_processed;

	/* use tx_complete in host role to track endpoints waiting for
	 * FIFONOTEMPTY to clear.
	 */
	struct list_head	tx_complete;
};

/* CPPI DMA controller object */
struct cppi {
	struct dma_controller		controller;
	void __iomem			*mregs;		/* Mentor regs */
	void __iomem			*tibase;	/* TI/CPPI regs */

	int				irq;

	struct cppi_channel		tx[4];
	struct cppi_channel		rx[4];

	struct dma_pool			*pool;

	struct list_head		tx_complete;
};

/* CPPI IRQ handler */
extern irqreturn_t cppi_interrupt(int, void *);

struct cppi41_dma_channel {
	struct dma_channel channel;
	struct cppi41_dma_controller *controller;
	struct musb_hw_ep *hw_ep;
	struct dma_chan *dc;
	dma_cookie_t cookie;
	u8 port_num;
	u8 is_tx;
	u8 is_allocated;
	u8 usb_toggle;

	dma_addr_t buf_addr;
	u32 total_len;
	u32 prog_len;
	u32 transferred;
	u32 packet_sz;
	struct list_head tx_check;
	int tx_zlp;
};

#endif				/* end of ifndef _CPPI_DMA_H_ */
