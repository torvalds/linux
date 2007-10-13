/**
  * This file contains definition for IOCTL call.
  */
#ifndef	_WLAN_WEXT_H_
#define	_WLAN_WEXT_H_

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

#define WLAN_MONITOR_OFF			0

extern struct iw_handler_def libertas_handler_def;
extern struct iw_handler_def mesh_handler_def;

#endif				/* _WLAN_WEXT_H_ */
