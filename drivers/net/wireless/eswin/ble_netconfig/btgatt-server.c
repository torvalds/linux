// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2014  Google Inc.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

#include "lib/bluetooth.h"
#include "lib/hci.h"
#include "lib/hci_lib.h"
#include "lib/l2cap.h"
#include "lib/uuid.h"

#include "src/shared/mainloop.h"
#include "src/shared/util.h"
#include "src/shared/att.h"
#include "src/shared/queue.h"
#include "src/shared/timeout.h"
#include "src/shared/gatt-db.h"
#include "src/shared/gatt-server.h"

#define UUID_GAP			0x1800
#define UUID_GATT			0x1801
#define UUID_HEART_RATE			0x180d
#define UUID_HEART_RATE_MSRMT		0x2a37
#define UUID_HEART_RATE_BODY		0x2a38
#define UUID_HEART_RATE_CTRL		0x2a39

#define ESWIN_TEST  1

#ifdef ESWIN_TEST
#define UUID_NETCFG         0x1920
#define UUID_SSID           0x2b10
#define UUID_PASSWORD       0x2b11
#define UUID_NETSTATUS      0x2b12
#define CFG_ITEM_LEN    40
#define CFG_FLAG_SSID  0x01
#define CFG_FLAG_PWD   0x02
#define CFG_FLAG_ALL   0x03
#endif

#define ATT_CID 4

#define PRLOG(...) \
	do { \
		printf(__VA_ARGS__); \
		print_prompt(); \
	} while (0)

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define COLOR_OFF	"\x1B[0m"
#define COLOR_RED	"\x1B[0;91m"
#define COLOR_GREEN	"\x1B[0;92m"
#define COLOR_YELLOW	"\x1B[0;93m"
#define COLOR_BLUE	"\x1B[0;94m"
#define COLOR_MAGENTA	"\x1B[0;95m"
#define COLOR_BOLDGRAY	"\x1B[1;30m"
#define COLOR_BOLDWHITE	"\x1B[1;37m"

//static const char test_device_name[] = "Very Long Test Device Name For Testing "
//				"ATT Protocol Operations On GATT Server";
static const char test_device_name[] = "BlueZTest";

static bool verbose = false;

#ifdef ESWIN_TEST
static uint8_t netcfg_ssid[CFG_ITEM_LEN] = "";
static uint8_t netcfg_pwd[CFG_ITEM_LEN] = "";
static uint8_t net_status[CFG_ITEM_LEN] = "";
static uint8_t ssid_len = 0;
static uint8_t pwd_len = 0;
static uint8_t netcfg_flag = 0;
#endif


struct server {
	int fd;
	struct bt_att *att;
	struct gatt_db *db;
	struct bt_gatt_server *gatt;

	uint8_t *device_name;
	size_t name_len;

	uint16_t gatt_svc_chngd_handle;
	bool svc_chngd_enabled;

	uint16_t hr_handle;
	uint16_t hr_msrmt_handle;
	uint16_t hr_energy_expended;
	bool hr_visible;
	bool hr_msrmt_enabled;
	int hr_ee_count;
	unsigned int hr_timeout_id;
#ifdef ESWIN_TEST
	uint16_t netstatus_handle;
	bool netstatus_enabled;
	unsigned int netstatus_timeout_id;
#endif
};

static void print_prompt(void)
{
	printf(COLOR_BLUE "[GATT server]" COLOR_OFF "# ");
	fflush(stdout);
}

static void att_disconnect_cb(int err, void *user_data)
{
	printf("Device disconnected: %s\n", strerror(err));

	mainloop_quit();
}

static void att_debug_cb(const char *str, void *user_data)
{
	const char *prefix = user_data;

	PRLOG(COLOR_BOLDGRAY "%s" COLOR_BOLDWHITE "%s\n" COLOR_OFF, prefix,
									str);
}

static void gatt_debug_cb(const char *str, void *user_data)
{
	const char *prefix = user_data;

	PRLOG(COLOR_GREEN "%s%s\n" COLOR_OFF, prefix, str);
}

static void gap_device_name_read_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t error = 0;
	size_t len = 0;
	const uint8_t *value = NULL;

	PRLOG("GAP Device Name Read called\n");

	len = server->name_len;

	if (offset > len) {
		error = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	len -= offset;
	value = len ? &server->device_name[offset] : NULL;

done:
	gatt_db_attribute_read_result(attrib, id, error, value, len);
}

static void gap_device_name_write_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					const uint8_t *value, size_t len,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t error = 0;

	PRLOG("GAP Device Name Write called\n");

	/* If the value is being completely truncated, clean up and return */
	if (!(offset + len)) {
		free(server->device_name);
		server->device_name = NULL;
		server->name_len = 0;
		goto done;
	}

	/* Implement this as a variable length attribute value. */
	if (offset > server->name_len) {
		error = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	if (offset + len != server->name_len) {
		uint8_t *name;

		name = realloc(server->device_name, offset + len);
		if (!name) {
			error = BT_ATT_ERROR_INSUFFICIENT_RESOURCES;
			goto done;
		}

		server->device_name = name;
		server->name_len = offset + len;
	}

	if (value)
		memcpy(server->device_name + offset, value, len);

done:
	gatt_db_attribute_write_result(attrib, id, error);
}

static void gap_device_name_ext_prop_read_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	uint8_t value[2];

	PRLOG("Device Name Extended Properties Read called\n");

	value[0] = BT_GATT_CHRC_EXT_PROP_RELIABLE_WRITE;
	value[1] = 0;

	gatt_db_attribute_read_result(attrib, id, 0, value, sizeof(value));
}

