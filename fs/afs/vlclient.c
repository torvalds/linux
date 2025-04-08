// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS Volume Location Service client
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
	u32 nr_servers, vlflags;
	int i, ret;

	_enter("");

	ret = afs_transfer_reply(call);
	if (ret < 0)
		return ret;

	/* unmarshall the reply once we've received all of it */
	uvldb = call->buffer;
	entry = call->ret_vldb;

	nr_servers = ntohl(uvldb->nServers);
	if (nr_servers > AFS_NMAXNSERVERS)
		nr_servers = AFS_NMAXNSERVERS;

	for (i = 0; i < ARRAY_SIZE(uvldb->name) - 1; i++)
		entry->name[i] = (u8)ntohl(uvldb->name[i]);
	entry->name[i] = 0;
	entry->name_len = strlen(entry->name);

	vlflags = ntohl(uvldb->flags);
	for (i = 0; i < nr_servers; i++) {
		struct afs_uuid__xdr *xdr;
		struct afs_uuid *uuid;
		u32 tmp = ntohl(uvldb->serverFlags[i]);
		int j;
		int n = entry->nr_servers;

		if (tmp & AFS_VLSF_RWVOL) {
			entry->fs_mask[n] |= AFS_VOL_VTM_RW;
			if (vlflags & AFS_VLF_BACKEXISTS)
				entry->fs_mask[n] |= AFS_VOL_VTM_BAK;
		}
		if (tmp & AFS_VLSF_ROVOL)
			entry->fs_mask[n] |= AFS_VOL_VTM_RO;
		if (!entry->fs_mask[n])
			continue;

		xdr = &uvldb->serverNumber[i];
		uuid = (struct afs_uuid *)&entry->fs_server[n];
		uuid->time_low			= xdr->time_low;
		uuid->time_mid			= htons(ntohl(xdr->time_mid));
		uuid->time_hi_and_version	= htons(ntohl(xdr->time_hi_and_version));
		uuid->clock_seq_hi_and_reserved	= (u8)ntohl(xdr->clock_seq_hi_and_reserved);
		uuid->clock_seq_low		= (u8)ntohl(xdr->clock_seq_low);
		for (j = 0; j < 6; j++)
			uuid->node[j] = (u8)ntohl(xdr->node[j]);

		entry->vlsf_flags[n] = tmp;
		entry->addr_version[n] = ntohl(uvldb->serverUnique[i]);
		entry->nr_servers++;
	}

	for (i = 0; i < AFS_MAXTYPES; i++)
		entry->vid[i] = ntohl(uvldb->volumeId[i]);

	if (vlflags & AFS_VLF_RWEXISTS)
		__set_bit(AFS_VLDB_HAS_RW, &entry->flags);
	if (vlflags & AFS_VLF_ROEXISTS)
		__set_bit(AFS_VLDB_HAS_RO, &entry->flags);
	if (vlflags & AFS_VLF_BACKEXISTS)
		__set_bit(AFS_VLDB_HAS_BAK, &entry->flags);

	if (!(vlflags & (AFS_VLF_RWEXISTS | AFS_VLF_ROEXISTS | AFS_VLF_BACKEXISTS))) {
		entry->error = -ENOMEDIUM;
		__set_bit(AFS_VLDB_QUERY_ERROR, &entry->flags);
	}

	__set_bit(AFS_VLDB_QUERY_VALID, &entry->flags);
	_leave(" = 0 [done]");
	return 0;
}

/*
 * VL.GetEntryByNameU operation type.
 */
