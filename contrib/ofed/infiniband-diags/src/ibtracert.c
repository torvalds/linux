/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
 * Copyright (c) 2010,2011 Mellanox Technologies LTD.  All rights reserved.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include <netinet/in.h>
#include <inttypes.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include <complib/cl_nodenamemap.h>

#include "ibdiag_common.h"

struct ibmad_port *srcport;

#define MAXHOPS	63

static char *node_type_str[] = {
	"???",
	"ca",
	"switch",
	"router",
	"iwarp rnic"
};

static int timeout = 0;		/* ms */
static int force;
static FILE *f;

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;

typedef struct Port Port;
typedef struct Switch Switch;
typedef struct Node Node;

struct Port {
	Port *next;
	Port *remoteport;
	uint64_t portguid;
	int portnum;
	int lid;
	int lmc;
	int state;
	int physstate;
	char portinfo[64];
};

struct Switch {
	int linearcap;
	int mccap;
	int linearFDBtop;
	int fdb_base;
	int enhsp0;
	int8_t fdb[64];
	char switchinfo[64];
};

struct Node {
	Node *htnext;
	Node *dnext;
	Port *ports;
	ib_portid_t path;
	int type;
	int dist;
	int numports;
	int upport;
	Node *upnode;
	uint64_t nodeguid;	/* also portguid */
	char nodedesc[64];
	char nodeinfo[64];
};

Node *nodesdist[MAXHOPS];
uint64_t target_portguid;

/*
 * is_port_inactive
 * Checks whether or not the port state is other than active.
 * The "sw" argument is only relevant when the port is on a
 * switch; for HCAs and routers, this argument is ignored.
 * Returns 1 when port is not active and 0 when active.
 * Base switch port 0 is considered always active.
 */
static int is_port_inactive(Node * node, Port * port, Switch * sw)
{
	int res = 0;
	if (port->state != 4 &&
	    (node->type != IB_NODE_SWITCH ||
	     (node->type == IB_NODE_SWITCH && sw->enhsp0)))
		res = 1;
	return res;
}

static int get_node(Node * node, Port * port, ib_portid_t * portid)
{
	void *pi = port->portinfo, *ni = node->nodeinfo, *nd = node->nodedesc;
	char *s, *e;

	memset(ni, 0, sizeof(node->nodeinfo));
	if (!smp_query_via(ni, portid, IB_ATTR_NODE_INFO, 0, timeout, srcport))
		return -1;

	memset(nd, 0, sizeof(node->nodedesc));
	if (!smp_query_via(nd, portid, IB_ATTR_NODE_DESC, 0, timeout, srcport))
		return -1;

	for (s = nd, e = s + 64; s < e; s++) {
		if (!*s)
			break;
		if (!isprint(*s))
			*s = ' ';
	}

	memset(pi, 0, sizeof(port->portinfo));
	if (!smp_query_via(pi, portid, IB_ATTR_PORT_INFO, 0, timeout, srcport))
		return -1;

	mad_decode_field(ni, IB_NODE_GUID_F, &node->nodeguid);
	mad_decode_field(ni, IB_NODE_TYPE_F, &node->type);
	mad_decode_field(ni, IB_NODE_NPORTS_F, &node->numports);

	mad_decode_field(ni, IB_NODE_PORT_GUID_F, &port->portguid);
	mad_decode_field(ni, IB_NODE_LOCAL_PORT_F, &port->portnum);
	mad_decode_field(pi, IB_PORT_LID_F, &port->lid);
	mad_decode_field(pi, IB_PORT_LMC_F, &port->lmc);
	mad_decode_field(pi, IB_PORT_STATE_F, &port->state);

	DEBUG("portid %s: got node %" PRIx64 " '%s'", portid2str(portid),
	      node->nodeguid, node->nodedesc);
	return 0;
}

