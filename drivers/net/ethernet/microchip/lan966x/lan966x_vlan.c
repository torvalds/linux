// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

#define VLANACCESS_CMD_IDLE		0
#define VLANACCESS_CMD_READ		1
#define VLANACCESS_CMD_WRITE		2
#define VLANACCESS_CMD_INIT		3

static int lan966x_vlan_get_status(struct lan966x *lan966x)
{
	return lan_rd(lan966x, ANA_VLANACCESS);
}

static int lan966x_vlan_wait_for_completion(struct lan966x *lan966x)
{
	u32 val;

	return readx_poll_timeout(lan966x_vlan_get_status,
		lan966x, val,
		(val & ANA_VLANACCESS_VLAN_TBL_CMD) ==
		VLANACCESS_CMD_IDLE,
		TABLE_UPDATE_SLEEP_US, TABLE_UPDATE_TIMEOUT_US);
}

static void lan966x_vlan_set_mask(struct lan966x *lan966x, u16 vid)
{
	u16 mask = lan966x->vlan_mask[vid];
	bool cpu_dis;

	cpu_dis = !(mask & BIT(CPU_PORT));

	/* Set flags and the VID to configure */
	lan_rmw(ANA_VLANTIDX_VLAN_PGID_CPU_DIS_SET(cpu_dis) |
		ANA_VLANTIDX_V_INDEX_SET(vid),
		ANA_VLANTIDX_VLAN_PGID_CPU_DIS |
		ANA_VLANTIDX_V_INDEX,
		lan966x, ANA_VLANTIDX);

	/* Set the vlan port members mask */
	lan_rmw(ANA_VLAN_PORT_MASK_VLAN_PORT_MASK_SET(mask),
		ANA_VLAN_PORT_MASK_VLAN_PORT_MASK,
		lan966x, ANA_VLAN_PORT_MASK);

	/* Issue a write command */
	lan_rmw(ANA_VLANACCESS_VLAN_TBL_CMD_SET(VLANACCESS_CMD_WRITE),
		ANA_VLANACCESS_VLAN_TBL_CMD,
		lan966x, ANA_VLANACCESS);

	if (lan966x_vlan_wait_for_completion(lan966x))
		dev_err(lan966x->dev, "Vlan set mask failed\n");
}

static void lan966x_vlan_port_add_vlan_mask(struct lan966x_port *port, u16 vid)
{
	struct lan966x *lan966x = port->lan966x;
	u8 p = port->chip_port;

	lan966x->vlan_mask[vid] |= BIT(p);
	lan966x_vlan_set_mask(lan966x, vid);
}

static void lan966x_vlan_port_del_vlan_mask(struct lan966x_port *port, u16 vid)
{
	struct lan966x *lan966x = port->lan966x;
	u8 p = port->chip_port;

	lan966x->vlan_mask[vid] &= ~BIT(p);
	lan966x_vlan_set_mask(lan966x, vid);
}

static bool lan966x_vlan_port_any_vlan_mask(struct lan966x *lan966x, u16 vid)
{
	return !!(lan966x->vlan_mask[vid] & ~BIT(CPU_PORT));
}

static void lan966x_vlan_cpu_add_vlan_mask(struct lan966x *lan966x, u16 vid)
{
	lan966x->vlan_mask[vid] |= BIT(CPU_PORT);
	lan966x_vlan_set_mask(lan966x, vid);
}

static void lan966x_vlan_cpu_del_vlan_mask(struct lan966x *lan966x, u16 vid)
{
	lan966x->vlan_mask[vid] &= ~BIT(CPU_PORT);
	lan966x_vlan_set_mask(lan966x, vid);
}

static void lan966x_vlan_cpu_add_cpu_vlan_mask(struct lan966x *lan966x, u16 vid)
{
	__set_bit(vid, lan966x->cpu_vlan_mask);
}

static void lan966x_vlan_cpu_del_cpu_vlan_mask(struct lan966x *lan966x, u16 vid)
{
	__clear_bit(vid, lan966x->cpu_vlan_mask);
}

bool lan966x_vlan_cpu_member_cpu_vlan_mask(struct lan966x *lan966x, u16 vid)
{
	return test_bit(vid, lan966x->cpu_vlan_mask);
}

static u16 lan966x_vlan_port_get_pvid(struct lan966x_port *port)
{
	struct lan966x *lan966x = port->lan966x;

	if (!(lan966x->bridge_mask & BIT(port->chip_port)))
		return HOST_PVID;

	return port->vlan_aware ? port->pvid : UNAWARE_PVID;
}

