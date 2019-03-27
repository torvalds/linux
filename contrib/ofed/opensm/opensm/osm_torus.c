/*
 * Copyright 2009 Sandia Corporation.  Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
 * certain rights in this software.
 * Copyright (c) 2009-2011 ZIH, TU Dresden, Federal Republic of Germany. All rights reserved.
 * Copyright (c) 2010-2012 Mellanox Technologies LTD. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#define	_WITH_GETLINE	/* for getline() */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_TORUS_C
#include <opensm/osm_log.h>
#include <opensm/osm_port.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_node.h>
#include <opensm/osm_opensm.h>

#define TORUS_MAX_DIM        3
#define PORTGRP_MAX_PORTS    16
#define SWITCH_MAX_PORTGRPS  (1 + 2 * TORUS_MAX_DIM)
#define DEFAULT_MAX_CHANGES  32

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef ib_net64_t guid_t;

/*
 * An endpoint terminates a link, and is one of three types:
 *   UNKNOWN  - Uninitialized endpoint.
 *   SRCSINK  - generates or consumes traffic, and thus has an associated LID;
 *		  i.e. a CA or router port.
 *   PASSTHRU - Has no associated LID; i.e. a switch port.
 *
 * If it is possible to communicate in-band with a switch, it will require
 * a port with a GUID in the switch to source/sink that traffic, but there
 * will be no attached link.  This code assumes there is only one such port.
 *
 * Here is an endpoint taxonomy:
 *
 *   type == SRCSINK
 *   link == pointer to a valid struct link
 *     ==> This endpoint is a CA or router port connected via a link to
 *	     either a switch or another CA/router.  Thus:
 *	   n_id ==> identifies the CA/router node GUID
 *	   sw   ==> NULL
 *	   port ==> identifies the port on the CA/router this endpoint uses
 *	   pgrp ==> NULL
 *
 *   type == SRCSINK
 *   link == NULL pointer
 *     ==> This endpoint is the switch port used for in-band communication
 *	     with the switch itself.  Thus:
 *	   n_id ==> identifies the node GUID used to talk to the switch
 *		      containing this endpoint
 *	   sw   ==> pointer to valid struct switch containing this endpoint
 *	   port ==> identifies the port on the switch this endpoint uses
 *	   pgrp ==> NULL, or pointer to the valid struct port_grp holding
 *		      the port in a t_switch.
 *
 *   type == PASSTHRU
 *   link == pointer to valid struct link
 *     ==> This endpoint is a switch port connected via a link to either
 *	     another switch or a CA/router.  Thus:
 *	   n_id ==> identifies the node GUID used to talk to the switch
 *		      containing this endpoint - since each switch is assumed
 *		      to have only one in-band communication port, this is a
 *		      convenient unique name for the switch itself.
 *	   sw   ==> pointer to valid struct switch containing this endpoint,
 *		      or NULL, in the case of a fabric link that has been
 *		      disconnected after being transferred to a torus link.
 *	   port ==> identifies the port on the switch this endpoint uses.
 *		      Note that in the special case of the coordinate direction
 *		      links, the port value is -1, as those links aren't
 *		      really connected to anything.
 *	   pgrp ==> NULL, or pointer to the valid struct port_grp holding
 *		      the port in a t_switch.
 */
enum endpt_type { UNKNOWN = 0, SRCSINK, PASSTHRU };
struct torus;
struct t_switch;
struct port_grp;

struct endpoint {
	enum endpt_type type;
	int port;
	guid_t n_id;		/* IBA node GUID */
	void *sw;		/* void* can point to either switch type */
	struct link *link;
	struct port_grp *pgrp;
	void *tmp;
	/*
	 * Note: osm_port is only guaranteed to contain a valid pointer
	 * when the call stack contains torus_build_lfts() or
	 * osm_port_relink_endpoint().
	 *
	 * Otherwise, the opensm core could have deleted an osm_port object
	 * without notifying us, invalidating the pointer we hold.
	 *
	 * When presented with a pointer to an osm_port_t, it is generally
	 * safe and required to cast osm_port_t:priv to struct endpoint, and
	 * check that the endpoint's osm_port is the same as the original
	 * osm_port_t pointer.  Failure to do so means that invalidated
	 * pointers will go undetected.
	 */
	struct osm_port *osm_port;
};

struct link {
	struct endpoint end[2];
};

/*
 * A port group is a collection of endpoints on a switch that share certain
 * characteristics.  All the endpoints in a port group must have the same
 * type.  Furthermore, if that type is PASSTHRU, then the connected links:
 *   1) are parallel to a given coordinate direction
 *   2) share the same two switches as endpoints.
 *
 * Torus-2QoS uses one master spanning tree for multicast, of which every
 * multicast group spanning tree is a subtree.  to_stree_root is a pointer
 * to the next port_grp on the path to the master spanning tree root.
 * to_stree_tip is a pointer to the next port_grp on the path to a master
 * spanning tree branch tip.
 *
 * Each t_switch can have at most one port_grp with a non-NULL to_stree_root.
 * Exactly one t_switch in the fabric will have all port_grp objects with
 * to_stree_root NULL; it is the master spanning tree root.
 *
 * A t_switch with all port_grp objects where to_stree_tip is NULL is at a
 * master spanning tree branch tip.
 */
struct port_grp {
	enum endpt_type type;
	size_t port_cnt;	/* number of attached ports in group */
	size_t port_grp;	/* what switch port_grp we're in */
	unsigned sw_dlid_cnt;	/* switch dlids routed through this group */
	unsigned ca_dlid_cnt;	/* CA dlids routed through this group */
	struct t_switch *sw;	/* what switch we're attached to */
	struct port_grp *to_stree_root;
	struct port_grp *to_stree_tip;
	struct endpoint **port;
};

/*
 * A struct t_switch is used to represent a switch as placed in a torus.
 *
 * A t_switch used to build an N-dimensional torus will have 2N+1 port groups,
 * used as follows, assuming 0 <= d < N:
 *   port_grp[2d]   => links leaving in negative direction for coordinate d
 *   port_grp[2d+1] => links leaving in positive direction for coordinate d
 *   port_grp[2N]   => endpoints local to switch; i.e., hosts on switch
 *
 * struct link objects referenced by a t_switch are assumed to be oriented:
 * traversing a link from link.end[0] to link.end[1] is always in the positive
 * coordinate direction.
 */
struct t_switch {
	guid_t n_id;		/* IBA node GUID */
	int i, j, k;
	unsigned port_cnt;	/* including management port */
	struct torus *torus;
	void *tmp;
	/*
	 * Note: osm_switch is only guaranteed to contain a valid pointer
	 * when the call stack contains torus_build_lfts().
	 *
	 * Otherwise, the opensm core could have deleted an osm_switch object
	 * without notifying us, invalidating the pointer we hold.
	 *
	 * When presented with a pointer to an osm_switch_t, it is generally
	 * safe and required to cast osm_switch_t:priv to struct t_switch, and
	 * check that the switch's osm_switch is the same as the original
	 * osm_switch_t pointer.  Failure to do so means that invalidated
	 * pointers will go undetected.
	 */
	struct osm_switch *osm_switch;

	struct port_grp ptgrp[SWITCH_MAX_PORTGRPS];
	struct endpoint **port;
};

/*
 * We'd like to be able to discover the torus topology in a pile of switch
 * links if we can.  We'll use a struct f_switch to store raw topology for a
 * fabric description, then contruct the torus topology from struct t_switch
 * objects as we process the fabric and recover it.
 */
struct f_switch {
	guid_t n_id;		/* IBA node GUID */
	unsigned port_cnt;	/* including management port */
	void *tmp;
	/*
	 * Same rules apply here as for a struct t_switch member osm_switch.
	 */
	struct osm_switch *osm_switch;
	struct endpoint **port;
};

struct fabric {
	osm_opensm_t *osm;
	unsigned ca_cnt;
	unsigned link_cnt;
	unsigned switch_cnt;

	unsigned link_cnt_max;
	unsigned switch_cnt_max;

	struct link **link;
	struct f_switch **sw;
};

struct coord_dirs {
	/*
	 * These links define the coordinate directions for the torus.
	 * They are duplicates of links connected to switches.  Each of
	 * these links must connect to a common switch.
	 *
	 * In the event that a failed switch was specified as one of these
	 * link endpoints, our algorithm would not be able to find the
	 * torus in the fabric.  So, we'll allow multiple instances of
	 * this in the config file to allow improved resiliency.
	 */
	struct link xm_link, ym_link, zm_link;
	struct link xp_link, yp_link, zp_link;
	/*
	 * A torus dimension has coordinate values 0, 1, ..., radix - 1.
	 * The dateline, where we need to change VLs to avoid credit loops,
	 * for a torus dimension is always between coordinate values
	 * radix - 1 and 0.  The following specify the dateline location
	 * relative to the coordinate links shared switch location.
	 *
	 * E.g. if the shared switch is at 0,0,0, the following are all
	 * zero; if the shared switch is at 1,1,1, the following are all
	 * -1, etc.
	 *
	 * Since our SL/VL assignment for a path depends on the position
	 * of the path endpoints relative to the torus datelines, we need
	 * this information to keep SL/VL assignment constant in the event
	 * one of the switches used to specify coordinate directions fails.
	 */
	int x_dateline, y_dateline, z_dateline;
};

struct torus {
	osm_opensm_t *osm;
	unsigned ca_cnt;
	unsigned link_cnt;
	unsigned switch_cnt;
	unsigned seed_cnt, seed_idx;
	unsigned x_sz, y_sz, z_sz;

	unsigned port_order[IB_NODE_NUM_PORTS_MAX+1];

	unsigned sw_pool_sz;
	unsigned link_pool_sz;
	unsigned seed_sz;
	unsigned portgrp_sz;	/* max ports for port groups in this torus */

	struct fabric *fabric;
	struct t_switch **sw_pool;
	struct link *link_pool;

	struct coord_dirs *seed;
	struct t_switch ****sw;
	struct t_switch *master_stree_root;

	unsigned flags;
	unsigned max_changes;
	int debug;
};

/*
 * Bits to use in torus.flags
 */
#define X_MESH (1U << 0)
#define Y_MESH (1U << 1)
#define Z_MESH (1U << 2)
#define MSG_DEADLOCK (1U << 29)
#define NOTIFY_CHANGES (1U << 30)

#define ALL_MESH(flags) \
	((flags & (X_MESH | Y_MESH | Z_MESH)) == (X_MESH | Y_MESH | Z_MESH))


struct torus_context {
	osm_opensm_t *osm;
	struct torus *torus;
	struct fabric fabric;
};

static
void teardown_fabric(struct fabric *f)
{
	unsigned l, p, s;
	struct endpoint *port;
	struct f_switch *sw;

	if (!f)
		return;

	if (f->sw) {
		/*
		 * Need to free switches, and also find/free the endpoints
		 * we allocated for switch management ports.
		 */
		for (s = 0; s < f->switch_cnt; s++) {
			sw = f->sw[s];
			if (!sw)
				continue;

			for (p = 0; p < sw->port_cnt; p++) {
				port = sw->port[p];
				if (port && !port->link)
					free(port);	/* management port */
			}
			free(sw);
		}
		free(f->sw);
	}
	if (f->link) {
		for (l = 0; l < f->link_cnt; l++)
			if (f->link[l])
				free(f->link[l]);

		free(f->link);
	}
	memset(f, 0, sizeof(*f));
}

void teardown_torus(struct torus *t)
{
	unsigned p, s;
	struct endpoint *port;
	struct t_switch *sw;

	if (!t)
		return;

	if (t->sw_pool) {
		/*
		 * Need to free switches, and also find/free the endpoints
		 * we allocated for switch management ports.
		 */
		for (s = 0; s < t->switch_cnt; s++) {
			sw = t->sw_pool[s];
			if (!sw)
				continue;

			for (p = 0; p < sw->port_cnt; p++) {
				port = sw->port[p];
				if (port && !port->link)
					free(port);	/* management port */
			}
			free(sw);
		}
		free(t->sw_pool);
	}
	if (t->link_pool)
		free(t->link_pool);

	if (t->sw)
		free(t->sw);

	if (t->seed)
		free(t->seed);

	free(t);
}

static
struct torus_context *torus_context_create(osm_opensm_t *osm)
{
	struct torus_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx)
		ctx->osm = osm;
	else
		OSM_LOG(&osm->log, OSM_LOG_ERROR,
			"ERR 4E01: calloc: %s\n", strerror(errno));

	return ctx;
}

static
void torus_context_delete(void *context)
{
	struct torus_context *ctx = context;

	teardown_fabric(&ctx->fabric);
	if (ctx->torus)
		teardown_torus(ctx->torus);
	free(ctx);
}

static
bool grow_seed_array(struct torus *t, int new_seeds)
{
	unsigned cnt;
	void *ptr;

	cnt = t->seed_cnt + new_seeds;
	if (cnt > t->seed_sz) {
		cnt += 2 + cnt / 2;
		ptr = realloc(t->seed, cnt * sizeof(*t->seed));
		if (!ptr)
			return false;
		t->seed = ptr;
		t->seed_sz = cnt;
		memset(&t->seed[t->seed_cnt], 0,
		       (cnt - t->seed_cnt) * sizeof(*t->seed));
	}
	return true;
}

static
struct f_switch *find_f_sw(struct fabric *f, guid_t sw_guid)
{
	unsigned s;
	struct f_switch *sw;

	if (f->sw) {
		for (s = 0; s < f->switch_cnt; s++) {
			sw = f->sw[s];
			if (sw->n_id == sw_guid)
				return sw;
		}
	}
	return NULL;
}

static
struct link *find_f_link(struct fabric *f,
			 guid_t guid0, int port0, guid_t guid1, int port1)
{
	unsigned l;
	struct link *link;

	if (f->link) {
		for (l = 0; l < f->link_cnt; l++) {
			link = f->link[l];
			if ((link->end[0].n_id == guid0 &&
			     link->end[0].port == port0 &&
			     link->end[1].n_id == guid1 &&
			     link->end[1].port == port1) ||
			    (link->end[0].n_id == guid1 &&
			     link->end[0].port == port1 &&
			     link->end[1].n_id == guid0 &&
			     link->end[1].port == port0))
				return link;
		}
	}
	return NULL;
}

static
struct f_switch *alloc_fswitch(struct fabric *f,
			       guid_t sw_id, unsigned port_cnt)
{
	size_t new_sw_sz;
	unsigned cnt_max;
	struct f_switch *sw = NULL;
	void *ptr;

	if (f->switch_cnt >= f->switch_cnt_max) {

		cnt_max = 16 + 5 * f->switch_cnt_max / 4;
		ptr = realloc(f->sw, cnt_max * sizeof(*f->sw));
		if (!ptr) {
			OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
				"ERR 4E02: realloc: %s\n", strerror(errno));
			goto out;
		}
		f->sw = ptr;
		f->switch_cnt_max = cnt_max;
		memset(&f->sw[f->switch_cnt], 0,
		       (f->switch_cnt_max - f->switch_cnt)*sizeof(*f->sw));
	}
	new_sw_sz = sizeof(*sw) + port_cnt * sizeof(*sw->port);
	sw = calloc(1, new_sw_sz);
	if (!sw) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"ERR 4E03: calloc: %s\n", strerror(errno));
		goto out;
	}
	sw->port = (void *)(sw + 1);
	sw->n_id = sw_id;
	sw->port_cnt = port_cnt;
	f->sw[f->switch_cnt++] = sw;
out:
	return sw;
}

static
struct link *alloc_flink(struct fabric *f)
{
	unsigned cnt_max;
	struct link *l = NULL;
	void *ptr;

	if (f->link_cnt >= f->link_cnt_max) {

		cnt_max = 16 + 5 * f->link_cnt_max / 4;
		ptr = realloc(f->link, cnt_max * sizeof(*f->link));
		if (!ptr) {
			OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
				"ERR 4E04: realloc: %s\n", strerror(errno));
			goto out;
		}
		f->link = ptr;
		f->link_cnt_max = cnt_max;
		memset(&f->link[f->link_cnt], 0,
		       (f->link_cnt_max - f->link_cnt) * sizeof(*f->link));
	}
	l = calloc(1, sizeof(*l));
	if (!l) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"ERR 4E05: calloc: %s\n", strerror(errno));
		goto out;
	}
	f->link[f->link_cnt++] = l;
out:
	return l;
}

/*
 * Caller must ensure osm_port points to a valid port which contains
 * a valid osm_physp_t pointer for port 0, the switch management port.
 */
static
bool build_sw_endpoint(struct fabric *f, osm_port_t *osm_port)
{
	int sw_port;
	guid_t sw_guid;
	struct osm_switch *osm_sw;
	struct f_switch *sw;
	struct endpoint *ep;
	bool success = false;

	sw_port = osm_physp_get_port_num(osm_port->p_physp);
	sw_guid = osm_node_get_node_guid(osm_port->p_node);
	osm_sw = osm_port->p_node->sw;

	/*
	 * The switch must already exist.
	 */
	sw = find_f_sw(f, sw_guid);
	if (!sw) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"ERR 4E06: missing switch w/GUID 0x%04"PRIx64"\n",
			cl_ntoh64(sw_guid));
		goto out;
	}
	/*
	 * The endpoint may already exist.
	 */
	if (sw->port[sw_port]) {
		if (sw->port[sw_port]->n_id == sw_guid) {
			ep = sw->port[sw_port];
			goto success;
		} else
			OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
				"ERR 4E07: switch port %d has id "
				"0x%04"PRIx64", expected 0x%04"PRIx64"\n",
				sw_port, cl_ntoh64(sw->port[sw_port]->n_id),
				cl_ntoh64(sw_guid));
		goto out;
	}
	ep = calloc(1, sizeof(*ep));
	if (!ep) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"ERR 4E08: allocating endpoint: %s\n", strerror(errno));
		goto out;
	}
	ep->type = SRCSINK;
	ep->port = sw_port;
	ep->n_id = sw_guid;
	ep->link = NULL;
	ep->sw = sw;

	sw->port[sw_port] = ep;

success:
	/*
	 * Fabric objects are temporary, so don't set osm_sw/osm_port priv
	 * pointers using them.  Wait until torus objects get constructed.
	 */
	sw->osm_switch = osm_sw;
	ep->osm_port = osm_port;

	success = true;
out:
	return success;
}

static
bool build_ca_link(struct fabric *f,
		   osm_port_t *osm_port_ca, guid_t sw_guid, int sw_port)
{
	int ca_port;
	guid_t ca_guid;
	struct link *l;
	struct f_switch *sw;
	bool success = false;

	ca_port = osm_physp_get_port_num(osm_port_ca->p_physp);
	ca_guid = osm_node_get_node_guid(osm_port_ca->p_node);

	/*
	 * The link may already exist.
	 */
	l = find_f_link(f, sw_guid, sw_port, ca_guid, ca_port);
	if (l) {
		success = true;
		goto out;
	}
	/*
	 * The switch must already exist.
	 */
	sw = find_f_sw(f, sw_guid);
	if (!sw) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"ERR 4E09: missing switch w/GUID 0x%04"PRIx64"\n",
			cl_ntoh64(sw_guid));
		goto out;
	}
	l = alloc_flink(f);
	if (!l)
		goto out;

	l->end[0].type = PASSTHRU;
	l->end[0].port = sw_port;
	l->end[0].n_id = sw_guid;
	l->end[0].sw = sw;
	l->end[0].link = l;

	sw->port[sw_port] = &l->end[0];

	l->end[1].type = SRCSINK;
	l->end[1].port = ca_port;
	l->end[1].n_id = ca_guid;
	l->end[1].sw = NULL;		/* Correct for a CA */
	l->end[1].link = l;

	/*
	 * Fabric objects are temporary, so don't set osm_sw/osm_port priv
	 * pointers using them.  Wait until torus objects get constructed.
	 */
	l->end[1].osm_port = osm_port_ca;

	++f->ca_cnt;
	success = true;
out:
	return success;
}

static
bool build_link(struct fabric *f,
		guid_t sw_guid0, int sw_port0, guid_t sw_guid1, int sw_port1)
{
	struct link *l;
	struct f_switch *sw0, *sw1;
	bool success = false;

	/*
	 * The link may already exist.
	 */
	l = find_f_link(f, sw_guid0, sw_port0, sw_guid1, sw_port1);
	if (l) {
		success = true;
		goto out;
	}
	/*
	 * The switches must already exist.
	 */
	sw0 = find_f_sw(f, sw_guid0);
	if (!sw0) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"ERR 4E0A: missing switch w/GUID 0x%04"PRIx64"\n",
			cl_ntoh64(sw_guid0));
		goto out;
	}
	sw1 = find_f_sw(f, sw_guid1);
	if (!sw1) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"ERR 4E0B: missing switch w/GUID 0x%04"PRIx64"\n",
			cl_ntoh64(sw_guid1));
		goto out;
	}
	l = alloc_flink(f);
	if (!l)
		goto out;

	l->end[0].type = PASSTHRU;
	l->end[0].port = sw_port0;
	l->end[0].n_id = sw_guid0;
	l->end[0].sw = sw0;
	l->end[0].link = l;

	sw0->port[sw_port0] = &l->end[0];

	l->end[1].type = PASSTHRU;
	l->end[1].port = sw_port1;
	l->end[1].n_id = sw_guid1;
	l->end[1].sw = sw1;
	l->end[1].link = l;

	sw1->port[sw_port1] = &l->end[1];

	success = true;
out:
	return success;
}

static
bool parse_size(unsigned *tsz, unsigned *tflags, unsigned mask,
		const char *parse_sep)
{
	char *val, *nextchar;

	val = strtok(NULL, parse_sep);
	if (!val)
		return false;
	*tsz = strtoul(val, &nextchar, 0);
	if (*tsz) {
		if (*nextchar == 't' || *nextchar == 'T')
			*tflags &= ~mask;
		else if (*nextchar == 'm' || *nextchar == 'M')
			*tflags |= mask;
		/*
		 * A torus of radix two is also a mesh of radix two
		 * with multiple links between switches in that direction.
		 *
		 * Make it so always, otherwise the failure case routing
		 * logic gets confused.
		 */
		if (*tsz == 2)
			*tflags |= mask;
	}
	return true;
}

static
bool parse_torus(struct torus *t, const char *parse_sep)
{
	unsigned i, j, k, cnt;
	char *ptr;
	bool success = false;

	/*
	 * There can be only one.  Ignore the imposters.
	 */
	if (t->sw_pool)
		goto out;

	if (!parse_size(&t->x_sz, &t->flags, X_MESH, parse_sep))
		goto out;

	if (!parse_size(&t->y_sz, &t->flags, Y_MESH, parse_sep))
		goto out;

	if (!parse_size(&t->z_sz, &t->flags, Z_MESH, parse_sep))
		goto out;

	/*
	 * Set up a linear array of switch pointers big enough to hold
	 * all expected switches.
	 */
	t->sw_pool_sz = t->x_sz * t->y_sz * t->z_sz;
	t->sw_pool = calloc(t->sw_pool_sz, sizeof(*t->sw_pool));
	if (!t->sw_pool) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E0C: Torus switch array calloc: %s\n",
			strerror(errno));
		goto out;
	}
	/*
	 * Set things up so that t->sw[i][j][k] can point to the i,j,k switch.
	 */
	cnt = t->x_sz * (1 + t->y_sz * (1 + t->z_sz));
	t->sw = malloc(cnt * sizeof(void *));
	if (!t->sw) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E0D: Torus switch array malloc: %s\n",
			strerror(errno));
		goto out;
	}
	ptr = (void *)(t->sw);

	ptr += t->x_sz * sizeof(void *);
	for (i = 0; i < t->x_sz; i++) {
		t->sw[i] = (void *)ptr;
		ptr += t->y_sz * sizeof(void *);
	}
	for (i = 0; i < t->x_sz; i++)
		for (j = 0; j < t->y_sz; j++) {
			t->sw[i][j] = (void *)ptr;
			ptr += t->z_sz * sizeof(void *);
		}

	for (i = 0; i < t->x_sz; i++)
		for (j = 0; j < t->y_sz; j++)
			for (k = 0; k < t->z_sz; k++)
				t->sw[i][j][k] = NULL;

	success = true;
out:
	return success;
}

static
bool parse_unsigned(unsigned *result, const char *parse_sep)
{
	char *val, *nextchar;

	val = strtok(NULL, parse_sep);
	if (!val)
		return false;
	*result = strtoul(val, &nextchar, 0);
	return true;
}

static
bool parse_port_order(struct torus *t, const char *parse_sep)
{
	unsigned i, j, k, n;

	for (i = 0; i < ARRAY_SIZE(t->port_order); i++) {
		if (!parse_unsigned(&(t->port_order[i]), parse_sep))
			break;

		for (j = 0; j < i; j++) {
			if (t->port_order[j] == t->port_order[i]) {
				OSM_LOG(&t->osm->log, OSM_LOG_INFO,
					"Ignored duplicate port %u in"
					" port_order parsing\n",
					t->port_order[j]);
				i--;	/* Ignore duplicate port number */
				break;
			}
		}
	}

	n = i;
	for (j = 0; j < ARRAY_SIZE(t->port_order); j++) {
		for (k = 0; k < i; k++)
			if (t->port_order[k] == j)
				break;
		if (k >= i)
			t->port_order[n++] = j;
	}

	return true;
}

static
bool parse_guid(struct torus *t, guid_t *guid, const char *parse_sep)
{
	char *val;
	bool success = false;

	val = strtok(NULL, parse_sep);
	if (!val)
		goto out;
	*guid = strtoull(val, NULL, 0);
	*guid = cl_hton64(*guid);

	success = true;
out:
	return success;
}

