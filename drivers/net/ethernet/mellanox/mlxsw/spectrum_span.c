/*
 * drivers/net/ethernet/mellanox/mlxsw/mlxsw_span.c
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/list.h>

#include "spectrum.h"
#include "spectrum_span.h"

int mlxsw_sp_span_init(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_SPAN))
		return -EIO;

	mlxsw_sp->span.entries_count = MLXSW_CORE_RES_GET(mlxsw_sp->core,
							  MAX_SPAN);
	mlxsw_sp->span.entries = kcalloc(mlxsw_sp->span.entries_count,
					 sizeof(struct mlxsw_sp_span_entry),
					 GFP_KERNEL);
	if (!mlxsw_sp->span.entries)
		return -ENOMEM;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		INIT_LIST_HEAD(&curr->bound_ports_list);
		curr->id = i;
	}

	return 0;
}

void mlxsw_sp_span_fini(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		WARN_ON_ONCE(!list_empty(&curr->bound_ports_list));
	}
	kfree(mlxsw_sp->span.entries);
}

static int
mlxsw_sp_span_entry_phys_parms(const struct net_device *to_dev,
			       struct mlxsw_sp_span_parms *sparmsp)
{
	sparmsp->dest_port = netdev_priv(to_dev);
	return 0;
}

static int
mlxsw_sp_span_entry_phys_configure(struct mlxsw_sp_span_entry *span_entry,
				   struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_port *dest_port = sparms.dest_port;
	struct mlxsw_sp *mlxsw_sp = dest_port->mlxsw_sp;
	u8 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	/* Create a new port analayzer entry for local_port. */
	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_LOCAL_ETH);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

static void
mlxsw_sp_span_entry_deconfigure_common(struct mlxsw_sp_span_entry *span_entry,
				       enum mlxsw_reg_mpat_span_type span_type)
{
	struct mlxsw_sp_port *dest_port = span_entry->parms.dest_port;
	struct mlxsw_sp *mlxsw_sp = dest_port->mlxsw_sp;
	u8 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, false, span_type);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

static void
mlxsw_sp_span_entry_phys_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure_common(span_entry,
					    MLXSW_REG_MPAT_SPAN_TYPE_LOCAL_ETH);
}

static const
struct mlxsw_sp_span_entry_ops mlxsw_sp_span_entry_ops_phys = {
	.can_handle = mlxsw_sp_port_dev_check,
	.parms = mlxsw_sp_span_entry_phys_parms,
	.configure = mlxsw_sp_span_entry_phys_configure,
	.deconfigure = mlxsw_sp_span_entry_phys_deconfigure,
};

static const
struct mlxsw_sp_span_entry_ops *const mlxsw_sp_span_entry_types[] = {
	&mlxsw_sp_span_entry_ops_phys,
};

static int
mlxsw_sp_span_entry_nop_parms(const struct net_device *to_dev,
			      struct mlxsw_sp_span_parms *sparmsp)
{
	sparmsp->dest_port = NULL;
	return 0;
}

static int
mlxsw_sp_span_entry_nop_configure(struct mlxsw_sp_span_entry *span_entry,
				  struct mlxsw_sp_span_parms sparms)
{
	return 0;
}

static void
mlxsw_sp_span_entry_nop_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
}

static const struct mlxsw_sp_span_entry_ops mlxsw_sp_span_entry_ops_nop = {
	.parms = mlxsw_sp_span_entry_nop_parms,
	.configure = mlxsw_sp_span_entry_nop_configure,
	.deconfigure = mlxsw_sp_span_entry_nop_deconfigure,
};

static void
mlxsw_sp_span_entry_configure(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_span_entry *span_entry,
			      struct mlxsw_sp_span_parms sparms)
{
	if (sparms.dest_port) {
		if (sparms.dest_port->mlxsw_sp != mlxsw_sp) {
			netdev_err(span_entry->to_dev, "Cannot mirror to %s, which belongs to a different mlxsw instance",
				   sparms.dest_port->dev->name);
			sparms.dest_port = NULL;
		} else if (span_entry->ops->configure(span_entry, sparms)) {
			netdev_err(span_entry->to_dev, "Failed to offload mirror to %s",
				   sparms.dest_port->dev->name);
			sparms.dest_port = NULL;
		}
	}

	span_entry->parms = sparms;
}

static void
mlxsw_sp_span_entry_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	if (span_entry->parms.dest_port)
		span_entry->ops->deconfigure(span_entry);
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_create(struct mlxsw_sp *mlxsw_sp,
			   const struct net_device *to_dev,
			   const struct mlxsw_sp_span_entry_ops *ops,
			   struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_span_entry *span_entry = NULL;
	int i;

	/* find a free entry to use */
	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		if (!mlxsw_sp->span.entries[i].ref_count) {
			span_entry = &mlxsw_sp->span.entries[i];
			break;
		}
	}
	if (!span_entry)
		return NULL;

	span_entry->ops = ops;
	span_entry->ref_count = 1;
	span_entry->to_dev = to_dev;
	mlxsw_sp_span_entry_configure(mlxsw_sp, span_entry, sparms);

	return span_entry;
}

