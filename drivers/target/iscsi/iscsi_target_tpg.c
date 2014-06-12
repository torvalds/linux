/*******************************************************************************
 * This file contains iSCSI Target Portal Group related functions.
 *
 * (c) Copyright 2007-2013 Datera, Inc.
 *
 * Author: Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 ******************************************************************************/

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_configfs.h>

#include "iscsi_target_core.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_login.h"
#include "iscsi_target_nodeattrib.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"
#include "iscsi_target_parameters.h"

#include <target/iscsi/iscsi_transport.h>

struct iscsi_portal_group *iscsit_alloc_portal_group(struct iscsi_tiqn *tiqn, u16 tpgt)
{
	struct iscsi_portal_group *tpg;

	tpg = kzalloc(sizeof(struct iscsi_portal_group), GFP_KERNEL);
	if (!tpg) {
		pr_err("Unable to allocate struct iscsi_portal_group\n");
		return NULL;
	}

	tpg->tpgt = tpgt;
	tpg->tpg_state = TPG_STATE_FREE;
	tpg->tpg_tiqn = tiqn;
	INIT_LIST_HEAD(&tpg->tpg_gnp_list);
	INIT_LIST_HEAD(&tpg->tpg_list);
	mutex_init(&tpg->tpg_access_lock);
	sema_init(&tpg->np_login_sem, 1);
	spin_lock_init(&tpg->tpg_state_lock);
	spin_lock_init(&tpg->tpg_np_lock);

	return tpg;
}

static void iscsit_set_default_tpg_attribs(struct iscsi_portal_group *);

int iscsit_load_discovery_tpg(void)
{
	struct iscsi_param *param;
	struct iscsi_portal_group *tpg;
	int ret;

	tpg = iscsit_alloc_portal_group(NULL, 1);
	if (!tpg) {
		pr_err("Unable to allocate struct iscsi_portal_group\n");
		return -1;
	}

	ret = core_tpg_register(
			&lio_target_fabric_configfs->tf_ops,
			NULL, &tpg->tpg_se_tpg, tpg,
			TRANSPORT_TPG_TYPE_DISCOVERY);
	if (ret < 0) {
		kfree(tpg);
		return -1;
	}

	tpg->sid = 1; /* First Assigned LIO Session ID */
	iscsit_set_default_tpg_attribs(tpg);

	if (iscsi_create_default_params(&tpg->param_list) < 0)
		goto out;
	/*
	 * By default we disable authentication for discovery sessions,
	 * this can be changed with:
	 *
	 * /sys/kernel/config/target/iscsi/discovery_auth/enforce_discovery_auth
	 */
	param = iscsi_find_param_from_key(AUTHMETHOD, tpg->param_list);
	if (!param)
		goto out;

	if (iscsi_update_param_value(param, "CHAP,None") < 0)
		goto out;

	tpg->tpg_attrib.authentication = 0;

	spin_lock(&tpg->tpg_state_lock);
	tpg->tpg_state  = TPG_STATE_ACTIVE;
	spin_unlock(&tpg->tpg_state_lock);

	iscsit_global->discovery_tpg = tpg;
	pr_debug("CORE[0] - Allocated Discovery TPG\n");

	return 0;
out:
	if (tpg->sid == 1)
		core_tpg_deregister(&tpg->tpg_se_tpg);
	kfree(tpg);
	return -1;
}

void iscsit_release_discovery_tpg(void)
{
	struct iscsi_portal_group *tpg = iscsit_global->discovery_tpg;

	if (!tpg)
		return;

	core_tpg_deregister(&tpg->tpg_se_tpg);

	kfree(tpg);
	iscsit_global->discovery_tpg = NULL;
}

struct iscsi_portal_group *iscsit_get_tpg_from_np(
	struct iscsi_tiqn *tiqn,
	struct iscsi_np *np,
	struct iscsi_tpg_np **tpg_np_out)
{
	struct iscsi_portal_group *tpg = NULL;
	struct iscsi_tpg_np *tpg_np;

