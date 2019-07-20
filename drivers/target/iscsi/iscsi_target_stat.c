// SPDX-License-Identifier: GPL-2.0-or-later
/*******************************************************************************
 * Modern ConfigFS group context specific iSCSI statistics based on original
 * iscsi_target_mib.c code
 *
 * Copyright (c) 2011-2013 Datera, Inc.
 *
 * Author: Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 ******************************************************************************/

#include <linux/configfs.h>
#include <linux/export.h>
#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>

#include <target/iscsi/iscsi_target_core.h>
#include "iscsi_target_parameters.h"
#include "iscsi_target_device.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include <target/iscsi/iscsi_target_stat.h>

#ifndef INITIAL_JIFFIES
#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))
#endif

/* Instance Attributes Table */
#define ISCSI_INST_NUM_NODES		1
#define ISCSI_INST_DESCR		"Storage Engine Target"
#define ISCSI_INST_LAST_FAILURE_TYPE	0
#define ISCSI_DISCONTINUITY_TIME	0

#define ISCSI_NODE_INDEX		1

#define ISPRINT(a)   ((a >= ' ') && (a <= '~'))

/****************************************************************************
 * iSCSI MIB Tables
 ****************************************************************************/
/*
 * Instance Attributes Table
 */
static struct iscsi_tiqn *iscsi_instance_tiqn(struct config_item *item)
{
	struct iscsi_wwn_stat_grps *igrps = container_of(to_config_group(item),
			struct iscsi_wwn_stat_grps, iscsi_instance_group);
	return container_of(igrps, struct iscsi_tiqn, tiqn_stat_grps);
}

static ssize_t iscsi_stat_instance_inst_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n",
			iscsi_instance_tiqn(item)->tiqn_index);
}

static ssize_t iscsi_stat_instance_min_ver_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_DRAFT20_VERSION);
}

static ssize_t iscsi_stat_instance_max_ver_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_DRAFT20_VERSION);
}

static ssize_t iscsi_stat_instance_portals_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n",
			iscsi_instance_tiqn(item)->tiqn_num_tpg_nps);
}

static ssize_t iscsi_stat_instance_nodes_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_INST_NUM_NODES);
}

static ssize_t iscsi_stat_instance_sessions_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n",
		iscsi_instance_tiqn(item)->tiqn_nsessions);
}

static ssize_t iscsi_stat_instance_fail_sess_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_instance_tiqn(item);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;
	u32 sess_err_count;

	spin_lock_bh(&sess_err->lock);
	sess_err_count = (sess_err->digest_errors +
			  sess_err->cxn_timeout_errors +
			  sess_err->pdu_format_errors);
	spin_unlock_bh(&sess_err->lock);

	return snprintf(page, PAGE_SIZE, "%u\n", sess_err_count);
}

static ssize_t iscsi_stat_instance_fail_type_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_instance_tiqn(item);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%u\n",
			sess_err->last_sess_failure_type);
}

static ssize_t iscsi_stat_instance_fail_rem_name_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_instance_tiqn(item);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%s\n",
			sess_err->last_sess_fail_rem_name[0] ?
			sess_err->last_sess_fail_rem_name : NONE);
}

static ssize_t iscsi_stat_instance_disc_time_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_DISCONTINUITY_TIME);
}

static ssize_t iscsi_stat_instance_description_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", ISCSI_INST_DESCR);
}

static ssize_t iscsi_stat_instance_vendor_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "Datera, Inc. iSCSI-Target\n");
}

static ssize_t iscsi_stat_instance_version_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", ISCSIT_VERSION);
}

CONFIGFS_ATTR_RO(iscsi_stat_instance_, inst);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, min_ver);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, max_ver);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, portals);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, nodes);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, sessions);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, fail_sess);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, fail_type);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, fail_rem_name);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, disc_time);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, description);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, vendor);
CONFIGFS_ATTR_RO(iscsi_stat_instance_, version);

