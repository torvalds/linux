/*
 * WPA Supplicant - command line interface for wpa_supplicant daemon
 * Copyright (c) 2004-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#ifdef CONFIG_CTRL_IFACE

#ifdef CONFIG_CTRL_IFACE_UNIX
#include <dirent.h>
#endif /* CONFIG_CTRL_IFACE_UNIX */

#include "common/cli.h"
#include "common/wpa_ctrl.h"
#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/edit.h"
#include "utils/list.h"
#include "common/version.h"
#include "common/ieee802_11_defs.h"
#ifdef ANDROID
#include <cutils/properties.h>
#endif /* ANDROID */


static const char *const wpa_cli_version =
"wpa_cli v" VERSION_STR "\n"
"Copyright (c) 2004-2018, Jouni Malinen <j@w1.fi> and contributors";

#define VENDOR_ELEM_FRAME_ID \
	"  0: Probe Req (P2P), 1: Probe Resp (P2P) , 2: Probe Resp (GO), " \
	"3: Beacon (GO), 4: PD Req, 5: PD Resp, 6: GO Neg Req, " \
	"7: GO Neg Resp, 8: GO Neg Conf, 9: Inv Req, 10: Inv Resp, " \
	"11: Assoc Req (P2P), 12: Assoc Resp (P2P)"

static struct wpa_ctrl *ctrl_conn;
static struct wpa_ctrl *mon_conn;
static int wpa_cli_quit = 0;
static int wpa_cli_attached = 0;
static int wpa_cli_connected = -1;
static int wpa_cli_last_id = 0;
#ifndef CONFIG_CTRL_IFACE_DIR
#define CONFIG_CTRL_IFACE_DIR "/var/run/wpa_supplicant"
#endif /* CONFIG_CTRL_IFACE_DIR */
static const char *ctrl_iface_dir = CONFIG_CTRL_IFACE_DIR;
static const char *client_socket_dir = NULL;
static char *ctrl_ifname = NULL;
static const char *pid_file = NULL;
static const char *action_file = NULL;
static int ping_interval = 5;
static int interactive = 0;
static char *ifname_prefix = NULL;

static DEFINE_DL_LIST(bsses); /* struct cli_txt_entry */
static DEFINE_DL_LIST(p2p_peers); /* struct cli_txt_entry */
static DEFINE_DL_LIST(p2p_groups); /* struct cli_txt_entry */
static DEFINE_DL_LIST(ifnames); /* struct cli_txt_entry */
static DEFINE_DL_LIST(networks); /* struct cli_txt_entry */
static DEFINE_DL_LIST(creds); /* struct cli_txt_entry */
#ifdef CONFIG_AP
static DEFINE_DL_LIST(stations); /* struct cli_txt_entry */
#endif /* CONFIG_AP */


static void print_help(const char *cmd);
static void wpa_cli_mon_receive(int sock, void *eloop_ctx, void *sock_ctx);
static void wpa_cli_close_connection(void);
static char * wpa_cli_get_default_ifname(void);
static char ** wpa_list_cmd_list(void);
static void update_creds(struct wpa_ctrl *ctrl);
static void update_networks(struct wpa_ctrl *ctrl);
static void update_stations(struct wpa_ctrl *ctrl);


static void usage(void)
{
	printf("wpa_cli [-p<path to ctrl sockets>] [-i<ifname>] [-hvB] "
	       "[-a<action file>] \\\n"
	       "        [-P<pid file>] [-g<global ctrl>] [-G<ping interval>] "
	       "\\\n"
	       "        [-s<wpa_client_socket_file_path>] "
	       "[command..]\n"
	       "  -h = help (show this usage text)\n"
	       "  -v = shown version information\n"
	       "  -a = run in daemon mode executing the action file based on "
	       "events from\n"
	       "       wpa_supplicant\n"
	       "  -B = run a daemon in the background\n"
	       "  default path: " CONFIG_CTRL_IFACE_DIR "\n"
	       "  default interface: first interface found in socket path\n");
	print_help(NULL);
}


static int wpa_cli_show_event(const char *event)
{
	const char *start;

	start = os_strchr(event, '>');
	if (start == NULL)
		return 1;

	start++;
	/*
	 * Skip BSS added/removed events since they can be relatively frequent
	 * and are likely of not much use for an interactive user.
	 */
	if (str_starts(start, WPA_EVENT_BSS_ADDED) ||
	    str_starts(start, WPA_EVENT_BSS_REMOVED))
		return 0;

	return 1;
}


static int wpa_cli_open_connection(const char *ifname, int attach)
{
#if defined(CONFIG_CTRL_IFACE_UDP) || defined(CONFIG_CTRL_IFACE_NAMED_PIPE)
	ctrl_conn = wpa_ctrl_open(ifname);
	if (ctrl_conn == NULL)
		return -1;

	if (attach && interactive)
		mon_conn = wpa_ctrl_open(ifname);
	else
		mon_conn = NULL;
#else /* CONFIG_CTRL_IFACE_UDP || CONFIG_CTRL_IFACE_NAMED_PIPE */
	char *cfile = NULL;
	int flen, res;

	if (ifname == NULL)
		return -1;

#ifdef ANDROID
	if (access(ctrl_iface_dir, F_OK) < 0) {
		cfile = os_strdup(ifname);
		if (cfile == NULL)
			return -1;
	}
#endif /* ANDROID */

	if (client_socket_dir && client_socket_dir[0] &&
	    access(client_socket_dir, F_OK) < 0) {
		perror(client_socket_dir);
		os_free(cfile);
		return -1;
	}

	if (cfile == NULL) {
		flen = os_strlen(ctrl_iface_dir) + os_strlen(ifname) + 2;
		cfile = os_malloc(flen);
		if (cfile == NULL)
			return -1;
		res = os_snprintf(cfile, flen, "%s/%s", ctrl_iface_dir,
				  ifname);
		if (os_snprintf_error(flen, res)) {
			os_free(cfile);
			return -1;
		}
	}

	ctrl_conn = wpa_ctrl_open2(cfile, client_socket_dir);
	if (ctrl_conn == NULL) {
		os_free(cfile);
		return -1;
	}

	if (attach && interactive)
		mon_conn = wpa_ctrl_open2(cfile, client_socket_dir);
	else
		mon_conn = NULL;
	os_free(cfile);
#endif /* CONFIG_CTRL_IFACE_UDP || CONFIG_CTRL_IFACE_NAMED_PIPE */

	if (mon_conn) {
		if (wpa_ctrl_attach(mon_conn) == 0) {
			wpa_cli_attached = 1;
			if (interactive)
				eloop_register_read_sock(
					wpa_ctrl_get_fd(mon_conn),
					wpa_cli_mon_receive, NULL, NULL);
		} else {
			printf("Warning: Failed to attach to "
			       "wpa_supplicant.\n");
			wpa_cli_close_connection();
			return -1;
		}
	}

	return 0;
}


static void wpa_cli_close_connection(void)
{
	if (ctrl_conn == NULL)
		return;

	if (wpa_cli_attached) {
		wpa_ctrl_detach(interactive ? mon_conn : ctrl_conn);
		wpa_cli_attached = 0;
	}
	wpa_ctrl_close(ctrl_conn);
	ctrl_conn = NULL;
	if (mon_conn) {
		eloop_unregister_read_sock(wpa_ctrl_get_fd(mon_conn));
		wpa_ctrl_close(mon_conn);
		mon_conn = NULL;
	}
}


static void wpa_cli_msg_cb(char *msg, size_t len)
{
	printf("%s\n", msg);
}


static int _wpa_ctrl_command(struct wpa_ctrl *ctrl, const char *cmd, int print)
{
	char buf[4096];
	size_t len;
	int ret;

	if (ctrl_conn == NULL) {
		printf("Not connected to wpa_supplicant - command dropped.\n");
		return -1;
	}
	if (ifname_prefix) {
		os_snprintf(buf, sizeof(buf), "IFNAME=%s %s",
			    ifname_prefix, cmd);
		buf[sizeof(buf) - 1] = '\0';
		cmd = buf;
	}
	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, cmd, os_strlen(cmd), buf, &len,
			       wpa_cli_msg_cb);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0) {
		printf("'%s' command failed.\n", cmd);
		return -1;
	}
	if (print) {
		buf[len] = '\0';
		printf("%s", buf);
		if (interactive && len > 0 && buf[len - 1] != '\n')
			printf("\n");
	}
	return 0;
}


static int wpa_ctrl_command(struct wpa_ctrl *ctrl, const char *cmd)
{
	return _wpa_ctrl_command(ctrl, cmd, 1);
}


static int wpa_cli_cmd(struct wpa_ctrl *ctrl, const char *cmd, int min_args,
		       int argc, char *argv[])
{
	char buf[4096];
	if (argc < min_args) {
		printf("Invalid %s command - at least %d argument%s "
		       "required.\n", cmd, min_args,
		       min_args > 1 ? "s are" : " is");
		return -1;
	}
	if (write_cmd(buf, sizeof(buf), cmd, argc, argv) < 0)
		return -1;
	return wpa_ctrl_command(ctrl, buf);
}


static int wpa_cli_cmd_ifname(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "IFNAME");
}


static int wpa_cli_cmd_status(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc > 0 && os_strcmp(argv[0], "verbose") == 0)
		return wpa_ctrl_command(ctrl, "STATUS-VERBOSE");
	if (argc > 0 && os_strcmp(argv[0], "wps") == 0)
		return wpa_ctrl_command(ctrl, "STATUS-WPS");
	if (argc > 0 && os_strcmp(argv[0], "driver") == 0)
		return wpa_ctrl_command(ctrl, "STATUS-DRIVER");
#ifdef ANDROID
	if (argc > 0 && os_strcmp(argv[0], "no_events") == 0)
		return wpa_ctrl_command(ctrl, "STATUS-NO_EVENTS");
#endif /* ANDROID */
	return wpa_ctrl_command(ctrl, "STATUS");
}


static int wpa_cli_cmd_ping(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "PING");
}


static int wpa_cli_cmd_relog(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "RELOG");
}


static int wpa_cli_cmd_note(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "NOTE", 1, argc, argv);
}


static int wpa_cli_cmd_mib(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "MIB");
}


static int wpa_cli_cmd_pmksa(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "PMKSA");
}


static int wpa_cli_cmd_pmksa_flush(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "PMKSA_FLUSH");
}


#ifdef CONFIG_PMKSA_CACHE_EXTERNAL

static int wpa_cli_cmd_pmksa_get(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "PMKSA_GET", 1, argc, argv);
}


static int wpa_cli_cmd_pmksa_add(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "PMKSA_ADD", 8, argc, argv);
}


#ifdef CONFIG_MESH

static int wpa_cli_mesh_cmd_pmksa_get(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_cli_cmd(ctrl, "MESH_PMKSA_GET", 1, argc, argv);
}


static int wpa_cli_mesh_cmd_pmksa_add(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_cli_cmd(ctrl, "MESH_PMKSA_ADD", 4, argc, argv);
}

#endif /* CONFIG_MESH */
#endif /* CONFIG_PMKSA_CACHE_EXTERNAL */


static int wpa_cli_cmd_help(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	print_help(argc > 0 ? argv[0] : NULL);
	return 0;
}


static char ** wpa_cli_complete_help(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = wpa_list_cmd_list();
		break;
	}

	return res;
}


static int wpa_cli_cmd_license(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	printf("%s\n\n%s\n", wpa_cli_version, cli_full_license);
	return 0;
}


static int wpa_cli_cmd_quit(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	wpa_cli_quit = 1;
	if (interactive)
		eloop_terminate();
	return 0;
}


static int wpa_cli_cmd_set(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256];
	int res;

	if (argc == 1) {
		res = os_snprintf(cmd, sizeof(cmd), "SET %s ", argv[0]);
		if (os_snprintf_error(sizeof(cmd), res)) {
			printf("Too long SET command.\n");
			return -1;
		}
		return wpa_ctrl_command(ctrl, cmd);
	}

	return wpa_cli_cmd(ctrl, "SET", 2, argc, argv);
}


static char ** wpa_cli_complete_set(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	const char *fields[] = {
		/* runtime values */
		"EAPOL::heldPeriod", "EAPOL::authPeriod", "EAPOL::startPeriod",
		"EAPOL::maxStart", "dot11RSNAConfigPMKLifetime",
		"dot11RSNAConfigPMKReauthThreshold", "dot11RSNAConfigSATimeout",
		"wps_fragment_size", "wps_version_number", "ampdu",
		"tdls_testing", "tdls_disabled", "pno", "radio_disabled",
		"uapsd", "ps", "wifi_display", "bssid_filter", "disallow_aps",
		"no_keep_alive",
		/* global configuration parameters */
#ifdef CONFIG_CTRL_IFACE
		"ctrl_interface", "no_ctrl_interface", "ctrl_interface_group",
#endif /* CONFIG_CTRL_IFACE */
		"eapol_version", "ap_scan", "bgscan",
#ifdef CONFIG_MESH
		"user_mpm", "max_peer_links", "mesh_max_inactivity",
		"dot11RSNASAERetransPeriod",
#endif /* CONFIG_MESH */
		"disable_scan_offload", "fast_reauth", "opensc_engine_path",
		"pkcs11_engine_path", "pkcs11_module_path", "openssl_ciphers",
		"pcsc_reader", "pcsc_pin", "external_sim", "driver_param",
		"dot11RSNAConfigPMKLifetime",
		"dot11RSNAConfigPMKReauthThreshold",
		"dot11RSNAConfigSATimeout",
#ifndef CONFIG_NO_CONFIG_WRITE
		"update_config",
#endif /* CONFIG_NO_CONFIG_WRITE */
		"load_dynamic_eap",
#ifdef CONFIG_WPS
		"uuid", "device_name", "manufacturer", "model_name",
		"model_number", "serial_number", "device_type", "os_version",
		"config_methods", "wps_cred_processing", "wps_vendor_ext_m1",
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P
		"sec_device_type",
		"p2p_listen_reg_class", "p2p_listen_channel",
		"p2p_oper_reg_class", "p2p_oper_channel", "p2p_go_intent",
		"p2p_ssid_postfix", "persistent_reconnect", "p2p_intra_bss",
		"p2p_group_idle", "p2p_passphrase_len", "p2p_pref_chan",
		"p2p_no_go_freq", "p2p_add_cli_chan",
		"p2p_optimize_listen_chan", "p2p_go_ht40", "p2p_go_vht",
		"p2p_disabled", "p2p_go_ctwindow", "p2p_no_group_iface",
		"p2p_ignore_shared_freq", "ip_addr_go", "ip_addr_mask",
		"ip_addr_start", "ip_addr_end",
#endif /* CONFIG_P2P */
		"country", "bss_max_count", "bss_expiration_age",
		"bss_expiration_scan_count", "filter_ssids", "filter_rssi",
		"max_num_sta", "disassoc_low_ack", "ap_isolate",
#ifdef CONFIG_HS20
		"hs20",
#endif /* CONFIG_HS20 */
		"interworking", "hessid", "access_network_type", "pbc_in_m1",
		"go_interworking", "go_access_network_type", "go_internet",
		"go_venue_group", "go_venue_type",
		"autoscan", "wps_nfc_dev_pw_id", "wps_nfc_dh_pubkey",
		"wps_nfc_dh_privkey", "wps_nfc_dev_pw", "ext_password_backend",
		"p2p_go_max_inactivity", "auto_interworking", "okc", "pmf",
		"sae_groups", "dtim_period", "beacon_int",
		"ap_vendor_elements", "ignore_old_scan_res", "freq_list",
		"scan_cur_freq", "sched_scan_interval",
		"tdls_external_control", "osu_dir", "wowlan_triggers",
		"p2p_search_delay", "mac_addr", "rand_addr_lifetime",
		"preassoc_mac_addr", "key_mgmt_offload", "passive_scan",
		"reassoc_same_bss_optim", "wps_priority",
#ifdef CONFIG_TESTING_OPTIONS
		"ignore_auth_resp",
#endif /* CONFIG_TESTING_OPTIONS */
		"relative_rssi", "relative_band_adjust",
	};
	int i, num_fields = ARRAY_SIZE(fields);

	if (arg == 1) {
		char **res = os_calloc(num_fields + 1, sizeof(char *));
		if (res == NULL)
			return NULL;
		for (i = 0; i < num_fields; i++) {
			res[i] = os_strdup(fields[i]);
			if (res[i] == NULL)
				return res;
		}
		return res;
	}

	if (arg > 1 && os_strncasecmp(str, "set bssid_filter ", 17) == 0)
		return cli_txt_list_array(&bsses);

	return NULL;
}

static int wpa_cli_cmd_dump(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "DUMP");
}


static int wpa_cli_cmd_driver_flags(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_ctrl_command(ctrl, "DRIVER_FLAGS");
}


static int wpa_cli_cmd_get(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "GET", 1, argc, argv);
}


