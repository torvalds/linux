/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"

#if 1 // by bbelief
#include "wfa_p2p.h"
#endif

#ifdef __APPLE__
#define SCRIPT_PATH "/usr/local/sbin/"
#else /* __APPLE__ */
#define SCRIPT_PATH "/home/atheros/Atheros-P2P/scripts/"
#endif /* __APPLE__ */


int cmd_sta_p2p_reset_test(void)
{
       char *intf = "wlan0";
	struct wfa_cs_p2p_group *grp, *prev;
	char buf[256];

#if 0
	dut->wps_method = WFA_CS_WPS_NOT_READY;

	grp = dut->groups;
	while (grp) {
		prev = grp;
		grp = grp->next;

		snprintf(buf, sizeof(buf), "P2P_GROUP_REMOVE %s",
			 prev->ifname);
		wpa_command(intf, buf);
		p2p_group_remove(dut, prev->grpid);
	}
#endif

	wpa_command(intf, "P2P_GROUP_REMOVE *");
	wpa_command(intf, "P2P_STOP_FIND");
	wpa_command(intf, "P2P_FLUSH");
	wpa_command(intf, "P2P_SERVICE_FLUSH");
	wpa_command(intf, "P2P_SET ssid_postfix ");
	wpa_command(intf, "P2P_EXT_LISTEN");
	wpa_command(intf, "P2P_SET client_apsd disable");
	wpa_command(intf, "P2P_SET go_apsd disable");
	wpa_command(get_station_ifname(), "P2P_SET ps 98");
	wpa_command(get_station_ifname(), "P2P_SET ps 96");
	wpa_command(get_station_ifname(), "P2P_SET ps 0");
	wpa_command(intf, "SET ampdu 1");
#if 0    
	run_system(dut, "iptables -F INPUT");
	if (dut->arp_ipaddr[0]) {
		snprintf(buf, sizeof(buf), "ip nei del %s dev %s",
			 dut->arp_ipaddr, dut->arp_ifname);
		run_system(dut, buf);
		dut->arp_ipaddr[0] = '\0';
	}
	snprintf(buf, sizeof(buf), "ip nei flush dev %s",
		 get_station_ifname());
	run_system(dut, buf);
	dut->p2p_mode = P2P_IDLE;
	dut->client_uapsd = 0;

	remove_wpa_networks(intf);

	disconnect_station(dut);
#endif

	return 1;
}




int cmd_sta_set_p2p_test(void)
{
	const char *intf = "wlan0";
	char buf[256];

        
sigma_dut_print(DUT_MSG_INFO, "%s : %s", __func__, get_main_ifname());
        

        wpa_command(intf, "P2P_SET disabled 0");
        if (wpa_command(intf, "P2P_FIND") < 0)
                return -2;

        snprintf(buf, sizeof(buf), "P2P_SET discoverability %d",
			 1);
		if (wpa_command(intf, buf) < 0)
			return -2;        
                

	return 1;
}



int cmd_sta_get_p2p_dev_address_test(void)
{
       //char *intf = command->intf;
       char *intf = "wlan1";
	char buf[100];

	//start_sta_mode(dut);
	//if (get_wpa_status(intf, "p2p_device_address", buf, sizeof(buf)) < 0) {
	if (get_wpa_status(intf, "address", buf, sizeof(buf)) < 0)
		return -1;

        sigma_dut_print(DUT_MSG_INFO, "%s : %s", __func__, buf);

        //strcpy(resp->cmdru.devid/*[0]*/, buf);
	return 0;
}




int get_ip_config(struct sigma_dut *dut, const char *ifname, char *buf,
		  size_t buf_len);


static int run_system(struct sigma_dut *dut, const char *cmd)
{
	sigma_dut_print( DUT_MSG_DEBUG, "Running '%s'", cmd);
	return system(cmd);
}


static int p2p_group_add(struct sigma_dut *dut, const char *ifname,
			 int go, const char *grpid, const char *ssid)
{
	struct wfa_cs_p2p_group *grp;

	grp = malloc(sizeof(*grp));
	if (grp == NULL)
		return -1;
	memset(grp, 0, sizeof(*grp));
	strncpy(grp->ifname, ifname, IFNAMSIZ);
	grp->go = go;
	strncpy(grp->grpid, grpid, P2P_GRP_ID_LEN);
	strncpy(grp->ssid, ssid, sizeof(grp->ssid));

	grp->next = dut->groups;
	dut->groups = grp;

	return 0;
}


static int p2p_group_remove(struct sigma_dut *dut, const char *grpid)
{
	struct wfa_cs_p2p_group *grp, *prev;

	prev = NULL;
	grp = dut->groups;
	while (grp) {
		if (strcmp(grpid, grp->grpid) == 0) {
			if (prev)
				prev->next = grp->next;
			else
				dut->groups = grp->next;
			free(grp);
			return 0;
		}
		prev = grp;
		grp = grp->next;
	}
	return -1;
}


static struct wfa_cs_p2p_group * p2p_group_get(struct sigma_dut *dut,
					       const char *grpid)
{
	struct wfa_cs_p2p_group *grp;
	char buf[1000], buf2[1000], *ifname, *pos;
	char go_dev_addr[50];
	char ssid[33];

	for (grp = dut->groups; grp; grp = grp->next) {
		if (strcmp(grpid, grp->grpid) == 0)
			return grp;
	}

	/*
	 * No group found based on group id. As a workaround for GO Negotiation
	 * responder case where we do not store group id, try to find an active
	 * group that matches with the requested group id.
	 */

	pos = strchr(grpid, ' ');
	if (pos == NULL)
		return NULL;
	if (pos - grpid > sizeof(go_dev_addr))
		return NULL;
	memcpy(go_dev_addr, grpid, pos - grpid);
	go_dev_addr[pos - grpid] = '\0';
	strncpy(ssid, pos + 1, sizeof(ssid));
	ssid[sizeof(ssid) - 1] = '\0';
	printf("Trying to find suitable interface for group: go_dev_addr='%s' "
	       "grpid='%s'\n", go_dev_addr, grpid);

	if (wpa_command_resp(get_main_ifname(), "INTERFACES", buf, sizeof(buf))
	    < 0)
		return NULL;
	ifname = buf;
	while (ifname && *ifname) {
		int add = 0;
		int go = 0;
		pos = strchr(ifname, '\n');
		if (pos)
			*pos++ = '\0';
		printf("Considering interface '%s' for group\n", ifname);

		if (wpa_command_resp(ifname, "STATUS", buf2, sizeof(buf2)) ==
		    0) {
			if (strstr(buf2, ssid)) {
				printf("Selected interface '%s' based on "
				       "STATUS\n", ifname);
				add = 1;
			}
			if (strstr(buf2, "P2P GO"))
				go = 1;
		}

		if (wpa_command_resp(ifname, "LIST_NETWORKS", buf2,
				     sizeof(buf2)) == 0) {
			char *line, *end;
			line = buf2;
			while (line && *line) {
				end = strchr(line, ' ');
				if (end)
					*end++ = '\0';
				if (strstr(line, ssid) &&
				    strstr(line, "[CURRENT]")) {
					printf("Selected interface '%s' "
					       "based on LIST_NETWORKS\n",
					       ifname);
					add = 1;
					break;
				}
				line = end;
			}
		}

		if (add) {
			p2p_group_add(dut, ifname, go, grpid, ssid);
			return dut->groups;
		}

		ifname = pos;
	}

	return NULL;
}


