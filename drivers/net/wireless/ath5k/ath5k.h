/*
 * Copyright (c) 2004-2007 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2007 Nick Kossifidis <mickflemm@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _ATH5K_H
#define _ATH5K_H

/* TODO: Clean up channel debuging -doesn't work anyway- and start
 * working on reg. control code using all available eeprom information
 * -rev. engineering needed- */
#define CHAN_DEBUG	0

#include <linux/io.h>
#include <linux/types.h>
#include <net/mac80211.h>

/* RX/TX descriptor hw structs
 * TODO: Driver part should only see sw structs */
#include "desc.h"

/* EEPROM structs/offsets
 * TODO: Make a more generic struct (eg. add more stuff to ath5k_capabilities)
 * and clean up common bits, then introduce set/get functions in eeprom.c */
#include "eeprom.h"

/* PCI IDs */
#define PCI_DEVICE_ID_ATHEROS_AR5210 		0x0007 /* AR5210 */
#define PCI_DEVICE_ID_ATHEROS_AR5311 		0x0011 /* AR5311 */
#define PCI_DEVICE_ID_ATHEROS_AR5211 		0x0012 /* AR5211 */
#define PCI_DEVICE_ID_ATHEROS_AR5212 		0x0013 /* AR5212 */
#define PCI_DEVICE_ID_3COM_3CRDAG675 		0x0013 /* 3CRDAG675 (Atheros AR5212) */
#define PCI_DEVICE_ID_3COM_2_3CRPAG175 		0x0013 /* 3CRPAG175 (Atheros AR5212) */
#define PCI_DEVICE_ID_ATHEROS_AR5210_AP 	0x0207 /* AR5210 (Early) */
#define PCI_DEVICE_ID_ATHEROS_AR5212_IBM	0x1014 /* AR5212 (IBM MiniPCI) */
#define PCI_DEVICE_ID_ATHEROS_AR5210_DEFAULT 	0x1107 /* AR5210 (no eeprom) */
#define PCI_DEVICE_ID_ATHEROS_AR5212_DEFAULT 	0x1113 /* AR5212 (no eeprom) */
#define PCI_DEVICE_ID_ATHEROS_AR5211_DEFAULT 	0x1112 /* AR5211 (no eeprom) */
#define PCI_DEVICE_ID_ATHEROS_AR5212_FPGA 	0xf013 /* AR5212 (emulation board) */
#define PCI_DEVICE_ID_ATHEROS_AR5211_LEGACY 	0xff12 /* AR5211 (emulation board) */
#define PCI_DEVICE_ID_ATHEROS_AR5211_FPGA11B 	0xf11b /* AR5211 (emulation board) */
#define PCI_DEVICE_ID_ATHEROS_AR5312_REV2 	0x0052 /* AR5312 WMAC (AP31) */
#define PCI_DEVICE_ID_ATHEROS_AR5312_REV7 	0x0057 /* AR5312 WMAC (AP30-040) */
#define PCI_DEVICE_ID_ATHEROS_AR5312_REV8 	0x0058 /* AR5312 WMAC (AP43-030) */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0014 	0x0014 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0015 	0x0015 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0016 	0x0016 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0017 	0x0017 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0018 	0x0018 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0019 	0x0019 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR2413 		0x001a /* AR2413 (Griffin-lite) */
#define PCI_DEVICE_ID_ATHEROS_AR5413 		0x001b /* AR5413 (Eagle) */
#define PCI_DEVICE_ID_ATHEROS_AR5424 		0x001c /* AR5424 (Condor PCI-E) */
#define PCI_DEVICE_ID_ATHEROS_AR5416 		0x0023 /* AR5416 */
#define PCI_DEVICE_ID_ATHEROS_AR5418 		0x0024 /* AR5418 */

/****************************\
  GENERIC DRIVER DEFINITIONS
\****************************/

#define ATH5K_PRINTF(fmt, ...)   printk("%s: " fmt, __func__, ##__VA_ARGS__)

