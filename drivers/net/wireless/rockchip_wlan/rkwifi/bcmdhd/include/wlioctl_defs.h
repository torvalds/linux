/*
 * Custom OID/ioctl definitions for
 * Broadcom 802.11abg Networking Device Driver
 *
 * Definitions subject to change without notice.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: wlioctl_defs.h 677667 2017-01-04 07:43:05Z $
 */


#ifndef wlioctl_defs_h
#define wlioctl_defs_h




/* All builds use the new 11ac ratespec/chanspec */
#undef  D11AC_IOTYPES
#define D11AC_IOTYPES

#ifndef USE_NEW_RSPEC_DEFS
/* WL_RSPEC defines for rate information */
#define WL_RSPEC_RATE_MASK		0x000000FF      /* rate or HT MCS value */
#define WL_RSPEC_HE_MCS_MASK		0x0000000F      /* HE MCS value */
#define WL_RSPEC_HE_NSS_MASK		0x000000F0      /* HE Nss value */
#define WL_RSPEC_HE_NSS_SHIFT		4               /* HE Nss value shift */
#define WL_RSPEC_VHT_MCS_MASK		0x0000000F      /* VHT MCS value */
#define WL_RSPEC_VHT_NSS_MASK		0x000000F0      /* VHT Nss value */
#define WL_RSPEC_VHT_NSS_SHIFT		4               /* VHT Nss value shift */
#define WL_RSPEC_TXEXP_MASK		0x00000300
#define WL_RSPEC_TXEXP_SHIFT		8
#define WL_RSPEC_BW_MASK		0x00070000      /* bandwidth mask */
#define WL_RSPEC_BW_SHIFT		16              /* bandwidth shift */
#define WL_RSPEC_STBC			0x00100000      /* STBC encoding, Nsts = 2 x Nss */
#define WL_RSPEC_TXBF			0x00200000      /* bit indicates TXBF mode */
#define WL_RSPEC_LDPC			0x00400000      /* bit indicates adv coding in use */
#define WL_RSPEC_SGI			0x00800000      /* Short GI mode */
#define WL_RSPEC_ENCODING_MASK		0x03000000      /* Encoding of Rate/MCS field */
#define WL_RSPEC_OVERRIDE_RATE		0x40000000      /* bit indicate to override mcs only */
#define WL_RSPEC_OVERRIDE_MODE		0x80000000      /* bit indicates override rate & mode */

/* WL_RSPEC_ENCODING field defs */
#define WL_RSPEC_ENCODE_RATE	0x00000000      /* Legacy rate is stored in RSPEC_RATE_MASK */
#define WL_RSPEC_ENCODE_HT	0x01000000      /* HT MCS is stored in RSPEC_RATE_MASK */
#define WL_RSPEC_ENCODE_VHT	0x02000000      /* VHT MCS and Nss is stored in RSPEC_RATE_MASK */
#define WL_RSPEC_ENCODE_HE	0x03000000      /* HE MCS and Nss is stored in RSPEC_RATE_MASK */

/* WL_RSPEC_BW field defs */
#define WL_RSPEC_BW_UNSPECIFIED 0
#define WL_RSPEC_BW_20MHZ       0x00010000
#define WL_RSPEC_BW_40MHZ       0x00020000
#define WL_RSPEC_BW_80MHZ       0x00030000
#define WL_RSPEC_BW_160MHZ      0x00040000
#define WL_RSPEC_BW_10MHZ	0x00050000
#define WL_RSPEC_BW_5MHZ	0x00060000
#define WL_RSPEC_BW_2P5MHZ      0x00070000

#define HIGHEST_SINGLE_STREAM_MCS	7 /* MCS values greater than this enable multiple streams */

#endif /* !USE_NEW_RSPEC_DEFS */

/* Legacy defines for the nrate iovar */
#define OLD_NRATE_MCS_INUSE         0x00000080 /* MSC in use,indicates b0-6 holds an mcs */
#define OLD_NRATE_RATE_MASK         0x0000007f /* rate/mcs value */
#define OLD_NRATE_STF_MASK          0x0000ff00 /* stf mode mask: siso, cdd, stbc, sdm */
#define OLD_NRATE_STF_SHIFT         8          /* stf mode shift */
#define OLD_NRATE_OVERRIDE          0x80000000 /* bit indicates override both rate & mode */
#define OLD_NRATE_OVERRIDE_MCS_ONLY 0x40000000 /* bit indicate to override mcs only */
#define OLD_NRATE_SGI               0x00800000 /* sgi mode */
#define OLD_NRATE_LDPC_CODING       0x00400000 /* bit indicates adv coding in use */

#define OLD_NRATE_STF_SISO	0		/* stf mode SISO */
#define OLD_NRATE_STF_CDD	1		/* stf mode CDD */
#define OLD_NRATE_STF_STBC	2		/* stf mode STBC */
#define OLD_NRATE_STF_SDM	3		/* stf mode SDM */

#define WLC_11N_N_PROP_MCS	6
#define WLC_11N_FIRST_PROP_MCS	87
#define WLC_11N_LAST_PROP_MCS	102

#define MAX_CCA_CHANNELS 38	/* Max number of 20 Mhz wide channels */
#define MAX_CCA_SECS	60	/* CCA keeps this many seconds history */

#define IBSS_MED        15	/* Mediom in-bss congestion percentage */
#define IBSS_HI         25	/* Hi in-bss congestion percentage */
#define OBSS_MED        12
#define OBSS_HI         25
#define INTERFER_MED    5
#define INTERFER_HI     10

#define  CCA_FLAG_2G_ONLY		0x01	/* Return a channel from 2.4 Ghz band */
#define  CCA_FLAG_5G_ONLY		0x02	/* Return a channel from 2.4 Ghz band */
#define  CCA_FLAG_IGNORE_DURATION	0x04	/* Ignore dwell time for each channel */
#define  CCA_FLAGS_PREFER_1_6_11	0x10
#define  CCA_FLAG_IGNORE_INTERFER 	0x20 /* do not exlude channel based on interfer level */

#define CCA_ERRNO_BAND 		1	/* After filtering for band pref, no choices left */
#define CCA_ERRNO_DURATION	2	/* After filtering for duration, no choices left */
#define CCA_ERRNO_PREF_CHAN	3	/* After filtering for chan pref, no choices left */
#define CCA_ERRNO_INTERFER	4	/* After filtering for interference, no choices left */
#define CCA_ERRNO_TOO_FEW	5	/* Only 1 channel was input */

#define WL_STA_AID(a)		((a) &~ 0xc000)

/* Flags for sta_info_t indicating properties of STA */
#define WL_STA_BRCM		0x00000001	/* Running a Broadcom driver */
#define WL_STA_WME		0x00000002	/* WMM association */
#define WL_STA_NONERP		0x00000004	/* No ERP */
#define WL_STA_AUTHE		0x00000008	/* Authenticated */
#define WL_STA_ASSOC		0x00000010	/* Associated */
#define WL_STA_AUTHO		0x00000020	/* Authorized */
#define WL_STA_WDS		0x00000040	/* Wireless Distribution System */
#define WL_STA_WDS_LINKUP	0x00000080	/* WDS traffic/probes flowing properly */
#define WL_STA_PS		0x00000100	/* STA is in power save mode from AP's viewpoint */
#define WL_STA_APSD_BE		0x00000200	/* APSD delv/trigger for AC_BE is default enabled */
#define WL_STA_APSD_BK		0x00000400	/* APSD delv/trigger for AC_BK is default enabled */
#define WL_STA_APSD_VI		0x00000800	/* APSD delv/trigger for AC_VI is default enabled */
#define WL_STA_APSD_VO		0x00001000	/* APSD delv/trigger for AC_VO is default enabled */
#define WL_STA_N_CAP		0x00002000	/* STA 802.11n capable */
#define WL_STA_SCBSTATS		0x00004000	/* Per STA debug stats */
#define WL_STA_AMPDU_CAP	0x00008000	/* STA AMPDU capable */
#define WL_STA_AMSDU_CAP	0x00010000	/* STA AMSDU capable */
#define WL_STA_MIMO_PS		0x00020000	/* mimo ps mode is enabled */
#define WL_STA_MIMO_RTS		0x00040000	/* send rts in mimo ps mode */
#define WL_STA_RIFS_CAP		0x00080000	/* rifs enabled */
#define WL_STA_VHT_CAP		0x00100000	/* STA VHT(11ac) capable */
#define WL_STA_WPS		0x00200000	/* WPS state */
#define WL_STA_DWDS_CAP		0x01000000	/* DWDS CAP */
#define WL_STA_DWDS		0x02000000	/* DWDS active */
#define WL_WDS_LINKUP		WL_STA_WDS_LINKUP	/* deprecated */

/* STA HT cap fields */
#define WL_STA_CAP_LDPC_CODING		0x0001	/* Support for rx of LDPC coded pkts */
#define WL_STA_CAP_40MHZ		0x0002  /* FALSE:20Mhz, TRUE:20/40MHZ supported */
#define WL_STA_CAP_MIMO_PS_MASK		0x000C  /* Mimo PS mask */
#define WL_STA_CAP_MIMO_PS_SHIFT	0x0002	/* Mimo PS shift */
#define WL_STA_CAP_MIMO_PS_OFF		0x0003	/* Mimo PS, no restriction */
#define WL_STA_CAP_MIMO_PS_RTS		0x0001	/* Mimo PS, send RTS/CTS around MIMO frames */
#define WL_STA_CAP_MIMO_PS_ON		0x0000	/* Mimo PS, MIMO disallowed */
#define WL_STA_CAP_GF			0x0010	/* Greenfield preamble support */
#define WL_STA_CAP_SHORT_GI_20		0x0020	/* 20MHZ short guard interval support */
#define WL_STA_CAP_SHORT_GI_40		0x0040	/* 40Mhz short guard interval support */
#define WL_STA_CAP_TX_STBC		0x0080	/* Tx STBC support */
#define WL_STA_CAP_RX_STBC_MASK		0x0300	/* Rx STBC mask */
#define WL_STA_CAP_RX_STBC_SHIFT	8	/* Rx STBC shift */
#define WL_STA_CAP_DELAYED_BA		0x0400	/* delayed BA support */
#define WL_STA_CAP_MAX_AMSDU		0x0800	/* Max AMSDU size in bytes , 0=3839, 1=7935 */
#define WL_STA_CAP_DSSS_CCK		0x1000	/* DSSS/CCK supported by the BSS */
#define WL_STA_CAP_PSMP			0x2000	/* Power Save Multi Poll support */
#define WL_STA_CAP_40MHZ_INTOLERANT	0x4000	/* 40MHz Intolerant */
#define WL_STA_CAP_LSIG_TXOP		0x8000	/* L-SIG TXOP protection support */

#define WL_STA_CAP_RX_STBC_NO		0x0	/* no rx STBC support */
#define WL_STA_CAP_RX_STBC_ONE_STREAM	0x1	/* rx STBC support of 1 spatial stream */
#define WL_STA_CAP_RX_STBC_TWO_STREAM	0x2	/* rx STBC support of 1-2 spatial streams */
#define WL_STA_CAP_RX_STBC_THREE_STREAM	0x3	/* rx STBC support of 1-3 spatial streams */

/* scb vht flags */
#define WL_STA_VHT_LDPCCAP	0x0001
#define WL_STA_SGI80		0x0002
#define WL_STA_SGI160		0x0004
#define WL_STA_VHT_TX_STBCCAP	0x0008
#define WL_STA_VHT_RX_STBCCAP	0x0010
#define WL_STA_SU_BEAMFORMER	0x0020
#define WL_STA_SU_BEAMFORMEE	0x0040
#define WL_STA_MU_BEAMFORMER	0x0080
#define WL_STA_MU_BEAMFORMEE	0x0100
#define WL_STA_VHT_TXOP_PS	0x0200
#define WL_STA_HTC_VHT_CAP	0x0400

/* Values for TX Filter override mode */
#define WLC_TXFILTER_OVERRIDE_DISABLED  0
#define WLC_TXFILTER_OVERRIDE_ENABLED   1

#define WL_IOCTL_ACTION_GET		0x0
#define WL_IOCTL_ACTION_SET		0x1
#define WL_IOCTL_ACTION_OVL_IDX_MASK	0x1e
#define WL_IOCTL_ACTION_OVL_RSV		0x20
#define WL_IOCTL_ACTION_OVL		0x40
#define WL_IOCTL_ACTION_MASK		0x7e
#define WL_IOCTL_ACTION_OVL_SHIFT	1

/* For WLC_SET_INFRA ioctl & infra_configuration iovar SET/GET operations */
#define WL_BSSTYPE_INDEP 0
#define WL_BSSTYPE_INFRA 1
#define WL_BSSTYPE_ANY   2	/* deprecated */
#define WL_BSSTYPE_MESH  3
/* Bitmask for scan_type */
#define WL_SCANFLAGS_PASSIVE	0x01	/* force passive scan */
#define WL_SCANFLAGS_RESERVED	0x02	/* Reserved */
#define WL_SCANFLAGS_PROHIBITED	0x04	/* allow scanning prohibited channels */
#define WL_SCANFLAGS_OFFCHAN	0x08	/* allow scanning/reporting off-channel APs */
#define WL_SCANFLAGS_HOTSPOT	0x10	/* automatic ANQP to hotspot APs */
#define WL_SCANFLAGS_SWTCHAN	0x20	/* Force channel switch for differerent bandwidth */
#define WL_SCANFLAGS_FORCE_PARALLEL 0x40 /* Force parallel scan even when actcb_fn_t is on.
					  * by default parallel scan will be disabled if actcb_fn_t
					  * is provided.
					  */
#define WL_SCANFLAGS_SISO	0x40	/* Use 1 RX chain for scanning */
#define WL_SCANFLAGS_MIMO	0x80	/* Force MIMO scanning */

/* wl_iscan_results status values */
#define WL_SCAN_RESULTS_SUCCESS	0
#define WL_SCAN_RESULTS_PARTIAL	1
#define WL_SCAN_RESULTS_PENDING	2
#define WL_SCAN_RESULTS_ABORTED	3
#define WL_SCAN_RESULTS_NO_MEM  4

#define SCANOL_ENABLED			(1 << 0)
#define SCANOL_BCAST_SSID		(1 << 1)
#define SCANOL_NOTIFY_BCAST_SSID	(1 << 2)
#define SCANOL_RESULTS_PER_CYCLE	(1 << 3)

/* scan times in milliseconds */
#define SCANOL_HOME_TIME		45	/* for home channel processing */
#define SCANOL_ASSOC_TIME		20	/* dwell on a channel while associated */
#define SCANOL_UNASSOC_TIME		40	/* dwell on a channel while unassociated */
#define SCANOL_PASSIVE_TIME		110	/* listen on a channelfor passive scan */
#define SCANOL_AWAY_LIMIT		100	/* max time to be away from home channel */
#define SCANOL_IDLE_REST_TIME		40
#define SCANOL_IDLE_REST_MULTIPLIER	0
#define SCANOL_ACTIVE_REST_TIME		20
#define SCANOL_ACTIVE_REST_MULTIPLIER	0
#define SCANOL_CYCLE_IDLE_REST_TIME	300000	/* Idle Rest Time between Scan Cycle (msec) */
#define SCANOL_CYCLE_IDLE_REST_MULTIPLIER	0	/* Idle Rest Time Multiplier */
#define SCANOL_CYCLE_ACTIVE_REST_TIME	200
#define SCANOL_CYCLE_ACTIVE_REST_MULTIPLIER	0
#define SCANOL_MAX_REST_TIME		3600000	/* max rest time between scan cycle (msec) */
#define SCANOL_CYCLE_DEFAULT		0	/* default for Max Scan Cycle, 0 = forever */
#define SCANOL_CYCLE_MAX		864000	/* Max Scan Cycle */
						/* 10 sec/scan cycle => 100 days */