static int switch_lookup(Switch * sw, ib_portid_t * portid, int lid)
{
	void *si = sw->switchinfo, *fdb = sw->fdb;

	memset(si, 0, sizeof(sw->switchinfo));
	if (!smp_query_via(si, portid, IB_ATTR_SWITCH_INFO, 0, timeout,
			   srcport))
		return -1;

	mad_decode_field(si, IB_SW_LINEAR_FDB_CAP_F, &sw->linearcap);
	mad_decode_field(si, IB_SW_LINEAR_FDB_TOP_F, &sw->linearFDBtop);
	mad_decode_field(si, IB_SW_ENHANCED_PORT0_F, &sw->enhsp0);

	if (lid >= sw->linearcap && lid > sw->linearFDBtop)
		return -1;

	memset(fdb, 0, sizeof(sw->fdb));
	if (!smp_query_via(fdb, portid, IB_ATTR_LINEARFORWTBL, lid / 64,
			   timeout, srcport))
		return -1;

	DEBUG("portid %s: forward lid %d to port %d",
	      portid2str(portid), lid, sw->fdb[lid % 64]);
	return sw->fdb[lid % 64];
}

static int sameport(Port * a, Port * b)
{
	return a->portguid == b->portguid || (force && a->lid == b->lid);
}

static int extend_dpath(ib_dr_path_t * path, int nextport)
{
	if (path->cnt + 2 >= sizeof(path->p))
		return -1;
	++path->cnt;
	path->p[path->cnt] = (uint8_t) nextport;
	return path->cnt;
}

static void dump_endnode(int dump, char *prompt, Node * node, Port * port)
{
	char *nodename = NULL;

	if (!dump)
		return;
	if (dump == 1) {
		fprintf(f, "%s {0x%016" PRIx64 "}[%d]\n",
			prompt, node->nodeguid,
			node->type == IB_NODE_SWITCH ? 0 : port->portnum);
		return;
	}

	nodename =
	    remap_node_name(node_name_map, node->nodeguid, node->nodedesc);

	fprintf(f, "%s %s {0x%016" PRIx64 "} portnum %d lid %u-%u \"%s\"\n",
		prompt,
		(node->type <= IB_NODE_MAX ? node_type_str[node->type] : "???"),
		node->nodeguid,
		node->type == IB_NODE_SWITCH ? 0 : port->portnum, port->lid,
		port->lid + (1 << port->lmc) - 1, nodename);

	free(nodename);
}

static void dump_route(int dump, Node * node, int outport, Port * port)
{
	char *nodename = NULL;

	if (!dump && !ibverbose)
		return;

	nodename =
	    remap_node_name(node_name_map, node->nodeguid, node->nodedesc);

	if (dump == 1)
		fprintf(f, "[%d] -> {0x%016" PRIx64 "}[%d]\n",
			outport, port->portguid, port->portnum);
	else
		fprintf(f, "[%d] -> %s port {0x%016" PRIx64
			"}[%d] lid %u-%u \"%s\"\n", outport,
			(node->type <=
			 IB_NODE_MAX ? node_type_str[node->type] : "???"),
			port->portguid, port->portnum, port->lid,
			port->lid + (1 << port->lmc) - 1, nodename);

	free(nodename);
}

