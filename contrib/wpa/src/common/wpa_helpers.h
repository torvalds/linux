/*
 * wpa_supplicant ctrl_iface helpers
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2012, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_HELPERS_H
#define WPA_HELPERS_H

int wpa_command(const char *ifname, const char *cmd);
int wpa_command_resp(const char *ifname, const char *cmd,
		     char *resp, size_t resp_size);
int get_wpa_status(const char *ifname, const char *field, char *obuf,
		   size_t obuf_size);

struct wpa_ctrl * open_wpa_mon(const char *ifname);
int wait_ip_addr(const char *ifname, int timeout);
int get_wpa_cli_event(struct wpa_ctrl *mon,
		      const char *event, char *buf, size_t buf_size);
int get_wpa_cli_event2(struct wpa_ctrl *mon,
		       const char *event, const char *event2,
		       char *buf, size_t buf_size);

int add_network(const char *ifname);
int set_network(const char *ifname, int id, const char *field,
		const char *value);
int set_network_quoted(const char *ifname, int id, const char *field,
		       const char *value);
int add_cred(const char *ifname);
int set_cred(const char *ifname, int id, const char *field, const char *value);
int set_cred_quoted(const char *ifname, int id, const char *field,
		    const char *value);

#endif /* WPA_HELPERS_H */
