/**
  * This file contains definition for IOCTL call.
  */
#ifndef	_LBS_WEXT_H_
#define	_LBS_WEXT_H_

void lbs_send_iwevcustom_event(struct lbs_private *priv, s8 *str);

extern struct iw_handler_def lbs_handler_def;
extern struct iw_handler_def mesh_handler_def;

#endif
