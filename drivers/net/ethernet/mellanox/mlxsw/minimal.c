// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2016-2019 Mellanox Technologies. All rights reserved */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/types.h>

#include "core.h"
#include "core_env.h"
#include "i2c.h"

static const char mlxsw_m_driver_name[] = "mlxsw_minimal";

#define MLXSW_M_FWREV_MINOR	2000
#define MLXSW_M_FWREV_SUBMINOR	1886

static const struct mlxsw_fw_rev mlxsw_m_fw_rev = {
	.minor = MLXSW_M_FWREV_MINOR,
	.subminor = MLXSW_M_FWREV_SUBMINOR,
};

struct mlxsw_m_port;

struct mlxsw_m_line_card {
	bool active;
	int module_to_port[];
};

struct mlxsw_m {
	struct mlxsw_m_port **ports;
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	u8 base_mac[ETH_ALEN];
	u8 max_ports;
	u8 max_modules_per_slot; /* Maximum number of modules per-slot. */
	u8 num_of_slots; /* Including the main board. */
	struct mlxsw_m_line_card **line_cards;
};

struct mlxsw_m_port {
	struct net_device *dev;
	struct mlxsw_m *mlxsw_m;
	u16 local_port;
	u8 slot_index;
	u8 module;
	u8 module_offset;
};

static int mlxsw_m_base_mac_get(struct mlxsw_m *mlxsw_m)
{
	char spad_pl[MLXSW_REG_SPAD_LEN] = {0};
	int err;

	err = mlxsw_reg_query(mlxsw_m->core, MLXSW_REG(spad), spad_pl);
	if (err)
		return err;
	mlxsw_reg_spad_base_mac_memcpy_from(spad_pl, mlxsw_m->base_mac);
	return 0;
}

static int mlxsw_m_port_open(struct net_device *dev)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(dev);
	struct mlxsw_m *mlxsw_m = mlxsw_m_port->mlxsw_m;

	return mlxsw_env_module_port_up(mlxsw_m->core, 0,
					mlxsw_m_port->module);
}

static int mlxsw_m_port_stop(struct net_device *dev)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(dev);
	struct mlxsw_m *mlxsw_m = mlxsw_m_port->mlxsw_m;

	mlxsw_env_module_port_down(mlxsw_m->core, 0, mlxsw_m_port->module);
	return 0;
}

static struct devlink_port *
mlxsw_m_port_get_devlink_port(struct net_device *dev)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(dev);
	struct mlxsw_m *mlxsw_m = mlxsw_m_port->mlxsw_m;

	return mlxsw_core_port_devlink_port_get(mlxsw_m->core,
						mlxsw_m_port->local_port);
}

static const struct net_device_ops mlxsw_m_port_netdev_ops = {
	.ndo_open		= mlxsw_m_port_open,
	.ndo_stop		= mlxsw_m_port_stop,
	.ndo_get_devlink_port	= mlxsw_m_port_get_devlink_port,
};

static void mlxsw_m_module_get_drvinfo(struct net_device *dev,
				       struct ethtool_drvinfo *drvinfo)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(dev);
	struct mlxsw_m *mlxsw_m = mlxsw_m_port->mlxsw_m;

	strscpy(drvinfo->driver, mlxsw_m->bus_info->device_kind,
		sizeof(drvinfo->driver));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d",
		 mlxsw_m->bus_info->fw_rev.major,
		 mlxsw_m->bus_info->fw_rev.minor,
		 mlxsw_m->bus_info->fw_rev.subminor);
	strscpy(drvinfo->bus_info, mlxsw_m->bus_info->device_name,
		sizeof(drvinfo->bus_info));
}

static int mlxsw_m_get_module_info(struct net_device *netdev,
				   struct ethtool_modinfo *modinfo)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(netdev);
	struct mlxsw_core *core = mlxsw_m_port->mlxsw_m->core;

	return mlxsw_env_get_module_info(netdev, core,
					 mlxsw_m_port->slot_index,
					 mlxsw_m_port->module, modinfo);
}

static int
mlxsw_m_get_module_eeprom(struct net_device *netdev, struct ethtool_eeprom *ee,
			  u8 *data)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(netdev);
	struct mlxsw_core *core = mlxsw_m_port->mlxsw_m->core;

	return mlxsw_env_get_module_eeprom(netdev, core,
					   mlxsw_m_port->slot_index,
					   mlxsw_m_port->module, ee, data);
}

