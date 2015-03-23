/*
 * hostapd - command line interface for hostapd daemon
 * Copyright (c) 2004-2011, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#include <dirent.h>

#include "common/wpa_ctrl.h"
#include "common.h"
#include "common/version.h"


static const char *hostapd_cli_version =
"hostapd_cli v" VERSION_STR "\n"
"Copyright (c) 2004-2011, Jouni Malinen <j@w1.fi> and contributors";


static const char *hostapd_cli_license =
"This program is free software. You can distribute it and/or modify it\n"
"under the terms of the GNU General Public License version 2.\n"
"\n"
"Alternatively, this software may be distributed under the terms of the\n"
"BSD license. See README and COPYING for more details.\n";

static const char *hostapd_cli_full_license =
"This program is free software; you can redistribute it and/or modify\n"
"it under the terms of the GNU General Public License version 2 as\n"
"published by the Free Software Foundation.\n"
"\n"
"This program is distributed in the hope that it will be useful,\n"
"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
"GNU General Public License for more details.\n"
"\n"
"You should have received a copy of the GNU General Public License\n"
"along with this program; if not, write to the Free Software\n"
"Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA\n"
"\n"
"Alternatively, this software may be distributed under the terms of the\n"
"BSD license.\n"
"\n"
"Redistribution and use in source and binary forms, with or without\n"
"modification, are permitted provided that the following conditions are\n"
"met:\n"
"\n"
"1. Redistributions of source code must retain the above copyright\n"
"   notice, this list of conditions and the following disclaimer.\n"
"\n"
"2. Redistributions in binary form must reproduce the above copyright\n"
"   notice, this list of conditions and the following disclaimer in the\n"
"   documentation and/or other materials provided with the distribution.\n"
"\n"
"3. Neither the name(s) of the above-listed copyright holder(s) nor the\n"
"   names of its contributors may be used to endorse or promote products\n"
"   derived from this software without specific prior written permission.\n"
"\n"
"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n"
"\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n"
"LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n"
"A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n"
"OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
"SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n"
"LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
"DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
"THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
"(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
"OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
"\n";

static const char *commands_help =
"Commands:\n"
"   mib                  get MIB variables (dot1x, dot11, radius)\n"
"   sta <addr>           get MIB variables for one station\n"
"   all_sta              get MIB variables for all stations\n"
"   new_sta <addr>       add a new station\n"
"   deauthenticate <addr>  deauthenticate a station\n"
"   disassociate <addr>  disassociate a station\n"
#ifdef CONFIG_IEEE80211W
"   sa_query <addr>      send SA Query to a station\n"
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_WPS
"   wps_pin <uuid> <pin> [timeout] [addr]  add WPS Enrollee PIN\n"
"   wps_check_pin <PIN>  verify PIN checksum\n"
"   wps_pbc              indicate button pushed to initiate PBC\n"
#ifdef CONFIG_WPS_OOB
"   wps_oob <type> <path> <method>  use WPS with out-of-band (UFD)\n"
#endif /* CONFIG_WPS_OOB */
"   wps_ap_pin <cmd> [params..]  enable/disable AP PIN\n"
"   wps_config <SSID> <auth> <encr> <key>  configure AP\n"
#endif /* CONFIG_WPS */
"   get_config           show current configuration\n"
"   help                 show this usage help\n"
"   interface [ifname]   show interfaces/select interface\n"
"   level <debug level>  change debug level\n"
"   license              show full hostapd_cli license\n"
"   quit                 exit hostapd_cli\n";

static struct wpa_ctrl *ctrl_conn;
static int hostapd_cli_quit = 0;
static int hostapd_cli_attached = 0;
static const char *ctrl_iface_dir = "/var/run/hostapd";
static char *ctrl_ifname = NULL;
static const char *pid_file = NULL;
static const char *action_file = NULL;
static int ping_interval = 5;