static const char * get_group_ifname(struct sigma_dut *dut, const char *ifname)
{
	char buf[1000], *iface, *pos;
	char state[100];

	if (dut->groups) {
		sigma_dut_print( DUT_MSG_DEBUG, "%s: Use group interface "
				"%s instead of main interface %s",
				__func__, dut->groups->ifname, ifname);
		return dut->groups->ifname;
	}

	/* Try to find a suitable group interface */
	if (wpa_command_resp(get_main_ifname(), "INTERFACES",
			     buf, sizeof(buf)) < 0)
		return ifname;

	iface = buf;
	while (iface && *iface) {
		pos = strchr(iface, '\n');
		if (pos)
			*pos++ = '\0';
		sigma_dut_print( DUT_MSG_DEBUG, "Considering interface "
				"'%s' for IP address", iface);
		if (get_wpa_status(iface, "wpa_state", state, sizeof(state)) ==
		    0 && strcmp(state, "COMPLETED") == 0)
			return iface;
		iface = pos;
	}

	return ifname;
}


static int p2p_peer_known(const char *ifname, const char *peer, int full)
{
	char buf[1000];

	snprintf(buf, sizeof(buf), "P2P_PEER %s", peer);
	if (wpa_command_resp(ifname, buf, buf, sizeof(buf)) < 0)
		return 0;
	if (strncasecmp(buf, peer, strlen(peer)) != 0)
		return 0;
	if (!full)
		return 1;
	return strstr(buf, "[PROBE_REQ_ONLY]") == NULL ? 1 : 0;
}


static int p2p_discover_peer(struct sigma_dut *dut, const char *ifname,
			     const char *peer, int full)
{
	int count;

	if (p2p_peer_known(ifname, peer, full))
		return 0;
	printf("Peer not yet discovered - start discovery\n");
	if (wpa_command(ifname, "P2P_FIND") < 0) {
		printf("Failed to start discovery\n");
		return -1;
	}

	count = 0;
	while (count < dut->default_timeout) {
		count++;
		sleep(1);
		if (p2p_peer_known(ifname, peer, full)) {
			printf("Peer discovered - return to previous state\n");
			switch (dut->p2p_mode) {
			case P2P_IDLE:
				wpa_command(ifname, "P2P_STOP_FIND");
				break;
			case P2P_DISCOVER:
				/* Already running discovery */
				break;
			case P2P_LISTEN:
				wpa_command(ifname, "P2P_LISTEN");
				break;
			case P2P_DISABLE:
				printf("Invalid state - P2P was disabled?!\n");
				break;
			}
			return 0;
		}
	}

	printf("Peer discovery timed out - peer not discovered\n");
	wpa_command(ifname, "P2P_STOP_FIND");

	return -1;
}


static void add_dummy_services(const char *intf)
{
	wpa_command(intf, "P2P_SERVICE_ADD bonjour 0b5f6166706f766572746370c00c000c01 074578616d706c65c027");
	wpa_command(intf, "P2P_SERVICE_ADD bonjour 076578616d706c650b5f6166706f766572746370c00c001001 00");
	wpa_command(intf, "P2P_SERVICE_ADD bonjour 045f697070c00c000c01 094d795072696e746572c027");
	wpa_command(intf, "P2P_SERVICE_ADD bonjour 096d797072696e746572045f697070c00c001001 09747874766572733d311a70646c3d6170706c69636174696f6e2f706f7374736372797074");

	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:6859dede-8574-59ab-9332-123456789012::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:5566d33e-9774-09ab-4822-333456785632::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1122de4e-8574-59ab-9322-333456789044::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:5566d33e-9774-09ab-4822-333456785632::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:6859dede-8574-59ab-9332-123456789012::urn:schemas-upnp-org:device:InternetGatewayDevice:1");

	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1859dede-8574-59ab-9332-123456789012::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1566d33e-9774-09ab-4822-333456785632::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2122de4e-8574-59ab-9322-333456789044::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1566d33e-9774-09ab-4822-333456785632::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1859dede-8574-59ab-9332-123456789012::urn:schemas-upnp-org:device:InternetGatewayDevice:1");

	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2859dede-8574-59ab-9332-123456789012::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2566d33e-9774-09ab-4822-333456785632::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3122de4e-8574-59ab-9322-333456789044::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2566d33e-9774-09ab-4822-333456785632::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2859dede-8574-59ab-9332-123456789012::urn:schemas-upnp-org:device:InternetGatewayDevice:1");

	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3859dede-8574-59ab-9332-123456789012::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3566d33e-9774-09ab-4822-333456785632::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:4122de4e-8574-59ab-9322-333456789044::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3566d33e-9774-09ab-4822-333456785632::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3859dede-8574-59ab-9332-123456789012::urn:schemas-upnp-org:device:InternetGatewayDevice:1");
}


void disconnect_station(struct sigma_dut *dut)
{
#ifdef __APPLE__
	run_system(dut, "apple80211 en1 --disassoc");
#else /* __APPLE__ */
	wpa_command(get_station_ifname(), "DISCONNECT");
	remove_wpa_networks(get_station_ifname());
	dut->infra_ssid[0] = '\0';
#ifdef __linux__
	{
		char path[128];
		char buf[200];
		struct stat s;
		snprintf(path, sizeof(path), "/var/run/dhclient-%s.pid",
			 get_station_ifname());
		if (stat(path, &s) == 0) {
			snprintf(buf, sizeof(buf),
				 "kill `cat %s`", path);
			sigma_dut_print( DUT_MSG_DEBUG,
					"Kill previous DHCP client: %s", buf);
			run_system(dut, buf);
		}
		snprintf(buf, sizeof(buf),
			 "ifconfig %s 0.0.0.0", get_station_ifname());
		sigma_dut_print( DUT_MSG_DEBUG,
				"Clear infrastructure station IP address: %s",
				buf);
		run_system(dut, buf);
   }
#endif /* __linux__ */
#endif /* __APPLE__ */
}


int cmd_sta_get_p2p_dev_address(struct sigma_dut *dut, dutCommand_t *command, dutCmdResponse_t *resp)
{
       //char *intf = command->intf;
       char *intf = "wlan1";
	char buf[100];

	start_sta_mode(dut);
	//if (get_wpa_status(intf, "p2p_device_address", buf, sizeof(buf)) < 0) {
	if (get_wpa_status(intf, "address", buf, sizeof(buf)) < 0)
		return -1;

        strcpy(resp->cmdru.devid/*[0]*/, buf);
	return 0;
}


