/**
  * Interface for the wlan infrastructure and adhoc join routines
  *
  * Driver interface functions and type declarations for the join module
  *   implemented in wlan_join.c.  Process all start/join requests for
  *   both adhoc and infrastructure networks
  */
#ifndef _WLAN_JOIN_H
#define _WLAN_JOIN_H

#include "defs.h"
#include "dev.h"

struct cmd_ds_command;
int libertas_cmd_80211_authenticate(wlan_private * priv,
					struct cmd_ds_command *cmd,
					void *pdata_buf);
int libertas_cmd_80211_ad_hoc_join(wlan_private * priv,
				       struct cmd_ds_command *cmd,
				       void *pdata_buf);
int libertas_cmd_80211_ad_hoc_stop(wlan_private * priv,
				       struct cmd_ds_command *cmd);
int libertas_cmd_80211_ad_hoc_start(wlan_private * priv,
					struct cmd_ds_command *cmd,
					void *pdata_buf);
int libertas_cmd_80211_deauthenticate(wlan_private * priv,
					  struct cmd_ds_command *cmd);
int libertas_cmd_80211_associate(wlan_private * priv,
				     struct cmd_ds_command *cmd,
				     void *pdata_buf);

int libertas_ret_80211_ad_hoc_start(wlan_private * priv,
					struct cmd_ds_command *resp);
int libertas_ret_80211_ad_hoc_stop(wlan_private * priv,
				       struct cmd_ds_command *resp);
int libertas_ret_80211_disassociate(wlan_private * priv,
					struct cmd_ds_command *resp);
int libertas_ret_80211_associate(wlan_private * priv,
				     struct cmd_ds_command *resp);

int libertas_start_adhoc_network(wlan_private * priv,
			     struct assoc_request * assoc_req);
int libertas_join_adhoc_network(wlan_private * priv,
				struct assoc_request * assoc_req);
int libertas_stop_adhoc_network(wlan_private * priv);

int libertas_send_deauthentication(wlan_private * priv);

int wlan_associate(wlan_private * priv, struct assoc_request * assoc_req);

void libertas_unset_basic_rate_flags(u8 * rates, size_t len);

#endif