static void usage(void)
{
	fprintf(stderr, "%s\n", hostapd_cli_version);
	fprintf(stderr,
		"\n"
		"usage: hostapd_cli [-p<path>] [-i<ifname>] [-hvB] "
		"[-a<path>] \\\n"
		"                   [-G<ping interval>] [command..]\n"
		"\n"
		"Options:\n"
		"   -h           help (show this usage text)\n"
		"   -v           shown version information\n"
		"   -p<path>     path to find control sockets (default: "
		"/var/run/hostapd)\n"
		"   -a<file>     run in daemon mode executing the action file "
		"based on events\n"
		"                from hostapd\n"
		"   -B           run a daemon in the background\n"
		"   -i<ifname>   Interface to listen on (default: first "
		"interface found in the\n"
		"                socket path)\n\n"
		"%s",
		commands_help);
}


static struct wpa_ctrl * hostapd_cli_open_connection(const char *ifname)
{
	char *cfile;
	int flen;

	if (ifname == NULL)
		return NULL;

	flen = strlen(ctrl_iface_dir) + strlen(ifname) + 2;
	cfile = malloc(flen);
	if (cfile == NULL)
		return NULL;
	snprintf(cfile, flen, "%s/%s", ctrl_iface_dir, ifname);

	ctrl_conn = wpa_ctrl_open(cfile);
	free(cfile);
	return ctrl_conn;
}


static void hostapd_cli_close_connection(void)
{
	if (ctrl_conn == NULL)
		return;

	if (hostapd_cli_attached) {
		wpa_ctrl_detach(ctrl_conn);
		hostapd_cli_attached = 0;
	}
	wpa_ctrl_close(ctrl_conn);
	ctrl_conn = NULL;
}


static void hostapd_cli_msg_cb(char *msg, size_t len)
{
	printf("%s\n", msg);
}


static int _wpa_ctrl_command(struct wpa_ctrl *ctrl, char *cmd, int print)
{
	char buf[4096];
	size_t len;
	int ret;

	if (ctrl_conn == NULL) {
		printf("Not connected to hostapd - command dropped.\n");
		return -1;
	}
	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len,
			       hostapd_cli_msg_cb);
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
	}
	return 0;
}


static inline int wpa_ctrl_command(struct wpa_ctrl *ctrl, char *cmd)
{
	return _wpa_ctrl_command(ctrl, cmd, 1);
}


static int hostapd_cli_cmd_ping(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "PING");
}


static int hostapd_cli_cmd_relog(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "RELOG");
}


static int hostapd_cli_cmd_mib(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "MIB");
}


static int hostapd_cli_exec(const char *program, const char *arg1,
			    const char *arg2)
{
	char *cmd;
	size_t len;
	int res;
	int ret = 0;

	len = os_strlen(program) + os_strlen(arg1) + os_strlen(arg2) + 3;
	cmd = os_malloc(len);
	if (cmd == NULL)
		return -1;
	res = os_snprintf(cmd, len, "%s %s %s", program, arg1, arg2);
	if (res < 0 || (size_t) res >= len) {
		os_free(cmd);
		return -1;
	}
	cmd[len - 1] = '\0';
#ifndef _WIN32_WCE
	if (system(cmd) < 0)
		ret = -1;
#endif /* _WIN32_WCE */
	os_free(cmd);

	return ret;
}


static void hostapd_cli_action_process(char *msg, size_t len)
{
	const char *pos;

	pos = msg;
	if (*pos == '<') {
		pos = os_strchr(pos, '>');
		if (pos)
			pos++;
		else
			pos = msg;
	}

	hostapd_cli_exec(action_file, ctrl_ifname, pos);
}


static int hostapd_cli_cmd_sta(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char buf[64];
	if (argc != 1) {
		printf("Invalid 'sta' command - exactly one argument, STA "
		       "address, is required.\n");
		return -1;
	}
	snprintf(buf, sizeof(buf), "STA %s", argv[0]);
	return wpa_ctrl_command(ctrl, buf);
}


static int hostapd_cli_cmd_new_sta(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	char buf[64];
	if (argc != 1) {
		printf("Invalid 'new_sta' command - exactly one argument, STA "
		       "address, is required.\n");
		return -1;
	}
	snprintf(buf, sizeof(buf), "NEW_STA %s", argv[0]);
	return wpa_ctrl_command(ctrl, buf);
}


