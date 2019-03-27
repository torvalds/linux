/*
 * daemon/acl_list.h - client access control storage for the server.
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
 * This file helps the server keep out queries from outside sources, that
 * should not be answered.
 */
#include "config.h"
#include "daemon/acl_list.h"
#include "util/regional.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "services/localzone.h"
#include "sldns/str2wire.h"

struct acl_list* 
acl_list_create(void)
{
	struct acl_list* acl = (struct acl_list*)calloc(1,
		sizeof(struct acl_list));
	if(!acl)
		return NULL;
	acl->region = regional_create();
	if(!acl->region) {
		acl_list_delete(acl);
		return NULL;
	}
	return acl;
}

void 
acl_list_delete(struct acl_list* acl)
{
	if(!acl) 
		return;
	regional_destroy(acl->region);
	free(acl);
}

/** insert new address into acl_list structure */
static struct acl_addr*
acl_list_insert(struct acl_list* acl, struct sockaddr_storage* addr, 
	socklen_t addrlen, int net, enum acl_access control, 
	int complain_duplicates)
{
	struct acl_addr* node = regional_alloc_zero(acl->region,
		sizeof(struct acl_addr));
	if(!node)
		return NULL;
	node->control = control;
	if(!addr_tree_insert(&acl->tree, &node->node, addr, addrlen, net)) {
		if(complain_duplicates)
			verbose(VERB_QUERY, "duplicate acl address ignored.");
	}
	return node;
}

/** apply acl_list string */
static int
acl_list_str_cfg(struct acl_list* acl, const char* str, const char* s2,
	int complain_duplicates)
{
	struct sockaddr_storage addr;
	int net;
	socklen_t addrlen;
	enum acl_access control;
	if(strcmp(s2, "allow") == 0)
		control = acl_allow;
	else if(strcmp(s2, "deny") == 0)
		control = acl_deny;
	else if(strcmp(s2, "refuse") == 0)
		control = acl_refuse;
	else if(strcmp(s2, "deny_non_local") == 0)
		control = acl_deny_non_local;
	else if(strcmp(s2, "refuse_non_local") == 0)
		control = acl_refuse_non_local;
	else if(strcmp(s2, "allow_snoop") == 0)
		control = acl_allow_snoop;
	else if(strcmp(s2, "allow_setrd") == 0)
		control = acl_allow_setrd;
	else {
		log_err("access control type %s unknown", str);
		return 0;
	}
	if(!netblockstrtoaddr(str, UNBOUND_DNS_PORT, &addr, &addrlen, &net)) {
		log_err("cannot parse access control: %s %s", str, s2);
		return 0;
	}
	if(!acl_list_insert(acl, &addr, addrlen, net, control, 
		complain_duplicates)) {
		log_err("out of memory");
		return 0;
	}
	return 1;
}

/** find or create node (NULL on parse or error) */
static struct acl_addr*
acl_find_or_create(struct acl_list* acl, const char* str)
{
	struct acl_addr* node;
	struct sockaddr_storage addr;
	int net;
	socklen_t addrlen;
	if(!netblockstrtoaddr(str, UNBOUND_DNS_PORT, &addr, &addrlen, &net)) {
		log_err("cannot parse netblock: %s", str);
		return NULL;
	}
	/* find or create node */
	if(!(node=(struct acl_addr*)addr_tree_find(&acl->tree, &addr,
		addrlen, net))) {
		/* create node, type 'allow' since otherwise tags are
		 * pointless, can override with specific access-control: cfg */
		if(!(node=(struct acl_addr*)acl_list_insert(acl, &addr,
			addrlen, net, acl_allow, 1))) {
			log_err("out of memory");
			return NULL;
		}
	}
	return node;
}

/** apply acl_tag string */
static int
acl_list_tags_cfg(struct acl_list* acl, const char* str, uint8_t* bitmap,
	size_t bitmaplen)
{
	struct acl_addr* node;
	if(!(node=acl_find_or_create(acl, str)))
		return 0;
	node->taglen = bitmaplen;
	node->taglist = regional_alloc_init(acl->region, bitmap, bitmaplen);
	if(!node->taglist) {
		log_err("out of memory");
		return 0;
	}
	return 1;
}

/** apply acl_view string */
static int
acl_list_view_cfg(struct acl_list* acl, const char* str, const char* str2,
	struct views* vs)
{
	struct acl_addr* node;
	if(!(node=acl_find_or_create(acl, str)))
		return 0;
	node->view = views_find_view(vs, str2, 0 /* get read lock*/);
	if(!node->view) {
		log_err("no view with name: %s", str2);
		return 0;
	}
	lock_rw_unlock(&node->view->lock);
	return 1;
}

