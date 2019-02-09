/*
 *  Copyright (C) 2013-2014 Chelsio Communications.  All rights reserved.
 *
 *  Written by Anish Bhatt (anish@chelsio.com)
 *	       Casey Leedom (leedom@chelsio.com)
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#include "cxgb4.h"

/* DCBx version control
 */
const char * const dcb_ver_array[] = {
	"Unknown",
	"DCBx-CIN",
	"DCBx-CEE 1.01",
	"DCBx-IEEE",
	"", "", "",
	"Auto Negotiated"
};

static inline bool cxgb4_dcb_state_synced(enum cxgb4_dcb_state state)
{
	if (state == CXGB4_DCB_STATE_FW_ALLSYNCED ||
	    state == CXGB4_DCB_STATE_HOST)
		return true;
	else
		return false;
}

/* Initialize a port's Data Center Bridging state.
 */
void cxgb4_dcb_state_init(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct port_dcb_info *dcb = &pi->dcb;
	int version_temp = dcb->dcb_version;

	memset(dcb, 0, sizeof(struct port_dcb_info));
	dcb->state = CXGB4_DCB_STATE_START;
	if (version_temp)
		dcb->dcb_version = version_temp;

	netdev_dbg(dev, "%s: Initializing DCB state for port[%d]\n",
		    __func__, pi->port_id);
}

void cxgb4_dcb_version_init(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct port_dcb_info *dcb = &pi->dcb;

	/* Any writes here are only done on kernels that exlicitly need
	 * a specific version, say < 2.6.38 which only support CEE
	 */
	dcb->dcb_version = FW_PORT_DCB_VER_AUTO;
}

static void cxgb4_dcb_cleanup_apps(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	struct port_dcb_info *dcb = &pi->dcb;
	struct dcb_app app;
	int i, err;

	/* zero priority implies remove */
	app.priority = 0;

	for (i = 0; i < CXGB4_MAX_DCBX_APP_SUPPORTED; i++) {
		/* Check if app list is exhausted */
		if (!dcb->app_priority[i].protocolid)
			break;

		app.protocol = dcb->app_priority[i].protocolid;

		if (dcb->dcb_version == FW_PORT_DCB_VER_IEEE) {
			app.priority = dcb->app_priority[i].user_prio_map;
			app.selector = dcb->app_priority[i].sel_field + 1;
			err = dcb_ieee_delapp(dev, &app);
		} else {
			app.selector = !!(dcb->app_priority[i].sel_field);
			err = dcb_setapp(dev, &app);
		}

		if (err) {
			dev_err(adap->pdev_dev,
				"Failed DCB Clear %s Application Priority: sel=%d, prot=%d, , err=%d\n",
				dcb_ver_array[dcb->dcb_version], app.selector,
				app.protocol, -err);
			break;
		}
	}
}

/* Reset a port's Data Center Bridging state.  Typically used after a
 * Link Down event.
 */
void cxgb4_dcb_reset(struct net_device *dev)
{
	cxgb4_dcb_cleanup_apps(dev);
	cxgb4_dcb_state_init(dev);
}

/* Finite State machine for Data Center Bridging.
 */
void cxgb4_dcb_state_fsm(struct net_device *dev,
			 enum cxgb4_dcb_state_input transition_to)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct port_dcb_info *dcb = &pi->dcb;
	struct adapter *adap = pi->adapter;
	enum cxgb4_dcb_state current_state = dcb->state;

	netdev_dbg(dev, "%s: State change from %d to %d for %s\n",
		    __func__, dcb->state, transition_to, dev->name);

	switch (current_state) {
	case CXGB4_DCB_STATE_START: {
		switch (transition_to) {
		case CXGB4_DCB_INPUT_FW_DISABLED: {
			/* we're going to use Host DCB */
			dcb->state = CXGB4_DCB_STATE_HOST;
			dcb->supported = CXGB4_DCBX_HOST_SUPPORT;
			break;
		}

		case CXGB4_DCB_INPUT_FW_ENABLED: {
			/* we're going to use Firmware DCB */
			dcb->state = CXGB4_DCB_STATE_FW_INCOMPLETE;
			dcb->supported = DCB_CAP_DCBX_LLD_MANAGED;
			if (dcb->dcb_version == FW_PORT_DCB_VER_IEEE)
				dcb->supported |= DCB_CAP_DCBX_VER_IEEE;
			else
				dcb->supported |= DCB_CAP_DCBX_VER_CEE;
			break;
		}

		case CXGB4_DCB_INPUT_FW_INCOMPLETE: {
			/* expected transition */
			break;
		}

		case CXGB4_DCB_INPUT_FW_ALLSYNCED: {
			dcb->state = CXGB4_DCB_STATE_FW_ALLSYNCED;
			break;
		}

		default:
			goto bad_state_input;
		}
		break;
	}

	case CXGB4_DCB_STATE_FW_INCOMPLETE: {
		switch (transition_to) {
		case CXGB4_DCB_INPUT_FW_ENABLED: {
			/* we're alreaady in firmware DCB mode */
			break;
		}

		case CXGB4_DCB_INPUT_FW_INCOMPLETE: {
			/* we're already incomplete */
			break;
		}

		case CXGB4_DCB_INPUT_FW_ALLSYNCED: {
			dcb->state = CXGB4_DCB_STATE_FW_ALLSYNCED;
			dcb->enabled = 1;
			linkwatch_fire_event(dev);
			break;
		}

		default:
			goto bad_state_input;
		}
		break;
	}

	case CXGB4_DCB_STATE_FW_ALLSYNCED: {
		switch (transition_to) {
		case CXGB4_DCB_INPUT_FW_ENABLED: {
			/* we're alreaady in firmware DCB mode */
			break;
		}

		case CXGB4_DCB_INPUT_FW_INCOMPLETE: {
			/* We were successfully running with firmware DCB but
			 * now it's telling us that it's in an "incomplete
			 * state.  We need to reset back to a ground state
			 * of incomplete.
			 */
			cxgb4_dcb_reset(dev);
			dcb->state = CXGB4_DCB_STATE_FW_INCOMPLETE;
			dcb->supported = CXGB4_DCBX_FW_SUPPORT;
			linkwatch_fire_event(dev);
			break;
		}

		case CXGB4_DCB_INPUT_FW_ALLSYNCED: {
			/* we're already all sync'ed
			 * this is only applicable for IEEE or
			 * when another VI already completed negotiaton
			 */
			dcb->enabled = 1;
			linkwatch_fire_event(dev);
			break;
		}

		default:
			goto bad_state_input;
		}
		break;
	}

	case CXGB4_DCB_STATE_HOST: {
		switch (transition_to) {
		case CXGB4_DCB_INPUT_FW_DISABLED: {
			/* we're alreaady in Host DCB mode */
			break;
		}

		default:
			goto bad_state_input;
		}
		break;
	}

	default:
		goto bad_state_transition;
	}
	return;

