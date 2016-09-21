/*
 * Copyright (c) 2014 - 2016 Jes Sorensen <Jes.Sorensen@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * Register definitions taken from original Realtek rtl8723au driver
 */

#include <asm/byteorder.h>

#define RTL8XXXU_DEBUG_REG_WRITE	0x01
#define RTL8XXXU_DEBUG_REG_READ		0x02
#define RTL8XXXU_DEBUG_RFREG_WRITE	0x04
#define RTL8XXXU_DEBUG_RFREG_READ	0x08
#define RTL8XXXU_DEBUG_CHANNEL		0x10
#define RTL8XXXU_DEBUG_TX		0x20
#define RTL8XXXU_DEBUG_TX_DUMP		0x40
#define RTL8XXXU_DEBUG_RX		0x80
#define RTL8XXXU_DEBUG_RX_DUMP		0x100
#define RTL8XXXU_DEBUG_USB		0x200
#define RTL8XXXU_DEBUG_KEY		0x400
#define RTL8XXXU_DEBUG_H2C		0x800
#define RTL8XXXU_DEBUG_ACTION		0x1000
#define RTL8XXXU_DEBUG_EFUSE		0x2000

#define RTW_USB_CONTROL_MSG_TIMEOUT	500
#define RTL8XXXU_MAX_REG_POLL		500
#define	USB_INTR_CONTENT_LENGTH		56

#define RTL8XXXU_OUT_ENDPOINTS		4

#define REALTEK_USB_READ		0xc0
#define REALTEK_USB_WRITE		0x40
#define REALTEK_USB_CMD_REQ		0x05
#define REALTEK_USB_CMD_IDX		0x00

#define TX_TOTAL_PAGE_NUM		0xf8
#define TX_TOTAL_PAGE_NUM_8192E		0xf3
#define TX_TOTAL_PAGE_NUM_8723B		0xf7
/* (HPQ + LPQ + NPQ + PUBQ) = TX_TOTAL_PAGE_NUM */
#define TX_PAGE_NUM_PUBQ		0xe7
#define TX_PAGE_NUM_HI_PQ		0x0c
#define TX_PAGE_NUM_LO_PQ		0x02
#define TX_PAGE_NUM_NORM_PQ		0x02

#define TX_PAGE_NUM_PUBQ_8192E		0xe7
#define TX_PAGE_NUM_HI_PQ_8192E		0x08
#define TX_PAGE_NUM_LO_PQ_8192E		0x0c
#define TX_PAGE_NUM_NORM_PQ_8192E	0x00

#define TX_PAGE_NUM_PUBQ_8723B		0xe7
#define TX_PAGE_NUM_HI_PQ_8723B		0x0c
#define TX_PAGE_NUM_LO_PQ_8723B		0x02
#define TX_PAGE_NUM_NORM_PQ_8723B	0x02

#define RTL_FW_PAGE_SIZE		4096
#define RTL8XXXU_FIRMWARE_POLL_MAX	1000

#define RTL8723A_CHANNEL_GROUPS		3
#define RTL8723A_MAX_RF_PATHS		2
#define RTL8723B_CHANNEL_GROUPS		6
#define RTL8723B_TX_COUNT		4
#define RTL8723B_MAX_RF_PATHS		4
#define RTL8XXXU_MAX_CHANNEL_GROUPS	6
#define RF6052_MAX_TX_PWR		0x3f

#define EFUSE_MAP_LEN			512
#define EFUSE_MAX_SECTION_8723A		64
#define EFUSE_REAL_CONTENT_LEN_8723A	512
#define EFUSE_BT_MAP_LEN_8723A		1024
#define EFUSE_MAX_WORD_UNIT		4

enum rtl8xxxu_rtl_chip {
	RTL8192S = 0x81920,
	RTL8191S = 0x81910,
	RTL8192C = 0x8192c,
	RTL8191C = 0x8191c,
	RTL8188C = 0x8188c,
	RTL8188R = 0x81889,
	RTL8192D = 0x8192d,
	RTL8723A = 0x8723a,
	RTL8188E = 0x8188e,
	RTL8812  = 0x88120,
	RTL8821  = 0x88210,
	RTL8192E = 0x8192e,
	RTL8191E = 0x8191e,
	RTL8723B = 0x8723b,
	RTL8814A = 0x8814a,
	RTL8881A = 0x8881a,
	RTL8821B = 0x8821b,
	RTL8822B = 0x8822b,
	RTL8703B = 0x8703b,
	RTL8195A = 0x8195a,
	RTL8188F = 0x8188f
};

enum rtl8xxxu_rx_type {
	RX_TYPE_DATA_PKT = 0,
	RX_TYPE_C2H = 1,
	RX_TYPE_ERROR = -1
};

struct rtl8xxxu_rxdesc16 {
#ifdef __LITTLE_ENDIAN
	u32 pktlen:14;
	u32 crc32:1;
	u32 icverr:1;
	u32 drvinfo_sz:4;
	u32 security:3;
	u32 qos:1;
	u32 shift:2;
	u32 phy_stats:1;
	u32 swdec:1;
	u32 ls:1;
	u32 fs:1;
	u32 eor:1;
	u32 own:1;

	u32 macid:5;
	u32 tid:4;
	u32 hwrsvd:4;
	u32 amsdu:1;
	u32 paggr:1;
	u32 faggr:1;
	u32 a1fit:4;
	u32 a2fit:4;
	u32 pam:1;
	u32 pwr:1;
	u32 md:1;
	u32 mf:1;
	u32 type:2;
	u32 mc:1;
	u32 bc:1;

	u32 seq:12;
	u32 frag:4;
	u32 pkt_cnt:8;
	u32 reserved:6;
	u32 nextind:1;
	u32 reserved0:1;

	u32 rxmcs:6;
	u32 rxht:1;
	u32 gf:1;
	u32 splcp:1;
	u32 bw:1;
	u32 htc:1;
	u32 eosp:1;
	u32 bssidfit:2;
	u32 reserved1:16;
	u32 unicastwake:1;
	u32 magicwake:1;

	u32 pattern0match:1;
	u32 pattern1match:1;
	u32 pattern2match:1;
	u32 pattern3match:1;
	u32 pattern4match:1;
	u32 pattern5match:1;
	u32 pattern6match:1;
	u32 pattern7match:1;
	u32 pattern8match:1;
	u32 pattern9match:1;
	u32 patternamatch:1;
	u32 patternbmatch:1;
	u32 patterncmatch:1;
	u32 reserved2:19;
#else
	u32 own:1;
	u32 eor:1;
	u32 fs:1;
	u32 ls:1;
	u32 swdec:1;
	u32 phy_stats:1;
	u32 shift:2;
	u32 qos:1;
	u32 security:3;
	u32 drvinfo_sz:4;
	u32 icverr:1;
	u32 crc32:1;
	u32 pktlen:14;

	u32 bc:1;
	u32 mc:1;
	u32 type:2;
	u32 mf:1;
	u32 md:1;
	u32 pwr:1;
	u32 pam:1;
	u32 a2fit:4;
	u32 a1fit:4;
	u32 faggr:1;
	u32 paggr:1;
	u32 amsdu:1;
	u32 hwrsvd:4;
	u32 tid:4;
	u32 macid:5;

