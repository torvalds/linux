/*
 *  linux/drivers/acorn/net/ether3.h
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  network driver for Acorn/ANT Ether3 cards
 */

#ifndef _LINUX_ether3_H
#define _LINUX_ether3_H

/* use 0 for production, 1 for verification, >2 for debug. debug flags: */
#define DEBUG_TX	 2
#define DEBUG_RX	 4
#define DEBUG_INT	 8
#define DEBUG_IC	16
#ifndef NET_DEBUG
#define NET_DEBUG 	0
#endif

#define priv(dev)	((struct dev_priv *)netdev_priv(dev))

/* Command register definitions & bits */
#define REG_COMMAND		(priv(dev)->seeq + 0x0000)
#define CMD_ENINTDMA		0x0001
#define CMD_ENINTRX		0x0002
#define CMD_ENINTTX		0x0004
#define CMD_ENINTBUFWIN		0x0008
#define CMD_ACKINTDMA		0x0010
#define CMD_ACKINTRX		0x0020
#define CMD_ACKINTTX		0x0040
#define CMD_ACKINTBUFWIN	0x0080
#define CMD_DMAON		0x0100
#define CMD_RXON		0x0200
#define CMD_TXON		0x0400
#define CMD_DMAOFF		0x0800
#define CMD_RXOFF		0x1000
#define CMD_TXOFF		0x2000
#define CMD_FIFOREAD		0x4000
#define CMD_FIFOWRITE		0x8000

/* status register */
#define REG_STATUS		(priv(dev)->seeq + 0x0000)
#define STAT_ENINTSTAT		0x0001
#define STAT_ENINTRX		0x0002
#define STAT_ENINTTX		0x0004
#define STAT_ENINTBUFWIN	0x0008
#define STAT_INTDMA		0x0010
#define STAT_INTRX		0x0020
#define STAT_INTTX		0x0040
#define STAT_INTBUFWIN		0x0080
#define STAT_DMAON		0x0100
#define STAT_RXON		0x0200
#define STAT_TXON		0x0400
#define STAT_FIFOFULL		0x2000
#define STAT_FIFOEMPTY		0x4000
#define STAT_FIFODIR		0x8000

/* configuration register 1 */
#define REG_CONFIG1		(priv(dev)->seeq + 0x0040)
#define CFG1_BUFSELSTAT0	0x0000
#define CFG1_BUFSELSTAT1	0x0001
#define CFG1_BUFSELSTAT2	0x0002
#define CFG1_BUFSELSTAT3	0x0003
#define CFG1_BUFSELSTAT4	0x0004
#define CFG1_BUFSELSTAT5	0x0005
#define CFG1_ADDRPROM		0x0006
#define CFG1_TRANSEND		0x0007
#define CFG1_LOCBUFMEM		0x0008
#define CFG1_INTVECTOR		0x0009
#define CFG1_RECVSPECONLY	0x0000
#define CFG1_RECVSPECBROAD	0x4000
#define CFG1_RECVSPECBRMULTI	0x8000
#define CFG1_RECVPROMISC	0xC000

/* The following aren't in 8004 */
#define CFG1_DMABURSTCONT	0x0000
#define CFG1_DMABURST800NS	0x0010
#define CFG1_DMABURST1600NS	0x0020
#define CFG1_DMABURST3200NS	0x0030
#define CFG1_DMABURST1		0x0000
#define CFG1_DMABURST4		0x0040
#define CFG1_DMABURST8		0x0080
#define CFG1_DMABURST16		0x00C0
#define CFG1_RECVCOMPSTAT0	0x0100
#define CFG1_RECVCOMPSTAT1	0x0200
#define CFG1_RECVCOMPSTAT2	0x0400
#define CFG1_RECVCOMPSTAT3	0x0800
#define CFG1_RECVCOMPSTAT4	0x1000
#define CFG1_RECVCOMPSTAT5	0x2000

/* configuration register 2 */
#define REG_CONFIG2		(priv(dev)->seeq + 0x0080)
#define CFG2_BYTESWAP		0x0001
#define CFG2_ERRENCRC		0x0008
#define CFG2_ERRENDRIBBLE	0x0010
#define CFG2_ERRSHORTFRAME	0x0020
#define CFG2_SLOTSELECT		0x0040
#define CFG2_PREAMSELECT	0x0080
#define CFG2_ADDRLENGTH		0x0100
#define CFG2_RECVCRC		0x0200
#define CFG2_XMITNOCRC		0x0400
#define CFG2_LOOPBACK		0x0800
#define CFG2_CTRLO		0x1000
#define CFG2_RESET		0x8000

#define REG_RECVEND		(priv(dev)->seeq + 0x00c0)

#define REG_BUFWIN		(priv(dev)->seeq + 0x0100)

#define REG_RECVPTR		(priv(dev)->seeq + 0x0140)

#define REG_TRANSMITPTR		(priv(dev)->seeq + 0x0180)

#define REG_DMAADDR		(priv(dev)->seeq + 0x01c0)

/*
 * Cards transmit/receive headers
 */
#define TX_NEXT			(0xffff)
#define TXHDR_ENBABBLEINT	(1 << 16)
#define TXHDR_ENCOLLISIONINT	(1 << 17)
#define TXHDR_EN16COLLISION	(1 << 18)
#define TXHDR_ENSUCCESS		(1 << 19)
#define TXHDR_DATAFOLLOWS	(1 << 21)
#define TXHDR_CHAINCONTINUE	(1 << 22)
#define TXHDR_TRANSMIT		(1 << 23)
#define TXSTAT_BABBLED		(1 << 24)
#define TXSTAT_COLLISION	(1 << 25)
#define TXSTAT_16COLLISIONS	(1 << 26)
#define TXSTAT_DONE		(1 << 31)

#define RX_NEXT			(0xffff)
#define RXHDR_CHAINCONTINUE	(1 << 6)
#define RXHDR_RECEIVE		(1 << 7)
#define RXSTAT_OVERSIZE		(1 << 8)
#define RXSTAT_CRCERROR		(1 << 9)
#define RXSTAT_DRIBBLEERROR	(1 << 10)
#define RXSTAT_SHORTPACKET	(1 << 11)
#define RXSTAT_DONE		(1 << 15)


#define TX_START	0x0000
#define TX_END		0x6000
#define RX_START	0x6000
#define RX_LEN		0xA000
#define RX_END		0x10000
/* must be a power of 2 and greater than MAX_TX_BUFFERED */
#define MAX_TXED	16
#define MAX_TX_BUFFERED	10

struct dev_priv {
    void __iomem *base;
    void __iomem *seeq;
    struct {
	unsigned int command;
	unsigned int config1;
	unsigned int config2;
    } regs;
    unsigned char tx_head;		/* buffer nr to insert next packet	 */
    unsigned char tx_tail;		/* buffer nr of transmitting packet	 */
    unsigned int rx_head;		/* address to fetch next packet from	 */
    struct net_device_stats stats;
    struct timer_list timer;
    int broken;				/* 0 = ok, 1 = something went wrong	 */
};

struct ether3_data {
	const char name[8];
	unsigned long base_offset;
};

#endif
