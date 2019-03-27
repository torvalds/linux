/*
 * services/view.h - named views containing local zones authority service.
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

#ifndef SERVICES_VIEW_H
#define SERVICES_VIEW_H
#include "util/rbtree.h"
#include "util/locks.h"
struct regional;
struct config_file;
struct config_view;
struct respip_set;


/**
 * Views storage, shared.
 */
struct views {
	/** lock on the view tree */
	lock_rw_type lock;
	/** rbtree of struct view */
	rbtree_type vtree;
};

/**
 * View. Named structure holding local authority zones.
 */
struct view {
	/** rbtree node, key is name */
	rbnode_type node;
	/** view name.
	 * Has to be right after rbnode_t due to pointer arithmetic in
	 * view_create's lock protect */
	char* name;
	/** view specific local authority zones */
	struct local_zones* local_zones;
	/** response-ip configuration data for this view */
	struct respip_set* respip_set;
	/** Fallback to global local_zones when there is no match in the view
	 * specific tree. 1 for yes, 0 for no */	
	int isfirst;
	/** lock on the data in the structure
	 * For the node and name you need to also hold the views_tree lock to
	 * change them. */
	lock_rw_type lock;
};


/**
 * Create views storage
 * @return new struct or NULL on error.
 */
struct views* views_create(void);

/**
 * Delete views storage
 * @param v: views to delete.
 */
void views_delete(struct views* v);

/**
 * Apply config settings;
 * Takes care of locking.
 * @param v: view is set up.
 * @param cfg: config data.
 * @return false on error.
 */
int views_apply_cfg(struct views* v, struct config_file* cfg);

/**
 * Compare two view entries in rbtree. Sort canonical.
 * @param v1: view 1
 * @param v2: view 2
 * @return: negative, positive or 0 comparison value.
 */
int view_cmp(const void* v1, const void* v2);

/**
 * Delete one view
 * @param v: view to delete.
 */
void view_delete(struct view* v);

/**
 * Debug helper. Print all views 
 * Takes care of locking.
 * @param v: the views tree
 */
void views_print(struct views* v);

/* Find a view by name.
 * @param vs: views
 * @param name: name of the view we are looking for
 * @param write: 1 for obtaining write lock on found view, 0 for read lock
 * @return: locked view or NULL. 
 */
struct view* views_find_view(struct views* vs, const char* name, int write);

#endif /* SERVICES_VIEW_H */
