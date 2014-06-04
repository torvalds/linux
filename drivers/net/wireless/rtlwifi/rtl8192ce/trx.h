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

#ifndef __RTL92CE_TRX_H__
#define __RTL92CE_TRX_H__

#define TX_DESC_SIZE				64
#define TX_DESC_AGGR_SUBFRAME_SIZE		32

#define RX_DESC_SIZE				32
#define RX_DRV_INFO_SIZE_UNIT			8

#define	TX_DESC_NEXT_DESC_OFFSET		40
#define USB_HWDESC_HEADER_LEN			32
#define CRCLENGTH				4

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
	(*(__le32 *)(__pdesc) = 				\
	(cpu_to_le32((le32_to_cpu(*((__le32 *)(__pdesc))) &	\
	(~(BIT_OFFSET_LEN_MASK_32((__shift), __len)))) |		\
	(((u32)(__val) & BIT_LEN_MASK_32(__len)) << (__shift)))));

/* macros to read/write various fields in RX or TX descriptors */

#define SET_TX_DESC_PKT_SIZE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 0, 16, __val)
#define SET_TX_DESC_OFFSET(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 16, 8, __val)
#define SET_TX_DESC_BMC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 24, 1, __val)
#define SET_TX_DESC_HTC(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 25, 1, __val)
#define SET_TX_DESC_LAST_SEG(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 26, 1, __val)
#define SET_TX_DESC_FIRST_SEG(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 27, 1, __val)
#define SET_TX_DESC_LINIP(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 28, 1, __val)
#define SET_TX_DESC_NO_ACM(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 29, 1, __val)
#define SET_TX_DESC_GF(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 30, 1, __val)
#define SET_TX_DESC_OWN(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 31, 1, __val)

#define GET_TX_DESC_PKT_SIZE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 0, 16)
#define GET_TX_DESC_OFFSET(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 16, 8)
#define GET_TX_DESC_BMC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 24, 1)
#define GET_TX_DESC_HTC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 25, 1)
#define GET_TX_DESC_LAST_SEG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 26, 1)
#define GET_TX_DESC_FIRST_SEG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 27, 1)
#define GET_TX_DESC_LINIP(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 28, 1)
#define GET_TX_DESC_NO_ACM(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 29, 1)
#define GET_TX_DESC_GF(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc, 30, 1)
#define GET_TX_DESC_OWN(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 31, 1)

#define SET_TX_DESC_MACID(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 0, 5, __val)
#define SET_TX_DESC_AGG_BREAK(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 5, 1, __val)
#define SET_TX_DESC_BK(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc+4, 6, 1, __val)
#define SET_TX_DESC_RDG_ENABLE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 7, 1, __val)
#define SET_TX_DESC_QUEUE_SEL(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 8, 5, __val)
#define SET_TX_DESC_RDG_NAV_EXT(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+4, 13, 1, __val)
#define SET_TX_DESC_LSIG_TXOP_EN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+4, 14, 1, __val)
#define SET_TX_DESC_PIFS(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 15, 1, __val)
#define SET_TX_DESC_RATE_ID(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 16, 4, __val)
#define SET_TX_DESC_NAV_USE_HDR(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+4, 20, 1, __val)
#define SET_TX_DESC_EN_DESC_ID(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 21, 1, __val)
#define SET_TX_DESC_SEC_TYPE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 22, 2, __val)
#define SET_TX_DESC_PKT_OFFSET(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+4, 24, 8, __val)

#define GET_TX_DESC_MACID(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 0, 5)
#define GET_TX_DESC_AGG_ENABLE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 5, 1)
#define GET_TX_DESC_AGG_BREAK(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 6, 1)
#define GET_TX_DESC_RDG_ENABLE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 7, 1)
#define GET_TX_DESC_QUEUE_SEL(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 8, 5)
#define GET_TX_DESC_RDG_NAV_EXT(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+4, 13, 1)
#define GET_TX_DESC_LSIG_TXOP_EN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+4, 14, 1)
#define GET_TX_DESC_PIFS(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 15, 1)
#define GET_TX_DESC_RATE_ID(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 16, 4)
#define GET_TX_DESC_NAV_USE_HDR(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+4, 20, 1)
#define GET_TX_DESC_EN_DESC_ID(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 21, 1)
#define GET_TX_DESC_SEC_TYPE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 22, 2)
#define GET_TX_DESC_PKT_OFFSET(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 24, 8)