#define SCANOL_NPROBES			2	/* for Active scan; send n probes on each channel */
#define SCANOL_NPROBES_MAX		5	/* for Active scan; send n probes on each channel */
#define SCANOL_SCAN_START_DLY		10	/* delay start of offload scan (sec) */
#define SCANOL_SCAN_START_DLY_MAX	240	/* delay start of offload scan (sec) */
#define SCANOL_MULTIPLIER_MAX		10	/* Max Multiplier */
#define SCANOL_UNASSOC_TIME_MAX		100	/* max dwell on a channel while unassociated */
#define SCANOL_PASSIVE_TIME_MAX		500	/* max listen on a channel for passive scan */
#define SCANOL_SSID_MAX			16	/* max supported preferred SSID */

/* masks for channel and ssid count */
#define WL_SCAN_PARAMS_COUNT_MASK 0x0000ffff
#define WL_SCAN_PARAMS_NSSID_SHIFT 16

#define WL_SCAN_ACTION_START      1
#define WL_SCAN_ACTION_CONTINUE   2
#define WL_SCAN_ACTION_ABORT      3

#define ANTENNA_NUM_1	1		/* total number of antennas to be used */
#define ANTENNA_NUM_2	2
#define ANTENNA_NUM_3	3
#define ANTENNA_NUM_4	4

#define ANT_SELCFG_AUTO		0x80	/* bit indicates antenna sel AUTO */
#define ANT_SELCFG_MASK		0x33	/* antenna configuration mask */
#define ANT_SELCFG_TX_UNICAST	0	/* unicast tx antenna configuration */
#define ANT_SELCFG_RX_UNICAST	1	/* unicast rx antenna configuration */
#define ANT_SELCFG_TX_DEF	2	/* default tx antenna configuration */
#define ANT_SELCFG_RX_DEF	3	/* default rx antenna configuration */

/* interference source detection and identification mode */
#define ITFR_MODE_DISABLE	0	/* disable feature */
#define ITFR_MODE_MANUAL_ENABLE	1	/* enable manual detection */
#define ITFR_MODE_AUTO_ENABLE	2	/* enable auto detection */

/* bit definitions for flags in interference source report */
#define ITFR_INTERFERENCED	1	/* interference detected */
#define ITFR_HOME_CHANNEL	2	/* home channel has interference */
#define ITFR_NOISY_ENVIRONMENT	4	/* noisy environemnt so feature stopped */

#define WL_NUM_RPI_BINS		8
#define WL_RM_TYPE_BASIC	1
#define WL_RM_TYPE_CCA		2
#define WL_RM_TYPE_RPI		3
#define WL_RM_TYPE_ABORT	-1	/* ABORT any in-progress RM request */

#define WL_RM_FLAG_PARALLEL	(1<<0)

#define WL_RM_FLAG_LATE		(1<<1)
#define WL_RM_FLAG_INCAPABLE	(1<<2)
#define WL_RM_FLAG_REFUSED	(1<<3)

/* flags */
#define WLC_ASSOC_REQ_IS_REASSOC 0x01 /* assoc req was actually a reassoc */

#define WLC_CIS_DEFAULT	0	/* built-in default */
#define WLC_CIS_SROM	1	/* source is sprom */
#define WLC_CIS_OTP	2	/* source is otp */

/* PCL - Power Control Loop */
/* current gain setting is replaced by user input */
#define WL_ATTEN_APP_INPUT_PCL_OFF	0	/* turn off PCL, apply supplied input */
#define WL_ATTEN_PCL_ON			1	/* turn on PCL */
/* current gain setting is maintained */
#define WL_ATTEN_PCL_OFF		2	/* turn off PCL. */

/* defines used by poweridx iovar - it controls power in a-band */
/* current gain setting is maintained */
#define WL_PWRIDX_PCL_OFF	-2	/* turn off PCL.  */
#define WL_PWRIDX_PCL_ON	-1	/* turn on PCL */
#define WL_PWRIDX_LOWER_LIMIT	-2	/* lower limit */
#define WL_PWRIDX_UPPER_LIMIT	63	/* upper limit */
/* value >= 0 causes
 *	- input to be set to that value
 *	- PCL to be off
 */

#define BCM_MAC_STATUS_INDICATION	(0x40010200L)

/* Values for TX Filter override mode */
#define WLC_TXFILTER_OVERRIDE_DISABLED  0
#define WLC_TXFILTER_OVERRIDE_ENABLED   1

/* magic pattern used for mismatch driver and wl */
#define WL_TXFIFO_SZ_MAGIC	0xa5a5

/* check this magic number */
#define WLC_IOCTL_MAGIC		0x14e46c77


/* bss_info_cap_t flags */
#define WL_BSS_FLAGS_FROM_BEACON	0x01	/* bss_info derived from beacon */
#define WL_BSS_FLAGS_FROM_CACHE		0x02	/* bss_info collected from cache */
#define WL_BSS_FLAGS_RSSI_ONCHANNEL	0x04	/* rssi info received on channel (vs offchannel) */
#define WL_BSS_FLAGS_HS20		0x08	/* hotspot 2.0 capable */
#define WL_BSS_FLAGS_RSSI_INVALID	0x10	/* BSS contains invalid RSSI */
#define WL_BSS_FLAGS_RSSI_INACCURATE	0x20	/* BSS contains inaccurate RSSI */
#define WL_BSS_FLAGS_SNR_INVALID	0x40	/* BSS contains invalid SNR */
#define WL_BSS_FLAGS_NF_INVALID		0x80	/* BSS contains invalid noise floor */

/* bit definitions for bcnflags in wl_bss_info */
#define WL_BSS_BCNFLAGS_INTERWORK_PRESENT	0x01 /* beacon had IE, accessnet valid */
#define WL_BSS_BCNFLAGS_INTERWORK_PRESENT_VALID 0x02 /* on indicates support for this API */

/* bssinfo flag for nbss_cap */
#define VHT_BI_SGI_80MHZ			0x00000100
#define VHT_BI_80MHZ			    0x00000200
#define VHT_BI_160MHZ			    0x00000400
#define VHT_BI_8080MHZ			    0x00000800

/* reference to wl_ioctl_t struct used by usermode driver */
#define ioctl_subtype	set		/* subtype param */
#define ioctl_pid	used		/* pid param */
#define ioctl_status	needed		/* status param */


/* Enumerate crypto algorithms */
#define	CRYPTO_ALGO_OFF			0
#define	CRYPTO_ALGO_WEP1		1
#define	CRYPTO_ALGO_TKIP		2
#define	CRYPTO_ALGO_WEP128		3
#define CRYPTO_ALGO_AES_CCM		4
#define CRYPTO_ALGO_AES_OCB_MSDU	5
#define CRYPTO_ALGO_AES_OCB_MPDU	6
#if !defined(BCMEXTCCX)
#define CRYPTO_ALGO_NALG		7
#else
#define CRYPTO_ALGO_CKIP		7
#define CRYPTO_ALGO_CKIP_MMH	8
#define CRYPTO_ALGO_WEP_MMH		9
#define CRYPTO_ALGO_NALG		10
#endif 

#define CRYPTO_ALGO_SMS4		11
#define CRYPTO_ALGO_PMK			12	/* for 802.1x supp to set PMK before 4-way */
#define CRYPTO_ALGO_BIP			13  /* 802.11w BIP (aes cmac) */

#define CRYPTO_ALGO_AES_GCM     14  /* 128 bit GCM */
#define CRYPTO_ALGO_AES_CCM256  15  /* 256 bit CCM */
#define CRYPTO_ALGO_AES_GCM256  16  /* 256 bit GCM */
#define CRYPTO_ALGO_BIP_CMAC256 17  /* 256 bit BIP CMAC */
#define CRYPTO_ALGO_BIP_GMAC    18  /* 128 bit BIP GMAC */
#define CRYPTO_ALGO_BIP_GMAC256 19  /* 256 bit BIP GMAC */

#define CRYPTO_ALGO_NONE        CRYPTO_ALGO_OFF

/* algo bit vector */
#define KEY_ALGO_MASK(_algo)	(1 << _algo)

#if defined(BCMEXTCCX)
#define KEY_ALGO_MASK_CCX		(KEY_ALGO_MASK(CRYPTO_ALGO_CKIP) | \
					KEY_ALGO_MASK(CRYPTO_ALGO_CKIP_MMH) | \
					KEY_ALGO_MASK(CRYPTO_ALGO_WEP_MMH))
#endif 

#define KEY_ALGO_MASK_WEP		(KEY_ALGO_MASK(CRYPTO_ALGO_WEP1) | \
					KEY_ALGO_MASK(CRYPTO_ALGO_WEP128) | \
					KEY_ALGO_MASK(CRYPTO_ALGO_NALG))

#define KEY_ALGO_MASK_AES		(KEY_ALGO_MASK(CRYPTO_ALGO_AES_CCM) | \
					KEY_ALGO_MASK(CRYPTO_ALGO_AES_CCM256) | \
					KEY_ALGO_MASK(CRYPTO_ALGO_AES_GCM) | \
					KEY_ALGO_MASK(CRYPTO_ALGO_AES_GCM256))
#define KEY_ALGO_MASK_TKIP		(KEY_ALGO_MASK(CRYPTO_ALGO_TKIP))
#define KEY_ALGO_MASK_WAPI		(KEY_ALGO_MASK(CRYPTO_ALGO_SMS4))

#define WSEC_GEN_MIC_ERROR	0x0001
#define WSEC_GEN_REPLAY		0x0002
#define WSEC_GEN_ICV_ERROR	0x0004
#define WSEC_GEN_MFP_ACT_ERROR	0x0008
#define WSEC_GEN_MFP_DISASSOC_ERROR	0x0010
#define WSEC_GEN_MFP_DEAUTH_ERROR	0x0020

#define WL_SOFT_KEY	(1 << 0)	/* Indicates this key is using soft encrypt */
#define WL_PRIMARY_KEY	(1 << 1)	/* Indicates this key is the primary (ie tx) key */
#if defined(BCMEXTCCX)
#define WL_CKIP_KP	(1 << 4)	/* CMIC */
#define WL_CKIP_MMH	(1 << 5)	/* CKIP */
#else
#define WL_KF_RES_4	(1 << 4)	/* Reserved for backward compat */
#define WL_KF_RES_5	(1 << 5)	/* Reserved for backward compat */
#endif 
#define WL_IBSS_PEER_GROUP_KEY	(1 << 6)	/* Indicates a group key for a IBSS PEER */

/* wireless security bitvec */
#define WEP_ENABLED		0x0001
#define TKIP_ENABLED		0x0002
#define AES_ENABLED		0x0004
#define WSEC_SWFLAG		0x0008
#define SES_OW_ENABLED		0x0040	/* to go into transition mode without setting wep */
#ifdef BCMWAPI_WPI
#define SMS4_ENABLED		0x0100
#endif /* BCMWAPI_WPI */

#define WSEC_WEP_ENABLED(wsec)	((wsec) & WEP_ENABLED)
#define WSEC_TKIP_ENABLED(wsec)	((wsec) & TKIP_ENABLED)
#define WSEC_AES_ENABLED(wsec)	((wsec) & AES_ENABLED)

#ifdef BCMCCX
#define WSEC_CKIP_KP_ENABLED(wsec)	((wsec) & CKIP_KP_ENABLED)
#define WSEC_CKIP_MIC_ENABLED(wsec)	((wsec) & CKIP_MIC_ENABLED)
#define WSEC_CKIP_ENABLED(wsec)	((wsec) & (CKIP_KP_ENABLED|CKIP_MIC_ENABLED))

#ifdef BCMWAPI_WPI
#define WSEC_ENABLED(wsec) \
	((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED | CKIP_KP_ENABLED |	\
	  CKIP_MIC_ENABLED | SMS4_ENABLED))
#else /* BCMWAPI_WPI */
#define WSEC_ENABLED(wsec) \
		((wsec) & \
		 (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED | CKIP_KP_ENABLED | CKIP_MIC_ENABLED))
#endif /* BCMWAPI_WPI */
#else /* defined BCMCCX */
#ifdef BCMWAPI_WPI
#define WSEC_ENABLED(wsec)	((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED | SMS4_ENABLED))
#else /* BCMWAPI_WPI */
#define WSEC_ENABLED(wsec)	((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))
#endif /* BCMWAPI_WPI */
#endif /* BCMCCX */

#define WSEC_SES_OW_ENABLED(wsec)	((wsec) & SES_OW_ENABLED)
#ifdef BCMWAPI_WAI
#define WSEC_SMS4_ENABLED(wsec)	((wsec) & SMS4_ENABLED)
#endif /* BCMWAPI_WAI */

/* Following macros are not used any more. Just kept here to
 * avoid build issue in BISON/CARIBOU branch
 */
#define MFP_CAPABLE		0x0200
#define MFP_REQUIRED	0x0400
#define MFP_SHA256		0x0800 /* a special configuration for STA for WIFI test tool */

/* WPA authentication mode bitvec */
#define WPA_AUTH_DISABLED	0x0000	/* Legacy (i.e., non-WPA) */
#define WPA_AUTH_NONE		0x0001	/* none (IBSS) */
#define WPA_AUTH_UNSPECIFIED	0x0002	/* over 802.1x */
#define WPA_AUTH_PSK		0x0004	/* Pre-shared key */
#if defined(BCMEXTCCX)
#define WPA_AUTH_CCKM		0x0008	/* CCKM */
#define WPA2_AUTH_CCKM		0x0010	/* CCKM2 */
#endif	
/* #define WPA_AUTH_8021X 0x0020 */	/* 802.1x, reserved */
#define WPA2_AUTH_UNSPECIFIED	0x0040	/* over 802.1x */
#define WPA2_AUTH_PSK		0x0080	/* Pre-shared key */
#define BRCM_AUTH_PSK           0x0100  /* BRCM specific PSK */
#define BRCM_AUTH_DPT		0x0200	/* DPT PSK without group keys */
#if defined(BCMWAPI_WAI) || defined(BCMWAPI_WPI)
#define WPA_AUTH_WAPI           0x0400
#define WAPI_AUTH_NONE		WPA_AUTH_NONE	/* none (IBSS) */
#define WAPI_AUTH_UNSPECIFIED	0x0400	/* over AS */
#define WAPI_AUTH_PSK		0x0800	/* Pre-shared key */
#endif /* BCMWAPI_WAI || BCMWAPI_WPI */
#define WPA2_AUTH_1X_SHA256	0x1000  /* 1X with SHA256 key derivation */
#define WPA2_AUTH_TPK		0x2000	/* TDLS Peer Key */
#define WPA2_AUTH_FT		0x4000	/* Fast Transition. */
#define WPA2_AUTH_PSK_SHA256	0x8000	/* PSK with SHA256 key derivation */
#define WPA3_AUTH_SAE_PSK	0x40000 /* SAE with 4-way handshake */
/* WPA2_AUTH_SHA256 not used anymore. Just kept here to avoid build issue in DINGO */
#define WPA2_AUTH_SHA256	0x8000
#define WPA_AUTH_PFN_ANY	0xffffffff	/* for PFN, match only ssid */

/* pmkid */
#define	MAXPMKID		16

/* SROM12 changes */
#define	WLC_IOCTL_MAXLEN		8192	/* max length ioctl buffer required */