static
bool parse_dir_link(int c_dir, struct torus *t, const char *parse_sep)
{
	guid_t sw_guid0, sw_guid1;
	struct link *l;
	bool success = false;

	if (!parse_guid(t, &sw_guid0, parse_sep))
		goto out;

	if (!parse_guid(t, &sw_guid1, parse_sep))
		goto out;

	if (!t) {
		success = true;
		goto out;
	}

	switch (c_dir) {
	case -1:
		l = &t->seed[t->seed_cnt - 1].xm_link;
		break;
	case  1:
		l = &t->seed[t->seed_cnt - 1].xp_link;
		break;
	case -2:
		l = &t->seed[t->seed_cnt - 1].ym_link;
		break;
	case  2:
		l = &t->seed[t->seed_cnt - 1].yp_link;
		break;
	case -3:
		l = &t->seed[t->seed_cnt - 1].zm_link;
		break;
	case  3:
		l = &t->seed[t->seed_cnt - 1].zp_link;
		break;
	default:
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E0E: unknown link direction %d\n", c_dir);
		goto out;
	}
	l->end[0].type = PASSTHRU;
	l->end[0].port = -1;		/* We don't really connect. */
	l->end[0].n_id = sw_guid0;
	l->end[0].sw = NULL;		/* Fix this up later. */
	l->end[0].link = NULL;		/* Fix this up later. */

	l->end[1].type = PASSTHRU;
	l->end[1].port = -1;		/* We don't really connect. */
	l->end[1].n_id = sw_guid1;
	l->end[1].sw = NULL;		/* Fix this up later. */
	l->end[1].link = NULL;		/* Fix this up later. */

	success = true;
out:
	return success;
}

static
bool parse_dir_dateline(int c_dir, struct torus *t, const char *parse_sep)
{
	char *val;
	int *dl, max_dl;
	bool success = false;

	val = strtok(NULL, parse_sep);
	if (!val)
		goto out;

	if (!t) {
		success = true;
		goto out;
	}

	switch (c_dir) {
	case  1:
		dl = &t->seed[t->seed_cnt - 1].x_dateline;
		max_dl = t->x_sz;
		break;
	case  2:
		dl = &t->seed[t->seed_cnt - 1].y_dateline;
		max_dl = t->y_sz;
		break;
	case  3:
		dl = &t->seed[t->seed_cnt - 1].z_dateline;
		max_dl = t->z_sz;
		break;
	default:
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E0F: unknown dateline direction %d\n", c_dir);
		goto out;
	}
	*dl = strtol(val, NULL, 0);

	if ((*dl < 0 && *dl <= -max_dl) || *dl >= max_dl)
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E10: dateline value for coordinate direction %d "
			"must be %d < dl < %d\n",
			c_dir, -max_dl, max_dl);
	else
		success = true;
out:
	return success;
}

static
bool parse_config(const char *fn, struct fabric *f, struct torus *t)
{
	FILE *fp;
	unsigned i;
	char *keyword;
	char *line_buf = NULL;
	const char *parse_sep = " \n\t\015";
	size_t line_buf_sz = 0;
	size_t line_cntr = 0;
	ssize_t llen;
	bool kw_success, success = true;

	if (!grow_seed_array(t, 2))
		return false;

	for (i = 0; i < ARRAY_SIZE(t->port_order); i++)
		t->port_order[i] = i;

	fp = fopen(fn, "r");
	if (!fp) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E11: Opening %s: %s\n", fn, strerror(errno));
		return false;
	}
	t->flags |= NOTIFY_CHANGES;
	t->portgrp_sz = PORTGRP_MAX_PORTS;
	t->max_changes = DEFAULT_MAX_CHANGES;

next_line:
	llen = getline(&line_buf, &line_buf_sz, fp);
	if (llen < 0)
		goto out;

	++line_cntr;

	keyword = strtok(line_buf, parse_sep);
	if (!keyword)
		goto next_line;

	if (strcmp("torus", keyword) == 0) {
		kw_success = parse_torus(t, parse_sep);
	} else if (strcmp("mesh", keyword) == 0) {
		t->flags |= X_MESH | Y_MESH | Z_MESH;
		kw_success = parse_torus(t, parse_sep);
	} else if (strcmp("port_order", keyword) == 0) {
		kw_success = parse_port_order(t, parse_sep);
	} else if (strcmp("next_seed", keyword) == 0) {
		kw_success = grow_seed_array(t, 1);
		t->seed_cnt++;
	} else if (strcmp("portgroup_max_ports", keyword) == 0) {
		kw_success = parse_unsigned(&t->portgrp_sz, parse_sep);
	} else if (strcmp("xp_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(1, t, parse_sep);
	} else if (strcmp("xm_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(-1, t, parse_sep);
	} else if (strcmp("x_dateline", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_dateline(1, t, parse_sep);
	} else if (strcmp("yp_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(2, t, parse_sep);
	} else if (strcmp("ym_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(-2, t, parse_sep);
	} else if (strcmp("y_dateline", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_dateline(2, t, parse_sep);
	} else if (strcmp("zp_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(3, t, parse_sep);
	} else if (strcmp("zm_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(-3, t, parse_sep);
	} else if (strcmp("z_dateline", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_dateline(3, t, parse_sep);
	} else if (strcmp("max_changes", keyword) == 0) {
		kw_success = parse_unsigned(&t->max_changes, parse_sep);
	} else if (keyword[0] == '#')
		goto next_line;
	else {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E12: no keyword found: line %u\n",
			(unsigned)line_cntr);
		kw_success = false;
	}
	if (!kw_success) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E13: parsing '%s': line %u\n",
			keyword, (unsigned)line_cntr);
	}
	success = success && kw_success;
	goto next_line;

out:
	if (line_buf)
		free(line_buf);
	fclose(fp);
	return success;
}

static
bool capture_fabric(struct fabric *fabric)
{
	osm_subn_t *subnet = &fabric->osm->subn;
	osm_switch_t *osm_sw;
	osm_physp_t *lphysp, *rphysp;
	osm_port_t *lport;
	osm_node_t *osm_node;
	cl_map_item_t *item;
	uint8_t ltype, rtype;
	int p, port_cnt;
	guid_t sw_guid;
	bool success = true;

	OSM_LOG_ENTER(&fabric->osm->log);

	/*
	 * On OpenSM data structures:
	 *
	 * Apparently, every port in a fabric has an associated osm_physp_t,
	 * but not every port has an associated osm_port_t.  Apparently every
	 * osm_port_t has an associated osm_physp_t.
	 *
	 * So, in order to find the inter-switch links we need to walk the
	 * switch list and examine each port, via its osm_physp_t object.
	 *
	 * But, we need to associate our CA and switch management port
	 * endpoints with the corresponding osm_port_t objects, in order
	 * to simplify computation of LFT entries and perform SL lookup for
	 * path records. Since it is apparently difficult to locate the
	 * osm_port_t that corresponds to a given osm_physp_t, we also
	 * need to walk the list of ports indexed by GUID to get access
	 * to the appropriate osm_port_t objects.
	 *
	 * Need to allocate our switches before we do anything else.
	 */
	item = cl_qmap_head(&subnet->sw_guid_tbl);
	while (item != cl_qmap_end(&subnet->sw_guid_tbl)) {

		osm_sw = (osm_switch_t *)item;
		item = cl_qmap_next(item);
		osm_sw->priv = NULL;  /* avoid stale pointer dereferencing */
		osm_node = osm_sw->p_node;

		if (osm_node_get_type(osm_node) != IB_NODE_TYPE_SWITCH)
			continue;

		port_cnt = osm_node_get_num_physp(osm_node);
		sw_guid = osm_node_get_node_guid(osm_node);

		success = alloc_fswitch(fabric, sw_guid, port_cnt);
		if (!success)
			goto out;
	}
	/*
	 * Now build all our endpoints.
	 */
	item = cl_qmap_head(&subnet->port_guid_tbl);
	while (item != cl_qmap_end(&subnet->port_guid_tbl)) {

		lport = (osm_port_t *)item;
		item = cl_qmap_next(item);
		lport->priv = NULL;  /* avoid stale pointer dereferencing */

		lphysp = lport->p_physp;
		if (!(lphysp && osm_physp_is_valid(lphysp)))
			continue;

		ltype = osm_node_get_type(lphysp->p_node);
		/*
		 * Switch management port is always port 0.
		 */
		if (lphysp->port_num == 0 && ltype == IB_NODE_TYPE_SWITCH) {
			success = build_sw_endpoint(fabric, lport);
			if (!success)
				goto out;
			continue;
		}
		rphysp = lphysp->p_remote_physp;
		if (!(rphysp && osm_physp_is_valid(rphysp)))
			continue;

		rtype = osm_node_get_type(rphysp->p_node);

		if ((ltype != IB_NODE_TYPE_CA &&
		     ltype != IB_NODE_TYPE_ROUTER) ||
		    rtype != IB_NODE_TYPE_SWITCH)
			continue;

		success =
			build_ca_link(fabric, lport,
				      osm_node_get_node_guid(rphysp->p_node),
				      osm_physp_get_port_num(rphysp));
		if (!success)
			goto out;
	}
	/*
	 * Lastly, build all our interswitch links.
	 */
	item = cl_qmap_head(&subnet->sw_guid_tbl);
	while (item != cl_qmap_end(&subnet->sw_guid_tbl)) {

		osm_sw = (osm_switch_t *)item;
		item = cl_qmap_next(item);

		port_cnt = osm_node_get_num_physp(osm_sw->p_node);
		for (p = 0; p < port_cnt; p++) {

			lphysp = osm_node_get_physp_ptr(osm_sw->p_node, p);
			if (!(lphysp && osm_physp_is_valid(lphysp)))
				continue;

			rphysp = lphysp->p_remote_physp;
			if (!(rphysp && osm_physp_is_valid(rphysp)))
				continue;

			if (lphysp == rphysp)
				continue;	/* ignore loopbacks */

			ltype = osm_node_get_type(lphysp->p_node);
			rtype = osm_node_get_type(rphysp->p_node);

			if (ltype != IB_NODE_TYPE_SWITCH ||
			    rtype != IB_NODE_TYPE_SWITCH)
				continue;

			success =
				build_link(fabric,
					   osm_node_get_node_guid(lphysp->p_node),
					   osm_physp_get_port_num(lphysp),
					   osm_node_get_node_guid(rphysp->p_node),
					   osm_physp_get_port_num(rphysp));
			if (!success)
				goto out;
		}
	}
out:
	OSM_LOG_EXIT(&fabric->osm->log);
	return success;
}

/*
 * diagnose_fabric() is just intended to report on fabric elements that
 * could not be placed into the torus.  We want to warn that there were
 * non-torus fabric elements, but they will be ignored for routing purposes.
 * Having them is not an error, and diagnose_fabric() thus has no return
 * value.
 */
static
void diagnose_fabric(struct fabric *f)
{
	struct link *l;
	struct endpoint *ep;
	unsigned k, p;

	/*
	 * Report on any links that didn't get transferred to the torus.
	 */
	for (k = 0; k < f->link_cnt; k++) {
		l = f->link[k];

		if (!(l->end[0].sw && l->end[1].sw))
			continue;

		OSM_LOG(&f->osm->log, OSM_LOG_INFO,
			"Found non-torus fabric link:"
			" sw GUID 0x%04"PRIx64" port %d <->"
			" sw GUID 0x%04"PRIx64" port %d\n",
			cl_ntoh64(l->end[0].n_id), l->end[0].port,
			cl_ntoh64(l->end[1].n_id), l->end[1].port);
	}
	/*
	 * Report on any switches with ports using endpoints that didn't
	 * get transferred to the torus.
	 */
	for (k = 0; k < f->switch_cnt; k++)
		for (p = 0; p < f->sw[k]->port_cnt; p++) {

			if (!f->sw[k]->port[p])
				continue;

			ep = f->sw[k]->port[p];

			/*
			 * We already reported on inter-switch links above.
			 */
			if (ep->type == PASSTHRU)
				continue;

			OSM_LOG(&f->osm->log, OSM_LOG_INFO,
				"Found non-torus fabric port:"
				" sw GUID 0x%04"PRIx64" port %d\n",
				cl_ntoh64(f->sw[k]->n_id), p);
		}
}

static
struct t_switch *alloc_tswitch(struct torus *t, struct f_switch *fsw)
{
	unsigned g;
	size_t new_sw_sz;
	struct t_switch *sw = NULL;
	void *ptr;

	if (!fsw)
		goto out;

	if (t->switch_cnt >= t->sw_pool_sz) {
		/*
		 * This should never happen, but occasionally a particularly
		 * pathological fabric can induce it.  So log an error.
		 */
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E14: unexpectedly requested too many switch "
			"structures!\n");
		goto out;
	}
	new_sw_sz = sizeof(*sw)
		+ fsw->port_cnt * sizeof(*sw->port)
		+ SWITCH_MAX_PORTGRPS * t->portgrp_sz * sizeof(*sw->ptgrp[0].port);
	sw = calloc(1, new_sw_sz);
	if (!sw) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E15: calloc: %s\n", strerror(errno));
		goto out;
	}
	sw->port = (void *)(sw + 1);
	sw->n_id = fsw->n_id;
	sw->port_cnt = fsw->port_cnt;
	sw->torus = t;
	sw->tmp = fsw;

	ptr = &sw->port[sw->port_cnt];

	for (g = 0; g < SWITCH_MAX_PORTGRPS; g++) {
		sw->ptgrp[g].port_grp = g;
		sw->ptgrp[g].sw = sw;
		sw->ptgrp[g].port = ptr;
		ptr = &sw->ptgrp[g].port[t->portgrp_sz];
	}
	t->sw_pool[t->switch_cnt++] = sw;
out:
	return sw;
}

/*
 * install_tswitch() expects the switch coordinates i,j,k to be canonicalized
 * by caller.
 */
static
bool install_tswitch(struct torus *t,
		     int i, int j, int k, struct f_switch *fsw)
{
	struct t_switch **sw = &t->sw[i][j][k];

	if (!*sw)
		*sw = alloc_tswitch(t, fsw);

	if (*sw) {
		(*sw)->i = i;
		(*sw)->j = j;
		(*sw)->k = k;
	}
	return !!*sw;
}

static
struct link *alloc_tlink(struct torus *t)
{
	if (t->link_cnt >= t->link_pool_sz) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E16: unexpectedly out of pre-allocated link "
			"structures!\n");
		return NULL;
	}
	return &t->link_pool[t->link_cnt++];
}

static
int canonicalize(int v, int vmax)
{
	if (v >= 0 && v < vmax)
		return v;

	if (v < 0)
		v += vmax * (1 - v/vmax);

	return v % vmax;
}

static
unsigned set_fp_bit(bool present, int i, int j, int k)
{
	return (unsigned)(!present) << (i + 2 * j + 4 * k);
}

/*
 * Returns an 11-bit fingerprint of what switches are absent in a cube of
 * neighboring switches.  Each bit 0-7 corresponds to a corner of the cube;
 * if a bit is set the corresponding switch is absent.
 *
 * Bits 8-10 distinguish between 2D and 3D cases.  If bit 8+d is set,
 * for 0 <= d < 3;  the d dimension of the desired torus has radix greater
 * than 1. Thus, if all bits 8-10 are set, the desired torus is 3D.
 */
static
unsigned fingerprint(struct torus *t, int i, int j, int k)
{
	unsigned fp;
	int ip1, jp1, kp1;
	int x_sz_gt1, y_sz_gt1, z_sz_gt1;

	x_sz_gt1 = t->x_sz > 1;
	y_sz_gt1 = t->y_sz > 1;
	z_sz_gt1 = t->z_sz > 1;

	ip1 = canonicalize(i + 1, t->x_sz);
	jp1 = canonicalize(j + 1, t->y_sz);
	kp1 = canonicalize(k + 1, t->z_sz);

	fp  = set_fp_bit(t->sw[i][j][k], 0, 0, 0);
	fp |= set_fp_bit(t->sw[ip1][j][k], x_sz_gt1, 0, 0);
	fp |= set_fp_bit(t->sw[i][jp1][k], 0, y_sz_gt1, 0);
	fp |= set_fp_bit(t->sw[ip1][jp1][k], x_sz_gt1, y_sz_gt1, 0);
	fp |= set_fp_bit(t->sw[i][j][kp1], 0, 0, z_sz_gt1);
	fp |= set_fp_bit(t->sw[ip1][j][kp1], x_sz_gt1, 0, z_sz_gt1);
	fp |= set_fp_bit(t->sw[i][jp1][kp1], 0, y_sz_gt1, z_sz_gt1);
	fp |= set_fp_bit(t->sw[ip1][jp1][kp1], x_sz_gt1, y_sz_gt1, z_sz_gt1);

	fp |= x_sz_gt1 << 8;
	fp |= y_sz_gt1 << 9;
	fp |= z_sz_gt1 << 10;

	return fp;
}

static
bool connect_tlink(struct port_grp *pg0, struct endpoint *f_ep0,
		   struct port_grp *pg1, struct endpoint *f_ep1,
		   struct torus *t)
{
	struct link *l;
	bool success = false;

	if (pg0->port_cnt == t->portgrp_sz) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E17: exceeded port group max "
			"port count (%d): switch GUID 0x%04"PRIx64"\n",
			t->portgrp_sz, cl_ntoh64(pg0->sw->n_id));
		goto out;
	}
	if (pg1->port_cnt == t->portgrp_sz) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E18: exceeded port group max "
			"port count (%d): switch GUID 0x%04"PRIx64"\n",
			t->portgrp_sz, cl_ntoh64(pg1->sw->n_id));
		goto out;
	}
	l = alloc_tlink(t);
	if (!l)
		goto out;

	l->end[0].type = f_ep0->type;
	l->end[0].port = f_ep0->port;
	l->end[0].n_id = f_ep0->n_id;
	l->end[0].sw = pg0->sw;
	l->end[0].link = l;
	l->end[0].pgrp = pg0;
	pg0->port[pg0->port_cnt++] = &l->end[0];
	pg0->sw->port[f_ep0->port] = &l->end[0];

	if (f_ep0->osm_port) {
		l->end[0].osm_port = f_ep0->osm_port;
		l->end[0].osm_port->priv = &l->end[0];
		f_ep0->osm_port = NULL;
	}

	l->end[1].type = f_ep1->type;
	l->end[1].port = f_ep1->port;
	l->end[1].n_id = f_ep1->n_id;
	l->end[1].sw = pg1->sw;
	l->end[1].link = l;
	l->end[1].pgrp = pg1;
	pg1->port[pg1->port_cnt++] = &l->end[1];
	pg1->sw->port[f_ep1->port] = &l->end[1];

	if (f_ep1->osm_port) {
		l->end[1].osm_port = f_ep1->osm_port;
		l->end[1].osm_port->priv = &l->end[1];
		f_ep1->osm_port = NULL;
	}
	/*
	 * Disconnect fabric link, so that later we can see if any were
	 * left unconnected in the torus.
	 */
	((struct f_switch *)f_ep0->sw)->port[f_ep0->port] = NULL;
	f_ep0->sw = NULL;
	f_ep0->port = -1;

	((struct f_switch *)f_ep1->sw)->port[f_ep1->port] = NULL;
	f_ep1->sw = NULL;
	f_ep1->port = -1;

	success = true;
out:
	return success;
}

static
bool link_tswitches(struct torus *t, int cdir,
		    struct t_switch *t_sw0, struct t_switch *t_sw1)
{
	int p;
	struct port_grp *pg0, *pg1;
	struct f_switch *f_sw0, *f_sw1;
	const char *cdir_name = "unknown";
	unsigned port_cnt;
	int success = false;

	/*
	 * If this is a 2D torus, it is possible for this function to be
	 * called with its two switch arguments being the same switch, in
	 * which case there are no links to install.
	 */
	if (t_sw0 == t_sw1 &&
	    ((cdir == 0 && t->x_sz == 1) ||
	     (cdir == 1 && t->y_sz == 1) ||
	     (cdir == 2 && t->z_sz == 1))) {
		success = true;
		goto out;
	}
	/*
	 * Ensure that t_sw1 is in the positive cdir direction wrt. t_sw0.
	 * ring_next_sw() relies on it.
	 */
	switch (cdir) {
	case 0:
		if (t->x_sz > 1 &&
		    canonicalize(t_sw0->i + 1, t->x_sz) != t_sw1->i) {
			cdir_name = "x";
			goto cdir_error;
		}
		break;
	case 1:
		if (t->y_sz > 1 &&
		    canonicalize(t_sw0->j + 1, t->y_sz) != t_sw1->j) {
			cdir_name = "y";
			goto cdir_error;
		}
		break;
	case 2:
		if (t->z_sz > 1 &&
		    canonicalize(t_sw0->k + 1, t->z_sz) != t_sw1->k) {
			cdir_name = "z";
			goto cdir_error;
		}
		break;
	default:
	cdir_error:
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR, "ERR 4E19: "
			"sw 0x%04"PRIx64" (%d,%d,%d) <--> "
			"sw 0x%04"PRIx64" (%d,%d,%d) "
			"invalid torus %s link orientation\n",
			cl_ntoh64(t_sw0->n_id), t_sw0->i, t_sw0->j, t_sw0->k,
			cl_ntoh64(t_sw1->n_id), t_sw1->i, t_sw1->j, t_sw1->k,
			cdir_name);
		goto out;
	}

	f_sw0 = t_sw0->tmp;
	f_sw1 = t_sw1->tmp;

	if (!f_sw0 || !f_sw1) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E1A: missing fabric switches!\n"
			"  switch GUIDs: 0x%04"PRIx64" 0x%04"PRIx64"\n",
			cl_ntoh64(t_sw0->n_id), cl_ntoh64(t_sw1->n_id));
		goto out;
	}
	pg0 = &t_sw0->ptgrp[2*cdir + 1];
	pg0->type = PASSTHRU;

	pg1 = &t_sw1->ptgrp[2*cdir];
	pg1->type = PASSTHRU;

	port_cnt = f_sw0->port_cnt;
	/*
	 * Find all the links between these two switches.
	 */
	for (p = 0; p < port_cnt; p++) {
		struct endpoint *f_ep0 = NULL, *f_ep1 = NULL;

		if (!f_sw0->port[p] || !f_sw0->port[p]->link)
			continue;

		if (f_sw0->port[p]->link->end[0].n_id == t_sw0->n_id &&
		    f_sw0->port[p]->link->end[1].n_id == t_sw1->n_id) {

			f_ep0 = &f_sw0->port[p]->link->end[0];
			f_ep1 = &f_sw0->port[p]->link->end[1];
		} else if (f_sw0->port[p]->link->end[1].n_id == t_sw0->n_id &&
			   f_sw0->port[p]->link->end[0].n_id == t_sw1->n_id) {

			f_ep0 = &f_sw0->port[p]->link->end[1];
			f_ep1 = &f_sw0->port[p]->link->end[0];
		} else
			continue;

		if (!(f_ep0->type == PASSTHRU && f_ep1->type == PASSTHRU)) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E1B: not interswitch "
				"link:\n  0x%04"PRIx64"/%d <-> 0x%04"PRIx64"/%d\n",
				cl_ntoh64(f_ep0->n_id), f_ep0->port,
				cl_ntoh64(f_ep1->n_id), f_ep1->port);
			goto out;
		}
		/*
		 * Skip over links that already have been established in the
		 * torus.
		 */
		if (!(f_ep0->sw && f_ep1->sw))
			continue;

		if (!connect_tlink(pg0, f_ep0, pg1, f_ep1, t))
			goto out;
	}
	success = true;
out:
	return success;
}

static
bool link_srcsink(struct torus *t, int i, int j, int k)
{
	struct endpoint *f_ep0;
	struct endpoint *f_ep1;
	struct t_switch *tsw;
	struct f_switch *fsw;
	struct port_grp *pg;
	struct link *fl, *tl;
	unsigned p, port_cnt;
	bool success = false;

	i = canonicalize(i, t->x_sz);
	j = canonicalize(j, t->y_sz);
	k = canonicalize(k, t->z_sz);

	tsw = t->sw[i][j][k];
	if (!tsw)
		return true;

	fsw = tsw->tmp;
	/*
	 * link_srcsink is supposed to get called once for every switch in
	 * the fabric.  At this point every fsw we encounter must have a
	 * non-null osm_switch.  Otherwise something has gone horribly
	 * wrong with topology discovery; the most likely reason is that
	 * the fabric contains a radix-4 torus dimension, but the user gave
	 * a config that didn't say so, breaking all the checking in
	 * safe_x_perpendicular and friends.
	 */
	if (!(fsw && fsw->osm_switch)) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E1C: Invalid topology discovery. "
			"Verify torus-2QoS.conf contents.\n");
		return false;
	}

	pg = &tsw->ptgrp[2 * TORUS_MAX_DIM];
	pg->type = SRCSINK;
	tsw->osm_switch = fsw->osm_switch;
	tsw->osm_switch->priv = tsw;
	fsw->osm_switch = NULL;

	port_cnt = fsw->port_cnt;
	for (p = 0; p < port_cnt; p++) {

		if (!fsw->port[p])
			continue;

		if (fsw->port[p]->type == SRCSINK) {
			/*
			 * If the endpoint is the switch port used for in-band
			 * communication with the switch itself, move it to
			 * the torus.
			 */
			if (pg->port_cnt == t->portgrp_sz) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E1D: exceeded port group max port "
					"count (%d): switch GUID 0x%04"PRIx64"\n",
					t->portgrp_sz, cl_ntoh64(tsw->n_id));
				goto out;
			}
			fsw->port[p]->sw = tsw;
			fsw->port[p]->pgrp = pg;
			tsw->port[p] = fsw->port[p];
			tsw->port[p]->osm_port->priv = tsw->port[p];
			pg->port[pg->port_cnt++] = fsw->port[p];
			fsw->port[p] = NULL;

		} else if (fsw->port[p]->link &&
			   fsw->port[p]->type == PASSTHRU) {
			/*
			 * If the endpoint is a link to a CA, create a new link
			 * in the torus.  Disconnect the fabric link.
			 */

			fl = fsw->port[p]->link;

			if (fl->end[0].sw == fsw) {
				f_ep0 = &fl->end[0];
				f_ep1 = &fl->end[1];
			} else if (fl->end[1].sw == fsw) {
				f_ep1 = &fl->end[0];
				f_ep0 = &fl->end[1];
			} else
				continue;

			if (f_ep1->type != SRCSINK)
				continue;

			if (pg->port_cnt == t->portgrp_sz) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E1E: exceeded port group max port "
					"count (%d): switch GUID 0x%04"PRIx64"\n",
					t->portgrp_sz, cl_ntoh64(tsw->n_id));
				goto out;
			}
			/*
			 * Switch ports connected to links don't get
			 * associated with osm_port_t objects; see
			 * capture_fabric().  So just check CA end.
			 */
			if (!f_ep1->osm_port) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E1F: NULL osm_port->priv port "
					"GUID 0x%04"PRIx64"\n",
					cl_ntoh64(f_ep1->n_id));
				goto out;
			}
			tl = alloc_tlink(t);
			if (!tl)
				continue;

			tl->end[0].type = f_ep0->type;
			tl->end[0].port = f_ep0->port;
			tl->end[0].n_id = f_ep0->n_id;
			tl->end[0].sw = tsw;
			tl->end[0].link = tl;
			tl->end[0].pgrp = pg;
			pg->port[pg->port_cnt++] = &tl->end[0];
			pg->sw->port[f_ep0->port] =  &tl->end[0];

			tl->end[1].type = f_ep1->type;
			tl->end[1].port = f_ep1->port;
			tl->end[1].n_id = f_ep1->n_id;
			tl->end[1].sw = NULL;	/* Correct for a CA */
			tl->end[1].link = tl;
			tl->end[1].pgrp = NULL;	/* Correct for a CA */

			tl->end[1].osm_port = f_ep1->osm_port;
			tl->end[1].osm_port->priv = &tl->end[1];
			f_ep1->osm_port = NULL;

			t->ca_cnt++;
			f_ep0->sw = NULL;
			f_ep0->port = -1;
			fsw->port[p] = NULL;
		}
	}
	success = true;
