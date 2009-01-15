#ifndef __GL_80211_H__
#define __GL_80211_H__

#include <linux/types.h>

/****************** CONSTANT AND MACRO SECTION ******************************/

/* BSS Type */
enum {
    WLAN_BSSTYPE_INFRASTRUCTURE         = 0,
    WLAN_BSSTYPE_INDEPENDENT,
    WLAN_BSSTYPE_ANY_BSS,
};



/* Preamble_Type, see <SFS-802.11G-MIB-203> */
typedef enum preamble_type {
    WLAN_PREAMBLE_TYPE_SHORT,
    WLAN_PREAMBLE_TYPE_LONG,
}    preamble_type_e;


/* Slot_Time_Type, see <SFS-802.11G-MIB-208> */
typedef enum slot_time_type {
    WLAN_SLOT_TIME_TYPE_LONG,
    WLAN_SLOT_TIME_TYPE_SHORT,
}    slot_time_type_e;

/*--------------------------------------------------------------------------*/
/* Encryption Mode */
typedef enum {
    WEP_DISABLE                                         = 0,
    WEP_64,
    WEP_128,

    ENCRYPT_DISABLE,
    ENCRYPT_WEP,
    ENCRYPT_WEP_NOKEY,
    ENCRYPT_TKIP,
    ENCRYPT_TKIP_NOKEY,
    ENCRYPT_CCMP,
    ENCRYPT_CCMP_NOKEY,
}    encryption_mode_e;

typedef enum _WLAN_RADIO {
    WLAN_RADIO_ON,
    WLAN_RADIO_OFF,
    WLAN_RADIO_MAX, // not a real type, defined as an upper bound
} WLAN_RADIO;

typedef struct _WLAN_RADIO_STATUS {
	WLAN_RADIO HWStatus;
	WLAN_RADIO SWStatus;
} WLAN_RADIO_STATUS;

//----------------------------------------------------------------------------
// 20041021 1.1.81.1000 ybjiang
// add for radio notification
typedef
void (*RADIO_NOTIFICATION_HANDLER)(
	void *Data,
	void *RadioStatusBuffer,
	u32 RadioStatusBufferLen
	);

typedef struct _WLAN_RADIO_NOTIFICATION
{
    RADIO_NOTIFICATION_HANDLER RadioChangeHandler;
    void *Data;
} WLAN_RADIO_NOTIFICATION;

//----------------------------------------------------------------------------
// 20041102 1.1.91.1000 ybjiang
// add for OID_802_11_CUST_REGION_CAPABILITIES and OID_802_11_OID_REGION
typedef enum _WLAN_REGION_CODE
{
	WLAN_REGION_UNKNOWN,
	WLAN_REGION_EUROPE,
	WLAN_REGION_JAPAN,
	WLAN_REGION_USA,
	WLAN_REGION_FRANCE,
	WLAN_REGION_SPAIN,
	WLAN_REGION_ISRAEL,
	WLAN_REGION_MAX, // not a real type, defined as an upper bound
} WLAN_REGION_CODE;

#define REGION_NAME_MAX_LENGTH   256

typedef struct _WLAN_REGION_CHANNELS
{
	u32 Length;
	u32 NameLength;
	u8 Name[REGION_NAME_MAX_LENGTH];
	WLAN_REGION_CODE Code;
	u32 Frequency[1];
} WLAN_REGION_CHANNELS;

typedef struct _WLAN_REGION_CAPABILITIES
{
	u32 NumberOfItems;
	WLAN_REGION_CHANNELS Region[1];
} WLAN_REGION_CAPABILITIES;

typedef struct _region_name_map {
	WLAN_REGION_CODE region;
	u8 *name;
	u32 *channels;
} region_name_map;

/*--------------------------------------------------------------------------*/
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

// TODO: 0627 kevin
#define MIC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7]
#define MICSTR "%02X %02X %02X %02X %02X %02X %02X %02X"

#define MICKEY2STR(a)   MIC2STR(a)
#define MICKEYSTR       MICSTR


#endif /* __GL_80211_H__ */
/*** end of file ***/