int cmd_sta_set_p2p(struct sigma_dut *dut, caStaSetP2p_t *command, dutCmdResponse_t *resp)
{
	const char *intf = command->intf;
	char buf[256];
	const char *val;
	const char *noa_dur, *noa_int, *noa_count;
	const char *ext_listen_int, *ext_listen_period;
        dut->Concurrency = 0; 

	if (command->listen_chn_flag) {
		dut->listen_chn =  command->listen_chn;
		snprintf(buf, sizeof(buf), "P2P_SET listen_channel %d",
			 dut->listen_chn);
		if (wpa_command(intf, buf) < 0)
			return -2;
	}

	val = command->p2p_mode;
	if (command->p2p_mode_flag) {
		if (strcasecmp(val, "Listen") == 0) {
			wpa_command(intf, "P2P_SET disabled 0");
			if (wpa_command(intf, "P2P_LISTEN") < 0)
				return -2;
			dut->p2p_mode = P2P_LISTEN;
		} else if (strcasecmp(val, "Discover") == 0) {
			wpa_command(intf, "P2P_SET disabled 0");
			if (wpa_command(intf, "P2P_FIND") < 0)
				return -2;
			dut->p2p_mode = P2P_DISCOVER;
		} else if (strcasecmp(val, "Idle") == 0) {
			wpa_command(intf, "P2P_SET disabled 0");
			if (wpa_command(intf, "P2P_STOP_FIND") < 0)
				return -2;
			dut->p2p_mode = P2P_IDLE;
		} else if (strcasecmp(val, "Disable") == 0) {
			if (wpa_command(intf, "P2P_SET disabled 1") < 0)
				return -2;
			dut->p2p_mode = P2P_DISABLE;
		} else
			return -1;
	}

	//val = get_param(cmd, "PERSISTENT");
	if (command->presistent_flag) {
		dut->persistent = command->presistent;
	}

	//val = 	get_param(cmd, "INTRA_BSS");
	if (command->intra_bss_flag) {
		dut->intra_bss = command->intra_bss;

        snprintf(buf, sizeof(buf), "P2P_SET intra_bss %d",
			 dut->intra_bss);
		if (wpa_command(intf, buf) < 0)
			return -2;
	}

	//noa_dur = get_param(cmd, "NoA_duration");
	//noa_int = get_param(cmd, "NoA_Interval");
	//noa_count = get_param(cmd, "NoA_Count");
	if (command->noa_duration_flag)
		dut->noa_duration = command->noa_duration;

	if (command->noa_interval_flag)
		dut->noa_interval = command->noa_interval;

	if (command->noa_count_flag)
		dut->noa_count = command->noa_count;

	if (dut->noa_duration || dut->noa_interval || dut->noa_count) {
		int start;
		const char *ifname;
		if (dut->noa_count == 0 && dut->noa_duration == 0)
			start = 0;
		else if (dut->noa_duration > 102) /* likely non-periodic NoA */
			start = 50;
		else
			start = 102 - dut->noa_duration;
		snprintf(buf, sizeof(buf), "P2P_SET noa %d,%d,%d",
			 dut->noa_count, start,
			 dut->noa_duration);
		ifname = get_group_ifname(dut, intf);
		sigma_dut_print( DUT_MSG_INFO,
				"Set GO NoA for interface %s", ifname);
		if (wpa_command(ifname, buf) < 0)
			return -2;
	}

	//val = get_param(cmd, "Concurrency");
	if (command->concurrency_flag) {
            dut->Concurrency = command->concurrency; 
		/* TODO */
	}

	//val = get_param(cmd, "P2PInvitation");
	if (command->p2p_invitation_flag) {
		/* TODO */
	}

	//val = get_param(cmd, "BCN_INT");
	if (command->bcn_int_flag) {
		/* TODO */
	}

	//ext_listen_int = get_param(cmd, "Ext_Listen_Time_Interval");
	//ext_listen_period = get_param(cmd, "Ext_Listen_Time_Period");

	if (command->ext_listen_time_int_flag || command->ext_listen_time_period_flag) {
		if (!command->ext_listen_time_int || !command->ext_listen_time_period) {
			sigma_dut_print( DUT_MSG_INFO, "Only one "
					"ext_listen_time parameter included; "
					"both are needed");
			return -1;
		}
		snprintf(buf, sizeof(buf), "P2P_EXT_LISTEN %d %d",
			 command->ext_listen_time_period,
			 command->ext_listen_time_int);
		if (wpa_command(intf, buf) < 0)
			return -2;
	}

	//val = get_param(cmd, "Discoverability");
	if (command->discoverability_flag) {
		snprintf(buf, sizeof(buf), "P2P_SET discoverability %d",
			 command->discoverability);
		if (wpa_command(intf, buf) < 0)
			return -2;
	}

	//val = get_param(cmd, "Service_Discovery");
	if (command->service_discovry_flag) {
		int sd = command->service_discovery;
		if (sd) {
			wpa_command(intf, "P2P_SERVICE_FLUSH");

			if (sd == 2)
				wpa_command(intf, "P2P_SET force_long_sd 1");

			/*
			 * Set up some dummy service to create a large SD
			 * response that requires fragmentation.
			 */
			add_dummy_services(intf);
		} else {
			wpa_command(intf, "P2P_SERVICE_FLUSH");
		}
	}

	//val = get_param(cmd, "CrossConnection");
	if (command->crossconnection_flag) {
		if (command->crossconnection) {
			if (wpa_command(intf, "P2P_SET cross_connect 1") < 0)
				return -2;
		} else {
			if (wpa_command(intf, "P2P_SET cross_connect 0") < 0)
				return -2;
		}
	}

	//val = get_param(cmd, "P2PManaged");
	if (command->p2pmanaged_flag) {
		if (command->p2pmanaged) {
			//send_resp(dut, conn, SIGMA_INVALID, "ErrorCode,"
			//	  "P2P Managed functionality not supported");
			return -1;
		}
	}

	//val = get_param(cmd, "GO_APSD");
	if (command->go_apsd_flag) {
		if (command->go_apsd) {
			if (wpa_command(intf, "P2P_SET go_apsd 1") < 0)
				return -2;
		} else {
			if (wpa_command(intf, "P2P_SET go_apsd 0") < 0)
				return -2;
		}
	}

	return 1;
}