#define	WLC_IOCTL_SMLEN			256	/* "small" length ioctl buffer required */
#define WLC_IOCTL_MEDLEN		1536    /* "med" length ioctl buffer required */
#if defined(LCNCONF) || defined(LCN40CONF) || defined(LCN20CONF)
#define WLC_SAMPLECOLLECT_MAXLEN	8192	/* Max Sample Collect buffer */
#else
#define WLC_SAMPLECOLLECT_MAXLEN	10240	/* Max Sample Collect buffer for two cores */
#endif
#define WLC_SAMPLECOLLECT_MAXLEN_LCN40  8192

/* common ioctl definitions */
#define WLC_GET_MAGIC				0
#define WLC_GET_VERSION				1
#define WLC_UP					2
#define WLC_DOWN				3
#define WLC_GET_LOOP				4
#define WLC_SET_LOOP				5
#define WLC_DUMP				6
#define WLC_GET_MSGLEVEL			7
#define WLC_SET_MSGLEVEL			8
#define WLC_GET_PROMISC				9
#define WLC_SET_PROMISC				10
/* #define WLC_OVERLAY_IOCTL			11 */ /* not supported */
#define WLC_GET_RATE				12
#define WLC_GET_MAX_RATE			13
#define WLC_GET_INSTANCE			14
/* #define WLC_GET_FRAG				15 */ /* no longer supported */
/* #define WLC_SET_FRAG				16 */ /* no longer supported */
/* #define WLC_GET_RTS				17 */ /* no longer supported */
/* #define WLC_SET_RTS				18 */ /* no longer supported */
#define WLC_GET_INFRA				19
#define WLC_SET_INFRA				20
#define WLC_GET_AUTH				21
#define WLC_SET_AUTH				22
#define WLC_GET_BSSID				23
#define WLC_SET_BSSID				24
#define WLC_GET_SSID				25
#define WLC_SET_SSID				26
#define WLC_RESTART				27
#define WLC_TERMINATED				28
/* #define WLC_DUMP_SCB				28 */ /* no longer supported */
#define WLC_GET_CHANNEL				29
#define WLC_SET_CHANNEL				30
#define WLC_GET_SRL				31
#define WLC_SET_SRL				32
#define WLC_GET_LRL				33
#define WLC_SET_LRL				34
#define WLC_GET_PLCPHDR				35
#define WLC_SET_PLCPHDR				36
#define WLC_GET_RADIO				37
#define WLC_SET_RADIO				38
#define WLC_GET_PHYTYPE				39
#define WLC_DUMP_RATE				40
#define WLC_SET_RATE_PARAMS			41
#define WLC_GET_FIXRATE				42
#define WLC_SET_FIXRATE				43
/* #define WLC_GET_WEP				42 */ /* no longer supported */
/* #define WLC_SET_WEP				43 */ /* no longer supported */
#define WLC_GET_KEY				44
#define WLC_SET_KEY				45
#define WLC_GET_REGULATORY			46
#define WLC_SET_REGULATORY			47
#define WLC_GET_PASSIVE_SCAN			48
#define WLC_SET_PASSIVE_SCAN			49
#define WLC_SCAN				50
#define WLC_SCAN_RESULTS			51
#define WLC_DISASSOC				52
#define WLC_REASSOC				53
#define WLC_GET_ROAM_TRIGGER			54
#define WLC_SET_ROAM_TRIGGER			55
#define WLC_GET_ROAM_DELTA			56
#define WLC_SET_ROAM_DELTA			57
#define WLC_GET_ROAM_SCAN_PERIOD		58
#define WLC_SET_ROAM_SCAN_PERIOD		59
#define WLC_EVM					60	/* diag */
#define WLC_GET_TXANT				61
#define WLC_SET_TXANT				62
#define WLC_GET_ANTDIV				63
#define WLC_SET_ANTDIV				64
/* #define WLC_GET_TXPWR			65 */ /* no longer supported */
/* #define WLC_SET_TXPWR			66 */ /* no longer supported */
#define WLC_GET_CLOSED				67
#define WLC_SET_CLOSED				68
#define WLC_GET_MACLIST				69
#define WLC_SET_MACLIST				70
#define WLC_GET_RATESET				71
#define WLC_SET_RATESET				72
/* #define WLC_GET_LOCALE			73 */ /* no longer supported */
#define WLC_LONGTRAIN				74
#define WLC_GET_BCNPRD				75
#define WLC_SET_BCNPRD				76
#define WLC_GET_DTIMPRD				77
#define WLC_SET_DTIMPRD				78
#define WLC_GET_SROM				79
#define WLC_SET_SROM				80
#define WLC_GET_WEP_RESTRICT			81
#define WLC_SET_WEP_RESTRICT			82
#define WLC_GET_COUNTRY				83
#define WLC_SET_COUNTRY				84
#define WLC_GET_PM				85
#define WLC_SET_PM				86
#define WLC_GET_WAKE				87
#define WLC_SET_WAKE				88
/* #define WLC_GET_D11CNTS			89 */ /* -> "counters" iovar */
#define WLC_GET_FORCELINK			90	/* ndis only */
#define WLC_SET_FORCELINK			91	/* ndis only */
#define WLC_FREQ_ACCURACY			92	/* diag */
#define WLC_CARRIER_SUPPRESS			93	/* diag */
#define WLC_GET_PHYREG				94
#define WLC_SET_PHYREG				95
#define WLC_GET_RADIOREG			96
#define WLC_SET_RADIOREG			97
#define WLC_GET_REVINFO				98
#define WLC_GET_UCANTDIV			99
#define WLC_SET_UCANTDIV			100
#define WLC_R_REG				101
#define WLC_W_REG				102
/* #define WLC_DIAG_LOOPBACK			103	old tray diag */
/* #define WLC_RESET_D11CNTS			104 */ /* -> "reset_d11cnts" iovar */
#define WLC_GET_MACMODE				105
#define WLC_SET_MACMODE				106
#define WLC_GET_MONITOR				107
#define WLC_SET_MONITOR				108
#define WLC_GET_GMODE				109
#define WLC_SET_GMODE				110
#define WLC_GET_LEGACY_ERP			111
#define WLC_SET_LEGACY_ERP			112
#define WLC_GET_RX_ANT				113
#define WLC_GET_CURR_RATESET			114	/* current rateset */
#define WLC_GET_SCANSUPPRESS			115
#define WLC_SET_SCANSUPPRESS			116
#define WLC_GET_AP				117
#define WLC_SET_AP				118
#define WLC_GET_EAP_RESTRICT			119
#define WLC_SET_EAP_RESTRICT			120
#define WLC_SCB_AUTHORIZE			121
#define WLC_SCB_DEAUTHORIZE			122
#define WLC_GET_WDSLIST				123
#define WLC_SET_WDSLIST				124
#define WLC_GET_ATIM				125
#define WLC_SET_ATIM				126
#define WLC_GET_RSSI				127
#define WLC_GET_PHYANTDIV			128
#define WLC_SET_PHYANTDIV			129
#define WLC_AP_RX_ONLY				130
#define WLC_GET_TX_PATH_PWR			131
#define WLC_SET_TX_PATH_PWR			132
#define WLC_GET_WSEC				133
#define WLC_SET_WSEC				134
#define WLC_GET_PHY_NOISE			135
#define WLC_GET_BSS_INFO			136
#define WLC_GET_PKTCNTS				137
#define WLC_GET_LAZYWDS				138
#define WLC_SET_LAZYWDS				139
#define WLC_GET_BANDLIST			140

#define WLC_GET_BAND				141
#define WLC_SET_BAND				142
#define WLC_SCB_DEAUTHENTICATE			143
#define WLC_GET_SHORTSLOT			144
#define WLC_GET_SHORTSLOT_OVERRIDE		145
#define WLC_SET_SHORTSLOT_OVERRIDE		146
#define WLC_GET_SHORTSLOT_RESTRICT		147
#define WLC_SET_SHORTSLOT_RESTRICT		148
#define WLC_GET_GMODE_PROTECTION		149
#define WLC_GET_GMODE_PROTECTION_OVERRIDE	150
#define WLC_SET_GMODE_PROTECTION_OVERRIDE	151
#define WLC_UPGRADE				152
/* #define WLC_GET_MRATE			153 */ /* no longer supported */
/* #define WLC_SET_MRATE			154 */ /* no longer supported */
#define WLC_GET_IGNORE_BCNS			155
#define WLC_SET_IGNORE_BCNS			156
#define WLC_GET_SCB_TIMEOUT			157
#define WLC_SET_SCB_TIMEOUT			158
#define WLC_GET_ASSOCLIST			159
#define WLC_GET_CLK				160
#define WLC_SET_CLK				161
#define WLC_GET_UP				162
#define WLC_OUT					163
#define WLC_GET_WPA_AUTH			164
#define WLC_SET_WPA_AUTH			165
#define WLC_GET_UCFLAGS				166
#define WLC_SET_UCFLAGS				167
#define WLC_GET_PWRIDX				168
#define WLC_SET_PWRIDX				169
#define WLC_GET_TSSI				170
#define WLC_GET_SUP_RATESET_OVERRIDE		171
#define WLC_SET_SUP_RATESET_OVERRIDE		172
/* #define WLC_SET_FAST_TIMER			173 */ /* no longer supported */
/* #define WLC_GET_FAST_TIMER			174 */ /* no longer supported */
/* #define WLC_SET_SLOW_TIMER			175 */ /* no longer supported */
/* #define WLC_GET_SLOW_TIMER			176 */ /* no longer supported */
/* #define WLC_DUMP_PHYREGS			177 */ /* no longer supported */
#define WLC_GET_PROTECTION_CONTROL		178
#define WLC_SET_PROTECTION_CONTROL		179
#define WLC_GET_PHYLIST				180
#define WLC_ENCRYPT_STRENGTH			181	/* ndis only */
#define WLC_DECRYPT_STATUS			182	/* ndis only */
#define WLC_GET_KEY_SEQ				183
#define WLC_GET_SCAN_CHANNEL_TIME		184
#define WLC_SET_SCAN_CHANNEL_TIME		185
#define WLC_GET_SCAN_UNASSOC_TIME		186
#define WLC_SET_SCAN_UNASSOC_TIME		187
#define WLC_GET_SCAN_HOME_TIME			188
#define WLC_SET_SCAN_HOME_TIME			189
#define WLC_GET_SCAN_NPROBES			190
#define WLC_SET_SCAN_NPROBES			191
#define WLC_GET_PRB_RESP_TIMEOUT		192
#define WLC_SET_PRB_RESP_TIMEOUT		193
#define WLC_GET_ATTEN				194
#define WLC_SET_ATTEN				195
#define WLC_GET_SHMEM				196	/* diag */
#define WLC_SET_SHMEM				197	/* diag */
/* #define WLC_GET_GMODE_PROTECTION_CTS		198 */ /* no longer supported */
/* #define WLC_SET_GMODE_PROTECTION_CTS		199 */ /* no longer supported */
#define WLC_SET_WSEC_TEST			200
#define WLC_SCB_DEAUTHENTICATE_FOR_REASON	201
#define WLC_TKIP_COUNTERMEASURES		202
#define WLC_GET_PIOMODE				203
#define WLC_SET_PIOMODE				204
#define WLC_SET_ASSOC_PREFER			205
#define WLC_GET_ASSOC_PREFER			206
#define WLC_SET_ROAM_PREFER			207
#define WLC_GET_ROAM_PREFER			208
#define WLC_SET_LED				209
#define WLC_GET_LED				210
#define WLC_GET_INTERFERENCE_MODE		211
#define WLC_SET_INTERFERENCE_MODE		212
#define WLC_GET_CHANNEL_QA			213
#define WLC_START_CHANNEL_QA			214
#define WLC_GET_CHANNEL_SEL			215
#define WLC_START_CHANNEL_SEL			216
#define WLC_GET_VALID_CHANNELS			217
#define WLC_GET_FAKEFRAG			218
#define WLC_SET_FAKEFRAG			219
#define WLC_GET_PWROUT_PERCENTAGE		220
#define WLC_SET_PWROUT_PERCENTAGE		221
#define WLC_SET_BAD_FRAME_PREEMPT		222
#define WLC_GET_BAD_FRAME_PREEMPT		223
#define WLC_SET_LEAP_LIST			224
#define WLC_GET_LEAP_LIST			225
#define WLC_GET_CWMIN				226
#define WLC_SET_CWMIN				227
#define WLC_GET_CWMAX				228
#define WLC_SET_CWMAX				229
#define WLC_GET_WET				230
#define WLC_SET_WET				231
#define WLC_GET_PUB				232
/* #define WLC_SET_GLACIAL_TIMER		233 */ /* no longer supported */
/* #define WLC_GET_GLACIAL_TIMER		234 */ /* no longer supported */
#define WLC_GET_KEY_PRIMARY			235
#define WLC_SET_KEY_PRIMARY			236


/* #define WLC_DUMP_RADIOREGS			237 */ /* no longer supported */
#define WLC_GET_ACI_ARGS			238
#define WLC_SET_ACI_ARGS			239
#define WLC_UNSET_CALLBACK			240
#define WLC_SET_CALLBACK			241
#define WLC_GET_RADAR				242
#define WLC_SET_RADAR				243
#define WLC_SET_SPECT_MANAGMENT			244
#define WLC_GET_SPECT_MANAGMENT			245
#define WLC_WDS_GET_REMOTE_HWADDR		246	/* handled in wl_linux.c/wl_vx.c */
#define WLC_WDS_GET_WPA_SUP			247
#define WLC_SET_CS_SCAN_TIMER			248
#define WLC_GET_CS_SCAN_TIMER			249
#define WLC_MEASURE_REQUEST			250
#define WLC_INIT				251
#define WLC_SEND_QUIET				252
#define WLC_KEEPALIVE			253
#define WLC_SEND_PWR_CONSTRAINT			254
#define WLC_UPGRADE_STATUS			255
#define WLC_CURRENT_PWR				256
#define WLC_GET_SCAN_PASSIVE_TIME		257
#define WLC_SET_SCAN_PASSIVE_TIME		258
#define WLC_LEGACY_LINK_BEHAVIOR		259
#define WLC_GET_CHANNELS_IN_COUNTRY		260
#define WLC_GET_COUNTRY_LIST			261
#define WLC_GET_VAR				262	/* get value of named variable */
#define WLC_SET_VAR				263	/* set named variable to value */
#define WLC_NVRAM_GET				264	/* deprecated */
#define WLC_NVRAM_SET				265
#define WLC_NVRAM_DUMP				266
#define WLC_REBOOT				267
#define WLC_SET_WSEC_PMK			268
#define WLC_GET_AUTH_MODE			269
#define WLC_SET_AUTH_MODE			270
#define WLC_GET_WAKEENTRY			271
#define WLC_SET_WAKEENTRY			272
#define WLC_NDCONFIG_ITEM			273	/* currently handled in wl_oid.c */
#define WLC_NVOTPW				274
#define WLC_OTPW				275
#define WLC_IOV_BLOCK_GET			276
#define WLC_IOV_MODULES_GET			277
#define WLC_SOFT_RESET				278
#define WLC_GET_ALLOW_MODE			279
#define WLC_SET_ALLOW_MODE			280
#define WLC_GET_DESIRED_BSSID			281
#define WLC_SET_DESIRED_BSSID			282
#define	WLC_DISASSOC_MYAP			283
#define WLC_GET_NBANDS				284	/* for Dongle EXT_STA support */
#define WLC_GET_BANDSTATES			285	/* for Dongle EXT_STA support */
#define WLC_GET_WLC_BSS_INFO			286	/* for Dongle EXT_STA support */
#define WLC_GET_ASSOC_INFO			287	/* for Dongle EXT_STA support */
#define WLC_GET_OID_PHY				288	/* for Dongle EXT_STA support */
#define WLC_SET_OID_PHY				289	/* for Dongle EXT_STA support */
#define WLC_SET_ASSOC_TIME			290	/* for Dongle EXT_STA support */
#define WLC_GET_DESIRED_SSID			291	/* for Dongle EXT_STA support */
#define WLC_GET_CHANSPEC			292	/* for Dongle EXT_STA support */
#define WLC_GET_ASSOC_STATE			293	/* for Dongle EXT_STA support */
#define WLC_SET_PHY_STATE			294	/* for Dongle EXT_STA support */
#define WLC_GET_SCAN_PENDING			295	/* for Dongle EXT_STA support */
#define WLC_GET_SCANREQ_PENDING			296	/* for Dongle EXT_STA support */
#define WLC_GET_PREV_ROAM_REASON		297	/* for Dongle EXT_STA support */
#define WLC_SET_PREV_ROAM_REASON		298	/* for Dongle EXT_STA support */
#define WLC_GET_BANDSTATES_PI			299	/* for Dongle EXT_STA support */
#define WLC_GET_PHY_STATE			300	/* for Dongle EXT_STA support */
#define WLC_GET_BSS_WPA_RSN			301	/* for Dongle EXT_STA support */
#define WLC_GET_BSS_WPA2_RSN			302	/* for Dongle EXT_STA support */
#define WLC_GET_BSS_BCN_TS			303	/* for Dongle EXT_STA support */
#define WLC_GET_INT_DISASSOC			304	/* for Dongle EXT_STA support */
#define WLC_SET_NUM_PEERS			305     /* for Dongle EXT_STA support */
#define WLC_GET_NUM_BSS				306	/* for Dongle EXT_STA support */
#define WLC_PHY_SAMPLE_COLLECT			307	/* phy sample collect mode */
/* #define WLC_UM_PRIV				308 */	/* Deprecated: usermode driver */
#define WLC_GET_CMD				309
/* #define WLC_LAST				310 */	/* Never used - can be reused */
#define WLC_SET_INTERFERENCE_OVERRIDE_MODE	311	/* set inter mode override */
#define WLC_GET_INTERFERENCE_OVERRIDE_MODE	312	/* get inter mode override */
/* #define WLC_GET_WAI_RESTRICT	313 */
/* #define WLC_SET_WAI_RESTRICT	314 */
/* #define WLC_SET_WAI_REKEY	315 */
#define WLC_SET_NAT_CONFIG			316	/* for configuring NAT filter driver */
#define WLC_GET_NAT_STATE			317
#define WLC_GET_TXBF_RATESET			318
#define WLC_SET_TXBF_RATESET			319
#define WLC_SCAN_CQ				320
#define WLC_GET_RSSI_QDB			321 /* qdB portion of the RSSI */
#define WLC_DUMP_RATESET			322
#define WLC_ECHO				323
#define WLC_LAST				324
#define WLC_SPEC_FLAG			0x80000000 /* For some special IOCTL */
#ifndef EPICTRL_COOKIE
#define EPICTRL_COOKIE		0xABADCEDE
#endif

