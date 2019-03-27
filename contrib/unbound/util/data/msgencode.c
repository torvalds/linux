/*
 * util/data/msgencode.c - Encode DNS messages, queries and replies.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains a routines to encode DNS messages.
 */

#include "config.h"
#include "util/data/msgencode.h"
#include "util/data/msgreply.h"
#include "util/data/msgparse.h"
#include "util/data/dname.h"
#include "util/log.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "sldns/sbuffer.h"
#include "services/localzone.h"

/** return code that means the function ran out of memory. negative so it does
 * not conflict with DNS rcodes. */
#define RETVAL_OUTMEM	-2
/** return code that means the data did not fit (completely) in the packet */
#define RETVAL_TRUNC	-4
/** return code that means all is peachy keen. Equal to DNS rcode NOERROR */
#define RETVAL_OK	0

/**
 * Data structure to help domain name compression in outgoing messages.
 * A tree of dnames and their offsets in the packet is kept.
 * It is kept sorted, not canonical, but by label at least, so that after
 * a lookup of a name you know its closest match, and the parent from that
 * closest match. These are possible compression targets.
 *
 * It is a binary tree, not a rbtree or balanced tree, as the effort
 * of keeping it balanced probably outweighs usefulness (given typical
 * DNS packet size).
 */
struct compress_tree_node {
	/** left node in tree, all smaller to this */
	struct compress_tree_node* left;
	/** right node in tree, all larger than this */
	struct compress_tree_node* right;

	/** the parent node - not for tree, but zone parent. One less label */
	struct compress_tree_node* parent;
	/** the domain name for this node. Pointer to uncompressed memory. */
	uint8_t* dname;
	/** number of labels in domain name, kept to help compare func. */
	int labs;
	/** offset in packet that points to this dname */
	size_t offset;
};

/**
 * Find domain name in tree, returns exact and closest match.
 * @param tree: root of tree.
 * @param dname: pointer to uncompressed dname.
 * @param labs: number of labels in domain name.
 * @param match: closest or exact match.
 *	guaranteed to be smaller or equal to the sought dname.
 *	can be null if the tree is empty.
 * @param matchlabels: number of labels that match with closest match.
 *	can be zero is there is no match.
 * @param insertpt: insert location for dname, if not found.
 * @return: 0 if no exact match.
 */
static int
compress_tree_search(struct compress_tree_node** tree, uint8_t* dname,
	int labs, struct compress_tree_node** match, int* matchlabels,
	struct compress_tree_node*** insertpt)
{
	int c, n, closen=0;
	struct compress_tree_node* p = *tree;
	struct compress_tree_node* close = 0;
	struct compress_tree_node** prev = tree;
	while(p) {
		if((c = dname_lab_cmp(dname, labs, p->dname, p->labs, &n)) 
			== 0) {
			*matchlabels = n;
			*match = p;
			return 1;
		}
		if(c<0) {
			prev = &p->left;
			p = p->left;
		} else	{
			closen = n;
			close = p; /* p->dname is smaller than dname */
			prev = &p->right;
			p = p->right;
		}
	}
	*insertpt = prev;
	*matchlabels = closen;
	*match = close;
	return 0;
}

/**
 * Lookup a domain name in compression tree.
 * @param tree: root of tree (not the node with '.').
 * @param dname: pointer to uncompressed dname.
 * @param labs: number of labels in domain name.
 * @param insertpt: insert location for dname, if not found.
 * @return: 0 if not found or compress treenode with best compression.
 */
static struct compress_tree_node*
compress_tree_lookup(struct compress_tree_node** tree, uint8_t* dname,
	int labs, struct compress_tree_node*** insertpt)
{
	struct compress_tree_node* p;
	int m;
	if(labs <= 1)
		return 0; /* do not compress root node */
	if(compress_tree_search(tree, dname, labs, &p, &m, insertpt)) {
		/* exact match */
		return p;
	}
	/* return some ancestor of p that compresses well. */
	if(m>1) {
		/* www.example.com. (labs=4) matched foo.example.com.(labs=4)
		 * then matchcount = 3. need to go up. */
		while(p && p->labs > m)
			p = p->parent;
		return p;
	}
	return 0;
}

/**
 * Create node for domain name compression tree.
 * @param dname: pointer to uncompressed dname (stored in tree).
 * @param labs: number of labels in dname.
 * @param offset: offset into packet for dname.
 * @param region: how to allocate memory for new node.
 * @return new node or 0 on malloc failure.
 */