int cmd_sta_start_autonomous_go(struct sigma_dut *dut, caStaStartAutoGo_t *command, dutCmdResponse_t *cmdresp)
{
	const char *intf = command->intf;
       unsigned short chan = command->oper_chn;
	const char *ssid_param = command->ssid;
	int freq, res;
	char buf[256], grpid[100], resp[200];
	struct wpa_ctrl *ctrl;
	char *ifname, *gtype, *pos, *ssid, bssid[20];
	char *go_dev_addr;

	//chan = atoi(oper_chn);
	if (chan >= 1 && chan <= 13)
		freq = 2407 + chan * 5;
	else if (chan == 14)
		freq = 2484;
	else
		freq = 5000 + chan * 5;

	if (command->ssid_flag)
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix %s",
			 ssid_param);
	else
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix ");
	if (wpa_command(intf, buf) < 0)
		return -2;

	/* Stop Listen/Discovery state to avoid issues with GO operations */
	if (wpa_command(intf, "P2P_STOP_FIND") < 0)
		return -2;

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print( DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	snprintf(buf, sizeof(buf), "P2P_GROUP_ADD %sfreq=%d",
		 dut->persistent ? "persistent " : "", freq);
	if (wpa_command(intf, buf) < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return -2;
	}

	res = get_wpa_cli_event(dut, ctrl, "P2P-GROUP-STARTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,GO starting "
			  "did not complete");
		return 0;
	}

	sigma_dut_print( DUT_MSG_DEBUG, "Group started event '%s'", buf);
	ifname = strchr(buf, ' ');
	if (ifname == NULL)
		return -2;
	ifname++;
	pos = strchr(ifname, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group interface %s", ifname);

	gtype = pos;
	pos = strchr(gtype, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group type %s", gtype);

	ssid = strstr(pos, "ssid=\"");
	if (ssid == NULL)
		return -2;
	ssid += 6;
	pos = strchr(ssid, '"');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group SSID %s", ssid);

	go_dev_addr = strstr(pos, "go_dev_addr=");
	if (go_dev_addr == NULL) {
		sigma_dut_print( DUT_MSG_ERROR, "No GO P2P Device Address "
				"found");
		return -2;
	}
	go_dev_addr += 12;
	if (strlen(go_dev_addr) < 17) {
		sigma_dut_print( DUT_MSG_ERROR, "Too short GO P2P Device "
				"Address '%s'", go_dev_addr);
		return -2;
	}
	go_dev_addr[17] = '\0';
	*pos = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "GO P2P Device Address %s",
			go_dev_addr);

	if (get_wpa_status(ifname, "bssid", bssid, sizeof(bssid)) < 0)
		return -2;
	sigma_dut_print( DUT_MSG_DEBUG, "Group BSSID %s", bssid);

	snprintf(grpid, sizeof(grpid), "%s %s", go_dev_addr, ssid);
	p2p_group_add(dut, ifname, strcmp(gtype, "GO") == 0, grpid, ssid);

	snprintf(resp, sizeof(resp), "GroupID,%s", grpid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

#if 1        
        // Fetch the group ID and store into 	infoResp->cmdru.grpid 
	strcpy(&cmdresp->cmdru.grpid[0], grpid);
#endif

	return 0;
}


int cmd_sta_p2p_connect(struct sigma_dut *dut, caStaP2pConnect_t *command, dutCmdResponse_t *cmdresp)
{
	const char *intf = command->intf;
	const char *devid = command->devId;
	/* const char *grpid_param = get_param(cmd, "GroupID"); */
	int res;
	char buf[256];
	struct wpa_ctrl *ctrl;
	char *ifname, *gtype, *pos, *ssid, bssid[20];
	char grpid[100];

	/* TODO: handle the new grpid argument */

	if (strcmp(devid, "") == 0)
		return -1;

	if (dut->wps_method == WFA_CS_WPS_NOT_READY) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS parameters "
			  "not yet set");
		return -1;
	}

	sigma_dut_print( DUT_MSG_DEBUG, "Trying to discover GO %s", devid);
	if (p2p_discover_peer(dut, intf, devid, 1) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return -1;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print( DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	snprintf(buf, sizeof(buf), "P2P_CONNECT %s %s join",
		 devid,
		 dut->wps_method == WFA_CS_WPS_PBC ?
		 "pbc" : dut->wps_pin);
	if (wpa_command(intf, buf) < 0) {        
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed to join "
			  "the group");
		if (ctrl) {
			wpa_ctrl_detach(ctrl);
			wpa_ctrl_close(ctrl);
		}
		return -1;
	}

	res = get_wpa_cli_event(dut, ctrl, "P2P-GROUP-STARTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Group joining "
			  "did not complete");
		return -1;
	}

	sigma_dut_print( DUT_MSG_DEBUG, "Group started event '%s'", buf);
	ifname = strchr(buf, ' ');
	if (ifname == NULL)
		return -2;
	ifname++;
	pos = strchr(ifname, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group interface %s", ifname);

	gtype = pos;
	pos = strchr(gtype, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group type %s", gtype);

	ssid = strstr(pos, "ssid=\"");
	if (ssid == NULL)
		return -2;
	ssid += 6;
	pos = strchr(ssid, '"');
	if (pos == NULL)
		return -2;
	*pos = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group SSID %s", ssid);

	if (get_wpa_status(ifname, "bssid", bssid, sizeof(bssid)) < 0)
		return -2;
	sigma_dut_print( DUT_MSG_DEBUG, "Group BSSID %s", bssid);

	snprintf(grpid, sizeof(grpid), "%s %s", bssid, ssid);
	p2p_group_add(dut, ifname, strcmp(gtype, "GO") == 0, grpid, ssid);

	return 1;

}


int cmd_sta_p2p_start_group_formation(struct sigma_dut *dut,
					     caStaP2pStartGrpForm_t *command,
					     dutCmdResponse_t *cmdresp)
{
	const char *intf = command->intf;;
	const char *devid = command->devId;
	unsigned short intent_val = command->intent_val;
	unsigned short init_go_neg = command->init_go_neg;
	unsigned short oper_chn = command->oper_chn;
	const char *ssid_param = command->ssid;
	int freq = 0, chan, res, init;
	char buf[256], grpid[50], resp[256];
	struct wpa_ctrl *ctrl;
	char *ifname, *gtype, *pos, *ssid, bssid[20];
	char *go_dev_addr;

sigma_dut_print( DUT_MSG_DEBUG, "%s -1, intent_val : %d\n", __func__, intent_val);

#if 1
       if(intent_val == 0) // o means oob ??
            intent_val = 1;
#endif

	if (strcmp(devid, "")  == 0 || !intent_val)
		return -1;

sigma_dut_print( DUT_MSG_DEBUG, "%s -2\n", __func__);
    

	if (init_go_neg)
		init = init_go_neg;
	else
		init = 0;

sigma_dut_print( DUT_MSG_DEBUG, "%s -3 : init : %d\n", __func__, init);

	if (command->oper_chn_flag && oper_chn) {
		chan = oper_chn;
		if (chan >= 1 && chan <= 13)
			freq = 2407 + chan * 5;
		else if (chan == 14)
			freq = 2484;
		else
			freq = 5000 + chan * 5;
	}

sigma_dut_print( DUT_MSG_DEBUG, "%s -4\n", __func__);    

	if (dut->wps_method == WFA_CS_WPS_NOT_READY) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS parameters "
			  "not yet set");
		return -1;
	}

	sigma_dut_print( DUT_MSG_DEBUG, "Trying to discover peer %s for "
			"group formation", devid);
	if (p2p_discover_peer(dut, intf, devid, init) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return -1;
	}

    sigma_dut_print( DUT_MSG_DEBUG, "%s -6\n", __func__);
    

	if (ssid_param)
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix %s",
			 ssid_param);
	else
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix ");
	if (wpa_command(intf, buf) < 0)
		return -2;

	if (init) {
		ctrl = open_wpa_mon(intf);
		if (ctrl == NULL) {
			sigma_dut_print( DUT_MSG_ERROR, "Failed to open "
					"wpa_supplicant monitor connection");
			return -2;
		}
	} else
		ctrl = NULL;

	snprintf(buf, sizeof(buf), "P2P_CONNECT %s %s%s%s%s go_intent=%d",
		 devid,
		 dut->wps_method == WFA_CS_WPS_PBC ?
		 "pbc" : dut->wps_pin,
		 dut->wps_method == WFA_CS_WPS_PBC ? "" :
		 (dut->wps_method == WFA_CS_WPS_PIN_DISPLAY ? " display" :
		  (dut->wps_method == WFA_CS_WPS_PIN_LABEL ? " label" :
		   " keypad" )),
		 dut->persistent ? " persistent" : "",
		 init ? "" : " auth",
		 intent_val);
	if (freq > 0) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
			 " freq=%d", freq);
	}
	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed to start "
			  "group formation");
		if (ctrl) {
			wpa_ctrl_detach(ctrl);
			wpa_ctrl_close(ctrl);
		}
		return -1;
	}

	if (!init)
		return 1;

	res = get_wpa_cli_event2(dut, ctrl, "P2P-GROUP-STARTED",
				 "P2P-GO-NEG-FAILURE",
				 buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

    if (res == -2) {
		send_resp(dut, conn, SIGMA_INVALID, "ErrorCode,Group formation "
			  "did not complete");
		return -1;
	}

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Group formation "
			  "did not complete");
		return -1;
	}

	sigma_dut_print( DUT_MSG_DEBUG, "Group started event '%s'", buf);

	if (strstr(buf, "P2P-GO-NEG-FAILURE")) {
		int status = -1;
		pos = strstr(buf, " status=");
		if (pos)
			status = atoi(pos + 8);
		sigma_dut_print( DUT_MSG_INFO, "GO Negotiation failed "
				"(status=%d)", status);
		if (status == 9) {
			sigma_dut_print( DUT_MSG_INFO, "Both devices "
					"tried to use GO Intent 15");
			send_resp(dut, conn, SIGMA_COMPLETE, "result,FAIL");
			return -1;
		}
		snprintf(buf, sizeof(buf), "ErrorCode,GO Negotiation failed "
			 "(status=%d)", status);
		send_resp(dut, conn, SIGMA_ERROR, buf);
		return -1;
	}

	ifname = strchr(buf, ' ');
	if (ifname == NULL)
		return -2;
	ifname++;
	pos = strchr(ifname, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group interface %s", ifname);

	gtype = pos;
	pos = strchr(gtype, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group type %s", gtype);

	ssid = strstr(pos, "ssid=\"");
	if (ssid == NULL)
		return -2;
	ssid += 6;
	pos = strchr(ssid, '"');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print( DUT_MSG_DEBUG, "Group SSID %s", ssid);

	go_dev_addr = strstr(pos, "go_dev_addr=");
	if (go_dev_addr == NULL) {
		sigma_dut_print( DUT_MSG_ERROR, "No GO P2P Device Address "
				"found\n");
		return -2;
	}
	go_dev_addr += 12;
	if (strlen(go_dev_addr) < 17) {
		sigma_dut_print( DUT_MSG_ERROR, "Too short GO P2P Device "
				"Address '%s'", go_dev_addr);
		return -2;
	}
	go_dev_addr[17] = '\0';
	*pos = '\0';
	sigma_dut_print( DUT_MSG_ERROR, "GO P2P Device Address %s",
			go_dev_addr);

	if (get_wpa_status(ifname, "bssid", bssid, sizeof(bssid)) < 0)
		return -2;
	sigma_dut_print( DUT_MSG_DEBUG, "Group BSSID %s", bssid);

	snprintf(grpid, sizeof(grpid), "%s %s", go_dev_addr, ssid);
	p2p_group_add(dut, ifname, strcmp(gtype, "GO") == 0, grpid, ssid);
	snprintf(resp, sizeof(resp), "Result,%s,GroupID,%s",
		 strcmp(gtype, "GO") == 0 ? "GO" : "CLIENT", grpid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

#if 1
	// Fetch the device mode and put in 	infoResp->cmdru.p2presult 
	//strcpy(infoResp->cmdru.p2presult, "GO");

	// Fetch the device grp id and put in 	infoResp->cmdru.grpid 
	//strcpy(infoResp->cmdru.grpid, "AA:BB:CC:DD:EE:FF_DIRECT-SSID");
	
	strcpy(cmdresp->cmdru.grpFormInfo.result, gtype);
	strcpy(cmdresp->cmdru.grpFormInfo.grpId, grpid);
#endif

            
	return 0;
}


int cmd_sta_p2p_dissolve(struct sigma_dut *dut, caStaP2pDissolve_t *command,
				dutCmdResponse_t *cmdresp)
{
	const char *intf = command->intf;
	const char *grpid = command->grpId;
	struct wfa_cs_p2p_group *grp;
	char buf[128];

	if (strcmp(grpid, "") == 0)
		return -1;

	grp = p2p_group_get(dut, grpid);
	if (grp == NULL) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Requested group "
			  "not found");
		return -1;
	}

	snprintf(buf, sizeof(buf), "P2P_GROUP_REMOVE %s", grp->ifname);
	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed to remove "
			  "the specified group");
		return -1;
	}
	sigma_dut_print( DUT_MSG_DEBUG, "Removed group %s", grpid);
	p2p_group_remove(dut, grpid);
	return 1;
}