static char ** wpa_cli_complete_get(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	const char *fields[] = {
#ifdef CONFIG_CTRL_IFACE
		"ctrl_interface", "ctrl_interface_group",
#endif /* CONFIG_CTRL_IFACE */
		"eapol_version", "ap_scan",
#ifdef CONFIG_MESH
		"user_mpm", "max_peer_links", "mesh_max_inactivity",
#endif /* CONFIG_MESH */
		"disable_scan_offload", "fast_reauth", "opensc_engine_path",
		"pkcs11_engine_path", "pkcs11_module_path", "openssl_ciphers",
		"pcsc_reader", "pcsc_pin", "external_sim", "driver_param",
		"dot11RSNAConfigPMKLifetime",
		"dot11RSNAConfigPMKReauthThreshold",
		"dot11RSNAConfigSATimeout",
#ifndef CONFIG_NO_CONFIG_WRITE
		"update_config",
#endif /* CONFIG_NO_CONFIG_WRITE */
#ifdef CONFIG_WPS
		"device_name", "manufacturer", "model_name", "model_number",
		"serial_number", "config_methods", "wps_cred_processing",
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P
		"p2p_listen_reg_class", "p2p_listen_channel",
		"p2p_oper_reg_class", "p2p_oper_channel", "p2p_go_intent",
		"p2p_ssid_postfix", "persistent_reconnect", "p2p_intra_bss",
		"p2p_group_idle", "p2p_passphrase_len", "p2p_add_cli_chan",
		"p2p_optimize_listen_chan", "p2p_go_ht40", "p2p_go_vht",
		"p2p_disabled", "p2p_go_ctwindow", "p2p_no_group_iface",
		"p2p_ignore_shared_freq", "ip_addr_go", "ip_addr_mask",
		"ip_addr_start", "ip_addr_end",
#endif /* CONFIG_P2P */
		"bss_max_count", "bss_expiration_age",
		"bss_expiration_scan_count", "filter_ssids", "filter_rssi",
		"max_num_sta", "disassoc_low_ack", "ap_isolate",
#ifdef CONFIG_HS20
		"hs20",
#endif /* CONFIG_HS20 */
		"interworking", "access_network_type", "pbc_in_m1", "autoscan",
		"go_interworking", "go_access_network_type", "go_internet",
		"go_venue_group", "go_venue_type",
		"wps_nfc_dev_pw_id", "ext_password_backend",
		"p2p_go_max_inactivity", "auto_interworking", "okc", "pmf",
		"dtim_period", "beacon_int", "ignore_old_scan_res",
		"scan_cur_freq", "sched_scan_interval",
		"sched_scan_start_delay",
		"tdls_external_control", "osu_dir", "wowlan_triggers",
		"p2p_search_delay", "mac_addr", "rand_addr_lifetime",
		"preassoc_mac_addr", "key_mgmt_offload", "passive_scan",
		"reassoc_same_bss_optim"
	};
	int i, num_fields = ARRAY_SIZE(fields);

	if (arg == 1) {
		char **res = os_calloc(num_fields + 1, sizeof(char *));
		if (res == NULL)
			return NULL;
		for (i = 0; i < num_fields; i++) {
			res[i] = os_strdup(fields[i]);
			if (res[i] == NULL)
				return res;
		}
		return res;
	}

	return NULL;
}


static int wpa_cli_cmd_logoff(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "LOGOFF");
}


static int wpa_cli_cmd_logon(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "LOGON");
}


static int wpa_cli_cmd_reassociate(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "REASSOCIATE");
}


static int wpa_cli_cmd_reattach(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "REATTACH");
}


static int wpa_cli_cmd_preauthenticate(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_cli_cmd(ctrl, "PREAUTH", 1, argc, argv);
}


static int wpa_cli_cmd_ap_scan(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "AP_SCAN", 1, argc, argv);
}


static int wpa_cli_cmd_scan_interval(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "SCAN_INTERVAL", 1, argc, argv);
}


static int wpa_cli_cmd_bss_expire_age(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_cli_cmd(ctrl, "BSS_EXPIRE_AGE", 1, argc, argv);
}


static int wpa_cli_cmd_bss_expire_count(struct wpa_ctrl *ctrl, int argc,
				        char *argv[])
{
	return wpa_cli_cmd(ctrl, "BSS_EXPIRE_COUNT", 1, argc, argv);
}


static int wpa_cli_cmd_bss_flush(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256];
	int res;

	if (argc < 1)
		res = os_snprintf(cmd, sizeof(cmd), "BSS_FLUSH 0");
	else
		res = os_snprintf(cmd, sizeof(cmd), "BSS_FLUSH %s", argv[0]);
	if (os_snprintf_error(sizeof(cmd), res)) {
		printf("Too long BSS_FLUSH command.\n");
		return -1;
	}
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_ft_ds(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "FT_DS", 1, argc, argv);
}


static int wpa_cli_cmd_wps_pbc(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "WPS_PBC", 0, argc, argv);
}


static int wpa_cli_cmd_wps_pin(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc == 0) {
		printf("Invalid WPS_PIN command: need one or two arguments:\n"
		       "- BSSID: use 'any' to select any\n"
		       "- PIN: optional, used only with devices that have no "
		       "display\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "WPS_PIN", 1, argc, argv);
}


static int wpa_cli_cmd_wps_check_pin(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "WPS_CHECK_PIN", 1, argc, argv);
}


static int wpa_cli_cmd_wps_cancel(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_ctrl_command(ctrl, "WPS_CANCEL");
}


#ifdef CONFIG_WPS_NFC

static int wpa_cli_cmd_wps_nfc(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "WPS_NFC", 0, argc, argv);
}


static int wpa_cli_cmd_wps_nfc_config_token(struct wpa_ctrl *ctrl, int argc,
					    char *argv[])
{
	return wpa_cli_cmd(ctrl, "WPS_NFC_CONFIG_TOKEN", 1, argc, argv);
}


static int wpa_cli_cmd_wps_nfc_token(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "WPS_NFC_TOKEN", 1, argc, argv);
}


static int wpa_cli_cmd_wps_nfc_tag_read(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	int ret;
	char *buf;
	size_t buflen;

	if (argc != 1) {
		printf("Invalid 'wps_nfc_tag_read' command - one argument "
		       "is required.\n");
		return -1;
	}

	buflen = 18 + os_strlen(argv[0]);
	buf = os_malloc(buflen);
	if (buf == NULL)
		return -1;
	os_snprintf(buf, buflen, "WPS_NFC_TAG_READ %s", argv[0]);

	ret = wpa_ctrl_command(ctrl, buf);
	os_free(buf);

	return ret;
}


static int wpa_cli_cmd_nfc_get_handover_req(struct wpa_ctrl *ctrl, int argc,
					    char *argv[])
{
	return wpa_cli_cmd(ctrl, "NFC_GET_HANDOVER_REQ", 2, argc, argv);
}


static int wpa_cli_cmd_nfc_get_handover_sel(struct wpa_ctrl *ctrl, int argc,
					    char *argv[])
{
	return wpa_cli_cmd(ctrl, "NFC_GET_HANDOVER_SEL", 2, argc, argv);
}


static int wpa_cli_cmd_nfc_report_handover(struct wpa_ctrl *ctrl, int argc,
					   char *argv[])
{
	return wpa_cli_cmd(ctrl, "NFC_REPORT_HANDOVER", 4, argc, argv);
}

#endif /* CONFIG_WPS_NFC */


static int wpa_cli_cmd_wps_reg(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256];
	int res;

	if (argc == 2)
		res = os_snprintf(cmd, sizeof(cmd), "WPS_REG %s %s",
				  argv[0], argv[1]);
	else if (argc == 5 || argc == 6) {
		char ssid_hex[2 * SSID_MAX_LEN + 1];
		char key_hex[2 * 64 + 1];
		int i;

		ssid_hex[0] = '\0';
		for (i = 0; i < SSID_MAX_LEN; i++) {
			if (argv[2][i] == '\0')
				break;
			os_snprintf(&ssid_hex[i * 2], 3, "%02x", argv[2][i]);
		}

		key_hex[0] = '\0';
		if (argc == 6) {
			for (i = 0; i < 64; i++) {
				if (argv[5][i] == '\0')
					break;
				os_snprintf(&key_hex[i * 2], 3, "%02x",
					    argv[5][i]);
			}
		}

		res = os_snprintf(cmd, sizeof(cmd),
				  "WPS_REG %s %s %s %s %s %s",
				  argv[0], argv[1], ssid_hex, argv[3], argv[4],
				  key_hex);
	} else {
		printf("Invalid WPS_REG command: need two arguments:\n"
		       "- BSSID of the target AP\n"
		       "- AP PIN\n");
		printf("Alternatively, six arguments can be used to "
		       "reconfigure the AP:\n"
		       "- BSSID of the target AP\n"
		       "- AP PIN\n"
		       "- new SSID\n"
		       "- new auth (OPEN, WPAPSK, WPA2PSK)\n"
		       "- new encr (NONE, WEP, TKIP, CCMP)\n"
		       "- new key\n");
		return -1;
	}

	if (os_snprintf_error(sizeof(cmd), res)) {
		printf("Too long WPS_REG command.\n");
		return -1;
	}
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_wps_ap_pin(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_cli_cmd(ctrl, "WPS_AP_PIN", 1, argc, argv);
}


static int wpa_cli_cmd_wps_er_start(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_cli_cmd(ctrl, "WPS_ER_START", 0, argc, argv);
}


static int wpa_cli_cmd_wps_er_stop(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "WPS_ER_STOP");

}