	u32 reserved0:1;
	u32 nextind:1;
	u32 reserved:6;
	u32 pkt_cnt:8;
	u32 frag:4;
	u32 seq:12;

	u32 magicwake:1;
	u32 unicastwake:1;
	u32 reserved1:16;
	u32 bssidfit:2;
	u32 eosp:1;
	u32 htc:1;
	u32 bw:1;
	u32 splcp:1;
	u32 gf:1;
	u32 rxht:1;
	u32 rxmcs:6;

	u32 reserved2:19;
	u32 patterncmatch:1;
	u32 patternbmatch:1;
	u32 patternamatch:1;
	u32 pattern9match:1;
	u32 pattern8match:1;
	u32 pattern7match:1;
	u32 pattern6match:1;
	u32 pattern5match:1;
	u32 pattern4match:1;
	u32 pattern3match:1;
	u32 pattern2match:1;
	u32 pattern1match:1;
	u32 pattern0match:1;
#endif
	__le32 tsfl;
#if 0
	u32 bassn:12;
	u32 bavld:1;
	u32 reserved3:19;
#endif
};

struct rtl8xxxu_rxdesc24 {
#ifdef __LITTLE_ENDIAN
	u32 pktlen:14;
	u32 crc32:1;
	u32 icverr:1;
	u32 drvinfo_sz:4;
	u32 security:3;
	u32 qos:1;
	u32 shift:2;
	u32 phy_stats:1;
	u32 swdec:1;
	u32 ls:1;
	u32 fs:1;
	u32 eor:1;
	u32 own:1;

	u32 macid:7;
	u32 dummy1_0:1;
	u32 tid:4;
	u32 dummy1_1:1;
	u32 amsdu:1;
	u32 rxid_match:1;
	u32 paggr:1;
	u32 a1fit:4;	/* 16 */
	u32 chkerr:1;
	u32 ipver:1;
	u32 tcpudp:1;
	u32 chkvld:1;
	u32 pam:1;
	u32 pwr:1;
	u32 more_data:1;
	u32 more_frag:1;
	u32 type:2;
	u32 mc:1;
	u32 bc:1;

	u32 seq:12;
	u32 frag:4;
	u32 rx_is_qos:1;	/* 16 */
	u32 dummy2_0:1;
	u32 wlanhd_iv_len:6;
	u32 dummy2_1:4;
	u32 rpt_sel:1;
	u32 dummy2_2:3;

	u32 rxmcs:7;
	u32 dummy3_0:3;
	u32 htc:1;
	u32 eosp:1;
	u32 bssidfit:2;
	u32 dummy3_1:2;
	u32 usb_agg_pktnum:8;	/* 16 */
	u32 dummy3_2:5;
	u32 pattern_match:1;
	u32 unicast_match:1;
	u32 magic_match:1;

	u32 splcp:1;
	u32 ldcp:1;
	u32 stbc:1;
	u32 dummy4_0:1;
	u32 bw:2;
	u32 dummy4_1:26;
#else
	u32 own:1;
	u32 eor:1;
	u32 fs:1;
	u32 ls:1;
	u32 swdec:1;
	u32 phy_stats:1;
	u32 shift:2;
	u32 qos:1;
	u32 security:3;
	u32 drvinfo_sz:4;
	u32 icverr:1;
	u32 crc32:1;
	u32 pktlen:14;

	u32 bc:1;
	u32 mc:1;
	u32 type:2;
	u32 mf:1;
	u32 md:1;
	u32 pwr:1;
	u32 pam:1;
	u32 a2fit:4;
	u32 a1fit:4;
	u32 faggr:1;
	u32 paggr:1;
	u32 amsdu:1;
	u32 hwrsvd:4;
	u32 tid:4;
	u32 macid:5;

	u32 dummy2_2:3;
	u32 rpt_sel:1;
	u32 dummy2_1:4;
	u32 wlanhd_iv_len:6;
	u32 dummy2_0:1;
	u32 rx_is_qos:1;
	u32 frag:4;		/* 16 */
	u32 seq:12;

	u32 magic_match:1;
	u32 unicast_match:1;
	u32 pattern_match:1;
	u32 dummy3_2:5;
	u32 usb_agg_pktnum:8;
	u32 dummy3_1:2;		/* 16 */
	u32 bssidfit:2;
	u32 eosp:1;
	u32 htc:1;
	u32 dummy3_0:3;
	u32 rxmcs:7;

	u32 dumm4_1:26;
	u32 bw:2;
	u32 dummy4_0:1;
	u32 stbc:1;
	u32 ldcp:1;
	u32 splcp:1;
#endif
	__le32 tsfl;
};

struct rtl8xxxu_txdesc32 {
	__le16 pkt_size;
	u8 pkt_offset;
	u8 txdw0;
	__le32 txdw1;
	__le32 txdw2;
	__le32 txdw3;
	__le32 txdw4;
	__le32 txdw5;
	__le32 txdw6;
	__le16 csum;
	__le16 txdw7;
};

struct rtl8xxxu_txdesc40 {
	__le16 pkt_size;
	u8 pkt_offset;
	u8 txdw0;
	__le32 txdw1;
	__le32 txdw2;
	__le32 txdw3;
	__le32 txdw4;
	__le32 txdw5;
	__le32 txdw6;
	__le16 csum;
	__le16 txdw7;
	__le32 txdw8;
	__le32 txdw9;
};

/*  CCK Rates, TxHT = 0 */
#define DESC_RATE_1M			0x00
#define DESC_RATE_2M			0x01
#define DESC_RATE_5_5M			0x02
#define DESC_RATE_11M			0x03

/*  OFDM Rates, TxHT = 0 */
#define DESC_RATE_6M			0x04
#define DESC_RATE_9M			0x05
#define DESC_RATE_12M			0x06
#define DESC_RATE_18M			0x07
#define DESC_RATE_24M			0x08
#define DESC_RATE_36M			0x09
#define DESC_RATE_48M			0x0a
#define DESC_RATE_54M			0x0b

/*  MCS Rates, TxHT = 1 */
#define DESC_RATE_MCS0			0x0c
#define DESC_RATE_MCS1			0x0d
#define DESC_RATE_MCS2			0x0e
#define DESC_RATE_MCS3			0x0f
#define DESC_RATE_MCS4			0x10
#define DESC_RATE_MCS5			0x11
#define DESC_RATE_MCS6			0x12
#define DESC_RATE_MCS7			0x13
#define DESC_RATE_MCS8			0x14
#define DESC_RATE_MCS9			0x15
#define DESC_RATE_MCS10			0x16
#define DESC_RATE_MCS11			0x17
#define DESC_RATE_MCS12			0x18
#define DESC_RATE_MCS13			0x19
#define DESC_RATE_MCS14			0x1a
#define DESC_RATE_MCS15			0x1b
#define DESC_RATE_MCS15_SG		0x1c
#define DESC_RATE_MCS32			0x20

