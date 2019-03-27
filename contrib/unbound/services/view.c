/*
 * services/view.c - named views containing local zones authority service.
 *
 * Copyright (c) 2016, NLnet Labs. All rights reserved.
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
 * This file contains functions to enable named views that can hold local zone
 * authority service.
 */
#include "config.h"
#include "services/view.h"
#include "services/localzone.h"
#include "util/config_file.h"

int 
view_cmp(const void* v1, const void* v2)
{
	struct view* a = (struct view*)v1;
	struct view* b = (struct view*)v2;
	
	return strcmp(a->name, b->name);
}

struct views* 
views_create(void)
{
	struct views* v = (struct views*)calloc(1, 
		sizeof(*v));
	if(!v)
		return NULL;
	rbtree_init(&v->vtree, &view_cmp);
	lock_rw_init(&v->lock);
	lock_protect(&v->lock, &v->vtree, sizeof(v->vtree));
	return v;
}

/** This prototype is defined in in respip.h, but we want to avoid
  * unnecessary dependencies */
void respip_set_delete(struct respip_set *set);

void 
view_delete(struct view* v)
{
	if(!v)
		return;
	lock_rw_destroy(&v->lock);
	local_zones_delete(v->local_zones);
	respip_set_delete(v->respip_set);
	free(v->name);
	free(v);
}

static void
delviewnode(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct view* v = (struct view*)n;
	view_delete(v);
}

void 
views_delete(struct views* v)
{
	if(!v)
		return;
	lock_rw_destroy(&v->lock);
	traverse_postorder(&v->vtree, delviewnode, NULL);
	free(v);
}

/** create a new view */
static struct view*
view_create(char* name)
{
	struct view* v = (struct view*)calloc(1, sizeof(*v));
	if(!v)
		return NULL;
	v->node.key = v;
	if(!(v->name = strdup(name))) {
		free(v);
		return NULL;
	}
	lock_rw_init(&v->lock);
	lock_protect(&v->lock, &v->name, sizeof(*v)-sizeof(rbnode_type));
	return v;
}

/** enter a new view returns with WRlock */
static struct view*
views_enter_view_name(struct views* vs, char* name)
{
	struct view* v = view_create(name);
	if(!v) {
		log_err("out of memory");
		return NULL;
	}

	/* add to rbtree */
	lock_rw_wrlock(&vs->lock);
	lock_rw_wrlock(&v->lock);
	if(!rbtree_insert(&vs->vtree, &v->node)) {
		log_warn("duplicate view: %s", name);
		lock_rw_unlock(&v->lock);
		view_delete(v);
		lock_rw_unlock(&vs->lock);
		return NULL;
	}
	lock_rw_unlock(&vs->lock);
	return v;
}

int 
views_apply_cfg(struct views* vs, struct config_file* cfg)
{
	struct config_view* cv;
	struct view* v;
	struct config_file lz_cfg;
	/* Check existence of name in first view (last in config). Rest of
	 * views are already checked when parsing config. */
	if(cfg->views && !cfg->views->name) {
		log_err("view without a name");
		return 0;
	}
	for(cv = cfg->views; cv; cv = cv->next) {
		/* create and enter view */
		if(!(v = views_enter_view_name(vs, cv->name)))
			return 0;
		v->isfirst = cv->isfirst;
		if(cv->local_zones || cv->local_data) {
			if(!(v->local_zones = local_zones_create())){
				lock_rw_unlock(&v->lock);
				return 0;
			}
			memset(&lz_cfg, 0, sizeof(lz_cfg));
			lz_cfg.local_zones = cv->local_zones;
			lz_cfg.local_data = cv->local_data;
			lz_cfg.local_zones_nodefault =
				cv->local_zones_nodefault;
			if(v->isfirst) {
				/* Do not add defaults to view-specific
				 * local-zone when global local zone will be
				 * used. */
				struct config_strlist* nd;
				lz_cfg.local_zones_disable_default = 1;
				/* Add nodefault zones to list of zones to add,
				 * so they will be used as if they are
				 * configured as type transparent */
				for(nd = cv->local_zones_nodefault; nd;
					nd = nd->next) {
					char* nd_str, *nd_type;
					nd_str = strdup(nd->str);
					if(!nd_str) {
						log_err("out of memory");
						lock_rw_unlock(&v->lock);
						return 0;
					}
					nd_type = strdup("nodefault");
					if(!nd_type) {
						log_err("out of memory");
						free(nd_str);
						lock_rw_unlock(&v->lock);
						return 0;
					}
					if(!cfg_str2list_insert(
						&lz_cfg.local_zones, nd_str,
						nd_type)) {
						log_err("failed to insert "
							"default zones into "
							"local-zone list");
						free(nd_str);
						free(nd_type);
						lock_rw_unlock(&v->lock);
						return 0;
					}
				}
			}
			if(!local_zones_apply_cfg(v->local_zones, &lz_cfg)){
				lock_rw_unlock(&v->lock);
				return 0;
			}
			/* local_zones, local_zones_nodefault and local_data 
			 * are free'd from config_view by local_zones_apply_cfg.
			 * Set pointers to NULL. */
			cv->local_zones = NULL;
			cv->local_data = NULL;
			cv->local_zones_nodefault = NULL;
		}
		lock_rw_unlock(&v->lock);
	}
	return 1;
}

/** find a view by name */
struct view*
views_find_view(struct views* vs, const char* name, int write)
{
	struct view* v;
	struct view key;
	key.node.key = &v;
	key.name = (char *)name;
	lock_rw_rdlock(&vs->lock);
	if(!(v = (struct view*)rbtree_search(&vs->vtree, &key.node))) {
		lock_rw_unlock(&vs->lock);
		return 0;
	}
	if(write) {
		lock_rw_wrlock(&v->lock);
	} else {
		lock_rw_rdlock(&v->lock);
	}
	lock_rw_unlock(&vs->lock);
	return v;
}

void views_print(struct views* v)
{
	/* TODO implement print */
	(void)v;
}
