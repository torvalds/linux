/*
 * services/localzone.h - local zones authority service.
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
 * This file contains functions to enable local zone authority service.
 */

#ifndef SERVICES_LOCALZONE_H
#define SERVICES_LOCALZONE_H
#include "util/rbtree.h"
#include "util/locks.h"
#include "util/storage/dnstree.h"
#include "util/module.h"
#include "services/view.h"
struct packed_rrset_data;
struct ub_packed_rrset_key;
struct regional;
struct config_file;
struct edns_data;
struct query_info;
struct sldns_buffer;
struct comm_reply;
struct config_strlist;

/**
 * Local zone type
 * This type determines processing for queries that did not match
 * local-data directly.
 */
enum localzone_type {
	/** unset type, used for unset tag_action elements */
	local_zone_unset = 0,
	/** drop query */
	local_zone_deny,
	/** answer with error */
	local_zone_refuse,
	/** answer nxdomain or nodata */
	local_zone_static,
	/** resolve normally */
	local_zone_transparent,
	/** do not block types at localdata names */
	local_zone_typetransparent,
	/** answer with data at zone apex */
	local_zone_redirect,
	/** remove default AS112 blocking contents for zone
	 * nodefault is used in config not during service. */
	local_zone_nodefault,
	/** log client address, but no block (transparent) */
	local_zone_inform,
	/** log client address, and block (drop) */
	local_zone_inform_deny,
	/** resolve normally, even when there is local data */	
	local_zone_always_transparent,
	/** answer with error, even when there is local data */	
	local_zone_always_refuse,
	/** answer with nxdomain, even when there is local data */
	local_zone_always_nxdomain,
	/** answer not from the view, but global or no-answer */
	local_zone_noview
};

/**
 * Authoritative local zones storage, shared.
 */
struct local_zones {
	/** lock on the localzone tree */
	lock_rw_type lock;
	/** rbtree of struct local_zone */
	rbtree_type ztree;
};

/**
 * Local zone. A locally served authoritative zone.
 */
struct local_zone {
	/** rbtree node, key is name and class */
	rbnode_type node;
	/** parent zone, if any. */
	struct local_zone* parent;

	/** zone name, in uncompressed wireformat */
	uint8_t* name;
	/** length of zone name */
	size_t namelen;
	/** number of labels in zone name */
	int namelabs;
	/** the class of this zone. 
	 * uses 'dclass' to not conflict with c++ keyword class. */
	uint16_t dclass;

	/** lock on the data in the structure
	 * For the node, parent, name, namelen, namelabs, dclass, you
	 * need to also hold the zones_tree lock to change them (or to
	 * delete this zone) */
	lock_rw_type lock;

	/** how to process zone */
	enum localzone_type type;
	/** tag bitlist */
	uint8_t* taglist;
	/** length of the taglist (in bytes) */
	size_t taglen;
	/** netblock addr_tree with struct local_zone_override information
	 * or NULL if there are no override elements */
	struct rbtree_type* override_tree;

	/** in this region the zone's data is allocated.
	 * the struct local_zone itself is malloced. */
	struct regional* region;
	/** local data for this zone
	 * rbtree of struct local_data */
	rbtree_type data;
	/** if data contains zone apex SOA data, this is a ptr to it. */
	struct ub_packed_rrset_key* soa;
};

/**
 * Local data. One domain name, and the RRs to go with it.
 */
struct local_data {
	/** rbtree node, key is name only */
	rbnode_type node;
	/** domain name */
	uint8_t* name;
	/** length of name */
	size_t namelen;
	/** number of labels in name */
	int namelabs;
	/** the data rrsets, with different types, linked list.
	 * If this list is NULL, the node is an empty non-terminal. */
	struct local_rrset* rrsets;
};

/**
 * A local data RRset
 */
struct local_rrset {
	/** next in list */
	struct local_rrset* next;
	/** RRset data item */
	struct ub_packed_rrset_key* rrset;
};

/**
 * Local zone override information
 */
struct local_zone_override {
	/** node in addrtree */
	struct addr_tree_node node;
	/** override for local zone type */
	enum localzone_type type;
};