int cmd_sta_send_p2p_invitation_req(struct sigma_dut *dut,
					   caStaSendP2pInvReq_t *command,
					   dutCmdResponse_t *cmdresp)
{
	const char *intf = command->intf;;
	const char *devid = command->devId;
	const char *grpid = command->grpId;
	int                 reinvoke = command->reinvoke;
	char c[256];
	char buf[4096];
	struct wpa_ctrl *ctrl;
	int res;

	if (strcmp(devid, "") == 0 || strcmp(grpid, "") == 0)
		return -1;

	if (reinvoke) {
		int id = -1;
		char *ssid, *pos;

		ssid = strchr(grpid, ' ');
		if (ssid == NULL) {
			sigma_dut_print( DUT_MSG_INFO, "Invalid grpid");
			return -1;
		}
		ssid++;
		sigma_dut_print( DUT_MSG_DEBUG, "Search for persistent "
				"group credentials based on SSID: '%s'", ssid);
		if (wpa_command_resp(intf, "LIST_NETWORKS",
				     buf, sizeof(buf)) < 0)
			return -2;
		pos = strstr(buf, ssid);
		if (pos == NULL || pos == buf || pos[-1] != '\t' ||
		    pos[strlen(ssid)] != '\t') {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
				  "Persistent group credentials not found");
			return -1;
		}
		while (pos > buf && pos[-1] != '\n')
			pos--;
		id = atoi(pos);
		snprintf(c, sizeof(c), "P2P_INVITE persistent=%d peer=%s",
			 id, devid);
	} else {
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
				  "No active P2P group found for invitation");
			return -1;
		}
		snprintf(c, sizeof(c), "P2P_INVITE group=%s peer=%s",
			 grp->ifname, devid);
	}

	sigma_dut_print( DUT_MSG_DEBUG, "Trying to discover peer %s for "
			"invitation", devid);
	if (p2p_discover_peer(dut, intf, devid, 0) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return -1;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print( DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	if (wpa_command(intf, c) < 0) {
		sigma_dut_print( DUT_MSG_INFO, "Failed to send invitation "
				"request");
		return -2;
	}

	res = get_wpa_cli_event(dut, ctrl, "P2P-INVITATION-RESULT",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0)
		return -2;

	sigma_dut_print( DUT_MSG_DEBUG, "Invitation event: '%s'", buf);
	return 1;
}


int cmd_sta_accept_p2p_invitation_req(struct sigma_dut *dut,
					     caStaAcceptP2pInvReq_t *command,
					     dutCmdResponse_t *cmdresp)
{
	const char *intf = command->intf;;
	const char *devid = command->devId;
	const char *grpid = command->grpId;
	int                 reinvoke = command->reinvoke;

    
	char buf[100];

	if (devid == NULL || grpid == NULL)
		return -1;

	if (reinvoke) {
		/*
		 * Assume persistent reconnect is enabled and there is no need
		 * to do anything here.
		 */
		return 1;
	}

	/*
	 * In a client-joining-a-running-group case, we need to separately
	 * authorize the invitation.
	 */

	sigma_dut_print( DUT_MSG_DEBUG, "Trying to discover GO %s", devid);
	if (p2p_discover_peer(dut, intf, devid, 1) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return -1;
	}

	snprintf(buf, sizeof(buf), "P2P_CONNECT %s %s join auth",
		 devid,
		 dut->wps_method == WFA_CS_WPS_PBC ?
		 "pbc" : dut->wps_pin);
	if (wpa_command(intf, buf) < 0)
		return -2;

	return 1;
}


int cmd_sta_send_p2p_provision_dis_req(struct sigma_dut *dut,
					      caStaSendP2pProvDisReq_t *command,
					      dutCmdResponse_t *cmdresp)
{
	const char *intf = command->intf;
	const char *conf_method = command->confMethod;
	const char *devid = command->devId;
	char buf[256];
	char *method;

	if (conf_method == NULL || devid == NULL)
		return -1;

	if (strcasecmp(conf_method, "Display") == 0)
		method = "display";
	else if (strcasecmp(conf_method, "Keyboard") == 0 ||
		 strcasecmp(conf_method, "keypad") == 0)
		method = "keypad";
	else if (strcasecmp(conf_method, "Label") == 0)
		method = "label";
	else if (strcasecmp(conf_method, "pbc") == 0 ||
		 strcasecmp(conf_method, "pushbutton") == 0)
		method = "pbc";
	else
		return -1;

	sigma_dut_print( DUT_MSG_DEBUG, "Trying to discover peer %s for "
			"provision discovery", devid);
	if (p2p_discover_peer(dut, intf, devid, 0) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return -1;
	}

	snprintf(buf, sizeof(buf), "P2P_PROV_DISC %s %s", devid, method);
	if (wpa_command(intf, buf) < 0) {
		sigma_dut_print( DUT_MSG_INFO, "Failed to send provision "
				"discovery request");
		return -2;
	}

	return 1;
}


int cmd_sta_set_wps_pbc(struct sigma_dut *dut, caStaSetWpsPbc_t *command,
			       dutCmdResponse_t *cmdresp)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *grpid = command->grpId;

	if (grpid) {
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp && grp->go) {
			sigma_dut_print( DUT_MSG_DEBUG, "Authorize a "
					"client to join with WPS");
			wpa_command(grp->ifname, "WPS_PBC");
			return 1;
		}
	}

	dut->wps_method = WFA_CS_WPS_PBC;
	return 1;
}


