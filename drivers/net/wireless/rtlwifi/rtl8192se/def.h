/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __REALTEK_92S_DEF_H__
#define __REALTEK_92S_DEF_H__

#define RX_MPDU_QUEUE				0
#define RX_CMD_QUEUE				1
#define RX_MAX_QUEUE				2

#define SHORT_SLOT_TIME				9
#define NON_SHORT_SLOT_TIME			20

/* Rx smooth factor */
#define	RX_SMOOTH_FACTOR			20

/* Queue Select Value in TxDesc */
#define QSLT_BK					0x2
#define QSLT_BE					0x0
#define QSLT_VI					0x5
#define QSLT_VO					0x6
#define QSLT_BEACON				0x10
#define QSLT_HIGH				0x11
#define QSLT_MGNT				0x12
#define QSLT_CMD				0x13

#define	PHY_RSSI_SLID_WIN_MAX			100
#define	PHY_LINKQUALITY_SLID_WIN_MAX		20
#define	PHY_BEACON_RSSI_SLID_WIN_MAX		10

/* Tx Desc */
#define TX_DESC_SIZE_RTL8192S			(16 * 4)
#define TX_CMDDESC_SIZE_RTL8192S		(16 * 4)

/* Define a macro that takes a le32 word, converts it to host ordering,
 * right shifts by a specified count, creates a mask of the specified
 * bit count, and extracts that number of bits.
 */

#define SHIFT_AND_MASK_LE(__pdesc, __shift, __mask)		\
	((le32_to_cpu(*(((__le32 *)(__pdesc)))) >> (__shift)) &	\
	BIT_LEN_MASK_32(__mask))

/* Define a macro that clears a bit field in an le32 word and
 * sets the specified value into that bit field. The resulting
 * value remains in le32 ordering; however, it is properly converted
 * to host ordering for the clear and set operations before conversion
 * back to le32.
 */

#define SET_BITS_OFFSET_LE(__pdesc, __shift, __len, __val)	\
	(*(__le32 *)(__pdesc) =					\
	(cpu_to_le32((le32_to_cpu(*((__le32 *)(__pdesc))) &	\
	(~(BIT_OFFSET_LEN_MASK_32((__shift), __len)))) |	\
	(((u32)(__val) & BIT_LEN_MASK_32(__len)) << (__shift)))));

/* macros to read/write various fields in RX or TX descriptors */

/* Dword 0 */
#define SET_TX_DESC_PKT_SIZE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 0, 16, __val)
#define SET_TX_DESC_OFFSET(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 16, 8, __val)
#define SET_TX_DESC_TYPE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 24, 2, __val)
#define SET_TX_DESC_LAST_SEG(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 26, 1, __val)
#define SET_TX_DESC_FIRST_SEG(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 27, 1, __val)
#define SET_TX_DESC_LINIP(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 28, 1, __val)
#define SET_TX_DESC_AMSDU(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 29, 1, __val)
#define SET_TX_DESC_GREEN_FIELD(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 30, 1, __val)
#define SET_TX_DESC_OWN(__pdesc, __val)				\
	SET_BITS_OFFSET_LE(__pdesc, 31, 1, __val)

#define GET_TX_DESC_OWN(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc, 31, 1)

/* Dword 1 */
#define SET_TX_DESC_MACID(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 0, 5, __val)
#define SET_TX_DESC_MORE_DATA(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 5, 1, __val)
#define SET_TX_DESC_MORE_FRAG(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 6, 1, __val)
#define SET_TX_DESC_PIFS(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 7, 1, __val)
#define SET_TX_DESC_QUEUE_SEL(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 8, 5, __val)
#define SET_TX_DESC_ACK_POLICY(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 13, 2, __val)
#define SET_TX_DESC_NO_ACM(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 15, 1, __val)
#define SET_TX_DESC_NON_QOS(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 16, 1, __val)
#define SET_TX_DESC_KEY_ID(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 17, 2, __val)
#define SET_TX_DESC_OUI(__pdesc, __val)				\
	SET_BITS_OFFSET_LE(__pdesc + 4, 19, 1, __val)
#define SET_TX_DESC_PKT_TYPE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 20, 1, __val)
#define SET_TX_DESC_EN_DESC_ID(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 21, 1, __val)
#define SET_TX_DESC_SEC_TYPE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 22, 2, __val)
#define SET_TX_DESC_WDS(__pdesc, __val)				\
	SET_BITS_OFFSET_LE(__pdesc + 4, 24, 1, __val)
