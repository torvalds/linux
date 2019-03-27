/*
 * Hotspot 2.0 SPP client
 * Copyright (c) 2012-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <sys/stat.h>

#include "common.h"
#include "browser.h"
#include "wpa_ctrl.h"
#include "wpa_helpers.h"
#include "xml-utils.h"
#include "http-utils.h"
#include "utils/base64.h"
#include "crypto/crypto.h"
#include "crypto/sha256.h"
#include "osu_client.h"


extern const char *spp_xsd_fname;

static int hs20_spp_update_response(struct hs20_osu_client *ctx,
				    const char *session_id,
				    const char *spp_status,
				    const char *error_code);
static void hs20_policy_update_complete(
	struct hs20_osu_client *ctx, const char *pps_fname);


static char * get_spp_attr_value(struct xml_node_ctx *ctx, xml_node_t *node,
				 char *attr_name)
{
	return xml_node_get_attr_value_ns(ctx, node, SPP_NS_URI, attr_name);
}


static int hs20_spp_validate(struct hs20_osu_client *ctx, xml_node_t *node,
			     const char *expected_name)
{
	struct xml_node_ctx *xctx = ctx->xml;
	const char *name;
	char *err;
	int ret;

	if (!xml_node_is_element(xctx, node))
		return -1;

	name = xml_node_get_localname(xctx, node);
	if (name == NULL)
		return -1;

	if (strcmp(expected_name, name) != 0) {
		wpa_printf(MSG_INFO, "Unexpected SOAP method name '%s' (expected '%s')",
			   name, expected_name);
		write_summary(ctx, "Unexpected SOAP method name '%s' (expected '%s')",
			      name, expected_name);
		return -1;
	}

	ret = xml_validate(xctx, node, spp_xsd_fname, &err);
	if (ret < 0) {
		wpa_printf(MSG_INFO, "XML schema validation error(s)\n%s", err);
		write_summary(ctx, "SPP XML schema validation failed");
		os_free(err);
	}
	return ret;
}


static void add_mo_container(struct xml_node_ctx *ctx, xml_namespace_t *ns,
			     xml_node_t *parent, const char *urn,
			     const char *fname)
{
	xml_node_t *node;
	xml_node_t *fnode, *tnds;
	char *str;

	errno = 0;
	fnode = node_from_file(ctx, fname);
	if (!fnode) {
		wpa_printf(MSG_ERROR,
			   "Failed to create XML node from file: %s, possible error: %s",
			   fname, strerror(errno));
		return;
	}
	tnds = mo_to_tnds(ctx, fnode, 0, urn, "syncml:dmddf1.2");
	xml_node_free(ctx, fnode);
	if (!tnds)
		return;

	str = xml_node_to_str(ctx, tnds);
	xml_node_free(ctx, tnds);
	if (str == NULL)
		return;

	node = xml_node_create_text(ctx, parent, ns, "moContainer", str);
	if (node)
		xml_node_add_attr(ctx, node, ns, "moURN", urn);
	os_free(str);
}


static xml_node_t * build_spp_post_dev_data(struct hs20_osu_client *ctx,
					    xml_namespace_t **ret_ns,
					    const char *session_id,
					    const char *reason)
{
	xml_namespace_t *ns;
	xml_node_t *spp_node;

	write_summary(ctx, "Building sppPostDevData requestReason='%s'",
		      reason);
	spp_node = xml_node_create_root(ctx->xml, SPP_NS_URI, "spp", &ns,
					"sppPostDevData");
	if (spp_node == NULL)
		return NULL;
	if (ret_ns)
		*ret_ns = ns;

	xml_node_add_attr(ctx->xml, spp_node, ns, "sppVersion", "1.0");
	xml_node_add_attr(ctx->xml, spp_node, NULL, "requestReason", reason);
	if (session_id)
		xml_node_add_attr(ctx->xml, spp_node, ns, "sessionID",
				  session_id);
	xml_node_add_attr(ctx->xml, spp_node, NULL, "redirectURI",
			  "http://localhost:12345/");

	xml_node_create_text(ctx->xml, spp_node, ns, "supportedSPPVersions",
			     "1.0");
	xml_node_create_text(ctx->xml, spp_node, ns, "supportedMOList",
			     URN_HS20_PPS " " URN_OMA_DM_DEVINFO " "
			     URN_OMA_DM_DEVDETAIL " " URN_HS20_DEVDETAIL_EXT);

	add_mo_container(ctx->xml, ns, spp_node, URN_OMA_DM_DEVINFO,
			 "devinfo.xml");
	add_mo_container(ctx->xml, ns, spp_node, URN_OMA_DM_DEVDETAIL,
			 "devdetail.xml");

	return spp_node;
}


static int process_update_node(struct hs20_osu_client *ctx, xml_node_t *pps,
			       xml_node_t *update)
{
	xml_node_t *node, *parent, *tnds, *unode;
	char *str;
	const char *name;
	char *uri, *pos;
	char *cdata, *cdata_end;
	size_t fqdn_len;

	wpa_printf(MSG_INFO, "Processing updateNode");
	debug_dump_node(ctx, "updateNode", update);

	uri = get_spp_attr_value(ctx->xml, update, "managementTreeURI");
	if (uri == NULL) {
		wpa_printf(MSG_INFO, "No managementTreeURI present");
		return -1;
	}
	wpa_printf(MSG_INFO, "managementTreeUri: '%s'", uri);

	name = os_strrchr(uri, '/');
	if (name == NULL) {
		wpa_printf(MSG_INFO, "Unexpected URI");
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}
	name++;
	wpa_printf(MSG_INFO, "Update interior node: '%s'", name);

	str = xml_node_get_text(ctx->xml, update);
	if (str == NULL) {
		wpa_printf(MSG_INFO, "Could not extract MO text");
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "[hs20] nodeContainer text: '%s'", str);
	cdata = strstr(str, "<![CDATA[");
	cdata_end = strstr(str, "]]>");
	if (cdata && cdata_end && cdata_end > cdata &&
	    cdata < strstr(str, "MgmtTree") &&
	    cdata_end > strstr(str, "/MgmtTree")) {
		char *tmp;
		wpa_printf(MSG_DEBUG, "[hs20] Removing extra CDATA container");
		tmp = strdup(cdata + 9);
		if (tmp) {
			cdata_end = strstr(tmp, "]]>");
			if (cdata_end)
				*cdata_end = '\0';
			wpa_printf(MSG_DEBUG, "[hs20] nodeContainer text with CDATA container removed: '%s'",
				   tmp);
			tnds = xml_node_from_buf(ctx->xml, tmp);
			free(tmp);
		} else
			tnds = NULL;
	} else
		tnds = xml_node_from_buf(ctx->xml, str);
	xml_node_get_text_free(ctx->xml, str);
	if (tnds == NULL) {
		wpa_printf(MSG_INFO, "[hs20] Could not parse nodeContainer text");
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}

	unode = tnds_to_mo(ctx->xml, tnds);
	xml_node_free(ctx->xml, tnds);
	if (unode == NULL) {
		wpa_printf(MSG_INFO, "[hs20] Could not parse nodeContainer TNDS text");
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}

	debug_dump_node(ctx, "Parsed TNDS", unode);

	if (get_node_uri(ctx->xml, unode, name) == NULL) {
		wpa_printf(MSG_INFO, "[hs20] %s node not found", name);
		xml_node_free(ctx->xml, unode);
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}

	if (os_strncasecmp(uri, "./Wi-Fi/", 8) != 0) {
		wpa_printf(MSG_INFO, "Do not allow update outside ./Wi-Fi");
		xml_node_free(ctx->xml, unode);
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}
	pos = uri + 8;

	if (ctx->fqdn == NULL) {
		wpa_printf(MSG_INFO, "FQDN not known");
		xml_node_free(ctx->xml, unode);
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}
	fqdn_len = os_strlen(ctx->fqdn);
	if (os_strncasecmp(pos, ctx->fqdn, fqdn_len) != 0 ||
	    pos[fqdn_len] != '/') {
		wpa_printf(MSG_INFO, "Do not allow update outside ./Wi-Fi/%s",
			   ctx->fqdn);
		xml_node_free(ctx->xml, unode);
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}
	pos += fqdn_len + 1;

	if (os_strncasecmp(pos, "PerProviderSubscription/", 24) != 0) {
		wpa_printf(MSG_INFO, "Do not allow update outside ./Wi-Fi/%s/PerProviderSubscription",
			   ctx->fqdn);
		xml_node_free(ctx->xml, unode);
		xml_node_get_attr_value_free(ctx->xml, uri);
		return -1;
	}
	pos += 24;

	wpa_printf(MSG_INFO, "Update command for PPS node %s", pos);

	node = get_node(ctx->xml, pps, pos);
	if (node) {
		parent = xml_node_get_parent(ctx->xml, node);
		xml_node_detach(ctx->xml, node);
		wpa_printf(MSG_INFO, "Replace '%s' node", name);
	} else {
		char *pos2;
		pos2 = os_strrchr(pos, '/');
		if (pos2 == NULL) {
			parent = pps;
		} else {
			*pos2 = '\0';
			parent = get_node(ctx->xml, pps, pos);
		}
		if (parent == NULL) {
			wpa_printf(MSG_INFO, "Could not find parent %s", pos);
			xml_node_free(ctx->xml, unode);
			xml_node_get_attr_value_free(ctx->xml, uri);
			return -1;
		}
		wpa_printf(MSG_INFO, "Add '%s' node", name);
	}
	xml_node_add_child(ctx->xml, parent, unode);

	xml_node_get_attr_value_free(ctx->xml, uri);

	return 0;
}


static int update_pps(struct hs20_osu_client *ctx, xml_node_t *update,
		      const char *pps_fname, xml_node_t *pps)
{
	wpa_printf(MSG_INFO, "Updating PPS based on updateNode element(s)");
	xml_node_for_each_sibling(ctx->xml, update) {
		xml_node_for_each_check(ctx->xml, update);
		if (process_update_node(ctx, pps, update) < 0)
			return -1;
	}

	return update_pps_file(ctx, pps_fname, pps);
}


static void hs20_sub_rem_complete(struct hs20_osu_client *ctx,
				  const char *pps_fname)
{
	/*
	 * Update wpa_supplicant credentials and reconnect using updated
	 * information.
	 */
	wpa_printf(MSG_INFO, "Updating wpa_supplicant credentials");
	cmd_set_pps(ctx, pps_fname);

	if (ctx->no_reconnect)
		return;

	wpa_printf(MSG_INFO, "Requesting reconnection with updated configuration");
	if (wpa_command(ctx->ifname, "INTERWORKING_SELECT auto") < 0)
		wpa_printf(MSG_ERROR, "Failed to request wpa_supplicant to reconnect");
}