/* vx wlc ioctl's offset */
#define CMN_IOCTL_OFF 0x180

/*
 * custom OID support
 *
 * 0xFF - implementation specific OID
 * 0xE4 - first byte of Broadcom PCI vendor ID
 * 0x14 - second byte of Broadcom PCI vendor ID
 * 0xXX - the custom OID number
 */

/* begin 0x1f values beyond the start of the ET driver range. */
#define WL_OID_BASE		0xFFE41420

/* NDIS overrides */
#define OID_WL_GETINSTANCE	(WL_OID_BASE + WLC_GET_INSTANCE)
#define OID_WL_GET_FORCELINK	(WL_OID_BASE + WLC_GET_FORCELINK)
#define OID_WL_SET_FORCELINK	(WL_OID_BASE + WLC_SET_FORCELINK)
#define	OID_WL_ENCRYPT_STRENGTH	(WL_OID_BASE + WLC_ENCRYPT_STRENGTH)
#define OID_WL_DECRYPT_STATUS	(WL_OID_BASE + WLC_DECRYPT_STATUS)
#define OID_LEGACY_LINK_BEHAVIOR (WL_OID_BASE + WLC_LEGACY_LINK_BEHAVIOR)
#define OID_WL_NDCONFIG_ITEM	(WL_OID_BASE + WLC_NDCONFIG_ITEM)

/* EXT_STA Dongle suuport */
#define OID_STA_CHANSPEC	(WL_OID_BASE + WLC_GET_CHANSPEC)
#define OID_STA_NBANDS		(WL_OID_BASE + WLC_GET_NBANDS)
#define OID_STA_GET_PHY		(WL_OID_BASE + WLC_GET_OID_PHY)
#define OID_STA_SET_PHY		(WL_OID_BASE + WLC_SET_OID_PHY)
#define OID_STA_ASSOC_TIME	(WL_OID_BASE + WLC_SET_ASSOC_TIME)
#define OID_STA_DESIRED_SSID	(WL_OID_BASE + WLC_GET_DESIRED_SSID)
#define OID_STA_SET_PHY_STATE	(WL_OID_BASE + WLC_SET_PHY_STATE)
#define OID_STA_SCAN_PENDING	(WL_OID_BASE + WLC_GET_SCAN_PENDING)
#define OID_STA_SCANREQ_PENDING (WL_OID_BASE + WLC_GET_SCANREQ_PENDING)
#define OID_STA_GET_ROAM_REASON (WL_OID_BASE + WLC_GET_PREV_ROAM_REASON)
#define OID_STA_SET_ROAM_REASON (WL_OID_BASE + WLC_SET_PREV_ROAM_REASON)
#define OID_STA_GET_PHY_STATE	(WL_OID_BASE + WLC_GET_PHY_STATE)
#define OID_STA_INT_DISASSOC	(WL_OID_BASE + WLC_GET_INT_DISASSOC)
#define OID_STA_SET_NUM_PEERS	(WL_OID_BASE + WLC_SET_NUM_PEERS)
#define OID_STA_GET_NUM_BSS	(WL_OID_BASE + WLC_GET_NUM_BSS)

/* NAT filter driver support */
#define OID_NAT_SET_CONFIG	(WL_OID_BASE + WLC_SET_NAT_CONFIG)
#define OID_NAT_GET_STATE	(WL_OID_BASE + WLC_GET_NAT_STATE)

#define WL_DECRYPT_STATUS_SUCCESS	1
#define WL_DECRYPT_STATUS_FAILURE	2
#define WL_DECRYPT_STATUS_UNKNOWN	3

/* allows user-mode app to poll the status of USB image upgrade */
#define WLC_UPGRADE_SUCCESS			0
#define WLC_UPGRADE_PENDING			1

/* WLC_GET_AUTH, WLC_SET_AUTH values */
#define WL_AUTH_OPEN_SYSTEM		0	/* d11 open authentication */
#define WL_AUTH_SHARED_KEY		1	/* d11 shared authentication */
#define WL_AUTH_OPEN_SHARED		2	/* try open, then shared if open failed w/rc 13 */

/* a large TX Power as an init value to factor out of MIN() calculations,
 * keep low enough to fit in an int8, units are .25 dBm
 */
#define WLC_TXPWR_MAX		(127)	/* ~32 dBm = 1,500 mW */

/* "diag" iovar argument and error code */
#define WL_DIAG_INTERRUPT			1	/* d11 loopback interrupt test */
#define WL_DIAG_LOOPBACK			2	/* d11 loopback data test */
#define WL_DIAG_MEMORY				3	/* d11 memory test */
#define WL_DIAG_LED				4	/* LED test */
#define WL_DIAG_REG				5	/* d11/phy register test */
#define WL_DIAG_SROM				6	/* srom read/crc test */
#define WL_DIAG_DMA				7	/* DMA test */
#define WL_DIAG_LOOPBACK_EXT			8	/* enhenced d11 loopback data test */

#define WL_DIAGERR_SUCCESS			0
#define WL_DIAGERR_FAIL_TO_RUN			1	/* unable to run requested diag */
#define WL_DIAGERR_NOT_SUPPORTED		2	/* diag requested is not supported */
#define WL_DIAGERR_INTERRUPT_FAIL		3	/* loopback interrupt test failed */
#define WL_DIAGERR_LOOPBACK_FAIL		4	/* loopback data test failed */
#define WL_DIAGERR_SROM_FAIL			5	/* srom read failed */
#define WL_DIAGERR_SROM_BADCRC			6	/* srom crc failed */
#define WL_DIAGERR_REG_FAIL			7	/* d11/phy register test failed */
#define WL_DIAGERR_MEMORY_FAIL			8	/* d11 memory test failed */
#define WL_DIAGERR_NOMEM			9	/* diag test failed due to no memory */
#define WL_DIAGERR_DMA_FAIL			10	/* DMA test failed */

#define WL_DIAGERR_MEMORY_TIMEOUT		11	/* d11 memory test didn't finish in time */
#define WL_DIAGERR_MEMORY_BADPATTERN		12	/* d11 memory test result in bad pattern */

/* band types */
#define	WLC_BAND_AUTO		0	/* auto-select */
#define	WLC_BAND_5G		1	/* 5 Ghz */
#define	WLC_BAND_2G		2	/* 2.4 Ghz */
#define	WLC_BAND_ALL		3	/* all bands */
#define WLC_BAND_INVALID	-1	/* Invalid band */

/* band range returned by band_range iovar */
#define WL_CHAN_FREQ_RANGE_2G      0
#define WL_CHAN_FREQ_RANGE_5GL     1
#define WL_CHAN_FREQ_RANGE_5GM     2
#define WL_CHAN_FREQ_RANGE_5GH     3

#define WL_CHAN_FREQ_RANGE_5GLL_5BAND    4
#define WL_CHAN_FREQ_RANGE_5GLH_5BAND    5
#define WL_CHAN_FREQ_RANGE_5GML_5BAND    6
#define WL_CHAN_FREQ_RANGE_5GMH_5BAND    7
#define WL_CHAN_FREQ_RANGE_5GH_5BAND     8

#define WL_CHAN_FREQ_RANGE_5G_BAND0     1
#define WL_CHAN_FREQ_RANGE_5G_BAND1     2
#define WL_CHAN_FREQ_RANGE_5G_BAND2     3
#define WL_CHAN_FREQ_RANGE_5G_BAND3     4
#define WL_CHAN_FREQ_RANGE_5G_4BAND     5

/* SROM12 */
#define WL_CHAN_FREQ_RANGE_5G_BAND4 5
#define WL_CHAN_FREQ_RANGE_2G_40 6
#define WL_CHAN_FREQ_RANGE_5G_BAND0_40 7
#define WL_CHAN_FREQ_RANGE_5G_BAND1_40 8
#define WL_CHAN_FREQ_RANGE_5G_BAND2_40 9
#define WL_CHAN_FREQ_RANGE_5G_BAND3_40 10
#define WL_CHAN_FREQ_RANGE_5G_BAND4_40 11
#define WL_CHAN_FREQ_RANGE_5G_BAND0_80 12
#define WL_CHAN_FREQ_RANGE_5G_BAND1_80 13
#define WL_CHAN_FREQ_RANGE_5G_BAND2_80 14
#define WL_CHAN_FREQ_RANGE_5G_BAND3_80 15
#define WL_CHAN_FREQ_RANGE_5G_BAND4_80 16

#define WL_CHAN_FREQ_RANGE_5G_5BAND	18
#define WL_CHAN_FREQ_RANGE_5G_5BAND_40	19
#define WL_CHAN_FREQ_RANGE_5G_5BAND_80	20

#define WLC_MACMODE_DISABLED	0	/* MAC list disabled */
#define WLC_MACMODE_DENY	1	/* Deny specified (i.e. allow unspecified) */
#define WLC_MACMODE_ALLOW	2	/* Allow specified (i.e. deny unspecified) */

/*
 * 54g modes (basic bits may still be overridden)
 *
 * GMODE_LEGACY_B			Rateset: 1b, 2b, 5.5, 11
 *					Preamble: Long
 *					Shortslot: Off
 * GMODE_AUTO				Rateset: 1b, 2b, 5.5b, 11b, 18, 24, 36, 54
 *					Extended Rateset: 6, 9, 12, 48
 *					Preamble: Long
 *					Shortslot: Auto
 * GMODE_ONLY				Rateset: 1b, 2b, 5.5b, 11b, 18, 24b, 36, 54
 *					Extended Rateset: 6b, 9, 12b, 48
 *					Preamble: Short required
 *					Shortslot: Auto
 * GMODE_B_DEFERRED			Rateset: 1b, 2b, 5.5b, 11b, 18, 24, 36, 54
 *					Extended Rateset: 6, 9, 12, 48
 *					Preamble: Long
 *					Shortslot: On
 * GMODE_PERFORMANCE			Rateset: 1b, 2b, 5.5b, 6b, 9, 11b, 12b, 18, 24b, 36, 48, 54
 *					Preamble: Short required
 *					Shortslot: On and required
 * GMODE_LRS				Rateset: 1b, 2b, 5.5b, 11b
 *					Extended Rateset: 6, 9, 12, 18, 24, 36, 48, 54
 *					Preamble: Long
 *					Shortslot: Auto
 */
#define GMODE_LEGACY_B		0
#define GMODE_AUTO		1
#define GMODE_ONLY		2
#define GMODE_B_DEFERRED	3
#define GMODE_PERFORMANCE	4
#define GMODE_LRS		5
#define GMODE_MAX		6

/* values for PLCPHdr_override */
#define WLC_PLCP_AUTO	-1
#define WLC_PLCP_SHORT	0
#define WLC_PLCP_LONG	1

/* values for g_protection_override and n_protection_override */
#define WLC_PROTECTION_AUTO		-1
#define WLC_PROTECTION_OFF		0
#define WLC_PROTECTION_ON		1
#define WLC_PROTECTION_MMHDR_ONLY	2
#define WLC_PROTECTION_CTS_ONLY		3

/* values for g_protection_control and n_protection_control */
#define WLC_PROTECTION_CTL_OFF		0
#define WLC_PROTECTION_CTL_LOCAL	1
#define WLC_PROTECTION_CTL_OVERLAP	2

/* values for n_protection */
#define WLC_N_PROTECTION_OFF		0
#define WLC_N_PROTECTION_OPTIONAL	1
#define WLC_N_PROTECTION_20IN40		2
#define WLC_N_PROTECTION_MIXEDMODE	3

/* values for n_preamble_type */
#define WLC_N_PREAMBLE_MIXEDMODE	0
#define WLC_N_PREAMBLE_GF		1
#define WLC_N_PREAMBLE_GF_BRCM          2

/* values for band specific 40MHz capabilities (deprecated) */
#define WLC_N_BW_20ALL			0
#define WLC_N_BW_40ALL			1
#define WLC_N_BW_20IN2G_40IN5G		2

#define WLC_BW_20MHZ_BIT		(1<<0)
#define WLC_BW_40MHZ_BIT		(1<<1)
#define WLC_BW_80MHZ_BIT		(1<<2)
#define WLC_BW_160MHZ_BIT		(1<<3)
#define WLC_BW_10MHZ_BIT		(1<<4)
#define WLC_BW_5MHZ_BIT			(1<<5)
#define WLC_BW_2P5MHZ_BIT		(1<<6)
/* Bandwidth capabilities */
#define WLC_BW_CAP_20MHZ		(WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_40MHZ		(WLC_BW_40MHZ_BIT|WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_80MHZ		(WLC_BW_80MHZ_BIT|WLC_BW_40MHZ_BIT|WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_160MHZ		(WLC_BW_160MHZ_BIT|WLC_BW_80MHZ_BIT| \
	WLC_BW_40MHZ_BIT|WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_2P5MHZ		(WLC_BW_2P5MHZ_BIT)