static void gatt_service_changed_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	PRLOG("Service Changed Read called\n");

	gatt_db_attribute_read_result(attrib, id, 0, NULL, 0);
}

static void gatt_svc_chngd_ccc_read_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t value[2];

	PRLOG("Service Changed CCC Read called\n");

	value[0] = server->svc_chngd_enabled ? 0x02 : 0x00;
	value[1] = 0x00;

	gatt_db_attribute_read_result(attrib, id, 0, value, sizeof(value));
}

static void gatt_svc_chngd_ccc_write_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					const uint8_t *value, size_t len,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t ecode = 0;

	PRLOG("Service Changed CCC Write called\n");

	if (!value || len != 2) {
		ecode = BT_ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LEN;
		goto done;
	}

	if (offset) {
		ecode = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	if (value[0] == 0x00)
		server->svc_chngd_enabled = false;
	else if (value[0] == 0x02)
		server->svc_chngd_enabled = true;
	else
		ecode = 0x80;

	PRLOG("Service Changed Enabled: %s\n",
				server->svc_chngd_enabled ? "true" : "false");

done:
	gatt_db_attribute_write_result(attrib, id, ecode);
}

static void hr_msrmt_ccc_read_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t value[2];

	value[0] = server->hr_msrmt_enabled ? 0x01 : 0x00;
	value[1] = 0x00;

	gatt_db_attribute_read_result(attrib, id, 0, value, 2);
}

static bool hr_msrmt_cb(void *user_data)
{
	struct server *server = user_data;
	bool expended_present = !(server->hr_ee_count % 10);
	uint16_t len = 2;
	uint8_t pdu[4];
	uint32_t cur_ee;

	pdu[0] = 0x06;
	pdu[1] = 90 + (rand() % 40);

	if (expended_present) {
		pdu[0] |= 0x08;
		put_le16(server->hr_energy_expended, pdu + 2);
		len += 2;
	}

	bt_gatt_server_send_notification(server->gatt,
						server->hr_msrmt_handle,
						pdu, len, false);


	cur_ee = server->hr_energy_expended;
	server->hr_energy_expended = MIN(UINT16_MAX, cur_ee + 10);
	server->hr_ee_count++;

	return true;
}

static void update_hr_msrmt_simulation(struct server *server)
{
	if (!server->hr_msrmt_enabled || !server->hr_visible) {
		timeout_remove(server->hr_timeout_id);
		return;
	}

	server->hr_timeout_id = timeout_add(1000, hr_msrmt_cb, server, NULL);
}

static void hr_msrmt_ccc_write_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					const uint8_t *value, size_t len,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t ecode = 0;

	if (!value || len != 2) {
		ecode = BT_ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LEN;
		goto done;
	}

	if (offset) {
		ecode = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	if (value[0] == 0x00)
		server->hr_msrmt_enabled = false;
	else if (value[0] == 0x01) {
		if (server->hr_msrmt_enabled) {
			PRLOG("HR Measurement Already Enabled\n");
			goto done;
		}

		server->hr_msrmt_enabled = true;
	} else
		ecode = 0x80;

	PRLOG("HR: Measurement Enabled: %s\n",
				server->hr_msrmt_enabled ? "true" : "false");

	update_hr_msrmt_simulation(server);

done:
	gatt_db_attribute_write_result(attrib, id, ecode);
}

static void hr_control_point_write_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					const uint8_t *value, size_t len,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t ecode = 0;

	if (!value || len != 1) {
		ecode = BT_ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LEN;
		goto done;
	}

	if (offset) {
		ecode = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	if (value[0] == 1) {
		PRLOG("HR: Energy Expended value reset\n");
		server->hr_energy_expended = 0;
	}

done:
	gatt_db_attribute_write_result(attrib, id, ecode);
}

static void confirm_write(struct gatt_db_attribute *attr, int err,
							void *user_data)
{
	if (!err)
		return;

	fprintf(stderr, "Error caching attribute %p - err: %d\n", attr, err);
	exit(1);
}

#ifdef ESWIN_TEST
static void modify_wpa_conf(void)
{
	FILE *fp;
	char ssid_str[2*CFG_ITEM_LEN]={0};
	char pwd_str[2*CFG_ITEM_LEN]={0};
	PRLOG("enter\n");

	fp = fopen("/etc/wpa_supplicant/wpa_supplicant.conf", "we");
	if (!fp){
		PRLOG("fopen fail\n");
		return;
	}
	snprintf(ssid_str, sizeof(ssid_str), "ssid=\"%s\"\n", netcfg_ssid);
	snprintf(pwd_str, sizeof(pwd_str), "psk=\"%s\"\n", netcfg_pwd);

	fputs("ctrl_interface=DIR=/var/run/wpa_supplicant\n",fp);
	fputs("update_config=1\n", fp);
	fputs("network={\n", fp);
	fputs(ssid_str, fp);
	fputs(pwd_str, fp);
	fputs("}\n", fp);
	

	fclose(fp);
	PRLOG("exit\n");
}

