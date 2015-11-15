/*
 * Copyright (c) 2014 - 2015 Jes Sorensen <Jes.Sorensen@redhat.com>
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

#define RTL8XXXU_OUT_ENDPOINTS		3

#define REALTEK_USB_READ		0xc0
#define REALTEK_USB_WRITE		0x40
#define REALTEK_USB_CMD_REQ		0x05
#define REALTEK_USB_CMD_IDX		0x00

#define TX_TOTAL_PAGE_NUM		0xf8
/* (HPQ + LPQ + NPQ + PUBQ) = TX_TOTAL_PAGE_NUM */
#define TX_PAGE_NUM_PUBQ		0xe7
#define TX_PAGE_NUM_HI_PQ		0x0c
#define TX_PAGE_NUM_LO_PQ		0x02
#define TX_PAGE_NUM_NORM_PQ		0x02

#define RTL_FW_PAGE_SIZE		4096
#define RTL8XXXU_FIRMWARE_POLL_MAX	1000

#define RTL8723A_CHANNEL_GROUPS		3
#define RTL8723A_MAX_RF_PATHS		2
#define RF6052_MAX_TX_PWR		0x3f

#define EFUSE_MAP_LEN_8723A		256
#define EFUSE_MAX_SECTION_8723A		32
#define EFUSE_REAL_CONTENT_LEN_8723A	512
#define EFUSE_BT_MAP_LEN_8723A		1024
#define EFUSE_MAX_WORD_UNIT		4

struct rtl8xxxu_rx_desc {
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
	u32 nextpktlen:14;
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
	u32 nextpktlen:14;
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

struct rtl8xxxu_tx_desc {
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
#define TXDESC_LAST_SEGMENT		BIT(2)
#define TXDESC_FIRST_SEGMENT		BIT(3)
#define TXDESC_OWN			BIT(7)
#endif

/* Word 1 */
#define TXDESC_PKT_OFFSET_SZ		0
#define TXDESC_AGG_ENABLE		BIT(5)
#define TXDESC_BK			BIT(6)
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

#define DESC_RATE_ID_SHIFT		16
#define DESC_RATE_ID_MASK		0xf
#define TXDESC_NAVUSEHDR		BIT(20)
#define TXDESC_SEC_RC4			0x00400000
#define TXDESC_SEC_AES			0x00c00000
#define TXDESC_PKT_OFFSET_SHIFT		26
#define TXDESC_AGG_EN			BIT(29)
#define TXDESC_HWPC			BIT(31)

/* Word 2 */
#define TXDESC_ACK_REPORT		BIT(19)
#define TXDESC_AMPDU_DENSITY_SHIFT	20

/* Word 3 */
#define TXDESC_SEQ_SHIFT		16
#define TXDESC_SEQ_MASK			0x0fff0000

/* Word 4 */
#define TXDESC_QOS			BIT(6)
#define TXDESC_HW_SEQ_ENABLE		BIT(7)
#define TXDESC_USE_DRIVER_RATE		BIT(8)
#define TXDESC_DISABLE_DATA_FB		BIT(10)
#define TXDESC_CTS_SELF_ENABLE		BIT(11)
#define TXDESC_RTS_CTS_ENABLE		BIT(12)
#define TXDESC_HW_RTS_ENABLE		BIT(13)
#define TXDESC_PRIME_CH_OFF_LOWER	BIT(20)
#define TXDESC_PRIME_CH_OFF_UPPER	BIT(21)
#define TXDESC_SHORT_PREAMBLE		BIT(24)
#define TXDESC_DATA_BW			BIT(25)
#define TXDESC_RTS_DATA_BW		BIT(27)
#define TXDESC_RTS_PRIME_CH_OFF_LOWER	BIT(28)
#define TXDESC_RTS_PRIME_CH_OFF_UPPER	BIT(29)

/* Word 5 */
#define TXDESC_RTS_RATE_SHIFT		0
#define TXDESC_RTS_RATE_MASK		0x3f
#define TXDESC_SHORT_GI			BIT(6)
#define TXDESC_CCX_TAG			BIT(7)
#define TXDESC_RETRY_LIMIT_ENABLE	BIT(17)
#define TXDESC_RETRY_LIMIT_SHIFT	18
#define TXDESC_RETRY_LIMIT_MASK		0x00fc0000

/* Word 6 */
#define TXDESC_MAX_AGG_SHIFT		11

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
#define H2C_SET_POWER_MODE		1
#define H2C_JOIN_BSS_REPORT		2
#define  H2C_JOIN_BSS_DISCONNECT	0
#define  H2C_JOIN_BSS_CONNECT		1
#define H2C_SET_RSSI			5
#define H2C_SET_RATE_MASK		(6 | H2C_EXT)

struct h2c_cmd {
	union {
		struct {
			u8 cmd;
			u8 data[5];
		} __packed cmd;
		struct {
			__le32 data;
			__le16 ext;
		} __packed raw;
		struct {
			u8 cmd;
			u8 data;
			u8 pad[4];
		} __packed joinbss;
		struct {
			u8 cmd;
			__le16 mask_hi;
			u8 arg;
			__le16 mask_lo;
		} __packed ramask;
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
	u8 cck_tx_power_index_A[3];	/* 0x10 */
	u8 cck_tx_power_index_B[3];
	u8 ht40_1s_tx_power_index_A[3];	/* 0x16 */
	u8 ht40_1s_tx_power_index_B[3];
	/*
	 * The following entries are half-bytes split as:
	 * bits 0-3: path A, bits 4-7: path B, all values 4 bits signed
	 */
	struct rtl8723au_idx ht40_2s_tx_power_index_diff[3];
	struct rtl8723au_idx ht20_tx_power_index_diff[3];
	struct rtl8723au_idx ofdm_tx_power_index_diff[3];
	struct rtl8723au_idx ht40_max_power_offset[3];
	struct rtl8723au_idx ht20_max_power_offset[3];
	u32 chip_cut:4;
	u32 rom_rev:4;
	u32 has_wifi:1;
	u32 has_bluetooth:1;
	u32 enable_bluetooth:1;
	u32 has_gps:1;
	u32 hi_pa:1;
	u32 vendor_umc:1;
	u32 has_polarity_ctrl:1;
	u32 has_eeprom:1;
	u32 boot_eeprom:1;
	u32 ep_tx_high_queue:1;
	u32 ep_tx_normal_queue:1;
	u32 ep_tx_low_queue:1;
	u32 path_a_hi_power:1;
	u32 path_a_rf_paths:4;
	unsigned int pipe_interrupt;
	unsigned int pipe_in;
	unsigned int pipe_out[TXDESC_QUEUE_MAX];
	u8 out_ep[RTL8XXXU_OUT_ENDPOINTS];
	u8 path_a_ig_value;
	u8 ep_tx_count;
	u8 rf_paths;
	u8 rx_paths;
	u8 tx_paths;
	u32 rf_mode_ag[2];
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
		u8 raw[EFUSE_MAP_LEN_8723A];
		struct rtl8723au_efuse efuse8723;
		struct rtl8192cu_efuse efuse8192;
	} efuse_wifi;
	u32 adda_backup[RTL8XXXU_ADDA_REGS];
	u32 mac_backup[RTL8XXXU_MAC_REGS];
	u32 bb_backup[RTL8XXXU_BB_REGS];
	u32 bb_recovery_backup[RTL8XXXU_BB_REGS];
	u32 rtlchip;
	u8 pi_enabled:1;
	u8 iqk_initialized:1;
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
	int writeN_block_size;
};