#define ATH5K_PRINTK(_sc, _level, _fmt, ...) \
	printk(_level "ath5k %s: " _fmt, \
		((_sc) && (_sc)->hw) ? wiphy_name((_sc)->hw->wiphy) : "", \
		##__VA_ARGS__)

#define ATH5K_PRINTK_LIMIT(_sc, _level, _fmt, ...) do { \
	if (net_ratelimit()) \
		ATH5K_PRINTK(_sc, _level, _fmt, ##__VA_ARGS__); \
	} while (0)

#define ATH5K_INFO(_sc, _fmt, ...) \
	ATH5K_PRINTK(_sc, KERN_INFO, _fmt, ##__VA_ARGS__)

#define ATH5K_WARN(_sc, _fmt, ...) \
	ATH5K_PRINTK_LIMIT(_sc, KERN_WARNING, _fmt, ##__VA_ARGS__)

#define ATH5K_ERR(_sc, _fmt, ...) \
	ATH5K_PRINTK_LIMIT(_sc, KERN_ERR, _fmt, ##__VA_ARGS__)

/*
 * AR5K REGISTER ACCESS
 */

/* Some macros to read/write fields */

/* First shift, then mask */
#define AR5K_REG_SM(_val, _flags)					\
	(((_val) << _flags##_S) & (_flags))

/* First mask, then shift */
#define AR5K_REG_MS(_val, _flags)					\
	(((_val) & (_flags)) >> _flags##_S)

/* Some registers can hold multiple values of interest. For this
 * reason when we want to write to these registers we must first
 * retrieve the values which we do not want to clear (lets call this
 * old_data) and then set the register with this and our new_value:
 * ( old_data | new_value) */
#define AR5K_REG_WRITE_BITS(ah, _reg, _flags, _val)			\
	ath5k_hw_reg_write(ah, (ath5k_hw_reg_read(ah, _reg) & ~(_flags)) | \
	    (((_val) << _flags##_S) & (_flags)), _reg)

#define AR5K_REG_MASKED_BITS(ah, _reg, _flags, _mask)			\
	ath5k_hw_reg_write(ah, (ath5k_hw_reg_read(ah, _reg) &		\
			(_mask)) | (_flags), _reg)

#define AR5K_REG_ENABLE_BITS(ah, _reg, _flags)				\
	ath5k_hw_reg_write(ah, ath5k_hw_reg_read(ah, _reg) | (_flags), _reg)

#define AR5K_REG_DISABLE_BITS(ah, _reg, _flags)			\
	ath5k_hw_reg_write(ah, ath5k_hw_reg_read(ah, _reg) & ~(_flags), _reg)

/* Access to PHY registers */
#define AR5K_PHY_READ(ah, _reg)					\
	ath5k_hw_reg_read(ah, (ah)->ah_phy + ((_reg) << 2))

#define AR5K_PHY_WRITE(ah, _reg, _val)					\
	ath5k_hw_reg_write(ah, _val, (ah)->ah_phy + ((_reg) << 2))

/* Access QCU registers per queue */
#define AR5K_REG_READ_Q(ah, _reg, _queue)				\
	(ath5k_hw_reg_read(ah, _reg) & (1 << _queue))			\

#define AR5K_REG_WRITE_Q(ah, _reg, _queue)				\
	ath5k_hw_reg_write(ah, (1 << _queue), _reg)

#define AR5K_Q_ENABLE_BITS(_reg, _queue) do {				\
	_reg |= 1 << _queue;						\
} while (0)

#define AR5K_Q_DISABLE_BITS(_reg, _queue) do {				\
	_reg &= ~(1 << _queue);						\
} while (0)

/* Used while writing initvals */
#define AR5K_REG_WAIT(_i) do {						\
	if (_i % 64)							\
		udelay(1);						\
} while (0)

/* Register dumps are done per operation mode */
#define AR5K_INI_RFGAIN_5GHZ		0
#define AR5K_INI_RFGAIN_2GHZ		1

/* TODO: Clean this up */
#define AR5K_INI_VAL_11A		0
#define AR5K_INI_VAL_11A_TURBO		1
#define AR5K_INI_VAL_11B		2
#define AR5K_INI_VAL_11G		3
#define AR5K_INI_VAL_11G_TURBO		4
#define AR5K_INI_VAL_XR			0
#define AR5K_INI_VAL_MAX		5

/* Used for BSSID etc manipulation */
#define AR5K_LOW_ID(_a)(				\
(_a)[0] | (_a)[1] << 8 | (_a)[2] << 16 | (_a)[3] << 24	\
)

#define AR5K_HIGH_ID(_a)	((_a)[4] | (_a)[5] << 8)

/*
 * Some tuneable values (these should be changeable by the user)
 * TODO: Make use of them and add more options OR use debug/configfs
 */
#define AR5K_TUNE_DMA_BEACON_RESP		2
#define AR5K_TUNE_SW_BEACON_RESP		10
#define AR5K_TUNE_ADDITIONAL_SWBA_BACKOFF	0
#define AR5K_TUNE_RADAR_ALERT			false
#define AR5K_TUNE_MIN_TX_FIFO_THRES		1
#define AR5K_TUNE_MAX_TX_FIFO_THRES		((IEEE80211_MAX_LEN / 64) + 1)
#define AR5K_TUNE_REGISTER_TIMEOUT		20000
/* Register for RSSI threshold has a mask of 0xff, so 255 seems to
 * be the max value. */
#define AR5K_TUNE_RSSI_THRES			129
/* This must be set when setting the RSSI threshold otherwise it can
 * prevent a reset. If AR5K_RSSI_THR is read after writing to it
 * the BMISS_THRES will be seen as 0, seems harware doesn't keep
 * track of it. Max value depends on harware. For AR5210 this is just 7.
 * For AR5211+ this seems to be up to 255. */
#define AR5K_TUNE_BMISS_THRES			7
#define AR5K_TUNE_REGISTER_DWELL_TIME		20000
#define AR5K_TUNE_BEACON_INTERVAL		100
#define AR5K_TUNE_AIFS				2
#define AR5K_TUNE_AIFS_11B			2
#define AR5K_TUNE_AIFS_XR			0
#define AR5K_TUNE_CWMIN				15
#define AR5K_TUNE_CWMIN_11B			31
#define AR5K_TUNE_CWMIN_XR			3
#define AR5K_TUNE_CWMAX				1023
#define AR5K_TUNE_CWMAX_11B			1023
#define AR5K_TUNE_CWMAX_XR			7
#define AR5K_TUNE_NOISE_FLOOR			-72
#define AR5K_TUNE_MAX_TXPOWER			63
#define AR5K_TUNE_DEFAULT_TXPOWER		25
#define AR5K_TUNE_TPC_TXPOWER			false
#define AR5K_TUNE_ANT_DIVERSITY			true
#define AR5K_TUNE_HWTXTRIES			4

#define AR5K_INIT_CARR_SENSE_EN			1

/*Swap RX/TX Descriptor for big endian archs*/
#if defined(__BIG_ENDIAN)
#define AR5K_INIT_CFG	(		\
	AR5K_CFG_SWTD | AR5K_CFG_SWRD	\
)
#else
#define AR5K_INIT_CFG	0x00000000
#endif

/* Initial values */
#define	AR5K_INIT_CYCRSSI_THR1			2
#define AR5K_INIT_TX_LATENCY			502
#define AR5K_INIT_USEC				39
#define AR5K_INIT_USEC_TURBO			79
#define AR5K_INIT_USEC_32			31
#define AR5K_INIT_SLOT_TIME			396
#define AR5K_INIT_SLOT_TIME_TURBO		480
#define AR5K_INIT_ACK_CTS_TIMEOUT		1024
#define AR5K_INIT_ACK_CTS_TIMEOUT_TURBO		0x08000800
#define AR5K_INIT_PROG_IFS			920
#define AR5K_INIT_PROG_IFS_TURBO		960
#define AR5K_INIT_EIFS				3440
#define AR5K_INIT_EIFS_TURBO			6880
#define AR5K_INIT_SIFS				560
#define AR5K_INIT_SIFS_TURBO			480
#define AR5K_INIT_SH_RETRY			10
#define AR5K_INIT_LG_RETRY			AR5K_INIT_SH_RETRY
#define AR5K_INIT_SSH_RETRY			32
#define AR5K_INIT_SLG_RETRY			AR5K_INIT_SSH_RETRY
#define AR5K_INIT_TX_RETRY			10

#define AR5K_INIT_TRANSMIT_LATENCY		(			\
	(AR5K_INIT_TX_LATENCY << 14) | (AR5K_INIT_USEC_32 << 7) |	\
	(AR5K_INIT_USEC)						\
)
#define AR5K_INIT_TRANSMIT_LATENCY_TURBO	(			\
	(AR5K_INIT_TX_LATENCY << 14) | (AR5K_INIT_USEC_32 << 7) |	\
	(AR5K_INIT_USEC_TURBO)						\
)
#define AR5K_INIT_PROTO_TIME_CNTRL		(			\
	(AR5K_INIT_CARR_SENSE_EN << 26) | (AR5K_INIT_EIFS << 12) |	\
	(AR5K_INIT_PROG_IFS)						\
)
#define AR5K_INIT_PROTO_TIME_CNTRL_TURBO	(			\
	(AR5K_INIT_CARR_SENSE_EN << 26) | (AR5K_INIT_EIFS_TURBO << 12) | \
	(AR5K_INIT_PROG_IFS_TURBO)					\
)

/* token to use for aifs, cwmin, cwmax in MadWiFi */
#define	AR5K_TXQ_USEDEFAULT	((u32) -1)

/* GENERIC CHIPSET DEFINITIONS */

/* MAC Chips */
enum ath5k_version {
	AR5K_AR5210	= 0,
	AR5K_AR5211	= 1,
	AR5K_AR5212	= 2,
};

/* PHY Chips */
enum ath5k_radio {
	AR5K_RF5110	= 0,
	AR5K_RF5111	= 1,
	AR5K_RF5112	= 2,
	AR5K_RF2413	= 3,
	AR5K_RF5413	= 4,
	AR5K_RF2316	= 5,
	AR5K_RF2317	= 6,
	AR5K_RF2425	= 7,
};

/*
 * Common silicon revision/version values
 */

enum ath5k_srev_type {
	AR5K_VERSION_MAC,
	AR5K_VERSION_RAD,
};

struct ath5k_srev_name {
	const char		*sr_name;
	enum ath5k_srev_type	sr_type;
	u_int			sr_val;
};

#define AR5K_SREV_UNKNOWN	0xffff

#define AR5K_SREV_AR5210	0x00 /* Crete */
#define AR5K_SREV_AR5311	0x10 /* Maui 1 */
#define AR5K_SREV_AR5311A	0x20 /* Maui 2 */
#define AR5K_SREV_AR5311B	0x30 /* Spirit */
#define AR5K_SREV_AR5211	0x40 /* Oahu */
#define AR5K_SREV_AR5212	0x50 /* Venice */
#define AR5K_SREV_AR5213	0x55 /* ??? */
#define AR5K_SREV_AR5213A	0x59 /* Hainan */
#define AR5K_SREV_AR2413	0x78 /* Griffin lite */
#define AR5K_SREV_AR2414	0x70 /* Griffin */
#define AR5K_SREV_AR5424	0x90 /* Condor */
#define AR5K_SREV_AR5413	0xa4 /* Eagle lite */
#define AR5K_SREV_AR5414	0xa0 /* Eagle */
#define AR5K_SREV_AR2415	0xb0 /* Talon */
#define AR5K_SREV_AR5416	0xc0 /* PCI-E */
#define AR5K_SREV_AR5418	0xca /* PCI-E */
#define AR5K_SREV_AR2425	0xe0 /* Swan */
#define AR5K_SREV_AR2417	0xf0 /* Nala */

#define AR5K_SREV_RAD_5110	0x00
#define AR5K_SREV_RAD_5111	0x10
#define AR5K_SREV_RAD_5111A	0x15
#define AR5K_SREV_RAD_2111	0x20
#define AR5K_SREV_RAD_5112	0x30
#define AR5K_SREV_RAD_5112A	0x35
#define	AR5K_SREV_RAD_5112B	0x36
#define AR5K_SREV_RAD_2112	0x40
#define AR5K_SREV_RAD_2112A	0x45
#define	AR5K_SREV_RAD_2112B	0x46
#define AR5K_SREV_RAD_2413	0x50
#define AR5K_SREV_RAD_5413	0x60
#define AR5K_SREV_RAD_2316	0x70 /* Cobra SoC */
#define AR5K_SREV_RAD_2317	0x80
#define AR5K_SREV_RAD_5424	0xa0 /* Mostly same as 5413 */
#define AR5K_SREV_RAD_2425	0xa2
#define AR5K_SREV_RAD_5133	0xc0

#define AR5K_SREV_PHY_5211	0x30
#define AR5K_SREV_PHY_5212	0x41
#define	AR5K_SREV_PHY_5212A	0x42
#define AR5K_SREV_PHY_5212B	0x43
#define AR5K_SREV_PHY_2413	0x45
#define AR5K_SREV_PHY_5413	0x61
#define AR5K_SREV_PHY_2425	0x70

/* IEEE defs */
#define IEEE80211_MAX_LEN       2500

/* TODO add support to mac80211 for vendor-specific rates and modes */

/*
 * Some of this information is based on Documentation from:
 *
 * http://madwifi.org/wiki/ChipsetFeatures/SuperAG
 *
 * Modulation for Atheros' eXtended Range - range enhancing extension that is
 * supposed to double the distance an Atheros client device can keep a
 * connection with an Atheros access point. This is achieved by increasing
 * the receiver sensitivity up to, -105dBm, which is about 20dB above what
 * the 802.11 specifications demand. In addition, new (proprietary) data rates
 * are introduced: 3, 2, 1, 0.5 and 0.25 MBit/s.
 *
 * Please note that can you either use XR or TURBO but you cannot use both,
 * they are exclusive.
 *
 */
#define MODULATION_XR 		0x00000200
/*
 * Modulation for Atheros' Turbo G and Turbo A, its supposed to provide a
 * throughput transmission speed up to 40Mbit/s-60Mbit/s at a 108Mbit/s
 * signaling rate achieved through the bonding of two 54Mbit/s 802.11g
 * channels. To use this feature your Access Point must also suport it.
 * There is also a distinction between "static" and "dynamic" turbo modes:
 *
 * - Static: is the dumb version: devices set to this mode stick to it until
 *     the mode is turned off.
 * - Dynamic: is the intelligent version, the network decides itself if it
 *     is ok to use turbo. As soon as traffic is detected on adjacent channels
 *     (which would get used in turbo mode), or when a non-turbo station joins
 *     the network, turbo mode won't be used until the situation changes again.
 *     Dynamic mode is achieved by Atheros' Adaptive Radio (AR) feature which
 *     monitors the used radio band in order to decide whether turbo mode may
 *     be used or not.
 *
 * This article claims Super G sticks to bonding of channels 5 and 6 for
 * USA:
 *
 * http://www.pcworld.com/article/id,113428-page,1/article.html
 *
 * The channel bonding seems to be driver specific though. In addition to
 * deciding what channels will be used, these "Turbo" modes are accomplished
 * by also enabling the following features:
 *
 * - Bursting: allows multiple frames to be sent at once, rather than pausing
 *     after each frame. Bursting is a standards-compliant feature that can be
 *     used with any Access Point.
 * - Fast frames: increases the amount of information that can be sent per
 *     frame, also resulting in a reduction of transmission overhead. It is a
 *     proprietary feature that needs to be supported by the Access Point.
 * - Compression: data frames are compressed in real time using a Lempel Ziv
 *     algorithm. This is done transparently. Once this feature is enabled,
 *     compression and decompression takes place inside the chipset, without
 *     putting additional load on the host CPU.
 *
 */
#define MODULATION_TURBO	0x00000080

enum ath5k_driver_mode {
	AR5K_MODE_11A		=	0,
	AR5K_MODE_11A_TURBO	=	1,
	AR5K_MODE_11B		=	2,
	AR5K_MODE_11G		=	3,
	AR5K_MODE_11G_TURBO	=	4,
	AR5K_MODE_XR		=	0,
	AR5K_MODE_MAX		=	5
};


/****************\
  TX DEFINITIONS
\****************/

/*
 * TX Status descriptor
 */
struct ath5k_tx_status {
	u16	ts_seqnum;
	u16	ts_tstamp;
	u8	ts_status;
	u8	ts_rate[4];
	u8	ts_retry[4];
	u8	ts_final_idx;
	s8	ts_rssi;
	u8	ts_shortretry;
	u8	ts_longretry;
	u8	ts_virtcol;
	u8	ts_antenna;
};

#define AR5K_TXSTAT_ALTRATE	0x80
#define AR5K_TXERR_XRETRY	0x01
#define AR5K_TXERR_FILT		0x02
#define AR5K_TXERR_FIFO		0x04

/**
 * enum ath5k_tx_queue - Queue types used to classify tx queues.
 * @AR5K_TX_QUEUE_INACTIVE: q is unused -- see ath5k_hw_release_tx_queue
 * @AR5K_TX_QUEUE_DATA: A normal data queue
 * @AR5K_TX_QUEUE_XR_DATA: An XR-data queue
 * @AR5K_TX_QUEUE_BEACON: The beacon queue
 * @AR5K_TX_QUEUE_CAB: The after-beacon queue
 * @AR5K_TX_QUEUE_UAPSD: Unscheduled Automatic Power Save Delivery queue
 */
enum ath5k_tx_queue {
	AR5K_TX_QUEUE_INACTIVE = 0,
	AR5K_TX_QUEUE_DATA,
	AR5K_TX_QUEUE_XR_DATA,
	AR5K_TX_QUEUE_BEACON,
	AR5K_TX_QUEUE_CAB,
	AR5K_TX_QUEUE_UAPSD,
};

#define	AR5K_NUM_TX_QUEUES		10
#define	AR5K_NUM_TX_QUEUES_NOQCU	2

/*
 * Queue syb-types to classify normal data queues.
 * These are the 4 Access Categories as defined in
 * WME spec. 0 is the lowest priority and 4 is the
 * highest. Normal data that hasn't been classified
 * goes to the Best Effort AC.
 */
enum ath5k_tx_queue_subtype {
	AR5K_WME_AC_BK = 0,	/*Background traffic*/
	AR5K_WME_AC_BE, 	/*Best-effort (normal) traffic)*/
	AR5K_WME_AC_VI, 	/*Video traffic*/
	AR5K_WME_AC_VO, 	/*Voice traffic*/
};

/*
 * Queue ID numbers as returned by the hw functions, each number
 * represents a hw queue. If hw does not support hw queues
 * (eg 5210) all data goes in one queue. These match
 * d80211 definitions (net80211/MadWiFi don't use them).
 */
enum ath5k_tx_queue_id {
	AR5K_TX_QUEUE_ID_NOQCU_DATA	= 0,
	AR5K_TX_QUEUE_ID_NOQCU_BEACON	= 1,
	AR5K_TX_QUEUE_ID_DATA_MIN	= 0, /*IEEE80211_TX_QUEUE_DATA0*/
	AR5K_TX_QUEUE_ID_DATA_MAX	= 4, /*IEEE80211_TX_QUEUE_DATA4*/
	AR5K_TX_QUEUE_ID_DATA_SVP	= 5, /*IEEE80211_TX_QUEUE_SVP - Spectralink Voice Protocol*/
	AR5K_TX_QUEUE_ID_CAB		= 6, /*IEEE80211_TX_QUEUE_AFTER_BEACON*/
	AR5K_TX_QUEUE_ID_BEACON		= 7, /*IEEE80211_TX_QUEUE_BEACON*/
	AR5K_TX_QUEUE_ID_UAPSD		= 8,
	AR5K_TX_QUEUE_ID_XR_DATA	= 9,
};

/*
 * Flags to set hw queue's parameters...
 */
#define AR5K_TXQ_FLAG_TXOKINT_ENABLE		0x0001	/* Enable TXOK interrupt */
#define AR5K_TXQ_FLAG_TXERRINT_ENABLE		0x0002	/* Enable TXERR interrupt */
#define AR5K_TXQ_FLAG_TXEOLINT_ENABLE		0x0004	/* Enable TXEOL interrupt -not used- */
#define AR5K_TXQ_FLAG_TXDESCINT_ENABLE		0x0008	/* Enable TXDESC interrupt -not used- */
#define AR5K_TXQ_FLAG_TXURNINT_ENABLE		0x0010	/* Enable TXURN interrupt */
#define AR5K_TXQ_FLAG_CBRORNINT_ENABLE		0x0020	/* Enable CBRORN interrupt */
#define AR5K_TXQ_FLAG_CBRURNINT_ENABLE		0x0040	/* Enable CBRURN interrupt */
#define AR5K_TXQ_FLAG_QTRIGINT_ENABLE		0x0080	/* Enable QTRIG interrupt */
#define AR5K_TXQ_FLAG_TXNOFRMINT_ENABLE		0x0100	/* Enable TXNOFRM interrupt */
#define AR5K_TXQ_FLAG_BACKOFF_DISABLE		0x0200	/* Disable random post-backoff */
#define AR5K_TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE	0x0300	/* Enable ready time expiry policy (?)*/
#define AR5K_TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE	0x0800	/* Enable backoff while bursting */
#define AR5K_TXQ_FLAG_POST_FR_BKOFF_DIS		0x1000	/* Disable backoff while bursting */
#define AR5K_TXQ_FLAG_COMPRESSION_ENABLE	0x2000	/* Enable hw compression -not implemented-*/

/*
 * A struct to hold tx queue's parameters
 */
struct ath5k_txq_info {
	enum ath5k_tx_queue tqi_type;
	enum ath5k_tx_queue_subtype tqi_subtype;
	u16	tqi_flags;	/* Tx queue flags (see above) */
	u32	tqi_aifs;	/* Arbitrated Interframe Space */
	s32	tqi_cw_min;	/* Minimum Contention Window */
	s32	tqi_cw_max;	/* Maximum Contention Window */
	u32	tqi_cbr_period; /* Constant bit rate period */
	u32	tqi_cbr_overflow_limit;
	u32	tqi_burst_time;
	u32	tqi_ready_time; /* Not used */
};

/*
 * Transmit packet types.
 * used on tx control descriptor
 * TODO: Use them inside base.c corectly
 */
enum ath5k_pkt_type {
	AR5K_PKT_TYPE_NORMAL		= 0,
	AR5K_PKT_TYPE_ATIM		= 1,
	AR5K_PKT_TYPE_PSPOLL		= 2,
	AR5K_PKT_TYPE_BEACON		= 3,
	AR5K_PKT_TYPE_PROBE_RESP	= 4,
	AR5K_PKT_TYPE_PIFS		= 5,
};

/*
 * TX power and TPC settings
 */
#define AR5K_TXPOWER_OFDM(_r, _v)	(			\
	((0 & 1) << ((_v) + 6)) |				\
	(((ah->ah_txpower.txp_rates_power_table[(_r)]) & 0x3f) << (_v))	\
)