bad_state_input:
	dev_err(adap->pdev_dev, "cxgb4_dcb_state_fsm: illegal input symbol %d\n",
		transition_to);
	return;

bad_state_transition:
	dev_err(adap->pdev_dev, "cxgb4_dcb_state_fsm: bad state transition, state = %d, input = %d\n",
		current_state, transition_to);
}

/* Handle a DCB/DCBX update message from the firmware.
 */
void cxgb4_dcb_handle_fw_update(struct adapter *adap,
				const struct fw_port_cmd *pcmd)
{
	const union fw_port_dcb *fwdcb = &pcmd->u.dcb;
	int port = FW_PORT_CMD_PORTID_G(be32_to_cpu(pcmd->op_to_portid));
	struct net_device *dev = adap->port[adap->chan_map[port]];
	struct port_info *pi = netdev_priv(dev);
	struct port_dcb_info *dcb = &pi->dcb;
	int dcb_type = pcmd->u.dcb.pgid.type;
	int dcb_running_version;

	/* Handle Firmware DCB Control messages separately since they drive
	 * our state machine.
	 */
	if (dcb_type == FW_PORT_DCB_TYPE_CONTROL) {
		enum cxgb4_dcb_state_input input =
			((pcmd->u.dcb.control.all_syncd_pkd &
			  FW_PORT_CMD_ALL_SYNCD_F)
			 ? CXGB4_DCB_STATE_FW_ALLSYNCED
			 : CXGB4_DCB_STATE_FW_INCOMPLETE);

		if (dcb->dcb_version != FW_PORT_DCB_VER_UNKNOWN) {
			dcb_running_version = FW_PORT_CMD_DCB_VERSION_G(
				be16_to_cpu(
				pcmd->u.dcb.control.dcb_version_to_app_state));
			if (dcb_running_version == FW_PORT_DCB_VER_CEE1D01 ||
			    dcb_running_version == FW_PORT_DCB_VER_IEEE) {
				dcb->dcb_version = dcb_running_version;
				dev_warn(adap->pdev_dev, "Interface %s is running %s\n",
					 dev->name,
					 dcb_ver_array[dcb->dcb_version]);
			} else {
				dev_warn(adap->pdev_dev,
					 "Something screwed up, requested firmware for %s, but firmware returned %s instead\n",
					 dcb_ver_array[dcb->dcb_version],
					 dcb_ver_array[dcb_running_version]);
				dcb->dcb_version = FW_PORT_DCB_VER_UNKNOWN;
			}
		}

		cxgb4_dcb_state_fsm(dev, input);
		return;
	}

	/* It's weird, and almost certainly an error, to get Firmware DCB
	 * messages when we either haven't been told whether we're going to be
	 * doing Host or Firmware DCB; and even worse when we've been told
	 * that we're doing Host DCB!
	 */
	if (dcb->state == CXGB4_DCB_STATE_START ||
	    dcb->state == CXGB4_DCB_STATE_HOST) {
		dev_err(adap->pdev_dev, "Receiving Firmware DCB messages in State %d\n",
			dcb->state);
		return;
	}

	/* Now handle the general Firmware DCB update messages ...
	 */
	switch (dcb_type) {
	case FW_PORT_DCB_TYPE_PGID:
		dcb->pgid = be32_to_cpu(fwdcb->pgid.pgid);
		dcb->msgs |= CXGB4_DCB_FW_PGID;
		break;

	case FW_PORT_DCB_TYPE_PGRATE:
		dcb->pg_num_tcs_supported = fwdcb->pgrate.num_tcs_supported;
		memcpy(dcb->pgrate, &fwdcb->pgrate.pgrate,
		       sizeof(dcb->pgrate));
		memcpy(dcb->tsa, &fwdcb->pgrate.tsa,
		       sizeof(dcb->tsa));
		dcb->msgs |= CXGB4_DCB_FW_PGRATE;
		if (dcb->msgs & CXGB4_DCB_FW_PGID)
			IEEE_FAUX_SYNC(dev, dcb);
		break;

	case FW_PORT_DCB_TYPE_PRIORATE:
		memcpy(dcb->priorate, &fwdcb->priorate.strict_priorate,
		       sizeof(dcb->priorate));
		dcb->msgs |= CXGB4_DCB_FW_PRIORATE;
		break;

	case FW_PORT_DCB_TYPE_PFC:
		dcb->pfcen = fwdcb->pfc.pfcen;
		dcb->pfc_num_tcs_supported = fwdcb->pfc.max_pfc_tcs;
		dcb->msgs |= CXGB4_DCB_FW_PFC;
		IEEE_FAUX_SYNC(dev, dcb);
		break;

	case FW_PORT_DCB_TYPE_APP_ID: {
		const struct fw_port_app_priority *fwap = &fwdcb->app_priority;
		int idx = fwap->idx;
		struct app_priority *ap = &dcb->app_priority[idx];

		struct dcb_app app = {
			.protocol = be16_to_cpu(fwap->protocolid),
		};
		int err;

		/* Convert from firmware format to relevant format
		 * when using app selector
		 */
		if (dcb->dcb_version == FW_PORT_DCB_VER_IEEE) {
			app.selector = (fwap->sel_field + 1);
			app.priority = ffs(fwap->user_prio_map) - 1;
			err = dcb_ieee_setapp(dev, &app);
			IEEE_FAUX_SYNC(dev, dcb);
		} else {
			/* Default is CEE */
			app.selector = !!(fwap->sel_field);
			app.priority = fwap->user_prio_map;
			err = dcb_setapp(dev, &app);
		}

		if (err)
			dev_err(adap->pdev_dev,
				"Failed DCB Set Application Priority: sel=%d, prot=%d, prio=%d, err=%d\n",
				app.selector, app.protocol, app.priority, -err);

		ap->user_prio_map = fwap->user_prio_map;
		ap->sel_field = fwap->sel_field;
		ap->protocolid = be16_to_cpu(fwap->protocolid);
		dcb->msgs |= CXGB4_DCB_FW_APP_ID;
		break;
	}

	default:
		dev_err(adap->pdev_dev, "Unknown DCB update type received %x\n",
			dcb_type);
		break;
	}
}