static xml_node_t * hs20_spp_upload_mo(struct hs20_osu_client *ctx,
				       xml_node_t *cmd,
				       const char *session_id,
				       const char *pps_fname)
{
	xml_namespace_t *ns;
	xml_node_t *node, *ret_node;
	char *urn;

	urn = get_spp_attr_value(ctx->xml, cmd, "moURN");
	if (!urn) {
		wpa_printf(MSG_INFO, "No URN included");
		return NULL;
	}
	wpa_printf(MSG_INFO, "Upload MO request - URN=%s", urn);
	if (strcasecmp(urn, URN_HS20_PPS) != 0) {
		wpa_printf(MSG_INFO, "Unsupported moURN");
		xml_node_get_attr_value_free(ctx->xml, urn);
		return NULL;
	}
	xml_node_get_attr_value_free(ctx->xml, urn);

	if (!pps_fname) {
		wpa_printf(MSG_INFO, "PPS file name no known");
		return NULL;
	}

	node = build_spp_post_dev_data(ctx, &ns, session_id,
				       "MO upload");
	if (node == NULL)
		return NULL;
	add_mo_container(ctx->xml, ns, node, URN_HS20_PPS, pps_fname);

	ret_node = soap_send_receive(ctx->http, node);
	if (ret_node == NULL)
		return NULL;

	debug_dump_node(ctx, "Received response to MO upload", ret_node);

	if (hs20_spp_validate(ctx, ret_node, "sppPostDevDataResponse") < 0) {
		wpa_printf(MSG_INFO, "SPP validation failed");
		xml_node_free(ctx->xml, ret_node);
		return NULL;
	}

	return ret_node;
}