static int wifi_run_cmd(char *cmd)
{
	int ret = 0;
	ret = system(cmd);
	if(ret < 0)	{
		PRLOG("cmd=%s\t error:%s\n",cmd, strerror(errno));
		return -1;
	}
	if(WIFEXITED(ret)){

		PRLOG("success,cmd=%s,status=%d\n",cmd,WEXITSTATUS(ret));
		return WEXITSTATUS(ret);
	}

	return -1;	

}
static void update_wpa_network(void)
{
	char sys_cmd[100] = {0};
	char temp[CFG_ITEM_LEN] = {0};
	int result = -1;
          
	sprintf(sys_cmd, "cd /wpa_supplicant-2.10/wpa_supplicant");
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}

	sprintf(sys_cmd, "sudo killall wpa_supplicant");
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}

	sprintf(sys_cmd, "sudo /wpa_supplicant-2.10/wpa_supplicant/wpa_supplicant -B -c/etc/wpa_supplicant/wpa_supplicant.conf -iwlan0 -Dnl80211,wext");
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}


	sprintf(sys_cmd, "sudo /wpa_supplicant-2.10/wpa_supplicant/wpa_cli -i wlan0 remove_network all");
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}
	memset(sys_cmd, 0x00, 100);
	sprintf(sys_cmd, "sudo /wpa_supplicant-2.10/wpa_supplicant/wpa_cli -i wlan0 add_network");
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}
	memset(sys_cmd, 0x00, 100);
	sprintf(sys_cmd, "sudo /wpa_supplicant-2.10/wpa_supplicant/wpa_cli -i wlan0 set_network 0 ssid ");
	snprintf(temp, sizeof(temp),"'\"%s\"'", netcfg_ssid);
	strcat(sys_cmd,temp);
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}

	memset(sys_cmd, 0x00, 100);
	memset(temp, 0x00, CFG_ITEM_LEN);
	sprintf(sys_cmd, "sudo /wpa_supplicant-2.10/wpa_supplicant/wpa_cli -i wlan0 set_network 0 psk ");
	snprintf(temp, sizeof(temp),"'\"%s\"'", netcfg_pwd);
	strcat(sys_cmd,temp);
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}

	memset(sys_cmd, 0x00, 100);
	sprintf(sys_cmd, "sudo /wpa_supplicant-2.10/wpa_supplicant/wpa_cli -i wlan0 save_config");
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}

	memset(sys_cmd, 0x00, 100);
	sprintf(sys_cmd, "sudo /wpa_supplicant-2.10/wpa_supplicant/wpa_cli -i wlan0 select_network 0");
	result = wifi_run_cmd(sys_cmd);
	if(result < 0){
		return;
	}

	PRLOG("update_wpa_network finish\n");
}

static void eswin_netcfg_wpa_conf(int flag)
{
	netcfg_flag |= flag;

	if(netcfg_flag == CFG_FLAG_ALL){
		netcfg_flag = 0;
		//modify_wpa_conf();
		update_wpa_network();
	}
}




static void eswin_netcfg_ssid_read_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t error = 0;
	size_t len = 0;
	const uint8_t *value = NULL;

	//PRLOG("ESWIN SSID Read called ssid_len=%d,offset=%d\n",ssid_len,offset);

	if (offset > ssid_len) {
		error = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	len = ssid_len - offset;
	value = len ? &netcfg_ssid[offset] : NULL;

done:
	gatt_db_attribute_read_result(attrib, id, error, value, len);
}


static void eswin_netcfg_ssid_write_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					const uint8_t *value, size_t len,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t error = 0;

	//PRLOG("ESWIN SSID write called offset=%d len=%d\n",offset, len);
	
	memset(netcfg_ssid, 0x00, CFG_ITEM_LEN);
	ssid_len = 0;

	if (offset+len >= CFG_ITEM_LEN) {
		error = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	if (value)
		memcpy(netcfg_ssid + offset, value, len);

	ssid_len +=len;
	eswin_netcfg_wpa_conf(CFG_FLAG_SSID);
done:
	gatt_db_attribute_write_result(attrib, id, error);
	
}


static void eswin_netcfg_pwd_read_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t error = 0;
	size_t len = 0;
	const uint8_t *value = NULL;

	//PRLOG("ESWIN pwd Read called pwd_len=%d,offset=%d\n",pwd_len,offset);

	if (offset > pwd_len) {
		error = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	len = pwd_len - offset;
	value = len ? &netcfg_pwd[offset] : NULL;

done:
	gatt_db_attribute_read_result(attrib, id, error, value, len);
}


static void eswin_netcfg_pwd_write_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					const uint8_t *value, size_t len,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t error = 0;

	//PRLOG("ESWIN pwd write called offset=%d len=%d\n",offset, len);
	
	memset(netcfg_pwd, 0x00, CFG_ITEM_LEN);
	pwd_len = 0;

	if (offset+len >= CFG_ITEM_LEN) {
		error = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	if (value)
		memcpy(netcfg_pwd + offset, value, len);

	pwd_len +=len;
	eswin_netcfg_wpa_conf(CFG_FLAG_PWD);
done:
	gatt_db_attribute_write_result(attrib, id, error);
	
}

static void eswin_netstatus_ccc_read_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t value[2];

	value[0] = server->netstatus_enabled ? 0x01 : 0x00;
	value[1] = 0x00;

	gatt_db_attribute_read_result(attrib, id, 0, value, 2);
}

#define WAP_STATE  "wpa_state="
static bool netstatus_cb(void *user_data)
{
	struct server *server = user_data;
	FILE *fp = NULL;
	char buff[500] = {0};
	char *ptr = NULL, *ptr_end = NULL;
	int head_len = strlen(WAP_STATE);
	uint8_t cur_status[CFG_ITEM_LEN] = "";

	fp = popen("sudo wpa_cli -i wlan0 status", "r");

	fread(buff, 1, 499, fp);
	ptr = strstr(buff, WAP_STATE);
	if (ptr == NULL) {
		PRLOG("no wpa_state\n");
		return false;
	}

	ptr_end = strchr(ptr, '\n');
	if (ptr_end == NULL) {
		PRLOG("no wpa_state end\n");
		return false;
	}
	strncpy(cur_status, ptr+head_len, ptr_end-ptr-head_len);
	PRLOG("%s\n", cur_status);

	if(strcmp(cur_status, net_status) != 0)	{
		memset(net_status, 0x00, CFG_ITEM_LEN);
		strcpy(net_status, cur_status);
		PRLOG("bt_gatt_server_send_notification\n");

		bt_gatt_server_send_notification(server->gatt,
						server->netstatus_handle,
						net_status, strlen(net_status), false);
	}
	return true;
}