#define SET_TX_DESC_RTS_RC(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 0, 6, __val)
#define SET_TX_DESC_DATA_RC(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 6, 6, __val)
#define SET_TX_DESC_BAR_RTY_TH(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 14, 2, __val)
#define SET_TX_DESC_MORE_FRAG(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 17, 1, __val)
#define SET_TX_DESC_RAW(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc+8, 18, 1, __val)
#define SET_TX_DESC_CCX(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc+8, 19, 1, __val)
#define SET_TX_DESC_AMPDU_DENSITY(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+8, 20, 3, __val)
#define SET_TX_DESC_ANTSEL_A(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 24, 1, __val)
#define SET_TX_DESC_ANTSEL_B(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 25, 1, __val)
#define SET_TX_DESC_TX_ANT_CCK(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 26, 2, __val)
#define SET_TX_DESC_TX_ANTL(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 28, 2, __val)
#define SET_TX_DESC_TX_ANT_HT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+8, 30, 2, __val)

#define GET_TX_DESC_RTS_RC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 0, 6)
#define GET_TX_DESC_DATA_RC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 6, 6)
#define GET_TX_DESC_BAR_RTY_TH(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 14, 2)
#define GET_TX_DESC_MORE_FRAG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 17, 1)
#define GET_TX_DESC_RAW(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 18, 1)
#define GET_TX_DESC_CCX(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 19, 1)
#define GET_TX_DESC_AMPDU_DENSITY(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+8, 20, 3)
#define GET_TX_DESC_ANTSEL_A(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 24, 1)
#define GET_TX_DESC_ANTSEL_B(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 25, 1)
#define GET_TX_DESC_TX_ANT_CCK(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 26, 2)
#define GET_TX_DESC_TX_ANTL(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 28, 2)
#define GET_TX_DESC_TX_ANT_HT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 30, 2)

#define SET_TX_DESC_NEXT_HEAP_PAGE(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+12, 0, 8, __val)
#define SET_TX_DESC_TAIL_PAGE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+12, 8, 8, __val)
#define SET_TX_DESC_SEQ(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc+12, 16, 12, __val)
#define SET_TX_DESC_PKT_ID(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+12, 28, 4, __val)

#define GET_TX_DESC_NEXT_HEAP_PAGE(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+12, 0, 8)
#define GET_TX_DESC_TAIL_PAGE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 8, 8)
#define GET_TX_DESC_SEQ(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 16, 12)
#define GET_TX_DESC_PKT_ID(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 28, 4)

#define SET_TX_DESC_RTS_RATE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 0, 5, __val)
#define SET_TX_DESC_AP_DCFE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 5, 1, __val)
#define SET_TX_DESC_QOS(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc+16, 6, 1, __val)
#define SET_TX_DESC_HWSEQ_EN(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 7, 1, __val)
#define SET_TX_DESC_USE_RATE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 8, 1, __val)
#define SET_TX_DESC_DISABLE_RTS_FB(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+16, 9, 1, __val)
#define SET_TX_DESC_DISABLE_FB(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 10, 1, __val)
#define SET_TX_DESC_CTS2SELF(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 11, 1, __val)
#define SET_TX_DESC_RTS_ENABLE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 12, 1, __val)
#define SET_TX_DESC_HW_RTS_ENABLE(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+16, 13, 1, __val)
#define SET_TX_DESC_PORT_ID(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 14, 1, __val)
#define SET_TX_DESC_WAIT_DCTS(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 18, 1, __val)
#define SET_TX_DESC_CTS2AP_EN(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 19, 1, __val)
#define SET_TX_DESC_TX_SUB_CARRIER(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+16, 20, 2, __val)
#define SET_TX_DESC_TX_STBC(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 22, 2, __val)
#define SET_TX_DESC_DATA_SHORT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 24, 1, __val)
#define SET_TX_DESC_DATA_BW(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 25, 1, __val)
#define SET_TX_DESC_RTS_SHORT(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 26, 1, __val)
#define SET_TX_DESC_RTS_BW(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 27, 1, __val)
#define SET_TX_DESC_RTS_SC(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 28, 2, __val)
#define SET_TX_DESC_RTS_STBC(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+16, 30, 2, __val)