static int wpa_cli_cmd_wps_er_pin(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	if (argc < 2) {
		printf("Invalid WPS_ER_PIN command: need at least two "
		       "arguments:\n"
		       "- UUID: use 'any' to select any\n"
		       "- PIN: Enrollee PIN\n"
		       "optional: - Enrollee MAC address\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "WPS_ER_PIN", 2, argc, argv);
}


static int wpa_cli_cmd_wps_er_pbc(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_cli_cmd(ctrl, "WPS_ER_PBC", 1, argc, argv);
}


static int wpa_cli_cmd_wps_er_learn(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	if (argc != 2) {
		printf("Invalid WPS_ER_LEARN command: need two arguments:\n"
		       "- UUID: specify which AP to use\n"
		       "- PIN: AP PIN\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "WPS_ER_LEARN", 2, argc, argv);
}


static int wpa_cli_cmd_wps_er_set_config(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	if (argc != 2) {
		printf("Invalid WPS_ER_SET_CONFIG command: need two "
		       "arguments:\n"
		       "- UUID: specify which AP to use\n"
		       "- Network configuration id\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "WPS_ER_SET_CONFIG", 2, argc, argv);
}


static int wpa_cli_cmd_wps_er_config(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	char cmd[256];
	int res;

	if (argc == 5 || argc == 6) {
		char ssid_hex[2 * SSID_MAX_LEN + 1];
		char key_hex[2 * 64 + 1];
		int i;

		ssid_hex[0] = '\0';
		for (i = 0; i < SSID_MAX_LEN; i++) {
			if (argv[2][i] == '\0')
				break;
			os_snprintf(&ssid_hex[i * 2], 3, "%02x", argv[2][i]);
		}

		key_hex[0] = '\0';
		if (argc == 6) {
			for (i = 0; i < 64; i++) {
				if (argv[5][i] == '\0')
					break;
				os_snprintf(&key_hex[i * 2], 3, "%02x",
					    argv[5][i]);
			}
		}

		res = os_snprintf(cmd, sizeof(cmd),
				  "WPS_ER_CONFIG %s %s %s %s %s %s",
				  argv[0], argv[1], ssid_hex, argv[3], argv[4],
				  key_hex);
	} else {
		printf("Invalid WPS_ER_CONFIG command: need six arguments:\n"
		       "- AP UUID\n"
		       "- AP PIN\n"
		       "- new SSID\n"
		       "- new auth (OPEN, WPAPSK, WPA2PSK)\n"
		       "- new encr (NONE, WEP, TKIP, CCMP)\n"
		       "- new key\n");
		return -1;
	}

	if (os_snprintf_error(sizeof(cmd), res)) {
		printf("Too long WPS_ER_CONFIG command.\n");
		return -1;
	}
	return wpa_ctrl_command(ctrl, cmd);
}


#ifdef CONFIG_WPS_NFC
static int wpa_cli_cmd_wps_er_nfc_config_token(struct wpa_ctrl *ctrl, int argc,
					       char *argv[])
{
	if (argc != 2) {
		printf("Invalid WPS_ER_NFC_CONFIG_TOKEN command: need two "
		       "arguments:\n"
		       "- WPS/NDEF: token format\n"
		       "- UUID: specify which AP to use\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "WPS_ER_NFC_CONFIG_TOKEN", 2, argc, argv);
}
#endif /* CONFIG_WPS_NFC */


static int wpa_cli_cmd_ibss_rsn(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "IBSS_RSN", 1, argc, argv);
}


static int wpa_cli_cmd_level(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "LEVEL", 1, argc, argv);
}


static int wpa_cli_cmd_identity(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256], *pos, *end;
	int i, ret;

	if (argc < 2) {
		printf("Invalid IDENTITY command: needs two arguments "
		       "(network id and identity)\n");
		return -1;
	}

	end = cmd + sizeof(cmd);
	pos = cmd;
	ret = os_snprintf(pos, end - pos, WPA_CTRL_RSP "IDENTITY-%s:%s",
			  argv[0], argv[1]);
	if (os_snprintf_error(end - pos, ret)) {
		printf("Too long IDENTITY command.\n");
		return -1;
	}
	pos += ret;
	for (i = 2; i < argc; i++) {
		ret = os_snprintf(pos, end - pos, " %s", argv[i]);
		if (os_snprintf_error(end - pos, ret)) {
			printf("Too long IDENTITY command.\n");
			return -1;
		}
		pos += ret;
	}

	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_password(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256], *pos, *end;
	int i, ret;

	if (argc < 2) {
		printf("Invalid PASSWORD command: needs two arguments "
		       "(network id and password)\n");
		return -1;
	}

	end = cmd + sizeof(cmd);
	pos = cmd;
	ret = os_snprintf(pos, end - pos, WPA_CTRL_RSP "PASSWORD-%s:%s",
			  argv[0], argv[1]);
	if (os_snprintf_error(end - pos, ret)) {
		printf("Too long PASSWORD command.\n");
		return -1;
	}
	pos += ret;
	for (i = 2; i < argc; i++) {
		ret = os_snprintf(pos, end - pos, " %s", argv[i]);
		if (os_snprintf_error(end - pos, ret)) {
			printf("Too long PASSWORD command.\n");
			return -1;
		}
		pos += ret;
	}

	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_new_password(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	char cmd[256], *pos, *end;
	int i, ret;

	if (argc < 2) {
		printf("Invalid NEW_PASSWORD command: needs two arguments "
		       "(network id and password)\n");
		return -1;
	}

	end = cmd + sizeof(cmd);
	pos = cmd;
	ret = os_snprintf(pos, end - pos, WPA_CTRL_RSP "NEW_PASSWORD-%s:%s",
			  argv[0], argv[1]);
	if (os_snprintf_error(end - pos, ret)) {
		printf("Too long NEW_PASSWORD command.\n");
		return -1;
	}
	pos += ret;
	for (i = 2; i < argc; i++) {
		ret = os_snprintf(pos, end - pos, " %s", argv[i]);
		if (os_snprintf_error(end - pos, ret)) {
			printf("Too long NEW_PASSWORD command.\n");
			return -1;
		}
		pos += ret;
	}

	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_pin(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256], *pos, *end;
	int i, ret;

	if (argc < 2) {
		printf("Invalid PIN command: needs two arguments "
		       "(network id and pin)\n");
		return -1;
	}

	end = cmd + sizeof(cmd);
	pos = cmd;
	ret = os_snprintf(pos, end - pos, WPA_CTRL_RSP "PIN-%s:%s",
			  argv[0], argv[1]);
	if (os_snprintf_error(end - pos, ret)) {
		printf("Too long PIN command.\n");
		return -1;
	}
	pos += ret;
	for (i = 2; i < argc; i++) {
		ret = os_snprintf(pos, end - pos, " %s", argv[i]);
		if (os_snprintf_error(end - pos, ret)) {
			printf("Too long PIN command.\n");
			return -1;
		}
		pos += ret;
	}
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_otp(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256], *pos, *end;
	int i, ret;

	if (argc < 2) {
		printf("Invalid OTP command: needs two arguments (network "
		       "id and password)\n");
		return -1;
	}

	end = cmd + sizeof(cmd);
	pos = cmd;
	ret = os_snprintf(pos, end - pos, WPA_CTRL_RSP "OTP-%s:%s",
			  argv[0], argv[1]);
	if (os_snprintf_error(end - pos, ret)) {
		printf("Too long OTP command.\n");
		return -1;
	}
	pos += ret;
	for (i = 2; i < argc; i++) {
		ret = os_snprintf(pos, end - pos, " %s", argv[i]);
		if (os_snprintf_error(end - pos, ret)) {
			printf("Too long OTP command.\n");
			return -1;
		}
		pos += ret;
	}

	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_sim(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256], *pos, *end;
	int i, ret;

	if (argc < 2) {
		printf("Invalid SIM command: needs two arguments "
		       "(network id and SIM operation response)\n");
		return -1;
	}

	end = cmd + sizeof(cmd);
	pos = cmd;
	ret = os_snprintf(pos, end - pos, WPA_CTRL_RSP "SIM-%s:%s",
			  argv[0], argv[1]);
	if (os_snprintf_error(end - pos, ret)) {
		printf("Too long SIM command.\n");
		return -1;
	}
	pos += ret;
	for (i = 2; i < argc; i++) {
		ret = os_snprintf(pos, end - pos, " %s", argv[i]);
		if (os_snprintf_error(end - pos, ret)) {
			printf("Too long SIM command.\n");
			return -1;
		}
		pos += ret;
	}
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_passphrase(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	char cmd[256], *pos, *end;
	int i, ret;

	if (argc < 2) {
		printf("Invalid PASSPHRASE command: needs two arguments "
		       "(network id and passphrase)\n");
		return -1;
	}

	end = cmd + sizeof(cmd);
	pos = cmd;
	ret = os_snprintf(pos, end - pos, WPA_CTRL_RSP "PASSPHRASE-%s:%s",
			  argv[0], argv[1]);
	if (os_snprintf_error(end - pos, ret)) {
		printf("Too long PASSPHRASE command.\n");
		return -1;
	}
	pos += ret;
	for (i = 2; i < argc; i++) {
		ret = os_snprintf(pos, end - pos, " %s", argv[i]);
		if (os_snprintf_error(end - pos, ret)) {
			printf("Too long PASSPHRASE command.\n");
			return -1;
		}
		pos += ret;
	}

	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_bssid(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc < 2) {
		printf("Invalid BSSID command: needs two arguments (network "
		       "id and BSSID)\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "BSSID", 2, argc, argv);
}


static int wpa_cli_cmd_blacklist(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "BLACKLIST", 0, argc, argv);
}


static int wpa_cli_cmd_log_level(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "LOG_LEVEL", 0, argc, argv);
}


static int wpa_cli_cmd_list_networks(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_ctrl_command(ctrl, "LIST_NETWORKS");
}


static int wpa_cli_cmd_select_network(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_cli_cmd(ctrl, "SELECT_NETWORK", 1, argc, argv);
}


static int wpa_cli_cmd_enable_network(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_cli_cmd(ctrl, "ENABLE_NETWORK", 1, argc, argv);
}


static int wpa_cli_cmd_disable_network(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_cli_cmd(ctrl, "DISABLE_NETWORK", 1, argc, argv);
}


static int wpa_cli_cmd_add_network(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	int res = wpa_ctrl_command(ctrl, "ADD_NETWORK");
	if (interactive)
		update_networks(ctrl);
	return res;
}


static int wpa_cli_cmd_remove_network(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	int res = wpa_cli_cmd(ctrl, "REMOVE_NETWORK", 1, argc, argv);
	if (interactive)
		update_networks(ctrl);
	return res;
}


static void wpa_cli_show_network_variables(void)
{
	printf("set_network variables:\n"
	       "  ssid (network name, SSID)\n"
	       "  psk (WPA passphrase or pre-shared key)\n"
	       "  key_mgmt (key management protocol)\n"
	       "  identity (EAP identity)\n"
	       "  password (EAP password)\n"
	       "  ...\n"
	       "\n"
	       "Note: Values are entered in the same format as the "
	       "configuration file is using,\n"
	       "i.e., strings values need to be inside double quotation "
	       "marks.\n"
	       "For example: set_network 1 ssid \"network name\"\n"
	       "\n"
	       "Please see wpa_supplicant.conf documentation for full list "
	       "of\navailable variables.\n");
}


static int wpa_cli_cmd_set_network(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	if (argc == 0) {
		wpa_cli_show_network_variables();
		return 0;
	}

	if (argc < 3) {
		printf("Invalid SET_NETWORK command: needs three arguments\n"
		       "(network id, variable name, and value)\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "SET_NETWORK", 3, argc, argv);
}


static int wpa_cli_cmd_get_network(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	if (argc == 0) {
		wpa_cli_show_network_variables();
		return 0;
	}

	if (argc != 2) {
		printf("Invalid GET_NETWORK command: needs two arguments\n"
		       "(network id and variable name)\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "GET_NETWORK", 2, argc, argv);
}


static const char *network_fields[] = {
	"ssid", "scan_ssid", "bssid", "bssid_blacklist",
	"bssid_whitelist", "psk", "proto", "key_mgmt",
	"bg_scan_period", "pairwise", "group", "auth_alg", "scan_freq",
	"freq_list", "max_oper_chwidth", "ht40", "vht", "vht_center_freq1",
	"vht_center_freq2", "ht",
#ifdef IEEE8021X_EAPOL
	"eap", "identity", "anonymous_identity", "password", "ca_cert",
	"ca_path", "client_cert", "private_key", "private_key_passwd",
	"dh_file", "subject_match", "altsubject_match",
	"domain_suffix_match", "domain_match", "ca_cert2", "ca_path2",
	"client_cert2", "private_key2", "private_key2_passwd",
	"dh_file2", "subject_match2", "altsubject_match2",
	"domain_suffix_match2", "domain_match2", "phase1", "phase2",
	"pcsc", "pin", "engine_id", "key_id", "cert_id", "ca_cert_id",
	"pin2", "engine2_id", "key2_id", "cert2_id", "ca_cert2_id",
	"engine", "engine2", "eapol_flags", "sim_num",
	"openssl_ciphers", "erp",
#endif /* IEEE8021X_EAPOL */
	"wep_key0", "wep_key1", "wep_key2", "wep_key3",
	"wep_tx_keyidx", "priority",
#ifdef IEEE8021X_EAPOL
	"eap_workaround", "pac_file", "fragment_size", "ocsp",
#endif /* IEEE8021X_EAPOL */
#ifdef CONFIG_MESH
	"mode", "no_auto_peer", "mesh_rssi_threshold",
#else /* CONFIG_MESH */
	"mode",
#endif /* CONFIG_MESH */
	"proactive_key_caching", "disabled", "id_str",
#ifdef CONFIG_IEEE80211W
	"ieee80211w",
#endif /* CONFIG_IEEE80211W */
	"mixed_cell", "frequency", "fixed_freq",
#ifdef CONFIG_MESH
	"mesh_basic_rates", "dot11MeshMaxRetries",
	"dot11MeshRetryTimeout", "dot11MeshConfirmTimeout",
	"dot11MeshHoldingTimeout",
#endif /* CONFIG_MESH */
	"wpa_ptk_rekey", "bgscan", "ignore_broadcast_ssid",
#ifdef CONFIG_P2P
	"go_p2p_dev_addr", "p2p_client_list", "psk_list",
#endif /* CONFIG_P2P */
#ifdef CONFIG_HT_OVERRIDES
	"disable_ht", "disable_ht40", "disable_sgi", "disable_ldpc",
	"ht40_intolerant", "disable_max_amsdu", "ampdu_factor",
	"ampdu_density", "ht_mcs",
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_VHT_OVERRIDES
	"disable_vht", "vht_capa", "vht_capa_mask", "vht_rx_mcs_nss_1",
	"vht_rx_mcs_nss_2", "vht_rx_mcs_nss_3", "vht_rx_mcs_nss_4",
	"vht_rx_mcs_nss_5", "vht_rx_mcs_nss_6", "vht_rx_mcs_nss_7",
	"vht_rx_mcs_nss_8", "vht_tx_mcs_nss_1", "vht_tx_mcs_nss_2",
	"vht_tx_mcs_nss_3", "vht_tx_mcs_nss_4", "vht_tx_mcs_nss_5",
	"vht_tx_mcs_nss_6", "vht_tx_mcs_nss_7", "vht_tx_mcs_nss_8",
#endif /* CONFIG_VHT_OVERRIDES */
	"ap_max_inactivity", "dtim_period", "beacon_int",
#ifdef CONFIG_MACSEC
	"macsec_policy",
	"macsec_integ_only",
	"macsec_port",
	"mka_priority",
#endif /* CONFIG_MACSEC */
#ifdef CONFIG_HS20
	"update_identifier",
#endif /* CONFIG_HS20 */
	"mac_addr", "pbss", "wps_disabled"
};


static char ** wpa_cli_complete_network(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	int i, num_fields = ARRAY_SIZE(network_fields);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&networks);
		break;
	case 2:
		res = os_calloc(num_fields + 1, sizeof(char *));
		if (res == NULL)
			return NULL;
		for (i = 0; i < num_fields; i++) {
			res[i] = os_strdup(network_fields[i]);
			if (res[i] == NULL)
				break;
		}
	}
	return res;
}


static char ** wpa_cli_complete_network_id(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	if (arg == 1)
		return cli_txt_list_array(&networks);
	return NULL;
}


static int wpa_cli_cmd_dup_network(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	if (argc == 0) {
		wpa_cli_show_network_variables();
		return 0;
	}

	if (argc < 3) {
		printf("Invalid DUP_NETWORK command: needs three arguments\n"
		       "(src netid, dest netid, and variable name)\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "DUP_NETWORK", 3, argc, argv);
}


static char ** wpa_cli_complete_dup_network(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	int i, num_fields = ARRAY_SIZE(network_fields);
	char **res = NULL;

	switch (arg) {
	case 1:
	case 2:
		res = cli_txt_list_array(&networks);
		break;
	case 3:
		res = os_calloc(num_fields + 1, sizeof(char *));
		if (res == NULL)
			return NULL;
		for (i = 0; i < num_fields; i++) {
			res[i] = os_strdup(network_fields[i]);
			if (res[i] == NULL)
				break;
		}
	}
	return res;
}


static int wpa_cli_cmd_list_creds(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_ctrl_command(ctrl, "LIST_CREDS");
}


static int wpa_cli_cmd_add_cred(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	int res = wpa_ctrl_command(ctrl, "ADD_CRED");
	if (interactive)
		update_creds(ctrl);
	return res;
}


static int wpa_cli_cmd_remove_cred(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	int res = wpa_cli_cmd(ctrl, "REMOVE_CRED", 1, argc, argv);
	if (interactive)
		update_creds(ctrl);
	return res;
}


static const char * const cred_fields[] = {
	"temporary", "priority", "sp_priority", "pcsc", "eap",
	"update_identifier", "min_dl_bandwidth_home", "min_ul_bandwidth_home",
	"min_dl_bandwidth_roaming", "min_ul_bandwidth_roaming", "max_bss_load",
	"req_conn_capab", "ocsp", "sim_num", "realm", "username", "password",
	"ca_cert", "client_cert", "private_key", "private_key_passwd", "imsi",
	"milenage", "domain_suffix_match", "domain", "phase1", "phase2",
	"roaming_consortium", "required_roaming_consortium", "excluded_ssid",
	"roaming_partner", "provisioning_sp"
};


static char ** wpa_cli_complete_cred(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	int i, num_fields = ARRAY_SIZE(cred_fields);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&creds);
		break;
	case 2:
		res = os_calloc(num_fields + 1, sizeof(char *));
		if (res == NULL)
			return NULL;
		for (i = 0; i < num_fields; i++) {
			res[i] = os_strdup(cred_fields[i]);
			if (res[i] == NULL)
				break;
		}
	}
	return res;
}


static int wpa_cli_cmd_set_cred(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc != 3) {
		printf("Invalid SET_CRED command: needs three arguments\n"
		       "(cred id, variable name, and value)\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "SET_CRED", 3, argc, argv);
}


static int wpa_cli_cmd_get_cred(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc != 2) {
		printf("Invalid GET_CRED command: needs two arguments\n"
		       "(cred id, variable name)\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "GET_CRED", 2, argc, argv);
}


static int wpa_cli_cmd_disconnect(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_ctrl_command(ctrl, "DISCONNECT");
}


static int wpa_cli_cmd_reconnect(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_ctrl_command(ctrl, "RECONNECT");
}


static int wpa_cli_cmd_save_config(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "SAVE_CONFIG");
}


static int wpa_cli_cmd_scan(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "SCAN", 0, argc, argv);
}


static int wpa_cli_cmd_scan_results(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_ctrl_command(ctrl, "SCAN_RESULTS");
}


static int wpa_cli_cmd_abort_scan(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_ctrl_command(ctrl, "ABORT_SCAN");
}


static int wpa_cli_cmd_bss(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "BSS", 1, argc, argv);
}


static char ** wpa_cli_complete_bss(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&bsses);
		break;
	}

	return res;
}


static int wpa_cli_cmd_get_capability(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	if (argc < 1 || argc > 2) {
		printf("Invalid GET_CAPABILITY command: need either one or "
		       "two arguments\n");
		return -1;
	}

	if ((argc == 2) && os_strcmp(argv[1], "strict") != 0) {
		printf("Invalid GET_CAPABILITY command: second argument, "
		       "if any, must be 'strict'\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "GET_CAPABILITY", 1, argc, argv);
}


static char ** wpa_cli_complete_get_capability(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	const char *fields[] = {
		"eap", "pairwise", "group", "group_mgmt", "key_mgmt",
		"proto", "auth_alg", "modes", "channels", "freq",
#ifdef CONFIG_TDLS
		"tdls",
#endif /* CONFIG_TDLS */
#ifdef CONFIG_ERP
		"erp",
#endif /* CONFIG_ERP */
#ifdef CONFIG_FIPS
		"fips",
#endif /* CONFIG_FIPS */
#ifdef CONFIG_ACS
		"acs",
#endif /* CONFIG_ACS */
	};
	int i, num_fields = ARRAY_SIZE(fields);
	char **res = NULL;

	if (arg == 1) {
		res = os_calloc(num_fields + 1, sizeof(char *));
		if (res == NULL)
			return NULL;
		for (i = 0; i < num_fields; i++) {
			res[i] = os_strdup(fields[i]);
			if (res[i] == NULL)
				return res;
		}
	}
	if (arg == 2) {
		res = os_calloc(1 + 1, sizeof(char *));
		if (res == NULL)
			return NULL;
		res[0] = os_strdup("strict");
	}
	return res;
}


static int wpa_cli_list_interfaces(struct wpa_ctrl *ctrl)
{
	printf("Available interfaces:\n");
	return wpa_ctrl_command(ctrl, "INTERFACES");
}


static int wpa_cli_cmd_interface(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc < 1) {
		wpa_cli_list_interfaces(ctrl);
		return 0;
	}

	wpa_cli_close_connection();
	os_free(ctrl_ifname);
	ctrl_ifname = os_strdup(argv[0]);
	if (!ctrl_ifname) {
		printf("Failed to allocate memory\n");
		return 0;
	}

	if (wpa_cli_open_connection(ctrl_ifname, 1) == 0) {
		printf("Connected to interface '%s.\n", ctrl_ifname);
	} else {
		printf("Could not connect to interface '%s' - re-trying\n",
		       ctrl_ifname);
	}
	return 0;
}


static int wpa_cli_cmd_reconfigure(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "RECONFIGURE");
}


static int wpa_cli_cmd_terminate(struct wpa_ctrl *ctrl, int argc,
				 char *argv[])
{
	return wpa_ctrl_command(ctrl, "TERMINATE");
}


static int wpa_cli_cmd_interface_add(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	char cmd[256];
	int res;

	if (argc < 1) {
		printf("Invalid INTERFACE_ADD command: needs at least one "
		       "argument (interface name)\n"
		       "All arguments: ifname confname driver ctrl_interface "
		       "driver_param bridge_name [create]\n");
		return -1;
	}

	/*
	 * INTERFACE_ADD <ifname>TAB<confname>TAB<driver>TAB<ctrl_interface>TAB
	 * <driver_param>TAB<bridge_name>[TAB<create>[TAB<type>]]
	 */
	res = os_snprintf(cmd, sizeof(cmd),
			  "INTERFACE_ADD %s\t%s\t%s\t%s\t%s\t%s\t%s\t%s",
			  argv[0],
			  argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "",
			  argc > 3 ? argv[3] : "", argc > 4 ? argv[4] : "",
			  argc > 5 ? argv[5] : "", argc > 6 ? argv[6] : "",
			  argc > 7 ? argv[7] : "");
	if (os_snprintf_error(sizeof(cmd), res))
		return -1;
	cmd[sizeof(cmd) - 1] = '\0';
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_interface_remove(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_cli_cmd(ctrl, "INTERFACE_REMOVE", 1, argc, argv);
}


static int wpa_cli_cmd_interface_list(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_ctrl_command(ctrl, "INTERFACE_LIST");
}


#ifdef CONFIG_AP
static int wpa_cli_cmd_sta(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "STA", 1, argc, argv);
}


static char ** wpa_cli_complete_sta(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&stations);
		break;
	}

	return res;
}


static int wpa_ctrl_command_sta(struct wpa_ctrl *ctrl, const char *cmd,
				char *addr, size_t addr_len, int print)
{
	char buf[4096], *pos;
	size_t len;
	int ret;

	if (ctrl_conn == NULL) {
		printf("Not connected to hostapd - command dropped.\n");
		return -1;
	}
	if (ifname_prefix) {
		os_snprintf(buf, sizeof(buf), "IFNAME=%s %s",
			    ifname_prefix, cmd);
		buf[sizeof(buf) - 1] = '\0';
		cmd = buf;
	}
	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, cmd, os_strlen(cmd), buf, &len,
			       wpa_cli_msg_cb);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0) {
		printf("'%s' command failed.\n", cmd);
		return -1;
	}

	buf[len] = '\0';
	if (os_memcmp(buf, "FAIL", 4) == 0 ||
	    os_memcmp(buf, "UNKNOWN COMMAND", 15) == 0)
		return -1;
	if (print)
		printf("%s", buf);

	pos = buf;
	while (*pos != '\0' && *pos != '\n')
		pos++;
	*pos = '\0';
	os_strlcpy(addr, buf, addr_len);
	return 0;
}


static int wpa_cli_cmd_all_sta(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char addr[32], cmd[64];

	if (wpa_ctrl_command_sta(ctrl, "STA-FIRST", addr, sizeof(addr), 1))
		return 0;
	do {
		os_snprintf(cmd, sizeof(cmd), "STA-NEXT %s", addr);
	} while (wpa_ctrl_command_sta(ctrl, cmd, addr, sizeof(addr), 1) == 0);

	return -1;
}


static int wpa_cli_cmd_list_sta(struct wpa_ctrl *ctrl, int argc,
				char *argv[])
{
	char addr[32], cmd[64];

	if (wpa_ctrl_command_sta(ctrl, "STA-FIRST", addr, sizeof(addr), 0))
		return 0;
	do {
		if (os_strcmp(addr, "") != 0)
			printf("%s\n", addr);
		os_snprintf(cmd, sizeof(cmd), "STA-NEXT %s", addr);
	} while (wpa_ctrl_command_sta(ctrl, cmd, addr, sizeof(addr), 0) == 0);

	return 0;
}


static int wpa_cli_cmd_deauthenticate(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_cli_cmd(ctrl, "DEAUTHENTICATE", 1, argc, argv);
}


static char ** wpa_cli_complete_deauthenticate(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&stations);
		break;
	}

	return res;
}


static int wpa_cli_cmd_disassociate(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_cli_cmd(ctrl, "DISASSOCIATE", 1, argc, argv);
}


static char ** wpa_cli_complete_disassociate(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&stations);
		break;
	}

	return res;
}


static int wpa_cli_cmd_chanswitch(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_cli_cmd(ctrl, "CHAN_SWITCH", 2, argc, argv);
}

#endif /* CONFIG_AP */


static int wpa_cli_cmd_suspend(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "SUSPEND");
}


static int wpa_cli_cmd_resume(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "RESUME");
}


#ifdef CONFIG_TESTING_OPTIONS
static int wpa_cli_cmd_drop_sa(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "DROP_SA");
}
#endif /* CONFIG_TESTING_OPTIONS */