/* Data Center Bridging netlink operations.
 */


/* Get current DCB enabled/disabled state.
 */
static u8 cxgb4_getstate(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);

	return pi->dcb.enabled;
}

/* Set DCB enabled/disabled.
 */
static u8 cxgb4_setstate(struct net_device *dev, u8 enabled)
{
	struct port_info *pi = netdev2pinfo(dev);

	/* If DCBx is host-managed, dcb is enabled by outside lldp agents */
	if (pi->dcb.state == CXGB4_DCB_STATE_HOST) {
		pi->dcb.enabled = enabled;
		return 0;
	}

	/* Firmware doesn't provide any mechanism to control the DCB state.
	 */
	if (enabled != (pi->dcb.state == CXGB4_DCB_STATE_FW_ALLSYNCED))
		return 1;

	return 0;
}

static void cxgb4_getpgtccfg(struct net_device *dev, int tc,
			     u8 *prio_type, u8 *pgid, u8 *bw_per,
			     u8 *up_tc_map, int local)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int err;

	*prio_type = *pgid = *bw_per = *up_tc_map = 0;

	if (local)
		INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
	else
		INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);

	pcmd.u.dcb.pgid.type = FW_PORT_DCB_TYPE_PGID;
	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGID failed with %d\n", -err);
		return;
	}
	*pgid = (be32_to_cpu(pcmd.u.dcb.pgid.pgid) >> (tc * 4)) & 0xf;

	if (local)
		INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
	else
		INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);
	pcmd.u.dcb.pgrate.type = FW_PORT_DCB_TYPE_PGRATE;
	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGRATE failed with %d\n",
			-err);
		return;
	}

	*bw_per = pcmd.u.dcb.pgrate.pgrate[*pgid];
	*up_tc_map = (1 << tc);

	/* prio_type is link strict */
	if (*pgid != 0xF)
		*prio_type = 0x2;
}

