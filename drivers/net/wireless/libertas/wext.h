/**
  * This file contains definition for IOCTL call.
  */
#ifndef	_WLAN_WEXT_H_
#define	_WLAN_WEXT_H_

#define SUBCMD_OFFSET			4
#define SUBCMD_DATA(x)			*((int *)(x->u.name + SUBCMD_OFFSET))

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

#define WLAN_LINKMODE_802_3			0
#define WLAN_LINKMODE_802_11			2
#define WLAN_RADIOMODE_NONE			0
#define WLAN_RADIOMODE_RADIOTAP			2

extern struct iw_handler_def libertas_handler_def;
extern struct iw_handler_def mesh_handler_def;
int wlan_radio_ioctl(wlan_private * priv, u8 option);

#endif				/* _WLAN_WEXT_H_ */