/**
 * Create local zones storage
 * @return new struct or NULL on error.
 */
struct local_zones* local_zones_create(void);

/**
 * Delete local zones storage
 * @param zones: to delete.
 */
void local_zones_delete(struct local_zones* zones);

/**
 * Apply config settings; setup the local authoritative data. 
 * Takes care of locking.
 * @param zones: is set up.
 * @param cfg: config data.
 * @return false on error.
 */
int local_zones_apply_cfg(struct local_zones* zones, struct config_file* cfg);

/**
 * Compare two local_zone entries in rbtree. Sort hierarchical but not
 * canonical
 * @param z1: zone 1
 * @param z2: zone 2
 * @return: -1, 0, +1 comparison value.
 */
int local_zone_cmp(const void* z1, const void* z2);

/**
 * Compare two local_data entries in rbtree. Sort canonical.
 * @param d1: data 1
 * @param d2: data 2
 * @return: -1, 0, +1 comparison value.
 */
int local_data_cmp(const void* d1, const void* d2);

/**
 * Delete one zone
 * @param z: to delete.
 */
void local_zone_delete(struct local_zone* z);

/**
 * Lookup zone that contains the given name, class and taglist.
 * User must lock the tree or result zone.
 * @param zones: the zones tree
 * @param name: dname to lookup
 * @param len: length of name.
 * @param labs: labelcount of name.
 * @param dclass: class to lookup.
 * @param dtype: type to lookup, if type DS a zone higher is used for zonecuts.
 * @param taglist: taglist to lookup.
 * @param taglen: lenth of taglist.
 * @param ignoretags: lookup zone by name and class, regardless the
 * local-zone's tags.
 * @return closest local_zone or NULL if no covering zone is found.
 */
struct local_zone* local_zones_tags_lookup(struct local_zones* zones, 
	uint8_t* name, size_t len, int labs, uint16_t dclass, uint16_t dtype,
	uint8_t* taglist, size_t taglen, int ignoretags);

/**
 * Lookup zone that contains the given name, class.
 * User must lock the tree or result zone.
 * @param zones: the zones tree
 * @param name: dname to lookup
 * @param len: length of name.
 * @param labs: labelcount of name.
 * @param dclass: class to lookup.
 * @param dtype: type of the record, if type DS then a zone higher up is found
 *   pass 0 to just plain find a zone for a name.
 * @return closest local_zone or NULL if no covering zone is found.
 */
struct local_zone* local_zones_lookup(struct local_zones* zones, 
	uint8_t* name, size_t len, int labs, uint16_t dclass, uint16_t dtype);

/**
 * Debug helper. Print all zones 
 * Takes care of locking.
 * @param zones: the zones tree
 */
void local_zones_print(struct local_zones* zones);

/**
 * Answer authoritatively for local zones.
 * Takes care of locking.
 * @param zones: the stored zones (shared, read only).
 * @param env: the module environment.
 * @param qinfo: query info (parsed).
 * @param edns: edns info (parsed).
 * @param buf: buffer with query ID and flags, also for reply.
 * @param temp: temporary storage region.
 * @param repinfo: source address for checks. may be NULL.
 * @param taglist: taglist for checks. May be NULL.
 * @param taglen: length of the taglist.
 * @param tagactions: local zone actions for tags. May be NULL.
 * @param tagactionssize: length of the tagactions.
 * @param tag_datas: array per tag of strlist with rdata strings. or NULL.
 * @param tag_datas_size: size of tag_datas array.
 * @param tagname: array of tag name strings (for debug output).
 * @param num_tags: number of items in tagname array.
 * @param view: answer using this view. May be NULL.
 * @return true if answer is in buffer. false if query is not answered 
 * by authority data. If the reply should be dropped altogether, the return 
 * value is true, but the buffer is cleared (empty).
 * It can also return true if a non-exact alias answer is found.  In this
 * case qinfo->local_alias points to the corresponding alias RRset but the
 * answer is NOT encoded in buffer.  It's the caller's responsibility to
 * complete the alias chain (if needed) and encode the final set of answer.
 * Data pointed to by qinfo->local_alias is allocated in 'temp' or refers to
 * configuration data.  So the caller will need to make a deep copy of it
 * if it needs to keep it beyond the lifetime of 'temp' or a dynamic update
 * to local zone data.
 */
