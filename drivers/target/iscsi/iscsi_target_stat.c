/*******************************************************************************
 * Modern ConfigFS group context specific iSCSI statistics based on original
 * iscsi_target_mib.c code
 *
 * Copyright (c) 2011-2013 Datera, Inc.
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

#include <linux/configfs.h>
#include <linux/export.h>
#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>
#include <target/configfs_macros.h>

#include "iscsi_target_core.h"
#include "iscsi_target_parameters.h"
#include "iscsi_target_device.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target_stat.h"

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
CONFIGFS_EATTR_STRUCT(iscsi_stat_instance, iscsi_wwn_stat_grps);
#define ISCSI_STAT_INSTANCE_ATTR(_name, _mode)			\
static struct iscsi_stat_instance_attribute			\
			iscsi_stat_instance_##_name =		\
	__CONFIGFS_EATTR(_name, _mode,				\
	iscsi_stat_instance_show_attr_##_name,			\
	iscsi_stat_instance_store_attr_##_name);

#define ISCSI_STAT_INSTANCE_ATTR_RO(_name)			\
static struct iscsi_stat_instance_attribute			\
			iscsi_stat_instance_##_name =		\
	__CONFIGFS_EATTR_RO(_name,				\
	iscsi_stat_instance_show_attr_##_name);

static ssize_t iscsi_stat_instance_show_attr_inst(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_index);
}
ISCSI_STAT_INSTANCE_ATTR_RO(inst);

static ssize_t iscsi_stat_instance_show_attr_min_ver(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_DRAFT20_VERSION);
}
ISCSI_STAT_INSTANCE_ATTR_RO(min_ver);

static ssize_t iscsi_stat_instance_show_attr_max_ver(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_DRAFT20_VERSION);
}
ISCSI_STAT_INSTANCE_ATTR_RO(max_ver);

static ssize_t iscsi_stat_instance_show_attr_portals(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_num_tpg_nps);
}
ISCSI_STAT_INSTANCE_ATTR_RO(portals);

static ssize_t iscsi_stat_instance_show_attr_nodes(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_INST_NUM_NODES);
}
ISCSI_STAT_INSTANCE_ATTR_RO(nodes);

static ssize_t iscsi_stat_instance_show_attr_sessions(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_nsessions);
}
ISCSI_STAT_INSTANCE_ATTR_RO(sessions);

static ssize_t iscsi_stat_instance_show_attr_fail_sess(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;
	u32 sess_err_count;

	spin_lock_bh(&sess_err->lock);
	sess_err_count = (sess_err->digest_errors +
			  sess_err->cxn_timeout_errors +
			  sess_err->pdu_format_errors);
	spin_unlock_bh(&sess_err->lock);

	return snprintf(page, PAGE_SIZE, "%u\n", sess_err_count);
}
ISCSI_STAT_INSTANCE_ATTR_RO(fail_sess);

static ssize_t iscsi_stat_instance_show_attr_fail_type(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%u\n",
			sess_err->last_sess_failure_type);
}
ISCSI_STAT_INSTANCE_ATTR_RO(fail_type);

static ssize_t iscsi_stat_instance_show_attr_fail_rem_name(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%s\n",
			sess_err->last_sess_fail_rem_name[0] ?
			sess_err->last_sess_fail_rem_name : NONE);
}
ISCSI_STAT_INSTANCE_ATTR_RO(fail_rem_name);

static ssize_t iscsi_stat_instance_show_attr_disc_time(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_DISCONTINUITY_TIME);
}
ISCSI_STAT_INSTANCE_ATTR_RO(disc_time);

static ssize_t iscsi_stat_instance_show_attr_description(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", ISCSI_INST_DESCR);
}
ISCSI_STAT_INSTANCE_ATTR_RO(description);

static ssize_t iscsi_stat_instance_show_attr_vendor(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "Datera, Inc. iSCSI-Target\n");
}
ISCSI_STAT_INSTANCE_ATTR_RO(vendor);

static ssize_t iscsi_stat_instance_show_attr_version(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", ISCSIT_VERSION);
}
ISCSI_STAT_INSTANCE_ATTR_RO(version);

CONFIGFS_EATTR_OPS(iscsi_stat_instance, iscsi_wwn_stat_grps,
		iscsi_instance_group);

static struct configfs_attribute *iscsi_stat_instance_attrs[] = {
	&iscsi_stat_instance_inst.attr,
	&iscsi_stat_instance_min_ver.attr,
	&iscsi_stat_instance_max_ver.attr,
	&iscsi_stat_instance_portals.attr,
	&iscsi_stat_instance_nodes.attr,
	&iscsi_stat_instance_sessions.attr,
	&iscsi_stat_instance_fail_sess.attr,
	&iscsi_stat_instance_fail_type.attr,
	&iscsi_stat_instance_fail_rem_name.attr,
	&iscsi_stat_instance_disc_time.attr,
	&iscsi_stat_instance_description.attr,
	&iscsi_stat_instance_vendor.attr,
	&iscsi_stat_instance_version.attr,
	NULL,
};

static struct configfs_item_operations iscsi_stat_instance_item_ops = {
	.show_attribute		= iscsi_stat_instance_attr_show,
	.store_attribute	= iscsi_stat_instance_attr_store,
};

struct config_item_type iscsi_stat_instance_cit = {
	.ct_item_ops		= &iscsi_stat_instance_item_ops,
	.ct_attrs		= iscsi_stat_instance_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Instance Session Failure Stats Table
 */
