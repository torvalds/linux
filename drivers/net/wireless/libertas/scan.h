/**
  * Interface for the wlan network scan routines
  *
  * Driver interface functions and type declarations for the scan module
  * implemented in scan.c.
  */
#ifndef _LBS_SCAN_H
#define _LBS_SCAN_H

#include <net/iw_handler.h>

#define MAX_NETWORK_COUNT 128

/**
 *  @brief Maximum number of channels that can be sent in a setuserscan ioctl
 */
#define LBS_IOCTL_USER_SCAN_CHAN_MAX  50

int lbs_ssid_cmp(u8 *ssid1, u8 ssid1_len, u8 *ssid2, u8 ssid2_len);

int lbs_send_specific_ssid_scan(struct lbs_private *priv, u8 *ssid,
				u8 ssid_len);

int lbs_get_scan(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra);
int lbs_set_scan(struct net_device *dev, struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra);

int lbs_scan_networks(struct lbs_private *priv, int full_scan);

void lbs_scan_worker(struct work_struct *work);

#endif
