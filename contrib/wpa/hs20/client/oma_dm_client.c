/*
 * Hotspot 2.0 - OMA DM client
 * Copyright (c) 2013-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "wpa_helpers.h"
#include "xml-utils.h"
#include "http-utils.h"
#include "utils/browser.h"
#include "osu_client.h"


#define DM_SERVER_INITIATED_MGMT 1200
#define DM_CLIENT_INITIATED_MGMT 1201
#define DM_GENERIC_ALERT 1226

/* OMA-TS-SyncML-RepPro-V1_2_2 - 10. Response Status Codes */
#define DM_RESP_OK 200
#define DM_RESP_AUTH_ACCEPTED 212
#define DM_RESP_CHUNKED_ITEM_ACCEPTED 213
#define DM_RESP_NOT_EXECUTED 215
#define DM_RESP_ATOMIC_ROLL_BACK_OK 216
#define DM_RESP_NOT_MODIFIED 304
#define DM_RESP_BAD_REQUEST 400
#define DM_RESP_UNAUTHORIZED 401
#define DM_RESP_FORBIDDEN 403
#define DM_RESP_NOT_FOUND 404
#define DM_RESP_COMMAND_NOT_ALLOWED 405
#define DM_RESP_OPTIONAL_FEATURE_NOT_SUPPORTED 406
#define DM_RESP_MISSING_CREDENTIALS 407
#define DM_RESP_CONFLICT 409
#define DM_RESP_GONE 410
#define DM_RESP_INCOMPLETE_COMMAND 412
#define DM_RESP_REQ_ENTITY_TOO_LARGE 413
#define DM_RESP_URI_TOO_LONG 414
#define DM_RESP_UNSUPPORTED_MEDIA_TYPE_OR_FORMAT 415
#define DM_RESP_REQ_TOO_BIG 416
#define DM_RESP_ALREADY_EXISTS 418
#define DM_RESP_DEVICE_FULL 420
#define DM_RESP_SIZE_MISMATCH 424
#define DM_RESP_PERMISSION_DENIED 425
#define DM_RESP_COMMAND_FAILED 500
#define DM_RESP_COMMAND_NOT_IMPLEMENTED 501
#define DM_RESP_ATOMIC_ROLL_BACK_FAILED 516

#define DM_HS20_SUBSCRIPTION_CREATION \
	"org.wi-fi.hotspot2dot0.SubscriptionCreation"
#define DM_HS20_SUBSCRIPTION_PROVISIONING \
	"org.wi-fi.hotspot2dot0.SubscriptionProvisioning"
#define DM_HS20_SUBSCRIPTION_REMEDIATION \
	"org.wi-fi.hotspot2dot0.SubscriptionRemediation"
#define DM_HS20_POLICY_UPDATE \
	"org.wi-fi.hotspot2dot0.PolicyUpdate"

#define DM_URI_PPS "./Wi-Fi/org.wi-fi/PerProviderSubscription"
#define DM_URI_LAUNCH_BROWSER \
	"./DevDetail/Ext/org.wi-fi/Wi-Fi/Ops/launchBrowserToURI"


static void add_item(struct hs20_osu_client *ctx, xml_node_t *parent,
		     const char *locuri, const char *data);


static const char * int2str(int val)
{
	static char buf[20];
	snprintf(buf, sizeof(buf), "%d", val);
	return buf;
}


static char * oma_dm_get_target_locuri(struct hs20_osu_client *ctx,
				       xml_node_t *node)
{
	xml_node_t *locuri;
	char *uri, *ret = NULL;

	locuri = get_node(ctx->xml, node, "Item/Target/LocURI");
	if (locuri == NULL)
		return NULL;

	uri = xml_node_get_text(ctx->xml, locuri);
	if (uri)
		ret = os_strdup(uri);
	xml_node_get_text_free(ctx->xml, uri);
	return ret;
}


static void oma_dm_add_locuri(struct hs20_osu_client *ctx, xml_node_t *parent,
			      const char *element, const char *uri)
{
	xml_node_t *node;

	node = xml_node_create(ctx->xml, parent, NULL, element);
	if (node == NULL)
		return;
	xml_node_create_text(ctx->xml, node, NULL, "LocURI", uri);
}


static xml_node_t * oma_dm_build_hdr(struct hs20_osu_client *ctx,
				     const char *url, int msgid)
{
	xml_node_t *syncml, *synchdr;
	xml_namespace_t *ns;

	if (!ctx->devid) {
		wpa_printf(MSG_ERROR,
			   "DevId from devinfo.xml is not available - cannot use OMA DM");
		return NULL;
	}

	syncml = xml_node_create_root(ctx->xml, "SYNCML:SYNCML1.2", NULL, &ns,
				      "SyncML");

	synchdr = xml_node_create(ctx->xml, syncml, NULL, "SyncHdr");
	xml_node_create_text(ctx->xml, synchdr, NULL, "VerDTD", "1.2");
	xml_node_create_text(ctx->xml, synchdr, NULL, "VerProto", "DM/1.2");
	xml_node_create_text(ctx->xml, synchdr, NULL, "SessionID", "1");
	xml_node_create_text(ctx->xml, synchdr, NULL, "MsgID", int2str(msgid));

	oma_dm_add_locuri(ctx, synchdr, "Target", url);
	oma_dm_add_locuri(ctx, synchdr, "Source", ctx->devid);

	return syncml;
}


static void oma_dm_add_cmdid(struct hs20_osu_client *ctx, xml_node_t *parent,
			     int cmdid)
{
	xml_node_create_text(ctx->xml, parent, NULL, "CmdID", int2str(cmdid));
}


static xml_node_t * add_alert(struct hs20_osu_client *ctx, xml_node_t *parent,
			      int cmdid, int data)
{
	xml_node_t *node;

	node = xml_node_create(ctx->xml, parent, NULL, "Alert");
	if (node == NULL)
		return NULL;
	oma_dm_add_cmdid(ctx, node, cmdid);
	xml_node_create_text(ctx->xml, node, NULL, "Data", int2str(data));

	return node;
}