int lan966x_vlan_port_set_vid(struct lan966x_port *port, u16 vid,
			      bool pvid, bool untagged)
{
	struct lan966x *lan966x = port->lan966x;

	/* Egress vlan classification */
	if (untagged && port->vid != vid) {
		if (port->vid) {
			dev_err(lan966x->dev,
				"Port already has a native VLAN: %d\n",
				port->vid);
			return -EBUSY;
		}
		port->vid = vid;
	}

	/* Default ingress vlan classification */
	if (pvid)
		port->pvid = vid;

	return 0;
}

static void lan966x_vlan_port_remove_vid(struct lan966x_port *port, u16 vid)
{
	if (port->pvid == vid)
		port->pvid = 0;

	if (port->vid == vid)
		port->vid = 0;
}

void lan966x_vlan_port_set_vlan_aware(struct lan966x_port *port,
				      bool vlan_aware)
{
	port->vlan_aware = vlan_aware;
}

void lan966x_vlan_port_apply(struct lan966x_port *port)
{
	struct lan966x *lan966x = port->lan966x;
	u16 pvid;
	u32 val;

	pvid = lan966x_vlan_port_get_pvid(port);

	/* Ingress classification (ANA_PORT_VLAN_CFG) */
	/* Default vlan to classify for untagged frames (may be zero) */
	val = ANA_VLAN_CFG_VLAN_VID_SET(pvid);
	if (port->vlan_aware)
		val |= ANA_VLAN_CFG_VLAN_AWARE_ENA_SET(1) |
		       ANA_VLAN_CFG_VLAN_POP_CNT_SET(1);

	lan_rmw(val,
		ANA_VLAN_CFG_VLAN_VID | ANA_VLAN_CFG_VLAN_AWARE_ENA |
		ANA_VLAN_CFG_VLAN_POP_CNT,
		lan966x, ANA_VLAN_CFG(port->chip_port));

	lan_rmw(DEV_MAC_TAGS_CFG_VLAN_AWR_ENA_SET(port->vlan_aware) |
		DEV_MAC_TAGS_CFG_VLAN_DBL_AWR_ENA_SET(port->vlan_aware),
		DEV_MAC_TAGS_CFG_VLAN_AWR_ENA |
		DEV_MAC_TAGS_CFG_VLAN_DBL_AWR_ENA,
		lan966x, DEV_MAC_TAGS_CFG(port->chip_port));

	/* Drop frames with multicast source address */
	val = ANA_DROP_CFG_DROP_MC_SMAC_ENA_SET(1);
	if (port->vlan_aware && !pvid)
		/* If port is vlan-aware and tagged, drop untagged and priority
		 * tagged frames.
		 */
		val |= ANA_DROP_CFG_DROP_UNTAGGED_ENA_SET(1) |
		       ANA_DROP_CFG_DROP_PRIO_S_TAGGED_ENA_SET(1) |
		       ANA_DROP_CFG_DROP_PRIO_C_TAGGED_ENA_SET(1);

	lan_wr(val, lan966x, ANA_DROP_CFG(port->chip_port));

	/* Egress configuration (REW_TAG_CFG): VLAN tag type to 8021Q */
	val = REW_TAG_CFG_TAG_TPID_CFG_SET(0);
	if (port->vlan_aware) {
		if (port->vid)
			/* Tag all frames except when VID == DEFAULT_VLAN */
			val |= REW_TAG_CFG_TAG_CFG_SET(1);
		else
			val |= REW_TAG_CFG_TAG_CFG_SET(3);
	}

	/* Update only some bits in the register */
	lan_rmw(val,
		REW_TAG_CFG_TAG_TPID_CFG | REW_TAG_CFG_TAG_CFG,
		lan966x, REW_TAG_CFG(port->chip_port));

	/* Set default VLAN and tag type to 8021Q */
	lan_rmw(REW_PORT_VLAN_CFG_PORT_TPID_SET(ETH_P_8021Q) |
		REW_PORT_VLAN_CFG_PORT_VID_SET(port->vid),
		REW_PORT_VLAN_CFG_PORT_TPID |
		REW_PORT_VLAN_CFG_PORT_VID,
		lan966x, REW_PORT_VLAN_CFG(port->chip_port));
}

void lan966x_vlan_port_add_vlan(struct lan966x_port *port,
				u16 vid,
				bool pvid,
				bool untagged)
{
	struct lan966x *lan966x = port->lan966x;

	/* If the CPU(br) is already part of the vlan then add the fdb
	 * entries in MAC table to copy the frames to the CPU(br).
	 * If the CPU(br) is not part of the vlan then it would
	 * just drop the frames.
	 */
	if (lan966x_vlan_cpu_member_cpu_vlan_mask(lan966x, vid)) {
		lan966x_vlan_cpu_add_vlan_mask(lan966x, vid);
		lan966x_fdb_write_entries(lan966x, vid);
		lan966x_mdb_write_entries(lan966x, vid);
	}

	lan966x_vlan_port_set_vid(port, vid, pvid, untagged);
	lan966x_vlan_port_add_vlan_mask(port, vid);
	lan966x_vlan_port_apply(port);
}

