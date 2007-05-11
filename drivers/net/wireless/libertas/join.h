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

struct cmd_ds_command;
extern int libertas_cmd_80211_authenticate(wlan_private * priv,
					struct cmd_ds_command *cmd,
					void *pdata_buf);
extern int libertas_cmd_80211_ad_hoc_join(wlan_private * priv,
				       struct cmd_ds_command *cmd,
				       void *pdata_buf);
extern int libertas_cmd_80211_ad_hoc_stop(wlan_private * priv,
				       struct cmd_ds_command *cmd);
extern int libertas_cmd_80211_ad_hoc_start(wlan_private * priv,
					struct cmd_ds_command *cmd,
					void *pssid);
extern int libertas_cmd_80211_deauthenticate(wlan_private * priv,
					  struct cmd_ds_command *cmd);
extern int libertas_cmd_80211_associate(wlan_private * priv,
				     struct cmd_ds_command *cmd,
				     void *pdata_buf);

extern int libertas_ret_80211_ad_hoc_start(wlan_private * priv,
					struct cmd_ds_command *resp);
extern int libertas_ret_80211_ad_hoc_stop(wlan_private * priv,
				       struct cmd_ds_command *resp);
extern int libertas_ret_80211_disassociate(wlan_private * priv,
					struct cmd_ds_command *resp);
extern int libertas_ret_80211_associate(wlan_private * priv,
				     struct cmd_ds_command *resp);

extern int libertas_reassociation_thread(void *data);

struct WLAN_802_11_SSID;
struct bss_descriptor;

extern int libertas_start_adhoc_network(wlan_private * priv,
			     struct WLAN_802_11_SSID *adhocssid);
extern int libertas_join_adhoc_network(wlan_private * priv, struct bss_descriptor *pbssdesc);
extern int libertas_stop_adhoc_network(wlan_private * priv);

extern int libertas_send_deauthentication(wlan_private * priv);
extern int libertas_send_deauth(wlan_private * priv);

extern int libertas_do_adhocstop_ioctl(wlan_private * priv);

int wlan_associate(wlan_private * priv, struct bss_descriptor * pbssdesc);

#endif
