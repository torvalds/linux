/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Shteryana Sotirova Shopova
 * under sponsorship from the FreeBSD Foundation.
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
 * $Begemot: bsnmp/snmpd/trap.c,v 1.9 2005/10/04 11:21:39 brandt_h Exp $
 *
 * TrapSinkTable
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <syslog.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "snmpmod.h"
#include "snmpd.h"

#define	SNMPTREE_TYPES
#include "tree.h"
#include "oid.h"

struct trapsink_list trapsink_list = TAILQ_HEAD_INITIALIZER(trapsink_list);

/* List of target addresses */
static struct target_addresslist target_addresslist =
    SLIST_HEAD_INITIALIZER(target_addresslist);

/* List of target parameters */
static struct target_paramlist target_paramlist =
    SLIST_HEAD_INITIALIZER(target_paramlist);

/* List of notification targets */
static struct target_notifylist target_notifylist =
    SLIST_HEAD_INITIALIZER(target_notifylist);

static const struct asn_oid oid_begemotTrapSinkTable =
    OIDX_begemotTrapSinkTable;
static const struct asn_oid oid_sysUpTime = OIDX_sysUpTime;
static const struct asn_oid oid_snmpTrapOID = OIDX_snmpTrapOID;

struct trapsink_dep {
	struct snmp_dependency dep;
	u_int	set;
	u_int	status;
	u_char	comm[SNMP_COMMUNITY_MAXLEN + 1];
	u_int	version;
	u_int	rb;
	u_int	rb_status;
	u_int	rb_version;
	u_char	rb_comm[SNMP_COMMUNITY_MAXLEN + 1];
};
enum {
	TDEP_STATUS	= 0x0001,
	TDEP_COMM	= 0x0002,
	TDEP_VERSION	= 0x0004,

	TDEP_CREATE	= 0x0001,
	TDEP_MODIFY	= 0x0002,
	TDEP_DESTROY	= 0x0004,
};