#define GET_TX_DESC_RTS_RATE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 0, 5)
#define GET_TX_DESC_AP_DCFE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 5, 1)
#define GET_TX_DESC_QOS(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 6, 1)
#define GET_TX_DESC_HWSEQ_EN(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 7, 1)
#define GET_TX_DESC_USE_RATE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 8, 1)
#define GET_TX_DESC_DISABLE_RTS_FB(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+16, 9, 1)
#define GET_TX_DESC_DISABLE_FB(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 10, 1)
#define GET_TX_DESC_CTS2SELF(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 11, 1)
#define GET_TX_DESC_RTS_ENABLE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 12, 1)
#define GET_TX_DESC_HW_RTS_ENABLE(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+16, 13, 1)
#define GET_TX_DESC_PORT_ID(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 14, 1)
#define GET_TX_DESC_WAIT_DCTS(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 18, 1)
#define GET_TX_DESC_CTS2AP_EN(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 19, 1)
#define GET_TX_DESC_TX_SUB_CARRIER(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+16, 20, 2)
#define GET_TX_DESC_TX_STBC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 22, 2)
#define GET_TX_DESC_DATA_SHORT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 24, 1)
#define GET_TX_DESC_DATA_BW(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 25, 1)
#define GET_TX_DESC_RTS_SHORT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 26, 1)
#define GET_TX_DESC_RTS_BW(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 27, 1)
#define GET_TX_DESC_RTS_SC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 28, 2)
#define GET_TX_DESC_RTS_STBC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 30, 2)

#define SET_TX_DESC_TX_RATE(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+20, 0, 6, __val)
#define SET_TX_DESC_DATA_SHORTGI(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+20, 6, 1, __val)
#define SET_TX_DESC_CCX_TAG(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+20, 7, 1, __val)
#define SET_TX_DESC_DATA_RATE_FB_LIMIT(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+20, 8, 5, __val)
#define SET_TX_DESC_RTS_RATE_FB_LIMIT(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+20, 13, 4, __val)
#define SET_TX_DESC_RETRY_LIMIT_ENABLE(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+20, 17, 1, __val)
#define SET_TX_DESC_DATA_RETRY_LIMIT(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+20, 18, 6, __val)
#define SET_TX_DESC_USB_TXAGG_NUM(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+20, 24, 8, __val)

#define GET_TX_DESC_TX_RATE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+20, 0, 6)
#define GET_TX_DESC_DATA_SHORTGI(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+20, 6, 1)
#define GET_TX_DESC_CCX_TAG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+20, 7, 1)
#define GET_TX_DESC_DATA_RATE_FB_LIMIT(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+20, 8, 5)
#define GET_TX_DESC_RTS_RATE_FB_LIMIT(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+20, 13, 4)
#define GET_TX_DESC_RETRY_LIMIT_ENABLE(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+20, 17, 1)
#define GET_TX_DESC_DATA_RETRY_LIMIT(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+20, 18, 6)
#define GET_TX_DESC_USB_TXAGG_NUM(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+20, 24, 8)

#define SET_TX_DESC_TXAGC_A(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+24, 0, 5, __val)
#define SET_TX_DESC_TXAGC_B(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+24, 5, 5, __val)
#define SET_TX_DESC_USE_MAX_LEN(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+24, 10, 1, __val)
#define SET_TX_DESC_MAX_AGG_NUM(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+24, 11, 5, __val)
#define SET_TX_DESC_MCSG1_MAX_LEN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+24, 16, 4, __val)
#define SET_TX_DESC_MCSG2_MAX_LEN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+24, 20, 4, __val)
#define SET_TX_DESC_MCSG3_MAX_LEN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+24, 24, 4, __val)
#define SET_TX_DESC_MCS7_SGI_MAX_LEN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+24, 28, 4, __val)

