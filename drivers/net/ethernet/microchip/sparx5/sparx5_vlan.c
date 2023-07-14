// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_main_regs.h"
#include "sparx5_main.h"

static int sparx5_vlant_set_mask(struct sparx5 *sparx5, u16 vid)
{
	u32 mask[3];

	/* Divide up mask in 32 bit words */
	bitmap_to_arr32(mask, sparx5->vlan_mask[vid], SPX5_PORTS);

	/* Output mask to respective registers */
	spx5_wr(mask[0], sparx5, ANA_L3_VLAN_MASK_CFG(vid));
	spx5_wr(mask[1], sparx5, ANA_L3_VLAN_MASK_CFG1(vid));
	spx5_wr(mask[2], sparx5, ANA_L3_VLAN_MASK_CFG2(vid));

	return 0;
}

void sparx5_vlan_init(struct sparx5 *sparx5)
{
	u16 vid;

	spx5_rmw(ANA_L3_VLAN_CTRL_VLAN_ENA_SET(1),
		 ANA_L3_VLAN_CTRL_VLAN_ENA,
		 sparx5,
		 ANA_L3_VLAN_CTRL);

	/* Map VLAN = FID */
	for (vid = NULL_VID; vid < VLAN_N_VID; vid++)
		spx5_rmw(ANA_L3_VLAN_CFG_VLAN_FID_SET(vid),
			 ANA_L3_VLAN_CFG_VLAN_FID,
			 sparx5,
			 ANA_L3_VLAN_CFG(vid));
}

void sparx5_vlan_port_setup(struct sparx5 *sparx5, int portno)
{
	struct sparx5_port *port = sparx5->ports[portno];

	/* Configure PVID */
	spx5_rmw(ANA_CL_VLAN_CTRL_VLAN_AWARE_ENA_SET(0) |
		 ANA_CL_VLAN_CTRL_PORT_VID_SET(port->pvid),
		 ANA_CL_VLAN_CTRL_VLAN_AWARE_ENA |
		 ANA_CL_VLAN_CTRL_PORT_VID,
		 sparx5,
		 ANA_CL_VLAN_CTRL(port->portno));
}

int sparx5_vlan_vid_add(struct sparx5_port *port, u16 vid, bool pvid,
			bool untagged)
{
	struct sparx5 *sparx5 = port->sparx5;
	int ret;

	/* Untagged egress vlan classification */
	if (untagged && port->vid != vid) {
		if (port->vid) {
			netdev_err(port->ndev,
				   "Port already has a native VLAN: %d\n",
				   port->vid);
			return -EBUSY;
		}
		port->vid = vid;
	}

	/* Make the port a member of the VLAN */
	set_bit(port->portno, sparx5->vlan_mask[vid]);
	ret = sparx5_vlant_set_mask(sparx5, vid);
	if (ret)
		return ret;

	/* Default ingress vlan classification */
	if (pvid)
		port->pvid = vid;

	sparx5_vlan_port_apply(sparx5, port);

	return 0;
}

int sparx5_vlan_vid_del(struct sparx5_port *port, u16 vid)
{
	struct sparx5 *sparx5 = port->sparx5;
	int ret;

	/* 8021q removes VID 0 on module unload for all interfaces
	 * with VLAN filtering feature. We need to keep it to receive
	 * untagged traffic.
	 */
	if (vid == 0)
		return 0;

	/* Stop the port from being a member of the vlan */
	clear_bit(port->portno, sparx5->vlan_mask[vid]);
	ret = sparx5_vlant_set_mask(sparx5, vid);
	if (ret)
		return ret;

	/* Ingress */
	if (port->pvid == vid)
		port->pvid = 0;

	/* Egress */
	if (port->vid == vid)
		port->vid = 0;

	sparx5_vlan_port_apply(sparx5, port);

	return 0;
}

void sparx5_pgid_update_mask(struct sparx5_port *port, int pgid, bool enable)
{
	struct sparx5 *sparx5 = port->sparx5;
	u32 val, mask;

	/* mask is spread across 3 registers x 32 bit */
	if (port->portno < 32) {
		mask = BIT(port->portno);
		val = enable ? mask : 0;
		spx5_rmw(val, mask, sparx5, ANA_AC_PGID_CFG(pgid));
	} else if (port->portno < 64) {
		mask = BIT(port->portno - 32);
		val = enable ? mask : 0;
		spx5_rmw(val, mask, sparx5, ANA_AC_PGID_CFG1(pgid));
	} else if (port->portno < SPX5_PORTS) {
		mask = BIT(port->portno - 64);
		val = enable ? mask : 0;
		spx5_rmw(val, mask, sparx5, ANA_AC_PGID_CFG2(pgid));
	} else {
		netdev_err(port->ndev, "Invalid port no: %d\n", port->portno);
	}
}