static int find_route(ib_portid_t * from, ib_portid_t * to, int dump)
{
	Node *node, fromnode, tonode, nextnode;
	Port *port, fromport, toport, nextport;
	Switch sw;
	int maxhops = MAXHOPS;
	int portnum, outport = 255, next_sw_outport = 255;

	memset(&fromnode,0,sizeof(Node));
	memset(&tonode,0,sizeof(Node));
	memset(&nextnode,0,sizeof(Node));
	memset(&fromport,0,sizeof(Port));
	memset(&toport,0,sizeof(Port));
	memset(&nextport,0,sizeof(Port));

	DEBUG("from %s", portid2str(from));

	if (get_node(&fromnode, &fromport, from) < 0 ||
	    get_node(&tonode, &toport, to) < 0) {
		IBWARN("can't reach to/from ports");
		if (!force)
			return -1;
		if (to->lid > 0)
			toport.lid = to->lid;
		IBWARN("Force: look for lid %d", to->lid);
	}

	node = &fromnode;
	port = &fromport;
	portnum = port->portnum;

	dump_endnode(dump, "From", node, port);
	if (node->type == IB_NODE_SWITCH) {
		next_sw_outport = switch_lookup(&sw, from, to->lid);
		if (next_sw_outport < 0 || next_sw_outport > node->numports) {
			/* Need to print the port in badtbl */
			outport = next_sw_outport;
			goto badtbl;
		}
	}

	while (maxhops--) {
		if (is_port_inactive(node, port, &sw))
			goto badport;

		if (sameport(port, &toport))
			break;	/* found */

		if (node->type == IB_NODE_SWITCH) {
			DEBUG("switch node");
			outport = next_sw_outport;

			if (extend_dpath(&from->drpath, outport) < 0)
				goto badpath;

			if (get_node(&nextnode, &nextport, from) < 0) {
				IBWARN("can't reach port at %s",
				       portid2str(from));
				return -1;
			}
			if (outport == 0) {
				if (!sameport(&nextport, &toport))
					goto badtbl;
				else
					break;	/* found SMA port */
			}
		} else if ((node->type == IB_NODE_CA) ||
			   (node->type == IB_NODE_ROUTER)) {
			int ca_src = 0;

			outport = portnum;
			DEBUG("ca or router node");
			if (!sameport(port, &fromport)) {
				IBWARN
				    ("can't continue: reached CA or router port %"
				     PRIx64 ", lid %d", port->portguid,
				     port->lid);
				return -1;
			}
			/* we are at CA or router "from" - go one hop back to (hopefully) a switch */
			if (from->drpath.cnt > 0) {
				DEBUG("ca or router node - return back 1 hop");
				from->drpath.cnt--;
			} else {
				ca_src = 1;
				if (portnum
				    && extend_dpath(&from->drpath, portnum) < 0)
					goto badpath;
			}
			if (get_node(&nextnode, &nextport, from) < 0) {
				IBWARN("can't reach port at %s",
				       portid2str(from));
				return -1;
			}
			/* fix port num to be seen from the CA or router side */
			if (!ca_src)
				nextport.portnum =
				    from->drpath.p[from->drpath.cnt + 1];
		}
		/* only if the next node is a switch, get switch info */
		if (nextnode.type == IB_NODE_SWITCH) {
			next_sw_outport = switch_lookup(&sw, from, to->lid);
			if (next_sw_outport < 0 ||
			    next_sw_outport > nextnode.numports) {
				/* needed to print the port in badtbl */
				outport = next_sw_outport;
				goto badtbl;
			}
		}

		port = &nextport;
		if (is_port_inactive(&nextnode, port, &sw))
			goto badoutport;
		node = &nextnode;
		portnum = port->portnum;
		dump_route(dump, node, outport, port);
	}

	if (maxhops <= 0) {
		IBWARN("no route found after %d hops", MAXHOPS);
		return -1;
	}
	dump_endnode(dump, "To", node, port);
	return 0;

badport:
	IBWARN("Bad port state found: node \"%s\" port %d state %d",
	       clean_nodedesc(node->nodedesc), portnum, port->state);
	return -1;
badoutport:
	IBWARN("Bad out port state found: node \"%s\" outport %d state %d",
	       clean_nodedesc(node->nodedesc), outport, port->state);
	return -1;
badtbl:
	IBWARN
	    ("Bad forwarding table entry found at: node \"%s\" lid entry %d is %d (top %d)",
	     clean_nodedesc(node->nodedesc), to->lid, outport, sw.linearFDBtop);
	return -1;
badpath:
	IBWARN("Direct path too long!");
	return -1;
}

/**************************
 * MC span part
 */

#define HASHGUID(guid)		((uint32_t)(((uint32_t)(guid) * 101) ^ ((uint32_t)((guid) >> 32) * 103)))
#define HTSZ 137

static int insert_node(Node * new)
{
	static Node *nodestbl[HTSZ];
	int hash = HASHGUID(new->nodeguid) % HTSZ;
	Node *node;

	for (node = nodestbl[hash]; node; node = node->htnext)
		if (node->nodeguid == new->nodeguid) {
			DEBUG("node %" PRIx64 " already exists", new->nodeguid);
			return -1;
		}

	new->htnext = nodestbl[hash];
	nodestbl[hash] = new;

	return 0;
}

static int get_port(Port * port, int portnum, ib_portid_t * portid)
{
	char portinfo[64] = { 0 };
	void *pi = portinfo;

	port->portnum = portnum;

	if (!smp_query_via(pi, portid, IB_ATTR_PORT_INFO, portnum, timeout,
			   srcport))
		return -1;

	mad_decode_field(pi, IB_PORT_LID_F, &port->lid);
	mad_decode_field(pi, IB_PORT_LMC_F, &port->lmc);
	mad_decode_field(pi, IB_PORT_STATE_F, &port->state);
	mad_decode_field(pi, IB_PORT_PHYS_STATE_F, &port->physstate);

	VERBOSE("portid %s portnum %d: lid %d state %d physstate %d",
		portid2str(portid), portnum, port->lid, port->state,
		port->physstate);
	return 1;
}

