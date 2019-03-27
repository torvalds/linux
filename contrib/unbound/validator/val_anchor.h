/*
 * validator/val_anchor.h - validator trust anchor storage.
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
 * This file contains storage for the trust anchors for the validator.
 */

#ifndef VALIDATOR_VAL_ANCHOR_H
#define VALIDATOR_VAL_ANCHOR_H
#include "util/rbtree.h"
#include "util/locks.h"
struct trust_anchor;
struct config_file;
struct ub_packed_rrset_key;
struct autr_point_data;
struct autr_global_data;
struct sldns_buffer;

/**
 * Trust anchor store.
 * The tree must be locked, while no other locks (from trustanchors) are held.
 * And then an anchor searched for.  Which can be locked or deleted.  Then
 * the tree can be unlocked again.  This means you have to release the lock
 * on a trust anchor and look it up again to delete it.
 */
struct val_anchors {
	/** lock on trees */
	lock_basic_type lock;
	/**
	 * Anchors are store in this tree. Sort order is chosen, so that
	 * dnames are in nsec-like order. A lookup on class, name will return
	 * an exact match of the closest match, with the ancestor needed.
	 * contents of type trust_anchor.
	 */
	rbtree_type* tree;
	/** The DLV trust anchor (if one is configured, else NULL) */
	struct trust_anchor* dlv_anchor;
	/** Autotrust global data, anchors sorted by next probe time */
	struct autr_global_data* autr;
};

/**
 * Trust anchor key
 */
struct ta_key {
	/** next in list */
	struct ta_key* next;
	/** rdata, in wireformat of the key RR. starts with rdlength. */
	uint8_t* data;
	/** length of the rdata (including rdlength). */
	size_t len;
	/** DNS type (host format) of the key, DS or DNSKEY */
	uint16_t type;
};

/**
 * A trust anchor in the trust anchor store.
 * Unique by name, class.
 */
struct trust_anchor {
	/** rbtree node, key is this structure */
	rbnode_type node;
	/** lock on the entire anchor and its keys; for autotrust changes */
	lock_basic_type lock;
	/** name of this trust anchor */
	uint8_t* name;
	/** length of name */
	size_t namelen;
	/** number of labels in name of rrset */
	int namelabs;
	/** the ancestor in the trustanchor tree */
	struct trust_anchor* parent;
	/** 
	 * List of DS or DNSKEY rrs that form the trust anchor.
	 */
	struct ta_key* keylist;
	/** Autotrust anchor point data, or NULL */
	struct autr_point_data* autr;
	/** number of DSs in the keylist */
	size_t numDS;
	/** number of DNSKEYs in the keylist */
	size_t numDNSKEY;
	/** the DS RRset */
	struct ub_packed_rrset_key* ds_rrset;
	/** The DNSKEY RRset */
	struct ub_packed_rrset_key* dnskey_rrset;
	/** class of the trust anchor */
	uint16_t dclass;
};

/**
 * Create trust anchor storage
 * @return new storage or NULL on error.
 */
struct val_anchors* anchors_create(void);

/**
 * Delete trust anchor storage.
 * @param anchors: to delete.
 */
void anchors_delete(struct val_anchors* anchors);

/**
 * Process trust anchor config.
 * @param anchors: struct anchor storage
 * @param cfg: config options.
 * @return 0 on error.
 */
int anchors_apply_cfg(struct val_anchors* anchors, struct config_file* cfg);

/**
 * Recalculate parent pointers.  The caller must hold the lock on the
 * anchors structure (say after removing an item from the rbtree).
 * Caller must not hold any locks on trust anchors.
 * After the call is complete the parent pointers are updated and an item
 * just removed is no longer referenced in parent pointers.
 * @param anchors: the structure to update.
 */
void anchors_init_parents_locked(struct val_anchors* anchors);

/**
 * Given a qname/qclass combination, find the trust anchor closest above it.
 * Or return NULL if none exists.
 *
 * @param anchors: struct anchor storage
 * @param qname: query name, uncompressed wireformat.
 * @param qname_len: length of qname.
 * @param qclass: class to query for.
 * @return the trust anchor or NULL if none is found. The anchor is locked.
 */
struct trust_anchor* anchors_lookup(struct val_anchors* anchors,
	uint8_t* qname, size_t qname_len, uint16_t qclass);

/**
 * Find a trust anchor. Exact matching.
 * @param anchors: anchor storage.
 * @param name: name of trust anchor (wireformat)
 * @param namelabs: labels in name
 * @param namelen: length of name
 * @param dclass: class of trust anchor
 * @return NULL if not found. The anchor is locked.
 */
struct trust_anchor* anchor_find(struct val_anchors* anchors, 
	uint8_t* name, int namelabs, size_t namelen, uint16_t dclass);

/**
 * Store one string as trust anchor RR.
 * @param anchors: anchor storage.
 * @param buffer: parsing buffer, to generate the RR wireformat in.
 * @param str: string.
 * @return NULL on error.
 */
struct trust_anchor* anchor_store_str(struct val_anchors* anchors, 
	struct sldns_buffer* buffer, const char* str);

/**
 * Get memory in use by the trust anchor storage
 * @param anchors: anchor storage.
 * @return memory in use in bytes.
 */
size_t anchors_get_mem(struct val_anchors* anchors);

/** compare two trust anchors */
int anchor_cmp(const void* k1, const void* k2);

/**
 * Add insecure point trust anchor.  For external use (locks and init_parents)
 * @param anchors: anchor storage.
 * @param c: class.
 * @param nm: name of insecure trust point.
 * @return false on alloc failure.
 */
int anchors_add_insecure(struct val_anchors* anchors, uint16_t c, uint8_t* nm);

/**
 * Delete insecure point trust anchor.  Does not remove if no such point.
 * For external use (locks and init_parents)
 * @param anchors: anchor storage.
 * @param c: class.
 * @param nm: name of insecure trust point.
 */
void anchors_delete_insecure(struct val_anchors* anchors, uint16_t c,
	uint8_t* nm);

/**
 * Get a list of keytags for the trust anchor.  Zero tags for insecure points.
 * @param ta: trust anchor (locked by caller).
 * @param list: array of uint16_t.
 * @param num: length of array.
 * @return number of keytags filled into array.  If total number of keytags is
 * bigger than the array, it is truncated at num.  On errors, less keytags
 * are filled in.  The array is sorted.
 */
size_t anchor_list_keytags(struct trust_anchor* ta, uint16_t* list, size_t num);

/**
 * Check if there is a trust anchor for given zone with this keytag.
 *
 * @param anchors: anchor storage
 * @param name: name of trust anchor (wireformat)
 * @param namelabs: labels in name
 * @param namelen: length of name
 * @param dclass: class of trust anchor
 * @param keytag: keytag
 * @return 1 if there is a trust anchor in the trustachor store for this zone
 * and keytag, else 0.
 */
int anchor_has_keytag(struct val_anchors* anchors, uint8_t* name, int namelabs,
	size_t namelen, uint16_t dclass, uint16_t keytag);

#endif /* VALIDATOR_VAL_ANCHOR_H */