static xml_node_t * add_status(struct hs20_osu_client *ctx, xml_node_t *parent,
			       int msgref, int cmdref, int cmdid,
			       const char *cmd, int data, const char *targetref)
{
	xml_node_t *node;

	node = xml_node_create(ctx->xml, parent, NULL, "Status");
	if (node == NULL)
		return NULL;
	oma_dm_add_cmdid(ctx, node, cmdid);
	xml_node_create_text(ctx->xml, node, NULL, "MsgRef", int2str(msgref));
	if (cmdref)
		xml_node_create_text(ctx->xml, node, NULL, "CmdRef",
				     int2str(cmdref));
	xml_node_create_text(ctx->xml, node, NULL, "Cmd", cmd);
	xml_node_create_text(ctx->xml, node, NULL, "Data", int2str(data));
	if (targetref) {
		xml_node_create_text(ctx->xml, node, NULL, "TargetRef",
				     targetref);
	}

	return node;
}


static xml_node_t * add_results(struct hs20_osu_client *ctx, xml_node_t *parent,
				int msgref, int cmdref, int cmdid,
				const char *locuri, const char *data)
{
	xml_node_t *node;

	node = xml_node_create(ctx->xml, parent, NULL, "Results");
	if (node == NULL)
		return NULL;

	oma_dm_add_cmdid(ctx, node, cmdid);
	xml_node_create_text(ctx->xml, node, NULL, "MsgRef", int2str(msgref));
	xml_node_create_text(ctx->xml, node, NULL, "CmdRef", int2str(cmdref));
	add_item(ctx, node, locuri, data);

	return node;
}


static char * mo_str(struct hs20_osu_client *ctx, const char *urn,
		     const char *fname)
{
	xml_node_t *fnode, *tnds;
	char *str;

	fnode = node_from_file(ctx->xml, fname);
	if (!fnode)
		return NULL;
	tnds = mo_to_tnds(ctx->xml, fnode, 0, urn, "syncml:dmddf1.2");
	xml_node_free(ctx->xml, fnode);
	if (!tnds)
		return NULL;

	str = xml_node_to_str(ctx->xml, tnds);
	xml_node_free(ctx->xml, tnds);
	if (str == NULL)
		return NULL;
	wpa_printf(MSG_INFO, "MgmtTree: %s", str);

	return str;
}


static void add_item(struct hs20_osu_client *ctx, xml_node_t *parent,
		     const char *locuri, const char *data)
{
	xml_node_t *item, *node;

	item = xml_node_create(ctx->xml, parent, NULL, "Item");
	oma_dm_add_locuri(ctx, item, "Source", locuri);
	node = xml_node_create(ctx->xml, item, NULL, "Meta");
	xml_node_create_text_ns(ctx->xml, node, "syncml:metinf", "Format",
				"Chr");
	xml_node_create_text_ns(ctx->xml, node, "syncml:metinf", "Type",
				"text/plain");
	xml_node_create_text(ctx->xml, item, NULL, "Data", data);
}


static void add_replace_devinfo(struct hs20_osu_client *ctx, xml_node_t *parent,
				int cmdid)
{
	xml_node_t *info, *child, *replace;
	const char *name;
	char locuri[200], *txt;

	info = node_from_file(ctx->xml, "devinfo.xml");
	if (info == NULL) {
		wpa_printf(MSG_INFO, "Could not read devinfo.xml");
		return;
	}

	replace = xml_node_create(ctx->xml, parent, NULL, "Replace");
	if (replace == NULL) {
		xml_node_free(ctx->xml, info);
		return;
	}
	oma_dm_add_cmdid(ctx, replace, cmdid);

	xml_node_for_each_child(ctx->xml, child, info) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		os_snprintf(locuri, sizeof(locuri), "./DevInfo/%s", name);
		txt = xml_node_get_text(ctx->xml, child);
		if (txt) {
			add_item(ctx, replace, locuri, txt);
			xml_node_get_text_free(ctx->xml, txt);
		}
	}

	xml_node_free(ctx->xml, info);
}


static void oma_dm_add_hs20_generic_alert(struct hs20_osu_client *ctx,
					  xml_node_t *syncbody,
					  int cmdid, const char *oper,
					  const char *data)
{
	xml_node_t *node, *item;
	char buf[200];

	node = add_alert(ctx, syncbody, cmdid, DM_GENERIC_ALERT);

	item = xml_node_create(ctx->xml, node, NULL, "Item");
	oma_dm_add_locuri(ctx, item, "Source", DM_URI_PPS);
	node = xml_node_create(ctx->xml, item, NULL, "Meta");
	snprintf(buf, sizeof(buf), "Reversed-Domain-Name: %s", oper);
	xml_node_create_text_ns(ctx->xml, node, "syncml:metinf", "Type", buf);
	xml_node_create_text_ns(ctx->xml, node, "syncml:metinf", "Format",
				"xml");
	xml_node_create_text(ctx->xml, item, NULL, "Data", data);
}


static xml_node_t * build_oma_dm_1(struct hs20_osu_client *ctx,
				   const char *url, int msgid, const char *oper)
{
	xml_node_t *syncml, *syncbody;
	char *str;
	int cmdid = 0;

	syncml = oma_dm_build_hdr(ctx, url, msgid);
	if (syncml == NULL)
		return NULL;

	syncbody = xml_node_create(ctx->xml, syncml, NULL, "SyncBody");
	if (syncbody == NULL) {
		xml_node_free(ctx->xml, syncml);
		return NULL;
	}

	cmdid++;
	add_alert(ctx, syncbody, cmdid, DM_CLIENT_INITIATED_MGMT);

	str = mo_str(ctx, NULL, "devdetail.xml");
	if (str == NULL) {
		xml_node_free(ctx->xml, syncml);
		return NULL;
	}
	cmdid++;
	oma_dm_add_hs20_generic_alert(ctx, syncbody, cmdid, oper, str);
	os_free(str);

	cmdid++;
	add_replace_devinfo(ctx, syncbody, cmdid);

	xml_node_create(ctx->xml, syncbody, NULL, "Final");

	return syncml;
}


static xml_node_t * build_oma_dm_1_sub_reg(struct hs20_osu_client *ctx,
					   const char *url, int msgid)
{
	xml_node_t *syncml;

	syncml = build_oma_dm_1(ctx, url, msgid, DM_HS20_SUBSCRIPTION_CREATION);
	if (syncml)
		debug_dump_node(ctx, "OMA-DM Package 1 (sub reg)", syncml);

	return syncml;
}


