/*
	Copyright (C) 2009 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
	Copyright (C) 2009 Axel Kollhofer <rain_maker@root-forum.org>
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2800usb
	Abstract: Data structures and registers for the rt2800usb module.
	Supported chipsets: RT2800U.
 */

#ifndef RT2800USB_H
#define RT2800USB_H

/*
 * USB registers.
 */

/*
 * USB_DMA_CFG
 * RX_BULK_AGG_TIMEOUT: Rx Bulk Aggregation TimeOut in unit of 33ns.
 * RX_BULK_AGG_LIMIT: Rx Bulk Aggregation Limit in unit of 256 bytes.
 * PHY_CLEAR: phy watch dog enable.
 * TX_CLEAR: Clear USB DMA TX path.
 * TXOP_HALT: Halt TXOP count down when TX buffer is full.
 * RX_BULK_AGG_EN: Enable Rx Bulk Aggregation.
 * RX_BULK_EN: Enable USB DMA Rx.
 * TX_BULK_EN: Enable USB DMA Tx.
 * EP_OUT_VALID: OUT endpoint data valid.
 * RX_BUSY: USB DMA RX FSM busy.
 * TX_BUSY: USB DMA TX FSM busy.
 */
#define USB_DMA_CFG			0x02a0
#define USB_DMA_CFG_RX_BULK_AGG_TIMEOUT	FIELD32(0x000000ff)
#define USB_DMA_CFG_RX_BULK_AGG_LIMIT	FIELD32(0x0000ff00)
#define USB_DMA_CFG_PHY_CLEAR		FIELD32(0x00010000)
#define USB_DMA_CFG_TX_CLEAR		FIELD32(0x00080000)
#define USB_DMA_CFG_TXOP_HALT		FIELD32(0x00100000)
#define USB_DMA_CFG_RX_BULK_AGG_EN	FIELD32(0x00200000)
#define USB_DMA_CFG_RX_BULK_EN		FIELD32(0x00400000)
#define USB_DMA_CFG_TX_BULK_EN		FIELD32(0x00800000)
#define USB_DMA_CFG_EP_OUT_VALID	FIELD32(0x3f000000)
#define USB_DMA_CFG_RX_BUSY		FIELD32(0x40000000)
#define USB_DMA_CFG_TX_BUSY		FIELD32(0x80000000)

/*
 * USB_CYC_CFG
 */
#define USB_CYC_CFG			0x02a4
#define USB_CYC_CFG_CLOCK_CYCLE		FIELD32(0x000000ff)

/*
 * 8051 firmware image.
 */
#define FIRMWARE_RT2870			"rt2870.bin"
#define FIRMWARE_IMAGE_BASE		0x3000

/*
 * DMA descriptor defines.
 */
#define TXINFO_DESC_SIZE		( 1 * sizeof(__le32) )
#define RXINFO_DESC_SIZE		( 1 * sizeof(__le32) )
#define RXWI_DESC_SIZE			( 4 * sizeof(__le32) )
#define RXD_DESC_SIZE			( 1 * sizeof(__le32) )

/*
 * TX Info structure
 */

/*
 * Word0
 * WIV: Wireless Info Valid. 1: Driver filled WI,  0: DMA needs to copy WI
 * QSEL: Select on-chip FIFO ID for 2nd-stage output scheduler.
 *       0:MGMT, 1:HCCA 2:EDCA
 * USB_DMA_NEXT_VALID: Used ONLY in USB bulk Aggregation, NextValid
 * DMA_TX_BURST: used ONLY in USB bulk Aggregation.
 *               Force USB DMA transmit frame from current selected endpoint
 */
#define TXINFO_W0_USB_DMA_TX_PKT_LEN	FIELD32(0x0000ffff)
#define TXINFO_W0_WIV			FIELD32(0x01000000)
#define TXINFO_W0_QSEL			FIELD32(0x06000000)
#define TXINFO_W0_SW_USE_LAST_ROUND	FIELD32(0x08000000)
#define TXINFO_W0_USB_DMA_NEXT_VALID	FIELD32(0x40000000)
#define TXINFO_W0_USB_DMA_TX_BURST	FIELD32(0x80000000)