#define AR5K_TXPOWER_CCK(_r, _v)	(			\
	(ah->ah_txpower.txp_rates_power_table[(_r)] & 0x3f) << (_v)	\
)

/*
 * DMA size definitions (2^n+2)
 */
enum ath5k_dmasize {
	AR5K_DMASIZE_4B	= 0,
	AR5K_DMASIZE_8B,
	AR5K_DMASIZE_16B,
	AR5K_DMASIZE_32B,
	AR5K_DMASIZE_64B,
	AR5K_DMASIZE_128B,
	AR5K_DMASIZE_256B,
	AR5K_DMASIZE_512B
};


/****************\
  RX DEFINITIONS
\****************/

/*
 * RX Status descriptor
 */
struct ath5k_rx_status {
	u16	rs_datalen;
	u16	rs_tstamp;
	u8	rs_status;
	u8	rs_phyerr;
	s8	rs_rssi;
	u8	rs_keyix;
	u8	rs_rate;
	u8	rs_antenna;
	u8	rs_more;
};

#define AR5K_RXERR_CRC		0x01
#define AR5K_RXERR_PHY		0x02
#define AR5K_RXERR_FIFO		0x04
#define AR5K_RXERR_DECRYPT	0x08
#define AR5K_RXERR_MIC		0x10
#define AR5K_RXKEYIX_INVALID	((u8) - 1)
#define AR5K_TXKEYIX_INVALID	((u32) - 1)