	spin_lock(&tiqn->tiqn_tpg_lock);
	list_for_each_entry(tpg, &tiqn->tiqn_tpg_list, tpg_list) {

		spin_lock(&tpg->tpg_state_lock);
		if (tpg->tpg_state != TPG_STATE_ACTIVE) {
			spin_unlock(&tpg->tpg_state_lock);
			continue;
		}
		spin_unlock(&tpg->tpg_state_lock);

		spin_lock(&tpg->tpg_np_lock);
		list_for_each_entry(tpg_np, &tpg->tpg_gnp_list, tpg_np_list) {
			if (tpg_np->tpg_np == np) {
				*tpg_np_out = tpg_np;
				kref_get(&tpg_np->tpg_np_kref);
				spin_unlock(&tpg->tpg_np_lock);
				spin_unlock(&tiqn->tiqn_tpg_lock);
				return tpg;
			}
		}
		spin_unlock(&tpg->tpg_np_lock);
	}
	spin_unlock(&tiqn->tiqn_tpg_lock);

	return NULL;
}

int iscsit_get_tpg(
	struct iscsi_portal_group *tpg)
{
	int ret;

	ret = mutex_lock_interruptible(&tpg->tpg_access_lock);
	return ((ret != 0) || signal_pending(current)) ? -1 : 0;
}

void iscsit_put_tpg(struct iscsi_portal_group *tpg)
{
	mutex_unlock(&tpg->tpg_access_lock);
}

static void iscsit_clear_tpg_np_login_thread(
	struct iscsi_tpg_np *tpg_np,
	struct iscsi_portal_group *tpg,
	bool shutdown)
{
	if (!tpg_np->tpg_np) {
		pr_err("struct iscsi_tpg_np->tpg_np is NULL!\n");
		return;
	}

	tpg_np->tpg_np->enabled = false;
	iscsit_reset_np_thread(tpg_np->tpg_np, tpg_np, tpg, shutdown);
}

void iscsit_clear_tpg_np_login_threads(
	struct iscsi_portal_group *tpg,
	bool shutdown)
{
	struct iscsi_tpg_np *tpg_np;

	spin_lock(&tpg->tpg_np_lock);
	list_for_each_entry(tpg_np, &tpg->tpg_gnp_list, tpg_np_list) {
		if (!tpg_np->tpg_np) {
			pr_err("struct iscsi_tpg_np->tpg_np is NULL!\n");
			continue;
		}
		spin_unlock(&tpg->tpg_np_lock);
		iscsit_clear_tpg_np_login_thread(tpg_np, tpg, shutdown);
		spin_lock(&tpg->tpg_np_lock);
	}
	spin_unlock(&tpg->tpg_np_lock);
}

void iscsit_tpg_dump_params(struct iscsi_portal_group *tpg)
{
	iscsi_print_params(tpg->param_list);
}

static void iscsit_set_default_tpg_attribs(struct iscsi_portal_group *tpg)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	a->authentication = TA_AUTHENTICATION;
	a->login_timeout = TA_LOGIN_TIMEOUT;
	a->netif_timeout = TA_NETIF_TIMEOUT;
	a->default_cmdsn_depth = TA_DEFAULT_CMDSN_DEPTH;
	a->generate_node_acls = TA_GENERATE_NODE_ACLS;
	a->cache_dynamic_acls = TA_CACHE_DYNAMIC_ACLS;
	a->demo_mode_write_protect = TA_DEMO_MODE_WRITE_PROTECT;
	a->prod_mode_write_protect = TA_PROD_MODE_WRITE_PROTECT;
	a->demo_mode_discovery = TA_DEMO_MODE_DISCOVERY;
	a->default_erl = TA_DEFAULT_ERL;
	a->t10_pi = TA_DEFAULT_T10_PI;
}

int iscsit_tpg_add_portal_group(struct iscsi_tiqn *tiqn, struct iscsi_portal_group *tpg)
{
	if (tpg->tpg_state != TPG_STATE_FREE) {
		pr_err("Unable to add iSCSI Target Portal Group: %d"
			" while not in TPG_STATE_FREE state.\n", tpg->tpgt);
		return -EEXIST;
	}
	iscsit_set_default_tpg_attribs(tpg);

	if (iscsi_create_default_params(&tpg->param_list) < 0)
		goto err_out;

	tpg->tpg_attrib.tpg = tpg;

	spin_lock(&tpg->tpg_state_lock);
	tpg->tpg_state	= TPG_STATE_INACTIVE;
	spin_unlock(&tpg->tpg_state_lock);

	spin_lock(&tiqn->tiqn_tpg_lock);
	list_add_tail(&tpg->tpg_list, &tiqn->tiqn_tpg_list);
	tiqn->tiqn_ntpgs++;
	pr_debug("CORE[%s]_TPG[%hu] - Added iSCSI Target Portal Group\n",
			tiqn->tiqn, tpg->tpgt);
	spin_unlock(&tiqn->tiqn_tpg_lock);

	return 0;
err_out:
	if (tpg->param_list) {
		iscsi_release_param_list(tpg->param_list);
		tpg->param_list = NULL;
	}
	kfree(tpg);
	return -ENOMEM;
}