static xml_node_t * build_oma_dm_1_sub_prov(struct hs20_osu_client *ctx,
					    const char *url, int msgid)
{
	xml_node_t *syncml;

	syncml = build_oma_dm_1(ctx, url, msgid,
				DM_HS20_SUBSCRIPTION_PROVISIONING);
	if (syncml)
		debug_dump_node(ctx, "OMA-DM Package 1 (sub prov)", syncml);

	return syncml;
}


static xml_node_t * build_oma_dm_1_pol_upd(struct hs20_osu_client *ctx,
					   const char *url, int msgid)
{
	xml_node_t *syncml;

	syncml = build_oma_dm_1(ctx, url, msgid, DM_HS20_POLICY_UPDATE);
	if (syncml)
		debug_dump_node(ctx, "OMA-DM Package 1 (pol upd)", syncml);

	return syncml;
}


static xml_node_t * build_oma_dm_1_sub_rem(struct hs20_osu_client *ctx,
					   const char *url, int msgid)
{
	xml_node_t *syncml;

	syncml = build_oma_dm_1(ctx, url, msgid,
				DM_HS20_SUBSCRIPTION_REMEDIATION);
	if (syncml)
		debug_dump_node(ctx, "OMA-DM Package 1 (sub rem)", syncml);

	return syncml;
}


static int oma_dm_exec_browser(struct hs20_osu_client *ctx, xml_node_t *exec)
{
	xml_node_t *node;
	char *data;
	int res;

	node = get_node(ctx->xml, exec, "Item/Data");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No Data node found");
		return DM_RESP_BAD_REQUEST;
	}

	data = xml_node_get_text(ctx->xml, node);
	if (data == NULL) {
		wpa_printf(MSG_INFO, "Invalid data");
		return DM_RESP_BAD_REQUEST;
	}
	wpa_printf(MSG_INFO, "Data: %s", data);
	wpa_printf(MSG_INFO, "Launch browser to URI '%s'", data);
	write_summary(ctx, "Launch browser to URI '%s'", data);
	res = hs20_web_browser(data);
	xml_node_get_text_free(ctx->xml, data);
	if (res > 0) {
		wpa_printf(MSG_INFO, "User response in browser completed successfully");
		write_summary(ctx, "User response in browser completed successfully");
		return DM_RESP_OK;
	} else {
		wpa_printf(MSG_INFO, "Failed to receive user response");
		write_summary(ctx, "Failed to receive user response");
		return DM_RESP_COMMAND_FAILED;
	}
}


static int oma_dm_exec_get_cert(struct hs20_osu_client *ctx, xml_node_t *exec)
{
	xml_node_t *node, *getcert;
	char *data;
	const char *name;
	int res;

	wpa_printf(MSG_INFO, "Client certificate enrollment");
	write_summary(ctx, "Client certificate enrollment");

	node = get_node(ctx->xml, exec, "Item/Data");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No Data node found");
		return DM_RESP_BAD_REQUEST;
	}

	data = xml_node_get_text(ctx->xml, node);
	if (data == NULL) {
		wpa_printf(MSG_INFO, "Invalid data");
		return DM_RESP_BAD_REQUEST;
	}
	wpa_printf(MSG_INFO, "Data: %s", data);
	getcert = xml_node_from_buf(ctx->xml, data);
	xml_node_get_text_free(ctx->xml, data);

	if (getcert == NULL) {
		wpa_printf(MSG_INFO, "Could not parse Item/Data node contents");
		return DM_RESP_BAD_REQUEST;
	}

	debug_dump_node(ctx, "OMA-DM getCertificate", getcert);

	name = xml_node_get_localname(ctx->xml, getcert);
	if (name == NULL || os_strcasecmp(name, "getCertificate") != 0) {
		wpa_printf(MSG_INFO, "Unexpected getCertificate node name '%s'",
			   name);
		return DM_RESP_BAD_REQUEST;
	}

	res = osu_get_certificate(ctx, getcert);

	xml_node_free(ctx->xml, getcert);

	return res == 0 ? DM_RESP_OK : DM_RESP_COMMAND_FAILED;
}


static int oma_dm_exec(struct hs20_osu_client *ctx, xml_node_t *exec)
{
	char *locuri;
	int ret;

	locuri = oma_dm_get_target_locuri(ctx, exec);
	if (locuri == NULL) {
		wpa_printf(MSG_INFO, "No Target LocURI node found");
		return DM_RESP_BAD_REQUEST;
	}

	wpa_printf(MSG_INFO, "Target LocURI: %s", locuri);

	if (os_strcasecmp(locuri, "./DevDetail/Ext/org.wi-fi/Wi-Fi/Ops/"
			  "launchBrowserToURI") == 0) {
		ret = oma_dm_exec_browser(ctx, exec);
	} else if (os_strcasecmp(locuri, "./DevDetail/Ext/org.wi-fi/Wi-Fi/Ops/"
			  "getCertificate") == 0) {
		ret = oma_dm_exec_get_cert(ctx, exec);
	} else {
		wpa_printf(MSG_INFO, "Unsupported exec Target LocURI");
		ret = DM_RESP_NOT_FOUND;
	}
	os_free(locuri);

	return ret;
}