/**************************\
 BEACON TIMERS DEFINITIONS
\**************************/

#define AR5K_BEACON_PERIOD	0x0000ffff
#define AR5K_BEACON_ENA		0x00800000 /*enable beacon xmit*/
#define AR5K_BEACON_RESET_TSF	0x01000000 /*force a TSF reset*/

#if 0
/**
 * struct ath5k_beacon_state - Per-station beacon timer state.
 * @bs_interval: in TU's, can also include the above flags
 * @bs_cfp_max_duration: if non-zero hw is setup to coexist with a
 * 	Point Coordination Function capable AP
 */
struct ath5k_beacon_state {
	u32	bs_next_beacon;
	u32	bs_next_dtim;
	u32	bs_interval;
	u8	bs_dtim_period;
	u8	bs_cfp_period;
	u16	bs_cfp_max_duration;
	u16	bs_cfp_du_remain;
	u16	bs_tim_offset;
	u16	bs_sleep_duration;
	u16	bs_bmiss_threshold;
	u32  	bs_cfp_next;
};
#endif


/*
 * TSF to TU conversion:
 *
 * TSF is a 64bit value in usec (microseconds).
 * TU is a 32bit value and defined by IEEE802.11 (page 6) as "A measurement of
 * time equal to 1024 usec", so it's roughly milliseconds (usec / 1024).
 */
#define TSF_TO_TU(_tsf) (u32)((_tsf) >> 10)


/*******************************\
  GAIN OPTIMIZATION DEFINITIONS
\*******************************/

enum ath5k_rfgain {
	AR5K_RFGAIN_INACTIVE = 0,
	AR5K_RFGAIN_ACTIVE,
	AR5K_RFGAIN_READ_REQUESTED,
	AR5K_RFGAIN_NEED_CHANGE,
};

struct ath5k_gain {
	u8			g_step_idx;
	u8			g_current;
	u8			g_target;
	u8			g_low;
	u8			g_high;
	u8			g_f_corr;
	u8			g_state;
};

/********************\
  COMMON DEFINITIONS
\********************/

#define AR5K_SLOT_TIME_9	396
#define AR5K_SLOT_TIME_20	880
#define AR5K_SLOT_TIME_MAX	0xffff

/* channel_flags */
#define	CHANNEL_CW_INT	0x0008	/* Contention Window interference detected */
#define	CHANNEL_TURBO	0x0010	/* Turbo Channel */
#define	CHANNEL_CCK	0x0020	/* CCK channel */
#define	CHANNEL_OFDM	0x0040	/* OFDM channel */
#define	CHANNEL_2GHZ	0x0080	/* 2GHz channel. */
#define	CHANNEL_5GHZ	0x0100	/* 5GHz channel */
#define	CHANNEL_PASSIVE	0x0200	/* Only passive scan allowed */
#define	CHANNEL_DYN	0x0400	/* Dynamic CCK-OFDM channel (for g operation) */
#define	CHANNEL_XR	0x0800	/* XR channel */