/** apply acl_tag_action string */
static int
acl_list_tag_action_cfg(struct acl_list* acl, struct config_file* cfg,
	const char* str, const char* tag, const char* action)
{
	struct acl_addr* node;
	int tagid;
	enum localzone_type t;
	if(!(node=acl_find_or_create(acl, str)))
		return 0;
	/* allocate array if not yet */
	if(!node->tag_actions) {
		node->tag_actions = (uint8_t*)regional_alloc_zero(acl->region,
			sizeof(*node->tag_actions)*cfg->num_tags);
		if(!node->tag_actions) {
			log_err("out of memory");
			return 0;
		}
		node->tag_actions_size = (size_t)cfg->num_tags;
	}
	/* parse tag */
	if((tagid=find_tag_id(cfg, tag)) == -1) {
		log_err("cannot parse tag (define-tag it): %s %s", str, tag);
		return 0;
	}
	if((size_t)tagid >= node->tag_actions_size) {
		log_err("tagid too large for array %s %s", str, tag);
		return 0;
	}
	if(!local_zone_str2type(action, &t)) {
		log_err("cannot parse access control action type: %s %s %s",
			str, tag, action);
		return 0;
	}
	node->tag_actions[tagid] = (uint8_t)t;
	return 1;
}

/** check wire data parse */
static int
check_data(const char* data, const struct config_strlist* head)
{
	char buf[65536];
	uint8_t rr[LDNS_RR_BUF_SIZE];
	size_t len = sizeof(rr);
	int res;
	/* '.' is sufficient for validation, and it makes the call to
	 * sldns_wirerr_get_type() simpler below. */
	snprintf(buf, sizeof(buf), "%s %s", ".", data);
	res = sldns_str2wire_rr_buf(buf, rr, &len, NULL, 3600, NULL, 0,
		NULL, 0);

	/* Reject it if we would end up having CNAME and other data (including
	 * another CNAME) for the same tag. */
	if(res == 0 && head) {
		const char* err_data = NULL;

		if(sldns_wirerr_get_type(rr, len, 1) == LDNS_RR_TYPE_CNAME) {
			/* adding CNAME while other data already exists. */
			err_data = data;
		} else {
			snprintf(buf, sizeof(buf), "%s %s", ".", head->str);
			len = sizeof(rr);
			res = sldns_str2wire_rr_buf(buf, rr, &len, NULL, 3600,
				NULL, 0, NULL, 0);
			if(res != 0) {
				/* This should be impossible here as head->str
				 * has been validated, but we check it just in
				 * case. */
				return 0;
			}
			if(sldns_wirerr_get_type(rr, len, 1) ==
				LDNS_RR_TYPE_CNAME) /* already have CNAME */
				err_data = head->str;
		}
		if(err_data) {
			log_err("redirect tag data '%s' must not coexist with "
				"other data.", err_data);
			return 0;
		}
	}
	if(res == 0)
		return 1;
	log_err("rr data [char %d] parse error %s",
		(int)LDNS_WIREPARSE_OFFSET(res)-13,
		sldns_get_errorstr_parse(res));
	return 0;
}

/** apply acl_tag_data string */
static int
acl_list_tag_data_cfg(struct acl_list* acl, struct config_file* cfg,
	const char* str, const char* tag, const char* data)
{
	struct acl_addr* node;
	int tagid;
	char* dupdata;
	if(!(node=acl_find_or_create(acl, str)))
		return 0;
	/* allocate array if not yet */
	if(!node->tag_datas) {
		node->tag_datas = (struct config_strlist**)regional_alloc_zero(
			acl->region, sizeof(*node->tag_datas)*cfg->num_tags);
		if(!node->tag_datas) {
			log_err("out of memory");
			return 0;
		}
		node->tag_datas_size = (size_t)cfg->num_tags;
	}
	/* parse tag */
	if((tagid=find_tag_id(cfg, tag)) == -1) {
		log_err("cannot parse tag (define-tag it): %s %s", str, tag);
		return 0;
	}
	if((size_t)tagid >= node->tag_datas_size) {
		log_err("tagid too large for array %s %s", str, tag);
		return 0;
	}

	/* check data? */
	if(!check_data(data, node->tag_datas[tagid])) {
		log_err("cannot parse access-control-tag data: %s %s '%s'",
			str, tag, data);
		return 0;
	}

	dupdata = regional_strdup(acl->region, data);
	if(!dupdata) {
		log_err("out of memory");
		return 0;
	}
	if(!cfg_region_strlist_insert(acl->region,
		&(node->tag_datas[tagid]), dupdata)) {
		log_err("out of memory");
		return 0;
	}
	return 1;
}