static struct compress_tree_node*
compress_tree_newnode(uint8_t* dname, int labs, size_t offset, 
	struct regional* region)
{
	struct compress_tree_node* n = (struct compress_tree_node*)
		regional_alloc(region, sizeof(struct compress_tree_node));
	if(!n) return 0;
	n->left = 0;
	n->right = 0;
	n->parent = 0;
	n->dname = dname;
	n->labs = labs;
	n->offset = offset;
	return n;
}

/**
 * Store domain name and ancestors into compression tree.
 * @param dname: pointer to uncompressed dname (stored in tree).
 * @param labs: number of labels in dname.
 * @param offset: offset into packet for dname.
 * @param region: how to allocate memory for new node.
 * @param closest: match from previous lookup, used to compress dname.
 *	may be NULL if no previous match.
 *	if the tree has an ancestor of dname already, this must be it.
 * @param insertpt: where to insert the dname in tree. 
 * @return: 0 on memory error.
 */
static int
compress_tree_store(uint8_t* dname, int labs, size_t offset, 
	struct regional* region, struct compress_tree_node* closest, 
	struct compress_tree_node** insertpt)
{
	uint8_t lablen;
	struct compress_tree_node* newnode;
	struct compress_tree_node* prevnode = NULL;
	int uplabs = labs-1; /* does not store root in tree */
	if(closest) uplabs = labs - closest->labs;
	log_assert(uplabs >= 0);
	/* algorithms builds up a vine of dname-labels to hang into tree */
	while(uplabs--) {
		if(offset > PTR_MAX_OFFSET) {
			/* insertion failed, drop vine */
			return 1; /* compression pointer no longer useful */
		}
		if(!(newnode = compress_tree_newnode(dname, labs, offset, 
			region))) {
			/* insertion failed, drop vine */
			return 0;
		}

		if(prevnode) {
			/* chain nodes together, last one has one label more,
			 * so is larger than newnode, thus goes right. */
			newnode->right = prevnode;
			prevnode->parent = newnode;
		}

		/* next label */
		lablen = *dname++;
		dname += lablen;
		offset += lablen+1;
		prevnode = newnode;
		labs--;
	}
	/* if we have a vine, hang the vine into the tree */
	if(prevnode) {
		*insertpt = prevnode;
		prevnode->parent = closest;
	}
	return 1;
}

/** compress a domain name */
static int
write_compressed_dname(sldns_buffer* pkt, uint8_t* dname, int labs,
	struct compress_tree_node* p)
{
	/* compress it */
	int labcopy = labs - p->labs;
	uint8_t lablen;
	uint16_t ptr;

	if(labs == 1) {
		/* write root label */
		if(sldns_buffer_remaining(pkt) < 1)
			return 0;
		sldns_buffer_write_u8(pkt, 0);
		return 1;
	}

	/* copy the first couple of labels */
	while(labcopy--) {
		lablen = *dname++;
		if(sldns_buffer_remaining(pkt) < (size_t)lablen+1)
			return 0;
		sldns_buffer_write_u8(pkt, lablen);
		sldns_buffer_write(pkt, dname, lablen);
		dname += lablen;
	}
	/* insert compression ptr */
	if(sldns_buffer_remaining(pkt) < 2)
		return 0;
	ptr = PTR_CREATE(p->offset);
	sldns_buffer_write_u16(pkt, ptr);
	return 1;
}

