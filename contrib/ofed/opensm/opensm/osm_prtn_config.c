/*
 * Copyright (c) 2006-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2012-2015 Mellanox Technologies LTD. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *    Implementation of opensm partition management configuration
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <iba/ib_types.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_PRTN_CONFIG_C
#include <opensm/osm_base.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_log.h>
#include <arpa/inet.h>
#include <sys/socket.h>

typedef enum {
	LIMITED,
	FULL,
	BOTH
} membership_t;

const ib_gid_t osm_ipoib_broadcast_mgid = {
	{
	 0xff,			/*  multicast field */
	 0x12,			/*  non-permanent bit, link local scope */
	 0x40, 0x1b,		/*  IPv4 signature */
	 0xff, 0xff,		/*  16 bits of P_Key (to be filled in) */
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/*  48 bits of zeros */
	 0xff, 0xff, 0xff, 0xff,	/*  32 bit IPv4 broadcast address */
	 },
};

struct group_flags {
	unsigned mtu, rate, sl, scope_mask;
	uint32_t Q_Key;
	uint8_t TClass;
	uint32_t FlowLabel;
};

struct precreate_mgroup {
	ib_gid_t mgid;
	struct group_flags flags;
};

struct part_conf {
	osm_log_t *p_log;
	osm_subn_t *p_subn;
	osm_prtn_t *p_prtn;
	unsigned is_ipoib;
	struct group_flags flags;
	membership_t membership;
	boolean_t indx0;
};

extern osm_prtn_t *osm_prtn_make_new(osm_log_t * p_log, osm_subn_t * p_subn,
				     const char *name, uint16_t pkey);
extern ib_api_status_t osm_prtn_add_all(osm_log_t * p_log, osm_subn_t * p_subn,
					osm_prtn_t * p, unsigned type,
					boolean_t full, boolean_t indx0);
extern ib_api_status_t osm_prtn_add_port(osm_log_t * p_log,
					 osm_subn_t * p_subn, osm_prtn_t * p,
					 ib_net64_t guid, boolean_t full,
					 boolean_t indx0);

ib_api_status_t osm_prtn_add_mcgroup(osm_log_t * p_log, osm_subn_t * p_subn,
				     osm_prtn_t * p, uint8_t rate, uint8_t mtu,
				     uint8_t sl, uint8_t scope, uint32_t Q_Key,
				     uint8_t TClass, uint32_t FlowLabel,
				     const ib_gid_t *mgid);


static inline boolean_t mgid_is_broadcast(const ib_gid_t *mgid)
{
	return (memcmp(mgid, &osm_ipoib_broadcast_mgid,
			sizeof(osm_ipoib_broadcast_mgid)) == 0);
}

static inline boolean_t mgid_is_ip(const ib_gid_t *mgid)
{
	ib_net16_t ipsig = *(ib_net16_t *)&mgid->raw[2];
	return (ipsig == cl_hton16(0x401b) || ipsig == cl_hton16(0x601b));
}

static inline boolean_t ip_mgroup_pkey_ok(struct part_conf *conf,
					  struct precreate_mgroup *group)
{
	ib_net16_t mpkey = *(ib_net16_t *)&group->mgid.raw[4];
	char gid_str[INET6_ADDRSTRLEN];

	if (mgid_is_broadcast(&group->mgid)
	    /* user requested "wild card" of pkey */
	    || mpkey == 0x0000
	    /* user was smart enough to match */
	    || mpkey == (conf->p_prtn->pkey | cl_hton16(0x8000)))
		return (TRUE);

	OSM_LOG(conf->p_log, OSM_LOG_ERROR,
		"IP MC group (%s) specified with invalid pkey 0x%04x "
		"for partition pkey = 0x%04x (%s)\n",
		inet_ntop(AF_INET6, group->mgid.raw, gid_str, sizeof gid_str),
		cl_ntoh16(mpkey), cl_ntoh16(conf->p_prtn->pkey), conf->p_prtn->name);
	return (FALSE);
}

static inline boolean_t ip_mgroup_rate_ok(struct part_conf *conf,
				struct precreate_mgroup *group)
{
	char gid_str[INET6_ADDRSTRLEN];

	if (group->flags.rate == conf->flags.rate)
		return (TRUE);