static int hostapd_cli_cmd_deauthenticate(struct wpa_ctrl *ctrl, int argc,
					  char *argv[])
{
	char buf[64];
	if (argc < 1) {
		printf("Invalid 'deauthenticate' command - exactly one "
		       "argument, STA address, is required.\n");
		return -1;
	}
	if (argc > 1)
		os_snprintf(buf, sizeof(buf), "DEAUTHENTICATE %s %s",
			    argv[0], argv[1]);
	else
		os_snprintf(buf, sizeof(buf), "DEAUTHENTICATE %s", argv[0]);
	return wpa_ctrl_command(ctrl, buf);
}


static int hostapd_cli_cmd_disassociate(struct wpa_ctrl *ctrl, int argc,
					char *argv[])
{
	char buf[64];
	if (argc < 1) {
		printf("Invalid 'disassociate' command - exactly one "
		       "argument, STA address, is required.\n");
		return -1;
	}
	if (argc > 1)
		os_snprintf(buf, sizeof(buf), "DISASSOCIATE %s %s",
			    argv[0], argv[1]);
	else
		os_snprintf(buf, sizeof(buf), "DISASSOCIATE %s", argv[0]);
	return wpa_ctrl_command(ctrl, buf);
}


#ifdef CONFIG_IEEE80211W
static int hostapd_cli_cmd_sa_query(struct wpa_ctrl *ctrl, int argc,
				    char *argv[])
{
	char buf[64];
	if (argc != 1) {
		printf("Invalid 'sa_query' command - exactly one argument, "
		       "STA address, is required.\n");
		return -1;
	}
	snprintf(buf, sizeof(buf), "SA_QUERY %s", argv[0]);
	return wpa_ctrl_command(ctrl, buf);
}
#endif /* CONFIG_IEEE80211W */


#ifdef CONFIG_WPS
static int hostapd_cli_cmd_wps_pin(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	char buf[256];
	if (argc < 2) {
		printf("Invalid 'wps_pin' command - at least two arguments, "
		       "UUID and PIN, are required.\n");
		return -1;
	}
	if (argc > 3)
		snprintf(buf, sizeof(buf), "WPS_PIN %s %s %s %s",
			 argv[0], argv[1], argv[2], argv[3]);
	else if (argc > 2)
		snprintf(buf, sizeof(buf), "WPS_PIN %s %s %s",
			 argv[0], argv[1], argv[2]);
	else
		snprintf(buf, sizeof(buf), "WPS_PIN %s %s", argv[0], argv[1]);
	return wpa_ctrl_command(ctrl, buf);
}


static int hostapd_cli_cmd_wps_check_pin(struct wpa_ctrl *ctrl, int argc,
					 char *argv[])
{
	char cmd[256];
	int res;

	if (argc != 1 && argc != 2) {
		printf("Invalid WPS_CHECK_PIN command: needs one argument:\n"
		       "- PIN to be verified\n");
		return -1;
	}

	if (argc == 2)
		res = os_snprintf(cmd, sizeof(cmd), "WPS_CHECK_PIN %s %s",
				  argv[0], argv[1]);
	else
		res = os_snprintf(cmd, sizeof(cmd), "WPS_CHECK_PIN %s",
				  argv[0]);
	if (res < 0 || (size_t) res >= sizeof(cmd) - 1) {
		printf("Too long WPS_CHECK_PIN command.\n");
		return -1;
	}
	return wpa_ctrl_command(ctrl, cmd);
}


static int hostapd_cli_cmd_wps_pbc(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "WPS_PBC");
}