static void mlxsw_sp_span_entry_destroy(struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure(span_entry);
}

struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_port(struct mlxsw_sp *mlxsw_sp,
				 const struct net_device *to_dev)
{
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		if (curr->ref_count && curr->to_dev == to_dev)
			return curr;
	}
	return NULL;
}

void mlxsw_sp_span_entry_invalidate(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure(span_entry);
	span_entry->ops = &mlxsw_sp_span_entry_ops_nop;
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_id(struct mlxsw_sp *mlxsw_sp, int span_id)
{
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		if (curr->ref_count && curr->id == span_id)
			return curr;
	}
	return NULL;
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_get(struct mlxsw_sp *mlxsw_sp,
			const struct net_device *to_dev,
			const struct mlxsw_sp_span_entry_ops *ops,
			struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_span_entry *span_entry;

	span_entry = mlxsw_sp_span_entry_find_by_port(mlxsw_sp, to_dev);
	if (span_entry) {
		/* Already exists, just take a reference */
		span_entry->ref_count++;
		return span_entry;
	}

	return mlxsw_sp_span_entry_create(mlxsw_sp, to_dev, ops, sparms);
}

static int mlxsw_sp_span_entry_put(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_span_entry *span_entry)
{
	WARN_ON(!span_entry->ref_count);
	if (--span_entry->ref_count == 0)
		mlxsw_sp_span_entry_destroy(span_entry);
	return 0;
}

static bool mlxsw_sp_span_is_egress_mirror(struct mlxsw_sp_port *port)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	struct mlxsw_sp_span_inspected_port *p;
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		list_for_each_entry(p, &curr->bound_ports_list, list)
			if (p->local_port == port->local_port &&
			    p->type == MLXSW_SP_SPAN_EGRESS)
				return true;
	}

	return false;
}

static int mlxsw_sp_span_mtu_to_buffsize(const struct mlxsw_sp *mlxsw_sp,
					 int mtu)
{
	return mlxsw_sp_bytes_cells(mlxsw_sp, mtu * 5 / 2) + 1;
}

int mlxsw_sp_span_port_mtu_update(struct mlxsw_sp_port *port, u16 mtu)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char sbib_pl[MLXSW_REG_SBIB_LEN];
	int err;

	/* If port is egress mirrored, the shared buffer size should be
	 * updated according to the mtu value
	 */
	if (mlxsw_sp_span_is_egress_mirror(port)) {
		u32 buffsize = mlxsw_sp_span_mtu_to_buffsize(mlxsw_sp, mtu);

		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, buffsize);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
		if (err) {
			netdev_err(port->dev, "Could not update shared buffer for mirroring\n");
			return err;
		}
	}

	return 0;
}

static struct mlxsw_sp_span_inspected_port *
mlxsw_sp_span_entry_bound_port_find(struct mlxsw_sp_port *port,
				    struct mlxsw_sp_span_entry *span_entry)
{
	struct mlxsw_sp_span_inspected_port *p;

	list_for_each_entry(p, &span_entry->bound_ports_list, list)
		if (port->local_port == p->local_port)
			return p;
	return NULL;
}

static int
mlxsw_sp_span_inspected_port_bind(struct mlxsw_sp_port *port,
				  struct mlxsw_sp_span_entry *span_entry,
				  enum mlxsw_sp_span_type type,
				  bool bind)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char mpar_pl[MLXSW_REG_MPAR_LEN];
	int pa_id = span_entry->id;

	/* bind the port to the SPAN entry */
	mlxsw_reg_mpar_pack(mpar_pl, port->local_port,
			    (enum mlxsw_reg_mpar_i_e)type, bind, pa_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpar), mpar_pl);
}

static int
mlxsw_sp_span_inspected_port_add(struct mlxsw_sp_port *port,
				 struct mlxsw_sp_span_entry *span_entry,
				 enum mlxsw_sp_span_type type,
				 bool bind)
{
	struct mlxsw_sp_span_inspected_port *inspected_port;
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char sbib_pl[MLXSW_REG_SBIB_LEN];
	int err;

	/* if it is an egress SPAN, bind a shared buffer to it */
	if (type == MLXSW_SP_SPAN_EGRESS) {
		u32 buffsize = mlxsw_sp_span_mtu_to_buffsize(mlxsw_sp,
							     port->dev->mtu);

		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, buffsize);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
		if (err) {
			netdev_err(port->dev, "Could not create shared buffer for mirroring\n");
			return err;
		}
	}

	if (bind) {
		err = mlxsw_sp_span_inspected_port_bind(port, span_entry, type,
							true);
		if (err)
			goto err_port_bind;
	}

	inspected_port = kzalloc(sizeof(*inspected_port), GFP_KERNEL);
	if (!inspected_port) {
		err = -ENOMEM;
		goto err_inspected_port_alloc;
	}
	inspected_port->local_port = port->local_port;
	inspected_port->type = type;
	list_add_tail(&inspected_port->list, &span_entry->bound_ports_list);

	return 0;