static void update_netstatus_simulation(struct server *server)
{
	if (!server->netstatus_enabled) {
		timeout_remove(server->netstatus_timeout_id);
		return;
	}

	server->netstatus_timeout_id = timeout_add(1000, netstatus_cb, server, NULL);
}


static void eswin_netstatus_ccc_write_cb(struct gatt_db_attribute *attrib,
					unsigned int id, uint16_t offset,
					const uint8_t *value, size_t len,
					uint8_t opcode, struct bt_att *att,
					void *user_data)
{
	struct server *server = user_data;
	uint8_t ecode = 0;

	if (!value || len != 2) {
		ecode = BT_ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LEN;
		goto done;
	}

	if (offset) {
		ecode = BT_ATT_ERROR_INVALID_OFFSET;
		goto done;
	}

	if (value[0] == 0x00)
		server->netstatus_enabled = false;
	else if (value[0] == 0x01) {		
		server->netstatus_enabled = true;		
	} else
		ecode = 0x80;

	PRLOG("netstatus Enabled: %s\n",
				server->netstatus_enabled ? "true" : "false");
	update_netstatus_simulation(server);

done:
	gatt_db_attribute_write_result(attrib, id, ecode);
}

#endif

static void populate_gap_service(struct server *server)
{
	bt_uuid_t uuid;
	struct gatt_db_attribute *service, *tmp;
	uint16_t appearance;

	/* Add the GAP service */
	bt_uuid16_create(&uuid, UUID_GAP);
	service = gatt_db_add_service(server->db, &uuid, true, 6);

	/*
	 * Device Name characteristic. Make the value dynamically read and
	 * written via callbacks.
	 */
	bt_uuid16_create(&uuid, GATT_CHARAC_DEVICE_NAME);
	gatt_db_service_add_characteristic(service, &uuid,
					BT_ATT_PERM_READ | BT_ATT_PERM_WRITE,
					BT_GATT_CHRC_PROP_READ |
					BT_GATT_CHRC_PROP_EXT_PROP,
					gap_device_name_read_cb,
					gap_device_name_write_cb,
					server);

	bt_uuid16_create(&uuid, GATT_CHARAC_EXT_PROPER_UUID);
	gatt_db_service_add_descriptor(service, &uuid, BT_ATT_PERM_READ,
					gap_device_name_ext_prop_read_cb,
					NULL, server);

	/*
	 * Appearance characteristic. Reads and writes should obtain the value
	 * from the database.
	 */
	bt_uuid16_create(&uuid, GATT_CHARAC_APPEARANCE);
	tmp = gatt_db_service_add_characteristic(service, &uuid,
							BT_ATT_PERM_READ,
							BT_GATT_CHRC_PROP_READ,
							NULL, NULL, server);

	/*
	 * Write the appearance value to the database, since we're not using a
	 * callback.
	 */
	put_le16(128, &appearance);
	gatt_db_attribute_write(tmp, 0, (void *) &appearance,
							sizeof(appearance),
							BT_ATT_OP_WRITE_REQ,
							NULL, confirm_write,
							NULL);

	gatt_db_service_set_active(service, true);
}

static void populate_gatt_service(struct server *server)
{
	bt_uuid_t uuid;
	struct gatt_db_attribute *service, *svc_chngd;

	/* Add the GATT service */
	bt_uuid16_create(&uuid, UUID_GATT);
	service = gatt_db_add_service(server->db, &uuid, true, 4);

	bt_uuid16_create(&uuid, GATT_CHARAC_SERVICE_CHANGED);
	svc_chngd = gatt_db_service_add_characteristic(service, &uuid,
			BT_ATT_PERM_READ,
			BT_GATT_CHRC_PROP_READ | BT_GATT_CHRC_PROP_INDICATE,
			gatt_service_changed_cb,
			NULL, server);
	server->gatt_svc_chngd_handle = gatt_db_attribute_get_handle(svc_chngd);

	bt_uuid16_create(&uuid, GATT_CLIENT_CHARAC_CFG_UUID);
	gatt_db_service_add_descriptor(service, &uuid,
				BT_ATT_PERM_READ | BT_ATT_PERM_WRITE,
				gatt_svc_chngd_ccc_read_cb,
				gatt_svc_chngd_ccc_write_cb, server);

	gatt_db_service_set_active(service, true);
}