CONFIGFS_EATTR_STRUCT(iscsi_stat_sess_err, iscsi_wwn_stat_grps);
#define ISCSI_STAT_SESS_ERR_ATTR(_name, _mode)			\
static struct iscsi_stat_sess_err_attribute			\
			iscsi_stat_sess_err_##_name =		\
	__CONFIGFS_EATTR(_name, _mode,				\
	iscsi_stat_sess_err_show_attr_##_name,			\
	iscsi_stat_sess_err_store_attr_##_name);

#define ISCSI_STAT_SESS_ERR_ATTR_RO(_name)			\
static struct iscsi_stat_sess_err_attribute			\
			iscsi_stat_sess_err_##_name =		\
	__CONFIGFS_EATTR_RO(_name,				\
	iscsi_stat_sess_err_show_attr_##_name);

static ssize_t iscsi_stat_sess_err_show_attr_inst(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_index);
}
ISCSI_STAT_SESS_ERR_ATTR_RO(inst);

static ssize_t iscsi_stat_sess_err_show_attr_digest_errors(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", sess_err->digest_errors);
}
ISCSI_STAT_SESS_ERR_ATTR_RO(digest_errors);

static ssize_t iscsi_stat_sess_err_show_attr_cxn_errors(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", sess_err->cxn_timeout_errors);
}
ISCSI_STAT_SESS_ERR_ATTR_RO(cxn_errors);

static ssize_t iscsi_stat_sess_err_show_attr_format_errors(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_sess_err_stats *sess_err = &tiqn->sess_err_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", sess_err->pdu_format_errors);
}
ISCSI_STAT_SESS_ERR_ATTR_RO(format_errors);

CONFIGFS_EATTR_OPS(iscsi_stat_sess_err, iscsi_wwn_stat_grps,
		iscsi_sess_err_group);

static struct configfs_attribute *iscsi_stat_sess_err_attrs[] = {
	&iscsi_stat_sess_err_inst.attr,
	&iscsi_stat_sess_err_digest_errors.attr,
	&iscsi_stat_sess_err_cxn_errors.attr,
	&iscsi_stat_sess_err_format_errors.attr,
	NULL,
};

static struct configfs_item_operations iscsi_stat_sess_err_item_ops = {
	.show_attribute		= iscsi_stat_sess_err_attr_show,
	.store_attribute	= iscsi_stat_sess_err_attr_store,
};

struct config_item_type iscsi_stat_sess_err_cit = {
	.ct_item_ops		= &iscsi_stat_sess_err_item_ops,
	.ct_attrs		= iscsi_stat_sess_err_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Target Attributes Table
 */
CONFIGFS_EATTR_STRUCT(iscsi_stat_tgt_attr, iscsi_wwn_stat_grps);
#define ISCSI_STAT_TGT_ATTR(_name, _mode)			\
static struct iscsi_stat_tgt_attr_attribute			\
			iscsi_stat_tgt_attr_##_name =		\
	__CONFIGFS_EATTR(_name, _mode,				\
	iscsi_stat_tgt-attr_show_attr_##_name,			\
	iscsi_stat_tgt_attr_store_attr_##_name);

#define ISCSI_STAT_TGT_ATTR_RO(_name)				\
static struct iscsi_stat_tgt_attr_attribute			\
			iscsi_stat_tgt_attr_##_name =		\
	__CONFIGFS_EATTR_RO(_name,				\
	iscsi_stat_tgt_attr_show_attr_##_name);

static ssize_t iscsi_stat_tgt_attr_show_attr_inst(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_index);
}
ISCSI_STAT_TGT_ATTR_RO(inst);

static ssize_t iscsi_stat_tgt_attr_show_attr_indx(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_NODE_INDEX);
}
ISCSI_STAT_TGT_ATTR_RO(indx);

static ssize_t iscsi_stat_tgt_attr_show_attr_login_fails(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	u32 fail_count;

	spin_lock(&lstat->lock);
	fail_count = (lstat->redirects + lstat->authorize_fails +
			lstat->authenticate_fails + lstat->negotiate_fails +
			lstat->other_fails);
	spin_unlock(&lstat->lock);

	return snprintf(page, PAGE_SIZE, "%u\n", fail_count);
}
ISCSI_STAT_TGT_ATTR_RO(login_fails);

static ssize_t iscsi_stat_tgt_attr_show_attr_last_fail_time(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	u32 last_fail_time;

	spin_lock(&lstat->lock);
	last_fail_time = lstat->last_fail_time ?
			(u32)(((u32)lstat->last_fail_time -
				INITIAL_JIFFIES) * 100 / HZ) : 0;
	spin_unlock(&lstat->lock);

	return snprintf(page, PAGE_SIZE, "%u\n", last_fail_time);
}
ISCSI_STAT_TGT_ATTR_RO(last_fail_time);

static ssize_t iscsi_stat_tgt_attr_show_attr_last_fail_type(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	u32 last_fail_type;

	spin_lock(&lstat->lock);
	last_fail_type = lstat->last_fail_type;
	spin_unlock(&lstat->lock);

	return snprintf(page, PAGE_SIZE, "%u\n", last_fail_type);
}
ISCSI_STAT_TGT_ATTR_RO(last_fail_type);

static ssize_t iscsi_stat_tgt_attr_show_attr_fail_intr_name(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	unsigned char buf[224];

	spin_lock(&lstat->lock);
	snprintf(buf, 224, "%s", lstat->last_intr_fail_name[0] ?
				lstat->last_intr_fail_name : NONE);
	spin_unlock(&lstat->lock);

	return snprintf(page, PAGE_SIZE, "%s\n", buf);
}
ISCSI_STAT_TGT_ATTR_RO(fail_intr_name);

static ssize_t iscsi_stat_tgt_attr_show_attr_fail_intr_addr_type(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
			struct iscsi_tiqn, tiqn_stat_grps);
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
ISCSI_STAT_TGT_ATTR_RO(fail_intr_addr_type);

static ssize_t iscsi_stat_tgt_attr_show_attr_fail_intr_addr(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
			struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	int ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%s\n", lstat->last_intr_fail_ip_addr);
	spin_unlock(&lstat->lock);

	return ret;
}
ISCSI_STAT_TGT_ATTR_RO(fail_intr_addr);

CONFIGFS_EATTR_OPS(iscsi_stat_tgt_attr, iscsi_wwn_stat_grps,
		iscsi_tgt_attr_group);

static struct configfs_attribute *iscsi_stat_tgt_attr_attrs[] = {
	&iscsi_stat_tgt_attr_inst.attr,
	&iscsi_stat_tgt_attr_indx.attr,
	&iscsi_stat_tgt_attr_login_fails.attr,
	&iscsi_stat_tgt_attr_last_fail_time.attr,
	&iscsi_stat_tgt_attr_last_fail_type.attr,
	&iscsi_stat_tgt_attr_fail_intr_name.attr,
	&iscsi_stat_tgt_attr_fail_intr_addr_type.attr,
	&iscsi_stat_tgt_attr_fail_intr_addr.attr,
	NULL,
};

static struct configfs_item_operations iscsi_stat_tgt_attr_item_ops = {
	.show_attribute		= iscsi_stat_tgt_attr_attr_show,
	.store_attribute	= iscsi_stat_tgt_attr_attr_store,
};

struct config_item_type iscsi_stat_tgt_attr_cit = {
	.ct_item_ops		= &iscsi_stat_tgt_attr_item_ops,
	.ct_attrs		= iscsi_stat_tgt_attr_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Target Login Stats Table
 */
CONFIGFS_EATTR_STRUCT(iscsi_stat_login, iscsi_wwn_stat_grps);
#define ISCSI_STAT_LOGIN(_name, _mode)				\
static struct iscsi_stat_login_attribute			\
			iscsi_stat_login_##_name =		\
	__CONFIGFS_EATTR(_name, _mode,				\
	iscsi_stat_login_show_attr_##_name,			\
	iscsi_stat_login_store_attr_##_name);

#define ISCSI_STAT_LOGIN_RO(_name)				\
static struct iscsi_stat_login_attribute			\
			iscsi_stat_login_##_name =		\
	__CONFIGFS_EATTR_RO(_name,				\
	iscsi_stat_login_show_attr_##_name);

static ssize_t iscsi_stat_login_show_attr_inst(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_index);
}
ISCSI_STAT_LOGIN_RO(inst);

static ssize_t iscsi_stat_login_show_attr_indx(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_NODE_INDEX);
}
ISCSI_STAT_LOGIN_RO(indx);

static ssize_t iscsi_stat_login_show_attr_accepts(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->accepts);
	spin_unlock(&lstat->lock);

	return ret;
}
ISCSI_STAT_LOGIN_RO(accepts);

static ssize_t iscsi_stat_login_show_attr_other_fails(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->other_fails);
	spin_unlock(&lstat->lock);

	return ret;
}
ISCSI_STAT_LOGIN_RO(other_fails);

static ssize_t iscsi_stat_login_show_attr_redirects(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->redirects);
	spin_unlock(&lstat->lock);

	return ret;
}
ISCSI_STAT_LOGIN_RO(redirects);