out:
	return success;
}

static
struct f_switch *ffind_face_corner(struct f_switch *fsw0,
				   struct f_switch *fsw1,
				   struct f_switch *fsw2)
{
	int p0, p3;
	struct link *l;
	struct endpoint *far_end;
	struct f_switch *fsw, *fsw3 = NULL;

	if (!(fsw0 && fsw1 && fsw2))
		goto out;

	for (p0 = 0; p0 < fsw0->port_cnt; p0++) {
		/*
		 * Ignore everything except switch links that haven't
		 * been installed into the torus.
		 */
		if (!(fsw0->port[p0] && fsw0->port[p0]->sw &&
		      fsw0->port[p0]->type == PASSTHRU))
			continue;

		l = fsw0->port[p0]->link;

		if (l->end[0].n_id == fsw0->n_id)
			far_end = &l->end[1];
		else
			far_end = &l->end[0];

		/*
		 * Ignore CAs
		 */
		if (!(far_end->type == PASSTHRU && far_end->sw))
			continue;

		fsw3 = far_end->sw;
		if (fsw3->n_id == fsw1->n_id)	/* existing corner */
			continue;

		for (p3 = 0; p3 < fsw3->port_cnt; p3++) {
			/*
			 * Ignore everything except switch links that haven't
			 * been installed into the torus.
			 */
			if (!(fsw3->port[p3] && fsw3->port[p3]->sw &&
			      fsw3->port[p3]->type == PASSTHRU))
				continue;

			l = fsw3->port[p3]->link;

			if (l->end[0].n_id == fsw3->n_id)
				far_end = &l->end[1];
			else
				far_end = &l->end[0];

			/*
			 * Ignore CAs
			 */
			if (!(far_end->type == PASSTHRU && far_end->sw))
				continue;

			fsw = far_end->sw;
			if (fsw->n_id == fsw2->n_id)
				goto out;
		}
	}
	fsw3 = NULL;
out:
	return fsw3;
}

static
struct f_switch *tfind_face_corner(struct t_switch *tsw0,
				   struct t_switch *tsw1,
				   struct t_switch *tsw2)
{
	if (!(tsw0 && tsw1 && tsw2))
		return NULL;

	return ffind_face_corner(tsw0->tmp, tsw1->tmp, tsw2->tmp);
}

/*
 * This code can break on any torus with a dimension that has radix four.
 *
 * What is supposed to happen is that this code will find the
 * two faces whose shared edge is the desired perpendicular.
 *
 * What actually happens is while searching we send two connected
 * edges that are colinear in a torus dimension with radix four to
 * ffind_face_corner(), which tries to complete a face by finding a
 * 4-loop of edges.
 *
 * In the radix four torus case, it can find a 4-loop which is a ring in a
 * dimension with radix four, rather than the desired face.  It thus returns
 * true when it shouldn't, so the wrong edge is returned as the perpendicular.
 *
 * The appropriate instance of safe_N_perpendicular() (where N == x, y, z)
 * should be used to determine if it is safe to call ffind_perpendicular();
 * these functions will return false it there is a possibility of finding
 * a wrong perpendicular.
 */
struct f_switch *ffind_3d_perpendicular(struct f_switch *fsw0,
					struct f_switch *fsw1,
					struct f_switch *fsw2,
					struct f_switch *fsw3)
{
	int p1;
	struct link *l;
	struct endpoint *far_end;
	struct f_switch *fsw4 = NULL;

	if (!(fsw0 && fsw1 && fsw2 && fsw3))
		goto out;

	/*
	 * Look at all the ports on the switch, fsw1,  that is the base of
	 * the perpendicular.
	 */
	for (p1 = 0; p1 < fsw1->port_cnt; p1++) {
		/*
		 * Ignore everything except switch links that haven't
		 * been installed into the torus.
		 */
		if (!(fsw1->port[p1] && fsw1->port[p1]->sw &&
		      fsw1->port[p1]->type == PASSTHRU))
			continue;

		l = fsw1->port[p1]->link;

		if (l->end[0].n_id == fsw1->n_id)
			far_end = &l->end[1];
		else
			far_end = &l->end[0];
		/*
		 * Ignore CAs
		 */
		if (!(far_end->type == PASSTHRU && far_end->sw))
			continue;

		fsw4 = far_end->sw;
		if (fsw4->n_id == fsw3->n_id)	/* wrong perpendicular */
			continue;

		if (ffind_face_corner(fsw0, fsw1, fsw4) &&
		    ffind_face_corner(fsw2, fsw1, fsw4))
			goto out;
	}
	fsw4 = NULL;
out:
	return fsw4;
}
struct f_switch *ffind_2d_perpendicular(struct f_switch *fsw0,
					struct f_switch *fsw1,
					struct f_switch *fsw2)
{
	int p1;
	struct link *l;
	struct endpoint *far_end;
	struct f_switch *fsw3 = NULL;

	if (!(fsw0 && fsw1 && fsw2))
		goto out;

	/*
	 * Look at all the ports on the switch, fsw1,  that is the base of
	 * the perpendicular.
	 */
	for (p1 = 0; p1 < fsw1->port_cnt; p1++) {
		/*
		 * Ignore everything except switch links that haven't
		 * been installed into the torus.
		 */
		if (!(fsw1->port[p1] && fsw1->port[p1]->sw &&
		      fsw1->port[p1]->type == PASSTHRU))
			continue;

		l = fsw1->port[p1]->link;

		if (l->end[0].n_id == fsw1->n_id)
			far_end = &l->end[1];
		else
			far_end = &l->end[0];
		/*
		 * Ignore CAs
		 */
		if (!(far_end->type == PASSTHRU && far_end->sw))
			continue;

		fsw3 = far_end->sw;
		if (fsw3->n_id == fsw2->n_id)	/* wrong perpendicular */
			continue;

		if (ffind_face_corner(fsw0, fsw1, fsw3))
			goto out;
	}
	fsw3 = NULL;
out:
	return fsw3;
}

static
struct f_switch *tfind_3d_perpendicular(struct t_switch *tsw0,
					struct t_switch *tsw1,
					struct t_switch *tsw2,
					struct t_switch *tsw3)
{
	if (!(tsw0 && tsw1 && tsw2 && tsw3))
		return NULL;

	return ffind_3d_perpendicular(tsw0->tmp, tsw1->tmp,
				      tsw2->tmp, tsw3->tmp);
}

static
struct f_switch *tfind_2d_perpendicular(struct t_switch *tsw0,
					struct t_switch *tsw1,
					struct t_switch *tsw2)
{
	if (!(tsw0 && tsw1 && tsw2))
		return NULL;

	return ffind_2d_perpendicular(tsw0->tmp, tsw1->tmp, tsw2->tmp);
}

static
bool safe_x_ring(struct torus *t, int i, int j, int k)
{
	int im1, ip1, ip2;
	bool success = true;

	/*
	 * If this x-direction radix-4 ring has at least two links
	 * already installed into the torus,  then this ring does not
	 * prevent us from looking for y or z direction perpendiculars.
	 *
	 * It is easier to check for the appropriate switches being installed
	 * into the torus than it is to check for the links, so force the
	 * link installation if the appropriate switches are installed.
	 *
	 * Recall that canonicalize(n - 2, 4) == canonicalize(n + 2, 4).
	 */
	if (t->x_sz != 4 || t->flags & X_MESH)
		goto out;

	im1 = canonicalize(i - 1, t->x_sz);
	ip1 = canonicalize(i + 1, t->x_sz);
	ip2 = canonicalize(i + 2, t->x_sz);

	if (!!t->sw[im1][j][k] +
	    !!t->sw[ip1][j][k] + !!t->sw[ip2][j][k] < 2) {
		success = false;
		goto out;
	}
	if (t->sw[ip2][j][k] && t->sw[im1][j][k])
		success = link_tswitches(t, 0,
					 t->sw[ip2][j][k],
					 t->sw[im1][j][k])
			&& success;

	if (t->sw[im1][j][k] && t->sw[i][j][k])
		success = link_tswitches(t, 0,
					 t->sw[im1][j][k],
					 t->sw[i][j][k])
			&& success;

	if (t->sw[i][j][k] && t->sw[ip1][j][k])
		success = link_tswitches(t, 0,
					 t->sw[i][j][k],
					 t->sw[ip1][j][k])
			&& success;

	if (t->sw[ip1][j][k] && t->sw[ip2][j][k])
		success = link_tswitches(t, 0,
					 t->sw[ip1][j][k],
					 t->sw[ip2][j][k])
			&& success;
out:
	return success;
}

static
bool safe_y_ring(struct torus *t, int i, int j, int k)
{
	int jm1, jp1, jp2;
	bool success = true;

	/*
	 * If this y-direction radix-4 ring has at least two links
	 * already installed into the torus,  then this ring does not
	 * prevent us from looking for x or z direction perpendiculars.
	 *
	 * It is easier to check for the appropriate switches being installed
	 * into the torus than it is to check for the links, so force the
	 * link installation if the appropriate switches are installed.
	 *
	 * Recall that canonicalize(n - 2, 4) == canonicalize(n + 2, 4).
	 */
	if (t->y_sz != 4 || (t->flags & Y_MESH))
		goto out;

	jm1 = canonicalize(j - 1, t->y_sz);
	jp1 = canonicalize(j + 1, t->y_sz);
	jp2 = canonicalize(j + 2, t->y_sz);

	if (!!t->sw[i][jm1][k] +
	    !!t->sw[i][jp1][k] + !!t->sw[i][jp2][k] < 2) {
		success = false;
		goto out;
	}
	if (t->sw[i][jp2][k] && t->sw[i][jm1][k])
		success = link_tswitches(t, 1,
					 t->sw[i][jp2][k],
					 t->sw[i][jm1][k])
			&& success;

	if (t->sw[i][jm1][k] && t->sw[i][j][k])
		success = link_tswitches(t, 1,
					 t->sw[i][jm1][k],
					 t->sw[i][j][k])
			&& success;

	if (t->sw[i][j][k] && t->sw[i][jp1][k])
		success = link_tswitches(t, 1,
					 t->sw[i][j][k],
					 t->sw[i][jp1][k])
			&& success;

	if (t->sw[i][jp1][k] && t->sw[i][jp2][k])
		success = link_tswitches(t, 1,
					 t->sw[i][jp1][k],
					 t->sw[i][jp2][k])
			&& success;
out:
	return success;
}

static
bool safe_z_ring(struct torus *t, int i, int j, int k)
{
	int km1, kp1, kp2;
	bool success = true;

	/*
	 * If this z-direction radix-4 ring has at least two links
	 * already installed into the torus,  then this ring does not
	 * prevent us from looking for x or y direction perpendiculars.
	 *
	 * It is easier to check for the appropriate switches being installed
	 * into the torus than it is to check for the links, so force the
	 * link installation if the appropriate switches are installed.
	 *
	 * Recall that canonicalize(n - 2, 4) == canonicalize(n + 2, 4).
	 */
	if (t->z_sz != 4 || t->flags & Z_MESH)
		goto out;

	km1 = canonicalize(k - 1, t->z_sz);
	kp1 = canonicalize(k + 1, t->z_sz);
	kp2 = canonicalize(k + 2, t->z_sz);

	if (!!t->sw[i][j][km1] +
	    !!t->sw[i][j][kp1] + !!t->sw[i][j][kp2] < 2) {
		success = false;
		goto out;
	}
	if (t->sw[i][j][kp2] && t->sw[i][j][km1])
		success = link_tswitches(t, 2,
					 t->sw[i][j][kp2],
					 t->sw[i][j][km1])
			&& success;

	if (t->sw[i][j][km1] && t->sw[i][j][k])
		success = link_tswitches(t, 2,
					 t->sw[i][j][km1],
					 t->sw[i][j][k])
			&& success;

	if (t->sw[i][j][k] && t->sw[i][j][kp1])
		success = link_tswitches(t, 2,
					 t->sw[i][j][k],
					 t->sw[i][j][kp1])
			&& success;

	if (t->sw[i][j][kp1] && t->sw[i][j][kp2])
		success = link_tswitches(t, 2,
					 t->sw[i][j][kp1],
					 t->sw[i][j][kp2])
			&& success;
out:
	return success;
}

/*
 * These functions return true when it safe to call
 * tfind_3d_perpendicular()/ffind_3d_perpendicular().
 */
static
bool safe_x_perpendicular(struct torus *t, int i, int j, int k)
{
	/*
	 * If the dimensions perpendicular to the search direction are
	 * not radix 4 torus dimensions, it is always safe to search for
	 * a perpendicular.
	 *
	 * Here we are checking for enough appropriate links having been
	 * installed into the torus to prevent an incorrect link from being
	 * considered as a perpendicular candidate.
	 */
	return safe_y_ring(t, i, j, k) && safe_z_ring(t, i, j, k);
}

static
bool safe_y_perpendicular(struct torus *t, int i, int j, int k)
{
	/*
	 * If the dimensions perpendicular to the search direction are
	 * not radix 4 torus dimensions, it is always safe to search for
	 * a perpendicular.
	 *
	 * Here we are checking for enough appropriate links having been
	 * installed into the torus to prevent an incorrect link from being
	 * considered as a perpendicular candidate.
	 */
	return safe_x_ring(t, i, j, k) && safe_z_ring(t, i, j, k);
}

static
bool safe_z_perpendicular(struct torus *t, int i, int j, int k)
{
	/*
	 * If the dimensions perpendicular to the search direction are
	 * not radix 4 torus dimensions, it is always safe to search for
	 * a perpendicular.
	 *
	 * Implement this by checking for enough appropriate links having
	 * been installed into the torus to prevent an incorrect link from
	 * being considered as a perpendicular candidate.
	 */
	return safe_x_ring(t, i, j, k) && safe_y_ring(t, i, j, k);
}

/*
 * Templates for determining 2D/3D case fingerprints. Recall that if
 * a fingerprint bit is set the corresponding switch is absent from
 * the all-switches-present template.
 *
 * I.e., for the 2D case where the x,y dimensions have a radix greater
 * than one, and the z dimension has radix 1, fingerprint bits 4-7 are
 * always zero.
 *
 * For the 2D case where the x,z dimensions have a radix greater than
 * one, and the y dimension has radix 1, fingerprint bits 2,3,6,7 are
 * always zero.
 *
 * For the 2D case where the y,z dimensions have a radix greater than
 * one, and the x dimension has radix 1, fingerprint bits 1,3,5,7 are
 * always zero.
 *
 * Recall also that bits 8-10 distinguish between 2D and 3D cases.
 * If bit 8+d is set, for 0 <= d < 3;  the d dimension of the desired
 * torus has radix greater than 1.
 */

/*
 * 2D case 0x300
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O . . . . . O
 * 2D case 0x500                      .           .
 *  b0: t->sw[i  ][0  ][k  ]          .           .
 *  b1: t->sw[i+1][0  ][k  ]          .           .
 *  b4: t->sw[i  ][0  ][k+1]          .           .
 *  b5: t->sw[i+1][0  ][k+1]          .           .
 *                                    @ . . . . . O
 * 2D case 0x600
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6: t->sw[0  ][j+1][k+1]
 */

/*
 * 3D case 0x700:                           O
 *                                        . . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]      .   .   .   .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .   .   .   .   .
 *                                . .       O       . .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */

static
void log_no_crnr(struct torus *t, unsigned n,
		 int case_i, int case_j, int case_k,
		 int crnr_i, int crnr_j, int crnr_k)
{
	if (t->debug)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO, "Case 0x%03x "
			"@ %d %d %d: no corner @ %d %d %d\n",
			n, case_i, case_j, case_k, crnr_i, crnr_j, crnr_k);
}

static
void log_no_perp(struct torus *t, unsigned n,
		 int case_i, int case_j, int case_k,
		 int perp_i, int perp_j, int perp_k)
{
	if (t->debug)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO, "Case 0x%03x "
			"@ %d %d %d: no perpendicular @ %d %d %d\n",
			n, case_i, case_j, case_k, perp_i, perp_j, perp_k);
}

/*
 * Handle the 2D cases with a single existing edge.
 *
 */

/*
 * 2D case 0x30c
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2:
 *  b3:
 *                                    O           O
 * 2D case 0x530
 *  b0: t->sw[i  ][0  ][k  ]
 *  b1: t->sw[i+1][0  ][k  ]
 *  b4:
 *  b5:
 *                                    @ . . . . . O
 * 2D case 0x650
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4:
 *  b6:
 */
