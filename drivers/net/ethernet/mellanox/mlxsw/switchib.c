// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <net/switchdev.h>

#include "pci.h"
#include "core.h"
#include "reg.h"
#include "port.h"
#include "trap.h"
#include "txheader.h"
#include "ib.h"

static const char mlxsw_sib_driver_name[] = "mlxsw_switchib";
static const char mlxsw_sib2_driver_name[] = "mlxsw_switchib2";

struct mlxsw_sib_port;

struct mlxsw_sib {
	struct mlxsw_sib_port **ports;
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	u8 hw_id[ETH_ALEN];
};

struct mlxsw_sib_port {
	struct mlxsw_sib *mlxsw_sib;
	u8 local_port;
	struct {
		u8 module;
	} mapping;
};

/* tx_v1_hdr_version
 * Tx header version.
 * Must be set to 1.
 */
MLXSW_ITEM32(tx_v1, hdr, version, 0x00, 28, 4);

/* tx_v1_hdr_ctl
 * Packet control type.
 * 0 - Ethernet control (e.g. EMADs, LACP)
 * 1 - Ethernet data
 */
MLXSW_ITEM32(tx_v1, hdr, ctl, 0x00, 26, 2);

/* tx_v1_hdr_proto
 * Packet protocol type. Must be set to 1 (Ethernet).
 */
MLXSW_ITEM32(tx_v1, hdr, proto, 0x00, 21, 3);

/* tx_v1_hdr_swid
 * Switch partition ID. Must be set to 0.
 */
MLXSW_ITEM32(tx_v1, hdr, swid, 0x00, 12, 3);

/* tx_v1_hdr_control_tclass
 * Indicates if the packet should use the control TClass and not one
 * of the data TClasses.
 */
MLXSW_ITEM32(tx_v1, hdr, control_tclass, 0x00, 6, 1);

/* tx_v1_hdr_port_mid
 * Destination local port for unicast packets.
 * Destination multicast ID for multicast packets.
 *
 * Control packets are directed to a specific egress port, while data
 * packets are transmitted through the CPU port (0) into the switch partition,
 * where forwarding rules are applied.
 */
MLXSW_ITEM32(tx_v1, hdr, port_mid, 0x04, 16, 16);

/* tx_v1_hdr_type
 * 0 - Data packets
 * 6 - Control packets
 */
MLXSW_ITEM32(tx_v1, hdr, type, 0x0C, 0, 4);

static void
mlxsw_sib_tx_v1_hdr_construct(struct sk_buff *skb,
			      const struct mlxsw_tx_info *tx_info)
{
	char *txhdr = skb_push(skb, MLXSW_TXHDR_LEN);

	memset(txhdr, 0, MLXSW_TXHDR_LEN);

	mlxsw_tx_v1_hdr_version_set(txhdr, MLXSW_TXHDR_VERSION_1);
	mlxsw_tx_v1_hdr_ctl_set(txhdr, MLXSW_TXHDR_ETH_CTL);
	mlxsw_tx_v1_hdr_proto_set(txhdr, MLXSW_TXHDR_PROTO_ETH);
	mlxsw_tx_v1_hdr_swid_set(txhdr, 0);
	mlxsw_tx_v1_hdr_control_tclass_set(txhdr, 1);
	mlxsw_tx_v1_hdr_port_mid_set(txhdr, tx_info->local_port);
	mlxsw_tx_v1_hdr_type_set(txhdr, MLXSW_TXHDR_TYPE_CONTROL);
}

static int mlxsw_sib_hw_id_get(struct mlxsw_sib *mlxsw_sib)
{
	char spad_pl[MLXSW_REG_SPAD_LEN] = {0};
	int err;

	err = mlxsw_reg_query(mlxsw_sib->core, MLXSW_REG(spad), spad_pl);
	if (err)
		return err;
	mlxsw_reg_spad_base_mac_memcpy_from(spad_pl, mlxsw_sib->hw_id);
	return 0;
}

static int
mlxsw_sib_port_admin_status_set(struct mlxsw_sib_port *mlxsw_sib_port,
				bool is_up)
{
	struct mlxsw_sib *mlxsw_sib = mlxsw_sib_port->mlxsw_sib;
	char paos_pl[MLXSW_REG_PAOS_LEN];

	mlxsw_reg_paos_pack(paos_pl, mlxsw_sib_port->local_port,
			    is_up ? MLXSW_PORT_ADMIN_STATUS_UP :
			    MLXSW_PORT_ADMIN_STATUS_DOWN);
	return mlxsw_reg_write(mlxsw_sib->core, MLXSW_REG(paos), paos_pl);
}

