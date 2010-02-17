/**
  * This file contains definition for IOCTL call.
  */
#ifndef	_LBS_WEXT_H_
#define	_LBS_WEXT_H_

void lbs_send_disconnect_notification(struct lbs_private *priv);
void lbs_send_mic_failureevent(struct lbs_private *priv, u32 event);

struct chan_freq_power *lbs_find_cfp_by_band_and_channel(
	struct lbs_private *priv,
	u8 band,
	u16 channel);

extern struct iw_handler_def lbs_handler_def;

#endif