static void cxgb4_getpgtccfg_tx(struct net_device *dev, int tc,
				u8 *prio_type, u8 *pgid, u8 *bw_per,
				u8 *up_tc_map)
{
	/* tc 0 is written at MSB position */
	return cxgb4_getpgtccfg(dev, (7 - tc), prio_type, pgid, bw_per,
				up_tc_map, 1);
}


static void cxgb4_getpgtccfg_rx(struct net_device *dev, int tc,
				u8 *prio_type, u8 *pgid, u8 *bw_per,
				u8 *up_tc_map)
{
	/* tc 0 is written at MSB position */
	return cxgb4_getpgtccfg(dev, (7 - tc), prio_type, pgid, bw_per,
				up_tc_map, 0);
}

static void cxgb4_setpgtccfg_tx(struct net_device *dev, int tc,
				u8 prio_type, u8 pgid, u8 bw_per,
				u8 up_tc_map)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int fw_tc = 7 - tc;
	u32 _pgid;
	int err;

	if (pgid == DCB_ATTR_VALUE_UNDEFINED)
		return;
	if (bw_per == DCB_ATTR_VALUE_UNDEFINED)
		return;

	INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
	pcmd.u.dcb.pgid.type = FW_PORT_DCB_TYPE_PGID;

	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGID failed with %d\n", -err);
		return;
	}

	_pgid = be32_to_cpu(pcmd.u.dcb.pgid.pgid);
	_pgid &= ~(0xF << (fw_tc * 4));
	_pgid |= pgid << (fw_tc * 4);
	pcmd.u.dcb.pgid.pgid = cpu_to_be32(_pgid);

	INIT_PORT_DCB_WRITE_CMD(pcmd, pi->port_id);

	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB write PGID failed with %d\n",
			-err);
		return;
	}

	memset(&pcmd, 0, sizeof(struct fw_port_cmd));

	INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
	pcmd.u.dcb.pgrate.type = FW_PORT_DCB_TYPE_PGRATE;

	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGRATE failed with %d\n",
			-err);
		return;
	}

	pcmd.u.dcb.pgrate.pgrate[pgid] = bw_per;

	INIT_PORT_DCB_WRITE_CMD(pcmd, pi->port_id);
	if (pi->dcb.state == CXGB4_DCB_STATE_HOST)
		pcmd.op_to_portid |= cpu_to_be32(FW_PORT_CMD_APPLY_F);

	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS)
		dev_err(adap->pdev_dev, "DCB write PGRATE failed with %d\n",
			-err);
}

static void cxgb4_getpgbwgcfg(struct net_device *dev, int pgid, u8 *bw_per,
			      int local)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int err;

	if (local)
		INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
	else
		INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);

	pcmd.u.dcb.pgrate.type = FW_PORT_DCB_TYPE_PGRATE;
	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGRATE failed with %d\n",
			-err);
		return;
	}

	*bw_per = pcmd.u.dcb.pgrate.pgrate[pgid];
}

static void cxgb4_getpgbwgcfg_tx(struct net_device *dev, int pgid, u8 *bw_per)
{
	return cxgb4_getpgbwgcfg(dev, pgid, bw_per, 1);
}

static void cxgb4_getpgbwgcfg_rx(struct net_device *dev, int pgid, u8 *bw_per)
{
	return cxgb4_getpgbwgcfg(dev, pgid, bw_per, 0);
}

static void cxgb4_setpgbwgcfg_tx(struct net_device *dev, int pgid,
				 u8 bw_per)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int err;

	INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
	pcmd.u.dcb.pgrate.type = FW_PORT_DCB_TYPE_PGRATE;

	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGRATE failed with %d\n",
			-err);
		return;
	}

	pcmd.u.dcb.pgrate.pgrate[pgid] = bw_per;

	INIT_PORT_DCB_WRITE_CMD(pcmd, pi->port_id);
	if (pi->dcb.state == CXGB4_DCB_STATE_HOST)
		pcmd.op_to_portid |= cpu_to_be32(FW_PORT_CMD_APPLY_F);

	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);

	if (err != FW_PORT_DCB_CFG_SUCCESS)
		dev_err(adap->pdev_dev, "DCB write PGRATE failed with %d\n",
			-err);
}

/* Return whether the specified Traffic Class Priority has Priority Pause
 * Frames enabled.
 */
static void cxgb4_getpfccfg(struct net_device *dev, int priority, u8 *pfccfg)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct port_dcb_info *dcb = &pi->dcb;

	if (!cxgb4_dcb_state_synced(dcb->state) ||
	    priority >= CXGB4_MAX_PRIORITY)
		*pfccfg = 0;
	else
		*pfccfg = (pi->dcb.pfcen >> (7 - priority)) & 1;
}

/* Enable/disable Priority Pause Frames for the specified Traffic Class
 * Priority.
 */