	OSM_LOG(conf->p_log, OSM_LOG_ERROR,
		"IP MC group (%s) specified with invalid rate (%d): "
		"partition pkey = 0x%04x (%s) "
		"[Partition broadcast group rate = %d]\n",
		inet_ntop(AF_INET6, group->mgid.raw, gid_str, sizeof gid_str),
		group->flags.rate, cl_ntoh16(conf->p_prtn->pkey),
		conf->p_prtn->name, conf->flags.rate);
	return (FALSE);
}

static inline boolean_t ip_mgroup_mtu_ok(struct part_conf *conf,
				struct precreate_mgroup *group)
{
	char gid_str[INET6_ADDRSTRLEN];

	if (group->flags.mtu == conf->flags.mtu)
		return (TRUE);

	OSM_LOG(conf->p_log, OSM_LOG_ERROR,
		"IP MC group (%s) specified with invalid mtu (%d): "
		"partition pkey = 0x%04x (%s) "
		"[Partition broadcast group mtu = %d]\n",
		inet_ntop(AF_INET6, group->mgid.raw, gid_str, sizeof gid_str),
		group->flags.mtu, cl_ntoh16(conf->p_prtn->pkey),
		conf->p_prtn->name, conf->flags.mtu);
	return (FALSE);
}

static void __create_mgrp(struct part_conf *conf, struct precreate_mgroup *group)
{
	unsigned int scope;

	if (!group->flags.scope_mask) {
		osm_prtn_add_mcgroup(conf->p_log, conf->p_subn, conf->p_prtn,
				     (uint8_t) group->flags.rate,
				     (uint8_t) group->flags.mtu,
				     group->flags.sl,
				     0,
				     group->flags.Q_Key,
				     group->flags.TClass,
				     group->flags.FlowLabel,
				     &group->mgid);
	} else {
		for (scope = 0; scope < 16; scope++) {
			if (((1<<scope) & group->flags.scope_mask) == 0)
				continue;

			osm_prtn_add_mcgroup(conf->p_log, conf->p_subn, conf->p_prtn,
					     (uint8_t)group->flags.rate,
					     (uint8_t)group->flags.mtu,
					     (uint8_t)group->flags.sl,
					     (uint8_t)scope,
					     group->flags.Q_Key,
					     group->flags.TClass,
					     group->flags.FlowLabel,
					     &group->mgid);
		}
	}
}

static int partition_create(unsigned lineno, struct part_conf *conf,
			    char *name, char *id, char *flag, char *flag_val)
{
	ib_net16_t pkey;

	if (!id && name && isdigit(*name)) {
		id = name;
		name = NULL;
	}

	if (id) {
		char *end;

		pkey = cl_hton16((uint16_t)strtoul(id, &end, 0));
		if (end == id || *end)
			return -1;
	} else
		pkey = 0;

	conf->p_prtn = osm_prtn_make_new(conf->p_log, conf->p_subn,
					 name, pkey);
	if (!conf->p_prtn)
		return -1;

	if (!conf->p_subn->opt.qos && conf->flags.sl != OSM_DEFAULT_SL) {
		OSM_LOG(conf->p_log, OSM_LOG_DEBUG, "Overriding SL %d"
			" to default SL %d on partition %s"
			" as QoS is not enabled.\n",
			conf->flags.sl, OSM_DEFAULT_SL, name);
		conf->flags.sl = OSM_DEFAULT_SL;
	}
	conf->p_prtn->sl = (uint8_t) conf->flags.sl;

	if (conf->is_ipoib) {
		struct precreate_mgroup broadcast_mgroup;
		memset(&broadcast_mgroup, 0, sizeof(broadcast_mgroup));
		broadcast_mgroup.mgid = osm_ipoib_broadcast_mgid;
		pkey = CL_HTON16(0x8000) | conf->p_prtn->pkey;
		memcpy(&broadcast_mgroup.mgid.raw[4], &pkey , sizeof(pkey));
		broadcast_mgroup.flags.mtu = conf->flags.mtu;
		broadcast_mgroup.flags.rate = conf->flags.rate;
		broadcast_mgroup.flags.sl = conf->flags.sl;
		broadcast_mgroup.flags.Q_Key = conf->flags.Q_Key ?
						conf->flags.Q_Key :
						OSM_IPOIB_BROADCAST_MGRP_QKEY;
		broadcast_mgroup.flags.TClass = conf->flags.TClass;
		broadcast_mgroup.flags.FlowLabel = conf->flags.FlowLabel;
		__create_mgrp(conf, &broadcast_mgroup);
	}

	return 0;
}