static int wpa_cli_cmd_roam(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "ROAM", 1, argc, argv);
}


#ifdef CONFIG_MESH

static int wpa_cli_cmd_mesh_interface_add(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	return wpa_cli_cmd(ctrl, "MESH_INTERFACE_ADD", 0, argc, argv);
}


static int wpa_cli_cmd_mesh_group_add(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_cli_cmd(ctrl, "MESH_GROUP_ADD", 1, argc, argv);
}


static int wpa_cli_cmd_mesh_group_remove(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	return wpa_cli_cmd(ctrl, "MESH_GROUP_REMOVE", 1, argc, argv);
}


static int wpa_cli_cmd_mesh_peer_remove(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_cli_cmd(ctrl, "MESH_PEER_REMOVE", 1, argc, argv);
}


static int wpa_cli_cmd_mesh_peer_add(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "MESH_PEER_ADD", 1, argc, argv);
}

#endif /* CONFIG_MESH */


#ifdef CONFIG_P2P

static int wpa_cli_cmd_p2p_find(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_FIND", 0, argc, argv);
}


static char ** wpa_cli_complete_p2p_find(const char *str, int pos)
{
	char **res = NULL;
	int arg = get_cmd_arg_num(str, pos);

	res = os_calloc(6, sizeof(char *));
	if (res == NULL)
		return NULL;
	res[0] = os_strdup("type=social");
	if (res[0] == NULL) {
		os_free(res);
		return NULL;
	}
	res[1] = os_strdup("type=progressive");
	if (res[1] == NULL)
		return res;
	res[2] = os_strdup("delay=");
	if (res[2] == NULL)
		return res;
	res[3] = os_strdup("dev_id=");
	if (res[3] == NULL)
		return res;
	if (arg == 1)
		res[4] = os_strdup("[timeout]");

	return res;
}


static int wpa_cli_cmd_p2p_stop_find(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_ctrl_command(ctrl, "P2P_STOP_FIND");
}


static int wpa_cli_cmd_p2p_asp_provision(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_ASP_PROVISION", 3, argc, argv);
}


static int wpa_cli_cmd_p2p_asp_provision_resp(struct wpa_ctrl *ctrl, int argc,
					      char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_ASP_PROVISION_RESP", 2, argc, argv);
}


static int wpa_cli_cmd_p2p_connect(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_CONNECT", 2, argc, argv);
}


static char ** wpa_cli_complete_p2p_connect(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&p2p_peers);
		break;
	}

	return res;
}


static int wpa_cli_cmd_p2p_listen(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_LISTEN", 0, argc, argv);
}


static int wpa_cli_cmd_p2p_group_remove(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_GROUP_REMOVE", 1, argc, argv);
}


static char ** wpa_cli_complete_p2p_group_remove(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&p2p_groups);
		break;
	}

	return res;
}


static int wpa_cli_cmd_p2p_group_add(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_GROUP_ADD", 0, argc, argv);
}


static int wpa_cli_cmd_p2p_group_member(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_GROUP_MEMBER", 1, argc, argv);
}


static int wpa_cli_cmd_p2p_prov_disc(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	if (argc != 2 && argc != 3) {
		printf("Invalid P2P_PROV_DISC command: needs at least "
		       "two arguments, address and config method\n"
		       "(display, keypad, or pbc) and an optional join\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "P2P_PROV_DISC", 2, argc, argv);
}


static int wpa_cli_cmd_p2p_get_passphrase(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	return wpa_ctrl_command(ctrl, "P2P_GET_PASSPHRASE");
}


static int wpa_cli_cmd_p2p_serv_disc_req(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	char cmd[4096];

	if (argc < 2) {
		printf("Invalid P2P_SERV_DISC_REQ command: needs two "
		       "or more arguments (address and TLVs)\n");
		return -1;
	}

	if (write_cmd(cmd, sizeof(cmd), "P2P_SERV_DISC_REQ", argc, argv) < 0)
		return -1;
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_p2p_serv_disc_cancel_req(struct wpa_ctrl *ctrl,
						int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_SERV_DISC_CANCEL_REQ", 1, argc, argv);
}


static int wpa_cli_cmd_p2p_serv_disc_resp(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	char cmd[4096];
	int res;

	if (argc != 4) {
		printf("Invalid P2P_SERV_DISC_RESP command: needs four "
		       "arguments (freq, address, dialog token, and TLVs)\n");
		return -1;
	}

	res = os_snprintf(cmd, sizeof(cmd), "P2P_SERV_DISC_RESP %s %s %s %s",
			  argv[0], argv[1], argv[2], argv[3]);
	if (os_snprintf_error(sizeof(cmd), res))
		return -1;
	cmd[sizeof(cmd) - 1] = '\0';
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_p2p_service_update(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	return wpa_ctrl_command(ctrl, "P2P_SERVICE_UPDATE");
}


static int wpa_cli_cmd_p2p_serv_disc_external(struct wpa_ctrl *ctrl,
					      int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_SERV_DISC_EXTERNAL", 1, argc, argv);
}


static int wpa_cli_cmd_p2p_service_flush(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	return wpa_ctrl_command(ctrl, "P2P_SERVICE_FLUSH");
}


static int wpa_cli_cmd_p2p_service_add(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	if (argc < 3) {
		printf("Invalid P2P_SERVICE_ADD command: needs 3-6 arguments\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "P2P_SERVICE_ADD", 3, argc, argv);
}


static int wpa_cli_cmd_p2p_service_rep(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	if (argc < 5 || argc > 6) {
		printf("Invalid P2P_SERVICE_REP command: needs 5-6 "
		       "arguments\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "P2P_SERVICE_REP", 5, argc, argv);
}


static int wpa_cli_cmd_p2p_service_del(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	char cmd[4096];
	int res;

	if (argc != 2 && argc != 3) {
		printf("Invalid P2P_SERVICE_DEL command: needs two or three "
		       "arguments\n");
		return -1;
	}

	if (argc == 3)
		res = os_snprintf(cmd, sizeof(cmd),
				  "P2P_SERVICE_DEL %s %s %s",
				  argv[0], argv[1], argv[2]);
	else
		res = os_snprintf(cmd, sizeof(cmd),
				  "P2P_SERVICE_DEL %s %s",
				  argv[0], argv[1]);
	if (os_snprintf_error(sizeof(cmd), res))
		return -1;
	cmd[sizeof(cmd) - 1] = '\0';
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_p2p_reject(struct wpa_ctrl *ctrl,
				  int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_REJECT", 1, argc, argv);
}


static int wpa_cli_cmd_p2p_invite(struct wpa_ctrl *ctrl,
				  int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_INVITE", 1, argc, argv);
}


static int wpa_cli_cmd_p2p_peer(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_PEER", 1, argc, argv);
}


static char ** wpa_cli_complete_p2p_peer(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	char **res = NULL;

	switch (arg) {
	case 1:
		res = cli_txt_list_array(&p2p_peers);
		break;
	}

	return res;
}


static int wpa_ctrl_command_p2p_peer(struct wpa_ctrl *ctrl, const char *cmd,
				     char *addr, size_t addr_len,
				     int discovered)
{
	char buf[4096], *pos;
	size_t len;
	int ret;

	if (ctrl_conn == NULL)
		return -1;
	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, cmd, os_strlen(cmd), buf, &len,
			       wpa_cli_msg_cb);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0) {
		printf("'%s' command failed.\n", cmd);
		return -1;
	}

	buf[len] = '\0';
	if (os_memcmp(buf, "FAIL", 4) == 0)
		return -1;

	pos = buf;
	while (*pos != '\0' && *pos != '\n')
		pos++;
	*pos++ = '\0';
	os_strlcpy(addr, buf, addr_len);
	if (!discovered || os_strstr(pos, "[PROBE_REQ_ONLY]") == NULL)
		printf("%s\n", addr);
	return 0;
}


static int wpa_cli_cmd_p2p_peers(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char addr[32], cmd[64];
	int discovered;

	discovered = argc > 0 && os_strcmp(argv[0], "discovered") == 0;

	if (wpa_ctrl_command_p2p_peer(ctrl, "P2P_PEER FIRST",
				      addr, sizeof(addr), discovered))
		return -1;
	do {
		os_snprintf(cmd, sizeof(cmd), "P2P_PEER NEXT-%s", addr);
	} while (wpa_ctrl_command_p2p_peer(ctrl, cmd, addr, sizeof(addr),
			 discovered) == 0);

	return 0;
}


static int wpa_cli_cmd_p2p_set(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_SET", 2, argc, argv);
}


static char ** wpa_cli_complete_p2p_set(const char *str, int pos)
{
	int arg = get_cmd_arg_num(str, pos);
	const char *fields[] = {
		"discoverability",
		"managed",
		"listen_channel",
		"ssid_postfix",
		"noa",
		"ps",
		"oppps",
		"ctwindow",
		"disabled",
		"conc_pref",
		"force_long_sd",
		"peer_filter",
		"cross_connect",
		"go_apsd",
		"client_apsd",
		"disallow_freq",
		"disc_int",
		"per_sta_psk",
	};
	int i, num_fields = ARRAY_SIZE(fields);

	if (arg == 1) {
		char **res = os_calloc(num_fields + 1, sizeof(char *));
		if (res == NULL)
			return NULL;
		for (i = 0; i < num_fields; i++) {
			res[i] = os_strdup(fields[i]);
			if (res[i] == NULL)
				return res;
		}
		return res;
	}

	if (arg == 2 && os_strncasecmp(str, "p2p_set peer_filter ", 20) == 0)
		return cli_txt_list_array(&p2p_peers);

	return NULL;
}


static int wpa_cli_cmd_p2p_flush(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "P2P_FLUSH");
}


static int wpa_cli_cmd_p2p_cancel(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_ctrl_command(ctrl, "P2P_CANCEL");
}


static int wpa_cli_cmd_p2p_unauthorize(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_UNAUTHORIZE", 1, argc, argv);
}


static int wpa_cli_cmd_p2p_presence_req(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	if (argc != 0 && argc != 2 && argc != 4) {
		printf("Invalid P2P_PRESENCE_REQ command: needs two arguments "
		       "(preferred duration, interval; in microsecods).\n"
		       "Optional second pair can be used to provide "
		       "acceptable values.\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "P2P_PRESENCE_REQ", 0, argc, argv);
}


static int wpa_cli_cmd_p2p_ext_listen(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	if (argc != 0 && argc != 2) {
		printf("Invalid P2P_EXT_LISTEN command: needs two arguments "
		       "(availability period, availability interval; in "
		       "millisecods).\n"
		       "Extended Listen Timing can be cancelled with this "
		       "command when used without parameters.\n");
		return -1;
	}

	return wpa_cli_cmd(ctrl, "P2P_EXT_LISTEN", 0, argc, argv);
}


static int wpa_cli_cmd_p2p_remove_client(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_REMOVE_CLIENT", 1, argc, argv);
}

#endif /* CONFIG_P2P */


static int wpa_cli_cmd_vendor_elem_add(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_cli_cmd(ctrl, "VENDOR_ELEM_ADD", 2, argc, argv);
}


static int wpa_cli_cmd_vendor_elem_get(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_cli_cmd(ctrl, "VENDOR_ELEM_GET", 1, argc, argv);
}


static int wpa_cli_cmd_vendor_elem_remove(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	return wpa_cli_cmd(ctrl, "VENDOR_ELEM_REMOVE", 2, argc, argv);
}


#ifdef CONFIG_WIFI_DISPLAY

static int wpa_cli_cmd_wfd_subelem_set(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	char cmd[100];
	int res;

	if (argc != 1 && argc != 2) {
		printf("Invalid WFD_SUBELEM_SET command: needs one or two "
		       "arguments (subelem, hexdump)\n");
		return -1;
	}

	res = os_snprintf(cmd, sizeof(cmd), "WFD_SUBELEM_SET %s %s",
			  argv[0], argc > 1 ? argv[1] : "");
	if (os_snprintf_error(sizeof(cmd), res))
		return -1;
	cmd[sizeof(cmd) - 1] = '\0';
	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_wfd_subelem_get(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	char cmd[100];
	int res;

	if (argc != 1) {
		printf("Invalid WFD_SUBELEM_GET command: needs one "
		       "argument (subelem)\n");
		return -1;
	}

	res = os_snprintf(cmd, sizeof(cmd), "WFD_SUBELEM_GET %s",
			  argv[0]);
	if (os_snprintf_error(sizeof(cmd), res))
		return -1;
	cmd[sizeof(cmd) - 1] = '\0';
	return wpa_ctrl_command(ctrl, cmd);
}
#endif /* CONFIG_WIFI_DISPLAY */


#ifdef CONFIG_INTERWORKING
static int wpa_cli_cmd_fetch_anqp(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_ctrl_command(ctrl, "FETCH_ANQP");
}


static int wpa_cli_cmd_stop_fetch_anqp(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_ctrl_command(ctrl, "STOP_FETCH_ANQP");
}


static int wpa_cli_cmd_interworking_select(struct wpa_ctrl *ctrl, int argc,
					   char *argv[])
{
	return wpa_cli_cmd(ctrl, "INTERWORKING_SELECT", 0, argc, argv);
}


static int wpa_cli_cmd_interworking_connect(struct wpa_ctrl *ctrl, int argc,
					    char *argv[])
{
	return wpa_cli_cmd(ctrl, "INTERWORKING_CONNECT", 1, argc, argv);
}


static int wpa_cli_cmd_interworking_add_network(struct wpa_ctrl *ctrl, int argc,
						char *argv[])
{
	return wpa_cli_cmd(ctrl, "INTERWORKING_ADD_NETWORK", 1, argc, argv);
}


static int wpa_cli_cmd_anqp_get(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "ANQP_GET", 2, argc, argv);
}


static int wpa_cli_cmd_gas_request(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_cli_cmd(ctrl, "GAS_REQUEST", 2, argc, argv);
}


static int wpa_cli_cmd_gas_response_get(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_cli_cmd(ctrl, "GAS_RESPONSE_GET", 2, argc, argv);
}
#endif /* CONFIG_INTERWORKING */


#ifdef CONFIG_HS20

static int wpa_cli_cmd_hs20_anqp_get(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "HS20_ANQP_GET", 2, argc, argv);
}


static int wpa_cli_cmd_get_nai_home_realm_list(struct wpa_ctrl *ctrl, int argc,
					       char *argv[])
{
	char cmd[512];

	if (argc == 0) {
		printf("Command needs one or two arguments (dst mac addr and "
		       "optional home realm)\n");
		return -1;
	}

	if (write_cmd(cmd, sizeof(cmd), "HS20_GET_NAI_HOME_REALM_LIST",
		      argc, argv) < 0)
		return -1;

	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_hs20_icon_request(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	char cmd[512];

	if (argc < 2) {
		printf("Command needs two arguments (dst mac addr and "
		       "icon name)\n");
		return -1;
	}

	if (write_cmd(cmd, sizeof(cmd), "HS20_ICON_REQUEST", argc, argv) < 0)
		return -1;

	return wpa_ctrl_command(ctrl, cmd);
}


static int wpa_cli_cmd_fetch_osu(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "FETCH_OSU");
}


static int wpa_cli_cmd_cancel_fetch_osu(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_ctrl_command(ctrl, "CANCEL_FETCH_OSU");
}

#endif /* CONFIG_HS20 */


static int wpa_cli_cmd_sta_autoconnect(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_cli_cmd(ctrl, "STA_AUTOCONNECT", 1, argc, argv);
}


static int wpa_cli_cmd_tdls_discover(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "TDLS_DISCOVER", 1, argc, argv);
}


static int wpa_cli_cmd_tdls_setup(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_cli_cmd(ctrl, "TDLS_SETUP", 1, argc, argv);
}


static int wpa_cli_cmd_tdls_teardown(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "TDLS_TEARDOWN", 1, argc, argv);
}


static int wpa_cli_cmd_tdls_link_status(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_cli_cmd(ctrl, "TDLS_LINK_STATUS", 1, argc, argv);
}


static int wpa_cli_cmd_wmm_ac_addts(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_cli_cmd(ctrl, "WMM_AC_ADDTS", 3, argc, argv);
}


static int wpa_cli_cmd_wmm_ac_delts(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_cli_cmd(ctrl, "WMM_AC_DELTS", 1, argc, argv);
}


static int wpa_cli_cmd_wmm_ac_status(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_ctrl_command(ctrl, "WMM_AC_STATUS");
}


static int wpa_cli_cmd_tdls_chan_switch(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	return wpa_cli_cmd(ctrl, "TDLS_CHAN_SWITCH", 2, argc, argv);
}


static int wpa_cli_cmd_tdls_cancel_chan_switch(struct wpa_ctrl *ctrl, int argc,
					       char *argv[])
{
	return wpa_cli_cmd(ctrl, "TDLS_CANCEL_CHAN_SWITCH", 1, argc, argv);
}


static int wpa_cli_cmd_signal_poll(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "SIGNAL_POLL");
}


static int wpa_cli_cmd_signal_monitor(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_cli_cmd(ctrl, "SIGNAL_MONITOR", 0, argc, argv);
}


static int wpa_cli_cmd_pktcnt_poll(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "PKTCNT_POLL");
}


static int wpa_cli_cmd_reauthenticate(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_ctrl_command(ctrl, "REAUTHENTICATE");
}


#ifdef CONFIG_AUTOSCAN

static int wpa_cli_cmd_autoscan(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc == 0)
		return wpa_ctrl_command(ctrl, "AUTOSCAN ");

	return wpa_cli_cmd(ctrl, "AUTOSCAN", 0, argc, argv);
}

#endif /* CONFIG_AUTOSCAN */


#ifdef CONFIG_WNM

static int wpa_cli_cmd_wnm_sleep(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "WNM_SLEEP", 0, argc, argv);
}


static int wpa_cli_cmd_wnm_bss_query(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "WNM_BSS_QUERY", 1, argc, argv);
}

#endif /* CONFIG_WNM */


