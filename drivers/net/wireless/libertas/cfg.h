#ifndef __LBS_CFG80211_H__
#define __LBS_CFG80211_H__

#include "dev.h"

struct wireless_dev *lbs_cfg_alloc(struct device *dev);
int lbs_cfg_register(struct lbs_private *priv);
void lbs_cfg_free(struct lbs_private *priv);

int lbs_send_specific_ssid_scan(struct lbs_private *priv, u8 *ssid,
	u8 ssid_len);
int lbs_scan_networks(struct lbs_private *priv, int full_scan);
void lbs_cfg_scan_worker(struct work_struct *work);


#endif
