/*
 * drivers/net/ethernet/mellanox/mlxsw/core.c
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Elad Raz <eladr@mellanox.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/if_link.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/u64_stats_sync.h>
#include <linux/netdevice.h>

#include "core.h"
#include "item.h"
#include "cmd.h"
#include "port.h"
#include "trap.h"

static LIST_HEAD(mlxsw_core_driver_list);
static DEFINE_SPINLOCK(mlxsw_core_driver_list_lock);

static const char mlxsw_core_driver_name[] = "mlxsw_core";

static struct dentry *mlxsw_core_dbg_root;

struct mlxsw_core_pcpu_stats {
	u64			trap_rx_packets[MLXSW_TRAP_ID_MAX];
	u64			trap_rx_bytes[MLXSW_TRAP_ID_MAX];
	u64			port_rx_packets[MLXSW_PORT_MAX_PORTS];
	u64			port_rx_bytes[MLXSW_PORT_MAX_PORTS];
	struct u64_stats_sync	syncp;
	u32			trap_rx_dropped[MLXSW_TRAP_ID_MAX];
	u32			port_rx_dropped[MLXSW_PORT_MAX_PORTS];
	u32			trap_rx_invalid;
	u32			port_rx_invalid;
};

struct mlxsw_core {
	struct mlxsw_driver *driver;
	const struct mlxsw_bus *bus;
	void *bus_priv;
	const struct mlxsw_bus_info *bus_info;
	struct list_head rx_listener_list;
	struct mlxsw_core_pcpu_stats __percpu *pcpu_stats;
	struct dentry *dbg_dir;
	struct {
		struct debugfs_blob_wrapper vsd_blob;
		struct debugfs_blob_wrapper psid_blob;
	} dbg;
	unsigned long driver_priv[0];
	/* driver_priv has to be always the last item */
};

struct mlxsw_rx_listener_item {
	struct list_head list;
	struct mlxsw_rx_listener rxl;
	void *priv;
};

/*****************
 * Core functions
 *****************/

static int mlxsw_core_rx_stats_dbg_read(struct seq_file *file, void *data)
{
	struct mlxsw_core *mlxsw_core = file->private;
	struct mlxsw_core_pcpu_stats *p;
	u64 rx_packets, rx_bytes;
	u64 tmp_rx_packets, tmp_rx_bytes;
	u32 rx_dropped, rx_invalid;
	unsigned int start;
	int i;
	int j;
	static const char hdr[] =
		"     NUM   RX_PACKETS     RX_BYTES RX_DROPPED\n";

	seq_printf(file, hdr);
	for (i = 0; i < MLXSW_TRAP_ID_MAX; i++) {
		rx_packets = 0;
		rx_bytes = 0;
		rx_dropped = 0;
		for_each_possible_cpu(j) {
			p = per_cpu_ptr(mlxsw_core->pcpu_stats, j);
			do {
				start = u64_stats_fetch_begin(&p->syncp);
				tmp_rx_packets = p->trap_rx_packets[i];
				tmp_rx_bytes = p->trap_rx_bytes[i];
			} while (u64_stats_fetch_retry(&p->syncp, start));

			rx_packets += tmp_rx_packets;
			rx_bytes += tmp_rx_bytes;
			rx_dropped += p->trap_rx_dropped[i];
		}
		seq_printf(file, "trap %3d %12llu %12llu %10u\n",
			   i, rx_packets, rx_bytes, rx_dropped);
	}
	rx_invalid = 0;
	for_each_possible_cpu(j) {
		p = per_cpu_ptr(mlxsw_core->pcpu_stats, j);
		rx_invalid += p->trap_rx_invalid;
	}
	seq_printf(file, "trap INV                           %10u\n",
		   rx_invalid);

	for (i = 0; i < MLXSW_PORT_MAX_PORTS; i++) {
		rx_packets = 0;
		rx_bytes = 0;
		rx_dropped = 0;
		for_each_possible_cpu(j) {
			p = per_cpu_ptr(mlxsw_core->pcpu_stats, j);
			do {
				start = u64_stats_fetch_begin(&p->syncp);
				tmp_rx_packets = p->port_rx_packets[i];
				tmp_rx_bytes = p->port_rx_bytes[i];
			} while (u64_stats_fetch_retry(&p->syncp, start));

			rx_packets += tmp_rx_packets;
			rx_bytes += tmp_rx_bytes;
			rx_dropped += p->port_rx_dropped[i];
		}
		seq_printf(file, "port %3d %12llu %12llu %10u\n",
			   i, rx_packets, rx_bytes, rx_dropped);
	}
	rx_invalid = 0;
	for_each_possible_cpu(j) {
		p = per_cpu_ptr(mlxsw_core->pcpu_stats, j);
		rx_invalid += p->port_rx_invalid;
	}
	seq_printf(file, "port INV                           %10u\n",
		   rx_invalid);
	return 0;
}