#define TXDESC_OFFSET_SZ		0
#define TXDESC_OFFSET_SHT		16
#if 0
#define TXDESC_BMC			BIT(24)
#define TXDESC_LSG			BIT(26)
#define TXDESC_FSG			BIT(27)
#define TXDESC_OWN			BIT(31)
#else
#define TXDESC_BROADMULTICAST		BIT(0)
#define TXDESC_HTC			BIT(1)
#define TXDESC_LAST_SEGMENT		BIT(2)
#define TXDESC_FIRST_SEGMENT		BIT(3)
#define TXDESC_LINIP			BIT(4)
#define TXDESC_NO_ACM			BIT(5)
#define TXDESC_GF			BIT(6)
#define TXDESC_OWN			BIT(7)
#endif

/* Word 1 */
/*
 * Bits 0-7 differ dependent on chip generation. For 8723au bits 5/6 are
 * aggregation enable and break respectively. For 8723bu, bits 0-7 are macid.
 */
#define TXDESC_PKT_OFFSET_SZ		0
#define TXDESC32_AGG_ENABLE		BIT(5)
#define TXDESC32_AGG_BREAK		BIT(6)
#define TXDESC40_MACID_SHIFT		0
#define TXDESC40_MACID_MASK		0x00f0
#define TXDESC_QUEUE_SHIFT		8
#define TXDESC_QUEUE_MASK		0x1f00
#define TXDESC_QUEUE_BK			0x2
#define TXDESC_QUEUE_BE			0x0
#define TXDESC_QUEUE_VI			0x5
#define TXDESC_QUEUE_VO			0x7
#define TXDESC_QUEUE_BEACON		0x10
#define TXDESC_QUEUE_HIGH		0x11
#define TXDESC_QUEUE_MGNT		0x12
#define TXDESC_QUEUE_CMD		0x13
#define TXDESC_QUEUE_MAX		(TXDESC_QUEUE_CMD + 1)
#define TXDESC40_RDG_NAV_EXT		BIT(13)
#define TXDESC40_LSIG_TXOP_ENABLE	BIT(14)
#define TXDESC40_PIFS			BIT(15)

#define DESC_RATE_ID_SHIFT		16
#define DESC_RATE_ID_MASK		0xf
#define TXDESC_NAVUSEHDR		BIT(20)
#define TXDESC_SEC_RC4			0x00400000
#define TXDESC_SEC_AES			0x00c00000
#define TXDESC_PKT_OFFSET_SHIFT		26
#define TXDESC_AGG_EN			BIT(29)
#define TXDESC_HWPC			BIT(31)

/* Word 2 */
#define TXDESC40_PAID_SHIFT		0
#define TXDESC40_PAID_MASK		0x1ff
#define TXDESC40_CCA_RTS_SHIFT		10
#define TXDESC40_CCA_RTS_MASK		0xc00
#define TXDESC40_AGG_ENABLE		BIT(12)
#define TXDESC40_RDG_ENABLE		BIT(13)
#define TXDESC40_AGG_BREAK		BIT(16)
#define TXDESC40_MORE_FRAG		BIT(17)
#define TXDESC40_RAW			BIT(18)
#define TXDESC32_ACK_REPORT		BIT(19)
#define TXDESC40_SPE_RPT		BIT(19)
#define TXDESC_AMPDU_DENSITY_SHIFT	20
#define TXDESC40_BT_INT			BIT(23)
#define TXDESC40_GID_SHIFT		24

/* Word 3 */
#define TXDESC40_USE_DRIVER_RATE	BIT(8)
#define TXDESC40_CTS_SELF_ENABLE	BIT(11)
#define TXDESC40_RTS_CTS_ENABLE		BIT(12)
#define TXDESC40_HW_RTS_ENABLE		BIT(13)
#define TXDESC32_SEQ_SHIFT		16
#define TXDESC32_SEQ_MASK		0x0fff0000

/* Word 4 */
#define TXDESC32_RTS_RATE_SHIFT		0
#define TXDESC32_RTS_RATE_MASK		0x3f
#define TXDESC32_QOS			BIT(6)
#define TXDESC32_HW_SEQ_ENABLE		BIT(7)
#define TXDESC32_USE_DRIVER_RATE	BIT(8)
#define TXDESC_DISABLE_DATA_FB		BIT(10)
#define TXDESC32_CTS_SELF_ENABLE	BIT(11)
#define TXDESC32_RTS_CTS_ENABLE		BIT(12)
#define TXDESC32_HW_RTS_ENABLE		BIT(13)
#define TXDESC_PRIME_CH_OFF_LOWER	BIT(20)
#define TXDESC_PRIME_CH_OFF_UPPER	BIT(21)
#define TXDESC32_SHORT_PREAMBLE		BIT(24)
#define TXDESC_DATA_BW			BIT(25)
#define TXDESC_RTS_DATA_BW		BIT(27)
#define TXDESC_RTS_PRIME_CH_OFF_LOWER	BIT(28)
#define TXDESC_RTS_PRIME_CH_OFF_UPPER	BIT(29)
#define TXDESC40_DATA_RATE_FB_SHIFT	8
#define TXDESC40_DATA_RATE_FB_MASK	0x00001f00
#define TXDESC40_RETRY_LIMIT_ENABLE	BIT(17)
#define TXDESC40_RETRY_LIMIT_SHIFT	18
#define TXDESC40_RETRY_LIMIT_MASK	0x00fc0000
#define TXDESC40_RTS_RATE_SHIFT		24
#define TXDESC40_RTS_RATE_MASK		0x3f000000

/* Word 5 */
#define TXDESC40_SHORT_PREAMBLE		BIT(4)
#define TXDESC32_SHORT_GI		BIT(6)
#define TXDESC_CCX_TAG			BIT(7)
#define TXDESC32_RETRY_LIMIT_ENABLE	BIT(17)
#define TXDESC32_RETRY_LIMIT_SHIFT	18
#define TXDESC32_RETRY_LIMIT_MASK	0x00fc0000

/* Word 6 */
#define TXDESC_MAX_AGG_SHIFT		11

/* Word 8 */
#define TXDESC40_HW_SEQ_ENABLE		BIT(15)

/* Word 9 */
#define TXDESC40_SEQ_SHIFT		12
#define TXDESC40_SEQ_MASK		0x00fff000

struct phy_rx_agc_info {
#ifdef __LITTLE_ENDIAN
	u8	gain:7, trsw:1;
#else
	u8	trsw:1, gain:7;
#endif
};

struct rtl8723au_phy_stats {
	struct phy_rx_agc_info path_agc[RTL8723A_MAX_RF_PATHS];
	u8	ch_corr[RTL8723A_MAX_RF_PATHS];
	u8	cck_sig_qual_ofdm_pwdb_all;
	u8	cck_agc_rpt_ofdm_cfosho_a;
	u8	cck_rpt_b_ofdm_cfosho_b;
	u8	reserved_1;
	u8	noise_power_db_msb;
	u8	path_cfotail[RTL8723A_MAX_RF_PATHS];
	u8	pcts_mask[RTL8723A_MAX_RF_PATHS];
	s8	stream_rxevm[RTL8723A_MAX_RF_PATHS];
	u8	path_rxsnr[RTL8723A_MAX_RF_PATHS];
	u8	noise_power_db_lsb;
	u8	reserved_2[3];
	u8	stream_csi[RTL8723A_MAX_RF_PATHS];
	u8	stream_target_csi[RTL8723A_MAX_RF_PATHS];
	s8	sig_evm;
	u8	reserved_3;

#ifdef __LITTLE_ENDIAN
	u8	antsel_rx_keep_2:1;	/* ex_intf_flg:1; */
	u8	sgi_en:1;
	u8	rxsc:2;
	u8	idle_long:1;
	u8	r_ant_train_en:1;
	u8	antenna_select_b:1;
	u8	antenna_select:1;
#else	/*  _BIG_ENDIAN_ */
	u8	antenna_select:1;
	u8	antenna_select_b:1;
	u8	r_ant_train_en:1;
	u8	idle_long:1;
	u8	rxsc:2;
	u8	sgi_en:1;
	u8	antsel_rx_keep_2:1;	/* ex_intf_flg:1; */
#endif
};