static ssize_t iscsi_stat_login_show_attr_authorize_fails(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->authorize_fails);
	spin_unlock(&lstat->lock);

	return ret;
}
ISCSI_STAT_LOGIN_RO(authorize_fails);

static ssize_t iscsi_stat_login_show_attr_authenticate_fails(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->authenticate_fails);
	spin_unlock(&lstat->lock);

	return ret;
}
ISCSI_STAT_LOGIN_RO(authenticate_fails);

static ssize_t iscsi_stat_login_show_attr_negotiate_fails(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
				struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_login_stats *lstat = &tiqn->login_stats;
	ssize_t ret;

	spin_lock(&lstat->lock);
	ret = snprintf(page, PAGE_SIZE, "%u\n", lstat->negotiate_fails);
	spin_unlock(&lstat->lock);

	return ret;
}
ISCSI_STAT_LOGIN_RO(negotiate_fails);

CONFIGFS_EATTR_OPS(iscsi_stat_login, iscsi_wwn_stat_grps,
		iscsi_login_stats_group);

static struct configfs_attribute *iscsi_stat_login_stats_attrs[] = {
	&iscsi_stat_login_inst.attr,
	&iscsi_stat_login_indx.attr,
	&iscsi_stat_login_accepts.attr,
	&iscsi_stat_login_other_fails.attr,
	&iscsi_stat_login_redirects.attr,
	&iscsi_stat_login_authorize_fails.attr,
	&iscsi_stat_login_authenticate_fails.attr,
	&iscsi_stat_login_negotiate_fails.attr,
	NULL,
};

