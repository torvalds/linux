/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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
 */

#include <linux/mlx5/driver.h>
#include <linux/etherdevice.h>
#include <linux/idr.h>
#include "mlx5_core.h"
#include "lib/mlx5.h"

void mlx5_init_reserved_gids(struct mlx5_core_dev *dev)
{
	unsigned int tblsz = MLX5_CAP_ROCE(dev, roce_address_table_size);

	ida_init(&dev->roce.reserved_gids.ida);
	dev->roce.reserved_gids.start = tblsz;
	dev->roce.reserved_gids.count = 0;
}

void mlx5_cleanup_reserved_gids(struct mlx5_core_dev *dev)
{
	WARN_ON(!ida_is_empty(&dev->roce.reserved_gids.ida));
	dev->roce.reserved_gids.start = 0;
	dev->roce.reserved_gids.count = 0;
	ida_destroy(&dev->roce.reserved_gids.ida);
}

int mlx5_core_reserve_gids(struct mlx5_core_dev *dev, unsigned int count)
{
	if (test_bit(MLX5_INTERFACE_STATE_UP, &dev->intf_state)) {
		mlx5_core_err(dev, "Cannot reserve GIDs when interfaces are up\n");
		return -EPERM;
	}
	if (dev->roce.reserved_gids.start < count) {
		mlx5_core_warn(dev, "GID table exhausted attempting to reserve %d more GIDs\n",
			       count);
		return -ENOMEM;
	}
	if (dev->roce.reserved_gids.count + count > MLX5_MAX_RESERVED_GIDS) {
		mlx5_core_warn(dev, "Unable to reserve %d more GIDs\n", count);
		return -ENOMEM;
	}

	dev->roce.reserved_gids.start -= count;
	dev->roce.reserved_gids.count += count;
	mlx5_core_dbg(dev, "Reserved %u GIDs starting at %u\n",
		      dev->roce.reserved_gids.count,
		      dev->roce.reserved_gids.start);
	return 0;
}

void mlx5_core_unreserve_gids(struct mlx5_core_dev *dev, unsigned int count)
{
	WARN(test_bit(MLX5_INTERFACE_STATE_UP, &dev->intf_state), "Unreserving GIDs when interfaces are up");
	WARN(count > dev->roce.reserved_gids.count, "Unreserving %u GIDs when only %u reserved",
	     count, dev->roce.reserved_gids.count);

	dev->roce.reserved_gids.start += count;
	dev->roce.reserved_gids.count -= count;
	mlx5_core_dbg(dev, "%u GIDs starting at %u left reserved\n",
		      dev->roce.reserved_gids.count,
		      dev->roce.reserved_gids.start);
}

int mlx5_core_reserved_gid_alloc(struct mlx5_core_dev *dev, int *gid_index)
{
	int end = dev->roce.reserved_gids.start +
		  dev->roce.reserved_gids.count;
	int index = 0;

	index = ida_simple_get(&dev->roce.reserved_gids.ida,
			       dev->roce.reserved_gids.start, end,
			       GFP_KERNEL);
	if (index < 0)
		return index;

	mlx5_core_dbg(dev, "Allocating reserved GID %u\n", index);
	*gid_index = index;
	return 0;
}

void mlx5_core_reserved_gid_free(struct mlx5_core_dev *dev, int gid_index)
{
	mlx5_core_dbg(dev, "Freeing reserved GID %u\n", gid_index);
	ida_simple_remove(&dev->roce.reserved_gids.ida, gid_index);
}

unsigned int mlx5_core_reserved_gids_count(struct mlx5_core_dev *dev)
{
	return dev->roce.reserved_gids.count;
}
EXPORT_SYMBOL_GPL(mlx5_core_reserved_gids_count);

int mlx5_core_roce_gid_set(struct mlx5_core_dev *dev, unsigned int index,
			   u8 roce_version, u8 roce_l3_type, const u8 *gid,
			   const u8 *mac, bool vlan, u16 vlan_id, u8 port_num)
{
#define MLX5_SET_RA(p, f, v) MLX5_SET(roce_addr_layout, p, f, v)
	u32  in[MLX5_ST_SZ_DW(set_roce_address_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(set_roce_address_out)] = {0};
	void *in_addr = MLX5_ADDR_OF(set_roce_address_in, in, roce_address);
	char *addr_l3_addr = MLX5_ADDR_OF(roce_addr_layout, in_addr,
					  source_l3_address);
	void *addr_mac = MLX5_ADDR_OF(roce_addr_layout, in_addr,
				      source_mac_47_32);
	int gidsz = MLX5_FLD_SZ_BYTES(roce_addr_layout, source_l3_address);

	if (MLX5_CAP_GEN(dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return -EINVAL;

	if (gid) {
		if (vlan) {
			MLX5_SET_RA(in_addr, vlan_valid, 1);
			MLX5_SET_RA(in_addr, vlan_id, vlan_id);
		}

		ether_addr_copy(addr_mac, mac);
		MLX5_SET_RA(in_addr, roce_version, roce_version);
		MLX5_SET_RA(in_addr, roce_l3_type, roce_l3_type);
		memcpy(addr_l3_addr, gid, gidsz);
	}

	if (MLX5_CAP_GEN(dev, num_vhca_ports) > 0)
		MLX5_SET(set_roce_address_in, in, vhca_port_num, port_num);

	MLX5_SET(set_roce_address_in, in, roce_address_index, index);
	MLX5_SET(set_roce_address_in, in, opcode, MLX5_CMD_OP_SET_ROCE_ADDRESS);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_roce_gid_set);