int cmd_sta_wps_read_pin(struct sigma_dut *dut, caStaWpsReadPin_t *command,
				dutCmdResponse_t *cmdresp)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *grpid = command->grpId;
	char *pin = "12345670"; /* TODO: use random PIN */
	char resp[100];

	if (command->grpid_flag && grpid) {
		char buf[100];
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp && grp->go) {
			sigma_dut_print( DUT_MSG_DEBUG, "Authorize a "
					"client to join with WPS");
			snprintf(buf, sizeof(buf), "WPS_PIN any %s", pin);
			wpa_command(grp->ifname, buf);
			//return 1;
		}
	}

	strncpy(dut->wps_pin, pin, sizeof(dut->wps_pin));
	dut->wps_method = WFA_CS_WPS_PIN_DISPLAY;
	snprintf(resp, sizeof(resp), "PIN,%s", pin);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

#if 1
 	// Fetch the device PIN and put in 	infoResp->cmdru.wpsPin 
	//strcpy(infoResp->cmdru.wpsPin, "12345678");
	strcpy(&(cmdresp->cmdru.wpsPin[0]), pin);   
#endif

	return 0;
}


int cmd_sta_wps_read_label(struct sigma_dut *dut,
				  caStaWpsReadLabel_t *command,
				  dutCmdResponse_t *cmdresp)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *grpid = command->grpId;;
	char *pin = "12345670";
	char resp[100];

	if (command->grpid_flag && grpid) {
		char buf[100];
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp && grp->go) {
			sigma_dut_print( DUT_MSG_DEBUG, "Authorize a "
					"client to join with WPS");
			snprintf(buf, sizeof(buf), "WPS_PIN any %s", pin);
			wpa_command(grp->ifname, buf);
			return 1;
		}
	}

	strncpy(dut->wps_pin, pin, sizeof(dut->wps_pin));
	dut->wps_method = WFA_CS_WPS_PIN_LABEL;
	snprintf(resp, sizeof(resp), "LABEL,%s", pin);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

#if 1
	// Fetch the device Label and put in	infoResp->cmdru.wpsPin 
	//strcpy(infoResp->cmdru.wpsPin, "12345678");
	strcpy(&cmdresp->cmdru.wpsPin[0], pin);
#endif    

	return 0;
}


int cmd_sta_wps_enter_pin(struct sigma_dut *dut,
				 caStaWpsEnterPin_t *command,
				 dutCmdResponse_t *cmdresp)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *grpid = command->grpId;
	const char *pin = command->wpsPin;
        char *ifname;  

	if (pin == NULL)
		return -1;
        ifname = get_main_ifname();        
                                                                      
        if(dut->Concurrency) {                                                                   
            wpa_command(ifname, "P2P_STOP_FIND");                                                
            wpa_command(ifname, "P2P_FIND");                                                     
        } 

	if (grpid) {
		char buf[100];
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp && grp->go) {
			sigma_dut_print( DUT_MSG_DEBUG, "Authorize a "
					"client to join with WPS");
			snprintf(buf, sizeof(buf), "WPS_PIN any %s", pin);
			wpa_command(grp->ifname, buf);
			return 1;
		}
	}

	strncpy(dut->wps_pin, pin, sizeof(dut->wps_pin));
	dut->wps_pin[sizeof(dut->wps_pin) - 1] = '\0';
	dut->wps_method = WFA_CS_WPS_PIN_KEYPAD;

	return 1;
}