#define WLC_BW_CAP_5MHZ			(WLC_BW_5MHZ_BIT)
#define WLC_BW_CAP_10MHZ		(WLC_BW_10MHZ_BIT)
#define WLC_BW_CAP_UNRESTRICTED		0xFF

#define WL_BW_CAP_20MHZ(bw_cap)	(((bw_cap) & WLC_BW_20MHZ_BIT) ? TRUE : FALSE)
#define WL_BW_CAP_40MHZ(bw_cap)	(((bw_cap) & WLC_BW_40MHZ_BIT) ? TRUE : FALSE)
#define WL_BW_CAP_80MHZ(bw_cap)	(((bw_cap) & WLC_BW_80MHZ_BIT) ? TRUE : FALSE)
#define WL_BW_CAP_160MHZ(bw_cap)(((bw_cap) & WLC_BW_160MHZ_BIT) ? TRUE : FALSE)
#define WL_BW_CAP_2P5MHZ(bw_cap)(((bw_cap) & WLC_BW_2P5MHZ_BIT) ? TRUE : FALSE)
#define WL_BW_CAP_5MHZ(bw_cap)	(((bw_cap) & WLC_BW_5MHZ_BIT) ? TRUE : FALSE)
#define WL_BW_CAP_10MHZ(bw_cap)	(((bw_cap) & WLC_BW_10MHZ_BIT) ? TRUE : FALSE)
/* values to force tx/rx chain */
#define WLC_N_TXRX_CHAIN0		0
#define WLC_N_TXRX_CHAIN1		1

/* bitflags for SGI support (sgi_rx iovar) */
#define WLC_N_SGI_20			0x01
#define WLC_N_SGI_40			0x02
#define WLC_VHT_SGI_80			0x04
#define WLC_VHT_SGI_160			0x08

/* when sgi_tx==WLC_SGI_ALL, bypass rate selection, enable sgi for all mcs */
#define WLC_SGI_ALL				0x02

#define LISTEN_INTERVAL			10
/* interference mitigation options */
#define	INTERFERE_OVRRIDE_OFF	-1	/* interference override off */
#define	INTERFERE_NONE	0	/* off */
#define	NON_WLAN	1	/* foreign/non 802.11 interference, no auto detect */
#define	WLAN_MANUAL	2	/* ACI: no auto detection */
#define	WLAN_AUTO	3	/* ACI: auto detect */
#define	WLAN_AUTO_W_NOISE	4	/* ACI: auto - detect and non 802.11 interference */
#define AUTO_ACTIVE	(1 << 7) /* Auto is currently active */

/* interfernece mode bit-masks (ACPHY) */
#define ACPHY_ACI_GLITCHBASED_DESENSE 1   /* bit 0 */
#define ACPHY_ACI_HWACI_PKTGAINLMT 2      /* bit 1 */
#define ACPHY_ACI_W2NB_PKTGAINLMT 4       /* bit 2 */
#define ACPHY_ACI_PREEMPTION 8            /* bit 3 */
#define ACPHY_HWACI_MITIGATION 16         /* bit 4 */
#define ACPHY_LPD_PREEMPTION 32           /* bit 5 */
#define ACPHY_HWOBSS_MITIGATION 64        /* bit 6 */
#define ACPHY_ACI_MAX_MODE 127

/* AP environment */
#define AP_ENV_DETECT_NOT_USED		0 /* We aren't using AP environment detection */
#define AP_ENV_DENSE			1 /* "Corporate" or other AP dense environment */
#define AP_ENV_SPARSE			2 /* "Home" or other sparse environment */
#define AP_ENV_INDETERMINATE		3 /* AP environment hasn't been identified */

#define TRIGGER_NOW				0
#define TRIGGER_CRS				0x01
#define TRIGGER_CRSDEASSERT			0x02
#define TRIGGER_GOODFCS				0x04
#define TRIGGER_BADFCS				0x08
#define TRIGGER_BADPLCP				0x10
#define TRIGGER_CRSGLITCH			0x20

#define	WL_SAMPLEDATA_HEADER_TYPE	1
#define WL_SAMPLEDATA_HEADER_SIZE	80	/* sample collect header size (bytes) */
#define	WL_SAMPLEDATA_TYPE		2
#define	WL_SAMPLEDATA_SEQ		0xff	/* sequence # */
#define	WL_SAMPLEDATA_MORE_DATA		0x100	/* more data mask */

/* WL_OTA START */
#define WL_OTA_ARG_PARSE_BLK_SIZE	1200
#define WL_OTA_TEST_MAX_NUM_RATE	30
#define WL_OTA_TEST_MAX_NUM_SEQ		100
#define WL_OTA_TEST_MAX_NUM_RSSI	85
#define WL_THRESHOLD_LO_BAND	70	/* range from 5250MHz - 5350MHz */

/* radar iovar SET defines */
#define WL_RADAR_DETECTOR_OFF		0	/* radar detector off */
#define WL_RADAR_DETECTOR_ON		1	/* radar detector on */
#define WL_RADAR_SIMULATED		2	/* force radar detector to declare
						 * detection once
						 */
#define WL_RADAR_SIMULATED_SC		3	/* force radar detector to declare
						 * detection once on scan core
						 * if available and active
						 */
#define WL_RSSI_ANT_VERSION	1	/* current version of wl_rssi_ant_t */
#define WL_ANT_RX_MAX		2	/* max 2 receive antennas */
#define WL_ANT_HT_RX_MAX	4	/* max 4 receive antennas/cores */
#define WL_ANT_IDX_1		0	/* antenna index 1 */
#define WL_ANT_IDX_2		1	/* antenna index 2 */

#ifndef WL_RSSI_ANT_MAX
#define WL_RSSI_ANT_MAX		4	/* max possible rx antennas */
#elif WL_RSSI_ANT_MAX != 4
#error "WL_RSSI_ANT_MAX does not match"
#endif

/* dfs_status iovar-related defines */

/* cac - channel availability check,
 * ism - in-service monitoring
 * csa - channel switching announcement
 */

/* cac state values */
#define WL_DFS_CACSTATE_IDLE		0	/* state for operating in non-radar channel */
#define	WL_DFS_CACSTATE_PREISM_CAC	1	/* CAC in progress */
#define WL_DFS_CACSTATE_ISM		2	/* ISM in progress */
#define WL_DFS_CACSTATE_CSA		3	/* csa */
#define WL_DFS_CACSTATE_POSTISM_CAC	4	/* ISM CAC */
#define WL_DFS_CACSTATE_PREISM_OOC	5	/* PREISM OOC */
#define WL_DFS_CACSTATE_POSTISM_OOC	6	/* POSTISM OOC */
#define WL_DFS_CACSTATES		7	/* this many states exist */

/* Defines used with channel_bandwidth for curpower */
#define WL_BW_20MHZ		0
#define WL_BW_40MHZ		1
#define WL_BW_80MHZ		2
#define WL_BW_160MHZ		3
#define WL_BW_8080MHZ		4
#define WL_BW_2P5MHZ		5
#define WL_BW_5MHZ		6
#define WL_BW_10MHZ		7

/* tx_power_t.flags bits */
#define WL_TX_POWER_F_ENABLED	1
#define WL_TX_POWER_F_HW		2
#define WL_TX_POWER_F_MIMO		4
#define WL_TX_POWER_F_SISO		8
#define WL_TX_POWER_F_HT		0x10
#define WL_TX_POWER_F_VHT		0x20
#define WL_TX_POWER_F_OPENLOOP		0x40
#define WL_TX_POWER_F_PROP11NRATES	0x80
#define WL_TX_POWER_F_UNIT_QDBM		0x100
/* Message levels */
#define WL_ERROR_VAL		0x00000001
#define WL_TRACE_VAL		0x00000002
#define WL_PRHDRS_VAL		0x00000004
#define WL_PRPKT_VAL		0x00000008
#define WL_INFORM_VAL		0x00000010
#define WL_TMP_VAL		0x00000020
#define WL_OID_VAL		0x00000040
#define WL_RATE_VAL		0x00000080
#define WL_ASSOC_VAL		0x00000100
#define WL_PRUSR_VAL		0x00000200
#define WL_PS_VAL		0x00000400
#define WL_TXPWR_VAL		0x00000000	/* retired in TOT on 6/10/2009 */
#define WL_MODE_SWITCH_VAL	0x00000800 /* Using retired TXPWR val */
#define WL_PORT_VAL		0x00001000
#define WL_DUAL_VAL		0x00002000
#define WL_WSEC_VAL		0x00004000
#define WL_WSEC_DUMP_VAL	0x00008000
#define WL_LOG_VAL		0x00010000
#define WL_NRSSI_VAL		0x00000000	/* retired in TOT on 6/10/2009 */
#define WL_BCNTRIM_VAL		0x00020000	/* Using retired NRSSI VAL */
#define WL_LOFT_VAL		0x00000000	/* retired in TOT on 6/10/2009 */
#define WL_PFN_VAL		0x00040000 /* Using retired LOFT_VAL */
#define WL_REGULATORY_VAL	0x00080000
#define WL_CSA_VAL		0x00080000  /* Reusing REGULATORY_VAL due to lackof bits */
#define WL_TAF_VAL		0x00100000
#define WL_RADAR_VAL		0x00000000	/* retired in TOT on 6/10/2009 */
#define WL_WDI_VAL		0x00200000	/* Using retired WL_RADAR_VAL VAL */
#define WL_MPC_VAL		0x00400000
#define WL_APSTA_VAL		0x00800000
#define WL_DFS_VAL		0x01000000
#define WL_BA_VAL		0x00000000	/* retired in TOT on 6/14/2010 */
#define WL_MUMIMO_VAL		0x02000000      /* Using retired WL_BA_VAL */
#define WL_ACI_VAL		0x04000000
#define WL_PRMAC_VAL		0x04000000
#define WL_MBSS_VAL		0x04000000
#define WL_CAC_VAL		0x08000000
#define WL_AMSDU_VAL		0x10000000
#define WL_AMPDU_VAL		0x20000000
#define WL_FFPLD_VAL		0x40000000
#define WL_ROAM_EXP_VAL		0x80000000

/* wl_msg_level is full. For new bits take the next one and AND with
 * wl_msg_level2 in wl_dbg.h
 */
#define WL_DPT_VAL		0x00000001
/* re-using WL_DPT_VAL */
/* re-using WL_MESH_VAL */
#define WL_NATOE_VAL		0x00000001
#define WL_MESH_VAL		0x00000001
#define WL_SCAN_VAL		0x00000002
#define WL_WOWL_VAL		0x00000004
#define WL_COEX_VAL		0x00000008
#define WL_RTDC_VAL		0x00000010
#define WL_PROTO_VAL		0x00000020
#define WL_SWDIV_VAL		0x00000040
#define WL_CHANINT_VAL		0x00000080
#define WL_WMF_VAL		0x00000100
#define WL_P2P_VAL		0x00000200
#define WL_ITFR_VAL		0x00000400
#define WL_MCHAN_VAL		0x00000800
#define WL_TDLS_VAL		0x00001000
#define WL_MCNX_VAL		0x00002000
#define WL_PROT_VAL		0x00004000
#define WL_PSTA_VAL		0x00008000
#define WL_TSO_VAL		0x00010000
#define WL_TRF_MGMT_VAL		0x00020000
#define WL_LPC_VAL	        0x00040000
#define WL_L2FILTER_VAL		0x00080000
#define WL_TXBF_VAL		0x00100000
#define WL_P2PO_VAL		0x00200000
#define WL_TBTT_VAL		0x00400000
#define WL_FBT_VAL		0x00800000
#define WL_RRM_VAL		0x00800000 /* reuse */
#define WL_MQ_VAL		0x01000000

/* This level is currently used in Phoenix2 only */
#define WL_SRSCAN_VAL		0x02000000

#define WL_WNM_VAL		0x04000000
/* re-using WL_WNM_VAL for MBO */
#define WL_MBO_VAL		0x04000000
#define WL_PWRSEL_VAL		0x10000000
#define WL_NET_DETECT_VAL	0x20000000
#define WL_OCE_VAL  0x20000000 /* reuse */
#define WL_PCIE_VAL		0x40000000
#define WL_PMDUR_VAL	0x80000000


/* use top-bit for WL_TIME_STAMP_VAL because this is a modifier
 * rather than a message-type of its own
 */
#define WL_TIMESTAMP_VAL        0x80000000

/* max # of leds supported by GPIO (gpio pin# == led index#) */
#define	WL_LED_NUMGPIO		32	/* gpio 0-31 */

/* led per-pin behaviors */
#define	WL_LED_OFF		0		/* always off */
#define	WL_LED_ON		1		/* always on */
#define	WL_LED_ACTIVITY		2		/* activity */
#define	WL_LED_RADIO		3		/* radio enabled */
#define	WL_LED_ARADIO		4		/* 5  Ghz radio enabled */
#define	WL_LED_BRADIO		5		/* 2.4Ghz radio enabled */
#define	WL_LED_BGMODE		6		/* on if gmode, off if bmode */
#define	WL_LED_WI1		7
#define	WL_LED_WI2		8
#define	WL_LED_WI3		9
#define	WL_LED_ASSOC		10		/* associated state indicator */
#define	WL_LED_INACTIVE		11		/* null behavior (clears default behavior) */
#define	WL_LED_ASSOCACT		12		/* on when associated; blink fast for activity */
#define WL_LED_WI4		13
#define WL_LED_WI5		14
#define	WL_LED_BLINKSLOW	15		/* blink slow */
#define	WL_LED_BLINKMED		16		/* blink med */
#define	WL_LED_BLINKFAST	17		/* blink fast */
#define	WL_LED_BLINKCUSTOM	18		/* blink custom */
#define	WL_LED_BLINKPERIODIC	19		/* blink periodic (custom 1000ms / off 400ms) */
#define WL_LED_ASSOC_WITH_SEC	20		/* when connected with security */
						/* keep on for 300 sec */
#define WL_LED_START_OFF	21		/* off upon boot, could be turned on later */
#define WL_LED_WI6		22
#define WL_LED_WI7		23
#define WL_LED_WI8		24
#define	WL_LED_NUMBEHAVIOR	25

/* led behavior numeric value format */
#define	WL_LED_BEH_MASK		0x3f		/* behavior mask */
#define	WL_LED_PMU_OVERRIDE	0x40		/* need to set PMU Override bit for the GPIO */
#define	WL_LED_AL_MASK		0x80		/* activelow (polarity) bit */

/* number of bytes needed to define a proper bit mask for MAC event reporting */
#define BCMIO_ROUNDUP(x, y)	((((x) + ((y) - 1)) / (y)) * (y))
#define BCMIO_NBBY		8
#define WL_EVENTING_MASK_LEN	(16+4)

#define WL_EVENTING_MASK_EXT_LEN \
    MAX(WL_EVENTING_MASK_LEN, (ROUNDUP(WLC_E_LAST, NBBY)/NBBY))

/* join preference types */
#define WL_JOIN_PREF_RSSI	1	/* by RSSI */
#define WL_JOIN_PREF_WPA	2	/* by akm and ciphers */
#define WL_JOIN_PREF_BAND	3	/* by 802.11 band */
#define WL_JOIN_PREF_RSSI_DELTA	4	/* by 802.11 band only if RSSI delta condition matches */
#define WL_JOIN_PREF_TRANS_PREF	5	/* defined by requesting AP */

/* band preference */
#define WLJP_BAND_ASSOC_PREF	255	/* use what WLC_SET_ASSOC_PREFER ioctl specifies */

/* any multicast cipher suite */
#define WL_WPA_ACP_MCS_ANY	"\x00\x00\x00\x00"

/* 802.11h measurement types */
#define WLC_MEASURE_TPC			1
#define WLC_MEASURE_CHANNEL_BASIC	2
#define WLC_MEASURE_CHANNEL_CCA		3
#define WLC_MEASURE_CHANNEL_RPI		4