static
bool handle_case_0x30c(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (safe_y_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_2d_perpendicular(t->sw[ip1][j][k],
						   t->sw[i][j][k],
						   t->sw[i][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x30c, i, j, k, i, j, k);

	if (safe_y_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[ip1][j][k],
						   t->sw[ip1][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x30c, i, j, k, ip1, j, k);
	return false;
}

static
bool handle_case_0x530(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_2d_perpendicular(t->sw[ip1][j][k],
						   t->sw[i][j][k],
						   t->sw[i][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x530, i, j, k, i, j, k);

	if (safe_z_perpendicular(t, ip1, j, k) &&
	      install_tswitch(t, ip1, j, kp1,
			      tfind_2d_perpendicular(t->sw[i][j][k],
						     t->sw[ip1][j][k],
						     t->sw[ip1][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x530, i, j, k, ip1, j, k);
	return false;
}

static
bool handle_case_0x650(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_2d_perpendicular(t->sw[i][jp1][k],
						   t->sw[i][j][k],
						   t->sw[i][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x650, i, j, k, i, j, k);

	if (safe_z_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[i][jp1][km1]))) {
		return true;
	}
	log_no_perp(t, 0x650, i, j, k, i, jp1, k);
	return false;
}

/*
 * 2D case 0x305
 *  b0:
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O           O
 * 2D case 0x511                                  .
 *  b0:                                           .
 *  b1: t->sw[i+1][0  ][k  ]                      .
 *  b4:                                           .
 *  b5: t->sw[i+1][0  ][k+1]                      .
 *                                    @           O
 * 2D case 0x611
 *  b0:
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4:
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x305(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (safe_x_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[ip1][jp1][k],
						   t->sw[ip1][j][k],
						   t->sw[ip2][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x305, i, j, k, ip1, j, k);

	if (safe_x_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_2d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip2][jp1][k]))) {
		return true;
	}
	log_no_perp(t, 0x305, i, j, k, ip1, jp1, k);
	return false;
}

static
bool handle_case_0x511(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[ip1][j][k],
						   t->sw[ip2][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x511, i, j, k, ip1, j, k);

	if (safe_x_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_2d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][j][kp1],
						   t->sw[ip2][j][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x511, i, j, k, ip1, j, kp1);
	return false;
}

static
bool handle_case_0x611(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[i][jp1][kp1],
						   t->sw[i][jp1][k],
						   t->sw[i][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x611, i, j, k, i, jp1, k);

	if (safe_y_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_2d_perpendicular(t->sw[i][jp1][k],
						   t->sw[i][jp1][kp1],
						   t->sw[i][jp2][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x611, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 2D case 0x303
 *  b0:
 *  b1:
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O . . . . . O
 * 2D case 0x503
 *  b0:
 *  b1:
 *  b4: t->sw[i  ][0  ][k+1]
 *  b5: t->sw[i+1][0  ][k+1]
 *                                    @           O
 * 2D case 0x605
 *  b0:
 *  b2:
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x303(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);

	if (safe_y_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[ip1][jp1][k],
						   t->sw[i][jp1][k],
						   t->sw[i][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x303, i, j, k, i, jp1, k);

	if (safe_y_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_2d_perpendicular(t->sw[i][jp1][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x303, i, j, k, ip1, jp1, k);
	return false;
}

static
bool handle_case_0x503(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[i][j][kp1],
						   t->sw[i][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x503, i, j, k, i, j, kp1);

	if (safe_z_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_2d_perpendicular(t->sw[i][j][kp1],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x503, i, j, k, ip1, j, kp1);
	return false;
}

static
bool handle_case_0x605(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[i][jp1][kp1],
						   t->sw[i][j][kp1],
						   t->sw[i][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x605, i, j, k, i, j, kp1);

	if (safe_z_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_2d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[i][jp1][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x605, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 2D case 0x30a
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3:
 *                                    O           O
 * 2D case 0x522                      .
 *  b0: t->sw[i  ][0  ][k  ]          .
 *  b1:                               .
 *  b4: t->sw[i  ][0  ][k+1]          .
 *  b5:                               .
 *                                    @           O
 * 2D case 0x644
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2:
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6:
 */
static
bool handle_case_0x30a(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (safe_x_perpendicular(t, i, j, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_2d_perpendicular(t->sw[i][jp1][k],
						   t->sw[i][j][k],
						   t->sw[im1][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x30a, i, j, k, i, j, k);

	if (safe_x_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[im1][jp1][k]))) {
		return true;
	}
	log_no_perp(t, 0x30a, i, j, k, i, jp1, k);
	return false;
}

static
bool handle_case_0x522(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, j, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_2d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][j][k],
						   t->sw[im1][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x522, i, j, k, i, j, k);

	if (safe_x_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, ip1, j, kp1,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[i][j][kp1],
						   t->sw[im1][j][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x522, i, j, k, i, j, kp1);
	return false;
}

static
bool handle_case_0x644(struct torus *t, int i, int j, int k)
{
	int jm1 = canonicalize(j - 1, t->y_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_2d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][j][k],
						   t->sw[i][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x644, i, j, k, i, j, k);

	if (safe_y_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[i][j][kp1],
						   t->sw[i][jm1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x644, i, j, k, i, j, kp1);
	return false;
}

/*
 * Handle the 2D cases where two existing edges meet at a corner.
 *
 */

/*
 * 2D case 0x301
 *  b0:
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O . . . . . O
 * 2D case 0x501                                  .
 *  b0:                                           .
 *  b1: t->sw[i+1][0  ][k  ]                      .
 *  b4: t->sw[i  ][0  ][k+1]                      .
 *  b5: t->sw[i+1][0  ][k+1]                      .
 *                                    @           O
 * 2D case 0x601
 *  b0:
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x301(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x301, i, j, k, i, j, k);
	return false;
}

static
bool handle_case_0x501(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x501, i, j, k, i, j, k);
	return false;
}

static
bool handle_case_0x601(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x601, i, j, k, i, j, k);
	return false;
}

/*
 * 2D case 0x302
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O . . . . . O
 * 2D case 0x502                      .
 *  b0: t->sw[i  ][0  ][k  ]          .
 *  b1:                               .
 *  b4: t->sw[i  ][0  ][k+1]          .
 *  b5: t->sw[i+1][0  ][k+1]          .
 *                                    @           O
 * 2D case 0x604
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2:
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x302(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x302, i, j, k, ip1, j, k);
	return false;
}

static
bool handle_case_0x502(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x502, i, j, k, ip1, j, k);
	return false;
}

static
bool handle_case_0x604(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x604, i, j, k, i, jp1, k);
	return false;
}


/*
 * 2D case 0x308
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3:
 *                                    O           O
 * 2D case 0x520                      .
 *  b0: t->sw[i  ][0  ][k  ]          .
 *  b1: t->sw[i+1][0  ][k  ]          .
 *  b4: t->sw[i  ][0  ][k+1]          .
 *  b5:                               .
 *                                    @ . . . . . O
 * 2D case 0x640
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6:
 */
static
bool handle_case_0x308(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x308, i, j, k, ip1, jp1, k);
	return false;
}

static
bool handle_case_0x520(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x520, i, j, k, ip1, j, kp1);
	return false;
}

static
bool handle_case_0x640(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x640, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 2D case 0x304
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O           O
 * 2D case 0x510                                  .
 *  b0: t->sw[i  ][0  ][k  ]                      .
 *  b1: t->sw[i+1][0  ][k  ]                      .
 *  b4:                                           .
 *  b5: t->sw[i+1][0  ][k+1]                      .
 *                                    @ . . . . . O
 * 2D case 0x610
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4:
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x304(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x304, i, j, k, i, jp1, k);
	return false;
}

static
bool handle_case_0x510(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x510, i, j, k, i, j, kp1);
	return false;
}

static
bool handle_case_0x610(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x610, i, j, k, i, j, kp1);
	return false;
}

/*
 * Handle the 3D cases where two existing edges meet at a corner.
 *
 */

/*
 * 3D case 0x71f:                           O
 *                                        .   .
 *  b0:                                 .       .
 *  b1:                               .           .
 *  b2:                             .               .
 *  b3:                           O                   O
 *  b4:                                     O
 *  b5: t->sw[i+1][j  ][k+1]
 *  b6: t->sw[i  ][j+1][k+1]
 *  b7: t->sw[i+1][j+1][k+1]
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x71f(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, ip1, jp1, kp1) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_3d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[ip1][jp1][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x71f, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x72f:                           O
 *                                        .
 *  b0:                                 .
 *  b1:                               .
 *  b2:                             .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O
 *  b5:                               .
 *  b6: t->sw[i  ][j+1][k+1]            .
 *  b7: t->sw[i+1][j+1][k+1]              .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x72f(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[i][jp1][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x72f, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x737:                           O
 *                                        . .
 *  b0:                                 .   .
 *  b1:                               .     .
 *  b2:                             .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:
 *  b6: t->sw[i  ][j+1][k+1]
 *  b7: t->sw[i+1][j+1][k+1]
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x737(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, ip1, jp1, kp1) &&
	    install_tswitch(t, ip1, j, kp1,
			    tfind_3d_perpendicular(t->sw[i][jp1][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][jp2][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x737, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x73b:                           O
 *                                        .
 *  b0:                                 .
 *  b1:                               .
 *  b2: t->sw[i  ][j+1][k  ]        .
 *  b3:                           O                   O
 *  b4:                           .         O
 *  b5:                           .
 *  b6: t->sw[i  ][j+1][k+1]      .
 *  b7: t->sw[i+1][j+1][k+1]      .
 *                                .         O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x73b(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_3d_perpendicular(t->sw[i][jp1][k],
						   t->sw[i][jp1][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[i][jp2][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x73b, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x74f:                           O
 *                                            .
 *  b0:                                         .
 *  b1:                                           .
 *  b2:                                             .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       .
 *  b5: t->sw[i+1][j  ][k+1]                      .
 *  b6:                                         .
 *  b7: t->sw[i+1][j+1][k+1]                  .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x74f(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip1][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x74f, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x757:                           O
 *                                          . .
 *  b0:                                     .   .
 *  b1:                                     .     .
 *  b2:                                     .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5: t->sw[i+1][j  ][k+1]
 *  b6:
 *  b7: t->sw[i+1][j+1][k+1]
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x757(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, jp1, kp1) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip1][jp1][k],
						   t->sw[ip2][jp1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x757, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x75d:                           O
 *                                            .
 *  b0:                                         .
 *  b1: t->sw[i+1][j  ][k  ]                      .
 *  b2:                                             .
 *  b3:                           O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                          .
 *  b6:                                               .
 *  b7: t->sw[i+1][j+1][k+1]                          .
 *                                          O         .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x75d(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_3d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip2][j][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x75d, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x773:                           O
 *                                          .
 *  b0:                                     .
 *  b1:                                     .
 *  b2: t->sw[i  ][j+1][k  ]                .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:                                   .
 *  b6:                                 .
 *  b7: t->sw[i+1][j+1][k+1]          .
 *                                  .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x773(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_3d_perpendicular(t->sw[i][jp1][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip1][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x773, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x775:                           O
 *                                          .
 *  b0:                                     .
 *  b1: t->sw[i+1][j  ][k  ]                .
 *  b2:                                     .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:                                       .
 *  b6:                                         .
 *  b7: t->sw[i+1][j+1][k+1]                      .
 *                                          O       .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x775(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_3d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip2][jp1][k]))) {
		return true;
	}
	log_no_perp(t, 0x775, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x78f:                           O
 *
 *  b0:
 *  b1:
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       .
 *  b5: t->sw[i+1][j  ][k+1]          .           .
 *  b6: t->sw[i  ][j+1][k+1]            .       .
 *  b7:                                   .   .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x78f(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, j, k,
			    tfind_3d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[i][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x78f, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x7ab:                           O
 *
 *  b0:
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .       .
 *                                .         O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7ab(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, ip1, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[i][jp1][k],
						   t->sw[im1][jp1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x7ab, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7ae:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O
 *  b5:                               .
 *  b6: t->sw[i  ][j+1][k+1]            .
 *  b7:                                   .
 *                                          O
 *                                O         .         O
 *                                          .
 *                                          .
 *                                          .
 *                                          .
 *                                          @
 */
static
bool handle_case_0x7ae(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, ip1, j, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[im1][j][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x7ae, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x7b3:                           O
 *
 *  b0:
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                           .         O
 *  b5:                           .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .   .
 *                                . .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7b3(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_3d_perpendicular(t->sw[i][jp1][kp1],
						   t->sw[i][jp1][k],
						   t->sw[ip1][jp1][k],
						   t->sw[i][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x7b3, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x7ba:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4:                           .         O
 *  b5:                           .
 *  b6: t->sw[i  ][j+1][k+1]      .
 *  b7:                           .
 *                                .         O
 *                                O                   O
 *                                  .
 *                                    .
 *                                      .
 *                                        .
 *                                          @
 */
static
bool handle_case_0x7ba(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[i][jp1][kp1],
						   t->sw[im1][jp1][k]))) {
		return true;
	}
	log_no_perp(t, 0x7ba, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x7cd:                           O
 *
 *  b0:
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                      .   .
 *  b6:                                         .     .
 *  b7:                                       .       .
 *                                          O         .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7cd(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, ip1, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][j][k],
						   t->sw[ip1][jm1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x7cd, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x7ce:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       .
 *  b5: t->sw[i+1][j  ][k+1]                      .
 *  b6:                                         .
 *  b7:                                       .
 *                                          O
 *                                O         .         O
 *                                          .
 *                                          .
 *                                          .
 *                                          .
 *                                          @
 */
static
bool handle_case_0x7ce(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[i][j][kp1],
						   t->sw[ip1][j][kp1],
						   t->sw[i][jm1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x7ce, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x7d5:                           O
 *
 *  b0:
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                  .       .
 *  b6:                                         .     .
 *  b7:                                           .   .
 *                                          O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7d5(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_3d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip2][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x7d5, i, j, k, ip1, j, k);
	return false;
}

/*
 * 3D case 0x7dc:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                          .
 *  b6:                                               .
 *  b7:                                               .
 *                                          O         .
 *                                O                   O
 *                                                  .
 *                                                .
 *                                              .
 *                                            .
 *                                          @
 */
static
bool handle_case_0x7dc(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[ip1][j][k],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x7dc, i, j, k, ip1, j, k);
	return false;
}

/*
 * 3D case 0x7ea:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                            O                   O
 *  b4: t->sw[i  ][j  ][k+1]                 O
 *  b5:
 *  b6:
 *  b7:
 *                                          O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x7ea(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, j, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[im1][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x7ea, i, j, k, i, j, k);
	return false;
}

/*
 * 3D case 0x7ec:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O
 *  b5:
 *  b6:
 *  b7:
 *                                          O
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x7ec(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][j][k],
						   t->sw[ip1][j][k],
						   t->sw[i][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x7ec, i, j, k, i, j, k);
	return false;
}

/*
 * 3D case 0x7f1:                           O
 *
 *  b0:
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O
 *  b5:                                   .   .
 *  b6:                                 .       .
 *  b7:                               .           .
 *                                  .       O       .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7f1(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, ip1, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[i][jp1][k],
						   t->sw[ip1][jp1][km1]))) {
		return true;
	}
	log_no_perp(t, 0x7f1, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x7f2:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O
 *  b5:                                   .
 *  b6:                                 .
 *  b7:                               .
 *                                  .       O
 *                                O                   O
 *                                  .
 *                                    .
 *                                      .
 *                                        .
 *                                          @
 */
static
bool handle_case_0x7f2(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[ip1][jp1][k],
						   t->sw[i][jp1][km1]))) {
		return true;
	}
	log_no_perp(t, 0x7f2, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x7f4:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O
 *  b5:                                       .
 *  b6:                                         .
 *  b7:                                           .
 *                                          O       .
 *                                O                   O
 *                                                  .
 *                                                .
 *                                              .
 *                                            .
 *                                          @
 */
static
bool handle_case_0x7f4(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, ip1, j, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x7f4, i, j, k, ip1, j, k);
	return false;
}

/*
 * 3D case 0x7f8:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4:                                     O
 *  b5:
 *  b6:
 *  b7:
 *                                          O
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x7f8(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_3d_perpendicular(t->sw[ip1][j][k],
						   t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[i][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x7f8, i, j, k, i, j, k);
	return false;
}

/*
 * Handle the cases where three existing edges meet at a corner.
 */

/*
 * 3D case 0x717:                           O
 *                                        . . .
 *  b0:                                 .   .   .
 *  b1:                               .     .     .
 *  b2:                             .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5: t->sw[i+1][j  ][k+1]
 *  b6: t->sw[i  ][j+1][k+1]
 *  b7: t->sw[i+1][j+1][k+1]
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x717(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x717, i, j, k, i, j, kp1);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x717, i, j, k, ip1, j, k);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x717, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x72b:                           O
 *                                        .
 *  b0:                                 .
 *  b1:                               .
 *  b2: t->sw[i  ][j+1][k  ]        .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .       .
 *                                .         O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x72b(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x72b, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x72b, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x72b, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x74d:                           O
 *                                            .
 *  b0:                                         .
 *  b1: t->sw[i+1][j  ][k  ]                      .
 *  b2:                                             .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                      .   .
 *  b6:                                         .     .
 *  b7: t->sw[i+1][j+1][k+1]                  .       .
 *                                          O         .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x74d(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x74d, i, j, k, i, jp1, kp1);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x74d, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x74d, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x771:                           O
 *                                          .
 *  b0:                                     .
 *  b1: t->sw[i+1][j  ][k  ]                .
 *  b2: t->sw[i  ][j+1][k  ]                .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:                                   .   .
 *  b6:                                 .       .
 *  b7: t->sw[i+1][j+1][k+1]          .           .
 *                                  .       O       .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x771(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x771, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][jp1][kp1],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x771, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][jp1][kp1],
					      t->sw[ip1][jp1][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x771, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x78e:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       .
 *  b5: t->sw[i+1][j  ][k+1]          .           .
 *  b6: t->sw[i  ][j+1][k+1]            .       .
 *  b7:                                   .   .
 *                                          O
 *                                O         .         O
 *                                          .
 *                                          .
 *                                          .
 *                                          .
 *                                          @
 */
static
bool handle_case_0x78e(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78e, i, j, k, ip1, jp1, kp1);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78e, i, j, k, ip1, j, k);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78e, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x7b2:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                           .         O
 *  b5:                           .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .   .
 *                                . .       O
 *                                O                   O
 *                                  .
 *                                    .
 *                                      .
 *                                        .
 *                                          @
 */
static
bool handle_case_0x7b2(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7b2, i, j, k, ip1, j, k);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7b2, i, j, k, ip1, jp1, kp1);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[i][jp1][k],
					      t->sw[i][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7b2, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x7d4:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                  .       .
 *  b6:                                         .     .
 *  b7:                                           .   .
 *                                          O       . .
 *                                O                   O
 *                                                  .
 *                                                .
 *                                              .
 *                                            .
 *                                          @
 */
static
bool handle_case_0x7d4(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7d4, i, j, k, i, jp1, k);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][j][k],
					      t->sw[i][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7d4, i, j, k, i, j, kp1);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7d4, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7e8:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O
 *  b5:
 *  b6:
 *  b7:
 *                                          O
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x7e8(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7e8, i, j, k, ip1, jp1, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x7e8, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x7e8, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * Handle the cases where four corners on a single face are missing.
 */

/*
 * 3D case 0x70f:                           O
 *                                        .   .
 *  b0:                                 .       .
 *  b1:                               .           .
 *  b2:                             .               .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       .
 *  b5: t->sw[i+1][j  ][k+1]          .           .
 *  b6: t->sw[i  ][j+1][k+1]            .       .
 *  b7: t->sw[i+1][j+1][k+1]              .   .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x70f(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x71f(t, i, j, k))
		return true;

	if (handle_case_0x72f(t, i, j, k))
		return true;

	if (handle_case_0x74f(t, i, j, k))
		return true;

	return handle_case_0x78f(t, i, j, k);
}

/*
 * 3D case 0x733:                           O
 *                                        . .
 *  b0:                                 .   .
 *  b1:                               .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                           .         O
 *  b5:                           .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .
 *                                . .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x733(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x737(t, i, j, k))
		return true;

	if (handle_case_0x73b(t, i, j, k))
		return true;

	if (handle_case_0x773(t, i, j, k))
		return true;

	return handle_case_0x7b3(t, i, j, k);
}

/*
 * 3D case 0x755:                           O
 *                                          . .
 *  b0:                                     .   .
 *  b1: t->sw[i+1][j  ][k  ]                .     .
 *  b2:                                     .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                  .       .
 *  b6:                                         .     .
 *  b7: t->sw[i+1][j+1][k+1]                      .   .
 *                                          O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x755(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x757(t, i, j, k))
		return true;

	if (handle_case_0x75d(t, i, j, k))
		return true;

	if (handle_case_0x775(t, i, j, k))
		return true;

	return handle_case_0x7d5(t, i, j, k);
}

/*
 * 3D case 0x7aa:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .       .
 *                                .         O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x7aa(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x7ab(t, i, j, k))
		return true;

	if (handle_case_0x7ae(t, i, j, k))
		return true;

	if (handle_case_0x7ba(t, i, j, k))
		return true;

	return handle_case_0x7ea(t, i, j, k);
}

/*
 * 3D case 0x7cc:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                      .   .
 *  b6:                                         .     .
 *  b7:                                       .       .
 *                                          O         .
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x7cc(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x7cd(t, i, j, k))
		return true;

	if (handle_case_0x7ce(t, i, j, k))
		return true;

	if (handle_case_0x7dc(t, i, j, k))
		return true;

	return handle_case_0x7ec(t, i, j, k);
}

/*
 * 3D case 0x7f0:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O
 *  b5:                                   .   .
 *  b6:                                 .       .
 *  b7:                               .           .
 *                                  .       O       .
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x7f0(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x7f1(t, i, j, k))
		return true;

	if (handle_case_0x7f2(t, i, j, k))
		return true;

	if (handle_case_0x7f4(t, i, j, k))
		return true;

	return handle_case_0x7f8(t, i, j, k);
}

/*
 * Handle the cases where three corners on a single face are missing.
 */


/*
 * 3D case 0x707:                           O
 *                                        . . .
 *  b0:                                 .   .   .
 *  b1:                               .     .     .
 *  b2:                             .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       .
 *  b5: t->sw[i+1][j  ][k+1]          .           .
 *  b6: t->sw[i  ][j+1][k+1]            .       .
 *  b7: t->sw[i+1][j+1][k+1]              .   .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x707(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x707, i, j, k, ip1, j, k);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x707, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x70b:                           O
 *                                        .   .
 *  b0:                                 .       .
 *  b1:                               .           .
 *  b2: t->sw[i  ][j+1][k  ]        .               .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       .
 *  b5: t->sw[i+1][j  ][k+1]      .   .           .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7: t->sw[i+1][j+1][k+1]      .       .   .
 *                                .         O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x70b(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70b, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70b, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x70d:                           O
 *                                        .   .
 *  b0:                                 .       .
 *  b1: t->sw[i+1][j  ][k  ]          .           .
 *  b2:                             .               .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]          .           .   .
 *  b6: t->sw[i  ][j+1][k+1]            .       .     .
 *  b7: t->sw[i+1][j+1][k+1]              .   .       .
 *                                          O         .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x70d(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70d, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70d, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x70e:                           O
 *                                        .   .
 *  b0: t->sw[i  ][j  ][k  ]            .       .
 *  b1:                               .           .
 *  b2:                             .               .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       .
 *  b5: t->sw[i+1][j  ][k+1]          .           .
 *  b6: t->sw[i  ][j+1][k+1]            .       .
 *  b7: t->sw[i+1][j+1][k+1]              .   .
 *                                          O
 *                                O         .         O
 *                                          .
 *                                          .
 *                                          .
 *                                          .
 *                                          @
 */
static
bool handle_case_0x70e(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70e, i, j, k, ip1, j, k);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70e, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x713:                           O
 *                                        . . .
 *  b0:                                 .   .   .
 *  b1:                               .     .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                           .         O
 *  b5: t->sw[i+1][j  ][k+1]      .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .
 *                                . .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x713(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x713, i, j, k, ip1, j, k);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x713, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x715:                           O
 *                                        . . .
 *  b0:                                 .   .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .     .
 *  b2:                             .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                  .       .
 *  b6: t->sw[i  ][j+1][k+1]                    .     .
 *  b7: t->sw[i+1][j+1][k+1]                      .   .
 *                                          O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x715(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x715, i, j, k, i, jp1, k);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x715, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x723:                           O
 *                                        . .
 *  b0:                                 .   .
 *  b1:                               .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .   .
 *                                . .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x723(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x723, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x723, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x72a:                           O
 *                                        .
 *  b0: t->sw[i  ][j  ][k  ]            .
 *  b1:                               .
 *  b2: t->sw[i  ][j+1][k  ]        .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .       .
 *                                .         O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x72a(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x72a, i, j, k, ip1, jp1, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x72a, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x731:                           O
 *                                        . .
 *  b0:                                 .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                           .         O
 *  b5:                           .       .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7: t->sw[i+1][j+1][k+1]      .   .           .
 *                                . .       O       .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x731(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x731, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x731, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x732:                           O
 *                                        . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .
 *  b1:                               .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                           .         O
 *  b5:                           .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .
 *                                . .       O
 *                                O                   O
 *                                  .
 *                                    .
 *                                      .
 *                                        .
 *                                          @
 */
static
bool handle_case_0x732(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x732, i, j, k, ip1, j, k);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x732, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x745:                           O
 *                                          . .
 *  b0:                                     .   .
 *  b1: t->sw[i+1][j  ][k  ]                .     .
 *  b2:                                     .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                  .   .   .
 *  b6:                                         .     .
 *  b7: t->sw[i+1][j+1][k+1]                  .   .   .
 *                                          O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x745(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x745, i, j, k, i, j, k);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x745, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x74c:                           O
 *                                            .
 *  b0: t->sw[i  ][j  ][k  ]                    .
 *  b1: t->sw[i+1][j  ][k  ]                      .
 *  b2:                                             .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                      .   .
 *  b6:                                         .     .
 *  b7: t->sw[i+1][j+1][k+1]                  .       .
 *                                          O         .
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x74c(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x74c, i, j, k, ip1, jp1, k);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x74c, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x751:                           O
 *                                          . .
 *  b0:                                     .   .
 *  b1: t->sw[i+1][j  ][k  ]                .     .
 *  b2: t->sw[i  ][j+1][k  ]                .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]              .   .       .
 *  b6:                                 .       .     .
 *  b7: t->sw[i+1][j+1][k+1]          .           .   .
 *                                  .       O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x751(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x751, i, j, k, i, j, k);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x751, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x754:                           O
 *                                          . .
 *  b0: t->sw[i  ][j  ][k  ]                .   .
 *  b1: t->sw[i+1][j  ][k  ]                .     .
 *  b2:                                     .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                  .       .
 *  b6:                                         .     .
 *  b7: t->sw[i+1][j+1][k+1]                      .   .
 *                                          O       . .
 *                                O                   O
 *                                                  .
 *                                                .
 *                                              .
 *                                            .
 *                                          @
 */
static
bool handle_case_0x754(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x754, i, j, k, i, jp1, k);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x754, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x770:                           O
 *                                          .
 *  b0: t->sw[i  ][j  ][k  ]                .
 *  b1: t->sw[i+1][j  ][k  ]                .
 *  b2: t->sw[i  ][j+1][k  ]                .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:                                   .   .
 *  b6:                                 .       .
 *  b7: t->sw[i+1][j+1][k+1]          .           .
 *                                  .       O       .
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x770(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x770, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x770, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x78a:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       .
 *  b5: t->sw[i+1][j  ][k+1]      .   .           .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7:                           .       .   .
 *                                .         O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x78a(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78a, i, j, k, ip1, j, k);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78a, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x78c:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]          .           .   .
 *  b6: t->sw[i  ][j+1][k+1]            .       .     .
 *  b7:                                   .   .       .
 *                                          O         .
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x78c(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78c, i, j, k, i, jp1, k);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78c, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7a2:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .   .   .
 *                                . .       O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x7a2(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7a2, i, j, k, ip1, j, k);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7a2, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7a8:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[ip1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .       .
 *                                .         O
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x7a8(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7a8, i, j, k, ip1, jp1, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][j][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7a8, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x7b0:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                           .         O
 *  b5:                           .       .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7:                           .   .           .
 *                                . .       O       .
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x7b0(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x7b0, i, j, k, i, j, kp1);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7b0, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7c4:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                  .   .   .
 *  b6:                                         .     .
 *  b7:                                       .   .   .
 *                                          O       . .
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x7c4(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7c4, i, j, k, i, jp1, k);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7c4, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7c8:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                      .   .
 *  b6:                                         .     .
 *  b7:                                       .       .
 *                                          O         .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x7c8(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7c8, i, j, k, ip1, jp1, k);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7c8, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7d0:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]              .   .       .
 *  b6:                                 .       .     .
 *  b7:                               .           .   .
 *                                  .       O       . .
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x7d0(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x7d0, i, j, k, i, j, kp1);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7d0, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7e0:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O
 *  b5:                                   .   .
 *  b6:                                 .       .
 *  b7:                               .           .
 *                                  .       O       .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x7e0(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][j][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7e0, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7e0, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * Handle the cases where two corners on a single edge are missing.
 */

/*
 * 3D case 0x703:                           O
 *                                        . . .
 *  b0:                                 .   .   .
 *  b1:                               .     .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       .
 *  b5: t->sw[i+1][j  ][k+1]      .   .   .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7: t->sw[i+1][j+1][k+1]      .   .   .   .
 *                                . .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x703(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x703, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x703, i, j, k, ip1, j, k);
	return false;
}

/*
 * 3D case 0x705:                           O
 *                                        . . .
 *  b0:                                 .   .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .     .
 *  b2:                             .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]          .       .   .   .
 *  b6: t->sw[i  ][j+1][k+1]            .       .     .
 *  b7: t->sw[i+1][j+1][k+1]              .   .   .   .
 *                                          O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x705(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x705, i, j, k, i, j, k);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x705, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x70a:                           O
 *                                        . . .
 *  b0: t->sw[i  ][j  ][k  ]            .       .
 *  b1:                               .           .
 *  b2: t->sw[i  ][j+1][k  ]        .               .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       .
 *  b5: t->sw[i+1][j  ][k+1]      .   .           .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7: t->sw[i+1][j+1][k+1]      .       .   .
 *                                .         O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x70a(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70a, i, j, k, ip1, j, k);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70a, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x70c:                           O
 *                                        .   .
 *  b0: t->sw[i  ][j  ][k  ]            .       .
 *  b1: t->sw[i+1][j  ][k  ]          .           .
 *  b2:                             .               .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]          .           .   .
 *  b6: t->sw[i  ][j+1][k+1]            .       .     .
 *  b7: t->sw[i+1][j+1][k+1]              .   .       .
 *                                          O         .
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x70c(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70c, i, j, k, i, jp1, k);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x70c, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x711:                           O
 *                                        . . .
 *  b0:                                 .   .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                           .         O         .
 *  b5: t->sw[i+1][j  ][k+1]      .       .   .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .           .   .
 *                                . .       O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x711(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x711, i, j, k, i, j, k);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x711, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x722:                           O
 *                                        . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .
 *  b1:                               .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .   .
 *                                . .       O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x722(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x722, i, j, k, ip1, j, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x722, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x730:                           O
 *                                        . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                           .         O
 *  b5:                           .       .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7: t->sw[i+1][j+1][k+1]      .   .           .
 *                                . .       O       .
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x730(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x730, i, j, k, i, j, kp1);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x730, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x744:                           O
 *                                          . .
 *  b0: t->sw[i  ][j  ][k  ]                .   .
 *  b1: t->sw[i+1][j  ][k  ]                .     .
 *  b2:                                     .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                  .   .   .
 *  b6:                                         .     .
 *  b7: t->sw[i+1][j+1][k+1]                  .   .   .
 *                                          O       . .
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x744(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x744, i, j, k, i, jp1, k);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x744, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x750:                           O
 *                                          . .
 *  b0: t->sw[i  ][j  ][k  ]                .   .
 *  b1: t->sw[i+1][j  ][k  ]                .     .
 *  b2: t->sw[i  ][j+1][k  ]                .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]              .   .       .
 *  b6:                                 .       .     .
 *  b7: t->sw[i+1][j+1][k+1]          .           .   .
 *                                  .       O       . .
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x750(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x750, i, j, k, i, j, kp1);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x750, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x788:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[ip1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]      .   .           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .     .
 *  b7:                           .       .   .       .
 *                                .         O         .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x788(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x788, i, j, k, ip1, jp1, k);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x788, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7a0:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7:                           .   .   .       .
 *                                . .       O       .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x7a0(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][j][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7a0, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7a0, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7c0:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]              .   .   .   .
 *  b6:                                 .       .     .
 *  b7:                               .       .   .   .
 *                                  .       O       . .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x7c0(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7c0, i, j, k, i, jp1, kp1);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7c0, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * Handle the cases where a single corner is missing.
 */

/*
 * 3D case 0x701:                           O
 *                                        . . .
 *  b0:                                     .   .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]      .   .   .   .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .   .   .   .   .
 *                                . .       O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x701(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x701, i, j, k, i, j, k);
	return false;
}

/*
 * 3D case 0x702:                           O
 *                                        . . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .   .
 *  b1:                               .     .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       .
 *  b5: t->sw[i+1][j  ][k+1]      .   .   .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7: t->sw[i+1][j+1][k+1]      .   .   .   .
 *                                . .       O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x702(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x702, i, j, k, ip1, j, k);
	return false;
}

/*
 * 3D case 0x704:                           O
 *                                        . . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .     .
 *  b2:                             .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]          .       .   .   .
 *  b6: t->sw[i  ][j+1][k+1]            .       .     .
 *  b7: t->sw[i+1][j+1][k+1]              .   .   .   .
 *                                          O       . .
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x704(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x704, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x708:                           O
 *                                        .   .
 *  b0: t->sw[i  ][j  ][k  ]            .       .
 *  b1: t->sw[i+1][j  ][k  ]          .           .
 *  b2: t->sw[i  ][j+1][k  ]        .               .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]      .   .           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .     .
 *  b7: t->sw[i+1][j+1][k+1]      .       .   .       .
 *                                .         O         .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x708(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][j][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x708, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x710:                           O
 *                                        . . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                           .         O         .
 *  b5: t->sw[i+1][j  ][k+1]      .       .   .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .           .   .
 *                                . .       O       . .
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x710(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x710, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x720:                           O
 *                                        . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .
 *  b7: t->sw[i+1][j+1][k+1]      .   .   .       .
 *                                . .       O       .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x720(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x720, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x740:                           O
 *                                          . .
 *  b0: t->sw[i  ][j  ][k  ]                .   .
 *  b1: t->sw[i+1][j  ][k  ]                .     .
 *  b2: t->sw[i  ][j+1][k  ]                .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]              .   .   .   .
 *  b6:                                 .       .     .
 *  b7: t->sw[i+1][j+1][k+1]          .       .   .   .
 *                                  .       O       . .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x740(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x740, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x780:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]      .   .   .   .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .     .
 *  b7:                           .   .   .   .   .   .
 *                                . .       O       . .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x780(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x780, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * Make sure links between all known torus/mesh switches are installed.
 *
 * We don't have to worry about links that wrap on a mesh coordinate, as
 * there shouldn't be any; if there are it indicates an input error.
 */
static
void check_tlinks(struct torus *t, int i, int j, int k)
{
	struct t_switch ****sw = t->sw;
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	/*
	 * Don't waste time/code checking return status of link_tswitches()
	 * here.  It is unlikely to fail, and the result of any failure here
	 * will be caught elsewhere anyway.
	 */
	if (sw[i][j][k] && sw[ip1][j][k])
		link_tswitches(t, 0, sw[i][j][k], sw[ip1][j][k]);

	if (sw[i][jp1][k] && sw[ip1][jp1][k])
		link_tswitches(t, 0, sw[i][jp1][k], sw[ip1][jp1][k]);

	if (sw[i][j][kp1] && sw[ip1][j][kp1])
		link_tswitches(t, 0, sw[i][j][kp1], sw[ip1][j][kp1]);

	if (sw[i][jp1][kp1] && sw[ip1][jp1][kp1])
		link_tswitches(t, 0, sw[i][jp1][kp1], sw[ip1][jp1][kp1]);


	if (sw[i][j][k] && sw[i][jp1][k])
		link_tswitches(t, 1, sw[i][j][k], sw[i][jp1][k]);

	if (sw[ip1][j][k] && sw[ip1][jp1][k])
		link_tswitches(t, 1, sw[ip1][j][k], sw[ip1][jp1][k]);

	if (sw[i][j][kp1] && sw[i][jp1][kp1])
		link_tswitches(t, 1, sw[i][j][kp1], sw[i][jp1][kp1]);

	if (sw[ip1][j][kp1] && sw[ip1][jp1][kp1])
		link_tswitches(t, 1, sw[ip1][j][kp1], sw[ip1][jp1][kp1]);


	if (sw[i][j][k] && sw[i][j][kp1])
		link_tswitches(t, 2, sw[i][j][k], sw[i][j][kp1]);

	if (sw[ip1][j][k] && sw[ip1][j][kp1])
		link_tswitches(t, 2, sw[ip1][j][k], sw[ip1][j][kp1]);

	if (sw[i][jp1][k] && sw[i][jp1][kp1])
		link_tswitches(t, 2, sw[i][jp1][k], sw[i][jp1][kp1]);

	if (sw[ip1][jp1][k] && sw[ip1][jp1][kp1])
		link_tswitches(t, 2, sw[ip1][jp1][k], sw[ip1][jp1][kp1]);
}

static
void locate_sw(struct torus *t, int i, int j, int k)
{
	unsigned fp;
	bool success;

	i = canonicalize(i, t->x_sz);
	j = canonicalize(j, t->y_sz);
	k = canonicalize(k, t->z_sz);

	/*
	 * By definition, if a coordinate direction is meshed, we don't
	 * allow it to wrap to zero.
	 */
	if (t->flags & X_MESH) {
		int ip1 = canonicalize(i + 1, t->x_sz);
		if (ip1 < i)
			goto out;
	}
	if (t->flags & Y_MESH) {
		int jp1 = canonicalize(j + 1, t->y_sz);
		if (jp1 < j)
			goto out;
	}
	if (t->flags & Z_MESH) {
		int kp1 = canonicalize(k + 1, t->z_sz);
		if (kp1 < k)
			goto out;
	}
	/*
	 * There are various reasons that the links are not installed between
	 * known torus switches.  These include cases where the search for
	 * new switches only partially succeeds due to missing switches, and
	 * cases where we haven't processed this position yet, but processing
	 * of multiple independent neighbor positions has installed switches
	 * into corners of our case.
	 *
	 * In any event, the topology assumptions made in handling the
	 * fingerprint for this position require that all links be installed
	 * between installed switches for this position.
	 */
again:
	check_tlinks(t, i, j, k);
	fp = fingerprint(t, i, j, k);

	switch (fp) {
	/*
	 * When all switches are present, we are done.  Otherwise, one of
	 * the cases below will be unsuccessful, and we'll be done also.
	 *
	 * Note that check_tlinks() above will ensure all links that are
	 * present are connected, in the event that all our switches are
	 * present due to successful case handling in the surrounding
	 * torus/mesh.
	 */
	case 0x300:
	case 0x500:
	case 0x600:
	case 0x700:
		goto out;
	/*
	 * Ignore the 2D cases where there isn't enough information to uniquely
	 * locate/place a switch into the cube.
	 */
	case 0x30f: 	/* 0 corners available */
	case 0x533: 	/* 0 corners available */
	case 0x655: 	/* 0 corners available */
	case 0x30e:	/* 1 corner available */
	case 0x532:	/* 1 corner available */
	case 0x654:	/* 1 corner available */
	case 0x30d:	/* 1 corner available */
	case 0x531:	/* 1 corner available */
	case 0x651:	/* 1 corner available */
	case 0x30b:	/* 1 corner available */
	case 0x523:	/* 1 corner available */
	case 0x645:	/* 1 corner available */
	case 0x307:	/* 1 corner available */
	case 0x513:	/* 1 corner available */
	case 0x615:	/* 1 corner available */
		goto out;
	/*
	 * Handle the 2D cases with a single existing edge.
	 *
	 */
	case 0x30c:
		success = handle_case_0x30c(t, i, j, k);
		break;
	case 0x303:
		success = handle_case_0x303(t, i, j, k);
		break;
	case 0x305:
		success = handle_case_0x305(t, i, j, k);
		break;
	case 0x30a:
		success = handle_case_0x30a(t, i, j, k);
		break;
	case 0x503:
		success = handle_case_0x503(t, i, j, k);
		break;
	case 0x511:
		success = handle_case_0x511(t, i, j, k);
		break;
	case 0x522:
		success = handle_case_0x522(t, i, j, k);
		break;
	case 0x530:
		success = handle_case_0x530(t, i, j, k);
		break;
	case 0x605:
		success = handle_case_0x605(t, i, j, k);
		break;
	case 0x611:
		success = handle_case_0x611(t, i, j, k);
		break;
	case 0x644:
		success = handle_case_0x644(t, i, j, k);
		break;
	case 0x650:
		success = handle_case_0x650(t, i, j, k);
		break;
	/*
	 * Handle the 2D cases where two existing edges meet at a corner.
	 */
	case 0x301:
		success = handle_case_0x301(t, i, j, k);
		break;
	case 0x302:
		success = handle_case_0x302(t, i, j, k);
		break;
	case 0x304:
		success = handle_case_0x304(t, i, j, k);
		break;
	case 0x308:
		success = handle_case_0x308(t, i, j, k);
		break;
	case 0x501:
		success = handle_case_0x501(t, i, j, k);
		break;
	case 0x502:
		success = handle_case_0x502(t, i, j, k);
		break;
	case 0x520:
		success = handle_case_0x520(t, i, j, k);
		break;
	case 0x510:
		success = handle_case_0x510(t, i, j, k);
		break;
	case 0x601:
		success = handle_case_0x601(t, i, j, k);
		break;
	case 0x604:
		success = handle_case_0x604(t, i, j, k);
		break;
	case 0x610:
		success = handle_case_0x610(t, i, j, k);
		break;
	case 0x640:
		success = handle_case_0x640(t, i, j, k);
		break;
	/*
	 * Ignore the 3D cases where there isn't enough information to uniquely
	 * locate/place a switch into the cube.
	 */
	case 0x7ff:	/* 0 corners available */
	case 0x7fe:	/* 1 corner available */
	case 0x7fd:	/* 1 corner available */
	case 0x7fb:	/* 1 corner available */
	case 0x7f7:	/* 1 corner available */
	case 0x7ef:	/* 1 corner available */
	case 0x7df:	/* 1 corner available */
	case 0x7bf:	/* 1 corner available */
	case 0x77f:	/* 1 corner available */
	case 0x7fc:	/* 2 adj corners available */
	case 0x7fa:	/* 2 adj corners available */
	case 0x7f5:	/* 2 adj corners available */
	case 0x7f3:	/* 2 adj corners available */
	case 0x7cf:	/* 2 adj corners available */
	case 0x7af:	/* 2 adj corners available */
	case 0x75f:	/* 2 adj corners available */
	case 0x73f:	/* 2 adj corners available */
	case 0x7ee:	/* 2 adj corners available */
	case 0x7dd:	/* 2 adj corners available */
	case 0x7bb:	/* 2 adj corners available */
	case 0x777:	/* 2 adj corners available */
		goto out;
	/*
	 * Handle the 3D cases where two existing edges meet at a corner.
	 *
	 */
	case 0x71f:
		success = handle_case_0x71f(t, i, j, k);
		break;
	case 0x72f:
		success = handle_case_0x72f(t, i, j, k);
		break;
	case 0x737:
		success = handle_case_0x737(t, i, j, k);
		break;
	case 0x73b:
		success = handle_case_0x73b(t, i, j, k);
		break;
	case 0x74f:
		success = handle_case_0x74f(t, i, j, k);
		break;
	case 0x757:
		success = handle_case_0x757(t, i, j, k);
		break;
	case 0x75d:
		success = handle_case_0x75d(t, i, j, k);
		break;
	case 0x773:
		success = handle_case_0x773(t, i, j, k);
		break;
	case 0x775:
		success = handle_case_0x775(t, i, j, k);
		break;
	case 0x78f:
		success = handle_case_0x78f(t, i, j, k);
		break;
	case 0x7ab:
		success = handle_case_0x7ab(t, i, j, k);
		break;
	case 0x7ae:
		success = handle_case_0x7ae(t, i, j, k);
		break;
	case 0x7b3:
		success = handle_case_0x7b3(t, i, j, k);
		break;
	case 0x7ba:
		success = handle_case_0x7ba(t, i, j, k);
		break;
	case 0x7cd:
		success = handle_case_0x7cd(t, i, j, k);
		break;
	case 0x7ce:
		success = handle_case_0x7ce(t, i, j, k);
		break;
	case 0x7d5:
		success = handle_case_0x7d5(t, i, j, k);
		break;
	case 0x7dc:
		success = handle_case_0x7dc(t, i, j, k);
		break;
	case 0x7ea:
		success = handle_case_0x7ea(t, i, j, k);
		break;
	case 0x7ec:
		success = handle_case_0x7ec(t, i, j, k);
		break;
	case 0x7f1:
		success = handle_case_0x7f1(t, i, j, k);
		break;
	case 0x7f2:
		success = handle_case_0x7f2(t, i, j, k);
		break;
	case 0x7f4:
		success = handle_case_0x7f4(t, i, j, k);
		break;
	case 0x7f8:
		success = handle_case_0x7f8(t, i, j, k);
		break;
	/*
	 * Handle the cases where three existing edges meet at a corner.
	 *
	 */
	case 0x717:
		success = handle_case_0x717(t, i, j, k);
		break;
	case 0x72b:
		success = handle_case_0x72b(t, i, j, k);
		break;
	case 0x74d:
		success = handle_case_0x74d(t, i, j, k);
		break;
	case 0x771:
		success = handle_case_0x771(t, i, j, k);
		break;
	case 0x78e:
		success = handle_case_0x78e(t, i, j, k);
		break;
	case 0x7b2:
		success = handle_case_0x7b2(t, i, j, k);
		break;
	case 0x7d4:
		success = handle_case_0x7d4(t, i, j, k);
		break;
	case 0x7e8:
		success = handle_case_0x7e8(t, i, j, k);
		break;
	/*
	 * Handle the cases where four corners on a single face are missing.
	 */
	case 0x70f:
		success = handle_case_0x70f(t, i, j, k);
		break;
	case 0x733:
		success = handle_case_0x733(t, i, j, k);
		break;
	case 0x755:
		success = handle_case_0x755(t, i, j, k);
		break;
	case 0x7aa:
		success = handle_case_0x7aa(t, i, j, k);
		break;
	case 0x7cc:
		success = handle_case_0x7cc(t, i, j, k);
		break;
	case 0x7f0:
		success = handle_case_0x7f0(t, i, j, k);
		break;
	/*
	 * Handle the cases where three corners on a single face are missing.
	 */
	case 0x707:
		success = handle_case_0x707(t, i, j, k);
		break;
	case 0x70b:
		success = handle_case_0x70b(t, i, j, k);
		break;
	case 0x70d:
		success = handle_case_0x70d(t, i, j, k);
		break;
	case 0x70e:
		success = handle_case_0x70e(t, i, j, k);
		break;
	case 0x713:
		success = handle_case_0x713(t, i, j, k);
		break;
	case 0x715:
		success = handle_case_0x715(t, i, j, k);
		break;
	case 0x723:
		success = handle_case_0x723(t, i, j, k);
		break;
	case 0x72a:
		success = handle_case_0x72a(t, i, j, k);
		break;
	case 0x731:
		success = handle_case_0x731(t, i, j, k);
		break;
	case 0x732:
		success = handle_case_0x732(t, i, j, k);
		break;
	case 0x745:
		success = handle_case_0x745(t, i, j, k);
		break;
	case 0x74c:
		success = handle_case_0x74c(t, i, j, k);
		break;
	case 0x751:
		success = handle_case_0x751(t, i, j, k);
		break;
	case 0x754:
		success = handle_case_0x754(t, i, j, k);
		break;
	case 0x770:
		success = handle_case_0x770(t, i, j, k);
		break;
	case 0x78a:
		success = handle_case_0x78a(t, i, j, k);
		break;
	case 0x78c:
		success = handle_case_0x78c(t, i, j, k);
		break;
	case 0x7a2:
		success = handle_case_0x7a2(t, i, j, k);
		break;
	case 0x7a8:
		success = handle_case_0x7a8(t, i, j, k);
		break;
	case 0x7b0:
		success = handle_case_0x7b0(t, i, j, k);
		break;
	case 0x7c4:
		success = handle_case_0x7c4(t, i, j, k);
		break;
	case 0x7c8:
		success = handle_case_0x7c8(t, i, j, k);
		break;
	case 0x7d0:
		success = handle_case_0x7d0(t, i, j, k);
		break;
	case 0x7e0:
		success = handle_case_0x7e0(t, i, j, k);
		break;
	/*
	 * Handle the cases where two corners on a single edge are missing.
	 */
	case 0x703:
		success = handle_case_0x703(t, i, j, k);
		break;
	case 0x705:
		success = handle_case_0x705(t, i, j, k);
		break;
	case 0x70a:
		success = handle_case_0x70a(t, i, j, k);
		break;
	case 0x70c:
		success = handle_case_0x70c(t, i, j, k);
		break;
	case 0x711:
		success = handle_case_0x711(t, i, j, k);
		break;
	case 0x722:
		success = handle_case_0x722(t, i, j, k);
		break;
	case 0x730:
		success = handle_case_0x730(t, i, j, k);
		break;
	case 0x744:
		success = handle_case_0x744(t, i, j, k);
		break;
	case 0x750:
		success = handle_case_0x750(t, i, j, k);
		break;
	case 0x788:
		success = handle_case_0x788(t, i, j, k);
		break;
	case 0x7a0:
		success = handle_case_0x7a0(t, i, j, k);
		break;
	case 0x7c0:
		success = handle_case_0x7c0(t, i, j, k);
		break;
	/*
	 * Handle the cases where a single corner is missing.
	 */
	case 0x701:
		success = handle_case_0x701(t, i, j, k);
		break;
	case 0x702:
		success = handle_case_0x702(t, i, j, k);
		break;
	case 0x704:
		success = handle_case_0x704(t, i, j, k);
		break;
	case 0x708:
		success = handle_case_0x708(t, i, j, k);
		break;
	case 0x710:
		success = handle_case_0x710(t, i, j, k);
		break;
	case 0x720:
		success = handle_case_0x720(t, i, j, k);
		break;
	case 0x740:
		success = handle_case_0x740(t, i, j, k);
		break;
	case 0x780:
		success = handle_case_0x780(t, i, j, k);
		break;

	default:
		/*
		 * There's lots of unhandled cases still, but it's not clear
		 * we care.  Let debugging show us what they are so we can
		 * learn if we care.
		 */
		if (t->debug)
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"Unhandled fingerprint 0x%03x @ %d %d %d\n",
				fp, i, j, k);
		goto out;
	}
	/*
	 * If we successfully handled a case, we may be able to make more
	 * progress at this position, so try again.  Otherwise, even though
	 * we didn't successfully handle a case, we may have installed a
	 * switch into the torus/mesh, so try to install links as well.
	 * Then we'll have another go at the next position.
	 */
	if (success) {
		if (t->debug)
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"Success on fingerprint 0x%03x @ %d %d %d\n",
				fp, i, j, k);
		goto again;
	} else {
		check_tlinks(t, i, j, k);
		if (t->debug)
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"Failed on fingerprint 0x%03x @ %d %d %d\n",
				fp, i, j, k);
	}
out:
	return;
}

#define LINK_ERR_STR " direction link required for topology seed configuration since radix == 4! See torus-2QoS.conf(5).\n"
#define LINK_ERR2_STR " direction link required for topology seed configuration! See torus-2QoS.conf(5).\n"
#define SEED_ERR_STR " direction links for topology seed do not share a common switch! See torus-2QoS.conf(5).\n"

static
bool verify_setup(struct torus *t, struct fabric *f)
{
	struct coord_dirs *o;
	struct f_switch *sw;
	unsigned p, s, n = 0;
	bool success = false;
	bool all_sw_present, need_seed = true;

	if (!(t->x_sz && t->y_sz && t->z_sz)) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E20: missing required torus size specification!\n");
		goto out;
	}
	if (t->osm->subn.min_sw_data_vls < 2) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E48: Too few data VLs to support torus routing "
			"without credit loops (have switchport %d need 2)\n",
			(int)t->osm->subn.min_sw_data_vls);
		goto out;
	}
	if (t->osm->subn.min_sw_data_vls < 4)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Warning: Too few data VLs to support torus routing "
			"with a failed switch without credit loops "
			"(have switchport %d need 4)\n",
			(int)t->osm->subn.min_sw_data_vls);
	if (t->osm->subn.min_sw_data_vls < 8)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Warning: Too few data VLs to support torus routing "
			"with two QoS levels (have switchport %d need 8)\n",
			(int)t->osm->subn.min_sw_data_vls);
	if (t->osm->subn.min_data_vls < 2)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Warning: Too few data VLs to support torus routing "
			"with two QoS levels (have endport %d need 2)\n",
			(int)t->osm->subn.min_data_vls);
	/*
	 * Be sure all the switches in the torus support the port
	 * ordering that might have been configured.
	 */
	for (s = 0; s < f->switch_cnt; s++) {
		sw = f->sw[s];
		for (p = 0; p < sw->port_cnt; p++) {
			if (t->port_order[p] >= sw->port_cnt) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E21: port_order configured using "
					"port %u, but only %u ports in "
					"switch w/ GUID 0x%04"PRIx64"\n",
					t->port_order[p], sw->port_cnt - 1,
					cl_ntoh64(sw->n_id));
				goto out;
			}
		}
	}
	/*
	 * Unfortunately, there is a problem with non-unique topology for any
	 * torus dimension which has radix four.  This problem requires extra
	 * input, in the form of specifying both the positive and negative
	 * coordinate directions from a common switch, for any torus dimension
	 * with radix four (see also build_torus()).
	 *
	 * Do the checking required to ensure that the required information
	 * is present, but more than the needed information is not required.
	 *
	 * So, verify that we learned the coordinate directions correctly for
	 * the fabric.  The coordinate direction links get an invalid port
	 * set on their ends when parsed.
	 */