static int hs20_add_mo(struct hs20_osu_client *ctx, xml_node_t *add_mo,
		       char *fname, size_t fname_len)
{
	char *uri, *urn;
	int ret;

	debug_dump_node(ctx, "Received addMO", add_mo);

	urn = get_spp_attr_value(ctx->xml, add_mo, "moURN");
	if (urn == NULL) {
		wpa_printf(MSG_INFO, "[hs20] No moURN in addMO");
		return -1;
	}
	wpa_printf(MSG_INFO, "addMO - moURN: '%s'", urn);
	if (strcasecmp(urn, URN_HS20_PPS) != 0) {
		wpa_printf(MSG_INFO, "[hs20] Unsupported MO in addMO");
		xml_node_get_attr_value_free(ctx->xml, urn);
		return -1;
	}
	xml_node_get_attr_value_free(ctx->xml, urn);

	uri = get_spp_attr_value(ctx->xml, add_mo, "managementTreeURI");
	if (uri == NULL) {
		wpa_printf(MSG_INFO, "[hs20] No managementTreeURI in addMO");
		return -1;
	}
	wpa_printf(MSG_INFO, "addMO - managementTreeURI: '%s'", uri);

	ret = hs20_add_pps_mo(ctx, uri, add_mo, fname, fname_len);
	xml_node_get_attr_value_free(ctx->xml, uri);
	return ret;
}