static void cxgb4_setpfccfg(struct net_device *dev, int priority, u8 pfccfg)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int err;

	if (!cxgb4_dcb_state_synced(pi->dcb.state) ||
	    priority >= CXGB4_MAX_PRIORITY)
		return;

	INIT_PORT_DCB_WRITE_CMD(pcmd, pi->port_id);
	if (pi->dcb.state == CXGB4_DCB_STATE_HOST)
		pcmd.op_to_portid |= cpu_to_be32(FW_PORT_CMD_APPLY_F);

	pcmd.u.dcb.pfc.type = FW_PORT_DCB_TYPE_PFC;
	pcmd.u.dcb.pfc.pfcen = pi->dcb.pfcen;

	if (pfccfg)
		pcmd.u.dcb.pfc.pfcen |= (1 << (7 - priority));
	else
		pcmd.u.dcb.pfc.pfcen &= (~(1 << (7 - priority)));

	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB PFC write failed with %d\n", -err);
		return;
	}

	pi->dcb.pfcen = pcmd.u.dcb.pfc.pfcen;
}

static u8 cxgb4_setall(struct net_device *dev)
{
	return 0;
}

/* Return DCB capabilities.
 */
static u8 cxgb4_getcap(struct net_device *dev, int cap_id, u8 *caps)
{
	struct port_info *pi = netdev2pinfo(dev);

	switch (cap_id) {
	case DCB_CAP_ATTR_PG:
	case DCB_CAP_ATTR_PFC:
		*caps = true;
		break;

	case DCB_CAP_ATTR_PG_TCS:
		/* 8 priorities for PG represented by bitmap */
		*caps = 0x80;
		break;

	case DCB_CAP_ATTR_PFC_TCS:
		/* 8 priorities for PFC represented by bitmap */
		*caps = 0x80;
		break;

	case DCB_CAP_ATTR_GSP:
		*caps = true;
		break;

	case DCB_CAP_ATTR_UP2TC:
	case DCB_CAP_ATTR_BCN:
		*caps = false;
		break;

	case DCB_CAP_ATTR_DCBX:
		*caps = pi->dcb.supported;
		break;

	default:
		*caps = false;
	}

	return 0;
}

/* Return the number of Traffic Classes for the indicated Traffic Class ID.
 */
static int cxgb4_getnumtcs(struct net_device *dev, int tcs_id, u8 *num)
{
	struct port_info *pi = netdev2pinfo(dev);

	switch (tcs_id) {
	case DCB_NUMTCS_ATTR_PG:
		if (pi->dcb.msgs & CXGB4_DCB_FW_PGRATE)
			*num = pi->dcb.pg_num_tcs_supported;
		else
			*num = 0x8;
		break;

	case DCB_NUMTCS_ATTR_PFC:
		*num = 0x8;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* Set the number of Traffic Classes supported for the indicated Traffic Class
 * ID.
 */
static int cxgb4_setnumtcs(struct net_device *dev, int tcs_id, u8 num)
{
	/* Setting the number of Traffic Classes isn't supported.
	 */
	return -ENOSYS;
}

/* Return whether Priority Flow Control is enabled.  */
static u8 cxgb4_getpfcstate(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);

	if (!cxgb4_dcb_state_synced(pi->dcb.state))
		return false;

	return pi->dcb.pfcen != 0;
}

/* Enable/disable Priority Flow Control. */
static void cxgb4_setpfcstate(struct net_device *dev, u8 state)
{
	/* We can't enable/disable Priority Flow Control but we also can't
	 * return an error ...
	 */
}

/* Return the Application User Priority Map associated with the specified
 * Application ID.
 */
static int __cxgb4_getapp(struct net_device *dev, u8 app_idtype, u16 app_id,
			  int peer)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int i;

	if (!cxgb4_dcb_state_synced(pi->dcb.state))
		return 0;

	for (i = 0; i < CXGB4_MAX_DCBX_APP_SUPPORTED; i++) {
		struct fw_port_cmd pcmd;
		int err;

		if (peer)
			INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);
		else
			INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);

		pcmd.u.dcb.app_priority.type = FW_PORT_DCB_TYPE_APP_ID;
		pcmd.u.dcb.app_priority.idx = i;

		err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
		if (err != FW_PORT_DCB_CFG_SUCCESS) {
			dev_err(adap->pdev_dev, "DCB APP read failed with %d\n",
				-err);
			return err;
		}
		if (be16_to_cpu(pcmd.u.dcb.app_priority.protocolid) == app_id)
			if (pcmd.u.dcb.app_priority.sel_field == app_idtype)
				return pcmd.u.dcb.app_priority.user_prio_map;

		/* exhausted app list */
		if (!pcmd.u.dcb.app_priority.protocolid)
			break;
	}

	return -EEXIST;
}

/* Return the Application User Priority Map associated with the specified
 * Application ID.
 */
static int cxgb4_getapp(struct net_device *dev, u8 app_idtype, u16 app_id)
{
	/* Convert app_idtype to firmware format before querying */
	return __cxgb4_getapp(dev, app_idtype == DCB_APP_IDTYPE_ETHTYPE ?
			      app_idtype : 3, app_id, 0);
}

/* Write a new Application User Priority Map for the specified Application ID
 */