/** compress owner name of RR, return RETVAL_OUTMEM RETVAL_TRUNC */
static int
compress_owner(struct ub_packed_rrset_key* key, sldns_buffer* pkt, 
	struct regional* region, struct compress_tree_node** tree, 
	size_t owner_pos, uint16_t* owner_ptr, int owner_labs)
{
	struct compress_tree_node* p;
	struct compress_tree_node** insertpt = NULL;
	if(!*owner_ptr) {
		/* compress first time dname */
		if((p = compress_tree_lookup(tree, key->rk.dname, 
			owner_labs, &insertpt))) {
			if(p->labs == owner_labs) 
				/* avoid ptr chains, since some software is
				 * not capable of decoding ptr after a ptr. */
				*owner_ptr = htons(PTR_CREATE(p->offset));
			if(!write_compressed_dname(pkt, key->rk.dname, 
				owner_labs, p))
				return RETVAL_TRUNC;
			/* check if typeclass+4 ttl + rdatalen is available */
			if(sldns_buffer_remaining(pkt) < 4+4+2)
				return RETVAL_TRUNC;
		} else {
			/* no compress */
			if(sldns_buffer_remaining(pkt) < key->rk.dname_len+4+4+2)
				return RETVAL_TRUNC;
			sldns_buffer_write(pkt, key->rk.dname, 
				key->rk.dname_len);
			if(owner_pos <= PTR_MAX_OFFSET)
				*owner_ptr = htons(PTR_CREATE(owner_pos));
		}
		if(!compress_tree_store(key->rk.dname, owner_labs, 
			owner_pos, region, p, insertpt))
			return RETVAL_OUTMEM;
	} else {
		/* always compress 2nd-further RRs in RRset */
		if(owner_labs == 1) {
			if(sldns_buffer_remaining(pkt) < 1+4+4+2) 
				return RETVAL_TRUNC;
			sldns_buffer_write_u8(pkt, 0);
		} else {
			if(sldns_buffer_remaining(pkt) < 2+4+4+2) 
				return RETVAL_TRUNC;
			sldns_buffer_write(pkt, owner_ptr, 2);
		}
	}
	return RETVAL_OK;
}

/** compress any domain name to the packet, return RETVAL_* */
static int
compress_any_dname(uint8_t* dname, sldns_buffer* pkt, int labs, 
	struct regional* region, struct compress_tree_node** tree)
{
	struct compress_tree_node* p;
	struct compress_tree_node** insertpt = NULL;
	size_t pos = sldns_buffer_position(pkt);
	if((p = compress_tree_lookup(tree, dname, labs, &insertpt))) {
		if(!write_compressed_dname(pkt, dname, labs, p))
			return RETVAL_TRUNC;
	} else {
		if(!dname_buffer_write(pkt, dname))
			return RETVAL_TRUNC;
	}
	if(!compress_tree_store(dname, labs, pos, region, p, insertpt))
		return RETVAL_OUTMEM;
	return RETVAL_OK;
}

/** return true if type needs domain name compression in rdata */
static const sldns_rr_descriptor*
type_rdata_compressable(struct ub_packed_rrset_key* key)
{
	uint16_t t = ntohs(key->rk.type);
	if(sldns_rr_descript(t) && 
		sldns_rr_descript(t)->_compress == LDNS_RR_COMPRESS)
		return sldns_rr_descript(t);
	return 0;
}

/** compress domain names in rdata, return RETVAL_* */
static int
compress_rdata(sldns_buffer* pkt, uint8_t* rdata, size_t todolen, 
	struct regional* region, struct compress_tree_node** tree, 
	const sldns_rr_descriptor* desc)
{
	int labs, r, rdf = 0;
	size_t dname_len, len, pos = sldns_buffer_position(pkt);
	uint8_t count = desc->_dname_count;

	sldns_buffer_skip(pkt, 2); /* rdata len fill in later */
	/* space for rdatalen checked for already */
	rdata += 2;
	todolen -= 2;
	while(todolen > 0 && count) {
		switch(desc->_wireformat[rdf]) {
		case LDNS_RDF_TYPE_DNAME:
			labs = dname_count_size_labels(rdata, &dname_len);
			if((r=compress_any_dname(rdata, pkt, labs, region, 
				tree)) != RETVAL_OK)
				return r;
			rdata += dname_len;
			todolen -= dname_len;
			count--;
			len = 0;
			break;
		case LDNS_RDF_TYPE_STR:
			len = *rdata + 1;
			break;
		default:
			len = get_rdf_size(desc->_wireformat[rdf]);
		}
		if(len) {
			/* copy over */
			if(sldns_buffer_remaining(pkt) < len)
				return RETVAL_TRUNC;
			sldns_buffer_write(pkt, rdata, len);
			todolen -= len;
			rdata += len;
		}
		rdf++;
	}
	/* copy remainder */
	if(todolen > 0) {
		if(sldns_buffer_remaining(pkt) < todolen)
			return RETVAL_TRUNC;
		sldns_buffer_write(pkt, rdata, todolen);
	}

	/* set rdata len */
	sldns_buffer_write_u16_at(pkt, pos, sldns_buffer_position(pkt)-pos-2);
	return RETVAL_OK;
}