/** read acl_list config */
static int 
read_acl_list(struct acl_list* acl, struct config_file* cfg)
{
	struct config_str2list* p;
	for(p = cfg->acls; p; p = p->next) {
		log_assert(p->str && p->str2);
		if(!acl_list_str_cfg(acl, p->str, p->str2, 1))
			return 0;
	}
	return 1;
}

/** read acl tags config */
static int 
read_acl_tags(struct acl_list* acl, struct config_file* cfg)
{
	struct config_strbytelist* np, *p = cfg->acl_tags;
	cfg->acl_tags = NULL;
	while(p) {
		log_assert(p->str && p->str2);
		if(!acl_list_tags_cfg(acl, p->str, p->str2, p->str2len)) {
			config_del_strbytelist(p);
			return 0;
		}
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl view config */
static int 
read_acl_view(struct acl_list* acl, struct config_file* cfg, struct views* v)
{
	struct config_str2list* np, *p = cfg->acl_view;
	cfg->acl_view = NULL;
	while(p) {
		log_assert(p->str && p->str2);
		if(!acl_list_view_cfg(acl, p->str, p->str2, v)) {
			return 0;
		}
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl tag actions config */
static int 
read_acl_tag_actions(struct acl_list* acl, struct config_file* cfg)
{
	struct config_str3list* p, *np;
	p = cfg->acl_tag_actions;
	cfg->acl_tag_actions = NULL;
	while(p) {
		log_assert(p->str && p->str2 && p->str3);
		if(!acl_list_tag_action_cfg(acl, cfg, p->str, p->str2,
			p->str3)) {
			config_deltrplstrlist(p);
			return 0;
		}
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p->str3);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl tag datas config */
static int 
read_acl_tag_datas(struct acl_list* acl, struct config_file* cfg)
{
	struct config_str3list* p, *np;
	p = cfg->acl_tag_datas;
	cfg->acl_tag_datas = NULL;
	while(p) {
		log_assert(p->str && p->str2 && p->str3);
		if(!acl_list_tag_data_cfg(acl, cfg, p->str, p->str2, p->str3)) {
			config_deltrplstrlist(p);
			return 0;
		}
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p->str3);
		free(p);
		p = np;
	}
	return 1;
}

int 
acl_list_apply_cfg(struct acl_list* acl, struct config_file* cfg,
	struct views* v)
{
	regional_free_all(acl->region);
	addr_tree_init(&acl->tree);
	if(!read_acl_list(acl, cfg))
		return 0;
	if(!read_acl_view(acl, cfg, v))
		return 0;
	if(!read_acl_tags(acl, cfg))
		return 0;
	if(!read_acl_tag_actions(acl, cfg))
		return 0;
	if(!read_acl_tag_datas(acl, cfg))
		return 0;
	/* insert defaults, with '0' to ignore them if they are duplicates */
	if(!acl_list_str_cfg(acl, "0.0.0.0/0", "refuse", 0))
		return 0;
	if(!acl_list_str_cfg(acl, "127.0.0.0/8", "allow", 0))
		return 0;
	if(cfg->do_ip6) {
		if(!acl_list_str_cfg(acl, "::0/0", "refuse", 0))
			return 0;
		if(!acl_list_str_cfg(acl, "::1", "allow", 0))
			return 0;
		if(!acl_list_str_cfg(acl, "::ffff:127.0.0.1", "allow", 0))
			return 0;
	}
	addr_tree_init_parents(&acl->tree);
	return 1;
}

enum acl_access 
acl_get_control(struct acl_addr* acl)
{
	if(acl) return acl->control;
	return acl_deny;
}

struct acl_addr*
acl_addr_lookup(struct acl_list* acl, struct sockaddr_storage* addr,
        socklen_t addrlen)
{
	return (struct acl_addr*)addr_tree_lookup(&acl->tree,
		addr, addrlen);
}

size_t 
acl_list_get_mem(struct acl_list* acl)
{
	if(!acl) return 0;
	return sizeof(*acl) + regional_get_mem(acl->region);
}