void lan966x_vlan_port_del_vlan(struct lan966x_port *port, u16 vid)
{
	struct lan966x *lan966x = port->lan966x;

	lan966x_vlan_port_remove_vid(port, vid);
	lan966x_vlan_port_del_vlan_mask(port, vid);
	lan966x_vlan_port_apply(port);

	/* In case there are no other ports in vlan then remove the CPU from
	 * that vlan but still keep it in the mask because it may be needed
	 * again then another port gets added in that vlan
	 */
	if (!lan966x_vlan_port_any_vlan_mask(lan966x, vid)) {
		lan966x_vlan_cpu_del_vlan_mask(lan966x, vid);
		lan966x_fdb_erase_entries(lan966x, vid);
		lan966x_mdb_erase_entries(lan966x, vid);
	}
}

void lan966x_vlan_cpu_add_vlan(struct lan966x *lan966x, u16 vid)
{
	/* Add an entry in the MAC table for the CPU
	 * Add the CPU part of the vlan only if there is another port in that
	 * vlan otherwise all the broadcast frames in that vlan will go to CPU
	 * even if none of the ports are in the vlan and then the CPU will just
	 * need to discard these frames. It is required to store this
	 * information so when a front port is added then it would add also the
	 * CPU port.
	 */
	if (lan966x_vlan_port_any_vlan_mask(lan966x, vid)) {
		lan966x_vlan_cpu_add_vlan_mask(lan966x, vid);
		lan966x_mdb_write_entries(lan966x, vid);
	}

	lan966x_vlan_cpu_add_cpu_vlan_mask(lan966x, vid);
	lan966x_fdb_write_entries(lan966x, vid);
}

void lan966x_vlan_cpu_del_vlan(struct lan966x *lan966x, u16 vid)
{
	/* Remove the CPU part of the vlan */
	lan966x_vlan_cpu_del_cpu_vlan_mask(lan966x, vid);
	lan966x_vlan_cpu_del_vlan_mask(lan966x, vid);
	lan966x_fdb_erase_entries(lan966x, vid);
	lan966x_mdb_erase_entries(lan966x, vid);
}

void lan966x_vlan_init(struct lan966x *lan966x)
{
	u16 port, vid;

	/* Clear VLAN table, by default all ports are members of all VLANS */
	lan_rmw(ANA_VLANACCESS_VLAN_TBL_CMD_SET(VLANACCESS_CMD_INIT),
		ANA_VLANACCESS_VLAN_TBL_CMD,
		lan966x, ANA_VLANACCESS);
	lan966x_vlan_wait_for_completion(lan966x);

	for (vid = 1; vid < VLAN_N_VID; vid++) {
		lan966x->vlan_mask[vid] = 0;
		lan966x_vlan_set_mask(lan966x, vid);
	}

	/* Set all the ports + cpu to be part of HOST_PVID and UNAWARE_PVID */
	lan966x->vlan_mask[HOST_PVID] =
		GENMASK(lan966x->num_phys_ports - 1, 0) | BIT(CPU_PORT);
	lan966x_vlan_set_mask(lan966x, HOST_PVID);

	lan966x->vlan_mask[UNAWARE_PVID] =
		GENMASK(lan966x->num_phys_ports - 1, 0) | BIT(CPU_PORT);
	lan966x_vlan_set_mask(lan966x, UNAWARE_PVID);

	lan966x_vlan_cpu_add_cpu_vlan_mask(lan966x, UNAWARE_PVID);

	/* Configure the CPU port to be vlan aware */
	lan_wr(ANA_VLAN_CFG_VLAN_VID_SET(0) |
	       ANA_VLAN_CFG_VLAN_AWARE_ENA_SET(1) |
	       ANA_VLAN_CFG_VLAN_POP_CNT_SET(1),
	       lan966x, ANA_VLAN_CFG(CPU_PORT));

	/* Set vlan ingress filter mask to all ports */
	lan_wr(GENMASK(lan966x->num_phys_ports, 0),
	       lan966x, ANA_VLANMASK);

	for (port = 0; port < lan966x->num_phys_ports; port++) {
		lan_wr(0, lan966x, REW_PORT_VLAN_CFG(port));
		lan_wr(0, lan966x, REW_TAG_CFG(port));
	}
}