/** Returns true if RR type should be included */
static int
rrset_belongs_in_reply(sldns_pkt_section s, uint16_t rrtype, uint16_t qtype, 
	int dnssec)
{
	if(dnssec)
		return 1;
	/* skip non DNSSEC types, except if directly queried for */
	if(s == LDNS_SECTION_ANSWER) {
		if(qtype == LDNS_RR_TYPE_ANY || qtype == rrtype)
			return 1;
	}
	/* check DNSSEC-ness */
	switch(rrtype) {
		case LDNS_RR_TYPE_SIG:
		case LDNS_RR_TYPE_KEY:
		case LDNS_RR_TYPE_NXT:
		case LDNS_RR_TYPE_DS:
		case LDNS_RR_TYPE_RRSIG:
		case LDNS_RR_TYPE_NSEC:
		case LDNS_RR_TYPE_DNSKEY:
		case LDNS_RR_TYPE_NSEC3:
		case LDNS_RR_TYPE_NSEC3PARAMS:
			return 0;
	}
	return 1;
}

/** store rrset in buffer in wireformat, return RETVAL_* */
static int
packed_rrset_encode(struct ub_packed_rrset_key* key, sldns_buffer* pkt, 
	uint16_t* num_rrs, time_t timenow, struct regional* region,
	int do_data, int do_sig, struct compress_tree_node** tree,
	sldns_pkt_section s, uint16_t qtype, int dnssec, size_t rr_offset)
{
	size_t i, j, owner_pos;
	int r, owner_labs;
	uint16_t owner_ptr = 0;
	struct packed_rrset_data* data = (struct packed_rrset_data*)
		key->entry.data;
	
	/* does this RR type belong in the answer? */
	if(!rrset_belongs_in_reply(s, ntohs(key->rk.type), qtype, dnssec))
		return RETVAL_OK;

	owner_labs = dname_count_labels(key->rk.dname);
	owner_pos = sldns_buffer_position(pkt);

	/* For an rrset with a fixed TTL, use the rrset's TTL as given */
	if((key->rk.flags & PACKED_RRSET_FIXEDTTL) != 0)
		timenow = 0;

	if(do_data) {
		const sldns_rr_descriptor* c = type_rdata_compressable(key);
		for(i=0; i<data->count; i++) {
			/* rrset roundrobin */
			j = (i + rr_offset) % data->count;
			if((r=compress_owner(key, pkt, region, tree, 
				owner_pos, &owner_ptr, owner_labs))
				!= RETVAL_OK)
				return r;
			sldns_buffer_write(pkt, &key->rk.type, 2);
			sldns_buffer_write(pkt, &key->rk.rrset_class, 2);
			if(data->rr_ttl[j] < timenow)
				sldns_buffer_write_u32(pkt, 0);
			else 	sldns_buffer_write_u32(pkt, 
					data->rr_ttl[j]-timenow);
			if(c) {
				if((r=compress_rdata(pkt, data->rr_data[j],
					data->rr_len[j], region, tree, c))
					!= RETVAL_OK)
					return r;
			} else {
				if(sldns_buffer_remaining(pkt) < data->rr_len[j])
					return RETVAL_TRUNC;
				sldns_buffer_write(pkt, data->rr_data[j],
					data->rr_len[j]);
			}
		}
	}
	/* insert rrsigs */
	if(do_sig && dnssec) {
		size_t total = data->count+data->rrsig_count;
		for(i=data->count; i<total; i++) {
			if(owner_ptr && owner_labs != 1) {
				if(sldns_buffer_remaining(pkt) <
					2+4+4+data->rr_len[i]) 
					return RETVAL_TRUNC;
				sldns_buffer_write(pkt, &owner_ptr, 2);
			} else {
				if((r=compress_any_dname(key->rk.dname, 
					pkt, owner_labs, region, tree))
					!= RETVAL_OK)
					return r;
				if(sldns_buffer_remaining(pkt) < 
					4+4+data->rr_len[i])
					return RETVAL_TRUNC;
			}
			sldns_buffer_write_u16(pkt, LDNS_RR_TYPE_RRSIG);
			sldns_buffer_write(pkt, &key->rk.rrset_class, 2);
			if(data->rr_ttl[i] < timenow)
				sldns_buffer_write_u32(pkt, 0);
			else 	sldns_buffer_write_u32(pkt, 
					data->rr_ttl[i]-timenow);
			/* rrsig rdata cannot be compressed, perform 100+ byte
			 * memcopy. */
			sldns_buffer_write(pkt, data->rr_data[i],
				data->rr_len[i]);
		}
	}
	/* change rrnum only after we are sure it fits */
	if(do_data)
		*num_rrs += data->count;
	if(do_sig && dnssec)
		*num_rrs += data->rrsig_count;

	return RETVAL_OK;
}

