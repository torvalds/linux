/*********************************************************************
 *                
 * Filename:      nsc-ircc.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri Nov 13 14:37:40 1998
 * Modified at:   Sun Jan 23 17:47:00 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-2000 Dag Brattli <dagb@cs.uit.no>
 *     Copyright (c) 1998 Lichen Wang, <lwang@actisys.com>
 *     Copyright (c) 1998 Actisys Corp., www.actisys.com
 *     All Rights Reserved
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#ifndef NSC_IRCC_H
#define NSC_IRCC_H

#include <linux/time.h>

#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/types.h>
#include <asm/io.h>

/* Features for chips (set in driver_data) */
#define NSC_FORCE_DONGLE_TYPE9	0x00000001

/* DMA modes needed */
#define DMA_TX_MODE     0x08    /* Mem to I/O, ++, demand. */
#define DMA_RX_MODE     0x04    /* I/O to mem, ++, demand. */

/* Config registers for the '108 */
#define CFG_108_BAIC 0x00
#define CFG_108_CSRT 0x01
#define CFG_108_MCTL 0x02

/* Config registers for the '338 */
#define CFG_338_FER  0x00
#define CFG_338_FAR  0x01
#define CFG_338_PTR  0x02
#define CFG_338_PNP0 0x1b
#define CFG_338_PNP1 0x1c
#define CFG_338_PNP3 0x4f

/* Config registers for the '39x (in the logical device bank) */
#define CFG_39X_LDN	0x07	/* Logical device number (Super I/O bank) */
#define CFG_39X_SIOCF1	0x21	/* SuperI/O Config */
#define CFG_39X_ACT	0x30	/* Device activation */
#define CFG_39X_BASEH	0x60	/* Device base address (high bits) */
#define CFG_39X_BASEL	0x61	/* Device base address (low bits) */
#define CFG_39X_IRQNUM	0x70	/* Interrupt number & wake up enable */
#define CFG_39X_IRQSEL	0x71	/* Interrupt select (edge/level + polarity) */
#define CFG_39X_DMA0	0x74	/* DMA 0 configuration */
#define CFG_39X_DMA1	0x75	/* DMA 1 configuration */
#define CFG_39X_SPC	0xF0	/* Serial port configuration register */

/* Flags for configuration register CRF0 */
#define APEDCRC		0x02
#define ENBNKSEL	0x01

/* Set 0 */
#define TXD             0x00 /* Transmit data port */
#define RXD             0x00 /* Receive data port */

/* Register 1 */
#define IER		0x01 /* Interrupt Enable Register*/
#define IER_RXHDL_IE    0x01 /* Receiver high data level interrupt */
#define IER_TXLDL_IE    0x02 /* Transeiver low data level interrupt */
#define IER_LS_IE	0x04//* Link Status Interrupt */
#define IER_ETXURI      0x04 /* Tx underrun */
#define IER_DMA_IE	0x10 /* DMA finished interrupt */
#define IER_TXEMP_IE    0x20
#define IER_SFIF_IE     0x40 /* Frame status FIFO intr */
#define IER_TMR_IE      0x80 /* Timer event */

#define FCR		0x02 /* (write only) */
#define FCR_FIFO_EN     0x01 /* Enable FIFO's */
#define FCR_RXSR        0x02 /* Rx FIFO soft reset */
#define FCR_TXSR        0x04 /* Tx FIFO soft reset */
#define FCR_RXTH	0x40 /* Rx FIFO threshold (set to 16) */
#define FCR_TXTH	0x20 /* Tx FIFO threshold (set to 17) */

#define EIR		0x02 /* (read only) */
#define EIR_RXHDL_EV	0x01
#define EIR_TXLDL_EV    0x02
#define EIR_LS_EV	0x04
#define EIR_DMA_EV	0x10
#define EIR_TXEMP_EV	0x20
#define EIR_SFIF_EV     0x40
#define EIR_TMR_EV      0x80

#define LCR             0x03 /* Link control register */
#define LCR_WLS_8       0x03 /* 8 bits */

#define BSR 	        0x03 /* Bank select register */
#define BSR_BKSE        0x80
#define BANK0 	        LCR_WLS_8 /* Must make sure that we set 8N1 */
#define BANK1	        0x80
#define BANK2	        0xe0
#define BANK3	        0xe4
#define BANK4	        0xe8
#define BANK5	        0xec
#define BANK6	        0xf0
#define BANK7     	0xf4

#define MCR		0x04 /* Mode Control Register */
#define MCR_MODE_MASK	~(0xd0)
#define MCR_UART        0x00
#define MCR_RESERVED  	0x20	
#define MCR_SHARP_IR    0x40
#define MCR_SIR         0x60
#define MCR_MIR  	0x80
#define MCR_FIR		0xa0
#define MCR_CEIR        0xb0
#define MCR_IR_PLS      0x10
#define MCR_DMA_EN	0x04
#define MCR_EN_IRQ	0x08
#define MCR_TX_DFR	0x08

#define LSR             0x05 /* Link status register */
#define LSR_RXDA        0x01 /* Receiver data available */
#define LSR_TXRDY       0x20 /* Transmitter ready */
#define LSR_TXEMP       0x40 /* Transmitter empty */