int local_zones_answer(struct local_zones* zones, struct module_env* env,
	struct query_info* qinfo, struct edns_data* edns, struct sldns_buffer* buf,
	struct regional* temp, struct comm_reply* repinfo, uint8_t* taglist,
	size_t taglen, uint8_t* tagactions, size_t tagactionssize,
	struct config_strlist** tag_datas, size_t tag_datas_size,
	char** tagname, int num_tags, struct view* view);

/**
 * Parse the string into localzone type.
 *
 * @param str: string to parse
 * @param t: local zone type returned here.
 * @return 0 on parse error.
 */
int local_zone_str2type(const char* str, enum localzone_type* t);

/**
 * Print localzone type to a string.  Pointer to a constant string.
 *
 * @param t: local zone type.
 * @return constant string that describes type.
 */
const char* local_zone_type2str(enum localzone_type t);

/**
 * Find zone that with exactly given name, class.
 * User must lock the tree or result zone.
 * @param zones: the zones tree
 * @param name: dname to lookup
 * @param len: length of name.
 * @param labs: labelcount of name.
 * @param dclass: class to lookup.
 * @return the exact local_zone or NULL.
 */
struct local_zone* local_zones_find(struct local_zones* zones, 
	uint8_t* name, size_t len, int labs, uint16_t dclass);

/**
 * Add a new zone. Caller must hold the zones lock.
 * Adjusts the other zones as well (parent pointers) after insertion.
 * The zone must NOT exist (returns NULL and logs error).
 * @param zones: the zones tree
 * @param name: dname to add
 * @param len: length of name.
 * @param labs: labelcount of name.
 * @param dclass: class to add.
 * @param tp: type.
 * @return local_zone or NULL on error, caller must printout memory error.
 */
struct local_zone* local_zones_add_zone(struct local_zones* zones, 
	uint8_t* name, size_t len, int labs, uint16_t dclass, 
	enum localzone_type tp);

/**
 * Delete a zone. Caller must hold the zones lock.
 * Adjusts the other zones as well (parent pointers) after insertion.
 * @param zones: the zones tree
 * @param zone: the zone to delete from tree. Also deletes zone from memory.
 */
void local_zones_del_zone(struct local_zones* zones, struct local_zone* zone);

/**
 * Add RR data into the localzone data.
 * Looks up the zone, if no covering zone, a transparent zone with the
 * name of the RR is created.
 * @param zones: the zones tree. Not locked by caller.
 * @param rr: string with on RR.
 * @return false on failure.
 */
int local_zones_add_RR(struct local_zones* zones, const char* rr);

/**
 * Remove data from domain name in the tree.
 * All types are removed. No effect if zone or name does not exist.
 * @param zones: zones tree.
 * @param name: dname to remove
 * @param len: length of name.
 * @param labs: labelcount of name.
 * @param dclass: class to remove.
 */
void local_zones_del_data(struct local_zones* zones, 
	uint8_t* name, size_t len, int labs, uint16_t dclass);


/** 
 * Form wireformat from text format domain name. 
 * @param str: the domain name in text "www.example.com"
 * @param res: resulting wireformat is stored here with malloc.
 * @param len: length of resulting wireformat.
 * @param labs: number of labels in resulting wireformat.
 * @return false on error, syntax or memory. Also logged.
 */
int parse_dname(const char* str, uint8_t** res, size_t* len, int* labs);

/**
 * Find local data tag string match for the given type (in qinfo) in the list.
 * If found, 'r' will be filled with corresponding rrset information.
 * @param qinfo: contains name, type, and class for the data
 * @param list: stores local tag data to be searched
 * @param r: rrset key to be filled for matched data
 * @param temp: region to allocate rrset in 'r'
 * @return 1 if a match is found and rrset is built; otherwise 0 including
 * errors.
 */
int local_data_find_tag_datas(const struct query_info* qinfo,
	struct config_strlist* list, struct ub_packed_rrset_key* r,
	struct regional* temp);