int iscsit_tpg_del_portal_group(
	struct iscsi_tiqn *tiqn,
	struct iscsi_portal_group *tpg,
	int force)
{
	u8 old_state = tpg->tpg_state;

	spin_lock(&tpg->tpg_state_lock);
	tpg->tpg_state = TPG_STATE_INACTIVE;
	spin_unlock(&tpg->tpg_state_lock);

	iscsit_clear_tpg_np_login_threads(tpg, true);

	if (iscsit_release_sessions_for_tpg(tpg, force) < 0) {
		pr_err("Unable to delete iSCSI Target Portal Group:"
			" %hu while active sessions exist, and force=0\n",
			tpg->tpgt);
		tpg->tpg_state = old_state;
		return -EPERM;
	}

	core_tpg_clear_object_luns(&tpg->tpg_se_tpg);

	if (tpg->param_list) {
		iscsi_release_param_list(tpg->param_list);
		tpg->param_list = NULL;
	}

	core_tpg_deregister(&tpg->tpg_se_tpg);

	spin_lock(&tpg->tpg_state_lock);
	tpg->tpg_state = TPG_STATE_FREE;
	spin_unlock(&tpg->tpg_state_lock);

	spin_lock(&tiqn->tiqn_tpg_lock);
	tiqn->tiqn_ntpgs--;
	list_del(&tpg->tpg_list);
	spin_unlock(&tiqn->tiqn_tpg_lock);

	pr_debug("CORE[%s]_TPG[%hu] - Deleted iSCSI Target Portal Group\n",
			tiqn->tiqn, tpg->tpgt);

	kfree(tpg);
	return 0;
}

int iscsit_tpg_enable_portal_group(struct iscsi_portal_group *tpg)
{
	struct iscsi_param *param;
	struct iscsi_tiqn *tiqn = tpg->tpg_tiqn;
	int ret;

	spin_lock(&tpg->tpg_state_lock);
	if (tpg->tpg_state == TPG_STATE_ACTIVE) {
		pr_err("iSCSI target portal group: %hu is already"
			" active, ignoring request.\n", tpg->tpgt);
		spin_unlock(&tpg->tpg_state_lock);
		return -EINVAL;
	}
	/*
	 * Make sure that AuthMethod does not contain None as an option
	 * unless explictly disabled.  Set the default to CHAP if authentication
	 * is enforced (as per default), and remove the NONE option.
	 */
	param = iscsi_find_param_from_key(AUTHMETHOD, tpg->param_list);
	if (!param) {
		spin_unlock(&tpg->tpg_state_lock);
		return -EINVAL;
	}

	if (tpg->tpg_attrib.authentication) {
		if (!strcmp(param->value, NONE)) {
			ret = iscsi_update_param_value(param, CHAP);
			if (ret)
				goto err;
		}

		ret = iscsit_ta_authentication(tpg, 1);
		if (ret < 0)
			goto err;
	}

	tpg->tpg_state = TPG_STATE_ACTIVE;
	spin_unlock(&tpg->tpg_state_lock);

	spin_lock(&tiqn->tiqn_tpg_lock);
	tiqn->tiqn_active_tpgs++;
	pr_debug("iSCSI_TPG[%hu] - Enabled iSCSI Target Portal Group\n",
			tpg->tpgt);
	spin_unlock(&tiqn->tiqn_tpg_lock);

	return 0;

err:
	spin_unlock(&tpg->tpg_state_lock);
	return ret;
}

