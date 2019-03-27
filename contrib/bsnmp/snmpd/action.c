/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 * Copyright (c) 2004-2006
 *	Hartmut Brandt.
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: action.c 517 2006-10-31 08:52:04Z brandt_h $
 *
 * Variable access for SNMPd
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "snmpmod.h"
#include "snmpd.h"
#include "tree.h"
#include "oid.h"

static const struct asn_oid
	oid_begemotSnmpdModuleTable = OIDX_begemotSnmpdModuleTable;

#ifdef __FreeBSD__
static const struct asn_oid
	oid_freeBSDVersion = OIDX_freeBSDVersion;
#endif

/*
 * Get an integer value from the KERN sysctl subtree.
 */
static char *
act_getkernint(int id)
{
	int mib[2];
	size_t len;
	u_long value;
	char *string;

	mib[0] = CTL_KERN;
	mib[1] = id;
	len = sizeof(value);
	if (sysctl(mib, 2, &value, &len, NULL, 0) != 0)
		return (NULL);

	if ((string = malloc(20)) == NULL)
		return (NULL);
	sprintf(string, "%lu", value);
	return (string);
}

/*
 * Initialize global variables of the system group.
 */
int
init_actvals(void)
{
	struct utsname uts;
	char *hostid;
	size_t len;
#ifdef __FreeBSD__
	char *rel, *p, *end;
	u_long num;
#endif

	if (uname(&uts) == -1)
		return (-1);

	if ((systemg.name = strdup(uts.nodename)) == NULL)
		return (-1);

	if ((hostid = act_getkernint(KERN_HOSTID)) == NULL)
		return (-1);

	len = strlen(uts.nodename) + 1;
	len += strlen(hostid) + 1;
	len += strlen(uts.sysname) + 1;
	len += strlen(uts.release) + 1;

	if ((systemg.descr = malloc(len)) == NULL) {
		free(hostid);
		return (-1);
	}
	sprintf(systemg.descr, "%s %s %s %s", uts.nodename, hostid, uts.sysname,
	    uts.release);

#ifdef __FreeBSD__
	/*
	 * Construct a FreeBSD oid
	 */
	systemg.object_id = oid_freeBSDVersion;
	rel = uts.release;
	while ((p = strsep(&rel, ".")) != NULL &&
	    systemg.object_id.len < ASN_MAXOIDLEN) {
		systemg.object_id.subs[systemg.object_id.len] = 0;
		if (*p != '\0') {
			num = strtoul(p, &end, 10);
			if (end == p)
				break;
			systemg.object_id.subs[systemg.object_id.len] = num;
		}
		systemg.object_id.len++;
	}
#endif

	free(hostid);

	return (0);
}

/*
 * Initialize global variables of the snmpEngine group.
 */
int
init_snmpd_engine(void)
{
	char *hostid;

	snmpd_engine.engine_boots = 1;
	snmpd_engine.engine_time = 1;
	snmpd_engine.max_msg_size = 1500; /* XXX */

	snmpd_engine.engine_id[0] = ((OID_freeBSD & 0xff000000) >> 24) | 0x80;
	snmpd_engine.engine_id[1] = (OID_freeBSD & 0xff0000) >> 16;
	snmpd_engine.engine_id[2] = (OID_freeBSD & 0xff00) >> 8;
	snmpd_engine.engine_id[3] = OID_freeBSD & 0xff;
	snmpd_engine.engine_id[4] = 128;
	snmpd_engine.engine_len = 5;

	if ((hostid = act_getkernint(KERN_HOSTID)) == NULL)
		return (-1);

	if (strlen(hostid) > SNMP_ENGINE_ID_SIZ - snmpd_engine.engine_len) {
		memcpy(snmpd_engine.engine_id + snmpd_engine.engine_len,
		    hostid, SNMP_ENGINE_ID_SIZ - snmpd_engine.engine_len);
		snmpd_engine.engine_len = SNMP_ENGINE_ID_SIZ;
	} else {
		memcpy(snmpd_engine.engine_id + snmpd_engine.engine_len,
		    hostid, strlen(hostid));
		snmpd_engine.engine_len += strlen(hostid);
	}

	free(hostid);

	return (0);
}