again:
	all_sw_present = true;
	o = &t->seed[n];

	if (t->x_sz == 4 && !(t->flags & X_MESH)) {
		if (o->xp_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E22: Positive x" LINK_ERR_STR);
			goto out;
		}
		if (o->xm_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E23: Negative x" LINK_ERR_STR);
			goto out;
		}
		if (o->xp_link.end[0].n_id != o->xm_link.end[0].n_id) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E24: Positive/negative x" SEED_ERR_STR);
			goto out;
		}
	}
	if (t->y_sz == 4 && !(t->flags & Y_MESH)) {
		if (o->yp_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E25: Positive y" LINK_ERR_STR);
			goto out;
		}
		if (o->ym_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E26: Negative y" LINK_ERR_STR);
			goto out;
		}
		if (o->yp_link.end[0].n_id != o->ym_link.end[0].n_id) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E27: Positive/negative y" SEED_ERR_STR);
			goto out;
		}
	}
	if (t->z_sz == 4 && !(t->flags & Z_MESH)) {
		if (o->zp_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E28: Positive z" LINK_ERR_STR);
			goto out;
		}
		if (o->zm_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E29: Negative z" LINK_ERR_STR);
			goto out;
		}
		if (o->zp_link.end[0].n_id != o->zm_link.end[0].n_id) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E2A: Positive/negative z" SEED_ERR_STR);
			goto out;
		}
	}
	if (t->x_sz > 1) {
		if (o->xp_link.end[0].port >= 0 &&
		    o->xm_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E2B: Positive or negative x" LINK_ERR2_STR);
			goto out;
		}
		if (o->xp_link.end[0].port < 0 &&
		    !find_f_sw(f, o->xp_link.end[0].n_id))
			all_sw_present = false;

		if (o->xp_link.end[1].port < 0 &&
		    !find_f_sw(f, o->xp_link.end[1].n_id))
			all_sw_present = false;

		if (o->xm_link.end[0].port < 0 &&
		    !find_f_sw(f, o->xm_link.end[0].n_id))
			all_sw_present = false;

		if (o->xm_link.end[1].port < 0 &&
		    !find_f_sw(f, o->xm_link.end[1].n_id))
			all_sw_present = false;
	}
	if (t->z_sz > 1) {
		if (o->zp_link.end[0].port >= 0 &&
		    o->zm_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E2C: Positive or negative z" LINK_ERR2_STR);
			goto out;
		}
		if ((o->xp_link.end[0].port < 0 &&
		     o->zp_link.end[0].port < 0 &&
		     o->zp_link.end[0].n_id != o->xp_link.end[0].n_id) ||

		    (o->xp_link.end[0].port < 0 &&
		     o->zm_link.end[0].port < 0 &&
		     o->zm_link.end[0].n_id != o->xp_link.end[0].n_id) ||

		    (o->xm_link.end[0].port < 0 &&
		     o->zp_link.end[0].port < 0 &&
		     o->zp_link.end[0].n_id != o->xm_link.end[0].n_id) ||

		    (o->xm_link.end[0].port < 0 &&
		     o->zm_link.end[0].port < 0 &&
		     o->zm_link.end[0].n_id != o->xm_link.end[0].n_id)) {

			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E2D: x and z" SEED_ERR_STR);
			goto out;
		}
		if (o->zp_link.end[0].port < 0 &&
		    !find_f_sw(f, o->zp_link.end[0].n_id))
			all_sw_present = false;

		if (o->zp_link.end[1].port < 0 &&
		    !find_f_sw(f, o->zp_link.end[1].n_id))
			all_sw_present = false;

		if (o->zm_link.end[0].port < 0 &&
		    !find_f_sw(f, o->zm_link.end[0].n_id))
			all_sw_present = false;

		if (o->zm_link.end[1].port < 0 &&
		    !find_f_sw(f, o->zm_link.end[1].n_id))
			all_sw_present = false;
	}
	if (t->y_sz > 1) {
		if (o->yp_link.end[0].port >= 0 &&
		    o->ym_link.end[0].port >= 0) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E2E: Positive or negative y" LINK_ERR2_STR);
			goto out;
		}
		if ((o->xp_link.end[0].port < 0 &&
		     o->yp_link.end[0].port < 0 &&
		     o->yp_link.end[0].n_id != o->xp_link.end[0].n_id) ||

		    (o->xp_link.end[0].port < 0 &&
		     o->ym_link.end[0].port < 0 &&
		     o->ym_link.end[0].n_id != o->xp_link.end[0].n_id) ||

		    (o->xm_link.end[0].port < 0 &&
		     o->yp_link.end[0].port < 0 &&
		     o->yp_link.end[0].n_id != o->xm_link.end[0].n_id) ||

		    (o->xm_link.end[0].port < 0 &&
		     o->ym_link.end[0].port < 0 &&
		     o->ym_link.end[0].n_id != o->xm_link.end[0].n_id)) {

			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"ERR 4E2F: x and y" SEED_ERR_STR);
			goto out;
		}
		if (o->yp_link.end[0].port < 0 &&
		    !find_f_sw(f, o->yp_link.end[0].n_id))
			all_sw_present = false;

		if (o->yp_link.end[1].port < 0 &&
		    !find_f_sw(f, o->yp_link.end[1].n_id))
			all_sw_present = false;

		if (o->ym_link.end[0].port < 0 &&
		    !find_f_sw(f, o->ym_link.end[0].n_id))
			all_sw_present = false;

		if (o->ym_link.end[1].port < 0 &&
		    !find_f_sw(f, o->ym_link.end[1].n_id))
			all_sw_present = false;
	}
	if (all_sw_present && need_seed) {
		t->seed_idx = n;
		need_seed = false;
	}
	if (++n < t->seed_cnt)
		goto again;

	if (need_seed)
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E30: Every configured torus seed has at "
			"least one switch missing in fabric! See "
			"torus-2QoS.conf(5) and TORUS TOPOLOGY DISCOVERY "
			"in torus-2QoS(8)\n");
	else
		success = true;
