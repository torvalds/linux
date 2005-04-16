/*********************************************************************
 *                
 * Filename:      ali-ircc.h
 * Version:       0.5
 * Description:   Driver for the ALI M1535D and M1543C FIR Controller
 * Status:        Experimental.
 * Author:        Benjamin Kong <benjamin_kong@ali.com.tw>
 * Created at:    2000/10/16 03:46PM
 * Modified at:   2001/1/3 02:56PM
 * Modified by:   Benjamin Kong <benjamin_kong@ali.com.tw>
 * 
 *     Copyright (c) 2000 Benjamin Kong <benjamin_kong@ali.com.tw>
 *     All Rights Reserved
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 ********************************************************************/

#ifndef ALI_IRCC_H
#define ALI_IRCC_H

#include <linux/time.h>

#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/types.h>
#include <asm/io.h>

/* SIR Register */
/* Usr definition of linux/serial_reg.h */

/* FIR Register */
#define BANK0		0x20
#define BANK1		0x21
#define BANK2		0x22
#define BANK3		0x23

#define FIR_MCR		0x07	/* Master Control Register */

/* Bank 0 */
#define FIR_DR		0x00	/* Alias 0, FIR Data Register (R/W) */ 
#define FIR_IER		0x01	/* Alias 1, FIR Interrupt Enable Register (R/W) */
#define FIR_IIR		0x02	/* Alias 2, FIR Interrupt Identification Register (Read only) */
#define FIR_LCR_A	0x03	/* Alias 3, FIR Line Control Register A (R/W) */
#define FIR_LCR_B	0x04	/* Alias 4, FIR Line Control Register B (R/W) */
#define FIR_LSR		0x05	/* Alias 5, FIR Line Status Register (R/W) */
#define FIR_BSR		0x06	/* Alias 6, FIR Bus Status Register (Read only) */


	/* Alias 1 */
	#define	IER_FIFO	0x10	/* FIR FIFO Interrupt Enable */	
	#define	IER_TIMER	0x20 	/* Timer Interrupt Enable */ 
	#define	IER_EOM		0x40	/* End of Message Interrupt Enable */
	#define IER_ACT		0x80	/* Active Frame Interrupt Enable */
	
	/* Alias 2 */
	#define IIR_FIFO	0x10	/* FIR FIFO Interrupt */
	#define IIR_TIMER	0x20	/* Timer Interrupt */
	#define IIR_EOM		0x40	/* End of Message Interrupt */
	#define IIR_ACT		0x80	/* Active Frame Interrupt */	
	
	/* Alias 3 */
	#define LCR_A_FIFO_RESET 0x80	/* FIFO Reset */

	/* Alias 4 */
	#define	LCR_B_BW	0x10	/* Brick Wall */
	#define LCR_B_SIP	0x20	/* SIP Enable */
	#define	LCR_B_TX_MODE 	0x40	/* Transmit Mode */
	#define LCR_B_RX_MODE	0x80	/* Receive Mode */
	
	/* Alias 5 */	
	#define LSR_FIR_LSA	0x00	/* FIR Line Status Address */
	#define LSR_FRAME_ABORT	0x08	/* Frame Abort */
	#define LSR_CRC_ERROR	0x10	/* CRC Error */
	#define LSR_SIZE_ERROR	0x20	/* Size Error */
	#define LSR_FRAME_ERROR	0x40	/* Frame Error */
	#define LSR_FIFO_UR	0x80	/* FIFO Underrun */
	#define LSR_FIFO_OR	0x80	/* FIFO Overrun */
		
	/* Alias 6 */
	#define BSR_FIFO_NOT_EMPTY	0x80	/* FIFO Not Empty */
	
/* Bank 1 */
#define	FIR_CR		0x00 	/* Alias 0, FIR Configuration Register (R/W) */
#define FIR_FIFO_TR	0x01   	/* Alias 1, FIR FIFO Threshold Register (R/W) */
#define FIR_DMA_TR	0x02	/* Alias 2, FIR DMA Threshold Register (R/W) */
#define FIR_TIMER_IIR	0x03	/* Alias 3, FIR Timer interrupt interval register (W/O) */
#define FIR_FIFO_FR	0x03	/* Alias 3, FIR FIFO Flag register (R/O) */
#define FIR_FIFO_RAR	0x04 	/* Alias 4, FIR FIFO Read Address register (R/O) */
#define FIR_FIFO_WAR	0x05	/* Alias 5, FIR FIFO Write Address register (R/O) */
#define FIR_TR		0x06	/* Alias 6, Test REgister (W/O) */

	/* Alias 0 */
	#define CR_DMA_EN	0x01	/* DMA Enable */
	#define CR_DMA_BURST	0x02	/* DMA Burst Mode */
	#define CR_TIMER_EN 	0x08	/* Timer Enable */
	
	/* Alias 3 */
	#define TIMER_IIR_500	0x00	/* 500 us */
	#define TIMER_IIR_1ms	0x01	/* 1   ms */
	#define TIMER_IIR_2ms	0x02	/* 2   ms */
	#define TIMER_IIR_4ms	0x03	/* 4   ms */
	
