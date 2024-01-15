/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * Purpose: MAC Data structure
 *
 * Author: Tevin Chen
 *
 * Date: Mar 17, 1997
 *
 */

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/crc32.h>
#include <net/mac80211.h>

/* device specific */

#include "device_cfg.h"
#include "card.h"
#include "srom.h"
#include "desc.h"
#include "key.h"
#include "mac.h"

/*---------------------  Export Definitions -------------------------*/

#define RATE_1M		0
#define RATE_2M		1
#define RATE_5M		2
#define RATE_11M	3
#define RATE_6M		4
#define RATE_9M		5
#define RATE_12M	6
#define RATE_18M	7
#define RATE_24M	8
#define RATE_36M	9
#define RATE_48M	10
#define RATE_54M	11
#define MAX_RATE	12

#define AUTO_FB_NONE            0
#define AUTO_FB_0               1
#define AUTO_FB_1               2

#define FB_RATE0                0
#define FB_RATE1                1

/* Antenna Mode */
#define ANT_A                   0
#define ANT_B                   1
#define ANT_DIVERSITY           2
#define ANT_RXD_TXA             3
#define ANT_RXD_TXB             4
#define ANT_UNKNOWN             0xFF

#define BB_VGA_LEVEL            4
#define BB_VGA_CHANGE_THRESHOLD 16

#define MAKE_BEACON_RESERVED	10  /* (us) */

/* BUILD OBJ mode */

#define	AVAIL_TD(p, q)	((p)->opts.tx_descs[(q)] - ((p)->iTDUsed[(q)]))

/* 0:11A 1:11B 2:11G */
#define BB_TYPE_11A    0
#define BB_TYPE_11B    1
#define BB_TYPE_11G    2

/* 0:11a, 1:11b, 2:11gb (only CCK in BasicRate), 3:11ga (OFDM in BasicRate) */
#define PK_TYPE_11A     0
#define PK_TYPE_11B     1
#define PK_TYPE_11GB    2
#define PK_TYPE_11GA    3

#define OWNED_BY_HOST	0
#define	OWNED_BY_NIC	1

struct vnt_options {
	int rx_descs0;		/* Number of RX descriptors0 */
	int rx_descs1;		/* Number of RX descriptors1 */
	int tx_descs[2];	/* Number of TX descriptors 0, 1 */
	int int_works;		/* interrupt limits */
	int short_retry;
	int long_retry;
	int bbp_type;
	u32 flags;
};

struct vnt_private {
	struct pci_dev *pcid;
	/* mac80211 */
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif;
	unsigned long key_entry_inuse;
	u32 basic_rates;
	u16 current_aid;
	int mc_list_count;
	u8 mac_hw;

/* dma addr, rx/tx pool */
	dma_addr_t                  pool_dma;
	dma_addr_t                  rd0_pool_dma;
	dma_addr_t                  rd1_pool_dma;

	dma_addr_t                  td0_pool_dma;
	dma_addr_t                  td1_pool_dma;

	dma_addr_t                  tx_bufs_dma0;
	dma_addr_t                  tx_bufs_dma1;
	dma_addr_t                  tx_beacon_dma;

	unsigned char *tx0_bufs;
	unsigned char *tx1_bufs;
	unsigned char *tx_beacon_bufs;

	void __iomem                *port_offset;
	u32                         memaddr;
	u32                         ioaddr;

	spinlock_t                  lock;

	volatile int                iTDUsed[TYPE_MAXTD];

	struct vnt_tx_desc *apCurrTD[TYPE_MAXTD];
	struct vnt_tx_desc *apTailTD[TYPE_MAXTD];

	struct vnt_tx_desc *apTD0Rings;
	struct vnt_tx_desc *apTD1Rings;

	struct vnt_rx_desc *aRD0Ring;
	struct vnt_rx_desc *aRD1Ring;
	struct vnt_rx_desc *pCurrRD[TYPE_MAXRD];

	struct vnt_options opts;

	u32                         flags;

	u32                         rx_buf_sz;
	u8 rx_rate;

	u32                         rx_bytes;

	/* Version control */
	unsigned char local_id;
	unsigned char rf_type;

	unsigned char max_pwr_level;
	unsigned char byZoneType;
	bool bZoneRegExist;
	unsigned char byOriginalZonetype;

	unsigned char abyCurrentNetAddr[ETH_ALEN]; __aligned(2)
	bool bLinkPass;          /* link status: OK or fail */