/*
 * Regs to backup
 */
#define RTL8XXXU_ADDA_REGS		16
#define RTL8XXXU_MAC_REGS		4
#define RTL8XXXU_BB_REGS		9

struct rtl8xxxu_firmware_header {
	__le16	signature;		/*  92C0: test chip; 92C,
					    88C0: test chip;
					    88C1: MP A-cut;
					    92C1: MP A-cut */
	u8	category;		/*  AP/NIC and USB/PCI */
	u8	function;

	__le16	major_version;		/*  FW Version */
	u8	minor_version;		/*  FW Subversion, default 0x00 */
	u8	reserved1;

	u8	month;			/*  Release time Month field */
	u8	date;			/*  Release time Date field */
	u8	hour;			/*  Release time Hour field */
	u8	minute;			/*  Release time Minute field */

	__le16	ramcodesize;		/*  Size of RAM code */
	u16	reserved2;

	__le32	svn_idx;		/*  SVN entry index */
	u32	reserved3;

	u32	reserved4;
	u32	reserved5;

	u8	data[0];
};

/*
 * 8723au/8192cu/8188ru required base power index offset tables.
 */
struct rtl8xxxu_power_base {
	u32 reg_0e00;
	u32 reg_0e04;
	u32 reg_0e08;
	u32 reg_086c;

	u32 reg_0e10;
	u32 reg_0e14;
	u32 reg_0e18;
	u32 reg_0e1c;

	u32 reg_0830;
	u32 reg_0834;
	u32 reg_0838;
	u32 reg_086c_2;

	u32 reg_083c;
	u32 reg_0848;
	u32 reg_084c;
	u32 reg_0868;
};

/*
 * The 8723au has 3 channel groups: 1-3, 4-9, and 10-14
 */
struct rtl8723au_idx {
#ifdef __LITTLE_ENDIAN
	int	a:4;
	int	b:4;
#else
	int	b:4;
	int	a:4;
#endif
} __attribute__((packed));

struct rtl8723au_efuse {
	__le16 rtl_id;
	u8 res0[0xe];
	u8 cck_tx_power_index_A[3];	/* 0x10 */
	u8 cck_tx_power_index_B[3];
	u8 ht40_1s_tx_power_index_A[3];	/* 0x16 */
	u8 ht40_1s_tx_power_index_B[3];
	/*
	 * The following entries are half-bytes split as:
	 * bits 0-3: path A, bits 4-7: path B, all values 4 bits signed
	 */
	struct rtl8723au_idx ht20_tx_power_index_diff[3];
	struct rtl8723au_idx ofdm_tx_power_index_diff[3];
	struct rtl8723au_idx ht40_max_power_offset[3];
	struct rtl8723au_idx ht20_max_power_offset[3];
	u8 channel_plan;		/* 0x28 */
	u8 tssi_a;
	u8 thermal_meter;
	u8 rf_regulatory;
	u8 rf_option_2;
	u8 rf_option_3;
	u8 rf_option_4;
	u8 res7;
	u8 version			/* 0x30 */;
	u8 customer_id_major;
	u8 customer_id_minor;
	u8 xtal_k;
	u8 chipset;			/* 0x34 */
	u8 res8[0x82];
	u8 vid;				/* 0xb7 */
	u8 res9;
	u8 pid;				/* 0xb9 */
	u8 res10[0x0c];
	u8 mac_addr[ETH_ALEN];		/* 0xc6 */
	u8 res11[2];
	u8 vendor_name[7];
	u8 res12[2];
	u8 device_name[0x29];		/* 0xd7 */
};

struct rtl8192cu_efuse {
	__le16 rtl_id;
	__le16 hpon;
	u8 res0[2];
	__le16 clk;
	__le16 testr;
	__le16 vid;
	__le16 did;
	__le16 svid;
	__le16 smid;						/* 0x10 */
	u8 res1[4];
	u8 mac_addr[ETH_ALEN];					/* 0x16 */
	u8 res2[2];
	u8 vendor_name[7];
	u8 res3[3];
	u8 device_name[0x14];					/* 0x28 */
	u8 res4[0x1e];						/* 0x3c */
	u8 cck_tx_power_index_A[3];				/* 0x5a */
	u8 cck_tx_power_index_B[3];
	u8 ht40_1s_tx_power_index_A[3];				/* 0x60 */
	u8 ht40_1s_tx_power_index_B[3];
	/*
	 * The following entries are half-bytes split as:
	 * bits 0-3: path A, bits 4-7: path B, all values 4 bits signed
	 */
	struct rtl8723au_idx ht40_2s_tx_power_index_diff[3];
	struct rtl8723au_idx ht20_tx_power_index_diff[3];	/* 0x69 */
	struct rtl8723au_idx ofdm_tx_power_index_diff[3];
	struct rtl8723au_idx ht40_max_power_offset[3];		/* 0x6f */
	struct rtl8723au_idx ht20_max_power_offset[3];
	u8 channel_plan;					/* 0x75 */
	u8 tssi_a;
	u8 tssi_b;
	u8 thermal_meter;	/* xtal_k */			/* 0x78 */
	u8 rf_regulatory;
	u8 rf_option_2;
	u8 rf_option_3;
	u8 rf_option_4;
	u8 res5[1];						/* 0x7d */
	u8 version;
	u8 customer_id;
};

struct rtl8723bu_pwr_idx {
#ifdef __LITTLE_ENDIAN
	int	ht20:4;
	int	ht40:4;
	int	ofdm:4;
	int	cck:4;
#else
	int	cck:4;
	int	ofdm:4;
	int	ht40:4;
	int	ht20:4;
#endif
} __attribute__((packed));

struct rtl8723bu_efuse_tx_power {
	u8 cck_base[6];
	u8 ht40_base[5];
	struct rtl8723au_idx ht20_ofdm_1s_diff;
	struct rtl8723bu_pwr_idx pwr_diff[3];
	u8 dummy5g[24]; /* max channel group (14) + power diff offset (10) */
};