static void link_port(Port * port, Node * node)
{
	port->next = node->ports;
	node->ports = port;
}

static int new_node(Node * node, Port * port, ib_portid_t * path, int dist)
{
	if (port->portguid == target_portguid) {
		node->dist = -1;	/* tag as target */
		link_port(port, node);
		dump_endnode(ibverbose, "found target", node, port);
		return 1;	/* found; */
	}

	/* BFS search start with my self */
	if (insert_node(node) < 0)
		return -1;	/* known switch */

	VERBOSE("insert dist %d node %p port %d lid %d", dist, node,
		port->portnum, port->lid);

	link_port(port, node);

	node->dist = dist;
	node->path = *path;
	node->dnext = nodesdist[dist];
	nodesdist[dist] = node;

	return 0;
}

static int switch_mclookup(Node * node, ib_portid_t * portid, int mlid,
			   char *map)
{
	Switch sw;
	char mdb[64];
	void *si = sw.switchinfo;
	uint16_t *msets = (uint16_t *) mdb;
	int maxsets, block, i, set;

	memset(map, 0, 256);

	memset(si, 0, sizeof(sw.switchinfo));
	if (!smp_query_via(si, portid, IB_ATTR_SWITCH_INFO, 0, timeout,
			   srcport))
		return -1;

	mlid -= 0xc000;

	mad_decode_field(si, IB_SW_MCAST_FDB_CAP_F, &sw.mccap);

	if (mlid >= sw.mccap)
		return -1;

	block = mlid / 32;
	maxsets = (node->numports + 15) / 16;	/* round up */

	for (set = 0; set < maxsets; set++) {
		memset(mdb, 0, sizeof(mdb));
		if (!smp_query_via(mdb, portid, IB_ATTR_MULTICASTFORWTBL,
				   block | (set << 28), timeout, srcport))
			return -1;

		for (i = 0; i < 16; i++, map++) {
			uint16_t mask = ntohs(msets[mlid % 32]);
			if (mask & (1 << i))
				*map = 1;
			else
				continue;
			VERBOSE("Switch guid 0x%" PRIx64
				": mlid 0x%x is forwarded to port %d",
				node->nodeguid, mlid + 0xc000, i + set * 16);
		}
	}

	return 0;
}

/*
 * Return 1 if found, 0 if not, -1 on errors.
 */