static void populate_hr_service(struct server *server)
{
	bt_uuid_t uuid;
	struct gatt_db_attribute *service, *hr_msrmt, *body;
	uint8_t body_loc = 1;  /* "Chest" */

	/* Add Heart Rate Service */
	bt_uuid16_create(&uuid, UUID_HEART_RATE);
	service = gatt_db_add_service(server->db, &uuid, true, 8);
	server->hr_handle = gatt_db_attribute_get_handle(service);

	/* HR Measurement Characteristic */
	bt_uuid16_create(&uuid, UUID_HEART_RATE_MSRMT);
	hr_msrmt = gatt_db_service_add_characteristic(service, &uuid,
						BT_ATT_PERM_NONE,
						BT_GATT_CHRC_PROP_NOTIFY,
						NULL, NULL, NULL);
	server->hr_msrmt_handle = gatt_db_attribute_get_handle(hr_msrmt);

	bt_uuid16_create(&uuid, GATT_CLIENT_CHARAC_CFG_UUID);
	gatt_db_service_add_descriptor(service, &uuid,
					BT_ATT_PERM_READ | BT_ATT_PERM_WRITE,
					hr_msrmt_ccc_read_cb,
					hr_msrmt_ccc_write_cb, server);

	/*
	 * Body Sensor Location Characteristic. Make reads obtain the value from
	 * the database.
	 */
	bt_uuid16_create(&uuid, UUID_HEART_RATE_BODY);
	body = gatt_db_service_add_characteristic(service, &uuid,
						BT_ATT_PERM_READ,
						BT_GATT_CHRC_PROP_READ,
						NULL, NULL, server);
	gatt_db_attribute_write(body, 0, (void *) &body_loc, sizeof(body_loc),
							BT_ATT_OP_WRITE_REQ,
							NULL, confirm_write,
							NULL);

	/* HR Control Point Characteristic */
	bt_uuid16_create(&uuid, UUID_HEART_RATE_CTRL);
	gatt_db_service_add_characteristic(service, &uuid,
						BT_ATT_PERM_WRITE,
						BT_GATT_CHRC_PROP_WRITE,
						NULL, hr_control_point_write_cb,
						server);

	if (server->hr_visible)
		gatt_db_service_set_active(service, true);
}

#ifdef ESWIN_TEST
static void populate_eswin_service(struct server *server)
{
	bt_uuid_t uuid;
	struct gatt_db_attribute *service, *netstatus;

	ssid_len = strlen(netcfg_ssid);
	pwd_len = strlen(netcfg_pwd);
	server->netstatus_enabled = true;

	/* Add Net config Service */
	bt_uuid16_create(&uuid, UUID_NETCFG);
	service = gatt_db_add_service(server->db, &uuid, true, 10);

	/* SSID Characteristic */
	bt_uuid16_create(&uuid, UUID_SSID);
	gatt_db_service_add_characteristic(service, &uuid,
						BT_ATT_PERM_READ | BT_ATT_PERM_WRITE,
						BT_GATT_CHRC_PROP_READ |
						BT_GATT_CHRC_PROP_WRITE,
						eswin_netcfg_ssid_read_cb,
						eswin_netcfg_ssid_write_cb,
						server);
	

	/*
	 * PASSWORD Characteristic.
	 */
	bt_uuid16_create(&uuid, UUID_PASSWORD);
	gatt_db_service_add_characteristic(service, &uuid,
						BT_ATT_PERM_READ| BT_ATT_PERM_WRITE,
						BT_GATT_CHRC_PROP_READ |
						BT_GATT_CHRC_PROP_WRITE,
						eswin_netcfg_pwd_read_cb,
						eswin_netcfg_pwd_write_cb,
						server);

	/*
	 * NETSTATUS Characteristic.
	 */
	bt_uuid16_create(&uuid, UUID_NETSTATUS);
	netstatus = gatt_db_service_add_characteristic(service, &uuid,
						BT_ATT_PERM_NONE,
						BT_GATT_CHRC_PROP_NOTIFY,
						NULL, NULL, NULL);
	server->netstatus_handle = gatt_db_attribute_get_handle(netstatus);

	bt_uuid16_create(&uuid, GATT_CLIENT_CHARAC_CFG_UUID);
	gatt_db_service_add_descriptor(service, &uuid,
					BT_ATT_PERM_READ | BT_ATT_PERM_WRITE,
					eswin_netstatus_ccc_read_cb,
					eswin_netstatus_ccc_write_cb, server);

	gatt_db_service_set_active(service, true);
}

#endif

static void populate_db(struct server *server)
{
	populate_gap_service(server);
	populate_gatt_service(server);	
	populate_hr_service(server);
#ifdef ESWIN_TEST
	populate_eswin_service(server);
#endif
}

static struct server *server_create(int fd, uint16_t mtu, bool hr_visible)
{
	struct server *server;
	size_t name_len = strlen(test_device_name);

	server = new0(struct server, 1);
	if (!server) {
		fprintf(stderr, "Failed to allocate memory for server\n");
		return NULL;
	}

	server->att = bt_att_new(fd, false);
	if (!server->att) {
		fprintf(stderr, "Failed to initialze ATT transport layer\n");
		goto fail;
	}

	if (!bt_att_set_close_on_unref(server->att, true)) {
		fprintf(stderr, "Failed to set up ATT transport layer\n");
		goto fail;
	}

	if (!bt_att_register_disconnect(server->att, att_disconnect_cb, NULL,
									NULL)) {
		fprintf(stderr, "Failed to set ATT disconnect handler\n");
		goto fail;
	}

	server->name_len = name_len + 1;
	server->device_name = malloc(name_len + 1);
	if (!server->device_name) {
		fprintf(stderr, "Failed to allocate memory for device name\n");
		goto fail;
	}

	memcpy(server->device_name, test_device_name, name_len);
	server->device_name[name_len] = '\0';

	server->fd = fd;
	server->db = gatt_db_new();
	if (!server->db) {
		fprintf(stderr, "Failed to create GATT database\n");
		goto fail;
	}

	server->gatt = bt_gatt_server_new(server->db, server->att, mtu, 0);
	if (!server->gatt) {
		fprintf(stderr, "Failed to create GATT server\n");
		goto fail;
	}

	server->hr_visible = hr_visible;

	if (verbose) {
		bt_att_set_debug(server->att, BT_ATT_DEBUG_VERBOSE,
						att_debug_cb, "att: ", NULL);
		bt_gatt_server_set_debug(server->gatt, gatt_debug_cb,
							"server: ", NULL);
	}

	/* Random seed for generating fake Heart Rate measurements */
	srand(time(NULL));

	/* bt_gatt_server already holds a reference */
	populate_db(server);