static int
mlxsw_m_get_module_eeprom_by_page(struct net_device *netdev,
				  const struct ethtool_module_eeprom *page,
				  struct netlink_ext_ack *extack)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(netdev);
	struct mlxsw_core *core = mlxsw_m_port->mlxsw_m->core;

	return mlxsw_env_get_module_eeprom_by_page(core,
						   mlxsw_m_port->slot_index,
						   mlxsw_m_port->module,
						   page, extack);
}

static int mlxsw_m_reset(struct net_device *netdev, u32 *flags)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(netdev);
	struct mlxsw_core *core = mlxsw_m_port->mlxsw_m->core;

	return mlxsw_env_reset_module(netdev, core, mlxsw_m_port->slot_index,
				      mlxsw_m_port->module,
				      flags);
}

static int
mlxsw_m_get_module_power_mode(struct net_device *netdev,
			      struct ethtool_module_power_mode_params *params,
			      struct netlink_ext_ack *extack)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(netdev);
	struct mlxsw_core *core = mlxsw_m_port->mlxsw_m->core;

	return mlxsw_env_get_module_power_mode(core, mlxsw_m_port->slot_index,
					       mlxsw_m_port->module,
					       params, extack);
}

static int
mlxsw_m_set_module_power_mode(struct net_device *netdev,
			      const struct ethtool_module_power_mode_params *params,
			      struct netlink_ext_ack *extack)
{
	struct mlxsw_m_port *mlxsw_m_port = netdev_priv(netdev);
	struct mlxsw_core *core = mlxsw_m_port->mlxsw_m->core;

	return mlxsw_env_set_module_power_mode(core, mlxsw_m_port->slot_index,
					       mlxsw_m_port->module,
					       params->policy, extack);
}

static const struct ethtool_ops mlxsw_m_port_ethtool_ops = {
	.get_drvinfo		= mlxsw_m_module_get_drvinfo,
	.get_module_info	= mlxsw_m_get_module_info,
	.get_module_eeprom	= mlxsw_m_get_module_eeprom,
	.get_module_eeprom_by_page = mlxsw_m_get_module_eeprom_by_page,
	.reset			= mlxsw_m_reset,
	.get_module_power_mode	= mlxsw_m_get_module_power_mode,
	.set_module_power_mode	= mlxsw_m_set_module_power_mode,
};

static int
mlxsw_m_port_module_info_get(struct mlxsw_m *mlxsw_m, u16 local_port,
			     u8 *p_module, u8 *p_width, u8 *p_slot_index)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	int err;

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	err = mlxsw_reg_query(mlxsw_m->core, MLXSW_REG(pmlp), pmlp_pl);
	if (err)
		return err;
	*p_module = mlxsw_reg_pmlp_module_get(pmlp_pl, 0);
	*p_width = mlxsw_reg_pmlp_width_get(pmlp_pl);
	*p_slot_index = mlxsw_reg_pmlp_slot_index_get(pmlp_pl, 0);

	return 0;
}

static int
mlxsw_m_port_dev_addr_get(struct mlxsw_m_port *mlxsw_m_port)
{
	struct mlxsw_m *mlxsw_m = mlxsw_m_port->mlxsw_m;
	char ppad_pl[MLXSW_REG_PPAD_LEN];
	u8 addr[ETH_ALEN];
	int err;

	mlxsw_reg_ppad_pack(ppad_pl, false, 0);
	err = mlxsw_reg_query(mlxsw_m->core, MLXSW_REG(ppad), ppad_pl);
	if (err)
		return err;
	mlxsw_reg_ppad_mac_memcpy_from(ppad_pl, addr);
	eth_hw_addr_gen(mlxsw_m_port->dev, addr, mlxsw_m_port->module + 1 +
			mlxsw_m_port->module_offset);
	return 0;
}

static bool mlxsw_m_port_created(struct mlxsw_m *mlxsw_m, u16 local_port)
{
	return mlxsw_m->ports[local_port];
}

