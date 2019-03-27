/*
 * Copyright (c) 2006-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2010-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 *    Implementation of OpenSM QoS infrastructure primitives
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_QOS_C
#include <opensm/osm_opensm.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_qos_policy.h>

struct qos_config {
	uint8_t max_vls;
	uint8_t vl_high_limit;
	ib_vl_arb_table_t vlarb_high[2];
	ib_vl_arb_table_t vlarb_low[2];
	ib_slvl_table_t sl2vl;
};

typedef struct qos_mad_item {
	cl_list_item_t list_item;
	osm_madw_t *p_madw;
} qos_mad_item_t;

typedef struct qos_mad_list {
	cl_list_item_t list_item;
	cl_qlist_t port_mad_list;
} qos_mad_list_t;

static void qos_build_config(struct qos_config *cfg,
			     osm_qos_options_t * opt,
			     osm_qos_options_t * dflt);

/*
 * QoS primitives
 */

static qos_mad_item_t *osm_qos_mad_create(IN osm_sm_t * sm,
					  IN osm_physp_t * p,
					  IN uint32_t data_size,
					  IN uint8_t * p_data,
					  IN ib_net16_t attr_id,
					  IN uint32_t attr_mod)

{
	qos_mad_item_t *p_mad;
	osm_madw_context_t context;
	osm_madw_t *p_madw;
	osm_node_t *p_node;
	osm_physp_t *physp0;
	ib_net64_t m_key;

	p_node = osm_physp_get_node_ptr(p);
	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH &&
	    osm_physp_get_port_num(p) != 0) {
		physp0 = osm_node_get_physp_ptr(p_node, 0);
		m_key = ib_port_info_get_m_key(&physp0->port_info);
	} else
		m_key = ib_port_info_get_m_key(&p->port_info);

	switch (attr_id){
	case IB_MAD_ATTR_SLVL_TABLE:
		context.slvl_context.node_guid = osm_node_get_node_guid(p_node);
		context.slvl_context.port_guid = osm_physp_get_port_guid(p);
		context.slvl_context.set_method = TRUE;
		break;
	case IB_MAD_ATTR_VL_ARBITRATION:
		context.vla_context.node_guid = osm_node_get_node_guid(p_node);
		context.vla_context.port_guid = osm_physp_get_port_guid(p);
		context.vla_context.set_method = TRUE;
		break;
	default:
		return NULL;
	}

	p_mad = (qos_mad_item_t *) malloc(sizeof(*p_mad));
	if (!p_mad)
		return NULL;

	memset(p_mad, 0, sizeof(*p_mad));

	p_madw = osm_prepare_req_set(sm, osm_physp_get_dr_path_ptr(p),
				     p_data, data_size,
				     attr_id, cl_hton32(attr_mod),
				     FALSE, m_key,
				     CL_DISP_MSGID_NONE, &context);

	if (p_madw == NULL) {
		free(p_mad);
		return NULL;
	}
	p_mad->p_madw = p_madw;
	return p_mad;
}

static void osm_qos_mad_delete(qos_mad_item_t ** p_item)
{
	free(*p_item);
	*p_item = NULL;
}

static ib_api_status_t vlarb_update_table_block(osm_sm_t * sm,
						osm_physp_t * p,
						uint8_t port_num,
						unsigned force_update,
						const ib_vl_arb_table_t *
						table_block,
						unsigned block_length,
						unsigned block_num,
						cl_qlist_t *mad_list)
{
	struct osm_routing_engine *re = sm->p_subn->p_osm->routing_engine_used;
	ib_vl_arb_table_t block;
	uint32_t attr_mod;
	unsigned vl_mask, i;
	qos_mad_item_t *p_mad;
	vl_mask = (1 << (ib_port_info_get_op_vls(&p->port_info) - 1)) - 1;

	memset(&block, 0, sizeof(block));
	memcpy(&block, table_block, block_length * sizeof(block.vl_entry[0]));

	if (re && re->update_vlarb)
		re->update_vlarb(re->context, p, port_num, &block,
				 block_length, block_num);

	for (i = 0; i < block_length; i++)
		block.vl_entry[i].vl &= vl_mask;

	if (!force_update &&
	    !memcmp(&p->vl_arb[block_num], &block,
		    block_length * sizeof(block.vl_entry[0])))
		return IB_SUCCESS;

	attr_mod = ((block_num + 1) << 16) | port_num;

	p_mad = osm_qos_mad_create(sm,p,sizeof(block),(uint8_t *) & block,
				   IB_MAD_ATTR_VL_ARBITRATION, attr_mod);

	if (!p_mad)
		return IB_INSUFFICIENT_MEMORY;

	/*
	 * Zero the stored VL Arbitration block, so in case the MAD will
	 * end up with error, we will resend it in the next sweep.
	 */
	memset(&p->vl_arb[block_num], 0,
	       block_length * sizeof(block.vl_entry[0]));

	cl_qlist_insert_tail(mad_list, &p_mad->list_item);

	return IB_SUCCESS;
}

