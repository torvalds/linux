/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2003 Herbert Valerio Riedel <hvr@gnu.org>
 *  Copyright (C) 2004 Luis R. Rodriguez <mcgrof@ruslug.rutgers.edu>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
 */

#if !defined(_ISL_OID_H)
#define _ISL_OID_H

/*
 * MIB related constant and structure definitions for communicating
 * with the device firmware
 */

struct obj_ssid {
	u8 length;
	char octets[33];
} __packed;

struct obj_key {
	u8 type;		/* dot11_priv_t */
	u8 length;
	char key[32];
} __packed;

struct obj_mlme {
	u8 address[6];
	u16 id;
	u16 state;
	u16 code;
} __packed;

struct obj_mlmeex {
	u8 address[6];
	u16 id;
	u16 state;
	u16 code;
	u16 size;
	u8 data[];
} __packed;

struct obj_buffer {
	u32 size;
	u32 addr;		/* 32bit bus address */
} __packed;

struct obj_bss {
	u8 address[6];
	int:16;			/* padding */

	char state;
	char reserved;
	short age;

	char quality;
	char rssi;

	struct obj_ssid ssid;
	short channel;
	char beacon_period;
	char dtim_period;
	short capinfo;
	short rates;
	short basic_rates;
	int:16;			/* padding */
} __packed;

struct obj_bsslist {
	u32 nr;
	struct obj_bss bsslist[];
} __packed;

struct obj_frequencies {
	u16 nr;
	u16 mhz[];
} __packed;

struct obj_attachment {
	char type;
	char reserved;
	short id;
	short size;
	char data[];
} __packed;

/*
 * in case everything's ok, the inlined function below will be
 * optimized away by the compiler...
 */
static inline void
__bug_on_wrong_struct_sizes(void)
{
	BUILD_BUG_ON(sizeof (struct obj_ssid) != 34);
	BUILD_BUG_ON(sizeof (struct obj_key) != 34);
	BUILD_BUG_ON(sizeof (struct obj_mlme) != 12);
	BUILD_BUG_ON(sizeof (struct obj_mlmeex) != 14);
	BUILD_BUG_ON(sizeof (struct obj_buffer) != 8);
	BUILD_BUG_ON(sizeof (struct obj_bss) != 60);
	BUILD_BUG_ON(sizeof (struct obj_bsslist) != 4);
	BUILD_BUG_ON(sizeof (struct obj_frequencies) != 2);
}

enum dot11_state_t {
	DOT11_STATE_NONE = 0,
	DOT11_STATE_AUTHING = 1,
	DOT11_STATE_AUTH = 2,
	DOT11_STATE_ASSOCING = 3,

	DOT11_STATE_ASSOC = 5,
	DOT11_STATE_IBSS = 6,
	DOT11_STATE_WDS = 7
};

enum dot11_bsstype_t {
	DOT11_BSSTYPE_NONE = 0,
	DOT11_BSSTYPE_INFRA = 1,
	DOT11_BSSTYPE_IBSS = 2,
	DOT11_BSSTYPE_ANY = 3
};

enum dot11_auth_t {
	DOT11_AUTH_NONE = 0,
	DOT11_AUTH_OS = 1,
	DOT11_AUTH_SK = 2,
	DOT11_AUTH_BOTH = 3
};

enum dot11_mlme_t {
	DOT11_MLME_AUTO = 0,
	DOT11_MLME_INTERMEDIATE = 1,
	DOT11_MLME_EXTENDED = 2
};

enum dot11_priv_t {
	DOT11_PRIV_WEP = 0,
	DOT11_PRIV_TKIP = 1
};

/* Prism "Nitro" / Frameburst / "Packet Frame Grouping"
 * Value is in microseconds. Represents the # microseconds
 * the firmware will take to group frames before sending out then out
 * together with a CSMA contention. Without this all frames are
 * sent with a CSMA contention.
 * Bibliography:
 * https://www.hpl.hp.com/personal/Jean_Tourrilhes/Papers/Packet.Frame.Grouping.html
 */