int
set_snmpd_engine(void)
{
	FILE *fp;
	uint32_t i;
	uint8_t *cptr, engine[2 * SNMP_ENGINE_ID_SIZ + 2];
	uint8_t myengine[2 * SNMP_ENGINE_ID_SIZ + 2];

	if (engine_file[0] == '\0')
		return (-1);

	cptr = myengine;
	for (i = 0; i < snmpd_engine.engine_len; i++)
		cptr += sprintf(cptr, "%.2x", snmpd_engine.engine_id[i]);
	*cptr++ = '\n';
	*cptr++ = '\0';

	if ((fp = fopen(engine_file, "r+")) != NULL) {
		if (fgets(engine, sizeof(engine) - 1, fp) == NULL ||
		    fscanf(fp, "%u",  &snmpd_engine.engine_boots) <= 0) {
			fclose(fp);
			goto save_boots;
		}

		fclose(fp);
		if (strcmp(myengine, engine) != 0)
			snmpd_engine.engine_boots = 1;
		else
			snmpd_engine.engine_boots++;
	} else if (errno != ENOENT)
		return (-1);

save_boots:
	if ((fp = fopen(engine_file, "w+")) == NULL)
		return (-1);
	fprintf(fp, "%s%u\n", myengine, snmpd_engine.engine_boots);
	fclose(fp);

	return (0);
}

void
update_snmpd_engine_time(void)
{
	uint64_t etime;

	etime = (get_ticks() - start_tick) / 100ULL;
	if (etime < INT32_MAX)
		snmpd_engine.engine_time = etime;
	else {
		start_tick = get_ticks();
		(void)set_snmpd_engine();
		snmpd_engine.engine_time = start_tick;
	}
}

/*************************************************************
 *
 * System group
 */