static ib_api_status_t vlarb_update(osm_sm_t * sm, osm_physp_t * p,
				    uint8_t port_num, unsigned force_update,
				    const struct qos_config *qcfg,
				    cl_qlist_t *mad_list)
{
	ib_api_status_t status = IB_SUCCESS;
	ib_port_info_t *p_pi = &p->port_info;
	unsigned len;

	if (p_pi->vl_arb_low_cap > 0) {
		len = p_pi->vl_arb_low_cap < IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK ?
		    p_pi->vl_arb_low_cap : IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK;
		if ((status = vlarb_update_table_block(sm, p, port_num,
						       force_update,
						       &qcfg->vlarb_low[0],
						       len, 0,
						       mad_list)) != IB_SUCCESS)
			return status;
	}
	if (p_pi->vl_arb_low_cap > IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK) {
		len = p_pi->vl_arb_low_cap % IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK;
		if ((status = vlarb_update_table_block(sm, p, port_num,
						       force_update,
						       &qcfg->vlarb_low[1],
						       len, 1,
						       mad_list)) != IB_SUCCESS)
			return status;
	}
	if (p_pi->vl_arb_high_cap > 0) {
		len = p_pi->vl_arb_high_cap < IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK ?
		    p_pi->vl_arb_high_cap : IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK;
		if ((status = vlarb_update_table_block(sm, p, port_num,
						       force_update,
						       &qcfg->vlarb_high[0],
						       len, 2,
						       mad_list)) != IB_SUCCESS)
			return status;
	}
	if (p_pi->vl_arb_high_cap > IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK) {
		len = p_pi->vl_arb_high_cap % IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK;
		if ((status = vlarb_update_table_block(sm, p, port_num,
						       force_update,
						       &qcfg->vlarb_high[1],
						       len, 3,
						       mad_list)) != IB_SUCCESS)
			return status;
	}

	return status;
}

static ib_api_status_t sl2vl_update_table(osm_sm_t * sm, osm_physp_t * p,
					  uint8_t in_port, uint32_t attr_mod,
					  unsigned force_update,
					  const ib_slvl_table_t * sl2vl_table,
					  cl_qlist_t *mad_list)
{
	ib_slvl_table_t tbl, *p_tbl;
	unsigned vl_mask;
	uint8_t vl1, vl2;
	int i;
	qos_mad_item_t *p_mad;

	vl_mask = (1 << (ib_port_info_get_op_vls(&p->port_info) - 1)) - 1;

	for (i = 0; i < IB_MAX_NUM_VLS / 2; i++) {
		vl1 = sl2vl_table->raw_vl_by_sl[i] >> 4;
		vl2 = sl2vl_table->raw_vl_by_sl[i] & 0xf;
		if (vl1 != 15)
			vl1 &= vl_mask;
		if (vl2 != 15)
			vl2 &= vl_mask;
		tbl.raw_vl_by_sl[i] = (vl1 << 4) | vl2;
	}

	p_tbl = osm_physp_get_slvl_tbl(p, in_port);

	if (!force_update && !memcmp(p_tbl, &tbl, sizeof(tbl)))
		return IB_SUCCESS;

	p_mad = osm_qos_mad_create(sm, p, sizeof(tbl), (uint8_t *) & tbl,
				   IB_MAD_ATTR_SLVL_TABLE, attr_mod);
	if (!p_mad)
		return IB_INSUFFICIENT_MEMORY;

	/*
	 * Zero the stored SL2VL block, so in case the MAD will
	 * end up with error, we will resend it in the next sweep.
	 */
	memset(p_tbl, 0, sizeof(tbl));

	cl_qlist_insert_tail(mad_list, &p_mad->list_item);
	return IB_SUCCESS;
}

static int qos_extports_setup(osm_sm_t * sm, osm_node_t *node,
			      const struct qos_config *qcfg,
			      cl_qlist_t *port_mad_list)