enum dot11_maxframeburst_t {
	/* Values for DOT11_OID_MAXFRAMEBURST */
	DOT11_MAXFRAMEBURST_OFF = 0, /* Card firmware default */
	DOT11_MAXFRAMEBURST_MIXED_SAFE = 650, /* 802.11 a,b,g safe */
	DOT11_MAXFRAMEBURST_IDEAL = 1300, /* Theoretical ideal level */
	DOT11_MAXFRAMEBURST_MAX = 5000, /* Use this as max,
		* Note: firmware allows for greater values. This is a
		* recommended max. I'll update this as I find
		* out what the real MAX is. Also note that you don't necessarily
		* get better results with a greater value here.
		*/
};

/* Support for 802.11 long and short frame preambles.
 * Long	 preamble uses 128-bit sync field, 8-bit  CRC
 * Short preamble uses 56-bit  sync field, 16-bit CRC
 *
 * 802.11a -- not sure, both optionally ?
 * 802.11b supports long and optionally short
 * 802.11g supports both */
enum dot11_preamblesettings_t {
	DOT11_PREAMBLESETTING_LONG = 0,
		/* Allows *only* long 802.11 preambles */
	DOT11_PREAMBLESETTING_SHORT = 1,
		/* Allows *only* short 802.11 preambles */
	DOT11_PREAMBLESETTING_DYNAMIC = 2
		/* AutomatiGically set */
};

/* Support for 802.11 slot timing (time between packets).
 *
 * Long uses 802.11a slot timing  (9 usec ?)
 * Short uses 802.11b slot timing (20 use ?) */
enum dot11_slotsettings_t {
	DOT11_SLOTSETTINGS_LONG = 0,
		/* Allows *only* long 802.11b slot timing */
	DOT11_SLOTSETTINGS_SHORT = 1,
		/* Allows *only* long 802.11a slot timing */
	DOT11_SLOTSETTINGS_DYNAMIC = 2
		/* AutomatiGically set */
};

/* All you need to know, ERP is "Extended Rate PHY".
 * An Extended Rate PHY (ERP) STA or AP shall support three different
 * preamble and header formats:
 * Long  preamble (refer to above)
 * Short preamble (refer to above)
 * OFDM  preamble ( ? )
 *
 * I'm assuming here Protection tells the AP
 * to be careful, a STA which cannot handle the long pre-amble
 * has joined.
 */
enum do11_nonerpstatus_t {
	DOT11_ERPSTAT_NONEPRESENT = 0,
	DOT11_ERPSTAT_USEPROTECTION = 1
};

/* (ERP is "Extended Rate PHY") Way to read NONERP is NON-ERP-*
 * The key here is DOT11 NON ERP NEVER protects against
 * NON ERP STA's. You *don't* want this unless
 * you know what you are doing. It means you will only
 * get Extended Rate capabilities */
enum dot11_nonerpprotection_t {
	DOT11_NONERP_NEVER = 0,
	DOT11_NONERP_ALWAYS = 1,
	DOT11_NONERP_DYNAMIC = 2
};

/* Preset OID configuration for 802.11 modes
 * Note: DOT11_OID_CW[MIN|MAX] hold the values of the
 * DCS MIN|MAX backoff used */