static int wpa_cli_cmd_raw(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	if (argc == 0)
		return -1;
	return wpa_cli_cmd(ctrl, argv[0], 0, argc - 1, &argv[1]);
}


#ifdef ANDROID
static int wpa_cli_cmd_driver(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "DRIVER", 1, argc, argv);
}
#endif /* ANDROID */


static int wpa_cli_cmd_vendor(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "VENDOR", 1, argc, argv);
}


static int wpa_cli_cmd_flush(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "FLUSH");
}


static int wpa_cli_cmd_radio_work(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_cli_cmd(ctrl, "RADIO_WORK", 1, argc, argv);
}


static int wpa_cli_cmd_neighbor_rep_request(struct wpa_ctrl *ctrl, int argc,
					    char *argv[])
{
	return wpa_cli_cmd(ctrl, "NEIGHBOR_REP_REQUEST", 0, argc, argv);
}


static int wpa_cli_cmd_erp_flush(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "ERP_FLUSH");
}


static int wpa_cli_cmd_mac_rand_scan(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "MAC_RAND_SCAN", 1, argc, argv);
}


static int wpa_cli_cmd_get_pref_freq_list(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	return wpa_cli_cmd(ctrl, "GET_PREF_FREQ_LIST", 1, argc, argv);
}


static int wpa_cli_cmd_p2p_lo_start(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_LO_START", 4, argc, argv);
}


static int wpa_cli_cmd_p2p_lo_stop(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_cli_cmd(ctrl, "P2P_LO_STOP", 0, argc, argv);
}


#ifdef CONFIG_DPP