{
	osm_physp_t *p0, *p;
	unsigned force_update;
	unsigned num_ports = osm_node_get_num_physp(node);
	struct osm_routing_engine *re = sm->p_subn->p_osm->routing_engine_used;
	int ret = 0;
	unsigned in, out;
	uint8_t op_vl, common_op_vl = 0, max_num = 0;
	uint8_t op_vl_arr[15];

	/*
	 * Do nothing unless the most recent routing attempt was successful.
	 */
	if (!re)
		return ret;

	for (out = 1; out < num_ports; out++) {
		p = osm_node_get_physp_ptr(node, out);
		if (!p)
			continue;
		if (ib_port_info_get_port_state(&p->port_info) == IB_LINK_DOWN)
			continue;
		force_update = p->need_update || sm->p_subn->need_update;
		p->vl_high_limit = qcfg->vl_high_limit;
		if (vlarb_update(sm, p, p->port_num, force_update, qcfg,
				 port_mad_list))
			ret = -1;
	}

	p0 = osm_node_get_physp_ptr(node, 0);
	if (!(p0->port_info.capability_mask & IB_PORT_CAP_HAS_SL_MAP))
		return ret;

	if (ib_switch_info_get_opt_sl2vlmapping(&node->sw->switch_info) &&
	    sm->p_subn->opt.use_optimized_slvl && !re->update_sl2vl) {

		/* we should find the op_vl that is used by majority of ports */
		memset(&op_vl_arr[0], 0, sizeof(op_vl_arr));
		p0 = osm_node_get_physp_ptr(node, 1);

		for (out = 1; out < num_ports; out++) {
			p = osm_node_get_physp_ptr(node, out);
			if (!p)
				continue;
			if (ib_port_info_get_port_state(&p->port_info) ==
			    IB_LINK_DOWN)
				continue;
			op_vl = ib_port_info_get_op_vls(&p->port_info);
			op_vl_arr[op_vl]++;
			if (op_vl_arr[op_vl] > max_num){
				max_num = op_vl_arr[op_vl];
				common_op_vl = op_vl;
				/* remember the port with most common op_vl */
				p0 = p;
			}

		}
		if (!p0)
			return -1;
		force_update = node->sw->need_update || sm->p_subn->need_update;
		if (sl2vl_update_table(sm, p0, p0->port_num, 0x30000, force_update,
					&qcfg->sl2vl, port_mad_list))
			ret = -1;
		/*
		 * Overwrite default ALL configuration if port's
		 * op_vl is different.
		 */
		for (out = 1; out < num_ports; out++) {
			p = osm_node_get_physp_ptr(node, out);
			if (!p)
				continue;
			if (ib_port_info_get_port_state(&p->port_info) ==
			    IB_LINK_DOWN)
				continue;

			force_update = p->need_update || force_update;
			if (ib_port_info_get_op_vls(&p->port_info) !=
			    common_op_vl &&
			    sl2vl_update_table(sm, p, p->port_num, 0x20000 | out,
					       force_update, &qcfg->sl2vl,
					       port_mad_list))
				ret = -1;
		}
		return ret;
	}

	/* non optimized sl2vl configuration */
	out = ib_switch_info_is_enhanced_port0(&node->sw->switch_info) ? 0 : 1;
	for (; out < num_ports; out++) {
		p = osm_node_get_physp_ptr(node, out);
		if (!p)
			continue;
		if (ib_port_info_get_port_state(&p->port_info) == IB_LINK_DOWN)
			continue;
		force_update = p->need_update || sm->p_subn->need_update;
		/* go over all in ports */
		for (in = 0; in < num_ports; in++) {
			const ib_slvl_table_t *port_sl2vl = &qcfg->sl2vl;
			ib_slvl_table_t routing_sl2vl;

			if (re->update_sl2vl) {
				routing_sl2vl = *port_sl2vl;
				re->update_sl2vl(re->context,
						 p, in, out, &routing_sl2vl);
				port_sl2vl = &routing_sl2vl;
			}
			if (sl2vl_update_table(sm, p, in, in << 8 | out,
					       force_update, port_sl2vl,
					       port_mad_list))
				ret = -1;
		}
	}

	return ret;
}