/* regulatory enforcement levels */
#define SPECT_MNGMT_OFF			0		/* both 11h and 11d disabled */
#define SPECT_MNGMT_LOOSE_11H		1		/* allow non-11h APs in scan lists */
#define SPECT_MNGMT_STRICT_11H		2		/* prune out non-11h APs from scan list */
#define SPECT_MNGMT_STRICT_11D		3		/* switch to 802.11D mode */
/* SPECT_MNGMT_LOOSE_11H_D - same as SPECT_MNGMT_LOOSE with the exception that Country IE
 * adoption is done regardless of capability spectrum_management
 */
#define SPECT_MNGMT_LOOSE_11H_D		4		/* operation defined above */

/* bit position in per_chan_info; these depend on current country/regulatory domain */
#define WL_CHAN_VALID_HW		(1 << 0)	/* valid with current HW */
#define WL_CHAN_VALID_SW		(1 << 1)	/* valid with current country setting */
#define WL_CHAN_BAND_5G			(1 << 2)	/* 5GHz-band channel */
#define WL_CHAN_RADAR			(1 << 3)	/* radar sensitive channel */
#define WL_CHAN_INACTIVE		(1 << 4)	/* temporarily inactive due to radar */
#define WL_CHAN_PASSIVE			(1 << 5)	/* channel is in passive mode */
#define WL_CHAN_RESTRICTED		(1 << 6)	/* restricted use channel */
#define WL_CHAN_RADAR_EU_WEATHER	(1 << 7)	/* EU Radar weather channel. Implies an
							 * EU Radar channel.
							 */
#define WL_CHAN_CLM_RESTRICTED		(1 << 8)	/* channel restricted in CLM
							 * (i.e. by default)
							 */

/* following definition is for precommit; will be removed once wl, acsd switch to the new def */
#define WL_CHAN_WEATHER_RADAR		WL_CHAN_RADAR_EU_WEATHER

/* BTC mode used by "btc_mode" iovar */
#define	WL_BTC_DISABLE		0	/* disable BT coexistence */
#define WL_BTC_FULLTDM      1	/* full TDM COEX */
#define WL_BTC_ENABLE       1	/* full TDM COEX to maintain backward compatiblity */
#define WL_BTC_PREMPT      2    /* full TDM COEX with preemption */
#define WL_BTC_LITE        3	/* light weight coex for large isolation platform */
#define WL_BTC_PARALLEL		4   /* BT and WLAN run in parallel with separate antenna  */
#define WL_BTC_HYBRID		5   /* hybrid coex, only ack is allowed to transmit in BT slot */
#define WL_BTC_DEFAULT		8	/* set the default mode for the device */
#define WL_INF_BTC_DISABLE      0
#define WL_INF_BTC_ENABLE       1
#define WL_INF_BTC_AUTO         3

/* BTC wire used by "btc_wire" iovar */
#define	WL_BTC_DEFWIRE		0	/* use default wire setting */
#define WL_BTC_2WIRE		2	/* use 2-wire BTC */
#define WL_BTC_3WIRE		3	/* use 3-wire BTC */
#define WL_BTC_4WIRE		4	/* use 4-wire BTC */

/* BTC flags: BTC configuration that can be set by host */
#define WL_BTC_FLAG_PREMPT               (1 << 0)
#define WL_BTC_FLAG_BT_DEF               (1 << 1)
#define WL_BTC_FLAG_ACTIVE_PROT          (1 << 2)
#define WL_BTC_FLAG_SIM_RSP              (1 << 3)
#define WL_BTC_FLAG_PS_PROTECT           (1 << 4)
#define WL_BTC_FLAG_SIM_TX_LP	         (1 << 5)
#define WL_BTC_FLAG_ECI                  (1 << 6)
#define WL_BTC_FLAG_LIGHT                (1 << 7)
#define WL_BTC_FLAG_PARALLEL             (1 << 8)

/* maximum channels returned by the get valid channels iovar */
#define WL_NUMCHANNELS		64

/* max number of chanspecs (used by the iovar to calc. buf space) */
#ifdef WL11AC_80P80
#define WL_NUMCHANSPECS 206
#else
#define WL_NUMCHANSPECS 110
#endif

/* WDS link local endpoint WPA role */
#define WL_WDS_WPA_ROLE_AUTH	0	/* authenticator */
#define WL_WDS_WPA_ROLE_SUP	1	/* supplicant */
#define WL_WDS_WPA_ROLE_AUTO	255	/* auto, based on mac addr value */

/* Base offset values */
#define WL_PKT_FILTER_BASE_PKT   0
#define WL_PKT_FILTER_BASE_END   1
#define WL_PKT_FILTER_BASE_D11_H 2 /* May be removed */
#define WL_PKT_FILTER_BASE_D11_D 3 /* May be removed */
#define WL_PKT_FILTER_BASE_ETH_H 4
#define WL_PKT_FILTER_BASE_ETH_D 5
#define WL_PKT_FILTER_BASE_ARP_H 6
#define WL_PKT_FILTER_BASE_ARP_D 7 /* May be removed */
#define WL_PKT_FILTER_BASE_IP4_H 8
#define WL_PKT_FILTER_BASE_IP4_D 9
#define WL_PKT_FILTER_BASE_IP6_H 10
#define WL_PKT_FILTER_BASE_IP6_D 11
#define WL_PKT_FILTER_BASE_TCP_H 12
#define WL_PKT_FILTER_BASE_TCP_D 13 /* May be removed */
#define WL_PKT_FILTER_BASE_UDP_H 14
#define WL_PKT_FILTER_BASE_UDP_D 15
#define WL_PKT_FILTER_BASE_IP6_P 16
#define WL_PKT_FILTER_BASE_COUNT 17 /* May be removed */

/* String mapping for bases that may be used by applications or debug */
#define WL_PKT_FILTER_BASE_NAMES \
	{ "START", WL_PKT_FILTER_BASE_PKT },   \
	{ "END",   WL_PKT_FILTER_BASE_END },   \
	{ "ETH_H", WL_PKT_FILTER_BASE_ETH_H }, \
	{ "ETH_D", WL_PKT_FILTER_BASE_ETH_D }, \
	{ "D11_H", WL_PKT_FILTER_BASE_D11_H }, \
	{ "D11_D", WL_PKT_FILTER_BASE_D11_D }, \
	{ "ARP_H", WL_PKT_FILTER_BASE_ARP_H }, \
	{ "IP4_H", WL_PKT_FILTER_BASE_IP4_H }, \
	{ "IP4_D", WL_PKT_FILTER_BASE_IP4_D }, \
	{ "IP6_H", WL_PKT_FILTER_BASE_IP6_H }, \
	{ "IP6_D", WL_PKT_FILTER_BASE_IP6_D }, \
	{ "IP6_P", WL_PKT_FILTER_BASE_IP6_P }, \
	{ "TCP_H", WL_PKT_FILTER_BASE_TCP_H }, \
	{ "TCP_D", WL_PKT_FILTER_BASE_TCP_D }, \
	{ "UDP_H", WL_PKT_FILTER_BASE_UDP_H }, \
	{ "UDP_D", WL_PKT_FILTER_BASE_UDP_D }

/* Flags for a pattern list element */
#define WL_PKT_FILTER_MFLAG_NEG 0x0001

/*
 * Packet engine interface
 */

#define WL_PKTENG_PER_TX_START			0x01
#define WL_PKTENG_PER_TX_STOP			0x02
#define WL_PKTENG_PER_RX_START			0x04
#define WL_PKTENG_PER_RX_WITH_ACK_START		0x05
#define WL_PKTENG_PER_TX_WITH_ACK_START		0x06
#define WL_PKTENG_PER_RX_STOP			0x08
#define WL_PKTENG_PER_RU_TX_START		0x09
#define WL_PKTENG_PER_MASK			0xff

#define WL_PKTENG_SYNCHRONOUS			0x100	/* synchronous flag */
#define WL_PKTENG_SYNCHRONOUS_UNBLK		0x200	/* synchronous unblock flag */
#ifdef PKTENG_LONGPKTSZ
/* max pktsz limit for pkteng */
#define WL_PKTENG_MAXPKTSZ				PKTENG_LONGPKTSZ
#else
#define WL_PKTENG_MAXPKTSZ				16384
#endif

#define NUM_80211b_RATES	4
#define NUM_80211ag_RATES	8
#define NUM_80211n_RATES	32
#define NUM_80211_RATES		(NUM_80211b_RATES+NUM_80211ag_RATES+NUM_80211n_RATES)

/*
 * WOWL capability/override settings
 */
#define WL_WOWL_MAGIC           (1 << 0)    /* Wakeup on Magic packet */
#define WL_WOWL_NET             (1 << 1)    /* Wakeup on Netpattern */
#define WL_WOWL_DIS             (1 << 2)    /* Wakeup on loss-of-link due to Disassoc/Deauth */
#define WL_WOWL_RETR            (1 << 3)    /* Wakeup on retrograde TSF */
#define WL_WOWL_BCN             (1 << 4)    /* Wakeup on loss of beacon */
#define WL_WOWL_TST             (1 << 5)    /* Wakeup after test */
#define WL_WOWL_M1              (1 << 6)    /* Wakeup after PTK refresh */
#define WL_WOWL_EAPID           (1 << 7)    /* Wakeup after receipt of EAP-Identity Req */
#define WL_WOWL_PME_GPIO        (1 << 8)    /* Wakeind via PME(0) or GPIO(1) */
#define WL_WOWL_ULP_BAILOUT     (1 << 8)    /* wakeind via unknown pkt by basic ULP-offloads -
 * WL_WOWL_ULP_BAILOUT - same as WL_WOWL_PME_GPIO used only for DONGLE BUILDS
 */
#define WL_WOWL_NEEDTKIP1       (1 << 9)    /* need tkip phase 1 key to be updated by the driver */
#define WL_WOWL_GTK_FAILURE     (1 << 10)   /* enable wakeup if GTK fails */
#define WL_WOWL_EXTMAGPAT       (1 << 11)   /* support extended magic packets */
#define WL_WOWL_ARPOFFLOAD      (1 << 12)   /* support ARP/NS/keepalive offloading */
#define WL_WOWL_WPA2            (1 << 13)   /* read protocol version for EAPOL frames */
#define WL_WOWL_KEYROT          (1 << 14)   /* If the bit is set, use key rotaton */
#define WL_WOWL_BCAST           (1 << 15)   /* If the bit is set, frm received was bcast frame */
#define WL_WOWL_SCANOL          (1 << 16)   /* If the bit is set, scan offload is enabled */
#define WL_WOWL_TCPKEEP_TIME    (1 << 17)   /* Wakeup on tcpkeep alive timeout */
#define WL_WOWL_MDNS_CONFLICT   (1 << 18)   /* Wakeup on mDNS Conflict Resolution */
#define WL_WOWL_MDNS_SERVICE    (1 << 19)   /* Wakeup on mDNS Service Connect */
#define WL_WOWL_TCPKEEP_DATA    (1 << 20)   /* tcp keepalive got data */
#define WL_WOWL_FW_HALT         (1 << 21)   /* Firmware died in wowl mode */
#define WL_WOWL_ENAB_HWRADIO    (1 << 22)   /* Enable detection of radio button changes */
#define WL_WOWL_MIC_FAIL        (1 << 23)   /* Offloads detected MIC failure(s) */
#define WL_WOWL_UNASSOC         (1 << 24)   /* Wakeup in Unassociated state (Net/Magic Pattern) */
#define WL_WOWL_SECURE          (1 << 25)   /* Wakeup if received matched secured pattern */
#define WL_WOWL_EXCESS_WAKE     (1 << 26)   /* Excess wake */
#define WL_WOWL_LINKDOWN        (1 << 31)   /* Link Down indication in WoWL mode */

#define WL_WOWL_TCPKEEP         (1 << 20)   /* temp copy to satisfy automerger */
#define MAGIC_PKT_MINLEN 102    /* Magic pkt min length is 6 * 0xFF + 16 * ETHER_ADDR_LEN */

#define WOWL_PATTEN_TYPE_ARP	(1 << 0)	/* ARP offload Pattern */
#define WOWL_PATTEN_TYPE_NA	(1 << 1)	/* NA offload Pattern */

#define MAGIC_PKT_MINLEN	102    /* Magic pkt min length is 6 * 0xFF + 16 * ETHER_ADDR_LEN */
#define MAGIC_PKT_NUM_MAC_ADDRS	16

/* Overlap BSS Scan parameters default, minimum, maximum */
#define WLC_OBSS_SCAN_PASSIVE_DWELL_DEFAULT		20	/* unit TU */
#define WLC_OBSS_SCAN_PASSIVE_DWELL_MIN			5	/* unit TU */
#define WLC_OBSS_SCAN_PASSIVE_DWELL_MAX			1000	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_DWELL_DEFAULT		10	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_DWELL_MIN			10	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_DWELL_MAX			1000	/* unit TU */
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_DEFAULT	300	/* unit Sec */
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_MIN		10	/* unit Sec */
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_MAX		900	/* unit Sec */
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_DEFAULT	5
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_MIN	5
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_MAX	100
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_DEFAULT	200	/* unit TU */
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_MIN	200	/* unit TU */
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_MAX	10000	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_DEFAULT	20	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_MIN	20	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_MAX	10000	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_DEFAULT	25	/* unit percent */
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_MIN		0	/* unit percent */
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_MAX		100	/* unit percent */

#define WL_MIN_NUM_OBSS_SCAN_ARG 7	/* minimum number of arguments required for OBSS Scan */

#define WL_COEX_INFO_MASK		0x07
#define WL_COEX_INFO_REQ		0x01
#define	WL_COEX_40MHZ_INTOLERANT	0x02
#define	WL_COEX_WIDTH20			0x04

#define	WLC_RSSI_INVALID	 0	/* invalid RSSI value */

#define MAX_RSSI_LEVELS 8

/* **** EXTLOG **** */
#define EXTLOG_CUR_VER		0x0100

#define MAX_ARGSTR_LEN		18 /* At least big enough for storing ETHER_ADDR_STR_LEN */

/* log modules (bitmap) */
#define LOG_MODULE_COMMON	0x0001
#define LOG_MODULE_ASSOC	0x0002
#define LOG_MODULE_EVENT	0x0004
#define LOG_MODULE_MAX		3			/* Update when adding module */

/* log levels */
#define WL_LOG_LEVEL_DISABLE	0
#define WL_LOG_LEVEL_ERR	1
#define WL_LOG_LEVEL_WARN	2
#define WL_LOG_LEVEL_INFO	3
#define WL_LOG_LEVEL_MAX	WL_LOG_LEVEL_INFO	/* Update when adding level */

/* flag */
#define LOG_FLAG_EVENT		1

/* log arg_type */
#define LOG_ARGTYPE_NULL	0
#define LOG_ARGTYPE_STR		1	/* %s */
#define LOG_ARGTYPE_INT		2	/* %d */
#define LOG_ARGTYPE_INT_STR	3	/* %d...%s */
#define LOG_ARGTYPE_STR_INT	4	/* %s...%d */

/* 802.11 Mgmt Packet flags */
#define VNDR_IE_BEACON_FLAG	0x1
#define VNDR_IE_PRBRSP_FLAG	0x2
#define VNDR_IE_ASSOCRSP_FLAG	0x4
#define VNDR_IE_AUTHRSP_FLAG	0x8
#define VNDR_IE_PRBREQ_FLAG	0x10
#define VNDR_IE_ASSOCREQ_FLAG	0x20
#define VNDR_IE_IWAPID_FLAG	0x40 /* vendor IE in IW advertisement protocol ID field */
#define VNDR_IE_AUTHREQ_FLAG	0x80
#define VNDR_IE_CUSTOM_FLAG	0x100 /* allow custom IE id */
#define VNDR_IE_DISASSOC_FLAG	0x200

