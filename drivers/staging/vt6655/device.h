/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: device.h
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
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/if_arp.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/if.h>
#include <linux/crc32.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/inetdevice.h>
#include <linux/reboot.h>
#include <linux/ethtool.h>
/* Include Wireless Extension definition and check version - Jean II */
#include <net/mac80211.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>	/* New driver API */

#ifndef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#define WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#endif

/* device specific */

#include "device_cfg.h"
#include "card.h"
#include "mib.h"
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
#define RATE_AUTO	12
#define MAX_RATE	12

#define MAC_MAX_CONTEXT_REG     (256+128)

#define MAX_MULTICAST_ADDRESS_NUM       32
#define MULTICAST_ADDRESS_LIST_SIZE     (MAX_MULTICAST_ADDRESS_NUM * ETH_ALEN)

#define DUPLICATE_RX_CACHE_LENGTH       5

#define NUM_KEY_ENTRY                   11

#define TX_WEP_NONE                     0
#define TX_WEP_OTF                      1
#define TX_WEP_SW                       2
#define TX_WEP_SWOTP                    3
#define TX_WEP_OTPSW                    4
#define TX_WEP_SW232                    5

#define KEYSEL_WEP40                    0
#define KEYSEL_WEP104                   1
#define KEYSEL_TKIP                     2
#define KEYSEL_CCMP                     3

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

#define MAXCHECKHANGCNT         4

#define BB_VGA_LEVEL            4
#define BB_VGA_CHANGE_THRESHOLD 16

#ifndef RUN_AT
#define RUN_AT(x)                       (jiffies+(x))
#endif

#define MAKE_BEACON_RESERVED	10  /* (us) */

/* DMA related */
#define RESERV_AC0DMA                   4

/* BUILD OBJ mode */

#define	AVAIL_TD(p, q)	((p)->sOpts.nTxDescs[(q)] - ((p)->iTDUsed[(q)]))

#define	NUM				64

/* 0:11A 1:11B 2:11G */
#define BB_TYPE_11A    0
#define BB_TYPE_11B    1
#define BB_TYPE_11G    2

/* 0:11a, 1:11b, 2:11gb (only CCK in BasicRate), 3:11ga (OFDM in BasicRate) */
#define PK_TYPE_11A     0
#define PK_TYPE_11B     1
#define PK_TYPE_11GB    2
#define PK_TYPE_11GA    3

typedef struct __chip_info_tbl {
	CHIP_TYPE   chip_id;
	char *name;
	int         io_size;
	int         nTxQueue;
	u32         flags;
} CHIP_INFO, *PCHIP_INFO;

typedef enum {
	OWNED_BY_HOST = 0,
	OWNED_BY_NIC = 1
} DEVICE_OWNER_TYPE, *PDEVICE_OWNER_TYPE;

/* flags for options */
#define     DEVICE_FLAGS_IP_ALIGN        0x00000001UL
#define     DEVICE_FLAGS_PREAMBLE_TYPE   0x00000002UL
#define     DEVICE_FLAGS_OP_MODE         0x00000004UL
#define     DEVICE_FLAGS_PS_MODE         0x00000008UL
#define		DEVICE_FLAGS_80211h_MODE	 0x00000010UL
#define		DEVICE_FLAGS_DiversityANT	 0x00000020UL

/* flags for driver status */
#define     DEVICE_FLAGS_OPENED          0x00010000UL
#define     DEVICE_FLAGS_WOL_ENABLED     0x00080000UL
/* flags for capabilities */
#define     DEVICE_FLAGS_TX_ALIGN        0x01000000UL
#define     DEVICE_FLAGS_HAVE_CAM        0x02000000UL
#define     DEVICE_FLAGS_FLOW_CTRL       0x04000000UL

/* flags for MII status */
#define     DEVICE_LINK_FAIL             0x00000001UL
#define     DEVICE_SPEED_10              0x00000002UL
#define     DEVICE_SPEED_100             0x00000004UL
#define     DEVICE_SPEED_1000            0x00000008UL
#define     DEVICE_DUPLEX_FULL           0x00000010UL
#define     DEVICE_AUTONEG_ENABLE        0x00000020UL
#define     DEVICE_FORCED_BY_EEPROM      0x00000040UL
/* for device_set_media_duplex */
#define     DEVICE_LINK_CHANGE           0x00000001UL

