#ifndef DUAL_ANT_BWCS_H
#define DUAL_ANT_BWCS_H
typedef unsigned long	DWORD;
typedef unsigned short	USHORT;
typedef unsigned char	UCHAR;
typedef unsigned char	BYTE;
typedef unsigned char	BOOL;
typedef unsigned char	BOOLEAN;
typedef signed int INT;
typedef void * PNET_DEV;
#define DBGPRINT(Level, Fmt)    printk Fmt

#define RT_WLAN_EVENT_CUSTOM							0x01
#define RT_WLAN_EVENT_CGIWAP							0x02
#define RT_WLAN_EVENT_ASSOC_REQ_IE						0x03
#define RT_WLAN_EVENT_SCAN								0x04
#define RT_WLAN_EVENT_EXPIRED							0x05
#define RT_WLAN_EVENT_SHOWPIN							0x06
#define RT_WLAN_EVENT_PIN							0x07



/* WiFi Driver events to bwcs */
#define WIFI_EVENT_WIFI_OPEN		(DWORD)(1<<0)
#define WIFI_EVENT_WIFI_CLOSE		(DWORD)(1<<1)
#define WIFI_EVENT_CONN_NEW			(DWORD)(1<<2)
#define WIFI_EVENT_CONN_DEL			(DWORD)(1<<3)
#define WIFI_EVENT_STA_CONN_NEW		(DWORD)(1<<4)
#define WIFI_EVENT_STA_CONN_DEL		(DWORD)(1<<5)
#define WIFI_EVENT_P2P_GO_CONN_NEW	(DWORD)(1<<6)
#define WIFI_EVENT_P2P_GO_CONN_DEL	(DWORD)(1<<7)
#define WIFI_EVENT_P2P_GC_CONN_NEW	(DWORD)(1<<8)
#define WIFI_EVENT_P2P_GC_CONN_DEL	(DWORD)(1<<9)
#define WIFI_EVENT_SOFTAP_CONN_NEW	(DWORD)(1<<10)
#define WIFI_EVENT_SOFTAP_CONN_DEL	(DWORD)(1<<11)

/* WiFi events from bwcs */
#define WIFI_ACTION_CUSTOM_1		0x1001
#define WIFI_ACTION_CUSTOM_2		0x1002
#define WIFI_ACTION_CUSTOM_3		0x1003
#define WIFI_ACTION_CUSTOM_4		0x1004

enum {
	WIFI_CONTYPE_STA = 0,
	WIFI_CONTYPE_P2P,
	WIFI_CONTYPE_SOFTAP,
	WIFI_CONTYPE_MAX
};

enum {
	WIFI_EVENT_PARAM_CH = 0,
	WIFI_EVENT_PARAM_BW,
	WIFI_EVENT_PARAM_FREQ,	/*no used*/
	WIFI_EVENT_PARAM_MAX
};

/* BW -- The Bandwidth of WIFI_EVENT_PARAM_BW*/
#define WIFI_BW_20		0
#define WIFI_BW_40		1
#define WIFI_BW_80		2
#define WIFI_BW_10		4	/* 802.11j has 10MHz. This definition is for internal usage. doesn't fill in the IE or other field. */


#define BWCS_NAME_MAX_LEN			32
#define BWCS_NAME					"BWCS"

typedef struct __BWCS_WIFI {
    char	name[BWCS_NAME_MAX_LEN];		/*BWCS*/
    USHORT	event;
    UCHAR	para[WIFI_EVENT_PARAM_MAX];
} BWCS_WIFI;

void wifi2bwcs_connection_event_ind_handler (P_GLUE_INFO_T prGlueInfo, USHORT event);

/*This API is only used by 7601 Wifi driver*/
int RtmpWirelessChannelNotify(
	IN	PNET_DEV				pNetDev,
	IN	UINT32					eventType,
	IN	INT 					flags,
	IN	PUCHAR					pSrcMac,
	IN	PUCHAR					pData,
	IN	UINT32					dataLen);

#endif  //DUAL_ANT_BWCS_H
