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

/* TODO: Clean up channel debugging (doesn't work anyway) and start
 * working on reg. control code using all available eeprom information
 * (rev. engineering needed) */
#define CHAN_DEBUG	0

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/average.h>
#include <linux/leds.h>
#include <net/mac80211.h>

/* RX/TX descriptor hw structs
 * TODO: Driver part should only see sw structs */
#include "desc.h"

/* EEPROM structs/offsets
 * TODO: Make a more generic struct (eg. add more stuff to ath5k_capabilities)
 * and clean up common bits, then introduce set/get functions in eeprom.c */
#include "eeprom.h"
#include "debug.h"
#include "../ath.h"
#include "ani.h"

/* PCI IDs */
#define PCI_DEVICE_ID_ATHEROS_AR5210		0x0007 /* AR5210 */
#define PCI_DEVICE_ID_ATHEROS_AR5311		0x0011 /* AR5311 */
#define PCI_DEVICE_ID_ATHEROS_AR5211		0x0012 /* AR5211 */
#define PCI_DEVICE_ID_ATHEROS_AR5212		0x0013 /* AR5212 */
#define PCI_DEVICE_ID_3COM_3CRDAG675		0x0013 /* 3CRDAG675 (Atheros AR5212) */
#define PCI_DEVICE_ID_3COM_2_3CRPAG175		0x0013 /* 3CRPAG175 (Atheros AR5212) */
#define PCI_DEVICE_ID_ATHEROS_AR5210_AP		0x0207 /* AR5210 (Early) */
#define PCI_DEVICE_ID_ATHEROS_AR5212_IBM	0x1014 /* AR5212 (IBM MiniPCI) */
#define PCI_DEVICE_ID_ATHEROS_AR5210_DEFAULT	0x1107 /* AR5210 (no eeprom) */
#define PCI_DEVICE_ID_ATHEROS_AR5212_DEFAULT	0x1113 /* AR5212 (no eeprom) */
#define PCI_DEVICE_ID_ATHEROS_AR5211_DEFAULT	0x1112 /* AR5211 (no eeprom) */
#define PCI_DEVICE_ID_ATHEROS_AR5212_FPGA	0xf013 /* AR5212 (emulation board) */
#define PCI_DEVICE_ID_ATHEROS_AR5211_LEGACY	0xff12 /* AR5211 (emulation board) */
#define PCI_DEVICE_ID_ATHEROS_AR5211_FPGA11B	0xf11b /* AR5211 (emulation board) */
#define PCI_DEVICE_ID_ATHEROS_AR5312_REV2	0x0052 /* AR5312 WMAC (AP31) */
#define PCI_DEVICE_ID_ATHEROS_AR5312_REV7	0x0057 /* AR5312 WMAC (AP30-040) */
#define PCI_DEVICE_ID_ATHEROS_AR5312_REV8	0x0058 /* AR5312 WMAC (AP43-030) */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0014	0x0014 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0015	0x0015 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0016	0x0016 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0017	0x0017 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0018	0x0018 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR5212_0019	0x0019 /* AR5212 compatible */
#define PCI_DEVICE_ID_ATHEROS_AR2413		0x001a /* AR2413 (Griffin-lite) */
#define PCI_DEVICE_ID_ATHEROS_AR5413		0x001b /* AR5413 (Eagle) */
#define PCI_DEVICE_ID_ATHEROS_AR5424		0x001c /* AR5424 (Condor PCI-E) */
#define PCI_DEVICE_ID_ATHEROS_AR5416		0x0023 /* AR5416 */
#define PCI_DEVICE_ID_ATHEROS_AR5418		0x0024 /* AR5418 */

/****************************\
  GENERIC DRIVER DEFINITIONS
\****************************/

