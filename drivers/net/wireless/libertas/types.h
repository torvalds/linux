/**
  * This header file contains definition for global types
  */
#ifndef _LBS_TYPES_H_
#define _LBS_TYPES_H_

#include <linux/if_ether.h>
#include <asm/byteorder.h>

struct ieeetypes_cfparamset {
	u8 elementid;
	u8 len;
	u8 cfpcnt;
	u8 cfpperiod;
	__le16 cfpmaxduration;
	__le16 cfpdurationremaining;
} __attribute__ ((packed));


struct ieeetypes_ibssparamset {
	u8 elementid;
	u8 len;
	__le16 atimwindow;
} __attribute__ ((packed));

union IEEEtypes_ssparamset {
	struct ieeetypes_cfparamset cfparamset;
	struct ieeetypes_ibssparamset ibssparamset;
} __attribute__ ((packed));

struct ieeetypes_fhparamset {
	u8 elementid;
	u8 len;
	__le16 dwelltime;
	u8 hopset;
	u8 hoppattern;
	u8 hopindex;
} __attribute__ ((packed));

struct ieeetypes_dsparamset {
	u8 elementid;
	u8 len;
	u8 currentchan;
} __attribute__ ((packed));

union ieeetypes_phyparamset {
	struct ieeetypes_fhparamset fhparamset;
	struct ieeetypes_dsparamset dsparamset;
} __attribute__ ((packed));

struct ieeetypes_assocrsp {
	__le16 capability;
	__le16 statuscode;
	__le16 aid;
	u8 iebuffer[1];
} __attribute__ ((packed));

/** TLV  type ID definition */
#define PROPRIETARY_TLV_BASE_ID		0x0100

/* Terminating TLV type */
#define MRVL_TERMINATE_TLV_ID		0xffff

#define TLV_TYPE_SSID				0x0000
#define TLV_TYPE_RATES				0x0001
#define TLV_TYPE_PHY_FH				0x0002
#define TLV_TYPE_PHY_DS				0x0003
#define TLV_TYPE_CF				    0x0004
#define TLV_TYPE_IBSS				0x0006

#define TLV_TYPE_DOMAIN				0x0007

#define TLV_TYPE_POWER_CAPABILITY	0x0021

#define TLV_TYPE_KEY_MATERIAL       (PROPRIETARY_TLV_BASE_ID + 0)
#define TLV_TYPE_CHANLIST           (PROPRIETARY_TLV_BASE_ID + 1)
#define TLV_TYPE_NUMPROBES          (PROPRIETARY_TLV_BASE_ID + 2)
#define TLV_TYPE_RSSI_LOW           (PROPRIETARY_TLV_BASE_ID + 4)
#define TLV_TYPE_SNR_LOW            (PROPRIETARY_TLV_BASE_ID + 5)
#define TLV_TYPE_FAILCOUNT          (PROPRIETARY_TLV_BASE_ID + 6)
#define TLV_TYPE_BCNMISS            (PROPRIETARY_TLV_BASE_ID + 7)
#define TLV_TYPE_LED_GPIO           (PROPRIETARY_TLV_BASE_ID + 8)
#define TLV_TYPE_LEDBEHAVIOR        (PROPRIETARY_TLV_BASE_ID + 9)
#define TLV_TYPE_PASSTHROUGH        (PROPRIETARY_TLV_BASE_ID + 10)
#define TLV_TYPE_REASSOCAP          (PROPRIETARY_TLV_BASE_ID + 11)
#define TLV_TYPE_POWER_TBL_2_4GHZ   (PROPRIETARY_TLV_BASE_ID + 12)
#define TLV_TYPE_POWER_TBL_5GHZ     (PROPRIETARY_TLV_BASE_ID + 13)
#define TLV_TYPE_BCASTPROBE	    (PROPRIETARY_TLV_BASE_ID + 14)
#define TLV_TYPE_NUMSSID_PROBE	    (PROPRIETARY_TLV_BASE_ID + 15)
#define TLV_TYPE_WMMQSTATUS   	    (PROPRIETARY_TLV_BASE_ID + 16)
#define TLV_TYPE_CRYPTO_DATA	    (PROPRIETARY_TLV_BASE_ID + 17)
#define TLV_TYPE_WILDCARDSSID	    (PROPRIETARY_TLV_BASE_ID + 18)
#define TLV_TYPE_TSFTIMESTAMP	    (PROPRIETARY_TLV_BASE_ID + 19)
#define TLV_TYPE_RSSI_HIGH          (PROPRIETARY_TLV_BASE_ID + 22)
#define TLV_TYPE_SNR_HIGH           (PROPRIETARY_TLV_BASE_ID + 23)

