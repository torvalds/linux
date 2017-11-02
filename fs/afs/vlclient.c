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
 * Deliver reply data to a VL.GetEntryByNameU call.
 */
static int afs_deliver_vl_get_entry_by_name_u(struct afs_call *call)
{
	struct afs_uvldbentry__xdr *uvldb;
	struct afs_vldb_entry *entry;
	bool new_only = false;
	u32 tmp;
	int i, ret;

	_enter("");

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	uvldb = call->buffer;
	entry = call->reply[0];

	for (i = 0; i < ARRAY_SIZE(uvldb->name) - 1; i++)
		entry->name[i] = (u8)ntohl(uvldb->name[i]);
	entry->name[i] = 0;
	entry->name_len = strlen(entry->name);

	/* If there is a new replication site that we can use, ignore all the
	 * sites that aren't marked as new.
	 */
	for (i = 0; i < AFS_NMAXNSERVERS; i++) {
		tmp = ntohl(uvldb->serverFlags[i]);
		if (!(tmp & AFS_VLSF_DONTUSE) &&
		    (tmp & AFS_VLSF_NEWREPSITE))
			new_only = true;
	}

	for (i = 0; i < AFS_NMAXNSERVERS; i++) {
		struct afs_uuid__xdr *xdr;
		struct afs_uuid *uuid;
		int j;

		tmp = ntohl(uvldb->serverFlags[i]);
		if (tmp & AFS_VLSF_DONTUSE ||
		    (new_only && !(tmp & AFS_VLSF_NEWREPSITE)))
			continue;
		if (tmp & AFS_VLSF_RWVOL)
			entry->fs_mask[i] |= AFS_VOL_VTM_RW;
		if (tmp & AFS_VLSF_ROVOL)
			entry->fs_mask[i] |= AFS_VOL_VTM_RO;
		if (tmp & AFS_VLSF_BACKVOL)
			entry->fs_mask[i] |= AFS_VOL_VTM_BAK;
		if (!entry->fs_mask[i])
			continue;

		xdr = &uvldb->serverNumber[i];
		uuid = (struct afs_uuid *)&entry->fs_server[i];
		uuid->time_low			= xdr->time_low;
		uuid->time_mid			= htons(ntohl(xdr->time_mid));
		uuid->time_hi_and_version	= htons(ntohl(xdr->time_hi_and_version));
		uuid->clock_seq_hi_and_reserved	= (u8)ntohl(xdr->clock_seq_hi_and_reserved);
		uuid->clock_seq_low		= (u8)ntohl(xdr->clock_seq_low);
		for (j = 0; j < 6; j++)
			uuid->node[j] = (u8)ntohl(xdr->node[j]);

		entry->nr_servers++;
	}

	for (i = 0; i < AFS_MAXTYPES; i++)
		entry->vid[i] = ntohl(uvldb->volumeId[i]);

	tmp = ntohl(uvldb->flags);
	if (tmp & AFS_VLF_RWEXISTS)
		__set_bit(AFS_VLDB_HAS_RW, &entry->flags);
	if (tmp & AFS_VLF_ROEXISTS)
		__set_bit(AFS_VLDB_HAS_RO, &entry->flags);
	if (tmp & AFS_VLF_BACKEXISTS)
		__set_bit(AFS_VLDB_HAS_BAK, &entry->flags);

	if (!(tmp & (AFS_VLF_RWEXISTS | AFS_VLF_ROEXISTS | AFS_VLF_BACKEXISTS))) {
		entry->error = -ENOMEDIUM;
		__set_bit(AFS_VLDB_QUERY_ERROR, &entry->flags);
	}

	__set_bit(AFS_VLDB_QUERY_VALID, &entry->flags);
	_leave(" = 0 [done]");
	return 0;
}

static void afs_destroy_vl_get_entry_by_name_u(struct afs_call *call)
{
	kfree(call->reply[0]);
	afs_flat_call_destructor(call);
}

/*
 * VL.GetEntryByNameU operation type.
 */
static const struct afs_call_type afs_RXVLGetEntryByNameU = {
	.name		= "VL.GetEntryByNameU",
	.deliver	= afs_deliver_vl_get_entry_by_name_u,
	.destructor	= afs_destroy_vl_get_entry_by_name_u,
};

/*
 * Dispatch a get volume entry by name or ID operation (uuid variant).  If the
 * volname is a decimal number then it's a volume ID not a volume name.
 */