#ifdef CONFIG_WPS_OOB
static int hostapd_cli_cmd_wps_oob(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	char cmd[256];
	int res;

	if (argc != 3 && argc != 4) {
		printf("Invalid WPS_OOB command: need three or four "
		       "arguments:\n"
		       "- DEV_TYPE: use 'ufd' or 'nfc'\n"
		       "- PATH: path of OOB device like '/mnt'\n"
		       "- METHOD: OOB method 'pin-e' or 'pin-r', "
		       "'cred'\n"
		       "- DEV_NAME: (only for NFC) device name like "
		       "'pn531'\n");
		return -1;
	}

	if (argc == 3)
		res = os_snprintf(cmd, sizeof(cmd), "WPS_OOB %s %s %s",
				  argv[0], argv[1], argv[2]);
	else
		res = os_snprintf(cmd, sizeof(cmd), "WPS_OOB %s %s %s %s",
				  argv[0], argv[1], argv[2], argv[3]);
	if (res < 0 || (size_t) res >= sizeof(cmd) - 1) {
		printf("Too long WPS_OOB command.\n");
		return -1;
	}
	return wpa_ctrl_command(ctrl, cmd);
}
#endif /* CONFIG_WPS_OOB */


static int hostapd_cli_cmd_wps_ap_pin(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	char buf[64];
	if (argc < 1) {
		printf("Invalid 'wps_ap_pin' command - at least one argument "
		       "is required.\n");
		return -1;
	}
	if (argc > 2)
		snprintf(buf, sizeof(buf), "WPS_AP_PIN %s %s %s",
			 argv[0], argv[1], argv[2]);
	else if (argc > 1)
		snprintf(buf, sizeof(buf), "WPS_AP_PIN %s %s",
			 argv[0], argv[1]);
	else
		snprintf(buf, sizeof(buf), "WPS_AP_PIN %s", argv[0]);
	return wpa_ctrl_command(ctrl, buf);
}


static int hostapd_cli_cmd_wps_config(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	char buf[256];
	char ssid_hex[2 * 32 + 1];
	char key_hex[2 * 64 + 1];
	int i;

	if (argc < 1) {
		printf("Invalid 'wps_config' command - at least two arguments "
		       "are required.\n");
		return -1;
	}

	ssid_hex[0] = '\0';
	for (i = 0; i < 32; i++) {
		if (argv[0][i] == '\0')
			break;
		os_snprintf(&ssid_hex[i * 2], 3, "%02x", argv[0][i]);
	}

	key_hex[0] = '\0';
	if (argc > 3) {
		for (i = 0; i < 64; i++) {
			if (argv[3][i] == '\0')
				break;
			os_snprintf(&key_hex[i * 2], 3, "%02x",
				    argv[3][i]);
		}
	}

	if (argc > 3)
		snprintf(buf, sizeof(buf), "WPS_CONFIG %s %s %s %s",
			 ssid_hex, argv[1], argv[2], key_hex);
	else if (argc > 2)
		snprintf(buf, sizeof(buf), "WPS_CONFIG %s %s %s",
			 ssid_hex, argv[1], argv[2]);
	else
		snprintf(buf, sizeof(buf), "WPS_CONFIG %s %s",
			 ssid_hex, argv[1]);
	return wpa_ctrl_command(ctrl, buf);
}
#endif /* CONFIG_WPS */


static int hostapd_cli_cmd_get_config(struct wpa_ctrl *ctrl, int argc,
				      char *argv[])
{
	return wpa_ctrl_command(ctrl, "GET_CONFIG");
}


static int wpa_ctrl_command_sta(struct wpa_ctrl *ctrl, char *cmd,
				char *addr, size_t addr_len)
{
	char buf[4096], *pos;
	size_t len;
	int ret;

	if (ctrl_conn == NULL) {
		printf("Not connected to hostapd - command dropped.\n");
		return -1;
	}
	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len,
			       hostapd_cli_msg_cb);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0) {
		printf("'%s' command failed.\n", cmd);
		return -1;
	}

	buf[len] = '\0';
	if (memcmp(buf, "FAIL", 4) == 0)
		return -1;
	printf("%s", buf);

	pos = buf;
	while (*pos != '\0' && *pos != '\n')
		pos++;
	*pos = '\0';
	os_strlcpy(addr, buf, addr_len);
	return 0;
}


static int hostapd_cli_cmd_all_sta(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	char addr[32], cmd[64];

	if (wpa_ctrl_command_sta(ctrl, "STA-FIRST", addr, sizeof(addr)))
		return 0;
	do {
		snprintf(cmd, sizeof(cmd), "STA-NEXT %s", addr);
	} while (wpa_ctrl_command_sta(ctrl, cmd, addr, sizeof(addr)) == 0);

	return -1;
}