out:
	return success;
}

static
bool build_torus(struct fabric *f, struct torus *t)
{
	int i, j, k;
	int im1, jm1, km1;
	int ip1, jp1, kp1;
	unsigned nlink;
	struct coord_dirs *o;
	struct f_switch *fsw0, *fsw1;
	struct t_switch ****sw = t->sw;
	bool success = true;

	t->link_pool_sz = f->link_cnt;
	t->link_pool = calloc(1, t->link_pool_sz * sizeof(*t->link_pool));
	if (!t->link_pool) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E31: Allocating torus link pool: %s\n",
			strerror(errno));
		goto out;
	}
	t->fabric = f;

	/*
	 * Get things started by locating the up to seven switches that
	 * define the torus "seed", coordinate directions, and datelines.
	 */
	o = &t->seed[t->seed_idx];

	i = canonicalize(-o->x_dateline, t->x_sz);
	j = canonicalize(-o->y_dateline, t->y_sz);
	k = canonicalize(-o->z_dateline, t->z_sz);

	if (o->xp_link.end[0].port < 0) {
		ip1 = canonicalize(1 - o->x_dateline, t->x_sz);
		fsw0 = find_f_sw(f, o->xp_link.end[0].n_id);
		fsw1 = find_f_sw(f, o->xp_link.end[1].n_id);
		success =
			install_tswitch(t, i, j, k, fsw0) &&
			install_tswitch(t, ip1, j, k, fsw1) && success;
	}
	if (o->xm_link.end[0].port < 0) {
		im1 = canonicalize(-1 - o->x_dateline, t->x_sz);
		fsw0 = find_f_sw(f, o->xm_link.end[0].n_id);
		fsw1 = find_f_sw(f, o->xm_link.end[1].n_id);
		success =
			install_tswitch(t, i, j, k, fsw0) &&
			install_tswitch(t, im1, j, k, fsw1) && success;
	}
	if (o->yp_link.end[0].port < 0) {
		jp1 = canonicalize(1 - o->y_dateline, t->y_sz);
		fsw0 = find_f_sw(f, o->yp_link.end[0].n_id);
		fsw1 = find_f_sw(f, o->yp_link.end[1].n_id);
		success =
			install_tswitch(t, i, j, k, fsw0) &&
			install_tswitch(t, i, jp1, k, fsw1) && success;
	}
	if (o->ym_link.end[0].port < 0) {
		jm1 = canonicalize(-1 - o->y_dateline, t->y_sz);
		fsw0 = find_f_sw(f, o->ym_link.end[0].n_id);
		fsw1 = find_f_sw(f, o->ym_link.end[1].n_id);
		success =
			install_tswitch(t, i, j, k, fsw0) &&
			install_tswitch(t, i, jm1, k, fsw1) && success;
	}
	if (o->zp_link.end[0].port < 0) {
		kp1 = canonicalize(1 - o->z_dateline, t->z_sz);
		fsw0 = find_f_sw(f, o->zp_link.end[0].n_id);
		fsw1 = find_f_sw(f, o->zp_link.end[1].n_id);
		success =
			install_tswitch(t, i, j, k, fsw0) &&
			install_tswitch(t, i, j, kp1, fsw1) && success;
	}
	if (o->zm_link.end[0].port < 0) {
		km1 = canonicalize(-1 - o->z_dateline, t->z_sz);
		fsw0 = find_f_sw(f, o->zm_link.end[0].n_id);
		fsw1 = find_f_sw(f, o->zm_link.end[1].n_id);
		success =
			install_tswitch(t, i, j, k, fsw0) &&
			install_tswitch(t, i, j, km1, fsw1) && success;
	}
	if (!success)
		goto out;

	if (!t->seed_idx)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Using torus seed configured as default "
			"(seed sw %d,%d,%d GUID 0x%04"PRIx64").\n",
			i, j, k, cl_ntoh64(sw[i][j][k]->n_id));
	else
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Using torus seed configured as backup #%u "
			"(seed sw %d,%d,%d GUID 0x%04"PRIx64").\n",
			t->seed_idx, i, j, k, cl_ntoh64(sw[i][j][k]->n_id));

	/*
	 * Search the fabric and construct the expected torus topology.
	 *
	 * The algorithm is to consider the "cube" formed by eight switch
	 * locations bounded by the corners i, j, k and i+1, j+1, k+1.
	 * For each such cube look at the topology of the switches already
	 * placed in the torus, and deduce which new switches can be placed
	 * into their proper locations in the torus.  Examine each cube
	 * multiple times, until the number of links moved into the torus
	 * topology does not change.
	 */
again:
	nlink = t->link_cnt;

	for (k = 0; k < (int)t->z_sz; k++)
		for (j = 0; j < (int)t->y_sz; j++)
			for (i = 0; i < (int)t->x_sz; i++)
				locate_sw(t, i, j, k);

	if (t->link_cnt != nlink)
		goto again;

	/*
	 * Move all other endpoints into torus/mesh.
	 */
	for (k = 0; k < (int)t->z_sz; k++)
		for (j = 0; j < (int)t->y_sz; j++)
			for (i = 0; i < (int)t->x_sz; i++)
				if (!link_srcsink(t, i, j, k)) {
					success = false;
					goto out;
				}
out:
	return success;
}

/*
 * Returns a count of differences between old and new switches.
 */
static
unsigned tsw_changes(struct t_switch *nsw, struct t_switch *osw)
{
	unsigned p, cnt = 0, port_cnt;
	struct endpoint *npt, *opt;
	struct endpoint *rnpt, *ropt;

	if (nsw && !osw) {
		cnt++;
		OSM_LOG(&nsw->torus->osm->log, OSM_LOG_INFO,
			"New torus switch %d,%d,%d GUID 0x%04"PRIx64"\n",
			nsw->i, nsw->j, nsw->k, cl_ntoh64(nsw->n_id));
		goto out;
	}
	if (osw && !nsw) {
		cnt++;
		OSM_LOG(&osw->torus->osm->log, OSM_LOG_INFO,
			"Lost torus switch %d,%d,%d GUID 0x%04"PRIx64"\n",
			osw->i, osw->j, osw->k, cl_ntoh64(osw->n_id));
		goto out;
	}
	if (!(nsw && osw))
		goto out;

	if (nsw->n_id != osw->n_id) {
		cnt++;
		OSM_LOG(&nsw->torus->osm->log, OSM_LOG_INFO,
			"Torus switch %d,%d,%d GUID "
			"was 0x%04"PRIx64", now 0x%04"PRIx64"\n",
			nsw->i, nsw->j, nsw->k,
			cl_ntoh64(osw->n_id), cl_ntoh64(nsw->n_id));
	}

	if (nsw->port_cnt != osw->port_cnt) {
		cnt++;
		OSM_LOG(&nsw->torus->osm->log, OSM_LOG_INFO,
			"Torus switch %d,%d,%d GUID 0x%04"PRIx64" "
			"had %d ports, now has %d\n",
			nsw->i, nsw->j, nsw->k, cl_ntoh64(nsw->n_id),
			osw->port_cnt, nsw->port_cnt);
	}
	port_cnt = nsw->port_cnt;
	if (port_cnt > osw->port_cnt)
		port_cnt = osw->port_cnt;

	for (p = 0; p < port_cnt; p++) {
		npt = nsw->port[p];
		opt = osw->port[p];

		if (npt && npt->link) {
			if (&npt->link->end[0] == npt)
				rnpt = &npt->link->end[1];
			else
				rnpt = &npt->link->end[0];
		} else
			rnpt = NULL;

		if (opt && opt->link) {
			if (&opt->link->end[0] == opt)
				ropt = &opt->link->end[1];
			else
				ropt = &opt->link->end[0];
		} else
			ropt = NULL;

		if (rnpt && !ropt) {
			++cnt;
			OSM_LOG(&nsw->torus->osm->log, OSM_LOG_INFO,
				"Torus switch %d,%d,%d GUID 0x%04"PRIx64"[%d] "
				"remote now %s GUID 0x%04"PRIx64"[%d], "
				"was missing\n",
				nsw->i, nsw->j, nsw->k, cl_ntoh64(nsw->n_id),
				p, rnpt->type == PASSTHRU ? "sw" : "node",
				cl_ntoh64(rnpt->n_id), rnpt->port);
			continue;
		}
		if (ropt && !rnpt) {
			++cnt;
			OSM_LOG(&nsw->torus->osm->log, OSM_LOG_INFO,
				"Torus switch %d,%d,%d GUID 0x%04"PRIx64"[%d] "
				"remote now missing, "
				"was %s GUID 0x%04"PRIx64"[%d]\n",
				osw->i, osw->j, osw->k, cl_ntoh64(nsw->n_id),
				p, ropt->type == PASSTHRU ? "sw" : "node",
				cl_ntoh64(ropt->n_id), ropt->port);
			continue;
		}
		if (!(rnpt && ropt))
			continue;

		if (rnpt->n_id != ropt->n_id) {
			++cnt;
			OSM_LOG(&nsw->torus->osm->log, OSM_LOG_INFO,
				"Torus switch %d,%d,%d GUID 0x%04"PRIx64"[%d] "
				"remote now %s GUID 0x%04"PRIx64"[%d], "
				"was %s GUID 0x%04"PRIx64"[%d]\n",
				nsw->i, nsw->j, nsw->k, cl_ntoh64(nsw->n_id),
				p, rnpt->type == PASSTHRU ? "sw" : "node",
				cl_ntoh64(rnpt->n_id), rnpt->port,
				ropt->type == PASSTHRU ? "sw" : "node",
				cl_ntoh64(ropt->n_id), ropt->port);
			continue;
		}
	}
out:
	return cnt;
}

static
void dump_torus(struct torus *t)
{
	unsigned i, j, k;
	unsigned x_sz = t->x_sz;
	unsigned y_sz = t->y_sz;
	unsigned z_sz = t->z_sz;
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", t->osm->subn.opt.dump_files_dir,
		 "opensm-torus.dump");
	file = fopen(path, "w");
	if (!file) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E47: cannot create file \'%s\'\n", path);
		return;
	}

	for (k = 0; k < z_sz; k++)
		for (j = 0; j < y_sz; j++)
			for (i = 0; i < x_sz; i++)
				if (t->sw[i][j][k])
					fprintf(file, "switch %u,%u,%u GUID 0x%04"
						PRIx64 " (%s)\n",
						i, j, k,
						cl_ntoh64(t->sw[i][j][k]->n_id),
						t->sw[i][j][k]->osm_switch->p_node->print_desc);
	fclose(file);
}

static
void report_torus_changes(struct torus *nt, struct torus *ot)
{
	unsigned cnt = 0;
	unsigned i, j, k;
	unsigned x_sz = nt->x_sz;
	unsigned y_sz = nt->y_sz;
	unsigned z_sz = nt->z_sz;
	unsigned max_changes = nt->max_changes;

	if (OSM_LOG_IS_ACTIVE_V2(&nt->osm->log, OSM_LOG_ROUTING))
		dump_torus(nt);

	if (!ot)
		return;

	if (x_sz != ot->x_sz) {
		cnt++;
		OSM_LOG(&nt->osm->log, OSM_LOG_INFO,
			"Torus x radix was %d now %d\n",
			ot->x_sz, nt->x_sz);
		if (x_sz > ot->x_sz)
			x_sz = ot->x_sz;
	}
	if (y_sz != ot->y_sz) {
		cnt++;
		OSM_LOG(&nt->osm->log, OSM_LOG_INFO,
			"Torus y radix was %d now %d\n",
			ot->y_sz, nt->y_sz);
		if (y_sz > ot->y_sz)
			y_sz = ot->y_sz;
	}
	if (z_sz != ot->z_sz) {
		cnt++;
		OSM_LOG(&nt->osm->log, OSM_LOG_INFO,
			"Torus z radix was %d now %d\n",
			ot->z_sz, nt->z_sz);
		if (z_sz > ot->z_sz)
			z_sz = ot->z_sz;
	}

	for (k = 0; k < z_sz; k++)
		for (j = 0; j < y_sz; j++)
			for (i = 0; i < x_sz; i++) {
				cnt += tsw_changes(nt->sw[i][j][k],
						   ot->sw[i][j][k]);
				/*
				 * Booting a big fabric will cause lots of
				 * changes as hosts come up, so don't spew.
				 * We want to log changes to learn more about
				 * bouncing links, etc, so they can be fixed.
				 */
				if (cnt > max_changes) {
					OSM_LOG(&nt->osm->log, OSM_LOG_INFO,
						"Too many torus changes; "
						"stopping reporting early\n");
					return;
				}
			}
}

static
void rpt_torus_missing(struct torus *t, int i, int j, int k,
		       struct t_switch *sw, int *missing_z)
{
	uint64_t guid_ho;

	if (!sw) {
		/*
		 * We can have multiple missing switches without deadlock
		 * if and only if they are adajacent in the Z direction.
		 */
		if ((t->switch_cnt + 1) < t->sw_pool_sz) {
			if (t->sw[i][j][canonicalize(k - 1, t->z_sz)] &&
			    t->sw[i][j][canonicalize(k + 1, t->z_sz)])
				t->flags |= MSG_DEADLOCK;
		}
		/*
		 * There can be only one such Z-column of missing switches.
		 */
		if (*missing_z < 0)
			*missing_z = i + j * t->x_sz;
		else if (*missing_z != i + j * t->x_sz)
			t->flags |= MSG_DEADLOCK;

		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Missing torus switch at %d,%d,%d\n", i, j, k);
		return;
	}
	guid_ho = cl_ntoh64(sw->n_id);

	if (!(sw->ptgrp[0].port_cnt || (t->x_sz == 1) ||
	      ((t->flags & X_MESH) && i == 0)))
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Missing torus -x link on "
			"switch %d,%d,%d GUID 0x%04"PRIx64"\n",
			i, j, k, guid_ho);
	if (!(sw->ptgrp[1].port_cnt || (t->x_sz == 1) ||
	      ((t->flags & X_MESH) && (i + 1) == t->x_sz)))
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Missing torus +x link on "
			"switch %d,%d,%d GUID 0x%04"PRIx64"\n",
			i, j, k, guid_ho);
	if (!(sw->ptgrp[2].port_cnt || (t->y_sz == 1) ||
	      ((t->flags & Y_MESH) && j == 0)))
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Missing torus -y link on "
			"switch %d,%d,%d GUID 0x%04"PRIx64"\n",
			i, j, k, guid_ho);
	if (!(sw->ptgrp[3].port_cnt || (t->y_sz == 1) ||
	      ((t->flags & Y_MESH) && (j + 1) == t->y_sz)))
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Missing torus +y link on "
			"switch %d,%d,%d GUID 0x%04"PRIx64"\n",
			i, j, k, guid_ho);
	if (!(sw->ptgrp[4].port_cnt || (t->z_sz == 1) ||
	      ((t->flags & Z_MESH) && k == 0)))
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Missing torus -z link on "
			"switch %d,%d,%d GUID 0x%04"PRIx64"\n",
			i, j, k, guid_ho);
	if (!(sw->ptgrp[5].port_cnt || (t->z_sz == 1) ||
	      ((t->flags & Z_MESH) && (k + 1) == t->z_sz)))
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Missing torus +z link on "
			"switch %d,%d,%d GUID 0x%04"PRIx64"\n",
			i, j, k, guid_ho);
}

/*
 * Returns true if the torus can be successfully routed, false otherwise.
 */
static
bool routable_torus(struct torus *t, struct fabric *f)
{
	int i, j, k, tmp = -1;
	unsigned b2g_cnt, g2b_cnt;
	bool success = true;

	t->flags &= ~MSG_DEADLOCK;

	if (t->link_cnt != f->link_cnt || t->switch_cnt != f->switch_cnt)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO,
			"Warning: Could not construct torus using all "
			"known fabric switches and/or links.\n");

	for (k = 0; k < (int)t->z_sz; k++)
		for (j = 0; j < (int)t->y_sz; j++)
			for (i = 0; i < (int)t->x_sz; i++)
				rpt_torus_missing(t, i, j, k,
						  t->sw[i][j][k], &tmp);
	/*
	 * Check for multiple failures that create disjoint regions on a ring.
	 */
	for (k = 0; k < (int)t->z_sz; k++)
		for (j = 0; j < (int)t->y_sz; j++) {
			b2g_cnt = 0;
			g2b_cnt = 0;
			for (i = 0; i < (int)t->x_sz; i++) {

				if (!t->sw[i][j][k])
					continue;

				if (!t->sw[i][j][k]->ptgrp[0].port_cnt)
					b2g_cnt++;
				if (!t->sw[i][j][k]->ptgrp[1].port_cnt)
					g2b_cnt++;
			}
			if (b2g_cnt != g2b_cnt) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E32: strange failures in "
					"x ring at y=%d  z=%d"
					" b2g_cnt %u g2b_cnt %u\n",
					j, k, b2g_cnt, g2b_cnt);
				success = false;
			}
			if (b2g_cnt > 1) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E33: disjoint failures in "
					"x ring at y=%d  z=%d\n", j, k);
				success = false;
			}
		}

	for (i = 0; i < (int)t->x_sz; i++)
		for (k = 0; k < (int)t->z_sz; k++) {
			b2g_cnt = 0;
			g2b_cnt = 0;
			for (j = 0; j < (int)t->y_sz; j++) {

				if (!t->sw[i][j][k])
					continue;

				if (!t->sw[i][j][k]->ptgrp[2].port_cnt)
					b2g_cnt++;
				if (!t->sw[i][j][k]->ptgrp[3].port_cnt)
					g2b_cnt++;
			}
			if (b2g_cnt != g2b_cnt) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E34: strange failures in "
					"y ring at x=%d  z=%d"
					" b2g_cnt %u g2b_cnt %u\n",
					i, k, b2g_cnt, g2b_cnt);
				success = false;
			}
			if (b2g_cnt > 1) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E35: disjoint failures in "
					"y ring at x=%d  z=%d\n", i, k);
				success = false;
			}
		}

	for (j = 0; j < (int)t->y_sz; j++)
		for (i = 0; i < (int)t->x_sz; i++) {
			b2g_cnt = 0;
			g2b_cnt = 0;
			for (k = 0; k < (int)t->z_sz; k++) {

				if (!t->sw[i][j][k])
					continue;

				if (!t->sw[i][j][k]->ptgrp[4].port_cnt)
					b2g_cnt++;
				if (!t->sw[i][j][k]->ptgrp[5].port_cnt)
					g2b_cnt++;
			}
			if (b2g_cnt != g2b_cnt) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E36: strange failures in "
					"z ring at x=%d  y=%d"
					" b2g_cnt %u g2b_cnt %u\n",
					i, j, b2g_cnt, g2b_cnt);
				success = false;
			}
			if (b2g_cnt > 1) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E37: disjoint failures in "
					"z ring at x=%d  y=%d\n", i, j);
				success = false;
			}
		}

	if (t->flags & MSG_DEADLOCK) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E38: missing switch topology "
			"==> message deadlock!\n");
		success = false;
	}
	return success;
}

/*
 * Use this function to re-establish the pointers between a torus endpoint
 * and an opensm osm_port_t.
 *
 * Typically this is only needed when "opensm --ucast-cache" is used, and
 * a CA link bounces.  When the CA port goes away, the osm_port_t object
 * is destroyed, invalidating the endpoint osm_port_t pointer.  When the
 * link comes back, a new osm_port_t object is created with a NULL priv
 * member.  Thus, when osm_get_torus_sl() is called it is missing the data
 * needed to do its work.  Use this function to fix things up.
 */
static
struct endpoint *osm_port_relink_endpoint(const osm_port_t *osm_port)
{
	guid_t node_guid;
	uint8_t port_num, r_port_num;
	struct t_switch *sw;
	struct endpoint *ep = NULL;
	osm_switch_t *osm_sw;
	osm_physp_t *osm_physp;
	osm_node_t *osm_node, *r_osm_node;

	/*
	 * We need to find the torus endpoint that has the same GUID as
	 * the osm_port.  Rather than search the entire set of endpoints,
	 * we'll try to follow pointers.
	 */
	osm_physp = osm_port->p_physp;
	osm_node = osm_port->p_node;
	port_num = osm_physp_get_port_num(osm_physp);
	node_guid = osm_node_get_node_guid(osm_node);
	/*
	 * Switch management port?
	 */
	if (port_num == 0 &&
	    osm_node_get_type(osm_node) == IB_NODE_TYPE_SWITCH) {

		osm_sw = osm_node->sw;
		if (osm_sw && osm_sw->priv) {
			sw = osm_sw->priv;
			if (sw->osm_switch == osm_sw &&
			    sw->port[0]->n_id == node_guid) {

				ep = sw->port[0];
				goto relink_priv;
			}
		}
	}
	/*
	 * CA port?  Try other end of link.  This should also catch a
	 * router port if it is connected to a switch.
	 */
	r_osm_node = osm_node_get_remote_node(osm_node, port_num, &r_port_num);
	if (!r_osm_node)
		goto out;

	osm_sw = r_osm_node->sw;
	if (!osm_sw)
		goto out;

	sw = osm_sw->priv;
	if (!(sw && sw->osm_switch == osm_sw))
		goto out;

	ep = sw->port[r_port_num];
	if (!(ep && ep->link))
		goto out;

	if (ep->link->end[0].n_id == node_guid) {
		ep = &ep->link->end[0];
		goto relink_priv;
	}
	if (ep->link->end[1].n_id == node_guid) {
		ep = &ep->link->end[1];
		goto relink_priv;
	}
	ep = NULL;
	goto out;

relink_priv:
	/* FIXME:
	 * Unfortunately, we need to cast away const to rebuild the links
	 * between the torus endpoint and the osm_port_t.
	 *
	 * What is really needed is to check whether pr_rcv_get_path_parms()
	 * needs its port objects to be const.  If so, why, and whether
	 * anything can be done about it.
	 */
	((osm_port_t *)osm_port)->priv = ep;
	ep->osm_port = (osm_port_t *)osm_port;
out:
	return ep;
}

/*
 * Computing LFT entries and path SL values:
 *
 * For a pristine torus, we compute LFT entries using XYZ DOR, and select
 * which direction to route on a ring (i.e., the 1-D torus for the coordinate
 * in question) based on shortest path.  We compute the SL to use for the
 * path based on whether we crossed a dateline (where a ring coordinate
 * wraps to zero) for each coordinate.
 *
 * When there is a link/switch failure, we want to compute LFT entries
 * to route around the failure, without changing the path SL.  I.e., we
 * want the SL to reach a given destination from a given source to be
 * independent of the presence or number of failed components in the fabric.
 *
 * In order to make this feasible, we will assume that no ring is broken
 * into disjoint pieces by multiple failures
 *
 * We handle failure by attempting to take the long way around any ring
 * with connectivity interrupted by failed components, unless the path
 * requires a turn on a failed switch.
 *
 * For paths that require a turn on a failed switch, we head towards the
 * failed switch, then turn when progress is blocked by a failure, using a
 * turn allowed under XYZ DOR.  However, such a path will also require a turn
 * that is not a legal XYZ DOR turn, so we construct the SL2VL mapping tables
 * such that XYZ DOR turns use one set of VLs and ZYX DOR turns use a
 * separate set of VLs.
 *
 * Under these rules the algorithm guarantees credit-loop-free routing for a
 * single failed switch, without any change in path SL values.  We can also
 * guarantee credit-loop-free routing for failures of multiple switches, if
 * they are adjacent in the last DOR direction.  Since we use XYZ-DOR,
 * that means failed switches at i,j,k and i,j,k+1 will not cause credit
 * loops.
 *
 * These failure routing rules are intended to prevent paths that cross any
 * coordinate dateline twice (over and back), so we don't need to worry about
 * any ambiguity over which SL to use for such a case.  Also, we cannot have
 * a ring deadlock when a ring is broken by failure and we route the long
 * way around, so we don't need to worry about the impact of such routing
 * on SL choice.
 */

/*
 * Functions to set our SL bit encoding for routing/QoS info.  Combine the
 * resuts of these functions with bitwise or to get final SL.
 *
 * SL bits 0-2 encode whether we "looped" in a given direction
 * on the torus on the path from source to destination.
 *
 * SL bit 3 encodes the QoS level.  We only support two QoS levels.
 *
 * Below we assume TORUS_MAX_DIM == 3 and 0 <= coord_dir < TORUS_MAX_DIM.
 */
static inline
unsigned sl_set_use_loop_vl(bool use_loop_vl, unsigned coord_dir)
{
	return (coord_dir < TORUS_MAX_DIM)
		? ((unsigned)use_loop_vl << coord_dir) : 0;
}

static inline
unsigned sl_set_qos(unsigned qos)
{
	return (unsigned)(!!qos) << TORUS_MAX_DIM;
}

/*
 * Functions to crack our SL bit encoding for routing/QoS info.
 */
static inline
bool sl_get_use_loop_vl(unsigned sl, unsigned coord_dir)
{
	return (coord_dir < TORUS_MAX_DIM)
		? (sl >> coord_dir) & 0x1 : false;
}

static inline
unsigned sl_get_qos(unsigned sl)
{
	return (sl >> TORUS_MAX_DIM) & 0x1;
}

/*
 * Functions to encode routing/QoS info into VL bits.  Combine the resuts of
 * these functions with bitwise or to get final VL.
 *
 * For interswitch links:
 * VL bit 0 encodes whether we need to leave on the "loop" VL.
 *
 * VL bit 1 encodes whether turn is XYZ DOR or ZYX DOR. A 3d mesh/torus
 * has 6 turn types: x-y, y-z, x-z, y-x, z-y, z-x.  The first three are
 * legal XYZ DOR turns, and the second three are legal ZYX DOR turns.
 * Straight-through (x-x, y-y, z-z) paths are legal in both DOR variants,
 * so we'll assign them to XYZ DOR VLs.
 *
 * Note that delivery to switch-local ports (i.e. those that source/sink
 * traffic, rather than forwarding it) cannot cause a deadlock, so that
 * can also use either XYZ or ZYX DOR.
 *
 * VL bit 2 encodes QoS level.
 *
 * For end port links:
 * VL bit 0 encodes QoS level.
 *
 * Note that if VL bit encodings are changed here, the available fabric VL
 * verification in verify_setup() needs to be updated as well.
 */