static int
trapsink_create(struct trapsink_dep *tdep)
{
	struct trapsink *t;
	struct sockaddr_in sa;

	if ((t = malloc(sizeof(*t))) == NULL)
		return (SNMP_ERR_RES_UNAVAIL);

	t->index = tdep->dep.idx;
	t->status = TRAPSINK_NOT_READY;
	t->comm[0] = '\0';
	t->version = TRAPSINK_V2;

	if ((t->socket = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
		syslog(LOG_ERR, "socket(UDP): %m");
		free(t);
		return (SNMP_ERR_RES_UNAVAIL);
	}
	(void)shutdown(t->socket, SHUT_RD);
	memset(&sa, 0, sizeof(sa));
	sa.sin_len = sizeof(sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl((t->index.subs[0] << 24) |
	    (t->index.subs[1] << 16) | (t->index.subs[2] << 8) |
	    (t->index.subs[3] << 0));
	sa.sin_port = htons(t->index.subs[4]);

	if (connect(t->socket, (struct sockaddr *)&sa, sa.sin_len) == -1) {
		syslog(LOG_ERR, "connect(%s,%u): %m",
		    inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
		(void)close(t->socket);
		free(t);
		return (SNMP_ERR_GENERR);
	}

	if (tdep->set & TDEP_VERSION)
		t->version = tdep->version;
	if (tdep->set & TDEP_COMM)
		strcpy(t->comm, tdep->comm);

	if (t->comm[0] != '\0')
		t->status = TRAPSINK_NOT_IN_SERVICE;

	/* look whether we should activate */
	if (tdep->status == 4) {
		if (t->status == TRAPSINK_NOT_READY) {
			if (t->socket != -1)
				(void)close(t->socket);
			free(t);
			return (SNMP_ERR_INCONS_VALUE);
		}
		t->status = TRAPSINK_ACTIVE;
	}

	INSERT_OBJECT_OID(t, &trapsink_list);

	tdep->rb |= TDEP_CREATE;

	return (SNMP_ERR_NOERROR);
}

static void
trapsink_free(struct trapsink *t)
{
	TAILQ_REMOVE(&trapsink_list, t, link);
	if (t->socket != -1)
		(void)close(t->socket);
	free(t);
}

static int
trapsink_modify(struct trapsink *t, struct trapsink_dep *tdep)
{
	tdep->rb_status = t->status;
	tdep->rb_version = t->version;
	strcpy(tdep->rb_comm, t->comm);

	if (tdep->set & TDEP_STATUS) {
		/* if we are active and should move to not_in_service do
		 * this first */
		if (tdep->status == 2 && tdep->rb_status == TRAPSINK_ACTIVE) {
			t->status = TRAPSINK_NOT_IN_SERVICE;
			tdep->rb |= TDEP_MODIFY;
		}
	}

	if (tdep->set & TDEP_VERSION)
		t->version = tdep->version;
	if (tdep->set & TDEP_COMM)
		strcpy(t->comm, tdep->comm);

	if (tdep->set & TDEP_STATUS) {
		/* if we were inactive and should go active - do this now */
		if (tdep->status == 1 && tdep->rb_status != TRAPSINK_ACTIVE) {
			if (t->comm[0] == '\0') {
				t->status = tdep->rb_status;
				t->version = tdep->rb_version;
				strcpy(t->comm, tdep->rb_comm);
				return (SNMP_ERR_INCONS_VALUE);
			}
			t->status = TRAPSINK_ACTIVE;
			tdep->rb |= TDEP_MODIFY;
		}
	}
	return (SNMP_ERR_NOERROR);
}

static int
trapsink_unmodify(struct trapsink *t, struct trapsink_dep *tdep)
{
	if (tdep->set & TDEP_STATUS)
		t->status = tdep->rb_status;
	if (tdep->set & TDEP_VERSION)
		t->version = tdep->rb_version;
	if (tdep->set & TDEP_COMM)
		strcpy(t->comm, tdep->rb_comm);

	return (SNMP_ERR_NOERROR);
}

static int
trapsink_destroy(struct snmp_context *ctx __unused, struct trapsink *t,
    struct trapsink_dep *tdep)
{
	t->status = TRAPSINK_DESTROY;
	tdep->rb_status = t->status;
	tdep->rb |= TDEP_DESTROY;
	return (SNMP_ERR_NOERROR);
}

static int
trapsink_undestroy(struct trapsink *t, struct trapsink_dep *tdep)
{
	t->status = tdep->rb_status;
	return (SNMP_ERR_NOERROR);
}

static int
trapsink_dep(struct snmp_context *ctx, struct snmp_dependency *dep,
    enum snmp_depop op)
{
	struct trapsink_dep *tdep = (struct trapsink_dep *)dep;
	struct trapsink *t;

	t = FIND_OBJECT_OID(&trapsink_list, &dep->idx, 0);

	switch (op) {

	  case SNMP_DEPOP_COMMIT:
		if (tdep->set & TDEP_STATUS) {
			switch (tdep->status) {

			  case 1:
			  case 2:
				if (t == NULL)
					return (SNMP_ERR_INCONS_VALUE);
				return (trapsink_modify(t, tdep));

			  case 4:
			  case 5:
				if (t != NULL)
					return (SNMP_ERR_INCONS_VALUE);
				return (trapsink_create(tdep));

			  case 6:
				if (t == NULL)
					return (SNMP_ERR_NOERROR);
				return (trapsink_destroy(ctx, t, tdep));
			}
		} else if (tdep->set != 0)
			return (trapsink_modify(t, tdep));

		return (SNMP_ERR_NOERROR);

	  case SNMP_DEPOP_ROLLBACK:
		if (tdep->rb & TDEP_CREATE) {
			trapsink_free(t);
			return (SNMP_ERR_NOERROR);
		}
		if (tdep->rb & TDEP_MODIFY)
			return (trapsink_unmodify(t, tdep));
		if(tdep->rb & TDEP_DESTROY)
			return (trapsink_undestroy(t, tdep));
		return (SNMP_ERR_NOERROR);

	  case SNMP_DEPOP_FINISH:
		if ((tdep->rb & TDEP_DESTROY) && t != NULL &&
		    ctx->code == SNMP_RET_OK)
			trapsink_free(t);
		return (SNMP_ERR_NOERROR);
	}
	abort();
}

int
op_trapsink(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx, enum snmp_op op)
{
	struct trapsink *t;
	u_char ipa[4];
	int32_t port;
	struct asn_oid idx;
	struct trapsink_dep *tdep;
	u_char *p;

	t = NULL;		/* gcc */

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((t = NEXT_OBJECT_OID(&trapsink_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &t->index);
		break;

	  case SNMP_OP_GET:
		if ((t = FIND_OBJECT_OID(&trapsink_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if (index_decode(&value->var, sub, iidx, ipa, &port) ||
		    port == 0 || port > 65535)
			return (SNMP_ERR_NO_CREATION);
		t = FIND_OBJECT_OID(&trapsink_list, &value->var, sub);

		asn_slice_oid(&idx, &value->var, sub, value->var.len);

		tdep = (struct trapsink_dep *)snmp_dep_lookup(ctx,
		    &oid_begemotTrapSinkTable, &idx,
		    sizeof(*tdep), trapsink_dep);
		if (tdep == NULL)
			return (SNMP_ERR_RES_UNAVAIL);

		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotTrapSinkStatus:
			if (tdep->set & TDEP_STATUS)
				return (SNMP_ERR_INCONS_VALUE);
			switch (value->v.integer) {

			  case 1:
			  case 2:
				if (t == NULL)
					return (SNMP_ERR_INCONS_VALUE);
				break;

			  case 4:
			  case 5:
				if (t != NULL)
					return (SNMP_ERR_INCONS_VALUE);
				break;

			  case 6:
				break;

			  default:
				return (SNMP_ERR_WRONG_VALUE);
			}
			tdep->status = value->v.integer;
			tdep->set |= TDEP_STATUS;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotTrapSinkComm:
			if (tdep->set & TDEP_COMM)
				return (SNMP_ERR_INCONS_VALUE);
			if (value->v.octetstring.len == 0 ||
			    value->v.octetstring.len > SNMP_COMMUNITY_MAXLEN)
				return (SNMP_ERR_WRONG_VALUE);
			for (p = value->v.octetstring.octets;
			     p < value->v.octetstring.octets + value->v.octetstring.len;
			     p++) {
				if (!isascii(*p) || !isprint(*p))
					return (SNMP_ERR_WRONG_VALUE);
			}
			tdep->set |= TDEP_COMM;
			strncpy(tdep->comm, value->v.octetstring.octets,
			    value->v.octetstring.len);
			tdep->comm[value->v.octetstring.len] = '\0';
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotTrapSinkVersion:
			if (tdep->set & TDEP_VERSION)
				return (SNMP_ERR_INCONS_VALUE);
			if (value->v.integer != TRAPSINK_V1 &&
			    value->v.integer != TRAPSINK_V2)
				return (SNMP_ERR_WRONG_VALUE);
			tdep->version = value->v.integer;
			tdep->set |= TDEP_VERSION;
			return (SNMP_ERR_NOERROR);
		}
		if (t == NULL)
			return (SNMP_ERR_INCONS_NAME);
		else
			return (SNMP_ERR_NOT_WRITEABLE);


	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	}

	switch (value->var.subs[sub - 1]) {

	  case LEAF_begemotTrapSinkStatus:
		value->v.integer = t->status;
		break;

	  case LEAF_begemotTrapSinkComm:
		return (string_get(value, t->comm, -1));

	  case LEAF_begemotTrapSinkVersion:
		value->v.integer = t->version;
		break;

	}
	return (SNMP_ERR_NOERROR);
}

static void
snmp_create_v1_trap(struct snmp_pdu *pdu, char *com,
    const struct asn_oid *trap_oid)
{
	memset(pdu, 0, sizeof(*pdu));
	strlcpy(pdu->community, com, sizeof(pdu->community));

	pdu->version = SNMP_V1;
	pdu->type = SNMP_PDU_TRAP;
	pdu->enterprise = systemg.object_id;
	memcpy(pdu->agent_addr, snmpd.trap1addr, 4);
	pdu->generic_trap = trap_oid->subs[trap_oid->len - 1] - 1;
	pdu->specific_trap = 0;
	pdu->time_stamp = get_ticks() - start_tick;
	pdu->nbindings = 0;
}

static void
snmp_create_v2_trap(struct snmp_pdu *pdu, char *com,
    const struct asn_oid *trap_oid)
{
	memset(pdu, 0, sizeof(*pdu));
	strlcpy(pdu->community, com, sizeof(pdu->community));

	pdu->version = SNMP_V2c;
	pdu->type = SNMP_PDU_TRAP2;
	pdu->request_id = reqid_next(trap_reqid);
	pdu->error_index = 0;
	pdu->error_status = SNMP_ERR_NOERROR;

	pdu->bindings[0].var = oid_sysUpTime;
	pdu->bindings[0].var.subs[pdu->bindings[0].var.len++] = 0;
	pdu->bindings[0].syntax = SNMP_SYNTAX_TIMETICKS;
	pdu->bindings[0].v.uint32 = get_ticks() - start_tick;

	pdu->bindings[1].var = oid_snmpTrapOID;
	pdu->bindings[1].var.subs[pdu->bindings[1].var.len++] = 0;
	pdu->bindings[1].syntax = SNMP_SYNTAX_OID;
	pdu->bindings[1].v.oid = *trap_oid;

	pdu->nbindings = 2;
}

static void
snmp_create_v3_trap(struct snmp_pdu *pdu, struct target_param *target,
    const struct asn_oid *trap_oid)
{
	struct usm_user *usmuser;

	memset(pdu, 0, sizeof(*pdu));

	pdu->version = SNMP_V3;
	pdu->type = SNMP_PDU_TRAP2;
	pdu->request_id = reqid_next(trap_reqid);
	pdu->error_index = 0;
	pdu->error_status = SNMP_ERR_NOERROR;

	pdu->bindings[0].var = oid_sysUpTime;
	pdu->bindings[0].var.subs[pdu->bindings[0].var.len++] = 0;
	pdu->bindings[0].syntax = SNMP_SYNTAX_TIMETICKS;
	pdu->bindings[0].v.uint32 = get_ticks() - start_tick;

	pdu->bindings[1].var = oid_snmpTrapOID;
	pdu->bindings[1].var.subs[pdu->bindings[1].var.len++] = 0;
	pdu->bindings[1].syntax = SNMP_SYNTAX_OID;
	pdu->bindings[1].v.oid = *trap_oid;

	pdu->nbindings = 2;

	update_snmpd_engine_time();

	memcpy(pdu->engine.engine_id, snmpd_engine.engine_id,
	    snmpd_engine.engine_len);
	pdu->engine.engine_len = snmpd_engine.engine_len;
	pdu->engine.engine_boots = snmpd_engine.engine_boots;
	pdu->engine.engine_time = snmpd_engine.engine_time;
	pdu->engine.max_msg_size = snmpd_engine.max_msg_size;
	strlcpy(pdu->user.sec_name, target->secname,
	    sizeof(pdu->user.sec_name));
	pdu->security_model = target->sec_model;

	pdu->context_engine_len = snmpd_engine.engine_len;
	memcpy(pdu->context_engine, snmpd_engine.engine_id,
	    snmpd_engine.engine_len);

	if (target->sec_model == SNMP_SECMODEL_USM &&
	    target->sec_level != SNMP_noAuthNoPriv) {
	    	usmuser = usm_find_user(pdu->engine.engine_id,
	    	   pdu->engine.engine_len, pdu->user.sec_name);
		if (usmuser != NULL) {
			pdu->user.auth_proto = usmuser->suser.auth_proto;
			pdu->user.priv_proto = usmuser->suser.priv_proto;
			memcpy(pdu->user.auth_key, usmuser->suser.auth_key,
			    sizeof(pdu->user.auth_key));
			memcpy(pdu->user.priv_key, usmuser->suser.priv_key,
			    sizeof(pdu->user.priv_key));
		}
		snmp_pdu_init_secparams(pdu);
	}
}

void
snmp_send_trap(const struct asn_oid *trap_oid, ...)
{
	struct snmp_pdu pdu;
	struct trapsink *t;
	const struct snmp_value *v;
	struct target_notify *n;
	struct target_address *ta;
	struct target_param *tp;

	va_list ap;
	u_char *sndbuf;
	char *tag;
	size_t sndlen;
	ssize_t len;
	int32_t ip;

	TAILQ_FOREACH(t, &trapsink_list, link) {
		if (t->status != TRAPSINK_ACTIVE)
			continue;

		if (t->version == TRAPSINK_V1)
			snmp_create_v1_trap(&pdu, t->comm, trap_oid);
		else
			snmp_create_v2_trap(&pdu, t->comm, trap_oid);

		va_start(ap, trap_oid);
		while ((v = va_arg(ap, const struct snmp_value *)) != NULL)
			pdu.bindings[pdu.nbindings++] = *v;
		va_end(ap);

		if (snmp_pdu_auth_access(&pdu, &ip) != SNMP_CODE_OK) {
			syslog(LOG_DEBUG, "send trap to %s failed: no access",
			    t->comm);
			continue;
		}

		if ((sndbuf = buf_alloc(1)) == NULL) {
			syslog(LOG_ERR, "trap send buffer: %m");
			return;
		}

		snmp_output(&pdu, sndbuf, &sndlen, "TRAP");

		if ((len = send(t->socket, sndbuf, sndlen, 0)) == -1)
			syslog(LOG_ERR, "send: %m");
		else if ((size_t)len != sndlen)
			syslog(LOG_ERR, "send: short write %zu/%zu",
			    sndlen, (size_t)len);

		free(sndbuf);
	}

	SLIST_FOREACH(n, &target_notifylist, tn) {
		if (n->status != RowStatus_active || n->taglist[0] == '\0')
			continue;

		SLIST_FOREACH(ta, &target_addresslist, ta)
			if ((tag = strstr(ta->taglist, n->taglist)) != NULL  &&
			    (tag[strlen(n->taglist)] == ' ' ||
			     tag[strlen(n->taglist)] == '\0' ||
			     tag[strlen(n->taglist)] == '\t' ||
			     tag[strlen(n->taglist)] == '\r' ||
			     tag[strlen(n->taglist)] == '\n') &&
			     ta->status == RowStatus_active)
				break;
		if (ta == NULL)
			continue;

		SLIST_FOREACH(tp, &target_paramlist, tp)
			if (strcmp(tp->name, ta->paramname) == 0 &&
			    tp->status == 1)
				break;
		if (tp == NULL)
			continue;

		switch (tp->mpmodel) {
		case SNMP_MPM_SNMP_V1:
			snmp_create_v1_trap(&pdu, tp->secname, trap_oid);
			break;

		case SNMP_MPM_SNMP_V2c:
			snmp_create_v2_trap(&pdu, tp->secname, trap_oid);
			break;

		case SNMP_MPM_SNMP_V3:
			snmp_create_v3_trap(&pdu, tp, trap_oid);
			break;

		default:
			continue;
		}

		va_start(ap, trap_oid);
		while ((v = va_arg(ap, const struct snmp_value *)) != NULL)
			pdu.bindings[pdu.nbindings++] = *v;
		va_end(ap);

		if (snmp_pdu_auth_access(&pdu, &ip) != SNMP_CODE_OK) {
			syslog(LOG_DEBUG, "send trap to %s failed: no access",
			    t->comm);
			continue;
		}

		if ((sndbuf = buf_alloc(1)) == NULL) {
			syslog(LOG_ERR, "trap send buffer: %m");
			return;
		}

		snmp_output(&pdu, sndbuf, &sndlen, "TRAP");

		if ((len = send(ta->socket, sndbuf, sndlen, 0)) == -1)
			syslog(LOG_ERR, "send: %m");
		else if ((size_t)len != sndlen)
			syslog(LOG_ERR, "send: short write %zu/%zu",
			    sndlen, (size_t)len);

		free(sndbuf);
	}
}

/*
 * RFC 3413 SNMP Management Target MIB
 */
struct snmpd_target_stats *
bsnmpd_get_target_stats(void)
{
	return (&snmpd_target_stats);
}

struct target_address *
target_first_address(void)
{
	return (SLIST_FIRST(&target_addresslist));
}

struct target_address *
target_next_address(struct target_address *addrs)
{
	if (addrs == NULL)
		return (NULL);

	return (SLIST_NEXT(addrs, ta));
}

struct target_address *
target_new_address(char *aname)
{
	int cmp;
	struct target_address *addrs, *temp, *prev;

	SLIST_FOREACH(addrs, &target_addresslist, ta)
		if (strcmp(aname, addrs->name) == 0)
			return (NULL);

	if ((addrs = (struct target_address *)malloc(sizeof(*addrs))) == NULL)
		return (NULL);

	memset(addrs, 0, sizeof(*addrs));
	strlcpy(addrs->name, aname, sizeof(addrs->name));
	addrs->timeout = 150;
	addrs->retry = 3; /* XXX */

	if ((prev = SLIST_FIRST(&target_addresslist)) == NULL ||
	    strcmp(aname, prev->name) < 0) {
		SLIST_INSERT_HEAD(&target_addresslist, addrs, ta);
		return (addrs);
	}

	SLIST_FOREACH(temp, &target_addresslist, ta) {
		if ((cmp = strcmp(aname, temp->name)) <= 0)
			break;
		prev = temp;
	}

	if (temp == NULL || cmp < 0)
		SLIST_INSERT_AFTER(prev, addrs, ta);
	else if (cmp > 0)
		SLIST_INSERT_AFTER(temp, addrs, ta);
	else {
		syslog(LOG_ERR, "Target address %s exists", addrs->name);
		free(addrs);
		return (NULL);
	}

	return (addrs);
}

int
target_activate_address(struct target_address *addrs)
{
	struct sockaddr_in sa;

	if ((addrs->socket = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
		syslog(LOG_ERR, "socket(UDP): %m");
		return (SNMP_ERR_RES_UNAVAIL);
	}

	(void)shutdown(addrs->socket, SHUT_RD);
	memset(&sa, 0, sizeof(sa));
	sa.sin_len = sizeof(sa);
	sa.sin_family = AF_INET;

	sa.sin_addr.s_addr = htonl((addrs->address[0] << 24) |
	    (addrs->address[1] << 16) | (addrs->address[2] << 8) |
	    (addrs->address[3] << 0));
	sa.sin_port = htons(addrs->address[4]) << 8 |
	     htons(addrs->address[5]) << 0;

	if (connect(addrs->socket, (struct sockaddr *)&sa, sa.sin_len) == -1) {
		syslog(LOG_ERR, "connect(%s,%u): %m",
		    inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
		(void)close(addrs->socket);
		return (SNMP_ERR_GENERR);
	}

	addrs->status = RowStatus_active;

	return (SNMP_ERR_NOERROR);
}

int
target_delete_address(struct target_address *addrs)
{
	SLIST_REMOVE(&target_addresslist, addrs, target_address, ta);
	if (addrs->status == RowStatus_active)
		close(addrs->socket);
	free(addrs);

	return (0);
}

struct target_param *
target_first_param(void)
{
	return (SLIST_FIRST(&target_paramlist));
}

struct target_param *
target_next_param(struct target_param *param)
{
	if (param == NULL)
		return (NULL);

	return (SLIST_NEXT(param, tp));
}

struct target_param *
target_new_param(char *pname)
{
	int cmp;
	struct target_param *param, *temp, *prev;

	SLIST_FOREACH(param, &target_paramlist, tp)
		if (strcmp(pname, param->name) == 0)
			return (NULL);

	if ((param = (struct target_param *)malloc(sizeof(*param))) == NULL)
		return (NULL);

	memset(param, 0, sizeof(*param));
	strlcpy(param->name, pname, sizeof(param->name));

	if ((prev = SLIST_FIRST(&target_paramlist)) == NULL ||
	    strcmp(pname, prev->name) < 0) {
		SLIST_INSERT_HEAD(&target_paramlist, param, tp);
		return (param);
	}

	SLIST_FOREACH(temp, &target_paramlist, tp) {
		if ((cmp = strcmp(pname, temp->name)) <= 0)
			break;
		prev = temp;
	}

	if (temp == NULL || cmp < 0)
		SLIST_INSERT_AFTER(prev, param, tp);
	else if (cmp > 0)
		SLIST_INSERT_AFTER(temp, param, tp);
	else {
		syslog(LOG_ERR, "Target parameter %s exists", param->name);
		free(param);
		return (NULL);
	}

	return (param);
}

int
target_delete_param(struct target_param *param)
{
	SLIST_REMOVE(&target_paramlist, param, target_param, tp);
	free(param);

	return (0);
}

struct target_notify *
target_first_notify(void)
{
	return (SLIST_FIRST(&target_notifylist));
}

struct target_notify *
target_next_notify(struct target_notify *notify)
{
	if (notify == NULL)
		return (NULL);

	return (SLIST_NEXT(notify, tn));
}

struct target_notify *
target_new_notify(char *nname)
{
	int cmp;
	struct target_notify *notify, *temp, *prev;

	SLIST_FOREACH(notify, &target_notifylist, tn)
		if (strcmp(nname, notify->name) == 0)
			return (NULL);

	if ((notify = (struct target_notify *)malloc(sizeof(*notify))) == NULL)
		return (NULL);

	memset(notify, 0, sizeof(*notify));
	strlcpy(notify->name, nname, sizeof(notify->name));

	if ((prev = SLIST_FIRST(&target_notifylist)) == NULL ||
	    strcmp(nname, prev->name) < 0) {
		SLIST_INSERT_HEAD(&target_notifylist, notify, tn);
		return (notify);
	}

	SLIST_FOREACH(temp, &target_notifylist, tn) {
		if ((cmp = strcmp(nname, temp->name)) <= 0)
			break;
		prev = temp;
	}

	if (temp == NULL || cmp < 0)
		SLIST_INSERT_AFTER(prev, notify, tn);
	else if (cmp > 0)
		SLIST_INSERT_AFTER(temp, notify, tn);
	else {
		syslog(LOG_ERR, "Notification target %s exists", notify->name);
		free(notify);
		return (NULL);
	}

	return (notify);
}

int
target_delete_notify(struct target_notify *notify)
{
	SLIST_REMOVE(&target_notifylist, notify, target_notify, tn);
	free(notify);

	return (0);
}

void
target_flush_all(void)
{
	struct target_address *addrs;
	struct target_param *param;
	struct target_notify *notify;

	while ((addrs = SLIST_FIRST(&target_addresslist)) != NULL) {
		SLIST_REMOVE_HEAD(&target_addresslist, ta);
		if (addrs->status == RowStatus_active)
			close(addrs->socket);
		free(addrs);
	}
	SLIST_INIT(&target_addresslist);

	while ((param = SLIST_FIRST(&target_paramlist)) != NULL) {
		SLIST_REMOVE_HEAD(&target_paramlist, tp);
		free(param);
	}
	SLIST_INIT(&target_paramlist);

	while ((notify = SLIST_FIRST(&target_notifylist)) != NULL) {
		SLIST_REMOVE_HEAD(&target_notifylist, tn);
		free(notify);
	}
	SLIST_INIT(&target_notifylist);
}