static int process_spp_user_input_response(struct hs20_osu_client *ctx,
					   const char *session_id,
					   xml_node_t *add_mo)
{
	int ret;
	char fname[300];

	debug_dump_node(ctx, "addMO", add_mo);

	wpa_printf(MSG_INFO, "Subscription registration completed");

	if (hs20_add_mo(ctx, add_mo, fname, sizeof(fname)) < 0) {
		wpa_printf(MSG_INFO, "Could not add MO");
		ret = hs20_spp_update_response(
			ctx, session_id,
			"Error occurred",
			"MO addition or update failed");
		return 0;
	}

	ret = hs20_spp_update_response(ctx, session_id, "OK", NULL);
	if (ret == 0)
		hs20_sub_rem_complete(ctx, fname);

	return 0;
}


static xml_node_t * hs20_spp_user_input_completed(struct hs20_osu_client *ctx,
						    const char *session_id)
{
	xml_node_t *node, *ret_node;

	node = build_spp_post_dev_data(ctx, NULL, session_id,
				       "User input completed");
	if (node == NULL)
		return NULL;

	ret_node = soap_send_receive(ctx->http, node);
	if (!ret_node) {
		if (soap_reinit_client(ctx->http) < 0)
			return NULL;
		wpa_printf(MSG_INFO, "Try to finish with re-opened connection");
		node = build_spp_post_dev_data(ctx, NULL, session_id,
					       "User input completed");
		if (node == NULL)
			return NULL;
		ret_node = soap_send_receive(ctx->http, node);
		if (ret_node == NULL)
			return NULL;
		wpa_printf(MSG_INFO, "Continue with new connection");
	}

	if (hs20_spp_validate(ctx, ret_node, "sppPostDevDataResponse") < 0) {
		wpa_printf(MSG_INFO, "SPP validation failed");
		xml_node_free(ctx->xml, ret_node);
		return NULL;
	}

	return ret_node;
}


static xml_node_t * hs20_spp_get_certificate(struct hs20_osu_client *ctx,
					     xml_node_t *cmd,
					     const char *session_id,
					     const char *pps_fname)
{
	xml_namespace_t *ns;
	xml_node_t *node, *ret_node;
	int res;

	wpa_printf(MSG_INFO, "Client certificate enrollment");

	res = osu_get_certificate(ctx, cmd);
	if (res < 0)
		wpa_printf(MSG_INFO, "EST simpleEnroll failed");

	node = build_spp_post_dev_data(ctx, &ns, session_id,
				       res == 0 ?
				       "Certificate enrollment completed" :
				       "Certificate enrollment failed");
	if (node == NULL)
		return NULL;

	ret_node = soap_send_receive(ctx->http, node);
	if (ret_node == NULL)
		return NULL;

	debug_dump_node(ctx, "Received response to certificate enrollment "
			"completed", ret_node);

	if (hs20_spp_validate(ctx, ret_node, "sppPostDevDataResponse") < 0) {
		wpa_printf(MSG_INFO, "SPP validation failed");
		xml_node_free(ctx->xml, ret_node);
		return NULL;
	}

	return ret_node;
}


