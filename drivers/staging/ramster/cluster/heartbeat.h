/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.h
 *
 * Function prototypes
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#ifndef R2CLUSTER_HEARTBEAT_H
#define R2CLUSTER_HEARTBEAT_H

#define R2HB_REGION_TIMEOUT_MS		2000

#define R2HB_MAX_REGION_NAME_LEN	32

/* number of changes to be seen as live */
#define R2HB_LIVE_THRESHOLD	   2
/* number of equal samples to be seen as dead */
extern unsigned int r2hb_dead_threshold;
#define R2HB_DEFAULT_DEAD_THRESHOLD	   31
/* Otherwise MAX_WRITE_TIMEOUT will be zero... */
#define R2HB_MIN_DEAD_THRESHOLD	  2
#define R2HB_MAX_WRITE_TIMEOUT_MS \
	(R2HB_REGION_TIMEOUT_MS * (r2hb_dead_threshold - 1))

#define R2HB_CB_MAGIC		0x51d1e4ec

/* callback stuff */
enum r2hb_callback_type {
	R2HB_NODE_DOWN_CB = 0,
	R2HB_NODE_UP_CB,
	R2HB_NUM_CB
};

struct r2nm_node;
typedef void (r2hb_cb_func)(struct r2nm_node *, int, void *);

struct r2hb_callback_func {
	u32			hc_magic;
	struct list_head	hc_item;
	r2hb_cb_func		*hc_func;
	void			*hc_data;
	int			hc_priority;
	enum r2hb_callback_type hc_type;
};

struct config_group *r2hb_alloc_hb_set(void);
void r2hb_free_hb_set(struct config_group *group);

void r2hb_setup_callback(struct r2hb_callback_func *hc,
			 enum r2hb_callback_type type,
			 r2hb_cb_func *func,
			 void *data,
			 int priority);
int r2hb_register_callback(const char *region_uuid,
			   struct r2hb_callback_func *hc);
void r2hb_unregister_callback(const char *region_uuid,
			      struct r2hb_callback_func *hc);
void r2hb_fill_node_map(unsigned long *map,
			unsigned bytes);
void r2hb_exit(void);
int r2hb_init(void);
int r2hb_check_node_heartbeating_from_callback(u8 node_num);
void r2hb_stop_all_regions(void);
int r2hb_get_all_regions(char *region_uuids, u8 numregions);
int r2hb_global_heartbeat_active(void);
void r2hb_manual_set_node_heartbeating(int);

#endif /* R2CLUSTER_HEARTBEAT_H */
