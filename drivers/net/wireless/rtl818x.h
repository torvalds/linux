/*
 * Definitions for RTL818x hardware
 *
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andreamrl@tiscali.it>
 *
 * Based on the r8187 driver, which is:
 * Copyright 2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RTL818X_H
#define RTL818X_H

struct rtl818x_csr {
	u8	MAC[6];
	u8	reserved_0[2];
	__le32	MAR[2];
	u8	RX_FIFO_COUNT;
	u8	reserved_1;
	u8	TX_FIFO_COUNT;
	u8	BQREQ;
	u8	reserved_2[4];
	__le32	TSFT[2];
	__le32	TLPDA;
	__le32	TNPDA;
	__le32	THPDA;
	__le16	BRSR;
	u8	BSSID[6];
	u8	RESP_RATE;
	u8	EIFS;
	u8	reserved_3[1];
	u8	CMD;
#define RTL818X_CMD_TX_ENABLE		(1 << 2)
#define RTL818X_CMD_RX_ENABLE		(1 << 3)
#define RTL818X_CMD_RESET		(1 << 4)
	u8	reserved_4[4];
	__le16	INT_MASK;
	__le16	INT_STATUS;
#define RTL818X_INT_RX_OK		(1 <<  0)
#define RTL818X_INT_RX_ERR		(1 <<  1)
#define RTL818X_INT_TXL_OK		(1 <<  2)
#define RTL818X_INT_TXL_ERR		(1 <<  3)
#define RTL818X_INT_RX_DU		(1 <<  4)
#define RTL818X_INT_RX_FO		(1 <<  5)
#define RTL818X_INT_TXN_OK		(1 <<  6)
#define RTL818X_INT_TXN_ERR		(1 <<  7)
#define RTL818X_INT_TXH_OK		(1 <<  8)
#define RTL818X_INT_TXH_ERR		(1 <<  9)
#define RTL818X_INT_TXB_OK		(1 << 10)
#define RTL818X_INT_TXB_ERR		(1 << 11)
#define RTL818X_INT_ATIM		(1 << 12)
#define RTL818X_INT_BEACON		(1 << 13)
#define RTL818X_INT_TIME_OUT		(1 << 14)
#define RTL818X_INT_TX_FO		(1 << 15)
	__le32	TX_CONF;
#define RTL818X_TX_CONF_LOOPBACK_MAC	(1 << 17)
#define RTL818X_TX_CONF_NO_ICV		(1 << 19)
#define RTL818X_TX_CONF_DISCW		(1 << 20)
#define RTL818X_TX_CONF_R8180_ABCD	(2 << 25)
#define RTL818X_TX_CONF_R8180_F		(3 << 25)
#define RTL818X_TX_CONF_R8185_ABC	(4 << 25)
#define RTL818X_TX_CONF_R8185_D		(5 << 25)
#define RTL818X_TX_CONF_HWVER_MASK	(7 << 25)
#define RTL818X_TX_CONF_CW_MIN		(1 << 31)
	__le32	RX_CONF;
#define RTL818X_RX_CONF_MONITOR		(1 <<  0)
#define RTL818X_RX_CONF_NICMAC		(1 <<  1)
#define RTL818X_RX_CONF_MULTICAST	(1 <<  2)
#define RTL818X_RX_CONF_BROADCAST	(1 <<  3)
#define RTL818X_RX_CONF_FCS		(1 <<  5)
#define RTL818X_RX_CONF_DATA		(1 << 18)
#define RTL818X_RX_CONF_CTRL		(1 << 19)
#define RTL818X_RX_CONF_MGMT		(1 << 20)
#define RTL818X_RX_CONF_BSSID		(1 << 23)
#define RTL818X_RX_CONF_RX_AUTORESETPHY	(1 << 28)
#define RTL818X_RX_CONF_ONLYERLPKT	(1 << 31)
	__le32	INT_TIMEOUT;
	__le32	TBDA;
	u8	EEPROM_CMD;
#define RTL818X_EEPROM_CMD_READ		(1 << 0)
#define RTL818X_EEPROM_CMD_WRITE	(1 << 1)
#define RTL818X_EEPROM_CMD_CK		(1 << 2)
#define RTL818X_EEPROM_CMD_CS		(1 << 3)
#define RTL818X_EEPROM_CMD_NORMAL	(0 << 6)
#define RTL818X_EEPROM_CMD_LOAD		(1 << 6)
#define RTL818X_EEPROM_CMD_PROGRAM	(2 << 6)
#define RTL818X_EEPROM_CMD_CONFIG	(3 << 6)
	u8	CONFIG0;
	u8	CONFIG1;
	u8	CONFIG2;
	__le32	ANAPARAM;
	u8	MSR;
#define RTL818X_MSR_NO_LINK		(0 << 2)
#define RTL818X_MSR_ADHOC		(1 << 2)
#define RTL818X_MSR_INFRA		(2 << 2)
	u8	CONFIG3;
#define RTL818X_CONFIG3_ANAPARAM_WRITE	(1 << 6)
	u8	CONFIG4;
#define RTL818X_CONFIG4_POWEROFF	(1 << 6)
#define RTL818X_CONFIG4_VCOOFF		(1 << 7)
	u8	TESTR;
	u8	reserved_9[2];
	__le16	PGSELECT;
	__le32	ANAPARAM2;
	u8	reserved_10[12];
	__le16	BEACON_INTERVAL;
	__le16	ATIM_WND;
	__le16	BEACON_INTERVAL_TIME;
	__le16	ATIMTR_INTERVAL;
	u8	reserved_11[4];
	u8	PHY[4];
	__le16	RFPinsOutput;
	__le16	RFPinsEnable;
	__le16	RFPinsSelect;
	__le16	RFPinsInput;
	__le32	RF_PARA;
	__le32	RF_TIMING;
	u8	GP_ENABLE;
	u8	GPIO;
	u8	reserved_12[10];
	u8	TX_AGC_CTL;
#define RTL818X_TX_AGC_CTL_PERPACKET_GAIN_SHIFT		(1 << 0)
#define RTL818X_TX_AGC_CTL_PERPACKET_ANTSEL_SHIFT	(1 << 1)
#define RTL818X_TX_AGC_CTL_FEEDBACK_ANT			(1 << 2)
	u8	TX_GAIN_CCK;
	u8	TX_GAIN_OFDM;
	u8	TX_ANTENNA;
	u8	reserved_13[16];
	u8	WPA_CONF;
	u8	reserved_14[3];
	u8	SIFS;
	u8	DIFS;
	u8	SLOT;
	u8	reserved_15[5];
	u8	CW_CONF;
#define RTL818X_CW_CONF_PERPACKET_CW_SHIFT	(1 << 0)
#define RTL818X_CW_CONF_PERPACKET_RETRY_SHIFT	(1 << 1)
	u8	CW_VAL;
	u8	RATE_FALLBACK;
	u8	reserved_16[25];
	u8	CONFIG5;
	u8	TX_DMA_POLLING;
	u8	reserved_17[2];
	__le16	CWR;
	u8	RETRY_CTR;
	u8	reserved_18[5];
	__le32	RDSAR;
	u8	reserved_19[18];
	u16	TALLY_CNT;
	u8	TALLY_SEL;
} __attribute__((packed));

static const struct ieee80211_rate rtl818x_rates[] = {
	{ .rate = 10,
	  .val = 0,
	  .flags = IEEE80211_RATE_CCK },
	{ .rate = 20,
	  .val = 1,
	  .flags = IEEE80211_RATE_CCK },
	{ .rate = 55,
	  .val = 2,
	  .flags = IEEE80211_RATE_CCK },
	{ .rate = 110,
	  .val = 3,
	  .flags = IEEE80211_RATE_CCK },
	{ .rate = 60,
	  .val = 4,
	  .flags = IEEE80211_RATE_OFDM },
	{ .rate = 90,
	  .val = 5,
	  .flags = IEEE80211_RATE_OFDM },
	{ .rate = 120,
	  .val = 6,
	  .flags = IEEE80211_RATE_OFDM },
	{ .rate = 180,
	  .val = 7,
	  .flags = IEEE80211_RATE_OFDM },
	{ .rate = 240,
	  .val = 8,
	  .flags = IEEE80211_RATE_OFDM },
	{ .rate = 360,
	  .val = 9,
	  .flags = IEEE80211_RATE_OFDM },
	{ .rate = 480,
	  .val = 10,
	  .flags = IEEE80211_RATE_OFDM },
	{ .rate = 540,
	  .val = 11,
	  .flags = IEEE80211_RATE_OFDM },
};

static const struct ieee80211_channel rtl818x_channels[] = {
	{ .chan = 1,
	  .freq = 2412},
	{ .chan = 2,
	  .freq = 2417},
	{ .chan = 3,
	  .freq = 2422},
	{ .chan = 4,
	  .freq = 2427},
	{ .chan = 5,
	  .freq = 2432},
	{ .chan = 6,
	  .freq = 2437},
	{ .chan = 7,
	  .freq = 2442},
	{ .chan = 8,
	  .freq = 2447},
	{ .chan = 9,
	  .freq = 2452},
	{ .chan = 10,
	  .freq = 2457},
	{ .chan = 11,
	  .freq = 2462},
	{ .chan = 12,
	  .freq = 2467},
	{ .chan = 13,
	  .freq = 2472},
	{ .chan = 14,
	  .freq = 2484}
};

#endif /* RTL818X_H */