static int mlxsw_sib_port_mtu_set(struct mlxsw_sib_port *mlxsw_sib_port,
				  u16 mtu)
{
	struct mlxsw_sib *mlxsw_sib = mlxsw_sib_port->mlxsw_sib;
	char pmtu_pl[MLXSW_REG_PMTU_LEN];
	int max_mtu;
	int err;

	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sib_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sib->core, MLXSW_REG(pmtu), pmtu_pl);
	if (err)
		return err;
	max_mtu = mlxsw_reg_pmtu_max_mtu_get(pmtu_pl);

	if (mtu > max_mtu)
		return -EINVAL;

	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sib_port->local_port, mtu);
	return mlxsw_reg_write(mlxsw_sib->core, MLXSW_REG(pmtu), pmtu_pl);
}

static int mlxsw_sib_port_set(struct mlxsw_sib_port *mlxsw_sib_port, u8 port)
{
	struct mlxsw_sib *mlxsw_sib = mlxsw_sib_port->mlxsw_sib;
	char plib_pl[MLXSW_REG_PLIB_LEN] = {0};
	int err;

	mlxsw_reg_plib_local_port_set(plib_pl, mlxsw_sib_port->local_port);
	mlxsw_reg_plib_ib_port_set(plib_pl, port);
	err = mlxsw_reg_write(mlxsw_sib->core, MLXSW_REG(plib), plib_pl);
	return err;
}

static int mlxsw_sib_port_swid_set(struct mlxsw_sib_port *mlxsw_sib_port,
				   u8 swid)
{
	struct mlxsw_sib *mlxsw_sib = mlxsw_sib_port->mlxsw_sib;
	char pspa_pl[MLXSW_REG_PSPA_LEN];

	mlxsw_reg_pspa_pack(pspa_pl, swid, mlxsw_sib_port->local_port);
	return mlxsw_reg_write(mlxsw_sib->core, MLXSW_REG(pspa), pspa_pl);
}

static int mlxsw_sib_port_module_info_get(struct mlxsw_sib *mlxsw_sib,
					  u8 local_port, u8 *p_module,
					  u8 *p_width)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	int err;

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	err = mlxsw_reg_query(mlxsw_sib->core, MLXSW_REG(pmlp), pmlp_pl);
	if (err)
		return err;
	*p_module = mlxsw_reg_pmlp_module_get(pmlp_pl, 0);
	*p_width = mlxsw_reg_pmlp_width_get(pmlp_pl);
	return 0;
}

static int mlxsw_sib_port_speed_set(struct mlxsw_sib_port *mlxsw_sib_port,
				    u16 speed, u16 width)
{
	struct mlxsw_sib *mlxsw_sib = mlxsw_sib_port->mlxsw_sib;
	char ptys_pl[MLXSW_REG_PTYS_LEN];

	mlxsw_reg_ptys_ib_pack(ptys_pl, mlxsw_sib_port->local_port, speed,
			       width);
	return mlxsw_reg_write(mlxsw_sib->core, MLXSW_REG(ptys), ptys_pl);
}

static bool mlxsw_sib_port_created(struct mlxsw_sib *mlxsw_sib, u8 local_port)
{
	return mlxsw_sib->ports[local_port] != NULL;
}