static int mlxsw_core_rx_stats_dbg_open(struct inode *inode, struct file *f)
{
	struct mlxsw_core *mlxsw_core = inode->i_private;

	return single_open(f, mlxsw_core_rx_stats_dbg_read, mlxsw_core);
}

static const struct file_operations mlxsw_core_rx_stats_dbg_ops = {
	.owner = THIS_MODULE,
	.open = mlxsw_core_rx_stats_dbg_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

static void mlxsw_core_buf_dump_dbg(struct mlxsw_core *mlxsw_core,
				    const char *buf, size_t size)
{
	__be32 *m = (__be32 *) buf;
	int i;
	int count = size / sizeof(__be32);

	for (i = count - 1; i >= 0; i--)
		if (m[i])
			break;
	i++;
	count = i ? i : 1;
	for (i = 0; i < count; i += 4)
		dev_dbg(mlxsw_core->bus_info->dev, "%04x - %08x %08x %08x %08x\n",
			i * 4, be32_to_cpu(m[i]), be32_to_cpu(m[i + 1]),
			be32_to_cpu(m[i + 2]), be32_to_cpu(m[i + 3]));
}

int mlxsw_core_driver_register(struct mlxsw_driver *mlxsw_driver)
{
	spin_lock(&mlxsw_core_driver_list_lock);
	list_add_tail(&mlxsw_driver->list, &mlxsw_core_driver_list);
	spin_unlock(&mlxsw_core_driver_list_lock);
	return 0;
}
EXPORT_SYMBOL(mlxsw_core_driver_register);

void mlxsw_core_driver_unregister(struct mlxsw_driver *mlxsw_driver)
{
	spin_lock(&mlxsw_core_driver_list_lock);
	list_del(&mlxsw_driver->list);
	spin_unlock(&mlxsw_core_driver_list_lock);
}
EXPORT_SYMBOL(mlxsw_core_driver_unregister);

static struct mlxsw_driver *__driver_find(const char *kind)
{
	struct mlxsw_driver *mlxsw_driver;

	list_for_each_entry(mlxsw_driver, &mlxsw_core_driver_list, list) {
		if (strcmp(mlxsw_driver->kind, kind) == 0)
			return mlxsw_driver;
	}
	return NULL;
}

static struct mlxsw_driver *mlxsw_core_driver_get(const char *kind)
{
	struct mlxsw_driver *mlxsw_driver;

	spin_lock(&mlxsw_core_driver_list_lock);
	mlxsw_driver = __driver_find(kind);
	if (!mlxsw_driver) {
		spin_unlock(&mlxsw_core_driver_list_lock);
		request_module(MLXSW_MODULE_ALIAS_PREFIX "%s", kind);
		spin_lock(&mlxsw_core_driver_list_lock);
		mlxsw_driver = __driver_find(kind);
	}
	if (mlxsw_driver) {
		if (!try_module_get(mlxsw_driver->owner))
			mlxsw_driver = NULL;
	}

	spin_unlock(&mlxsw_core_driver_list_lock);
	return mlxsw_driver;
}

static void mlxsw_core_driver_put(const char *kind)
{
	struct mlxsw_driver *mlxsw_driver;

	spin_lock(&mlxsw_core_driver_list_lock);
	mlxsw_driver = __driver_find(kind);
	spin_unlock(&mlxsw_core_driver_list_lock);
	if (!mlxsw_driver)
		return;
	module_put(mlxsw_driver->owner);
}

static int mlxsw_core_debugfs_init(struct mlxsw_core *mlxsw_core)
{
	const struct mlxsw_bus_info *bus_info = mlxsw_core->bus_info;

	mlxsw_core->dbg_dir = debugfs_create_dir(bus_info->device_name,
						 mlxsw_core_dbg_root);
	if (!mlxsw_core->dbg_dir)
		return -ENOMEM;
	debugfs_create_file("rx_stats", S_IRUGO, mlxsw_core->dbg_dir,
			    mlxsw_core, &mlxsw_core_rx_stats_dbg_ops);
	mlxsw_core->dbg.vsd_blob.data = (void *) &bus_info->vsd;
	mlxsw_core->dbg.vsd_blob.size = sizeof(bus_info->vsd);
	debugfs_create_blob("vsd", S_IRUGO, mlxsw_core->dbg_dir,
			    &mlxsw_core->dbg.vsd_blob);
	mlxsw_core->dbg.psid_blob.data = (void *) &bus_info->psid;
	mlxsw_core->dbg.psid_blob.size = sizeof(bus_info->psid);
	debugfs_create_blob("psid", S_IRUGO, mlxsw_core->dbg_dir,
			    &mlxsw_core->dbg.psid_blob);
	return 0;
}

static void mlxsw_core_debugfs_fini(struct mlxsw_core *mlxsw_core)
{
	debugfs_remove_recursive(mlxsw_core->dbg_dir);
}

int mlxsw_core_bus_device_register(const struct mlxsw_bus_info *mlxsw_bus_info,
				   const struct mlxsw_bus *mlxsw_bus,
				   void *bus_priv)
{
	const char *device_kind = mlxsw_bus_info->device_kind;
	struct mlxsw_core *mlxsw_core;
	struct mlxsw_driver *mlxsw_driver;
	size_t alloc_size;
	int err;

	mlxsw_driver = mlxsw_core_driver_get(device_kind);
	if (!mlxsw_driver)
		return -EINVAL;
	alloc_size = sizeof(*mlxsw_core) + mlxsw_driver->priv_size;
	mlxsw_core = kzalloc(alloc_size, GFP_KERNEL);
	if (!mlxsw_core) {
		err = -ENOMEM;
		goto err_core_alloc;
	}

	INIT_LIST_HEAD(&mlxsw_core->rx_listener_list);
	mlxsw_core->driver = mlxsw_driver;
	mlxsw_core->bus = mlxsw_bus;
	mlxsw_core->bus_priv = bus_priv;
	mlxsw_core->bus_info = mlxsw_bus_info;

	mlxsw_core->pcpu_stats =
		netdev_alloc_pcpu_stats(struct mlxsw_core_pcpu_stats);
	if (!mlxsw_core->pcpu_stats) {
		err = -ENOMEM;
		goto err_alloc_stats;
	}
	err = mlxsw_bus->init(bus_priv, mlxsw_core, mlxsw_driver->profile);
	if (err)
		goto err_bus_init;

	err = mlxsw_driver->init(mlxsw_core->driver_priv, mlxsw_core,
				 mlxsw_bus_info);
	if (err)
		goto err_driver_init;

	err = mlxsw_core_debugfs_init(mlxsw_core);
	if (err)
		goto err_debugfs_init;

	return 0;

err_debugfs_init:
	mlxsw_core->driver->fini(mlxsw_core->driver_priv);
err_driver_init:
	mlxsw_bus->fini(bus_priv);
err_bus_init:
	free_percpu(mlxsw_core->pcpu_stats);
err_alloc_stats:
	kfree(mlxsw_core);
err_core_alloc:
	mlxsw_core_driver_put(device_kind);
	return err;
}
EXPORT_SYMBOL(mlxsw_core_bus_device_register);

void mlxsw_core_bus_device_unregister(struct mlxsw_core *mlxsw_core)
{
	const char *device_kind = mlxsw_core->bus_info->device_kind;

	mlxsw_core_debugfs_fini(mlxsw_core);
	mlxsw_core->driver->fini(mlxsw_core->driver_priv);
	mlxsw_core->bus->fini(mlxsw_core->bus_priv);
	free_percpu(mlxsw_core->pcpu_stats);
	kfree(mlxsw_core);
	mlxsw_core_driver_put(device_kind);
}
EXPORT_SYMBOL(mlxsw_core_bus_device_unregister);

static struct mlxsw_core *__mlxsw_core_get(void *driver_priv)
{
	return container_of(driver_priv, struct mlxsw_core, driver_priv);
}

int mlxsw_core_skb_transmit(void *driver_priv, struct sk_buff *skb,
			    const struct mlxsw_tx_info *tx_info)
{
	struct mlxsw_core *mlxsw_core = __mlxsw_core_get(driver_priv);

	return mlxsw_core->bus->skb_transmit(mlxsw_core->bus_priv, skb,
					     tx_info);
}
EXPORT_SYMBOL(mlxsw_core_skb_transmit);

static bool __is_rx_listener_equal(const struct mlxsw_rx_listener *rxl_a,
				   const struct mlxsw_rx_listener *rxl_b)
{
	return (rxl_a->func == rxl_b->func &&
		rxl_a->local_port == rxl_b->local_port &&
		rxl_a->trap_id == rxl_b->trap_id);
}

static struct mlxsw_rx_listener_item *
__find_rx_listener_item(struct mlxsw_core *mlxsw_core,
			const struct mlxsw_rx_listener *rxl,
			void *priv)
{
	struct mlxsw_rx_listener_item *rxl_item;

	list_for_each_entry(rxl_item, &mlxsw_core->rx_listener_list, list) {
		if (__is_rx_listener_equal(&rxl_item->rxl, rxl) &&
		    rxl_item->priv == priv)
			return rxl_item;
	}
	return NULL;
}

int mlxsw_core_rx_listener_register(struct mlxsw_core *mlxsw_core,
				    const struct mlxsw_rx_listener *rxl,
				    void *priv)
{
	struct mlxsw_rx_listener_item *rxl_item;

	rxl_item = __find_rx_listener_item(mlxsw_core, rxl, priv);
	if (rxl_item)
		return -EEXIST;
	rxl_item = kmalloc(sizeof(*rxl_item), GFP_KERNEL);
	if (!rxl_item)
		return -ENOMEM;
	rxl_item->rxl = *rxl;
	rxl_item->priv = priv;

	list_add_rcu(&rxl_item->list, &mlxsw_core->rx_listener_list);
	return 0;
}
EXPORT_SYMBOL(mlxsw_core_rx_listener_register);

void mlxsw_core_rx_listener_unregister(struct mlxsw_core *mlxsw_core,
				       const struct mlxsw_rx_listener *rxl,
				       void *priv)
{
	struct mlxsw_rx_listener_item *rxl_item;

	rxl_item = __find_rx_listener_item(mlxsw_core, rxl, priv);
	if (!rxl_item)
		return;
	list_del_rcu(&rxl_item->list);
	synchronize_rcu();
	kfree(rxl_item);
}
EXPORT_SYMBOL(mlxsw_core_rx_listener_unregister);

void mlxsw_core_skb_receive(struct mlxsw_core *mlxsw_core, struct sk_buff *skb,
			    struct mlxsw_rx_info *rx_info)
{
	struct mlxsw_rx_listener_item *rxl_item;
	const struct mlxsw_rx_listener *rxl;
	struct mlxsw_core_pcpu_stats *pcpu_stats;
	u8 local_port = rx_info->sys_port;
	bool found = false;

	dev_dbg_ratelimited(mlxsw_core->bus_info->dev, "%s: sys_port = %d, trap_id = 0x%x\n",
			    __func__, rx_info->sys_port, rx_info->trap_id);

	if ((rx_info->trap_id >= MLXSW_TRAP_ID_MAX) ||
	    (local_port >= MLXSW_PORT_MAX_PORTS))
		goto drop;

	rcu_read_lock();
	list_for_each_entry_rcu(rxl_item, &mlxsw_core->rx_listener_list, list) {
		rxl = &rxl_item->rxl;
		if ((rxl->local_port == MLXSW_PORT_DONT_CARE ||
		     rxl->local_port == local_port) &&
		    rxl->trap_id == rx_info->trap_id) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();
	if (!found)
		goto drop;

	pcpu_stats = this_cpu_ptr(mlxsw_core->pcpu_stats);
	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->port_rx_packets[local_port]++;
	pcpu_stats->port_rx_bytes[local_port] += skb->len;
	pcpu_stats->trap_rx_packets[rx_info->trap_id]++;
	pcpu_stats->trap_rx_bytes[rx_info->trap_id] += skb->len;
	u64_stats_update_end(&pcpu_stats->syncp);

	rxl->func(skb, local_port, rxl_item->priv);
	return;

drop:
	if (rx_info->trap_id >= MLXSW_TRAP_ID_MAX)
		this_cpu_inc(mlxsw_core->pcpu_stats->trap_rx_invalid);
	else
		this_cpu_inc(mlxsw_core->pcpu_stats->trap_rx_dropped[rx_info->trap_id]);
	if (local_port >= MLXSW_PORT_MAX_PORTS)
		this_cpu_inc(mlxsw_core->pcpu_stats->port_rx_invalid);
	else
		this_cpu_inc(mlxsw_core->pcpu_stats->port_rx_dropped[local_port]);
	dev_kfree_skb(skb);
}
EXPORT_SYMBOL(mlxsw_core_skb_receive);

int mlxsw_cmd_exec(struct mlxsw_core *mlxsw_core, u16 opcode, u8 opcode_mod,
		   u32 in_mod, bool out_mbox_direct,
		   char *in_mbox, size_t in_mbox_size,
		   char *out_mbox, size_t out_mbox_size)
{
	u8 status;
	int err;

	BUG_ON(in_mbox_size % sizeof(u32) || out_mbox_size % sizeof(u32));
	if (!mlxsw_core->bus->cmd_exec)
		return -EOPNOTSUPP;

	dev_dbg(mlxsw_core->bus_info->dev, "Cmd exec (opcode=%x(%s),opcode_mod=%x,in_mod=%x)\n",
		opcode, mlxsw_cmd_opcode_str(opcode), opcode_mod, in_mod);
	if (in_mbox) {
		dev_dbg(mlxsw_core->bus_info->dev, "Input mailbox:\n");
		mlxsw_core_buf_dump_dbg(mlxsw_core, in_mbox, in_mbox_size);
	}

	err = mlxsw_core->bus->cmd_exec(mlxsw_core->bus_priv, opcode,
					opcode_mod, in_mod, out_mbox_direct,
					in_mbox, in_mbox_size,
					out_mbox, out_mbox_size, &status);

	if (err == -EIO && status != MLXSW_CMD_STATUS_OK) {
		dev_err(mlxsw_core->bus_info->dev, "Cmd exec failed (opcode=%x(%s),opcode_mod=%x,in_mod=%x,status=%x(%s))\n",
			opcode, mlxsw_cmd_opcode_str(opcode), opcode_mod,
			in_mod, status, mlxsw_cmd_status_str(status));
	} else if (err == -ETIMEDOUT) {
		dev_err(mlxsw_core->bus_info->dev, "Cmd exec timed-out (opcode=%x(%s),opcode_mod=%x,in_mod=%x)\n",
			opcode, mlxsw_cmd_opcode_str(opcode), opcode_mod,
			in_mod);
	}

	if (!err && out_mbox) {
		dev_dbg(mlxsw_core->bus_info->dev, "Output mailbox:\n");
		mlxsw_core_buf_dump_dbg(mlxsw_core, out_mbox, out_mbox_size);
	}
	return err;
}
EXPORT_SYMBOL(mlxsw_cmd_exec);

static int __init mlxsw_core_module_init(void)
{
	mlxsw_core_dbg_root = debugfs_create_dir(mlxsw_core_driver_name, NULL);
	if (!mlxsw_core_dbg_root)
		return -ENOMEM;
	return 0;
}

static void __exit mlxsw_core_module_exit(void)
{
	debugfs_remove_recursive(mlxsw_core_dbg_root);
}

module_init(mlxsw_core_module_init);
module_exit(mlxsw_core_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox switch device core driver");