static int qos_endport_setup(osm_sm_t * sm, osm_physp_t * p,
			     const struct qos_config *qcfg, int vlarb_only,
			     cl_qlist_t *port_mad_list)
{
	unsigned force_update = p->need_update || sm->p_subn->need_update;
	struct osm_routing_engine *re = sm->p_subn->p_osm->routing_engine_used;
	const ib_slvl_table_t *port_sl2vl = &qcfg->sl2vl;
	ib_slvl_table_t routing_sl2vl;

	p->vl_high_limit = qcfg->vl_high_limit;
	if (vlarb_update(sm, p, 0, force_update, qcfg, port_mad_list))
		return -1;
	if (vlarb_only)
		return 0;

	if (!(p->port_info.capability_mask & IB_PORT_CAP_HAS_SL_MAP))
		return 0;

	if (re && re->update_sl2vl) {
		routing_sl2vl = *port_sl2vl;
		re->update_sl2vl(re->context, p, 0, 0, &routing_sl2vl);
		port_sl2vl = &routing_sl2vl;
	}
	if (sl2vl_update_table(sm, p, 0, 0, force_update, port_sl2vl,
			       port_mad_list))
		return -1;

	return 0;
}

int osm_qos_setup(osm_opensm_t * p_osm)
{
	struct qos_config ca_config, sw0_config, swe_config, rtr_config;
	struct qos_config *cfg;
	cl_qmap_t *p_tbl;
	cl_map_item_t *p_next;
	osm_port_t *p_port;
	osm_node_t *p_node;
	int ret = 0;
	int vlarb_only;
	qos_mad_list_t *p_list, *p_list_next;
	qos_mad_item_t *p_port_mad;
	cl_qlist_t qos_mad_list;

	if (!p_osm->subn.opt.qos)
		return 0;

	OSM_LOG_ENTER(&p_osm->log);

	qos_build_config(&ca_config, &p_osm->subn.opt.qos_ca_options,
			 &p_osm->subn.opt.qos_options);
	qos_build_config(&sw0_config, &p_osm->subn.opt.qos_sw0_options,
			 &p_osm->subn.opt.qos_options);
	qos_build_config(&swe_config, &p_osm->subn.opt.qos_swe_options,
			 &p_osm->subn.opt.qos_options);
	qos_build_config(&rtr_config, &p_osm->subn.opt.qos_rtr_options,
			 &p_osm->subn.opt.qos_options);

	cl_qlist_init(&qos_mad_list);

	cl_plock_excl_acquire(&p_osm->lock);

	/* read QoS policy config file */
	osm_qos_parse_policy_file(&p_osm->subn);
	p_tbl = &p_osm->subn.port_guid_tbl;
	p_next = cl_qmap_head(p_tbl);
	while (p_next != cl_qmap_end(p_tbl)) {
		vlarb_only = 0;
		p_port = (osm_port_t *) p_next;
		p_next = cl_qmap_next(p_next);

		p_list = (qos_mad_list_t *) malloc(sizeof(*p_list));
		if (!p_list) {
			cl_plock_release(&p_osm->lock);
			return -1;
		}

		memset(p_list, 0, sizeof(*p_list));

		cl_qlist_init(&p_list->port_mad_list);

		p_node = p_port->p_node;
		if (p_node->sw) {
			if (qos_extports_setup(&p_osm->sm, p_node, &swe_config,
					       &p_list->port_mad_list)) {
				cl_plock_release(&p_osm->lock);
				ret = -1;
			}

			/* skip base port 0 */
			if (!ib_switch_info_is_enhanced_port0
			    (&p_node->sw->switch_info))
				goto Continue;

			if (ib_switch_info_get_opt_sl2vlmapping(&p_node->sw->switch_info) &&
			    p_osm->sm.p_subn->opt.use_optimized_slvl &&
			    !memcmp(&swe_config.sl2vl, &sw0_config.sl2vl,
				    sizeof(swe_config.sl2vl)))
				vlarb_only = 1;

			cfg = &sw0_config;
		} else if (osm_node_get_type(p_node) == IB_NODE_TYPE_ROUTER)
			cfg = &rtr_config;
		else
			cfg = &ca_config;

		if (qos_endport_setup(&p_osm->sm, p_port->p_physp, cfg,
				      vlarb_only, &p_list->port_mad_list)) {
			cl_plock_release(&p_osm->lock);
			ret = -1;
		}
Continue:
		/* if MAD list is not empty, add it to the global MAD list */
		if (cl_qlist_count(&p_list->port_mad_list)) {
			cl_qlist_insert_tail(&qos_mad_list, &p_list->list_item);
		} else {
			free(p_list);
		}
	}
	while (cl_qlist_count(&qos_mad_list)) {
		p_list_next = (qos_mad_list_t *) cl_qlist_head(&qos_mad_list);
		while (p_list_next !=
			(qos_mad_list_t *) cl_qlist_end(&qos_mad_list)) {
			p_list = p_list_next;
			p_list_next = (qos_mad_list_t *)
				      cl_qlist_next(&p_list->list_item);
			/* next MAD to send*/
			p_port_mad = (qos_mad_item_t *)
				     cl_qlist_remove_head(&p_list->port_mad_list);
			osm_send_req_mad(&p_osm->sm, p_port_mad->p_madw);
			osm_qos_mad_delete(&p_port_mad);
			/* remove the QoS MAD from global MAD list */
			if (cl_qlist_count(&p_list->port_mad_list) == 0) {
				cl_qlist_remove_item(&qos_mad_list, &p_list->list_item);
				free(p_list);
			}
		}
	}

	cl_plock_release(&p_osm->lock);
	OSM_LOG_EXIT(&p_osm->log);

	return ret;
}