static int oma_dm_run_add(struct hs20_osu_client *ctx, const char *locuri,
			  xml_node_t *add, xml_node_t *pps,
			  const char *pps_fname)
{
	const char *pos;
	size_t fqdn_len;
	xml_node_t *node, *tnds, *unode, *pps_node;
	char *data, *uri, *upos, *end;
	int use_tnds = 0;
	size_t uri_len;

	wpa_printf(MSG_INFO, "Add command target LocURI: %s", locuri);

	if (os_strncasecmp(locuri, "./Wi-Fi/", 8) != 0) {
		wpa_printf(MSG_INFO, "Do not allow Add outside ./Wi-Fi");
		return DM_RESP_PERMISSION_DENIED;
	}
	pos = locuri + 8;

	if (ctx->fqdn == NULL)
		return DM_RESP_COMMAND_FAILED;
	fqdn_len = os_strlen(ctx->fqdn);
	if (os_strncasecmp(pos, ctx->fqdn, fqdn_len) != 0 ||
	    pos[fqdn_len] != '/') {
		wpa_printf(MSG_INFO, "Do not allow Add outside ./Wi-Fi/%s",
			   ctx->fqdn);
		return DM_RESP_PERMISSION_DENIED;
	}
	pos += fqdn_len + 1;

	if (os_strncasecmp(pos, "PerProviderSubscription/", 24) != 0) {
		wpa_printf(MSG_INFO,
			   "Do not allow Add outside ./Wi-Fi/%s/PerProviderSubscription",
			   ctx->fqdn);
		return DM_RESP_PERMISSION_DENIED;
	}
	pos += 24;

	wpa_printf(MSG_INFO, "Add command for PPS node %s", pos);

	pps_node = get_node(ctx->xml, pps, pos);
	if (pps_node) {
		wpa_printf(MSG_INFO, "Specified PPS node exists already");
		return DM_RESP_ALREADY_EXISTS;
	}

	uri = os_strdup(pos);
	if (uri == NULL)
		return DM_RESP_COMMAND_FAILED;
	while (!pps_node) {
		upos = os_strrchr(uri, '/');
		if (!upos)
			break;
		upos[0] = '\0';
		pps_node = get_node(ctx->xml, pps, uri);
		wpa_printf(MSG_INFO, "Node %s %s", uri,
			   pps_node ? "exists" : "does not exist");
	}

	wpa_printf(MSG_INFO, "Parent URI: %s", uri);

	if (!pps_node) {
		/* Add at root of PPS MO */
		pps_node = pps;
	}

	uri_len = os_strlen(uri);
	os_strlcpy(uri, pos + uri_len, os_strlen(pos));
	upos = uri;
	while (*upos == '/')
		upos++;
	wpa_printf(MSG_INFO, "Nodes to add: %s", upos);

	for (;;) {
		end = os_strchr(upos, '/');
		if (!end)
			break;
		*end = '\0';
		wpa_printf(MSG_INFO, "Adding interim node %s", upos);
		pps_node = xml_node_create(ctx->xml, pps_node, NULL, upos);
		if (pps_node == NULL) {
			os_free(uri);
			return DM_RESP_COMMAND_FAILED;
		}
		upos = end + 1;
	}

	wpa_printf(MSG_INFO, "Adding node %s", upos);

	node = get_node(ctx->xml, add, "Item/Meta/Type");
	if (node) {
		char *type;
		type = xml_node_get_text(ctx->xml, node);
		if (type == NULL) {
			wpa_printf(MSG_ERROR, "Could not find type text");
			os_free(uri);
			return DM_RESP_BAD_REQUEST;
		}
		use_tnds = node &&
			os_strstr(type, "application/vnd.syncml.dmtnds+xml");
	}

	node = get_node(ctx->xml, add, "Item/Data");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No Add/Item/Data found");
		os_free(uri);
		return DM_RESP_BAD_REQUEST;
	}

	data = xml_node_get_text(ctx->xml, node);
	if (data == NULL) {
		wpa_printf(MSG_INFO, "Could not get Add/Item/Data text");
		os_free(uri);
		return DM_RESP_BAD_REQUEST;
	}

	wpa_printf(MSG_DEBUG, "Add/Item/Data: %s", data);

	if (use_tnds) {
		tnds = xml_node_from_buf(ctx->xml, data);
		xml_node_get_text_free(ctx->xml, data);
		if (tnds == NULL) {
			wpa_printf(MSG_INFO,
				   "Could not parse Add/Item/Data text");
			os_free(uri);
			return DM_RESP_BAD_REQUEST;
		}

		unode = tnds_to_mo(ctx->xml, tnds);
		xml_node_free(ctx->xml, tnds);
		if (unode == NULL) {
			wpa_printf(MSG_INFO, "Could not parse TNDS text");
			os_free(uri);
			return DM_RESP_BAD_REQUEST;
		}

		debug_dump_node(ctx, "Parsed TNDS", unode);

		xml_node_add_child(ctx->xml, pps_node, unode);
	} else {
		/* TODO: What to do here? */
		os_free(uri);
		return DM_RESP_BAD_REQUEST;
	}

	os_free(uri);

	if (update_pps_file(ctx, pps_fname, pps) < 0)
		return DM_RESP_COMMAND_FAILED;

	ctx->pps_updated = 1;

	return DM_RESP_OK;
}


static int oma_dm_add(struct hs20_osu_client *ctx, xml_node_t *add,
		      xml_node_t *pps, const char *pps_fname)
{
	xml_node_t *node;
	char *locuri;
	char fname[300];
	int ret;

	node = get_node(ctx->xml, add, "Item/Target/LocURI");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No Target LocURI node found");
		return DM_RESP_BAD_REQUEST;
	}
	locuri = xml_node_get_text(ctx->xml, node);
	if (locuri == NULL) {
		wpa_printf(MSG_ERROR, "No LocURI node text found");
		return DM_RESP_BAD_REQUEST;
	}
	wpa_printf(MSG_INFO, "Target LocURI: %s", locuri);
	if (os_strncasecmp(locuri, "./Wi-Fi/", 8) != 0) {
		wpa_printf(MSG_INFO, "Unsupported Add Target LocURI");
		xml_node_get_text_free(ctx->xml, locuri);
		return DM_RESP_PERMISSION_DENIED;
	}

	node = get_node(ctx->xml, add, "Item/Data");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No Data node found");
		xml_node_get_text_free(ctx->xml, locuri);
		return DM_RESP_BAD_REQUEST;
	}

	if (pps_fname && os_file_exists(pps_fname)) {
		ret = oma_dm_run_add(ctx, locuri, add, pps, pps_fname);
		if (ret != DM_RESP_OK) {
			xml_node_get_text_free(ctx->xml, locuri);
			return ret;
		}
		ret = 0;
		os_strlcpy(fname, pps_fname, sizeof(fname));
	} else
		ret = hs20_add_pps_mo(ctx, locuri, node, fname, sizeof(fname));
	xml_node_get_text_free(ctx->xml, locuri);
	if (ret < 0)
		return ret == -2 ? DM_RESP_ALREADY_EXISTS :
			DM_RESP_COMMAND_FAILED;

	if (ctx->no_reconnect == 2) {
		os_snprintf(ctx->pps_fname, sizeof(ctx->pps_fname), "%s",
			    fname);
		ctx->pps_cred_set = 1;
		return DM_RESP_OK;
	}

	wpa_printf(MSG_INFO, "Updating wpa_supplicant credentials");
	cmd_set_pps(ctx, fname);

	if (ctx->no_reconnect)
		return DM_RESP_OK;

	wpa_printf(MSG_INFO, "Requesting reconnection with updated configuration");
	if (wpa_command(ctx->ifname, "INTERWORKING_SELECT auto") < 0)
		wpa_printf(MSG_INFO, "Failed to request wpa_supplicant to reconnect");

	return DM_RESP_OK;
}