static int __cxgb4_setapp(struct net_device *dev, u8 app_idtype, u16 app_id,
			  u8 app_prio)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int i, err;


	if (!cxgb4_dcb_state_synced(pi->dcb.state))
		return -EINVAL;

	/* DCB info gets thrown away on link up */
	if (!netif_carrier_ok(dev))
		return -ENOLINK;

	for (i = 0; i < CXGB4_MAX_DCBX_APP_SUPPORTED; i++) {
		INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
		pcmd.u.dcb.app_priority.type = FW_PORT_DCB_TYPE_APP_ID;
		pcmd.u.dcb.app_priority.idx = i;
		err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);

		if (err != FW_PORT_DCB_CFG_SUCCESS) {
			dev_err(adap->pdev_dev, "DCB app table read failed with %d\n",
				-err);
			return err;
		}
		if (be16_to_cpu(pcmd.u.dcb.app_priority.protocolid) == app_id) {
			/* overwrite existing app table */
			pcmd.u.dcb.app_priority.protocolid = 0;
			break;
		}
		/* find first empty slot */
		if (!pcmd.u.dcb.app_priority.protocolid)
			break;
	}

	if (i == CXGB4_MAX_DCBX_APP_SUPPORTED) {
		/* no empty slots available */
		dev_err(adap->pdev_dev, "DCB app table full\n");
		return -EBUSY;
	}

	/* write out new app table entry */
	INIT_PORT_DCB_WRITE_CMD(pcmd, pi->port_id);
	if (pi->dcb.state == CXGB4_DCB_STATE_HOST)
		pcmd.op_to_portid |= cpu_to_be32(FW_PORT_CMD_APPLY_F);

	pcmd.u.dcb.app_priority.type = FW_PORT_DCB_TYPE_APP_ID;
	pcmd.u.dcb.app_priority.protocolid = cpu_to_be16(app_id);
	pcmd.u.dcb.app_priority.sel_field = app_idtype;
	pcmd.u.dcb.app_priority.user_prio_map = app_prio;
	pcmd.u.dcb.app_priority.idx = i;

	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB app table write failed with %d\n",
			-err);
		return err;
	}

	return 0;
}

/* Priority for CEE inside dcb_app is bitmask, with 0 being an invalid value */
static int cxgb4_setapp(struct net_device *dev, u8 app_idtype, u16 app_id,
			u8 app_prio)
{
	int ret;
	struct dcb_app app = {
		.selector = app_idtype,
		.protocol = app_id,
		.priority = app_prio,
	};

	if (app_idtype != DCB_APP_IDTYPE_ETHTYPE &&
	    app_idtype != DCB_APP_IDTYPE_PORTNUM)
		return -EINVAL;

	/* Convert app_idtype to a format that firmware understands */
	ret = __cxgb4_setapp(dev, app_idtype == DCB_APP_IDTYPE_ETHTYPE ?
			      app_idtype : 3, app_id, app_prio);
	if (ret)
		return ret;

	return dcb_setapp(dev, &app);
}

/* Return whether IEEE Data Center Bridging has been negotiated.
 */
static inline int
cxgb4_ieee_negotiation_complete(struct net_device *dev,
				enum cxgb4_dcb_fw_msgs dcb_subtype)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct port_dcb_info *dcb = &pi->dcb;

	if (dcb->state == CXGB4_DCB_STATE_FW_ALLSYNCED)
		if (dcb_subtype && !(dcb->msgs & dcb_subtype))
			return 0;

	return (cxgb4_dcb_state_synced(dcb->state) &&
		(dcb->supported & DCB_CAP_DCBX_VER_IEEE));
}

static int cxgb4_ieee_read_ets(struct net_device *dev, struct ieee_ets *ets,
			       int local)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct port_dcb_info *dcb = &pi->dcb;
	struct adapter *adap = pi->adapter;
	uint32_t tc_info;
	struct fw_port_cmd pcmd;
	int i, bwg, err;

	if (!(dcb->msgs & (CXGB4_DCB_FW_PGID | CXGB4_DCB_FW_PGRATE)))
		return 0;

	ets->ets_cap =  dcb->pg_num_tcs_supported;

	if (local) {
		ets->willing = 1;
		INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
	} else {
		INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);
	}

	pcmd.u.dcb.pgid.type = FW_PORT_DCB_TYPE_PGID;
	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGID failed with %d\n", -err);
		return err;
	}

	tc_info = be32_to_cpu(pcmd.u.dcb.pgid.pgid);

	if (local)
		INIT_PORT_DCB_READ_LOCAL_CMD(pcmd, pi->port_id);
	else
		INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);

	pcmd.u.dcb.pgrate.type = FW_PORT_DCB_TYPE_PGRATE;
	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGRATE failed with %d\n",
			-err);
		return err;
	}

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		bwg = (tc_info >> ((7 - i) * 4)) & 0xF;
		ets->prio_tc[i] = bwg;
		ets->tc_tx_bw[i] = pcmd.u.dcb.pgrate.pgrate[i];
		ets->tc_rx_bw[i] = ets->tc_tx_bw[i];
		ets->tc_tsa[i] = pcmd.u.dcb.pgrate.tsa[i];
	}

	return 0;
}

static int cxgb4_ieee_get_ets(struct net_device *dev, struct ieee_ets *ets)
{
	return cxgb4_ieee_read_ets(dev, ets, 1);
}