enum dot11_profile_t { /* And set/allowed values */
	/* Allowed values for DOT11_OID_PROFILES */
	DOT11_PROFILE_B_ONLY = 0,
		/* DOT11_OID_RATES: 1, 2, 5.5, 11Mbps
		 * DOT11_OID_PREAMBLESETTINGS: DOT11_PREAMBLESETTING_DYNAMIC
		 * DOT11_OID_CWMIN: 31
		 * DOT11_OID_NONEPROTECTION: DOT11_NOERP_DYNAMIC
		 * DOT11_OID_SLOTSETTINGS: DOT11_SLOTSETTINGS_LONG
		 */
	DOT11_PROFILE_MIXED_G_WIFI = 1,
		/* DOT11_OID_RATES: 1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54Mbs
		 * DOT11_OID_PREAMBLESETTINGS: DOT11_PREAMBLESETTING_DYNAMIC
		 * DOT11_OID_CWMIN: 15
		 * DOT11_OID_NONEPROTECTION: DOT11_NOERP_DYNAMIC
		 * DOT11_OID_SLOTSETTINGS: DOT11_SLOTSETTINGS_DYNAMIC
		 */
	DOT11_PROFILE_MIXED_LONG = 2, /* "Long range" */
		/* Same as Profile MIXED_G_WIFI */
	DOT11_PROFILE_G_ONLY = 3,
		/* Same as Profile MIXED_G_WIFI */
	DOT11_PROFILE_TEST = 4,
		/* Same as Profile MIXED_G_WIFI except:
		 * DOT11_OID_PREAMBLESETTINGS: DOT11_PREAMBLESETTING_SHORT
		 * DOT11_OID_NONEPROTECTION: DOT11_NOERP_NEVER
		 * DOT11_OID_SLOTSETTINGS: DOT11_SLOTSETTINGS_SHORT
		 */
	DOT11_PROFILE_B_WIFI = 5,
		/* Same as Profile B_ONLY */
	DOT11_PROFILE_A_ONLY = 6,
		/* Same as Profile MIXED_G_WIFI except:
		 * DOT11_OID_RATES: 6, 9, 12, 18, 24, 36, 48, 54Mbs
		 */
	DOT11_PROFILE_MIXED_SHORT = 7
		/* Same as MIXED_G_WIFI */
};


/* The dot11d conformance level configures the 802.11d conformance levels.
 * The following conformance levels exist:*/
enum oid_inl_conformance_t {
	OID_INL_CONFORMANCE_NONE = 0,	/* Perform active scanning */
	OID_INL_CONFORMANCE_STRICT = 1,	/* Strictly adhere to 802.11d */
	OID_INL_CONFORMANCE_FLEXIBLE = 2,	/* Use passed 802.11d info to
		* determine channel AND/OR just make assumption that active
		* channels are valid  channels */
};

enum oid_inl_mode_t {
	INL_MODE_NONE = -1,
	INL_MODE_PROMISCUOUS = 0,
	INL_MODE_CLIENT = 1,
	INL_MODE_AP = 2,
	INL_MODE_SNIFFER = 3
};

enum oid_inl_config_t {
	INL_CONFIG_NOTHING = 0x00,
	INL_CONFIG_MANUALRUN = 0x01,
	INL_CONFIG_FRAMETRAP = 0x02,
	INL_CONFIG_RXANNEX = 0x04,
	INL_CONFIG_TXANNEX = 0x08,
	INL_CONFIG_WDS = 0x10
};

enum oid_inl_phycap_t {
	INL_PHYCAP_2400MHZ = 1,
	INL_PHYCAP_5000MHZ = 2,
	INL_PHYCAP_FAA = 0x80000000,	/* Means card supports the FAA switch */
};


enum oid_num_t {
	GEN_OID_MACADDRESS = 0,
	GEN_OID_LINKSTATE,
	GEN_OID_WATCHDOG,
	GEN_OID_MIBOP,
	GEN_OID_OPTIONS,
	GEN_OID_LEDCONFIG,

	/* 802.11 */
	DOT11_OID_BSSTYPE,
	DOT11_OID_BSSID,
	DOT11_OID_SSID,
	DOT11_OID_STATE,
	DOT11_OID_AID,
	DOT11_OID_COUNTRYSTRING,
	DOT11_OID_SSIDOVERRIDE,

	DOT11_OID_MEDIUMLIMIT,
	DOT11_OID_BEACONPERIOD,
	DOT11_OID_DTIMPERIOD,
	DOT11_OID_ATIMWINDOW,
	DOT11_OID_LISTENINTERVAL,
	DOT11_OID_CFPPERIOD,
	DOT11_OID_CFPDURATION,