static struct configfs_attribute *iscsi_stat_instance_attrs[] = {
	&iscsi_stat_instance_attr_inst,
	&iscsi_stat_instance_attr_min_ver,
	&iscsi_stat_instance_attr_max_ver,
	&iscsi_stat_instance_attr_portals,
	&iscsi_stat_instance_attr_nodes,
	&iscsi_stat_instance_attr_sessions,
	&iscsi_stat_instance_attr_fail_sess,
	&iscsi_stat_instance_attr_fail_type,
	&iscsi_stat_instance_attr_fail_rem_name,
	&iscsi_stat_instance_attr_disc_time,
	&iscsi_stat_instance_attr_description,
	&iscsi_stat_instance_attr_vendor,
	&iscsi_stat_instance_attr_version,
	NULL,
};

const struct config_item_type iscsi_stat_instance_cit = {
	.ct_attrs		= iscsi_stat_instance_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Instance Session Failure Stats Table
 */
static struct iscsi_tiqn *iscsi_sess_err_tiqn(struct config_item *item)
{
	struct iscsi_wwn_stat_grps *igrps = container_of(to_config_group(item),
			struct iscsi_wwn_stat_grps, iscsi_sess_err_group);
	return container_of(igrps, struct iscsi_tiqn, tiqn_stat_grps);
}

static ssize_t iscsi_stat_sess_err_inst_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n",
		iscsi_sess_err_tiqn(item)->tiqn_index);
}

static ssize_t iscsi_stat_sess_err_digest_errors_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_sess_err_tiqn(item);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", sess_err->digest_errors);
}

static ssize_t iscsi_stat_sess_err_cxn_errors_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_sess_err_tiqn(item);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", sess_err->cxn_timeout_errors);
}

static ssize_t iscsi_stat_sess_err_format_errors_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_sess_err_tiqn(item);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", sess_err->pdu_format_errors);
}

CONFIGFS_ATTR_RO(iscsi_stat_sess_err_, inst);
CONFIGFS_ATTR_RO(iscsi_stat_sess_err_, digest_errors);
CONFIGFS_ATTR_RO(iscsi_stat_sess_err_, cxn_errors);
CONFIGFS_ATTR_RO(iscsi_stat_sess_err_, format_errors);

static struct configfs_attribute *iscsi_stat_sess_err_attrs[] = {
	&iscsi_stat_sess_err_attr_inst,
	&iscsi_stat_sess_err_attr_digest_errors,
	&iscsi_stat_sess_err_attr_cxn_errors,
	&iscsi_stat_sess_err_attr_format_errors,
	NULL,
};

const struct config_item_type iscsi_stat_sess_err_cit = {
	.ct_attrs		= iscsi_stat_sess_err_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Target Attributes Table
 */
static struct iscsi_tiqn *iscsi_tgt_attr_tiqn(struct config_item *item)
{
	struct iscsi_wwn_stat_grps *igrps = container_of(to_config_group(item),
			struct iscsi_wwn_stat_grps, iscsi_tgt_attr_group);
	return container_of(igrps, struct iscsi_tiqn, tiqn_stat_grps);
}

static ssize_t iscsi_stat_tgt_attr_inst_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n",
			iscsi_tgt_attr_tiqn(item)->tiqn_index);
}

static ssize_t iscsi_stat_tgt_attr_indx_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_NODE_INDEX);
}

static ssize_t iscsi_stat_tgt_attr_login_fails_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_tgt_attr_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	u32 fail_count;

	spin_lock(&lstat->lock);
	fail_count = (lstat->redirects + lstat->authorize_fails +
			lstat->authenticate_fails + lstat->negotiate_fails +
			lstat->other_fails);
	spin_unlock(&lstat->lock);

	return snprintf(page, PAGE_SIZE, "%u\n", fail_count);
}

static ssize_t iscsi_stat_tgt_attr_last_fail_time_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_tgt_attr_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	u32 last_fail_time;

	spin_lock(&lstat->lock);
	last_fail_time = lstat->last_fail_time ?
			(u32)(((u32)lstat->last_fail_time -
				INITIAL_JIFFIES) * 100 / HZ) : 0;
	spin_unlock(&lstat->lock);

	return snprintf(page, PAGE_SIZE, "%u\n", last_fail_time);
}