/* We reuse this for peer PFC as well, as we can't have it enabled one way */
static int cxgb4_ieee_get_pfc(struct net_device *dev, struct ieee_pfc *pfc)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct port_dcb_info *dcb = &pi->dcb;

	memset(pfc, 0, sizeof(struct ieee_pfc));

	if (!(dcb->msgs & CXGB4_DCB_FW_PFC))
		return 0;

	pfc->pfc_cap = dcb->pfc_num_tcs_supported;
	pfc->pfc_en = bitswap_1(dcb->pfcen);

	return 0;
}

static int cxgb4_ieee_peer_ets(struct net_device *dev, struct ieee_ets *ets)
{
	return cxgb4_ieee_read_ets(dev, ets, 0);
}

/* Fill in the Application User Priority Map associated with the
 * specified Application.
 * Priority for IEEE dcb_app is an integer, with 0 being a valid value
 */
static int cxgb4_ieee_getapp(struct net_device *dev, struct dcb_app *app)
{
	int prio;

	if (!cxgb4_ieee_negotiation_complete(dev, CXGB4_DCB_FW_APP_ID))
		return -EINVAL;
	if (!(app->selector && app->protocol))
		return -EINVAL;

	/* Try querying firmware first, use firmware format */
	prio = __cxgb4_getapp(dev, app->selector - 1, app->protocol, 0);

	if (prio < 0)
		prio = dcb_ieee_getapp_mask(dev, app);

	app->priority = ffs(prio) - 1;
	return 0;
}

/* Write a new Application User Priority Map for the specified Application ID.
 * Priority for IEEE dcb_app is an integer, with 0 being a valid value
 */
static int cxgb4_ieee_setapp(struct net_device *dev, struct dcb_app *app)
{
	int ret;

	if (!cxgb4_ieee_negotiation_complete(dev, CXGB4_DCB_FW_APP_ID))
		return -EINVAL;
	if (!(app->selector && app->protocol))
		return -EINVAL;

	if (!(app->selector > IEEE_8021QAZ_APP_SEL_ETHERTYPE  &&
	      app->selector < IEEE_8021QAZ_APP_SEL_ANY))
		return -EINVAL;

	/* change selector to a format that firmware understands */
	ret = __cxgb4_setapp(dev, app->selector - 1, app->protocol,
			     (1 << app->priority));
	if (ret)
		return ret;

	return dcb_ieee_setapp(dev, app);
}

/* Return our DCBX parameters.
 */
static u8 cxgb4_getdcbx(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);

	/* This is already set by cxgb4_set_dcb_caps, so just return it */
	return pi->dcb.supported;
}

/* Set our DCBX parameters.
 */
static u8 cxgb4_setdcbx(struct net_device *dev, u8 dcb_request)
{
	struct port_info *pi = netdev2pinfo(dev);

	/* Filter out requests which exceed our capabilities.
	 */
	if ((dcb_request & (CXGB4_DCBX_FW_SUPPORT | CXGB4_DCBX_HOST_SUPPORT))
	    != dcb_request)
		return 1;

	/* Can't enable DCB if we haven't successfully negotiated it.
	 */
	if (!cxgb4_dcb_state_synced(pi->dcb.state))
		return 1;

	/* There's currently no mechanism to allow for the firmware DCBX
	 * negotiation to be changed from the Host Driver.  If the caller
	 * requests exactly the same parameters that we already have then
	 * we'll allow them to be successfully "set" ...
	 */
	if (dcb_request != pi->dcb.supported)
		return 1;

	pi->dcb.supported = dcb_request;
	return 0;
}

static int cxgb4_getpeer_app(struct net_device *dev,
			     struct dcb_peer_app_info *info, u16 *app_count)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int i, err = 0;

	if (!cxgb4_dcb_state_synced(pi->dcb.state))
		return 1;

	info->willing = 0;
	info->error = 0;

	*app_count = 0;
	for (i = 0; i < CXGB4_MAX_DCBX_APP_SUPPORTED; i++) {
		INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);
		pcmd.u.dcb.app_priority.type = FW_PORT_DCB_TYPE_APP_ID;
		pcmd.u.dcb.app_priority.idx = *app_count;
		err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);

		if (err != FW_PORT_DCB_CFG_SUCCESS) {
			dev_err(adap->pdev_dev, "DCB app table read failed with %d\n",
				-err);
			return err;
		}

		/* find first empty slot */
		if (!pcmd.u.dcb.app_priority.protocolid)
			break;
	}
	*app_count = i;
	return err;
}