	DOT11_OID_AUTHENABLE,
	DOT11_OID_PRIVACYINVOKED,
	DOT11_OID_EXUNENCRYPTED,
	DOT11_OID_DEFKEYID,
	DOT11_OID_DEFKEYX,	/* DOT11_OID_DEFKEY1,...DOT11_OID_DEFKEY4 */
	DOT11_OID_STAKEY,
	DOT11_OID_REKEYTHRESHOLD,
	DOT11_OID_STASC,

	DOT11_OID_PRIVTXREJECTED,
	DOT11_OID_PRIVRXPLAIN,
	DOT11_OID_PRIVRXFAILED,
	DOT11_OID_PRIVRXNOKEY,

	DOT11_OID_RTSTHRESH,
	DOT11_OID_FRAGTHRESH,
	DOT11_OID_SHORTRETRIES,
	DOT11_OID_LONGRETRIES,
	DOT11_OID_MAXTXLIFETIME,
	DOT11_OID_MAXRXLIFETIME,
	DOT11_OID_AUTHRESPTIMEOUT,
	DOT11_OID_ASSOCRESPTIMEOUT,

	DOT11_OID_ALOFT_TABLE,
	DOT11_OID_ALOFT_CTRL_TABLE,
	DOT11_OID_ALOFT_RETREAT,
	DOT11_OID_ALOFT_PROGRESS,
	DOT11_OID_ALOFT_FIXEDRATE,
	DOT11_OID_ALOFT_RSSIGRAPH,
	DOT11_OID_ALOFT_CONFIG,

	DOT11_OID_VDCFX,
	DOT11_OID_MAXFRAMEBURST,

	DOT11_OID_PSM,
	DOT11_OID_CAMTIMEOUT,
	DOT11_OID_RECEIVEDTIMS,
	DOT11_OID_ROAMPREFERENCE,

	DOT11_OID_BRIDGELOCAL,
	DOT11_OID_CLIENTS,
	DOT11_OID_CLIENTSASSOCIATED,
	DOT11_OID_CLIENTX,	/* DOT11_OID_CLIENTX,...DOT11_OID_CLIENT2007 */

	DOT11_OID_CLIENTFIND,
	DOT11_OID_WDSLINKADD,
	DOT11_OID_WDSLINKREMOVE,
	DOT11_OID_EAPAUTHSTA,
	DOT11_OID_EAPUNAUTHSTA,
	DOT11_OID_DOT1XENABLE,
	DOT11_OID_MICFAILURE,
	DOT11_OID_REKEYINDICATE,

	DOT11_OID_MPDUTXSUCCESSFUL,
	DOT11_OID_MPDUTXONERETRY,
	DOT11_OID_MPDUTXMULTIPLERETRIES,
	DOT11_OID_MPDUTXFAILED,
	DOT11_OID_MPDURXSUCCESSFUL,
	DOT11_OID_MPDURXDUPS,
	DOT11_OID_RTSSUCCESSFUL,
	DOT11_OID_RTSFAILED,
	DOT11_OID_ACKFAILED,
	DOT11_OID_FRAMERECEIVES,
	DOT11_OID_FRAMEERRORS,
	DOT11_OID_FRAMEABORTS,
	DOT11_OID_FRAMEABORTSPHY,