static Node *find_mcpath(ib_portid_t * from, int mlid)
{
	Node *node, *remotenode;
	Port *port, *remoteport;
	char map[256];
	int r, i;
	int dist = 0, leafport = 0;
	ib_portid_t *path;

	DEBUG("from %s", portid2str(from));

	if (!(node = calloc(1, sizeof(Node))))
		IBEXIT("out of memory");

	if (!(port = calloc(1, sizeof(Port))))
		IBEXIT("out of memory");

	if (get_node(node, port, from) < 0) {
		IBWARN("can't reach node %s", portid2str(from));
		return 0;
	}

	node->upnode = 0;	/* root */
	if ((r = new_node(node, port, from, 0)) > 0) {
		if (node->type != IB_NODE_SWITCH) {
			IBWARN("ibtracert from CA to CA is unsupported");
			return 0;	/* ibtracert from host to itself is unsupported */
		}

		if (switch_mclookup(node, from, mlid, map) < 0 || !map[0])
			return 0;
		return node;
	}

	for (dist = 0; dist < MAXHOPS; dist++) {

		for (node = nodesdist[dist]; node; node = node->dnext) {

			path = &node->path;

			VERBOSE("dist %d node %p", dist, node);
			dump_endnode(ibverbose, "processing", node,
				     node->ports);

			memset(map, 0, sizeof(map));

			if (node->type != IB_NODE_SWITCH) {
				if (dist)
					continue;
				leafport = path->drpath.p[path->drpath.cnt];
				map[port->portnum] = 1;
				node->upport = 0;	/* starting here */
				DEBUG("Starting from CA 0x%" PRIx64
				      " lid %d port %d (leafport %d)",
				      node->nodeguid, port->lid, port->portnum,
				      leafport);
			} else {	/* switch */

				/* if starting from a leaf port fix up port (up port) */
				if (dist == 1 && leafport)
					node->upport = leafport;

				if (switch_mclookup(node, path, mlid, map) < 0) {
					IBWARN("skipping bad Switch 0x%" PRIx64
					       "", node->nodeguid);
					continue;
				}
			}

			for (i = 1; i <= node->numports; i++) {
				if (!map[i] || i == node->upport)
					continue;

				if (dist == 0 && leafport) {
					if (from->drpath.cnt > 0)
						path->drpath.cnt--;
				} else {
					if (!(port = calloc(1, sizeof(Port))))
						IBEXIT("out of memory");

					if (get_port(port, i, path) < 0) {
						IBWARN
						    ("can't reach node %s port %d",
						     portid2str(path), i);
						free(port);
						return 0;
					}

					if (port->physstate != 5) {	/* LinkUP */
						free(port);
						continue;
					}
#if 0
					link_port(port, node);
#endif

					if (extend_dpath(&path->drpath, i) < 0) {
						free(port);
						return 0;
					}
				}

				if (!(remotenode = calloc(1, sizeof(Node))))
					IBEXIT("out of memory");

				if (!(remoteport = calloc(1, sizeof(Port))))
					IBEXIT("out of memory");

				if (get_node(remotenode, remoteport, path) < 0) {
					IBWARN
					    ("NodeInfo on %s port %d failed, skipping port",
					     portid2str(path), i);
					path->drpath.cnt--;	/* restore path */
					free(remotenode);
					free(remoteport);
					continue;
				}

				remotenode->upnode = node;
				remotenode->upport = remoteport->portnum;
				remoteport->remoteport = port;

				if ((r = new_node(remotenode, remoteport, path,
						  dist + 1)) > 0)
					return remotenode;

				if (r == 0)
					dump_endnode(ibverbose, "new remote",
						     remotenode, remoteport);
				else if (remotenode->type == IB_NODE_SWITCH)
					dump_endnode(2,
						     "ERR: circle discovered at",
						     remotenode, remoteport);

				path->drpath.cnt--;	/* restore path */
			}
		}
	}

	return 0;		/* not found */
}

static uint64_t find_target_portguid(ib_portid_t * to)
{
	Node tonode;
	Port toport;

	if (get_node(&tonode, &toport, to) < 0) {
		IBWARN("can't find to port\n");
		return -1;
	}

	return toport.portguid;
}

static void dump_mcpath(Node * node, int dumplevel)
{
	char *nodename = NULL;

	if (node->upnode)
		dump_mcpath(node->upnode, dumplevel);

	nodename =
	    remap_node_name(node_name_map, node->nodeguid, node->nodedesc);

	if (!node->dist) {
		printf("From %s 0x%" PRIx64 " port %d lid %u-%u \"%s\"\n",
		       (node->type <=
			IB_NODE_MAX ? node_type_str[node->type] : "???"),
		       node->nodeguid, node->ports->portnum, node->ports->lid,
		       node->ports->lid + (1 << node->ports->lmc) - 1,
		       nodename);
		goto free_name;
	}

	if (node->dist) {
		if (dumplevel == 1)
			printf("[%d] -> %s {0x%016" PRIx64 "}[%d]\n",
			       node->ports->remoteport->portnum,
			       (node->type <=
				IB_NODE_MAX ? node_type_str[node->type] :
				"???"), node->nodeguid, node->upport);
		else
			printf("[%d] -> %s 0x%" PRIx64 "[%d] lid %u \"%s\"\n",
			       node->ports->remoteport->portnum,
			       (node->type <=
				IB_NODE_MAX ? node_type_str[node->type] :
				"???"), node->nodeguid, node->upport,
			       node->ports->lid, nodename);
	}

	if (node->dist < 0)
		/* target node */
		printf("To %s 0x%" PRIx64 " port %d lid %u-%u \"%s\"\n",
		       (node->type <=
			IB_NODE_MAX ? node_type_str[node->type] : "???"),
		       node->nodeguid, node->ports->portnum, node->ports->lid,
		       node->ports->lid + (1 << node->ports->lmc) - 1,
		       nodename);

free_name:
	free(nodename);
}