struct rtl8723bu_efuse {
	__le16 rtl_id;
	u8 res0[0x0e];
	struct rtl8723bu_efuse_tx_power tx_power_index_A;	/* 0x10 */
	struct rtl8723bu_efuse_tx_power tx_power_index_B;	/* 0x3a */
	struct rtl8723bu_efuse_tx_power tx_power_index_C;	/* 0x64 */
	struct rtl8723bu_efuse_tx_power tx_power_index_D;	/* 0x8e */
	u8 channel_plan;		/* 0xb8 */
	u8 xtal_k;
	u8 thermal_meter;
	u8 iqk_lck;
	u8 pa_type;			/* 0xbc */
	u8 lna_type_2g;			/* 0xbd */
	u8 res2[3];
	u8 rf_board_option;
	u8 rf_feature_option;
	u8 rf_bt_setting;
	u8 eeprom_version;
	u8 eeprom_customer_id;
	u8 res3[2];
	u8 tx_pwr_calibrate_rate;
	u8 rf_antenna_option;		/* 0xc9 */
	u8 rfe_option;
	u8 res4[9];
	u8 usb_optional_function;
	u8 res5[0x1e];
	u8 res6[2];
	u8 serial[0x0b];		/* 0xf5 */
	u8 vid;				/* 0x100 */
	u8 res7;
	u8 pid;
	u8 res8[4];
	u8 mac_addr[ETH_ALEN];		/* 0x107 */
	u8 res9[2];
	u8 vendor_name[0x07];
	u8 res10[2];
	u8 device_name[0x14];
	u8 res11[0xcf];
	u8 package_type;		/* 0x1fb */
	u8 res12[0x4];
};

struct rtl8192eu_efuse_tx_power {
	u8 cck_base[6];
	u8 ht40_base[5];
	struct rtl8723au_idx ht20_ofdm_1s_diff;
	struct rtl8723bu_pwr_idx pwr_diff[3];
	u8 dummy5g[24]; /* max channel group (14) + power diff offset (10) */
};

struct rtl8192eu_efuse {
	__le16 rtl_id;
	u8 res0[0x0e];
	struct rtl8192eu_efuse_tx_power tx_power_index_A;	/* 0x10 */
	struct rtl8192eu_efuse_tx_power tx_power_index_B;	/* 0x3a */
	u8 res2[0x54];
	u8 channel_plan;		/* 0xb8 */
	u8 xtal_k;
	u8 thermal_meter;
	u8 iqk_lck;
	u8 pa_type;			/* 0xbc */
	u8 lna_type_2g;			/* 0xbd */
	u8 res3[1];
	u8 lna_type_5g;			/* 0xbf */
	u8 res4[1];
	u8 rf_board_option;
	u8 rf_feature_option;
	u8 rf_bt_setting;
	u8 eeprom_version;
	u8 eeprom_customer_id;
	u8 res5[3];
	u8 rf_antenna_option;		/* 0xc9 */
	u8 res6[6];
	u8 vid;				/* 0xd0 */
	u8 res7[1];
	u8 pid;				/* 0xd2 */
	u8 res8[1];
	u8 usb_optional_function;
	u8 res9[2];
	u8 mac_addr[ETH_ALEN];		/* 0xd7 */
	u8 res10[2];
	u8 vendor_name[7];
	u8 res11[2];
	u8 device_name[0x0b];		/* 0xe8 */
	u8 res12[2];
	u8 serial[0x0b];		/* 0xf5 */
	u8 res13[0x30];
	u8 unknown[0x0d];		/* 0x130 */
	u8 res14[0xc3];
};

struct rtl8xxxu_reg8val {
	u16 reg;
	u8 val;
};

struct rtl8xxxu_reg32val {
	u16 reg;
	u32 val;
};

struct rtl8xxxu_rfregval {
	u8 reg;
	u32 val;
};

enum rtl8xxxu_rfpath {
	RF_A = 0,
	RF_B = 1,
};

struct rtl8xxxu_rfregs {
	u16 hssiparm1;
	u16 hssiparm2;
	u16 lssiparm;
	u16 hspiread;
	u16 lssiread;
	u16 rf_sw_ctrl;
};

#define H2C_MAX_MBOX			4
#define H2C_EXT				BIT(7)
#define  H2C_JOIN_BSS_DISCONNECT	0
#define  H2C_JOIN_BSS_CONNECT		1

/*
 * H2C (firmware) commands differ between the older generation chips
 * 8188[cr]u, 819[12]cu, and 8723au, and the more recent chips 8723bu,
 * 8192[de]u, 8192eu, and 8812.
 */
enum h2c_cmd_8723a {
	H2C_SET_POWER_MODE = 1,
	H2C_JOIN_BSS_REPORT = 2,
	H2C_SET_RSSI = 5,
	H2C_SET_RATE_MASK = (6 | H2C_EXT),
};

enum h2c_cmd_8723b {
	/*
	 * Common Class: 000
	 */
	H2C_8723B_RSVD_PAGE = 0x00,
	H2C_8723B_MEDIA_STATUS_RPT = 0x01,
	H2C_8723B_SCAN_ENABLE = 0x02,
	H2C_8723B_KEEP_ALIVE = 0x03,
	H2C_8723B_DISCON_DECISION = 0x04,
	H2C_8723B_PSD_OFFLOAD = 0x05,
	H2C_8723B_AP_OFFLOAD = 0x08,
	H2C_8723B_BCN_RSVDPAGE = 0x09,
	H2C_8723B_PROBERSP_RSVDPAGE = 0x0A,
	H2C_8723B_FCS_RSVDPAGE = 0x10,
	H2C_8723B_FCS_INFO = 0x11,
	H2C_8723B_AP_WOW_GPIO_CTRL = 0x13,

	/*
	 * PoweSave Class: 001
	 */
	H2C_8723B_SET_PWR_MODE = 0x20,
	H2C_8723B_PS_TUNING_PARA = 0x21,
	H2C_8723B_PS_TUNING_PARA2 = 0x22,
	H2C_8723B_P2P_LPS_PARAM = 0x23,
	H2C_8723B_P2P_PS_OFFLOAD = 0x24,
	H2C_8723B_PS_SCAN_ENABLE = 0x25,
	H2C_8723B_SAP_PS_ = 0x26,
	H2C_8723B_INACTIVE_PS_ = 0x27,
	H2C_8723B_FWLPS_IN_IPS_ = 0x28,

	/*
	 * Dynamic Mechanism Class: 010
	 */
	H2C_8723B_MACID_CFG_RAID = 0x40,
	H2C_8723B_TXBF = 0x41,
	H2C_8723B_RSSI_SETTING = 0x42,
	H2C_8723B_AP_REQ_TXRPT = 0x43,
	H2C_8723B_INIT_RATE_COLLECT = 0x44,

	/*
	 * BT Class: 011
	 */
	H2C_8723B_B_TYPE_TDMA = 0x60,
	H2C_8723B_BT_INFO = 0x61,
	H2C_8723B_FORCE_BT_TXPWR = 0x62,
	H2C_8723B_BT_IGNORE_WLANACT = 0x63,
	H2C_8723B_DAC_SWING_VALUE = 0x64,
	H2C_8723B_ANT_SEL_RSV = 0x65,
	H2C_8723B_WL_OPMODE = 0x66,
	H2C_8723B_BT_MP_OPER = 0x67,
	H2C_8723B_BT_CONTROL = 0x68,
	H2C_8723B_BT_WIFI_CTRL = 0x69,
	H2C_8723B_BT_FW_PATCH = 0x6a,
	H2C_8723B_BT_WLAN_CALIBRATION = 0x6d,
	H2C_8723B_BT_GRANT = 0x6e,

