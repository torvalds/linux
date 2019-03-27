/*
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
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

/*
 * Abstract:
 *    Various OpenSM dumpers
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_DUMP_C
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>

static void dump_ucast_path_distribution(cl_map_item_t * item, FILE * file,
					 void *cxt)
{
	osm_node_t *p_node;
	osm_node_t *p_remote_node;
	uint8_t i;
	uint8_t num_ports;
	uint32_t num_paths;
	ib_net64_t remote_guid_ho;
	osm_switch_t *p_sw = (osm_switch_t *) item;

	p_node = p_sw->p_node;
	num_ports = p_sw->num_ports;

	fprintf(file, "dump_ucast_path_distribution: Switch 0x%" PRIx64 "\n"
		"Port : Path Count Through Port",
		cl_ntoh64(osm_node_get_node_guid(p_node)));

	for (i = 0; i < num_ports; i++) {
		num_paths = osm_switch_path_count_get(p_sw, i);
		fprintf(file, "\n %03u : %u", i, num_paths);
		if (i == 0) {
			fprintf(file, " (switch management port)");
			continue;
		}

		p_remote_node = osm_node_get_remote_node(p_node, i, NULL);
		if (p_remote_node == NULL)
			continue;

		remote_guid_ho =
		    cl_ntoh64(osm_node_get_node_guid(p_remote_node));

		switch (osm_node_get_type(p_remote_node)) {
		case IB_NODE_TYPE_SWITCH:
			fprintf(file, " (link to switch");
			break;
		case IB_NODE_TYPE_ROUTER:
			fprintf(file, " (link to router");
			break;
		case IB_NODE_TYPE_CA:
			fprintf(file, " (link to CA");
			break;
		default:
			fprintf(file, " (link to unknown node type");
			break;
		}

		fprintf(file, " 0x%" PRIx64 ")", remote_guid_ho);
	}

	fprintf(file, "\n");
}

static void dump_ucast_routes(cl_map_item_t * item, FILE * file, void *cxt)
{
	const osm_node_t *p_node;
	osm_port_t *p_port;
	uint8_t port_num;
	uint8_t num_hops;
	uint8_t best_hops;
	uint8_t best_port;
	uint16_t max_lid_ho;
	uint16_t lid_ho, base_lid;
	boolean_t direct_route_exists = FALSE;
	boolean_t dor;
	osm_switch_t *p_sw = (osm_switch_t *) item;
	osm_opensm_t *p_osm = cxt;

	p_node = p_sw->p_node;

	max_lid_ho = p_sw->max_lid_ho;

	fprintf(file, "dump_ucast_routes: "
		"Switch 0x%016" PRIx64 "\nLID    : Port : Hops : Optimal\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)));

	dor = (p_osm->routing_engine_used &&
	       p_osm->routing_engine_used->type == OSM_ROUTING_ENGINE_TYPE_DOR);

	for (lid_ho = 1; lid_ho <= max_lid_ho; lid_ho++) {
		fprintf(file, "0x%04X : ", lid_ho);

		p_port = osm_get_port_by_lid_ho(&p_osm->subn, lid_ho);
		if (!p_port) {
			fprintf(file, "UNREACHABLE\n");
			continue;
		}

		port_num = osm_switch_get_port_by_lid(p_sw, lid_ho,
						      OSM_NEW_LFT);
		if (port_num == OSM_NO_PATH) {
			/*
			   This may occur if there are 'holes' in the existing
			   LID assignments.  Running SM with --reassign_lids
			   will reassign and compress the LID range.  The
			   subnet should work fine either way.
			 */
			fprintf(file, "UNREACHABLE\n");
			continue;
		}
		/*
		   Switches can lie about which port routes a given
		   lid due to a recent reconfiguration of the subnet.
		   Therefore, ensure that the hop count is better than
		   OSM_NO_PATH.
		 */
		if (p_port->p_node->sw) {
			/* Target LID is switch.
			   Get its base lid and check hop count for this base LID only. */
			base_lid = osm_node_get_base_lid(p_port->p_node, 0);
			base_lid = cl_ntoh16(base_lid);
			num_hops =
			    osm_switch_get_hop_count(p_sw, base_lid, port_num);
		} else {
			/* Target LID is not switch (CA or router).
			   Check if we have route to this target from current switch. */
			num_hops =
			    osm_switch_get_hop_count(p_sw, lid_ho, port_num);
			if (num_hops != OSM_NO_PATH) {
				direct_route_exists = TRUE;
				base_lid = lid_ho;
			} else {
				osm_physp_t *p_physp = p_port->p_physp;

				if (!p_physp || !p_physp->p_remote_physp ||
				    !p_physp->p_remote_physp->p_node->sw)
					num_hops = OSM_NO_PATH;
				else {
					base_lid =
					    osm_node_get_base_lid(p_physp->
								  p_remote_physp->
								  p_node, 0);
					base_lid = cl_ntoh16(base_lid);
					num_hops =
					    p_physp->p_remote_physp->p_node->
					    sw ==
					    p_sw ? 0 :
					    osm_switch_get_hop_count(p_sw,
								     base_lid,
								     port_num);
				}
			}
		}

		if (num_hops == OSM_NO_PATH) {
			fprintf(file, "%03u  : HOPS UNKNOWN\n", port_num);
			continue;
		}

		best_hops = osm_switch_get_least_hops(p_sw, base_lid);
		if (!p_port->p_node->sw && !direct_route_exists) {
			best_hops++;
			num_hops++;
		}

		fprintf(file, "%03u  : %02u   : ", port_num, num_hops);

		if (best_hops == num_hops)
			fprintf(file, "yes");
		else {
			/* No LMC Optimization */
			best_port = osm_switch_recommend_path(p_sw, p_port,
							      lid_ho, 1, TRUE,
							      FALSE, dor,
							      p_osm->subn.opt.port_shifting,
							      p_osm->subn.opt.scatter_ports,
							      OSM_NEW_LFT);
			fprintf(file, "No %u hop path possible via port %u!",
				best_hops, best_port);
		}

		fprintf(file, "\n");
	}
}

