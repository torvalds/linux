/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
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
 * $Begemot: bsnmp/snmp_mibII/mibII_rcvaddr.c,v 1.9 2004/08/06 08:47:03 brandt Exp $
 *
 * Interface receive address table.
 */
#include "mibII.h"
#include "mibII_oid.h"

/*
 * find receive address
 */
struct mibrcvaddr *
mib_find_rcvaddr(u_int ifindex, const u_char *addr, size_t addrlen)
{
	struct mibrcvaddr *rcv;

	TAILQ_FOREACH(rcv, &mibrcvaddr_list, link)
		if (rcv->ifindex == ifindex &&
		    rcv->addrlen == addrlen &&
		    memcmp(rcv->addr, addr, addrlen) == 0)
			return (rcv);
	return (NULL);
}

/*
 * Create receive address
 */
struct mibrcvaddr *
mib_rcvaddr_create(struct mibif *ifp, const u_char *addr, size_t addrlen)
{
	struct mibrcvaddr *rcv;
	u_int i;

	if (addrlen + OIDLEN_ifRcvAddressEntry + 1 > ASN_MAXOIDLEN)
		return (NULL);

	if ((rcv = malloc(sizeof(*rcv))) == NULL)
		return (NULL);
	rcv->ifindex = ifp->index;
	rcv->addrlen = addrlen;
	memcpy(rcv->addr, addr, addrlen);
	rcv->flags = 0;

	rcv->index.len = addrlen + 2;
	rcv->index.subs[0] = ifp->index;
	rcv->index.subs[1] = addrlen;
	for (i = 0; i < addrlen; i++)
		rcv->index.subs[i + 2] = addr[i];

	INSERT_OBJECT_OID(rcv, &mibrcvaddr_list);

	return (rcv);
}

/*
 * Delete a receive address
 */
void
mib_rcvaddr_delete(struct mibrcvaddr *rcv)
{
	TAILQ_REMOVE(&mibrcvaddr_list, rcv, link);
	free(rcv);
}

int
op_rcvaddr(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct mibrcvaddr *rcv;

	rcv = NULL;	/* make compiler happy */

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((rcv = NEXT_OBJECT_OID(&mibrcvaddr_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &rcv->index);
		break;

	  case SNMP_OP_GET:
		if ((rcv = FIND_OBJECT_OID(&mibrcvaddr_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if ((rcv = FIND_OBJECT_OID(&mibrcvaddr_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NO_CREATION);
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		abort();
	}

	switch (value->var.subs[sub - 1]) {

	  case LEAF_ifRcvAddressStatus:
		value->v.integer = 1;
		break;

	  case LEAF_ifRcvAddressType:
		value->v.integer = (rcv->flags & MIBRCVADDR_VOLATILE) ? 2 : 3;
		break;
	}
	return (SNMP_ERR_NOERROR);
}