	/*
	 * WOWLAN Class: 100
	 */
	H2C_8723B_WOWLAN = 0x80,
	H2C_8723B_REMOTE_WAKE_CTRL = 0x81,
	H2C_8723B_AOAC_GLOBAL_INFO = 0x82,
	H2C_8723B_AOAC_RSVD_PAGE = 0x83,
	H2C_8723B_AOAC_RSVD_PAGE2 = 0x84,
	H2C_8723B_D0_SCAN_OFFLOAD_CTRL = 0x85,
	H2C_8723B_D0_SCAN_OFFLOAD_INFO = 0x86,
	H2C_8723B_CHNL_SWITCH_OFFLOAD = 0x87,

	H2C_8723B_RESET_TSF = 0xC0,
};


struct h2c_cmd {
	union {
		struct {
			u8 cmd;
			u8 data[7];
		} __packed cmd;
		struct {
			__le32 data;
			__le16 ext;
		} __packed raw;
		struct {
			__le32 data;
			__le32 ext;
		} __packed raw_wide;
		struct {
			u8 cmd;
			u8 data;
		} __packed joinbss;
		struct {
			u8 cmd;
			__le16 mask_hi;
			u8 arg;
			__le16 mask_lo;
		} __packed ramask;
		struct {
			u8 cmd;
			u8 parm;
			u8 macid;
			u8 macid_end;
		} __packed media_status_rpt;
		struct {
			u8 cmd;
			u8 macid;
			/*
			 * [0:4] - RAID
			 * [7]   - SGI
			 */
			u8 data1;
			/*
			 * [0:1] - Bandwidth
			 * [3]   - No Update
			 * [4:5] - VHT enable
			 * [6]   - DISPT
			 * [7]   - DISRA
			 */
			u8 data2;
			u8 ramask0;
			u8 ramask1;
			u8 ramask2;
			u8 ramask3;
		} __packed b_macid_cfg;
		struct {
			u8 cmd;
			u8 data1;
			u8 data2;
			u8 data3;
			u8 data4;
			u8 data5;
		} __packed b_type_dma;
		struct {
			u8 cmd;
			u8 data;
		} __packed bt_info;
		struct {
			u8 cmd;
			u8 operreq;
			u8 opcode;
			u8 data;
			u8 addr;
		} __packed bt_mp_oper;
		struct {
			u8 cmd;
			u8 data;
		} __packed bt_wlan_calibration;
		struct {
			u8 cmd;
			u8 data;
		} __packed ignore_wlan;
		struct {
			u8 cmd;
			u8 ant_inverse;
			u8 int_switch_type;
		} __packed ant_sel_rsv;
		struct {
			u8 cmd;
			u8 data;
		} __packed bt_grant;
	};
};

enum c2h_evt_8723b {
	C2H_8723B_DEBUG = 0,
	C2H_8723B_TSF = 1,
	C2H_8723B_AP_RPT_RSP = 2,
	C2H_8723B_CCX_TX_RPT = 3,
	C2H_8723B_BT_RSSI = 4,
	C2H_8723B_BT_OP_MODE = 5,
	C2H_8723B_EXT_RA_RPT = 6,
	C2H_8723B_BT_INFO = 9,
	C2H_8723B_HW_INFO_EXCH = 0x0a,
	C2H_8723B_BT_MP_INFO = 0x0b,
	C2H_8723B_RA_REPORT = 0x0c,
	C2H_8723B_FW_DEBUG = 0xff,
};

enum bt_info_src_8723b {
	BT_INFO_SRC_8723B_WIFI_FW = 0x0,
        BT_INFO_SRC_8723B_BT_RSP = 0x1,
        BT_INFO_SRC_8723B_BT_ACTIVE_SEND = 0x2,
};

enum bt_mp_oper_opcode_8723b {
	BT_MP_OP_GET_BT_VERSION	= 0x00,
	BT_MP_OP_RESET = 0x01,
	BT_MP_OP_TEST_CTRL = 0x02,
	BT_MP_OP_SET_BT_MODE = 0x03,
	BT_MP_OP_SET_CHNL_TX_GAIN = 0x04,
	BT_MP_OP_SET_PKT_TYPE_LEN = 0x05,
	BT_MP_OP_SET_PKT_CNT_L_PL_TYPE = 0x06,
	BT_MP_OP_SET_PKT_CNT_H_PKT_INTV = 0x07,
	BT_MP_OP_SET_PKT_HEADER = 0x08,
	BT_MP_OP_SET_WHITENCOEFF = 0x09,
	BT_MP_OP_SET_BD_ADDR_L = 0x0a,
	BT_MP_OP_SET_BD_ADDR_H = 0x0b,
	BT_MP_OP_WRITE_REG_ADDR = 0x0c,
	BT_MP_OP_WRITE_REG_VALUE = 0x0d,
	BT_MP_OP_GET_BT_STATUS = 0x0e,
	BT_MP_OP_GET_BD_ADDR_L = 0x0f,
	BT_MP_OP_GET_BD_ADDR_H = 0x10,
	BT_MP_OP_READ_REG = 0x11,
	BT_MP_OP_SET_TARGET_BD_ADDR_L = 0x12,
	BT_MP_OP_SET_TARGET_BD_ADDR_H = 0x13,
	BT_MP_OP_SET_TX_POWER_CALIBRATION = 0x14,
	BT_MP_OP_GET_RX_PKT_CNT_L = 0x15,
	BT_MP_OP_GET_RX_PKT_CNT_H = 0x16,
	BT_MP_OP_GET_RX_ERROR_BITS_L = 0x17,
	BT_MP_OP_GET_RX_ERROR_BITS_H = 0x18,
	BT_MP_OP_GET_RSSI = 0x19,
	BT_MP_OP_GET_CFO_HDR_QUALITY_L = 0x1a,
	BT_MP_OP_GET_CFO_HDR_QUALITY_H = 0x1b,
	BT_MP_OP_GET_TARGET_BD_ADDR_L = 0x1c,
	BT_MP_OP_GET_TARGET_BD_ADDR_H = 0x1d,
	BT_MP_OP_GET_AFH_MAP_L = 0x1e,
	BT_MP_OP_GET_AFH_MAP_M = 0x1f,
	BT_MP_OP_GET_AFH_MAP_H = 0x20,
	BT_MP_OP_GET_AFH_STATUS = 0x21,
	BT_MP_OP_SET_TRACKING_INTERVAL = 0x22,
	BT_MP_OP_SET_THERMAL_METER = 0x23,
	BT_MP_OP_ENABLE_CFO_TRACKING = 0x24,
};

struct rtl8723bu_c2h {
	u8 id;
	u8 seq;
	union {
		struct {
			u8 payload[0];
		} __packed raw;
		struct {
			u8 ext_id;
			u8 status:4;
			u8 retlen:4;
			u8 opcode_ver:4;
			u8 req_num:4;
			u8 payload[2];
		} __packed bt_mp_info;
		struct {
			u8 response_source:4;
			u8 dummy0_0:4;

			u8 bt_info;

			u8 retry_count:4;
			u8 dummy2_0:1;
			u8 bt_page:1;
			u8 tx_rx_mask:1;
			u8 dummy2_2:1;

			u8 rssi;

			u8 basic_rate:1;
			u8 bt_has_reset:1;
			u8 dummy4_1:1;;
			u8 ignore_wlan:1;
			u8 auto_report:1;
			u8 dummy4_2:3;