int iscsit_tpg_disable_portal_group(struct iscsi_portal_group *tpg, int force)
{
	struct iscsi_tiqn *tiqn;
	u8 old_state = tpg->tpg_state;

	spin_lock(&tpg->tpg_state_lock);
	if (tpg->tpg_state == TPG_STATE_INACTIVE) {
		pr_err("iSCSI Target Portal Group: %hu is already"
			" inactive, ignoring request.\n", tpg->tpgt);
		spin_unlock(&tpg->tpg_state_lock);
		return -EINVAL;
	}
	tpg->tpg_state = TPG_STATE_INACTIVE;
	spin_unlock(&tpg->tpg_state_lock);

	iscsit_clear_tpg_np_login_threads(tpg, false);

	if (iscsit_release_sessions_for_tpg(tpg, force) < 0) {
		spin_lock(&tpg->tpg_state_lock);
		tpg->tpg_state = old_state;
		spin_unlock(&tpg->tpg_state_lock);
		pr_err("Unable to disable iSCSI Target Portal Group:"
			" %hu while active sessions exist, and force=0\n",
			tpg->tpgt);
		return -EPERM;
	}

	tiqn = tpg->tpg_tiqn;
	if (!tiqn || (tpg == iscsit_global->discovery_tpg))
		return 0;

	spin_lock(&tiqn->tiqn_tpg_lock);
	tiqn->tiqn_active_tpgs--;
	pr_debug("iSCSI_TPG[%hu] - Disabled iSCSI Target Portal Group\n",
			tpg->tpgt);
	spin_unlock(&tiqn->tiqn_tpg_lock);

	return 0;
}

struct iscsi_node_attrib *iscsit_tpg_get_node_attrib(
	struct iscsi_session *sess)
{
	struct se_session *se_sess = sess->se_sess;
	struct se_node_acl *se_nacl = se_sess->se_node_acl;
	struct iscsi_node_acl *acl = container_of(se_nacl, struct iscsi_node_acl,
					se_node_acl);

	return &acl->node_attrib;
}

struct iscsi_tpg_np *iscsit_tpg_locate_child_np(
	struct iscsi_tpg_np *tpg_np,
	int network_transport)
{
	struct iscsi_tpg_np *tpg_np_child, *tpg_np_child_tmp;

	spin_lock(&tpg_np->tpg_np_parent_lock);
	list_for_each_entry_safe(tpg_np_child, tpg_np_child_tmp,
			&tpg_np->tpg_np_parent_list, tpg_np_child_list) {
		if (tpg_np_child->tpg_np->np_network_transport ==
				network_transport) {
			spin_unlock(&tpg_np->tpg_np_parent_lock);
			return tpg_np_child;
		}
	}
	spin_unlock(&tpg_np->tpg_np_parent_lock);

	return NULL;
}

static bool iscsit_tpg_check_network_portal(
	struct iscsi_tiqn *tiqn,
	struct __kernel_sockaddr_storage *sockaddr,
	int network_transport)
{
	struct iscsi_portal_group *tpg;
	struct iscsi_tpg_np *tpg_np;
	struct iscsi_np *np;
	bool match = false;

	spin_lock(&tiqn->tiqn_tpg_lock);
	list_for_each_entry(tpg, &tiqn->tiqn_tpg_list, tpg_list) {

		spin_lock(&tpg->tpg_np_lock);
		list_for_each_entry(tpg_np, &tpg->tpg_gnp_list, tpg_np_list) {
			np = tpg_np->tpg_np;

			match = iscsit_check_np_match(sockaddr, np,
						network_transport);
			if (match == true)
				break;
		}
		spin_unlock(&tpg->tpg_np_lock);
	}
	spin_unlock(&tiqn->tiqn_tpg_lock);

	return match;
}