#define	CHANNEL_A	(CHANNEL_5GHZ|CHANNEL_OFDM)
#define	CHANNEL_B	(CHANNEL_2GHZ|CHANNEL_CCK)
#define	CHANNEL_G	(CHANNEL_2GHZ|CHANNEL_OFDM)
#define	CHANNEL_T	(CHANNEL_5GHZ|CHANNEL_OFDM|CHANNEL_TURBO)
#define	CHANNEL_TG	(CHANNEL_2GHZ|CHANNEL_OFDM|CHANNEL_TURBO)
#define	CHANNEL_108A	CHANNEL_T
#define	CHANNEL_108G	CHANNEL_TG
#define	CHANNEL_X	(CHANNEL_5GHZ|CHANNEL_OFDM|CHANNEL_XR)

#define	CHANNEL_ALL 	(CHANNEL_OFDM|CHANNEL_CCK|CHANNEL_2GHZ|CHANNEL_5GHZ| \
		CHANNEL_TURBO)

#define	CHANNEL_ALL_NOTURBO 	(CHANNEL_ALL & ~CHANNEL_TURBO)
#define CHANNEL_MODES		CHANNEL_ALL

/*
 * Used internaly for reset_tx_queue).
 * Also see struct struct ieee80211_channel.
 */
#define IS_CHAN_XR(_c)	((_c.hw_value & CHANNEL_XR) != 0)
#define IS_CHAN_B(_c)	((_c.hw_value & CHANNEL_B) != 0)

/*
 * The following structure is used to map 2GHz channels to
 * 5GHz Atheros channels.
 * TODO: Clean up
 */
struct ath5k_athchan_2ghz {
	u32	a2_flags;
	u16	a2_athchan;
};


/******************\
  RATE DEFINITIONS
\******************/

/**
 * Seems the ar5xxx harware supports up to 32 rates, indexed by 1-32.
 *
 * The rate code is used to get the RX rate or set the TX rate on the
 * hardware descriptors. It is also used for internal modulation control
 * and settings.
 *
 * This is the hardware rate map we are aware of:
 *
 * rate_code   0x01    0x02    0x03    0x04    0x05    0x06    0x07    0x08
 * rate_kbps   3000    1000    ?       ?       ?       2000    500     48000
 *
 * rate_code   0x09    0x0A    0x0B    0x0C    0x0D    0x0E    0x0F    0x10
 * rate_kbps   24000   12000   6000    54000   36000   18000   9000    ?
 *
 * rate_code   17      18      19      20      21      22      23      24
 * rate_kbps   ?       ?       ?       ?       ?       ?       ?       11000
 *
 * rate_code   25      26      27      28      29      30      31      32
 * rate_kbps   5500    2000    1000    11000S  5500S   2000S   ?       ?
 *
 * "S" indicates CCK rates with short preamble.
 *
 * AR5211 has different rate codes for CCK (802.11B) rates. It only uses the
 * lowest 4 bits, so they are the same as below with a 0xF mask.
 * (0xB, 0xA, 0x9 and 0x8 for 1M, 2M, 5.5M and 11M).
 * We handle this in ath5k_setup_bands().
 */
#define AR5K_MAX_RATES 32

/* B */
#define ATH5K_RATE_CODE_1M	0x1B
#define ATH5K_RATE_CODE_2M	0x1A
#define ATH5K_RATE_CODE_5_5M	0x19
#define ATH5K_RATE_CODE_11M	0x18
/* A and G */
#define ATH5K_RATE_CODE_6M	0x0B
#define ATH5K_RATE_CODE_9M	0x0F
#define ATH5K_RATE_CODE_12M	0x0A
#define ATH5K_RATE_CODE_18M	0x0E
#define ATH5K_RATE_CODE_24M	0x09
#define ATH5K_RATE_CODE_36M	0x0D
#define ATH5K_RATE_CODE_48M	0x08
#define ATH5K_RATE_CODE_54M	0x0C
/* XR */
#define ATH5K_RATE_CODE_XR_500K	0x07
#define ATH5K_RATE_CODE_XR_1M	0x02
#define ATH5K_RATE_CODE_XR_2M	0x06
#define ATH5K_RATE_CODE_XR_3M	0x01

/* adding this flag to rate_code enables short preamble */
#define AR5K_SET_SHORT_PREAMBLE 0x04

/*
 * Crypto definitions
 */

#define AR5K_KEYCACHE_SIZE	8

/***********************\
 HW RELATED DEFINITIONS
\***********************/

/*
 * Misc definitions
 */
#define	AR5K_RSSI_EP_MULTIPLIER	(1<<7)

#define AR5K_ASSERT_ENTRY(_e, _s) do {		\
	if (_e >= _s)				\
		return (false);			\
} while (0)

/*
 * Hardware interrupt abstraction
 */

/**
 * enum ath5k_int - Hardware interrupt masks helpers
 *
 * @AR5K_INT_RX: mask to identify received frame interrupts, of type
 * 	AR5K_ISR_RXOK or AR5K_ISR_RXERR
 * @AR5K_INT_RXDESC: Request RX descriptor/Read RX descriptor (?)
 * @AR5K_INT_RXNOFRM: No frame received (?)
 * @AR5K_INT_RXEOL: received End Of List for VEOL (Virtual End Of List). The
 * 	Queue Control Unit (QCU) signals an EOL interrupt only if a descriptor's
 * 	LinkPtr is NULL. For more details, refer to:
 * 	http://www.freepatentsonline.com/20030225739.html
 * @AR5K_INT_RXORN: Indicates we got RX overrun (eg. no more descriptors).
 * 	Note that Rx overrun is not always fatal, on some chips we can continue
 * 	operation without reseting the card, that's why int_fatal is not
 * 	common for all chips.
 * @AR5K_INT_TX: mask to identify received frame interrupts, of type
 * 	AR5K_ISR_TXOK or AR5K_ISR_TXERR
 * @AR5K_INT_TXDESC: Request TX descriptor/Read TX status descriptor (?)
 * @AR5K_INT_TXURN: received when we should increase the TX trigger threshold
 * 	We currently do increments on interrupt by
 * 	(AR5K_TUNE_MAX_TX_FIFO_THRES - current_trigger_level) / 2
 * @AR5K_INT_MIB: Indicates the Management Information Base counters should be
 * 	checked. We should do this with ath5k_hw_update_mib_counters() but
 * 	it seems we should also then do some noise immunity work.
 * @AR5K_INT_RXPHY: RX PHY Error
 * @AR5K_INT_RXKCM: RX Key cache miss
 * @AR5K_INT_SWBA: SoftWare Beacon Alert - indicates its time to send a
 * 	beacon that must be handled in software. The alternative is if you
 * 	have VEOL support, in that case you let the hardware deal with things.
 * @AR5K_INT_BMISS: If in STA mode this indicates we have stopped seeing
 * 	beacons from the AP have associated with, we should probably try to
 * 	reassociate. When in IBSS mode this might mean we have not received
 * 	any beacons from any local stations. Note that every station in an
 * 	IBSS schedules to send beacons at the Target Beacon Transmission Time
 * 	(TBTT) with a random backoff.
 * @AR5K_INT_BNR: Beacon Not Ready interrupt - ??
 * @AR5K_INT_GPIO: GPIO interrupt is used for RF Kill, disabled for now
 * 	until properly handled
 * @AR5K_INT_FATAL: Fatal errors were encountered, typically caused by DMA
 * 	errors. These types of errors we can enable seem to be of type
 * 	AR5K_SIMR2_MCABT, AR5K_SIMR2_SSERR and AR5K_SIMR2_DPERR.
 * @AR5K_INT_GLOBAL: Used to clear and set the IER
 * @AR5K_INT_NOCARD: signals the card has been removed
 * @AR5K_INT_COMMON: common interrupts shared amogst MACs with the same
 * 	bit value
 *
 * These are mapped to take advantage of some common bits
 * between the MACs, to be able to set intr properties
 * easier. Some of them are not used yet inside hw.c. Most map
 * to the respective hw interrupt value as they are common amogst different
 * MACs.
 */