			u8 a4;
			u8 a5;
		} __packed bt_info;
		struct {
			u8 rate:7;
			u8 dummy0_0:1;
			u8 macid;
			u8 ldpc:1;
			u8 txbf:1;
			u8 noisy_state:1;
			u8 dummy2_0:5;
			u8 dummy3_0;
		} __packed ra_report;
	};
};

struct rtl8xxxu_fileops;

struct rtl8xxxu_priv {
	struct ieee80211_hw *hw;
	struct usb_device *udev;
	struct rtl8xxxu_fileops *fops;

	spinlock_t tx_urb_lock;
	struct list_head tx_urb_free_list;
	int tx_urb_free_count;
	bool tx_stopped;

	spinlock_t rx_urb_lock;
	struct list_head rx_urb_pending_list;
	int rx_urb_pending_count;
	bool shutdown;
	struct work_struct rx_urb_wq;

	u8 mac_addr[ETH_ALEN];
	char chip_name[8];
	char chip_vendor[8];
	u8 cck_tx_power_index_A[RTL8XXXU_MAX_CHANNEL_GROUPS];
	u8 cck_tx_power_index_B[RTL8XXXU_MAX_CHANNEL_GROUPS];
	u8 ht40_1s_tx_power_index_A[RTL8XXXU_MAX_CHANNEL_GROUPS];
	u8 ht40_1s_tx_power_index_B[RTL8XXXU_MAX_CHANNEL_GROUPS];
	/*
	 * The following entries are half-bytes split as:
	 * bits 0-3: path A, bits 4-7: path B, all values 4 bits signed
	 */
	struct rtl8723au_idx ht40_2s_tx_power_index_diff[
		RTL8723A_CHANNEL_GROUPS];
	struct rtl8723au_idx ht20_tx_power_index_diff[RTL8723A_CHANNEL_GROUPS];
	struct rtl8723au_idx ofdm_tx_power_index_diff[RTL8723A_CHANNEL_GROUPS];
	struct rtl8723au_idx ht40_max_power_offset[RTL8723A_CHANNEL_GROUPS];
	struct rtl8723au_idx ht20_max_power_offset[RTL8723A_CHANNEL_GROUPS];
	/*
	 * Newer generation chips only keep power diffs per TX count,
	 * not per channel group.
	 */
	struct rtl8723au_idx ofdm_tx_power_diff[RTL8723B_TX_COUNT];
	struct rtl8723au_idx ht20_tx_power_diff[RTL8723B_TX_COUNT];
	struct rtl8723au_idx ht40_tx_power_diff[RTL8723B_TX_COUNT];
	struct rtl8xxxu_power_base *power_base;
	u32 chip_cut:4;
	u32 rom_rev:4;
	u32 is_multi_func:1;
	u32 has_wifi:1;
	u32 has_bluetooth:1;
	u32 enable_bluetooth:1;
	u32 has_gps:1;
	u32 hi_pa:1;
	u32 vendor_umc:1;
	u32 vendor_smic:1;
	u32 has_polarity_ctrl:1;
	u32 has_eeprom:1;
	u32 boot_eeprom:1;
	u32 usb_interrupts:1;
	u32 ep_tx_high_queue:1;
	u32 ep_tx_normal_queue:1;
	u32 ep_tx_low_queue:1;
	u32 has_xtalk:1;
	u32 rx_buf_aggregation:1;
	u8 xtalk;
	unsigned int pipe_interrupt;
	unsigned int pipe_in;
	unsigned int pipe_out[TXDESC_QUEUE_MAX];
	u8 out_ep[RTL8XXXU_OUT_ENDPOINTS];
	u8 ep_tx_count;
	u8 rf_paths;
	u8 rx_paths;
	u8 tx_paths;
	u32 rege94;
	u32 rege9c;
	u32 regeb4;
	u32 regebc;
	int next_mbox;
	int nr_out_eps;

	struct mutex h2c_mutex;

	struct usb_anchor rx_anchor;
	struct usb_anchor tx_anchor;
	struct usb_anchor int_anchor;
	struct rtl8xxxu_firmware_header *fw_data;
	size_t fw_size;
	struct mutex usb_buf_mutex;
	union {
		__le32 val32;
		__le16 val16;
		u8 val8;
	} usb_buf;
	union {
		u8 raw[EFUSE_MAP_LEN];
		struct rtl8723au_efuse efuse8723;
		struct rtl8723bu_efuse efuse8723bu;
		struct rtl8192cu_efuse efuse8192;
		struct rtl8192eu_efuse efuse8192eu;
	} efuse_wifi;
	u32 adda_backup[RTL8XXXU_ADDA_REGS];
	u32 mac_backup[RTL8XXXU_MAC_REGS];
	u32 bb_backup[RTL8XXXU_BB_REGS];
	u32 bb_recovery_backup[RTL8XXXU_BB_REGS];
	enum rtl8xxxu_rtl_chip rtl_chip;
	u8 pi_enabled:1;
	u8 no_pape:1;
	u8 int_buf[USB_INTR_CONTENT_LENGTH];
};

struct rtl8xxxu_rx_urb {
	struct urb urb;
	struct ieee80211_hw *hw;
	struct list_head list;
};

struct rtl8xxxu_tx_urb {
	struct urb urb;
	struct ieee80211_hw *hw;
	struct list_head list;
};

struct rtl8xxxu_fileops {
	int (*parse_efuse) (struct rtl8xxxu_priv *priv);
	int (*load_firmware) (struct rtl8xxxu_priv *priv);
	int (*power_on) (struct rtl8xxxu_priv *priv);
	void (*power_off) (struct rtl8xxxu_priv *priv);
	void (*reset_8051) (struct rtl8xxxu_priv *priv);
	int (*llt_init) (struct rtl8xxxu_priv *priv);
	void (*init_phy_bb) (struct rtl8xxxu_priv *priv);
	int (*init_phy_rf) (struct rtl8xxxu_priv *priv);
	void (*phy_init_antenna_selection) (struct rtl8xxxu_priv *priv);
	void (*phy_iq_calibrate) (struct rtl8xxxu_priv *priv);
	void (*config_channel) (struct ieee80211_hw *hw);
	int (*parse_rx_desc) (struct rtl8xxxu_priv *priv, struct sk_buff *skb);
	void (*init_aggregation) (struct rtl8xxxu_priv *priv);
	void (*init_statistics) (struct rtl8xxxu_priv *priv);
	void (*enable_rf) (struct rtl8xxxu_priv *priv);
	void (*disable_rf) (struct rtl8xxxu_priv *priv);
	void (*usb_quirks) (struct rtl8xxxu_priv *priv);
	void (*set_tx_power) (struct rtl8xxxu_priv *priv, int channel,
			      bool ht40);
	void (*update_rate_mask) (struct rtl8xxxu_priv *priv,
				  u32 ramask, int sgi);
	void (*report_connect) (struct rtl8xxxu_priv *priv,
				u8 macid, bool connect);
	void (*fill_txdesc) (struct ieee80211_hdr *hdr,
			     struct rtl8xxxu_txdesc32 *tx_desc, u32 rate,
			     u16 rate_flag, bool sgi, bool short_preamble,
			     bool ampdu_enable);
	int writeN_block_size;
	int rx_agg_buf_size;
	char tx_desc_size;
	char rx_desc_size;
	u8 has_s0s1:1;
	u8 has_tx_report:1;
	u8 gen2_thermal_meter:1;
	u32 adda_1t_init;
	u32 adda_1t_path_on;
	u32 adda_2t_path_on_a;
	u32 adda_2t_path_on_b;
	u16 trxff_boundary;
	u8 pbp_rx;
	u8 pbp_tx;
	struct rtl8xxxu_reg8val *mactable;
	u8 total_page_num;
	u8 page_num_hi;
	u8 page_num_lo;
	u8 page_num_norm;
};