static int oma_dm_replace(struct hs20_osu_client *ctx, xml_node_t *replace,
			  xml_node_t *pps, const char *pps_fname)
{
	char *locuri, *pos;
	size_t fqdn_len;
	xml_node_t *node, *tnds, *unode, *pps_node, *parent;
	char *data;
	int use_tnds = 0;

	locuri = oma_dm_get_target_locuri(ctx, replace);
	if (locuri == NULL)
		return DM_RESP_BAD_REQUEST;

	wpa_printf(MSG_INFO, "Replace command target LocURI: %s", locuri);
	if (os_strncasecmp(locuri, "./Wi-Fi/", 8) != 0) {
		wpa_printf(MSG_INFO, "Do not allow Replace outside ./Wi-Fi");
		os_free(locuri);
		return DM_RESP_PERMISSION_DENIED;
	}
	pos = locuri + 8;

	if (ctx->fqdn == NULL) {
		os_free(locuri);
		return DM_RESP_COMMAND_FAILED;
	}
	fqdn_len = os_strlen(ctx->fqdn);
	if (os_strncasecmp(pos, ctx->fqdn, fqdn_len) != 0 ||
	    pos[fqdn_len] != '/') {
		wpa_printf(MSG_INFO, "Do not allow Replace outside ./Wi-Fi/%s",
			   ctx->fqdn);
		os_free(locuri);
		return DM_RESP_PERMISSION_DENIED;
	}
	pos += fqdn_len + 1;

	if (os_strncasecmp(pos, "PerProviderSubscription/", 24) != 0) {
		wpa_printf(MSG_INFO,
			   "Do not allow Replace outside ./Wi-Fi/%s/PerProviderSubscription",
			   ctx->fqdn);
		os_free(locuri);
		return DM_RESP_PERMISSION_DENIED;
	}
	pos += 24;

	wpa_printf(MSG_INFO, "Replace command for PPS node %s", pos);

	pps_node = get_node(ctx->xml, pps, pos);
	if (pps_node == NULL) {
		wpa_printf(MSG_INFO, "Specified PPS node not found");
		os_free(locuri);
		return DM_RESP_NOT_FOUND;
	}

	node = get_node(ctx->xml, replace, "Item/Meta/Type");
	if (node) {
		char *type;
		type = xml_node_get_text(ctx->xml, node);
		if (type == NULL) {
			wpa_printf(MSG_INFO, "Could not find type text");
			os_free(locuri);
			return DM_RESP_BAD_REQUEST;
		}
		use_tnds = node &&
			os_strstr(type, "application/vnd.syncml.dmtnds+xml");
	}

	node = get_node(ctx->xml, replace, "Item/Data");
	if (node == NULL) {
		wpa_printf(MSG_INFO, "No Replace/Item/Data found");
		os_free(locuri);
		return DM_RESP_BAD_REQUEST;
	}

	data = xml_node_get_text(ctx->xml, node);
	if (data == NULL) {
		wpa_printf(MSG_INFO, "Could not get Replace/Item/Data text");
		os_free(locuri);
		return DM_RESP_BAD_REQUEST;
	}

	wpa_printf(MSG_DEBUG, "Replace/Item/Data: %s", data);

	if (use_tnds) {
		tnds = xml_node_from_buf(ctx->xml, data);
		xml_node_get_text_free(ctx->xml, data);
		if (tnds == NULL) {
			wpa_printf(MSG_INFO,
				   "Could not parse Replace/Item/Data text");
			os_free(locuri);
			return DM_RESP_BAD_REQUEST;
		}

		unode = tnds_to_mo(ctx->xml, tnds);
		xml_node_free(ctx->xml, tnds);
		if (unode == NULL) {
			wpa_printf(MSG_INFO, "Could not parse TNDS text");
			os_free(locuri);
			return DM_RESP_BAD_REQUEST;
		}

		debug_dump_node(ctx, "Parsed TNDS", unode);

		parent = xml_node_get_parent(ctx->xml, pps_node);
		xml_node_detach(ctx->xml, pps_node);
		xml_node_add_child(ctx->xml, parent, unode);
	} else {
		xml_node_set_text(ctx->xml, pps_node, data);
		xml_node_get_text_free(ctx->xml, data);
	}

	os_free(locuri);

	if (update_pps_file(ctx, pps_fname, pps) < 0)
		return DM_RESP_COMMAND_FAILED;

	ctx->pps_updated = 1;

	return DM_RESP_OK;
}