static int __mlxsw_sib_port_create(struct mlxsw_sib *mlxsw_sib, u8 local_port,
				   u8 module, u8 width)
{
	struct mlxsw_sib_port *mlxsw_sib_port;
	int err;

	mlxsw_sib_port = kzalloc(sizeof(*mlxsw_sib_port), GFP_KERNEL);
	if (!mlxsw_sib_port)
		return -ENOMEM;
	mlxsw_sib_port->mlxsw_sib = mlxsw_sib;
	mlxsw_sib_port->local_port = local_port;
	mlxsw_sib_port->mapping.module = module;

	err = mlxsw_sib_port_swid_set(mlxsw_sib_port, 0);
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Port %d: Failed to set SWID\n",
			mlxsw_sib_port->local_port);
		goto err_port_swid_set;
	}

	/* Expose the IB port number as it's front panel name */
	err = mlxsw_sib_port_set(mlxsw_sib_port, module + 1);
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Port %d: Failed to set IB port\n",
			mlxsw_sib_port->local_port);
		goto err_port_ib_set;
	}

	/* Supports all speeds from SDR to FDR (bitmask) and support bus width
	 * of 1x, 2x and 4x (3 bits bitmask)
	 */
	err = mlxsw_sib_port_speed_set(mlxsw_sib_port,
				       MLXSW_REG_PTYS_IB_SPEED_EDR - 1,
				       BIT(3) - 1);
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Port %d: Failed to set speed\n",
			mlxsw_sib_port->local_port);
		goto err_port_speed_set;
	}

	/* Change to the maximum MTU the device supports, the SMA will take
	 * care of the active MTU
	 */
	err = mlxsw_sib_port_mtu_set(mlxsw_sib_port, MLXSW_IB_DEFAULT_MTU);
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Port %d: Failed to set MTU\n",
			mlxsw_sib_port->local_port);
		goto err_port_mtu_set;
	}

	err = mlxsw_sib_port_admin_status_set(mlxsw_sib_port, true);
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Port %d: Failed to change admin state to UP\n",
			mlxsw_sib_port->local_port);
		goto err_port_admin_set;
	}

	mlxsw_core_port_ib_set(mlxsw_sib->core, mlxsw_sib_port->local_port,
			       mlxsw_sib_port);
	mlxsw_sib->ports[local_port] = mlxsw_sib_port;
	return 0;

err_port_admin_set:
err_port_mtu_set:
err_port_speed_set:
err_port_ib_set:
	mlxsw_sib_port_swid_set(mlxsw_sib_port, MLXSW_PORT_SWID_DISABLED_PORT);
err_port_swid_set:
	kfree(mlxsw_sib_port);
	return err;
}

static int mlxsw_sib_port_create(struct mlxsw_sib *mlxsw_sib, u8 local_port,
				 u8 module, u8 width)
{
	int err;

	err = mlxsw_core_port_init(mlxsw_sib->core, local_port,
				   module + 1, false, 0,
				   mlxsw_sib->hw_id, sizeof(mlxsw_sib->hw_id));
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Port %d: Failed to init core port\n",
			local_port);
		return err;
	}
	err = __mlxsw_sib_port_create(mlxsw_sib, local_port, module, width);
	if (err)
		goto err_port_create;

	return 0;

err_port_create:
	mlxsw_core_port_fini(mlxsw_sib->core, local_port);
	return err;
}

static void __mlxsw_sib_port_remove(struct mlxsw_sib *mlxsw_sib, u8 local_port)
{
	struct mlxsw_sib_port *mlxsw_sib_port = mlxsw_sib->ports[local_port];

	mlxsw_core_port_clear(mlxsw_sib->core, local_port, mlxsw_sib);
	mlxsw_sib->ports[local_port] = NULL;
	mlxsw_sib_port_admin_status_set(mlxsw_sib_port, false);
	mlxsw_sib_port_swid_set(mlxsw_sib_port, MLXSW_PORT_SWID_DISABLED_PORT);
	kfree(mlxsw_sib_port);
}

static void mlxsw_sib_port_remove(struct mlxsw_sib *mlxsw_sib, u8 local_port)
{
	__mlxsw_sib_port_remove(mlxsw_sib, local_port);
	mlxsw_core_port_fini(mlxsw_sib->core, local_port);
}

static void mlxsw_sib_ports_remove(struct mlxsw_sib *mlxsw_sib)
{
	int i;

	for (i = 1; i < MLXSW_PORT_MAX_IB_PORTS; i++)
		if (mlxsw_sib_port_created(mlxsw_sib, i))
			mlxsw_sib_port_remove(mlxsw_sib, i);
	kfree(mlxsw_sib->ports);
}

static int mlxsw_sib_ports_create(struct mlxsw_sib *mlxsw_sib)
{
	size_t alloc_size;
	u8 module, width;
	int i;
	int err;

	alloc_size = sizeof(struct mlxsw_sib_port *) * MLXSW_PORT_MAX_IB_PORTS;
	mlxsw_sib->ports = kzalloc(alloc_size, GFP_KERNEL);
	if (!mlxsw_sib->ports)
		return -ENOMEM;

	for (i = 1; i < MLXSW_PORT_MAX_IB_PORTS; i++) {
		err = mlxsw_sib_port_module_info_get(mlxsw_sib, i, &module,
						     &width);
		if (err)
			goto err_port_module_info_get;
		if (!width)
			continue;
		err = mlxsw_sib_port_create(mlxsw_sib, i, module, width);
		if (err)
			goto err_port_create;
	}
	return 0;

err_port_create:
err_port_module_info_get:
	for (i--; i >= 1; i--)
		if (mlxsw_sib_port_created(mlxsw_sib, i))
			mlxsw_sib_port_remove(mlxsw_sib, i);
	kfree(mlxsw_sib->ports);
	return err;
}