static int hostapd_cli_cmd_help(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	printf("%s", commands_help);
	return 0;
}


static int hostapd_cli_cmd_license(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	printf("%s\n\n%s\n", hostapd_cli_version, hostapd_cli_full_license);
	return 0;
}


static int hostapd_cli_cmd_quit(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	hostapd_cli_quit = 1;
	return 0;
}


static int hostapd_cli_cmd_level(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256];
	if (argc != 1) {
		printf("Invalid LEVEL command: needs one argument (debug "
		       "level)\n");
		return 0;
	}
	snprintf(cmd, sizeof(cmd), "LEVEL %s", argv[0]);
	return wpa_ctrl_command(ctrl, cmd);
}


static void hostapd_cli_list_interfaces(struct wpa_ctrl *ctrl)
{
	struct dirent *dent;
	DIR *dir;

	dir = opendir(ctrl_iface_dir);
	if (dir == NULL) {
		printf("Control interface directory '%s' could not be "
		       "openned.\n", ctrl_iface_dir);
		return;
	}

	printf("Available interfaces:\n");
	while ((dent = readdir(dir))) {
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0)
			continue;
		printf("%s\n", dent->d_name);
	}
	closedir(dir);
}


static int hostapd_cli_cmd_interface(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	if (argc < 1) {
		hostapd_cli_list_interfaces(ctrl);
		return 0;
	}

	hostapd_cli_close_connection();
	free(ctrl_ifname);
	ctrl_ifname = strdup(argv[0]);

	if (hostapd_cli_open_connection(ctrl_ifname)) {
		printf("Connected to interface '%s.\n", ctrl_ifname);
		if (wpa_ctrl_attach(ctrl_conn) == 0) {
			hostapd_cli_attached = 1;
		} else {
			printf("Warning: Failed to attach to "
			       "hostapd.\n");
		}
	} else {
		printf("Could not connect to interface '%s' - re-trying\n",
			ctrl_ifname);
	}
	return 0;
}


static int hostapd_cli_cmd_set(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256];
	int res;

	if (argc != 2) {
		printf("Invalid SET command: needs two arguments (variable "
		       "name and value)\n");
		return -1;
	}

	res = os_snprintf(cmd, sizeof(cmd), "SET %s %s", argv[0], argv[1]);
	if (res < 0 || (size_t) res >= sizeof(cmd) - 1) {
		printf("Too long SET command.\n");
		return -1;
	}
	return wpa_ctrl_command(ctrl, cmd);
}


static int hostapd_cli_cmd_get(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256];
	int res;

	if (argc != 1) {
		printf("Invalid GET command: needs one argument (variable "
		       "name)\n");
		return -1;
	}

	res = os_snprintf(cmd, sizeof(cmd), "GET %s", argv[0]);
	if (res < 0 || (size_t) res >= sizeof(cmd) - 1) {
		printf("Too long GET command.\n");
		return -1;
	}
	return wpa_ctrl_command(ctrl, cmd);
}


struct hostapd_cli_cmd {
	const char *cmd;
	int (*handler)(struct wpa_ctrl *ctrl, int argc, char *argv[]);
};

static struct hostapd_cli_cmd hostapd_cli_commands[] = {
	{ "ping", hostapd_cli_cmd_ping },
	{ "mib", hostapd_cli_cmd_mib },
	{ "relog", hostapd_cli_cmd_relog },
	{ "sta", hostapd_cli_cmd_sta },
	{ "all_sta", hostapd_cli_cmd_all_sta },
	{ "new_sta", hostapd_cli_cmd_new_sta },
	{ "deauthenticate", hostapd_cli_cmd_deauthenticate },
	{ "disassociate", hostapd_cli_cmd_disassociate },
#ifdef CONFIG_IEEE80211W
	{ "sa_query", hostapd_cli_cmd_sa_query },
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_WPS
	{ "wps_pin", hostapd_cli_cmd_wps_pin },
	{ "wps_check_pin", hostapd_cli_cmd_wps_check_pin },
	{ "wps_pbc", hostapd_cli_cmd_wps_pbc },
#ifdef CONFIG_WPS_OOB
	{ "wps_oob", hostapd_cli_cmd_wps_oob },
#endif /* CONFIG_WPS_OOB */
	{ "wps_ap_pin", hostapd_cli_cmd_wps_ap_pin },
	{ "wps_config", hostapd_cli_cmd_wps_config },
#endif /* CONFIG_WPS */
	{ "get_config", hostapd_cli_cmd_get_config },
	{ "help", hostapd_cli_cmd_help },
	{ "interface", hostapd_cli_cmd_interface },
	{ "level", hostapd_cli_cmd_level },
	{ "license", hostapd_cli_cmd_license },
	{ "quit", hostapd_cli_cmd_quit },
	{ "set", hostapd_cli_cmd_set },
	{ "get", hostapd_cli_cmd_get },
	{ NULL, NULL }
};