static int wpa_cli_cmd_dpp_qr_code(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_QR_CODE", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_bootstrap_gen(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_BOOTSTRAP_GEN", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_bootstrap_remove(struct wpa_ctrl *ctrl, int argc,
					    char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_BOOTSTRAP_REMOVE", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_bootstrap_get_uri(struct wpa_ctrl *ctrl, int argc,
					     char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_BOOTSTRAP_GET_URI", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_bootstrap_info(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_BOOTSTRAP_INFO", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_auth_init(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_AUTH_INIT", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_listen(struct wpa_ctrl *ctrl, int argc,
				  char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_LISTEN", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_stop_listen(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_ctrl_command(ctrl, "DPP_STOP_LISTEN");
}


static int wpa_cli_cmd_dpp_configurator_add(struct wpa_ctrl *ctrl, int argc,
					    char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_CONFIGURATOR_ADD", 0, argc, argv);
}


static int wpa_cli_cmd_dpp_configurator_remove(struct wpa_ctrl *ctrl, int argc,
					       char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_CONFIGURATOR_REMOVE", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_configurator_get_key(struct wpa_ctrl *ctrl, int argc,
						char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_CONFIGURATOR_GET_KEY", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_pkex_add(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_PKEX_ADD", 1, argc, argv);
}


static int wpa_cli_cmd_dpp_pkex_remove(struct wpa_ctrl *ctrl, int argc,
				       char *argv[])
{
	return wpa_cli_cmd(ctrl, "DPP_PKEX_REMOVE", 1, argc, argv);
}

#endif /* CONFIG_DPP */


enum wpa_cli_cmd_flags {
	cli_cmd_flag_none		= 0x00,
	cli_cmd_flag_sensitive		= 0x01
};

struct wpa_cli_cmd {
	const char *cmd;
	int (*handler)(struct wpa_ctrl *ctrl, int argc, char *argv[]);
	char ** (*completion)(const char *str, int pos);
	enum wpa_cli_cmd_flags flags;
	const char *usage;
};

static const struct wpa_cli_cmd wpa_cli_commands[] = {
	{ "status", wpa_cli_cmd_status, NULL,
	  cli_cmd_flag_none,
	  "[verbose] = get current WPA/EAPOL/EAP status" },
	{ "ifname", wpa_cli_cmd_ifname, NULL,
	  cli_cmd_flag_none,
	  "= get current interface name" },
	{ "ping", wpa_cli_cmd_ping, NULL,
	  cli_cmd_flag_none,
	  "= pings wpa_supplicant" },
	{ "relog", wpa_cli_cmd_relog, NULL,
	  cli_cmd_flag_none,
	  "= re-open log-file (allow rolling logs)" },
	{ "note", wpa_cli_cmd_note, NULL,
	  cli_cmd_flag_none,
	  "<text> = add a note to wpa_supplicant debug log" },
	{ "mib", wpa_cli_cmd_mib, NULL,
	  cli_cmd_flag_none,
	  "= get MIB variables (dot1x, dot11)" },
	{ "help", wpa_cli_cmd_help, wpa_cli_complete_help,
	  cli_cmd_flag_none,
	  "[command] = show usage help" },
	{ "interface", wpa_cli_cmd_interface, NULL,
	  cli_cmd_flag_none,
	  "[ifname] = show interfaces/select interface" },
	{ "level", wpa_cli_cmd_level, NULL,
	  cli_cmd_flag_none,
	  "<debug level> = change debug level" },
	{ "license", wpa_cli_cmd_license, NULL,
	  cli_cmd_flag_none,
	  "= show full wpa_cli license" },
	{ "quit", wpa_cli_cmd_quit, NULL,
	  cli_cmd_flag_none,
	  "= exit wpa_cli" },
	{ "set", wpa_cli_cmd_set, wpa_cli_complete_set,
	  cli_cmd_flag_none,
	  "= set variables (shows list of variables when run without "
	  "arguments)" },
	{ "dump", wpa_cli_cmd_dump, NULL,
	  cli_cmd_flag_none,
	  "= dump config variables" },
	{ "get", wpa_cli_cmd_get, wpa_cli_complete_get,
	  cli_cmd_flag_none,
	  "<name> = get information" },
	{ "driver_flags", wpa_cli_cmd_driver_flags, NULL,
	  cli_cmd_flag_none,
	  "= list driver flags" },
	{ "logon", wpa_cli_cmd_logon, NULL,
	  cli_cmd_flag_none,
	  "= IEEE 802.1X EAPOL state machine logon" },
	{ "logoff", wpa_cli_cmd_logoff, NULL,
	  cli_cmd_flag_none,
	  "= IEEE 802.1X EAPOL state machine logoff" },
	{ "pmksa", wpa_cli_cmd_pmksa, NULL,
	  cli_cmd_flag_none,
	  "= show PMKSA cache" },
	{ "pmksa_flush", wpa_cli_cmd_pmksa_flush, NULL,
	  cli_cmd_flag_none,
	  "= flush PMKSA cache entries" },
#ifdef CONFIG_PMKSA_CACHE_EXTERNAL
	{ "pmksa_get", wpa_cli_cmd_pmksa_get, NULL,
	  cli_cmd_flag_none,
	  "<network_id> = fetch all stored PMKSA cache entries" },
	{ "pmksa_add", wpa_cli_cmd_pmksa_add, NULL,
	  cli_cmd_flag_sensitive,
	  "<network_id> <BSSID> <PMKID> <PMK> <reauth_time in seconds> <expiration in seconds> <akmp> <opportunistic> = store PMKSA cache entry from external storage" },
#ifdef CONFIG_MESH
	{ "mesh_pmksa_get", wpa_cli_mesh_cmd_pmksa_get, NULL,
	  cli_cmd_flag_none,
	  "<peer MAC address | any> = fetch all stored mesh PMKSA cache entries" },
	{ "mesh_pmksa_add", wpa_cli_mesh_cmd_pmksa_add, NULL,
	  cli_cmd_flag_sensitive,
	  "<BSSID> <PMKID> <PMK> <expiration in seconds> = store mesh PMKSA cache entry from external storage" },
#endif /* CONFIG_MESH */
#endif /* CONFIG_PMKSA_CACHE_EXTERNAL */
	{ "reassociate", wpa_cli_cmd_reassociate, NULL,
	  cli_cmd_flag_none,
	  "= force reassociation" },
	{ "reattach", wpa_cli_cmd_reattach, NULL,
	  cli_cmd_flag_none,
	  "= force reassociation back to the same BSS" },
	{ "preauthenticate", wpa_cli_cmd_preauthenticate, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "<BSSID> = force preauthentication" },
	{ "identity", wpa_cli_cmd_identity, wpa_cli_complete_network_id,
	  cli_cmd_flag_none,
	  "<network id> <identity> = configure identity for an SSID" },
	{ "password", wpa_cli_cmd_password, wpa_cli_complete_network_id,
	  cli_cmd_flag_sensitive,
	  "<network id> <password> = configure password for an SSID" },
	{ "new_password", wpa_cli_cmd_new_password,
	  wpa_cli_complete_network_id, cli_cmd_flag_sensitive,
	  "<network id> <password> = change password for an SSID" },
	{ "pin", wpa_cli_cmd_pin, wpa_cli_complete_network_id,
	  cli_cmd_flag_sensitive,
	  "<network id> <pin> = configure pin for an SSID" },
	{ "otp", wpa_cli_cmd_otp, wpa_cli_complete_network_id,
	  cli_cmd_flag_sensitive,
	  "<network id> <password> = configure one-time-password for an SSID"
	},
	{ "passphrase", wpa_cli_cmd_passphrase, wpa_cli_complete_network_id,
	  cli_cmd_flag_sensitive,
	  "<network id> <passphrase> = configure private key passphrase\n"
	  "  for an SSID" },
	{ "sim", wpa_cli_cmd_sim, wpa_cli_complete_network_id,
	  cli_cmd_flag_sensitive,
	  "<network id> <pin> = report SIM operation result" },
	{ "bssid", wpa_cli_cmd_bssid, wpa_cli_complete_network_id,
	  cli_cmd_flag_none,
	  "<network id> <BSSID> = set preferred BSSID for an SSID" },
	{ "blacklist", wpa_cli_cmd_blacklist, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "<BSSID> = add a BSSID to the blacklist\n"
	  "blacklist clear = clear the blacklist\n"
	  "blacklist = display the blacklist" },
	{ "log_level", wpa_cli_cmd_log_level, NULL,
	  cli_cmd_flag_none,
	  "<level> [<timestamp>] = update the log level/timestamp\n"
	  "log_level = display the current log level and log options" },
	{ "list_networks", wpa_cli_cmd_list_networks, NULL,
	  cli_cmd_flag_none,
	  "= list configured networks" },
	{ "select_network", wpa_cli_cmd_select_network,
	  wpa_cli_complete_network_id,
	  cli_cmd_flag_none,
	  "<network id> = select a network (disable others)" },
	{ "enable_network", wpa_cli_cmd_enable_network,
	  wpa_cli_complete_network_id,
	  cli_cmd_flag_none,
	  "<network id> = enable a network" },
	{ "disable_network", wpa_cli_cmd_disable_network,
	  wpa_cli_complete_network_id,
	  cli_cmd_flag_none,
	  "<network id> = disable a network" },
	{ "add_network", wpa_cli_cmd_add_network, NULL,
	  cli_cmd_flag_none,
	  "= add a network" },
	{ "remove_network", wpa_cli_cmd_remove_network,
	  wpa_cli_complete_network_id,
	  cli_cmd_flag_none,
	  "<network id> = remove a network" },
	{ "set_network", wpa_cli_cmd_set_network, wpa_cli_complete_network,
	  cli_cmd_flag_sensitive,
	  "<network id> <variable> <value> = set network variables (shows\n"
	  "  list of variables when run without arguments)" },
	{ "get_network", wpa_cli_cmd_get_network, wpa_cli_complete_network,
	  cli_cmd_flag_none,
	  "<network id> <variable> = get network variables" },
	{ "dup_network", wpa_cli_cmd_dup_network, wpa_cli_complete_dup_network,
	  cli_cmd_flag_none,
	  "<src network id> <dst network id> <variable> = duplicate network variables"
	},
	{ "list_creds", wpa_cli_cmd_list_creds, NULL,
	  cli_cmd_flag_none,
	  "= list configured credentials" },
	{ "add_cred", wpa_cli_cmd_add_cred, NULL,
	  cli_cmd_flag_none,
	  "= add a credential" },
	{ "remove_cred", wpa_cli_cmd_remove_cred, NULL,
	  cli_cmd_flag_none,
	  "<cred id> = remove a credential" },
	{ "set_cred", wpa_cli_cmd_set_cred, wpa_cli_complete_cred,
	  cli_cmd_flag_sensitive,
	  "<cred id> <variable> <value> = set credential variables" },
	{ "get_cred", wpa_cli_cmd_get_cred, wpa_cli_complete_cred,
	  cli_cmd_flag_none,
	  "<cred id> <variable> = get credential variables" },
	{ "save_config", wpa_cli_cmd_save_config, NULL,
	  cli_cmd_flag_none,
	  "= save the current configuration" },
	{ "disconnect", wpa_cli_cmd_disconnect, NULL,
	  cli_cmd_flag_none,
	  "= disconnect and wait for reassociate/reconnect command before\n"
	  "  connecting" },
	{ "reconnect", wpa_cli_cmd_reconnect, NULL,
	  cli_cmd_flag_none,
	  "= like reassociate, but only takes effect if already disconnected"
	},
	{ "scan", wpa_cli_cmd_scan, NULL,
	  cli_cmd_flag_none,
	  "= request new BSS scan" },
	{ "scan_results", wpa_cli_cmd_scan_results, NULL,
	  cli_cmd_flag_none,
	  "= get latest scan results" },
	{ "abort_scan", wpa_cli_cmd_abort_scan, NULL,
	  cli_cmd_flag_none,
	  "= request ongoing scan to be aborted" },
	{ "bss", wpa_cli_cmd_bss, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "<<idx> | <bssid>> = get detailed scan result info" },
	{ "get_capability", wpa_cli_cmd_get_capability,
	  wpa_cli_complete_get_capability, cli_cmd_flag_none,
	  "<eap/pairwise/group/key_mgmt/proto/auth_alg/channels/freq/modes> "
	  "= get capabilities" },
	{ "reconfigure", wpa_cli_cmd_reconfigure, NULL,
	  cli_cmd_flag_none,
	  "= force wpa_supplicant to re-read its configuration file" },
	{ "terminate", wpa_cli_cmd_terminate, NULL,
	  cli_cmd_flag_none,
	  "= terminate wpa_supplicant" },
	{ "interface_add", wpa_cli_cmd_interface_add, NULL,
	  cli_cmd_flag_none,
	  "<ifname> <confname> <driver> <ctrl_interface> <driver_param>\n"
	  "  <bridge_name> <create> <type> = adds new interface, all "
	  "parameters but\n"
	  "  <ifname> are optional. Supported types are station ('sta') and "
	  "AP ('ap')" },
	{ "interface_remove", wpa_cli_cmd_interface_remove, NULL,
	  cli_cmd_flag_none,
	  "<ifname> = removes the interface" },
	{ "interface_list", wpa_cli_cmd_interface_list, NULL,
	  cli_cmd_flag_none,
	  "= list available interfaces" },
	{ "ap_scan", wpa_cli_cmd_ap_scan, NULL,
	  cli_cmd_flag_none,
	  "<value> = set ap_scan parameter" },
	{ "scan_interval", wpa_cli_cmd_scan_interval, NULL,
	  cli_cmd_flag_none,
	  "<value> = set scan_interval parameter (in seconds)" },
	{ "bss_expire_age", wpa_cli_cmd_bss_expire_age, NULL,
	  cli_cmd_flag_none,
	  "<value> = set BSS expiration age parameter" },
	{ "bss_expire_count", wpa_cli_cmd_bss_expire_count, NULL,
	  cli_cmd_flag_none,
	  "<value> = set BSS expiration scan count parameter" },
	{ "bss_flush", wpa_cli_cmd_bss_flush, NULL,
	  cli_cmd_flag_none,
	  "<value> = set BSS flush age (0 by default)" },
	{ "ft_ds", wpa_cli_cmd_ft_ds, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "<addr> = request over-the-DS FT with <addr>" },
	{ "wps_pbc", wpa_cli_cmd_wps_pbc, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "[BSSID] = start Wi-Fi Protected Setup: Push Button Configuration" },
	{ "wps_pin", wpa_cli_cmd_wps_pin, wpa_cli_complete_bss,
	  cli_cmd_flag_sensitive,
	  "<BSSID> [PIN] = start WPS PIN method (returns PIN, if not "
	  "hardcoded)" },
	{ "wps_check_pin", wpa_cli_cmd_wps_check_pin, NULL,
	  cli_cmd_flag_sensitive,
	  "<PIN> = verify PIN checksum" },
	{ "wps_cancel", wpa_cli_cmd_wps_cancel, NULL, cli_cmd_flag_none,
	  "Cancels the pending WPS operation" },
#ifdef CONFIG_WPS_NFC
	{ "wps_nfc", wpa_cli_cmd_wps_nfc, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "[BSSID] = start Wi-Fi Protected Setup: NFC" },
	{ "wps_nfc_config_token", wpa_cli_cmd_wps_nfc_config_token, NULL,
	  cli_cmd_flag_none,
	  "<WPS|NDEF> = build configuration token" },
	{ "wps_nfc_token", wpa_cli_cmd_wps_nfc_token, NULL,
	  cli_cmd_flag_none,
	  "<WPS|NDEF> = create password token" },
	{ "wps_nfc_tag_read", wpa_cli_cmd_wps_nfc_tag_read, NULL,
	  cli_cmd_flag_sensitive,
	  "<hexdump of payload> = report read NFC tag with WPS data" },
	{ "nfc_get_handover_req", wpa_cli_cmd_nfc_get_handover_req, NULL,
	  cli_cmd_flag_none,
	  "<NDEF> <WPS> = create NFC handover request" },
	{ "nfc_get_handover_sel", wpa_cli_cmd_nfc_get_handover_sel, NULL,
	  cli_cmd_flag_none,
	  "<NDEF> <WPS> = create NFC handover select" },
	{ "nfc_report_handover", wpa_cli_cmd_nfc_report_handover, NULL,
	  cli_cmd_flag_none,
	  "<role> <type> <hexdump of req> <hexdump of sel> = report completed "
	  "NFC handover" },
#endif /* CONFIG_WPS_NFC */
	{ "wps_reg", wpa_cli_cmd_wps_reg, wpa_cli_complete_bss,
	  cli_cmd_flag_sensitive,
	  "<BSSID> <AP PIN> = start WPS Registrar to configure an AP" },
	{ "wps_ap_pin", wpa_cli_cmd_wps_ap_pin, NULL,
	  cli_cmd_flag_sensitive,
	  "[params..] = enable/disable AP PIN" },
	{ "wps_er_start", wpa_cli_cmd_wps_er_start, NULL,
	  cli_cmd_flag_none,
	  "[IP address] = start Wi-Fi Protected Setup External Registrar" },
	{ "wps_er_stop", wpa_cli_cmd_wps_er_stop, NULL,
	  cli_cmd_flag_none,
	  "= stop Wi-Fi Protected Setup External Registrar" },
	{ "wps_er_pin", wpa_cli_cmd_wps_er_pin, NULL,
	  cli_cmd_flag_sensitive,
	  "<UUID> <PIN> = add an Enrollee PIN to External Registrar" },
	{ "wps_er_pbc", wpa_cli_cmd_wps_er_pbc, NULL,
	  cli_cmd_flag_none,
	  "<UUID> = accept an Enrollee PBC using External Registrar" },
	{ "wps_er_learn", wpa_cli_cmd_wps_er_learn, NULL,
	  cli_cmd_flag_sensitive,
	  "<UUID> <PIN> = learn AP configuration" },
	{ "wps_er_set_config", wpa_cli_cmd_wps_er_set_config, NULL,
	  cli_cmd_flag_none,
	  "<UUID> <network id> = set AP configuration for enrolling" },
	{ "wps_er_config", wpa_cli_cmd_wps_er_config, NULL,
	  cli_cmd_flag_sensitive,
	  "<UUID> <PIN> <SSID> <auth> <encr> <key> = configure AP" },
#ifdef CONFIG_WPS_NFC
	{ "wps_er_nfc_config_token", wpa_cli_cmd_wps_er_nfc_config_token, NULL,
	  cli_cmd_flag_none,
	  "<WPS/NDEF> <UUID> = build NFC configuration token" },
#endif /* CONFIG_WPS_NFC */
	{ "ibss_rsn", wpa_cli_cmd_ibss_rsn, NULL,
	  cli_cmd_flag_none,
	  "<addr> = request RSN authentication with <addr> in IBSS" },
#ifdef CONFIG_AP
	{ "sta", wpa_cli_cmd_sta, wpa_cli_complete_sta,
	  cli_cmd_flag_none,
	  "<addr> = get information about an associated station (AP)" },
	{ "all_sta", wpa_cli_cmd_all_sta, NULL,
	  cli_cmd_flag_none,
	  "= get information about all associated stations (AP)" },
	{ "list_sta", wpa_cli_cmd_list_sta, NULL,
	  cli_cmd_flag_none,
	  "= list all stations (AP)" },
	{ "deauthenticate", wpa_cli_cmd_deauthenticate,
	  wpa_cli_complete_deauthenticate, cli_cmd_flag_none,
	  "<addr> = deauthenticate a station" },
	{ "disassociate", wpa_cli_cmd_disassociate,
	  wpa_cli_complete_disassociate, cli_cmd_flag_none,
	  "<addr> = disassociate a station" },
	{ "chan_switch", wpa_cli_cmd_chanswitch, NULL,
	  cli_cmd_flag_none,
	  "<cs_count> <freq> [sec_channel_offset=] [center_freq1=]"
	  " [center_freq2=] [bandwidth=] [blocktx] [ht|vht]"
	  " = CSA parameters" },
#endif /* CONFIG_AP */
	{ "suspend", wpa_cli_cmd_suspend, NULL, cli_cmd_flag_none,
	  "= notification of suspend/hibernate" },
	{ "resume", wpa_cli_cmd_resume, NULL, cli_cmd_flag_none,
	  "= notification of resume/thaw" },
#ifdef CONFIG_TESTING_OPTIONS
	{ "drop_sa", wpa_cli_cmd_drop_sa, NULL, cli_cmd_flag_none,
	  "= drop SA without deauth/disassoc (test command)" },
#endif /* CONFIG_TESTING_OPTIONS */
	{ "roam", wpa_cli_cmd_roam, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "<addr> = roam to the specified BSS" },
#ifdef CONFIG_MESH
	{ "mesh_interface_add", wpa_cli_cmd_mesh_interface_add, NULL,
	  cli_cmd_flag_none,
	  "[ifname] = Create a new mesh interface" },
	{ "mesh_group_add", wpa_cli_cmd_mesh_group_add, NULL,
	  cli_cmd_flag_none,
	  "<network id> = join a mesh network (disable others)" },
	{ "mesh_group_remove", wpa_cli_cmd_mesh_group_remove, NULL,
	  cli_cmd_flag_none,
	  "<ifname> = Remove mesh group interface" },
	{ "mesh_peer_remove", wpa_cli_cmd_mesh_peer_remove, NULL,
	  cli_cmd_flag_none,
	  "<addr> = Remove a mesh peer" },
	{ "mesh_peer_add", wpa_cli_cmd_mesh_peer_add, NULL,
	  cli_cmd_flag_none,
	  "<addr> [duration=<seconds>] = Add a mesh peer" },
#endif /* CONFIG_MESH */
#ifdef CONFIG_P2P
	{ "p2p_find", wpa_cli_cmd_p2p_find, wpa_cli_complete_p2p_find,
	  cli_cmd_flag_none,
	  "[timeout] [type=*] = find P2P Devices for up-to timeout seconds" },
	{ "p2p_stop_find", wpa_cli_cmd_p2p_stop_find, NULL, cli_cmd_flag_none,
	  "= stop P2P Devices search" },
	{ "p2p_asp_provision", wpa_cli_cmd_p2p_asp_provision, NULL,
	  cli_cmd_flag_none,
	  "<addr> adv_id=<adv_id> conncap=<conncap> [info=<infodata>] = provision with a P2P ASP Device" },
	{ "p2p_asp_provision_resp", wpa_cli_cmd_p2p_asp_provision_resp, NULL,
	  cli_cmd_flag_none,
	  "<addr> adv_id=<adv_id> [role<conncap>] [info=<infodata>] = provision with a P2P ASP Device" },
	{ "p2p_connect", wpa_cli_cmd_p2p_connect, wpa_cli_complete_p2p_connect,
	  cli_cmd_flag_none,
	  "<addr> <\"pbc\"|PIN> [ht40] = connect to a P2P Device" },
	{ "p2p_listen", wpa_cli_cmd_p2p_listen, NULL, cli_cmd_flag_none,
	  "[timeout] = listen for P2P Devices for up-to timeout seconds" },
	{ "p2p_group_remove", wpa_cli_cmd_p2p_group_remove,
	  wpa_cli_complete_p2p_group_remove, cli_cmd_flag_none,
	  "<ifname> = remove P2P group interface (terminate group if GO)" },
	{ "p2p_group_add", wpa_cli_cmd_p2p_group_add, NULL, cli_cmd_flag_none,
	  "[ht40] = add a new P2P group (local end as GO)" },
	{ "p2p_group_member", wpa_cli_cmd_p2p_group_member, NULL,
	  cli_cmd_flag_none,
	  "<dev_addr> = Get peer interface address on local GO using peer Device Address" },
	{ "p2p_prov_disc", wpa_cli_cmd_p2p_prov_disc,
	  wpa_cli_complete_p2p_peer, cli_cmd_flag_none,
	  "<addr> <method> = request provisioning discovery" },
	{ "p2p_get_passphrase", wpa_cli_cmd_p2p_get_passphrase, NULL,
	  cli_cmd_flag_none,
	  "= get the passphrase for a group (GO only)" },
	{ "p2p_serv_disc_req", wpa_cli_cmd_p2p_serv_disc_req,
	  wpa_cli_complete_p2p_peer, cli_cmd_flag_none,
	  "<addr> <TLVs> = schedule service discovery request" },
	{ "p2p_serv_disc_cancel_req", wpa_cli_cmd_p2p_serv_disc_cancel_req,
	  NULL, cli_cmd_flag_none,
	  "<id> = cancel pending service discovery request" },
	{ "p2p_serv_disc_resp", wpa_cli_cmd_p2p_serv_disc_resp, NULL,
	  cli_cmd_flag_none,
	  "<freq> <addr> <dialog token> <TLVs> = service discovery response" },
	{ "p2p_service_update", wpa_cli_cmd_p2p_service_update, NULL,
	  cli_cmd_flag_none,
	  "= indicate change in local services" },
	{ "p2p_serv_disc_external", wpa_cli_cmd_p2p_serv_disc_external, NULL,
	  cli_cmd_flag_none,
	  "<external> = set external processing of service discovery" },
	{ "p2p_service_flush", wpa_cli_cmd_p2p_service_flush, NULL,
	  cli_cmd_flag_none,
	  "= remove all stored service entries" },
	{ "p2p_service_add", wpa_cli_cmd_p2p_service_add, NULL,
	  cli_cmd_flag_none,
	  "<bonjour|upnp|asp> <query|version> <response|service> = add a local "
	  "service" },
	{ "p2p_service_rep", wpa_cli_cmd_p2p_service_rep, NULL,
	  cli_cmd_flag_none,
	  "asp <auto> <adv_id> <svc_state> <svc_string> [<svc_info>] = replace "
	  "local ASP service" },
	{ "p2p_service_del", wpa_cli_cmd_p2p_service_del, NULL,
	  cli_cmd_flag_none,
	  "<bonjour|upnp> <query|version> [|service] = remove a local "
	  "service" },
	{ "p2p_reject", wpa_cli_cmd_p2p_reject, wpa_cli_complete_p2p_peer,
	  cli_cmd_flag_none,
	  "<addr> = reject connection attempts from a specific peer" },
	{ "p2p_invite", wpa_cli_cmd_p2p_invite, NULL,
	  cli_cmd_flag_none,
	  "<cmd> [peer=addr] = invite peer" },
	{ "p2p_peers", wpa_cli_cmd_p2p_peers, NULL, cli_cmd_flag_none,
	  "[discovered] = list known (optionally, only fully discovered) P2P "
	  "peers" },
	{ "p2p_peer", wpa_cli_cmd_p2p_peer, wpa_cli_complete_p2p_peer,
	  cli_cmd_flag_none,
	  "<address> = show information about known P2P peer" },
	{ "p2p_set", wpa_cli_cmd_p2p_set, wpa_cli_complete_p2p_set,
	  cli_cmd_flag_none,
	  "<field> <value> = set a P2P parameter" },
	{ "p2p_flush", wpa_cli_cmd_p2p_flush, NULL, cli_cmd_flag_none,
	  "= flush P2P state" },
	{ "p2p_cancel", wpa_cli_cmd_p2p_cancel, NULL, cli_cmd_flag_none,
	  "= cancel P2P group formation" },
	{ "p2p_unauthorize", wpa_cli_cmd_p2p_unauthorize,
	  wpa_cli_complete_p2p_peer, cli_cmd_flag_none,
	  "<address> = unauthorize a peer" },
	{ "p2p_presence_req", wpa_cli_cmd_p2p_presence_req, NULL,
	  cli_cmd_flag_none,
	  "[<duration> <interval>] [<duration> <interval>] = request GO "
	  "presence" },
	{ "p2p_ext_listen", wpa_cli_cmd_p2p_ext_listen, NULL,
	  cli_cmd_flag_none,
	  "[<period> <interval>] = set extended listen timing" },
	{ "p2p_remove_client", wpa_cli_cmd_p2p_remove_client,
	  wpa_cli_complete_p2p_peer, cli_cmd_flag_none,
	  "<address|iface=address> = remove a peer from all groups" },
#endif /* CONFIG_P2P */
	{ "vendor_elem_add", wpa_cli_cmd_vendor_elem_add, NULL,
	  cli_cmd_flag_none,
	  "<frame id> <hexdump of elem(s)> = add vendor specific IEs to frame(s)\n"
	  VENDOR_ELEM_FRAME_ID },
	{ "vendor_elem_get", wpa_cli_cmd_vendor_elem_get, NULL,
	  cli_cmd_flag_none,
	  "<frame id> = get vendor specific IE(s) to frame(s)\n"
	  VENDOR_ELEM_FRAME_ID },
	{ "vendor_elem_remove", wpa_cli_cmd_vendor_elem_remove, NULL,
	  cli_cmd_flag_none,
	  "<frame id> <hexdump of elem(s)> = remove vendor specific IE(s) in frame(s)\n"
	  VENDOR_ELEM_FRAME_ID },
#ifdef CONFIG_WIFI_DISPLAY
	{ "wfd_subelem_set", wpa_cli_cmd_wfd_subelem_set, NULL,
	  cli_cmd_flag_none,
	  "<subelem> [contents] = set Wi-Fi Display subelement" },
	{ "wfd_subelem_get", wpa_cli_cmd_wfd_subelem_get, NULL,
	  cli_cmd_flag_none,
	  "<subelem> = get Wi-Fi Display subelement" },
#endif /* CONFIG_WIFI_DISPLAY */
#ifdef CONFIG_INTERWORKING
	{ "fetch_anqp", wpa_cli_cmd_fetch_anqp, NULL, cli_cmd_flag_none,
	  "= fetch ANQP information for all APs" },
	{ "stop_fetch_anqp", wpa_cli_cmd_stop_fetch_anqp, NULL,
	  cli_cmd_flag_none,
	  "= stop fetch_anqp operation" },
	{ "interworking_select", wpa_cli_cmd_interworking_select, NULL,
	  cli_cmd_flag_none,
	  "[auto] = perform Interworking network selection" },
	{ "interworking_connect", wpa_cli_cmd_interworking_connect,
	  wpa_cli_complete_bss, cli_cmd_flag_none,
	  "<BSSID> = connect using Interworking credentials" },
	{ "interworking_add_network", wpa_cli_cmd_interworking_add_network,
	  wpa_cli_complete_bss, cli_cmd_flag_none,
	  "<BSSID> = connect using Interworking credentials" },
	{ "anqp_get", wpa_cli_cmd_anqp_get, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "<addr> <info id>[,<info id>]... = request ANQP information" },
	{ "gas_request", wpa_cli_cmd_gas_request, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "<addr> <AdvProtoID> [QueryReq] = GAS request" },
	{ "gas_response_get", wpa_cli_cmd_gas_response_get,
	  wpa_cli_complete_bss, cli_cmd_flag_none,
	  "<addr> <dialog token> [start,len] = Fetch last GAS response" },
#endif /* CONFIG_INTERWORKING */
#ifdef CONFIG_HS20
	{ "hs20_anqp_get", wpa_cli_cmd_hs20_anqp_get, wpa_cli_complete_bss,
	  cli_cmd_flag_none,
	  "<addr> <subtype>[,<subtype>]... = request HS 2.0 ANQP information"
	},
	{ "nai_home_realm_list", wpa_cli_cmd_get_nai_home_realm_list,
	  wpa_cli_complete_bss, cli_cmd_flag_none,
	  "<addr> <home realm> = get HS20 nai home realm list" },
	{ "hs20_icon_request", wpa_cli_cmd_hs20_icon_request,
	  wpa_cli_complete_bss, cli_cmd_flag_none,
	  "<addr> <icon name> = get Hotspot 2.0 OSU icon" },
	{ "fetch_osu", wpa_cli_cmd_fetch_osu, NULL, cli_cmd_flag_none,
	  "= fetch OSU provider information from all APs" },
	{ "cancel_fetch_osu", wpa_cli_cmd_cancel_fetch_osu, NULL,
	  cli_cmd_flag_none,
	  "= cancel fetch_osu command" },
#endif /* CONFIG_HS20 */
	{ "sta_autoconnect", wpa_cli_cmd_sta_autoconnect, NULL,
	  cli_cmd_flag_none,
	  "<0/1> = disable/enable automatic reconnection" },
	{ "tdls_discover", wpa_cli_cmd_tdls_discover, NULL,
	  cli_cmd_flag_none,
	  "<addr> = request TDLS discovery with <addr>" },
	{ "tdls_setup", wpa_cli_cmd_tdls_setup, NULL,
	  cli_cmd_flag_none,
	  "<addr> = request TDLS setup with <addr>" },
	{ "tdls_teardown", wpa_cli_cmd_tdls_teardown, NULL,
	  cli_cmd_flag_none,
	  "<addr> = tear down TDLS with <addr>" },
	{ "tdls_link_status", wpa_cli_cmd_tdls_link_status, NULL,
	  cli_cmd_flag_none,
	  "<addr> = TDLS link status with <addr>" },
	{ "wmm_ac_addts", wpa_cli_cmd_wmm_ac_addts, NULL,
	  cli_cmd_flag_none,
	  "<uplink/downlink/bidi> <tsid=0..7> <up=0..7> [nominal_msdu_size=#] "
	  "[mean_data_rate=#] [min_phy_rate=#] [sba=#] [fixed_nominal_msdu] "
	  "= add WMM-AC traffic stream" },
	{ "wmm_ac_delts", wpa_cli_cmd_wmm_ac_delts, NULL,
	  cli_cmd_flag_none,
	  "<tsid> = delete WMM-AC traffic stream" },
	{ "wmm_ac_status", wpa_cli_cmd_wmm_ac_status, NULL,
	  cli_cmd_flag_none,
	  "= show status for Wireless Multi-Media Admission-Control" },
	{ "tdls_chan_switch", wpa_cli_cmd_tdls_chan_switch, NULL,
	  cli_cmd_flag_none,
	  "<addr> <oper class> <freq> [sec_channel_offset=] [center_freq1=] "
	  "[center_freq2=] [bandwidth=] [ht|vht] = enable channel switching "
	  "with TDLS peer" },
	{ "tdls_cancel_chan_switch", wpa_cli_cmd_tdls_cancel_chan_switch, NULL,
	  cli_cmd_flag_none,
	  "<addr> = disable channel switching with TDLS peer <addr>" },
	{ "signal_poll", wpa_cli_cmd_signal_poll, NULL,
	  cli_cmd_flag_none,
	  "= get signal parameters" },
	{ "signal_monitor", wpa_cli_cmd_signal_monitor, NULL,
	  cli_cmd_flag_none,
	  "= set signal monitor parameters" },
	{ "pktcnt_poll", wpa_cli_cmd_pktcnt_poll, NULL,
	  cli_cmd_flag_none,
	  "= get TX/RX packet counters" },
	{ "reauthenticate", wpa_cli_cmd_reauthenticate, NULL,
	  cli_cmd_flag_none,
	  "= trigger IEEE 802.1X/EAPOL reauthentication" },
#ifdef CONFIG_AUTOSCAN
	{ "autoscan", wpa_cli_cmd_autoscan, NULL, cli_cmd_flag_none,
	  "[params] = Set or unset (if none) autoscan parameters" },
#endif /* CONFIG_AUTOSCAN */
#ifdef CONFIG_WNM
	{ "wnm_sleep", wpa_cli_cmd_wnm_sleep, NULL, cli_cmd_flag_none,
	  "<enter/exit> [interval=#] = enter/exit WNM-Sleep mode" },
	{ "wnm_bss_query", wpa_cli_cmd_wnm_bss_query, NULL, cli_cmd_flag_none,
	  "<query reason> [list]"
	  " [neighbor=<BSSID>,<BSSID information>,<operating class>,<channel number>,<PHY type>[,<hexdump of optional subelements>]"
	  " = Send BSS Transition Management Query" },
#endif /* CONFIG_WNM */
	{ "raw", wpa_cli_cmd_raw, NULL, cli_cmd_flag_sensitive,
	  "<params..> = Sent unprocessed command" },
	{ "flush", wpa_cli_cmd_flush, NULL, cli_cmd_flag_none,
	  "= flush wpa_supplicant state" },
#ifdef ANDROID
	{ "driver", wpa_cli_cmd_driver, NULL, cli_cmd_flag_none,
	  "<command> = driver private commands" },
#endif /* ANDROID */
	{ "radio_work", wpa_cli_cmd_radio_work, NULL, cli_cmd_flag_none,
	  "= radio_work <show/add/done>" },
	{ "vendor", wpa_cli_cmd_vendor, NULL, cli_cmd_flag_none,
	  "<vendor id> <command id> [<hex formatted command argument>] = Send vendor command"
	},
	{ "neighbor_rep_request",
	  wpa_cli_cmd_neighbor_rep_request, NULL, cli_cmd_flag_none,
	  "[ssid=<SSID>] [lci] [civic] = Trigger request to AP for neighboring AP report (with optional given SSID in hex or enclosed in double quotes, default: current SSID; with optional LCI and location civic request)"
	},
	{ "erp_flush", wpa_cli_cmd_erp_flush, NULL, cli_cmd_flag_none,
	  "= flush ERP keys" },
	{ "mac_rand_scan",
	  wpa_cli_cmd_mac_rand_scan, NULL, cli_cmd_flag_none,
	  "<scan|sched|pno|all> enable=<0/1> [addr=mac-address "
	  "mask=mac-address-mask] = scan MAC randomization"
	},
	{ "get_pref_freq_list", wpa_cli_cmd_get_pref_freq_list, NULL,
	  cli_cmd_flag_none,
	  "<interface type> = retrieve preferred freq list for the specified interface type" },
	{ "p2p_lo_start", wpa_cli_cmd_p2p_lo_start, NULL,
	  cli_cmd_flag_none,
	  "<freq> <period> <interval> <count> = start P2P listen offload" },
	{ "p2p_lo_stop", wpa_cli_cmd_p2p_lo_stop, NULL,
	  cli_cmd_flag_none,
	  "= stop P2P listen offload" },
#ifdef CONFIG_DPP
	{ "dpp_qr_code", wpa_cli_cmd_dpp_qr_code, NULL, cli_cmd_flag_none,
	  "report a scanned DPP URI from a QR Code" },
	{ "dpp_bootstrap_gen", wpa_cli_cmd_dpp_bootstrap_gen, NULL,
	  cli_cmd_flag_sensitive,
	  "type=<qrcode> [chan=..] [mac=..] [info=..] [curve=..] [key=..] = generate DPP bootstrap information" },
	{ "dpp_bootstrap_remove", wpa_cli_cmd_dpp_bootstrap_remove, NULL,
	  cli_cmd_flag_none,
	  "*|<id> = remove DPP bootstrap information" },
	{ "dpp_bootstrap_get_uri", wpa_cli_cmd_dpp_bootstrap_get_uri, NULL,
	  cli_cmd_flag_none,
	  "<id> = get DPP bootstrap URI" },
	{ "dpp_bootstrap_info", wpa_cli_cmd_dpp_bootstrap_info, NULL,
	  cli_cmd_flag_none,
	  "<id> = show DPP bootstrap information" },
	{ "dpp_auth_init", wpa_cli_cmd_dpp_auth_init, NULL, cli_cmd_flag_none,
	  "peer=<id> [own=<id>] = initiate DPP bootstrapping" },
	{ "dpp_listen", wpa_cli_cmd_dpp_listen, NULL, cli_cmd_flag_none,
	  "<freq in MHz> = start DPP listen" },
	{ "dpp_stop_listen", wpa_cli_cmd_dpp_stop_listen, NULL,
	  cli_cmd_flag_none,
	  "= stop DPP listen" },
	{ "dpp_configurator_add", wpa_cli_cmd_dpp_configurator_add, NULL,
	  cli_cmd_flag_sensitive,
	  "[curve=..] [key=..] = add DPP configurator" },
	{ "dpp_configurator_remove", wpa_cli_cmd_dpp_configurator_remove, NULL,
	  cli_cmd_flag_none,
	  "*|<id> = remove DPP configurator" },
	{ "dpp_configurator_get_key", wpa_cli_cmd_dpp_configurator_get_key,
	  NULL, cli_cmd_flag_none,
	  "<id> = Get DPP configurator's private key" },
	{ "dpp_pkex_add", wpa_cli_cmd_dpp_pkex_add, NULL,
	  cli_cmd_flag_sensitive,
	  "add PKEX code" },
	{ "dpp_pkex_remove", wpa_cli_cmd_dpp_pkex_remove, NULL,
	  cli_cmd_flag_none,
	  "*|<id> = remove DPP pkex information" },
#endif /* CONFIG_DPP */
	{ NULL, NULL, NULL, cli_cmd_flag_none, NULL }
};


/*
 * Prints command usage, lines are padded with the specified string.
 */
static void print_cmd_help(const struct wpa_cli_cmd *cmd, const char *pad)
{
	char c;
	size_t n;

	printf("%s%s ", pad, cmd->cmd);
	for (n = 0; (c = cmd->usage[n]); n++) {
		printf("%c", c);
		if (c == '\n')
			printf("%s", pad);
	}
	printf("\n");
}


static void print_help(const char *cmd)
{
	int n;
	printf("commands:\n");
	for (n = 0; wpa_cli_commands[n].cmd; n++) {
		if (cmd == NULL || str_starts(wpa_cli_commands[n].cmd, cmd))
			print_cmd_help(&wpa_cli_commands[n], "  ");
	}
}


static int wpa_cli_edit_filter_history_cb(void *ctx, const char *cmd)
{
	const char *c, *delim;
	int n;
	size_t len;

	delim = os_strchr(cmd, ' ');
	if (delim)
		len = delim - cmd;
	else
		len = os_strlen(cmd);

	for (n = 0; (c = wpa_cli_commands[n].cmd); n++) {
		if (os_strncasecmp(cmd, c, len) == 0 && len == os_strlen(c))
			return (wpa_cli_commands[n].flags &
				cli_cmd_flag_sensitive);
	}
	return 0;
}


static char ** wpa_list_cmd_list(void)
{
	char **res;
	int i, count;
	struct cli_txt_entry *e;

	count = ARRAY_SIZE(wpa_cli_commands);
	count += dl_list_len(&p2p_groups);
	count += dl_list_len(&ifnames);
	res = os_calloc(count + 1, sizeof(char *));
	if (res == NULL)
		return NULL;

	for (i = 0; wpa_cli_commands[i].cmd; i++) {
		res[i] = os_strdup(wpa_cli_commands[i].cmd);
		if (res[i] == NULL)
			break;
	}

	dl_list_for_each(e, &p2p_groups, struct cli_txt_entry, list) {
		size_t len = 8 + os_strlen(e->txt);
		res[i] = os_malloc(len);
		if (res[i] == NULL)
			break;
		os_snprintf(res[i], len, "ifname=%s", e->txt);
		i++;
	}

	dl_list_for_each(e, &ifnames, struct cli_txt_entry, list) {
		res[i] = os_strdup(e->txt);
		if (res[i] == NULL)
			break;
		i++;
	}

	return res;
}


static char ** wpa_cli_cmd_completion(const char *cmd, const char *str,
				      int pos)
{
	int i;

	for (i = 0; wpa_cli_commands[i].cmd; i++) {
		if (os_strcasecmp(wpa_cli_commands[i].cmd, cmd) == 0) {
			if (wpa_cli_commands[i].completion)
				return wpa_cli_commands[i].completion(str,
								      pos);
			edit_clear_line();
			printf("\r%s\n", wpa_cli_commands[i].usage);
			edit_redraw();
			break;
		}
	}

	return NULL;
}


static char ** wpa_cli_edit_completion_cb(void *ctx, const char *str, int pos)
{
	char **res;
	const char *end;
	char *cmd;

	if (pos > 7 && os_strncasecmp(str, "IFNAME=", 7) == 0) {
		end = os_strchr(str, ' ');
		if (end && pos > end - str) {
			pos -= end - str + 1;
			str = end + 1;
		}
	}

	end = os_strchr(str, ' ');
	if (end == NULL || str + pos < end)
		return wpa_list_cmd_list();

	cmd = os_malloc(pos + 1);
	if (cmd == NULL)
		return NULL;
	os_memcpy(cmd, str, pos);
	cmd[end - str] = '\0';
	res = wpa_cli_cmd_completion(cmd, str, pos);
	os_free(cmd);
	return res;
}


static int wpa_request(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	const struct wpa_cli_cmd *cmd, *match = NULL;
	int count;
	int ret = 0;

	if (argc > 1 && os_strncasecmp(argv[0], "IFNAME=", 7) == 0) {
		ifname_prefix = argv[0] + 7;
		argv = &argv[1];
		argc--;
	} else
		ifname_prefix = NULL;

	if (argc == 0)
		return -1;

	count = 0;
	cmd = wpa_cli_commands;
	while (cmd->cmd) {
		if (os_strncasecmp(cmd->cmd, argv[0], os_strlen(argv[0])) == 0)
		{
			match = cmd;
			if (os_strcasecmp(cmd->cmd, argv[0]) == 0) {
				/* we have an exact match */
				count = 1;
				break;
			}
			count++;
		}
		cmd++;
	}

	if (count > 1) {
		printf("Ambiguous command '%s'; possible commands:", argv[0]);
		cmd = wpa_cli_commands;
		while (cmd->cmd) {
			if (os_strncasecmp(cmd->cmd, argv[0],
					   os_strlen(argv[0])) == 0) {
				printf(" %s", cmd->cmd);
			}
			cmd++;
		}
		printf("\n");
		ret = 1;
	} else if (count == 0) {
		printf("Unknown command '%s'\n", argv[0]);
		ret = 1;
	} else {
		ret = match->handler(ctrl, argc - 1, &argv[1]);
	}

	return ret;
}


static int wpa_cli_exec(const char *program, const char *arg1,
			const char *arg2)
{
	char *arg;
	size_t len;
	int res;

	/* If no interface is specified, set the global */
	if (!arg1)
		arg1 = "global";

	len = os_strlen(arg1) + os_strlen(arg2) + 2;
	arg = os_malloc(len);
	if (arg == NULL)
		return -1;
	os_snprintf(arg, len, "%s %s", arg1, arg2);
	res = os_exec(program, arg, 1);
	os_free(arg);

	return res;
}


static void wpa_cli_action_process(const char *msg)
{
	const char *pos;
	char *copy = NULL, *id, *pos2;
	const char *ifname = ctrl_ifname;
	char ifname_buf[100];

	if (eloop_terminated())
		return;

	pos = msg;
	if (os_strncmp(pos, "IFNAME=", 7) == 0) {
		const char *end;
		end = os_strchr(pos + 7, ' ');
		if (end && (unsigned int) (end - pos) < sizeof(ifname_buf)) {
			pos += 7;
			os_memcpy(ifname_buf, pos, end - pos);
			ifname_buf[end - pos] = '\0';
			ifname = ifname_buf;
			pos = end + 1;
		}
	}
	if (*pos == '<') {
		const char *prev = pos;
		/* skip priority */
		pos = os_strchr(pos, '>');
		if (pos)
			pos++;
		else
			pos = prev;
	}

	if (str_starts(pos, WPA_EVENT_CONNECTED)) {
		int new_id = -1;
		os_unsetenv("WPA_ID");
		os_unsetenv("WPA_ID_STR");
		os_unsetenv("WPA_CTRL_DIR");

		pos = os_strstr(pos, "[id=");
		if (pos)
			copy = os_strdup(pos + 4);

		if (copy) {
			pos2 = id = copy;
			while (*pos2 && *pos2 != ' ')
				pos2++;
			*pos2++ = '\0';
			new_id = atoi(id);
			os_setenv("WPA_ID", id, 1);
			while (*pos2 && *pos2 != '=')
				pos2++;
			if (*pos2 == '=')
				pos2++;
			id = pos2;
			while (*pos2 && *pos2 != ']')
				pos2++;
			*pos2 = '\0';
			os_setenv("WPA_ID_STR", id, 1);
			os_free(copy);
		}

		os_setenv("WPA_CTRL_DIR", ctrl_iface_dir, 1);

		if (wpa_cli_connected <= 0 || new_id != wpa_cli_last_id) {
			wpa_cli_connected = 1;
			wpa_cli_last_id = new_id;
			wpa_cli_exec(action_file, ifname, "CONNECTED");
		}
	} else if (str_starts(pos, WPA_EVENT_DISCONNECTED)) {
		if (wpa_cli_connected) {
			wpa_cli_connected = 0;
			wpa_cli_exec(action_file, ifname, "DISCONNECTED");
		}
	} else if (str_starts(pos, AP_EVENT_ENABLED)) {
		wpa_cli_exec(action_file, ctrl_ifname, pos);
	} else if (str_starts(pos, AP_EVENT_DISABLED)) {
		wpa_cli_exec(action_file, ctrl_ifname, pos);
	} else if (str_starts(pos, MESH_GROUP_STARTED)) {
		wpa_cli_exec(action_file, ctrl_ifname, pos);
	} else if (str_starts(pos, MESH_GROUP_REMOVED)) {
		wpa_cli_exec(action_file, ctrl_ifname, pos);
	} else if (str_starts(pos, MESH_PEER_CONNECTED)) {
		wpa_cli_exec(action_file, ctrl_ifname, pos);
	} else if (str_starts(pos, MESH_PEER_DISCONNECTED)) {
		wpa_cli_exec(action_file, ctrl_ifname, pos);
	} else if (str_starts(pos, P2P_EVENT_GROUP_STARTED)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, P2P_EVENT_GROUP_REMOVED)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, P2P_EVENT_CROSS_CONNECT_ENABLE)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, P2P_EVENT_CROSS_CONNECT_DISABLE)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, P2P_EVENT_GO_NEG_FAILURE)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, WPS_EVENT_SUCCESS)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, WPS_EVENT_ACTIVE)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, WPS_EVENT_TIMEOUT)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, WPS_EVENT_FAIL)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, AP_STA_CONNECTED)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, AP_STA_DISCONNECTED)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, ESS_DISASSOC_IMMINENT)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, HS20_SUBSCRIPTION_REMEDIATION)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, HS20_DEAUTH_IMMINENT_NOTICE)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, HS20_T_C_ACCEPTANCE)) {
		wpa_cli_exec(action_file, ifname, pos);
	} else if (str_starts(pos, WPA_EVENT_TERMINATING)) {
		printf("wpa_supplicant is terminating - stop monitoring\n");
		wpa_cli_quit = 1;
	}
}


