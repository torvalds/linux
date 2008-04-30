/* Copyright (C) 2006, Red Hat, Inc. */

#ifndef _LBS_ASSOC_H_
#define _LBS_ASSOC_H_

#include "dev.h"

void lbs_association_worker(struct work_struct *work);
struct assoc_request *lbs_get_association_request(struct lbs_private *priv);

struct cmd_ds_command;
int lbs_cmd_80211_authenticate(struct lbs_private *priv,
					struct cmd_ds_command *cmd,
					void *pdata_buf);
int lbs_cmd_80211_ad_hoc_join(struct lbs_private *priv,
				       struct cmd_ds_command *cmd,
				       void *pdata_buf);
int lbs_cmd_80211_ad_hoc_stop(struct cmd_ds_command *cmd);
int lbs_cmd_80211_ad_hoc_start(struct lbs_private *priv,
					struct cmd_ds_command *cmd,
					void *pdata_buf);
int lbs_cmd_80211_deauthenticate(struct lbs_private *priv,
					  struct cmd_ds_command *cmd);
int lbs_cmd_80211_associate(struct lbs_private *priv,
				     struct cmd_ds_command *cmd,
				     void *pdata_buf);

int lbs_ret_80211_ad_hoc_start(struct lbs_private *priv,
					struct cmd_ds_command *resp);
int lbs_ret_80211_ad_hoc_stop(struct lbs_private *priv);
int lbs_ret_80211_disassociate(struct lbs_private *priv);
int lbs_ret_80211_associate(struct lbs_private *priv,
				     struct cmd_ds_command *resp);

int lbs_stop_adhoc_network(struct lbs_private *priv);

int lbs_send_deauthentication(struct lbs_private *priv);

#endif /* _LBS_ASSOC_H */