static inline
unsigned vl_set_loop_vl(bool use_loop_vl)
{
	return use_loop_vl;
}

static inline
unsigned vl_set_qos_vl(unsigned qos)
{
	return (qos & 0x1) << 2;
}

static inline
unsigned vl_set_ca_qos_vl(unsigned qos)
{
	return qos & 0x1;
}

static inline
unsigned vl_set_turn_vl(unsigned in_coord_dir, unsigned out_coord_dir)
{
	unsigned vl = 0;

	if (in_coord_dir != TORUS_MAX_DIM &&
	    out_coord_dir != TORUS_MAX_DIM)
		vl = (in_coord_dir > out_coord_dir)
			? 0x1 << 1 : 0;

	return vl;
}

static
unsigned sl2vl_entry(struct torus *t, struct t_switch *sw,
		     int input_pt, int output_pt, unsigned sl)
{
	unsigned id, od, vl, data_vls;

	if (sw && sw->port[input_pt])
		id = sw->port[input_pt]->pgrp->port_grp / 2;
	else
		id = TORUS_MAX_DIM;

	if (sw && sw->port[output_pt])
		od = sw->port[output_pt]->pgrp->port_grp / 2;
	else
		od = TORUS_MAX_DIM;

	if (sw)
		data_vls = t->osm->subn.min_sw_data_vls;
	else
		data_vls = t->osm->subn.min_data_vls;

	vl = 0;
	if (sw && od != TORUS_MAX_DIM) {
		if (data_vls >= 2)
			vl |= vl_set_loop_vl(sl_get_use_loop_vl(sl, od));
		if (data_vls >= 4)
			vl |= vl_set_turn_vl(id, od);
		if (data_vls >= 8)
			vl |= vl_set_qos_vl(sl_get_qos(sl));
	} else {
		if (data_vls >= 2)
			vl |= vl_set_ca_qos_vl(sl_get_qos(sl));
	}
	return vl;
}

static
void torus_update_osm_sl2vl(void *context, osm_physp_t *osm_phys_port,
			    uint8_t iport_num, uint8_t oport_num,
			    ib_slvl_table_t *osm_oport_sl2vl)
{
	osm_node_t *node = osm_physp_get_node_ptr(osm_phys_port);
	struct torus_context *ctx = context;
	struct t_switch *sw = NULL;
	int sl, vl;

	if (node->sw) {
		sw = node->sw->priv;
		if (sw && sw->osm_switch != node->sw) {
			osm_log_t *log = &ctx->osm->log;
			guid_t guid;

			guid = osm_node_get_node_guid(node);
			OSM_LOG(log, OSM_LOG_INFO,
				"Note: osm_switch (GUID 0x%04"PRIx64") "
				"not in torus fabric description\n",
				cl_ntoh64(guid));
			return;
		}
	}
	for (sl = 0; sl < 16; sl++) {
		vl = sl2vl_entry(ctx->torus, sw, iport_num, oport_num, sl);
		ib_slvl_table_set(osm_oport_sl2vl, sl, vl);
	}
}

static
void torus_update_osm_vlarb(void *context, osm_physp_t *osm_phys_port,
			    uint8_t port_num, ib_vl_arb_table_t *block,
			    unsigned block_length, unsigned block_num)
{
	osm_node_t *node = osm_physp_get_node_ptr(osm_phys_port);
	struct torus_context *ctx = context;
	struct t_switch *sw = NULL;
	unsigned i, next;

	if (node->sw) {
		sw = node->sw->priv;
		if (sw && sw->osm_switch != node->sw) {
			osm_log_t *log = &ctx->osm->log;
			guid_t guid;

			guid = osm_node_get_node_guid(node);
			OSM_LOG(log, OSM_LOG_INFO,
				"Note: osm_switch (GUID 0x%04"PRIx64") "
				"not in torus fabric description\n",
				cl_ntoh64(guid));
			return;
		}
	}

	/*
	 * If osm_phys_port is a switch port that connects to a CA, then
	 * we're using at most VL 0 (for QoS level 0) and VL 1 (for QoS
	 * level 1).  We've been passed the VLarb values for a switch
	 * external port, so we need to fix them up to avoid unexpected
	 * results depending on how the switch handles VLarb values for
	 * unprogrammed VLs.
	 *
	 * For inter-switch links torus-2QoS uses VLs 0-3 to implement
	 * QoS level 0, and VLs 4-7 to implement QoS level 1.
	 *
	 * So, leave VL 0 alone, remap VL 4 to VL 1, zero out the rest,
	 * and compress out the zero entries to the end.
	 */
	if (!sw || !port_num || !sw->port[port_num] ||
	    sw->port[port_num]->pgrp->port_grp != 2 * TORUS_MAX_DIM)
		return;

	next = 0;
	for (i = 0; i < block_length; i++) {
		switch (block->vl_entry[i].vl) {
		case 4:
			block->vl_entry[i].vl = 1;
			/* fall through */
		case 0:
			block->vl_entry[next].vl = block->vl_entry[i].vl;
			block->vl_entry[next].weight = block->vl_entry[i].weight;
			next++;
			/*
			 * If we didn't update vl_entry[i] in place,
			 * fall through to zero it out.
			 */
			if (next > i)
				break;
		default:
			block->vl_entry[i].vl = 0;
			block->vl_entry[i].weight = 0;
			break;
		}
	}
}

/*
 * Computes the path lengths *vl0_len and *vl1_len to get from src
 * to dst on a ring with count switches.
 *
 * *vl0_len is the path length for a direct path; it corresponds to a path
 * that should be assigned to use VL0 in a switch.  *vl1_len is the path
 * length for a path that wraps aroung the ring, i.e. where the ring index
 * goes from count to zero or from zero to count.  It corresponds to the path
 * that should be assigned to use VL1 in a switch.
 */
static
void get_pathlen(unsigned src, unsigned dst, unsigned count,
		 unsigned *vl0_len, unsigned *vl1_len)
{
	unsigned s, l;		/* assume s < l */

	if (dst > src) {
		s = src;
		l = dst;
	} else {
		s = dst;
		l = src;
	}
	*vl0_len = l - s;
	*vl1_len = s + count - l;
}

/*
 * Returns a positive number if we should take the "positive" ring direction
 * to reach dst from src, a negative number if we should take the "negative"
 * ring direction, and 0 if src and dst are the same.  The choice is strictly
 * based on which path is shorter.
 */
static
int ring_dir_idx(unsigned src, unsigned dst, unsigned count)
{
	int r;
	unsigned vl0_len, vl1_len;

	if (dst == src)
		return 0;

	get_pathlen(src, dst, count, &vl0_len, &vl1_len);

	if (dst > src)
		r = vl0_len <= vl1_len ? 1 : -1;
	else
		r = vl0_len <= vl1_len ? -1 : 1;

	return r;
}

/*
 * Returns true if the VL1 path should be used to reach src from dst on a
 * ring, based on which path is shorter.
 */
static
bool use_vl1(unsigned src, unsigned dst, unsigned count)
{
	unsigned vl0_len, vl1_len;

	get_pathlen(src, dst, count, &vl0_len, &vl1_len);

	return vl0_len <= vl1_len ? false : true;
}

/*
 * Returns the next switch in the ring of switches along coordinate direction
 * cdir, in the positive ring direction if rdir is positive, and in the
 * negative ring direction if rdir is negative.
 *
 * Returns NULL if rdir is zero, or there is no next switch.
 */
static
struct t_switch *ring_next_sw(struct t_switch *sw, unsigned cdir, int rdir)
{
	unsigned pt_grp, far_end = 0;

	if (!rdir)
		return NULL;
	/*
	 * Recall that links are installed into the torus so that their 1 end
	 * is in the "positive" coordinate direction relative to their 0 end
	 * (see link_tswitches() and connect_tlink()).  Recall also that for
	 * interswitch links, all links in a given switch port group have the
	 * same endpoints, so we just need to look at the first link.
	 */
	pt_grp = 2 * cdir;
	if (rdir > 0) {
		pt_grp++;
		far_end = 1;
	}

	if (!sw->ptgrp[pt_grp].port_cnt)
		return NULL;

	return sw->ptgrp[pt_grp].port[0]->link->end[far_end].sw;
}

/*
 * Returns a positive number if we should take the "positive" ring direction
 * to reach dsw from ssw, a negative number if we should take the "negative"
 * ring direction, and 0 if src and dst are the same, or if dsw is not
 * reachable from ssw because the path is interrupted by failure.
 */
static
int ring_dir_path(struct torus *t, unsigned cdir,
		  struct t_switch *ssw, struct t_switch *dsw)
{
	int d = 0;
	struct t_switch *sw;

	switch (cdir) {
	case 0:
		d = ring_dir_idx(ssw->i, dsw->i, t->x_sz);
		break;
	case 1:
		d = ring_dir_idx(ssw->j, dsw->j, t->y_sz);
		break;
	case 2:
		d = ring_dir_idx(ssw->k, dsw->k, t->z_sz);
		break;
	default:
		break;
	}
	if (!d)
		goto out;

	sw = ssw;
	while (sw) {
		sw = ring_next_sw(sw, cdir, d);
		if (sw == dsw)
			goto out;
	}
	d *= -1;
	sw = ssw;
	while (sw) {
		sw = ring_next_sw(sw, cdir, d);
		if (sw == dsw)
			goto out;
	}
	d = 0;
out:
	return d;
}

/*
 * Returns true, and sets *pt_grp to the port group index to use for the
 * next hop, if it is possible to make progress from ssw to dsw along the
 * coordinate direction cdir, taking into account whether there are
 * interruptions in the path.
 *
 * This next hop result can be used without worrying about ring deadlocks -
 * if we don't choose the shortest path it is because there is a failure in
 * the ring, which removes the possibilility of a ring deadlock on that ring.
 */
static
bool next_hop_path(struct torus *t, unsigned cdir,
		   struct t_switch *ssw, struct t_switch *dsw,
		   unsigned *pt_grp)
{
	struct t_switch *tsw = NULL;
	bool success = false;
	int d;

	/*
	 * If the path from ssw to dsw turns, this is the switch where the
	 * turn happens.
	 */
	switch (cdir) {
	case 0:
		tsw = t->sw[dsw->i][ssw->j][ssw->k];
		break;
	case 1:
		tsw = t->sw[ssw->i][dsw->j][ssw->k];
		break;
	case 2:
		tsw = t->sw[ssw->i][ssw->j][dsw->k];
		break;
	default:
		goto out;
	}
	if (tsw) {
		d = ring_dir_path(t, cdir, ssw, tsw);
		cdir *= 2;
		if (d > 0)
			*pt_grp = cdir + 1;
		else if (d < 0)
			*pt_grp = cdir;
		else
			goto out;
		success = true;
	}
out:
	return success;
}

/*
 * Returns true, and sets *pt_grp to the port group index to use for the
 * next hop, if it is possible to make progress from ssw to dsw along the
 * coordinate direction cdir.  This decision is made strictly on a
 * shortest-path basis without regard for path availability.
 */
static
bool next_hop_idx(struct torus *t, unsigned cdir,
		  struct t_switch *ssw, struct t_switch *dsw,
		  unsigned *pt_grp)
{
	int d;
	unsigned g;
	bool success = false;

	switch (cdir) {
	case 0:
		d = ring_dir_idx(ssw->i, dsw->i, t->x_sz);
		break;
	case 1:
		d = ring_dir_idx(ssw->j, dsw->j, t->y_sz);
		break;
	case 2:
		d = ring_dir_idx(ssw->k, dsw->k, t->z_sz);
		break;
	default:
		goto out;
	}

	cdir *= 2;
	if (d > 0)
		g = cdir + 1;
	else if (d < 0)
		g = cdir;
	else
		goto out;

	if (!ssw->ptgrp[g].port_cnt)
		goto out;

	*pt_grp = g;
	success = true;
out:
	return success;
}

static
void warn_on_routing(const char *msg,
		     struct t_switch *sw, struct t_switch *dsw)
{
	OSM_LOG(&sw->torus->osm->log, OSM_LOG_ERROR,
		"%s from sw 0x%04"PRIx64" (%d,%d,%d) "
		"to sw 0x%04"PRIx64" (%d,%d,%d)\n",
		msg, cl_ntoh64(sw->n_id), sw->i, sw->j, sw->k,
		cl_ntoh64(dsw->n_id), dsw->i, dsw->j, dsw->k);
}

static
bool next_hop_x(struct torus *t,
		struct t_switch *ssw, struct t_switch *dsw, unsigned *pt_grp)
{
	if (t->sw[dsw->i][ssw->j][ssw->k])
		/*
		 * The next turning switch on this path is available,
		 * so head towards it by the shortest available path.
		 */
		return next_hop_path(t, 0, ssw, dsw, pt_grp);
	else
		/*
		 * The next turning switch on this path is not
		 * available, so head towards it in the shortest
		 * path direction.
		 */
		return next_hop_idx(t, 0, ssw, dsw, pt_grp);
}

static
bool next_hop_y(struct torus *t,
		struct t_switch *ssw, struct t_switch *dsw, unsigned *pt_grp)
{
	if (t->sw[ssw->i][dsw->j][ssw->k])
		/*
		 * The next turning switch on this path is available,
		 * so head towards it by the shortest available path.
		 */
		return next_hop_path(t, 1, ssw, dsw, pt_grp);
	else
		/*
		 * The next turning switch on this path is not
		 * available, so head towards it in the shortest
		 * path direction.
		 */
		return next_hop_idx(t, 1, ssw, dsw, pt_grp);
}

static
bool next_hop_z(struct torus *t,
		struct t_switch *ssw, struct t_switch *dsw, unsigned *pt_grp)
{
	return next_hop_path(t, 2, ssw, dsw, pt_grp);
}

/*
 * Returns the port number on *sw to use to reach *dsw, or -1 if unable to
 * route.
 */
static
int lft_port(struct torus *t,
	     struct t_switch *sw, struct t_switch *dsw,
	     bool update_port_cnt, bool ca)
{
	unsigned g, p;
	struct port_grp *pg;

	/*
	 * The IBA does not provide a way to preserve path history for
	 * routing decisions and VL assignment, and the only mechanism to
	 * provide global fabric knowledge to the routing engine is via
	 * the four SL bits.  This severely constrains the ability to deal
	 * with missing/dead switches.
	 *
	 * Also, if routing a torus with XYZ-DOR, the only way to route
	 * around a missing/dead switch is to introduce a turn that is
	 * illegal under XYZ-DOR.
	 *
	 * But here's what we can do:
	 *
	 * We have a VL bit we use to flag illegal turns, thus putting the
	 * hop directly after an illegal turn on a separate set of VLs.
	 * Unfortunately, since there is no path history,  the _second_
	 * and subsequent hops after an illegal turn use the standard
	 * XYZ-DOR VL set.  This is enough to introduce credit loops in
	 * many cases.
	 *
	 * To minimize the number of cases such illegal turns can introduce
	 * credit loops, we try to introduce the illegal turn as late in a
	 * path as possible.
	 *
	 * Define a turning switch as a switch where a path turns from one
	 * coordinate direction onto another.  If a turning switch in a path
	 * is missing, construct the LFT entries so that the path progresses
	 * as far as possible on the shortest path to the turning switch.
	 * When progress is not possible, turn onto the next coordinate
	 * direction.
	 *
	 * The next turn after that will be an illegal turn, after which
	 * point the path will continue to use a standard XYZ-DOR path.
	 */
	if (dsw->i != sw->i) {

		if (next_hop_x(t, sw, dsw, &g))
			goto done;
		/*
		 * This path has made as much progress in this direction as
		 * is possible, so turn it now.
		 */
		if (dsw->j != sw->j && next_hop_y(t, sw, dsw, &g))
			goto done;

		if (dsw->k != sw->k && next_hop_z(t, sw, dsw, &g))
			goto done;

		warn_on_routing("Error: unable to route", sw, dsw);
		goto no_route;
	} else if (dsw->j != sw->j) {

		if (next_hop_y(t, sw, dsw, &g))
			goto done;

		if (dsw->k != sw->k && next_hop_z(t, sw, dsw, &g))
			goto done;

		warn_on_routing("Error: unable to route", sw, dsw);
		goto no_route;
	} else {
		if (dsw->k == sw->k)
			warn_on_routing("Warning: bad routing", sw, dsw);

		if (next_hop_z(t, sw, dsw, &g))
			goto done;

		warn_on_routing("Error: unable to route", sw, dsw);
		goto no_route;
	}
done:
	pg = &sw->ptgrp[g];
	if (!pg->port_cnt)
		goto no_route;

	if (update_port_cnt) {
		if (ca)
			p = pg->ca_dlid_cnt++ % pg->port_cnt;
		else
			p = pg->sw_dlid_cnt++ % pg->port_cnt;
	} else {
		/*
		 * If we're not updating port counts, then we're just running
		 * routes for SL path checking, and it doesn't matter which
		 * of several parallel links we use.  Use the first one.
		 */
		p = 0;
	}
	p = pg->port[p]->port;

	return p;

no_route:
	/*
	 * We can't get there from here.
	 */
	OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
		"ERR 4E39: routing on sw 0x%04"PRIx64": sending "
		"traffic for dest sw 0x%04"PRIx64" to port %u\n",
		cl_ntoh64(sw->n_id), cl_ntoh64(dsw->n_id), OSM_NO_PATH);
	return -1;
}

static
bool get_lid(struct port_grp *pg, unsigned p,
	     uint16_t *dlid_base, uint8_t *dlid_lmc, bool *ca)
{
	struct endpoint *ep;
	osm_port_t *osm_port;

	if (p >= pg->port_cnt) {
		OSM_LOG(&pg->sw->torus->osm->log, OSM_LOG_ERROR,
			"ERR 4E3A: Port group index %u too large: sw "
			"0x%04"PRIx64" pt_grp %u pt_grp_cnt %u\n",
			p, cl_ntoh64(pg->sw->n_id),
			(unsigned)pg->port_grp, (unsigned)pg->port_cnt);
		return false;
	}
	if (pg->port[p]->type == SRCSINK) {
		ep = pg->port[p];
		if (ca)
			*ca = false;
	} else if (pg->port[p]->type == PASSTHRU &&
		   pg->port[p]->link->end[1].type == SRCSINK) {
		/*
		 * If this port is connected via a link to a CA, then we
		 * know link->end[0] is the switch end and link->end[1] is
		 * the CA end; see build_ca_link() and link_srcsink().
		 */
		ep = &pg->port[p]->link->end[1];
		if (ca)
			*ca = true;
	} else {
		OSM_LOG(&pg->sw->torus->osm->log, OSM_LOG_ERROR,
			"ERR 4E3B: Switch 0x%04"PRIx64" port %d improperly connected\n",
			cl_ntoh64(pg->sw->n_id), pg->port[p]->port);
		return false;
	}
	osm_port = ep->osm_port;
	if (!(osm_port && osm_port->priv == ep)) {
		OSM_LOG(&pg->sw->torus->osm->log, OSM_LOG_ERROR,
			"ERR 4E3C: ep->osm_port->priv != ep "
			"for sw 0x%04"PRIx64" port %d\n",
			cl_ntoh64(((struct t_switch *)(ep->sw))->n_id), ep->port);
		return false;
	}
	*dlid_base = cl_ntoh16(osm_physp_get_base_lid(osm_port->p_physp));
	*dlid_lmc = osm_physp_get_lmc(osm_port->p_physp);

	return true;
}

static
bool torus_lft(struct torus *t, struct t_switch *sw)
{
	bool success = true;
	int dp;
	unsigned p, s;
	uint16_t l, dlid_base;
	uint8_t dlid_lmc;
	bool ca;
	struct port_grp *pgrp;
	struct t_switch *dsw;
	osm_switch_t *osm_sw;
	uint8_t order[IB_NODE_NUM_PORTS_MAX+1];

	if (!(sw->osm_switch && sw->osm_switch->priv == sw)) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"ERR 4E3D: sw->osm_switch->priv != sw "
			"for sw 0x%04"PRIx64"\n", cl_ntoh64(sw->n_id));
		return false;
	}
	osm_sw = sw->osm_switch;
	memset(osm_sw->new_lft, OSM_NO_PATH, osm_sw->lft_size);

	for (s = 0; s < t->switch_cnt; s++) {

		dsw = t->sw_pool[s];
		pgrp = &dsw->ptgrp[2 * TORUS_MAX_DIM];

		memset(order, IB_INVALID_PORT_NUM, sizeof(order));
		for (p = 0; p < pgrp->port_cnt; p++)
			order[pgrp->port[p]->port] = p;

		for (p = 0; p < ARRAY_SIZE(order); p++) {

			uint8_t px = order[t->port_order[p]];

			if (px == IB_INVALID_PORT_NUM)
				continue;

			if (!get_lid(pgrp, px, &dlid_base, &dlid_lmc, &ca))
				return false;

			if (sw->n_id == dsw->n_id)
				dp = pgrp->port[px]->port;
			else
				dp = lft_port(t, sw, dsw, true, ca);
			/*
			 * LMC > 0 doesn't really make sense for torus-2QoS.
			 * So, just make sure traffic gets delivered if
			 * non-zero LMC is used.
			 */
			if (dp >= 0)
				for (l = 0; l < (1U << dlid_lmc); l++)
					osm_sw->new_lft[dlid_base + l] = dp;
			else
				success = false;
		}
	}
	return success;
}

static
osm_mtree_node_t *mcast_stree_branch(struct t_switch *sw, osm_switch_t *osm_sw,
				     osm_mgrp_box_t *mgb, unsigned depth,
				     unsigned *port_cnt, unsigned *max_depth)
{
	osm_mtree_node_t *mtn = NULL;
	osm_mcast_tbl_t *mcast_tbl, *ds_mcast_tbl;
	osm_node_t *ds_node;
	struct t_switch *ds_sw;
	struct port_grp *ptgrp;
	struct link *link;
	struct endpoint *port;
	unsigned g, p;
	unsigned mcast_fwd_ports = 0, mcast_end_ports = 0;

	depth++;

	if (osm_sw->priv != sw) {
		OSM_LOG(&sw->torus->osm->log, OSM_LOG_ERROR,
			"ERR 4E3E: osm_sw (GUID 0x%04"PRIx64") "
			"not in torus fabric description\n",
			cl_ntoh64(osm_node_get_node_guid(osm_sw->p_node)));
		goto out;
	}
	if (!osm_switch_supports_mcast(osm_sw)) {
		OSM_LOG(&sw->torus->osm->log, OSM_LOG_ERROR,
			"ERR 4E3F: osm_sw (GUID 0x%04"PRIx64") "
			"does not support multicast\n",
			cl_ntoh64(osm_node_get_node_guid(osm_sw->p_node)));
		goto out;
	}
	mtn = osm_mtree_node_new(osm_sw);
	if (!mtn) {
		OSM_LOG(&sw->torus->osm->log, OSM_LOG_ERROR,
			"ERR 4E46: Insufficient memory to build multicast tree\n");
		goto out;
	}
	mcast_tbl = osm_switch_get_mcast_tbl_ptr(osm_sw);
	/*
	 * Recurse to downstream switches, i.e. those closer to master
	 * spanning tree branch tips.
	 *
	 * Note that if there are multiple ports in this port group, i.e.,
	 * multiple parallel links, we can pick any one of them to use for
	 * any individual MLID without causing loops.  Pick one based on MLID
	 * for now, until someone turns up evidence we need to be smarter.
	 *
	 * Also, it might be we got called in a window between a switch getting
	 * removed from the fabric, and torus-2QoS getting to rebuild its
	 * fabric representation.  If that were to happen, our next hop
	 * osm_switch pointer might be stale.  Look it up via opensm's fabric
	 * description to be sure it's not.
	 */
	for (g = 0; g < 2 * TORUS_MAX_DIM; g++) {
		ptgrp = &sw->ptgrp[g];
		if (!ptgrp->to_stree_tip)
			continue;

		p = mgb->mlid % ptgrp->port_cnt;/* port # in port group */
		p = ptgrp->port[p]->port;	/* now port # in switch */

		ds_node = osm_node_get_remote_node(osm_sw->p_node, p, NULL);
		ds_sw = ptgrp->to_stree_tip->sw;

		if (!(ds_node && ds_node->sw &&
		      ds_sw->osm_switch == ds_node->sw)) {
			OSM_LOG(&sw->torus->osm->log, OSM_LOG_ERROR,
				"ERR 4E40: stale pointer to osm_sw "
				"(GUID 0x%04"PRIx64")\n", cl_ntoh64(ds_sw->n_id));
			continue;
		}
		mtn->child_array[p] =
			mcast_stree_branch(ds_sw, ds_node->sw, mgb,
					   depth, port_cnt, max_depth);
		if (!mtn->child_array[p])
			continue;

		osm_mcast_tbl_set(mcast_tbl, mgb->mlid, p);
		mcast_fwd_ports++;
		/*
		 * Since we forward traffic for this multicast group on this
		 * port, cause the switch on the other end of the link
		 * to forward traffic back to us.  Do it now since have at
		 * hand the link used; otherwise it'll be hard to figure out
		 * later, and if we get it wrong we get a MC routing loop.
		 */
		link = sw->port[p]->link;
		ds_mcast_tbl = osm_switch_get_mcast_tbl_ptr(ds_node->sw);

		if (&link->end[0] == sw->port[p])
			osm_mcast_tbl_set(ds_mcast_tbl, mgb->mlid,
					  link->end[1].port);
		else
			osm_mcast_tbl_set(ds_mcast_tbl, mgb->mlid,
					  link->end[0].port);
	}
	/*
	 * Add any host ports marked as in mcast group into spanning tree.
	 */
	ptgrp = &sw->ptgrp[2 * TORUS_MAX_DIM];
	for (p = 0; p < ptgrp->port_cnt; p++) {
		port = ptgrp->port[p];
		if (port->tmp) {
			port->tmp = NULL;
			mtn->child_array[port->port] = OSM_MTREE_LEAF;
			osm_mcast_tbl_set(mcast_tbl, mgb->mlid, port->port);
			mcast_end_ports++;
		}
	}
	if (!(mcast_end_ports || mcast_fwd_ports)) {
		osm_mtree_destroy(mtn);
		mtn = NULL;
	} else if (depth > *max_depth)
		*max_depth = depth;

	*port_cnt += mcast_end_ports;
out:
	return mtn;
}