#define GET_TX_DESC_TXAGC_A(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+24, 0, 5)
#define GET_TX_DESC_TXAGC_B(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+24, 5, 5)
#define GET_TX_DESC_USE_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+24, 10, 1)
#define GET_TX_DESC_MAX_AGG_NUM(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+24, 11, 5)
#define GET_TX_DESC_MCSG1_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+24, 16, 4)
#define GET_TX_DESC_MCSG2_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+24, 20, 4)
#define GET_TX_DESC_MCSG3_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+24, 24, 4)
#define GET_TX_DESC_MCS7_SGI_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+24, 28, 4)

#define SET_TX_DESC_TX_BUFFER_SIZE(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+28, 0, 16, __val)
#define SET_TX_DESC_MCSG4_MAX_LEN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+28, 16, 4, __val)
#define SET_TX_DESC_MCSG5_MAX_LEN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+28, 20, 4, __val)
#define SET_TX_DESC_MCSG6_MAX_LEN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+28, 24, 4, __val)
#define SET_TX_DESC_MCS15_SGI_MAX_LEN(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+28, 28, 4, __val)

#define GET_TX_DESC_TX_BUFFER_SIZE(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+28, 0, 16)
#define GET_TX_DESC_MCSG4_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+28, 16, 4)
#define GET_TX_DESC_MCSG5_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+28, 20, 4)
#define GET_TX_DESC_MCSG6_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+28, 24, 4)
#define GET_TX_DESC_MCS15_SGI_MAX_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+28, 28, 4)

#define SET_TX_DESC_TX_BUFFER_ADDRESS(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+32, 0, 32, __val)
#define SET_TX_DESC_TX_BUFFER_ADDRESS64(__pdesc, __val) \
	SET_BITS_OFFSET_LE(__pdesc+36, 0, 32, __val)

#define GET_TX_DESC_TX_BUFFER_ADDRESS(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+32, 0, 32)
#define GET_TX_DESC_TX_BUFFER_ADDRESS64(__pdesc)	\
	SHIFT_AND_MASK_LE(__pdesc+36, 0, 32)

#define SET_TX_DESC_NEXT_DESC_ADDRESS(__pdesc, __val)	\
	SET_BITS_OFFSET_LE(__pdesc+40, 0, 32, __val)
#define SET_TX_DESC_NEXT_DESC_ADDRESS64(__pdesc, __val) \
	SET_BITS_OFFSET_LE(__pdesc+44, 0, 32, __val)

#define GET_TX_DESC_NEXT_DESC_ADDRESS(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+40, 0, 32)
#define GET_TX_DESC_NEXT_DESC_ADDRESS64(__pdesc)	\
	SHIFT_AND_MASK_LE(__pdesc+44, 0, 32)

#define GET_RX_DESC_PKT_LEN(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 0, 14)
#define GET_RX_DESC_CRC32(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 14, 1)
#define GET_RX_DESC_ICV(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 15, 1)
#define GET_RX_DESC_DRV_INFO_SIZE(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc, 16, 4)
#define GET_RX_DESC_SECURITY(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 20, 3)
#define GET_RX_DESC_QOS(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 23, 1)
#define GET_RX_DESC_SHIFT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 24, 2)
#define GET_RX_DESC_PHYST(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 26, 1)
#define GET_RX_DESC_SWDEC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 27, 1)
#define GET_RX_DESC_LS(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc, 28, 1)
#define GET_RX_DESC_FS(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc, 29, 1)
#define GET_RX_DESC_EOR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 30, 1)
#define GET_RX_DESC_OWN(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc, 31, 1)

#define SET_RX_DESC_PKT_LEN(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc, 0, 14, __val)
#define SET_RX_DESC_EOR(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 30, 1, __val)
#define SET_RX_DESC_OWN(__pdesc, __val)			\
	SET_BITS_OFFSET_LE(__pdesc, 31, 1, __val)