static struct configfs_item_operations iscsi_stat_login_stats_item_ops = {
	.show_attribute		= iscsi_stat_login_attr_show,
	.store_attribute	= iscsi_stat_login_attr_store,
};

struct config_item_type iscsi_stat_login_cit = {
	.ct_item_ops		= &iscsi_stat_login_stats_item_ops,
	.ct_attrs		= iscsi_stat_login_stats_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Target Logout Stats Table
 */

CONFIGFS_EATTR_STRUCT(iscsi_stat_logout, iscsi_wwn_stat_grps);
#define ISCSI_STAT_LOGOUT(_name, _mode)				\
static struct iscsi_stat_logout_attribute			\
			iscsi_stat_logout_##_name =		\
	__CONFIGFS_EATTR(_name, _mode,				\
	iscsi_stat_logout_show_attr_##_name,			\
	iscsi_stat_logout_store_attr_##_name);

#define ISCSI_STAT_LOGOUT_RO(_name)				\
static struct iscsi_stat_logout_attribute			\
			iscsi_stat_logout_##_name =		\
	__CONFIGFS_EATTR_RO(_name,				\
	iscsi_stat_logout_show_attr_##_name);

static ssize_t iscsi_stat_logout_show_attr_inst(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
			struct iscsi_tiqn, tiqn_stat_grps);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_index);
}
ISCSI_STAT_LOGOUT_RO(inst);

static ssize_t iscsi_stat_logout_show_attr_indx(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", ISCSI_NODE_INDEX);
}
ISCSI_STAT_LOGOUT_RO(indx);

static ssize_t iscsi_stat_logout_show_attr_normal_logouts(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
			struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_logout_stats *lstats = &tiqn->logout_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", lstats->normal_logouts);
}
ISCSI_STAT_LOGOUT_RO(normal_logouts);

static ssize_t iscsi_stat_logout_show_attr_abnormal_logouts(
	struct iscsi_wwn_stat_grps *igrps, char *page)
{
	struct iscsi_tiqn *tiqn = container_of(igrps,
			struct iscsi_tiqn, tiqn_stat_grps);
	struct iscsi_logout_stats *lstats = &tiqn->logout_stats;