static void wpa_request(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	struct hostapd_cli_cmd *cmd, *match = NULL;
	int count;

	count = 0;
	cmd = hostapd_cli_commands;
	while (cmd->cmd) {
		if (strncasecmp(cmd->cmd, argv[0], strlen(argv[0])) == 0) {
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
		cmd = hostapd_cli_commands;
		while (cmd->cmd) {
			if (strncasecmp(cmd->cmd, argv[0], strlen(argv[0])) ==
			    0) {
				printf(" %s", cmd->cmd);
			}
			cmd++;
		}
		printf("\n");
	} else if (count == 0) {
		printf("Unknown command '%s'\n", argv[0]);
	} else {
		match->handler(ctrl, argc - 1, &argv[1]);
	}
}


static void hostapd_cli_recv_pending(struct wpa_ctrl *ctrl, int in_read,
				     int action_monitor)
{
	int first = 1;
	if (ctrl_conn == NULL)
		return;
	while (wpa_ctrl_pending(ctrl)) {
		char buf[256];
		size_t len = sizeof(buf) - 1;
		if (wpa_ctrl_recv(ctrl, buf, &len) == 0) {
			buf[len] = '\0';
			if (action_monitor)
				hostapd_cli_action_process(buf, len);
			else {
				if (in_read && first)
					printf("\n");
				first = 0;
				printf("%s\n", buf);
			}
		} else {
			printf("Could not read pending message.\n");
			break;
		}
	}
}


static void hostapd_cli_interactive(void)
{
	const int max_args = 10;
	char cmd[256], *res, *argv[max_args], *pos;
	int argc;

	printf("\nInteractive mode\n\n");

	do {
		hostapd_cli_recv_pending(ctrl_conn, 0, 0);
		printf("> ");
		alarm(ping_interval);
		res = fgets(cmd, sizeof(cmd), stdin);
		alarm(0);
		if (res == NULL)
			break;
		pos = cmd;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		argc = 0;
		pos = cmd;
		for (;;) {
			while (*pos == ' ')
				pos++;
			if (*pos == '\0')
				break;
			argv[argc] = pos;
			argc++;
			if (argc == max_args)
				break;
			while (*pos != '\0' && *pos != ' ')
				pos++;
			if (*pos == ' ')
				*pos++ = '\0';
		}
		if (argc)
			wpa_request(ctrl_conn, argc, argv);
	} while (!hostapd_cli_quit);
}


static void hostapd_cli_cleanup(void)
{
	hostapd_cli_close_connection();
	if (pid_file)
		os_daemonize_terminate(pid_file);

	os_program_deinit();
}


static void hostapd_cli_terminate(int sig)
{
	hostapd_cli_cleanup();
	exit(0);
}


static void hostapd_cli_alarm(int sig)
{
	if (ctrl_conn && _wpa_ctrl_command(ctrl_conn, "PING", 0)) {
		printf("Connection to hostapd lost - trying to reconnect\n");
		hostapd_cli_close_connection();
	}
	if (!ctrl_conn) {
		ctrl_conn = hostapd_cli_open_connection(ctrl_ifname);
		if (ctrl_conn) {
			printf("Connection to hostapd re-established\n");
			if (wpa_ctrl_attach(ctrl_conn) == 0) {
				hostapd_cli_attached = 1;
			} else {
				printf("Warning: Failed to attach to "
				       "hostapd.\n");
			}
		}
	}
	if (ctrl_conn)
		hostapd_cli_recv_pending(ctrl_conn, 1, 0);
	alarm(ping_interval);
}


static void hostapd_cli_action(struct wpa_ctrl *ctrl)
{
	fd_set rfds;
	int fd, res;
	struct timeval tv;
	char buf[256];
	size_t len;

	fd = wpa_ctrl_get_fd(ctrl);

	while (!hostapd_cli_quit) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = ping_interval;
		tv.tv_usec = 0;
		res = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (res < 0 && errno != EINTR) {
			perror("select");
			break;
		}

		if (FD_ISSET(fd, &rfds))
			hostapd_cli_recv_pending(ctrl, 0, 1);
		else {
			len = sizeof(buf) - 1;
			if (wpa_ctrl_request(ctrl, "PING", 4, buf, &len,
					     hostapd_cli_action_process) < 0 ||
			    len < 4 || os_memcmp(buf, "PONG", 4) != 0) {
				printf("hostapd did not reply to PING "
				       "command - exiting\n");
				break;
			}
		}
	}
}