/**
 * See if two sets of tag lists (in the form of bitmap) have the same tag that
 * has an action.  If so, '*tag' will be set to the found tag index, and the
 * corresponding action will be returned in the form of local zone type.
 * Otherwise the passed type (lzt) will be returned as the default action.
 * Pointers except tagactions must not be NULL.
 * @param taglist: 1st list of tags
 * @param taglen: size of taglist in bytes
 * @param taglist2: 2nd list of tags
 * @param taglen2: size of taglist2 in bytes
 * @param tagactions: local data actions for tags. May be NULL.
 * @param tagactionssize: length of the tagactions.
 * @param lzt: default action (local zone type) if no tag action is found.
 * @param tag: see above.
 * @param tagname: array of tag name strings (for debug output).
 * @param num_tags: number of items in tagname array.
 * @return found tag action or the default action.
 */
enum localzone_type local_data_find_tag_action(const uint8_t* taglist,
	size_t taglen, const uint8_t* taglist2, size_t taglen2,
	const uint8_t* tagactions, size_t tagactionssize,
	enum localzone_type lzt, int* tag, char* const* tagname, int num_tags);

/**
 * Enter defaults to local zone.
 * @param zones: to add defaults to
 * @param cfg: containing list of zones to exclude from default set.
 * @return 1 on success; 0 otherwise.
 */
int local_zone_enter_defaults(struct local_zones* zones,
	struct config_file* cfg);

/**
  * Parses resource record string into wire format, also returning its field values.
  * @param str: input resource record
  * @param nm: domain name field
  * @param type: record type field
  * @param dclass: record class field
  * @param ttl: ttl field
  * @param rr: buffer for the parsed rr in wire format
  * @param len: buffer length
  * @param rdata: rdata field
  * @param rdata_len: rdata field length
  * @return 1 on success; 0 otherwise.
  */
int rrstr_get_rr_content(const char* str, uint8_t** nm, uint16_t* type,
	uint16_t* dclass, time_t* ttl, uint8_t* rr, size_t len,
	uint8_t** rdata, size_t* rdata_len);

/**
  * Insert specified rdata into the specified resource record.
  * @param region: allocator
  * @param pd: data portion of the destination resource record
  * @param rdata: source rdata
  * @param rdata_len: source rdata length
  * @param ttl: time to live
  * @param rrstr: resource record in text form (for logging)
  * @return 1 on success; 0 otherwise.
  */
int rrset_insert_rr(struct regional* region, struct packed_rrset_data* pd,
	uint8_t* rdata, size_t rdata_len, time_t ttl, const char* rrstr);

/**
  * Valid response ip actions for the IP-response-driven-action feature;
  * defined here instead of in the respip module to enable sharing of enum
  * values with the localzone_type enum.
  * Note that these values except 'none' are the same as localzone types of
  * the 'same semantics'.  It's intentional as we use these values via
  * access-control-tags, which can be shared for both response ip actions and
  * local zones.
  */
enum respip_action {
	/** no respip action */
	respip_none = local_zone_unset,
	/** don't answer */
	respip_deny = local_zone_deny,
	/** redirect as per provided data */
	respip_redirect = local_zone_redirect,
        /** log query source and answer query */
	respip_inform = local_zone_inform,
        /** log query source and don't answer query */
	respip_inform_deny = local_zone_inform_deny,
        /** resolve normally, even when there is response-ip data */
	respip_always_transparent = local_zone_always_transparent,
        /** answer with 'refused' response */
	respip_always_refuse = local_zone_always_refuse,
        /** answer with 'no such domain' response */
	respip_always_nxdomain = local_zone_always_nxdomain,

	/* The rest of the values are only possible as
	 * access-control-tag-action */

	/** serves response data (if any), else, drops queries. */
	respip_refuse = local_zone_refuse,
	/** serves response data, else, nodata answer. */
	respip_static = local_zone_static,
	/** gives response data (if any), else nodata answer. */
	respip_transparent = local_zone_transparent,
	/** gives response data (if any), else nodata answer. */
	respip_typetransparent = local_zone_typetransparent,
};

#endif /* SERVICES_LOCALZONE_H */