#define SET_TX_DESC_HTC(__pdesc, __val)				\
	SET_BITS_OFFSET_LE(__pdesc + 4, 25, 1, __val)
#define SET_TX_DESC_PKT_OFFSET(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 26, 5, __val)
#define SET_TX_DESC_HWPC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 27, 1, __val)

/* Dword 2 */
#define SET_TX_DESC_DATA_RETRY_LIMIT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 8, 0, 6, __val)
#define SET_TX_DESC_RETRY_LIMIT_ENABLE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 8, 6, 1, __val)
#define SET_TX_DESC_TSFL(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 8, 7, 5, __val)
#define SET_TX_DESC_RTS_RETRY_COUNT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 8, 12, 6, __val)
#define SET_TX_DESC_DATA_RETRY_COUNT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 8, 18, 6, __val)
#define	SET_TX_DESC_RSVD_MACID(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(((__pdesc) + 8), 24, 5, __val)
#define SET_TX_DESC_AGG_ENABLE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 8, 29, 1, __val)
#define SET_TX_DESC_AGG_BREAK(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 8, 30, 1, __val)
#define SET_TX_DESC_OWN_MAC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 8, 31, 1, __val)

/* Dword 3 */
#define SET_TX_DESC_NEXT_HEAP_PAGE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 0, 8, __val)
#define SET_TX_DESC_TAIL_PAGE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 12, 8, 8, __val)
#define SET_TX_DESC_SEQ(__pdesc, __val)				\
	SET_BITS_OFFSET_LE(__pdesc + 12, 16, 12, __val)
#define SET_TX_DESC_FRAG(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 12, 28, 4, __val)

/* Dword 4 */
#define SET_TX_DESC_RTS_RATE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 0, 6, __val)
#define SET_TX_DESC_DISABLE_RTS_FB(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 16, 6, 1, __val)
#define SET_TX_DESC_RTS_RATE_FB_LIMIT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 16, 7, 4, __val)
#define SET_TX_DESC_CTS_ENABLE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 11, 1, __val)
#define SET_TX_DESC_RTS_ENABLE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 12, 1, __val)
#define SET_TX_DESC_RA_BRSR_ID(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 13, 3, __val)
#define SET_TX_DESC_TXHT(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 16, 1, __val)
#define SET_TX_DESC_TX_SHORT(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 17, 1, __val)
#define SET_TX_DESC_TX_BANDWIDTH(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 16, 18, 1, __val)
#define SET_TX_DESC_TX_SUB_CARRIER(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 16, 19, 2, __val)
#define SET_TX_DESC_TX_STBC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 21, 2, __val)
#define SET_TX_DESC_TX_REVERSE_DIRECTION(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc + 16, 23, 1, __val)
#define SET_TX_DESC_RTS_HT(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 24, 1, __val)
#define SET_TX_DESC_RTS_SHORT(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 25, 1, __val)
#define SET_TX_DESC_RTS_BANDWIDTH(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 16, 26, 1, __val)
#define SET_TX_DESC_RTS_SUB_CARRIER(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 16, 27, 2, __val)
#define SET_TX_DESC_RTS_STBC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 29, 2, __val)
#define SET_TX_DESC_USER_RATE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 31, 1, __val)

/* Dword 5 */
#define SET_TX_DESC_PACKET_ID(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 20, 0, 9, __val)
#define SET_TX_DESC_TX_RATE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 20, 9, 6, __val)
#define SET_TX_DESC_DISABLE_FB(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 20, 15, 1, __val)
#define SET_TX_DESC_DATA_RATE_FB_LIMIT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 20, 16, 5, __val)
#define SET_TX_DESC_TX_AGC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 20, 21, 11, __val)

/* Dword 6 */
#define SET_TX_DESC_IP_CHECK_SUM(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 24, 0, 16, __val)
#define SET_TX_DESC_TCP_CHECK_SUM(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 24, 16, 16, __val)

/* Dword 7 */
#define SET_TX_DESC_TX_BUFFER_SIZE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 28, 0, 16, __val)
#define SET_TX_DESC_IP_HEADER_OFFSET(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 28, 16, 8, __val)
#define SET_TX_DESC_TCP_ENABLE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 28, 31, 1, __val)