	return server;

fail:
	gatt_db_unref(server->db);
	free(server->device_name);
	bt_att_unref(server->att);
	free(server);

	return NULL;
}

static void server_destroy(struct server *server)
{
	timeout_remove(server->hr_timeout_id);
	bt_gatt_server_unref(server->gatt);
	gatt_db_unref(server->db);
}

static void usage(void)
{
	printf("btgatt-server\n");
	printf("Usage:\n\tbtgatt-server [options]\n");

	printf("Options:\n"
		"\t-i, --index <id>\t\tSpecify adapter index, e.g. hci0\n"
		"\t-m, --mtu <mtu>\t\t\tThe ATT MTU to use\n"
		"\t-s, --security-level <sec>\tSet security level (low|"
								"medium|high)\n"
		"\t-t, --type [random|public] \t The source address type\n"
		"\t-v, --verbose\t\t\tEnable extra logging\n"
		"\t-r, --heart-rate\t\tEnable Heart Rate service\n"
		"\t-h, --help\t\t\tDisplay help\n");
}

static struct option main_options[] = {
	{ "index",		1, 0, 'i' },
	{ "mtu",		1, 0, 'm' },
	{ "security-level",	1, 0, 's' },
	{ "type",		1, 0, 't' },
	{ "verbose",		0, 0, 'v' },
	{ "heart-rate",		0, 0, 'r' },
	{ "help",		0, 0, 'h' },
	{ }
};

static int l2cap_le_att_listen_and_accept(bdaddr_t *src, int sec,
							uint8_t src_type)
{
	int sk, nsk;
	struct sockaddr_l2 srcaddr, addr;
	socklen_t optlen;
	struct bt_security btsec;
	char ba[18];

	sk = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
	if (sk < 0) {
		perror("Failed to create L2CAP socket");
		return -1;
	}

	/* Set up source address */
	memset(&srcaddr, 0, sizeof(srcaddr));
	srcaddr.l2_family = AF_BLUETOOTH;
	srcaddr.l2_cid = htobs(ATT_CID);
	srcaddr.l2_bdaddr_type = src_type;
	bacpy(&srcaddr.l2_bdaddr, src);

	if (bind(sk, (struct sockaddr *) &srcaddr, sizeof(srcaddr)) < 0) {
		perror("Failed to bind L2CAP socket");
		goto fail;
	}

	/* Set the security level */
	memset(&btsec, 0, sizeof(btsec));
	btsec.level = sec;
	if (setsockopt(sk, SOL_BLUETOOTH, BT_SECURITY, &btsec,
							sizeof(btsec)) != 0) {
		fprintf(stderr, "Failed to set L2CAP security level\n");
		goto fail;
	}

	if (listen(sk, 10) < 0) {
		perror("Listening on socket failed");
		goto fail;
	}

	printf("Started listening on ATT channel. Waiting for connections\n");

	memset(&addr, 0, sizeof(addr));
	optlen = sizeof(addr);
	nsk = accept(sk, (struct sockaddr *) &addr, &optlen);
	if (nsk < 0) {
		perror("Accept failed");
		goto fail;
	}

	ba2str(&addr.l2_bdaddr, ba);
	printf("Connect from %s\n", ba);
	close(sk);

	return nsk;

fail:
	close(sk);
	return -1;
}

static void notify_usage(void)
{
	printf("Usage: notify [options] <value_handle> <value>\n"
					"Options:\n"
					"\t -i, --indicate\tSend indication\n"
					"e.g.:\n"
					"\tnotify 0x0001 00 01 00\n");
}

static struct option notify_options[] = {
	{ "indicate",	0, 0, 'i' },
	{ }
};

static bool parse_args(char *str, int expected_argc,  char **argv, int *argc)
{
	char **ap;

	for (ap = argv; (*ap = strsep(&str, " \t")) != NULL;) {
		if (**ap == '\0')
			continue;

		(*argc)++;
		ap++;

		if (*argc > expected_argc)
			return false;
	}

	return true;
}

static void conf_cb(void *user_data)
{
	PRLOG("Received confirmation\n");
}