/* returns 1 if processed 0 if _not_ */
static int parse_group_flag(unsigned lineno, osm_log_t * p_log,
			    struct group_flags *flags,
			    char *flag, char *val)
{
	int rc = 0;
	int len = strlen(flag);
	if (!strncmp(flag, "mtu", len)) {
		rc = 1;
		if (!val || (flags->mtu = strtoul(val, NULL, 0)) == 0)
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"flag \'mtu\' requires valid value"
				" - skipped\n", lineno);
	} else if (!strncmp(flag, "rate", len)) {
		rc = 1;
		if (!val || (flags->rate = strtoul(val, NULL, 0)) == 0)
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"flag \'rate\' requires valid value"
				" - skipped\n", lineno);
	} else if (!strncmp(flag, "scope", len)) {
		unsigned int scope;
		rc = 1;
		if (!val || (scope = strtoul(val, NULL, 0)) == 0 || scope > 0xF)
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"flag \'scope\' requires valid value"
				" - skipped\n", lineno);
		else
			flags->scope_mask |= (1<<scope);
	} else if (!strncmp(flag, "Q_Key", strlen(flag))) {
		rc = 1;
		if (!val || (flags->Q_Key = strtoul(val, NULL, 0)) == 0)
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"flag \'Q_Key\' requires valid value"
				" - using '0'\n", lineno);
	} else if (!strncmp(flag, "TClass", strlen(flag))) {
		rc =1;
		if (!val || (flags->TClass = strtoul(val, NULL, 0)) == 0)
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"flag \'TClass\' requires valid value"
				" - using '0'\n", lineno);
	} else if (!strncmp(flag, "sl", len)) {
		unsigned sl;
		char *end;
		rc = 1;

		if (!val || !*val || (sl = strtoul(val, &end, 0)) > 15 ||
		    (*end && !isspace(*end)))
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"flag \'sl\' requires valid value"
				" - skipped\n", lineno);
		else
			flags->sl = sl;
	} else if (!strncmp(flag, "FlowLabel", len)) {
		uint32_t FlowLabel;
		char *end;
		rc = 1;

		if (!val || !*val ||
		    (FlowLabel = strtoul(val, &end, 0)) > 0xFFFFF ||
		    (*end && !isspace(*end)))
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"flag \'FlowLabel\' requires valid value"
				" - skipped\n", lineno);
		else
			flags->FlowLabel = FlowLabel;
	}

	return rc;
}

static int partition_add_flag(unsigned lineno, struct part_conf *conf,
			      char *flag, char *val)
{
	int len = strlen(flag);

	/* ipoib gc group flags are processed here. */
	if (parse_group_flag(lineno, conf->p_log, &conf->flags, flag, val))
		return 0;

	/* partition flags go here. */
	if (!strncmp(flag, "ipoib", len)) {
		conf->is_ipoib = 1;
	} else if (!strncmp(flag, "defmember", len)) {
		if (!val || (strncmp(val, "limited", strlen(val))
			     && strncmp(val, "both", strlen(val))
			     && strncmp(val, "full", strlen(val))))
			OSM_LOG(conf->p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"flag \'defmember\' requires valid value (limited or full or both)"
				" - skipped\n", lineno);
		else {
			if (!strncmp(val, "full", strlen(val)))
				conf->membership = FULL;
			else if (!strncmp(val, "both", strlen(val)))
				conf->membership = BOTH;
			else
				conf->membership = LIMITED;
		}
	} else if (!strcmp(flag, "indx0"))
		conf->indx0 = TRUE;
	else {
		OSM_LOG(conf->p_log, OSM_LOG_VERBOSE,
			"PARSE WARN: line %d: "
			"unrecognized partition flag \'%s\'"
			" - ignored\n", lineno, flag);
	}
	return 0;
}
static void manage_membership_change(struct part_conf *conf, osm_prtn_t * p,
				     unsigned type, membership_t membership,
				     ib_net64_t guid)
{
	cl_map_t *p_tbl;
	cl_map_iterator_t p_next, p_item;
	osm_physp_t *p_physp;

	/* In allow_both_pkeys mode */
	/* if membership of the PKEY is set to FULL */
	/* need to clean up the part_guid_tbl table entry for this guid */
	/* if membership of the PKEY is set to LIMITED */
	/* need to clean up the full_guid_tbl table entry for this guid */
	/* as it could be populated because of previous definitions */

	if (!conf->p_subn->opt.allow_both_pkeys || membership == BOTH)
		return;

	switch (type){
	/* ALL = 0 */
	case 0:
		cl_map_remove_all(membership == LIMITED ?
				  &p->full_guid_tbl : &p->part_guid_tbl);
		break;
	/* specific GUID */
	case 0xFF:
		cl_map_remove(membership == LIMITED ?
			      &p->full_guid_tbl : &p->part_guid_tbl,
			      cl_hton64(guid));
		break;

	case IB_NODE_TYPE_CA:
	case IB_NODE_TYPE_SWITCH:
	case IB_NODE_TYPE_ROUTER:
		p_tbl = (membership == LIMITED) ?
			 &p->full_guid_tbl : &p->part_guid_tbl;

		p_next = cl_map_head(p_tbl);
		while (p_next != cl_map_end(p_tbl)) {
			p_item = p_next;
			p_next = cl_map_next(p_item);
			p_physp = (osm_physp_t *) cl_map_obj(p_item);
			if (osm_node_get_type(p_physp->p_node) == type)
				cl_map_remove_item(p_tbl, p_item);
		}
		break;
	default:
		break;

	}
}
static int partition_add_all(struct part_conf *conf, osm_prtn_t * p,
			     unsigned type, membership_t membership)
{
	manage_membership_change(conf, p, type, membership, 0);

	if (membership != LIMITED &&
	    osm_prtn_add_all(conf->p_log, conf->p_subn, p, type, TRUE, conf->indx0) != IB_SUCCESS)
		return -1;
	if ((membership == LIMITED ||
	     (membership == BOTH && conf->p_subn->opt.allow_both_pkeys)) &&
	    osm_prtn_add_all(conf->p_log, conf->p_subn, p, type, FALSE, conf->indx0) != IB_SUCCESS)
		return -1;
	return 0;
}

