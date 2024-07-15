// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd. */

#include <linux/mlx5/driver.h>
#include "eswitch.h"

static void
mlx5_esw_get_port_parent_id(struct mlx5_core_dev *dev, struct netdev_phys_item_id *ppid)
{
	u64 parent_id;

	parent_id = mlx5_query_nic_system_image_guid(dev);
	ppid->id_len = sizeof(parent_id);
	memcpy(ppid->id, &parent_id, sizeof(parent_id));
}

static bool mlx5_esw_devlink_port_supported(struct mlx5_eswitch *esw, u16 vport_num)
{
	return (mlx5_core_is_ecpf(esw->dev) && vport_num == MLX5_VPORT_PF) ||
	       mlx5_eswitch_is_vf_vport(esw, vport_num) ||
	       mlx5_core_is_ec_vf_vport(esw->dev, vport_num);
}

static void mlx5_esw_offloads_pf_vf_devlink_port_attrs_set(struct mlx5_eswitch *esw,
							   u16 vport_num,
							   struct devlink_port *dl_port)
{
	struct mlx5_core_dev *dev = esw->dev;
	struct netdev_phys_item_id ppid = {};
	u32 controller_num = 0;
	bool external;
	u16 pfnum;

	mlx5_esw_get_port_parent_id(dev, &ppid);
	pfnum = mlx5_get_dev_index(dev);
	external = mlx5_core_is_ecpf_esw_manager(dev);
	if (external)
		controller_num = dev->priv.eswitch->offloads.host_number + 1;

	if (vport_num == MLX5_VPORT_PF) {
		memcpy(dl_port->attrs.switch_id.id, ppid.id, ppid.id_len);
		dl_port->attrs.switch_id.id_len = ppid.id_len;
		devlink_port_attrs_pci_pf_set(dl_port, controller_num, pfnum, external);
	} else if (mlx5_eswitch_is_vf_vport(esw, vport_num)) {
		memcpy(dl_port->attrs.switch_id.id, ppid.id, ppid.id_len);
		dl_port->attrs.switch_id.id_len = ppid.id_len;
		devlink_port_attrs_pci_vf_set(dl_port, controller_num, pfnum,
					      vport_num - 1, external);
	}  else if (mlx5_core_is_ec_vf_vport(esw->dev, vport_num)) {
		memcpy(dl_port->attrs.switch_id.id, ppid.id, ppid.id_len);
		dl_port->attrs.switch_id.id_len = ppid.id_len;
		devlink_port_attrs_pci_vf_set(dl_port, 0, pfnum,
					      vport_num - 1, false);
	}
}

int mlx5_esw_offloads_pf_vf_devlink_port_init(struct mlx5_eswitch *esw,
					      struct mlx5_vport *vport)
{
	struct mlx5_devlink_port *dl_port;
	u16 vport_num = vport->vport;

	if (!mlx5_esw_devlink_port_supported(esw, vport_num))
		return 0;

	dl_port = kzalloc(sizeof(*dl_port), GFP_KERNEL);
	if (!dl_port)
		return -ENOMEM;

	mlx5_esw_offloads_pf_vf_devlink_port_attrs_set(esw, vport_num,
						       &dl_port->dl_port);

	vport->dl_port = dl_port;
	mlx5_devlink_port_init(dl_port, vport);
	return 0;
}

void mlx5_esw_offloads_pf_vf_devlink_port_cleanup(struct mlx5_eswitch *esw,
						  struct mlx5_vport *vport)
{
	if (!vport->dl_port)
		return;

	kfree(vport->dl_port);
	vport->dl_port = NULL;
}

static const struct devlink_port_ops mlx5_esw_pf_vf_dl_port_ops = {
	.port_fn_hw_addr_get = mlx5_devlink_port_fn_hw_addr_get,
	.port_fn_hw_addr_set = mlx5_devlink_port_fn_hw_addr_set,
	.port_fn_roce_get = mlx5_devlink_port_fn_roce_get,
	.port_fn_roce_set = mlx5_devlink_port_fn_roce_set,
	.port_fn_migratable_get = mlx5_devlink_port_fn_migratable_get,
	.port_fn_migratable_set = mlx5_devlink_port_fn_migratable_set,
#ifdef CONFIG_XFRM_OFFLOAD
	.port_fn_ipsec_crypto_get = mlx5_devlink_port_fn_ipsec_crypto_get,
	.port_fn_ipsec_crypto_set = mlx5_devlink_port_fn_ipsec_crypto_set,
	.port_fn_ipsec_packet_get = mlx5_devlink_port_fn_ipsec_packet_get,
	.port_fn_ipsec_packet_set = mlx5_devlink_port_fn_ipsec_packet_set,
#endif /* CONFIG_XFRM_OFFLOAD */
};