static int oma_dm_get(struct hs20_osu_client *ctx, xml_node_t *get,
		      xml_node_t *pps, const char *pps_fname, char **value)
{
	char *locuri, *pos;
	size_t fqdn_len;
	xml_node_t *pps_node;
	const char *name;

	*value = NULL;

	locuri = oma_dm_get_target_locuri(ctx, get);
	if (locuri == NULL)
		return DM_RESP_BAD_REQUEST;

	wpa_printf(MSG_INFO, "Get command target LocURI: %s", locuri);
	if (os_strncasecmp(locuri, "./Wi-Fi/", 8) != 0) {
		wpa_printf(MSG_INFO, "Do not allow Get outside ./Wi-Fi");
		os_free(locuri);
		return DM_RESP_PERMISSION_DENIED;
	}
	pos = locuri + 8;

	if (ctx->fqdn == NULL)
		return DM_RESP_COMMAND_FAILED;
	fqdn_len = os_strlen(ctx->fqdn);
	if (os_strncasecmp(pos, ctx->fqdn, fqdn_len) != 0 ||
	    pos[fqdn_len] != '/') {
		wpa_printf(MSG_INFO, "Do not allow Get outside ./Wi-Fi/%s",
			   ctx->fqdn);
		os_free(locuri);
		return DM_RESP_PERMISSION_DENIED;
	}
	pos += fqdn_len + 1;

	if (os_strncasecmp(pos, "PerProviderSubscription/", 24) != 0) {
		wpa_printf(MSG_INFO,
			   "Do not allow Get outside ./Wi-Fi/%s/PerProviderSubscription",
			   ctx->fqdn);
		os_free(locuri);
		return DM_RESP_PERMISSION_DENIED;
	}
	pos += 24;

	wpa_printf(MSG_INFO, "Get command for PPS node %s", pos);

	pps_node = get_node(ctx->xml, pps, pos);
	if (pps_node == NULL) {
		wpa_printf(MSG_INFO, "Specified PPS node not found");
		os_free(locuri);
		return DM_RESP_NOT_FOUND;
	}

	name = xml_node_get_localname(ctx->xml, pps_node);
	wpa_printf(MSG_INFO, "Get command returned node with name '%s'", name);
	if (os_strcasecmp(name, "Password") == 0) {
		wpa_printf(MSG_INFO, "Do not allow Get for Password node");
		os_free(locuri);
		return DM_RESP_PERMISSION_DENIED;
	}

	/*
	 * TODO: No support for DMTNDS, so if interior node, reply with a
	 * list of children node names in Results element. The child list type is
	 * defined in [DMTND].
	 */

	*value = xml_node_get_text(ctx->xml, pps_node);
	if (*value == NULL)
		return DM_RESP_COMMAND_FAILED;

	return DM_RESP_OK;
}


static int oma_dm_get_cmdid(struct hs20_osu_client *ctx, xml_node_t *node)
{
	xml_node_t *cnode;
	char *str;
	int ret;

	cnode = get_node(ctx->xml, node, "CmdID");
	if (cnode == NULL)
		return 0;

	str = xml_node_get_text(ctx->xml, cnode);
	if (str == NULL)
		return 0;
	ret = atoi(str);
	xml_node_get_text_free(ctx->xml, str);
	return ret;
}


static xml_node_t * oma_dm_send_recv(struct hs20_osu_client *ctx,
				     const char *url, xml_node_t *syncml,
				     const char *ext_hdr,
				     const char *username, const char *password,
				     const char *client_cert,
				     const char *client_key)
{
	xml_node_t *resp;
	char *str, *res;
	char *resp_uri = NULL;

	str = xml_node_to_str(ctx->xml, syncml);
	xml_node_free(ctx->xml, syncml);
	if (str == NULL)
		return NULL;

	wpa_printf(MSG_INFO, "Send OMA DM Package");
	write_summary(ctx, "Send OMA DM Package");
	os_free(ctx->server_url);
	ctx->server_url = os_strdup(url);
	res = http_post(ctx->http, url, str, "application/vnd.syncml.dm+xml",
			ext_hdr, ctx->ca_fname, username, password,
			client_cert, client_key, NULL);
	os_free(str);
	os_free(resp_uri);
	resp_uri = NULL;

	if (res == NULL) {
		const char *err = http_get_err(ctx->http);
		if (err) {
			wpa_printf(MSG_INFO, "HTTP error: %s", err);
			write_result(ctx, "HTTP error: %s", err);
		} else {
			write_summary(ctx, "Failed to send OMA DM Package");
		}
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "Server response: %s", res);

	wpa_printf(MSG_INFO, "Process OMA DM Package");
	write_summary(ctx, "Process received OMA DM Package");
	resp = xml_node_from_buf(ctx->xml, res);
	os_free(res);
	if (resp == NULL) {
		wpa_printf(MSG_INFO, "Failed to parse OMA DM response");
		return NULL;
	}

	debug_dump_node(ctx, "OMA DM Package", resp);

	return resp;
}