static int partition_add_port(unsigned lineno, struct part_conf *conf,
			      char *name, char *flag)
{
	osm_prtn_t *p = conf->p_prtn;
	ib_net64_t guid;
	membership_t membership = conf->membership;

	if (!name || !*name || !strncmp(name, "NONE", strlen(name)))
		return 0;

	if (flag) {
		/* reset default membership to limited */
		membership = LIMITED;
		if (!strncmp(flag, "full", strlen(flag)))
			membership = FULL;
		else if (!strncmp(flag, "both", strlen(flag)))
			membership = BOTH;
		else if (strncmp(flag, "limited", strlen(flag))) {
			OSM_LOG(conf->p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"unrecognized port flag \'%s\'."
				" Assume \'limited\'\n", lineno, flag);
		}
	}

	if (!strncmp(name, "ALL", strlen(name)))
		return partition_add_all(conf, p, 0, membership);
	else if (!strncmp(name, "ALL_CAS", strlen(name)))
		return partition_add_all(conf, p, IB_NODE_TYPE_CA, membership);
	else if (!strncmp(name, "ALL_SWITCHES", strlen(name)))
		return partition_add_all(conf, p, IB_NODE_TYPE_SWITCH,
					 membership);
	else if (!strncmp(name, "ALL_ROUTERS", strlen(name)))
		return partition_add_all(conf, p, IB_NODE_TYPE_ROUTER,
					 membership);
	else if (!strncmp(name, "SELF", strlen(name))) {
		guid = cl_ntoh64(conf->p_subn->sm_port_guid);
	} else {
		char *end;
		guid = strtoull(name, &end, 0);
		if (!guid || *end)
			return -1;
	}

	manage_membership_change(conf, p, 0xFF, membership, guid);
	if (membership != LIMITED &&
	    osm_prtn_add_port(conf->p_log, conf->p_subn, p,
			      cl_hton64(guid), TRUE, conf->indx0) != IB_SUCCESS)
		return -1;
	if ((membership == LIMITED ||
	    (membership == BOTH && conf->p_subn->opt.allow_both_pkeys)) &&
	    osm_prtn_add_port(conf->p_log, conf->p_subn, p,
			      cl_hton64(guid), FALSE, conf->indx0) != IB_SUCCESS)
		return -1;
	return 0;
}

/* conf file parser */

#define STRIP_HEAD_SPACES(p) while (*(p) == ' ' || *(p) == '\t' || \
		*(p) == '\n') { (p)++; }
#define STRIP_TAIL_SPACES(p) { char *q = (p) + strlen(p); \
				while ( q != (p) && ( *q == '\0' || \
					*q == ' ' || *q == '\t' || \
					*q == '\n')) { *q-- = '\0'; }; }