enum ath5k_int {
	AR5K_INT_RXOK	= 0x00000001,
	AR5K_INT_RXDESC	= 0x00000002,
	AR5K_INT_RXERR	= 0x00000004,
	AR5K_INT_RXNOFRM = 0x00000008,
	AR5K_INT_RXEOL	= 0x00000010,
	AR5K_INT_RXORN	= 0x00000020,
	AR5K_INT_TXOK	= 0x00000040,
	AR5K_INT_TXDESC	= 0x00000080,
	AR5K_INT_TXERR	= 0x00000100,
	AR5K_INT_TXNOFRM = 0x00000200,
	AR5K_INT_TXEOL	= 0x00000400,
	AR5K_INT_TXURN	= 0x00000800,
	AR5K_INT_MIB	= 0x00001000,
	AR5K_INT_SWI	= 0x00002000,
	AR5K_INT_RXPHY	= 0x00004000,
	AR5K_INT_RXKCM	= 0x00008000,
	AR5K_INT_SWBA	= 0x00010000,
	AR5K_INT_BRSSI	= 0x00020000,
	AR5K_INT_BMISS	= 0x00040000,
	AR5K_INT_FATAL	= 0x00080000, /* Non common */
	AR5K_INT_BNR	= 0x00100000, /* Non common */
	AR5K_INT_TIM	= 0x00200000, /* Non common */
	AR5K_INT_DTIM	= 0x00400000, /* Non common */
	AR5K_INT_DTIM_SYNC =	0x00800000, /* Non common */
	AR5K_INT_GPIO	=	0x01000000,
	AR5K_INT_BCN_TIMEOUT =	0x02000000, /* Non common */
	AR5K_INT_CAB_TIMEOUT =	0x04000000, /* Non common */
	AR5K_INT_RX_DOPPLER =	0x08000000, /* Non common */
	AR5K_INT_QCBRORN =	0x10000000, /* Non common */
	AR5K_INT_QCBRURN =	0x20000000, /* Non common */
	AR5K_INT_QTRIG	=	0x40000000, /* Non common */
	AR5K_INT_GLOBAL =	0x80000000,

	AR5K_INT_COMMON  = AR5K_INT_RXOK
		| AR5K_INT_RXDESC
		| AR5K_INT_RXERR
		| AR5K_INT_RXNOFRM
		| AR5K_INT_RXEOL
		| AR5K_INT_RXORN
		| AR5K_INT_TXOK
		| AR5K_INT_TXDESC
		| AR5K_INT_TXERR
		| AR5K_INT_TXNOFRM
		| AR5K_INT_TXEOL
		| AR5K_INT_TXURN
		| AR5K_INT_MIB
		| AR5K_INT_SWI
		| AR5K_INT_RXPHY
		| AR5K_INT_RXKCM
		| AR5K_INT_SWBA
		| AR5K_INT_BRSSI
		| AR5K_INT_BMISS
		| AR5K_INT_GPIO
		| AR5K_INT_GLOBAL,

	AR5K_INT_NOCARD	= 0xffffffff
};

/*
 * Power management
 */
enum ath5k_power_mode {
	AR5K_PM_UNDEFINED = 0,
	AR5K_PM_AUTO,
	AR5K_PM_AWAKE,
	AR5K_PM_FULL_SLEEP,
	AR5K_PM_NETWORK_SLEEP,
};

/*
 * These match net80211 definitions (not used in
 * mac80211).
 * TODO: Clean this up
 */
#define AR5K_LED_INIT	0 /*IEEE80211_S_INIT*/
#define AR5K_LED_SCAN	1 /*IEEE80211_S_SCAN*/
#define AR5K_LED_AUTH	2 /*IEEE80211_S_AUTH*/
#define AR5K_LED_ASSOC	3 /*IEEE80211_S_ASSOC*/
#define AR5K_LED_RUN	4 /*IEEE80211_S_RUN*/

/* GPIO-controlled software LED */
#define AR5K_SOFTLED_PIN	0
#define AR5K_SOFTLED_ON		0
#define AR5K_SOFTLED_OFF	1

/*
 * Chipset capabilities -see ath5k_hw_get_capability-
 * get_capability function is not yet fully implemented
 * in ath5k so most of these don't work yet...
 * TODO: Implement these & merge with _TUNE_ stuff above
 */
enum ath5k_capability_type {
	AR5K_CAP_REG_DMN		= 0,	/* Used to get current reg. domain id */
	AR5K_CAP_TKIP_MIC		= 2,	/* Can handle TKIP MIC in hardware */
	AR5K_CAP_TKIP_SPLIT		= 3,	/* TKIP uses split keys */
	AR5K_CAP_PHYCOUNTERS		= 4,	/* PHY error counters */
	AR5K_CAP_DIVERSITY		= 5,	/* Supports fast diversity */
	AR5K_CAP_NUM_TXQUEUES		= 6,	/* Used to get max number of hw txqueues */
	AR5K_CAP_VEOL			= 7,	/* Supports virtual EOL */
	AR5K_CAP_COMPRESSION		= 8,	/* Supports compression */
	AR5K_CAP_BURST			= 9,	/* Supports packet bursting */
	AR5K_CAP_FASTFRAME		= 10,	/* Supports fast frames */
	AR5K_CAP_TXPOW			= 11,	/* Used to get global tx power limit */
	AR5K_CAP_TPC			= 12,	/* Can do per-packet tx power control (needed for 802.11a) */
	AR5K_CAP_BSSIDMASK		= 13,	/* Supports bssid mask */
	AR5K_CAP_MCAST_KEYSRCH		= 14,	/* Supports multicast key search */
	AR5K_CAP_TSF_ADJUST		= 15,	/* Supports beacon tsf adjust */
	AR5K_CAP_XR			= 16,	/* Supports XR mode */
	AR5K_CAP_WME_TKIPMIC 		= 17,	/* Supports TKIP MIC when using WMM */
	AR5K_CAP_CHAN_HALFRATE 		= 18,	/* Supports half rate channels */
	AR5K_CAP_CHAN_QUARTERRATE 	= 19,	/* Supports quarter rate channels */
	AR5K_CAP_RFSILENT		= 20,	/* Supports RFsilent */
};


/* XXX: we *may* move cap_range stuff to struct wiphy */
struct ath5k_capabilities {
	/*
	 * Supported PHY modes
	 * (ie. CHANNEL_A, CHANNEL_B, ...)
	 */
	DECLARE_BITMAP(cap_mode, AR5K_MODE_MAX);

	/*
	 * Frequency range (without regulation restrictions)
	 */
	struct {
		u16	range_2ghz_min;
		u16	range_2ghz_max;
		u16	range_5ghz_min;
		u16	range_5ghz_max;
	} cap_range;

	/*
	 * Values stored in the EEPROM (some of them...)
	 */
	struct ath5k_eeprom_info	cap_eeprom;

	/*
	 * Queue information
	 */
	struct {
		u8	q_tx_num;
	} cap_queues;
};


/***************************************\
  HARDWARE ABSTRACTION LAYER STRUCTURE
\***************************************/

/*
 * Misc defines
 */

#define AR5K_MAX_GPIO		10
#define AR5K_MAX_RF_BANKS	8

/* TODO: Clean up and merge with ath5k_softc */
struct ath5k_hw {
	u32			ah_magic;

	struct ath5k_softc	*ah_sc;
	void __iomem		*ah_iobase;

	enum ath5k_int		ah_imr;

	enum nl80211_iftype	ah_op_mode;
	enum ath5k_power_mode	ah_power_mode;
	struct ieee80211_channel ah_current_channel;
	bool			ah_turbo;
	bool			ah_calibration;
	bool			ah_running;
	bool			ah_single_chip;
	bool			ah_combined_mic;

	u32			ah_mac_srev;
	u16			ah_mac_version;
	u16			ah_mac_revision;
	u16			ah_phy_revision;
	u16			ah_radio_5ghz_revision;
	u16			ah_radio_2ghz_revision;

	enum ath5k_version	ah_version;
	enum ath5k_radio	ah_radio;
	u32			ah_phy;