static int hs20_spp_exec(struct hs20_osu_client *ctx, xml_node_t *exec,
			 const char *session_id, const char *pps_fname,
			 xml_node_t *pps, xml_node_t **ret_node)
{
	xml_node_t *cmd;
	const char *name;
	char *uri;
	char *id = strdup(session_id);

	if (id == NULL)
		return -1;

	*ret_node = NULL;

	debug_dump_node(ctx, "exec", exec);

	xml_node_for_each_child(ctx->xml, cmd, exec) {
		xml_node_for_each_check(ctx->xml, cmd);
		break;
	}
	if (!cmd) {
		wpa_printf(MSG_INFO, "exec command element not found (cmd=%p)",
			   cmd);
		free(id);
		return -1;
	}

	name = xml_node_get_localname(ctx->xml, cmd);

	if (strcasecmp(name, "launchBrowserToURI") == 0) {
		int res;
		uri = xml_node_get_text(ctx->xml, cmd);
		if (!uri) {
			wpa_printf(MSG_INFO, "No URI found");
			free(id);
			return -1;
		}
		wpa_printf(MSG_INFO, "Launch browser to URI '%s'", uri);
		write_summary(ctx, "Launch browser to URI '%s'", uri);
		res = hs20_web_browser(uri);
		xml_node_get_text_free(ctx->xml, uri);
		if (res > 0) {
			wpa_printf(MSG_INFO, "User response in browser completed successfully - sessionid='%s'",
				   id);
			write_summary(ctx, "User response in browser completed successfully");
			*ret_node = hs20_spp_user_input_completed(ctx, id);
			free(id);
			return *ret_node ? 0 : -1;
		} else {
			wpa_printf(MSG_INFO, "Failed to receive user response");
			write_summary(ctx, "Failed to receive user response");
			hs20_spp_update_response(
				ctx, id, "Error occurred", "Other");
			free(id);
			return -1;
		}
		return 0;
	}

	if (strcasecmp(name, "uploadMO") == 0) {
		if (pps_fname == NULL)
			return -1;
		*ret_node = hs20_spp_upload_mo(ctx, cmd, id,
					       pps_fname);
		free(id);
		return *ret_node ? 0 : -1;
	}

	if (strcasecmp(name, "getCertificate") == 0) {
		*ret_node = hs20_spp_get_certificate(ctx, cmd, id,
						     pps_fname);
		free(id);
		return *ret_node ? 0 : -1;
	}

	wpa_printf(MSG_INFO, "Unsupported exec command: '%s'", name);
	free(id);
	return -1;
}


enum spp_post_dev_data_use {
	SPP_SUBSCRIPTION_REMEDIATION,
	SPP_POLICY_UPDATE,
	SPP_SUBSCRIPTION_REGISTRATION,
};