static ssize_t iscsi_stat_tgt_attr_last_fail_type_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_tgt_attr_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	u32 last_fail_type;

	spin_lock(&lstat->lock);
	last_fail_type = lstat->last_fail_type;
	spin_unlock(&lstat->lock);

	return snprintf(page, PAGE_SIZE, "%u\n", last_fail_type);
}

static ssize_t iscsi_stat_tgt_attr_fail_intr_name_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_tgt_attr_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	unsigned char buf[ISCSI_IQN_LEN];

	spin_lock(&lstat->lock);
	snprintf(buf, ISCSI_IQN_LEN, "%s", lstat->last_intr_fail_name[0] ?
				lstat->last_intr_fail_name : NONE);
	spin_unlock(&lstat->lock);

	return snprintf(page, PAGE_SIZE, "%s\n", buf);
}

static ssize_t iscsi_stat_tgt_attr_fail_intr_addr_type_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_tgt_attr_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	int ret;

	spin_lock(&lstat->lock);
	if (lstat->last_intr_fail_ip_family == AF_INET6)
		ret = snprintf(page, PAGE_SIZE, "ipv6\n");
	else
		ret = snprintf(page, PAGE_SIZE, "ipv4\n");
	spin_unlock(&lstat->lock);

	return ret;
}

static ssize_t iscsi_stat_tgt_attr_fail_intr_addr_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_tgt_attr_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	int ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%pISc\n", &lstat->last_intr_fail_sockaddr);
	spin_unlock(&lstat->lock);

	return ret;
}

CONFIGFS_ATTR_RO(iscsi_stat_tgt_attr_, inst);
CONFIGFS_ATTR_RO(iscsi_stat_tgt_attr_, indx);
CONFIGFS_ATTR_RO(iscsi_stat_tgt_attr_, login_fails);
CONFIGFS_ATTR_RO(iscsi_stat_tgt_attr_, last_fail_time);
CONFIGFS_ATTR_RO(iscsi_stat_tgt_attr_, last_fail_type);
CONFIGFS_ATTR_RO(iscsi_stat_tgt_attr_, fail_intr_name);
CONFIGFS_ATTR_RO(iscsi_stat_tgt_attr_, fail_intr_addr_type);
CONFIGFS_ATTR_RO(iscsi_stat_tgt_attr_, fail_intr_addr);

static struct configfs_attribute *iscsi_stat_tgt_attr_attrs[] = {
	&iscsi_stat_tgt_attr_attr_inst,
	&iscsi_stat_tgt_attr_attr_indx,
	&iscsi_stat_tgt_attr_attr_login_fails,
	&iscsi_stat_tgt_attr_attr_last_fail_time,
	&iscsi_stat_tgt_attr_attr_last_fail_type,
	&iscsi_stat_tgt_attr_attr_fail_intr_name,
	&iscsi_stat_tgt_attr_attr_fail_intr_addr_type,
	&iscsi_stat_tgt_attr_attr_fail_intr_addr,
	NULL,
};

const struct config_item_type iscsi_stat_tgt_attr_cit = {
	.ct_attrs		= iscsi_stat_tgt_attr_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Target Login Stats Table
 */
static struct iscsi_tiqn *iscsi_login_stat_tiqn(struct config_item *item)
{
	struct iscsi_wwn_stat_grps *igrps = container_of(to_config_group(item),
			struct iscsi_wwn_stat_grps, iscsi_login_stats_group);
	return container_of(igrps, struct iscsi_tiqn, tiqn_stat_grps);
}

static ssize_t iscsi_stat_login_inst_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n",
		iscsi_login_stat_tiqn(item)->tiqn_index);
}

static ssize_t iscsi_stat_login_indx_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_NODE_INDEX);
}

static ssize_t iscsi_stat_login_accepts_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_login_stat_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->accepts);
	spin_unlock(&lstat->lock);

	return ret;
}