err_inspected_port_alloc:
	if (bind)
		mlxsw_sp_span_inspected_port_bind(port, span_entry, type,
						  false);
err_port_bind:
	if (type == MLXSW_SP_SPAN_EGRESS) {
		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, 0);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
	}
	return err;
}

static void
mlxsw_sp_span_inspected_port_del(struct mlxsw_sp_port *port,
				 struct mlxsw_sp_span_entry *span_entry,
				 enum mlxsw_sp_span_type type,
				 bool bind)
{
	struct mlxsw_sp_span_inspected_port *inspected_port;
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char sbib_pl[MLXSW_REG_SBIB_LEN];

	inspected_port = mlxsw_sp_span_entry_bound_port_find(port, span_entry);
	if (!inspected_port)
		return;

	if (bind)
		mlxsw_sp_span_inspected_port_bind(port, span_entry, type,
						  false);
	/* remove the SBIB buffer if it was egress SPAN */
	if (type == MLXSW_SP_SPAN_EGRESS) {
		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, 0);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
	}

	mlxsw_sp_span_entry_put(mlxsw_sp, span_entry);

	list_del(&inspected_port->list);
	kfree(inspected_port);
}

static const struct mlxsw_sp_span_entry_ops *
mlxsw_sp_span_entry_ops(struct mlxsw_sp *mlxsw_sp,
			const struct net_device *to_dev)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_span_entry_types); ++i)
		if (mlxsw_sp_span_entry_types[i]->can_handle(to_dev))
			return mlxsw_sp_span_entry_types[i];

	return NULL;
}

int mlxsw_sp_span_mirror_add(struct mlxsw_sp_port *from,
			     const struct net_device *to_dev,
			     enum mlxsw_sp_span_type type, bool bind,
			     int *p_span_id)
{
	struct mlxsw_sp *mlxsw_sp = from->mlxsw_sp;
	const struct mlxsw_sp_span_entry_ops *ops;
	struct mlxsw_sp_span_parms sparms = {0};
	struct mlxsw_sp_span_entry *span_entry;
	int err;

	ops = mlxsw_sp_span_entry_ops(mlxsw_sp, to_dev);
	if (!ops) {
		netdev_err(to_dev, "Cannot mirror to %s", to_dev->name);
		return -EOPNOTSUPP;
	}

	err = ops->parms(to_dev, &sparms);
	if (err)
		return err;

	span_entry = mlxsw_sp_span_entry_get(mlxsw_sp, to_dev, ops, sparms);
	if (!span_entry)
		return -ENOENT;

	netdev_dbg(from->dev, "Adding inspected port to SPAN entry %d\n",
		   span_entry->id);

	err = mlxsw_sp_span_inspected_port_add(from, span_entry, type, bind);
	if (err)
		goto err_port_bind;

	*p_span_id = span_entry->id;
	return 0;

err_port_bind:
	mlxsw_sp_span_entry_put(mlxsw_sp, span_entry);
	return err;
}

void mlxsw_sp_span_mirror_del(struct mlxsw_sp_port *from, int span_id,
			      enum mlxsw_sp_span_type type, bool bind)
{
	struct mlxsw_sp_span_entry *span_entry;

	span_entry = mlxsw_sp_span_entry_find_by_id(from->mlxsw_sp, span_id);
	if (!span_entry) {
		netdev_err(from->dev, "no span entry found\n");
		return;
	}

	netdev_dbg(from->dev, "removing inspected port from SPAN entry %d\n",
		   span_entry->id);
	mlxsw_sp_span_inspected_port_del(from, span_entry, type, bind);
}

void mlxsw_sp_span_respin(struct mlxsw_sp *mlxsw_sp)
{
	int i;
	int err;

	ASSERT_RTNL();
	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];
		struct mlxsw_sp_span_parms sparms = {0};

		if (!curr->ref_count)
			continue;

		err = curr->ops->parms(curr->to_dev, &sparms);
		if (err)
			continue;

		if (memcmp(&sparms, &curr->parms, sizeof(sparms))) {
			mlxsw_sp_span_entry_deconfigure(curr);
			mlxsw_sp_span_entry_configure(mlxsw_sp, curr, sparms);
		}
	}
}