static int cxgb4_getpeerapp_tbl(struct net_device *dev, struct dcb_app *table)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	int i, err = 0;

	if (!cxgb4_dcb_state_synced(pi->dcb.state))
		return 1;

	for (i = 0; i < CXGB4_MAX_DCBX_APP_SUPPORTED; i++) {
		INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);
		pcmd.u.dcb.app_priority.type = FW_PORT_DCB_TYPE_APP_ID;
		pcmd.u.dcb.app_priority.idx = i;
		err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);

		if (err != FW_PORT_DCB_CFG_SUCCESS) {
			dev_err(adap->pdev_dev, "DCB app table read failed with %d\n",
				-err);
			return err;
		}

		/* find first empty slot */
		if (!pcmd.u.dcb.app_priority.protocolid)
			break;

		table[i].selector = (pcmd.u.dcb.app_priority.sel_field + 1);
		table[i].protocol =
			be16_to_cpu(pcmd.u.dcb.app_priority.protocolid);
		table[i].priority =
			ffs(pcmd.u.dcb.app_priority.user_prio_map) - 1;
	}
	return err;
}

/* Return Priority Group information.
 */
static int cxgb4_cee_peer_getpg(struct net_device *dev, struct cee_pg *pg)
{
	struct fw_port_cmd pcmd;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = pi->adapter;
	u32 pgid;
	int i, err;

	/* We're always "willing" -- the Switch Fabric always dictates the
	 * DCBX parameters to us.
	 */
	pg->willing = true;

	INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);
	pcmd.u.dcb.pgid.type = FW_PORT_DCB_TYPE_PGID;
	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGID failed with %d\n", -err);
		return err;
	}
	pgid = be32_to_cpu(pcmd.u.dcb.pgid.pgid);

	for (i = 0; i < CXGB4_MAX_PRIORITY; i++)
		pg->prio_pg[7 - i] = (pgid >> (i * 4)) & 0xF;

	INIT_PORT_DCB_READ_PEER_CMD(pcmd, pi->port_id);
	pcmd.u.dcb.pgrate.type = FW_PORT_DCB_TYPE_PGRATE;
	err = t4_wr_mbox(adap, adap->mbox, &pcmd, sizeof(pcmd), &pcmd);
	if (err != FW_PORT_DCB_CFG_SUCCESS) {
		dev_err(adap->pdev_dev, "DCB read PGRATE failed with %d\n",
			-err);
		return err;
	}

	for (i = 0; i < CXGB4_MAX_PRIORITY; i++)
		pg->pg_bw[i] = pcmd.u.dcb.pgrate.pgrate[i];

	pg->tcs_supported = pcmd.u.dcb.pgrate.num_tcs_supported;

	return 0;
}

/* Return Priority Flow Control information.
 */
static int cxgb4_cee_peer_getpfc(struct net_device *dev, struct cee_pfc *pfc)
{
	struct port_info *pi = netdev2pinfo(dev);

	cxgb4_getnumtcs(dev, DCB_NUMTCS_ATTR_PFC, &(pfc->tcs_supported));

	/* Firmware sends this to us in a formwat that is a bit flipped version
	 * of spec, correct it before we send it to host. This is taken care of
	 * by bit shifting in other uses of pfcen
	 */
	pfc->pfc_en = bitswap_1(pi->dcb.pfcen);

	pfc->tcs_supported = pi->dcb.pfc_num_tcs_supported;

	return 0;
}

const struct dcbnl_rtnl_ops cxgb4_dcb_ops = {
	.ieee_getets		= cxgb4_ieee_get_ets,
	.ieee_getpfc		= cxgb4_ieee_get_pfc,
	.ieee_getapp		= cxgb4_ieee_getapp,
	.ieee_setapp		= cxgb4_ieee_setapp,
	.ieee_peer_getets	= cxgb4_ieee_peer_ets,
	.ieee_peer_getpfc	= cxgb4_ieee_get_pfc,

	/* CEE std */
	.getstate		= cxgb4_getstate,
	.setstate		= cxgb4_setstate,
	.getpgtccfgtx		= cxgb4_getpgtccfg_tx,
	.getpgbwgcfgtx		= cxgb4_getpgbwgcfg_tx,
	.getpgtccfgrx		= cxgb4_getpgtccfg_rx,
	.getpgbwgcfgrx		= cxgb4_getpgbwgcfg_rx,
	.setpgtccfgtx		= cxgb4_setpgtccfg_tx,
	.setpgbwgcfgtx		= cxgb4_setpgbwgcfg_tx,
	.setpfccfg		= cxgb4_setpfccfg,
	.getpfccfg		= cxgb4_getpfccfg,
	.setall			= cxgb4_setall,
	.getcap			= cxgb4_getcap,
	.getnumtcs		= cxgb4_getnumtcs,
	.setnumtcs		= cxgb4_setnumtcs,
	.getpfcstate		= cxgb4_getpfcstate,
	.setpfcstate		= cxgb4_setpfcstate,
	.getapp			= cxgb4_getapp,
	.setapp			= cxgb4_setapp,

	/* DCBX configuration */
	.getdcbx		= cxgb4_getdcbx,
	.setdcbx		= cxgb4_setdcbx,

	/* peer apps */
	.peer_getappinfo	= cxgb4_getpeer_app,
	.peer_getapptable	= cxgb4_getpeerapp_tbl,

	/* CEE peer */
	.cee_peer_getpg		= cxgb4_cee_peer_getpg,
	.cee_peer_getpfc	= cxgb4_cee_peer_getpfc,
};