/** store msg section in wireformat buffer, return RETVAL_* */
static int
insert_section(struct reply_info* rep, size_t num_rrsets, uint16_t* num_rrs,
	sldns_buffer* pkt, size_t rrsets_before, time_t timenow, 
	struct regional* region, struct compress_tree_node** tree,
	sldns_pkt_section s, uint16_t qtype, int dnssec, size_t rr_offset)
{
	int r;
	size_t i, setstart;
	/* we now allow this function to be called multiple times for the
	 * same section, incrementally updating num_rrs.  The caller is
	 * responsible for initializing it (which is the case in the current
	 * implementation). */

	if(s != LDNS_SECTION_ADDITIONAL) {
		if(s == LDNS_SECTION_ANSWER && qtype == LDNS_RR_TYPE_ANY)
			dnssec = 1; /* include all types in ANY answer */
	  	for(i=0; i<num_rrsets; i++) {
			setstart = sldns_buffer_position(pkt);
			if((r=packed_rrset_encode(rep->rrsets[rrsets_before+i], 
				pkt, num_rrs, timenow, region, 1, 1, tree,
				s, qtype, dnssec, rr_offset))
				!= RETVAL_OK) {
				/* Bad, but if due to size must set TC bit */
				/* trim off the rrset neatly. */
				sldns_buffer_set_position(pkt, setstart);
				return r;
			}
		}
	} else {
	  	for(i=0; i<num_rrsets; i++) {
			setstart = sldns_buffer_position(pkt);
			if((r=packed_rrset_encode(rep->rrsets[rrsets_before+i], 
				pkt, num_rrs, timenow, region, 1, 0, tree,
				s, qtype, dnssec, rr_offset))
				!= RETVAL_OK) {
				sldns_buffer_set_position(pkt, setstart);
				return r;
			}
		}
		if(dnssec)
	  	  for(i=0; i<num_rrsets; i++) {
			setstart = sldns_buffer_position(pkt);
			if((r=packed_rrset_encode(rep->rrsets[rrsets_before+i], 
				pkt, num_rrs, timenow, region, 0, 1, tree,
				s, qtype, dnssec, rr_offset))
				!= RETVAL_OK) {
				sldns_buffer_set_position(pkt, setstart);
				return r;
			}
		  }
	}
	return RETVAL_OK;
}

/** store query section in wireformat buffer, return RETVAL */
static int
insert_query(struct query_info* qinfo, struct compress_tree_node** tree, 
	sldns_buffer* buffer, struct regional* region)
{
	uint8_t* qname = qinfo->local_alias ?
		qinfo->local_alias->rrset->rk.dname : qinfo->qname;
	size_t qname_len = qinfo->local_alias ?
		qinfo->local_alias->rrset->rk.dname_len : qinfo->qname_len;
	if(sldns_buffer_remaining(buffer) < 
		qinfo->qname_len+sizeof(uint16_t)*2)
		return RETVAL_TRUNC; /* buffer too small */
	/* the query is the first name inserted into the tree */
	if(!compress_tree_store(qname, dname_count_labels(qname),
		sldns_buffer_position(buffer), region, NULL, tree))
		return RETVAL_OUTMEM;
	if(sldns_buffer_current(buffer) == qname)
		sldns_buffer_skip(buffer, (ssize_t)qname_len);
	else	sldns_buffer_write(buffer, qname, qname_len);
	sldns_buffer_write_u16(buffer, qinfo->qtype);
	sldns_buffer_write_u16(buffer, qinfo->qclass);
	return RETVAL_OK;
}