int
op_system_group(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		break;

	  case SNMP_OP_SET:
		switch (which) {

		  case LEAF_sysDescr:
			if (community != COMM_INITIALIZE)
				return (SNMP_ERR_NOT_WRITEABLE);
			return (string_save(value, ctx, -1, &systemg.descr));

		  case LEAF_sysObjectId:
			if (community != COMM_INITIALIZE)
				return (SNMP_ERR_NOT_WRITEABLE);
			return (oid_save(value, ctx, &systemg.object_id));

		  case LEAF_sysContact:
			return (string_save(value, ctx, -1, &systemg.contact));

		  case LEAF_sysName:
			return (string_save(value, ctx, -1, &systemg.name));

		  case LEAF_sysLocation:
			return (string_save(value, ctx, -1, &systemg.location));
		}
		return (SNMP_ERR_NO_CREATION);

	  case SNMP_OP_ROLLBACK:
		switch (which) {

		  case LEAF_sysDescr:
			string_rollback(ctx, &systemg.descr);
			return (SNMP_ERR_NOERROR);
		  case LEAF_sysObjectId:
			oid_rollback(ctx, &systemg.object_id);
			return (SNMP_ERR_NOERROR);
		  case LEAF_sysContact:
			string_rollback(ctx, &systemg.contact);
			return (SNMP_ERR_NOERROR);
		  case LEAF_sysName:
			string_rollback(ctx, &systemg.name);
			return (SNMP_ERR_NOERROR);
		  case LEAF_sysLocation:
			string_rollback(ctx, &systemg.location);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (which) {

		  case LEAF_sysDescr:
			string_commit(ctx);
			return (SNMP_ERR_NOERROR);
		  case LEAF_sysObjectId:
			oid_commit(ctx);
			return (SNMP_ERR_NOERROR);
		  case LEAF_sysContact:
			string_commit(ctx);
			return (SNMP_ERR_NOERROR);
		  case LEAF_sysName:
			string_commit(ctx);
			return (SNMP_ERR_NOERROR);
		  case LEAF_sysLocation:
			string_commit(ctx);
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}

	/*
	 * Come here for GET.
	 */
	switch (which) {

	  case LEAF_sysDescr:
		return (string_get(value, systemg.descr, -1));
	  case LEAF_sysObjectId:
		return (oid_get(value, &systemg.object_id));
	  case LEAF_sysUpTime:
		value->v.uint32 = get_ticks() - start_tick;
		break;
	  case LEAF_sysContact:
		return (string_get(value, systemg.contact, -1));
	  case LEAF_sysName:
		return (string_get(value, systemg.name, -1));
	  case LEAF_sysLocation:
		return (string_get(value, systemg.location, -1));
	  case LEAF_sysServices:
		value->v.integer = systemg.services;
		break;
	  case LEAF_sysORLastChange:
		value->v.uint32 = systemg.or_last_change;
		break;
	}
	return (SNMP_ERR_NOERROR);
}

/*************************************************************
 *
 * Debug group
 */
int
op_debug(struct snmp_context *ctx, struct snmp_value *value, u_int sub,
    u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		switch (which) {

		  case LEAF_begemotSnmpdDebugDumpPdus:
			value->v.integer = TRUTH_MK(debug.dump_pdus);
			break;

		  case LEAF_begemotSnmpdDebugSnmpTrace:
			value->v.uint32 = snmp_trace;
			break;

		  case LEAF_begemotSnmpdDebugSyslogPri:
			value->v.integer = debug.logpri;
			break;
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_SET:
		switch (which) {

		  case LEAF_begemotSnmpdDebugDumpPdus:
			if (!TRUTH_OK(value->v.integer))
				return (SNMP_ERR_WRONG_VALUE);
			ctx->scratch->int1 = debug.dump_pdus;
			debug.dump_pdus = TRUTH_GET(value->v.integer);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotSnmpdDebugSnmpTrace:
			ctx->scratch->int1 = snmp_trace;
			snmp_trace = value->v.uint32;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotSnmpdDebugSyslogPri:
			if (value->v.integer < 0 || value->v.integer > 8)
				return (SNMP_ERR_WRONG_VALUE);
			ctx->scratch->int1 = debug.logpri;
			debug.logpri = (u_int)value->v.integer;
			return (SNMP_ERR_NOERROR);
		}
		return (SNMP_ERR_NO_CREATION);

	  case SNMP_OP_ROLLBACK:
		switch (which) {

		  case LEAF_begemotSnmpdDebugDumpPdus:
			debug.dump_pdus = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotSnmpdDebugSnmpTrace:
			snmp_trace = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotSnmpdDebugSyslogPri:
			debug.logpri = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (which) {

		  case LEAF_begemotSnmpdDebugDumpPdus:
		  case LEAF_begemotSnmpdDebugSnmpTrace:
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotSnmpdDebugSyslogPri:
			if (debug.logpri == 0)
				setlogmask(0);
			else
				setlogmask(LOG_UPTO(debug.logpri - 1));
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}
	abort();
}

/*************************************************************
 *
 * OR Table
 */
int
op_or_table(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct objres *objres;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((objres = NEXT_OBJECT_INT(&objres_list, &value->var, sub))
		    == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.subs[sub] = objres->index;
		value->var.len = sub + 1;
		break;

	  case SNMP_OP_GET:
		if ((objres = FIND_OBJECT_INT(&objres_list, &value->var, sub))
		    == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if ((objres = FIND_OBJECT_INT(&objres_list, &value->var, sub))
		    == NULL)
			return (SNMP_ERR_NO_CREATION);
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
	  default:
		abort();
	}

	/*
	 * Come here for GET, GETNEXT.
	 */
	switch (value->var.subs[sub - 1]) {

	  case LEAF_sysORID:
		value->v.oid = objres->oid;
		break;

	  case LEAF_sysORDescr:
		return (string_get(value, objres->descr, -1));

	  case LEAF_sysORUpTime:
		value->v.uint32 = objres->uptime;
		break;
	}
	return (SNMP_ERR_NOERROR);
}

/*************************************************************
 *
 * mib-2 snmp
 */
int
op_snmp(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_snmpInPkts:
			value->v.uint32 = snmpd_stats.inPkts;
			break;

		  case LEAF_snmpInBadVersions:
			value->v.uint32 = snmpd_stats.inBadVersions;
			break;

		  case LEAF_snmpInBadCommunityNames:
			value->v.uint32 = snmpd_stats.inBadCommunityNames;
			break;

		  case LEAF_snmpInBadCommunityUses:
			value->v.uint32 = snmpd_stats.inBadCommunityUses;
			break;

		  case LEAF_snmpInASNParseErrs:
			value->v.uint32 = snmpd_stats.inASNParseErrs;
			break;

		  case LEAF_snmpEnableAuthenTraps:
			value->v.integer = TRUTH_MK(snmpd.auth_traps);
			break;

		  case LEAF_snmpSilentDrops:
			value->v.uint32 = snmpd_stats.silentDrops;
			break;

		  case LEAF_snmpProxyDrops:
			value->v.uint32 = snmpd_stats.proxyDrops;
			break;

		  default:
			return (SNMP_ERR_NOSUCHNAME);

		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_SET:
		switch (value->var.subs[sub - 1]) {
		  case LEAF_snmpEnableAuthenTraps:
			if (!TRUTH_OK(value->v.integer))
				return (SNMP_ERR_WRONG_VALUE);
			ctx->scratch->int1 = value->v.integer;
			snmpd.auth_traps = TRUTH_GET(value->v.integer);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_ROLLBACK:
		switch (value->var.subs[sub - 1]) {
		  case LEAF_snmpEnableAuthenTraps:
			snmpd.auth_traps = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (value->var.subs[sub - 1]) {
		  case LEAF_snmpEnableAuthenTraps:
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}
	abort();
}

/*************************************************************
 *
 * SNMPd statistics group
 */
int
op_snmpd_stats(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	switch (op) {

	  case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotSnmpdStatsNoRxBufs:
			value->v.uint32 = snmpd_stats.noRxbuf;
			break;

		  case LEAF_begemotSnmpdStatsNoTxBufs:
			value->v.uint32 = snmpd_stats.noTxbuf;
			break;

		  case LEAF_begemotSnmpdStatsInTooLongPkts:
			value->v.uint32 = snmpd_stats.inTooLong;
			break;

		  case LEAF_begemotSnmpdStatsInBadPduTypes:
			value->v.uint32 = snmpd_stats.inBadPduTypes;
			break;

		  default:
			return (SNMP_ERR_NOSUCHNAME);
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_SET:
	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
	  case SNMP_OP_GETNEXT:
		abort();
	}
	abort();
}

/*
 * SNMPd configuration scalars
 */
int
op_snmpd_config(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		switch (which) {

		  case LEAF_begemotSnmpdTransmitBuffer:
			value->v.integer = snmpd.txbuf;
			break;
		  case LEAF_begemotSnmpdReceiveBuffer:
			value->v.integer = snmpd.rxbuf;
			break;
		  case LEAF_begemotSnmpdCommunityDisable:
			value->v.integer = TRUTH_MK(snmpd.comm_dis);
			break;
		  case LEAF_begemotSnmpdTrap1Addr:
			return (ip_get(value, snmpd.trap1addr));
		  case LEAF_begemotSnmpdVersionEnable:
			value->v.uint32 = snmpd.version_enable;
			break;
		  default:
			return (SNMP_ERR_NOSUCHNAME);
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_SET:
		switch (which) {

		  case LEAF_begemotSnmpdTransmitBuffer:
			ctx->scratch->int1 = snmpd.txbuf;
			if (value->v.integer < 484 ||
			    value->v.integer > 65535)
				return (SNMP_ERR_WRONG_VALUE);
			snmpd.txbuf = value->v.integer;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotSnmpdReceiveBuffer:
			ctx->scratch->int1 = snmpd.rxbuf;
			if (value->v.integer < 484 ||
			    value->v.integer > 65535)
				return (SNMP_ERR_WRONG_VALUE);
			snmpd.rxbuf = value->v.integer;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotSnmpdCommunityDisable:
			ctx->scratch->int1 = snmpd.comm_dis;
			if (!TRUTH_OK(value->v.integer))
				return (SNMP_ERR_WRONG_VALUE);
			if (TRUTH_GET(value->v.integer)) {
				snmpd.comm_dis = 1;
			} else {
				if (snmpd.comm_dis)
					return (SNMP_ERR_WRONG_VALUE);
			}
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotSnmpdTrap1Addr:
			return (ip_save(value, ctx, snmpd.trap1addr));

		  case LEAF_begemotSnmpdVersionEnable:
			if (community != COMM_INITIALIZE)
				return (SNMP_ERR_NOT_WRITEABLE);
			ctx->scratch->int1 = snmpd.version_enable;
			if (value->v.uint32 == 0 ||
			    (value->v.uint32 & ~VERS_ENABLE_ALL))
				return (SNMP_ERR_WRONG_VALUE);
			snmpd.version_enable = value->v.uint32;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_ROLLBACK:
		switch (which) {

		  case LEAF_begemotSnmpdTransmitBuffer:
			snmpd.rxbuf = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);
		  case LEAF_begemotSnmpdReceiveBuffer:
			snmpd.txbuf = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);
		  case LEAF_begemotSnmpdCommunityDisable:
			snmpd.comm_dis = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);
		  case LEAF_begemotSnmpdTrap1Addr:
			ip_rollback(ctx, snmpd.trap1addr);
			return (SNMP_ERR_NOERROR);
		  case LEAF_begemotSnmpdVersionEnable:
			snmpd.version_enable = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (which) {

		  case LEAF_begemotSnmpdTransmitBuffer:
		  case LEAF_begemotSnmpdReceiveBuffer:
		  case LEAF_begemotSnmpdCommunityDisable:
			return (SNMP_ERR_NOERROR);
		  case LEAF_begemotSnmpdTrap1Addr:
			ip_commit(ctx);
			return (SNMP_ERR_NOERROR);
		  case LEAF_begemotSnmpdVersionEnable:
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}
	abort();
}

/*
 * The community table
 */
int
op_community(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct asn_oid idx;
	struct community *c;
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((community != COMM_INITIALIZE && snmpd.comm_dis) ||
		    (c = NEXT_OBJECT_OID(&community_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &c->index);
		break;

	  case SNMP_OP_GET:
		if ((community != COMM_INITIALIZE && snmpd.comm_dis) ||
		    (c = FIND_OBJECT_OID(&community_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if (community != COMM_INITIALIZE && snmpd.comm_dis)
			return (SNMP_ERR_NOT_WRITEABLE);
		idx.len = 2;
		idx.subs[0] = 0;
		idx.subs[1] = value->var.subs[value->var.len - 1];
		switch (which) {
		case LEAF_begemotSnmpdCommunityString:
			/* check that given string is unique */
			TAILQ_FOREACH(c, &community_list, link) {
				if (!asn_compare_oid(&idx, &c->index))
					continue;
				if (c->string != NULL && strcmp(c->string,
				    value->v.octetstring.octets) == 0)
					return (SNMP_ERR_WRONG_VALUE);
			}
		case LEAF_begemotSnmpdCommunityPermission:
			break;
		default:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		if ((c = FIND_OBJECT_OID(&community_list, &value->var,
		    sub)) == NULL) {
			/* create new community and use user sepcified index */
			c = comm_define_ordered(COMM_READ, "SNMP Custom Community",
			    &idx, NULL, NULL);
			if (c == NULL)
				return (SNMP_ERR_NO_CREATION);
		}
		switch (which) {
		case LEAF_begemotSnmpdCommunityString:
			return (string_save(value, ctx, -1, &c->string));
		case LEAF_begemotSnmpdCommunityPermission:
			if (value->v.integer != COMM_READ &&
			    value->v.integer != COMM_WRITE)
				return (SNMP_ERR_WRONG_VALUE);
			c->private = value->v.integer;
			break;
		default:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_ROLLBACK:
		if (which == LEAF_begemotSnmpdCommunityString) {
			if ((c = FIND_OBJECT_OID(&community_list, &value->var,
			    sub)) == NULL)
				string_free(ctx);
			else
				string_rollback(ctx, &c->string);
			return (SNMP_ERR_NOERROR);
		}
		if (which == LEAF_begemotSnmpdCommunityPermission)
			return (SNMP_ERR_NOERROR);
		abort();

	  case SNMP_OP_COMMIT:
		if (which == LEAF_begemotSnmpdCommunityString) {
			if ((c = FIND_OBJECT_OID(&community_list, &value->var,
			    sub)) == NULL)
				string_free(ctx);
			else
				string_commit(ctx);
			return (SNMP_ERR_NOERROR);
		}
		if (which == LEAF_begemotSnmpdCommunityPermission)
			return (SNMP_ERR_NOERROR);
		abort();

	  default:
		abort();
	}

	switch (which) {

	  case LEAF_begemotSnmpdCommunityString:
		return (string_get(value, c->string, -1));

	  case LEAF_begemotSnmpdCommunityDescr:
		return (string_get(value, c->descr, -1));

	  case LEAF_begemotSnmpdCommunityPermission:
		value->v.integer = c->private;
		return (SNMP_ERR_NOERROR);
	  default:
		return (SNMP_ERR_NOT_WRITEABLE);
	}
	abort();
}

/*
 * Module table.
 */
struct module_dep {
	struct snmp_dependency dep;
	u_char	section[LM_SECTION_MAX + 1];
	u_char	*path;
	struct lmodule *m;
};

static int
dep_modules(struct snmp_context *ctx, struct snmp_dependency *dep,
    enum snmp_depop op)
{
	struct module_dep *mdep = (struct module_dep *)(void *)dep;

	switch (op) {

	  case SNMP_DEPOP_COMMIT:
		if (mdep->path == NULL) {
			/* unload - find the module */
			TAILQ_FOREACH(mdep->m, &lmodules, link)
				if (strcmp(mdep->m->section,
				    mdep->section) == 0)
					break;
			if (mdep->m == NULL)
				/* no such module - that's ok */
				return (SNMP_ERR_NOERROR);

			/* handle unloading in the finalizer */
			return (SNMP_ERR_NOERROR);
		}
		/* load */
		if ((mdep->m = lm_load(mdep->path, mdep->section)) == NULL) {
			/* could not load */
			return (SNMP_ERR_RES_UNAVAIL);
		}
		/* start in finalizer */
		return (SNMP_ERR_NOERROR);

	  case SNMP_DEPOP_ROLLBACK:
		if (mdep->path == NULL) {
			/* rollback unload - the finalizer takes care */
			return (SNMP_ERR_NOERROR);
		}
		/* rollback load */
		lm_unload(mdep->m);
		return (SNMP_ERR_NOERROR);

	  case SNMP_DEPOP_FINISH:
		if (mdep->path == NULL) {
			if (mdep->m != NULL && ctx->code == SNMP_RET_OK)
				lm_unload(mdep->m);
		} else {
			if (mdep->m != NULL && ctx->code == SNMP_RET_OK &&
			    community != COMM_INITIALIZE)
				lm_start(mdep->m);
			free(mdep->path);
		}
		return (SNMP_ERR_NOERROR);
	}
	abort();
}

int
op_modules(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	struct lmodule *m;
	u_char *section, *ptr;
	size_t seclen;
	struct module_dep *mdep;
	struct asn_oid idx;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((m = NEXT_OBJECT_OID(&lmodules, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &m->index);
		break;

	  case SNMP_OP_GET:
		if ((m = FIND_OBJECT_OID(&lmodules, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		m = FIND_OBJECT_OID(&lmodules, &value->var, sub);
		if (which != LEAF_begemotSnmpdModulePath) {
			if (m == NULL)
				return (SNMP_ERR_NO_CREATION);
			return (SNMP_ERR_NOT_WRITEABLE);
		}

		/* the errors in the next few statements can only happen when
		 * m is NULL, hence the NO_CREATION error. */
		if (index_decode(&value->var, sub, iidx,
		    &section, &seclen))
			return (SNMP_ERR_NO_CREATION);

		/* check the section name */
		if (seclen > LM_SECTION_MAX || seclen == 0) {
			free(section);
			return (SNMP_ERR_NO_CREATION);
		}
		for (ptr = section; ptr < section + seclen; ptr++)
			if (!isascii(*ptr) || !isalnum(*ptr)) {
				free(section);
				return (SNMP_ERR_NO_CREATION);
			}
		if (!isalpha(section[0])) {
			free(section);
			return (SNMP_ERR_NO_CREATION);
		}

		/* check the path */
		for (ptr = value->v.octetstring.octets;
		     ptr < value->v.octetstring.octets + value->v.octetstring.len;
		     ptr++) {
			if (*ptr == '\0') {
				free(section);
				return (SNMP_ERR_WRONG_VALUE);
			}
		}

		if (m == NULL) {
			if (value->v.octetstring.len == 0) {
				free(section);
				return (SNMP_ERR_INCONS_VALUE);
			}
		} else {
			if (value->v.octetstring.len != 0) {
				free(section);
				return (SNMP_ERR_INCONS_VALUE);
			}
		}

		asn_slice_oid(&idx, &value->var, sub, value->var.len);

		/* so far, so good */
		mdep = (struct module_dep *)(void *)snmp_dep_lookup(ctx,
		    &oid_begemotSnmpdModuleTable, &idx,
		    sizeof(*mdep), dep_modules);
		if (mdep == NULL) {
			free(section);
			return (SNMP_ERR_RES_UNAVAIL);
		}

		if (mdep->section[0] != '\0') {
			/* two writes to the same entry - bad */
			free(section);
			return (SNMP_ERR_INCONS_VALUE);
		}

		strncpy(mdep->section, section, seclen);
		mdep->section[seclen] = '\0';
		free(section);

		if (value->v.octetstring.len == 0)
			mdep->path = NULL;
		else {
			if ((mdep->path = malloc(value->v.octetstring.len + 1)) == NULL)
				return (SNMP_ERR_RES_UNAVAIL);
			strncpy(mdep->path, value->v.octetstring.octets,
			    value->v.octetstring.len);
			mdep->path[value->v.octetstring.len] = '\0';
		}
		ctx->scratch->ptr1 = mdep;
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	  default:
		abort();
	}

	switch (which) {

	  case LEAF_begemotSnmpdModulePath:
		return (string_get(value, m->path, -1));

	  case LEAF_begemotSnmpdModuleComment:
		return (string_get(value, m->config->comment, -1));
	}
	abort();
}

int
op_snmp_set(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_snmpSetSerialNo:
			value->v.integer = snmp_serial_no;
			break;

		  default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_SET:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_snmpSetSerialNo:
			if (value->v.integer != snmp_serial_no)
				return (SNMP_ERR_INCONS_VALUE);
			break;

		  default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_ROLLBACK:
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_COMMIT:
		if (snmp_serial_no++ == 2147483647)
			snmp_serial_no = 0;
		return (SNMP_ERR_NOERROR);
	}
	abort();
}

/*
 * SNMP Engine
 */
int
op_snmp_engine(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GETNEXT:
		abort();

	case SNMP_OP_GET:
		break;

	case SNMP_OP_SET:
		if (community != COMM_INITIALIZE)
			return (SNMP_ERR_NOT_WRITEABLE);
		switch (which) {
		case LEAF_snmpEngineID:
			if (value->v.octetstring.len > SNMP_ENGINE_ID_SIZ)
				return (SNMP_ERR_WRONG_VALUE);
			ctx->scratch->ptr1 = malloc(snmpd_engine.engine_len);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			memcpy(ctx->scratch->ptr1, snmpd_engine.engine_id,
			    snmpd_engine.engine_len);
			ctx->scratch->int1 = snmpd_engine.engine_len;
			snmpd_engine.engine_len = value->v.octetstring.len;
			memcpy(snmpd_engine.engine_id,
			    value->v.octetstring.octets,
			    value->v.octetstring.len);
			break;

		case LEAF_snmpEngineMaxMessageSize:
			ctx->scratch->int1 = snmpd_engine.max_msg_size;
			snmpd_engine.max_msg_size = value->v.integer;
			break;

		default:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		switch (which) {
		case LEAF_snmpEngineID:
			snmpd_engine.engine_len = ctx->scratch->int1;
			memcpy(snmpd_engine.engine_id, ctx->scratch->ptr1,
			    snmpd_engine.engine_len);
			free(ctx->scratch->ptr1);
			break;

		case LEAF_snmpEngineMaxMessageSize:
			snmpd_engine.max_msg_size = ctx->scratch->int1;
			break;

		default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		if (which == LEAF_snmpEngineID) {
			if (set_snmpd_engine() < 0) {
				snmpd_engine.engine_len = ctx->scratch->int1;
				memcpy(snmpd_engine.engine_id,
				    ctx->scratch->ptr1, ctx->scratch->int1);
			}
			free(ctx->scratch->ptr1);
		}
		return (SNMP_ERR_NOERROR);
	}


	switch (which) {
	case LEAF_snmpEngineID:
		return (string_get(value, snmpd_engine.engine_id,
		    snmpd_engine.engine_len));
	case LEAF_snmpEngineBoots:
		value->v.integer = snmpd_engine.engine_boots;
		break;
	case LEAF_snmpEngineTime:
		update_snmpd_engine_time();
		value->v.integer = snmpd_engine.engine_time;
		break;
	case LEAF_snmpEngineMaxMessageSize:
		value->v.integer = snmpd_engine.max_msg_size;
		break;
	default:
		return (SNMP_ERR_NOSUCHNAME);
	}

	return (SNMP_ERR_NOERROR);
}

/*
 * Transport table
 */
int
op_transport_table(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	struct transport *t;
	u_char *tname, *ptr;
	size_t tnamelen;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((t = NEXT_OBJECT_OID(&transport_list, &value->var, sub))
		    == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &t->index);
		break;

	  case SNMP_OP_GET:
		if ((t = FIND_OBJECT_OID(&transport_list, &value->var, sub))
		    == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		t = FIND_OBJECT_OID(&transport_list, &value->var, sub);
		if (which != LEAF_begemotSnmpdTransportStatus) {
			if (t == NULL)
				return (SNMP_ERR_NO_CREATION);
			return (SNMP_ERR_NOT_WRITEABLE);
		}

		/* the errors in the next few statements can only happen when
		 * t is NULL, hence the NO_CREATION error. */
		if (index_decode(&value->var, sub, iidx,
		    &tname, &tnamelen))
			return (SNMP_ERR_NO_CREATION);

		/* check the section name */
		if (tnamelen >= TRANS_NAMELEN || tnamelen == 0) {
			free(tname);
			return (SNMP_ERR_NO_CREATION);
		}
		for (ptr = tname; ptr < tname + tnamelen; ptr++) {
			if (!isascii(*ptr) || !isalnum(*ptr)) {
				free(tname);
				return (SNMP_ERR_NO_CREATION);
			}
		}

		/* for now */
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	  default:
		abort();
	}

	switch (which) {

	    case LEAF_begemotSnmpdTransportStatus:
		value->v.integer = 1;
		break;

	    case LEAF_begemotSnmpdTransportOid:
		memcpy(&value->v.oid, &t->vtab->id, sizeof(t->vtab->id));
		break;
	}
	return (SNMP_ERR_NOERROR);
}