static void process_spp_post_dev_data_response(
	struct hs20_osu_client *ctx,
	enum spp_post_dev_data_use use, xml_node_t *node,
	const char *pps_fname, xml_node_t *pps)
{
	xml_node_t *child;
	char *status = NULL;
	xml_node_t *update = NULL, *exec = NULL, *add_mo = NULL, *no_mo = NULL;
	char *session_id = NULL;

	debug_dump_node(ctx, "sppPostDevDataResponse node", node);

	status = get_spp_attr_value(ctx->xml, node, "sppStatus");
	if (status == NULL) {
		wpa_printf(MSG_INFO, "No sppStatus attribute");
		goto out;
	}
	write_summary(ctx, "Received sppPostDevDataResponse sppStatus='%s'",
		      status);

	session_id = get_spp_attr_value(ctx->xml, node, "sessionID");
	if (session_id == NULL) {
		wpa_printf(MSG_INFO, "No sessionID attribute");
		goto out;
	}

	wpa_printf(MSG_INFO, "[hs20] sppPostDevDataResponse - sppStatus: '%s'  sessionID: '%s'",
		   status, session_id);

	xml_node_for_each_child(ctx->xml, child, node) {
		const char *name;
		xml_node_for_each_check(ctx->xml, child);
		debug_dump_node(ctx, "child", child);
		name = xml_node_get_localname(ctx->xml, child);
		wpa_printf(MSG_INFO, "localname: '%s'", name);
		if (!update && strcasecmp(name, "updateNode") == 0)
			update = child;
		if (!exec && strcasecmp(name, "exec") == 0)
			exec = child;
		if (!add_mo && strcasecmp(name, "addMO") == 0)
			add_mo = child;
		if (!no_mo && strcasecmp(name, "noMOUpdate") == 0)
			no_mo = child;
	}

	if (use == SPP_SUBSCRIPTION_REMEDIATION &&
	    strcasecmp(status,
		       "Remediation complete, request sppUpdateResponse") == 0)
	{
		int res, ret;
		if (!update && !no_mo) {
			wpa_printf(MSG_INFO, "No updateNode or noMOUpdate element");
			goto out;
		}
		wpa_printf(MSG_INFO, "Subscription remediation completed");
		res = update_pps(ctx, update, pps_fname, pps);
		if (res < 0)
			wpa_printf(MSG_INFO, "Failed to update PPS MO");
		ret = hs20_spp_update_response(
			ctx, session_id,
			res < 0 ? "Error occurred" : "OK",
			res < 0 ? "MO addition or update failed" : NULL);
		if (res == 0 && ret == 0)
			hs20_sub_rem_complete(ctx, pps_fname);
		goto out;
	}

	if (use == SPP_SUBSCRIPTION_REMEDIATION &&
	    strcasecmp(status, "Exchange complete, release TLS connection") ==
	    0) {
		if (!no_mo) {
			wpa_printf(MSG_INFO, "No noMOUpdate element");
			goto out;
		}
		wpa_printf(MSG_INFO, "Subscription remediation completed (no MO update)");
		goto out;
	}

	if (use == SPP_POLICY_UPDATE &&
	    strcasecmp(status, "Update complete, request sppUpdateResponse") ==
	    0) {
		int res, ret;
		wpa_printf(MSG_INFO, "Policy update received - update PPS");
		res = update_pps(ctx, update, pps_fname, pps);
		ret = hs20_spp_update_response(
			ctx, session_id,
			res < 0 ? "Error occurred" : "OK",
			res < 0 ? "MO addition or update failed" : NULL);
		if (res == 0 && ret == 0)
			hs20_policy_update_complete(ctx, pps_fname);
		goto out;
	}

	if (use == SPP_SUBSCRIPTION_REGISTRATION &&
	    strcasecmp(status, "Provisioning complete, request "
		       "sppUpdateResponse")  == 0) {
		if (!add_mo) {
			wpa_printf(MSG_INFO, "No addMO element - not sure what to do next");
			goto out;
		}
		process_spp_user_input_response(ctx, session_id, add_mo);
		node = NULL;
		goto out;
	}

	if (strcasecmp(status, "No update available at this time") == 0) {
		wpa_printf(MSG_INFO, "No update available at this time");
		goto out;
	}

	if (strcasecmp(status, "OK") == 0) {
		int res;
		xml_node_t *ret;

		if (!exec) {
			wpa_printf(MSG_INFO, "No exec element - not sure what to do next");
			goto out;
		}
		res = hs20_spp_exec(ctx, exec, session_id,
				    pps_fname, pps, &ret);
		/* xml_node_free(ctx->xml, node); */
		node = NULL;
		if (res == 0 && ret)
			process_spp_post_dev_data_response(ctx, use,
							   ret, pps_fname, pps);
		goto out;
	}

	if (strcasecmp(status, "Error occurred") == 0) {
		xml_node_t *err;
		char *code = NULL;
		err = get_node(ctx->xml, node, "sppError");
		if (err)
			code = xml_node_get_attr_value(ctx->xml, err,
						       "errorCode");
		wpa_printf(MSG_INFO, "Error occurred - errorCode=%s",
			   code ? code : "N/A");
		xml_node_get_attr_value_free(ctx->xml, code);
		goto out;
	}

	wpa_printf(MSG_INFO,
		   "[hs20] Unsupported sppPostDevDataResponse sppStatus '%s'",
		   status);
out:
	xml_node_get_attr_value_free(ctx->xml, status);
	xml_node_get_attr_value_free(ctx->xml, session_id);
	xml_node_free(ctx->xml, node);
}