struct iscsi_tpg_np *iscsit_tpg_add_network_portal(
	struct iscsi_portal_group *tpg,
	struct __kernel_sockaddr_storage *sockaddr,
	char *ip_str,
	struct iscsi_tpg_np *tpg_np_parent,
	int network_transport)
{
	struct iscsi_np *np;
	struct iscsi_tpg_np *tpg_np;

	if (!tpg_np_parent) {
		if (iscsit_tpg_check_network_portal(tpg->tpg_tiqn, sockaddr,
				network_transport) == true) {
			pr_err("Network Portal: %s already exists on a"
				" different TPG on %s\n", ip_str,
				tpg->tpg_tiqn->tiqn);
			return ERR_PTR(-EEXIST);
		}
	}

	tpg_np = kzalloc(sizeof(struct iscsi_tpg_np), GFP_KERNEL);
	if (!tpg_np) {
		pr_err("Unable to allocate memory for"
				" struct iscsi_tpg_np.\n");
		return ERR_PTR(-ENOMEM);
	}

	np = iscsit_add_np(sockaddr, ip_str, network_transport);
	if (IS_ERR(np)) {
		kfree(tpg_np);
		return ERR_CAST(np);
	}

	INIT_LIST_HEAD(&tpg_np->tpg_np_list);
	INIT_LIST_HEAD(&tpg_np->tpg_np_child_list);
	INIT_LIST_HEAD(&tpg_np->tpg_np_parent_list);
	spin_lock_init(&tpg_np->tpg_np_parent_lock);
	init_completion(&tpg_np->tpg_np_comp);
	kref_init(&tpg_np->tpg_np_kref);
	tpg_np->tpg_np		= np;
	np->tpg_np		= tpg_np;
	tpg_np->tpg		= tpg;

	spin_lock(&tpg->tpg_np_lock);
	list_add_tail(&tpg_np->tpg_np_list, &tpg->tpg_gnp_list);
	tpg->num_tpg_nps++;
	if (tpg->tpg_tiqn)
		tpg->tpg_tiqn->tiqn_num_tpg_nps++;
	spin_unlock(&tpg->tpg_np_lock);

	if (tpg_np_parent) {
		tpg_np->tpg_np_parent = tpg_np_parent;
		spin_lock(&tpg_np_parent->tpg_np_parent_lock);
		list_add_tail(&tpg_np->tpg_np_child_list,
			&tpg_np_parent->tpg_np_parent_list);
		spin_unlock(&tpg_np_parent->tpg_np_parent_lock);
	}

	pr_debug("CORE[%s] - Added Network Portal: %s:%hu,%hu on %s\n",
		tpg->tpg_tiqn->tiqn, np->np_ip, np->np_port, tpg->tpgt,
		np->np_transport->name);

	return tpg_np;
}

static int iscsit_tpg_release_np(
	struct iscsi_tpg_np *tpg_np,
	struct iscsi_portal_group *tpg,
	struct iscsi_np *np)
{
	iscsit_clear_tpg_np_login_thread(tpg_np, tpg, true);

	pr_debug("CORE[%s] - Removed Network Portal: %s:%hu,%hu on %s\n",
		tpg->tpg_tiqn->tiqn, np->np_ip, np->np_port, tpg->tpgt,
		np->np_transport->name);

	tpg_np->tpg_np = NULL;
	tpg_np->tpg = NULL;
	kfree(tpg_np);
	/*
	 * iscsit_del_np() will shutdown struct iscsi_np when last TPG reference is released.
	 */
	return iscsit_del_np(np);
}

int iscsit_tpg_del_network_portal(
	struct iscsi_portal_group *tpg,
	struct iscsi_tpg_np *tpg_np)
{
	struct iscsi_np *np;
	struct iscsi_tpg_np *tpg_np_child, *tpg_np_child_tmp;
	int ret = 0;

	np = tpg_np->tpg_np;
	if (!np) {
		pr_err("Unable to locate struct iscsi_np from"
				" struct iscsi_tpg_np\n");
		return -EINVAL;
	}

	if (!tpg_np->tpg_np_parent) {
		/*
		 * We are the parent tpg network portal.  Release all of the
		 * child tpg_np's (eg: the non ISCSI_TCP ones) on our parent
		 * list first.
		 */
		list_for_each_entry_safe(tpg_np_child, tpg_np_child_tmp,
				&tpg_np->tpg_np_parent_list,
				tpg_np_child_list) {
			ret = iscsit_tpg_del_network_portal(tpg, tpg_np_child);
			if (ret < 0)
				pr_err("iscsit_tpg_del_network_portal()"
					" failed: %d\n", ret);
		}
	} else {
		/*
		 * We are not the parent ISCSI_TCP tpg network portal.  Release
		 * our own network portals from the child list.
		 */
		spin_lock(&tpg_np->tpg_np_parent->tpg_np_parent_lock);
		list_del(&tpg_np->tpg_np_child_list);
		spin_unlock(&tpg_np->tpg_np_parent->tpg_np_parent_lock);
	}

	spin_lock(&tpg->tpg_np_lock);
	list_del(&tpg_np->tpg_np_list);
	tpg->num_tpg_nps--;
	if (tpg->tpg_tiqn)
		tpg->tpg_tiqn->tiqn_num_tpg_nps--;
	spin_unlock(&tpg->tpg_np_lock);

	return iscsit_tpg_release_np(tpg_np, tpg, np);
}

int iscsit_tpg_set_initiator_node_queue_depth(
	struct iscsi_portal_group *tpg,
	unsigned char *initiatorname,
	u32 queue_depth,
	int force)
{
	return core_tpg_set_initiator_node_queue_depth(&tpg->tpg_se_tpg,
		initiatorname, queue_depth, force);
}