static
osm_port_t *next_mgrp_box_port(osm_mgrp_box_t *mgb,
			       cl_list_item_t **list_iterator,
			       cl_map_item_t **map_iterator)
{
	osm_mgrp_t *mgrp;
	osm_mcm_port_t *mcm_port;
	osm_port_t *osm_port = NULL;
	cl_map_item_t *m_item = *map_iterator;
	cl_list_item_t *l_item = *list_iterator;

next_mgrp:
	if (!l_item)
		l_item = cl_qlist_head(&mgb->mgrp_list);
	if (l_item == cl_qlist_end(&mgb->mgrp_list)) {
		l_item = NULL;
		goto out;
	}
	mgrp = cl_item_obj(l_item, mgrp, list_item);

	if (!m_item)
		m_item = cl_qmap_head(&mgrp->mcm_port_tbl);
	if (m_item == cl_qmap_end(&mgrp->mcm_port_tbl)) {
		m_item = NULL;
		l_item = cl_qlist_next(l_item);
		goto next_mgrp;
	}
	mcm_port = cl_item_obj(m_item, mcm_port, map_item);
	m_item = cl_qmap_next(m_item);
	osm_port = mcm_port->port;
out:
	*list_iterator = l_item;
	*map_iterator = m_item;
	return osm_port;
}

static
ib_api_status_t torus_mcast_stree(void *context, osm_mgrp_box_t *mgb)
{
	struct torus_context *ctx = context;
	struct torus *t = ctx->torus;
	cl_map_item_t *m_item = NULL;
	cl_list_item_t *l_item = NULL;
	osm_port_t *osm_port;
	osm_switch_t *osm_sw;
	struct endpoint *port;
	unsigned port_cnt = 0, max_depth = 0;

	osm_purge_mtree(&ctx->osm->sm, mgb);

	/*
	 * Build a spanning tree for a multicast group by first marking
	 * the torus endpoints that are participating in the group.
	 * Then do a depth-first search of the torus master spanning
	 * tree to build up the spanning tree specific to this group.
	 *
	 * Since the torus master spanning tree is constructed specifically
	 * to guarantee that multicast will not deadlock against unicast
	 * when they share VLs, we can be sure that any multicast group
	 * spanning tree constructed this way has the same property.
	 */
	while ((osm_port = next_mgrp_box_port(mgb, &l_item, &m_item))) {
		port = osm_port->priv;
		if (!(port && port->osm_port == osm_port)) {
			port = osm_port_relink_endpoint(osm_port);
			if (!port) {
				guid_t id;
				id = osm_node_get_node_guid(osm_port->p_node);
				OSM_LOG(&ctx->osm->log, OSM_LOG_ERROR,
					"ERR 4E41: osm_port (GUID 0x%04"PRIx64") "
					"not in torus fabric description\n",
					cl_ntoh64(id));
				continue;
			}
		}
		/*
		 * If this is a CA port, mark the switch port at the
		 * other end of this port's link.
		 *
		 * By definition, a CA port is connected to end[1] of a link,
		 * and the switch port is end[0].  See build_ca_link() and
		 * link_srcsink().
		 */
		if (port->link)
			port = &port->link->end[0];
		port->tmp = osm_port;
	}
	/*
	 * It might be we got called in a window between a switch getting
	 * removed from the fabric, and torus-2QoS getting to rebuild its
	 * fabric representation.  If that were to happen, our
	 * master_stree_root->osm_switch pointer might be stale.  Look up
	 * the osm_switch by GUID to be sure it's not.
	 *
	 * Also, call into mcast_stree_branch with depth = -1, because
	 * depth at root switch needs to be 0.
	 */
	osm_sw = (osm_switch_t *)cl_qmap_get(&ctx->osm->subn.sw_guid_tbl,
					     t->master_stree_root->n_id);
	if (!(osm_sw && t->master_stree_root->osm_switch == osm_sw)) {
		OSM_LOG(&ctx->osm->log, OSM_LOG_ERROR,
			"ERR 4E42: stale pointer to osm_sw (GUID 0x%04"PRIx64")\n",
			cl_ntoh64(t->master_stree_root->n_id));
		return IB_ERROR;
	}
	mgb->root = mcast_stree_branch(t->master_stree_root, osm_sw,
				       mgb, -1, &port_cnt, &max_depth);

	OSM_LOG(&ctx->osm->log, OSM_LOG_VERBOSE,
		"Configured MLID 0x%X for %u ports, max tree depth = %u\n",
		mgb->mlid, port_cnt, max_depth);

	return IB_SUCCESS;
}

static
bool good_xy_ring(struct torus *t, const int x, const int y, const int z)
{
	struct t_switch ****sw = t->sw;
	bool good_ring = true;
	int x_tst, y_tst;

	for (x_tst = 0; x_tst < t->x_sz && good_ring; x_tst++)
		good_ring = sw[x_tst][y][z];

	for (y_tst = 0; y_tst < t->y_sz && good_ring; y_tst++)
		good_ring = sw[x][y_tst][z];

	return good_ring;
}

static
struct t_switch *find_plane_mid(struct torus *t, const int z)
{
	int x, dx, xm = t->x_sz / 2;
	int y, dy, ym = t->y_sz / 2;
	struct t_switch ****sw = t->sw;

	if (good_xy_ring(t, xm, ym, z))
		return sw[xm][ym][z];

	for (dx = 1, dy = 1; dx <= xm && dy <= ym; dx++, dy++) {

		x = canonicalize(xm - dx, t->x_sz);
		y = canonicalize(ym - dy, t->y_sz);
		if (good_xy_ring(t, x, y, z))
			return sw[x][y][z];

		x = canonicalize(xm + dx, t->x_sz);
		y = canonicalize(ym + dy, t->y_sz);
		if (good_xy_ring(t, x, y, z))
			return sw[x][y][z];
	}
	return NULL;
}

static
struct t_switch *find_stree_root(struct torus *t)
{
	int x, y, z, dz, zm = t->z_sz / 2;
	struct t_switch ****sw = t->sw;
	struct t_switch *root;
	bool good_plane;

	/*
	 * Look for a switch near the "center" (wrt. the datelines) of the
	 * torus, as that will be the most optimum spanning tree root.  Use
	 * a search that is not exhaustive, on the theory that this routing
	 * engine isn't useful anyway if too many switches are missing.
	 *
	 * Also, want to pick an x-y plane with no missing switches, so that
	 * the master spanning tree construction algorithm doesn't have to
	 * deal with needing a turn on a missing switch.
	 */
	for (dz = 0; dz <= zm; dz++) {

		z = canonicalize(zm - dz, t->z_sz);
		good_plane = true;
		for (y = 0; y < t->y_sz && good_plane; y++)
			for (x = 0; x < t->x_sz && good_plane; x++)
				good_plane = sw[x][y][z];

		if (good_plane) {
			root = find_plane_mid(t, z);
			if (root)
				goto out;
		}
		if (!dz)
			continue;

		z = canonicalize(zm + dz, t->z_sz);
		good_plane = true;
		for (y = 0; y < t->y_sz && good_plane; y++)
			for (x = 0; x < t->x_sz && good_plane; x++)
				good_plane = sw[x][y][z];

		if (good_plane) {
			root = find_plane_mid(t, z);
			if (root)
				goto out;
		}
	}
	/*
	 * Note that torus-2QoS can route a torus that is missing an entire
	 * column (switches with x,y constant, for all z values) without
	 * deadlocks.
	 *
	 * if we've reached this point, we must have a column of missing
	 * switches, as routable_torus() would have returned false for
	 * any other configuration of missing switches that made it through
	 * the above.
	 *
	 * So any switch in the mid-z plane will do as the root.
	 */
	root = find_plane_mid(t, zm);
out:
	return root;
}

static
bool sw_in_master_stree(struct t_switch *sw)
{
	int g;
	bool connected;

	connected = sw == sw->torus->master_stree_root;
	for (g = 0; g < 2 * TORUS_MAX_DIM; g++)
		connected = connected || sw->ptgrp[g].to_stree_root;

	return connected;
}

static
void grow_master_stree_branch(struct t_switch *root, struct t_switch *tip,
			      unsigned to_root_pg, unsigned to_tip_pg)
{
	root->ptgrp[to_tip_pg].to_stree_tip = &tip->ptgrp[to_root_pg];
	tip->ptgrp[to_root_pg].to_stree_root = &root->ptgrp[to_tip_pg];
}

static
void build_master_stree_branch(struct t_switch *branch_root, int cdir)
{
	struct t_switch *sw, *n_sw, *p_sw;
	unsigned l, idx, cnt, pg, ng;

	switch (cdir) {
	case 0:
		idx = branch_root->i;
		cnt = branch_root->torus->x_sz;
		break;
	case 1:
		idx = branch_root->j;
		cnt = branch_root->torus->y_sz;
		break;
	case 2:
		idx = branch_root->k;
		cnt = branch_root->torus->z_sz;
		break;
	default:
		goto out;
	}
	/*
	 * This algorithm intends that a spanning tree branch never crosses
	 * a dateline unless the 1-D ring for which we're building the branch
	 * is interrupted by failure.  We need that guarantee to prevent
	 * multicast/unicast credit loops.
	 */
	n_sw = branch_root;		/* tip of negative cdir branch */
	ng = 2 * cdir;			/* negative cdir port group index */
	p_sw = branch_root;		/* tip of positive cdir branch */
	pg = 2 * cdir + 1;		/* positive cdir port group index */

	for (l = idx; n_sw && l >= 1; l--) {
		sw = ring_next_sw(n_sw, cdir, -1);
		if (sw && !sw_in_master_stree(sw)) {
			grow_master_stree_branch(n_sw, sw, pg, ng);
			n_sw = sw;
		} else
			n_sw = NULL;
	}
	for (l = idx; p_sw && l < (cnt - 1); l++) {
		sw = ring_next_sw(p_sw, cdir, 1);
		if (sw && !sw_in_master_stree(sw)) {
			grow_master_stree_branch(p_sw, sw, ng, pg);
			p_sw = sw;
		} else
			p_sw = NULL;
	}
	if (n_sw && p_sw)
		goto out;
	/*
	 * At least one branch couldn't grow to the dateline for this ring.
	 * That means it is acceptable to grow the branch by crossing the
	 * dateline.
	 */
	for (l = 0; l < cnt; l++) {
		if (n_sw) {
			sw = ring_next_sw(n_sw, cdir, -1);
			if (sw && !sw_in_master_stree(sw)) {
				grow_master_stree_branch(n_sw, sw, pg, ng);
				n_sw = sw;
			} else
				n_sw = NULL;
		}
		if (p_sw) {
			sw = ring_next_sw(p_sw, cdir, 1);
			if (sw && !sw_in_master_stree(sw)) {
				grow_master_stree_branch(p_sw, sw, ng, pg);
				p_sw = sw;
			} else
				p_sw = NULL;
		}
		if (!(n_sw || p_sw))
			break;
	}
out:
	return;
}

static
bool torus_master_stree(struct torus *t)
{
	int i, j, k;
	bool success = false;
	struct t_switch *stree_root = find_stree_root(t);

	if (stree_root)
		build_master_stree_branch(stree_root, 0);
	else
		goto out;

	k = stree_root->k;
	for (i = 0; i < t->x_sz; i++) {
		j = stree_root->j;
		if (t->sw[i][j][k])
			build_master_stree_branch(t->sw[i][j][k], 1);

		for (j = 0; j < t->y_sz; j++)
			if (t->sw[i][j][k])
				build_master_stree_branch(t->sw[i][j][k], 2);
	}
	t->master_stree_root = stree_root;
	/*
	 * At this point we should have a master spanning tree that contains
	 * every present switch, for all fabrics that torus-2QoS can route
	 * without deadlocks.  Make sure this is the case; otherwise warn
	 * and return failure so we get bug reports.
	 */
	success = true;
	for (i = 0; i < t->x_sz; i++)
		for (j = 0; j < t->y_sz; j++)
			for (k = 0; k < t->z_sz; k++) {
				struct t_switch *sw = t->sw[i][j][k];
				if (!sw || sw_in_master_stree(sw))
					continue;

				success = false;
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"ERR 4E43: sw 0x%04"PRIx64" (%d,%d,%d) not in "
					"torus multicast master spanning tree\n",
					cl_ntoh64(sw->n_id), i, j, k);
			}
out:
	return success;
}

int route_torus(struct torus *t)
{
	int s;
	bool success = true;

	for (s = 0; s < (int)t->switch_cnt; s++)
		success = torus_lft(t, t->sw_pool[s]) && success;

	success = success && torus_master_stree(t);

	return success ? 0 : -1;
}

uint8_t torus_path_sl(void *context, uint8_t path_sl_hint,
		      const ib_net16_t slid, const ib_net16_t dlid)
{
	struct torus_context *ctx = context;
	osm_opensm_t *p_osm = ctx->osm;
	osm_log_t *log = &p_osm->log;
	osm_port_t *osm_sport, *osm_dport;
	struct endpoint *sport, *dport;
	struct t_switch *ssw, *dsw;
	struct torus *t;
	guid_t guid;
	unsigned sl = 0;

	osm_sport = osm_get_port_by_lid(&p_osm->subn, slid);
	if (!osm_sport)
		goto out;

	osm_dport = osm_get_port_by_lid(&p_osm->subn, dlid);
	if (!osm_dport)
		goto out;

	sport = osm_sport->priv;
	if (!(sport && sport->osm_port == osm_sport)) {
		sport = osm_port_relink_endpoint(osm_sport);
		if (!sport) {
			guid = osm_node_get_node_guid(osm_sport->p_node);
			OSM_LOG(log, OSM_LOG_INFO,
				"Note: osm_sport (GUID 0x%04"PRIx64") "
				"not in torus fabric description\n",
				cl_ntoh64(guid));
			goto out;
		}
	}
	dport = osm_dport->priv;
	if (!(dport && dport->osm_port == osm_dport)) {
		dport = osm_port_relink_endpoint(osm_dport);
		if (!dport) {
			guid = osm_node_get_node_guid(osm_dport->p_node);
			OSM_LOG(log, OSM_LOG_INFO,
				"Note: osm_dport (GUID 0x%04"PRIx64") "
				"not in torus fabric description\n",
				cl_ntoh64(guid));
			goto out;
		}
	}
	/*
	 * We're only supposed to be called for CA ports, and maybe
	 * switch management ports.
	 */
	if (sport->type != SRCSINK) {
		guid = osm_node_get_node_guid(osm_sport->p_node);
		OSM_LOG(log, OSM_LOG_INFO,
			"Error: osm_sport (GUID 0x%04"PRIx64") "
			"not a data src/sink port\n", cl_ntoh64(guid));
		goto out;
	}
	if (dport->type != SRCSINK) {
		guid = osm_node_get_node_guid(osm_dport->p_node);
		OSM_LOG(log, OSM_LOG_INFO,
			"Error: osm_dport (GUID 0x%04"PRIx64") "
			"not a data src/sink port\n", cl_ntoh64(guid));
		goto out;
	}
	/*
	 * By definition, a CA port is connected to end[1] of a link, and
	 * the switch port is end[0].  See build_ca_link() and link_srcsink().
	 */
	if (sport->link) {
		ssw = sport->link->end[0].sw;
	} else {
		ssw = sport->sw;
	}
	if (dport->link)
		dsw = dport->link->end[0].sw;
	else
		dsw = dport->sw;

	t = ssw->torus;

	sl  = sl_set_use_loop_vl(use_vl1(ssw->i, dsw->i, t->x_sz), 0);
	sl |= sl_set_use_loop_vl(use_vl1(ssw->j, dsw->j, t->y_sz), 1);
	sl |= sl_set_use_loop_vl(use_vl1(ssw->k, dsw->k, t->z_sz), 2);
	sl |= sl_set_qos(sl_get_qos(path_sl_hint));
out:
	return sl;
}

static
void sum_vlarb_weights(const char *vlarb_str,
		       unsigned total_weight[IB_MAX_NUM_VLS])
{
	unsigned i = 0, v, vl = 0;
	char *end;

	while (*vlarb_str && i++ < 2 * IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK) {
		v = strtoul(vlarb_str, &end, 0);
		if (*end)
			end++;
		vlarb_str = end;
		if (i & 0x1)
			vl = v & 0xf;
		else
			total_weight[vl] += v & 0xff;
	}
}

static
int uniform_vlarb_weight_value(unsigned *weight, unsigned count)
{
	int i, v = weight[0];

	for (i = 1; i < count; i++) {
		if (v != weight[i])
			return -1;
	}
	return v;
}

static
void check_vlarb_config(const char *vlarb_str, bool is_default,
			const char *str, const char *pri, osm_log_t *log)
{
	unsigned total_weight[IB_MAX_NUM_VLS] = {0,};

	sum_vlarb_weights(vlarb_str, total_weight);
	if (!(uniform_vlarb_weight_value(&total_weight[0], 4) >= 0 &&
	      uniform_vlarb_weight_value(&total_weight[4], 4) >= 0))
		OSM_LOG(log, OSM_LOG_INFO,
			"Warning: torus-2QoS requires same VLarb weights for "
			"VLs 0-3; also for VLs 4-7: not true for %s "
			"%s_vlarb_%s\n",
			(is_default ? "default" : "configured"), str, pri);
}

/*
 * Use this to check the qos_config for switch external ports.
 */
static
void check_qos_swe_config(osm_qos_options_t *opt,
			  osm_qos_options_t *def, osm_log_t *log)
{
	const char *vlarb_str, *tstr;
	bool is_default;
	unsigned max_vls;

	max_vls = def->max_vls;
	if (opt->max_vls > 0)
		max_vls = opt->max_vls;

	if (max_vls > 0 && max_vls < 8)
		OSM_LOG(log, OSM_LOG_INFO,
			"Warning: full torus-2QoS functionality not available "
			"for configured %s_max_vls = %d\n",
			(opt->max_vls > 0 ? "qos_swe" : "qos"), opt->max_vls);

	vlarb_str = opt->vlarb_high;
	is_default = false;
	tstr = "qos_swe";
	if (!vlarb_str) {
		vlarb_str = def->vlarb_high;
		tstr = "qos";
	}
	if (!vlarb_str) {
		vlarb_str = OSM_DEFAULT_QOS_VLARB_HIGH;
		is_default = true;
	}
	check_vlarb_config(vlarb_str, is_default, tstr, "high", log);

	vlarb_str = opt->vlarb_low;
	is_default = false;
	tstr = "qos_swe";
	if (!vlarb_str) {
		vlarb_str = def->vlarb_low;
		tstr = "qos";
	}
	if (!vlarb_str) {
		vlarb_str = OSM_DEFAULT_QOS_VLARB_LOW;
		is_default = true;
	}
	check_vlarb_config(vlarb_str, is_default, tstr, "low", log);

	if (opt->sl2vl)
		OSM_LOG(log, OSM_LOG_INFO,
			"Warning: torus-2QoS must override configured "
			"qos_swe_sl2vl to generate deadlock-free routes\n");
}

static
void check_ep_vlarb_config(const char *vlarb_str,
			   bool is_default, bool is_specific,
			   const char *str, const char *pri, osm_log_t *log)
{
	unsigned i, total_weight[IB_MAX_NUM_VLS] = {0,};
	int val = 0;

	sum_vlarb_weights(vlarb_str, total_weight);
	for (i = 2; i < 8; i++) {
		val += total_weight[i];
	}
	if (!val)
		return;

	if (is_specific)
		OSM_LOG(log, OSM_LOG_INFO,
			"Warning: torus-2QoS recommends 0 VLarb weights"
			" for VLs 2-7 on endpoint links; not true for "
			" configured %s_vlarb_%s\n", str, pri);
	else
		OSM_LOG(log, OSM_LOG_INFO,
			"Warning: torus-2QoS recommends 0 VLarb weights "
			"for VLs 2-7 on endpoint links; not true for %s "
			"qos_vlarb_%s values used for %s_vlarb_%s\n",
			(is_default ? "default" : "configured"), pri, str, pri);
}

/*
 * Use this to check the qos_config for endports
 */
static
void check_qos_ep_config(osm_qos_options_t *opt, osm_qos_options_t *def,
			 const char *str, osm_log_t *log)
{
	const char *vlarb_str;
	bool is_default, is_specific;
	unsigned max_vls;

	max_vls = def->max_vls;
	if (opt->max_vls > 0)
		max_vls = opt->max_vls;

	if (max_vls > 0 && max_vls < 2)
		OSM_LOG(log, OSM_LOG_INFO,
			"Warning: full torus-2QoS functionality not available "
			"for configured %s_max_vls = %d\n",
			(opt->max_vls > 0 ? str : "qos"), opt->max_vls);

	vlarb_str = opt->vlarb_high;
	is_default = false;
	is_specific = true;
	if (!vlarb_str) {
		vlarb_str = def->vlarb_high;
		is_specific = false;
	}
	if (!vlarb_str) {
		vlarb_str = OSM_DEFAULT_QOS_VLARB_HIGH;
		is_default = true;
	}
	check_ep_vlarb_config(vlarb_str, is_default, is_specific,
			      str, "high", log);

	vlarb_str = opt->vlarb_low;
	is_default = false;
	is_specific = true;
	if (!vlarb_str) {
		vlarb_str = def->vlarb_low;
		is_specific = false;
	}
	if (!vlarb_str) {
		vlarb_str = OSM_DEFAULT_QOS_VLARB_LOW;
		is_default = true;
	}
	check_ep_vlarb_config(vlarb_str, is_default, is_specific,
			      str, "low", log);

	if (opt->sl2vl)
		OSM_LOG(log, OSM_LOG_INFO,
			"Warning: torus-2QoS must override configured "
			"%s_sl2vl to generate deadlock-free routes\n", str);
}

static
int torus_build_lfts(void *context)
{
	int status = -1;
	struct torus_context *ctx = context;
	struct fabric *fabric;
	struct torus *torus;

	if (!ctx->osm->subn.opt.qos) {
		OSM_LOG(&ctx->osm->log, OSM_LOG_ERROR,
			"ERR 4E44: Routing engine list contains torus-2QoS. "
			"Enable QoS for correct operation "
			"(-Q or 'qos TRUE' in opensm.conf).\n");
		return status;
	}

	fabric = &ctx->fabric;
	teardown_fabric(fabric);

	torus = calloc(1, sizeof(*torus));
	if (!torus) {
		OSM_LOG(&ctx->osm->log, OSM_LOG_ERROR,
			"ERR 4E45: allocating torus: %s\n", strerror(errno));
		goto out;
	}
	torus->osm = ctx->osm;
	fabric->osm = ctx->osm;

	if (!parse_config(ctx->osm->subn.opt.torus_conf_file,
			  fabric, torus))
		goto out;

	if (!capture_fabric(fabric))
		goto out;

	OSM_LOG(&torus->osm->log, OSM_LOG_INFO,
		"Found fabric w/ %d links, %d switches, %d CA ports, "
		"minimum data VLs: endport %d, switchport %d\n",
		(int)fabric->link_cnt, (int)fabric->switch_cnt,
		(int)fabric->ca_cnt, (int)ctx->osm->subn.min_data_vls,
		(int)ctx->osm->subn.min_sw_data_vls);

	if (!verify_setup(torus, fabric))
		goto out;

	OSM_LOG(&torus->osm->log, OSM_LOG_INFO,
		"Looking for %d x %d x %d %s\n",
		(int)torus->x_sz, (int)torus->y_sz, (int)torus->z_sz,
		(ALL_MESH(torus->flags) ? "mesh" : "torus"));

	if (!build_torus(fabric, torus)) {
		OSM_LOG(&torus->osm->log, OSM_LOG_ERROR, "ERR 4E57: "
			"build_torus finished with errors\n");
		goto out;
	}

	OSM_LOG(&torus->osm->log, OSM_LOG_INFO,
		"Built %d x %d x %d %s w/ %d links, %d switches, %d CA ports\n",
		(int)torus->x_sz, (int)torus->y_sz, (int)torus->z_sz,
		(ALL_MESH(torus->flags) ? "mesh" : "torus"),
		(int)torus->link_cnt, (int)torus->switch_cnt,
		(int)torus->ca_cnt);

	diagnose_fabric(fabric);
	/*
	 * Since we found some sort of torus fabric, report on any topology
	 * changes vs. the last torus we found.
	 */
	if (torus->flags & NOTIFY_CHANGES)
		report_torus_changes(torus, ctx->torus);

	if (routable_torus(torus, fabric))
		status = route_torus(torus);

out:
	if (status) {		/* bad torus!! */
		if (torus)
			teardown_torus(torus);
	} else {
		osm_subn_opt_t *opt = &torus->osm->subn.opt;
		osm_log_t *log = &torus->osm->log;

		if (ctx->torus)
			teardown_torus(ctx->torus);
		ctx->torus = torus;

		check_qos_swe_config(&opt->qos_swe_options, &opt->qos_options,
				     log);

		check_qos_ep_config(&opt->qos_ca_options,
				    &opt->qos_options, "qos_ca", log);
		check_qos_ep_config(&opt->qos_sw0_options,
				    &opt->qos_options, "qos_sw0", log);
		check_qos_ep_config(&opt->qos_rtr_options,
				    &opt->qos_options, "qos_rtr", log);
	}
	teardown_fabric(fabric);
	return status;
}

int osm_ucast_torus2QoS_setup(struct osm_routing_engine *r,
			      osm_opensm_t *osm)
{
	struct torus_context *ctx;

	ctx = torus_context_create(osm);
	if (!ctx)
		return -1;

	r->context = ctx;
	r->ucast_build_fwd_tables = torus_build_lfts;
	r->build_lid_matrices = ucast_dummy_build_lid_matrices;
	r->update_sl2vl = torus_update_osm_sl2vl;
	r->update_vlarb = torus_update_osm_vlarb;
	r->path_sl = torus_path_sl;
	r->mcast_build_stree = torus_mcast_stree;
	r->destroy = torus_context_delete;
	return 0;
}