static int parse_name_token(char *str, char **name, char **val)
{
	int len = 0;
	char *p, *q;

	*name = *val = NULL;

	p = str;

	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;

	q = strchr(p, '=');
	if (q)
		*q++ = '\0';

	len = strlen(str) + 1;
	str = q;

	q = p + strlen(p);
	while (q != p && (*q == '\0' || *q == ' ' || *q == '\t' || *q == '\n'))
		*q-- = '\0';

	*name = p;

	p = str;
	if (!p)
		return len;

	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;

	q = p + strlen(p);
	len += (int)(q - str) + 1;
	while (q != p && (*q == '\0' || *q == ' ' || *q == '\t' || *q == '\n'))
		*q-- = '\0';
	*val = p;

	return len;
}

static int parse_mgroup_flags(osm_log_t * p_log,
				struct precreate_mgroup *mgroup,
				char *p, unsigned lineno)
{
	int ret, len = 0;
	char *flag, *val, *q;
	do {
		flag = val = NULL;
		q = strchr(p, ',');
		if (q)
			*q++ = '\0';

		ret = parse_name_token(p, &flag, &val);

		if (!parse_group_flag(lineno, p_log, &mgroup->flags,
				     flag, val)) {
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"PARSE WARN: line %d: "
				"unrecognized mgroup flag \'%s\'"
				" - ignored\n", lineno, flag);
		}
		p += ret;
		len += ret;
	} while (q);

	return (len);
}

static int mgroup_create(char *p, char *mgid, unsigned lineno, struct part_conf *conf)
{
	int ret = 0;
	struct precreate_mgroup mgroup;

	memset(&mgroup, 0, sizeof(mgroup));

	if (inet_pton(AF_INET6, mgid, &mgroup.mgid) != 1
	    || mgroup.mgid.raw[0] != 0xff) {
		OSM_LOG(conf->p_log, OSM_LOG_ERROR,
			"PARSE ERROR partition conf file line %d: "
			"mgid \"%s\": gid is not multicast\n", lineno, mgid);
		return 0;
	}

	/* inherit partition flags */
	mgroup.flags.mtu = conf->flags.mtu;
	mgroup.flags.rate = conf->flags.rate;
	mgroup.flags.sl = conf->flags.sl;
	mgroup.flags.Q_Key = conf->flags.Q_Key;
	mgroup.flags.FlowLabel = conf->flags.FlowLabel;
	mgroup.flags.scope_mask = conf->flags.scope_mask;

	/* override with user specified flags */
	ret = parse_mgroup_flags(conf->p_log, &mgroup, p, lineno);

	/* check/verify special IP group parameters */
	if (mgid_is_ip(&mgroup.mgid)) {
		ib_net16_t pkey = conf->p_prtn->pkey | cl_hton16(0x8000);

		if (!ip_mgroup_pkey_ok(conf, &mgroup)
		    || !ip_mgroup_rate_ok(conf, &mgroup)
		    || !ip_mgroup_mtu_ok(conf, &mgroup))
			goto error;

		/* set special IP settings */
		memcpy(&mgroup.mgid.raw[4], &pkey, sizeof(pkey));

		if (mgroup.flags.Q_Key == 0)
			mgroup.flags.Q_Key = OSM_IPOIB_BROADCAST_MGRP_QKEY;
	}

	/* don't create multiple copies of the group */
	if (osm_get_mgrp_by_mgid(conf->p_subn, &mgroup.mgid))
		goto error;

	/* create the group */
	__create_mgrp(conf, &mgroup);

error:
	return ret;
}

static struct part_conf *new_part_conf(osm_log_t * p_log, osm_subn_t * p_subn)
{
	static struct part_conf part;
	struct part_conf *conf = &part;

	memset(conf, 0, sizeof(*conf));
	conf->p_log = p_log;
	conf->p_subn = p_subn;
	conf->p_prtn = NULL;
	conf->is_ipoib = 0;
	conf->flags.sl = OSM_DEFAULT_SL;
	conf->flags.rate = OSM_DEFAULT_MGRP_RATE;
	conf->flags.mtu = OSM_DEFAULT_MGRP_MTU;
	conf->membership = LIMITED;
	conf->indx0 = FALSE;
	return conf;
}

static int flush_part_conf(struct part_conf *conf)
{
	memset(conf, 0, sizeof(*conf));
	return 0;
}