typedef struct __device_opt {
	int         nRxDescs0;		/* Number of RX descriptors0 */
	int         nRxDescs1;		/* Number of RX descriptors1 */
	int         nTxDescs[2];	/* Number of TX descriptors 0, 1 */
	int         int_works;		/* interrupt limits */
	int         short_retry;
	int         long_retry;
	int         bbp_type;
	u32         flags;
} OPTIONS, *POPTIONS;

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

	CHIP_TYPE                   chip_id;

	void __iomem                *PortOffset;
	unsigned long dwIsr;
	u32                         memaddr;
	u32                         ioaddr;
	u32                         io_size;

	unsigned char byRevId;
	unsigned char byRxMode;
	unsigned short SubSystemID;
	unsigned short SubVendorID;

	spinlock_t                  lock;

	int                         nTxQueues;
	volatile int                iTDUsed[TYPE_MAXTD];

	volatile PSTxDesc           apCurrTD[TYPE_MAXTD];
	volatile PSTxDesc           apTailTD[TYPE_MAXTD];

	volatile PSTxDesc           apTD0Rings;
	volatile PSTxDesc           apTD1Rings;

	volatile PSRxDesc           aRD0Ring;
	volatile PSRxDesc           aRD1Ring;
	volatile PSRxDesc           pCurrRD[TYPE_MAXRD];

	OPTIONS                     sOpts;

	u32                         flags;

	u32                         rx_buf_sz;
	u8 rx_rate;
	int                         multicast_limit;

	u32                         rx_bytes;

	/* Version control */
	unsigned char byLocalID;
	unsigned char byRFType;

	unsigned char byMaxPwrLevel;
	unsigned char byZoneType;
	bool bZoneRegExist;
	unsigned char byOriginalZonetype;

	unsigned char abyCurrentNetAddr[ETH_ALEN]; __aligned(2)
	bool bLinkPass;          /* link status: OK or fail */

	/* Adapter statistics */
	SStatCounter                scStatistic;
	/* 802.11 counter */
	SDot11Counters              s802_11Counter;

	unsigned int	uCurrRSSI;
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
	unsigned char bySIFS;
	unsigned char byDIFS;
	unsigned char byEIFS;
	unsigned char bySlot;
	unsigned char byCWMaxMin;

	u8		byBBType; /* 0:11A, 1:11B, 2:11G */
	u8		byPacketType; /*
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

	unsigned char byPreambleType;
	unsigned char byShortPreamble;

	unsigned short wCurrentRate;
	unsigned char byShortRetryLimit;
	unsigned char byLongRetryLimit;
	enum nl80211_iftype op_mode;
	bool bBSSIDFilter;
	unsigned short wMaxTransmitMSDULifetime;

	bool bEncryptionEnable;
	bool bLongHeader;
	bool bShortSlotTime;
	bool bProtectMode;
	bool bNonERPPresent;
	bool bBarkerPreambleMd;

	bool bRadioControlOff;
	bool bRadioOff;
	bool bEnablePSMode;
	unsigned short wListenInterval;
	bool bPWBitOn;

	/* GPIO Radio Control */
	unsigned char byRadioCtl;
	unsigned char byGPIO;
	bool bHWRadioOff;
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
	u16 byCurrentCh;

	bool bAES;

	unsigned char byAutoFBCtrl;

	/* For Update BaseBand VGA Gain Offset */
	bool bUpdateBBVGA;
	unsigned int	uBBVGADiffCount;
	unsigned char byBBVGANew;
	unsigned char byBBVGACurrent;
	unsigned char abyBBVGA[BB_VGA_LEVEL];
	long                    ldBmThreshold[BB_VGA_LEVEL];

	unsigned char byBBPreEDRSSI;
	unsigned char byBBPreEDIndex;

	unsigned long dwDiagRefCount;

	/* For FOE Tuning */
	unsigned char byFOETuning;

	/* For RF Power table */
	unsigned char byCCKPwr;
	unsigned char byOFDMPwrG;
	unsigned char byCurPwr;
	char	 byCurPwrdBm;
	unsigned char abyCCKPwrTbl[CB_MAX_CHANNEL_24G+1];
	unsigned char abyOFDMPwrTbl[CB_MAX_CHANNEL+1];
	char	abyCCKDefaultPwr[CB_MAX_CHANNEL_24G+1];
	char	abyOFDMDefaultPwr[CB_MAX_CHANNEL+1];
	char	abyRegPwr[CB_MAX_CHANNEL+1];
	char	abyLocalPwr[CB_MAX_CHANNEL+1];

	/* BaseBand Loopback Use */
	unsigned char byBBCR4d;
	unsigned char byBBCRc9;
	unsigned char byBBCR88;
	unsigned char byBBCR09;

	unsigned char abyEEPROM[EEP_MAX_CONTEXT_SIZE]; /* unsigned long alignment */

	unsigned short wBeaconInterval;
};

static inline PDEVICE_RD_INFO alloc_rd_info(void)
{
	return kzalloc(sizeof(DEVICE_RD_INFO), GFP_ATOMIC);
}

static inline PDEVICE_TD_INFO alloc_td_info(void)
{
	return kzalloc(sizeof(DEVICE_TD_INFO), GFP_ATOMIC);
}
#endif