#ifndef CONFIG_ANSI_C_EXTRA
static void wpa_cli_action_cb(char *msg, size_t len)
{
	wpa_cli_action_process(msg);
}
#endif /* CONFIG_ANSI_C_EXTRA */


static void wpa_cli_reconnect(void)
{
	wpa_cli_close_connection();
	if (wpa_cli_open_connection(ctrl_ifname, 1) < 0)
		return;

	if (interactive) {
		edit_clear_line();
		printf("\rConnection to wpa_supplicant re-established\n");
		edit_redraw();
		update_stations(ctrl_conn);
	}
}


static void cli_event(const char *str)
{
	const char *start, *s;

	start = os_strchr(str, '>');
	if (start == NULL)
		return;

	start++;

	if (str_starts(start, WPA_EVENT_BSS_ADDED)) {
		s = os_strchr(start, ' ');
		if (s == NULL)
			return;
		s = os_strchr(s + 1, ' ');
		if (s == NULL)
			return;
		cli_txt_list_add(&bsses, s + 1);
		return;
	}

	if (str_starts(start, WPA_EVENT_BSS_REMOVED)) {
		s = os_strchr(start, ' ');
		if (s == NULL)
			return;
		s = os_strchr(s + 1, ' ');
		if (s == NULL)
			return;
		cli_txt_list_del_addr(&bsses, s + 1);
		return;
	}

#ifdef CONFIG_P2P
	if (str_starts(start, P2P_EVENT_DEVICE_FOUND)) {
		s = os_strstr(start, " p2p_dev_addr=");
		if (s == NULL)
			return;
		cli_txt_list_add_addr(&p2p_peers, s + 14);
		return;
	}

	if (str_starts(start, P2P_EVENT_DEVICE_LOST)) {
		s = os_strstr(start, " p2p_dev_addr=");
		if (s == NULL)
			return;
		cli_txt_list_del_addr(&p2p_peers, s + 14);
		return;
	}

	if (str_starts(start, P2P_EVENT_GROUP_STARTED)) {
		s = os_strchr(start, ' ');
		if (s == NULL)
			return;
		cli_txt_list_add_word(&p2p_groups, s + 1, ' ');
		return;
	}

	if (str_starts(start, P2P_EVENT_GROUP_REMOVED)) {
		s = os_strchr(start, ' ');
		if (s == NULL)
			return;
		cli_txt_list_del_word(&p2p_groups, s + 1, ' ');
		return;
	}
#endif /* CONFIG_P2P */
}