/* Dword 8 */
#define SET_TX_DESC_TX_BUFFER_ADDRESS(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 32, 0, 32, __val)
#define GET_TX_DESC_TX_BUFFER_ADDRESS(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 32, 0, 32)

/* Dword 9 */
#define SET_TX_DESC_NEXT_DESC_ADDRESS(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 36, 0, 32, __val)

/* Because the PCI Tx descriptors are chaied at the
 * initialization and all the NextDescAddresses in
 * these descriptors cannot not be cleared (,or
 * driver/HW cannot find the next descriptor), the
 * offset 36 (NextDescAddresses) is reserved when
 * the desc is cleared. */
#define	TX_DESC_NEXT_DESC_OFFSET			36
#define CLEAR_PCI_TX_DESC_CONTENT(__pdesc, _size)		\
	memset(__pdesc, 0, min_t(size_t, _size, TX_DESC_NEXT_DESC_OFFSET))

/* Rx Desc */
#define RX_STATUS_DESC_SIZE				24
#define RX_DRV_INFO_SIZE_UNIT				8

/* DWORD 0 */
#define SET_RX_STATUS_DESC_PKT_LEN(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 0, 14, __val)
#define SET_RX_STATUS_DESC_CRC32(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 14, 1, __val)
#define SET_RX_STATUS_DESC_ICV(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 15, 1, __val)
#define SET_RX_STATUS_DESC_DRVINFO_SIZE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 16, 4, __val)
#define SET_RX_STATUS_DESC_SECURITY(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 20, 3, __val)
#define SET_RX_STATUS_DESC_QOS(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 23, 1, __val)
#define SET_RX_STATUS_DESC_SHIFT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 24, 2, __val)
#define SET_RX_STATUS_DESC_PHY_STATUS(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 26, 1, __val)
#define SET_RX_STATUS_DESC_SWDEC(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 27, 1, __val)
#define SET_RX_STATUS_DESC_LAST_SEG(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 28, 1, __val)
#define SET_RX_STATUS_DESC_FIRST_SEG(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 29, 1, __val)
#define SET_RX_STATUS_DESC_EOR(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 30, 1, __val)
#define SET_RX_STATUS_DESC_OWN(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 31, 1, __val)

#define GET_RX_STATUS_DESC_PKT_LEN(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 0, 14)
#define GET_RX_STATUS_DESC_CRC32(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 14, 1)
#define GET_RX_STATUS_DESC_ICV(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc, 15, 1)
#define GET_RX_STATUS_DESC_DRVINFO_SIZE(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc, 16, 4)
#define GET_RX_STATUS_DESC_SECURITY(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 20, 3)
#define GET_RX_STATUS_DESC_QOS(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc, 23, 1)
#define GET_RX_STATUS_DESC_SHIFT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 24, 2)
#define GET_RX_STATUS_DESC_PHY_STATUS(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 26, 1)
#define GET_RX_STATUS_DESC_SWDEC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 27, 1)
#define GET_RX_STATUS_DESC_LAST_SEG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 28, 1)
#define GET_RX_STATUS_DESC_FIRST_SEG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 29, 1)
#define GET_RX_STATUS_DESC_EOR(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc, 30, 1)
#define GET_RX_STATUS_DESC_OWN(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc, 31, 1)

/* DWORD 1 */
#define SET_RX_STATUS_DESC_MACID(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 4, 0, 5, __val)
#define SET_RX_STATUS_DESC_TID(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 5, 4, __val)
#define SET_RX_STATUS_DESC_PAGGR(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 4, 14, 1, __val)
#define SET_RX_STATUS_DESC_FAGGR(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 4, 15, 1, __val)
#define SET_RX_STATUS_DESC_A1_FIT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 4, 16, 4, __val)
#define SET_RX_STATUS_DESC_A2_FIT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 4, 20, 4, __val)
#define SET_RX_STATUS_DESC_PAM(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 24, 1, __val)
#define SET_RX_STATUS_DESC_PWR(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 25, 1, __val)
#define SET_RX_STATUS_DESC_MOREDATA(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 4, 26, 1, __val)
#define SET_RX_STATUS_DESC_MOREFRAG(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 4, 27, 1, __val)
#define SET_RX_STATUS_DESC_TYPE(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 28, 2, __val)
#define SET_RX_STATUS_DESC_MC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 30, 1, __val)
#define SET_RX_STATUS_DESC_BC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 4, 31, 1, __val)