static void dump_mcast_routes(cl_map_item_t * item, FILE * file, void *cxt)
{
	osm_switch_t *p_sw = (osm_switch_t *) item;
	osm_mcast_tbl_t *p_tbl;
	int16_t mlid_ho = 0;
	int16_t mlid_start_ho;
	uint8_t position = 0;
	int16_t block_num = 0;
	boolean_t first_mlid;
	boolean_t first_port;
	const osm_node_t *p_node;
	uint16_t i, j;
	uint16_t mask_entry;
	char sw_hdr[256];
	char mlid_hdr[32];

	p_node = p_sw->p_node;

	p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);

	sprintf(sw_hdr, "\nSwitch 0x%016" PRIx64 "\nLID    : Out Port(s)\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)));
	first_mlid = TRUE;
	while (block_num <= p_tbl->max_block_in_use) {
		mlid_start_ho = (uint16_t) (block_num * IB_MCAST_BLOCK_SIZE);
		for (i = 0; i < IB_MCAST_BLOCK_SIZE; i++) {
			mlid_ho = mlid_start_ho + i;
			position = 0;
			first_port = TRUE;
			sprintf(mlid_hdr, "0x%04X :",
				mlid_ho + IB_LID_MCAST_START_HO);
			while (position <= p_tbl->max_position) {
				mask_entry =
				    cl_ntoh16((*p_tbl->
					       p_mask_tbl)[mlid_ho][position]);
				if (mask_entry == 0) {
					position++;
					continue;
				}
				for (j = 0; j < 16; j++) {
					if ((1 << j) & mask_entry) {
						if (first_mlid) {
							fprintf(file, "%s",
								sw_hdr);
							first_mlid = FALSE;
						}
						if (first_port) {
							fprintf(file, "%s",
								mlid_hdr);
							first_port = FALSE;
						}
						fprintf(file, " 0x%03X ",
							j + (position * 16));
					}
				}
				position++;
			}
			if (first_port == FALSE)
				fprintf(file, "\n");
		}
		block_num++;
	}
}