/** TLV related data structures*/
struct mrvlietypesheader {
	__le16 type;
	__le16 len;
} __attribute__ ((packed));

struct mrvlietypes_data {
	struct mrvlietypesheader header;
	u8 Data[1];
} __attribute__ ((packed));

struct mrvlietypes_ratesparamset {
	struct mrvlietypesheader header;
	u8 rates[1];
} __attribute__ ((packed));

struct mrvlietypes_ssidparamset {
	struct mrvlietypesheader header;
	u8 ssid[1];
} __attribute__ ((packed));

struct mrvlietypes_wildcardssidparamset {
	struct mrvlietypesheader header;
	u8 MaxSsidlength;
	u8 ssid[1];
} __attribute__ ((packed));

struct chanscanmode {
#ifdef __BIG_ENDIAN_BITFIELD
	u8 reserved_2_7:6;
	u8 disablechanfilt:1;
	u8 passivescan:1;
#else
	u8 passivescan:1;
	u8 disablechanfilt:1;
	u8 reserved_2_7:6;
#endif
} __attribute__ ((packed));

struct chanscanparamset {
	u8 radiotype;
	u8 channumber;
	struct chanscanmode chanscanmode;
	__le16 minscantime;
	__le16 maxscantime;
} __attribute__ ((packed));

struct mrvlietypes_chanlistparamset {
	struct mrvlietypesheader header;
	struct chanscanparamset chanscanparam[1];
} __attribute__ ((packed));

struct cfparamset {
	u8 cfpcnt;
	u8 cfpperiod;
	__le16 cfpmaxduration;
	__le16 cfpdurationremaining;
} __attribute__ ((packed));

struct ibssparamset {
	__le16 atimwindow;
} __attribute__ ((packed));

struct mrvlietypes_ssparamset {
	struct mrvlietypesheader header;
	union {
		struct cfparamset cfparamset[1];
		struct ibssparamset ibssparamset[1];
	} cf_ibss;
} __attribute__ ((packed));

struct fhparamset {
	__le16 dwelltime;
	u8 hopset;
	u8 hoppattern;
	u8 hopindex;
} __attribute__ ((packed));

struct dsparamset {
	u8 currentchan;
} __attribute__ ((packed));

struct mrvlietypes_phyparamset {
	struct mrvlietypesheader header;
	union {
		struct fhparamset fhparamset[1];
		struct dsparamset dsparamset[1];
	} fh_ds;
} __attribute__ ((packed));

struct mrvlietypes_rsnparamset {
	struct mrvlietypesheader header;
	u8 rsnie[1];
} __attribute__ ((packed));

struct mrvlietypes_tsftimestamp {
	struct mrvlietypesheader header;
	__le64 tsftable[1];
} __attribute__ ((packed));

/**  Local Power capability */
struct mrvlietypes_powercapability {
	struct mrvlietypesheader header;
	s8 minpower;
	s8 maxpower;
} __attribute__ ((packed));

/* used in CMD_802_11_SUBSCRIBE_EVENT for SNR, RSSI and Failure */
struct mrvlietypes_thresholds {
	struct mrvlietypesheader header;
	u8 value;
	u8 freq;
} __attribute__ ((packed));

struct mrvlietypes_beaconsmissed {
	struct mrvlietypesheader header;
	u8 beaconmissed;
	u8 reserved;
} __attribute__ ((packed));

struct mrvlietypes_numprobes {
	struct mrvlietypesheader header;
	__le16 numprobes;
} __attribute__ ((packed));

struct mrvlietypes_bcastprobe {
	struct mrvlietypesheader header;
	__le16 bcastprobe;
} __attribute__ ((packed));

struct mrvlietypes_numssidprobe {
	struct mrvlietypesheader header;
	__le16 numssidprobe;
} __attribute__ ((packed));

struct led_pin {
	u8 led;
	u8 pin;
} __attribute__ ((packed));

struct mrvlietypes_ledgpio {
	struct mrvlietypesheader header;
	struct led_pin ledpin[1];
} __attribute__ ((packed));

#endif