static int
positive_answer(struct reply_info* rep, uint16_t qtype) {
	size_t i;
	if (FLAGS_GET_RCODE(rep->flags) != LDNS_RCODE_NOERROR)
		return 0;

	for(i=0;i<rep->an_numrrsets; i++) {
		if(ntohs(rep->rrsets[i]->rk.type) == qtype) {
			/* in case it is a wildcard with DNSSEC, there will
			 * be NSEC/NSEC3 records in the authority section
			 * that we cannot remove */
			for(i=rep->an_numrrsets; i<rep->an_numrrsets+
				rep->ns_numrrsets; i++) {
				if(ntohs(rep->rrsets[i]->rk.type) ==
					LDNS_RR_TYPE_NSEC ||
				   ntohs(rep->rrsets[i]->rk.type) ==
				   	LDNS_RR_TYPE_NSEC3)
					return 0;
			}
			return 1;
		}
	}
	return 0;
}

int 
reply_info_encode(struct query_info* qinfo, struct reply_info* rep, 
	uint16_t id, uint16_t flags, sldns_buffer* buffer, time_t timenow, 
	struct regional* region, uint16_t udpsize, int dnssec)
{
	uint16_t ancount=0, nscount=0, arcount=0;
	struct compress_tree_node* tree = 0;
	int r;
	size_t rr_offset; 

	sldns_buffer_clear(buffer);
	if(udpsize < sldns_buffer_limit(buffer))
		sldns_buffer_set_limit(buffer, udpsize);
	if(sldns_buffer_remaining(buffer) < LDNS_HEADER_SIZE)
		return 0;

	sldns_buffer_write(buffer, &id, sizeof(uint16_t));
	sldns_buffer_write_u16(buffer, flags);
	sldns_buffer_write_u16(buffer, rep->qdcount);
	/* set an, ns, ar counts to zero in case of small packets */
	sldns_buffer_write(buffer, "\000\000\000\000\000\000", 6);

	/* insert query section */
	if(rep->qdcount) {
		if((r=insert_query(qinfo, &tree, buffer, region)) != 
			RETVAL_OK) {
			if(r == RETVAL_TRUNC) {
				/* create truncated message */
				sldns_buffer_write_u16_at(buffer, 4, 0);
				LDNS_TC_SET(sldns_buffer_begin(buffer));
				sldns_buffer_flip(buffer);
				return 1;
			}
			return 0;
		}
	}
	/* roundrobin offset. using query id for random number.  With ntohs
	 * for different roundrobins for sequential id client senders. */
	rr_offset = RRSET_ROUNDROBIN?ntohs(id):0;

	/* "prepend" any local alias records in the answer section if this
	 * response is supposed to be authoritative.  Currently it should
	 * be a single CNAME record (sanity-checked in worker_handle_request())
	 * but it can be extended if and when we support more variations of
	 * aliases. */
	if(qinfo->local_alias && (flags & BIT_AA)) {
		struct reply_info arep;
		time_t timezero = 0; /* to use the 'authoritative' TTL */
		memset(&arep, 0, sizeof(arep));
		arep.flags = rep->flags;
		arep.an_numrrsets = 1;
		arep.rrset_count = 1;
		arep.rrsets = &qinfo->local_alias->rrset;
		if((r=insert_section(&arep, 1, &ancount, buffer, 0,
			timezero, region, &tree, LDNS_SECTION_ANSWER,
			qinfo->qtype, dnssec, rr_offset)) != RETVAL_OK) {
			if(r == RETVAL_TRUNC) {
				/* create truncated message */
				sldns_buffer_write_u16_at(buffer, 6, ancount);
				LDNS_TC_SET(sldns_buffer_begin(buffer));
				sldns_buffer_flip(buffer);
				return 1;
			}
			return 0;
		}
	}

	/* insert answer section */
	if((r=insert_section(rep, rep->an_numrrsets, &ancount, buffer, 
		0, timenow, region, &tree, LDNS_SECTION_ANSWER, qinfo->qtype, 
		dnssec, rr_offset)) != RETVAL_OK) {
		if(r == RETVAL_TRUNC) {
			/* create truncated message */
			sldns_buffer_write_u16_at(buffer, 6, ancount);
			LDNS_TC_SET(sldns_buffer_begin(buffer));
			sldns_buffer_flip(buffer);
			return 1;
		}
		return 0;
	}
	sldns_buffer_write_u16_at(buffer, 6, ancount);