static int check_terminating(const char *msg)
{
	const char *pos = msg;

	if (*pos == '<') {
		/* skip priority */
		pos = os_strchr(pos, '>');
		if (pos)
			pos++;
		else
			pos = msg;
	}

	if (str_starts(pos, WPA_EVENT_TERMINATING) && ctrl_conn) {
		edit_clear_line();
		printf("\rConnection to wpa_supplicant lost - trying to "
		       "reconnect\n");
		edit_redraw();
		wpa_cli_attached = 0;
		wpa_cli_close_connection();
		return 1;
	}

	return 0;
}


static void wpa_cli_recv_pending(struct wpa_ctrl *ctrl, int action_monitor)
{
	if (ctrl_conn == NULL) {
		wpa_cli_reconnect();
		return;
	}
	while (wpa_ctrl_pending(ctrl) > 0) {
		char buf[4096];
		size_t len = sizeof(buf) - 1;
		if (wpa_ctrl_recv(ctrl, buf, &len) == 0) {
			buf[len] = '\0';
			if (action_monitor)
				wpa_cli_action_process(buf);
			else {
				cli_event(buf);
				if (wpa_cli_show_event(buf)) {
					edit_clear_line();
					printf("\r%s\n", buf);
					edit_redraw();
				}

				if (interactive && check_terminating(buf) > 0)
					return;
			}
		} else {
			printf("Could not read pending message.\n");
			break;
		}
	}

	if (wpa_ctrl_pending(ctrl) < 0) {
		printf("Connection to wpa_supplicant lost - trying to "
		       "reconnect\n");
		wpa_cli_reconnect();
	}
}


static void wpa_cli_ping(void *eloop_ctx, void *timeout_ctx)
{
	if (ctrl_conn) {
		int res;
		char *prefix = ifname_prefix;

		ifname_prefix = NULL;
		res = _wpa_ctrl_command(ctrl_conn, "PING", 0);
		ifname_prefix = prefix;
		if (res) {
			printf("Connection to wpa_supplicant lost - trying to "
			       "reconnect\n");
			wpa_cli_close_connection();
		}
	}
	if (!ctrl_conn)
		wpa_cli_reconnect();
	eloop_register_timeout(ping_interval, 0, wpa_cli_ping, NULL, NULL);
}


static void wpa_cli_mon_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	wpa_cli_recv_pending(mon_conn, 0);
}


static void wpa_cli_edit_cmd_cb(void *ctx, char *cmd)
{
	char *argv[max_args];
	int argc;
	argc = tokenize_cmd(cmd, argv);
	if (argc)
		wpa_request(ctrl_conn, argc, argv);
}


static void wpa_cli_edit_eof_cb(void *ctx)
{
	eloop_terminate();
}


static int warning_displayed = 0;
static char *hfile = NULL;
static int edit_started = 0;

static void start_edit(void)
{
	char *home;
	char *ps = NULL;

#ifdef CONFIG_CTRL_IFACE_UDP_REMOTE
	ps = wpa_ctrl_get_remote_ifname(ctrl_conn);
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */

#ifdef CONFIG_WPA_CLI_HISTORY_DIR
	home = CONFIG_WPA_CLI_HISTORY_DIR;
#else /* CONFIG_WPA_CLI_HISTORY_DIR */
	home = getenv("HOME");
#endif /* CONFIG_WPA_CLI_HISTORY_DIR */
	if (home) {
		const char *fname = ".wpa_cli_history";
		int hfile_len = os_strlen(home) + 1 + os_strlen(fname) + 1;
		hfile = os_malloc(hfile_len);
		if (hfile)
			os_snprintf(hfile, hfile_len, "%s/%s", home, fname);
	}

	if (edit_init(wpa_cli_edit_cmd_cb, wpa_cli_edit_eof_cb,
		      wpa_cli_edit_completion_cb, NULL, hfile, ps) < 0) {
		eloop_terminate();
		return;
	}

	edit_started = 1;
	eloop_register_timeout(ping_interval, 0, wpa_cli_ping, NULL, NULL);
}


static void update_bssid_list(struct wpa_ctrl *ctrl)
{
	char buf[4096];
	size_t len = sizeof(buf);
	int ret;
	const char *cmd = "BSS RANGE=ALL MASK=0x2";
	char *pos, *end;

	if (ctrl == NULL)
		return;
	ret = wpa_ctrl_request(ctrl, cmd, os_strlen(cmd), buf, &len, NULL);
	if (ret < 0)
		return;
	buf[len] = '\0';

	pos = buf;
	while (pos) {
		pos = os_strstr(pos, "bssid=");
		if (pos == NULL)
			break;
		pos += 6;
		end = os_strchr(pos, '\n');
		if (end == NULL)
			break;
		*end = '\0';
		cli_txt_list_add(&bsses, pos);
		pos = end + 1;
	}
}


static void update_ifnames(struct wpa_ctrl *ctrl)
{
	char buf[4096];
	size_t len = sizeof(buf);
	int ret;
	const char *cmd = "INTERFACES";
	char *pos, *end;
	char txt[200];

	cli_txt_list_flush(&ifnames);

	if (ctrl == NULL)
		return;
	ret = wpa_ctrl_request(ctrl, cmd, os_strlen(cmd), buf, &len, NULL);
	if (ret < 0)
		return;
	buf[len] = '\0';

	pos = buf;
	while (pos) {
		end = os_strchr(pos, '\n');
		if (end == NULL)
			break;
		*end = '\0';
		ret = os_snprintf(txt, sizeof(txt), "ifname=%s", pos);
		if (!os_snprintf_error(sizeof(txt), ret))
			cli_txt_list_add(&ifnames, txt);
		pos = end + 1;
	}
}


static void update_creds(struct wpa_ctrl *ctrl)
{
	char buf[4096];
	size_t len = sizeof(buf);
	int ret;
	const char *cmd = "LIST_CREDS";
	char *pos, *end;
	int header = 1;

	cli_txt_list_flush(&creds);

	if (ctrl == NULL)
		return;
	ret = wpa_ctrl_request(ctrl, cmd, os_strlen(cmd), buf, &len, NULL);
	if (ret < 0)
		return;
	buf[len] = '\0';

	pos = buf;
	while (pos) {
		end = os_strchr(pos, '\n');
		if (end == NULL)
			break;
		*end = '\0';
		if (!header)
			cli_txt_list_add_word(&creds, pos, '\t');
		header = 0;
		pos = end + 1;
	}
}


static void update_networks(struct wpa_ctrl *ctrl)
{
	char buf[4096];
	size_t len = sizeof(buf);
	int ret;
	const char *cmd = "LIST_NETWORKS";
	char *pos, *end;
	int header = 1;

	cli_txt_list_flush(&networks);

	if (ctrl == NULL)
		return;
	ret = wpa_ctrl_request(ctrl, cmd, os_strlen(cmd), buf, &len, NULL);
	if (ret < 0)
		return;
	buf[len] = '\0';

	pos = buf;
	while (pos) {
		end = os_strchr(pos, '\n');
		if (end == NULL)
			break;
		*end = '\0';
		if (!header)
			cli_txt_list_add_word(&networks, pos, '\t');
		header = 0;
		pos = end + 1;
	}
}


static void update_stations(struct wpa_ctrl *ctrl)
{
#ifdef CONFIG_AP
	char addr[32], cmd[64];

	if (!ctrl || !interactive)
		return;

	cli_txt_list_flush(&stations);

	if (wpa_ctrl_command_sta(ctrl, "STA-FIRST", addr, sizeof(addr), 0))
		return;
	do {
		if (os_strcmp(addr, "") != 0)
			cli_txt_list_add(&stations, addr);
		os_snprintf(cmd, sizeof(cmd), "STA-NEXT %s", addr);
	} while (wpa_ctrl_command_sta(ctrl, cmd, addr, sizeof(addr), 0) == 0);
#endif /* CONFIG_AP */
}


static void try_connection(void *eloop_ctx, void *timeout_ctx)
{
	if (ctrl_conn)
		goto done;

	if (ctrl_ifname == NULL)
		ctrl_ifname = wpa_cli_get_default_ifname();

	if (wpa_cli_open_connection(ctrl_ifname, 1)) {
		if (!warning_displayed) {
			printf("Could not connect to wpa_supplicant: "
			       "%s - re-trying\n",
			       ctrl_ifname ? ctrl_ifname : "(nil)");
			warning_displayed = 1;
		}
		eloop_register_timeout(1, 0, try_connection, NULL, NULL);
		return;
	}

	update_bssid_list(ctrl_conn);
	update_creds(ctrl_conn);
	update_networks(ctrl_conn);
	update_stations(ctrl_conn);

	if (warning_displayed)
		printf("Connection established.\n");

done:
	start_edit();
}


static void wpa_cli_interactive(void)
{
	printf("\nInteractive mode\n\n");

	eloop_register_timeout(0, 0, try_connection, NULL, NULL);
	eloop_run();
	eloop_cancel_timeout(try_connection, NULL, NULL);

	cli_txt_list_flush(&p2p_peers);
	cli_txt_list_flush(&p2p_groups);
	cli_txt_list_flush(&bsses);
	cli_txt_list_flush(&ifnames);
	cli_txt_list_flush(&creds);
	cli_txt_list_flush(&networks);
	if (edit_started)
		edit_deinit(hfile, wpa_cli_edit_filter_history_cb);
	os_free(hfile);
	eloop_cancel_timeout(wpa_cli_ping, NULL, NULL);
	wpa_cli_close_connection();
}


static void wpa_cli_action_ping(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_ctrl *ctrl = eloop_ctx;
	char buf[256];
	size_t len;

	/* verify that connection is still working */
	len = sizeof(buf) - 1;
	if (wpa_ctrl_request(ctrl, "PING", 4, buf, &len,
			     wpa_cli_action_cb) < 0 ||
	    len < 4 || os_memcmp(buf, "PONG", 4) != 0) {
		printf("wpa_supplicant did not reply to PING command - exiting\n");
		eloop_terminate();
		return;
	}
	eloop_register_timeout(ping_interval, 0, wpa_cli_action_ping,
			       ctrl, NULL);
}


static void wpa_cli_action_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct wpa_ctrl *ctrl = eloop_ctx;

	wpa_cli_recv_pending(ctrl, 1);
}


static void wpa_cli_action(struct wpa_ctrl *ctrl)
{
#ifdef CONFIG_ANSI_C_EXTRA
	/* TODO: ANSI C version(?) */
	printf("Action processing not supported in ANSI C build.\n");
#else /* CONFIG_ANSI_C_EXTRA */
	int fd;

	fd = wpa_ctrl_get_fd(ctrl);
	eloop_register_timeout(ping_interval, 0, wpa_cli_action_ping,
			       ctrl, NULL);
	eloop_register_read_sock(fd, wpa_cli_action_receive, ctrl, NULL);
	eloop_run();
	eloop_cancel_timeout(wpa_cli_action_ping, ctrl, NULL);
	eloop_unregister_read_sock(fd);
#endif /* CONFIG_ANSI_C_EXTRA */
}


static void wpa_cli_cleanup(void)
{
	wpa_cli_close_connection();
	if (pid_file)
		os_daemonize_terminate(pid_file);

	os_program_deinit();
}


static void wpa_cli_terminate(int sig, void *ctx)
{
	eloop_terminate();
}


static char * wpa_cli_get_default_ifname(void)
{
	char *ifname = NULL;

#ifdef ANDROID
	char ifprop[PROPERTY_VALUE_MAX];
	if (property_get("wifi.interface", ifprop, NULL) != 0) {
		ifname = os_strdup(ifprop);
		printf("Using interface '%s'\n", ifname ? ifname : "N/A");
	}
#else /* ANDROID */
#ifdef CONFIG_CTRL_IFACE_UNIX
	struct dirent *dent;
	DIR *dir = opendir(ctrl_iface_dir);
	if (!dir) {
		return NULL;
	}
	while ((dent = readdir(dir))) {
#ifdef _DIRENT_HAVE_D_TYPE
		/*
		 * Skip the file if it is not a socket. Also accept
		 * DT_UNKNOWN (0) in case the C library or underlying
		 * file system does not support d_type.
		 */
		if (dent->d_type != DT_SOCK && dent->d_type != DT_UNKNOWN)
			continue;
#endif /* _DIRENT_HAVE_D_TYPE */
		if (os_strcmp(dent->d_name, ".") == 0 ||
		    os_strcmp(dent->d_name, "..") == 0)
			continue;
		printf("Selected interface '%s'\n", dent->d_name);
		ifname = os_strdup(dent->d_name);
		break;
	}
	closedir(dir);
#endif /* CONFIG_CTRL_IFACE_UNIX */

#ifdef CONFIG_CTRL_IFACE_NAMED_PIPE
	char buf[4096], *pos;
	size_t len;
	struct wpa_ctrl *ctrl;
	int ret;

	ctrl = wpa_ctrl_open(NULL);
	if (ctrl == NULL)
		return NULL;

	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, "INTERFACES", 10, buf, &len, NULL);
	if (ret >= 0) {
		buf[len] = '\0';
		pos = os_strchr(buf, '\n');
		if (pos)
			*pos = '\0';
		ifname = os_strdup(buf);
	}
	wpa_ctrl_close(ctrl);
#endif /* CONFIG_CTRL_IFACE_NAMED_PIPE */
#endif /* ANDROID */

	return ifname;
}


int main(int argc, char *argv[])
{
	int c;
	int daemonize = 0;
	int ret = 0;
	const char *global = NULL;

	if (os_program_init())
		return -1;

	for (;;) {
		c = getopt(argc, argv, "a:Bg:G:hi:p:P:s:v");
		if (c < 0)
			break;
		switch (c) {
		case 'a':
			action_file = optarg;
			break;
		case 'B':
			daemonize = 1;
			break;
		case 'g':
			global = optarg;
			break;
		case 'G':
			ping_interval = atoi(optarg);
			break;
		case 'h':
			usage();
			return 0;
		case 'v':
			printf("%s\n", wpa_cli_version);
			return 0;
		case 'i':
			os_free(ctrl_ifname);
			ctrl_ifname = os_strdup(optarg);
			break;
		case 'p':
			ctrl_iface_dir = optarg;
			break;
		case 'P':
			pid_file = optarg;
			break;
		case 's':
			client_socket_dir = optarg;
			break;
		default:
			usage();
			return -1;
		}
	}

	interactive = (argc == optind) && (action_file == NULL);

	if (interactive)
		printf("%s\n\n%s\n\n", wpa_cli_version, cli_license);

	if (eloop_init())
		return -1;

	if (global) {
#ifdef CONFIG_CTRL_IFACE_NAMED_PIPE
		ctrl_conn = wpa_ctrl_open(NULL);
#else /* CONFIG_CTRL_IFACE_NAMED_PIPE */
		ctrl_conn = wpa_ctrl_open(global);
#endif /* CONFIG_CTRL_IFACE_NAMED_PIPE */
		if (ctrl_conn == NULL) {
			fprintf(stderr, "Failed to connect to wpa_supplicant "
				"global interface: %s  error: %s\n",
				global, strerror(errno));
			return -1;
		}

		if (interactive) {
			update_ifnames(ctrl_conn);
			mon_conn = wpa_ctrl_open(global);
			if (mon_conn) {
				if (wpa_ctrl_attach(mon_conn) == 0) {
					wpa_cli_attached = 1;
					eloop_register_read_sock(
						wpa_ctrl_get_fd(mon_conn),
						wpa_cli_mon_receive,
						NULL, NULL);
				} else {
					printf("Failed to open monitor "
					       "connection through global "
					       "control interface\n");
				}
			}
			update_stations(ctrl_conn);
		}
	}

	eloop_register_signal_terminate(wpa_cli_terminate, NULL);

	if (ctrl_ifname == NULL)
		ctrl_ifname = wpa_cli_get_default_ifname();

	if (interactive) {
		wpa_cli_interactive();
	} else {
		if (!global &&
		    wpa_cli_open_connection(ctrl_ifname, 0) < 0) {
			fprintf(stderr, "Failed to connect to non-global "
				"ctrl_ifname: %s  error: %s\n",
				ctrl_ifname ? ctrl_ifname : "(nil)",
				strerror(errno));
			return -1;
		}

		if (action_file) {
			if (wpa_ctrl_attach(ctrl_conn) == 0) {
				wpa_cli_attached = 1;
			} else {
				printf("Warning: Failed to attach to "
				       "wpa_supplicant.\n");
				return -1;
			}
		}

		if (daemonize && os_daemonize(pid_file) && eloop_sock_requeue())
			return -1;

		if (action_file)
			wpa_cli_action(ctrl_conn);
		else
			ret = wpa_request(ctrl_conn, argc - optind,
					  &argv[optind]);
	}

	os_free(ctrl_ifname);
	eloop_destroy();
	wpa_cli_cleanup();

	return ret;
}

#else /* CONFIG_CTRL_IFACE */
int main(int argc, char *argv[])
{
	printf("CONFIG_CTRL_IFACE not defined - wpa_cli disabled\n");
	return -1;
}
#endif /* CONFIG_CTRL_IFACE */