	unsigned int current_rssi;
	unsigned char byCurrSQ;

	unsigned long dwTxAntennaSel;
	unsigned long dwRxAntennaSel;
	unsigned char byAntennaCount;
	unsigned char byRxAntennaMode;
	unsigned char byTxAntennaMode;
	bool bTxRxAntInv;

	unsigned char *pbyTmpBuff;
	unsigned int	uSIFS;    /* Current SIFS */
	unsigned int	uDIFS;    /* Current DIFS */
	unsigned int	uEIFS;    /* Current EIFS */
	unsigned int	uSlot;    /* Current SlotTime */
	unsigned int	uCwMin;   /* Current CwMin */
	unsigned int	uCwMax;   /* CwMax is fixed on 1023. */
	/* PHY parameter */
	unsigned char sifs;
	unsigned char difs;
	unsigned char eifs;
	unsigned char slot;
	unsigned char cw_max_min;

	u8		byBBType; /* 0:11A, 1:11B, 2:11G */
	u8		packet_type; /*
				       * 0:11a,1:11b,2:11gb (only CCK
				       * in BasicRate), 3:11ga (OFDM in
				       * Basic Rate)
				       */
	unsigned short wBasicRate;
	unsigned char byACKRate;
	unsigned char byTopOFDMBasicRate;
	unsigned char byTopCCKBasicRate;

	unsigned char byMinChannel;
	unsigned char byMaxChannel;

	unsigned char preamble_type;
	unsigned char byShortPreamble;

	unsigned short wCurrentRate;
	unsigned char byShortRetryLimit;
	unsigned char byLongRetryLimit;
	enum nl80211_iftype op_mode;
	bool bBSSIDFilter;
	unsigned short wMaxTransmitMSDULifetime;

	bool bEncryptionEnable;
	bool bLongHeader;
	bool short_slot_time;
	bool bProtectMode;
	bool bNonERPPresent;
	bool bBarkerPreambleMd;

	bool bRadioControlOff;
	bool radio_off;
	bool bEnablePSMode;
	unsigned short wListenInterval;
	bool bPWBitOn;

	/* GPIO Radio Control */
	unsigned char byRadioCtl;
	unsigned char byGPIO;
	bool hw_radio_off;
	bool bPrvActive4RadioOFF;
	bool bGPIOBlockRead;

	/* Beacon related */
	unsigned short wSeqCounter;
	unsigned short wBCNBufLen;
	bool bBeaconBufReady;
	bool bBeaconSent;
	bool bIsBeaconBufReadySet;
	unsigned int	cbBeaconBufReadySetCnt;
	bool bFixRate;
	u16 current_ch;

	bool bAES;

	unsigned char byAutoFBCtrl;

	/* For Update BaseBand VGA Gain Offset */
	bool update_bbvga;
	unsigned int	uBBVGADiffCount;
	unsigned char bbvga_new;
	unsigned char bbvga_current;
	unsigned char bbvga[BB_VGA_LEVEL];
	long                    dbm_threshold[BB_VGA_LEVEL];

	unsigned char bb_pre_edrssi;
	unsigned char byBBPreEDIndex;

	unsigned long dwDiagRefCount;

	/* For FOE Tuning */
	unsigned char byFOETuning;

	/* For RF Power table */
	unsigned char byCCKPwr;
	unsigned char byOFDMPwrG;
	unsigned char cur_pwr;
	char	 byCurPwrdBm;
	unsigned char abyCCKPwrTbl[CB_MAX_CHANNEL_24G + 1];
	unsigned char abyOFDMPwrTbl[CB_MAX_CHANNEL + 1];
	char	abyCCKDefaultPwr[CB_MAX_CHANNEL_24G + 1];
	char	abyOFDMDefaultPwr[CB_MAX_CHANNEL + 1];
	char	abyRegPwr[CB_MAX_CHANNEL + 1];
	char	abyLocalPwr[CB_MAX_CHANNEL + 1];

	/* BaseBand Loopback Use */
	unsigned char byBBCR4d;
	unsigned char byBBCRc9;
	unsigned char byBBCR88;
	unsigned char byBBCR09;

	unsigned char abyEEPROM[EEP_MAX_CONTEXT_SIZE]; /* unsigned long alignment */

	unsigned short wBeaconInterval;
	u16 wake_up_count;

	struct work_struct interrupt_work;

	struct ieee80211_low_level_stats low_stats;
};

#endif