#define GET_RX_STATUS_DEC_MACID(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 4, 0, 5)
#define GET_RX_STATUS_DESC_TID(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 4, 5, 4)
#define GET_RX_STATUS_DESC_PAGGR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 4, 14, 1)
#define GET_RX_STATUS_DESC_FAGGR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 4, 15, 1)
#define GET_RX_STATUS_DESC_A1_FIT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 4, 16, 4)
#define GET_RX_STATUS_DESC_A2_FIT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 4, 20, 4)
#define GET_RX_STATUS_DESC_PAM(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 4, 24, 1)
#define GET_RX_STATUS_DESC_PWR(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 4, 25, 1)
#define GET_RX_STATUS_DESC_MORE_DATA(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 4, 26, 1)
#define GET_RX_STATUS_DESC_MORE_FRAG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 4, 27, 1)
#define GET_RX_STATUS_DESC_TYPE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 4, 28, 2)
#define GET_RX_STATUS_DESC_MC(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 4, 30, 1)
#define GET_RX_STATUS_DESC_BC(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 4, 31, 1)

/* DWORD 2 */
#define SET_RX_STATUS_DESC_SEQ(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 8, 0, 12, __val)
#define SET_RX_STATUS_DESC_FRAG(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 8, 12, 4, __val)
#define SET_RX_STATUS_DESC_NEXT_PKTLEN(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 8, 16, 8, __val)
#define SET_RX_STATUS_DESC_NEXT_IND(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 8, 30, 1, __val)

#define GET_RX_STATUS_DESC_SEQ(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 8, 0, 12)
#define GET_RX_STATUS_DESC_FRAG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 8, 12, 4)
#define GET_RX_STATUS_DESC_NEXT_PKTLEN(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 8, 16, 8)
#define GET_RX_STATUS_DESC_NEXT_IND(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 8, 30, 1)

/* DWORD 3 */
#define SET_RX_STATUS_DESC_RX_MCS(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 0, 6, __val)
#define SET_RX_STATUS_DESC_RX_HT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 6, 1, __val)
#define SET_RX_STATUS_DESC_AMSDU(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 7, 1, __val)
#define SET_RX_STATUS_DESC_SPLCP(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 8, 1, __val)
#define SET_RX_STATUS_DESC_BW(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 12, 9, 1, __val)
#define SET_RX_STATUS_DESC_HTC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 12, 10, 1, __val)
#define SET_RX_STATUS_DESC_TCP_CHK_RPT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 11, 1, __val)
#define SET_RX_STATUS_DESC_IP_CHK_RPT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 12, 1, __val)
#define SET_RX_STATUS_DESC_TCP_CHK_VALID(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc + 12, 13, 1, __val)
#define SET_RX_STATUS_DESC_HWPC_ERR(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 14, 1, __val)
#define SET_RX_STATUS_DESC_HWPC_IND(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc + 12, 15, 1, __val)
#define SET_RX_STATUS_DESC_IV0(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 12, 16, 16, __val)

#define GET_RX_STATUS_DESC_RX_MCS(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 12, 0, 6)
#define GET_RX_STATUS_DESC_RX_HT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 12, 6, 1)
#define GET_RX_STATUS_DESC_AMSDU(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 12, 7, 1)
#define GET_RX_STATUS_DESC_SPLCP(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 12, 8, 1)
#define GET_RX_STATUS_DESC_BW(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 12, 9, 1)
#define GET_RX_STATUS_DESC_HTC(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 12, 10, 1)
#define GET_RX_STATUS_DESC_TCP_CHK_RPT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 12, 11, 1)
#define GET_RX_STATUS_DESC_IP_CHK_RPT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 12, 12, 1)
#define GET_RX_STATUS_DESC_TCP_CHK_VALID(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc + 12, 13, 1)
#define GET_RX_STATUS_DESC_HWPC_ERR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 12, 14, 1)
#define GET_RX_STATUS_DESC_HWPC_IND(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 12, 15, 1)
#define GET_RX_STATUS_DESC_IV0(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 12, 16, 16)

/* DWORD 4 */
#define SET_RX_STATUS_DESC_IV1(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 16, 0, 32, __val)
#define GET_RX_STATUS_DESC_IV1(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc + 16, 0, 32)

/* DWORD 5 */
#define SET_RX_STATUS_DESC_TSFL(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc + 20, 0, 32, __val)
#define GET_RX_STATUS_DESC_TSFL(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 20, 0, 32)

