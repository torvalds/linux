/**
  * This file contains definition for IOCTL call.
  */
#ifndef	_WLAN_WEXT_H_
#define	_WLAN_WEXT_H_

#define SUBCMD_OFFSET			4
#define SUBCMD_DATA(x)			*((int *)(x->u.name + SUBCMD_OFFSET))

/** PRIVATE CMD ID */
#define	WLANIOCTL			SIOCIWFIRSTPRIV

#define WLANSETWPAIE			(WLANIOCTL + 0)

#define WLAN_SETINT_GETINT		(WLANIOCTL + 7)
#define WLANNF					1
#define WLANRSSI				2
#define WLANENABLE11D				5
#define WLANADHOCGRATE				6
#define WLAN_SUBCMD_SET_PRESCAN			11

#define WLAN_SETNONE_GETNONE	        (WLANIOCTL + 8)
#define WLANDEAUTH                  		1
#define WLANRADIOON                 		2
#define WLANRADIOOFF                		3
#define WLANREMOVEADHOCAES          		4
#define WLANADHOCSTOP               		5
#define WLANCIPHERTEST              		6
#define WLANCRYPTOTEST				7

#define WLANWLANIDLEON				10
#define WLANWLANIDLEOFF				11
#define WLAN_SUBCMD_BT_RESET			13
#define WLAN_SUBCMD_FWT_RESET			14

#define WLANGETLOG                  	(WLANIOCTL + 9)
#define GETLOG_BUFSIZE  300

#define WLANSCAN_TYPE			(WLANIOCTL + 11)

#define WLAN_SETNONE_GETONEINT		(WLANIOCTL + 15)
#define WLANGETREGION				1
#define WLAN_GET_LISTEN_INTERVAL		2
#define WLAN_GET_MULTIPLE_DTIM			3
#define WLAN_GET_TX_RATE			4
#define	WLANGETBCNAVG				5

#define WLAN_GET_LINKMODE			6
#define WLAN_GET_RADIOMODE			7
#define WLAN_GET_DEBUGMODE			8
#define WLAN_SUBCMD_FWT_CLEANUP			15
#define WLAN_SUBCMD_FWT_TIME			16
#define WLAN_SUBCMD_MESH_GET_TTL		17

#define WLANREGCFRDWR			(WLANIOCTL + 18)

#define WLAN_SETNONE_GETTWELVE_CHAR (WLANIOCTL + 19)
#define WLAN_SUBCMD_GETRXANTENNA    1
#define WLAN_SUBCMD_GETTXANTENNA    2
#define WLAN_GET_TSF                3

#define WLAN_SETNONE_GETWORDCHAR	(WLANIOCTL + 21)
#define WLANGETADHOCAES				1

#define WLAN_SETONEINT_GETONEINT	(WLANIOCTL + 23)
#define WLAN_BEACON_INTERVAL			1
#define	WLAN_LISTENINTRVL			4

#define WLAN_TXCONTROL				6
#define WLAN_NULLPKTINTERVAL			7

#define WLAN_SETONEINT_GETNONE		(WLANIOCTL + 24)
#define WLAN_SUBCMD_SETRXANTENNA		1
#define WLAN_SUBCMD_SETTXANTENNA		2
#define WLANSETAUTHALG				5
#define WLANSET8021XAUTHALG			6
#define WLANSETENCRYPTIONMODE			7
#define WLANSETREGION				8
#define WLAN_SET_LISTEN_INTERVAL		9

#define WLAN_SET_MULTIPLE_DTIM			10
#define WLAN_SET_ATIM_WINDOW			11
#define WLANSETBCNAVG				13
#define WLANSETDATAAVG				14
#define WLAN_SET_LINKMODE			15
#define WLAN_SET_RADIOMODE			16
#define WLAN_SET_DEBUGMODE			17
#define WLAN_SUBCMD_MESH_SET_TTL		18

#define WLAN_SET128CHAR_GET128CHAR	(WLANIOCTL + 25)
#define WLANSCAN_MODE				6

#define WLAN_GET_ADHOC_STATUS			9

#define WLAN_SUBCMD_BT_ADD			18
#define WLAN_SUBCMD_BT_DEL   			19
#define WLAN_SUBCMD_BT_LIST			20
#define WLAN_SUBCMD_FWT_ADD				21
#define WLAN_SUBCMD_FWT_DEL   		22
#define WLAN_SUBCMD_FWT_LOOKUP		23
#define WLAN_SUBCMD_FWT_LIST_NEIGHBOR			24
#define WLAN_SUBCMD_FWT_LIST			25
#define WLAN_SUBCMD_FWT_LIST_ROUTE			26

#define WLAN_SET_GET_SIXTEEN_INT       (WLANIOCTL + 29)
#define WLAN_TPCCFG                             1
#define WLAN_POWERCFG                           2

#define WLAN_AUTO_FREQ_SET			3
#define WLAN_AUTO_FREQ_GET			4
#define WLAN_LED_GPIO_CTRL			5
#define WLAN_SCANPROBES 			6
#define	WLAN_ADAPT_RATESET			8
#define	WLAN_INACTIVITY_TIMEOUT			9
#define WLANSNR					10
#define WLAN_GET_RATE				11
#define	WLAN_GET_RXINFO				12

#define WLANCMD52RDWR			(WLANIOCTL + 30)
#define WLANCMD53RDWR			(WLANIOCTL + 31)
#define CMD53BUFLEN				32

#define	REG_MAC					0x19
#define	REG_BBP					0x1a
#define	REG_RF					0x1b
#define	REG_EEPROM				0x59
#define WLAN_LINKMODE_802_3			0
#define WLAN_LINKMODE_802_11			2
#define WLAN_RADIOMODE_NONE    			0
#define WLAN_RADIOMODE_RADIOTAP			2

/** wlan_ioctl_regrdwr */
struct wlan_ioctl_regrdwr {
	/** Which register to access */
	u16 whichreg;
	/** Read or Write */
	u16 action;
	u32 offset;
	u16 NOB;
	u32 value;
};

extern struct iw_handler_def libertas_handler_def;
int libertas_do_ioctl(struct net_device *dev, struct ifreq *req, int i);
int wlan_radio_ioctl(wlan_private * priv, u8 option);

#endif				/* _WLAN_WEXT_H_ */