#define ASCR            0x07 /* Auxillary Status and Control Register */
#define ASCR_RXF_TOUT   0x01 /* Rx FIFO timeout */
#define ASCR_FEND_INF   0x02 /* Frame end bytes in rx FIFO */
#define ASCR_S_EOT      0x04 /* Set end of transmission */
#define ASCT_RXBSY      0x20 /* Rx busy */
#define ASCR_TXUR       0x40 /* Transeiver underrun */
#define ASCR_CTE        0x80 /* Clear timer event */

/* Bank 2 */
#define BGDL            0x00 /* Baud Generator Divisor Port (Low Byte) */
#define BGDH            0x01 /* Baud Generator Divisor Port (High Byte) */

#define ECR1		0x02 /* Extended Control Register 1 */
#define ECR1_EXT_SL	0x01 /* Extended Mode Select */
#define ECR1_DMANF	0x02 /* DMA Fairness */
#define ECR1_DMATH      0x04 /* DMA Threshold */
#define ECR1_DMASWP	0x08 /* DMA Swap */

#define EXCR2		0x04
#define EXCR2_TFSIZ	0x01 /* Rx FIFO size = 32 */
#define EXCR2_RFSIZ	0x04 /* Tx FIFO size = 32 */

#define TXFLV           0x06 /* Tx FIFO level */
#define RXFLV           0x07 /* Rx FIFO level */

/* Bank 3 */
#define MID		0x00

/* Bank 4 */
#define TMRL            0x00 /* Timer low byte */
#define TMRH            0x01 /* Timer high byte */
#define IRCR1           0x02 /* Infrared control register 1 */
#define IRCR1_TMR_EN    0x01 /* Timer enable */

#define TFRLL		0x04
#define TFRLH		0x05
#define RFRLL		0x06
#define RFRLH		0x07

/* Bank 5 */
#define IRCR2           0x04 /* Infrared control register 2 */
#define IRCR2_MDRS      0x04 /* MIR data rate select */
#define IRCR2_FEND_MD   0x20 /* */

#define FRM_ST          0x05 /* Frame status FIFO */
#define FRM_ST_VLD      0x80 /* Frame status FIFO data valid */
#define FRM_ST_ERR_MSK  0x5f
#define FRM_ST_LOST_FR  0x40 /* Frame lost */
#define FRM_ST_MAX_LEN  0x10 /* Max frame len exceeded */
#define FRM_ST_PHY_ERR  0x08 /* Physical layer error */
#define FRM_ST_BAD_CRC  0x04 
#define FRM_ST_OVR1     0x02 /* Rx FIFO overrun */
#define FRM_ST_OVR2     0x01 /* Frame status FIFO overrun */

#define RFLFL           0x06
#define RFLFH           0x07

/* Bank 6 */
#define IR_CFG2		0x00
#define IR_CFG2_DIS_CRC	0x02

/* Bank 7 */
#define IRM_CR		0x07 /* Infrared module control register */
#define IRM_CR_IRX_MSL	0x40
#define IRM_CR_AF_MNT   0x80 /* Automatic format */

/* NSC chip information */
struct nsc_chip {
	char *name;          /* Name of chipset */
	int cfg[3];          /* Config registers */
	u_int8_t cid_index;  /* Chip identification index reg */
	u_int8_t cid_value;  /* Chip identification expected value */
	u_int8_t cid_mask;   /* Chip identification revision mask */

	/* Functions for probing and initializing the specific chip */
	int (*probe)(struct nsc_chip *chip, chipio_t *info);
	int (*init)(struct nsc_chip *chip, chipio_t *info);
};
typedef struct nsc_chip nsc_chip_t;

/* For storing entries in the status FIFO */
struct st_fifo_entry {
	int status;
	int len;
};

#define MAX_TX_WINDOW 7
#define MAX_RX_WINDOW 7

struct st_fifo {
	struct st_fifo_entry entries[MAX_RX_WINDOW];
	int pending_bytes;
	int head;
	int tail;
	int len;
};

struct frame_cb {
	void *start; /* Start of frame in DMA mem */
	int len;     /* Length of frame in DMA mem */
};

struct tx_fifo {
	struct frame_cb queue[MAX_TX_WINDOW]; /* Info about frames in queue */
	int             ptr;                  /* Currently being sent */
	int             len;                  /* Length of queue */
	int             free;                 /* Next free slot */
	void           *tail;                 /* Next free start in DMA mem */
};

/* Private data for each instance */
struct nsc_ircc_cb {
	struct st_fifo st_fifo;    /* Info about received frames */
	struct tx_fifo tx_fifo;    /* Info about frames to be transmitted */

	struct net_device *netdev;     /* Yes! we are some kind of netdevice */
	
	struct irlap_cb *irlap;    /* The link layer we are binded to */
	struct qos_info qos;       /* QoS capabilities for this device */
	
	chipio_t io;               /* IrDA controller information */
	iobuff_t tx_buff;          /* Transmit buffer */
	iobuff_t rx_buff;          /* Receive buffer */
	dma_addr_t tx_buff_dma;
	dma_addr_t rx_buff_dma;

	__u8 ier;                  /* Interrupt enable register */

	struct timeval stamp;
	struct timeval now;

	spinlock_t lock;           /* For serializing operations */
	
	__u32 new_speed;
	int index;                 /* Instance index */

	struct platform_device *pldev;
};

static inline void switch_bank(int iobase, int bank)
{
		outb(bank, iobase+BSR);
}

#endif /* NSC_IRCC_H */