static xml_node_t * oma_dm_process(struct hs20_osu_client *ctx, const char *url,
				   xml_node_t *resp, int msgid,
				   char **ret_resp_uri,
				   xml_node_t *pps, const char *pps_fname)
{
	xml_node_t *syncml, *syncbody, *hdr, *body, *child;
	const char *name;
	char *resp_uri = NULL;
	int server_msgid = 0;
	int cmdid = 0;
	int server_cmdid;
	int resp_needed = 0;
	char *tmp;
	int final = 0;
	char *locuri;

	*ret_resp_uri = NULL;

	name = xml_node_get_localname(ctx->xml, resp);
	if (name == NULL || os_strcasecmp(name, "SyncML") != 0) {
		wpa_printf(MSG_INFO, "SyncML node not found");
		return NULL;
	}

	hdr = get_node(ctx->xml, resp, "SyncHdr");
	body = get_node(ctx->xml, resp, "SyncBody");
	if (hdr == NULL || body == NULL) {
		wpa_printf(MSG_INFO, "Could not find SyncHdr or SyncBody");
		return NULL;
	}

	xml_node_for_each_child(ctx->xml, child, hdr) {
		xml_node_for_each_check(ctx->xml, child);
		name = xml_node_get_localname(ctx->xml, child);
		wpa_printf(MSG_INFO, "SyncHdr %s", name);
		if (os_strcasecmp(name, "RespURI") == 0) {
			tmp = xml_node_get_text(ctx->xml, child);
			if (tmp)
				resp_uri = os_strdup(tmp);
			xml_node_get_text_free(ctx->xml, tmp);
		} else if (os_strcasecmp(name, "MsgID") == 0) {
			tmp = xml_node_get_text(ctx->xml, child);
			if (tmp)
				server_msgid = atoi(tmp);
			xml_node_get_text_free(ctx->xml, tmp);
		}
	}

	wpa_printf(MSG_INFO, "Server MsgID: %d", server_msgid);
	if (resp_uri)
		wpa_printf(MSG_INFO, "RespURI: %s", resp_uri);

	syncml = oma_dm_build_hdr(ctx, resp_uri ? resp_uri : url, msgid);
	if (syncml == NULL) {
		os_free(resp_uri);
		return NULL;
	}

	syncbody = xml_node_create(ctx->xml, syncml, NULL, "SyncBody");
	cmdid++;
	add_status(ctx, syncbody, server_msgid, 0, cmdid, "SyncHdr",
		   DM_RESP_AUTH_ACCEPTED, NULL);

	xml_node_for_each_child(ctx->xml, child, body) {
		xml_node_for_each_check(ctx->xml, child);
		server_cmdid = oma_dm_get_cmdid(ctx, child);
		name = xml_node_get_localname(ctx->xml, child);
		wpa_printf(MSG_INFO, "SyncBody CmdID=%d - %s",
			   server_cmdid, name);
		if (os_strcasecmp(name, "Exec") == 0) {
			int res = oma_dm_exec(ctx, child);
			cmdid++;
			locuri = oma_dm_get_target_locuri(ctx, child);
			if (locuri == NULL)
				res = DM_RESP_BAD_REQUEST;
			add_status(ctx, syncbody, server_msgid, server_cmdid,
				   cmdid, name, res, locuri);
			os_free(locuri);
			resp_needed = 1;
		} else if (os_strcasecmp(name, "Add") == 0) {
			int res = oma_dm_add(ctx, child, pps, pps_fname);
			cmdid++;
			locuri = oma_dm_get_target_locuri(ctx, child);
			if (locuri == NULL)
				res = DM_RESP_BAD_REQUEST;
			add_status(ctx, syncbody, server_msgid, server_cmdid,
				   cmdid, name, res, locuri);
			os_free(locuri);
			resp_needed = 1;
		} else if (os_strcasecmp(name, "Replace") == 0) {
			int res;
			res = oma_dm_replace(ctx, child, pps, pps_fname);
			cmdid++;
			locuri = oma_dm_get_target_locuri(ctx, child);
			if (locuri == NULL)
				res = DM_RESP_BAD_REQUEST;
			add_status(ctx, syncbody, server_msgid, server_cmdid,
				   cmdid, name, res, locuri);
			os_free(locuri);
			resp_needed = 1;
		} else if (os_strcasecmp(name, "Status") == 0) {
			/* TODO: Verify success */
		} else if (os_strcasecmp(name, "Get") == 0) {
			int res;
			char *value;
			res = oma_dm_get(ctx, child, pps, pps_fname, &value);
			cmdid++;
			locuri = oma_dm_get_target_locuri(ctx, child);
			if (locuri == NULL)
				res = DM_RESP_BAD_REQUEST;
			add_status(ctx, syncbody, server_msgid, server_cmdid,
				   cmdid, name, res, locuri);
			if (res == DM_RESP_OK && value) {
				cmdid++;
				add_results(ctx, syncbody, server_msgid,
					    server_cmdid, cmdid, locuri, value);
			}
			os_free(locuri);
			xml_node_get_text_free(ctx->xml, value);
			resp_needed = 1;
#if 0 /* TODO: MUST support */
		} else if (os_strcasecmp(name, "Delete") == 0) {
#endif
#if 0 /* TODO: MUST support */
		} else if (os_strcasecmp(name, "Sequence") == 0) {
#endif
		} else if (os_strcasecmp(name, "Final") == 0) {
			final = 1;
			break;
		} else {
			locuri = oma_dm_get_target_locuri(ctx, child);
			add_status(ctx, syncbody, server_msgid, server_cmdid,
				   cmdid, name, DM_RESP_COMMAND_NOT_IMPLEMENTED,
				   locuri);
			os_free(locuri);
			resp_needed = 1;
		}
	}

	if (!final) {
		wpa_printf(MSG_INFO, "Final node not found");
		xml_node_free(ctx->xml, syncml);
		os_free(resp_uri);
		return NULL;
	}

	if (!resp_needed) {
		wpa_printf(MSG_INFO, "Exchange completed - no response needed");
		xml_node_free(ctx->xml, syncml);
		os_free(resp_uri);
		return NULL;
	}

	xml_node_create(ctx->xml, syncbody, NULL, "Final");

	debug_dump_node(ctx, "OMA-DM Package 3", syncml);

	*ret_resp_uri = resp_uri;
	return syncml;
}


int cmd_oma_dm_prov(struct hs20_osu_client *ctx, const char *url)
{
	xml_node_t *syncml, *resp;
	char *resp_uri = NULL;
	int msgid = 0;

	if (url == NULL) {
		wpa_printf(MSG_INFO, "Invalid prov command (missing URL)");
		return -1;
	}

	wpa_printf(MSG_INFO, "OMA-DM credential provisioning requested");
	write_summary(ctx, "OMA-DM credential provisioning");

	msgid++;
	syncml = build_oma_dm_1_sub_reg(ctx, url, msgid);
	if (syncml == NULL)
		return -1;

	while (syncml) {
		resp = oma_dm_send_recv(ctx, resp_uri ? resp_uri : url,
					syncml, NULL, NULL, NULL, NULL, NULL);
		if (resp == NULL)
			return -1;

		msgid++;
		syncml = oma_dm_process(ctx, url, resp, msgid, &resp_uri,
					NULL, NULL);
		xml_node_free(ctx->xml, resp);
	}

	os_free(resp_uri);

	return ctx->pps_cred_set ? 0 : -1;
}