int iscsit_ta_authentication(struct iscsi_portal_group *tpg, u32 authentication)
{
	unsigned char buf1[256], buf2[256], *none = NULL;
	int len;
	struct iscsi_param *param;
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if ((authentication != 1) && (authentication != 0)) {
		pr_err("Illegal value for authentication parameter:"
			" %u, ignoring request.\n", authentication);
		return -EINVAL;
	}

	memset(buf1, 0, sizeof(buf1));
	memset(buf2, 0, sizeof(buf2));

	param = iscsi_find_param_from_key(AUTHMETHOD, tpg->param_list);
	if (!param)
		return -EINVAL;

	if (authentication) {
		snprintf(buf1, sizeof(buf1), "%s", param->value);
		none = strstr(buf1, NONE);
		if (!none)
			goto out;
		if (!strncmp(none + 4, ",", 1)) {
			if (!strcmp(buf1, none))
				sprintf(buf2, "%s", none+5);
			else {
				none--;
				*none = '\0';
				len = sprintf(buf2, "%s", buf1);
				none += 5;
				sprintf(buf2 + len, "%s", none);
			}
		} else {
			none--;
			*none = '\0';
			sprintf(buf2, "%s", buf1);
		}
		if (iscsi_update_param_value(param, buf2) < 0)
			return -EINVAL;
	} else {
		snprintf(buf1, sizeof(buf1), "%s", param->value);
		none = strstr(buf1, NONE);
		if (none)
			goto out;
		strncat(buf1, ",", strlen(","));
		strncat(buf1, NONE, strlen(NONE));
		if (iscsi_update_param_value(param, buf1) < 0)
			return -EINVAL;
	}

out:
	a->authentication = authentication;
	pr_debug("%s iSCSI Authentication Methods for TPG: %hu.\n",
		a->authentication ? "Enforcing" : "Disabling", tpg->tpgt);

	return 0;
}

int iscsit_ta_login_timeout(
	struct iscsi_portal_group *tpg,
	u32 login_timeout)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if (login_timeout > TA_LOGIN_TIMEOUT_MAX) {
		pr_err("Requested Login Timeout %u larger than maximum"
			" %u\n", login_timeout, TA_LOGIN_TIMEOUT_MAX);
		return -EINVAL;
	} else if (login_timeout < TA_LOGIN_TIMEOUT_MIN) {
		pr_err("Requested Logout Timeout %u smaller than"
			" minimum %u\n", login_timeout, TA_LOGIN_TIMEOUT_MIN);
		return -EINVAL;
	}

	a->login_timeout = login_timeout;
	pr_debug("Set Logout Timeout to %u for Target Portal Group"
		" %hu\n", a->login_timeout, tpg->tpgt);

	return 0;
}

int iscsit_ta_netif_timeout(
	struct iscsi_portal_group *tpg,
	u32 netif_timeout)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if (netif_timeout > TA_NETIF_TIMEOUT_MAX) {
		pr_err("Requested Network Interface Timeout %u larger"
			" than maximum %u\n", netif_timeout,
				TA_NETIF_TIMEOUT_MAX);
		return -EINVAL;
	} else if (netif_timeout < TA_NETIF_TIMEOUT_MIN) {
		pr_err("Requested Network Interface Timeout %u smaller"
			" than minimum %u\n", netif_timeout,
				TA_NETIF_TIMEOUT_MIN);
		return -EINVAL;
	}

	a->netif_timeout = netif_timeout;
	pr_debug("Set Network Interface Timeout to %u for"
		" Target Portal Group %hu\n", a->netif_timeout, tpg->tpgt);

	return 0;
}

int iscsit_ta_generate_node_acls(
	struct iscsi_portal_group *tpg,
	u32 flag)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if ((flag != 0) && (flag != 1)) {
		pr_err("Illegal value %d\n", flag);
		return -EINVAL;
	}

	a->generate_node_acls = flag;
	pr_debug("iSCSI_TPG[%hu] - Generate Initiator Portal Group ACLs: %s\n",
		tpg->tpgt, (a->generate_node_acls) ? "Enabled" : "Disabled");

	if (flag == 1 && a->cache_dynamic_acls == 0) {
		pr_debug("Explicitly setting cache_dynamic_acls=1 when "
			"generate_node_acls=1\n");
		a->cache_dynamic_acls = 1;
	}

	return 0;
}