/* Bank 2 */
#define FIR_IRDA_CR	0x00	/* Alias 0, IrDA Control Register (R/W) */
#define FIR_BOF_CR	0x01	/* Alias 1, BOF Count Register (R/W) */
#define FIR_BW_CR	0x02	/* Alias 2, Brick Wall Count Register (R/W) */
#define FIR_TX_DSR_HI	0x03	/* Alias 3, TX Data Size Register (high) (R/W) */
#define FIR_TX_DSR_LO	0x04	/* Alias 4, TX Data Size Register (low) (R/W) */
#define FIR_RX_DSR_HI	0x05	/* Alias 5, RX Data Size Register (high) (R/W) */
#define FIR_RX_DSR_LO	0x06	/* Alias 6, RX Data Size Register (low) (R/W) */
	
	/* Alias 0 */
	#define IRDA_CR_HDLC1152 0x80	/* 1.152Mbps HDLC Select */
	#define IRDA_CR_CRC	0X40	/* CRC Select. */
	#define IRDA_CR_HDLC	0x20	/* HDLC select. */
	#define IRDA_CR_HP_MODE 0x10	/* HP mode (read only) */
	#define IRDA_CR_SD_ST	0x08	/* SD/MODE State.  */
	#define IRDA_CR_FIR_SIN 0x04	/* FIR SIN Select. */
	#define IRDA_CR_ITTX_0	0x02	/* SOUT State. IRTX force to 0 */
	#define IRDA_CR_ITTX_1	0x03	/* SOUT State. IRTX force to 1 */
	
/* Bank 3 */
#define FIR_ID_VR	0x00	/* Alias 0, FIR ID Version Register (R/O) */
#define FIR_MODULE_CR	0x01	/* Alias 1, FIR Module Control Register (R/W) */
#define FIR_IO_BASE_HI	0x02	/* Alias 2, FIR Higher I/O Base Address Register (R/O) */
#define FIR_IO_BASE_LO	0x03	/* Alias 3, FIR Lower I/O Base Address Register (R/O) */
#define FIR_IRQ_CR	0x04	/* Alias 4, FIR IRQ Channel Register (R/O) */
#define FIR_DMA_CR	0x05	/* Alias 5, FIR DMA Channel Register (R/O) */

struct ali_chip {
	char *name;
	int cfg[2];
	unsigned char entr1;
	unsigned char entr2;
	unsigned char cid_index;
	unsigned char cid_value;
	int (*probe)(struct ali_chip *chip, chipio_t *info);
	int (*init)(struct ali_chip *chip, chipio_t *info); 
};
typedef struct ali_chip ali_chip_t;


/* DMA modes needed */
#define DMA_TX_MODE     0x08    /* Mem to I/O, ++, demand. */
#define DMA_RX_MODE     0x04    /* I/O to mem, ++, demand. */

#define MAX_TX_WINDOW 	7
#define MAX_RX_WINDOW 	7

#define TX_FIFO_Threshold	8
#define RX_FIFO_Threshold	1
#define TX_DMA_Threshold	1
#define RX_DMA_Threshold	1

/* For storing entries in the status FIFO */

struct st_fifo_entry {
	int status;
	int len;
};

struct st_fifo {
	struct st_fifo_entry entries[MAX_RX_WINDOW];
	int pending_bytes;
	int head;
	int tail;
	int len;
};

struct frame_cb {
	void *start; /* Start of frame in DMA mem */
	int len;     /* Lenght of frame in DMA mem */
};

struct tx_fifo {
	struct frame_cb queue[MAX_TX_WINDOW]; /* Info about frames in queue */
	int             ptr;                  /* Currently being sent */
	int             len;                  /* Lenght of queue */
	int             free;                 /* Next free slot */
	void           *tail;                 /* Next free start in DMA mem */
};

/* Private data for each instance */
struct ali_ircc_cb {

	struct st_fifo st_fifo;    /* Info about received frames */
	struct tx_fifo tx_fifo;    /* Info about frames to be transmitted */

	struct net_device *netdev;     /* Yes! we are some kind of netdevice */
	struct net_device_stats stats;
	
	struct irlap_cb *irlap;    /* The link layer we are binded to */
	struct qos_info qos;       /* QoS capabilities for this device */
	
	chipio_t io;               /* IrDA controller information */
	iobuff_t tx_buff;          /* Transmit buffer */
	iobuff_t rx_buff;          /* Receive buffer */
	dma_addr_t tx_buff_dma;
	dma_addr_t rx_buff_dma;

	__u8 ier;                  /* Interrupt enable register */
	
	__u8 InterruptID;	   /* Interrupt ID */	
	__u8 BusStatus;		   /* Bus Status */	
	__u8 LineStatus;	   /* Line Status */	
	
	unsigned char rcvFramesOverflow;
		
	struct timeval stamp;
	struct timeval now;

	spinlock_t lock;           /* For serializing operations */
	
	__u32 new_speed;
	int index;                 /* Instance index */
	
	unsigned char fifo_opti_buf;

        struct pm_dev *dev;
};

static inline void switch_bank(int iobase, int bank)
{
		outb(bank, iobase+FIR_MCR);
}

#endif /* ALI_IRCC_H */