static void cmd_notify(struct server *server, char *cmd_str)
{
	int opt, i;
	char *argvbuf[516];
	char **argv = argvbuf;
	int argc = 1;
	uint16_t handle;
	char *endptr = NULL;
	int length;
	uint8_t *value = NULL;
	bool indicate = false;

	if (!parse_args(cmd_str, 514, argv + 1, &argc)) {
		printf("Too many arguments\n");
		notify_usage();
		return;
	}

	optind = 0;
	argv[0] = "notify";
	while ((opt = getopt_long(argc, argv, "+i", notify_options,
								NULL)) != -1) {
		switch (opt) {
		case 'i':
			indicate = true;
			break;
		default:
			notify_usage();
			return;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		notify_usage();
		return;
	}

	handle = strtol(argv[0], &endptr, 16);
	if (!endptr || *endptr != '\0' || !handle) {
		printf("Invalid handle: %s\n", argv[0]);
		return;
	}

	length = argc - 1;

	if (length > 0) {
		if (length > UINT16_MAX) {
			printf("Value too long\n");
			return;
		}

		value = malloc(length);
		if (!value) {
			printf("Failed to construct value\n");
			return;
		}

		for (i = 1; i < argc; i++) {
			if (strlen(argv[i]) != 2) {
				printf("Invalid value byte: %s\n",
								argv[i]);
				goto done;
			}

			value[i-1] = strtol(argv[i], &endptr, 16);
			if (endptr == argv[i] || *endptr != '\0'
							|| errno == ERANGE) {
				printf("Invalid value byte: %s\n",
								argv[i]);
				goto done;
			}
		}
	}

	if (indicate) {
		if (!bt_gatt_server_send_indication(server->gatt, handle,
							value, length,
							conf_cb, NULL, NULL))
			printf("Failed to initiate indication\n");
	} else if (!bt_gatt_server_send_notification(server->gatt, handle,
							value, length, false))
		printf("Failed to initiate notification\n");

done:
	free(value);
}

static void heart_rate_usage(void)
{
	printf("Usage: heart-rate on|off\n");
}

static void cmd_heart_rate(struct server *server, char *cmd_str)
{
	bool enable;
	uint8_t pdu[4];
	struct gatt_db_attribute *attr;

	if (!cmd_str) {
		heart_rate_usage();
		return;
	}

	if (strcmp(cmd_str, "on") == 0)
		enable = true;
	else if (strcmp(cmd_str, "off") == 0)
		enable = false;
	else {
		heart_rate_usage();
		return;
	}

	if (enable == server->hr_visible) {
		printf("Heart Rate Service already %s\n",
						enable ? "visible" : "hidden");
		return;
	}

	server->hr_visible = enable;
	attr = gatt_db_get_attribute(server->db, server->hr_handle);
	gatt_db_service_set_active(attr, server->hr_visible);
	update_hr_msrmt_simulation(server);

	if (!server->svc_chngd_enabled)
		return;

	put_le16(server->hr_handle, pdu);
	put_le16(server->hr_handle + 7, pdu + 2);

	server->hr_msrmt_enabled = false;
	update_hr_msrmt_simulation(server);

	bt_gatt_server_send_indication(server->gatt,
						server->gatt_svc_chngd_handle,
						pdu, 4, conf_cb, NULL, NULL);
}

static void print_uuid(const bt_uuid_t *uuid)
{
	char uuid_str[MAX_LEN_UUID_STR];
	bt_uuid_t uuid128;

	bt_uuid_to_uuid128(uuid, &uuid128);
	bt_uuid_to_string(&uuid128, uuid_str, sizeof(uuid_str));

	printf("%s\n", uuid_str);
}

static void print_incl(struct gatt_db_attribute *attr, void *user_data)
{
	struct server *server = user_data;
	uint16_t handle, start, end;
	struct gatt_db_attribute *service;
	bt_uuid_t uuid;

	if (!gatt_db_attribute_get_incl_data(attr, &handle, &start, &end))
		return;

	service = gatt_db_get_attribute(server->db, start);
	if (!service)
		return;

	gatt_db_attribute_get_service_uuid(service, &uuid);

	printf("\t  " COLOR_GREEN "include" COLOR_OFF " - handle: "
					"0x%04x, - start: 0x%04x, end: 0x%04x,"
					"uuid: ", handle, start, end);
	print_uuid(&uuid);
}

static void print_desc(struct gatt_db_attribute *attr, void *user_data)
{
	printf("\t\t  " COLOR_MAGENTA "descr" COLOR_OFF
					" - handle: 0x%04x, uuid: ",
					gatt_db_attribute_get_handle(attr));
	print_uuid(gatt_db_attribute_get_type(attr));
}

static void print_chrc(struct gatt_db_attribute *attr, void *user_data)
{
	uint16_t handle, value_handle;
	uint8_t properties;
	uint16_t ext_prop;
	bt_uuid_t uuid;

	if (!gatt_db_attribute_get_char_data(attr, &handle,
								&value_handle,
								&properties,
								&ext_prop,
								&uuid))
		return;

	printf("\t  " COLOR_YELLOW "charac" COLOR_OFF
				" - start: 0x%04x, value: 0x%04x, "
				"props: 0x%02x, ext_prop: 0x%04x, uuid: ",
				handle, value_handle, properties, ext_prop);
	print_uuid(&uuid);

	gatt_db_service_foreach_desc(attr, print_desc, NULL);
}

static void print_service(struct gatt_db_attribute *attr, void *user_data)
{
	struct server *server = user_data;
	uint16_t start, end;
	bool primary;
	bt_uuid_t uuid;

	if (!gatt_db_attribute_get_service_data(attr, &start, &end, &primary,
									&uuid))
		return;

	printf(COLOR_RED "service" COLOR_OFF " - start: 0x%04x, "
				"end: 0x%04x, type: %s, uuid: ",
				start, end, primary ? "primary" : "secondary");
	print_uuid(&uuid);

	gatt_db_service_foreach_incl(attr, print_incl, server);
	gatt_db_service_foreach_char(attr, print_chrc, NULL);

	printf("\n");
}

static void cmd_services(struct server *server, char *cmd_str)
{
	gatt_db_foreach_service(server->db, NULL, print_service, server);
}

static bool convert_sign_key(char *optarg, uint8_t key[16])
{
	int i;

	if (strlen(optarg) != 32) {
		printf("sign-key length is invalid\n");
		return false;
	}

	for (i = 0; i < 16; i++) {
		if (sscanf(optarg + (i * 2), "%2hhx", &key[i]) != 1)
			return false;
	}

	return true;
}

static void set_sign_key_usage(void)
{
	printf("Usage: set-sign-key [options]\nOptions:\n"
		"\t -c, --sign-key <remote csrk>\tRemote CSRK\n"
		"e.g.:\n"
		"\tset-sign-key -c D8515948451FEA320DC05A2E88308188\n");
}

static bool remote_counter(uint32_t *sign_cnt, void *user_data)
{
	static uint32_t cnt = 0;

	if (*sign_cnt < cnt)
		return false;

	cnt = *sign_cnt;

	return true;
}

static void cmd_set_sign_key(struct server *server, char *cmd_str)
{
	char *argv[3];
	int argc = 0;
	uint8_t key[16];

	memset(key, 0, 16);

	if (!parse_args(cmd_str, 2, argv, &argc)) {
		set_sign_key_usage();
		return;
	}

	if (argc != 2) {
		set_sign_key_usage();
		return;
	}

	if (!strcmp(argv[0], "-c") || !strcmp(argv[0], "--sign-key")) {
		if (convert_sign_key(argv[1], key))
			bt_att_set_remote_key(server->att, key, remote_counter,
									server);
	} else
		set_sign_key_usage();
}

static void cmd_help(struct server *server, char *cmd_str);

typedef void (*command_func_t)(struct server *server, char *cmd_str);

static struct {
	char *cmd;
	command_func_t func;
	char *doc;
} command[] = {
	{ "help", cmd_help, "\tDisplay help message" },
	{ "notify", cmd_notify, "\tSend handle-value notification" },
	{ "heart-rate", cmd_heart_rate, "\tHide/Unhide Heart Rate Service" },
	{ "services", cmd_services, "\tEnumerate all services" },
	{ "set-sign-key", cmd_set_sign_key,
			"\tSet remote signing key for signed write command"},
	{ }
};

static void cmd_help(struct server *server, char *cmd_str)
{
	int i;

	printf("Commands:\n");
	for (i = 0; command[i].cmd; i++)
		printf("\t%-15s\t%s\n", command[i].cmd, command[i].doc);
}

static void prompt_read_cb(int fd, uint32_t events, void *user_data)
{
	ssize_t read;
	size_t len = 0;
	char *line = NULL;
	char *cmd = NULL, *args;
	struct server *server = user_data;
	int i;

	if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
		mainloop_quit();
		return;
	}

	read = getline(&line, &len, stdin);
	if (read < 0)
		return;

	if (read <= 1) {
		cmd_help(server, NULL);
		print_prompt();
		return;
	}

	line[read-1] = '\0';
	args = line;

	while ((cmd = strsep(&args, " \t")))
		if (*cmd != '\0')
			break;

	if (!cmd)
		goto failed;

	for (i = 0; command[i].cmd; i++) {
		if (strcmp(command[i].cmd, cmd) == 0)
			break;
	}

	if (command[i].cmd)
		command[i].func(server, args);
	else
		fprintf(stderr, "Unknown command: %s\n", line);

failed:
	print_prompt();

	free(line);
}