#define GET_RX_DESC_MACID(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 0, 5)
#define GET_RX_DESC_TID(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 5, 4)
#define GET_RX_DESC_HWRSVD(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 9, 5)
#define GET_RX_DESC_PAGGR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 14, 1)
#define GET_RX_DESC_FAGGR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 15, 1)
#define GET_RX_DESC_A1_FIT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 16, 4)
#define GET_RX_DESC_A2_FIT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 20, 4)
#define GET_RX_DESC_PAM(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 24, 1)
#define GET_RX_DESC_PWR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 25, 1)
#define GET_RX_DESC_MD(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc+4, 26, 1)
#define GET_RX_DESC_MF(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc+4, 27, 1)
#define GET_RX_DESC_TYPE(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+4, 28, 2)
#define GET_RX_DESC_MC(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc+4, 30, 1)
#define GET_RX_DESC_BC(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc+4, 31, 1)
#define GET_RX_DESC_SEQ(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 0, 12)
#define GET_RX_DESC_FRAG(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 12, 4)
#define GET_RX_DESC_NEXT_PKT_LEN(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+8, 16, 14)
#define GET_RX_DESC_NEXT_IND(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 30, 1)
#define GET_RX_DESC_RSVD(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+8, 31, 1)

#define GET_RX_DESC_RXMCS(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 0, 6)
#define GET_RX_DESC_RXHT(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 6, 1)
#define GET_RX_DESC_SPLCP(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 8, 1)
#define GET_RX_DESC_BW(__pdesc)				\
	SHIFT_AND_MASK_LE(__pdesc+12, 9, 1)
#define GET_RX_DESC_HTC(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 10, 1)
#define GET_RX_DESC_HWPC_ERR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 14, 1)
#define GET_RX_DESC_HWPC_IND(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 15, 1)
#define GET_RX_DESC_IV0(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+12, 16, 16)

#define GET_RX_DESC_IV1(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+16, 0, 32)
#define GET_RX_DESC_TSFL(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+20, 0, 32)

#define GET_RX_DESC_BUFF_ADDR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc+24, 0, 32)
#define GET_RX_DESC_BUFF_ADDR64(__pdesc)		\
	SHIFT_AND_MASK_LE(__pdesc+28, 0, 32)

#define SET_RX_DESC_BUFF_ADDR(__pdesc, __val)		\
	SET_BITS_OFFSET_LE(__pdesc+24, 0, 32, __val)
#define SET_RX_DESC_BUFF_ADDR64(__pdesc, __val) 	\
	SET_BITS_OFFSET_LE(__pdesc+28, 0, 32, __val)

#define CLEAR_PCI_TX_DESC_CONTENT(__pdesc, _size)	\
	memset(__pdesc, 0, min_t(size_t, _size, TX_DESC_NEXT_DESC_OFFSET))

struct rx_fwinfo_92c {
	u8 gain_trsw[4];
	u8 pwdb_all;
	u8 cfosho[4];
	u8 cfotail[4];
	char rxevm[2];
	char rxsnr[4];
	u8 pdsnr[2];
	u8 csi_current[2];
	u8 csi_target[2];
	u8 sigevm;
	u8 max_ex_pwr;
	u8 ex_intf_flag:1;
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 reserve:4;
} __packed;

struct tx_desc_92c {
	u32 pktsize:16;
	u32 offset:8;
	u32 bmc:1;
	u32 htc:1;
	u32 lastseg:1;
	u32 firstseg:1;
	u32 linip:1;
	u32 noacm:1;
	u32 gf:1;
	u32 own:1;

	u32 macid:5;
	u32 agg_en:1;
	u32 bk:1;
	u32 rdg_en:1;
	u32 queuesel:5;
	u32 rd_nav_ext:1;
	u32 lsig_txop_en:1;
	u32 pifs:1;
	u32 rateid:4;
	u32 nav_usehdr:1;
	u32 en_descid:1;
	u32 sectype:2;
	u32 pktoffset:8;

	u32 rts_rc:6;
	u32 data_rc:6;
	u32 rsvd0:2;
	u32 bar_retryht:2;
	u32 rsvd1:1;
	u32 morefrag:1;
	u32 raw:1;
	u32 ccx:1;
	u32 ampdudensity:3;
	u32 rsvd2:1;
	u32 ant_sela:1;
	u32 ant_selb:1;
	u32 txant_cck:2;
	u32 txant_l:2;
	u32 txant_ht:2;