static ssize_t iscsi_stat_login_other_fails_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_login_stat_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->other_fails);
	spin_unlock(&lstat->lock);

	return ret;
}

static ssize_t iscsi_stat_login_redirects_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_login_stat_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->redirects);
	spin_unlock(&lstat->lock);

	return ret;
}

static ssize_t iscsi_stat_login_authorize_fails_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_login_stat_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->authorize_fails);
	spin_unlock(&lstat->lock);

	return ret;
}

static ssize_t iscsi_stat_login_authenticate_fails_show(
		struct config_item *item, char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_login_stat_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->authenticate_fails);
	spin_unlock(&lstat->lock);

	return ret;
}

static ssize_t iscsi_stat_login_negotiate_fails_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_login_stat_tiqn(item);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->negotiate_fails);
	spin_unlock(&lstat->lock);

	return ret;
}

CONFIGFS_ATTR_RO(iscsi_stat_login_, inst);
CONFIGFS_ATTR_RO(iscsi_stat_login_, indx);
CONFIGFS_ATTR_RO(iscsi_stat_login_, accepts);
CONFIGFS_ATTR_RO(iscsi_stat_login_, other_fails);
CONFIGFS_ATTR_RO(iscsi_stat_login_, redirects);
CONFIGFS_ATTR_RO(iscsi_stat_login_, authorize_fails);
CONFIGFS_ATTR_RO(iscsi_stat_login_, authenticate_fails);
CONFIGFS_ATTR_RO(iscsi_stat_login_, negotiate_fails);

static struct configfs_attribute *iscsi_stat_login_stats_attrs[] = {
	&iscsi_stat_login_attr_inst,
	&iscsi_stat_login_attr_indx,
	&iscsi_stat_login_attr_accepts,
	&iscsi_stat_login_attr_other_fails,
	&iscsi_stat_login_attr_redirects,
	&iscsi_stat_login_attr_authorize_fails,
	&iscsi_stat_login_attr_authenticate_fails,
	&iscsi_stat_login_attr_negotiate_fails,
	NULL,
};

const struct config_item_type iscsi_stat_login_cit = {
	.ct_attrs		= iscsi_stat_login_stats_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Target Logout Stats Table
 */
static struct iscsi_tiqn *iscsi_logout_stat_tiqn(struct config_item *item)
{
	struct iscsi_wwn_stat_grps *igrps = container_of(to_config_group(item),
			struct iscsi_wwn_stat_grps, iscsi_logout_stats_group);
	return container_of(igrps, struct iscsi_tiqn, tiqn_stat_grps);
}

static ssize_t iscsi_stat_logout_inst_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n",
		iscsi_logout_stat_tiqn(item)->tiqn_index);
}

static ssize_t iscsi_stat_logout_indx_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_NODE_INDEX);
}

static ssize_t iscsi_stat_logout_normal_logouts_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_logout_stat_tiqn(item);
	struct iscsi_logout_stats *lstats = &tiqn->logout_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", lstats->normal_logouts);
}

static ssize_t iscsi_stat_logout_abnormal_logouts_show(struct config_item *item,
		char *page)
{
	struct iscsi_tiqn *tiqn = iscsi_logout_stat_tiqn(item);
	struct iscsi_logout_stats *lstats = &tiqn->logout_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", lstats->abnormal_logouts);
}

CONFIGFS_ATTR_RO(iscsi_stat_logout_, inst);
CONFIGFS_ATTR_RO(iscsi_stat_logout_, indx);
CONFIGFS_ATTR_RO(iscsi_stat_logout_, normal_logouts);
CONFIGFS_ATTR_RO(iscsi_stat_logout_, abnormal_logouts);

static struct configfs_attribute *iscsi_stat_logout_stats_attrs[] = {
	&iscsi_stat_logout_attr_inst,
	&iscsi_stat_logout_attr_indx,
	&iscsi_stat_logout_attr_normal_logouts,
	&iscsi_stat_logout_attr_abnormal_logouts,
	NULL,
};