static int
mlxsw_m_port_create(struct mlxsw_m *mlxsw_m, u16 local_port, u8 slot_index,
		    u8 module)
{
	struct mlxsw_m_port *mlxsw_m_port;
	struct net_device *dev;
	int err;

	err = mlxsw_core_port_init(mlxsw_m->core, local_port, slot_index,
				   module + 1, false, 0, false,
				   0, mlxsw_m->base_mac,
				   sizeof(mlxsw_m->base_mac));
	if (err) {
		dev_err(mlxsw_m->bus_info->dev, "Port %d: Failed to init core port\n",
			local_port);
		return err;
	}

	dev = alloc_etherdev(sizeof(struct mlxsw_m_port));
	if (!dev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	SET_NETDEV_DEV(dev, mlxsw_m->bus_info->dev);
	dev_net_set(dev, mlxsw_core_net(mlxsw_m->core));
	mlxsw_m_port = netdev_priv(dev);
	mlxsw_m_port->dev = dev;
	mlxsw_m_port->mlxsw_m = mlxsw_m;
	mlxsw_m_port->local_port = local_port;
	mlxsw_m_port->module = module;
	mlxsw_m_port->slot_index = slot_index;
	/* Add module offset for line card. Offset for main board iz zero.
	 * For line card in slot #n offset is calculated as (#n - 1)
	 * multiplied by maximum modules number, which could be found on a line
	 * card.
	 */
	mlxsw_m_port->module_offset = mlxsw_m_port->slot_index ?
				      (mlxsw_m_port->slot_index - 1) *
				      mlxsw_m->max_modules_per_slot : 0;

	dev->netdev_ops = &mlxsw_m_port_netdev_ops;
	dev->ethtool_ops = &mlxsw_m_port_ethtool_ops;

	err = mlxsw_m_port_dev_addr_get(mlxsw_m_port);
	if (err) {
		dev_err(mlxsw_m->bus_info->dev, "Port %d: Unable to get port mac address\n",
			mlxsw_m_port->local_port);
		goto err_dev_addr_get;
	}

	netif_carrier_off(dev);
	mlxsw_m->ports[local_port] = mlxsw_m_port;
	err = register_netdev(dev);
	if (err) {
		dev_err(mlxsw_m->bus_info->dev, "Port %d: Failed to register netdev\n",
			mlxsw_m_port->local_port);
		goto err_register_netdev;
	}

	mlxsw_core_port_eth_set(mlxsw_m->core, mlxsw_m_port->local_port,
				mlxsw_m_port, dev);

	return 0;

err_register_netdev:
	mlxsw_m->ports[local_port] = NULL;
err_dev_addr_get:
	free_netdev(dev);
err_alloc_etherdev:
	mlxsw_core_port_fini(mlxsw_m->core, local_port);
	return err;
}

static void mlxsw_m_port_remove(struct mlxsw_m *mlxsw_m, u16 local_port)
{
	struct mlxsw_m_port *mlxsw_m_port = mlxsw_m->ports[local_port];

	mlxsw_core_port_clear(mlxsw_m->core, local_port, mlxsw_m);
	unregister_netdev(mlxsw_m_port->dev); /* This calls ndo_stop */
	mlxsw_m->ports[local_port] = NULL;
	free_netdev(mlxsw_m_port->dev);
	mlxsw_core_port_fini(mlxsw_m->core, local_port);
}

static int*
mlxsw_m_port_mapping_get(struct mlxsw_m *mlxsw_m, u8 slot_index, u8 module)
{
	return &mlxsw_m->line_cards[slot_index]->module_to_port[module];
}

static int mlxsw_m_port_module_map(struct mlxsw_m *mlxsw_m, u16 local_port,
				   u8 *last_module)
{
	unsigned int max_ports = mlxsw_core_max_ports(mlxsw_m->core);
	u8 module, width, slot_index;
	int *module_to_port;
	int err;

	/* Fill out to local port mapping array */
	err = mlxsw_m_port_module_info_get(mlxsw_m, local_port, &module,
					   &width, &slot_index);
	if (err)
		return err;

	/* Skip if line card has been already configured */
	if (mlxsw_m->line_cards[slot_index]->active)
		return 0;
	if (!width)
		return 0;
	/* Skip, if port belongs to the cluster */
	if (module == *last_module)
		return 0;
	*last_module = module;

	if (WARN_ON_ONCE(module >= max_ports))
		return -EINVAL;
	mlxsw_env_module_port_map(mlxsw_m->core, slot_index, module);
	module_to_port = mlxsw_m_port_mapping_get(mlxsw_m, slot_index, module);
	*module_to_port = local_port;

	return 0;
}

static void
mlxsw_m_port_module_unmap(struct mlxsw_m *mlxsw_m, u8 slot_index, u8 module)
{
	int *module_to_port = mlxsw_m_port_mapping_get(mlxsw_m, slot_index,
						       module);
	*module_to_port = -1;
	mlxsw_env_module_port_unmap(mlxsw_m->core, slot_index, module);
}

static int mlxsw_m_linecards_init(struct mlxsw_m *mlxsw_m)
{
	unsigned int max_ports = mlxsw_core_max_ports(mlxsw_m->core);
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	u8 num_of_modules;
	int i, j, err;

	mlxsw_reg_mgpir_pack(mgpir_pl, 0);
	err = mlxsw_reg_query(mlxsw_m->core, MLXSW_REG(mgpir), mgpir_pl);
	if (err)
		return err;

	mlxsw_reg_mgpir_unpack(mgpir_pl, NULL, NULL, NULL, &num_of_modules,
			       &mlxsw_m->num_of_slots);
	/* If the system is modular, get the maximum number of modules per-slot.
	 * Otherwise, get the maximum number of modules on the main board.
	 */
	if (mlxsw_m->num_of_slots)
		mlxsw_m->max_modules_per_slot =
			mlxsw_reg_mgpir_max_modules_per_slot_get(mgpir_pl);
	else
		mlxsw_m->max_modules_per_slot = num_of_modules;
	/* Add slot for main board. */
	mlxsw_m->num_of_slots += 1;

	mlxsw_m->ports = kcalloc(max_ports, sizeof(*mlxsw_m->ports),
				 GFP_KERNEL);
	if (!mlxsw_m->ports)
		return -ENOMEM;

	mlxsw_m->line_cards = kcalloc(mlxsw_m->num_of_slots,
				      sizeof(*mlxsw_m->line_cards),
				      GFP_KERNEL);
	if (!mlxsw_m->line_cards) {
		err = -ENOMEM;
		goto err_kcalloc;
	}

	for (i = 0; i < mlxsw_m->num_of_slots; i++) {
		mlxsw_m->line_cards[i] =
			kzalloc(struct_size(mlxsw_m->line_cards[i],
					    module_to_port,
					    mlxsw_m->max_modules_per_slot),
				GFP_KERNEL);
		if (!mlxsw_m->line_cards[i]) {
			err = -ENOMEM;
			goto err_kmalloc_array;
		}

		/* Invalidate the entries of module to local port mapping array. */
		for (j = 0; j < mlxsw_m->max_modules_per_slot; j++)
			mlxsw_m->line_cards[i]->module_to_port[j] = -1;
	}

	return 0;

err_kmalloc_array:
	for (i--; i >= 0; i--)
		kfree(mlxsw_m->line_cards[i]);
	kfree(mlxsw_m->line_cards);
err_kcalloc:
	kfree(mlxsw_m->ports);
	return err;
}

static void mlxsw_m_linecards_fini(struct mlxsw_m *mlxsw_m)
{
	int i = mlxsw_m->num_of_slots;

	for (i--; i >= 0; i--)
		kfree(mlxsw_m->line_cards[i]);
	kfree(mlxsw_m->line_cards);
	kfree(mlxsw_m->ports);
}

static void
mlxsw_m_linecard_port_module_unmap(struct mlxsw_m *mlxsw_m, u8 slot_index)
{
	int i;

	for (i = mlxsw_m->max_modules_per_slot - 1; i >= 0; i--) {
		int *module_to_port;

		module_to_port = mlxsw_m_port_mapping_get(mlxsw_m, slot_index, i);
		if (*module_to_port > 0)
			mlxsw_m_port_module_unmap(mlxsw_m, slot_index, i);
	}
}

static int
mlxsw_m_linecard_ports_create(struct mlxsw_m *mlxsw_m, u8 slot_index)
{
	int *module_to_port;
	int i, err;

	for (i = 0; i < mlxsw_m->max_modules_per_slot; i++) {
		module_to_port = mlxsw_m_port_mapping_get(mlxsw_m, slot_index, i);
		if (*module_to_port > 0) {
			err = mlxsw_m_port_create(mlxsw_m, *module_to_port,
						  slot_index, i);
			if (err)
				goto err_port_create;
			/* Mark slot as active */
			if (!mlxsw_m->line_cards[slot_index]->active)
				mlxsw_m->line_cards[slot_index]->active = true;
		}
	}
	return 0;

err_port_create:
	for (i--; i >= 0; i--) {
		module_to_port = mlxsw_m_port_mapping_get(mlxsw_m, slot_index, i);
		if (*module_to_port > 0 &&
		    mlxsw_m_port_created(mlxsw_m, *module_to_port)) {
			mlxsw_m_port_remove(mlxsw_m, *module_to_port);
			/* Mark slot as inactive */
			if (mlxsw_m->line_cards[slot_index]->active)
				mlxsw_m->line_cards[slot_index]->active = false;
		}
	}
	return err;
}

static void
mlxsw_m_linecard_ports_remove(struct mlxsw_m *mlxsw_m, u8 slot_index)
{
	int i;

	for (i = 0; i < mlxsw_m->max_modules_per_slot; i++) {
		int *module_to_port = mlxsw_m_port_mapping_get(mlxsw_m,
							       slot_index, i);

		if (*module_to_port > 0 &&
		    mlxsw_m_port_created(mlxsw_m, *module_to_port)) {
			mlxsw_m_port_remove(mlxsw_m, *module_to_port);
			mlxsw_m_port_module_unmap(mlxsw_m, slot_index, i);
		}
	}
}

static int mlxsw_m_ports_module_map(struct mlxsw_m *mlxsw_m)
{
	unsigned int max_ports = mlxsw_core_max_ports(mlxsw_m->core);
	u8 last_module = max_ports;
	int i, err;

	for (i = 1; i < max_ports; i++) {
		err = mlxsw_m_port_module_map(mlxsw_m, i, &last_module);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_m_ports_create(struct mlxsw_m *mlxsw_m)
{
	int err;

	/* Fill out module to local port mapping array */
	err = mlxsw_m_ports_module_map(mlxsw_m);
	if (err)
		goto err_ports_module_map;

	/* Create port objects for each valid entry */
	err = mlxsw_m_linecard_ports_create(mlxsw_m, 0);
	if (err)
		goto err_linecard_ports_create;

	return 0;

err_linecard_ports_create:
err_ports_module_map:
	mlxsw_m_linecard_port_module_unmap(mlxsw_m, 0);

	return err;
}

static void mlxsw_m_ports_remove(struct mlxsw_m *mlxsw_m)
{
	mlxsw_m_linecard_ports_remove(mlxsw_m, 0);
}

static void
mlxsw_m_ports_remove_selected(struct mlxsw_core *mlxsw_core,
			      bool (*selector)(void *priv, u16 local_port),
			      void *priv)
{
	struct mlxsw_m *mlxsw_m = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_linecard *linecard_priv = priv;
	struct mlxsw_m_line_card *linecard;

	linecard = mlxsw_m->line_cards[linecard_priv->slot_index];

	if (WARN_ON(!linecard->active))
		return;

	mlxsw_m_linecard_ports_remove(mlxsw_m, linecard_priv->slot_index);
	linecard->active = false;
}

static int mlxsw_m_fw_rev_validate(struct mlxsw_m *mlxsw_m)
{
	const struct mlxsw_fw_rev *rev = &mlxsw_m->bus_info->fw_rev;

	/* Validate driver and FW are compatible.
	 * Do not check major version, since it defines chip type, while
	 * driver is supposed to support any type.
	 */
	if (mlxsw_core_fw_rev_minor_subminor_validate(rev, &mlxsw_m_fw_rev))
		return 0;

	dev_err(mlxsw_m->bus_info->dev, "The firmware version %d.%d.%d is incompatible with the driver (required >= %d.%d.%d)\n",
		rev->major, rev->minor, rev->subminor, rev->major,
		mlxsw_m_fw_rev.minor, mlxsw_m_fw_rev.subminor);

	return -EINVAL;
}

static void
mlxsw_m_got_active(struct mlxsw_core *mlxsw_core, u8 slot_index, void *priv)
{
	struct mlxsw_m_line_card *linecard;
	struct mlxsw_m *mlxsw_m = priv;
	int err;

	linecard = mlxsw_m->line_cards[slot_index];
	/* Skip if line card has been already configured during init */
	if (linecard->active)
		return;

	/* Fill out module to local port mapping array */
	err = mlxsw_m_ports_module_map(mlxsw_m);
	if (err)
		goto err_ports_module_map;

	/* Create port objects for each valid entry */
	err = mlxsw_m_linecard_ports_create(mlxsw_m, slot_index);
	if (err) {
		dev_err(mlxsw_m->bus_info->dev, "Failed to create port for line card at slot %d\n",
			slot_index);
		goto err_linecard_ports_create;
	}

	linecard->active = true;

	return;

err_linecard_ports_create:
err_ports_module_map:
	mlxsw_m_linecard_port_module_unmap(mlxsw_m, slot_index);
}

static void
mlxsw_m_got_inactive(struct mlxsw_core *mlxsw_core, u8 slot_index, void *priv)
{
	struct mlxsw_m_line_card *linecard;
	struct mlxsw_m *mlxsw_m = priv;

	linecard = mlxsw_m->line_cards[slot_index];

	if (WARN_ON(!linecard->active))
		return;

	mlxsw_m_linecard_ports_remove(mlxsw_m, slot_index);
	linecard->active = false;
}

static struct mlxsw_linecards_event_ops mlxsw_m_event_ops = {
	.got_active = mlxsw_m_got_active,
	.got_inactive = mlxsw_m_got_inactive,
};

static int mlxsw_m_init(struct mlxsw_core *mlxsw_core,
			const struct mlxsw_bus_info *mlxsw_bus_info,
			struct netlink_ext_ack *extack)
{
	struct mlxsw_m *mlxsw_m = mlxsw_core_driver_priv(mlxsw_core);
	int err;

	mlxsw_m->core = mlxsw_core;
	mlxsw_m->bus_info = mlxsw_bus_info;

	err = mlxsw_m_fw_rev_validate(mlxsw_m);
	if (err)
		return err;

	err = mlxsw_m_base_mac_get(mlxsw_m);
	if (err) {
		dev_err(mlxsw_m->bus_info->dev, "Failed to get base mac\n");
		return err;
	}

	err = mlxsw_m_linecards_init(mlxsw_m);
	if (err) {
		dev_err(mlxsw_m->bus_info->dev, "Failed to create line cards\n");
		return err;
	}

	err = mlxsw_linecards_event_ops_register(mlxsw_core,
						 &mlxsw_m_event_ops, mlxsw_m);
	if (err) {
		dev_err(mlxsw_m->bus_info->dev, "Failed to register line cards operations\n");
		goto linecards_event_ops_register;
	}

	err = mlxsw_m_ports_create(mlxsw_m);
	if (err) {
		dev_err(mlxsw_m->bus_info->dev, "Failed to create ports\n");
		goto err_ports_create;
	}

	return 0;

err_ports_create:
	mlxsw_linecards_event_ops_unregister(mlxsw_core,
					     &mlxsw_m_event_ops, mlxsw_m);
linecards_event_ops_register:
	mlxsw_m_linecards_fini(mlxsw_m);
	return err;
}

static void mlxsw_m_fini(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_m *mlxsw_m = mlxsw_core_driver_priv(mlxsw_core);

	mlxsw_m_ports_remove(mlxsw_m);
	mlxsw_linecards_event_ops_unregister(mlxsw_core,
					     &mlxsw_m_event_ops, mlxsw_m);
	mlxsw_m_linecards_fini(mlxsw_m);
}

static const struct mlxsw_config_profile mlxsw_m_config_profile;

static struct mlxsw_driver mlxsw_m_driver = {
	.kind			= mlxsw_m_driver_name,
	.priv_size		= sizeof(struct mlxsw_m),
	.init			= mlxsw_m_init,
	.fini			= mlxsw_m_fini,
	.ports_remove_selected	= mlxsw_m_ports_remove_selected,
	.profile		= &mlxsw_m_config_profile,
};

static const struct i2c_device_id mlxsw_m_i2c_id[] = {
	{ "mlxsw_minimal", 0},
	{ },
};

static struct i2c_driver mlxsw_m_i2c_driver = {
	.driver.name = "mlxsw_minimal",
	.class = I2C_CLASS_HWMON,
	.id_table = mlxsw_m_i2c_id,
};

static int __init mlxsw_m_module_init(void)
{
	int err;

	err = mlxsw_core_driver_register(&mlxsw_m_driver);
	if (err)
		return err;

	err = mlxsw_i2c_driver_register(&mlxsw_m_i2c_driver);
	if (err)
		goto err_i2c_driver_register;

	return 0;

err_i2c_driver_register:
	mlxsw_core_driver_unregister(&mlxsw_m_driver);

	return err;
}

static void __exit mlxsw_m_module_exit(void)
{
	mlxsw_i2c_driver_unregister(&mlxsw_m_i2c_driver);
	mlxsw_core_driver_unregister(&mlxsw_m_driver);
}

module_init(mlxsw_m_module_init);
module_exit(mlxsw_m_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("Mellanox minimal driver");
MODULE_DEVICE_TABLE(i2c, mlxsw_m_i2c_id);