int main(int argc, char *argv[])
{
	int interactive;
	int warning_displayed = 0;
	int c;
	int daemonize = 0;

	if (os_program_init())
		return -1;

	for (;;) {
		c = getopt(argc, argv, "a:BhG:i:p:v");
		if (c < 0)
			break;
		switch (c) {
		case 'a':
			action_file = optarg;
			break;
		case 'B':
			daemonize = 1;
			break;
		case 'G':
			ping_interval = atoi(optarg);
			break;
		case 'h':
			usage();
			return 0;
		case 'v':
			printf("%s\n", hostapd_cli_version);
			return 0;
		case 'i':
			os_free(ctrl_ifname);
			ctrl_ifname = os_strdup(optarg);
			break;
		case 'p':
			ctrl_iface_dir = optarg;
			break;
		default:
			usage();
			return -1;
		}
	}

	interactive = (argc == optind) && (action_file == NULL);

	if (interactive) {
		printf("%s\n\n%s\n\n", hostapd_cli_version,
		       hostapd_cli_license);
	}

	for (;;) {
		if (ctrl_ifname == NULL) {
			struct dirent *dent;
			DIR *dir = opendir(ctrl_iface_dir);
			if (dir) {
				while ((dent = readdir(dir))) {
					if (os_strcmp(dent->d_name, ".") == 0
					    ||
					    os_strcmp(dent->d_name, "..") == 0)
						continue;
					printf("Selected interface '%s'\n",
					       dent->d_name);
					ctrl_ifname = os_strdup(dent->d_name);
					break;
				}
				closedir(dir);
			}
		}
		ctrl_conn = hostapd_cli_open_connection(ctrl_ifname);
		if (ctrl_conn) {
			if (warning_displayed)
				printf("Connection established.\n");
			break;
		}

		if (!interactive) {
			perror("Failed to connect to hostapd - "
			       "wpa_ctrl_open");
			return -1;
		}

		if (!warning_displayed) {
			printf("Could not connect to hostapd - re-trying\n");
			warning_displayed = 1;
		}
		os_sleep(1, 0);
		continue;
	}

	signal(SIGINT, hostapd_cli_terminate);
	signal(SIGTERM, hostapd_cli_terminate);
	signal(SIGALRM, hostapd_cli_alarm);

	if (interactive || action_file) {
		if (wpa_ctrl_attach(ctrl_conn) == 0) {
			hostapd_cli_attached = 1;
		} else {
			printf("Warning: Failed to attach to hostapd.\n");
			if (action_file)
				return -1;
		}
	}

	if (daemonize && os_daemonize(pid_file))
		return -1;

	if (interactive)
		hostapd_cli_interactive();
	else if (action_file)
		hostapd_cli_action(ctrl_conn);
	else
		wpa_request(ctrl_conn, argc - optind, &argv[optind]);

	os_free(ctrl_ifname);
	hostapd_cli_cleanup();
	return 0;
}
