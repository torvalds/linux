/* Copyright (C) 2006, Red Hat, Inc. */

#ifndef _LBS_ASSOC_H_
#define _LBS_ASSOC_H_

#include "dev.h"

void lbs_association_worker(struct work_struct *work);
struct assoc_request *lbs_get_association_request(struct lbs_private *priv);

int lbs_adhoc_stop(struct lbs_private *priv);

int lbs_cmd_80211_deauthenticate(struct lbs_private *priv,
				 u8 bssid[ETH_ALEN], u16 reason);

#endif /* _LBS_ASSOC_H */