static const struct afs_call_type afs_RXVLGetEntryByNameU = {
	.name		= "VL.GetEntryByNameU",
	.op		= afs_VL_GetEntryByNameU,
	.deliver	= afs_deliver_vl_get_entry_by_name_u,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Dispatch a get volume entry by name or ID operation (uuid variant).  If the
 * volname is a decimal number then it's a volume ID not a volume name.
 */
struct afs_vldb_entry *afs_vl_get_entry_by_name_u(struct afs_vl_cursor *vc,
						  const char *volname,
						  int volnamesz)
{
	struct afs_vldb_entry *entry;
	struct afs_call *call;
	struct afs_net *net = vc->cell->net;
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

	call->key = vc->key;
	call->ret_vldb = entry;
	call->max_lifespan = AFS_VL_MAX_LIFESPAN;
	call->peer = rxrpc_kernel_get_peer(vc->alist->addrs[vc->addr_index].peer);
	call->service_id = vc->server->service_id;

	/* Marshall the parameters */
	bp = call->request;
	*bp++ = htonl(VLGETENTRYBYNAMEU);
	*bp++ = htonl(volnamesz);
	memcpy(bp, volname, volnamesz);
	if (padsz > 0)
		memset((void *)bp + volnamesz, 0, padsz);

	trace_afs_make_vl_call(call);
	afs_make_call(call, GFP_KERNEL);
	afs_wait_for_call_to_complete(call);
	vc->call_abort_code	= call->abort_code;
	vc->call_error		= call->error;
	vc->call_responded	= call->responded;
	afs_put_call(call);
	if (vc->call_error) {
		kfree(entry);
		return ERR_PTR(vc->call_error);
	}
	return entry;
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

	_enter("{%u,%zu/%u}",
	       call->unmarshall, iov_iter_count(call->iter), call->count);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_buf(call,
				   sizeof(struct afs_uuid__xdr) + 3 * sizeof(__be32));
		call->unmarshall++;

		/* Extract the returned uuid, uniquifier, nentries and
		 * blkaddrs size */
		fallthrough;
	case 1:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer + sizeof(struct afs_uuid__xdr);
		uniquifier	= ntohl(*bp++);
		nentries	= ntohl(*bp++);
		count		= ntohl(*bp);

		nentries = min(nentries, count);
		alist = afs_alloc_addrlist(nentries);
		if (!alist)
			return -ENOMEM;
		alist->version = uniquifier;
		call->ret_alist = alist;
		call->count = count;
		call->count2 = nentries;
		call->unmarshall++;

	more_entries:
		count = min(call->count, 4U);
		afs_extract_to_buf(call, count * sizeof(__be32));

		fallthrough;	/* and extract entries */
	case 2:
		ret = afs_extract_data(call, call->count > 4);
		if (ret < 0)
			return ret;

		alist = call->ret_alist;
		bp = call->buffer;
		count = min(call->count, 4U);
		for (i = 0; i < count; i++) {
			if (alist->nr_addrs < call->count2) {
				ret = afs_merge_fs_addr4(call->net, alist, *bp++, AFS_FS_PORT);
				if (ret < 0)
					return ret;
			}
		}

		call->count -= count;
		if (call->count > 0)
			goto more_entries;
		call->unmarshall++;
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * VL.GetAddrsU operation type.
 */
static const struct afs_call_type afs_RXVLGetAddrsU = {
	.name		= "VL.GetAddrsU",
	.op		= afs_VL_GetAddrsU,
	.deliver	= afs_deliver_vl_get_addrs_u,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Dispatch an operation to get the addresses for a server, where the server is
 * nominated by UUID.
 */
struct afs_addr_list *afs_vl_get_addrs_u(struct afs_vl_cursor *vc,
					 const uuid_t *uuid)
{
	struct afs_ListAddrByAttributes__xdr *r;
	struct afs_addr_list *alist;
	const struct afs_uuid *u = (const struct afs_uuid *)uuid;
	struct afs_call *call;
	struct afs_net *net = vc->cell->net;
	__be32 *bp;
	int i;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXVLGetAddrsU,
				   sizeof(__be32) + sizeof(struct afs_ListAddrByAttributes__xdr),
				   sizeof(struct afs_uuid__xdr) + 3 * sizeof(__be32));
	if (!call)
		return ERR_PTR(-ENOMEM);

	call->key = vc->key;
	call->ret_alist = NULL;
	call->max_lifespan = AFS_VL_MAX_LIFESPAN;
	call->peer = rxrpc_kernel_get_peer(vc->alist->addrs[vc->addr_index].peer);
	call->service_id = vc->server->service_id;

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
		r->uuid.node[i] = htonl(u->node[i]);

	trace_afs_make_vl_call(call);
	afs_make_call(call, GFP_KERNEL);
	afs_wait_for_call_to_complete(call);
	vc->call_abort_code	= call->abort_code;
	vc->call_error		= call->error;
	vc->call_responded	= call->responded;
	alist			= call->ret_alist;
	afs_put_call(call);
	if (vc->call_error) {
		afs_put_addrlist(alist, afs_alist_trace_put_getaddru);
		return ERR_PTR(vc->call_error);
	}
	return alist;
}

/*
 * Deliver reply data to an VL.GetCapabilities operation.
 */
static int afs_deliver_vl_get_capabilities(struct afs_call *call)
{
	u32 count;
	int ret;

	_enter("{%u,%zu/%u}",
	       call->unmarshall, iov_iter_count(call->iter), call->count);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;

		fallthrough;	/* and extract the capabilities word count */
	case 1:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		count = ntohl(call->tmp);
		call->count = count;
		call->count2 = count;

		call->unmarshall++;
		afs_extract_discard(call, count * sizeof(__be32));

		fallthrough;	/* and extract capabilities words */
	case 2:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		/* TODO: Examine capabilities */

		call->unmarshall++;
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

static void afs_destroy_vl_get_capabilities(struct afs_call *call)
{
	afs_put_addrlist(call->vl_probe, afs_alist_trace_put_vlgetcaps);
	afs_put_vlserver(call->net, call->vlserver);
	afs_flat_call_destructor(call);
}

/*
 * VL.GetCapabilities operation type
 */
static const struct afs_call_type afs_RXVLGetCapabilities = {
	.name		= "VL.GetCapabilities",
	.op		= afs_VL_GetCapabilities,
	.deliver	= afs_deliver_vl_get_capabilities,
	.immediate_cancel = afs_vlserver_probe_result,
	.done		= afs_vlserver_probe_result,
	.destructor	= afs_destroy_vl_get_capabilities,
};

/*
 * Probe a volume server for the capabilities that it supports.  This can
 * return up to 196 words.
 *
 * We use this to probe for service upgrade to determine what the server at the
 * other end supports.
 */
struct afs_call *afs_vl_get_capabilities(struct afs_net *net,
					 struct afs_addr_list *alist,
					 unsigned int addr_index,
					 struct key *key,
					 struct afs_vlserver *server,
					 unsigned int server_index)
{
	struct afs_call *call;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_RXVLGetCapabilities, 1 * 4, 16 * 4);
	if (!call)
		return ERR_PTR(-ENOMEM);

	call->key = key;
	call->vlserver = afs_get_vlserver(server);
	call->server_index = server_index;
	call->peer = rxrpc_kernel_get_peer(alist->addrs[addr_index].peer);
	call->vl_probe = afs_get_addrlist(alist, afs_alist_trace_get_vlgetcaps);
	call->probe_index = addr_index;
	call->service_id = server->service_id;
	call->upgrade = true;
	call->async = true;
	call->max_lifespan = AFS_PROBE_MAX_LIFESPAN;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(VLGETCAPABILITIES);

	/* Can't take a ref on server */
	trace_afs_make_vl_call(call);
	afs_make_call(call, GFP_KERNEL);
	return call;
}

/*
 * Deliver reply data to a YFSVL.GetEndpoints call.
 *
 *	GetEndpoints(IN yfsServerAttributes *attr,
 *		     OUT opr_uuid *uuid,
 *		     OUT afs_int32 *uniquifier,
 *		     OUT endpoints *fsEndpoints,
 *		     OUT endpoints *volEndpoints)
 */
static int afs_deliver_yfsvl_get_endpoints(struct afs_call *call)
{
	struct afs_addr_list *alist;
	__be32 *bp;
	u32 uniquifier, size;
	int ret;

	_enter("{%u,%zu,%u}",
	       call->unmarshall, iov_iter_count(call->iter), call->count2);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_buf(call, sizeof(uuid_t) + 3 * sizeof(__be32));
		call->unmarshall = 1;

		/* Extract the returned uuid, uniquifier, fsEndpoints count and
		 * either the first fsEndpoint type or the volEndpoints
		 * count if there are no fsEndpoints. */
		fallthrough;
	case 1:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer + sizeof(uuid_t);
		uniquifier	= ntohl(*bp++);
		call->count	= ntohl(*bp++);
		call->count2	= ntohl(*bp); /* Type or next count */

		if (call->count > YFS_MAXENDPOINTS)
			return afs_protocol_error(call, afs_eproto_yvl_fsendpt_num);

		alist = afs_alloc_addrlist(call->count);
		if (!alist)
			return -ENOMEM;
		alist->version = uniquifier;
		call->ret_alist = alist;

		if (call->count == 0)
			goto extract_volendpoints;

	next_fsendpoint:
		switch (call->count2) {
		case YFS_ENDPOINT_IPV4:
			size = sizeof(__be32) * (1 + 1 + 1);
			break;
		case YFS_ENDPOINT_IPV6:
			size = sizeof(__be32) * (1 + 4 + 1);
			break;
		default:
			return afs_protocol_error(call, afs_eproto_yvl_fsendpt_type);
		}

		size += sizeof(__be32);
		afs_extract_to_buf(call, size);
		call->unmarshall = 2;

		fallthrough;	/* and extract fsEndpoints[] entries */
	case 2:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		alist = call->ret_alist;
		bp = call->buffer;
		switch (call->count2) {
		case YFS_ENDPOINT_IPV4:
			if (ntohl(bp[0]) != sizeof(__be32) * 2)
				return afs_protocol_error(
					call, afs_eproto_yvl_fsendpt4_len);
			ret = afs_merge_fs_addr4(call->net, alist, bp[1], ntohl(bp[2]));
			if (ret < 0)
				return ret;
			bp += 3;
			break;
		case YFS_ENDPOINT_IPV6:
			if (ntohl(bp[0]) != sizeof(__be32) * 5)
				return afs_protocol_error(
					call, afs_eproto_yvl_fsendpt6_len);
			ret = afs_merge_fs_addr6(call->net, alist, bp + 1, ntohl(bp[5]));
			if (ret < 0)
				return ret;
			bp += 6;
			break;
		default:
			return afs_protocol_error(call, afs_eproto_yvl_fsendpt_type);
		}

		/* Got either the type of the next entry or the count of
		 * volEndpoints if no more fsEndpoints.
		 */
		call->count2 = ntohl(*bp++);

		call->count--;
		if (call->count > 0)
			goto next_fsendpoint;

	extract_volendpoints:
		/* Extract the list of volEndpoints. */
		call->count = call->count2;
		if (!call->count)
			goto end;
		if (call->count > YFS_MAXENDPOINTS)
			return afs_protocol_error(call, afs_eproto_yvl_vlendpt_type);

		afs_extract_to_buf(call, 1 * sizeof(__be32));
		call->unmarshall = 3;

		/* Extract the type of volEndpoints[0].  Normally we would
		 * extract the type of the next endpoint when we extract the
		 * data of the current one, but this is the first...
		 */
		fallthrough;
	case 3:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;

	next_volendpoint:
		call->count2 = ntohl(*bp++);
		switch (call->count2) {
		case YFS_ENDPOINT_IPV4:
			size = sizeof(__be32) * (1 + 1 + 1);
			break;
		case YFS_ENDPOINT_IPV6:
			size = sizeof(__be32) * (1 + 4 + 1);
			break;
		default:
			return afs_protocol_error(call, afs_eproto_yvl_vlendpt_type);
		}

		if (call->count > 1)
			size += sizeof(__be32); /* Get next type too */
		afs_extract_to_buf(call, size);
		call->unmarshall = 4;

		fallthrough;	/* and extract volEndpoints[] entries */
	case 4:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		bp = call->buffer;
		switch (call->count2) {
		case YFS_ENDPOINT_IPV4:
			if (ntohl(bp[0]) != sizeof(__be32) * 2)
				return afs_protocol_error(
					call, afs_eproto_yvl_vlendpt4_len);
			bp += 3;
			break;
		case YFS_ENDPOINT_IPV6:
			if (ntohl(bp[0]) != sizeof(__be32) * 5)
				return afs_protocol_error(
					call, afs_eproto_yvl_vlendpt6_len);
			bp += 6;
			break;
		default:
			return afs_protocol_error(call, afs_eproto_yvl_vlendpt_type);
		}

		/* Got either the type of the next entry or the count of
		 * volEndpoints if no more fsEndpoints.
		 */
		call->count--;
		if (call->count > 0)
			goto next_volendpoint;

	end:
		afs_extract_discard(call, 0);
		call->unmarshall = 5;

		fallthrough;	/* Done */
	case 5:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;
		call->unmarshall = 6;
		fallthrough;

	case 6:
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * YFSVL.GetEndpoints operation type.
 */
static const struct afs_call_type afs_YFSVLGetEndpoints = {
	.name		= "YFSVL.GetEndpoints",
	.op		= afs_YFSVL_GetEndpoints,
	.deliver	= afs_deliver_yfsvl_get_endpoints,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Dispatch an operation to get the addresses for a server, where the server is
 * nominated by UUID.
 */
struct afs_addr_list *afs_yfsvl_get_endpoints(struct afs_vl_cursor *vc,
					      const uuid_t *uuid)
{
	struct afs_addr_list *alist;
	struct afs_call *call;
	struct afs_net *net = vc->cell->net;
	__be32 *bp;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_YFSVLGetEndpoints,
				   sizeof(__be32) * 2 + sizeof(*uuid),
				   sizeof(struct in6_addr) + sizeof(__be32) * 3);
	if (!call)
		return ERR_PTR(-ENOMEM);

	call->key = vc->key;
	call->ret_alist = NULL;
	call->max_lifespan = AFS_VL_MAX_LIFESPAN;
	call->peer = rxrpc_kernel_get_peer(vc->alist->addrs[vc->addr_index].peer);
	call->service_id = vc->server->service_id;

	/* Marshall the parameters */
	bp = call->request;
	*bp++ = htonl(YVLGETENDPOINTS);
	*bp++ = htonl(YFS_SERVER_UUID);
	memcpy(bp, uuid, sizeof(*uuid)); /* Type opr_uuid */

	trace_afs_make_vl_call(call);
	afs_make_call(call, GFP_KERNEL);
	afs_wait_for_call_to_complete(call);
	vc->call_abort_code	= call->abort_code;
	vc->call_error		= call->error;
	vc->call_responded	= call->responded;
	alist			= call->ret_alist;
	afs_put_call(call);
	if (vc->call_error) {
		afs_put_addrlist(alist, afs_alist_trace_put_getaddru);
		return ERR_PTR(vc->call_error);
	}
	return alist;
}

/*
 * Deliver reply data to a YFSVL.GetCellName operation.
 */
static int afs_deliver_yfsvl_get_cell_name(struct afs_call *call)
{
	char *cell_name;
	u32 namesz, paddedsz;
	int ret;

	_enter("{%u,%zu/%u}",
	       call->unmarshall, iov_iter_count(call->iter), call->count);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;

		fallthrough;	/* and extract the cell name length */
	case 1:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		namesz = ntohl(call->tmp);
		if (namesz > YFS_VL_MAXCELLNAME)
			return afs_protocol_error(call, afs_eproto_cellname_len);
		paddedsz = (namesz + 3) & ~3;
		call->count = namesz;
		call->count2 = paddedsz - namesz;

		cell_name = kmalloc(namesz + 1, GFP_KERNEL);
		if (!cell_name)
			return -ENOMEM;
		cell_name[namesz] = 0;
		call->ret_str = cell_name;

		afs_extract_begin(call, cell_name, namesz);
		call->unmarshall++;

		fallthrough;	/* and extract cell name */
	case 2:
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		afs_extract_discard(call, call->count2);
		call->unmarshall++;

		fallthrough;	/* and extract padding */
	case 3:
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		call->unmarshall++;
		break;
	}

	_leave(" = 0 [done]");
	return 0;
}

/*
 * VL.GetCapabilities operation type
 */
static const struct afs_call_type afs_YFSVLGetCellName = {
	.name		= "YFSVL.GetCellName",
	.op		= afs_YFSVL_GetCellName,
	.deliver	= afs_deliver_yfsvl_get_cell_name,
	.destructor	= afs_flat_call_destructor,
};

/*
 * Probe a volume server for the capabilities that it supports.  This can
 * return up to 196 words.
 *
 * We use this to probe for service upgrade to determine what the server at the
 * other end supports.
 */
char *afs_yfsvl_get_cell_name(struct afs_vl_cursor *vc)
{
	struct afs_call *call;
	struct afs_net *net = vc->cell->net;
	__be32 *bp;
	char *cellname;

	_enter("");

	call = afs_alloc_flat_call(net, &afs_YFSVLGetCellName, 1 * 4, 0);
	if (!call)
		return ERR_PTR(-ENOMEM);

	call->key = vc->key;
	call->ret_str = NULL;
	call->max_lifespan = AFS_VL_MAX_LIFESPAN;
	call->peer = rxrpc_kernel_get_peer(vc->alist->addrs[vc->addr_index].peer);
	call->service_id = vc->server->service_id;

	/* marshall the parameters */
	bp = call->request;
	*bp++ = htonl(YVLGETCELLNAME);

	/* Can't take a ref on server */
	trace_afs_make_vl_call(call);
	afs_make_call(call, GFP_KERNEL);
	afs_wait_for_call_to_complete(call);
	vc->call_abort_code	= call->abort_code;
	vc->call_error		= call->error;
	vc->call_responded	= call->responded;
	cellname		= call->ret_str;
	afs_put_call(call);
	if (vc->call_error) {
		kfree(cellname);
		return ERR_PTR(vc->call_error);
	}
	return cellname;
}
