/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __REALTEK_92S_DEF_H__
#define __REALTEK_92S_DEF_H__

#define RX_MPDU_QUEUE				0
#define RX_CMD_QUEUE				1

#define SHORT_SLOT_TIME				9
#define NON_SHORT_SLOT_TIME			20

/* Queue Select Value in TxDesc */
#define QSLT_BK					0x2
#define QSLT_BE					0x0
#define QSLT_VI					0x5
#define QSLT_VO					0x6
#define QSLT_BEACON				0x10
#define QSLT_HIGH				0x11
#define QSLT_MGNT				0x12
#define QSLT_CMD				0x13

/* Tx Desc */
#define TX_DESC_SIZE_RTL8192S			(16 * 4)
#define TX_CMDDESC_SIZE_RTL8192S		(16 * 4)

/* macros to read/write various fields in RX or TX descriptors */

/* Dword 0 */
#define SET_TX_DESC_PKT_SIZE(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(15, 0))
#define SET_TX_DESC_OFFSET(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(23, 16))
#define SET_TX_DESC_LAST_SEG(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(26))
#define SET_TX_DESC_FIRST_SEG(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(27))
#define SET_TX_DESC_LINIP(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(28))
#define SET_TX_DESC_OWN(__pdesc, __val)				\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(31))

#define GET_TX_DESC_OWN(__pdesc)				\
	le32_get_bits(*((__le32 *)__pdesc), BIT(31))

/* Dword 1 */
#define SET_TX_DESC_MACID(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(4, 0))
#define SET_TX_DESC_QUEUE_SEL(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(12, 8))
#define SET_TX_DESC_NON_QOS(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, BIT(16))
#define SET_TX_DESC_SEC_TYPE(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 4), __val, GENMASK(23, 22))

/* Dword 2 */
#define	SET_TX_DESC_RSVD_MACID(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, GENMASK(28, 24))
#define SET_TX_DESC_AGG_ENABLE(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 8), __val, BIT(29))

/* Dword 3 */
#define SET_TX_DESC_SEQ(__pdesc, __val)				\
	le32p_replace_bits((__le32 *)(__pdesc + 12), __val, GENMASK(27, 16))

/* Dword 4 */
#define SET_TX_DESC_RTS_RATE(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(5, 0))
#define SET_TX_DESC_CTS_ENABLE(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, BIT(11))
#define SET_TX_DESC_RTS_ENABLE(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, BIT(12))
#define SET_TX_DESC_RA_BRSR_ID(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(15, 13))
#define SET_TX_DESC_TXHT(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, BIT(16))
#define SET_TX_DESC_TX_SHORT(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, BIT(17))
#define SET_TX_DESC_TX_BANDWIDTH(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, BIT(18))
#define SET_TX_DESC_TX_SUB_CARRIER(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(20, 19))
#define SET_TX_DESC_RTS_SHORT(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, BIT(25))
#define SET_TX_DESC_RTS_BANDWIDTH(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, BIT(26))
#define SET_TX_DESC_RTS_SUB_CARRIER(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(28, 27))
#define SET_TX_DESC_RTS_STBC(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, GENMASK(30, 29))
#define SET_TX_DESC_USER_RATE(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 16), __val, BIT(31))

/* Dword 5 */
#define SET_TX_DESC_PACKET_ID(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(8, 0))
#define SET_TX_DESC_TX_RATE(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(14, 9))
#define SET_TX_DESC_DATA_RATE_FB_LIMIT(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 20), __val, GENMASK(20, 16))

/* Dword 7 */
#define SET_TX_DESC_TX_BUFFER_SIZE(__pdesc, __val)		\
	le32p_replace_bits((__le32 *)(__pdesc + 28), __val, GENMASK(15, 0))

/* Dword 8 */
#define SET_TX_DESC_TX_BUFFER_ADDRESS(__pdesc, __val)		\
	*(__le32 *)(__pdesc + 32) = cpu_to_le32(__val)
#define GET_TX_DESC_TX_BUFFER_ADDRESS(__pdesc)			\
	le32_to_cpu(*((__le32 *)(__pdesc + 32)))