	bool			ah_5ghz;
	bool			ah_2ghz;

#define ah_regdomain		ah_capabilities.cap_regdomain.reg_current
#define ah_regdomain_hw		ah_capabilities.cap_regdomain.reg_hw
#define ah_modes		ah_capabilities.cap_mode
#define ah_ee_version		ah_capabilities.cap_eeprom.ee_version

	u32			ah_atim_window;
	u32			ah_aifs;
	u32			ah_cw_min;
	u32			ah_cw_max;
	bool			ah_software_retry;
	u32			ah_limit_tx_retries;

	u32			ah_antenna[AR5K_EEPROM_N_MODES][AR5K_ANT_MAX];
	bool			ah_ant_diversity;

	u8			ah_sta_id[ETH_ALEN];

	/* Current BSSID we are trying to assoc to / create.
	 * This is passed by mac80211 on config_interface() and cached here for
	 * use in resets */
	u8			ah_bssid[ETH_ALEN];
	u8			ah_bssid_mask[ETH_ALEN];

	u32			ah_gpio[AR5K_MAX_GPIO];
	int			ah_gpio_npins;

	struct ath5k_capabilities ah_capabilities;

	struct ath5k_txq_info	ah_txq[AR5K_NUM_TX_QUEUES];
	u32			ah_txq_status;
	u32			ah_txq_imr_txok;
	u32			ah_txq_imr_txerr;
	u32			ah_txq_imr_txurn;
	u32			ah_txq_imr_txdesc;
	u32			ah_txq_imr_txeol;
	u32			ah_txq_imr_cbrorn;
	u32			ah_txq_imr_cbrurn;
	u32			ah_txq_imr_qtrig;
	u32			ah_txq_imr_nofrm;
	u32			ah_txq_isr;
	u32			*ah_rf_banks;
	size_t			ah_rf_banks_size;
	size_t			ah_rf_regs_count;
	struct ath5k_gain	ah_gain;
	u8			ah_offset[AR5K_MAX_RF_BANKS];


	struct {
		/* Temporary tables used for interpolation */
		u8		tmpL[AR5K_EEPROM_N_PD_GAINS]
					[AR5K_EEPROM_POWER_TABLE_SIZE];
		u8		tmpR[AR5K_EEPROM_N_PD_GAINS]
					[AR5K_EEPROM_POWER_TABLE_SIZE];
		u8		txp_pd_table[AR5K_EEPROM_POWER_TABLE_SIZE * 2];
		u16		txp_rates_power_table[AR5K_MAX_RATES];
		u8		txp_min_idx;
		bool		txp_tpc;
		/* Values in 0.25dB units */
		s16		txp_min_pwr;
		s16		txp_max_pwr;
		s16		txp_offset;
		s16		txp_ofdm;
		/* Values in dB units */
		s16		txp_cck_ofdm_pwr_delta;
		s16		txp_cck_ofdm_gainf_delta;
	} ah_txpower;

	struct {
		bool		r_enabled;
		int		r_last_alert;
		struct ieee80211_channel r_last_channel;
	} ah_radar;

	/* noise floor from last periodic calibration */
	s32			ah_noise_floor;

	/*
	 * Function pointers
	 */
	int (*ah_setup_rx_desc)(struct ath5k_hw *ah, struct ath5k_desc *desc,
				u32 size, unsigned int flags);
	int (*ah_setup_tx_desc)(struct ath5k_hw *, struct ath5k_desc *,
		unsigned int, unsigned int, enum ath5k_pkt_type, unsigned int,
		unsigned int, unsigned int, unsigned int, unsigned int,
		unsigned int, unsigned int, unsigned int);
	int (*ah_setup_mrr_tx_desc)(struct ath5k_hw *, struct ath5k_desc *,
		unsigned int, unsigned int, unsigned int, unsigned int,
		unsigned int, unsigned int);
	int (*ah_proc_tx_desc)(struct ath5k_hw *, struct ath5k_desc *,
		struct ath5k_tx_status *);
	int (*ah_proc_rx_desc)(struct ath5k_hw *, struct ath5k_desc *,
		struct ath5k_rx_status *);
};

/*
 * Prototypes
 */

/* Attach/Detach Functions */
extern struct ath5k_hw *ath5k_hw_attach(struct ath5k_softc *sc, u8 mac_version);
extern void ath5k_hw_detach(struct ath5k_hw *ah);

/* LED functions */
extern int ath5k_init_leds(struct ath5k_softc *sc);
extern void ath5k_led_enable(struct ath5k_softc *sc);
extern void ath5k_led_off(struct ath5k_softc *sc);
extern void ath5k_unregister_leds(struct ath5k_softc *sc);

/* Reset Functions */
extern int ath5k_hw_nic_wakeup(struct ath5k_hw *ah, int flags, bool initial);
extern int ath5k_hw_reset(struct ath5k_hw *ah, enum nl80211_iftype op_mode, struct ieee80211_channel *channel, bool change_channel);
/* Power management functions */
extern int ath5k_hw_set_power(struct ath5k_hw *ah, enum ath5k_power_mode mode, bool set_chip, u16 sleep_duration);

/* DMA Related Functions */
extern void ath5k_hw_start_rx_dma(struct ath5k_hw *ah);
extern int ath5k_hw_stop_rx_dma(struct ath5k_hw *ah);
extern u32 ath5k_hw_get_rxdp(struct ath5k_hw *ah);
extern void ath5k_hw_set_rxdp(struct ath5k_hw *ah, u32 phys_addr);
extern int ath5k_hw_start_tx_dma(struct ath5k_hw *ah, unsigned int queue);
extern int ath5k_hw_stop_tx_dma(struct ath5k_hw *ah, unsigned int queue);
extern u32 ath5k_hw_get_txdp(struct ath5k_hw *ah, unsigned int queue);
extern int ath5k_hw_set_txdp(struct ath5k_hw *ah, unsigned int queue,
				u32 phys_addr);
extern int ath5k_hw_update_tx_triglevel(struct ath5k_hw *ah, bool increase);
/* Interrupt handling */
extern bool ath5k_hw_is_intr_pending(struct ath5k_hw *ah);
extern int ath5k_hw_get_isr(struct ath5k_hw *ah, enum ath5k_int *interrupt_mask);
extern enum ath5k_int ath5k_hw_set_imr(struct ath5k_hw *ah, enum
ath5k_int new_mask);
extern void ath5k_hw_update_mib_counters(struct ath5k_hw *ah, struct ieee80211_low_level_stats *stats);

/* EEPROM access functions */
extern int ath5k_eeprom_init(struct ath5k_hw *ah);
extern void ath5k_eeprom_detach(struct ath5k_hw *ah);
extern int ath5k_eeprom_read_mac(struct ath5k_hw *ah, u8 *mac);
extern bool ath5k_eeprom_is_hb63(struct ath5k_hw *ah);