	/* if response is positive answer, auth/add sections are not required */
	if( ! (MINIMAL_RESPONSES && positive_answer(rep, qinfo->qtype)) ) {
		/* insert auth section */
		if((r=insert_section(rep, rep->ns_numrrsets, &nscount, buffer, 
			rep->an_numrrsets, timenow, region, &tree,
			LDNS_SECTION_AUTHORITY, qinfo->qtype,
			dnssec, rr_offset)) != RETVAL_OK) {
			if(r == RETVAL_TRUNC) {
				/* create truncated message */
				sldns_buffer_write_u16_at(buffer, 8, nscount);
				LDNS_TC_SET(sldns_buffer_begin(buffer));
				sldns_buffer_flip(buffer);
				return 1;
			}
			return 0;
		}
		sldns_buffer_write_u16_at(buffer, 8, nscount);

		/* insert add section */
		if((r=insert_section(rep, rep->ar_numrrsets, &arcount, buffer, 
			rep->an_numrrsets + rep->ns_numrrsets, timenow, region, 
			&tree, LDNS_SECTION_ADDITIONAL, qinfo->qtype, 
			dnssec, rr_offset)) != RETVAL_OK) {
			if(r == RETVAL_TRUNC) {
				/* no need to set TC bit, this is the additional */
				sldns_buffer_write_u16_at(buffer, 10, arcount);
				sldns_buffer_flip(buffer);
				return 1;
			}
			return 0;
		}
		sldns_buffer_write_u16_at(buffer, 10, arcount);
	}
	sldns_buffer_flip(buffer);
	return 1;
}

uint16_t
calc_edns_field_size(struct edns_data* edns)
{
	size_t rdatalen = 0;
	struct edns_option* opt;
	if(!edns || !edns->edns_present) 
		return 0;
	for(opt = edns->opt_list; opt; opt = opt->next) {
		rdatalen += 4 + opt->opt_len;
	}
	/* domain root '.' + type + class + ttl + rdatalen */
	return 1 + 2 + 2 + 4 + 2 + rdatalen;
}

void
attach_edns_record(sldns_buffer* pkt, struct edns_data* edns)
{
	size_t len;
	size_t rdatapos;
	struct edns_option* opt;
	if(!edns || !edns->edns_present)
		return;
	/* inc additional count */
	sldns_buffer_write_u16_at(pkt, 10,
		sldns_buffer_read_u16_at(pkt, 10) + 1);
	len = sldns_buffer_limit(pkt);
	sldns_buffer_clear(pkt);
	sldns_buffer_set_position(pkt, len);
	/* write EDNS record */
	sldns_buffer_write_u8(pkt, 0); /* '.' label */
	sldns_buffer_write_u16(pkt, LDNS_RR_TYPE_OPT); /* type */
	sldns_buffer_write_u16(pkt, edns->udp_size); /* class */
	sldns_buffer_write_u8(pkt, edns->ext_rcode); /* ttl */
	sldns_buffer_write_u8(pkt, edns->edns_version);
	sldns_buffer_write_u16(pkt, edns->bits);
	rdatapos = sldns_buffer_position(pkt);
	sldns_buffer_write_u16(pkt, 0); /* rdatalen */
	/* write rdata */
	for(opt=edns->opt_list; opt; opt=opt->next) {
		sldns_buffer_write_u16(pkt, opt->opt_code);
		sldns_buffer_write_u16(pkt, opt->opt_len);
		if(opt->opt_len != 0)
			sldns_buffer_write(pkt, opt->opt_data, opt->opt_len);
	}
	if(edns->opt_list)
		sldns_buffer_write_u16_at(pkt, rdatapos, 
			sldns_buffer_position(pkt)-rdatapos-2);
	sldns_buffer_flip(pkt);
}

int 
reply_info_answer_encode(struct query_info* qinf, struct reply_info* rep, 
	uint16_t id, uint16_t qflags, sldns_buffer* pkt, time_t timenow,
	int cached, struct regional* region, uint16_t udpsize, 
	struct edns_data* edns, int dnssec, int secure)
{
	uint16_t flags;
	unsigned int attach_edns = 0;