	return snprintf(page, PAGE_SIZE, "%u\n", lstats->abnormal_logouts);
}
ISCSI_STAT_LOGOUT_RO(abnormal_logouts);

CONFIGFS_EATTR_OPS(iscsi_stat_logout, iscsi_wwn_stat_grps,
		iscsi_logout_stats_group);

static struct configfs_attribute *iscsi_stat_logout_stats_attrs[] = {
	&iscsi_stat_logout_inst.attr,
	&iscsi_stat_logout_indx.attr,
	&iscsi_stat_logout_normal_logouts.attr,
	&iscsi_stat_logout_abnormal_logouts.attr,
	NULL,
};

static struct configfs_item_operations iscsi_stat_logout_stats_item_ops = {
	.show_attribute		= iscsi_stat_logout_attr_show,
	.store_attribute	= iscsi_stat_logout_attr_store,
};

struct config_item_type iscsi_stat_logout_cit = {
	.ct_item_ops		= &iscsi_stat_logout_stats_item_ops,
	.ct_attrs		= iscsi_stat_logout_stats_attrs,
	.ct_owner		= THIS_MODULE,
};

/*
 * Session Stats Table
 */

CONFIGFS_EATTR_STRUCT(iscsi_stat_sess, iscsi_node_stat_grps);
#define ISCSI_STAT_SESS(_name, _mode)				\
static struct iscsi_stat_sess_attribute				\
			iscsi_stat_sess_##_name =		\
	__CONFIGFS_EATTR(_name, _mode,				\
	iscsi_stat_sess_show_attr_##_name,			\
	iscsi_stat_sess_store_attr_##_name);

#define ISCSI_STAT_SESS_RO(_name)				\
static struct iscsi_stat_sess_attribute				\
			iscsi_stat_sess_##_name =		\
	__CONFIGFS_EATTR_RO(_name,				\
	iscsi_stat_sess_show_attr_##_name);

static ssize_t iscsi_stat_sess_show_attr_inst(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
	struct se_wwn *wwn = acl->se_node_acl.se_tpg->se_tpg_wwn;
	struct iscsi_tiqn *tiqn = container_of(wwn,
			struct iscsi_tiqn, tiqn_wwn);

	return snprintf(page, PAGE_SIZE, "%u\n", tiqn->tiqn_index);
}
ISCSI_STAT_SESS_RO(inst);

static ssize_t iscsi_stat_sess_show_attr_node(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
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
ISCSI_STAT_SESS_RO(node);

static ssize_t iscsi_stat_sess_show_attr_indx(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
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
ISCSI_STAT_SESS_RO(indx);

static ssize_t iscsi_stat_sess_show_attr_cmd_pdus(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%u\n", sess->cmd_pdus);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}
ISCSI_STAT_SESS_RO(cmd_pdus);

static ssize_t iscsi_stat_sess_show_attr_rsp_pdus(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%u\n", sess->rsp_pdus);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}
ISCSI_STAT_SESS_RO(rsp_pdus);

static ssize_t iscsi_stat_sess_show_attr_txdata_octs(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%llu\n",
				(unsigned long long)sess->tx_data_octets);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}
ISCSI_STAT_SESS_RO(txdata_octs);

static ssize_t iscsi_stat_sess_show_attr_rxdata_octs(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
	struct se_node_acl *se_nacl = &acl->se_node_acl;
	struct iscsi_session *sess;
	struct se_session *se_sess;
	ssize_t ret = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (se_sess) {
		sess = se_sess->fabric_sess_ptr;
		if (sess)
			ret = snprintf(page, PAGE_SIZE, "%llu\n",
				(unsigned long long)sess->rx_data_octets);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}
ISCSI_STAT_SESS_RO(rxdata_octs);

static ssize_t iscsi_stat_sess_show_attr_conn_digest_errors(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
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
					sess->conn_digest_errors);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}
ISCSI_STAT_SESS_RO(conn_digest_errors);

static ssize_t iscsi_stat_sess_show_attr_conn_timeout_errors(
	struct iscsi_node_stat_grps *igrps, char *page)
{
	struct iscsi_node_acl *acl = container_of(igrps,
			struct iscsi_node_acl, node_stat_grps);
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
					sess->conn_timeout_errors);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return ret;
}
ISCSI_STAT_SESS_RO(conn_timeout_errors);

CONFIGFS_EATTR_OPS(iscsi_stat_sess, iscsi_node_stat_grps,
		iscsi_sess_stats_group);

static struct configfs_attribute *iscsi_stat_sess_stats_attrs[] = {
	&iscsi_stat_sess_inst.attr,
	&iscsi_stat_sess_node.attr,
	&iscsi_stat_sess_indx.attr,
	&iscsi_stat_sess_cmd_pdus.attr,
	&iscsi_stat_sess_rsp_pdus.attr,
	&iscsi_stat_sess_txdata_octs.attr,
	&iscsi_stat_sess_rxdata_octs.attr,
	&iscsi_stat_sess_conn_digest_errors.attr,
	&iscsi_stat_sess_conn_timeout_errors.attr,
	NULL,
};

static struct configfs_item_operations iscsi_stat_sess_stats_item_ops = {
	.show_attribute		= iscsi_stat_sess_attr_show,
	.store_attribute	= iscsi_stat_sess_attr_store,
};

struct config_item_type iscsi_stat_sess_cit = {
	.ct_item_ops		= &iscsi_stat_sess_stats_item_ops,
	.ct_attrs		= iscsi_stat_sess_stats_attrs,
	.ct_owner		= THIS_MODULE,
};