#define ATH5K_PRINTF(fmt, ...)						\
	pr_warn("%s: " fmt, __func__, ##__VA_ARGS__)

void __printf(3, 4)
_ath5k_printk(const struct ath5k_hw *ah, const char *level,
	      const char *fmt, ...);

#define ATH5K_PRINTK(_sc, _level, _fmt, ...)				\
	_ath5k_printk(_sc, _level, _fmt, ##__VA_ARGS__)

#define ATH5K_PRINTK_LIMIT(_sc, _level, _fmt, ...)			\
do {									\
	if (net_ratelimit())						\
		ATH5K_PRINTK(_sc, _level, _fmt, ##__VA_ARGS__); 	\
} while (0)

#define ATH5K_INFO(_sc, _fmt, ...)					\
	ATH5K_PRINTK(_sc, KERN_INFO, _fmt, ##__VA_ARGS__)

#define ATH5K_WARN(_sc, _fmt, ...)					\
	ATH5K_PRINTK_LIMIT(_sc, KERN_WARNING, _fmt, ##__VA_ARGS__)

#define ATH5K_ERR(_sc, _fmt, ...)					\
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

/*
 * Some tunable values (these should be changeable by the user)
 * TODO: Make use of them and add more options OR use debug/configfs
 */
#define AR5K_TUNE_DMA_BEACON_RESP		2
#define AR5K_TUNE_SW_BEACON_RESP		10
#define AR5K_TUNE_ADDITIONAL_SWBA_BACKOFF	0
#define AR5K_TUNE_MIN_TX_FIFO_THRES		1
#define AR5K_TUNE_MAX_TX_FIFO_THRES	((IEEE80211_MAX_FRAME_LEN / 64) + 1)
#define AR5K_TUNE_REGISTER_TIMEOUT		20000
/* Register for RSSI threshold has a mask of 0xff, so 255 seems to
 * be the max value. */
#define AR5K_TUNE_RSSI_THRES			129
/* This must be set when setting the RSSI threshold otherwise it can
 * prevent a reset. If AR5K_RSSI_THR is read after writing to it
 * the BMISS_THRES will be seen as 0, seems hardware doesn't keep
 * track of it. Max value depends on hardware. For AR5210 this is just 7.
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
#define AR5K_TUNE_CCA_MAX_GOOD_VALUE		-95
#define AR5K_TUNE_MAX_TXPOWER			63
#define AR5K_TUNE_DEFAULT_TXPOWER		25
#define AR5K_TUNE_TPC_TXPOWER			false
#define ATH5K_TUNE_CALIBRATION_INTERVAL_FULL    60000   /* 60 sec */
#define	ATH5K_TUNE_CALIBRATION_INTERVAL_SHORT	10000	/* 10 sec */
#define ATH5K_TUNE_CALIBRATION_INTERVAL_ANI	1000	/* 1 sec */
#define ATH5K_TX_COMPLETE_POLL_INT		3000	/* 3 sec */

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

/* Tx retry limit defaults from standard */
#define AR5K_INIT_RETRY_SHORT			7
#define AR5K_INIT_RETRY_LONG			4

/* Slot time */
#define AR5K_INIT_SLOT_TIME_TURBO		6
#define AR5K_INIT_SLOT_TIME_DEFAULT		9
#define	AR5K_INIT_SLOT_TIME_HALF_RATE		13
#define	AR5K_INIT_SLOT_TIME_QUARTER_RATE	21
#define	AR5K_INIT_SLOT_TIME_B			20
#define AR5K_SLOT_TIME_MAX			0xffff

/* SIFS */
#define	AR5K_INIT_SIFS_TURBO			6
#define	AR5K_INIT_SIFS_DEFAULT_BG		10
#define	AR5K_INIT_SIFS_DEFAULT_A		16
#define	AR5K_INIT_SIFS_HALF_RATE		32
#define AR5K_INIT_SIFS_QUARTER_RATE		64

/* Used to calculate tx time for non 5/10/40MHz
 * operation */
/* It's preamble time + signal time (16 + 4) */
#define	AR5K_INIT_OFDM_PREAMPLE_TIME		20
/* Preamble time for 40MHz (turbo) operation (min ?) */
#define	AR5K_INIT_OFDM_PREAMBLE_TIME_MIN	14
#define	AR5K_INIT_OFDM_SYMBOL_TIME		4
#define	AR5K_INIT_OFDM_PLCP_BITS		22

/* Rx latency for 5 and 10MHz operation (max ?) */
#define AR5K_INIT_RX_LAT_MAX			63
/* Tx latencies from initvals (5212 only but no problem
 * because we only tweak them on 5212) */
#define	AR5K_INIT_TX_LAT_A			54
#define	AR5K_INIT_TX_LAT_BG			384
/* Tx latency for 40MHz (turbo) operation (min ?) */
#define	AR5K_INIT_TX_LAT_MIN			32
/* Default Tx/Rx latencies (same for 5211)*/
#define AR5K_INIT_TX_LATENCY_5210		54
#define	AR5K_INIT_RX_LATENCY_5210		29

/* Tx frame to Tx data start delay */
#define AR5K_INIT_TXF2TXD_START_DEFAULT		14
#define AR5K_INIT_TXF2TXD_START_DELAY_10MHZ	12
#define AR5K_INIT_TXF2TXD_START_DELAY_5MHZ	13

/* We need to increase PHY switch and agc settling time
 * on turbo mode */
#define	AR5K_SWITCH_SETTLING			5760
#define	AR5K_SWITCH_SETTLING_TURBO		7168

#define	AR5K_AGC_SETTLING			28
/* 38 on 5210 but shouldn't matter */
#define	AR5K_AGC_SETTLING_TURBO			37



/*****************************\
* GENERIC CHIPSET DEFINITIONS *
\*****************************/

/**
 * enum ath5k_version - MAC Chips
 * @AR5K_AR5210: AR5210 (Crete)
 * @AR5K_AR5211: AR5211 (Oahu/Maui)
 * @AR5K_AR5212: AR5212 (Venice) and newer
 */
enum ath5k_version {
	AR5K_AR5210	= 0,
	AR5K_AR5211	= 1,
	AR5K_AR5212	= 2,
};

/**
 * enum ath5k_radio - PHY Chips
 * @AR5K_RF5110: RF5110 (Fez)
 * @AR5K_RF5111: RF5111 (Sombrero)
 * @AR5K_RF5112: RF2112/5112(A) (Derby/Derby2)
 * @AR5K_RF2413: RF2413/2414 (Griffin/Griffin-Lite)
 * @AR5K_RF5413: RF5413/5414/5424 (Eagle/Condor)
 * @AR5K_RF2316: RF2315/2316 (Cobra SoC)
 * @AR5K_RF2317: RF2317 (Spider SoC)
 * @AR5K_RF2425: RF2425/2417 (Swan/Nalla)
 */
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

#define AR5K_SREV_UNKNOWN	0xffff

#define AR5K_SREV_AR5210	0x00 /* Crete */
#define AR5K_SREV_AR5311	0x10 /* Maui 1 */
#define AR5K_SREV_AR5311A	0x20 /* Maui 2 */
#define AR5K_SREV_AR5311B	0x30 /* Spirit */
#define AR5K_SREV_AR5211	0x40 /* Oahu */
#define AR5K_SREV_AR5212	0x50 /* Venice */
#define AR5K_SREV_AR5312_R2	0x52 /* AP31 */
#define AR5K_SREV_AR5212_V4	0x54 /* ??? */
#define AR5K_SREV_AR5213	0x55 /* ??? */
#define AR5K_SREV_AR5312_R7	0x57 /* AP30 */
#define AR5K_SREV_AR2313_R8	0x58 /* AP43 */
#define AR5K_SREV_AR5213A	0x59 /* Hainan */
#define AR5K_SREV_AR2413	0x78 /* Griffin lite */
#define AR5K_SREV_AR2414	0x70 /* Griffin */
#define AR5K_SREV_AR2315_R6	0x86 /* AP51-Light */
#define AR5K_SREV_AR2315_R7	0x87 /* AP51-Full */
#define AR5K_SREV_AR5424	0x90 /* Condor */
#define AR5K_SREV_AR2317_R1	0x90 /* AP61-Light */
#define AR5K_SREV_AR2317_R2	0x91 /* AP61-Full */
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

/* TODO add support to mac80211 for vendor-specific rates and modes */

/**
 * DOC: Atheros XR
 *
 * Some of this information is based on Documentation from:
 *
 * http://madwifi-project.org/wiki/ChipsetFeatures/SuperAG
 *
 * Atheros' eXtended Range - range enhancing extension is a modulation scheme
 * that is supposed to double the link distance between an Atheros XR-enabled
 * client device with an Atheros XR-enabled access point. This is achieved
 * by increasing the receiver sensitivity up to, -105dBm, which is about 20dB
 * above what the 802.11 specifications demand. In addition, new (proprietary)
 * data rates are introduced: 3, 2, 1, 0.5 and 0.25 MBit/s.
 *
 * Please note that can you either use XR or TURBO but you cannot use both,
 * they are exclusive.
 *
 * Also note that we do not plan to support XR mode at least for now. You can
 * get a mode similar to XR by using 5MHz bwmode.
 */


/**
 * DOC: Atheros SuperAG
 *
 * In addition to XR we have another modulation scheme called TURBO mode
 * that is supposed to provide a throughput transmission speed up to 40Mbit/s
 * -60Mbit/s at a 108Mbit/s signaling rate achieved through the bonding of two
 * 54Mbit/s 802.11g channels. To use this feature both ends must support it.
 * There is also a distinction between "static" and "dynamic" turbo modes:
 *
 * - Static: is the dumb version: devices set to this mode stick to it until
 *     the mode is turned off.
 *
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
 * The channel bonding seems to be driver specific though.
 *
 * In addition to TURBO modes we also have the following features for even
 * greater speed-up:
 *
 * - Bursting: allows multiple frames to be sent at once, rather than pausing
 *     after each frame. Bursting is a standards-compliant feature that can be
 *     used with any Access Point.
 *
 * - Fast frames: increases the amount of information that can be sent per
 *     frame, also resulting in a reduction of transmission overhead. It is a
 *     proprietary feature that needs to be supported by the Access Point.
 *
 * - Compression: data frames are compressed in real time using a Lempel Ziv
 *     algorithm. This is done transparently. Once this feature is enabled,
 *     compression and decompression takes place inside the chipset, without
 *     putting additional load on the host CPU.
 *
 * As with XR we also don't plan to support SuperAG features for now. You can
 * get a mode similar to TURBO by using 40MHz bwmode.
 */


/**
 * enum ath5k_driver_mode - PHY operation mode
 * @AR5K_MODE_11A: 802.11a
 * @AR5K_MODE_11B: 802.11b
 * @AR5K_MODE_11G: 801.11g
 * @AR5K_MODE_MAX: Used for boundary checks
 *
 * Do not change the order here, we use these as
 * array indices and it also maps EEPROM structures.
 */
enum ath5k_driver_mode {
	AR5K_MODE_11A		=	0,
	AR5K_MODE_11B		=	1,
	AR5K_MODE_11G		=	2,
	AR5K_MODE_MAX		=	3
};

/**
 * enum ath5k_ant_mode - Antenna operation mode
 * @AR5K_ANTMODE_DEFAULT: Default antenna setup
 * @AR5K_ANTMODE_FIXED_A: Only antenna A is present
 * @AR5K_ANTMODE_FIXED_B: Only antenna B is present
 * @AR5K_ANTMODE_SINGLE_AP: STA locked on a single ap
 * @AR5K_ANTMODE_SECTOR_AP: AP with tx antenna set on tx desc
 * @AR5K_ANTMODE_SECTOR_STA: STA with tx antenna set on tx desc
 * @AR5K_ANTMODE_DEBUG: Debug mode -A -> Rx, B-> Tx-
 * @AR5K_ANTMODE_MAX: Used for boundary checks
 *
 * For more infos on antenna control check out phy.c
 */
enum ath5k_ant_mode {
	AR5K_ANTMODE_DEFAULT	= 0,
	AR5K_ANTMODE_FIXED_A	= 1,
	AR5K_ANTMODE_FIXED_B	= 2,
	AR5K_ANTMODE_SINGLE_AP	= 3,
	AR5K_ANTMODE_SECTOR_AP	= 4,
	AR5K_ANTMODE_SECTOR_STA	= 5,
	AR5K_ANTMODE_DEBUG	= 6,
	AR5K_ANTMODE_MAX,
};

/**
 * enum ath5k_bw_mode - Bandwidth operation mode
 * @AR5K_BWMODE_DEFAULT: 20MHz, default operation
 * @AR5K_BWMODE_5MHZ: Quarter rate
 * @AR5K_BWMODE_10MHZ: Half rate
 * @AR5K_BWMODE_40MHZ: Turbo
 */
enum ath5k_bw_mode {
	AR5K_BWMODE_DEFAULT	= 0,
	AR5K_BWMODE_5MHZ	= 1,
	AR5K_BWMODE_10MHZ	= 2,
	AR5K_BWMODE_40MHZ	= 3
};



/****************\
  TX DEFINITIONS
\****************/

/**
 * struct ath5k_tx_status - TX Status descriptor
 * @ts_seqnum: Sequence number
 * @ts_tstamp: Timestamp
 * @ts_status: Status code
 * @ts_final_idx: Final transmission series index
 * @ts_final_retry: Final retry count
 * @ts_rssi: RSSI for received ACK
 * @ts_shortretry: Short retry count
 * @ts_virtcol: Virtual collision count
 * @ts_antenna: Antenna used
 *
 * TX status descriptor gets filled by the hw
 * on each transmission attempt.
 */
struct ath5k_tx_status {
	u16	ts_seqnum;
	u16	ts_tstamp;
	u8	ts_status;
	u8	ts_final_idx;
	u8	ts_final_retry;
	s8	ts_rssi;
	u8	ts_shortretry;
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
 * @AR5K_TX_QUEUE_BEACON: The beacon queue
 * @AR5K_TX_QUEUE_CAB: The after-beacon queue
 * @AR5K_TX_QUEUE_UAPSD: Unscheduled Automatic Power Save Delivery queue
 */
enum ath5k_tx_queue {
	AR5K_TX_QUEUE_INACTIVE = 0,
	AR5K_TX_QUEUE_DATA,
	AR5K_TX_QUEUE_BEACON,
	AR5K_TX_QUEUE_CAB,
	AR5K_TX_QUEUE_UAPSD,
};

#define	AR5K_NUM_TX_QUEUES		10
#define	AR5K_NUM_TX_QUEUES_NOQCU	2

/**
 * enum ath5k_tx_queue_subtype - Queue sub-types to classify normal data queues
 * @AR5K_WME_AC_BK: Background traffic
 * @AR5K_WME_AC_BE: Best-effort (normal) traffic
 * @AR5K_WME_AC_VI: Video traffic
 * @AR5K_WME_AC_VO: Voice traffic
 *
 * These are the 4 Access Categories as defined in
 * WME spec. 0 is the lowest priority and 4 is the
 * highest. Normal data that hasn't been classified
 * goes to the Best Effort AC.
 */
enum ath5k_tx_queue_subtype {
	AR5K_WME_AC_BK = 0,
	AR5K_WME_AC_BE,
	AR5K_WME_AC_VI,
	AR5K_WME_AC_VO,
};

/**
 * enum ath5k_tx_queue_id - Queue ID numbers as returned by the hw functions
 * @AR5K_TX_QUEUE_ID_NOQCU_DATA: Data queue on AR5210 (no QCU available)
 * @AR5K_TX_QUEUE_ID_NOQCU_BEACON: Beacon queue on AR5210 (no QCU available)
 * @AR5K_TX_QUEUE_ID_DATA_MIN: Data queue min index
 * @AR5K_TX_QUEUE_ID_DATA_MAX: Data queue max index
 * @AR5K_TX_QUEUE_ID_CAB: Content after beacon queue
 * @AR5K_TX_QUEUE_ID_BEACON: Beacon queue
 * @AR5K_TX_QUEUE_ID_UAPSD: Urgent Automatic Power Save Delivery,
 *
 * Each number represents a hw queue. If hw does not support hw queues
 * (eg 5210) all data goes in one queue.
 */
enum ath5k_tx_queue_id {
	AR5K_TX_QUEUE_ID_NOQCU_DATA	= 0,
	AR5K_TX_QUEUE_ID_NOQCU_BEACON	= 1,
	AR5K_TX_QUEUE_ID_DATA_MIN	= 0,
	AR5K_TX_QUEUE_ID_DATA_MAX	= 3,
	AR5K_TX_QUEUE_ID_UAPSD		= 7,
	AR5K_TX_QUEUE_ID_CAB		= 8,
	AR5K_TX_QUEUE_ID_BEACON		= 9,
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

/**
 * struct ath5k_txq - Transmit queue state
 * @qnum: Hardware q number
 * @link: Link ptr in last TX desc
 * @q: Transmit queue (&struct list_head)
 * @lock: Lock on q and link
 * @setup: Is the queue configured
 * @txq_len:Number of queued buffers
 * @txq_max: Max allowed num of queued buffers
 * @txq_poll_mark: Used to check if queue got stuck
 * @txq_stuck: Queue stuck counter
 *
 * One of these exists for each hardware transmit queue.
 * Packets sent to us from above are assigned to queues based
 * on their priority.  Not all devices support a complete set
 * of hardware transmit queues. For those devices the array
 * sc_ac2q will map multiple priorities to fewer hardware queues
 * (typically all to one hardware queue).
 */
struct ath5k_txq {
	unsigned int		qnum;
	u32			*link;
	struct list_head	q;
	spinlock_t		lock;
	bool			setup;
	int			txq_len;
	int			txq_max;
	bool			txq_poll_mark;
	unsigned int		txq_stuck;
};

/**
 * struct ath5k_txq_info - A struct to hold TX queue's parameters
 * @tqi_type: One of enum ath5k_tx_queue
 * @tqi_subtype: One of enum ath5k_tx_queue_subtype
 * @tqi_flags: TX queue flags (see above)
 * @tqi_aifs: Arbitrated Inter-frame Space
 * @tqi_cw_min: Minimum Contention Window
 * @tqi_cw_max: Maximum Contention Window
 * @tqi_cbr_period: Constant bit rate period
 * @tqi_ready_time: Time queue waits after an event when RDYTIME is enabled
 */
struct ath5k_txq_info {
	enum ath5k_tx_queue tqi_type;
	enum ath5k_tx_queue_subtype tqi_subtype;
	u16	tqi_flags;
	u8	tqi_aifs;
	u16	tqi_cw_min;
	u16	tqi_cw_max;
	u32	tqi_cbr_period;
	u32	tqi_cbr_overflow_limit;
	u32	tqi_burst_time;
	u32	tqi_ready_time;
};

/**
 * enum ath5k_pkt_type - Transmit packet types
 * @AR5K_PKT_TYPE_NORMAL: Normal data
 * @AR5K_PKT_TYPE_ATIM: ATIM
 * @AR5K_PKT_TYPE_PSPOLL: PS-Poll
 * @AR5K_PKT_TYPE_BEACON: Beacon
 * @AR5K_PKT_TYPE_PROBE_RESP: Probe response
 * @AR5K_PKT_TYPE_PIFS: PIFS
 * Used on tx control descriptor
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



/****************\
  RX DEFINITIONS
\****************/

/**
 * struct ath5k_rx_status - RX Status descriptor
 * @rs_datalen: Data length
 * @rs_tstamp: Timestamp
 * @rs_status: Status code
 * @rs_phyerr: PHY error mask
 * @rs_rssi: RSSI in 0.5dbm units
 * @rs_keyix: Index to the key used for decrypting
 * @rs_rate: Rate used to decode the frame
 * @rs_antenna: Antenna used to receive the frame
 * @rs_more: Indicates this is a frame fragment (Fast frames)
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
#define AR5K_RXKEYIX_INVALID	((u8) -1)
#define AR5K_TXKEYIX_INVALID	((u32) -1)


/**************************\
 BEACON TIMERS DEFINITIONS
\**************************/

#define AR5K_BEACON_PERIOD	0x0000ffff
#define AR5K_BEACON_ENA		0x00800000 /*enable beacon xmit*/
#define AR5K_BEACON_RESET_TSF	0x01000000 /*force a TSF reset*/


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

/**
 * enum ath5k_rfgain - RF Gain optimization engine state
 * @AR5K_RFGAIN_INACTIVE: Engine disabled
 * @AR5K_RFGAIN_ACTIVE: Probe active
 * @AR5K_RFGAIN_READ_REQUESTED: Probe requested
 * @AR5K_RFGAIN_NEED_CHANGE: Gain_F needs change
 */
enum ath5k_rfgain {
	AR5K_RFGAIN_INACTIVE = 0,
	AR5K_RFGAIN_ACTIVE,
	AR5K_RFGAIN_READ_REQUESTED,
	AR5K_RFGAIN_NEED_CHANGE,
};

/**
 * struct ath5k_gain - RF Gain optimization engine state data
 * @g_step_idx: Current step index
 * @g_current: Current gain
 * @g_target: Target gain
 * @g_low: Low gain boundary
 * @g_high: High gain boundary
 * @g_f_corr: Gain_F correction
 * @g_state: One of enum ath5k_rfgain
 */
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

/**
 * struct ath5k_athchan_2ghz - 2GHz to 5GHZ map for RF5111
 * @a2_flags: Channel flags (internal)
 * @a2_athchan: HW channel number (internal)
 *
 * This structure is used to map 2GHz channels to
 * 5GHz Atheros channels on 2111 frequency converter
 * that comes together with RF5111
 * TODO: Clean up
 */
struct ath5k_athchan_2ghz {
	u32	a2_flags;
	u16	a2_athchan;
};

/**
 * enum ath5k_dmasize -  DMA size definitions (2^(n+2))
 * @AR5K_DMASIZE_4B: 4Bytes
 * @AR5K_DMASIZE_8B: 8Bytes
 * @AR5K_DMASIZE_16B: 16Bytes
 * @AR5K_DMASIZE_32B: 32Bytes
 * @AR5K_DMASIZE_64B: 64Bytes (Default)
 * @AR5K_DMASIZE_128B: 128Bytes
 * @AR5K_DMASIZE_256B: 256Bytes
 * @AR5K_DMASIZE_512B: 512Bytes
 *
 * These are used to set DMA burst size on hw
 *
 * Note: Some platforms can't handle more than 4Bytes
 * be careful on embedded boards.
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



/******************\
  RATE DEFINITIONS
\******************/

/**
 * DOC: Rate codes
 *
 * Seems the ar5xxx hardware supports up to 32 rates, indexed by 1-32.
 *
 * The rate code is used to get the RX rate or set the TX rate on the
 * hardware descriptors. It is also used for internal modulation control
 * and settings.
 *
 * This is the hardware rate map we are aware of (html unfriendly):
 *
 * Rate code	Rate (Kbps)
 * ---------	-----------
 * 0x01		 3000 (XR)
 * 0x02		 1000 (XR)
 * 0x03		  250 (XR)
 * 0x04 - 05	-Reserved-
 * 0x06		 2000 (XR)
 * 0x07		  500 (XR)
 * 0x08		48000 (OFDM)
 * 0x09		24000 (OFDM)
 * 0x0A		12000 (OFDM)
 * 0x0B		 6000 (OFDM)
 * 0x0C		54000 (OFDM)
 * 0x0D		36000 (OFDM)
 * 0x0E		18000 (OFDM)
 * 0x0F		 9000 (OFDM)
 * 0x10 - 17	-Reserved-
 * 0x18		11000L (CCK)
 * 0x19		 5500L (CCK)
 * 0x1A		 2000L (CCK)
 * 0x1B		 1000L (CCK)
 * 0x1C		11000S (CCK)
 * 0x1D		 5500S (CCK)
 * 0x1E		 2000S (CCK)
 * 0x1F		-Reserved-
 *
 * "S" indicates CCK rates with short preamble and "L" with long preamble.
 *
 * AR5211 has different rate codes for CCK (802.11B) rates. It only uses the
 * lowest 4 bits, so they are the same as above with a 0xF mask.
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

/* Adding this flag to rate_code on B rates
 * enables short preamble */
#define AR5K_SET_SHORT_PREAMBLE 0x04

/*
 * Crypto definitions
 */

#define AR5K_KEYCACHE_SIZE	8
extern bool ath5k_modparam_nohwcrypt;

/***********************\
 HW RELATED DEFINITIONS
\***********************/

/*
 * Misc definitions
 */
#define	AR5K_RSSI_EP_MULTIPLIER	(1 << 7)

#define AR5K_ASSERT_ENTRY(_e, _s) do {		\
	if (_e >= _s)				\
		return false;			\
} while (0)

/*
 * Hardware interrupt abstraction
 */

/**
 * enum ath5k_int - Hardware interrupt masks helpers
 * @AR5K_INT_RXOK: Frame successfully received
 * @AR5K_INT_RXDESC: Request RX descriptor/Read RX descriptor
 * @AR5K_INT_RXERR: Frame reception failed
 * @AR5K_INT_RXNOFRM: No frame received within a specified time period
 * @AR5K_INT_RXEOL: Reached "End Of List", means we need more RX descriptors
 * @AR5K_INT_RXORN: Indicates we got RX FIFO overrun. Note that Rx overrun is
 *		not always fatal, on some chips we can continue operation
 *		without resetting the card, that's why %AR5K_INT_FATAL is not
 *		common for all chips.
 * @AR5K_INT_RX_ALL: Mask to identify all RX related interrupts
 *
 * @AR5K_INT_TXOK: Frame transmission success
 * @AR5K_INT_TXDESC: Request TX descriptor/Read TX status descriptor
 * @AR5K_INT_TXERR: Frame transmission failure
 * @AR5K_INT_TXEOL: Received End Of List for VEOL (Virtual End Of List). The
 *		Queue Control Unit (QCU) signals an EOL interrupt only if a
 *		descriptor's LinkPtr is NULL. For more details, refer to:
 *		"http://www.freepatentsonline.com/20030225739.html"
 * @AR5K_INT_TXNOFRM: No frame was transmitted within a specified time period
 * @AR5K_INT_TXURN: Indicates we got TX FIFO underrun. In such case we should
 *		increase the TX trigger threshold.
 * @AR5K_INT_TX_ALL: Mask to identify all TX related interrupts
 *
 * @AR5K_INT_MIB: Indicates the either Management Information Base counters or
 *		one of the PHY error counters reached the maximum value and
 *		should be read and cleared.
 * @AR5K_INT_SWI: Software triggered interrupt.
 * @AR5K_INT_RXPHY: RX PHY Error
 * @AR5K_INT_RXKCM: RX Key cache miss
 * @AR5K_INT_SWBA: SoftWare Beacon Alert - indicates its time to send a
 *		beacon that must be handled in software. The alternative is if
 *		you have VEOL support, in that case you let the hardware deal
 *		with things.
 * @AR5K_INT_BRSSI: Beacon received with an RSSI value below our threshold
 * @AR5K_INT_BMISS: If in STA mode this indicates we have stopped seeing
 *		beacons from the AP have associated with, we should probably
 *		try to reassociate. When in IBSS mode this might mean we have
 *		not received any beacons from any local stations. Note that
 *		every station in an IBSS schedules to send beacons at the
 *		Target Beacon Transmission Time (TBTT) with a random backoff.
 * @AR5K_INT_BNR: Beacon queue got triggered (DMA beacon alert) while empty.
 * @AR5K_INT_TIM: Beacon with local station's TIM bit set
 * @AR5K_INT_DTIM: Beacon with DTIM bit and zero DTIM count received
 * @AR5K_INT_DTIM_SYNC: DTIM sync lost
 * @AR5K_INT_GPIO: GPIO interrupt is used for RF Kill switches connected to
 *		our GPIO pins.
 * @AR5K_INT_BCN_TIMEOUT: Beacon timeout, we waited after TBTT but got noting
 * @AR5K_INT_CAB_TIMEOUT: We waited for CAB traffic after the beacon but got
 *		nothing or an incomplete CAB frame sequence.
 * @AR5K_INT_QCBRORN: A queue got it's CBR counter expired
 * @AR5K_INT_QCBRURN: A queue got triggered wile empty
 * @AR5K_INT_QTRIG: A queue got triggered
 *
 * @AR5K_INT_FATAL: Fatal errors were encountered, typically caused by bus/DMA
 *		errors. Indicates we need to reset the card.
 * @AR5K_INT_GLOBAL: Used to clear and set the IER
 * @AR5K_INT_NOCARD: Signals the card has been removed
 * @AR5K_INT_COMMON: Common interrupts shared among MACs with the same
 *		bit value
 *
 * These are mapped to take advantage of some common bits
 * between the MACs, to be able to set intr properties
 * easier. Some of them are not used yet inside hw.c. Most map
 * to the respective hw interrupt value as they are common among different
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
	AR5K_INT_QCBRORN =	0x08000000, /* Non common */
	AR5K_INT_QCBRURN =	0x10000000, /* Non common */
	AR5K_INT_QTRIG	=	0x20000000, /* Non common */
	AR5K_INT_GLOBAL =	0x80000000,

	AR5K_INT_TX_ALL = AR5K_INT_TXOK
		| AR5K_INT_TXDESC
		| AR5K_INT_TXERR
		| AR5K_INT_TXNOFRM
		| AR5K_INT_TXEOL
		| AR5K_INT_TXURN,

	AR5K_INT_RX_ALL = AR5K_INT_RXOK
		| AR5K_INT_RXDESC
		| AR5K_INT_RXERR
		| AR5K_INT_RXNOFRM
		| AR5K_INT_RXEOL
		| AR5K_INT_RXORN,

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

/**
 * enum ath5k_calibration_mask - Mask which calibration is active at the moment
 * @AR5K_CALIBRATION_FULL: Full calibration (AGC + SHORT)
 * @AR5K_CALIBRATION_SHORT: Short calibration (NF + I/Q)
 * @AR5K_CALIBRATION_NF: Noise Floor calibration
 * @AR5K_CALIBRATION_ANI: Adaptive Noise Immunity
 */
enum ath5k_calibration_mask {
	AR5K_CALIBRATION_FULL = 0x01,
	AR5K_CALIBRATION_SHORT = 0x02,
	AR5K_CALIBRATION_NF = 0x04,
	AR5K_CALIBRATION_ANI = 0x08,
};

/**
 * enum ath5k_power_mode - Power management modes
 * @AR5K_PM_UNDEFINED: Undefined
 * @AR5K_PM_AUTO: Allow card to sleep if possible
 * @AR5K_PM_AWAKE: Force card to wake up
 * @AR5K_PM_FULL_SLEEP: Force card to full sleep (DANGEROUS)
 * @AR5K_PM_NETWORK_SLEEP: Allow to sleep for a specified duration
 *
 * Currently only PM_AWAKE is used, FULL_SLEEP and NETWORK_SLEEP/AUTO
 * are also known to have problems on some cards. This is not a big
 * problem though because we can have almost the same effect as
 * FULL_SLEEP by putting card on warm reset (it's almost powered down).
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


/* XXX: we *may* move cap_range stuff to struct wiphy */
struct ath5k_capabilities {
	/*
	 * Supported PHY modes
	 * (ie. AR5K_MODE_11A, AR5K_MODE_11B, ...)
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

	bool cap_has_phyerr_counters;
	bool cap_has_mrr_support;
	bool cap_needs_2GHz_ovr;
};

/* size of noise floor history (keep it a power of two) */
#define ATH5K_NF_CAL_HIST_MAX	8
struct ath5k_nfcal_hist {
	s16 index;				/* current index into nfval */
	s16 nfval[ATH5K_NF_CAL_HIST_MAX];	/* last few noise floors */
};

#define ATH5K_LED_MAX_NAME_LEN 31

/*
 * State for LED triggers
 */
struct ath5k_led {
	char name[ATH5K_LED_MAX_NAME_LEN + 1];	/* name of the LED in sysfs */
	struct ath5k_hw *ah;			/* driver state */
	struct led_classdev led_dev;		/* led classdev */
};

/* Rfkill */
struct ath5k_rfkill {
	/* GPIO PIN for rfkill */
	u16 gpio;
	/* polarity of rfkill GPIO PIN */
	bool polarity;
	/* RFKILL toggle tasklet */
	struct tasklet_struct toggleq;
};

/* statistics */
struct ath5k_statistics {
	/* antenna use */
	unsigned int antenna_rx[5];	/* frames count per antenna RX */
	unsigned int antenna_tx[5];	/* frames count per antenna TX */

	/* frame errors */
	unsigned int rx_all_count;	/* all RX frames, including errors */
	unsigned int tx_all_count;	/* all TX frames, including errors */
	unsigned int rx_bytes_count;	/* all RX bytes, including errored pkts
					 * and the MAC headers for each packet
					 */
	unsigned int tx_bytes_count;	/* all TX bytes, including errored pkts
					 * and the MAC headers and padding for
					 * each packet.
					 */
	unsigned int rxerr_crc;
	unsigned int rxerr_phy;
	unsigned int rxerr_phy_code[32];
	unsigned int rxerr_fifo;
	unsigned int rxerr_decrypt;
	unsigned int rxerr_mic;
	unsigned int rxerr_proc;
	unsigned int rxerr_jumbo;
	unsigned int txerr_retry;
	unsigned int txerr_fifo;
	unsigned int txerr_filt;

	/* MIB counters */
	unsigned int ack_fail;
	unsigned int rts_fail;
	unsigned int rts_ok;
	unsigned int fcs_error;
	unsigned int beacons;

	unsigned int mib_intr;
	unsigned int rxorn_intr;
	unsigned int rxeol_intr;
};

/*
 * Misc defines
 */

#define AR5K_MAX_GPIO		10
#define AR5K_MAX_RF_BANKS	8

#if CHAN_DEBUG
#define ATH_CHAN_MAX	(26 + 26 + 26 + 200 + 200)
#else
#define ATH_CHAN_MAX	(14 + 14 + 14 + 252 + 20)
#endif

#define	ATH_RXBUF	40		/* number of RX buffers */
#define	ATH_TXBUF	200		/* number of TX buffers */
#define ATH_BCBUF	4		/* number of beacon buffers */
#define ATH5K_TXQ_LEN_MAX	(ATH_TXBUF / 4)		/* bufs per queue */
#define ATH5K_TXQ_LEN_LOW	(ATH5K_TXQ_LEN_MAX / 2)	/* low mark */

/* Driver state associated with an instance of a device */
struct ath5k_hw {
	struct ath_common       common;

	struct pci_dev		*pdev;
	struct device		*dev;		/* for dma mapping */
	int irq;
	u16 devid;
	void __iomem		*iobase;	/* address of the device */
	struct mutex		lock;		/* dev-level lock */
	struct ieee80211_hw	*hw;		/* IEEE 802.11 common */
	struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];
	struct ieee80211_channel channels[ATH_CHAN_MAX];
	struct ieee80211_rate	rates[IEEE80211_NUM_BANDS][AR5K_MAX_RATES];
	s8			rate_idx[IEEE80211_NUM_BANDS][AR5K_MAX_RATES];
	enum nl80211_iftype	opmode;

#ifdef CONFIG_ATH5K_DEBUG
	struct ath5k_dbg_info	debug;		/* debug info */
#endif /* CONFIG_ATH5K_DEBUG */

	struct ath5k_buf	*bufptr;	/* allocated buffer ptr */
	struct ath5k_desc	*desc;		/* TX/RX descriptors */
	dma_addr_t		desc_daddr;	/* DMA (physical) address */
	size_t			desc_len;	/* size of TX/RX descriptors */

	DECLARE_BITMAP(status, 4);
#define ATH_STAT_INVALID	0		/* disable hardware accesses */
#define ATH_STAT_PROMISC	1
#define ATH_STAT_LEDSOFT	2		/* enable LED gpio status */
#define ATH_STAT_STARTED	3		/* opened & irqs enabled */

	unsigned int		filter_flags;	/* HW flags, AR5K_RX_FILTER_* */
	struct ieee80211_channel *curchan;	/* current h/w channel */

	u16			nvifs;

	enum ath5k_int		imask;		/* interrupt mask copy */

	spinlock_t		irqlock;
	bool			rx_pending;	/* rx tasklet pending */
	bool			tx_pending;	/* tx tasklet pending */

	u8			bssidmask[ETH_ALEN];

	unsigned int		led_pin,	/* GPIO pin for driving LED */
				led_on;		/* pin setting for LED on */

	struct work_struct	reset_work;	/* deferred chip reset */
	struct work_struct	calib_work;	/* deferred phy calibration */

	struct list_head	rxbuf;		/* receive buffer */
	spinlock_t		rxbuflock;
	u32			*rxlink;	/* link ptr in last RX desc */
	struct tasklet_struct	rxtq;		/* rx intr tasklet */
	struct ath5k_led	rx_led;		/* rx led */

	struct list_head	txbuf;		/* transmit buffer */
	spinlock_t		txbuflock;
	unsigned int		txbuf_len;	/* buf count in txbuf list */
	struct ath5k_txq	txqs[AR5K_NUM_TX_QUEUES];	/* tx queues */
	struct tasklet_struct	txtq;		/* tx intr tasklet */
	struct ath5k_led	tx_led;		/* tx led */

	struct ath5k_rfkill	rf_kill;

	spinlock_t		block;		/* protects beacon */
	struct tasklet_struct	beacontq;	/* beacon intr tasklet */
	struct list_head	bcbuf;		/* beacon buffer */
	struct ieee80211_vif	*bslot[ATH_BCBUF];
	u16			num_ap_vifs;
	u16			num_adhoc_vifs;
	u16			num_mesh_vifs;
	unsigned int		bhalq,		/* SW q for outgoing beacons */
				bmisscount,	/* missed beacon transmits */
				bintval,	/* beacon interval in TU */
				bsent;
	unsigned int		nexttbtt;	/* next beacon time in TU */
	struct ath5k_txq	*cabq;		/* content after beacon */

	int			power_level;	/* Requested tx power in dBm */
	bool			assoc;		/* associate state */
	bool			enable_beacon;	/* true if beacons are on */

	struct ath5k_statistics	stats;

	struct ath5k_ani_state	ani_state;
	struct tasklet_struct	ani_tasklet;	/* ANI calibration */

	struct delayed_work	tx_complete_work;

	struct survey_info	survey;		/* collected survey info */

	enum ath5k_int		ah_imr;

	struct ieee80211_channel *ah_current_channel;
	bool			ah_iq_cal_needed;
	bool			ah_single_chip;

	enum ath5k_version	ah_version;
	enum ath5k_radio	ah_radio;
	u32			ah_mac_srev;
	u16			ah_mac_version;
	u16			ah_phy_revision;
	u16			ah_radio_5ghz_revision;
	u16			ah_radio_2ghz_revision;

#define ah_modes		ah_capabilities.cap_mode
#define ah_ee_version		ah_capabilities.cap_eeprom.ee_version

	u8			ah_retry_long;
	u8			ah_retry_short;

	u32			ah_use_32khz_clock;

	u8			ah_coverage_class;
	bool			ah_ack_bitrate_high;
	u8			ah_bwmode;
	bool			ah_short_slot;

	/* Antenna Control */
	u32			ah_ant_ctl[AR5K_EEPROM_N_MODES][AR5K_ANT_MAX];
	u8			ah_ant_mode;
	u8			ah_tx_ant;
	u8			ah_def_ant;

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

	u32			ah_txq_isr_txok_all;
	u32			ah_txq_isr_txurn;
	u32			ah_txq_isr_qcborn;
	u32			ah_txq_isr_qcburn;
	u32			ah_txq_isr_qtrig;

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
		s16		txp_cur_pwr;
		/* Values in 0.5dB units */
		s16		txp_offset;
		s16		txp_ofdm;
		s16		txp_cck_ofdm_gainf_delta;
		/* Value in dB units */
		s16		txp_cck_ofdm_pwr_delta;
		bool		txp_setup;
	} ah_txpower;

	struct ath5k_nfcal_hist ah_nfcal_hist;

	/* average beacon RSSI in our BSS (used by ANI) */
	struct ewma		ah_beacon_rssi_avg;

	/* noise floor from last periodic calibration */
	s32			ah_noise_floor;

	/* Calibration timestamp */
	unsigned long		ah_cal_next_full;
	unsigned long		ah_cal_next_short;
	unsigned long		ah_cal_next_ani;

	/* Calibration mask */
	u8			ah_cal_mask;

	/*
	 * Function pointers
	 */
	int (*ah_setup_tx_desc)(struct ath5k_hw *, struct ath5k_desc *,
		unsigned int, unsigned int, int, enum ath5k_pkt_type,
		unsigned int, unsigned int, unsigned int, unsigned int,
		unsigned int, unsigned int, unsigned int, unsigned int);
	int (*ah_proc_tx_desc)(struct ath5k_hw *, struct ath5k_desc *,
		struct ath5k_tx_status *);
	int (*ah_proc_rx_desc)(struct ath5k_hw *, struct ath5k_desc *,
		struct ath5k_rx_status *);
};

struct ath_bus_ops {
	enum ath_bus_type ath_bus_type;
	void (*read_cachesize)(struct ath_common *common, int *csz);
	bool (*eeprom_read)(struct ath_common *common, u32 off, u16 *data);
	int (*eeprom_read_mac)(struct ath5k_hw *ah, u8 *mac);
};

/*
 * Prototypes
 */
extern const struct ieee80211_ops ath5k_hw_ops;

/* Initialization and detach functions */
int ath5k_hw_init(struct ath5k_hw *ah);
void ath5k_hw_deinit(struct ath5k_hw *ah);

int ath5k_sysfs_register(struct ath5k_hw *ah);
void ath5k_sysfs_unregister(struct ath5k_hw *ah);

/*Chip id helper functions */
int ath5k_hw_read_srev(struct ath5k_hw *ah);

/* LED functions */
int ath5k_init_leds(struct ath5k_hw *ah);
void ath5k_led_enable(struct ath5k_hw *ah);
void ath5k_led_off(struct ath5k_hw *ah);
void ath5k_unregister_leds(struct ath5k_hw *ah);


/* Reset Functions */
int ath5k_hw_nic_wakeup(struct ath5k_hw *ah, struct ieee80211_channel *channel);
int ath5k_hw_on_hold(struct ath5k_hw *ah);
int ath5k_hw_reset(struct ath5k_hw *ah, enum nl80211_iftype op_mode,
	   struct ieee80211_channel *channel, bool fast, bool skip_pcu);
int ath5k_hw_register_timeout(struct ath5k_hw *ah, u32 reg, u32 flag, u32 val,
			      bool is_set);
/* Power management functions */


/* Clock rate related functions */
unsigned int ath5k_hw_htoclock(struct ath5k_hw *ah, unsigned int usec);
unsigned int ath5k_hw_clocktoh(struct ath5k_hw *ah, unsigned int clock);
void ath5k_hw_set_clockrate(struct ath5k_hw *ah);


/* DMA Related Functions */
void ath5k_hw_start_rx_dma(struct ath5k_hw *ah);
u32 ath5k_hw_get_rxdp(struct ath5k_hw *ah);
int ath5k_hw_set_rxdp(struct ath5k_hw *ah, u32 phys_addr);
int ath5k_hw_start_tx_dma(struct ath5k_hw *ah, unsigned int queue);
int ath5k_hw_stop_beacon_queue(struct ath5k_hw *ah, unsigned int queue);
u32 ath5k_hw_get_txdp(struct ath5k_hw *ah, unsigned int queue);
int ath5k_hw_set_txdp(struct ath5k_hw *ah, unsigned int queue,
				u32 phys_addr);
int ath5k_hw_update_tx_triglevel(struct ath5k_hw *ah, bool increase);
/* Interrupt handling */
bool ath5k_hw_is_intr_pending(struct ath5k_hw *ah);
int ath5k_hw_get_isr(struct ath5k_hw *ah, enum ath5k_int *interrupt_mask);
enum ath5k_int ath5k_hw_set_imr(struct ath5k_hw *ah, enum ath5k_int new_mask);
void ath5k_hw_update_mib_counters(struct ath5k_hw *ah);
/* Init/Stop functions */
void ath5k_hw_dma_init(struct ath5k_hw *ah);
int ath5k_hw_dma_stop(struct ath5k_hw *ah);

/* EEPROM access functions */
int ath5k_eeprom_init(struct ath5k_hw *ah);
void ath5k_eeprom_detach(struct ath5k_hw *ah);


/* Protocol Control Unit Functions */
/* Helpers */
int ath5k_hw_get_frame_duration(struct ath5k_hw *ah, enum ieee80211_band band,
		int len, struct ieee80211_rate *rate, bool shortpre);
unsigned int ath5k_hw_get_default_slottime(struct ath5k_hw *ah);
unsigned int ath5k_hw_get_default_sifs(struct ath5k_hw *ah);
int ath5k_hw_set_opmode(struct ath5k_hw *ah, enum nl80211_iftype opmode);
void ath5k_hw_set_coverage_class(struct ath5k_hw *ah, u8 coverage_class);
/* RX filter control*/
int ath5k_hw_set_lladdr(struct ath5k_hw *ah, const u8 *mac);
void ath5k_hw_set_bssid(struct ath5k_hw *ah);
void ath5k_hw_set_bssid_mask(struct ath5k_hw *ah, const u8 *mask);
void ath5k_hw_set_mcast_filter(struct ath5k_hw *ah, u32 filter0, u32 filter1);
u32 ath5k_hw_get_rx_filter(struct ath5k_hw *ah);
void ath5k_hw_set_rx_filter(struct ath5k_hw *ah, u32 filter);
/* Receive (DRU) start/stop functions */
void ath5k_hw_start_rx_pcu(struct ath5k_hw *ah);
void ath5k_hw_stop_rx_pcu(struct ath5k_hw *ah);
/* Beacon control functions */
u64 ath5k_hw_get_tsf64(struct ath5k_hw *ah);
void ath5k_hw_set_tsf64(struct ath5k_hw *ah, u64 tsf64);
void ath5k_hw_reset_tsf(struct ath5k_hw *ah);
void ath5k_hw_init_beacon_timers(struct ath5k_hw *ah, u32 next_beacon,
							u32 interval);
bool ath5k_hw_check_beacon_timers(struct ath5k_hw *ah, int intval);
/* Init function */
void ath5k_hw_pcu_init(struct ath5k_hw *ah, enum nl80211_iftype op_mode);

/* Queue Control Unit, DFS Control Unit Functions */
int ath5k_hw_get_tx_queueprops(struct ath5k_hw *ah, int queue,
			       struct ath5k_txq_info *queue_info);
int ath5k_hw_set_tx_queueprops(struct ath5k_hw *ah, int queue,
			       const struct ath5k_txq_info *queue_info);
int ath5k_hw_setup_tx_queue(struct ath5k_hw *ah,
			    enum ath5k_tx_queue queue_type,
			    struct ath5k_txq_info *queue_info);
void ath5k_hw_set_tx_retry_limits(struct ath5k_hw *ah,
				  unsigned int queue);
u32 ath5k_hw_num_tx_pending(struct ath5k_hw *ah, unsigned int queue);
void ath5k_hw_release_tx_queue(struct ath5k_hw *ah, unsigned int queue);
int ath5k_hw_reset_tx_queue(struct ath5k_hw *ah, unsigned int queue);
int ath5k_hw_set_ifs_intervals(struct ath5k_hw *ah, unsigned int slot_time);
/* Init function */
int ath5k_hw_init_queues(struct ath5k_hw *ah);

/* Hardware Descriptor Functions */
int ath5k_hw_init_desc_functions(struct ath5k_hw *ah);
int ath5k_hw_setup_rx_desc(struct ath5k_hw *ah, struct ath5k_desc *desc,
			   u32 size, unsigned int flags);
int ath5k_hw_setup_mrr_tx_desc(struct ath5k_hw *ah, struct ath5k_desc *desc,
	unsigned int tx_rate1, u_int tx_tries1, u_int tx_rate2,
	u_int tx_tries2, unsigned int tx_rate3, u_int tx_tries3);


/* GPIO Functions */
void ath5k_hw_set_ledstate(struct ath5k_hw *ah, unsigned int state);
int ath5k_hw_set_gpio_input(struct ath5k_hw *ah, u32 gpio);
int ath5k_hw_set_gpio_output(struct ath5k_hw *ah, u32 gpio);
u32 ath5k_hw_get_gpio(struct ath5k_hw *ah, u32 gpio);
int ath5k_hw_set_gpio(struct ath5k_hw *ah, u32 gpio, u32 val);
void ath5k_hw_set_gpio_intr(struct ath5k_hw *ah, unsigned int gpio,
			    u32 interrupt_level);


/* RFkill Functions */
void ath5k_rfkill_hw_start(struct ath5k_hw *ah);
void ath5k_rfkill_hw_stop(struct ath5k_hw *ah);


/* Misc functions TODO: Cleanup */
int ath5k_hw_set_capabilities(struct ath5k_hw *ah);
int ath5k_hw_enable_pspoll(struct ath5k_hw *ah, u8 *bssid, u16 assoc_id);
int ath5k_hw_disable_pspoll(struct ath5k_hw *ah);


/* Initial register settings functions */
int ath5k_hw_write_initvals(struct ath5k_hw *ah, u8 mode, bool change_channel);


/* PHY functions */
/* Misc PHY functions */
u16 ath5k_hw_radio_revision(struct ath5k_hw *ah, enum ieee80211_band band);
int ath5k_hw_phy_disable(struct ath5k_hw *ah);
/* Gain_F optimization */
enum ath5k_rfgain ath5k_hw_gainf_calibrate(struct ath5k_hw *ah);
int ath5k_hw_rfgain_opt_init(struct ath5k_hw *ah);
/* PHY/RF channel functions */
bool ath5k_channel_ok(struct ath5k_hw *ah, struct ieee80211_channel *channel);
/* PHY calibration */
void ath5k_hw_init_nfcal_hist(struct ath5k_hw *ah);
int ath5k_hw_phy_calibrate(struct ath5k_hw *ah,
			   struct ieee80211_channel *channel);
void ath5k_hw_update_noise_floor(struct ath5k_hw *ah);
/* Spur mitigation */
bool ath5k_hw_chan_has_spur_noise(struct ath5k_hw *ah,
				  struct ieee80211_channel *channel);
/* Antenna control */
void ath5k_hw_set_antenna_mode(struct ath5k_hw *ah, u8 ant_mode);
void ath5k_hw_set_antenna_switch(struct ath5k_hw *ah, u8 ee_mode);
/* TX power setup */
int ath5k_hw_set_txpower_limit(struct ath5k_hw *ah, u8 txpower);
/* Init function */
int ath5k_hw_phy_init(struct ath5k_hw *ah, struct ieee80211_channel *channel,
				u8 mode, bool fast);

/*
 * Functions used internally
 */

static inline struct ath_common *ath5k_hw_common(struct ath5k_hw *ah)
{
	return &ah->common;
}

static inline struct ath_regulatory *ath5k_hw_regulatory(struct ath5k_hw *ah)
{
	return &(ath5k_hw_common(ah)->regulatory);
}

#ifdef CONFIG_ATHEROS_AR231X
#define AR5K_AR2315_PCI_BASE	((void __iomem *)0xb0100000)

static inline void __iomem *ath5k_ahb_reg(struct ath5k_hw *ah, u16 reg)
{
	/* On AR2315 and AR2317 the PCI clock domain registers
	 * are outside of the WMAC register space */
	if (unlikely((reg >= 0x4000) && (reg < 0x5000) &&
	    (ah->ah_mac_srev >= AR5K_SREV_AR2315_R6)))
		return AR5K_AR2315_PCI_BASE + reg;

	return ah->iobase + reg;
}

static inline u32 ath5k_hw_reg_read(struct ath5k_hw *ah, u16 reg)
{
	return ioread32(ath5k_ahb_reg(ah, reg));
}

static inline void ath5k_hw_reg_write(struct ath5k_hw *ah, u32 val, u16 reg)
{
	iowrite32(val, ath5k_ahb_reg(ah, reg));
}

#else

static inline u32 ath5k_hw_reg_read(struct ath5k_hw *ah, u16 reg)
{
	return ioread32(ah->iobase + reg);
}

static inline void ath5k_hw_reg_write(struct ath5k_hw *ah, u32 val, u16 reg)
{
	iowrite32(val, ah->iobase + reg);
}

#endif

static inline enum ath_bus_type ath5k_get_bus_type(struct ath5k_hw *ah)
{
	return ath5k_hw_common(ah)->bus_ops->ath_bus_type;
}

static inline void ath5k_read_cachesize(struct ath_common *common, int *csz)
{
	common->bus_ops->read_cachesize(common, csz);
}

static inline bool ath5k_hw_nvram_read(struct ath5k_hw *ah, u32 off, u16 *data)
{
	struct ath_common *common = ath5k_hw_common(ah);
	return common->bus_ops->eeprom_read(common, off, data);
}

static inline u32 ath5k_hw_bitswap(u32 val, unsigned int bits)
{
	u32 retval = 0, bit, i;

	for (i = 0; i < bits; i++) {
		bit = (val >> i) & 1;
		retval = (retval << 1) | bit;
	}

	return retval;
}

#endif