static void signal_cb(int signum, void *user_data)
{
	switch (signum) {
	case SIGINT:
	case SIGTERM:
		mainloop_quit();
		break;
	default:
		break;
	}
}

int main(int argc, char *argv[])
{
	int opt;
	bdaddr_t src_addr;
	int dev_id = -1;
	int fd;
	int sec = BT_SECURITY_LOW;
	uint8_t src_type = BDADDR_LE_PUBLIC;
	uint16_t mtu = 0;
	bool hr_visible = false;
	struct server *server;

	while ((opt = getopt_long(argc, argv, "+hvrs:t:m:i:",
						main_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'v':
			verbose = true;
			break;
		case 'r':
			hr_visible = true;
			break;
		case 's':
			if (strcmp(optarg, "low") == 0)
				sec = BT_SECURITY_LOW;
			else if (strcmp(optarg, "medium") == 0)
				sec = BT_SECURITY_MEDIUM;
			else if (strcmp(optarg, "high") == 0)
				sec = BT_SECURITY_HIGH;
			else {
				fprintf(stderr, "Invalid security level\n");
				return EXIT_FAILURE;
			}
			break;
		case 't':
			if (strcmp(optarg, "random") == 0)
				src_type = BDADDR_LE_RANDOM;
			else if (strcmp(optarg, "public") == 0)
				src_type = BDADDR_LE_PUBLIC;
			else {
				fprintf(stderr,
					"Allowed types: random, public\n");
				return EXIT_FAILURE;
			}
			break;
		case 'm': {
			int arg;

			arg = atoi(optarg);
			if (arg <= 0) {
				fprintf(stderr, "Invalid MTU: %d\n", arg);
				return EXIT_FAILURE;
			}

			if (arg > UINT16_MAX) {
				fprintf(stderr, "MTU too large: %d\n", arg);
				return EXIT_FAILURE;
			}

			mtu = (uint16_t) arg;
			break;
		}
		case 'i':
			dev_id = hci_devid(optarg);
			if (dev_id < 0) {
				perror("Invalid adapter");
				return EXIT_FAILURE;
			}

			break;
		default:
			fprintf(stderr, "Invalid option: %c\n", opt);
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv -= optind;
	optind = 0;

	if (argc) {
		usage();
		return EXIT_SUCCESS;
	}

	if (dev_id == -1)
		bacpy(&src_addr, BDADDR_ANY);
	else if (hci_devba(dev_id, &src_addr) < 0) {
		perror("Adapter not available");
		return EXIT_FAILURE;
	}

	fd = l2cap_le_att_listen_and_accept(&src_addr, sec, src_type);
	if (fd < 0) {
		fprintf(stderr, "Failed to accept L2CAP ATT connection\n");
		return EXIT_FAILURE;
	}

	mainloop_init();

	server = server_create(fd, mtu, hr_visible);
	if (!server) {
		close(fd);
		return EXIT_FAILURE;
	}

	if (mainloop_add_fd(fileno(stdin),
				EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR,
				prompt_read_cb, server, NULL) < 0) {
		fprintf(stderr, "Failed to initialize console\n");
		server_destroy(server);

		return EXIT_FAILURE;
	}

	printf("Running GATT server\n");

	print_prompt();

	mainloop_run_with_signal(signal_cb, NULL);

	printf("\n\nShutting down...\n");

	server_destroy(server);

	return EXIT_SUCCESS;
}