#if defined(WLP2P)
/* P2P Action Frames flags (spec ordered) */
#define VNDR_IE_GONREQ_FLAG     0x001000
#define VNDR_IE_GONRSP_FLAG     0x002000
#define VNDR_IE_GONCFM_FLAG     0x004000
#define VNDR_IE_INVREQ_FLAG     0x008000
#define VNDR_IE_INVRSP_FLAG     0x010000
#define VNDR_IE_DISREQ_FLAG     0x020000
#define VNDR_IE_DISRSP_FLAG     0x040000
#define VNDR_IE_PRDREQ_FLAG     0x080000
#define VNDR_IE_PRDRSP_FLAG     0x100000

#define VNDR_IE_P2PAF_SHIFT	12
#endif /* WLP2P */

/* channel interference measurement (chanim) related defines */

/* chanim mode */
#define CHANIM_DISABLE	0	/* disabled */
#define CHANIM_DETECT	1	/* detection only */
#define CHANIM_EXT		2	/* external state machine */
#define CHANIM_ACT		3	/* full internal state machine, detect + act */
#define CHANIM_MODE_MAX 4

/* define for apcs reason code */
#define APCS_INIT		0
#define APCS_IOCTL		1
#define APCS_CHANIM		2
#define APCS_CSTIMER		3
#define APCS_TXDLY		5
#define APCS_NONACSD		6
#define APCS_DFS_REENTRY	7
#define APCS_TXFAIL		8
#define APCS_MAX		9

/* number of ACS record entries */
#define CHANIM_ACS_RECORD			10

/* CHANIM */
#define CCASTATS_TXDUR  0
#define CCASTATS_INBSS  1
#define CCASTATS_OBSS   2
#define CCASTATS_NOCTG  3
#define CCASTATS_NOPKT  4
#define CCASTATS_DOZE   5
#define CCASTATS_TXOP	6
#define CCASTATS_GDTXDUR        7
#define CCASTATS_BDTXDUR        8

#ifndef WLCHANIM_V2
#define CCASTATS_MAX    9
#else /* WLCHANIM_V2 */
#define CCASTATS_MYRX      9
#define CCASTATS_MAX    10
#endif /* WLCHANIM_V2 */

#define WL_CHANIM_COUNT_ALL	0xff
#define WL_CHANIM_COUNT_ONE	0x1

/* ap tpc modes */
#define	AP_TPC_OFF		0
#define	AP_TPC_BSS_PWR		1	/* BSS power control */
#define AP_TPC_AP_PWR		2	/* AP power control */
#define	AP_TPC_AP_BSS_PWR	3	/* Both AP and BSS power control */
#define AP_TPC_MAX_LINK_MARGIN	127

/* ap tpc modes */
#define	AP_TPC_OFF		0
#define	AP_TPC_BSS_PWR		1	/* BSS power control */
#define AP_TPC_AP_PWR		2	/* AP power control */
#define	AP_TPC_AP_BSS_PWR	3	/* Both AP and BSS power control */
#define AP_TPC_MAX_LINK_MARGIN	127

/* state */
#define WL_P2P_DISC_ST_SCAN	0
#define WL_P2P_DISC_ST_LISTEN	1
#define WL_P2P_DISC_ST_SEARCH	2

/* i/f type */
#define WL_P2P_IF_CLIENT	0
#define WL_P2P_IF_GO		1
#define WL_P2P_IF_DYNBCN_GO	2
#define WL_P2P_IF_DEV		3

/* count */
#define WL_P2P_SCHED_RSVD	0
#define WL_P2P_SCHED_REPEAT	255	/* anything > 255 will be treated as 255 */

#define WL_P2P_SCHED_FIXED_LEN		3

/* schedule type */
#define WL_P2P_SCHED_TYPE_ABS		0	/* Scheduled Absence */
#define WL_P2P_SCHED_TYPE_REQ_ABS	1	/* Requested Absence */

/* schedule action during absence periods (for WL_P2P_SCHED_ABS type) */
#define WL_P2P_SCHED_ACTION_NONE	0	/* no action */
#define WL_P2P_SCHED_ACTION_DOZE	1	/* doze */
/* schedule option - WL_P2P_SCHED_TYPE_REQ_ABS */
#define WL_P2P_SCHED_ACTION_GOOFF	2	/* turn off GO beacon/prbrsp functions */
/* schedule option - WL_P2P_SCHED_TYPE_XXX */
#define WL_P2P_SCHED_ACTION_RESET	255	/* reset */

/* schedule option - WL_P2P_SCHED_TYPE_ABS */
#define WL_P2P_SCHED_OPTION_NORMAL	0	/* normal start/interval/duration/count */
#define WL_P2P_SCHED_OPTION_BCNPCT	1	/* percentage of beacon interval */
/* schedule option - WL_P2P_SCHED_TYPE_REQ_ABS */
#define WL_P2P_SCHED_OPTION_TSFOFS	2	/* normal start/internal/duration/count with
						 * start being an offset of the 'current' TSF
						 */

/* feature flags */
#define WL_P2P_FEAT_GO_CSA	(1 << 0)	/* GO moves with the STA using CSA method */
#define WL_P2P_FEAT_GO_NOLEGACY	(1 << 1)	/* GO does not probe respond to non-p2p probe
						 * requests
						 */
#define WL_P2P_FEAT_RESTRICT_DEV_RESP (1 << 2)	/* Restrict p2p dev interface from responding */

/* n-mode support capability */
/* 2x2 includes both 1x1 & 2x2 devices
 * reserved #define 2 for future when we want to separate 1x1 & 2x2 and
 * control it independently
 */
#define WL_11N_2x2			1
#define WL_11N_3x3			3
#define WL_11N_4x4			4

/* define 11n feature disable flags */
#define WLFEATURE_DISABLE_11N		0x00000001
#define WLFEATURE_DISABLE_11N_STBC_TX	0x00000002
#define WLFEATURE_DISABLE_11N_STBC_RX	0x00000004
#define WLFEATURE_DISABLE_11N_SGI_TX	0x00000008
#define WLFEATURE_DISABLE_11N_SGI_RX	0x00000010
#define WLFEATURE_DISABLE_11N_AMPDU_TX	0x00000020
#define WLFEATURE_DISABLE_11N_AMPDU_RX	0x00000040
#define WLFEATURE_DISABLE_11N_GF	0x00000080

/* Proxy STA modes */
#define PSTA_MODE_DISABLED		0
#define PSTA_MODE_PROXY			1
#define PSTA_MODE_REPEATER		2

/* op code in nat_cfg */
#define NAT_OP_ENABLE		1	/* enable NAT on given interface */
#define NAT_OP_DISABLE		2	/* disable NAT on given interface */
#define NAT_OP_DISABLE_ALL	3	/* disable NAT on all interfaces */

/* NAT state */
#define NAT_STATE_ENABLED	1	/* NAT is enabled */
#define NAT_STATE_DISABLED	2	/* NAT is disabled */

#define CHANNEL_5G_LOW_START	36	/* 5G low (36..48) CDD enable/disable bit mask */
#define CHANNEL_5G_MID_START	52	/* 5G mid (52..64) CDD enable/disable bit mask */
#define CHANNEL_5G_HIGH_START	100	/* 5G high (100..140) CDD enable/disable bit mask */
#define CHANNEL_5G_UPPER_START	149	/* 5G upper (149..161) CDD enable/disable bit mask */

/* D0 Coalescing */
#define IPV4_ARP_FILTER		0x0001
#define IPV4_NETBT_FILTER	0x0002
#define IPV4_LLMNR_FILTER	0x0004
#define IPV4_SSDP_FILTER	0x0008
#define IPV4_WSD_FILTER		0x0010
#define IPV6_NETBT_FILTER	0x0200
#define IPV6_LLMNR_FILTER	0x0400
#define IPV6_SSDP_FILTER	0x0800
#define IPV6_WSD_FILTER		0x1000

/* Network Offload Engine */
#define NWOE_OL_ENABLE		0x00000001

/*
 * Traffic management structures/defines.
 */

/* Traffic management bandwidth parameters */
#define TRF_MGMT_MAX_PRIORITIES                 3

#define TRF_MGMT_FLAG_ADD_DSCP                  0x0001  /* Add DSCP to IP TOS field */
#define TRF_MGMT_FLAG_DISABLE_SHAPING           0x0002  /* Don't shape traffic */
#define TRF_MGMT_FLAG_MANAGE_LOCAL_TRAFFIC      0x0008  /* Manage traffic over our local subnet */
#define TRF_MGMT_FLAG_FILTER_ON_MACADDR         0x0010  /* filter on MAC address */
#define TRF_MGMT_FLAG_NO_RX                     0x0020  /* do not apply fiters to rx packets */

#define TRF_FILTER_MAC_ADDR              0x0001 /* L2 filter use dst mac address for filtering */
#define TRF_FILTER_IP_ADDR               0x0002 /* L3 filter use ip ddress for filtering */
#define TRF_FILTER_L4                    0x0004 /* L4 filter use tcp/udp for filtering */
#define TRF_FILTER_DWM                   0x0008 /* L3 filter use DSCP for filtering */
#define TRF_FILTER_FAVORED               0x0010 /* Tag the packet FAVORED */

/* WNM/NPS subfeatures mask */
#define WL_WNM_BSSTRANS		0x00000001
#define WL_WNM_PROXYARP		0x00000002
#define WL_WNM_MAXIDLE		0x00000004
#define WL_WNM_TIMBC		0x00000008
#define WL_WNM_TFS		0x00000010
#define WL_WNM_SLEEP		0x00000020
#define WL_WNM_DMS		0x00000040
#define WL_WNM_FMS		0x00000080
#define WL_WNM_NOTIF		0x00000100
#define WL_WNM_WBTEXT	0x00000200
#define WL_WNM_MAX		0x00000400
#ifdef WLWNM_BRCM
#define BRCM_WNM_FEATURE_SET\
					(WL_WNM_PROXYARP | \
					WL_WNM_SLEEP | \
					WL_WNM_FMS | \
					WL_WNM_TFS | \
					WL_WNM_TIMBC | \
					WL_WNM_BSSTRANS | \
					WL_WNM_DMS | \
					WL_WNM_NOTIF | \
					0)
#endif /* WLWNM_BRCM */
#ifndef ETHER_MAX_DATA
#define ETHER_MAX_DATA	1500
#endif /* ETHER_MAX_DATA */

/* Different discovery modes for dpt */
#define	DPT_DISCOVERY_MANUAL	0x01	/* manual discovery mode */
#define	DPT_DISCOVERY_AUTO	0x02	/* auto discovery mode */
#define	DPT_DISCOVERY_SCAN	0x04	/* scan-based discovery mode */

/* different path selection values */
#define DPT_PATHSEL_AUTO	0	/* auto mode for path selection */
#define DPT_PATHSEL_DIRECT	1	/* always use direct DPT path */
#define DPT_PATHSEL_APPATH	2	/* always use AP path */

/* different ops for deny list */
#define DPT_DENY_LIST_ADD	1	/* add to dpt deny list */
#define DPT_DENY_LIST_REMOVE	2	/* remove from dpt deny list */

/* different ops for manual end point */
#define DPT_MANUAL_EP_CREATE	1	/* create manual dpt endpoint */
#define DPT_MANUAL_EP_MODIFY	2	/* modify manual dpt endpoint */
#define DPT_MANUAL_EP_DELETE	3	/* delete manual dpt endpoint */

/* flags to indicate DPT status */
#define	DPT_STATUS_ACTIVE	0x01	/* link active (though may be suspended) */
#define	DPT_STATUS_AES		0x02	/* link secured through AES encryption */
#define	DPT_STATUS_FAILED	0x04	/* DPT link failed */

#ifdef WLTDLS
/* different ops for manual end point */
#define TDLS_MANUAL_EP_CREATE	1	/* create manual dpt endpoint */
#define TDLS_MANUAL_EP_MODIFY	2	/* modify manual dpt endpoint */
#define TDLS_MANUAL_EP_DELETE	3	/* delete manual dpt endpoint */
#define TDLS_MANUAL_EP_PM		4	/*  put dpt endpoint in PM mode */
#define TDLS_MANUAL_EP_WAKE		5	/* wake up dpt endpoint from PM */
#define TDLS_MANUAL_EP_DISCOVERY	6	/* discover if endpoint is TDLS capable */
#define TDLS_MANUAL_EP_CHSW		7	/* channel switch */
#define TDLS_MANUAL_EP_WFD_TPQ	8	/* WiFi-Display Tunneled Probe reQuest */

/* modes */
#define TDLS_WFD_IE_TX			0
#define TDLS_WFD_IE_RX			1
#define TDLS_WFD_PROBE_IE_TX	2
#define TDLS_WFD_PROBE_IE_RX	3
#endif /* WLTDLS */

/* define for flag */
#define TSPEC_PENDING		0	/* TSPEC pending */
#define TSPEC_ACCEPTED		1	/* TSPEC accepted */
#define TSPEC_REJECTED		2	/* TSPEC rejected */
#define TSPEC_UNKNOWN		3	/* TSPEC unknown */
#define TSPEC_STATUS_MASK	7	/* TSPEC status mask */


/* Software feature flag defines used by wlfeatureflag */
#ifdef WLAFTERBURNER
#define WL_SWFL_ABBFL       0x0001 /* Allow Afterburner on systems w/o hardware BFL */
#define WL_SWFL_ABENCORE    0x0002 /* Allow AB on non-4318E chips */
#endif /* WLAFTERBURNER */
#define WL_SWFL_NOHWRADIO	0x0004
#define WL_SWFL_FLOWCONTROL     0x0008 /* Enable backpressure to OS stack */
#define WL_SWFL_WLBSSSORT	0x0010 /* Per-port supports sorting of BSS */

#define WL_LIFETIME_MAX 0xFFFF /* Max value in ms */

#define CSA_BROADCAST_ACTION_FRAME	0	/* csa broadcast action frame */
#define CSA_UNICAST_ACTION_FRAME	  1 /* csa unicast action frame */

/* Roaming trigger definitions for WLC_SET_ROAM_TRIGGER.
 *
 * (-100 < value < 0)   value is used directly as a roaming trigger in dBm
 * (0 <= value) value specifies a logical roaming trigger level from
 *                      the list below
 *
 * WLC_GET_ROAM_TRIGGER always returns roaming trigger value in dBm, never
 * the logical roam trigger value.
 */
#define WLC_ROAM_TRIGGER_DEFAULT	0 /* default roaming trigger */
#define WLC_ROAM_TRIGGER_BANDWIDTH	1 /* optimize for bandwidth roaming trigger */
#define WLC_ROAM_TRIGGER_DISTANCE	2 /* optimize for distance roaming trigger */
#define WLC_ROAM_TRIGGER_AUTO		3 /* auto-detect environment */
#define WLC_ROAM_TRIGGER_MAX_VALUE	3 /* max. valid value */

#define WLC_ROAM_NEVER_ROAM_TRIGGER	(-100) /* Avoid Roaming by setting a large value */

/* Preferred Network Offload (PNO, formerly PFN) defines */
#define WPA_AUTH_PFN_ANY	0xffffffff	/* for PFN, match only ssid */