static void mlx5_esw_offloads_sf_devlink_port_attrs_set(struct mlx5_eswitch *esw,
							struct devlink_port *dl_port,
							u32 controller, u32 sfnum)
{
	struct mlx5_core_dev *dev = esw->dev;
	struct netdev_phys_item_id ppid = {};
	u16 pfnum;

	pfnum = mlx5_get_dev_index(dev);
	mlx5_esw_get_port_parent_id(dev, &ppid);
	memcpy(dl_port->attrs.switch_id.id, &ppid.id[0], ppid.id_len);
	dl_port->attrs.switch_id.id_len = ppid.id_len;
	devlink_port_attrs_pci_sf_set(dl_port, controller, pfnum, sfnum, !!controller);
}

int mlx5_esw_offloads_sf_devlink_port_init(struct mlx5_eswitch *esw, struct mlx5_vport *vport,
					   struct mlx5_devlink_port *dl_port,
					   u32 controller, u32 sfnum)
{
	mlx5_esw_offloads_sf_devlink_port_attrs_set(esw, &dl_port->dl_port, controller, sfnum);

	vport->dl_port = dl_port;
	mlx5_devlink_port_init(dl_port, vport);
	return 0;
}

void mlx5_esw_offloads_sf_devlink_port_cleanup(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	vport->dl_port = NULL;
}

static const struct devlink_port_ops mlx5_esw_dl_sf_port_ops = {
#ifdef CONFIG_MLX5_SF_MANAGER
	.port_del = mlx5_devlink_sf_port_del,
#endif
	.port_fn_hw_addr_get = mlx5_devlink_port_fn_hw_addr_get,
	.port_fn_hw_addr_set = mlx5_devlink_port_fn_hw_addr_set,
	.port_fn_roce_get = mlx5_devlink_port_fn_roce_get,
	.port_fn_roce_set = mlx5_devlink_port_fn_roce_set,
#ifdef CONFIG_MLX5_SF_MANAGER
	.port_fn_state_get = mlx5_devlink_sf_port_fn_state_get,
	.port_fn_state_set = mlx5_devlink_sf_port_fn_state_set,
#endif
};

int mlx5_esw_offloads_devlink_port_register(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	struct mlx5_core_dev *dev = esw->dev;
	const struct devlink_port_ops *ops;
	struct mlx5_devlink_port *dl_port;
	u16 vport_num = vport->vport;
	unsigned int dl_port_index;
	struct devlink *devlink;
	int err;

	dl_port = vport->dl_port;
	if (!dl_port)
		return 0;

	if (mlx5_esw_is_sf_vport(esw, vport_num))
		ops = &mlx5_esw_dl_sf_port_ops;
	else if (mlx5_eswitch_is_pf_vf_vport(esw, vport_num))
		ops = &mlx5_esw_pf_vf_dl_port_ops;
	else
		ops = NULL;

	devlink = priv_to_devlink(dev);
	dl_port_index = mlx5_esw_vport_to_devlink_port_index(dev, vport_num);
	err = devl_port_register_with_ops(devlink, &dl_port->dl_port, dl_port_index, ops);
	if (err)
		return err;

	err = devl_rate_leaf_create(&dl_port->dl_port, vport, NULL);
	if (err)
		goto rate_err;

	return 0;

rate_err:
	devl_port_unregister(&dl_port->dl_port);
	return err;
}

void mlx5_esw_offloads_devlink_port_unregister(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	struct mlx5_devlink_port *dl_port;

	if (!vport->dl_port)
		return;
	dl_port = vport->dl_port;

	mlx5_esw_qos_vport_update_group(esw, vport, NULL, NULL);
	devl_rate_leaf_destroy(&dl_port->dl_port);

	devl_port_unregister(&dl_port->dl_port);
}

struct devlink_port *mlx5_esw_offloads_devlink_port(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_vport *vport;

	vport = mlx5_eswitch_get_vport(esw, vport_num);
	return IS_ERR(vport) ? ERR_CAST(vport) : &vport->dl_port->dl_port;
}