static void
mlxsw_sib_pude_ib_event_func(struct mlxsw_sib_port *mlxsw_sib_port,
			     enum mlxsw_reg_pude_oper_status status)
{
	if (status == MLXSW_PORT_OPER_STATUS_UP)
		pr_info("ib link for port %d - up\n",
			mlxsw_sib_port->mapping.module + 1);
	else
		pr_info("ib link for port %d - down\n",
			mlxsw_sib_port->mapping.module + 1);
}

static void mlxsw_sib_pude_event_func(const struct mlxsw_reg_info *reg,
				      char *pude_pl, void *priv)
{
	struct mlxsw_sib *mlxsw_sib = priv;
	struct mlxsw_sib_port *mlxsw_sib_port;
	enum mlxsw_reg_pude_oper_status status;
	u8 local_port;

	local_port = mlxsw_reg_pude_local_port_get(pude_pl);
	mlxsw_sib_port = mlxsw_sib->ports[local_port];
	if (!mlxsw_sib_port) {
		dev_warn(mlxsw_sib->bus_info->dev, "Port %d: Link event received for non-existent port\n",
			 local_port);
		return;
	}

	status = mlxsw_reg_pude_oper_status_get(pude_pl);
	mlxsw_sib_pude_ib_event_func(mlxsw_sib_port, status);
}

static const struct mlxsw_listener mlxsw_sib_listener[] = {
	MLXSW_EVENTL(mlxsw_sib_pude_event_func, PUDE, EMAD),
};

static int mlxsw_sib_taps_init(struct mlxsw_sib *mlxsw_sib)
{
	int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sib_listener); i++) {
		err = mlxsw_core_trap_register(mlxsw_sib->core,
					       &mlxsw_sib_listener[i],
					       mlxsw_sib);
		if (err)
			goto err_rx_listener_register;
	}

	return 0;

err_rx_listener_register:
	for (i--; i >= 0; i--) {
		mlxsw_core_trap_unregister(mlxsw_sib->core,
					   &mlxsw_sib_listener[i],
					   mlxsw_sib);
	}

	return err;
}

static void mlxsw_sib_traps_fini(struct mlxsw_sib *mlxsw_sib)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sib_listener); i++) {
		mlxsw_core_trap_unregister(mlxsw_sib->core,
					   &mlxsw_sib_listener[i], mlxsw_sib);
	}
}

static int mlxsw_sib_basic_trap_groups_set(struct mlxsw_core *mlxsw_core)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_EMAD,
			    MLXSW_REG_HTGT_INVALID_POLICER,
			    MLXSW_REG_HTGT_DEFAULT_PRIORITY,
			    MLXSW_REG_HTGT_DEFAULT_TC);
	mlxsw_reg_htgt_swid_set(htgt_pl, MLXSW_PORT_SWID_ALL_SWIDS);
	mlxsw_reg_htgt_local_path_rdq_set(htgt_pl,
					MLXSW_REG_HTGT_LOCAL_PATH_RDQ_SIB_EMAD);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(htgt), htgt_pl);
}

static int mlxsw_sib_init(struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_bus_info *mlxsw_bus_info,
			  struct netlink_ext_ack *extack)
{
	struct mlxsw_sib *mlxsw_sib = mlxsw_core_driver_priv(mlxsw_core);
	int err;

	mlxsw_sib->core = mlxsw_core;
	mlxsw_sib->bus_info = mlxsw_bus_info;

	err = mlxsw_sib_hw_id_get(mlxsw_sib);
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Failed to get switch HW ID\n");
		return err;
	}

	err = mlxsw_sib_ports_create(mlxsw_sib);
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Failed to create ports\n");
		return err;
	}

	err = mlxsw_sib_taps_init(mlxsw_sib);
	if (err) {
		dev_err(mlxsw_sib->bus_info->dev, "Failed to set traps\n");
		goto err_traps_init_err;
	}

	return 0;

err_traps_init_err:
	mlxsw_sib_ports_remove(mlxsw_sib);
	return err;
}

