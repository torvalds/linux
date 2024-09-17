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
static inline void set_tx_desc_pkt_size(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(15, 0));
}

static inline void set_tx_desc_offset(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(23, 16));
}

static inline void set_tx_desc_last_seg(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(26));
}

static inline void set_tx_desc_first_seg(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(27));
}

static inline void set_tx_desc_linip(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(28));
}

static inline void set_tx_desc_own(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(31));
}

static inline u32 get_tx_desc_own(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), BIT(31));
}

/* Dword 1 */
static inline void set_tx_desc_macid(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(4, 0));
}

static inline void set_tx_desc_queue_sel(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(12, 8));
}

static inline void set_tx_desc_non_qos(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, BIT(16));
}

static inline void set_tx_desc_sec_type(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 1), __val, GENMASK(23, 22));
}

/* Dword 2 */
static inline void	set_tx_desc_rsvd_macid(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 2), __val, GENMASK(28, 24));
}

static inline void set_tx_desc_agg_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 2), __val, BIT(29));
}

/* Dword 3 */
static inline void set_tx_desc_seq(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 3), __val, GENMASK(27, 16));
}

/* Dword 4 */
static inline void set_tx_desc_rts_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(5, 0));
}

static inline void set_tx_desc_cts_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(11));
}

static inline void set_tx_desc_rts_enable(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(12));
}

static inline void set_tx_desc_ra_brsr_id(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(15, 13));
}

static inline void set_tx_desc_txht(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(16));
}

static inline void set_tx_desc_tx_short(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(17));
}

static inline void set_tx_desc_tx_bandwidth(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(18));
}

static inline void set_tx_desc_tx_sub_carrier(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(20, 19));
}

static inline void set_tx_desc_rts_short(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(25));
}

static inline void set_tx_desc_rts_bandwidth(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(26));
}

static inline void set_tx_desc_rts_sub_carrier(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(28, 27));
}

static inline void set_tx_desc_rts_stbc(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, GENMASK(30, 29));
}

static inline void set_tx_desc_user_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 4), __val, BIT(31));
}

/* Dword 5 */
static inline void set_tx_desc_packet_id(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(8, 0));
}

static inline void set_tx_desc_tx_rate(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(14, 9));
}

static inline void set_tx_desc_data_rate_fb_limit(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 5), __val, GENMASK(20, 16));
}

/* Dword 7 */
static inline void set_tx_desc_tx_buffer_size(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits((__pdesc + 7), __val, GENMASK(15, 0));
}

/* Dword 8 */
static inline void set_tx_desc_tx_buffer_address(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 8) = cpu_to_le32(__val);
}

static inline u32 get_tx_desc_tx_buffer_address(__le32 *__pdesc)
{
	return le32_to_cpu(*((__pdesc + 8)));
}

/* Dword 9 */
static inline void set_tx_desc_next_desc_address(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 9) = cpu_to_le32(__val);
}

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
static inline void set_rx_status_desc_pkt_len(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, GENMASK(13, 0));
}

static inline void set_rx_status_desc_eor(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(30));
}

static inline void set_rx_status_desc_own(__le32 *__pdesc, u32 __val)
{
	le32p_replace_bits(__pdesc, __val, BIT(31));
}

static inline u32 get_rx_status_desc_pkt_len(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), GENMASK(13, 0));
}

static inline u32 get_rx_status_desc_crc32(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), BIT(14));
}

static inline u32 get_rx_status_desc_icv(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), BIT(15));
}

static inline u32 get_rx_status_desc_drvinfo_size(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), GENMASK(19, 16));
}

static inline u32 get_rx_status_desc_shift(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), GENMASK(25, 24));
}

static inline u32 get_rx_status_desc_phy_status(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), BIT(26));
}

static inline u32 get_rx_status_desc_swdec(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), BIT(27));
}

static inline u32 get_rx_status_desc_own(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc), BIT(31));
}

/* DWORD 1 */
static inline u32 get_rx_status_desc_paggr(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 1), BIT(14));
}

static inline u32 get_rx_status_desc_faggr(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 1), BIT(15));
}

/* DWORD 3 */
static inline u32 get_rx_status_desc_rx_mcs(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), GENMASK(5, 0));
}

static inline u32 get_rx_status_desc_rx_ht(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(6));
}

static inline u32 get_rx_status_desc_splcp(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(8));
}

static inline u32 get_rx_status_desc_bw(__le32 *__pdesc)
{
	return le32_get_bits(*(__pdesc + 3), BIT(9));
}

/* DWORD 5 */
static inline u32 get_rx_status_desc_tsfl(__le32 *__pdesc)
{
	return le32_to_cpu(*((__pdesc + 5)));
}

/* DWORD 6 */
static inline void set_rx_status__desc_buff_addr(__le32 *__pdesc, u32 __val)
{
	*(__pdesc + 6) = cpu_to_le32(__val);
}

static inline u32 get_rx_status_desc_buff_addr(__le32 *__pdesc)
{
	return le32_to_cpu(*(__pdesc + 6));
}

#define SE_RX_HAL_IS_CCK_RATE(_pdesc)\
	(get_rx_status_desc_rx_mcs(_pdesc) == DESC_RATE1M ||	\
	 get_rx_status_desc_rx_mcs(_pdesc) == DESC_RATE2M ||	\
	 get_rx_status_desc_rx_mcs(_pdesc) == DESC_RATE5_5M ||\
	 get_rx_status_desc_rx_mcs(_pdesc) == DESC_RATE11M)

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

