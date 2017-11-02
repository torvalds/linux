/* AFS Volume Location Service client
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/sched.h>
#include "afs_fs.h"
#include "internal.h"

/*
 * deliver reply data to a VL.GetEntryByXXX call
 */
static int afs_deliver_vl_get_entry_by_xxx(struct afs_call *call)
{
	struct afs_cache_vlocation *entry;
	__be32 *bp;
	u32 tmp;
	int loop, ret;

	_enter("");

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	entry = call->reply[0];
	bp = call->buffer;

	for (loop = 0; loop < 64; loop++)
		entry->name[loop] = ntohl(*bp++);
	entry->name[loop] = 0;
	bp++; /* final NUL */

	bp++; /* type */
	entry->nservers = ntohl(*bp++);

	for (loop = 0; loop < 8; loop++) {
		entry->servers[loop].srx_family = AF_RXRPC;
		entry->servers[loop].srx_service = FS_SERVICE;
		entry->servers[loop].transport_type = SOCK_DGRAM;
		entry->servers[loop].transport_len = sizeof(entry->servers[loop].transport.sin6);
		entry->servers[loop].transport.sin6.sin6_family = AF_INET6;
		entry->servers[loop].transport.sin6.sin6_port = htons(AFS_FS_PORT);
		entry->servers[loop].transport.sin6.sin6_flowinfo = 0;
		entry->servers[loop].transport.sin6.sin6_scope_id = 0;
		entry->servers[loop].transport.sin6.sin6_addr.s6_addr32[0] = 0;
		entry->servers[loop].transport.sin6.sin6_addr.s6_addr32[1] = 0;
		entry->servers[loop].transport.sin6.sin6_addr.s6_addr32[2] = htonl(0xffff);
		entry->servers[loop].transport.sin6.sin6_addr.s6_addr32[3] = *bp++;
	}

	bp += 8; /* partition IDs */

	for (loop = 0; loop < 8; loop++) {
		tmp = ntohl(*bp++);
		entry->srvtmask[loop] = 0;
		if (tmp & AFS_VLSF_RWVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_RW;
		if (tmp & AFS_VLSF_ROVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_RO;
		if (tmp & AFS_VLSF_BACKVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_BAK;
	}

	entry->vid[0] = ntohl(*bp++);
	entry->vid[1] = ntohl(*bp++);
	entry->vid[2] = ntohl(*bp++);

	bp++; /* clone ID */

	tmp = ntohl(*bp++); /* flags */
	entry->vidmask = 0;
	if (tmp & AFS_VLF_RWEXISTS)
		entry->vidmask |= AFS_VOL_VTM_RW;
	if (tmp & AFS_VLF_ROEXISTS)
		entry->vidmask |= AFS_VOL_VTM_RO;
	if (tmp & AFS_VLF_BACKEXISTS)
		entry->vidmask |= AFS_VOL_VTM_BAK;
	if (!entry->vidmask)
		return -EBADMSG;

	_leave(" = 0 [done]");
	return 0;
}

/*
 * VL.GetEntryByName operation type
 */
static const struct afs_call_type afs_RXVLGetEntryByName = {
	.name		= "VL.GetEntryByName",
	.deliver	= afs_deliver_vl_get_entry_by_xxx,
	.destructor	= afs_flat_call_destructor,
};

/*
 * VL.GetEntryById operation type
 */
static const struct afs_call_type afs_RXVLGetEntryById = {
	.name		= "VL.GetEntryById",
	.deliver	= afs_deliver_vl_get_entry_by_xxx,
	.destructor	= afs_flat_call_destructor,
};

/*
 * dispatch a get volume entry by name operation
 */
int afs_vl_get_entry_by_name(struct afs_net *net,
			     struct sockaddr_rxrpc *addr,
			     struct key *key,
			     const char *volname,
			     struct afs_cache_vlocation *entry,
			     bool async)
{
	struct afs_call *call;
	size_t volnamesz, reqsz, padsz;
	__be32 *bp;

	_enter("");

	volnamesz = strlen(volname);
	padsz = (4 - (volnamesz & 3)) & 3;
	reqsz = 8 + volnamesz + padsz;

	call = afs_alloc_flat_call(net, &afs_RXVLGetEntryByName, reqsz, 384);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply[0] = entry;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(VLGETENTRYBYNAME);
	*bp++ = htonl(volnamesz);
	memcpy(bp, volname, volnamesz);
	if (padsz > 0)
		memset((void *) bp + volnamesz, 0, padsz);

	/* initiate the call */
	return afs_make_call(addr, call, GFP_KERNEL, async);
}

/*
 * dispatch a get volume entry by ID operation
 */
int afs_vl_get_entry_by_id(struct afs_net *net,
			   struct sockaddr_rxrpc *addr,
			   struct key *key,
			   afs_volid_t volid,
			   afs_voltype_t voltype,
			   struct afs_cache_vlocation *entry,
			   bool async)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXVLGetEntryById, 12, 384);
	if (!call)
		return -ENOMEM;

	call->key = key;
	call->reply[0] = entry;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(VLGETENTRYBYID);
	*bp++ = htonl(volid);
	*bp   = htonl(voltype);

	/* initiate the call */
	return afs_make_call(addr, call, GFP_KERNEL, async);
}