#define SORT_CRITERIA_BIT		0
#define AUTO_NET_SWITCH_BIT		1
#define ENABLE_BKGRD_SCAN_BIT		2
#define IMMEDIATE_SCAN_BIT		3
#define	AUTO_CONNECT_BIT		4
#define	ENABLE_BD_SCAN_BIT		5
#define ENABLE_ADAPTSCAN_BIT		6
#define IMMEDIATE_EVENT_BIT		8
#define SUPPRESS_SSID_BIT		9
#define ENABLE_NET_OFFLOAD_BIT		10
/* report found/lost events for SSID and BSSID networks seperately */
#define REPORT_SEPERATELY_BIT		11
#define BESTN_BSSID_ONLY_BIT		12

#define SORT_CRITERIA_MASK		0x0001
#define AUTO_NET_SWITCH_MASK		0x0002
#define ENABLE_BKGRD_SCAN_MASK		0x0004
#define IMMEDIATE_SCAN_MASK		0x0008
#define	AUTO_CONNECT_MASK		0x0010

#define ENABLE_BD_SCAN_MASK		0x0020
#define ENABLE_ADAPTSCAN_MASK		0x00c0
#define IMMEDIATE_EVENT_MASK		0x0100
#define SUPPRESS_SSID_MASK		0x0200
#define ENABLE_NET_OFFLOAD_MASK		0x0400
/* report found/lost events for SSID and BSSID networks seperately */
#define REPORT_SEPERATELY_MASK		0x0800
#define BESTN_BSSID_ONLY_MASK		0x1000

#define PFN_VERSION			2
#ifdef PFN_SCANRESULT_2
#define PFN_SCANRESULT_VERSION		2
#else
#define PFN_SCANRESULT_VERSION		1
#endif /* PFN_SCANRESULT_2 */
#ifndef MAX_PFN_LIST_COUNT
#define MAX_PFN_LIST_COUNT		16
#endif /* MAX_PFN_LIST_COUNT */

#define PFN_COMPLETE			1
#define PFN_INCOMPLETE			0

#define DEFAULT_BESTN			2
#define DEFAULT_MSCAN			0
#define DEFAULT_REPEAT			10
#define DEFAULT_EXP				2

#define PFN_PARTIAL_SCAN_BIT		0
#define PFN_PARTIAL_SCAN_MASK		1

#define WL_PFN_SUPPRESSFOUND_MASK	0x08
#define WL_PFN_SUPPRESSLOST_MASK	0x10
#define WL_PFN_SSID_A_BAND_TRIG		0x20
#define WL_PFN_SSID_BG_BAND_TRIG	0x40
#define WL_PFN_SSID_IMPRECISE_MATCH	0x80
#define WL_PFN_SSID_SAME_NETWORK	0x10000
#define WL_PFN_SUPPRESS_AGING_MASK	0x20000
#define WL_PFN_FLUSH_ALL_SSIDS		0x40000
#define WL_PFN_RSSI_MASK		0xff00
#define WL_PFN_RSSI_SHIFT		8

#define WL_PFN_REPORT_ALLNET    0
#define WL_PFN_REPORT_SSIDNET   1
#define WL_PFN_REPORT_BSSIDNET  2

#define WL_PFN_CFG_FLAGS_PROHIBITED	0x00000001	/* Accept and use prohibited channels */
#define WL_PFN_CFG_FLAGS_HISTORY_OFF	0x00000002	/* Scan history suppressed */

#define WL_PFN_HIDDEN_BIT		2
#define PNO_SCAN_MAX_FW			508*1000	/* max time scan time in msec */
#define PNO_SCAN_MAX_FW_SEC		PNO_SCAN_MAX_FW/1000 /* max time scan time in SEC */
#define PNO_SCAN_MIN_FW_SEC		10			/* min time scan time in SEC */
#define WL_PFN_HIDDEN_MASK		0x4
#define MAX_SSID_WHITELIST_NUM         4
#define MAX_BSSID_PREF_LIST_NUM        32
#define MAX_BSSID_BLACKLIST_NUM        32

#ifndef BESTN_MAX
#define BESTN_MAX			10
#endif

#ifndef MSCAN_MAX
#define MSCAN_MAX			32
#endif

/* TCP Checksum Offload error injection for testing */
#define TOE_ERRTEST_TX_CSUM	0x00000001
#define TOE_ERRTEST_RX_CSUM	0x00000002
#define TOE_ERRTEST_RX_CSUM2	0x00000004

/* ARP Offload feature flags for arp_ol iovar */
#define ARP_OL_AGENT		0x00000001
#define ARP_OL_SNOOP		0x00000002
#define ARP_OL_HOST_AUTO_REPLY	0x00000004
#define ARP_OL_PEER_AUTO_REPLY	0x00000008

/* ARP Offload error injection */
#define ARP_ERRTEST_REPLY_PEER	0x1
#define ARP_ERRTEST_REPLY_HOST	0x2

#define ARP_MULTIHOMING_MAX	8	/* Maximum local host IP addresses */
#define ND_MULTIHOMING_MAX 10	/* Maximum local host IP addresses */
#define ND_REQUEST_MAX		5	/* Max set of offload params */
/* AOAC wake event flag */
#define WAKE_EVENT_NLO_DISCOVERY_BIT		1
#define WAKE_EVENT_AP_ASSOCIATION_LOST_BIT	2
#define WAKE_EVENT_GTK_HANDSHAKE_ERROR_BIT 4
#define WAKE_EVENT_4WAY_HANDSHAKE_REQUEST_BIT 8
#define WAKE_EVENT_NET_PACKET_BIT 0x10

#define MAX_NUM_WOL_PATTERN	22 /* LOGO requirements min 22 */


/* Packet filter operation mode */
/* True: 1; False: 0 */
#define PKT_FILTER_MODE_FORWARD_ON_MATCH		1
/* Enable and disable pkt_filter as a whole */
#define PKT_FILTER_MODE_DISABLE					2
/* Cache first matched rx pkt(be queried by host later) */
#define PKT_FILTER_MODE_PKT_CACHE_ON_MATCH		4
/* If pkt_filter is enabled and no filter is set, don't forward anything */
#define PKT_FILTER_MODE_PKT_FORWARD_OFF_DEFAULT 8

#ifdef DONGLEOVERLAYS
#define OVERLAY_IDX_MASK		0x000000ff
#define OVERLAY_IDX_SHIFT		0
#define OVERLAY_FLAGS_MASK		0xffffff00
#define OVERLAY_FLAGS_SHIFT		8
/* overlay written to device memory immediately after loading the base image */
#define OVERLAY_FLAG_POSTLOAD	0x100
/* defer overlay download until the device responds w/WLC_E_OVL_DOWNLOAD event */
#define OVERLAY_FLAG_DEFER_DL	0x200
/* overlay downloaded prior to the host going to sleep */
#define OVERLAY_FLAG_PRESLEEP	0x400
#define OVERLAY_DOWNLOAD_CHUNKSIZE	1024
#endif /* DONGLEOVERLAYS */

/* reuse two number in the sc/rc space */
#define	SMFS_CODE_MALFORMED 0xFFFE
#define SMFS_CODE_IGNORED	0xFFFD

/* RFAWARE def */
#define BCM_ACTION_RFAWARE		0x77
#define BCM_ACTION_RFAWARE_DCS  0x01

/* DCS reason code define */
#define BCM_DCS_IOVAR		0x1
#define BCM_DCS_UNKNOWN		0xFF

#ifdef PROP_TXSTATUS
/* Bit definitions for tlv iovar */
/*
 * enable RSSI signals:
 * WLFC_CTL_TYPE_RSSI
 */
#define WLFC_FLAGS_RSSI_SIGNALS			0x0001

/* enable (if/mac_open, if/mac_close,, mac_add, mac_del) signals:
 *
 * WLFC_CTL_TYPE_MAC_OPEN
 * WLFC_CTL_TYPE_MAC_CLOSE
 *
 * WLFC_CTL_TYPE_INTERFACE_OPEN
 * WLFC_CTL_TYPE_INTERFACE_CLOSE
 *
 * WLFC_CTL_TYPE_MACDESC_ADD
 * WLFC_CTL_TYPE_MACDESC_DEL
 *
 */
#define WLFC_FLAGS_XONXOFF_SIGNALS		0x0002

/* enable (status, fifo_credit, mac_credit) signals
 * WLFC_CTL_TYPE_MAC_REQUEST_CREDIT
 * WLFC_CTL_TYPE_TXSTATUS
 * WLFC_CTL_TYPE_FIFO_CREDITBACK
 */
#define WLFC_FLAGS_CREDIT_STATUS_SIGNALS	0x0004

#define WLFC_FLAGS_HOST_PROPTXSTATUS_ACTIVE	0x0008
#define WLFC_FLAGS_PSQ_GENERATIONFSM_ENABLE	0x0010
#define WLFC_FLAGS_PSQ_ZERO_BUFFER_ENABLE	0x0020
#define WLFC_FLAGS_HOST_RXRERODER_ACTIVE	0x0040
#define WLFC_FLAGS_PKT_STAMP_SIGNALS		0x0080

#endif /* PROP_TXSTATUS */

#define WL_TIMBC_STATUS_AP_UNKNOWN	255	/* AP status for internal use only */

#define WL_DFRTS_LOGIC_OFF	0	/* Feature is disabled */
#define WL_DFRTS_LOGIC_OR	1	/* OR all non-zero threshold conditions */
#define WL_DFRTS_LOGIC_AND	2	/* AND all non-zero threshold conditions */

/* Definitions for Reliable Multicast */
#define WL_RELMCAST_MAX_CLIENT		32
#define WL_RELMCAST_FLAG_INBLACKLIST	1
#define WL_RELMCAST_FLAG_ACTIVEACKER	2
#define WL_RELMCAST_FLAG_RELMCAST	4

/* structures for proximity detection device role */
#define WL_PROXD_MODE_DISABLE	0
#define WL_PROXD_MODE_NEUTRAL	1
#define WL_PROXD_MODE_INITIATOR	2
#define WL_PROXD_MODE_TARGET	3
#define WL_PROXD_RANDOM_WAKEUP	0x8000


#ifdef NET_DETECT
#define NET_DETECT_MAX_WAKE_DATA_SIZE	2048
#define NET_DETECT_MAX_PROFILES		16
#define NET_DETECT_MAX_CHANNELS		50
#endif /* NET_DETECT */


/* Bit masks for radio disabled status - returned by WL_GET_RADIO */
#define WL_RADIO_SW_DISABLE		(1<<0)
#define WL_RADIO_HW_DISABLE		(1<<1)
#define WL_RADIO_MPC_DISABLE		(1<<2)
#define WL_RADIO_COUNTRY_DISABLE	(1<<3)	/* some countries don't support any channel */
#define WL_RADIO_PERCORE_DISABLE	(1<<4)	/* Radio diable per core for DVT */

#define	WL_SPURAVOID_OFF	0
#define	WL_SPURAVOID_ON1	1
#define	WL_SPURAVOID_ON2	2


#define WL_4335_SPURAVOID_ON1	1
#define WL_4335_SPURAVOID_ON2	2
#define WL_4335_SPURAVOID_ON3	3
#define WL_4335_SPURAVOID_ON4	4
#define WL_4335_SPURAVOID_ON5	5
#define WL_4335_SPURAVOID_ON6	6
#define WL_4335_SPURAVOID_ON7	7
#define WL_4335_SPURAVOID_ON8	8
#define WL_4335_SPURAVOID_ON9	9

/* Override bit for WLC_SET_TXPWR.  if set, ignore other level limits */
#define WL_TXPWR_OVERRIDE	(1U<<31)
#define WL_TXPWR_2G		(1U<<30)
#define WL_TXPWR_5G		(1U<<29)
#define WL_TXPWR_NEG   (1U<<28)

#define WL_TXPWR_MASK		(~(0x7<<29))
#define WL_TXPWR_CORE_MAX	(3)
#define WL_TXPWR_CORE0_MASK	(0x000000FF)
#define WL_TXPWR_CORE0_SHIFT	(0)
#define WL_TXPWR_CORE1_MASK	(0x0000FF00)
#define WL_TXPWR_CORE1_SHIFT	(8)
#define WL_TXPWR_CORE2_MASK	(0x00FF0000)
#define WL_TXPWR_CORE2_SHIFT	(16)

/* phy types (returned by WLC_GET_PHYTPE) */
#define	WLC_PHY_TYPE_A		0
#define	WLC_PHY_TYPE_B		1
#define	WLC_PHY_TYPE_G		2
#define	WLC_PHY_TYPE_N		4
#define	WLC_PHY_TYPE_LP		5
#define	WLC_PHY_TYPE_SSN	6
#define	WLC_PHY_TYPE_HT		7
#define	WLC_PHY_TYPE_LCN	8
#define	WLC_PHY_TYPE_LCN40	10
#define WLC_PHY_TYPE_AC		11
#define	WLC_PHY_TYPE_LCN20	12
#define	WLC_PHY_TYPE_NULL	0xf

/* Values for PM */
#define PM_OFF	0
#define PM_MAX	1
#define PM_FAST 2
#define PM_FORCE_OFF 3		/* use this bit to force PM off even bt is active */

#define WL_WME_CNT_VERSION	1	/* current version of wl_wme_cnt_t */

/* fbt_cap: FBT assoc / reassoc modes. */
#define WLC_FBT_CAP_DRV_4WAY_AND_REASSOC  1 /* Driver 4-way handshake & reassoc (WLFBT). */

/* monitor_promisc_level bits */
#define WL_MONPROMISC_PROMISC 0x0001
#define WL_MONPROMISC_CTRL 0x0002
#define WL_MONPROMISC_FCS 0x0004

/* TCP Checksum Offload defines */
#define TOE_TX_CSUM_OL		0x00000001
#define TOE_RX_CSUM_OL		0x00000002

/* Wi-Fi Display Services (WFDS) */
#define WL_P2P_SOCIAL_CHANNELS_MAX  WL_NUMCHANNELS
#define MAX_WFDS_SEEK_SVC 4	/* Max # of wfds services to seek */
#define MAX_WFDS_ADVERT_SVC 4	/* Max # of wfds services to advertise */
#define MAX_WFDS_SVC_NAME_LEN 200	/* maximum service_name length */
#define MAX_WFDS_ADV_SVC_INFO_LEN 65000	/* maximum adv service_info length */
#define P2P_WFDS_HASH_LEN 6		/* Length of a WFDS service hash */
#define MAX_WFDS_SEEK_SVC_INFO_LEN 255	/* maximum seek service_info req length */
#define MAX_WFDS_SEEK_SVC_NAME_LEN 200	/* maximum service_name length */

/* ap_isolate bitmaps */
#define AP_ISOLATE_DISABLED		0x0
#define AP_ISOLATE_SENDUP_ALL		0x01
#define AP_ISOLATE_SENDUP_MCAST		0x02

/* Type values for the wl_pwrstats_t data field */
#define WL_PWRSTATS_TYPE_PHY		0 /**< struct wl_pwr_phy_stats */
#define WL_PWRSTATS_TYPE_SCAN		1 /**< struct wl_pwr_scan_stats */
#define WL_PWRSTATS_TYPE_USB_HSIC	2 /**< struct wl_pwr_usb_hsic_stats */
#define WL_PWRSTATS_TYPE_PM_AWAKE1	3 /**< struct wl_pwr_pm_awake_stats_v1 */
#define WL_PWRSTATS_TYPE_CONNECTION	4 /* struct wl_pwr_connect_stats; assoc and key-exch time */
#define WL_PWRSTATS_TYPE_PCIE		6 /**< struct wl_pwr_pcie_stats */
#define WL_PWRSTATS_TYPE_PM_AWAKE2	7 /**< struct wl_pwr_pm_awake_stats_v2 */
#define WL_PWRSTATS_TYPE_SDIO		8 /* struct wl_pwr_sdio_stats */
#define WL_PWRSTATS_TYPE_MIMO_PS_METRICS 9 /* struct wl_mimo_meas_metrics_t */

#endif /* wlioctl_defs_h */