static int spp_post_dev_data(struct hs20_osu_client *ctx,
			     enum spp_post_dev_data_use use,
			     const char *reason,
			     const char *pps_fname, xml_node_t *pps)
{
	xml_node_t *payload;
	xml_node_t *ret_node;

	payload = build_spp_post_dev_data(ctx, NULL, NULL, reason);
	if (payload == NULL)
		return -1;

	ret_node = soap_send_receive(ctx->http, payload);
	if (!ret_node) {
		const char *err = http_get_err(ctx->http);
		if (err) {
			wpa_printf(MSG_INFO, "HTTP error: %s", err);
			write_result(ctx, "HTTP error: %s", err);
		} else {
			write_summary(ctx, "Failed to send SOAP message");
		}
		return -1;
	}

	if (hs20_spp_validate(ctx, ret_node, "sppPostDevDataResponse") < 0) {
		wpa_printf(MSG_INFO, "SPP validation failed");
		xml_node_free(ctx->xml, ret_node);
		return -1;
	}

	process_spp_post_dev_data_response(ctx, use, ret_node,
					   pps_fname, pps);
	return 0;
}


void spp_sub_rem(struct hs20_osu_client *ctx, const char *address,
		 const char *pps_fname,
		 const char *client_cert, const char *client_key,
		 const char *cred_username, const char *cred_password,
		 xml_node_t *pps)
{
	wpa_printf(MSG_INFO, "SPP subscription remediation");
	write_summary(ctx, "SPP subscription remediation");

	os_free(ctx->server_url);
	ctx->server_url = os_strdup(address);

	if (soap_init_client(ctx->http, address, ctx->ca_fname,
			     cred_username, cred_password, client_cert,
			     client_key) == 0) {
		spp_post_dev_data(ctx, SPP_SUBSCRIPTION_REMEDIATION,
				  "Subscription remediation", pps_fname, pps);
	}
}


static void hs20_policy_update_complete(struct hs20_osu_client *ctx,
					const char *pps_fname)
{
	wpa_printf(MSG_INFO, "Policy update completed");

	/*
	 * Update wpa_supplicant credentials and reconnect using updated
	 * information.
	 */
	wpa_printf(MSG_INFO, "Updating wpa_supplicant credentials");
	cmd_set_pps(ctx, pps_fname);

	wpa_printf(MSG_INFO, "Requesting reconnection with updated configuration");
	if (wpa_command(ctx->ifname, "INTERWORKING_SELECT auto") < 0)
		wpa_printf(MSG_ERROR, "Failed to request wpa_supplicant to reconnect");
}


static int process_spp_exchange_complete(struct hs20_osu_client *ctx,
					 xml_node_t *node)
{
	char *status, *session_id;

	debug_dump_node(ctx, "sppExchangeComplete", node);

	status = get_spp_attr_value(ctx->xml, node, "sppStatus");
	if (status == NULL) {
		wpa_printf(MSG_INFO, "No sppStatus attribute");
		return -1;
	}
	write_summary(ctx, "Received sppExchangeComplete sppStatus='%s'",
		      status);

	session_id = get_spp_attr_value(ctx->xml, node, "sessionID");
	if (session_id == NULL) {
		wpa_printf(MSG_INFO, "No sessionID attribute");
		xml_node_get_attr_value_free(ctx->xml, status);
		return -1;
	}

	wpa_printf(MSG_INFO, "[hs20] sppStatus: '%s'  sessionID: '%s'",
		   status, session_id);
	xml_node_get_attr_value_free(ctx->xml, session_id);

	if (strcasecmp(status, "Exchange complete, release TLS connection") ==
	    0) {
		xml_node_get_attr_value_free(ctx->xml, status);
		return 0;
	}

	wpa_printf(MSG_INFO, "Unexpected sppStatus '%s'", status);
	write_summary(ctx, "Unexpected sppStatus '%s'", status);
	xml_node_get_attr_value_free(ctx->xml, status);
	return -1;
}


static xml_node_t * build_spp_update_response(struct hs20_osu_client *ctx,
					      const char *session_id,
					      const char *spp_status,
					      const char *error_code)
{
	xml_namespace_t *ns;
	xml_node_t *spp_node, *node;

	spp_node = xml_node_create_root(ctx->xml, SPP_NS_URI, "spp", &ns,
					"sppUpdateResponse");
	if (spp_node == NULL)
		return NULL;

	xml_node_add_attr(ctx->xml, spp_node, ns, "sppVersion", "1.0");
	xml_node_add_attr(ctx->xml, spp_node, ns, "sessionID", session_id);
	xml_node_add_attr(ctx->xml, spp_node, ns, "sppStatus", spp_status);

	if (error_code) {
		node = xml_node_create(ctx->xml, spp_node, ns, "sppError");
		if (node)
			xml_node_add_attr(ctx->xml, node, NULL, "errorCode",
					  error_code);
	}

	return spp_node;
}