struct afs_vldb_entry *afs_vl_get_entry_by_name_u(struct afs_net *net,
						  struct afs_addr_cursor *ac,
						  struct key *key,
						  const char *volname,
						  int volnamesz)
{
	struct afs_vldb_entry *entry;
	struct afs_call *call;
	size_t reqsz, padsz;
	__be32 *bp;

	_enter("");

	padsz = (4 - (volnamesz & 3)) & 3;
	reqsz = 8 + volnamesz + padsz;

	entry = kzalloc(sizeof(struct afs_vldb_entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	call = afs_alloc_flat_call(net, &afs_RXVLGetEntryByNameU, reqsz,
				   sizeof(struct afs_uvldbentry__xdr));
	if (!call) {
		kfree(entry);
		return ERR_PTR(-ENOMEM);
	}

	call->key = key;
	call->reply[0] = entry;
	call->ret_reply0 = true;

	/* Marshall the parameters */
	bp = call->request;
	*bp++ = htonl(VLGETENTRYBYNAMEU);
	*bp++ = htonl(volnamesz);
	memcpy(bp, volname, volnamesz);
	if (padsz > 0)
		memset((void *)bp + volnamesz, 0, padsz);

	return (struct afs_vldb_entry *)afs_make_call(ac, call, GFP_KERNEL, false);
}

/*
 * Deliver reply data to a VL.GetAddrsU call.
 *
 *	GetAddrsU(IN ListAddrByAttributes *inaddr,
 *		  OUT afsUUID *uuidp1,
 *		  OUT uint32_t *uniquifier,
 *		  OUT uint32_t *nentries,
 *		  OUT bulkaddrs *blkaddrs);
 */
static int afs_deliver_vl_get_addrs_u(struct afs_call *call)
{
	struct afs_addr_list *alist;
	__be32 *bp;
	u32 uniquifier, nentries, count;
	int i, ret;

	_enter("{%u,%zu/%u}", call->unmarshall, call->offset, call->count);

again:
	switch (call->unmarshall) {
	case 0:
		call->offset = 0;
		call->unmarshall++;

		/* Extract the returned uuid, uniquifier, nentries and blkaddrs size */
	case 1:
		ret = afs_extract_data(call, call->buffer,
				       sizeof(struct afs_uuid__xdr) + 3 * sizeof(__be32),
				       true);
		if (ret < 0)
			return ret;

		bp = call->buffer + sizeof(struct afs_uuid__xdr);
		uniquifier	= ntohl(*bp++);
		nentries	= ntohl(*bp++);
		count		= ntohl(*bp);

		nentries = min(nentries, count);
		alist = afs_alloc_addrlist(nentries, FS_SERVICE, AFS_FS_PORT);
		if (!alist)
			return -ENOMEM;
		alist->version = uniquifier;
		call->reply[0] = alist;
		call->count = count;
		call->count2 = nentries;
		call->offset = 0;
		call->unmarshall++;

		/* Extract entries */
	case 2:
		count = min(call->count, 4U);
		ret = afs_extract_data(call, call->buffer,
				       count * sizeof(__be32),
				       call->count > 4);
		if (ret < 0)
			return ret;

		alist = call->reply[0];
		bp = call->buffer;
		for (i = 0; i < count; i++)
			if (alist->nr_addrs < call->count2)
				afs_merge_fs_addr4(alist, *bp++);

		call->count -= count;
		if (call->count > 0)
			goto again;
		call->offset = 0;
		call->unmarshall++;
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

static void afs_vl_get_addrs_u_destructor(struct afs_call *call)
{
	afs_put_server(call->net, (struct afs_server *)call->reply[0]);
	kfree(call->reply[1]);
	return afs_flat_call_destructor(call);
}

/*
 * VL.GetAddrsU operation type.
 */
static const struct afs_call_type afs_RXVLGetAddrsU = {
	.name		= "VL.GetAddrsU",
	.deliver	= afs_deliver_vl_get_addrs_u,
	.destructor	= afs_vl_get_addrs_u_destructor,
};

/*
 * Dispatch an operation to get the addresses for a server, where the server is
 * nominated by UUID.
 */
struct afs_addr_list *afs_vl_get_addrs_u(struct afs_net *net,
					 struct afs_addr_cursor *ac,
					 struct key *key,
					 const uuid_t *uuid)
{
	struct afs_ListAddrByAttributes__xdr *r;
	const struct afs_uuid *u = (const struct afs_uuid *)uuid;
	struct afs_call *call;
	__be32 *bp;
	int i;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXVLGetAddrsU,
				   sizeof(__be32) + sizeof(struct afs_ListAddrByAttributes__xdr),
				   sizeof(struct afs_uuid__xdr) + 3 * sizeof(__be32));
	if (!call)
		return ERR_PTR(-ENOMEM);

	call->key = key;
	call->reply[0] = NULL;
	call->ret_reply0 = true;

	/* Marshall the parameters */
	bp = call->request;
	*bp++ = htonl(VLGETADDRSU);
	r = (struct afs_ListAddrByAttributes__xdr *)bp;
	r->Mask		= htonl(AFS_VLADDR_UUID);
	r->ipaddr	= 0;
	r->index	= 0;
	r->spare	= 0;
	r->uuid.time_low			= u->time_low;
	r->uuid.time_mid			= htonl(ntohs(u->time_mid));
	r->uuid.time_hi_and_version		= htonl(ntohs(u->time_hi_and_version));
	r->uuid.clock_seq_hi_and_reserved 	= htonl(u->clock_seq_hi_and_reserved);
	r->uuid.clock_seq_low			= htonl(u->clock_seq_low);
	for (i = 0; i < 6; i++)
		r->uuid.node[i] = ntohl(u->node[i]);

	return (struct afs_addr_list *)afs_make_call(ac, call, GFP_KERNEL, false);
}
