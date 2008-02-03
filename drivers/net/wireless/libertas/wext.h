/**
  * This file contains definition for IOCTL call.
  */
#ifndef	_LBS_WEXT_H_
#define	_LBS_WEXT_H_

/** lbs_ioctl_regrdwr */
struct lbs_ioctl_regrdwr {
	/** Which register to access */
	u16 whichreg;
	/** Read or Write */
	u16 action;
	u32 offset;
	u16 NOB;
	u32 value;
};

#define LBS_MONITOR_OFF			0

extern struct iw_handler_def lbs_handler_def;
extern struct iw_handler_def mesh_handler_def;

#endif