int cmd_sta_get_psk(struct sigma_dut *dut, caStaGetPsk_t *command,
			   dutCmdResponse_t *cmdresp)
{
	/* const char *intf = get_param(cmd, "interface"); */
	const char *grpid = command->grpId;
	struct wfa_cs_p2p_group *grp;
	char passphrase[64], resp[200];

	if (grpid == NULL)
		return -1;

	grp = p2p_group_get(dut, grpid);
	if (grp == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Requested group not found");
		return -1;
	}
	if (!grp->go) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Local role is not GO in the specified "
			  "group");
		return -1;
	}

	if (wpa_command_resp(grp->ifname, "P2P_GET_PASSPHRASE",
			     passphrase, sizeof(passphrase)) < 0)
		return -2;

	snprintf(resp, sizeof(resp), "passPhrase,%s,ssid,%s",
		 passphrase, grp->ssid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

#if 1
	// Fetch the device PP and SSID  and put in 	infoResp->cmdru.pskInfo 
	//strcpy(infoResp->cmdru.wpsPin, "12345678");
	strcpy(&cmdresp->cmdru.pskInfo.passPhrase[0], passphrase);
	strcpy(&cmdresp->cmdru.pskInfo.ssid[0], grp->ssid);	
#endif    

	return 0;
}


int cmd_sta_p2p_reset(struct sigma_dut *dut, dutCommand_t *command,
		      dutCmdResponse_t *resp)
{
       char *intf = command->intf;
	struct wfa_cs_p2p_group *grp, *prev;
	char buf[256];

	dut->wps_method = WFA_CS_WPS_NOT_READY;

	grp = dut->groups;
	while (grp) {
		prev = grp;
		grp = grp->next;

		snprintf(buf, sizeof(buf), "P2P_GROUP_REMOVE %s",
			 prev->ifname);
		wpa_command(intf, buf);
		p2p_group_remove(dut, prev->grpid);
	}

	wpa_command(intf, "P2P_GROUP_REMOVE *");
	wpa_command(intf, "P2P_STOP_FIND");
	wpa_command(intf, "P2P_FLUSH");
	wpa_command(intf, "P2P_SERVICE_FLUSH");
	wpa_command(intf, "P2P_SET ssid_postfix ");
	wpa_command(intf, "P2P_EXT_LISTEN");
	wpa_command(intf, "P2P_SET client_apsd disable");
	wpa_command(intf, "P2P_SET go_apsd disable");
	wpa_command(get_station_ifname(), "P2P_SET ps 98");
	wpa_command(get_station_ifname(), "P2P_SET ps 96");
	wpa_command(get_station_ifname(), "P2P_SET ps 0");
	wpa_command(intf, "SET ampdu 1");
	run_system(dut, "iptables -F INPUT");
	if (dut->arp_ipaddr[0]) {
		snprintf(buf, sizeof(buf), "ip nei del %s dev %s",
			 dut->arp_ipaddr, dut->arp_ifname);
		run_system(dut, buf);
		dut->arp_ipaddr[0] = '\0';
	}
	snprintf(buf, sizeof(buf), "ip nei flush dev %s",
		 get_station_ifname());
	run_system(dut, buf);
	dut->p2p_mode = P2P_IDLE;
	dut->client_uapsd = 0;

	remove_wpa_networks(intf);

	disconnect_station(dut);

	return 1;
}



int cmd_sta_get_p2p_ip_config(struct sigma_dut *dut,
				     caStaGetP2pIpConfig_t *command,
				     dutCmdResponse_t *cmdRes)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *grpid = command->grpId;
	struct wfa_cs_p2p_group *grp = NULL;
	int count;
	char macaddr[20];
	char resp[200], info[150];
    caStaGetIpConfigResp_t *ifinfo = &(cmdRes->cmdru.getIfconfig);

	if (grpid == NULL)
		return -1;

	/*
	 * If we did not initiate the operation that created the group, we may
	 * not have the group information available in the DUT code yet and it
	 * may take some time to get this from wpa_supplicant in case we are
	 * the P2P client. As such, we better try this multiple times to allow
	 * some time to complete the operation.
	 */

	sigma_dut_print( DUT_MSG_DEBUG, "Waiting to find the requested group : %s", grpid);
	count = dut->default_timeout;
	while (count > 0) {
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			sigma_dut_print( DUT_MSG_DEBUG, "Requested group "
					"not yet found (count=%d)", count);
			sleep(1);
		} else
			break;
		count--;
	}
	if (grp == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Requested group not found");
		return 0;
	}

	sigma_dut_print( DUT_MSG_DEBUG, "Waiting for IP address on group "
			"interface %s", grp->ifname);
	if (wait_ip_addr(dut, grp->ifname) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No IP address received");
		return 0;
	}

	if (get_ip_config(dut, grp->ifname, info, sizeof(info)) < 0) {
		sigma_dut_print( DUT_MSG_INFO, "Failed to get IP address "
				"for group interface %s",
				grp->ifname);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to get IP address");
		return 0;
	}

	if (get_wpa_status(grp->ifname, "address",
			   macaddr, sizeof(macaddr)) < 0) {
		sigma_dut_print( DUT_MSG_ERROR, "Failed to get interface "
				"address for group interface %s",
				grp->ifname);
		return -2;
	}

	sigma_dut_print( DUT_MSG_DEBUG, "IP address for group interface "
			"%s found", grp->ifname);

	snprintf(resp, sizeof(resp), "%s,P2PInterfaceAddress,%s",
		 info, macaddr);

	send_resp(dut, conn, SIGMA_COMPLETE, resp);

#if 1
	// Fetch the device IP config  and put in 	infoResp->cmdru 
	//strcpy(infoResp->cmdru.wpsPin, "12345678");

        // 'info' format : dhcp,%d,ip,%s,mask,%s,primary-dns,%s
        // 'info' parsing is needed for dhcp, ipaddr ...
	ifinfo->isDhcp =0;
	strcpy(&(ifinfo->ipaddr[0]), "192.165.100.111");
	strcpy(&(ifinfo->mask[0]), "255.255.255.0");
	strcpy(&(ifinfo->dns[0][0]), "192.165.100.1");
	strcpy(&(ifinfo->mac[0]), macaddr);
#endif	    
    
	return 0;
}


int cmd_sta_send_p2p_presence_req(struct sigma_dut *dut,
					 caStaSendP2pPresenceReq_t *command,
					 dutCmdResponse_t *cmdRes)
{
	const char *intf = command->intf;
	long long int dur = command->duration;
	long long int interv = command->interval;
	/* const char *grpid = get_param(cmd, "GroupID"); */
	const char *ifname;
	char buf[100];

	if (dur == 0 || interv == 0)
		return -1;

	/* TODO: need to add groupid into parameters in CAPI spec; for now,
	 * pick the first active group */
	ifname = get_group_ifname(dut, intf);
	snprintf(buf, sizeof(buf), "P2P_PRESENCE_REQ %lld %lld", dur, interv);
	if (wpa_command(ifname, buf) < 0)
		return -2;

	return 1;
}