extern int rtl8xxxu_debug;

extern struct rtl8xxxu_reg8val rtl8xxxu_gen1_mac_init_table[];
extern const u32 rtl8xxxu_iqk_phy_iq_bb_reg[];
u8 rtl8xxxu_read8(struct rtl8xxxu_priv *priv, u16 addr);
u16 rtl8xxxu_read16(struct rtl8xxxu_priv *priv, u16 addr);
u32 rtl8xxxu_read32(struct rtl8xxxu_priv *priv, u16 addr);
int rtl8xxxu_write8(struct rtl8xxxu_priv *priv, u16 addr, u8 val);
int rtl8xxxu_write16(struct rtl8xxxu_priv *priv, u16 addr, u16 val);
int rtl8xxxu_write32(struct rtl8xxxu_priv *priv, u16 addr, u32 val);
u32 rtl8xxxu_read_rfreg(struct rtl8xxxu_priv *priv,
			enum rtl8xxxu_rfpath path, u8 reg);
int rtl8xxxu_write_rfreg(struct rtl8xxxu_priv *priv,
			 enum rtl8xxxu_rfpath path, u8 reg, u32 data);
void rtl8xxxu_save_regs(struct rtl8xxxu_priv *priv, const u32 *regs,
			u32 *backup, int count);
void rtl8xxxu_restore_regs(struct rtl8xxxu_priv *priv, const u32 *regs,
			   u32 *backup, int count);
void rtl8xxxu_save_mac_regs(struct rtl8xxxu_priv *priv,
			    const u32 *reg, u32 *backup);
void rtl8xxxu_restore_mac_regs(struct rtl8xxxu_priv *priv,
			       const u32 *reg, u32 *backup);
void rtl8xxxu_path_adda_on(struct rtl8xxxu_priv *priv, const u32 *regs,
			   bool path_a_on);
void rtl8xxxu_mac_calibration(struct rtl8xxxu_priv *priv,
			      const u32 *regs, u32 *backup);
void rtl8xxxu_fill_iqk_matrix_a(struct rtl8xxxu_priv *priv, bool iqk_ok,
				int result[][8], int candidate, bool tx_only);
void rtl8xxxu_fill_iqk_matrix_b(struct rtl8xxxu_priv *priv, bool iqk_ok,
				int result[][8], int candidate, bool tx_only);
int rtl8xxxu_init_phy_rf(struct rtl8xxxu_priv *priv,
			 struct rtl8xxxu_rfregval *table,
			 enum rtl8xxxu_rfpath path);
int rtl8xxxu_init_phy_regs(struct rtl8xxxu_priv *priv,
			   struct rtl8xxxu_reg32val *array);
int rtl8xxxu_load_firmware(struct rtl8xxxu_priv *priv, char *fw_name);
void rtl8xxxu_firmware_self_reset(struct rtl8xxxu_priv *priv);
void rtl8xxxu_power_off(struct rtl8xxxu_priv *priv);
void rtl8xxxu_reset_8051(struct rtl8xxxu_priv *priv);
int rtl8xxxu_auto_llt_table(struct rtl8xxxu_priv *priv);
void rtl8xxxu_gen2_prepare_calibrate(struct rtl8xxxu_priv *priv, u8 start);
int rtl8xxxu_flush_fifo(struct rtl8xxxu_priv *priv);
int rtl8xxxu_gen2_h2c_cmd(struct rtl8xxxu_priv *priv,
			  struct h2c_cmd *h2c, int len);
int rtl8xxxu_active_to_lps(struct rtl8xxxu_priv *priv);
void rtl8xxxu_disabled_to_emu(struct rtl8xxxu_priv *priv);
int rtl8xxxu_init_llt_table(struct rtl8xxxu_priv *priv);
void rtl8xxxu_gen1_phy_iq_calibrate(struct rtl8xxxu_priv *priv);
void rtl8xxxu_gen1_init_phy_bb(struct rtl8xxxu_priv *priv);
void rtl8xxxu_gen1_set_tx_power(struct rtl8xxxu_priv *priv,
				int channel, bool ht40);
void rtl8xxxu_gen1_config_channel(struct ieee80211_hw *hw);
void rtl8xxxu_gen2_config_channel(struct ieee80211_hw *hw);
void rtl8xxxu_gen1_usb_quirks(struct rtl8xxxu_priv *priv);
void rtl8xxxu_gen2_usb_quirks(struct rtl8xxxu_priv *priv);
void rtl8xxxu_update_rate_mask(struct rtl8xxxu_priv *priv,
			       u32 ramask, int sgi);
void rtl8xxxu_gen2_update_rate_mask(struct rtl8xxxu_priv *priv,
				    u32 ramask, int sgi);
void rtl8xxxu_gen1_report_connect(struct rtl8xxxu_priv *priv,
				  u8 macid, bool connect);
void rtl8xxxu_gen2_report_connect(struct rtl8xxxu_priv *priv,
				  u8 macid, bool connect);
void rtl8xxxu_gen1_init_aggregation(struct rtl8xxxu_priv *priv);
void rtl8xxxu_gen1_enable_rf(struct rtl8xxxu_priv *priv);
void rtl8xxxu_gen1_disable_rf(struct rtl8xxxu_priv *priv);
void rtl8xxxu_gen2_disable_rf(struct rtl8xxxu_priv *priv);
int rtl8xxxu_parse_rxdesc16(struct rtl8xxxu_priv *priv, struct sk_buff *skb);
int rtl8xxxu_parse_rxdesc24(struct rtl8xxxu_priv *priv, struct sk_buff *skb);
int rtl8xxxu_gen2_channel_to_group(int channel);
bool rtl8xxxu_gen2_simularity_compare(struct rtl8xxxu_priv *priv,
				      int result[][8], int c1, int c2);
void rtl8xxxu_fill_txdesc_v1(struct ieee80211_hdr *hdr,
			     struct rtl8xxxu_txdesc32 *tx_desc, u32 rate,
			     u16 rate_flag, bool sgi, bool short_preamble,
			     bool ampdu_enable);
void rtl8xxxu_fill_txdesc_v2(struct ieee80211_hdr *hdr,
			     struct rtl8xxxu_txdesc32 *tx_desc32, u32 rate,
			     u16 rate_flag, bool sgi, bool short_preamble,
			     bool ampdu_enable);

extern struct rtl8xxxu_fileops rtl8192cu_fops;
extern struct rtl8xxxu_fileops rtl8192eu_fops;
extern struct rtl8xxxu_fileops rtl8723au_fops;
extern struct rtl8xxxu_fileops rtl8723bu_fops;