/*
 *  QoS config stuff
 */
static int parse_one_unsigned(const char *str, char delim, unsigned *val)
{
	char *end;
	*val = strtoul(str, &end, 0);
	if (*end)
		end++;
	return (int)(end - str);
}

static int parse_vlarb_entry(const char *str, ib_vl_arb_element_t * e)
{
	unsigned val;
	const char *p = str;
	p += parse_one_unsigned(p, ':', &val);
	e->vl = val % 15;
	p += parse_one_unsigned(p, ',', &val);
	e->weight = (uint8_t) val;
	return (int)(p - str);
}

static int parse_sl2vl_entry(const char *str, uint8_t * raw)
{
	unsigned val1, val2;
	const char *p = str;
	p += parse_one_unsigned(p, ',', &val1);
	p += parse_one_unsigned(p, ',', &val2);
	*raw = (val1 << 4) | (val2 & 0xf);
	return (int)(p - str);
}

static void qos_build_config(struct qos_config *cfg, osm_qos_options_t * opt,
			     osm_qos_options_t * dflt)
{
	int i;
	const char *p;

	memset(cfg, 0, sizeof(*cfg));

	if (opt->max_vls > 0)
		cfg->max_vls = opt->max_vls;
	else {
		if (dflt->max_vls > 0)
			cfg->max_vls = dflt->max_vls;
		else
			cfg->max_vls = OSM_DEFAULT_QOS_MAX_VLS;
	}

	if (opt->high_limit >= 0)
		cfg->vl_high_limit = (uint8_t) opt->high_limit;
	else {
		if (dflt->high_limit >= 0)
			cfg->vl_high_limit = (uint8_t) dflt->high_limit;
		else
			cfg->vl_high_limit = (uint8_t) OSM_DEFAULT_QOS_HIGH_LIMIT;
	}

	if (opt->vlarb_high)
		p = opt->vlarb_high;
	else {
		if (dflt->vlarb_high)
			p = dflt->vlarb_high;
		else
			p = OSM_DEFAULT_QOS_VLARB_HIGH;
	}
	for (i = 0; i < 2 * IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK; i++) {
		p += parse_vlarb_entry(p,
				       &cfg->vlarb_high[i /
							IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK].
				       vl_entry[i %
						IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK]);
	}

	if (opt->vlarb_low)
		p = opt->vlarb_low;
	else {
		if (dflt->vlarb_low)
			p = dflt->vlarb_low;
		else
			p = OSM_DEFAULT_QOS_VLARB_LOW;
	}
	for (i = 0; i < 2 * IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK; i++) {
		p += parse_vlarb_entry(p,
				       &cfg->vlarb_low[i /
						       IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK].
				       vl_entry[i %
						IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK]);
	}

	p = opt->sl2vl ? opt->sl2vl : dflt->sl2vl;
	if (opt->sl2vl)
		p = opt->sl2vl;
	else {
		if (dflt->sl2vl)
			p = dflt->sl2vl;
		else
			p = OSM_DEFAULT_QOS_SL2VL;
	}
	for (i = 0; i < IB_MAX_NUM_VLS / 2; i++)
		p += parse_sl2vl_entry(p, &cfg->sl2vl.raw_vl_by_sl[i]);
}