int cmd_sta_set_sleep(struct sigma_dut *dut, caStaSetSleep_t *command,
			     dutCmdResponse_t *cmdRes)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	struct wfa_cs_p2p_group *grp;
	char *ifname;
	const char *grpid = command->grpId;

	if (strcmp(grpid, "") == 0)
		ifname = get_station_ifname();
	else {
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Requested group not found");
			return -1;
		}
		ifname = grp->ifname;
	}

	if (dut->client_uapsd) {
		if (wpa_command(ifname, "P2P_SET ps 99") < 0)
			return -2;
	} else {
		if (wpa_command(ifname, "P2P_SET ps 1") < 0)
			return -2;
	}

	return 1;
}


int cmd_sta_set_opportunistic_ps(struct sigma_dut *dut,
					caStaSetOpprPs_t *command,
					dutCmdResponse_t *cmdRes)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	struct wfa_cs_p2p_group *grp;
	char buf[100];
	const char *grpid = command->grpId;
	int ctwindow = command->ctwindow;

	if (strcmp(grpid, "") == 0 || ctwindow == 0)
		return -1;

	grp = p2p_group_get(dut, grpid);
	if (grp == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Requested group not found");
		return -1;
	}

	if (wpa_command(grp->ifname, "P2P_SET oppps 1") < 0)
		return -2;
	snprintf(buf, sizeof(buf), "P2P_SET ctwindow %d", ctwindow);
	if (wpa_command(grp->ifname, buf) < 0)
		return -2;

	return 1;
}


int cmd_sta_send_service_discovery_req(struct sigma_dut *dut,
					      caStaSendServiceDiscoveryReq_t *command,
					      dutCmdResponse_t *cmdRes)
{
	const char *intf = command->intf;
	const char *devid = command->devId;
	char buf[128];

	if (strcmp(devid, "")  == 0)
		return -1;

	snprintf(buf, sizeof(buf), "P2P_SERV_DISC_REQ %s 02000001",
		 devid);
	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		return -1;
	}

	return 1;
}


int cmd_sta_add_arp_table_entry(struct sigma_dut *dut,
				       caStaAddARPTableEntry_t *command,
				       dutCmdResponse_t *cmdRes)
{
	char buf[256];
	char *ifname;
	const char *grpid, *ipaddr, *macaddr;

	grpid = NULL; //get_param(cmd, "GroupID");
	ipaddr = command->ipaddress;
	macaddr = command->macaddress;
	if (strcmp(ipaddr, "") == 0 || strcmp(macaddr, "") == 0)
		return -1;

	if (grpid == NULL)
		ifname = get_station_ifname();
	else {
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Requested group not found");
			return -1;
		}
		ifname = grp->ifname;
	}

	snprintf(dut->arp_ipaddr, sizeof(dut->arp_ipaddr), "%s",
		 ipaddr);
	snprintf(dut->arp_ifname, sizeof(dut->arp_ifname), "%s",
		 ifname);

	snprintf(buf, sizeof(buf), "ip nei add %s lladdr %s dev %s",
		 ipaddr, macaddr, ifname);
	run_system(dut, buf);

	return 1;
}


int cmd_sta_block_icmp_response(struct sigma_dut *dut,
				       caStaBlockICMPResponse_t *command,
				       dutCmdResponse_t *cmdRes)
{
	char buf[256];
	struct wfa_cs_p2p_group *grp;
	char *ifname;
	const char *grpid, *ipaddr;

	grpid = command->grpId;
	ipaddr = command->ipaddress;
	if (strcmp(ipaddr, "") == 0)
		return -1;

	if (strcmp(grpid, "") == 0)
		ifname = get_station_ifname();
	else {
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Requested group not found");
			return -1;
		}
		ifname = grp->ifname;
	}

	snprintf(buf, sizeof(buf),
		 "iptables -I INPUT -s %s -p icmp -i %s -j DROP",
		 ipaddr, ifname);
	run_system(dut, buf);

	return 1;
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


void p2p_register_cmds(void)
{
#ifdef _SIGMA_CA_REMOVED
	sigma_dut_reg_cmd("sta_get_p2p_dev_address", req_intf,
			  cmd_sta_get_p2p_dev_address);
	sigma_dut_reg_cmd("sta_set_p2p", req_intf, cmd_sta_set_p2p);
	sigma_dut_reg_cmd("sta_start_autonomous_go", req_intf,
			  cmd_sta_start_autonomous_go);
	sigma_dut_reg_cmd("sta_p2p_connect", req_intf, cmd_sta_p2p_connect);
	sigma_dut_reg_cmd("sta_p2p_start_group_formation", req_intf,
			  cmd_sta_p2p_start_group_formation);
	sigma_dut_reg_cmd("sta_p2p_dissolve", req_intf, cmd_sta_p2p_dissolve);
	sigma_dut_reg_cmd("sta_send_p2p_invitation_req", req_intf,
			  cmd_sta_send_p2p_invitation_req);
	sigma_dut_reg_cmd("sta_accept_p2p_invitation_req", req_intf,
			  cmd_sta_accept_p2p_invitation_req);
	sigma_dut_reg_cmd("sta_send_p2p_provision_dis_req", req_intf,
			  cmd_sta_send_p2p_provision_dis_req);
	sigma_dut_reg_cmd("sta_set_wps_pbc", req_intf, cmd_sta_set_wps_pbc);
	sigma_dut_reg_cmd("sta_wps_read_pin", req_intf, cmd_sta_wps_read_pin);
	sigma_dut_reg_cmd("sta_wps_read_label", req_intf,
			  cmd_sta_wps_read_label);
	sigma_dut_reg_cmd("sta_wps_enter_pin", req_intf,
			  cmd_sta_wps_enter_pin);
	sigma_dut_reg_cmd("sta_get_psk", req_intf, cmd_sta_get_psk);
	sigma_dut_reg_cmd("sta_p2p_reset", req_intf, cmd_sta_p2p_reset);
	sigma_dut_reg_cmd("sta_get_p2p_ip_config", req_intf,
			  cmd_sta_get_p2p_ip_config);
	sigma_dut_reg_cmd("sta_send_p2p_presence_req", req_intf,
			  cmd_sta_send_p2p_presence_req);
	sigma_dut_reg_cmd("sta_set_sleep", req_intf, cmd_sta_set_sleep);
	sigma_dut_reg_cmd("sta_set_opportunistic_ps", req_intf,
			  cmd_sta_set_opportunistic_ps);
	sigma_dut_reg_cmd("sta_send_service_discovery_req", req_intf,
			  cmd_sta_send_service_discovery_req);
	sigma_dut_reg_cmd("sta_add_arp_table_entry", req_intf,
			  cmd_sta_add_arp_table_entry);
	sigma_dut_reg_cmd("sta_block_icmp_response", req_intf,
			  cmd_sta_block_icmp_response);
#endif    
}
