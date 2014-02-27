
#ifndef IW_PRIV_H
#define IW_PRIV_H

#define TRUE 		1
#define FALSE 	0

/** PRIVATE CMD ID */
#define	WLANIOCTL			0x8BE0

#define WLANDEEPSLEEP							(WLANIOCTL + 1)
#define WLAN_SETONEINT_GETONEINT	(WLANIOCTL + 2)
#define 	WLAN_AUTODEEPSLEEP					1
#define WLAN_SUSPEND_RESUME				(WLANIOCTL + 3)
#define 	WLAN_SUSPEND								1
#define   WLAN_RESUME									2

#endif /* IW_PRIV_H */