const struct config_item_type iscsi_stat_logout_cit = {
	.ct_attrs		= iscsi_stat_logout_stats_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Session Stats Table
 */
static struct iscsi_node_acl *iscsi_stat_nacl(struct config_item *item)
{
	struct iscsi_node_stat_grps *igrps = container_of(to_config_group(item),
			struct iscsi_node_stat_grps, iscsi_sess_stats_group);
	return container_of(igrps, struct iscsi_node_acl, node_stat_grps);
}

static ssize_t iscsi_stat_sess_inst_show(struct config_item *item, char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_wwn *wwn = acl->se_node_acl.se_tpg->se_tpg_wwn;
	struct iscsi_tiqn *tiqn = container_of(wwn,
			struct iscsi_tiqn, tiqn_wwn);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_index);
}

static ssize_t iscsi_stat_sess_node_show(struct config_item *item, char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%u\n",
				sess->sess_ops->SessionType ? 0 : ISCSI_NODE_INDEX);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}

static ssize_t iscsi_stat_sess_indx_show(struct config_item *item, char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%u\n",
					sess->session_index);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}

static ssize_t iscsi_stat_sess_cmd_pdus_show(struct config_item *item,
		char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%lu\n",
				       atomic_long_read(&sess->cmd_pdus));
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}

static ssize_t iscsi_stat_sess_rsp_pdus_show(struct config_item *item,
		char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%lu\n",
				       atomic_long_read(&sess->rsp_pdus));
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}

static ssize_t iscsi_stat_sess_txdata_octs_show(struct config_item *item,
		char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%lu\n",
				       atomic_long_read(&sess->tx_data_octets));
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}

static ssize_t iscsi_stat_sess_rxdata_octs_show(struct config_item *item,
		char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%lu\n",
				       atomic_long_read(&sess->rx_data_octets));
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}

static ssize_t iscsi_stat_sess_conn_digest_errors_show(struct config_item *item,
		char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%lu\n",
				       atomic_long_read(&sess->conn_digest_errors));
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}

static ssize_t iscsi_stat_sess_conn_timeout_errors_show(
		struct config_item *item, char *page)
{
	struct iscsi_node_acl *acl = iscsi_stat_nacl(item);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%lu\n",
				       atomic_long_read(&sess->conn_timeout_errors));
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}

CONFIGFS_ATTR_RO(iscsi_stat_sess_, inst);
CONFIGFS_ATTR_RO(iscsi_stat_sess_, node);
CONFIGFS_ATTR_RO(iscsi_stat_sess_, indx);
CONFIGFS_ATTR_RO(iscsi_stat_sess_, cmd_pdus);
CONFIGFS_ATTR_RO(iscsi_stat_sess_, rsp_pdus);
CONFIGFS_ATTR_RO(iscsi_stat_sess_, txdata_octs);
CONFIGFS_ATTR_RO(iscsi_stat_sess_, rxdata_octs);
CONFIGFS_ATTR_RO(iscsi_stat_sess_, conn_digest_errors);
CONFIGFS_ATTR_RO(iscsi_stat_sess_, conn_timeout_errors);

static struct configfs_attribute *iscsi_stat_sess_stats_attrs[] = {
	&iscsi_stat_sess_attr_inst,
	&iscsi_stat_sess_attr_node,
	&iscsi_stat_sess_attr_indx,
	&iscsi_stat_sess_attr_cmd_pdus,
	&iscsi_stat_sess_attr_rsp_pdus,
	&iscsi_stat_sess_attr_txdata_octs,
	&iscsi_stat_sess_attr_rxdata_octs,
	&iscsi_stat_sess_attr_conn_digest_errors,
	&iscsi_stat_sess_attr_conn_timeout_errors,
	NULL,
};

const struct config_item_type iscsi_stat_sess_cit = {
	.ct_attrs		= iscsi_stat_sess_stats_attrs,
	.ct_owner		= THIS_MODULE,
};