/* Dword 9 */
#define SET_TX_DESC_NEXT_DESC_ADDRESS(__pdesc, __val)		\
	*(__le32 *)(__pdesc + 36) = cpu_to_le32(__val)

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
	le32p_replace_bits((__le32 *)__pdesc, __val, GENMASK(13, 0))
#define SET_RX_STATUS_DESC_EOR(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(30))
#define SET_RX_STATUS_DESC_OWN(__pdesc, __val)			\
	le32p_replace_bits((__le32 *)__pdesc, __val, BIT(31))

#define GET_RX_STATUS_DESC_PKT_LEN(__pdesc)			\
	le32_get_bits(*((__le32 *)__pdesc), GENMASK(13, 0))
#define GET_RX_STATUS_DESC_CRC32(__pdesc)			\
	le32_get_bits(*((__le32 *)__pdesc), BIT(14))
#define GET_RX_STATUS_DESC_ICV(__pdesc)				\
	le32_get_bits(*((__le32 *)__pdesc), BIT(15))
#define GET_RX_STATUS_DESC_DRVINFO_SIZE(__pdesc)		\
	le32_get_bits(*((__le32 *)__pdesc), GENMASK(19, 16))
#define GET_RX_STATUS_DESC_SHIFT(__pdesc)			\
	le32_get_bits(*((__le32 *)__pdesc), GENMASK(25, 24))
#define GET_RX_STATUS_DESC_PHY_STATUS(__pdesc)			\
	le32_get_bits(*((__le32 *)__pdesc), BIT(26))
#define GET_RX_STATUS_DESC_SWDEC(__pdesc)			\
	le32_get_bits(*((__le32 *)__pdesc), BIT(27))
#define GET_RX_STATUS_DESC_OWN(__pdesc)				\
	le32_get_bits(*((__le32 *)__pdesc), BIT(31))

/* DWORD 1 */
#define GET_RX_STATUS_DESC_PAGGR(__pdesc)			\
	le32_get_bits(*(__le32 *)(__pdesc + 4), BIT(14))
#define GET_RX_STATUS_DESC_FAGGR(__pdesc)			\
	le32_get_bits(*(__le32 *)(__pdesc + 4), BIT(15))

/* DWORD 3 */
#define GET_RX_STATUS_DESC_RX_MCS(__pdesc)			\
	le32_get_bits(*(__le32 *)(__pdesc + 12), GENMASK(5, 0))
#define GET_RX_STATUS_DESC_RX_HT(__pdesc)			\
	le32_get_bits(*(__le32 *)(__pdesc + 12), BIT(6))
#define GET_RX_STATUS_DESC_SPLCP(__pdesc)			\
	le32_get_bits(*(__le32 *)(__pdesc + 12), BIT(8))
#define GET_RX_STATUS_DESC_BW(__pdesc)				\
	le32_get_bits(*(__le32 *)(__pdesc + 12), BIT(9))

/* DWORD 5 */
#define GET_RX_STATUS_DESC_TSFL(__pdesc)			\
	le32_to_cpu(*((__le32 *)(__pdesc + 20)))

/* DWORD 6 */
#define SET_RX_STATUS__DESC_BUFF_ADDR(__pdesc, __val)	\
	*(__le32 *)(__pdesc + 24) = cpu_to_le32(__val)
#define GET_RX_STATUS_DESC_BUFF_ADDR(__pdesc)			\
	le32_to_cpu(*(__le32 *)(__pdesc + 24))

#define SE_RX_HAL_IS_CCK_RATE(_pdesc)\
	(GET_RX_STATUS_DESC_RX_MCS(_pdesc) == DESC_RATE1M ||	\
	 GET_RX_STATUS_DESC_RX_MCS(_pdesc) == DESC_RATE2M ||	\
	 GET_RX_STATUS_DESC_RX_MCS(_pdesc) == DESC_RATE5_5M ||\
	 GET_RX_STATUS_DESC_RX_MCS(_pdesc) == DESC_RATE11M)

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

/* Driver info contain PHY status
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

