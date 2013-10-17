/*	Copyright (C) 2009 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
 *	Copyright (C) 2009 Alban Browaeys <prahal@yahoo.com>
 *	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
 *	Copyright (C) 2009 Luis Correia <luis.f.correia@gmail.com>
 *	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
 *	Copyright (C) 2009 Mark Asselstine <asselsm@gmail.com>
 *	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
 *	Copyright (C) 2009 Bart Zolnierkiewicz <bzolnier@gmail.com>
 *	<http://rt2x00.serialmonkey.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the
 *	Free Software Foundation, Inc.,
 *	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*	Module: rt2800mmio
 *	Abstract: forward declarations for the rt2800mmio module.
 */

#ifndef RT2800MMIO_H
#define RT2800MMIO_H

/*
 * DMA descriptor defines.
 */
#define TXD_DESC_SIZE			(4 * sizeof(__le32))

/*
 * TX descriptor format for TX, PRIO and Beacon Ring.
 */

/*
 * Word0
 */
#define TXD_W0_SD_PTR0			FIELD32(0xffffffff)

/*
 * Word1
 */
#define TXD_W1_SD_LEN1			FIELD32(0x00003fff)
#define TXD_W1_LAST_SEC1		FIELD32(0x00004000)
#define TXD_W1_BURST			FIELD32(0x00008000)
#define TXD_W1_SD_LEN0			FIELD32(0x3fff0000)
#define TXD_W1_LAST_SEC0		FIELD32(0x40000000)
#define TXD_W1_DMA_DONE			FIELD32(0x80000000)

/*
 * Word2
 */
#define TXD_W2_SD_PTR1			FIELD32(0xffffffff)

/*
 * Word3
 * WIV: Wireless Info Valid. 1: Driver filled WI, 0: DMA needs to copy WI
 * QSEL: Select on-chip FIFO ID for 2nd-stage output scheduler.
 *       0:MGMT, 1:HCCA 2:EDCA
 */
#define TXD_W3_WIV			FIELD32(0x01000000)
#define TXD_W3_QSEL			FIELD32(0x06000000)
#define TXD_W3_TCO			FIELD32(0x20000000)
#define TXD_W3_UCO			FIELD32(0x40000000)
#define TXD_W3_ICO			FIELD32(0x80000000)


/* TX descriptor initialization */
__le32 *rt2800mmio_get_txwi(struct queue_entry *entry);
void rt2800mmio_write_tx_desc(struct queue_entry *entry,
			      struct txentry_desc *txdesc);

#endif /* RT2800MMIO_H */