/* Protocol Control Unit Functions */
extern int ath5k_hw_set_opmode(struct ath5k_hw *ah);
/* BSSID Functions */
extern void ath5k_hw_get_lladdr(struct ath5k_hw *ah, u8 *mac);
extern int ath5k_hw_set_lladdr(struct ath5k_hw *ah, const u8 *mac);
extern void ath5k_hw_set_associd(struct ath5k_hw *ah, const u8 *bssid, u16 assoc_id);
extern int ath5k_hw_set_bssid_mask(struct ath5k_hw *ah, const u8 *mask);
/* Receive start/stop functions */
extern void ath5k_hw_start_rx_pcu(struct ath5k_hw *ah);
extern void ath5k_hw_stop_rx_pcu(struct ath5k_hw *ah);
/* RX Filter functions */
extern void ath5k_hw_set_mcast_filter(struct ath5k_hw *ah, u32 filter0, u32 filter1);
extern int ath5k_hw_set_mcast_filter_idx(struct ath5k_hw *ah, u32 index);
extern int ath5k_hw_clear_mcast_filter_idx(struct ath5k_hw *ah, u32 index);
extern u32 ath5k_hw_get_rx_filter(struct ath5k_hw *ah);
extern void ath5k_hw_set_rx_filter(struct ath5k_hw *ah, u32 filter);
/* Beacon control functions */
extern u32 ath5k_hw_get_tsf32(struct ath5k_hw *ah);
extern u64 ath5k_hw_get_tsf64(struct ath5k_hw *ah);
extern void ath5k_hw_set_tsf64(struct ath5k_hw *ah, u64 tsf64);
extern void ath5k_hw_reset_tsf(struct ath5k_hw *ah);
extern void ath5k_hw_init_beacon(struct ath5k_hw *ah, u32 next_beacon, u32 interval);
#if 0
extern int ath5k_hw_set_beacon_timers(struct ath5k_hw *ah, const struct ath5k_beacon_state *state);
extern void ath5k_hw_reset_beacon(struct ath5k_hw *ah);
extern int ath5k_hw_beaconq_finish(struct ath5k_hw *ah, unsigned long phys_addr);
#endif
/* ACK bit rate */
void ath5k_hw_set_ack_bitrate_high(struct ath5k_hw *ah, bool high);
/* ACK/CTS Timeouts */
extern int ath5k_hw_set_ack_timeout(struct ath5k_hw *ah, unsigned int timeout);
extern unsigned int ath5k_hw_get_ack_timeout(struct ath5k_hw *ah);
extern int ath5k_hw_set_cts_timeout(struct ath5k_hw *ah, unsigned int timeout);
extern unsigned int ath5k_hw_get_cts_timeout(struct ath5k_hw *ah);
/* Key table (WEP) functions */
extern int ath5k_hw_reset_key(struct ath5k_hw *ah, u16 entry);
extern int ath5k_hw_is_key_valid(struct ath5k_hw *ah, u16 entry);
extern int ath5k_hw_set_key(struct ath5k_hw *ah, u16 entry, const struct ieee80211_key_conf *key, const u8 *mac);
extern int ath5k_hw_set_key_lladdr(struct ath5k_hw *ah, u16 entry, const u8 *mac);

/* Queue Control Unit, DFS Control Unit Functions */
extern int ath5k_hw_get_tx_queueprops(struct ath5k_hw *ah, int queue, struct ath5k_txq_info *queue_info);
extern int ath5k_hw_set_tx_queueprops(struct ath5k_hw *ah, int queue,
				const struct ath5k_txq_info *queue_info);
extern int ath5k_hw_setup_tx_queue(struct ath5k_hw *ah,
				enum ath5k_tx_queue queue_type,
				struct ath5k_txq_info *queue_info);
extern u32 ath5k_hw_num_tx_pending(struct ath5k_hw *ah, unsigned int queue);
extern void ath5k_hw_release_tx_queue(struct ath5k_hw *ah, unsigned int queue);
extern int ath5k_hw_reset_tx_queue(struct ath5k_hw *ah, unsigned int queue);
extern unsigned int ath5k_hw_get_slot_time(struct ath5k_hw *ah);
extern int ath5k_hw_set_slot_time(struct ath5k_hw *ah, unsigned int slot_time);

/* Hardware Descriptor Functions */
extern int ath5k_hw_init_desc_functions(struct ath5k_hw *ah);

/* GPIO Functions */
extern void ath5k_hw_set_ledstate(struct ath5k_hw *ah, unsigned int state);
extern int ath5k_hw_set_gpio_input(struct ath5k_hw *ah, u32 gpio);
extern int ath5k_hw_set_gpio_output(struct ath5k_hw *ah, u32 gpio);
extern u32 ath5k_hw_get_gpio(struct ath5k_hw *ah, u32 gpio);
extern int ath5k_hw_set_gpio(struct ath5k_hw *ah, u32 gpio, u32 val);
extern void ath5k_hw_set_gpio_intr(struct ath5k_hw *ah, unsigned int gpio, u32 interrupt_level);

/* Misc functions */
int ath5k_hw_set_capabilities(struct ath5k_hw *ah);
extern int ath5k_hw_get_capability(struct ath5k_hw *ah, enum ath5k_capability_type cap_type, u32 capability, u32 *result);
extern int ath5k_hw_enable_pspoll(struct ath5k_hw *ah, u8 *bssid, u16 assoc_id);
extern int ath5k_hw_disable_pspoll(struct ath5k_hw *ah);

/* Initial register settings functions */
extern int ath5k_hw_write_initvals(struct ath5k_hw *ah, u8 mode, bool change_channel);

/* Initialize RF */
extern int ath5k_hw_rfregs_init(struct ath5k_hw *ah,
				struct ieee80211_channel *channel,
				unsigned int mode);
extern int ath5k_hw_rfgain_init(struct ath5k_hw *ah, unsigned int freq);
extern enum ath5k_rfgain ath5k_hw_gainf_calibrate(struct ath5k_hw *ah);
extern int ath5k_hw_rfgain_opt_init(struct ath5k_hw *ah);
/* PHY/RF channel functions */
extern bool ath5k_channel_ok(struct ath5k_hw *ah, u16 freq, unsigned int flags);
extern int ath5k_hw_channel(struct ath5k_hw *ah, struct ieee80211_channel *channel);
/* PHY calibration */
extern int ath5k_hw_phy_calibrate(struct ath5k_hw *ah, struct ieee80211_channel *channel);
extern int ath5k_hw_noise_floor_calibration(struct ath5k_hw *ah, short freq);
/* Misc PHY functions */
extern u16 ath5k_hw_radio_revision(struct ath5k_hw *ah, unsigned int chan);
extern void ath5k_hw_set_def_antenna(struct ath5k_hw *ah, unsigned int ant);
extern unsigned int ath5k_hw_get_def_antenna(struct ath5k_hw *ah);
extern int ath5k_hw_phy_disable(struct ath5k_hw *ah);
/* TX power setup */
extern int ath5k_hw_txpower(struct ath5k_hw *ah, struct ieee80211_channel *channel, u8 ee_mode, u8 txpower);
extern int ath5k_hw_set_txpower_limit(struct ath5k_hw *ah, u8 ee_mode, u8 txpower);

/*
 * Functions used internaly
 */

/*
 * Translate usec to hw clock units
 * TODO: Half/quarter rate
 */
static inline unsigned int ath5k_hw_htoclock(unsigned int usec, bool turbo)
{
	return turbo ? (usec * 80) : (usec * 40);
}

/*
 * Translate hw clock units to usec
 * TODO: Half/quarter rate
 */
static inline unsigned int ath5k_hw_clocktoh(unsigned int clock, bool turbo)
{
	return turbo ? (clock / 80) : (clock / 40);
}

/*
 * Read from a register
 */
static inline u32 ath5k_hw_reg_read(struct ath5k_hw *ah, u16 reg)
{
	return ioread32(ah->ah_iobase + reg);
}

/*
 * Write to a register
 */
static inline void ath5k_hw_reg_write(struct ath5k_hw *ah, u32 val, u16 reg)
{
	iowrite32(val, ah->ah_iobase + reg);
}

#if defined(_ATH5K_RESET) || defined(_ATH5K_PHY)
/*
 * Check if a register write has been completed
 */
static int ath5k_hw_register_timeout(struct ath5k_hw *ah, u32 reg, u32 flag,
		u32 val, bool is_set)
{
	int i;
	u32 data;

	for (i = AR5K_TUNE_REGISTER_TIMEOUT; i > 0; i--) {
		data = ath5k_hw_reg_read(ah, reg);
		if (is_set && (data & flag))
			break;
		else if ((data & flag) == val)
			break;
		udelay(15);
	}

	return (i <= 0) ? -EAGAIN : 0;
}
#endif

static inline u32 ath5k_hw_bitswap(u32 val, unsigned int bits)
{
	u32 retval = 0, bit, i;

	for (i = 0; i < bits; i++) {
		bit = (val >> i) & 1;
		retval = (retval << 1) | bit;
	}

	return retval;
}

static inline int ath5k_pad_size(int hdrlen)
{
	return (hdrlen < 24) ? 0 : hdrlen & 3;
}

#endif
