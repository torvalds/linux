/**
  * This file contains definition for IOCTL call.
  */
#ifndef	_WLAN_WEXT_H_
#define	_WLAN_WEXT_H_

#define SUBCMD_OFFSET			4
#define SUBCMD_DATA(x)			*((int *)(x->u.name + SUBCMD_OFFSET))

/** PRIVATE CMD ID */
#define	WLANIOCTL			SIOCIWFIRSTPRIV

#define WLAN_SETNONE_GETNONE	        (WLANIOCTL + 8)
#define WLAN_SUBCMD_BT_RESET			13
#define WLAN_SUBCMD_FWT_RESET			14

#define WLAN_SETNONE_GETONEINT		(WLANIOCTL + 15)
#define WLANGETREGION				1

#define WLAN_SUBCMD_FWT_CLEANUP			15
#define WLAN_SUBCMD_FWT_TIME			16
#define WLAN_SUBCMD_MESH_GET_TTL		17
#define WLAN_SUBCMD_BT_GET_INVERT		18

#define WLAN_SETONEINT_GETNONE		(WLANIOCTL + 24)
#define WLANSETREGION				8
#define WLAN_SUBCMD_MESH_SET_TTL		18
#define WLAN_SUBCMD_BT_SET_INVERT		19

#define WLAN_SET128CHAR_GET128CHAR	(WLANIOCTL + 25)
#define WLAN_SUBCMD_BT_ADD			18
#define WLAN_SUBCMD_BT_DEL   			19
#define WLAN_SUBCMD_BT_LIST			20
#define WLAN_SUBCMD_FWT_ADD			21
#define WLAN_SUBCMD_FWT_DEL   			22
#define WLAN_SUBCMD_FWT_LOOKUP			23
#define WLAN_SUBCMD_FWT_LIST_NEIGHBOR		24
#define WLAN_SUBCMD_FWT_LIST			25
#define WLAN_SUBCMD_FWT_LIST_ROUTE		26

#define WLAN_SET_GET_SIXTEEN_INT       (WLANIOCTL + 29)
#define WLAN_LED_GPIO_CTRL			5

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
extern struct iw_handler_def mesh_handler_def;
int libertas_do_ioctl(struct net_device *dev, struct ifreq *req, int i);
int wlan_radio_ioctl(wlan_private * priv, u8 option);

#endif				/* _WLAN_WEXT_H_ */
