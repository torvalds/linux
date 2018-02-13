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

	for (i = 0; i < mlxsw_sp->span.entries_count; i++)
		INIT_LIST_HEAD(&mlxsw_sp->span.entries[i].bound_ports_list);

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

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_create(struct mlxsw_sp_port *port)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	struct mlxsw_sp_span_entry *span_entry;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	u8 local_port = port->local_port;
	int index;
	int i;
	int err;

	/* find a free entry to use */
	index = -1;
	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		if (!mlxsw_sp->span.entries[i].ref_count) {
			index = i;
			span_entry = &mlxsw_sp->span.entries[i];
			break;
		}
	}
	if (index < 0)
		return NULL;

	/* create a new port analayzer entry for local_port */
	mlxsw_reg_mpat_pack(mpat_pl, index, local_port, true);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
	if (err)
		return NULL;

	span_entry->id = index;
	span_entry->ref_count = 1;
	span_entry->local_port = local_port;
	return span_entry;
}

static void mlxsw_sp_span_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_span_entry *span_entry)
{
	u8 local_port = span_entry->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find(struct mlxsw_sp *mlxsw_sp, u8 local_port)
{
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		if (curr->ref_count && curr->local_port == local_port)
			return curr;
	}
	return NULL;
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_get(struct mlxsw_sp_port *port)
{
	struct mlxsw_sp_span_entry *span_entry;

	span_entry = mlxsw_sp_span_entry_find(port->mlxsw_sp,
					      port->local_port);
	if (span_entry) {
		/* Already exists, just take a reference */
		span_entry->ref_count++;
		return span_entry;
	}

	return mlxsw_sp_span_entry_create(port);
}

static int mlxsw_sp_span_entry_put(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_span_entry *span_entry)
{
	WARN_ON(!span_entry->ref_count);
	if (--span_entry->ref_count == 0)
		mlxsw_sp_span_entry_destroy(mlxsw_sp, span_entry);
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

int mlxsw_sp_span_mirror_add(struct mlxsw_sp_port *from,
			     struct mlxsw_sp_port *to,
			     enum mlxsw_sp_span_type type, bool bind)
{
	struct mlxsw_sp *mlxsw_sp = from->mlxsw_sp;
	struct mlxsw_sp_span_entry *span_entry;
	int err;

	span_entry = mlxsw_sp_span_entry_get(to);
	if (!span_entry)
		return -ENOENT;

	netdev_dbg(from->dev, "Adding inspected port to SPAN entry %d\n",
		   span_entry->id);

	err = mlxsw_sp_span_inspected_port_add(from, span_entry, type, bind);
	if (err)
		goto err_port_bind;

	return 0;

err_port_bind:
	mlxsw_sp_span_entry_put(mlxsw_sp, span_entry);
	return err;
}

void mlxsw_sp_span_mirror_del(struct mlxsw_sp_port *from, u8 destination_port,
			      enum mlxsw_sp_span_type type, bool bind)
{
	struct mlxsw_sp_span_entry *span_entry;

	span_entry = mlxsw_sp_span_entry_find(from->mlxsw_sp,
					      destination_port);
	if (!span_entry) {
		netdev_err(from->dev, "no span entry found\n");
		return;
	}

	netdev_dbg(from->dev, "removing inspected port from SPAN entry %d\n",
		   span_entry->id);
	mlxsw_sp_span_inspected_port_del(from, span_entry, type, bind);
}