/* DWORD 6 */
#define SET_RX_STATUS__DESC_BUFF_ADDR(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc + 24, 0, 32, __val)

#define SE_RX_HAL_IS_CCK_RATE(_pdesc)\
	(GET_RX_STATUS_DESC_RX_MCS(_pdesc) == DESC92_RATE1M ||	\
	 GET_RX_STATUS_DESC_RX_MCS(_pdesc) == DESC92_RATE2M ||	\
	 GET_RX_STATUS_DESC_RX_MCS(_pdesc) == DESC92_RATE5_5M ||\
	 GET_RX_STATUS_DESC_RX_MCS(_pdesc) == DESC92_RATE11M)

enum rf_optype {
	RF_OP_BY_SW_3WIRE = 0,
	RF_OP_BY_FW,
	RF_OP_MAX
};

enum ic_inferiority {
	IC_INFERIORITY_A = 0,
	IC_INFERIORITY_B = 1,
};

enum fwcmd_iotype {
	/* For DIG DM */
	FW_CMD_DIG_ENABLE = 0,
	FW_CMD_DIG_DISABLE = 1,
	FW_CMD_DIG_HALT = 2,
	FW_CMD_DIG_RESUME = 3,
	/* For High Power DM */
	FW_CMD_HIGH_PWR_ENABLE = 4,
	FW_CMD_HIGH_PWR_DISABLE = 5,
	/* For Rate adaptive DM */
	FW_CMD_RA_RESET = 6,
	FW_CMD_RA_ACTIVE = 7,
	FW_CMD_RA_REFRESH_N = 8,
	FW_CMD_RA_REFRESH_BG = 9,
	FW_CMD_RA_INIT = 10,
	/* For FW supported IQK */
	FW_CMD_IQK_INIT = 11,
	/* Tx power tracking switch,
	 * MP driver only */
	FW_CMD_TXPWR_TRACK_ENABLE = 12,
	/* Tx power tracking switch,
	 * MP driver only */
	FW_CMD_TXPWR_TRACK_DISABLE = 13,
	/* Tx power tracking with thermal
	 * indication, for Normal driver */
	FW_CMD_TXPWR_TRACK_THERMAL = 14,
	FW_CMD_PAUSE_DM_BY_SCAN = 15,
	FW_CMD_RESUME_DM_BY_SCAN = 16,
	FW_CMD_RA_REFRESH_N_COMB = 17,
	FW_CMD_RA_REFRESH_BG_COMB = 18,
	FW_CMD_ANTENNA_SW_ENABLE = 19,
	FW_CMD_ANTENNA_SW_DISABLE = 20,
	/* Tx Status report for CCX from FW */
	FW_CMD_TX_FEEDBACK_CCX_ENABLE = 21,
	/* Indifate firmware that driver
	 * enters LPS, For PS-Poll issue */
	FW_CMD_LPS_ENTER = 22,
	/* Indicate firmware that driver
	 * leave LPS*/
	FW_CMD_LPS_LEAVE = 23,
	/* Set DIG mode to signal strength */
	FW_CMD_DIG_MODE_SS = 24,
	/* Set DIG mode to false alarm. */
	FW_CMD_DIG_MODE_FA = 25,
	FW_CMD_ADD_A2_ENTRY = 26,
	FW_CMD_CTRL_DM_BY_DRIVER = 27,
	FW_CMD_CTRL_DM_BY_DRIVER_NEW = 28,
	FW_CMD_PAPE_CONTROL = 29,
	FW_CMD_IQK_ENABLE = 30,
};

/*
 * Driver info contain PHY status
 * and other variabel size info
 * PHY Status content as below
 */
struct  rx_fwinfo {
	/* DWORD 0 */
	u8 gain_trsw[4];
	/* DWORD 1 */
	u8 pwdb_all;
	u8 cfosho[4];
	/* DWORD 2 */
	u8 cfotail[4];
	/* DWORD 3 */
	s8 rxevm[2];
	s8 rxsnr[4];
	/* DWORD 4 */
	u8 pdsnr[2];
	/* DWORD 5 */
	u8 csi_current[2];
	u8 csi_target[2];
	/* DWORD 6 */
	u8 sigevm;
	u8 max_ex_pwr;
	u8 ex_intf_flag:1;
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 reserve:4;
};

struct phy_sts_cck_8192s_t {
	u8 adc_pwdb_x[4];
	u8 sq_rpt;
	u8 cck_agc_rpt;
};

#endif