/*
 * RX Info structure
 */

/*
 * Word 0
 */

#define RXINFO_W0_USB_DMA_RX_PKT_LEN	FIELD32(0x0000ffff)

/*
 * RX WI structure
 */

/*
 * Word0
 */
#define RXWI_W0_WIRELESS_CLI_ID		FIELD32(0x000000ff)
#define RXWI_W0_KEY_INDEX		FIELD32(0x00000300)
#define RXWI_W0_BSSID			FIELD32(0x00001c00)
#define RXWI_W0_UDF			FIELD32(0x0000e000)
#define RXWI_W0_MPDU_TOTAL_BYTE_COUNT	FIELD32(0x0fff0000)
#define RXWI_W0_TID			FIELD32(0xf0000000)

/*
 * Word1
 */
#define RXWI_W1_FRAG			FIELD32(0x0000000f)
#define RXWI_W1_SEQUENCE		FIELD32(0x0000fff0)
#define RXWI_W1_MCS			FIELD32(0x007f0000)
#define RXWI_W1_BW			FIELD32(0x00800000)
#define RXWI_W1_SHORT_GI		FIELD32(0x01000000)
#define RXWI_W1_STBC			FIELD32(0x06000000)
#define RXWI_W1_PHYMODE			FIELD32(0xc0000000)

/*
 * Word2
 */
#define RXWI_W2_RSSI0			FIELD32(0x000000ff)
#define RXWI_W2_RSSI1			FIELD32(0x0000ff00)
#define RXWI_W2_RSSI2			FIELD32(0x00ff0000)

/*
 * Word3
 */
#define RXWI_W3_SNR0			FIELD32(0x000000ff)
#define RXWI_W3_SNR1			FIELD32(0x0000ff00)

/*
 * RX descriptor format for RX Ring.
 */

/*
 * Word0
 * UNICAST_TO_ME: This RX frame is unicast to me.
 * MULTICAST: This is a multicast frame.
 * BROADCAST: This is a broadcast frame.
 * MY_BSS: this frame belongs to the same BSSID.
 * CRC_ERROR: CRC error.
 * CIPHER_ERROR: 0: decryption okay, 1:ICV error, 2:MIC error, 3:KEY not valid.
 * AMSDU: rx with 802.3 header, not 802.11 header.
 */

#define RXD_W0_BA			FIELD32(0x00000001)
#define RXD_W0_DATA			FIELD32(0x00000002)
#define RXD_W0_NULLDATA			FIELD32(0x00000004)
#define RXD_W0_FRAG			FIELD32(0x00000008)
#define RXD_W0_UNICAST_TO_ME		FIELD32(0x00000010)
#define RXD_W0_MULTICAST		FIELD32(0x00000020)
#define RXD_W0_BROADCAST		FIELD32(0x00000040)
#define RXD_W0_MY_BSS			FIELD32(0x00000080)
#define RXD_W0_CRC_ERROR		FIELD32(0x00000100)
#define RXD_W0_CIPHER_ERROR		FIELD32(0x00000600)
#define RXD_W0_AMSDU			FIELD32(0x00000800)
#define RXD_W0_HTC			FIELD32(0x00001000)
#define RXD_W0_RSSI			FIELD32(0x00002000)
#define RXD_W0_L2PAD			FIELD32(0x00004000)
#define RXD_W0_AMPDU			FIELD32(0x00008000)
#define RXD_W0_DECRYPTED		FIELD32(0x00010000)
#define RXD_W0_PLCP_RSSI		FIELD32(0x00020000)
#define RXD_W0_CIPHER_ALG		FIELD32(0x00040000)
#define RXD_W0_LAST_AMSDU		FIELD32(0x00080000)
#define RXD_W0_PLCP_SIGNAL		FIELD32(0xfff00000)

#endif /* RT2800USB_H */