	DOT11_OID_SLOTTIME,
	DOT11_OID_CWMIN, /* MIN DCS backoff */
	DOT11_OID_CWMAX, /* MAX DCS backoff */
	DOT11_OID_ACKWINDOW,
	DOT11_OID_ANTENNARX,
	DOT11_OID_ANTENNATX,
	DOT11_OID_ANTENNADIVERSITY,
	DOT11_OID_CHANNEL,
	DOT11_OID_EDTHRESHOLD,
	DOT11_OID_PREAMBLESETTINGS,
	DOT11_OID_RATES,
	DOT11_OID_CCAMODESUPPORTED,
	DOT11_OID_CCAMODE,
	DOT11_OID_RSSIVECTOR,
	DOT11_OID_OUTPUTPOWERTABLE,
	DOT11_OID_OUTPUTPOWER,
	DOT11_OID_SUPPORTEDRATES,
	DOT11_OID_FREQUENCY,
	DOT11_OID_SUPPORTEDFREQUENCIES,
	DOT11_OID_NOISEFLOOR,
	DOT11_OID_FREQUENCYACTIVITY,
	DOT11_OID_IQCALIBRATIONTABLE,
	DOT11_OID_NONERPPROTECTION,
	DOT11_OID_SLOTSETTINGS,
	DOT11_OID_NONERPTIMEOUT,
	DOT11_OID_PROFILES,
	DOT11_OID_EXTENDEDRATES,

	DOT11_OID_DEAUTHENTICATE,
	DOT11_OID_AUTHENTICATE,
	DOT11_OID_DISASSOCIATE,
	DOT11_OID_ASSOCIATE,
	DOT11_OID_SCAN,
	DOT11_OID_BEACON,
	DOT11_OID_PROBE,
	DOT11_OID_DEAUTHENTICATEEX,
	DOT11_OID_AUTHENTICATEEX,
	DOT11_OID_DISASSOCIATEEX,
	DOT11_OID_ASSOCIATEEX,
	DOT11_OID_REASSOCIATE,
	DOT11_OID_REASSOCIATEEX,

	DOT11_OID_NONERPSTATUS,

	DOT11_OID_STATIMEOUT,
	DOT11_OID_MLMEAUTOLEVEL,
	DOT11_OID_BSSTIMEOUT,
	DOT11_OID_ATTACHMENT,
	DOT11_OID_PSMBUFFER,

	DOT11_OID_BSSS,
	DOT11_OID_BSSX,		/*DOT11_OID_BSS1,...,DOT11_OID_BSS64 */
	DOT11_OID_BSSFIND,
	DOT11_OID_BSSLIST,

	OID_INL_TUNNEL,
	OID_INL_MEMADDR,
	OID_INL_MEMORY,
	OID_INL_MODE,
	OID_INL_COMPONENT_NR,
	OID_INL_VERSION,
	OID_INL_INTERFACE_ID,
	OID_INL_COMPONENT_ID,
	OID_INL_CONFIG,
	OID_INL_DOT11D_CONFORMANCE,
	OID_INL_PHYCAPABILITIES,
	OID_INL_OUTPUTPOWER,

	OID_NUM_LAST
};

#define OID_FLAG_CACHED		0x80
#define OID_FLAG_TYPE		0x7f

#define OID_TYPE_U32		0x01
#define OID_TYPE_SSID		0x02
#define OID_TYPE_KEY		0x03
#define OID_TYPE_BUFFER		0x04
#define OID_TYPE_BSS		0x05
#define OID_TYPE_BSSLIST	0x06
#define OID_TYPE_FREQUENCIES	0x07
#define OID_TYPE_MLME		0x08
#define OID_TYPE_MLMEEX		0x09
#define OID_TYPE_ADDR		0x0A
#define OID_TYPE_RAW		0x0B
#define OID_TYPE_ATTACH		0x0C

/* OID_TYPE_MLMEEX is special because of a variable size field when sending.
 * Not yet implemented (not used in driver anyway).
 */

struct oid_t {
	enum oid_num_t oid;
	short range;		/* to define a range of oid */
	short size;		/* max size of the associated data */
	char flags;
};

union oid_res_t {
	void *ptr;
	u32 u;
};

#define	IWMAX_BITRATES	20
#define	IWMAX_BSS	24
#define IWMAX_FREQ	30
#define PRIV_STR_SIZE	1024

#endif				/* !defined(_ISL_OID_H) */
/* EOF */