static void dump_lid_matrix(cl_map_item_t * item, FILE * file, void *cxt)
{
	osm_switch_t *p_sw = (osm_switch_t *) item;
	osm_opensm_t *p_osm = cxt;
	osm_node_t *p_node = p_sw->p_node;
	unsigned max_lid = p_sw->max_lid_ho;
	unsigned max_port = p_sw->num_ports;
	uint16_t lid;
	uint8_t port;

	fprintf(file, "Switch: guid 0x%016" PRIx64 "\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)));
	for (lid = 1; lid <= max_lid; lid++) {
		osm_port_t *p_port;
		if (osm_switch_get_least_hops(p_sw, lid) == OSM_NO_PATH)
			continue;
		fprintf(file, "0x%04x:", lid);
		for (port = 0; port < max_port; port++)
			fprintf(file, " %02x",
				osm_switch_get_hop_count(p_sw, lid, port));
		p_port = osm_get_port_by_lid_ho(&p_osm->subn, lid);
		if (p_port)
			fprintf(file, " # portguid 0x%016" PRIx64,
				cl_ntoh64(osm_port_get_guid(p_port)));
		fprintf(file, "\n");
	}
}

static void dump_ucast_lfts(cl_map_item_t * item, FILE * file, void *cxt)
{
	osm_switch_t *p_sw = (osm_switch_t *) item;
	osm_opensm_t *p_osm = cxt;
	osm_node_t *p_node = p_sw->p_node;
	unsigned max_lid = p_sw->max_lid_ho;
	unsigned max_port = p_sw->num_ports;
	uint16_t lid;
	uint8_t port;

	fprintf(file, "Unicast lids [0-%u] of switch Lid %u guid 0x%016"
		PRIx64 " (\'%s\'):\n",
		max_lid, cl_ntoh16(osm_node_get_base_lid(p_node, 0)),
		cl_ntoh64(osm_node_get_node_guid(p_node)), p_node->print_desc);
	for (lid = 0; lid <= max_lid; lid++) {
		osm_port_t *p_port;
		port = osm_switch_get_port_by_lid(p_sw, lid, OSM_NEW_LFT);

		if (port >= max_port)
			continue;

		fprintf(file, "0x%04x %03u # ", lid, port);

		p_port = osm_get_port_by_lid_ho(&p_osm->subn, lid);
		if (p_port) {
			p_node = p_port->p_node;
			fprintf(file, "%s portguid 0x%016" PRIx64 ": \'%s\'",
				ib_get_node_type_str(osm_node_get_type(p_node)),
				cl_ntoh64(osm_port_get_guid(p_port)),
				p_node->print_desc);
		} else
			fprintf(file, "unknown node and type");
		fprintf(file, "\n");
	}
	fprintf(file, "%u lids dumped\n", max_lid);
}

static void dump_topology_node(cl_map_item_t * item, FILE * file, void *cxt)
{
	osm_node_t *p_node = (osm_node_t *) item;
	uint32_t cPort;
	osm_node_t *p_nbnode;
	osm_physp_t *p_physp, *p_default_physp, *p_rphysp;
	uint8_t link_speed_act;
	const char *link_speed_act_str, *link_width_act_str;

	if (!p_node->node_info.num_ports)
		return;

	for (cPort = 1; cPort < osm_node_get_num_physp(p_node); cPort++) {
		uint8_t port_state;

		p_physp = osm_node_get_physp_ptr(p_node, cPort);
		if (!p_physp)
			continue;

		p_rphysp = p_physp->p_remote_physp;
		if (!p_rphysp)
			continue;

		CL_ASSERT(cPort == p_physp->port_num);

		if (p_node->node_info.node_type == IB_NODE_TYPE_SWITCH)
			p_default_physp = osm_node_get_physp_ptr(p_node, 0);
		else
			p_default_physp = p_physp;

		fprintf(file, "{ %s%s Ports:%02X SystemGUID:%016" PRIx64
			" NodeGUID:%016" PRIx64 " PortGUID:%016" PRIx64
			" VenID:%06X DevID:%04X Rev:%08X {%s} LID:%04X PN:%02X } ",
			p_node->node_info.node_type == IB_NODE_TYPE_SWITCH ?
			"SW" : p_node->node_info.node_type ==
			IB_NODE_TYPE_CA ? "CA" : p_node->node_info.node_type ==
			IB_NODE_TYPE_ROUTER ? "Rt" : "**",
			p_default_physp->port_info.base_lid ==
			p_default_physp->port_info.
			master_sm_base_lid ? "-SM" : "",
			p_node->node_info.num_ports,
			cl_ntoh64(p_node->node_info.sys_guid),
			cl_ntoh64(p_node->node_info.node_guid),
			cl_ntoh64(p_physp->port_guid),
			cl_ntoh32(ib_node_info_get_vendor_id
				  (&p_node->node_info)),
			cl_ntoh16(p_node->node_info.device_id),
			cl_ntoh32(p_node->node_info.revision),
			p_node->print_desc,
			cl_ntoh16(p_default_physp->port_info.base_lid), cPort);

		p_nbnode = p_rphysp->p_node;

		if (p_nbnode->node_info.node_type == IB_NODE_TYPE_SWITCH)
			p_default_physp = osm_node_get_physp_ptr(p_nbnode, 0);
		else
			p_default_physp = p_rphysp;

		fprintf(file, "{ %s%s Ports:%02X SystemGUID:%016" PRIx64
			" NodeGUID:%016" PRIx64 " PortGUID:%016" PRIx64
			" VenID:%08X DevID:%04X Rev:%08X {%s} LID:%04X PN:%02X } ",
			p_nbnode->node_info.node_type == IB_NODE_TYPE_SWITCH ?
			"SW" : p_nbnode->node_info.node_type ==
			IB_NODE_TYPE_CA ? "CA" :
			p_nbnode->node_info.node_type == IB_NODE_TYPE_ROUTER ?
			"Rt" : "**",
			p_default_physp->port_info.base_lid ==
			p_default_physp->port_info.
			master_sm_base_lid ? "-SM" : "",
			p_nbnode->node_info.num_ports,
			cl_ntoh64(p_nbnode->node_info.sys_guid),
			cl_ntoh64(p_nbnode->node_info.node_guid),
			cl_ntoh64(p_rphysp->port_guid),
			cl_ntoh32(ib_node_info_get_vendor_id
				  (&p_nbnode->node_info)),
			cl_ntoh32(p_nbnode->node_info.device_id),
			cl_ntoh32(p_nbnode->node_info.revision),
			p_nbnode->print_desc,
			cl_ntoh16(p_default_physp->port_info.base_lid),
			p_rphysp->port_num);

		port_state = ib_port_info_get_port_state(&p_physp->port_info);
		link_speed_act =
		    ib_port_info_get_link_speed_active(&p_physp->port_info);
		if (link_speed_act == IB_LINK_SPEED_ACTIVE_2_5)
			link_speed_act_str = "2.5";
		else if (link_speed_act == IB_LINK_SPEED_ACTIVE_5)
			link_speed_act_str = "5";
		else if (link_speed_act == IB_LINK_SPEED_ACTIVE_10)
			link_speed_act_str = "10";
		else
			link_speed_act_str = "??";

		if (p_physp->ext_port_info.link_speed_active & FDR10)
			link_speed_act_str = "FDR10";

		if (p_default_physp->port_info.capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS) {
			link_speed_act =
			    ib_port_info_get_link_speed_ext_active(&p_physp->port_info);
			if (link_speed_act == IB_LINK_SPEED_EXT_ACTIVE_14)
				link_speed_act_str = "14";
			else if (link_speed_act == IB_LINK_SPEED_EXT_ACTIVE_25)
				link_speed_act_str = "25";
			else if (link_speed_act != IB_LINK_SPEED_EXT_ACTIVE_NONE)
				link_speed_act_str = "??";
		}

		if (p_physp->port_info.link_width_active == 1)
			link_width_act_str = "1x";
		else if (p_physp->port_info.link_width_active == 2)
			link_width_act_str = "4x";
		else if (p_physp->port_info.link_width_active == 4)
			link_width_act_str = "8x";
		else if (p_physp->port_info.link_width_active == 8)
			link_width_act_str = "12x";
		else link_width_act_str = "??";

		if (p_default_physp->port_info.capability_mask2 &
		    IB_PORT_CAP2_IS_LINK_WIDTH_2X_SUPPORTED) {
			if (p_physp->port_info.link_width_active == 16)
				link_width_act_str = "2x";
		}

		fprintf(file, "PHY=%s LOG=%s SPD=%s\n",
			link_width_act_str,
			port_state == IB_LINK_ACTIVE ? "ACT" :
			port_state == IB_LINK_ARMED ? "ARM" :
			port_state == IB_LINK_INIT ? "INI" : "DWN",
			link_speed_act_str);
	}
}

static void dump_sl2vl_tbl(cl_map_item_t * item, FILE * file, void *cxt)
{
	osm_port_t *p_port = (osm_port_t *) item;
	osm_node_t *p_node = p_port->p_node;
	uint32_t in_port, out_port,
		 num_ports = p_node->node_info.num_ports;
	ib_net16_t base_lid = osm_port_get_base_lid(p_port);
	osm_physp_t *p_physp;
	ib_slvl_table_t *p_tbl;
	int i, n;
	char buf[1024];
	const char * header_line =	"#in out : 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15";
	const char * separator_line = "#--------------------------------------------------------";

	if (!num_ports)
		return;

	fprintf(file, "%s 0x%016" PRIx64 ", base LID %d, "
		"\"%s\"\n%s\n%s\n",
		ib_get_node_type_str(p_node->node_info.node_type),
		cl_ntoh64(p_port->guid), cl_ntoh16(base_lid),
		p_node->print_desc, header_line, separator_line);

	if (p_node->node_info.node_type == IB_NODE_TYPE_SWITCH) {
		for (out_port = 0; out_port <= num_ports; out_port++){
			p_physp = osm_node_get_physp_ptr(p_node, out_port);

			/* no need to print SL2VL table for port that is down */
			if (!p_physp || !p_physp->p_remote_physp)
				continue;

			for (in_port = 0; in_port <= num_ports; in_port++) {
				p_tbl = osm_physp_get_slvl_tbl(p_physp, in_port);
				for (i = 0, n = 0; i < 16; i++)
					n += sprintf(buf + n, " %-2d",
						ib_slvl_table_get(p_tbl, i));
				fprintf(file, "%-3d %-3d :%s\n",
					in_port, out_port, buf);
			}
		}
	} else {
		p_physp = p_port->p_physp;
		p_tbl = osm_physp_get_slvl_tbl(p_physp, 0);
		for (i = 0, n = 0; i < 16; i++)
			n += sprintf(buf + n, " %-2d",
					ib_slvl_table_get(p_tbl, i));
		fprintf(file, "%-3d %-3d :%s\n", 0, 0, buf);
	}

	fprintf(file, "%s\n\n", separator_line);
}

static void print_node_report(cl_map_item_t * item, FILE * file, void *cxt)
{
	osm_node_t *p_node = (osm_node_t *) item;
	osm_opensm_t *osm = cxt;
	const osm_physp_t *p_physp, *p_remote_physp;
	const ib_port_info_t *p_pi;
	uint8_t port_num;
	uint32_t num_ports;
	uint8_t node_type;

	node_type = osm_node_get_type(p_node);

	num_ports = osm_node_get_num_physp(p_node);
	port_num = node_type == IB_NODE_TYPE_SWITCH ? 0 : 1;
	for (; port_num < num_ports; port_num++) {
		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		if (!p_physp)
			continue;

		fprintf(file, "%-11s : %s : %02X :",
			osm_get_manufacturer_str(cl_ntoh64
						 (osm_node_get_node_guid
						  (p_node))),
			osm_get_node_type_str_fixed_width(node_type), port_num);

		p_pi = &p_physp->port_info;

		/*
		 * Port state is not defined for base switch port 0
		 */
		if (port_num == 0 &&
		    ib_switch_info_is_enhanced_port0(&p_node->sw->switch_info) == FALSE)
			fprintf(file, "     :");
		else
			fprintf(file, " %s :",
				osm_get_port_state_str_fixed_width
				(ib_port_info_get_port_state(p_pi)));

		/*
		 * LID values are only meaningful in select cases.
		 */
		if (ib_port_info_get_port_state(p_pi) != IB_LINK_DOWN
		    && ((node_type == IB_NODE_TYPE_SWITCH && port_num == 0)
			|| node_type != IB_NODE_TYPE_SWITCH))
			fprintf(file, " %04X :  %01X  :",
				cl_ntoh16(p_pi->base_lid),
				ib_port_info_get_lmc(p_pi));
		else
			fprintf(file, "      :     :");

		if (port_num == 0 &&
		    ib_switch_info_is_enhanced_port0(&p_node->sw->switch_info) == FALSE)
			fprintf(file, "      :     :      ");
		else
			fprintf(file, " %s : %s : %s ",
				osm_get_mtu_str
				(ib_port_info_get_neighbor_mtu(p_pi)),
				osm_get_lwa_str(p_pi->link_width_active),
				osm_get_lsa_str
				(ib_port_info_get_link_speed_active(p_pi),
				 ib_port_info_get_link_speed_ext_active(p_pi),
				 ib_port_info_get_port_state(p_pi),
				 p_physp->ext_port_info.link_speed_active & FDR10));

		if (osm_physp_get_port_guid(p_physp) == osm->subn.sm_port_guid)
			fprintf(file, "* %016" PRIx64 " *",
				cl_ntoh64(osm_physp_get_port_guid(p_physp)));
		else
			fprintf(file, ": %016" PRIx64 " :",
				cl_ntoh64(osm_physp_get_port_guid(p_physp)));

		if (port_num
		    && (ib_port_info_get_port_state(p_pi) != IB_LINK_DOWN)) {
			p_remote_physp = osm_physp_get_remote(p_physp);
			if (p_remote_physp)
				fprintf(file, " %016" PRIx64 " (%02X)",
					cl_ntoh64(osm_physp_get_port_guid
						  (p_remote_physp)),
					osm_physp_get_port_num(p_remote_physp));
			else
				fprintf(file, " UNKNOWN");
		}

		fprintf(file, "\n");
	}

	fprintf(file, "------------------------------------------------------"
		"------------------------------------------------\n");
}

struct dump_context {
	osm_opensm_t *p_osm;
	FILE *file;
	void (*func) (cl_map_item_t *, FILE *, void *);
	void *cxt;
};

static void dump_item(cl_map_item_t * item, void *cxt)
{
	((struct dump_context *)cxt)->func(item,
					   ((struct dump_context *)cxt)->file,
					   ((struct dump_context *)cxt)->cxt);
}

static void dump_qmap(FILE * file, cl_qmap_t * map,
		      void (*func) (cl_map_item_t *, FILE *, void *), void *cxt)
{
	struct dump_context dump_context;

	dump_context.file = file;
	dump_context.func = func;
	dump_context.cxt = cxt;

	cl_qmap_apply_func(map, dump_item, &dump_context);
}

void osm_dump_qmap_to_file(osm_opensm_t * p_osm, const char *file_name,
			   cl_qmap_t * map,
			   void (*func) (cl_map_item_t *, FILE *, void *),
			   void *cxt)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s",
		 p_osm->subn.opt.dump_files_dir, file_name);

	file = fopen(path, "w");
	if (!file) {
		OSM_LOG(&p_osm->log, OSM_LOG_ERROR,
			"cannot create file \'%s\': %s\n",
			path, strerror(errno));
		return;
	}

	dump_qmap(file, map, func, cxt);

	fclose(file);
}