int iscsit_ta_default_cmdsn_depth(
	struct iscsi_portal_group *tpg,
	u32 tcq_depth)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if (tcq_depth > TA_DEFAULT_CMDSN_DEPTH_MAX) {
		pr_err("Requested Default Queue Depth: %u larger"
			" than maximum %u\n", tcq_depth,
				TA_DEFAULT_CMDSN_DEPTH_MAX);
		return -EINVAL;
	} else if (tcq_depth < TA_DEFAULT_CMDSN_DEPTH_MIN) {
		pr_err("Requested Default Queue Depth: %u smaller"
			" than minimum %u\n", tcq_depth,
				TA_DEFAULT_CMDSN_DEPTH_MIN);
		return -EINVAL;
	}

	a->default_cmdsn_depth = tcq_depth;
	pr_debug("iSCSI_TPG[%hu] - Set Default CmdSN TCQ Depth to %u\n",
		tpg->tpgt, a->default_cmdsn_depth);

	return 0;
}

int iscsit_ta_cache_dynamic_acls(
	struct iscsi_portal_group *tpg,
	u32 flag)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if ((flag != 0) && (flag != 1)) {
		pr_err("Illegal value %d\n", flag);
		return -EINVAL;
	}

	if (a->generate_node_acls == 1 && flag == 0) {
		pr_debug("Skipping cache_dynamic_acls=0 when"
			" generate_node_acls=1\n");
		return 0;
	}

	a->cache_dynamic_acls = flag;
	pr_debug("iSCSI_TPG[%hu] - Cache Dynamic Initiator Portal Group"
		" ACLs %s\n", tpg->tpgt, (a->cache_dynamic_acls) ?
		"Enabled" : "Disabled");

	return 0;
}

int iscsit_ta_demo_mode_write_protect(
	struct iscsi_portal_group *tpg,
	u32 flag)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if ((flag != 0) && (flag != 1)) {
		pr_err("Illegal value %d\n", flag);
		return -EINVAL;
	}

	a->demo_mode_write_protect = flag;
	pr_debug("iSCSI_TPG[%hu] - Demo Mode Write Protect bit: %s\n",
		tpg->tpgt, (a->demo_mode_write_protect) ? "ON" : "OFF");

	return 0;
}

int iscsit_ta_prod_mode_write_protect(
	struct iscsi_portal_group *tpg,
	u32 flag)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if ((flag != 0) && (flag != 1)) {
		pr_err("Illegal value %d\n", flag);
		return -EINVAL;
	}

	a->prod_mode_write_protect = flag;
	pr_debug("iSCSI_TPG[%hu] - Production Mode Write Protect bit:"
		" %s\n", tpg->tpgt, (a->prod_mode_write_protect) ?
		"ON" : "OFF");

	return 0;
}

int iscsit_ta_demo_mode_discovery(
	struct iscsi_portal_group *tpg,
	u32 flag)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if ((flag != 0) && (flag != 1)) {
		pr_err("Illegal value %d\n", flag);
		return -EINVAL;
	}

	a->demo_mode_discovery = flag;
	pr_debug("iSCSI_TPG[%hu] - Demo Mode Discovery bit:"
		" %s\n", tpg->tpgt, (a->demo_mode_discovery) ?
		"ON" : "OFF");

	return 0;
}

int iscsit_ta_default_erl(
	struct iscsi_portal_group *tpg,
	u32 default_erl)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if ((default_erl != 0) && (default_erl != 1) && (default_erl != 2)) {
		pr_err("Illegal value for default_erl: %u\n", default_erl);
		return -EINVAL;
	}

	a->default_erl = default_erl;
	pr_debug("iSCSI_TPG[%hu] - DefaultERL: %u\n", tpg->tpgt, a->default_erl);

	return 0;
}

int iscsit_ta_t10_pi(
	struct iscsi_portal_group *tpg,
	u32 flag)
{
	struct iscsi_tpg_attrib *a = &tpg->tpg_attrib;

	if ((flag != 0) && (flag != 1)) {
		pr_err("Illegal value %d\n", flag);
		return -EINVAL;
	}

	a->t10_pi = flag;
	pr_debug("iSCSI_TPG[%hu] - T10 Protection information bit:"
		" %s\n", tpg->tpgt, (a->t10_pi) ?
		"ON" : "OFF");

	return 0;
}