int cmd_oma_dm_sim_prov(struct hs20_osu_client *ctx, const char *url)
{
	xml_node_t *syncml, *resp;
	char *resp_uri = NULL;
	int msgid = 0;

	if (url == NULL) {
		wpa_printf(MSG_INFO, "Invalid prov command (missing URL)");
		return -1;
	}

	wpa_printf(MSG_INFO, "OMA-DM SIM provisioning requested");
	ctx->no_reconnect = 2;

	wpa_printf(MSG_INFO, "Wait for IP address before starting SIM provisioning");
	write_summary(ctx, "Wait for IP address before starting SIM provisioning");

	if (wait_ip_addr(ctx->ifname, 15) < 0) {
		wpa_printf(MSG_INFO, "Could not get IP address for WLAN - try connection anyway");
	}
	write_summary(ctx, "OMA-DM SIM provisioning");

	msgid++;
	syncml = build_oma_dm_1_sub_prov(ctx, url, msgid);
	if (syncml == NULL)
		return -1;

	while (syncml) {
		resp = oma_dm_send_recv(ctx, resp_uri ? resp_uri : url,
					syncml, NULL, NULL, NULL, NULL, NULL);
		if (resp == NULL)
			return -1;

		msgid++;
		syncml = oma_dm_process(ctx, url, resp, msgid, &resp_uri,
					NULL, NULL);
		xml_node_free(ctx->xml, resp);
	}

	os_free(resp_uri);

	if (ctx->pps_cred_set) {
		wpa_printf(MSG_INFO, "Updating wpa_supplicant credentials");
		cmd_set_pps(ctx, ctx->pps_fname);

		wpa_printf(MSG_INFO, "Requesting reconnection with updated configuration");
		write_summary(ctx, "Requesting reconnection with updated configuration");
		if (wpa_command(ctx->ifname, "INTERWORKING_SELECT auto") < 0) {
			wpa_printf(MSG_INFO, "Failed to request wpa_supplicant to reconnect");
			write_summary(ctx, "Failed to request wpa_supplicant to reconnect");
			return -1;
		}
	}

	return ctx->pps_cred_set ? 0 : -1;
}


void oma_dm_pol_upd(struct hs20_osu_client *ctx, const char *address,
		    const char *pps_fname,
		    const char *client_cert, const char *client_key,
		    const char *cred_username, const char *cred_password,
		    xml_node_t *pps)
{
	xml_node_t *syncml, *resp;
	char *resp_uri = NULL;
	int msgid = 0;

	wpa_printf(MSG_INFO, "OMA-DM policy update");
	write_summary(ctx, "OMA-DM policy update");

	msgid++;
	syncml = build_oma_dm_1_pol_upd(ctx, address, msgid);
	if (syncml == NULL)
		return;

	while (syncml) {
		resp = oma_dm_send_recv(ctx, resp_uri ? resp_uri : address,
					syncml, NULL, cred_username,
					cred_password, client_cert, client_key);
		if (resp == NULL)
			return;

		msgid++;
		syncml = oma_dm_process(ctx, address, resp, msgid, &resp_uri,
					pps, pps_fname);
		xml_node_free(ctx->xml, resp);
	}

	os_free(resp_uri);

	if (ctx->pps_updated) {
		wpa_printf(MSG_INFO, "Update wpa_supplicant credential based on updated PPS MO");
		write_summary(ctx, "Update wpa_supplicant credential based on updated PPS MO and request connection");
		cmd_set_pps(ctx, pps_fname);
		if (wpa_command(ctx->ifname, "INTERWORKING_SELECT auto") < 0) {
			wpa_printf(MSG_INFO,
				   "Failed to request wpa_supplicant to reconnect");
			write_summary(ctx,
				      "Failed to request wpa_supplicant to reconnect");
		}
	}
}


void oma_dm_sub_rem(struct hs20_osu_client *ctx, const char *address,
		    const char *pps_fname,
		    const char *client_cert, const char *client_key,
		    const char *cred_username, const char *cred_password,
		    xml_node_t *pps)
{
	xml_node_t *syncml, *resp;
	char *resp_uri = NULL;
	int msgid = 0;

	wpa_printf(MSG_INFO, "OMA-DM subscription remediation");
	write_summary(ctx, "OMA-DM subscription remediation");

	msgid++;
	syncml = build_oma_dm_1_sub_rem(ctx, address, msgid);
	if (syncml == NULL)
		return;

	while (syncml) {
		resp = oma_dm_send_recv(ctx, resp_uri ? resp_uri : address,
					syncml, NULL, cred_username,
					cred_password, client_cert, client_key);
		if (resp == NULL)
			return;

		msgid++;
		syncml = oma_dm_process(ctx, address, resp, msgid, &resp_uri,
					pps, pps_fname);
		xml_node_free(ctx->xml, resp);
	}

	os_free(resp_uri);

	wpa_printf(MSG_INFO, "Update wpa_supplicant credential based on updated PPS MO and request reconnection");
	write_summary(ctx, "Update wpa_supplicant credential based on updated PPS MO and request reconnection");
	cmd_set_pps(ctx, pps_fname);
	if (wpa_command(ctx->ifname, "INTERWORKING_SELECT auto") < 0) {
		wpa_printf(MSG_INFO, "Failed to request wpa_supplicant to reconnect");
		write_summary(ctx, "Failed to request wpa_supplicant to reconnect");
	}
}


void cmd_oma_dm_add(struct hs20_osu_client *ctx, const char *pps_fname,
		    const char *add_fname)
{
	xml_node_t *pps, *add;
	int res;

	ctx->fqdn = os_strdup("wi-fi.org");

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "PPS file %s could not be parsed",
			   pps_fname);
		return;
	}

	add = node_from_file(ctx->xml, add_fname);
	if (add == NULL) {
		wpa_printf(MSG_INFO, "Add file %s could not be parsed",
			   add_fname);
		xml_node_free(ctx->xml, pps);
		return;
	}

	res = oma_dm_add(ctx, add, pps, pps_fname);
	wpa_printf(MSG_INFO, "oma_dm_add --> %d", res);

	xml_node_free(ctx->xml, pps);
	xml_node_free(ctx->xml, add);
}


void cmd_oma_dm_replace(struct hs20_osu_client *ctx, const char *pps_fname,
			const char *replace_fname)
{
	xml_node_t *pps, *replace;
	int res;

	ctx->fqdn = os_strdup("wi-fi.org");

	pps = node_from_file(ctx->xml, pps_fname);
	if (pps == NULL) {
		wpa_printf(MSG_INFO, "PPS file %s could not be parsed",
			   pps_fname);
		return;
	}

	replace = node_from_file(ctx->xml, replace_fname);
	if (replace == NULL) {
		wpa_printf(MSG_INFO, "Replace file %s could not be parsed",
			   replace_fname);
		xml_node_free(ctx->xml, pps);
		return;
	}

	res = oma_dm_replace(ctx, replace, pps, pps_fname);
	wpa_printf(MSG_INFO, "oma_dm_replace --> %d", res);

	xml_node_free(ctx->xml, pps);
	xml_node_free(ctx->xml, replace);
}
