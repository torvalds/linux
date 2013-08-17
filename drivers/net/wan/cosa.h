/* $Id: cosa.h,v 1.6 1999/01/06 14:02:44 kas Exp $ */

/*
 *  Copyright (C) 1995-1997  Jan "Yenya" Kasprzak <kas@fi.muni.cz>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef COSA_H__
#define COSA_H__

#include <linux/ioctl.h>

#ifdef __KERNEL__
/* status register - output bits */
#define SR_RX_DMA_ENA   0x04    /* receiver DMA enable bit */
#define SR_TX_DMA_ENA   0x08    /* transmitter DMA enable bit */
#define SR_RST          0x10    /* SRP reset */
#define SR_USR_INT_ENA  0x20    /* user interrupt enable bit */
#define SR_TX_INT_ENA   0x40    /* transmitter interrupt enable bit */
#define SR_RX_INT_ENA   0x80    /* receiver interrupt enable bit */

/* status register - input bits */
#define SR_USR_RQ       0x20    /* user interrupt request pending */
#define SR_TX_RDY       0x40    /* transmitter empty (ready) */
#define SR_RX_RDY       0x80    /* receiver data ready */

#define SR_UP_REQUEST   0x02    /* request from SRP to transfer data
                                   up to PC */
#define SR_DOWN_REQUEST 0x01    /* SRP is able to transfer data down
                                   from PC to SRP */
#define SR_END_OF_TRANSFER      0x03    /* SRP signalize end of
                                           transfer (up or down) */

#define SR_CMD_FROM_SRP_MASK    0x03    /* mask to get SRP command */

/* bits in driver status byte definitions : */
#define SR_RDY_RCV      0x01    /* ready to receive packet */
#define SR_RDY_SND      0x02    /* ready to send packet */
#define SR_CMD_PND      0x04    /* command pending */ /* not currently used */

/* ???? */
#define SR_PKT_UP       0x01    /* transfer of packet up in progress */
#define SR_PKT_DOWN     0x02    /* transfer of packet down in progress */

#endif /* __KERNEL__ */

#define SR_LOAD_ADDR    0x4400  /* SRP microcode load address */
#define SR_START_ADDR   0x4400  /* SRP microcode start address */

#define COSA_LOAD_ADDR    0x400  /* SRP microcode load address */
#define COSA_MAX_FIRMWARE_SIZE	0x10000

/* ioctls */
struct cosa_download {
	int addr, len;
	char __user *code;
};

/* Reset the device */
#define COSAIORSET	_IO('C',0xf0)

/* Start microcode at given address */
#define COSAIOSTRT	_IOW('C',0xf1, int)

/* Read the block from the device memory */
#define COSAIORMEM	_IOWR('C',0xf2, struct cosa_download *)
	/* actually the struct cosa_download itself; this is to keep
	 * the ioctl number same as in 2.4 in order to keep the user-space
	 * utils compatible. */

/* Write the block to the device memory (i.e. download the microcode) */
#define COSAIODOWNLD	_IOW('C',0xf2, struct cosa_download *)
	/* actually the struct cosa_download itself; this is to keep
	 * the ioctl number same as in 2.4 in order to keep the user-space
	 * utils compatible. */

/* Read the device type (one of "srp", "cosa", and "cosa8" for now) */
#define COSAIORTYPE	_IOR('C',0xf3, char *)

/* Read the device identification string */
#define COSAIORIDSTR	_IOR('C',0xf4, char *)
/* Maximum length of the identification string. */
#define COSA_MAX_ID_STRING 128

/* Increment/decrement the module usage count :-) */
/* #define COSAIOMINC	_IO('C',0xf5) */
/* #define COSAIOMDEC	_IO('C',0xf6) */

/* Get the total number of cards installed */
#define COSAIONRCARDS	_IO('C',0xf7)

/* Get the number of channels on this card */
#define COSAIONRCHANS	_IO('C',0xf8)

/* Set the driver for the bus-master operations */
#define COSAIOBMSET	_IOW('C', 0xf9, unsigned short)

#define COSA_BM_OFF	0	/* Bus-mastering off - use ISA DMA (default) */
#define COSA_BM_ON	1	/* Bus-mastering on - faster but untested */

/* Gets the busmaster status */
#define COSAIOBMGET	_IO('C', 0xfa)

#endif /* !COSA_H__ */