static void print_report(osm_opensm_t * osm, FILE * file)
{
	fprintf(file, "\n==================================================="
		"====================================================\n"
		"Vendor      : Ty : #  : Sta : LID  : LMC : MTU  : LWA :"
		" LSA  : Port GUID        : Neighbor Port (Port #)\n");
	dump_qmap(stdout, &osm->subn.node_guid_tbl, print_node_report, osm);
}

void osm_dump_mcast_routes(osm_opensm_t * osm)
{
	if (OSM_LOG_IS_ACTIVE_V2(&osm->log, OSM_LOG_ROUTING))
		/* multicast routes */
		osm_dump_qmap_to_file(osm, "opensm.mcfdbs",
				      &osm->subn.sw_guid_tbl,
				      dump_mcast_routes, osm);
}

void osm_dump_all(osm_opensm_t * osm)
{
	if (OSM_LOG_IS_ACTIVE_V2(&osm->log, OSM_LOG_ROUTING)) {
		/* unicast routes */
		osm_dump_qmap_to_file(osm, "opensm-lid-matrix.dump",
				      &osm->subn.sw_guid_tbl, dump_lid_matrix,
				      osm);
		osm_dump_qmap_to_file(osm, "opensm-lfts.dump",
				      &osm->subn.sw_guid_tbl, dump_ucast_lfts,
				      osm);
		if (OSM_LOG_IS_ACTIVE_V2(&osm->log, OSM_LOG_DEBUG))
			dump_qmap(stdout, &osm->subn.sw_guid_tbl,
				  dump_ucast_path_distribution, osm);

		/* An attempt to get osm_switch_recommend_path to report the
		   same routes that a sweep would assign. */
		if (osm->subn.opt.scatter_ports)
			srandom(osm->subn.opt.scatter_ports);

		osm_dump_qmap_to_file(osm, "opensm.fdbs",
				      &osm->subn.sw_guid_tbl,
				      dump_ucast_routes, osm);
		/* multicast routes */
		osm_dump_qmap_to_file(osm, "opensm.mcfdbs",
				      &osm->subn.sw_guid_tbl,
				      dump_mcast_routes, osm);
		/* SL2VL tables */
		if (osm->subn.opt.qos ||
		    (osm->routing_engine_used &&
		     osm->routing_engine_used->update_sl2vl))
			osm_dump_qmap_to_file(osm, "opensm-sl2vl.dump",
					      &osm->subn.port_guid_tbl,
					      dump_sl2vl_tbl, osm);
	}
	osm_dump_qmap_to_file(osm, "opensm-subnet.lst",
			      &osm->subn.node_guid_tbl, dump_topology_node,
			      osm);
	if (OSM_LOG_IS_ACTIVE_V2(&osm->log, OSM_LOG_VERBOSE))
		print_report(osm, stdout);
}
