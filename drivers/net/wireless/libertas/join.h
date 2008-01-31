/**
  * Interface for the wlan infrastructure and adhoc join routines
  *
  * Driver interface functions and type declarations for the join module
  *   implemented in join.c.  Process all start/join requests for
  *   both adhoc and infrastructure networks
  */
#ifndef _LBS_JOIN_H
#define _LBS_JOIN_H

#include "defs.h"
#include "dev.h"

struct cmd_ds_command;
int lbs_cmd_80211_authenticate(struct lbs_private *priv,
					struct cmd_ds_command *cmd,
					void *pdata_buf);
int lbs_cmd_80211_ad_hoc_join(struct lbs_private *priv,
				       struct cmd_ds_command *cmd,
				       void *pdata_buf);
int lbs_cmd_80211_ad_hoc_stop(struct lbs_private *priv,
				       struct cmd_ds_command *cmd);
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
int lbs_ret_80211_ad_hoc_stop(struct lbs_private *priv,
				       struct cmd_ds_command *resp);
int lbs_ret_80211_disassociate(struct lbs_private *priv,
					struct cmd_ds_command *resp);
int lbs_ret_80211_associate(struct lbs_private *priv,
				     struct cmd_ds_command *resp);

int lbs_start_adhoc_network(struct lbs_private *priv,
			     struct assoc_request * assoc_req);
int lbs_join_adhoc_network(struct lbs_private *priv,
				struct assoc_request * assoc_req);
int lbs_stop_adhoc_network(struct lbs_private *priv);

int lbs_send_deauthentication(struct lbs_private *priv);

int lbs_associate(struct lbs_private *priv, struct assoc_request *assoc_req);

void lbs_unset_basic_rate_flags(u8 *rates, size_t len);

#endif