	u32 nextheadpage:8;
	u32 tailpage:8;
	u32 seq:12;
	u32 pktid:4;

	u32 rtsrate:5;
	u32 apdcfe:1;
	u32 qos:1;
	u32 hwseq_enable:1;
	u32 userrate:1;
	u32 dis_rtsfb:1;
	u32 dis_datafb:1;
	u32 cts2self:1;
	u32 rts_en:1;
	u32 hwrts_en:1;
	u32 portid:1;
	u32 rsvd3:3;
	u32 waitdcts:1;
	u32 cts2ap_en:1;
	u32 txsc:2;
	u32 stbc:2;
	u32 txshort:1;
	u32 txbw:1;
	u32 rtsshort:1;
	u32 rtsbw:1;
	u32 rtssc:2;
	u32 rtsstbc:2;

	u32 txrate:6;
	u32 shortgi:1;
	u32 ccxt:1;
	u32 txrate_fb_lmt:5;
	u32 rtsrate_fb_lmt:4;
	u32 retrylmt_en:1;
	u32 txretrylmt:6;
	u32 usb_txaggnum:8;

	u32 txagca:5;
	u32 txagcb:5;
	u32 usemaxlen:1;
	u32 maxaggnum:5;
	u32 mcsg1maxlen:4;
	u32 mcsg2maxlen:4;
	u32 mcsg3maxlen:4;
	u32 mcs7sgimaxlen:4;

	u32 txbuffersize:16;
	u32 mcsg4maxlen:4;
	u32 mcsg5maxlen:4;
	u32 mcsg6maxlen:4;
	u32 mcsg15sgimaxlen:4;

	u32 txbuffaddr;
	u32 txbufferaddr64;
	u32 nextdescaddress;
	u32 nextdescaddress64;

	u32 reserve_pass_pcie_mm_limit[4];
} __packed;

struct rx_desc_92c {
	u32 length:14;
	u32 crc32:1;
	u32 icverror:1;
	u32 drv_infosize:4;
	u32 security:3;
	u32 qos:1;
	u32 shift:2;
	u32 phystatus:1;
	u32 swdec:1;
	u32 lastseg:1;
	u32 firstseg:1;
	u32 eor:1;
	u32 own:1;

	u32 macid:5;
	u32 tid:4;
	u32 hwrsvd:5;
	u32 paggr:1;
	u32 faggr:1;
	u32 a1_fit:4;
	u32 a2_fit:4;
	u32 pam:1;
	u32 pwr:1;
	u32 moredata:1;
	u32 morefrag:1;
	u32 type:2;
	u32 mc:1;
	u32 bc:1;

	u32 seq:12;
	u32 frag:4;
	u32 nextpktlen:14;
	u32 nextind:1;
	u32 rsvd:1;

	u32 rxmcs:6;
	u32 rxht:1;
	u32 amsdu:1;
	u32 splcp:1;
	u32 bandwidth:1;
	u32 htc:1;
	u32 tcpchk_rpt:1;
	u32 ipcchk_rpt:1;
	u32 tcpchk_valid:1;
	u32 hwpcerr:1;
	u32 hwpcind:1;
	u32 iv0:16;

	u32 iv1;

	u32 tsfl;

	u32 bufferaddress;
	u32 bufferaddress64;

} __packed;

void rtl92ce_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc,
			  u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb, u8 hw_queue,
			  struct rtl_tcb_desc *ptcb_desc);
bool rtl92ce_rx_query_desc(struct ieee80211_hw *hw,
			   struct rtl_stats *stats,
			   struct ieee80211_rx_status *rx_status,
			   u8 *pdesc, struct sk_buff *skb);
void rtl92ce_set_desc(struct ieee80211_hw *hw, u8 *pdesc, bool istx,
		      u8 desc_name, u8 *val);
u32 rtl92ce_get_desc(u8 *pdesc, bool istx, u8 desc_name);
void rtl92ce_tx_polling(struct ieee80211_hw *hw, u8 hw_queue);
void rtl92ce_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc,
			     bool b_firstseg, bool b_lastseg,
			     struct sk_buff *skb);
#endif
