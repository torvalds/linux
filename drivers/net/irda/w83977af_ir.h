/*********************************************************************
 *                
 * Filename:      w83977af_ir.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Paul VanderSpek
 * Created at:    Thu Nov 19 13:55:34 1998
 * Modified at:   Tue Jan 11 13:08:19 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-2000 Dag Brattli, All Rights Reserved.
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

#ifndef W83977AF_IR_H
#define W83977AF_IR_H

#include <asm/io.h>
#include <linux/types.h>

/* Flags for configuration register CRF0 */
#define ENBNKSEL	0x01
#define APEDCRC		0x02
#define TXW4C           0x04
#define RXW4C           0x08

/* Bank 0 */
#define RBR             0x00 /* Receiver buffer register */
#define TBR             0x00 /* Transmitter buffer register */

#define ICR		0x01 /* Interrupt configuration register */
#define ICR_ERBRI       0x01 /* Receiver buffer register interrupt */
#define ICR_ETBREI      0x02 /* Transeiver empty interrupt */
#define ICR_EUSRI	0x04//* IR status interrupt */
#define ICR_EHSRI       0x04
#define ICR_ETXURI      0x04 /* Tx underrun */
#define ICR_EDMAI	0x10 /* DMA interrupt */
#define ICR_ETXTHI      0x20 /* Transmitter threshold interrupt */
#define ICR_EFSFI       0x40 /* Frame status FIFO interrupt */
#define ICR_ETMRI       0x80 /* Timer interrupt */

#define UFR		0x02 /* FIFO control register */
#define UFR_EN_FIFO     0x01 /* Enable FIFO's */
#define UFR_RXF_RST     0x02 /* Reset Rx FIFO */
#define UFR_TXF_RST     0x04 /* Reset Tx FIFO */
#define UFR_RXTL	0x80 /* Rx FIFO threshold (set to 16) */
#define UFR_TXTL	0x20 /* Tx FIFO threshold (set to 17) */

#define ISR		0x02 /* Interrupt status register */
#define ISR_RXTH_I	0x01 /* Receive threshold interrupt */
#define ISR_TXEMP_I     0x02 /* Transmitter empty interrupt */
#define ISR_FEND_I	0x04
#define ISR_DMA_I	0x10
#define ISR_TXTH_I	0x20 /* Transmitter threshold interrupt */
#define ISR_FSF_I       0x40
#define ISR_TMR_I       0x80 /* Timer interrupt */

#define UCR             0x03 /* Uart control register */
#define UCR_DLS8        0x03 /* 8N1 */

#define SSR 	        0x03 /* Sets select register */
#define SET0 	        UCR_DLS8        /* Make sure we keep 8N1 */
#define SET1	        (0x80|UCR_DLS8) /* Make sure we keep 8N1 */
#define SET2	        0xE0
#define SET3	        0xE4
#define SET4	        0xE8
#define SET5	        0xEC
#define SET6	        0xF0
#define SET7	        0xF4

#define HCR		0x04
#define HCR_MODE_MASK	~(0xD0)
#define HCR_SIR         0x60
#define HCR_MIR_576  	0x20	
#define HCR_MIR_1152	0x80
#define HCR_FIR		0xA0
#define HCR_EN_DMA	0x04
#define HCR_EN_IRQ	0x08
#define HCR_TX_WT	0x08

#define USR             0x05 /* IR status register */
#define USR_RDR         0x01 /* Receive data ready */
#define USR_TSRE        0x40 /* Transmitter empty? */

#define AUDR            0x07
#define AUDR_SFEND      0x08 /* Set a frame end */
#define AUDR_RXBSY      0x20 /* Rx busy */
#define AUDR_UNDR       0x40 /* Transeiver underrun */

/* Set 2 */
#define ABLL            0x00 /* Advanced baud rate divisor latch (low byte) */
#define ABHL            0x01 /* Advanced baud rate divisor latch (high byte) */

#define ADCR1		0x02
#define ADCR1_ADV_SL	0x01	
#define ADCR1_D_CHSW	0x08	/* the specs are wrong. its bit 3, not 4 */
#define ADCR1_DMA_F	0x02

#define ADCR2		0x04
#define ADCR2_TXFS32	0x01
#define ADCR2_RXFS32	0x04

#define RXFDTH          0x07

/* Set 3 */
#define AUID		0x00

/* Set 4 */
#define TMRL            0x00 /* Timer value register (low byte) */
#define TMRH            0x01 /* Timer value register (high byte) */

#define IR_MSL          0x02 /* Infrared mode select */
#define IR_MSL_EN_TMR   0x01 /* Enable timer */

#define TFRLL		0x04 /* Transmitter frame length (low byte) */
#define TFRLH		0x05 /* Transmitter frame length (high byte) */
#define RFRLL		0x06 /* Receiver frame length (low byte) */
#define RFRLH		0x07 /* Receiver frame length (high byte) */

/* Set 5 */

#define FS_FO           0x05 /* Frame status FIFO */
#define FS_FO_FSFDR     0x80 /* Frame status FIFO data ready */
#define FS_FO_LST_FR    0x40 /* Frame lost */
#define FS_FO_MX_LEX    0x10 /* Max frame len exceeded */
#define FS_FO_PHY_ERR   0x08 /* Physical layer error */
#define FS_FO_CRC_ERR   0x04 
#define FS_FO_RX_OV     0x02 /* Receive overrun */
#define FS_FO_FSF_OV    0x01 /* Frame status FIFO overrun */
#define FS_FO_ERR_MSK   0x5f /* Error mask */

#define RFLFL           0x06
#define RFLFH           0x07

/* Set 6 */
#define IR_CFG2		0x00
#define IR_CFG2_DIS_CRC	0x02

/* Set 7 */
#define IRM_CR		0x07 /* Infrared module control register */
#define IRM_CR_IRX_MSL	0x40
#define IRM_CR_AF_MNT   0x80 /* Automatic format */

/* For storing entries in the status FIFO */
struct st_fifo_entry {
	int status;
	int len;
};

struct st_fifo {
	struct st_fifo_entry entries[10];
	int head;
	int tail;
	int len;
};

/* Private data for each instance */
struct w83977af_ir {
	struct st_fifo st_fifo;

	int tx_buff_offsets[10]; /* Offsets between frames in tx_buff */
	int tx_len;          /* Number of frames in tx_buff */

	struct net_device *netdev; /* Yes! we are some kind of netdevice */
	struct net_device_stats stats;
	
	struct irlap_cb    *irlap; /* The link layer we are binded to */
	struct qos_info     qos;   /* QoS capabilities for this device */
	
	chipio_t io;               /* IrDA controller information */
	iobuff_t tx_buff;          /* Transmit buffer */
	iobuff_t rx_buff;          /* Receive buffer */
	dma_addr_t tx_buff_dma;
	dma_addr_t rx_buff_dma;

	/* Note : currently locking is *very* incomplete, but this
	 * will get you started. Check in nsc-ircc.c for a proper
	 * locking strategy. - Jean II */
	spinlock_t lock;           /* For serializing operations */
	
	__u32 new_speed;
};

static inline void switch_bank( int iobase, int set)
{
	outb(set, iobase+SSR);
}

#endif