	if(!cached || rep->authoritative) {
		/* original flags, copy RD and CD bits from query. */
		flags = rep->flags | (qflags & (BIT_RD|BIT_CD)); 
	} else {
		/* remove AA bit, copy RD and CD bits from query. */
		flags = (rep->flags & ~BIT_AA) | (qflags & (BIT_RD|BIT_CD)); 
	}
	if(secure && (dnssec || (qflags&BIT_AD)))
		flags |= BIT_AD;
	/* restore AA bit if we have a local alias and the response can be
	 * authoritative.  Also clear AD bit if set as the local data is the
	 * primary answer. */
	if(qinf->local_alias &&
		(FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_NOERROR ||
		FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_NXDOMAIN)) {
		flags |= BIT_AA;
		flags &= ~BIT_AD;
	}
	log_assert(flags & BIT_QR); /* QR bit must be on in our replies */
	if(udpsize < LDNS_HEADER_SIZE)
		return 0;
	if(sldns_buffer_capacity(pkt) < udpsize)
		udpsize = sldns_buffer_capacity(pkt);
	if(udpsize < LDNS_HEADER_SIZE + calc_edns_field_size(edns)) {
		/* packet too small to contain edns, omit it. */
		attach_edns = 0;
	} else {
		/* reserve space for edns record */
		attach_edns = (unsigned int)calc_edns_field_size(edns);
		udpsize -= attach_edns;
	}

	if(!reply_info_encode(qinf, rep, id, flags, pkt, timenow, region,
		udpsize, dnssec)) {
		log_err("reply encode: out of memory");
		return 0;
	}
	if(attach_edns && sldns_buffer_capacity(pkt) >=
		sldns_buffer_limit(pkt)+attach_edns)
		attach_edns_record(pkt, edns);
	return 1;
}

void 
qinfo_query_encode(sldns_buffer* pkt, struct query_info* qinfo)
{
	uint16_t flags = 0; /* QUERY, NOERROR */
	const uint8_t* qname = qinfo->local_alias ?
		qinfo->local_alias->rrset->rk.dname : qinfo->qname;
	size_t qname_len = qinfo->local_alias ?
		qinfo->local_alias->rrset->rk.dname_len : qinfo->qname_len;
	sldns_buffer_clear(pkt);
	log_assert(sldns_buffer_remaining(pkt) >= 12+255+4/*max query*/);
	sldns_buffer_skip(pkt, 2); /* id done later */
	sldns_buffer_write_u16(pkt, flags);
	sldns_buffer_write_u16(pkt, 1); /* query count */
	sldns_buffer_write(pkt, "\000\000\000\000\000\000", 6); /* counts */
	sldns_buffer_write(pkt, qname, qname_len);
	sldns_buffer_write_u16(pkt, qinfo->qtype);
	sldns_buffer_write_u16(pkt, qinfo->qclass);
	sldns_buffer_flip(pkt);
}

void 
error_encode(sldns_buffer* buf, int r, struct query_info* qinfo,
	uint16_t qid, uint16_t qflags, struct edns_data* edns)
{
	uint16_t flags;

	sldns_buffer_clear(buf);
	sldns_buffer_write(buf, &qid, sizeof(uint16_t));
	flags = (uint16_t)(BIT_QR | BIT_RA | r); /* QR and retcode*/
	flags |= (qflags & (BIT_RD|BIT_CD)); /* copy RD and CD bit */
	sldns_buffer_write_u16(buf, flags);
	if(qinfo) flags = 1;
	else	flags = 0;
	sldns_buffer_write_u16(buf, flags);
	flags = 0;
	sldns_buffer_write(buf, &flags, sizeof(uint16_t));
	sldns_buffer_write(buf, &flags, sizeof(uint16_t));
	sldns_buffer_write(buf, &flags, sizeof(uint16_t));
	if(qinfo) {
		const uint8_t* qname = qinfo->local_alias ?
			qinfo->local_alias->rrset->rk.dname : qinfo->qname;
		size_t qname_len = qinfo->local_alias ?
			qinfo->local_alias->rrset->rk.dname_len :
			qinfo->qname_len;
		if(sldns_buffer_current(buf) == qname)
			sldns_buffer_skip(buf, (ssize_t)qname_len);
		else	sldns_buffer_write(buf, qname, qname_len);
		sldns_buffer_write_u16(buf, qinfo->qtype);
		sldns_buffer_write_u16(buf, qinfo->qclass);
	}
	sldns_buffer_flip(buf);
	if(edns) {
		struct edns_data es = *edns;
		es.edns_version = EDNS_ADVERTISED_VERSION;
		es.udp_size = EDNS_ADVERTISED_SIZE;
		es.ext_rcode = 0;
		es.bits &= EDNS_DO;
		if(sldns_buffer_limit(buf) + calc_edns_field_size(&es) >
			edns->udp_size)
			return;
		attach_edns_record(buf, &es);
	}
}