static int hs20_spp_update_response(struct hs20_osu_client *ctx,
				    const char *session_id,
				    const char *spp_status,
				    const char *error_code)
{
	xml_node_t *node, *ret_node;
	int ret;

	write_summary(ctx, "Building sppUpdateResponse sppStatus='%s' error_code='%s'",
		      spp_status, error_code);
	node = build_spp_update_response(ctx, session_id, spp_status,
					 error_code);
	if (node == NULL)
		return -1;
	ret_node = soap_send_receive(ctx->http, node);
	if (!ret_node) {
		if (soap_reinit_client(ctx->http) < 0)
			return -1;
		wpa_printf(MSG_INFO, "Try to finish with re-opened connection");
		node = build_spp_update_response(ctx, session_id, spp_status,
						 error_code);
		if (node == NULL)
			return -1;
		ret_node = soap_send_receive(ctx->http, node);
		if (ret_node == NULL)
			return -1;
		wpa_printf(MSG_INFO, "Continue with new connection");
	}

	if (hs20_spp_validate(ctx, ret_node, "sppExchangeComplete") < 0) {
		wpa_printf(MSG_INFO, "SPP validation failed");
		xml_node_free(ctx->xml, ret_node);
		return -1;
	}

	ret = process_spp_exchange_complete(ctx, ret_node);
	xml_node_free(ctx->xml, ret_node);
	return ret;
}


void spp_pol_upd(struct hs20_osu_client *ctx, const char *address,
		 const char *pps_fname,
		 const char *client_cert, const char *client_key,
		 const char *cred_username, const char *cred_password,
		 xml_node_t *pps)
{
	wpa_printf(MSG_INFO, "SPP policy update");
	write_summary(ctx, "SPP policy update");

	os_free(ctx->server_url);
	ctx->server_url = os_strdup(address);

	if (soap_init_client(ctx->http, address, ctx->ca_fname, cred_username,
			     cred_password, client_cert, client_key) == 0) {
		spp_post_dev_data(ctx, SPP_POLICY_UPDATE, "Policy update",
				  pps_fname, pps);
	}
}


int cmd_prov(struct hs20_osu_client *ctx, const char *url)
{
	unlink("Cert/est_cert.der");
	unlink("Cert/est_cert.pem");

	if (url == NULL) {
		wpa_printf(MSG_INFO, "Invalid prov command (missing URL)");
		return -1;
	}

	wpa_printf(MSG_INFO,
		   "Credential provisioning requested - URL: %s ca_fname: %s",
		   url, ctx->ca_fname ? ctx->ca_fname : "N/A");

	os_free(ctx->server_url);
	ctx->server_url = os_strdup(url);

	if (soap_init_client(ctx->http, url, ctx->ca_fname, NULL, NULL, NULL,
			     NULL) < 0)
		return -1;
	spp_post_dev_data(ctx, SPP_SUBSCRIPTION_REGISTRATION,
			  "Subscription registration", NULL, NULL);

	return ctx->pps_cred_set ? 0 : -1;
}


int cmd_sim_prov(struct hs20_osu_client *ctx, const char *url)
{
	if (url == NULL) {
		wpa_printf(MSG_INFO, "Invalid prov command (missing URL)");
		return -1;
	}

	wpa_printf(MSG_INFO, "SIM provisioning requested");

	os_free(ctx->server_url);
	ctx->server_url = os_strdup(url);

	wpa_printf(MSG_INFO, "Wait for IP address before starting SIM provisioning");

	if (wait_ip_addr(ctx->ifname, 15) < 0) {
		wpa_printf(MSG_INFO, "Could not get IP address for WLAN - try connection anyway");
	}

	if (soap_init_client(ctx->http, url, ctx->ca_fname, NULL, NULL, NULL,
			     NULL) < 0)
		return -1;
	spp_post_dev_data(ctx, SPP_SUBSCRIPTION_REGISTRATION,
			  "Subscription provisioning", NULL, NULL);

	return ctx->pps_cred_set ? 0 : -1;
}