static int parse_part_conf(struct part_conf *conf, char *str, int lineno)
{
	int ret, len = 0;
	char *name, *id, *flag, *flval;
	char *q, *p;

	p = str;
	if (*p == '\t' || *p == '\0' || *p == '\n')
		p++;

	len += (int)(p - str);
	str = p;

	if (conf->p_prtn)
		goto skip_header;

	q = strchr(p, ':');
	if (!q) {
		OSM_LOG(conf->p_log, OSM_LOG_ERROR, "PARSE ERROR: line %d: "
			"no partition definition found\n", lineno);
		fprintf(stderr, "\nPARSE ERROR: line %d: "
			"no partition definition found\n", lineno);
		return -1;
	}

	*q++ = '\0';
	str = q;

	name = id = flag = flval = NULL;

	q = strchr(p, ',');
	if (q)
		*q = '\0';

	ret = parse_name_token(p, &name, &id);
	p += ret;
	len += ret;

	while (q) {
		flag = flval = NULL;
		q = strchr(p, ',');
		if (q)
			*q++ = '\0';
		ret = parse_name_token(p, &flag, &flval);
		if (!flag) {
			OSM_LOG(conf->p_log, OSM_LOG_ERROR,
				"PARSE ERROR: line %d: "
				"bad partition flags\n", lineno);
			fprintf(stderr, "\nPARSE ERROR: line %d: "
				"bad partition flags\n", lineno);
			return -1;
		}
		p += ret;
		len += ret;
		partition_add_flag(lineno, conf, flag, flval);
	}

	if (p != str || (partition_create(lineno, conf,
					  name, id, flag, flval) < 0)) {
		OSM_LOG(conf->p_log, OSM_LOG_ERROR, "PARSE ERROR: line %d: "
			"bad partition definition\n", lineno);
		fprintf(stderr, "\nPARSE ERROR: line %d: "
			"bad partition definition\n", lineno);
		return -1;
	}

skip_header:
	do {
		name = flag = NULL;
		q = strchr(p, ',');
		if (q)
			*q++ = '\0';
		ret = parse_name_token(p, &name, &flag);
		len += ret;

		if (strcmp(name, "mgid") == 0) {
			/* parse an mgid line if specified. */
			len += mgroup_create(p+ret, flag, lineno, conf);
			goto done; /* We're done: this consumes the line */
		}
		if (partition_add_port(lineno, conf, name, flag) < 0) {
			OSM_LOG(conf->p_log, OSM_LOG_ERROR,
				"PARSE ERROR: line %d: "
				"bad PortGUID\n", lineno);
			fprintf(stderr, "PARSE ERROR: line %d: "
				"bad PortGUID\n", lineno);
			return -1;
		}
		p += ret;
	} while (q);

done:
	return len;
}

/**
 * @return 1 on error, 0 on success
 */
int osm_prtn_config_parse_file(osm_log_t * p_log, osm_subn_t * p_subn,
			       const char *file_name)
{
	char line[4096];
	struct part_conf *conf = NULL;
	FILE *file;
	int lineno;
	int is_parse_success;

	line[0] = '\0';
	file = fopen(file_name, "r");
	if (!file) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE,
			"Cannot open config file \'%s\': %s\n",
			file_name, strerror(errno));
		return -1;
	}

	lineno = 0;

	is_parse_success = 0;

	while (fgets(line, sizeof(line) - 1, file) != NULL) {
		char *q, *p = line;

		lineno++;

		p = line;

		q = strchr(p, '#');
		if (q)
			*q = '\0';

		do {
			int len;
			while (*p == ' ' || *p == '\t' || *p == '\n')
				p++;
			if (*p == '\0')
				break;

			if (!conf && !(conf = new_part_conf(p_log, p_subn))) {
				OSM_LOG(p_log, OSM_LOG_ERROR,
					"PARSE ERROR: line %d: "
					"internal: cannot create config\n",
					lineno);
				fprintf(stderr,
					"PARSE ERROR: line %d: "
					"internal: cannot create config\n",
					lineno);
				is_parse_success = -1;
				break;
			}

			q = strchr(p, ';');
			if (q)
				*q = '\0';

			len = parse_part_conf(conf, p, lineno);
			if (len < 0) {
				is_parse_success = -1;
				break;
			}

			is_parse_success = 1;

			p += len;

			if (q) {
				flush_part_conf(conf);
				conf = NULL;
			}
		} while (q);

		if (is_parse_success == -1)
			break;
	}

	fclose(file);

	return (is_parse_success == 1) ? 0 : 1;
}
