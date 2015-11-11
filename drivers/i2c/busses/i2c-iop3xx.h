/* ------------------------------------------------------------------------- */
/* i2c-iop3xx.h algorithm driver definitions private to i2c-iop3xx.c         */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 2003 Peter Milne, D-TACQ Solutions Ltd
 *                      <Peter dot Milne at D hyphen TACQ dot com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.			     */
/* ------------------------------------------------------------------------- */


#ifndef I2C_IOP3XX_H
#define I2C_IOP3XX_H 1

/*
 * iop321 hardware bit definitions
 */
#define IOP3XX_ICR_FAST_MODE	0x8000	/* 1=400kBps, 0=100kBps */
#define IOP3XX_ICR_UNIT_RESET	0x4000	/* 1=RESET */
#define IOP3XX_ICR_SAD_IE	0x2000	/* 1=Slave Detect Interrupt Enable */
#define IOP3XX_ICR_ALD_IE	0x1000	/* 1=Arb Loss Detect Interrupt Enable */
#define IOP3XX_ICR_SSD_IE	0x0800	/* 1=Slave STOP Detect Interrupt Enable */
#define IOP3XX_ICR_BERR_IE	0x0400	/* 1=Bus Error Interrupt Enable */
#define IOP3XX_ICR_RXFULL_IE	0x0200	/* 1=Receive Full Interrupt Enable */
#define IOP3XX_ICR_TXEMPTY_IE	0x0100	/* 1=Transmit Empty Interrupt Enable */
#define IOP3XX_ICR_GCD		0x0080	/* 1=General Call Disable */
/*
 * IOP3XX_ICR_GCD: 1 disables response as slave. "This bit must be set
 * when sending a master mode general call message from the I2C unit"
 */
#define IOP3XX_ICR_UE		0x0040	/* 1=Unit Enable */
/*
 * "NOTE: To avoid I2C bus integrity problems, 
 * the user needs to ensure that the GPIO Output Data Register - 
 * GPOD bits associated with an I2C port are cleared prior to setting 
 * the enable bit for that I2C serial port. 
 * The user prepares to enable I2C port 0 and 
 * I2C port 1 by clearing GPOD bits 7:6 and GPOD bits 5:4, respectively.
 */
#define IOP3XX_ICR_SCLEN	0x0020	/* 1=SCL enable for master mode */
#define IOP3XX_ICR_MABORT	0x0010	/* 1=Send a STOP with no data 
					 * NB TBYTE must be clear */
#define IOP3XX_ICR_TBYTE	0x0008	/* 1=Send/Receive a byte. i2c clears */
#define IOP3XX_ICR_NACK		0x0004	/* 1=reply with NACK */
#define IOP3XX_ICR_MSTOP	0x0002	/* 1=send a STOP after next data byte */
#define IOP3XX_ICR_MSTART	0x0001	/* 1=initiate a START */


#define IOP3XX_ISR_BERRD	0x0400	/* 1=BUS ERROR Detected */
#define IOP3XX_ISR_SAD		0x0200	/* 1=Slave ADdress Detected */
#define IOP3XX_ISR_GCAD		0x0100	/* 1=General Call Address Detected */
#define IOP3XX_ISR_RXFULL	0x0080	/* 1=Receive Full */
#define IOP3XX_ISR_TXEMPTY	0x0040	/* 1=Transmit Empty */
#define IOP3XX_ISR_ALD		0x0020	/* 1=Arbitration Loss Detected */
#define IOP3XX_ISR_SSD		0x0010	/* 1=Slave STOP Detected */
#define IOP3XX_ISR_BBUSY	0x0008	/* 1=Bus BUSY */
#define IOP3XX_ISR_UNITBUSY	0x0004	/* 1=Unit Busy */
#define IOP3XX_ISR_NACK		0x0002	/* 1=Unit Rx or Tx a NACK */
#define IOP3XX_ISR_RXREAD	0x0001	/* 1=READ 0=WRITE (R/W bit of slave addr */

#define IOP3XX_ISR_CLEARBITS	0x07f0

#define IOP3XX_ISAR_SAMASK	0x007f

#define IOP3XX_IDBR_MASK	0x00ff

#define IOP3XX_IBMR_SCL		0x0002
#define IOP3XX_IBMR_SDA		0x0001

#define IOP3XX_GPOD_I2C0	0x00c0	/* clear these bits to enable ch0 */
#define IOP3XX_GPOD_I2C1	0x0030	/* clear these bits to enable ch1 */

#define MYSAR			0	/* default slave address */

#define I2C_ERR			321
#define I2C_ERR_BERR		(I2C_ERR+0)
#define I2C_ERR_ALD		(I2C_ERR+1)


#define	CR_OFFSET		0
#define	SR_OFFSET		0x4
#define	SAR_OFFSET		0x8
#define	DBR_OFFSET		0xc
#define	CCR_OFFSET		0x10
#define	BMR_OFFSET		0x14

#define	IOP3XX_I2C_IO_SIZE	0x18

struct i2c_algo_iop3xx_data {
	void __iomem *ioaddr;
	wait_queue_head_t waitq;
	spinlock_t lock;
	u32 SR_enabled, SR_received;
	int id;
};

#endif /* I2C_IOP3XX_H */