static int resolve_lid(ib_portid_t * portid, const void *srcport)
{
	uint8_t portinfo[64] = { 0 };
	uint16_t lid;

	if (!smp_query_via(portinfo, portid, IB_ATTR_PORT_INFO, 0, 0, srcport))
		return -1;
	mad_decode_field(portinfo, IB_PORT_LID_F, &lid);

	ib_portid_set(portid, lid, 0, 0);

	return 0;
}

static int dumplevel = 2, multicast, mlid;

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 1:
		node_name_map_file = strdup(optarg);
		break;
	case 'm':
		multicast++;
		mlid = strtoul(optarg, 0, 0);
		break;
	case 'f':
		force++;
		break;
	case 'n':
		dumplevel = 1;
		break;
	default:
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int mgmt_classes[3] =
	    { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS };
	ib_portid_t my_portid = { 0 };
	ib_portid_t src_portid = { 0 };
	ib_portid_t dest_portid = { 0 };
	Node *endnode;

	const struct ibdiag_opt opts[] = {
		{"force", 'f', 0, NULL, "force"},
		{"no_info", 'n', 0, NULL, "simple format"},
		{"mlid", 'm', 1, "<mlid>", "multicast trace of the mlid"},
		{"node-name-map", 1, 1, "<file>", "node name map file"},
		{0}
	};
	char usage_args[] = "<src-addr> <dest-addr>";
	const char *usage_examples[] = {
		"- Unicast examples:",
		"4 16\t\t\t# show path between lids 4 and 16",
		"-n 4 16\t\t# same, but using simple output format",
		"-G 0x8f1040396522d 0x002c9000100d051\t# use guid addresses",

		" - Multicast examples:",
		"-m 0xc000 4 16\t# show multicast path of mlid 0xc000 between lids 4 and 16",
		NULL,
	};

	ibdiag_process_opts(argc, argv, NULL, "DK", opts, process_opt,
			    usage_args, usage_examples);

	f = stdout;
	argc -= optind;
	argv += optind;

	if (argc < 2)
		ibdiag_show_usage();

	if (ibd_timeout)
		timeout = ibd_timeout;

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	smp_mkey_set(srcport, ibd_mkey);

	node_name_map = open_node_name_map(node_name_map_file);

	if (resolve_portid_str(ibd_ca, ibd_ca_port, &src_portid, argv[0],
			       ibd_dest_type, ibd_sm_id, srcport) < 0)
		IBEXIT("can't resolve source port %s", argv[0]);

	if (resolve_portid_str(ibd_ca, ibd_ca_port, &dest_portid, argv[1],
			       ibd_dest_type, ibd_sm_id, srcport) < 0)
		IBEXIT("can't resolve destination port %s", argv[1]);

	if (ibd_dest_type == IB_DEST_DRPATH) {
		if (resolve_lid(&src_portid, NULL) < 0)
			IBEXIT("cannot resolve lid for port \'%s\'",
				portid2str(&src_portid));
		if (resolve_lid(&dest_portid, NULL) < 0)
			IBEXIT("cannot resolve lid for port \'%s\'",
				portid2str(&dest_portid));
	}

	if (dest_portid.lid == 0 || src_portid.lid == 0) {
		IBWARN("bad src/dest lid");
		ibdiag_show_usage();
	}

	if (ibd_dest_type != IB_DEST_DRPATH) {
		/* first find a direct path to the src port */
		if (find_route(&my_portid, &src_portid, 0) < 0)
			IBEXIT("can't find a route to the src port");

		src_portid = my_portid;
	}

	if (!multicast) {
		if (find_route(&src_portid, &dest_portid, dumplevel) < 0)
			IBEXIT("can't find a route from src to dest");
		exit(0);
	} else {
		if (mlid < 0xc000)
			IBWARN("invalid MLID; must be 0xc000 or larger");
	}

	if (!(target_portguid = find_target_portguid(&dest_portid)))
		IBEXIT("can't reach target lid %d", dest_portid.lid);

	if (!(endnode = find_mcpath(&src_portid, mlid)))
		IBEXIT("can't find a multicast route from src to dest");

	/* dump multicast path */
	dump_mcpath(endnode, dumplevel);

	close_node_name_map(node_name_map);

	mad_rpc_close_port(srcport);

	exit(0);
}