static void mlxsw_sib_fini(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_sib *mlxsw_sib = mlxsw_core_driver_priv(mlxsw_core);

	mlxsw_sib_traps_fini(mlxsw_sib);
	mlxsw_sib_ports_remove(mlxsw_sib);
}

static const struct mlxsw_config_profile mlxsw_sib_config_profile = {
	.used_max_system_port		= 1,
	.max_system_port		= 48000,
	.used_max_ib_mc			= 1,
	.max_ib_mc			= 27,
	.used_max_pkey			= 1,
	.max_pkey			= 32,
	.swid_config			= {
		{
			.used_type	= 1,
			.type		= MLXSW_PORT_SWID_TYPE_IB,
		}
	},
};

static struct mlxsw_driver mlxsw_sib_driver = {
	.kind			= mlxsw_sib_driver_name,
	.priv_size		= sizeof(struct mlxsw_sib),
	.init			= mlxsw_sib_init,
	.fini			= mlxsw_sib_fini,
	.basic_trap_groups_set	= mlxsw_sib_basic_trap_groups_set,
	.txhdr_construct	= mlxsw_sib_tx_v1_hdr_construct,
	.txhdr_len		= MLXSW_TXHDR_LEN,
	.profile		= &mlxsw_sib_config_profile,
};

static struct mlxsw_driver mlxsw_sib2_driver = {
	.kind			= mlxsw_sib2_driver_name,
	.priv_size		= sizeof(struct mlxsw_sib),
	.init			= mlxsw_sib_init,
	.fini			= mlxsw_sib_fini,
	.basic_trap_groups_set	= mlxsw_sib_basic_trap_groups_set,
	.txhdr_construct	= mlxsw_sib_tx_v1_hdr_construct,
	.txhdr_len		= MLXSW_TXHDR_LEN,
	.profile		= &mlxsw_sib_config_profile,
};

static const struct pci_device_id mlxsw_sib_pci_id_table[] = {
	{PCI_VDEVICE(MELLANOX, PCI_DEVICE_ID_MELLANOX_SWITCHIB), 0},
	{0, },
};

static struct pci_driver mlxsw_sib_pci_driver = {
	.name = mlxsw_sib_driver_name,
	.id_table = mlxsw_sib_pci_id_table,
};

static const struct pci_device_id mlxsw_sib2_pci_id_table[] = {
	{PCI_VDEVICE(MELLANOX, PCI_DEVICE_ID_MELLANOX_SWITCHIB2), 0},
	{0, },
};

static struct pci_driver mlxsw_sib2_pci_driver = {
	.name = mlxsw_sib2_driver_name,
	.id_table = mlxsw_sib2_pci_id_table,
};

static int __init mlxsw_sib_module_init(void)
{
	int err;

	err = mlxsw_core_driver_register(&mlxsw_sib_driver);
	if (err)
		return err;

	err = mlxsw_core_driver_register(&mlxsw_sib2_driver);
	if (err)
		goto err_sib2_driver_register;

	err = mlxsw_pci_driver_register(&mlxsw_sib_pci_driver);
	if (err)
		goto err_sib_pci_driver_register;

	err = mlxsw_pci_driver_register(&mlxsw_sib2_pci_driver);
	if (err)
		goto err_sib2_pci_driver_register;

	return 0;

err_sib2_pci_driver_register:
	mlxsw_pci_driver_unregister(&mlxsw_sib_pci_driver);
err_sib_pci_driver_register:
	mlxsw_core_driver_unregister(&mlxsw_sib2_driver);
err_sib2_driver_register:
	mlxsw_core_driver_unregister(&mlxsw_sib_driver);
	return err;
}

static void __exit mlxsw_sib_module_exit(void)
{
	mlxsw_pci_driver_unregister(&mlxsw_sib2_pci_driver);
	mlxsw_pci_driver_unregister(&mlxsw_sib_pci_driver);
	mlxsw_core_driver_unregister(&mlxsw_sib2_driver);
	mlxsw_core_driver_unregister(&mlxsw_sib_driver);
}

module_init(mlxsw_sib_module_init);
module_exit(mlxsw_sib_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Elad Raz <eladr@@mellanox.com>");
MODULE_DESCRIPTION("Mellanox SwitchIB and SwitchIB-2 driver");
MODULE_ALIAS("mlxsw_switchib2");
MODULE_DEVICE_TABLE(pci, mlxsw_sib_pci_id_table);
MODULE_DEVICE_TABLE(pci, mlxsw_sib2_pci_id_table);