void sparx5_pgid_clear(struct sparx5 *spx5, int pgid)
{
	spx5_wr(0, spx5, ANA_AC_PGID_CFG(pgid));
	spx5_wr(0, spx5, ANA_AC_PGID_CFG1(pgid));
	spx5_wr(0, spx5, ANA_AC_PGID_CFG2(pgid));
}

void sparx5_pgid_read_mask(struct sparx5 *spx5, int pgid, u32 portmask[3])
{
	portmask[0] = spx5_rd(spx5, ANA_AC_PGID_CFG(pgid));
	portmask[1] = spx5_rd(spx5, ANA_AC_PGID_CFG1(pgid));
	portmask[2] = spx5_rd(spx5, ANA_AC_PGID_CFG2(pgid));
}

void sparx5_update_fwd(struct sparx5 *sparx5)
{
	DECLARE_BITMAP(workmask, SPX5_PORTS);
	u32 mask[3];
	int port;

	/* Divide up fwd mask in 32 bit words */
	bitmap_to_arr32(mask, sparx5->bridge_fwd_mask, SPX5_PORTS);

	/* Update flood masks */
	for (port = PGID_UC_FLOOD; port <= PGID_BCAST; port++) {
		spx5_wr(mask[0], sparx5, ANA_AC_PGID_CFG(port));
		spx5_wr(mask[1], sparx5, ANA_AC_PGID_CFG1(port));
		spx5_wr(mask[2], sparx5, ANA_AC_PGID_CFG2(port));
	}

	/* Update SRC masks */
	for (port = 0; port < SPX5_PORTS; port++) {
		if (test_bit(port, sparx5->bridge_fwd_mask)) {
			/* Allow to send to all bridged but self */
			bitmap_copy(workmask, sparx5->bridge_fwd_mask, SPX5_PORTS);
			clear_bit(port, workmask);
			bitmap_to_arr32(mask, workmask, SPX5_PORTS);
			spx5_wr(mask[0], sparx5, ANA_AC_SRC_CFG(port));
			spx5_wr(mask[1], sparx5, ANA_AC_SRC_CFG1(port));
			spx5_wr(mask[2], sparx5, ANA_AC_SRC_CFG2(port));
		} else {
			spx5_wr(0, sparx5, ANA_AC_SRC_CFG(port));
			spx5_wr(0, sparx5, ANA_AC_SRC_CFG1(port));
			spx5_wr(0, sparx5, ANA_AC_SRC_CFG2(port));
		}
	}

	/* Learning enabled only for bridged ports */
	bitmap_and(workmask, sparx5->bridge_fwd_mask,
		   sparx5->bridge_lrn_mask, SPX5_PORTS);
	bitmap_to_arr32(mask, workmask, SPX5_PORTS);

	/* Apply learning mask */
	spx5_wr(mask[0], sparx5, ANA_L2_AUTO_LRN_CFG);
	spx5_wr(mask[1], sparx5, ANA_L2_AUTO_LRN_CFG1);
	spx5_wr(mask[2], sparx5, ANA_L2_AUTO_LRN_CFG2);
}

void sparx5_vlan_port_apply(struct sparx5 *sparx5,
			    struct sparx5_port *port)

{
	u32 val;

	/* Configure PVID, vlan aware */
	val = ANA_CL_VLAN_CTRL_VLAN_AWARE_ENA_SET(port->vlan_aware) |
		ANA_CL_VLAN_CTRL_VLAN_POP_CNT_SET(port->vlan_aware) |
		ANA_CL_VLAN_CTRL_PORT_VID_SET(port->pvid);
	spx5_wr(val, sparx5, ANA_CL_VLAN_CTRL(port->portno));

	val = 0;
	if (port->vlan_aware && !port->pvid)
		/* If port is vlan-aware and tagged, drop untagged and
		 * priority tagged frames.
		 */
		val = ANA_CL_VLAN_FILTER_CTRL_TAG_REQUIRED_ENA_SET(1) |
			ANA_CL_VLAN_FILTER_CTRL_PRIO_CTAG_DIS_SET(1) |
			ANA_CL_VLAN_FILTER_CTRL_PRIO_STAG_DIS_SET(1);
	spx5_wr(val, sparx5,
		ANA_CL_VLAN_FILTER_CTRL(port->portno, 0));

	/* Egress configuration (REW_TAG_CFG): VLAN tag selected via IFH */
	val = REW_TAG_CTRL_TAG_TPID_CFG_SET(5);
	if (port->vlan_aware) {
		if (port->vid)
			/* Tag all frames except when VID == DEFAULT_VLAN */
			val |= REW_TAG_CTRL_TAG_CFG_SET(1);
		else
			val |= REW_TAG_CTRL_TAG_CFG_SET(3);
	}
	spx5_wr(val, sparx5, REW_TAG_CTRL(port->portno));

	/* Egress VID */
	spx5_rmw(REW_PORT_VLAN_CFG_PORT_VID_SET(port->vid),
		 REW_PORT_VLAN_CFG_PORT_VID,
		 sparx5,
		 REW_PORT_VLAN_CFG(port->portno));
}
